# Build and installation guide

LTree Commander produces one executable, **`ltc`**. The default build includes both interfaces; `LTREE_BUILD_GUI=OFF` builds only the terminal interface. Run build commands from the repository root. Compilation does not require root privileges; package installation commands do.

## Requirements

| Dependency | Required for | Notes |
| --- | --- | --- |
| C++20 compiler | Compilation | GCC 12.2 and newer have been used successfully. |
| CMake 3.22+ | Configuration | Ninja is used in the examples. |
| Qt 6.11+ development files | Graphical build | Core, Concurrent, Widgets, Network and PrintSupport. Graphical tests also need Test. |
| QTermWidget for Qt 6 | Graphical build | CMake package `qtermwidget6`; upstream 2.4.0 is tested. A Qt 5 build is insufficient. |
| ICU, PCRE2-16 and nlohmann JSON development files | Terminal-only build | Unicode, regular expressions and JSON histories; no Qt required. |
| libarchive development files | All builds | Archive reading, creation and extraction. |
| Python 3 and fontTools | Graphical build | Generate the integrated shell's font. Terminal tests need Python 3, without fontTools. |
| PSF2 8×16 font with a Unicode table | Graphical build and runtime | See [console font](#console-font). Keep the selected file installed. |
| pkg-config and ncursesw development files | Terminal backend | Enabled by default; the wide-character library is required. |

**Both interfaces are enabled by default.** Disable the graphical interface with `LTREE_BUILD_GUI=OFF`, or omit terminal mode with `LTREE_BUILD_TERMINAL=OFF`. At least one must remain enabled.

### Terminal mode on a server

`ltc --terminal` needs no graphical desktop, X11 server or Wayland session. The combined executable still loads the libraries linked into it. **Build with `LTREE_BUILD_GUI=OFF` to eliminate Qt completely from both compilation and runtime.** This build uses ICU, PCRE2-16, nlohmann JSON, libarchive, ncursesw and the C++/POSIX runtime. Background work uses standard C++ threads.

A terminal-only executable selects terminal mode even inside a graphical desktop; passing `--terminal` is optional. It retains the file operations, archives, search modes, histories and viewer commands of the terminal backend. Use a separate build directory to keep the graphical build available.

Additional runtime tools:

- GNU `diff` for text comparison and GNU `mv` for Graft across filesystems.
- Bash and coreutils for shell execution; gzip for the graphical font loader.
- A terminal text editor for E/O: the system selection, `~/.selected_editor`, `VISUAL` or `EDITOR`. See [terminal usage](TERMINAL.md).
- Optional Sublime Text (`subl`) for graphical text editing and Kitty for a separate graphical shell. Kitty also improves modifier-key menus in terminal mode; ordinary terminals and SSH remain supported.

The archive engine uses libarchive. The `zip` and `7z` executables are needed by the **test suite**, rather than normal archive operations.

## Arch Linux / CachyOS

Install build and runtime dependencies:

```bash
sudo pacman -Syu --needed gcc cmake ninja git qt6-base qtermwidget \
  libarchive python-fonttools kbd gzip pkgconf ncurses bash coreutils diffutils
```

Check the installed versions and font before configuring:

```bash
cmake --version
c++ --version
pkg-config --modversion Qt6Core libarchive ncursesw
python3 -c "import fontTools; print(fontTools.__version__)"
test -r /usr/share/kbd/consolefonts/default8x16.psfu.gz
```

The Qt version must be at least **6.11**. A missing font or failed Python import must be resolved before building.

## Debian / Ubuntu and other distributions

Installing packages named `qt6-base-dev` and `libqtermwidget6-dev` does **not** guarantee a compatible Qt version. Debian [12 (Bookworm)](https://packages.debian.org/bookworm/qt6-base-dev) provides Qt 6.4.2 and [13 (Trixie)](https://packages.debian.org/trixie/qt6-base-dev) provides Qt 6.8.2. Neither meets the graphical interface's Qt 6.11 requirement. Terminal-only compilation works with Debian 12's standard packages; use the recipe below. Check Ubuntu and other distribution versions before using their packages.

For a graphical build on Debian / Ubuntu, these packages cover the prerequisites other than Qt and QTermWidget:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config \
  libarchive-dev libncurses-dev python3 python3-fonttools gzip \
  bash coreutils diffutils curl
```

**Those commands alone are insufficient to build the graphical interface.** You must also supply Qt 6.11+ development files and QTermWidget built against that Qt installation. Use a compatible distribution package where available, or install them under a separate prefix. A private prefix keeps that SDK separate from the desktop's system Qt.

For a separate SDK:

1. Install or build Qt 6.11+ for the target system. Follow Qt's [Linux build instructions](https://doc.qt.io/qt-6.11/linux-building.html). Include the modules listed above. Building QTermWidget also requires Qt LinguistTools from Qt Tools.
2. Build/install [lxqt-build-tools 2.4.0](https://github.com/lxqt/lxqt-build-tools/tree/2.4.0), then [QTermWidget 2.4.0](https://github.com/lxqt/qtermwidget/tree/2.4.0), against that SDK. QTermWidget needs their development CMake files; a Qt 5 version cannot be substituted.
3. Select a valid font as described below, and pass the SDK prefixes to CMake.

With the dependencies already installed in the example prefixes:

```bash
cmake -S . -B build-private -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH="/path/to/qt6;/path/to/qtermwidget6" \
  -DLTREE_PSF_FONT="$HOME/.local/share/ltreec/fonts/default8x16.psfu"
cmake --build build-private -j 4
./build-private/ltc --version
```

Replace `/path/to/...` with the actual installation prefixes. See Qt's [CMake command-line guide](https://doc.qt.io/qt-6.11/cmake-build-on-cmdline.html). Keep the corresponding shared libraries and Qt platform plugins available when running the executable. A prebuilt Qt SDK must also be compatible with the target system's C library and CPU architecture.

## Build only the terminal interface

**No Qt package, SDK, library, plugin or font converter is needed.**

On Arch / CachyOS:

```bash
sudo pacman -Syu --needed gcc cmake ninja git libarchive ncurses \
  icu pcre2 nlohmann-json pkgconf bash coreutils diffutils
```

On Debian 12 / Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config \
  libarchive-dev libncurses-dev libicu-dev libpcre2-dev nlohmann-json3-dev \
  bash coreutils diffutils
```

From the repository root:

```bash
cmake -S . -B build-terminal -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DLTREE_BUILD_GUI=OFF
cmake --build build-terminal -j 4
./build-terminal/ltc "$HOME"
ldd build-terminal/ltc
```

CMake searches for Qt and QTermWidget only when `LTREE_BUILD_GUI=ON`. The terminal-only build links ICU's Unicode/conversion library, PCRE2's 16-bit library, libarchive and ncursesw; nlohmann JSON is a header dependency. Qt is absent from `ldd` and from the compiled sources. The Qt implementation remains available to the graphical build through the platform interface.

This configuration has been compiled and tested in a clean Debian 12 container without Qt development or runtime packages. The graphical/default build is tested separately. Both execute the same filesystem algorithms, while platform tests check Unicode, regex, timestamps, JSON, file IO and process behavior against the Qt backend.

Keep the non-Qt shared libraries installed on the target. Build on the oldest distribution you intend to support, or in a matching build container; the resulting executable still depends on that system's C library and CPU architecture. Install `build-terminal/ltc` using the user installation instructions below. A terminal-only installation does not need the graphical launcher or console font.

## Console font

The default input is supplied by Arch's `kbd` package:

```text
/usr/share/kbd/consolefonts/default8x16.psfu.gz
```

The converter accepts a compressed or uncompressed **PSF2 8×16 font with a Unicode table**. A PSF1 file or a different glyph size will fail conversion. Installing `kbd` on another distribution may not put this particular font at the default path.

If necessary, obtain the same font from the pinned upstream kbd source:

```bash
install -d "$HOME/.local/share/ltreec/fonts"
curl --fail --location \
  https://raw.githubusercontent.com/legionus/kbd/v2.10.0/data/consolefonts/default8x16.psfu \
  --output "$HOME/.local/share/ltreec/fonts/default8x16.psfu"
```

Then add this argument to the configuration command:

```bash
-DLTREE_PSF_FONT="$HOME/.local/share/ltreec/fonts/default8x16.psfu"
```

That line is an argument to `cmake`, not a separate shell command. The build embeds a generated TrueType font, while the graphical grid also reads the selected PSF file at runtime. **Keep the font at the configured absolute path after installing `ltc`.** See the font provenance and license information in [THIRD_PARTY.md](../THIRD_PARTY.md).

## Configure, build and run

```bash
git clone https://github.com/dejotaerre/ltree-commander.git
cd ltree-commander
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j 4
./build/ltc --version
./build/ltc "$HOME"
./build/ltc --terminal "$HOME"
```

Use fewer parallel jobs, such as `-j 2`, if memory is limited. Without a graphical environment, `ltc` selects terminal mode automatically.

| CMake option | Default | Effect |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | Unset | Use `Release` for normal use or `Debug` for development. |
| `BUILD_TESTING` | `ON` | Build and register tests; Qt Test is needed only for graphical tests. |
| `LTREE_BUILD_GUI` | `ON` | Include the graphical interface and its dependencies. |
| `LTREE_BUILD_TERMINAL` | `ON` | Include ncurses terminal mode. |
| `LTREE_PSF_FONT` | Default kbd path above | Select the font file used to generate and display the console font. |
| `CMAKE_PREFIX_PATH` | System prefixes | Locate Qt and QTermWidget when installed outside the system paths. |

Use a new build directory when changing compiler, SDK or architecture. Both amd64 and arm64 have been built and tested; native ARM64 compilation uses the same recipe and dependencies. An amd64 executable cannot run natively on a Raspberry Pi 5: compile on ARM64 or use a compatible ARM64 package.

## Run the tests

Terminal tests additionally need Python 3, `zip` and a `7z` command. Graphical tests also need Qt Test. On Arch / CachyOS:

```bash
sudo pacman -S --needed zip 7zip
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

For the terminal-only suite on Debian 12, install `python3 zip p7zip-full`, configure with `-DLTREE_BUILD_GUI=OFF -DBUILD_TESTING=ON`, build, then run `ctest --test-dir build-terminal --output-on-failure`. No Qt SDK is required. Other distributions may call the package `7zip`; verify that it supplies `7z`, which the tests invoke.

The graphical tests use Qt's `offscreen` platform and the terminal tests create temporary pseudo-terminals. Run them as your normal user. A test build can take longer and use more memory than the quick start.

## Install for your user

There are currently **no CMake install rules**. Use this manual installation from the repository root, adjusting `build/ltc` if you used another build directory:

```bash
install -Dm755 build/ltc "$HOME/.local/bin/ltc"
export PATH="$HOME/.local/bin:$PATH"
ltc --version
```

Keep `~/.local/bin` in your shell's `PATH` for future sessions. Keep the libraries required by your build installed. The default build needs Qt and QTermWidget; terminal-only builds require ICU, PCRE2-16, libarchive and ncursesw, with no Qt libraries. A graphical build also needs the selected font.

### Desktop launcher and icon

Install `desktop-file-utils` using your package manager. Then run:

```bash
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}"
install -d "$data_dir/applications"
install -Dm644 resources/icons/ltreec.svg \
  "$data_dir/icons/hicolor/scalable/apps/ltreec.svg"
desktop-file-install --dir="$data_dir/applications" \
  --set-key=Exec --set-value="$HOME/.local/bin/ltc --tree-sizes" \
  --set-key=TryExec --set-value="$HOME/.local/bin/ltc" \
  resources/ltreec.desktop
update-desktop-database "$data_dir/applications"
```

The installed launcher uses the absolute executable path, so it also works when the desktop's `PATH` differs from the shell's. Search for **ltc** or **LTree Commander** in your application menu. This procedure does not change global keyboard shortcuts.

## Troubleshooting

| Error or symptom | What to check |
| --- | --- |
| Qt version rejected | For a graphical build, Qt must be 6.11+. Check the SDK selected in `CMakeCache.txt`; use a fresh build directory and the correct prefix. |
| `qtermwidget6` not found | Install the Qt 6 development package, or add its installation prefix to `CMAKE_PREFIX_PATH`. |
| `Python fontTools is required` | Install fontTools for the `python3` selected by CMake. The graphical build requires it even with tests disabled. The terminal-only build does not use it. |
| Missing font or converter assertion | Set `LTREE_PSF_FONT` to an existing PSF2 8×16 file with a Unicode table. |
| `ncursesw` not found | Install wide-character ncurses development files and pkg-config, or set `LTREE_BUILD_TERMINAL=OFF`. |
| ICU / PCRE2 / nlohmann JSON not found | Install `libicu-dev libpcre2-dev nlohmann-json3-dev` on Debian, or `icu pcre2 nlohmann-json` on Arch. |
| Archive tests cannot start `zip` / `7z` | Install both test tools and check their executable names in `PATH`. |
| Shared library or Qt platform plugin missing | Keep the selected SDK's libraries/plugins installed; copying the executable alone is insufficient for a private SDK. |
| C++ compiler killed during a build | Retry with fewer parallel jobs to reduce memory use. |
| `ltc` not found after installation | Use `~/.local/bin/ltc` directly or add `~/.local/bin` to `PATH`. |
