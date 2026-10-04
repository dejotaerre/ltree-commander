#include "fs/copyfiles.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include <unistd.h>

namespace ltree {
namespace {
struct Token { QChar character; bool insert=false; };
QString maskSection(const QString &source,const QVector<Token> &tokens)
{
    QString output;int position=0;
    for(int i=0;i<tokens.size();++i){
        const auto token=tokens[i];const auto ch=token.character;
        if(ch=='*'){
            int remaining=0;
            for(int j=i+1;j<tokens.size();++j)if(!tokens[j].insert || tokens[j].character=='?' || tokens[j].character=='/')++remaining;
            const int count=std::max(0,int(source.size())-position-remaining);
            output+=source.mid(position,count);position+=count;
        }else if(ch=='?' || ch=='/'){
            if(position<source.size()){if(ch=='?')output+=source[position];++position;}
        }else{output+=ch;if(!token.insert && position<source.size())++position;}
    }
    return output;
}
struct Descriptor {
    int fd=-1;
    explicit Descriptor(int value=-1):fd(value){}
    ~Descriptor(){if(fd>=0)::close(fd);}
    void reset(int value){if(fd>=0)::close(fd);fd=value;}
};
int openDirectory(const QString &path,bool create,QStringList *created)
{
    Descriptor current(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));QString prefix;
    for(const auto &part:path.split('/',Qt::SkipEmptyParts)){
        const auto bytes=QFile::encodeName(part);prefix+='/'+part;
        int next=::openat(current.fd,bytes.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(next<0 && errno==ENOENT && create){
            if(::mkdirat(current.fd,bytes.constData(),0777)==0)created->append(prefix);
            else if(errno!=EEXIST)return -1;
            next=::openat(current.fd,bytes.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        }
        if(next<0)return -1;
        current.reset(next);
    }
    const int fd=current.fd;current.fd=-1;return fd;
}
QString systemError(const QString &action){return action+": "+QString::fromLocal8Bit(std::strerror(errno));}
bool sameFile(const struct stat &a,const struct stat &b){return a.st_dev==b.st_dev && a.st_ino==b.st_ino;}
bool older(const struct stat &a,const struct stat &b){return a.st_mtim.tv_sec<b.st_mtim.tv_sec || (a.st_mtim.tv_sec==b.st_mtim.tv_sec && a.st_mtim.tv_nsec<b.st_mtim.tv_nsec);}
bool sameContents(const struct stat &a,const struct stat &b)
{
    return sameFile(a,b) && a.st_size==b.st_size && a.st_mtim.tv_sec==b.st_mtim.tv_sec && a.st_mtim.tv_nsec==b.st_mtim.tv_nsec;
}
bool unchanged(const struct stat &a,const struct stat &b)
{
    return sameContents(a,b) && a.st_ctim.tv_sec==b.st_ctim.tv_sec && a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
bool restoreSource(int parent,const QByteArray &staged,const QByteArray &name,const QString &path,CopyResult &result)
{
    if(::syscall(SYS_renameat2,parent,staged.constData(),parent,name.constData(),RENAME_NOREPLACE)==0)return true;
    result.error+="; source preserved at "+QDir(QFileInfo(path).absolutePath()).filePath(QString::fromLocal8Bit(staged))+": "+QString::fromLocal8Bit(std::strerror(errno));
    return false;
}
bool stageSource(int parent,const QByteArray &name,const struct stat &expected,const QString &path,QByteArray &staged,CopyResult &result)
{
    struct stat current{};
    if(::fstatat(parent,name.constData(),&current,AT_SYMLINK_NOFOLLOW)<0 || !unchanged(expected,current)){
        result.error="Source changed; it was not removed";return false;
    }
    staged=QFile::encodeName(".ltree-move-"+QUuid::createUuid().toString(QUuid::Id128));
    if(::syscall(SYS_renameat2,parent,name.constData(),parent,staged.constData(),RENAME_NOREPLACE)<0){
        result.error=systemError("Cannot move source file");return false;
    }
    // Se comprueba el objeto apartado antes de moverlo o borrarlo; un reemplazo concurrente se conserva.
    if(::fstatat(parent,staged.constData(),&current,AT_SYMLINK_NOFOLLOW)<0 || !sameContents(expected,current)){
        result.error="Source changed during move; it was not removed";restoreSource(parent,staged,name,path,result);return false;
    }
    return true;
}

CopyResult copyOne(const QString &sourceRoot,const CopyEntry &entry,CopyReplace replace,const Cancellation &cancel,bool move=false)
{
    CopyResult result;result.target=entry.target;
    if(entry.source.contains(QChar(0)) || entry.target.contains(QChar(0)) || !QDir::isAbsolutePath(entry.target) ||
       entry.source!=QDir::cleanPath(entry.source) || entry.target!=QDir::cleanPath(entry.target) || !isWithin(entry.source,sourceRoot)){
        result.error="Invalid source or destination path";return result;
    }
    if(cancel->load()){result.cancelled=true;return result;}
    Descriptor sourceParent(openDirectory(QFileInfo(entry.source).absolutePath(),false,nullptr));
    if(sourceParent.fd<0){result.error=systemError("Cannot open source directory");return result;}
    const QByteArray sourceName=QFile::encodeName(QFileInfo(entry.source).fileName());
    Descriptor source(::openat(sourceParent.fd,sourceName.constData(),(move?O_PATH:O_RDONLY|O_NONBLOCK)|O_NOFOLLOW|O_CLOEXEC));
    struct stat info{};
    if(source.fd<0 || ::fstat(source.fd,&info)<0){result.error=systemError("Cannot open source file (links are not followed)");return result;}
    if(!S_ISREG(info.st_mode)){result.error=move?"Moving this file type is not implemented yet":"Copying this file type is not implemented yet";return result;}
    Descriptor destination(openDirectory(QFileInfo(entry.target).absolutePath(),true,&result.created));
    if(destination.fd<0){result.error=systemError("Cannot create destination directory (links are not followed)");return result;}
    QByteArray target=QFile::encodeName(QFileInfo(entry.target).fileName());struct stat existing{};
    bool present=::fstatat(destination.fd,target.constData(),&existing,AT_SYMLINK_NOFOLLOW)==0;
    if(!present && errno!=ENOENT){result.error=systemError("Cannot inspect destination");return result;}
    if(present && sameFile(info,existing)){result.error="Source and destination are the same file";return result;}
    if(present && !S_ISREG(existing.st_mode)){result.error="Destination is not a regular file; links are not replaced";return result;}
    if(present && replace==CopyReplace::Ask){result.exists=true;return result;}
    if(present && (replace==CopyReplace::Never || (replace==CopyReplace::Older && !older(existing,info)))){result.skipped=true;return result;}
    if(present && replace==CopyReplace::Rename){
        const QString original=QFileInfo(entry.target).fileName();const int dot=int(original.lastIndexOf('.'));
        const QString stem=dot>0?original.left(dot):original,ext=dot>0?original.mid(dot):QString{};
        int ordinal=2;
        do{
            const auto name=stem+QString("(%1)").arg(ordinal++)+ext;target=QFile::encodeName(name);
            result.target=QDir(QFileInfo(entry.target).absolutePath()).filePath(name);
            present=::fstatat(destination.fd,target.constData(),&existing,AT_SYMLINK_NOFOLLOW)==0;
            if(!present && errno!=ENOENT){result.error=systemError("Cannot inspect destination");return result;}
        }while(present && ordinal<100000);
        if(present){result.error="No available sequential filename";return result;}
    }
    if(move){
        if(cancel->load()){result.cancelled=true;return result;}
        struct stat current{};const bool existsNow=::fstatat(destination.fd,target.constData(),&current,AT_SYMLINK_NOFOLLOW)==0;
        if(!existsNow && errno!=ENOENT){result.error=systemError("Cannot inspect destination");return result;}
        if(existsNow && (!S_ISREG(current.st_mode) || sameFile(info,current))){result.error="Destination changed or is the source file";return result;}
        if(existsNow && replace==CopyReplace::Older && !older(current,info)){result.skipped=true;return result;}
        QByteArray staged;
        if(!stageSource(sourceParent.fd,sourceName,info,entry.source,staged,result))return result;
        if(cancel->load()){
            result.cancelled=true;restoreSource(sourceParent.fd,staged,sourceName,entry.source,result);return result;
        }
        const bool replaceAllowed=present && (replace==CopyReplace::Always || replace==CopyReplace::Older);
        if(::syscall(SYS_renameat2,sourceParent.fd,staged.constData(),destination.fd,target.constData(),replaceAllowed?0:RENAME_NOREPLACE)==0){
            result.moved=true;
            if(::fsync(destination.fd)<0 || ::fsync(sourceParent.fd)<0)result.error=systemError("File moved, but directory synchronization failed");
            return result;
        }
        const int failure=errno;
        if(failure!=EXDEV){
            if(failure==EEXIST){if(replace==CopyReplace::Never)result.skipped=true;else result.exists=true;}
            else result.error=systemError("Cannot publish moved file");
        }
        if(!restoreSource(sourceParent.fd,staged,sourceName,entry.source,result) || failure!=EXDEV)return result;
        // Entre montajes se vuelve al origen y se utiliza la copia temporal antes de retirarlo.
        struct stat restored{};
        if(::fstat(source.fd,&restored)<0 || !sameContents(info,restored)){result.error="Source changed; it was not removed";return result;}
        source.reset(::openat(sourceParent.fd,sourceName.constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
        if(source.fd<0){result.error=systemError("Cannot read source for cross-filesystem move");return result;}
        if(::fstat(source.fd,&info)<0 || !unchanged(restored,info)){result.error="Source changed; it was not removed";return result;}
    }
    const QByteArray temporary=QFile::encodeName(".ltree-copy-"+QUuid::createUuid().toString(QUuid::Id128));
    Descriptor output(::openat(destination.fd,temporary.constData(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600));
    if(output.fd<0){result.error=systemError("Cannot create copy");return result;}
    // La copia temporal evita truncar destinos; solo se publica tras completar datos y metadatos.
    const auto cleanup=[&]{::unlinkat(destination.fd,temporary.constData(),0);};
    char buffer[128*1024];
    while(!cancel->load()){
        const auto count=::read(source.fd,buffer,sizeof(buffer));
        if(count<0){if(errno==EINTR)continue;result.error=systemError("Cannot read source file");break;}
        if(count==0)break;
        ssize_t offset=0;
        while(offset<count && !cancel->load()){
            const auto written=::write(output.fd,buffer+offset,size_t(count-offset));
            if(written<0 && errno==EINTR)continue;
            if(written<=0){result.error=systemError("Cannot write destination file");break;}
            offset+=written;
        }
        if(!result.error.isEmpty())break;
    }
    result.cancelled=cancel->load();
    const timespec times[]{info.st_atim,info.st_mtim};
    if(!result.cancelled && result.error.isEmpty() && (::fchmod(output.fd,info.st_mode&0777)<0 || ::futimens(output.fd,times)<0 || ::fsync(output.fd)<0))
        result.error=systemError("Cannot preserve copy metadata");
    if(result.cancelled || !result.error.isEmpty()){cleanup();return result;}
    if(move){
        struct stat current{};
        if(::fstat(source.fd,&current)<0 || !unchanged(info,current)){
            result.error="Source changed while copying; it was not removed";cleanup();return result;
        }
    }
    struct stat now{};const bool existsNow=::fstatat(destination.fd,target.constData(),&now,AT_SYMLINK_NOFOLLOW)==0;
    if(existsNow && (!S_ISREG(now.st_mode) || sameFile(info,now))){result.error="Destination changed or is the source file";cleanup();return result;}
    if(existsNow && replace==CopyReplace::Older && !older(now,info)){result.skipped=true;cleanup();return result;}
    const bool replaceAllowed=present && (replace==CopyReplace::Always || replace==CopyReplace::Older);
    if(cancel->load()){result.cancelled=true;cleanup();return result;}
    if(::syscall(SYS_renameat2,destination.fd,temporary.constData(),destination.fd,target.constData(),replaceAllowed?0:RENAME_NOREPLACE)<0){
        if(errno==EEXIST){if(replace==CopyReplace::Never)result.skipped=true;else result.exists=true;}
        else result.error=systemError("Cannot publish copied file");
        cleanup();return result;
    }
    result.copied=true;
    if(move){
        if(::fsync(destination.fd)<0){result.error=systemError("Copy complete; source retained because destination synchronization failed");return result;}
        if(cancel->load()){result.cancelled=true;return result;}
        QByteArray staged;
        if(!stageSource(sourceParent.fd,sourceName,info,entry.source,staged,result)){
            result.error="Copy complete; source retained: "+result.error;return result;
        }
        if(cancel->load()){
            result.cancelled=true;restoreSource(sourceParent.fd,staged,sourceName,entry.source,result);return result;
        }
        if(::unlinkat(sourceParent.fd,staged.constData(),0)<0){
            result.error=systemError("Copy complete; cannot remove source");restoreSource(sourceParent.fd,staged,sourceName,entry.source,result);return result;
        }
        result.moved=true;
        if(::fsync(sourceParent.fd)<0)result.error=systemError("File moved, but source directory synchronization failed");
    }
    return result;
}
}

QString copyName(const QString &name,const QString &input,CopyCase letterCase,QString *error)
{
    error->clear();const QString mask=input.isEmpty()?QString("*.*"):input;
    QVector<Token> parts[2];bool insertion=false;int separator=-1;
    for(int i=0;i<mask.size();++i){
        const auto ch=mask[i];
        if(ch=='<'){if(insertion){*error="Invalid insertion pair in mask";return {};}insertion=true;}
        else if(ch=='>'){if(!insertion){*error="Invalid insertion pair in mask";return {};}insertion=false;}
        else if(ch=='.' && !insertion)separator=i;
    }
    if(insertion){*error="Unclosed insertion pair in mask";return {};}
    int section=0;
    for(int i=0;i<mask.size();++i){
        const auto ch=mask[i];
        if(i==separator){section=1;continue;}
        if(ch=='<'){insertion=true;continue;}if(ch=='>'){insertion=false;continue;}
        if(ch==QChar(0) || ch=='|' || ch==':' || ch=='"' || ch=='\\'){
            *error="Sequences, find/replace and piped masks are not implemented yet";return {};
        }
        parts[section].append({ch,insertion});
    }
    for(const auto &part:parts){int stars=0;for(const auto &token:part)if(token.character=='*')++stars;
        if(stars>1){*error="Only one asterisk per mask section is allowed";return {};}}
    const int dot=int(name.lastIndexOf('.'));
    QString result=maskSection(dot>0?name.left(dot):name,parts[0]);
    const QString extension=maskSection(dot>0?name.mid(dot+1):QString{},parts[1]);
    if(!extension.isEmpty())result+='.'+extension;
    if(letterCase==CopyCase::Lower)result=result.toLower();else if(letterCase==CopyCase::Upper)result=result.toUpper();
    if(result.isEmpty() || result=="." || result==".." || result.contains('/') || result.contains(QChar(0))){*error="The mask produces an invalid filename";return {};}
    return result;
}

CopyPlan planCopy(const QVector<FileEntry> &files,const QString &sourceDirectory,const QString &destination,CopyPaths paths,const QString &mask,CopyCase letterCase)
{
    CopyPlan result;QSet<QString> targets,sources;
    if(destination.isEmpty() || destination.contains(QChar(0)) || !QDir::isAbsolutePath(destination)){
        result.error="Enter an absolute destination directory";return result;
    }
    for(const auto &file:files)sources.insert(file.path);
    for(const auto &file:files){
        const QString name=copyName(file.name,mask,letterCase,&result.error);if(!result.error.isEmpty())return result;
        QString relative;
        if(paths==CopyPaths::Full)relative=QFileInfo(file.path).absolutePath().mid(1);
        else if(paths!=CopyPaths::Flat){
            if(!isWithin(file.path,sourceDirectory)){result.error="A tagged file is outside the source branch";return result;}
            relative=QDir(sourceDirectory).relativeFilePath(QFileInfo(file.path).absolutePath());
            if(relative==".")relative.clear();
            if(paths==CopyPaths::Current)relative=QFileInfo(sourceDirectory).fileName()+(relative.isEmpty()?QString{}:'/'+relative);
        }
        const QString target=QDir::cleanPath(QDir(destination).filePath((relative.isEmpty()?QString{}:relative+'/')+name));
        if(targets.contains(target)){result.error="The mask maps multiple files to the same destination: "+target;return result;}
        if(sources.contains(target)){result.error="A destination is also a source file: "+target;return result;}
        targets.insert(target);result.entries.append({file.path,target});
    }
    return result;
}

CopyResult copyFile(const QString &sourceRoot,const CopyEntry &entry,CopyReplace replace,const Cancellation &cancel)
{
    auto result=copyOne(sourceRoot,entry,replace,cancel);
    QSet<QString> affected;
    for(const auto &path:result.created)affected.insert(QFileInfo(path).absolutePath());
    if(result.copied)affected.insert(QFileInfo(result.target).absolutePath());
    const auto refresh=std::make_shared<std::atomic_bool>(false);
    for(const auto &path:affected)result.scan.directories+=scanDirectories(path,false,refresh).directories;
    return result;
}
CopyResult moveFile(const QString &sourceRoot,const CopyEntry &entry,CopyReplace replace,const Cancellation &cancel)
{
    auto result=copyOne(sourceRoot,entry,replace,cancel,true);
    QSet<QString> affected;
    for(const auto &path:result.created)affected.insert(QFileInfo(path).absolutePath());
    if(result.copied || result.moved)affected.insert(QFileInfo(result.target).absolutePath());
    affected.insert(QFileInfo(entry.source).absolutePath());
    const auto refresh=std::make_shared<std::atomic_bool>(false);
    for(const auto &path:affected)result.scan.directories+=scanDirectories(path,false,refresh).directories;
    return result;
}
}
