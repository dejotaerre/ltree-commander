#pragma once
#include "fs/scanner.h"
#include <QSet>

namespace ltree {
struct DirectoryCompareOptions {
    int identical=0, size=0, binary=0;
    bool unique=true, newer=true, older=false, caseSensitive=true;
};
struct DirectoryCompareResult {
    QSet<QString> matches;
    QStringList errors;
    bool cancelled=false;
};
DirectoryCompareResult compareDirectories(const QVector<FileEntry> &source,const QString &sourceRoot,
    const QVector<FileEntry> &target,const QString &targetRoot,const DirectoryCompareOptions &options,const Cancellation &cancel);
enum class CompareFilter { Duplicate, Unique, Size, Content, IdenticalDates, Newest, Oldest };
DirectoryCompareResult filterDuplicates(const QVector<FileEntry> &files, CompareFilter filter,
    bool caseSensitive, bool includeEmpty, const Cancellation &cancel);
}
