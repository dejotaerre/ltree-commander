#pragma once
#include "platform/platform.h"
#include "fs/mounts.h"
#include <optional>

namespace ltree {
enum class AttributeStyle { Posix, Dos, Unknown };
struct FileAttributes {
    AttributeStyle style = AttributeStyle::Posix;
    std::optional<uint32> mode, dos;
};
String filesystemTypeForPath(const String &path, const Vector<MountPoint> &mounts);
FileAttributes readFileAttributes(const String &path, const String &filesystemType);
String fileAttributeText(const FileAttributes &attributes, bool symlink, int width = 11);
}
