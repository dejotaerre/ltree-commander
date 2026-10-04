#include "ui/theme.h"
#include "ui/treewindow.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QDateTime>
#include <QDir>
#include <QClipboard>
#include <QGuiApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QStorageInfo>
#include <QTextBoundaryFinder>
#include <QFontMetricsF>
#include <QtMath>
#include <QWheelEvent>
#include <algorithm>

namespace ltree {
namespace {
const QColor cyan = theme::labels;
const QColor yellow = theme::normal;
const QColor white = theme::selection;
QString number(qint64 n) { return QLocale(QLocale::Spanish, QLocale::Uruguay).toString(n); }
QString viewName(View view)
{
    switch (view) {
    case View::Tree: return "DISK";
    case View::Branch: return "BRANCH";
    case View::Showall: return "SHOWALL";
    case View::Global: return "GLOBAL";
    default: return "DIRECTORY";
    }
}
}

TreeWindow::TreeWindow(const QString &root, QWidget *parent) : QWidget(parent), session_(root)
{
    connectGraft();
    connectPrune();
    connectLink();
    connectCopy();
    connectMetadata();
    connectStamp();
    connectRename();
    connectDirectoryCompare();
    connect(&statsTimer_,&QTimer::timeout,this,[this]{if(extendedStats_){refreshExtendedStats();update();}});
    initializeHistories();
    initialRoot_ = session_.root;
    setWindowTitle("LTree Commander " LTREE_VERSION " — " + session_.root);
    availableBytes_ = QStorageInfo(session_.root).bytesAvailable();
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(14);
    setFont(mono);
    const QFontMetricsF metrics(mono);
    cellWidth_ = qCeil(metrics.horizontalAdvance('M'));
    cellHeight_ = qCeil(metrics.height());
    ascent_ = qCeil(metrics.ascent());
    if (consoleFont_.available()) { cellWidth_ = 8; cellHeight_ = 16; ascent_ = 13; }
    setMinimumSize(cellWidth_ * 80, cellHeight_ * 25);
    resize(cellWidth_ * 160, cellHeight_ * 48);
    connect(&watcher_, &QFutureWatcher<ScanResult>::finished, this, [this] {
        const auto result = watcher_.result();
        bool prepareDelete = false, preparePrune = false;
        const bool parentCancelled = !pendingParent_.isEmpty() && cancel_->load();
        if (!pendingParent_.isEmpty()) {
            const Session previous = session_;
            if (!parentCancelled && session_.expandToParent(result)) {
                rootHistory_.insert(previous.root, previous);
                rootScroll_.insert(previous.root, {treeTop_, fileTop_});
                loggedRoots_.insert(previous.root, previous);
                treeTop_ = fileTop_ = 0;
                availableBytes_ = QStorageInfo(session_.root).bytesAvailable();
                if (pendingParentSelection_) {
                    session_.selectDirectory(previous.root);
                    prepareDelete = pendingRootDelete_ && session_.directory == previous.root;
                    preparePrune = pendingRootPrune_ && session_.directory == previous.root;
                }
            }
            pendingParent_.clear();
            pendingParentSelection_ = pendingRootDelete_ = pendingRootPrune_ = false;
        } else session_.apply(result, preserveTags_);
        busy_ = false;
        int errors = 0;
        QString firstError;
        for (const auto &dir : result.directories) if (!dir.error.isEmpty()) {
            ++errors;
            if (firstError.isEmpty()) firstError = dir.error;
        }
        if (errors) status_ = QString("%1 error(s): %2").arg(errors).arg(firstError);
        else status_ = result.cancelled || parentCancelled ? "Loading cancelled; completed directories are retained" : QString{};
        if (pendingAutoview_) { pendingAutoview_ = false; if (!result.cancelled && !errors) startAutoview(); }
        if (prepareDelete) beginDeletePrompt(deleteBranchDirectory_);
        if (preparePrune) beginPrune();
        if (!pendingBranchCheck_.isEmpty()) {
            const QString branch=pendingBranchCheck_;pendingBranchCheck_.clear();status_=deleteSummary_;
            bool empty=!result.cancelled && !cancel_->load() && !errors && !result.directories.isEmpty();
            QSet<QString> directories;
            for(const auto &dir:result.directories){directories.insert(dir.path);if(!dir.files.isEmpty())empty=false;}
            for(const auto &dir:result.directories)for(const auto &child:dir.children)if(!directories.contains(child))empty=false;
            if(empty && branch!="/"){
                stopAutoview();session_.returnToTree();session_.selectDirectory(branch);beginDeletePrompt(true);
            }else if(result.cancelled || cancel_->load())status_+="; directory check cancelled";
            else if(errors)status_+="; directory check: "+firstError;
            else status_+="; branch directory retained";
        }
        refresh();
    });
    connect(&clock_, &QTimer::timeout, this, [this] {
        clock_.start(60000 - QTime::currentTime().msecsSinceStartOfDay() % 60000);
        const QDate today = QDate::currentDate();
        if (filterDate_ != today && !busy()) {
            filterDate_ = today;
            if (session_.filespec.usesToday()) { session_.rebuild(); refresh(); return; }
        }
        update();
    });
    connect(&makeWatcher_,&QFutureWatcher<MakeDirectoryResult>::finished,this,[this]{
        const auto result=makeWatcher_.result();busy_=false;
        session_.apply(result.scan,true);
        if(otherSession_)otherSession_->syncLoggingFrom(session_);
        if(result.cancelled){
            makePrompt_=false;status_=QString("Creation cancelled; directories created: %1").arg(result.created.size());
        }else if(!result.error.isEmpty()){
            status_=result.error;
            if(!result.created.isEmpty())status_+=QString("; directories created: %1").arg(result.created.size());
        }else{
            makePrompt_=false;makeHistory_.removeAll(makeInput_);makeHistory_.prepend(makeInput_);
            if(makeHistory_.size()>64)makeHistory_.removeLast();
            if(makeJump_)session_.selectDirectory(result.path);
            status_="Directory created: "+result.path;
            for(const auto &dir:result.scan.directories)if(!dir.error.isEmpty()){
                status_+="; read error: "+dir.error;break;
            }
        }
        availableBytes_=QStorageInfo(session_.root).bytesAvailable();refresh();
    });
    connect(&deleteWatcher_,&QFutureWatcher<DeleteResult>::finished,this,[this]{
        const auto result=deleteWatcher_.result();busy_=false;
        for(const auto &directory:result.removedDirectories)session_.removeDirectory(directory);
        if(!result.deleted && (!result.removedDirectories.isEmpty() || deleteBranchDirectory_)){
            session_.apply(result.scan,true);if(otherSession_)otherSession_->syncLoggingFrom(session_);
        }
        if(result.deleted){
            if(result.directory){
                session_.removeDirectory(result.path);
            }else session_.removeFile(result.path);
            session_.apply(result.scan,true);
            if(otherSession_)otherSession_->syncLoggingFrom(session_);
            status_=QString(result.directory?"Directory deleted: ":"File deleted: ")+result.path;
            for(const auto &dir:result.scan.directories)if(!dir.error.isEmpty()){
                status_+="; read error: "+dir.error;break;
            }
        }else status_=result.cancelled?"Deletion cancelled":result.error;
        if(deleteBatch_){
            if(result.deleted)++deleteCount_;
            for(const auto &dir:result.scan.directories)if(deleteReadError_.isEmpty() && !dir.error.isEmpty())deleteReadError_=dir.error;
            if(result.cancelled || cancel_->load()){finishDeleteBatch(true);return;}
            if(!result.error.isEmpty()){
                deleteHadError_=true;
                if(deleteErrorMessage_.isEmpty())deleteErrorMessage_=result.error;
                if(deleteConfirmEach_){deleteBatchError_=true;deleteErrorMessage_=result.error;refresh();return;}
                ++deleteSkipped_;
            }
            advanceDeleteBatch();return;
        }
        deletePrompt_=false;
        availableBytes_=QStorageInfo(session_.root).bytesAvailable();refresh();
    });
    searchProgressTimer_.setInterval(100);
    connect(&searchProgressTimer_, &QTimer::timeout, this, [this] {
        searchSpinner_ = (searchSpinner_ + 1) % 4;
        update();
    });
    connect(&searchWatcher_, &QFutureWatcher<FileSearchResult>::finished, this, [this] {
        const auto result = searchWatcher_.result();
        searchReadBytes_ += searchFileBytes_->exchange(0);
        if (result.outcome == SearchOutcome::Cancelled) { finishSearch(true); return; }
        if (result.outcome == SearchOutcome::Error) {
            if (errorPolicies_.contains(result.errorType)) {
                if (!errorPolicies_.value(result.errorType)) session_.tags.remove(searchTargets_[searchIndex_]);
            } else {
                lastSearchError_ = result;
                searchError_ = true;
                searchProgressTimer_.stop();
                status_ = result.error;
                refresh();
                return;
            }
        } else if (result.outcome == SearchOutcome::NotFound) session_.tags.remove(searchTargets_[searchIndex_]);
        if (result.outcome == SearchOutcome::Found) ++searchHits_;
        ++searchIndex_;
        searchNext();
    });
    clock_.setSingleShot(true);
    clock_.start(60000 - QTime::currentTime().msecsSinceStartOfDay() % 60000);
    load(false, true);
}

TreeWindow::~TreeWindow()
{
    if(searchHistory_.dirty)saveHistory(searchHistory_);
    if(filespecHistory_.dirty)saveHistory(filespecHistory_);
    if(graftHistory_.dirty)saveHistory(graftHistory_);
    if(stampHistory_.dirty)saveHistory(stampHistory_);
    if (cancel_) cancel_->store(true);
    watcher_.waitForFinished();
    searchWatcher_.waitForFinished();
    makeWatcher_.waitForFinished();
    deleteWatcher_.waitForFinished();
    copyWatcher_.waitForFinished();
    stampWatcher_.waitForFinished();
    permissionsWatcher_.waitForFinished();
    permissionsBatchWatcher_.waitForFinished();
    renameWatcher_.waitForFinished();
    directoryCompareWatcher_.waitForFinished();
    duplicateWatcher_.waitForFinished();
    linkWatcher_.waitForFinished();
    pruneWatcher_.waitForFinished();
    graftWatcher_.waitForFinished();
    graftBrowseWatcher_.waitForFinished();
}

void TreeWindow::load(bool recursive, bool preserveTags, const QString &directory)
{
    if (busy()) return;
    busy_ = true;
    preserveTags_ = preserveTags;
    cancel_ = std::make_shared<std::atomic_bool>(false);
    status_ = recursive ? "Logging branch… Esc cancels" : "Logging directory… Esc cancels";
    const QString path = directory.isEmpty()?(session_.view==View::Global && session_.currentFile()?QFileInfo(session_.currentFile()->path).absolutePath():session_.directory):directory;
    const auto cancel = cancel_;
    watcher_.setFuture(QtConcurrent::run([path, recursive, cancel] {
        return scanDirectories(path, recursive, cancel);
    }));
    update();
}

int TreeWindow::paneWidth(int side) const { return side == 0 ? columns_ / 2 : columns_ - columns_ / 2; }
int TreeWindow::layoutColumns() const { return paintingColumns_ ? paintingColumns_ : isSplit() ? paneWidth(activeSide_) : columns_; }
bool TreeWindow::showStatistics() const
{
    if (preview_ && paintingActive_) return false;
    if (!isSplit()) return true;
    const int mode = columns_ < 102 ? 0 : columns_ < 122 ? std::min(splitStats_, 1) : splitStats_;
    return mode == 2 || (mode == 1 && (paintingSide_ < 0 || paintingSide_ == activeSide_));
}
int TreeWindow::contentRight() const
{
    if (preview_ && paintingActive_) return autoviewWidth() - 1;
    return layoutColumns() - (showStatistics() ? 23 : 1);
}
int TreeWindow::bottom() const { return rows_ - 5; }
int TreeWindow::divider() const { return 2 + (bottom() - 2) * 2 / 3; }
QString &TreeWindow::spellText() { return session_.view == View::Tree ? session_.treeSpell : session_.fileSpell; }

void TreeWindow::keepVisible()
{
    const int treeHeight = std::max(1, divider() - 2);
    const int treeIndex = session_.treeIndex();
    treeTop_ = std::clamp(treeTop_, std::max(0, treeIndex - treeHeight + 1), treeIndex);
    const auto layout = fileLayout(session_.view == View::Tree ? divider() + 1 : 2, bottom());
    if (layout.columns == 1) fileTop_ = std::clamp(fileTop_, std::max(0, session_.fileIndex - layout.rows + 1), session_.fileIndex);
    else {
        fileTop_ = std::max(0, fileTop_) / layout.rows * layout.rows;
        if (session_.fileIndex < fileTop_) fileTop_ = session_.fileIndex / layout.rows * layout.rows;
        else if (session_.fileIndex >= fileTop_ + layout.capacity())
            fileTop_ = (session_.fileIndex / layout.rows - layout.columns + 1) * layout.rows;
        const int lastTop = std::max(0, (std::max(0, int(session_.files().size()) - 1) / layout.rows - layout.columns + 1) * layout.rows);
        fileTop_ = std::min(fileTop_, lastTop);
    }
}

void TreeWindow::refresh()
{
    if(globalReturn_){if(session_.view==View::Tree)endGlobal();else publishGlobal();}
    keepVisible();
    if(infoVisible_)refreshInfo();
    const auto statistics=[](const Session &session){
        std::array<qint64,6> totals{};
        const auto count=[&](const FileEntry &file){
            ++totals[3];totals[0]+=file.size;
            if(!session.filespec.matches(file))return;
            ++totals[5];totals[2]+=file.size;
            if(session.tags.contains(file.path)){++totals[4];totals[1]+=file.size;}
        };
        if(session.view==View::Tree){
            for(const auto &dir:session.directories())for(const auto &file:dir.files)count(file);
        }else for(const auto &file:session.scopeFiles())count(file);
        return totals;
    };
    const auto totals=statistics(session_);
    totalBytes_=totals[0];taggedBytes_=totals[1];matchingBytes_=totals[2];
    totalFiles_=int(totals[3]);taggedFiles_=int(totals[4]);matchingFiles_=int(totals[5]);
    if(otherSession_)otherStats_=statistics(*otherSession_);
    QString current = session_.directory;
    if (session_.view != View::Tree && session_.currentFile()) current = session_.currentFile()->path;
    setWindowTitle("LTree Commander " LTREE_VERSION " — " + current);
    updateAutoview();
    update();
}

void TreeWindow::resizeEvent(QResizeEvent *)
{
    if (viewer_) viewer_->setGeometry(rect());
    if (archive_) archive_->setGeometry(rect());
    if (comparison_) comparison_->setGeometry(rect());
    if (execution_) execution_->setGeometry(rect());
    columns_ = width() / cellWidth_;
    rows_ = height() / cellHeight_;
    keepVisible();
    updateAutoview();
}

void TreeWindow::drawText(QPainter &p, int x, int y, const QString &text, const QColor &color, int limit)
{
    if (limit < 0) limit = (paintingColumns_ ? paintingColumns_ : columns_) - x;
    if (limit <= 0 || y < 0 || y >= rows_) return;
    p.save();
    p.setPen(color);
    // Cada grafema ocupa una celda: los avances fraccionarios no deben desplazar las columnas.
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, text);
    int start = 0;
    for (int col = 0; col < limit; ++col) {
        const int end = int(graphemes.toNextBoundary());
        if (end < 0) break;
        p.setClipRect((x + col) * cellWidth_, y * cellHeight_, cellWidth_, cellHeight_);
        QString glyph = text.mid(start, end - start);
        if (glyph.size() == 1 && glyph[0].category() == QChar::Other_Control) glyph = "?";
        const int px = (x + col) * cellWidth_, py = y * cellHeight_;
        const QString boxes = "─│┌┐└┘├┤┬┴┼";
        const int box = glyph.size() == 1 ? int(boxes.indexOf(glyph)) : -1;
        if (box >= 0) {
            // Brazos hasta el límite de la celda: no dependen de métricas ni del antialias.
            const int masks[] = {3, 12, 10, 9, 6, 5, 14, 13, 11, 7, 15};
            const int mask = masks[box], mx = cellWidth_ / 2, my = cellHeight_ / 2;
            if (mask & 1) p.fillRect(px, py + my, mx + 1, 1, color);
            if (mask & 2) p.fillRect(px + mx, py + my, cellWidth_ - mx, 1, color);
            if (mask & 4) p.fillRect(px + mx, py, 1, my + 1, color);
            if (mask & 8) p.fillRect(px + mx, py + my, 1, cellHeight_ - my, color);
        } else if (!consoleFont_.available() || !consoleFont_.draw(p, px, py, glyph, color)) {
            p.drawText(px, py + ascent_, glyph);
        }
        start = end;
    }
    p.restore();
}

