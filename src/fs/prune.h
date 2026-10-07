#pragma once
#include "platform/platform.h"
#include "fs/metadata.h"
#include <functional>

namespace ltree {
struct PruneOptions {
    bool keepCurrentFiles = false, onlyEmpty = false, forceReadOnly = false, trash = false;
};
struct PruneResult {
    String path, error;
    StringList removedFiles, removedDirectories;
    int retained = 0;
    bool cancelled = false;
    ScanResult scan;
};
PruneResult pruneBranch(const String &root, const FileMetadata &source,
                        const Map<String, FileMetadata> &logged, const PruneOptions &options,
                        const Cancellation &cancel, const std::function<void(const String &)> &progress = {});
}
