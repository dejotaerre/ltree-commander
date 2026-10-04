#pragma once

#include <QDateTime>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>

namespace ltree {

struct FileEntry {
    QString path;
    QString name;
    qint64 size = 0;
    QDateTime modified;
    bool hidden = false;
    bool writable = true;
    bool symlink = false;
};

struct DirectoryScan {
    QString path;
    QStringList children;
    QVector<FileEntry> files;
    QString error;
};

struct ScanResult {
    QVector<DirectoryScan> directories;
    bool cancelled = false;
};

using Cancellation = std::shared_ptr<std::atomic_bool>;
ScanResult scanDirectories(const QString &path, bool recursive, const Cancellation &cancel, const QStringList &boundaries = {});
bool isWithin(const QString &path, const QString &base);

}
