#pragma once

#include "fs/scanner.h"

namespace ltree {

struct FileMetadata {
    QString path, error, type, owner, group, linkTarget;
    quint32 mode = 0, uid = 0, gid = 0;
    quint64 device = 0, inode = 0, links = 0, size = 0, allocated = 0;
    QDateTime accessed, modified, changed, created;
    qint64 changedSeconds = 0, changedNanoseconds = 0;
    qint64 accessedNanoseconds = 0, modifiedNanoseconds = 0;
    bool directory = false, regular = false, symlink = false;
};

struct PermissionResult {
    QString error;
    bool changed = false, cancelled = false, writable = false;
    quint32 mode = 0;
};

FileMetadata readMetadata(const QString &path);
QString permissionText(quint32 mode);
QString permissionOctal(quint32 mode);
bool parsePermissions(const QString &expression, quint32 current, bool directory, quint32 &result, QString &error);
PermissionResult changePermissions(const FileMetadata &expected, quint32 mode, const Cancellation &cancel);

}
