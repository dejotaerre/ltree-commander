#pragma once
#include "ui/helpview.h"
#include "fs/viewdocument.h"
#include "fs/search.h"
#include "fs/hexedit.h"
#include <QTimer>
#include <array>
#include <optional>
#include "ui/consolefont.h"
#include <QFutureWatcher>
#include <QWidget>

namespace ltree {
class FileViewer : public QWidget {
    Q_OBJECT
public:
    explicit FileViewer(QString path, QWidget *parent = nullptr);
    ~FileViewer() override;
    void setPath(const QString &path);
    void reload();
    void setSearch(const SearchOptions &options);
    void setAutoview(bool enabled);
    QString path() const { return path_; }
    bool prompting() const { return searchPrompt_ || offsetPrompt_ || editing_ || gather_ || help_ || historyVisible_; }
    void setFiles(const QStringList &paths);
    void setDisplayName(const QString &name) { displayName_=name; update(); }
    void setEditable(bool enabled) { editable_=enabled; }
    bool editing() const { return editing_; }
    QByteArray viewedBytes() const { return document_.bytes; }
    QString viewedText() const { return document_.text; }
    QString characterSet() const { return document_.encoding; }
    bool lineMode() const { return lineMode_; }
    int tabWidth() const { return tabWidth_; }
    void handleKey(QKeyEvent *event) { keyPressEvent(event); }
    bool loading() const { return loading_; }
    QString status() const { return status_; }
    void showStatus(const QString &status) { status_=status;update(); }
    qsizetype currentRow() const { return top_; }
    qsizetype hit() const { return hit_; }
    bool dumpMode() const { return dump_; }
    bool wrapMode() const { return wrap_; }
    bool hexMode() const { return hex_; }
signals:
    void closed();
    void editRequested(const QString &path);
    void alternateEditRequested(const QString &path);
    void executeRequested(const QString &directory, bool separate);
    void untagRequested(const QString &path);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    bool focusNextPrevChild(bool) override { return false; }
private:
    HelpDocument helpDocument_;
    QString path_, status_, input_, query_;
    std::optional<SearchOptions> batchSearch_;
    ConsoleFont consoleFont_;
    ViewDocument document_;
    QFutureWatcher<ViewDocument> watcher_;
    Cancellation cancel_;
    bool pendingPath_ = false, autoview_ = false;
    bool loading_ = false, hex_ = false, searchPrompt_ = false, offsetPrompt_ = false;
    bool wrap_ = false, dump_ = false;
    bool junk_ = false, mask_ = false, ruler_ = false, lineMode_ = false;
    int tabWidth_=8, charset_=0, gap_=0, menuMode_=0, color_=0, background_=0;
    Qt::KeyboardModifiers modifiers_{};
    QSize sizeBase_;int widthStep_=0,heightStep_=0;
    qsizetype lineCursor_=0;
    std::array<qsizetype,10> bookmarks_{};
    QString displayName_;
    QStringList files_, searchHistory_,gatherHistory_;
    bool historyGather_=false;
    QStringList &historyEntries() { return historyGather_?gatherHistory_:searchHistory_; }
    int fileIndex_=0, historyIndex_=0;
    bool help_=false, historyVisible_=false, editable_=true, archiveListing_=false, rawArchive_=false;
    int gather_=0;qsizetype gatherStart_=0,gatherEnd_=0;
    QTimer scrollTimer_, reloadTimer_;
    bool scrollLoop_=false, skipPages_=false;
    int scrollDirection_=1;
    bool editing_=false, asciiEdit_=false, savePrompt_=false;
    qsizetype editOffset_=0, editCursor_=0;int editNibble_=0, afterSave_=0;
    QByteArray editPage_;HexSnapshot snapshot_;
    int previousMode_ = Qt::Key_A;
    int dumpColumns_ = 1;
    bool byteMode() const { return hex_ || dump_; }
    int bytesPerRow() const { return dump_ ? dumpColumns_ : 16; }
    void selectMode(int key);
    void decode();
    void navigateFile(int index, bool continueSearch=false);
    void beginHexEdit();
    void hexEditKey(QKeyEvent *);
    void finishHexEdit(bool save);
    bool extraKey(QKeyEvent *);
    void drawCommands(QPainter &,int x,int y,const QString &);
    void drawExtras(QPainter &);
    QString gatheredText() const;
    void copyGather(bool append);
    void rememberQuery();
    void loadViewHistory();
    void saveViewHistory();
    bool historyDirty_=false;
    SearchMode promptMode_=SearchMode::Unicode;
    QColor foreground() const;
    QColor background() const;
    QString byteGlyph(quint8 byte,bool applyMask=true) const;
    QString maskedGlyph(const QString &glyph) const;
    QVector<qsizetype> wrapStarts_;
    int wrapColumns_ = 1;
    void rebuildWrap();
    qsizetype sourceLine(qsizetype row) const;
    qsizetype sourceOffset(qsizetype row) const;
    qsizetype lineEnd(qsizetype line) const;
    qsizetype displayColumn(qsizetype line, qsizetype offset) const;
    bool caseSensitive_ = false, backward_ = false;
    qsizetype top_ = 0, left_ = 0, hit_ = -1, hitLength_ = 0, searchStart_ = 0;
    int cw_ = 8, ch_ = 16, ascent_ = 13;
    qsizetype totalRows() const;
    int pageRows() const;
    void clamp();
    void find(bool next,bool acrossFiles=false);
    bool continueSearch_=false,continueBackward_=false;
    void text(QPainter &, int x, int y, const QString &, QColor, int max = -1);
};
}
