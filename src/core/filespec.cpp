#include "platform/platform.h"
#include "core/filespec.h"

namespace ltree {
namespace {
Pair<String, String> split(String text)
{
    int dot = int(text.lastIndexOf('.'));
    if (dot == text.size() - 1 && text.left(dot).lastIndexOf('.') > 0) {
        text.chop(1);
        dot = int(text.lastIndexOf('.'));
    }
    return dot > 0 ? makePair(text.left(dot), text.mid(dot + 1)) : makePair(text, String{});
}

String expression(const String &part, String &error)
{
    String result = "\\A";
    int trailing = int(part.size());
    while (trailing > 0 && part[trailing - 1] == '?') --trailing;
    if (!part.isEmpty() && trailing == 0) return result + String(".{1,%1}\\z").arg(part.size());
    for (int i = 0; i < part.size(); ++i) {
        const Char c = part[i];
        if (c == '*') result += ".*";
        else if (c == '?') result += i >= trailing ? ".?" : ".";
        else if (c == Char(0x25c4) || c == Char(0x25c0)) {
            String group;
            while (++i < part.size() && part[i] != Char(0x25ba) && part[i] != Char(0x25b6)) {
                if (part[i] == '-') group += '-';
                else group += Regex::escape(String(part[i]));
            }
            if (i == part.size() || group.isEmpty()) { error = "Incomplete or empty character group"; return {}; }
            result += '[' + group + ']';
        } else result += Regex::escape(String(c));
    }
    return result + "\\z";
}
}

bool Filespec::set(const String &input, String *error)
{
    String text = input.trimmed();
    if (text.isEmpty()) text = "*.*";
    String body = text;
    bool quoted = false;
    for (int i = 0; i < body.size(); ++i) {
        if (body[i] == '"') quoted = !quoted;
        if (body[i] == ':' && !quoted) { body = body.mid(i + 1); break; }
    }
    Vector<Rule> rules;
    Vector<Comparison> dates, sizes;
    String failure, token;
    bool negative = false, started = false, literal = false;
    int position = 0;
    quoted = false;
    const auto append = [&] {
        if (!started) return;
        ++position;
        if (token.isEmpty()) { failure = "Missing pattern after exclusion"; return; }
        if (!literal && (String("<>=").contains(token[0]) || token.compare("TODAY", TextOptions::CaseInsensitive) == 0 || token.startsWith("TODAY-", TextOptions::CaseInsensitive))) {
            Comparison rule;
            rule.position = position;
            int offset = 0;
            while (offset < token.size() && String("<>=").contains(token[offset])) ++offset;
            const String op = token.left(offset);
            if (op == "<") rule.mask = 1;
            else if (op == "=" || op.isEmpty()) rule.mask = 2;
            else if (op == ">") rule.mask = 4;
            else if (op == "<=" || op == "=<") rule.mask = 3;
            else if (op == ">=" || op == "=>") rule.mask = 6;
            else if (op == "<>") rule.mask = 5;
            else failure = "Invalid date/size operator";
            if (negative) rule.mask ^= 7;
            const String value = token.mid(offset);
            const bool size = value.startsWith('s', TextOptions::CaseInsensitive);
            if (size) {
                bool ok = false;
                rule.value = value.mid(1).toLongLong(&ok);
                if (!ok || !Regex("\\A[0-9]+\\z").match(value.mid(1)).hasMatch())
                    failure = "Invalid size: use whole bytes without separators";
            } else if (value.startsWith("TODAY", TextOptions::CaseInsensitive)) {
                const auto relative = Regex("\\ATODAY(?:-([0-9]+))?\\z", Regex::CaseInsensitiveOption).match(value);
                bool ok = true;
                rule.value = relative.captured(1).isEmpty() ? 0 : relative.captured(1).toLongLong(&ok);
                rule.relative = true;
                if (!relative.hasMatch() || !ok || rule.value > 3652058) failure = "Use TODAY or TODAY-days";
            } else {
                const auto date = Regex("\\A([0-9]{1,2})([./-])([0-9]{1,2})\\2([0-9]{2}|[0-9]{4})\\z").match(value);
                int year = date.captured(4).toInt();
                // Perfil inicial: formato del manual; el corte de siglo queda documentado.
                if (date.captured(4).size() == 2) year += year < 30 ? 2000 : 1900;
                const Date parsed(year, date.captured(1).toInt(), date.captured(3).toInt());
                if (!date.hasMatch() || !parsed.isValid()) failure = "Invalid date: use MM-DD-YYYY or TODAY";
                rule.value = parsed.toJulianDay();
            }
            if (size) sizes.append(rule); else dates.append(rule);
            token.clear(); negative = false; started = false; literal = false;
            return;
        }
        if (token.contains("*?")) { failure = "The *? combination is invalid in Filespec"; return; }
        const auto parts = split(token);
        const String name = expression(parts.first, failure), ext = expression(parts.second, failure);
        Regex a(name, Regex::CaseInsensitiveOption | Regex::DotMatchesEverythingOption);
        Regex b(ext, Regex::CaseInsensitiveOption | Regex::DotMatchesEverythingOption);
        if (!a.isValid() || !b.isValid()) failure = "Invalid character range";
        rules.append({a, b, negative});
        token.clear(); negative = false; started = false; literal = false;
    };
    for (Char c : body) {
        if (c == '"') { quoted = !quoted; started = true; if (token.isEmpty()) literal = true; continue; }
        if (!quoted && (c.isSpace() || c == ',' || c == ';')) { append(); continue; }
        if (!quoted && !started && c == '-') { negative = true; started = true; continue; }
        started = true;
        token += c;
    }
    append();
    if (quoted) failure = "Missing closing quotation mark";
    if (rules.isEmpty() && dates.isEmpty() && sizes.isEmpty() && failure.isEmpty()) failure = "Missing file specification";
    if (!failure.isEmpty()) { if (error) *error = failure; return false; }
    rules_ = rules;
    dates_ = dates;
    sizes_ = sizes;
    text_ = text;
    inverted = false;
    return true;
}

bool Filespec::matches(const String &filename) const
{
    FileEntry file;
    file.name = filename;
    return matches(file);
}

bool Filespec::usesToday() const
{
    for (const auto &rule : dates_) if (rule.relative) return true;
    return false;
}

bool Filespec::compare(const Vector<Comparison> &rules, int64 value, Date today)
{
    const auto threshold = [today](const Comparison &rule) {
        return rule.relative ? today.addDays(-rule.value).toJulianDay() : rule.value;
    };
    const auto check = [value, &threshold](const Comparison &rule) {
        const int64 target = threshold(rule);
        return (rule.mask & (value < target ? 1 : value == target ? 2 : 4)) != 0;
    };
    bool hasPositive = false, included = false;
    for (int i = 0; i < rules.size(); ++i) {
        const auto &rule = rules[i];
        if (rule.mask == 5) { if (!check(rule)) return false; continue; }
        hasPositive = true;
        bool matches = check(rule);
        if ((rule.mask == 4 || rule.mask == 6) && i + 1 < rules.size()) {
            const auto &next = rules[i + 1];
            if ((next.mask == 1 || next.mask == 3) && next.position == rule.position + 1 && threshold(next) > threshold(rule)) {
                matches &= check(next);
                ++i;
            }
        }
        included |= matches;
    }
    return !hasPositive || included;
}

bool Filespec::matches(const FileEntry &file, Date today) const
{
    const auto parts = split(file.name);
    bool positive = false, included = false, excluded = false;
    for (const auto &rule : rules_) {
        const bool match = rule.name.match(parts.first).hasMatch() && rule.extension.match(parts.second).hasMatch();
        if (rule.negative) excluded |= match;
        else { positive = true; included |= match; }
    }
    const bool match = (!positive || included) && !excluded && compare(sizes_, file.size, today) &&
        (dates_.isEmpty() || (file.modified.isValid() && compare(dates_, file.modified.toLocalTime().date().toJulianDay(), today)));
    return inverted ? !match : match;
}
}
