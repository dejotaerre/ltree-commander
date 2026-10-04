#pragma once
#include "ui/helpview.h"
#include "fs/filecompare.h"
#include "ui/consolefont.h"
#include <QWidget>
#include <QFutureWatcher>

namespace ltree {
class CompareWindow : public QWidget {
    Q_OBJECT
public:
    CompareWindow(QString first, QString second, QWidget *parent=nullptr);
    ~CompareWindow() override;
    bool loading() const { return loading_; }
    const CompareResult &result() const { return result_; }
    int currentRow() const { return top_; }
    bool hexMode() const { return hex_; }
    int bytesPerRow() const { return hexColumns_; }
    qint64 currentOffset() const { return qint64(top_)*hexColumns_; }
    CompareOptions options() const { return options_; }
    bool vertical() const { return vertical_; }
signals:
    void closed();
protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool focusNextPrevChild(bool) override { return false; }
private:
    HelpDocument helpDocument_;
    QString first_,second_,status_;
    ConsoleFont consoleFont_;
    CompareResult result_, source_;
    CompareOptions options_;
    bool initialLoad_=true;
    int anchorFirst_=-1,anchorSecond_=-1;
    void recompute();
    QFutureWatcher<CompareResult> watcher_;
    Cancellation cancel_;
    bool loading_=true,vertical_=false,numbers_=true,help_=false;
    bool hex_=false, byteDetail_=true;
    int hexColumns_=16, textTop_=0, hexOffset_=0;
    void updateHexWidth();
    int totalRows() const;
    void hexPane(QPainter &,int,int,int,int,bool);
    int top_=0,left_=0,cw_=8,ch_=16,ascent_=13;
    int pageRows() const;
    void clamp();
    void jump(int direction);
    void text(QPainter &,int,int,const QString &,QColor,int);
    void pane(QPainter &,int,int,int,int,bool);
};
}
