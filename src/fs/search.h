#pragma once
#include "platform/platform.h"

#include "fs/scanner.h"
#include <functional>

namespace ltree {

enum class SearchMode { Text, Hex, Unicode, Regex };
struct SearchOptions {
    String query;
    SearchMode mode = SearchMode::Text;
    bool caseSensitive = false;
};
enum class SearchOutcome { Found, NotFound, Error, Cancelled };
struct FileSearchResult {
    SearchOutcome outcome = SearchOutcome::NotFound;
    String error;
    int errorType = 0;
};

struct RegexSearchMatch {
    Index start = -1, length = 0;
    String error;
};
inline constexpr Index RegexLineLimit = 8 * 1024 * 1024;
String searchModeName(SearchMode mode);
SearchMode nextSearchMode(SearchMode mode);
Regex searchExpression(const SearchOptions &options);
RegexSearchMatch matchRegexLine(const Regex &expression, StringView line,
                               Index from = 0, bool backward = false);
String validateSearch(const SearchOptions &options);
FileSearchResult searchFile(const String &path, const SearchOptions &options, const Cancellation &cancel,
                            const std::function<void(int64)> &progress = {});

}
