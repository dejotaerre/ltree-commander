#pragma once
#include <algorithm>
#include <bit>
#include <chrono>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ltree {
using int64 = std::int64_t;
using uint64 = std::uint64_t;
using uint32 = std::uint32_t;
using uint16 = std::uint16_t;
using uint8 = std::uint8_t;
using Index = std::ptrdiff_t;
namespace TextOptions {
enum CaseSensitivity { CaseInsensitive, CaseSensitive };
enum SplitBehavior { KeepEmptyParts, SkipEmptyParts };
enum Initialization { Uninitialized };
}
struct Char {
    char16_t value;
    Char() = default;
    constexpr Char(char16_t c) : value(c) {}
    constexpr Char(char c) : value(static_cast<unsigned char>(c)) {}
    constexpr Char(int c) : value(char16_t(c)) {}
    constexpr Char(unsigned long c) : value(char16_t(c)) {}
    explicit constexpr operator unsigned long() const { return value; }
    static constexpr char16_t Null = 0;
    constexpr uint16 unicode() const { return uint16(value); }
    constexpr auto operator<=>(const Char &) const = default;
    bool isSpace() const;
    bool isDigit() const;
    bool isLetter() const;
    bool isLetterOrNumber() const;
    bool isPrint() const;
    bool isHighSurrogate() const { return value >= 0xd800 && value <= 0xdbff; }
    bool isLowSurrogate() const { return value >= 0xdc00 && value <= 0xdfff; }
    Char toLower() const;
    Char toUpper() const;
};
template <class T> class Vector : public std::vector<T> {
    using Base = std::vector<T>;

  public:
    using Base::Base;
    Vector(const Base &v) : Base(v) {}
    Index size() const { return Index(Base::size()); }
    bool isEmpty() const { return Base::empty(); }
    void append(const T &v) { Base::push_back(v); }
    void append(T &&v) { Base::push_back(std::move(v)); }
    void append(const Vector &v) { Base::insert(Base::end(), v.begin(), v.end()); }
    void prepend(const T &v) { Base::insert(Base::begin(), v); }
    Vector &operator<<(const T &v) {
        append(v);
        return *this;
    }
    friend Vector operator+(Vector a, const Vector &b) {
        a += b;
        return a;
    }
    void removeLast() { Base::pop_back(); }
    Vector &operator+=(const Vector &v) {
        append(v);
        return *this;
    }
    void removeAt(Index i) { Base::erase(Base::begin() + i); }
    Index removeAll(const T &v) {
        auto n = size();
        std::erase(*this, v);
        return n - size();
    }
    void remove(Index i, Index n = 1) { Base::erase(Base::begin() + i, Base::begin() + i + n); }
    void insert(Index i, const T &v) { Base::insert(Base::begin() + i, v); }
    bool contains(const T &v) const { return std::find(Base::begin(), Base::end(), v) != Base::end(); }
    Index indexOf(const T &v) const {
        auto p = std::find(Base::begin(), Base::end(), v);
        return p == Base::end() ? -1 : p - Base::begin();
    }
    T value(Index i, const T &fallback = {}) const { return i >= 0 && i < size() ? (*this)[i] : fallback; }
    typename Base::reference first() { return Base::front(); }
    typename Base::const_reference first() const { return Base::front(); }
    typename Base::reference last() { return Base::back(); }
    typename Base::const_reference last() const { return Base::back(); }
    T takeFirst() {
        T v = std::move(Base::front());
        Base::erase(Base::begin());
        return v;
    }
    T takeLast() {
        T v = std::move(Base::back());
        Base::pop_back();
        return v;
    }
    void fill(const T &v) { std::fill(Base::begin(), Base::end(), v); }
    Vector mid(Index i, Index n = -1) const {
        i = std::clamp(i, Index(0), size());
        n = n < 0 ? size() - i : std::min(n, size() - i);
        return Vector(Base::begin() + i, Base::begin() + i + n);
    }
};
template <class T> class Set : public std::set<T> {
    using Base = std::set<T>;

  public:
    using Base::Base;
    Index size() const { return Index(Base::size()); }
    bool isEmpty() const { return Base::empty(); }
    bool contains(const T &v) const { return Base::find(v) != Base::end(); }
    void remove(const T &v) { Base::erase(v); }
    Set &intersect(const Set &other) {
        for (auto it = Base::begin(); it != Base::end();)
            if (!other.contains(*it))
                it = Base::erase(it);
            else
                ++it;
        return *this;
    }
    Set &unite(const Set &other) {
        Base::insert(other.begin(), other.end());
        return *this;
    }
    Vector<T> values() const { return Vector<T>(Base::begin(), Base::end()); }
};
template <class K, class V> class Map {
    std::map<K, V> data_;
    template <bool Const> class Iterator {
        using Native = std::conditional_t<Const, typename std::map<K, V>::const_iterator,
                                          typename std::map<K, V>::iterator>;
        Native it_;
        friend class Map;
        explicit Iterator(Native it) : it_(it) {}

      public:
        Iterator() = default;
        bool operator==(const Iterator &) const = default;
        Iterator &operator++() {
            ++it_;
            return *this;
        }
        Iterator &operator--() {
            --it_;
            return *this;
        }
        auto &operator*() const { return it_->second; }
        auto *operator->() const { return &it_->second; }
        const K &key() const { return it_->first; }
        auto &value() const { return it_->second; }
    };

  public:
    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;
    Index size() const { return Index(data_.size()); }
    bool isEmpty() const { return data_.empty(); }
    const V &operator[](const K &key) const {
        auto it = data_.find(key);
        if (it != data_.end())
            return it->second;
        static const V empty{};
        return empty;
    }
    V &operator[](const K &key) { return data_[key]; }
    V value(const K &key, const V &fallback = {}) const {
        auto it = data_.find(key);
        return it == data_.end() ? fallback : it->second;
    }
    bool contains(const K &key) const { return data_.contains(key); }
    void insert(const K &key, const V &v) { data_.insert_or_assign(key, v); }
    void remove(const K &key) { data_.erase(key); }
    void clear() { data_.clear(); }
    iterator begin() { return iterator(data_.begin()); }
    iterator end() { return iterator(data_.end()); }
    const_iterator begin() const { return const_iterator(data_.begin()); }
    const_iterator end() const { return const_iterator(data_.end()); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }
    iterator find(const K &k) { return iterator(data_.find(k)); }
    const_iterator constFind(const K &k) const { return const_iterator(data_.find(k)); }
    iterator erase(iterator it) { return iterator(data_.erase(it.it_)); }
    Vector<K> keys() const {
        Vector<K> v;
        for (const auto &[k, x] : data_)
            v.append(k);
        return v;
    }
};
template <class A, class B> using Pair = std::pair<A, B>;
template <class A, class B> auto makePair(A a, B b) { return std::make_pair(a, b); }
class Bytes;
class String;
class StringList;
class StringView;
class Regex;
class String {
    std::basic_string<Char> data_;

