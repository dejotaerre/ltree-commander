#pragma once
#include "platform/platform.h"
#include <atomic>
#include <memory>

namespace ltree {
struct MountPoint { String path, type, source; bool readOnly = false; int64 available = -1, capacity = -1; };
String locationSpaceLabel(int64 bytes);
Vector<MountPoint> locationSpaces(Vector<MountPoint> locations, const std::shared_ptr<std::atomic_bool> &cancel);
Vector<MountPoint> parseMountInfo(const Bytes &data, bool includeSystem = false);
Vector<MountPoint> mountedLocations(String *error = nullptr, bool includeSystem = false);
}
