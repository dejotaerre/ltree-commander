#pragma once
#include <QColor>
#include <QFileInfo>
#include <QStringList>

namespace ltree::theme {
// Paleta medida en la captura del usuario; extensiones según su ZCOLORS.INI.
inline const QColor background(0, 0, 128);
inline const QColor labels(0, 255, 255);
inline const QColor normal(255, 255, 0);
inline const QColor selection(192, 192, 192);

inline QColor fileColor(const QString &name)
{
    const QString ext = QFileInfo(name).suffix().toUpper();
    if (QStringList{"COM","EXE","BAT","BTM","CMD","VBS","VBE","JS","JSE","WSH","WSF"}.contains(ext)) return QColor(0,255,0);
    if (QStringList{"DLL","DRV","SYS"}.contains(ext)) return QColor(0,128,0);
    if (QStringList{"C","H","CPP","HPP","MAK","DEF"}.contains(ext)) return QColor(255,0,255);
    if (QStringList{"TXT","DAT","HLP","ASC","LOG","LIS","DES","DOC","WRI","WQ1"}.contains(ext)) return QColor(128,128,128);
    if (QStringList{"ZIP","ARJ","J","RAR","LZH"}.contains(ext) || (ext.size()==3 && ext.endsWith('_'))) return selection;
    if (QStringList{"INI","INF","CFG"}.contains(ext)) return QColor(128,128,0);
    if (QStringList{"TMP","BAK"}.contains(ext)) return QColor(255,0,0);
    return normal;
}
}
