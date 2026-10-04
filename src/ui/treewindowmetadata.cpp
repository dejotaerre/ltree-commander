#include "ui/treewindow.h"
#include "ui/theme.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace ltree {

void TreeWindow::connectMetadata()
{
    connect(&permissionsBatchWatcher_,&QFutureWatcher<PermissionBatchResult>::finished,this,[this]{
        const auto batch=permissionsBatchWatcher_.result();busy_=false;
        permissionsPrompt_=permissionsReview_=false;
        int changed=0,failed=0;QString firstError;
        for(const auto &item:batch.items){
            if(item.second.changed){
                ++changed;session_.setFileWritable(item.first.path,item.second.writable);
                if(otherSession_)otherSession_->setFileWritable(item.first.path,item.second.writable);
            }else if(!item.second.error.isEmpty()){
                ++failed;if(firstError.isEmpty())firstError=item.first.path+": "+item.second.error;
            }
        }
        status_=QString("Permissions %1: %2/%3 changed; %4 failed").arg(batch.cancelled?"cancelled":"completed")
            .arg(changed).arg(permissionsTargets_.size()).arg(failed);
        if(!firstError.isEmpty())status_+="; "+firstError;
        refresh();
    });
    connect(&permissionsWatcher_,&QFutureWatcher<PermissionResult>::finished,this,[this]{
        const auto result=permissionsWatcher_.result();busy_=false;
        permissionsPrompt_=permissionsReview_=false;
        status_=result.changed?"Permissions changed to "+permissionOctal(result.mode)+" ("+permissionText(result.mode)+")":
            result.cancelled?"Permissions cancelled":"Permissions not changed: "+result.error;
        if(result.changed && permissionsBefore_.regular){
            session_.setFileWritable(permissionsBefore_.path,result.writable);
            if(otherSession_)otherSession_->setFileWritable(permissionsBefore_.path,result.writable);
        }
        if(infoVisible_)refreshInfo();
        refresh();
    });
}

void TreeWindow::refreshInfo()
{
    const QString path=session_.view==View::Tree?session_.directory:
        session_.currentFile()?session_.currentFile()->path:QString{};
    info_=readMetadata(path);
}
void TreeWindow::beginInfo()
{
    if(infoVisible_){infoVisible_=false;return;}
    if(preview_)autoviewReturn_=session_.view;
    stopAutoview();infoVisible_=true;activeSpell_=false;refreshInfo();
}
QRect TreeWindow::infoRect() const
{
    const int width=std::min(46,isSplit()?paneWidth(1)-2:columns_-2);
    const int height=info_.error.isEmpty()?(info_.symlink?16:15):5;
    return QRect(columns_-width-1,std::max(2,bottom()-height+1),width,height);
}

void TreeWindow::drawInfo(QPainter &p)
{
    const auto box=infoRect();
    const int left=box.left(),top=box.top(),right=box.right(),end=box.bottom();
    p.fillRect(left*cellWidth_,top*cellHeight_,box.width()*cellWidth_,box.height()*cellHeight_,theme::background);
    QString border="┌"+QString(box.width()-2,QChar(0x2500))+"┐";
    const QString title=session_.view==View::Tree?" Directory information ":" File information ";
    border.replace(2,std::min(int(title.size()),box.width()-4),title.left(box.width()-4));
    drawText(p,left,top,border,theme::labels,box.width());
    drawText(p,left,end,"└"+QString(box.width()-2,QChar(0x2500))+"┘",theme::labels,box.width());
    for(int row=top+1;row<end;++row){
        drawText(p,left,row,"│",theme::labels,1);drawText(p,right,row,"│",theme::labels,1);
    }
    const auto field=[&](int row,const QString &label,const QString &value){
        drawText(p,left+2,top+row,label+":",theme::labels,14);
        drawText(p,left+16,top+row,value,theme::normal,box.width()-18);
    };
    if(!info_.error.isEmpty()){
        drawText(p,left+2,top+1,info_.path.isEmpty()?"No file selected":QFileInfo(info_.path).fileName(),theme::normal,box.width()-4);
        for(int line=0;line<2;++line)drawText(p,left+2,top+2+line,info_.error.mid(line*(box.width()-4),box.width()-4),theme::normal,box.width()-4);
        return;
    }
    const auto date=[](const QDateTime &value){return value.isValid()?value.toString("yyyy-MM-dd HH:mm:ss"):QString("Unavailable");};
    field(1,"Created",date(info_.created));
    field(2,"Last Write",date(info_.modified));
    field(3,"Last Access",date(info_.accessed));
    field(4,"Changed",date(info_.changed));
    field(5,"Size (Bytes)",QString::number(info_.size));
    field(6,"Permissions",permissionText(info_.mode)+" "+permissionOctal(info_.mode));
    field(7,"Name",QFileInfo(info_.path).fileName());
    field(8,"Type",info_.type);
    field(9,"Owner / UID",(info_.owner.isEmpty()?"Unknown":info_.owner)+" / "+QString::number(info_.uid));
    field(10,"Group / GID",(info_.group.isEmpty()?"Unknown":info_.group)+" / "+QString::number(info_.gid));
    field(11,"Hard links",QString::number(info_.links));
    field(12,"Allocated",QString::number(info_.allocated)+" bytes");
    field(13,"Device/inode",QString::number(info_.device)+" / "+QString::number(info_.inode));
    if(info_.symlink)field(14,"Link target",info_.linkTarget);
}

