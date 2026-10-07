#pragma once
#include "platform/platform.h"

#include "fs/scanner.h"

namespace ltree {

struct FileMetadata {
    String path, error, type, owner, group, linkTarget;
    uint32 mode = 0, uid = 0, gid = 0;
    uint64 device = 0, inode = 0, links = 0, size = 0, allocated = 0;
    DateTime accessed, modified, changed, created;
    int64 changedSeconds = 0, changedNanoseconds = 0;
    int64 accessedNanoseconds = 0, modifiedNanoseconds = 0;
    bool directory = false, regular = false, symlink = false;
};

struct PermissionResult {
    String error;
    bool changed = false, cancelled = false, writable = false;
    uint32 mode = 0;
};

FileMetadata readMetadata(const String &path);
String permissionText(uint32 mode);
String permissionOctal(uint32 mode);
bool parsePermissions(const String &expression, uint32 current, bool directory, uint32 &result, String &error);
PermissionResult changePermissions(const FileMetadata &expected, uint32 mode, const Cancellation &cancel);

}
