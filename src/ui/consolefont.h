#pragma once
#include <QHash>
#include <QPainter>

namespace ltree {
class ConsoleFont {
public:
    ConsoleFont();
    bool available() const { return !glyphs_.isEmpty(); }
    bool draw(QPainter &p, int x, int y, const QString &text, const QColor &color) const;
private:
    QHash<char32_t, QByteArray> glyphs_;
};
}