  public:
    String() = default;
    String(const char *s);
    String(const std::string &s) : String(s.c_str()) {}
    String(Char c) : data_(1, c) {}
    String(char c) : String(Char(c)) {}
    String(Index count, Char c) : data_(size_t(std::max(Index(0), count)), c) {}
    explicit String(const std::u16string &s);
    Index size() const { return Index(data_.size()); }
    bool isEmpty() const { return data_.empty(); }
    void clear() { data_.clear(); }
    void reserve(Index n) { data_.reserve(size_t(n)); }
    Char &operator[](Index i) { return data_[size_t(i)]; }
    Char operator[](Index i) const { return data_[size_t(i)]; }
    Char at(Index i) const { return (*this)[i]; }
    Char front() const { return data_.front(); }
    Char back() const { return data_.back(); }
    auto begin() { return data_.begin(); }
    auto end() { return data_.end(); }
    auto begin() const { return data_.begin(); }
    auto end() const { return data_.end(); }
    auto cbegin() const { return data_.cbegin(); }
    auto cend() const { return data_.cend(); }
    std::u16string std16() const;
    Bytes toUtf8() const;
    Bytes toLatin1() const;
    Bytes toLocal8Bit() const;
    std::string toStdString() const;
    Vector<char32_t> toUcs4() const;
    static String fromUtf8(const char *s, Index n = -1);
    static String fromUtf8(const Bytes &s);
    static String fromLatin1(const char *s, Index n = -1);
    static String fromLatin1(const Bytes &s);
    static String fromLocal8Bit(const char *s, Index n = -1) { return fromUtf8(s, n); }
    static String fromLocal8Bit(const Bytes &s);
    static String fromUcs4(const char32_t *s, Index n);
    static String number(int64 n, int base = 10);
    static String number(uint64 n, int base = 10);
    static String number(int n, int base = 10) { return number(int64(n), base); }
    static String number(unsigned n, int base = 10) { return number(uint64(n), base); }
    static String number(double n, char format = 'g', int precision = 6);
    String mid(Index i, Index n = -1) const;
    String left(Index n) const;
    String right(Index n) const;
    String chopped(Index n) const { return left(std::max(Index(0), size() - n)); }
    void chop(Index n) { *this = chopped(n); }
    void truncate(Index n) { *this = left(n); }
    String trimmed() const;
    String simplified() const;
    String toLower() const;
    String toUpper() const;
    String toCaseFolded() const;
    static int compare(const String &a, const String &b,
                       TextOptions::CaseSensitivity cs = TextOptions::CaseSensitive);
    int compare(const String &b, TextOptions::CaseSensitivity cs = TextOptions::CaseSensitive) const {
        return compare(*this, b, cs);
    }
    Index indexOf(const String &s, Index from = 0,
                  TextOptions::CaseSensitivity cs = TextOptions::CaseSensitive) const;
    Index lastIndexOf(const String &s, Index from = -1,
                      TextOptions::CaseSensitivity cs = TextOptions::CaseSensitive) const;
    bool contains(const String &s, TextOptions::CaseSensitivity cs = TextOptions::CaseSensitive) const {
        return indexOf(s, 0, cs) >= 0;
    }
    bool startsWith(const String &s, TextOptions::CaseSensitivity cs = TextOptions::CaseSensitive) const;
    bool endsWith(const String &s, TextOptions::CaseSensitivity cs = TextOptions::CaseSensitive) const;
    StringList split(const String &separator,
                     TextOptions::SplitBehavior behavior = TextOptions::KeepEmptyParts) const;
    StringList split(const Regex &separator,
                     TextOptions::SplitBehavior behavior = TextOptions::KeepEmptyParts) const;
    String &replace(const String &from, const String &to);
    String &replace(const Regex &from, const String &to);
    String &remove(Char c) { return replace(String(c), {}); }
    String &remove(const Regex &r) { return replace(r, {}); }
    String &remove(Index i, Index n) {
        data_.erase(size_t(i), size_t(n));
        return *this;
    }
    String &insert(Index i, const String &s) {
        data_.insert(size_t(i), s.data_);
        return *this;
    }
    String &append(const String &s) {
        data_ += s.data_;
        return *this;
    }
    String &prepend(const String &s) {
        data_.insert(0, s.data_);
        return *this;
    }
    String &operator+=(const String &s) { return append(s); }
    String leftJustified(Index width, Char fill = ' ', bool truncate = false) const;
    String rightJustified(Index width, Char fill = ' ', bool truncate = false) const;
    String arg(const String &value, Index width = 0, Char fill = ' ') const;
    String arg(int64 n, int width = 0, int base = 10, Char fill = ' ') const {
        return arg(number(n, base), width, fill);
    }
    String arg(uint64 n, int width = 0, int base = 10, Char fill = ' ') const {
        return arg(number(n, base), width, fill);
    }
    String arg(int n, int width = 0, int base = 10, Char fill = ' ') const {
        return arg(int64(n), width, base, fill);
    }
    String arg(unsigned n, int width = 0, int base = 10, Char fill = ' ') const {
        return arg(uint64(n), width, base, fill);
    }
    template <class T>
        requires std::is_integral_v<T>
    String arg(T n, int width, int base, char fill) const {
        return arg(n, width, base, Char(fill));
    }
    String arg(double n, int width = 0, char format = 'g', int precision = 6, Char fill = ' ') const {
        return arg(number(n, format, precision), width, fill);
    }
    String argMany(const Vector<String> &values) const;
    String arg(const String &a, const String &b) const { return argMany({a, b}); }
    template <class... S>
        requires(sizeof...(S) >= 3 && ((std::is_convertible_v<S, String> && !std::is_arithmetic_v<S> &&
                                        !std::is_same_v<std::remove_cvref_t<S>, Char>) &&
                                       ...))
    String arg(const S &...values) const {
        return argMany({String(values)...});
    }
    unsigned toUInt(bool *ok = nullptr, int base = 10) const;
    int64 toLongLong(bool *ok = nullptr, int base = 10) const;
    uint64 toULongLong(bool *ok = nullptr, int base = 10) const;
    int toInt(bool *ok = nullptr, int base = 10) const;
    double toDouble(bool *ok = nullptr) const;
    bool operator==(const String &s) const { return data_ == s.data_; }
    auto operator<=>(const String &s) const { return data_ <=> s.data_; }
    friend String operator+(String a, const String &b) {
        a += b;
        return a;
    }
    friend class StringView;
};
class StringView {
    const String *text_;
    Index start_, length_;

