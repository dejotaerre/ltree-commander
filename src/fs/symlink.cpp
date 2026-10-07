#include "platform/platform.h"
#include "fs/symlink.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ltree {
SymlinkResult createDirectoryLink(const FileMetadata &source,const String &destination,const Cancellation &cancel)
{
    SymlinkResult result;result.path=destination;
    if(cancel->load()){result.cancelled=true;return result;}
    if(!source.error.isEmpty() || !source.directory || source.symlink ||
       !DirectoryPath::isAbsolutePath(source.path) || source.path.contains(Char::Null) ||
       DirectoryPath::cleanPath(source.path)!=source.path){
        result.error="Select a real directory to create its symbolic link";return result;
    }
    if(destination.isEmpty() || !DirectoryPath::isAbsolutePath(destination) || destination=="/" ||
       destination.contains(Char::Null) || DirectoryPath::cleanPath(destination)!=destination){
        result.error="Enter the full path of the new symbolic link";return result;
    }
    struct Descriptor { int fd=-1;~Descriptor(){if(fd>=0)::close(fd);} } sourceDir, parent;
    const auto openDirectory=[](const String &path){
        int fd=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        for(const auto &part:path.split('/',TextOptions::SkipEmptyParts)){
            const auto name=File::encodeName(part);
            const int next=fd<0 ? -1 : ::openat(fd,name.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            const int code=errno;if(fd>=0)::close(fd);fd=next;errno=code;
            if(fd<0)break;
        }
        return fd;
    };
    sourceDir.fd=openDirectory(source.path);
    struct stat before{};
    if(sourceDir.fd<0 || ::fstat(sourceDir.fd,&before)<0){
        result.error="Cannot open source directory: "+String::fromLocal8Bit(std::strerror(errno));return result;
    }
    if(uint64(before.st_dev)!=source.device || uint64(before.st_ino)!=source.inode ||
       before.st_ctim.tv_sec!=source.changedSeconds || before.st_ctim.tv_nsec!=source.changedNanoseconds){
        result.error="Source directory changed; reopen Shortcut";return result;
    }
    const String parentPath=FileInfo(destination).absolutePath();parent.fd=openDirectory(parentPath);
    if(parent.fd<0){result.error="Cannot open destination directory: "+String::fromLocal8Bit(std::strerror(errno));return result;}
    if(cancel->load()){result.cancelled=true;return result;}
    const auto target=File::encodeName(source.path),name=File::encodeName(FileInfo(destination).fileName());
    // symlinkat falla si el nombre existe, incluidos enlaces colgantes; nunca reemplaza.
    if(::symlinkat(target.constData(),parent.fd,name.constData())<0){
        result.error=errno==EEXIST?"Destination already exists; no replacement made":
            "Cannot create symbolic link: "+String::fromLocal8Bit(std::strerror(errno));return result;
    }
    result.created=true;
    if(::fsync(parent.fd)<0)result.error="Link created, but directory synchronization failed: "+String::fromLocal8Bit(std::strerror(errno));
    result.scan=scanDirectories(parentPath,false,std::make_shared<std::atomic_bool>(false));
    return result;
}
}
