#pragma once
class QWidget;

namespace ltree {
void restoreWindowGeometry(QWidget &window);
void saveWindowGeometry(const QWidget &window);
}
