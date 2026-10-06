#pragma once
#include <QByteArray>
#include <QVector>
#include <optional>

namespace ltree {
struct KeyboardEvent {
    int code = 0, modifiers = 0, type = 1;
    char final = 0;
    bool reply = false;
    QVector<int> text;
};
std::optional<KeyboardEvent> decodeKeyboard(const QByteArray &sequence);
struct TerminalInput { int value; bool alt = false, control = false, literal = false; };
class TerminalKeyboard {
    bool supported_ = false, pushed_ = false, discarded_ = false;
    int modifiers_ = 0;
    QByteArray sequence_;
    QVector<TerminalInput> pending_;
    std::optional<TerminalInput> sequenceInput();
public:
    TerminalKeyboard();
    ~TerminalKeyboard();
    std::optional<TerminalInput> read();
    void suspend();
    void resume();
    int menu(int fallback) const { return modifiers_ & 4 ? 1 : modifiers_ & 2 ? 2 : fallback; }
};
}
