#pragma once
#include "platform/platform.h"
#include "fs/metadata.h"

namespace ltree {
struct RenameResult {
    String source, target, error;
    bool renamed = false, cancelled = false;
    ScanResult scan;
};
RenameResult renameItem(const FileMetadata &expected, const String &name, const Cancellation &cancel);
}
