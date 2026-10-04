#include "ui/executewindow.h"
#include "ui/theme.h"
#include <algorithm>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>
#include <QProcessEnvironment>
#include <QResource>

static void initializeExecuteFont() { Q_INIT_RESOURCE(execute_font); }

namespace ltree {
ExecuteWindow::ExecuteWindow(QString directory,const QString &shell,const QStringList &arguments,QWidget *parent)
    : QWidget(parent),directory_(std::move(directory)),terminal_(nullptr)
{
    setAttribute(Qt::WA_OpaquePaintEvent);setAttribute(Qt::WA_NoMousePropagation);
    initializeExecuteFont();
    static const int fontId=QFontDatabase::addApplicationFont(":/resources/console8x16.ttf");
    QFont font=QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if(fontId>=0){
        font.setFamily(QFontDatabase::applicationFontFamilies(fontId).first());
        // QTermWidget construye primero su fuente Monospace antes de aceptar la nuestra.
        QFont::insertSubstitution("Monospace",font.family());
    }
    terminal_=new QTermWidget(0,this);
    font.setPixelSize(16);font.setFixedPitch(true);font.setHintingPreference(QFont::PreferNoHinting);font.setStyleStrategy(QFont::NoAntialias);
    terminal_->setTerminalFont(font);terminal_->setColorScheme("Linux");
    terminal_->setMargin(0);terminal_->setScrollBarPosition(QTermWidgetInterface::NoScrollBar);
    terminal_->setHistorySize(5000);terminal_->setKeyboardCursorShape(QTermWidget::KeyboardCursorShape::BlockCursor);
    terminal_->setBlinkingCursor(false);terminal_->setDrawLineChars(true);
    terminal_->setWorkingDirectory(directory_);terminal_->setShellProgram(shell);terminal_->setArgs(arguments);
    terminal_->setEnvironment(QProcessEnvironment::systemEnvironment().toStringList());
    terminal_->setAutoClose(true);setFocusProxy(terminal_);
    terminal_->installEventFilter(this);
    for(auto *child:terminal_->findChildren<QWidget*>())child->installEventFilter(this);
    connect(terminal_,&QTermWidget::finished,this,&ExecuteWindow::finish);
    terminal_->startShellProgram();
}
void ExecuteWindow::finish()
{
    if(closing_)return;
    closing_=true;emit closed();
}
bool ExecuteWindow::eventFilter(QObject *,QEvent *event)
{
    if(event->type()!=QEvent::KeyPress)return false;
    const auto *key=static_cast<QKeyEvent*>(event);
    if(key->modifiers()==Qt::NoModifier && key->key()==Qt::Key_Escape &&
       terminal_->getShellPID()>0 && terminal_->getForegroundProcessId()==terminal_->getShellPID()){
        finish();return true;
    }
    if(key->modifiers().testFlag(Qt::AltModifier) && key->key()==Qt::Key_Return){
        if(window()->isFullScreen())window()->showNormal();else window()->showFullScreen();return true;
    }
    if(key->modifiers().testFlag(Qt::AltModifier) && key->key()==Qt::Key_F7){
        if(window()->isMaximized())window()->showNormal();else window()->showMaximized();return true;
    }
    return false;
}
void ExecuteWindow::resizeEvent(QResizeEvent *)
{
    terminal_->setGeometry(0,32,width(),std::max(1,height()-32));
}
void ExecuteWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);p.fillRect(rect(),Qt::black);
    const auto draw=[&](int x,int y,const QString &value,int limit){
        int column=0;for(char32_t code:value.toUcs4()){
            if(column>=limit)break;
            const auto glyph=QString::fromUcs4(&code,1);
            if(!consoleFont_.draw(p,(x+column)*8,y*16,glyph,theme::labels)){
                p.setPen(theme::labels);p.drawText((x+column)*8,y*16+13,glyph);
            }
            ++column;
        }
    };
    const int columns=width()/8;
    draw(0,0,"eXecute: "+directory_,columns);
    draw(0,1,"Esc: return from the shell   exit / Ctrl+D: quit",columns);
    p.fillRect(0,31,width(),1,theme::labels);
}
}
