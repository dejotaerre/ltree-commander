#pragma once
#include "platform/platform.h"
namespace ltree {
StringList terminalEditorCommand(String *error = nullptr);
bool terminalBinaryData(const Bytes &bytes);
}
