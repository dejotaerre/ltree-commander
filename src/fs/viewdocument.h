#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"

namespace ltree {
struct ViewDocument {
    Bytes bytes;
    String text, encoding, error;
    Vector<Index> lines;
    bool cancelled = false;
};
ViewDocument readViewDocument(const String &path, const Cancellation &cancel, bool indexLines = true);
}
