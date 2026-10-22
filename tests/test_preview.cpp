#include "ui/fileviewer.h"
#include "ui/treewindow.h"
#include "core/buildinfo.h"
#include <QtTest>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <archive.h>
#include <QCryptographicHash>
#include <QUrl>
#include <QScopeGuard>
#include <archive_entry.h>
using namespace ltree;

class PreviewTests:public QObject {
    Q_OBJECT
    QTemporaryDir temp_;
    QByteArray previousConfig_;
    Cancellation cancel(){return std::make_shared<std::atomic_bool>(false);}
    QString fixture(const QString &name){return temp_.filePath(name);}
#ifndef Q_MOC_RUN
    void docx(const QString &path,bool textmaker=false){
        QMap<QString,QByteArray> files{
            {"[Content_Types].xml",R"(<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>)"},
            {"_rels/.rels",R"(<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>)"},
            {"word/document.xml",R"(<?xml version="1.0"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:rPr><w:b/><w:sz w:val="48"/></w:rPr><w:t>Office preview example</w:t></w:r></w:p><w:p><w:r><w:t>The original DOCX remains unchanged.</w:t></w:r></w:p><w:sectPr><w:pgSz w:w="12240" w:h="15840"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440"/></w:sectPr></w:body></w:document>)"}};
        if(textmaker)files["[Content_Types].xml"].replace("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml","application/textmaker.wordprocessingml.document.main+xml");
        auto *zip=archive_write_new();archive_write_set_format_zip(zip);QCOMPARE(archive_write_open_filename(zip,qPrintable(path)),ARCHIVE_OK);
        for(auto it=files.begin();it!=files.end();++it){
            auto *entry=archive_entry_new();archive_entry_set_pathname(entry,qPrintable(it.key()));archive_entry_set_size(entry,it.value().size());
            archive_entry_set_filetype(entry,AE_IFREG);archive_entry_set_perm(entry,0644);
            QCOMPARE(archive_write_header(zip,entry),ARCHIVE_OK);QCOMPARE(archive_write_data(zip,it.value().constData(),it.value().size()),it.value().size());archive_entry_free(entry);
        }
        archive_write_close(zip);archive_write_free(zip);
    }
#endif
private slots:
    void initTestCase(){previousConfig_=qgetenv("XDG_CONFIG_HOME");qputenv("XDG_CONFIG_HOME",fixture("config").toUtf8());}
    void cleanupTestCase(){if(previousConfig_.isNull())qunsetenv("XDG_CONFIG_HOME");else qputenv("XDG_CONFIG_HOME",previousConfig_);}
    void imageAndRawSwitch(){
        QImage red(320,160,QImage::Format_RGB32);red.fill(Qt::red);QVERIFY(red.save(fixture("red.png")));
        QImage green(160,320,QImage::Format_RGB32);green.fill(Qt::green);QVERIFY(green.save(fixture("green.png")));
        FileViewer viewer(fixture("red.png"),nullptr,true);viewer.resize(640,560);viewer.show();
        auto *visual=viewer.findChild<VisualPreview*>("visualPreview");QVERIFY(visual);
        QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !visual->image().isNull(),10000);
        QCOMPARE(visual->image().pixelColor(0,0),QColor(Qt::red));
        viewer.setPath(fixture("green.png"));viewer.setPath(fixture("red.png"));viewer.setPath(fixture("green.png"));
        QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !visual->image().isNull(),10000);
        QCOMPARE(visual->image().pixelColor(0,0),QColor(Qt::green));
        QTest::keyClick(&viewer,Qt::Key_H);QTRY_VERIFY(!viewer.loading());QVERIFY(viewer.hexMode());QVERIFY(viewer.viewedBytes().startsWith("\x89PNG"));QVERIFY(visual->isHidden());
        QTest::keyClick(&viewer,Qt::Key_F12);QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !visual->image().isNull(),10000);QVERIFY(!visual->isHidden());
        const auto output=qEnvironmentVariable("LTREE_CAPTURE_DIR");if(!output.isEmpty()){QDir().mkpath(output);viewer.grab().save(output+"/autoview-image.png");}
    }
    void pdfPages(){
        if(QStandardPaths::findExecutable("pdftoppm").isEmpty())QSKIP("Poppler not installed");
        {QPdfWriter pdf(fixture("two.pdf"));QPainter painter(&pdf);painter.fillRect(0,0,pdf.width(),pdf.height(),Qt::red);pdf.newPage();painter.fillRect(0,0,pdf.width(),pdf.height(),Qt::green);}
        FileViewer viewer(fixture("two.pdf"),nullptr,true);viewer.resize(640,560);viewer.show();
        auto *visual=viewer.findChild<VisualPreview*>("visualPreview");QVERIFY(visual);
        QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !visual->image().isNull(),15000);QCOMPARE(visual->error(),QString());
        const auto first=visual->image();QTest::keyClick(&viewer,Qt::Key_PageDown);
        QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !visual->image().isNull(),15000);QCOMPARE(visual->page(),2);QVERIFY(first!=visual->image());
        auto source=preparePreview(fixture("two.pdf"),cancel());QCOMPARE(source->pages,2);
        auto raster=renderPreview(source,1,320,320,cancel());QVERIFY2(raster.error.isEmpty(),qPrintable(raster.error));QVERIFY(!raster.rgb.isEmpty());
        QVERIFY(raster.height>raster.width);
    }
    void officeDocument(){
        if(QStandardPaths::findExecutable("libreoffice").isEmpty() && QStandardPaths::findExecutable("soffice").isEmpty())QSKIP("LibreOffice not installed");
        const auto path=fixture("document with spaces.docx");docx(path);QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto original=file.readAll();file.close();
        auto source=preparePreview(path,cancel());QVERIFY2(source->error.isEmpty(),qPrintable(source->error));QCOMPARE(source->pages,1);
        auto raster=renderPreview(source,1,640,640,cancel());QVERIFY2(raster.error.isEmpty(),qPrintable(raster.error));QVERIFY(!raster.rgb.isEmpty());
        FileViewer viewer(path,nullptr,true);viewer.resize(640,720);viewer.show();auto *visual=viewer.findChild<VisualPreview*>("visualPreview");
        QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !visual->image().isNull(),40000);QCOMPARE(visual->error(),QString());
        const auto output=qEnvironmentVariable("LTREE_CAPTURE_DIR");if(!output.isEmpty()){QDir().mkpath(output);viewer.grab().save(output+"/autoview-docx.png");}
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),original);
        const auto temporary=source->temporary->path();raster={};source.reset();QVERIFY(!QDir(temporary).exists());
    }
    void missingReadersAndCancellation(){
        docx(fixture("document with spaces.docx"));
        const auto old=qgetenv("PATH");qputenv("PATH",temp_.path().toUtf8());
        const auto source=preparePreview(fixture("document with spaces.docx"),cancel());qputenv("PATH",old);
        QVERIFY(source->error.contains("LibreOffice"));
        auto stop=cancel();stop->store(true);QVERIFY(renderPreview(source,1,320,320,stop).cancelled);
        QFile file(fixture("broken.pdf"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("broken");file.close();
        QVERIFY(!preparePreview(file.fileName(),cancel())->error.isEmpty());
    }
    void textmakerWithoutDisplay(){
        if(QStandardPaths::findExecutable("libreoffice").isEmpty())QSKIP("LibreOffice not installed");
        const auto path=fixture("softmaker.docx");docx(path,true);
        const auto display=qgetenv("DISPLAY"),wayland=qgetenv("WAYLAND_DISPLAY");
        qunsetenv("DISPLAY");qunsetenv("WAYLAND_DISPLAY");
        auto source=preparePreview(path,cancel());
        if(display.isNull())qunsetenv("DISPLAY");else qputenv("DISPLAY",display);
        if(wayland.isNull())qunsetenv("WAYLAND_DISPLAY");else qputenv("WAYLAND_DISPLAY",wayland);
        QVERIFY2(source->error.isEmpty(),qPrintable(source->error));QCOMPARE(source->pages,1);
        auto raster=renderPreview(source,1,640,640,cancel());QVERIFY2(raster.error.isEmpty(),qPrintable(raster.error));QVERIFY(!raster.rgb.isEmpty());
    }
    void buildIdentity(){QVERIFY(QRegularExpression("^[0-9a-f]{10}$").match(buildId()).hasMatch());QVERIFY(versionDescription().contains(LTREE_VERSION));}
    void videoFrames(){
        const auto ffmpeg=QStandardPaths::findExecutable("ffmpeg");if(ffmpeg.isEmpty())QSKIP("FFmpeg not installed");
        const auto path=fixture("video with spaces.mkv");QProcess generate;
        generate.start(ffmpeg,{"-v","error","-f","lavfi","-i","color=c=red:s=160x90:r=2:d=12","-f","lavfi","-i","color=c=blue:s=160x90:r=2:d=12","-filter_complex","[0:v][1:v]concat=n=2:v=1:a=0[v]","-map","[v]","-c:v","ffv1",path});
        QVERIFY(generate.waitForFinished(15000));QCOMPARE(generate.exitCode(),0);
        auto source=preparePreview(path,cancel());QVERIFY2(source->error.isEmpty(),qPrintable(source->error));
        QCOMPARE(source->kind,PreviewKind::Video);QCOMPARE(source->pages,4);QCOMPARE(source->initialPage,2);QCOMPARE(source->durationMs,24000);
        QVERIFY(source->details.contains("160x90"));QVERIFY(source->details.contains("FFV1"));
        QCOMPARE(previewPosition(*source,2),QString("00:00:10 / 00:00:24"));
        auto first=renderPreview(source,-1,320,180,cancel());QVERIFY2(first.error.isEmpty(),qPrintable(first.error));QCOMPARE(first.page,2);
        auto next=renderPreview(source,3,320,180,cancel());QVERIFY2(next.error.isEmpty(),qPrintable(next.error));QVERIFY(first.rgb!=next.rgb);
        FileViewer viewer(path,nullptr,true);viewer.resize(640,560);viewer.show();auto *visual=viewer.findChild<VisualPreview*>("visualPreview");QVERIFY(visual);
        QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !visual->image().isNull(),15000);QCOMPARE(visual->page(),2);
        QTest::qWait(200);QTRY_VERIFY(!viewer.loading());
        const auto fixed=visual->image();
        for(const int key:{Qt::Key_PageDown,Qt::Key_PageUp,Qt::Key_Home,Qt::Key_End,Qt::Key_Space,Qt::Key_Backspace}){
            QTest::keyClick(&viewer,Qt::Key(key));QTest::qWait(50);QVERIFY(!viewer.loading());QCOMPARE(visual->page(),2);QCOMPARE(visual->image(),fixed);
        }
        QTest::keyClick(&viewer,Qt::Key_F3);QTRY_VERIFY(!viewer.loading());QCOMPARE(visual->page(),2);
        QTest::keyClick(&viewer,Qt::Key_H);QTRY_VERIFY(!viewer.loading());QVERIFY(viewer.hexMode());QVERIFY(viewer.viewedBytes().startsWith(QByteArray::fromHex("1a45dfa3")));
        QTest::keyClick(&viewer,Qt::Key_F12);QTRY_VERIFY(!viewer.loading());QVERIFY(!visual->image().isNull());
        const auto output=qEnvironmentVariable("LTREE_CAPTURE_DIR");if(!output.isEmpty()){QDir().mkpath(output);viewer.grab().save(output+"/autoview-video.png");}
        const auto previous=qgetenv("PATH");qputenv("PATH",temp_.path().toUtf8());const auto missing=preparePreview(path,cancel());qputenv("PATH",previous);QVERIFY(missing->error.contains("FFmpeg"));
        QFile broken(fixture("broken.mkv"));QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("not a video");broken.close();QVERIFY(!preparePreview(broken.fileName(),cancel())->error.isEmpty());
        QCOMPARE(previewKind(fixture("script.ts")),PreviewKind::None);
        const auto anamorphic=fixture("anamorphic.mkv");
        generate.start(ffmpeg,{"-v","error","-f","lavfi","-i","color=c=red:s=160x120:r=2:d=1","-vf","setsar=4/3","-c:v","ffv1",anamorphic});
        QVERIFY(generate.waitForFinished(15000));QCOMPARE(generate.exitCode(),0);
        auto wideSource=preparePreview(anamorphic,cancel());auto wide=renderPreview(wideSource,1,320,240,cancel());
        QVERIFY2(wide.error.isEmpty(),qPrintable(wide.error));QCOMPARE(wide.width,320);QCOMPARE(wide.height,180);
    }
    void sharedThumbnail(){
        const auto path=fixture("cached space á@&.mkv");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("not decoded when thumbnail exists");file.close();
        const auto cache=fixture("cache");const auto oldCache=qgetenv("XDG_CACHE_HOME");qputenv("XDG_CACHE_HOME",cache.toUtf8());
        const auto uri=QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded());
        const auto hash=QCryptographicHash::hash(uri.toUtf8(),QCryptographicHash::Md5).toHex();
        const auto dir=cache+"/thumbnails/large";QVERIFY(QDir().mkpath(dir));const auto thumb=dir+"/"+hash+".png";
        QImage image(32,16,QImage::Format_RGB32);image.fill(Qt::green);
        image.setText("Thumb::URI",uri);image.setText("Thumb::MTime",QString::number(QFileInfo(path).lastModified().toSecsSinceEpoch()));image.setText("Thumb::Size",QString::number(QFileInfo(path).size()));QVERIFY(image.save(thumb));
        const auto oldPath=qgetenv("PATH");qputenv("PATH",temp_.path().toUtf8());
        const auto source=preparePreview(path,cancel());qputenv("PATH",oldPath);
        QVERIFY2(source->error.isEmpty(),qPrintable(source->error));QCOMPARE(source->initialPage,0);QVERIFY(!source->videoProbed);
        const auto raster=renderPreview(source,-1,640,480,cancel());QCOMPARE(raster.page,0);QCOMPARE(raster.width,32);QVERIFY(!raster.rgb.isEmpty());QCOMPARE(previewPosition(*source,0),QString("Desktop thumbnail"));
        FileViewer viewer(path,nullptr,true);viewer.resize(640,560);viewer.show();auto *visual=viewer.findChild<VisualPreview*>("visualPreview");
        QTRY_VERIFY(!viewer.loading());QVERIFY(!visual->image().isNull());QCOMPARE(visual->page(),0);QCOMPARE(visual->image().pixelColor(0,0),QColor(Qt::green));
        for(const int key:{Qt::Key_PageDown,Qt::Key_PageUp,Qt::Key_Home,Qt::Key_End,Qt::Key_Space,Qt::Key_Backspace}){QTest::keyClick(&viewer,Qt::Key(key));QVERIFY(!viewer.loading());QCOMPARE(visual->page(),0);QCOMPARE(visual->image().pixelColor(0,0),QColor(Qt::green));}
        image.setText("Thumb::MTime","0");QVERIFY(image.save(thumb));QVERIFY(preparePreview(path,cancel())->thumbnailRgb.isEmpty());
        image.setText("Thumb::MTime",QString::number(QFileInfo(path).lastModified().toSecsSinceEpoch()));image.setText("Thumb::Size","999");QVERIFY(image.save(thumb));QVERIFY(preparePreview(path,cancel())->thumbnailRgb.isEmpty());
        image.setText("Thumb::Size",QString::number(QFileInfo(path).size()));image.setText("Thumb::URI","file:///wrong");QVERIFY(image.save(thumb));QVERIFY(preparePreview(path,cancel())->thumbnailRgb.isEmpty());
        QFile bad(thumb);QVERIFY(bad.open(QIODevice::WriteOnly));bad.write("broken PNG");bad.close();QVERIFY(preparePreview(path,cancel())->thumbnailRgb.isEmpty());
        if(oldCache.isNull())qunsetenv("XDG_CACHE_HOME");else qputenv("XDG_CACHE_HOME",oldCache);
    }
    void lateThumbnail(){
        const auto ffmpeg=QStandardPaths::findExecutable("ffmpeg");if(ffmpeg.isEmpty())QSKIP("FFmpeg not installed");
        const auto path=fixture("late thumbnail.mkv");QProcess generate;
        generate.start(ffmpeg,{"-v","error","-f","lavfi","-i","color=c=blue:s=160x90:r=1:d=12","-c:v","ffv1",path});QVERIFY(generate.waitForFinished(15000));QCOMPARE(generate.exitCode(),0);
        const auto old=qgetenv("XDG_CACHE_HOME");qputenv("XDG_CACHE_HOME",fixture("late-cache").toUtf8());
        const auto restore=qScopeGuard([old]{if(old.isNull())qunsetenv("XDG_CACHE_HOME");else qputenv("XDG_CACHE_HOME",old);});
        FileViewer automatic(path,nullptr,true),reloaded(path,nullptr,true);
        for(auto *viewer:{&automatic,&reloaded}){viewer->resize(640,560);viewer->show();QTRY_VERIFY_WITH_TIMEOUT(!viewer->loading(),15000);}
        auto *autoVisual=automatic.findChild<VisualPreview*>();auto *reloadVisual=reloaded.findChild<VisualPreview*>();
        QCOMPARE(autoVisual->page(),2);QVERIFY(!autoVisual->image().isNull());
        const auto uri=QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded());const auto hash=QCryptographicHash::hash(uri.toUtf8(),QCryptographicHash::Md5).toHex();
        const auto directory=fixture("late-cache/thumbnails/large");QVERIFY(QDir().mkpath(directory));
        QImage thumbnail(32,16,QImage::Format_RGB32);thumbnail.fill(Qt::green);thumbnail.setText("Thumb::URI",uri);thumbnail.setText("Thumb::MTime",QString::number(QFileInfo(path).lastModified().toSecsSinceEpoch()));QVERIFY(thumbnail.save(directory+"/"+hash+".png"));
        QTest::keyClick(&reloaded,Qt::Key_F3);QTRY_VERIFY(!reloaded.loading());QCOMPARE(reloadVisual->page(),0);QCOMPARE(reloadVisual->image().pixelColor(0,0),QColor(Qt::green));
        QTRY_COMPARE_WITH_TIMEOUT(autoVisual->page(),0,7000);QTRY_VERIFY(!automatic.loading());QCOMPARE(autoVisual->image().pixelColor(0,0),QColor(Qt::green));
    }
    void fontSpecimens(){
        QByteArray bytes;
        const auto be=[](quint32 n,int count){QByteArray out;for(int i=count-1;i>=0;--i)out+=char(n>>(8*i));return out;};
        const auto section=[&](QByteArray tag,QByteArray data){bytes+=tag+be(data.size(),4)+data;};
        section("FILE","PFF2");section("NAME",QByteArray("Test Bitmap",12));section("MAXH",be(7,2));
        const auto glyph=be(5,2)+be(7,2)+be(0,2)+be(0xffff,2)+be(6,2)+QByteArray::fromHex("ffffffffff");
        const auto offset=bytes.size()+8+9+8;section("CHIX",be(65,4)+QByteArray(1,0)+be(offset,4));bytes+="DATA"+be(0xffffffff,4)+glyph;
        const auto path=fixture("test.pf2");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(bytes);file.close();
        auto source=preparePreview(path,cancel());QVERIFY2(source->error.isEmpty(),qPrintable(source->error));QCOMPARE(source->pages,2);QVERIFY(source->details.contains("Test Bitmap"));
        const auto grid=renderPreview(source,2,640,480,cancel());QVERIFY(!grid.rgb.isEmpty());
        // Cinco bits por fila: el segundo renglón cruza el límite del primer byte.
        const auto at=(int(15+54+30/2-35/2)-30+5)*1000*3+(20+40-25/2)*3;
        QCOMPARE(static_cast<unsigned char>(grid.rgb[at]),255);
        auto stopped=cancel();stopped->store(true);QVERIFY(renderPreview(source,1,640,480,stopped).cancelled);
        for(const auto malformed:QList<QByteArray>{bytes.left(10),bytes.left(bytes.size()-1),QByteArray("FILE")}){
            QVERIFY(file.open(QIODevice::WriteOnly));file.write(malformed);file.close();QVERIFY(!preparePreview(path,cancel())->error.isEmpty());
        }
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(bytes);file.close();
        const QStringList fonts={"/usr/share/fonts/Adwaita/AdwaitaMono-Regular.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/TTF/DejaVuSans.ttf"};
        for(const auto &font:fonts)if(QFile::exists(font)){
            source=preparePreview(font,cancel());QVERIFY2(source->error.isEmpty(),qPrintable(source->error));QVERIFY(source->pages>2);
            auto sample=renderPreview(source,1,640,480,cancel());auto characters=renderPreview(source,2,640,480,cancel());QVERIFY(!sample.rgb.isEmpty());QVERIFY(sample.rgb!=characters.rgb);
            FileViewer viewer(font,nullptr,true);viewer.resize(1000,800);viewer.show();auto *visual=viewer.findChild<VisualPreview*>("visualPreview");QTRY_VERIFY(!viewer.loading());QVERIFY(!visual->image().isNull());
            QTest::keyClick(&viewer,Qt::Key_PageDown);QTRY_VERIFY(!viewer.loading());QCOMPARE(visual->page(),2);
            QTest::keyClick(&viewer,Qt::Key_H);QTRY_VERIFY(!viewer.loading());QVERIFY(viewer.hexMode());QVERIFY(!viewer.viewedBytes().isEmpty());
            QTest::keyClick(&viewer,Qt::Key_F12);QTRY_VERIFY(!viewer.loading());QVERIFY(!visual->isHidden());
            const auto output=qEnvironmentVariable("LTREE_CAPTURE_DIR");if(!output.isEmpty())viewer.grab().save(output+"/autoview-font.png");
            break;
        }
    }
    void treeIntegration(){
        if(QStandardPaths::findExecutable("libreoffice").isEmpty())QSKIP("LibreOffice not installed");
        const auto dir=fixture("panel");QVERIFY(QDir().mkpath(dir));docx(dir+"/document.docx");
        TreeWindow window(dir);window.resize(1280,800);window.setTreeSizesVisible(true);window.show();QTRY_VERIFY(!window.busy());
        QTest::keyClick(&window,Qt::Key_Return);QTest::keyClick(&window,Qt::Key_F7);
        auto *viewer=window.findChild<FileViewer*>("autoview");QVERIFY(viewer);auto *visual=viewer->findChild<VisualPreview*>("visualPreview");QVERIFY(visual);
        QTRY_VERIFY_WITH_TIMEOUT(!viewer->loading() && !visual->image().isNull(),40000);
        const auto output=qEnvironmentVariable("LTREE_CAPTURE_DIR");if(!output.isEmpty()){QDir().mkpath(output);window.grab().save(output+"/autoview-panel-docx.png");}
        QTest::keyClick(&window,Qt::Key_F7);QTRY_VERIFY(!window.findChild<FileViewer*>("autoview"));
    }
};
QTEST_MAIN(PreviewTests)
#include "test_preview.moc"
