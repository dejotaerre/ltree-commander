#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"

namespace ltree {
struct DirectoryCompareOptions {
    int identical=0, size=0, binary=0;
    bool unique=true, newer=true, older=false, caseSensitive=true;
};
struct DirectoryCompareResult {
    Set<String> matches;
    StringList errors;
    bool cancelled=false;
};
DirectoryCompareResult compareDirectories(const Vector<FileEntry> &source,const String &sourceRoot,
    const Vector<FileEntry> &target,const String &targetRoot,const DirectoryCompareOptions &options,const Cancellation &cancel);
enum class CompareFilter { Duplicate, Unique, Size, Content, IdenticalDates, Newest, Oldest };
DirectoryCompareResult filterDuplicates(const Vector<FileEntry> &files, CompareFilter filter,
    bool caseSensitive, bool includeEmpty, const Cancellation &cancel);
}
