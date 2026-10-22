#pragma once
#include "fs/preview.h"
#include "ui/consolefont.h"
#include <QFutureWatcher>
#include <QImage>
#include <QTimer>
#include <QWidget>

namespace ltree {
class VisualPreview : public QWidget {
    Q_OBJECT
public:
    explicit VisualPreview(QWidget *parent);
    ~VisualPreview() override;
    void setPath(const QString &path);
    void cancel();
    void reload();
    bool busy() const { return busy_; }
    QImage image() const { return image_; }
    int page() const { return page_; }
    QString error() const { return error_; }
    bool handleKey(int key);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    struct Result { QImage image;std::shared_ptr<PreviewSource> source;QString error;int page=1; };
    QString path_,error_;
    QImage image_;
    std::shared_ptr<PreviewSource> source_;
    QFutureWatcher<Result> watcher_;
    QFutureWatcher<std::shared_ptr<PreviewSource>> thumbnailWatcher_;
    Cancellation thumbnailCancel_;
    int thumbnailGeneration_=0;
    Cancellation cancel_;
    QTimer spinner_,resize_,thumbnailTimer_;
    ConsoleFont font_;
    int generation_=0,jobGeneration_=0,page_=1,tick_=0;
    bool busy_=false,pending_=false,initialFrame_=true;
    double zoom_=1;
    QPoint pan_;
    void request();
    void start();
};
}
