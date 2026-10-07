#include "platform/platform.h"
#include "fs/directorycompare.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ltree {
namespace {
struct Descriptor { int fd=-1;~Descriptor(){if(fd>=0)::close(fd);} };
int openRegular(const String &path,String &error)
{
    Descriptor parent;parent.fd=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    const auto parts=path.split('/',TextOptions::SkipEmptyParts);
    if(!DirectoryPath::isAbsolutePath(path)||DirectoryPath::cleanPath(path)!=path||path.contains(Char::Null)||parts.isEmpty()){
        error="Invalid file path";return -1;
    }
    for(int i=0;i<parts.size();++i){
        const auto name=File::encodeName(parts[i]);
        const int next=parent.fd<0 ? -1 : ::openat(parent.fd,name.constData(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK|(i+1<parts.size()?O_DIRECTORY:0));
        if(next<0){error=String::fromLocal8Bit(std::strerror(errno));return -1;}
        ::close(parent.fd);parent.fd=next;
    }
    struct stat value{};
    if(::fstat(parent.fd,&value)<0||!S_ISREG(value.st_mode)){error="Binary comparison requires regular files";return -1;}
    const int fd=parent.fd;parent.fd=-1;return fd;
}
bool equalContents(const String &first,const String &second,const Cancellation &cancel,String &error)
{
    Descriptor a,b;a.fd=openRegular(first,error);if(a.fd<0)return false;
    b.fd=openRegular(second,error);if(b.fd<0)return false;
    struct stat startA{},startB{};::fstat(a.fd,&startA);::fstat(b.fd,&startB);
    bool equal=startA.st_size==startB.st_size;
    Bytes blockA(65536,TextOptions::Uninitialized),blockB(65536,TextOptions::Uninitialized);
    const auto readBlock=[&](int fd,Bytes &block){
        int64 count=0;
        while(count<block.size()&&!cancel->load()){
            const auto n=::read(fd,block.data()+count,size_t(block.size()-count));
            if(n<0){if(errno==EINTR)continue;error=String::fromLocal8Bit(std::strerror(errno));return int64(-1);}
            if(n==0)break;
            count+=n;
        }
        return count;
    };
    while(equal&&!cancel->load()){
        const auto n=readBlock(a.fd,blockA),m=readBlock(b.fd,blockB);
        if(n<0||m<0)return false;
        if(n!=m || std::memcmp(blockA.constData(),blockB.constData(),size_t(n))!=0){equal=false;break;}
        if(n==0)break;
    }
    struct stat endA{},endB{};
    const auto stable=[](const struct stat &a,const struct stat &b){return a.st_size==b.st_size &&
        a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;};
    if(::fstat(a.fd,&endA)<0||::fstat(b.fd,&endB)<0||!stable(startA,endA)||!stable(startB,endB))error="File changed during comparison; tags retained";
    return equal;
}
}
DirectoryCompareResult compareDirectories(const Vector<FileEntry> &source,const String &sourceRoot,
    const Vector<FileEntry> &target,const String &targetRoot,const DirectoryCompareOptions &options,const Cancellation &cancel)
{
    DirectoryCompareResult result;Map<String,FileEntry> lookup;Set<String> duplicates;
    const auto key=[&](const String &path,const String &root){const auto relative=DirectoryPath(root).relativeFilePath(path);return options.caseSensitive?relative:relative.toCaseFolded();};
    for(const auto &file:target){const auto name=key(file.path,targetRoot);if(lookup.contains(name))duplicates.insert(name);lookup.insert(name,file);}
    for(const auto &file:source){
        if(cancel->load()){result.cancelled=true;break;}
        const auto name=key(file.path,sourceRoot);
        if(duplicates.contains(name)){result.errors.append("Ambiguous names in target: "+name);continue;}
        const auto other=lookup.constFind(name);
        if(other==lookup.cend()){if(options.unique)result.matches.insert(file.path);continue;}
        const bool sizeEqual=file.size==other->size,dateEqual=file.modified==other->modified;
        const bool metadata=(options.identical==1&&sizeEqual)||(options.identical==2&&dateEqual)||(options.identical==3&&sizeEqual&&dateEqual)||
            (options.newer&&file.modified>other->modified)||(options.older&&file.modified<other->modified)||
            (options.size==1&&file.size<other->size)||(options.size==2&&file.size>other->size)||(options.size==3&&!sizeEqual);
        const bool hasMetadata=options.identical||options.newer||options.older||options.size;
        bool match=metadata;
        if(options.binary && (!hasMetadata||metadata)){
            String error;const bool equal=equalContents(file.path,other->path,cancel,error);
            if(!error.isEmpty()){result.errors.append(file.path+": "+error);continue;}
            match=options.binary==1?equal:!equal;
        }
        if(match)result.matches.insert(file.path);
    }
    if(cancel->load())result.cancelled=true;
    return result;
}
DirectoryCompareResult filterDuplicates(const Vector<FileEntry> &files,CompareFilter filter,
    bool caseSensitive,bool includeEmpty,const Cancellation &cancel)
{
    DirectoryCompareResult result;Map<String,Vector<FileEntry>> groups;
    for(const auto &file:files){
        const bool bySize=filter==CompareFilter::Size||filter==CompareFilter::Content;
        if(bySize&&!includeEmpty&&file.size==0)continue;
        groups[bySize?String::number(file.size):caseSensitive?file.name:file.name.toCaseFolded()].append(file);
    }
    for(const auto &group:groups){
        if(cancel->load()){result.cancelled=true;break;}
        if(filter==CompareFilter::Unique){if(group.size()==1)result.matches.insert(group.first().path);continue;}
        if(group.size()<2)continue;
        DateTime extreme=group.first().modified;
        for(const auto &file:group){if(filter==CompareFilter::Newest&&file.modified>extreme)extreme=file.modified;
            if(filter==CompareFilter::Oldest&&file.modified<extreme)extreme=file.modified;}
        for(int i=0;i<group.size();++i){
            if(cancel->load()){result.cancelled=true;break;}
            bool match=filter==CompareFilter::Duplicate||filter==CompareFilter::Size||
                ((filter==CompareFilter::Newest||filter==CompareFilter::Oldest)&&group[i].modified==extreme);
            if(filter==CompareFilter::IdenticalDates||filter==CompareFilter::Content)for(int j=i+1;j<group.size();++j){
                if(cancel->load()){result.cancelled=true;break;}
                String error;const bool equal=filter==CompareFilter::IdenticalDates?group[i].modified==group[j].modified:
                    equalContents(group[i].path,group[j].path,cancel,error);
                if(!error.isEmpty())result.errors.append(group[i].path+": "+error);
                else if(equal){match=true;result.matches.insert(group[j].path);}
            }
            if(match)result.matches.insert(group[i].path);
        }
    }
    if(cancel->load())result.cancelled=true;
    return result;
}
}
