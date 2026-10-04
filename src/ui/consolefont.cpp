#include "ui/consolefont.h"
#include <QProcess>
#include <QFile>
#include <QtEndian>

namespace ltree {
ConsoleFont::ConsoleFont()
{
    // Se usa la fuente instalada por kbd, sin copiarla ni modificar el sistema.
    const QString path=QStringLiteral(LTREE_PSF_FONT_PATH);
    QByteArray data;
    if(path.endsWith(".gz")){
        QProcess gzip;gzip.start("gzip", {"-dc", path});
        if(!gzip.waitForFinished(1000) || gzip.exitCode()!=0)return;
        data=gzip.readAllStandardOutput();
    }else{
        QFile source(path);if(!source.open(QIODevice::ReadOnly))return;data=source.readAll();
    }
    if (data.size() < 32) return;
    const auto word = [&](int offset) { return qFromLittleEndian<quint32>(data.constData() + offset); };
    if (word(0) != 0x864ab572 || word(4) != 0 || word(24) != 16 || word(28) != 8) return;
    const quint32 header = word(8), count = word(16), size = word(20);
    if (count > 65536 || size != 16 || quint64(header) + quint64(count) * size > quint64(data.size())) return;
    if (!(word(12) & 1)) return;
    qsizetype pos = header + count * size;
    for (quint32 i = 0; i < count && pos < data.size(); ++i) {
        const qsizetype end = data.indexOf(char(0xff), pos);
        if (end < 0) return;
        QByteArray mapping = data.mid(pos, end - pos);
        const auto sequence = mapping.indexOf(char(0xfe));
        if (sequence >= 0) mapping.truncate(sequence);
        for (char32_t code : QString::fromUtf8(mapping).toUcs4())
            glyphs_.insert(code, data.mid(header + i * size, size));
        pos = end + 1;
    }
}

bool ConsoleFont::draw(QPainter &p, int x, int y, const QString &text, const QColor &color) const
{
    const auto codes = text.toUcs4();
    if (codes.size() != 1 || !glyphs_.contains(codes[0])) return false;
    const auto &bitmap = glyphs_[codes[0]];
    for (int row = 0; row < 16; ++row)
        for (int col = 0; col < 8; ++col)
            if (quint8(bitmap[row]) & (0x80 >> col)) p.fillRect(x + col, y + row, 1, 1, color);
    return true;
}
}
