#pragma once
#include "platform/platform.h"

namespace ltree {
inline String dosGlyph(uint8 byte)
{
    // Glifos de pantalla DOS: los controles se dibujan, no se ejecutan.
    static const String controls = String::fromUtf8(" ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼");
    static const String extended = String::fromUtf8("ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ");
    if (byte < 32) return String(controls[byte]);
    if (byte == 127) return String(Char(0x2302));
    if (byte >= 128) return String(extended[byte-128]);
    return String(Char(byte));
}
}
