#include "ui/treewindow.h"
#include "ui/theme.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace ltree {
void TreeWindow::connectStamp()
{
    stampEditor_.setMaxLength(128);
    connect(&stampWatcher_,&QFutureWatcher<StampResult>::finished,this,[this]{
        const auto result=stampWatcher_.result();busy_=stampPrompt_=false;
        const auto reconcile=[&](Session &saved){if(!result.scan.directories.isEmpty())saved.apply(result.scan,true);};
        reconcile(session_);if(otherSession_)reconcile(*otherSession_);
        if(globalReturn_)reconcile(*globalReturn_);
        if(otherGlobalReturn_)reconcile(*otherGlobalReturn_);
        for(auto &saved:rootHistory_)reconcile(saved);
        for(auto &saved:otherRootHistory_)reconcile(saved);
        for(auto &saved:loggedRoots_)reconcile(saved);
        status_=QString("Stamp %1: %2/%3 changed; %4 unchanged; %5 failed")
            .arg(result.cancelled?"cancelled":"completed").arg(result.changed).arg(stampTargets_.size()).arg(result.unchanged).arg(result.failed);
        if(!result.error.isEmpty())status_+="; "+result.error;
        for(const auto &directory:result.scan.directories)if(!directory.error.isEmpty()){status_+="; read error: "+directory.error;break;}
        refresh();
    });
}
void TreeWindow::setStampInput(const QString &value,bool selected)
{
    stampEditor_.setText(value);stampEditor_.setCursorPosition(int(value.size()));
    if(selected)stampEditor_.selectAll();
    stampInput_=stampEditor_.text();stampCursor_=stampEditor_.cursorPosition();
    if(value.startsWith('+') || QRegularExpression("^-[0-9]+[dmyhns]").match(value).hasMatch() || value=="e"){
        if(stampMode_==StampMode::Set)stampMode_=StampMode::Adjust;
    }else if(!value.trimmed().isEmpty())stampMode_=StampMode::Set;
}
void TreeWindow::beginStamp(bool tagged)
{
    if(!session_.currentFile()){status_="No file selected";return;}
    stopAutoview();activeSpell_=false;stampTagged_=tagged;stampTargets_.clear();
    stampReference_=readMetadata(session_.currentFile()->path);
    if(tagged){for(const auto &file:session_.files())if(session_.tags.contains(file.path))stampTargets_.append(readMetadata(file.path));}
    else stampTargets_.append(stampReference_);
    if(stampTargets_.isEmpty()){status_="No tagged files in the current list";return;}
    stampPrompt_=true;stampHelp_=stampHistory_.visible=false;
    stampField_=StampField::Written;stampMode_=StampMode::Set;
    setStampInput(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),true);
}
void TreeWindow::stampKey(QKeyEvent *event)
{
    const int key=event->key();
    if(stampHistory_.visible){
        historyKey(event);if(!stampHistory_.visible)setStampInput(stampInput_);
        refresh();return;
    }
    if(stampHelp_){stampHelp_=helpDocumentKey(helpDocument_,event,columns_,rows_);refresh();return;}
    if(key==Qt::Key_Escape){stampPrompt_=false;status_="Stamp cancelled";}
    else if(key==Qt::Key_F1){helpDocument_.open(HelpTopic::Stamp);stampHelp_=true;}
    else if(key==Qt::Key_F2){stampMode_=StampMode::Set;setStampInput(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),true);}
    else if(key==Qt::Key_Tab || key==Qt::Key_Backtab){
        stampMode_=StampMode::Set;
        const auto value=stampField_==StampField::Accessed?stampReference_.accessed:stampReference_.modified;
        if(value.isValid())setStampInput(value.toString("yyyy-MM-dd HH:mm:ss"),true);
        else status_="Original timestamp is unavailable";
    }else if(key==Qt::Key_F3){
        if(stampHistory_.entries.isEmpty())status_="No New date history items";
        else{
            setStampInput(stampHistory_.entries.first(),true);
        }
    }else if(key==Qt::Key_F4)stampField_=StampField((int(stampField_)+1)%3);
    else if(key==Qt::Key_F5){
        const auto previous=stampMode_;
        stampMode_=StampMode((int(stampMode_)+1)%(stampTagged_?3:2));
        setStampInput(stampMode_==StampMode::Set?QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"):previous==StampMode::Set?QString{}:stampInput_,true);
    }else if(key==Qt::Key_Up || key==Qt::Key_Down)openHistory(key==Qt::Key_Up);
    else if(key==Qt::Key_Return || key==Qt::Key_Enter){
        const auto plan=planStamp(stampTargets_,stampInput_,stampField_,stampMode_);
        if(!plan.error.isEmpty()){status_=plan.error;refresh();return;}
        rememberHistory(stampHistory_,stampInput_);saveHistory(stampHistory_);
        busy_=true;cancel_=std::make_shared<std::atomic_bool>(false);const auto cancel=cancel_;
        status_="Changing file timestamps… Esc cancels";
        stampWatcher_.setFuture(QtConcurrent::run([plan,cancel]{return stampFiles(plan,cancel);}));
    }else{
        QCoreApplication::sendEvent(&stampEditor_,event);
        stampInput_=stampEditor_.text();stampCursor_=stampEditor_.cursorPosition();
    }
    refresh();
}
void TreeWindow::drawStamp(QPainter &p)
{
    if(stampHelp_){drawHelp(p);return;}
    const int y=bottom()+1;
    p.fillRect(0,y*cellHeight_,width(),3*cellHeight_,theme::background);
    const auto field=QStringList{"written","accessed","both"}[int(stampField_)];
    const auto mode=QStringList{"set to","adjust","increment"}[int(stampMode_)];
    const auto original=stampField_==StampField::Accessed?stampReference_.accessed:stampReference_.modified;
    drawText(p,0,y,stampTagged_?QString("STAMP %1 tagged files").arg(stampTargets_.size()):"STAMP file: "+QFileInfo(stampReference_.path).fileName(),theme::labels);
    if(columns_>=80)drawText(p,columns_-21,y,original.toString("yyyy-MM-dd HH:mm:ss"),theme::normal,20);
    const QString label="  "+mode+": ";
    const int inputWidth=std::min(43,columns_-38);
    drawInput(p,0,y+1,label,stampInput_,stampCursor_,theme::normal,inputWidth);
    const int selection=stampEditor_.selectionStart();
    if(selection>=0){
        const int length=int(stampEditor_.selectedText().size()),left=int(label.size());
        const int room=inputWidth-left,start=std::max(0,stampCursor_-room+1);
        const int begin=std::max(selection,start),end=std::min(selection+length,start+room);
        for(int i=begin;i<end;++i){
            const int x=left+i-start;p.fillRect(x*cellWidth_,(y+1)*cellHeight_,cellWidth_,cellHeight_,theme::selection);
            drawText(p,x,y+1,stampInput_.mid(i,1),Qt::black,1);
        }
    }
    if(columns_>=110)drawCommands(p,44,y+1,"[F2] Now  [F4] Timestamp ("+field+")  [F5] Mode ("+mode+")");
    else drawCommands(p,inputWidth+1,y+1,"[F2] Now [F4] "+field+" [F5] "+mode);
    if(busy_)drawCommands(p,0,y+2,"Changing timestamps… [Esc] Stop after current file");
    else if(!status_.isEmpty())drawText(p,0,y+2,status_,theme::labels);
    else drawCommands(p,0,y+2,"[Enter] OK  [Tab] Current  [F3] Last  [Up/Down] History  [F1] Help  [Esc] Cancel");
}
}
