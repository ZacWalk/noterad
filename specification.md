# Noterad Specification

Noterad is a lightweight text editor with multi-file search, Markdown, CSV, and other features, written in C++. It is intended to support research using various text files.

Command-line and other non-interactive startup modes may read persisted configuration, but they must not overwrite it. Configuration is only written during full interactive app sessions.

## Application Layout

The window is split into two panes separated by a draggable vertical splitter:

- **Left panel** — either the folder browser or the search panel (toggled with `Ctrl+Shift+F`)
- **Right panel** — the active document, shown as text/markdown/hex/CSV view
- **Splitter** — draggable divider (5px); highlights on hover, changes color while tracking. Defined as the `splitter` type in `ui.h`.

Each open document remembers its own active content view while it stays open. Switching to another file and back restores that document's last content view rather than reusing the most recently selected mode from another document.

The File menu includes an Open Recent Root Folder submenu listing up to 8 previously used root folders in most-recent-first order. Selecting one switches the current root folder, prompting to save modified files first when needed, and restores that folder's last open file when it is still present. This list is persisted in config alongside the last open folder and document, plus a per-folder map of the last open document for each recent folder.

## Folder View

### Click on a file
- The item highlights in the folder panel
- The file loads (or is retrieved from cache) and displays in the text view
- If the file changed on disk while also modified locally, a prompt asks to reload
- Focus stays in the folder panel

### Click on a folder
- Toggles expand/collapse
- Scroll position is preserved
- The currently selected file stays selected

### Arrow keys
- Moves the highlight up/down and previews the file in the text view (focus stays in the folder panel)
- Press **Enter** to open the highlighted file and move focus to the text view
- Press **Enter** on a folder to toggle expand/collapse

### Visual indicators
- Modified (unsaved) files are shown in red
- Folders show an expand/collapse chevron icon
- Long file names are ellipsized with `...`

### Context Menu (Right-Click)
- **New File** — creates `new-file.md` in the folder context (the clicked folder, or parent folder of the clicked file), refreshes the file list, and opens the new file for editing. If that name is already in use, appends `-2`, `-3`, etc. until a unique name is found.
- **New Folder** — creates `new-folder` in the folder context and refreshes the file list. If that name is already in use, appends `-2`, `-3`, etc. until a unique name is found.
- **Copy Path** — copies the full path of the clicked item to the clipboard.
- **Delete** — sends the selected file to the Recycle Bin after a confirmation prompt; if the deleted file is the active document, switches to a new document first; refreshes the file list. Disabled for folders.
- **Rename** — opens an inline edit widget over the file name. Enter commits the rename, Escape cancels. The file's base name (without extension) is pre-selected so the extension is preserved by default. Shows an error if the target name already exists. Disabled for folders.

### Keyboard
- **Delete** — deletes the selected file in the folder panel (same action as the context menu Delete item). No effect on folders.
- **Ctrl+C** — copies the full path of the selected item in the folder panel.
- **F2** — begins inline rename of the selected file (same behavior as the Rename context menu item)

## Search View

Opened with `Ctrl+Shift+F`. Press `Escape` to return to the folder view.

### Edit box
- Typing triggers a live search across all files in the current folder
- Placeholder text "Search..." shown when empty
- Supports text selection and standard edit keys (Ctrl+A, Ctrl+C, etc.)
- Total result count shown below the edit box

### Results display
- Results are grouped by file with a collapsible header per file
- File headers show the relative path and the number of matches, e.g. `src/app.cpp (5)`
- Match lines show the line number, the trimmed line text, and the match highlighted in orange
- Long match lines are ellipsized with `...` while keeping the matched substring visible

### Click on a match result
- The result highlights in the search panel
- The corresponding file opens in the text view
- The match text is selected and scrolled into view
- Focus stays in the search panel

### Click on a file header
- Toggles the visibility of that file's match results (collapse/expand)
- The header stays visible; only the child matches hide/show

### Context Menu (Right-Click)
- **Open Result** — opens the clicked match result in the editor and selects the match text.
- **Expand / Collapse** — toggles a file header open or closed when right-clicking a header.
- **Copy Path** — copies the full path of the clicked file header, or `path:line` for a specific match result using a 1-based line number.

### Arrow keys (Up/Down)
- Navigate between all items including file headers and match results
- On a match result, immediately opens the file and selects the match (live preview)
- On a file header, pressing **Enter** toggles collapse/expand of that file's matches
- On a match result, pressing **Enter** moves focus to the text view

