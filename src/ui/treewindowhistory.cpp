#include "ui/treewindow.h"
#include "ui/theme.h"
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace ltree {
TreeWindow::PromptHistory &TreeWindow::promptHistory() { return stampPrompt_ ? stampHistory_ : graftPrompt_ ? graftHistory_ : filespecPrompt_ ? filespecHistory_ : searchHistory_; }
const TreeWindow::PromptHistory &TreeWindow::promptHistory() const { return stampPrompt_ ? stampHistory_ : graftPrompt_ ? graftHistory_ : filespecPrompt_ ? filespecHistory_ : searchHistory_; }
void TreeWindow::initializeHistories()
{
    const auto override = qEnvironmentVariable("LTREE_HISTORY_FILE");
    const auto config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/ltreec";
    searchHistory_.file = override.isEmpty() ? config + "/search-history.json" : override;
    searchHistory_.key = "search";searchHistory_.label = "Search";
    filespecHistory_.file = override.isEmpty() ? config + "/filespec-history.json" : QFileInfo(override).absolutePath() + "/filespec-history.json";
    filespecHistory_.key = "filespec";filespecHistory_.label = "Filespec";filespecHistory_.limit = 1024;
    if (QFileInfo::exists(searchHistory_.file)) loadHistory(searchHistory_);
    else if (override.isEmpty()) {
        const auto destination = searchHistory_.file;
        searchHistory_.file = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/ltreec/search-history.json";
        const bool imported = QFileInfo::exists(searchHistory_.file) && loadHistory(searchHistory_);
        searchHistory_.file = destination;
        // El archivo anterior se conserva como respaldo durante la importación.
        if (imported) { searchHistory_.dirty = true;saveHistory(searchHistory_); }
    }
    if (QFileInfo::exists(filespecHistory_.file)) loadHistory(filespecHistory_);
    graftHistory_.file = override.isEmpty() ? config + "/graft-history.json" : QFileInfo(override).absolutePath() + "/graft-history.json";
    graftHistory_.key = "graft";graftHistory_.label = "Graft";graftHistory_.limit = 4096;
    if (QFileInfo::exists(graftHistory_.file)) loadHistory(graftHistory_);
    stampHistory_.file = override.isEmpty() ? config + "/stamp-history.json" : QFileInfo(override).absolutePath() + "/stamp-history.json";
    stampHistory_.key="stamp";stampHistory_.label="New date";stampHistory_.limit=128;
    if(QFileInfo::exists(stampHistory_.file))loadHistory(stampHistory_);
}
void TreeWindow::rememberSearch(const QString &text) { rememberHistory(searchHistory_, text); }
void TreeWindow::trimHistory(PromptHistory &history)
{
    int unmarked=0;
    for(int i=0;i<history.entries.size();){
        if(!history.marks.contains(history.entries[i]) && ++unmarked>64)history.entries.removeAt(i);
        else ++i;
    }
}
void TreeWindow::rememberHistory(PromptHistory &history, const QString &text)
{
    if(text.isEmpty() || text.size()>history.limit || text.contains(QChar(0)) || text.contains('\n') || text.contains('\r'))return;
    history.entries.removeAll(text);history.entries.prepend(text);trimHistory(history);history.dirty=true;
}
bool TreeWindow::saveHistory(PromptHistory &history)
{
    if(!QDir().mkpath(QFileInfo(history.file).absolutePath())){status_=QString("Cannot create %1 history directory").arg(history.label.toLower());return false;}
    QJsonArray entries;
    for(const auto &text:history.entries)entries.append(QJsonObject{{"text",text},{"mark",history.marks.value(text)}});
    QSaveFile file(history.file);
    if(!file.open(QIODevice::WriteOnly)){status_=QString("Cannot save %1 history: ").arg(history.label.toLower())+file.errorString();return false;}
    file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
    const auto data=QJsonDocument(QJsonObject{{"version",1},{history.key,entries}}).toJson();
    if(file.write(data)!=data.size() || !file.commit()){status_=QString("Cannot save %1 history: ").arg(history.label.toLower())+file.errorString();return false;}
    history.dirty=false;return true;
}
bool TreeWindow::loadHistory(PromptHistory &history)
{
    QFile file(history.file);
    if(!file.exists()){status_=history.label + " history file does not exist";return false;}
    if(!file.open(QIODevice::ReadOnly)){status_=QString("Cannot load %1 history: ").arg(history.label.toLower())+file.errorString();return false;}
    QJsonParseError error;const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject() || document.object().value("version").toInt()!=1 || !document.object().value(history.key).isArray()){
        status_=QString("Invalid %1 history file").arg(history.label.toLower());return false;
    }
    QStringList texts;QMap<QString,QString> marks;QStringList usedMarks;
    for(const auto &entry:document.object().value(history.key).toArray()){
        if(!entry.isObject()){status_=QString("Invalid %1 history entry").arg(history.label.toLower());return false;}
        const auto object=entry.toObject();const auto text=object.value("text").toString();const auto mark=object.value("mark").toString();
        if(text.isEmpty() || text.size()>history.limit || text.contains(QChar(0)) || text.contains('\n') || text.contains('\r') || texts.contains(text) ||
           (!mark.isEmpty() && (mark.size()!=1 || (mark!="*" && !QString("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789").contains(mark)))) ||
           (mark!="*" && !mark.isEmpty() && usedMarks.contains(mark))){status_=QString("Invalid %1 history entry").arg(history.label.toLower());return false;}
        texts.append(text);if(!mark.isEmpty()){marks.insert(text,mark);usedMarks.append(mark);}
    }
    history.entries=texts;history.marks=marks;trimHistory(history);history.dirty=false;return true;
}
QStringList TreeWindow::historyItems() const
{
    const auto &history = promptHistory();
    QStringList items;
    for(auto i=history.entries.crbegin();i!=history.entries.crend();++i){
        const auto mark=history.marks.value(*i);
        if(history.filter==1 && mark.isEmpty())continue;
        if(history.filter==2 && (mark.isEmpty() || mark=="*"))continue;
        if(history.filter==3 && !mark.isEmpty())continue;
        items.append(*i);
    }
    if(history.sort){
        std::stable_sort(items.begin(),items.end(),[&](const QString &a,const QString &b){
            const auto first=history.sort<=2?history.marks.value(a):a;
            const auto second=history.sort<=2?history.marks.value(b):b;
            const int order=QString::compare(first,second,Qt::CaseInsensitive);
            return history.sort%2?order<0:order>0;
        });
    }
    return items;
}
QRect TreeWindow::historyRect() const {const int x=columns_/6;return QRect(x,4,columns_-x-1,rows_-5);}
void TreeWindow::keepHistoryVisible()
{
    auto &history = promptHistory();
    const int count=int(historyItems().size()),height=std::max(1,historyRect().height()-6);
    history.index=std::clamp(history.index,0,std::max(0,count-1));
    history.top=std::clamp(history.top,0,std::max(0,count-height));
    if(history.index<history.top)history.top=history.index;
    if(history.index>=history.top+height)history.top=history.index-height+1;
}
void TreeWindow::openHistory(bool newest)
{
    auto &history = promptHistory();
    history.visible=true;history.filter=history.sort=0;history.top=0;
    history.index=newest?std::max(0,int(history.entries.size())-1):0;
    keepHistoryVisible();update();
}
void TreeWindow::historyKey(QKeyEvent *event)
{
    auto &history = promptHistory();
    auto &input = stampPrompt_ ? stampInput_ : graftPrompt_ ? graftInput_ : filespecPrompt_ ? filespecInput_ : searchInput_;
    auto &cursor = stampPrompt_ ? stampCursor_ : graftPrompt_ ? graftCursor_ : filespecPrompt_ ? filespecCursor_ : searchCursor_;
    const int key=event->key();const bool ctrl=event->modifiers().testFlag(Qt::ControlModifier),alt=event->modifiers().testFlag(Qt::AltModifier);
    const auto items=historyItems();const auto selected=items.value(history.index);
    const int page=std::max(1,historyRect().height()-6);
    bool reselect=false;
    const auto retrieve=[&](const QString &text,bool execute=false){input=text;cursor=int(text.size());history.visible=false;if(stampPrompt_)setStampInput(text);if(graftPrompt_)graftSelected_=false;if(execute){
        if(stampPrompt_){QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);stampKey(&enter);}
        else if(graftPrompt_){QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);graftKey(&enter);}
        else if(filespecPrompt_){QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);filespecPromptKey(&enter);}
        else startSearch();
    }};
    if(key==Qt::Key_Escape)history.visible=false;
    else if(key==Qt::Key_Return || key==Qt::Key_Enter){if(!selected.isEmpty())retrieve(selected);}
    else if(key==Qt::Key_Up)--history.index;
    else if(key==Qt::Key_Down)++history.index;
    else if(key==Qt::Key_PageUp)history.index-=page;
    else if(key==Qt::Key_PageDown)history.index+=page;
    else if(key==Qt::Key_Home)history.index=0;
    else if(key==Qt::Key_End)history.index=std::max(0,int(items.size())-1);
    else if(key==Qt::Key_Left || key==Qt::Key_Right){
        auto &value=alt?history.sort:history.filter;const int count=alt?5:4;
        value=(value+(key==Qt::Key_Right?1:count-1))%count;reselect=true;
    }else if(key==Qt::Key_F3){if(saveHistory(history))status_=history.label + " history saved";}
    else if(key==Qt::Key_F4){if(loadHistory(history)){status_=history.label + " history loaded";reselect=true;}}
    else if(ctrl && (key==Qt::Key_C || key==Qt::Key_V || key==Qt::Key_X)){
        if(key==Qt::Key_C && !selected.isEmpty())QGuiApplication::clipboard()->setText(selected);
    }
    else if((key>=Qt::Key_A && key<=Qt::Key_Z) || (key>=Qt::Key_0 && key<=Qt::Key_9)){
        const auto mark=QString(QChar(key));QString marked;
        for(const auto &text:history.entries)if(history.marks.value(text)==mark){marked=text;break;}
        if(ctrl && !selected.isEmpty()){
            if(marked==selected){history.marks.remove(selected);reselect=true;}
            else if(!marked.isEmpty()){history.filter=0;history.index=historyItems().indexOf(marked);}
            else{history.marks.insert(selected,mark);reselect=true;}
            trimHistory(history);history.dirty=true;
        }else if(!ctrl && !marked.isEmpty())retrieve(marked,alt);
    }
    else if(!selected.isEmpty()){
        if(ctrl && key==Qt::Key_Insert)QGuiApplication::clipboard()->setText(selected);
        else if(key==Qt::Key_Insert){
            if(history.marks.contains(selected))history.marks.remove(selected);else history.marks.insert(selected,"*");
            trimHistory(history);history.dirty=true;reselect=true;
        }else if(key==Qt::Key_Delete){
            if(history.marks.contains(selected))status_="Unmark the item before deleting it";
            else{history.entries.removeAll(selected);history.dirty=true;}
        }else if(key==Qt::Key_Plus || key==Qt::Key_Bar){
            input+=selected.left(history.limit-input.size());cursor=int(input.size());if(graftPrompt_)graftSelected_=false;
        }
    }
    if(reselect){const int index=historyItems().indexOf(selected);if(index>=0)history.index=index;}
    keepHistoryVisible();update();
}
void TreeWindow::drawHistory(QPainter &p)
{
    auto &history = promptHistory();
    keepHistoryVisible();const auto box=historyRect();const auto items=historyItems();
    const QColor background(0,128,128),border(0,255,0);
    p.fillRect(box.x()*cellWidth_,box.y()*cellHeight_,box.width()*cellWidth_,box.height()*cellHeight_,background);
    const auto line=[&](int y){drawText(p,box.x(),y,"├"+QString(box.width()-2,QChar(u'─'))+"┤",border,box.width());};
    drawText(p,box.x(),box.top(),"┌"+QString(box.width()-2,QChar(u'─'))+"┐",border,box.width());
    drawText(p,box.x(),box.bottom(),"└"+QString(box.width()-2,QChar(u'─'))+"┘",border,box.width());
    for(int y=box.top()+1;y<box.bottom();++y){drawText(p,box.left(),y,"│",border,1);drawText(p,box.right(),y,"│",border,1);}
    const int footer=box.bottom()-4;
    for(int row=0;row<box.height()-6 && history.top+row<items.size();++row){
        const int index=history.top+row,y=box.top()+1+row;const auto text=items[index];
        if(index==history.index)p.fillRect((box.x()+1)*cellWidth_,y*cellHeight_,(box.width()-2)*cellWidth_,cellHeight_,theme::selection);
        drawText(p,box.x()+1,y,history.marks.value(text," ")+" "+text,index==history.index?Qt::black:theme::normal,box.width()-2);
    }
    if(items.isEmpty())drawText(p,box.x()+2,box.top()+1,"No history items",theme::normal,box.width()-3);
    line(footer);
    p.fillRect((box.x()+1)*cellWidth_,(footer+1)*cellHeight_,(box.width()-2)*cellWidth_,3*cellHeight_,theme::background);
    drawCommands(p,box.x()+1,footer+1,box.width()<100?"[Up/Down/PgUp/PgDn/Home/End] Move [Del] Delete [Ins] Mark":"[Up/Down/PgUp/PgDn/Home/End] Move [Left/Right] Filter [Del] Delete [Ins] Mark [Ctrl+A..Z/0..9] Bookmark",box.width()-2);
    drawCommands(p,box.x()+1,footer+2,"[Enter] Retrieve [+/|] Append [F3] Save [F4] Load [Esc] Cancel",box.width()-2);
    const auto label=QString("Displayed: %1  Sorted: %2").arg(QStringList{"All","Marked","Bookmarks","Unmarked"}[history.filter],QStringList{"none","mark ascending","mark descending","text ascending","text descending"}[history.sort]);
    if(!status_.isEmpty())drawText(p,box.x()+1,footer+3,status_,theme::labels,box.width()-2);
    else if(box.width()<100)drawCommands(p,box.x()+1,footer+3,QString("%1/%2 [Left/Right] Filter [Alt+Left/Right] Sort").arg(QStringList{"All","Marked","Bookmarks","Unmarked"}[history.filter],QStringList{"none","mark+","mark-","text+","text-"}[history.sort]),box.width()-2);
    else drawCommands(p,box.x()+1,footer+3,label+"  [Alt+Left/Right] Sort",box.width()-2);
}
}
