#include "platform/platform.h"
#include "fs/scanner.h"
#include "fs/mounts.h"

#include <algorithm>

namespace ltree {

bool isWithin(const String &path, const String &base)
{
    return path == base || path.startsWith(base.endsWith('/') ? base : base + '/');
}

ScanResult scanDirectories(const String &path, bool recursive, const Cancellation &cancel, const StringList &boundaries)
{
    ScanResult result;
    const auto locations = mountedLocations(nullptr, true);
    Set<String> mounts(boundaries.begin(), boundaries.end());
    if (recursive) for (const auto &mount : locations) mounts.insert(mount.path);
    StringList pending{path};
    while (!pending.isEmpty()) {
        if (cancel->load()) { result.cancelled = true; break; }
        const String current = pending.takeLast();
        DirectoryScan scan;
        scan.path = current;
        const FileInfo dirInfo(current);
        if (dirInfo.isSymLink()) {
            scan.error = "Directory link: use Enter or + to open its target; recursive logging does not follow links";
        } else if (!dirInfo.isDir()) {
            scan.error = "The directory no longer exists";
        } else if (!dirInfo.isReadable() || !dirInfo.isExecutable()) {
            scan.error = "Access denied";
        } else {
            const auto entries = DirectoryPath(current).entryInfoList(
                DirectoryPath::AllEntries | DirectoryPath::Hidden | DirectoryPath::System | DirectoryPath::NoDotAndDotDot,
                DirectoryPath::Unsorted);
            for (const auto &entry : entries) {
                if (cancel->load()) { result.cancelled = true; break; }
                if (entry.isDir()) {
                    scan.children.append(entry.absoluteFilePath());
                    if (entry.isSymLink()) scan.directoryLinks.insert(entry.absoluteFilePath(), entry.symLinkTarget());
                    // Los montajes anidados se ven, pero * no recorre otro volumen automáticamente.
                    if (recursive && !entry.isSymLink() && !mounts.contains(entry.absoluteFilePath())) pending.append(entry.absoluteFilePath());
                } else {
                    scan.files.append({entry.absoluteFilePath(), entry.fileName(), entry.size(),
                                       entry.lastModified(), entry.isHidden(), entry.isWritable(),
                                       entry.isSymLink(), readFileAttributes(entry.absoluteFilePath(), filesystemTypeForPath(entry.absoluteFilePath(), locations))});
                }
            }
            // Una lectura interrumpida no debe reemplazar un directorio con datos parciales.
            if (result.cancelled) break;
            // El árbol conserva orden alfabético; los archivos retienen el orden de lectura para Unsorted.
            std::sort(scan.children.begin(), scan.children.end(), [](const auto &a,const auto &b) {
                const int value=String::compare(a,b,TextOptions::CaseInsensitive);
                return value?value<0:a<b;
            });
        }
        result.directories.append(std::move(scan));
    }
    return result;
}

ScanResult scanStartupTree(const String &directory, const Cancellation &cancel)
{
    ScanResult result;
    String path = directory;
    for (;;) {
        const auto scan = scanDirectories(path, false, cancel);
        result.directories += scan.directories;
        result.cancelled = scan.cancelled;
        if (scan.cancelled || scan.directories.isEmpty() || !scan.directories.first().error.isEmpty()) break;
        const String parent = FileInfo(path).absolutePath();
        if (parent == path) break;
        path = parent;
    }
    return result;
}

ScanResult scanBrowseDirectory(const String &path, bool recursive, const Cancellation &cancel)
{
    if (recursive) return scanDirectories(path, recursive, cancel);
    const FileInfo info(path);const String target = info.canonicalFilePath();
    if (!info.isSymLink() && (target.isEmpty() || target == info.absoluteFilePath())) return scanDirectories(path, false, cancel);
    if (target.isEmpty() || !FileInfo(target).isDir()) {
        ScanResult result;
        result.directories.append({path, {}, {}, "Directory link target is missing, circular or not a directory"});
        return result;
    }
    auto result = scanStartupTree(target, cancel);
    if (!result.cancelled && !result.directories.isEmpty() && result.directories.first().error.isEmpty())
        result.navigationTarget = target;
    else if (!result.cancelled && !result.directories.isEmpty())
        result.directories = {{path, {}, {}, "Cannot open directory link target: " + result.directories.first().error}};
    return result;
}

}
