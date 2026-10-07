#include "platform/platform.h"
#include "fs/deletedirectory.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>

namespace ltree {
static DeleteResult deleteEntry(const String &root, const String &path, const Cancellation &cancel, bool directory)
{
    DeleteResult result;
    result.directory=directory;
    if(root.contains(Char(0)) || path.contains(Char(0))){result.error="Paths cannot contain NUL";return result;}
    result.path=DirectoryPath::cleanPath(FileInfo(path).absoluteFilePath());
    const auto base=DirectoryPath::cleanPath(FileInfo(root).absoluteFilePath());
    if(result.path==base || !isWithin(result.path,base)){
        result.error="Cannot delete the root or an entry outside it";return result;
    }
    if(cancel->load()){result.cancelled=true;return result;}
    const auto parts=DirectoryPath(base).relativeFilePath(result.path).split('/');
    int parent=::open(File::encodeName(base).constData(),O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    // Se recorre mediante descriptores para no seguir enlaces cambiados tras cargar el árbol.
    for(int i=0;parent>=0 && i<parts.size()-1;++i){
        const int next=::openat(parent,File::encodeName(parts[i]).constData(),O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        const int code=errno;::close(parent);parent=next;errno=code;
    }
    if(parent<0){result.error="Cannot open the source directory: "+String::fromLocal8Bit(std::strerror(errno));return result;}
    if(cancel->load()){::close(parent);result.cancelled=true;return result;}
    // Sin AT_REMOVEDIR se borra solo un archivo o enlace; nunca un directorio ni el destino del enlace.
    const int outcome=::unlinkat(parent,File::encodeName(parts.last()).constData(),directory?AT_REMOVEDIR:0);
    const int code=errno;::close(parent);
    if(outcome!=0){
        result.error=directory && (code==ENOTEMPTY || code==EEXIST) ? "The directory is not empty" :
            String(directory?"Cannot delete the directory: ":"Cannot delete the file: ")+String::fromLocal8Bit(std::strerror(code));
        return result;
    }
    result.deleted=true;
    result.scan=scanDirectories(FileInfo(result.path).absolutePath(),false,std::make_shared<std::atomic_bool>(false));
    return result;
}
DeleteResult deleteEmptyDirectory(const String &root, const String &path, const Cancellation &cancel)
{
    return deleteEntry(root,path,cancel,true);
}
DeleteResult deleteFile(const String &root, const String &path, const Cancellation &cancel)
{
    return deleteEntry(root,path,cancel,false);
}
DeleteResult deleteEmptyBranch(const String &root, const String &path, const Cancellation &cancel)
{
    DeleteResult result;
    result.path=DirectoryPath::cleanPath(FileInfo(path).absoluteFilePath());
    const auto base=DirectoryPath::cleanPath(FileInfo(root).absoluteFilePath());
    if(root.contains(Char(0)) || path.contains(Char(0)) || result.path==base || !isWithin(result.path,base)){
        result.error="Cannot delete the root or an entry outside it";return result;
    }
    result.scan=scanDirectories(result.path,true,cancel);
    if(result.scan.cancelled){result.cancelled=true;return result;}
    StringList directories;
    for(const auto &dir:result.scan.directories){
        if(!dir.error.isEmpty()){result.error="Cannot read branch: "+dir.error;return result;}
        if(!dir.files.isEmpty()){result.error="The branch still contains files";return result;}
        directories.append(dir.path);
    }
    const Set<String> present(directories.begin(),directories.end());
    for(const auto &dir:result.scan.directories)for(const auto &child:dir.children)if(!present.contains(child)){
        result.error="Cannot delete a directory link or nested mount point";return result;
    }
    std::sort(directories.begin(),directories.end(),[](const auto &a,const auto &b){return a.size()>b.size();});
    // Solo rmdir de directorios vacíos; un archivo aparecido después de la lectura impide su borrado.
    for(const auto &directory:directories){
        const auto removed=deleteEmptyDirectory(base,directory,cancel);
        if(!removed.deleted){
            result.error=removed.error;result.cancelled=removed.cancelled;
            result.scan=scanDirectories(result.path,true,std::make_shared<std::atomic_bool>(false));return result;
        }
        result.removedDirectories.append(directory);
        if(directory==result.path){result.deleted=true;result.scan=removed.scan;}
    }
    return result;
}
}
