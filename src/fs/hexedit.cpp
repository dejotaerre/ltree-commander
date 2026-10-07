#include "platform/platform.h"
#include "fs/hexedit.h"
#include "fs/safeio.h"
namespace ltree {
HexSnapshot hexSnapshot(const String &path)
{
    const FileInfo info(path);safeio::Fd parent(safeio::directory(info.absolutePath()));
    struct stat st{};
    if(parent.value<0 || ::fstatat(parent.value,File::encodeName(info.fileName()).constData(),&st,AT_SYMLINK_NOFOLLOW)<0 || !S_ISREG(st.st_mode)) return {};
    return {uint64(st.st_dev),uint64(st.st_ino),st.st_ctim.tv_sec,st.st_ctim.tv_nsec,true};
}
String saveHexPage(const String &path,const HexSnapshot &expected,const Bytes &original,Index offset,const Bytes &page)
{
    if(!expected.valid || offset<0 || offset+page.size()>original.size()) return "Invalid edit range or file identity";
    const FileInfo info(path);safeio::Fd parent(safeio::directory(info.absolutePath()));
    if(parent.value<0) return safeio::error();
    safeio::Fd fd(::openat(parent.value,File::encodeName(info.fileName()).constData(),O_RDWR|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
    if(fd.value<0) return safeio::error();
    struct stat st{};
    if(::fstat(fd.value,&st)<0) return safeio::error();
    if(!S_ISREG(st.st_mode) || uint64(st.st_dev)!=expected.device || uint64(st.st_ino)!=expected.inode || st.st_size!=original.size() || st.st_ctim.tv_sec!=expected.changedSeconds || st.st_ctim.tv_nsec!=expected.changedNanos)
        return "The file changed outside the viewer. Reload before editing";
    Bytes current(original.size(),TextOptions::Uninitialized);
    Index done=0;
    while(done<current.size()) { const auto n=::pread(fd.value,current.data()+done,current.size()-done,done);if(n<0 && errno==EINTR)continue;if(n<=0)return "Cannot verify the original file";done+=n; }
    if(current!=original) return "The file changed outside the viewer. Reload before editing";
    done=0;
    while(done<page.size()) {
        const auto n=::pwrite(fd.value,page.constData()+done,page.size()-done,offset+done);
        if(n<0 && errno==EINTR)continue;
        if(n<=0) break;
        done+=n;
    }
    if(done==page.size() && ::fsync(fd.value)==0) return {};
    const String message=safeio::error();
    // Se intenta restaurar la página original si falla una escritura parcial.
    Index restored=0;
    while(restored<done) { const auto n=::pwrite(fd.value,original.constData()+offset+restored,done-restored,offset+restored);if(n<0 && errno==EINTR)continue;if(n<=0)break;restored+=n; }
    ::fsync(fd.value);
    return "Save failed: "+message+(restored<done?"; rollback incomplete":"; original bytes restored");
}
}
