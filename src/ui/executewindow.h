#pragma once
#include "ui/consolefont.h"
#include <QWidget>
#include <qtermwidget.h>

namespace ltree {
class ExecuteWindow : public QWidget {
    Q_OBJECT
public:
    ExecuteWindow(QString directory, const QString &shell, const QStringList &arguments, QWidget *parent=nullptr);
    QTermWidget *terminal() const { return terminal_; }
    QString directory() const { return directory_; }
signals:
    void closed();
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    QString directory_;
    ConsoleFont consoleFont_;
    QTermWidget *terminal_;
    bool closing_=false;
    void finish();
};
}
