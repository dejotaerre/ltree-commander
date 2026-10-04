#include "ui/treewindow.h"
#include "ui/theme.h"
#include "fs/mounts.h"
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace ltree {
void TreeWindow::connectGraft()
{
    graftTimer_.setInterval(100);
    connect(&graftTimer_, &QTimer::timeout, this, [this] { ++graftSpinner_;update(); });
    connect(&graftWatcher_, &QFutureWatcher<GraftResult>::finished, this, [this] {
        const auto result = graftWatcher_.result();busy_ = false;graftTimer_.stop();
        const auto target = result.moved ? result.target : result.recoverySource;
        const auto reconcile = [&](Session &saved) {
            if (!target.isEmpty()) saved.graftPath(result.source, target);
            saved.apply(result.scan, true);
        };
        reconcile(session_);if (otherSession_) reconcile(*otherSession_);
        if (otherGlobalReturn_) reconcile(*otherGlobalReturn_);
        const auto mapHistory = [&](auto &history) {
            QMap<QString, Session> mapped;
            for (auto saved : history) { reconcile(saved);mapped.insert(saved.root, saved); }
            history = mapped;
        };
        mapHistory(rootHistory_);mapHistory(otherRootHistory_);mapHistory(loggedRoots_);
        if (!target.isEmpty()) {
            if (isWithin(initialRoot_, result.source)) initialRoot_ = target + initialRoot_.mid(result.source.size());
            QSet<QString> mapped;
            for (const auto &path : pendingMergedTags_) mapped.insert(isWithin(path, result.source) ? target + path.mid(result.source.size()) : path);
            pendingMergedTags_ = mapped;
            const auto mapScroll = [&](auto &scroll) {
                QMap<QString, QPair<int,int>> next;
                for (auto it = scroll.cbegin(); it != scroll.cend(); ++it)
                    next.insert(isWithin(it.key(), result.source) ? target + it.key().mid(result.source.size()) : it.key(), it.value());
                scroll = next;
            };
            mapScroll(rootScroll_);mapScroll(otherRootScroll_);
        }
        if (result.moved) {
            graftPrompt_ = false;status_ = "Branch grafted to: " + result.target;
            if (result.cancelled) status_ += "; move completed before stop";
        } else if (result.cancelled) { graftPrompt_ = false;status_ = "Graft cancelled"; }
        else {
            status_ = result.error;
            const auto current = readMetadata(result.source);
            if (current.error.isEmpty() && current.inode == graftSource_.inode && current.device == graftSource_.device) graftSource_ = current;
            else graftPrompt_ = false;
        }
        if (!result.error.isEmpty() && (result.moved || result.cancelled)) status_ += "; " + result.error;
        if (!result.recoverySource.isEmpty()) graftPrompt_ = false;
        availableBytes_ = QStorageInfo(session_.root).bytesAvailable();refresh();
    });
    connect(&graftBrowseWatcher_, &QFutureWatcher<ScanResult>::finished, this, [this] {
        const auto result = graftBrowseWatcher_.result();busy_ = false;
        if (graftBrowser_) {
            if (graftBrowseParent_) graftBrowser_->expandToParent(result);
            else graftBrowser_->apply(result, true);
            session_.apply(result, true);if (otherSession_) otherSession_->apply(result, true);
            graftBrowseTop_ = 0;
        }
        graftBrowseParent_ = false;
        status_ = result.cancelled ? "Browse logging cancelled" : QString();
        for (const auto &dir : result.directories) if (!dir.error.isEmpty()) { status_ = dir.error;break; }
        refresh();
    });
}

void TreeWindow::beginGraft()
{
    stopAutoview();infoVisible_ = false;activeSpell_ = false;
    graftSource_ = readMetadata(session_.directory);
    if (!graftSource_.error.isEmpty() || !graftSource_.directory || graftSource_.symlink || graftSource_.path == "/") {
        status_ = "Select a real directory below the filesystem root to graft";return;
    }
    const QString opposite = otherSession_ ? (otherSession_->view != View::Tree && otherSession_->currentFile() ?
        QFileInfo(otherSession_->currentFile()->path).absolutePath() : otherSession_->directory) : QString();
    graftDefault_ = !opposite.isEmpty() ? opposite : graftHistory_.entries.isEmpty() ? QFileInfo(graftSource_.path).absolutePath() : graftHistory_.entries.first();
    graftInput_ = graftDefault_;graftCursor_ = int(graftInput_.size());graftSelected_ = true;
    graftPaths_ = {graftSource_.path};
    for (const auto &path : QStringList{graftHistory_.entries.value(0), opposite, graftDefault_})
        if (!path.isEmpty() && !graftPaths_.contains(path)) graftPaths_.append(path);
    graftPathIndex_ = -1;graftHistory_.visible = graftHelp_ = graftLocations_ = false;
    graftBrowser_.reset();graftPrompt_ = true;refresh();
}

void TreeWindow::startGraftBrowse(const QString &path, bool parent)
{
    if (!parent && !graftBrowser_) graftBrowser_.emplace(path);
    graftBrowseParent_ = parent;busy_ = true;cancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = cancel_;status_ = "Logging browse directory…";
    graftBrowseWatcher_.setFuture(QtConcurrent::run([path, cancel] { return scanDirectories(path, false, cancel); }));update();
}

void TreeWindow::graftKey(QKeyEvent *event)
{
    if (graftBrowser_) { graftBrowseKey(event);return; }
    if (graftHistory_.visible) { historyKey(event);return; }
    const int key = event->key();const auto mods = event->modifiers();
    const bool ctrl = mods.testFlag(Qt::ControlModifier), alt = mods.testFlag(Qt::AltModifier);
    if (graftHelp_) {
        graftHelp_ = helpDocumentKey(helpDocument_, event, columns_, rows_);
        update();return;
    }
    if (key == Qt::Key_Escape) { graftPrompt_ = false;status_ = "Graft cancelled"; }
    else if (key == Qt::Key_F1) { helpDocument_.open(HelpTopic::Graft);graftHelp_ = true; }
    else if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_F2) {
        if (key == Qt::Key_F2 && graftInput_.isEmpty()) { startGraftBrowse(graftSource_.path);return; }
        if (graftInput_.isEmpty() || graftInput_.contains(QChar::Null)) { status_ = "Enter a destination directory";refresh();return; }
        QString input = graftInput_;if (input == "~") input = QDir::homePath();else if (input.startsWith("~/")) input = QDir::homePath() + input.mid(1);
        const auto path = QDir::cleanPath(QDir(graftSource_.path).absoluteFilePath(input));
        const auto destination = readMetadata(path);
        if (!destination.error.isEmpty() || !destination.directory || destination.symlink) {
            status_ = "Destination must be an existing real directory";refresh();return;
        }
        if (key == Qt::Key_F2) { graftBrowseTop_ = 0;startGraftBrowse(path);return; }
        rememberHistory(graftHistory_, path);
        const auto source = graftSource_;cancel_ = std::make_shared<std::atomic_bool>(false);const auto cancel = cancel_;
        busy_ = true;status_ = "Moving branch…";graftTimer_.start();
        graftWatcher_.setFuture(QtConcurrent::run([source, destination, cancel] { return graftBranch(source, destination, cancel); }));
    } else if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
        const bool reverse = key == Qt::Key_Backtab || mods.testFlag(Qt::ShiftModifier);
        graftPathIndex_ = graftPathIndex_ < 0 ? (reverse ? int(graftPaths_.size()) - 1 : 0) :
            (graftPathIndex_ + (reverse ? int(graftPaths_.size()) - 1 : 1)) % int(graftPaths_.size());
        graftInput_ = graftPaths_[graftPathIndex_];graftCursor_ = int(graftInput_.size());graftSelected_ = true;
    } else if (alt && key == Qt::Key_Up) rememberHistory(graftHistory_, graftInput_);
    else if (key == Qt::Key_Up || key == Qt::Key_Down) { openHistory(key == Qt::Key_Up);return; }
    else if (key == Qt::Key_F3) {
        if (!graftHistory_.entries.isEmpty()) { graftInput_ = graftHistory_.entries.first();graftCursor_ = int(graftInput_.size());graftSelected_ = true; }
    } else if (ctrl && key == Qt::Key_A) graftSelected_ = true;
    else if (key == Qt::Key_Left) { graftCursor_ = std::max(0, graftCursor_ - 1);graftSelected_ = false; }
    else if (key == Qt::Key_Right) { graftCursor_ = std::min(int(graftInput_.size()), graftCursor_ + 1);graftSelected_ = false; }
    else if (key == Qt::Key_Home) { graftCursor_ = 0;graftSelected_ = false; }
    else if (key == Qt::Key_End) { graftCursor_ = int(graftInput_.size());graftSelected_ = false; }
    else if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
        if (graftSelected_ || (ctrl && key == Qt::Key_Backspace)) { graftInput_.clear();graftCursor_ = 0;graftSelected_ = false; }
        else if (key == Qt::Key_Backspace && graftCursor_ > 0) graftInput_.remove(--graftCursor_, 1);
        else if (key == Qt::Key_Delete) graftInput_.remove(graftCursor_, 1);
    } else {
        QString text;
        if ((ctrl && key == Qt::Key_V) || (key == Qt::Key_Insert && mods.testFlag(Qt::ShiftModifier))) text = QGuiApplication::clipboard()->text();
        else if (!ctrl && !alt && !event->text().isEmpty() && event->text()[0].isPrint()) text = event->text();
        if (!text.isEmpty()) {
            if (graftSelected_) { graftInput_.clear();graftCursor_ = 0;graftSelected_ = false; }
            text.remove('\n');text.remove('\r');text.remove(QChar::Null);text = text.left(4096 - graftInput_.size());
            graftInput_.insert(graftCursor_, text);graftCursor_ += int(text.size());
        }
    }
    refresh();
}

