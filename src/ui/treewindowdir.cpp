#include "ui/treewindow.h"
#include "ui/theme.h"
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace ltree {
void TreeWindow::beginGlobal(bool tagged)
{
    stopAutoview();
    loggedRoots_.insert(session_.root,session_);
    QVector<Session> sources;
    for(const auto &saved:loggedRoots_)sources.append(saved);
    if(otherSession_ && !otherGlobalReturn_)sources.append(*otherSession_);
    sources.append(session_);
    auto global=Session::global(sources,session_,tagged);
    if(global.files().isEmpty()){status_=tagged?"No matching tagged files in logged locations":"No matching files in logged locations";return;}
    globalReturn_=session_;globalScroll_={treeTop_,fileTop_};session_=std::move(global);fileTop_=0;
    globalRevision_=0;globalPublishedTags_.clear();
    activeSpell_=false;refresh();
}
void TreeWindow::publishGlobal()
{
    if(!globalReturn_)return;
    if(globalRevision_==session_.revision() && globalPublishedTags_==session_.tags)return;
    globalRevision_=session_.revision();globalPublishedTags_=session_.tags;
    globalReturn_->syncGlobalFrom(session_);
    for(auto &saved:rootHistory_)saved.syncGlobalFrom(session_);
    for(auto &saved:otherRootHistory_)saved.syncGlobalFrom(session_);
    for(auto &saved:loggedRoots_)saved.syncGlobalFrom(session_);
    if(otherSession_)otherSession_->syncGlobalFrom(session_);
    if(otherGlobalReturn_)otherGlobalReturn_->syncGlobalFrom(session_);
}
void TreeWindow::endGlobal()
{
    if(!globalReturn_)return;
    publishGlobal();globalReturn_->filespec=session_.filespec;globalReturn_->sort=session_.sort;
    session_=*globalReturn_;globalReturn_.reset();session_.rebuild();
    treeTop_=globalScroll_.first;fileTop_=globalScroll_.second;
    availableBytes_=QStorageInfo(session_.root).bytesAvailable();
}
void TreeWindow::cycleLoggedRoot(int direction)
{
    // Como en el original, recorrer ubicaciones registradas evita acceder a montajes ajenos.
    endGlobal();
    QStringList roots=loggedRoots_.keys();
    if(!roots.contains(session_.root))roots.append(session_.root);
    if(otherSession_){const QString path=otherGlobalReturn_?otherGlobalReturn_->root:otherSession_->root;if(!roots.contains(path))roots.append(path);}
    std::sort(roots.begin(),roots.end());
    if(roots.size()<2){status_="Only one location logged; L logs another mount or directory";return;}
    const int index=int(roots.indexOf(session_.root)),count=int(roots.size());
    selectRoot(roots[(index+direction+count)%count]);
}
void TreeWindow::refreshAvailable()
{
    availCapacity_=availBytes_=-1;
    if(mounts_.isEmpty())return;
    const QStorageInfo storage(mounts_[mountIndex_].path);
    if(storage.isValid() && storage.isReady()){availCapacity_=storage.bytesTotal();availBytes_=storage.bytesAvailable();}
}
void TreeWindow::connectLink()
{
    connect(&linkWatcher_,&QFutureWatcher<SymlinkResult>::finished,this,[this]{
        const auto result=linkWatcher_.result();busy_=false;
        if(result.created){
            linkPrompt_=false;session_.apply(result.scan,true);if(otherSession_)otherSession_->apply(result.scan,true);
            for(auto &saved:rootHistory_)saved.apply(result.scan,true);
            for(auto &saved:otherRootHistory_)saved.apply(result.scan,true);
            for(auto &saved:loggedRoots_)saved.apply(result.scan,true);
            status_="Symbolic link created: "+result.path;if(!result.error.isEmpty())status_+="; "+result.error;
        }else if(result.cancelled){linkPrompt_=false;status_="Shortcut cancelled";}
        else status_=result.error;
        refresh();
    });
}
void TreeWindow::beginLink()
{
    stopAutoview();linkSource_=readMetadata(session_.directory);
    if(!linkSource_.error.isEmpty() || !linkSource_.directory || linkSource_.symlink){status_="Select a real directory to create its symbolic link";return;}
    const QString name=QFileInfo(session_.directory).fileName();
    const QString base=otherSession_?otherSession_->directory:QFileInfo(session_.directory).absolutePath();
    linkInput_=QDir(base).filePath(name.isEmpty()?"root-link":name+(base==QFileInfo(session_.directory).absolutePath()?"-link":""));
    linkCursor_=int(linkInput_.size());linkSelected_=true;linkPrompt_=true;activeSpell_=false;
}
void TreeWindow::linkKey(QKeyEvent *event)
{
    const int key=event->key();const auto mods=event->modifiers();
    const bool ctrl=mods.testFlag(Qt::ControlModifier),alt=mods.testFlag(Qt::AltModifier);
    if(key==Qt::Key_Escape){linkPrompt_=false;status_="Shortcut cancelled";}
    else if(key==Qt::Key_Return || key==Qt::Key_Enter){
        if(linkInput_.isEmpty() || linkInput_.contains(QChar::Null)){status_="Enter the path of the new symbolic link";refresh();return;}
        QString input=linkInput_;if(input=="~")input=QDir::homePath();else if(input.startsWith("~/"))input=QDir::homePath()+input.mid(1);
        const QString path=QDir::cleanPath(QDir(linkSource_.path).absoluteFilePath(input));
        const auto source=linkSource_;cancel_=std::make_shared<std::atomic_bool>(false);const auto cancel=cancel_;
        busy_=true;status_="Creating symbolic link…";
        linkWatcher_.setFuture(QtConcurrent::run([source,path,cancel]{return createDirectoryLink(source,path,cancel);}));
    }else if(key==Qt::Key_Left){linkCursor_=std::max(0,linkCursor_-1);linkSelected_=false;}
    else if(key==Qt::Key_Right){linkCursor_=std::min(int(linkInput_.size()),linkCursor_+1);linkSelected_=false;}
    else if(key==Qt::Key_Home){linkCursor_=0;linkSelected_=false;}
    else if(key==Qt::Key_End){linkCursor_=int(linkInput_.size());linkSelected_=false;}
    else if(key==Qt::Key_Backspace || key==Qt::Key_Delete){
        if(linkSelected_ || (ctrl && key==Qt::Key_Backspace)){linkInput_.clear();linkCursor_=0;linkSelected_=false;}
        else if(key==Qt::Key_Backspace && linkCursor_>0)linkInput_.remove(--linkCursor_,1);
        else if(key==Qt::Key_Delete)linkInput_.remove(linkCursor_,1);
    }else{
        QString text;
        if((ctrl && key==Qt::Key_V) || (key==Qt::Key_Insert && mods.testFlag(Qt::ShiftModifier)))text=QGuiApplication::clipboard()->text();
        else if(!ctrl && !alt && !event->text().isEmpty() && event->text()[0].isPrint())text=event->text();
        if(!text.isEmpty()){
            if(linkSelected_){linkInput_.clear();linkCursor_=0;linkSelected_=false;}
            text.remove('\n');text.remove('\r');text=text.left(4096-linkInput_.size());linkInput_.insert(linkCursor_,text);linkCursor_+=int(text.size());
        }
    }
    refresh();
}
void TreeWindow::drawLink(QPainter &p)
{
    const int y=bottom()+1;p.fillRect(0,y*cellHeight_,width(),3*cellHeight_,theme::background);
    const QString label="SHORTCUT directory: ";
    const int space=std::max(3,columns_-int(label.size()));
    drawText(p,0,y,label+(linkSource_.path.size()>space?"..."+linkSource_.path.right(space-3):linkSource_.path),theme::labels);
    drawInput(p,0,y+1,"Create link at: ",linkInput_,linkCursor_,theme::normal);
    if(linkSelected_){
        const int prefix=15,start=std::max(0,linkCursor_-(columns_-prefix)+1);
        const QString visible=linkInput_.mid(start).left(columns_-prefix);
        p.fillRect(prefix*cellWidth_,(y+1)*cellHeight_,visible.size()*cellWidth_,cellHeight_,theme::selection);
        drawText(p,prefix,y+1,visible,Qt::black);drawCursor(p,prefix+linkCursor_-start,y+1,linkInput_.mid(linkCursor_,1));
    }
    drawCommands(p,0,y+2,"[Enter] Create symbolic link   [Esc] Cancel");
    if(!status_.isEmpty()){p.fillRect(0,y*cellHeight_,width(),cellHeight_,theme::background);drawText(p,0,y,status_,theme::normal);}
}
}
