#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"

namespace ltree {
struct MakeDirectoryResult {
    String path;
    String error;
    StringList created;
    ScanResult scan;
    bool cancelled = false;
};
MakeDirectoryResult makeDirectory(const String &base, const String &name, const Cancellation &cancel);
}