void TreeWindow::drawCommands(QPainter &p, int x, int y, const QString &text, int limit)
{
    if(limit<0)limit=columns_-x;
    int column=0,start=0;
    // Los corchetes delimitan teclas en textos fijos; no se aplican a rutas ni mensajes del usuario.
    while(start<text.size() && column<limit){
        const bool key=text[start]=='[';
        const int next=int(text.indexOf(key?']':'[',key?start+1:start));
        const int end=next<0?int(text.size()):next;
        const QString span=text.mid(start+(key?1:0),end-start-(key?1:0));
        drawText(p,x+column,y,span,key?yellow:cyan,limit-column);
        column+=int(span.size());start=end+(key?1:0);
    }
}

void TreeWindow::drawCursor(QPainter &p, int x, int y, const QString &glyph)
{
    p.fillRect(x*cellWidth_,y*cellHeight_,cellWidth_,cellHeight_,theme::selection);
    drawText(p,x,y,glyph,Qt::black,1);
}

void TreeWindow::drawInput(QPainter &p, int x, int y, const QString &label, const QString &input, int cursor, const QColor &color, int limit)
{
    if(limit<0)limit=columns_-x;
    if(limit<1)return;
    const QString prefix=label.left(limit-1);
    const int available=limit-int(prefix.size());
    const int start=std::max(0,cursor-available+1);
    drawText(p,x,y,prefix+input.mid(start),color,limit);
    drawCursor(p,x+int(prefix.size())+cursor-start,y,input.mid(cursor,1));
}

void TreeWindow::horizontal(QPainter &p, int y, int left, int right)
{
    drawText(p, left, y, QString(std::max(0, right - left + 1), QChar(0x2500)), cyan);
}

TreeWindow::FileLayout TreeWindow::fileLayout(int top, int end) const
{
    const int available = std::max(1, contentRight() - 1);
    const int minimum = fileDisplay_ == FileDisplay::Name ? 24 : 42;
    const bool multiple = fileDisplay_ == FileDisplay::Name || fileDisplay_ == FileDisplay::SizeAttributes;
    const int columns = multiple ? std::max(1, available / minimum) : 1;
    const int stride = available / columns;
    FileLayout layout{std::max(1, end - top), columns, stride, stride - (columns > 1 ? 1 : 0)};
    if (fileDisplay_ == FileDisplay::LongName) { layout.nameWidth = layout.width - 1;return layout; }
    if (fileDisplay_ == FileDisplay::Details) layout.dateWidth = layout.width >= 54 ? 16 : layout.width >= 44 ? 10 : 0;
    if (fileDisplay_ == FileDisplay::Details || fileDisplay_ == FileDisplay::SizeAttributes) {
        layout.sizeWidth = layout.width >= 26 ? 11 : layout.width >= 18 ? 8 : 0;
        layout.attrsWidth = layout.width >= 26 ? 3 : 0;
    }
    int remaining = layout.width;
    for (const int width : {layout.dateWidth, layout.attrsWidth, layout.sizeWidth}) if (width) remaining -= width + 1;
    const int initialExtension = fileDisplay_ == FileDisplay::Details && layout.dateWidth == 16 ? 9 : 4;
    layout.extensionWidth = std::clamp(initialExtension + extensionOffset_, 1, std::max(1, remaining - 10));
    layout.nameWidth = std::max(1, remaining - layout.extensionWidth - 3);
    return layout;
}

int TreeWindow::fileAtCell(int x, int y) const
{
    if (x < 1 || x >= contentRight() || y < 2 || y >= bottom()) return -1;
    const auto layout = fileLayout(2, bottom());
    const int column = (x - 1) / layout.stride;
    const int index = fileTop_ + column * layout.rows + y - 2;
    return column < layout.columns && (x - 1) % layout.stride < layout.width && index < session_.files().size() ? index : -1;
}

void TreeWindow::cycleFileDisplay()
{
    fileDisplay_ = FileDisplay((int(fileDisplay_) + 1) % 4);
    const QStringList labels{"Name", "Name, size and attributes", "Name, size, attributes and date", "Long name"};
    status_ = "File display: " + labels[int(fileDisplay_)];
}

void TreeWindow::drawFiles(QPainter &p, int top, int end, bool selected)
{
    const int right = contentRight();
    if (session_.files().isEmpty()) {
        const auto &dir = session_.currentDirectory();
        const QString message = !dir.error.isEmpty() ? dir.error : !dir.loaded ? "Directory not logged" : "No files";
        drawText(p, 2, top, message, yellow, right - 3);
        return;
    }
    const auto layout = fileLayout(top, end);
    const int first = selected ? fileTop_ : 0;
    // Llenar cada columna de arriba abajo mantiene el orden del listado.
    for (int offset = 0; offset < layout.capacity() && first + offset < session_.files().size(); ++offset) {
        const int index = first + offset, row = top + offset % layout.rows;
        const int x = 1 + offset / layout.rows * layout.stride;
        const auto &file = session_.files()[index];
        const bool current = selected && index == session_.fileIndex;
        if (current && paintingActive_) p.fillRect(x * cellWidth_, row * cellHeight_, layout.width * cellWidth_, cellHeight_, white);
        const QColor ink = current && paintingActive_ ? Qt::black : theme::fileColor(file.name);
        if (session_.tags.contains(file.path)) drawText(p, x, row, "♦", current && paintingActive_ ? Qt::black : cyan, 1);
        if (current && !paintingActive_) {
            p.fillRect(x * cellWidth_, row * cellHeight_, cellWidth_, cellHeight_, white);
            drawText(p, x, row, session_.tags.contains(file.path) ? "♦" : "►", Qt::black, 1);
        }
        if (fileDisplay_ == FileDisplay::LongName) {
            drawText(p, x + 1, row, file.name, ink, layout.nameWidth);continue;
        }
        QString base = file.name, extension;
        const int dot = int(base.lastIndexOf('.'));
        if (dot > 0) { extension = base.mid(dot);base = base.left(dot); }
        int column = x + layout.width;
        if (layout.dateWidth) {
            column -= layout.dateWidth + 1;
            drawText(p, column, row, file.modified.toString(layout.dateWidth == 16 ? "yyyy-MM-dd HH:mm" : "yyyy-MM-dd"), ink, layout.dateWidth);
        }
        if (layout.attrsWidth) {
            column -= layout.attrsWidth + 1;
            const QString attrs = QString(file.writable ? "." : "r") + (file.hidden ? "h" : ".") + (file.symlink ? "l" : ".");
            drawText(p, column, row, attrs, ink, layout.attrsWidth);
        }
        if (layout.sizeWidth) {
            column -= layout.sizeWidth + 1;
            drawText(p, column, row, number(file.size).rightJustified(layout.sizeWidth), ink, layout.sizeWidth);
        }
        column -= layout.extensionWidth + 1;
        drawText(p, x + 1, row, base, ink, layout.nameWidth);
        drawText(p, column, row, extension, ink, layout.extensionWidth);
    }
}

void TreeWindow::drawStats(QPainter &p)
{
    const int x = contentRight() + 1;
    const int w = layoutColumns() - x - 1;
    drawText(p, x, 2, "FILE " + QString(session_.filespec.inverted ? "!" : "") + session_.filespec.text(), cyan, w);
    horizontal(p, 3, x, layoutColumns() - 2);
    drawText(p, x - 1, 3, "├", cyan, 1);
    drawText(p, layoutColumns() - 1, 3, "┤", cyan, 1);
    drawText(p, x, 4, "ROOT", cyan, w);
    drawText(p, x, 5, (session_.view==View::Global?QString("All logged locations"):session_.root == "/" ? QString("/") : QFileInfo(session_.root).fileName()), yellow, w);
    drawText(p, x, 6, "  Available", cyan, w);
    drawText(p, x, 7, (session_.view==View::Global?QString("N/A"):number(availableBytes_)).rightJustified(w), yellow, w);
    horizontal(p, 8, x, layoutColumns() - 2);
    drawText(p, x - 1, 8, "├", cyan, 1);
    drawText(p, layoutColumns() - 1, 8, "┤", cyan, 1);
    drawText(p, x, 9, viewName(session_.view) + " Statistics", cyan, w);
    for (int section = 0; section < 3; ++section) {
        const int y = 10 + section * 3;
        drawText(p, x, y, QString("        ") + QStringList{"Total", "Matching", "Tagged"}[section], cyan, w);
        drawText(p, x, y + 1, "Files", cyan, w);
        drawText(p, x + 6, y + 1, number(section == 2 ? taggedFiles_ : section == 1 ? matchingFiles_ : totalFiles_).rightJustified(w - 6), yellow, w - 6);
        drawText(p, x, y + 2, "Bytes", cyan, w);
        drawText(p, x + 6, y + 2, number(section == 2 ? taggedBytes_ : section == 1 ? matchingBytes_ : totalBytes_).rightJustified(w - 6), yellow, w - 6);
    }
    if (bottom() > 24) {
        const bool tree = session_.view == View::Tree;
        drawText(p, x, 20, tree ? "Current Directory" : "Current File", cyan, w);
        drawText(p, x, 21, tree ? QFileInfo(session_.directory).fileName() :
                     session_.currentFile() ? session_.currentFile()->name : "", yellow, w);
    }
    drawText(p, x, bottom() - 1, "LTree 0.1 alpha", cyan, w);
}

void TreeWindow::drawHelp(QPainter &p)
{
    drawHelpDocument(p, rect(), helpDocument_, cellWidth_, cellHeight_, ascent_, &consoleFont_);
}

void TreeWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setFont(font());
    p.fillRect(rect(), theme::background);
    if(extendedStats_){drawExtendedStats(p);return;}
    if (help_) { drawHelp(p); return; }
    if(duplicatePrompt_){drawDuplicates(p);return;}
    if(directoryComparePrompt_){drawDirectoryCompare(p);return;}
    if (mountPrompt_) { drawMountPrompt(p); return; }
    const int end = bottom();
    if (graftPrompt_) {
        if (graftBrowser_) { drawGraftBrowse(p);return; }
        drawGraft(p);
        if (graftHelp_) return;
    }
    if (prunePrompt_) {
        drawPrune(p);
        if (pruneHelp_) return;
    }
    if (searching_ && !searchError_) drawSearchProgress(p);
    if (isSplit()) {
        for (int side = 0; side < 2; ++side) {
            paintingSide_ = side;
            paintingColumns_ = paneWidth(side);
            paintingActive_ = side == activeSide_;
            p.save();
            const int offset = side == 0 ? 0 : paneWidth(0) * cellWidth_;
            p.setClipRect(offset, 0, paintingColumns_ * cellWidth_, (end + 1) * cellHeight_);
            p.translate(offset, 0);
            if (!paintingActive_) swapPaneState();
            keepVisible();drawPane(p);
            if (!paintingActive_) swapPaneState();
            p.restore();
        }
        paintingColumns_ = 0; paintingSide_ = -1; paintingActive_ = true;
    } else drawPane(p);
    if(infoVisible_)drawInfo(p);
    if (graftPrompt_) { if (graftHistory_.visible) drawHistory(p);return; }
    if (prunePrompt_ || (searching_ && !searchError_)) return;
    const bool tree = session_.view == View::Tree;
    const int menu = modifiers_.testFlag(Qt::AltModifier) ? 2 : modifiers_.testFlag(Qt::ControlModifier) ? 1 : menuMode_;
    drawText(p, 0, end + 1, tree ? "DIR" : "FILE", cyan, 5);
    drawText(p, 0, end + 2, menu == 1 ? "CTRL" : menu == 2 ? "ALT" : "COMMANDS", cyan, 9);
    QString first, second;
    if (menu == 0) {
        first = tree ? "[A]vail  [B]ranch  [C]ompare  [D]el  [F]ilespec  [G]lobal  s[H]ortcut  [L]og  [M]ake  [R]ename  [S]howall  [T]ag  [U]ntag  e[X]ecute  [Q]uit" : "[A]ttributes  [C]opy  [D]el  [E]dit  [F]ilespec  [J]FC  [L]og  [M]ove  [N]ew date  [O]pen  [R]ename  [T]ag  [U]ntag  [V]iew  e[X]ecute  [Q]uit";
        second = "[F1] Help  [F3] Refresh  [F4] Menu  [F7] Autoview  [F8] Split  [Enter] Tree/File or archive  [?] Stats  [</>] Location";
        if(!tree)second+="  [F5] Create archive";
    } else if (menu == 1) {
        first = tree ? "[B]ranch tagged  [S]howall tagged  [G]lobal tagged  [T]ag all  [U]ntag all" : "[A]ttributes  [C]opy  [D]elete  [M]ove  [N]ew date  [R]ename  [S]earch  [J]FC  [V]iew  [T]ag all  [U]ntag all";
        second = tree?"[F1] Help   [Alt+Enter] Fullscreen":"[F5] Archive tagged   [Enter/F4] Tags-only   [F1] Help   [Alt+Enter] Fullscreen";
    } else {
        first = tree ? "[A] Permissions  [C]ompare  [F]ile display  [G]raft  [P]rune  [I]nfo  [S]ort" : "[C]opy paths  [F]ile display  [M]ove paths  [K] Compare filter  [V]iew  [I]nfo  [S]ort";
        second = "[F3] Relog   [F7] Zoom   [Enter] Fullscreen   [F1] Help";
    }
    drawCommands(p, 10, end + 1, first);
    drawCommands(p, 10, end + 2, second);
    QString footer = status_.isEmpty() ? "Sort: " + sortLabel(session_.sort) : status_;
    QString footerCommands;
    if (preview_) footerCommands = "AUTOVIEW: [Shift+key] = viewer   [Alt+Left/Right] = width   [F7/Enter/Esc] = return";
    if (quitPrompt_) footerCommands = "Quit? [Y] / [Q] = Yes    [N] / [Esc] = No";
    if (splitPrompt_) footerCommands = "[S] Swap   [N] No stats   [C] Current (102+)   [B] Both (122+)   [Esc] Cancel";
    if (invertPrompt_) footerCommands = "INVERT: [F] Filespec    [T] Tags    [Esc] Cancel";
    if (!promptedSpell_) {
        if (footerCommands.isEmpty()) drawText(p, 0, end + 3, footer, cyan);
        else drawCommands(p, 0, end + 3, footerCommands);
    }
    if(promptedSpell_){
        drawInput(p,0,end+3,"SPELL SEARCH: ",spellText(),int(spellText().size()),cyan,columns_-24);
        drawCommands(p,columns_-22,end+3,"[Enter] Accept / [Esc] Return");
    }
    if(linkPrompt_)drawLink(p);
    if(deletePrompt_){
        p.fillRect(0,(end+1)*cellHeight_,width(),3*cellHeight_,theme::background);
        if(deleteBatch_){
            const auto label=QString("DELETE %1 tagged files (%2 deleted): ").arg(deleteTargets_.size()).arg(deleteCount_);
            const int available=std::max(3,columns_-int(label.size()));
            const auto path=deletePath_.size()>available?"..."+deletePath_.right(available-3):deletePath_;
            drawText(p,0,end+1,label+path,cyan);
            drawText(p,0,end+2,deleteBatchChoice_?"Confirm delete for each file?":deleteEachPrompt_?"Delete this file?":deleteBatchError_?deleteErrorMessage_:busy_?QString("Processing %1/%2").arg(deleteIndex_+1).arg(deleteTargets_.size()):"Permanently delete tagged files in the current list.",yellow);
            drawCommands(p,0,end+3,deleteBatchChoice_?"[Y]: confirm each file    [N]: delete all possible    [Esc]: cancel":deleteEachPrompt_?"[Y] / [Enter]: delete    [N]: skip    [Esc]: cancel":deleteBatchError_?"[R] / [Enter]: retry    [S]: skip    [C] / [Esc]: cancel":busy_?"[Esc]: stop pending deletions":"[Enter] / [Y]: delete all    [N] / [Esc]: cancel");
        }else{
            const QString label=deleteBranchDirectory_?"DELETE empty branch directory: ":deleteDirectory_?"DELETE empty directory: ":"DELETE file: ";
            const int available=std::max(3,columns_-int(label.size()));
            const auto path=deletePath_.size()>available?"..."+deletePath_.right(available-3):deletePath_;
            drawText(p,0,end+1,label+path,cyan);
            drawText(p,0,end+2,deleteBranchDirectory_?"Delete the branch directory and its empty subdirectories?":deleteDirectory_?"Permanently delete the directory if it is empty.":"Permanently delete the selected file.",yellow);
            drawCommands(p,0,end+3,"[Enter] / [Y]: delete    [N] / [Esc]: cancel");
        }
    }
    if (comparePrompt_) {
        p.fillRect(0,(end+1)*cellHeight_,width(),3*cellHeight_,theme::background);
        const QString prefix="     with: ";
        const int start=std::max(0,compareCursor_-columns_+int(prefix.size())+1);
        const QString input=compareInput_.mid(start);
        drawText(p,0,end+1,"COMPARE file: "+compareFirst_,cyan);
        drawText(p,0,end+2,prefix+input,cyan);
        if(compareReplace_ && !compareInput_.isEmpty()){
            const int name=int(compareInput_.lastIndexOf('/'))+1;
            const int visible=std::max(name,start),x=int(prefix.size())+visible-start;
            const int count=std::min(int(compareInput_.size())-visible,columns_-x);
            if(count>0){p.fillRect(x*cellWidth_,(end+2)*cellHeight_,count*cellWidth_,cellHeight_,theme::selection);
                drawText(p,x,end+2,compareInput_.mid(visible,count),Qt::black,count);}
        }
        const int cursorX=int(prefix.size())+compareCursor_-start;
        drawCursor(p,cursorX,end+2,compareInput_.mid(compareCursor_,1));
        if(status_.isEmpty())drawCommands(p,0,end+3,"[Enter] Compare   [Tab/Shift+Tab] Candidates   [Esc] Cancel");
        else drawText(p,0,end+3,status_,yellow);
    }
    if (makePrompt_) {
        p.fillRect(0,(end+1)*cellHeight_,width(),3*cellHeight_,theme::background);
        drawText(p,0,end+1,"MAKE directory in: "+makeBase_,cyan);
        drawInput(p,0,end+2,"Name: ",makeInput_,makeCursor_,yellow);
        if(status_.isEmpty())drawCommands(p,0,end+3,QString("[Enter] Create  [Esc] Cancel  [F4] Jump (%1)  [F3] Last  [Up/Down] History").arg(makeJump_?"yes":"no"));
        else drawText(p,0,end+3,status_,cyan);
    }
    if (sortPrompt_) {
        p.fillRect(0, (end + 1) * cellHeight_, width(), 3 * cellHeight_, theme::background);
        drawCommands(p, 0, end + 1, "SORT: [N]ame [E]xt [D]ate [T]ime [L]ength [A]lpha nu[M]ber [S]ize [U]nsorted [V]alue");
        drawCommands(p, 0, end + 2, QString("(%1) [O] Order  [P] Path  [F8] %2  [F12] Reset").arg(sortLabel(sortDraft_), sortDraftSame_ ? "Same" : "Diff"));
        if(status_.isEmpty())drawCommands(p,0,end+3,"Letter: apply   [Enter]: re-sort   [Up/Down]: order   [Esc]: cancel");
        else drawText(p,0,end+3,status_,cyan);
    }
    if (filespecPrompt_) {
        p.fillRect(0, (end + 1) * cellHeight_, width(), 3 * cellHeight_, theme::background);
        drawInput(p,0,end+1,"FILESPEC: ",filespecInput_,filespecCursor_,cyan);
        drawCommands(p, 0, end + 2, "[Enter] Apply (empty = *.*)   [Esc] Cancel   [F3] Last   [Up/Down] History");
        drawText(p, 0, end + 3, status_.isEmpty() ? "Dates: MM-DD-YYYY / TODAY-7    Size: =>s1000,<=s5000 (bytes)" : status_, cyan);
    }
    if (searchPrompt_ || searchError_) {
        p.fillRect(0, (end + 1) * cellHeight_, width(), 3 * cellHeight_, theme::background);
        if (searchPrompt_) {
            const QString label = QString("SEARCH %1 tagged files for: ").arg(searchTargets_.size());
            drawInput(p,0,end+1,label,searchInput_,searchCursor_,cyan);
            const QString mode = searchModeName(searchOptions_.mode);
            drawCommands(p, 0, end + 2, QString("[F2] Case sensitive (%1)   [F4] Search for (%2)   [F3] Last").arg(searchOptions_.caseSensitive ? "yes" : "no", mode));
            if(status_.isEmpty())drawCommands(p,0,end+3,"[Enter] Search   [Esc] Cancel   [Up/Down] History");
            else drawText(p,0,end+3,status_,cyan);
        } else {
            drawText(p, 0, end + 1, "ERROR: " + lastSearchError_.error, yellow);
            drawCommands(p, 0, end + 2, "[C] Cancel  [R] Retry  [T] Tag  [U] Untag  [K] Keep errors  [L] Lose errors");
            drawText(p, 0, end + 3, searchTargets_[searchIndex_], yellow);
        }
    }
    if(copyStep_!=CopyStep::None)drawCopyPrompt(p);
    if(permissionsPrompt_)drawPermissions(p);
    if(stampPrompt_)drawStamp(p);
    if(renamePrompt_)drawRename(p);
    if(promptHistory().visible)drawHistory(p);
}

