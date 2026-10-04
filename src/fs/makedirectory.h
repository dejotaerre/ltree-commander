#pragma once
#include "fs/scanner.h"

namespace ltree {
struct MakeDirectoryResult {
    QString path;
    QString error;
    QStringList created;
    ScanResult scan;
    bool cancelled = false;
};
MakeDirectoryResult makeDirectory(const QString &base, const QString &name, const Cancellation &cancel);
}
