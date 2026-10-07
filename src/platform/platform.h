#pragma once
#ifdef LTREE_USE_QT
#include <QByteArray>
#include <QChar>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QString>
#include <QStringDecoder>
#include <QStringList>
#include <QStringView>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUuid>
#include <QVector>
#include <QtConcurrent/QtConcurrentRun>
#include <QtEndian>

namespace ltree {
using String = QString;
using StringList = QStringList;
using StringView = QStringView;
using Char = QChar;
using Bytes = QByteArray;
template <class T> using Vector = QVector<T>;
template <class T> using Set = QSet<T>;
template <class K, class V> using Map = QMap<K, V>;
template <class K, class V> using Pair = QPair<K, V>;
using DateTime = QDateTime;
using Regex = QRegularExpression;
using File = QFile;
using FileInfo = QFileInfo;
using DirectoryPath = QDir;
using IO = QIODevice;
using SaveFile = QSaveFile;
using Process = QProcess;
using ProcessEnvironment = QProcessEnvironment;
using TemporaryDirectory = QTemporaryDir;
using ElapsedTimer = QElapsedTimer;
using Paths = QStandardPaths;
using Locale = QLocale;
using Uuid = QUuid;
using StorageInfo = QStorageInfo;
using SystemInfo = QSysInfo;
using TextEncoding = QStringConverter;
using TextDecoder = QStringDecoder;
using TerminalApplication = QCoreApplication;
template <class T> using Future = QFuture<T>;
using Mutex = QMutex;
template <class M> using MutexLocker = QMutexLocker<M>;
using JsonArray = QJsonArray;
using JsonObject = QJsonObject;
using JsonDocument = QJsonDocument;
using int64 = qint64;
using uint64 = quint64;
using uint32 = quint32;
using uint16 = quint16;
using uint8 = quint8;
using Index = qsizetype;
using Date = QDate;
using Time = QTime;
namespace TextOptions = Qt;
inline String environment(const char *name, const String &fallback = {}) {
    return qEnvironmentVariable(name, fallback);
}
inline bool environmentEmpty(const char *name) { return qEnvironmentVariableIsEmpty(name); }
template <class A, class B> auto makePair(A a, B b) { return qMakePair(a, b); }
template <class T> T fromBigEndian(const unsigned char *value) { return qFromBigEndian<T>(value); }
template <class F> auto runAsync(F &&function) { return QtConcurrent::run(std::forward<F>(function)); }
}
#else
#include "platform/portable.h"
#endif