void TreeWindow::graftBrowseKey(QKeyEvent *event)
{
    const int key = event->key();const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    auto &browser = *graftBrowser_;
    if (key == Qt::Key_Escape) {
        if (graftLocations_) graftLocations_ = false;else graftBrowser_.reset();
    } else if (graftLocations_) {
        if (key == Qt::Key_Up) graftLocationIndex_ = std::max(0, graftLocationIndex_ - 1);
        else if (key == Qt::Key_Down) graftLocationIndex_ = std::min(int(graftLocationPaths_.size()) - 1, graftLocationIndex_ + 1);
        else if (enter && !graftLocationPaths_.isEmpty()) {
            const auto path = graftLocationPaths_[graftLocationIndex_];browser = Session(path);graftLocations_ = false;startGraftBrowse(path);
        }
    } else if (enter) {
        const auto path = browser.directory;const auto metadata = readMetadata(path);
        if (!metadata.error.isEmpty() || !metadata.directory || metadata.symlink) status_ = "Select a real destination directory";
        else { graftInput_ = path;graftCursor_ = int(path.size());graftSelected_ = true;graftBrowser_.reset(); }
    } else if (key == Qt::Key_Up || key == Qt::Key_Down) browser.move(key == Qt::Key_Up ? -1 : 1);
    else if (key == Qt::Key_Home) browser.first();
    else if (key == Qt::Key_End) browser.last();
    else if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) browser.move((key == Qt::Key_PageUp ? -1 : 1) * std::max(1, rows_ - 8));
    else if (key == Qt::Key_Left || key == Qt::Key_Backspace) {
        if (browser.directory != browser.root) browser.parent();
        else if (browser.root != "/") startGraftBrowse(QFileInfo(browser.root).absolutePath(), true);
    } else if (key == Qt::Key_Right || key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_F3) startGraftBrowse(browser.directory);
    else if (key == Qt::Key_F4) { browser = Session(graftSource_.path);startGraftBrowse(browser.root); }
    else if (key == Qt::Key_F5 || key == Qt::Key_F6) browser.toggleCollapse(key == Qt::Key_F5);
    else if (key == Qt::Key_Tab || key == Qt::Key_Backtab) browser.sibling(key == Qt::Key_Backtab || event->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
    else if (key == Qt::Key_L) {
        graftLocationPaths_.clear();for (const auto &mount : mountedLocations()) graftLocationPaths_.append(mount.path);
        for (const auto &path : loggedRoots_.keys()) if (!graftLocationPaths_.contains(path)) graftLocationPaths_.append(path);
        if (otherSession_ && !graftLocationPaths_.contains(otherSession_->root)) graftLocationPaths_.append(otherSession_->root);
        graftLocationPaths_.sort();graftLocationIndex_ = std::max(0, int(graftLocationPaths_.indexOf(browser.root)));graftLocations_ = true;
    } else if (!event->text().isEmpty() && event->text()[0].isLetter() && !event->modifiers().testFlag(Qt::ControlModifier)) {
        browser.treeSpell += event->text();if (!browser.spell(browser.treeSpell)) { browser.treeSpell = event->text();browser.spell(browser.treeSpell); }
    }
    refresh();
}

