#pragma once
#include "platform/platform.h"
#include "core/session.h"
#include "fs/mounts.h"

namespace ltree {
struct SystemStatistics {
    String path, mount, source, filesystem, error, cpuType, host, user, sessionType, desktop;
    int64 capacity=-1, available=-1, free=-1, blockSize=-1, totalBlocks=-1;
    int64 ramTotal=-1, ramAvailable=-1, committed=-1, commitLimit=-1;
    double cpuMHz=-1;
    DateTime measured;
};
struct LoggedStatistics {
    uint64 totalFiles=0, totalBytes=0, matchingFiles=0, matchingBytes=0, taggedFiles=0, taggedBytes=0;
    uint64 directories=0, displayedFiles=0, displayedBytes=0;
};
SystemStatistics parseProcStatistics(const Bytes &memory, const Bytes &cpu);
SystemStatistics systemStatistics(const String &path);
LoggedStatistics loggedStatistics(const Session &, const Vector<MountPoint> &mounts={}, const String &mount={});
}
