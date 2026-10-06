#pragma once
#include "fs/mounts.h"
#include <optional>

namespace ltree {
enum class AttributeStyle { Posix, Dos, Unknown };
struct FileAttributes {
    AttributeStyle style = AttributeStyle::Posix;
    std::optional<quint32> mode, dos;
};
QString filesystemTypeForPath(const QString &path, const QVector<MountPoint> &mounts);
FileAttributes readFileAttributes(const QString &path, const QString &filesystemType);
QString fileAttributeText(const FileAttributes &attributes, bool symlink, int width = 11);
}
