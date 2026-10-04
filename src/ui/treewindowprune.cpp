#include "ui/treewindow.h"
#include "ui/theme.h"
#include "fs/mounts.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QKeyEvent>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace ltree {
void TreeWindow::connectPrune()
{
    pruneTimer_.setInterval(100);
    connect(&pruneTimer_, &QTimer::timeout, this, [this] { ++pruneSpinner_;update(); });
    connect(&pruneWatcher_, &QFutureWatcher<PruneResult>::finished, this, [this] {
        const auto result = pruneWatcher_.result();
        pruneTimer_.stop();busy_ = prunePrompt_ = false;
        const auto reconcile = [&](Session &saved) {
            // Una raíz eliminada pasa a su padre antes de quitarla del modelo.
            if (result.removedDirectories.contains(saved.root) && !saved.expandToParent(result.scan))
                saved = Session(QFileInfo(result.path).absolutePath());
            for (const auto &path : result.removedFiles) saved.removeFile(path);
            for (const auto &path : result.removedDirectories) saved.removeDirectory(path);
            saved.apply(result.scan, true);
        };
        reconcile(session_);
        if (otherSession_) reconcile(*otherSession_);
        if (otherGlobalReturn_) reconcile(*otherGlobalReturn_);
        for (const auto &path : result.removedFiles) pendingMergedTags_.remove(path);
        for (auto &saved : rootHistory_) reconcile(saved);
        for (auto &saved : otherRootHistory_) reconcile(saved);
        for (auto &saved : loggedRoots_) reconcile(saved);
        // Las claves de raíces borradas no deben reaparecer al ciclar ubicaciones.
        const auto removeStale = [&](auto &saved) {
            for (auto it = saved.begin(); it != saved.end();)
                if (result.removedDirectories.contains(it.key())) it = saved.erase(it); else ++it;
        };
        removeStale(rootHistory_);removeStale(otherRootHistory_);removeStale(loggedRoots_);
        removeStale(rootScroll_);removeStale(otherRootScroll_);
        status_ = QString("Prune %1: %2 files, %3 directories %4; %5 retained")
            .arg(result.cancelled ? "cancelled" : "completed")
            .arg(result.removedFiles.size()).arg(result.removedDirectories.size())
            .arg(pruneOptions_.trash ? "trashed" : "deleted").arg(result.retained);
        if (!result.error.isEmpty()) status_ += "; " + result.error;
        availableBytes_ = QStorageInfo(session_.root).bytesAvailable();refresh();
    });
}

void TreeWindow::beginPrune()
{
    stopAutoview();infoVisible_ = false;activeSpell_ = false;
    if (session_.directory == "/") { status_ = "Cannot prune the filesystem root";return; }
    for (const auto &mount : mountedLocations(nullptr, true)) {
        if (mount.path == session_.directory) { status_ = "Cannot prune a mount point; select a directory below it";return; }
    }
    if (session_.directory == session_.root) {
        pendingRootPrune_ = true;navigateParent(true);return;
    }
    if (!session_.currentDirectory().loaded || !session_.currentDirectory().error.isEmpty()) {
        status_ = "Log the current directory before pruning; * logs its branch";return;
    }
    pruneSource_ = readMetadata(session_.directory);
    if (!pruneSource_.error.isEmpty() || !pruneSource_.directory || pruneSource_.symlink) {
        status_ = "Select a real logged directory to prune";return;
    }
    pruneLogged_.clear();prunePartial_ = false;
    for (const auto &dir : session_.directories()) {
        if (!isWithin(dir.path, pruneSource_.path)) continue;
        const auto metadata = readMetadata(dir.path);
        if (metadata.symlink) continue;
        if (dir.loaded && dir.error.isEmpty() && metadata.error.isEmpty() && metadata.directory)
            pruneLogged_.insert(dir.path, metadata);
        else prunePartial_ = true;
    }
    pruneOptions_ = {};pruneInput_.clear();pruneCursor_ = pruneSpinner_ = 0;
    pruneProceed_ = pruneHelp_ = false;prunePrompt_ = true;refresh();
}

void TreeWindow::startPrune()
{
    cancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = cancel_;const auto source = pruneSource_;const auto logged = pruneLogged_;
    const auto options = pruneOptions_;const auto root = session_.root;
    const QPointer<TreeWindow> window(this);
    busy_ = true;status_ = source.path;pruneTimer_.start();
    pruneWatcher_.setFuture(QtConcurrent::run([root, source, logged, options, cancel, window] {
        QElapsedTimer elapsed;elapsed.start();
        return pruneBranch(root, source, logged, options, cancel, [&](const QString &path) {
            if (elapsed.elapsed() < 50) return;
            elapsed.restart();
            if (window) QMetaObject::invokeMethod(window, [window, path] {
                if (window && window->busy_ && window->prunePrompt_) { window->status_ = path;window->update(); }
            }, Qt::QueuedConnection);
        });
    }));
    update();
}

