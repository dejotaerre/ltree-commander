# Terminal mode

Run `ltc --terminal [directory]` in a terminal or over SSH. The same filesystem engines are used by the graphical and ncurses interfaces. Both preserve tags, nearby selection and refreshed destination panels after file operations.

## Keyboard menus

Kitty's extended keyboard protocol is detected automatically, including over SSH. Hold Ctrl or Alt to display its command menu; releasing the modifier restores the normal menu. Pressing a command while holding the modifier runs that command. This also works in the viewer. No Kitty configuration changes are required.

LTC restores the previous keyboard protocol before opening an external editor or shell and before exiting. Other terminals keep their existing keyboard behavior. If a desktop shortcut intercepts a key, use the command menus below.

`F4` selects the normal, Ctrl or Alt command menu. Type the command letter once; the menu returns to normal after that operation or any unrelated key. `Esc` cancels the selected menu. In the viewer, use `F10` instead, because `F4` marks a Gather selection.

Some traditional terminals send identical bytes for Ctrl+I/Tab, Ctrl+M/Enter and Ctrl+J/Line Feed. Use **F4 then I/M/J** for Invert, tagged Move and tagged file comparison. **F4 then Enter** filters tagged files. Ctrl+function keys and modern modified key sequences work when the terminal transmits them. Desktop shortcuts remain unchanged; use the menus when the desktop intercepts a key.

## File manager

| Scope | Commands |
| --- | --- |
| Tree | A Avail, B Branch, C Compare directory, D Delete empty directory, F Filespec, G Global, H Symbolic link, M Make directory, R Rename, S Showall, X Shell, ? Statistics |
| Files | A Permissions, C Copy, D Delete, E/O Edit, F Filespec, J Compare files, M Move, N New date, R Rename, T/U Tag/Untag, V View, X Shell |
| Ctrl files | A/C/D/M/N/R on tagged files, S Search tagged contents, J Compare tagged pair, I Invert, V View tagged sequence, T/U Tag/Untag all, Enter Tagged-only, F Previous Filespec |
| Alt tree | A Permissions, C Compare branch, G Graft branch, P Prune, I Information, F Display, S Sort |
| Alt files | C/M Copy/Move tagged files with paths, I Information, K/F4 Comparison filter, F Display, S Sort, V Alternative viewer |

- `+` logs the selected directory; `*` logs its branch; `-` unlogs. F3 refreshes; Alt+F3 relogs. Tree F5/F6 collapse controls remain available.
- Enter switches tree/file views and opens archives. Backspace returns to the tree/parent, including from an empty file list.
- F8 splits the display; Tab changes panes. Shift+F8 offers swapping and statistics panel preferences. Ctrl+F6 combines tags. Ctrl+F7/F8/F9 tag, untag or invert the tree branch.
- L selects a mounted location or typed path; `<`/`>` and comma/period cycle registered roots, retaining their state. Tab/Shift+Tab select sibling directories in a single tree pane; Ctrl+Tab works when its distinct sequence is available.
- Shift+letters and digits spell a name incrementally; `"` finds the next match. `|` opens the editable Spell prompt. Shift+Backspace/Esc erase/reset when the terminal sends distinct sequences.
- Shift+Left/Right/Home adjust/reset the extension column. F7 Autoview uses Shift+letters/function keys for viewer commands, Shift+navigation to scroll and Alt+Left/Right/Home for its width.
- POSIX permissions or actual DOS HRSA attributes are displayed according to the filesystem, with symbolic links indicated separately. A edits POSIX modes; N changes Written/Accessed/Both timestamps through Set/Adjust/Increment with review before applying.

Transfers offer an editable mask, letter case, destination browsing and replacement policy. Alt+C/M additionally select Full/Current/Relative/Flat paths. Ctrl+D first asks Y/N, then **Confirm delete for each file?**. A successful Branch batch offers a separate confirmation for the empty branch and parent. Failed or skipped files prevent that follow-up. Delete is permanent. Prune requires typing **PRUNE**, with keep-current-files, only-empty, force-read-only and trash options.

## Search and histories

Ctrl+S in files searches tagged contents using Text, Hex, Unicode or line-based Regex. F2 changes case sensitivity; F4 changes search mode. The file selector follows the current file. The footer shows Hits (matching files), a moving bar, percentage, elapsed/remaining time, throughput and spinner. Esc cancels; unprocessed tags remain. Read errors offer retry, keep/untag and cancellation choices.

Opening a result with V retains the query, mode and case setting; Space/+ and - navigate matches.