  public:
    StringView(const String &s) : text_(&s), start_(0), length_(s.size()) {}
    Index size() const { return length_; }
    Char operator[](Index i) const { return (*text_)[start_ + i]; }
    StringView mid(Index i, Index n = -1) const {
        auto result = *this;
        result.start_ += i;
        result.length_ = n < 0 ? length_ - i : n;
        return result;
    }
    const char16_t *data() const {
        static_assert(sizeof(Char) == sizeof(char16_t) && alignof(Char) == alignof(char16_t));
        return reinterpret_cast<const char16_t *>(text_->data_.data() + start_);
    }
    String toString() const { return text_->mid(start_, length_); }
};
class StringList : public Vector<String> {
  public:
    using Vector::Vector;
    StringList(const Vector<String> &v) : Vector(v) {}
    String join(const String &separator) const {
        String out;
        for (Index i = 0; i < size(); ++i) {
            if (i)
                out += separator;
            out += (*this)[i];
        }
        return out;
    }
};
class Bytes {
    std::string data_;

  public:
    static Bytes number(int64 n, int base = 10) { return String::number(n, base).toLatin1(); }
    Bytes(char c) : data_(1, c) {}
    Bytes() = default;
    Bytes(const char *s) : data_(s ? s : "") {}
    Bytes(std::string s) : data_(std::move(s)) {}
    Bytes(const char *s, Index n) : data_(s, size_t(n)) {}
    Bytes(Index n, char fill) : data_(size_t(n), fill) {}
    Bytes(Index n, TextOptions::Initialization) : Bytes(n, char(0)) {}
    Index size() const { return Index(data_.size()); }
    bool isEmpty() const { return data_.empty(); }
    const char *constData() const { return data_.c_str(); }
    const char *data() const { return data_.data(); }
    char *data() { return data_.data(); }
    char &operator[](Index i) { return data_[size_t(i)]; }
    char operator[](Index i) const { return data_[size_t(i)]; }
    auto begin() { return data_.begin(); }
    auto end() { return data_.end(); }
    auto begin() const { return data_.begin(); }
    auto end() const { return data_.end(); }
    void clear() { data_.clear(); }
    void resize(Index n) { data_.resize(size_t(n)); }
    void reserve(Index n) { data_.reserve(size_t(n)); }
    char back() const { return data_.back(); }
    Bytes &append(const Bytes &b) {
        data_ += b.data_;
        return *this;
    }
    Bytes &append(char c) {
        data_ += c;
        return *this;
    }
    Bytes &append(const char *s, Index n) {
        data_.append(s, size_t(n));
        return *this;
    }
    Bytes &operator+=(const Bytes &b) { return append(b); }
    Bytes &operator+=(char c) { return append(c); }
    Bytes mid(Index i, Index n = -1) const;
    Bytes left(Index n) const { return mid(0, n); }
    Bytes right(Index n) const { return mid(std::max(Index(0), size() - n)); }
    void remove(Index i, Index n) { data_.erase(size_t(i), size_t(n)); }
    void chop(Index n) { resize(std::max(Index(0), size() - n)); }
    Bytes trimmed() const;
    Bytes simplified() const;
    Bytes toLower() const;
    Bytes toHex(char separator = 0) const;
    static Bytes fromHex(const Bytes &s);
    Index indexOf(const Bytes &s, Index from = 0) const;
    Index indexOf(char c, Index from = 0) const { return indexOf(Bytes(&c, 1), from); }
    bool contains(const Bytes &s) const { return indexOf(s) >= 0; }
    bool startsWith(const Bytes &s) const { return data_.starts_with(s.data_); }
    bool endsWith(const Bytes &s) const { return data_.ends_with(s.data_); }
    Bytes &replace(Index at, Index count, const Bytes &to) {
        data_.replace(size_t(at), size_t(count), to.data_);
        return *this;
    }
    Bytes &replace(const Bytes &from, const Bytes &to);
    Index lastIndexOf(const Bytes &s, Index from = -1) const;
    Vector<Bytes> split(char separator) const;
    int toInt(bool *ok = nullptr, int base = 10) const { return String::fromLatin1(*this).toInt(ok, base); }
    int64 toLongLong(bool *ok = nullptr, int base = 10) const {
        return String::fromLatin1(*this).toLongLong(ok, base);
    }
    double toDouble(bool *ok = nullptr) const { return String::fromLatin1(*this).toDouble(ok); }
    friend bool operator==(const Bytes &, const Bytes &) = default;
    auto operator<=>(const Bytes &) const = default;
    friend Bytes operator+(Bytes a, const Bytes &b) {
        a += b;
        return a;
    }
};
class RegexMatch {
    String text_;
    std::optional<StringView> view_;
    Vector<Pair<Index, Index>> captures_;
    bool valid_ = true;
    friend class Regex;

