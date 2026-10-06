#pragma once
#include <QStringList>
#include <QByteArray>
namespace ltree {
QStringList terminalEditorCommand(QString *error = nullptr);
bool terminalBinaryData(const QByteArray &bytes);
}
