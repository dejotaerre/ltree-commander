#pragma once
#include "platform/platform.h"
#include "fs/fileattributes.h"

#include <atomic>
#include <memory>

namespace ltree {

struct FileEntry {
    String path;
    String name;
    int64 size = 0;
    DateTime modified;
    bool hidden = false;
    bool writable = true;
    bool symlink = false;
    FileAttributes attributes{};
};

struct DirectoryScan {
    String path;
    StringList children;
    Vector<FileEntry> files;
    String error;
};

struct ScanResult {
    Vector<DirectoryScan> directories;
    bool cancelled = false;
};

using Cancellation = std::shared_ptr<std::atomic_bool>;
ScanResult scanDirectories(const String &path, bool recursive, const Cancellation &cancel, const StringList &boundaries = {});
bool isWithin(const String &path, const String &base);

}
