# 0.1.0-alpha.1

First public source alpha of **LTree Commander**, a native Linux clone aiming to reproduce ZTreeWin / ZTreeBold by ZEDTEK as faithfully as possible. The main behavioral reference is ZTreeWin 2.2.19. Full compatibility remains a development goal.

## Release contents

- C++20 / Qt 6 graphical file manager, optional ncurses terminal backend, tests, build tools, documentation and project icons.
- MIT license for the original project work; dependencies retain their own terms. See [THIRD_PARTY.md](../THIRD_PARTY.md), particularly QTermWidget's GPL requirements when distributing combined executables.
- Screenshots captured from the current graphical program using synthetic example files.
- Sources only: no prebuilt executable, generated fonts, private manuals, reference screenshots, user configuration or original ZTree/XTree code.

## Main capabilities

Tree/Branch/Showall/Global navigation, logging and tags; Filespec and persistent histories; split panels and Autoview; copy/move/delete/rename with supported tagged variants and masks; POSIX permissions, timestamps and symbolic links; Prune/Graft; content search with progress, spinner and optional line-based Regex; text/Hex viewer and byte overwrite editing; comparison; archive browsing, extraction and ZIP/TAR/7Z/compressed stream creation; shell integration and contextual F1 manual.

The graphical window also remembers its normal size and maximized/fullscreen state. X11 placement can be restored; Wayland leaves placement to the compositor. Settings remain in ~/.config/ltreec, respecting XDG_CONFIG_HOME.

## Known limits

- The terminal backend is experimental. It offers navigation, tagging, Filespec, split panels, Autoview and text/Hex viewing. It does not yet offer content search or file mutation commands.
- The graphical viewer loads at most 32 MiB. Regex is evaluated by line, with bounded resources; multiline matching is not implemented.
- No complete ZTree configuration/macro compatibility, advanced rename masks, recovery journal or general undo.
- Ordinary Delete is permanent; Prune optionally uses trash. Canceling a batch preserves completed operations. Interrupted cross-filesystem moves can leave partial results requiring manual review.
- Recursive permission editing, ACL/owner management, tolerance rules and some Windows-specific commands remain incomplete or deliberately adapted to Linux.
- Archive members are read-only; extract, edit and create a new archive. Encrypted archive creation and in-place archive updates are not implemented. RAR variants and physical printing still need broader testing.
- GNU diff and mv are required for their respective comparison/Graft paths. Sublime Text and Kitty integration requires the external programs to be installed.
- Tested on CachyOS x86-64 with Qt 6.11.2, GNOME/Wayland and XWayland. ARM64/Raspberry Pi 5 has not yet been tested.

## Validation

The release checks cover the main file manager, viewer/archives, timestamp changes, manual and window geometry using isolated synthetic fixtures. Debug/Release and selected native Wayland checks were run during development. Real PTY tests covered terminal startup, navigation, color handling, F7 Autoview and F1.

For the publication, the public source selection is rebuilt from scratch in Release and all five CTest suites are run. See the [release notes](https://github.com/dejotaerre/ltree-commander/releases/tag/v0.1.0-alpha.1) for the final result.

To reproduce the automated suite:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Tests use disposable fixtures and isolate the application's histories. Do not point destructive evaluation steps at important files.

## Further reading

[README](../README.md) · [Detailed usage](USAGE.md) · [Viewer and archives](VISOR_Y_COMPRIMIDOS.md) · [Implementation notes](IMPLEMENTACION.md) · [Changelog](../CHANGELOG.md)
