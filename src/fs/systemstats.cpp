#include "platform/platform.h"
#include "fs/systemstats.h"
#include <limits>
#include <algorithm>
#include <cmath>
#include <pwd.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace ltree {
SystemStatistics parseProcStatistics(const Bytes &memory,const Bytes &cpu)
{
    SystemStatistics result;
    for(const auto &line:memory.split('\n')){
        const auto fields=line.simplified().split(' ');if(fields.size()!=3 || fields[2]!="kB")continue;
        bool ok=false;const auto value=fields[1].toLongLong(&ok);
        if(!ok || value<0 || value>std::numeric_limits<int64>::max()/1024)continue;
        if(fields[0]=="MemTotal:")result.ramTotal=value*1024;
        else if(fields[0]=="MemAvailable:")result.ramAvailable=value*1024;
        else if(fields[0]=="Committed_AS:")result.committed=value*1024;
        else if(fields[0]=="CommitLimit:")result.commitLimit=value*1024;
    }
    for(const auto &line:cpu.split('\n')){
        const int split=int(line.indexOf(':'));if(split<0)continue;
        const auto key=line.left(split).trimmed(),value=line.mid(split+1).trimmed();
        if(result.cpuType.isEmpty() && (key=="model name" || key=="Hardware" || key=="Processor"))result.cpuType=String::fromUtf8(value);
        if(result.cpuMHz<0 && key=="cpu MHz"){
            bool ok=false;const auto speed=value.toDouble(&ok);if(ok && std::isfinite(speed) && speed>0)result.cpuMHz=speed;
        }
    }
    return result;
}
SystemStatistics systemStatistics(const String &path)
{
    const auto read=[](const String &name){File file(name);return file.open(IO::ReadOnly)?file.readAll():Bytes{};};
    auto result=parseProcStatistics(read("/proc/meminfo"),read("/proc/cpuinfo"));
    result.path=path;result.measured=DateTime::currentDateTime();
    const StorageInfo storage(path);
    result.mount=storage.rootPath();result.source=String::fromLocal8Bit(storage.device());result.filesystem=String::fromLatin1(storage.fileSystemType());
    if(storage.isValid() && storage.isReady()){
        result.capacity=storage.bytesTotal();result.available=storage.bytesAvailable();result.free=storage.bytesFree();
        struct statvfs data{};
        if(::statvfs(File::encodeName(path).constData(),&data)==0){
            const auto maximum=uint64(std::numeric_limits<int64>::max());
            const auto block=data.f_frsize?data.f_frsize:data.f_bsize;
            if(block<=maximum)result.blockSize=int64(block);
            if(data.f_blocks<=maximum)result.totalBlocks=int64(data.f_blocks);
        }
    }else result.error="Filesystem statistics are unavailable for this location";
    result.host=SystemInfo::machineHostName();
    passwd account{};passwd *found=nullptr;Bytes buffer(16384,0);
    if(::getpwuid_r(::getuid(),&account,buffer.data(),size_t(buffer.size()),&found)==0 && found)result.user=String::fromLocal8Bit(found->pw_name);
    if(result.user.isEmpty())result.user=String::number(::getuid());
    result.sessionType=environment("XDG_SESSION_TYPE");result.desktop=environment("XDG_CURRENT_DESKTOP");
    return result;
}
LoggedStatistics loggedStatistics(const Session &session,const Vector<MountPoint> &mounts,const String &mount)
{
    const auto included=[&](const String &path){
        if(mount.isEmpty())return true;
        String owner;
        for(const auto &location:mounts)if(location.path.size()>owner.size() && isWithin(path,location.path))owner=location.path;
        return owner==mount;
    };
    const auto add=[](uint64 &total,uint64 value){total=value>std::numeric_limits<uint64>::max()-total?std::numeric_limits<uint64>::max():total+value;};
    LoggedStatistics result;
    for(const auto &directory:session.directories()){
        if(!directory.loaded || !included(directory.path))continue;
        if(session.view==View::Directory && directory.path!=session.directory)continue;
        if(session.view==View::Branch && !isWithin(directory.path,session.directory))continue;
        ++result.directories;
        for(const auto &file:directory.files){
            const auto size=uint64(std::max(int64(0),file.size));++result.totalFiles;add(result.totalBytes,size);
            if(!session.filespec.matches(file))continue;
            ++result.matchingFiles;add(result.matchingBytes,size);
            if(session.tags.contains(file.path)){++result.taggedFiles;add(result.taggedBytes,size);}
        }
    }
    for(const auto &file:session.files())if(included(FileInfo(file.path).absolutePath())){++result.displayedFiles;add(result.displayedBytes,uint64(std::max(int64(0),file.size)));}
    return result;
}
}
