#include "fs/symlink.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ltree {
SymlinkResult createDirectoryLink(const FileMetadata &source,const QString &destination,const Cancellation &cancel)
{
    SymlinkResult result;result.path=destination;
    if(cancel->load()){result.cancelled=true;return result;}
    if(!source.error.isEmpty() || !source.directory || source.symlink ||
       !QDir::isAbsolutePath(source.path) || source.path.contains(QChar::Null) ||
       QDir::cleanPath(source.path)!=source.path){
        result.error="Select a real directory to create its symbolic link";return result;
    }
    if(destination.isEmpty() || !QDir::isAbsolutePath(destination) || destination=="/" ||
       destination.contains(QChar::Null) || QDir::cleanPath(destination)!=destination){
        result.error="Enter the full path of the new symbolic link";return result;
    }
    struct Descriptor { int fd=-1;~Descriptor(){if(fd>=0)::close(fd);} } sourceDir, parent;
    const auto openDirectory=[](const QString &path){
        int fd=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        for(const auto &part:path.split('/',Qt::SkipEmptyParts)){
            const auto name=QFile::encodeName(part);
            const int next=fd<0 ? -1 : ::openat(fd,name.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            const int code=errno;if(fd>=0)::close(fd);fd=next;errno=code;
            if(fd<0)break;
        }
        return fd;
    };
    sourceDir.fd=openDirectory(source.path);
    struct stat before{};
    if(sourceDir.fd<0 || ::fstat(sourceDir.fd,&before)<0){
        result.error="Cannot open source directory: "+QString::fromLocal8Bit(std::strerror(errno));return result;
    }
    if(quint64(before.st_dev)!=source.device || quint64(before.st_ino)!=source.inode ||
       before.st_ctim.tv_sec!=source.changedSeconds || before.st_ctim.tv_nsec!=source.changedNanoseconds){
        result.error="Source directory changed; reopen Shortcut";return result;
    }
    const QString parentPath=QFileInfo(destination).absolutePath();parent.fd=openDirectory(parentPath);
    if(parent.fd<0){result.error="Cannot open destination directory: "+QString::fromLocal8Bit(std::strerror(errno));return result;}
    if(cancel->load()){result.cancelled=true;return result;}
    const auto target=QFile::encodeName(source.path),name=QFile::encodeName(QFileInfo(destination).fileName());
    // symlinkat falla si el nombre existe, incluidos enlaces colgantes; nunca reemplaza.
    if(::symlinkat(target.constData(),parent.fd,name.constData())<0){
        result.error=errno==EEXIST?"Destination already exists; no replacement made":
            "Cannot create symbolic link: "+QString::fromLocal8Bit(std::strerror(errno));return result;
    }
    result.created=true;
    if(::fsync(parent.fd)<0)result.error="Link created, but directory synchronization failed: "+QString::fromLocal8Bit(std::strerror(errno));
    result.scan=scanDirectories(parentPath,false,std::make_shared<std::atomic_bool>(false));
    return result;
}
}
