#include "ui/treewindow.h"
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
namespace ltree {
void TreeWindow::openViewer(bool tagged,bool alternate)
{
    const auto current=session_.currentFile();if(!current){status_="No file selected";return;}
    if(alternate){auto command=QProcess::splitCommand(qEnvironmentVariable("LTREE_ALT_VIEWER"));if(!command.isEmpty()){const auto executable=command.takeFirst();command.append(current->path);if(!QProcess::startDetached(executable,command,QFileInfo(current->path).absolutePath()))status_="Cannot start alternate viewer";return;}}
    QStringList paths;if(tagged)for(const auto &file:session_.files())if(session_.tags.contains(file.path))paths.append(file.path);
    if(tagged && paths.isEmpty()){status_="No tagged files in this view";return;}
    const QString path=tagged && !paths.contains(current->path)?paths.first():current->path;
    viewer_=new FileViewer(path,this);if(tagged)viewer_->setFiles(paths);if(lastExecutedSearch_)viewer_->setSearch(*lastExecutedSearch_);viewer_->setGeometry(rect());
    connect(viewer_,&FileViewer::closed,this,[this]{if(viewer_){viewer_->hide();viewer_->deleteLater();viewer_=nullptr;}setFocus();update();});
    connect(viewer_,&FileViewer::editRequested,this,[this](const QString &path){emit openRequested(path,true);});
    connect(viewer_,&FileViewer::alternateEditRequested,this,[this](const QString &path){
        auto command=QProcess::splitCommand(qEnvironmentVariable("LTREE_ALT_EDITOR"));
        if(command.isEmpty()){emit openRequested(path,true);return;}
        const auto executable=command.takeFirst();command.append(path);if(!QProcess::startDetached(executable,command,QFileInfo(path).absolutePath()) && viewer_)viewer_->showStatus("Cannot start alternate editor");
    });
    connect(viewer_,&FileViewer::untagRequested,this,[this](const QString &path){session_.tags.remove(path);session_.rebuild();refresh();});
    connect(viewer_,&FileViewer::executeRequested,this,[this](const QString &directory,bool separate){
        QString shell=qEnvironmentVariable("SHELL");if(shell.isEmpty())shell=QStandardPaths::findExecutable("bash");
        if(separate){if(!QProcess::startDetached("kitty",{"--directory",directory,shell,"-i"}) && viewer_)viewer_->showStatus("Cannot start Kitty shell");return;}
        const QPointer<FileViewer> origin=viewer_;execution_=new ExecuteWindow(directory,shell,{"-i"},viewer_);execution_->setGeometry(viewer_->rect());
        connect(execution_,&ExecuteWindow::closed,this,[this,origin]{if(execution_){execution_->hide();execution_->deleteLater();execution_=nullptr;}if(origin){origin->reload();origin->setFocus();}});execution_->show();execution_->raise();execution_->setFocus();
    });
    viewer_->show();viewer_->raise();viewer_->setFocus();
}
void TreeWindow::openArchive(bool create,bool tagged)
{
    stopAutoview();const auto current=session_.currentFile();if(!current){status_="No file selected";return;}
    const QString destination=otherSession_?otherSession_->directory:session_.directory;
    if(create){QStringList paths;if(tagged){for(const auto &file:session_.files())if(session_.tags.contains(file.path))paths.append(file.path);}else paths.append(current->path);
        if(paths.isEmpty()){status_="No tagged files in this view";return;}
        const QString base=session_.view==View::Global?session_.root:session_.directory;archive_=new ArchiveWindow(paths,base,destination,this);
    }else archive_=new ArchiveWindow(current->path,destination,this);
    if(lastExecutedSearch_)archive_->setSearch(*lastExecutedSearch_);
    archive_->setGeometry(rect());
    connect(archive_,&ArchiveWindow::closed,this,[this,create]{if(archive_){archive_->hide();archive_->deleteLater();archive_=nullptr;}setFocus();if(create)load(false,true);else refresh();});
    archive_->show();archive_->raise();archive_->setFocus();
}
}
