#include "platform/portable.h"
#include "fs/mounts.h"
#include "fs/scanner.h"
#include <unicode/uchar.h>
#include <unicode/ucnv.h>
#include <unicode/ucnv_err.h>
#include <unicode/ustring.h>
#define PCRE2_CODE_UNIT_WIDTH 16
#include <atomic>
#include <cerrno>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <pcre2.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <random>
#include <sstream>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace ltree {
namespace {
String systemError() { return String::fromLocal8Bit(std::strerror(errno)); }
std::filesystem::path nativePath(const String &s) { return std::filesystem::path(s.toStdString()); }
String displayPath(const std::filesystem::path &s) { return String::fromUtf8(s.string().c_str()); }
String transform(const String &s, int mode) {
    const auto input = s.std16();
    UErrorCode error = U_ZERO_ERROR;
    const auto run = [&](char16_t *out, int32_t count) {
        if (mode == 0)
            return u_strToLower(out, count, input.data(), int32_t(input.size()), "", &error);
        if (mode == 1)
            return u_strToUpper(out, count, input.data(), int32_t(input.size()), "", &error);
        return u_strFoldCase(out, count, input.data(), int32_t(input.size()), U_FOLD_CASE_DEFAULT, &error);
    };
    auto n = run(nullptr, 0);
    if (error != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(error))
        return s;
    error = U_ZERO_ERROR;
    std::u16string out(size_t(n), char16_t(0));
    run(out.data(), n);
    return U_SUCCESS(error) ? String(out) : s;
}

}
bool Char::isSpace() const { return u_isUWhiteSpace(value); }
bool Char::isDigit() const { return u_isdigit(value); }
bool Char::isLetter() const { return u_isalpha(value); }
bool Char::isLetterOrNumber() const { return u_isalnum(value); }
bool Char::isPrint() const { return u_isprint(value); }
Char Char::toLower() const { return Char(int(u_tolower(value))); }
Char Char::toUpper() const { return Char(int(u_toupper(value))); }
String::String(const char *s) { *this = fromUtf8(s ? s : ""); }
String::String(const std::u16string &s) {
    for (auto c : s)
        data_.push_back(Char(c));
}
std::u16string String::std16() const {
    std::u16string result;
    result.reserve(data_.size());
    for (Char c : data_)
        result += char16_t(c.unicode());
    return result;
}
String String::fromUtf8(const char *s, Index n) {
    if (!s)
        return {};
    if (n < 0)
        n = Index(std::strlen(s));
    UErrorCode error = U_ZERO_ERROR;
    int32_t length = 0, substitutions = 0;
    u_strFromUTF8WithSub(nullptr, 0, &length, s, int32_t(n), 0xfffd, &substitutions, &error);
    if (error != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(error))
        return {};
    error = U_ZERO_ERROR;
    std::u16string output(size_t(length), 0);
    u_strFromUTF8WithSub(output.data(), length, &length, s, int32_t(n), 0xfffd, &substitutions, &error);
    return U_SUCCESS(error) ? String(output) : String{};
}
String String::fromUtf8(const Bytes &s) { return fromUtf8(s.constData(), s.size()); }
String String::fromLatin1(const char *s, Index n) {
    String out;
    if (!s)
        return out;
    if (n < 0)
        n = Index(std::strlen(s));
    out.reserve(n);
    for (Index i = 0; i < n; ++i)
        out.data_ += Char(s[i]);
    return out;
}
String String::fromLatin1(const Bytes &s) { return fromLatin1(s.constData(), s.size()); }
String String::fromLocal8Bit(const Bytes &s) { return fromUtf8(s); }
Bytes String::toUtf8() const {
    auto input = std16();
    UErrorCode error = U_ZERO_ERROR;
    int32_t length = 0;
    u_strToUTF8WithSub(nullptr, 0, &length, input.data(), int32_t(input.size()), 0xfffd, nullptr, &error);
    if (error != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(error))
        return {};
    error = U_ZERO_ERROR;
    std::string out(size_t(length), 0);
    u_strToUTF8WithSub(out.data(), length, &length, input.data(), int32_t(input.size()), 0xfffd, nullptr,
                       &error);
    return U_SUCCESS(error) ? Bytes(out) : Bytes{};
}
Bytes String::toLatin1() const {
    Bytes out;
    out.reserve(size());
    for (Char c : data_)
        out += c.unicode() <= 255 ? char(c.unicode()) : '?';
    return out;
}
Bytes String::toLocal8Bit() const { return toUtf8(); }
std::string String::toStdString() const {
    auto bytes = toUtf8();
    return std::string(bytes.constData(), size_t(bytes.size()));
}
Vector<char32_t> String::toUcs4() const {
    Vector<char32_t> out;
    for (Index i = 0; i < size(); ++i) {
        char32_t c = (*this)[i].unicode();
        if ((*this)[i].isHighSurrogate() && i + 1 < size() && (*this)[i + 1].isLowSurrogate()) {
            c = 0x10000 + ((c - 0xd800) << 10) + ((*this)[++i].unicode() - 0xdc00);
        }
        out.append(c);
    }
    return out;
}
String String::fromUcs4(const char32_t *s, Index n) {
    String out;
    for (Index i = 0; i < n; ++i) {
        auto c = s[i];
        if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            c = 0xfffd;
        if (c < 0x10000)
            out += Char(int(c));
        else {
            c -= 0x10000;
            out += Char(int(0xd800 + (c >> 10)));
            out += Char(int(0xdc00 + (c & 1023)));
        }
    }
    return out;
}
String String::number(uint64 n, int base) {
    if (base < 2 || base > 36)
        base = 10;
    std::string out;
    do {
        out += "0123456789abcdefghijklmnopqrstuvwxyz"[n % uint64(base)];
        n /= uint64(base);
    } while (n);
    std::reverse(out.begin(), out.end());
    return String(out);
}
String String::number(int64 n, int base) {
    if (n < 0)
        return "-" + number(uint64(-(n + 1)) + 1, base);
    return number(uint64(n), base);
}
String String::number(double n, char format, int precision) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(precision);
    if (format == 'f')
        out << std::fixed;
    if (format == 'e')
        out << std::scientific;
    out << n;
    return String(out.str());
}
String String::mid(Index i, Index n) const {
    String out;
    if (i < 0) {
        n = n < 0 ? n : n + i;
        i = 0;
    }
    if (i > size())
        return out;
    if (n < 0)
        n = size() - i;
    n = std::clamp(n, Index(0), size() - i);
    out.data_ = data_.substr(size_t(i), size_t(n));
    return out;
}
String String::left(Index n) const { return n < 0 ? *this : mid(0, n); }
String String::right(Index n) const { return n < 0 ? *this : mid(std::max(Index(0), size() - n)); }
String String::trimmed() const {
    Index a = 0, b = size();
    while (a < b && (*this)[a].isSpace())
        ++a;
    while (b > a && (*this)[b - 1].isSpace())
        --b;
    return mid(a, b - a);
}
String String::simplified() const {
    String out;
    bool pending = false;
    for (auto c : *this) {
        if (c.isSpace())
            pending = !out.isEmpty();
        else {
            if (pending)
                out += ' ';
            out += c;
            pending = false;
        }
    }
    return out;
}
String String::toLower() const { return transform(*this, 0); }
String String::toUpper() const { return transform(*this, 1); }
String String::toCaseFolded() const {
    auto values = toUcs4();
    for (auto &c : values)
        c = char32_t(u_foldCase(UChar32(c), U_FOLD_CASE_DEFAULT));
    return fromUcs4(values.data(), values.size());
}
int String::compare(const String &a, const String &b, TextOptions::CaseSensitivity cs) {
    if (cs == TextOptions::CaseInsensitive)
        return compare(a.toCaseFolded(), b.toCaseFolded(), TextOptions::CaseSensitive);
    const auto n = std::min(a.size(), b.size());
    for (Index i = 0; i < n; ++i) {
        const auto x = a[i].unicode(), y = b[i].unicode();
        if (x != y)
            return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
}
Index String::indexOf(const String &s, Index from, TextOptions::CaseSensitivity cs) const {
    if (cs == TextOptions::CaseInsensitive)
        return toCaseFolded().indexOf(s.toCaseFolded(), from, TextOptions::CaseSensitive);
    if (from < 0)
        from = std::max(Index(0), size() + from);
    auto position = data_.find(s.data_, size_t(from));
    return position == decltype(data_)::npos ? -1 : Index(position);
}
Index String::lastIndexOf(const String &s, Index from, TextOptions::CaseSensitivity cs) const {
    if (cs == TextOptions::CaseInsensitive)
        return toCaseFolded().lastIndexOf(s.toCaseFolded(), from, TextOptions::CaseSensitive);
    if (from < 0)
        from += size();
    if (from < 0)
        return -1;
    auto position = data_.rfind(s.data_, size_t(from));
    return position == decltype(data_)::npos ? -1 : Index(position);
}
bool String::startsWith(const String &s, TextOptions::CaseSensitivity cs) const {
    return s.size() <= size() && compare(left(s.size()), s, cs) == 0;
}
bool String::endsWith(const String &s, TextOptions::CaseSensitivity cs) const {
    return s.size() <= size() && compare(right(s.size()), s, cs) == 0;
}
StringList String::split(const String &separator, TextOptions::SplitBehavior behavior) const {
    StringList out;
    Index start = 0;
    while (start <= size()) {
        const auto at = separator.isEmpty() ? start : indexOf(separator, start);
        auto item = at < 0 ? mid(start) : mid(start, at - start);
        if (behavior == TextOptions::KeepEmptyParts || !item.isEmpty())
            out.append(item);
        if (at < 0)
            break;
        start = at + std::max(Index(1), separator.size());
    }
    return out;
}
StringList String::split(const Regex &separator, TextOptions::SplitBehavior behavior) const {
    StringList out;
    Index start = 0, pos = 0;
    while (pos <= size()) {
        const auto found = separator.match(*this, pos);
        if (!found.hasMatch())
            break;
        auto item = mid(start, found.capturedStart() - start);
        if (behavior == TextOptions::KeepEmptyParts || !item.isEmpty())
            out.append(item);
        start = found.capturedEnd();
        pos = start + (found.capturedLength() == 0);
    }
    auto tail = mid(start);
    if (behavior == TextOptions::KeepEmptyParts || !tail.isEmpty())
        out.append(tail);
    return out;
}
String &String::replace(const String &from, const String &to) {
    if (from.isEmpty())
        return *this;
    Index at = 0;
    while ((at = indexOf(from, at)) >= 0) {
        remove(at, from.size());
        insert(at, to);
        at += to.size();
    }
    return *this;
}
String &String::replace(const Regex &from, const String &to) {
    Index pos = 0;
    while (pos <= size()) {
        auto match = from.match(*this, pos);
        if (!match.hasMatch())
            break;
        const auto at = match.capturedStart(), count = match.capturedLength();
        remove(at, count);
        insert(at, to);
        pos = at + to.size() + (count == 0);
    }
    return *this;
}
String String::leftJustified(Index width, Char fill, bool cut) const {
    if (size() >= width)
        return cut ? left(width) : *this;
    return *this + String(width - size(), fill);
}
String String::rightJustified(Index width, Char fill, bool cut) const {
    if (size() >= width)
        return cut ? left(width) : *this;
    return String(width - size(), fill) + *this;
}
String String::arg(const String &value, Index width, Char fill) const {
    int minimum = 100;
    for (Index i = 0; i + 1 < size(); ++i)
        if ((*this)[i] == '%' && (*this)[i + 1].isDigit()) {
            int n = (*this)[i + 1].unicode() - '0';
            if (i + 2 < size() && (*this)[i + 2].isDigit())
                n = n * 10 + (*this)[i + 2].unicode() - '0';
            minimum = std::min(minimum, n);
        }
    if (minimum == 100)
        return *this;
    String result;
    auto inserted = width < 0 ? value.leftJustified(-width, fill) : value.rightJustified(width, fill);
    if (fill == '0' && width > value.size() && value.startsWith('-'))
        inserted = String("-") + value.mid(1).rightJustified(width - 1, fill);
    for (Index i = 0; i < size(); ++i) {
        if ((*this)[i] == '%' && i + 1 < size() && (*this)[i + 1].isDigit()) {
            int n = (*this)[i + 1].unicode() - '0';
            Index digits = 1;
            if (i + 2 < size() && (*this)[i + 2].isDigit()) {
                n = n * 10 + (*this)[i + 2].unicode() - '0';
                digits = 2;
            }
            if (n == minimum) {
                result += inserted;
                i += digits;
                continue;
            }
        }
        result += (*this)[i];
    }
    return result;
}
String String::argMany(const Vector<String> &values) const {
    Set<int> placeholders;
    for (Index i = 0; i + 1 < size(); ++i)
        if ((*this)[i] == '%' && (*this)[i + 1] >= '0' && (*this)[i + 1] <= '9') {
            int n = (*this)[i + 1].unicode() - '0';
            if (i + 2 < size() && (*this)[i + 2] >= '0' && (*this)[i + 2] <= '9')
                n = n * 10 + (*this)[i + 2].unicode() - '0';
            placeholders.insert(n);
        }
    Map<int, String> replacements;
    Index argument = 0;
    for (int n : placeholders) {
        if (argument >= values.size())
            break;
        replacements.insert(n, values[argument++]);
    }
    String result;
    for (Index i = 0; i < size(); ++i) {
        if ((*this)[i] == '%' && i + 1 < size() && (*this)[i + 1] >= '0' && (*this)[i + 1] <= '9') {
            int n = (*this)[i + 1].unicode() - '0';
            Index digits = 1;
            if (i + 2 < size() && (*this)[i + 2] >= '0' && (*this)[i + 2] <= '9') {
                n = n * 10 + (*this)[i + 2].unicode() - '0';
                digits = 2;
            }
            if (replacements.contains(n)) {
                result += replacements.value(n);
                i += digits;
                continue;
            }
        }
        result += (*this)[i];
    }
    return result;
}
int64 String::toLongLong(bool *ok, int base) const {
    auto text = trimmed().toStdString();
    char *end = nullptr;
    errno = 0;
    auto value = std::strtoll(text.c_str(), &end, base);
    bool valid = !text.empty() && end == text.c_str() + text.size() && errno != ERANGE;
    if (ok)
        *ok = valid;
    return valid ? int64(value) : 0;
}
unsigned String::toUInt(bool *ok, int base) const {
    bool valid;
    auto n = toULongLong(&valid, base);
    valid = valid && n <= std::numeric_limits<unsigned>::max();
    if (ok)
        *ok = valid;
    return valid ? unsigned(n) : 0;
}
uint64 String::toULongLong(bool *ok, int base) const {
    auto text = trimmed().toStdString();
    char *end = nullptr;
    errno = 0;
    auto value = std::strtoull(text.c_str(), &end, base);
    bool valid = !text.empty() && text[0] != '-' && end == text.c_str() + text.size() && errno != ERANGE;
    if (ok)
        *ok = valid;
    return valid ? uint64(value) : 0;
}
int String::toInt(bool *ok, int base) const {
    bool valid;
    auto value = toLongLong(&valid, base);
    valid = valid && value >= INT_MIN && value <= INT_MAX;
    if (ok)
        *ok = valid;
    return valid ? int(value) : 0;
}
double String::toDouble(bool *ok) const {
    auto text = trimmed().toStdString();
    char *end = nullptr;
    errno = 0;
    auto value = std::strtod(text.c_str(), &end);
    bool valid = !text.empty() && end == text.c_str() + text.size() && errno != ERANGE;
    if (ok)
        *ok = valid;
    return valid ? value : 0;
}
Bytes Bytes::mid(Index i, Index n) const {
    i = std::clamp(i, Index(0), size());
    if (n < 0)
        n = size() - i;
    return Bytes(data_.substr(size_t(i), size_t(std::clamp(n, Index(0), size() - i))));
}
Bytes Bytes::trimmed() const { return String::fromLatin1(*this).trimmed().toLatin1(); }
Bytes Bytes::simplified() const { return String::fromLatin1(*this).simplified().toLatin1(); }
Bytes Bytes::toLower() const {
    Bytes out = *this;
    for (auto &c : out)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 'a' - 'A');
    return out;
}
Bytes Bytes::toHex(char separator) const {
    Bytes out;
    for (Index i = 0; i < size(); ++i) {
        if (i && separator)
            out += separator;
        const auto c = uint8((*this)[i]);
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
Bytes Bytes::fromHex(const Bytes &s) {
    Bytes digits;
    for (auto c : s)
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
            digits += c;
    if (digits.size() % 2)
        digits = Bytes("0") + digits;
    Bytes out;
    int high = -1;
    for (auto c : digits) {
        int n = c <= '9' ? c - '0' : c <= 'F' ? c - 'A' + 10 : c - 'a' + 10;
        if (high < 0)
            high = n;
        else {
            out += char(high * 16 + n);
            high = -1;
        }
    }
    return out;
}
Index Bytes::indexOf(const Bytes &s, Index from) const {
    auto pos = data_.find(s.data_, size_t(std::max(Index(0), from)));
    return pos == std::string::npos ? -1 : Index(pos);
}
Bytes &Bytes::replace(const Bytes &from, const Bytes &to) {
    if (from.isEmpty())
        return *this;
    Index at = 0;
    while ((at = indexOf(from, at)) >= 0) {
        data_.replace(size_t(at), size_t(from.size()), to.data_);
        at += to.size();
    }
    return *this;
}
Index Bytes::lastIndexOf(const Bytes &s, Index from) const {
    if (from < 0)
        from = size() + from;
    if (from < 0)
        return -1;
    auto pos = data_.rfind(s.data_, size_t(std::max(Index(0), from)));
    return pos == std::string::npos ? -1 : Index(pos);
}
Vector<Bytes> Bytes::split(char separator) const {
    Vector<Bytes> out;
    Index start = 0;
    for (Index i = 0; i <= size(); ++i)
        if (i == size() || (*this)[i] == separator) {
            out.append(mid(start, i - start));
            start = i + 1;
        }
    return out;
}
struct Regex::Data {
    pcre2_code *code = nullptr;
    String error;
    Index offset = -1;
    ~Data() {
        if (code)
            pcre2_code_free(code);
    }
};
Regex::Regex(const String &pattern, int options) : data_(std::make_shared<Data>()) {
    auto text = pattern.std16();
    uint32 flags = PCRE2_UTF;
    if (options & CaseInsensitiveOption)
        flags |= PCRE2_CASELESS;
    if (options & DotMatchesEverythingOption)
        flags |= PCRE2_DOTALL;
    if (options & UseUnicodePropertiesOption)
        flags |= PCRE2_UCP;
    if (options & MultilineOption)
        flags |= PCRE2_MULTILINE;
    int error = 0;
    PCRE2_SIZE offset = 0;
    data_->code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(), flags, &error,
                                &offset, nullptr);
    if (!data_->code) {
        PCRE2_UCHAR message[256];
        int n = pcre2_get_error_message(error, message, 256);
        if (n > 0)
            data_->error = String(std::u16string(reinterpret_cast<char16_t *>(message), size_t(n)));
        data_->offset = Index(offset);
    }
}
bool Regex::isValid() const { return data_ && data_->code; }
String Regex::errorString() const { return data_ ? data_->error : String("No expression"); }
Index Regex::patternErrorOffset() const { return data_ ? data_->offset : -1; }
RegexMatch Regex::match(const String &s, Index offset) const {
    auto result = matchView(StringView(s), offset);
    result.text_ = s;
    result.view_.reset();
    return result;
}
RegexMatch Regex::matchView(StringView s, Index offset) const {
    // La vista mantiene los offsets UTF-16 sin copiar la línea en cada coincidencia.
    RegexMatch result;
    result.view_ = s;
    if (!isValid()) {
        result.valid_ = false;
        return result;
    }
    if (offset < 0)
        offset = std::max(Index(0), s.size() + offset);
    if (offset > s.size())
        return result;
    auto match = pcre2_match_data_create_from_pattern(data_->code, nullptr);
    if (!match) {
        result.valid_ = false;
        return result;
    }
    int code = pcre2_match(data_->code, reinterpret_cast<PCRE2_SPTR>(s.data()), size_t(s.size()),
                           size_t(offset), 0, match, nullptr);
    if (code >= 0) {
        auto values = pcre2_get_ovector_pointer(match);
        auto count = pcre2_get_ovector_count(match);
        for (uint32 i = 0; i < count; ++i)
            result.captures_.append(
                values[i * 2] == PCRE2_UNSET
                    ? Pair<Index, Index>{-1, 0}
                    : Pair<Index, Index>{Index(values[i * 2]), Index(values[i * 2 + 1] - values[i * 2])});
    } else if (code != PCRE2_ERROR_NOMATCH)
        result.valid_ = false;
    pcre2_match_data_free(match);
    return result;
}
String Regex::escape(const String &s) {
    String out;
    for (auto c : s) {
        if (String("\\.^$|()[]{}*+?").contains(c))
            out += '\\';
        out += c;
    }
    return out;
}
namespace {
bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
int daysInMonth(int y, int m) {
    constexpr int days[]{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m >= 1 && m <= 12 ? days[m] + (m == 2 && leap(y)) : 0;
}
std::tm localTm(int64 seconds) {
    std::tm value{};
    time_t n = time_t(seconds);
    ::localtime_r(&n, &value);
    return value;
}
String formatTm(const std::tm &tm, const String &format) {
    String out;
    auto pattern = format.toStdString();
    for (size_t i = 0; i < pattern.size();) {
        bool matched = false;
        for (const auto &part : std::initializer_list<Pair<std::string, int>>{{"yyyy", tm.tm_year + 1900},
                                                                              {"MM", tm.tm_mon + 1},
                                                                              {"dd", tm.tm_mday},
                                                                              {"HH", tm.tm_hour},
                                                                              {"mm", tm.tm_min},
                                                                              {"ss", tm.tm_sec}}) {
            if (pattern.compare(i, part.first.size(), part.first) == 0) {
                out += String::number(part.second).rightJustified(Index(part.first.size()), '0');
                i += part.first.size();
                matched = true;
                break;
            }
        }
        if (!matched)
            out += Char(pattern[i++]);
    }
    return out;
}
}
bool Date::isValid() const {
    return year_ >= 1 && year_ <= 9999 && month_ >= 1 && month_ <= 12 && day_ >= 1 &&
           day_ <= daysInMonth(year_, month_);
}
int64 Date::toJulianDay() const {
    if (!isValid())
        return 0;
    int64 a = (14 - month_) / 12, y = year_ + 4800 - a, m = month_ + 12 * a - 3;
    return day_ + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}
Date Date::fromJulianDay(int64 n) {
    int64 a = n + 32044, b = (4 * a + 3) / 146097, c = a - (146097 * b) / 4, d = (4 * c + 3) / 1461,
          e = c - 1461 * d / 4, m = (5 * e + 2) / 153;
    return Date(int(100 * b + d - 4800 + m / 10), int(m + 3 - 12 * (m / 10)), int(e - (153 * m + 2) / 5 + 1));
}
Date Date::currentDate() { return DateTime::currentDateTime().date(); }
Date Date::addDays(int64 n) const { return isValid() ? fromJulianDay(toJulianDay() + n) : Date{}; }
Date Date::fromString(const String &s, const String &format) {
    if (format != "yyyy-MM-dd")
        return {};
    const auto match = Regex("^([0-9]{4})-([0-9]{2})-([0-9]{2})$").match(s);
    return match.hasMatch()
               ? Date(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt())
               : Date{};
}
String Date::toString(const String &format) const {
    if (!isValid())
        return {};
    std::tm tm{};
    tm.tm_year = year_ - 1900;
    tm.tm_mon = month_ - 1;
    tm.tm_mday = day_;
    return formatTm(tm, format);
}
Time Time::fromString(const String &s, const String &) {
    const auto m = Regex("^([0-9]{2}):([0-9]{2}):([0-9]{2})$").match(s);
    return m.hasMatch() ? Time(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()) : Time{};
}
String Time::toString(const String &format) const {
    if (!isValid())
        return {};
    std::tm tm{};
    tm.tm_hour = hour_;
    tm.tm_min = minute_;
    tm.tm_sec = second_;
    return formatTm(tm, format);
}
DateTime::DateTime(Date d, Time t) {
    if (!d.isValid() || !t.isValid())
        return;
    std::tm tm{};
    tm.tm_year = d.year() - 1900;
    tm.tm_mon = d.month() - 1;
    tm.tm_mday = d.day();
    tm.tm_hour = t.hour();
    tm.tm_min = t.minute();
    tm.tm_sec = t.second();
    tm.tm_isdst = -1;
    errno = 0;
    time_t n = ::mktime(&tm);
    if (errno == EOVERFLOW)
        return;
    milliseconds_ = int64(n) * 1000;
    valid_ = true;
}
DateTime DateTime::fromSecsSinceEpoch(int64 n) { return fromMSecsSinceEpoch(n * 1000); }
DateTime DateTime::fromMSecsSinceEpoch(int64 n) {
    DateTime out;
    out.milliseconds_ = n;
    out.valid_ = true;
    return out;
}
DateTime DateTime::currentDateTime() {
    return fromMSecsSinceEpoch(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());
}
int64 DateTime::toSecsSinceEpoch() const {
    return milliseconds_ / 1000 - (milliseconds_ < 0 && milliseconds_ % 1000 != 0);
}
Date DateTime::date() const {
    if (!valid_)
        return {};
    auto tm = localTm(toSecsSinceEpoch());
    return Date(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}
Time DateTime::time() const {
    if (!valid_)
        return {};
    auto tm = localTm(toSecsSinceEpoch());
    return Time(tm.tm_hour, tm.tm_min, tm.tm_sec);
}
DateTime DateTime::addSecs(int64 n) const {
    if (!valid_ || n > INT64_MAX / 1000 || n < INT64_MIN / 1000)
        return {};
    auto out = *this;
    out.milliseconds_ += n * 1000;
    return out;
}
DateTime DateTime::addDays(int64 n) const {
    if (!valid_)
        return {};
    DateTime out(date().addDays(n), time());
    out.milliseconds_ += milliseconds_ % 1000;
    return out;
}
DateTime DateTime::addMonths(int n) const {
    if (!valid_)
        return {};
    auto d = date();
    int64 total = int64(d.year()) * 12 + d.month() - 1 + n;
    int y = int(total / 12), m = int(total % 12) + 1;
    DateTime out(Date(y, m, std::min(d.day(), daysInMonth(y, m))), time());
    out.milliseconds_ += milliseconds_ % 1000;
    return out;
}
DateTime DateTime::addYears(int n) const { return addMonths(n * 12); }
String DateTime::toString(const String &format) const {
    return valid_ ? formatTm(localTm(toSecsSinceEpoch()), format) : String{};
}
File::~File() { close(); }
bool File::open(int mode) {
    close();
    int flags = (mode & IO::ReadWrite) == IO::ReadWrite ? O_RDWR
                : (mode & IO::WriteOnly)                ? O_WRONLY
                                                        : O_RDONLY;
    if (mode & IO::WriteOnly) {
        flags |= O_CREAT;
        if (mode & IO::Append)
            flags |= O_APPEND;
        else if ((mode & IO::ReadWrite) != IO::ReadWrite || (mode & IO::Truncate))
            flags |= O_TRUNC;
    }
    fd_ = ::open(name_.toUtf8().constData(), flags | O_CLOEXEC, 0666);
    own_ = true;
    errorCode_ = fd_ < 0 ? OpenError : NoError;
    error_ = fd_ < 0 ? systemError() : String{};
    return fd_ >= 0;
}
bool File::open(int descriptor, int) {
    close();
    fd_ = descriptor;
    own_ = false;
    errorCode_ = NoError;
    return fd_ >= 0;
}
void File::close() {
    if (fd_ >= 0 && own_)
        ::close(fd_);
    fd_ = -1;
}
int64 File::size() const {
    struct stat st{};
    return ::fstat(fd_, &st) == 0 ? int64(st.st_size) : 0;
}
int64 File::pos() const { return ::lseek(fd_, 0, SEEK_CUR); }
bool File::seek(int64 offset) { return ::lseek(fd_, offset, SEEK_SET) >= 0; }
bool File::atEnd() const {
    const auto offset = pos();
    if (offset < 0)
        return true;
    if (offset < size())
        return false;
    // /proc y /sys pueden informar tamaño cero aunque todavía tengan contenido.
    char next;
    ssize_t count;
    do {
        count = ::pread(fd_, &next, 1, offset);
    } while (count < 0 && errno == EINTR);
    return count <= 0;
}
int64 File::read(char *data, int64 n) {
    ssize_t count;
    do {
        count = ::read(fd_, data, size_t(n));
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        errorCode_ = ReadError;
        error_ = systemError();
    }
    return count;
}
Bytes File::read(int64 n) {
    if (n <= 0)
        return {};
    Bytes out(n, TextOptions::Uninitialized);
    auto count = read(out.data(), n);
    out.resize(count > 0 ? count : 0);
    return out;
}
Bytes File::readAll() {
    Bytes out;
    while (true) {
        auto bytes = read(65536);
        if (bytes.isEmpty())
            break;
        out += bytes;
    }
    return out;
}
Bytes File::readLine(int64 limit) {
    Bytes out;
    char c;
    while ((limit <= 0 || out.size() < limit) && read(&c, 1) > 0) {
        out += c;
        if (c == '\n')
            break;
    }
    return out;
}
Bytes File::peek(int64 n) {
    const auto offset = pos();
    auto result = read(n);
    if (offset >= 0)
        seek(offset);
    return result;
}
int64 File::write(const Bytes &s) { return write(s.constData(), s.size()); }
int64 File::write(const char *s, int64 n) {
    int64 done = 0;
    while (done < n) {
        const auto count = ::write(fd_, s + done, size_t(n - done));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            errorCode_ = WriteError;
            error_ = systemError();
            return done ? done : -1;
        }
        done += count;
    }
    return done;
}
bool File::flush() {
    if (fd_ < 0)
        return false;
    if (::fsync(fd_) == 0)
        return true;
    if (errno == EINVAL)
        return true;
    errorCode_ = WriteError;
    error_ = systemError();
    return false;
}
bool File::setPermissions(int mode) {
    return fd_ >= 0 ? ::fchmod(fd_, mode) == 0 : ::chmod(name_.toUtf8().constData(), mode) == 0;
}
bool File::remove() {
    close();
    return remove(name_);
}
bool File::remove(const String &s) { return ::unlink(s.toUtf8().constData()) == 0; }
bool File::exists(const String &s) { return FileInfo(s).exists(); }
bool File::moveToTrash(const String &s, String *target) {
    const auto source = FileInfo(s).absoluteFilePath();
    String base = environment("XDG_DATA_HOME");
    if (base.isEmpty())
        base = DirectoryPath::homePath() + "/.local/share";
    base += "/Trash";
    struct stat original{}, home{};
    if (::lstat(source.toUtf8().constData(), &original) != 0)
        return false;
    if (!DirectoryPath().mkpath(base) || ::stat(base.toUtf8().constData(), &home) != 0)
        return false;
    String storedPath = source;
    if (original.st_dev != home.st_dev) {
        String mount;
        for (const auto &entry : mountedLocations(nullptr, true))
            if (isWithin(source, entry.path) && entry.path.size() > mount.size())
                mount = entry.path;
        if (mount.isEmpty())
            return false;
        base = DirectoryPath(mount).filePath(".Trash-" + String::number(unsigned(::getuid())));
        if (!DirectoryPath().mkpath(base))
            return false;
        storedPath = DirectoryPath(mount).relativeFilePath(source);
    }
    if (isWithin(source, base))
        return false;
    struct Descriptor {
        int fd = -1;
        ~Descriptor() {
            if (fd >= 0)
                ::close(fd);
        }
    } trash, files, info;
    trash.fd = ::open(base.toUtf8().constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat directory{};
    if (trash.fd < 0 || ::fstat(trash.fd, &directory) != 0 || directory.st_uid != ::getuid() ||
        ::fchmod(trash.fd, 0700) != 0)
        return false;
    const auto child = [&](const char *name, Descriptor &into) {
        if (::mkdirat(trash.fd, name, 0700) != 0 && errno != EEXIST)
            return false;
        into.fd = ::openat(trash.fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        return into.fd >= 0 && ::fstat(into.fd, &directory) == 0 && directory.st_uid == ::getuid() &&
               ::fchmod(into.fd, 0700) == 0;
    };
    if (!child("files", files) || !child("info", info))
        return false;
    const auto name = FileInfo(source).fileName() + "-" + Uuid::createUuid().toString(Uuid::Id128);
    const auto native = File::encodeName(name), metadata = File::encodeName(name + ".trashinfo");
    Bytes encoded;
    for (auto c : storedPath.toUtf8()) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' ||
            c == '.' || c == '-' || c == '_')
            encoded += c;
        else {
            encoded += '%';
            Bytes byte(&c, 1);
            encoded += byte.toHex();
        }
    }
    Descriptor output;
    output.fd =
        ::openat(info.fd, metadata.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (output.fd < 0)
        return false;
    const auto body = Bytes("[Trash Info]\nPath=") + encoded + "\nDeletionDate=" +
                      DateTime::currentDateTime().toString("yyyy-MM-ddTHH:mm:ss").toUtf8() + "\n";
    File stream;
    stream.open(output.fd, IO::WriteOnly);
    if (stream.write(body) != body.size() || !stream.flush() ||
        ::syscall(SYS_renameat2, AT_FDCWD, source.toUtf8().constData(), files.fd, native.constData(),
                  RENAME_NOREPLACE) != 0) {
        ::unlinkat(info.fd, metadata.constData(), 0);
        return false;
    }
    if (target)
        *target = base + "/files/" + name;
    return true;
}
SaveFile::~SaveFile() {
    close();
    if (!committed_ && name_ != target_)
        File::remove(name_);
}
bool SaveFile::open(int) {
    std::string temporary = target_.toStdString() + ".ltree-XXXXXX";
    fd_ = ::mkstemp(temporary.data());
    own_ = true;
    if (fd_ < 0) {
        errorCode_ = OpenError;
        error_ = systemError();
        return false;
    }
    ::fcntl(fd_, F_SETFD, FD_CLOEXEC);
    name_ = String::fromUtf8(temporary.c_str());
    return true;
}
bool SaveFile::commit() {
    if (fd_ < 0 || !flush())
        return false;
    close();
    if (::rename(name_.toUtf8().constData(), target_.toUtf8().constData()) != 0) {
        errorCode_ = WriteError;
        error_ = systemError();
        return false;
    }
    committed_ = true;
    return true;
}
namespace {
bool fileStat(const String &path, struct stat &st, bool link = false) {
    return (link ? ::lstat(path.toUtf8().constData(), &st) : ::stat(path.toUtf8().constData(), &st)) == 0;
}
}
bool FileInfo::exists() const {
    struct stat st{};
    return fileStat(path_, st);
}
bool FileInfo::isFile() const {
    struct stat st{};
    return fileStat(path_, st) && S_ISREG(st.st_mode);
}
bool FileInfo::isDir() const {
    struct stat st{};
    return fileStat(path_, st) && S_ISDIR(st.st_mode);
}
bool FileInfo::isSymLink() const {
    struct stat st{};
    return fileStat(path_, st, true) && S_ISLNK(st.st_mode);
}
bool FileInfo::isReadable() const { return ::access(path_.toUtf8().constData(), R_OK) == 0; }
bool FileInfo::isWritable() const { return ::access(path_.toUtf8().constData(), W_OK) == 0; }
bool FileInfo::isExecutable() const { return ::access(path_.toUtf8().constData(), X_OK) == 0; }
bool FileInfo::isHidden() const { return fileName().startsWith('.'); }
String FileInfo::path() const {
    auto parent = nativePath(path_).parent_path();
    return parent.empty() ? String(".") : displayPath(parent);
}
int64 FileInfo::size() const {
    struct stat st{};
    return fileStat(path_, st) ? int64(st.st_size) : 0;
}
String FileInfo::fileName() const { return displayPath(nativePath(path_).filename()); }
String FileInfo::suffix() const {
    const auto name = fileName();
    const auto at = name.lastIndexOf('.');
    return at >= 0 ? name.mid(at + 1) : String{};
}
String FileInfo::baseName() const {
    const auto name = fileName();
    auto at = name.indexOf('.');
    return at >= 0 ? name.left(at) : name;
}
String FileInfo::completeBaseName() const {
    auto name = fileName();
    auto at = name.lastIndexOf('.');
    return at >= 0 ? name.left(at) : name;
}
String FileInfo::absoluteFilePath() const {
    return DirectoryPath::cleanPath(
        DirectoryPath::isAbsolutePath(path_) ? path_ : DirectoryPath::currentPath() + "/" + path_);
}
String FileInfo::absolutePath() const { return displayPath(nativePath(absoluteFilePath()).parent_path()); }
String FileInfo::canonicalFilePath() const {
    std::error_code error;
    auto out = std::filesystem::canonical(nativePath(path_), error);
    return error ? String{} : displayPath(out);
}
String FileInfo::canonicalPath() const {
    auto out = canonicalFilePath();
    return out.isEmpty() ? String{} : displayPath(nativePath(out).parent_path());
}
String FileInfo::symLinkTarget() const {
    std::error_code error;
    auto target = std::filesystem::read_symlink(nativePath(path_), error);
    if (error)
        return {};
    return DirectoryPath::cleanPath(
        displayPath(target.is_absolute() ? target : nativePath(absolutePath()) / target));
}
DateTime FileInfo::lastModified() const {
    struct stat st{};
    return fileStat(path_, st)
               ? DateTime::fromMSecsSinceEpoch(int64(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000)
               : DateTime{};
}
String DirectoryPath::cleanPath(const String &s) {
    if (s.isEmpty())
        return ".";
    auto result = displayPath(nativePath(s).lexically_normal());
    if (result.size() > 1 && result.endsWith('/'))
        result.chop(1);
    return result;
}
bool DirectoryPath::isAbsolutePath(const String &s) { return s.startsWith('/'); }
String DirectoryPath::homePath() {
    auto home = environment("HOME");
    if (!home.isEmpty())
        return home;
    passwd account{}, *found = nullptr;
    char buffer[16384];
    if (::getpwuid_r(::getuid(), &account, buffer, sizeof(buffer), &found) == 0 && found)
        return String::fromLocal8Bit(found->pw_dir);
    return "/";
}
String DirectoryPath::currentPath() {
    char *path = ::getcwd(nullptr, 0);
    auto result = path ? String::fromUtf8(path) : String("/");
    std::free(path);
    return result;
}
String DirectoryPath::tempPath() {
    auto path = environment("TMPDIR");
    return path.isEmpty() ? String("/tmp") : path;
}
String DirectoryPath::filePath(const String &s) const {
    return isAbsolutePath(s) ? s : path_ + (path_.endsWith('/') ? String{} : String("/")) + s;
}
String DirectoryPath::absolutePath() const { return FileInfo(path_).absoluteFilePath(); }
String DirectoryPath::absoluteFilePath(const String &s) const {
    return FileInfo(filePath(s)).absoluteFilePath();
}
String DirectoryPath::relativeFilePath(const String &s) const {
    return displayPath(
        nativePath(FileInfo(s).absoluteFilePath()).lexically_relative(nativePath(absolutePath())));
}
bool DirectoryPath::exists() const { return FileInfo(path_).isDir(); }
bool DirectoryPath::exists(const String &s) const { return FileInfo(filePath(s)).exists(); }
bool DirectoryPath::mkpath(const String &s) const {
    std::error_code error;
    std::filesystem::create_directories(nativePath(filePath(s)), error);
    return !error && FileInfo(filePath(s)).isDir();
}
bool DirectoryPath::mkdir(const String &s) const {
    return ::mkdir(filePath(s).toUtf8().constData(), 0777) == 0;
}
bool DirectoryPath::rmdir(const String &s) const { return ::rmdir(filePath(s).toUtf8().constData()) == 0; }
Vector<FileInfo> DirectoryPath::entryInfoList(int filters, int order) const {
    Vector<FileInfo> out;
    auto handle = ::opendir(path_.toUtf8().constData());
    if (!handle)
        return out;
    while (auto entry = ::readdir(handle)) {
        String name = String::fromUtf8(entry->d_name);
        if ((filters & NoDotAndDotDot) && (name == "." || name == ".."))
            continue;
        if (!(filters & Hidden) && name.startsWith('.') && name != "." && name != "..")
            continue;
        FileInfo info(filePath(name));
        if ((filters & NoSymLinks) && info.isSymLink())
            continue;
        if ((filters & Dirs) && !(filters & AllEntries) && !info.isDir())
            continue;
        if ((filters & Readable) && !info.isReadable())
            continue;
        out.append(info);
    }
    ::closedir(handle);
    if (order == Name)
        std::sort(out.begin(), out.end(), [](const FileInfo &a, const FileInfo &b) {
            return String::compare(a.fileName(), b.fileName(), TextOptions::CaseInsensitive) < 0;
        });
    return out;
}
StringList DirectoryPath::entryList(int filters, int order) const {
    StringList out;
    for (const auto &info : entryInfoList(filters, order))
        out.append(info.fileName());
    return out;
}
TemporaryDirectory::TemporaryDirectory() {
    auto value = (DirectoryPath::tempPath() + "/ltc-XXXXXX").toStdString();
    if (::mkdtemp(value.data()))
        path_ = String::fromUtf8(value.c_str());
}
TemporaryDirectory::~TemporaryDirectory() {
    if (!path_.isEmpty()) {
        std::error_code error;
        std::filesystem::remove_all(nativePath(path_), error);
    }
}
String Paths::writableLocation(Location type) {
    const auto home = DirectoryPath::homePath();
    if (type == HomeLocation)
        return home;
    if (type == TempLocation)
        return DirectoryPath::tempPath();
    if (type == RuntimeLocation)
        return environment("XDG_RUNTIME_DIR");
    auto config = environment("XDG_CONFIG_HOME");
    if (config.isEmpty())
        config = home + "/.config";
    return type == AppConfigLocation ? config + "/ltreec" : config;
}
String Paths::findExecutable(const String &name) {
    if (name.contains('/'))
        return FileInfo(name).isFile() && FileInfo(name).isExecutable() ? FileInfo(name).absoluteFilePath()
                                                                        : String{};
    for (auto path : environment("PATH").split(':')) {
        auto file = DirectoryPath(path).filePath(name);
        if (FileInfo(file).isFile() && FileInfo(file).isExecutable())
            return FileInfo(file).absoluteFilePath();
    }
    return {};
}
ProcessEnvironment ProcessEnvironment::systemEnvironment() {
    ProcessEnvironment result;
    for (char **it = ::environ; *it; ++it) {
        const auto item = String::fromUtf8(*it);
        auto at = item.indexOf('=');
        if (at > 0)
            result.values_.insert(item.left(at), item.mid(at + 1));
    }
    return result;
}
struct Process::Data {
    pid_t pid = -1;
    int input = -1, output = -1, errors = -1, status = 0;
    bool done = false, started = false;
    ChannelMode channels = SeparateChannels;
    InputChannelMode inputMode = ManagedInputChannel;
    ProcessError error = UnknownError;
    std::function<void()> childModifier;
    String directory, message;
    std::optional<ProcessEnvironment> environment;
    Bytes stdoutData, stderrData;
    void closeFd(int &fd) {
        if (fd >= 0)
            ::close(fd);
        fd = -1;
    }
    void drainFd(int &fd, Bytes &into) {
        char buffer[8192];
        while (fd >= 0) {
            auto n = ::read(fd, buffer, sizeof(buffer));
            if (n > 0)
                into += Bytes(buffer, n);
            else if (n == 0) {
                closeFd(fd);
                break;
            } else if (errno == EINTR)
                continue;
            else
                break;
        }
    }
    void drain() {
        drainFd(output, stdoutData);
        drainFd(errors, stderrData);
    }
    void update() {
        drain();
        if (pid > 0 && !done) {
            auto result = ::waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                done = true;
                closeFd(input);
                drain();
            }
        }
    }
    ~Data() {
        closeFd(input);
        closeFd(output);
        closeFd(errors);
    }
};
Process::Process() : data_(std::make_unique<Data>()) {}
Process::~Process() {
    if (data_->pid > 0 && !data_->done) {
        ::kill(data_->pid, SIGKILL);
        while (::waitpid(data_->pid, &data_->status, 0) < 0 && errno == EINTR) {
        }
        data_->done = true;
    }
}
void Process::setWorkingDirectory(const String &s) { data_->directory = s; }
void Process::setProcessEnvironment(const ProcessEnvironment &e) { data_->environment = e; }
void Process::setProcessChannelMode(ChannelMode mode) { data_->channels = mode; }
void Process::setInputChannelMode(InputChannelMode mode) { data_->inputMode = mode; }
void Process::setChildProcessModifier(std::function<void()> modifier) {
    data_->childModifier = std::move(modifier);
}
void Process::start(const String &program, const StringList &arguments) {
    auto &d = *data_;
    String selected = program.contains('/') ? program : Paths::findExecutable(program);
    if (selected.isEmpty()) {
        d.error = FailedToStart;
        d.message = "Executable not found: " + program;
        d.done = true;
        return;
    }
    std::vector<std::string> args{program.toStdString()};
    for (const auto &arg : arguments)
        args.push_back(arg.toStdString());
    std::vector<char *> argv;
    for (auto &arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    auto env = d.environment.value_or(ProcessEnvironment::systemEnvironment());
    std::vector<std::string> variables;
    for (auto it = env.values_.begin(); it != env.values_.end(); ++it)
        variables.push_back((it.key() + "=" + it.value()).toStdString());
    std::vector<char *> envp;
    for (auto &v : variables)
        envp.push_back(v.data());
    envp.push_back(nullptr);
    auto executable = selected.toStdString(), directory = d.directory.toStdString();
    int in[2]{-1, -1}, out[2]{-1, -1}, err[2]{-1, -1}, handshake[2]{-1, -1};
    const auto cleanup = [&] {
        for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1], handshake[0], handshake[1]})
            if (fd >= 0)
                ::close(fd);
    };
    if (::pipe2(handshake, O_CLOEXEC) != 0 ||
        (d.inputMode == ManagedInputChannel && ::pipe2(in, O_CLOEXEC) != 0) ||
        (d.channels != ForwardedChannels && (::pipe2(out, O_CLOEXEC) != 0 || ::pipe2(err, O_CLOEXEC) != 0))) {
        d.error = FailedToStart;
        d.message = systemError();
        cleanup();
        return;
    }
    d.pid = ::fork();
    if (d.pid == 0) {
        ::close(handshake[0]);
        if (!directory.empty() && ::chdir(directory.c_str()) != 0) {
            int code = errno;
            ::write(handshake[1], &code, sizeof(code));
            ::_exit(127);
        }
        if (in[0] >= 0)
            ::dup2(in[0], STDIN_FILENO);
        if (out[1] >= 0)
            ::dup2(out[1], STDOUT_FILENO);
        if (err[1] >= 0)
            ::dup2(d.channels == MergedChannels ? out[1] : err[1], STDERR_FILENO);
        for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]})
            if (fd >= 0)
                ::close(fd);
        struct sigaction action{};
        action.sa_handler = SIG_DFL;
        ::sigemptyset(&action.sa_mask);
        for (int sig : {SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGPIPE, SIGCHLD, SIGTSTP})
            ::sigaction(sig, &action, nullptr);
        if (d.childModifier)
            d.childModifier();
        ::execve(executable.c_str(), argv.data(), envp.data());
        int code = errno;
        ::write(handshake[1], &code, sizeof(code));
        ::_exit(127);
    }
    if (d.pid < 0) {
        d.error = FailedToStart;
        d.message = systemError();
        cleanup();
        return;
    }
    ::close(handshake[1]);
    int code = 0;
    ssize_t count;
    do {
        count = ::read(handshake[0], &code, sizeof(code));
    } while (count < 0 && errno == EINTR);
    ::close(handshake[0]);
    if (in[0] >= 0)
        ::close(in[0]);
    if (out[1] >= 0)
        ::close(out[1]);
    if (err[1] >= 0)
        ::close(err[1]);
    d.input = in[1];
    d.output = out[0];
    d.errors = err[0];
    for (int fd : {d.input, d.output, d.errors})
        if (fd >= 0)
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    if (count > 0) {
        d.error = FailedToStart;
        d.message = String::fromLocal8Bit(std::strerror(code));
        waitForFinished();
        return;
    }
    d.started = true;
    d.error = UnknownError;
}
bool Process::waitForStarted(int) { return data_->started; }
bool Process::waitForFinished(int timeout) {
    auto &d = *data_;
    if (d.pid <= 0)
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, timeout));
    while (true) {
        d.update();
        if (d.done)
            return true;
        if (timeout >= 0 && std::chrono::steady_clock::now() >= deadline)
            return false;
        pollfd fds[2]{{d.output, POLLIN, 0}, {d.errors, POLLIN, 0}};
        ::poll(fds, 2, 10);
    }
}
Process::State Process::state() const {
    data_->update();
    return data_->pid > 0 && !data_->done ? Running : NotRunning;
}
Process::ExitStatus Process::exitStatus() const {
    return WIFSIGNALED(data_->status) ? CrashExit : NormalExit;
}
int Process::exitCode() const { return WIFEXITED(data_->status) ? WEXITSTATUS(data_->status) : -1; }
Process::ProcessError Process::error() const { return data_->error; }
String Process::errorString() const { return data_->message; }
void Process::terminate() {
    if (data_->pid > 0 && !data_->done)
        ::kill(data_->pid, SIGTERM);
}
void Process::kill() {
    if (data_->pid > 0 && !data_->done)
        ::kill(data_->pid, SIGKILL);
}
int64 Process::write(const Bytes &bytes) {
    // Bloquear SIGPIPE solo en este hilo evita terminar la aplicación si el hijo cierra stdin.
    sigset_t blocked, previous, pending;
    ::sigemptyset(&blocked);
    ::sigaddset(&blocked, SIGPIPE);
    const bool maskChanged = ::pthread_sigmask(SIG_BLOCK, &blocked, &previous) == 0;
    ::sigpending(&pending);
    const bool alreadyPending = ::sigismember(&pending, SIGPIPE);
    Index done = 0;
    while (done < bytes.size() && data_->input >= 0) {
        auto n = ::write(data_->input, bytes.constData() + done, size_t(bytes.size() - done));
        if (n > 0)
            done += n;
        else if (n < 0 && errno == EINTR)
            continue;
        else if (n < 0 && errno == EAGAIN) {
            data_->update();
            pollfd fd{data_->input, POLLOUT, 0};
            ::poll(&fd, 1, 50);
        } else
            break;
    }
    if (maskChanged) {
        if (!alreadyPending && !::sigismember(&previous, SIGPIPE)) {
            timespec timeout{};
            while (::sigtimedwait(&blocked, nullptr, &timeout) < 0 && errno == EINTR) {
            }
        }
        ::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    }
    return done;
}
void Process::closeWriteChannel() { data_->closeFd(data_->input); }
Bytes Process::readAllStandardOutput() {
    data_->drain();
    auto out = data_->stdoutData;
    data_->stdoutData.clear();
    return out;
}
Bytes Process::readAllStandardError() {
    data_->drain();
    auto out = data_->stderrData;
    data_->stderrData.clear();
    return out;
}
StringList Process::splitCommand(const String &command) {
    StringList out;
    String token;
    bool quoted = false;
    for (Index i = 0; i < command.size(); ++i) {
        auto c = command[i];
        if (c == '"') {
            if (i + 2 < command.size() && command[i + 1] == '"' && command[i + 2] == '"') {
                token += '"';
                i += 2;
            } else
                quoted = !quoted;
        } else if (c.isSpace() && !quoted) {
            if (!token.isEmpty()) {
                out.append(token);
                token.clear();
            }
        } else
            token += c;
    }
    if (!token.isEmpty())
        out.append(token);
    return out;
}
String Locale::toString(uint64 n) const {
    auto text = String::number(n);
    for (Index i = text.size() - 3; i > 0; i -= 3)
        text.insert(i, ".");
    return text;
}
Uuid Uuid::createUuid() {
    unsigned char bytes[16];
    Index done = 0;
    while (done < 16) {
        auto n = ::getrandom(bytes + done, size_t(16 - done), 0);
        if (n > 0)
            done += n;
        else if (errno != EINTR)
            throw std::runtime_error("Cannot generate a secure temporary name");
    }
    Uuid result;
    result.value_ = String::fromLatin1(Bytes(reinterpret_cast<char *>(bytes), 16).toHex());
    return result;
}
String Uuid::toString(Format format) const {
    if (format == Id128)
        return value_;
    auto out = value_;
    for (Index at : {Index(20), Index(16), Index(12), Index(8)})
        out.insert(at, "-");
    return out;
}
StorageInfo::StorageInfo(const String &path) : path_(FileInfo(path).absoluteFilePath()) {
    for (const auto &entry : mountedLocations(nullptr, true))
        if (isWithin(path_, entry.path) && entry.path.size() > mount_.size()) {
            mount_ = entry.path;
            source_ = entry.source;
            type_ = entry.type;
        }
    struct statvfs st{};
    if (::statvfs(path_.toUtf8().constData(), &st) == 0) {
        auto block = st.f_frsize ? st.f_frsize : st.f_bsize;
        total_ = int64(st.f_blocks) * int64(block);
        free_ = int64(st.f_bfree) * int64(block);
        available_ = int64(st.f_bavail) * int64(block);
    }
}
String SystemInfo::machineHostName() {
    char name[HOST_NAME_MAX + 1]{};
    return ::gethostname(name, sizeof(name)) == 0 ? String::fromLocal8Bit(name) : String{};
}
std::optional<TextEncoding::Encoding> TextEncoding::encodingForData(const Bytes &b) {
    if (b.size() >= 4 && b.left(4) == Bytes("\x00\x00\xfe\xff", 4))
        return Utf32BE;
    if (b.size() >= 4 && b.left(4) == Bytes("\xff\xfe\x00\x00", 4))
        return Utf32LE;
    if (b.size() >= 2 && b.left(2) == Bytes("\xfe\xff", 2))
        return Utf16BE;
    if (b.size() >= 2 && b.left(2) == Bytes("\xff\xfe", 2))
        return Utf16LE;
    if (b.size() >= 3 && b.left(3) == Bytes("\xef\xbb\xbf", 3))
        return Utf8;
    return std::nullopt;
}
const char *TextEncoding::nameForEncoding(Encoding e) {
    switch (e) {
    case Utf8:
        return "UTF-8";
    case Utf16:
        return "UTF-16";
    case Utf16LE:
        return "UTF-16LE";
    case Utf16BE:
        return "UTF-16BE";
    case Utf32:
        return "UTF-32";
    case Utf32LE:
        return "UTF-32LE";
    case Utf32BE:
        return "UTF-32BE";
    case Latin1:
        return "ISO-8859-1";
    case System:
        return "UTF-8";
    }
    return "UTF-8";
}
std::optional<TextEncoding::Encoding> TextEncoding::encodingForName(const Bytes &n) {
    auto text = String::fromLatin1(n).toUpper();
    for (int i = 0; i <= int(System); ++i)
        if (text == nameForEncoding(Encoding(i)))
            return Encoding(i);
    if (text == "LATIN1" || text == "LATIN-1")
        return Latin1;
    return std::nullopt;
}
struct TextDecoder::Data {
    UConverter *converter = nullptr;
    TextEncoding::Encoding encoding;
    bool error = false, first = true;
    Bytes prelude;
    void initialize(TextEncoding::Encoding selected) {
        UErrorCode code = U_ZERO_ERROR;
        converter = ucnv_open(TextEncoding::nameForEncoding(selected), &code);
        if (U_FAILURE(code)) {
            error = true;
            return;
        }
        ucnv_setToUCallBack(converter, UCNV_TO_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &code);
        error = U_FAILURE(code);
    }
    ~Data() {
        if (converter)
            ucnv_close(converter);
    }
};
TextDecoder::TextDecoder(TextEncoding::Encoding e) : data_(std::make_shared<Data>()) {
    data_->encoding = e;
    if (e != TextEncoding::Utf16 && e != TextEncoding::Utf32)
        data_->initialize(e);
}
String TextDecoder::decode(const Bytes &bytes) {
    auto &d = *data_;
    if (d.error)
        return {};
    auto input = bytes;
    if (!d.converter) {
        d.prelude += bytes;
        const bool utf16 = d.encoding == TextEncoding::Utf16;
        if (d.prelude.size() < (utf16 ? 2 : 4))
            return {};
        const auto detected = TextEncoding::encodingForData(d.prelude);
        auto selected = utf16 ? (std::endian::native == std::endian::little ? TextEncoding::Utf16LE
                                                                            : TextEncoding::Utf16BE)
                              : (std::endian::native == std::endian::little ? TextEncoding::Utf32LE
                                                                            : TextEncoding::Utf32BE);
        if (detected &&
            ((utf16 && (*detected == TextEncoding::Utf16LE || *detected == TextEncoding::Utf16BE)) ||
             (!utf16 && (*detected == TextEncoding::Utf32LE || *detected == TextEncoding::Utf32BE))))
            selected = *detected;
        d.initialize(selected);
        input = std::move(d.prelude);
        d.prelude.clear();
        if (d.error)
            return {};
    }

    std::u16string output(size_t(input.size() + 8), 0);
    UChar *to = output.data();
    const char *from = input.constData();
    UErrorCode code = U_ZERO_ERROR;
    ucnv_toUnicode(d.converter, &to, output.data() + output.size(), &from, input.constData() + input.size(),
                   nullptr, false, &code);
    if (U_FAILURE(code))
        d.error = true;
    output.resize(size_t(to - output.data()));
    if (d.first && !output.empty()) {
        d.first = false;
        if (output[0] == 0xfeff)
            output.erase(0, 1);
    }
    return String(output);
}
bool TextDecoder::hasError() const { return data_->error; }
TextEncoding::FinalizeResult TextDecoder::finalize(char16_t *out, Index capacity) {
    if (!data_->converter) {
        if (data_->prelude.isEmpty() && !data_->error)
            return {};
        data_->error = true;
        return {TextEncoding::FinalizeResultError::IncompleteSequence, 0};
    }
    UChar *to = out;
    const char empty = 0, *from = &empty;
    UErrorCode code = U_ZERO_ERROR;
    ucnv_toUnicode(data_->converter, &to, out + capacity, &from, from, nullptr, true, &code);
    if (U_FAILURE(code))
        data_->error = true;
    return {U_FAILURE(code) ? TextEncoding::FinalizeResultError::IncompleteSequence
                            : TextEncoding::FinalizeResultError::NoError,
            to - out};
}
struct JsonValue::Data {
    nlohmann::json value;
};
JsonValue::JsonValue() : data_(std::make_shared<Data>()) {}
JsonValue::JsonValue(const String &s) : JsonValue() { data_->value = s.toStdString(); }
JsonValue::JsonValue(int n) : JsonValue() { data_->value = n; }
JsonValue::JsonValue(const JsonObject &o) : data_(o.value_.data_) {}
JsonValue::JsonValue(const JsonArray &values) : JsonValue() {
    data_->value = nlohmann::json::array();
    for (const auto &v : values)
        data_->value.push_back(v.data_->value);
}
bool JsonValue::isString() const { return data_->value.is_string(); }
String JsonValue::toString() const {
    if (!isString())
        return {};
    const auto text = data_->value.get<std::string>();
    return String::fromUtf8(text.data(), Index(text.size()));
}
int JsonValue::toInt() const {
    if (!data_->value.is_number())
        return 0;
    auto n = data_->value.get<double>();
    return n >= INT_MIN && n <= INT_MAX && std::trunc(n) == n ? int(n) : 0;
}
JsonObject JsonValue::toObject() const {
    JsonObject object;
    if (data_->value.is_object())
        object.value_.data_ = data_;
    return object;
}
JsonArray JsonValue::toArray() const {
    JsonArray array;
    if (data_->value.is_array())
        for (const auto &v : data_->value) {
            JsonValue copy;
            copy.data_->value = v;
            array.append(copy);
        }
    return array;
}
JsonObject::JsonObject() { value_.data_->value = nlohmann::json::object(); }
JsonObject::JsonObject(std::initializer_list<Pair<String, JsonValue>> list) : JsonObject() {
    for (const auto &[key, value] : list)
        value_.data_->value[key.toStdString()] = value.data_->value;
}
JsonValue JsonObject::value(const String &key) const {
    JsonValue result;
    auto found = value_.data_->value.find(key.toStdString());
    if (found != value_.data_->value.end())
        result.data_->value = *found;
    return result;
}
JsonDocument JsonDocument::fromJson(const Bytes &bytes) {
    JsonDocument result{JsonObject{}};
    auto parsed = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (!parsed.is_discarded())
        result.value_.data_->value = parsed;
    return result;
}
Bytes JsonDocument::toJson() const { return Bytes(value_.data_->value.dump(4) + '\n'); }
}
