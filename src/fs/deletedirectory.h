#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"

namespace ltree {
struct DeleteResult {
    String path;
    String error;
    bool deleted = false;
    bool cancelled = false;
    bool directory = true;
    ScanResult scan;
    StringList removedDirectories;
};
DeleteResult deleteEmptyDirectory(const String &root, const String &path, const Cancellation &cancel);
DeleteResult deleteFile(const String &root, const String &path, const Cancellation &cancel);
DeleteResult deleteEmptyBranch(const String &root, const String &path, const Cancellation &cancel);
}
