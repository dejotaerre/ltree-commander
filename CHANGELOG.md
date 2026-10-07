# Changelog

## License update — 2026-10-06

- License the current original code, documentation and project-created assets under GNU GPL version 3 or later (`GPL-3.0-or-later`). Replace LICENSE with the complete GPLv3 text and update the README, alpha status and dependency notices.
- Dependencies retain their own licenses. Revisions previously published under MIT retain their original licensing terms.

## 0.1.0-alpha.1 — 2026-10-03

First functional alpha of LTree Commander for Linux.

### Terminal-only build without Qt — 2026-10-06

- `LTREE_BUILD_GUI=OFF` builds and runs without Qt, QTermWidget or the graphical font toolchain. Both interfaces remain enabled in the default build.
- Shared file-operation algorithms now use a platform interface. The graphical build keeps its Qt implementation; terminal-only builds use C++/POSIX, ICU, PCRE2 and nlohmann JSON for Unicode, regex, processes, JSON histories and background work.
- Terminal-only startup selects the TUI directly and allows independent sessions. All terminal commands remain available.
- Build documentation separates graphical and terminal dependencies, with a Qt-free recipe using Debian 12's standard packages.
- Graphical tests, terminal PTY suites and platform comparisons cover the two configurations. A clean Debian 12 build verifies that no Qt packages or libraries are required.

### Paired branch-size markers — 2026-10-04

- The optional tree-size column closes complete totals with ] and partial totals with }, keeping its 14-cell width in both panels.

### Graphical clock seconds — 2026-10-04

- The main graphical header displays HH:mm:ss and updates its row each second. The path leaves room for the full timestamp.

### Remember graphical window geometry — 2026-10-04

- Normal graphical exits save the normal window size and maximized/fullscreen state under ~/.config/ltreec/window.ini, respecting XDG_CONFIG_HOME. The next graphical start restores them; --fullscreen overrides the saved state.
- X11 also restores saved placement when accepted by the window manager. Wayland restores size and state while the compositor controls placement. Minimized windows reopen visible; invalid data retains defaults and restored sizes adapt to the available screen.
- Terminal mode, help/version, invalid-path exits and activation of an existing instance leave these settings unchanged.

### Navigable F1 manual — 2026-10-04

- Replaces clipped help lists with a classic blue/yellow manual: workspace diagrams, chapter headings, highlighted command keys, examples, a contents page and a scroll indicator.
- Sixteen graphical chapters open at the relevant context in the viewer, archives, comparison, Prune, Graft, timestamps and statistics. Tab opens contents; arrows/pages and the mouse wheel scroll; Left/Right changes chapters; F finds text across the manual and Space repeats.
- Esc/F1 returns to the existing view or prompt without changing tags, selection, queries or operation options. Terminal help uses three dedicated chapters and supports the same keyboard navigation, including F1 from its viewer.

### Optional regular expression searches — 2026-10-04

- F4 adds Regex to tagged-file, viewer and archive-member search modes; F2 retains case sensitivity. Invalid patterns remain editable and leave tags and the last executed search intact.
- Unicode Qt/PCRE2 expressions run per line with anchors, groups, alternatives, lookarounds and Unicode classes. Streaming handles block boundaries and UTF-8/UTF-16/UTF-32 input, with bounded line memory and matching resources.
- V inherits Regex; Space and +/- navigate and highlight variable-length matches, advance safely past zero-length matches and retain the query across tagged files/reloads. Regex switches byte views to Alpha. Queries use the existing histories.

### Executable name — 2026-10-04

- The executable is now ltc (Linux + Tree + Commander). Desktop launch commands and binary tests use the new name.
- Existing histories under ~/.config/ltreec and the desktop/icon identity remain compatible. The installed ltreec command is retained as a compatibility link to ltc.

### Desktop launcher and icon — 2026-10-04

- Original SVG/PNG icon and desktop launcher template in resources. The graphical application embeds its icon and identifies itself as ltreec for desktop integration.
- Launching from the desktop uses the existing single-instance behavior; keyboard shortcuts are preserved.

### New date file commands — 2026-10-04

- N stamps the current file; Ctrl+N stamps tagged files in the visible list. F4 Menu then N also selects the tagged command.
- Classic STAMP prompt with editable date/time, F2 Now, Tab Current, F3 Last, F4 written/accessed/both and F5 set/adjust/increment modes. Date-only/time-only inputs retain the other component; signed adjustments and even-second rounding follow the reference syntax.
- Up/Down opens persistent New date history in ~/.config/ltreec/stamp-history.json. Both F8 panes and cached locations refresh while retaining view, selection and tags.
- Linux modification/access times are editable; creation/change times are not offered. Files changed since opening, links and special files are skipped with a completion summary; Esc cancels remaining items.

### Destination refresh after file transfers — 2026-10-04

- Copy and Move update both F8 panes and saved logged locations after each file, including newly created subdirectories. Tab and returning to a visited location retain the updated listing.
- Both panes refresh file/byte statistics while preserving their view, selection, Filespec and remaining tags.