  public:
    bool hasMatch() const { return !captures_.isEmpty(); }
    bool isValid() const { return valid_; }
    Index capturedStart(int n = 0) const { return n >= 0 && n < captures_.size() ? captures_[n].first : -1; }
    Index capturedLength(int n = 0) const { return n >= 0 && n < captures_.size() ? captures_[n].second : 0; }
    Index capturedEnd(int n = 0) const { return capturedStart(n) + capturedLength(n); }
    String captured(int n = 0) const {
        return capturedStart(n) < 0 ? String{}
               : view_              ? view_->mid(capturedStart(n), capturedLength(n)).toString()
                                    : text_.mid(capturedStart(n), capturedLength(n));
    }
};
class Regex {
    struct Data;
    std::shared_ptr<Data> data_;

  public:
    enum PatternOption {
        NoPatternOption = 0,
        CaseInsensitiveOption = 1,
        DotMatchesEverythingOption = 2,
        UseUnicodePropertiesOption = 4,
        MultilineOption = 8
    };
    Regex() = default;
    Regex(const String &pattern, int options = 0);
    bool isValid() const;
    String errorString() const;
    Index patternErrorOffset() const;
    RegexMatch match(const String &s, Index offset = 0) const;
    RegexMatch matchView(StringView s, Index offset = 0) const;
    static String escape(const String &s);
};
class Date {
    int year_ = 0, month_ = 0, day_ = 0;

