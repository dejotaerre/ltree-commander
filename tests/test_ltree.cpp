#include "core/session.h"
#include "ui/treewindow.h"
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFocusEvent>
#include <QSemaphore>
#include <QScopeGuard>
#include <QThreadPool>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFontInfo>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStorageInfo>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>
#include <limits>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/xattr.h>

using namespace ltree;
static QStringList *fontDiagnostics=nullptr;
static QtMessageHandler previousFontHandler=nullptr;
static void checkFontMessage(QtMsgType type,const QMessageLogContext &context,const QString &message)
{
    if(message.contains("variable-width font") && fontDiagnostics)fontDiagnostics->append(message);
    if(previousFontHandler)previousFontHandler(type,context,message);
}

class LTreeTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> fixture;
    QString root;
    QString alpha;
    QString panel;
    QByteArray priorHistoryFile_;
    bool priorHistorySet_ = false;
    QByteArray priorShellHistoryFile_;
    bool priorShellHistorySet_ = false;

    void write(const QString &path, const QByteArray &data = "ordenes\n")
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }
    ScanResult scan(const QString &path, bool recursive = false)
    {
        return scanDirectories(path, recursive, std::make_shared<std::atomic_bool>(false));
    }
    void capture(TreeWindow &window, const QString &name)
    {
        const QString dir = qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if (dir.isEmpty()) return;
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(window.grab().save(dir + '/' + name + ".png"));
    }

    void displayCells(TreeWindow &window, int x, int y, const QString &text, bool selected = false)
    {
        QImage expected(text.size()*8,16,QImage::Format_RGB32);
        expected.fill(selected?QColor(192,192,192):QColor(0,0,128));
        ConsoleFont font;QVERIFY(font.available());
        {QPainter painter(&expected);for(int i=0;i<text.size();++i)
            QVERIFY(font.draw(painter,i*8,0,text.mid(i,1),selected?Qt::black:QColor(255,255,0)));}
        const auto image=window.grab().toImage();const auto ratio=image.devicePixelRatio();
        const auto actual=image.copy(qRound(x*8*ratio),qRound(y*16*ratio),qRound(text.size()*8*ratio),qRound(16*ratio));
        QVERIFY2(actual.convertToFormat(QImage::Format_RGB32)==expected.scaled(actual.size(),Qt::IgnoreAspectRatio,Qt::FastTransformation),
            qPrintable(QString("File display cells (%1,%2): %3").arg(x).arg(y).arg(text)));
    }

    QString terminalOutput(const QSignalSpy &spy)
    {
        QString output;for(const auto &event:spy)output+=event.first().toString();return output;
    }
    void confirmTaggedDelete(TreeWindow &window, bool each = false)
    {
        QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,each?Qt::Key_Y:Qt::Key_N);
    }
    QMap<QString, FileMetadata> pruneSnapshot(const QString &path, bool recursive = true)
    {
        QMap<QString, FileMetadata> logged;
        for (const auto &dir : scan(path, recursive).directories) logged.insert(dir.path, readMetadata(dir.path));
        return logged;
    }
private slots:
    void fileDisplayCycleTreeAndFiles()
    {
        const auto base=root+"/display";QVERIFY(QDir().mkpath(base));
        for(int i=0;i<6;++i){const auto path=base+QString("/item_%1.php").arg(i,2,10,QChar('0'));write(path,QByteArray(123,'a'));QVERIFY(::chmod(QFile::encodeName(path).constData(),0644)==0);}
        TreeWindow window(base);window.resize(960,400);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::Details);
        displayCells(window,46,15,".php");displayCells(window,56,15,QString("123").rightJustified(11));displayCells(window,68,15,"rw-r--r-- .");
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);const auto tags=window.session().tags;
        const auto directory=window.session().directory;
        QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::LongName);
        displayCells(window,2,15,"item_00.php");capture(window,"file-display-tree-long");
        QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::Name);
        displayCells(window,19,15,".php");displayCells(window,26,15,"item_05");capture(window,"file-display-tree-name");
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F);
        QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::SizeAttributes);displayCells(window,29,15,QString("123").rightJustified(11));displayCells(window,41,15,"0644 .");
        capture(window,"file-display-tree-size");QCOMPARE(window.session().view,View::Tree);QCOMPARE(window.session().directory,directory);QCOMPARE(window.session().tags,tags);
        QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::Details);
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Down);const auto selected=window.session().currentFile()->path;
        displayCells(window,46,3,".php",true);displayCells(window,80,3,window.session().currentFile()->modified.toString("yyyy-MM-dd HH:mm"),true);displayCells(window,68,3,"rw-r--r-- .",true);
        capture(window,"file-display-details");
        for(int i=0;i<4;++i){QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.session().currentFile()->path,selected);QCOMPARE(window.session().tags,tags);}
        QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::Details);QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(window.session().view,View::Tree);
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.php");QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().filespec.text(),QString("*.php"));
        QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.session().filespec.text(),QString("*.php"));QCOMPARE(window.session().tags,tags);
    }
    void fileDisplayColumnsNavigationMouseAndResize()
    {
        const auto base=root+"/columns";QVERIFY(QDir().mkpath(base));
        for(int i=0;i<90;++i)write(base+QString("/file_%1.php").arg(i,2,10,QChar('0')));
        TreeWindow window(base);window.resize(960,400);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);
        QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::Name);
        displayCells(window,2,2,"file_00",true);displayCells(window,26,2,"file_18");displayCells(window,50,2,"file_36");displayCells(window,74,2,"file_54");
        capture(window,"file-display-columns");QTest::keyClick(&window,Qt::Key_Right);QCOMPARE(window.session().fileIndex,18);
        QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(window.session().fileIndex,19);QTest::keyClick(&window,Qt::Key_Left);QCOMPARE(window.session().fileIndex,1);
        QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_PageDown);QCOMPARE(window.session().fileIndex,72);
        displayCells(window,74,2,"file_72",true);QTest::keyClick(&window,Qt::Key_PageUp);QCOMPARE(window.session().fileIndex,0);
        QTest::mouseClick(&window,Qt::RightButton,Qt::NoModifier,QPoint(27*8,2*16+8));QCOMPARE(window.session().fileIndex,18);QVERIFY(window.session().tags.contains(base+"/file_18.php"));
        QSignalSpy opened(&window,&TreeWindow::openRequested);QTest::mouseDClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint(51*8,2*16+8));
        QCOMPARE(opened.size(),1);QCOMPARE(opened.first().first().toString(),base+"/file_36.php");
        QTest::mouseDClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint(24*8,2*16+8));QCOMPARE(opened.size(),1);
        QTest::keyClick(&window,Qt::Key_End);QCOMPARE(window.session().fileIndex,89);const auto selected=window.session().currentFile()->path;
        QTest::qWait(250);window.resize(640,400);QTest::qWait(100);QTRY_COMPARE(window.columns(),80);QCOMPARE(window.session().currentFile()->path,selected);displayCells(window,2,2,"file_54");displayCells(window,30,19,"file_89",true);
        capture(window,"file-display-columns-80");QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::SizeAttributes);
        QCOMPARE(window.session().currentFile()->path,selected);QVERIFY(window.session().tags.contains(base+"/file_18.php"));
        capture(window,"file-display-size-80");
        QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::Details);
        displayCells(window,40,2,window.session().currentFile()->modified.toString("yyyy-MM-dd HH:mm"),true);
    }
    void fileDisplaySplitFiltersAndPreview()
    {
        TreeWindow window(panel);window.resize(1280,768);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        window.load(true,true);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto tags=window.session().tags;const auto selected=window.session().currentFile()->path;
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);
        QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::Name);QCOMPARE(window.session().currentFile()->path,selected);QCOMPARE(window.oppositeSession()->currentFile()->path,selected);
        capture(window,"file-display-split-name");QTest::keyClick(&window,Qt::Key_Tab);QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::Name);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F);QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::SizeAttributes);
        QCOMPARE(window.session().tags,tags);QCOMPARE(window.oppositeSession()->tags,tags);capture(window,"file-display-split-size");
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.php");QTest::keyClick(&window,Qt::Key_Return);
        const auto count=window.session().files().size();QVERIFY(count>0);const auto filtered=window.session().currentFile()->path;
        QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.session().files().size(),count);QCOMPARE(window.session().currentFile()->path,filtered);
        QCOMPARE(window.oppositeSession()->filespec.text(),QString("*.*"));QTest::keyClick(&window,Qt::Key_F7);
        auto preview=window.findChild<FileViewer*>("autoview");QVERIFY(preview);QTRY_VERIFY(!preview->loading());
        QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::LongName);QVERIFY(preview->isVisible());
        QCOMPARE(window.session().currentFile()->path,filtered);capture(window,"file-display-autoview-long");QTest::keyClick(&window,Qt::Key_F7);
        QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Tree);QTest::keyClick(&window,Qt::Key_S);QCOMPARE(window.session().view,View::Showall);
        QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.fileDisplay(),TreeWindow::FileDisplay::Name);QCOMPARE(window.session().tags,tags);
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_G);QCOMPARE(window.session().view,View::Global);
        const auto global=window.session().currentFile()->path;QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);QCOMPARE(window.session().view,View::Global);QCOMPARE(window.session().currentFile()->path,global);
    }
    void fileDisplayExtensionsLongNamesAndEmpty()
    {
        const auto base=root+"/extensions";QVERIFY(QDir().mkpath(base));write(base+"/sample.extension");
        TreeWindow window(base);window.resize(640,400);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);displayCells(window,11,2,".extensio",true);
        QTest::keyClick(&window,Qt::Key_Left,Qt::ShiftModifier);displayCells(window,10,2,".extension",true);
        QTest::keyClick(&window,Qt::Key_Right,Qt::ShiftModifier);displayCells(window,11,2,".extensio",true);
        QTest::keyClick(&window,Qt::Key_Home,Qt::ShiftModifier);QCOMPARE(window.session().fileIndex,0);
        QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);displayCells(window,2,2,"sample.extension",true);capture(window,"file-display-long-80");
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.missing");QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.session().files().isEmpty());
        for(int i=0;i<4;++i)QTest::keyClick(&window,Qt::Key_F,Qt::AltModifier);
        QTest::keyClick(&window,Qt::Key_PageDown);QTest::keyClick(&window,Qt::Key_Right);QCOMPARE(window.session().fileIndex,0);
        QTest::mouseDClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint(40,40));QVERIFY(window.session().files().isEmpty());capture(window,"file-display-empty");
    }
    void pruneEngineLoggedBranchAndLinks()
    {
        const auto base = root + "/prune";
        QVERIFY(QDir().mkpath(base + "/sub/empty"));write(base + "/.hidden");write(base + "/sub/binary", QByteArray("a\0b", 3));
        const auto outside = root + "/beta/externo.php";
        QVERIFY(QFile::link(root + "/beta", base + "/outside-link"));
        QVERIFY(QFile::link(outside, base + "/file-link"));
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        const auto result = pruneBranch(root, readMetadata(base), pruneSnapshot(base), {}, cancel);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));QVERIFY(!QFileInfo::exists(base));
        QCOMPARE(result.removedFiles.size(), 4);QCOMPARE(result.removedDirectories.size(), 3);
        QVERIFY(QFileInfo::exists(outside));QCOMPARE(result.scan.directories.size(), 1);QCOMPARE(result.scan.directories.first().path, root);
    }
    void pruneEngineKeepCurrentAndEmptyOnly()
    {
        const auto base = root + "/prune";QVERIFY(QDir().mkpath(base + "/sub/empty"));
        write(base + "/keep");write(base + "/sub/remove");
        PruneOptions options;options.keepCurrentFiles = true;
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        auto result = pruneBranch(root, readMetadata(base), pruneSnapshot(base), options, cancel);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));QVERIFY(QFileInfo::exists(base + "/keep"));QVERIFY(!QFileInfo::exists(base + "/sub"));
        QVERIFY(QDir().mkpath(base + "/sub/empty"));write(base + "/sub/keep");
        QVERIFY(QFile::link(root + "/beta", base + "/link"));
        options.keepCurrentFiles = false;options.onlyEmpty = true;
        result = pruneBranch(root, readMetadata(base), pruneSnapshot(base), options, cancel);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));QCOMPARE(result.removedFiles.size(), 0);
        QCOMPARE(result.removedDirectories, QStringList{base + "/sub/empty"});QVERIFY(QFileInfo::exists(base + "/sub/keep"));QVERIFY(QFileInfo(base + "/link").isSymLink());
        QVERIFY(QDir().mkpath(root + "/empty/deeper"));
        result = pruneBranch(root, readMetadata(root + "/empty"), pruneSnapshot(root + "/empty"), options, cancel);
        QCOMPARE(result.removedDirectories.size(), 2);QVERIFY(!QFileInfo::exists(root + "/empty"));
    }
    void pruneEngineReadOnlyAndUnlogged()
    {
        const auto base = root + "/prune";QVERIFY(QDir().mkpath(base + "/unlogged"));
        write(base + "/read-only");write(base + "/unlogged/keep");
        QVERIFY(::chmod(QFile::encodeName(base + "/read-only").constData(), 0444) == 0);
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        auto result = pruneBranch(root, readMetadata(base), pruneSnapshot(base, false), {}, cancel);
        QVERIFY(!result.error.isEmpty());QVERIFY(QFileInfo::exists(base + "/read-only"));QVERIFY(QFileInfo::exists(base + "/unlogged/keep"));
        QCOMPARE(result.scan.directories.size(), 1);
        PruneOptions options;options.forceReadOnly = true;
        result = pruneBranch(root, readMetadata(base), pruneSnapshot(base, false), options, cancel);
        QVERIFY(!QFileInfo::exists(base + "/read-only"));QVERIFY(QFileInfo::exists(base + "/unlogged/keep"));
        QCOMPARE(result.removedFiles.size(), 1);QCOMPARE(result.removedDirectories.size(), 0);
    }
    void pruneEngineIdentityAndCancellation()
    {
        const auto base = root + "/prune";QVERIFY(QDir().mkpath(base + "/sub"));write(base + "/sub/keep");
        const auto source = readMetadata(base);const auto logged = pruneSnapshot(base);
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        QVERIFY(QDir().rename(base, root + "/original"));QVERIFY(QDir().mkpath(base));write(base + "/replacement");
        auto result = pruneBranch(root, source, logged, {}, cancel);QVERIFY(!result.error.isEmpty());QVERIFY(QFileInfo::exists(base + "/replacement"));
        QVERIFY(!pruneBranch(root, readMetadata(root), pruneSnapshot(root), {}, cancel).error.isEmpty());
        QVERIFY(!pruneBranch(root, readMetadata(root + "/original"), {}, {}, cancel).error.isEmpty());
        QVERIFY(QFile::link(root + "/original", root + "/redirect"));
        QVERIFY(!pruneBranch(root, readMetadata(root + "/redirect"), pruneSnapshot(root + "/redirect"), {}, cancel).error.isEmpty());
        const auto real = root + "/original";const auto snapshot = pruneSnapshot(real);
        result = pruneBranch(root, readMetadata(real), snapshot, {}, cancel, [&](const QString &path) {
            if (path == real + "/sub") {
                QDir().rename(real + "/sub", root + "/moved-sub");QDir().mkdir(real + "/sub");write(real + "/sub/replacement");
            }
        });
        QVERIFY(!result.error.isEmpty());QVERIFY(QFileInfo::exists(real + "/sub/replacement"));QVERIFY(QFileInfo::exists(root + "/moved-sub/keep"));
        cancel->store(true);result = pruneBranch(root, readMetadata(base), pruneSnapshot(base), {}, cancel);
        QVERIFY(result.cancelled);QVERIFY(result.removedFiles.isEmpty());QVERIFY(QFileInfo::exists(base + "/replacement"));
        cancel->store(false);write(base + "/second");int visited = 0;
        result = pruneBranch(root, readMetadata(base), pruneSnapshot(base), {}, cancel, [&](const QString &path) {
            if (path != base && ++visited == 2) cancel->store(true);
        });
        QVERIFY(result.cancelled);QCOMPARE(result.removedFiles.size(), 1);QVERIFY(QFileInfo::exists(base));
        QCOMPARE(result.scan.directories.first().files.size(), 1);
    }
    void pruneEnginePermissionFailure()
    {
        const auto base = root + "/prune";QVERIFY(QDir().mkdir(base));write(base + "/keep");
        const auto snapshot = pruneSnapshot(base);const auto source = readMetadata(base);
        QVERIFY(::chmod(QFile::encodeName(base).constData(), 0500) == 0);
        const auto restore = qScopeGuard([&]{::chmod(QFile::encodeName(base).constData(), 0700);});
        const auto result = pruneBranch(root, source, snapshot, {}, std::make_shared<std::atomic_bool>(false));
        QVERIFY(!result.error.isEmpty());QVERIFY(result.removedFiles.isEmpty());QVERIFY(QFileInfo::exists(base + "/keep"));
    }
    void pruneEngineTrashAndFailure()
    {
        const auto prior = qgetenv("XDG_DATA_HOME");const bool had = qEnvironmentVariableIsSet("XDG_DATA_HOME");
        const auto restore = qScopeGuard([&]{if (had) qputenv("XDG_DATA_HOME", prior);else qunsetenv("XDG_DATA_HOME");});
        const auto data = root + "/data";QVERIFY(QDir().mkdir(data));qputenv("XDG_DATA_HOME", QFile::encodeName(data));
        const auto base = root + "/prune";QVERIFY(QDir().mkpath(base + "/sub"));write(base + "/sub/trashed", "TRASH TEST");
        QVERIFY(QFile::link(root + "/beta", base + "/outside-link"));
        PruneOptions options;options.trash = true;const auto cancel = std::make_shared<std::atomic_bool>(false);
        const auto result = pruneBranch(root, readMetadata(base), pruneSnapshot(base), options, cancel);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));QVERIFY(!QFileInfo::exists(base));
        QCOMPARE(result.removedFiles.size(), 2);QCOMPARE(result.removedDirectories.size(), 2);
        QVERIFY(QFileInfo::exists(root + "/beta/externo.php"));
        QVERIFY(QFileInfo(data + "/Trash/files/outside-link").isSymLink());
        QFile file(data + "/Trash/files/trashed");QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(), QByteArray("TRASH TEST"));
        QCOMPARE(QDir(data + "/Trash/info").entryList({"*.trashinfo"}, QDir::Files).size(), 4);
        const auto blockedData = root + "/blocked-data";QVERIFY(QDir().mkdir(blockedData));write(blockedData + "/Trash", "BLOCK");
        qputenv("XDG_DATA_HOME", QFile::encodeName(blockedData));QVERIFY(QDir().mkdir(base));write(base + "/retained");
        const auto failed = pruneBranch(root, readMetadata(base), pruneSnapshot(base), options, cancel);
        QVERIFY(!failed.error.isEmpty());QVERIFY(failed.removedFiles.isEmpty());QVERIFY(QFileInfo::exists(base + "/retained"));
    }
    void prunePromptConfirmationOptionsAndSplit()
    {
        TreeWindow window(panel);window.resize(1280,800);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        window.load(true, true);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_P,Qt::AltModifier);QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().directory, panel);QCOMPARE(window.session().root, QFileInfo(panel).absolutePath());
        capture(window, "prune-confirmation-wide");QTest::keyClick(&window,Qt::Key_Return);QVERIFY(!window.busy());QVERIFY(QFileInfo::exists(panel + "/ordenes.php"));
        QTest::keyClicks(&window,"wrong");QTest::keyClick(&window,Qt::Key_Return);QVERIFY(!window.busy());
        for (const int key : {Qt::Key_F2, Qt::Key_F4, Qt::Key_F6}) {
            QTest::keyClick(&window,Qt::Key_F5);QTest::keyClick(&window,Qt::Key_F5);
            QTest::keyClicks(&window,"PRUNE");QTest::keyClick(&window,Qt::Key(key));
            QTest::keyClick(&window,Qt::Key_Return);QVERIFY(!window.busy());QVERIFY(QFileInfo::exists(panel + "/ordenes.php"));
            QTest::keyClick(&window,Qt::Key(key));
        }
        QTest::keyClick(&window,Qt::Key_F5);QTest::keyClick(&window,Qt::Key_F1);capture(window,"prune-help");QTest::keyClick(&window,Qt::Key_Escape);
        window.resize(640,400);capture(window,"prune-confirmation-80");QTest::keyClicks(&window,"prune");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY2(window.status().startsWith("Prune completed"),qPrintable(window.status()));QVERIFY(QFileInfo::exists(panel + "/ordenes.php"));
        QVERIFY(!QFileInfo::exists(panel + "/modulos"));QCOMPARE(window.session().directory,panel);
        QVERIFY(!window.oppositeSession()->directories().contains(panel + "/modulos"));
        QTest::keyClick(&window,Qt::Key_P,Qt::AltModifier);QTest::keyClicks(&window,"PRUNE");QTest::keyClick(&window,Qt::Key_Escape);
        QVERIFY(QFileInfo::exists(panel));QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_P);
        QTest::keyClicks(&window,"PRUNE");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY(!QFileInfo::exists(panel));QVERIFY(!window.session().directories().contains(panel));QVERIFY(!window.oppositeSession()->directories().contains(panel));
        QVERIFY(QFileInfo::exists(root + "/beta/externo.php"));
    }
    void prunePromptPartialRequiresExplicitYes()
    {
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_P,Qt::AltModifier);QTRY_VERIFY(!window.busy());
        QTest::keyClicks(&window,"PRUNE");QTest::keyClick(&window,Qt::Key_Return);capture(window,"prune-partial-confirmation");
        QTest::keyClick(&window,Qt::Key_Return);QVERIFY(!window.busy());QVERIFY(QFileInfo::exists(panel + "/ordenes.php"));
        QTest::keyClick(&window,Qt::Key_N);QVERIFY(QFileInfo::exists(panel));
        QTest::keyClick(&window,Qt::Key_P,Qt::AltModifier);QTest::keyClicks(&window,"PRUNE");QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);
        QTRY_VERIFY(!window.busy());QVERIFY(!QFileInfo::exists(panel + "/ordenes.php"));QVERIFY(QFileInfo::exists(panel + "/modulos/interno.php"));
        QVERIFY(window.status().contains("retained"));QVERIFY(!window.session().directories().value(panel + "/modulos").loaded);
    }
    void prunePromptCancellationWhileQueued()
    {
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_P,Qt::AltModifier);QTRY_VERIFY(!window.busy());
        auto pool = QThreadPool::globalInstance();const int maximum = pool->maxThreadCount();pool->setMaxThreadCount(1);
        QSemaphore started, release;auto blocker = QtConcurrent::run([&]{started.release();release.acquire();});
        QVERIFY(started.tryAcquire(1,5000));bool released = false;
        const auto restore = qScopeGuard([&]{if(!released)release.release();blocker.waitForFinished();pool->setMaxThreadCount(maximum);});
        QTest::keyClicks(&window,"PRUNE");QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.busy());
        QTest::qWait(150);capture(window,"prune-progress-spinner");QTest::keyClick(&window,Qt::Key_Escape);
        release.release();released = true;QTRY_VERIFY(!window.busy());
        QVERIFY(window.status().startsWith("Prune cancelled"));QVERIFY(QFileInfo::exists(panel + "/ordenes.php"));QVERIFY(QFileInfo::exists(panel + "/modulos/interno.php"));
    }
    void prunePromptPreservesPositionAndIgnoresFilters()
    {
        for (int i = 0; i < 30; ++i) { const auto path = root + QString("/folder-%1").arg(i,2,10,QChar('0'));QVERIFY(QDir().mkdir(path));write(path + "/file"); }
        TreeWindow window(root);window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Return);
        const auto outsideTags = window.session().tags;QVERIFY(outsideTags.contains(root + "/README.txt"));
        QTest::keyClick(&window,Qt::Key_Home);for(int i=0;i<18;++i)QTest::keyClick(&window,Qt::Key_Down);
        const auto path = window.session().directory;const int index = window.session().treeIndex();QVERIFY(index > 10);
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.not-visible");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_P,Qt::AltModifier);QTest::keyClicks(&window,"PRUNE");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY(!QFileInfo::exists(path));QCOMPARE(window.session().treeIndex(),index);QCOMPARE(window.session().tags,outsideTags);
        QCOMPARE(window.session().filespec.text(),QString("*.not-visible"));QVERIFY(QFileInfo::exists(root + "/README.txt"));
        capture(window,"prune-position-after-delete");
    }
    void graftEngineWholeBranchAndGuards()
    {
        const auto base=root+"/graft";QVERIFY(QDir().mkpath(base+"/branch/sub/empty"));QVERIFY(QDir().mkdir(base+"/destination"));
        write(base+"/branch/.hidden");write(base+"/branch/sub/binary",QByteArray("x\0y",3));
        QVERIFY(QFile::link(root+"/beta",base+"/branch/link"));const auto source=readMetadata(base+"/branch");
        const auto child=readMetadata(base+"/branch/sub/binary");const auto cancel=std::make_shared<std::atomic_bool>(false);
        auto result=graftBranch(source,readMetadata(base+"/destination"),cancel);
        QVERIFY2(result.moved,qPrintable(result.error));QVERIFY(result.error.isEmpty());QVERIFY(!result.crossDevice);
        QVERIFY(!QFileInfo::exists(source.path));QCOMPARE(readMetadata(result.target).inode,source.inode);
        QCOMPARE(readMetadata(result.target+"/sub/binary").inode,child.inode);QCOMPARE(readMetadata(result.target+"/sub/binary").modified,child.modified);
        QVERIFY(QFileInfo::exists(result.target+"/.hidden"));QVERIFY(QFileInfo(result.target+"/link").isSymLink());QVERIFY(QFileInfo::exists(root+"/beta/externo.php"));
        QVERIFY(QDir().mkpath(base+"/branch/sub"));write(base+"/branch/keep");
        QVERIFY(!graftBranch(readMetadata(base+"/branch"),readMetadata(base+"/destination"),cancel).moved);
        QVERIFY(QFileInfo::exists(base+"/branch/keep"));QVERIFY(!graftBranch(readMetadata(base+"/branch"),readMetadata(base+"/branch/sub"),cancel).moved);
        QVERIFY(!graftBranch(readMetadata(base+"/branch"),readMetadata(base),cancel).moved);
        QVERIFY(!graftBranch(readMetadata(root+"/beta/externo.php"),readMetadata(base),cancel).moved);
        QVERIFY(QFile::link(base+"/branch",base+"/redirect"));QVERIFY(!graftBranch(readMetadata(base+"/redirect"),readMetadata(base),cancel).moved);
        cancel->store(true);QVERIFY(graftBranch(readMetadata(base+"/branch"),readMetadata(base+"/destination"),cancel).cancelled);
        QVERIFY(QFileInfo::exists(base+"/branch/keep"));
    }
    void graftEngineChangedParentsAndRollback()
    {
        const auto base=root+"/graft";QVERIFY(QDir().mkpath(base+"/source"));QVERIFY(QDir().mkdir(base+"/destination"));write(base+"/source/keep");
        const auto old=readMetadata(base+"/source");QVERIFY(QDir().rename(old.path,base+"/original"));QVERIFY(QDir().mkdir(old.path));
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        QVERIFY(!graftBranch(old,readMetadata(base+"/destination"),cancel).moved);
        const auto destination=readMetadata(base+"/destination");QVERIFY(QDir().rename(destination.path,base+"/old-destination"));QVERIFY(QFile::link(base+"/old-destination",destination.path));
        QVERIFY(!graftBranch(readMetadata(base+"/original"),destination,cancel).moved);QVERIFY(QFileInfo::exists(base+"/original/keep"));
        QFile::remove(destination.path);QVERIFY(QDir().mkdir(destination.path));
        auto result=graftBranch(readMetadata(base+"/original"),readMetadata(destination.path),cancel,[&](const QString &){QVERIFY(QDir().mkdir(destination.path+"/original"));write(destination.path+"/original/collision");});
        QVERIFY(!result.moved);QVERIFY(result.recoverySource.isEmpty());QVERIFY(QFileInfo::exists(base+"/original/keep"));QVERIFY(QFileInfo::exists(destination.path+"/original/collision"));
        QVERIFY(QDir().remove(destination.path+"/original/collision"));QVERIFY(QDir().rmdir(destination.path+"/original"));
        result=graftBranch(readMetadata(base+"/original"),readMetadata(destination.path),cancel,[&](const QString &){cancel->store(true);});
        QVERIFY(result.cancelled);QVERIFY(QFileInfo::exists(base+"/original/keep"));
        cancel->store(false);
        result=graftBranch(readMetadata(base+"/original"),readMetadata(destination.path),cancel,[&](const QString &){
            QVERIFY(QDir().mkdir(base+"/original"));write(base+"/original/new");QVERIFY(QDir().mkdir(destination.path+"/original"));
        });
        QVERIFY(!result.moved);QVERIFY(!result.recoverySource.isEmpty());QVERIFY(QFileInfo::exists(result.recoverySource+"/keep"));QVERIFY(QFileInfo::exists(base+"/original/new"));
    }
    void graftEngineAcrossFilesystems()
    {
        QTemporaryDir destination("/dev/shm/ltree-graft-test-XXXXXX");QVERIFY(destination.isValid());
        QVERIFY(QDir().mkpath(root+"/graft/sub/empty"));write(root+"/graft/.hidden",QByteArray("ABC\0DEF",7));
        QVERIFY(::chmod(QFile::encodeName(root+"/graft/.hidden").constData(),0440)==0);
        QVERIFY(::link(QFile::encodeName(root+"/graft/.hidden").constData(),QFile::encodeName(root+"/graft/sub/hardlink").constData())==0);
        QVERIFY(QFile::link(root+"/beta",root+"/graft/link"));
        const auto file=readMetadata(root+"/graft/.hidden");
        const auto bytes=QFile::encodeName(root+"/graft/.hidden");
        const bool attr=::setxattr(bytes.constData(),"user.ltree-test","GRAFT",5,0)==0;
        const auto source=readMetadata(root+"/graft");QVERIFY(source.device!=readMetadata(destination.path()).device);
        const auto result=graftBranch(source,readMetadata(destination.path()),std::make_shared<std::atomic_bool>(false));
        QVERIFY2(result.moved,qPrintable(result.error));QVERIFY(result.crossDevice);QVERIFY(!QFileInfo::exists(source.path));
        QFile copied(result.target+"/.hidden");QVERIFY(copied.open(QIODevice::ReadOnly));QCOMPARE(copied.readAll(),QByteArray("ABC\0DEF",7));
        const auto after=readMetadata(copied.fileName());QCOMPARE(after.mode,file.mode);QCOMPARE(after.modified,file.modified);
        QCOMPARE(after.inode,readMetadata(result.target+"/sub/hardlink").inode);QVERIFY(QFileInfo(result.target+"/link").isSymLink());
        QVERIFY(QFileInfo::exists(root+"/beta/externo.php"));QVERIFY(QFileInfo::exists(result.target+"/sub/empty"));
        if(attr){char value[10]{};QCOMPARE(::getxattr(QFile::encodeName(copied.fileName()).constData(),"user.ltree-test",value,sizeof(value)),ssize_t(5));QCOMPARE(QByteArray(value,5),QByteArray("GRAFT"));}
    }
    void graftEngineAcrossFilesystemsFailure()
    {
        QTemporaryDir destination("/dev/shm/ltree-graft-test-XXXXXX");QVERIFY(destination.isValid());
        QVERIFY(QDir().mkdir(root+"/graft"));write(root+"/graft/blocked");
        QVERIFY(::chmod(QFile::encodeName(root+"/graft/blocked").constData(),0000)==0);
        const auto restore=qScopeGuard([&]{::chmod(QFile::encodeName(root+"/graft/blocked").constData(),0600);});
        const auto result=graftBranch(readMetadata(root+"/graft"),readMetadata(destination.path()),std::make_shared<std::atomic_bool>(false));
        QVERIFY(!result.moved);QVERIFY(result.crossDevice);QVERIFY(!result.error.isEmpty());QVERIFY(QFileInfo::exists(root+"/graft/blocked"));QVERIFY(result.recoverySource.isEmpty());
    }
    void graftEngineInterruptedCrossFilesystemTransfer()
    {
        QTemporaryDir destination("/dev/shm/ltree-graft-test-XXXXXX");QVERIFY(destination.isValid());
        QVERIFY(QDir().mkdir(root+"/graft"));write(root+"/graft/first","FIRST");write(root+"/graft/second","SECOND");
        const auto bin=root+"/test-bin";QVERIFY(QDir().mkdir(bin));
        // Un ejecutable de prueba simula la retirada parcial sin usar archivos personales.
        write(bin+"/mv",R"PY(#!/usr/bin/python3
import os, shutil, sys, time
source, target = sys.argv[-2:]
shutil.copytree(source, target, symlinks=True)
os.unlink(source + '/first')
with open(os.environ['LTREE_GRAFT_TEST_MARKER'], 'w') as marker:
    marker.write('ready')
while True:
    time.sleep(0.05)
)PY");
        QVERIFY(::chmod(QFile::encodeName(bin+"/mv").constData(),0700)==0);
        const auto path=qgetenv("PATH"),marker=qgetenv("LTREE_GRAFT_TEST_MARKER");const bool hadMarker=qEnvironmentVariableIsSet("LTREE_GRAFT_TEST_MARKER");
        const auto restore=qScopeGuard([&]{qputenv("PATH",path);if(hadMarker)qputenv("LTREE_GRAFT_TEST_MARKER",marker);else qunsetenv("LTREE_GRAFT_TEST_MARKER");});
        qputenv("PATH",QFile::encodeName(bin)+':'+path);qputenv("LTREE_GRAFT_TEST_MARKER",QFile::encodeName(root+"/ready"));
        const auto source=readMetadata(root+"/graft"),to=readMetadata(destination.path());const auto cancel=std::make_shared<std::atomic_bool>(false);
        auto future=QtConcurrent::run([&]{return graftBranch(source,to,cancel);});
        const auto stop=qScopeGuard([&]{cancel->store(true);future.waitForFinished();});
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root+"/ready"),5000);cancel->store(true);future.waitForFinished();
        const auto result=future.result();QVERIFY(result.cancelled);QVERIFY(!result.moved);QVERIFY(result.recoverySource.isEmpty());
        QVERIFY(!QFileInfo::exists(root+"/graft/first"));QVERIFY(QFileInfo::exists(root+"/graft/second"));
        QFile first(result.target+"/first");QVERIFY(first.open(QIODevice::ReadOnly));QCOMPARE(first.readAll(),QByteArray("FIRST"));
        QFile second(result.target+"/second");QVERIFY(second.open(QIODevice::ReadOnly));QCOMPARE(second.readAll(),QByteArray("SECOND"));
        bool loaded=false;for(const auto &dir:result.scan.directories)if(dir.path==source.path){loaded=true;QCOMPARE(dir.files.size(),1);}QVERIFY(loaded);
    }
    void graftSessionReparentsLoadedBranches()
    {
        Session session(root);session.apply(scan(root,true),true);session.selectDirectory(panel);session.tags.insert(panel+"/ordenes.php");
        const auto target=root+"/beta/sublimepanel";session.graftPath(panel,target);
        QCOMPARE(session.directory,target);QVERIFY(session.currentDirectory().loaded);QVERIFY(!session.directories().value(QFileInfo(panel).absolutePath()).children.contains(target));
        QVERIFY(session.directories().value(root+"/beta").children.contains(target));QVERIFY(session.tags.contains(target+"/ordenes.php"));
        bool found=false;for(const auto &row:session.tree())if(row.path==target)found=true;QVERIFY(found);
        const auto outside=root+"-outside/sublimepanel";session.graftPath(target,outside);
        QCOMPARE(session.root,QFileInfo(outside).absolutePath());QCOMPARE(session.directory,outside);QVERIFY(session.currentDirectory().loaded);
        QVERIFY(!session.directories().contains(root+"/beta"));QVERIFY(session.tags.contains(outside+"/ordenes.php"));
    }
    void graftPromptSplitMovesUnloggedFilesAndKeepsTags()
    {
        TreeWindow window(panel);window.resize(1280,800);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);QVERIFY(window.selectRoot(root+"/beta"));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Tab);
        const auto tags=window.session().tags;QVERIFY(tags.contains(panel+"/ordenes.php"));
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.does-not-match");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_G,Qt::AltModifier);capture(window,"graft-prompt-split");
        QTest::keyClick(&window,Qt::Key_Tab);capture(window,"graft-tab-current");QTest::keyClick(&window,Qt::Key_Tab);capture(window,"graft-tab-opposite");
        QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(QFileInfo::exists(panel));
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_G);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        const auto target=root+"/beta/sublimepanel";QVERIFY2(QFileInfo::exists(target+"/modulos/interno.php"),qPrintable(window.status()));QVERIFY(!QFileInfo::exists(panel));
        QCOMPARE(window.session().root,target);QCOMPARE(window.session().directory,target);QVERIFY(window.session().tags.contains(target+"/ordenes.php"));
        QCOMPARE(window.session().filespec.text(),QString("*.does-not-match"));QVERIFY(!window.session().directories().value(target+"/modulos").loaded);
        QVERIFY(window.oppositeSession()->directories().contains(target));QVERIFY(!window.oppositeSession()->directories().contains(panel));capture(window,"graft-after-move");
    }
    void graftPromptBrowseHistoryAndRetry()
    {
        {
            TreeWindow window(panel);window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());
            QTest::keyClick(&window,Qt::Key_G,Qt::AltModifier);QTest::keyClicks(&window,root+"/beta");
            capture(window,"graft-prompt-80");QTest::keyClick(&window,Qt::Key_F2);QTRY_VERIFY(!window.busy());capture(window,"graft-browse");
            QCOMPARE(window.session().root,panel);QTest::keyClick(&window,Qt::Key_Down);QTest::keyClick(&window,Qt::Key_Return);capture(window,"graft-browse-selected");
            QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Up);capture(window,"graft-history");
            QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Escape);
            QVERIFY(QFileInfo::exists(panel));
        }
        QFile history(root+"/graft-history.json");QVERIFY(history.open(QIODevice::ReadOnly));QVERIFY(!history.permissions().testFlag(QFile::ReadOther));
        QCOMPARE(QJsonDocument::fromJson(history.readAll()).object().value("graft").toArray().size(),1);
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_G,Qt::AltModifier);
        QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        const auto target=root+"/beta/vacio/sublimepanel";QVERIFY2(QFileInfo::exists(target),qPrintable(window.status()));
        QTest::keyClick(&window,Qt::Key_G,Qt::AltModifier);QTest::keyClicks(&window,target);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY(QFileInfo::exists(target));QVERIFY(window.status().contains("itself"));
        QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);QTest::keyClicks(&window,root+"/beta");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY2(QFileInfo::exists(root+"/beta/sublimepanel"),qPrintable(window.status()));
    }
    void graftPromptCancelledWhileQueued()
    {
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_G,Qt::AltModifier);QTest::keyClicks(&window,root+"/beta");
        auto pool=QThreadPool::globalInstance();const int maximum=pool->maxThreadCount();pool->setMaxThreadCount(1);
        QSemaphore started,release;auto blocker=QtConcurrent::run([&]{started.release();release.acquire();});QVERIFY(started.tryAcquire(1,5000));bool released=false;
        const auto restore=qScopeGuard([&]{if(!released)release.release();blocker.waitForFinished();pool->setMaxThreadCount(maximum);});
        QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.busy());QTest::qWait(150);capture(window,"graft-spinner");QTest::keyClick(&window,Qt::Key_Escape);
        release.release();released=true;QTRY_VERIFY(!window.busy());QVERIFY(QFileInfo::exists(panel));QVERIFY(!QFileInfo::exists(root+"/beta/sublimepanel"));QVERIFY(window.status().startsWith("Graft cancelled"));
    }
    void directoryLinkEngine()
    {
        const auto base=root+"/links";QVERIFY(QDir().mkpath(base+"/source"));QVERIFY(QDir().mkpath(base+"/destination"));
        const auto source=readMetadata(base+"/source");const auto cancel=std::make_shared<std::atomic_bool>(false);
        auto result=createDirectoryLink(source,base+"/destination/a link",cancel);
        QVERIFY2(result.created,qPrintable(result.error));QVERIFY(QFileInfo(result.path).isSymLink());QCOMPARE(QFileInfo(result.path).symLinkTarget(),source.path);
        QVERIFY(!createDirectoryLink(source,result.path,cancel).created);QVERIFY(QFileInfo(result.path).isSymLink());
        write(base+"/destination/existing","KEEP");QVERIFY(!createDirectoryLink(source,base+"/destination/existing",cancel).created);
        QFile keep(base+"/destination/existing");QVERIFY(keep.open(QIODevice::ReadOnly));QCOMPARE(keep.readAll(),QByteArray("KEEP"));
        QVERIFY(QFile::link(base+"/missing",base+"/destination/dangling"));QVERIFY(!createDirectoryLink(source,base+"/destination/dangling",cancel).created);
        QVERIFY(QFile::link(base+"/destination",base+"/redirect"));QVERIFY(!createDirectoryLink(source,base+"/redirect/escaped",cancel).created);
        QVERIFY(!QFileInfo::exists(base+"/destination/escaped"));QVERIFY(!createDirectoryLink(source,base+"/absent/link",cancel).created);
        QVERIFY(!createDirectoryLink(source,"relative",cancel).created);QVERIFY(!createDirectoryLink(readMetadata(result.path),base+"/destination/nested",cancel).created);
        cancel->store(true);QVERIFY(createDirectoryLink(source,base+"/destination/cancelled",cancel).cancelled);QVERIFY(!QFileInfo::exists(base+"/destination/cancelled"));cancel->store(false);
        const auto old=readMetadata(base+"/source");QVERIFY(QDir().rename(base+"/source",base+"/old-source"));QVERIFY(QDir().mkdir(base+"/source"));
        QVERIFY(!createDirectoryLink(old,base+"/destination/changed",cancel).created);
    }
    void globalSessionDeduplicatesAndFilters()
    {
        Session parent(root);parent.apply(scan(root,true),true);Session nested(panel);nested.apply(scan(panel,true),true);
        nested.tags.insert(panel+"/ordenes.php");QVERIFY(nested.filespec.set("*.php"));
        auto global=Session::global({parent,nested},nested);
        QCOMPARE(global.view,View::Global);QSet<QString> paths;for(const auto &file:global.files()){QVERIFY(!paths.contains(file.path));paths.insert(file.path);QVERIFY(file.name.endsWith(".php"));}
        QVERIFY(paths.contains(root+"/beta/externo.php"));QVERIFY(paths.contains(panel+"/ordenes.php"));
        auto tagged=Session::global({parent,nested},nested,true);QCOMPARE(tagged.files().size(),1);
        global.removeFile(panel+"/ordenes.php");nested.syncGlobalFrom(global);QVERIFY(!nested.tags.contains(panel+"/ordenes.php"));
        QCOMPARE(nested.currentDirectory().files.size(),2);QCOMPARE(nested.root,panel);
    }
    void globalKeyboardAcrossLoggedLocations()
    {
        const auto base=root+"/global-keyboard";QVERIFY(QDir().mkpath(base+"/one/sub"));QVERIFY(QDir().mkpath(base+"/two"));
        write(base+"/one/a.txt");write(base+"/one/sub/hidden.txt");write(base+"/two/b.txt");
        TreeWindow window(base+"/one");window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());
        QVERIFY(window.selectRoot(base+"/two"));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_G);
        QCOMPARE(window.session().view,View::Global);QCOMPARE(window.session().files().size(),2);capture(window,"dir-global-80");
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Return);
        QCOMPARE(window.session().root,base+"/two");QCOMPARE(window.session().view,View::Tree);QVERIFY(window.session().tags.contains(base+"/two/b.txt"));
        QVERIFY(window.selectRoot(base+"/one"));QVERIFY(window.session().tags.contains(base+"/one/a.txt"));QVERIFY(!window.session().tags.contains(base+"/two/b.txt"));
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_G);QCOMPARE(window.session().view,View::Global);QVERIFY(window.session().tagsOnly);QCOMPARE(window.session().files().size(),2);
        QTest::keyClick(&window,Qt::Key_Escape);window.load(true,true);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_G);QCOMPARE(window.session().files().size(),3);
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"a.txt");QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().files().size(),1);
        QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(window.session().root,base+"/one");QCOMPARE(window.session().view,View::Tree);
    }
    void globalDeletePersistsInSavedLocations()
    {
        const auto base=root+"/global-delete";QVERIFY(QDir().mkpath(base+"/one"));QVERIFY(QDir().mkpath(base+"/two"));write(base+"/one/a.txt");write(base+"/two/b.txt");
        TreeWindow window(base+"/one");window.show();QTRY_VERIFY(!window.busy());QVERIFY(window.selectRoot(base+"/two"));QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_G);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);
        confirmTaggedDelete(window);QTRY_VERIFY(!window.busy());QVERIFY(!QFileInfo::exists(base+"/one/a.txt"));QVERIFY(!QFileInfo::exists(base+"/two/b.txt"));
        QCOMPARE(window.session().view,View::Global);QVERIFY(window.session().files().isEmpty());QTest::keyClick(&window,Qt::Key_Return);
        QCOMPARE(window.session().root,base+"/two");QVERIFY(window.session().currentDirectory().files.isEmpty());
        QVERIFY(window.selectRoot(base+"/one"));QVERIFY(window.session().currentDirectory().files.isEmpty());QVERIFY(window.session().tags.isEmpty());
    }
    void globalCopyAndRenameAcrossLocations()
    {
        const auto base=root+"/global-operations";QVERIFY(QDir().mkpath(base+"/one"));QVERIFY(QDir().mkpath(base+"/two"));QVERIFY(QDir().mkpath(base+"/destination"));
        write(base+"/one/a.txt","FIRST");write(base+"/two/b.txt","SECOND");
        TreeWindow window(base+"/one");window.show();QTRY_VERIFY(!window.busy());QVERIFY(window.selectRoot(base+"/two"));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_G);
        QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(window.session().currentFile()->path,base+"/one/a.txt");
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClicks(&window,qPrintable(base+"/destination"));QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY2(QFileInfo::exists(base+"/destination/a.txt"),qPrintable(window.status()));QCOMPARE(window.session().files().size(),2);
        QTest::keyClick(&window,Qt::Key_R);QTest::keyClicks(&window,"renamed.txt");QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());
        QVERIFY2(QFileInfo::exists(base+"/one/renamed.txt"),qPrintable(window.status()));QVERIFY(!QFileInfo::exists(base+"/one/a.txt"));
        QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(window.selectRoot(base+"/one"));QCOMPARE(window.session().currentDirectory().files.first().name,QString("renamed.txt"));
    }
    void globalSplitRestoresBothPanes()
    {
        const auto base=root+"/global-split";QVERIFY(QDir().mkpath(base+"/one"));QVERIFY(QDir().mkpath(base+"/two"));write(base+"/one/a.txt");write(base+"/two/b.txt");
        TreeWindow window(base+"/one");window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_F8);QVERIFY(window.selectRoot(base+"/two"));QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_G);QCOMPARE(window.session().files().size(),2);QTest::keyClick(&window,Qt::Key_Tab);QCOMPARE(window.session().root,base+"/one");
        QTest::keyClick(&window,Qt::Key_G);QCOMPARE(window.session().files().size(),2);QTest::keyClick(&window,Qt::Key_U,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Tab);
        QCOMPARE(window.session().view,View::Global);QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(window.session().root,base+"/two");
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(window.session().root,base+"/one");QCOMPARE(window.session().view,View::Tree);
        QTest::keyClick(&window,Qt::Key_G);QTest::keyClick(&window,Qt::Key_F8);QCOMPARE(window.session().view,View::Global);QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(window.session().root,base+"/one");
    }
    void loggedLocationsCycleAndPreserveState()
    {
        const auto base=root+"/cycle";QVERIFY(QDir().mkpath(base+"/one"));QVERIFY(QDir().mkpath(base+"/two"));write(base+"/one/a.txt");write(base+"/one/b.txt");write(base+"/two/c.txt");
        TreeWindow window(base+"/one");window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Greater);QVERIFY(window.status().contains("Only one"));
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_End);QTest::keyClick(&window,Qt::Key_T);const auto selected=window.session().currentFile()->path;
        QVERIFY(window.selectRoot(base+"/two"));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_Greater);QCOMPARE(window.session().root,base+"/one");QCOMPARE(window.session().currentFile()->path,selected);QVERIFY(window.session().tags.contains(selected));
        QCOMPARE(window.oppositeSession()->root,base+"/two");QTest::keyClick(&window,Qt::Key_Less);QCOMPARE(window.session().root,base+"/two");
        QTest::keyClick(&window,Qt::Key_Period);QCOMPARE(window.session().root,base+"/one");QTest::keyClick(&window,Qt::Key_Comma);QCOMPARE(window.session().root,base+"/two");
        QTest::keyClick(&window,Qt::Key_G);QTest::keyClick(&window,Qt::Key_Less);QCOMPARE(window.session().root,base+"/one");QCOMPARE(window.session().view,View::Directory);
    }
    void availableSpaceDoesNotNavigate()
    {
        TreeWindow window(panel);window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());const auto before=window.session().directory;
        QTest::keyClick(&window,Qt::Key_A);capture(window,"dir-available-80");QTest::keyClick(&window,Qt::Key_F5);QTest::keyClick(&window,Qt::Key_Return);
        QCOMPARE(window.session().directory,before);QCOMPARE(window.session().root,panel);QVERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T);QTest::keyClick(&window,Qt::Key_Return);const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_A);QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(window.session().tags,tags);QCOMPARE(window.session().directory,before);
    }
    void shortcutKeyboardAndOppositePane()
    {
        const auto base=root+"/shortcut";QVERIFY(QDir().mkpath(base+"/source"));QVERIFY(QDir().mkpath(base+"/destination"));
        TreeWindow window(base+"/source");window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_F8);QVERIFY(window.selectRoot(base+"/destination"));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_H);capture(window,"dir-shortcut-80");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY(QFileInfo(base+"/destination/source").isSymLink());QCOMPARE(QFileInfo(base+"/destination/source").symLinkTarget(),base+"/source");
        QCOMPARE(window.session().directory,base+"/source");QVERIFY(window.oppositeSession()->currentDirectory().children.contains(base+"/destination/source"));
        QTest::keyClick(&window,Qt::Key_H);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("already exists"));
        QTest::keyClicks(&window,qPrintable(base+"/destination/new link"));QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY(QFileInfo(base+"/destination/new link").isSymLink());
        QTest::keyClick(&window,Qt::Key_H);QTest::keyClicks(&window,"cancelled");QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(!QFileInfo::exists(base+"/source/cancelled"));
        QTest::keyClick(&window,Qt::Key_H);QTest::keyClicks(&window,"relative-link");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY(QFileInfo(base+"/source/relative-link").isSymLink());QCOMPARE(QFileInfo(base+"/source/relative-link").symLinkTarget(),base+"/source");
    }
    void alphaPendingWorkersCancelWithoutMutation()
    {
        const auto base=root+"/alpha-cancel";QVERIFY(QDir().mkpath(base+"/sub"));write(base+"/a.txt","A");write(base+"/sub/a.txt","A");
        TreeWindow window(base);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);const auto tags=window.session().tags;
        auto *pool=QThreadPool::globalInstance();const auto original=pool->maxThreadCount();pool->setMaxThreadCount(1);
        const auto restore=qScopeGuard([&]{pool->setMaxThreadCount(original);});
        const auto cancelled=[&](auto launch){
            QSemaphore release,started;auto blocker=QtConcurrent::run(pool,[&]{started.release();release.acquire();});
            const auto unblock=qScopeGuard([&]{release.release();blocker.waitForFinished();});
            QVERIFY(started.tryAcquire(1,2000));launch();QVERIFY(window.busy());QTest::keyClick(&window,Qt::Key_Escape);
            release.release();QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("cancelled"));QCOMPARE(window.session().tags,tags);
        };
        const auto mode=readMetadata(base+"/a.txt").mode;
        cancelled([&]{QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(&window,"600");QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);});
        QCOMPARE(readMetadata(base+"/a.txt").mode,mode);
        cancelled([&]{QTest::keyClick(&window,Qt::Key_R,Qt::ControlModifier);QTest::keyClicks(&window,"*.bak");QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);});
        QVERIFY(QFileInfo::exists(base+"/a.txt"));QVERIFY(!QFileInfo::exists(base+"/a.bak"));
        cancelled([&]{QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_K);QTest::keyClick(&window,Qt::Key_C);});
        QVERIFY(!window.session().hasCompareFilter());QCOMPARE(window.session().files().size(),2);
        QTest::keyClick(&window,Qt::Key_Return);
        cancelled([&]{QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Return);});
    }
    void taggedPermissionsRespectModesFiltersAndErrors()
    {
        const auto base=root+"/batch-mode";QVERIFY(QDir().mkpath(base+"/sub"));
        write(base+"/a.txt");write(base+"/sub/b.txt");write(base+"/keep.php");
        QVERIFY(::chmod(QFile::encodeName(base+"/a.txt").constData(),0640)==0);
        QVERIFY(::chmod(QFile::encodeName(base+"/sub/b.txt").constData(),0600)==0);
        QVERIFY(QFile::link(base+"/keep.php",base+"/link.txt"));
        TreeWindow window(base);window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.txt");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_A);
        QTest::keyClicks(&window,"go-r");QTest::keyClick(&window,Qt::Key_Return);capture(window,"alpha-tagged-permissions-review-80");
        QCOMPARE(readMetadata(base+"/a.txt").mode,quint32(0640));QTest::keyClick(&window,Qt::Key_Y);
        QTRY_VERIFY(!window.busy());QVERIFY2(window.status().contains("2/3"),qPrintable(window.status()));QVERIFY(window.status().contains("1 failed"));
        QCOMPARE(readMetadata(base+"/a.txt").mode,quint32(0600));QCOMPARE(readMetadata(base+"/sub/b.txt").mode,quint32(0600));
        QCOMPARE(window.session().tags,tags);QVERIFY(QFileInfo(base+"/keep.php").isReadable());QCOMPARE(window.session().view,View::Branch);
        QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(&window,"644");QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_N);
        QCOMPARE(readMetadata(base+"/a.txt").mode,quint32(0600));QCOMPARE(window.session().tags,tags);
    }
    void renameEngineIdentityLinksCollisionsAndDirectories()
    {
        const auto base=root+"/rename-engine";QVERIFY(QDir().mkpath(base+"/dir/sub"));write(base+"/old.txt","DATA");write(base+"/exists.txt","KEEP");
        write(base+"/dir/sub/.keep","CHILD");QVERIFY(::chmod(QFile::encodeName(base+"/old.txt").constData(),0000)==0);
        auto cancel=std::make_shared<std::atomic_bool>(false);const auto before=readMetadata(base+"/old.txt");
        auto result=renameItem(before,"exists.txt",cancel);QVERIFY(!result.renamed);QVERIFY(result.error.contains("exists"));
        QCOMPARE(readMetadata(base+"/old.txt").inode,before.inode);QCOMPARE(readMetadata(base+"/old.txt").mode,quint32(0));
        result=renameItem(readMetadata(base+"/old.txt"),"new.txt",cancel);QVERIFY2(result.renamed,qPrintable(result.error));
        QCOMPARE(readMetadata(base+"/new.txt").inode,before.inode);QCOMPARE(readMetadata(base+"/new.txt").mode,quint32(0));
        const auto dir=readMetadata(base+"/dir");result=renameItem(dir,"renamed-dir",cancel);QVERIFY2(result.renamed,qPrintable(result.error));
        QVERIFY(QFileInfo::exists(base+"/renamed-dir/sub/.keep"));QCOMPARE(readMetadata(base+"/renamed-dir").inode,dir.inode);
        QVERIFY(QFile::link(base+"/exists.txt",base+"/link"));QVERIFY(!renameItem(readMetadata(base+"/link"),"no",cancel).renamed);
        QVERIFY(QFile::link(base+"/renamed-dir",base+"/parent-link"));QVERIFY(!renameItem(readMetadata(base+"/parent-link/sub/.keep"),"no",cancel).renamed);
        const auto stale=readMetadata(base+"/exists.txt");QVERIFY(QFile::remove(base+"/exists.txt"));write(base+"/exists.txt","REPLACED");
        QVERIFY(!renameItem(stale,"bad",cancel).renamed);QVERIFY(QFileInfo::exists(base+"/exists.txt"));
        QVERIFY(!renameItem(readMetadata(base+"/exists.txt"),"../bad",cancel).renamed);
        cancel->store(true);QVERIFY(renameItem(readMetadata(base+"/exists.txt"),"cancelled",cancel).cancelled);
        QVERIFY(QDir(base).entryList({".ltree-rename-*"},QDir::AllEntries|QDir::Hidden).isEmpty());
    }
    void renameMasksSplitAndDirectoryCache()
    {
        const auto base=root+"/rename-ui";QVERIFY(QDir().mkpath(base+"/sub"));write(base+"/a.txt","A");write(base+"/sub/a.txt","B");write(base+"/keep.php");
        TreeWindow window(base);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.txt");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_R,Qt::ControlModifier);QTest::keyClicks(&window,"*.bak");QTest::keyClick(&window,Qt::Key_Return);capture(window,"alpha-rename-mask-review");QTest::keyClick(&window,Qt::Key_Y);
        QTRY_VERIFY(window.status().contains("Rename completed"));QVERIFY2(window.status().contains("2/2"),qPrintable(window.status()));
        QVERIFY(QFileInfo::exists(base+"/a.bak"));QVERIFY(QFileInfo::exists(base+"/sub/a.bak"));
        QCOMPARE(window.session().tags,(QSet<QString>{base+"/a.bak",base+"/sub/a.bak",base+"/keep.php"}));
        QVERIFY(window.oppositeSession()->tags.contains(base+"/sub/a.bak"));QVERIFY(window.session().files().isEmpty());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_F);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(window.session().directory,base+"/sub");
        QTest::keyClick(&window,Qt::Key_R);QTest::keyClicks(&window,"renamed");QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);
        QTRY_VERIFY(window.status().contains("Rename completed"));QCOMPARE(window.session().directory,base+"/renamed");
        QVERIFY(window.session().directories().value(base+"/renamed").loaded);QVERIFY(window.session().tags.contains(base+"/renamed/a.bak"));
        QVERIFY(window.oppositeSession()->directories().contains(base+"/renamed"));QVERIFY(!window.oppositeSession()->directories().contains(base+"/sub"));
        QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().currentFile()->path,base+"/renamed/a.bak");
        QTest::keyClick(&window,Qt::Key_R);QTest::keyClicks(&window,"single.txt");QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);
        QTRY_VERIFY(window.status().contains("Rename completed"));QCOMPARE(window.session().currentFile()->path,base+"/renamed/single.txt");
    }
    void renameRootConfirmEachSkipAndStale()
    {
        const auto base=root+"/rename-root";QVERIFY(QDir().mkdir(base));write(base+"/a.txt","A");write(base+"/b.txt","B");
        TreeWindow window(base);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_R);QTest::keyClicks(&window,"new-root");QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);
        QTRY_VERIFY(window.status().contains("Rename completed"));const auto moved=root+"/new-root";
        QCOMPARE(window.session().root,moved);QCOMPARE(window.oppositeSession()->root,moved);QVERIFY(QFileInfo::exists(moved+"/a.txt"));
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_R,Qt::ControlModifier);QTest::keyClicks(&window,"*.bak");QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);
        QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(window.status().contains("Rename completed"));
        QVERIFY(QFileInfo::exists(moved+"/a.txt"));QVERIFY(QFileInfo::exists(moved+"/b.bak"));QVERIFY(window.session().tags.contains(moved+"/a.txt"));
        QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_R);QTest::keyClicks(&window,"stale.txt");QTest::keyClick(&window,Qt::Key_Return);
        QVERIFY(::chmod(QFile::encodeName(moved+"/a.txt").constData(),0600)==0);QTest::keyClick(&window,Qt::Key_Y);
        QTRY_VERIFY(window.status().contains("Rename completed"));QVERIFY(window.status().contains("changed since review"));QVERIFY(!QFileInfo::exists(moved+"/stale.txt"));
    }
    void directoryCompareCriteriaBinaryAndCase()
    {
        const auto a=root+"/compare-a",b=root+"/compare-b";QVERIFY(QDir().mkpath(a+"/sub"));QVERIFY(QDir().mkpath(b+"/sub"));
        write(a+"/same","abc");write(b+"/same","abc");write(a+"/changed","abcd");write(b+"/changed","wxyz");
        write(a+"/unique","u");write(a+"/sub/new","n");write(b+"/sub/new","n");
        write(a+"/Case","C");write(b+"/case","C");
        QVector<FileEntry> source,target;for(const auto &dir:scan(a,true).directories)source+=dir.files;for(const auto &dir:scan(b,true).directories)target+=dir.files;
        auto cancel=std::make_shared<std::atomic_bool>(false);DirectoryCompareOptions options;options.newer=false;options.unique=false;options.binary=2;
        auto result=compareDirectories(source,a,target,b,options,cancel);QVERIFY(result.errors.isEmpty());QCOMPARE(result.matches,QSet<QString>{a+"/changed"});
        options.unique=true;result=compareDirectories(source,a,target,b,options,cancel);QCOMPARE(result.matches,(QSet<QString>{a+"/changed",a+"/unique",a+"/Case"}));
        options.caseSensitive=false;result=compareDirectories(source,a,target,b,options,cancel);QCOMPARE(result.matches,(QSet<QString>{a+"/changed",a+"/unique"}));
        options.binary=1;options.unique=false;result=compareDirectories(source,a,target,b,options,cancel);
        QCOMPARE(result.matches,(QSet<QString>{a+"/same",a+"/Case",a+"/sub/new"}));
        for(auto &file:source)file.modified=QDateTime::fromMSecsSinceEpoch(2000);for(auto &file:target)file.modified=QDateTime::fromMSecsSinceEpoch(1000);
        options.binary=2;options.newer=true;result=compareDirectories(source,a,target,b,options,cancel);QCOMPARE(result.matches,QSet<QString>{a+"/changed"});
        options.binary=0;options.newer=false;options.older=true;QVERIFY(compareDirectories(source,a,target,b,options,cancel).matches.isEmpty());
        options.older=false;options.identical=1;QCOMPARE(compareDirectories(source,a,target,b,options,cancel).matches.size(),4);
        options.identical=2;QVERIFY(compareDirectories(source,a,target,b,options,cancel).matches.isEmpty());
        write(b+"/Case","AMBIGUOUS");target=scan(b).directories.first().files;options.binary=1;
        QVERIFY(!compareDirectories(source,a,target,b,options,cancel).errors.isEmpty());
        cancel->store(true);QVERIFY(compareDirectories(source,a,target,b,options,cancel).cancelled);
    }
    void directoryCompareUiDirectoryBranchAndErrors()
    {
        const auto a=root+"/dircmp-ui",b=root+"/dircmp-other";QVERIFY(QDir().mkpath(a+"/sub"));QVERIFY(QDir().mkpath(b+"/sub"));
        write(a+"/same","S");write(b+"/same","S");write(a+"/changed","A");write(b+"/changed","B");write(a+"/unique","U");
        write(a+"/sub/child","A");write(b+"/sub/child","B");
        TreeWindow window(a);window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);QVERIFY(window.selectRoot(b));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_B);
        capture(window,"alpha-compare-directory-options-80");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(window.status().contains("comparison completed"));
        QCOMPARE(window.session().tags,(QSet<QString>{a+"/changed",a+"/unique"}));QCOMPARE(window.session().view,View::Tree);QVERIFY(window.oppositeSession()->tags.isEmpty());
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_B);
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(window.status().contains("comparison completed"));
        QCOMPARE(window.session().tags,(QSet<QString>{a+"/changed",a+"/unique",a+"/sub/child"}));
        QVERIFY(QFile::remove(b+"/same"));QVERIFY(QFile::link(a+"/same",b+"/same"));const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_B);
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(window.status().contains("comparison failed"));QCOMPARE(window.session().tags,tags);
    }
    void branchTagsAndCompareFiltersNarrowing()
    {
        const auto base=root+"/filters";QVERIFY(QDir().mkpath(base+"/sub"));QVERIFY(QDir().mkpath(base+"/other"));
        write(base+"/a.txt","SAME");write(base+"/sub/a.txt","SAME");write(base+"/sub/b.txt","DIFF");write(base+"/other/unique.php","U");
        TreeWindow window(base);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_End);QCOMPARE(window.session().directory,base+"/sub");
        QTest::keyClick(&window,Qt::Key_F7,Qt::ControlModifier);QCOMPARE(window.session().tags,(QSet<QString>{base+"/sub/a.txt",base+"/sub/b.txt"}));
        QTest::keyClick(&window,Qt::Key_F9,Qt::ControlModifier);QVERIFY(window.session().tags.isEmpty());
        QTest::keyClick(&window,Qt::Key_F7,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F8,Qt::ControlModifier);QVERIFY(window.session().tags.isEmpty());QVERIFY(!window.isSplit());
        QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_B);
        QTest::keyClick(&window,Qt::Key_F4,Qt::AltModifier);capture(window,"alpha-compare-filter-menu");QTest::keyClick(&window,Qt::Key_D);
        QTRY_VERIFY(!window.busy());QCOMPARE(window.session().files().size(),2);QVERIFY(window.session().hasCompareFilter());
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QCOMPARE(window.session().tags,(QSet<QString>{base+"/a.txt",base+"/sub/a.txt"}));
        QTest::keyClick(&window,Qt::Key_F4,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_U);QTRY_VERIFY(!window.busy());QVERIFY(window.session().files().isEmpty());
        QTest::keyClick(&window,Qt::Key_F4,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_A);QCOMPARE(window.session().files().size(),4);
        QTest::keyClick(&window,Qt::Key_F4,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_C);QTRY_VERIFY(!window.busy());QCOMPARE(window.session().files().size(),2);
        QTest::keyClick(&window,Qt::Key_Return);QVERIFY(!window.session().hasCompareFilter());QCOMPARE(window.session().view,View::Tree);
        QTest::keyClick(&window,Qt::Key_B,Qt::ControlModifier);QCOMPARE(window.session().files().size(),2);
    }
    void compareFiltersDatesEmptyAndBinaryErrors()
    {
        QVector<FileEntry> files{{"/a/same","same",0,QDateTime::fromMSecsSinceEpoch(1000)},
            {"/b/same","same",0,QDateTime::fromMSecsSinceEpoch(2000)},
            {"/c/same","same",0,QDateTime::fromMSecsSinceEpoch(2000)},
            {"/d/unique","unique",2,QDateTime::fromMSecsSinceEpoch(2000)}};
        auto cancel=std::make_shared<std::atomic_bool>(false);
        QCOMPARE(filterDuplicates(files,CompareFilter::IdenticalDates,true,true,cancel).matches,(QSet<QString>{"/b/same","/c/same"}));
        QCOMPARE(filterDuplicates(files,CompareFilter::Newest,true,true,cancel).matches,(QSet<QString>{"/b/same","/c/same"}));
        QCOMPARE(filterDuplicates(files,CompareFilter::Oldest,true,true,cancel).matches,QSet<QString>{"/a/same"});
        QCOMPARE(filterDuplicates(files,CompareFilter::Size,true,true,cancel).matches.size(),3);
        QVERIFY(filterDuplicates(files,CompareFilter::Size,true,false,cancel).matches.isEmpty());
        QVERIFY(!filterDuplicates(files,CompareFilter::Content,true,true,cancel).errors.isEmpty());
        cancel->store(true);QVERIFY(filterDuplicates(files,CompareFilter::Duplicate,true,true,cancel).cancelled);
    }
    void ctrlCopyMoveFlatBranchAndF4Menu()
    {
        const QString source=root+"/flat-source",destination=root+"/flat-target";
        QVERIFY(QDir().mkpath(source+"/sub"));QVERIFY(QDir().mkdir(destination));write(source+"/a.txt","A");write(source+"/sub/b.txt","B");
        TreeWindow window(source);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_C,Qt::ControlModifier);capture(window,"ctrl-copy-flat-mask");
        QTest::keyClicks(&window,"*.bak");QTest::keyClick(&window,Qt::Key_Return);QTest::keyClicks(&window,destination);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_V);QTRY_VERIFY(window.status().contains("Copy completed"));QVERIFY(window.status().contains("2/2"));
        QVERIFY(QFileInfo::exists(destination+"/a.bak"));QVERIFY(QFileInfo::exists(destination+"/b.bak"));QVERIFY(!QFileInfo::exists(destination+"/sub"));QCOMPARE(window.session().tags,tags);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_M);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_V);
        QTRY_VERIFY(window.status().contains("Move completed"));QVERIFY(window.status().contains("2/2"));
        QVERIFY(QFileInfo::exists(destination+"/a.txt"));QVERIFY(QFileInfo::exists(destination+"/b.txt"));QVERIFY(window.session().tags.isEmpty());QVERIFY(window.session().files().isEmpty());
        for(const auto &path:tags)QVERIFY(!QFileInfo::exists(path));QVERIFY(QFileInfo::exists(source+"/sub"));
        capture(window,"ctrl-move-flat-completed");
    }
    void ctrlMoveFlatCollisionsAndShowall()
    {
        const QString source=root+"/flat-collision",destination=root+"/flat-collision-target";
        QVERIFY(QDir().mkpath(source+"/sub"));QVERIFY(QDir().mkdir(destination));write(source+"/same.txt","A");write(source+"/sub/same.txt","B");
        TreeWindow window(source);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_S);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_M,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClicks(&window,destination);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Y);QVERIFY(window.status().contains("multiple files"));QVERIFY(!window.busy());QCOMPARE(window.session().tags,tags);
        for(const auto &path:tags)QVERIFY(QFileInfo::exists(path));QVERIFY(QDir(destination).entryList(QDir::Files|QDir::Hidden).isEmpty());
        QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_U);QCOMPARE(window.session().tags.size(),1);
        QTest::keyClick(&window,Qt::Key_M,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClicks(&window,destination);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_V);QTRY_VERIFY(window.status().contains("Move completed"));QVERIFY(window.status().contains("1/1"));
        QVERIFY(QFileInfo::exists(destination+"/same.txt"));QVERIFY(!QFileInfo::exists(destination+source));QCOMPARE(window.session().view,View::Showall);
    }
    void altMoveBranchMaskFilterAndSplitDestination()
    {
        const QString source=root+"/move-ui-source",destination=root+"/move-ui-target";
        QVERIFY(QDir().mkpath(source+"/sub"));QVERIFY(QDir().mkdir(source+"/empty"));QVERIFY(QDir().mkdir(destination));
        write(source+"/a.php","A");write(source+"/b.txt","B");write(source+"/sub/a.php","SUB A");
        TreeWindow window(source);window.resize(1280,768);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.php");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);QVERIFY(window.selectRoot(destination));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_M,Qt::AltModifier);capture(window,"alt-move-mask-split");
        QTest::keyClicks(&window,"*.bak");QTest::keyClick(&window,Qt::Key_Return);capture(window,"alt-move-destination-split");
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_R);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_V);
        QTRY_VERIFY(window.status().contains("Move completed"));QVERIFY2(window.status().contains("2/2"),qPrintable(window.status()));
        QCOMPARE(window.activeSide(),0);QVERIFY(window.isSplit());QCOMPARE(window.session().view,View::Branch);QCOMPARE(window.session().directory,source);
        QCOMPARE(window.session().tags,QSet<QString>{source+"/b.txt"});QVERIFY(window.session().files().isEmpty());
        QVERIFY(!QFileInfo::exists(source+"/a.php"));QVERIFY(!QFileInfo::exists(source+"/sub/a.php"));QVERIFY(QFileInfo::exists(source+"/b.txt"));
        QVERIFY(QFileInfo::exists(destination+"/a.bak"));QVERIFY(QFileInfo::exists(destination+"/sub/a.bak"));
        QVERIFY(QFileInfo::exists(source+"/sub"));QVERIFY(!QFileInfo::exists(destination+"/empty"));
        QVERIFY(window.oppositeSession()->directories().contains(destination+"/sub"));QCOMPARE(window.oppositeSession()->currentDirectory().files.size(),1);
        capture(window,"alt-move-completed-split");QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Tree);
    }
    void altMoveConfirmationSkipErrorCancelAndMenu()
    {
        const QString source=root+"/move-confirm",destination=root+"/move-confirm-target";
        QVERIFY(QDir().mkdir(source));QVERIFY(QDir().mkdir(destination));write(source+"/a.txt","A");write(source+"/b.txt","B");
        QVERIFY(QFile::link(source+"/a.txt",source+"/c-link"));
        TreeWindow window(source);window.show();QTRY_VERIFY(!window.busy());QTest::qWait(250);window.resize(640,400);
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_M);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClicks(&window,destination);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_V);capture(window,"alt-move-confirm-80");
        QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());
        QVERIFY(!QFileInfo::exists(source+"/b.txt"));QVERIFY(QFileInfo::exists(destination+"/move-confirm/b.txt"));
        QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());capture(window,"alt-move-error-80");
        QVERIFY(QFileInfo(source+"/c-link").isSymLink());QTest::keyClick(&window,Qt::Key_Escape);
        QVERIFY(window.status().contains("Move cancelled: 1/3"));QVERIFY(window.status().contains("first error"));
        QCOMPARE(window.session().tags,(QSet<QString>{source+"/a.txt",source+"/c-link"}));QVERIFY(QFileInfo::exists(source+"/a.txt"));
        QVERIFY(QDir(source).entryList({".ltree-move-*"},QDir::Files|QDir::Hidden).isEmpty());
    }
    void moveCurrentAllViewsAndSameRootPanels()
    {
        for(const auto view:{View::Directory,View::Branch,View::Showall}){
            const QString source=root+QString("/move-single-%1").arg(int(view)),destination=source+"/target";
            QVERIFY(QDir().mkpath(source+"/child"));QVERIFY(QDir().mkdir(destination));write(source+"/a.txt","A");write(source+"/b.txt","B");write(source+"/child/nested.txt","N");
            TreeWindow window(source);window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
            QTest::keyClick(&window,view==View::Directory?Qt::Key_Return:view==View::Branch?Qt::Key_B:Qt::Key_S);
            for(int i=0;i<(view==View::Directory?1:2);++i)QTest::keyClick(&window,Qt::Key_Down);
            const QString selected=window.session().currentFile()->path;
            QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);const auto beforeTags=window.session().tags;
            QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Home);
            for(int i=0;i<window.session().tree().size() && window.session().directory!=destination;++i)QTest::keyClick(&window,Qt::Key_Down);
            QCOMPARE(window.session().directory,destination);
            QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_M);capture(window,"move-current-as");
            QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(window.status().contains("Move completed"));
            QVERIFY(window.status().contains("1/1"));QVERIFY(!QFileInfo::exists(selected));QVERIFY(QFileInfo::exists(destination+'/'+QFileInfo(selected).fileName()));
            auto expected=beforeTags;expected.remove(selected);QCOMPARE(window.session().tags,expected);QCOMPARE(window.session().view,view);QCOMPARE(window.session().directory,source);
            QVERIFY(!window.oppositeSession()->tags.contains(selected));QVERIFY(window.oppositeSession()->currentDirectory().loaded);
            QCOMPARE(window.oppositeSession()->currentDirectory().files.size(),1);QCOMPARE(window.activeSide(),0);
        }
    }
    void altMoveQueuedCancellationAndReplacementSkip()
    {
        const QString source=root+"/move-cancel",destination=root+"/move-cancel-target";QVERIFY(QDir().mkdir(source));QVERIFY(QDir().mkdir(destination));
        write(source+"/a.txt","NEW A");write(source+"/b.txt","NEW B");write(destination+"/a.txt","KEEP");
        TreeWindow window(source);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto tags=window.session().tags;
        const auto prompt=[&]{QTest::keyClick(&window,Qt::Key_M,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClicks(&window,destination);
            QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_R);QTest::keyClick(&window,Qt::Key_Return);};
        prompt();auto *pool=QThreadPool::globalInstance();const int threads=pool->maxThreadCount();pool->setMaxThreadCount(1);
        QSemaphore release;auto blocker=QtConcurrent::run(pool,[&]{release.acquire();});const auto cleanup=qScopeGuard([&]{release.release();blocker.waitForFinished();pool->setMaxThreadCount(threads);});
        QTest::keyClick(&window,Qt::Key_V);QVERIFY(window.busy());QTest::keyClick(&window,Qt::Key_Escape);release.release();blocker.waitForFinished();QTRY_VERIFY(!window.busy());
        QVERIFY(window.status().contains("Move cancelled"));QCOMPARE(window.session().tags,tags);QVERIFY(QFileInfo::exists(source+"/b.txt"));QVERIFY(!QFileInfo::exists(destination+"/b.txt"));
        prompt();QTest::keyClick(&window,Qt::Key_V);QTRY_VERIFY(window.status().contains("Move completed"));
        QVERIFY(window.status().contains("1/2"));QVERIFY(window.status().contains("1 skipped"));QCOMPARE(window.session().tags,QSet<QString>{source+"/a.txt"});
        QVERIFY(QFileInfo::exists(source+"/a.txt"));QVERIFY(!QFileInfo::exists(source+"/b.txt"));QVERIFY(QFileInfo::exists(destination+"/b.txt"));
        QCOMPARE(window.session().currentFile()->path,source+"/a.txt");capture(window,"alt-move-replacement-skipped");
    }
    void moveEngineSameFilesystemMetadataAndInode()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        const QString source=panel+"/ordenes.php",target=root+"/moved/deep/renamed.php",peer=root+"/hard-peer";
        QVERIFY(::chmod(QFile::encodeName(source).constData(),0640)==0);
        QVERIFY(::link(QFile::encodeName(source).constData(),QFile::encodeName(peer).constData())==0);
        struct stat before{},after{};QVERIFY(::stat(QFile::encodeName(source).constData(),&before)==0);
        const auto result=moveFile(root,{source,target},CopyReplace::Ask,cancel);
        QVERIFY2(result.moved,qPrintable(result.error));QVERIFY(!result.copied);QVERIFY(result.error.isEmpty());
        QVERIFY(!QFileInfo::exists(source));QVERIFY(QFileInfo::exists(peer));QVERIFY(::stat(QFile::encodeName(target).constData(),&after)==0);
        QCOMPARE(after.st_ino,before.st_ino);QCOMPARE(after.st_dev,before.st_dev);QCOMPARE(after.st_mode,before.st_mode);
        QCOMPARE(after.st_uid,before.st_uid);QCOMPARE(after.st_gid,before.st_gid);QCOMPARE(after.st_mtim.tv_sec,before.st_mtim.tv_sec);
        bool sawSource=false,sawTarget=false;
        for(const auto &directory:result.scan.directories){
            if(directory.path==panel){sawSource=true;for(const auto &file:directory.files)QVERIFY(file.path!=source);}
            if(directory.path==QFileInfo(target).absolutePath()){sawTarget=true;QCOMPARE(directory.files.first().path,target);}
        }
        QVERIFY(sawSource);QVERIFY(sawTarget);
        const QString unreadable=panel+"/unreadable";write(unreadable,"preserved");QVERIFY(::chmod(QFile::encodeName(unreadable).constData(),0)==0);
        const auto noRead=moveFile(root,{unreadable,root+"/moved/no-read"},CopyReplace::Ask,cancel);
        QVERIFY2(noRead.moved,qPrintable(noRead.error));QVERIFY(::stat(QFile::encodeName(noRead.target).constData(),&after)==0);QCOMPARE(after.st_mode&0777,mode_t(0));
        QVERIFY(QDir(panel).entryList({".ltree-move-*"},QDir::Files|QDir::Hidden).isEmpty());
    }
    void moveEngineReplacementPoliciesKeepSkippedSources()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);const QString source=panel+"/ordenes.php",target=root+"/move-target.php";
        write(source,"NEW");write(target,"OLD");
        for(const auto policy:{CopyReplace::Ask,CopyReplace::Never,CopyReplace::Older}){
            const auto result=moveFile(root,{source,target},policy,cancel);QVERIFY(!result.moved);
            QVERIFY(policy==CopyReplace::Ask?result.exists:result.skipped);QVERIFY(QFileInfo::exists(source));
            QFile file(target);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("OLD"));
        }
        const auto sequence=moveFile(root,{source,target},CopyReplace::Rename,cancel);
        QVERIFY2(sequence.moved,qPrintable(sequence.error));QCOMPARE(sequence.target,root+"/move-target(2).php");QVERIFY(!QFileInfo::exists(source));
        write(source,"LATEST");
        {QFile file(target);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.setFileTime(QDateTime::fromSecsSinceEpoch(1500000000),QFileDevice::FileModificationTime));}
        QVERIFY(moveFile(root,{source,target},CopyReplace::Older,cancel).moved);
        write(source,"ALWAYS");QVERIFY(moveFile(root,{source,target},CopyReplace::Always,cancel).moved);
        QFile file(target);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("ALWAYS"));
    }
    void moveEngineRefusesLinksSelfErrorsAndCancellation()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);const QString source=panel+"/ordenes.php",target=root+"/move-protected.php";
        write(target,"KEEP");QVERIFY(QFile::link(source,root+"/move-link"));QVERIFY(QFile::link(target,root+"/move-target-link"));
        QVERIFY(QFile::link(panel,root+"/move-dir-link"));
        QVERIFY(::link(QFile::encodeName(source).constData(),QFile::encodeName(root+"/move-hard-link").constData())==0);
        QVERIFY(::mkfifo(QFile::encodeName(root+"/move-fifo").constData(),0600)==0);
        const QVector<CopyEntry> invalid{{root+"/move-link",target},{source,root+"/move-target-link"},{source,root+"/move-dir-link/new"},
            {source,source},{source,root+"/move-hard-link"},{root+"/move-fifo",target},{source+"/../ordenes.php",target},{root+"/missing",target}};
        for(const auto &entry:invalid){const auto result=moveFile(root,entry,CopyReplace::Always,cancel);QVERIFY(!result.moved);QVERIFY(!result.error.isEmpty());}
        QVERIFY(QFileInfo::exists(source));{QFile file(target);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("KEEP"));}
        QVERIFY(QDir().mkdir(root+"/move-locked"));QVERIFY(::chmod(QFile::encodeName(root+"/move-locked").constData(),0500)==0);
        const auto denied=moveFile(root,{source,root+"/move-locked/new"},CopyReplace::Ask,cancel);
        QVERIFY(::chmod(QFile::encodeName(root+"/move-locked").constData(),0700)==0);QVERIFY(!denied.error.isEmpty());QVERIFY(QFileInfo::exists(source));
        cancel->store(true);const auto stopped=moveFile(root,{source,root+"/not-created/file"},CopyReplace::Always,cancel);
        QVERIFY(stopped.cancelled);QVERIFY(!stopped.moved);QVERIFY(!QFileInfo::exists(root+"/not-created"));QVERIFY(QFileInfo::exists(source));
        QVERIFY(QDir(panel).entryList({".ltree-move-*"},QDir::Files|QDir::Hidden).isEmpty());
    }
    void moveEngineCrossFilesystemCopyAndReadFailure()
    {
        QTemporaryDir destination("/dev/shm/ltree-move-XXXXXX");if(!destination.isValid())QSKIP("Writable /dev/shm is unavailable");
        struct stat local{},remote{};QVERIFY(::stat(QFile::encodeName(root).constData(),&local)==0);QVERIFY(::stat(QFile::encodeName(destination.path()).constData(),&remote)==0);
        if(local.st_dev==remote.st_dev)QSKIP("A second filesystem is unavailable");
        const auto cancel=std::make_shared<std::atomic_bool>(false);const QString source=panel+"/ordenes.php",target=destination.path()+"/deep/moved.bin";
        const QByteArray data=QByteArray::fromHex("00010203fffe")+QByteArray(400000,'x');write(source,data);
        QVERIFY(::chmod(QFile::encodeName(source).constData(),0640)==0);
        {QFile file(source);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.setFileTime(QDateTime::fromSecsSinceEpoch(1600000000),QFileDevice::FileModificationTime));}
        const auto expected=QFileInfo(source).lastModified();const auto result=moveFile(root,{source,target},CopyReplace::Ask,cancel);
        QVERIFY2(result.moved,qPrintable(result.error));QVERIFY(result.copied);QVERIFY(!QFileInfo::exists(source));
        {QFile file(target);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),data);}
        QCOMPARE(QFileInfo(target).lastModified(),expected);QVERIFY(::stat(QFile::encodeName(target).constData(),&remote)==0);QCOMPARE(remote.st_mode&0777,mode_t(0640));
        write(source,"NO READ");QVERIFY(::chmod(QFile::encodeName(source).constData(),0)==0);
        const auto denied=moveFile(root,{source,destination.path()+"/denied"},CopyReplace::Ask,cancel);
        QVERIFY(!denied.moved);QVERIFY(!denied.error.isEmpty());QVERIFY(QFileInfo::exists(source));QVERIFY(!QFileInfo::exists(destination.path()+"/denied"));
        QVERIFY(::chmod(QFile::encodeName(source).constData(),0600)==0);
        QVERIFY(QDir(panel).entryList({".ltree-move-*"},QDir::Files|QDir::Hidden).isEmpty());
        QVERIFY(QDir(destination.path()+"/deep").entryList({".ltree-copy-*"},QDir::Files|QDir::Hidden).isEmpty());
    }
    void systemStatisticsParsingAndFilesystem()
    {
        const auto data=parseProcStatistics("MemTotal: 8192 kB\nMemAvailable: 4096 kB\nCommitted_AS: 12000 kB\nCommitLimit: 16000 kB\n",
            "model name : Test CPU\ncpu MHz : 1234.5\n\nmodel name : Second CPU\ncpu MHz : 900\n");
        QCOMPARE(data.ramTotal,qint64(8388608));QCOMPARE(data.ramAvailable,qint64(4194304));QCOMPARE(data.committed,qint64(12288000));
        QCOMPARE(data.commitLimit,qint64(16384000));QCOMPARE(data.cpuType,QString("Test CPU"));QCOMPARE(data.cpuMHz,1234.5);
        const auto invalid=parseProcStatistics("MemTotal: -1 kB\nMemAvailable: 99999999999999999999 kB\nCommitted_AS: abc kB\nCommitLimit: 2 MB\n","cpu MHz: inf\n");
        QCOMPARE(invalid.ramTotal,qint64(-1));QCOMPARE(invalid.ramAvailable,qint64(-1));QCOMPARE(invalid.committed,qint64(-1));QCOMPARE(invalid.commitLimit,qint64(-1));QCOMPARE(invalid.cpuMHz,-1.0);
        QCOMPARE(parseProcStatistics({},"Hardware: ARM Test\n").cpuType,QString("ARM Test"));
        const auto live=systemStatistics(root);const QStorageInfo expected(root);
        QVERIFY2(live.error.isEmpty(),qPrintable(live.error));QCOMPARE(live.capacity,expected.bytesTotal());QCOMPARE(live.mount,expected.rootPath());
        QCOMPARE(live.filesystem,QString::fromLatin1(expected.fileSystemType()));QVERIFY(live.available>=0);QVERIFY(live.free>=live.available);
        QVERIFY(live.blockSize>0);QVERIFY(live.totalBlocks>0);QVERIFY(live.ramTotal>0);QVERIFY(live.ramAvailable>=0);
        QVERIFY(!live.host.isEmpty());QVERIFY(!live.user.isEmpty());QVERIFY(live.measured.isValid());
        const auto missing=systemStatistics(root+"/does-not-exist");QVERIFY(!missing.error.isEmpty());QCOMPARE(missing.capacity,qint64(-1));
    }
    void loggedStatisticsScopesFiltersTagsAndMounts()
    {
        const QString base="/virtual",branch=base+"/branch",deep=branch+"/deep",other=base+"/other";
        const auto file=[](const QString &path,qint64 size){FileEntry entry;entry.path=path;entry.name=QFileInfo(path).fileName();entry.size=size;return entry;};
        Session session(base);ScanResult snapshot;
        snapshot.directories={{base,{branch,other},{file(base+"/root.bin",10)},{}},
            {branch,{deep},{file(branch+"/a.php",20),file(branch+"/data.bin",30)},{}},
            {deep,{},{file(deep+"/a.php",40)},{}},{other,{},{file(other+"/a.php",50)},{}}};
        session.apply(snapshot,true);auto stats=loggedStatistics(session);QCOMPARE(stats.totalFiles,quint64(5));QCOMPARE(stats.totalBytes,quint64(150));
        QCOMPARE(stats.directories,quint64(4));QCOMPARE(stats.displayedFiles,quint64(1));QCOMPARE(stats.displayedBytes,quint64(10));
        QVERIFY(session.filespec.set("*.php"));session.tags={branch+"/a.php",other+"/a.php"};session.rebuild();stats=loggedStatistics(session);
        QCOMPARE(stats.matchingFiles,quint64(3));QCOMPARE(stats.matchingBytes,quint64(110));QCOMPARE(stats.taggedFiles,quint64(2));QCOMPARE(stats.taggedBytes,quint64(70));QCOMPARE(stats.displayedFiles,quint64(0));
        session.selectDirectory(branch);session.enter(View::Branch);stats=loggedStatistics(session);
        QCOMPARE(stats.totalFiles,quint64(3));QCOMPARE(stats.totalBytes,quint64(90));QCOMPARE(stats.directories,quint64(2));QCOMPARE(stats.displayedFiles,quint64(2));
        session.toggleTagsOnly();stats=loggedStatistics(session);QCOMPARE(stats.displayedFiles,quint64(1));QCOMPARE(stats.displayedBytes,quint64(20));QCOMPARE(stats.totalFiles,quint64(3));
        session.tags.remove(branch+"/a.php");stats=loggedStatistics(session);QCOMPARE(stats.taggedFiles,quint64(0));QCOMPARE(stats.displayedFiles,quint64(1));
        session.returnToTree();session.toggleCollapse(false);stats=loggedStatistics(session);QCOMPARE(stats.totalBytes,quint64(150));
        const QVector<MountPoint> mounts{{base,"test","main",false},{deep,"test","nested",false}};
        stats=loggedStatistics(session,mounts,base);QCOMPARE(stats.totalFiles,quint64(4));QCOMPARE(stats.totalBytes,quint64(110));QCOMPARE(stats.directories,quint64(3));
        stats=loggedStatistics(session,mounts,deep);QCOMPARE(stats.totalFiles,quint64(1));QCOMPARE(stats.totalBytes,quint64(40));QCOMPARE(stats.directories,quint64(1));
        QCOMPARE(loggedStatistics(session,mounts,"/elsewhere").totalFiles,quint64(0));
        session.selectDirectory(branch);session.enter(View::Directory);QCOMPARE(loggedStatistics(session).totalFiles,quint64(2));
        session.enter(View::Showall);QCOMPARE(loggedStatistics(session).totalFiles,quint64(5));
        Session empty(base);QCOMPARE(loggedStatistics(empty).directories,quint64(0));
        snapshot.directories={{base,{},{file(base+"/huge1",std::numeric_limits<qint64>::max()),file(base+"/huge2",std::numeric_limits<qint64>::max()),file(base+"/huge3",std::numeric_limits<qint64>::max())},{}}};
        empty.apply(snapshot,true);QCOMPARE(loggedStatistics(empty).totalBytes,std::numeric_limits<quint64>::max());
    }
    void extendedStatisticsAllViewsAndSplitState()
    {
        TreeWindow window(root);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F8);
        for(const auto view:{View::Tree,View::Directory,View::Branch,View::Showall}){
            if(view!=View::Tree)QTest::keyClick(&window,view==View::Directory?Qt::Key_Return:view==View::Branch?Qt::Key_B:Qt::Key_S);
            const auto tags=window.session().tags;const auto index=window.session().fileIndex;const auto directory=window.session().directory;
            const auto loaded=window.session().directories().size();const auto stats=loggedStatistics(window.session());
            QKeyEvent question(QEvent::KeyPress,Qt::Key_Question,Qt::ShiftModifier,"?");QCoreApplication::sendEvent(&window,&question);
            QVERIFY(window.statisticsVisible());QCOMPARE(window.statisticsLogged().totalFiles,stats.totalFiles);QCOMPARE(window.statisticsLogged().totalBytes,stats.totalBytes);
            capture(window,"extended-statistics-split");const auto time=window.statisticsSystem().measured;
            if(view==View::Tree)write(root+"/late-stats.txt","new content\n");
            QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Down);
            QCOMPARE(window.activeSide(),0);QVERIFY(window.isSplit());QCOMPARE(window.session().directory,directory);QCOMPARE(window.session().fileIndex,index);QCOMPARE(window.session().tags,tags);
            QTest::qWait(5);QTest::keyClick(&window,Qt::Key_F3);QVERIFY(window.statisticsSystem().measured>time);QCOMPARE(window.session().directories().size(),loaded);
            QCOMPARE(window.statisticsLogged().totalFiles,stats.totalFiles);
            QTest::keyClick(&window,Qt::Key_Greater);QVERIFY(window.statisticsVisible());QCOMPARE(window.session().directory,directory);
            QTest::keyClick(&window,Qt::Key_Return);QVERIFY(!window.statisticsVisible());QCOMPARE(window.session().view,view);QCOMPARE(window.session().tags,tags);QCOMPARE(window.session().fileIndex,index);
            if(view!=View::Tree)QTest::keyClick(&window,Qt::Key_Return);
            else{QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());}
        }
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_Slash);QVERIFY(window.statisticsVisible());QCOMPARE(window.activeSide(),1);
        QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(!window.statisticsVisible());QCOMPARE(window.activeSide(),1);
    }
    void extendedStatisticsCompactHelpAutoAndPreview()
    {
        TreeWindow window(panel);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QTest::qWait(250);window.resize(640,400);QTest::qWait(100);QTRY_COMPARE(window.columns(),80);
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_F7);auto *preview=window.findChild<FileViewer*>();QVERIFY(preview);QTRY_VERIFY(!preview->loading());
        const auto selected=window.session().currentFile()->path;const auto view=window.session().view;
        QTest::keyClick(&window,Qt::Key_Question);QVERIFY(window.statisticsVisible());QVERIFY(!preview->isVisible());capture(window,"extended-statistics-80-top");
        QTest::keyClick(&window,Qt::Key_PageDown);capture(window,"extended-statistics-80-bottom");QTest::keyClick(&window,Qt::Key_End);QTest::keyClick(&window,Qt::Key_Home);
        const auto time=window.statisticsSystem().measured;QTest::keyClick(&window,Qt::Key_F3,Qt::ControlModifier);QVERIFY(window.statisticsAutoRefresh());
        QTRY_VERIFY(window.statisticsSystem().measured.msecsTo(QDateTime::currentDateTime())<800 && window.statisticsSystem().measured.msecsTo(time)<-900);
        QTest::keyClick(&window,Qt::Key_F3,Qt::ControlModifier);QVERIFY(!window.statisticsAutoRefresh());
        QTest::keyClick(&window,Qt::Key_F1);capture(window,"extended-statistics-help-80");QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(window.statisticsVisible());
        QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(!window.statisticsVisible());QVERIFY(preview->isVisible());QCOMPARE(window.session().view,view);QCOMPARE(window.session().currentFile()->path,selected);
        QTest::keyClick(&window,Qt::Key_F7);QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint(79*8-4,5*16));QVERIFY(window.statisticsVisible());
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint(10*8,5*16));QVERIFY(!window.statisticsVisible());
        QTest::keyClick(&window,Qt::Key_F,Qt::NoModifier);QTest::keyClicks(&window,"?");QVERIFY(!window.statisticsVisible());QTest::keyClick(&window,Qt::Key_Escape);
    }
    void filespecHistoryNavigationPersistenceAndSplit()
    {
        const auto selected=[](TreeWindow &window){QGuiApplication::clipboard()->setText("NO_HISTORY");QTest::keyClick(&window,Qt::Key_Insert,Qt::ControlModifier);const auto text=QGuiApplication::clipboard()->text();return text=="NO_HISTORY"?QString():text;};
        {
            TreeWindow window(panel);window.resize(1280,800);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
            QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
            for(const auto &mask:QStringList{"*.php","*.txt","*.php"}){QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,mask);QTest::keyClick(&window,Qt::Key_Return);}
            QTest::keyClick(&window,Qt::Key_F8);const auto tags=window.session().tags;const auto index=window.session().fileIndex;
            const auto otherSpec=window.oppositeSession()->filespec.text();
            QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"draft");QTest::keyClick(&window,Qt::Key_Up);
            QCOMPARE(selected(window),QString("*.php"));QCOMPARE(window.session().fileIndex,index);capture(window,"filespec-history-newest-split");
            QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(selected(window),QString("*.txt"));
            QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_Return);
            QCOMPARE(window.session().filespec.text(),QString("draft"));
            QTest::keyClick(&window,Qt::Key_F);QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(selected(window),QString("*.txt"));
            QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().filespec.text(),QString("draft"));
            QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().filespec.text(),QString("*.txt"));
            QCOMPARE(window.session().tags,tags);QCOMPARE(window.oppositeSession()->filespec.text(),otherSpec);

            QTest::keyClick(&window,Qt::Key_F);QTest::keyClick(&window,Qt::Key_Up);QTest::keyClick(&window,Qt::Key_1,Qt::ControlModifier);
            QTest::keyClick(&window,Qt::Key_F3);QVERIFY(window.status().contains("Filespec history saved"));
        }
        QFile file(root+"/filespec-history.json");QVERIFY(file.open(QIODevice::ReadOnly));
        const auto entries=QJsonDocument::fromJson(file.readAll()).object().value("filespec").toArray();file.close();
        QCOMPARE(entries.size(),3);QVERIFY(!(file.permissions()&QFile::ReadOther));QCOMPARE(entries.first().toObject().value("mark").toString(),QString("1"));
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(selected(window),QString("*.txt"));
        QTest::keyClick(&window,Qt::Key_1);QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().filespec.text(),QString("*.txt"));
        QCOMPARE(window.session().view,View::Tree);
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().filespec.text(),QString("*.txt"));
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClick(&window,Qt::Key_Up);
        QTest::mouseDClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint((window.columns()/6+4)*8,5*16+8));
        QCOMPARE(window.session().filespec.text(),QString("*.txt"));
        QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().filespec.text(),QString("*.php"));
        QTest::keyClick(&window,Qt::Key_F);
        QWheelEvent wheel(QPointF(40,40),QPointF(window.mapToGlobal(QPoint(40,40))),{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&window,&wheel);QCOMPARE(selected(window),QString("*.php"));
    }
    void filespecHistoryPagingAppendBookmarksAndReload()
    {
        TreeWindow window(panel);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());window.resize(640,400);
        const auto selected=[&]{QGuiApplication::clipboard()->setText("NO_HISTORY");QTest::keyClick(&window,Qt::Key_Insert,Qt::ControlModifier);const auto text=QGuiApplication::clipboard()->text();return text=="NO_HISTORY"?QString():text;};
        QTest::keyClick(&window,Qt::Key_F);
        for(int i=0;i<70;++i){QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);QTest::keyClicks(&window,QString("mask_%1.*").arg(i,2,10,QChar('0')));QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);}
        QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(selected(),QString("mask_69.*"));QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(selected(),QString("mask_06.*"));
        QTest::keyClick(&window,Qt::Key_Insert);QTest::keyClick(&window,Qt::Key_Delete);QVERIFY(window.status().contains("Unmark"));
        QTest::keyClick(&window,Qt::Key_Right);QCOMPARE(selected(),QString("mask_06.*"));QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_F3);capture(window,"filespec-history-marks-80");
        QTest::keyClick(&window,Qt::Key_Insert);QTest::keyClick(&window,Qt::Key_Delete);QCOMPARE(selected(),QString());
        QTest::keyClick(&window,Qt::Key_F4);QCOMPARE(selected(),QString("mask_06.*"));
        write(root+"/filespec-history.json","invalid");QTest::keyClick(&window,Qt::Key_F4);QVERIFY(window.status().contains("Invalid filespec"));QCOMPARE(selected(),QString("mask_06.*"));
        QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_A,Qt::AltModifier);
        QCOMPARE(window.session().filespec.text(),QString("mask_06.*"));
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.php");QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);
        QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);QTest::keyClicks(&window,"*.txt,");QTest::keyClick(&window,Qt::Key_Up);QTest::keyClick(&window,Qt::Key_Plus);QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_Return);
        QCOMPARE(window.session().filespec.text(),QString("*.txt,*.php"));
        QTest::keyClick(&window,Qt::Key_F);QGuiApplication::clipboard()->setText(QString(1024,'a'));QTest::keyClick(&window,Qt::Key_V,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Up);
        QCOMPARE(selected().size(),1024);QTest::keyClick(&window,Qt::Key_F3);
    }
    void historyConfigLocationAndLegacyMigration()
    {
        const auto priorConfig=qgetenv("XDG_CONFIG_HOME"),priorData=qgetenv("XDG_DATA_HOME"),priorOverride=qgetenv("LTREE_HISTORY_FILE");
        const bool hadConfig=qEnvironmentVariableIsSet("XDG_CONFIG_HOME"),hadData=qEnvironmentVariableIsSet("XDG_DATA_HOME");
        const auto restore=qScopeGuard([&]{if(hadConfig)qputenv("XDG_CONFIG_HOME",priorConfig);else qunsetenv("XDG_CONFIG_HOME");if(hadData)qputenv("XDG_DATA_HOME",priorData);else qunsetenv("XDG_DATA_HOME");qputenv("LTREE_HISTORY_FILE",priorOverride);});
        qputenv("XDG_CONFIG_HOME",QFile::encodeName(root+"/config"));qputenv("XDG_DATA_HOME",QFile::encodeName(root+"/data"));qunsetenv("LTREE_HISTORY_FILE");
        const auto oldPath=root+"/data/ltreec/search-history.json",newPath=root+"/config/ltreec/search-history.json";
        QVERIFY(QDir().mkpath(QFileInfo(oldPath).absolutePath()));
        const auto legacy=QJsonDocument(QJsonObject{{"version",1},{"search",QJsonArray{QJsonObject{{"text","legacy query"},{"mark","Z"}}}}}).toJson();write(oldPath,legacy);
        {
            TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());
            QVERIFY(QFileInfo::exists(newPath));QFile old(oldPath);QVERIFY(old.open(QIODevice::ReadOnly));QCOMPARE(old.readAll(),legacy);
            QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Up);
            QTest::keyClick(&window,Qt::Key_Insert,Qt::ControlModifier);QCOMPARE(QGuiApplication::clipboard()->text(),QString("legacy query"));
            QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_Escape);
            QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.php");QTest::keyClick(&window,Qt::Key_Return);
        }
        QVERIFY(QFileInfo::exists(root+"/config/ltreec/filespec-history.json"));
        QFile file(newPath);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("search").toArray().first().toObject().value("mark").toString(),QString("Z"));file.close();
        write(oldPath,"broken legacy");
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_F);QTest::keyClick(&window,Qt::Key_Up);QTest::keyClick(&window,Qt::Key_Insert,Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(),QString("*.php"));QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Up);QTest::keyClick(&window,Qt::Key_Insert,Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(),QString("legacy query"));
    }
    void searchHistoryNavigationRetrievalAndSplit()
    {
        const auto base=root+"/history-flow";QVERIFY(QDir().mkdir(base));
        write(base+"/alpha.txt","alpha\n");write(base+"/beta.txt","beta\n");write(base+"/both.txt","alpha beta\n");
        TreeWindow window(base);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);
        for(const auto &query:QStringList{"alpha","beta","alpha"}){
            QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);
            QTest::keyClicks(&window,query);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("Search completed"));
        }
        QTest::keyClick(&window,Qt::Key_F8);const auto tags=window.session().tags;const auto index=window.session().fileIndex;
        const auto selected=[&]{QGuiApplication::clipboard()->setText("LTREE_NO_HISTORY_SELECTION");QTest::keyClick(&window,Qt::Key_Insert,Qt::ControlModifier);const auto text=QGuiApplication::clipboard()->text();return text=="LTREE_NO_HISTORY_SELECTION"?QString():text;};
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClicks(&window,"draft_");QTest::keyClick(&window,Qt::Key_Up);
        QCOMPARE(selected(),QString("alpha"));capture(window,"search-history-newest-split");
        const auto image=window.grab().toImage();const auto ratio=image.devicePixelRatio();
        QCOMPARE(image.pixelColor(qRound((window.columns()/6+4)*8*ratio),qRound(10*16*ratio)),QColor(0,128,128));
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_F8);QCOMPARE(window.activeSide(),0);QVERIFY(window.isSplit());
        QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(selected(),QString("beta"));QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(selected(),QString("beta"));
        QTest::keyClick(&window,Qt::Key_End);QCOMPARE(selected(),QString("alpha"));QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(selected(),QString("beta"));
        QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(window.session().tags,tags);QCOMPARE(window.session().fileIndex,index);
        QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(selected(),QString("draft_"));
        QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(selected(),QString("beta"));QTest::keyClick(&window,Qt::Key_Return);
        QVERIFY(!window.busy());QCOMPARE(window.session().tags,tags);capture(window,"search-history-retrieved-prompt");
        QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(selected(),QString("beta"));QTest::keyClick(&window,Qt::Key_Return);QVERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().tags,QSet<QString>({base+"/beta.txt",base+"/both.txt"}));QCOMPARE(window.session().fileIndex,index);
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(!window.busy());QCOMPARE(window.session().tags,QSet<QString>({base+"/beta.txt",base+"/both.txt"}));
    }
    void searchHistoryPagingMarksFiltersAndLimits()
    {
        TreeWindow window(panel);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);
        const auto record=[&](int from,int to){for(int i=from;i<to;++i){QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);
            QTest::keyClicks(&window,QString("query_%1").arg(i,2,10,QChar('0')));QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);}};
        const auto selected=[&]{QGuiApplication::clipboard()->setText("LTREE_NO_HISTORY_SELECTION");QTest::keyClick(&window,Qt::Key_Insert,Qt::ControlModifier);const auto text=QGuiApplication::clipboard()->text();return text=="LTREE_NO_HISTORY_SELECTION"?QString():text;};
        record(0,70);QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(selected(),QString("query_69"));
        QTest::keyClick(&window,Qt::Key_PageUp);QCOMPARE(selected(),QString("query_32"));QTest::keyClick(&window,Qt::Key_PageDown);QCOMPARE(selected(),QString("query_69"));
        QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(selected(),QString("query_06"));QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(selected(),QString("query_07"));QTest::keyClick(&window,Qt::Key_Insert);
        QTest::keyClick(&window,Qt::Key_Escape);record(70,140);QTest::keyClick(&window,Qt::Key_Up);QTest::keyClick(&window,Qt::Key_F3);
        QFile file(root+"/search-history.json");QVERIFY(file.open(QIODevice::ReadOnly));auto entries=QJsonDocument::fromJson(file.readAll()).object().value("search").toArray();file.close();
        QCOMPARE(entries.size(),66);QCOMPARE(entries.last().toObject().value("text").toString(),QString("query_06"));
        QTest::qWait(250);window.resize(640,400);QTest::qWait(100);QTRY_COMPARE(window.columns(),80);
        QTest::keyClick(&window,Qt::Key_End);QTest::keyClick(&window,Qt::Key_PageUp);QCOMPARE(selected(),QString("query_125"));capture(window,"search-history-80-scroll");
        QTest::keyClick(&window,Qt::Key_Right);QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(selected(),QString("query_06"));
        QTest::keyClick(&window,Qt::Key_Right);QCOMPARE(selected(),QString("query_06"));QTest::keyClick(&window,Qt::Key_Delete);QVERIFY(window.status().contains("Unmark"));
        QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);QCOMPARE(selected(),QString());capture(window,"search-history-empty-filter");
        QTest::keyClick(&window,Qt::Key_Left);QCOMPARE(selected(),QString("query_07"));QTest::keyClick(&window,Qt::Key_Insert);
        QTest::keyClick(&window,Qt::Key_Left);QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(selected(),QString("query_76"));
        QTest::keyClick(&window,Qt::Key_Delete);QCOMPARE(selected(),QString("query_77"));QTest::keyClick(&window,Qt::Key_F3);
        QVERIFY(file.open(QIODevice::ReadOnly));entries=QJsonDocument::fromJson(file.readAll()).object().value("search").toArray();QCOMPARE(entries.size(),63);
    }
    void searchHistoryAppendBookmarksSortingAndMouse()
    {
        const auto base=root+"/history-edit";QVERIFY(QDir().mkdir(base));write(base+"/match.txt","alphabeta\n");write(base+"/other.txt","other\n");
        TreeWindow window(base);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);
        for(const auto &text:QStringList{"alpha","beta","gamma"}){QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);QTest::keyClicks(&window,text);QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);}
        QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Up);QTest::keyClick(&window,Qt::Key_Home);
        QTest::keyClick(&window,Qt::Key_Z,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Plus);QTest::keyClick(&window,Qt::Key_Down);QTest::keyClick(&window,Qt::Key_Bar);
        QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QCOMPARE(window.session().tags,QSet<QString>{base+"/match.txt"});
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_Z,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QCOMPARE(window.session().tags,QSet<QString>{base+"/match.txt"});
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Down);
        const auto selected=[&]{QGuiApplication::clipboard()->setText("LTREE_NO_HISTORY_SELECTION");QTest::keyClick(&window,Qt::Key_Insert,Qt::ControlModifier);const auto text=QGuiApplication::clipboard()->text();return text=="LTREE_NO_HISTORY_SELECTION"?QString():text;};
        QTest::keyClick(&window,Qt::Key_Right,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Right,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Right,Qt::AltModifier);
        QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(selected(),QString("alpha"));QTest::keyClick(&window,Qt::Key_End);QCOMPARE(selected(),QString("gamma"));
        QTest::keyClick(&window,Qt::Key_Right,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(selected(),QString("gamma"));
        QTest::mouseDClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint((window.columns()/6+4)*8,20*16+8));QCOMPARE(selected(),QString("gamma"));
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint((window.columns()/6+4)*8,6*16+8));QCOMPARE(selected(),QString("beta"));
        capture(window,"search-history-bookmarks-sort");QTest::mouseDClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint((window.columns()/6+4)*8,6*16+8));
        QVERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);QTest::keyClicks(&window,"alpha");QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_Z,Qt::AltModifier);QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("Search completed"));QCOMPARE(window.session().tags,QSet<QString>{base+"/match.txt"});
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QGuiApplication::clipboard()->setText(QString::fromUtf8("búsqueda ñandú"));
        QTest::keyClick(&window,Qt::Key_V,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Up);
        QCOMPARE(selected(),QString::fromUtf8("búsqueda ñandú"));QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);
        QGuiApplication::clipboard()->setText(QString(280,'a'));QTest::keyClick(&window,Qt::Key_V,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);
        QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(selected().size(),255);QTest::keyClick(&window,Qt::Key_Plus);QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(selected().size(),255);
    }
    void searchHistoryPersistenceReloadAndInvalidFile()
    {
        const auto selected=[](TreeWindow &window){QGuiApplication::clipboard()->setText("LTREE_NO_HISTORY_SELECTION");QTest::keyClick(&window,Qt::Key_Insert,Qt::ControlModifier);const auto text=QGuiApplication::clipboard()->text();return text=="LTREE_NO_HISTORY_SELECTION"?QString():text;};
        {
            TreeWindow window(panel);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
            QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);
            QTest::keyClicks(&window,"saved query");QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Up);QTest::keyClick(&window,Qt::Key_1,Qt::ControlModifier);
        }
        QFile file(root+"/search-history.json");QVERIFY(file.exists());QVERIFY(!(file.permissions()&QFile::ReadOther));
        TreeWindow window(panel);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Up);
        QCOMPARE(selected(window),QString("saved query"));QTest::keyClick(&window,Qt::Key_Insert);QTest::keyClick(&window,Qt::Key_Delete);
        QCOMPARE(selected(window),QString());QTest::keyClick(&window,Qt::Key_F4);QCOMPARE(selected(window),QString("saved query"));
        write(file.fileName(),"broken JSON");QTest::keyClick(&window,Qt::Key_F4);QVERIFY(window.status().contains("Invalid"));QCOMPARE(selected(window),QString("saved query"));
        QTest::keyClick(&window,Qt::Key_F3);QVERIFY(window.status().contains("saved"));
        QVERIFY(QFile::rename(file.fileName(),file.fileName()+".held"));QVERIFY(QDir().mkdir(file.fileName()));
        QTest::keyClick(&window,Qt::Key_F3);QVERIFY(window.status().contains("Cannot save"));QCOMPARE(selected(window),QString("saved query"));
        QVERIFY(QDir().rmdir(file.fileName()));QVERIFY(QFile::rename(file.fileName()+".held",file.fileName()));
        QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_1,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);
        QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(selected(window),QString("saved query"));QTest::keyClick(&window,Qt::Key_F3);
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("search").toArray().size(),1);file.close();
        QTest::keyClick(&window,Qt::Key_Insert);QTest::keyClick(&window,Qt::Key_Delete);QTest::keyClick(&window,Qt::Key_F3);QCOMPARE(selected(window),QString());
        QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(selected(window),QString());capture(window,"search-history-empty");
        QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);QTest::keyClicks(&window,"6f 72 64 65 6e 65 73");
        QTest::keyClick(&window,Qt::Key_Up,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F2);QTest::keyClick(&window,Qt::Key_Up);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QCOMPARE(window.session().tags,QSet<QString>({panel+"/.htaccess",panel+"/ordenes.php"}));
    }
    void copyCurrentFileAllViewsAndSplit()
    {
        const auto source=root+"/current-source";QVERIFY(QDir().mkpath(source+"/deep"));
        const QByteArray bytes=QByteArray("BOOT\0",5)+QByteArray(70000,'b');
        write(source+"/bootmgr",bytes);write(source+"/other.php","other\n");write(source+"/deep/config.php","nested\n");
        for(const auto view:{View::Directory,View::Branch,View::Showall}){
            const auto target=root+"/current-target-"+QString::number(int(view));QVERIFY(QDir().mkdir(target));
            TreeWindow window(source);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
            window.load(true,true);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,view==View::Directory?Qt::Key_Return:view==View::Branch?Qt::Key_B:Qt::Key_S);
            QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
            const auto selected=view==View::Directory?source+"/bootmgr":source+"/deep/config.php";
            const auto select=[&]{
                QTest::keyClick(&window,Qt::Key_Home);
                for(int i=0;i<window.session().files().size() && window.session().currentFile()->path!=selected;++i)QTest::keyClick(&window,Qt::Key_Down);
            };
            select();QCOMPARE(window.session().currentFile()->path,selected);QTest::keyClick(&window,Qt::Key_U);select();
            const auto tags=window.session().tags;QVERIFY(!tags.contains(selected));QVERIFY(!tags.isEmpty());const auto count=window.session().files().size();
            const auto index=window.session().fileIndex;const auto sourceDir=window.session().directory;const auto metadata=readMetadata(selected);
            QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);QVERIFY(window.selectRoot(target));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Tab);
            QTest::keyClick(&window,Qt::Key_C);capture(window,"copy-current-as");QTest::keyClick(&window,Qt::Key_Return);capture(window,"copy-current-to");
            QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(window.status().contains("Copy completed"));
            QVERIFY2(window.status().contains("1/1"),qPrintable(window.status()));
            const auto destination=target+'/'+QFileInfo(selected).fileName();QFile copied(destination);QVERIFY(copied.open(QIODevice::ReadOnly));
            QCOMPARE(copied.readAll(),view==View::Directory?bytes:QByteArray("nested\n"));
            QCOMPARE(readMetadata(destination).mode,metadata.mode);QCOMPARE(readMetadata(destination).modified,metadata.modified);
            QCOMPARE(QDir(target).entryList(QDir::Files|QDir::NoDotAndDotDot),QStringList{QFileInfo(selected).fileName()});
            QVERIFY(!QFileInfo::exists(target+"/deep"));QCOMPARE(window.session().view,view);QCOMPARE(window.session().directory,sourceDir);
            QCOMPARE(window.session().fileIndex,index);QCOMPARE(window.session().currentFile()->path,selected);QCOMPARE(window.session().tags,tags);
            QCOMPARE(window.session().files().size(),count);QVERIFY(window.isSplit());QCOMPARE(window.activeSide(),0);
            QCOMPARE(window.oppositeSession()->files().size(),1);QCOMPARE(window.oppositeSession()->files().first().path,destination);
            QTest::keyClick(&window,Qt::Key_Tab);
            QCOMPARE(window.session().files().size(),1);QCOMPARE(window.session().files().first().path,destination);
            QTest::keyClick(&window,Qt::Key_Tab);
            QTest::keyClick(&window,Qt::Key_C,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Escape);
            QVERIFY(window.status().contains("Copy cancelled"));QCOMPARE(window.session().tags,tags);
        }
    }
    void copyDestinationRefreshPreservesContext_data()
    {
        QTest::addColumn<bool>("sharedRoot");QTest::addColumn<int>("operation");
        for(bool shared:{false,true})for(int operation=0;operation<3;++operation)
            QTest::newRow(qPrintable(QString("%1-%2").arg(shared?"shared":"separate").arg(operation)))<<shared<<operation;
    }
    void copyDestinationRefreshPreservesContext()
    {
        QFETCH(bool,sharedRoot);QFETCH(int,operation);
        const auto base=root+"/refresh",source=base+"/source",target=base+"/target";
        QVERIFY(QDir().mkpath(source+"/new"));QVERIFY(QDir().mkpath(target+"/sub"));
        write(source+"/c.txt","copied");write(source+"/new/deep.txt","nested");
        write(target+"/z.txt","selected");write(target+"/sub/keep.txt","tagged");
        TreeWindow window(sharedRoot?base:source);window.resize(1280,768);window.show();QTRY_VERIFY(!window.busy());
        window.load(true,true);QTRY_VERIFY(!window.busy());
        if(sharedRoot)QTest::keyClick(&window,Qt::Key_Down);
        QCOMPARE(window.session().directory,source);QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto sourceTags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);
        if(sharedRoot){QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_End);QTest::keyClick(&window,Qt::Key_Up);}
        else{QVERIFY(window.selectRoot(target));QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());}
        QCOMPARE(window.session().directory,target);QTest::keyClick(&window,Qt::Key_B);
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);QTest::keyClicks(&window,"*.txt");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Home);
        for(int i=0;i<window.session().files().size() && window.session().currentFile()->path!=target+"/z.txt";++i)QTest::keyClick(&window,Qt::Key_Down);
        QCOMPARE(window.session().currentFile()->path,target+"/z.txt");
        auto targetTags=window.session().tags;
        if(operation==2)targetTags.subtract(sourceTags);
        QTest::keyClick(&window,Qt::Key_F8,Qt::ShiftModifier);QTest::keyClick(&window,Qt::Key_B);
        QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,operation==2?Qt::Key_M:Qt::Key_C,operation==1?Qt::AltModifier:Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Return);
        if(operation==1){QTest::keyClick(&window,Qt::Key_R);QTest::keyClick(&window,Qt::Key_Return);}
        QTest::keyClick(&window,Qt::Key_V);QTRY_VERIFY(window.status().contains(operation==2?"Move completed":"Copy completed"));
        QCOMPARE(window.session().tags,operation==2?QSet<QString>{}:sourceTags);
        const auto check=[&](const Session &saved){
            QCOMPARE(saved.view,View::Branch);QCOMPARE(saved.directory,target);QCOMPARE(saved.tags,targetTags);
            QCOMPARE(saved.filespec.text(),QString("*.txt"));QCOMPARE(saved.files().size(),4);
            QVERIFY(saved.currentFile());QCOMPARE(saved.currentFile()->path,target+"/z.txt");
            const auto nested=target+(operation==1?"/new/deep.txt":"/deep.txt");
            bool found=false;for(const auto &file:saved.files())if(file.path==nested)found=true;QVERIFY(found);
        };
        check(*window.oppositeSession());
        displayCells(window,158,11,"4");displayCells(window,158,14,"4");displayCells(window,158,17,"2");
        capture(window,"copy-refresh-inactive");
        QTest::keyClick(&window,Qt::Key_Tab);check(window.session());
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_Tab);check(window.session());
        QVERIFY(window.selectRoot(root));QTRY_VERIFY(!window.busy());QVERIFY(window.selectRoot(sharedRoot?base:target));QTRY_VERIFY(!window.busy());
        check(window.session());capture(window,"copy-refresh-returned");
    }
    void copyCurrentNameMaskCaseAndHistory()
    {
        const auto source=root+"/copy-names";QVERIFY(QDir().mkdir(source));write(source+"/MiXeD.TXT","name test\n");
        TreeWindow window(source);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);
        const auto selected=window.session().currentFile()->path;
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Return);
        QVERIFY(window.status().contains("also a source"));QVERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClicks(&window,"*.bak");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(window.status().contains("Copy completed"));QVERIFY(QFileInfo::exists(source+"/MiXeD.bak"));
        QCOMPARE(window.session().currentFile()->path,selected);
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(window.status().contains("Copy completed"));QVERIFY(QFileInfo::exists(source+"/mixed.txt"));
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClicks(&window,"*123*.*");QTest::keyClick(&window,Qt::Key_Return);
        QVERIFY(window.status().contains("asterisk"));capture(window,"copy-current-invalid-mask");QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);
        QTest::keyClicks(&window,"saved.txt");QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(window.status().contains("Copy completed"));QVERIFY(QFileInfo::exists(source+"/saved.txt"));
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());capture(window,"copy-current-exists");
        QTest::keyClick(&window,Qt::Key_N);QVERIFY(window.status().contains("0/1"));
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Backspace);QTest::keyClick(&window,Qt::Key_Return);
        const auto target=root+"/hidden-copy";QVERIFY(QDir().mkdir(target));QTest::keyClicks(&window,target);QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(window.status().contains("Copy completed"));QVERIFY(QFileInfo::exists(target+"/MiXeD.TXT"));
        write(source+"/.htaccess","hidden\n");QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Home);
        QCOMPARE(window.session().currentFile()->name,QString(".htaccess"));
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClicks(&window,target);QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(window.status().contains("Copy completed"));QVERIFY(QFileInfo::exists(target+"/.htaccess"));
    }
    void copyCurrentReplaceNewPathAndCancel()
    {
        const auto source=root+"/single-copy",target=root+"/single-target",newTarget=root+"/single-new/path";
        QVERIFY(QDir().mkdir(source));QVERIFY(QDir().mkdir(target));write(source+"/file.txt","new\n");write(target+"/file.txt","old\n");
        TreeWindow window(source);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QTest::qWait(250);window.resize(640,400);QTest::qWait(100);QTRY_COMPARE(window.columns(),80);QTest::keyClick(&window,Qt::Key_Return);
        const auto request=[&](const QString &destination){QTest::keyClick(&window,Qt::Key_C);capture(window,"copy-current-as-80");
            QTest::keyClick(&window,Qt::Key_Return);QTest::keyClicks(&window,destination);QTest::keyClick(&window,Qt::Key_Return);};
        request(target);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_N);
        QFile old(target+"/file.txt");QVERIFY(old.open(QIODevice::ReadOnly));QCOMPARE(old.readAll(),QByteArray("old\n"));old.close();
        request(target);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(window.status().contains("Copy completed"));
        QVERIFY(old.open(QIODevice::ReadOnly));QCOMPARE(old.readAll(),QByteArray("new\n"));old.close();
        request(newTarget);QVERIFY(!QFileInfo::exists(newTarget));capture(window,"copy-current-make-path-80");QTest::keyClick(&window,Qt::Key_N);
        QVERIFY(!QFileInfo::exists(newTarget));
        request(newTarget);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(window.status().contains("Copy completed"));QVERIFY(QFileInfo::exists(newTarget+"/file.txt"));
        const auto cancelled=root+"/single-cancel";request(cancelled);
        auto *pool=QThreadPool::globalInstance();const auto threads=pool->maxThreadCount();pool->setMaxThreadCount(1);
        QSemaphore release,started;auto blocker=QtConcurrent::run(pool,[&]{started.release();release.acquire();});
        const auto cleanup=qScopeGuard([&]{release.release();blocker.waitForFinished();pool->setMaxThreadCount(threads);});QVERIFY(started.tryAcquire(1,2000));
        QTest::keyClick(&window,Qt::Key_Y);QVERIFY(window.busy());QTest::keyClick(&window,Qt::Key_Escape);release.release();
        QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("Copy cancelled"));QVERIFY(!QFileInfo::exists(cancelled));
        QCOMPARE(window.session().currentFile()->path,source+"/file.txt");
        QVERIFY(::symlink("file.txt",QFile::encodeName(source+"/link.txt").constData())==0);QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Down);
        QCOMPARE(window.session().currentFile()->name,QString("link.txt"));request(target);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_S);QVERIFY2(window.status().contains("links"),qPrintable(window.status()));QVERIFY(!QFileInfo::exists(target+"/link.txt"));
        QVERIFY(QFile::remove(source+"/file.txt"));QVERIFY(QFile::remove(source+"/link.txt"));
        QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_C);QVERIFY(window.status().contains("No file selected"));
    }
    void filePermissionsSafeUpdateAndRecovery()
    {
        const auto path=panel+"/ordenes.php",alias=panel+"/hard.php",link=panel+"/linked.php";
        const auto native=QFile::encodeName(path);QVERIFY(::chmod(native.constData(),0644)==0);
        const auto restore=qScopeGuard([&]{::chmod(native.constData(),0644);});
        QVERIFY(::link(native.constData(),QFile::encodeName(alias).constData())==0);
        QVERIFY(::symlink("ordenes.php",QFile::encodeName(link).constData())==0);
        const auto initial=readMetadata(path);QVERIFY(initial.regular);QVERIFY(!initial.directory);
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        auto result=changePermissions(initial,0444,cancel);QVERIFY2(result.changed,qPrintable(result.error));
        QCOMPARE(result.mode,quint32(0444));QCOMPARE(result.writable,::geteuid()==0);
        QCOMPARE(readMetadata(alias).mode,quint32(0444));QCOMPARE(readMetadata(path).modified,initial.modified);
        QCOMPARE(readMetadata(path).size,initial.size);
        result=changePermissions(readMetadata(path),0,cancel);QVERIFY(result.changed);QCOMPARE(result.mode,quint32(0));
        result=changePermissions(readMetadata(path),0640,cancel);QVERIFY2(result.changed,qPrintable(result.error));
        QVERIFY(result.writable);QCOMPARE(readMetadata(path).mode,quint32(0640));
        result=changePermissions(readMetadata(link),0777,cancel);QVERIFY(!result.changed);QVERIFY(!result.error.isEmpty());
        QCOMPARE(readMetadata(path).mode,quint32(0640));
        QFile data(path);QVERIFY(data.open(QIODevice::ReadOnly));QCOMPARE(data.readAll(),QByteArray("ordenes\n"));
        const auto reviewed=readMetadata(path);QVERIFY(QFile::rename(path,path+".old"));write(path,"replacement\n");
        const auto replacement=readMetadata(path);
        result=changePermissions(reviewed,0700,cancel);QVERIFY(!result.changed);QVERIFY(result.error.contains("changed since review"));
        QCOMPARE(readMetadata(path).mode,replacement.mode);QCOMPARE(readMetadata(path+".old").mode,quint32(0640));
        QVERIFY(QFile::remove(path));QVERIFY(::mkfifo(native.constData(),0600)==0);
        result=changePermissions(reviewed,0700,cancel);QVERIFY(!result.changed);QVERIFY(result.error.contains("changed type"));
        result=changePermissions(readMetadata(path),0700,cancel);QVERIFY(!result.changed);
    }
    void fileAttributesDirectoryBranchShowall()
    {
        const auto base=root+"/file-attributes";QVERIFY(QDir().mkpath(base+"/child"));
        for(int i=0;i<44;++i)write(base+QString("/file_%1.php").arg(i,2,10,QChar('0')));
        write(base+"/child/nested.php");
        for(const auto view:{View::Directory,View::Branch,View::Showall}){
            TreeWindow window(base);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
            window.load(true,true);QTRY_VERIFY(!window.busy());
            QTest::keyClick(&window,view==View::Directory?Qt::Key_Return:view==View::Branch?Qt::Key_B:Qt::Key_S);
            QCOMPARE(window.session().view,view);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
            if(view!=View::Directory){
                QTest::keyClick(&window,Qt::Key_U);QTest::keyClick(&window,Qt::Key_Return,Qt::ControlModifier);QVERIFY(window.session().tagsOnly);
                QTest::keyClick(&window,Qt::Key_Home);
            }
            QTest::keyClick(&window,Qt::Key_F8);
            for(int i=0;i<43;++i)QTest::keyClick(&window,Qt::Key_Down);
            const auto selected=window.session().currentFile()->path,untouched=window.session().files().first().path;
            const auto native=QFile::encodeName(selected);QVERIFY(::chmod(native.constData(),0644)==0);
            const auto restore=qScopeGuard([&]{::chmod(native.constData(),0644);});
            const auto old=readMetadata(selected),other=readMetadata(untouched),directory=readMetadata(base);
            const auto tags=window.session().tags;const auto index=window.session().fileIndex;
            const auto normal=window.grab().toImage();
            QTest::keyClick(&window,Qt::Key_A);QTest::keyClicks(&window,"u-w,go-w");
            capture(window,"file-attributes-input");QTest::keyClick(&window,Qt::Key_Return);capture(window,"file-attributes-review");
            QCOMPARE(readMetadata(selected).mode,old.mode);QTest::keyClick(&window,Qt::Key_N);
            QCOMPARE(readMetadata(selected).mode,old.mode);QCOMPARE(window.grab().toImage().copy(0,0,window.width(),window.height()-80),normal.copy(0,0,window.width(),window.height()-80));
            QTest::keyClick(&window,Qt::Key_A);QTest::keyClicks(&window,"444");
            QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());
            QVERIFY2(window.status().contains("changed to 0444"),qPrintable(window.status()));
            QCOMPARE(readMetadata(selected).mode,quint32(0444));QCOMPARE(readMetadata(untouched).mode,other.mode);
            QCOMPARE(readMetadata(base).mode,directory.mode);QCOMPARE(readMetadata(selected).modified,old.modified);
            QCOMPARE(window.session().view,view);QCOMPARE(window.session().fileIndex,index);QCOMPARE(window.session().currentFile()->path,selected);
            QCOMPARE(window.session().tags,tags);QVERIFY(window.isSplit());QCOMPARE(window.activeSide(),0);
            QCOMPARE(window.session().currentFile()->writable,::geteuid()==0);
            QCOMPARE(window.oppositeSession()->files()[index].writable,::geteuid()==0);
            QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_A);
            QTest::keyClicks(&window,"a+X");QTest::keyClick(&window,Qt::Key_Return);
            QVERIFY(window.status().contains("already match"));QCOMPARE(readMetadata(selected).mode,quint32(0444));
            QTest::keyClick(&window,Qt::Key_A);QTest::keyClicks(&window,"u+w");QTest::keyClick(&window,Qt::Key_Return);
            QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QCOMPARE(readMetadata(selected).mode,quint32(0644));
            capture(window,"file-attributes-info");QTest::keyClick(&window,Qt::Key_Escape);
            QCOMPARE(window.session().currentFile()->path,selected);QVERIFY(window.session().currentFile()->writable);
            const auto directoryMode=readMetadata(base).mode;
            QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(window.status().contains("cancelled"));
            QCOMPARE(readMetadata(base).mode,directoryMode);QCOMPARE(readMetadata(selected).mode,quint32(0644));
        }
    }
    void fileAttributesErrorsAndCompactPrompt()
    {
        const auto base=root+"/file-attributes-errors";QVERIFY(QDir().mkpath(base));write(base+"/current.php");
        QVERIFY(::symlink("current.php",QFile::encodeName(base+"/link.php").constData())==0);
        TreeWindow window(base);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QTest::qWait(250);window.resize(640,400);QTest::qWait(100);QTRY_COMPARE(window.columns(),80);
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_A);QTest::keyClicks(&window,"888");
        QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.status().contains("octal"));QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClicks(&window,"400");QTest::keyClick(&window,Qt::Key_Return);capture(window,"file-attributes-review-80");
        QTest::keyClick(&window,Qt::Key_Escape);const auto mode=readMetadata(base+"/current.php").mode;
        QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(window.session().currentFile()->name,QString("link.php"));
        QTest::keyClick(&window,Qt::Key_A);QVERIFY(window.status().contains("symbolic links"));
        QCOMPARE(readMetadata(base+"/current.php").mode,mode);
        QTest::keyClick(&window,Qt::Key_Up);QTest::keyClick(&window,Qt::Key_A);QTest::keyClicks(&window,"400");
        QTest::keyClick(&window,Qt::Key_Return);QVERIFY(::chmod(QFile::encodeName(base+"/current.php").constData(),0600)==0);
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("changed since review"));
        QCOMPARE(readMetadata(base+"/current.php").mode,quint32(0600));
        QVERIFY(QFile::remove(base+"/current.php"));QVERIFY(QDir().mkdir(base+"/current.php"));
        const auto replacementMode=readMetadata(base+"/current.php").mode;
        QTest::keyClick(&window,Qt::Key_A);QVERIFY(window.status().contains("not a regular file"));
        QCOMPARE(readMetadata(base+"/current.php").mode,replacementMode);QVERIFY(QDir().rmdir(base+"/current.php"));
        QTest::keyClick(&window,Qt::Key_A);QVERIFY(window.status().contains("Cannot read permissions"));
        QVERIFY(QFile::remove(base+"/link.php"));QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_A);QVERIFY(window.status().contains("No file selected"));
    }
    void metadataFilesDirectoriesAndLinks()
    {
        const auto file=panel+"/ordenes.php",hard=panel+"/hard.php",link=panel+"/linked",dangling=panel+"/dangling";
        QVERIFY(::chmod(QFile::encodeName(file).constData(),0640)==0);
        QVERIFY(::link(QFile::encodeName(file).constData(),QFile::encodeName(hard).constData())==0);
        QVERIFY(::symlink("ordenes.php",QFile::encodeName(link).constData())==0);
        QVERIFY(::symlink("missing",QFile::encodeName(dangling).constData())==0);
        const auto value=readMetadata(file);QVERIFY(value.error.isEmpty());QVERIFY(!value.directory);QVERIFY(!value.symlink);
        QCOMPARE(value.type,QString("Regular file"));QCOMPARE(value.mode,quint32(0640));QCOMPARE(value.links,quint64(2));
        QCOMPARE(value.uid,quint32(::getuid()));QCOMPARE(value.gid,quint32(::getgid()));QVERIFY(!value.owner.isEmpty());
        QCOMPARE(value.size,quint64(QFileInfo(file).size()));QVERIFY(value.allocated>=value.size);
        QVERIFY(value.modified.isValid());QVERIFY(value.accessed.isValid());QVERIFY(value.changed.isValid());
        QCOMPARE(readMetadata(hard).inode,value.inode);
        const auto directory=readMetadata(panel);QVERIFY(directory.directory);QCOMPARE(directory.type,QString("Directory"));
        const auto symbolic=readMetadata(link);QVERIFY(symbolic.error.isEmpty());QVERIFY(symbolic.symlink);QVERIFY(!symbolic.directory);
        QCOMPARE(symbolic.linkTarget,QString("ordenes.php"));QVERIFY(symbolic.inode!=value.inode);
        const auto broken=readMetadata(dangling);QVERIFY(broken.error.isEmpty());QVERIFY(broken.symlink);QCOMPARE(broken.linkTarget,QString("missing"));
        QVERIFY(!readMetadata(panel+"/missing").error.isEmpty());QVERIFY(!readMetadata({}).error.isEmpty());
        QCOMPARE(permissionText(04755),QString("rwsr-xr-x"));QCOMPARE(permissionText(02640),QString("rw-r-S---"));
        QCOMPARE(permissionText(01777),QString("rwxrwxrwt"));QCOMPARE(permissionText(01000),QString("--------T"));
        QCOMPARE(permissionOctal(00755),QString("0755"));
    }
    void permissionExpressions()
    {
        quint32 mode=0;QString error;
        for(const auto &test:QList<QPair<QString,quint32>>{{"755",0755},{"0000",0},{"2770",02770},{"u+w",0755},
            {"go-w",0755},{"u=rwx,g=rx,o=",0750},{"a=",0},{"a+X",0755},{"u+s,g+s,o+t",07755},{"u-s,g-s,o-t",0755}}){
            QVERIFY2(parsePermissions(test.first,test.first=="u-s,g-s,o-t"?07755:0755,true,mode,error),qPrintable(error));
            QCOMPARE(mode,test.second);
        }
        QVERIFY(parsePermissions("u+w",0555,true,mode,error));QCOMPARE(mode,quint32(0755));
        QVERIFY(parsePermissions("go-w",0777,true,mode,error));QCOMPARE(mode,quint32(0755));
        QVERIFY(parsePermissions("a+X",0640,false,mode,error));QCOMPARE(mode,quint32(0640));
        QVERIFY(parsePermissions("a+X",0640,true,mode,error));QCOMPARE(mode,quint32(0751));
        QVERIFY(parsePermissions("g+s",0750,true,mode,error));QCOMPARE(mode,quint32(02750));
        for(const auto &invalid:QStringList{"","888","77","10000","u+","+w","u+r,","u+rwz","g=u","u+755","o+s","g+t","u+r;pwd"}){
            QVERIFY2(!parsePermissions(invalid,0755,true,mode,error),qPrintable(invalid));QVERIFY(!error.isEmpty());
        }
    }
    void directoryPermissionsSafeUpdate()
    {
        const auto path=root+"/permissions";QVERIFY(QDir().mkpath(path+"/child"));write(path+"/keep.txt");
        const auto native=QFile::encodeName(path);const auto restore=qScopeGuard([&]{::chmod(native.constData(),0755);});
        QVERIFY(::chmod(native.constData(),0755)==0);const auto child=readMetadata(path+"/child"),file=readMetadata(path+"/keep.txt");
        auto cancel=std::make_shared<std::atomic_bool>(false);
        auto changed=changePermissions(readMetadata(path),02750,cancel);QVERIFY2(changed.changed,qPrintable(changed.error));
        QCOMPARE(changed.mode,quint32(02750));QCOMPARE(readMetadata(path).mode,quint32(02750));
        QCOMPARE(readMetadata(path+"/child").mode,child.mode);QCOMPARE(readMetadata(path+"/keep.txt").mode,file.mode);
        changed=changePermissions(readMetadata(path),0,cancel);QVERIFY(changed.changed);QCOMPARE(readMetadata(path).mode,quint32(0));
        changed=changePermissions(readMetadata(path),0700,cancel);QVERIFY2(changed.changed,qPrintable(changed.error));
        QCOMPARE(readMetadata(path).mode,quint32(0700));cancel->store(true);
        changed=changePermissions(readMetadata(path),0755,cancel);QVERIFY(changed.cancelled);QVERIFY(!changed.changed);
        QCOMPARE(readMetadata(path).mode,quint32(0700));
    }
    void permissionsRejectChangedDirectoriesAndLinks()
    {
        const auto path=root+"/permissions",old=root+"/old-permissions",link=root+"/permissions-link";
        QVERIFY(QDir().mkpath(path));QVERIFY(::chmod(QFile::encodeName(path).constData(),0755)==0);
        const auto initial=readMetadata(path);auto cancel=std::make_shared<std::atomic_bool>(false);
        QVERIFY(::chmod(QFile::encodeName(path).constData(),0700)==0);
        auto result=changePermissions(initial,0777,cancel);QVERIFY(!result.changed);QVERIFY(result.error.contains("changed since review"));
        const auto current=readMetadata(path);QVERIFY(QDir().rename(path,old));QVERIFY(QDir().mkpath(path));
        result=changePermissions(current,0777,cancel);QVERIFY(!result.changed);QVERIFY(result.error.contains("changed since review"));
        const auto oldMode=readMetadata(old).mode,newMode=readMetadata(path).mode;
        QVERIFY(::symlink(QFile::encodeName(old).constData(),QFile::encodeName(link).constData())==0);
        result=changePermissions(readMetadata(link),0777,cancel);QVERIFY(!result.changed);QVERIFY(!result.error.isEmpty());
        QVERIFY(QDir().mkpath(old+"/child"));
        result=changePermissions(readMetadata(link+"/child"),0777,cancel);QVERIFY(!result.changed);QVERIFY(!result.error.isEmpty());
        QCOMPARE(readMetadata(old).mode,oldMode);QCOMPARE(readMetadata(path).mode,newMode);
        QCOMPARE(readMetadata(old+"/child").mode,readMetadata(link+"/child").mode);
    }
    void infoNavigationAndSplitState()
    {
        TreeWindow window(panel);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        window.load(true,true);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_B);
        QCOMPARE(window.session().view,View::Branch);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto tags=window.session().tags;QTest::keyClick(&window,Qt::Key_F8);
        const auto before=window.grab().toImage();QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);
        const auto infoImage=window.grab().toImage();QVERIFY(infoImage!=before);capture(window,"info-file");
        const auto nameCells=infoImage.copy((window.columns()-31)*8,(window.rows()-12)*16,26*8,16);
        int namePixels=0;
        for(int y=0;y<nameCells.height();++y)for(int x=0;x<nameCells.width();++x)
            if(nameCells.pixelColor(x,y)==QColor(255,255,0))++namePixels;
        QVERIFY(namePixels>10);
        QSignalSpy opened(&window,&TreeWindow::openRequested);QTest::keyClick(&window,Qt::Key_O);QCOMPARE(opened.size(),1);
        QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(window.session().fileIndex,1);QCOMPARE(window.session().tags,tags);
        QTest::keyClick(&window,Qt::Key_End);QCOMPARE(window.session().fileIndex,int(window.session().files().size())-1);
        QTest::keyClick(&window,Qt::Key_Home);QCOMPARE(window.session().fileIndex,0);
        QTest::keyClick(&window,Qt::Key_Tab);QCOMPARE(window.activeSide(),1);QCOMPARE(window.session().view,View::Branch);
        QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(window.isSplit());QCOMPARE(window.session().tags,tags);
        QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Tree);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_I);
        QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(window.session().directory,panel+"/modulos");capture(window,"info-directory");
        QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);QCOMPARE(window.session().directory,panel+"/modulos");
        QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Directory);
        QTest::keyClick(&window,Qt::Key_O);QCOMPARE(opened.size(),2);
    }
    void infoOverlayPreservesListScrollAndCommands()
    {
        const auto base=root+"/info-overlay";QVERIFY(QDir().mkpath(base+"/sub"));
        for(int i=0;i<80;++i)write(base+QString("/file-%1.txt").arg(i,2,10,QChar('0')));
        write(base+"/sub/nested.txt");
        for(const auto view:{View::Directory,View::Branch,View::Showall,View::Global}){
            TreeWindow window(base);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
            window.load(true,true);QTRY_VERIFY(!window.busy());
            QTest::keyClick(&window,view==View::Directory?Qt::Key_Return:view==View::Branch?Qt::Key_B:view==View::Showall?Qt::Key_S:Qt::Key_G);
            QCOMPARE(window.session().view,view);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
            QTest::keyClick(&window,Qt::Key_U);QTest::keyClick(&window,Qt::Key_Home);
            QTest::qWait(250);window.resize(640,400);QTest::qWait(100);QTRY_COMPARE(window.columns(),80);
            for(int i=0;i<43;++i)QTest::keyClick(&window,Qt::Key_Down);
            const auto path=window.session().currentFile()->path;const auto tags=window.session().tags;
            const auto index=window.session().fileIndex;const auto normal=window.grab().toImage();
            QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);const auto shown=window.grab().toImage();
            QCOMPARE(window.session().fileIndex,index);QCOMPARE(window.session().currentFile()->path,path);QCOMPARE(window.session().tags,tags);
            QCOMPARE(shown.copy(0,0,33*8,320),normal.copy(0,0,33*8,320));
            QCOMPARE(shown.copy(0,0,640,6*16),normal.copy(0,0,640,6*16));
            QCOMPARE(shown.copy(0,21*16,640,2*16),normal.copy(0,21*16,640,2*16));
            QVERIFY(shown!=normal);capture(window,"info-overlay-80");
            QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint(50*8,12*16));
            QCOMPARE(window.session().fileIndex,index);
            QTest::keyClick(&window,Qt::Key_Return,Qt::ControlModifier);QVERIFY(window.session().tagsOnly);
            QTest::keyClick(&window,Qt::Key_Return,Qt::ControlModifier);QVERIFY(!window.session().tagsOnly);
            QCOMPARE(window.grab().toImage().copy(33*8,6*16,46*8,15*16),shown.copy(33*8,6*16,46*8,15*16));
            QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(window.session().fileIndex,index+1);
            QVERIFY(window.grab().toImage().copy(33*8,6*16,46*8,15*16)!=shown.copy(33*8,6*16,46*8,15*16));
            const auto untagged=window.session().currentFile()->path;
            QTest::keyClick(&window,Qt::Key_U);QVERIFY(!window.session().tags.contains(untagged));
            QTest::keyClick(&window,Qt::Key_C);capture(window,"info-overlay-copy");QTest::keyClick(&window,Qt::Key_Escape);
            QCOMPARE(window.session().view,view);const auto afterCopy=window.session().fileIndex;
            QTest::keyClick(&window,Qt::Key_T);QCOMPARE(window.session().fileIndex,afterCopy+1);
            QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(window.session().view,view);
            const auto without=window.grab().toImage();capture(window,"info-overlay-toggle-before");
            QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_I);
            QVERIFY(window.grab().toImage()!=without);
            QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_I);
            capture(window,"info-overlay-toggle-after");
            QCOMPARE(window.grab().toImage().copy(0,16,640,320),without.copy(0,16,640,320));
            QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_F8);
            const auto split=window.grab().toImage();
            QTest::keyClick(&window,Qt::Key_Escape);
            QCOMPARE(window.grab().toImage().copy(0,0,320,320),split.copy(0,0,320,320));
            QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);
            capture(window,"info-overlay-right-pane-80");
            QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);QCOMPARE(window.activeSide(),1);QCOMPARE(window.session().view,view);
        }
    }
    void infoLinksErrorsAndCompactLayout()
    {
        const auto path=root+"/info";QVERIFY(QDir().mkpath(path));
        QVERIFY(::symlink("missing-target",QFile::encodeName(path+"/dangling").constData())==0);
        TreeWindow window(path);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        // Esperar también la configuración inicial del compositor antes de redimensionar.
        QTest::qWait(250);window.resize(640,400);QTest::qWait(100);QTRY_COMPARE(window.columns(),80);
        QTest::keyClick(&window,Qt::Key_F7);QVERIFY(window.findChild<FileViewer*>("autoview"));
        QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);QCOMPARE(window.session().view,View::Directory);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QTest::keyClick(&window,Qt::Key_F7);QVERIFY(window.findChild<FileViewer*>("autoview")->isVisible());
        QTest::keyClick(&window,Qt::Key_F7);QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);capture(window,"info-link-80");
        const auto linkImage=window.grab().toImage();QVERIFY(QFile::remove(path+"/dangling"));
        QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());QVERIFY(window.grab().toImage()!=linkImage);capture(window,"info-missing-80");
        QTest::keyClick(&window,Qt::Key_Enter);QCOMPARE(window.session().view,View::Directory);
        QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());QVERIFY(window.session().files().isEmpty());
        QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);capture(window,"info-no-file-80");
        QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(window.session().view,View::Directory);
    }
    void permissionsPromptReviewEditingAndState()
    {
        const auto native=QFile::encodeName(panel);QVERIFY(::chmod(native.constData(),0755)==0);
        const auto restore=qScopeGuard([&]{::chmod(native.constData(),0755);});
        TreeWindow window(panel);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        window.load(true,true);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto tags=window.session().tags;QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_A);
        QTest::keyClicks(&window,"888");QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.status().contains("octal"));
        QCOMPARE(readMetadata(panel).mode,quint32(0755));QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClicks(&window,"u-w");QTest::keyClick(&window,Qt::Key_Return);capture(window,"permissions-review");
        QCOMPARE(readMetadata(panel).mode,quint32(0755));QTest::keyClick(&window,Qt::Key_N);
        QCOMPARE(readMetadata(panel).mode,quint32(0755));QVERIFY(window.status().contains("cancelled"));
        QTest::keyClick(&window,Qt::Key_A,Qt::AltModifier);QTest::keyClicks(&window,"u-w");
        QTest::keyClick(&window,Qt::Key_Left);QTest::keyClick(&window,Qt::Key_Backspace);QTest::keyClicks(&window,"+");
        QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.status().contains("already match"));
        QTest::keyClick(&window,Qt::Key_A,Qt::AltModifier);QTest::keyClicks(&window,"u-w");
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Backspace);
        QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(&window,"750");capture(window,"permissions-input");
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());
        QCOMPARE(readMetadata(panel).mode,quint32(0750));QVERIFY(window.status().contains("changed to 0750"));
        QCOMPARE(window.session().view,View::Tree);QCOMPARE(window.session().directory,panel);
        QCOMPARE(window.session().tags,tags);QVERIFY(window.isSplit());QCOMPARE(window.activeSide(),0);
        QTest::keyClick(&window,Qt::Key_I,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_A,Qt::AltModifier);
        QTest::keyClicks(&window,"u+w");QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.status().contains("already match"));
        QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Directory);
        QTest::keyClick(&window,Qt::Key_A,Qt::AltModifier);QVERIFY(window.status().contains("not implemented"));
    }
    void permissionsPromptStaleAndCancelledWorker()
    {
        const auto native=QFile::encodeName(panel);QVERIFY(::chmod(native.constData(),0755)==0);
        const auto restore=qScopeGuard([&]{::chmod(native.constData(),0755);});
        TreeWindow window(panel);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_A,Qt::AltModifier);QTest::keyClicks(&window,"700");QTest::keyClick(&window,Qt::Key_Return);
        QVERIFY(::chmod(native.constData(),0750)==0);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QCOMPARE(readMetadata(panel).mode,quint32(0750));QVERIFY(window.status().contains("changed since review"));
        auto *pool=QThreadPool::globalInstance();const auto original=pool->maxThreadCount();pool->setMaxThreadCount(1);
        QSemaphore release,started;auto blocker=QtConcurrent::run(pool,[&]{started.release();release.acquire();});
        const auto unblock=qScopeGuard([&]{release.release();blocker.waitForFinished();pool->setMaxThreadCount(original);});
        QVERIFY(started.tryAcquire(1,2000));
        QTest::keyClick(&window,Qt::Key_A,Qt::AltModifier);QTest::keyClicks(&window,"700");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.busy());QTest::keyClick(&window,Qt::Key_Escape);
        release.release();QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("cancelled"));
        QCOMPARE(readMetadata(panel).mode,quint32(0750));
        window.resize(640,400);QTest::qWait(100);QTRY_COMPARE(window.columns(),80);
        QTest::keyClick(&window,Qt::Key_A,Qt::AltModifier);QTest::keyClicks(&window,"700");QTest::keyClick(&window,Qt::Key_Return);
        capture(window,"permissions-review-80");QTest::keyClick(&window,Qt::Key_Escape);
        QCOMPARE(readMetadata(panel).mode,quint32(0750));
    }
    void branchSizeLabels()
    {
        BranchSize size;QVERIFY(size.label().isEmpty());
        size.logged=true;QCOMPARE(size.label(),QString("{ ? }"));
        size.complete=true;QVERIFY(size.label().isEmpty());
        size.files=1;
        for(const auto pair:QList<QPair<quint64,QString>>{{0,"0k"},{1,"1k"},{1024,"1k"},{1025,"2k"},{2577*1024,"2.577k"}}){
            size.bytes=pair.first;const auto label=size.label();QCOMPARE(label.size(),14);QVERIFY(label.startsWith('['));QVERIFY(label.endsWith(pair.second+"]"));
        }
        size.complete=false;QVERIFY(size.label().startsWith('{'));QVERIFY(size.label().endsWith('}'));
        size.complete=true;size.bytes=quint64(std::numeric_limits<qint64>::max());
        QCOMPARE(size.label().size(),14);QVERIFY(size.label().endsWith("8.388.608T]"));
        size.bytes=std::numeric_limits<quint64>::max();QCOMPARE(size.label().size(),14);QVERIFY(size.label().endsWith("16.777.216T]"));
    }
    void branchSizesLoggingCollapseAndChanges()
    {
        const auto base=root+"/sizes",a=base+"/a",deep=a+"/deep",a2=base+"/a2";
        for(const auto &path:{deep,a2,base+"/empty"})QVERIFY(QDir().mkpath(path));
        write(base+"/root.txt",QByteArray(17,'r'));write(a+"/a.txt",QByteArray(2048,'a'));
        write(deep+"/deep.txt",QByteArray(1536,'d'));write(a2+"/zero.txt",{});
        Session session(base);session.apply(scan(base),true);
        auto size=session.branchSizes().value(base);QCOMPARE(size.bytes,quint64(17));QCOMPARE(size.files,quint64(1));QVERIFY(!size.complete);
        QVERIFY(session.branchSizes().value(a).label().isEmpty());
        session.apply(scan(a,true),true);size=session.branchSizes().value(a);
        QCOMPARE(size.bytes,quint64(3584));QVERIFY(size.complete);QVERIFY(!session.branchSizes().value(base).complete);
        session.apply(scan(base,true),true);size=session.branchSizes().value(base);
        QCOMPARE(size.bytes,quint64(3601));QCOMPARE(size.files,quint64(4));QVERIFY(size.complete);
        QVERIFY(session.branchSizes().value(base+"/empty").label().isEmpty());
        QCOMPARE(session.branchSizes().value(a2).files,quint64(1));QVERIFY(session.branchSizes().value(a2).label().endsWith("0k]"));
        session.selectDirectory(a);session.toggleCollapse(false);
        QVERIFY(std::none_of(session.tree().cbegin(),session.tree().cend(),[&](const TreeRow &row){return row.path==deep;}));
        QCOMPARE(session.branchSizes().value(a).bytes,quint64(3584));QVERIFY(session.branchSizes().value(a).complete);
        QVERIFY(session.filespec.set("*.missing"));session.tags.insert(a+"/a.txt");session.rebuild();
        QCOMPARE(session.branchSizes().value(base).bytes,quint64(3601));
        QVERIFY(QFile::remove(deep+"/deep.txt"));session.removeFile(deep+"/deep.txt");
        QCOMPARE(session.branchSizes().value(base).bytes,quint64(2065));QVERIFY(session.branchSizes().value(deep).label().isEmpty());
        write(a+"/a.txt",QByteArray(8192,'a'));session.apply(scan(a),true);
        QCOMPARE(session.branchSizes().value(base).bytes,quint64(8209));QVERIFY(session.branchSizes().value(base).complete);
        ScanResult failed;failed.directories.append({a,{}, {},"Access denied"});session.apply(failed,true);
        QCOMPARE(session.branchSizes().value(base).bytes,quint64(8209));QVERIFY(!session.branchSizes().value(base).complete);
        session.apply(scan(a,true),true);QVERIFY(session.branchSizes().value(base).complete);
        session.selectDirectory(a);session.unlog();
        QCOMPARE(session.branchSizes().value(base).bytes,quint64(17));QVERIFY(!session.branchSizes().value(base).complete);
        QVERIFY(session.branchSizes().value(a).label().isEmpty());
        session.apply(scan(base,true),true);session.removeDirectory(a);
        QCOMPARE(session.branchSizes().value(base).bytes,quint64(17));QVERIFY(session.branchSizes().value(base).complete);
        const auto boundary=std::make_shared<std::atomic_bool>(false);
        Session bounded(base);bounded.apply(scanDirectories(base,true,boundary,{a}),true);
        QCOMPARE(bounded.branchSizes().value(base).bytes,quint64(17));QVERIFY(!bounded.branchSizes().value(base).complete);
    }
    void treeSizesOptionalLayoutAndRefresh()
    {
        const auto base=root+"/a-very-long-directory-name-to-check-column-clipping",child=base+"/child";
        QVERIFY(QDir().mkpath(child));write(base+"/root.txt",QByteArray(1500,'r'));write(child+"/child.txt",QByteArray(2048,'c'));
        TreeWindow window(base);window.resize(1280,768);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTRY_VERIFY(!window.busy());
        QVERIFY(!window.treeSizesVisible());
        const auto treeRegion=[&]{return window.grab().toImage().copy(0,32,window.width(),200);};
        const auto without=treeRegion();window.setTreeSizesVisible(true);QVERIFY(window.treeSizesVisible());QVERIFY(treeRegion()!=without);
        const auto badge=[&](int column,const QString &text,bool selected,int offset=0){
            QImage expected(14*8,16,QImage::Format_RGB32);expected.fill(selected?QColor(192,192,192):QColor(0,0,128));
            ConsoleFont font;{QPainter painter(&expected);for(int i=0;i<text.size();++i)
                QVERIFY(font.draw(painter,i*8,0,text.mid(i,1),selected?Qt::black:QColor(255,255,0)));}
            const auto image=window.grab().toImage();const auto ratio=image.devicePixelRatio();
            const auto actual=image.copy(qRound((offset+column*8)*ratio),qRound(32*ratio),qRound(14*8*ratio),qRound(16*ratio));
            const auto reference=expected.scaled(actual.size(),Qt::IgnoreAspectRatio,Qt::FastTransformation);
            const auto converted=actual.convertToFormat(QImage::Format_RGB32);
            if(converted!=reference){
                const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");
                if(!dir.isEmpty()){QVERIFY(QDir().mkpath(dir));actual.save(dir+"/tree-sizes-actual.png");reference.save(dir+"/tree-sizes-expected.png");window.grab().save(dir+"/tree-sizes-layout-error.png");}
            }
            QVERIFY2(converted==reference,qPrintable(QString("Size column %1 offset %2: %3").arg(column).arg(offset).arg(text)));
        };
        badge(123,"{          2k}",true);capture(window,"tree-sizes-partial");
        window.setTreeSizesVisible(false);QCOMPARE(treeRegion(),without);window.setTreeSizesVisible(true);
        window.load(true,true);QTRY_VERIFY(!window.busy());badge(123,"[          4k]",true);capture(window,"tree-sizes-complete");
        QTest::keyClick(&window,Qt::Key_F8);badge(65,"[          4k]",true);badge(65,"[          4k]",false,640);
        capture(window,"tree-sizes-split");
        QTest::keyClick(&window,Qt::Key_F8,Qt::ShiftModifier);QTest::keyClick(&window,Qt::Key_C);
        badge(43,"[          4k]",true);badge(65,"[          4k]",false,640);
        QTest::keyClick(&window,Qt::Key_F8,Qt::ShiftModifier);QTest::keyClick(&window,Qt::Key_B);
        badge(43,"[          4k]",true);badge(43,"[          4k]",false,640);
        window.resize(640,400);QTest::qWait(100);QTRY_COMPARE(window.columns(),80);
        badge(25,"[          4k]",true);badge(25,"[          4k]",false,320);capture(window,"tree-sizes-split-80");
        write(base+"/root.txt",QByteArray(5000,'r'));QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());
        badge(25,"[          7k]",true);badge(25,"[          4k]",false,320);
        QTest::keyClick(&window,Qt::Key_Tab);badge(25,"[          7k]",false);badge(25,"[          7k]",true,320);
        QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Directory);
        const auto files=treeRegion();window.setTreeSizesVisible(false);QCOMPARE(treeRegion(),files);
    }
    void menuOneCommandAndCancellation()
    {
        TreeWindow window(panel);window.resize(1280,768);window.show();QTRY_VERIFY(!window.busy());
        const auto menuLabel=[&]{const auto image=window.grab().toImage();const auto scale=image.devicePixelRatio();
            return image.copy(0,qRound((window.rows()-3)*16*scale),qRound(9*8*scale),qRound(16*scale));};
        const auto normal=menuLabel();
        QTest::keyClick(&window,Qt::Key_F4);QVERIFY(menuLabel()!=normal);
        QTest::keyClick(&window,Qt::Key_T);QVERIFY(!window.session().tags.isEmpty());QCOMPARE(menuLabel(),normal);
        QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Directory);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_S);
        QTest::keyClicks(&window,"ordenes");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QCOMPARE(menuLabel(),normal);
        const int indexAfterSearch=window.session().fileIndex;
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_Down);
        QCOMPARE(window.session().fileIndex,std::min(indexAfterSearch+1,int(window.session().files().size())-1));QCOMPARE(menuLabel(),normal);QVERIFY(!window.status().contains("not implemented"));
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_Escape);
        QCOMPARE(window.session().view,View::Directory);QCOMPARE(menuLabel(),normal);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F1);QTest::keyClick(&window,Qt::Key_Escape);
        QCOMPARE(menuLabel(),normal);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_C);
        QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(menuLabel(),normal);
        QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Tree);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_Return);
        QVERIFY(window.isFullScreen());QTest::keyClick(&window,Qt::Key_Return,Qt::AltModifier);QVERIFY(!window.isFullScreen());
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F7);
        QVERIFY(window.isMaximized());QTest::keyClick(&window,Qt::Key_F7,Qt::AltModifier);QVERIFY(!window.isMaximized());
        QTest::keyClick(&window,Qt::Key_F4);QFocusEvent lost(QEvent::FocusOut);QApplication::sendEvent(&window,&lost);
        QCOMPARE(menuLabel(),normal);
    }
    void automaticFrontendBinary()
    {
        QProcessEnvironment environment=QProcessEnvironment::systemEnvironment();
        for(const auto &name:{"DISPLAY","WAYLAND_DISPLAY","QT_QPA_PLATFORM"})environment.remove(name);
        QProcess process;process.setProcessEnvironment(environment);
        const auto executable=QCoreApplication::applicationDirPath()+"/ltc";
        process.start(executable,{"--help"});QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitStatus(),QProcess::NormalExit);QCOMPARE(process.exitCode(),0);
        QVERIFY(QString::fromUtf8(process.readAllStandardOutput()).simplified().contains("automatic without a graphical environment"));
        process.start(executable,{"--version"});QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitStatus(),QProcess::NormalExit);QCOMPARE(process.exitCode(),0);
        QVERIFY(process.readAllStandardOutput().startsWith("ltc "));
        process.start(executable,{"--new-instance",root+"/missing-auto-terminal"});QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitStatus(),QProcess::NormalExit);QCOMPARE(process.exitCode(),2);
        QVERIFY(process.readAllStandardError().contains("Not a directory"));
        // --terminal prevalece incluso sobre una plataforma gráfica inválida.
        environment.insert("QT_QPA_PLATFORM","ltree-invalid-platform");process.setProcessEnvironment(environment);
        process.start(executable,{"--terminal","--help"});QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitStatus(),QProcess::NormalExit);QCOMPARE(process.exitCode(),0);
    }
    void singleInstanceBinary()
    {
        QTemporaryDir runtime("/tmp/ltree-instance-XXXXXX");QVERIFY(runtime.isValid());
        QVERIFY(QFile::setPermissions(runtime.path(),QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        QProcessEnvironment environment=QProcessEnvironment::systemEnvironment();
        environment.insert("XDG_RUNTIME_DIR",runtime.path());environment.insert("QT_QPA_PLATFORM","offscreen");
        const auto executable=QCoreApplication::applicationDirPath()+"/ltc";
        QProcess first,second,extra,restarted;
        for(auto *process:{&first,&second,&extra,&restarted})process->setProcessEnvironment(environment);
        const auto cleanup=qScopeGuard([&]{for(auto *process:{&first,&second,&extra,&restarted}){
            if(process->state()!=QProcess::NotRunning){process->kill();process->waitForFinished(3000);}
        }});
        first.start(executable,{root});QVERIFY(first.waitForStarted());
        QTRY_VERIFY(QFileInfo::exists(runtime.path()+"/ltreec.socket"));
        second.start(executable,{"--tree-sizes",panel});QVERIFY(second.waitForStarted());QVERIFY(second.waitForFinished(5000));
        QCOMPARE(second.exitCode(),0);QVERIFY(second.readAllStandardOutput().contains("Activated the existing"));
        QCOMPARE(first.state(),QProcess::Running);
        extra.start(executable,{"--new-instance","--tree-sizes",panel});QVERIFY(extra.waitForStarted());
        QVERIFY(!extra.waitForFinished(200));QCOMPARE(first.state(),QProcess::Running);
        // Un cierre abrupto debe permitir recuperar el bloqueo y el socket residuales.
        first.kill();QVERIFY(first.waitForFinished(3000));
        restarted.start(executable,{root});QVERIFY(restarted.waitForStarted());QVERIFY(!restarted.waitForFinished(200));
        second.start(executable,{root});QVERIFY(second.waitForStarted());QVERIFY(second.waitForFinished(5000));QCOMPARE(second.exitCode(),0);
        extra.kill();QVERIFY(extra.waitForFinished(3000));restarted.kill();QVERIFY(restarted.waitForFinished(3000));
        first.start(executable,{root});second.start(executable,{root});QVERIFY(first.waitForStarted());QVERIFY(second.waitForStarted());
        QVERIFY(QTest::qWaitFor([&]{return first.state()==QProcess::NotRunning || second.state()==QProcess::NotRunning;},5000));
        auto *winner=first.state()==QProcess::Running?&first:&second;
        auto *loser=winner==&first?&second:&first;
        QCOMPARE(winner->state(),QProcess::Running);QCOMPARE(loser->exitCode(),0);
        QVERIFY(loser->readAllStandardOutput().contains("Activated the existing"));
    }
    void executeFixedFontWithoutWarning()
    {
        QStringList messages;fontDiagnostics=&messages;previousFontHandler=qInstallMessageHandler(checkFontMessage);
        const auto restore=qScopeGuard([]{qInstallMessageHandler(previousFontHandler);fontDiagnostics=nullptr;});
        ExecuteWindow window(root,"/bin/bash",{"--noprofile","--norc","-i"});
        QVERIFY2(messages.isEmpty(),qPrintable(messages.join('\n')));
        auto font=window.terminal()->getTerminalFont();QVERIFY(QFontInfo(font).fixedPitch());
        for(bool bold:{false,true}){
            font.setBold(bold);QFontMetrics metrics(font);
            for(int code=32;code<127;++code)QCOMPARE(metrics.horizontalAdvance(QChar(code)),8);
            for(const auto glyph:QString::fromUtf8("áéñ│─┌┐└┘┼█"))QCOMPARE(metrics.horizontalAdvance(glyph),8);
        }
    }
    void copyMasksFromReference()
    {
        const QList<QPair<QString,QString>> examples{
            {"*.*","ABC.DEF.XYZ"},{"1*.*","1BC.DEF.XYZ"},{"1*.2*","1BC.DEF.2YZ"},{"1*.","1BC.DEF"},{"1*","1BC.DEF"},
            {"12*.*","12C.DEF.XYZ"},{"*12.*","ABC.D12.XYZ"},{"1*2.*3","1BC.DE2.XY3"},{"1.*.2*3","1.C.DEF.2Y3"},{".*1.2*",".BC.DE1.2YZ"},
            {"12345?\?.6?\?","12345EF.6YZ"},{"?\?12345.?67","AB12345.X67"},{"12?\?345.6?7","12C.345.6Y7"},
            {"?1*2?.*","A1C.D2F.XYZ"},{"?1*2?\??.*","A1C2DEF.XYZ"},{"?\?1*2?\?.*","AB1.2EF.XYZ"},{".?\?1.?2.*",".BC1.E2.XYZ"},
            {"/*.*","BC.DEF.XYZ"},{"*/.?/*","ABC.DE.XZ"},{"?\?//*./*","ABDEF.YZ"},{"1?///?\?.?/?","1BEF.XZ"},
            {"?\??.?\??123.*","ABC.DEF123.XYZ"},{"1234?\?5678.*","1234DE5678.XYZ"},{"1234/?/5678.?987*","1234E5678.X987"},
            {"123?.?","123..X"},{"?\?123.?4","AB123.X4"},{"12?\?34.5?","12C.34.5Y"},
            {"12345678*.*9","12345678.XY9"},{"*12345678.9*8","12345678.9Y8"},{"1234*5678.9*87","12345678.987"},
            {"1234?\??\??\??\?.7?\??\??*","1234DEF.7YZ"},{"?\??\??\??\??123.?\??\??4*","ABC.DEF123.XYZ4"},
            {"12?\??\??\??\?34.5?\??\?6*","12C.DEF34.5YZ6"},{"?\?123?\??\?456?\?.*","AB123EF456.XYZ"},
            {"1234////////.*","1234.XYZ"},{"////////1234.*","1234.XYZ"},{"////1234////.*","1234.XYZ"},{"1?\?2//?\??\?3.*","1BC2F3.XYZ"},
            {"12?\??3456*.*","12C.D3456.XYZ"},{"*12?\??3456.*","12C.D3456.XYZ"},{"12?\??3*456.*","12C.D3456.XYZ"},
            {"?1*2?\??\??.*","A12.DEF.XYZ"},{"?123*456?.*","A123456.XYZ"},{"?12*34?\??.*","A1234EF.XYZ"},
            {"?1234*5678?.9*876","A12345678.9876"},{"12///3456*.*","123456.XYZ"},{"*12///3456.*","123456.XYZ"},{"//123*456/.*","123456.XYZ"},
            {"<12>*.<34>*","12ABC.DEF.34XYZ"},{"<12>*","12ABC.DEF"},{"*.<12>","ABC.DEF.12"},{"*<12>.*<34>","ABC.DEF12.XYZ34"},
            {"<12>?\?.<34>?","12AB.34X"},{"?\?<12>.?\?<34>","AB12.XY34"},{"?<12>?.?<3>?","A12B.X3Y"},
            {"*<12>?\?.*<3>?","ABC.D12EF.XY3Z"},{"?\?<12>*.?<3>*","AB12C.DEF.X3YZ"},
            {"<12*34>.<5?6*>","12ABC.DEF34.5X6YZ"},{"<*12>.<*3>","ABC.DEF12.XYZ3"},
            {"<?1?\?2*3?\?>.<4?5>","A1BC2.D3EF.4X5"},{"<?12/?34?/5>*.<?/6?>","A12C34.5EF.X6Z"},
            {"*.*<.123>","ABC.DEF.XYZ.123"},{"1*.*<.234>","1BC.DEF.XYZ.234"},{"*.*<1.2>","ABC.DEF.XYZ1.2"},
            {"*.<1.2>*","ABC.DEF.1.2XYZ"},{"*.?\?<1.2>*","ABC.DEF.XY1.2Z"},{"*12?.?<.34>*","ABC.12F.X.34YZ"}
        };
        QString error;
        for(const auto &example:examples){QCOMPARE(copyName("ABC.DEF.XYZ",example.first,CopyCase::Keep,&error),example.second);QVERIFY2(error.isEmpty(),qPrintable(error));}
        QCOMPARE(copyName(".htaccess","*.*",CopyCase::Keep,&error),QString(".htaccess"));
        QCOMPARE(copyName("README","*.*",CopyCase::Keep,&error),QString("README"));
        QCOMPARE(copyName("área.PHP","<backup_>*.*",CopyCase::Upper,&error),QString::fromUtf8("BACKUP_ÁREA.PHP"));
        for(const auto &mask:QStringList{"*123*.*","*.*.123","1*.23*.*","*<12.*","<..>","*:001:.*","*|*","<a"}){
            copyName("ABC.DEF.XYZ",mask,CopyCase::Keep,&error);QVERIFY2(!error.isEmpty(),qPrintable(mask));
        }
    }
    void copyPlanPathModesAndCollisions()
    {
        write(panel+"/modulos/ordenes.php");Session session(panel);session.apply(scan(panel,true),true);session.enter(View::Branch);
        const auto files=session.files();const QString destination=root+"/copies";
        const auto relative=planCopy(files,panel,destination,CopyPaths::Relative,"*.*",CopyCase::Keep);
        QVERIFY(relative.error.isEmpty());QCOMPARE(relative.entries.size(),files.size());
        for(const auto &entry:relative.entries)QCOMPARE(entry.target,destination+'/'+QDir(panel).relativeFilePath(entry.source));
        const auto current=planCopy(files,panel,destination,CopyPaths::Current,"*.bak",CopyCase::Keep);
        QVERIFY(current.error.isEmpty());for(const auto &entry:current.entries)QVERIFY(entry.target.startsWith(destination+"/sublimepanel/"));
        const auto full=planCopy(files,panel,destination,CopyPaths::Full,"*.*",CopyCase::Keep);
        for(const auto &entry:full.entries)QCOMPARE(entry.target,destination+entry.source);
        QVERIFY(!planCopy(files,panel,destination,CopyPaths::Relative,"fixed.txt",CopyCase::Keep).error.isEmpty());
        QVERIFY(!planCopy(files,panel,panel,CopyPaths::Relative,"*.*",CopyCase::Keep).error.isEmpty());
        QVERIFY(!planCopy(files,panel+"/modulos",destination,CopyPaths::Relative,"*.*",CopyCase::Keep).error.isEmpty());
        QVERIFY(!QFileInfo::exists(destination));
    }
    void copyEngineDataMetadataAndReplacement()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);const QString source=panel+"/ordenes.php",target=root+"/copies/deep/file.bin";
        const QByteArray bytes=QByteArray::fromHex("00010203fffe")+QByteArray(400000,'x');write(source,bytes);
        QVERIFY(QFile::setPermissions(source,QFile::ReadOwner));
        {QFile file(source);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.setFileTime(QDateTime::fromSecsSinceEpoch(1600000000),QFileDevice::FileModificationTime));}
        const auto copied=copyFile(root,{source,target},CopyReplace::Ask,cancel);QVERIFY2(copied.copied,qPrintable(copied.error));
        {QFile file(target);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),bytes);}
        QCOMPARE(QFileInfo(target).lastModified(),QFileInfo(source).lastModified());QCOMPARE(QFile::permissions(target),QFile::permissions(source));
        QVERIFY(copyFile(root,{source,target},CopyReplace::Ask,cancel).exists);
        QVERIFY(copyFile(root,{source,target},CopyReplace::Never,cancel).skipped);
        QVERIFY(copyFile(root,{source,target},CopyReplace::Older,cancel).skipped);
        {QFile file(target);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.setFileTime(QDateTime::fromSecsSinceEpoch(1500000000),QFileDevice::FileModificationTime));}
        QVERIFY(copyFile(root,{source,target},CopyReplace::Older,cancel).copied);
        QVERIFY(copyFile(root,{source,target},CopyReplace::Always,cancel).copied);
        const auto renamed=copyFile(root,{source,target},CopyReplace::Rename,cancel);QVERIFY(renamed.copied);QCOMPARE(renamed.target,root+"/copies/deep/file(2).bin");
        QVERIFY(QDir(root+"/copies/deep").entryList({".ltree-copy-*"},QDir::Files|QDir::Hidden).isEmpty());
        const QString absent=root+"/cancel/no/file";cancel->store(true);
        QVERIFY(copyFile(root,{source,absent},CopyReplace::Always,cancel).cancelled);QVERIFY(!QFileInfo::exists(root+"/cancel"));
    }
    void copyEngineRefusesLinksSelfAndErrors()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);const QString source=panel+"/ordenes.php",target=root+"/target.php";
        write(target,"KEEP");QVERIFY(QFile::link(source,root+"/source-link"));
        QVERIFY(!copyFile(root,{root+"/source-link",target},CopyReplace::Always,cancel).error.isEmpty());
        QVERIFY(QFile::link(target,root+"/target-link"));QVERIFY(!copyFile(root,{source,root+"/target-link"},CopyReplace::Always,cancel).error.isEmpty());
        QVERIFY(QFile::link(panel,root+"/dir-link"));QVERIFY(!copyFile(root,{source,root+"/dir-link/new"},CopyReplace::Always,cancel).error.isEmpty());
        QVERIFY(!copyFile(root,{source,source},CopyReplace::Always,cancel).error.isEmpty());
        QVERIFY(::link(QFile::encodeName(source).constData(),QFile::encodeName(root+"/hard-link").constData())==0);
        QVERIFY(!copyFile(root,{source,root+"/hard-link"},CopyReplace::Always,cancel).error.isEmpty());
        {QFile file(target);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("KEEP"));}
        QVERIFY(QDir().mkdir(root+"/locked"));QVERIFY(QFile::setPermissions(root+"/locked",QFile::ReadOwner|QFile::ExeOwner));
        const auto denied=copyFile(root,{source,root+"/locked/copy"},CopyReplace::Ask,cancel);
        QVERIFY(QFile::setPermissions(root+"/locked",QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));QVERIFY(!denied.error.isEmpty());
        QVERIFY(QDir(root+"/locked").entryList(QDir::Files|QDir::Hidden).isEmpty());
        QVERIFY(!copyFile(root,{source+"/../ordenes.php",target},CopyReplace::Always,cancel).error.isEmpty());
    }
    void altCopyBranchMaskSplitAndDestination()
    {
        write(panel+"/modulos/ordenes.php");QVERIFY(QDir().mkdir(panel+"/empty"));const QString destination=root+"/target";QVERIFY(QDir().mkdir(destination));
        TreeWindow window(panel);window.resize(1280,768);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClicks(&window,"*");QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto tags=window.session().tags;const auto selected=window.session().currentFile()->path;
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);QVERIFY(window.selectRoot(destination));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);capture(window,"alt-copy-mask");
        QTest::keyClicks(&window,"*123*.*");QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.status().contains("asterisk"));
        QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);QTest::keyClicks(&window,"<backup_>*.*");QTest::keyClick(&window,Qt::Key_Return);capture(window,"alt-copy-destination");
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_R);capture(window,"alt-copy-paths-relative");QTest::keyClick(&window,Qt::Key_Return);capture(window,"alt-copy-replace");
        QTest::keyClick(&window,Qt::Key_V);
        const bool copied=QTest::qWaitFor([&]{return window.status().contains("Copy completed");},5000);
        if(!copied)capture(window,"alt-copy-failure");
        QVERIFY2(copied,qPrintable(window.status()));
        QCOMPARE(window.session().tags,tags);QCOMPARE(window.session().currentFile()->path,selected);QCOMPARE(window.session().view,View::Branch);
        for(const auto &path:tags){const QString parent=QDir(panel).relativeFilePath(QFileInfo(path).absolutePath());
            QVERIFY(QFileInfo::exists(destination+'/'+(parent=="."?QString{}:parent+'/')+"backup_"+QFileInfo(path).fileName()));}
        QVERIFY(!QFileInfo::exists(destination+"/empty"));
        QVERIFY(window.oppositeSession()->directories().contains(destination+"/modulos"));
        QVERIFY(!window.oppositeSession()->directories().value(destination).files.isEmpty());capture(window,"alt-copy-branch-completed");
        QTest::keyClick(&window,Qt::Key_Tab);
        QVERIFY(!window.session().directories().value(destination).files.isEmpty());
        QVERIFY(!window.session().directories().value(destination+"/modulos").files.isEmpty());
        QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_R);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_V);
        capture(window,"alt-copy-history-pending");
        const bool reused=QTest::qWaitFor([&]{return window.status().contains("Copy completed");},5000);
        if(!reused)capture(window,"alt-copy-history-failure");
        QVERIFY2(reused,qPrintable(window.status()));QVERIFY(window.status().contains("0/5"));
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(window.session().tags,tags);
    }
    void altCopyReplaceAllCaseAndRenameSeq()
    {
        const QString source=root+"/source",target=root+"/target";QVERIFY(QDir().mkdir(source));QVERIFY(QDir().mkdir(target));
        write(source+"/A.TXT","NEW A");write(source+"/B.TXT","NEW B");write(target+"/a.txt","OLD A");write(target+"/b.txt","OLD B");
        TreeWindow window(source);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClicks(&window,target);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_R);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_N);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_A);QTRY_VERIFY(window.status().contains("Copy completed"));
        QVERIFY(window.status().contains("2/2"));
        for(const auto &name:QStringList{"a","b"}){QFile file(target+'/'+name+".txt");QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),("NEW "+name.toUpper()).toUtf8());}
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_R);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_R);QTRY_VERIFY(window.status().contains("Copy completed"));
        QVERIFY(QFileInfo::exists(target+"/a(2).txt"));QVERIFY(QFileInfo::exists(target+"/b(2).txt"));QCOMPARE(window.session().tags,tags);
    }
    void altCopyConfirmationReplacementAndErrors()
    {
        const QString source=root+"/copy-ui",destination=root+"/new-target";QVERIFY(QDir().mkdir(source));write(source+"/a.txt","NEW A");write(source+"/b.txt","NEW B");
        TreeWindow window(source);window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClicks(&window,destination);QTest::keyClick(&window,Qt::Key_Return);capture(window,"alt-copy-new-directory-80");
        QVERIFY(!QFileInfo::exists(destination));QTest::keyClick(&window,Qt::Key_Y);QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_V);
        capture(window,"alt-copy-confirm-each-80");QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(window.status().contains("Copy completed"));
        QVERIFY(!QFileInfo::exists(destination+"/copy-ui/a.txt"));QVERIFY(QFileInfo::exists(destination+"/copy-ui/b.txt"));QVERIFY(window.status().contains("1/2"));
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_C);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_N);QTRY_VERIFY(!window.busy());
        capture(window,"alt-copy-existing-file-80");QTest::keyClick(&window,Qt::Key_N);QTRY_VERIFY(window.status().contains("Copy completed"));QVERIFY(window.status().contains("1/2"));
        QVERIFY(QFile::remove(source+"/a.txt"));
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_R);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());
        capture(window,"alt-copy-error-80");QTest::keyClick(&window,Qt::Key_S);QTRY_VERIFY(window.status().contains("Copy completed"));
        QVERIFY(window.status().contains("first error"));QVERIFY(QFileInfo::exists(destination+"/b.txt"));
    }
    void altCopyVisibleTagsAndCancelledWorker()
    {
        const QString destination=root+"/filtered-target";QVERIFY(QDir().mkdir(destination));
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());QTest::keyClicks(&window,"*");QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto tags=window.session().tags;QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.php");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClicks(&window,destination);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_R);QTest::keyClick(&window,Qt::Key_Return);
        auto *pool=QThreadPool::globalInstance();const int threads=pool->maxThreadCount();pool->setMaxThreadCount(1);
        QSemaphore release;auto blocker=QtConcurrent::run(pool,[&]{release.acquire();});
        const auto cleanup=qScopeGuard([&]{release.release();blocker.waitForFinished();pool->setMaxThreadCount(threads);});
        QTest::keyClick(&window,Qt::Key_V);QVERIFY(window.busy());QTest::keyClick(&window,Qt::Key_Escape);release.release();blocker.waitForFinished();QTRY_VERIFY(!window.busy());
        QVERIFY(window.status().contains("Copy cancelled"));QCOMPARE(window.session().tags,tags);QVERIFY(QDir(destination).entryList(QDir::Files).isEmpty());
        QTest::keyClick(&window,Qt::Key_C,Qt::AltModifier);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClicks(&window,destination);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_R);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_V);QTRY_VERIFY(window.status().contains("Copy completed"));
        QVERIFY(!QFileInfo::exists(destination+"/.htaccess"));QVERIFY(QFileInfo::exists(destination+"/modulos/interno.php"));QCOMPARE(window.session().tags,tags);
    }
    void commandKeyColors()
    {
        TreeWindow window(panel);window.resize(1280,768);window.show();QTRY_VERIFY(!window.busy());
        const auto colorAt=[&](int column,int row,const QColor &color){
            const auto image=window.grab().toImage();const auto ratio=image.devicePixelRatio();
            const QRect cell(qRound(column*8*ratio),qRound(row*16*ratio),qRound(8*ratio),qRound(16*ratio));
            for(int y=cell.top();y<=cell.bottom();++y)for(int x=cell.left();x<=cell.right();++x)
                if(image.pixelColor(x,y)==color)return true;
            return false;
        };
        const QColor key(255,255,0),body(0,255,255);
        const int first=window.rows()-4,second=window.rows()-3,footer=window.rows()-2;
        QVERIFY(colorAt(10,first,key));QVERIFY(colorAt(11,first,body));QVERIFY(!colorAt(11,first,key));
        const QString tree="Avail  Branch  Compare  Del  Filespec  Global  sHortcut  Log  Make  Rename  Showall  Tag  Untag  eXecute  Quit";
        const int execute=10+int(tree.indexOf("eXecute"));
        QVERIFY(colorAt(execute,first,body));QVERIFY(colorAt(execute+1,first,key));QVERIFY(colorAt(execute+2,first,body));
        QVERIFY(colorAt(10,second,key));QVERIFY(colorAt(11,second,key));QVERIFY(colorAt(13,second,body));
        capture(window,"usability-tree-hotkeys");
        QTest::keyClick(&window,Qt::Key_Return);
        const QString files="Attributes  Copy  Del  Edit  Filespec  JFC  Log  Move  Open  Rename  Tag  Untag  View  eXecute  Quit";
        const int jfc=10+int(files.indexOf("JFC"));
        QVERIFY(colorAt(jfc,first,key));QVERIFY(colorAt(jfc+1,first,body));QVERIFY(colorAt(jfc+2,first,body));
        QTest::keyPress(&window,Qt::Key_Control);QVERIFY(colorAt(10,first,key));QVERIFY(colorAt(11,first,body));
        QTest::keyRelease(&window,Qt::Key_Control);QTest::keyPress(&window,Qt::Key_Alt);
        QVERIFY(colorAt(10,first,key));QVERIFY(colorAt(11,first,body));QTest::keyRelease(&window,Qt::Key_Alt);
        QTest::keyClick(&window,Qt::Key_D);QVERIFY(colorAt(0,footer,key));QVERIFY(colorAt(11,footer,body));
        capture(window,"usability-delete-hotkeys");QTest::keyClick(&window,Qt::Key_N);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);
        QVERIFY(colorAt(0,second,key));QVERIFY(colorAt(3,second,body));QVERIFY(colorAt(0,footer,key));
        capture(window,"usability-search-hotkeys");QTest::keyClick(&window,Qt::Key_Escape);
        window.resize(640,400);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_M);
        capture(window,"usability-make-hotkeys-80");QTest::keyClick(&window,Qt::Key_Escape);
    }
    void deleteKeepsScrolledPosition()
    {
        for(const bool directory:{false,true})for(const bool split:{false,true}){
            const QString base=root+QString("/position-%1-%2").arg(directory).arg(split);QVERIFY(QDir().mkpath(base));
            for(int i=0;i<60;++i){
                const QString path=base+QString("/entry_%1").arg(i,2,10,QChar('0'));
                if(directory)QVERIFY(QDir().mkdir(path));else write(path+".txt");
            }
            TreeWindow window(base);window.resize(split?1280:640,400);window.show();QTRY_VERIFY(!window.busy());
            if(!directory)QTest::keyClick(&window,Qt::Key_Return);
            for(int i=0;i<40+(directory?1:0);++i)QTest::keyClick(&window,Qt::Key_Down);
            if(split)QTest::keyClick(&window,Qt::Key_F8);
            const auto selectedRow=[&]{
                const auto image=window.grab().toImage();const auto ratio=image.devicePixelRatio();
                for(int y=2;y<window.rows()-5;++y)
                    if(image.pixelColor(qRound(((split?window.activeSide()*window.width()/2:0)+16)*ratio),qRound((y*16+15)*ratio))==QColor(192,192,192))return y;
                return -1;
            };
            const int before=selectedRow();QVERIFY(before>2);
            const QString removed=base+"/entry_40"+(directory?"":".txt");
            QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_N);
            QCOMPARE(selectedRow(),before);QVERIFY(QFileInfo::exists(removed));
            QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());
            QVERIFY(!QFileInfo::exists(removed));QCOMPARE(selectedRow(),before);
            const QString next=base+"/entry_41"+(directory?"":".txt");
            if(directory)QCOMPARE(window.session().directory,next);else QCOMPARE(window.session().currentFile()->path,next);
            if(split){
                if(directory)QCOMPARE(window.oppositeSession()->directory,next);else QCOMPARE(window.oppositeSession()->currentFile()->path,next);
                QTest::keyClick(&window,Qt::Key_Tab);QCOMPARE(selectedRow(),before);
            }
            capture(window,QString("usability-delete-position-%1-%2").arg(directory).arg(split));
            QTest::keyClick(&window,Qt::Key_End);const int lastRow=selectedRow();
            QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());
            QCOMPARE(selectedRow(),lastRow-1);
            const QString previous=base+"/entry_58"+(directory?"":".txt");
            if(directory)QCOMPARE(window.session().directory,previous);else QCOMPARE(window.session().currentFile()->path,previous);
        }
    }
    void branchDeleteFullFlowAndDirectoryConfirmation()
    {
        for(const bool removeDirectory:{false,true}){
            const QString base=root+(removeDirectory?"/branch-yes":"/branch-no");
            QVERIFY(QDir().mkpath(base+"/nested/deep"));write(base+"/.hidden");write(base+"/nested/one.txt");write(base+"/nested/deep/two.txt");
            TreeWindow window(base);window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());
            QTest::keyClicks(&window,"*");QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_B);
            QCOMPARE(window.session().view,View::Branch);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
            QCOMPARE(window.session().tags.size(),3);QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);
            QTest::keyClick(&window,Qt::Key_Y);capture(window,"branch-delete-confirm-each-80");
            QVERIFY(QFileInfo::exists(base+"/.hidden"));QVERIFY(!window.busy());
            QTest::keyClick(&window,Qt::Key_N);QTRY_VERIFY(!window.busy());
            QCOMPARE(window.session().view,View::Tree);QCOMPARE(window.session().directory,base);QCOMPARE(window.session().root,root);
            QVERIFY(QFileInfo::exists(base+"/nested/deep"));QVERIFY(!QFileInfo::exists(base+"/.hidden"));QVERIFY(window.session().tags.isEmpty());
            capture(window,"branch-delete-directory-80");
            QTest::keyClick(&window,removeDirectory?Qt::Key_Y:Qt::Key_N);QTRY_VERIFY(!window.busy());
            QCOMPARE(QFileInfo::exists(base),!removeDirectory);
            QVERIFY(QFileInfo::exists(root+"/README.txt"));
            if(removeDirectory)QCOMPARE(window.session().directory,root+"/branch-no");
        }
    }
    void branchDeleteBulkFailuresContinueWithoutDirectoryPrompt()
    {
        const QString base=root+"/branch-failure";QVERIFY(QDir().mkpath(base+"/locked"));
        write(base+"/a.txt");write(base+"/z.txt");write(base+"/locked/keep.txt");
        TreeWindow window(base);window.show();QTRY_VERIFY(!window.busy());QTest::keyClicks(&window,"*");QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QVERIFY(QFile::setPermissions(base+"/locked",QFile::ReadOwner|QFile::ExeOwner));
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window);
        QTRY_VERIFY(window.status().contains("Deletion completed"));
        QVERIFY(QFile::setPermissions(base+"/locked",QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        QVERIFY(window.status().contains("2/3"));QVERIFY(window.status().contains("1 skipped"));QVERIFY(window.status().contains("delete error"));
        QVERIFY(!QFileInfo::exists(base+"/a.txt"));QVERIFY(!QFileInfo::exists(base+"/z.txt"));QVERIFY(QFileInfo::exists(base+"/locked/keep.txt"));
        QCOMPARE(window.session().view,View::Branch);QCOMPARE(window.session().tags,QSet<QString>{base+"/locked/keep.txt"});
        capture(window,"branch-delete-failure-retained");QTest::keyClick(&window,Qt::Key_Y);QVERIFY(QFileInfo::exists(base));
    }
    void branchDeleteIndividualSkipAndChoiceCancellation()
    {
        const QString base=root+"/branch-each";QVERIFY(QDir().mkpath(base));
        for(const auto &name:QStringList{"a.txt","b.txt","c.txt"})write(base+'/'+name);
        TreeWindow window(base);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Y);QTest::keyClick(&window,Qt::Key_Escape);
        QVERIFY(window.status().contains("Deletion cancelled"));QCOMPARE(window.session().tags.size(),3);QVERIFY(QFileInfo::exists(base+"/a.txt"));
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window,true);
        capture(window,"branch-delete-individual");QTest::keyClick(&window,Qt::Key_N);
        QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());QVERIFY(!QFileInfo::exists(base+"/b.txt"));QVERIFY(QFileInfo::exists(base+"/c.txt"));
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(window.status().contains("Deletion completed"));
        QVERIFY(window.status().contains("2/3"));QVERIFY(QFileInfo::exists(base+"/a.txt"));QVERIFY(!QFileInfo::exists(base+"/c.txt"));
        QCOMPARE(window.session().view,View::Branch);QCOMPARE(window.session().tags,QSet<QString>{base+"/a.txt"});
    }
    void branchDeleteLateFileAndUnloadedContent()
    {
        const QString base=root+"/branch-late";QVERIFY(QDir().mkpath(base+"/sub/deep"));write(base+"/one.txt");
        TreeWindow window(base);window.show();QTRY_VERIFY(!window.busy());QTest::keyClicks(&window,"*");QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window);QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().view,View::Tree);write(base+"/sub/late.txt");
        QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("still contains files"));
        QVERIFY(QFileInfo::exists(base+"/sub/late.txt"));QVERIFY(QFileInfo::exists(base+"/sub/deep"));
        QVERIFY(!window.session().directories().value(base+"/sub").files.isEmpty());
        const QString other=root+"/branch-unloaded";QVERIFY(QDir().mkpath(other));write(other+"/one.txt");
        QVERIFY(window.selectRoot(other));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QVERIFY(QDir().mkpath(other+"/hidden-sub"));write(other+"/hidden-sub/keep.txt");
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window);
        QTRY_VERIFY(window.status().contains("Deletion completed"));QVERIFY(window.status().contains("directory retained"));
        QCOMPARE(window.session().view,View::Branch);QVERIFY(QFileInfo::exists(other+"/hidden-sub/keep.txt"));
    }
    void branchDeleteSplitAndDirectoryCheckCancellation()
    {
        const QString base=root+"/target";QVERIFY(QDir().mkpath(base+"/sub"));write(base+"/one.txt");write(base+"/sub/two.txt");
        TreeWindow window(root);window.show();QTRY_VERIFY(!window.busy());QTest::keyClicks(&window,"*");QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_T,Qt::ShiftModifier);QCOMPARE(window.session().directory,base);
        QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window);QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().view,View::Tree);QTest::keyClick(&window,Qt::Key_Tab);QCOMPARE(window.activeSide(),0);
        QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());QVERIFY(!QFileInfo::exists(base));
        QCOMPARE(window.session().directory,root+"/beta/vacio");QCOMPARE(window.oppositeSession()->directory,root);
        QVERIFY(!window.oppositeSession()->directories().contains(base));QVERIFY(QFileInfo::exists(root+"/README.txt"));
        const QString cancelled=root+"/check-cancel";QVERIFY(QDir().mkpath(cancelled));write(cancelled+"/one.txt");
        QVERIFY(window.selectRoot(cancelled));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        auto *pool=QThreadPool::globalInstance();const int threads=pool->maxThreadCount();pool->setMaxThreadCount(1);
        QSemaphore release;QFuture<void> blocker;
        const auto cleanup=qScopeGuard([&]{release.release();blocker.waitForFinished();pool->setMaxThreadCount(threads);});
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window);
        // La comprobación espera detrás de este trabajo, para cancelar sin depender de la velocidad del disco.
        blocker=QtConcurrent::run(pool,[&]{release.acquire();});
        QTRY_VERIFY(window.status().contains("Logging branch"));QTest::keyClick(&window,Qt::Key_Escape);
        release.release();blocker.waitForFinished();QTRY_VERIFY(!window.busy());
        QVERIFY(QFileInfo::exists(cancelled));QVERIFY(window.status().contains("directory check cancelled"));
        QCOMPARE(window.session().view,View::Branch);
    }
    void emptyBranchEngineValidationAndPartialFailure()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        const QString base=root+"/empty-branch";QVERIFY(QDir().mkpath(base+"/sub/deep"));
        QVERIFY(!deleteEmptyBranch(root,root,cancel).deleted);QVERIFY(!deleteEmptyBranch(root,root+"/../outside",cancel).deleted);
        write(base+"/.hidden");const auto nonempty=deleteEmptyBranch(root,base,cancel);QVERIFY(!nonempty.deleted);QVERIFY(QFileInfo::exists(base+"/sub/deep"));
        QVERIFY(QFile::remove(base+"/.hidden"));QVERIFY(QFile::link(root+"/beta",base+"/link"));
        QVERIFY(!deleteEmptyBranch(root,base,cancel).deleted);QVERIFY(QFileInfo::exists(root+"/beta/externo.php"));QVERIFY(QFile::remove(base+"/link"));
        cancel->store(true);QVERIFY(deleteEmptyBranch(root,base,cancel).cancelled);QVERIFY(QFileInfo::exists(base+"/sub/deep"));cancel->store(false);
        QVERIFY(QFile::setPermissions(base,QFile::ReadOwner|QFile::ExeOwner));const auto partial=deleteEmptyBranch(root,base,cancel);
        QVERIFY(QFile::setPermissions(base,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        QVERIFY(!partial.deleted);QVERIFY(!partial.error.isEmpty());QVERIFY(partial.removedDirectories.contains(base+"/sub/deep"));
        QVERIFY(!QFileInfo::exists(base+"/sub/deep"));QVERIFY(QFileInfo::exists(base+"/sub"));
        const auto done=deleteEmptyBranch(root,base,cancel);QVERIFY2(done.deleted,qPrintable(done.error));QVERIFY(!QFileInfo::exists(base));
    }
    void emptyInitialDirectoryEnterThenDelete()
    {
        for (const bool alreadyTree : {false, true}) {
            const QString base = root + (alreadyTree ? "/empty-tree" : "/empty-files");
            QVERIFY(QDir().mkpath(base)); write(base + "/one.txt");
            TreeWindow window(base); window.show(); QTRY_VERIFY(!window.busy());
            QTest::keyClick(&window, Qt::Key_Return);
            QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
            QTest::keyClick(&window, Qt::Key_D, Qt::ControlModifier);
            confirmTaggedDelete(window);
            QTRY_VERIFY(window.status().contains("Deletion completed"));
            QVERIFY(window.session().files().isEmpty());
            if (alreadyTree) QTest::keyClick(&window, Qt::Key_Escape);
            QTest::keyClick(&window, Qt::Key_Return); QTRY_VERIFY(!window.busy());
            QCOMPARE(window.session().view, View::Tree);
            QCOMPARE(window.session().root, root); QCOMPARE(window.session().directory, base);
            QTest::keyClick(&window, Qt::Key_D); capture(window, "empty-initial-directory-confirm");
            QTest::keyClick(&window, Qt::Key_N); QVERIFY(QFileInfo::exists(base));
            QTest::keyClick(&window, Qt::Key_D); QTest::keyClick(&window, Qt::Key_Return);
            QTRY_VERIFY(!window.busy()); QVERIFY(!QFileInfo::exists(base));
            QCOMPARE(window.session().directory, root+"/beta");
        }
    }
    void deleteInitialDirectoryPrepareCancelAndPermissions()
    {
        const QString base = root + "/initial-empty"; QVERIFY(QDir().mkpath(base));
        TreeWindow window(base); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_D); QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY(!window.busy()); QCOMPARE(window.session().root, base);
        QTest::keyClick(&window, Qt::Key_Return); QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().directory, base); QVERIFY(QFileInfo::exists(base));
        QVERIFY(window.selectRoot(base)); QTRY_VERIFY(!window.busy());
        const auto permissions = QFile::permissions(root);
        QVERIFY(QFile::setPermissions(root, QFile::ReadOwner));
        QTest::keyClick(&window, Qt::Key_D); QTRY_VERIFY(!window.busy());
        QVERIFY(QFile::setPermissions(root, permissions));
        QCOMPARE(window.session().root, base); QVERIFY(window.status().contains("Access denied"));
        QTest::keyClick(&window, Qt::Key_Return); QTRY_VERIFY(!window.busy());
        QVERIFY(QFileInfo::exists(base));
        QVERIFY(window.selectRoot(base)); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_D); QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().root, root); QCOMPARE(window.session().directory, base);
        QTest::keyClick(&window, Qt::Key_Return); QTRY_VERIFY(!window.busy());
        QVERIFY(!QFileInfo::exists(base));
        TreeWindow filesystem("/"); filesystem.show(); QTRY_VERIFY(!filesystem.busy());
        QTest::keyClick(&filesystem, Qt::Key_D);
        QVERIFY(!filesystem.busy()); QVERIFY(filesystem.status().contains("filesystem root"));
        QCOMPARE(filesystem.session().root, QString("/"));
    }
    void deleteAllThenParentAndEmptyDirectory()
    {
        const QString base = root + "/delete-all";
        QVERIFY(QDir().mkpath(base));
        write(base + "/a.txt"); write(base + "/b.txt");
        TreeWindow window(base); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_D, Qt::ControlModifier);
        confirmTaggedDelete(window);
        QTRY_VERIFY(window.status().contains("Deletion completed"));
        QVERIFY(window.session().files().isEmpty());
        QCOMPARE(window.session().view, View::Directory);
        QTest::keyClick(&window, Qt::Key_Left);
        QCOMPARE(window.session().view, View::Tree);
        QCOMPARE(window.session().directory, base);
        QTest::keyClick(&window, Qt::Key_Backspace); QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().root, root); QCOMPARE(window.session().directory, root);
        for (int i = 0; i < window.session().tree().size() && window.session().directory != base; ++i)
            QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(window.session().directory, base);
        capture(window, "delete-all-parent-tree");
        QTest::keyClick(&window, Qt::Key_D); QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY(!window.busy()); QVERIFY(!QFileInfo::exists(base));
        QCOMPARE(window.session().directory, root+"/beta");
        QVERIFY(QFileInfo::exists(root + "/README.txt"));
    }
    void parentExpansionPreservesLoadedTreeAndSplit()
    {
        TreeWindow window(root + "/beta"); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClicks(&window, "*"); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_F); QTest::keyClicks(&window, "*.php"); QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_F8);
        const auto tags = window.session().tags;
        QTest::keyClick(&window, Qt::Key_Backspace); QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().root, root); QCOMPARE(window.session().directory, root);
        QCOMPARE(window.session().tags, tags); QCOMPARE(window.session().filespec.text(), QString("*.php"));
        QVERIFY(window.session().directories().value(root + "/beta").loaded);
        QVERIFY(window.session().directories().value(root + "/beta/vacio").loaded);
        QCOMPARE(window.oppositeSession()->root, root + "/beta");
        QCOMPARE(window.oppositeSession()->view, View::Directory);
        QCOMPARE(window.oppositeSession()->currentFile()->path, root + "/beta/externo.php");
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.session().root, root + "/beta"); QCOMPARE(window.session().tags, tags);
    }
    void parentExpansionFailureCancelAndFilesystemRoot()
    {
        Session session(panel); session.apply(scan(panel, true), true);
        session.tags.insert(panel + "/ordenes.php"); session.enter(View::Directory);
        const auto tags = session.tags;
        auto cancelled = scan(QFileInfo(panel).absolutePath()); cancelled.cancelled = true;
        QVERIFY(!session.expandToParent(cancelled));
        QVERIFY(!session.expandToParent(scan(root)));
        QCOMPARE(session.root, panel); QCOMPARE(session.tags, tags); QCOMPARE(session.view, View::Directory);
        const QString parent = QFileInfo(panel).absolutePath();
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        const auto permissions = QFile::permissions(parent);
        QVERIFY(QFile::setPermissions(parent, QFile::ReadOwner));
        QTest::keyClick(&window, Qt::Key_Backspace); QTRY_VERIFY(!window.busy());
        QVERIFY(QFile::setPermissions(parent, permissions));
        QCOMPARE(window.session().root, panel); QCOMPARE(window.session().directory, panel);
        QVERIFY(window.session().currentDirectory().loaded); QVERIFY(window.status().contains("Access denied"));
        Session filesystem("/"); QVERIFY(!filesystem.expandToParent(scan(root)));
        filesystem.parent(); QCOMPARE(filesystem.root, QString("/")); QCOMPARE(filesystem.directory, QString("/"));
    }
    void deleteTaggedConfirmScopeAndSplit()
    {
        TreeWindow window(panel);window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClicks(&window,"*");QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.php");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_U,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Tab);
        const auto hidden=QSet<QString>{panel+"/.htaccess",panel+"/modulos/interno.php"};
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);capture(window,"delete-tagged-confirm-80");
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_F8);QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint(500,100));
        QCOMPARE(window.activeSide(),0);QVERIFY(window.isSplit());QTest::keyClick(&window,Qt::Key_N);QVERIFY(QFileInfo::exists(panel+"/ordenes.php"));
        QTest::keyClick(&window,Qt::Key_Delete,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(QFileInfo::exists(panel+"/usuarios.php"));
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window);
        QTRY_VERIFY(window.status().contains("Deletion completed"));QVERIFY(window.status().contains("2/2"));QVERIFY(!window.busy());
        QVERIFY(!QFileInfo::exists(panel+"/ordenes.php"));QVERIFY(!QFileInfo::exists(panel+"/usuarios.php"));
        for(const auto &path:hidden)QVERIFY(QFileInfo::exists(path));QCOMPARE(window.session().tags,hidden);QCOMPARE(window.oppositeSession()->tags,hidden);
        QCOMPARE(window.session().view,View::Directory);QCOMPARE(window.session().directory,panel);QCOMPARE(window.session().filespec.text(),QString("*.php"));
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);QVERIFY(window.status().contains("No tagged files"));capture(window,"delete-tagged-finished-80");
    }
    void deleteTaggedBranchShowallAndTagsOnly()
    {
        for(const auto view:QList<View>{View::Branch,View::Showall}){
            const QString base=root+'/'+(view==View::Branch?"batch-branch":"batch-showall");
            QVERIFY(QDir().mkpath(base+"/sub"));write(base+"/keep.txt");write(base+"/one.php");write(base+"/sub/two.php");
            TreeWindow window(base);window.show();QTRY_VERIFY(!window.busy());QTest::keyClicks(&window,"*");QTRY_VERIFY(!window.busy());
            QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,view==View::Branch?Qt::Key_B:Qt::Key_S);
            QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.php");QTest::keyClick(&window,Qt::Key_Return);
            QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window);QTRY_VERIFY(window.status().contains("Deletion completed"));
            QCOMPARE(window.session().view,view);QCOMPARE(window.session().directory,base);QCOMPARE(window.session().tags,QSet<QString>{base+"/keep.txt"});
            QVERIFY(QFileInfo::exists(base+"/keep.txt"));QVERIFY(!QFileInfo::exists(base+"/one.php"));QVERIFY(!QFileInfo::exists(base+"/sub/two.php"));
        }
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Down);QTest::keyClick(&window,Qt::Key_T);QTest::keyClick(&window,Qt::Key_T);QTest::keyClick(&window,Qt::Key_Home);
        QTest::keyClick(&window,Qt::Key_Return,Qt::ControlModifier);QVERIFY(window.session().tagsOnly);
        QTest::keyClick(&window,Qt::Key_Delete,Qt::ControlModifier);confirmTaggedDelete(window);QTRY_VERIFY(window.status().contains("Deletion completed"));
        QVERIFY(window.session().tags.isEmpty());QVERIFY(QFileInfo::exists(panel+"/.htaccess"));QCOMPARE(window.session().currentFile()->path,panel+"/.htaccess");
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);QVERIFY(window.status().contains("No tagged files"));
    }
    void deleteTaggedErrorRetrySkipAndCancel()
    {
        const QString base=root+"/batch-errors";QVERIFY(QDir().mkpath(base));
        const auto a=base+"/a.txt",b=base+"/b.txt",c=base+"/c.txt",d=base+"/d.txt";for(const auto &path:QStringList{a,b,c,d})write(path);
        TreeWindow window(base);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QVERIFY(QFile::remove(b));QVERIFY(QDir().mkdir(b));
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window,true);
        QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Y);
        QTRY_VERIFY(!window.busy());QVERIFY(!QFileInfo::exists(a));QVERIFY(QFileInfo(b).isDir());QVERIFY(QFileInfo::exists(c));capture(window,"delete-tagged-error");
        QTest::keyClick(&window,Qt::Key_R);QTRY_VERIFY(!window.busy());QVERIFY(QFileInfo::exists(c));
        QVERIFY(QDir().rmdir(b));write(b);QVERIFY(QFile::remove(c));QVERIFY(QDir().mkdir(c));
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY(!QFileInfo::exists(b));QVERIFY(QFileInfo(c).isDir());QVERIFY(QFileInfo::exists(d));
        QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_S);QTest::keyClick(&window,Qt::Key_Y);
        QTRY_VERIFY(window.status().contains("Deletion completed"));QVERIFY(window.status().contains("3/4"));QVERIFY(window.status().contains("1 skipped"));QVERIFY(!QFileInfo::exists(d));
        const auto e=base+"/e.txt",f=base+"/f.txt",g=base+"/g.txt";for(const auto &path:QStringList{e,f,g})write(path);
        QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QVERIFY(QFile::remove(f));QVERIFY(QDir().mkdir(f));QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window,true);
        QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Y);
        QTRY_VERIFY(!window.busy());QVERIFY(!QFileInfo::exists(e));QTest::keyClick(&window,Qt::Key_Escape);
        QVERIFY(window.status().contains("Deletion cancelled"));QVERIFY(QFileInfo::exists(g));QVERIFY(window.session().tags.contains(g));
    }
    void deleteTaggedPermissionsAndBusyCancel()
    {
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QVERIFY(QFile::setPermissions(panel,QFile::ReadOwner|QFile::ExeOwner));QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(window);
        QTRY_VERIFY(!window.busy());
        QVERIFY(QFile::setPermissions(panel,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        QCOMPARE(window.session().tags.size(),3);QVERIFY(QFileInfo::exists(panel+"/.htaccess"));
        QVERIFY(window.status().contains("0/3"));
        const QString base=root+"/batch-cancel";QVERIFY(QDir().mkpath(base));for(int i=0;i<30;++i)write(base+QString("/%1.txt").arg(i,2,10,QChar('0')));
        TreeWindow cancelWindow(base);cancelWindow.show();QTRY_VERIFY(!cancelWindow.busy());QTest::keyClick(&cancelWindow,Qt::Key_Return);QTest::keyClick(&cancelWindow,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&cancelWindow,Qt::Key_D,Qt::ControlModifier);confirmTaggedDelete(cancelWindow);QTest::keyClick(&cancelWindow,Qt::Key_Escape);
        QTRY_VERIFY(cancelWindow.status().contains("Deletion cancelled"));QVERIFY(!cancelWindow.busy());
        const auto remaining=QDir(base).entryList(QDir::Files|QDir::NoDotAndDotDot);QVERIFY(remaining.size()>=29);QCOMPARE(cancelWindow.session().tags.size(),remaining.size());capture(cancelWindow,"delete-tagged-cancel");
    }
    void deleteFileNamesLinksAndReadOnly()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        const QString name=root+"/ñandú $() ' ;.txt";write(name,"original");
        const auto deleted=deleteFile(root,name,cancel);QVERIFY2(deleted.error.isEmpty(),qPrintable(deleted.error));
        QVERIFY(deleted.deleted);QVERIFY(!deleted.directory);QVERIFY(!QFileInfo::exists(name));
        const QString target=root+"/README.txt",link=root+"/link.txt",missing=root+"/dangling.txt";
        QVERIFY(QFile::link(target,link));QVERIFY(deleteFile(root,link,cancel).deleted);QVERIFY(!QFileInfo(link).isSymLink());
        QFile original(target);QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(original.readAll(),QByteArray("ordenes\n"));original.close();
        QVERIFY(QFile::link(root+"/absent",missing));QVERIFY(QFileInfo(missing).isSymLink());QVERIFY(deleteFile(root,missing,cancel).deleted);QVERIFY(!QFileInfo(missing).isSymLink());
        QVERIFY(QFile::setPermissions(target,QFile::ReadOwner));QVERIFY(deleteFile(root,target,cancel).deleted);QVERIFY(!QFileInfo::exists(target));
    }
    void deleteFileValidationPermissionsAndModel()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        for(const auto &path:QStringList{root,root+"/beta/vacio",root+"/missing",root+"/../outside",QString::fromLatin1(QByteArray("bad\0name",8))}){
            const auto result=deleteFile(root,path,cancel);QVERIFY(!result.deleted);QVERIFY(!result.error.isEmpty());
        }
        QVERIFY(QFile::link(root+"/beta",root+"/linked-parent"));QVERIFY(!deleteFile(root,root+"/linked-parent/externo.php",cancel).deleted);QVERIFY(QFileInfo::exists(root+"/beta/externo.php"));
        const QString parent=root+"/locked",path=parent+"/one.txt",other=parent+"/two.txt";
        QVERIFY(QDir().mkpath(parent));write(path);write(other);
        Session session(root);session.apply(scan(root,true),true);session.selectDirectory(parent);session.enter(View::Directory);session.tags={path,other};
        cancel->store(true);QVERIFY(deleteFile(root,path,cancel).cancelled);QVERIFY(QFileInfo::exists(path));cancel->store(false);
        QVERIFY(QFile::setPermissions(parent,QFile::ReadOwner|QFile::ExeOwner));const auto denied=deleteFile(root,path,cancel);
        QVERIFY(QFile::setPermissions(parent,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));QVERIFY(!denied.deleted);QVERIFY(QFileInfo::exists(path));
        QVERIFY(QFile::setPermissions(parent,QFile::WriteOwner|QFile::ExeOwner));const auto result=deleteFile(root,path,cancel);
        QVERIFY(QFile::setPermissions(parent,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));QVERIFY(result.deleted);QVERIFY(!result.scan.directories.first().error.isEmpty());
        session.removeFile(path);session.apply(result.scan,true);QCOMPARE(session.tags,QSet<QString>{other});QCOMPARE(session.files().size(),1);QCOMPARE(session.currentFile()->path,other);
    }
    void deleteFilePromptAndSplitSelection()
    {
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);const auto tags=window.session().tags;QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_Down);const auto selected=window.session().currentFile()->path;QCOMPARE(selected,panel+"/ordenes.php");
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(QFileInfo::exists(selected));
        QTest::keyClick(&window,Qt::Key_D);capture(window,"delete-file-prompt");
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(window.session().currentFile()->path,selected);QCOMPARE(window.activeSide(),0);QVERIFY(window.isSplit());
        QTest::keyClick(&window,Qt::Key_N);QVERIFY(QFileInfo::exists(selected));
        QTest::keyClick(&window,Qt::Key_Delete);QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(QFileInfo::exists(selected));
        QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY(!QFileInfo::exists(selected));
        auto remaining=tags;remaining.remove(selected);QCOMPARE(window.session().tags,remaining);QCOMPARE(window.oppositeSession()->tags,remaining);
        QCOMPARE(window.session().directory,panel);QCOMPARE(window.session().view,View::Directory);QCOMPARE(window.session().currentFile()->path,panel+"/usuarios.php");
        QCOMPARE(window.oppositeSession()->currentFile()->path,panel+"/.htaccess");capture(window,"delete-file-split");
        QTest::keyClick(&window,Qt::Key_Delete);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());QCOMPARE(window.session().currentFile()->path,panel+"/.htaccess");
        QVERIFY(QFileInfo::exists(panel+"/.htaccess"));
    }
    void deleteFileBranchShowallAndFilter()
    {
        for(const auto view:QList<View>{View::Branch,View::Showall}){
            const auto name=QString(view==View::Branch?"branch.php":"showall.php");const QString target=panel+"/modulos/"+name,keep=panel+'/'+name;
            write(target);write(keep);
            TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());QTest::keyClicks(&window,"*");QTRY_VERIFY(!window.busy());
            QTest::keyClick(&window,view==View::Branch?Qt::Key_B:Qt::Key_S);QCOMPARE(window.session().view,view);
            QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.php");QTest::keyClick(&window,Qt::Key_Return);
            QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
            int index=-1;for(int i=0;i<window.session().files().size();++i)if(window.session().files()[i].path==target)index=i;
            QVERIFY(index>=0);QTest::keyClick(&window,Qt::Key_Home);for(int i=0;i<index;++i)QTest::keyClick(&window,Qt::Key_Down);
            QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());
            QVERIFY(!QFileInfo::exists(target));QVERIFY(QFileInfo::exists(keep));QCOMPARE(window.session().directory,panel);QCOMPARE(window.session().view,view);
            QCOMPARE(window.session().filespec.text(),QString("*.php"));QVERIFY(window.session().tags.contains(keep));QVERIFY(!window.session().tags.contains(target));
            for(const auto &entry:window.session().files())QVERIFY(entry.path!=target);
        }
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Down);QTest::keyClick(&window,Qt::Key_T);QTest::keyClick(&window,Qt::Key_T);QTest::keyClick(&window,Qt::Key_Home);
        QTest::keyClick(&window,Qt::Key_Return,Qt::ControlModifier);QVERIFY(window.session().tagsOnly);
        const auto selected=window.session().currentFile()->path;QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY(!QFileInfo::exists(selected));QVERIFY(window.session().tagsOnly);QCOMPARE(window.session().files().size(),1);QVERIFY(window.session().tags.contains(window.session().currentFile()->path));
        capture(window,"delete-file-tags-only");
    }
    void deleteFileDisappearedChangedAndLastAutoview()
    {
        TreeWindow window(root);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);const auto path=root+"/README.txt";
        QVERIFY(QFile::remove(path));QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("Cannot delete"));
        QVERIFY(QDir().mkdir(path));QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("Cannot delete"));QVERIFY(QFileInfo(path).isDir());
        QVERIFY(QDir().rmdir(path));write(path);QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_F7);auto preview=window.findChild<FileViewer *>("autoview");QVERIFY(preview);QTRY_VERIFY(!preview->loading());
        QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());QVERIFY(!QFileInfo::exists(path));QVERIFY(!window.session().currentFile());
        QTest::keyClick(&window,Qt::Key_D);QVERIFY(window.status().contains("No file"));QVERIFY(QFileInfo::exists(root+"/beta/externo.php"));capture(window,"delete-last-file");
    }
    void deleteEmptyDirectoryAndModel()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        const QString path=root+"/beta/vacio";
        Session session(root);session.apply(scan(root,true),true);session.tags.insert(panel+"/ordenes.php");session.selectDirectory(path);
        const auto result=deleteEmptyDirectory(root,path,cancel);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(result.deleted);QVERIFY(!QFileInfo::exists(path));
        session.removeDirectory(path);session.apply(result.scan,true);
        QCOMPARE(session.directory,root+"/beta");QVERIFY(!session.directories().contains(path));QVERIFY(session.tags.contains(panel+"/ordenes.php"));
        QVERIFY(!deleteEmptyDirectory(root,path,cancel).error.isEmpty());
    }
    void deleteRefusesContentRootsLinksAndCancellation()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        for(const auto &path:QStringList{root,alpha,root+"/README.txt",root+"/../outside"}){
            const auto result=deleteEmptyDirectory(root,path,cancel);QVERIFY(!result.error.isEmpty());QVERIFY(!result.deleted);
        }
        const QString hidden=root+"/hidden",child=root+"/child",target=root+"/beta/vacio";
        QVERIFY(QDir().mkpath(hidden));write(hidden+"/.secret");QVERIFY(QDir().mkpath(child+"/sub"));
        for(const auto &path:QStringList{hidden,child})QVERIFY(deleteEmptyDirectory(root,path,cancel).error.contains("not empty"));
        QVERIFY(QFile::link(target,root+"/link"));QVERIFY(!deleteEmptyDirectory(root,root+"/link",cancel).deleted);QVERIFY(QFileInfo(root+"/link").isSymLink());
        QVERIFY(QFile::link(root+"/beta",root+"/linked-parent"));QVERIFY(!deleteEmptyDirectory(root,root+"/linked-parent/vacio",cancel).deleted);QVERIFY(QFileInfo::exists(target));
        cancel->store(true);const auto stopped=deleteEmptyDirectory(root,target,cancel);QVERIFY(stopped.cancelled);QVERIFY(!stopped.deleted);QVERIFY(QFileInfo::exists(target));
        QFile secret(hidden+"/.secret");QVERIFY(secret.open(QIODevice::ReadOnly));QCOMPARE(secret.readAll(),QByteArray("ordenes\n"));
    }
    void deletePermissionsAndUnreadableParent()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        const QString parent=root+"/locked",path=parent+"/empty";
        QVERIFY(QDir().mkpath(path));Session session(root);session.apply(scan(root,true),true);session.selectDirectory(path);
        QVERIFY(QFile::setPermissions(parent,QFile::ReadOwner|QFile::ExeOwner));
        const auto denied=deleteEmptyDirectory(root,path,cancel);
        QVERIFY(QFile::setPermissions(parent,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        QVERIFY(!denied.deleted);QVERIFY(!denied.error.isEmpty());QVERIFY(QFileInfo::exists(path));
        QVERIFY(QFile::setPermissions(parent,QFile::WriteOwner|QFile::ExeOwner));
        const auto deleted=deleteEmptyDirectory(root,path,cancel);
        QVERIFY(QFile::setPermissions(parent,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        QVERIFY(deleted.deleted);QVERIFY(!deleted.scan.directories.isEmpty());QVERIFY(!deleted.scan.directories.first().error.isEmpty());
        session.removeDirectory(path);session.apply(deleted.scan,true);QVERIFY(!session.directories().contains(path));QCOMPARE(session.directory,parent);
    }
    void deletePromptSplitAndReturn()
    {
        TreeWindow window(root);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClicks(&window,"*");QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_End);const QString empty=root+"/beta/vacio";QCOMPARE(window.session().directory,empty);
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_End);QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_D);capture(window,"delete-empty-prompt");
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_F8);QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint(900,100));
        QCOMPARE(window.activeSide(),0);QVERIFY(window.isSplit());QTest::keyClick(&window,Qt::Key_N);QVERIFY(QFileInfo::exists(empty));
        QTest::keyClick(&window,Qt::Key_Delete);QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(QFileInfo::exists(empty));
        QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY(!QFileInfo::exists(empty));QCOMPARE(window.session().directory,root+"/beta");QCOMPARE(window.oppositeSession()->directory,root+"/beta");
        QVERIFY(!window.session().directories().contains(empty));QVERIFY(!window.oppositeSession()->directories().contains(empty));
        QCOMPARE(window.session().tags,tags);QCOMPARE(window.oppositeSession()->tags,tags);capture(window,"delete-empty-split");
    }
    void deleteUnloadedDirectoryAndContext()
    {
        TreeWindow window(root);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_D);QTRY_VERIFY(!window.busy());QVERIFY(QFileInfo::exists(root));
        QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(window.selectRoot(root));QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_A,Qt::ShiftModifier);QCOMPARE(window.session().directory,alpha);QVERIFY(!window.session().currentDirectory().loaded);
        QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());
        QVERIFY(window.status().contains("not empty"));QCOMPARE(window.session().directory,alpha);QVERIFY(QFileInfo::exists(panel+"/ordenes.php"));
        const auto created=makeDirectory(root,"empty",std::make_shared<std::atomic_bool>(false));QVERIFY(created.error.isEmpty());
        QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_F3);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_E,Qt::ShiftModifier);QCOMPARE(window.session().directory,root+"/empty");QVERIFY(!window.session().currentDirectory().loaded);
        QTest::keyClick(&window,Qt::Key_Delete);QTest::keyClick(&window,Qt::Key_Y);QTRY_VERIFY(!window.busy());QVERIFY(!QFileInfo::exists(root+"/empty"));
        QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Directory);
        QTest::keyClick(&window,Qt::Key_D);QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(QFileInfo::exists(root+"/README.txt"));
    }
    void blockCursorsInTreePrompts()
    {
        TreeWindow window(panel);window.resize(640,400);window.show();QTRY_VERIFY(!window.busy());
        const int end=window.rows()-5;
        const auto block=[&](int x,int y){
            const auto rendered=window.grab().toImage();const auto ratio=rendered.devicePixelRatio();
            QCOMPARE(rendered.pixelColor(qRound(x*8*ratio),qRound(y*16*ratio)),QColor(192,192,192));
        };
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"pet_eco");QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_Right);
        block(11,end+1);capture(window,"cursor-filespec");QTest::keyClick(&window,Qt::Key_End);block(17,end+1);
        QTest::keyClicks(&window,QString(100,'a'));block(79,end+1);QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_Bar);QTest::keyClicks(&window,"mod");block(17,end+3);capture(window,"cursor-spell");QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_L);QTest::keyClicks(&window,"/tmp/example");block(28,window.rows()-3);capture(window,"cursor-log");QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClicks(&window,"pet_eco");
        QTest::keyClick(&window,Qt::Key_Home);for(int i=0;i<3;++i)QTest::keyClick(&window,Qt::Key_Right);
        const int label=QString("SEARCH %1 tagged files for: ").arg(window.session().tags.size()).size();
        block(label+3,end+1);capture(window,"cursor-content-search");
        const auto before=window.grab().toImage();QTest::keyClick(&window,Qt::Key_Right);block(label+4,end+1);
        const auto after=window.grab().toImage();const auto ratio=before.devicePixelRatio();
        QCOMPARE(before.copy(qRound((label+5)*8*ratio),qRound((end+1)*16*ratio),qRound(16*ratio),qRound(16*ratio)),after.copy(qRound((label+5)*8*ratio),qRound((end+1)*16*ratio),qRound(16*ratio),qRound(16*ratio)));
        QTest::keyClick(&window,Qt::Key_End);QTest::keyClicks(&window,QString(100,'a'));block(79,end+1);capture(window,"cursor-search-long");
        QTest::keyClick(&window,Qt::Key_Escape);
    }
    void blockCursorsInViewerPrompts()
    {
        FileViewer viewer(panel+"/ordenes.php");viewer.resize(640,400);viewer.show();QTRY_VERIFY(!viewer.loading());
        const auto block=[&](int x){
            const auto rendered=viewer.grab().toImage();const auto ratio=rendered.devicePixelRatio();
            QCOMPARE(rendered.pixelColor(qRound(x*8*ratio),qRound(23*16*ratio)),QColor(192,192,192));
        };
        QTest::keyClick(&viewer,Qt::Key_F);QTest::keyClicks(&viewer,"pet_eco");block(23);
        const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/cursor-viewer-search.png"));
        QTest::keyClicks(&viewer,QString(100,'a'));block(79);QTest::keyClick(&viewer,Qt::Key_Escape);
        QTest::keyClick(&viewer,Qt::Key_O);QTest::keyClicks(&viewer,"123");block(15);QTest::keyClick(&viewer,Qt::Key_Escape);
        QTest::keyClick(&viewer,Qt::Key_H);QTest::keyClick(&viewer,Qt::Key_O);QTest::keyClicks(&viewer,"1A");block(16);
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/cursor-viewer-offset.png"));
    }
    void makeDirectoriesAndUpdateModel()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        Session session(root);session.apply(scan(root,true),true);session.tags.insert(panel+"/ordenes.php");
        const QString name=" carpeta $() ' ; á/child\\literal";
        const auto result=makeDirectory(alpha,name,cancel);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QCOMPARE(result.created.size(),2);QVERIFY(QFileInfo(result.path).isDir());
        QCOMPARE(result.path,alpha+'/'+name);session.apply(result.scan,true);
        QVERIFY(session.directories().contains(result.path));QVERIFY(session.directories().value(result.path).loaded);
        QVERIFY(session.tags.contains(panel+"/ordenes.php"));QVERIFY(session.directories().value(panel).loaded);
        const auto deeper=makeDirectory(alpha,name+"/more",cancel);QVERIFY(deeper.error.isEmpty());QCOMPARE(deeper.created.size(),1);
        QVERIFY(QFileInfo(deeper.path).isDir());
    }
    void makeValidationAndExistingNames()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        const auto before=QDir(root).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot);
        for(const auto &name:QStringList{"",".","..","new/../wrong","new/./wrong",root+"/absolute",QString::fromLatin1(QByteArray("bad\0name",8))}){
            const auto result=makeDirectory(root,name,cancel);QVERIFY(!result.error.isEmpty());QVERIFY(result.created.isEmpty());
        }
        QCOMPARE(QDir(root).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot),before);
        for(const auto &name:QStringList{"alpha","README.txt","README.txt/child"}){
            const auto result=makeDirectory(root,name,cancel);QVERIFY(result.error.contains("already exists"));QVERIFY(result.created.isEmpty());
        }
        QFile file(root+"/README.txt");QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("ordenes\n"));
        QVERIFY(!makeDirectory(root+"/missing","child",cancel).error.isEmpty());
    }
    void makeErrorsPartialCreationAndCancellation()
    {
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        const auto partial=makeDirectory(root,"partial/"+QString(300,'x'),cancel);
        QVERIFY(!partial.error.isEmpty());QCOMPARE(partial.created,QStringList{root+"/partial"});
        Session session(root);session.apply(scan(root),true);session.apply(partial.scan,true);QVERIFY(session.directories().contains(root+"/partial"));
        QVERIFY(QFile::link(alpha,root+"/shortcut"));
        QVERIFY(!makeDirectory(root,"shortcut/new",cancel).error.isEmpty());QVERIFY(!QFileInfo::exists(alpha+"/new"));
        const auto locked=root+"/locked";QVERIFY(QDir().mkpath(locked));
        QVERIFY(QFile::setPermissions(locked,QFile::ReadOwner|QFile::ExeOwner));
        const auto denied=makeDirectory(locked,"new",cancel);
        QVERIFY(QFile::setPermissions(locked,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        QVERIFY(!denied.error.isEmpty());QVERIFY(denied.created.isEmpty());
        cancel->store(true);const auto stopped=makeDirectory(root,"cancelled",cancel);
        QVERIFY(stopped.cancelled);QVERIFY(stopped.created.isEmpty());QVERIFY(!QFileInfo::exists(root+"/cancelled"));
    }
    void makeTreeJumpAndHistory()
    {
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Home);const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_M);QTest::keyClicks(&window,"docs/sub");capture(window,"make-prompt");QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(!window.busy());QVERIFY(QFileInfo(panel+"/docs/sub").isDir());QCOMPARE(window.session().directory,panel);
        QVERIFY(window.session().directories().contains(panel+"/docs/sub"));QCOMPARE(window.session().tags,tags);
        QTest::keyClick(&window,Qt::Key_M);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClicks(&window,"jump");QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(!window.busy());QCOMPARE(window.session().directory,panel+"/jump");QCOMPARE(window.session().tags,tags);
        QTest::keyClick(&window,Qt::Key_M);QTest::keyClicks(&window,"nested");QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(!window.busy());QCOMPARE(window.session().directory,panel+"/jump/nested");
        QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_M);QTest::keyClick(&window,Qt::Key_F12);
        QTest::keyClick(&window,Qt::Key_F3);QTest::keyClick(&window,Qt::Key_Up);QTest::keyClick(&window,Qt::Key_Down);QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(!window.busy());QVERIFY(QFileInfo(panel+"/nested").isDir());QCOMPARE(window.session().directory,panel);capture(window,"make-created");
    }
    void makeEditingErrorsAndCancel()
    {
        TreeWindow window(root);window.show();QTRY_VERIFY(!window.busy());window.resize(640,400);
        QTest::keyClick(&window,Qt::Key_M);QTest::keyClicks(&window,"cancelled");QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(!QFileInfo::exists(root+"/cancelled"));
        QTest::keyClick(&window,Qt::Key_M);QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("name"));
        QTest::keyClicks(&window,"alpha");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("already exists"));
        QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);QTest::keyClicks(&window,"newXfolder");QTest::keyClick(&window,Qt::Key_Home);
        for(int i=0;i<3;++i)QTest::keyClick(&window,Qt::Key_Right);
        capture(window,"make-cursor-80");QTest::keyClick(&window,Qt::Key_Delete);QTest::keyClick(&window,Qt::Key_End);QTest::keyClicks(&window,"z");QTest::keyClick(&window,Qt::Key_Backspace);
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY(QFileInfo(root+"/newfolder").isDir());
        QTest::keyClick(&window,Qt::Key_M);QTest::keyClicks(&window,QString(120,'a'));QTest::keyClick(&window,Qt::Key_Home);
        capture(window,"make-long-name-80");QTest::keyClick(&window,Qt::Key_End);QTest::keyClick(&window,Qt::Key_Escape);
        const QString name="ñandú $() ' ;";QTest::keyClick(&window,Qt::Key_M);
        QKeyEvent unicode(QEvent::KeyPress,Qt::Key_unknown,Qt::NoModifier,name);QApplication::sendEvent(&window,&unicode);QTest::keyClick(&window,Qt::Key_Return);
        QTRY_VERIFY(!window.busy());QVERIFY(QFileInfo(root+'/'+name).isDir());
    }
    void makeSplitUnloadedDirectoryAndContext()
    {
        TreeWindow window(root);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_U,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Tab);
        const auto tags=window.session().tags;QTest::keyClick(&window,Qt::Key_A,Qt::ShiftModifier);QCOMPARE(window.session().directory,alpha);QVERIFY(!window.session().currentDirectory().loaded);
        QTest::keyClick(&window,Qt::Key_M);QTest::keyClicks(&window,"made/child");
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_F8);QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,QPoint(900,80));
        QCOMPARE(window.activeSide(),0);QVERIFY(window.isSplit());QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().directory,alpha);QCOMPARE(window.session().tags,tags);QVERIFY(QFileInfo(alpha+"/made/child").isDir());
        QVERIFY(window.oppositeSession()->tags.isEmpty());QVERIFY(window.oppositeSession()->directories().contains(alpha+"/made/child"));
        QTest::keyClick(&window,Qt::Key_Tab);QCOMPARE(window.session().directory,root);QVERIFY(window.session().tags.isEmpty());capture(window,"make-split");
        QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Directory);
        QTest::keyClick(&window,Qt::Key_M);QTest::keyClick(&window,Qt::Key_Escape);
        QVERIFY(window.status().contains("Move cancelled"));QCOMPARE(window.session().view,View::Directory);
    }
    void executeTerminalCommandsAndTty()
    {
        const auto directory=root+"/shell $() ' ; space";QVERIFY(QDir().mkpath(directory));
        ExecuteWindow window(directory,"/bin/bash",{"--noprofile","--norc","-i"});window.resize(1280,768);window.show();
        auto *terminal=window.terminal();QSignalSpy output(terminal,&QTermWidget::receivedData);QSignalSpy closed(&window,&ExecuteWindow::closed);
        QTRY_VERIFY(terminal->getShellPID()>0);QTRY_VERIFY(terminal->screenColumnsCount()>10);
        QCOMPARE(terminal->getTerminalFont().family(),QString("LTree Console"));
        const QFontMetrics metrics(terminal->getTerminalFont());QCOMPARE(metrics.height(),16);
        for(int code=32;code<127;++code)QCOMPARE(metrics.horizontalAdvance(QChar(code)),8);
        ConsoleFont bitmap;
        for(int code=32;code<127;++code){
            QImage expected(8,16,QImage::Format_RGB32),actual(8,16,QImage::Format_RGB32);expected.fill(Qt::black);actual.fill(Qt::black);
            {QPainter painter(&expected);QVERIFY(bitmap.draw(painter,0,0,QString(QChar(code)),Qt::white));}
            {QPainter painter(&actual);painter.setFont(terminal->getTerminalFont());painter.setPen(Qt::white);painter.drawText(0,13,QString(QChar(code)));}
            QVERIFY2(actual==expected,qPrintable(QString("Glifo distinto: %1").arg(code)));
        }
        terminal->sendText("printf '\nCWD=%s\nHISTORY=%s\n' \"$PWD\" \"$HISTFILE\"; if test -t 0 && test -t 1; then printf '__%s__\n' REAL_TTY; fi; printf 'pipeline' | tr '[:lower:]' '[:upper:]' > result.txt; cat result.txt; printf '\n__%s__\n' COMMAND_DONE\n");
        QTRY_VERIFY(terminalOutput(output).contains("__COMMAND_DONE__"));
        QVERIFY(terminalOutput(output).contains("CWD="+directory));QVERIFY(terminalOutput(output).contains("__REAL_TTY__"));
        QVERIFY(terminalOutput(output).contains("HISTORY="+root+"/shell-history"));
        QVERIFY(terminalOutput(output).contains("PIPELINE"));
        QFile result(directory+"/result.txt");QVERIFY(result.open(QIODevice::ReadOnly));QCOMPARE(result.readAll(),QByteArray("PIPELINE"));
        const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if(!dir.isEmpty()){QVERIFY(QDir().mkpath(dir));QTest::qWait(100);QVERIFY(window.grab().save(dir+"/execute-wide.png"));}
        const int before=terminal->screenColumnsCount();window.resize(640,400);QTRY_VERIFY(terminal->screenColumnsCount()<before);
        terminal->sendText("printf '\nSIZE=%s\n' \"$COLUMNS\"\n");QTRY_VERIFY(terminalOutput(output).contains("SIZE="+QString::number(terminal->screenColumnsCount())));
        if(!dir.isEmpty()){QTest::qWait(100);QVERIFY(window.grab().save(dir+"/execute-80.png"));}
        terminal->sendText("exit\n");QTRY_COMPARE(closed.size(),1);
    }
    void executeForegroundKeysAndReturn()
    {
        ExecuteWindow window(root,"/bin/bash",{"--noprofile","--norc","-i"});window.resize(1280,768);window.show();
        auto *terminal=window.terminal();QSignalSpy output(terminal,&QTermWidget::receivedData);QSignalSpy closed(&window,&ExecuteWindow::closed);
        terminal->sendText("/bin/bash --noprofile --norc -c 'printf \"\\n__%s__\\n\" ESC_INPUT; IFS= read -r -n 1 key; printf \"\\n__%s__\\n\" ESC_DONE'\n");
        QTRY_VERIFY(terminalOutput(output).contains("__ESC_INPUT__"));
        QTRY_VERIFY(terminal->getForegroundProcessId()!=terminal->getShellPID());
        auto *input=terminal->focusProxy();QVERIFY(input);
        QTest::keyClick(input,Qt::Key_Escape);QTRY_VERIFY(terminalOutput(output).contains("__ESC_DONE__"));QCOMPARE(closed.size(),0);
        terminal->sendText("/usr/bin/sleep 30\n");QTRY_VERIFY(terminal->getForegroundProcessId()!=terminal->getShellPID());
        QTest::keyClick(input,Qt::Key_C,Qt::ControlModifier);
        QTRY_COMPARE(terminal->getForegroundProcessId(),terminal->getShellPID());QCOMPARE(closed.size(),0);
        QTest::keyClick(input,Qt::Key_Escape);QCOMPARE(closed.size(),1);
    }
    void executeFromTreeRefreshesAndPreservesState()
    {
        TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_T);QTest::keyClick(&window,Qt::Key_Home);
        const auto tags=window.session().tags;const auto directory=window.session().directory;
        QTest::keyClick(&window,Qt::Key_X);
        QPointer<ExecuteWindow> execution=window.findChild<ExecuteWindow*>();QVERIFY(execution);QCOMPARE(execution->directory(),directory);
        auto *terminal=execution->terminal();QSignalSpy output(terminal,&QTermWidget::receivedData);
        terminal->sendText("printf 'new file' > new_file.txt; printf '\n__%s__\n' REFRESH_READY\n");
        QTRY_VERIFY(terminalOutput(output).contains("__REFRESH_READY__"));terminal->sendText("exit\n");
        QTRY_VERIFY(execution.isNull());QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().view,View::Tree);QCOMPARE(window.session().directory,directory);QCOMPARE(window.session().tags,tags);
        bool found=false;for(const auto &file:window.session().currentDirectory().files)if(file.name=="new_file.txt")found=true;
        QVERIFY(found);capture(window,"execute-return-tree");
    }
    void executeFromShowallAndSplit()
    {
        TreeWindow window(root);window.show();QTRY_VERIFY(!window.busy());window.load(true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_S);QTest::keyClick(&window,Qt::Key_F);
        QTest::keyClicks(&window,"ordenes.php");QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().files().size(),1);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_U,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Tab);
        const auto selected=window.session().currentFile()->path;const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_X);
        QPointer<ExecuteWindow> execution=window.findChild<ExecuteWindow*>();QVERIFY(execution);QCOMPARE(execution->directory(),panel);
        window.resize(640,400);QCOMPARE(execution->size(),window.size());
        auto *terminal=execution->terminal();QSignalSpy output(terminal,&QTermWidget::receivedData);
        terminal->sendText("printf '\nCWD=%s\n' \"$PWD\"; printf '\n__%s__\n' SPLIT_READY\n");
        QTRY_VERIFY(terminalOutput(output).contains("__SPLIT_READY__"));QVERIFY(terminalOutput(output).contains("CWD="+panel));
        QTRY_COMPARE(terminal->getForegroundProcessId(),terminal->getShellPID());
        QTest::keyClick(terminal->focusProxy(),Qt::Key_Escape);QTRY_VERIFY(execution.isNull());QTRY_VERIFY(!window.busy());
        QVERIFY(window.isSplit());QCOMPARE(window.activeSide(),0);QCOMPARE(window.session().view,View::Showall);
        QCOMPARE(window.session().tags,tags);QVERIFY(window.oppositeSession()->tags.isEmpty());
        QCOMPARE(window.session().currentFile()->path,selected);QCOMPARE(window.session().filespec.text(),QString("ordenes.php"));
    }
    void executeMissingDirectory()
    {
        const auto directory=root+"/gone";QVERIFY(QDir().mkpath(directory));
        TreeWindow window(directory);window.show();QTRY_VERIFY(!window.busy());QVERIFY(QDir().rmdir(directory));
        QTest::keyClick(&window,Qt::Key_X);QVERIFY(!window.findChild<ExecuteWindow*>());QVERIFY(window.status().contains("no longer exists"));
    }
    void executeAutoviewRefreshesAndKeepsKeys()
    {
        write(panel+"/shell.txt","before\n");TreeWindow window(panel);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"shell.txt");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_F7);auto *preview=window.findChild<FileViewer*>("autoview");QVERIFY(preview);QTRY_VERIFY(!preview->loading());
        QTest::keyClick(&window,Qt::Key_X);QPointer<ExecuteWindow> execution=window.findChild<ExecuteWindow*>();QVERIFY(execution);QVERIFY(!preview->isVisible());
        QTest::keyClick(&window,Qt::Key_F8);QVERIFY(!window.isSplit());
        auto *terminal=execution->terminal();QSignalSpy output(terminal,&QTermWidget::receivedData);
        terminal->sendText("printf 'after' > shell.txt; printf '\n__%s__\n' PREVIEW_READY\n");QTRY_VERIFY(terminalOutput(output).contains("__PREVIEW_READY__"));
        terminal->sendText("exit\n");QTRY_VERIFY(execution.isNull());QTRY_VERIFY(!window.busy());QTRY_VERIFY(!preview->loading());QVERIFY(preview->isVisible());
        QTest::keyClick(&window,Qt::Key_F,Qt::ShiftModifier);QTest::keyClicks(&window,"after");QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(preview->hit(),0);
    }
    void compareOptionsNormalizationAndLines()
    {
        const auto a=root+"/first.txt",b=root+"/second.txt";
        const QByteArray first="ANCHOR\r\n\tAlpha\t  Beta \r\n \t\r\nend\r\n",second="anchor\n Alpha Beta \n\nEND\n";
        write(a,first);write(b,second);auto cancel=std::make_shared<std::atomic_bool>(false);
        const auto original=compareFiles(a,b,cancel);QVERIFY(original.error.isEmpty());QVERIFY(!original.changes.isEmpty());
        CompareOptions options;options.caseSensitive=false;options.compressWhitespace=true;options.suppressEmpty=true;
        const auto result=compareCharacters(original,options,cancel);QVERIFY(result.textError.isEmpty());QVERIFY(result.changes.isEmpty());
        QCOMPARE(result.first,QStringList({"ANCHOR"," Alpha Beta ","end"}));
        QCOMPARE(result.second,QStringList({"anchor"," Alpha Beta ","END"}));
        QCOMPARE(result.firstLineNumbers,QVector<int>({1,2,4}));QCOMPARE(result.secondLineNumbers,QVector<int>({1,2,4}));
        QCOMPARE(result.firstBytes,first);QCOMPARE(result.secondBytes,second);QCOMPARE(result.differentBytes,original.differentBytes);
        QVERIFY(!result.bytesEqual);QCOMPARE(original.first.size(),4);
        write(a,"a b\n");write(b,"ab\n");QVERIFY(!compareFiles(a,b,cancel,options).changes.isEmpty());
        write(a," a \n");write(b,"a\n");QVERIFY(!compareFiles(a,b,cancel,options).changes.isEmpty());
        write(a,"\n \t\n\n");write(b,"\t\n");
        const auto empty=compareFiles(a,b,cancel,options);QVERIFY(empty.rows.isEmpty());QVERIFY(empty.changes.isEmpty());QVERIFY(!empty.bytesEqual);
        write(a,QString("\n\u00a0\n").toUtf8());write(b,"\n");
        const auto nonbreaking=compareFiles(a,b,cancel,options);QCOMPARE(nonbreaking.firstLineNumbers,QVector<int>{2});
        QVERIFY(!nonbreaking.changes.isEmpty());
        cancel->store(true);QVERIFY(compareCharacters(original,options,cancel).cancelled);
    }
    void compareCaseUnicodeAndRawBytes()
    {
        const auto a=root+"/first.txt",b=root+"/second.txt";
        auto cancel=std::make_shared<std::atomic_bool>(false);CompareOptions options;options.caseSensitive=false;
        write(a,QString::fromUtf8("Ärbol ÁÉ\n").toUtf8());write(b,QString::fromUtf8("äRBOL áé\n").toUtf8());
        const auto folded=compareFiles(a,b,cancel,options);QVERIFY(folded.changes.isEmpty());QVERIFY(!folded.rawCharacters);
        QVERIFY(!compareFiles(a,b,cancel).changes.isEmpty());
        write(a,QByteArray("A\0Z",3));write(b,QByteArray("a\0z",3));
        auto raw=compareFiles(a,b,cancel,options);QVERIFY(raw.rawCharacters);QVERIFY(raw.changes.isEmpty());QCOMPARE(raw.differentBytes,2);
        write(a,QByteArray::fromHex("c400"));write(b,QByteArray::fromHex("e400"));
        raw=compareFiles(a,b,cancel,options);QVERIFY(raw.rawCharacters);QVERIFY(!raw.changes.isEmpty());QCOMPARE(raw.differentBytes,1);
    }
    void compareOptionsToggleWithoutRereading()
    {
        const auto a=root+"/first.txt",b=root+"/second.txt";
        const QByteArray first="ANCHOR\nAlpha\t  Beta\n \t\nend\n",second="anchor\nalpha Beta\n\nEND\n";
        write(a,first);write(b,second);
        CompareWindow viewer(a,b);viewer.resize(1280,768);viewer.show();QTRY_VERIFY(!viewer.loading());
        const auto initialChanges=viewer.result().changes;const auto difference=viewer.result().differentBytes;
        QVERIFY(QFile::remove(a));QVERIFY(QFile::remove(b));
        QTest::keyClick(&viewer,Qt::Key_C);QTRY_VERIFY(!viewer.loading());QVERIFY(!viewer.options().caseSensitive);
        QVERIFY(viewer.result().error.isEmpty());QVERIFY(!viewer.result().changes.isEmpty());
        QTest::keyClick(&viewer,Qt::Key_W);QTRY_VERIFY(!viewer.loading());QVERIFY(viewer.options().compressWhitespace);
        QVERIFY(!viewer.result().changes.isEmpty());
        QTest::keyClick(&viewer,Qt::Key_E);QTRY_VERIFY(!viewer.loading());QVERIFY(viewer.options().suppressEmpty);
        QVERIFY(viewer.result().changes.isEmpty());QCOMPARE(viewer.result().firstBytes,first);QCOMPARE(viewer.result().secondBytes,second);
        QCOMPARE(viewer.result().differentBytes,difference);
        const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/compare-options.png"));
        viewer.resize(640,400);if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/compare-options-80.png"));
        QTest::keyClick(&viewer,Qt::Key_H);QVERIFY(viewer.hexMode());QCOMPARE(viewer.result().differentBytes,difference);
        QTest::keyClick(&viewer,Qt::Key_C);QVERIFY(!viewer.options().caseSensitive);QVERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_H);QVERIFY(!viewer.hexMode());
        QTest::keyClick(&viewer,Qt::Key_E);QTRY_VERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_W);QTRY_VERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_C);QTRY_VERIFY(!viewer.loading());
        QCOMPARE(viewer.result().changes,initialChanges);QVERIFY(viewer.options().caseSensitive);
        QVERIFY(!viewer.options().compressWhitespace);QVERIFY(!viewer.options().suppressEmpty);
    }
    void compareOptionsPositionAndClose()
    {
        const auto a=root+"/first.txt",b=root+"/second.txt";
        write(a,"same\n\nsame2\nold\nend\n \t\n");write(b,"same\n \t\nsame2\nnew\nend\n\n");
        CompareWindow viewer(a,b);viewer.resize(1280,768);viewer.show();QTRY_VERIFY(!viewer.loading());
        for(int i=0;i<3;++i)QTest::keyClick(&viewer,Qt::Key_Down);QCOMPARE(viewer.currentRow(),3);
        QTest::keyClick(&viewer,Qt::Key_E);QTRY_VERIFY(!viewer.loading());QCOMPARE(viewer.currentRow(),2);
        const auto &row=viewer.result().rows[viewer.currentRow()];QCOMPARE(viewer.result().firstLineNumbers[row.first],4);
        QTest::keyClick(&viewer,Qt::Key_E);QTRY_VERIFY(!viewer.loading());QCOMPARE(viewer.currentRow(),3);
        QTest::keyClick(&viewer,Qt::Key_Down);QTest::keyClick(&viewer,Qt::Key_Down);QCOMPARE(viewer.currentRow(),5);
        QTest::keyClick(&viewer,Qt::Key_E);QTRY_VERIFY(!viewer.loading());QCOMPARE(viewer.currentRow(),3);
        QSignalSpy closed(&viewer,&CompareWindow::closed);
        QTest::keyClick(&viewer,Qt::Key_W);QTest::keyClick(&viewer,Qt::Key_Escape);QCOMPARE(closed.size(),1);
        QTRY_VERIFY(!viewer.loading());
    }
    void compareTaggedCurrentAndValidation()
    {
        const auto folder=root+"/tagged";QVERIFY(QDir().mkpath(folder));
        write(folder+"/a.txt","first\n");write(folder+"/b.txt","second\n");write(folder+"/c.txt","third\n");
        TreeWindow window(folder);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_J,Qt::ControlModifier);
        QVERIFY(!window.findChild<CompareWindow*>());QVERIFY(window.status().contains("tag two"));
        QTest::keyClick(&window,Qt::Key_T);QTest::keyClick(&window,Qt::Key_Home);
        QTest::keyClick(&window,Qt::Key_J,Qt::ControlModifier);
        QVERIFY(!window.findChild<CompareWindow*>());QCOMPARE(window.session().tags.size(),1);
        QTest::keyClick(&window,Qt::Key_Down);const auto selected=window.session().currentFile()->path;
        QTest::keyClick(&window,Qt::Key_J,Qt::ControlModifier);
        QPointer<CompareWindow> viewer=window.findChild<CompareWindow*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QCOMPARE(viewer->result().first,QStringList{"second"});QCOMPARE(viewer->result().second,QStringList{"first"});
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(viewer.isNull());
        QCOMPARE(window.session().currentFile()->path,selected);QCOMPARE(window.session().tags.size(),1);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_J,Qt::ControlModifier);
        QVERIFY(!window.findChild<CompareWindow*>());QCOMPARE(window.session().tags.size(),3);
        QTest::keyClick(&window,Qt::Key_U,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Home);QTest::keyClick(&window,Qt::Key_T);
        QVERIFY(QFile::remove(folder+"/a.txt"));QTest::keyClick(&window,Qt::Key_J,Qt::ControlModifier);
        QVERIFY(!window.findChild<CompareWindow*>());QVERIFY(window.status().contains("does not exist"));
        QCOMPARE(window.session().tags.size(),1);QCOMPARE(window.session().currentFile()->path,selected);
    }
    void compareTaggedPairFiltersAndMenu()
    {
        const auto folder=root+"/tagged";QVERIFY(QDir().mkpath(folder));
        write(folder+"/a.txt","first\n");write(folder+"/b.txt","second\n");write(folder+"/c.txt","third\n");
        TreeWindow window(folder);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_T);QTest::keyClick(&window,Qt::Key_T);
        const auto selected=window.session().currentFile()->path;QCOMPARE(selected,folder+"/c.txt");
        QTest::keyClick(&window,Qt::Key_J,Qt::ControlModifier);
        QPointer<CompareWindow> viewer=window.findChild<CompareWindow*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QCOMPARE(viewer->result().first,QStringList{"first"});QCOMPARE(viewer->result().second,QStringList{"second"});
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(viewer.isNull());QCOMPARE(window.session().currentFile()->path,selected);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"a.txt,b.txt");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_End);QCOMPARE(window.session().currentFile()->path,folder+"/b.txt");
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_J);
        viewer=window.findChild<CompareWindow*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QCOMPARE(viewer->result().first,QStringList{"second"});QCOMPARE(viewer->result().second,QStringList{"first"});
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(viewer.isNull());
        QCOMPARE(window.session().tags.size(),3);QCOMPARE(window.session().files().size(),2);
        QCOMPARE(window.session().filespec.text(),QString("a.txt,b.txt"));
    }
    void compareTaggedAcrossDirectoriesAndSplit()
    {
        const auto folder=root+"/pair",left=folder+"/left",right=folder+"/right";
        QVERIFY(QDir().mkpath(left));QVERIFY(QDir().mkpath(right));
        write(left+"/config.php",QByteArray("a\0b",3));write(right+"/config.php",QByteArray("a\0c",3));
        write(right+"/other.txt","hidden\n");
        TreeWindow window(folder);window.show();QTRY_VERIFY(!window.busy());window.load(true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"config.php");QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_S);QCOMPARE(window.session().view,View::Showall);QCOMPARE(window.session().files().size(),2);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_U,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_Tab);
        QCOMPARE(window.session().tags.size(),2);QVERIFY(window.oppositeSession()->tags.isEmpty());
        const auto selected=window.session().currentFile()->path;const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_J,Qt::ControlModifier);
        QPointer<CompareWindow> viewer=window.findChild<CompareWindow*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QCOMPARE(viewer->result().firstBytes,QByteArray("a\0b",3));QCOMPARE(viewer->result().secondBytes,QByteArray("a\0c",3));
        QVERIFY(!viewer->hexMode());QTest::keyClick(viewer,Qt::Key_H);QVERIFY(viewer->hexMode());QCOMPARE(viewer->result().differentBytes,1);
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(viewer.isNull());
        QVERIFY(window.isSplit());QCOMPARE(window.activeSide(),0);QCOMPARE(window.session().view,View::Showall);
        QCOMPARE(window.session().tags,tags);QVERIFY(window.oppositeSession()->tags.isEmpty());
        QCOMPARE(window.session().currentFile()->path,selected);QCOMPARE(window.session().filespec.text(),QString("config.php"));
        capture(window,"compare-tagged-return");
    }
    void binaryCompareRawBytesAndLengths()
    {
        const auto a=root+"/a.bin",b=root+"/b.bin";
        QByteArray original;for(int i=0;i<256;++i)original.append(char(i));
        auto changed=original;changed[17]='x';changed[240]='y';changed.append("tail");
        write(a,original);write(b,changed);
        auto cancel=std::make_shared<std::atomic_bool>(false);
        auto result=compareFiles(a,b,cancel);QVERIFY(result.error.isEmpty());
        QCOMPARE(result.firstBytes,original);QCOMPARE(result.secondBytes,changed);
        QCOMPARE(result.differentBytes,6);QCOMPARE(result.binaryBlocks,QVector<int>({16,240,256}));
        write(b,original);result=compareFiles(a,b,cancel);QVERIFY(result.bytesEqual);QVERIFY(result.binaryBlocks.isEmpty());
        write(a,{});result=compareFiles(a,b,cancel);QCOMPARE(result.differentBytes,256);
        write(b,{});result=compareFiles(a,b,cancel);QCOMPARE(result.differentBytes,0);QVERIFY(result.bytesEqual);
        write(a,"abcdef");write(b,"abXcdef");result=compareFiles(a,b,cancel);QCOMPARE(result.differentBytes,5);
    }
    void binaryCompareHexNavigationAndResize()
    {
        const auto a=root+"/a.bin",b=root+"/b.bin";
        QByteArray first(300,'\0'),second=first;second[40]='A';second[160]='B';second.append("tail");
        write(a,first);write(b,second);
        CompareWindow viewer(a,b);viewer.resize(640,400);viewer.show();QTRY_VERIFY(!viewer.loading());
        QVERIFY(!viewer.hexMode());QTest::keyClick(&viewer,Qt::Key_H);
        QVERIFY(viewer.hexMode());QCOMPARE(viewer.bytesPerRow(),16);
        const auto rendered=viewer.grab().toImage();
        bool secondDigitVisible=false;
        for(int y=16;y<32;++y)for(int x=88;x<96;++x)
            if(rendered.pixelColor(x,y)==QColor(255,255,0))secondDigitVisible=true;
        QVERIFY(secondDigitVisible);
        QTest::keyClick(&viewer,Qt::Key_Space);QCOMPARE(viewer.currentOffset(),32);
        QTest::keyClick(&viewer,Qt::Key_Plus);QCOMPARE(viewer.currentOffset(),160);
        QTest::keyClick(&viewer,Qt::Key_Minus);QCOMPARE(viewer.currentOffset(),32);
        const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/compare-hex.png"));
        QTest::keyClick(&viewer,Qt::Key_S);QCOMPARE(viewer.bytesPerRow(),8);QCOMPARE(viewer.currentOffset(),32);
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/compare-hex-split-80.png"));
        viewer.resize(1024,600);QCOMPARE(viewer.bytesPerRow(),12);QCOMPARE(viewer.currentOffset(),24);
        QTest::keyClick(&viewer,Qt::Key_H);QVERIFY(!viewer.hexMode());
        QTest::keyClick(&viewer,Qt::Key_H);QVERIFY(viewer.hexMode());
        QTest::keyClick(&viewer,Qt::Key_Plus);QCOMPARE(viewer.currentOffset(),36);
    }
    void binaryCompareCharactersThenHex()
    {
        const auto a=root+"/first.jpg",b=root+"/second.jpg";
        auto first=QByteArray::fromHex("ffd8ffe000104a464946000101");first+="\r\n";
        first+=QByteArray("a\0b",3);first+="\r\nanchor\r\n";
        auto second=first;second[16]=' ';
        write(a,first);write(b,second);
        CompareWindow viewer(a,b);viewer.resize(1280,768);viewer.show();QTRY_VERIFY(!viewer.loading());
        QVERIFY(viewer.result().error.isEmpty());QVERIFY(viewer.result().textError.isEmpty());
        QVERIFY(viewer.result().rawCharacters);QVERIFY(!viewer.hexMode());
        QCOMPARE(viewer.result().first.size(),3);QCOMPARE(viewer.result().first[1],QString::fromLatin1("a\0b",3));
        QCOMPARE(viewer.result().second[1],QString("a b"));QCOMPARE(viewer.result().changes,QVector<int>{1});
        QCOMPARE(viewer.result().differentBytes,1);QCOMPARE(viewer.result().firstBytes,first);
        const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/compare-binary-characters.png"));
        QTest::keyClick(&viewer,Qt::Key_Space);QCOMPARE(viewer.currentRow(),1);
        QTest::keyClick(&viewer,Qt::Key_H);QVERIFY(viewer.hexMode());QCOMPARE(viewer.currentOffset(),0);
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/compare-binary-hex.png"));
        QTest::keyClick(&viewer,Qt::Key_H);QVERIFY(!viewer.hexMode());QCOMPARE(viewer.currentRow(),1);
    }
    void binaryCompareRowAndByteHighlight()
    {
        const auto a=root+"/first.bin",b=root+"/second.bin";
        QByteArray first(40,'\0'),second=first;second[0]='A';write(a,first);write(b,second);
        CompareWindow viewer(a,b);viewer.resize(640,400);viewer.show();QTRY_VERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_H);QVERIFY(viewer.hexMode());
        const auto rendered=viewer.grab().toImage();
        QCOMPARE(rendered.pixelColor(120,20),QColor(0,0,128));
        QCOMPARE(rendered.pixelColor(80,16),QColor(128,0,0));
        for(int i=1;i<viewer.bytesPerRow();++i){
            QCOMPARE(rendered.pixelColor((10+i*3)*8,16),QColor(0,0,128));
            QCOMPARE(rendered.pixelColor((11+viewer.bytesPerRow()*3+i)*8,16),QColor(0,0,128));
        }
        const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/compare-byte-highlight.png"));
        QTest::keyClick(&viewer,Qt::Key_D);
        QCOMPARE(viewer.grab().toImage().pixelColor(120,20),QColor(128,0,0));
        QTest::keyClick(&viewer,Qt::Key_D);
        QCOMPARE(viewer.grab().toImage().pixelColor(120,20),QColor(0,0,128));
    }
    void compareFilenameInOppositeDirectory()
    {
        const auto left=root+"/compare left",right=root+"/compare right";
        QVERIFY(QDir().mkpath(left));QVERIFY(QDir().mkpath(right));
        write(left+"/config.php","left\n");write(right+"/config.php","right\n");
        write(left+"/other.php","wrong folder\n");write(right+"/other.php","correct folder\n");
        TreeWindow window(left);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);
        QVERIFY(window.selectRoot(right));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Tab);QCOMPARE(window.session().currentFile()->path,left+"/config.php");
        QTest::keyClick(&window,Qt::Key_J);QTest::keyClick(&window,Qt::Key_Return);
        QPointer<CompareWindow> viewer=window.findChild<CompareWindow*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QCOMPARE(viewer->result().first,QStringList{"left"});QCOMPARE(viewer->result().second,QStringList{"right"});
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(viewer.isNull());
        QTest::keyClick(&window,Qt::Key_J);QTest::keyClicks(&window,"other.php");QTest::keyClick(&window,Qt::Key_Return);
        viewer=window.findChild<CompareWindow*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QCOMPARE(viewer->result().first,QStringList{"left"});QCOMPARE(viewer->result().second,QStringList{"correct folder"});
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(viewer.isNull());
        write(right+"/other_v2.php","edited name\n");
        QTest::keyClick(&window,Qt::Key_J);QTest::keyClicks(&window,"other.php");
        for(int i=0;i<4;++i)QTest::keyClick(&window,Qt::Key_Left);
        capture(window,"compare-block-cursor");
        QTest::keyClicks(&window,"_v2");QTest::keyClick(&window,Qt::Key_Return);
        viewer=window.findChild<CompareWindow*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QCOMPARE(viewer->result().second,QStringList{"edited name"});
    }
    void compareTextHexOriginalEncoding()
    {
        const auto a=root+"/a.txt",b=root+"/b.txt";
        const auto utf16=QByteArray::fromHex("fffe61000d000a006200");
        write(a,utf16);write(b,"a\nb");
        CompareWindow viewer(a,b);viewer.resize(640,400);viewer.show();QTRY_VERIFY(!viewer.loading());
        QVERIFY(!viewer.hexMode());QVERIFY(viewer.result().textError.isEmpty());QVERIFY(viewer.result().changes.isEmpty());
        QVERIFY(!viewer.result().bytesEqual);QCOMPARE(viewer.result().firstBytes,utf16);
        QTest::keyClick(&viewer,Qt::Key_H);QVERIFY(viewer.hexMode());QVERIFY(viewer.result().differentBytes>0);
        QTest::keyClick(&viewer,Qt::Key_H);QVERIFY(!viewer.hexMode());
        QTest::keyClick(&viewer,Qt::Key_H);QVERIFY(viewer.hexMode());
    }
    void compareBinaryFromSplitReturnsUnchanged()
    {
        const auto other=root+"/other";QVERIFY(QDir().mkpath(other));
        write(panel+"/sample.bin",QByteArray("a\0b",3));write(other+"/sample.bin",QByteArray("a\0c!",4));
        TreeWindow window(other);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);
        QVERIFY(window.selectRoot(panel));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_S,Qt::ShiftModifier);
        const auto selected=window.session().currentFile()->path;QCOMPARE(selected,panel+"/sample.bin");
        QTest::keyClick(&window,Qt::Key_J);QTest::keyClick(&window,Qt::Key_Return);
        QPointer<CompareWindow> viewer=window.findChild<CompareWindow*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QVERIFY(!viewer->hexMode());QTest::keyClick(viewer,Qt::Key_H);
        QVERIFY(viewer->hexMode());QCOMPARE(viewer->result().differentBytes,2);
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(viewer.isNull());
        QCOMPARE(window.session().currentFile()->path,selected);QVERIFY(window.isSplit());QCOMPARE(window.oppositeSession()->tags.size(),1);
        QFile file(panel+"/sample.bin");QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("a\0b",3));
    }
    void compareAlignmentAndEndings()
    {
        const auto a=root+"/first.txt",b=root+"/second.txt";
        auto cancel=std::make_shared<std::atomic_bool>(false);
        write(a,"same\nold\nanchor\nremoved\nend\n");
        write(b,"same\nnew\ninserted\nanchor\nend\n");
        const auto result=compareFiles(a,b,cancel); QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QCOMPARE(result.changes.size(),2); QCOMPARE(result.rows.size(),6);
        QCOMPARE(result.rows[0].first,0); QCOMPARE(result.rows[0].second,0); QVERIFY(!result.rows[0].changed);
        QCOMPARE(result.rows[2].first,-1); QCOMPARE(result.rows[2].second,2);
        QCOMPARE(result.rows[3].first,2); QCOMPARE(result.rows[3].second,3); QVERIFY(!result.rows[3].changed);
        QCOMPARE(result.rows[4].first,3); QCOMPARE(result.rows[4].second,-1);
        write(a,"one\r\ntwo\rthree\n");write(b,"one\ntwo\nthree\n");
        auto equal=compareFiles(a,b,cancel);QVERIFY(equal.error.isEmpty());QVERIFY(equal.changes.isEmpty());QVERIFY(!equal.bytesEqual);
        write(a,{});write(b,"added\n");auto added=compareFiles(a,b,cancel);
        QVERIFY2(added.error.isEmpty(),qPrintable(added.error));QCOMPARE(added.rows.size(),1);QCOMPARE(added.rows[0].first,-1);
        write(a,"old\n");write(b,{});auto removed=compareFiles(a,b,cancel);
        QVERIFY2(removed.error.isEmpty(),qPrintable(removed.error));QCOMPARE(removed.rows[0].second,-1);
        write(a,{});auto empty=compareFiles(a,b,cancel);QVERIFY(empty.error.isEmpty());QVERIFY(empty.rows.isEmpty());QVERIFY(empty.bytesEqual);
    }
    void compareErrorsAndCancellation()
    {
        const auto a=root+"/first.txt",b=root+"/second.txt";
        auto cancel=std::make_shared<std::atomic_bool>(false);
        write(a,QByteArray("a\0b",3));write(b,"text");
        const auto binary=compareFiles(a,b,cancel);
        QVERIFY(binary.textError.isEmpty());QVERIFY(binary.rawCharacters);QVERIFY(!binary.changes.isEmpty());
        QVERIFY(!compareFiles(root+"/missing",b,cancel).error.isEmpty());
        write(a,QByteArray(200001,'\n'));QVERIFY(compareFiles(a,b,cancel).textError.contains("200000"));
        cancel->store(true);QVERIFY(compareFiles(a,b,cancel).cancelled);
    }
    void compareViewerNavigationAndLayout()
    {
        const auto a=root+"/first.txt",b=root+"/second.txt";
        write(a,"anchor\nold\ncommon\nlast old\nend\n");write(b,"anchor\nnew\ncommon\nlast new\nend\n");
        CompareWindow viewer(a,b);viewer.resize(1280,768);viewer.show();QTRY_VERIFY(!viewer.loading());
        QVERIFY(viewer.result().error.isEmpty());QCOMPARE(viewer.result().changes.size(),2);
        QTest::keyClick(&viewer,Qt::Key_Space);QCOMPARE(viewer.currentRow(),1);
        QTest::keyClick(&viewer,Qt::Key_Plus);QCOMPARE(viewer.currentRow(),3);
        QTest::keyClick(&viewer,Qt::Key_Minus);QCOMPARE(viewer.currentRow(),1);
        QTest::keyClick(&viewer,Qt::Key_Home);
        const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/compare-horizontal.png"));
        QTest::keyClick(&viewer,Qt::Key_S);QVERIFY(viewer.vertical());
        viewer.resize(640,400);
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/compare-vertical-80.png"));
        QSignalSpy closed(&viewer,&CompareWindow::closed);QTest::keyClick(&viewer,Qt::Key_Escape);QCOMPARE(closed.size(),1);
    }
    void compareSplitPromptAndReturn()
    {
        const auto left=root+"/compare left",right=root+"/compare right";
        QVERIFY(QDir().mkpath(left));QVERIFY(QDir().mkpath(right));
        write(left+"/config.php","left\n");write(right+"/config.php","right\n");
        TreeWindow window(left);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);
        QVERIFY(window.selectRoot(right));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto tags=window.session().tags;const auto selected=window.session().currentFile()->path;
        QTest::keyClick(&window,Qt::Key_J);capture(window,"compare-prompt");QTest::keyClick(&window,Qt::Key_Return);
        QPointer<CompareWindow> compare=window.findChild<CompareWindow*>();QVERIFY(compare);QTRY_VERIFY(!compare->loading());
        QVERIFY2(compare->result().error.isEmpty(),qPrintable(compare->result().error));
        QCOMPARE(compare->result().first,QStringList{"right"});QCOMPARE(compare->result().second,QStringList{"left"});
        window.resize(window.minimumSize());QTRY_COMPARE(compare->size(),window.size());
        QTest::keyClick(compare,Qt::Key_Escape);QTRY_VERIFY(compare.isNull());
        QVERIFY(window.isSplit());QCOMPARE(window.activeSide(),1);QCOMPARE(window.session().tags,tags);QCOMPARE(window.session().currentFile()->path,selected);
        QTest::keyClick(&window,Qt::Key_J);QTest::keyClicks(&window,"missing");QTest::keyClick(&window,Qt::Key_Return);
        QVERIFY(window.status().contains("does not exist"));QVERIFY(!window.findChild<CompareWindow*>());
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_Return);
        compare=window.findChild<CompareWindow*>();QVERIFY(compare);QTRY_VERIFY(!compare->loading());
        QVERIFY(compare->result().error.isEmpty());
    }
    void viewerDumpRawOffsetsAndSearch()
    {
        const auto path=root+"/dump.bin";
        QByteArray bytes(29,'x'); bytes+="Needle\r\n"; bytes+=QByteArray(100,'z');
        write(path,bytes);
        FileViewer viewer(path); viewer.resize(320,400); viewer.show(); QTRY_VERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_D); QVERIFY(viewer.dumpMode());
        QTest::keyClick(&viewer,Qt::Key_F); QTest::keyClicks(&viewer,"needle"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.hit(),29); QCOMPARE(viewer.currentRow(),0);
        QTest::keyClick(&viewer,Qt::Key_O); QTest::keyClicks(&viewer,"25"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.currentRow(),1);
        viewer.resize(640,400); QTRY_COMPARE(viewer.currentRow(),0);
        QTest::keyClick(&viewer,Qt::Key_H); QVERIFY(viewer.hexMode());
        QTest::keyClick(&viewer,Qt::Key_F); QTest::keyClicks(&viewer,"0D 0A"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.hit(),35);
        QTest::keyClick(&viewer,Qt::Key_H); QVERIFY(viewer.dumpMode());
        QTest::keyClick(&viewer,Qt::Key_H); QVERIFY(viewer.hexMode());
        QTest::keyClick(&viewer,Qt::Key_W); QVERIFY(viewer.wrapMode());
        QTest::keyClick(&viewer,Qt::Key_W); QVERIFY(viewer.hexMode());
    }
    void viewerDumpAllBytesEmptyAndReload()
    {
        const auto path=root+"/dump.bin";
        QByteArray bytes; for(int i=0;i<256;++i)bytes.append(char(i)); write(path,bytes);
        FileViewer viewer(path); viewer.resize(640,400); viewer.show(); QTRY_VERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_D);
        const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/visor-dump.png"));
        QSignalSpy editor(&viewer,&FileViewer::editRequested); QTest::keyClick(&viewer,Qt::Key_E); QCOMPARE(editor.size(),0);
        QTest::keyClick(&viewer,Qt::Key_O); QTest::keyClicks(&viewer,"FF"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.currentRow(),3);
        write(path,{}); QTest::keyClick(&viewer,Qt::Key_F3); QTRY_VERIFY(!viewer.loading()); QCOMPARE(viewer.currentRow(),0);
        QTest::keyClick(&viewer,Qt::Key_F); QTest::keyClicks(&viewer,"x"); QTest::keyClick(&viewer,Qt::Key_Return);
        QVERIFY(viewer.status().contains("No more"));
    }
    void autoviewDumpModeAndAlternation()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy()); QTest::keyClick(&window,Qt::Key_F7);
        auto preview=window.findChild<FileViewer*>("autoview"); QVERIFY(preview); QTRY_VERIFY(!preview->loading());
        QTest::keyClick(&window,Qt::Key_D,Qt::ShiftModifier); QVERIFY(preview->dumpMode());
        QTest::keyClick(&window,Qt::Key_Down); QTRY_VERIFY(!preview->loading()); QVERIFY(preview->dumpMode());
        QCOMPARE(preview->path(),panel+"/ordenes.php");
        QTest::keyClick(&window,Qt::Key_D,Qt::ShiftModifier); QVERIFY(!preview->dumpMode()); QVERIFY(!preview->hexMode());
        QTest::keyClick(&window,Qt::Key_D,Qt::ShiftModifier); QVERIFY(preview->dumpMode());
        capture(window,"autoview-dump");
    }
    void viewerWrapSearchResizeAndLineJump()
    {
        const auto path=root+"/wrap.txt";
        write(path,QByteArray(100,'a')+"needle"+QByteArray(80,'b')+"\nnext\n");
        FileViewer viewer(path); viewer.resize(320,400); viewer.show(); QTRY_VERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_W); QVERIFY(viewer.wrapMode()); QVERIFY(!viewer.hexMode());
        QTest::keyClick(&viewer,Qt::Key_F); QTest::keyClicks(&viewer,"needle"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.hit(),100); QCOMPARE(viewer.currentRow(),2);
        viewer.resize(640,400); QTRY_COMPARE(viewer.currentRow(),1); QCOMPARE(viewer.hit(),100);
        QTest::keyClick(&viewer,Qt::Key_O); QTest::keyClicks(&viewer,"2"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.currentRow(),3);
        QTest::keyClick(&viewer,Qt::Key_Slash); QTest::keyClicks(&viewer,"next"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.hit(),187); QCOMPARE(viewer.currentRow(),3);
        const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");
        QTest::keyClick(&viewer,Qt::Key_Home);
        if(!dir.isEmpty())QVERIFY(viewer.grab().save(dir+"/visor-wrap.png"));
        QTest::keyClick(&viewer,Qt::Key_H); QVERIFY(viewer.hexMode()); QVERIFY(!viewer.wrapMode());
        QTest::keyClick(&viewer,Qt::Key_A); QVERIFY(!viewer.hexMode()); QVERIFY(!viewer.wrapMode());
    }
    void viewerWrapTabsUnicodeEmptyAndReload()
    {
        const auto path=root+"/wrap.txt";
        write(path,QByteArray(70,'a')+"\t"+QString::fromUtf8("😀").repeated(10).toUtf8()+"\n\nlast");
        FileViewer viewer(path); viewer.resize(640,400); viewer.show(); QTRY_VERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_W);
        QTest::keyClick(&viewer,Qt::Key_O); QTest::keyClicks(&viewer,"3"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.currentRow(),3);
        write(path,"\n"); QTest::keyClick(&viewer,Qt::Key_F3); QTRY_VERIFY(!viewer.loading()); QCOMPARE(viewer.currentRow(),0);
        write(path,{}); QTest::keyClick(&viewer,Qt::Key_F3); QTRY_VERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_End); QCOMPARE(viewer.currentRow(),0);
        viewer.resize(320,400); QVERIFY(viewer.wrapMode());
    }
    void autoviewWrapFollowsWidthAndFile()
    {
        write(panel+"/.htaccess",QByteArray(300,'a')+" target\n");
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_F7);
        auto preview=window.findChild<FileViewer*>("autoview"); QVERIFY(preview); QTRY_VERIFY(!preview->loading());
        QTest::keyClick(&window,Qt::Key_W,Qt::ShiftModifier); QVERIFY(preview->wrapMode());
        QTest::keyClick(&window,Qt::Key_F,Qt::ShiftModifier); QTest::keyClicks(&window,"target"); QTest::keyClick(&window,Qt::Key_Return);
        QCOMPARE(preview->hit(),301); QVERIFY(preview->currentRow()>0);
        QTest::keyClick(&window,Qt::Key_Right,Qt::AltModifier); QVERIFY(preview->wrapMode()); QCOMPARE(preview->hit(),301);
        capture(window,"autoview-wrap");
        QTest::keyClick(&window,Qt::Key_Down); QTRY_VERIFY(!preview->loading());
        QVERIFY(preview->wrapMode()); QCOMPARE(preview->currentRow(),0); QCOMPARE(preview->path(),panel+"/ordenes.php");
    }
    void autoviewNavigationAndCommands()
    {
        write(panel+"/.htaccess","primera\nsegunda\ntercera\n");
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_F7);
        auto preview=window.findChild<FileViewer*>("autoview"); QVERIFY(preview); QTRY_VERIFY(!preview->loading());
        QCOMPARE(window.session().view,View::Directory);
        const auto selected=window.session().currentFile()->path;
        QTest::keyClick(&window,Qt::Key_Down,Qt::ShiftModifier);
        QCOMPARE(preview->currentRow(),1); QCOMPARE(window.session().currentFile()->path,selected);
        QTest::keyClick(&window,Qt::Key_H,Qt::ShiftModifier); QVERIFY(preview->hexMode());
        QTest::keyClick(&window,Qt::Key_A,Qt::ShiftModifier);
        QTest::keyClick(&window,Qt::Key_F,Qt::ShiftModifier); QVERIFY(preview->prompting());
        QTest::keyClicks(&window,"segunda"); QTest::keyClick(&window,Qt::Key_Return);
        QVERIFY(!preview->prompting()); QCOMPARE(preview->hit(),8);
        const auto wide=preview->width();
        QTest::keyClick(&window,Qt::Key_Right,Qt::AltModifier); QVERIFY(preview->width()<wide);
        QTest::keyClick(&window,Qt::Key_Home,Qt::AltModifier); QCOMPARE(preview->width(),wide);
        QTest::keyClick(&window,Qt::Key_Down); QTRY_VERIFY(!preview->loading());
        QCOMPARE(preview->path(),panel+"/ordenes.php"); QCOMPARE(preview->hit(),-1);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier); QCOMPARE(window.session().tags.size(),3);
        capture(window,"autoview");
        QTest::keyClick(&window,Qt::Key_F1); QVERIFY(!preview->isVisible());
        QTest::keyClick(&window,Qt::Key_Escape); QVERIFY(preview->isVisible());
        QTest::keyClick(&window,Qt::Key_L); QVERIFY(!preview->isVisible());
        QTest::keyClick(&window,Qt::Key_Escape); QVERIFY(preview->isVisible());
        QTest::keyClick(&window,Qt::Key_Return); QVERIFY(!preview->isVisible());
        QCOMPARE(window.session().view,View::Tree); QCOMPARE(window.session().tags.size(),3);
    }
    void autoviewSplitAndResize()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return); QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_Tab); QTest::keyClick(&window,Qt::Key_F7);
        auto preview=window.findChild<FileViewer*>("autoview"); QVERIFY(preview); QTRY_VERIFY(!preview->loading());
        QVERIFY(preview->x()>=window.width()/2);
        window.resize(window.minimumSize());
        QVERIFY(preview->geometry().right()<window.width()); QVERIFY(preview->width()>0);
        capture(window,"autoview-split-80");
        QTest::keyClick(&window,Qt::Key_Tab); QVERIFY(!preview->isVisible()); QCOMPARE(window.activeSide(),0);
        QTest::keyClick(&window,Qt::Key_F7); QTest::keyClick(&window,Qt::Key_F8);
        QVERIFY(!window.isSplit()); QCOMPARE(window.session().view,View::Directory);
    }
    void autoviewUnloggedAndEmpty()
    {
        TreeWindow window(root); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Down);
        QCOMPARE(window.session().directory,alpha);
        QTest::keyClick(&window,Qt::Key_F7); QTRY_VERIFY(!window.busy());
        QVERIFY(window.session().currentDirectory().loaded);
        QVERIFY(!window.findChild<FileViewer*>("autoview"));
        QVERIFY(window.status().contains("No files")); QCOMPARE(window.session().view,View::Tree);
        QVERIFY(window.selectRoot(panel)); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_F7); QVERIFY(window.findChild<FileViewer*>("autoview"));
        QTest::keyClick(&window,Qt::Key_Escape); QCOMPARE(window.session().view,View::Tree);
    }
    void viewerRapidPathChanges()
    {
        FileViewer viewer(panel+"/.htaccess"); viewer.show();
        viewer.setPath(panel+"/usuarios.php"); viewer.setPath(panel+"/.htaccess");
        QTRY_VERIFY(!viewer.loading()); QVERIFY(viewer.status().isEmpty());
        QTest::keyClick(&viewer,Qt::Key_F); QTest::keyClicks(&viewer,"ordenes"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.hit(),0);
        viewer.setPath(panel+"/ordenes.php"); viewer.setPath(panel+"/usuarios.php");
        QTRY_VERIFY(!viewer.loading()); QVERIFY(viewer.status().isEmpty());
        QTest::keyClick(&viewer,Qt::Key_F); QTest::keyClicks(&viewer,"usuarios"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.hit(),0);
        viewer.setPath(root+"/missing"); QTRY_VERIFY(!viewer.loading()); QVERIFY(!viewer.status().isEmpty()); QCOMPARE(viewer.hit(),-1);
    }
    void sortCriteriaAndPaths()
    {
        const auto early=QDateTime(QDate(2024,1,1),QTime(18,0));
        const auto late=QDateTime(QDate(2024,1,2),QTime(9,0));
        QVector<FileEntry> original{{root+"/z/a.zzz","a.zzz",qint64(1)<<40,early},
            {root+"/a/b.txt","b.txt",20,late}, {root+"/a/a.b.txt","a.b.txt",10,early}};
        auto names=[](const QVector<FileEntry> &files) { QStringList result; for(const auto &f:files)result<<f.name; return result; };
        auto files=original;
        sortFiles(files,{SortKey::Name},false); QCOMPARE(names(files),QStringList({"a.zzz","a.b.txt","b.txt"}));
        sortFiles(files,{SortKey::Extension},false); QCOMPARE(names(files),QStringList({"a.b.txt","b.txt","a.zzz"}));
        sortFiles(files,{SortKey::Size,true},false); QCOMPARE(names(files),QStringList({"a.zzz","b.txt","a.b.txt"}));
        sortFiles(files,{SortKey::Date},false); QCOMPARE(names(files),QStringList({"a.zzz","a.b.txt","b.txt"}));
        sortFiles(files,{SortKey::Time},false); QCOMPARE(names(files),QStringList({"b.txt","a.zzz","a.b.txt"}));
        sortFiles(files,{SortKey::Length},false); QCOMPARE(names(files),QStringList({"a.zzz","b.txt","a.b.txt"}));
        sortFiles(files,{SortKey::Name,false,true},true); QCOMPARE(names(files),QStringList({"a.b.txt","b.txt","a.zzz"}));
        files=original; sortFiles(files,{SortKey::Unsorted,true,true},true); QCOMPARE(names(files),names(original));
    }
    void sortNumericAndAlpha()
    {
        auto ordered=[&](QStringList names,SortKey key) {
            QVector<FileEntry> files; for(const auto &name:names)files.append({root+'/'+name,name});
            sortFiles(files,{key},false); names.clear(); for(const auto &f:files)names<<f.name; return names;
        };
        QCOMPARE(ordered({"f10.txt","f2.txt","f0002.txt","f999999999999999999999.txt"},SortKey::Number),
                 QStringList({"f0002.txt","f2.txt","f10.txt","f999999999999999999999.txt"}));
        QCOMPARE(ordered({"a02x11","a2x2","a2x10"},SortKey::Number),QStringList({"a2x2","a2x10","a02x11"}));
        QCOMPARE(ordered({"z2.txt","a10.txt","b001.txt","none.txt"},SortKey::Value),QStringList({"none.txt","b001.txt","z2.txt","a10.txt"}));
        QCOMPARE(ordered({"!20z.txt","100a.txt","--b.txt"},SortKey::Alpha),QStringList({"100a.txt","--b.txt","!20z.txt"}));
    }
    void sortPreservesSelectionAndTags()
    {
        Session session(panel); session.apply(scan(panel),true); session.enter(View::Directory);
        session.spell("ordenes"); const auto selected=session.currentFile()->path;
        session.setTag(true,true); session.tags.remove(panel+"/.htaccess"); const auto tags=session.tags;
        session.toggleTagsOnly();
        session.sort={SortKey::Name,true}; session.rebuild();
        QCOMPARE(session.currentFile()->path,selected); QCOMPARE(session.tags,tags);
        QVERIFY(session.tagsOnly); QCOMPARE(session.files().size(),2);
        session.sort={SortKey::Unsorted}; session.rebuild();
        QCOMPARE(session.currentFile()->path,selected); QCOMPARE(session.tags,tags);
        const auto &raw=session.currentDirectory().files;
        int index=0;
        for(const auto &file:raw)if(tags.contains(file.path))QCOMPARE(session.files()[index++].path,file.path);
    }
    void sortKeyboardAndSplit()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto selected=window.session().currentFile()->path;
        const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_S,Qt::AltModifier);
        QTest::keyClick(&window,Qt::Key_Down);
        capture(window,"sort-menu");
        QTest::keyClick(&window,Qt::Key_N);
        QVERIFY(window.session().sort.descending);
        QCOMPARE(window.session().files().first().name,QString("usuarios.php"));
        QCOMPARE(window.session().currentFile()->path,selected); QCOMPARE(window.session().tags,tags);
        QTest::keyClick(&window,Qt::Key_S,Qt::AltModifier);
        QTest::keyClick(&window,Qt::Key_F12); QTest::keyClick(&window,Qt::Key_Escape);
        QVERIFY(window.session().sort.descending);
        QTest::keyClick(&window,Qt::Key_F8); QVERIFY(window.isSplit());
        QTest::keyClick(&window,Qt::Key_S,Qt::AltModifier); QTest::keyClick(&window,Qt::Key_Up); QTest::keyClick(&window,Qt::Key_E);
        QTest::keyClick(&window,Qt::Key_Tab);
        QCOMPARE(window.session().sort.key,SortKey::Extension); QVERIFY(!window.session().sort.descending);
        QTest::keyClick(&window,Qt::Key_S,Qt::AltModifier); QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_S); QVERIFY(window.isSplit());
        QCOMPARE(window.session().sort.key,SortKey::Size);
        QTest::keyClick(&window,Qt::Key_Tab); QCOMPARE(window.session().sort.key,SortKey::Extension);
        QTest::keyClick(&window,Qt::Key_Tab); QCOMPARE(window.session().sort.key,SortKey::Size);
        QVERIFY(window.selectRoot(root)); QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().sort.key,SortKey::Size);
        QVERIFY(window.selectRoot(panel)); QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().sort.key,SortKey::Size);
        QTest::keyClick(&window,Qt::Key_S,Qt::AltModifier); QTest::keyClick(&window,Qt::Key_F12); QTest::keyClick(&window,Qt::Key_Return);
        QVERIFY(window.session().sort==SortOptions{});
        QTest::keyClick(&window,Qt::Key_Tab); QVERIFY(window.session().sort==SortOptions{});
        capture(window,"sort-split");
        window.resize(window.minimumSize());
        QTest::keyClick(&window,Qt::Key_S,Qt::AltModifier); capture(window,"sort-menu-80");
    }
    void viewerReadEncodingsAndErrors()
    {
        auto cancel=std::make_shared<std::atomic_bool>(false);
        const QString path=root+"/vista.txt";
        write(path,"uno\r\ndos\rtres\n");
        auto data=readViewDocument(path,cancel);
        QVERIFY(data.error.isEmpty()); QCOMPARE(data.lines.size(),3);
        QCOMPARE(data.text,QString("uno\ndos\ntres\n"));
        write(path,QByteArray::fromHex("fffe61000a006200"));
        data=readViewDocument(path,cancel);
        QCOMPARE(data.text,QString("a\nb")); QCOMPARE(data.lines.size(),2);
        write(path,QByteArray::fromHex("e1ff"));
        data=readViewDocument(path,cancel); QVERIFY(data.encoding.contains("Latin-1"));
        QVERIFY(!readViewDocument(root+"/missing",cancel).error.isEmpty());
        write(path,{}); data=readViewDocument(path,cancel); QVERIFY(data.error.isEmpty()); QCOMPARE(data.lines.size(),1);
        QFile large(path); QVERIFY(large.open(QIODevice::WriteOnly)); QVERIFY(large.resize(33*1024*1024)); large.close();
        QVERIFY(readViewDocument(path,cancel).error.contains("32 MiB"));
        cancel->store(true); QVERIFY(readViewDocument(path,cancel).cancelled);
    }
    void viewerSearchAndHex()
    {
        const QString path=root+"/vista.txt";
        write(path,"uno\nordenes\nORDENES\nfin\n");
        FileViewer viewer(path); viewer.resize(1280,768); viewer.show(); QTRY_VERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_F); QTest::keyClicks(&viewer,"ordenes"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.hit(),4); QCOMPARE(viewer.currentRow(),1);
        QTest::keyClick(&viewer,Qt::Key_Plus); QCOMPARE(viewer.hit(),12);
        QTest::keyClick(&viewer,Qt::Key_Plus); QVERIFY(viewer.status().contains("No more"));
        QTest::keyClick(&viewer,Qt::Key_Minus); QCOMPARE(viewer.hit(),4);
        QTest::keyClick(&viewer,Qt::Key_B); QTest::keyClick(&viewer,Qt::Key_F3); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.hit(),12);
        const QString captures=qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if(!captures.isEmpty()) QVERIFY(viewer.grab().save(captures+"/visor-alpha.png"));
        QTest::keyClick(&viewer,Qt::Key_H); QVERIFY(viewer.hexMode());
        QTest::keyClick(&viewer,Qt::Key_F); QTest::keyClicks(&viewer,"6F 72 64"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.hit(),4);
        if(!captures.isEmpty()) QVERIFY(viewer.grab().save(captures+"/visor-hex.png"));
        QTest::keyClick(&viewer,Qt::Key_F); QTest::keyClicks(&viewer,"ZZ"); QTest::keyClick(&viewer,Qt::Key_Return);
        QVERIFY(viewer.status().contains("hex pairs"));
        QTest::keyClick(&viewer,Qt::Key_A); QVERIFY(!viewer.hexMode());
        QTest::keyClick(&viewer,Qt::Key_O); QTest::keyClicks(&viewer,"3"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.currentRow(),2);
    }
    void viewerRemembersBatchSearch_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<QString>("query");
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("sensitive");
        QTest::addColumn<qlonglong>("first");
        QTest::addColumn<qlonglong>("second");
        QTest::newRow("text-folded") << QByteArray("skip\nNEEDLE\nneedle\n") << QString("needle") << 0 << false << 5LL << 12LL;
        QTest::newRow("text-sensitive") << QByteArray("NEEDLE\nneedle\nneedle\n") << QString("needle") << 0 << true << 7LL << 14LL;
        QTest::newRow("text-nonascii-case") << QString::fromUtf8("CAFÉ\ncafé\ncafé\n").toUtf8() << QString::fromUtf8("café") << 0 << false << 5LL << 10LL;
        QTest::newRow("hex") << QByteArray::fromHex("112200ff334400ff") << QString("00 FF") << 1 << false << 2LL << 6LL;
        QTest::newRow("unicode-utf16") << QByteArray::fromHex("fffe78000a00c100520042004f004c000a00e100720062006f006c000a00") << QString::fromUtf8("árbol") << 2 << false << 2LL << 8LL;
        QTest::newRow("internal-stars") << QByteArray("ord\nenes\nordZZenes\nordQQenes\n") << QString("ord*enes") << 0 << false << 9LL << 19LL;
        QTest::newRow("leading-literal-star") << QByteArray("*a*b\n*aZZb\n*a*b\n") << QString("*a*b") << 0 << true << 0LL << 11LL;
        QTest::newRow("text-utf8-fallback") << (QByteArray::fromHex("ff") + QString::fromUtf8("café\ncafé\n").toUtf8()) << QString::fromUtf8("café") << 0 << false << 1LL << 7LL;
    }
    void viewerRemembersBatchSearch()
    {
        QFETCH(QByteArray,contents);QFETCH(QString,query);QFETCH(int,mode);QFETCH(bool,sensitive);
        QFETCH(qlonglong,first);QFETCH(qlonglong,second);
        const QString directory=root+"/batch";QVERIFY(QDir().mkpath(directory));write(directory+"/sample.txt",contents);
        TreeWindow window(directory);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);
        for(int i=0;i<mode;++i)QTest::keyClick(&window,Qt::Key_F4);
        if(sensitive)QTest::keyClick(&window,Qt::Key_F2);
        QKeyEvent input(QEvent::KeyPress,Qt::Key_unknown,Qt::NoModifier,query);QApplication::sendEvent(&window,&input);
        QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QVERIFY(window.status().contains("Hits: 1"));QCOMPARE(window.session().tags.size(),1);
        QTest::keyClick(&window,Qt::Key_V);
        auto viewer=window.findChild<FileViewer*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QCOMPARE(viewer->hexMode(),mode==1);QCOMPARE(viewer->hit(),first);
        QTest::keyClick(viewer,Qt::Key_Space);QCOMPARE(viewer->hit(),second);
        QTest::keyClick(viewer,Qt::Key_Space);QVERIFY(viewer->status().contains("No more matches"));QCOMPARE(viewer->hit(),second);
        QTest::keyClick(viewer,Qt::Key_Minus);QCOMPARE(viewer->hit(),first);
        QTest::keyClick(viewer,Qt::Key_F3);QTRY_VERIFY(!viewer->loading());QCOMPARE(viewer->hit(),first);
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(window.findChildren<FileViewer*>().isEmpty());
        QTest::keyClick(&window,Qt::Key_V);viewer=window.findChild<FileViewer*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QCOMPARE(viewer->hit(),first);QTest::keyClick(viewer,Qt::Key_Space);QCOMPARE(viewer->hit(),second);
    }
    void viewerBatchSearchKeepsLastExecutedAndSplitContext()
    {
        const QString directory=root+"/batch";QVERIFY(QDir().mkpath(directory+"/sub"));
        for(const auto &name:QStringList{"a.txt","b.txt","sub/c.txt"})write(directory+'/'+name,"skip\nneedle\nneedle\nsecond\nsecond\n");
        TreeWindow window(directory);window.show();QTRY_VERIFY(!window.busy());window.load(true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_B);QCOMPARE(window.session().view,View::Branch);
        QTest::keyClick(&window,Qt::Key_F8);QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_V);
        auto viewer=window.findChild<FileViewer*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QTest::keyClick(viewer,Qt::Key_Space);QCOMPARE(viewer->hit(),-1);
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(window.findChildren<FileViewer*>().isEmpty());
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);
        QTest::keyClicks(&window,"needle");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().tags.size(),3);
        const QString selected=window.session().currentFile()->path;const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClicks(&window,"cancelled");QTest::keyClick(&window,Qt::Key_F2);
        QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.status().contains("hex pairs"));
        QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_V);
        viewer=window.findChild<FileViewer*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QVERIFY(!viewer->hexMode());QCOMPARE(viewer->hit(),5);QTest::keyClick(viewer,Qt::Key_Space);QCOMPARE(viewer->hit(),12);
        QTest::keyClick(viewer,Qt::Key_F);QTest::keyClicks(viewer,"second");QTest::keyClick(viewer,Qt::Key_Return);
        QCOMPARE(viewer->hit(),19);QTest::keyClick(viewer,Qt::Key_Space);QCOMPARE(viewer->hit(),26);
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(window.findChildren<FileViewer*>().isEmpty());
        QCOMPARE(window.activeSide(),1);QVERIFY(window.isSplit());QCOMPARE(window.session().view,View::Branch);
        QCOMPARE(window.session().currentFile()->path,selected);QCOMPARE(window.session().tags,tags);
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_V);
        viewer=window.findChild<FileViewer*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());QCOMPARE(viewer->hit(),5);
        QTest::keyClick(viewer,Qt::Key_Escape);QTRY_VERIFY(window.findChildren<FileViewer*>().isEmpty());QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);
        QTest::keyClicks(&window,"second");QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_V);viewer=window.findChild<FileViewer*>();QVERIFY(viewer);QTRY_VERIFY(!viewer->loading());
        QCOMPARE(viewer->hit(),19);QTest::keyClick(viewer,Qt::Key_Space);QCOMPARE(viewer->hit(),26);
        const QString captures=qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if(!captures.isEmpty())QVERIFY(viewer->grab().save(captures+"/viewer-batch-search.png"));
    }
    void viewerReturnsToSplitSelection()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_Down);
        QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto path=window.session().currentFile()->path;
        const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_V);
        auto viewer=window.findChild<FileViewer*>(); QVERIFY(viewer); QTRY_VERIFY(!viewer->loading());
        QTest::mouseClick(viewer,Qt::RightButton,Qt::NoModifier,QPoint(20,40));
        QCOMPARE(window.session().currentFile()->path,path); QCOMPARE(window.session().tags,tags);
        QSignalSpy editor(&window,&TreeWindow::openRequested);
        QTest::keyClick(viewer,Qt::Key_E); QCOMPARE(editor.size(),1); QCOMPARE(editor.first()[0].toString(),path);
        window.resize(window.minimumSize()); QTRY_COMPARE(viewer->size(),window.size());
        QTest::keyClick(viewer,Qt::Key_Escape);
        QCOMPARE(window.activeSide(),1); QVERIFY(window.isSplit());
        QCOMPARE(window.session().currentFile()->path,path); QCOMPARE(window.session().tags,tags);
    }
    void viewerReloadAndCloseLoading()
    {
        const QString path=root+"/recarga.txt"; write(path,"antes\n");
        FileViewer viewer(path); viewer.show(); QTRY_VERIFY(!viewer.loading());
        write(path,"despues\n"); QTest::keyClick(&viewer,Qt::Key_F3); QTRY_VERIFY(!viewer.loading());
        QTest::keyClick(&viewer,Qt::Key_F); QTest::keyClicks(&viewer,"despues"); QTest::keyClick(&viewer,Qt::Key_Return);
        QCOMPARE(viewer.hit(),0);
        QSignalSpy closed(&viewer,&FileViewer::closed);
        QTest::keyClick(&viewer,Qt::Key_F3); QTest::keyClick(&viewer,Qt::Key_Escape);
        QCOMPARE(closed.size(),1);
    }
    void mountTableParsing()
    {
        const QByteArray data =
            "1 0 8:1 / / rw - ext4 /dev/test rw\n"
            "2 1 0:1 / /proc rw - proc proc rw\n"
            "3 1 0:2 / /media/Nas\\040Trabajo ro - cifs //server/a\\040b ro\n"
            "4 1 8:2 / /media/Datos rw - ext4 /dev/test2 rw\n"
            "5 1 0:3 / /var/lib/docker/test rw - overlay overlay rw\n"
            "6 1 0:4 / /run/user/1000/doc rw - fuse.portal portal rw\n"
            "bad line\n";
        const auto mounts = parseMountInfo(data);
        QCOMPARE(mounts.size(), 3);
        QCOMPARE(mounts[0].path, QString("/"));
        QCOMPARE(mounts[2].path, QString("/media/Nas Trabajo"));
        QCOMPARE(mounts[2].source, QString("//server/a b"));
        QVERIFY(mounts[2].readOnly);
        QCOMPARE(parseMountInfo(data, true).size(), 6);
    }
    void recursiveScanStopsAtMountBoundary()
    {
        const auto result = scanDirectories(panel, true, std::make_shared<std::atomic_bool>(false), {panel + "/modulos"});
        QCOMPARE(result.directories.size(), 1);
        QVERIFY(result.directories[0].children.contains(panel + "/modulos"));
        const auto inside = scanDirectories(panel + "/modulos", true, std::make_shared<std::atomic_bool>(false), {panel + "/modulos"});
        QCOMPARE(inside.directories.size(), 1);
        QCOMPARE(inside.directories[0].files.size(), 1);
    }
    void locationRestoreAndErrors()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_End);
        const QString selected = window.session().currentFile()->path;
        const auto tags = window.session().tags;
        QVERIFY(window.selectRoot(root + "/beta")); QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().root, root + "/beta");
        QVERIFY(window.session().tags.isEmpty());
        QVERIFY(!window.selectRoot(root + "/inexistente"));
        QCOMPARE(window.session().root, root + "/beta");
        QVERIFY(window.selectRoot(panel));
        QCOMPARE(window.session().currentFile()->path, selected);
        QCOMPARE(window.session().tags, tags);
        QCOMPARE(window.session().view, View::Directory);
    }
    void logKeyboardPrompt()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_L);
        capture(window, "log-puntos-montaje");
        QTest::keyClicks(&window, "noexiste_ubicacion_xyz"); QTest::keyClick(&window, Qt::Key_Return);
        QVERIFY(window.status().contains("No location"));
        QTest::keyClick(&window, Qt::Key_Backspace, Qt::ControlModifier);
        QTest::keyClicks(&window, qPrintable(root + "/beta"));
        QTest::keyClick(&window, Qt::Key_Return); QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().root, root + "/beta");
        QTest::keyClick(&window, Qt::Key_L, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_F5);
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(window.session().root, root + "/beta");
        QTest::keyClick(&window, Qt::Key_L);
        QTest::keyClicks(&window, qPrintable(panel)); QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().root, panel);
    }
    void splitDifferentLocationsAndMerge()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_F8);
        QVERIFY(window.selectRoot(root + "/beta")); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        const auto betaTags = window.session().tags;
        QTest::keyClick(&window, Qt::Key_F6, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.session().root, panel);
        QCOMPARE(window.session().files().size(), 3);
        QCOMPARE(window.oppositeSession()->root, root + "/beta");
        capture(window, "split-ubicaciones");
        QVERIFY(window.selectRoot(root + "/beta")); QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().tags, betaTags);
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.session().root, root + "/beta");
        QCOMPARE(window.session().tags, betaTags);
        QVERIFY(window.selectRoot(panel));
        QVERIFY(window.session().tags.isEmpty());
        QTest::keyClick(&window, Qt::Key_F8);
        QCOMPARE(window.session().root, panel);
    }
    void splitIndependentStateAndUnsplit()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        const auto originalTags = window.session().tags;
        QTest::keyClick(&window, Qt::Key_F8);
        QVERIFY(window.isSplit()); QCOMPARE(window.activeSide(), 0);
        QCOMPARE(window.oppositeSession()->tags, originalTags);
        QTest::keyClick(&window, Qt::Key_U, Qt::ControlModifier);
        QVERIFY(window.session().tags.isEmpty());
        QCOMPARE(window.oppositeSession()->tags, originalTags);
        QTest::keyClick(&window, Qt::Key_F); QTest::keyClicks(&window, "*.php"); QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().files().size(), 2);
        QTest::keyClick(&window, Qt::Key_End);
        const QString leftFile = window.session().currentFile()->path;
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.activeSide(), 1);
        QCOMPARE(window.session().files().size(), 3);
        QCOMPARE(window.session().tags, originalTags);
        QTest::keyClick(&window, Qt::Key_F); QTest::keyClicks(&window, ".htaccess"); QTest::keyClick(&window, Qt::Key_Return);
        capture(window, "split-filtros-independientes");
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.session().currentFile()->path, leftFile);
        QVERIFY(window.session().tags.isEmpty());
        QTest::keyClick(&window, Qt::Key_Tab);
        QTest::keyClick(&window, Qt::Key_F8);
        QVERIFY(!window.isSplit());
        QCOMPARE(window.session().filespec.text(), QString(".htaccess"));
        QCOMPARE(window.session().tags, originalTags);
    }
    void splitMergeSwapAndHistory()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_F8);
        QTest::keyClick(&window, Qt::Key_F); QTest::keyClicks(&window, "*.php"); QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_F6, Qt::ControlModifier);
        QVERIFY(window.oppositeSession()->tags.isEmpty());
        QTest::keyClick(&window, Qt::Key_F8, Qt::ShiftModifier); QTest::keyClick(&window, Qt::Key_S);
        QCOMPARE(window.activeSide(), 1);
        QCOMPARE(window.session().tags.size(), 2);
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.activeSide(), 0);
        QCOMPARE(window.session().tags.size(), 2);
        QCOMPARE(window.session().files().size(), 3);
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(window.session().filespec.text(), QString("*.php"));
        QTest::keyClick(&window, Qt::Key_U, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_F8);
        QVERIFY(window.session().tags.isEmpty());
    }
    void splitSharedLoggingAndCollapse()
    {
        Session left(root); left.apply(scan(root, true), true);
        Session right = left;
        right.selectDirectory(panel); right.setTag(true, true);
        const auto tags = right.tags;
        left.selectDirectory(alpha); left.toggleCollapse(false);
        right.syncLoggingFrom(left);
        QCOMPARE(right.directory, alpha);
        QCOMPARE(right.tags, tags);
        left.unlog(); right.syncLoggingFrom(left);
        QVERIFY(!right.currentDirectory().loaded);
        QVERIFY(!right.tags.contains(panel + "/ordenes.php"));
        QVERIFY(right.tags.contains(root + "/README.txt"));
        left.apply(scan(alpha, true), true); right.syncLoggingFrom(left);
        QVERIFY(right.directories().value(panel).loaded);
        QCOMPARE(right.directory, alpha);
    }
    void splitNavigationAndMouse()
    {
        TreeWindow window(root); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Asterisk); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(window.session().directory, alpha);
        QTest::keyClick(&window, Qt::Key_F8);
        QTest::keyClick(&window, Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(window.session().directory, root + "/beta");
        QTest::keyClick(&window, Qt::Key_Backtab, Qt::ShiftModifier);
        QCOMPARE(window.session().directory, alpha);
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.activeSide(), 1);
        QCOMPARE(window.session().directory, alpha);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(20, 40));
        QCOMPARE(window.activeSide(), 0);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(window.width() * 3 / 4, 40));
        QCOMPARE(window.activeSide(), 1);
    }
    void splitResizeAndStats()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_F8);
        const QString file = window.session().currentFile()->path;
        QTest::keyClick(&window, Qt::Key_F8, Qt::ShiftModifier); QTest::keyClick(&window, Qt::Key_B);
        capture(window, "split-estadisticas-ambos");
        window.resize(window.minimumSize()); QTRY_COMPARE(window.columns(), 80);
        QCOMPARE(window.session().currentFile()->path, file);
        capture(window, "split-80x25");
        QTest::keyClick(&window, Qt::Key_F8, Qt::ShiftModifier); QTest::keyClick(&window, Qt::Key_B);
        QVERIFY(window.status().contains("unavailable"));
        QTest::keyClick(&window, Qt::Key_Escape);
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.activeSide(), 1);
        QCOMPARE(window.session().currentFile()->path, file);
        QSignalSpy opened(&window, &TreeWindow::openRequested);
        QTest::keyClick(&window, Qt::Key_O);
        QCOMPARE(opened.first().at(0).toString(), file);
        QVERIFY(window.isSplit());
        QTest::keyClick(&window, Qt::Key_Return, Qt::AltModifier);
        QVERIFY(window.isFullScreen());
        QTest::keyClick(&window, Qt::Key_Return, Qt::AltModifier);
        QTest::keyClick(&window, Qt::Key_F8);
        QVERIFY(!window.isSplit());
    }
    void splitSearchAndSharedScan()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_F8);
        QTest::keyClick(&window, Qt::Key_Asterisk);
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.activeSide(), 0);
        QTRY_VERIFY(!window.busy());
        QVERIFY(!window.oppositeSession()->directories().value(panel + "/modulos").loaded);
        QTest::keyClick(&window, Qt::Key_Tab);
        QVERIFY(window.session().directories().value(panel + "/modulos").loaded);
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_F8);
        QVERIFY(window.isSplit());
        QTest::keyClicks(&window, "ordenes");
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().tags.size(), 2);
        QVERIFY(window.oppositeSession()->tags.isEmpty());
        capture(window, "split-busqueda");
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.session().view, View::Tree);
        QVERIFY(window.session().tags.isEmpty());
        QTest::keyClick(&window, Qt::Key_Minus);
        QTest::keyClick(&window, Qt::Key_Tab);
        QVERIFY(window.session().tags.isEmpty());
        QVERIFY(window.session().files().isEmpty());
    }
    void filespecSizeComparisons()
    {
        Filespec spec;
        FileEntry file; file.name = "a.php";
        const auto check = [&](QString pattern, QList<qint64> yes, QList<qint64> no) {
            QString error;
            QVERIFY2(spec.set(pattern, &error), qPrintable(error));
            for (auto size : yes) { file.size = size; QVERIFY2(spec.matches(file), qPrintable(pattern + " incluye " + QString::number(size))); }
            for (auto size : no) { file.size = size; QVERIFY2(!spec.matches(file), qPrintable(pattern + " excluye " + QString::number(size))); }
        };
        check("=>s10,<=s20", {10, 15, 20}, {0, 9, 21});
        check(">s10,<s20", {11, 19}, {10, 20});
        check("<s10,>s20", {0, 9, 21}, {10, 15, 20});
        check("-<s10,->s20,<>s15", {10, 14, 20}, {0, 15, 21});
        check("-=>s10,-<=s20", {0, 9, 21}, {10, 15, 20});
        check("-=s0", {1, 9223372036854775807LL}, {0});
        check("=s0,=s20", {0, 20}, {1, 21});
        check("=>s9223372036854775806", {9223372036854775806LL, 9223372036854775807LL}, {0});
        check(">s10,*.php,<s20", {0, 15, 25}, {});
        check(">s10,<s20,>s30,<s40", {11, 19, 31, 39}, {0, 20, 25, 40});
    }
    void filespecDateComparisons()
    {
        Filespec spec;
        FileEntry file; file.name = "a.txt";
        const auto check = [&](QString pattern, QList<int> yes, QList<int> no) {
            QString error;
            QVERIFY2(spec.set(pattern, &error), qPrintable(error));
            for (int day : yes) {
                file.modified = QDateTime(QDate(2006, 5, 1).addDays(day - 1), QTime(23, 59));
                QVERIFY2(spec.matches(file), qPrintable(pattern + " incluye día " + QString::number(day)));
            }
            for (int day : no) {
                file.modified = QDateTime(QDate(2006, 5, 1).addDays(day - 1), QTime(0, 0));
                QVERIFY2(!spec.matches(file), qPrintable(pattern + " excluye día " + QString::number(day)));
            }
        };
        check("=>5-1-06,=<5-31-06", {1, 10, 31}, {0, 32});
        check("<5-1-2006,>5-31-2006", {0, 32}, {1, 10, 31});
        check("-<5/1/2006,->5/31/2006,<>5/10/2006", {1, 9, 11, 31}, {0, 10, 32});
        check("=5.10.2006", {10}, {9, 11});
        QVERIFY(spec.set("=2-29-2024"));
        file.modified = QDateTime(QDate(2024, 2, 29), QTime(12, 0));
        QVERIFY(spec.matches(file));
        file.modified = {};
        QVERIFY(!spec.matches(file));
    }
    void filespecTodayAndCombinedTypes()
    {
        Filespec spec;
        FileEntry file; file.name = "ordenes.php"; file.size = 150;
        const QDate today(2026, 10, 1);
        file.modified = QDateTime(today, QTime(12, 0));
        QVERIFY(spec.set("*.php,=>TODAY-7,<=TODAY,=>s100,<=s200"));
        QVERIFY(spec.usesToday());
        QVERIFY(spec.matches(file, today));
        QVERIFY(!spec.matches(file, today.addDays(8)));
        file.name = "ordenes.txt"; QVERIFY(!spec.matches(file, today));
        file.name = "ordenes.php"; file.size = 201; QVERIFY(!spec.matches(file, today));
        file.size = 150; file.modified = QDateTime(today.addDays(-8), QTime(12, 0));
        QVERIFY(!spec.matches(file, today));
        spec.inverted = true; QVERIFY(spec.matches(file, today));
        QVERIFY(spec.set("TODAY"));
        file.modified = QDateTime(today, QTime(0, 0));
        QVERIFY(spec.matches(file, today));
        QVERIFY(!spec.matches(file, today.addDays(1)));
        QVERIFY(spec.set("\"TODAY\",\"=s100\",TODAY.php"));
        QVERIFY(!spec.usesToday());
        QVERIFY(spec.matches(QString("TODAY")));
        QVERIFY(spec.matches(QString("=s100")));
        QVERIFY(spec.matches(QString("TODAY.php")));
    }
    void filespecInvalidMetadataPreservesFilter()
    {
        Filespec spec; QVERIFY(spec.set("*.php,=>s100")); spec.inverted = true;
        for (QString bad : {"=2-29-2023", "=13-1-2026", "=1/2-2026", "=1-2-1", "=TODAY-abc", "=TODAY-9999999999999999", "=s-1", "=s1.5", "=s9223372036854775808", "=>=s10", "<", "=s", "*.php,>sabc"}) {
            QString error;
            QVERIFY2(!spec.set(bad, &error), qPrintable(bad));
            QVERIFY(!error.isEmpty());
            QCOMPARE(spec.text(), QString("*.php,=>s100"));
            QVERIFY(spec.inverted);
        }
    }
    void filespecMetadataKeyboardSearch()
    {
        TreeWindow window(panel); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_F);
        QTest::keyClicks(&window, "*.php,=TODAY,=s8");
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().files().size(), 1);
        QCOMPARE(window.session().currentFile()->name, QString("ordenes.php"));
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QCOMPARE(window.session().tags.size(), 1);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QTest::keyClicks(&window, "ordenes"); QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().tags.size(), 1);
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(window.session().view, View::Showall);
        QCOMPARE(window.session().files().size(), 1);
        capture(window, "filespec-fecha-tamano");
        QTest::keyClick(&window, Qt::Key_F); QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().tags.size(), 1);
    }
    void filespecNameRules()
    {
        Filespec spec;
        const auto check = [&](const QString &pattern, const QStringList &yes, const QStringList &no) {
            QString error;
            QVERIFY2(spec.set(pattern, &error), qPrintable(error));
            for (const auto &name : yes) QVERIFY2(spec.matches(name), qPrintable(pattern + " debe incluir " + name));
            for (const auto &name : no) QVERIFY2(!spec.matches(name), qPrintable(pattern + " debe excluir " + name));
        };
        check("*.*", {".htaccess", "archivo", "a.tar.gz"}, {});
        check("*.htm*", {"index.HTML"}, {".html", "html"});
        check(".htaccess", {".htaccess"}, {".htaccess.bak"});
        check("archivo", {"archivo"}, {"archivo.php"});
        check("*.", {"archivo", ".htaccess"}, {"a.php"});
        check("*.tar.gz.", {"a.tar.gz"}, {"a.gz"});
        check("AB???.*", {"ab", "abcde.txt"}, {"abcdef.txt"});
        check("A*.??", {"a.c", "abc.hi"}, {"a", "abc.php"});
        check("???.php,-??.php", {"abc.php"}, {"ab.php", "abcd.php"});
        check("A??*G*.*", {"abcg", "abcdefgh.php"}, {"abg.php"});
        check("ABC◄1-3►.TXT", {"abc1.txt", "ABC3.TXT"}, {"abc4.txt"});
        check("Web: *.php;*.html,-admin*.*", {"index.php", "index.html"}, {"admin.php", "a.css"});
        check("-*.php,-*.html", {"a.css", "archivo"}, {"index.php"});
        check("\"a b;*.txt\",\"-*.php\",\"=abc\"", {"a b;c.txt", "-hola.php", "=abc"}, {"a.txt"});
        spec.inverted = true;
        QVERIFY(spec.matches("a.txt"));
        const QString previous = spec.text();
        QString error;
        for (const QString bad : {"*?.php", "\"abc", ">sABC", "ABC◄Z-A►.*"}) {
            QVERIFY(!spec.set(bad, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(spec.text(), previous);
            QVERIFY(spec.inverted);
        }
    }
    void filespecScopesAndMarks()
    {
        Session session(root);
        session.apply(scan(root, true), true);
        session.setTag(true, true);
        const auto allTags = session.tags;
        QVERIFY(session.filespec.set("*.php,-usuarios.*"));
        session.rebuild();
        session.setTag(false, true);
        QVERIFY(session.tags.contains(panel + "/usuarios.php"));
        QVERIFY(session.tags.contains(root + "/README.txt"));
        QVERIFY(!session.tags.contains(panel + "/ordenes.php"));
        session.tags = allTags;
        session.selectDirectory(panel);
        session.enter(View::Directory);
        QCOMPARE(session.files().size(), 1);
        session.enter(View::Branch);
        QCOMPARE(session.files().size(), 2);
        session.enter(View::Showall, true);
        QCOMPARE(session.files().size(), 3);
        QCOMPARE(session.tags, allTags);
        QVERIFY(session.filespec.set("*.nothing"));
        session.rebuild();
        QVERIFY(session.files().isEmpty());
        QCOMPARE(session.tags, allTags);
    }
    void filespecKeyboardAndHistory()
    {
        TreeWindow window(panel); window.show();
        QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        const auto tags = window.session().tags;
        QTest::keyClick(&window, Qt::Key_F);
        QTest::keyClicks(&window, "*.php,-usuarios.*");
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().files().size(), 1);
        QCOMPARE(window.session().tags, tags);
        capture(window, "filespec-nombres");
        QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_F);
        QCOMPARE(window.session().files().size(), 2);
        QVERIFY(window.session().filespec.inverted);
        QTest::keyClick(&window, Qt::Key_F);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().files().size(), 3);
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(window.session().files().size(), 1);
        QTest::keyClick(&window, Qt::Key_F);
        QTest::keyClicks(&window, ".htaccess");
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().currentFile()->name, QString(".htaccess"));
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(window.session().currentFile()->name, QString("ordenes.php"));
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(window.session().currentFile()->name, QString(".htaccess"));
        QTest::keyClick(&window, Qt::Key_F);
        QTest::keyClicks(&window, "*?.php");
        QTest::keyClick(&window, Qt::Key_Return);
        QVERIFY(window.status().contains("invalid"));
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(window.session().filespec.text(), QString(".htaccess"));
    }
    void continuousConsoleBorders()
    {
        TreeWindow window(panel); window.show();
        QTRY_VERIFY(!window.busy());
        for (QSize size : {QSize(1280, 768), QSize(731, 463)}) {
            window.resize(size);
            QTRY_COMPARE(window.size(), size);
            QTest::qWait(30);
            const auto image = window.grab().toImage();
            const qreal ratio = image.devicePixelRatio();
            const QColor cyan(0, 255, 255);
            const int cw = window.minimumWidth() / 80, ch = window.minimumHeight() / 25;
            const int end = window.rows() - 5;
            for (int x = cw / 2; x <= (window.columns() - 1) * cw + cw / 2; ++x)
                QCOMPARE(image.pixelColor(qRound(x * ratio), qRound((ch + ch / 2) * ratio)), cyan);
            for (int y = ch + ch / 2; y <= end * ch + ch / 2; ++y)
                QCOMPARE(image.pixelColor(qRound(((window.columns() - 1) * cw + cw / 2) * ratio), qRound(y * ratio)), cyan);
        }
        capture(window, "bordes-continuos");
    }
    void init()
    {
        priorHistorySet_=qEnvironmentVariableIsSet("LTREE_HISTORY_FILE");priorHistoryFile_=qgetenv("LTREE_HISTORY_FILE");
        priorShellHistorySet_=qEnvironmentVariableIsSet("HISTFILE");priorShellHistoryFile_=qgetenv("HISTFILE");
        fixture = std::make_unique<QTemporaryDir>(QDir::currentPath() + "/ltree-test-XXXXXX");
        QVERIFY(fixture->isValid());
        root = fixture->path();
        qputenv("LTREE_HISTORY_FILE",QFile::encodeName(root+"/search-history.json"));
        // Los shells de prueba no deben cargar ni sobrescribir el historial personal del usuario.
        qputenv("HISTFILE",QFile::encodeName(root+"/shell-history"));
        alpha = root + "/alpha";
        panel = alpha + "/public_html/sublimepanel";
        QVERIFY(QDir().mkpath(panel + "/modulos"));
        QVERIFY(QDir().mkpath(root + "/beta/vacio"));
        write(root + "/README.txt");
        write(panel + "/.htaccess");
        write(panel + "/ordenes.php");
        write(panel + "/usuarios.php", "usuarios\n");
        write(panel + "/modulos/interno.php");
        write(root + "/beta/externo.php");
    }
    void cleanup()
    {
        if(priorHistorySet_)qputenv("LTREE_HISTORY_FILE",priorHistoryFile_);else qunsetenv("LTREE_HISTORY_FILE");
        if(priorShellHistorySet_)qputenv("HISTFILE",priorShellHistoryFile_);else qunsetenv("HISTFILE");
    }
    void shallowAndRecursiveLoading()
    {
        Session session(root);
        session.apply(scan(root), true);
        QCOMPARE(session.tree().size(), 3);
        QCOMPARE(session.files().size(), 1);
        QVERIFY(!session.directories().value(alpha).loaded);
        session.apply(scan(root, true), true);
        QCOMPARE(session.tree().size(), 7);
        QVERIFY(session.directories().value(panel).loaded);
        QCOMPARE(session.directories().value(panel).files.size(), 3);
    }
    void collapsePreservesLoadedFilesAndTags()
    {
        Session session(root);
        session.apply(scan(root, true), true);
        session.selectDirectory(alpha);
        session.tags.insert(panel + "/ordenes.php");
        session.toggleCollapse(false);
        QCOMPARE(session.tree().size(), 4);
        QVERIFY(session.directories().value(panel).loaded);
        QVERIFY(session.tags.contains(panel + "/ordenes.php"));
        session.enter(View::Branch, true);
        QCOMPARE(session.files().size(), 1);
        session.returnToTree();
        session.toggleCollapse(false);
        QCOMPARE(session.tree().size(), 7);
        session.unlog();
        QVERIFY(!session.currentDirectory().loaded);
        QVERIFY(!session.directories().contains(panel));
        QVERIFY(session.tags.isEmpty());
        QVERIFY(session.directories().contains(root + "/beta/vacio"));
    }
    void secondLevelCollapse()
    {
        Session session(root);
        session.apply(scan(root, true), true);
        session.toggleCollapse(true);
        QCOMPARE(session.tree().size(), 3);
        session.toggleCollapse(true);
        QCOMPARE(session.tree().size(), 7);
    }
    void branchAndShowallScopes()
    {
        Session session(root);
        session.apply(scan(root, true), true);
        session.selectDirectory(panel);
        session.enter(View::Directory);
        QCOMPARE(session.files().size(), 3);
        session.setTag(true, true);
        session.tags.insert(root + "/beta/externo.php");
        session.returnToTree();
        session.enter(View::Branch, true);
        QCOMPARE(session.files().size(), 3);
        session.returnToTree();
        session.enter(View::Showall, true);
        QCOMPARE(session.files().size(), 4);
        QCOMPARE(session.directory, panel);
    }
    void tagsOnlyRetainsUntaggedUntilRefinement()
    {
        Session session(root);
        session.apply(scan(root, true), true);
        session.selectDirectory(panel);
        session.enter(View::Directory);
        session.tags.insert(panel + "/ordenes.php");
        session.tags.insert(panel + "/usuarios.php");
        session.toggleTagsOnly();
        QCOMPARE(session.files().size(), 2);
        session.setTag(false, false);
        QCOMPARE(session.files().size(), 2);
        session.toggleTagsOnly();
        QCOMPARE(session.files().size(), 1);
        session.toggleTagsOnly();
        QVERIFY(!session.tagsOnly);
        QCOMPARE(session.files().size(), 3);
        session.setTag(false, true);
        session.toggleTagsOnly();
        QVERIFY(!session.tagsOnly);
    }
    void refreshVersusRelog()
    {
        Session session(root);
        session.apply(scan(root, true), true);
        session.tags.insert(panel + "/ordenes.php");
        session.tags.insert(root + "/beta/externo.php");
        write(panel + "/nuevo.php");
        session.apply(scan(panel), true);
        QVERIFY(session.tags.contains(panel + "/ordenes.php"));
        QCOMPARE(session.directories().value(panel).files.size(), 4);
        session.apply(scan(panel), false);
        QVERIFY(!session.tags.contains(panel + "/ordenes.php"));
        QVERIFY(session.tags.contains(root + "/beta/externo.php"));
    }
    void deletedBranchesAreRemoved()
    {
        Session session(root);
        session.apply(scan(root, true), true);
        session.tags.insert(root + "/beta/externo.php");
        QVERIFY(QDir(root + "/beta").removeRecursively());
        session.apply(scan(root), true);
        QVERIFY(!session.directories().contains(root + "/beta"));
        QVERIFY(!session.directories().contains(root + "/beta/vacio"));
        QVERIFY(session.tags.isEmpty());
    }
    void symlinkLoopDoesNotRecurse()
    {
        QVERIFY(QFile::link(root, panel + "/loop"));
        const auto result = scan(root, true);
        QCOMPARE(result.directories.size(), 7);
        const auto link = scan(panel + "/loop");
        QVERIFY(!link.directories.first().error.isEmpty());
    }
    void cancellationAndPathBoundary()
    {
        auto cancel = std::make_shared<std::atomic_bool>(true);
        const auto result = scanDirectories(root, true, cancel);
        QVERIFY(result.cancelled);
        QVERIFY(result.directories.isEmpty());
        QVERIFY(isWithin("/a/b", "/a"));
        QVERIFY(!isWithin("/ab", "/a"));
        QVERIFY(isWithin("/a", "/"));
    }
    void navigationAndSpell()
    {
        Session session(root);
        session.apply(scan(root, true), true);
        QVERIFY(session.spell("alp"));
        QCOMPARE(session.directory, alpha);
        session.parent(false);
        QCOMPARE(session.directory, alpha);
        session.parent(true);
        QCOMPARE(session.directory, root);
        QVERIFY(session.spell("sublime"));
        QCOMPARE(session.directory, panel);
        session.enter(View::Directory);
        QVERIFY(session.spell("usua"));
        QCOMPARE(session.currentFile()->name, QString("usuarios.php"));
        QVERIFY(!session.spell("no-existe"));
    }
    void keyboardFlowAndOpenSignal()
    {
        TreeWindow window(root);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 5000);
        QTest::keyClick(&window, Qt::Key_Asterisk);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 5000);
        QTest::keyClick(&window, Qt::Key_S, Qt::ShiftModifier);
        QTest::keyClick(&window, Qt::Key_U, Qt::ShiftModifier);
        QCOMPARE(window.session().directory, panel);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().view, View::Directory);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QCOMPARE(window.session().tags.size(), 3);
        QTest::keyClick(&window, Qt::Key_Down);
        QSignalSpy opened(&window, &TreeWindow::openRequested);
        QTest::keyClick(&window, Qt::Key_O);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.first().at(0).toString(), panel + "/ordenes.php");
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(window.session().view, View::Directory);
        QTest::keyClicks(&window, "ordenes");
        capture(window, "busqueda-prompt");
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 5000);
        QCOMPARE(window.session().tags.size(), 2);
        QCOMPARE(window.session().files().size(), 3);
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(window.session().view, View::Showall);
        QCOMPARE(window.session().files().size(), 2);
        capture(window, "listado-marcados");
    }
    void enterFirstLoadsThenOpens()
    {
        TreeWindow window(root);
        window.show();
        QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_B, Qt::ShiftModifier);
        QCOMPARE(window.session().directory, root + "/beta");
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY(!window.busy());
        QCOMPARE(window.session().view, View::Tree);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().view, View::Directory);
    }
    void resizeFullscreenAndRestore()
    {
        TreeWindow window(root);
        window.show();
        QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Asterisk);
        QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_Down);
        const QString selection = window.session().directory;
        const auto tags = window.session().tags;
        window.resize(1440, 900);
        QTest::qWait(30);
        const int wide = window.columns();
        capture(window, "arbol-grande");
        window.resize(window.minimumSize());
        QTest::qWait(30);
        QVERIFY(window.columns() < wide);
        QCOMPARE(window.columns(), 80);
        QCOMPARE(window.rows(), 25);
        capture(window, "arbol-80x25");
        QTest::keyClick(&window, Qt::Key_Return, Qt::AltModifier);
        QVERIFY(window.isFullScreen());
        QTest::keyClick(&window, Qt::Key_Return, Qt::AltModifier);
        QVERIFY(!window.isFullScreen());
        window.showMinimized();
        window.showNormal();
        QTest::qWait(30);
        QCOMPARE(window.session().directory, selection);
        QCOMPARE(window.session().tags, tags);
    }
    void modifierFocusAndPromptedSearch()
    {
        TreeWindow window(root);
        window.show();
        QTRY_VERIFY(!window.busy());
        QTest::keyPress(&window, Qt::Key_Control);
        QFocusEvent lost(QEvent::FocusOut);
        QApplication::sendEvent(&window, &lost);
        QTest::keyClick(&window, Qt::Key_Bar);
        QTest::keyClicks(&window, "beta");
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().directory, root + "/beta");
        QTest::keyClick(&window, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(window.session().directory, root + "/beta");
        QVERIFY(window.status().contains("not implemented yet"));
        QTest::keyClick(&window, Qt::Key_F1);
        capture(window, "ayuda");
        QTest::keyClick(&window, Qt::Key_Escape);
        QTest::keyClick(&window, Qt::Key_F11);
        QVERIFY(window.status().contains("not implemented yet"));
    }
    void searchReportsRealReadProgressAndCancellation()
    {
        const auto path = root + "/progress.bin";
        write(path, QByteArray(3 * 65536 + 7, 'x'));
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        QVector<qint64> positions;
        auto result = searchFile(path, {"missing", SearchMode::Text, true}, cancel,
                                 [&](qint64 bytes) { positions.append(bytes); });
        QCOMPARE(result.outcome, SearchOutcome::NotFound);
        QCOMPARE(positions, (QVector<qint64>{0, 65536, 131072, 196608, 196615}));
        positions.clear();
        result = searchFile(path, {"missing", SearchMode::Text, true}, cancel,
                            [&](qint64 bytes) { positions.append(bytes); if (bytes == 131072) cancel->store(true); });
        QCOMPARE(result.outcome, SearchOutcome::Cancelled);
        QCOMPARE(positions, (QVector<qint64>{0, 65536, 131072}));
        cancel->store(false);
        write(path, "found" + QByteArray(3 * 65536, 'x'));
        positions.clear();
        QCOMPARE(searchFile(path, {"found", SearchMode::Text, true}, cancel,
                            [&](qint64 bytes) { positions.append(bytes); }).outcome, SearchOutcome::Found);
        QCOMPARE(positions, (QVector<qint64>{0, 65536}));
        write(path, "");positions.clear();
        QCOMPARE(searchFile(path, {"missing", SearchMode::Text, true}, cancel,
                            [&](qint64 bytes) { positions.append(bytes); }).outcome, SearchOutcome::NotFound);
        QCOMPARE(positions, (QVector<qint64>{0}));
    }
    void searchSelectorSpinnerAndErrorHits()
    {
        const auto base = root + "/search-progress";QVERIFY(QDir().mkpath(base));
        for (int i = 0; i < 32; ++i) write(base + QString("/file_%1.txt").arg(i, 2, 10, QChar('0')));
        TreeWindow window(base);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);QTest::keyClick(&window, Qt::Key_F8);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        const auto oppositeIndex = window.oppositeSession()->fileIndex;
        const auto oppositeTags = window.oppositeSession()->tags;
        QVERIFY(QFile::remove(base + "/file_31.txt"));
        auto *pool = QThreadPool::globalInstance();const int threads = pool->maxThreadCount();pool->setMaxThreadCount(1);
        QSemaphore release, started;auto blocker = QtConcurrent::run(pool, [&] { started.release();release.acquire(); });
        const auto cleanup = qScopeGuard([&] { release.release();blocker.waitForFinished();pool->setMaxThreadCount(threads); });
        QVERIFY(started.tryAcquire(1, 2000));
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);QTest::keyClicks(&window, "ordenes");QTest::keyClick(&window, Qt::Key_Return);
        QVERIFY(window.busy());QCOMPARE(window.session().fileIndex, 0);
        const QRect spinner(0, (window.rows()-2)*16, 8, 16);
        const auto first = window.grab().toImage().copy(spinner);
        QTRY_VERIFY(window.grab().toImage().copy(spinner) != first);
        capture(window, "search-progress-spinner-split");
        release.release();
        QTRY_VERIFY2(window.status().contains("Not a regular file"), qPrintable(window.status()));
        QCOMPARE(window.session().currentFile()->path, base + "/file_31.txt");
        QCOMPARE(window.oppositeSession()->fileIndex, oppositeIndex);
        QCOMPARE(window.oppositeSession()->tags, oppositeTags);
        QTest::keyClick(&window, Qt::Key_K);QVERIFY(!window.busy());
        QCOMPARE(window.session().fileIndex, 31);
        QVERIFY(window.status().contains("Hits: 31"));QCOMPARE(window.session().tags.size(), 32);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);QTest::keyClick(&window, Qt::Key_F3);QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY2(window.status().contains("Not a regular file"), qPrintable(window.status()));QTest::keyClick(&window, Qt::Key_Escape);
        QVERIFY(window.status().contains("Search cancelled"));QVERIFY(window.status().contains("Hits: 31"));
    }
    void searchProgressWithinLargeFileAndCompactFooter()
    {
        const auto base = root + "/search-reading";QVERIFY(QDir().mkpath(base));
        write(base + "/a-hit.txt", "ordenes");
        write(base + "/z-large.bin", QByteArray(8 * 1024 * 1024, 'x'));
        TreeWindow window(base);window.resize(1280, 800);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);QTest::keyClicks(&window, "ord*enes");QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY(window.session().currentFile()->path == base + "/z-large.bin");
        const auto barPixels = [&] {
            const auto image = window.grab().toImage();int count = 0;
            const int y = (window.rows()-2)*16 + 6;
            for (int x = 16; x < 16 + 4*8; ++x) if (image.pixelColor(x,y) == QColor(0,255,255)) ++count;
            return count;
        };
        QTRY_VERIFY(barPixels() > 24);QVERIFY(window.busy());
        const auto footerImage = window.grab().toImage();
        int cancelKeyPixels = 0;
        for (int y = (window.rows()-2)*16; y < (window.rows()-1)*16; ++y)
            for (int x = (window.columns()-11)*8; x < (window.columns()-8)*8; ++x)
                if (footerImage.pixelColor(x,y) == QColor(255,255,0)) ++cancelKeyPixels;
        QVERIFY(cancelKeyPixels > 0);
        capture(window, "search-progress-reading-wide");
        window.resize(window.minimumSize());QCOMPARE(window.columns(), 80);
        capture(window, "search-progress-reading-80");
        QTest::keyClick(&window, Qt::Key_Escape);QTRY_VERIFY(!window.busy());
        QVERIFY2(window.status().contains("Hits: 1"), qPrintable(window.status()));
        QCOMPARE(window.session().tags.size(), 2);
        QCOMPARE(window.session().currentFile()->path, base + "/z-large.bin");
    }
    void regexSearchSyntaxAndLines_data()
    {
        QTest::addColumn<QString>("content"); QTest::addColumn<QString>("pattern");
        QTest::addColumn<bool>("sensitive"); QTest::addColumn<bool>("found");
        const auto row=[](const char *name,const QString &content,const QString &pattern,bool sensitive,bool found) { QTest::newRow(name)<<content<<pattern<<sensitive<<found; };
        row("alternation","info\nWARNING: disk\n","error|warning",false,true);
        row("case-sensitive","WARNING","warning",true,false);
        row("line-anchors","skip\nORDER_123\nend\n","^ORDER_[0-9]+$",true,true);
        row("crlf-anchors","skip\r\nORDER_123\r\n","^ORDER_[0-9]+$",true,true);
        row("cr-anchors","skip\rORDER_123\rend","^ORDER_[0-9]+$",true,true);
        row("no-multiline","error\nwarning","(?s)error.*warning",true,false);
        row("unicode-class",QString::fromUtf8("árbol ٢٣"),"^\\p{L}+ \\d+$",true,true);
        row("unicode-fold",QString::fromUtf8("ÁRBOL"),QString::fromUtf8("^árbol$"),false,true);
        row("lookbehind","ID:42","(?<=ID:)\\d+",true,true);
        row("backreference","go go","^(\\w+) \\1$",true,true);
        row("literal-metachar","a+b","a\\+b",true,true);
        row("empty-file","","^$",true,true);
        row("empty-line","a\n\nb\n","^$",true,true);
        row("no-phantom-crlf-line","a\r\nb\r\n","^$",true,false);
        row("zero-length","one two","(?=two)",true,true);
    }
    void regexSearchSyntaxAndLines()
    {
        QFETCH(QString,content); QFETCH(QString,pattern); QFETCH(bool,sensitive); QFETCH(bool,found);
        const auto path=root+"/regex.txt"; write(path,content.toUtf8());
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        const auto result=searchFile(path,{pattern,SearchMode::Regex,sensitive},cancel);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QCOMPARE(result.outcome,found?SearchOutcome::Found:SearchOutcome::NotFound);
    }
    void regexStreamingValidationAndLimits()
    {
        const auto path=root+"/regex-large.txt"; const auto cancel=std::make_shared<std::atomic_bool>(false);
        write(path,QByteArray(65534,'x')+"ORDER_42\n");
        QCOMPARE(searchFile(path,{"ORDER_\\d+$",SearchMode::Regex,true},cancel).outcome,SearchOutcome::Found);
        write(path,QByteArray(65535,'x')+"\r\nORDER_42\r\n");
        QCOMPARE(searchFile(path,{"^ORDER_\\d+$",SearchMode::Regex,true},cancel).outcome,SearchOutcome::Found);
        write(path,QByteArray(65535,'x')+QString::fromUtf8("árbol").toUtf8());
        QCOMPARE(searchFile(path,{QString::fromUtf8("árb.l$"),SearchMode::Regex,true},cancel).outcome,SearchOutcome::Found);
        for(const auto &bytes:{QByteArray::fromHex("fffe490044003a0034003200"),QByteArray::fromHex("feff00490044003a00340032"),
             QByteArray::fromHex("fffe000049000000440000003a0000003400000032000000"),QByteArray::fromHex("0000feff00000049000000440000003a0000003400000032")}) {
            write(path,bytes); QCOMPARE(searchFile(path,{"^ID:\\d+$",SearchMode::Regex,true},cancel).outcome,SearchOutcome::Found);
        }
        write(path,QByteArray("none")+QByteArray::fromHex("c3"));
        QCOMPARE(searchFile(path,{"ID",SearchMode::Regex,false},cancel).outcome,SearchOutcome::Error);
        const auto invalid=validateSearch({"([",SearchMode::Regex,false}); QVERIFY(invalid.startsWith("Invalid regex at character"));
        write(path,"anything"); QCOMPARE(searchFile(path,{"([",SearchMode::Regex,false},cancel).errorType,4);
        write(path,QByteArray(150000,'x')); int progress=0;
        const auto cancelled=searchFile(path,{"not-found",SearchMode::Regex,true},cancel,[&](qint64 bytes){++progress;if(bytes>=65536)cancel->store(true);});
        QCOMPARE(cancelled.outcome,SearchOutcome::Cancelled); QVERIFY(progress>=2); cancel->store(false);
        write(path,QByteArray(RegexLineLimit+1,'x'));
        const auto tooLong=searchFile(path,{"needle",SearchMode::Regex,true},cancel); QCOMPARE(tooLong.outcome,SearchOutcome::Error); QVERIFY(tooLong.error.contains("line limit"));
        write(path,QByteArray(100,'a')+'!');
        const auto bounded=searchFile(path,{"(*NO_JIT)(*NO_START_OPT)(a+)+$",SearchMode::Regex,true},cancel);
        QCOMPARE(bounded.outcome,SearchOutcome::Error); QVERIFY(bounded.error.contains("matching limit"));
    }
    void regexTaggedSearchHandoffHistoryAndInvalidPattern()
    {
        const auto base=root+"/regex-batch"; QVERIFY(QDir().mkpath(base+"/sub"));
        write(base+"/a.txt","skip\nORDER_1\nORDER_234\n"); write(base+"/sub/b.txt","ORDER_90\n"); write(base+"/c.txt","ORDER_bad\n");
        TreeWindow window(base); window.resize(1280,768); window.show(); QTRY_VERIFY(!window.busy()); window.load(true); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_B); QTest::keyClick(&window,Qt::Key_F8); QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto original=window.session().tags;
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier); for(int i=0;i<3;++i)QTest::keyClick(&window,Qt::Key_F4);
        QTest::keyClicks(&window,"(["); QTest::keyClick(&window,Qt::Key_Return); QVERIFY(!window.busy()); QCOMPARE(window.session().tags,original); QVERIFY(window.status().startsWith("Invalid regex"));
        capture(window,"regex-invalid"); QTest::keyClick(&window,Qt::Key_Backspace,Qt::ControlModifier);
        QTest::keyClicks(&window,"^ORDER_[0-9]+$"); capture(window,"regex-prompt");
        window.resize(window.minimumSize()); capture(window,"regex-prompt-80"); window.resize(1280,768);
        QTest::keyClick(&window,Qt::Key_Return); QTRY_VERIFY(!window.busy()); QCOMPARE(window.session().tags,(QSet<QString>{base+"/a.txt",base+"/sub/b.txt"})); QVERIFY(window.status().contains("Hits: 2"));
        QTest::keyClick(&window,Qt::Key_Home); QTest::keyClick(&window,Qt::Key_V);
        auto viewer=window.findChild<FileViewer*>(); QVERIFY(viewer); QTRY_VERIFY(!viewer->loading()); QCOMPARE(viewer->hit(),5);
        QTest::keyClick(viewer,Qt::Key_Space); QCOMPARE(viewer->hit(),13); capture(window,"regex-viewer-hits"); QTest::keyClick(viewer,Qt::Key_Minus); QCOMPARE(viewer->hit(),5);
        QTest::keyClick(viewer,Qt::Key_Escape); QTRY_VERIFY(window.findChildren<FileViewer*>().isEmpty());
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier); QTest::keyClick(&window,Qt::Key_Up); QTest::keyClick(&window,Qt::Key_Return);
        QTest::keyClick(&window,Qt::Key_Return); QTRY_VERIFY(!window.busy()); QCOMPARE(window.session().tags.size(),2);
        QTest::keyClick(&window,Qt::Key_S,Qt::ControlModifier); QTest::keyClicks(&window,"(["); QTest::keyClick(&window,Qt::Key_Return); QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_Home); QTest::keyClick(&window,Qt::Key_V); viewer=window.findChild<FileViewer*>(); QVERIFY(viewer); QTRY_VERIFY(!viewer->loading()); QCOMPARE(viewer->hit(),5);
    }
    void searchAcrossBlocksAndLines()
    {
        SearchOptions options{"ordenes", SearchMode::Text, false};
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        const QString path = root + "/large.txt";
        write(path, QByteArray(65534, 'x') + "ORDENES\n");
        QCOMPARE(searchFile(path, options, cancel).outcome, SearchOutcome::Found);
        options.caseSensitive = true;
        QCOMPARE(searchFile(path, options, cancel).outcome, SearchOutcome::NotFound);
        options.caseSensitive = false;
        write(path, "ord\nenes\n");
        QCOMPARE(searchFile(path, options, cancel).outcome, SearchOutcome::NotFound);
        options.query = "ord*enes";
        QCOMPARE(searchFile(path, options, cancel).outcome, SearchOutcome::NotFound);
        write(path, QByteArray("ord") + QByteArray(130000, 'x') + "enes");
        QCOMPARE(searchFile(path, options, cancel).outcome, SearchOutcome::Found);
        options.query = "ord**enes";
        QCOMPARE(searchFile(path, options, cancel).outcome, SearchOutcome::Found);
    }
    void literalStarsAndHex()
    {
        const QString path = root + "/search.txt";
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        write(path, "abZZcd*\n*aZZb\n");
        QCOMPARE(searchFile(path, {"ab*cd*", SearchMode::Text, true}, cancel).outcome, SearchOutcome::Found);
        QCOMPARE(searchFile(path, {"*a*b", SearchMode::Text, true}, cancel).outcome, SearchOutcome::NotFound);
        write(path, QByteArray(65535, 'x') + QByteArray::fromHex("000aff"));
        QCOMPARE(searchFile(path, {"00 0A FF", SearchMode::Hex, false}, cancel).outcome, SearchOutcome::Found);
        QCOMPARE(searchFile(path, {"00 0G", SearchMode::Hex, false}, cancel).outcome, SearchOutcome::Error);
    }
    void unicodeSearchAndInvalidEncoding()
    {
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        const QString path = root + "/unicode.txt";
        write(path, QByteArray(65535, 'x') + QString("ÁRBOL").toUtf8());
        QCOMPARE(searchFile(path, {"árbol", SearchMode::Unicode, false}, cancel).outcome, SearchOutcome::Found);
        write(path, QByteArray::fromHex("fffe6f007200640065006e0065007300"));
        QCOMPARE(searchFile(path, {"ordenes", SearchMode::Unicode, false}, cancel).outcome, SearchOutcome::Found);
        write(path, QByteArray::fromHex("feff006f007200640065006e00650073"));
        QCOMPARE(searchFile(path, {"ordenes", SearchMode::Unicode, false}, cancel).outcome, SearchOutcome::Found);
        write(path, QByteArray("nada") + QByteArray::fromHex("c3"));
        QCOMPARE(searchFile(path, {"ordenes", SearchMode::Unicode, false}, cancel).outcome, SearchOutcome::Error);
        write(path, QByteArray::fromHex("e1ff") + " ORDENES");
        QCOMPARE(searchFile(path, {"ordenes", SearchMode::Text, false}, cancel).outcome, SearchOutcome::Found);
        cancel->store(true);
        QCOMPARE(searchFile(path, {"ordenes", SearchMode::Text, false}, cancel).outcome, SearchOutcome::Cancelled);
    }
    void searchErrorKeepsUnprocessedMarks()
    {
        TreeWindow window(panel);
        window.show();
        QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QVERIFY(QFile::remove(panel + "/.htaccess"));
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QTest::keyClicks(&window, "ordenes");
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(window.status().contains("Not a regular file"), 5000);
        QVERIFY(window.busy());
        QCOMPARE(window.session().tags.size(), 3);
        QTest::keyClick(&window, Qt::Key_Escape);
        QVERIFY(!window.busy());
        QCOMPARE(window.session().tags.size(), 3);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_F3);
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(window.status().contains("Not a regular file"), 5000);
        QTest::keyClick(&window, Qt::Key_U);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 5000);
        QCOMPARE(window.session().tags.size(), 1);
        QVERIFY(window.session().tags.contains(panel + "/ordenes.php"));
    }
    void searchErrorPolicyAndRetry()
    {
        TreeWindow window(panel);
        window.show();
        QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QVERIFY(QFile::remove(panel + "/.htaccess"));
        QVERIFY(QFile::remove(panel + "/ordenes.php"));
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QTest::keyClicks(&window, "ordenes");
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(window.status().contains("Not a regular file"), 5000);
        write(panel + "/.htaccess");
        QTest::keyClick(&window, Qt::Key_R);
        QTRY_VERIFY_WITH_TIMEOUT(window.status().contains("Not a regular file"), 5000);
        QTest::keyClick(&window, Qt::Key_K);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 5000);
        QCOMPARE(window.session().tags.size(), 2);
    }
    void complete174FileWorkflow()
    {
        const QString testRoot = root + "/flujo";
        QVERIFY(QDir().mkpath(testRoot));
        for (int i = 0; i < 174; ++i)
            write(testRoot + QString("/archivo_%1.php").arg(i, 3, 10, QChar('0')),
                  i < 17 ? "ordenes pendientes\n" : "usuarios\n");
        TreeWindow window(testRoot);
        window.show();
        QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.session().files().size(), 174);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QCOMPARE(window.session().tags.size(), 174);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QTest::keyClicks(&window, "ordenes");
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy(), 10000);
        QCOMPARE(window.session().tags.size(), 17);
        QCOMPARE(window.session().files().size(), 174);
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(window.session().view, View::Showall);
        QCOMPARE(window.session().files().size(), 17);
        QTest::keyClick(&window, Qt::Key_Down);
        QSignalSpy opened(&window, &TreeWindow::openRequested);
        QTest::keyClick(&window, Qt::Key_O);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.first().at(0).toString(), testRoot + "/archivo_001.php");
        capture(window, "flujo-174-a-17");
    }
};

QTEST_MAIN(LTreeTests)
#include "test_ltree.moc"