void TreeWindow::drawPane(QPainter &p)
{
    const int right = contentRight();
    const int end = bottom();
    QString path = session_.directory;
    if (session_.view != View::Tree && session_.currentFile()) path = QFileInfo(session_.currentFile()->path).absolutePath();
    drawText(p, 0, 0, session_.hasCompareFilter()?"#":session_.tagsOnly ? "♦" : session_.filespec.text() != "*.*" || session_.filespec.inverted ? "-" : " ", cyan, 1);
    drawText(p, 1, 0, path, cyan, layoutColumns() - (isSplit() ? 1 : 20));
    if (!isSplit()) drawText(p, layoutColumns() - 18, 0, QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"), cyan);
    horizontal(p, 1, 0, layoutColumns() - 1);
    horizontal(p, end, 0, layoutColumns() - 1);
    for (const auto &edge : {std::pair{0, QString("┌└")}, std::pair{right, QString("┬┴")},
                             std::pair{layoutColumns() - 1, QString("┐┘")}}) {
        p.fillRect(edge.first * cellWidth_, cellHeight_, cellWidth_, cellHeight_, theme::background);
        p.fillRect(edge.first * cellWidth_, end * cellHeight_, cellWidth_, cellHeight_, theme::background);
        drawText(p, edge.first, 1, edge.second.left(1), cyan, 1);
        drawText(p, edge.first, end, edge.second.right(1), cyan, 1);
    }
    for (int y = 2; y < end; ++y) {
        drawText(p, 0, y, "│", cyan, 1);
        drawText(p, right, y, "│", cyan, 1);
        drawText(p, layoutColumns() - 1, y, "│", cyan, 1);
    }
    if (session_.view == View::Tree) {
        const int split = divider();
        const int sizeWidth=14;
        const auto *sizes=treeSizesVisible_?&session_.branchSizes():nullptr;
        for (int y = 2, i = treeTop_; y < split && i < session_.tree().size(); ++y, ++i) {
            const auto &row = session_.tree()[i];
            const auto found = session_.directories().constFind(row.path);
            const auto &dir = found.value();
            const bool selected = row.path == session_.directory;
            if (selected && paintingActive_) p.fillRect(cellWidth_, y * cellHeight_, (right - 1) * cellWidth_, cellHeight_, white);
            QString state = dir.loaded ? " " : "+";
            if (dir.collapsed && !dir.children.isEmpty()) state = dir.loaded ? "·" : ":";
            const QColor ink=selected && paintingActive_?Qt::black:yellow;
            drawText(p, 1, y, state + row.prefix + row.name, ink, sizes?right-sizeWidth-2:right-1);
            if(sizes)drawText(p,right-sizeWidth,y,sizes->value(row.path).label(sizeWidth),ink,sizeWidth);
            if (selected && !paintingActive_) {
                p.fillRect(cellWidth_, y * cellHeight_, cellWidth_, cellHeight_, white);
                drawText(p, 1, y, state == " " ? "►" : state, Qt::black, 1);
            }
        }
        horizontal(p, split, 0, right);
        p.fillRect(0, split * cellHeight_, cellWidth_, cellHeight_, theme::background);
        p.fillRect(right * cellWidth_, split * cellHeight_, cellWidth_, cellHeight_, theme::background);
        drawText(p, 0, split, "├", cyan, 1);
        drawText(p, right, split, "┤", cyan, 1);
        drawFiles(p, split + 1, end, false);
    } else drawFiles(p, 2, end, true);
    if (showStatistics()) drawStats(p);
    const int total = session_.view == View::Tree ? int(session_.tree().size()) : int(session_.files().size());
    const int current = total ? (session_.view == View::Tree ? session_.treeIndex() : session_.fileIndex) + 1 : 0;
    const QString ordinal = QString(" %1/%2 ").arg(current).arg(total);
    p.fillRect((right - ordinal.size()) * cellWidth_, end * cellHeight_, ordinal.size() * cellWidth_, cellHeight_, theme::background);
    drawText(p, right - ordinal.size(), end, ordinal, cyan, ordinal.size());
    if (!spellText().isEmpty()) drawText(p, 1, end, "<" + spellText() + ">", yellow, right - ordinal.size() - 2);

    if (isSplit() || session_.view==View::Global) {
        const QString label = "<" + (session_.view == View::Tree ? QString("DIR") : viewName(session_.view)) + ": " + session_.filespec.text() + ">";
        const int count = std::min(int(label.size()), right - 3);
        p.fillRect(cellWidth_, cellHeight_, count * cellWidth_, cellHeight_, theme::background);
        drawText(p, 1, 1, label, paintingActive_ ? white : cyan, count);
    }
}

void TreeWindow::enterDirectory()
{
    if (!session_.currentDirectory().loaded) load(false);
    else {
        session_.enter(View::Directory);
        if (session_.view == View::Tree) {
            if (session_.directory == session_.root && session_.currentDirectory().files.isEmpty() &&
                session_.currentDirectory().children.isEmpty()) navigateParent(true);
            else status_ = "No files";
        }
    }
}

void TreeWindow::swapPaneState()
{
    std::swap(session_, *otherSession_);
    globalReturn_.swap(otherGlobalReturn_);std::swap(globalScroll_,otherGlobalScroll_);
    std::swap(globalRevision_,otherGlobalRevision_);globalPublishedTags_.swap(otherGlobalPublishedTags_);
    rootHistory_.swap(otherRootHistory_);
    rootScroll_.swap(otherRootScroll_);
    std::swap(availableBytes_, otherAvailableBytes_);
    std::swap(treeTop_, otherTreeTop_);
    std::swap(fileTop_, otherFileTop_);
    const std::array<qint64, 6> current{totalBytes_, taggedBytes_, matchingBytes_, totalFiles_, taggedFiles_, matchingFiles_};
    totalBytes_ = otherStats_[0]; taggedBytes_ = otherStats_[1]; matchingBytes_ = otherStats_[2];
    totalFiles_ = int(otherStats_[3]); taggedFiles_ = int(otherStats_[4]); matchingFiles_ = int(otherStats_[5]);
    otherStats_ = current;
}

void TreeWindow::toggleSplit()
{
    stopAutoview();
    if (isSplit()) {
        otherGlobalReturn_.reset();
        otherSession_.reset(); otherRootHistory_.clear(); otherRootScroll_.clear(); pendingMergedTags_.clear(); activeSide_ = 0;
    } else {
        otherSession_ = session_; otherGlobalReturn_=globalReturn_;otherGlobalScroll_=globalScroll_;
        otherGlobalRevision_=globalRevision_;otherGlobalPublishedTags_=globalPublishedTags_;activeSide_ = 0;
        otherRootHistory_ = rootHistory_; otherRootScroll_ = rootScroll_; otherAvailableBytes_ = availableBytes_;
        otherTreeTop_ = treeTop_; otherFileTop_ = fileTop_;
        otherStats_ = {totalBytes_, taggedBytes_, matchingBytes_, totalFiles_, taggedFiles_, matchingFiles_};
    }
    refresh();
}

void TreeWindow::switchPane()
{
    if (!isSplit()) return;
    stopAutoview();
    if (sameSort_) otherSession_->sort = session_.sort;
    otherSession_->tags.unite(pendingMergedTags_);
    pendingMergedTags_.clear();
    if(globalReturn_)publishGlobal();else loggedRoots_.insert(session_.root, session_);
    if(otherGlobalReturn_ && !globalReturn_)otherSession_->syncGlobalFrom(session_);
    const auto logged = loggedRoots_.constFind(otherSession_->root);
    if (!otherGlobalReturn_ && logged != loggedRoots_.cend()) otherSession_->syncLoggingFrom(logged.value());
    otherSession_->rebuild();
    swapPaneState();
    activeSide_ = 1 - activeSide_;
    activeSpell_ = false;
    status_.clear();
    refresh();
}

void TreeWindow::mergeTags()
{
    if (!isSplit()) { status_ = "Merge tags requires F8 Split"; return; }
    if(globalReturn_){publishGlobal();status_="Global tags are shared with logged locations";return;}
    if (otherSession_->root != session_.root) {
        auto found = otherRootHistory_.find(session_.root);
        if (found == otherRootHistory_.end()) {
            Session saved = session_; saved.tags.clear();
            saved.returnToTree();
            found = otherRootHistory_.insert(session_.root, saved);
        }
        int added = 0;
        for (const auto &file : session_.files()) if (session_.tags.contains(file.path) && !found->tags.contains(file.path)) {
            found->tags.insert(file.path); ++added;
        }
        status_ = QString("%1 tags copied to that location in the other panel").arg(added);
        return;
    }
    int added = 0;
    for (const auto &file : session_.files()) if (session_.tags.contains(file.path) && !otherSession_->tags.contains(file.path)) {
        if (!pendingMergedTags_.contains(file.path)) { pendingMergedTags_.insert(file.path); ++added; }
    }
    status_ = QString("%1 tags copied to the other panel; Tab updates its view").arg(added);
}

void TreeWindow::navigateParent(bool keepSelection)
{
    endGlobal();
    session_.returnToTree();
    if (session_.directory != session_.root) { session_.parent(); return; }
    const QString path = QFileInfo(session_.root).absolutePath();
    if (path == session_.root) return;
    pendingParent_ = path;
    pendingParentSelection_ = keepSelection;
    load(false, true, path);
}

bool TreeWindow::selectRoot(const QString &input)
{
    if (busy()) return false;
    QString path = input;
    if (path == "~") path = QDir::homePath();
    else if (path.startsWith("~/")) path = QDir::homePath() + path.mid(1);
    if (!QDir::isAbsolutePath(path)) { status_ = "Enter an absolute path or select a mount point"; return false; }
    const QFileInfo info(path);
    if (!info.isDir() || !info.isReadable()) { status_ = "Directory unavailable or read permission denied"; return false; }
    path = info.canonicalFilePath();
    if (path.isEmpty()) { status_ = "Cannot resolve the location"; return false; }
    endGlobal();
    if (path == session_.root) { mountPrompt_ = false; updateAutoview(); update(); return true; }
    stopAutoview();
    rootHistory_.insert(session_.root, session_);
    rootScroll_.insert(session_.root, {treeTop_, fileTop_});
    loggedRoots_.insert(session_.root, session_);
    const auto paneSort = session_.sort;
    const auto previous = rootHistory_.constFind(path);
    session_ = previous == rootHistory_.cend() ? Session(path) : previous.value();
    session_.sort = paneSort;
    session_.rebuild();
    const auto loaded = loggedRoots_.constFind(path);
    if (loaded != loggedRoots_.cend()) session_.syncLoggingFrom(loaded.value());
    else if (otherSession_ && otherSession_->root == path) session_.syncLoggingFrom(*otherSession_);
    const auto scroll = rootScroll_.value(path);
    treeTop_ = scroll.first; fileTop_ = scroll.second;
    availableBytes_ = QStorageInfo(path).bytesAvailable();
    activeSpell_ = false; mountPrompt_ = false; status_.clear();
    refresh();
    if (!session_.currentDirectory().loaded) load(false, true);
    return true;
}

void TreeWindow::beginMountPrompt()
{
    availPrompt_=false;
    mounts_ = mountedLocations(&status_);
    const auto shortcut = [this](const QString &path, const QString &name) {
        for (const auto &mount : mounts_) if (mount.path == path) return;
        mounts_.append({path, "directory", name, false});
    };
    shortcut(QDir::homePath(), "Home directory");
    shortcut(initialRoot_, "Initial directory");
    shortcut(session_.root, "Current location");
    for (auto it = rootHistory_.cbegin(); it != rootHistory_.cend(); ++it) shortcut(it.key(), "Visited location");
    mountIndex_ = 0;
    for (int i = 0; i < mounts_.size(); ++i) if (mounts_[i].path == session_.root) { mountIndex_ = i; break; }
    mountInput_.clear(); mountPrompt_ = true; activeSpell_ = false;
    update();
}

void TreeWindow::drawMountPrompt(QPainter &p)
{
    drawText(p, 1, 0, availPrompt_?"AVAILABLE — Mount points / locations":"LOG — Mount points / locations", cyan, columns_ - 2);
    drawCommands(p, 1, 1, availPrompt_?"[Arrows] Select   [Enter/Esc] Return   [F5] Refresh":"[Arrows] + [Enter]: open   [F5]: refresh   [Esc]: cancel", columns_ - 2);
    const int count = rows_ - 8;
    const int start = std::max(0, mountIndex_ - count + 1);
    for (int row = 0, i = start; row < count && i < mounts_.size(); ++row, ++i) {
        const auto &mount = mounts_[i];
        const bool selected = i == mountIndex_;
        if (selected) p.fillRect(cellWidth_, (row + 3) * cellHeight_, (columns_ - 2) * cellWidth_, cellHeight_, white);
        const QString text = QString(selected ? "> " : "  ") + mount.path + "  [" + mount.type + (mount.readOnly ? ", RO" : "") + "]";
        drawText(p, 1, row + 3, text, selected ? Qt::black : yellow, columns_ - 2);
    }
    if (!mounts_.isEmpty()) drawText(p, 1, rows_ - 5, "Source: " + mounts_[mountIndex_].source, cyan, columns_ - 2);
    if(availPrompt_){
        drawText(p,1,rows_-4,"Capacity: "+(availCapacity_<0?QString("N/A"):number(availCapacity_)+" bytes"),cyan,columns_-2);
        drawText(p,1,rows_-3,"Available: "+(availBytes_<0?QString("N/A"):number(availBytes_)+" bytes"),yellow,columns_-2);
    }else drawInput(p,1,rows_-3,"Path / prefix: ",mountInput_,int(mountInput_.size()),yellow,columns_-2);
    drawText(p, 1, rows_ - 2, status_.isEmpty() ? (availPrompt_?"Read-only statistics; the active folder stays unchanged.":"Mounted locations; Enter logs only the first level.") : status_, cyan, columns_ - 2);
}

void TreeWindow::mountPromptKey(QKeyEvent *event)
{
    const int key = event->key();
    if (key == Qt::Key_Escape || (availPrompt_ && (key==Qt::Key_Return || key==Qt::Key_Enter))) {mountPrompt_ = false;availPrompt_=false;}
    else if (key == Qt::Key_F5) {
        const bool available=availPrompt_;const QString selected=mounts_.isEmpty()?QString{}:mounts_[mountIndex_].path;
        beginMountPrompt();availPrompt_=available;
        for(int i=0;i<mounts_.size();++i)if(mounts_[i].path==selected){mountIndex_=i;break;}
        if(available)refreshAvailable();
        update();return;
    }
    else if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
        const int delta = key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : key == Qt::Key_PageUp ? -(rows_ - 8) : rows_ - 8;
        mountIndex_ = std::clamp(mountIndex_ + delta, 0, std::max(0, int(mounts_.size()) - 1)); mountInput_.clear();
    } else if (key == Qt::Key_Home || key == Qt::Key_End) {
        mountIndex_ = key == Qt::Key_Home ? 0 : std::max(0, int(mounts_.size()) - 1); mountInput_.clear();
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (mountInput_.startsWith('/') || mountInput_.startsWith('~')) selectRoot(mountInput_);
        else if (!mounts_.isEmpty()) {
            const auto selected = mounts_[mountIndex_];
            if (!mountInput_.isEmpty() && !selected.path.startsWith(mountInput_, Qt::CaseInsensitive) &&
                !QFileInfo(selected.path).fileName().startsWith(mountInput_, Qt::CaseInsensitive)) {
                status_ = "No location matches that prefix"; update(); return;
            }
            bool present = selected.type == "directory";
            if (!present) for (const auto &mount : mountedLocations())
                if (mount.path == selected.path && mount.source == selected.source) { present = true; break; }
            if (present) selectRoot(selected.path);
            else status_ = "Mount changed or disappeared; F5 refreshes the list";
        }
    } else if(!availPrompt_) {
        if (key == Qt::Key_Backspace) {
            if (event->modifiers().testFlag(Qt::ControlModifier)) mountInput_.clear(); else mountInput_.chop(1);
        } else if (key == Qt::Key_V && event->modifiers().testFlag(Qt::ControlModifier)) {
            mountInput_ += QGuiApplication::clipboard()->text().section('\n', 0, 0).remove('\r');
        } else if (!event->modifiers().testFlag(Qt::ControlModifier) && !event->modifiers().testFlag(Qt::AltModifier)) mountInput_ += event->text();
        mountInput_.truncate(4096);
        if (!mountInput_.isEmpty()) for (int i = 0; i < mounts_.size(); ++i) {
            if (mounts_[i].path.startsWith(mountInput_, Qt::CaseInsensitive) || QFileInfo(mounts_[i].path).fileName().startsWith(mountInput_, Qt::CaseInsensitive)) {
                mountIndex_ = i; break;
            }
        }
    }
    if(availPrompt_)refreshAvailable();
    updateAutoview(); update();
}

bool TreeWindow::focusNextPrevChild(bool)
{
    // QWidget no debe consumir Tab como navegación de foco antes de los comandos.
    return false;
}

bool TreeWindow::activateMousePane(int column)
{
    if (!isSplit()) return true;
    const int side = column < paneWidth(0) ? 0 : 1;
    if (side != activeSide_) switchPane();
    return true;
}

