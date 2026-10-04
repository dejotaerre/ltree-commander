#pragma once
#include "fs/metadata.h"
#include <QMap>
#include <functional>

namespace ltree {
struct PruneOptions {
    bool keepCurrentFiles = false, onlyEmpty = false, forceReadOnly = false, trash = false;
};
struct PruneResult {
    QString path, error;
    QStringList removedFiles, removedDirectories;
    int retained = 0;
    bool cancelled = false;
    ScanResult scan;
};
PruneResult pruneBranch(const QString &root, const FileMetadata &source,
                        const QMap<QString, FileMetadata> &logged, const PruneOptions &options,
                        const Cancellation &cancel, const std::function<void(const QString &)> &progress = {});
}
