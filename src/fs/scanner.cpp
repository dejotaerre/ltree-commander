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
            scan.error = "Directory link: following links is not implemented yet";
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

}
