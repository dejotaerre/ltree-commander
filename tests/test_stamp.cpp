#include "fs/stamp.h"
#include "ui/treewindow.h"
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThreadPool>
#include <QSemaphore>
#include <QScopeGuard>
#include <QtConcurrent/QtConcurrentRun>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace ltree;
class StampTests:public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> fixture_;
    QString root_;
    QByteArray previousHistory_;
    bool historySet_=false;
    const QString original_="2024-02-28 12:34:55";
    Cancellation cancel(){return std::make_shared<std::atomic_bool>(false);}
    QDateTime date(const QString &text){return QDateTime::fromString(text,"yyyy-MM-dd HH:mm:ss");}
    QString file(const QString &name,const QString &stamp={}){
        const auto path=root_+'/'+name;QFile out(path);
        if(!out.open(QIODevice::WriteOnly))return {};
        out.write("content");out.close();
        const auto seconds=date(stamp.isEmpty()?original_:stamp).toSecsSinceEpoch();
        const timespec times[]{{seconds-3600,123456789},{seconds,987654321}};
        if(::utimensat(AT_FDCWD,QFile::encodeName(path).constData(),times,0)<0)return {};
        return path;
    }
    StampPlan plan(const QString &path,const QString &input,StampField field=StampField::Written,StampMode mode=StampMode::Set){return planStamp({readMetadata(path)},input,field,mode);}
    void ready(TreeWindow &window){window.resize(1280,768);window.show();QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(window.session().view,View::Directory);}
    void type(TreeWindow &window,const QString &value){QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(&window,value);}
    void apply(TreeWindow &window){QTest::keyClick(&window,Qt::Key_Return);QTRY_VERIFY(!window.busy());QVERIFY2(window.status().contains("Stamp completed"),qPrintable(window.status()));}
    void capture(TreeWindow &window,const QString &name){const auto dir=qEnvironmentVariable("LTREE_CAPTURE_DIR");if(!dir.isEmpty()){QVERIFY(QDir().mkpath(dir));QVERIFY(window.grab().save(dir+'/'+name+".png"));}}