### Preserve GNOME shortcuts — 2026-10-04

- Enter opens the selected recognized archive and Esc returns to the same file list. Enter on ordinary files retains Tree/File navigation.
- Alt+F5 is removed from archive menus/handling and GNOME's original unmaximize binding is restored. Desktop shortcuts are preserved.

### Archive formats and editable destination — 2026-10-04

- Creation/extraction prompts now support normal cursor movement, selection, Delete and clipboard editing with a block cursor.
- F3 Format selects ZIP, TAR, TAR.GZ, TAR.BZ2, TAR.XZ, 7Z, GZ, BZ2 or XZ and updates the filename extension. Typed extensions also select the writer.
- GZ/BZ2/XZ require one source file; multi-file jobs can use a TAR container. Publication, collision confirmation and cancellation protection apply to every format.

### Graphical viewer, archives and Hex editing — 2026-10-04

- Classic VIEW COMMANDS / CTRL / ALT menus with highlighted shortcuts; Alpha/Dump/Hex/Junk/Wrap, charset, line/mask/ruler, tab widths, bookmarks, scrolling and reload controls.
- Gather to clipboard, UTF-8 append files and the native Linux print dialog; persistent viewer search/output history. Alternate editor/viewer commands via LTREE_ALT_EDITOR / LTREE_ALT_VIEWER.
- Ctrl+V traverses tagged files and carries searches across them. X/Alt+X opens an integrated or separate Kitty shell from the graphical viewer.
- H then E overwrites visible bytes. Tab selects Hex/character input, F8 undoes the page, Enter confirms saving, Esc discards. External changes, unsafe paths and special files are rejected; file size, inode and hardlinks are preserved.
- Enter opens an archive browser: directories, tags, Filespec, member view, content search, password and extraction with paths/overwrite confirmation. F5/Ctrl+F5 creates a deflated UTF-8 ZIP from current/tagged files.
- libarchive handles ZIP, TAR/filter streams, 7z and supported RAR variants. Unsafe paths, links and special members are rejected on extraction. Members are read only until extracted; in-place archive changes and encrypted ZIP creation remain pending.
- Alt+K or F4/F4/K now provides the compare-filter alternative; Alt+F5 remains available to GNOME. These additions target the graphical UI; the terminal prototype retains its prior scope.

### Experimental terminal increment

- Without DISPLAY/WAYLAND_DISPLAY or an explicit Qt platform, startup selects terminal mode automatically. `--terminal` forces it within a graphical session.
- `--terminal` runs a native ncursesw interface in the current terminal without initializing Qt's graphical platform. Its default navigation root is the current working directory.
- Initial scope: Tree/Directory/Branch/Showall, logging, tags, Filespec, Alt+F formats, F8 panes, mount/path selection and text/hex viewing. File operations, content search, JFC, X and persistent history remain GUI features.
- F7 Autoview keeps the file list visible beside an asynchronous text/hex preview, including the active F8 pane. Selection changes discard stale reads. Shift+H/A switches mode, Shift+arrows/pages/Home/End scrolls, and Alt+Left/Right/Home adjusts width. F7/Enter/Esc restores the original view.
- GUI and terminal modes share single-instance enforcement; `--new-instance` permits an independent session. Normal exit and handled termination restore terminal input modes.
- Terminal font comes from the emulator. The classic palette uses extended 256-color indices or direct RGB when advertised by terminfo; 8/16-color terminals use their available palette. The terminal palette is never redefined. The backend can be excluded with `-DLTREE_BUILD_TERMINAL=OFF`.

### Viewer search handoff

- V inherits the last executed tagged-file search, including query, case setting and Text/Hex/Unicode mode. It opens at the first match; Space repeats and +/- selects direction.
- Closing/reopening V or selecting another F8 pane keeps the batch search. Cancelled or invalid drafts leave it intact; an explicit viewer search replaces it locally.
- Batch searches with internal `*` wildcards retain their line boundaries in the viewer. Reload repeats the inherited search from the beginning. The footer shows the active query.

### File display increment

- Alt+F in Tree and file lists cycles Name, Name/size/attributes, Details/date and Long name display modes; F4/F4/F also works.
- Compact modes use multiple columns, filled top to bottom, with matching keyboard paging, mouse selection/opening and resize behavior. Display mode is shared across F8 panes while selection, tags, Filespec and sorting are preserved.
- Shift+Left/Right adjusts the extension field and Shift+Home restores it. Full names retain their adjacent extension in Long name mode; narrow Details views reduce date/time fields to fit.
- Extended statistics report the current file capacity and name/extension widths.

### Graft increment

- Alt+G in Tree moves the whole physical branch to an existing containing directory, including unlogged contents independently of tags and Filespec.
- Destination prompt includes Tab paths, F2 tree browsing, F3 last destination and persistent history under ~/.config/ltreec/graft-history.json.
- Same-filesystem moves use no-replacement rename; cross-filesystem moves use GNU mv with inherited parent descriptors. Existing destination branches are never replaced or merged.
- Completed moves update both panes, tags and cached locations; interrupted transfers retain available source/destination contents and report recovery paths.

