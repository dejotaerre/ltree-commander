#pragma once
#include "fs/scanner.h"

namespace ltree {
struct ViewDocument {
    QByteArray bytes;
    QString text, encoding, error;
    QVector<qsizetype> lines;
    bool cancelled = false;
};
ViewDocument readViewDocument(const QString &path, const Cancellation &cancel, bool indexLines = true);
}
