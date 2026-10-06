#include "fs/fileattributes.h"
#include "fs/scanner.h"
#include "core/session.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

extern "C" ssize_t __real_lgetxattr(const char *, const char *, void *, size_t);
extern "C" ssize_t __wrap_lgetxattr(const char *path, const char *name, void *buffer, size_t size)
{
    // Probar ambos contratos NTFS en fixtures locales cuando no hay un montaje NTFS disponible.
    const QString file = QString::fromLocal8Bit(path).section('/', -1);
    if (file.startsWith("ntfs-fixture-")) {
        if (file == "ntfs-fixture-be" && !std::strcmp(name, "system.ntfs_attrib_be") && size >= 4) {
            qToBigEndian<quint32>(0x27, buffer);return 4;
        }
        if (file == "ntfs-fixture-native" && !std::strcmp(name, "system.ntfs_attrib") && size >= 4) {
            const quint32 flags = 0x22;std::memcpy(buffer, &flags, 4);return 4;
        }
        if (file == "ntfs-fixture-short") return 2;
        errno = EOPNOTSUPP;return -1;
    }
    return __real_lgetxattr(path, name, buffer, size);
}
using namespace ltree;
class FileAttributesTests : public QObject {
    Q_OBJECT
    QTemporaryDir fixture_;
    QString file(const QString &name) {
        const QString path = fixture_.filePath(name);QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write("unchanged") != 9) qFatal("Cannot create test fixture");
        f.close();return path;
    }