void TreeWindow::drawGraft(QPainter &p)
{
    if(graftHelp_){drawHelp(p);return;}
    const int y = bottom() + 1;
    drawText(p, 0, y, "GRAFT branch: " + graftSource_.path, theme::labels);
    drawInput(p, 0, y + 1, "         to: ", graftInput_, graftCursor_, theme::normal);
    if (graftSelected_ && !busy_) {
        const int prefix = 13, start = std::max(0, graftCursor_ - (columns_ - prefix) + 1);
        const auto visible = graftInput_.mid(start).left(columns_ - prefix);
        p.fillRect(prefix * cellWidth_, (y + 1) * cellHeight_, visible.size() * cellWidth_, cellHeight_, theme::selection);
        drawText(p, prefix, y + 1, visible, Qt::black);drawCursor(p, prefix + graftCursor_ - start, y + 1, graftInput_.mid(graftCursor_, 1));
    }
    if (busy_) drawCommands(p, 0, y + 2, QString("%1 Moving branch…  [Esc] Stop").arg(QString("|/-\\")[graftSpinner_ % 4]));
    else if (!status_.isEmpty()) drawText(p, 0, y + 2, status_, theme::normal);
    else drawCommands(p, 0, y + 2, "Enter path   [Tab] Paths [F2] Browse [F3] Last [Up] History [Enter] OK [Esc] Cancel");
}

