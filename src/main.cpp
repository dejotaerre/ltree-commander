#include "ui/treewindow.h"
#include "ui/windowgeometry.h"
#ifdef LTREE_HAS_TERMINAL
#include "terminal/terminalui.h"
#endif
#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QFileInfo>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <cstdio>
#include <clocale>
#include <memory>
#include <cstring>

int main(int argc, char **argv)
{
    // Respetar también una plataforma Qt explícita, como offscreen en las pruebas.
    bool terminal = qEnvironmentVariableIsEmpty("DISPLAY") &&
        qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM");
    for (int i = 1; i < argc && std::strcmp(argv[i], "--") != 0; ++i)
        if (std::strcmp(argv[i], "--terminal") == 0) terminal = true;
    // La ruta TUI no inicializa plugins gráficos ni requiere DISPLAY/Wayland.
    std::unique_ptr<QCoreApplication> app;
    if (terminal) app = std::make_unique<QCoreApplication>(argc, argv);
    else app = std::make_unique<QApplication>(argc, argv);
    // Mantener los diagnósticos del sistema en inglés sin cambiar el entorno de la consola X.
    std::setlocale(LC_MESSAGES, "C");
    QCoreApplication::setApplicationName("ltc");
    if (!terminal) {
        QApplication::setApplicationDisplayName("LTree Commander");
        QApplication::setDesktopFileName("ltreec");
        QApplication::setWindowIcon(QIcon(":/icons/ltreec.png"));
    }
    QCoreApplication::setApplicationVersion(LTREE_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("LTree Commander: file manager for Linux. F1 shows available commands.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("directory", "Navigation root; defaults to home (GUI) or the current directory (terminal).");
    parser.addOption({"terminal", "Force the terminal interface; automatic without a graphical environment."});
    parser.addOption({"fullscreen", "Start in fullscreen mode."});
    parser.addOption({"new-instance", "Allow an additional independent instance."});
    parser.addOption({"tree-sizes", "Show logged branch sizes beside the directory tree."});
    parser.process(*app);
    if (parser.positionalArguments().size() > 1) parser.showHelp(2);
    const QString root = parser.positionalArguments().isEmpty() ? (terminal ? QDir::currentPath() : QDir::homePath()) : parser.positionalArguments().first();
    if (!QFileInfo(root).isDir()) {
        std::fprintf(stderr, "Not a directory: %s\n", qPrintable(root));
        return 2;
    }
    const QString canonical = QFileInfo(root).canonicalFilePath();
    const QString runtime=QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QLockFile instanceLock(runtime+"/ltreec.lock");
    instanceLock.setStaleLockTime(0);
    QLocalServer instanceServer;
    if(!parser.isSet("new-instance")){
        if(runtime.isEmpty() || !QDir().mkpath(runtime)){
            std::fprintf(stderr,"Cannot access the runtime directory.\n");return 2;
        }
        if(!instanceLock.tryLock(0)){
            if(instanceLock.error()!=QLockFile::LockFailedError){
                std::fprintf(stderr,"Cannot acquire the instance lock.\n");return 2;
            }
            // El servidor puede estar iniciándose en un lanzamiento simultáneo.
            QLocalSocket socket;
            for(int attempt=0;attempt<30;++attempt){
                socket.connectToServer(runtime+"/ltreec.socket");
                if(socket.waitForConnected(100)){
                    socket.write(parser.isSet("tree-sizes")?"activate-tree-sizes\n":"activate\n");socket.waitForBytesWritten(1000);
                    if(socket.waitForReadyRead(1000)) {
                        const auto reply = socket.readAll();
                        if (reply == "activated\n") {
                            std::fprintf(stdout,"Activated the existing LTree Commander instance.\n");return 0;
                        }
                        if (reply == "terminal-running\n") {
                            std::fprintf(stderr,"LTree Commander is running in another terminal. Use --new-instance for an independent session.\n");return 1;
                        }
                    }
                    std::fprintf(stderr,"LTree Commander is already running, but its window could not be activated.\n");return 1;
                }
                socket.abort();QThread::msleep(100);
            }
            std::fprintf(stderr,"LTree Commander is already running, but its window could not be activated.\n");return 1;
        }
        instanceServer.setSocketOptions(QLocalServer::UserAccessOption);
        QLocalServer::removeServer(runtime+"/ltreec.socket");
        if(!instanceServer.listen(runtime+"/ltreec.socket")){
            std::fprintf(stderr,"Cannot start the instance server: %s\n",qPrintable(instanceServer.errorString()));return 2;
        }
    }
    if (terminal) {
        if (parser.isSet("fullscreen")) { std::fprintf(stderr,"--fullscreen applies to the graphical interface.\n");return 2; }
#ifdef LTREE_HAS_TERMINAL
        QObject::connect(&instanceServer, &QLocalServer::newConnection, app.get(), [&] {
            while (auto *socket = instanceServer.nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                const auto reply = [socket] {
                    if (!socket->canReadLine()) return;
                    socket->readLine();socket->write("terminal-running\n");socket->disconnectFromServer();
                };
                QObject::connect(socket, &QLocalSocket::readyRead, app.get(), reply);
                if (socket->bytesAvailable()) reply();
            }
        });
        return ltree::runTerminal(canonical, parser.isSet("tree-sizes"));
#else
        std::fprintf(stderr,"This build does not include the terminal interface.\n");return 2;
#endif
    }
    ltree::TreeWindow window(canonical);
    ltree::restoreWindowGeometry(window);
    QObject::connect(app.get(), &QCoreApplication::aboutToQuit, &window, [&window] {
        ltree::saveWindowGeometry(window);
    });
    window.setTreeSizesVisible(parser.isSet("tree-sizes"));
    QObject::connect(&instanceServer,&QLocalServer::newConnection,&window,[&]{
        while(auto *socket=instanceServer.nextPendingConnection()){
            QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
            const auto activate=[socket,&window]{
                if(!socket->canReadLine())return;
                const auto command=socket->readLine();
                if(command=="activate\n" || command=="activate-tree-sizes\n"){
                    if(command=="activate-tree-sizes\n")window.setTreeSizesVisible(true);
                    if(window.isMinimized())window.setWindowState(window.windowState() & ~Qt::WindowMinimized);
                    window.raise();window.activateWindow();
                    socket->write("activated\n");socket->disconnectFromServer();
                }
            };
            QObject::connect(socket,&QLocalSocket::readyRead,&window,activate);
            if(socket->bytesAvailable())activate();
        }
    });
    QObject::connect(&window, &ltree::TreeWindow::openRequested, &window, [&window](const QString &path, bool editor) {
        const QMimeType type = QMimeDatabase().mimeTypeForFile(path);
        const QString suffix = QFileInfo(path).suffix().toLower();
        const QStringList sourceTypes{"php", "phtml", "html", "htm", "css", "js", "ts", "json", "xml", "md", "txt", "cpp", "h", "py", "sh", "ini", "conf"};
        const bool text = type.name().startsWith("text/") || sourceTypes.contains(suffix) || QFileInfo(path).fileName() == ".htaccess";
        bool launched = false;
        if (editor || text) {
            const QString sublime = QStandardPaths::findExecutable("subl");
            if (!sublime.isEmpty()) launched = QProcess::startDetached(sublime, {path}, QFileInfo(path).absolutePath());
        } else launched = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        if (!launched) QMessageBox::warning(&window, "Open file", "Cannot open the file with the selected application.");
    });
    if (parser.isSet("fullscreen")) window.showFullScreen(); else window.show();
    return app->exec();
}
