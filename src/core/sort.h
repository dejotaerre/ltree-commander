#pragma once
#include "fs/scanner.h"

namespace ltree {
enum class SortKey { Name, Extension, Date, Time, Length, Alpha, Number, Size, Unsorted, Value };
struct SortOptions {
    SortKey key = SortKey::Name;
    bool descending = false;
    bool pathFirst = false;
    bool operator==(const SortOptions &) const = default;
};
QString sortLabel(const SortOptions &);
void sortFiles(QVector<FileEntry> &, const SortOptions &, bool aggregate);
}
