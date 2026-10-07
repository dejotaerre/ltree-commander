#include "platform/platform.h"
#include "fs/fileattributes.h"
#include "fs/metadata.h"
#include <algorithm>
#include <fcntl.h>
#include <linux/msdos_fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace ltree {
String filesystemTypeForPath(const String &path, const Vector<MountPoint> &mounts)
{
    String type;Index longest = -1;
    for (const auto &mount : mounts) {
        if (mount.path.size() > longest && (path == mount.path || path.startsWith(mount.path == "/" ? "/" : mount.path + '/'))) {
            longest = mount.path.size();type = mount.type;
        }
    }
    return type;
}
FileAttributes readFileAttributes(const String &path, const String &filesystemType)
{
    FileAttributes result;
    const String type = filesystemType.toLower();
    const bool fat = type == "vfat" || type == "msdos" || type == "fat" || type == "exfat";
    const bool ntfs = type == "ntfs" || type == "ntfs3" || type == "ntfs-3g" || type.startsWith("fuse.ntfs");
    const bool fuseBlock = type == "fuseblk";
    result.style = fat || ntfs ? AttributeStyle::Dos : fuseBlock ? AttributeStyle::Unknown : AttributeStyle::Posix;
    if (path.isEmpty() || path.contains(Char::Null)) return result;
    const Bytes native = File::encodeName(path);struct stat before{};
    if (::lstat(native.constData(), &before) < 0) return result;
    result.mode = uint32(before.st_mode & 07777);
    if (fat && (S_ISREG(before.st_mode) || S_ISDIR(before.st_mode))) {
        // Consultar solo la entrada seleccionada, sin seguir enlaces ni bloquear en archivos especiales.
        const int fd = ::open(native.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            struct stat current{};uint32 flags = 0;
            if (::fstat(fd, &current) == 0 && current.st_dev == before.st_dev && current.st_ino == before.st_ino &&
                ::ioctl(fd, FAT_IOCTL_GET_ATTRIBUTES, &flags) == 0) result.dos = flags;
            ::close(fd);
        }
    } else if (ntfs || fuseBlock || type == "fuse") {
        unsigned char bigEndian[4];uint32 flags = 0;
        // Ambos controladores NTFS publican la variante BE; la variante nativa sirve de compatibilidad.
        if (::lgetxattr(native.constData(), "system.ntfs_attrib_be", bigEndian, sizeof(bigEndian)) == ssize_t(sizeof(bigEndian)))
            result.dos = fromBigEndian<uint32>(bigEndian);
        else if (::lgetxattr(native.constData(), "system.ntfs_attrib", &flags, sizeof(flags)) == ssize_t(sizeof(flags))) result.dos = flags;
        if (result.dos) result.style = AttributeStyle::Dos;
    }
    return result;
}
String fileAttributeText(const FileAttributes &attributes, bool symlink, int width)
{
    String text;
    if (attributes.style == AttributeStyle::Posix) {
        text = attributes.mode ? (width >= 11 ? permissionText(*attributes.mode) : permissionOctal(*attributes.mode)) : String(width >= 11 ? "?????????" : "????");
    } else if (attributes.dos) {
        const uint32 flags = *attributes.dos;
        text = String(flags & ATTR_HIDDEN ? "H" : ".") + (flags & ATTR_RO ? "R" : ".") + (flags & ATTR_SYS ? "S" : ".") + (flags & ATTR_ARCH ? "A" : ".");
    } else text = "????";
    return text.leftJustified(std::max(4, width - 2), ' ') + ' ' + (symlink ? 'l' : '.');
}
}