  public:
    Date() = default;
    Date(int y, int m, int d) : year_(y), month_(m), day_(d) {}
    bool isValid() const;
    int year() const { return year_; }
    int month() const { return month_; }
    int day() const { return day_; }
    int64 toJulianDay() const;
    static Date fromJulianDay(int64 day);
    static Date currentDate();
    static Date fromString(const String &s, const String &format);
    Date addDays(int64 n) const;
    String toString(const String &format = "yyyy-MM-dd") const;
    auto operator<=>(const Date &) const = default;
};
class Time {
    int hour_ = -1, minute_ = -1, second_ = -1;

  public:
    Time() = default;
    Time(int h, int m, int s = 0) : hour_(h), minute_(m), second_(s) {}
    bool isValid() const {
        return hour_ >= 0 && hour_ < 24 && minute_ >= 0 && minute_ < 60 && second_ >= 0 && second_ < 60;
    }
    int hour() const { return hour_; }
    int minute() const { return minute_; }
    int second() const { return second_; }
    static Time fromString(const String &s, const String &format);
    String toString(const String &format = "HH:mm:ss") const;
    auto operator<=>(const Time &) const = default;
};
class DateTime {
    int64 milliseconds_ = 0;
    bool valid_ = false;

  public:
    DateTime() = default;
    DateTime(Date date, Time time);
    static int64 currentMSecsSinceEpoch() { return currentDateTime().toMSecsSinceEpoch(); }
    static DateTime fromSecsSinceEpoch(int64 n);
    static DateTime fromMSecsSinceEpoch(int64 n);
    static DateTime currentDateTime();
    bool isValid() const { return valid_; }
    int64 toSecsSinceEpoch() const;
    int64 toMSecsSinceEpoch() const { return milliseconds_; }
    Date date() const;
    Time time() const;
    DateTime toLocalTime() const { return *this; }
    DateTime addSecs(int64 n) const;
    DateTime addDays(int64 n) const;
    DateTime addMonths(int n) const;
    DateTime addYears(int n) const;
    String toString(const String &format) const;
    bool operator==(const DateTime &) const = default;
    auto operator<=>(const DateTime &other) const { return milliseconds_ <=> other.milliseconds_; }
};
class IO {
  public:
    enum OpenMode { ReadOnly = 1, WriteOnly = 2, ReadWrite = 3, Append = 4, Truncate = 8, Text = 16 };
};
class File {
  protected:
    String name_, error_;
    int fd_ = -1, errorCode_ = 0;
    bool own_ = true;