Up/Down opens selectable histories in prompts; F3 retrieves the latest entry. The popup provides navigation, filtering, marks, sorting, deletion, retrieval/append and save/load. Marked entries must be unmarked before deletion.

Histories persist below `${XDG_CONFIG_HOME:-~/.config}/ltreec`: `search-history.json`, `filespec-history.json`, `stamp-history.json`, `graft-history.json`, `gather-history.json` and `viewer-history.json`. Their formats are shared with the graphical interface. `LTREE_HISTORY_FILE` overrides the search history path for isolated tests.

## Viewer

| Keys | Operation |
| --- | --- |
| A / D / H / J | Alpha / decimal Dump / Hex / printable ASCII Junk |
| C / Shift+C | Cycle charset forwards/backwards: automatic, CP437, Windows-1252, UTF-8, UTF-16LE/BE, EBCDIC |
| W / R / L / M / Tab | Wrap, ruler, line selector, character mask, tab width |
| Arrows, PgUp/PgDn, Home/End | Scroll; Left/Right scroll horizontally; Ctrl variants accelerate or reach line edges |
| O / digits | Jump to line/offset, decimal/hexadecimal, absolute/relative |
| F / B / S / F9 / slash / backslash | Find forward/backward/on-page with search mode/history |
| Space / + / - / F8 | Next/previous match; F8 toggles hit/page navigation |
| F10 → Ctrl/Alt → 0..9 | Set/retrieve bookmarks; direct Ctrl/Alt digits when available |
| N / P / U | Next/previous file; U untags and advances |
| Ctrl+V / Ctrl+S | Tagged view sequence / toggle recent queries |
| F3 / Ctrl+F3 | Reload / continuous reload |
| Shift+F2..F6 / Shift+F7 | Autoscroll speeds / loop |
| G / F4 | Mark selection start/end; write/append file or copy/append/clear clipboard |
| Ctrl+C / F5 / F2 / F6 | Copy line/selection / append / clear / gap |
| Alt+P | Print to a file or pipe command (`| command`) |
| Alt+V / Alt+E | Run `LTREE_ALT_VIEWER` / `LTREE_ALT_EDITOR`, a terminal program |
| Alt+F5/F6 | Text/background colors, also available through the F10 Alt menu |
| E | External text editor or safe internal Hex editor |
| X / Alt+X | Shell in the file's directory |

The text editor is selected using `.selected_editor`, VISUAL, EDITOR or the system fallback. File-list O also edits in terminal mode; viewer O remains Offset. Binary files use the internal Hex editor. Changes require confirmation; N at the save prompt keeps the pending edit. External modifications are detected before saving.

Both backends currently limit full viewer/member reads to 32 MiB. Regex is line-based and resource bounded. Archive members are read-only until extracted.

## Archives and comparison

Enter opens a supported archive in a directory/file browser. Navigate folders, Branch, Filespec and tags; V views a member. C/E extracts the selection, Ctrl+C/E tagged members, Alt+C/E tagged members with paths. F9 accepts a reader password. Ctrl+S searches member contents. F3 reloads.

File-list F5 creates an archive from the current file; Ctrl+F5 uses tagged files. F3 in that prompt chooses **ZIP, TAR, TAR.GZ, TAR.BZ2, TAR.XZ, 7Z, GZ, BZ2 or XZ**; the destination name is editable. F2 controls paths; F4 controls replacement. GZ/BZ2/XZ contain one file; use a TAR format for multiple files. In-place updates and encrypted creation are unavailable in both backends.

J/Ctrl+J compares files in text/Hex with split layouts, per-byte/row differences and next/previous navigation. C/Alt+C in the tree compares a directory/branch using presence, date, size and binary options. Alt+K filters duplicates/unique/size/content/date/newest/oldest. Alt+S offers the complete sort chooser.

## Environment-dependent differences

Window position, pixel size, zoom and font belong to the terminal emulator/compositor. Terminal colors and key transmission depend on TERM and the emulator. A desktop clipboard is used through an installed `wl-copy` or `xclip` when available; the internal clipboard and Gather-to-file remain usable without a desktop. Printing uses files or an installed command instead of a Qt printer dialog. X temporarily hands the existing terminal to a shell and restores the file manager on return.

## Automated validation

CTest includes four real pseudoterminal suites covering transfers, deletion/Prune/Graft, two-pane refresh, tags/Global, search/regex/history, viewer/reload/Autoview/Hex editing, archives in nine formats, comparison, shell integration, contextual help and Kitty keyboard press/release events. Each run uses disposable files and a private HOME/config/runtime/history, without a desktop or personal shell history.
