#pragma once
#include "ui/helpview.h"

#include "core/session.h"
#include "fs/search.h"
#include "fs/makedirectory.h"
#include "fs/deletedirectory.h"
#include "fs/prune.h"
#include "fs/graft.h"
#include "fs/copyfiles.h"
#include "fs/metadata.h"
#include "fs/stamp.h"
#include <QLineEdit>
#include "fs/rename.h"
#include "fs/directorycompare.h"
#include "fs/systemstats.h"
#include "fs/symlink.h"
#include "ui/consolefont.h"
#include "fs/mounts.h"
#include "ui/fileviewer.h"
#include "ui/archivewindow.h"
#include "ui/comparewindow.h"
#include "ui/executewindow.h"
#include <QPointer>
#include <QFutureWatcher>
#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QPainter>
#include <optional>
#include <array>

namespace ltree {

class TreeWindow : public QWidget {
    Q_OBJECT
public:
    enum class FileDisplay { Name, SizeAttributes, Details, LongName };
    FileDisplay fileDisplay() const { return fileDisplay_; }
    explicit TreeWindow(const QString &root, QWidget *parent = nullptr);
    ~TreeWindow() override;
    const Session &session() const { return session_; }
    bool busy() const { return busy_ || searching_; }
    QString status() const { return status_; }
    int columns() const { return columns_; }
    int rows() const { return rows_; }
    bool isSplit() const { return otherSession_.has_value(); }
    int activeSide() const { return activeSide_; }
    const Session *oppositeSession() const { return otherSession_ ? &*otherSession_ : nullptr; }
    void load(bool recursive, bool preserveTags = false, const QString &directory = {});
    bool selectRoot(const QString &path);
    void setTreeSizesVisible(bool visible) { treeSizesVisible_=visible; update(); }
    bool treeSizesVisible() const { return treeSizesVisible_; }
    bool statisticsVisible() const { return extendedStats_; }
    bool statisticsAutoRefresh() const { return statsTimer_.isActive(); }
    const SystemStatistics &statisticsSystem() const { return systemStats_; }
    const LoggedStatistics &statisticsLogged() const { return loggedStats_; }

signals:
    void openRequested(const QString &path, bool editor);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    bool focusNextPrevChild(bool next) override;

private:
    HelpDocument helpDocument_;
    std::optional<Session> globalReturn_, otherGlobalReturn_;
    QPair<int,int> globalScroll_, otherGlobalScroll_;
    quint64 globalRevision_ = 0, otherGlobalRevision_ = 0;
    QSet<QString> globalPublishedTags_, otherGlobalPublishedTags_;
    void beginGlobal(bool tagged = false);
    void publishGlobal();
    void endGlobal();
    void cycleLoggedRoot(int direction);
    bool availPrompt_ = false;
    qint64 availCapacity_ = -1, availBytes_ = -1;
    void refreshAvailable();
    bool graftPrompt_ = false, graftSelected_ = false, graftBrowseParent_ = false, graftLocations_ = false, graftHelp_ = false;
    QString graftInput_, graftDefault_;
    int graftCursor_ = 0, graftPathIndex_ = -1, graftBrowseTop_ = 0, graftLocationIndex_ = 0, graftSpinner_ = 0;
    QStringList graftPaths_, graftLocationPaths_;
    FileMetadata graftSource_;
    std::optional<Session> graftBrowser_;
    QFutureWatcher<GraftResult> graftWatcher_;
    QFutureWatcher<ScanResult> graftBrowseWatcher_;
    QTimer graftTimer_;
    void connectGraft();
    void beginGraft();
    void graftKey(QKeyEvent *);
    void startGraftBrowse(const QString &path, bool parent = false);
    void graftBrowseKey(QKeyEvent *);
    void drawGraft(QPainter &);
    void drawGraftBrowse(QPainter &);
    bool linkPrompt_ = false, linkSelected_ = false;
    QString linkInput_;
    int linkCursor_ = 0;
    FileMetadata linkSource_;
    QFutureWatcher<SymlinkResult> linkWatcher_;
    void connectLink();
    void beginLink();
    void linkKey(QKeyEvent *);
    void drawLink(QPainter &);
    bool duplicatePrompt_=false, duplicateCase_=true, duplicateEmpty_=true;
    CompareFilter duplicateFilter_=CompareFilter::Duplicate;
    QFutureWatcher<DirectoryCompareResult> duplicateWatcher_;
    void duplicateKey(QKeyEvent *);
    void drawDuplicates(QPainter &);
    bool directoryComparePrompt_=false, directoryCompareReview_=false, directoryCompareBranch_=false, directoryCompareSelected_=false;
    QString directoryCompareInput_, directoryCompareSource_;
    int directoryCompareCursor_=0;
    DirectoryCompareOptions directoryCompareOptions_;
    QVector<FileEntry> directoryCompareFiles_;
    QFutureWatcher<DirectoryCompareResult> directoryCompareWatcher_;
    void connectDirectoryCompare();
    void beginDirectoryCompare(bool branch);
    void directoryCompareKey(QKeyEvent *);
    void drawDirectoryCompare(QPainter &);
    bool renamePrompt_=false, renameReview_=false, renameBatch_=false, renameSelected_=false;
    bool renameEach_=false, renameItemPrompt_=false;
    QString renameInput_, renameFirstError_;
    int renameCursor_=0, renameIndex_=0, renameCount_=0, renameFailed_=0;
    CopyCase renameCase_=CopyCase::Keep;
    QVector<FileMetadata> renameTargets_;
    QStringList renameNames_;
    QFutureWatcher<RenameResult> renameWatcher_;
    void connectRename();
    void beginRename(bool tagged=false);
    void renameKey(QKeyEvent *);
    void drawRename(QPainter &);
    void nextRename(bool confirmed=false);
    void finishRename(bool cancelled);
    bool extendedStats_=false, statsHelp_=false;
    int statsScroll_=0;
    QString statsPath_;
    SystemStatistics systemStats_;
    LoggedStatistics loggedStats_;
    QVector<MountPoint> statsMounts_;
    QTimer statsTimer_;
    void beginExtendedStats();
    void refreshExtendedStats();
    void extendedStatsKey(QKeyEvent *);
    void drawExtendedStats(QPainter &);
    bool stampPrompt_=false,stampTagged_=false,stampHelp_=false;
    QString stampInput_;
    int stampCursor_=0;
    QLineEdit stampEditor_;
    StampField stampField_=StampField::Written;
    StampMode stampMode_=StampMode::Set;
    FileMetadata stampReference_;
    QVector<FileMetadata> stampTargets_;
    QFutureWatcher<StampResult> stampWatcher_;
    void connectStamp();
    void beginStamp(bool tagged);
    void setStampInput(const QString &,bool selected=false);
    void stampKey(QKeyEvent *);
    void drawStamp(QPainter &);
    bool infoVisible_ = false, permissionsPrompt_ = false, permissionsReview_ = false;
    FileMetadata info_, permissionsBefore_;
    QString permissionsInput_;
    int permissionsCursor_ = 0;
    quint32 permissionsMode_ = 0;
    QFutureWatcher<PermissionResult> permissionsWatcher_;
    struct PermissionBatchResult {
        QVector<QPair<FileMetadata, PermissionResult>> items;
        bool cancelled = false;
    };
    bool permissionsBatch_ = false;
    QVector<FileMetadata> permissionsTargets_;
    QVector<quint32> permissionsModes_;
    QFutureWatcher<PermissionBatchResult> permissionsBatchWatcher_;
    void connectMetadata();
    void beginInfo();
    void refreshInfo();
    QRect infoRect() const;
    void drawInfo(QPainter &);
    void beginPermissions(bool tagged = false);
    void permissionsKey(QKeyEvent *);
    void drawPermissions(QPainter &);
    enum class CopyStep { None, Mask, Destination, MakePath, Paths, Replace, Confirm, Exists, Error, Running };
    CopyStep copyStep_ = CopyStep::None;
    CopyPaths copyPaths_ = CopyPaths::Full;
    CopyReplace copyReplace_ = CopyReplace::Ask;
    CopyCase copyCase_ = CopyCase::Keep;
    bool copyConfirm_ = false, copyInputSelected_ = false, copySingle_ = false, copyMove_ = false, copyFlat_ = false;
    QString copyInput_, copyMask_, copyDestination_, copySourceDirectory_, copySourceRoot_, copyError_, copyFirstError_;
    QStringList copyMaskHistory_, copyDestinationHistory_;
    int copyCursor_ = 0, copyHistoryIndex_ = -1, copyIndex_ = 0, copyCount_ = 0, copySkipped_ = 0;
    QVector<FileEntry> copyFiles_;
    QVector<CopyEntry> copyEntries_;
    QFutureWatcher<CopyResult> copyWatcher_;
    void connectCopy();
    void beginCopy(bool single = false, bool move = false, bool flat = false);
    void copyPromptKey(QKeyEvent *);
    void drawCopyPrompt(QPainter &);
    void prepareCopyPaths();
    void prepareCopyPlan();
    void startCopyItem(bool confirmed = false, bool replace = false);
    void advanceCopy();
    void finishCopy(bool cancelled);
    Session session_;
    bool treeSizesVisible_ = false;
    QPointer<FileViewer> viewer_, preview_;
    QPointer<ArchiveWindow> archive_;
    void openViewer(bool tagged=false,bool alternate=false);
    void openArchive(bool create=false,bool tagged=false);
    QPointer<CompareWindow> comparison_;
    QPointer<ExecuteWindow> execution_;
    void beginExecute();
    bool makePrompt_ = false, makeJump_ = false;
    QString makeInput_, makeBase_;
    QStringList makeHistory_;
    int makeCursor_ = 0, makeHistoryIndex_ = -1;
    QFutureWatcher<MakeDirectoryResult> makeWatcher_;
    void beginMakePrompt();
    void makePromptKey(QKeyEvent *);
    void startMake();
    bool prunePrompt_ = false, prunePartial_ = false, pruneProceed_ = false, pendingRootPrune_ = false;
    bool pruneHelp_ = false;
    PruneOptions pruneOptions_;
    FileMetadata pruneSource_;
    QMap<QString, FileMetadata> pruneLogged_;
    QString pruneInput_;
    int pruneCursor_ = 0, pruneSpinner_ = 0;
    QTimer pruneTimer_;
    QFutureWatcher<PruneResult> pruneWatcher_;
    void connectPrune();
    void beginPrune();
    void startPrune();
    void pruneKey(QKeyEvent *);
    void drawPrune(QPainter &);
    bool deletePrompt_ = false;
    bool deleteDirectory_ = true;
    bool deleteBatch_ = false, deleteBatchError_ = false;
    bool deleteBatchChoice_ = false, deleteEachPrompt_ = false, deleteConfirmEach_ = false;
    bool deleteHadError_ = false, deleteBranchDirectory_ = false;
    QString deleteBranch_, pendingBranchCheck_, deleteSummary_;
    QStringList deleteTargets_;
    int deleteIndex_ = 0, deleteCount_ = 0, deleteSkipped_ = 0;
    QString deleteErrorMessage_;
    QString deleteReadError_;
    QString deletePath_;
    QFutureWatcher<DeleteResult> deleteWatcher_;
    void beginDeletePrompt(bool emptyBranch = false);
    void startDelete();
    void beginDeleteTagged();
    void startDeleteBatchItem(bool confirmed = false);
    void advanceDeleteBatch();
    void finishDeleteBatch(bool cancelled);
    bool comparePrompt_ = false, compareReplace_ = false;
    QString compareFirst_, compareInput_, compareDirectory_;
    QStringList compareCandidates_;
    int compareCandidate_ = 0, compareCursor_ = 0;
    void beginCompare();
    void compareTagged();
    void openComparison(const QString &first, const QString &second);
    void comparePromptKey(QKeyEvent *);

