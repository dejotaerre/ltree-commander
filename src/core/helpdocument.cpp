#include "core/helpdocument.h"
#include <algorithm>

namespace ltree {
namespace {
QVector<HelpSection> manual()
{
    return {
{HelpTopic::Display, "Main display", R"HELP(
= YOUR WORKSPACE
LTree Commander uses a tree, a file list and a statistics panel. The current path appears above the lists. Commands at the bottom change with the active view and with Ctrl or Alt.

```
           Current path                    Clock
 ┌───────────────────────────────┬─────────────┐
 │                               │ FILESPEC    │
 │        Directory tree         ├─────────────┤
 │                               │ Disk space  │
 │                               ├─────────────┤
 ├───────────────────────────────┤ Statistics  │
 │        Small file list        │ Total       │
 │                               │ Matching    │
 │                               │ Tagged      │
 └───────────────────────────────┴─────────────┘
   DIR COMMANDS / FILE COMMANDS and function keys
```

The selection bar identifies the directory or file that receives the next command. A tagged file has a diamond beside its name. Commands for tagged files act on the current list.

: Enter | Switch between the tree and the file list. In the file list, Enter opens a supported archive.
: F4 | Show Ctrl commands, then Alt commands. The menu returns after one command or an unrelated key.
: F7 | Preview the selected file with Autoview.
: F8 | Split or unsplit the workspace.

= TWO PANES
Each side has its own directory, file view, filter and selection. Tab changes the active side. Copy and move prompts suggest the opposite directory; completed transfers refresh the destination.

```
       Left path                 Right path
 ┌──────────────────────┬──────────────────────┐
 │                      │    Directory tree    │
 │  Expanded file list  │                      │
 │                      ├──────────────────────┤
 │                      │    Small file list   │
 └──────────────────────┴──────────────────────┘
       Commands apply to the active pane
```

: Tab | Change the active pane when split.
: Shift+F8 | Swap sides or cycle the statistics layout.
: Alt+F | Cycle Name, Size/attributes, Details and Long name layouts.

= FILE ATTRIBUTES
The attributes column follows each file's mounted filesystem, including mixed Branch/Showall/Global lists. Linux filesystems show POSIX permissions such as rw-r--r-- (0644 in compact columns). Special modes use s/S and t/T. A separate l identifies symbolic links and their own permissions.

FAT/exFAT and NTFS show H/R/S/A: Hidden, Read-only, System and Archive. These are actual on-disk DOS attributes; a dot filename and an unavailable write permission do not imply DOS H/R. A dot marks each unset flag; ???? means attributes could not be read. NTFS3 and NTFS-3G are supported when the driver exposes the attributes. F3 refreshes the values. The A command continues to edit POSIX permissions.

= READING THIS MANUAL
Use Left/Right for chapters, Tab for the index and F to find a word anywhere in the manual. Space repeats a help search. Esc or F1 returns to your work with the selection and tags intact.
)HELP"},
{HelpTopic::Navigation, "Directories and navigation", R"HELP(
= LOGGING A TREE
Logging reads directory entries into the workspace. Expanding or collapsing the tree changes what is visible; unlogging releases the stored branch. Directory symbolic links are not followed recursively.

: Arrows | Move the selection through directories or files.
: PgUp / PgDn | Move one screen at a time.
: Home / End | Move to the first or last entry.
: + or = | Log the selected directory.
: * | Log the entire selected branch, including its subdirectories.
: - | Unlog the selected branch.
: F5 / F6 | Collapse the second / first level while retaining logged data.
: Enter | Log an unloaded directory, or enter its file list. Return to the tree from a regular file.
: Backspace | Go to the parent. At the current root, expand the root upward.
: Esc | Return to the tree or cancel the current operation.

= FIND A NAME QUICKLY
Hold Shift while typing a prefix to move to a matching name. Use | to open a prefix prompt without holding Shift. Shift+Esc clears the prefix.

: L | Select a mounted location or enter a path.
: < / > | Cycle previously logged locations; comma / period also work.
: F3 | Refresh the current directory or file list.
: Alt+F3 | Relog and clear tags.
: ? | Open disk, logged-file, system and display statistics.

= DIRECTORIES IN LINUX
: M | Create a directory. Use / for nested levels. F4 Jump enters it after creation.
: H | Create a symbolic link to the selected directory. Enter the full destination path for the new link.
: R | Rename the selected directory.
: A | Show available disk space.
: G | List files across logged locations (Global).
: Alt+G | Graft the selected branch into a destination directory; review the source and destination before confirming.

An empty directory can be deleted with D after confirmation. For a populated branch, read the Deletion and Prune chapter first.
)HELP"},
{HelpTopic::Tags, "Tags, Branch and Showall", R"HELP(
= INDIVIDUAL AND GROUP COMMANDS
Tags are a working selection of files. A diamond beside a filename marks a tagged file. Tags are independent of the highlight bar.

: T / U | Tag / untag the current file and advance.
: Ctrl+T / Ctrl+U | Tag / untag all files in the current list.
: Ctrl+I, T | Invert tags in the current context.
: B | Branch: list logged files under the selected directory.
: S | Showall: list files in the current logged root.
: G | Global: list files across logged locations.
: Ctrl+B / S / G | Open the corresponding view with tagged files only.
: Ctrl+Enter | Show or refine the tagged files in the current context.
: Ctrl+F4 | Toggle the tags-only file view.

= A COMMON WORKFLOW
1. Select a directory in the tree and press * to log the branch.
2. Press B to list its files, then Ctrl+T to tag them.
3. Press Ctrl+S to search their contents. Matching files remain tagged.
4. Open a result with V; Space moves through the search hits.

Filespec limits the visible list. Before a group operation, check the view name, the filter and the tagged count. Tags outside that list do not automatically become targets.

: Ctrl+F7 | Tag files in the logged branch from the tree.
: Ctrl+F8 | Untag files in the logged branch from the tree.
: Ctrl+F9 | Invert tags in the logged branch from the tree.

= RETURNING FROM HELP
Help navigation never tags, copies or deletes files. Esc or F1 restores the view underneath it.
)HELP"},
{HelpTopic::Filespec, "Filespec, history and sorting", R"HELP(
= FILTER THE LIST
F opens Filespec in tree or file views. Use wildcard names to choose which files appear. This filters names and metadata; Ctrl+S searches file contents.

: F | Enter a Filespec. Enter applies it; Esc cancels.
: Up / Down | Open the saved Filespec history in the prompt.
: F3 | Retrieve the last Filespec.
: Ctrl+F | Return to previous filters.
: Ctrl+I, F | Invert the filter.
: Alt+S | Open sorting options.

= EXAMPLES
*.php              PHP files
*.php,*.js,*.css    Several filename patterns
*report*           Names containing report
=>s1000            Files at least 1000 bytes
<=s5000            Files at most 5000 bytes

Date filters use MM-DD-YYYY or TODAY. Size filters use bytes. Review the filter shown at the top of the list before tagging files.

= SAVED HISTORY
In Filespec or tagged-search prompts, Up/Down opens the history window. Arrows, PageUp/PageDown and Home/End navigate; Enter retrieves an entry into the prompt. Review it and press Enter again to apply it. Esc returns without selecting an entry.

History is stored under ~/.config/ltreec, or XDG_CONFIG_HOME/ltreec when configured. It survives restarting the graphical application. Terminal history is currently limited to the session.

= DISPLAY OPTIONS
: Alt+F | Cycle the file layouts without changing the filter.
: Shift+Left/Right | Move the extension column.
: Shift+Home | Restore the extension column position.

Changing the display does not copy, rename or alter a file.
)HELP"},
{HelpTopic::Search, "Search contents and regular expressions", R"HELP(
= SEARCH TAGGED FILES
Tag the files to scan, then press Ctrl+S in a file view. The search scans those files and retains matching tags. The selection follows the current file; the footer shows progress, a spinner, hit files, elapsed time and speed. Esc cancels.

: Ctrl+S | Search contents of tagged files in the current list.
: F2 | Toggle case sensitivity in the search prompt.
: F3 | Retrieve the last search string.
: F4 | Cycle Text, Hex, Unicode and Regex modes.
: Up / Down | Open search history and retrieve a previous query.
: Enter / Esc | Start / cancel the search prompt.

= CHOOSE THE SEARCH MODE
Text searches a text byte pattern. Hex searches bytes written in hexadecimal. Unicode decodes text. Regex interprets a Unicode regular expression instead of a literal string.

= REGEX EXAMPLES
^ORDER_[0-9]+$     A complete line such as ORDER_123
error|warning      Either word
ID:\d+             ID: followed by one or more digits
\btotal\b          A whole word

Regex runs within each line. ^ and $ refer to that line; a match cannot span multiple lines. UTF-8, UTF-16 and UTF-32 are supported with encoding detection. Invalid patterns remain editable. Resource or decoding errors are reported as errors.

= FOLLOW THE RESULTS
: V | View the selected result with the batch query and case option retained.
: Space / + | Go to the next occurrence in the viewer.
: - | Go to the previous occurrence.
: Ctrl+V | View tagged files as a sequence.

Hits during a batch search counts matching files. In the viewer, each occurrence is highlighted. Zero-length Regex matches advance without looping. A retrieved history string uses the mode currently selected with F4.

Terminal mode does not yet search file contents. Use the graphical interface for this workflow.
)HELP"},
{HelpTopic::Transfer, "Copy, move and rename", R"HELP(
= COPY OR MOVE FILES
In a file list, C and M act on the highlighted file. Ctrl variants act on tagged files in that list. Alt variants retain branch paths relative to the source.

: C / M | Copy / move the current file.
: Ctrl+C / Ctrl+M | Copy / move tagged files into one destination directory.
: Alt+C / Alt+M | Copy / move tagged files with paths and a filename mask.
: R | Rename the current file.
: Ctrl+R | Rename tagged filenames using a mask.

= PROMPTS AND MASKS
Review the filename or mask first, then the destination. * preserves the matching name component. The opposite pane provides a convenient destination when split. Check the full path before accepting.

: Left / Right | Move the insertion cursor within the prompt.
: Home / End | Move to the start / end.
: Delete / Backspace | Remove characters ahead of / behind the cursor.
: Ctrl+A | Select the prompt text for replacement.
: F3 | Retrieve the previous value where offered.
: Enter / Esc | Accept / cancel.

Replacement requires confirmation. Successful transfers refresh both panes and preserve the view and selection. Completed moves remove source entries and clear their source tags. Check the completion summary for files that could not be transferred.

= DIRECTORIES
Alt+G in the tree grafts a branch into a destination directory. This is a directory move; read its source and destination prompt carefully. H creates a symbolic link instead of copying the directory's contents.
)HELP"},
{HelpTopic::Delete, "Deletion and Prune", R"HELP(
= DELETE THE CURRENT ENTRY
: D / Delete | Delete the current file, or an empty directory, after confirmation.
: Ctrl+D | Delete tagged files in the visible file list after confirmation.

For group deletion, first confirm the selected set with Y/N. The next prompt asks "Confirm delete for each file?". Choose Y to review individual files, or N to attempt the confirmed group without individual questions. A failure is reported in the result summary.

= DELETE A LOGGED BRANCH
1. In the tree, press * to log the branch.
2. Press B, then Ctrl+T to tag its files.
3. Press Ctrl+D and confirm the group.
4. Choose whether to confirm each file.
5. Review the result before confirming removal of the parent directory.

The parent is not removed if a file could not be deleted. D does not recursively remove a populated directory. After deletion, the selection stays near its former position.

= PRUNE
Alt+P in the tree removes a logged branch. This operation can remove directories and their contents. Enter the word PRUNE to confirm the prompt, then review any further confirmation.

: F5 | Keep files in the current directory.
: F6 | Delete only empty directories.
: F2 | Toggle force deletion of read-only files.
: F4 | Toggle use of Trash when supported by the prompt.
: F1 | Read the detailed Prune options before proceeding.
: Esc | Cancel before confirmation or stop remaining work.

Check the branch path and every option. Do not assume a deletion can be undone. The completion summary identifies any remaining entries or failures.
)HELP"},
{HelpTopic::Metadata, "Permissions, timestamps and information", R"HELP(
= LINUX PERMISSIONS
: A (files) | Change permissions of the current file.
: Ctrl+A (files) | Change permissions of tagged files.
: Alt+A (tree) | Change permissions of the selected directory.

The permission prompt supports Linux permission modes and shows a review before Y/N confirmation. Symbolic changes are applied to each file's existing permissions. Read the result for skipped files or errors.

= NEW DATE
: N | Change timestamps of the current file.
: Ctrl+N | Change timestamps of tagged files in the current list.
: F2 | Use Now in the STAMP prompt.
: Tab | Retrieve the current timestamp.
: F3 | Retrieve the last value.
: F4 | Choose modification time, access time or both.
: F5 | Choose Set, Adjust or Increment mode.
: Up / Down | Open saved timestamp history.

Enter a local ISO date and time, for example 2026-10-04 17:32:23. A date alone keeps the time; a time alone keeps the date. Signed adjustments use y/m/d/h/n/s; n means minutes. Increment applies successive offsets in visible file order.

Linux creation time and metadata-change time are not offered as editable timestamps. Links, special files and files changed since the prompt opened are skipped. Timestamp editing is currently graphical only.

= INFORMATION WITHOUT LEAVING THE LIST
: Alt+I | Show the information box while keeping the file view and its commands.
: Enter / Esc | Close the information box.
: ? | Open extended disk, system, display and logged-file statistics.
: F3 | Refresh the statistics screen.
: Ctrl+F3 | Toggle automatic statistics refresh.
)HELP"},
{HelpTopic::Viewer, "Viewer, find, gather and Hex editing", R"HELP(
= VIEW A FILE
V opens the internal viewer. A batch-search query is retained so that Space can follow its occurrences. The top line identifies the file; commands stay at the bottom.

: A / H / D | Alpha text / Hex bytes / Dump.
: C / Shift+C | Cycle character sets forward / backward.
: W | Toggle wrapping.
: J / M | Junk-character handling / character mask.
: L / R | Line highlight / ruler.
: Tab | Cycle tab width.
: Arrows / PgUp/PgDn | Scroll the document.
: Home / End | Go to the start / end.
: O | Go to a byte offset or line.

= FIND OCCURRENCES
: F / B | Find from the start / backward from the end.
: / or S | Find from the current page.
: Backslash | Search backward from the page.
: Space / + / - | Repeat / find forward / find backward.
: F8 | Toggle search skip by hit or page.

The find prompt has F2 Case, F3 Last, F4 Text/Hex/Unicode/Regex and Up/Down History. Regex searches lines of Unicode text and switches Hex/Dump to Alpha.

= TAGGED FILE SEQUENCE
: Ctrl+V (list) | Open tagged files as a sequence.
: N / P | Next / previous tagged file.
: U | Untag the current file.
: Alt+Home / End | First / last tagged file.

= GATHER AND BOOKMARKS
: G | Gather a range of lines. Select first and last line, then a destination.
: F4 / Ctrl+C | Copy gathered text to the clipboard.
: F5 / F2 | Append gathered text / clear the clipboard.
: F6 | Change the gap between gathered blocks.
: Ctrl+0..9 | Store a position bookmark.
: Alt+0..9 | Retrieve a position bookmark.

Gather destinations include a filename to append to, CLIP: for the clipboard and PRN for the print dialog.

= EDITING
E in Alpha opens the configured external editor (Sublime by default). E in Hex starts byte overwrite editing of the visible page. Archive members are read only; extract them before editing.

: Tab (Hex edit) | Switch Hex / ASCII entry.
: Arrows | Move the byte cursor.
: F8 | Undo page edits.
: Enter | Finish and review the save confirmation.
: Esc | Discard the uncommitted edits.

Hex edits preserve file length. An externally changed file is rejected when saving. Review every edit and the confirmation before writing bytes.

= MORE VIEW COMMANDS
: F3 / Ctrl+F3 | Reload / toggle continuous reload.
: Shift+F2..F6 | Start automatic scrolling.
: Shift+F7 | Toggle the automatic-scroll loop.
: X / Alt+X | Integrated / separate shell.
: F10 | Cycle Ctrl and Alt VIEW COMMANDS for one command.
: Esc | Return to the file list.

This chapter describes the graphical viewer. Terminal viewer commands have their own help chapter.
)HELP"},
{HelpTopic::Archives, "Archives: browse, extract and create", R"HELP(
= OPEN AN ARCHIVE
Select a supported archive in a file list and press Enter. Browse its directory structure with the same blue tree-and-file style. Enter opens a folder or member; Backspace moves to its parent. Esc returns to the original list.

: Enter / V | Browse a directory / view a file member.
: B | List the archive branch.
: F / F3 | Filespec / reload the archive.
: T / U | Tag / untag a member.
: Ctrl+T / Ctrl+U | Tag / untag all visible members.
: Ctrl+S | Search tagged members in Text/Hex/Unicode/Regex mode.
: F9 | Enter the password for an encrypted archive.

= EXTRACT MEMBERS
: C / E | Extract the current file or directory.
: Ctrl+C / Ctrl+E | Extract tagged members.
: Alt+C / Alt+E | Extract tagged members with their paths.
: F2 (prompt) | Toggle preserving paths.
: F4 (prompt) | Toggle replacement; overwriting needs further Y/N confirmation.

Unsafe paths, links and special entries are rejected during extraction. Member viewing has a 32 MiB limit. Encrypted-reader support depends on the installed archive library and format.

= CREATE AN ARCHIVE
F5 in a file list creates an archive from the current file; Ctrl+F5 uses tagged files. Edit the proposed output path normally with Left/Right/Home/End, Delete and Ctrl+A.

: F3 (create) | Open the format selector.
: F2 (create) | Toggle preserving paths.
: F4 (create) | Toggle replacement.
: Enter / Esc | Create / cancel.

Formats: ZIP, TAR, TAR.GZ, TAR.BZ2, TAR.XZ, 7Z, GZ, BZ2 and XZ. GZ/BZ2/XZ accept one file; ZIP, 7Z and TAR variants contain several files. ZIP uses UTF-8 relative names for Windows/Linux colleagues.

Archive members cannot be edited in place. Extract the file, edit it, then create a new archive. Encrypted archive creation is not available. Enter is the advertised archive-open key; GNOME keeps its Alt+F5 shortcut.
)HELP"},
{HelpTopic::Compare, "Compare files and directories", R"HELP(
= SELECT TWO FILES
: J | Compare the current file with a chosen second file.
: Ctrl+J | Compare two tagged files, or the current file with one tagged file.

Check both paths in the compare prompt. Two identical filenames in different directories are a useful pair; selecting the same full path twice compares a file with itself.

= COMPARISON VIEW
: Arrows / PgUp/PgDn | Scroll both files together.
: Home / End | Go to the start / end.
: Space or + | Next difference.
: - | Previous difference.
: S | Switch stacked / side-by-side panes.
: L | Toggle line numbers.
: H | Switch character / Hex comparison.
: D (Hex) | Highlight differing bytes / the entire differing row.
: C | Toggle case sensitivity in character mode.
: W | Toggle compression of spaces and tabs.
: E | Skip empty or whitespace-only lines.
: Enter / Esc / Q | Return to files.

Hex compares original bytes at the same offsets. C/W/E affect character comparison only. Row highlighting deliberately colors a whole changed row; byte highlighting marks only unequal positions. Comparison is read only.

= DIRECTORIES
: C (tree) | Compare directories.
: Alt+C (tree) | Compare branches.
: Alt+K | Open the compare filter.

Review the comparison criteria in the prompt. Directory comparison and filtering do not replace the file viewer or byte comparison.
)HELP"},
{HelpTopic::Console, "Shell, startup and Linux integration", R"HELP(
= EXECUTE COMMANDS
X opens a shell in the current directory. Use it as your normal shell, then exit to return to LTree. Alt+X opens a separate shell where offered. Commands run with your user permissions.

The user's shell keeps its normal history. LTree does not disable or erase that history. E uses Sublime Text; the alternate editor can be configured with LTREE_ALT_EDITOR.

= STARTUP EXAMPLES
ltc                         Graphical application when a display is available
ltc /home/alex             Start at a selected directory
ltc --tree-sizes             Show logged directory size annotations
ltc --terminal              Use the terminal interface
ltc --new-instance          Explicitly allow another instance
ltc --fullscreen            Start the graphical interface fullscreen
ltc --help                  Show command-line options

The graphical window remembers its normal size and maximized/fullscreen state when closed normally. Settings are saved in ~/.config/ltreec/window.ini (or XDG_CONFIG_HOME). --fullscreen overrides the saved state. X11 may restore placement; Wayland leaves placement to the desktop. A minimized window reopens visible.

By default, launching again activates the existing graphical instance. Without a graphical display, LTree selects terminal mode automatically. Terminal mode adapts portable commands to ncurses; its help explains keyboard and desktop-dependent differences.

= DESKTOP KEYS
GNOME may reserve some Alt function keys. LTree keeps those desktop functions intact. Archives use Enter. Where offered, F4 in lists and F10 in the viewer show alternate command menus without holding a modifier.

: Alt+Enter | Toggle fullscreen in the graphical interface.
: Alt+F7 | Maximize / restore where delivered by the desktop.
: Q | Quit after confirmation.

Saved graphical histories use ~/.config/ltreec (or XDG_CONFIG_HOME). The executable is ltc; the desktop and icon identity remains ltreec for compatibility.
)HELP"},
{HelpTopic::Prune, "Prune a logged branch", R"HELP(
= BEFORE CONFIRMING
Alt+P prunes the selected directory and its logged subdirectories. Filespec and tags do not limit Prune. Hidden files are included. Unlogged directories, symbolic-link targets and nested mounts are kept.

All options initially say no. Check the full branch path, choose the options, then type PRUNE and press Enter. An incompletely logged branch needs an additional Y confirmation.

: F5 | Keep the selected directory and its own files; prune below it.
: F6 | Delete only empty directories. Do not delete files or symbolic links.
: F2 | Permit deletion of files with no write permission bits. This does not grant privileges or change directory permissions.
: F4 | Move entries to Linux Trash. A Trash failure keeps the entry.
: Esc | Cancel the prompt or stop pending work.

Completed deletions cannot be undone here. Review the completion summary for retained entries and failures. Help keys only navigate this manual; they do not change the Prune options.
)HELP"},
{HelpTopic::Graft, "Graft a whole branch", R"HELP(
= MOVE THE DIRECTORY BRANCH
Alt+G in the tree opens GRAFT. The destination is a containing directory: its new child keeps the branch name. Filespec, tags and logging do not limit the move; all contents are included.

: Tab | Cycle proposed destination paths.
: F2 | Browse directories for a destination.
: F3 | Retrieve the last destination.
: Up / Down | Open persistent path history. Enter retrieves; Enter again starts the move.
: Enter | Confirm the source and destination.
: Esc | Cancel the prompt or stop remaining work.

Existing destination branches are never replaced or merged. On the same filesystem, the branch is renamed. Across filesystems, GNU mv copies it before removing the source.

Symbolic links inside the branch move as links; their targets are not followed. Source branches containing mount points are rejected.

An interrupted cross-filesystem move may retain a destination copy. Warnings and recovery paths are reported. Retained copies are not automatically removed. Check the result before using either branch again.
)HELP"},
{HelpTopic::Stamp, "New date: file timestamps", R"HELP(
= CHOOSE FILES AND TIMESTAMPS
N changes the current file; Ctrl+N changes tagged files in the current list. The STAMP prompt shows the source timestamp and an editable value.

: F2 | Now: use the current local date and time.
: Tab | Retrieve the original highlighted timestamp.
: F3 | Retrieve the last value.
: Up / Down | Open saved New date history.
: F4 | Choose written (modification), accessed or both.
: F5 | Choose Set or Adjust; tagged files also offer Increment.
: Enter | Apply the timestamp change.
: Esc | Cancel or stop remaining files. Completed changes remain.

= SET
Use yyyy-MM-dd HH:mm:ss. A date alone preserves the time; a time alone preserves the date.

2026-10-04 17:32:23     Date and time
2026-10-04              Date only
17:32:23                Time only

= ADJUST AND INCREMENT
Adjust uses each file's original timestamp. Increment starts at the first tagged file and adjusts each following value successively in visible order.

+1y-5n                  Add one year and subtract five minutes
-3d+10s                 Subtract three days and add ten seconds
e                       Round up to an even second

y = years, m = months, d = days, h = hours, n = minutes, s = seconds. Dates use local time. Creation time and metadata-change time cannot be assigned.

Links, special files and files changed since opening are skipped. Check the summary for failures. Timestamp history survives restarting the graphical application.
)HELP"},
{HelpTopic::Statistics, "Extended statistics in Linux", R"HELP(
= INSPECT WITHOUT RELOGGING
? or / opens statistics for the selected directory or file. This screen reports disk, logged-file, system, display and login information.

: F3 | Refresh statistics without logging new directories.
: Ctrl+F3 | Toggle automatic refresh every second while this screen is open.
: < / > | Switch mounts without changing the file panels.
: Up / Down | Scroll in a smaller window.
: PgUp / PgDn | Scroll pages.
: Home / End | Start / end of the statistics.
: Esc / Enter | Return to the file panels.

= DISK STATISTICS
Capacity, available and free space are reported by the filesystem. Used = capacity - free. Reserved = free - available. Block size and count refer to filesystem allocation units.

Sector geometry and NTFS compression metrics are N/A. A missing or unsupported value also appears as N/A.

= LOGGED FILES
Logged counts use the cache, scope, filter and selected mount. Displayed counts and average size use the visible file list, including Tags-Only. Refreshing statistics does not discover new files; F3 in the file panels refreshes the list.

= SYSTEM AND DISPLAY
RAM available uses Linux MemAvailable. MB here means 1024 x 1024 bytes. Commit total / limit use Committed_AS / CommitLimit. CPU MHz samples the first reported CPU and changes with frequency scaling.

Screen size uses Qt logical pixels, including on Wayland. Session and desktop information replace the Windows workgroup and domain fields.
)HELP"}
    };
}
QVector<HelpSection> terminalManual()
{
    auto sections=manual();
    sections.append({HelpTopic::Terminal, "Terminal workspace and keyboard", R"HELP(
= SAME FILE OPERATIONS
The terminal interface uses the same filesystem engines as the graphical interface. Copy, move, rename, deletion, attributes, timestamps, Prune, Graft, comparisons, searches and archives retain their safety checks. Operations refresh both panels and keep the selection near its previous position.

```
 Current directory                 FILE specification
 ┌─────────────────────────┬───────────────────────┐
 │      Directory tree     │ Disk / file statistics│
 ├─────────────────────────┤                       │
 │         File list       │                       │
 └─────────────────────────┴───────────────────────┘
 Commands, prompts and current status
```

: Enter | Tree / file list; open compressed files from the list.
: + / * | Log a directory / log a branch.
: F3 / Alt+F3 | Refresh / relog.
: F4 | Cycle normal, Ctrl and Alt menus. Choose a command to close the menu.
: F8 / Tab | Split / switch panels.
: Shift+F8 | Swap panels or select statistics panels.
: < / > | Cycle logged roots without losing their tags or position.
: A (tree) / ? | Available space / extended statistics.
: E / O (file list) | Edit with the system editor; binary files use internal Hex editing.
: X | Run a command or open a shell in the selected directory, then return.

= TERMINAL KEY TRANSPORT
Kitty's extended keyboard protocol is detected automatically, including over SSH. Hold Ctrl or Alt to display its command menu; release it to restore the normal menu. Modifier release events never run commands. External editors and shells receive the previous keyboard mode.

Classic terminal protocols cannot distinguish Ctrl+I from Tab, Ctrl+M from Enter or Ctrl+J from a newline. Use F4 then the command letter; F4 then Enter toggles tagged-only files. F4 then a function key selects its Ctrl variant. F10 provides modifier menus in the viewer. Kitty/xterm modified function keys are supported when delivered by the terminal.

GNOME's global keys remain unchanged. Archives open with Enter. Pixel window placement, font selection and graphical window zoom are controlled by your terminal emulator and desktop.

= CONFIGURATION AND EXTERNAL PROGRAMS
Filespec, search, timestamp and Graft histories share ~/.config/ltreec with the graphical application, respecting XDG_CONFIG_HOME and LTREE_HISTORY_FILE. Up/Down opens a selectable history list; F3 retrieves the last entry.

~/.selected_editor takes priority, followed by VISUAL, EDITOR and an available system editor. The configuration is read as a literal assignment, never executed. LTREE_ALT_EDITOR and LTREE_ALT_VIEWER may name alternate terminal programs.

The user's X shell keeps normal history. LTree releases the terminal to external programs and restores its screen on return. Clipboard publication requires wl-copy on Wayland or xclip on X11; an internal selection and Gather-to-file remain available without a desktop. Printing supports a file or a pipe to an installed print command.
)HELP"});
    sections.append({HelpTopic::TerminalFiles, "Terminal files, transfers and searches", R"HELP(
= FILE AND DIRECTORY COMMANDS
: C / Ctrl+C / Alt+C | Copy current / tagged flat / tagged with directory paths.
: M / Ctrl+M / Alt+M | Move current / tagged flat / tagged with directory paths.
: M (tree) | Create a directory, including nested names.
: D / Ctrl+D | Delete current / tagged files with confirmation.
: R / Ctrl+R | Rename current / tagged files with a mask and case selection.
: A / Ctrl+A | Set permissions of the selected / tagged files.
: Alt+A (tree) | Set the selected directory's permissions.
: N / Ctrl+N | Set, adjust or increment written/accessed timestamps.
: G / Ctrl+G (tree) | Global / tagged Global across logged roots.
: H (tree) | Create a symbolic link at the destination you enter.
: Alt+G / Alt+P (tree) | Graft / Prune; PRUNE must be typed explicitly.
: C / Alt+C (tree) | Directory / logged branch comparison.
: J / Ctrl+J | Compare two files / two tagged files.
: Alt+K | Duplicate, unique, size, content or date filters.
: Ctrl+S (files) | Search tagged files in Text, Hex, Unicode or Regex mode.
: F5 / Ctrl+F5 (files) | Create an archive from current / tagged files.

Copy/move accepts a filename mask, letter case, destination, path policy and replacement policy. The destination defaults to the other panel. Existing files and per-file errors offer confirmation, retry, skip or cancel.

Tagged deletion asks Y/N and then "Confirm delete for each file?". After deleting a Branch without failures or skipped files, removing its empty directories and parent requires a separate confirmation. Existing files keep their directories safe. Selection stays close to the removed item.

= SEARCH AND HISTORY
F2 switches case sensitivity; F4 chooses Text, Hex, Unicode or Regex. Progress shows the current file, moving selector, hits, byte bar, elapsed time, remaining time, speed and spinner. Esc cancels remaining files and retains unprocessed tags. Reading errors offer Retry, Keep tag, Untag or a policy for that error type.

V remembers the executed query, mode and case setting; Space/+ finds the next match and - the previous one. Regex uses the shared line-based matcher and bounded resources.

History uses Up/Down, PgUp/PgDn and Home/End to select. Left/Right filters marks; Del deletes, Ins marks, a letter or digit assigns a retrieval mark, Enter retrieves, | appends, F2 sorts, F3 saves and F4 reloads.

= COMPRESSED FILES
Enter browses ZIP, TAR, 7Z and formats supported by libarchive. Members can be tagged, filtered, viewed, searched or extracted. Archive members remain read-only; extract before editing.

F3 in Create chooses ZIP, TAR, TAR.GZ, TAR.BZ2, TAR.XZ, 7Z, GZ, BZ2 or XZ. F2 preserves paths; F4 controls replacement. The filename is editable with arrows, Home, End, Backspace and Delete. GZ/BZ2/XZ streams hold one file; use a TAR variant for multiple files. Encrypted input can request a password; encrypted output and in-place archive updates are shared development limits.
)HELP"});
    sections.append({HelpTopic::TerminalView, "Terminal viewer, Autoview and Hex editor", R"HELP(
= VIEW COMMANDS
The common Viewer chapter describes the text commands. The terminal supplies Alpha, Dump, Hex, Junk, seven character sets, wrapping, line selection, mask, ruler, tab widths, offsets, bookmarks, query navigation, file sequences, reload, autoscroll and Gather.

: F10 | Cycle Ctrl / Alt menus; select a command to return to normal.
: Ctrl+0..9 / Alt+0..9 | Set / retrieve bookmarks through F10 menus.
: F3 / Ctrl+F3 | Reload / continuous reload.
: Shift+F2..F6 / Shift+F7 | Autoscroll speeds / loop.
: G / F4 | Mark a selection start and end, then write, append or copy it.
: Ctrl+C / F5 / F2 | Copy selection / append / clear clipboard.
: F6 | Cycle clipboard gaps.
: Alt+P | Print to a file or | print-command.
: X / Alt+X | Run a command or open the current directory's shell.

= AUTOVIEW
F7 previews the selected file beside the list. Shift+viewer-key operates the preview; Shift+arrows and pages scroll it. Alt+Left/Right/Home changes preview width. F7, Enter or Esc returns to normal lists.

= INTERNAL HEX EDITOR
The editor replaces existing bytes without inserting or removing data. One page is buffered at a time. Files must be regular and accessed through safe paths; symbolic links and archive members cannot be edited internally.

: 0-9 / A-F | Replace the highlighted hexadecimal digit.
: Tab | Switch Hex / ASCII input (one byte per character).
: Arrows / Home / End | Move within the buffered page.
: PgUp / PgDn | Change pages, asking to save modified bytes first.
: Ctrl+S | Save and continue editing.
: Enter | Finish, asking to save modified bytes.
: Y (save prompt) | Save changes.
: N / Esc (save prompt) | Keep editing.
: F8 | Undo unsaved changes on the page.
: Esc / Ctrl+C | Discard unsaved page changes and return to the viewer.

Previously saved pages stay saved. Saving verifies file identity and original contents. External changes cause a refusal to save; reload before editing again. Resize preserves the byte position. The internal viewer/editor shares the graphical 32 MiB limit.
)HELP"});
    return sections;
}
QStringList wrap(QString text, int width)
{
    QStringList result;
    while (text.size() > width) {
        int cut = text.lastIndexOf(' ', width);
        if (cut <= 0) cut = width;
        result.append(text.left(cut));
        text = text.mid(cut);
        if (text.startsWith(' ')) text.remove(0, 1);
    }
    result.append(text);
    return result;
}
}

HelpDocument::HelpDocument(bool terminal) : sections_(terminal ? terminalManual() : manual()) {}

void HelpDocument::open(HelpTopic topic)
{
    section_ = 0;
    for (int i = 0; i < sections_.size(); ++i) if (sections_[i].id == topic) section_ = i;
    selected_ = section_; top_ = 0; match_ = -1; finding_ = false; notice_.clear();
}
QString HelpDocument::title() const
{
    return index() ? "Contents" : QString("%1. %2").arg(section_ + 1).arg(sections_[section_].title);
}
QVector<HelpLine> HelpDocument::lines(int width) const
{
    width = std::max(8, width);
    QVector<HelpLine> result;
    if (index()) {
        result.append({"CONTENTS", HelpStyle::Heading});
        result.append({""});
        for (int i = 0; i < sections_.size(); ++i)
            result.append({QString("%1. %2").arg(i + 1, 2).arg(sections_[i].title).left(width)});
        result.append({""});
        for (const auto &line : wrap("Choose a chapter with Up/Down and Enter. Left/Right reads adjacent chapters. F searches the entire manual.", width)) result.append({line});
        return result;
    }
    bool diagram = false;
    for (QString line : sections_[section_].body.trimmed().split('\n')) {
        if (line == "```") { diagram = !diagram; continue; }
        if (diagram) {
            if (width >= 48) result.append({line.left(width), HelpStyle::Diagram});
            else if (line.contains("Current") || line.contains("Commands"))
                for (const auto &part : wrap(line.trimmed(), width)) result.append({part, HelpStyle::Diagram});
            continue;
        }
        HelpStyle style = HelpStyle::Text;
        if (line.startsWith("= ")) { line.remove(0, 2); style = HelpStyle::Heading; }
        if (line.startsWith(": ")) {
            line.remove(0, 2);
            const int separator = line.indexOf(" | ");
            const QString keys = line.left(separator), description = line.mid(separator + 3);
            const int labelWidth = std::min(22, width / 3);
            if (keys.size() < labelWidth) {
                const auto parts = wrap(description, width - labelWidth);
                for (int i = 0; i < parts.size(); ++i)
                    result.append({(i ? QString(labelWidth, ' ') : keys.leftJustified(labelWidth)) + parts[i], HelpStyle::Keys, i ? 0 : int(keys.size())});
            } else {
                for (const auto &part : wrap(keys, width)) result.append({part, HelpStyle::Keys, int(part.size())});
                for (const auto &part : wrap(description, width - 2)) result.append({"  " + part});
            }
        } else {
            for (const auto &part : wrap(line, width)) result.append({part, style});
        }
    }
    return result;
}
void HelpDocument::constrain(int width, int height)
{
    height = std::max(1, height);
    if (index()) {
        selected_ = std::clamp(selected_, 0, int(sections_.size()) - 1);
        const int row = selected_ + 2;
        if (row < top_) top_ = row;
        if (row >= top_ + height) top_ = row - height + 1;
    }
    top_ = std::clamp(top_, 0, std::max(0, int(lines(width).size()) - height));
}
void HelpDocument::find(int width, int height, bool next)
{
    if (query_.isEmpty()) return;
    const int original = section_, previousTop = top_;
    const int start = index() ? selected_ : section_;
    const int startRow = next && !index() ? std::max(top_ + 1, match_ + 1) : 0;
    for (int pass = 0; pass <= sections_.size(); ++pass) {
        section_ = (start + pass) % sections_.size();
        const auto content = lines(width);
        const int begin = pass == 0 ? startRow : 0;
        const int end = pass == sections_.size() ? std::min(startRow, int(content.size())) : int(content.size());
        for (int row = begin; row < end; ++row) {
            if (content[row].text.contains(query_, Qt::CaseInsensitive)) {
                match_ = top_ = row; selected_ = section_; notice_ = "Found: " + query_; constrain(width, height); return;
            }
        }
    }
    section_ = original; top_ = previousTop; match_ = -1; notice_ = "No match: " + query_; constrain(width, height);
}
bool HelpDocument::act(HelpAction action, int width, int height, const QString &text)
{
    height = std::max(1, height);
    if (finding_) {
        if (action == HelpAction::Close) { finding_ = false; query_ = draft_; }
        else if (action == HelpAction::Accept) { finding_ = false; find(width, height, false); }
        else if (action == HelpAction::Backspace) { if (!query_.isEmpty()) query_.chop(1); }
        else if (action == HelpAction::Input && query_.size() < 128) query_ += text.left(128 - query_.size());
        return true;
    }
    notice_.clear();
    switch (action) {
    case HelpAction::Close: return false;
    case HelpAction::Index: selected_ = index() ? selected_ : section_; section_ = -1; top_ = 0; match_ = -1; break;
    case HelpAction::Accept: if (index()) { section_ = selected_; top_ = 0; match_ = -1; } break;
    case HelpAction::Previous: case HelpAction::Next:
        section_ = (index() ? selected_ : section_) + (action == HelpAction::Next ? 1 : -1);
        section_ = (section_ + sections_.size()) % sections_.size(); selected_ = section_; top_ = 0; match_ = -1; break;
    case HelpAction::Find: draft_ = query_; query_.clear(); finding_ = true; break;
    case HelpAction::Repeat: find(width, height, true); break;
    case HelpAction::Up: if (index()) --selected_; else --top_; break;
    case HelpAction::Down: if (index()) ++selected_; else ++top_; break;
    case HelpAction::PageUp: if (index()) selected_ -= height; else top_ -= height; break;
    case HelpAction::PageDown: if (index()) selected_ += height; else top_ += height; break;
    case HelpAction::Home: if (index()) selected_ = 0; else top_ = 0; break;
    case HelpAction::End: if (index()) selected_ = sections_.size() - 1; else top_ = lines(width).size(); break;
    case HelpAction::Input: case HelpAction::Backspace: break;
    }
    constrain(width, height);
    return true;
}
}
