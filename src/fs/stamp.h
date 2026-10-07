#pragma once
#include "platform/platform.h"
#include "fs/metadata.h"

namespace ltree {
enum class StampField { Written, Accessed, Both };
enum class StampMode { Set, Adjust, Increment };
struct StampTime { int64 seconds=0, nanoseconds=0; };
struct StampEntry { FileMetadata expected; StampTime written, accessed; StampField field=StampField::Written; };
struct StampPlan { Vector<StampEntry> entries; String error; };
struct StampResult {
    int changed=0, unchanged=0, failed=0;
    bool cancelled=false;
    String error;
    ScanResult scan;
};
StampPlan planStamp(const Vector<FileMetadata> &,const String &,StampField,StampMode);
StampResult stampFiles(const StampPlan &,const Cancellation &);
}