    bool pendingAutoview_ = false;
    View autoviewReturn_ = View::Directory;
    int autoviewPercent_ = 40;
    void startAutoview();
    void stopAutoview();
    void updateAutoview();
    int autoviewWidth() const;

    QMap<QString, Session> rootHistory_, otherRootHistory_, loggedRoots_;
    QMap<QString, QPair<int, int>> rootScroll_, otherRootScroll_;
    qint64 otherAvailableBytes_ = 0;
    QString initialRoot_;
    QString pendingParent_;
    bool pendingParentSelection_ = false, pendingRootDelete_ = false;
    void navigateParent(bool keepSelection = false);
    bool mountPrompt_ = false;
    QVector<MountPoint> mounts_;
    QString mountInput_;
    int mountIndex_ = 0;
    void beginMountPrompt();
    void drawMountPrompt(QPainter &);
    void mountPromptKey(QKeyEvent *);
    std::optional<Session> otherSession_;
    QSet<QString> pendingMergedTags_;
    int activeSide_ = 0;
    int otherTreeTop_ = 0, otherFileTop_ = 0;
    std::array<qint64, 6> otherStats_{};
    int splitStats_ = 0;
    bool splitPrompt_ = false;
    int paintingSide_ = -1;
    int paintingColumns_ = 0;
    bool paintingActive_ = true;
    ConsoleFont consoleFont_;
    QFutureWatcher<ScanResult> watcher_;
    QFutureWatcher<FileSearchResult> searchWatcher_;
    Cancellation cancel_;
    QTimer clock_;
    QDate filterDate_ = QDate::currentDate();
    bool busy_ = false;
    bool preserveTags_ = false;
    bool help_ = false;
    bool quitPrompt_ = false;
    bool promptedSpell_ = false;
    bool activeSpell_ = false;
    bool searchPrompt_ = false;
    bool filespecPrompt_ = false;
    bool invertPrompt_ = false;
    bool sortPrompt_ = false;
    bool sameSort_ = true, sortDraftSame_ = true;
    SortOptions sortDraft_;
    void sortPromptKey(QKeyEvent *);
    QString filespecInput_;
    int filespecCursor_ = 0;
    bool searching_ = false;
    bool searchError_ = false;
    SearchOptions searchOptions_;
    std::optional<SearchOptions> lastExecutedSearch_;
    QString searchInput_;
    int searchCursor_ = 0;
    struct PromptHistory {
        QStringList entries;
        QMap<QString, QString> marks;
        QString file, key, label;
        int limit = 255;
        bool visible = false, dirty = false;
        int index = 0, top = 0, filter = 0, sort = 0;
    };
    PromptHistory searchHistory_, filespecHistory_, graftHistory_, stampHistory_;
    PromptHistory &promptHistory();
    const PromptHistory &promptHistory() const;
    void initializeHistories();
    QStringList historyItems() const;
    QRect historyRect() const;
    void keepHistoryVisible();
    void openHistory(bool newest);
    void historyKey(QKeyEvent *);
    void drawHistory(QPainter &);
    void rememberSearch(const QString &);
    void rememberHistory(PromptHistory &, const QString &);
    void trimHistory(PromptHistory &);
    bool saveHistory(PromptHistory &);
    bool loadHistory(PromptHistory &);
    QStringList searchTargets_;
    int searchIndex_ = 0;
    int searchHits_ = 0, searchSpinner_ = 0;
    QVector<qint64> searchWeights_;
    qint64 searchReadBytes_ = 0;
    std::shared_ptr<std::atomic<qint64>> searchFileBytes_;
    QElapsedTimer searchElapsed_;
    QTimer searchProgressTimer_;
    FileSearchResult lastSearchError_;
    QMap<int, bool> errorPolicies_;
    QString status_;
    qint64 availableBytes_ = 0;
    qint64 totalBytes_ = 0;
    qint64 taggedBytes_ = 0;
    int totalFiles_ = 0;
    int taggedFiles_ = 0;
    int matchingFiles_ = 0;
    qint64 matchingBytes_ = 0;
    Qt::KeyboardModifiers modifiers_{};
    int menuMode_ = 0;
    int columns_ = 0;
    int rows_ = 0;
    int cellWidth_ = 0;
    int cellHeight_ = 0;
    int ascent_ = 0;
    int treeTop_ = 0;
    int fileTop_ = 0;
    FileDisplay fileDisplay_ = FileDisplay::Details;
    int extensionOffset_ = 0;
    struct FileLayout {
        int rows, columns, stride, width;
        int nameWidth = 0, extensionWidth = 0, sizeWidth = 0, attrsWidth = 0, dateWidth = 0;
        int capacity() const { return rows * columns; }
    };
    FileLayout fileLayout(int top, int end) const;
    void cycleFileDisplay();
    int fileAtCell(int x, int y) const;

