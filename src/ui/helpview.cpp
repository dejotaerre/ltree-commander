#include "ui/helpview.h"
#include "ui/theme.h"
#include <algorithm>

namespace ltree {
void drawHelpDocument(QPainter &p, const QRect &rect, HelpDocument &help, int cw, int ch, int ascent, const ConsoleFont *font)
{
    const int cols = rect.width() / cw, rows = rect.height() / ch;
    const int x = std::max(2, (cols - 88) / 2), w = std::max(8, std::min(88, cols - 2*x));
    const int height = std::max(1, rows - 6);
    help.constrain(w, height);
    const auto content = help.lines(w);
    p.fillRect(rect, theme::background);
    auto text = [&](int tx, int ty, QString value, QColor color) {
        int column = tx;
        for (char32_t code : value.toUcs4()) {
            if (column >= cols) break;
            const QString glyph = QString::fromUcs4(&code, 1);
            p.save(); p.setClipRect(column*cw, ty*ch, cw, ch, Qt::IntersectClip);
            if (!font || !font->draw(p, column*cw, ty*ch, glyph, color)) {
                p.setPen(color); p.drawText(column*cw, ty*ch+ascent, glyph);
            }
            p.restore(); ++column;
        }
    };
    text(1, 0, (cols < 64 ? "LTree Help / " : "LTree Commander Help  /  ") + help.title(), theme::labels);
    p.setPen(theme::labels);
    p.drawLine(cw, 2*ch-1, (cols-1)*cw, 2*ch-1);
    p.drawLine(cw, (rows-3)*ch, (cols-1)*cw, (rows-3)*ch);
    for (int i = 0; i < height && help.top()+i < content.size(); ++i) {
        const int row = help.top()+i, y = i+3;
        const auto &line = content[row];
        const bool selected = help.index() && row == help.selected()+2;
        if (selected) p.fillRect(x*cw, y*ch, w*cw, ch, theme::selection);
        const QColor color = selected ? Qt::black : line.style == HelpStyle::Heading ? theme::labels : theme::normal;
        text(x, y, line.text, color);
        if (!selected && line.keyLength) text(x, y, line.text.left(line.keyLength), theme::labels);
        if (!help.index() && !help.query().isEmpty() && !help.finding()) {
            int at = 0;
            while ((at = line.text.indexOf(help.query(), at, Qt::CaseInsensitive)) >= 0) {
                const int count = help.query().size();
                p.fillRect((x+at)*cw, y*ch, count*cw, ch, theme::selection);
                text(x+at, y, line.text.mid(at, count), Qt::black); at += std::max(1, count);
            }
        }
    }
    if (content.size() > height) {
        p.fillRect((cols-2)*cw, 3*ch, cw, height*ch, QColor(0, 80, 128));
        const int thumb = std::max(1, height*height/int(content.size()));
        const int position = help.top()*(height-thumb)/std::max(1, int(content.size())-height);
        p.fillRect((cols-2)*cw, (3+position)*ch, cw, thumb*ch, theme::labels);
    }
    auto commands = [&](int y, const QString &value) {
        int column = 1; bool key = false;
        for (QChar c : value) {
            if (c == '[') key = true;
            else if (c == ']') key = false;
            else { text(column++, y, QString(c), key ? theme::labels : theme::normal); }
        }
    };
    commands(rows-2, cols >= 90 ? "[↑↓/PgUp/PgDn] Scroll  [←→] Chapter  [Tab] Contents  [F] Find  [Space] Next  [Esc/F1] Return" :
             cols < 64 ? "[↑↓] Scroll [←→] Chapter [Tab] Index" : "[Up/Down] Scroll [PgUp/PgDn] Page [Left/Right] Chapter [Tab] Index");
    if (help.finding()) {
        commands(rows-2, "[Enter] Find  [Backspace] Erase  [Esc] Cancel search");
        text(1, rows-1, "Find in help: " + help.query().right(std::max(1, cols-17)), theme::normal);
        const int cursor = std::min(cols-2, 15+int(help.query().size()));
        p.fillRect(cursor*cw, (rows-1)*ch, cw, ch, theme::normal);
    } else {
        const QString progress = QString("Lines %1-%2 of %3").arg(help.top()+1).arg(std::min(help.top()+height, int(content.size()))).arg(content.size());
        if (cols < 90) commands(rows-1, "[F] Find [Space] Next [Esc/F1] Return  " + progress);
        else text(1, rows-1, help.notice().isEmpty() ? progress + "    Read with arrows or the mouse wheel" : help.notice() + "    " + progress, theme::labels);
    }
}

bool helpDocumentKey(HelpDocument &help, QKeyEvent *event, int cols, int rows)
{
    const int width = std::max(8, std::min(88, cols-4)), height = std::max(1, rows-6);
    const int key = event->key();
    if (help.finding()) {
        if (key == Qt::Key_Escape || key == Qt::Key_F1) return help.act(HelpAction::Close, width, height);
        if (key == Qt::Key_Return || key == Qt::Key_Enter) return help.act(HelpAction::Accept, width, height);
        if (key == Qt::Key_Backspace) return help.act(HelpAction::Backspace, width, height);
        if (!event->text().isEmpty() && !event->modifiers().testFlag(Qt::ControlModifier) && !event->modifiers().testFlag(Qt::AltModifier))
            return help.act(HelpAction::Input, width, height, event->text());
        return true;
    }
    switch (key) {
    case Qt::Key_F1: case Qt::Key_Escape: return false;
    case Qt::Key_Up: return help.act(HelpAction::Up, width, height);
    case Qt::Key_Down: return help.act(HelpAction::Down, width, height);
    case Qt::Key_PageUp: return help.act(HelpAction::PageUp, width, height);
    case Qt::Key_PageDown: return help.act(HelpAction::PageDown, width, height);
    case Qt::Key_Home: return help.act(HelpAction::Home, width, height);
    case Qt::Key_End: return help.act(HelpAction::End, width, height);
    case Qt::Key_Left: case Qt::Key_BracketLeft: return help.act(HelpAction::Previous, width, height);
    case Qt::Key_Right: case Qt::Key_BracketRight: return help.act(HelpAction::Next, width, height);
    case Qt::Key_Tab: return help.act(HelpAction::Index, width, height);
    case Qt::Key_Return: case Qt::Key_Enter: return help.act(HelpAction::Accept, width, height);
    case Qt::Key_F: case Qt::Key_Slash: return help.act(HelpAction::Find, width, height);
    case Qt::Key_Space: return help.act(HelpAction::Repeat, width, height);
    default: return true;
    }
}
void helpDocumentScroll(HelpDocument &help, int steps, int cols, int rows)
{
    for (int i = 0; i < std::abs(steps)*3; ++i)
        help.act(steps > 0 ? HelpAction::Up : HelpAction::Down, std::max(8, std::min(88, cols-4)), std::max(1, rows-6));
}
}
