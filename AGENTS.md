# AGENTS.md

Rethinkify is a lightweight Windows text editor for research across folders of text files. See [README.md](README.md) for what it does and [docs/design.md](docs/design.md) for how it works — read the design document before making structural changes, and update it when you change high-level behaviour.

## Non-negotiables

1. **Windows-only code lives in the platform layer.** Everything in this repo talks to `pf::` abstractions declared in `platform.h`, which lives in the shared **platform-h** repository along with its Win32 implementation. No `windows.h` types, Win32 API calls, `HWND`/`HDC`, or Win32 constants anywhere in this repo. A change that needs a new OS capability goes into platform-h first, with its test in `tests/platform_tests.cpp` — then push it and bump `GIT_TAG` in `CMakeLists.txt`, or CI and a fresh clone build against the old revision. The other apps build against the same header, so a signature change breaks them until they bump their own pin.
2. **Build with CMake + Ninja.** `.\dd.ps1 build -Config Debug`, or `cmake --preset debug && cmake --build --preset debug` from an x64 Developer PowerShell. platform-h is pulled in by `FetchContent`, preferring a sibling `..\platform-h` checkout when one exists — develop the two together there. Do not add other dependencies.
3. **Run the tests.** `.\dd.ps1 test` (or `exe\rethinkify-64d.exe /test`) — exits 0 on success, 1 on any failure. Add a test in `tests.cpp` for every behaviour you fix, and run platform-h's own suite after touching it.
4. **Optimise for small and fast.** This is the point of the project. Avoid per-keystroke or per-paint allocation, avoid O(document) work for a local edit, prefer `string_view` and reusable buffers. Delete dead code rather than leaving it.
5. **Temporary files go in `tmp/`.**

## Conventions

- Modern C++ in `snake_case`. Some older code uses Win32 Hungarian (`nActualItems`, `pBuf`, `dwCookie`) — convert it when you touch it, do not add more.
- Text coordinates are **UTF‑8 byte offsets**, never codepoints or columns. Use `pf::utf8_prev` / `pf::utf8_next` to step; never `++`/`--` a byte index over text that may be non-ASCII.
- Views never repaint directly. Raise an `invalid::*` bit and let `app_idle` coalesce the work.
- Off-thread work must operate on a snapshot taken on the UI thread. The worker must not touch a live `document` or `index_item`. A hosted agent adds a reader thread per pipe; those own nothing but a byte buffer and marshal every line back through `pf::run_ui`.
- A `document` belongs to one view's event sink. The agent transcript has its own, because routing its edits to the document pane would corrupt that pane's wrap cache.
- Comments explain *why*, in one line. Do not narrate what the next line does.

## Where things live

| Area | Files |
|---|---|
| Platform abstraction | the separate **platform-h** repo: `platform.h` and `platform_win.cpp` (entry point, windowing, drawing, files, config, clipboard, spell check, async, child processes, path containment), shared with the other apps |
| Application | `app.h`, `app.cpp` (main window, panes, splitters, document index, search, session), `app_state.h` (state and testable logic) |
| Commands | `commands.h`, `commands.cpp` (`command_def` and lookup), `app_commands.cpp` (the command table and menu builder) |
| Text model | `document.h`, `document.cpp` (lines, selection, undo, load/save, JSON reformat, sort), `document_syntax.cpp` (C++, Rust, Python, PowerShell, Markdown, hex highlighters) |
| Document views | `view_base.h` → `view_text.h` → `view_doc.h` → `view_doc_edit.h` (editable) → `view_agent_input.h`, and `view_doc_readonly.h` → `view_doc_markdown.h`, `view_doc_csv.h`, `view_doc_hex.h`, `view_agent.h` |
| Panel views | `view_list.h` → `view_list_files.h`, `view_list_search.h` |
| Agent | `acp.h`/`acp.cpp` (Agent Client Protocol), `agent_session.h`/`.cpp` (`session.md` format, slash commands), `agent_host.h`/`.cpp` (process, turn, permissions) |
| Widgets | `ui.h` (colours, `edit_box`, `caret_blinker`, `splitter`, `custom_scrollbar`) |
| Utilities | `util.h`/`util.cpp` (string ops, colour), `json.h`/`json.cpp` (JSON DOM), `calc.h` (expression parser for Calculate Selection), `gitignore.h` (index filtering) |
| Tests | `test.h` (assertions and runner), `tests.cpp` |
| Build | `CMakeLists.txt` (declares the app with `platform_add_app()` — icon, manifest and version info are generated, so there is no `.rc`), `CMakePresets.json`, `dd.ps1`, `pch.h`, `targetver.h` |

## Adding a command

Add one entry to the table in `app_state::make_commands` (`app_commands.cpp`): description, menu text, `command_id`, accelerator, optional enabled/checked predicates, and the lambda. That single entry drives the menu item, its enable/check state, the runtime accelerator and the generated About document. Do not introduce a parallel dispatch table, and do not also handle the key in a view — the accelerator table consumes it first, so the view branch would be dead code. A second binding for the same command goes in `accel_alt`.

Global accelerators fire regardless of focus, so a command that acts on "the selection" must decide what the focused pane means — the editor, the file list, or an inline edit box.

## Command line

| | |
|---|---|
| `exe\rethinkify-64d.exe /test` | Run unit tests to stdout, no GUI |
| `exe\rethinkify-64d.exe /spell:<word>` | Spell-checker diagnostics for `<word>`, no GUI |
| `exe\rethinkify-64d.exe /acp[:<prompt>]` | Agent protocol handshake against the real CLI, no GUI |
| `exe\rethinkify-64d.exe /agent:<prompt>` | One turn through the host the panel uses, no GUI |

All accept `/x` and `--x`. None writes configuration. **Output is only captured when stdout is piped** (`| Out-String`); redirecting to a file yields nothing, because the console binding reopens stdout.

The two agent modes exist because `pf::run_ui` callbacks never drain under `/test` — there is no message loop — so the process and protocol layers cannot be unit tested. Run `/agent:` after changing anything in `agent_host` or the platform's child-process code. Unit tests must never launch a real agent: inject `spawn` and `locate` on `agent_host`, or drive it with a fake transport through `connect`.
