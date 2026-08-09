# Noterad — Design

How Noterad is put together and why. For what it does and how to build it, see [README.md](../README.md). For working-agreements when changing the code, see [AGENTS.md](../AGENTS.md).

## Goals

1. **Fast and small.** No dependencies beyond the Win32 API. Files load once into a single immutable buffer; lines are slices until edited. Typical memory use is a few megabytes.
2. **One obvious mental model.** Two panes. The left pane lists things to open; the right pane shows the current document. Everything else is a mode of one of those two.
3. **Nothing hidden.** No background writes, no autosave, no telemetry. Configuration is written at shutdown.

## Layers

```
platform.h / platform_win.cpp   OS abstraction: windows, input, drawing, files, config, clipboard, spell check, async
        ↑
app.cpp / app_state.h           Application: window layout, document index, search, commands, session
        ↑
view_*.h                        Panes: document views and list panels
document.* / document_syntax.*  Text model: lines, selection, undo, highlighting
util.* / calc.h                 Leaf utilities
```

`platform.h` declares OS-free types (`window_frame`, `frame_reactor`, `draw_context`, `file_path`, …); `platform_win.cpp` is the only implementation. Nothing above the platform layer includes `windows.h`.

### Threading

One UI thread and one worker thread. `pf::run_async` queues work onto the worker; `pf::run_ui` marshals results back, waking the message loop through `MsgWaitForMultipleObjects`. Only two operations run off the UI thread — **folder indexing** and **search** — and both take a snapshot of what they need on the UI thread first. The worker never dereferences a live `index_item` or `document`.

Opening a document is also asynchronous, so anything that depends on the loaded text (such as selecting a search match) must be passed to `load_doc` as a completion callback. Tests use `deferred_scheduler`, which queues tasks and drains them on `pump()`, so they exercise the production ordering rather than completing inline.

### Invalidation

Views never repaint directly. They set bits in an atomic mask (`invalid::doc`, `doc_layout`, `doc_caret`, `doc_scrollbar`, `list`, `windows`). The message loop calls `app_idle()` once per pump, which coalesces layout, scrollbar recalculation, caret update, list population and repaint into one pass.

The document model distinguishes two notifications, and the difference is what keeps typing cheap:

- `invalidate_lines(start, end)` — **repaint only**. Raised by selection and caret movement. It touches no layout and no highlighting.
- `lines_changed(start, end)` — **the text changed**. Marks those lines dirty for word wrap, lowers the syntax-highlight cookie watermark, and raises `doc_layout`.

`doc_view::layout()` then re-wraps only the dirty lines and patches the row prefix sum by the delta. A full re-wrap happens only when the line count changes, the document is replaced, the width changes, or word wrap is toggled.

## Window layout

```
┌───────────────┬─┬───────────────────────────────┐
│ left pane     │ │ document pane                 │
│ files  OR     │▓│ text / markdown / csv / hex   │
│ search        │ │                               │
└───────────────┴─┴───────────────────────────────┘
                 └ splitter (5px, DPI-scaled, ratio 0.05–0.95)
```

`view_mode` is the cross product of `view_content` (`edit_text`, `markdown`, `csv`, `hex`) and search-panel on/off. `app_state::set_mode` swaps the document pane's reactor for the matching view class and points the left pane at either the file list or the search panel.

## Document model

- A file is loaded once into an immutable `file_buffer` (capped at **2 MB**; larger files are truncated at a codepoint boundary and forced read-only).
- `document` is a `std::vector<document_line>`. A line is either an owned `std::string` or a `(buffer, offset, length)` slice; slices cost 32 bytes and transcode UTF‑16 on render. Byte length is cached, so `size()` is O(1).
- All coordinates (`text_location.x`) are **UTF‑8 byte offsets**, not columns or codepoints. Display columns are derived through a per-line expanded-length cache that accounts for tabs.
- **Undo** is a linear `vector<undo_item>` with a redo cursor. Each item is an ordered list of insert/erase steps, replayed forward to redo and in reverse to undo. `undo_group` is an RAII scope that commits on destruction. Dirty state is `_undo_pos != _saved_undo_pos`, so undoing back to the last save marks the document clean.
- **Save** writes to a temp file and moves it into place. Encoding is preserved to the extent that a BOM-less file never gains a BOM. Saving re-checks the on-disk timestamp and prompts if the file changed underneath.
- Documents live in the folder index and are **never evicted**. Switching files never prompts to save; unsaved documents stay in memory with their undo history and are shown in red. You are prompted only when changing the root folder or exiting.

### Syntax highlighting

Per-line, stateless except for a 32-bit carry cookie (in-comment / in-string flags) threaded from the previous line. Each highlighter resets and fills a sorted array of `text_block{char_pos, style}`. The view caches cookies with a "valid to line N" watermark and rescans backwards at most 1000 lines, so an edit invalidates highlighting in O(1). Languages: C++, Rust, Python, PowerShell, Markdown, hex, plain text.