void TreeWindow::notImplemented() { status_ = "This command is not implemented yet. F1 shows available commands."; }

void TreeWindow::beginDeletePrompt(bool emptyBranch)
{
    deleteBranchDirectory_=emptyBranch;
    deleteBatch_=deleteBatchError_=false;
    deleteDirectory_=session_.view==View::Tree;
    if(deleteDirectory_){
        if(session_.directory==session_.root){
            if(session_.root=="/"){status_="Cannot delete the filesystem root";return;}
            pendingRootDelete_=true;navigateParent(true);return;
        }
        deletePath_=session_.directory;
    }else{
        if(!session_.currentFile()){status_="No file selected";return;}
        deletePath_=session_.currentFile()->path;
    }
    deletePrompt_=true;
}
void TreeWindow::startDelete()
{
    if(deleteBatch_){deleteBatchChoice_=true;refresh();return;}
    busy_=true;cancel_=std::make_shared<std::atomic_bool>(false);status_=deleteDirectory_?"Deleting empty directory…":"Deleting file…";
    const auto root=session_.root,path=deletePath_;const auto cancel=cancel_;const bool directory=deleteDirectory_;
    const bool branch=deleteBranchDirectory_;
    deleteWatcher_.setFuture(QtConcurrent::run([root,path,cancel,directory,branch]{return branch?deleteEmptyBranch(root,path,cancel):directory?deleteEmptyDirectory(root,path,cancel):deleteFile(root,path,cancel);}));
    update();
}

void TreeWindow::beginDeleteTagged()
{
    deleteTargets_.clear();
    for(const auto &file:session_.files())if(session_.tags.contains(file.path))deleteTargets_.append(file.path);
    if(deleteTargets_.isEmpty()){status_="No tagged files in the current list";return;}
    deleteDirectory_=false;deleteBatch_=true;deleteBatchError_=false;
    deleteBatchChoice_=deleteEachPrompt_=deleteConfirmEach_=deleteHadError_=deleteBranchDirectory_=false;
    deleteBranch_=session_.view==View::Branch?session_.directory:QString{};deleteErrorMessage_.clear();
    deleteIndex_=deleteCount_=deleteSkipped_=0;deleteReadError_.clear();deletePath_=deleteTargets_.first();deletePrompt_=true;
}
void TreeWindow::startDeleteBatchItem(bool confirmed)
{
    deleteBatchError_=false;deletePath_=deleteTargets_[deleteIndex_];
    if(deleteConfirmEach_ && !confirmed){deleteEachPrompt_=true;refresh();return;}
    deleteEachPrompt_=false;busy_=true;
    const auto root=session_.root,path=deletePath_;const auto cancel=cancel_;
    deleteWatcher_.setFuture(QtConcurrent::run([root,path,cancel]{return deleteFile(root,path,cancel);}));
    refresh();
}
void TreeWindow::advanceDeleteBatch()
{
    ++deleteIndex_;
    if(deleteIndex_<deleteTargets_.size())startDeleteBatchItem();else finishDeleteBatch(false);
}
void TreeWindow::finishDeleteBatch(bool cancelled)
{
    busy_=false;deletePrompt_=deleteBatch_=deleteBatchError_=false;
    deleteBatchChoice_=deleteEachPrompt_=false;
    status_=QString(cancelled?"Deletion cancelled: %1/%2 files deleted; %3 skipped":"Deletion completed: %1/%2 files deleted; %3 skipped")
        .arg(deleteCount_).arg(deleteTargets_.size()).arg(deleteSkipped_);
    if(!deleteReadError_.isEmpty())status_+="; read error: "+deleteReadError_;
    if(deleteHadError_ && !deleteErrorMessage_.isEmpty())status_+="; delete error: "+deleteErrorMessage_;
    if(!cancelled && !deleteBranch_.isEmpty() && !deleteHadError_ && deleteReadError_.isEmpty() &&
       deleteSkipped_==0 && deleteCount_==deleteTargets_.size()){
        pendingBranchCheck_=deleteBranch_;deleteSummary_=status_;load(true,true,deleteBranch_);return;
    }
    availableBytes_=QStorageInfo(session_.root).bytesAvailable();refresh();
}

void TreeWindow::beginMakePrompt()
{
    makeBase_=session_.directory;makeInput_.clear();makeCursor_=0;makeHistoryIndex_=-1;makePrompt_=true;
}
void TreeWindow::startMake()
{
    busy_=true;cancel_=std::make_shared<std::atomic_bool>(false);status_="Creating directories… Esc cancels";
    const auto base=makeBase_,name=makeInput_;const auto cancel=cancel_;
    makeWatcher_.setFuture(QtConcurrent::run([base,name,cancel]{return makeDirectory(base,name,cancel);}));
    update();
}
void TreeWindow::makePromptKey(QKeyEvent *event)
{
    const int key=event->key();const bool ctrl=event->modifiers().testFlag(Qt::ControlModifier),alt=event->modifiers().testFlag(Qt::AltModifier);
    if(key==Qt::Key_Escape)makePrompt_=false;
    else if(key==Qt::Key_Return || key==Qt::Key_Enter){startMake();return;}
    else if(key==Qt::Key_F4)makeJump_=!makeJump_;
    else if(key==Qt::Key_F12)makeJump_=false;
    else if(key==Qt::Key_F1){helpDocument_.open(HelpTopic::Navigation);help_=true;}
    else if(key==Qt::Key_F3 || key==Qt::Key_Up || key==Qt::Key_Down){
        if(!makeHistory_.isEmpty()){
            makeHistoryIndex_=key==Qt::Key_F3?0:std::clamp(makeHistoryIndex_+(key==Qt::Key_Down?-1:1),0,int(makeHistory_.size())-1);
            makeInput_=makeHistory_[makeHistoryIndex_];makeCursor_=int(makeInput_.size());
        }
    }else if(key==Qt::Key_Left)makeCursor_=std::max(0,makeCursor_-1);
    else if(key==Qt::Key_Right)makeCursor_=std::min(int(makeInput_.size()),makeCursor_+1);
    else if(key==Qt::Key_Home)makeCursor_=0;
    else if(key==Qt::Key_End)makeCursor_=int(makeInput_.size());
    else if(key==Qt::Key_Backspace && ctrl){makeInput_.clear();makeCursor_=0;}
    else if(key==Qt::Key_Backspace && makeCursor_>0)makeInput_.remove(--makeCursor_,1);
    else if(key==Qt::Key_Delete)makeInput_.remove(makeCursor_,1);
    else{
        QString text;
        if((ctrl && key==Qt::Key_V) || (key==Qt::Key_Insert && event->modifiers().testFlag(Qt::ShiftModifier)))text=QGuiApplication::clipboard()->text();
        else if(!ctrl && !alt && !event->text().isEmpty() && event->text().at(0).isPrint())text=event->text();
        text.remove('\n');text.remove('\r');text=text.left(4096-makeInput_.size());
        makeInput_.insert(makeCursor_,text);makeCursor_+=int(text.size());
    }
    refresh();
}

void TreeWindow::beginExecute()
{
    const QString directory=session_.view!=View::Tree && session_.currentFile()?
        QFileInfo(session_.currentFile()->path).absolutePath():session_.directory;
    if(!QFileInfo(directory).isDir()){status_="The Execute directory no longer exists";return;}
    QString shell=qEnvironmentVariable("SHELL");
    if(!QFileInfo(shell).isExecutable())shell=QStandardPaths::findExecutable("bash");
    if(shell.isEmpty()){status_="No shell found for Execute";return;}
    execution_=new ExecuteWindow(directory,shell,{"-i"},this);execution_->setGeometry(rect());
    updateAutoview();
    connect(execution_,&ExecuteWindow::closed,this,[this,directory]{
        if(execution_){execution_->hide();execution_->deleteLater();execution_=nullptr;}
        setFocus();load(false,true,directory);refresh();
        if(preview_)preview_->reload();
    });
    execution_->show();execution_->raise();execution_->terminal()->setFocus();
}

void TreeWindow::beginCompare()
{
    compareFirst_=session_.currentFile()->path;
    const QFileInfo first(compareFirst_);
    const QString opposite=otherSession_ ? (otherSession_->view!=View::Tree && otherSession_->currentFile() ?
        QFileInfo(otherSession_->currentFile()->path).absolutePath() : otherSession_->directory) : first.absolutePath();
    compareDirectory_=opposite;
    const QString same=QDir(opposite).filePath(first.fileName());
    const QString backup=first.absolutePath()+'/'+first.completeBaseName()+".bak";
    compareCandidates_.clear();
    compareCandidates_ << (isSplit() ? (QFileInfo(same).isFile()?same:QString{}) : backup);
    if (otherSession_ && otherSession_->view!=View::Tree && otherSession_->currentFile()) compareCandidates_ << otherSession_->currentFile()->path;
    if (isSplit()) compareCandidates_ << backup;
    compareCandidates_ << compareFirst_;
    auto tagged=session_.tags;tagged.remove(compareFirst_);
    if(tagged.size()==1)compareCandidates_ << *tagged.cbegin();
    compareCandidate_=0;compareInput_=compareCandidates_.first();compareCursor_=int(compareInput_.size());
    compareReplace_=true;comparePrompt_=true;
    if(QDir::isAbsolutePath(compareInput_))compareDirectory_=QFileInfo(compareInput_).absolutePath();
}

void TreeWindow::openComparison(const QString &first, const QString &second)
{
    if(!QFileInfo(first).isFile() || !QFileInfo(second).isFile()){
        status_="A comparison file does not exist or is not a regular file";return;
    }
    comparison_=new CompareWindow(first,second,this);comparison_->setGeometry(rect());
    connect(comparison_,&CompareWindow::closed,this,[this]{
        if(comparison_){comparison_->hide();comparison_->deleteLater();comparison_=nullptr;}
        setFocus();refresh();
    });
    comparison_->show();comparison_->raise();comparison_->setFocus();
}

void TreeWindow::compareTagged()
{
    const auto *current=session_.currentFile();
    if(!current){status_="No files to compare";return;}
    QStringList tagged;
    for(const auto &file:session_.files())if(session_.tags.contains(file.path))tagged.append(file.path);
    if(tagged.size()==2){
        if(tagged.last()==current->path)tagged.swapItemsAt(0,1);
        openComparison(tagged[0],tagged[1]);
    }else if(tagged.size()==1 && tagged.first()!=current->path){
        openComparison(current->path,tagged.first());
    }else status_="Ctrl+J: tag two files, or one other than the current file";
}

void TreeWindow::comparePromptKey(QKeyEvent *event)
{
    const int key=event->key();const auto modifiers=event->modifiers();
    if(key==Qt::Key_Escape){comparePrompt_=false;refresh();return;}
    if(key==Qt::Key_Tab || key==Qt::Key_Backtab){
        const int direction=key==Qt::Key_Backtab || modifiers.testFlag(Qt::ShiftModifier)?-1:1;
        compareCandidate_=(compareCandidate_+direction+compareCandidates_.size())%compareCandidates_.size();
        compareInput_=compareCandidates_[compareCandidate_];compareCursor_=int(compareInput_.size());compareReplace_=true;
        if(QDir::isAbsolutePath(compareInput_))compareDirectory_=QFileInfo(compareInput_).absolutePath();
    } else if(key==Qt::Key_Return || key==Qt::Key_Enter){
        QString path=compareInput_;
        if(path.isEmpty()){status_="Select or enter the second file";update();return;}
        if(path.startsWith("~/"))path=QDir::homePath()+path.mid(1);
        if(!QDir::isAbsolutePath(path))path=QDir(compareDirectory_).filePath(path);
        if(QFileInfo(path).isDir())path=QDir(path).filePath(QFileInfo(compareFirst_).fileName());
        if(!QFileInfo(path).isFile()){status_="The second file does not exist or is not a regular file";update();return;}
        openComparison(compareFirst_,path);
        if(comparison_)comparePrompt_=false;
    } else if(key==Qt::Key_A && modifiers.testFlag(Qt::ControlModifier))compareReplace_=true;
    else if(key==Qt::Key_Left){compareCursor_=std::max(0,compareCursor_-1);compareReplace_=false;}
    else if(key==Qt::Key_Right){compareCursor_=std::min(int(compareInput_.size()),compareCursor_+1);compareReplace_=false;}
    else if(key==Qt::Key_Home){compareCursor_=0;compareReplace_=false;}
    else if(key==Qt::Key_End){compareCursor_=int(compareInput_.size());compareReplace_=false;}
    else if(key==Qt::Key_Backspace || key==Qt::Key_Delete){
        if(compareReplace_){compareInput_.clear();compareCursor_=0;compareReplace_=false;}
        else if(key==Qt::Key_Backspace && compareCursor_>0)compareInput_.remove(--compareCursor_,1);
        else if(key==Qt::Key_Delete)compareInput_.remove(compareCursor_,1);
    } else {
        QString input;
        if(key==Qt::Key_V && modifiers.testFlag(Qt::ControlModifier))input=QGuiApplication::clipboard()->text().section('\n',0,0).remove('\r');
        else if(!modifiers.testFlag(Qt::ControlModifier) && !modifiers.testFlag(Qt::AltModifier))input=event->text();
        if(!input.isEmpty()){
            if(compareReplace_){compareInput_.clear();compareCursor_=0;compareReplace_=false;}
            input=input.left(std::max(0,4096-int(compareInput_.size())));
            compareInput_.insert(compareCursor_,input);compareCursor_+=int(input.size());
        }
    }
    refresh();
}

int TreeWindow::autoviewWidth() const
{
    return std::clamp(layoutColumns() * autoviewPercent_ / 100, 16, layoutColumns() - 20);
}

