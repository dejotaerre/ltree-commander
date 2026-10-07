#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"

namespace ltree {
enum class CopyPaths { Full, Current, Relative, Flat };
enum class CopyReplace { Ask, Always, Older, Never, Rename };
enum class CopyCase { Keep, Lower, Upper };
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
CopyResult copyFile(const String &sourceRoot, const CopyEntry &entry, CopyReplace replace, const Cancellation &cancel);
CopyResult moveFile(const String &sourceRoot, const CopyEntry &entry, CopyReplace replace, const Cancellation &cancel);
}
