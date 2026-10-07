#include "platform/platform.h"
#include "fs/metadata.h"
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ltree {
namespace {
String systemError() { return String::fromLocal8Bit(std::strerror(errno)); }
DateTime timeValue(const timespec &time)
{
    return DateTime::fromMSecsSinceEpoch(int64(time.tv_sec)*1000+time.tv_nsec/1000000);
}
struct Descriptor {
    int fd = -1;
    ~Descriptor() { if(fd>=0)::close(fd); }
};
}

String permissionOctal(uint32 mode) { return String::number(mode & 07777,8).rightJustified(4,'0'); }
String permissionText(uint32 mode)
{
    String text="---------";
    for(int i=0;i<9;++i)if(mode & (1u << (8-i)))text[i]=String("rwx")[i%3];
    if(mode&S_ISUID)text[2]=mode&S_IXUSR?'s':'S';
    if(mode&S_ISGID)text[5]=mode&S_IXGRP?'s':'S';
    if(mode&S_ISVTX)text[8]=mode&S_IXOTH?'t':'T';
    return text;
}

FileMetadata readMetadata(const String &path)
{
    FileMetadata result;result.path=path;
    if(path.isEmpty() || path.contains(Char::Null)){result.error="No valid path selected";return result;}
    struct stat value{};const auto native=File::encodeName(path);
    if(::lstat(native.constData(),&value)<0){result.error=systemError();return result;}
    result.mode=value.st_mode&07777;result.uid=value.st_uid;result.gid=value.st_gid;
    result.device=value.st_dev;result.inode=value.st_ino;result.links=value.st_nlink;
    result.size=value.st_size;result.allocated=uint64(value.st_blocks)*512;
    result.accessed=timeValue(value.st_atim);result.modified=timeValue(value.st_mtim);result.changed=timeValue(value.st_ctim);
    result.changedSeconds=value.st_ctim.tv_sec;result.changedNanoseconds=value.st_ctim.tv_nsec;
    result.accessedNanoseconds=value.st_atim.tv_nsec;result.modifiedNanoseconds=value.st_mtim.tv_nsec;
    result.directory=S_ISDIR(value.st_mode);result.regular=S_ISREG(value.st_mode);result.symlink=S_ISLNK(value.st_mode);
    result.type=result.directory?"Directory":result.symlink?"Symbolic link":S_ISREG(value.st_mode)?"Regular file":
        S_ISFIFO(value.st_mode)?"FIFO":S_ISSOCK(value.st_mode)?"Socket":S_ISBLK(value.st_mode)?"Block device":"Character device";
    char buffer[16384];struct passwd pw{},*pwp=nullptr;struct group gr{},*grp=nullptr;
    if(::getpwuid_r(value.st_uid,&pw,buffer,sizeof(buffer),&pwp)==0 && pwp)result.owner=String::fromLocal8Bit(pwp->pw_name);
    if(::getgrgid_r(value.st_gid,&gr,buffer,sizeof(buffer),&grp)==0 && grp)result.group=String::fromLocal8Bit(grp->gr_name);
    if(result.symlink){
        Bytes target(256,TextOptions::Uninitialized);
        for(;;){
            const auto count=::readlink(native.constData(),target.data(),size_t(target.size()));
            if(count<0){result.linkTarget="Unreadable: "+systemError();break;}
            if(count<target.size()){target.resize(count);result.linkTarget=File::decodeName(target);break;}
            target.resize(target.size()*2);
        }
    }
    struct statx extended{};
    if(::statx(AT_FDCWD,native.constData(),AT_SYMLINK_NOFOLLOW,STATX_BTIME|STATX_INO,&extended)==0 &&
       (extended.stx_mask&STATX_BTIME) && extended.stx_ino==result.inode){
        result.created=DateTime::fromMSecsSinceEpoch(int64(extended.stx_btime.tv_sec)*1000+extended.stx_btime.tv_nsec/1000000);
    }
    return result;
}

bool parsePermissions(const String &expression, uint32 current, bool directory, uint32 &result, String &error)
{
    const auto text=expression.trimmed();error.clear();result=current&07777;
    // Los modos numéricos son exactos; las cláusulas simbólicas exigen indicar quién.
    static const Regex octal("^[0-7]{3,4}$"),symbolic("^([ugoa]+)([+=-])([rwxXst]*)$");
    if(octal.match(text).hasMatch()){result=text.toUInt(nullptr,8);return true;}
    if(text.isEmpty()){error="Enter permissions, e.g. 755, u+w or go-w";return false;}
    for(const auto &clause:text.split(',')){
        const auto match=symbolic.match(clause);
        if(!match.hasMatch()){error="Use 3/4 octal digits or explicit u/g/o/a clauses: u+rwx,go-w";return false;}
        const auto who=match.captured(1),rights=match.captured(3);const auto op=match.captured(2);
        if(rights.isEmpty() && op!="="){error="Missing permissions after + or -";return false;}
        const bool u=who.contains('u')||who.contains('a'),g=who.contains('g')||who.contains('a'),o=who.contains('o')||who.contains('a');
        if((rights.contains('s')&&!u&&!g) || (rights.contains('t')&&!o)){
            error="Use u/g for s (set-ID), and o/a for t (sticky)";return false;
        }
        uint32 bits=0,mask=(u?04700u:0u)|(g?02070u:0u)|(o?01007u:0u);
        const bool execute=rights.contains('x')||(rights.contains('X')&&(directory||(result&0111)));
        const uint32 basic=(rights.contains('r')?4u:0u)|(rights.contains('w')?2u:0u)|(execute?1u:0u);
        if(u)bits|=basic<<6;
        if(g)bits|=basic<<3;
        if(o)bits|=basic;
        if(rights.contains('s'))bits|=(u?04000u:0u)|(g?02000u:0u);
        if(rights.contains('t'))bits|=01000u;
        if(op=="+")result|=bits;else if(op=="-")result&=~bits;else result=(result&~mask)|bits;
    }
    return true;
}

PermissionResult changePermissions(const FileMetadata &expected, uint32 mode, const Cancellation &cancel)
{
    PermissionResult result;
    if(cancel && cancel->load()){result.cancelled=true;return result;}
    if(!expected.error.isEmpty() || (!expected.directory && !expected.regular) || expected.symlink || mode>07777 ||
       !DirectoryPath::isAbsolutePath(expected.path) || expected.path.contains(Char::Null)){
        result.error="Permissions require a regular file or directory; symbolic links are not supported";return result;
    }
    // O_PATH permite restaurar permisos incluso en un archivo o directorio con modo 0000.
    Descriptor directory;directory.fd=::open("/",O_PATH|O_DIRECTORY|O_CLOEXEC);
    if(directory.fd<0){result.error=systemError();return result;}
    const auto parts=DirectoryPath::cleanPath(expected.path).split('/',TextOptions::SkipEmptyParts);
    for(int index=0;index<parts.size();++index){
        const auto &part=parts[index];
        const auto name=File::encodeName(part);
        const bool requireDirectory=index<parts.size()-1 || expected.directory;
        const int next=::openat(directory.fd,name.constData(),O_PATH|O_NOFOLLOW|O_CLOEXEC|(requireDirectory?O_DIRECTORY:0));
        if(next<0){result.error=systemError();return result;}
        ::close(directory.fd);directory.fd=next;
    }
    struct stat value{};
    if(::fstat(directory.fd,&value)<0){result.error=systemError();return result;}
    if(expected.directory!=bool(S_ISDIR(value.st_mode)) || expected.regular!=bool(S_ISREG(value.st_mode))){
        result.error="Selected item changed type since review; reopen Permissions";return result;
    }
    if(uint64(value.st_dev)!=expected.device || uint64(value.st_ino)!=expected.inode ||
       uint32(value.st_mode&07777)!=expected.mode || value.st_ctim.tv_sec!=expected.changedSeconds ||
       value.st_ctim.tv_nsec!=expected.changedNanoseconds){
        result.error="Selected item changed since review; reopen Permissions and try again";return result;
    }
    if(cancel && cancel->load()){result.cancelled=true;return result;}
    int changed=-1;
#ifdef SYS_fchmodat2
    changed=int(::syscall(SYS_fchmodat2,directory.fd,"",mode,AT_EMPTY_PATH));
    if(changed<0 && (errno==ENOSYS || errno==EINVAL || errno==EOPNOTSUPP))
#endif
    {
        // /proc mantiene el inodo fijado por el descriptor si se renombra la ruta.
        const auto pinned=Bytes("/proc/self/fd/")+Bytes::number(directory.fd);
        changed=::chmod(pinned.constData(),mode);
    }
    if(changed<0){result.error=systemError();return result;}
    result.changed=true;
    if(::fstat(directory.fd,&value)==0)result.mode=value.st_mode&07777;else result.mode=mode;
    result.writable=::faccessat(directory.fd,"",W_OK,AT_EMPTY_PATH|AT_EACCESS)==0;
    return result;
}

}