void TreeWindow::startAutoview()
{
    if (preview_) return;
    infoVisible_=false;
    if (!session_.currentDirectory().loaded && session_.view == View::Tree) {
        pendingAutoview_ = true; load(false, true); return;
    }
    autoviewReturn_ = session_.view;
    if (session_.view == View::Tree) session_.enter(View::Directory);
    if (!session_.currentFile()) { status_ = "No files for Autoview"; return; }
    preview_ = new FileViewer(session_.currentFile()->path, this);
    preview_->setObjectName("autoview");
    preview_->setAutoview(true);
    preview_->setFocusPolicy(Qt::NoFocus);
    connect(preview_, &FileViewer::closed, this, [this] { stopAutoview(); refresh(); });
    connect(preview_, &FileViewer::editRequested, this, [this](const QString &path) { emit openRequested(path, true); });
    updateAutoview(); preview_->show(); setFocus();
}

void TreeWindow::stopAutoview()
{
    if (!preview_) return;
    preview_->hide(); preview_->deleteLater(); preview_ = nullptr;
    if (autoviewReturn_ == View::Tree) session_.returnToTree();
    setFocus();
}

void TreeWindow::updateAutoview()
{
    if (!preview_) return;
    if (session_.view == View::Tree || !session_.currentFile()) { stopAutoview(); return; }
    preview_->setPath(session_.currentFile()->path);
    const int offset = isSplit() && activeSide_ == 1 ? paneWidth(0) : 0;
    const int list = autoviewWidth();
    preview_->setGeometry((offset + list) * cellWidth_, 2 * cellHeight_,
                          (layoutColumns() - list - 1) * cellWidth_, (bottom() - 2) * cellHeight_);
    preview_->setVisible(!help_ && !mountPrompt_ && !linkPrompt_ && !execution_ && !extendedStats_);
}

void TreeWindow::sortPromptKey(QKeyEvent *event)
{
    const int key = event->key();
    if (key == Qt::Key_Escape) { sortPrompt_ = false; refresh(); return; }
    bool apply = false;
    switch (key) {
    case Qt::Key_N: sortDraft_.key = SortKey::Name; apply = true; break;
    case Qt::Key_E: sortDraft_.key = SortKey::Extension; apply = true; break;
    case Qt::Key_D: sortDraft_.key = SortKey::Date; apply = true; break;
    case Qt::Key_T: sortDraft_.key = SortKey::Time; apply = true; break;
    case Qt::Key_L: sortDraft_.key = SortKey::Length; apply = true; break;
    case Qt::Key_A: sortDraft_.key = SortKey::Alpha; apply = true; break;
    case Qt::Key_M: sortDraft_.key = SortKey::Number; apply = true; break;
    case Qt::Key_S: sortDraft_.key = SortKey::Size; apply = true; break;
    case Qt::Key_U: sortDraft_.key = SortKey::Unsorted; apply = true; break;
    case Qt::Key_V: sortDraft_.key = SortKey::Value; apply = true; break;
    case Qt::Key_Return: case Qt::Key_Enter: apply = true; break;
    case Qt::Key_O: sortDraft_.descending = !sortDraft_.descending; break;
    case Qt::Key_Up: case Qt::Key_Greater: sortDraft_.descending = false; break;
    case Qt::Key_Down: case Qt::Key_Less: sortDraft_.descending = true; break;
    case Qt::Key_P: sortDraft_.pathFirst = !sortDraft_.pathFirst; break;
    case Qt::Key_F8: sortDraftSame_ = !sortDraftSame_; break;
    case Qt::Key_F12: sortDraft_ = {}; sortDraftSame_ = true; break;
    case Qt::Key_C: status_ = "OEM/ANSI charset is not implemented yet; Unicode comparison ignores case"; break;
    case Qt::Key_X: status_ = "on eXit is not implemented yet; sort order is retained when leaving files"; break;
    default: break;
    }
    if (apply) {
        session_.sort = sortDraft_; sameSort_ = sortDraftSame_;
        session_.rebuild(); sortPrompt_ = false;
    }
    refresh();
}

void TreeWindow::keyPressEvent(QKeyEvent *event)
{
    if(execution_){event->accept();return;}
    const int key = event->key();
    modifiers_ = event->modifiers();
    if (key == Qt::Key_Alt || key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_AltGr) {
        if (key != Qt::Key_Shift) activeSpell_ = false;
        update(); return;
    }
    if (key == Qt::Key_Return && modifiers_.testFlag(Qt::AltModifier)) {
        menuMode_=0;
        if (isFullScreen()) showNormal(); else showFullScreen();
        refresh(); return;
    }
    if (key == Qt::Key_F7 && modifiers_.testFlag(Qt::AltModifier)) {
        menuMode_=0;
        if (isMaximized()) showNormal(); else showMaximized();
        refresh(); return;
    }
    if (preview_ && !extendedStats_ && !infoVisible_ && !stampPrompt_ && !permissionsPrompt_ && !directoryComparePrompt_ && !duplicatePrompt_ && !renamePrompt_ && copyStep_==CopyStep::None && !busy() && !help_ && !mountPrompt_ && !sortPrompt_ && !splitPrompt_ && !comparePrompt_ && !linkPrompt_ && !makePrompt_ && !graftPrompt_ && !prunePrompt_ && !deletePrompt_ &&
        !searchPrompt_ && !searchError_ && !filespecPrompt_ && !invertPrompt_ && !quitPrompt_ && !promptedSpell_) {
        if (preview_->prompting() || (modifiers_.testFlag(Qt::ShiftModifier) && !modifiers_.testFlag(Qt::AltModifier))) {
            QKeyEvent forwarded(event->type(), key, modifiers_ & ~Qt::ShiftModifier, event->text());
            preview_->handleKey(&forwarded); return;
        }
        if (modifiers_.testFlag(Qt::AltModifier) && (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Home)) {
            autoviewPercent_ = key == Qt::Key_Home ? 40 : std::clamp(autoviewPercent_ + (key == Qt::Key_Right ? 5 : -5), 20, 75);
            refresh(); return;
        }
        if (modifiers_ == Qt::NoModifier && (key == Qt::Key_F7 || key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter)) {
            stopAutoview(); refresh(); return;
        }
    }
    if (searchError_) { searchErrorKey(key); return; }
    if (busy()) {
        if (key == Qt::Key_Escape) { cancel_->store(true); status_ = "Cancelling…"; }
        update(); return;
    }
    if (help_) { help_ = helpDocumentKey(helpDocument_, event, columns_, rows_); updateAutoview(); update(); return; }
    if (quitPrompt_) {
        if (key == Qt::Key_Y || key == Qt::Key_Q) close();
        else if (key == Qt::Key_N || key == Qt::Key_Escape) quitPrompt_ = false;
        update(); return;
    }
    status_.clear();
    if(graftPrompt_){graftKey(event);return;}
    if(prunePrompt_){pruneKey(event);return;}
    if(extendedStats_){extendedStatsKey(event);return;}
    if(stampPrompt_){stampKey(event);return;}
    if(permissionsPrompt_){permissionsKey(event);return;}
    if(renamePrompt_){renameKey(event);return;}
    if(directoryComparePrompt_){directoryCompareKey(event);return;}
    if(duplicatePrompt_){duplicateKey(event);return;}
    if (mountPrompt_) { mountPromptKey(event); return; }
    if (linkPrompt_) { linkKey(event); return; }
    if (makePrompt_) { makePromptKey(event); return; }
    if(copyStep_!=CopyStep::None){copyPromptKey(event);return;}
    if(deletePrompt_){
        if(deleteBatchChoice_){
            if(key==Qt::Key_Y || key==Qt::Key_N || key==Qt::Key_Return || key==Qt::Key_Enter){
                deleteConfirmEach_=key!=Qt::Key_N;deleteBatchChoice_=false;
                cancel_=std::make_shared<std::atomic_bool>(false);startDeleteBatchItem();
            }else if(key==Qt::Key_Escape)finishDeleteBatch(true);
        }else if(deleteEachPrompt_){
            if(key==Qt::Key_Y || key==Qt::Key_Return || key==Qt::Key_Enter)startDeleteBatchItem(true);
            else if(key==Qt::Key_N){deleteEachPrompt_=false;++deleteSkipped_;advanceDeleteBatch();}
            else if(key==Qt::Key_Escape)finishDeleteBatch(true);
        }else if(deleteBatchError_){
            if(key==Qt::Key_R || key==Qt::Key_Return || key==Qt::Key_Enter)startDeleteBatchItem(true);
            else if(key==Qt::Key_S){
                ++deleteSkipped_;advanceDeleteBatch();
            }else if(key==Qt::Key_C || key==Qt::Key_Escape)finishDeleteBatch(true);
        }else if(key==Qt::Key_Return || key==Qt::Key_Enter || key==Qt::Key_Y)startDelete();
        else if(key==Qt::Key_Escape || key==Qt::Key_N){deletePrompt_=deleteBatch_=false;refresh();}
        return;
    }
    if (comparePrompt_) { comparePromptKey(event); return; }
    if (sortPrompt_) { sortPromptKey(event); return; }
    if (splitPrompt_) {
        if (key == Qt::Key_S) activeSide_ = 1 - activeSide_;
        else if (key == Qt::Key_N) splitStats_ = 0;
        else if (key == Qt::Key_C && columns_ >= 102) splitStats_ = 1;
        else if (key == Qt::Key_B && columns_ >= 122) splitStats_ = 2;
        else if (key != Qt::Key_Escape) { status_ = "Option unavailable at this width"; update(); return; }
        splitPrompt_ = false; refresh(); return;
    }
    if (searchPrompt_) { searchPromptKey(event); return; }
    if (filespecPrompt_) { filespecPromptKey(event); return; }
    if (invertPrompt_) {
        if (key == Qt::Key_F) { session_.filespec.inverted = !session_.filespec.inverted; session_.rebuild(); }
        else if (key == Qt::Key_T) {
            if (session_.view == View::Tree) {
                for (const auto &dir : session_.directories()) for (const auto &file : dir.files)
                    if (session_.filespec.matches(file)) {
                        if (session_.tags.contains(file.path)) session_.tags.remove(file.path); else session_.tags.insert(file.path);
                    }
            } else for (const auto &file : session_.files()) {
                if (session_.tags.contains(file.path)) session_.tags.remove(file.path); else session_.tags.insert(file.path);
            }
        } else if (key != Qt::Key_Escape) return;
        invertPrompt_ = false; refresh(); return;
    }
    if(infoVisible_ && (key==Qt::Key_Escape ||
        ((key==Qt::Key_Return || key==Qt::Key_Enter) && modifiers_==Qt::NoModifier) ||
        (key==Qt::Key_I && modifiers_.testFlag(Qt::AltModifier) && !modifiers_.testFlag(Qt::ControlModifier)))){
        infoVisible_=false;menuMode_=0;refresh();return;
    }
    const bool physicalCtrl = modifiers_.testFlag(Qt::ControlModifier);
    const bool physicalAlt = modifiers_.testFlag(Qt::AltModifier);
    const bool shift = modifiers_.testFlag(Qt::ShiftModifier);
    const bool tree = session_.view == View::Tree;
    if(key==Qt::Key_F4 && !physicalCtrl && !physicalAlt && !promptedSpell_){
        menuMode_=(menuMode_+1)%3;refresh();return;
    }
    const int menu=menuMode_;
    menuMode_=0;
    if(menu && key==Qt::Key_Escape && !physicalCtrl && !physicalAlt){refresh();return;}
    const bool enter=key==Qt::Key_Return || key==Qt::Key_Enter;
    const bool menuCtrl=menu==1 && !shift && (key==Qt::Key_T || key==Qt::Key_U || enter ||
        (tree?(key==Qt::Key_B || key==Qt::Key_S || key==Qt::Key_G):(key==Qt::Key_A || key==Qt::Key_C || key==Qt::Key_D || key==Qt::Key_M || key==Qt::Key_N || key==Qt::Key_R || key==Qt::Key_S || key==Qt::Key_J || key==Qt::Key_V || key==Qt::Key_F5)));
    const bool menuAlt=menu==2 && !shift && (key==Qt::Key_F || key==Qt::Key_I || (tree && (key==Qt::Key_A || key==Qt::Key_C || key==Qt::Key_G || key==Qt::Key_P)) || key==Qt::Key_S || key==Qt::Key_F3 || key==Qt::Key_F7 || enter || (!tree && (key==Qt::Key_C || key==Qt::Key_M || key==Qt::Key_V || key==Qt::Key_K)));
    const bool ctrl = physicalCtrl || (!physicalAlt && menuCtrl);
    const bool alt = physicalAlt || (!physicalCtrl && menuAlt);
    if(menuAlt && !physicalCtrl && !physicalAlt && (enter || key==Qt::Key_F7)){
        if(enter){if(isFullScreen())showNormal();else showFullScreen();}
        else {if(isMaximized())showNormal();else showMaximized();}
        refresh();return;
    }
    if (promptedSpell_) {
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape) promptedSpell_ = false;
        else if (key == Qt::Key_Backspace) { spellText().chop(1); session_.spell(spellText()); }
        else if (key == Qt::Key_QuoteDbl || key == Qt::Key_PageDown) session_.spell(spellText(), true);
        else if (!event->text().isEmpty() && !physicalCtrl && !physicalAlt) {
            spellText() += event->text();
            if (!session_.spell(spellText())) status_ = "No match";
        }
        refresh(); return;
    }
    if (!ctrl && !alt && event->text() == "|") {
        promptedSpell_ = true;
        activeSpell_ = false;
        spellText().clear();
        refresh(); return;
    }
    if (!ctrl && !alt && ((shift && !event->text().isEmpty() && event->text()[0].isLetter()) ||
                         (key >= Qt::Key_0 && key <= Qt::Key_9))) {
        if (!activeSpell_) spellText().clear();
        activeSpell_ = true;
        spellText() += event->text();
        if (!session_.spell(spellText())) status_ = "No match";
        refresh(); return;
    }
    if (shift && key == Qt::Key_Backspace && !ctrl && !alt) {
        spellText().chop(1); session_.spell(spellText()); refresh(); return;
    }
    if (shift && key == Qt::Key_Escape) {
        spellText().clear(); activeSpell_ = false; refresh(); return;
    }
    if (!ctrl && !alt && event->text() == "\"") { session_.spell(spellText(), true); refresh(); return; }
    activeSpell_ = false;
    if(!ctrl && !alt && (key==Qt::Key_Question || key==Qt::Key_Slash || event->text()=="?")){beginExtendedStats();return;}
    if (alt && !ctrl && !shift && key == Qt::Key_F) cycleFileDisplay();
    else if (!alt && !ctrl && shift && (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Home)) {
        extensionOffset_ = key == Qt::Key_Home ? 0 : std::clamp(extensionOffset_ + (key == Qt::Key_Left ? 1 : -1), -8, 200);
    }
    else if(tree && alt && !ctrl && !shift && key==Qt::Key_G)beginGraft();
    else if(tree && alt && !ctrl && !shift && key==Qt::Key_P)beginPrune();
    else if(tree && !alt && !shift && key==Qt::Key_G)beginGlobal(ctrl);
    else if(tree && !ctrl && !alt && !shift && key==Qt::Key_A){beginMountPrompt();availPrompt_=true;refreshAvailable();}
    else if(tree && !ctrl && !alt && !shift && key==Qt::Key_H)beginLink();
    else if(!ctrl && !alt && (key==Qt::Key_Less || key==Qt::Key_Greater || key==Qt::Key_Comma || key==Qt::Key_Period || event->text()=="<" || event->text()==">"))cycleLoggedRoot(key==Qt::Key_Less || key==Qt::Key_Comma || event->text()=="<"?-1:1);
    else if(tree && ctrl && !alt && !shift && (key==Qt::Key_F7 || key==Qt::Key_F8 || key==Qt::Key_F9))session_.tagBranch(key==Qt::Key_F7?1:key==Qt::Key_F8?0:2);
    else if(!tree && !alt && key==Qt::Key_F5)openArchive(true,ctrl);
    else if(!tree && key==Qt::Key_V && (ctrl || alt))openViewer(ctrl,alt);
    else if(alt && !ctrl && !shift && !tree && (key==Qt::Key_F4 || key==Qt::Key_K)){stopAutoview();duplicatePrompt_=true;}
    else if (key == Qt::Key_L && !alt && !shift) beginMountPrompt();
    else if (key == Qt::Key_F8 && !alt && !ctrl) {
        if (shift) { if (isSplit()) splitPrompt_ = true; else status_ = "Enable F8 Split first"; }
        else toggleSplit();
    }
    else if (isSplit() && key == Qt::Key_F6 && ctrl && !alt && !shift) mergeTags();
    else if (isSplit() && key == Qt::Key_Tab && !ctrl && !alt && !shift) switchPane();
    else if (tree && !alt && (key == Qt::Key_Tab || key == Qt::Key_Backtab) && (ctrl || shift)) session_.sibling(shift ? -1 : 1);
    else if (key == Qt::Key_F7 && !alt && !ctrl && !shift) startAutoview();
    else if (key == Qt::Key_F1 && !alt && !ctrl) { helpDocument_.open(HelpTopic::Display); help_ = true; }
    else if (key == Qt::Key_Q && !ctrl && !alt) quitPrompt_ = true;
    else if (key == Qt::Key_F && !alt) {
        if (ctrl) {
            if (!filespecHistory_.entries.isEmpty()) {
                const int index = filespecHistory_.entries.first() == session_.filespec.text() && filespecHistory_.entries.size() > 1 ? 1 : 0;
                applyFilespec(filespecHistory_.entries[index]);
            }
        } else {
            filespecPrompt_ = true; filespecInput_.clear(); filespecCursor_ = 0; filespecHistory_.visible = false;
        }
    }
    else if (key == Qt::Key_I && ctrl && !alt) invertPrompt_ = true;
    else if (ctrl && !alt && key == Qt::Key_T) session_.setTag(true, true);
    else if (ctrl && !alt && key == Qt::Key_U) session_.setTag(false, true);
    else if (ctrl && !alt && tree && (key == Qt::Key_B || key == Qt::Key_S))
        session_.enter(key == Qt::Key_B ? View::Branch : View::Showall, true);
    else if (ctrl && !alt && !tree && (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_F4))
        session_.toggleTagsOnly();
    else if (ctrl && !alt && !tree && key == Qt::Key_S) beginSearchPrompt();
    else if (ctrl && !alt && !tree && key == Qt::Key_J) compareTagged();
    else if (ctrl && !alt && !shift && !tree && (key==Qt::Key_D || key==Qt::Key_Delete))beginDeleteTagged();
    else if(!ctrl && !shift && tree && key==Qt::Key_C)beginDirectoryCompare(alt);
    else if(alt && !ctrl && !shift && !tree && key==Qt::Key_C)beginCopy();
    else if(!alt && !ctrl && !shift && !tree && key==Qt::Key_C)beginCopy(true);
    else if(alt && !ctrl && !shift && !tree && key==Qt::Key_M)beginCopy(false,true);
    else if(!alt && !ctrl && !shift && !tree && key==Qt::Key_M)beginCopy(true,true);
    else if(!alt && !shift && key==Qt::Key_R && (!ctrl || !tree))beginRename(ctrl);
    else if(!alt && !shift && !tree && key==Qt::Key_N)beginStamp(ctrl);
    else if(ctrl && !alt && !shift && !tree && key==Qt::Key_A)beginPermissions(true);
    else if(ctrl && !alt && !shift && !tree && key==Qt::Key_C)beginCopy(false,false,true);
    else if(ctrl && !alt && !shift && !tree && key==Qt::Key_M)beginCopy(false,true,true);
    else if (ctrl && !alt && tree && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
        if (!session_.currentDirectory().loaded) load(false); else session_.enter(View::Directory, true);
    }
    else if (key == Qt::Key_F3 && !ctrl && !shift) load(false, !alt);
    else if (key == Qt::Key_S && alt && !ctrl) {
        sortDraft_ = session_.sort; sortDraftSame_ = sameSort_; sortPrompt_ = true;
    }
    else if (key == Qt::Key_X && !ctrl && !alt) beginExecute();
    else if (tree && key==Qt::Key_M && !ctrl && !alt)beginMakePrompt();
    else if ((key==Qt::Key_D || key==Qt::Key_Delete) && !ctrl && !alt && !shift)beginDeletePrompt();
    else if(alt && !ctrl && !shift && key==Qt::Key_I)beginInfo();
    else if(alt && !ctrl && !shift && tree && key==Qt::Key_A)beginPermissions();
    else if(!alt && !ctrl && !shift && !tree && key==Qt::Key_A)beginPermissions();
    else if (ctrl || alt) notImplemented();
    else if (shift && ((key >= Qt::Key_F1 && key <= Qt::Key_F35) ||
                      key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left ||
                      key == Qt::Key_Right || key == Qt::Key_Home || key == Qt::Key_End ||
                      key == Qt::Key_PageUp || key == Qt::Key_PageDown)) notImplemented();
    else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (tree) enterDirectory();
        else if(session_.currentFile() && archiveName(session_.currentFile()->path))openArchive();
        else {
            if(globalReturn_){endGlobal();refresh();return;}
            session_.returnToTree();
            if (session_.directory == session_.root && session_.currentDirectory().files.isEmpty() &&
                session_.currentDirectory().children.isEmpty()) navigateParent(true);
        }
    }
    else if (key == Qt::Key_Escape) { if(globalReturn_)endGlobal();else if (!tree) session_.returnToTree(); else menuMode_ = 0; }
    else if (key == Qt::Key_Up) session_.move(-1);
    else if (key == Qt::Key_Down || key == Qt::Key_Space) session_.move(1);
    else if (key == Qt::Key_Home) session_.first();
    else if (key == Qt::Key_End) session_.last();
    else if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
        const int amount = tree ? divider() - 2 : fileLayout(2, bottom()).capacity();
        session_.move(key == Qt::Key_PageUp ? -amount : amount);
    }
    else if (key == Qt::Key_Left || key == Qt::Key_Backspace) {
        if (key == Qt::Key_Backspace) navigateParent();
        else if (tree) session_.parent(false);
        else if (session_.files().isEmpty()) session_.returnToTree();
        else session_.move(-fileLayout(2, bottom()).rows);
    }
    else if (key == Qt::Key_Right) session_.move(tree ? 1 : fileLayout(2, bottom()).rows);
    else if ((key == Qt::Key_Tab || key == Qt::Key_Backtab) && tree) session_.sibling(shift || key == Qt::Key_Backtab ? -1 : 1);
    else if (tree && (event->text() == "+" || event->text() == "=")) load(false);
    else if (tree && event->text() == "*") load(true);
    else if (tree && (event->text() == "-" || event->text() == "_")) session_.unlog();
    else if (tree && (key == Qt::Key_F5 || key == Qt::Key_F6)) session_.toggleCollapse(key == Qt::Key_F5);
    else if (tree && (key == Qt::Key_B || key == Qt::Key_S)) session_.enter(key == Qt::Key_B ? View::Branch : View::Showall);
    else if (key == Qt::Key_T || key == Qt::Key_U) session_.setTag(key == Qt::Key_T, false);
    else if (!tree && key == Qt::Key_J && session_.currentFile()) beginCompare();
    else if (!tree && key == Qt::Key_V && session_.currentFile()) openViewer();
    else if (!tree && (key == Qt::Key_O || key == Qt::Key_E) && session_.currentFile()) emit openRequested(session_.currentFile()->path, key == Qt::Key_E);
    else if (!event->text().isEmpty() || key >= Qt::Key_F1) notImplemented();
    refresh();
}

