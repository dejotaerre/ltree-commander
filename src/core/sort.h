#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"

namespace ltree {
enum class SortKey { Name, Extension, Date, Time, Length, Alpha, Number, Size, Unsorted, Value };
struct SortOptions {
    SortKey key = SortKey::Name;
    bool descending = false;
    bool pathFirst = false;
    bool operator==(const SortOptions &) const = default;
};
String sortLabel(const SortOptions &);
void sortFiles(Vector<FileEntry> &, const SortOptions &, bool aggregate);
}
