# Noterad

[![Build](https://github.com/ZacWalk/noterad/actions/workflows/build.yml/badge.svg)](https://github.com/ZacWalk/noterad/actions/workflows/build.yml)

A lightweight Windows text editor for working across a folder full of notes, logs and data files. Point it at a folder, search everything in it, and read or edit whatever comes back.

Written in C++ with no dependencies beyond the Win32 API and rendered with plain GDI. It starts instantly and typically uses a few megabytes of memory.

Still a work in progress. (for about 10 years)

![Noterad screenshot](screenshot.png)

## Features

- **Folder browser** — navigate a root folder; create, rename and delete files inline
- **Multi-file search** — live search across the folder, grouped by file with highlighted matches (`Ctrl+Shift+F`)
- **Four document views** — text, Markdown, CSV table and hex, chosen automatically and remembered per file
- **Syntax highlighting** — C++, Rust, Python, PowerShell and Markdown
- **Editing** — unlimited undo, word wrap, indent/unindent, spell check
- **Utilities** — JSON reformat (`Ctrl+R`), sort and de-duplicate lines, evaluate a selected expression (`Ctrl+E`)
- **Session memory** — restores the recent root folders and the last document open in each

Open documents are never closed behind your back: unsaved files stay in memory with their full undo history and are shown in red. You are prompted to save only when changing folder or exiting.

## Building

From an x64 Developer PowerShell:

```
.\dd.ps1 build
```

`dd.ps1` locates Visual Studio, enters the MSVC environment and falls back to the
CMake and Ninja that ship with it, so nothing extra needs installing. To drive
CMake directly: `cmake --preset release && cmake --build --preset release`.

The platform layer comes from the separate
[platform-h](https://github.com/ZacWalk/platform-h) repository via `FetchContent`;
a sibling `../platform-h` checkout is used automatically when present.

Output is `exe\noterad-64.exe` (Release) or `exe\noterad-64d.exe` (Debug).

## Testing

```
.\dd.ps1 test
```

Runs the unit tests to stdout without starting the GUI, exiting 0 on success and 1 on any failure.

## Documentation

- [docs/design.md](docs/design.md) — architecture, views, keyboard reference and configuration
- [AGENTS.md](AGENTS.md) — conventions for contributors and coding agents

## License

[MIT](LICENSE)
