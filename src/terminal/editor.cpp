#include "terminal/editor.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringDecoder>

namespace ltree {
bool terminalBinaryData(const QByteArray &bytes)
{
    // Decodificar primero para no confundir los ceros de UTF-16/32 con datos binarios.
    const auto encoding = QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8);
    QStringDecoder decoder(encoding);
    QString decoded = decoder.decode(bytes);
    if (decoder.hasError()) {
        if (encoding != QStringConverter::Utf8) return true;
        decoded = QString::fromLatin1(bytes);
    }
    qsizetype controls = 0;
    for (const auto ch : decoded) {
        const ushort c = ch.unicode();
        if (!c) return true;
        if ((c < 32 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\b') || c == 127) ++controls;
    }
    return controls * 100 > decoded.size();
}
QStringList terminalEditorCommand(QString *error)
{
    if (error) error->clear();
    QString selected;
    const QString settings = QDir::homePath() + "/.selected_editor";
    QFile file(settings);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = "Cannot read " + settings + ": " + file.errorString();
            return {};
        }
        const QRegularExpression assignment("^\\s*(?:export\\s+)?SELECTED_EDITOR\\s*=\\s*(.*)$");
        const QRegularExpression literal("^(?:'([^']*)'|\"([^\"]*)\"|([^\\s#'\"]+))\\s*(?:#.*)?$");
        while (!file.atEnd()) {
            const auto match = assignment.match(QString::fromUtf8(file.readLine()).trimmed());
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
    if (selected.trimmed().isEmpty()) selected = qEnvironmentVariable("VISUAL");
    if (selected.trimmed().isEmpty()) selected = qEnvironmentVariable("EDITOR");
    if (selected.trimmed().isEmpty()) {
        for (const auto &name : {"sensible-editor", "editor", "nano", "vi", "vim"}) {
            const QString executable = QStandardPaths::findExecutable(name);
            if (!executable.isEmpty()) return {executable};
        }
        if (error) *error = "No editor found. Set SELECTED_EDITOR in ~/.selected_editor or EDITOR.";
        return {};
    }
    // Un archivo ejecutable con espacios es una sola ruta, sin argumentos adicionales.
    const QFileInfo path(selected);
    QStringList command = path.isFile() && path.isExecutable() ? QStringList{selected} : QProcess::splitCommand(selected);
    if (!command.isEmpty()) {
        const QString executable = QStandardPaths::findExecutable(command.first());
        if (!executable.isEmpty()) { command[0] = executable;return command; }
    }
    if (error) *error = "Editor not found or not executable: " + selected;
    return {};
}
}
