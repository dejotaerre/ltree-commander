#pragma once
#include "fs/scanner.h"

namespace ltree {
struct CompareOptions {
    bool caseSensitive = true, compressWhitespace = false, suppressEmpty = false;
};
struct CompareRow { int first = -1, second = -1; bool changed = false; };
struct CompareResult {
    QStringList first, second;
    QVector<int> firstLineNumbers, secondLineNumbers;
    QByteArray firstBytes, secondBytes;
    QVector<int> binaryBlocks;
    qsizetype differentBytes = 0;
    QString textError;
    QVector<CompareRow> rows;
    QVector<int> changes;
    QString error;
    bool cancelled = false, bytesEqual = false, rawCharacters = false;
};
CompareResult compareFiles(const QString &first, const QString &second, const Cancellation &cancel, const CompareOptions &options = {});
CompareResult compareCharacters(const CompareResult &source, const CompareOptions &options, const Cancellation &cancel);
}
