#include "ui/treewindow.h"
#include "ui/archivewindow.h"
#include "fs/archive.h"
#include "fs/hexedit.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QClipboard>
#include <QJsonDocument>
#include <QProcess>
#include <archive.h>
#include <archive_entry.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace ltree;
class ViewArchiveTests:public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> fixture;QString root;QByteArray previous_;bool set_=false;
    Cancellation cancel(){return std::make_shared<std::atomic_bool>(false);}
    void write(const QString &path,const QByteArray &bytes){QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),bytes.size());}
    QByteArray read(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
    void capture(QWidget &widget,const QString &name){const auto directory=qEnvironmentVariable("LTREE_CAPTURE_DIR");if(!directory.isEmpty()){QVERIFY(QDir().mkpath(directory));QVERIFY(widget.grab().save(directory+'/'+name+".png"));}}
    void archiveFixture(const QString &path,const QVector<QPair<QString,QByteArray>> &files,bool tar=false,bool symlink=false){
        auto a=archive_write_new();if(tar)archive_write_set_format_pax_restricted(a);else {archive_write_set_format_zip(a);archive_write_set_options(a,"zip:compression=store");}
        QCOMPARE(archive_write_open_filename(a,QFile::encodeName(path).constData()),ARCHIVE_OK);
        for(const auto &file:files){auto e=archive_entry_new();archive_entry_set_pathname_utf8(e,file.first.toUtf8().constData());archive_entry_set_filetype(e,symlink?AE_IFLNK:AE_IFREG);archive_entry_set_perm(e,0644);archive_entry_set_size(e,symlink?0:file.second.size());if(symlink)archive_entry_set_symlink(e,file.second.constData());QCOMPARE(archive_write_header(a,e),ARCHIVE_OK);if(!symlink)QCOMPARE(archive_write_data(a,file.second.constData(),file.second.size()),file.second.size());archive_entry_free(e);}
        QCOMPARE(archive_write_close(a),ARCHIVE_OK);archive_write_free(a);
    }
private slots:
    void init(){fixture=std::make_unique<QTemporaryDir>(QDir::currentPath()+"/view-archive-XXXXXX");QVERIFY(fixture->isValid());root=fixture->path();set_=qEnvironmentVariableIsSet("LTREE_HISTORY_FILE");previous_=qgetenv("LTREE_HISTORY_FILE");qputenv("LTREE_HISTORY_FILE",(root+"/search-history.json").toUtf8());}
    void cleanup(){if(set_)qputenv("LTREE_HISTORY_FILE",previous_);else qunsetenv("LTREE_HISTORY_FILE");fixture.reset();}
    void regexViewerVariableLengthsZeroMatchesAndValidation(){
        const auto path=root+"/regex.txt"; const QString source=QString::fromUtf8("árbol\nID:1\nID:234\n"); write(path,source.toUtf8());
        FileViewer view(path); view.resize(1280,768); view.show(); QTRY_VERIFY(!view.loading());
        QTest::keyClick(&view,Qt::Key_F); QTest::keyClick(&view,Qt::Key_F4); QTest::keyClicks(&view,"(?<=ID:)\\d+"); capture(view,"regex-viewer-prompt");
        QTest::keyClick(&view,Qt::Key_Return); QCOMPARE(view.hit(),9); QTest::keyClick(&view,Qt::Key_Space); QCOMPARE(view.hit(),14);
        capture(view,"regex-viewer-length"); QTest::keyClick(&view,Qt::Key_Minus); QCOMPARE(view.hit(),9);
        QTest::keyClick(&view,Qt::Key_F); QTest::keyClicks(&view,"(["); QTest::keyClick(&view,Qt::Key_Return); QVERIFY(view.prompting()); QVERIFY(view.status().startsWith("Invalid regex"));
        QTest::keyClick(&view,Qt::Key_Escape); QTest::keyClick(&view,Qt::Key_Space); QCOMPARE(view.hit(),14);
        view.setSearch({"(?=ID:)",SearchMode::Regex,true}); QCOMPARE(view.hit(),6); QVERIFY(view.status().contains("Zero-length")); QTest::keyClick(&view,Qt::Key_Space); QCOMPARE(view.hit(),11);
        QTest::keyClick(&view,Qt::Key_Space); QCOMPARE(view.hit(),11); QVERIFY(view.status().contains("No more matches")); QTest::keyClick(&view,Qt::Key_Minus); QCOMPARE(view.hit(),6);
        view.setSearch({"^",SearchMode::Regex,true}); QCOMPARE(view.hit(),0); QTest::keyClick(&view,Qt::Key_Space); QCOMPARE(view.hit(),6); QTest::keyClick(&view,Qt::Key_F3); QTRY_VERIFY(!view.loading()); QCOMPARE(view.hit(),0);
        view.setSearch({"ID:(\\d+)",SearchMode::Regex,true}); QCOMPARE(view.hit(),6); QTest::keyClick(&view,Qt::Key_F); QTest::keyClick(&view,Qt::Key_Up); QTest::keyClick(&view,Qt::Key_Return); QTest::keyClick(&view,Qt::Key_Return); QCOMPARE(view.hit(),9);
        view.setSearch({"$",SearchMode::Regex,true}); QCOMPARE(view.hit(),5); QTest::keyClick(&view,Qt::Key_B); QTest::keyClicks(&view,"$"); QTest::keyClick(&view,Qt::Key_Return); QCOMPARE(view.hit(),17);
        const auto empty=root+"/empty.txt"; write(empty,{}); FileViewer blank(empty); blank.show(); QTRY_VERIFY(!blank.loading()); blank.setSearch({"^$",SearchMode::Regex,true}); QCOMPARE(blank.hit(),0); QTest::keyClick(&blank,Qt::Key_Space); QVERIFY(blank.status().contains("No more matches"));
    }
    void regexViewerFromHexAndAcrossTaggedFiles(){
        const auto a=root+"/a.txt",b=root+"/b.txt"; write(a,"ID:1\n"); write(b,"ID:2\nID:3\n");
        FileViewer view(a); view.show(); QTRY_VERIFY(!view.loading()); QTest::keyClick(&view,Qt::Key_H); QVERIFY(view.hexMode());
        QTest::keyClick(&view,Qt::Key_F); QTest::keyClick(&view,Qt::Key_F4); QTest::keyClick(&view,Qt::Key_F4); QTest::keyClicks(&view,"ID:\\d+"); QTest::keyClick(&view,Qt::Key_Return); QVERIFY(!view.hexMode()); QCOMPARE(view.hit(),0);
        view.setFiles({a,b}); view.setSearch({"ID:\\d+",SearchMode::Regex,true}); QTest::keyClick(&view,Qt::Key_Space); QTRY_COMPARE(view.path(),b); QTRY_VERIFY(!view.loading()); QCOMPARE(view.hit(),0); QTest::keyClick(&view,Qt::Key_Space); QCOMPARE(view.hit(),5);
        QTest::keyClick(&view,Qt::Key_Minus); QCOMPARE(view.hit(),0); QTest::keyClick(&view,Qt::Key_Minus); QTRY_COMPARE(view.path(),a); QTRY_VERIFY(!view.loading()); QCOMPARE(view.hit(),0);
        write(a,QString::fromUtf8("😀😀").toUtf8()); view.reload(); QTRY_VERIFY(!view.loading()); view.setSearch({"(?=.)",SearchMode::Regex,true}); QCOMPARE(view.hit(),0); QTest::keyClick(&view,Qt::Key_Space); QCOMPARE(view.hit(),2); QTest::keyClick(&view,Qt::Key_Minus); QCOMPARE(view.hit(),0);
    }
    void regexArchiveTaggedSearchAndMemberView(){
        const auto zip=root+"/regex.zip"; archiveFixture(zip,{{"a.txt","ORDER_1\nORDER_234\n"},{"b.txt","skip\n"},{"c.txt","ORDER_99\n"}});
        ArchiveWindow window(zip,root); window.resize(1280,768); window.show(); QTRY_VERIFY(!window.busy()); QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier); QCOMPARE(window.taggedCount(),3);
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier); for(int i=0;i<3;++i)QTest::keyClick(&window,Qt::Key_F4);
        QTest::keyClicks(&window,"(["); QTest::keyClick(&window,Qt::Key_Return); QVERIFY(!window.busy()); QCOMPARE(window.taggedCount(),3); QVERIFY(window.status().startsWith("Invalid regex"));
        QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier); QTest::keyClicks(&window,"^ORDER_\\d+$"); capture(window,"regex-archive-prompt"); QTest::keyClick(&window,Qt::Key_Return); QTRY_VERIFY(!window.busy()); QCOMPARE(window.taggedCount(),2); QVERIFY2(window.status().contains("0 errors"),qPrintable(window.status()));
        QTest::keyClick(&window,Qt::Key_V); QTRY_VERIFY(window.findChild<FileViewer*>()); auto viewer=window.findChild<FileViewer*>(); QTRY_VERIFY(!viewer->loading()); QCOMPARE(viewer->hit(),0); QTest::keyClick(viewer,Qt::Key_Space); QCOMPARE(viewer->hit(),8);
    }
    void hexOverwriteCancelUndoAndAscii(){
        const QString path=root+"/bytes.bin";const QByteArray bytes=QByteArray::fromHex("00112233445566778899aabbccddeeff")+QByteArray(80,'x');write(path,bytes);
        FileViewer view(path);view.resize(1280,768);view.show();QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_H);QTest::keyClick(&view,Qt::Key_E);QVERIFY(view.editing());
        QTest::keyClicks(&view,"FE");QTest::keyClick(&view,Qt::Key_Escape);QCOMPARE(read(path),bytes);QVERIFY(!view.editing());
        QTest::keyClick(&view,Qt::Key_E);QTest::keyClicks(&view,"FE");QTest::keyClick(&view,Qt::Key_F8);QTest::keyClick(&view,Qt::Key_Return);QVERIFY(!view.editing());QCOMPARE(read(path),bytes);
        QTest::keyClick(&view,Qt::Key_E);QTest::keyClicks(&view,"FE");QTest::keyClick(&view,Qt::Key_Tab);QTest::keyClicks(&view,"Z");capture(view,"hex-edit");QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_N);QVERIFY(view.editing());QCOMPARE(read(path),bytes);
        QTest::keyClick(&view,Qt::Key_Return);capture(view,"hex-confirm");QTest::keyClick(&view,Qt::Key_Y);QVERIFY(!view.editing());auto expected=bytes;expected[0]=char(254);expected[1]='Z';QCOMPARE(read(path),expected);QCOMPARE(view.viewedBytes(),expected);
    }
    void hexPageAndHardlinks(){
        const auto path=root+"/page.bin",alias=root+"/alias.bin";QByteArray bytes(1024,'a');write(path,bytes);QVERIFY(::link(QFile::encodeName(path).constData(),QFile::encodeName(alias).constData())==0);
        struct stat before{},after{};QVERIFY(::stat(QFile::encodeName(path).constData(),&before)==0);
        FileViewer view(path);view.resize(640,400);view.show();QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_H);QTest::keyClick(&view,Qt::Key_PageDown);const auto offset=view.currentRow()*16;
        QTest::keyClick(&view,Qt::Key_E);QTest::keyClicks(&view,"00");QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_Y);bytes[offset]=0;QCOMPARE(read(path),bytes);QCOMPARE(read(alias),bytes);QVERIFY(::stat(QFile::encodeName(path).constData(),&after)==0);QCOMPARE(before.st_ino,after.st_ino);QCOMPARE(after.st_nlink,nlink_t(2));
    }
    void hexRejectsExternalChangesAndLinks(){
        const auto path=root+"/edit.bin";write(path,"0123");const auto snapshot=hexSnapshot(path);write(path,"abcd");QVERIFY(saveHexPage(path,snapshot,"0123",0,"X").contains("changed"));QCOMPARE(read(path),QByteArray("abcd"));
        const auto link=root+"/link.bin";QVERIFY(::symlink(QFile::encodeName(path).constData(),QFile::encodeName(link).constData())==0);QVERIFY(!hexSnapshot(link).valid);
        const auto dir=root+"/dir";QVERIFY(QDir().mkpath(dir));QVERIFY(!hexSnapshot(dir).valid);
        QVERIFY(!saveHexPage(path,hexSnapshot(path),"abcd",5,"Z").isEmpty());QCOMPARE(read(path),QByteArray("abcd"));
        FileViewer view(path);view.show();QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_H);QTest::keyClick(&view,Qt::Key_E);QTest::keyClicks(&view,"FF");write(path,"wxyz");QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_Y);QVERIFY(view.editing());QVERIFY(view.status().contains("changed"));QCOMPARE(read(path),QByteArray("wxyz"));QTest::keyClick(&view,Qt::Key_Escape);
    }
    void viewerModesCharsetsTabsMaskAndMenu(){
        const auto path=root+"/chars.txt";write(path,QByteArray("A\tB\n",4)+QByteArray::fromHex("82a08001")+"abc\n");FileViewer view(path);view.resize(1280,768);view.show();QTRY_VERIFY(!view.loading());
        QTest::keyClick(&view,Qt::Key_C);QCOMPARE(view.characterSet(),QString("OEM CP437"));QTest::keyClick(&view,Qt::Key_C);QCOMPARE(view.characterSet(),QString("ANSI Windows-1252"));QTest::keyClick(&view,Qt::Key_C,Qt::ShiftModifier);QCOMPARE(view.characterSet(),QString("OEM CP437"));
        QTest::keyClick(&view,Qt::Key_Tab);QCOMPARE(view.tabWidth(),2);QTest::keyClick(&view,Qt::Key_Tab);QCOMPARE(view.tabWidth(),3);
        QTest::keyClick(&view,Qt::Key_J);QVERIFY(view.wrapMode());QTest::keyClick(&view,Qt::Key_A);QVERIFY(!view.wrapMode());QTest::keyClick(&view,Qt::Key_L);QVERIFY(view.lineMode());QTest::keyClick(&view,Qt::Key_R);QTest::keyClick(&view,Qt::Key_M);capture(view,"view-commands");
        QTest::keyPress(&view,Qt::Key_Control);capture(view,"ctrl-view-commands");QTest::keyRelease(&view,Qt::Key_Control);QTest::keyPress(&view,Qt::Key_Alt);capture(view,"alt-view-commands");QTest::keyRelease(&view,Qt::Key_Alt);
        QTest::keyClick(&view,Qt::Key_F10);QTest::keyClick(&view,Qt::Key_F10);QTest::keyClick(&view,Qt::Key_F5);QTest::keyClick(&view,Qt::Key_A);QVERIFY(!view.hexMode());QTest::keyClick(&view,Qt::Key_F10);QSignalSpy closing(&view,&FileViewer::closed);QTest::keyClick(&view,Qt::Key_Escape);QCOMPARE(closing.size(),0);
        QTest::keyClick(&view,Qt::Key_F1);capture(view,"view-help");QTest::keyClick(&view,Qt::Key_Escape);QVERIFY(!view.prompting());
    }
    void viewerGatherClipboardFileAndBookmarks(){
        const auto path=root+"/gather.txt";write(path,"first\nsecond\nthird\n");FileViewer view(path);view.resize(1280,768);view.show();QTRY_VERIFY(!view.loading());
        QTest::keyClick(&view,Qt::Key_Down);QTest::keyClick(&view,Qt::Key_2,Qt::ControlModifier);QTest::keyClick(&view,Qt::Key_Home);QTest::keyClick(&view,Qt::Key_2,Qt::AltModifier);QCOMPARE(view.currentRow(),1);
        QTest::keyClick(&view,Qt::Key_G);QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_Down);QTest::keyClick(&view,Qt::Key_Return);QTest::keyClicks(&view,"CLIP:");QTest::keyClick(&view,Qt::Key_Return);QCOMPARE(QGuiApplication::clipboard()->text(),QString("second\nthird\n"));
        QTest::keyClick(&view,Qt::Key_Home);QTest::keyClick(&view,Qt::Key_G);QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_Return);QTest::keyClicks(&view,"gathered.txt");QTest::keyClick(&view,Qt::Key_Return);QCOMPARE(read(root+"/gathered.txt"),QByteArray("first\n"));
        QTest::keyClick(&view,Qt::Key_F5);QCOMPARE(QGuiApplication::clipboard()->text(),QString("second\nthird\nfirst\n"));QTest::keyClick(&view,Qt::Key_F2);QVERIFY(QGuiApplication::clipboard()->text().isEmpty());
    }
    void viewerHistoryReloadAndTaggedTraversal(){
        const auto a=root+"/a.txt",b=root+"/b.txt";write(a,"no hit\n");write(b,"needle one\nneedle two\n");
        {FileViewer view(a);view.resize(1280,768);view.show();view.setFiles({a,b});QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_F);QTest::keyClicks(&view,"needle");QTest::keyClick(&view,Qt::Key_Return);QTRY_VERIFY(!view.loading());QCOMPARE(view.path(),b);QCOMPARE(view.hit(),0);QTest::keyClick(&view,Qt::Key_Space);QCOMPARE(view.hit(),11);QTest::keyClick(&view,Qt::Key_Home,Qt::AltModifier);QTRY_VERIFY(!view.loading());QCOMPARE(view.path(),a);capture(view,"tagged-view");}
        QFile history(root+"/viewer-history.json");QVERIFY(history.open(QIODevice::ReadOnly));QVERIFY(history.readAll().contains("needle"));
        FileViewer view(b);view.show();QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_F);QTest::keyClick(&view,Qt::Key_Up);QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_Return);QCOMPARE(view.hit(),0);
        QTest::keyClick(&view,Qt::Key_F3,Qt::ControlModifier);write(b,"changed\n");QTRY_COMPARE_WITH_TIMEOUT(view.viewedBytes(),QByteArray("changed\n"),3000);QTest::keyClick(&view,Qt::Key_F3,Qt::ControlModifier);
    }
    void hexReadonlyEmptyLastByteAndAlternateEdit(){
        const auto path=root+"/last.bin";write(path,"012");FileViewer view(path);view.resize(640,400);view.show();QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_H);QTest::keyClick(&view,Qt::Key_E,Qt::AltModifier);QVERIFY(view.editing());QTest::keyClick(&view,Qt::Key_End);QTest::keyClick(&view,Qt::Key_Home);QTest::keyClick(&view,Qt::Key_Tab);QTest::keyClick(&view,Qt::Key_End);QTest::keyClicks(&view,"Z");QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_Y);QCOMPARE(read(path),QByteArray("01Z"));
        QFile::setPermissions(path,QFile::ReadOwner);const auto snapshot=hexSnapshot(path);QVERIFY(!saveHexPage(path,snapshot,"01Z",0,"X").isEmpty());QCOMPARE(read(path),QByteArray("01Z"));QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner);
        write(path,{});QTest::keyClick(&view,Qt::Key_F3);QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_E);QVERIFY(!view.editing());QVERIFY(view.status().contains("empty"));
    }
    void viewerUnicodeHexSearchAndCharsetConversion(){
        const auto path=root+"/unicode.bin";write(path,QByteArray::fromHex("fffe61004200630042006300"));FileViewer view(path);view.resize(1280,768);view.show();QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_H);
        QTest::keyClick(&view,Qt::Key_F);QTest::keyClick(&view,Qt::Key_F4);QTest::keyClicks(&view,"bc");QTest::keyClick(&view,Qt::Key_Return);QVERIFY(view.hexMode());QCOMPARE(view.hit(),4);QTest::keyClick(&view,Qt::Key_Space);QCOMPARE(view.hit(),8);QTest::keyClick(&view,Qt::Key_Minus);QCOMPARE(view.hit(),4);
        const auto charset=root+"/charset.txt";write(charset,QByteArray::fromHex("8280a0"));FileViewer other(charset);other.show();QTRY_VERIFY(!other.loading());QTest::keyClick(&other,Qt::Key_C);QCOMPARE(other.viewedText(),QString::fromUtf8("éÇá"));QTest::keyClick(&other,Qt::Key_C);QCOMPARE(other.viewedText(),QString::fromUtf8("‚€ "));QTest::keyClick(&other,Qt::Key_C,Qt::ShiftModifier);QCOMPARE(other.characterSet(),QString("OEM CP437"));
        for(int i=0;i<5;++i)QTest::keyClick(&other,Qt::Key_C);QCOMPARE(other.characterSet(),QString("EBCDIC IBM037"));
    }
    void viewerGatherGapSingleLineAndPersistentFileHistory(){
        const auto path=root+"/lines.txt";write(path,"first\nsecond\nthird\n");
        {FileViewer view(path);view.resize(1280,768);view.show();QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_G);QTest::keyClick(&view,Qt::Key_Down);QTest::keyClick(&view,Qt::Key_F4);QCOMPARE(QGuiApplication::clipboard()->text(),QString("second\n"));
            QTest::keyClick(&view,Qt::Key_F6);QTest::keyClick(&view,Qt::Key_Home);QTest::keyClick(&view,Qt::Key_F5);QCOMPARE(QGuiApplication::clipboard()->text(),QString("second\n\nfirst\n"));
            QTest::keyClick(&view,Qt::Key_G);QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_Return);QTest::keyClicks(&view,"saved.txt");QTest::keyClick(&view,Qt::Key_Return);QCOMPARE(read(root+"/saved.txt"),QByteArray("first\n"));}
        FileViewer view(path);view.show();QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_G);QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_Up);QTest::keyClick(&view,Qt::Key_Return);QTest::keyClick(&view,Qt::Key_Return);QCOMPARE(read(root+"/saved.txt"),QByteArray("first\nfirst\n"));
    }
    void viewerAutomaticScrollStopsAndLineNavigation(){
        const auto path=root+"/long.txt";QByteArray bytes;for(int i=0;i<120;++i)bytes+=QByteArray::number(i)+"\n";write(path,bytes);FileViewer view(path);view.resize(640,400);view.show();QTRY_VERIFY(!view.loading());QTest::keyClick(&view,Qt::Key_F6,Qt::ShiftModifier);QTRY_VERIFY(view.currentRow()>0);QSignalSpy closed(&view,&FileViewer::closed);QTest::keyClick(&view,Qt::Key_Escape);QCOMPARE(closed.size(),0);const auto at=view.currentRow();QTest::qWait(300);QCOMPARE(view.currentRow(),at);
        QTest::keyClick(&view,Qt::Key_L);QTest::keyClick(&view,Qt::Key_Down,Qt::ControlModifier);QCOMPARE(view.currentRow(),at+1);QTest::keyClick(&view,Qt::Key_PageDown);QVERIFY(view.currentRow()>=at+1);QTest::keyClick(&view,Qt::Key_Escape);QCOMPARE(closed.size(),1);
    }
    void archiveRejectsStaleCatalogAndCanExtractEmptyFolder(){
        const auto zip=root+"/stale.zip";archiveFixture(zip,{{"a.txt","initial"}});ArchiveWindow window(zip,root+"/out");window.show();QTRY_VERIFY(!window.busy());archiveFixture(zip,{{"b.txt","changed"}});QTest::keyClick(&window,Qt::Key_V);QVERIFY(!window.busy());QVERIFY(window.status().contains("changed"));QVERIFY(!window.findChild<FileViewer*>());
        const auto empty=root+"/empty.zip";auto a=archive_write_new();archive_write_set_format_zip(a);QCOMPARE(archive_write_open_filename(a,QFile::encodeName(empty).constData()),ARCHIVE_OK);auto entry=archive_entry_new();archive_entry_set_pathname(entry,"empty");archive_entry_set_filetype(entry,AE_IFDIR);archive_entry_set_perm(entry,0755);archive_entry_set_size(entry,0);QCOMPARE(archive_write_header(a,entry),ARCHIVE_OK);archive_entry_free(entry);archive_write_close(a);archive_write_free(a);
        ArchiveWindow folder(empty,root+"/empty-out");folder.show();QTRY_VERIFY(!folder.busy());QTest::keyClick(&folder,Qt::Key_E);QTest::keyClick(&folder,Qt::Key_Return);QTRY_VERIFY(!folder.busy());QVERIFY(QFileInfo(root+"/empty-out/empty").isDir());
    }
    void treeZipCreationFromBranchAndCancelledOverwrite(){
        write(root+"/sub/a.txt","A");write(root+"/b.txt","B");TreeWindow window(root);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_F5,Qt::ControlModifier);QPointer<ArchiveWindow> creator=window.findChild<ArchiveWindow*>();QVERIFY(creator);QTest::keyClicks(creator,root+"/branch.zip");QTest::keyClick(creator,Qt::Key_Return);QTRY_VERIFY(!creator->busy());const auto catalog=listArchive(root+"/branch.zip",cancel());QCOMPARE(catalog.entries.size(),2);QSet<QString> names;for(const auto &entry:catalog.entries)names.insert(entry.path);QCOMPARE(names,(QSet<QString>{"sub/a.txt","b.txt"}));QTest::keyClick(creator,Qt::Key_Escape);QTRY_VERIFY(creator.isNull());QTRY_VERIFY(!window.busy());QCOMPARE(window.session().tags,tags);QCOMPARE(window.session().view,View::Branch);
    }
    void zipRoundTripWindowsNamesAndBinary(){
        const auto a=root+"/source/café/hello.txt",b=root+"/source/sub/raw.bin",zip=root+"/files.zip",dest=root+"/output";write(a,"hello ZIP\r\n");write(b,QByteArray::fromHex("0001feff0042"));
        const auto result=createZip({a,b},root+"/source",zip,true,false,cancel());QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.completed,2);
        const auto catalog=listArchive(zip,cancel());QVERIFY2(catalog.error.isEmpty(),qPrintable(catalog.error));QCOMPARE(catalog.entries.size(),2);QCOMPARE(catalog.entries[0].path,QString("café/hello.txt"));QCOMPARE(readArchiveMember(zip,1,cancel()).bytes,read(b));
        const auto extracted=extractArchive(zip,{0,1},dest,true,false,cancel());QVERIFY2(extracted.error.isEmpty(),qPrintable(extracted.error));QCOMPARE(read(dest+"/café/hello.txt"),read(a));QCOMPARE(read(dest+"/sub/raw.bin"),read(b));
        QProcess python;python.start("python3",{"-c","import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None; assert 'café/hello.txt' in z.namelist(); assert z.read('sub/raw.bin') == bytes.fromhex('0001feff0042')",zip});QVERIFY(python.waitForFinished(10000));QCOMPARE(python.exitCode(),0);
    }
    void archiveUnsafeNamesAndFilesystemLinks(){
        const auto zip=root+"/unsafe.zip";archiveFixture(zip,{{"../outside.txt","escape"},{"/absolute.txt","absolute"},{"C:/drive.txt","drive"},{"good/file.txt","safe"},{"dir\\windows.txt","backslash"}});
        const auto catalog=listArchive(zip,cancel());QVERIFY(catalog.error.isEmpty());QVERIFY(!catalog.entries[0].safe);QVERIFY(!catalog.entries[1].safe);QVERIFY(!catalog.entries[2].safe);
        const auto result=extractArchive(zip,{0,1,2,3,4},root+"/output",true,false,cancel());QCOMPARE(result.failed,3);QCOMPARE(result.completed,2);QVERIFY(!QFileInfo::exists(root+"/outside.txt"));QCOMPARE(read(root+"/output/dir/windows.txt"),QByteArray("backslash"));
        const auto outside=root+"/separate";QVERIFY(QDir().mkpath(outside));QVERIFY(::symlink(QFile::encodeName(outside).constData(),QFile::encodeName(root+"/output/good").constData())!=0);
        QDir(root+"/output/good").removeRecursively();QVERIFY(::symlink(QFile::encodeName(outside).constData(),QFile::encodeName(root+"/output/good").constData())==0);
        const auto linked=extractArchive(zip,{3},root+"/output",true,true,cancel());QCOMPARE(linked.failed,1);QVERIFY(!QFileInfo::exists(outside+"/file.txt"));
        const auto tar=root+"/link.tar";archiveFixture(tar,{{"escape","../separate"}},true,true);QCOMPARE(extractArchive(tar,{0},root+"/other",true,false,cancel()).failed,1);QVERIFY(!QFileInfo::exists(root+"/other/escape"));
    }
    void archiveOverwriteCancelAndCollisions(){
        const auto a=root+"/a/same.txt",b=root+"/b/same.txt",zip=root+"/existing.zip";write(a,"first");write(b,"second");write(zip,"original");
        QVERIFY(!createZip({a},root,zip,true,false,cancel()).error.isEmpty());QCOMPARE(read(zip),QByteArray("original"));
        QVERIFY(!createZip({a,b},root,zip,false,true,cancel()).error.isEmpty());QCOMPARE(read(zip),QByteArray("original"));
        const auto stopped=cancel();stopped->store(true);QVERIFY(createZip({a},root,zip,true,true,stopped).cancelled);QCOMPARE(read(zip),QByteArray("original"));
        QVERIFY(createZip({a,b},root,zip,true,true,cancel()).error.isEmpty());const auto target=root+"/flat";const auto flat=extractArchive(zip,{0,1},target,false,false,cancel());QCOMPARE(flat.completed,1);QCOMPARE(flat.failed,1);QCOMPARE(read(target+"/same.txt"),QByteArray("first"));
        const auto link=root+"/link.txt";QVERIFY(::symlink(QFile::encodeName(a).constData(),QFile::encodeName(link).constData())==0);QVERIFY(!createZip({link},root,root+"/links.zip",false,false,cancel()).error.isEmpty());QVERIFY(!QFileInfo::exists(root+"/links.zip"));
        QVERIFY(QDir(root).entryList({".ltree-*"},QDir::Files|QDir::Hidden).isEmpty());
    }
    void archiveCorruptionAndMemberLimit(){
        const auto zip=root+"/bad.zip";const QByteArray contents="UNIQUE-CONTENTS-TO-CORRUPT";archiveFixture(zip,{{"data.txt",contents}});auto bytes=read(zip);const int offset=bytes.indexOf(contents);QVERIFY(offset>=0);bytes[offset]='X';write(zip,bytes);
        QVERIFY(!readArchiveMember(zip,0,cancel()).error.isEmpty());const auto result=extractArchive(zip,{0},root+"/output",true,false,cancel());QVERIFY(!result.error.isEmpty());QVERIFY(!QFileInfo::exists(root+"/output/data.txt"));
        const auto big=root+"/large.zip";archiveFixture(big,{{"large.txt",QByteArray(32*1024*1024+1,'a')}});QVERIFY(readArchiveMember(big,0,cancel()).error.contains("32 MiB"));
    }
    void archiveUIBrowseSearchExtractAndReadonly(){
        const auto zip=root+"/demo.zip";archiveFixture(zip,{{"folder/first.txt","no match\n"},{"folder/second.txt","needle\n"},{"top.txt","top\n"}});ArchiveWindow window(zip,root+"/output");window.resize(1280,768);window.show();QTRY_VERIFY(!window.busy());QCOMPARE(window.entryCount(),3);
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QCOMPARE(window.taggedCount(),2);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClicks(&window,"needle");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QCOMPARE(window.taggedCount(),1);QVERIFY(window.status().contains("1 hits"));capture(window,"archive-search");
        QTest::keyClick(&window,Qt::Key_Down);QTest::keyClick(&window,Qt::Key_V);QTRY_VERIFY(!window.busy());auto viewer=window.findChild<FileViewer*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());QCOMPARE(viewer->hit(),0);QTest::keyClick(viewer,Qt::Key_H);QTest::keyClick(viewer,Qt::Key_E);QVERIFY(!viewer->editing());QVERIFY(viewer->status().contains("Extract"));QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(!window.findChild<FileViewer*>());
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);capture(window,"archive-extract");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QCOMPARE(read(root+"/output/folder/second.txt"),QByteArray("needle\n"));QVERIFY(!QFileInfo::exists(root+"/output/folder/first.txt"));
    }
    void archiveUIMasksMenusOverwriteAndCreate(){
        const auto a=root+"/a.txt",b=root+"/b.bin",zip=root+"/new.zip";write(a,"alpha");write(b,"beta");ArchiveWindow creator({a,b},root,root);creator.resize(1280,768);creator.show();QTest::keyClicks(&creator,zip);QTest::keyClick(&creator,Qt::Key_Return);QTRY_VERIFY(!creator.busy());QVERIFY(QFileInfo::exists(zip));QCOMPARE(listArchive(zip,cancel()).entries.size(),2);
        ArchiveWindow browser(zip,root+"/out");browser.resize(1280,768);browser.show();QTRY_VERIFY(!browser.busy());QTest::keyClick(&browser,Qt::Key_F);QTest::keyClicks(&browser,"*.txt");QTest::keyClick(&browser,Qt::Key_Return);QTest::keyClick(&browser,Qt::Key_T,Qt::ControlModifier);QCOMPARE(browser.taggedCount(),1);capture(browser,"archive-commands");
        QTest::keyClick(&browser,Qt::Key_F4);QTest::keyClick(&browser,Qt::Key_C);QTest::keyClick(&browser,Qt::Key_Return);QTRY_VERIFY(!browser.busy());QCOMPARE(read(root+"/out/a.txt"),QByteArray("alpha"));write(root+"/out/a.txt","keep");
        QTest::keyClick(&browser,Qt::Key_C);QTest::keyClick(&browser,Qt::Key_F4);QTest::keyClick(&browser,Qt::Key_Return);QCOMPARE(read(root+"/out/a.txt"),QByteArray("keep"));QTest::keyClick(&browser,Qt::Key_N);QTest::keyClick(&browser,Qt::Key_Escape);QCOMPARE(read(root+"/out/a.txt"),QByteArray("keep"));
    }
    void createFormats_data(){
        QTest::addColumn<int>("format");
        for(int i=0;i<9;++i)QTest::newRow(qPrintable(archiveFormatName(ArchiveFormat(i))))<<i;
    }
    void createFormats(){
        QFETCH(int,format);const auto type=ArchiveFormat(format);const auto a=root+"/nested/café.txt",b=root+"/b.bin",output=root+"/test"+archiveFormatSuffix(type);
        const QByteArray content="Archive round trip\n";write(a,content);write(b,QByteArray::fromHex("000102ff"));const QStringList files=archiveStreamFormat(type)?QStringList{a}:QStringList{a,b};
        auto result=createArchive(files,root,output,type,true,false,cancel());QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.completed,files.size());
        const auto catalog=listArchive(output,cancel());QVERIFY2(catalog.error.isEmpty(),qPrintable(catalog.error));QCOMPARE(catalog.entries.size(),files.size());
        for(int i=0;i<files.size();++i)QCOMPARE(readArchiveMember(output,i,cancel()).bytes,read(files[i]));
        const auto dest=root+"/out";QSet<int> selected;for(const auto &entry:catalog.entries)selected.insert(entry.index);
        result=extractArchive(output,selected,dest,true,false,cancel());QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.completed,files.size());
        for(int i=0;i<files.size();++i)QCOMPARE(read(QDir(dest).filePath(catalog.entries[i].path)),read(files[i]));
        if(type==ArchiveFormat::SevenZip){QProcess process;process.start("7z",{"t",output});QVERIFY(process.waitForFinished(10000));QCOMPARE(process.exitCode(),0);}
        else {QProcess process;process.start("python3",{"-c","import sys,zipfile,tarfile,gzip,bz2,lzma; p,k=sys.argv[1:]; expected=b'Archive round trip\\n'; data=zipfile.ZipFile(p).read('nested/café.txt') if k=='ZIP' else (gzip.open(p,'rb').read() if k=='GZ' else bz2.open(p,'rb').read() if k=='BZ2' else lzma.open(p,'rb').read() if k=='XZ' else tarfile.open(p,'r:*').extractfile('nested/café.txt').read()); assert data==expected",output,archiveFormatName(type)});QVERIFY(process.waitForFinished(10000));QVERIFY2(process.exitCode()==0,process.readAllStandardError().constData());}
        if(archiveStreamFormat(type)){write(output,"keep");result=createArchive({a,b},root,output,type,true,true,cancel());QVERIFY(!result.error.isEmpty());QCOMPARE(read(output),QByteArray("keep"));}
        else {const auto stopped=cancel();stopped->store(true);write(output,"keep");result=createArchive(files,root,output,type,true,true,stopped);QVERIFY(result.cancelled);QCOMPARE(read(output),QByteArray("keep"));}
    }
    void archiveNameEditingAndFormatPicker(){
        const auto source=root+"/a.txt";write(source,"Sample");ArchiveWindow creator({source},root,root);creator.resize(640,400);creator.show();
        QTest::keyClick(&creator,Qt::Key_End);for(int i=0;i<4;++i)QTest::keyClick(&creator,Qt::Key_Left);
        QTest::keyClick(&creator,Qt::Key_Backspace);QTest::keyClicks(&creator,"renameX");QTest::keyClick(&creator,Qt::Key_Left);QTest::keyClick(&creator,Qt::Key_Delete);
        QTest::keyClick(&creator,Qt::Key_Home);QTest::keyClick(&creator,Qt::Key_End);QTest::keyClick(&creator,Qt::Key_Left,Qt::ShiftModifier);QTest::keyClicks(&creator,"p");
        QTest::keyClick(&creator,Qt::Key_F3);capture(creator,"archive-format-picker");QTest::keyClick(&creator,Qt::Key_Down);QTest::keyClick(&creator,Qt::Key_Down);QTest::keyClick(&creator,Qt::Key_Return);capture(creator,"archive-edit-name");
        QTest::keyClick(&creator,Qt::Key_Return);QTRY_VERIFY(!creator.busy());QVERIFY2(QFileInfo::exists(root+"/rename.tar.gz"),qPrintable(creator.status()));QCOMPARE(readArchiveMember(root+"/rename.tar.gz",0,cancel()).bytes,QByteArray("Sample"));
        ArchiveWindow paste({source},root,root);paste.show();QGuiApplication::clipboard()->setText(root+"/pasted.7z");QTest::keyClick(&paste,Qt::Key_A,Qt::ControlModifier);QTest::keyClick(&paste,Qt::Key_V,Qt::ControlModifier);QTest::keyClick(&paste,Qt::Key_Return);QTRY_VERIFY(!paste.busy());QVERIFY2(QFileInfo::exists(root+"/pasted.7z"),qPrintable(paste.status()));QCOMPARE(readArchiveMember(root+"/pasted.7z",0,cancel()).bytes,QByteArray("Sample"));
    }
    void archiveStreamUIRejectsMultipleAndPickerCancel(){
        const auto a=root+"/a.txt",b=root+"/b.txt";write(a,"A");write(b,"B");ArchiveWindow creator({a,b},root,root);creator.resize(640,400);creator.show();
        QTest::keyClick(&creator,Qt::Key_F3);QTest::keyClick(&creator,Qt::Key_End);QTest::keyClick(&creator,Qt::Key_Escape);
        QTest::keyClick(&creator,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(&creator,root+"/batch.gz");QTest::keyClick(&creator,Qt::Key_Return);QVERIFY(!creator.busy());QVERIFY(creator.status().contains("one file"));QVERIFY(!QFileInfo::exists(root+"/batch.gz"));
        QTest::keyClick(&creator,Qt::Key_F3);QTest::keyClick(&creator,Qt::Key_Home);QTest::keyClick(&creator,Qt::Key_Return);QTest::keyClick(&creator,Qt::Key_Return);QTRY_VERIFY(!creator.busy());QCOMPARE(listArchive(root+"/batch.zip",cancel()).entries.size(),2);
        ArchiveWindow single({a},root,root);single.show();QTest::keyClick(&single,Qt::Key_F3);QTest::keyClick(&single,Qt::Key_End);QTest::keyClick(&single,Qt::Key_Up);QTest::keyClick(&single,Qt::Key_Up);QTest::keyClick(&single,Qt::Key_Return);QTest::keyClick(&single,Qt::Key_Return);QTRY_VERIFY(!single.busy());QVERIFY(QFileInfo::exists(root+"/a.txt.gz"));QCOMPARE(listArchive(root+"/a.txt.gz",cancel()).entries[0].path,QString("a.txt"));
    }
    void compressedTarGzipAndSevenZip(){
        const auto source=root+"/data.txt";write(source,"Compressed contents\n");const auto seven=root+"/demo.7z";QProcess tool;tool.setWorkingDirectory(root);tool.start("7z",{"a",seven,"data.txt"});QVERIFY(tool.waitForFinished(10000));QCOMPARE(tool.exitCode(),0);const auto catalog=listArchive(seven,cancel());QVERIFY2(catalog.error.isEmpty(),qPrintable(catalog.error));QCOMPARE(catalog.entries.size(),1);QCOMPARE(readArchiveMember(seven,0,cancel()).bytes,read(source));
        const auto gzip=root+"/data.txt.gz",tar=root+"/demo.tar.gz";QProcess python;python.start("python3",{"-c","import gzip,tarfile,sys; data=open(sys.argv[1],'rb').read(); gzip.open(sys.argv[2],'wb').write(data); t=tarfile.open(sys.argv[3],'w:gz'); t.add(sys.argv[1],arcname='nested/data.txt'); t.close()",source,gzip,tar});QVERIFY(python.waitForFinished(10000));QCOMPARE(python.exitCode(),0);
        const auto single=listArchive(gzip,cancel());QVERIFY2(single.error.isEmpty(),qPrintable(single.error));QCOMPARE(single.entries.size(),1);QCOMPARE(single.entries[0].path,QString("data.txt"));QCOMPARE(readArchiveMember(gzip,0,cancel()).bytes,read(source));QCOMPARE(readArchiveMember(tar,0,cancel()).bytes,read(source));QVERIFY(extractArchive(gzip,{0},root+"/gz-out",true,false,cancel()).error.isEmpty());QCOMPARE(read(root+"/gz-out/data.txt"),read(source));
    }
    void archiveEncryptedZipAndTar(){
        const auto file=root+"/secret.txt",zip=root+"/encrypted.zip";write(file,"secret text");QProcess process;process.start("zip",{"-j","-P","fixture-password",zip,file});QVERIFY(process.waitForFinished(10000));QCOMPARE(process.exitCode(),0);
        QVERIFY(!readArchiveMember(zip,0,cancel()).error.isEmpty());QCOMPARE(readArchiveMember(zip,0,cancel(),"fixture-password").bytes,QByteArray("secret text"));
        const auto tar=root+"/files.tar";archiveFixture(tar,{{"nested/a.txt","TAR contents"}},true);QVERIFY(listArchive(tar,cancel()).error.isEmpty());QCOMPARE(readArchiveMember(tar,0,cancel()).bytes,QByteArray("TAR contents"));
    }
    void enterBrowsesArchiveAndNormalFilesReturnToTree(){
        const auto zip=root+"/a.zip",plain=root+"/b.txt";archiveFixture(zip,{{"nested/c.txt","C"}});write(plain,"plain");
        TreeWindow window(root);window.resize(1280,768);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Directory);QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(window.session().currentFile()->path,zip);
        QTest::keyClick(&window,Qt::Key_F5,Qt::AltModifier);QVERIFY(!window.findChild<ArchiveWindow*>());QCOMPARE(window.session().view,View::Directory);
        QTest::keyClick(&window,Qt::Key_Return);QPointer<ArchiveWindow> browser=window.findChild<ArchiveWindow*>();QVERIFY(browser);QTRY_VERIFY(!browser->busy());QCOMPARE(browser->entryCount(),1);capture(window,"enter-open-archive");
        QTest::keyClick(browser,Qt::Key_Escape);QTRY_VERIFY(browser.isNull());QCOMPARE(window.session().view,View::Directory);QCOMPARE(window.session().currentFile()->path,zip);
        QTest::keyClick(&window,Qt::Key_End);QCOMPARE(window.session().currentFile()->path,plain);QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Tree);
        QTest::keyClick(&window,Qt::Key_B);QCOMPARE(window.session().view,View::Branch);QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_Return);browser=window.findChild<ArchiveWindow*>();QVERIFY(browser);QTRY_VERIFY(!browser->busy());QTest::keyClick(browser,Qt::Key_Escape);QTRY_VERIFY(browser.isNull());QCOMPARE(window.session().view,View::Branch);
    }
    void treeArchiveAndTaggedViewReturnContext(){
        const auto a=root+"/a.txt",b=root+"/b.txt",zip=root+"/demo.zip";write(a,"A");write(b,"B");archiveFixture(zip,{{"nested/c.txt","C"}});
        TreeWindow window(root);window.resize(1280,768);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F8);const auto current=window.session().currentFile()->path;const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_V,Qt::ControlModifier);QPointer<FileViewer> viewer=window.findChild<FileViewer*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());QTest::keyClick(viewer,Qt::Key_N);QTRY_VERIFY(!viewer->loading());QCOMPARE(viewer->path(),b);QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(viewer.isNull());QCOMPARE(window.session().currentFile()->path,current);QCOMPARE(window.session().tags,tags);QVERIFY(window.isSplit());
        QTest::keyClick(&window,Qt::Key_End);QCOMPARE(window.session().currentFile()->path,zip);QTest::keyClick(&window,Qt::Key_Return);QPointer<ArchiveWindow> browser=window.findChild<ArchiveWindow*>();QVERIFY(browser);QTRY_VERIFY(!browser->busy());QCOMPARE(browser->entryCount(),1);capture(window,"archive-from-split");QTest::keyClick(browser,Qt::Key_Escape);QTRY_VERIFY(browser.isNull());QVERIFY(window.isSplit());QCOMPARE(window.session().tags,tags);
        QTest::keyClick(&window,Qt::Key_V);viewer=window.findChild<FileViewer*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());QVERIFY(viewer->viewedBytes().contains("nested/c.txt"));QTest::keyClick(viewer,Qt::Key_H);QTRY_VERIFY(!viewer->loading());QVERIFY(viewer->viewedBytes().startsWith("PK"));
    }
};
QTEST_MAIN(ViewArchiveTests)
#include "test_view_archive.moc"
