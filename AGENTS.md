# AGENTS.md

Noterad is a lightweight Windows text editor for research across folders of text files. See [README.md](README.md) for what it does and [docs/design.md](docs/design.md) for how it works — read the design document before making structural changes, and update it when you change high-level behaviour.

## Non-negotiables

1. **Windows-only code lives in `platform_win.cpp`.** Everything above the platform layer talks to `pf::` abstractions declared in `platform.h`. No `windows.h` types, Win32 API calls, `HWND`/`HDC`, or Win32 constants outside `platform.h` / `platform_win.cpp`. If a change needs a new OS capability, add it to `platform.h` first.
2. **Build with `noterad.sln`.** `msbuild noterad.sln /p:Configuration=Debug /p:Platform=x64 /m`. Do not add build systems or third-party dependencies — the project has none by design.
3. **Run the tests.** `exe\noterad-64d.exe /test` — exits 0 on success, 1 on any failure. Add a test in `tests.cpp` for every behaviour you fix.
4. **Optimise for small and fast.** This is the point of the project. Avoid per-keystroke or per-paint allocation, avoid O(document) work for a local edit, prefer `string_view` and reusable buffers. Delete dead code rather than leaving it.
5. **Temporary files go in `tmp/`.**

## Conventions

- Modern C++ in `snake_case`. Some older code uses Win32 Hungarian (`nActualItems`, `pBuf`, `dwCookie`) — convert it when you touch it, do not add more.
- Text coordinates are **UTF‑8 byte offsets**, never codepoints or columns. Use `pf::utf8_prev` / `pf::utf8_next` to step; never `++`/`--` a byte index over text that may be non-ASCII.
- Views never repaint directly. Raise an `invalid::*` bit and let `app_idle` coalesce the work.
- Off-thread work must operate on a snapshot taken on the UI thread. The worker must not touch a live `document` or `index_item`.
- Comments explain *why*, in one line. Do not narrate what the next line does.

## Where things live

| Area | Files |
|---|---|
| Platform abstraction | `platform.h`, `platform_win.cpp` (entry point, windowing, drawing, files, config, clipboard, spell check, async) |
| Application | `app.h`, `app.cpp` (main window, panes, splitter, document index, search, session), `app_state.h` (state and testable logic) |
| Commands | `commands.h`, `commands.cpp` (`command_def` and lookup), `app_commands.cpp` (the command table and menu builder) |
| Text model | `document.h`, `document.cpp` (lines, selection, undo, load/save, JSON reformat, sort), `document_syntax.cpp` (C++, Rust, Python, PowerShell, Markdown, hex highlighters) |
| Document views | `view_base.h` → `view_text.h` → `view_doc.h` → `view_doc_edit.h` (editable) and `view_doc_readonly.h` → `view_doc_markdown.h`, `view_doc_csv.h`, `view_doc_hex.h` |
| Panel views | `view_list.h` → `view_list_files.h`, `view_list_search.h` |
| Widgets | `ui.h` (colours, `edit_box`, `caret_blinker`, `splitter`, `custom_scrollbar`) |
| Utilities | `util.h`/`util.cpp` (string ops, colour), `calc.h` (expression parser for Calculate Selection) |
| Tests | `test.h` (assertions and runner), `tests.cpp` |
| Build | `app.vcxproj` (+ `.filters`), `pch.h`, `resource.h` (icon ID only — menus and accelerators are built at runtime), `targetver.h` |

## Adding a command

Add one entry to the table in `app_state::make_commands` (`app_commands.cpp`): description, menu text, `command_id`, accelerator, optional enabled/checked predicates, and the lambda. That single entry drives the menu item, its enable/check state, the runtime accelerator and the generated About document. Do not introduce a parallel dispatch table.

Global accelerators fire regardless of focus, so a command that acts on "the selection" must decide what the focused pane means — the editor, the file list, or an inline edit box.

## Command line

| | |
|---|---|
| `exe\noterad-64d.exe /test` | Run unit tests to stdout, no GUI |
| `exe\noterad-64d.exe /spell:<word>` | Spell-checker diagnostics for `<word>`, no GUI |

Both accept `/x` and `--x`. Neither writes configuration.
