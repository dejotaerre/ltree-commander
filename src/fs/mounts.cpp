#include "platform/platform.h"
#include "fs/mounts.h"
#include <algorithm>
#include <limits>

namespace ltree {
namespace {
String decode(const Bytes &text)
{
    Bytes result;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 3 < text.size() && text[i+1] >= '0' && text[i+1] <= '7' &&
            text[i+2] >= '0' && text[i+2] <= '7' && text[i+3] >= '0' && text[i+3] <= '7') {
            result += char((text[i+1]-'0')*64 + (text[i+2]-'0')*8 + text[i+3]-'0'); i += 3;
        } else result += text[i];
    }
    return String::fromUtf8(result);
}
}
String locationSpaceLabel(int64 bytes)
{
    if (bytes < 0) return "N/A";
    if (bytes < 1024) return String::number(bytes) + " B";
    const StringList units{"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    double value = double(bytes); int unit = 0;
    while (value >= 1024 && unit + 1 < units.size()) { value /= 1024; ++unit; }
    return String::number(value, 'f', value >= 100 ? 0 : 1) + " " + units[unit];
}
Vector<MountPoint> locationSpaces(Vector<MountPoint> locations, const std::shared_ptr<std::atomic_bool> &cancel)
{
    const auto program = Paths::findExecutable("stat");
    for (auto &location : locations) {
        location.available = location.capacity = -1;
        if (cancel->load() || program.isEmpty()) break;
        // Limitar la consulta permite salir aunque un NAS no responda.
        Process process; process.start(program, {"-f", "-c", "%a %b %S", "--", location.path});
        if (!process.waitForStarted(500)) continue;
        ElapsedTimer timer; timer.start();
        while (!process.waitForFinished(100) && !cancel->load() && timer.elapsed() < 2000) {}
        if (cancel->load() || process.state() != Process::NotRunning) {
            process.kill(); process.waitForFinished(500); continue;
        }
        if (process.exitCode() != 0) continue;
        const auto fields = process.readAllStandardOutput().trimmed().split(' ');
        if (fields.size() != 3) continue;
        bool availableOk=false, totalOk=false, blockOk=false;
        const auto available=String::fromLatin1(fields[0]).toULongLong(&availableOk), total=String::fromLatin1(fields[1]).toULongLong(&totalOk), block=String::fromLatin1(fields[2]).toULongLong(&blockOk);
        const auto maximum=uint64(std::numeric_limits<int64>::max());
        if (!availableOk || !totalOk || !blockOk || !block || available > maximum/block || total > maximum/block) continue;
        location.available=int64(available*block);location.capacity=int64(total*block);
    }
    return locations;
}
Vector<MountPoint> parseMountInfo(const Bytes &data, bool includeSystem)
{
    const Set<Bytes> virtualTypes{"proc", "sysfs", "devtmpfs", "tmpfs", "devpts", "cgroup", "cgroup2", "securityfs", "pstore", "efivarfs", "bpf", "debugfs", "tracefs", "configfs", "fusectl", "hugetlbfs", "mqueue", "binfmt_misc", "autofs", "nsfs", "overlay", "squashfs", "fuse.portal", "fuse.gvfsd-fuse"};
    Vector<MountPoint> result;
    Set<String> paths;
    for (const auto &line : data.split('\n')) {
        const int divider = int(line.indexOf(" - "));
        if (divider < 0) continue;
        const auto fields = line.left(divider).split(' '), tail = line.mid(divider + 3).split(' ');
        if (fields.size() < 6 || tail.size() < 3) continue;
        const String path = decode(fields[4]);
        if (!path.startsWith('/') || paths.contains(path)) continue;
        if (!includeSystem && path != "/" && virtualTypes.contains(tail[0])) continue;
        if (!includeSystem && (path.startsWith("/var/lib/docker/") || path.startsWith("/run/docker/"))) continue;
        paths.insert(path);
        result.append({path, String::fromUtf8(tail[0]), decode(tail[1]), fields[5].split(',').contains("ro") || tail[2].split(',').contains("ro")});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.path < b.path; });
    return result;
}
Vector<MountPoint> mountedLocations(String *error, bool includeSystem)
{
    // Consultar la tabla no accede a cada disco ni espera respuestas de los NAS.
    File file("/proc/self/mountinfo");
    if (!file.open(IO::ReadOnly)) { if (error) *error = file.errorString(); return {}; }
    return parseMountInfo(file.readAll(), includeSystem);
}
}
