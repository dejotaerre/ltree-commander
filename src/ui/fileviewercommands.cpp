#include "ui/fileviewer.h"
#include "ui/theme.h"
#include "ui/doschars.h"
#include "fs/archive.h"
#include <QPainter>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QPrinter>
#include <QPrintDialog>
#include <QTextDocument>
#include <QFontDatabase>
#include <QStringDecoder>
#include <iconv.h>
#include <algorithm>
namespace ltree {
namespace {
const std::array<QColor,8> colors{theme::normal,theme::labels,QColor(255,255,255),QColor(0,255,0),QColor(255,0,255),QColor(255,128,0),QColor(192,192,192),QColor(0,0,0)};
const std::array<QColor,6> backgrounds{theme::background,QColor(0,0,0),QColor(0,80,80),QColor(80,0,0),QColor(64,64,64),QColor(128,128,128)};
}
QColor FileViewer::foreground() const { return colors[color_]; }
QColor FileViewer::background() const { return backgrounds[background_]; }
QString FileViewer::byteGlyph(quint8 byte,bool applyMask) const
{
    if(mask_ && applyMask && (byte<32 || byte>127))return ".";
    if(charset_==2 || charset_==6){
        const auto table=[](const char *encoding){QByteArray input;for(int i=0;i<256;++i)input.append(char(i));QByteArray output(1032,Qt::Uninitialized);char *in=input.data(),*out=output.data();size_t left=input.size(),remaining=output.size();auto converter=iconv_open("UTF-8",encoding);
            if(converter==(iconv_t)-1)return QString::fromLatin1(input);
            while(left){if(iconv(converter,&in,&left,&out,&remaining)!=(size_t)-1)break;if(errno!=EILSEQ && errno!=EINVAL)break;*out++='?';--remaining;++in;--left;}iconv_close(converter);return QString::fromUtf8(output.constData(),out-output.data());};
        static const QString ansi=table("WINDOWS-1252"),ebcdic=table("IBM037");return QString((charset_==2?ansi:ebcdic)[byte]);
    }
    return dosGlyph(byte);
}
QString FileViewer::maskedGlyph(const QString &glyph) const
{
    if(!mask_ || junk_ || glyph.size()!=1)return glyph;
    int code=glyph[0].unicode();
    if((charset_==1 || charset_==2) && code>=32){for(int b=32;b<256;++b)if((charset_==1?dosGlyph(b):byteGlyph(b,false))==glyph){code=b;break;}}
    if(code<32 || (code>=128 && code<160))return " ";
    if(code>=160 && code<=255)return QString(QChar(code&127));
    return glyph;
}
void FileViewer::decode()
{
    if(charset_==0) {
        const auto encoding=QStringConverter::encodingForData(document_.bytes).value_or(QStringConverter::Utf8);
        QStringDecoder decoder(encoding);document_.text=decoder(document_.bytes);char16_t tail[8];const auto result=decoder.finalize(tail,8);
        if(decoder.hasError() || result.error!=QStringConverter::FinalizeResultError::NoError){document_.text=QString::fromLatin1(document_.bytes);document_.encoding="Latin-1 (fallback)";}
        else document_.encoding=QString::fromLatin1(QStringConverter::nameForEncoding(encoding));
    } else if(charset_==1) {
        document_.text.clear();for(const auto b:document_.bytes)document_.text+=quint8(b)<32?QString(QChar(quint8(b))):dosGlyph(quint8(b));document_.encoding="OEM CP437";
    } else if(charset_==2 || charset_==6) {
        const auto converter=iconv_open("UTF-8",charset_==2?"WINDOWS-1252":"IBM037");
        QByteArray output(document_.bytes.size()*4+8,Qt::Uninitialized);char *input=document_.bytes.data(),*out=output.data();size_t left=document_.bytes.size(),remaining=output.size();
        if(converter!=(iconv_t)-1) {
            while(left) { if(iconv(converter,&input,&left,&out,&remaining)!=(size_t)-1)break;if(errno!=EILSEQ && errno!=EINVAL)break;*out++='?';--remaining;++input;--left; }
            iconv_close(converter);
        }
        document_.text=QString::fromUtf8(output.constData(),out-output.data());document_.encoding=charset_==2?"ANSI Windows-1252":"EBCDIC IBM037";
    } else {
        const auto encoding=charset_==3?QStringConverter::Utf8:charset_==4?QStringConverter::Utf16LE:QStringConverter::Utf16BE;
        QStringDecoder decoder(encoding);document_.text=decoder(document_.bytes);document_.encoding=QString::fromLatin1(QStringConverter::nameForEncoding(encoding));
    }
    if(junk_) { QString clean;for(const auto b:document_.bytes)if(quint8(b)>=32 && quint8(b)<=126)clean+=QChar(quint8(b));document_.text=clean; }
    document_.text.replace("\r\n","\n");document_.text.replace('\r','\n');document_.lines.clear();document_.lines.append(0);
    for(qsizetype i=0;i<document_.text.size();++i)if(document_.text[i]=='\n' && i+1<document_.text.size())document_.lines.append(i+1);
    if(wrap_)rebuildWrap();
    clamp();lineCursor_=std::clamp<qsizetype>(lineCursor_,0,totalRows()-1);
}
void FileViewer::setFiles(const QStringList &paths)
{
    files_=paths;fileIndex_=std::max(0,int(paths.indexOf(path_)));
    if(archiveName(path_) && !rawArchive_){rawArchive_=true;if(loading_){pendingPath_=true;cancel_->store(true);}else reload();}
}
void FileViewer::setAutoview(bool enabled)
{
    autoview_=enabled;if(enabled && archiveName(path_) && !rawArchive_){rawArchive_=true;if(loading_){pendingPath_=true;cancel_->store(true);}else reload();}update();
}
void FileViewer::navigateFile(int index,bool continueSearch)
{
    if(index<0 || index>=files_.size()){status_="No more tagged files";return;}
    const auto search=batchSearch_.value_or(SearchOptions{query_,hex_?SearchMode::Hex:SearchMode::Unicode,caseSensitive_});
    const bool wasBackward=backward_,wasHex=hex_,wasWrap=wrap_,wasDump=dump_,wasJunk=junk_;setPath(files_[index]);fileIndex_=index;continueSearch_=continueSearch;continueBackward_=continueSearch && wasBackward;
    if(!search.query.isEmpty()) { setSearch(search);backward_=continueSearch && wasBackward;hex_=wasHex;wrap_=wasWrap;dump_=wasDump;junk_=wasJunk; }
}
void FileViewer::beginHexEdit()
{
    if(!editable_ || archiveListing_) { status_="Extract this member before editing";return; }
    if(document_.bytes.isEmpty()){status_="An empty file has no bytes to edit";return;}
    if(!snapshot_.valid){status_="Only regular files with safe paths can be edited";return;}
    reloadTimer_.stop();scrollTimer_.stop();editing_=true;savePrompt_=false;asciiEdit_=false;editNibble_=0;afterSave_=0;
    editOffset_=top_*16;editPage_=document_.bytes.mid(editOffset_,pageRows()*16);editCursor_=0;
}
void FileViewer::finishHexEdit(bool save)
{
    if(save && editPage_!=document_.bytes.mid(editOffset_,editPage_.size())) {
        const auto error=saveHexPage(path_,snapshot_,document_.bytes,editOffset_,editPage_);
        if(!error.isEmpty()){status_=error;savePrompt_=false;return;}
        document_.bytes.replace(editOffset_,editPage_.size(),editPage_);snapshot_=hexSnapshot(path_);decode();status_="Hex changes saved";
    }
    editing_=savePrompt_=false;editPage_.clear();top_+=afterSave_*pageRows();afterSave_=0;clamp();
}
void FileViewer::hexEditKey(QKeyEvent *event)
{
    const int key=event->key();
    if(savePrompt_) {
        if(key==Qt::Key_Y || key==Qt::Key_Return || key==Qt::Key_Enter)finishHexEdit(true);
        else if(key==Qt::Key_N){savePrompt_=false;afterSave_=0;}
        else if(key==Qt::Key_Escape){savePrompt_=false;afterSave_=0;}
        update();return;
    }
    if(key==Qt::Key_Escape){afterSave_=0;finishHexEdit(false);}
    else if(key==Qt::Key_Tab){asciiEdit_=!asciiEdit_;editNibble_=0;}
    else if(key==Qt::Key_F8){editPage_=document_.bytes.mid(editOffset_,editPage_.size());status_="Unsaved changes undone";}
    else if(key==Qt::Key_Return || key==Qt::Key_Enter || key==Qt::Key_PageDown || key==Qt::Key_PageUp){
        afterSave_=key==Qt::Key_PageDown?1:key==Qt::Key_PageUp?-1:0;
        if(editPage_==document_.bytes.mid(editOffset_,editPage_.size()))finishHexEdit(false);else savePrompt_=true;
    } else if(key==Qt::Key_Home){editCursor_=0;editNibble_=0;}
    else if(key==Qt::Key_End){editCursor_=editPage_.size()-1;editNibble_=asciiEdit_?0:1;}
    else if(key==Qt::Key_Up)editCursor_-=16;
    else if(key==Qt::Key_Down)editCursor_+=16;
    else if(key==Qt::Key_Left){if(asciiEdit_)--editCursor_;else if(editNibble_==1)editNibble_=0;else {--editCursor_;editNibble_=1;}}
    else if(key==Qt::Key_Right){if(asciiEdit_)++editCursor_;else if(editNibble_==0)editNibble_=1;else {++editCursor_;editNibble_=0;}}
    else if(!event->modifiers().testFlag(Qt::ControlModifier) && !event->modifiers().testFlag(Qt::AltModifier)) {
        for(const auto c:event->text()) {
            if(asciiEdit_) { if(c.unicode()>255){status_="Character must fit in one byte";break;}editPage_[editCursor_]=char(c.unicode());if(editCursor_+1<editPage_.size())++editCursor_; }
            else { bool ok=false;const int value=QString(c).toInt(&ok,16);if(!ok)continue;
                const int old=quint8(editPage_[editCursor_]);editPage_[editCursor_]=char(editNibble_?(old&240)|value:(old&15)|(value<<4));
                if(editNibble_==0)editNibble_=1;else {editNibble_=0;if(editCursor_+1<editPage_.size())++editCursor_;}
            }
        }
    }
    if(editing_)editCursor_=std::clamp<qsizetype>(editCursor_,0,editPage_.size()-1);
    update();
}
QString FileViewer::gatheredText() const
{
    const auto start=sourceLine(std::min(gatherStart_,gatherEnd_)),end=sourceLine(std::max(gatherStart_,gatherEnd_));
    const auto offset=document_.lines[start];const auto finish=lineEnd(end);
    return document_.text.mid(offset,finish-offset)+"\n";
}
void FileViewer::copyGather(bool append)
{
    if(!gather_) { if(!lineMode_){gather_=1;lineMode_=true;lineCursor_=top_;gatherStart_=gatherEnd_=top_;return;}gatherStart_=gatherEnd_=lineCursor_; }
    else if(gather_==1)gatherStart_=gatherEnd_=lineCursor_;
    else if(gather_==2)gatherEnd_=lineCursor_;
    const auto content=gatheredText();const auto clipboard=QGuiApplication::clipboard();QString previous=append?clipboard->text():QString{};if(!previous.isEmpty()){if(!previous.endsWith('\n'))previous+='\n';previous+=QString(gap_,'\n');}clipboard->setText(previous+content);
    gather_=0;status_=append?"Lines appended to clipboard":"Lines copied to clipboard";
}
void FileViewer::rememberQuery()
{
    if(query_.isEmpty())return;
    searchHistory_.removeAll(query_);searchHistory_.append(query_);while(searchHistory_.size()>255)searchHistory_.removeFirst();historyDirty_=true;
}
void FileViewer::loadViewHistory()
{
    const auto override=qEnvironmentVariable("LTREE_HISTORY_FILE");
    const QString path=(override.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/ltreec":QFileInfo(override).absolutePath())+"/viewer-history.json";
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return;
    const auto json=QJsonDocument::fromJson(file.read(1024*1024));
    for(const auto &item:json.object().value("search").toArray()){const auto value=item.toString();if(!value.isEmpty() && value.size()<=255 && !value.contains(QChar(0)) && !searchHistory_.contains(value))searchHistory_.append(value);}
    while(searchHistory_.size()>255)searchHistory_.removeFirst();
    for(const auto &item:json.object().value("gather").toArray()){const auto value=item.toString();if(!value.isEmpty() && value.size()<=4096 && !value.contains(QChar(0)) && !gatherHistory_.contains(value))gatherHistory_.append(value);}
    while(gatherHistory_.size()>64)gatherHistory_.removeFirst();
}
void FileViewer::saveViewHistory()
{
    const auto override=qEnvironmentVariable("LTREE_HISTORY_FILE");
    const QString directory=override.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/ltreec":QFileInfo(override).absolutePath();
    if(!QDir().mkpath(directory))return;
    QJsonArray entries,gather;for(const auto &entry:searchHistory_)entries.append(entry);for(const auto &entry:gatherHistory_)gather.append(entry);QSaveFile file(directory+"/viewer-history.json");
    if(file.open(QIODevice::WriteOnly)){file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"search",entries},{"gather",gather}}).toJson();if(file.write(bytes)==bytes.size())file.commit();}
}
void FileViewer::drawCommands(QPainter &p,int x,int y,const QString &label)
{
    bool key=false;for(const auto c:label){if(c=='['){key=true;continue;}if(c==']'){key=false;continue;}text(p,x++,y,QString(c),key?foreground():theme::labels,1);}
}
void FileViewer::drawExtras(QPainter &p)
{
    const int rows=height()/ch_,cols=width()/cw_;
    const int border=rows-4;p.fillRect(0,border*ch_+ch_/2,width(),1,theme::labels);
    const int span=std::clamp(int(pageRows()*qsizetype(cols)/std::max<qsizetype>(1,totalRows())),1,cols);const int marker=std::min(cols-span,int(top_*cols/std::max<qsizetype>(1,totalRows())));p.fillRect(marker*cw_,border*ch_+ch_/2-2,span*cw_,1,theme::labels);p.fillRect(marker*cw_,border*ch_+ch_/2+2,span*cw_,1,theme::labels);
    const int menu=modifiers_.testFlag(Qt::AltModifier)?2:modifiers_.testFlag(Qt::ControlModifier)?1:menuMode_;
    text(p,0,rows-3,"VIEW",theme::labels);text(p,0,rows-2,menu==1?"CTRL":menu==2?"ALT":"COMMANDS",theme::labels);
    if(menu==0) {
        drawCommands(p,10,rows-3,"[A]lpha [C]harset [D]ump [E]dit [G]ather [H]ex [J]unk [L]ine [M]ask");
        drawCommands(p,10,rows-2,QString("[O]ffset [R]uler [Tab] (%1) [W]rap Find:[/+] Backward:[\\-] Next:[Space] [Esc]").arg(tabWidth_));
    } else if(menu==1) {
        drawCommands(p,10,rows-3,"[C] Copy [F3] Auto reload [S] Previous search [0..9] Set bookmark");
        drawCommands(p,10,rows-2,"[Arrows] Scroll [Home/End] Horizontal limits [Insert] Copy lines");
    } else {
        drawCommands(p,10,rows-3,"[E] Edit [X] Shell [0..9] Bookmark [F5] Foreground [F6] Background");
        drawCommands(p,10,rows-2,"[F7] Maximize [F8] Width [F9] Height [Home/End] First/last tagged");
    }
    const QString mode=hex_?"HEX":dump_?"DUMP":junk_?"JUNK":wrap_?"WRAP":"ALPHA";
    drawCommands(p,0,rows-1,QString("[↑↓] Scroll [F3] Reload [F6] Gap (%1) [F8] S-Skip (%2)  %3 %4 (%5 mask)").arg(gap_).arg(skipPages_?"page":"hit",document_.encoding,mode,mask_?"yes":"no"));
    if(!files_.isEmpty())text(p,std::max(0,cols-26),0,QString("Tagged %1/%2 [N/P/U]").arg(fileIndex_+1).arg(files_.size()),theme::labels);
    if(!query_.isEmpty() && status_.isEmpty()) {
        const int length=std::min(int(query_.size())+6,std::max(1,cols-23));
        p.fillRect(0,border*ch_,length*cw_,ch_,background());
        text(p,0,border,"Find: "+query_,theme::labels,length);
    }
    p.fillRect(std::max(0,cols-20)*cw_,border*ch_,20*cw_,ch_,background());
    drawCommands(p,std::max(0,cols-20),border,"[F10] Menu [F1] Help");
    if(!status_.isEmpty()) {p.fillRect(0,(rows-1)*ch_,width(),ch_,background());text(p,0,rows-1,status_,theme::labels);}
    if(gather_) {
        p.fillRect(0,(rows-3)*ch_,width(),3*ch_,background());
        text(p,0,rows-3,gather_==1?"GATHER: select first line":gather_==2?"GATHER: select last line":"GATHER to file (append) or CLIP: "+input_,theme::labels);
        drawCommands(p,0,rows-2,QString("[Enter] Accept [↑↓/Home/End] Select [F2] Clear clipboard [F4] Copy [F5] Append [F6] Gap (%1)").arg(gap_));
        drawCommands(p,0,rows-1,status_.isEmpty()?"[Esc] Cancel":status_);
        if(gather_==3){const int x=std::min(cols-1,31+int(input_.size()));p.fillRect(x*cw_,(rows-3)*ch_,cw_,ch_,theme::selection);}
    }
    if(editing_) {
        const auto offset=editOffset_+editCursor_;const int value=quint8(editPage_[editCursor_]);
        const int x=asciiEdit_?60+int(editCursor_%16):10+int(editCursor_%16)*3+editNibble_,y=2+int(editCursor_/16);
        p.fillRect(x*cw_,y*ch_,cw_,ch_,theme::selection);text(p,x,y,asciiEdit_?dosGlyph(value):QString::number(editNibble_?value&15:value>>4,16).toUpper(),Qt::black,1);
        p.fillRect(0,(rows-3)*ch_,width(),3*ch_,background());
        text(p,0,rows-3,QString("EDIT HEX  Offset: %1  Hex: %2  Char: %3  Decimal: %4  %5").arg(offset,8,16,QChar('0')).arg(value,2,16,QChar('0')).arg(dosGlyph(value)).arg(value).arg(asciiEdit_?"ASCII":"HEX").toUpper(),theme::labels);
        drawCommands(p,0,rows-2,"[Tab] Hex/ASCII [Arrows] Move [Home/End] Page limits [F8] Undo [Enter] Finish [Esc] Discard");
        drawCommands(p,0,rows-1,savePrompt_?"Save changes to this file? [Y/N]":status_);
    }
    if(historyVisible_) {
        const int h=std::min(rows-5,int(historyEntries().size())+3),y=rows-h-3,x=std::min(8,cols/8);p.fillRect(x*cw_,y*ch_,(cols-x*2)*cw_,h*ch_,QColor(0,128,128));
        text(p,x+1,y,historyGather_?"GATHER FILE HISTORY":"VIEW SEARCH HISTORY",theme::normal);
        const int first=std::max(0,historyIndex_-(h-3));for(int i=first;i<historyEntries().size() && i-first<h-2;++i){if(i==historyIndex_)p.fillRect((x+1)*cw_,(y+1+i-first)*ch_,(cols-x*2-2)*cw_,ch_,theme::selection);text(p,x+1,y+1+i-first,historyEntries()[i],i==historyIndex_?QColor(Qt::black):theme::normal);}
        drawCommands(p,x+1,y+h-1,"[↑↓] Select [Enter] Retrieve [Del] Delete [Esc] Cancel");
    }
    if(help_) drawHelpDocument(p, rect(), helpDocument_, cw_, ch_, ascent_, &consoleFont_);
}
void FileViewer::keyReleaseEvent(QKeyEvent *event) { modifiers_=event->modifiers();if(event->key()==Qt::Key_Alt)modifiers_&=~Qt::AltModifier;if(event->key()==Qt::Key_Control)modifiers_&=~Qt::ControlModifier;update(); }
void FileViewer::focusOutEvent(QFocusEvent *) { modifiers_={};menuMode_=0;update(); }
bool FileViewer::extraKey(QKeyEvent *event)
{
    const int key=event->key();const auto mods=event->modifiers();const bool ctrl=mods.testFlag(Qt::ControlModifier),alt=mods.testFlag(Qt::AltModifier),shift=mods.testFlag(Qt::ShiftModifier);
    if(editing_){hexEditKey(event);return true;}
    if(historyVisible_) {
        if(key==Qt::Key_Escape)historyVisible_=false;
        else if(key==Qt::Key_Return || key==Qt::Key_Enter){input_=historyEntries().value(historyIndex_);historyVisible_=false;}
        else if(key==Qt::Key_Delete){historyEntries().removeAt(historyIndex_);historyDirty_=true;if(historyEntries().isEmpty())historyVisible_=false;historyIndex_=std::max(0,std::min(historyIndex_,int(historyEntries().size())-1));}
        else if(key==Qt::Key_Up)--historyIndex_;else if(key==Qt::Key_Down)++historyIndex_;else if(key==Qt::Key_PageUp)historyIndex_-=pageRows();else if(key==Qt::Key_PageDown)historyIndex_+=pageRows();else if(key==Qt::Key_Home)historyIndex_=0;else if(key==Qt::Key_End)historyIndex_=historyEntries().size()-1;
        historyIndex_=std::clamp(historyIndex_,0,std::max(0,int(historyEntries().size())-1));update();return true;
    }
    if(help_){help_=helpDocumentKey(helpDocument_,event,width()/cw_,height()/ch_);update();return true;}
    if(gather_) {
        if(gather_==3 && key==Qt::Key_F3){input_=gatherHistory_.value(gatherHistory_.size()-1);update();return true;}
        if(gather_==3 && (key==Qt::Key_Up || key==Qt::Key_Down) && !gatherHistory_.isEmpty()){historyGather_=true;historyVisible_=true;historyIndex_=gatherHistory_.size()-1;update();return true;}
        if(key==Qt::Key_Escape){gather_=0;update();return true;}
        if(key==Qt::Key_F2){QGuiApplication::clipboard()->clear();status_="Clipboard cleared";update();return true;}
        if(key==Qt::Key_F4 || key==Qt::Key_F5 || (ctrl && key==Qt::Key_C)){copyGather(key==Qt::Key_F5);update();return true;}
        if(key==Qt::Key_F6){gap_=(gap_+1)%4;update();return true;}
        if(key==Qt::Key_Return || key==Qt::Key_Enter) {
            if(gather_==1){gatherStart_=lineCursor_;gather_=2;}
            else if(gather_==2){gatherEnd_=lineCursor_;gather_=3;input_.clear();}
            else if(input_.compare("CLIP:",Qt::CaseInsensitive)==0){copyGather(false);}
            else if(input_.compare("PRN",Qt::CaseInsensitive)==0 || input_.compare("PRN:",Qt::CaseInsensitive)==0){QPrinter printer;QPrintDialog dialog(&printer,this);if(dialog.exec()==QDialog::Accepted){QTextDocument text;text.setDefaultFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));text.setPlainText(gatheredText());text.print(&printer);gather_=0;status_="Lines sent to printer";}else status_="Print cancelled";}
            else {const QString path=QDir(QFileInfo(path_).absolutePath()).absoluteFilePath(input_);QFile out(path);
                if(input_.isEmpty())status_="Enter an output file or CLIP:";
                else if(QFileInfo(path).isSymLink() || (QFileInfo::exists(path) && !QFileInfo(path).isFile()))status_="Output must be a regular file";
                else if(!out.open(QIODevice::ReadWrite|QIODevice::Append))status_=out.errorString();
                else {QByteArray prefix;if(out.size()>0){out.seek(out.size()-1);if(out.read(1)!="\n")prefix+='\n';prefix+=QByteArray(gap_,'\n');}const auto bytes=prefix+gatheredText().toUtf8();if(out.write(bytes)!=bytes.size() || !out.flush())status_=out.errorString();else{gather_=0;status_="Lines appended to "+path;gatherHistory_.removeAll(input_);gatherHistory_.append(input_);while(gatherHistory_.size()>64)gatherHistory_.removeFirst();historyDirty_=true;}}
            }
            update();return true;
        }
        if(gather_==3){if(key==Qt::Key_Backspace)input_.chop(1);else if(ctrl && key==Qt::Key_V)input_+=QGuiApplication::clipboard()->text().section('\n',0,0);else if(!ctrl && !alt)input_+=event->text();input_.truncate(4096);update();return true;}
    }
    if(searchPrompt_) {
        if((key==Qt::Key_Up || key==Qt::Key_Down) && !searchHistory_.isEmpty()){historyGather_=false;historyVisible_=true;historyIndex_=searchHistory_.size()-1;update();return true;}
        if(key==Qt::Key_F8){skipPages_=!skipPages_;update();return true;}
        if(key==Qt::Key_F4){promptMode_=nextSearchMode(promptMode_);update();return true;}
        return false;
    }
    if(offsetPrompt_)return false;
    if(key==Qt::Key_Control || key==Qt::Key_Alt || key==Qt::Key_Shift){modifiers_=mods;update();return true;}
    if(key==Qt::Key_F2 && !ctrl && !alt){QGuiApplication::clipboard()->clear();status_="Clipboard cleared";update();return true;}
    if(key==Qt::Key_F1){helpDocument_.open(HelpTopic::Viewer);help_=true;update();return true;}
    if(key==Qt::Key_F4 && !ctrl && !alt && !gather_){if(lineMode_)copyGather(false);else {gather_=1;lineMode_=true;lineCursor_=top_;gatherStart_=gatherEnd_=top_;}update();return true;}
    if(ctrl && key>=Qt::Key_0 && key<=Qt::Key_9){bookmarks_[key-Qt::Key_0]=top_;status_="Bookmark set";update();return true;}
    if(alt && key>=Qt::Key_0 && key<=Qt::Key_9){top_=bookmarks_[key-Qt::Key_0];clamp();update();return true;}
    if(alt && (key==Qt::Key_Home || key==Qt::Key_End) && !files_.isEmpty()){navigateFile(key==Qt::Key_Home?0:files_.size()-1);return true;}
    if(ctrl && key==Qt::Key_F3){if(reloadTimer_.isActive())reloadTimer_.stop();else reloadTimer_.start(1000);status_=reloadTimer_.isActive()?"Continuous reload enabled":"Continuous reload stopped";update();return true;}
    if(shift && key>=Qt::Key_F2 && key<=Qt::Key_F6){scrollTimer_.start(1200/(key-Qt::Key_F2+1));update();return true;}
    if(shift && key==Qt::Key_F7){scrollLoop_=!scrollLoop_;status_=scrollLoop_?"Auto scroll loop enabled":"Auto scroll loop disabled";update();return true;}
    if(alt && key==Qt::Key_F5){color_=(color_+1)%colors.size();update();return true;}
    if(alt && key==Qt::Key_F6){background_=(background_+1)%backgrounds.size();update();return true;}
    if(alt && key==Qt::Key_F7){if(window()->isMaximized())window()->showNormal();else window()->showMaximized();return true;}
    if(alt && (key==Qt::Key_F8 || key==Qt::Key_F9)){const bool horizontal=key==Qt::Key_F8;if(sizeBase_.isEmpty())sizeBase_=window()->size();const auto step=horizontal?widthStep_=(widthStep_+1)%3:heightStep_=(heightStep_+1)%3;const double ratio=step==1?0.75:step==2?0.5:1.0;if(window()->isMaximized())window()->showNormal();window()->resize(horizontal?std::max(640,int(sizeBase_.width()*ratio)):window()->width(),horizontal?window()->height():std::max(400,int(sizeBase_.height()*ratio)));return true;}
    if((alt || !ctrl) && key==Qt::Key_X){scrollTimer_.stop();emit executeRequested(QFileInfo(path_).absolutePath(),alt);return true;}
    if(alt && key==Qt::Key_E){if(hex_)beginHexEdit();else if(archiveListing_)status_="Use Enter in the file list to browse this archive";else if(editable_)emit alternateEditRequested(path_);else status_="Extract this member before editing";return true;}
    if(ctrl && (key==Qt::Key_C || key==Qt::Key_Insert)){if(!byteMode())copyGather(false);else status_="Gather is available in text modes";update();return true;}
    if(ctrl && key==Qt::Key_S){if(searchHistory_.size()>1){query_=query_==searchHistory_.last()?searchHistory_[searchHistory_.size()-2]:searchHistory_.last();hit_=-1;searchStart_=0;find(false);}return true;}
    if(!ctrl && !alt) {
        if(key==Qt::Key_C){charset_=(charset_+(shift?6:1))%7;decode();hit_=-1;update();return true;}
        if(key==Qt::Key_L && !byteMode()){lineMode_=!lineMode_;lineCursor_=top_;update();return true;}
        if(key==Qt::Key_M){mask_=!mask_;decode();update();return true;}
        if(key==Qt::Key_R){ruler_=!ruler_;update();return true;}
        if(key==Qt::Key_Tab){const std::array<int,5> widths{2,3,4,6,8};const auto i=std::find(widths.begin(),widths.end(),tabWidth_);tabWidth_=widths[(i-widths.begin()+1)%5];if(wrap_)rebuildWrap();update();return true;}
        if(key==Qt::Key_G && !byteMode()){gather_=1;lineMode_=true;lineCursor_=top_;gatherStart_=gatherEnd_=top_;update();return true;}
        if(key==Qt::Key_F5 && !byteMode()){copyGather(true);update();return true;}
        if(key==Qt::Key_F6){gap_=(gap_+1)%4;update();return true;}
        if(key==Qt::Key_F8){skipPages_=!skipPages_;update();return true;}
        if(key==Qt::Key_E && hex_){beginHexEdit();update();return true;}
        if(!files_.isEmpty() && (key==Qt::Key_N || key==Qt::Key_P)){navigateFile(fileIndex_+(key==Qt::Key_N?1:-1));return true;}
        if(!files_.isEmpty() && key==Qt::Key_U){emit untagRequested(path_);files_.removeAt(fileIndex_);if(files_.isEmpty())emit closed();else navigateFile(std::min(fileIndex_,int(files_.size())-1));return true;}
        if(mods.testFlag(Qt::KeypadModifier) && key==Qt::Key_5){if(lineMode_)top_=lineCursor_-pageRows()/2;else top_=totalRows()/2;clamp();update();return true;}
        if(shift && mods.testFlag(Qt::KeypadModifier) && key>=Qt::Key_1 && key<=Qt::Key_9){top_=totalRows()*(key-Qt::Key_0)/10;clamp();update();return true;}
    }
    if(ctrl && (key==Qt::Key_Home || key==Qt::Key_End)){left_=0;if(key==Qt::Key_End && !byteMode()){for(auto row=top_;row<std::min(totalRows(),top_+pageRows());++row)left_=std::max(left_,displayColumn(sourceLine(row),lineEnd(sourceLine(row))));left_=std::max<qsizetype>(0,left_-width()/cw_+1);}update();return true;}
    if(lineMode_ && !byteMode() && (key==Qt::Key_Up || key==Qt::Key_Down || key==Qt::Key_Home || key==Qt::Key_End || key==Qt::Key_PageUp || key==Qt::Key_PageDown)) {
        if(ctrl){top_+=(key==Qt::Key_Up?-1:key==Qt::Key_Down?1:key==Qt::Key_PageUp?-pageRows():pageRows());clamp();lineCursor_=std::clamp<qsizetype>(lineCursor_,top_,std::min(totalRows()-1,top_+pageRows()-1));}
        else if(key==Qt::Key_Home)lineCursor_=0;else if(key==Qt::Key_End)lineCursor_=totalRows()-1;
        else if(key==Qt::Key_PageUp)lineCursor_=lineCursor_>top_?top_:lineCursor_-pageRows();
        else if(key==Qt::Key_PageDown)lineCursor_=lineCursor_<std::min(totalRows()-1,top_+pageRows()-1)?std::min(totalRows()-1,top_+pageRows()-1):lineCursor_+pageRows();
        else lineCursor_+=(key==Qt::Key_Up?-1:1);
        lineCursor_=std::clamp<qsizetype>(lineCursor_,0,totalRows()-1);if(lineCursor_<top_)top_=lineCursor_;if(lineCursor_>=top_+pageRows())top_=lineCursor_-pageRows()+1;clamp();update();return true;
    }
    return false;
}
}
