#pragma once
#include "platform/platform.h"
#include "fs/metadata.h"

namespace ltree {
struct SymlinkResult {
    String path, error;
    bool created = false, cancelled = false;
    ScanResult scan;
};
SymlinkResult createDirectoryLink(const FileMetadata &source, const String &destination, const Cancellation &cancel);
}
