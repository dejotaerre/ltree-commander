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
void TreeWindow::connectCopy()
{
    connect(&copyWatcher_,&QFutureWatcher<CopyResult>::finished,this,[this]{
        const auto result=copyWatcher_.result();busy_=false;
        // Actualizar también las ubicaciones guardadas evita recuperar listados anteriores al cambiar de panel.
        const auto reconcile=[&](Session &saved){
            if(result.moved)saved.removeFile(copyEntries_[copyIndex_].source);
            if(!result.scan.directories.isEmpty())saved.apply(result.scan,true);
        };
        reconcile(session_);
        if(otherSession_)reconcile(*otherSession_);
        if(globalReturn_)reconcile(*globalReturn_);
        if(otherGlobalReturn_)reconcile(*otherGlobalReturn_);
        for(auto &saved:rootHistory_)reconcile(saved);
        for(auto &saved:otherRootHistory_)reconcile(saved);
        for(auto &saved:loggedRoots_)reconcile(saved);
        for(const auto &directory:result.scan.directories)if(!directory.error.isEmpty() && copyFirstError_.isEmpty())copyFirstError_="Read error: "+directory.error;
        if(copyMove_?result.moved:result.copied)++copyCount_;
        if(result.cancelled || cancel_->load()){finishCopy(true);return;}
        if(!result.error.isEmpty()){
            copyError_=result.error;if(copyFirstError_.isEmpty())copyFirstError_=result.error;
            if(result.moved){advanceCopy();return;}
            copyStep_=CopyStep::Error;refresh();return;
        }
        if(result.exists){copyStep_=CopyStep::Exists;refresh();return;}
        if(result.skipped)++copySkipped_;
        advanceCopy();
    });
}
void TreeWindow::beginCopy(bool single,bool move,bool flat)
{
    copySingle_=single;copyMove_=move;copyFlat_=flat;copyFiles_.clear();
    if(single){if(session_.currentFile())copyFiles_.append(*session_.currentFile());}
    else for(const auto &file:session_.files())if(session_.tags.contains(file.path))copyFiles_.append(file);
    if(copyFiles_.isEmpty()){status_=single?"No file selected":"No tagged files in the current list";return;}
    stopAutoview();copySourceDirectory_=single?QFileInfo(copyFiles_.first().path).absolutePath():session_.directory;copySourceRoot_=session_.root;
    copyMask_="*.*";copyInput_=single?copyFiles_.first().name:copyMask_;
    copyCursor_=int(copyInput_.size());copyInputSelected_=true;copyHistoryIndex_=-1;
    copyConfirm_=false;copyCase_=CopyCase::Keep;copyPaths_=CopyPaths::Full;copyReplace_=CopyReplace::Ask;
    copyFirstError_.clear();copyError_.clear();copyIndex_=copyCount_=copySkipped_=0;copyEntries_.clear();copyStep_=CopyStep::Mask;
}
void TreeWindow::prepareCopyPaths()
{
    if(copySingle_){copyPaths_=CopyPaths::Relative;prepareCopyPlan();return;}
    if(copyFlat_){copyPaths_=CopyPaths::Flat;copyStep_=CopyStep::Replace;return;}
    copyStep_=session_.view==View::Global || session_.view==View::Showall || copySourceDirectory_=="/"?CopyStep::Replace:CopyStep::Paths;
}
void TreeWindow::prepareCopyPlan()
{
    const auto plan=planCopy(copyFiles_,copySourceDirectory_,copyDestination_,copyPaths_,copyMask_,copyCase_);
    if(!plan.error.isEmpty()){status_=plan.error;return;}
    copyEntries_=plan.entries;cancel_=std::make_shared<std::atomic_bool>(false);startCopyItem();
}
void TreeWindow::startCopyItem(bool confirmed,bool replace)
{
    if(copyConfirm_ && !confirmed){copyStep_=CopyStep::Confirm;refresh();return;}
    copyStep_=CopyStep::Running;busy_=true;
    const auto entry=copyEntries_[copyIndex_];const auto root=copySourceRoot_;const auto cancel=cancel_;
    const auto policy=replace?CopyReplace::Always:copyReplace_;
    const bool move=copyMove_;
    copyWatcher_.setFuture(QtConcurrent::run([root,entry,policy,cancel,move]{return move?moveFile(root,entry,policy,cancel):copyFile(root,entry,policy,cancel);}));
    refresh();
}
void TreeWindow::advanceCopy()
{
    if(++copyIndex_<copyEntries_.size())startCopyItem();else finishCopy(false);
}
void TreeWindow::finishCopy(bool cancelled)
{
    busy_=false;copyStep_=CopyStep::None;
    status_=QString("%1 %2: %3/%4 files %5; %6 skipped")
        .arg(copyMove_?"Move":"Copy",cancelled?"cancelled":"completed").arg(copyCount_).arg(copyFiles_.size()).arg(copyMove_?"moved":"copied").arg(copySkipped_);
    if(!copyFirstError_.isEmpty())status_+="; first error: "+copyFirstError_;
    refresh();
}
void TreeWindow::copyPromptKey(QKeyEvent *event)
{
    const int key=event->key();const bool enter=key==Qt::Key_Return || key==Qt::Key_Enter;
    if(key==Qt::Key_Escape){finishCopy(true);return;}
    if(copyStep_==CopyStep::Paths){
        if(key==Qt::Key_P)copyPaths_=CopyPaths((int(copyPaths_)+1)%3);
        else if(key==Qt::Key_F)copyPaths_=CopyPaths::Full;
        else if(key==Qt::Key_C)copyPaths_=CopyPaths::Current;
        else if(key==Qt::Key_R)copyPaths_=CopyPaths::Relative;
        else if(enter)copyStep_=CopyStep::Replace;
    }else if(copyStep_==CopyStep::Replace){
        if(key==Qt::Key_Y)copyReplace_=CopyReplace::Always;
        else if(key==Qt::Key_N || enter)copyReplace_=CopyReplace::Ask;
        else if(key==Qt::Key_O)copyReplace_=CopyReplace::Older;
        else if(key==Qt::Key_V)copyReplace_=CopyReplace::Never;
        else if(key==Qt::Key_R)copyReplace_=CopyReplace::Rename;
        else{refresh();return;}
        prepareCopyPlan();
    }else if(copyStep_==CopyStep::MakePath){
        if(key==Qt::Key_Y || enter)prepareCopyPaths();else if(key==Qt::Key_N)finishCopy(true);
    }else if(copyStep_==CopyStep::Confirm || copyStep_==CopyStep::Exists){
        const bool exists=copyStep_==CopyStep::Exists;
        if(key==Qt::Key_Y || enter)startCopyItem(true,exists);
        else if(key==Qt::Key_N){++copySkipped_;advanceCopy();}
        else if(key==Qt::Key_A){if(exists)copyReplace_=CopyReplace::Always;else copyConfirm_=false;startCopyItem(true,exists);}
    }else if(copyStep_==CopyStep::Error){
        if(key==Qt::Key_R || enter)startCopyItem(true);
        else if(key==Qt::Key_S){++copySkipped_;advanceCopy();}
    }else if(copyStep_==CopyStep::Mask || copyStep_==CopyStep::Destination){
        const bool mask=copyStep_==CopyStep::Mask;
        const bool ctrl=event->modifiers().testFlag(Qt::ControlModifier),alt=event->modifiers().testFlag(Qt::AltModifier);
        auto &history=mask?copyMaskHistory_:copyDestinationHistory_;
        if(enter){
            if(mask){
                const QString maskValue=copyInput_.isEmpty() || (copySingle_ && copyInput_==copyFiles_.first().name)?"*.*":copyInput_;
                QString error;for(const auto &file:copyFiles_){copyName(file.name,maskValue,copyCase_,&error);if(!error.isEmpty())break;}
                if(!error.isEmpty()){status_=error;refresh();return;}
                copyMask_=maskValue;
                history.removeAll(copyMask_);history.prepend(copyMask_);
                copyInput_=otherSession_?otherSession_->directory:copySourceDirectory_;copyStep_=CopyStep::Destination;
                copyInputSelected_=true;copyCursor_=int(copyInput_.size());copyHistoryIndex_=-1;
            }else{
                if(copyInput_.isEmpty() || copyInput_.contains(QChar(0))){status_="Enter the destination directory";refresh();return;}
                const auto input=copyInput_.startsWith("~/")?QDir::homePath()+copyInput_.mid(1):copyInput_;
                copyDestination_=QDir::cleanPath(QDir(copySourceDirectory_).absoluteFilePath(input));
                const QFileInfo info(copyDestination_);
                if(info.exists() && (!info.isDir() || info.isSymLink())){status_="Destination must be a directory; links are not followed";refresh();return;}
                history.removeAll(copyDestination_);history.prepend(copyDestination_);
                if(info.exists())prepareCopyPaths();else copyStep_=CopyStep::MakePath;
            }
            if(history.size()>64)history.removeLast();
        }else if(mask && key==Qt::Key_F4 && !copySingle_)copyConfirm_=!copyConfirm_;
        else if(mask && key==Qt::Key_Tab)copyCase_=CopyCase((int(copyCase_)+1)%3);
        else if(!mask && key==Qt::Key_Tab && otherSession_){copyInput_=otherSession_->directory;copyCursor_=int(copyInput_.size());copyInputSelected_=true;}
        else if(key==Qt::Key_F3 || key==Qt::Key_Up || key==Qt::Key_Down){
            if(!history.isEmpty()){
                copyHistoryIndex_=key==Qt::Key_F3?0:std::clamp(copyHistoryIndex_+(key==Qt::Key_Down?-1:1),0,int(history.size())-1);
                copyInput_=history[copyHistoryIndex_];copyCursor_=int(copyInput_.size());copyInputSelected_=true;
            }
        }else if(key==Qt::Key_Left){copyCursor_=std::max(0,copyCursor_-1);copyInputSelected_=false;}
        else if(key==Qt::Key_Right){copyCursor_=std::min(int(copyInput_.size()),copyCursor_+1);copyInputSelected_=false;}
        else if(key==Qt::Key_Home){copyCursor_=0;copyInputSelected_=false;}
        else if(key==Qt::Key_End){copyCursor_=int(copyInput_.size());copyInputSelected_=false;}
        else if(key==Qt::Key_Backspace || key==Qt::Key_Delete){
            if(copyInputSelected_ || (key==Qt::Key_Backspace && ctrl)){copyInput_.clear();copyCursor_=0;copyInputSelected_=false;}
            else if(key==Qt::Key_Backspace && copyCursor_>0)copyInput_.remove(--copyCursor_,1);
            else if(key==Qt::Key_Delete)copyInput_.remove(copyCursor_,1);
        }else{
            QString text;
            if((ctrl && key==Qt::Key_V) || (key==Qt::Key_Insert && event->modifiers().testFlag(Qt::ShiftModifier)))text=QGuiApplication::clipboard()->text();
            else if(!ctrl && !alt && !event->text().isEmpty() && event->text().at(0).isPrint())text=event->text();
            if(!text.isEmpty()){
                if(copyInputSelected_){copyInput_.clear();copyCursor_=0;copyInputSelected_=false;}
                text.remove('\n');text.remove('\r');text=text.left(4096-copyInput_.size());
                copyInput_.insert(copyCursor_,text);copyCursor_+=int(text.size());
            }
        }
    }
    refresh();
}
void TreeWindow::drawCopyPrompt(QPainter &p)
{
    const int y=bottom()+1;
    const QString operation=copyMove_?"MOVE":"COPY";
    p.fillRect(0,y*cellHeight_,width(),3*cellHeight_,theme::background);
    const QString caseLabel=QStringList{"unchanged","lower","UPPER"}[int(copyCase_)];
    const auto pathLine=[&](int row,const QString &label,const QString &path,const QColor &color){
        const int available=std::max(3,columns_-int(label.size()));
        drawText(p,0,row,label+(path.size()>available?"..."+path.right(available-3):path),color);
    };
    if(copyStep_==CopyStep::Mask || copyStep_==CopyStep::Destination){
        const bool mask=copyStep_==CopyStep::Mask;
        const bool singleMask=mask && copySingle_;
        const QString label=singleMask?"     as: ":mask?QString(copyFlat_?"%1 %2 tagged files as: ":"Duplicate paths and %1 %2 tagged files as: ").arg(operation).arg(copyFiles_.size()):operation+" to: ";
        const int inputRow=singleMask?y+1:y;
        if(singleMask)pathLine(y,operation+" file: ",copyFiles_.first().name,theme::labels);
        drawInput(p,0,inputRow,label,copyInput_,copyCursor_,theme::normal);
        drawText(p,0,inputRow,label,theme::labels,columns_-1);
        if(copyInputSelected_){
            const int prefix=std::min(int(label.size()),columns_-1),start=std::max(0,copyCursor_-(columns_-prefix)+1);
            const auto visible=copyInput_.mid(start).left(columns_-prefix);
            p.fillRect(prefix*cellWidth_,inputRow*cellHeight_,visible.size()*cellWidth_,cellHeight_,theme::selection);
            drawText(p,prefix,inputRow,visible,Qt::black);drawCursor(p,prefix+copyCursor_-start,inputRow,copyInput_.mid(copyCursor_,1));
        }
        if(singleMask)drawCommands(p,0,y+2,QString("[Enter] OK  [Tab] Case (%1)  [F3] Last  [Up/Down] History  [Esc] Cancel").arg(caseLabel));
        else{
            drawCommands(p,0,y+1,mask?QString("[F4] Confirm (%1)   [Tab] Change case (%2)").arg(copyConfirm_?"yes":"no",caseLabel):"[Tab] Other panel   [F3] Last   [Up/Down] History");
            drawCommands(p,0,y+2,mask?"[Enter] Filename mask   [F3] Last   [Up/Down] History   [Esc] Cancel":"[Enter] Destination directory   [Esc] Cancel");
        }
    }else if(copyStep_==CopyStep::Paths){
        pathLine(y,operation+" to: ",copyDestination_,theme::labels);
        drawText(p,0,y+1,"Source: Paths are ("+QStringList{"full","current","relative"}[int(copyPaths_)]+")",theme::normal);
        drawCommands(p,0,y+2,"[F] Full   [C] Current   [R] Relative   [P] Cycle   [Enter] OK   [Esc] Cancel");
    }else if(copyStep_==CopyStep::MakePath || copyStep_==CopyStep::Replace){
        pathLine(y,operation+" to: ",copyDestination_,theme::labels);
        drawText(p,0,y+1,copyStep_==CopyStep::MakePath?"Directory does not exist. Make new path?":"Automatically replace existing files?",theme::normal);
        drawCommands(p,0,y+2,copyStep_==CopyStep::MakePath?"[Y]/[Enter] Yes   [N]/[Esc] Cancel":"[Y] Yes  [N]/[Enter] Confirm each  [O] Older  ne[V]er  [R] Rename Seq  [Esc] Cancel");
    }else if(!copyEntries_.isEmpty()){
        const auto &entry=copyEntries_[copyIndex_];
        pathLine(y,QString("%1 %2/%3: ").arg(operation).arg(copyIndex_+1).arg(copyEntries_.size()),entry.source,theme::labels);
        if(copyStep_==CopyStep::Error)drawText(p,0,y+1,copyError_,theme::normal);
        else pathLine(y+1,copyStep_==CopyStep::Exists?"File exists, replace? ":copyStep_==CopyStep::Confirm?(copyMove_?"Confirm move? ":"Confirm copy? "):"To: ",entry.target,theme::normal);
        drawCommands(p,0,y+2,copyStep_==CopyStep::Error?"[R]/[Enter] Retry   [S] Skip   [Esc] Cancel":copyStep_==CopyStep::Running?(copyMove_?"[Esc] Stop pending moves":"[Esc] Stop pending copies"):"[Y]/[Enter] Yes   [N] Skip   [A] All   [Esc] Cancel");
    }
    if(!status_.isEmpty()){
        const int row=copySingle_ && copyStep_==CopyStep::Mask?y+2:y+1;
        p.fillRect(0,row*cellHeight_,width(),cellHeight_,theme::background);drawText(p,0,row,status_,theme::normal);
    }
}
}
