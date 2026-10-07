#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"

namespace ltree {
class Filespec {
public:
    bool set(const String &text, String *error = nullptr);
    bool matches(const String &filename) const;
    bool matches(const FileEntry &file, Date today = Date::currentDate()) const;
    bool usesToday() const;
    String text() const { return text_; }
    bool inverted = false;
private:
    struct Rule { Regex name, extension; bool negative; };
    struct Comparison {
        int mask = 0;
        int64 value = 0;
        bool relative = false;
        int position = 0;
    };
    static bool compare(const Vector<Comparison> &, int64 value, Date today);
    String text_ = "*.*";
    Vector<Rule> rules_;
    Vector<Comparison> dates_, sizes_;
};
}
