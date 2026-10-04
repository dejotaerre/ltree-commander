#include "fs/rename.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ltree {
RenameResult renameItem(const FileMetadata &expected,const QString &name,const Cancellation &cancel)
{
    RenameResult result;result.source=expected.path;
    if(cancel->load()){result.cancelled=true;return result;}
    if(!expected.error.isEmpty() || expected.symlink || (!expected.regular && !expected.directory) ||
       !QDir::isAbsolutePath(expected.path) || QDir::cleanPath(expected.path)!=expected.path || expected.path=="/" ||
       name.isEmpty() || name=="." || name==".." || name.contains('/') || name.contains(QChar::Null)){
        result.error="Rename requires a regular file or directory and a single valid name";return result;
    }
    const auto parent=QFileInfo(expected.path).absolutePath();result.target=QDir(parent).filePath(name);
    if(result.target==expected.path){result.error="Name is unchanged";return result;}
    struct Descriptor { int fd=-1;~Descriptor(){if(fd>=0)::close(fd);} } dir;
    dir.fd=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    for(const auto &part:parent.split('/',Qt::SkipEmptyParts)){
        const auto native=QFile::encodeName(part);
        const int next=dir.fd<0 ? -1 : ::openat(dir.fd,native.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(next<0){result.error="Cannot open parent directory: "+QString::fromLocal8Bit(std::strerror(errno));return result;}
        ::close(dir.fd);dir.fd=next;
    }
    if(dir.fd<0){result.error="Cannot open root directory";return result;}
    const auto original=QFile::encodeName(QFileInfo(expected.path).fileName()),target=QFile::encodeName(name);
    struct stat before{};
    if(::fstatat(dir.fd,original.constData(),&before,AT_SYMLINK_NOFOLLOW)<0){result.error=QString::fromLocal8Bit(std::strerror(errno));return result;}
    if(quint64(before.st_dev)!=expected.device || quint64(before.st_ino)!=expected.inode ||
       before.st_ctim.tv_sec!=expected.changedSeconds || before.st_ctim.tv_nsec!=expected.changedNanoseconds ||
       expected.directory!=bool(S_ISDIR(before.st_mode)) || expected.regular!=bool(S_ISREG(before.st_mode))){
        result.error="Selected item changed since review; reopen Rename";return result;
    }
    if(cancel->load()){result.cancelled=true;return result;}
    // Apartar y verificar evita publicar otro objeto si se sustituye el origen durante la operación.
    const auto staged=QFile::encodeName(".ltree-rename-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
    if(::syscall(SYS_renameat2,dir.fd,original.constData(),dir.fd,staged.constData(),RENAME_NOREPLACE)<0){
        result.error=QString::fromLocal8Bit(std::strerror(errno));return result;
    }
    struct stat held{};const bool same=::fstatat(dir.fd,staged.constData(),&held,AT_SYMLINK_NOFOLLOW)==0 &&
        held.st_dev==before.st_dev && held.st_ino==before.st_ino && held.st_mode==before.st_mode;
    if(!same || cancel->load()){
        result.cancelled=cancel->load();if(!same)result.error="Source changed during Rename";
    }else if(::syscall(SYS_renameat2,dir.fd,staged.constData(),dir.fd,target.constData(),RENAME_NOREPLACE)==0){
        result.renamed=true;
        if(::fsync(dir.fd)<0)result.error="Renamed, but directory synchronization failed: "+QString::fromLocal8Bit(std::strerror(errno));
    }else result.error=errno==EEXIST?"Destination already exists; no replacement made":QString::fromLocal8Bit(std::strerror(errno));
    if(!result.renamed && ::syscall(SYS_renameat2,dir.fd,staged.constData(),dir.fd,original.constData(),RENAME_NOREPLACE)<0)
        result.error+="; cannot restore source; item retained at "+QDir(parent).filePath(QFile::decodeName(staged));
    result.scan=scanDirectories(parent,false,std::make_shared<std::atomic_bool>(false));
    return result;
}
}