  public:
    enum FileError { NoError, ReadError, WriteError, OpenError };
    enum Permission { ReadOwner = 0400, WriteOwner = 0200 };
    explicit File(const String &name = {}) : name_(name) {}
    virtual ~File();
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    bool open(int mode);
    bool open(int descriptor, int mode);
    void close();
    bool isOpen() const { return fd_ >= 0; }
    int handle() const { return fd_; }
    String fileName() const { return name_; }
    void setFileName(const String &s) { name_ = s; }
    int64 size() const;
    int64 pos() const;
    bool seek(int64 offset);
    bool atEnd() const;
    Bytes read(int64 n);
    int64 read(char *data, int64 n);
    Bytes readAll();
    Bytes readLine(int64 limit = 0);
    Bytes peek(int64 n);
    int64 write(const Bytes &s);
    int64 write(const char *s, int64 n);
    bool flush();
    bool setPermissions(int modes);
    int error() const { return errorCode_; }
    String errorString() const { return error_; }
    bool exists() const { return exists(name_); }
    bool remove();
    static bool remove(const String &s);
    static bool exists(const String &s);
    static Bytes encodeName(const String &s) { return s.toUtf8(); }
    static String decodeName(const Bytes &s) { return String::fromUtf8(s); }
    static String decodeName(const char *s) { return String::fromUtf8(s); }
    bool moveToTrash() {
        close();
        return moveToTrash(name_);
    }
    static bool supportsMoveToTrash() { return true; }
    static bool moveToTrash(const String &s, String *target = nullptr);
};
class SaveFile : public File {
    String target_;
    bool committed_ = false;

  public:
    explicit SaveFile(const String &target) : File(target), target_(target) {}
    ~SaveFile() override;
    bool open(int mode);
    bool commit();
    void cancelWriting() { close(); }
};
class FileInfo {
    String path_;

  public:
    explicit FileInfo(const String &path = {}) : path_(path) {}
    explicit FileInfo(const File &f) : path_(f.fileName()) {}
    bool exists() const;
    static bool exists(const String &s) { return FileInfo(s).exists(); }
    bool isFile() const;
    bool isDir() const;
    bool isSymLink() const;
    bool isReadable() const;
    bool isWritable() const;
    bool isExecutable() const;
    bool isHidden() const;
    String path() const;
    int64 size() const;
    String fileName() const;
    String suffix() const;
    String baseName() const;
    String completeBaseName() const;
    String absoluteFilePath() const;
    String absolutePath() const;
    String canonicalFilePath() const;
    String canonicalPath() const;
    String symLinkTarget() const;
    DateTime lastModified() const;
    void refresh() {}
};
class DirectoryPath {
    String path_;