### Other keys
- **F8** — jump to the next match result (skips headers)
- **Shift+F8** — jump to the previous match result (skips headers)
- **F5** — re-run the current search
- **Ctrl+C** — copies the selected search result path, appending `:line` for a specific match
- **Escape** — close search panel, return to folder view

### Limits
- Maximum 5,000 results
- Files larger than 10 MB are skipped
- Binary files are skipped

## Text Editor View

### Editing
- Full character input with undo/redo history (unlimited, per-document)
- Word wrap is enabled by default and can be toggled with **Alt+Z**; the current setting is restored on the next app launch
- **Tab** / **Shift+Tab** — indent / unindent selected lines
- **Ctrl+R** — reformat JSON
- **Sort & Remove Duplicates** — available from the Edit menu
- Double-click selects a word; drag to extend selection
- Context menu with spell suggestions (when spell check is enabled — see Spell Check)
- When the text editor has focus, the active row is shown with a subtle full-width highlight band to keep the current edit line visually prominent

### Navigation
| Shortcut | Action |
|----------|--------|
| Arrow keys | Move cursor |
| Shift+Arrow | Extend selection |
| Ctrl+Left/Right | Word left/right |
| Ctrl+Shift+Left/Right | Select word left/right |
| Home / End | Line start / end |
| Ctrl+Home / Ctrl+End | Document start / end |
| Page Up / Page Down | Page navigation |
| Ctrl+Up / Ctrl+Down | Scroll without moving cursor |

### Clipboard
| Shortcut | Action |
|----------|--------|
| Ctrl+C / Ctrl+Insert | Copy |
| Ctrl+X / Shift+Delete | Cut |
| Ctrl+V / Shift+Insert | Paste |
| Ctrl+A | Select all |

### Undo/Redo
| Shortcut | Action |
|----------|--------|
| Ctrl+Z / Alt+Backspace | Undo |
| Ctrl+Y | Redo |

## Markdown Preview

- **Ctrl+M** toggles markdown preview for the current document
- `.md` / `.markdown` files auto-open in preview mode
- Renders headings (H1–H3 with size scaling), **bold**, *italic*, links, ordered/unordered lists
- Word-wrapped continuation lines inside bullet points are indented to align with the content start (hanging indent)
- Renders markdown tables with aligned columns; column widths are capped to fit the available screen width, with cell text word-wrapping within each column; cells containing numbers, percentages, or currency values are right-aligned, others left-aligned
- Read-only; press **Escape** to return to the text editor

## Hex View

- Automatically shown for binary files
- Displays offset (8 hex digits) | hex bytes (16 per row) | ASCII representation
- Read-only with keyboard scrolling (arrows, Page Up/Down, Ctrl+Home/End)

## CSV View

- Automatically shown for `.csv` files
- Parses comma-separated values with support for quoted fields (RFC 4180)
- Renders data as an aligned table with pipe-delimited columns, similar to markdown tables
- The first row is treated as the header and displayed in bold
- A separator line is drawn below the header
- Column widths are computed from the widest cell in each column, capped to fit the available screen width
- Cells containing numbers, percentages, or currency values are right-aligned; others are left-aligned
- Cell text word-wraps within its column when the content exceeds the column width
- Read-only; scrolling with keyboard (arrows, Page Up/Down, Ctrl+Home/End) and mouse wheel
- Press Escape to return to text editing mode

## Spell Check

- Spell checking has three modes: `auto_detect` (default), `enabled`, and `disabled`. The mode is persisted in config.
- When enabled (or auto-detected as available), misspellings are underlined in the text editor and the right-click context menu offers suggestions plus an "Add to dictionary" action.
- The platform spell checker is created lazily; if no platform checker is available, spell check is silently inactive.
- Command-line option `/spell:<word>` (or `--spell:<word>`) prints diagnostics about the spell checker and suggestions for the given word to stdout, then exits without starting the GUI. Intended for diagnosing spell-check availability.

## Persisted Configuration

Configuration is read at startup and written only on normal interactive shutdown. Non-interactive entry points (`/test`, `/spell:`) never write the config. Persisted values include:

- Last open root folder and last open document within it
- Recent root folders list (up to 8) and per-folder last open document
- Word wrap on/off
- Spell check mode
- Window size, position, and maximized state
- Text view font size and list/panel font size
- Vertical panel splitter ratio

## Command Line

- `noterad64d.exe /test` — run unit tests, write results to stdout, exit with code 0 on success or 1 on any failure. Does not start the GUI.
- `noterad64d.exe /spell:<word>` — print spell-checker diagnostics and suggestions for `<word>`. Does not start the GUI.
- Any other argument is treated as a file path to open at startup.