## View hierarchy

```
pf::frame_reactor
└── view_base                  scroll offset and content extent, both in pixels
    ├── text_view              font metrics, screen lines, message bar, clipboard, zoom, Escape
    │   └── doc_view           document, caret, selection, word wrap, hit-testing, painting
    │       ├── edit_doc_view          the only writable view
    │       └── read_only_doc_view     no caret, no h-scroll, word wrap locked, keys scroll
    │           ├── markdown_doc_view
    │           ├── csv_doc_view
    │           └── hex_doc_view
    └── list_view              items, selection, hover, keyboard navigation
        ├── file_list_view     folder tree, inline rename, drag-drop
        └── search_list_view   search box, grouped results
```

The split between `edit_doc_view` and `read_only_doc_view` is what makes the read-only panes predictable: they have no caret, cannot scroll horizontally, ignore Alt+Z, ignore Shift, and their arrow keys scroll rather than move an invisible cursor. Escape always returns them to the text editor.

## Panes and what each one does

| Pane | Purpose | Opens with | Leaves with |
|---|---|---|---|
| **Folder browser** | Navigate the root folder; create, rename and delete files | default | — |
| **Search** | Live text search across the root folder | `Ctrl+Shift+F` | `Escape` |
| **Text editor** | The only place text can be changed | default | — |
| **Markdown preview** | Read rendered `.md` | `Ctrl+M`, auto for `.md`/`.markdown` | `Escape` |
| **CSV table** | Read `.csv` as an aligned table | auto for `.csv` | `Escape` |
| **Hex** | Read binary files | auto for binary content | `Escape` |

Each document remembers its own content view, so switching away and back restores what you were looking at.

### Folder browser

Click or arrow-key to preview a file (focus stays in the list); `Enter` opens it and moves focus to the editor. Clicking a folder expands or collapses it. Modified files are red; long names are ellipsized. Right-click gives New File, New Folder, Copy Path, Rename (`F2`) and Delete (Recycle Bin, with confirmation). New items are named `new-file.md` / `new-folder`, suffixed `-2`, `-3`, … until unique. Names are validated against path separators, `.`/`..`, trailing dots and reserved device names. Files can be dropped onto the panel to copy them in.

### Search

Typing runs a live search, debounced by 150 ms; a newer query cancels the one in flight. Results are grouped under a collapsible per-file header showing the relative path and match count. Selecting a match opens the file and selects the match — the document may still be loading, so the selection is applied from the load continuation rather than immediately. Focus stays in the panel until `Enter`. `F8` / `Shift+F8` jump between matches. Limits: **5,000 results** (the count reads "limit reached" when hit), files over **10 MB** skipped, binary files skipped (extension list plus a content sniff of the first block, taken from the same read used to scan the file).

### Text editor

Character input with unlimited per-document undo. Word wrap (`Alt+Z`) is application state, applied to every editable view and persisted. Tab/Shift+Tab indent and unindent; `Ctrl+R` reformats JSON; Edit ▸ Sort & Remove Duplicates sorts case-sensitively and de-duplicates; `Ctrl+E` replaces a selected arithmetic expression with its value. Double-click selects a word, the left margin selects lines, `Ctrl+click` selects a word, `Ctrl+click` in the margin selects everything. The active row carries a subtle highlight band while the editor has focus.

### Markdown preview

Headings (H1–H3, size-scaled), bold and italic (rendered as colour, not weight), links, ordered and unordered lists with hanging indents, and tables with wrapped cells, width-capped columns and numeric right-alignment.

### CSV table

RFC 4180 parsing including quoted fields. Aligned pipe-delimited columns, a brighter header row followed by a separator, width-capped columns with wrapped cell text, and numeric right-alignment.

### Hex

Offset (8 hex digits) | 16 bytes | ASCII.

## Commands and keyboard

One `std::vector<command_def>` (`app_state::make_commands`) is the single source of truth for the menu bar, the enable/check state, the runtime accelerator table and the generated About document. Each entry owns its own lambda; adding a command is one table entry. Commands report through the message bar at the top of the document pane.

There is exactly one accelerator dispatcher: the Win32 accelerator table built from the menu, which routes to the command's lambda. Commands are focus-aware rather than focus-routed — each one asks what has focus and acts, instead of re-dispatching a synthetic keystroke to another window.

Two rules cover every command:

- **Clipboard and delete** (`Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Delete`, `Ctrl+A`) act on whatever has focus — the editor, the file list, or the search/rename edit box. An inline edit box wins for copy and cut only when it has a selection, so `Ctrl+C` in the search panel still copies the selected result's path.
- **Text-editing commands** (undo, redo, reformat, sort, calculate, spell check) require the text editor: they are disabled while an inline edit box has focus, while a read-only preview is showing, and on a read-only document. `document::replace_text` also refuses on a read-only document, so no path can bypass the check.

