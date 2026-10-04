#pragma once
#include "fs/scanner.h"

namespace ltree {
enum class CopyPaths { Full, Current, Relative, Flat };
enum class CopyReplace { Ask, Always, Older, Never, Rename };
enum class CopyCase { Keep, Lower, Upper };
struct CopyEntry { QString source, target; };
struct CopyPlan { QVector<CopyEntry> entries; QString error; };
struct CopyResult {
    QString target, error;
    bool copied=false, moved=false, exists=false, skipped=false, cancelled=false;
    QStringList created;
    ScanResult scan;
};
QString copyName(const QString &name, const QString &mask, CopyCase letterCase, QString *error);
CopyPlan planCopy(const QVector<FileEntry> &files, const QString &sourceDirectory, const QString &destination,
                  CopyPaths paths, const QString &mask, CopyCase letterCase);
CopyResult copyFile(const QString &sourceRoot, const CopyEntry &entry, CopyReplace replace, const Cancellation &cancel);
CopyResult moveFile(const QString &sourceRoot, const CopyEntry &entry, CopyReplace replace, const Cancellation &cancel);
}
