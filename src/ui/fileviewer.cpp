#include "ui/theme.h"
#include "ui/fileviewer.h"
#include "ui/doschars.h"
#include "fs/archive.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QKeyEvent>
#include <QFileInfo>
#include <QWheelEvent>
#include <QFontDatabase>
#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>
#include <algorithm>

namespace ltree {
namespace {
const QColor cyan = theme::labels, yellow = theme::normal;

}
FileViewer::FileViewer(QString path, QWidget *parent) : QWidget(parent), path_(std::move(path))
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoMousePropagation);
    auto mono = QFontDatabase::systemFont(QFontDatabase::FixedFont); mono.setPixelSize(14); setFont(mono);
    if (!consoleFont_.available()) { const QFontMetrics m(mono); cw_ = m.horizontalAdvance('M'); ch_ = m.height(); ascent_ = m.ascent(); }
    connect(&watcher_, &QFutureWatcher<ViewDocument>::finished, this, [this] {
        loading_ = false;
        if (pendingPath_) { reload(); return; }
        auto data = watcher_.result();
        if (data.cancelled) status_ = "Read cancelled";
        else if (!data.error.isEmpty()) status_ = data.error;
        else { document_ = std::move(data); status_.clear(); hit_ = -1; archiveListing_=document_.encoding=="Archive contents"; if(!archiveListing_)decode(); if (wrap_) rebuildWrap(); clamp(); if (batchSearch_) { backward_=continueBackward_;continueBackward_=false;const bool across=continueSearch_;continueSearch_=false;searchStart_=backward_?std::max<qsizetype>(0,(byteMode()?document_.bytes.size():document_.text.size())-(batchSearch_->mode==SearchMode::Regex?0:1)):0; find(false,across); } }
        update();
    });
    connect(&scrollTimer_,&QTimer::timeout,this,[this]{if(loading_ || prompting())return;if(top_+pageRows()>=totalRows()){if(scrollLoop_)top_=0;else scrollTimer_.stop();}else top_+=scrollDirection_;clamp();update();});
    connect(&reloadTimer_,&QTimer::timeout,this,[this]{if(!loading_ && !prompting())reload();});
    loadViewHistory();
    reload();
}
FileViewer::~FileViewer() { if (cancel_) cancel_->store(true);if(historyDirty_)saveViewHistory(); }
qsizetype FileViewer::totalRows() const { return byteMode() ? std::max<qsizetype>(1, (document_.bytes.size()+bytesPerRow()-1)/bytesPerRow()) : wrap_ && !wrapStarts_.isEmpty() ? wrapStarts_.last() : document_.lines.size(); }
int FileViewer::pageRows() const { return std::max(1, height()/ch_ - 6); }
void FileViewer::clamp() { top_ = std::clamp<qsizetype>(top_, 0, std::max<qsizetype>(0, totalRows()-1)); left_ = std::max<qsizetype>(0, left_); }
void FileViewer::setPath(const QString &path)
{
    if (path_ == path) return;
    path_ = path;
    document_ = {}; wrapStarts_.clear(); top_ = left_ = 0; hit_ = -1;
    searchPrompt_ = offsetPrompt_ = false; editing_=savePrompt_=false;gather_=0;lineCursor_=0;bookmarks_.fill(0);rawArchive_=!files_.isEmpty(); input_.clear(); query_.clear(); batchSearch_.reset();
    status_ = "Reading…";
    if (loading_) { pendingPath_ = true; cancel_->store(true); } else reload();
    update();
}
void FileViewer::setSearch(const SearchOptions &options)
{
    batchSearch_ = options; query_ = options.query; caseSensitive_ = options.caseSensitive;
    promptMode_=options.mode;hex_ = options.mode == SearchMode::Hex; wrap_ = dump_ = backward_ = false;
    if (options.mode == SearchMode::Regex) { charset_ = 0; junk_ = false; if (!loading_) decode(); }
    top_ = left_ = searchStart_ = 0; hit_ = -1;
    if(options.mode==SearchMode::Hex && archiveName(path_) && !rawArchive_){rawArchive_=true;if(loading_){pendingPath_=true;cancel_->store(true);}else reload();}
    if (!loading_ && document_.error.isEmpty()) find(false);
    update();
}
void FileViewer::reload()
{
    if (loading_) return;
    cancel_ = std::make_shared<std::atomic_bool>(false); loading_ = true; status_ = "Reading… Esc returns to the file list";
    pendingPath_ = false;
    const auto path = path_; const auto cancel = cancel_;const bool listing=archiveName(path) && !rawArchive_ && !autoview_ && files_.isEmpty();
    snapshot_=hexSnapshot(path);
    watcher_.setFuture(QtConcurrent::run([path,cancel,listing] {
        if(!listing)return readViewDocument(path,cancel);
        const auto catalog=listArchive(path,cancel);ViewDocument doc;doc.error=catalog.error;doc.cancelled=catalog.cancelled;doc.encoding="Archive contents";
        doc.text="ARCHIVE CONTENTS: "+catalog.format+"\n\n";
        for(const auto &entry:catalog.entries)doc.text+=QString("%1 %2 %3\n").arg(entry.directory?"DIR":"FILE").arg(entry.size,12).arg(entry.path);
        doc.bytes=doc.text.toUtf8();doc.lines.append(0);for(qsizetype i=0;i+1<doc.text.size();++i)if(doc.text[i]=='\n')doc.lines.append(i+1);return doc;
    })); update();
}
qsizetype FileViewer::lineEnd(qsizetype line) const
{
    qsizetype end = line + 1 < document_.lines.size() ? document_.lines[line+1] : document_.text.size();
    if (end > document_.lines[line] && document_.text[end-1] == '\n') --end;
    return end;
}
qsizetype FileViewer::displayColumn(qsizetype line, qsizetype offset) const
{
    qsizetype column = 0;
    for (auto i = document_.lines[line]; i < offset; ++i) {
        column += document_.text[i] == '\t' ? tabWidth_ - column % tabWidth_ : 1;
        if (document_.text[i].isHighSurrogate() && i+1 < offset && document_.text[i+1].isLowSurrogate()) ++i;
    }
    return column;
}
void FileViewer::rebuildWrap()
{
    wrapColumns_ = std::max(1, width()/cw_);
    wrapStarts_.clear(); wrapStarts_.append(0);
    for (qsizetype line=0; line<document_.lines.size(); ++line) {
        const auto columns = displayColumn(line, lineEnd(line));
        wrapStarts_.append(wrapStarts_.last() + std::max<qsizetype>(1, (columns+wrapColumns_-1)/wrapColumns_));
    }
}
qsizetype FileViewer::sourceLine(qsizetype row) const
{
    if (!wrap_ || wrapStarts_.size()<2) return row;
    return std::clamp<qsizetype>(std::upper_bound(wrapStarts_.cbegin(),wrapStarts_.cend(),row)-wrapStarts_.cbegin()-1,0,document_.lines.size()-1);
}
qsizetype FileViewer::sourceOffset(qsizetype row) const
{
    if (row >= totalRows()) return document_.text.size();
    const auto line = sourceLine(row);
    if (line >= document_.lines.size()) return 0;
    const auto target = wrap_ ? (row-wrapStarts_[line])*wrapColumns_ : 0;
    qsizetype column=0, offset=document_.lines[line];
    while (offset<lineEnd(line) && column<target) {
        const auto next=column+(document_.text[offset]=='\t'?tabWidth_-column%tabWidth_:1);
        if (next>target) break;
        if (document_.text[offset].isHighSurrogate() && offset+1<lineEnd(line) && document_.text[offset+1].isLowSurrogate()) ++offset;
        ++offset; column=next;
    }
    return offset;
}
void FileViewer::selectMode(int key)
{
    const int current = hex_ ? Qt::Key_H : junk_ ? Qt::Key_J : wrap_ ? Qt::Key_W : dump_ ? Qt::Key_D : Qt::Key_A;
    if (key == current) key = previousMode_;
    previousMode_ = current;
    hex_ = key == Qt::Key_H; wrap_ = key == Qt::Key_W; dump_ = key == Qt::Key_D;junk_=key==Qt::Key_J;if(junk_)wrap_=true;
    dumpColumns_ = std::max(1,width()/cw_-10);
    if(archiveListing_) {rawArchive_=true;archiveListing_=false;document_={};reload();}
    else decode();
    if (wrap_) rebuildWrap();
    top_ = left_ = 0;lineCursor_=0; hit_ = -1; query_.clear(); batchSearch_.reset();
}
void FileViewer::text(QPainter &p, int x, int y, const QString &line, QColor color, int max)
{
    if (max < 0) max = width()/cw_ - x;
    p.save(); p.setPen(color);
    int col = 0;
    for (char32_t code : line.toUcs4()) {
        if (col >= max) break;
        QString glyph = QString::fromUcs4(&code,1);
        if (QChar::category(code) == QChar::Other_Control) glyph = ".";
        p.setClipRect((x+col)*cw_,y*ch_,cw_,ch_);
        if (!consoleFont_.draw(p,(x+col)*cw_,y*ch_,glyph,color)) p.drawText((x+col)*cw_,y*ch_+ascent_,glyph);
        ++col;
    }
    p.restore();
}
void FileViewer::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setFont(font()); p.fillRect(rect(),background());
    const int cols = width()/cw_, rows = height()/ch_;
    text(p,0,0,autoview_ ? QFileInfo(path_).fileName() : displayName_.isEmpty()?path_:displayName_,cyan);
    p.fillRect(0,ch_+ch_/2,width(),1,cyan);
    if(ruler_ && !byteMode()) { for(int x=0;x<cols;++x)text(p,x,1,QString(QChar(int((x+left_+1)%10?'.':'0'+((x+left_+1)/10)%10))),cyan,1); }
    for (int y = 0; y < pageRows() && top_+y < totalRows(); ++y) {
        const qsizetype row = top_+y;
        if (dump_) {
            const auto offset = row*dumpColumns_;
            text(p,0,y+2,QString("%1  ").arg(offset,8,16,QChar('0')).toUpper(),cyan,10);
            for (int i=0; i<dumpColumns_ && offset+i<document_.bytes.size(); ++i) {
                const bool marked=hit_>=0 && offset+i>=hit_ && offset+i<hit_+hitLength_;
                if (marked) p.fillRect((10+i)*cw_,(y+2)*ch_,cw_,ch_,cyan);
                text(p,10+i,y+2,byteGlyph(quint8(document_.bytes[offset+i])),marked?QColor(Qt::black):foreground(),1);
            }
        } else if (hex_) {
            const qsizetype offset = row*16;
            QString values, printable;
            for (int i=0;i<16;++i) {
                if (offset+i < document_.bytes.size()) {
                    const auto byte = quint8(editing_ && offset+i>=editOffset_ && offset+i<editOffset_+editPage_.size()?editPage_[offset+i-editOffset_]:document_.bytes[offset+i]);
                    values += QString("%1 ").arg(byte,2,16,QChar('0')).toUpper();
                    printable += byteGlyph(byte);
                } else { values += "   "; printable += ' '; }
            }
            text(p,0,y+2,QString("%1  ").arg(offset,8,16,QChar('0')).toUpper(),cyan,10);
            text(p,10,y+2,values,foreground(),48); text(p,60,y+2,printable,foreground(),16);
            for (int i=0;i<16;++i) if (hit_ >= 0 && offset+i>=hit_ && offset+i<hit_+hitLength_) {
                p.fillRect((10+i*3)*cw_,(y+2)*ch_,2*cw_,ch_,cyan);
                text(p,10+i*3,y+2,values.mid(i*3,2),Qt::black,2);
            }
        } else {
            const bool selected=lineMode_ && row==lineCursor_;
            const bool gathered=gather_ && row>=std::min(gatherStart_,lineCursor_) && row<=std::max(gatherStart_,lineCursor_);
            if(selected || gathered)p.fillRect(0,(y+2)*ch_,width(),ch_,selected?theme::selection:QColor(0,128,128));
            const auto line = sourceLine(row);
            const auto skip = wrap_ ? (row-wrapStarts_[line])*wrapColumns_ : left_;
            qsizetype column=0;
            for (auto i=document_.lines[line]; i<lineEnd(line) && column<skip+cols; ++i) {
                const auto offset=i;
                QString glyph(document_.text[i]);
                if (document_.text[i].isHighSurrogate() && i+1<lineEnd(line) && document_.text[i+1].isLowSurrogate()) glyph+=document_.text[++i];
                const bool tab=glyph=="\t";
                const int span=tab?int(tabWidth_-column%tabWidth_):1;
                const bool matched=hit_>=0 && offset<hit_+hitLength_ && i>=hit_;
                for (int cell=0; cell<span; ++cell,++column) if (column>=skip && column<skip+cols) {
                    const int x=int(column-skip);
                    if (matched) p.fillRect(x*cw_,(y+2)*ch_,cw_,ch_,cyan);
                    text(p,x,y+2,tab?QString(" "):maskedGlyph(glyph),matched || selected?QColor(Qt::black):foreground(),1);
                }
            }
        }
    }
    drawExtras(p);
    if (searchPrompt_ || offsetPrompt_) {
        p.fillRect(0,(rows-2)*ch_,width(),2*ch_,background());
        const QString label = searchPrompt_ ? "SEARCH " + searchModeName(promptMode_).toUpper() + ": " : (byteMode() ? "OFFSET (hex): " : "LINE (1..): ");
        const QString prefix=label.left(cols-1);
        const int available=cols-int(prefix.size());
        const QString visible=input_.right(std::max(0,available-1));
        text(p,0,rows-2,prefix+visible,yellow);
        const int cursor=int(prefix.size()+visible.size());
        p.fillRect(cursor*cw_,(rows-2)*ch_,cw_,ch_,theme::selection);
        text(p,0,rows-1,status_.isEmpty() ? QString("Enter Accept   Esc Cancel   F2 Case (%1)   F3 Last   Up/Down History   F4 Mode (%3)   F8 Skip (%2)").arg(caseSensitive_?"yes":"no",skipPages_?"page":"hit",searchModeName(promptMode_)) : status_,cyan);
    }
    if(historyVisible_)drawExtras(p);
}
void FileViewer::resizeEvent(QResizeEvent *)
{
    if (dump_) {
        const auto offset=top_*dumpColumns_;
        dumpColumns_=std::max(1,width()/cw_-10); top_=offset/dumpColumns_;
    }
    if (wrap_ && !document_.lines.isEmpty()) {
        const auto line=sourceLine(top_);
        const auto column=(top_-wrapStarts_[line])*wrapColumns_;
        rebuildWrap(); top_=std::min(wrapStarts_[line+1]-1,wrapStarts_[line]+column/wrapColumns_);
    }
    clamp();
}
void FileViewer::wheelEvent(QWheelEvent *event)
{
    if(help_){helpDocumentScroll(helpDocument_,event->angleDelta().y()/120,width()/cw_,height()/ch_);update();return;}
    if (loading_ || prompting()) return;
    top_ -= event->angleDelta().y()/120*3; clamp(); update();
}
void FileViewer::find(bool next,bool acrossFiles)
{
    if (query_.isEmpty()) return;
    qsizetype from = next && hit_>=0 ? hit_+(backward_?(skipPages_?-pageRows()*bytesPerRow():-1):(skipPages_?pageRows()*bytesPerRow():1)) : searchStart_;
    if(next && skipPages_ && !byteMode())from=backward_?sourceOffset(std::max<qsizetype>(0,top_-pageRows()))-1:sourceOffset(top_+pageRows());
    qsizetype found=-1;
    if (batchSearch_ && batchSearch_->mode == SearchMode::Regex) {
        status_ = validateSearch(*batchSearch_);
        if (!status_.isEmpty()) { update(); return; }
        if (document_.encoding == "Latin-1 (fallback)") { status_ = "Invalid encoding for regex search"; update(); return; }
        const auto expression = searchExpression(*batchSearch_);
        // Cada coincidencia conserva los límites de su línea, también al retroceder.
        for (qsizetype line = backward_ ? document_.lines.size()-1 : 0;
             line >= 0 && line < document_.lines.size(); line += backward_ ? -1 : 1) {
            const auto start = document_.lines[line], end = lineEnd(line);
            if ((!backward_ && end < from) || (backward_ && start > from)) continue;
            const auto match = matchRegexLine(expression, QStringView(document_.text).mid(start, end-start),
                backward_ ? std::min(from-start, end-start) : std::max<qsizetype>(0, from-start), backward_);
            if (!match.error.isEmpty()) { status_ = match.error; update(); return; }
            if (match.start >= 0) { found = start + match.start; hitLength_ = match.length; break; }
        }
    } else if (byteMode()) {
        const auto mode=batchSearch_?batchSearch_->mode:hex_?SearchMode::Hex:SearchMode::Text;
        QByteArray needle,subject=document_.bytes;
        if(mode==SearchMode::Hex) {
            if (!QRegularExpression("\\A[0-9a-fA-F]{2}(?:\\s+[0-9a-fA-F]{2})*\\z").match(query_.trimmed()).hasMatch()) { status_="Use hex pairs: 6F 72 64"; update(); return; }
            needle=QByteArray::fromHex(query_.toLatin1());
        } else if(mode==SearchMode::Unicode) {
            const bool bigEndian=charset_==5 || subject.startsWith(QByteArray::fromHex("feff"));QString unicode;
            for(qsizetype i=0;i+1<subject.size();i+=2){const auto a=quint8(subject[i]),b=quint8(subject[i+1]);unicode+=QChar(bigEndian?int((a<<8)|b):int(a|(b<<8)));}
            const auto sensitivity=caseSensitive_?Qt::CaseSensitive:Qt::CaseInsensitive;
            if(from>=0){const auto position=backward_?unicode.lastIndexOf(query_,from/2,sensitivity):unicode.indexOf(query_,(from+1)/2,sensitivity);if(position>=0)found=position*2;}
            hitLength_=query_.size()*2;
            needle.clear();
        } else {
            if(dump_)for(const auto c:query_)if(c.unicode()>127){status_="Dump: ASCII search; use Hex for other bytes";update();return;}
            needle=query_.toUtf8();if(!caseSensitive_){subject=subject.toLower();needle=needle.toLower();}
        }
        if(mode!=SearchMode::Unicode){if(from>=0)found=backward_?subject.lastIndexOf(needle,from):subject.indexOf(needle,from);hitLength_=needle.size();}
    } else {
        const bool batchText=batchSearch_ && batchSearch_->mode==SearchMode::Text;
        const auto sensitivity=caseSensitive_ || batchText ? Qt::CaseSensitive:Qt::CaseInsensitive;
        QString subject=document_.text;
        QString needle=batchText && document_.encoding=="Latin-1 (fallback)" ? QString::fromLatin1(query_.toUtf8()):query_;
        if (batchText && !caseSensitive_) {
            // El modo Text del lote solo ignora mayúsculas ASCII.
            const auto fold=[](QString &text) { for(QChar &ch:text) if(ch>='A' && ch<='Z') ch=QChar(ch.unicode()+32); };
            fold(subject);fold(needle);
        }
        hitLength_=needle.size();
        if (batchSearch_ && !needle.startsWith('*') && needle.mid(1,needle.size()-2).contains('*')) {
            QString pattern;
            for (qsizetype i=0;i<needle.size();++i)
                pattern += needle[i]=='*' && i>0 && i+1<needle.size() ? "[^\\r\\n]*?" : QRegularExpression::escape(QString(needle[i]));
            const QRegularExpression expression(pattern,sensitivity==Qt::CaseSensitive?QRegularExpression::NoPatternOption:QRegularExpression::CaseInsensitiveOption);
            // Los asteriscos internos heredados del lote no atraviesan líneas.
            for (qsizetype offset=backward_?0:from;offset>=0 && offset<=subject.size();) {
                const auto match=expression.match(subject,offset);
                if (!match.hasMatch() || (backward_ && match.capturedStart()>from)) break;
                found=match.capturedStart();hitLength_=match.capturedLength();
                if (!backward_) break;
                offset=found+1;
            }
        } else if (from>=0) found=backward_?subject.lastIndexOf(needle,from,sensitivity):subject.indexOf(needle,from,sensitivity);
    }
    if (found<0) {if(acrossFiles && !files_.isEmpty() && fileIndex_+(backward_?-1:1)>=0 && fileIndex_+(backward_?-1:1)<files_.size()){navigateFile(fileIndex_+(backward_?-1:1),true);return;}status_="No more matches";}
    else {
        hit_=found; status_ = hitLength_ == 0 ? QString("Zero-length match at character %1").arg(found + 1) : QString{};
        if (byteMode()) top_=found/bytesPerRow();
        else {
            const auto line=std::upper_bound(document_.lines.cbegin(),document_.lines.cend(),found)-document_.lines.cbegin()-1;
            const auto column=displayColumn(line,found);
            top_=wrap_?wrapStarts_[line]+column/wrapColumns_:line;
            left_=wrap_?0:column>=width()/cw_?column:0;
        }
    }
    if(lineMode_)lineCursor_=top_;
    clamp(); update();
}
void FileViewer::keyPressEvent(QKeyEvent *event)
{
    const int key=event->key(); const auto mods=event->modifiers();
    if (key==Qt::Key_Return && mods.testFlag(Qt::AltModifier)) { if(window()->isFullScreen())window()->showNormal();else window()->showFullScreen();return; }
    if (loading_) { if(key==Qt::Key_Escape){cancel_->store(true);emit closed();} return; }
    status_.clear();
    if(!prompting() && key==Qt::Key_F10 && !mods.testFlag(Qt::ControlModifier) && !mods.testFlag(Qt::AltModifier)){menuMode_=(menuMode_+1)%3;update();return;}
    if(menuMode_ && !prompting()) {
        const int menu=menuMode_;menuMode_=0;
        if(key==Qt::Key_Escape){update();return;}
        const bool number=key>=Qt::Key_0 && key<=Qt::Key_9;
        const bool supported=menu==1?(number || key==Qt::Key_C || key==Qt::Key_Insert || key==Qt::Key_S || key==Qt::Key_F3 || key==Qt::Key_Up || key==Qt::Key_Down || key==Qt::Key_Left || key==Qt::Key_Right || key==Qt::Key_Home || key==Qt::Key_End || key==Qt::Key_PageUp || key==Qt::Key_PageDown):(number || key==Qt::Key_E || key==Qt::Key_X || key==Qt::Key_Home || key==Qt::Key_End || (key>=Qt::Key_F5 && key<=Qt::Key_F9));
        if(supported && !mods.testFlag(Qt::ControlModifier) && !mods.testFlag(Qt::AltModifier)){QKeyEvent command(QEvent::KeyPress,key,mods|(menu==1?Qt::ControlModifier:Qt::AltModifier),event->text(),event->isAutoRepeat(),event->count());keyPressEvent(&command);return;}
    }
    if(extraKey(event))return;
    if (searchPrompt_ || offsetPrompt_) {
        if(key==Qt::Key_Escape){searchPrompt_=offsetPrompt_=false;}
        else if(key==Qt::Key_F2 && searchPrompt_)caseSensitive_=!caseSensitive_;
        else if(key==Qt::Key_F3 && searchPrompt_)input_=query_;
        else if(key==Qt::Key_Backspace)input_.chop(1);
        else if(key==Qt::Key_Return || key==Qt::Key_Enter){
            if(searchPrompt_){
                const SearchOptions options{input_,promptMode_,caseSensitive_};
                if (promptMode_ == SearchMode::Regex) {
                    status_ = validateSearch(options);
                    if (!status_.isEmpty()) { update(); return; }
                    const bool wasBytes = byteMode(); hex_=dump_=junk_=false; charset_=0; decode();
                    if (wasBytes) { top_=left_=0; searchStart_=backward_?document_.text.size():0; }
                }
                batchSearch_=options;
                if(promptMode_==SearchMode::Hex && !byteMode()){hex_=true;wrap_=junk_=false;top_=left_=0;}
                query_=input_;rememberQuery();hit_=-1;searchPrompt_=false;find(false,true);
            }
            else {bool ok=false;const auto value=input_.toLongLong(&ok,byteMode()?16:10);if(ok && value>=(byteMode()?0:1)){top_=byteMode()?value/bytesPerRow():wrap_ && !document_.lines.isEmpty()?wrapStarts_[std::min<qsizetype>(value-1,document_.lines.size()-1)]:value-1;clamp();offsetPrompt_=false;}else status_="Invalid position";}
        } else if(key==Qt::Key_V && mods.testFlag(Qt::ControlModifier))input_+=QGuiApplication::clipboard()->text().section('\n',0,0).remove('\r');
        else if(!mods.testFlag(Qt::ControlModifier) && !mods.testFlag(Qt::AltModifier))input_+=event->text();
        input_.truncate(255);update();return;
    }
    if(key==Qt::Key_Escape || key==Qt::Key_Q){if(scrollTimer_.isActive()){scrollTimer_.stop();update();return;}emit closed();return;}
    if(mods.testFlag(Qt::AltModifier)){status_="This variant is not implemented yet";update();return;}
    if(mods.testFlag(Qt::ControlModifier) && key!=Qt::Key_Left && key!=Qt::Key_Right && key!=Qt::Key_Up && key!=Qt::Key_Down && key!=Qt::Key_PageUp && key!=Qt::Key_PageDown){status_="This Ctrl variant is not implemented yet";update();return;}
    if(key==Qt::Key_A || key==Qt::Key_H || key==Qt::Key_W || key==Qt::Key_D || key==Qt::Key_J)selectMode(key);
    else if(key==Qt::Key_F3 && !mods.testFlag(Qt::ControlModifier))reload();
    else if(key==Qt::Key_E){if(archiveListing_)status_="Enter in the file list browses archive contents";else if(!editable_)status_="Extract this member before editing";else if(byteMode())status_="Use Hex mode (H) to edit bytes";else emit editRequested(path_);}
    else if(key==Qt::Key_Home)top_=0;
    else if(key==Qt::Key_End)top_=std::max<qsizetype>(0,totalRows()-pageRows());
    else if(key==Qt::Key_Up){if(scrollTimer_.isActive())scrollDirection_=-1;--top_;}
    else if(key==Qt::Key_Down){if(scrollTimer_.isActive())scrollDirection_=1;++top_;}
    else if(key==Qt::Key_PageUp)top_-=pageRows();
    else if(key==Qt::Key_PageDown)top_+=pageRows();
    else if(key==Qt::Key_Left && !wrap_ && !dump_)left_-=mods.testFlag(Qt::ControlModifier)?20:2;
    else if(key==Qt::Key_Right && !wrap_ && !dump_)left_+=mods.testFlag(Qt::ControlModifier)?20:2;
    else if(key==Qt::Key_F || key==Qt::Key_B || key==Qt::Key_S || key==Qt::Key_F9 || key==Qt::Key_Slash || key==Qt::Key_Backslash){
        backward_=key==Qt::Key_B || key==Qt::Key_Backslash;
        const qsizetype length=byteMode()?document_.bytes.size():document_.text.size();
        if(key==Qt::Key_F)searchStart_=0;
        else if(key==Qt::Key_B)searchStart_=length;
        else if(byteMode())searchStart_=backward_?std::min(length-1,(top_+pageRows())*bytesPerRow()-1):top_*bytesPerRow();
        else if(!document_.lines.isEmpty())searchStart_=backward_?std::max<qsizetype>(0,sourceOffset(top_+pageRows())-1):sourceOffset(top_);
        input_.clear();promptMode_=batchSearch_ && batchSearch_->mode==SearchMode::Regex ? SearchMode::Regex : hex_?SearchMode::Hex:dump_?SearchMode::Text:SearchMode::Unicode;searchPrompt_=true;
    } else if(key==Qt::Key_Plus || key==Qt::Key_Minus || key==Qt::Key_Space){if(key!=Qt::Key_Space)backward_=key==Qt::Key_Minus;find(true,true);}
    else if(key==Qt::Key_O || (key>=Qt::Key_0 && key<=Qt::Key_9)){offsetPrompt_=true;input_=key==Qt::Key_O?QString{}:event->text();}
    else status_="Viewer command is not implemented yet";
    clamp();update();
}
}
