#pragma once

#include "fs/scanner.h"
#include <QRegularExpression>
#include <functional>

namespace ltree {

enum class SearchMode { Text, Hex, Unicode, Regex };
struct SearchOptions {
    QString query;
    SearchMode mode = SearchMode::Text;
    bool caseSensitive = false;
};
enum class SearchOutcome { Found, NotFound, Error, Cancelled };
struct FileSearchResult {
    SearchOutcome outcome = SearchOutcome::NotFound;
    QString error;
    int errorType = 0;
};

struct RegexSearchMatch {
    qsizetype start = -1, length = 0;
    QString error;
};
inline constexpr qsizetype RegexLineLimit = 8 * 1024 * 1024;
QString searchModeName(SearchMode mode);
SearchMode nextSearchMode(SearchMode mode);
QRegularExpression searchExpression(const SearchOptions &options);
RegexSearchMatch matchRegexLine(const QRegularExpression &expression, QStringView line,
                               qsizetype from = 0, bool backward = false);
QString validateSearch(const SearchOptions &options);
FileSearchResult searchFile(const QString &path, const SearchOptions &options, const Cancellation &cancel,
                            const std::function<void(qint64)> &progress = {});

}
