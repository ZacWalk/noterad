# Noterad

[![Build](https://github.com/ZacWalk/noterad/actions/workflows/build.yml/badge.svg)](https://github.com/ZacWalk/noterad/actions/workflows/build.yml)

Noterad is a lightweight text editor with multi-file search, Markdown, CSV, and other features, written in C++. It is intended to support research using various text files.

Still a work in progress. (for about 10 years)

It has no dependencies and uses simple Win32 GDI rendering. It typically uses just a few megabytes of memory.

![Noterad screenshot](screenshot.png)

## Features

- **Folder browser** — navigate a root folder, open files, with inline rename, new file/folder, and delete to Recycle Bin
- **Multi-file search** — live search across the current folder, results grouped by file with highlighted matches (`Ctrl+Shift+F`)
- **Multiple document views** — text, Markdown, CSV table, and hex (for binary files), each remembered per document
- **Syntax highlighting** — C++, Markdown, and more
- **Editing** — undo/redo, cut/paste, indent/unindent, and spell check
- **Utilities** — JSON reformat, sort and remove duplicates, a calculator console, and AES-256 encryption
- **Session memory** — remembers recent root folders and the last open document per folder

## Building

Open `noterad.sln` in Visual Studio and build, or from the command line:

```
msbuild noterad.sln /p:Configuration=Release /p:Platform=x64
```

## Testing

Run the unit tests with the `/test` command-line option:

```
noterad64d.exe /test
```
