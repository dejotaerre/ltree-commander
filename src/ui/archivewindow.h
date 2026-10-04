#pragma once
#include "ui/helpview.h"
#include "fs/archive.h"
#include "fs/search.h"
#include "core/filespec.h"
#include "ui/consolefont.h"
#include "ui/fileviewer.h"
#include <QWidget>
#include <QPointer>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QTimer>
#include <QLineEdit>
namespace ltree {
class ArchiveWindow : public QWidget {
    Q_OBJECT
public:
    ArchiveWindow(QString path,QString destination,QWidget *parent=nullptr);
    ArchiveWindow(QStringList sources,QString base,QString destination,QWidget *parent=nullptr);
    ~ArchiveWindow() override;
    bool busy() const { return busy_; }
    QString status() const { return status_; }
    int entryCount() const { return catalog_.entries.size(); }
    int taggedCount() const { return tags_.size(); }
    void setSearch(const SearchOptions &search) { search_=search; }
signals:
    void closed();
protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    bool focusNextPrevChild(bool) override { return false; }
private:
    HelpDocument helpDocument_;
    struct Row {QString path,name;bool directory=false;int index=-1;qint64 size=0,modified=0;};
    struct SearchResult {QSet<int> found;int failed=0;QString error;bool cancelled=false;};
    QLineEdit inputEditor_;
    ArchiveFormat format_=ArchiveFormat::Zip;
    bool formatPicker_=false;int formatCursor_=0;
    void setInput(const QString &value,bool selected=false);
    void chooseFormat(ArchiveFormat format);
    QString path_,folder_,destination_,base_,status_,input_,password_;
    QStringList sources_;QVector<Row> rows_;ArchiveCatalog catalog_;Filespec filespec_;QSet<int> tags_,selected_;
    int cursor_=0,top_=0,spinner_=0,menu_=0;Qt::KeyboardModifiers modifiers_{};
    HexSnapshot snapshot_;
    bool unchanged();
    bool busy_=false,branch_=false,create_=false,paths_=true,replace_=false,help_=false;
    enum class Prompt {None,Extract,Create,Replace,Search,Filespec,Password};Prompt prompt_=Prompt::None;
    SearchOptions search_;Cancellation cancel_;ConsoleFont font_;QTimer timer_;QTemporaryDir temporary_;
    QFutureWatcher<ArchiveCatalog> catalogWatcher_;QFutureWatcher<ArchiveResult> operationWatcher_;QFutureWatcher<ArchiveBytes> memberWatcher_;QFutureWatcher<SearchResult> searchWatcher_;
    QPointer<FileViewer> viewer_;std::shared_ptr<std::atomic<int>> progress_;
    void initialize();void load();void rebuild();void clamp();void browseMember();void beginExtract(bool tagged,bool paths);void startOperation();void startSearch();void promptKey(QKeyEvent *);
    void text(QPainter &,int,int,const QString &,QColor,int limit=-1);void commands(QPainter &,int,int,const QString &);
    int pageRows() const { return std::max(1,height()/16-6); }
};
}
