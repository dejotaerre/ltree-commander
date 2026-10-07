#include "core/filespec.h"
#include "core/session.h"
#include "fs/copyfiles.h"
#include "fs/hexedit.h"
#include "fs/metadata.h"
#include "fs/search.h"
#include "fs/stamp.h"
#include "fs/viewdocument.h"
#include "platform/platform.h"
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using namespace ltree;
namespace {
int checks = 0;
void check(bool value, const char *name) {
    ++checks;
    if (!value)
        throw std::runtime_error(name);
}
void report(const char *name, const String &text) {
    std::cout << name << '=' << text.toUtf8().toHex().constData() << '\n';
}
template <class T> void reportNumber(const char *name, T value) { std::cout << name << '=' << value << '\n'; }
void write(const String &path, const Bytes &bytes) {
    File file(path);
    check(file.open(IO::WriteOnly), "open fixture");
    check(file.write(bytes) == bytes.size(), "write fixture");
}
void strings() {
    const String unicode = "Árbol Σίσυφος Straße 𐐀 😀";
    report("unicode.upper", unicode.toUpper());
    report("unicode.lower", unicode.toLower());
    report("unicode.fold", unicode.toCaseFolded());
    check(String::fromUtf8(unicode.toUtf8()) == unicode, "Unicode round trip");
    Vector<char32_t> scalars;
    for (auto c : unicode.toUcs4())
        scalars.append(char32_t(c));
    check(String::fromUcs4(scalars.data(), scalars.size()) == unicode, "surrogate round trip");
    const String pairs[][2] = {{"ÁRBOL", "árbol"}, {"𐐀", "𐐨"}, {"Straße", "STRASSE"},
                               {"Σςσ", "σσσ"},     {"İ", "i"}, {"ﬃ", "ffi"}};
    for (int i = 0; i < 6; ++i) {
        const auto label = "unicode.compare." + std::to_string(i);
        const int comparison = String::compare(pairs[i][0], pairs[i][1], TextOptions::CaseInsensitive);
        reportNumber(label.c_str(), (comparison > 0) - (comparison < 0));
        const auto search = "unicode.find." + std::to_string(i);
        reportNumber(search.c_str(),
                     ("x" + pairs[i][0] + "z").indexOf(pairs[i][1], 0, TextOptions::CaseInsensitive));
    }
    check(String(" \t alpha \n\u00a0 beta ").simplified() == "alpha beta", "Unicode whitespace");
    check(String("a,,b").split(',', TextOptions::SkipEmptyParts).size() == 2, "split skip blanks");
    report("arg.width", String("%2/%1/%2/%3").arg(15, 4, 16, '0').arg("name", -6).arg("end"));
    report("arg.negative", String("%1").arg(-15, 5, 10, '0'));
    report("arg.simultaneous", String("%1-%2-%3").arg("%2", "literal", "tail"));
    report("bytes.oddhex", String::fromLatin1(Bytes::fromHex("abc").toHex()));
    bool valid = false;
    check(String("18446744073709551615").toULongLong(&valid) == UINT64_MAX && valid, "unsigned limit");
    check(String("-9223372036854775808").toLongLong(&valid) == INT64_MIN && valid, "signed limit");
    String("9223372036854775808").toLongLong(&valid);
    check(!valid, "integer overflow rejected");
    check(String("755").toUInt(nullptr, 8) == 0755, "octal permissions");
    check(Bytes("zero\0tail", 9).indexOf(Bytes("\0t", 2)) == 4, "binary search includes NUL");
}
void regularExpressions() {
    const auto expression = searchExpression({"^order_(\\d+)$", SearchMode::Regex, false});
    check(expression.isValid(), "valid regular expression");
    const auto match = expression.match("ORDER_123");
    check(match.hasMatch() && match.captured(1) == "123", "regex captures");
    check(!Regex("[").isValid(), "invalid regular expression");
    check(!validateSearch({"[", SearchMode::Regex, false}).isEmpty(), "regex validation");
    const auto emoji = Regex("😀").match("abc😀z");
    check(emoji.capturedStart() == 3 && emoji.capturedLength() == 2, "regex UTF-16 offsets");
    const auto backward = matchRegexLine(Regex("[0-9]+"), String("a12 b345 c6"), 8, true);
    reportNumber("regex.backward", backward.start);
    check(backward.start == 7, "backward regex search");
    const auto empty = matchRegexLine(Regex("(?=a)"), String("a a"), 0);
    check(empty.start == 0 && empty.length == 0, "zero length regex");
    const String large = String(65536, 'a');
    check(matchRegexLine(Regex("a"), StringView(large), large.size() - 1, true).start == large.size() - 1,
          "backward regex navigation on a long line");
    const String subject = "xxa12yy";
    const auto slice = Regex("a([0-9]+)").matchView(StringView(subject).mid(2, 3));
    check(slice.capturedStart() == 0 && slice.captured(1) == "12", "regex captures from a substring view");
    const auto bounded =
        matchRegexLine(searchExpression({"^(a+)+$", SearchMode::Regex, true}), String(10000, 'a') + "b");
    check(!bounded.error.isEmpty(), "regex resource limit");
}
void encoding() {
    const Bytes samples[] = {Bytes::fromHex("efbbbf41c3a9f09f9880"), Bytes::fromHex("fffe4100e9003dd800de"),
                             Bytes::fromHex("feff004100e9d83dde00"),
                             Bytes::fromHex("fffe00004100000000f60100"),
                             Bytes::fromHex("0000feff000000410001f600")};
    const TextEncoding::Encoding kinds[] = {TextEncoding::Utf8, TextEncoding::Utf16LE, TextEncoding::Utf16BE,
                                            TextEncoding::Utf32LE, TextEncoding::Utf32BE};
    for (int i = 0; i < 5; ++i) {
        check(TextEncoding::encodingForData(samples[i]) == kinds[i], "encoding BOM detection");
        TextDecoder decoder(kinds[i]);
        String decoded;
        for (Index n = 0; n < samples[i].size(); ++n)
            decoded += decoder.decode(samples[i].mid(n, 1));
        char16_t tail[8];
        const auto end = decoder.finalize(tail, 8);
        check(!decoder.hasError() && end.error == TextEncoding::FinalizeResultError::NoError,
              "streaming Unicode decode");
        report(("encoding." + std::to_string(i)).c_str(), decoded);
    }
    for (auto kind : {TextEncoding::Utf16, TextEncoding::Utf32}) {
        TextDecoder native(kind);
        const auto input =
            kind == TextEncoding::Utf16 ? Bytes::fromHex("4100e900") : Bytes::fromHex("41000000e9000000");
        const String decoded = native.decode(input);
        check(decoded == "Aé", "native-endian Unicode without BOM");
        TextDecoder big(kind);
        String joined;
        const auto bytes = kind == TextEncoding::Utf16 ? Bytes::fromHex("feff004100e9")
                                                       : Bytes::fromHex("0000feff00000041000000e9");
        for (Index i = 0; i < bytes.size(); ++i)
            joined += big.decode(bytes.mid(i, 1));
        check(joined == "Aé", "generic Unicode streaming BOM");
    }
    const String longLine = String(1024 * 1024, 'a') + "END";
    check(longLine.lastIndexOf("absent") == -1 && longLine.lastIndexOf("END") == 1024 * 1024,
          "backward search in long viewer line");
    TextDecoder invalid(TextEncoding::Utf8);
    const String incomplete = invalid.decode(Bytes::fromHex("f09f98"));
    check(incomplete.isEmpty(), "incomplete Unicode is buffered");
    char16_t tail[8];
    const auto invalidEnd = invalid.finalize(tail, 8);
    check(invalid.hasError() || invalidEnd.error != TextEncoding::FinalizeResultError::NoError,
          "truncated Unicode rejected");
}
void calendarAndJson() {
    check(Date(2024, 2, 29).isValid() && !Date(2023, 2, 29).isValid(), "leap day validation");
    check(Date::fromJulianDay(Date(2024, 2, 29).toJulianDay()) == Date(2024, 2, 29), "Julian conversion");
    report("date.month",
           DateTime(Date(2024, 1, 31), Time(12, 34, 55)).addMonths(1).toString("yyyy-MM-dd HH:mm:ss"));
    report("date.year",
           DateTime(Date(2024, 2, 29), Time(12, 34, 55)).addYears(1).toString("yyyy-MM-dd HH:mm:ss"));
    report("date.negative", DateTime::fromMSecsSinceEpoch(-1).toString("yyyy-MM-dd HH:mm:ss"));
    const JsonDocument document(
        JsonObject{{"version", 1}, {"items", JsonArray{String("árbol"), String("a\nb")}}});
    const auto read = JsonDocument::fromJson(document.toJson()).object();
    check(read.value("version").toInt() == 1 && read.value("items").toArray().size() == 2,
          "JSON history round trip");
    check(read.value("items").toArray()[0].toString() == "árbol", "Unicode JSON history");
    reportNumber("json.overflow",
                 JsonDocument::fromJson("{\"version\":4294967297}").object().value("version").toInt());
    report("json.nul", JsonDocument::fromJson("{\"text\":\"a\\u0000b\"}").object().value("text").toString());
    check(JsonDocument::fromJson("{broken").object().value("version").toInt() == 0,
          "invalid JSON retained safely");
}
void operations(const String &root) {
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    check(DirectoryPath(root).mkpath("branch/sub"), "recursive directory creation");
    const auto source = root + "/branch/a.txt", other = root + "/branch/sub/b.txt";
    write(source, "ORDER_123\nsecond\nORDER_456\n");
    write(other, "plain\n");
    const auto scanned = scanDirectories(root, true, cancel);
    check(scanned.directories.size() == 3, "recursive scan");
    Session session(root);
    session.apply(scanned, false);
    session.selectDirectory(root + "/branch");
    session.enter(View::Branch);
    session.setTag(true, true);
    check(session.files().size() == 2 && session.tags.size() == 2, "branch and tagging");
    Filespec spec;
    check(spec.set("*.txt") && spec.matches("a.txt") && !spec.matches("a.bin"), "filespec filter");
    check(spec.set("*.txt,-b.*") && spec.matches("a.txt") && !spec.matches("b.txt"), "filespec exclusions");
    check(searchFile(source, {"order_", SearchMode::Text, false}, cancel).outcome == SearchOutcome::Found,
          "literal file search");
    check(searchFile(source, {"^ORDER_[0-9]+$", SearchMode::Regex, true}, cancel).outcome ==
              SearchOutcome::Found,
          "regex file search");
#ifndef LTREE_USE_QT
    // El backend nativo también acepta archivos virtuales que informan tamaño cero.
    check(readViewDocument("/proc/version", cancel).text.startsWith("Linux version"),
          "viewer reads virtual files reporting zero size");
    check(searchFile("/proc/version", {"Linux version", SearchMode::Text, true}, cancel).outcome ==
              SearchOutcome::Found,
          "search reads virtual files reporting zero size");
#endif
    const auto document = readViewDocument(source, cancel);
    check(document.error.isEmpty() && document.lines.size() == 3, "file viewer line index");
    const auto unicodePath = root + "/unicode.txt";
    write(unicodePath, Bytes::fromHex("fffe4100e9003dd800de0a00"));
    check(searchFile(unicodePath, {"Aé😀", SearchMode::Unicode, true}, cancel).outcome ==
              SearchOutcome::Found,
          "UTF-16 file search");
    check(readViewDocument(unicodePath, cancel).text == "Aé😀\n", "UTF-16 file viewer");
    const auto boundaryPath = root + "/boundary.txt";
    write(boundaryPath, Bytes(65535, 'x') + String("😀ÁRBOL\n").toUtf8());
    check(searchFile(boundaryPath, {"😀árbol", SearchMode::Unicode, false}, cancel).outcome ==
              SearchOutcome::Found,
          "Unicode search across read boundary");
    check(searchFile(boundaryPath, {"😀ÁRBOL$", SearchMode::Regex, true}, cancel).outcome ==
              SearchOutcome::Found,
          "regex search across read boundary");
    const auto invalidPath = root + "/invalid.txt";
    write(invalidPath, Bytes::fromHex("e9ff"));
    check(readViewDocument(invalidPath, cancel).encoding == "Latin-1 (fallback)", "legacy encoding fallback");
    auto metadata = readMetadata(source);
    uint32 mode;
    String error;
    check(parsePermissions("u+rw,go-rwx", metadata.mode, false, mode, error), "symbolic permission parsing");
    const auto permission = changePermissions(metadata, mode, cancel);
    check(permission.changed && readMetadata(source).mode == 0600, "permission mutation");
    const auto plan =
        planStamp({readMetadata(source)}, "2024-02-29 12:34:55", StampField::Both, StampMode::Set);
    check(plan.error.isEmpty() && stampFiles(plan, cancel).changed == 1, "timestamp mutation");
    metadata = readMetadata(source);
    check(metadata.modified.toString("yyyy-MM-dd HH:mm:ss") == "2024-02-29 12:34:55", "stored timestamp");
    check(::link(File::encodeName(source).constData(), File::encodeName(root + "/hardlink").constData()) == 0,
          "hardlink fixture");
    auto repeated = planStamp({readMetadata(source), readMetadata(root + "/hardlink")}, "+1d",
                              StampField::Written, StampMode::Adjust);
    auto changed = stampFiles(repeated, cancel);
    check(changed.changed == 1 && changed.unchanged == 1, "hardlink timestamp deduplication");
    check(DirectoryPath(root).mkpath("dest"), "copy destination");
    const auto copied = copyFile(root, {source, root + "/dest/a.txt"}, CopyReplace::Never, cancel);
    check(copied.copied && File::exists(root + "/dest/a.txt"), "file copy");
    check(copyFile(root, {source, root + "/dest/a.txt"}, CopyReplace::Never, cancel).skipped,
          "no overwrite copy");
    const auto snapshot = hexSnapshot(other);
    check(saveHexPage(other, snapshot, "plain\n", 0, "PLAIN\n").isEmpty(), "hex edit save");
    check(!saveHexPage(other, snapshot, "plain\n", 0, "again\n").isEmpty(), "stale hex edit rejected");
    {
        SaveFile saved(root + "/history.json");
        check(saved.open(IO::WriteOnly), "atomic history open");
        saved.write("{\"version\":1}");
        check(saved.commit(), "atomic history commit");
    }
    {
        SaveFile saved(root + "/history.json");
        check(saved.open(IO::WriteOnly), "atomic cancellation open");
        saved.write("partial");
        saved.cancelWriting();
    }
    File saved(root + "/history.json");
    check(saved.open(IO::ReadOnly) && saved.readAll() == "{\"version\":1}",
          "cancelled save preserves original");
    const auto dataPath = root + "/data";
    check(DirectoryPath().mkpath(dataPath), "Trash data directory");
    ::setenv("XDG_DATA_HOME", dataPath.toUtf8().constData(), 1);
    const auto trashSource = root + "/trash me.txt";
    write(trashSource, "keep this content");
    String trashed;
    check(File::moveToTrash(trashSource, &trashed) && !File::exists(trashSource), "move to Trash");
    File retained(trashed);
    check(retained.open(IO::ReadOnly) && retained.readAll() == "keep this content", "Trash retains content");
    check(DirectoryPath(dataPath + "/Trash/info")
                  .entryList(DirectoryPath::NoDotAndDotDot | DirectoryPath::AllEntries)
                  .size() == 1,
          "Trash restore metadata");
#ifndef LTREE_USE_QT
    const auto redirected = root + "/redirect";
    check(DirectoryPath().mkpath(redirected), "Trash redirection fixture");
    const auto safeData = root + "/unsafe-data";
    check(DirectoryPath().mkpath(safeData + "/Trash"), "Trash redirection parent");
    check(::symlink(redirected.toUtf8().constData(), (safeData + "/Trash/files").toUtf8().constData()) == 0,
          "Trash symlink fixture");
    ::setenv("XDG_DATA_HOME", safeData.toUtf8().constData(), 1);
    write(trashSource, "retain on failure");
    check(!File::moveToTrash(trashSource) && File::exists(trashSource),
          "Trash rejects redirected files directory");
    check(DirectoryPath(redirected)
              .entryList(DirectoryPath::NoDotAndDotDot | DirectoryPath::AllEntries)
              .isEmpty(),
          "Trash does not write outside its directory");
#endif
    ::setenv("TZ", "America/New_York", 1);
    ::tzset();
    check(!planStamp({readMetadata(source)}, "2024-03-10 02:30:00", StampField::Written, StampMode::Set)
               .error.isEmpty(),
          "nonexistent DST time rejected");
    ::setenv("TZ", "UTC", 1);
    ::tzset();
}
void processes() {
    TemporaryDirectory fixture;
    check(fixture.isValid(), "process descriptor fixture");
    const auto path = fixture.filePath("inherited.txt");
    write(path, "descriptor content");
    File pinned(path);
    check(pinned.open(IO::ReadOnly), "pinned process descriptor");
    const int descriptor = pinned.handle();
    Process inherited;
    inherited.setChildProcessModifier([descriptor] { ::fcntl(descriptor, F_SETFD, 0); });
    inherited.start("/bin/cat", {String("/proc/self/fd/%1").arg(descriptor)});
    check(inherited.waitForStarted() && inherited.waitForFinished(3000) &&
              inherited.readAllStandardOutput() == "descriptor content",
          "Graft descriptor inheritance through child modifier");
    check(::fcntl(descriptor, F_GETFD) & FD_CLOEXEC, "child modifier preserves parent descriptor flags");
    Process command;
    command.start("/bin/sh", {"-c", "printf output; printf error >&2; exit 7"});
    check(command.waitForStarted(3000) && command.waitForFinished(3000), "process launch");
    check(command.exitCode() == 7 && command.readAllStandardOutput() == "output" &&
              command.readAllStandardError() == "error",
          "process channels and status");
    Process cat;
    cat.start("/bin/cat");
    check(cat.waitForStarted(), "pipe launch");
    check(cat.write("Unicode: árbol\n") == 16, "pipe write");
    cat.closeWriteChannel();
    check(cat.waitForFinished(3000) && cat.readAllStandardOutput() == "Unicode: árbol\n", "pipe EOF");
    Process missing;
    missing.start("/ltc-fixture/nonexistent");
    check(!missing.waitForStarted(3000), "failed execution reported");
    const auto args = Process::splitCommand(String("editor --flag \"file name\" \"\"\"quote\"\"\""));
    report("process.quoted", args.join("|"));
    Process noInput;
    noInput.start("/bin/sh", {"-c", "exec 0<&-; printf ready; sleep 0.2"});
    check(noInput.waitForStarted(), "closed stdin process");
    for (int i = 0; i < 100 && noInput.readAllStandardOutput().isEmpty(); ++i)
        noInput.waitForFinished(10);
    noInput.write("writing to a closed pipe");
    check(noInput.waitForFinished(3000), "closed stdin does not terminate parent");
    Process largeOutput;
    largeOutput.start("/bin/sh", {"-c", "head -c 262144 /dev/zero; head -c 262144 /dev/zero >&2"});
    check(largeOutput.waitForStarted() && largeOutput.waitForFinished(3000), "large process output drained");
    check(largeOutput.readAllStandardOutput().size() == 262144 &&
              largeOutput.readAllStandardError().size() == 262144,
          "large process channels preserved");
    Process stopped;
    stopped.start("/bin/sleep", {"10"});
    check(stopped.waitForStarted(), "cancellable process");
    stopped.terminate();
    check(stopped.waitForFinished(3000) && stopped.exitStatus() == Process::CrashExit,
          "process cancellation reaps child");
}
}
int main(int argc, char **argv) {
#ifdef LTREE_USE_QT
    TerminalApplication application(argc, argv);
#else
    (void)argc;
    (void)argv;
#endif
    ::setenv("TZ", "UTC", 1);
    ::tzset();
    try {
        TemporaryDirectory fixture;
        check(fixture.isValid(), "temporary directory");
        strings();
        regularExpressions();
        encoding();
        calendarAndJson();
        operations(fixture.path());
        processes();
        std::cerr << checks << " platform checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Platform regression: " << error.what() << '\n';
        return 1;
    }
}
