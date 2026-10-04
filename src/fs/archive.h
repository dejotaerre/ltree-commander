#pragma once
#include "fs/scanner.h"
#include <QSet>
#include <functional>
struct archive;
namespace ltree {
void configureArchiveReader(struct archive *reader,const QString &path);
struct ArchiveEntry { int index=0;QString path; qint64 size=0,modified=0; quint32 mode=0; bool directory=false,regular=false,safe=false,encrypted=false; };
struct ArchiveCatalog { QVector<ArchiveEntry> entries;QString format,error;bool cancelled=false; };
struct ArchiveBytes { QByteArray bytes;QString error;bool cancelled=false; };
struct ArchiveResult { int completed=0,failed=0;QString error;bool cancelled=false; };
enum class ArchiveFormat {Zip,Tar,TarGzip,TarBzip2,TarXz,SevenZip,Gzip,Bzip2,Xz};
QString archiveFormatName(ArchiveFormat format);
QString archiveFormatSuffix(ArchiveFormat format);
bool archiveStreamFormat(ArchiveFormat format);
bool archiveFormatFromPath(const QString &path,ArchiveFormat &format);
ArchiveResult createArchive(const QStringList &sources,const QString &base,const QString &destination,ArchiveFormat format,bool paths,bool replace,const Cancellation &cancel,const std::function<void(int)> &progress={});
bool archiveName(const QString &path);
ArchiveCatalog listArchive(const QString &path,const Cancellation &cancel,const QString &password={});
ArchiveBytes readArchiveMember(const QString &path,int index,const Cancellation &cancel,const QString &password={});
ArchiveResult extractArchive(const QString &path,const QSet<int> &selected,const QString &destination,bool paths,bool replace,const Cancellation &cancel,const QString &password={},const std::function<void(int)> &progress={});
ArchiveResult createZip(const QStringList &sources,const QString &base,const QString &destination,bool paths,bool replace,const Cancellation &cancel,const std::function<void(int)> &progress={});
}
