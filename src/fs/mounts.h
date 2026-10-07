#pragma once
#include "platform/platform.h"

namespace ltree {
struct MountPoint { String path, type, source; bool readOnly = false; };
Vector<MountPoint> parseMountInfo(const Bytes &data, bool includeSystem = false);
Vector<MountPoint> mountedLocations(String *error = nullptr, bool includeSystem = false);
}
