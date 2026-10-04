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
void TreeWindow::connectDirectoryCompare()
{
    connect(&duplicateWatcher_,&QFutureWatcher<DirectoryCompareResult>::finished,this,[this]{
        const auto result=duplicateWatcher_.result();busy_=false;duplicatePrompt_=false;
        if(result.cancelled)status_="Compare filter cancelled; list retained";
        else if(!result.errors.isEmpty())status_="Compare filter failed; list retained: "+result.errors.first();
        else{
            session_.setCompareFilter(result.matches,duplicateFilter_==CompareFilter::Size||duplicateFilter_==CompareFilter::Content?SortKey::Size:SortKey::Name);
            status_=QString("Compare filter: %1 files; Ctrl+T tags this list; Alt+F4, A restores all").arg(result.matches.size());
        }
        refresh();
    });
    connect(&directoryCompareWatcher_,&QFutureWatcher<DirectoryCompareResult>::finished,this,[this]{
        const auto result=directoryCompareWatcher_.result();busy_=false;directoryComparePrompt_=false;
        if(result.cancelled)status_="Directory comparison cancelled; tags retained";
        else if(!result.errors.isEmpty())status_="Directory comparison failed; tags retained: "+result.errors.first();
        else{
            for(const auto &file:directoryCompareFiles_)session_.tags.remove(file.path);
            session_.tags.unite(result.matches);session_.rebuild();
            status_=QString("Directory comparison completed: %1/%2 files tagged").arg(result.matches.size()).arg(directoryCompareFiles_.size());
        }
        refresh();
    });
}
void TreeWindow::duplicateKey(QKeyEvent *event)
{
    const int key=event->key();
    if(key==Qt::Key_Escape){duplicatePrompt_=false;refresh();return;}
    if(key==Qt::Key_F2){duplicateCase_=!duplicateCase_;refresh();return;}
    if(key==Qt::Key_0){duplicateEmpty_=!duplicateEmpty_;refresh();return;}
    if(key==Qt::Key_A){session_.clearCompareFilter();duplicatePrompt_=false;refresh();return;}
    switch(key){
    case Qt::Key_D:duplicateFilter_=CompareFilter::Duplicate;break;
    case Qt::Key_U:duplicateFilter_=CompareFilter::Unique;break;
    case Qt::Key_Z:duplicateFilter_=CompareFilter::Size;break;
    case Qt::Key_C:duplicateFilter_=CompareFilter::Content;break;
    case Qt::Key_I:duplicateFilter_=CompareFilter::IdenticalDates;break;
    case Qt::Key_N:duplicateFilter_=CompareFilter::Newest;break;
    case Qt::Key_O:duplicateFilter_=CompareFilter::Oldest;break;
    default:return;
    }
    const auto files=session_.files();const auto filter=duplicateFilter_;const bool letterCase=duplicateCase_,empty=duplicateEmpty_;
    busy_=true;cancel_=std::make_shared<std::atomic_bool>(false);const auto cancel=cancel_;
    status_="Building compare filter…";
    duplicateWatcher_.setFuture(QtConcurrent::run([files,filter,letterCase,empty,cancel]{return filterDuplicates(files,filter,letterCase,empty,cancel);}));refresh();
}
void TreeWindow::drawDuplicates(QPainter &p)
{
    drawText(p,1,0,"COMPARE FILTER — current list only",theme::labels);horizontal(p,1,0,columns_-1);
    drawCommands(p,2,4,"[D]uplicate names      [U]nique names");
    drawCommands(p,2,6,"si[Z]e                [C]ontent (binary)");
    drawCommands(p,2,8,"[I]dentical dates      [N]ewest dates      [O]ldest dates");
    drawCommands(p,2,10,"[A]ll files (clear filter)");
    drawCommands(p,2,12,QString("[F2] Case sensitive names (%1)  [0] Include empty files (%2)").arg(duplicateCase_?"yes":"no",duplicateEmpty_?"yes":"no"));
    drawText(p,2,15,"Each choice narrows the current list; A restores all matching files.",theme::labels);
    drawText(p,2,16,"Ctrl+T tags the resulting list; leaving Files restores the sort.",theme::labels);
    horizontal(p,bottom(),0,columns_-1);drawCommands(p,0,bottom()+1,busy_?"[Esc] Cancel; previous list retained":"[Esc] Cancel");
    drawText(p,0,bottom()+3,status_,theme::labels);
}
void TreeWindow::beginDirectoryCompare(bool branch)
{
    stopAutoview();activeSpell_=false;directoryCompareFiles_.clear();directoryCompareSource_=session_.directory;
    for(const auto &dir:session_.directories())if(branch?isWithin(dir.path,session_.directory):dir.path==session_.directory){
        if(!dir.error.isEmpty()){status_="Relog the source before comparing: "+dir.error;return;}
        if(dir.loaded)for(const auto &file:dir.files)if(session_.filespec.matches(file))directoryCompareFiles_.append(file);
    }
    if(directoryCompareFiles_.isEmpty()){status_="No matching logged source files; use L or * first";return;}
    directoryCompareBranch_=branch;directoryComparePrompt_=true;directoryCompareReview_=false;directoryCompareSelected_=true;
    directoryCompareInput_=otherSession_?otherSession_->directory:session_.directory;
    directoryCompareCursor_=int(directoryCompareInput_.size());directoryCompareOptions_={};
}
void TreeWindow::directoryCompareKey(QKeyEvent *event)
{
    const int key=event->key();const bool ctrl=event->modifiers().testFlag(Qt::ControlModifier);
    const bool enter=key==Qt::Key_Return||key==Qt::Key_Enter;
    if(key==Qt::Key_Escape){directoryComparePrompt_=false;status_="Directory comparison cancelled";refresh();return;}
    if(directoryCompareReview_){
        if(key==Qt::Key_F2)directoryCompareOptions_.identical=(directoryCompareOptions_.identical+1)%4;
        else if(key==Qt::Key_U)directoryCompareOptions_.unique=!directoryCompareOptions_.unique;
        else if(key==Qt::Key_N)directoryCompareOptions_.newer=!directoryCompareOptions_.newer;
        else if(key==Qt::Key_O)directoryCompareOptions_.older=!directoryCompareOptions_.older;
        else if(key==Qt::Key_S)directoryCompareOptions_.size=(directoryCompareOptions_.size+1)%4;
        else if(key==Qt::Key_B)directoryCompareOptions_.binary=(directoryCompareOptions_.binary+1)%3;
        else if(key==Qt::Key_C)directoryCompareOptions_.caseSensitive=!directoryCompareOptions_.caseSensitive;
        else if(key==Qt::Key_F12)directoryCompareOptions_={};
        else if(key==Qt::Key_Backspace)directoryCompareReview_=false;
        else if(enter){
            const auto options=directoryCompareOptions_;const auto files=directoryCompareFiles_;const auto source=directoryCompareSource_;
            const auto destination=directoryCompareInput_;const bool branch=directoryCompareBranch_;
            busy_=true;cancel_=std::make_shared<std::atomic_bool>(false);const auto cancel=cancel_;
            status_="Comparing directories…";
            directoryCompareWatcher_.setFuture(QtConcurrent::run([files,source,destination,branch,options,cancel]{
                const auto scan=scanDirectories(destination,branch,cancel);DirectoryCompareResult result;QVector<FileEntry> target;
                for(const auto &dir:scan.directories){if(!dir.error.isEmpty())result.errors.append(dir.path+": "+dir.error);target+=dir.files;}
                if(scan.cancelled){result.cancelled=true;return result;}
                if(!result.errors.isEmpty())return result;
                return compareDirectories(files,source,target,destination,options,cancel);
            }));
        }
    }else if(enter){
        const QString path=QDir::cleanPath(QDir::isAbsolutePath(directoryCompareInput_)?directoryCompareInput_:QDir(directoryCompareSource_).filePath(directoryCompareInput_));
        const QFileInfo target(path);
        if(directoryCompareInput_.isEmpty()||directoryCompareInput_.contains(QChar::Null)||!target.isDir()||target.isSymLink())status_="Enter an existing directory; directory links are not supported";
        else{directoryCompareInput_=path;directoryCompareReview_=true;}
    }else if(key==Qt::Key_Left){directoryCompareCursor_=std::max(0,directoryCompareCursor_-1);directoryCompareSelected_=false;}
    else if(key==Qt::Key_Right){directoryCompareCursor_=std::min(int(directoryCompareInput_.size()),directoryCompareCursor_+1);directoryCompareSelected_=false;}
    else if(key==Qt::Key_Home){directoryCompareCursor_=0;directoryCompareSelected_=false;}
    else if(key==Qt::Key_End){directoryCompareCursor_=int(directoryCompareInput_.size());directoryCompareSelected_=false;}
    else if(key==Qt::Key_Backspace||key==Qt::Key_Delete){
        if(directoryCompareSelected_||(ctrl&&key==Qt::Key_Backspace)){directoryCompareInput_.clear();directoryCompareCursor_=0;directoryCompareSelected_=false;}
        else if(key==Qt::Key_Backspace&&directoryCompareCursor_>0)directoryCompareInput_.remove(--directoryCompareCursor_,1);
        else if(key==Qt::Key_Delete)directoryCompareInput_.remove(directoryCompareCursor_,1);
    }else{
        const QString text=ctrl&&key==Qt::Key_V?QGuiApplication::clipboard()->text():!ctrl&&!event->modifiers().testFlag(Qt::AltModifier)?event->text():QString{};
        if(!text.isEmpty()){if(directoryCompareSelected_){directoryCompareInput_.clear();directoryCompareCursor_=0;directoryCompareSelected_=false;}directoryCompareInput_.insert(directoryCompareCursor_,text);directoryCompareCursor_+=int(text.size());}
    }
    refresh();
}
void TreeWindow::drawDirectoryCompare(QPainter &p)
{
    drawText(p,1,0,directoryCompareBranch_?"COMPARE LOGGED BRANCH":"COMPARE DIRECTORY",theme::labels);horizontal(p,1,0,columns_-1);
    drawText(p,1,3,"Source: "+directoryCompareSource_,theme::labels);
    if(directoryCompareReview_)drawText(p,1,5,"Target: "+directoryCompareInput_,theme::labels);
    else drawInput(p,1,5,"with: ",directoryCompareInput_,directoryCompareCursor_,theme::normal);
    drawText(p,1,7,QString("%1 matching logged source files; target read from disk").arg(directoryCompareFiles_.size()),theme::labels);
    drawText(p,1,8,"Successful comparison replaces tags in this source scope only.",theme::labels);
    if(directoryCompareReview_){
        drawCommands(p,1,10,"[F2] Identical: "+QStringList{"off","size","date","size and date"}[directoryCompareOptions_.identical]);
        drawCommands(p,1,11,QString("[U] Unique: %1    [N] Newer: %2    [O] Older: %3").arg(directoryCompareOptions_.unique?"yes":"no",directoryCompareOptions_.newer?"yes":"no",directoryCompareOptions_.older?"yes":"no"));
        drawCommands(p,1,12,"[S] Size: "+QStringList{"off","smaller","larger","different"}[directoryCompareOptions_.size]);
        drawCommands(p,1,13,"[B] Binary: "+QStringList{"off","same","different"}[directoryCompareOptions_.binary]);
        drawCommands(p,1,14,QString("[C] Case sensitive names: %1").arg(directoryCompareOptions_.caseSensitive?"yes":"no"));
        drawText(p,1,16,"Metadata tests use OR; Binary uses AND; Unique uses OR.",theme::labels);
        drawText(p,1,17,"Branch comparison matches relative paths; links are not followed.",theme::labels);
    }
    horizontal(p,bottom(),0,columns_-1);
    drawCommands(p,0,bottom()+1,busy_?"[Esc] Cancel; tags retained":directoryCompareReview_?"[Enter] Compare  [Backspace] Target  [F12] Defaults  [Esc] Cancel":"[Enter] Options  [Esc] Cancel");
    drawText(p,0,bottom()+3,status_,theme::labels);
}
}
