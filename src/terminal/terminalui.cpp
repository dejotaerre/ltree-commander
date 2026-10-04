#include "terminal/terminalui.h"
#include "core/session.h"
#include "core/helpdocument.h"
#include "fs/mounts.h"
#include "fs/viewdocument.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <csignal>
#include <clocale>
#include <cstdio>
#include <cwchar>
#include <optional>
#include <unistd.h>
#define NCURSES_NOMACROS 1
#include <curses.h>

namespace ltree {
namespace {
volatile std::sig_atomic_t stopped = 0;
void stopTerminal(int) { stopped = 1; }
struct Pane {
    Session session;
    int treeTop = 0, fileTop = 0;
    explicit Pane(const QString &path) : session(path) {}
};
enum class Display { Name, SizeAttributes, Details, LongName };
enum class Prompt { None, Filespec, Path, Spell, Quit };
enum class Work { None, Scan, Document };
enum TerminalKey { AltLeft = 0x2000, AltRight, AltHome };
struct Layout { int rows, columns, stride, width; int capacity() const { return rows * columns; } };

class TerminalUI {
    HelpDocument helpDocument_{true};
    Pane active_;
    std::optional<Pane> other_;
    int side_ = 0, rows_ = 0, columns_ = 0, menu_ = 0, spinner_ = 0;
    Display display_ = Display::Details;
    Prompt prompt_ = Prompt::None;
    QString input_, status_, viewerPath_;
    QStringList filespecHistory_;
    int cursor_ = 0, historyIndex_ = -1, locationIndex_ = 0, locationTop_ = 0, viewerTop_ = 0;
    QVector<MountPoint> locations_;
    bool help_ = false, location_ = false, viewer_ = false, hex_ = false, quit_ = false;
    bool sizes_ = false, colors_ = false, parentLoad_ = false, preserveTags_ = true;
    Work work_ = Work::None;
    Cancellation cancel_;
    QFuture<ScanResult> scan_;
    QFuture<ViewDocument> document_;
    ViewDocument viewed_;
    bool autoview_ = false, autoviewFromTree_ = false, pendingAutoview_ = false;
    bool previewReading_ = false, previewHex_ = false, previewAuto_ = true;
    int previewTop_ = 0, autoviewPercent_ = 40;
    QString previewPath_;
    ViewDocument previewed_;
    Cancellation previewCancel_;
    QFuture<ViewDocument> previewRead_;

