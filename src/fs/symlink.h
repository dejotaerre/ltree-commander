#pragma once
#include "fs/metadata.h"

namespace ltree {
struct SymlinkResult {
    QString path, error;
    bool created = false, cancelled = false;
    ScanResult scan;
};
SymlinkResult createDirectoryLink(const FileMetadata &source, const QString &destination, const Cancellation &cancel);
}