void TreeWindow::drawGraftBrowse(QPainter &p)
{
    p.fillRect(rect(), QColor(0,128,128));
    const auto &browser = *graftBrowser_;const int page = std::max(1, rows_ - 8);
    const int selected = graftLocations_ ? graftLocationIndex_ : browser.treeIndex();
    graftBrowseTop_ = std::clamp(graftBrowseTop_, std::max(0, selected - page + 1), selected);
    drawText(p, 1, 1, graftLocations_ ? "BROWSE locations" : "BROWSE: " + browser.root, theme::normal);
    const int count = graftLocations_ ? int(graftLocationPaths_.size()) : int(browser.tree().size());
    for (int row = 0; row < page && graftBrowseTop_ + row < count; ++row) {
        const int index = graftBrowseTop_ + row;
        if (index == selected) p.fillRect(cellWidth_, (row + 3) * cellHeight_, (columns_ - 2) * cellWidth_, cellHeight_, theme::selection);
        const auto text = graftLocations_ ? graftLocationPaths_[index] : browser.tree()[index].prefix + browser.tree()[index].name;
        drawText(p, 1, row + 3, text, index == selected ? Qt::black : theme::normal, columns_ - 2);
    }
    drawText(p, 1, rows_ - 4, status_.isEmpty() ? browser.directory : status_, theme::normal, columns_ - 2);
    drawCommands(p, 1, rows_ - 3, busy_ ? "[Esc] Cancel logging" : "[Enter] Select [Esc] Return [Right/+] Log [Backspace] Parent [L] Locations", columns_ - 2);
    drawCommands(p, 1, rows_ - 2, "[Arrows/PgUp/PgDn/Home/End] Navigate [F3] Relog [F4] Current [F5/F6] Collapse", columns_ - 2);
}
}