  public:
    enum Filter {
        AllEntries = 1,
        Hidden = 2,
        System = 4,
        NoDotAndDotDot = 8,
        Dirs = 16,
        NoSymLinks = 32,
        Readable = 64
    };
    enum SortFlag { Unsorted, Name };
    explicit DirectoryPath(const String &s = {}) : path_(s.isEmpty() ? currentPath() : s) {}
    static String cleanPath(const String &s);
    static bool isAbsolutePath(const String &s);
    static String homePath();
    static String currentPath();
    static String tempPath();
    String filePath(const String &s) const;
    String absolutePath() const;
    String absoluteFilePath(const String &s) const;
    String relativeFilePath(const String &s) const;
    bool exists() const;
    bool exists(const String &s) const;
    bool mkpath(const String &s) const;
    bool mkdir(const String &s) const;
    bool rmdir(const String &s) const;
    Vector<FileInfo> entryInfoList(int filters = AllEntries, int order = Unsorted) const;
    StringList entryList(int filters = AllEntries, int order = Unsorted) const;
};
class TemporaryDirectory {
    String path_;

  public:
    TemporaryDirectory();
    ~TemporaryDirectory();
    bool isValid() const { return !path_.isEmpty(); }
    String path() const { return path_; }
    String filePath(const String &s) const { return DirectoryPath(path_).filePath(s); }
};
class ElapsedTimer {
    std::chrono::steady_clock::time_point start_;

  public:
    void start() { start_ = std::chrono::steady_clock::now(); }
    int64 elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                     start_)
            .count();
    }
    int64 restart() {
        auto result = elapsed();
        start();
        return result;
    }
};
class Paths {
  public:
    enum Location {
        ConfigLocation,
        AppConfigLocation,
        GenericConfigLocation,
        HomeLocation,
        TempLocation,
        RuntimeLocation
    };
    static String writableLocation(Location type);
    static String findExecutable(const String &name);
};
class ProcessEnvironment {
    friend class Process;
    Map<String, String> values_;

  public:
    static ProcessEnvironment systemEnvironment();
    void insert(const String &key, const String &value) { values_.insert(key, value); }
};
class Process {
    struct Data;
    std::unique_ptr<Data> data_;

  public:
    enum State { NotRunning, Starting, Running };
    enum ExitStatus { NormalExit, CrashExit };
    enum ProcessError { FailedToStart, Crashed, Timedout, UnknownError };
    enum ChannelMode { SeparateChannels, ForwardedChannels, MergedChannels };
    enum InputChannelMode { ManagedInputChannel, ForwardedInputChannel };
    enum class UnixProcessFlag { ResetSignalHandlers };
    Process();
    ~Process();
    void setWorkingDirectory(const String &s);
    void setProcessEnvironment(const ProcessEnvironment &e);
    void setProcessChannelMode(ChannelMode mode);
    void setInputChannelMode(InputChannelMode mode);
    void setUnixProcessParameters(UnixProcessFlag) {}
    void setChildProcessModifier(std::function<void()> modifier);
    void start(const String &program, const StringList &args = {});
    bool waitForStarted(int timeout = 30000);
    bool waitForFinished(int timeout = 30000);
    State state() const;
    ExitStatus exitStatus() const;
    int exitCode() const;
    ProcessError error() const;
    String errorString() const;
    void terminate();
    void kill();
    int64 write(const Bytes &data);
    void closeWriteChannel();
    Bytes readAllStandardOutput();
    Bytes readAllStandardError();
    static StringList splitCommand(const String &command);
};
class Locale {
  public:
    enum Language { Spanish };
    enum Country { Uruguay };
    Locale(Language, Country) {}
    String toString(uint64 n) const;
};
class Uuid {
    String value_;

  public:
    enum Format { WithoutBraces, Id128 };
    static Uuid createUuid();
    String toString(Format format) const;
};
class StorageInfo {
    String path_, mount_, source_, type_;
    int64 total_ = -1, free_ = -1, available_ = -1;

