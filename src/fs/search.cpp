#include "platform/platform.h"
#include "fs/search.h"

namespace ltree {
namespace {

// KMP para texto literal; un autómata acotado para los asteriscos internos.
class Matcher {
    String pattern_;
    Vector<bool> stars_;
    Vector<int> prefix_;
    Vector<bool> states_;
    bool wildcard_ = false;
    bool lines_;
    int matched_ = 0;
public:
    Matcher(String pattern, bool allowWildcards, bool lines)
        : pattern_(std::move(pattern)), stars_(pattern_.size(), false),
          prefix_(pattern_.size(), 0), states_(pattern_.size() + 1, false), lines_(lines)
    {
        if (allowWildcards && !pattern_.startsWith('*')) {
            for (int i = 1; i + 1 < pattern_.size(); ++i)
                if (pattern_[i] == '*') { stars_[i] = true; wildcard_ = true; }
        }
        for (int i = 1, j = 0; i < pattern_.size(); ++i) {
            while (j && pattern_[i] != pattern_[j]) j = prefix_[j - 1];
            if (pattern_[i] == pattern_[j]) ++j;
            prefix_[i] = j;
        }
    }
    bool feed(Char ch)
    {
        if (lines_ && (ch == '\n' || ch == '\r')) {
            matched_ = 0; states_.fill(false); return false;
        }
        if (!wildcard_) {
            while (matched_ && ch != pattern_[matched_]) matched_ = prefix_[matched_ - 1];
            if (ch == pattern_[matched_]) ++matched_;
            return matched_ == pattern_.size();
        }
        states_[0] = true;
        for (int i = 0; i < pattern_.size(); ++i)
            if (states_[i] && stars_[i]) states_[i + 1] = true;
        Vector<bool> next(states_.size(), false);
        for (int i = 0; i < pattern_.size(); ++i) if (states_[i]) {
            if (stars_[i]) next[i] = true;
            else if (pattern_[i] == ch) next[i + 1] = true;
        }
        for (int i = 0; i < pattern_.size(); ++i)
            if (next[i] && stars_[i]) next[i + 1] = true;
        states_ = std::move(next);
        return states_.last();
    }
};

}

String searchModeName(SearchMode mode)
{
    switch (mode) {
    case SearchMode::Text: return "text";
    case SearchMode::Hex: return "hex";
    case SearchMode::Unicode: return "unicode";
    case SearchMode::Regex: return "regex";
    }
    return {};
}

SearchMode nextSearchMode(SearchMode mode)
{
    return SearchMode((int(mode) + 1) % 4);
}

Regex searchExpression(const SearchOptions &options)
{
    // PCRE2 limita el trabajo y la memoria incluso con patrones de retroceso excesivo.
    return Regex("(*LIMIT_MATCH=1000000)(*LIMIT_DEPTH=1000)(*LIMIT_HEAP=8192)" + options.query,
        Regex::UseUnicodePropertiesOption |
        (options.caseSensitive ? Regex::NoPatternOption : Regex::CaseInsensitiveOption));
}

RegexSearchMatch matchRegexLine(const Regex &expression, StringView line,
                               Index from, bool backward)
{
    if (line.size() > RegexLineLimit) return {-1, 0, "Regex line limit: 8 Mi characters"};
    RegexSearchMatch result;
    if (from < 0 || (!backward && from > line.size())) return result;
    for (Index offset = backward ? 0 : from; offset <= line.size();) {
        // Los offsets del visor pueden quedar en la segunda mitad de un carácter suplementario.
        if (offset > 0 && offset < line.size() && line[offset].isLowSurrogate() && line[offset-1].isHighSurrogate()) ++offset;
        const auto match = expression.matchView(line, offset);
        if (!match.isValid()) return {-1, 0, "Regex matching limit reached; simplify the pattern"};
        if (!match.hasMatch() || (backward && match.capturedStart() > from)) break;
        result.start = match.capturedStart(); result.length = match.capturedLength();
        if (!backward) break;
        offset = result.start + 1;
    }
    return result;
}

String validateSearch(const SearchOptions &options)
{
    if (options.query.isEmpty()) return "Enter the search text";
    if (options.query.size() > 255) return "Search accepts up to 255 characters";
    if (options.mode == SearchMode::Hex) {
        static const Regex hex("^[0-9A-Fa-f]{2}(?:\\s+[0-9A-Fa-f]{2})*$");
        if (!hex.match(options.query.trimmed()).hasMatch()) return "Use hex pairs separated by spaces: 6F 72 64";
    } else if (options.query.contains('\n') || options.query.contains('\r')) {
        return "Text searches cannot contain line breaks";
    }
    if (options.mode == SearchMode::Regex) {
        const Regex expression(options.query, Regex::UseUnicodePropertiesOption);
        if (!expression.isValid())
            return String("Invalid regex at character %1: %2").arg(expression.patternErrorOffset() + 1).arg(expression.errorString());
        const auto bounded = searchExpression(options);
        if (!bounded.isValid()) return "Invalid regex: " + bounded.errorString();
    }
    return {};
}

FileSearchResult searchFile(const String &path, const SearchOptions &options, const Cancellation &cancel,
                            const std::function<void(int64)> &progress)
{
    const String invalid = validateSearch(options);
    if (!invalid.isEmpty()) return {SearchOutcome::Error, invalid, 4};
    if (cancel->load()) return {SearchOutcome::Cancelled, {}, 0};
    if (!FileInfo(path).isFile()) return {SearchOutcome::Error, "Not a regular file or it no longer exists", 2};
    File file(path);
    if (!file.open(IO::ReadOnly)) return {SearchOutcome::Error, file.errorString(), 100 + int(file.error())};
    if (progress) progress(0);
    const bool hex = options.mode == SearchMode::Hex;
    const bool regex = options.mode == SearchMode::Regex;
    const bool unicode = options.mode == SearchMode::Unicode || regex;
    const bool fold = !hex && !options.caseSensitive;
    const auto foldText = [unicode](String text) {
        if (unicode) return text.toCaseFolded();
        // En modo texto se conservan todos los bytes salvo las mayúsculas ASCII.
        for (Char &ch : text) if (ch >= Char('A') && ch <= Char('Z')) ch = Char(ch.unicode() + 32);
        return text;
    };
    String pattern = hex ? String::fromLatin1(Bytes::fromHex(options.query.toLatin1())) :
                      unicode ? options.query : String::fromLatin1(options.query.toUtf8());
    if (fold) pattern = foldText(pattern);
    Matcher matcher(pattern, !hex, !hex);
    const auto encoding = unicode
        ? TextEncoding::encodingForData(file.peek(4)).value_or(TextEncoding::Utf8)
        : TextEncoding::Utf8;
    TextDecoder decoder(encoding);
    const Regex expression = regex ? searchExpression(options) : Regex{};
    String line;
    bool afterCR = false, lastSeparator = false;
    while (!file.atEnd()) {
        if (cancel->load()) return {SearchOutcome::Cancelled, {}, 0};
        const Bytes block = file.read(64 * 1024);
        if (file.error() != File::NoError)
            return {SearchOutcome::Error, file.errorString(), 100 + int(file.error())};
        if (progress) progress(file.pos());
        String decoded = unicode ? String(decoder(block)) : String::fromLatin1(block);
        if (unicode && decoder.hasError()) return {SearchOutcome::Error, "Invalid encoding for this search mode", 3};
        if (fold && !regex) decoded = foldText(decoded);
        for (int i = 0; i < decoded.size(); ++i) {
            if ((i & 4095) == 0 && cancel->load()) return {SearchOutcome::Cancelled, {}, 0};
            if (!regex) {
                if (matcher.feed(decoded[i])) return {SearchOutcome::Found, {}, 0};
            } else {
                const Char ch = decoded[i];
                if (ch == '\r' || ch == '\n') {
                    lastSeparator = true;
                    if (ch == '\n' && afterCR) { afterCR = false; continue; }
                    const auto match = matchRegexLine(expression, line);
                    if (!match.error.isEmpty()) return {SearchOutcome::Error, match.error, 5};
                    if (match.start >= 0) return {SearchOutcome::Found, {}, 0};
                    line.clear(); afterCR = ch == '\r';
                } else {
                    lastSeparator = false;
                    afterCR = false;
                    if (line.size() == RegexLineLimit) return {SearchOutcome::Error, "Regex line limit: 8 Mi characters", 5};
                    line += ch;
                }
            }
        }
    }
    if (unicode) {
        char16_t tail[8];
        const auto final = decoder.finalize(tail, 8);
        if (decoder.hasError() || final.error != TextEncoding::FinalizeResultError::NoError)
            return {SearchOutcome::Error, "Incomplete Unicode sequence at end of file", 3};
    }
    if (regex && !lastSeparator) {
        if (cancel->load()) return {SearchOutcome::Cancelled, {}, 0};
        const auto match = matchRegexLine(expression, line);
        if (!match.error.isEmpty()) return {SearchOutcome::Error, match.error, 5};
        if (match.start >= 0) return {SearchOutcome::Found, {}, 0};
    }
    return {SearchOutcome::NotFound, {}, 0};
}

}
