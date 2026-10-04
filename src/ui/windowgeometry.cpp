#include "ui/windowgeometry.h"
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>

namespace ltree {
namespace {
QString settingsFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/ltreec/window.ini";
}
bool wayland()
{
    return QGuiApplication::platformName().startsWith("wayland");
}
}

void restoreWindowGeometry(QWidget &window)
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QSize size = settings.value("normalSize").toSize();
    if (!size.isValid() || size.isEmpty()) return;
    bool restored = false;
    if (!wayland()) restored = window.restoreGeometry(settings.value("geometry").toByteArray());
    if (!restored) {
        QSize available = size;
        if (window.screen()) available = available.boundedTo(window.screen()->availableGeometry().size());
        window.resize(available.expandedTo(window.minimumSize()));
    }
    // Wayland decide la posición; solo se restauran tamaño y estados compatibles.
    Qt::WindowStates state;
    if (settings.value("maximized", false).toBool()) state |= Qt::WindowMaximized;
    if (settings.value("fullscreen", false).toBool()) state |= Qt::WindowFullScreen;
    window.setWindowState(state);
}

void saveWindowGeometry(const QWidget &window)
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QSize normal = window.normalGeometry().isValid() ? window.normalGeometry().size() : window.size();
    settings.setValue("normalSize", normal);
    settings.setValue("maximized", window.isMaximized());
    settings.setValue("fullscreen", window.isFullScreen());
    if (!wayland()) settings.setValue("geometry", window.saveGeometry());
    else settings.remove("geometry");
    settings.sync();
    if (settings.status() != QSettings::NoError)
        qWarning("Cannot save window geometry to %s", qPrintable(settingsFile()));
}
}
