#pragma once
#include "platform/platform.h"
#include "fs/metadata.h"
#include "fs/copyfiles.h"
#include <functional>
#include <mutex>

namespace ltree {
struct GraftProgress {
    TransferProgress transfer;
    void setPath(const String &path) { std::lock_guard lock(mutex_);path_=path; }
    String path() const { std::lock_guard lock(mutex_);return path_; }
private:
    mutable std::mutex mutex_;
    String path_;
};
struct GraftResult {
    String source, target, error, recoverySource;
    bool moved = false, cancelled = false, crossDevice = false;
    ScanResult scan;
};
GraftResult graftBranch(const FileMetadata &source, const FileMetadata &destination,
                        const Cancellation &cancel, const std::function<void(const String &)> &progress = {},
                        GraftProgress *details = nullptr);
}
