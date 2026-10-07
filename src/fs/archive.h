#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"
#include <functional>
struct archive;
namespace ltree {
void configureArchiveReader(struct archive *reader,const String &path);
struct ArchiveEntry { int index=0;String path; int64 size=0,modified=0; uint32 mode=0; bool directory=false,regular=false,safe=false,encrypted=false; };
struct ArchiveCatalog { Vector<ArchiveEntry> entries;String format,error;bool cancelled=false; };
struct ArchiveBytes { Bytes bytes;String error;bool cancelled=false; };
struct ArchiveResult { int completed=0,failed=0;String error;bool cancelled=false; };
enum class ArchiveFormat {Zip,Tar,TarGzip,TarBzip2,TarXz,SevenZip,Gzip,Bzip2,Xz};
String archiveFormatName(ArchiveFormat format);
String archiveFormatSuffix(ArchiveFormat format);
bool archiveStreamFormat(ArchiveFormat format);
bool archiveFormatFromPath(const String &path,ArchiveFormat &format);
ArchiveResult createArchive(const StringList &sources,const String &base,const String &destination,ArchiveFormat format,bool paths,bool replace,const Cancellation &cancel,const std::function<void(int)> &progress={});
bool archiveName(const String &path);
ArchiveCatalog listArchive(const String &path,const Cancellation &cancel,const String &password={});
ArchiveBytes readArchiveMember(const String &path,int index,const Cancellation &cancel,const String &password={});
ArchiveResult extractArchive(const String &path,const Set<int> &selected,const String &destination,bool paths,bool replace,const Cancellation &cancel,const String &password={},const std::function<void(int)> &progress={});
ArchiveResult createZip(const StringList &sources,const String &base,const String &destination,bool paths,bool replace,const Cancellation &cancel,const std::function<void(int)> &progress={});
}
