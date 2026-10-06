#include "terminal/terminalui.h"
#include "terminal/editor.h"
#include "terminal/keyboard.h"
#include "core/session.h"
#include "core/helpdocument.h"
#include "fs/mounts.h"
#include "fs/viewdocument.h"
#include "fs/hexedit.h"
#include "fs/stamp.h"
#include "fs/copyfiles.h"
#include "fs/deletedirectory.h"
#include "fs/makedirectory.h"
#include "fs/rename.h"
#include "fs/symlink.h"
#include "fs/graft.h"
#include "fs/prune.h"
#include "fs/search.h"
#include "fs/archive.h"
#include "fs/directorycompare.h"
#include "fs/filecompare.h"
#include "fs/systemstats.h"
#include "ui/doschars.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QMutex>
#include <QMutexLocker>
#include <iconv.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QProcess>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <csignal>
#include <clocale>
#include <cstdio>
#include <cwchar>
#include <optional>
#include <utility>
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
    explicit Pane(const QString &path = QString("/")) : session(path) {}
};
enum class Display { Name, SizeAttributes, Details, LongName };
enum class Prompt { None, Filespec, Path, Spell, Quit, Permissions, Stamp, MetadataReview };
enum class Work { None, Scan, Document, Metadata };
struct MetadataResult {
    int changed = 0, unchanged = 0, failed = 0;
    bool cancelled = false;
    QString error;
    ScanResult scan;
};
enum TerminalKey { AltLeft = 0x2000, AltRight, AltHome };
struct Layout { int rows, columns, stride, width; int capacity() const { return rows * columns; } };

class TerminalUI {
    TerminalKeyboard keyboard_;
    HelpDocument helpDocument_{true};
    Pane active_;
    std::optional<Pane> other_;
    int side_ = 0, rows_ = 0, columns_ = 0, menu_ = 0, spinner_ = 0;
    Display display_ = Display::Details;
    Prompt prompt_ = Prompt::None;
    QString input_, status_, viewerPath_, editorNotice_;
    bool reloadEditedViewer_ = false;
    QStringList filespecHistory_;
    int cursor_ = 0, historyIndex_ = -1, locationIndex_ = 0, locationTop_ = 0, viewerTop_ = 0;
    QVector<MountPoint> locations_;
    bool help_ = false, location_ = false, viewer_ = false, hex_ = false, quit_ = false;
    bool sizes_ = false, colors_ = false, parentLoad_ = false, preserveTags_ = true;
    Work work_ = Work::None;
    Cancellation cancel_;
    QFuture<ScanResult> scan_;
    QFuture<ViewDocument> document_;
    QFuture<MetadataResult> metadata_;
    QVector<FileMetadata> metadataTargets_;
    QVector<quint32> permissionModes_;
    StampPlan stampPlan_;
    StampField stampField_ = StampField::Written;
    StampMode stampMode_ = StampMode::Set;
    bool stamping_ = false, metadataTagged_ = false, inputSelected_ = false;
    QStringList stampHistory_;
    ViewDocument viewed_;
    HexSnapshot documentSnapshot_, viewerSnapshot_;
    bool documentEdit_ = false, editing_ = false, savePrompt_ = false, asciiEdit_ = false;
    QByteArray editPage_;
    qsizetype editOffset_ = 0, editCursor_ = 0, editCapacity_ = 0;
    int editNibble_ = 0, afterSave_ = 0;
    std::optional<bool> documentMode_;
    bool autoview_ = false, autoviewFromTree_ = false, pendingAutoview_ = false;
    bool previewReading_ = false, previewHex_ = false, previewAuto_ = true;
    int previewTop_ = 0, autoviewPercent_ = 40;
    QString previewPath_;
    ViewDocument previewed_;
    Cancellation previewCancel_;
    QFuture<ViewDocument> previewRead_;

    #include "terminal/terminalcommands.inc"

