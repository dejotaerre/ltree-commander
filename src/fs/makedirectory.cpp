#include "fs/makedirectory.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>

namespace ltree {
MakeDirectoryResult makeDirectory(const QString &base, const QString &name, const Cancellation &cancel)
{
    MakeDirectoryResult result;
    const auto parts=name.split('/',Qt::SkipEmptyParts);
    if(name.isEmpty() || parts.isEmpty())result.error="Enter the directory name";
    else if(QDir::isAbsolutePath(name))result.error="Use a path relative to the current directory";
    else if(name.contains(QChar(0)) || parts.contains(".") || parts.contains(".."))result.error="Paths cannot contain NUL, . or ..";
    else if(QFileInfo(base).isSymLink())result.error="Following directory links is not implemented yet";
    else if(!QFileInfo(base).isDir())result.error="The source directory no longer exists";
    if(!result.error.isEmpty())return result;
    QString current=QDir(base).absolutePath();
    result.path=QDir(current).filePath(parts.join('/'));
    QStringList visited{current};
    for(int i=0;i<parts.size();++i){
        if(cancel->load()){result.cancelled=true;break;}
        current=QDir(current).filePath(parts[i]);
        const auto bytes=QFile::encodeName(current);
        if(::mkdir(bytes.constData(),0777)!=0){
            const int code=errno;
            const QFileInfo existing(current);
            if(code==EEXIST && existing.isDir() && !existing.isSymLink() && i<parts.size()-1){visited.append(current);continue;}
            if(existing.isSymLink())result.error="Following directory links is not implemented yet: "+current;
            else if(code==EEXIST)result.error="The name already exists: "+current;
            else result.error="Cannot create "+current+": "+QString::fromLocal8Bit(std::strerror(code));
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
