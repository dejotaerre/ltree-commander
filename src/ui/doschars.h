#pragma once
#include <QString>

namespace ltree {
inline QString dosGlyph(quint8 byte)
{
    // Glifos de pantalla DOS: los controles se dibujan, no se ejecutan.
    static const QString controls = QString::fromUtf8(" ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼");
    static const QString extended = QString::fromUtf8("ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ");
    if (byte < 32) return QString(controls[byte]);
    if (byte == 127) return QString(QChar(0x2302));
    if (byte >= 128) return QString(extended[byte-128]);
    return QString(QChar(byte));
}
}