void TreeWindow::pruneKey(QKeyEvent *event)
{
    const int key = event->key();
    if (pruneHelp_) {
        pruneHelp_ = helpDocumentKey(helpDocument_, event, columns_, rows_);
        update();return;
    }
    if (key == Qt::Key_Escape || (pruneProceed_ && key == Qt::Key_N)) {
        prunePrompt_ = false;status_ = "Prune cancelled";
    } else if (key == Qt::Key_F1) { helpDocument_.open(HelpTopic::Prune);pruneHelp_ = true; }
    else if (pruneProceed_) {
        if (key == Qt::Key_Y) startPrune();
    } else if (key == Qt::Key_F2 || key == Qt::Key_F4 || key == Qt::Key_F5 || key == Qt::Key_F6) {
        bool &option = key == Qt::Key_F2 ? pruneOptions_.forceReadOnly : key == Qt::Key_F4 ? pruneOptions_.trash :
            key == Qt::Key_F5 ? pruneOptions_.keepCurrentFiles : pruneOptions_.onlyEmpty;
        option = !option;pruneInput_.clear();pruneCursor_ = 0;
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (pruneInput_.compare("PRUNE", Qt::CaseInsensitive) != 0) status_ = "Type PRUNE to confirm";
        else if (prunePartial_) { pruneProceed_ = true;pruneInput_.clear();pruneCursor_ = 0; }
        else startPrune();
    } else if (key == Qt::Key_Left) pruneCursor_ = std::max(0, pruneCursor_ - 1);
    else if (key == Qt::Key_Right) pruneCursor_ = std::min(int(pruneInput_.size()), pruneCursor_ + 1);
    else if (key == Qt::Key_Home) pruneCursor_ = 0;
    else if (key == Qt::Key_End) pruneCursor_ = int(pruneInput_.size());
    else if (key == Qt::Key_Backspace && pruneCursor_ > 0) pruneInput_.remove(--pruneCursor_, 1);
    else if (key == Qt::Key_Delete) pruneInput_.remove(pruneCursor_, 1);
    else if (!event->modifiers().testFlag(Qt::ControlModifier) && !event->modifiers().testFlag(Qt::AltModifier) &&
             !event->text().isEmpty() && event->text()[0].isPrint() && pruneInput_.size() < 16) {
        const QString text = event->text().left(16 - pruneInput_.size());
        pruneInput_.insert(pruneCursor_, text);pruneCursor_ += int(text.size());
    }
    refresh();
}

void TreeWindow::drawPrune(QPainter &p)
{
    const auto yes = [](bool value) { return value ? "yes" : "no"; };
    const int y = bottom() + 1;
    if(pruneHelp_){drawHelp(p);return;}
    if (busy_) {
        drawText(p, 0, y, QString("PRUNE %1: %2").arg(QString("|/-\\")[pruneSpinner_ % 4], pruneSource_.path), theme::labels);
        drawText(p, 0, y + 1, status_, theme::normal);
        drawCommands(p, 0, y + 2, "[Esc] Stop pending deletions");return;
    }
    if (pruneProceed_) {
        drawText(p, 0, y, "PRUNE: " + pruneSource_.path, theme::labels);
        drawText(p, 0, y + 1, "Branch is not fully logged. Prune logged directories only? Y/N", theme::normal);
        drawCommands(p, 0, y + 2, "[Y] Proceed   [N/Esc] Cancel   [F1] Help");return;
    }
    const bool wide = columns_ >= 105;
    drawCommands(p, 0, y, QString(wide ? "PRUNE:  [F5] Keep current directory files (%1)   [F4] Use Trash (%2)" :
        "PRUNE:  [F5] Keep current files (%1)   [F4] Use Trash (%2)").arg(yes(pruneOptions_.keepCurrentFiles), yes(pruneOptions_.trash)));
    drawCommands(p, 0, y + 1, QString(wide ? "        [F6] Delete only empty directories (%1)   [F2] Force read-only files (%2)" :
        "        [F6] Empty dirs only (%1)   [F2] Force read-only files (%2)").arg(yes(pruneOptions_.onlyEmpty), yes(pruneOptions_.forceReadOnly)));
    drawInput(p, 0, y + 2, status_.isEmpty() ? "Enter the word PRUNE: " : "Type PRUNE: ", pruneInput_, pruneCursor_, theme::normal, 39);
    drawCommands(p, 40, y + 2, "[Enter] OK   [F1] Help   [Esc] Cancel");
}
}