    int end() const { return rows_ - 4; }
    int divider() const { return 2 + (end() - 2) * 2 / 3; }
    int width() const { return other_ ? (side_ ? columns_ - columns_ / 2 : columns_ / 2) : columns_; }
    int listWidth(const Pane &pane, int w, bool focused) const
    {
        return autoview_ && focused && pane.session.view != View::Tree && w >= 36 ?
            std::clamp(w * autoviewPercent_ / 100, 16, w - 20) : w;
    }
    int listWidth() const { const int w=width();return listWidth(active_,statisticsVisible(w,true)?w-22:w,true); }
    int bytesPerRow(int w) const { return w >= 78 ? 16 : w >= 46 ? 8 : w >= 30 ? 4 : 1; }
    int ink(int pair) const { return colors_ ? COLOR_PAIR((viewer_||previewDrawing_)&&!editing_&&viewerBackground_&&pair!=3&&pair<=10?20+pair+10*(viewerBackground_-1):pair) : pair == 3 ? A_REVERSE : A_NORMAL; }
    void text(int x, int y, const QString &value, int pair = 1, int limit = -1)
    {
        if (y < 0 || y >= rows_ || x < 0 || x >= columns_) return;
        limit = std::min(limit < 0 ? columns_ - x : limit, columns_ - x);
        wattrset(stdscr, ink(pair));
        int count = 0;
        for (char32_t cp : value.toUcs4()) {
            if (count >= limit) break;
            wchar_t glyph = wchar_t(cp);
            int cells = ::wcwidth(glyph);
            if (cells < 0 || cp < 32 || (cp >= 127 && cp < 160)) { glyph = L'?';cells = 1; }
            if (count + cells > limit) break;
            mvwaddnwstr(stdscr, y, x + count, &glyph, 1);count += cells;
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
            const int color = current && focused ? 3 : fileInk(file.name);
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
                const int attrsWidth = display_ == Display::Details && grid.width >= 64 ? 11 : 6;
                right -= attrsWidth + 1;text(right, y, fileAttributeText(file.attributes, file.symlink, attrsWidth), color, attrsWidth);
                right -= 12;text(right, y, QString::number(file.size).rightJustified(11), color, 11);
            }
            const int extWidth = std::min(display_ == Display::Details && grid.width >= 54 ? extensionWidth_ : 4, std::max(1, right - left - 3));right -= extWidth + 1;
            text(left + 1, y, base, color, std::max(1, right - left - 2));text(right, y, extension, color, extWidth);
        }
    }
    void pane(Pane &pane, int x, int w, bool focused)
    {
        const int area=statisticsVisible(w,focused)?w-22:w;
        const int list = listWidth(pane, area, focused);visible(pane, list);const auto &s = pane.session;
        const QString path = s.view != View::Tree && s.currentFile() ? QFileInfo(s.currentFile()->path).absolutePath() : s.directory;
        text(x + 1, 0, pathLabel(path, w - 2), 1, w - 2);
        for (int y = 1; y <= end(); ++y) { text(x, y, "│");text(x + w - 1, y, "│"); }
        text(x, 1, "┌" + QString(w - 2, QChar(0x2500)) + "┐", 1, w);
        text(x, end(), "└" + QString(w - 2, QChar(0x2500)) + "┘", 1, w);
        const QString mode = s.view == View::Tree ? "TREE" : s.view == View::Directory ? "DIR" : s.view == View::Branch ? "BRANCH" : s.view==View::Global?"GLOBAL":"SHOWALL";
        text(x + 1, 1, "<" + mode + ": " + (s.tagsOnly ? "♦ " : "") + s.filespec.text() + ">", focused ? 2 : 1, w - 2);
        if (s.view == View::Tree) {
            const auto *sizes = sizes_ && area > 35 ? &s.branchSizes() : nullptr;
            for (int row = 2, i = pane.treeTop; row < divider() && i < s.tree().size(); ++row, ++i) {
                const auto &item = s.tree()[i];const auto &dir = s.directories()[item.path];const bool current = item.path == s.directory;
                if (current && focused) fill(x + 1, row, area - 2, 3);
                text(x + 1, row, QString(dir.loaded ? " " : "+") + item.prefix + item.name, current && focused ? 3 : 2, area - (sizes ? 17 : 2));
                if (sizes) text(x + area - 15, row, sizes->value(item.path).label(), current && focused ? 3 : 2, 14);
                if (current && !focused) text(x + 1, row, "►", 3, 1);
            }
            text(x, divider(), "├" + QString(w - 2, QChar(0x2500)) + "┤", 1, w);
            files(pane, x, divider() + 1, area, focused);
        } else {
            files(pane, x, 2, list, focused);
            if (list < area) {
                for (int y = 2; y < end(); ++y) text(x + list - 1, y, "│");
                drawAutoview(x + list, area - list - 1);
            }
        }
        if(area<w)drawSidebar(pane,x+area-1,w-area);
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
                const int offset = index * bytes;auto chunk = doc.bytes.mid(offset, bytes);
                if (editing_ && &doc == &viewed_) {
                    for (qsizetype i = 0; i < chunk.size(); ++i) {
                        const qsizetype at = offset + i - editOffset_;
                        if (at >= 0 && at < editPage_.size()) chunk[i] = editPage_[at];
                    }
                }
                QString hex, ascii;
                for (const unsigned char value : chunk) { hex += QString("%1 ").arg(value, 2, 16, QChar('0')).toUpper();ascii += viewerMask_&&(value<32||value>127)?QString("."):terminalByteGlyph(value); }
                text(x, y + row, QString("%1").arg(offset, 8, 16, QChar('0')).toUpper(), 1, w);text(x + 10, y + row, hex, 2, w - 10);
                if (w >= 10 + bytes * 4) text(x + 10 + bytes * 3, y + row, ascii, 2, bytes);
                if (editing_ && &doc == &viewed_ && editOffset_ + editCursor_ >= offset && editOffset_ + editCursor_ < offset + chunk.size()) {
                    const int at = int(editOffset_ + editCursor_ - offset);
                    text(x + 10 + at * 3 + editNibble_, y + row, hex.mid(at * 3 + editNibble_, 1), asciiEdit_ ? 2 : 3, 1);
                    if (w >= 10 + bytes * 4) text(x + 10 + bytes * 3 + at, y + row, ascii.mid(at, 1), asciiEdit_ ? 3 : 2, 1);
                }
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
            drawStyledPreview(x,w);
            text(x, end() - 1, QString("%1 bytes  %2").arg(previewed_.bytes.size()).arg(previewed_.encoding), 1, w);
        }
    }
    void drawViewer()
    {
        if (!editing_) { drawExtendedViewer();return; }
        text(0, 0, pathLabel(viewerPath_, columns_), 1, columns_);
        if (editing_) {
            const int row = int((editOffset_ + editCursor_) / bytesPerRow(columns_));
            viewerTop_ = std::clamp(viewerTop_, std::max(0, row - (rows_ - 4) + 1), row);
        }
        drawDocument(0, 1, rows_ - 4, columns_, viewed_, hex_, viewerTop_);
        if (editing_) {
            if (savePrompt_) commands(0, rows_ - 2, "Save hex changes? [Y] Yes  [N/Esc] Keep editing");
            else {
                commands(0, rows_ - 3, "[Arrows/Home/End] Move [PgUp/PgDn] Page [Tab] Hex/ASCII");
                commands(0, rows_ - 2, "[Enter] Finish [Ctrl+S] Save [F8] Undo page [Esc] Discard");
            }
            text(0, rows_ - 1, QString("HEX EDIT %1  Offset %2  %3").arg(asciiEdit_ ? "ASCII" : "HEX").arg(editOffset_ + editCursor_, 8, 16, QChar('0')).arg(editPage_ == viewed_.bytes.mid(editOffset_, editPage_.size()) ? "Unchanged" : "Modified"));
        }
    }
    void draw()
    {
        getmaxyx(stdscr, rows_, columns_);werase(stdscr);curs_set(0);
        if (rows_ < 12 || columns_ < 40) { text(0, 0, "Resize to at least 40 x 12. Q quits.");wrefresh(stdscr);return; }
        if (help_) {
            drawHelp();
        } else if (viewer_) drawViewer();
        else if (comparing_) drawComparison();
        else if (inArchive_) drawArchive();
        else if (location_) {
            text(1, 0, avail_?"AVAILABLE SPACE — choose a mount or enter a path":"LOCATIONS — choose a mount or enter a path", 2);
            locationTop_ = std::clamp(locationTop_, std::max(0, locationIndex_ - (rows_ - 5) + 1), locationIndex_);
            for (int row = 2, i = locationTop_; row < rows_ - 3 && i < locations_.size(); ++row, ++i) {
                if (i == locationIndex_) fill(1, row, columns_ - 2, 3);
                text(1, row, locations_[i].path + "  " + locations_[i].type, i == locationIndex_ ? 3 : 2, columns_ - 2);
            }
            if(avail_&&!locations_.isEmpty()){auto stats=systemStatistics(locations_[locationIndex_].path);text(0,rows_-3,QString("Capacity %1  Available %2 bytes  %3").arg(stats.capacity).arg(stats.available).arg(stats.filesystem),2);}
            commands(0, rows_ - 2, "[Arrows] Select  [Enter] Open  [/] Enter path  [F5] Refresh  [Esc] Cancel");
        } else {
            if (other_) {
                pane(side_ ? *other_ : active_, 0, columns_ / 2, side_ == 0);
                pane(side_ ? active_ : *other_, columns_ / 2, columns_ - columns_ / 2, side_ == 1);
            } else pane(active_, 0, columns_, true);
            if (keyboard_.menu(menu_) == 1) commands(0, rows_ - 3, "CTRL: [A]ttr [C]opy [D]el [M]ove [N]ew date [R]ename [S]earch [J]FC [I]nvert [V]iew [T]ag [U]ntag");
            else if (keyboard_.menu(menu_) == 2) commands(0, rows_ - 3, "ALT: [A] Permissions [C]opy paths/Compare [M]ove paths [G]raft [P]rune [I]nfo [K] Filter [F] Display [S]ort");
            else if (autoview_) commands(0, rows_ - 3, "AUTOVIEW: [Shift+H] Hex/Text  [Shift+Arrows/PgUp/PgDn] Scroll  [Alt+Left/Right/Home] Width");
            else commands(0, rows_ - 3, active_.session.view == View::Tree ?
                "[A]vail [B]ranch [C]ompare [D]el [F]ilespec [G]lobal [H] Link [M]ake [R]ename [S]howall [X] Shell [?] Stats" :
                "[A]ttr [C]opy [D]el [E]dit [F]ilespec [J]FC [M]ove [N]ew date [R]ename [T]ag [U]ntag [V]iew [X] Shell");
            commands(0, rows_ - 2, "[Enter] Tree/File [F1] Help [F3] Refresh [F4] Menu [F7] Autoview [F8] Split [Tab] Pane [Q] Quit");
        }
        if (!help_ && !viewer_ && info_) drawInfoBox();
        if (work_ != Work::None) {
            fill(0, rows_ - 1, columns_, 1);
            commands(0, rows_ - 1, QString("%1 %2  [Esc] Stop").arg(QString("|/-\\")[spinner_++ % 4]).arg(work_ == Work::Scan ? "Logging…" : work_ == Work::Document ? "Reading file…" : "Changing metadata…"));
        }
        else if (prompt_ == Prompt::Permissions || prompt_ == Prompt::Stamp || prompt_ == Prompt::MetadataReview) drawMetadataPrompt();
        else if (prompt_ != Prompt::None) {
            const QString label = prompt_ == Prompt::Filespec ? "FILESPEC: " : prompt_ == Prompt::Path ? "LOG path: " : prompt_ == Prompt::Spell ? "SPELL: " : "Quit? Y/N: ";
            const int visible = std::max(1, columns_ - int(label.size()) - 1), start = std::max(0, cursor_ - visible + 1);
            if (!status_.isEmpty()) { fill(0, rows_ - 2, columns_, 1);text(0, rows_ - 2, status_); }
            fill(0, rows_ - 1, columns_, 2);text(0, rows_ - 1, label + input_.mid(start), 2);
            wmove(stdscr, rows_ - 1, int(label.size()) + cursor_ - start);curs_set(2);
        } else if (!help_ && !status_.isEmpty()) { fill(0, rows_ - 1, columns_, 1);text(0, rows_ - 1, status_, 1); }
        else if (!help_ && autoview_ && listWidth() == width()) text(0, rows_ - 1, "Autoview needs a wider panel", 1);
        else if (!viewer_ && !help_) text(0, rows_ - 1, QString("LTree Commander %1  Files %2  Tagged %3  %4").arg(QCoreApplication::applicationVersion()).arg(active_.session.files().size()).arg(active_.session.tags.size()).arg(QDateTime::currentDateTime().toString("HH:mm:ss")), 1);
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
                previewHit_= -1;previewHitLength_=0;
                previewed_ = std::move(result);previewTop_ = 0;
                if (previewAuto_) previewHex_ = terminalBinaryData(previewed_.bytes);
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
        if (work_ == Work::Metadata && metadata_.isFinished()) {
            const auto result = metadata_.result();work_ = Work::None;
            if (!result.scan.directories.isEmpty()) {
                active_.session.apply(result.scan, true);
                if (other_) other_->session.apply(result.scan, true);
            }
            invalidateAutoview();
            status_ = QString("%1 %2: %3/%4 changed; %5 unchanged; %6 failed")
                .arg(stamping_ ? "Stamp" : "Permissions", result.cancelled ? "cancelled" : "completed")
                .arg(result.changed).arg(metadataTargets_.size()).arg(result.unchanged).arg(result.failed);
            if (!result.error.isEmpty()) status_ += "; " + result.error;
            for (const auto &dir : result.scan.directories) if (!dir.error.isEmpty()) { status_ += "; read error: " + dir.error;break; }
        } else if (work_ == Work::Scan && scan_.isFinished()) {
            const auto result = scan_.result();work_ = Work::None;
            if (parentLoad_) active_.session.expandToParent(result);else active_.session.apply(result, preserveTags_);
            if (other_) other_->session.apply(result, true);
            status_ = result.cancelled ? "Logging cancelled" : std::exchange(editorNotice_, QString());
            for (const auto &dir : result.directories) if (!dir.error.isEmpty()) { status_ = dir.error;break; }
            if (reloadEditedViewer_) {
                reloadEditedViewer_ = false;
                if (!result.cancelled) readDocument(viewerPath_);
            }
            if (pendingAutoview_) {
                pendingAutoview_ = false;
                if (!result.cancelled && active_.session.currentDirectory().loaded) startAutoview();
            }
        } else if (work_ == Work::Document && document_.isFinished()) {
            auto result=document_.result();work_ = Work::None;
            if(documentPreview_){documentPreview_=false;previewed_=std::move(result);previewTop_=documentTop_;previewPath_=active_.session.currentFile()?active_.session.currentFile()->path:QString();previewAuto_=false;return;}
            viewed_=std::move(result);
            if (viewed_.cancelled || !viewed_.error.isEmpty()) status_ = viewed_.cancelled ? "Reading cancelled" : viewed_.error;
            else {
                viewer_ = true;hex_ = documentMode_.value_or(terminalBinaryData(viewed_.bytes));viewerTop_ = 0;viewerSnapshot_ = documentSnapshot_;
                if(documentReload_){viewerTop_=documentTop_;rebuildViewerLines();}else resetViewer();if (documentEdit_) beginHexEdit();
            }
            documentEdit_ = false;documentMode_.reset();
        }
    }
    void beginHexEdit()
    {
        if (viewed_.bytes.isEmpty()) { status_ = "An empty file has no bytes to edit";return; }
        if (!viewerSnapshot_.valid) { status_ = "Only regular files with safe paths can be edited";return; }
        if (hexSnapshot(viewerPath_) != viewerSnapshot_) { status_ = "The file changed outside the viewer. Reload before editing";return; }
        if (!hex_) viewerTop_ = 0;
        viewerTop_ = std::clamp(viewerTop_, 0, int((viewed_.bytes.size() - 1) / bytesPerRow(columns_)));
        hex_ = true;editing_ = true;savePrompt_ = asciiEdit_ = false;editNibble_ = afterSave_ = 0;
        editOffset_ = qsizetype(viewerTop_) * bytesPerRow(columns_);
        editCapacity_ = qsizetype(rows_ - 4) * bytesPerRow(columns_);
        editPage_ = viewed_.bytes.mid(editOffset_, editCapacity_);editCursor_ = 0;
    }
    void finishHexPage(bool save)
    {
        if (save && editPage_ != viewed_.bytes.mid(editOffset_, editPage_.size())) {
            const QString error = saveHexPage(viewerPath_, viewerSnapshot_, viewed_.bytes, editOffset_, editPage_);
            if (!error.isEmpty()) { status_ = error;savePrompt_ = false;return; }
            viewed_.bytes.replace(editOffset_, editPage_.size(), editPage_);viewerSnapshot_ = hexSnapshot(viewerPath_);
            load(false, true, QFileInfo(viewerPath_).absolutePath());editorNotice_ = "Hex changes saved";
        }
        savePrompt_ = false;
        if (afterSave_ == -1 || afterSave_ == 1) {
            editOffset_ = std::max(qsizetype(0), editOffset_ + afterSave_ * editCapacity_);
            viewerTop_ = int(editOffset_ / bytesPerRow(columns_));editCursor_ = editNibble_ = 0;
        } else if (afterSave_ != 2) editing_ = false;
        editPage_ = editing_ ? viewed_.bytes.mid(editOffset_, editCapacity_) : QByteArray();afterSave_ = 0;
    }
    void hexEditKey(int key, bool literal, bool alt)
    {
        if (savePrompt_) {
            if (key == 'y' || key == 'Y') finishHexPage(true);
            else if (key == 'n' || key == 'N') { savePrompt_ = false;afterSave_ = 0; }
            else if (key == 27 || key == 3) { savePrompt_ = false;afterSave_ = 0; }
            return;
        }
        if (alt) return;
        if (key == 27 || key == 3) { afterSave_ = 0;finishHexPage(false);return; }
        if (key == KEY_F(1)) { helpDocument_.open(HelpTopic::Viewer);help_ = true;return; }
        if (key == 10 || key == 13 || key == KEY_ENTER || key == 19 || key == KEY_PPAGE || key == KEY_NPAGE) {
            if ((key == KEY_PPAGE && editOffset_ == 0) || (key == KEY_NPAGE && editOffset_ + editCapacity_ >= viewed_.bytes.size())) return;
            afterSave_ = key == KEY_PPAGE ? -1 : key == KEY_NPAGE ? 1 : key == 19 ? 2 : 0;
            if (editPage_ == viewed_.bytes.mid(editOffset_, editPage_.size())) finishHexPage(false);else savePrompt_ = true;
            return;
        }
        if (key == 9) { asciiEdit_ = !asciiEdit_;editNibble_ = 0; }
        else if (key == KEY_F(8)) { editPage_ = viewed_.bytes.mid(editOffset_, editPage_.size());status_ = "Unsaved changes undone"; }
        else if (key == KEY_HOME) { editCursor_ = 0;editNibble_ = 0; }
        else if (key == KEY_END) { editCursor_ = editPage_.size() - 1;editNibble_ = asciiEdit_ ? 0 : 1; }
        else if (key == KEY_UP) editCursor_ -= bytesPerRow(columns_);
        else if (key == KEY_DOWN) editCursor_ += bytesPerRow(columns_);
        else if (key == KEY_LEFT) {
            if (asciiEdit_) --editCursor_;else if (editNibble_ == 1) editNibble_ = 0;else { --editCursor_;editNibble_ = 1; }
        } else if (key == KEY_RIGHT) {
            if (asciiEdit_) ++editCursor_;else if (editNibble_ == 0) editNibble_ = 1;else { ++editCursor_;editNibble_ = 0; }
        } else if (literal && key >= 32 && key < KEY_MIN && key != 127) {
            if (asciiEdit_) {
                if (key > 255) { status_ = "Character must fit in one byte";return; }
                editPage_[editCursor_] = char(key);if (editCursor_ + 1 < editPage_.size()) ++editCursor_;
            } else {
                bool ok = false;const int nibble = QString(QChar(key)).toInt(&ok, 16);if (!ok) return;
                const int old = quint8(editPage_[editCursor_]);
                editPage_[editCursor_] = char(editNibble_ ? (old & 240) | nibble : (old & 15) | (nibble << 4));
                if (editNibble_ == 0) editNibble_ = 1;else { editNibble_ = 0;if (editCursor_ + 1 < editPage_.size()) ++editCursor_; }
            }
        }
        editCursor_ = std::clamp(editCursor_, qsizetype(0), editPage_.size() - 1);
    }
    void readDocument(const QString &path, bool edit = false, std::optional<bool> mode = {})
    {
        documentReload_=viewer_&&viewerPath_==path;documentTop_=viewerTop_;documentPreview_=previewForward_;viewerPath_ = path;documentEdit_ = edit;documentMode_ = mode;documentSnapshot_ = hexSnapshot(path);
        cancel_ = std::make_shared<std::atomic_bool>(false);const auto cancel = cancel_;
        work_ = Work::Document;document_ = QtConcurrent::run([path, cancel] { return readViewDocument(path, cancel); });
    }
    void editFile(const QString &path)
    {
        if (!QFileInfo(path).isFile()) { status_ = "No file selected";return; }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { status_ = file.errorString();return; }
        const QByteArray sample = file.read(65536);
        if (file.error() != QFileDevice::NoError) { status_ = file.errorString();return; }
        if (terminalBinaryData(sample)) {
            if (file.size() > 32 * 1024 * 1024) { status_ = "Internal hex editor limit: 32 MiB";return; }
            readDocument(path, true);return;
        }
        file.close();
        QString error;auto command = terminalEditorCommand(&error);
        if (command.isEmpty()) { status_ = error;return; }
        QProcess editor;
        editor.setWorkingDirectory(QFileInfo(path).absolutePath());
        editor.setProcessChannelMode(QProcess::ForwardedChannels);
        editor.setInputChannelMode(QProcess::ForwardedInputChannel);
        editor.setUnixProcessParameters(QProcess::UnixProcessFlag::ResetSignalHandlers);
        const auto previousInt = std::signal(SIGINT, SIG_IGN), previousQuit = std::signal(SIGQUIT, SIG_IGN);
        // Entregar la terminal al editor y recuperar después el modo y la pantalla de ncurses.
        keyboard_.suspend();def_prog_mode();endwin();
        const QString executable = command.takeFirst();command.append(path);
        editor.start(executable, command);
        const bool started = editor.waitForStarted();
        if (started) {
            while (editor.state() != QProcess::NotRunning) {
                editor.waitForFinished(100);
                QCoreApplication::processEvents();
                if (stopped && editor.state() != QProcess::NotRunning) {
                    editor.terminate();
                    if (!editor.waitForFinished(3000)) { editor.kill();editor.waitForFinished(); }
                    break;
                }
            }
        }
        std::signal(SIGINT, previousInt);std::signal(SIGQUIT, previousQuit);
        reset_prog_mode();clearok(stdscr, true);wrefresh(stdscr);keyboard_.resume();
        if (!started) { status_ = "Cannot start editor: " + editor.errorString();return; }
        reloadEditedViewer_ = viewer_;
        load(false, true, QFileInfo(path).absolutePath());
        editorNotice_ = editor.exitStatus() == QProcess::CrashExit ? "Editor terminated unexpectedly" :
            editor.exitCode() ? QString("Editor exited with code %1").arg(editor.exitCode()) : QString();
    }
    void beginMetadata(bool stamp, bool tagged)
    {
        auto &s = active_.session;
        if (stamp && s.view == View::Tree) { status_ = "New date requires the file list";return; }
        metadataTargets_.clear();stamping_ = stamp;metadataTagged_ = tagged;
        if (tagged) {
            for (const auto &file : s.files()) if (s.tags.contains(file.path)) metadataTargets_.append(readMetadata(file.path));
        } else {
            const auto path = s.view == View::Tree ? s.directory : s.currentFile() ? s.currentFile()->path : QString();
            if (!path.isEmpty()) metadataTargets_.append(readMetadata(path));
        }
        if (metadataTargets_.isEmpty()) { status_ = tagged ? "No tagged files in the current list" : "No file selected";return; }
        if (!tagged) {
            const auto &target = metadataTargets_.first();
            if (!target.error.isEmpty()) { status_ = target.error;return; }
            if (target.symlink || (!target.regular && (stamp || !target.directory))) {
                status_ = stamp ? "Timestamps require a regular file; symbolic links are not supported" :
                    "Permissions require a regular file or directory; symbolic links are not supported";return;
            }
        }
        stampField_ = StampField::Written;stampMode_ = StampMode::Set;
        openPrompt(stamp ? Prompt::Stamp : Prompt::Permissions, stamp ? QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") : QString());
        inputSelected_ = stamp;
    }
    void drawMetadataPrompt()
    {
        for (int y = rows_ - 3; y < rows_; ++y) fill(0, y, columns_, 1);
        const auto &target = metadataTargets_.first();
        const QString title = stamping_ ? "STAMP" : "PERMISSIONS";
        text(0, rows_ - 3, metadataTagged_ ? QString("%1 %2 tagged files").arg(title).arg(metadataTargets_.size()) :
            title + ": " + pathLabel(target.path, std::max(1, columns_ - int(title.size()) - 2)), 1);
        if (prompt_ == Prompt::MetadataReview) {
            text(0, rows_ - 2, stamping_ ? QStringList{"Written", "Accessed", "Both"}[int(stampField_)] + " / " + QStringList{"Set", "Adjust", "Increment"}[int(stampMode_)] + ": " + input_ + " to " + QString::number(metadataTargets_.size()) + " file(s)" :
                metadataTagged_ ? "Apply " + input_ + " to " + QString::number(metadataTargets_.size()) + " tagged files" :
                permissionOctal(target.mode) + " (" + permissionText(target.mode) + ") -> " + permissionOctal(permissionModes_.first()) + " (" + permissionText(permissionModes_.first()) + ")", 2);
            commands(0, rows_ - 1, "[Y/Enter] Apply [N/Esc] Cancel [Backspace] Edit");return;
        }
        const QString label = stamping_ ? QStringList{"Set to: ", "Adjust: ", "Increment: "}[int(stampMode_)] : "Mode (" + permissionOctal(target.mode) + "): ";
        const int room = std::max(1, columns_ - int(label.size()) - 1), first = std::max(0, cursor_ - room + 1);
        text(0, rows_ - 2, label + input_.mid(first), 2);
        if (inputSelected_) fill(int(label.size()), rows_ - 2, std::min(room, int(input_.size())), 3);
        if (inputSelected_) text(int(label.size()), rows_ - 2, input_.mid(first), 3, room);
        if (!status_.isEmpty()) text(0, rows_ - 1, status_, 2);
        else if (stamping_) {
            commands(0, rows_ - 1, "[Enter] Review [Esc] Cancel [F2] Now [F4] " + QStringList{"Written", "Accessed", "Both"}[int(stampField_)] +
                " [F5] Mode [Tab] Current [F3/↑] Last");
        } else commands(0, rows_ - 1, "[Enter] Review [Esc] Cancel  Examples: 755, u+w, go-w");
        wmove(stdscr, rows_ - 2, int(label.size()) + cursor_ - first);curs_set(2);
    }
    void reviewMetadata()
    {
        if (stamping_) {
            stampPlan_ = planStamp(metadataTargets_, input_, stampField_, stampMode_);
            if (!stampPlan_.error.isEmpty()) { status_ = stampPlan_.error;return; }
        } else {
            permissionModes_.clear();
            for (const auto &target : metadataTargets_) {
                quint32 mode = 0;
                if (!parsePermissions(input_, target.mode, target.directory, mode, status_)) return;
                permissionModes_.append(mode);
            }
        }
        prompt_ = Prompt::MetadataReview;inputSelected_ = false;
    }
    void applyMetadata()
    {
        prompt_ = Prompt::None;work_ = Work::Metadata;invalidateAutoview();
        cancel_ = std::make_shared<std::atomic_bool>(false);const auto cancel = cancel_;
        if (stamping_) {
            remember("stamp",input_);stampHistory_=histories_["stamp"].entries;
            const auto plan = stampPlan_;
            metadata_ = QtConcurrent::run([plan, cancel] {
                const auto result = stampFiles(plan, cancel);
                return MetadataResult{result.changed, result.unchanged, result.failed, result.cancelled, result.error, result.scan};
            });
        } else {
            const auto targets = metadataTargets_;const auto modes = permissionModes_;
            metadata_ = QtConcurrent::run([targets, modes, cancel] {
                MetadataResult result;QSet<QString> directories;
                for (int i = 0; i < targets.size(); ++i) {
                    if (cancel->load()) { result.cancelled = true;break; }
                    const auto &target = targets[i];const auto changed = changePermissions(target, modes[i], cancel);
                    if (changed.changed) ++result.changed;
                    else if (!changed.error.isEmpty()) { ++result.failed;if (result.error.isEmpty()) result.error = target.path + ": " + changed.error; }
                    else if (!changed.cancelled) ++result.unchanged;
                    directories.insert(target.directory ? target.path : QFileInfo(target.path).absolutePath());
                    if (changed.cancelled) { result.cancelled = true;break; }
                }
                const auto refresh = std::make_shared<std::atomic_bool>(false);
                for (const auto &directory : directories) result.scan.directories += scanDirectories(directory, false, refresh).directories;
                return result;
            });
        }
    }
    void openPrompt(Prompt prompt, const QString &input = {}) { prompt_ = prompt;input_ = input;cursor_ = int(input.size());historyIndex_ = -1;inputSelected_ = false;status_.clear(); }
    void selectRoot(QString path)
    {
        if (path == "~") path = QDir::homePath();else if (path.startsWith("~/")) path = QDir::homePath() + path.mid(1);
        const QFileInfo info(QDir(active_.session.directory).absoluteFilePath(path));
        if (!info.isDir() || !info.isReadable()) { status_ = "Directory unavailable";return; }
        roots_.insert(active_.session.root,active_);
        stopAutoview();const auto root=info.canonicalFilePath();active_=roots_.contains(root)?roots_[root]:Pane(root);location_ = false;prompt_ = Prompt::None;load(false);
    }
    void promptKey(int key, bool literal)
    {
        if (key == KEY_F(1)) {
            showHelp(prompt_ == Prompt::Permissions ? HelpTopic::Metadata : prompt_ == Prompt::Stamp || prompt_ == Prompt::MetadataReview ? HelpTopic::Stamp : HelpTopic::Terminal);
            return;
        }
        if (key == 27 || key == 3) { prompt_ = Prompt::None;return; }
        if (prompt_ == Prompt::Quit) {
            if (key == 'y' || key == 'Y' || key == 10 || key == 13 || key == KEY_ENTER) quit_ = true;
            else if (key == 'n' || key == 'N') prompt_ = Prompt::None;
            return;
        }
        if (prompt_ == Prompt::MetadataReview) {
            if (key == 'n' || key == 'N') prompt_ = Prompt::None;
            else if (key == KEY_BACKSPACE || key == 127 || key == 8) prompt_ = stamping_ ? Prompt::Stamp : Prompt::Permissions;
            else if (key == 'y' || key == 'Y' || key == 10 || key == 13 || key == KEY_ENTER) applyMetadata();
            return;
        }
        if (prompt_ == Prompt::Stamp) {
            if (key==KEY_UP||key==KEY_DOWN){auto chosen=historyPopup("stamp",input_);stampHistory_=histories_["stamp"].entries;if(chosen){input_=*chosen;cursor_=input_.size();inputSelected_=true;}return;}
            bool option = true;
            if (key == KEY_F(4)) { stampField_ = StampField((int(stampField_) + 1) % 3);return; }
            if (key == KEY_F(5)) {
                const auto previous = stampMode_;stampMode_ = StampMode((int(stampMode_) + 1) % (metadataTagged_ ? 3 : 2));
                if (stampMode_ == StampMode::Set) input_ = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                else if (previous == StampMode::Set) input_.clear();
            } else if (key == KEY_F(2)) { stampMode_ = StampMode::Set;input_ = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"); }
            else if (key == 9) {
                const auto &target = metadataTargets_.first();stampMode_ = StampMode::Set;
                input_ = (stampField_ == StampField::Accessed ? target.accessed : target.modified).toString("yyyy-MM-dd HH:mm:ss");
            } else if ((key == KEY_F(3) || key == KEY_UP || key == KEY_DOWN) && !stampHistory_.isEmpty()) {
                historyIndex_ = key == KEY_F(3) ? 0 : std::clamp(historyIndex_ + (key == KEY_DOWN ? -1 : 1), 0, int(stampHistory_.size()) - 1);
                input_ = stampHistory_[historyIndex_];
                stampMode_ = input_.startsWith('+') || input_.startsWith('-') || input_ == "e" ? StampMode::Adjust : StampMode::Set;
            } else option = false;
            if (option) { cursor_ = int(input_.size());inputSelected_ = true;return; }
        }
        if (literal && key >= 32 && key != 127) {
            if (input_.size() < 1024) {
                if (inputSelected_) { input_.clear();cursor_ = 0;inputSelected_ = false; }
                const char32_t cp = char32_t(key);const QString added = QString::fromUcs4(&cp, 1);
                input_.insert(cursor_, added);cursor_ += int(added.size());
            }
            return;
        }
        if (key == 10 || key == 13 || key == KEY_ENTER) {
            if (prompt_ == Prompt::Permissions || prompt_ == Prompt::Stamp) reviewMetadata();
            else if (prompt_ == Prompt::Filespec) {
                QString error;if (!active_.session.filespec.set(input_, &error)) { status_ = error;return; }
                if (!input_.isEmpty()) { filespecHistory_.removeAll(input_);filespecHistory_.prepend(input_);if (filespecHistory_.size() > 64) filespecHistory_.removeLast(); }
                active_.session.rebuild();prompt_ = Prompt::None;
            } else if (prompt_ == Prompt::Path) selectRoot(input_);
            else { if (!active_.session.spell(input_)) status_ = "No match";prompt_ = Prompt::None; }
        } else if (inputSelected_ && (key == KEY_BACKSPACE || key == 127 || key == 8 || key == KEY_DC)) { input_.clear();cursor_ = 0; }
        else if (key == KEY_LEFT) cursor_ = std::max(0, cursor_ - 1);
        else if (key == KEY_RIGHT) cursor_ = std::min(int(input_.size()), cursor_ + 1);
        else if (key == KEY_HOME || key == 1) cursor_ = 0;
        else if (key == KEY_END || key == 5) cursor_ = int(input_.size());
        else if (key == 21) { input_.clear();cursor_ = 0; }
        else if ((key == KEY_UP || key == KEY_DOWN || key == KEY_F(3)) && prompt_ == Prompt::Filespec && !filespecHistory_.isEmpty()) {
            historyIndex_ = key == KEY_F(3) ? 0 : std::clamp(historyIndex_ + (key == KEY_DOWN ? -1 : 1), 0, int(filespecHistory_.size()) - 1);
            input_ = filespecHistory_[historyIndex_];cursor_ = int(input_.size());
        } else if (key == KEY_BACKSPACE || key == 127 || key == 8) { if (cursor_ > 0) input_.remove(--cursor_, 1); }
        else if (key == KEY_DC) input_.remove(cursor_, 1);
        inputSelected_ = false;
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
        if (event.bstate & BUTTON3_CLICKED) s.invertTag();
        if (event.bstate & BUTTON1_DOUBLE_CLICKED) {
            if (s.view == View::Tree) s.enter(View::Directory);
            else if (s.currentFile()) openSelected();
        }
    }
    void key(int value, bool alt, bool literal, bool directControl = false)
    {
        if (!literal && value == KEY_RESIZE) return;
        if (rows_ < 12 || columns_ < 40) { if (value == 'q' || value == 'Q' || value == 3) quit_ = true;return; }
        if (work_ != Work::None) { if (value == 27 || value == 3 || (directControl && value == 'c')) { cancel_->store(true);pendingAutoview_ = false; }return; }
        status_.clear();
        if (prompt_ != Prompt::None) {
            if (directControl && value >= 'a' && value <= 'z') value -= 'a' - 1;
            promptKey(value, literal);return;
        }
        if (literal && value >= KEY_MIN) return;
        const int letter = value >= 'A' && value <= 'Z' ? value + 'a' - 'A' : value;
        if (help_) { helpKey(value);return; }
        if (editing_) {
            if (directControl && value >= 'a' && value <= 'z') value -= 'a'-1;
            hexEditKey(value, literal, alt);return;
        }
        if (viewer_) { extendedViewerKey(value,alt,literal,directControl);return; }
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
            if (!alt && literal && value>='A' && value<='Z') {forwardPreviewKey(value+32,false,true);return;}
            if (!alt && value>=0x2201&&value<=0x220c){forwardPreviewKey(KEY_F(value-0x2200));return;}
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
        const bool inTree=active_.session.view==View::Tree;
        bool control = directControl || (menu_==1 && ((inTree?QString("btusgif"):QString("acdmnrsjvituf")).contains(QChar(letter))||value==13||value==10||value==KEY_ENTER||(value>=KEY_F(5)&&value<=KEY_F(9))||value==9));
        alt = alt || (menu_==2 && ((inTree?QString("acgpisf"):QString("cmviskf")).contains(QChar(letter))||value==KEY_F(3)));menu_ = 0;
        if (value > 0 && value < 27 && value != 8 && value != 9 && value != 10 && value != 13) control = true;
        if (control && value > 0 && value < 27 && value != 9 && value != 10 && value != 13) value += 'a' - 1;
        if (extendedKey(value, alt, control, literal)) return;
        auto &s = active_.session;const bool tree = s.view == View::Tree;
        if (value == KEY_MOUSE) { mouse();return; }
        if (alt && letter == 'f') { display_ = Display((int(display_) + 1) % 4);status_ = "File display: " + QStringList{"Name", "Name, size and attributes", "Details", "Long name"}[int(display_)]; }
        else if (alt && letter == 's') { s.sort.key = SortKey((int(s.sort.key) + 1) % 10);s.rebuild();status_ = "Sort: " + sortLabel(s.sort); }
        else if (alt && letter == 'a' && tree) beginMetadata(false, false);
        else if (alt) status_ = "Not available in terminal mode; F1 shows supported commands";
        else if (letter == 'a' || value == 1) beginMetadata(false, control || value == 1);
        else if (letter == 'n' || value == 14) beginMetadata(true, control || value == 14);
        else if (letter == 'q' || value == 3) openPrompt(Prompt::Quit);
        else if (value == KEY_F(1)) { helpDocument_.open(HelpTopic::Terminal);help_ = true; }
        else if (value == KEY_F(7)) startAutoview();
        else if (letter == 'f') openPrompt(Prompt::Filespec);
        else if (letter == '|') openPrompt(Prompt::Spell);
        else if (letter == 'l') { avail_=false;locations_ = mountedLocations();locationIndex_ = locationTop_ = 0;location_ = true; }
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
        else if ((letter == 'e' || letter == 'o') && !tree && s.currentFile()) editFile(s.currentFile()->path);
        else if (letter == 'v' && !tree && s.currentFile()) readDocument(s.currentFile()->path);
        else status_ = "Not available in terminal mode; F1 shows supported commands";
    }
public:
    TerminalUI(const QString &root, bool sizes, bool colors) : active_(root), sizes_(sizes), colors_(colors) { initializeTerminalHistories();load(false); }
    ~TerminalUI() {
        saveTerminalHistories();
        if (cancel_) cancel_->store(true);
        if (previewCancel_) previewCancel_->store(true);
        scan_.waitForFinished();document_.waitForFinished();metadata_.waitForFinished();previewRead_.waitForFinished();
    }
    int run()
    {
        while (!quit_ && !stopped) {
            QCoreApplication::processEvents();complete();publishGlobal();updateAutoview();viewerTick();draw();
            const auto input = keyboard_.read();
            if (input) key(input->value,input->alt,input->literal,input->control);
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
        init_extended_pair(3, black, grey);
        const int palette[] = {COLORS >= 0x1000000 ? 0x00ff00 : COLORS >= 256 ? 46 : COLOR_GREEN, COLORS >= 0x1000000 ? 0x008000 : COLORS >= 256 ? 28 : COLOR_GREEN,
            COLORS >= 0x1000000 ? 0xff00ff : COLORS >= 256 ? 201 : COLOR_MAGENTA, COLORS >= 0x1000000 ? 0x808080 : COLORS >= 256 ? 244 : COLOR_WHITE,
            grey, COLORS >= 0x1000000 ? 0x808000 : COLORS >= 256 ? 142 : COLOR_YELLOW, COLORS >= 0x1000000 ? 0xff0000 : COLORS >= 256 ? 196 : COLOR_RED};
        for (int i=0;i<7;++i) init_extended_pair(4+i,palette[i],blue);
        int teal=COLORS>=0x1000000?0x008080:COLORS>=256?30:COLOR_CYAN;
        init_extended_pair(11,cyan,teal);init_extended_pair(12,yellow,teal);init_extended_pair(13,palette[0],teal);
        init_extended_pair(14,yellow,COLORS>=0x1000000?0x800000:COLORS>=256?88:COLOR_RED);
        int foregrounds[]={cyan,yellow,black,palette[0],palette[1],palette[2],palette[3],palette[4],palette[5],palette[6]};
        for(int background=0;background<2;++background)for(int p=1;p<=10;++p)init_extended_pair(20+p+10*background,foregrounds[p-1],background?grey:black);
        wbkgd(stdscr, COLOR_PAIR(2));
    }
    // Normalizar teclas modificadas habituales de Kitty/xterm.
    define_key("\x1b[1;2A", KEY_SR);define_key("\x1b[1;2B", KEY_SF);
    define_key("\x1b[5;2~", KEY_SPREVIOUS);define_key("\x1b[6;2~", KEY_SNEXT);
    define_key("\x1b[1;2H", KEY_SHOME);define_key("\x1b[1;2F", KEY_SEND);
    define_key("\x1b[1;3D", AltLeft);define_key("\x1b[1;3C", AltRight);define_key("\x1b[1;3H", AltHome);
    for (int n=1;n<=12;++n) {
        const char *codes[] = {"", "", "", "", "", "15", "17", "18", "19", "20", "21", "23", "24"};
        const QByteArray prefix = n <= 4 ? QByteArray("\x1b[1;") : QByteArray("\x1b[")+codes[n]+";";
        const QByteArray suffix = n <= 4 ? QByteArray(1,char('P'+n-1)) : QByteArray("~");
        define_key((prefix+"5"+suffix).constData(),0x2100+n);
        define_key((prefix+"2"+suffix).constData(),0x2200+n);
        define_key((prefix+"3"+suffix).constData(),0x2500+n);
    }
    define_key("\x1b[1;5A",0x2301);define_key("\x1b[1;5B",0x2302);
    define_key("\x1b[1;5D",0x2303);define_key("\x1b[1;5C",0x2304);
    define_key("\x1b[1;5H",0x2305);define_key("\x1b[1;5F",0x2306);
    define_key("\x1b[13;5u",0x2601);define_key("\x1b[9;5u",0x2602);define_key("\x1b[3;5~",0x2603);
    define_key("\x1b[127;2u",0x2604);define_key("\x1b[27;2u",0x2605);
    define_key("\x1b[2;5~",0x2606);define_key("\x1b[2;2~",0x2607);
    define_key("\x1b[1;2D",0x2401);define_key("\x1b[1;2C",0x2402);define_key("\x1b[1;2H",0x2403);
    mousemask(BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED | BUTTON3_CLICKED | BUTTON4_PRESSED
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
