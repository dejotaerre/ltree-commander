#pragma once
#include "core/helpdocument.h"
#include "ui/consolefont.h"
#include <QPainter>
#include <QKeyEvent>

namespace ltree {
void drawHelpDocument(QPainter &, const QRect &, HelpDocument &, int cellWidth = 8,
                      int cellHeight = 16, int ascent = 13, const ConsoleFont * = nullptr);
bool helpDocumentKey(HelpDocument &, QKeyEvent *, int columns, int rows);
void helpDocumentScroll(HelpDocument &, int steps, int columns, int rows);
}
