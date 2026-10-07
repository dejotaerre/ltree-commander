#pragma once
class QWidget;
class QKeyEvent;

namespace ltree {
void toggleWindowFullscreen(QWidget &window);
bool handleFullscreenKey(QWidget &window, const QKeyEvent &event);
void restoreWindowGeometry(QWidget &window);
void saveWindowGeometry(const QWidget &window);
}
