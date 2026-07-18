Noterad is a lightweight text editor with multi-file search, markdown, csv and other features, written in C++. It is intended to enable research using various text files.

All Windows-specific code should be abstracted into platform_win.cpp and accessed via platform-independent abstractions declared in platform.h.

When updating high level behaviour of the app, specification.md should be updated.

Place temporary files in a tmp/ folder (test output etc.)

Always build using noterad.sln

## Key Files

| File | Purpose |
|------|---------|
| `specification.md` | Detailed specification of all features and behaviors |
| `app.h` | Application types: event interfaces, search results, index items, app_events |
| `app.cpp` | Application logic: main window, menus, splitter, file I/O commands |
| `app_commands.cpp` | Command definitions: console and menu commands wired to app_state |
| `app_state.h` | Application state: document collection, file operations, testable app logic |
| `calc.h` | Calculator expression parser used by console math commands |
| `platform.h` | Platform-independent types, constants, window/draw abstractions, API declarations |
| `platform_win.cpp` | Win32 platform layer: entry point, windowing, timers, menus, file dialogs, spell check |
| `document.h` / `document.cpp` | Text document model: lines, selections, undo/redo, syntax highlighting, JSON reformat, sort |
| `view_base.h` | Base class for all views: scrolling state (view extent, content extent, scroll offset in pixels), scrollbar widgets |
| `view_text.h` | Text view base: line-based text display, text selection, font metrics, syntax highlighting, message routing |
| `view_doc.h` | Document view: selection, caret, word wrap, syntax-highlighted rendering |
| `view_doc_csv.h` | Read-only CSV table view for comma-separated value files |
| `view_doc_edit.h` | Editable document view: character input, cut/paste, indent/unindent |
| `view_doc_markdown.h` | Read-only markdown renderer: headings, bold, italic, links, lists |
| `view_doc_hex.h` | Hex view for binary files: offset, hex bytes, ASCII columns |
| `view_list.h` | Base class for panel views: scrolling, selection, keyboard navigation, collapsible items |
| `view_list_files.h` | Folder browser panel: tree navigation, expand/collapse, item rendering |
| `view_list_search.h` | Search panel: text input, result display, navigation |
| `document_syntax.cpp` | Syntax highlighting parsers (C++, plain text, hex, markdown) |
| `util.h` / `util.cpp` | Core utilities: string ops, geometry types, AES-256, SHA-256, base64/hex, URL encoding |
| `ui.h` | UI types: color constants, edit_box, caret_blinker, splitter, custom_scrollbar |
| `commands.h` / `commands.cpp` | Unified command system: `command_def` drives both console and menu, tokenizer, case-insensitive map lookup, help text |
| `test.h` | Lightweight test framework (assertions + runner) |
| `tests.cpp` | Unit tests for document editing, undo/redo, search, crypto, app state |
| `resource.h` | Win32 resource IDs for menus, accelerators, dialogs |
| `pch.h` / `pch.cpp` | Precompiled header: standard library includes |
| `targetver.h` | Windows SDK version targeting |

## Document Management

Multiple documents can be open simultaneously within the current folder. Each document is kept in memory as a `shared_ptr<document>` indexed by file path. Switching between files in the file browser does not prompt to save — modified documents remain in the background with their full undo history intact. Modified files are highlighted in red in the file list. The user is prompted to save modified documents only when changing the root folder or exiting the program.

## Testing

command-line options for testing:

 - noterad64d.exe /test            — Run unit tests and print results to stdout
 - noterad64d.exe /spell:<word>    — Print spell-checker diagnostics and suggestions for <word>