  public:
    explicit StorageInfo(const String &path);
    String rootPath() const { return mount_; }
    Bytes device() const { return source_.toUtf8(); }
    Bytes fileSystemType() const { return type_.toUtf8(); }
    bool isValid() const { return total_ >= 0; }
    bool isReady() const { return isValid(); }
    int64 bytesTotal() const { return total_; }
    int64 bytesFree() const { return free_; }
    int64 bytesAvailable() const { return available_; }
};
class SystemInfo {
  public:
    static String machineHostName();
};
class TextEncoding {
  public:
    enum Encoding { Utf8, Utf16, Utf16LE, Utf16BE, Utf32, Utf32LE, Utf32BE, Latin1, System };
    enum class FinalizeResultError { NoError, IncompleteSequence };
    struct FinalizeResult {
        FinalizeResultError error = FinalizeResultError::NoError;
        Index size = 0;
    };
    static std::optional<Encoding> encodingForData(const Bytes &bytes);
    static std::optional<Encoding> encodingForName(const Bytes &name);
    static const char *nameForEncoding(Encoding e);
};
class TextDecoder {
    struct Data;
    std::shared_ptr<Data> data_;

  public:
    explicit TextDecoder(TextEncoding::Encoding e);
    String decode(const Bytes &s);
    String operator()(const Bytes &s) { return decode(s); }
    bool hasError() const;
    TextEncoding::FinalizeResult finalize(char16_t *out, Index capacity);
};
class JsonObject;
class JsonArray;
class JsonValue {
    struct Data;
    std::shared_ptr<Data> data_;
    friend class JsonDocument;
    friend class JsonObject;
    friend class JsonArray;

  public:
    JsonValue();
    JsonValue(const String &s);
    JsonValue(const char *s) : JsonValue(String(s)) {}
    JsonValue(int n);
    JsonValue(const JsonObject &s);
    JsonValue(const JsonArray &s);
    bool isString() const;
    String toString() const;
    int toInt() const;
    JsonObject toObject() const;
    JsonArray toArray() const;
};
class JsonObject {
    JsonValue value_;
    friend class JsonValue;
    friend class JsonDocument;

  public:
    JsonObject();
    JsonObject(std::initializer_list<Pair<String, JsonValue>> list);
    JsonValue value(const String &key) const;
};
class JsonArray : public Vector<JsonValue> {
  public:
    using Vector::Vector;
};
class JsonDocument {
    JsonValue value_;

  public:
    explicit JsonDocument(const JsonObject &v) : value_(v) {}
    static JsonDocument fromJson(const Bytes &s);
    JsonObject object() const { return value_.toObject(); }
    Bytes toJson() const;
};
class TerminalApplication {
  public:
    static void processEvents() {}
    static String applicationVersion() { return LTREE_VERSION; }
};
using Mutex = std::mutex;
class MutexLocker {
    Mutex *mutex_;

  public:
    explicit MutexLocker(Mutex *m) : mutex_(m) { m->lock(); }
    ~MutexLocker() { mutex_->unlock(); }
};
template <class T> class Future {
    std::shared_future<T> data_;

  public:
    Future() = default;
    explicit Future(std::shared_future<T> d) : data_(std::move(d)) {}
    bool isRunning() const {
        return data_.valid() && data_.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    }
    bool isFinished() const { return data_.valid() && !isRunning(); }
    void waitForFinished() const {
        if (data_.valid())
            data_.wait();
    }
    T result() const { return data_.get(); }
};
template <class F> auto runAsync(F &&function) {
    using T = std::invoke_result_t<F>;
    return Future<T>(std::async(std::launch::async, std::forward<F>(function)).share());
}
inline String environment(const char *name, const String &fallback = {}) {
    const auto s = std::getenv(name);
    return s ? String::fromUtf8(s) : fallback;
}
inline bool environmentEmpty(const char *name) { return environment(name).isEmpty(); }
template <class T> T fromBigEndian(const unsigned char *data) {
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        value = (value << 8) | data[i];
    return value;
}
}