    int end() const { return rows_ - 4; }
    int divider() const { return 2 + (end() - 2) * 2 / 3; }
    int width() const { return other_ ? (side_ ? columns_ - columns_ / 2 : columns_ / 2) : columns_; }
    int listWidth(const Pane &pane, int w, bool focused) const
    {
        return autoview_ && focused && pane.session.view != View::Tree && w >= 36 ?
            std::clamp(w * autoviewPercent_ / 100, 16, w - 20) : w;
    }
    int listWidth() const { return listWidth(active_, width(), true); }
    int bytesPerRow(int w) const { return w >= 78 ? 16 : w >= 46 ? 8 : w >= 30 ? 4 : 1; }
    int ink(int pair) const { return colors_ ? COLOR_PAIR(pair) : pair == 3 ? A_REVERSE : A_NORMAL; }
    void text(int x, int y, const QString &value, int pair = 1, int limit = -1)
    {
        if (y < 0 || y >= rows_ || x < 0 || x >= columns_) return;
        limit = std::min(limit < 0 ? columns_ - x : limit, columns_ - x);
        wattrset(stdscr, ink(pair));
        int count = 0;
        for (char32_t cp : value.toUcs4()) {
            if (count >= limit) break;
            // Evitar controles y glifos dobles que desplacen la cuadrícula.
            wchar_t glyph = wchar_t(cp);
            if (cp < 32 || (cp >= 127 && cp < 160) || ::wcwidth(glyph) != 1) glyph = L'?';
            mvwaddnwstr(stdscr, y, x + count++, &glyph, 1);
        }
    }
    QString pathLabel(const QString &path, int available) const
    {
        return path.size() <= available ? path : QString("…") + path.right(std::max(0, available - 1));
    }
    void fill(int x, int y, int count, int pair)
    {
        wattrset(stdscr, ink(pair));mvwhline(stdscr, y, x, ' ', std::max(0, std::min(count, columns_ - x)));
    }
    void drawHelp()
    {
        const int x = std::max(2, (columns_-88)/2), w = std::max(8, std::min(88, columns_-2*x));
        const int height = std::max(1, rows_-6);
        helpDocument_.constrain(w, height);
        const auto lines = helpDocument_.lines(w);
        text(1, 0, (columns_<64 ? "LTree Help / " : "LTree Commander Help / ") + helpDocument_.title(), 2);
        text(1, 1, QString(columns_-2, QChar(0x2500)), 2);
        for(int i = 0; i < height && helpDocument_.top()+i < lines.size(); ++i) {
            const int row = helpDocument_.top()+i;
            const auto &line = lines[row];
            const bool selected = helpDocument_.index() && row == helpDocument_.selected()+2;
            if(selected)fill(x, i+3, w, 3);
            text(x, i+3, line.text, selected ? 3 : line.style == HelpStyle::Heading ? 2 : 1, w);
            if(!selected && line.keyLength)text(x, i+3, line.text.left(line.keyLength), 2);
            if(!helpDocument_.index() && !helpDocument_.finding() && !helpDocument_.query().isEmpty()) {
                int at=0;
                while((at=line.text.indexOf(helpDocument_.query(),at,Qt::CaseInsensitive))>=0) {
                    text(x+at,i+3,line.text.mid(at,helpDocument_.query().size()),3);at+=helpDocument_.query().size();
                }
            }
        }
        if(lines.size()>height) {
            const int thumb=std::max(1,height*height/int(lines.size()));
            const int position=helpDocument_.top()*(height-thumb)/std::max(1,int(lines.size())-height);
            for(int i=0;i<height;++i)text(columns_-2,i+3,i>=position && i<position+thumb ? "█" : "│",2,1);
        }
        text(1, rows_-3, QString(columns_-2, QChar(0x2500)), 2);
        commands(1, rows_-2, columns_<64 ? "[↑↓] Scroll [←→] Chapter [Tab] Index" : "[Up/Down] Scroll [PgUp/PgDn] Page [Left/Right] Chapter [Tab] Index");
        if(helpDocument_.finding()) {
            commands(1,rows_-2,"[Enter] Find [Backspace] Erase [Esc] Cancel search");
            text(1, rows_-1, "Find in help: "+helpDocument_.query().right(std::max(1,columns_-17)));
            fill(std::min(columns_-2,15+int(helpDocument_.query().size())),rows_-1,1,3);
        } else commands(1,rows_-1,"[F] Find [Space] Next [Esc/F1] Return  "+(helpDocument_.notice().isEmpty() ?
            QString("Lines %1-%2/%3").arg(helpDocument_.top()+1).arg(std::min(helpDocument_.top()+height,int(lines.size()))).arg(lines.size()) : helpDocument_.notice()));
    }
    void helpKey(int value)
    {
        HelpAction action=HelpAction::Input;QString typed;
        if(value==27 || value==KEY_F(1))action=HelpAction::Close;
        else if(value==10 || value==13 || value==KEY_ENTER)action=HelpAction::Accept;
        else if(value==KEY_BACKSPACE || value==127 || value==8)action=HelpAction::Backspace;
        else if(helpDocument_.finding()) { if(value>=32 && value<KEY_MIN)typed=QString(QChar(value)); }
        else if(value==KEY_UP)action=HelpAction::Up;
        else if(value==KEY_DOWN)action=HelpAction::Down;
        else if(value==KEY_PPAGE)action=HelpAction::PageUp;
        else if(value==KEY_NPAGE)action=HelpAction::PageDown;
        else if(value==KEY_HOME)action=HelpAction::Home;
        else if(value==KEY_END)action=HelpAction::End;
        else if(value==KEY_LEFT || value=='[')action=HelpAction::Previous;
        else if(value==KEY_RIGHT || value==']')action=HelpAction::Next;
        else if(value==9)action=HelpAction::Index;
        else if(value=='f' || value=='F' || value=='/')action=HelpAction::Find;
        else if(value==' ')action=HelpAction::Repeat;
        help_=helpDocument_.act(action,std::max(8,std::min(88,columns_-4)),std::max(1,rows_-6),typed);
    }
    void commands(int x, int y, const QString &value)
    {
        bool key = false;
        for (const auto ch : value) {
            if (ch == '[') key = true;
            else if (ch == ']') key = false;
            else text(x++, y, QString(ch), key ? 2 : 1, 1);
        }
    }
    Layout layout(int top, int w) const
    {
        const int available = std::max(1, w - 2);
        const bool compact = display_ == Display::Name || display_ == Display::SizeAttributes;
        const int count = compact ? std::max(1, available / (display_ == Display::Name ? 24 : 42)) : 1;
        const int stride = available / count;
        return {std::max(1, end() - top), count, stride, stride - (count > 1 ? 1 : 0)};
    }
    void visible(Pane &pane, int w)
    {
        auto &s = pane.session;
        pane.treeTop = std::clamp(pane.treeTop, std::max(0, s.treeIndex() - (divider() - 2) + 1), s.treeIndex());
        const auto grid = layout(s.view == View::Tree ? divider() + 1 : 2, w);
        if (grid.columns == 1) pane.fileTop = std::clamp(pane.fileTop, std::max(0, s.fileIndex - grid.rows + 1), s.fileIndex);
        else {
            pane.fileTop = std::max(0, pane.fileTop) / grid.rows * grid.rows;
            if (s.fileIndex < pane.fileTop) pane.fileTop = s.fileIndex / grid.rows * grid.rows;
            if (s.fileIndex >= pane.fileTop + grid.capacity()) pane.fileTop = (s.fileIndex / grid.rows - grid.columns + 1) * grid.rows;
            const int last = std::max(0, (std::max(0, int(s.files().size()) - 1) / grid.rows - grid.columns + 1) * grid.rows);
            pane.fileTop = std::min(last, pane.fileTop);
        }
    }
    void files(Pane &pane, int x, int top, int w, bool focused)
    {
        const auto &s = pane.session;const auto grid = layout(top, w);
        if (s.files().isEmpty()) { text(x + 2, top, s.currentDirectory().loaded ? "No files" : "Directory not logged", 2, w - 3);return; }
        const int first = s.view == View::Tree ? 0 : pane.fileTop;
        for (int n = 0; n < grid.capacity() && first + n < s.files().size(); ++n) {
            const auto &file = s.files()[first + n];const int y = top + n % grid.rows, left = x + 1 + n / grid.rows * grid.stride;
            const bool current = s.view != View::Tree && first + n == s.fileIndex;
            const int color = current && focused ? 3 : 2;
            if (current && focused) fill(left, y, grid.width, 3);
            if (s.tags.contains(file.path) || (current && !focused)) text(left, y, s.tags.contains(file.path) ? "♦" : "►", current ? 3 : 1, 1);
            if (display_ == Display::LongName) { text(left + 1, y, file.name, color, grid.width - 1);continue; }
            QString base = file.name, extension;const int dot = int(base.lastIndexOf('.'));
            if (dot > 0) { extension = base.mid(dot);base = base.left(dot); }
            int right = left + grid.width;
            if (display_ == Display::Details && grid.width >= 44) {
                const int dateWidth = grid.width >= 54 ? 16 : 10;right -= dateWidth + 1;
                text(right, y, file.modified.toString(dateWidth == 16 ? "yyyy-MM-dd HH:mm" : "yyyy-MM-dd"), color, dateWidth);
            }
            if (display_ != Display::Name && grid.width >= 26) {
                right -= 4;text(right, y, QString(file.writable ? "." : "r") + (file.hidden ? "h" : ".") + (file.symlink ? "l" : "."), color, 3);
                right -= 12;text(right, y, QString::number(file.size).rightJustified(11), color, 11);
            }
            const int extWidth = std::min(display_ == Display::Details && grid.width >= 54 ? 9 : 4, std::max(1, right - left - 3));right -= extWidth + 1;
            text(left + 1, y, base, color, std::max(1, right - left - 2));text(right, y, extension, color, extWidth);
        }
    }
    void pane(Pane &pane, int x, int w, bool focused)
    {
        const int list = listWidth(pane, w, focused);visible(pane, list);const auto &s = pane.session;
        const QString path = s.view != View::Tree && s.currentFile() ? QFileInfo(s.currentFile()->path).absolutePath() : s.directory;
        text(x + 1, 0, pathLabel(path, w - 2), 1, w - 2);
        for (int y = 1; y <= end(); ++y) { text(x, y, "│");text(x + w - 1, y, "│"); }
        text(x, 1, "┌" + QString(w - 2, QChar(0x2500)) + "┐", 1, w);
        text(x, end(), "└" + QString(w - 2, QChar(0x2500)) + "┘", 1, w);
        const QString mode = s.view == View::Tree ? "TREE" : s.view == View::Directory ? "DIR" : s.view == View::Branch ? "BRANCH" : "SHOWALL";
        text(x + 1, 1, "<" + mode + ": " + (s.tagsOnly ? "♦ " : "") + s.filespec.text() + ">", focused ? 2 : 1, w - 2);
        if (s.view == View::Tree) {
            const auto *sizes = sizes_ && w > 35 ? &s.branchSizes() : nullptr;
            for (int row = 2, i = pane.treeTop; row < divider() && i < s.tree().size(); ++row, ++i) {
                const auto &item = s.tree()[i];const auto &dir = s.directories()[item.path];const bool current = item.path == s.directory;
                if (current && focused) fill(x + 1, row, w - 2, 3);
                text(x + 1, row, QString(dir.loaded ? " " : "+") + item.prefix + item.name, current && focused ? 3 : 2, w - (sizes ? 17 : 2));
                if (sizes) text(x + w - 15, row, sizes->value(item.path).label(), current && focused ? 3 : 2, 14);
                if (current && !focused) text(x + 1, row, "►", 3, 1);
            }
            text(x, divider(), "├" + QString(w - 2, QChar(0x2500)) + "┤", 1, w);
            files(pane, x, divider() + 1, w, focused);
        } else {
            files(pane, x, 2, list, focused);
            if (list < w) {
                for (int y = 2; y < end(); ++y) text(x + list - 1, y, "│");
                drawAutoview(x + list, w - list - 1);
            }
        }
        const int total = s.view == View::Tree ? int(s.tree().size()) : int(s.files().size());
        const int index = total ? (s.view == View::Tree ? s.treeIndex() : s.fileIndex) + 1 : 0;
        const auto ordinal = QString(" %1/%2 ").arg(index).arg(total);text(x + list - 1 - ordinal.size(), end(), ordinal, 1);
    }
    void drawDocument(int x, int y, int h, int w, const ViewDocument &doc, bool hexMode, int &first)
    {
        const int bytes = bytesPerRow(w);
        const int count = hexMode ? int((doc.bytes.size() + bytes - 1) / bytes) : int(doc.lines.size());
        first = std::clamp(first, 0, std::max(0, count - 1));
        for (int row = 0; row < h && first + row < count; ++row) {
            const int index = first + row;
            if (hexMode) {
                const int offset = index * bytes;const auto chunk = doc.bytes.mid(offset, bytes);
                QString hex, ascii;
                for (const unsigned char value : chunk) { hex += QString("%1 ").arg(value, 2, 16, QChar('0')).toUpper();ascii += value >= 32 && value < 127 ? QChar(value) : QChar('.'); }
                text(x, y + row, QString("%1").arg(offset, 8, 16, QChar('0')).toUpper(), 1, w);text(x + 10, y + row, hex, 2, w - 10);
                if (w >= 10 + bytes * 4) text(x + 10 + bytes * 3, y + row, ascii, 2, bytes);
            } else {
                const qsizetype from = doc.lines[index], to = index + 1 < doc.lines.size() ? doc.lines[index + 1] : doc.text.size();
                text(x, y + row, QString::number(index + 1).rightJustified(5), 1, std::min(w, 5));
                text(x + 7, y + row, doc.text.mid(from, to - from).remove('\r').remove('\n').replace('\t', "    "), 2, w - 7);
            }
        }
    }
    void drawAutoview(int x, int w)
    {
        const auto *file = active_.session.currentFile();if (!file) return;
        text(x, 2, "AUTOVIEW " + QString(previewHex_ ? "HEX: " : "TEXT: ") + file->name, 1, w);
        if (previewReading_ || previewPath_ != file->path) {
            text(x, 3, QString("%1 Reading…").arg(QString("|/-\\")[spinner_++ % 4]), 1, w);
        } else if (!previewed_.error.isEmpty()) text(x, 3, previewed_.error, 2, w);
        else {
            drawDocument(x, 3, end() - 4, w, previewed_, previewHex_, previewTop_);
            text(x, end() - 1, QString("%1 bytes  %2").arg(previewed_.bytes.size()).arg(previewed_.encoding), 1, w);
        }
    }
    void drawViewer()
    {
        text(0, 0, pathLabel(viewerPath_, columns_), 1, columns_);
        drawDocument(0, 1, rows_ - 4, columns_, viewed_, hex_, viewerTop_);
        commands(0, rows_ - 2, "[Arrows/PgUp/PgDn/Home/End] Navigate  [H] Hex/Text  [Enter/Esc/Q] Return");
        text(0, rows_ - 1, QString("%1  %2 bytes  %3").arg(hex_ ? "HEX" : "TEXT").arg(viewed_.bytes.size()).arg(viewed_.encoding));
    }
    void draw()
    {
        getmaxyx(stdscr, rows_, columns_);werase(stdscr);curs_set(0);
        if (rows_ < 12 || columns_ < 40) { text(0, 0, "Resize to at least 40 x 12. Q quits.");wrefresh(stdscr);return; }
        if (help_) {
            drawHelp();
        } else if (viewer_) drawViewer();
        else if (location_) {
            text(1, 0, "LOCATIONS — choose a mount or enter a path", 2);
            locationTop_ = std::clamp(locationTop_, std::max(0, locationIndex_ - (rows_ - 5) + 1), locationIndex_);
            for (int row = 2, i = locationTop_; row < rows_ - 3 && i < locations_.size(); ++row, ++i) {
                if (i == locationIndex_) fill(1, row, columns_ - 2, 3);
                text(1, row, locations_[i].path + "  " + locations_[i].type, i == locationIndex_ ? 3 : 2, columns_ - 2);
            }
            commands(0, rows_ - 2, "[Arrows] Select  [Enter] Open  [/] Enter path  [F5] Refresh  [Esc] Cancel");
        } else {
            if (other_) {
                pane(side_ ? *other_ : active_, 0, columns_ / 2, side_ == 0);
                pane(side_ ? active_ : *other_, columns_ / 2, columns_ - columns_ / 2, side_ == 1);
            } else pane(active_, 0, columns_, true);
            if (menu_ == 1) commands(0, rows_ - 3, "CTRL: [B]ranch tagged [S]howall tagged [T]ag all [U]ntag all");
            else if (menu_ == 2) commands(0, rows_ - 3, "ALT: [F]ile display [S]ort");
            else if (autoview_) commands(0, rows_ - 3, "AUTOVIEW: [Shift+H] Hex/Text  [Shift+Arrows/PgUp/PgDn] Scroll  [Alt+Left/Right/Home] Width");
            else commands(0, rows_ - 3, "[B]ranch [S]howall [F]ilespec [T]ag [U]ntag [V]iew [L]ocations [Q]uit");
            commands(0, rows_ - 2, "[Enter] Tree/File [F1] Help [F3] Refresh [F4] Menu [F7] Autoview [F8] Split [Tab] Pane");
        }
        if (work_ != Work::None) commands(0, rows_ - 1, QString("%1 %2  [Esc] Stop").arg(QString("|/-\\")[spinner_++ % 4]).arg(work_ == Work::Scan ? "Logging…" : "Reading file…"));
        else if (prompt_ != Prompt::None) {
            const QString label = prompt_ == Prompt::Filespec ? "FILESPEC: " : prompt_ == Prompt::Path ? "LOG path: " : prompt_ == Prompt::Spell ? "SPELL: " : "Quit? Y/N: ";
            const int visible = std::max(1, columns_ - int(label.size()) - 1), start = std::max(0, cursor_ - visible + 1);
            if (!status_.isEmpty()) { fill(0, rows_ - 2, columns_, 1);text(0, rows_ - 2, status_); }
            fill(0, rows_ - 1, columns_, 2);text(0, rows_ - 1, label + input_.mid(start), 2);
            wmove(stdscr, rows_ - 1, int(label.size()) + cursor_ - start);curs_set(2);
        } else if (!help_ && !status_.isEmpty()) text(0, rows_ - 1, status_, 1);
        else if (!help_ && autoview_ && listWidth() == width()) text(0, rows_ - 1, "Autoview needs a wider panel", 1);
        else if (!viewer_ && !help_) text(0, rows_ - 1, QString("LTree terminal prototype  Files %1  Tagged %2  %3").arg(active_.session.files().size()).arg(active_.session.tags.size()).arg(QDateTime::currentDateTime().toString("HH:mm")), 1);
        wrefresh(stdscr);
    }
    void stopAutoview()
    {
        if (autoview_ && autoviewFromTree_) active_.session.returnToTree();
        autoview_ = autoviewFromTree_ = pendingAutoview_ = false;
        if (previewCancel_) previewCancel_->store(true);
        previewPath_.clear();previewed_ = {};
    }
    void invalidateAutoview()
    {
        if (previewCancel_) previewCancel_->store(true);
        previewPath_.clear();previewed_ = {};
    }
    void startAutoview()
    {
        auto &s = active_.session;
        if (s.view == View::Tree && !s.currentDirectory().loaded) { pendingAutoview_ = true;load(false);return; }
        if (!s.currentFile()) { status_ = "No files for Autoview";return; }
        autoviewFromTree_ = s.view == View::Tree;
        if (autoviewFromTree_) s.enter(View::Directory);
        autoview_ = true;previewAuto_ = true;previewHex_ = false;previewTop_ = 0;invalidateAutoview();
    }
    void updateAutoview()
    {
        if (!autoview_ || viewer_ || help_ || location_) return;
        const auto *file = active_.session.currentFile();
        if (active_.session.view == View::Tree || !file) { stopAutoview();status_ = "No files for Autoview";return; }
        if (work_ != Work::None) { if (previewCancel_) previewCancel_->store(true);return; }
        const QString path = file->path;
        if (previewReading_) {
            if (previewPath_ != path) previewCancel_->store(true);
            if (!previewRead_.isFinished()) return;
            auto result = previewRead_.result();previewReading_ = false;
            // Una selección nueva nunca debe mostrar el resultado de la anterior.
            if (previewPath_ == path && !result.cancelled) {
                previewed_ = std::move(result);previewTop_ = 0;
                if (previewAuto_) previewHex_ = previewed_.bytes.contains('\0') && !previewed_.encoding.startsWith("UTF-16") && !previewed_.encoding.startsWith("UTF-32");
                previewed_.error.replace("; use E to open in the editor", "");
            } else previewPath_.clear();
        }
        if (previewPath_ == path) return;
        previewPath_ = path;previewed_ = {};previewTop_ = 0;previewReading_ = true;
        previewCancel_ = std::make_shared<std::atomic_bool>(false);const auto cancel = previewCancel_;
        previewRead_ = QtConcurrent::run([path, cancel] { return readViewDocument(path, cancel); });
    }
    void load(bool recursive, bool preserve = true, const QString &path = {}, bool parent = false)
    {
        invalidateAutoview();cancel_ = std::make_shared<std::atomic_bool>(false);const auto cancel = cancel_;
        const QString target = path.isEmpty() ? active_.session.directory : path;
        parentLoad_ = parent;preserveTags_ = preserve;work_ = Work::Scan;
        scan_ = QtConcurrent::run([target, recursive, cancel] { return scanDirectories(target, recursive, cancel); });
    }
    void complete()
    {
        if (work_ == Work::Scan && scan_.isFinished()) {
            const auto result = scan_.result();work_ = Work::None;
            if (parentLoad_) active_.session.expandToParent(result);else active_.session.apply(result, preserveTags_);
            if (other_) other_->session.apply(result, true);
            status_ = result.cancelled ? "Logging cancelled" : QString();
            for (const auto &dir : result.directories) if (!dir.error.isEmpty()) { status_ = dir.error;break; }
            if (pendingAutoview_) {
                pendingAutoview_ = false;
                if (!result.cancelled && active_.session.currentDirectory().loaded) startAutoview();
            }
        } else if (work_ == Work::Document && document_.isFinished()) {
            viewed_ = document_.result();work_ = Work::None;
            if (viewed_.cancelled || !viewed_.error.isEmpty()) status_ = viewed_.cancelled ? "Reading cancelled" : viewed_.error;
            else { viewer_ = true;hex_ = viewed_.bytes.contains('\0');viewerTop_ = 0; }
        }
    }
    void openPrompt(Prompt prompt, const QString &input = {}) { prompt_ = prompt;input_ = input;cursor_ = int(input.size());historyIndex_ = -1;status_.clear(); }
    void selectRoot(QString path)
    {
        if (path == "~") path = QDir::homePath();else if (path.startsWith("~/")) path = QDir::homePath() + path.mid(1);
        const QFileInfo info(QDir(active_.session.directory).absoluteFilePath(path));
        if (!info.isDir() || !info.isReadable()) { status_ = "Directory unavailable";return; }
        stopAutoview();active_ = Pane(info.canonicalFilePath());location_ = false;prompt_ = Prompt::None;load(false);
    }
    void promptKey(int key, bool literal)
    {
        if (key == 27 || key == 3) { prompt_ = Prompt::None;return; }
        if (prompt_ == Prompt::Quit) {
            if (key == 'y' || key == 'Y' || key == 10 || key == 13 || key == KEY_ENTER) quit_ = true;
            else if (key == 'n' || key == 'N') prompt_ = Prompt::None;
            return;
        }
        if (literal && key >= 32 && key != 127) {
            if (input_.size() < 1024) {
                const char32_t cp = char32_t(key);const QString added = QString::fromUcs4(&cp, 1);
                input_.insert(cursor_, added);cursor_ += int(added.size());
            }
            return;
        }
        if (key == 10 || key == 13 || key == KEY_ENTER) {
            if (prompt_ == Prompt::Filespec) {
                QString error;if (!active_.session.filespec.set(input_, &error)) { status_ = error;return; }
                if (!input_.isEmpty()) { filespecHistory_.removeAll(input_);filespecHistory_.prepend(input_);if (filespecHistory_.size() > 64) filespecHistory_.removeLast(); }
                active_.session.rebuild();prompt_ = Prompt::None;
            } else if (prompt_ == Prompt::Path) selectRoot(input_);
            else { if (!active_.session.spell(input_)) status_ = "No match";prompt_ = Prompt::None; }
        } else if (key == KEY_LEFT) cursor_ = std::max(0, cursor_ - 1);
        else if (key == KEY_RIGHT) cursor_ = std::min(int(input_.size()), cursor_ + 1);
        else if (key == KEY_HOME || key == 1) cursor_ = 0;
        else if (key == KEY_END || key == 5) cursor_ = int(input_.size());
        else if (key == 21) { input_.clear();cursor_ = 0; }
        else if ((key == KEY_UP || key == KEY_DOWN || key == KEY_F(3)) && prompt_ == Prompt::Filespec && !filespecHistory_.isEmpty()) {
            historyIndex_ = key == KEY_F(3) ? 0 : std::clamp(historyIndex_ + (key == KEY_DOWN ? -1 : 1), 0, int(filespecHistory_.size()) - 1);
            input_ = filespecHistory_[historyIndex_];cursor_ = int(input_.size());
        } else if (key == KEY_BACKSPACE || key == 127 || key == 8) { if (cursor_ > 0) input_.remove(--cursor_, 1); }
        else if (key == KEY_DC) input_.remove(cursor_, 1);
    }
    void mouse()
    {
        MEVENT event;if (getmouse(&event) != OK) return;
        if (other_ && (event.x >= columns_ / 2 ? 1 : 0) != side_) { stopAutoview();std::swap(active_, *other_);side_ = 1 - side_; }
        const int x = event.x - (other_ && side_ ? columns_ / 2 : 0);auto &s = active_.session;
        const bool inPreview = autoview_ && listWidth() < width() && x >= listWidth() - 1;
        if (event.bstate & BUTTON4_PRESSED) { if (inPreview) previewTop_ -= 3;else s.move(-3);return; }
#ifdef BUTTON5_PRESSED
        if (event.bstate & BUTTON5_PRESSED) { if (inPreview) previewTop_ += 3;else s.move(3);return; }
#endif
        if (inPreview || x < 1 || x >= width() - 1 || event.y < 2 || event.y >= end()) return;
        if (s.view == View::Tree && event.y < divider()) {
            const int index = active_.treeTop + event.y - 2;
            if (index < s.tree().size()) s.selectDirectory(s.tree()[index].path);
        } else if (s.view != View::Tree) {
            const auto grid = layout(2, listWidth());const int column = (x - 1) / grid.stride;
            const int index = active_.fileTop + column * grid.rows + event.y - 2;
            if (column < grid.columns && (x - 1) % grid.stride < grid.width && index < s.files().size()) s.fileIndex = index;
        }
    }
    void key(int value, bool alt, bool literal)
    {
        if (!literal && value == KEY_RESIZE) return;
        if (rows_ < 12 || columns_ < 40) { if (value == 'q' || value == 'Q' || value == 3) quit_ = true;return; }
        if (work_ != Work::None) { if (value == 27 || value == 3) { cancel_->store(true);pendingAutoview_ = false; }return; }
        status_.clear();
        if (prompt_ != Prompt::None) { promptKey(value, literal);return; }
        if (literal && value >= KEY_MIN) return;
        const int letter = value >= 'A' && value <= 'Z' ? value + 'a' - 'A' : value;
        if (help_) { helpKey(value);return; }
        if (viewer_) {
            if(value == KEY_F(1)){helpDocument_.open(HelpTopic::TerminalView);help_ = true;return;}
            const int page = rows_ - 4, count = hex_ ? int((viewed_.bytes.size() + bytesPerRow(columns_) - 1) / bytesPerRow(columns_)) : int(viewed_.lines.size());
            if (value == 27 || value == 10 || value == KEY_ENTER || letter == 'q') viewer_ = false;
            else if (letter == 'h') { hex_ = !hex_;viewerTop_ = 0; }
            else if (value == KEY_UP) --viewerTop_;else if (value == KEY_DOWN) ++viewerTop_;
            else if (value == KEY_PPAGE) viewerTop_ -= page;else if (value == KEY_NPAGE) viewerTop_ += page;
            else if (value == KEY_HOME) viewerTop_ = 0;else if (value == KEY_END) viewerTop_ = std::max(0, count - page);
            return;
        }
        if (location_) {
            if (value == 27) location_ = false;
            else if (value == KEY_UP) locationIndex_ = std::max(0, locationIndex_ - 1);
            else if (value == KEY_DOWN) locationIndex_ = std::min(std::max(0, int(locations_.size()) - 1), locationIndex_ + 1);
            else if (value == '/' || letter == 'p') openPrompt(Prompt::Path, value == '/' ? "/" : QString());
            else if (value == KEY_F(5)) locations_ = mountedLocations();
            else if ((value == 10 || value == KEY_ENTER) && !locations_.isEmpty()) selectRoot(locations_[locationIndex_].path);
            return;
        }
        if (value == AltLeft || value == AltRight || value == AltHome) {
            alt = true;value = value == AltLeft ? KEY_LEFT : value == AltRight ? KEY_RIGHT : KEY_HOME;
        }
        if (value == 27 && menu_) { menu_ = 0;return; }
        if (autoview_) {
            if (!alt && (value == KEY_F(7) || value == 10 || value == 13 || value == KEY_ENTER || value == 27)) { menu_ = 0;stopAutoview();return; }
            if (!alt && (value == 'H' || value == 'A')) {
                menu_ = 0;previewHex_ = value == 'H' && !previewHex_;previewAuto_ = false;previewTop_ = 0;return;
            }
            const int previewWidth = std::max(1, width() - listWidth() - 1);
            const int page = std::max(1, end() - 4);
            const int count = previewHex_ ? int((previewed_.bytes.size() + bytesPerRow(previewWidth) - 1) / bytesPerRow(previewWidth)) : int(previewed_.lines.size());
            if (!alt && value == KEY_SR) { menu_ = 0;--previewTop_;return; }
            if (!alt && value == KEY_SF) { menu_ = 0;++previewTop_;return; }
            if (!alt && value == KEY_SPREVIOUS) { menu_ = 0;previewTop_ -= page;return; }
            if (!alt && value == KEY_SNEXT) { menu_ = 0;previewTop_ += page;return; }
            if (!alt && value == KEY_SHOME) { menu_ = 0;previewTop_ = 0;return; }
            if (!alt && value == KEY_SEND) { menu_ = 0;previewTop_ = std::max(0, count - page);return; }
            if (alt && (value == KEY_LEFT || value == KEY_RIGHT || value == KEY_HOME)) {
                menu_ = 0;autoviewPercent_ = value == KEY_HOME ? 40 : std::clamp(autoviewPercent_ + (value == KEY_RIGHT ? 5 : -5), 20, 75);return;
            }
        }
        if (value == KEY_F(4)) { menu_ = (menu_ + 1) % 3;return; }
        const bool control = menu_ == 1 && (letter == 'b' || letter == 's' || letter == 't' || letter == 'u');
        alt = alt || (menu_ == 2 && (letter == 'f' || letter == 's'));menu_ = 0;
        auto &s = active_.session;const bool tree = s.view == View::Tree;
        if (value == KEY_MOUSE) { mouse();return; }
        if (alt && letter == 'f') { display_ = Display((int(display_) + 1) % 4);status_ = "File display: " + QStringList{"Name", "Name, size and attributes", "Details", "Long name"}[int(display_)]; }
        else if (alt && letter == 's') { s.sort.key = SortKey((int(s.sort.key) + 1) % 10);s.rebuild();status_ = "Sort: " + sortLabel(s.sort); }
        else if (alt) status_ = "Not available in the terminal prototype; F1 shows supported commands";
        else if (letter == 'q' || value == 3) openPrompt(Prompt::Quit);
        else if (value == KEY_F(1)) { helpDocument_.open(HelpTopic::Terminal);help_ = true; }
        else if (value == KEY_F(7)) startAutoview();
        else if (letter == 'f') openPrompt(Prompt::Filespec);
        else if (letter == '|') openPrompt(Prompt::Spell);
        else if (letter == 'l') { locations_ = mountedLocations();locationIndex_ = locationTop_ = 0;location_ = true; }
        else if (value == KEY_F(8)) { stopAutoview();if (other_) { other_.reset();side_ = 0; }else { other_ = active_;side_ = 0; } }
        else if (value == 9 && other_) { stopAutoview();if (other_->session.root == s.root) other_->session.syncLoggingFrom(s);std::swap(active_, *other_);side_ = 1 - side_; }
        else if (value == 10 || value == 13 || value == KEY_ENTER) {
            if (tree) { if (!s.currentDirectory().loaded) load(false);else s.enter(View::Directory); }
            else s.returnToTree();
        } else if (value == 27) { if (!tree) s.returnToTree(); }
        else if (letter == 'b' || letter == 's' || (tree && (value == 2 || value == 19))) s.enter(letter == 'b' || value == 2 ? View::Branch : View::Showall, control || value == 2 || value == 19);
        else if (letter == 't' || value == 20) s.setTag(true, control || value == 20);
        else if (letter == 'u' || value == 21) s.setTag(false, control || value == 21);
        else if (value == KEY_F(3) || (tree && (letter == '+' || letter == '=' || letter == '*'))) load(letter == '*');
        else if (tree && (letter == '-' || letter == '_')) s.unlog();
        else if (tree && (value == KEY_F(5) || value == KEY_F(6))) s.toggleCollapse(value == KEY_F(5));
        else if (value == KEY_BACKSPACE || value == 127 || value == 8) {
            s.returnToTree();if (s.directory != s.root) s.parent();else if (s.root != "/") load(false, true, QFileInfo(s.root).absolutePath(), true);
        } else if (value == KEY_UP) s.move(-1);else if (value == KEY_DOWN || value == ' ') s.move(1);
        else if (value == KEY_HOME) s.first();else if (value == KEY_END) s.last();
        else if (value == KEY_PPAGE || value == KEY_NPAGE) s.move((value == KEY_PPAGE ? -1 : 1) * (tree ? divider() - 2 : layout(2, listWidth()).capacity()));
        else if (value == KEY_LEFT) { if (tree) s.parent(false);else s.move(-layout(2, listWidth()).rows); }
        else if (value == KEY_RIGHT) s.move(tree ? 1 : layout(2, listWidth()).rows);
        else if ((letter == 'v' || letter == 'o') && !tree && s.currentFile()) {
            viewerPath_ = s.currentFile()->path;cancel_ = std::make_shared<std::atomic_bool>(false);const auto path = viewerPath_;const auto cancel = cancel_;
            work_ = Work::Document;document_ = QtConcurrent::run([path, cancel] { return readViewDocument(path, cancel); });
        } else status_ = "Not available in the terminal prototype; F1 shows supported commands";
    }
public:
    TerminalUI(const QString &root, bool sizes, bool colors) : active_(root), sizes_(sizes), colors_(colors) { load(false); }
    ~TerminalUI() {
        if (cancel_) cancel_->store(true);
        if (previewCancel_) previewCancel_->store(true);
        scan_.waitForFinished();document_.waitForFinished();previewRead_.waitForFinished();
    }
    int run()
    {
        while (!quit_ && !stopped) {
            QCoreApplication::processEvents();complete();updateAutoview();draw();
            wint_t value;const int result = wget_wch(stdscr, &value);if (result == ERR) continue;
            bool alt = false, literal = result == OK;
            if (result == OK && value == 27) {
                wtimeout(stdscr, 30);wint_t next;const int second = wget_wch(stdscr, &next);wtimeout(stdscr, 100);
                if (second != ERR) { value = next;alt = true;literal = second == OK; }
            }
            key(int(value), alt, literal);
        }
        return 0;
    }
};
}
int runTerminal(const QString &root, bool treeSizes)
{
    if (!::isatty(STDIN_FILENO) || !::isatty(STDOUT_FILENO)) { std::fprintf(stderr, "Terminal mode requires an interactive terminal on stdin and stdout.\n");return 2; }
    std::setlocale(LC_CTYPE, "");
    SCREEN *screen = newterm(nullptr, stdout, stdin);
    if (!screen) { std::fprintf(stderr, "Cannot initialize the terminal; check TERM and terminfo.\n");return 2; }
    set_term(screen);raw();noecho();keypad(stdscr, true);wtimeout(stdscr, 100);set_escdelay(25);
    const bool colors = has_colors() && start_color() == OK;
    if (colors) {
        int blue = COLOR_BLUE, yellow = COLOR_YELLOW, cyan = COLOR_CYAN;
        int black = COLOR_BLACK, grey = COLOR_WHITE;
        if (COLORS >= 0x1000000) {
            blue = 0x000080;yellow = 0xffff00;cyan = 0x00ffff;grey = 0xc0c0c0;
        } else if (COLORS >= 256) {
            // Usar el cubo y grises extendidos, sin redefinir la paleta del usuario.
            blue = 18;yellow = 226;cyan = 51;black = 16;grey = 250;
        } else if (COLORS >= 16) {
            yellow = COLOR_YELLOW + 8;cyan = COLOR_CYAN + 8;
        }
        init_extended_pair(1, cyan, blue);init_extended_pair(2, yellow, blue);
        init_extended_pair(3, black, grey);wbkgd(stdscr, COLOR_PAIR(2));
    }
    // Normalizar teclas modificadas habituales de Kitty/xterm.
    define_key("\x1b[1;2A", KEY_SR);define_key("\x1b[1;2B", KEY_SF);
    define_key("\x1b[5;2~", KEY_SPREVIOUS);define_key("\x1b[6;2~", KEY_SNEXT);
    define_key("\x1b[1;2H", KEY_SHOME);define_key("\x1b[1;2F", KEY_SEND);
    define_key("\x1b[1;3D", AltLeft);define_key("\x1b[1;3C", AltRight);define_key("\x1b[1;3H", AltHome);
    mousemask(BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED | BUTTON4_PRESSED
#ifdef BUTTON5_PRESSED
        | BUTTON5_PRESSED
#endif
        , nullptr);
    stopped = 0;
    const auto previousTerm = std::signal(SIGTERM, stopTerminal), previousInt = std::signal(SIGINT, stopTerminal), previousHup = std::signal(SIGHUP, stopTerminal);
    int result;
    { TerminalUI ui(root, treeSizes, colors);result = ui.run(); }
    endwin();delscreen(screen);
    std::signal(SIGTERM, previousTerm);std::signal(SIGINT, previousInt);std::signal(SIGHUP, previousHup);
    return result;
}
}