private slots:
    void init(){fixture_=std::make_unique<QTemporaryDir>(QDir::currentPath()+"/stamp-XXXXXX");QVERIFY(fixture_->isValid());root_=fixture_->path();historySet_=qEnvironmentVariableIsSet("LTREE_HISTORY_FILE");previousHistory_=qgetenv("LTREE_HISTORY_FILE");qputenv("LTREE_HISTORY_FILE",QFile::encodeName(root_+"/history/search-history.json"));}
    void cleanup(){if(historySet_)qputenv("LTREE_HISTORY_FILE",previousHistory_);else qunsetenv("LTREE_HISTORY_FILE");}
    void setWrittenRetainsAccessAndContent(){
        const auto path=file("one.txt");QVERIFY(!path.isEmpty());const auto before=readMetadata(path);
        QVERIFY(QFile::setPermissions(path,QFile::ReadOwner));
        const auto result=stampFiles(plan(path,"2020-01-02 03:04:05"),cancel());QCOMPARE(result.changed,1);QCOMPARE(result.failed,0);
        const auto after=readMetadata(path);QCOMPARE(after.modified,date("2020-01-02 03:04:05"));QCOMPARE(after.modifiedNanoseconds,0);
        QCOMPARE(after.accessed,before.accessed);QCOMPARE(after.accessedNanoseconds,before.accessedNanoseconds);
        QCOMPARE(after.inode,before.inode);QCOMPARE(after.size,before.size);QCOMPARE(after.mode,quint32(0400));
        QFile bytes(path);QVERIFY(bytes.open(QIODevice::ReadOnly));QCOMPARE(bytes.readAll(),QByteArray("content"));
    }
    void dateOnlyTimeOnlyAccessAndBoth(){
        const auto path=file("one.txt");const auto before=readMetadata(path);
        auto result=stampFiles(plan(path,"2020-02-29"),cancel());QCOMPARE(result.changed,1);
        auto after=readMetadata(path);QCOMPARE(after.modified.toString("yyyy-MM-dd HH:mm:ss"),QString("2020-02-29 12:34:55"));QCOMPARE(after.modifiedNanoseconds,987654321);
        result=stampFiles(plan(path,"01:02:03",StampField::Accessed),cancel());QCOMPARE(result.changed,1);
        after=readMetadata(path);QCOMPARE(after.accessed.date(),before.accessed.date());QCOMPARE(after.accessed.time(),QTime(1,2,3));QCOMPARE(after.accessedNanoseconds,0);
        QCOMPARE(after.modified.toString("yyyy-MM-dd HH:mm:ss"),QString("2020-02-29 12:34:55"));QCOMPARE(after.modifiedNanoseconds,987654321);
        result=stampFiles(plan(path,"2001-02-03 04:05:06",StampField::Both),cancel());QCOMPARE(result.changed,1);
        after=readMetadata(path);QCOMPARE(after.modified,after.accessed);QCOMPARE(after.modified,date("2001-02-03 04:05:06"));
    }
    void adjustCalendarAndEvenSeconds(){
        const auto path=file("one.txt");
        auto result=stampFiles(plan(path,"+1d-5n",StampField::Written,StampMode::Adjust),cancel());QCOMPARE(result.changed,1);
        auto after=readMetadata(path);QCOMPARE(after.modified.toString("yyyy-MM-dd HH:mm:ss"),QString("2024-02-29 12:29:55"));QCOMPARE(after.modifiedNanoseconds,987654321);
        result=stampFiles(plan(path,"e",StampField::Written,StampMode::Adjust),cancel());QCOMPARE(result.changed,1);
        after=readMetadata(path);QCOMPARE(after.modified.time(),QTime(12,29,56));QCOMPARE(after.modifiedNanoseconds,0);
        result=stampFiles(plan(path,"e",StampField::Written,StampMode::Adjust),cancel());QCOMPARE(result.unchanged,1);
        result=stampFiles(plan(path,"+1y+1m-1h+10s",StampField::Written,StampMode::Adjust),cancel());QCOMPARE(result.changed,1);
        QCOMPARE(readMetadata(path).modified.toString("yyyy-MM-dd HH:mm:ss"),QString("2025-03-28 11:30:06"));
    }
    void incrementUsesPreviousGeneratedValue(){
        const auto a=file("a.txt"),b=file("b.txt","2000-01-01 00:00:00"),c=file("c.txt","2005-01-01 00:00:00");
        const auto prepared=planStamp({readMetadata(a),readMetadata(b),readMetadata(c)},"+2s",StampField::Written,StampMode::Increment);
        QVERIFY(prepared.error.isEmpty());const auto result=stampFiles(prepared,cancel());QCOMPARE(result.changed,3);
        QCOMPARE(readMetadata(a).modified.time(),QTime(12,34,57,987));QCOMPARE(readMetadata(b).modified.time(),QTime(12,34,59,987));
        QCOMPARE(readMetadata(c).modified.time(),QTime(12,35,1,987));QCOMPARE(readMetadata(c).modified.date(),QDate(2024,2,28));
        QCOMPARE(readMetadata(b).modifiedNanoseconds,987654321);
    }
    void hardlinksShareTimestampsWithoutFalseConcurrentFailure(){
        const auto path=file("one.txt"),alias=root_+"/alias.txt";
        QVERIFY(::link(QFile::encodeName(path).constData(),QFile::encodeName(alias).constData())==0);
        const auto prepared=planStamp({readMetadata(path),readMetadata(alias)},"2000-01-01 00:00:00",StampField::Written,StampMode::Set);
        const auto result=stampFiles(prepared,cancel());QCOMPARE(result.changed,1);QCOMPARE(result.unchanged,1);QCOMPARE(result.failed,0);
        QCOMPARE(readMetadata(alias).modified,date("2000-01-01 00:00:00"));QCOMPARE(readMetadata(alias).inode,readMetadata(path).inode);
    }
    void storedTimestampIsVerified(){
        const auto path=file("one.txt");const auto requested=date("1024-02-28 12:34:55");
        const auto result=stampFiles(plan(path,"1024-02-28 12:34:55"),cancel());
        QCOMPARE(result.changed,1);
        if(readMetadata(path).modified==requested)QCOMPARE(result.failed,0);
        else{QCOMPARE(result.failed,1);QVERIFY(result.error.contains("Stored timestamp differs"));}
    }
    void invalidInputsCannotChangeFiles(){
        const auto path=file("one.txt");const auto before=readMetadata(path);
        for(const auto &input:QStringList{"","2023-02-29 10:00:00","2024-12-31 25:00:00","junk","0000-01-01"}){
            const auto prepared=plan(path,input);QVERIFY(!prepared.error.isEmpty());QCOMPARE(stampFiles(prepared,cancel()).changed,0);
        }
        for(const auto &input:QStringList{"","1d","+1x","+1degarbage","+999999999999999999y","+100000000y"}){
            const auto prepared=plan(path,input,StampField::Written,StampMode::Adjust);QVERIFY(!prepared.error.isEmpty());
        }
        QCOMPARE(readMetadata(path).modified,before.modified);QCOMPARE(readMetadata(path).modifiedNanoseconds,before.modifiedNanoseconds);
    }
    void linksSpecialFilesAndChangedInodesAreRetained(){
        const auto path=file("one.txt");const auto before=readMetadata(path);
        QVERIFY(QFile::link(path,root_+"/link.txt"));QCOMPARE(stampFiles(plan(root_+"/link.txt","2000-01-01"),cancel()).failed,1);
        QVERIFY(QDir().mkdir(root_+"/dir"));QVERIFY(::mkfifo(QFile::encodeName(root_+"/fifo").constData(),0600)==0);
        QCOMPARE(stampFiles(plan(root_+"/dir","2000-01-01"),cancel()).failed,1);QCOMPARE(stampFiles(plan(root_+"/fifo","2000-01-01"),cancel()).failed,1);
        QVERIFY(QFile::link(root_,root_+"/alias"));QCOMPARE(stampFiles(plan(root_+"/alias/one.txt","2000-01-01"),cancel()).failed,1);
        QCOMPARE(readMetadata(path).modified,before.modified);
        const auto prepared=plan(path,"2000-01-01");QVERIFY(QFile::rename(path,root_+"/old.txt"));QVERIFY(!file("one.txt","2022-01-01 01:00:00").isEmpty());
        const auto replacement=readMetadata(path);QCOMPARE(stampFiles(prepared,cancel()).failed,1);QCOMPARE(readMetadata(path).modified,replacement.modified);
        const auto stale=plan(path,"2000-01-01");QFile writer(path);QVERIFY(writer.open(QIODevice::Append));writer.write("changed");writer.close();
        QCOMPARE(stampFiles(stale,cancel()).failed,1);
    }
    void partialFailureAndCancellation(){
        const auto a=file("a.txt"),b=file("b.txt");const auto prepared=planStamp({readMetadata(a),readMetadata(b)},"2000-01-01 01:02:03",StampField::Written,StampMode::Set);
        QVERIFY(QFile::remove(a));auto result=stampFiles(prepared,cancel());QCOMPARE(result.failed,1);QCOMPARE(result.changed,1);QVERIFY(!result.error.isEmpty());
        QCOMPARE(readMetadata(b).modified,date("2000-01-01 01:02:03"));QCOMPARE(result.scan.directories.size(),1);
        const auto stopped=cancel();stopped->store(true);result=stampFiles(plan(b,"1990-01-01"),stopped);QVERIFY(result.cancelled);QCOMPARE(result.changed,0);
        QCOMPARE(readMetadata(b).modified,date("2000-01-01 01:02:03"));
    }
    void currentPromptEditingTabNowAndCancel(){
        const auto a=file("a.txt"),b=file("b.txt");
        TreeWindow window(root_);ready(window);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        const auto tags=window.session().tags;QTest::keyClick(&window,Qt::Key_N);
        window.resize(640,400);capture(window,"stamp-narrow");window.resize(1280,768);
        QTest::keyClick(&window,Qt::Key_F1);capture(window,"stamp-help");QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_Right);QTest::keyClick(&window,Qt::Key_Home);for(int i=0;i<3;++i)QTest::keyClick(&window,Qt::Key_Right);QTest::keyClick(&window,Qt::Key_Delete);QTest::keyClicks(&window,"5");
        capture(window,"stamp-edit");apply(window);
        QCOMPARE(readMetadata(a).modified.toString("yyyy-MM-dd HH:mm:ss"),QString("2025-02-28 12:34:55"));
        const auto edited=readMetadata(a).modified;QTest::keyClick(&window,Qt::Key_N);type(window,"1999-01-01");
        QTest::keyClick(&window,Qt::Key_Escape);QCOMPARE(readMetadata(a).modified,edited);
        QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_F2);capture(window,"stamp-now");QTest::keyClick(&window,Qt::Key_Escape);
        QTest::keyClick(&window,Qt::Key_N);type(window,"invalid");QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.status().contains("Enter yyyy"));
        type(window,"2001-02-03 04:05:06");apply(window);QCOMPARE(readMetadata(a).modified,date("2001-02-03 04:05:06"));QCOMPARE(readMetadata(b).modified.toString("yyyy-MM-dd HH:mm:ss"),original_);
        QCOMPARE(window.session().tags,tags);QCOMPARE(window.session().currentFile()->path,a);QCOMPARE(window.session().view,View::Directory);
    }
    void taggedVisibleScopeAndF4VirtualMenu(){
        const auto a=file("a.txt"),b=file("b.bin"),c=file("c.txt");const auto old=readMetadata(b);
        TreeWindow window(root_);ready(window);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_F);QTest::keyClicks(&window,"*.txt");QTest::keyClick(&window,Qt::Key_Return);
        const auto tags=window.session().tags;QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_N);
        type(window,"2000-01-01 01:02:03");capture(window,"stamp-tagged");apply(window);
        QCOMPARE(readMetadata(a).modified,date("2000-01-01 01:02:03"));QCOMPARE(readMetadata(c).modified,readMetadata(a).modified);QCOMPARE(readMetadata(b).modified,old.modified);
        QCOMPARE(window.session().tags,tags);QVERIFY(window.status().contains("2/2"));
    }
    void adjustIncrementAndTimestampOptions(){
        const auto a=file("a.txt"),b=file("b.txt","2000-01-01 00:00:00");const auto access=readMetadata(a).accessed;
        TreeWindow window(root_);ready(window);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_N,Qt::ControlModifier);
        QTest::keyClick(&window,Qt::Key_F5);type(window,"+1d");apply(window);QCOMPARE(readMetadata(a).modified.date(),QDate(2024,2,29));QCOMPARE(readMetadata(b).modified.date(),QDate(2000,1,2));
        QTest::keyClick(&window,Qt::Key_N,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_F5);QTest::keyClick(&window,Qt::Key_F5);type(window,"+2s");apply(window);
        QCOMPARE(readMetadata(a).modified.time(),QTime(12,34,57,987));QCOMPARE(readMetadata(b).modified.time(),QTime(12,34,59,987));
        QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_F4);type(window,"2010-01-01 00:00:00");apply(window);
        QCOMPARE(readMetadata(a).accessed,date("2010-01-01 00:00:00"));QVERIFY(readMetadata(a).accessed!=access);QCOMPARE(readMetadata(a).modified.time(),QTime(12,34,57,987));
        QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_F4);QTest::keyClick(&window,Qt::Key_F4);type(window,"2011-01-01 00:00:00");apply(window);
        QCOMPARE(readMetadata(a).modified,readMetadata(a).accessed);
    }
    void persistentHistoryLastAndRetrieve(){
        const auto a=file("a.txt");
        {TreeWindow window(root_);ready(window);QTest::keyClick(&window,Qt::Key_N);type(window,"2000-01-01 01:02:03");apply(window);}
        const auto history=root_+"/history/stamp-history.json";QFile saved(history);QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(!(saved.permissions()&QFile::ReadOther));QCOMPARE(QJsonDocument::fromJson(saved.readAll()).object().value("stamp").toArray().size(),1);saved.close();
        const auto before=readMetadata(a);TreeWindow window(root_);ready(window);QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_Up);capture(window,"stamp-history");
        QTest::keyClick(&window,Qt::Key_Return);QCOMPARE(readMetadata(a).modified,before.modified);apply(window);QVERIFY(window.status().contains("1 unchanged"));
        QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_F5);type(window,"+1d");apply(window);
        QTest::keyClick(&window,Qt::Key_N);QTest::keyClick(&window,Qt::Key_F3);apply(window);QCOMPARE(readMetadata(a).modified.date(),QDate(2000,1,3));
    }
    void branchShowallAndGlobalTaggedScope_data(){
        QTest::addColumn<int>("scope");QTest::newRow("Branch")<<int(View::Branch);QTest::newRow("Showall")<<int(View::Showall);QTest::newRow("Global")<<int(View::Global);
    }
    void branchShowallAndGlobalTaggedScope(){
        QFETCH(int,scope);QVERIFY(QDir().mkpath(root_+"/source/sub"));QVERIFY(QDir().mkdir(root_+"/other"));
        const auto a=file("source/a.txt"),b=file("source/sub/b.txt"),c=file("other/c.txt");
        TreeWindow window(root_+"/source");window.show();QTRY_VERIFY(!window.busy());window.load(true,true);QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,scope==int(View::Showall)?Qt::Key_S:Qt::Key_B);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
        if(scope==int(View::Global)){
            QVERIFY(window.selectRoot(root_+"/other"));QTRY_VERIFY(!window.busy());QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);
            QTest::keyClick(&window,Qt::Key_Escape);QTest::keyClick(&window,Qt::Key_G);
        }
        QCOMPARE(int(window.session().view),scope);const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_N,Qt::ControlModifier);type(window,"2000-01-01 01:02:03");apply(window);
        QCOMPARE(readMetadata(a).modified,date("2000-01-01 01:02:03"));QCOMPARE(readMetadata(b).modified,readMetadata(a).modified);
        QCOMPARE(readMetadata(c).modified.toString("yyyy-MM-dd HH:mm:ss"),scope==int(View::Global)?QString("2000-01-01 01:02:03"):original_);
        QCOMPARE(window.session().tags,tags);QCOMPARE(int(window.session().view),scope);
    }
    void cancelQueuedTaggedStampRetainsAllDates(){
        const auto a=file("a.txt"),b=file("b.txt");const auto before=readMetadata(a);
        TreeWindow window(root_);ready(window);QTest::keyClick(&window,Qt::Key_T,Qt::ControlModifier);const auto tags=window.session().tags;
        QTest::keyClick(&window,Qt::Key_N,Qt::ControlModifier);type(window,"2000-01-01 01:02:03");
        auto pool=QThreadPool::globalInstance();const auto threads=pool->maxThreadCount();pool->setMaxThreadCount(1);
        QSemaphore started,release;auto blocker=QtConcurrent::run(pool,[&]{started.release();release.acquire();});
        const auto restore=qScopeGuard([&]{release.release();blocker.waitForFinished();pool->setMaxThreadCount(threads);});started.acquire();
        QTest::keyClick(&window,Qt::Key_Return);QVERIFY(window.busy());QTest::keyClick(&window,Qt::Key_Escape);
        release.release();blocker.waitForFinished();QTRY_VERIFY(!window.busy());QVERIFY(window.status().contains("Stamp cancelled"));
        QCOMPARE(readMetadata(a).modified,before.modified);QCOMPARE(readMetadata(b).modified,before.modified);QCOMPARE(window.session().tags,tags);
    }
    void splitDestinationAndSavedLocationsRefresh(){
        const auto path=file("a.txt");TreeWindow window(root_);ready(window);QTest::keyClick(&window,Qt::Key_F8);
        QTest::keyClick(&window,Qt::Key_Tab);QTest::keyClick(&window,Qt::Key_Tab);
        QTest::keyClick(&window,Qt::Key_N);type(window,"2000-01-01 01:02:03");apply(window);
        QCOMPARE(window.oppositeSession()->currentFile()->modified,date("2000-01-01 01:02:03"));
        QTest::keyClick(&window,Qt::Key_Tab);QCOMPARE(window.session().currentFile()->path,path);QCOMPARE(window.session().currentFile()->modified,date("2000-01-01 01:02:03"));
        QCOMPARE(window.session().view,View::Directory);
    }
};
QTEST_MAIN(StampTests)
#include "test_stamp.moc"
