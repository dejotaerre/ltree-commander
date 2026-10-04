#include "ui/comparewindow.h"
#include "ui/theme.h"
#include "ui/doschars.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QFontDatabase>
#include <algorithm>

namespace ltree {
CompareWindow::CompareWindow(QString first,QString second,QWidget *parent)
    : QWidget(parent),first_(std::move(first)),second_(std::move(second))
{
    setFocusPolicy(Qt::StrongFocus); setAttribute(Qt::WA_OpaquePaintEvent); setAttribute(Qt::WA_NoMousePropagation);
    auto mono=QFontDatabase::systemFont(QFontDatabase::FixedFont); mono.setPixelSize(14); setFont(mono);
    if(!consoleFont_.available()){const QFontMetrics metrics(mono);cw_=metrics.horizontalAdvance('M');ch_=metrics.height();ascent_=metrics.ascent();}
    cancel_=std::make_shared<std::atomic_bool>(false);
    connect(&watcher_,&QFutureWatcher<CompareResult>::finished,this,[this]{
        loading_=false; result_=watcher_.result();
        status_=result_.error;
        if(initialLoad_){
            source_=result_;initialLoad_=false;
            hex_=!result_.textError.isEmpty() && result_.error.isEmpty();
            updateHexWidth();
        }else{
            top_=(anchorFirst_>=0 || anchorSecond_>=0)?std::max(0,int(result_.rows.size())-1):0;
            for(int row=0;row<result_.rows.size();++row){
                const auto &item=result_.rows[row];
                if((anchorFirst_>=0 && item.first>=0 && result_.firstLineNumbers[item.first]>=anchorFirst_) ||
                   (anchorFirst_<0 && anchorSecond_>=0 && item.second>=0 && result_.secondLineNumbers[item.second]>=anchorSecond_)){
                    top_=row;break;
                }
            }
            textTop_=top_;
            if(!result_.textError.isEmpty() && result_.error.isEmpty()){
                hex_=true;top_=hexOffset_/hexColumns_;status_=result_.textError;
            }
        }
        if(result_.cancelled)status_="Comparison cancelled";
        clamp();update();
    });
    const auto cancel=cancel_; const auto a=first_,b=second_;
    watcher_.setFuture(QtConcurrent::run([a,b,cancel]{return compareFiles(a,b,cancel);}));
}
CompareWindow::~CompareWindow(){cancel_->store(true);}
void CompareWindow::recompute()
{
    anchorFirst_=anchorSecond_=-1;
    if(top_<result_.rows.size()){
        const auto &row=result_.rows[top_];
        if(row.first>=0)anchorFirst_=result_.firstLineNumbers[row.first];
        if(row.second>=0)anchorSecond_=result_.secondLineNumbers[row.second];
    }
    loading_=true;cancel_=std::make_shared<std::atomic_bool>(false);
    const auto source=source_;const auto options=options_;const auto cancel=cancel_;
    watcher_.setFuture(QtConcurrent::run([source,options,cancel]{return compareCharacters(source,options,cancel);}));
}

int CompareWindow::pageRows() const {return std::max(1,(height()/ch_-3)/(vertical_?1:2)-2);}
void CompareWindow::clamp(){top_=std::clamp(top_,0,std::max(0,totalRows()-1));left_=std::max(0,left_);}
int CompareWindow::totalRows() const
{
    return hex_?int((std::max(result_.firstBytes.size(),result_.secondBytes.size())+hexColumns_-1)/hexColumns_):int(result_.rows.size());
}
void CompareWindow::updateHexWidth()
{
    const auto offset=qint64(top_)*hexColumns_;
    const int columns=vertical_?(width()/cw_-1)/2:width()/cw_;
    const int fit=(columns-11)/4;
    hexColumns_=fit>=24?fit/8*8:fit>=16?16:fit>=12?12:8;
    if(hex_)top_=int(offset/hexColumns_);
}
void CompareWindow::hexPane(QPainter &p,int x,int y,int columns,int rows,bool second)
{
    const auto &bytes=second?result_.secondBytes:result_.firstBytes;
    const auto &other=second?result_.firstBytes:result_.secondBytes;
    const bool ascii=columns>=11+hexColumns_*4;
    for(int r=0;r<rows-2 && top_+r<totalRows();++r){
        const auto offset=qint64(top_+r)*hexColumns_;
        text(p,x,y+1+r,QString("%1  ").arg(offset,8,16,QChar('0')).toUpper(),theme::labels,10);
        const auto block=std::lower_bound(result_.binaryBlocks.cbegin(),result_.binaryBlocks.cend(),offset);
        const bool changedRow=block!=result_.binaryBlocks.cend() && *block<offset+hexColumns_;
        if(changedRow && !byteDetail_)p.fillRect((x+10)*cw_,(y+1+r)*ch_,std::max(0,columns-10)*cw_,ch_,QColor(128,0,0));
        for(int i=0;i<hexColumns_;++i){
            const auto at=offset+i;
            if(at>=bytes.size() && at>=other.size())break;
            const bool present=at<bytes.size();
            const bool changed=!present || at>=other.size() || bytes[at]!=other[at];
            const int hx=x+10+i*3, ax=x+11+hexColumns_*3+i;
            if(changed && byteDetail_){
                p.fillRect(hx*cw_,(y+1+r)*ch_,2*cw_,ch_,QColor(128,0,0));
                if(ascii)p.fillRect(ax*cw_,(y+1+r)*ch_,cw_,ch_,QColor(128,0,0));
            }
            const auto byte=present?quint8(bytes[at]):0;
            text(p,hx,y+1+r,present?QString("%1").arg(byte,2,16,QChar('0')).toUpper():"--",theme::normal,2);
            if(ascii)text(p,ax,y+1+r,!present?"-":dosGlyph(byte),theme::normal,1);
        }
    }
}
void CompareWindow::text(QPainter &p,int x,int y,const QString &value,QColor color,int limit)
{
    p.save();p.setPen(color);int col=0;
    for(char32_t code:value.toUcs4()){
        if(col>=limit)break;
        QString glyph=QString::fromUcs4(&code,1);
        if(QChar::category(code)==QChar::Other_Control)glyph=".";
        p.save();
        p.setClipRect((x+col)*cw_,y*ch_,cw_,ch_,Qt::IntersectClip);
        if(!consoleFont_.draw(p,(x+col)*cw_,y*ch_,glyph,color))p.drawText((x+col)*cw_,y*ch_+ascent_,glyph);
        p.restore();
        ++col;
    }p.restore();
}
void CompareWindow::pane(QPainter &p,int x,int y,int columns,int rows,bool second)
{
    p.save();p.setClipRect(x*cw_,y*ch_,columns*cw_,rows*ch_);
    text(p,x,y,second?second_:first_,theme::labels,columns);
    p.fillRect(x*cw_,(y+1)*ch_-1,columns*cw_,1,theme::labels);
    if(hex_){hexPane(p,x,y,columns,rows,second);p.restore();return;}
    const auto &lines=second?result_.second:result_.first;
    const int margin=numbers_?7:0;
    for(int r=0;r<rows-2 && top_+r<result_.rows.size();++r){
        const auto &row=result_.rows[top_+r]; const int index=second?row.second:row.first;
        if(row.changed)p.fillRect((x+margin)*cw_,(y+1+r)*ch_,std::max(0,columns-margin)*cw_,ch_,QColor(128,0,0));
        if(numbers_)text(p,x,y+1+r,index<0?QString("     - "):QString::number((second?result_.secondLineNumbers:result_.firstLineNumbers)[index]).rightJustified(6)+' ',theme::labels,margin);
        if(index<0)continue;
        QString shown; int column=0;
        for(char32_t code:lines[index].toUcs4()){
            const int span=code=='\t'?4-column%4:1;
            for(int i=0;i<span;++i,++column)if(column>=left_ && column<left_+columns-margin)
                shown+=code=='\t'?QString(" "):result_.rawCharacters?dosGlyph(quint8(code)):QString::fromUcs4(&code,1);
            if(column>=left_+columns-margin)break;
        }
        text(p,x+margin,y+1+r,shown,theme::normal,columns-margin);
    }
    p.restore();
}
void CompareWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);p.setFont(font());p.fillRect(rect(),theme::background);
    const int cols=width()/cw_,rows=height()/ch_;
    if(help_){drawHelpDocument(p,rect(),helpDocument_,cw_,ch_,ascent_,&consoleFont_);return;}
    if(!loading_ && result_.error.isEmpty()){
        if(vertical_){pane(p,0,0,cols/2,rows-3,false);pane(p,cols/2+1,0,cols-cols/2-1,rows-3,true);p.fillRect(cols/2*cw_,0,1,(rows-3)*ch_,theme::labels);}
        else {const int half=(rows-3)/2;pane(p,0,0,cols,half,false);pane(p,0,half,cols,rows-3-half,true);}
    }
    QString summary=loading_?"Comparing… Esc cancels":!result_.error.isEmpty()?result_.error:result_.cancelled?"Comparison cancelled":!status_.isEmpty()?status_:hex_?
        QString("HEX: %1 different bytes | %2 / %3 bytes | offset %4").arg(result_.differentBytes).arg(result_.firstBytes.size()).arg(result_.secondBytes.size()).arg(currentOffset(),8,16,QChar('0')):
        result_.changes.isEmpty()?
        (result_.bytesEqual?"Files are identical":(!options_.caseSensitive || options_.compressWhitespace || options_.suppressEmpty)?
            "Text matches with current options; bytes differ":"Text matches; bytes, encoding or line endings differ"):
        QString("%1 different blocks — row %2/%3").arg(result_.changes.size()).arg(result_.rows.isEmpty()?0:top_+1).arg(result_.rows.size());
    text(p,0,rows-3,summary,theme::labels,cols);
    text(p,0,rows-2,"Space/+ Next   - Previous   S Split   H Hex/Chars   D Row/Byte   F1 Help",theme::normal,cols);
    text(p,0,rows-1,QString("C Case %1   W Spaces %2   E Empty %3   Enter/Esc/Q Return")
        .arg(options_.caseSensitive?"ON":"OFF",options_.compressWhitespace?"ON":"OFF",options_.suppressEmpty?"ON":"OFF"),theme::labels,cols);
}
void CompareWindow::jump(int direction)
{
    if(hex_){
        const auto &blocks=result_.binaryBlocks;
        if(direction>0){
            const auto next=std::lower_bound(blocks.cbegin(),blocks.cend(),qint64(top_+1)*hexColumns_);
            if(next!=blocks.cend()){top_=*next/hexColumns_;status_.clear();return;}
        }else{
            auto previous=std::lower_bound(blocks.cbegin(),blocks.cend(),currentOffset());
            if(previous!=blocks.cbegin()){--previous;top_=*previous/hexColumns_;status_.clear();return;}
        }
        status_=direction>0?"No more differences":"No previous differences";return;
    }
    if(direction>0){for(int row:result_.changes)if(row>top_){top_=row;status_.clear();return;}status_="No more differences";}
    else {for(auto i=result_.changes.crbegin();i!=result_.changes.crend();++i)if(*i<top_){top_=*i;status_.clear();return;}status_="No previous differences";}
}
void CompareWindow::keyPressEvent(QKeyEvent *event)
{
    const int key=event->key();
    if(event->modifiers().testFlag(Qt::AltModifier) && key==Qt::Key_Return){
        if(window()->isFullScreen())window()->showNormal();else window()->showFullScreen();return;
    }
    if(event->modifiers().testFlag(Qt::AltModifier) && key==Qt::Key_F7){
        if(window()->isMaximized())window()->showNormal();else window()->showMaximized();return;
    }
    if(help_){help_=helpDocumentKey(helpDocument_,event,width()/cw_,height()/ch_);update();return;}
    if(key==Qt::Key_Escape || key==Qt::Key_Return || key==Qt::Key_Enter || key==Qt::Key_Q){cancel_->store(true);emit closed();return;}
    if(loading_)return;
    status_.clear();
    if(key==Qt::Key_F1){helpDocument_.open(HelpTopic::Compare);help_=true;}
    else if(key==Qt::Key_H && result_.error.isEmpty() && !result_.cancelled){
        if(hex_ && !result_.textError.isEmpty())status_=result_.textError;
        else if(hex_){hexOffset_=int(currentOffset());hex_=false;top_=textTop_;}
        else {textTop_=top_;hex_=true;top_=hexOffset_/hexColumns_;}
    }
    else if(key==Qt::Key_C || key==Qt::Key_W || key==Qt::Key_E){
        if(hex_)status_="C/W/E apply to characters; Hex compares original bytes";
        else if(result_.error.isEmpty() && !result_.cancelled){
            if(key==Qt::Key_C)options_.caseSensitive=!options_.caseSensitive;
            else if(key==Qt::Key_W)options_.compressWhitespace=!options_.compressWhitespace;
            else options_.suppressEmpty=!options_.suppressEmpty;
            recompute();
        }
    }
    else if(key==Qt::Key_D && hex_)byteDetail_=!byteDetail_;
    else if(key==Qt::Key_S){vertical_=!vertical_;updateHexWidth();}
    else if(key==Qt::Key_L && !hex_)numbers_=!numbers_;
    else if(key==Qt::Key_Space || key==Qt::Key_Plus)jump(1);
    else if(key==Qt::Key_Minus)jump(-1);
    else if(key==Qt::Key_Up)--top_;else if(key==Qt::Key_Down)++top_;
    else if(key==Qt::Key_PageUp)top_-=pageRows();else if(key==Qt::Key_PageDown)top_+=pageRows();
    else if(key==Qt::Key_Home)top_=0;else if(key==Qt::Key_End)top_=std::max(0,totalRows()-pageRows());
    else if(key==Qt::Key_Left && !hex_)left_-=4;else if(key==Qt::Key_Right && !hex_)left_+=4;
    else status_="Comparison command is not implemented yet";
    clamp();update();
}
void CompareWindow::wheelEvent(QWheelEvent *event){if(help_){helpDocumentScroll(helpDocument_,event->angleDelta().y()/120,width()/cw_,height()/ch_);update();return;}if(loading_)return;top_-=event->angleDelta().y()/120*3;clamp();update();}
void CompareWindow::resizeEvent(QResizeEvent *){updateHexWidth();clamp();}
}
