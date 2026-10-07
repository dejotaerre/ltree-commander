#include "platform/platform.h"
#include "fs/stamp.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ltree {
namespace {
struct Step { int64 count; Char unit; };
struct Expression { Date date; Time time; Vector<Step> steps; bool even=false; String error; };
Expression parse(const String &input,StampMode mode)
{
    Expression result;const String text=input.trimmed();
    if(mode==StampMode::Set){
        static const Regex pattern("^(?:(\\d{4}-\\d{2}-\\d{2})(?: +(\\d{2}:\\d{2}:\\d{2}))?|(\\d{2}:\\d{2}:\\d{2}))$");
        const auto match=pattern.match(text);
        if(match.hasMatch()){
            if(!match.captured(1).isEmpty())result.date=Date::fromString(match.captured(1),"yyyy-MM-dd");
            const auto time=match.captured(2).isEmpty()?match.captured(3):match.captured(2);
            if(!time.isEmpty())result.time=Time::fromString(time,"HH:mm:ss");
            if((match.captured(1).isEmpty() || result.date.isValid()) && (time.isEmpty() || result.time.isValid()))return result;
        }
        result.error="Enter yyyy-MM-dd HH:mm:ss, a date only or a time only";return result;
    }
    static const Regex pattern("([+-][0-9]+)([dmyhns])");
    int position=0;const String body=text.endsWith('e')?text.chopped(1):text;result.even=text.endsWith('e');
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
StampTime transformed(StampTime original,const Expression &expression,StampMode mode,String &error)
{
    auto value=DateTime::fromSecsSinceEpoch(original.seconds);int64 fraction=original.nanoseconds;
    if(mode==StampMode::Set){
        const auto date=expression.date.isValid()?expression.date:value.date();
        const auto time=expression.time.isValid()?expression.time:value.time();
        value=DateTime(date,time);
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
String change(const StampEntry &entry,const Cancellation &cancel,bool &changed,timespec &version)
{
    const auto &expected=entry.expected;
    if(!expected.error.isEmpty() || !expected.regular || expected.symlink || !DirectoryPath::isAbsolutePath(expected.path) ||
       expected.path.contains(Char(0)) || entry.written.nanoseconds<0 || entry.written.nanoseconds>=1000000000 ||
       entry.accessed.nanoseconds<0 || entry.accessed.nanoseconds>=1000000000)
        return "Timestamps require a regular file; symbolic links are not supported";
    Descriptor file;file.fd=::open("/",O_PATH|O_DIRECTORY|O_CLOEXEC);
    const auto parts=DirectoryPath::cleanPath(expected.path).split('/',TextOptions::SkipEmptyParts);
    for(int i=0;i<parts.size();++i){
        const auto name=File::encodeName(parts[i]);
        const int next=::openat(file.fd,name.constData(),O_PATH|O_NOFOLLOW|O_CLOEXEC|(i+1<parts.size()?O_DIRECTORY:0));
        if(next<0)return String::fromLocal8Bit(std::strerror(errno));
        ::close(file.fd);file.fd=next;
    }
    struct stat current{};
    if(::fstat(file.fd,&current)<0)return String::fromLocal8Bit(std::strerror(errno));
    if(!S_ISREG(current.st_mode) || uint64(current.st_dev)!=expected.device || uint64(current.st_ino)!=expected.inode ||
       current.st_ctim.tv_sec!=expected.changedSeconds || current.st_ctim.tv_nsec!=expected.changedNanoseconds)
        return "File changed since the prompt opened; reopen New date and try again";
    const bool written=entry.field!=StampField::Accessed,accessed=entry.field!=StampField::Written;
    if((!written || (current.st_mtim.tv_sec==entry.written.seconds && current.st_mtim.tv_nsec==entry.written.nanoseconds)) &&
       (!accessed || (current.st_atim.tv_sec==entry.accessed.seconds && current.st_atim.tv_nsec==entry.accessed.nanoseconds)))return {};
    if(cancel && cancel->load())return {};
    const timespec times[]{{time_t(entry.accessed.seconds),accessed?long(entry.accessed.nanoseconds):UTIME_OMIT},
                           {time_t(entry.written.seconds),written?long(entry.written.nanoseconds):UTIME_OMIT}};
    // El descriptor fija el inodo y evita seguir enlaces o alterar un reemplazo concurrente de la ruta.
    if(::utimensat(file.fd,"",times,AT_EMPTY_PATH)<0)return String::fromLocal8Bit(std::strerror(errno));
    changed=true;
    struct stat stored{};
    if(::fstat(file.fd,&stored)<0)return "Timestamp changed, but cannot verify it: "+String::fromLocal8Bit(std::strerror(errno));
    version=stored.st_ctim;
    if((written && stored.st_mtim.tv_sec!=entry.written.seconds) || (accessed && stored.st_atim.tv_sec!=entry.accessed.seconds))
        return "Stored timestamp differs from request; check the filesystem date range or concurrent changes";
    return {};
}
}
StampPlan planStamp(const Vector<FileMetadata> &files,const String &input,StampField field,StampMode mode)
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
    Set<String> affected;
    Map<Pair<uint64,uint64>,Pair<int64,int64>> versions;
    for(auto entry:plan.entries){
        if(cancel && cancel->load()){result.cancelled=true;break;}
        const auto identity=makePair(entry.expected.device,entry.expected.inode);
        const auto own=versions.constFind(identity);
        // Los enlaces duros del mismo lote comparten el cambio previo, pero se comprueba su versión actual.
        if(own!=versions.cend()){entry.expected.changedSeconds=own->first;entry.expected.changedNanoseconds=own->second;}
        bool changed=false;timespec version{0,-1};const auto error=change(entry,cancel,changed,version);
        if(changed && version.tv_nsec>=0)versions.insert(identity,{version.tv_sec,version.tv_nsec});
        if(changed)++result.changed;
        if(!error.isEmpty()){++result.failed;if(result.error.isEmpty())result.error=entry.expected.path+": "+error;}
        else if(!changed)++result.unchanged;
        affected.insert(FileInfo(entry.expected.path).absolutePath());
        if(cancel && cancel->load()){result.cancelled=true;break;}
    }
    const auto refresh=std::make_shared<std::atomic_bool>(false);
    for(const auto &directory:affected)result.scan.directories+=scanDirectories(directory,false,refresh).directories;
    return result;
}
}
