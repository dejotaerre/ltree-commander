#include "platform/platform.h"
#include "terminal/editor.h"

namespace ltree {
bool terminalBinaryData(const Bytes &bytes)
{
    // Decodificar primero para no confundir los ceros de UTF-16/32 con datos binarios.
    const auto encoding = TextEncoding::encodingForData(bytes).value_or(TextEncoding::Utf8);
    TextDecoder decoder(encoding);
    String decoded = decoder.decode(bytes);
    if (decoder.hasError()) {
        if (encoding != TextEncoding::Utf8) return true;
        decoded = String::fromLatin1(bytes);
    }
    Index controls = 0;
    for (const auto ch : decoded) {
        const ushort c = ch.unicode();
        if (!c) return true;
        if ((c < 32 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\b') || c == 127) ++controls;
    }
    return controls * 100 > decoded.size();
}
StringList terminalEditorCommand(String *error)
{
    if (error) error->clear();
    String selected;
    const String settings = DirectoryPath::homePath() + "/.selected_editor";
    File file(settings);
    if (file.exists()) {
        if (!file.open(IO::ReadOnly)) {
            if (error) *error = "Cannot read " + settings + ": " + file.errorString();
            return {};
        }
        const Regex assignment("^\\s*(?:export\\s+)?SELECTED_EDITOR\\s*=\\s*(.*)$");
        const Regex literal("^(?:'([^']*)'|\"([^\"]*)\"|([^\\s#'\"]+))\\s*(?:#.*)?$");
        while (!file.atEnd()) {
            const auto match = assignment.match(String::fromUtf8(file.readLine()).trimmed());
            if (!match.hasMatch()) continue;
            // Leer una asignación literal sin ejecutar el archivo como un script.
            const auto value = literal.match(match.captured(1));
            if (!value.hasMatch()) {
                if (error) *error = "Invalid SELECTED_EDITOR in " + settings;
                return {};
            }
            selected = value.captured(1) + value.captured(2) + value.captured(3);
        }
    }
    if (selected.trimmed().isEmpty()) selected = environment("VISUAL");
    if (selected.trimmed().isEmpty()) selected = environment("EDITOR");
    if (selected.trimmed().isEmpty()) {
        for (const auto &name : {"sensible-editor", "editor", "nano", "vi", "vim"}) {
            const String executable = Paths::findExecutable(name);
            if (!executable.isEmpty()) return {executable};
        }
        if (error) *error = "No editor found. Set SELECTED_EDITOR in ~/.selected_editor or EDITOR.";
        return {};
    }
    // Un archivo ejecutable con espacios es una sola ruta, sin argumentos adicionales.
    const FileInfo path(selected);
    StringList command = path.isFile() && path.isExecutable() ? StringList{selected} : Process::splitCommand(selected);
    if (!command.isEmpty()) {
        const String executable = Paths::findExecutable(command.first());
        if (!executable.isEmpty()) { command[0] = executable;return command; }
    }
    if (error) *error = "Editor not found or not executable: " + selected;
    return {};
}
}
