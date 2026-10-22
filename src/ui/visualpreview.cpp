#include "ui/visualpreview.h"
#include "ui/theme.h"
#include "fs/thumbnail.h"
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace ltree {
VisualPreview::VisualPreview(QWidget *parent):QWidget(parent)
{
    setObjectName("visualPreview");setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);setAttribute(Qt::WA_NoMousePropagation);
    QImageReader::setAllocationLimit(128);
    spinner_.setInterval(100);connect(&spinner_,&QTimer::timeout,this,[this]{++tick_;update();});
    resize_.setSingleShot(true);resize_.setInterval(120);connect(&resize_,&QTimer::timeout,this,[this]{request();});
    thumbnailTimer_.setInterval(2000);
    connect(&thumbnailTimer_,&QTimer::timeout,this,[this]{
        if(!isVisible() || busy_ || pending_ || thumbnailWatcher_.isRunning() || !source_ || source_->kind!=PreviewKind::Video || !source_->thumbnailRgb.isEmpty())return;
        thumbnailCancel_=std::make_shared<std::atomic_bool>(false);thumbnailGeneration_=generation_;
        const auto source=source_;const auto cancel=thumbnailCancel_;
        thumbnailWatcher_.setFuture(QtConcurrent::run([source,cancel]{return refreshVideoThumbnail(source,cancel);}));
    });
    connect(&thumbnailWatcher_,&QFutureWatcher<std::shared_ptr<PreviewSource>>::finished,this,[this]{
        const auto source=thumbnailWatcher_.result();
        if(source && thumbnailGeneration_==generation_ && !thumbnailCancel_->load()){
            source_=source;page_=0;initialFrame_=false;request();
        }
    });
    connect(&watcher_,&QFutureWatcher<Result>::finished,this,[this]{
        busy_=false;
        if(jobGeneration_==generation_ && !cancel_->load()){
            const auto result=watcher_.result();image_=result.image;source_=result.source;error_=result.error;
            if(source_){page_=std::clamp(result.page,source_->thumbnailRgb.isEmpty()?1:0,source_->pages);initialFrame_=false;}
        }
        if(pending_)start();else spinner_.stop();
        update();
    });
}
VisualPreview::~VisualPreview(){cancel();}
void VisualPreview::cancel(){++generation_;pending_=false;resize_.stop();spinner_.stop();thumbnailTimer_.stop();if(thumbnailCancel_)thumbnailCancel_->store(true);if(cancel_)cancel_->store(true);}
void VisualPreview::setPath(const QString &path)
{
    if(path_==path){if(cancel_ && cancel_->load())request();return;}
    path_=path;source_.reset();image_={};error_.clear();page_=1;initialFrame_=true;zoom_=1;pan_={};request();
}
void VisualPreview::reload(){source_.reset();image_={};error_.clear();initialFrame_=true;request();}
void VisualPreview::request(){thumbnailTimer_.start();++generation_;pending_=true;if(cancel_)cancel_->store(true);if(!busy_)start();update();}
void VisualPreview::start()
{
    if(!pending_ || path_.isEmpty())return;
    pending_=false;busy_=true;jobGeneration_=generation_;spinner_.start();
    cancel_=std::make_shared<std::atomic_bool>(false);
    const auto cancel=cancel_;const auto path=path_;const auto cached=source_;const int page=page_;const bool initial=initialFrame_;
    const QSize target(std::clamp(width(),64,2048),std::clamp(height()-80,64,2048));
    watcher_.setFuture(QtConcurrent::run([path,cached,cancel,page,target,initial]{
        Result result;result.source=cached?cached:preparePreview(path,cancel);
        if(cancel->load())return result;
        if(!result.source->error.isEmpty()){result.error=result.source->error;return result;}
        if(result.source->kind==PreviewKind::Image){
            QImageReader reader(path);reader.setAutoTransform(true);
            const auto size=reader.size();
            if(size.isValid())reader.setScaledSize(size.scaled(target,Qt::KeepAspectRatio));
            result.image=reader.read();
            if(result.image.isNull())result.error="Cannot decode this image: "+reader.errorString();
        }else{
            const auto raster=renderPreview(result.source,initial && result.source->kind==PreviewKind::Video?-1:page,target.width(),target.height(),cancel);
            result.source=raster.source;result.error=raster.error;
            result.page=raster.page;
            if(!raster.rgb.isEmpty())result.image=QImage(reinterpret_cast<const uchar*>(raster.rgb.constData()),raster.width,raster.height,raster.width*3,QImage::Format_RGB888).copy();
        }
        return result;
    }));
}
bool VisualPreview::handleKey(int key)
{
    if(key==Qt::Key_F3){reload();return true;}
    if(previewKind(path_)==PreviewKind::Video && (key==Qt::Key_PageDown || key==Qt::Key_PageUp || key==Qt::Key_Home || key==Qt::Key_End || key==Qt::Key_Space || key==Qt::Key_Backspace))return true;
    const int pages=source_?source_->pages:1;int next=page_;
    if(key==Qt::Key_PageDown || key==Qt::Key_Space)next=std::min(pages,page_+1);
    else if(key==Qt::Key_PageUp || key==Qt::Key_Backspace)next=std::max(source_ && !source_->thumbnailRgb.isEmpty()?0:1,page_-1);
    else if(key==Qt::Key_Home)next=1;
    else if(key==Qt::Key_End)next=pages;
    else if(key==Qt::Key_Plus || key==Qt::Key_Equal)zoom_=std::min(8.0,zoom_*1.25);
    else if(key==Qt::Key_Minus || key==Qt::Key_Underscore)zoom_=std::max(0.25,zoom_/1.25);
    else if(key==Qt::Key_0 || key==Qt::Key_ParenRight){zoom_=1;pan_={};}
    else if(key==Qt::Key_Left)pan_.rx()+=32;
    else if(key==Qt::Key_Right)pan_.rx()-=32;
    else if(key==Qt::Key_Up)pan_.ry()+=32;
    else if(key==Qt::Key_Down)pan_.ry()-=32;
    else return false;
    if(next!=page_){page_=next;pan_={};image_={};request();}
    update();return true;
}
void VisualPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);p.fillRect(rect(),theme::background);
    const auto text=[&](int row,const QString &s,QColor color){
        const auto line=s.left(width()/8);
        if(!font_.draw(p,0,row*16,line,color)){p.setPen(color);p.drawText(0,row*16+13,line);}
    };
    text(0,QFileInfo(path_).fileName(),theme::labels);p.setPen(theme::labels);p.drawLine(0,24,width(),24);
    const bool video=source_ && source_->kind==PreviewKind::Video;
    const bool details=source_ && !source_->details.isEmpty();
    const QRect area(0,32,width(),std::max(1,height()-96-(details?16:0)));
    if(!image_.isNull()){
        const auto fit=image_.size().scaled(area.size(),Qt::KeepAspectRatio);
        const QSize size(std::max(1,int(fit.width()*zoom_)),std::max(1,int(fit.height()*zoom_)));
        p.save();p.setClipRect(area);p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRect(area.center()-QPoint(size.width()/2,size.height()/2)+pan_,size),image_);p.restore();
    }
    if(busy_)text(2,QString("%1 Preparing preview...").arg(QString("|/-\\")[tick_%4]),theme::labels);
    else if(!error_.isEmpty()){
        const int columns=std::max(1,width()/8);
        for(int i=0;i<error_.size() && i/columns<area.height()/16;i+=columns)text(2+i/columns,error_.mid(i,columns),theme::normal);
    }
    const int row=std::max(3,height()/16-3);
    if(details)text(row-1,source_->details,theme::normal);
    text(row,(source_?previewPosition(*source_,page_):QString("Page 1/1"))+QString("   Zoom %1%  F12 Raw/Visual").arg(int(zoom_*100)),theme::labels);
    text(row+1,video?"Thumbnail  +/- Zoom  0 Fit":"PgUp/PgDn Pages  +/- Zoom  0 Fit",theme::normal);
    text(row+2,"Arrows Pan  H Hex  Esc Return",theme::labels);
}
void VisualPreview::resizeEvent(QResizeEvent *){resize_.start();}
void VisualPreview::wheelEvent(QWheelEvent *event)
{
    handleKey(event->modifiers().testFlag(Qt::ControlModifier)?(event->angleDelta().y()>0?Qt::Key_Plus:Qt::Key_Minus):(event->angleDelta().y()>0?Qt::Key_PageUp:Qt::Key_PageDown));event->accept();
}
}
