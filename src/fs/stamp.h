#pragma once
#include "fs/metadata.h"

namespace ltree {
enum class StampField { Written, Accessed, Both };
enum class StampMode { Set, Adjust, Increment };
struct StampTime { qint64 seconds=0, nanoseconds=0; };
struct StampEntry { FileMetadata expected; StampTime written, accessed; StampField field=StampField::Written; };
struct StampPlan { QVector<StampEntry> entries; QString error; };
struct StampResult {
    int changed=0, unchanged=0, failed=0;
    bool cancelled=false;
    QString error;
    ScanResult scan;
};
StampPlan planStamp(const QVector<FileMetadata> &,const QString &,StampField,StampMode);
StampResult stampFiles(const StampPlan &,const Cancellation &);
}
