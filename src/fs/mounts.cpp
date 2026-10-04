#include "fs/mounts.h"
#include <QFile>
#include <QSet>
#include <algorithm>

namespace ltree {
namespace {
QString decode(const QByteArray &text)
{
    QByteArray result;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 3 < text.size() && text[i+1] >= '0' && text[i+1] <= '7' &&
            text[i+2] >= '0' && text[i+2] <= '7' && text[i+3] >= '0' && text[i+3] <= '7') {
            result += char((text[i+1]-'0')*64 + (text[i+2]-'0')*8 + text[i+3]-'0'); i += 3;
        } else result += text[i];
    }
    return QString::fromUtf8(result);
}
}
QVector<MountPoint> parseMountInfo(const QByteArray &data, bool includeSystem)
{
    const QSet<QByteArray> virtualTypes{"proc", "sysfs", "devtmpfs", "tmpfs", "devpts", "cgroup", "cgroup2", "securityfs", "pstore", "efivarfs", "bpf", "debugfs", "tracefs", "configfs", "fusectl", "hugetlbfs", "mqueue", "binfmt_misc", "autofs", "nsfs", "overlay", "squashfs", "fuse.portal", "fuse.gvfsd-fuse"};
    QVector<MountPoint> result;
    QSet<QString> paths;
    for (const auto &line : data.split('\n')) {
        const int divider = int(line.indexOf(" - "));
        if (divider < 0) continue;
        const auto fields = line.left(divider).split(' '), tail = line.mid(divider + 3).split(' ');
        if (fields.size() < 6 || tail.size() < 3) continue;
        const QString path = decode(fields[4]);
        if (!path.startsWith('/') || paths.contains(path)) continue;
        if (!includeSystem && path != "/" && virtualTypes.contains(tail[0])) continue;
        if (!includeSystem && (path.startsWith("/var/lib/docker/") || path.startsWith("/run/docker/"))) continue;
        paths.insert(path);
        result.append({path, QString::fromUtf8(tail[0]), decode(tail[1]), fields[5].split(',').contains("ro") || tail[2].split(',').contains("ro")});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.path < b.path; });
    return result;
}
QVector<MountPoint> mountedLocations(QString *error, bool includeSystem)
{
    // Consultar la tabla no accede a cada disco ni espera respuestas de los NAS.
    QFile file("/proc/self/mountinfo");
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return {}; }
    return parseMountInfo(file.readAll(), includeSystem);
}
}
