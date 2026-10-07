#include "platform/platform.h"
#include "fs/mounts.h"
#include <algorithm>

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
