#include "ui/windowgeometry.h"
#include "ui/treewindow.h"
#include "ui/archivewindow.h"
#include "ui/comparewindow.h"
#include "ui/executewindow.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QScreen>
#include <QProcess>
#include <QFile>

using namespace ltree;
class GeometryTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> fixture_;
    QByteArray previous_;
    bool wasSet_ = false;
    QString config() const { return fixture_->path()+"/ltreec/window.ini"; }
    void prepare(QWidget &window) { window.setMinimumSize(640,400); window.resize(704,432); }
    QByteArray contents() { QFile file(config()); if(!file.open(QIODevice::ReadOnly))return {};return file.readAll(); }
private slots:
    void init() {
        fixture_=std::make_unique<QTemporaryDir>(QDir::currentPath()+"/geometry-XXXXXX"); QVERIFY(fixture_->isValid());
        wasSet_=qEnvironmentVariableIsSet("XDG_CONFIG_HOME");previous_=qgetenv("XDG_CONFIG_HOME");qputenv("XDG_CONFIG_HOME",fixture_->path().toUtf8());
    }
    void cleanup() { if(wasSet_)qputenv("XDG_CONFIG_HOME",previous_);else qunsetenv("XDG_CONFIG_HOME");fixture_.reset(); }
    void noSettingsKeepsDefaultsAndDoesNotWrite() {
        QWidget window; prepare(window); restoreWindowGeometry(window); QCOMPARE(window.size(),QSize(704,432)); QVERIFY(!QFileInfo::exists(config()));
    }
    void normalSizeAndPositionRoundTrip() {
        QWidget original; prepare(original); original.move(31,43); original.show(); QTest::qWait(30); saveWindowGeometry(original);
        QVERIFY(QFileInfo::exists(config())); QSettings settings(config(),QSettings::IniFormat); QCOMPARE(settings.value("normalSize").toSize(),QSize(704,432));
        const bool wayland=QGuiApplication::platformName().startsWith("wayland"); QCOMPARE(settings.contains("geometry"),!wayland);
        QWidget restored; restored.setMinimumSize(640,400); restoreWindowGeometry(restored); restored.show(); QTest::qWait(30);
        QCOMPARE(restored.size(),QSize(704,432)); QVERIFY(!restored.isMaximized()); QVERIFY(!restored.isFullScreen());
        if(!wayland)QCOMPARE(restored.pos(),original.pos());
    }
    void maximizedKeepsNormalSize() {
        QWidget original; prepare(original); original.show(); QTest::qWait(30); original.showMaximized(); QTRY_VERIFY(original.isMaximized()); QTest::qWait(30); saveWindowGeometry(original);
        QSettings settings(config(),QSettings::IniFormat); QCOMPARE(settings.value("normalSize").toSize(),QSize(704,432)); QVERIFY(settings.value("maximized").toBool());
        QWidget restored; restored.setMinimumSize(640,400); restoreWindowGeometry(restored); restored.show(); QTRY_VERIFY(restored.isMaximized());
        restored.showNormal(); QTRY_COMPARE(restored.size(),QSize(704,432));
    }
    void fullscreenKeepsNormalSizeAndExplicitOverride() {
        QWidget original; prepare(original); original.show(); QTest::qWait(30); original.showFullScreen(); QTRY_VERIFY(original.isFullScreen()); QTest::qWait(30); saveWindowGeometry(original);
        QWidget restored; restored.setMinimumSize(640,400); restoreWindowGeometry(restored); restored.show(); QTRY_VERIFY(restored.isFullScreen());
        restored.showNormal(); QTRY_COMPARE(restored.size(),QSize(704,432)); saveWindowGeometry(restored);
        QWidget forced; forced.setMinimumSize(640,400); restoreWindowGeometry(forced); forced.showFullScreen(); QTRY_VERIFY(forced.isFullScreen());
    }
    void fullscreenTreeKeysPreserveGeometryAndPrompt() {
        TreeWindow window(fixture_->path()); window.resize(704,432); window.move(31,43); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window)); QTRY_VERIFY(!window.busy());
        const auto normal=window.geometry(); const auto position=window.pos(); const auto directory=window.session().directory;
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.txt");
        for(const bool maximized:{false,true}) {
            if(maximized)window.showMaximized();
            QTest::qWait(30);
            for(const auto key:{Qt::Key_Return,Qt::Key_Enter}) {
                QTest::keyClick(&window,key,Qt::AltModifier); QTRY_VERIFY(window.isFullScreen());
                QTest::qWait(100);
                QKeyEvent repeated(QEvent::KeyPress,key,Qt::AltModifier,{},true);
                QApplication::sendEvent(&window,&repeated); QVERIFY(window.isFullScreen());
                QTest::keyClick(&window,key,Qt::AltModifier); QTRY_VERIFY(!window.isFullScreen());
                QTest::qWait(100);
                QCOMPARE(window.isMaximized(),maximized);
                QCOMPARE(window.session().directory,directory);
                QCOMPARE(window.session().filespec.text(),QString("*.*"));
            }
            window.showNormal(); QTRY_COMPARE(window.size(),normal.size());
            if(!QGuiApplication::platformName().startsWith("wayland"))QCOMPARE(window.pos(),position);
        }
        QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().filespec.text(),QString("*.txt"));
    }
    void fullscreenKeysInChildViews_data() {
        QTest::addColumn<QString>("view");
        for(const auto name:{"viewer","archive","comparison","shell"})QTest::newRow(name)<<QString(name);
    }
    void fullscreenKeysInChildViews() {
        QFETCH(QString,view);
        const auto path=fixture_->path()+"/sample.txt";
        {QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("sample\n");}
        QWidget host;prepare(host);host.show();QTest::qWait(30);const auto normal=host.geometry();
        std::unique_ptr<QWidget> child;
        if(view=="viewer")child=std::make_unique<FileViewer>(path,&host);
        else if(view=="archive")child=std::make_unique<ArchiveWindow>(QStringList{path},fixture_->path(),fixture_->path(),&host);
        else if(view=="comparison")child=std::make_unique<CompareWindow>(path,path,&host);
        else child=std::make_unique<ExecuteWindow>(fixture_->path(),"/bin/cat",QStringList{},&host);
        child->resize(host.size());child->show();
        QSignalSpy closed(child.get(),SIGNAL(closed()));
        QWidget *target=view=="shell"?child->focusProxy():child.get();QVERIFY(target);
        for(const bool maximized:{false,true}) {
            if(maximized)host.showMaximized();
            for(const auto key:{Qt::Key_Return,Qt::Key_Enter}) {
                QTest::keyClick(target,key,Qt::AltModifier);QTRY_VERIFY(host.isFullScreen());
                QTest::keyClick(target,key,Qt::AltModifier);QTRY_VERIFY(!host.isFullScreen());
                QCOMPARE(host.isMaximized(),maximized);QCOMPARE(closed.count(),0);
            }
            host.showNormal();QTRY_COMPARE(host.size(),normal.size());
        }
        QVERIFY(!QFileInfo::exists(fixture_->path()+"/sample.zip"));
    }
    void minimizedDoesNotReopenMinimized() {
        QWidget original; prepare(original); original.show(); QTest::qWait(30); original.showMinimized(); QTRY_VERIFY(original.isMinimized()); saveWindowGeometry(original);
        QWidget restored; restored.setMinimumSize(640,400); restoreWindowGeometry(restored); restored.show(); QTest::qWait(30);
        QVERIFY(!restored.isMinimized()); QCOMPARE(restored.size(),QSize(704,432));
    }
    void corruptAndOversizedSettingsFallback() {
        { QSettings settings(config(),QSettings::IniFormat);settings.setValue("normalSize","broken"); settings.setValue("geometry","garbage"); }
        QWidget untouched; prepare(untouched); restoreWindowGeometry(untouched); QCOMPARE(untouched.size(),QSize(704,432));
        { QSettings settings(config(),QSettings::IniFormat);settings.setValue("normalSize",QSize(90000,90000)); }
        QWidget bounded; bounded.setMinimumSize(640,400); restoreWindowGeometry(bounded);
        QVERIFY(bounded.size().width()<=bounded.screen()->availableGeometry().width()); QVERIFY(bounded.size().height()<=bounded.screen()->availableGeometry().height());
        { QSettings settings(config(),QSettings::IniFormat);settings.setValue("normalSize",QSize(10,10)); }
        restoreWindowGeometry(bounded); QCOMPARE(bounded.size(),QSize(640,400));
    }
    void cliLeavesSettingsAlone() {
        QWidget window; prepare(window);
        saveWindowGeometry(window); const QByteArray before=contents(); QVERIFY(!before.isEmpty());
        QProcess process; auto env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen");process.setProcessEnvironment(env);
        const auto executable=QCoreApplication::applicationDirPath()+"/ltc";
        for(const QStringList args:{QStringList{"--help"},QStringList{"--version"},QStringList{"--terminal","--help"},QStringList{"--new-instance",fixture_->path()+"/missing"}}) {
            process.start(executable,args); QVERIFY(process.waitForFinished(5000)); QCOMPARE(process.exitStatus(),QProcess::NormalExit); QCOMPARE(contents(),before);
        }
    }
};
QTEST_MAIN(GeometryTests)
#include "test_geometry.moc"