void TreeWindow::keyReleaseEvent(QKeyEvent *event)
{
    modifiers_ = event->modifiers();
    if (event->key() == Qt::Key_Control) modifiers_ &= ~Qt::ControlModifier;
    if (event->key() == Qt::Key_Alt) modifiers_ &= ~Qt::AltModifier;
    if (event->key() == Qt::Key_Shift) modifiers_ &= ~Qt::ShiftModifier;
    update();
}

void TreeWindow::focusOutEvent(QFocusEvent *) { modifiers_ = {}; menuMode_ = 0; activeSpell_ = false; update(); }

void TreeWindow::mousePressEvent(QMouseEvent *event)
{
    if(extendedStats_){if(event->button()==Qt::LeftButton){QKeyEvent back(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);extendedStatsKey(&back);}return;}
    if(promptHistory().visible){
        const auto box=historyRect();const auto items=historyItems();
        const QPoint cell(int(event->position().x())/cellWidth_,int(event->position().y())/cellHeight_);
        if(event->button()==Qt::LeftButton && cell.x()>box.left() && cell.x()<box.right() && cell.y()>box.top() && cell.y()<box.bottom()-4){
            const int index=promptHistory().top+cell.y()-box.top()-1;
            if(index<items.size()){promptHistory().index=index;keepHistoryVisible();update();}
        }
        return;
    }
    if (graftPrompt_ || prunePrompt_ || stampPrompt_ || permissionsPrompt_ || directoryComparePrompt_ || duplicatePrompt_ || renamePrompt_ || copyStep_!=CopyStep::None || busy() || help_ || quitPrompt_ || promptedSpell_ || searchPrompt_ || filespecPrompt_ || invertPrompt_ || splitPrompt_ || sortPrompt_ || comparePrompt_ || mountPrompt_ || linkPrompt_ || makePrompt_ || deletePrompt_) return;
    if(infoVisible_ && infoRect().contains(QPoint(int(event->position().x())/cellWidth_,int(event->position().y())/cellHeight_)))return;
    int x = int(event->position().x()) / cellWidth_;
    activateMousePane(x);
    if (isSplit() && activeSide_ == 1) x -= paneWidth(0);
    const int y = int(event->position().y()) / cellHeight_;
    if(event->button()==Qt::LeftButton && showStatistics() && x>=contentRight() && y>=2 && y<bottom()){beginExtendedStats();return;}
    if (x < 1 || x >= contentRight()) return;
    if (session_.view == View::Tree && y >= 2 && y < divider()) {
        const int index = treeTop_ + y - 2;
        if (index < session_.tree().size()) session_.selectDirectory(session_.tree()[index].path);
    } else if (session_.view != View::Tree && y >= 2 && y < bottom()) {
        const int index = fileAtCell(x, y);
        if (index >= 0) {
            session_.fileIndex = index;
            if (event->button() == Qt::RightButton) session_.invertTag();
        }
    }
    refresh();
}

void TreeWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if(extendedStats_){mousePressEvent(event);return;}
    if(promptHistory().visible){
        const auto box=historyRect();const QPoint cell(int(event->position().x())/cellWidth_,int(event->position().y())/cellHeight_);
        if(event->button()==Qt::LeftButton && cell.x()>box.left() && cell.x()<box.right() && cell.y()>box.top() && cell.y()<box.bottom()-4 && promptHistory().top+cell.y()-box.top()-1<historyItems().size()){
            mousePressEvent(event);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);historyKey(&enter);
        }
        return;
    }
    if (graftPrompt_ || prunePrompt_ || stampPrompt_ || permissionsPrompt_ || directoryComparePrompt_ || duplicatePrompt_ || renamePrompt_ || copyStep_!=CopyStep::None || busy() || help_ || quitPrompt_ || promptedSpell_ || searchPrompt_ || filespecPrompt_ || invertPrompt_ || splitPrompt_ || sortPrompt_ || comparePrompt_ || mountPrompt_ || linkPrompt_ || makePrompt_ || deletePrompt_ || event->button() != Qt::LeftButton) return;
    if(infoVisible_ && infoRect().contains(QPoint(int(event->position().x())/cellWidth_,int(event->position().y())/cellHeight_)))return;
    int x = int(event->position().x()) / cellWidth_;
    activateMousePane(x);
    if (isSplit() && activeSide_ == 1) x -= paneWidth(0);
    const int y = int(event->position().y()) / cellHeight_;
    if (x < 1 || x >= contentRight() || y < 2 || y >= bottom()) return;
    if (session_.view != View::Tree && fileAtCell(x, y) < 0) return;
    mousePressEvent(event);
    if (session_.view == View::Tree && y < divider()) enterDirectory();
    else if (session_.view != View::Tree && session_.currentFile()) emit openRequested(session_.currentFile()->path, false);
    refresh();
}

