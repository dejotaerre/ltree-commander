#include "ui/archivewindow.h"
#include <QWheelEvent>
#include "ui/theme.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QKeyEvent>
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QClipboard>
#include <QGuiApplication>
#include <QUuid>
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
namespace ltree {
ArchiveWindow::ArchiveWindow(QString path,QString destination,QWidget *parent):QWidget(parent),path_(QFileInfo(path).canonicalFilePath().isEmpty()?path:QFileInfo(path).canonicalFilePath()),destination_(std::move(destination)) { initialize();load(); }
ArchiveWindow::ArchiveWindow(QStringList sources,QString base,QString destination,QWidget *parent):QWidget(parent),destination_(std::move(destination)),base_(std::move(base)),sources_(std::move(sources)),create_(true)
{
    initialize();prompt_=Prompt::Create;setInput(QDir(destination_).filePath((sources_.size()==1?QFileInfo(sources_.first()).completeBaseName():QFileInfo(base_).fileName())+".zip"),true);
    for(const auto &path:sources_)rows_.append({path,QDir(base_).relativeFilePath(path),false,-1,QFileInfo(path).size(),QFileInfo(path).lastModified().toSecsSinceEpoch()});
}
ArchiveWindow::~ArchiveWindow(){if(cancel_)cancel_->store(true);if(viewer_)delete viewer_;memberWatcher_.waitForFinished();searchWatcher_.waitForFinished();}
void ArchiveWindow::initialize()
{
    inputEditor_.setMaxLength(4096);
    setFocusPolicy(Qt::StrongFocus);setAttribute(Qt::WA_OpaquePaintEvent);progress_=std::make_shared<std::atomic<int>>(0);
    timer_.setInterval(80);connect(&timer_,&QTimer::timeout,this,[this]{spinner_=(spinner_+1)%4;update();});
    connect(&catalogWatcher_,&QFutureWatcher<ArchiveCatalog>::finished,this,[this]{busy_=false;timer_.stop();catalog_=catalogWatcher_.result();status_=catalog_.cancelled?"Archive read cancelled":catalog_.error;QSet<int> valid;for(const auto &entry:catalog_.entries)valid.insert(entry.index);tags_.intersect(valid);rebuild();update();});
    connect(&operationWatcher_,&QFutureWatcher<ArchiveResult>::finished,this,[this]{busy_=false;timer_.stop();const auto result=operationWatcher_.result();status_=QString("%1: %2 completed, %3 failed%4").arg(result.cancelled?"Cancelled":create_?archiveFormatName(format_)+" creation":"Extraction").arg(result.completed).arg(result.failed).arg(result.error.isEmpty()?QString{}:" — "+result.error);prompt_=Prompt::None;update();});
    connect(&memberWatcher_,&QFutureWatcher<ArchiveBytes>::finished,this,[this]{
        busy_=false;timer_.stop();const auto result=memberWatcher_.result();if(!unchanged()){update();return;}if(result.cancelled){status_="Read cancelled";update();return;}if(!result.error.isEmpty()){status_=result.error;update();return;}
        const auto row=rows_.value(cursor_);const QString file=temporary_.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+"."+QFileInfo(row.path).suffix());QFile out(file);
        if(!out.open(QIODevice::WriteOnly) || out.write(result.bytes)!=result.bytes.size()){status_="Cannot prepare member view";update();return;}out.close();
        viewer_=new FileViewer(file,this);viewer_->setDisplayName(path_+"::"+row.path);viewer_->setEditable(false);if(!search_.query.isEmpty())viewer_->setSearch(search_);viewer_->setGeometry(rect());
        connect(viewer_,&FileViewer::closed,this,[this]{if(viewer_){viewer_->hide();viewer_->deleteLater();viewer_=nullptr;}setFocus();update();});viewer_->show();viewer_->raise();viewer_->setFocus();
    });
    connect(&searchWatcher_,&QFutureWatcher<SearchResult>::finished,this,[this]{busy_=false;timer_.stop();const auto result=searchWatcher_.result();if(!result.cancelled)tags_=result.found;status_=QString("Search %1: %2 hits, %3 errors%4").arg(result.cancelled?"cancelled":"completed").arg(result.found.size()).arg(result.failed).arg(result.error.isEmpty()?QString{}:" — "+result.error);update();});
}
bool ArchiveWindow::unchanged()
{
    if(!snapshot_.valid || snapshot_!=hexSnapshot(path_)){status_="Archive changed outside LTree. Press F3 to reload";return false;}return true;
}
void ArchiveWindow::load()
{
    snapshot_=hexSnapshot(path_);
    cancel_=std::make_shared<std::atomic_bool>(false);busy_=true;status_="Reading archive";timer_.start();const auto path=path_,password=password_;const auto cancel=cancel_;
    catalogWatcher_.setFuture(QtConcurrent::run([path,password,cancel]{return listArchive(path,cancel,password);}));
}
void ArchiveWindow::rebuild()
{
    rows_.clear();QSet<QString> directories;
    for(const auto &entry:catalog_.entries) {
        if(!entry.path.startsWith(folder_))continue;
        const QString relative=entry.path.mid(folder_.size());if(relative.isEmpty())continue;
        if(!branch_ && relative.contains('/')) {
            const QString child=relative.section('/',0,0),full=folder_+child+"/";
            if(!directories.contains(full)){directories.insert(full);rows_.append({full,child,true,-1,0,0});}continue;
        }
        if(entry.directory){const QString full=entry.path.endsWith('/')?entry.path:entry.path+'/';if(!directories.contains(full)){directories.insert(full);rows_.append({full,relative,true,entry.index,0,entry.modified});}}
        else if(filespec_.matches(QFileInfo(entry.path).fileName()))rows_.append({entry.path,relative,false,entry.index,entry.size,entry.modified});
    }
    std::stable_sort(rows_.begin(),rows_.end(),[](const Row &a,const Row &b){return a.directory!=b.directory?a.directory>b.directory:QString::compare(a.name,b.name,Qt::CaseInsensitive)<0;});clamp();
}
void ArchiveWindow::clamp(){cursor_=std::clamp(cursor_,0,std::max(0,int(rows_.size())-1));if(cursor_<top_)top_=cursor_;if(cursor_>=top_+pageRows())top_=cursor_-pageRows()+1;top_=std::max(0,top_);}
void ArchiveWindow::text(QPainter &p,int x,int y,const QString &value,QColor color,int limit)
{
    if(limit<0)limit=width()/8-x;
    int col=0;for(const char32_t code:value.toUcs4()){if(col>=limit)break;const QString glyph=QString::fromUcs4(&code,1);font_.draw(p,(x+col)*8,y*16,glyph,color);++col;}
}
void ArchiveWindow::commands(QPainter &p,int x,int y,const QString &value){bool shortcut=false;for(const auto c:value){if(c=='['){shortcut=true;continue;}if(c==']'){shortcut=false;continue;}text(p,x++,y,QString(c),shortcut?theme::normal:theme::labels,1);}}
void ArchiveWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);p.fillRect(rect(),theme::background);const int cols=width()/8,lines=height()/16;
    text(p,0,0,create_?"CREATE "+archiveFormatName(format_)+": "+base_:path_+":/"+folder_,theme::labels);text(p,0,1,create_?"<tagged files>" : QString("<%1: %2> %3").arg(branch_?"branch":"archive",filespec_.text(),catalog_.format),theme::labels);
    for(int y=0;y<pageRows() && top_+y<rows_.size();++y){const int i=top_+y;const auto &row=rows_[i];const bool selected=i==cursor_;if(selected)p.fillRect(0,(y+2)*16,width(),16,theme::selection);
        const QColor color=selected?QColor(Qt::black):row.directory?theme::normal:theme::fileColor(row.name);
        text(p,0,y+2,selected?"►":tags_.contains(row.index)?"◆":row.directory?"+":" ",selected?QColor(Qt::black):theme::labels,1);
        text(p,2,y+2,row.name,color,std::max(1,cols-40));text(p,std::max(20,cols-36),y+2,row.directory?"<DIR>":QString::number(row.size).rightJustified(12),color,12);
        if(row.modified)text(p,std::max(32,cols-22),y+2,QDateTime::fromSecsSinceEpoch(row.modified).toString("yyyy-MM-dd HH:mm:ss"),color,19);
    }
    p.fillRect(0,(lines-4)*16+8,width(),1,theme::labels);text(p,std::max(0,cols-15),lines-4,QString("%1/%2").arg(rows_.isEmpty()?0:cursor_+1).arg(rows_.size()),theme::labels);
    text(p,0,lines-3,"ARCHIVE",theme::labels);text(p,0,lines-2,"COMMANDS",theme::labels);
    const int menu=modifiers_.testFlag(Qt::AltModifier)?2:modifiers_.testFlag(Qt::ControlModifier)?1:menu_;
    commands(p,10,lines-3,menu==1?"[C/E] Extract tagged [S] Search tagged [T] Tag all [U] Untag all":menu==2?"[C/E] Extract tagged paths":"[B]ranch [C]opy [E]xtract [F]ilespec [T]ag [U]ntag [V]iew [F3] Reload");
    commands(p,10,lines-2,"[Enter] Directory/View [Backspace] Parent [F4] Menu [F1] Help [Esc] Return");
    text(p,0,lines-1,status_.isEmpty()?QString("%1 tagged files — members are read only; extract to edit").arg(tags_.size()):status_,theme::labels);
    if(prompt_!=Prompt::None){p.fillRect(0,(lines-3)*16,width(),48,theme::background);
        const QString label=prompt_==Prompt::Replace?"Replace existing destination files? [Y/N]":prompt_==Prompt::Extract?"EXTRACT to: ":prompt_==Prompt::Create?"CREATE "+archiveFormatName(format_)+" as: ":prompt_==Prompt::Search?"SEARCH tagged members for: ":prompt_==Prompt::Filespec?"FILESPEC: ":"ARCHIVE password: ";
        if(prompt_==Prompt::Replace)commands(p,0,lines-3,label);else {
            const auto input=prompt_==Prompt::Password?QString(input_.size(),'*'):input_;const auto glyphs=input.toUcs4();
            const int position=input.left(inputEditor_.cursorPosition()).toUcs4().size();const int capacity=std::max(1,cols-int(label.size()));
            const int start=std::max(0,position-capacity+1),selection=inputEditor_.selectionStart();
            const int from=selection<0?-1:input.left(selection).toUcs4().size(),to=selection<0?-1:input.left(selection+inputEditor_.selectedText().size()).toUcs4().size();
            text(p,0,lines-3,label,theme::labels);
            for(int col=0;col<capacity && start+col<glyphs.size();++col){const bool selected=from>=0 && start+col>=from && start+col<to;
                if(selected)p.fillRect((label.size()+col)*8,(lines-3)*16,8,16,theme::selection);
                const char32_t glyph=glyphs[start+col];text(p,label.size()+col,lines-3,QString::fromUcs4(&glyph,1),selected?QColor(Qt::black):theme::labels,1);
            }
            const int x=std::min(cols-1,int(label.size())+position-start);p.fillRect(x*8,(lines-3)*16,8,16,theme::selection);
            if(position<glyphs.size()){const char32_t glyph=glyphs[position];text(p,x,lines-3,QString::fromUcs4(&glyph,1),Qt::black,1);}
        }
        if(prompt_==Prompt::Search)commands(p,0,lines-2,QString("[F2] Case (%1) [F4] Mode (%2) [Enter] Search [Esc] Cancel").arg(search_.caseSensitive?"yes":"no",searchModeName(search_.mode)));
        else commands(p,0,lines-2,QString("[Enter] Accept [Esc] Cancel [F2] Paths (%1) %2[F4] Replace (%3)").arg(paths_?"yes":"no",prompt_==Prompt::Create?"[F3] Format ("+archiveFormatName(format_)+") ":QString{},replace_?"yes":"no"));
        text(p,0,lines-1,status_,theme::labels);
    }
    if(formatPicker_){
        const int x=std::max(0,(cols-48)/2),y=std::max(0,(lines-13)/2),w=std::min(48,cols),h=std::min(13,lines);
        p.fillRect(x*8,y*16,w*8,h*16,theme::background);p.setPen(theme::labels);p.drawRect(x*8,y*16,w*8-1,h*16-1);
        text(p,x+2,y+1,"CREATE ARCHIVE — FORMAT",theme::labels,w-4);
        for(int i=0;i<9 && i+2<h-1;++i){const bool selected=i==formatCursor_;if(selected)p.fillRect((x+1)*8,(y+i+2)*16,(w-2)*8,16,theme::selection);
            const auto format=ArchiveFormat(i);text(p,x+2,y+i+2,archiveFormatName(format)+(archiveStreamFormat(format)?"  (one file)":"  (multiple files)"),selected?QColor(Qt::black):theme::normal,w-4);}
        commands(p,x+2,y+h-1,"[Up/Down] Choose [Enter] Accept [Esc] Cancel");
    }
    if(busy_){p.fillRect(0,(lines-3)*16,width(),48,theme::background);text(p,0,lines-3,QString("%1 %2").arg(QString("|/-\\")[spinner_]).arg(status_),theme::labels);text(p,0,lines-2,QString("Members processed: %1").arg(progress_->load()),theme::labels);commands(p,0,lines-1,"[Esc] Cancel current operation");}
    if(help_) drawHelpDocument(p,rect(),helpDocument_,8,16,13,&font_);
}
void ArchiveWindow::wheelEvent(QWheelEvent *event)
{
    if(help_){helpDocumentScroll(helpDocument_,event->angleDelta().y()/120,width()/8,height()/16);update();}
}
void ArchiveWindow::resizeEvent(QResizeEvent *){if(viewer_)viewer_->setGeometry(rect());clamp();}
void ArchiveWindow::keyReleaseEvent(QKeyEvent *event){modifiers_=event->modifiers();if(event->key()==Qt::Key_Alt)modifiers_&=~Qt::AltModifier;if(event->key()==Qt::Key_Control)modifiers_&=~Qt::ControlModifier;update();}
void ArchiveWindow::browseMember()
{
    if(rows_.isEmpty())return;
    if(!unchanged())return;
    const auto &row=rows_[cursor_];if(row.directory){folder_=row.path;branch_=false;cursor_=top_=0;rebuild();return;}
    cancel_=std::make_shared<std::atomic_bool>(false);busy_=true;status_="Reading "+row.path;timer_.start();const auto path=path_,password=password_;const auto cancel=cancel_;const int index=row.index;
    memberWatcher_.setFuture(QtConcurrent::run([path,index,password,cancel]{return readArchiveMember(path,index,cancel,password);}));
}
void ArchiveWindow::beginExtract(bool tagged,bool paths)
{
    if(rows_.isEmpty())return;
    if(!unchanged())return;
    selected_.clear();paths_=paths;
    if(tagged)selected_=tags_;
    else {const auto row=rows_[cursor_];if(row.directory){if(row.index>=0)selected_.insert(row.index);for(const auto &entry:catalog_.entries)if(entry.path==row.path || entry.path.startsWith(row.path))selected_.insert(entry.index);paths_=true;}else selected_.insert(row.index);}
    if(selected_.isEmpty()){status_="No tagged archive files";return;}
    prompt_=Prompt::Extract;setInput(destination_,true);replace_=false;
}
void ArchiveWindow::startOperation()
{
    if(!create_ && !unchanged()){prompt_=Prompt::None;return;}
    if(!QDir::isAbsolutePath(input_))input_=QDir(create_?base_:QFileInfo(path_).absolutePath()).absoluteFilePath(input_);
    input_=QDir::cleanPath(input_);if(input_.contains(QChar(0))){status_="Invalid path";return;}
    if(create_){
        ArchiveFormat detected;if(archiveFormatFromPath(input_,detected))format_=detected;
        else {const QString suffix=QFileInfo(input_).suffix();if(suffix.isEmpty())input_+=archiveFormatSuffix(format_);else {status_="Choose F3 Format or use a supported archive extension";setInput(input_);return;}}
        setInput(input_);
        if(archiveStreamFormat(format_) && sources_.size()!=1){status_="GZ/BZ2/XZ compress one file. Choose a TAR format for multiple files";return;}
    }
    if((create_ && QFileInfo::exists(input_)) || (!create_ && replace_)){prompt_=Prompt::Replace;return;}
    cancel_=std::make_shared<std::atomic_bool>(false);busy_=true;timer_.start();progress_->store(0);prompt_=Prompt::None;
    const auto cancel=cancel_;const auto path=path_,password=password_,destination=input_,base=base_;const auto selected=selected_;const auto sources=sources_;const auto format=format_;const bool paths=paths_,replace=replace_,create=create_;const auto progress=progress_;
    status_=create?"Creating "+archiveFormatName(format_):"Extracting archive members";
    operationWatcher_.setFuture(QtConcurrent::run([=]{return create?createArchive(sources,base,destination,format,paths,replace,cancel,[progress](int n){progress->store(n);}):extractArchive(path,selected,destination,paths,replace,cancel,password,[progress](int n){progress->store(n);});}));
}
void ArchiveWindow::startSearch()
{
    if(!unchanged()){prompt_=Prompt::None;return;}
    const QString validation=validateSearch(search_);if(!validation.isEmpty()){status_=validation;return;}if(tags_.isEmpty()){status_="Tag files before searching";return;}
    prompt_=Prompt::None;cancel_=std::make_shared<std::atomic_bool>(false);busy_=true;timer_.start();progress_->store(0);status_="Searching archive: "+search_.query;
    const auto path=path_,password=password_;const auto selected=tags_;const auto options=search_;const auto cancel=cancel_;const auto progress=progress_;const QString directory=temporary_.path();
    searchWatcher_.setFuture(QtConcurrent::run([=]{
        SearchResult result;auto archive=archive_read_new();configureArchiveReader(archive,path);if(!password.isEmpty())archive_read_add_passphrase(archive,password.toUtf8().constData());
        if(archive_read_open_filename(archive,QFile::encodeName(path).constData(),65536)!=ARCHIVE_OK){result.error=QString::fromUtf8(archive_error_string(archive));archive_read_free(archive);return result;}
        struct archive_entry *entry=nullptr;int index=0,state=ARCHIVE_OK;
        while(!cancel->load() && (state=archive_read_next_header(archive,&entry))==ARCHIVE_OK){const int current=index++;if(!selected.contains(current)){archive_read_data_skip(archive);continue;}
            if(archive_entry_filetype(entry)!=AE_IFREG || archive_entry_symlink(entry) || archive_entry_hardlink(entry)){++result.failed;archive_read_data_skip(archive);continue;}
            const QString file=QDir(directory).filePath("search-"+QUuid::createUuid().toString(QUuid::WithoutBraces));QFile out(file);bool ok=out.open(QIODevice::WriteOnly|QIODevice::NewOnly);char buffer[65536];la_ssize_t n=0;
            while(ok && !cancel->load() && (n=archive_read_data(archive,buffer,sizeof(buffer)))>0)ok=out.write(buffer,n)==n;
            if(n<0)ok=false;
            out.close();
            if(ok && !cancel->load()){const auto match=searchFile(file,options,cancel);if(match.outcome==SearchOutcome::Found)result.found.insert(current);else if(match.outcome==SearchOutcome::Error){++result.failed;if(result.error.isEmpty())result.error=match.error;}}
            else if(!cancel->load()){++result.failed;if(result.error.isEmpty())result.error=n<0?QString::fromUtf8(archive_error_string(archive)):"Cannot read archive member";}
            QFile::remove(file);progress->fetch_add(1);
        }
        result.cancelled=cancel->load();if(!result.cancelled && state!=ARCHIVE_EOF && result.error.isEmpty())result.error=QString::fromUtf8(archive_error_string(archive));archive_read_free(archive);return result;
    }));
}
void ArchiveWindow::promptKey(QKeyEvent *event)
{
    const int key=event->key();
    if(key==Qt::Key_Escape){prompt_=Prompt::None;return;}
    if(prompt_==Prompt::Replace){
        if(key==Qt::Key_N){prompt_=create_?Prompt::Create:Prompt::Extract;replace_=false;}
        else if(key==Qt::Key_Y){if(!create_ && !unchanged()){prompt_=Prompt::None;return;}replace_=true;prompt_=Prompt::None;const auto input=input_;const bool create=create_;const bool savedReplace=replace_;
            // La confirmación se consume una vez, sin volver a abrir el mismo prompt.
            cancel_=std::make_shared<std::atomic_bool>(false);busy_=true;timer_.start();status_=create?"Creating "+archiveFormatName(format_):"Extracting archive members";const auto cancel=cancel_;const auto sources=sources_;const auto format=format_;const auto selected=selected_;const auto path=path_,base=base_,password=password_;const bool paths=paths_;const auto progress=progress_;progress->store(0);
            operationWatcher_.setFuture(QtConcurrent::run([=]{return create?createArchive(sources,base,input,format,paths,savedReplace,cancel,[progress](int n){progress->store(n);}):extractArchive(path,selected,input,paths,savedReplace,cancel,password,[progress](int n){progress->store(n);});}));create_=create;
        }return;
    }
    if(key==Qt::Key_F1){helpDocument_.open(HelpTopic::Archives);help_=true;return;}
    if(key==Qt::Key_F3 && prompt_==Prompt::Create){formatPicker_=true;formatCursor_=int(format_);return;}
    if(key==Qt::Key_F2){if(prompt_==Prompt::Search)search_.caseSensitive=!search_.caseSensitive;else paths_=!paths_;return;}
    if(key==Qt::Key_F4){if(prompt_==Prompt::Search)search_.mode=nextSearchMode(search_.mode);else replace_=!replace_;return;}
    if(key==Qt::Key_Return || key==Qt::Key_Enter){
        if(prompt_==Prompt::Filespec){QString error;if(filespec_.set(input_.isEmpty()?"*.*":input_,&error)){prompt_=Prompt::None;cursor_=top_=0;rebuild();}else status_=error;}
        else if(prompt_==Prompt::Search){search_.query=input_;startSearch();}
        else if(prompt_==Prompt::Password){password_=input_;prompt_=Prompt::None;load();}
        else if(input_.isEmpty())status_="Enter a destination path";else startOperation();
        return;
    }
    QCoreApplication::sendEvent(&inputEditor_,event);input_=inputEditor_.text();
}
void ArchiveWindow::keyPressEvent(QKeyEvent *event)
{
    const int key=event->key();modifiers_=event->modifiers();
    if(formatPicker_){
        if(key==Qt::Key_Escape)formatPicker_=false;
        else if(key==Qt::Key_Up)formatCursor_=(formatCursor_+8)%9;
        else if(key==Qt::Key_Down)formatCursor_=(formatCursor_+1)%9;
        else if(key==Qt::Key_Home)formatCursor_=0;
        else if(key==Qt::Key_End)formatCursor_=8;
        else if(key==Qt::Key_Return || key==Qt::Key_Enter){chooseFormat(ArchiveFormat(formatCursor_));formatPicker_=false;}
        update();return;
    }
    if(busy_){if(key==Qt::Key_Escape){cancel_->store(true);status_="Cancelling…";}update();return;}
    if(help_){help_=helpDocumentKey(helpDocument_,event,width()/8,height()/16);update();return;}
    status_.clear();if(prompt_!=Prompt::None){promptKey(event);update();return;}
    if(key==Qt::Key_Control || key==Qt::Key_Alt){update();return;}
    if(key==Qt::Key_F4){menu_=(menu_+1)%3;update();return;}
    const int menu=menu_;menu_=0;const bool ctrl=event->modifiers().testFlag(Qt::ControlModifier) || (menu==1 && (key==Qt::Key_C || key==Qt::Key_E || key==Qt::Key_S || key==Qt::Key_T || key==Qt::Key_U));
    const bool alt=event->modifiers().testFlag(Qt::AltModifier) || (menu==2 && (key==Qt::Key_C || key==Qt::Key_E));
    if(key==Qt::Key_Escape){if(menu){update();return;}emit closed();return;}
    if(key==Qt::Key_F1){helpDocument_.open(HelpTopic::Archives);help_=true;}
    else if(key==Qt::Key_F9){prompt_=Prompt::Password;setInput({});}
    else if(create_)status_="Archive operation finished. Esc returns to the file list";
    else if(key==Qt::Key_C || key==Qt::Key_E)beginExtract(ctrl || alt,alt);
    else if(ctrl && key==Qt::Key_S){prompt_=Prompt::Search;setInput({});}
    else if(key==Qt::Key_T || key==Qt::Key_U){
        if(ctrl){for(const auto &row:rows_)if(!row.directory && row.index>=0){if(key==Qt::Key_T)tags_.insert(row.index);else tags_.remove(row.index);}}
        else if(!rows_.isEmpty() && !rows_[cursor_].directory){if(key==Qt::Key_T)tags_.insert(rows_[cursor_].index);else tags_.remove(rows_[cursor_].index);++cursor_;}
    }
    else if(key==Qt::Key_F){prompt_=Prompt::Filespec;setInput(filespec_.text(),true);}
    else if(key==Qt::Key_B){branch_=!branch_;cursor_=top_=0;rebuild();}
    else if(key==Qt::Key_F3)load();
    else if(key==Qt::Key_Backspace || key==Qt::Key_Left){if(!folder_.isEmpty()){folder_=folder_.left(folder_.size()-1).section('/',0,-2);if(!folder_.isEmpty())folder_+='/';cursor_=top_=0;rebuild();}else emit closed();}
    else if(key==Qt::Key_Return || key==Qt::Key_Enter || key==Qt::Key_V)browseMember();
    else if(key==Qt::Key_Up)--cursor_;else if(key==Qt::Key_Down)++cursor_;else if(key==Qt::Key_PageUp)cursor_-=pageRows();else if(key==Qt::Key_PageDown)cursor_+=pageRows();else if(key==Qt::Key_Home)cursor_=0;else if(key==Qt::Key_End)cursor_=rows_.size()-1;
    clamp();update();
}
void ArchiveWindow::setInput(const QString &value,bool selected)
{
    inputEditor_.setText(value);inputEditor_.setCursorPosition(value.size());if(selected)inputEditor_.selectAll();input_=inputEditor_.text();
}
void ArchiveWindow::chooseFormat(ArchiveFormat format)
{
    QString name=input_;ArchiveFormat previous;
    if(archiveFormatFromPath(name,previous)){
        const QString suffix=archiveFormatSuffix(previous);
        if(name.endsWith(suffix,Qt::CaseInsensitive))name.chop(suffix.size());else name.chop(QFileInfo(name).suffix().size()+1);
    }
    if(archiveStreamFormat(format) && sources_.size()==1 && QFileInfo(name).fileName()==QFileInfo(sources_.first()).completeBaseName())
        name=QDir(QFileInfo(name).absolutePath()).filePath(QFileInfo(sources_.first()).fileName());
    format_=format;setInput(name+archiveFormatSuffix(format_));
}

}
