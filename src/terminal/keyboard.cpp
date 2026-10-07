#include "platform/platform.h"
#include "terminal/keyboard.h"
#include <cstdio>
#include <cwchar>
#define NCURSES_NOMACROS 1
#include <curses.h>

namespace ltree {
namespace {
bool scalar(int n) { return n >= 0 && n <= 0x10ffff && !(n >= 0xd800 && n <= 0xdfff); }
void output(const char *s) { std::fputs(s, stdout);std::fflush(stdout); }
int function(int n) { return n >= 1 && n <= 12 ? KEY_F(n) : 0; }
int functional(const KeyboardEvent &e) {
    if (e.final == 'A') return KEY_UP;
    if (e.final == 'B') return KEY_DOWN;
    if (e.final == 'C') return KEY_RIGHT;
    if (e.final == 'D') return KEY_LEFT;
    if (e.final == 'H') return KEY_HOME;
    if (e.final == 'F') return KEY_END;
    if (e.final == 'P') return KEY_F(1);
    if (e.final == 'Q') return KEY_F(2);
    if (e.final == 'R') return KEY_F(3);
    if (e.final == 'S') return KEY_F(4);
    if (e.final == '~') {
        switch (e.code) {
        case 2: return KEY_IC;case 3: return KEY_DC;
        case 5: return KEY_PPAGE;case 6: return KEY_NPAGE;
        case 1: case 7: return KEY_HOME;case 4: case 8: return KEY_END;
        case 11: return KEY_F(1);case 12: return KEY_F(2);case 13: return KEY_F(3);
        case 14: return KEY_F(4);case 15: return KEY_F(5);case 17: return KEY_F(6);
        case 18: return KEY_F(7);case 19: return KEY_F(8);case 20: return KEY_F(9);
        case 21: return KEY_F(10);case 23: return KEY_F(11);case 24: return KEY_F(12);
        default: return 0;
        }
    }
    if (e.final != 'u') return 0;
    if (e.code >= 57364 && e.code <= 57375) return function(e.code - 57363);
    if (e.code >= 57399 && e.code <= 57408) return '0' + e.code - 57399;
    switch (e.code) {
    case 57409: return '.';case 57410: return '/';case 57411: return '*';
    case 57412: return '-';case 57413: return '+';case 57414: return 13;
    case 57415: return '=';case 57416: return ',';
    case 57417: return KEY_LEFT;case 57418: return KEY_RIGHT;
    case 57419: return KEY_UP;case 57420: return KEY_DOWN;
    case 57421: return KEY_PPAGE;case 57422: return KEY_NPAGE;
    case 57423: return KEY_HOME;case 57424: return KEY_END;
    case 57425: return KEY_IC;case 57426: return KEY_DC;
    default: return e.code >= 57344 && e.code <= 63743 ? 0 : e.code;
    }
}
TerminalInput normalize(int key, int mods, bool literal) {
    const bool shift = mods & 1, alt = mods & 2, control = mods & 4;
    if (key >= KEY_F(1) && key <= KEY_F(12) && !literal)
        key = (control ? 0x2100 : alt ? 0x2500 : shift ? 0x2200 : KEY_F(0)) + key - KEY_F(0);
    else if (!literal) {
        if (control) {
            if (key == KEY_UP) key = 0x2301;else if (key == KEY_DOWN) key = 0x2302;
            else if (key == KEY_LEFT) key = 0x2303;else if (key == KEY_RIGHT) key = 0x2304;
            else if (key == KEY_HOME) key = 0x2305;else if (key == KEY_END) key = 0x2306;
            else if (key == KEY_DC) key = 0x2603;else if (key == KEY_IC) key = 0x2606;
        } else if (shift) {
            if (key == KEY_UP) key = KEY_SR;else if (key == KEY_DOWN) key = KEY_SF;
            else if (key == KEY_PPAGE) key = KEY_SPREVIOUS;else if (key == KEY_NPAGE) key = KEY_SNEXT;
            else if (key == KEY_HOME) key = KEY_SHOME;else if (key == KEY_END) key = KEY_SEND;
            else if (key == KEY_LEFT) key = 0x2401;else if (key == KEY_RIGHT) key = 0x2402;
            else if (key == KEY_IC) key = 0x2607;
        }
    }
    if (key == 13 && control) { key = 0x2601;literal = false; }
    else if (key == 9 && control) { key = 0x2602;literal = false; }
    else if (key == 9 && shift) { key = KEY_BTAB;literal = false; }
    else if (key == 127 && shift) { key = 0x2604;literal = false; }
    else if (key == 27 && shift) { key = 0x2605;literal = false; }
    // Conservar la letra de Ctrl+I/J/M para distinguirla de Tab y Enter.
    if (control && key >= 'A' && key <= 'Z') key += 'a' - 'A';
    return {key, alt, control, literal && !control};
}
}
std::optional<KeyboardEvent> decodeKeyboard(const Bytes &s) {
    if (s.size() < 2 || s.size() > 512 || !s.startsWith("[")) return {};
    KeyboardEvent e;e.final = s.back();
    if (s.startsWith("[?") && e.final == 'u') {
        bool ok;const int flags = s.mid(2, s.size()-3).toInt(&ok);
        if (!ok || flags < 0 || flags > 31) return {};
        e.reply = true;e.code = flags;return e;
    }
    if (!Bytes("u~ABCDHFPQRS").contains(e.final)) return {};
    const auto fields = s.mid(1, s.size()-2).split(';');
    if (fields.size() > 3) return {};
    auto numbers = [](const Bytes &field, Vector<int> &out) {
        if (field.isEmpty()) return true;
        for (const auto &part : field.split(':')) {
            if (part.isEmpty()) { out.append(-1);continue; }
            for (const char c : part) if (c < '0' || c > '9') return false;
            bool ok;const int n = part.toInt(&ok);if (!ok) return false;out.append(n);
        }
        return true;
    };
    Vector<int> keys, mods;
    if (!numbers(fields[0], keys) || keys.size() > 3) return {};
    if (keys.isEmpty() && e.final != 'u' && e.final != '~') keys.append(1);
    if (keys.isEmpty() || !scalar(keys[0])) return {};
    for (const int k : keys) if (k != -1 && !scalar(k)) return {};
    e.code = keys[0];
    if (fields.size() >= 2 && !numbers(fields[1], mods)) return {};
    if (mods.size() > 2) return {};
    if (!mods.isEmpty()) {
        if (mods[0] != -1 && (mods[0] < 1 || mods[0] > 256)) return {};
        e.modifiers = mods[0] == -1 ? 0 : mods[0]-1;
    }
    if (mods.size() == 2 && mods[1] != -1) e.type = mods[1];
    if (e.type < 1 || e.type > 3) return {};
    if (fields.size() == 3 && !numbers(fields[2], e.text)) return {};
    for (const int cp : e.text) if (!scalar(cp) || cp < 32 || cp == 127) return {};
    return e;
}
TerminalKeyboard::TerminalKeyboard() { output("\x1b[?u\x1b[c"); }
TerminalKeyboard::~TerminalKeyboard() { suspend(); }
void TerminalKeyboard::suspend() {
    if (pushed_) output("\x1b[<u");
    pushed_ = false;modifiers_ = 0;pending_.clear();sequence_.clear();discarded_ = false;
}
void TerminalKeyboard::resume() {
    modifiers_ = 0;
    if (supported_ && !pushed_) { output("\x1b[>27u");pushed_ = true; }
}
std::optional<TerminalInput> TerminalKeyboard::sequenceInput() {
    // Conservar secuencias fragmentadas sin convertirlas en texto del prompt.
    wtimeout(stdscr, 30);
    while (true) {
        wint_t v;const int result = wget_wch(stdscr, &v);
        if (result == ERR) { wtimeout(stdscr, 100);return {}; }
        if (result != OK || v < 0x20 || v > 0x7e) { sequence_.clear();discarded_ = false;wtimeout(stdscr,100);return {}; }
        if (sequence_.size() < 512) sequence_.append(char(v));else discarded_ = true;
        if (v >= 0x40 && v <= 0x7e) {
            const auto event = discarded_ ? std::optional<KeyboardEvent>{} : decodeKeyboard(sequence_);
            sequence_.clear();discarded_ = false;wtimeout(stdscr,100);
            if (!event) return {};
            if (event->reply) { supported_ = true;resume();return {}; }
            if (pushed_) modifiers_ = event->modifiers;
            if (event->type == 3 || (event->code >= 57441 && event->code <= 57454)) return {};
            if (event->modifiers & (8 | 16 | 32)) return {};
            if (!event->text.isEmpty() && !(event->modifiers & (2 | 4))) {
                for (const int cp : event->text) pending_.append({cp,false,false,true});
                const auto first = pending_.takeFirst();return first;
            }
            const int k = functional(*event);if (!k) return {};
            const bool literal = event->final == 'u' && ((k == event->code && !(k >= 57344 && k <= 63743)) || (event->code >= 57399 && event->code <= 57416 && k >= 32));
            return normalize(k,event->modifiers,literal);
        }
    }
}
std::optional<TerminalInput> TerminalKeyboard::read() {
    if (!pending_.isEmpty()) return pending_.takeFirst();
    if (!sequence_.isEmpty()) return sequenceInput();
    wint_t v;const int result = wget_wch(stdscr,&v);
    if (result == ERR) return {};
    if (result != OK || v != 27) return TerminalInput{int(v),false,false,result == OK};
    wtimeout(stdscr,30);wint_t next;const int second = wget_wch(stdscr,&next);wtimeout(stdscr,100);
    if (second == ERR) return TerminalInput{27,false,false,true};
    if (second == OK && next == '[') { sequence_ = "[";return sequenceInput(); }
    return TerminalInput{int(next),true,false,second == OK};
}
}
