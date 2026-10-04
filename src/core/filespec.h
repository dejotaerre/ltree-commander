#pragma once
#include <QRegularExpression>
#include <QVector>
#include "fs/scanner.h"

namespace ltree {
class Filespec {
public:
    bool set(const QString &text, QString *error = nullptr);
    bool matches(const QString &filename) const;
    bool matches(const FileEntry &file, QDate today = QDate::currentDate()) const;
    bool usesToday() const;
    QString text() const { return text_; }
    bool inverted = false;
private:
    struct Rule { QRegularExpression name, extension; bool negative; };
    struct Comparison {
        int mask = 0;
        qint64 value = 0;
        bool relative = false;
        int position = 0;
    };
    static bool compare(const QVector<Comparison> &, qint64 value, QDate today);
    QString text_ = "*.*";
    QVector<Rule> rules_;
    QVector<Comparison> dates_, sizes_;
};
}