`F5` refreshes the focused panel: the search when the search panel has focus, the folder index otherwise. `F8` / `Shift+F8` and `Alt+Z` are disabled outside the panes they apply to.

Help ▸ About (`F1`) and Help ▸ Run Tests (`Ctrl+T`) generate a read-only, never-dirty document from the command table. They appear in the folder browser so you can return to them, but they are not files and never prompt to save.

| | |
|---|---|
| **File** | `Ctrl+N` new · `Ctrl+O` open · `Ctrl+S` save · `Ctrl+Shift+S` save all · File ▸ Save As · File ▸ Exit |
| **Edit** | `Ctrl+Z` undo (`Alt+Backspace`) · `Ctrl+Y` redo · `Ctrl+X`/`Ctrl+C`/`Ctrl+V` (also `Shift+Del`, `Ctrl+Ins`, `Shift+Ins`) · `Delete` · `Ctrl+A` · `Ctrl+R` reformat JSON · `Ctrl+E` calculate selection · `Ctrl+Shift+P` spell check · Edit ▸ Sort & Remove Duplicates |
| **View** | `Alt+Z` word wrap (editor only) · `Ctrl+M` markdown preview · `F5` refresh · `F8` / `Shift+F8` next/previous result (search only) · `Ctrl+Shift+F` search · `Ctrl++` / `Ctrl+-` / `Ctrl+Wheel` zoom |
| **Help** | `Ctrl+T` run tests · `F1` about |
| **Caret** | arrows · `Ctrl+←/→` word · `Home`/`End` · `Ctrl+Home`/`Ctrl+End` · `PageUp`/`PageDown` · `Ctrl+↑/↓` scroll only · add `Shift` to extend the selection |
| **Editing** | `Tab` / `Shift+Tab` indent · `Backspace` · `Ctrl+Backspace` delete word left |
| **Read-only views** | arrows, `Home`/`End`, `PageUp`/`PageDown` scroll · `Escape` returns to the editor |
| **Panels** | `↑`/`↓` navigate and preview · `Enter` open (or expand/collapse a folder or search group) · `F2` rename · `Delete` delete file · `Escape` close search |

Help ▸ About (`F1`) generates the authoritative shortcut list from the command table at runtime.

### Text input

`WM_CHAR` delivers UTF-16 code units. The platform layer combines surrogate pairs and hands the application a `char32_t`, which the editor and every inline edit box encode to UTF-8 before inserting. Word navigation classifies any non-ASCII byte as a word byte, so a scan can never stop between the lead and continuation bytes of one codepoint.

## Spell check

Three modes — `auto_detect` (default; active for `.md` and `.txt`), `enabled`, `disabled` — persisted in config and toggled with `Ctrl+Shift+P`. Misspellings are underlined in the editor; right-click offers suggestions and *Add to Dictionary*. The platform checker is created lazily and spell check is silently inactive when none is available.

## Configuration

An INI beside the executable when that folder is writable, otherwise `%LOCALAPPDATA%\Noterad\noterad.ini`. Written at shutdown; command-line modes (`/test`, `/spell:`) never write it.

| Section | Keys |
|---|---|
| `Window` | `Left`, `Top`, `Right`, `Bottom`, `Maximized` |
| `Font` | `TextSize`, `ListSize` (clamped 8–72) |
| `Splitter` | `PanelRatio` (clamped 0.05–0.95, default 0.2) |
| `View` | `WordWrap`, `SpellCheck` |
| `Recent` | `Folder`, `Document` |
| `RecentFolders` | `Folder1`–`Folder8`, `Document1`–`Document8` |

Restored paths are validated: UNC and non-existent roots are skipped. Passing a file on the command line skips session restore entirely.

## Command line

| | |
|---|---|
| `noterad-64d.exe /test` | Run the unit tests to stdout; exit 0 on success, 1 on any failure. No GUI. |
| `noterad-64d.exe /spell:<word>` | Print spell-checker diagnostics and suggestions. No GUI. |
| `noterad-64d.exe <path>` | Open a file. Arguments starting with `/` or `-` are ignored; the last plain argument wins. |

Both `/x` and `--x` forms are accepted.

## Known limitations

- Documents are capped at 2 MB and never evicted from memory.
- Folder indexing is eager and recursive with no ignore list, so a repository root pulls in `.git` and build output.
- `frame_reactor::handle_message` still carries raw `wParam`/`lParam`, and DPI change notifications are decoded in the app layer — the last significant leak across the platform boundary.
- Text drag-and-drop within the editor is not implemented.
- Case-insensitive search folds bytes, so it degrades to case-sensitive for non-ASCII text.
- Saving a UTF-16 file writes UTF-8; only the presence or absence of a BOM is preserved.
- Markdown and CSV preview inherit the editor's selection model, which assumes uniform line heights, so selection there is not meaningful.
