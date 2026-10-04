#include "ui/treewindow.h"
#include "ui/theme.h"
#include <QFileInfo>
#include <QKeyEvent>
#include <QLocale>
#include <QScreen>
#include <algorithm>

namespace ltree {
void TreeWindow::beginExtendedStats()
{
    statsPath_=session_.view!=View::Tree && session_.currentFile()?QFileInfo(session_.currentFile()->path).absolutePath():session_.directory;
    extendedStats_=true;statsHelp_=false;statsScroll_=0;activeSpell_=false;statsTimer_.stop();
    refreshExtendedStats();updateAutoview();update();
}
void TreeWindow::refreshExtendedStats()
{
    systemStats_=systemStatistics(statsPath_);
    const auto mounts=mountedLocations(nullptr,true);
    loggedStats_=loggedStatistics(session_,mounts,systemStats_.mount);
    statsMounts_=mountedLocations();
    if(!systemStats_.mount.isEmpty() && std::none_of(statsMounts_.cbegin(),statsMounts_.cend(),[&](const auto &mount){return mount.path==systemStats_.mount;}))
        statsMounts_.append({systemStats_.mount,systemStats_.filesystem,systemStats_.source,false});
}
void TreeWindow::extendedStatsKey(QKeyEvent *event)
{
    const int key=event->key();const bool ctrl=event->modifiers().testFlag(Qt::ControlModifier),alt=event->modifiers().testFlag(Qt::AltModifier);
    if(statsHelp_){statsHelp_=helpDocumentKey(helpDocument_,event,columns_,rows_);update();return;}
    if(key==Qt::Key_Escape || key==Qt::Key_Return || key==Qt::Key_Enter){extendedStats_=false;statsTimer_.stop();refresh();return;}
    if(key==Qt::Key_F1){helpDocument_.open(HelpTopic::Statistics);statsHelp_=true;}
    else if(key==Qt::Key_F3 && !alt){
        if(ctrl){if(statsTimer_.isActive())statsTimer_.stop();else statsTimer_.start(1000);}
        refreshExtendedStats();
    }else if(!ctrl && !alt && (key==Qt::Key_Less || key==Qt::Key_Greater || event->text()=="<" || event->text()==">")){
        if(!statsMounts_.isEmpty()){
            int current=0;for(int i=0;i<statsMounts_.size();++i)if(statsMounts_[i].path==systemStats_.mount){current=i;break;}
            const int delta=key==Qt::Key_Less || event->text()=="<"?-1:1;
            statsPath_=statsMounts_[(current+delta+int(statsMounts_.size()))%int(statsMounts_.size())].path;
            refreshExtendedStats();
        }
    }else if(!ctrl && !alt){
        const int page=std::max(1,std::min(28,rows_-6));
        if(key==Qt::Key_Up)--statsScroll_;
        else if(key==Qt::Key_Down)++statsScroll_;
        else if(key==Qt::Key_PageUp)statsScroll_-=page;
        else if(key==Qt::Key_PageDown)statsScroll_+=page;
        else if(key==Qt::Key_Home)statsScroll_=0;
        else if(key==Qt::Key_End)statsScroll_=28-page;
        statsScroll_=std::clamp(statsScroll_,0,28-page);
    }
    update();
}
void TreeWindow::drawExtendedStats(QPainter &p)
{
    const auto cyan=theme::labels,yellow=theme::normal;
    if(statsHelp_){drawHelp(p);return;}
    const int width=std::min(80,columns_-2),left=columns_>=88?4:1,top=2,height=std::min(30,rows_-4);
    const int visible=height-2,columnWidth=(width-5)/2,right=left+columnWidth+3;
    statsScroll_=std::clamp(statsScroll_,0,28-visible);
    const QLocale locale(QLocale::Spanish,QLocale::Uruguay);
    const auto number=[&](quint64 value){return locale.toString(value);};
    const auto bytes=[&](qint64 value){return value<0?QString("N/A"):number(quint64(value))+" bytes";};
    const auto memory=[&](qint64 value,qint64 total=-1){
        if(value<0)return QString("N/A");
        QString text=number(quint64(value)/1048576)+" MB";
        if(total>0)text+=" ("+locale.toString(double(value)*100/double(total),'f',0)+"%)";
        return text;
    };
    drawText(p,left+1,0,"LTree Commander 0.1",cyan,width);
    drawText(p,left+width-17,0,systemStats_.measured.toString("yyyy-MM-dd HH:mm"),cyan,16);
    drawText(p,left,top,"┌"+QString(width-2,QChar(u'─'))+"┐",cyan,width);
    drawText(p,left,top+height-1,"└"+QString(width-2,QChar(u'─'))+"┘",cyan,width);
    for(int y=top+1;y<top+height-1;++y){drawText(p,left,y,"│",cyan,1);drawText(p,left+width-1,y,"│",cyan,1);}
    const auto rowVisible=[&](int row){return row>=statsScroll_ && row<statsScroll_+visible;};
    const auto text=[&](int x,int row,const QString &value,const QColor &color){if(rowVisible(row))drawText(p,x,top+1+row-statsScroll_,value,color,columnWidth);};
    const auto field=[&](int x,int row,const QString &label,const QString &value){
        if(!rowVisible(row))return;
        const int gap=std::max(13,int(label.size())+1),available=columnWidth-gap;
        const auto shown=value.size()>available?"..."+value.right(std::max(0,available-3)):value;
        drawText(p,x,top+1+row-statsScroll_,label,cyan,gap);
        drawText(p,x+gap,top+1+row-statsScroll_,shown.rightJustified(available),yellow,available);
    };
    text(left+1,0,"DISK STATISTICS",yellow);
    field(left+1,1,"Mount point",systemStats_.mount.isEmpty()?"N/A":systemStats_.mount);
    field(left+1,2,"File system",systemStats_.filesystem.isEmpty()?"N/A":systemStats_.filesystem);
    field(left+1,3,"Source",systemStats_.source.isEmpty()?"N/A":systemStats_.source);
    if(!systemStats_.error.isEmpty())text(left+1,4,"Filesystem data unavailable",yellow);
    field(left+1,5,"Capacity",bytes(systemStats_.capacity));
    field(left+1,6,"Available",bytes(systemStats_.available));
    field(left+1,7,"Used space",bytes(systemStats_.capacity>=0 && systemStats_.free>=0?std::max(qint64(0),systemStats_.capacity-systemStats_.free):-1));
    field(left+1,8,"Compression gain","N/A");
    field(left+1,9,"Reserved space",bytes(systemStats_.free>=0 && systemStats_.available>=0?std::max(qint64(0),systemStats_.free-systemStats_.available):-1));
    field(left+1,10,"Block size",bytes(systemStats_.blockSize));
    field(left+1,11,"Sector size","N/A");
    field(left+1,13,"Total sectors","N/A");
    field(left+1,14,"Total blocks",systemStats_.totalBlocks<0?"N/A":number(quint64(systemStats_.totalBlocks)));
    field(left+1,16,"Sectors/block","N/A");
    text(left+1,18,"SYSTEM INFORMATION",yellow);
    field(left+1,19,"RAM total",memory(systemStats_.ramTotal));
    field(left+1,20,"RAM available",memory(systemStats_.ramAvailable,systemStats_.ramTotal));
    field(left+1,21,"Commit total",memory(systemStats_.committed,systemStats_.commitLimit));
    field(left+1,22,"Commit limit",memory(systemStats_.commitLimit));
    field(left+1,23,"CPU speed",systemStats_.cpuMHz>0?locale.toString(systemStats_.cpuMHz,'f',1)+" MHz":"N/A");
    text(left+1,24,"CPU type",cyan);
    const auto cpu=systemStats_.cpuType.isEmpty()?"N/A":systemStats_.cpuType;
    for(int i=0;i<3;++i)text(left+1,25+i,cpu.mid(i*columnWidth,columnWidth),yellow);
    text(right,0,"LOGGED FILE STATISTICS",yellow);
    field(right,2,"Filespec",(session_.filespec.inverted?"!":"")+session_.filespec.text());
    const auto files=[&](int row,const QString &label,quint64 count,quint64 size){field(right,row,label,number(count)+" files");text(right,row+1,(number(size)+" bytes").rightJustified(columnWidth),yellow);};
    files(4,"Total files",loggedStats_.totalFiles,loggedStats_.totalBytes);
    files(6,"Matching files",loggedStats_.matchingFiles,loggedStats_.matchingBytes);
    files(8,"Tagged files",loggedStats_.taggedFiles,loggedStats_.taggedBytes);
    field(right,11,"Compressed files","N/A");
    field(right,13,"Directories logged",number(loggedStats_.directories));
    files(14,"Displayed files",loggedStats_.displayedFiles,loggedStats_.displayedBytes);
    field(right,16,"Average size",number(loggedStats_.displayedFiles?loggedStats_.displayedBytes/loggedStats_.displayedFiles:0)+" bytes");
    text(right,18,"DISPLAY INFORMATION",yellow);
    text(right,19,QString("Cols %1 Rows %2 List height %3").arg(columns_).arg(rows_).arg(std::max(0,bottom()-2)),cyan);
    const auto layout=fileLayout(2,bottom());
    const auto length=layout.extensionWidth?QString("%1.%2").arg(layout.nameWidth).arg(layout.extensionWidth-1):QString::number(layout.nameWidth);
    text(right,20,QString("Files/page %1 Name.ext length %2").arg(layout.capacity()).arg(length),cyan);
    const auto monitor=screen();
    text(right,21,monitor?QString("Screen logical %1 x %2").arg(monitor->size().width()).arg(monitor->size().height()):"Screen logical N/A",cyan);
    text(right,23,"LOGON INFORMATION",yellow);
    field(right,24,"Computer name",systemStats_.host.isEmpty()?"N/A":systemStats_.host);
    field(right,25,"User login name",systemStats_.user);
    field(right,26,"Session type",systemStats_.sessionType.isEmpty()?"N/A":systemStats_.sessionType);
    field(right,27,"Desktop",systemStats_.desktop.isEmpty()?"N/A":systemStats_.desktop);
    drawCommands(p,1,rows_-2,QString("[F1] Help [F3] Refresh [Ctrl+F3] Auto (%1) [</>] Mount [Esc/Enter] Return").arg(statsTimer_.isActive()?"ON":"OFF"));
    if(visible<28)drawCommands(p,1,rows_-1,QString("[Up/Down/PgUp/PgDn/Home/End] Scroll  Rows %1-%2/28").arg(statsScroll_+1).arg(statsScroll_+visible));
}
}
