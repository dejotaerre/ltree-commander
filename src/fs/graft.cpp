#include "platform/platform.h"
#include "fs/graft.h"
#include "fs/mounts.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <set>

namespace ltree {
namespace {
struct Descriptor {
    int fd;
    explicit Descriptor(int value) : fd(value) {}
    ~Descriptor() { if (fd >= 0) ::close(fd); }
};
int openDirectory(const String &path)
{
    int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (const auto &part : path.split('/', TextOptions::SkipEmptyParts)) {
        if (fd < 0) break;
        const int next = ::openat(fd, File::encodeName(part).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int code = errno;::close(fd);fd = next;errno = code;
    }
    return fd;
}
bool matches(const struct stat &value, const FileMetadata &expected)
{
    return S_ISDIR(value.st_mode) && uint64(value.st_dev) == expected.device && uint64(value.st_ino) == expected.inode &&
        value.st_ctim.tv_sec == expected.changedSeconds && value.st_ctim.tv_nsec == expected.changedNanoseconds;
}
String systemError() { return String::fromLocal8Bit(std::strerror(errno)); }

int64 branchBytes(int fd, const String &path, const Cancellation &cancel, GraftProgress &details,
                  std::set<std::pair<dev_t,ino_t>> &seen)
{
    DIR *directory=::fdopendir(fd);
    if(!directory){::close(fd);return -1;}
    int64 total=0;
    details.setPath(path);
    while(!cancel->load()){
        errno=0;const auto *entry=::readdir(directory);
        if(!entry){if(errno)total=-1;break;}
        if(!std::strcmp(entry->d_name,".") || !std::strcmp(entry->d_name,".."))continue;
        struct stat value{};
        if(::fstatat(::dirfd(directory),entry->d_name,&value,AT_SYMLINK_NOFOLLOW)!=0){total=-1;break;}
        int64 size=0;
        if(S_ISDIR(value.st_mode)){
            const int child=::openat(::dirfd(directory),entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            if(child<0){total=-1;break;}
            size=branchBytes(child,path+'/'+File::decodeName(entry->d_name),cancel,details,seen);
        }else if(S_ISREG(value.st_mode) && (value.st_nlink<=1 || seen.insert({value.st_dev,value.st_ino}).second))size=value.st_size;
        if(size<0 || size>std::numeric_limits<int64>::max()-total){total=-1;break;}
        total+=size;
    }
    ::closedir(directory);
    return total;
}

void processProgress(int64 pid, const String &source, const String &target, int64 total, GraftProgress &details)
{
    if(pid<=0)return;
    const String proc=String("/proc/%1/").arg(pid);
    File io(proc+"io");
    if(io.open(IO::ReadOnly))for(const auto &line:String::fromLatin1(io.readAll()).split('\n'))if(line.startsWith("wchar:")){
        bool ok=false;const auto bytes=line.mid(6).trimmed().toLongLong(&ok);
        if(ok)details.transfer.report(bytes,total,TransferPhase::Copying);
        break;
    }
    // Consultar descriptores del proceso evita recorrer el destino de red en cada actualización.
    const auto fdPath=File::encodeName(proc+"fd");DIR *fds=::opendir(fdPath.constData());
    if(!fds)return;
    while(const auto *entry=::readdir(fds)){
        if(entry->d_name[0]=='.')continue;
        const String descriptor=File::decodeName(entry->d_name);
        File info(proc+"fdinfo/"+descriptor);bool writable=false;
        if(info.open(IO::ReadOnly))for(const auto &line:String::fromLatin1(info.readAll()).split('\n'))if(line.startsWith("flags:")){
            writable=(line.mid(6).trimmed().toLongLong(nullptr,8)&O_ACCMODE)!=O_RDONLY;break;
        }
        if(!writable)continue;
        char buffer[4096];const auto link=File::encodeName(proc+"fd/"+descriptor);
        const auto length=::readlink(link.constData(),buffer,sizeof(buffer));
        if(length<=0 || length==ssize_t(sizeof(buffer)))continue;
        const String path=File::decodeName(Bytes(buffer,int(length)));
        if(path.startsWith(target+'/')){details.setPath(source+path.mid(target.size()));break;}
    }
    ::closedir(fds);
}
}
GraftResult graftBranch(const FileMetadata &source, const FileMetadata &destination,
                        const Cancellation &cancel, const std::function<void(const String &)> &progress, GraftProgress *details)
{
    GraftResult result;result.source = source.path;
    if(details){details->setPath(source.path);details->transfer.report(0,-1,TransferPhase::Preparing);}
    if (cancel->load()) { result.cancelled = true;return result; }
    for (const auto &item : {source, destination}) {
        if (!item.error.isEmpty() || !item.directory || item.symlink || !DirectoryPath::isAbsolutePath(item.path) ||
            item.path.contains(Char::Null) || DirectoryPath::cleanPath(item.path) != item.path) {
            result.error = "Graft requires real source and destination directories";return result;
        }
    }
    if (source.path == "/" || isWithin(destination.path, source.path)) {
        result.error = "Cannot graft the root or move a branch into itself";return result;
    }
    for (const auto &mount : mountedLocations(nullptr, true)) if (isWithin(mount.path, source.path)) {
        result.error = "Branch contains a mount point; Graft was not started";return result;
    }
    const auto parentPath = FileInfo(source.path).absolutePath();
    const auto name = File::encodeName(FileInfo(source.path).fileName());
    result.target = DirectoryPath(destination.path).filePath(FileInfo(source.path).fileName());
    if (result.target == source.path) { result.error = "Branch is already in that directory";return result; }
    Descriptor parent(openDirectory(parentPath)), target(openDirectory(destination.path));
    struct stat original{}, targetDirectory{};
    if (parent.fd < 0 || target.fd < 0) { result.error = "Cannot open branch parents without following links: " + systemError();return result; }
    if (::fstatat(parent.fd, name.constData(), &original, AT_SYMLINK_NOFOLLOW) != 0 || !matches(original, source) ||
        ::fstat(target.fd, &targetDirectory) != 0 || !matches(targetDirectory, destination)) {
        result.error = "Source or destination changed; reopen Graft";return result;
    }
    struct stat existing{};
    if (::fstatat(target.fd, name.constData(), &existing, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) {
        result.error = "Destination branch already exists or cannot be checked; no replacement made";return result;
    }
    if (cancel->load()) { result.cancelled = true;return result; }
    // Apartar el origen permite verificar el objeto trasladado y restaurarlo ante fallos.
    const auto held = File::encodeName(".ltree-graft-" + Uuid::createUuid().toString(Uuid::WithoutBraces));
    if (::syscall(SYS_renameat2, parent.fd, name.constData(), parent.fd, held.constData(), RENAME_NOREPLACE) != 0) {
        result.error = "Cannot prepare branch: " + systemError();return result;
    }
    struct stat staged{};
    const bool same = ::fstatat(parent.fd, held.constData(), &staged, AT_SYMLINK_NOFOLLOW) == 0 &&
        staged.st_dev == original.st_dev && staged.st_ino == original.st_ino && S_ISDIR(staged.st_mode);
    if (!same) result.error = "Source changed while preparing Graft";
    else if (cancel->load()) result.cancelled = true;
    else {
        if (progress) progress(source.path);
        if(details)details->transfer.report(0,-1,TransferPhase::Moving);
        if (!cancel->load() && ::syscall(SYS_renameat2, parent.fd, held.constData(), target.fd, name.constData(), RENAME_NOREPLACE) == 0)
            result.moved = true;
        else if (cancel->load()) result.cancelled = true;
        else if (errno == EXDEV) {
            result.crossDevice = true;
            int64 total=-1;
            if(details){
                details->transfer.report(0,-1,TransferPhase::Preparing);
                const int directory=::openat(parent.fd,held.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
                if(directory>=0){std::set<std::pair<dev_t,ino_t>> seen;total=branchBytes(directory,source.path,cancel,*details,seen);}
                details->setPath(source.path);details->transfer.report(0,total,TransferPhase::Copying);
            }
            const auto executable = Paths::findExecutable("mv");
            if(cancel->load())result.cancelled=true;
            else if (executable.isEmpty()) result.error = "GNU mv is required for Graft across filesystems";
            else {
                // Los descriptores heredados fijan los padres; no se ejecuta un shell.
                Process process;auto env = ProcessEnvironment::systemEnvironment();env.insert("LC_ALL", "C");
                process.setProcessEnvironment(env);
                const int sourceFd = parent.fd, targetFd = target.fd;
                process.setChildProcessModifier([sourceFd, targetFd] {
                    ::fcntl(sourceFd, F_SETFD, 0);::fcntl(targetFd, F_SETFD, 0);
                });
                const auto from = String("/proc/self/fd/%1/%2").arg(parent.fd).arg(File::decodeName(held));
                const auto to = String("/proc/self/fd/%1/%2").arg(target.fd).arg(File::decodeName(name));
                process.start(executable, {"--no-clobber", "--no-target-directory", "--", from, to});
                if (!process.waitForStarted(5000)) result.error = "Cannot start GNU mv: " + process.errorString();
                else {
                    process.closeWriteChannel();Bytes diagnostics;
                    // Una operación entre volúmenes termina antes de devolver el control al modelo.
                    while (process.state() != Process::NotRunning) {
                        process.waitForFinished(50);
                        if(details)processProgress(process.processId(),source.path,result.target,total,*details);
                        diagnostics += process.readAllStandardError();diagnostics = diagnostics.left(65536);process.readAllStandardOutput();
                        if (cancel->load()) {
                            result.cancelled = true;process.terminate();
                            if (!process.waitForFinished(1000)) { process.kill();process.waitForFinished(-1); }
                            break;
                        }
                    }
                    diagnostics += process.readAllStandardError();
                    const bool retained = ::fstatat(parent.fd, held.constData(), &staged, AT_SYMLINK_NOFOLLOW) == 0;
                    const bool arrived = ::fstatat(target.fd, name.constData(), &existing, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(existing.st_mode);
                    result.moved = !retained && arrived;
                    if (!result.moved) result.error = result.cancelled ? "Graft stopped; any destination copy is retained" :
                        "Graft failed: " + (diagnostics.isEmpty() ? String("Transfer did not complete; source and any destination copy are retained") : String::fromLocal8Bit(diagnostics).trimmed());
                    else if (!diagnostics.isEmpty()) result.error = String::fromLocal8Bit(diagnostics).trimmed();
                    result.error.replace(from, source.path).replace(to, result.target);
                }
            }
        } else result.error = errno == EEXIST ? "Destination branch already exists; no replacement made" : "Cannot graft branch: " + systemError();
    }
    if (!result.moved) {
        if (::syscall(SYS_renameat2, parent.fd, held.constData(), parent.fd, name.constData(), RENAME_NOREPLACE) != 0) {
            result.recoverySource = DirectoryPath(parentPath).filePath(File::decodeName(held));
            result.error += "; source retained at " + result.recoverySource;
        }
    } else {
        if(details)details->transfer.phase.store(TransferPhase::Syncing);
        if (::fsync(parent.fd) != 0 || ::fsync(target.fd) != 0) result.error += "; moved, but directory synchronization failed";
    }
    if (cancel->load()) result.cancelled = true;
    const auto refreshCancel = std::make_shared<std::atomic_bool>(false);
    if(details)details->transfer.phase.store(TransferPhase::Refreshing);
    result.scan = scanDirectories(parentPath, false, refreshCancel);
    result.scan.directories += scanDirectories(destination.path, false, refreshCancel).directories;
    // Un fallo durante la retirada entre volúmenes puede dejar un origen parcial.
    if (result.crossDevice && !result.moved && result.recoverySource.isEmpty())
        result.scan.directories += scanDirectories(source.path, true, refreshCancel).directories;
    if(details && result.moved)details->transfer.phase.store(TransferPhase::Completed);
    return result;
}
}
