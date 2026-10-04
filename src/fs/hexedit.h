#pragma once
#include <QString>
#include <QByteArray>
namespace ltree {
struct HexSnapshot { quint64 device=0,inode=0; qint64 changedSeconds=0,changedNanos=0; bool valid=false; bool operator==(const HexSnapshot &) const = default; };
HexSnapshot hexSnapshot(const QString &path);
QString saveHexPage(const QString &path,const HexSnapshot &expected,const QByteArray &original,qsizetype offset,const QByteArray &page);
}
