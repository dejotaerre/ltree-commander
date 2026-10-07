#include "platform/platform.h"
#include "fs/viewdocument.h"

namespace ltree {
ViewDocument readViewDocument(const String &path, const Cancellation &cancel, bool indexLines)
{
    ViewDocument result;
    if (cancel->load()) { result.cancelled = true; return result; }
    constexpr int64 maximum = 32 * 1024 * 1024;
    const FileInfo info(path);
    if (!info.isFile()) { result.error = "The file does not exist or is not a regular file"; return result; }
    File file(path);
    if (!file.open(IO::ReadOnly)) { result.error = file.errorString(); return result; }
    if (file.size() > maximum) { result.error = "Viewer limit: 32 MiB; use E to open in the editor"; return result; }
    while (!file.atEnd()) {
        if (cancel->load()) { result.cancelled = true; return result; }
        const auto chunk = file.read(65536);
        if (file.error() != File::NoError) { result.error = file.errorString(); return result; }
        result.bytes += chunk;
        if (result.bytes.size() > maximum) { result.error = "The file grew beyond 32 MiB"; return result; }
    }
    const auto encoding = TextEncoding::encodingForData(result.bytes).value_or(TextEncoding::Utf8);
    TextDecoder decoder(encoding);
    result.text = decoder.decode(result.bytes);
    char16_t tail[8];
    const auto end = decoder.finalize(tail, 8);
    if (decoder.hasError() || end.error != TextEncoding::FinalizeResultError::NoError) {
        result.text = String::fromLatin1(result.bytes); result.encoding = "Latin-1 (fallback)";
    } else result.encoding = String::fromLatin1(TextEncoding::nameForEncoding(encoding));
    result.text.replace("\r\n", "\n"); result.text.replace('\r', '\n');
    if (!indexLines) return result;
    result.lines.append(0);
    for (Index i = 0; i < result.text.size(); ++i) {
        if ((i & 4095) == 0 && cancel->load()) { result.cancelled = true; return result; }
        if (result.text[i] == '\n' && i + 1 < result.text.size()) result.lines.append(i + 1);
    }
    return result;
}
}
