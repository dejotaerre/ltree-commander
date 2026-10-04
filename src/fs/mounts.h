#pragma once
#include <QString>
#include <QVector>

namespace ltree {
struct MountPoint { QString path, type, source; bool readOnly = false; };
QVector<MountPoint> parseMountInfo(const QByteArray &data, bool includeSystem = false);
QVector<MountPoint> mountedLocations(QString *error = nullptr, bool includeSystem = false);
}