void TreeWindow::wheelEvent(QWheelEvent *event)
{
    if (help_ || pruneHelp_ || graftHelp_ || stampHelp_ || statsHelp_) { helpDocumentScroll(helpDocument_, event->angleDelta().y()/120, columns_, rows_); update(); return; }
    if(extendedStats_){statsScroll_=std::clamp(statsScroll_-event->angleDelta().y()/120*3,0,std::max(0,28-(rows_-6)));update();return;}
    if(promptHistory().visible){promptHistory().index-=event->angleDelta().y()/120*3;keepHistoryVisible();update();return;}
    if((searchPrompt_ || filespecPrompt_) && !event->angleDelta().isNull()){openHistory(event->angleDelta().y()>0);return;}
    if (graftPrompt_ || prunePrompt_ || stampPrompt_ || permissionsPrompt_ || directoryComparePrompt_ || duplicatePrompt_ || renamePrompt_ || copyStep_!=CopyStep::None || busy() || help_ || promptedSpell_ || quitPrompt_ || searchPrompt_ || filespecPrompt_ || invertPrompt_ || splitPrompt_ || sortPrompt_ || comparePrompt_ || mountPrompt_ || linkPrompt_ || makePrompt_ || deletePrompt_) return;
    if(infoVisible_ && infoRect().contains(QPoint(int(event->position().x())/cellWidth_,int(event->position().y())/cellHeight_)))return;
    activateMousePane(int(event->position().x()) / cellWidth_);
    session_.move(-event->angleDelta().y() / 120 * 3);
    refresh();
}

void TreeWindow::beginSearchPrompt()
{
    searchTargets_.clear();
    for (const auto &file : session_.files())
        if (session_.tags.contains(file.path)) searchTargets_.append(file.path);
    if (searchTargets_.isEmpty()) { status_ = "No tagged files"; return; }
    searchPrompt_ = true;
    searchInput_.clear();
    searchCursor_ = 0;
    searchHistory_.visible=false;
}

void TreeWindow::startSearch()
{
    searchOptions_.query = searchInput_;
    status_ = validateSearch(searchOptions_);
    if (!status_.isEmpty()) { update(); return; }
    lastExecutedSearch_ = searchOptions_;
    rememberSearch(searchInput_);
    searchPrompt_ = false;
    searching_ = true;
    searchError_ = false;
    searchIndex_ = 0;
    searchHits_ = searchSpinner_ = 0;
    searchReadBytes_ = 0;
    searchWeights_ = {0};
    for (const auto &path : searchTargets_)
        searchWeights_.append(searchWeights_.last() + std::max(qint64(1), QFileInfo(path).size()));
    searchElapsed_.start();
    errorPolicies_.clear();
    cancel_ = std::make_shared<std::atomic_bool>(false);
    searchNext();
}

void TreeWindow::searchNext()
{
    searchError_ = false;
    if (cancel_->load()) { finishSearch(true); return; }
    if (searchIndex_ >= searchTargets_.size()) { finishSearch(false); return; }
    status_ = QString("SEARCH %1/%2: %3 — Esc cancels").arg(searchIndex_ + 1).arg(searchTargets_.size())
                  .arg(QFileInfo(searchTargets_[searchIndex_]).fileName());
    const QString path = searchTargets_[searchIndex_];
    for (int i = 0; i < session_.files().size(); ++i)
        if (session_.files()[i].path == path) { session_.fileIndex = i; break; }
    searchFileBytes_ = std::make_shared<std::atomic<qint64>>(0);
    const auto bytes = searchFileBytes_;
    searchProgressTimer_.start();
    const auto options = searchOptions_;
    const auto cancel = cancel_;
    searchWatcher_.setFuture(QtConcurrent::run([path, options, cancel, bytes] {
        return searchFile(path, options, cancel, [bytes](qint64 read) { bytes->store(read); });
    }));
    refresh();
}

void TreeWindow::finishSearch(bool cancelled)
{
    searching_ = false;
    searchError_ = false;
    searchProgressTimer_.stop();
    int tagged = 0;
    for (const auto &path : searchTargets_) if (session_.tags.contains(path)) ++tagged;
    status_ = QString("%1: %2/%3 processed; %4 tagged; Hits: %5").arg(cancelled ? "Search cancelled" : "Search completed")
                  .arg(searchIndex_).arg(searchTargets_.size()).arg(tagged).arg(searchHits_);
    refresh();
}

void TreeWindow::drawSearchProgress(QPainter &p)
{
    const int y = bottom() + 1;
    const QString hits = QString("Hits: %1").arg(searchHits_);
    const QString mode = searchModeName(searchOptions_.mode);
    drawText(p, 0, y, QString("Searching for (%1): %2").arg(mode, searchOptions_.query), cyan,
             columns_ - hits.size() - 2);
    drawText(p, columns_ - hits.size() - 1, y, hits, cyan, hits.size());
    QString path = searchTargets_[searchIndex_];
    const int pathWidth = columns_ - 11;
    if (path.size() > pathWidth) path = "..." + path.right(pathWidth - 3);
    drawText(p, 2, y + 1, "in file: " + path, cyan, columns_ - 2);
    const qint64 current = searchFileBytes_->load();
    const qint64 total = searchWeights_.last();
    const qint64 done = searchWeights_[searchIndex_] +
        std::min(current, searchWeights_[searchIndex_ + 1] - searchWeights_[searchIndex_]);
    const int percent = int(100.0 * double(done) / double(total));
    const qint64 elapsed = searchElapsed_.elapsed();
    const auto time = [](qint64 seconds) {
        return QString("%1:%2").arg(seconds / 60, 2, 10, QChar('0'))
            .arg(seconds % 60, 2, 10, QChar('0'));
    };
    const QString left = done > 0 ? time(qint64(double(elapsed) * double(total - done) / double(done) / 1000)) : "--:--";
    const double speed = elapsed > 0 ? double(searchReadBytes_ + current) / double(elapsed) / 1000.0 : 0;
    const QString metrics = QString(" %1%  Time: %2  Left: %3  (%4 MB/s)")
        .arg(percent).arg(time(elapsed / 1000), left).arg(speed, 0, 'f', 2);
    const int barWidth = std::clamp(columns_ - int(metrics.size()) - 16, 4, 32);
    drawText(p, 0, y + 2, QString("|/-\\").mid(searchSpinner_, 1), cyan, 1);
    // El avance mide el trabajo resuelto; la velocidad usa solamente los bytes leídos.
    p.fillRect(2 * cellWidth_, (y + 2) * cellHeight_ + cellHeight_ / 3,
               int(barWidth * cellWidth_ * double(done) / double(total)),
               std::max(2, cellHeight_ / 3), cyan);
    drawText(p, 2 + barWidth, y + 2, metrics, cyan, columns_ - 14 - barWidth);
    drawCommands(p, columns_ - 11, y + 2, "[Esc] Cancel", 11);
}

void TreeWindow::searchErrorKey(int key)
{
    if (key == Qt::Key_Escape || key == Qt::Key_C) { finishSearch(true); return; }
    if (key == Qt::Key_R) { searchNext(); return; }
    if (key != Qt::Key_T && key != Qt::Key_U && key != Qt::Key_K && key != Qt::Key_L) return;
    const bool keep = key == Qt::Key_T || key == Qt::Key_K;
    if (!keep) session_.tags.remove(searchTargets_[searchIndex_]);
    if ((key == Qt::Key_K || key == Qt::Key_L) && errorPolicies_.size() < 5)
        errorPolicies_.insert(lastSearchError_.errorType, keep);
    ++searchIndex_;
    searchNext();
}

bool TreeWindow::applyFilespec(const QString &text)
{
    QString error;
    if (!session_.filespec.set(text, &error)) { status_ = error; return false; }
    const QString applied = session_.filespec.text();
    if (applied != "*.*") rememberHistory(filespecHistory_, applied);
    session_.rebuild();
    refresh();
    return true;
}

void TreeWindow::filespecPromptKey(QKeyEvent *event)
{
    if (filespecHistory_.visible) { historyKey(event);return; }
    const int key = event->key();
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const bool alt = event->modifiers().testFlag(Qt::AltModifier);
    if (key == Qt::Key_Escape) filespecPrompt_ = false;
    else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (applyFilespec(filespecInput_)) filespecPrompt_ = false;
    } else if (alt && key == Qt::Key_Up) rememberHistory(filespecHistory_, filespecInput_);
    else if (key == Qt::Key_Up || key == Qt::Key_Down) { openHistory(key == Qt::Key_Up);return; }
    else if (key == Qt::Key_F3) {
        if (!filespecHistory_.entries.isEmpty()) {
            filespecInput_ = filespecHistory_.entries.first();filespecCursor_ = int(filespecInput_.size());
        }
    } else if ((ctrl || alt) && ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) &&
               !(ctrl && (key == Qt::Key_C || key == Qt::Key_V || key == Qt::Key_X))) {
        const auto mark = QString(QChar(key));
        for (const auto &text : filespecHistory_.entries) if (filespecHistory_.marks.value(text) == mark) {
            filespecInput_ = text;filespecCursor_ = int(text.size());
            if (alt && applyFilespec(text)) filespecPrompt_ = false;
            break;
        }
    } else if (key == Qt::Key_Left) filespecCursor_ = std::max(0, filespecCursor_ - 1);
    else if (key == Qt::Key_Right) filespecCursor_ = std::min(int(filespecInput_.size()), filespecCursor_ + 1);
    else if (key == Qt::Key_Home) filespecCursor_ = 0;
    else if (key == Qt::Key_End) filespecCursor_ = int(filespecInput_.size());
    else if (key == Qt::Key_Backspace && ctrl) { filespecInput_.clear(); filespecCursor_ = 0; }
    else if (key == Qt::Key_Backspace && filespecCursor_ > 0) filespecInput_.remove(--filespecCursor_, 1);
    else if (key == Qt::Key_Delete) filespecInput_.remove(filespecCursor_, 1);
    else {
        QString text;
        if ((ctrl && key == Qt::Key_V) || (key == Qt::Key_Insert && event->modifiers().testFlag(Qt::ShiftModifier)))
            text = QGuiApplication::clipboard()->text();
        else if (alt && key == Qt::Key_BracketLeft) text = "◄";
        else if (alt && key == Qt::Key_BracketRight) text = "►";
        else if (!ctrl && !alt) text = event->text();
        text.remove('\n'); text.remove('\r');
        text = text.left(1024 - filespecInput_.size());
        filespecInput_.insert(filespecCursor_, text); filespecCursor_ += int(text.size());
    }
    update();
}

void TreeWindow::searchPromptKey(QKeyEvent *event)
{
    if(promptHistory().visible){historyKey(event);return;}
    const int key = event->key();
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const bool alt = event->modifiers().testFlag(Qt::AltModifier);
    if (key == Qt::Key_Escape) searchPrompt_ = false;
    else if (key == Qt::Key_Return || key == Qt::Key_Enter) { startSearch(); return; }
    else if (key == Qt::Key_F2) searchOptions_.caseSensitive = !searchOptions_.caseSensitive;
    else if (key == Qt::Key_F4) searchOptions_.mode = nextSearchMode(searchOptions_.mode);
    else if(alt && key==Qt::Key_Up)rememberSearch(searchInput_);
    else if (key == Qt::Key_Up || key == Qt::Key_Down) { openHistory(key==Qt::Key_Up);return; }
    else if (key == Qt::Key_F3) {
        if (!searchHistory_.entries.isEmpty()) {searchInput_=searchHistory_.entries.first();searchCursor_=int(searchInput_.size());}
    }else if((ctrl || alt) && ((key>=Qt::Key_A && key<=Qt::Key_Z) || (key>=Qt::Key_0 && key<=Qt::Key_9)) && !(ctrl && (key==Qt::Key_C || key==Qt::Key_V || key==Qt::Key_X))){
        const auto mark=QString(QChar(key));
        for(const auto &text:searchHistory_.entries)if(searchHistory_.marks.value(text)==mark){
            searchInput_=text;searchCursor_=int(text.size());if(alt){startSearch();return;}break;
        }
    } else if (key == Qt::Key_Left) searchCursor_ = std::max(0, searchCursor_ - 1);
    else if (key == Qt::Key_Right) searchCursor_ = std::min(int(searchInput_.size()), searchCursor_ + 1);
    else if (key == Qt::Key_Home) searchCursor_ = 0;
    else if (key == Qt::Key_End) searchCursor_ = int(searchInput_.size());
    else if (key == Qt::Key_Backspace) {
        if (ctrl) { searchInput_.clear(); searchCursor_ = 0; }
        else if (searchCursor_ > 0) searchInput_.remove(--searchCursor_, 1);
    } else if (key == Qt::Key_Delete) searchInput_.remove(searchCursor_, 1);
    else if ((ctrl && key == Qt::Key_V) || (key == Qt::Key_Insert && event->modifiers().testFlag(Qt::ShiftModifier))) {
        QString text = QGuiApplication::clipboard()->text();
        text.replace('\n', ' '); text.replace('\r', ' ');
        text = text.left(255 - searchInput_.size());
        searchInput_.insert(searchCursor_, text); searchCursor_ += text.size();
    } else if (!ctrl && !event->modifiers().testFlag(Qt::AltModifier) && !event->text().isEmpty()) {
        const QString text = event->text().left(255 - searchInput_.size());
        searchInput_.insert(searchCursor_, text); searchCursor_ += text.size();
    } else status_ = "Search option is not implemented yet";
    update();
}

}
