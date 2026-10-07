#include "platform/platform.h"
#include "fs/archive.h"
#include "fs/safeio.h"
#include <archive.h>
#include <archive_entry.h>
#include <memory>
namespace ltree {
namespace {
using Reader=std::unique_ptr<struct archive,decltype(&archive_read_free)>;
using Writer=std::unique_ptr<struct archive,decltype(&archive_write_free)>;
String message(struct archive *a) { const auto s=archive_error_string(a);return s?String::fromUtf8(s):"Archive operation failed"; }
String memberPath(struct archive_entry *e) { const auto name=archive_entry_pathname_utf8(e);String path=String::fromUtf8(name?name:archive_entry_pathname(e));return path.replace('\\','/'); }
bool safePath(const String &path) {
    return !path.isEmpty() && !path.startsWith('/') && !path.contains(Char(0)) && !Regex("^[A-Za-z]:").match(path).hasMatch() && !path.split('/').contains("..");
}
Reader reader(const String &path,const String &password,String &error) {
    Reader a(archive_read_new(),archive_read_free);
    configureArchiveReader(a.get(),path);
    if(!password.isEmpty()) archive_read_add_passphrase(a.get(),password.toUtf8().constData());
    if(!FileInfo(path).isFile()) { error="Archive is not a regular file";return a; }
    if(archive_read_open_filename(a.get(),File::encodeName(path).constData(),65536)!=ARCHIVE_OK) error=message(a.get());
    return a;
}
}
void configureArchiveReader(struct archive *reader,const String &path)
{
    archive_read_support_filter_all(reader);
    // Se excluye mtree para no confundir texto descomprimido con un catálogo.
    for(const int format:{ARCHIVE_FORMAT_ZIP,ARCHIVE_FORMAT_TAR,ARCHIVE_FORMAT_7ZIP,ARCHIVE_FORMAT_RAR,ARCHIVE_FORMAT_RAR_V5,ARCHIVE_FORMAT_CPIO,ARCHIVE_FORMAT_AR,ARCHIVE_FORMAT_ISO9660,ARCHIVE_FORMAT_CAB,ARCHIVE_FORMAT_LHA,ARCHIVE_FORMAT_XAR})archive_read_support_format_by_code(reader,format);
    if(Regex("\\.(gz|bz2|xz|zst)$",Regex::CaseInsensitiveOption).match(path).hasMatch())archive_read_support_format_raw(reader);
}
bool archiveName(const String &path) { return Regex("\\.(zip|tar|tgz|tbz2?|txz|gz|bz2|xz|7z|rar|zst)$",Regex::CaseInsensitiveOption).match(path).hasMatch(); }
ArchiveCatalog listArchive(const String &path,const Cancellation &cancel,const String &password)
{
    ArchiveCatalog result;auto a=reader(path,password,result.error);if(!result.error.isEmpty())return result;
    struct archive_entry *entry=nullptr;int state=ARCHIVE_OK,index=0;
    while(!cancel->load() && (state=archive_read_next_header(a.get(),&entry))==ARCHIVE_OK) {
        if(archive_format(a.get())==ARCHIVE_FORMAT_RAW && archive_filter_code(a.get(),0)==ARCHIVE_FILTER_NONE){result.error="Not a compressed stream";break;}
        const String p=archive_format(a.get())==ARCHIVE_FORMAT_RAW?FileInfo(path).completeBaseName():memberPath(entry);const auto type=archive_entry_filetype(entry);
        result.entries.append({index++,p,archive_entry_size(entry),archive_entry_mtime(entry),uint32(archive_entry_perm(entry)),type==AE_IFDIR,type==AE_IFREG,safePath(p) && !archive_entry_symlink(entry) && !archive_entry_hardlink(entry),archive_entry_is_encrypted(entry)>0});
        if(archive_format(a.get())==ARCHIVE_FORMAT_RAW){result.format=String::fromUtf8(archive_format_name(a.get()));result.cancelled=cancel->load();return result;}
        if(result.entries.size()>100000) { result.error="Archive limit: 100,000 entries";break; }
        if(archive_read_data_skip(a.get())<ARCHIVE_OK) { result.error=message(a.get());break; }
    }
    result.cancelled=cancel->load();
    if(!result.cancelled && result.error.isEmpty() && state!=ARCHIVE_EOF)result.error=message(a.get());
    const auto format=archive_format_name(a.get());if(format)result.format=String::fromUtf8(format);
    return result;
}
ArchiveBytes readArchiveMember(const String &path,int index,const Cancellation &cancel,const String &password)
{
    ArchiveBytes result;auto a=reader(path,password,result.error);if(!result.error.isEmpty())return result;
    struct archive_entry *entry=nullptr;int current=0,state=ARCHIVE_OK;
    while(!cancel->load() && (state=archive_read_next_header(a.get(),&entry))==ARCHIVE_OK) {
        if(archive_format(a.get())==ARCHIVE_FORMAT_RAW && archive_filter_code(a.get(),0)==ARCHIVE_FILTER_NONE){result.error="Not a compressed stream";return result;}
        if(current++!=index) { archive_read_data_skip(a.get());continue; }
        if(archive_entry_filetype(entry)!=AE_IFREG || archive_entry_symlink(entry) || archive_entry_hardlink(entry)) { result.error="Only regular archive members can be viewed";return result; }
        if(archive_entry_size(entry)>32*1024*1024) { result.error="Viewer limit: 32 MiB per archive member";return result; }
        char buffer[65536];la_ssize_t size;
        while(!cancel->load() && (size=archive_read_data(a.get(),buffer,sizeof(buffer)))>0) {
            result.bytes.append(buffer,size);
            if(result.bytes.size()>32*1024*1024) { result.error="Viewer limit: 32 MiB per archive member";return result; }
        }
        result.cancelled=cancel->load();if(!result.cancelled && size<0)result.error=message(a.get());return result;
    }
    result.cancelled=cancel->load();if(!result.cancelled)result.error=state==ARCHIVE_EOF?"Archive member no longer exists":message(a.get());return result;
}
ArchiveResult extractArchive(const String &path,const Set<int> &selected,const String &destination,bool paths,bool replace,const Cancellation &cancel,const String &password,const std::function<void(int)> &progress)
{
    ArchiveResult result;auto a=reader(path,password,result.error);if(!result.error.isEmpty())return result;
    safeio::Fd root(safeio::directory(DirectoryPath::cleanPath(FileInfo(destination).absoluteFilePath()),true));
    if(root.value<0) { result.error=safeio::error();return result; }
    struct archive_entry *entry=nullptr;int current=0,state=ARCHIVE_OK;
    while(!cancel->load() && (state=archive_read_next_header(a.get(),&entry))==ARCHIVE_OK) {
        if(archive_format(a.get())==ARCHIVE_FORMAT_RAW && archive_filter_code(a.get(),0)==ARCHIVE_FILTER_NONE){result.error="Not a compressed stream";break;}
        if(!selected.contains(current++)) { archive_read_data_skip(a.get());continue; }
        String name=archive_format(a.get())==ARCHIVE_FORMAT_RAW?FileInfo(path).completeBaseName():memberPath(entry);const auto type=archive_entry_filetype(entry);
        if(!safePath(name) || archive_entry_symlink(entry) || archive_entry_hardlink(entry) || (type!=AE_IFREG && type!=AE_IFDIR)) { ++result.failed;if(result.error.isEmpty())result.error="Unsafe or unsupported member: "+name;archive_read_data_skip(a.get());continue; }
        name=DirectoryPath::cleanPath(name);if(!paths)name=FileInfo(name).fileName();
        const String target=DirectoryPath(destination).absoluteFilePath(name);
        safeio::Fd parent(safeio::directory(type==AE_IFDIR?target:FileInfo(target).absolutePath(),true));
        if(parent.value<0) { ++result.failed;if(result.error.isEmpty())result.error=name+": "+safeio::error();archive_read_data_skip(a.get());continue; }
        if(type==AE_IFDIR) { ++result.completed;if(progress)progress(result.completed+result.failed);continue; }
        const auto tmp=safeio::temporaryName();const auto leaf=File::encodeName(FileInfo(target).fileName());
        safeio::Fd fd(::openat(parent.value,tmp.constData(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
        String error;char buffer[65536];la_ssize_t size=0;
        if(fd.value<0) error=safeio::error();
        while(error.isEmpty() && !cancel->load() && (size=archive_read_data(a.get(),buffer,sizeof(buffer)))>0) {
            la_ssize_t done=0;while(done<size) { const auto n=::write(fd.value,buffer+done,size-done);if(n<0 && errno==EINTR)continue;if(n<=0){error=safeio::error();break;}done+=n; }
        }
        if(size<0)error=message(a.get());
        if(error.isEmpty() && !cancel->load()) {
            const mode_t mode=mode_t(archive_entry_perm(entry)) & 0777;
            struct timespec times[2]{{0,UTIME_OMIT},{archive_entry_mtime(entry),archive_entry_mtime_is_set(entry)?archive_entry_mtime_nsec(entry):UTIME_OMIT}};
            if(::fchmod(fd.value,mode?mode:0600)<0 || ::futimens(fd.value,times)<0 || ::fsync(fd.value)<0 || !safeio::publish(parent.value,tmp,leaf,replace))error=safeio::error();
            else ++result.completed;
        }
        ::unlinkat(parent.value,tmp.constData(),0);
        if(progress)progress(result.completed+result.failed);
        if(!error.isEmpty()) { ++result.failed;if(result.error.isEmpty())result.error=name+": "+error; }
    }
    result.cancelled=cancel->load();if(!result.cancelled && state!=ARCHIVE_EOF && result.error.isEmpty())result.error=message(a.get());return result;
}
ArchiveResult createArchive(const StringList &sources,const String &base,const String &destination,ArchiveFormat format,bool paths,bool replace,const Cancellation &cancel,const std::function<void(int)> &progress)
{
    ArchiveResult result;
    if(sources.isEmpty()) { result.error="Select at least one file";return result; }
    if(archiveStreamFormat(format) && sources.size()!=1) { result.error="GZ/BZ2/XZ compress one file. Use TAR.GZ/TAR.BZ2/TAR.XZ for multiple files";return result; }
    const FileInfo target(destination);safeio::Fd parent(safeio::directory(target.absolutePath()));
    if(parent.value<0) { result.error=safeio::error();return result; }
    Set<String> names;
    for(const auto &source:sources) {
        const auto name=paths?DirectoryPath(base).relativeFilePath(source):FileInfo(source).fileName();
        if(!safePath(name) || names.contains(name) || FileInfo(source).absoluteFilePath()==target.absoluteFilePath()) { result.error="Duplicate, unsafe or self-referencing archive path: "+name;return result; }
        names.insert(name);
    }
    const auto tmp=safeio::temporaryName();safeio::Fd output(::openat(parent.value,tmp.constData(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
    if(output.value<0) { result.error=safeio::error();return result; }
    Writer a(archive_write_new(),archive_write_free);int state=ARCHIVE_OK;
    if(format==ArchiveFormat::Zip) { state=archive_write_set_format_zip(a.get());if(state==ARCHIVE_OK)state=archive_write_set_options(a.get(),"zip:compression=deflate"); }
    else if(format==ArchiveFormat::SevenZip)state=archive_write_set_format_7zip(a.get());
    else if(archiveStreamFormat(format))state=archive_write_set_format_raw(a.get());
    else state=archive_write_set_format_pax_restricted(a.get());
    if(state==ARCHIVE_OK) {
        if(format==ArchiveFormat::Gzip || format==ArchiveFormat::TarGzip)state=archive_write_add_filter_gzip(a.get());
        else if(format==ArchiveFormat::Bzip2 || format==ArchiveFormat::TarBzip2)state=archive_write_add_filter_bzip2(a.get());
        else if(format==ArchiveFormat::Xz || format==ArchiveFormat::TarXz)state=archive_write_add_filter_xz(a.get());
    }
    if(state==ARCHIVE_OK)state=archive_write_set_bytes_in_last_block(a.get(),1);
    if(state!=ARCHIVE_OK || archive_write_open_fd(a.get(),output.value)!=ARCHIVE_OK)result.error=message(a.get());
    for(const auto &source:sources) {
        if(cancel->load() || !result.error.isEmpty())break;
        const FileInfo info(source);safeio::Fd dir(safeio::directory(info.absolutePath()));
        safeio::Fd input(dir.value<0 ? -1 : ::openat(dir.value,File::encodeName(info.fileName()).constData(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC));
        struct stat before{};
        if(input.value<0 || ::fstat(input.value,&before)<0 || !S_ISREG(before.st_mode)) { result.error="Cannot archive regular file: "+source;break; }
        auto entry=archive_entry_new();const auto name=(paths?DirectoryPath(base).relativeFilePath(source):info.fileName()).toUtf8();
        archive_entry_set_pathname_utf8(entry,name.constData());archive_entry_set_size(entry,before.st_size);archive_entry_set_filetype(entry,AE_IFREG);archive_entry_set_perm(entry,before.st_mode & 0777);archive_entry_set_mtime(entry,before.st_mtim.tv_sec,before.st_mtim.tv_nsec);
        if(archive_write_header(a.get(),entry)!=ARCHIVE_OK)result.error=message(a.get());
        archive_entry_free(entry);char buffer[65536];int64 total=0;
        while(!cancel->load() && result.error.isEmpty()) {
            const auto n=::read(input.value,buffer,sizeof(buffer));if(n<0 && errno==EINTR)continue;if(n<0){result.error=safeio::error();break;}if(n==0)break;
            total+=n;if(archive_write_data(a.get(),buffer,n)!=n){result.error=message(a.get());break;}
        }
        struct stat after{};
        if(result.error.isEmpty() && (::fstat(input.value,&after)<0 || total!=before.st_size || after.st_ctim.tv_sec!=before.st_ctim.tv_sec || after.st_ctim.tv_nsec!=before.st_ctim.tv_nsec))result.error="Source changed while archiving: "+source;
        if(result.error.isEmpty() && archive_write_finish_entry(a.get())!=ARCHIVE_OK)result.error=message(a.get());
        if(result.error.isEmpty())++result.completed;
        if(progress)progress(result.completed);
    }
    result.cancelled=cancel->load();
    if(archive_write_close(a.get())!=ARCHIVE_OK && result.error.isEmpty())result.error=message(a.get());
    if(!result.cancelled && result.error.isEmpty() && (::fsync(output.value)<0 || !safeio::publish(parent.value,tmp,File::encodeName(target.fileName()),replace)))result.error=safeio::error();
    ::unlinkat(parent.value,tmp.constData(),0);
    if(result.cancelled || !result.error.isEmpty())result.completed=0;
    return result;
}
ArchiveResult createZip(const StringList &sources,const String &base,const String &destination,bool paths,bool replace,const Cancellation &cancel,const std::function<void(int)> &progress)
{
    return createArchive(sources,base,destination,ArchiveFormat::Zip,paths,replace,cancel,progress);
}
String archiveFormatName(ArchiveFormat format)
{
    static const StringList names{"ZIP","TAR","TAR.GZ","TAR.BZ2","TAR.XZ","7Z","GZ","BZ2","XZ"};return names.value(int(format));
}
String archiveFormatSuffix(ArchiveFormat format)
{
    return '.'+archiveFormatName(format).toLower();
}
bool archiveStreamFormat(ArchiveFormat format)
{
    return format==ArchiveFormat::Gzip || format==ArchiveFormat::Bzip2 || format==ArchiveFormat::Xz;
}
bool archiveFormatFromPath(const String &path,ArchiveFormat &format)
{
    for(const auto candidate:{ArchiveFormat::TarGzip,ArchiveFormat::TarBzip2,ArchiveFormat::TarXz,ArchiveFormat::Zip,ArchiveFormat::Tar,ArchiveFormat::SevenZip,ArchiveFormat::Gzip,ArchiveFormat::Bzip2,ArchiveFormat::Xz})
        if(path.endsWith(archiveFormatSuffix(candidate),TextOptions::CaseInsensitive)){format=candidate;return true;}
    if(path.endsWith(".tgz",TextOptions::CaseInsensitive)){format=ArchiveFormat::TarGzip;return true;}
    if(path.endsWith(".tbz2",TextOptions::CaseInsensitive) || path.endsWith(".tbz",TextOptions::CaseInsensitive)){format=ArchiveFormat::TarBzip2;return true;}
    if(path.endsWith(".txz",TextOptions::CaseInsensitive)){format=ArchiveFormat::TarXz;return true;}
    return false;
}

}
