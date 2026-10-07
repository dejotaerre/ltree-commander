#include "platform/platform.h"
#include "fs/makedirectory.h"
#include <cerrno>
#include <cstring>
#include <sys/stat.h>

namespace ltree {
MakeDirectoryResult makeDirectory(const String &base, const String &name, const Cancellation &cancel)
{
    MakeDirectoryResult result;
    const auto parts=name.split('/',TextOptions::SkipEmptyParts);
    if(name.isEmpty() || parts.isEmpty())result.error="Enter the directory name";
    else if(DirectoryPath::isAbsolutePath(name))result.error="Use a path relative to the current directory";
    else if(name.contains(Char(0)) || parts.contains(".") || parts.contains(".."))result.error="Paths cannot contain NUL, . or ..";
    else if(FileInfo(base).isSymLink())result.error="Following directory links is not implemented yet";
    else if(!FileInfo(base).isDir())result.error="The source directory no longer exists";
    if(!result.error.isEmpty())return result;
    String current=DirectoryPath(base).absolutePath();
    result.path=DirectoryPath(current).filePath(parts.join('/'));
    StringList visited{current};
    for(int i=0;i<parts.size();++i){
        if(cancel->load()){result.cancelled=true;break;}
        current=DirectoryPath(current).filePath(parts[i]);
        const auto bytes=File::encodeName(current);
        if(::mkdir(bytes.constData(),0777)!=0){
            const int code=errno;
            const FileInfo existing(current);
            if(code==EEXIST && existing.isDir() && !existing.isSymLink() && i<parts.size()-1){visited.append(current);continue;}
            if(existing.isSymLink())result.error="Following directory links is not implemented yet: "+current;
            else if(code==EEXIST)result.error="The name already exists: "+current;
            else result.error="Cannot create "+current+": "+String::fromLocal8Bit(std::strerror(code));
            break;
        }
        result.created.append(current);visited.append(current);
    }
    // Las carpetas ya creadas se reflejan incluso tras un error o una cancelación.
    if(!result.created.isEmpty()){
        const auto refresh=std::make_shared<std::atomic_bool>(false);
        for(const auto &path:visited)result.scan.directories+=scanDirectories(path,false,refresh).directories;
    }
    return result;
}
}
