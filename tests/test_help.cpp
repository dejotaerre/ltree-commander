#include "core/helpdocument.h"
#include "ui/treewindow.h"
#include "ui/fileviewer.h"
#include "ui/archivewindow.h"
#include "ui/comparewindow.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QWheelEvent>
#include <archive.h>
#include <archive_entry.h>

using namespace ltree;
class HelpTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> fixture_;
    QByteArray history_;
    bool historySet_ = false;
    QString root_;
    void write(const QString &path, const QByteArray &data) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), data.size());
    }
    void capture(QWidget &widget, const QString &name) {
        const auto directory = qEnvironmentVariable("LTREE_CAPTURE_DIR");
        if (!directory.isEmpty()) { QVERIFY(QDir().mkpath(directory)); QVERIFY(widget.grab().save(directory+'/'+name+".png")); }
    }
    void wheel(QWidget &widget, int delta) {
        QWheelEvent event(QPointF(80,80), QPointF(80,80), QPoint(), QPoint(0,delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&widget, &event);
    }
    void zip(const QString &path) {
        auto writer = archive_write_new(); archive_write_set_format_zip(writer);
        QCOMPARE(archive_write_open_filename(writer,QFile::encodeName(path).constData()),ARCHIVE_OK);
        auto entry = archive_entry_new(); archive_entry_set_pathname(entry,"hello.txt"); archive_entry_set_filetype(entry,AE_IFREG);
        archive_entry_set_perm(entry,0644); archive_entry_set_size(entry,5);
        QCOMPARE(archive_write_header(writer,entry),ARCHIVE_OK); QCOMPARE(archive_write_data(writer,"hello",5),5);
        archive_entry_free(entry); QCOMPARE(archive_write_close(writer),ARCHIVE_OK); archive_write_free(writer);
    }
private slots:
    void init() {
        fixture_ = std::make_unique<QTemporaryDir>(QDir::currentPath()+"/help-XXXXXX"); QVERIFY(fixture_->isValid()); root_=fixture_->path();
        historySet_=qEnvironmentVariableIsSet("LTREE_HISTORY_FILE"); history_=qgetenv("LTREE_HISTORY_FILE");
        qputenv("LTREE_HISTORY_FILE",(root_+"/history.json").toUtf8()); write(root_+"/a.txt","first\nID:1\nID:2\n"); write(root_+"/b.txt","different\n");
    }
    void cleanup() {
        if(historySet_)qputenv("LTREE_HISTORY_FILE",history_); else qunsetenv("LTREE_HISTORY_FILE"); fixture_.reset();
    }
    void wrappingAndReachableEnd_data() {
        QTest::addColumn<int>("width"); QTest::addColumn<int>("height");
        QTest::newRow("wide") << 88 << 44; QTest::newRow("classic") << 76 << 19;
        QTest::newRow("terminal-small") << 36 << 6; QTest::newRow("narrow-core") << 12 << 3;
    }
    void wrappingAndReachableEnd() {
        QFETCH(int,width); QFETCH(int,height); HelpDocument help;
        QSet<int> ids;
        for (const auto &section:help.sections()) {
            QVERIFY(!ids.contains(int(section.id))); ids.insert(int(section.id)); help.open(section.id);
            const auto lines=help.lines(width); QVERIFY(!lines.isEmpty());
            for (const auto &line:lines) QVERIFY2(line.text.size()<=width,qPrintable(section.title+": "+line.text));
            help.act(HelpAction::End,width,height); QCOMPARE(help.top(),std::max(0,int(lines.size())-height));
            help.act(HelpAction::Down,width,height); QCOMPARE(help.top(),std::max(0,int(lines.size())-height));
            help.act(HelpAction::Home,width,height); QCOMPARE(help.top(),0);
        }
        QCOMPARE(help.sections().size(),16);
    }
    void contentsNavigationAndResize() {
        HelpDocument help; help.open(HelpTopic::Viewer); const int viewer=help.section(); help.act(HelpAction::Index,76,6); QVERIFY(help.index()); QCOMPARE(help.selected(),viewer);
        help.act(HelpAction::End,76,6); QCOMPARE(help.selected(),15); QVERIFY(help.top()>0);
        help.act(HelpAction::Accept,76,6); QVERIFY(!help.index()); QCOMPARE(help.sections()[help.section()].id,HelpTopic::Statistics);
        help.act(HelpAction::Next,76,6); QCOMPARE(help.section(),0); help.act(HelpAction::Previous,76,6); QCOMPARE(help.section(),15);
        help.act(HelpAction::End,76,6); help.constrain(36,44); QVERIFY(help.top()>=0); QVERIFY(help.top()<=std::max(0,int(help.lines(36).size())-44));
        QVERIFY(!help.act(HelpAction::Close,76,6));
    }
    void findAcrossChaptersRepeatAtEndAndCancel() {
        HelpDocument help; help.open(HelpTopic::Display);
        help.act(HelpAction::Find,76,19); help.act(HelpAction::Input,76,19,"Committed_AS"); help.act(HelpAction::Accept,76,19);
        QCOMPARE(help.sections()[help.section()].id,HelpTopic::Statistics); QVERIFY(help.matchLine()>=0); const int match=help.matchLine();
        help.act(HelpAction::Repeat,76,19); QCOMPARE(help.matchLine(),match);
        help.act(HelpAction::Find,76,19); help.act(HelpAction::Input,76,19,"zz-no-such-help-word"); help.act(HelpAction::Accept,76,19); QVERIFY(help.notice().startsWith("No match"));
        help.act(HelpAction::Find,76,19); help.act(HelpAction::Input,76,19,"pending draft"); help.act(HelpAction::Close,76,19); QVERIFY(!help.finding()); QCOMPARE(help.query(),QString("zz-no-such-help-word"));
        help.act(HelpAction::Find,76,19); help.act(HelpAction::Input,76,19,"Regexx"); help.act(HelpAction::Backspace,76,19); help.act(HelpAction::Accept,76,19); QVERIFY(help.matchLine()>=0);
    }
    void terminalManualOnlyOffersItsCommands() {
        HelpDocument help(true); QCOMPARE(help.sections().size(),3); help.open(HelpTopic::TerminalView); QCOMPARE(help.section(),2);
        QString body; for(const auto &section:help.sections())body+=section.body;
        QVERIFY(body.contains("not yet available")); QVERIFY(!body.contains(": Ctrl+D")); QVERIFY(!body.contains(": Alt+P"));
        for(const auto &section:help.sections()){help.open(section.id);for(const auto &line:help.lines(36))QVERIFY(line.text.size()<=36);}
    }
    void treeHelpScrollFindAndReturnPreserveWorkspace() {
        TreeWindow window(root_); window.resize(1280,800); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return); QTest::keyClick(&window,Qt::Key_Down); QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto view=window.session().view; const auto tags=window.session().tags; const int index=window.session().fileIndex;
        QTest::keyClick(&window,Qt::Key_F1); capture(window,"help-main-wide"); const auto main=window.grab().toImage();
        QTest::keyClick(&window,Qt::Key_PageDown); QVERIFY(window.grab().toImage()!=main);
        QTest::keyClick(&window,Qt::Key_Home); wheel(window,-120); QVERIFY(window.grab().toImage()!=main);
        QTest::keyClick(&window,Qt::Key_Tab); capture(window,"help-contents-wide");
        QTest::keyClick(&window,Qt::Key_F); QTest::keyClicks(&window,"Regex"); QTest::keyClick(&window,Qt::Key_Return); capture(window,"help-find-regex");
        QTest::keyClick(&window,Qt::Key_D); QVERIFY(QFileInfo::exists(root_+"/a.txt")); QVERIFY(QFileInfo::exists(root_+"/b.txt"));
        QTest::keyClick(&window,Qt::Key_Escape); QCOMPARE(window.session().view,view); QCOMPARE(window.session().tags,tags); QCOMPARE(window.session().fileIndex,index);
        window.resize(640,400); QTest::keyClick(&window,Qt::Key_F1); capture(window,"help-main-80x25");
        QTest::keyClick(&window,Qt::Key_End); capture(window,"help-main-end-80x25"); QTest::keyClick(&window,Qt::Key_Tab); capture(window,"help-contents-80x25");
        QTest::keyClick(&window,Qt::Key_End); QTest::keyClick(&window,Qt::Key_Return); capture(window,"help-statistics-80x25"); QTest::keyClick(&window,Qt::Key_F1);
        QCOMPARE(window.session().tags,tags);
    }
    void viewerHelpKeepsSearchModeAndOffset() {
        FileViewer view(root_+"/a.txt"); view.resize(640,400); view.show(); QTRY_VERIFY(!view.loading());
        view.setSearch({"ID:\\d+",SearchMode::Regex,true}); const auto hit=view.hit(); QSignalSpy closed(&view,&FileViewer::closed);
        QTest::keyClick(&view,Qt::Key_F1); capture(view,"help-viewer-80x25"); QVERIFY(view.prompting());
        QTest::keyClick(&view,Qt::Key_D); QVERIFY(!view.hexMode()); QTest::keyClick(&view,Qt::Key_End); capture(view,"help-viewer-end-80x25");
        QTest::keyClick(&view,Qt::Key_F); QTest::keyClicks(&view,"byte overwrite"); QTest::keyClick(&view,Qt::Key_Escape); QVERIFY(view.prompting());
        QTest::keyClick(&view,Qt::Key_Escape); QVERIFY(!view.prompting()); QCOMPARE(closed.size(),0); QCOMPARE(view.hit(),hit);
        QTest::keyClick(&view,Qt::Key_Space); QVERIFY(view.hit()>hit);
    }
    void archiveAndCompareHelpAreContextualAndDoNotMutate() {
        const QString path=root_+"/example.zip"; zip(path); ArchiveWindow archive(path,root_); archive.resize(640,400); archive.show(); QTRY_VERIFY(!archive.busy());
        QTest::keyClick(&archive,Qt::Key_T,Qt::ControlModifier); const int tagged=archive.taggedCount(); QSignalSpy closed(&archive,&ArchiveWindow::closed);
        QTest::keyClick(&archive,Qt::Key_F1); capture(archive,"help-archive-80x25"); QTest::keyClick(&archive,Qt::Key_End); wheel(archive,120); QTest::keyClick(&archive,Qt::Key_Escape);
        QCOMPARE(archive.taggedCount(),tagged); QCOMPARE(closed.size(),0);
        QTest::keyClick(&archive,Qt::Key_C); QTest::keyClick(&archive,Qt::Key_F1); capture(archive,"help-extract-prompt"); QTest::keyClick(&archive,Qt::Key_Escape); QTest::keyClick(&archive,Qt::Key_Escape);
        QCOMPARE(closed.size(),0); QVERIFY(!QFileInfo::exists(root_+"/hello.txt"));
        CompareWindow compare(root_+"/a.txt",root_+"/b.txt"); compare.resize(640,400); compare.show(); QTRY_VERIFY(!compare.loading());
        const int row=compare.currentRow(); QTest::keyClick(&compare,Qt::Key_F1); capture(compare,"help-compare-80x25"); QTest::keyClick(&compare,Qt::Key_H); QVERIFY(!compare.hexMode());
        QTest::keyClick(&compare,Qt::Key_PageDown); QTest::keyClick(&compare,Qt::Key_F1); QCOMPARE(compare.currentRow(),row);
    }
    void dangerousPromptHelpDoesNotExecuteCommands() {
        TreeWindow window(root_); window.resize(640,400); window.show(); QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_P,Qt::AltModifier); QTRY_VERIFY(!window.busy()); QTest::keyClick(&window,Qt::Key_F1); capture(window,"help-prune-80x25");
        QTest::keyClicks(&window,"PRUNE"); QTest::keyClick(&window,Qt::Key_Return); QVERIFY(QFileInfo::exists(root_+"/a.txt"));
        QTest::keyClick(&window,Qt::Key_Escape); QTest::keyClick(&window,Qt::Key_Escape); QVERIFY(QFileInfo::exists(root_+"/b.txt"));
        QTest::keyClick(&window,Qt::Key_G,Qt::AltModifier); QTest::keyClick(&window,Qt::Key_F1); capture(window,"help-graft-80x25"); QTest::keyClick(&window,Qt::Key_Escape); QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_Question); QTest::keyClick(&window,Qt::Key_F1); capture(window,"help-stats-context-80x25");
        QTest::keyClick(&window,Qt::Key_Escape); QVERIFY(window.statisticsVisible()); QTest::keyClick(&window,Qt::Key_Escape); QVERIFY(!window.statisticsVisible());
        QTest::keyClick(&window,Qt::Key_Return); QTest::keyClick(&window,Qt::Key_N); QTest::keyClick(&window,Qt::Key_F1); capture(window,"help-stamp-80x25");
        QTest::keyClick(&window,Qt::Key_Escape); QTest::keyClick(&window,Qt::Key_Escape); QVERIFY(QFileInfo::exists(root_+"/a.txt"));
    }
};
QTEST_MAIN(HelpTests)
#include "test_help.moc"
