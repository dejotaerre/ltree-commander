#include "fs/prune.h"
#include "fs/mounts.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ltree {
namespace {
struct Descriptor {
    int fd;
    explicit Descriptor(int value) : fd(value) {}
    ~Descriptor() { if (fd >= 0) ::close(fd); }
};
int openPath(const QString &path)
{
    int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (const auto &part : path.split('/', Qt::SkipEmptyParts)) {
        if (fd < 0) break;
        const int next = ::openat(fd, QFile::encodeName(part).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int code = errno;::close(fd);fd = next;errno = code;
    }
    return fd;
}
int openChild(int parent, const QByteArray &name)
{
    // NO_XDEV incluye montajes bind; la comprobación es atómica con la apertura.
    struct open_how how{};
    how.flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    how.resolve = RESOLVE_NO_XDEV | RESOLVE_NO_SYMLINKS;
    return int(::syscall(SYS_openat2, parent, name.constData(), &how, sizeof(how)));
}
bool same(const struct stat &value, const FileMetadata &expected)
{
    return quint64(value.st_dev) == expected.device && quint64(value.st_ino) == expected.inode;
}
}
PruneResult pruneBranch(const QString &root, const FileMetadata &source,
                        const QMap<QString, FileMetadata> &logged, const PruneOptions &options,
                        const Cancellation &cancel, const std::function<void(const QString &)> &progress)
{
    PruneResult result;result.path = QDir::cleanPath(source.path);
    const auto base = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const auto fail = [&](const QString &path, const QString &reason) {
        ++result.retained;if (result.error.isEmpty()) result.error = reason + ": " + path;
    };
    if (source.path.contains(QChar(0)) || root.contains(QChar(0)) || !source.path.startsWith('/') ||
        result.path == "/" || result.path == base || !isWithin(result.path, base) ||
        !source.error.isEmpty() || !source.directory || source.symlink || !logged.contains(result.path)) {
        result.error = "Select a logged real directory below the root";return result;
    }
    if (cancel->load()) { result.cancelled = true;return result; }
    if (options.trash && !QFile::supportsMoveToTrash()) { result.error = "Trash is not available";return result; }
    QSet<QString> mounts;
    for (const auto &mount : mountedLocations(nullptr, true)) mounts.insert(mount.path);
    Descriptor parent(openPath(QFileInfo(result.path).absolutePath()));
    if (parent.fd < 0) { result.error = "Cannot open the branch parent: " + QString::fromLocal8Bit(std::strerror(errno));return result; }
    const auto name = QFile::encodeName(QFileInfo(result.path).fileName());
    QSet<QString> visitedDirectories;
    // QFile usa rutas para la papelera: se comprueba también que el padre siga siendo el abierto.
    const auto remove = [&](int fd, const QByteArray &entry, const QString &path, const struct stat &expected, bool directory) {
        if (cancel->load()) { result.cancelled = true;return false; }
        struct stat current{};
        if (::fstatat(fd, entry.constData(), &current, AT_SYMLINK_NOFOLLOW) != 0 ||
            current.st_dev != expected.st_dev || current.st_ino != expected.st_ino) {
            fail(path, "Entry changed before removal");return false;
        }
        if (!options.forceReadOnly && S_ISREG(current.st_mode) && !(current.st_mode & 0222)) {
            fail(path, "Read-only file retained; F2 permits its deletion");return false;
        }
        bool removed = false;
        if (options.trash) {
            struct stat actualParent{}, openedParent{};
            const auto parentPath = QFile::encodeName(QFileInfo(path).absolutePath());
            if (::stat(parentPath.constData(), &actualParent) != 0 || ::fstat(fd, &openedParent) != 0 ||
                actualParent.st_dev != openedParent.st_dev || actualParent.st_ino != openedParent.st_ino ||
                QFileInfo(path).absolutePath() != QFileInfo(QFileInfo(path).absolutePath()).canonicalFilePath()) {
                fail(path, "Parent changed before moving to Trash");return false;
            }
            if (QFile::encodeName(QFile::decodeName(entry)) != entry) { fail(path, "Filename cannot be represented for Trash");return false; }
            QFile file(path);removed = file.moveToTrash();
            if (!removed) fail(path, "Cannot move to Trash: " + file.errorString());
        } else {
            removed = ::unlinkat(fd, entry.constData(), directory ? AT_REMOVEDIR : 0) == 0;
            if (!removed) fail(path, "Cannot delete: " + QString::fromLocal8Bit(std::strerror(errno)));
        }
        if (removed) (directory ? result.removedDirectories : result.removedFiles).append(path);
        return removed;
    };
    std::function<bool(int,const QByteArray &,const QString &,int)> visit;
    visit = [&](int parentFd, const QByteArray &entry, const QString &path, int depth) {
        if (cancel->load()) { result.cancelled = true;return false; }
        if (progress) progress(path);
        if (!logged.contains(path) || !logged.value(path).directory || logged.value(path).symlink ||
            !logged.value(path).error.isEmpty() || mounts.contains(path) || depth > 512) { fail(path, "Unlogged directory or mount retained");return false; }
        Descriptor dir(openChild(parentFd, entry));struct stat identity{};
        if (dir.fd < 0 || ::fstat(dir.fd, &identity) != 0 || !same(identity, logged.value(path)) ||
            (path == source.path && !same(identity, source))) {
            fail(path, "Cannot open the original directory without crossing links or mounts");return false;
        }
        visitedDirectories.insert(path);
        DIR *stream = ::fdopendir(::dup(dir.fd));
        if (!stream) { fail(path, "Cannot read directory");return false; }
        QVector<QByteArray> entries;int readError = 0;
        while (true) {
            errno = 0;const auto item = ::readdir(stream);
            if (!item) { readError = errno;break; }
            const QByteArray child(item->d_name);
            if (child != "." && child != "..") entries.append(child);
            if (cancel->load()) { result.cancelled = true;break; }
        }
        ::closedir(stream);
        if (readError || result.cancelled) { if (readError) fail(path, "Cannot finish reading directory");return false; }
        bool empty = true;
        for (const auto &child : entries) {
            if (cancel->load()) { result.cancelled = true;return false; }
            const auto childPath = QDir(path).filePath(QFile::decodeName(child));
            if (progress) progress(childPath);
            struct stat value{};
            if (::fstatat(dir.fd, child.constData(), &value, AT_SYMLINK_NOFOLLOW) != 0) { fail(childPath, "Entry changed while reading");empty = false;continue; }
            if (mounts.contains(childPath)) { fail(childPath, "Mount retained");empty = false;continue; }
            if (S_ISDIR(value.st_mode)) {
                if (!visit(dir.fd, child, childPath, depth + 1)) empty = false;
            } else if (options.onlyEmpty || (options.keepCurrentFiles && path == result.path)) empty = false;
            else if (!options.forceReadOnly && S_ISREG(value.st_mode) && !(value.st_mode & 0222)) {
                fail(childPath, "Read-only file retained; F2 permits its deletion");empty = false;
            } else if (!remove(dir.fd, child, childPath, value, false)) empty = false;
        }
        if (!empty || (options.keepCurrentFiles && path == result.path)) return false;
        // rmdir exige que siga vacío; un archivo nuevo impide borrar el directorio.
        if (options.trash) {
            DIR *check = ::fdopendir(::dup(dir.fd));bool stillEmpty = check != nullptr;
            if (check) {
                ::rewinddir(check);
                while (true) {
                    errno = 0;const auto item = ::readdir(check);
                    if (!item) { if (errno) stillEmpty = false;break; }
                    if (QByteArray(item->d_name) != "." && QByteArray(item->d_name) != "..") { stillEmpty = false;break; }
                }
                ::closedir(check);
            }
            if (!stillEmpty) { fail(path, "Directory is no longer empty");return false; }
        }
        return remove(parentFd, entry, path, identity, true);
    };
    visit(parent.fd, name, result.path, 0);
    if (cancel->load()) result.cancelled = true;
    // La recarga final no se cancela: el modelo debe reflejar incluso un resultado parcial.
    const auto refreshCancel = std::make_shared<std::atomic_bool>(false);
    if (result.removedDirectories.contains(result.path)) {
        result.scan = scanDirectories(QFileInfo(result.path).absolutePath(), false, refreshCancel);
    } else {
        // Actualizar solo carpetas conocidas evita cargar ramas retenidas o montajes ajenos.
        for (auto it = logged.cbegin(); it != logged.cend(); ++it) {
            if (!visitedDirectories.contains(it.key()) || result.removedDirectories.contains(it.key())) continue;
            const auto actual = readMetadata(it.key());
            if (!actual.error.isEmpty() || actual.symlink || actual.device != it->device || actual.inode != it->inode) continue;
            result.scan.directories += scanDirectories(it.key(), false, refreshCancel).directories;
        }
    }
    return result;
}
}
