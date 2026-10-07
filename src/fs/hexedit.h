#pragma once
#include "platform/platform.h"
namespace ltree {
struct HexSnapshot { uint64 device=0,inode=0; int64 changedSeconds=0,changedNanos=0; bool valid=false; bool operator==(const HexSnapshot &) const = default; };
HexSnapshot hexSnapshot(const String &path);
String saveHexPage(const String &path,const HexSnapshot &expected,const Bytes &original,Index offset,const Bytes &page);
}