void TreeWindow::beginPermissions(bool tagged)
{
    stopAutoview();activeSpell_=false;
    permissionsBatch_=tagged;permissionsTargets_.clear();permissionsModes_.clear();
    if(tagged){
        for(const auto &file:session_.files())if(session_.tags.contains(file.path))permissionsTargets_.append(readMetadata(file.path));
        if(permissionsTargets_.isEmpty()){status_="No tagged files in the current list";return;}
        permissionsBefore_=permissionsTargets_.first();
        permissionsPrompt_=true;permissionsReview_=false;permissionsInput_.clear();permissionsCursor_=0;return;
    }
    const QString path=session_.view==View::Tree?session_.directory:session_.currentFile()?session_.currentFile()->path:QString{};
    if(path.isEmpty()){status_="No file selected";return;}
    permissionsBefore_=readMetadata(path);
    if(!permissionsBefore_.error.isEmpty()){status_="Cannot read permissions: "+permissionsBefore_.error;return;}
    const bool tree=session_.view==View::Tree;
    if((tree?!permissionsBefore_.directory:!permissionsBefore_.regular) || permissionsBefore_.symlink){
        status_=tree?"Selected item is not a directory; symbolic links are not supported":
            "Selected item is not a regular file; symbolic links are not supported";return;
    }
    permissionsPrompt_=true;permissionsReview_=false;permissionsInput_.clear();permissionsCursor_=0;
}
void TreeWindow::permissionsKey(QKeyEvent *event)
{
    const auto key=event->key();const auto mods=event->modifiers();
    const bool ctrl=mods.testFlag(Qt::ControlModifier),alt=mods.testFlag(Qt::AltModifier);
    const bool enter=key==Qt::Key_Return || key==Qt::Key_Enter;
    if(key==Qt::Key_Escape || (permissionsReview_&&key==Qt::Key_N)){
        permissionsPrompt_=permissionsReview_=false;status_="Permissions cancelled";
    }else if(permissionsReview_){
        if(key==Qt::Key_Backspace){permissionsReview_=false;}
        else if(key==Qt::Key_Y || enter){
            busy_=true;cancel_=std::make_shared<std::atomic_bool>(false);
            if(permissionsBatch_){
                const auto targets=permissionsTargets_;const auto modes=permissionsModes_;const auto cancel=cancel_;
                status_="Changing tagged file permissions…";
                permissionsBatchWatcher_.setFuture(QtConcurrent::run([targets,modes,cancel]{
                    PermissionBatchResult batch;
                    for(int i=0;i<targets.size();++i){
                        if(cancel->load()){batch.cancelled=true;break;}
                        auto result=changePermissions(targets[i],modes[i],cancel);
                        batch.items.append({targets[i],result});
                        if(result.cancelled){batch.cancelled=true;break;}
                    }
                    return batch;
                }));refresh();return;
            }
            const auto before=permissionsBefore_;const auto mode=permissionsMode_;const auto cancel=cancel_;
            status_="Changing permissions…";
            permissionsWatcher_.setFuture(QtConcurrent::run([before,mode,cancel]{return changePermissions(before,mode,cancel);}));
        }
    }else if(enter){
        if(permissionsBatch_){
            permissionsModes_.clear();
            for(const auto &target:permissionsTargets_){
                quint32 mode=0;
                if(!parsePermissions(permissionsInput_,target.mode,false,mode,status_)){refresh();return;}
                permissionsModes_.append(mode);
            }
            permissionsReview_=true;refresh();return;
        }
        if(parsePermissions(permissionsInput_,permissionsBefore_.mode,permissionsBefore_.directory,permissionsMode_,status_)){
            if(permissionsMode_==permissionsBefore_.mode){permissionsPrompt_=false;status_="Permissions already match; no change made";}
            else permissionsReview_=true;
        }
    }else if(key==Qt::Key_V && ctrl){const auto value=QGuiApplication::clipboard()->text();permissionsInput_.insert(permissionsCursor_,value);permissionsCursor_+=int(value.size());}
    else if(key==Qt::Key_A && ctrl){permissionsInput_.clear();permissionsCursor_=0;}
    else if(key==Qt::Key_Backspace && permissionsCursor_>0)permissionsInput_.remove(--permissionsCursor_,1);
    else if(key==Qt::Key_Delete)permissionsInput_.remove(permissionsCursor_,1);
    else if(key==Qt::Key_Left)permissionsCursor_=std::max(0,permissionsCursor_-1);
    else if(key==Qt::Key_Right)permissionsCursor_=std::min(int(permissionsInput_.size()),permissionsCursor_+1);
    else if(key==Qt::Key_Home)permissionsCursor_=0;
    else if(key==Qt::Key_End)permissionsCursor_=int(permissionsInput_.size());
    else if(!ctrl&&!alt&&!event->text().isEmpty()){permissionsInput_.insert(permissionsCursor_,event->text());permissionsCursor_+=int(event->text().size());}
    if(infoVisible_ && !permissionsPrompt_)refreshInfo();
    refresh();
}
void TreeWindow::drawPermissions(QPainter &p)
{
    const int y=bottom()+1;
    p.fillRect(0,y*cellHeight_,width(),3*cellHeight_,theme::background);
    const QString label=permissionsBatch_?QString("PERMISSIONS %1 tagged files: ").arg(permissionsTargets_.size()):permissionsBefore_.directory?"PERMISSIONS directory: ":"PERMISSIONS file: ";
    const int room=std::max(3,columns_-int(label.size()));
    const auto path=permissionsBefore_.path.size()>room?"..."+permissionsBefore_.path.right(room-3):permissionsBefore_.path;
    drawText(p,0,y,label+path,theme::labels);
    if(permissionsBatch_ && permissionsReview_){
        drawText(p,0,y+1,QString("Apply %1 to %2 tagged files in current list; tags retained").arg(permissionsInput_).arg(permissionsTargets_.size()),theme::normal);
        drawCommands(p,0,y+2,busy_?"[Esc] Stop after current file":"[Y/Enter] Apply  [N/Esc] Cancel  [Backspace] Edit");return;
    }
    if(permissionsReview_){
        drawText(p,0,y+1,permissionOctal(permissionsBefore_.mode)+" ("+permissionText(permissionsBefore_.mode)+") -> "+
            permissionOctal(permissionsMode_)+" ("+permissionText(permissionsMode_)+")  "+
            (permissionsBefore_.directory?"This directory only":"This file only"),theme::normal);
        drawCommands(p,0,y+2,busy_?"[Esc] Cancel pending change":
            "[Y/Enter] Apply  [N/Esc] Cancel  [Backspace] Edit");
    }else{
        drawInput(p,0,y+1,"Mode ("+permissionOctal(permissionsBefore_.mode)+"): ",permissionsInput_,permissionsCursor_,theme::normal);
        if(status_.isEmpty())drawCommands(p,0,y+2,"[Enter] Review  [Esc] Cancel  Examples: 755, u+w, go-w, u=rwx,g=rx,o=");
        else drawText(p,0,y+2,status_,theme::labels);
    }
}

}
