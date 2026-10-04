#include "fs/stamp.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QMap>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ltree {
namespace {
struct Step { qint64 count; QChar unit; };
struct Expression { QDate date; QTime time; QVector<Step> steps; bool even=false; QString error; };
Expression parse(const QString &input,StampMode mode)
{
    Expression result;const QString text=input.trimmed();
    if(mode==StampMode::Set){
        static const QRegularExpression pattern("^(?:(\\d{4}-\\d{2}-\\d{2})(?: +(\\d{2}:\\d{2}:\\d{2}))?|(\\d{2}:\\d{2}:\\d{2}))$");
        const auto match=pattern.match(text);
        if(match.hasMatch()){
            if(!match.captured(1).isEmpty())result.date=QDate::fromString(match.captured(1),"yyyy-MM-dd");
            const auto time=match.captured(2).isEmpty()?match.captured(3):match.captured(2);
            if(!time.isEmpty())result.time=QTime::fromString(time,"HH:mm:ss");
            if((match.captured(1).isEmpty() || result.date.isValid()) && (time.isEmpty() || result.time.isValid()))return result;
        }
        result.error="Enter yyyy-MM-dd HH:mm:ss, a date only or a time only";return result;
    }
    static const QRegularExpression pattern("([+-][0-9]+)([dmyhns])");
    int position=0;const QString body=text.endsWith('e')?text.chopped(1):text;result.even=text.endsWith('e');
    while(position<body.size()){
        const auto match=pattern.match(body,position);
        bool valid=false;const auto value=match.captured(1).toLongLong(&valid);
        if(!match.hasMatch() || match.capturedStart()!=position || !valid || value>100000000 || value<-100000000){
            result.error="Use signed adjustments: +1y-5n, -3d+10s, or e (round up to an even second)";return result;
        }
        result.steps.append({value,match.captured(2)[0]});position=int(match.capturedEnd());
    }
    if(result.steps.isEmpty() && !result.even)result.error="Enter an adjustment, e.g. +1d or -5n";
    return result;
}
StampTime transformed(StampTime original,const Expression &expression,StampMode mode,QString &error)
{
    auto value=QDateTime::fromSecsSinceEpoch(original.seconds);qint64 fraction=original.nanoseconds;
    if(mode==StampMode::Set){
        const auto date=expression.date.isValid()?expression.date:value.date();
        const auto time=expression.time.isValid()?expression.time:value.time();
        value=QDateTime(date,time);
        if(expression.time.isValid())fraction=0;
        // Una hora inexistente por cambio de zona no debe ajustarse silenciosamente.
        if(value.date()!=date || value.time()!=time)value={};
    }else for(const auto &step:expression.steps){
        if(step.unit=='y')value=value.addYears(int(step.count));
        else if(step.unit=='m')value=value.addMonths(int(step.count));
        else if(step.unit=='d')value=value.addDays(step.count);
        else value=value.addSecs(step.count*(step.unit=='h'?3600:step.unit=='n'?60:1));
    }
    if(expression.even && value.isValid()){
        const auto seconds=value.toSecsSinceEpoch();
        const auto remainder=(seconds%2+2)%2;
        value=value.addSecs(remainder?1:fraction?2:0);fraction=0;
    }
    if(!value.isValid() || value.date().year()<1 || value.date().year()>9999){error="Timestamp is outside the supported calendar range";return {};}
    return {value.toSecsSinceEpoch(),fraction};
}
struct Descriptor { int fd=-1;~Descriptor(){if(fd>=0)::close(fd);} };
QString change(const StampEntry &entry,const Cancellation &cancel,bool &changed,timespec &version)
{
    const auto &expected=entry.expected;
    if(!expected.error.isEmpty() || !expected.regular || expected.symlink || !QDir::isAbsolutePath(expected.path) ||
       expected.path.contains(QChar(0)) || entry.written.nanoseconds<0 || entry.written.nanoseconds>=1000000000 ||
       entry.accessed.nanoseconds<0 || entry.accessed.nanoseconds>=1000000000)
        return "Timestamps require a regular file; symbolic links are not supported";
    Descriptor file;file.fd=::open("/",O_PATH|O_DIRECTORY|O_CLOEXEC);
    const auto parts=QDir::cleanPath(expected.path).split('/',Qt::SkipEmptyParts);
    for(int i=0;i<parts.size();++i){
        const auto name=QFile::encodeName(parts[i]);
        const int next=::openat(file.fd,name.constData(),O_PATH|O_NOFOLLOW|O_CLOEXEC|(i+1<parts.size()?O_DIRECTORY:0));
        if(next<0)return QString::fromLocal8Bit(std::strerror(errno));
        ::close(file.fd);file.fd=next;
    }
    struct stat current{};
    if(::fstat(file.fd,&current)<0)return QString::fromLocal8Bit(std::strerror(errno));
    if(!S_ISREG(current.st_mode) || quint64(current.st_dev)!=expected.device || quint64(current.st_ino)!=expected.inode ||
       current.st_ctim.tv_sec!=expected.changedSeconds || current.st_ctim.tv_nsec!=expected.changedNanoseconds)
        return "File changed since the prompt opened; reopen New date and try again";
    const bool written=entry.field!=StampField::Accessed,accessed=entry.field!=StampField::Written;
    if((!written || (current.st_mtim.tv_sec==entry.written.seconds && current.st_mtim.tv_nsec==entry.written.nanoseconds)) &&
       (!accessed || (current.st_atim.tv_sec==entry.accessed.seconds && current.st_atim.tv_nsec==entry.accessed.nanoseconds)))return {};
    if(cancel && cancel->load())return {};
    const timespec times[]{{time_t(entry.accessed.seconds),accessed?long(entry.accessed.nanoseconds):UTIME_OMIT},
                           {time_t(entry.written.seconds),written?long(entry.written.nanoseconds):UTIME_OMIT}};
    // El descriptor fija el inodo y evita seguir enlaces o alterar un reemplazo concurrente de la ruta.
    if(::utimensat(file.fd,"",times,AT_EMPTY_PATH)<0)return QString::fromLocal8Bit(std::strerror(errno));
    changed=true;
    struct stat stored{};
    if(::fstat(file.fd,&stored)<0)return "Timestamp changed, but cannot verify it: "+QString::fromLocal8Bit(std::strerror(errno));
    version=stored.st_ctim;
    if((written && stored.st_mtim.tv_sec!=entry.written.seconds) || (accessed && stored.st_atim.tv_sec!=entry.accessed.seconds))
        return "Stored timestamp differs from request; check the filesystem date range or concurrent changes";
    return {};
}
}
StampPlan planStamp(const QVector<FileMetadata> &files,const QString &input,StampField field,StampMode mode)
{
    StampPlan result;const auto expression=parse(input,mode);result.error=expression.error;
    if(!result.error.isEmpty())return result;
    StampTime previousWritten,previousAccessed;
    for(int i=0;i<files.size();++i){
        const auto &file=files[i];
        StampTime written{file.modified.toSecsSinceEpoch(),file.modifiedNanoseconds};
        StampTime accessed{file.accessed.toSecsSinceEpoch(),file.accessedNanoseconds};
        if(mode==StampMode::Increment && i){written=previousWritten;accessed=previousAccessed;}
        if(field!=StampField::Accessed)written=transformed(written,expression,mode,result.error);
        if(field!=StampField::Written)accessed=transformed(accessed,expression,mode,result.error);
        if(!result.error.isEmpty()){result.entries.clear();return result;}
        previousWritten=written;previousAccessed=accessed;
        result.entries.append({file,written,accessed,field});
    }
    return result;
}
StampResult stampFiles(const StampPlan &plan,const Cancellation &cancel)
{
    StampResult result;if(!plan.error.isEmpty()){result.error=plan.error;return result;}
    QSet<QString> affected;
    QMap<QPair<quint64,quint64>,QPair<qint64,qint64>> versions;
    for(auto entry:plan.entries){
        if(cancel && cancel->load()){result.cancelled=true;break;}
        const auto identity=qMakePair(entry.expected.device,entry.expected.inode);
        const auto own=versions.constFind(identity);
        // Los enlaces duros del mismo lote comparten el cambio previo, pero se comprueba su versión actual.
        if(own!=versions.cend()){entry.expected.changedSeconds=own->first;entry.expected.changedNanoseconds=own->second;}
        bool changed=false;timespec version{0,-1};const auto error=change(entry,cancel,changed,version);
        if(changed && version.tv_nsec>=0)versions.insert(identity,{version.tv_sec,version.tv_nsec});
        if(changed)++result.changed;
        if(!error.isEmpty()){++result.failed;if(result.error.isEmpty())result.error=entry.expected.path+": "+error;}
        else if(!changed)++result.unchanged;
        affected.insert(QFileInfo(entry.expected.path).absolutePath());
        if(cancel && cancel->load()){result.cancelled=true;break;}
    }
    const auto refresh=std::make_shared<std::atomic_bool>(false);
    for(const auto &directory:affected)result.scan.directories+=scanDirectories(directory,false,refresh).directories;
    return result;
}
}
