#include "fs/graft.h"
#include "fs/mounts.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ltree {
namespace {
struct Descriptor {
    int fd;
    explicit Descriptor(int value) : fd(value) {}
    ~Descriptor() { if (fd >= 0) ::close(fd); }
};
int openDirectory(const QString &path)
{
    int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (const auto &part : path.split('/', Qt::SkipEmptyParts)) {
        if (fd < 0) break;
        const int next = ::openat(fd, QFile::encodeName(part).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int code = errno;::close(fd);fd = next;errno = code;
    }
    return fd;
}
bool matches(const struct stat &value, const FileMetadata &expected)
{
    return S_ISDIR(value.st_mode) && quint64(value.st_dev) == expected.device && quint64(value.st_ino) == expected.inode &&
        value.st_ctim.tv_sec == expected.changedSeconds && value.st_ctim.tv_nsec == expected.changedNanoseconds;
}
QString systemError() { return QString::fromLocal8Bit(std::strerror(errno)); }
}
GraftResult graftBranch(const FileMetadata &source, const FileMetadata &destination,
                        const Cancellation &cancel, const std::function<void(const QString &)> &progress)
{
    GraftResult result;result.source = source.path;
    if (cancel->load()) { result.cancelled = true;return result; }
    for (const auto &item : {source, destination}) {
        if (!item.error.isEmpty() || !item.directory || item.symlink || !QDir::isAbsolutePath(item.path) ||
            item.path.contains(QChar::Null) || QDir::cleanPath(item.path) != item.path) {
            result.error = "Graft requires real source and destination directories";return result;
        }
    }
    if (source.path == "/" || isWithin(destination.path, source.path)) {
        result.error = "Cannot graft the root or move a branch into itself";return result;
    }
    for (const auto &mount : mountedLocations(nullptr, true)) if (isWithin(mount.path, source.path)) {
        result.error = "Branch contains a mount point; Graft was not started";return result;
    }
    const auto parentPath = QFileInfo(source.path).absolutePath();
    const auto name = QFile::encodeName(QFileInfo(source.path).fileName());
    result.target = QDir(destination.path).filePath(QFileInfo(source.path).fileName());
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
    const auto held = QFile::encodeName(".ltree-graft-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
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
        if (!cancel->load() && ::syscall(SYS_renameat2, parent.fd, held.constData(), target.fd, name.constData(), RENAME_NOREPLACE) == 0)
            result.moved = true;
        else if (cancel->load()) result.cancelled = true;
        else if (errno == EXDEV) {
            result.crossDevice = true;
            const auto executable = QStandardPaths::findExecutable("mv");
            if (executable.isEmpty()) result.error = "GNU mv is required for Graft across filesystems";
            else {
                // Los descriptores heredados fijan los padres; no se ejecuta un shell.
                QProcess process;auto env = QProcessEnvironment::systemEnvironment();env.insert("LC_ALL", "C");
                process.setProcessEnvironment(env);
                const int sourceFd = parent.fd, targetFd = target.fd;
                process.setChildProcessModifier([sourceFd, targetFd] {
                    ::fcntl(sourceFd, F_SETFD, 0);::fcntl(targetFd, F_SETFD, 0);
                });
                const auto from = QString("/proc/self/fd/%1/%2").arg(parent.fd).arg(QFile::decodeName(held));
                const auto to = QString("/proc/self/fd/%1/%2").arg(target.fd).arg(QFile::decodeName(name));
                process.start(executable, {"--no-clobber", "--no-target-directory", "--", from, to});
                if (!process.waitForStarted(5000)) result.error = "Cannot start GNU mv: " + process.errorString();
                else {
                    process.closeWriteChannel();QByteArray diagnostics;
                    // Una operación entre volúmenes termina antes de devolver el control al modelo.
                    while (process.state() != QProcess::NotRunning) {
                        process.waitForFinished(50);
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
                        "Graft failed: " + (diagnostics.isEmpty() ? QString("Transfer did not complete; source and any destination copy are retained") : QString::fromLocal8Bit(diagnostics).trimmed());
                    else if (!diagnostics.isEmpty()) result.error = QString::fromLocal8Bit(diagnostics).trimmed();
                    result.error.replace(from, source.path).replace(to, result.target);
                }
            }
        } else result.error = errno == EEXIST ? "Destination branch already exists; no replacement made" : "Cannot graft branch: " + systemError();
    }
    if (!result.moved) {
        if (::syscall(SYS_renameat2, parent.fd, held.constData(), parent.fd, name.constData(), RENAME_NOREPLACE) != 0) {
            result.recoverySource = QDir(parentPath).filePath(QFile::decodeName(held));
            result.error += "; source retained at " + result.recoverySource;
        }
    } else {
        if (::fsync(parent.fd) != 0 || ::fsync(target.fd) != 0) result.error += "; moved, but directory synchronization failed";
    }
    if (cancel->load()) result.cancelled = true;
    const auto refreshCancel = std::make_shared<std::atomic_bool>(false);
    result.scan = scanDirectories(parentPath, false, refreshCancel);
    result.scan.directories += scanDirectories(destination.path, false, refreshCancel).directories;
    // Un fallo durante la retirada entre volúmenes puede dejar un origen parcial.
    if (result.crossDevice && !result.moved && result.recoverySource.isEmpty())
        result.scan.directories += scanDirectories(source.path, true, refreshCancel).directories;
    return result;
}
}
