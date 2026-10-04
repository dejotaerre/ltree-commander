#pragma once
#include "fs/metadata.h"
#include <functional>

namespace ltree {
struct GraftResult {
    QString source, target, error, recoverySource;
    bool moved = false, cancelled = false, crossDevice = false;
    ScanResult scan;
};
GraftResult graftBranch(const FileMetadata &source, const FileMetadata &destination,
                        const Cancellation &cancel, const std::function<void(const QString &)> &progress = {});
}
