#pragma once
#include "platform/platform.h"
#include "fs/scanner.h"

namespace ltree {
struct CompareOptions {
    bool caseSensitive = true, compressWhitespace = false, suppressEmpty = false;
};
struct CompareRow { int first = -1, second = -1; bool changed = false; };
struct CompareResult {
    StringList first, second;
    Vector<int> firstLineNumbers, secondLineNumbers;
    Bytes firstBytes, secondBytes;
    Vector<int> binaryBlocks;
    Index differentBytes = 0;
    String textError;
    Vector<CompareRow> rows;
    Vector<int> changes;
    String error;
    bool cancelled = false, bytesEqual = false, rawCharacters = false;
};
CompareResult compareFiles(const String &first, const String &second, const Cancellation &cancel, const CompareOptions &options = {});
CompareResult compareCharacters(const CompareResult &source, const CompareOptions &options, const Cancellation &cancel);
}