    void refresh();
    int contentRight() const;
    int bottom() const;
    int divider() const;
    void keepVisible();
    QString &spellText();
    void enterDirectory();
    void drawText(QPainter &, int x, int y, const QString &, const QColor &, int limit = -1);
    void drawCommands(QPainter &, int x, int y, const QString &, int limit = -1);
    void drawCursor(QPainter &, int x, int y, const QString &glyph);
    void drawInput(QPainter &, int x, int y, const QString &label, const QString &input, int cursor, const QColor &, int limit = -1);
    void horizontal(QPainter &, int y, int left, int right);
    void drawFiles(QPainter &, int top, int end, bool selected);
    void drawStats(QPainter &);
    void drawHelp(QPainter &);
    void notImplemented();
    void beginSearchPrompt();
    void startSearch();
    void searchNext();
    void finishSearch(bool cancelled);
    void drawSearchProgress(QPainter &p);
    void searchPromptKey(QKeyEvent *);
    void searchErrorKey(int key);
    void filespecPromptKey(QKeyEvent *);
    bool applyFilespec(const QString &);
    void toggleSplit();
    void switchPane();
    void swapPaneState();
    void mergeTags();
    int paneWidth(int side) const;
    int layoutColumns() const;
    bool showStatistics() const;
    void drawPane(QPainter &);
    bool activateMousePane(int column);
};

}
