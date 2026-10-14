#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"
#include <functional>

namespace ltree {
enum class CopyPaths { Full, Current, Relative, Flat };
enum class CopyReplace { Ask, Always, Older, Never, Rename };
enum class CopyCase { Keep, Lower, Upper };
enum class TransferPhase { Preparing, Copying, Moving, Syncing, Publishing, RemovingSource, Refreshing, Completed };
using TransferCallback = std::function<void(int64, int64, TransferPhase)>;
struct TransferProgress {
    std::atomic<int64> bytes{0}, total{-1};
    std::atomic<TransferPhase> phase{TransferPhase::Preparing};
    void report(int64 done, int64 size, TransferPhase stage) { bytes.store(done); total.store(size); phase.store(stage); }
};
int transferPercent(const TransferProgress &progress);
String transferMetrics(const TransferProgress &progress, int64 elapsedMs, bool compact = false);
struct CopyEntry { String source, target; };
struct CopyPlan { Vector<CopyEntry> entries; String error; };
struct CopyResult {
    String target, error;
    bool copied=false, moved=false, exists=false, skipped=false, cancelled=false;
    StringList created;
    ScanResult scan;
};
String copyName(const String &name, const String &mask, CopyCase letterCase, String *error);
CopyPlan planCopy(const Vector<FileEntry> &files, const String &sourceDirectory, const String &destination,
                  CopyPaths paths, const String &mask, CopyCase letterCase);
CopyResult copyFile(const String &sourceRoot, const CopyEntry &entry, CopyReplace replace, const Cancellation &cancel, const TransferCallback &progress = {});
CopyResult moveFile(const String &sourceRoot, const CopyEntry &entry, CopyReplace replace, const Cancellation &cancel, const TransferCallback &progress = {});
}
