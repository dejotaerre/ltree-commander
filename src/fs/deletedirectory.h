#pragma once
#include "fs/scanner.h"

namespace ltree {
struct DeleteResult {
    QString path;
    QString error;
    bool deleted = false;
    bool cancelled = false;
    bool directory = true;
    ScanResult scan;
    QStringList removedDirectories;
};
DeleteResult deleteEmptyDirectory(const QString &root, const QString &path, const Cancellation &cancel);
DeleteResult deleteFile(const QString &root, const QString &path, const Cancellation &cancel);
DeleteResult deleteEmptyBranch(const QString &root, const QString &path, const Cancellation &cancel);
}
