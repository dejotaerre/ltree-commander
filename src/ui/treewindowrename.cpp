#include "ui/treewindow.h"
#include "ui/theme.h"
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace ltree {
void TreeWindow::connectRename()
{
    connect(&renameWatcher_,&QFutureWatcher<RenameResult>::finished,this,[this]{
        const auto result=renameWatcher_.result();busy_=false;
        if(result.renamed){
            ++renameCount_;session_.renamePath(result.source,result.target);
            if(otherSession_)otherSession_->renamePath(result.source,result.target);
            const auto updateHistory=[&](QMap<QString,Session> &history){
                QMap<QString,Session> mapped;
                for(auto item:history){item.renamePath(result.source,result.target);mapped.insert(item.root,item);}history=mapped;
            };
            updateHistory(rootHistory_);updateHistory(otherRootHistory_);updateHistory(loggedRoots_);
            if(isWithin(initialRoot_,result.source))initialRoot_=result.target+initialRoot_.mid(result.source.size());
        }
        session_.apply(result.scan,true);if(otherSession_)otherSession_->apply(result.scan,true);
        if(!result.error.isEmpty()){++renameFailed_;if(renameFirstError_.isEmpty())renameFirstError_=result.source+": "+result.error;}
        ++renameIndex_;
        if(result.cancelled || cancel_->load())finishRename(true);else nextRename();
    });
}
void TreeWindow::beginRename(bool tagged)
{
    stopAutoview();activeSpell_=false;renameTargets_.clear();renameNames_.clear();renameBatch_=tagged;
    if(tagged){for(const auto &file:session_.files())if(session_.tags.contains(file.path))renameTargets_.append(readMetadata(file.path));}
    else{
        const QString path=session_.view==View::Tree?session_.directory:session_.currentFile()?session_.currentFile()->path:QString{};
        if(!path.isEmpty())renameTargets_.append(readMetadata(path));
    }
    if(renameTargets_.isEmpty()){status_=tagged?"No tagged files in current list":"No item selected";return;}
    renamePrompt_=true;renameReview_=renameEach_=renameItemPrompt_=false;
    renameInput_=tagged?"*.*":QFileInfo(renameTargets_.first().path).fileName();renameSelected_=true;
    renameCursor_=int(renameInput_.size());renameCase_=CopyCase::Keep;
}
void TreeWindow::finishRename(bool cancelled)
{
    renamePrompt_=renameReview_=renameItemPrompt_=false;busy_=false;
    status_=QString("Rename %1: %2/%3 renamed; %4 failed or skipped").arg(cancelled?"cancelled":"completed")
        .arg(renameCount_).arg(renameTargets_.size()).arg(renameFailed_);
    if(!renameFirstError_.isEmpty())status_+="; "+renameFirstError_;
    refresh();
}
void TreeWindow::nextRename(bool confirmed)
{
    if(cancel_->load()){finishRename(true);return;}
    if(renameIndex_>=renameTargets_.size()){finishRename(false);return;}
    if(renameEach_&&!confirmed){renameItemPrompt_=true;refresh();return;}
    renameItemPrompt_=false;busy_=true;
    const auto item=renameTargets_[renameIndex_];const auto name=renameNames_[renameIndex_];const auto cancel=cancel_;
    status_=QString("RENAME %1/%2: %3").arg(renameIndex_+1).arg(renameTargets_.size()).arg(item.path);
    renameWatcher_.setFuture(QtConcurrent::run([item,name,cancel]{return renameItem(item,name,cancel);}));refresh();
}
void TreeWindow::renameKey(QKeyEvent *event)
{
    const int key=event->key();const bool ctrl=event->modifiers().testFlag(Qt::ControlModifier);
    const bool enter=key==Qt::Key_Return||key==Qt::Key_Enter;
    if(key==Qt::Key_Escape || ((renameReview_||renameItemPrompt_)&&key==Qt::Key_N&&!renameItemPrompt_)){
        if(renameReview_||renameItemPrompt_)finishRename(true);else {renamePrompt_=false;status_="Rename cancelled";refresh();}return;
    }
    if(renameItemPrompt_){
        if(key==Qt::Key_N){++renameFailed_;++renameIndex_;nextRename();}
        else if(key==Qt::Key_Y||enter||key==Qt::Key_A){if(key==Qt::Key_A)renameEach_=false;nextRename(true);}
        return;
    }
    if(renameReview_){
        if(key==Qt::Key_Backspace){renameReview_=false;refresh();}
        else if(key==Qt::Key_Y||enter){cancel_=std::make_shared<std::atomic_bool>(false);nextRename();}
        return;
    }
    if(enter){
        renameNames_.clear();QSet<QString> targets,sources;
        for(const auto &item:renameTargets_)sources.insert(item.path);
        for(const auto &item:renameTargets_){
            const auto name=renameBatch_?copyName(QFileInfo(item.path).fileName(),renameInput_,renameCase_,&status_):renameInput_;
            const auto target=QDir(QFileInfo(item.path).absolutePath()).filePath(name);
            if(!status_.isEmpty()){refresh();return;}
            if(name.isEmpty()||name=="."||name==".."||name.contains('/')||name.contains(QChar::Null))status_="Enter a single valid filename";
            else if(sources.contains(target))status_="A destination is also a source; unchanged names and swaps are not supported";
            else if(targets.contains(target))status_="Mask produces duplicate destinations";
            if(!status_.isEmpty()){refresh();return;}
            targets.insert(target);renameNames_.append(name);
        }
        renameReview_=true;renameIndex_=renameCount_=renameFailed_=0;renameFirstError_.clear();
    }else if(key==Qt::Key_F4 && renameBatch_)renameEach_=!renameEach_;
    else if(key==Qt::Key_Tab && renameBatch_)renameCase_=CopyCase((int(renameCase_)+1)%3);
    else if(key==Qt::Key_Left){renameCursor_=std::max(0,renameCursor_-1);renameSelected_=false;}
    else if(key==Qt::Key_Right){renameCursor_=std::min(int(renameInput_.size()),renameCursor_+1);renameSelected_=false;}
    else if(key==Qt::Key_Home){renameCursor_=0;renameSelected_=false;}
    else if(key==Qt::Key_End){renameCursor_=int(renameInput_.size());renameSelected_=false;}
    else if(key==Qt::Key_Backspace||key==Qt::Key_Delete){
        if(renameSelected_ || (ctrl&&key==Qt::Key_Backspace)){renameInput_.clear();renameCursor_=0;renameSelected_=false;}
        else if(key==Qt::Key_Backspace && renameCursor_>0)renameInput_.remove(--renameCursor_,1);
        else if(key==Qt::Key_Delete)renameInput_.remove(renameCursor_,1);
    }else{
        QString text=ctrl&&key==Qt::Key_V?QGuiApplication::clipboard()->text():!ctrl&&!event->modifiers().testFlag(Qt::AltModifier)?event->text():QString{};
        if(!text.isEmpty()){if(renameSelected_){renameInput_.clear();renameCursor_=0;renameSelected_=false;}renameInput_.insert(renameCursor_,text);renameCursor_+=int(text.size());}
    }
    refresh();
}
void TreeWindow::drawRename(QPainter &p)
{
    const int y=bottom()+1;p.fillRect(0,y*cellHeight_,width(),3*cellHeight_,theme::background);
    if(renameItemPrompt_){
        drawText(p,0,y,"RENAME: "+renameTargets_[renameIndex_].path,theme::labels);
        drawText(p,0,y+1,"as: "+renameNames_[renameIndex_],theme::normal);
        drawCommands(p,0,y+2,"[Y/Enter] Rename  [N] Skip  [A] All  [Esc] Cancel");return;
    }
    if(renameReview_){
        drawText(p,0,y,QString("RENAME %1 items; existing destinations will be skipped").arg(renameTargets_.size()),theme::labels);
        drawText(p,0,y+1,QFileInfo(renameTargets_.first().path).fileName()+" -> "+renameNames_.first(),theme::normal);
        drawCommands(p,0,y+2,busy_?"[Esc] Stop after current item":"[Y/Enter] Apply  [N/Esc] Cancel  [Backspace] Edit");return;
    }
    drawText(p,0,y,renameBatch_?QString("RENAME %1 tagged files as:").arg(renameTargets_.size()):"RENAME: "+renameTargets_.first().path,theme::labels);
    drawInput(p,0,y+1,"as: ",renameInput_,renameCursor_,theme::normal);
    if(status_.isEmpty())drawCommands(p,0,y+2,renameBatch_?QString("[Enter] Review  [F4] Confirm each (%1)  [Tab] Case (%2)  [Esc] Cancel")
        .arg(renameEach_?"yes":"no",QStringList{"keep","lower","upper"}[int(renameCase_)]):"[Enter] Review  [Esc] Cancel");
    else drawText(p,0,y+2,status_,theme::labels);
}
}
