#include "fs/scanner.h"
#include "fs/mounts.h"
#include <QSet>

#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace ltree {

bool isWithin(const QString &path, const QString &base)
{
    return path == base || path.startsWith(base.endsWith('/') ? base : base + '/');
}

ScanResult scanDirectories(const QString &path, bool recursive, const Cancellation &cancel, const QStringList &boundaries)
{
    ScanResult result;
    QSet<QString> mounts(boundaries.begin(), boundaries.end());
    if (recursive) for (const auto &mount : mountedLocations(nullptr, true)) mounts.insert(mount.path);
    QStringList pending{path};
    while (!pending.isEmpty()) {
        if (cancel->load()) { result.cancelled = true; break; }
        const QString current = pending.takeLast();
        DirectoryScan scan;
        scan.path = current;
        const QFileInfo dirInfo(current);
        if (dirInfo.isSymLink()) {
            scan.error = "Directory link: following links is not implemented yet";
        } else if (!dirInfo.isDir()) {
            scan.error = "The directory no longer exists";
        } else if (!dirInfo.isReadable() || !dirInfo.isExecutable()) {
            scan.error = "Access denied";
        } else {
            const auto entries = QDir(current).entryInfoList(
                QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                QDir::Unsorted);
            for (const auto &entry : entries) {
                if (cancel->load()) { result.cancelled = true; break; }
                if (entry.isDir()) {
                    scan.children.append(entry.absoluteFilePath());
                    // Los montajes anidados se ven, pero * no recorre otro volumen automáticamente.
                    if (recursive && !entry.isSymLink() && !mounts.contains(entry.absoluteFilePath())) pending.append(entry.absoluteFilePath());
                } else {
                    scan.files.append({entry.absoluteFilePath(), entry.fileName(), entry.size(),
                                       entry.lastModified(), entry.isHidden(), entry.isWritable(),
                                       entry.isSymLink()});
                }
            }
            // Una lectura interrumpida no debe reemplazar un directorio con datos parciales.
            if (result.cancelled) break;
            // El árbol conserva orden alfabético; los archivos retienen el orden de lectura para Unsorted.
            std::sort(scan.children.begin(), scan.children.end(), [](const auto &a,const auto &b) {
                const int value=QString::compare(a,b,Qt::CaseInsensitive);
                return value?value<0:a<b;
            });
        }
        result.directories.append(std::move(scan));
    }
    return result;
}

}
