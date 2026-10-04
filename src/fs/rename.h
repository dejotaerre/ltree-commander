#pragma once
#include "fs/metadata.h"

namespace ltree {
struct RenameResult {
    QString source, target, error;
    bool renamed = false, cancelled = false;
    ScanResult scan;
};
RenameResult renameItem(const FileMetadata &expected, const QString &name, const Cancellation &cancel);
}
