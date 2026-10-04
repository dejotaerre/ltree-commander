# Dependencies and third-party materials

LTree Commander is an independent implementation. Private ZTreeWin manuals, screenshots, configuration and binaries used as behavioral references are excluded from the public source tree.

## Runtime and build dependencies

- **Qt 6.11+**: Core, Widgets, Concurrent, Network and PrintSupport; Qt Test for tests. Qt's open-source licensing depends on the module and version. Check the installed module notices and [Qt licensing documentation](https://doc.qt.io/qt-6/licensing.html) when distributing binaries.
- **ncursesw and pkg-config**: experimental terminal backend, enabled by default and optional through LTREE_BUILD_TERMINAL. ncurses provides screen handling and terminfo; see its [upstream license notices](https://invisible-island.net/ncurses/ncurses-license.html). The terminal mode reads the emulator’s font and palette and does not bundle a new font.
- **QTermWidget 6**: terminal emulator. Upstream 2.4.0 declares **GPL-2.0-or-later**, with some files under LGPL-2.0-or-later and BSD-3-Clause; see its [README](https://github.com/lxqt/qtermwidget/blob/2.4.0/README.md) and [LICENSE](https://github.com/lxqt/qtermwidget/blob/2.4.0/LICENSE). Distribution package metadata can differ from the overall upstream notice; this project does not relicense QTermWidget.
- **kbd** and **gzip**: the DOS-style grid reads an installed PSF2 font. The build generates a TrueType outline version for the integrated terminal using the same glyphs. kbd 2.10.0 declares GPL-2.0-or-later at the project level; individual font data may carry their own terms. See [kbd](https://github.com/legionus/kbd/tree/v2.10.0). Do not assume that a generated font is exempt from the input font's terms.
- **Python 3 and fontTools**: used by tools/build_console_font.py to generate the font locally. fontTools is a build dependency, not bundled code. Its project contains MIT/BSD notices; see [fontTools license](https://github.com/fonttools/fonttools/blob/main/LICENSE).
- **libarchive**: native readers and ZIP writer, linked from the installed library. Upstream includes several file-specific BSD notices and additional notices; consult its [COPYING](https://github.com/libarchive/libarchive/blob/master/COPYING) and the distribution license files when packaging. No library source or binaries are bundled.
- **GNU diff**: external executable used by the text comparison viewer. See [GNU Diffutils](https://www.gnu.org/software/diffutils/).
- **Sublime Text**: optional external editor, launched through the user's installed subl executable. No Sublime code or binary is included.

## Font provenance and public source distribution

Default input: /usr/share/kbd/consolefonts/default8x16.psfu.gz, supplied by the user's distribution. Source package: kbd. The converter is project code and creates outlines from the installed bitmap, preserving Unicode mapping, advance widths and pixel boundaries. It uses the family name LTree Console and fixed internal timestamps.

The public source distribution excludes resources/*.ttf, PSF data and generated build artifacts. Each user generates the terminal font from their installed font when building. An alternate input can be selected with LTREE_PSF_FONT. Both the grid and terminal use that input. Distributing generated binaries or font files needs a separate review of the applicable input-font and dependency notices; this document does not authorize such redistribution.

## Project license

LTree Commander's original code, documentation and project-created assets are licensed under the [MIT License](LICENSE), copyright 2026 Hector De Armas (dejotaerre). Dependencies remain under their respective authors' licenses. This source release includes no prebuilt binaries, font data or dependency source code.

QTermWidget's GPL-2.0-or-later terms apply when distributing the combined executable; the project MIT license does not remove those obligations. Building from these MIT sources requires the installed dependencies. Anyone packaging binaries must also provide the applicable dependency notices, corresponding sources and other materials required by their licenses, and review the input-font terms.

The interface and workflow acknowledge ZTreeWin / ZTreeBold by ZEDTEK, Inc., and XTree / XTreeGold for DOS. The names identify the behavioral references; this project is not affiliated with or endorsed by their owners. No original code, manuals, logos, binaries or private user configuration are distributed.