private slots:
    void mountResolution() {
        const QVector<MountPoint> mounts{{"/", "btrfs", "", false}, {"/mnt/fat", "vfat", "", false}, {"/mnt/fat/nested", "ext4", "", false}, {"/single-file", "ntfs3", "", false}};
        QCOMPARE(filesystemTypeForPath("/home/a", mounts),QString("btrfs"));
        QCOMPARE(filesystemTypeForPath("/mnt/fat/a", mounts),QString("vfat"));
        QCOMPARE(filesystemTypeForPath("/mnt/fat/nested/a", mounts),QString("ext4"));
        QCOMPARE(filesystemTypeForPath("/mnt/fat-other/a", mounts),QString("btrfs"));
        QCOMPARE(filesystemTypeForPath("/single-file", mounts),QString("ntfs3"));
    }
    void posixModesAndLinks() {
        const auto path = file("linux");QVERIFY(::chmod(QFile::encodeName(path).constData(), 0640) == 0);
        const auto a = readFileAttributes(path, "btrfs");QCOMPARE(a.style, AttributeStyle::Posix);QVERIFY(a.mode);QCOMPARE(*a.mode, quint32(0640));
        QCOMPARE(fileAttributeText(a, false),QString("rw-r----- ."));QCOMPARE(fileAttributeText(a, false, 6),QString("0640 ."));
        FileAttributes special;special.mode = 04755;QCOMPARE(fileAttributeText(special, false),QString("rwsr-xr-x ."));
        special.mode = 02710;QCOMPARE(fileAttributeText(special, false),QString("rwx--s--- ."));
        special.mode = 01000;QCOMPARE(fileAttributeText(special, false),QString("--------T ."));
        const auto link = fixture_.filePath("link");QVERIFY(::symlink("linux", QFile::encodeName(link).constData()) == 0);
        const auto l = readFileAttributes(link, "ext4");QVERIFY(l.mode);QCOMPARE(*l.mode, quint32(0777));QCOMPARE(fileAttributeText(l, true, 6),QString("0777 l"));
        QCOMPARE(readFileAttributes(path, "vfat").style,AttributeStyle::Dos);QVERIFY(!readFileAttributes(path, "vfat").dos);
        QCOMPARE(fileAttributeText(readFileAttributes(path,"vfat"),false,6),QString("???? ."));
    }
    void dosFormatting() {
        FileAttributes a;a.style = AttributeStyle::Dos;a.dos = 0;
        QCOMPARE(fileAttributeText(a, false, 6),QString(".... ."));
        a.dos = 0x27;QCOMPARE(fileAttributeText(a, false, 6),QString("HRSA ."));
        a.dos = 0x22;QCOMPARE(fileAttributeText(a, true, 6),QString("H..A l"));
        QCOMPARE(fileAttributeText(a, true),QString("H..A      l"));
        a.dos.reset();QCOMPARE(fileAttributeText(a, false, 6),QString("???? ."));
        a.style = AttributeStyle::Posix;QCOMPARE(fileAttributeText(a, false),QString("????????? ."));
    }
    void ntfsDriversAndUnavailableAttributes() {
        const auto be = file("ntfs-fixture-be"), native = file("ntfs-fixture-native"), missing = file("ntfs-fixture-missing"), malformed = file("ntfs-fixture-short");
        for (const auto &type : {"ntfs3", "ntfs", "fuse.ntfs-3g", "fuseblk"}) {
            const auto result = readFileAttributes(be, type);QCOMPARE(result.style,AttributeStyle::Dos);QVERIFY(result.dos);QCOMPARE(*result.dos,quint32(0x27));
            QCOMPARE(fileAttributeText(result,false,6),QString("HRSA ."));
            QCOMPARE(fileAttributeText(readFileAttributes(native,type),false,6),QString("H..A ."));
            QCOMPARE(fileAttributeText(readFileAttributes(missing,type),false,6),QString("???? ."));
            QVERIFY(!readFileAttributes(malformed,type).dos);
        }
        QCOMPARE(readFileAttributes(missing,"fuseblk").style,AttributeStyle::Unknown);
        QCOMPARE(fileAttributeText(readFileAttributes(be,"fuse"),false,6),QString("HRSA ."));
        QCOMPARE(readFileAttributes(missing,"fuse").style,AttributeStyle::Posix);
        const auto ordinary = readFileAttributes(be,"ext4");QCOMPARE(ordinary.style,AttributeStyle::Posix);QVERIFY(!ordinary.dos);
        QFile f(be);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),QByteArray("unchanged"));
    }
    void scanRefreshAndBranch() {
        const QString path = file("permissions.txt");QVERIFY(::chmod(QFile::encodeName(path).constData(),0600) == 0);
        auto cancel = std::make_shared<std::atomic_bool>(false);Session s(fixture_.path());s.apply(scanDirectories(fixture_.path(),false,cancel),false);s.enter(View::Branch);
        for(int i=0;i<s.files().size();++i)if(s.files()[i].path==path)s.fileIndex=i;
        s.setTag(true,false);QCOMPARE(fileAttributeText(s.currentFile()->attributes,false,6),QString("0600 ."));
        QVERIFY(::chmod(QFile::encodeName(path).constData(),0755) == 0);s.apply(scanDirectories(fixture_.path(),false,cancel),true);
        QCOMPARE(s.currentFile()->path,path);QVERIFY(s.tags.contains(path));QCOMPARE(fileAttributeText(s.currentFile()->attributes,false),QString("rwxr-xr-x ."));
    }
    void mountedDosFilesystem() {
        const QString root = qEnvironmentVariable("LTREE_DOS_TEST_DIRECTORY");
        if(root.isEmpty())QSKIP("An isolated FAT/NTFS image is required for this integration test");
        const auto cancel = std::make_shared<std::atomic_bool>(false);const auto scan = scanDirectories(root,false,cancel);
        QVERIFY(!scan.directories.isEmpty());QVERIFY2(scan.directories.first().error.isEmpty(),qPrintable(scan.directories.first().error));
        QMap<QString,QString> attrs;for(const auto &f:scan.directories.first().files)attrs.insert(f.name,fileAttributeText(f.attributes,f.symlink,6));
        QCOMPARE(attrs.value("all.txt"),QString("HRSA ."));QCOMPARE(attrs.value("none.txt"),QString(".... ."));
        QCOMPARE(attrs.value(".dot.txt"),QString(".... ."));QCOMPARE(attrs.value("archive.txt"),QString("...A ."));
    }
};
QTEST_GUILESS_MAIN(FileAttributesTests)
#include "test_file_attributes.moc"