### Prune increment

- Alt+P in Tree prunes the selected logged branch after typing PRUNE; incomplete branches require an additional explicit Y confirmation.
- F5 keeps current directory files, F6 removes empty directories only, F2 permits read-only file deletion and F4 uses Linux Trash. Options default to no.
- Symbolic link targets, unlogged directories and mount points are retained; cancellation, failures and completed removals update both panes and cached locations.

### Filespec history increment

- F → Up/Down opens the same history list used for content search, with retrieval, editing, paging, marks, bookmarks, filters, sorting and append.
- Search and Filespec histories persist independently under ~/.config/ltreec, respecting XDG_CONFIG_HOME. Existing search history is imported from the earlier data directory without removing the original.
- F3/F4 in either history list save/reload; F3 in either prompt retrieves the latest entry.

### Content-search progress increment

- Selection follows the file being read while preserving the file list and F8 panes.
- Search footer displays query, file path, matching-file hits, animated spinner, cyan progress bar, percentage, elapsed time, estimated time left and read speed.
- In-file progress uses actual block reads; early matches complete that file’s work without inflating read speed. Errors retained as tagged files do not count as hits.

- DOS 8x16 grid, blue palette, highlighted keys, block cursors and resizable native Qt window.
- Tree, Directory, Branch and Showall, logged branch loading, Filespec, tags and two independent F8 panes.
- Literal/Hex/Unicode content search on tagged files, persistent search history and Sublime integration.
- File viewer, F7 preview and J comparison of text and binary files with byte differences.
- Integrated X terminal, mount selector L, extended statistics ?, optional --tree-sizes and single-instance operation.
- M creates directories in Tree; C/M copy/move files; Ctrl+C/Ctrl+M operate on tagged files; Alt+C/Alt+M retain path structure and support filename masks.
- D and Ctrl+D delete with confirmations, retain selection and verify empty branches before directory removal.
- A/Alt+A edit POSIX permissions of the current object; Ctrl+A applies modes to tagged files.
- R/Ctrl+R rename current objects/tagged files with review, collision checks and pane/tag updates.
- C/Alt+C in Tree compare a directory/logged branch using name, size, timestamp and binary criteria.
- Compare filters for duplicates, unique names, size, content and dates; branch tagging shortcuts Ctrl+F7/F8/F9.
- Local, reproducible terminal-font generation; test shells use temporary history files.

### DIR COMMANDS increment

- A displays available space without changing the active location.
- G/Ctrl+G aggregate logged files/tagged files across locations, preserve Filespec and synchronize operations with saved sessions and F8 panes.
- H creates a directory symbolic link at a typed destination, with collision and source-identity checks.
- < / > and comma/period cycle logged locations and restore their state. Directory Oops is omitted by request.

### Information display adjustment

- Alt+I shows a compact information box at the bottom right, preserving the file list, scroll, tags, menus and normal commands. The box follows selection and fits the right pane in F8 Split.

### Validation

182 functional Qt tests, plus initialization and cleanup after the DIR COMMANDS increment and information display adjustment. Full suite passed offscreen; new UI flows also passed on Qt Wayland with prompt captures reviewed at 80x25 and with split panes. These are synthetic tests; automated differential compatibility testing against ZTreeWin has not been completed.

### Known limits

This alpha implements a subset of the full compatibility roadmap. Recursive directory permissions, advanced rename masks/sequences/find-replace, comparison tolerances, size tolerance, in-place archive updates, macros, configuration pages, recycle-bin operations remain future work. The file viewer/J comparator is limited to 32 MiB; directory binary comparison reads in blocks. Large duplicate-content groups can be slow.

Symlinks and special files are rejected by copy/move/rename/permission operations; deletion of a link removes the link itself. Cross-filesystem movement preserves regular rwx and access/modification times, but does not preserve owners, ACLs, xattrs, special permission bits or hard-link relationships. Completed operations remain completed if a batch is cancelled. Empty source directories are retained by move. Abrupt interruption can leave .ltree-move-* or .ltree-rename-* items; there is no automatic recovery journal or undo. Concurrent external writers are not locked.

The initial alpha preparation preceded the public source release on 2026-10-04 and the GPL license update on 2026-10-06.

## Public source alpha — 2026-10-04

- Publish 0.1.0-alpha.1 under the MIT license for the original project work.
- Acknowledge ZTreeWin / ZTreeBold by ZEDTEK and the XTree / XTreeGold DOS lineage.
- Add a public README with screenshots captured from synthetic examples, build/run instructions and explicit alpha limits.
- Keep private reference materials, extracted reference inventories, user configuration, font data and build artifacts outside the public repository.
- Distribute sources only; document QTermWidget GPL obligations for anyone packaging combined binaries.
