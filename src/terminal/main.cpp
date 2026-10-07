#include "platform/platform.h"
#include "terminal/terminalui.h"
#include <clocale>
#include <cstdio>
#include <unistd.h>

int main(int argc, char **argv) {
    std::setlocale(LC_MESSAGES, "C");
    bool sizes = false;
    ltree::String directory;
    bool positional = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (!positional && (argument == "--help" || argument == "-h")) {
            std::puts("Usage: ltc [options] [directory]\n\nLTree Commander: terminal file manager for "
                      "Linux.\n\nOptions:\n  -h, --help       Show this help\n  -v, --version    Show "
                      "version\n  --terminal       Terminal interface (always selected in this build)\n  "
                      "--tree-sizes     Show logged branch sizes\n  --new-instance   Accepted for "
                      "compatibility; sessions are independent\n\nThe default directory is the current "
                      "working directory. F1 opens the manual.");
            return 0;
        }
        if (!positional && (argument == "--version" || argument == "-v")) {
            std::puts("ltc " LTREE_VERSION);
            return 0;
        }
        if (!positional && argument == "--") {
            positional = true;
            continue;
        }
        if (!positional && (argument == "--terminal" || argument == "--new-instance"))
            continue;
        if (!positional && argument == "--tree-sizes") {
            sizes = true;
            continue;
        }
        if (!positional && argument == "--fullscreen") {
            std::fputs("--fullscreen applies to the graphical interface.\n", stderr);
            return 2;
        }
        if (!positional && argument.starts_with('-')) {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 2;
        }
        if (!directory.isEmpty()) {
            std::fputs("Only one navigation directory is supported.\n", stderr);
            return 2;
        }
        directory = ltree::String::fromUtf8(argv[i]);
    }
    if (directory.isEmpty())
        directory = ltree::DirectoryPath::currentPath();
    const ltree::FileInfo info(directory);
    if (!info.isDir()) {
        std::fprintf(stderr, "Not a directory: %s\n", directory.toUtf8().constData());
        return 2;
    }
    if (!::isatty(STDIN_FILENO) || !::isatty(STDOUT_FILENO)) {
        std::fputs("Terminal mode requires an interactive terminal.\n", stderr);
        return 2;
    }
    return ltree::runTerminal(info.canonicalFilePath(), sizes);
}
