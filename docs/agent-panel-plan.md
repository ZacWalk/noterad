# Agent panel — implementation plan

Adds a third pane on the right of the main window: a chat transcript with an input box, driven by
a real coding agent. Target is an elegant, lightweight equivalent of the VS Code chat view.

Branch: `agent-panel`.

## 1. How we talk to Copilot

GitHub Copilot CLI exposes an **ACP (Agent Client Protocol) server**: `copilot --acp --stdio`.
It speaks **JSON-RPC 2.0 as newline-delimited JSON over stdin/stdout**. That is the integration
point — it is what the "Copilot SDK" wraps, and it is exactly the shape VS Code-style clients use.

Why this and not the alternatives:

| Option | Verdict |
|---|---|
| **`copilot --acp --stdio` child process** | **Chosen.** Two pipes and a JSON parser. No dependency in our binary. Auth, models, tools, MCP servers and the whole agent loop are the CLI's problem. Streaming, cancellation, tool approval and user elicitation are all in the protocol already. |
| Copilot SDK (TypeScript / Python) | Requires Node or Python at runtime. Contradicts "no dependencies". The SDK is a wrapper over the same protocol. |
| Direct HTTPS to `api.githubcopilot.com` | We would have to implement device-flow OAuth, token refresh, SSE streaming, the tool-call loop, MCP client, and store a credential. Large, fragile, and undocumented. |

Consequences we get for free, and which shape the UI:

- `session/update` notifications stream `agent_message_chunk`, `agent_thought_chunk`, `tool_call`,
  `tool_call_update`, `plan` and `available_commands_update`.
- `session/request_permission` — the agent asks **us** to approve a tool call, and supplies the
  option list. This is the "prompt the user with a question and some answers" feature, built in.
- `elicitation/create` — the agent asks the user a structured question. Same UI widget.
- `fs/read_text_file` / `fs/write_text_file` — if we advertise these capabilities, the agent's file
  reads and writes are routed **through Rethinkify**. Edits then land in the open `document`, with
  undo, dirty marking and the existing save path. This is the single biggest reason to prefer ACP.
- `session/cancel` — `/stop`.
- Slash commands the agent advertises (`/context`, `/usage`, `/model`, `/plan`, …) pass straight
  through, so our own `/` set stays tiny.

Requirement: `copilot` on `PATH` (winget `GitHub.Copilot`), already logged in. When it is missing
the panel shows a one-line explanation instead of failing.

## 2. New files

| File | Contents |
|---|---|
| `json.h` / `json.cpp` | Minimal JSON DOM written for this project: parse, build, serialise. UTF-8 in, UTF-8 out. ~400 lines, no third-party code. Pure, fully unit-testable. |
| `acp.h` / `acp.cpp` | JSON-RPC framing over an abstract byte transport, request/response correlation, notification dispatch, and the client-side method handlers. Knows nothing about windows. |
| `agent_session.h` / `agent_session.cpp` | `session.md` parse and serialise, the derived entry index, the slash-command parser, and the state machine (`idle` / `starting` / `running` / `awaiting_user` / `failed`). Pure logic over a `document`. |
| `view_agent.h` | The pane: the `session.md` document above, input box below, interactive option blocks. |

Additions to existing files: `platform.h` / `platform_win.cpp` (child process), `app.h`
(`view_focus::agent`, `zoom_target::agent`, new `invalid::` bits), `app_state.h` / `app.cpp`
(third window, second splitter, config), `app_commands.cpp` (commands), `tests.cpp`, and the
`.vcxproj` / `.filters` pair.

## 3. Platform layer

`platform.h` gains one abstraction; `platform_win.cpp` is the only implementation.

```cpp
struct child_process
{
    virtual ~child_process() = default;
    virtual bool write_line(std::string_view) = 0;   // UTF-8 + '\n'
    virtual void close_input() = 0;
    virtual void terminate() = 0;
    virtual bool is_running() const = 0;
};

// on_line and on_exit are invoked on the UI thread.
std::unique_ptr<child_process> spawn_child_process(
    const file_path& exe, std::span<const std::string> args, const file_path& working_dir,
    std::function<void(std::string_view)> on_line,     // stdout, one NDJSON line
    std::function<void(std::string_view)> on_stderr_line,
    std::function<void(int)> on_exit);

file_path find_executable(std::string_view name);      // PATH + PATHEXT
```

`CreateProcess` with three anonymous pipes and `CREATE_NO_WINDOW`. A **dedicated reader thread per
process** blocks on `ReadFile`, splits the buffer on `\n`, and hands each complete line to the UI
thread via `pf::run_ui`. This is a third thread beyond the documented UI + worker pair; it exists
only because a blocking pipe read cannot share the single worker with indexing and search. It
touches no `document` and no `index_item` — it owns nothing but a byte buffer.

Shutdown: close stdin, wait briefly, then `TerminateProcess`. The process is killed in
`app_destroy` so we never leak a `copilot` process.

## 4. Protocol client (`acp.h`)

```cpp
class acp_client
{
public:
    // Transport is injectable so tests drive it without a process.
    struct transport { virtual bool send(std::string_view line) = 0; ... };

    void on_line(std::string_view);                  // feed one NDJSON line

    void initialize(...);                            // -> session/new
    request_id prompt(std::string_view text);
    void cancel();

    std::function<void(const json::value&)> on_session_update;
    std::function<void(request_id, const json::value&)> on_permission_request;
    std::function<void(request_id, const json::value&)> on_elicitation;
    std::function<std::string(const pf::file_path&, int line, int limit)> on_read_text_file;
    std::function<bool(const pf::file_path&, std::string_view)> on_write_text_file;
};
```

Handles: `initialize` handshake and version negotiation, `session/new` with `cwd` = the root folder,
capability advertisement (`fs.readTextFile`, `fs.writeTextFile`, elicitation; `terminal/*` is
declined), correlation of pending requests, and replies to agent→client calls.

Malformed lines are dropped with a message-bar note, never thrown. A response to an unknown id is
ignored. Everything the agent sends is treated as untrusted text.

## 5. The transcript is a file (`session.md`)

The conversation is **not** a UI-only buffer. It is `session.md` in the root folder: a real file, in
the index, opened as an ordinary `document`, and reloaded when that folder is reopened. One file per
root folder, so each project keeps its own history.

This is the cheapest possible design and it buys a lot — these are the point, not side effects:

- history survives restart, per folder;
- the user edits history in the editor like any other file, and the agent then sees the edited
  version — pruning a wrong turn actually removes it from the agent's context;
- `Ctrl+Z` undoes an agent turn, because it is a `document` with the normal undo stack;
- it renders in the existing markdown preview, is found by `Ctrl+Shift+F`, copies, saves and shows
  as modified in the file list — all with no new code;
- session options (chosen model, yolo) live in the file, so they travel with the history.

### Model

`agent_session` is therefore a **parser and serialiser over a `document`**, not a second store:

```cpp
enum class agent_entry_kind { session, user, agent, thought, tool_call, plan, question, error, note };

struct agent_option { std::string label; bool chosen = false; };

struct agent_entry
{
    agent_entry_kind kind;
    int first_line = 0, last_line = 0;     // into the document
    std::string title;                     // tool name, question text
    std::vector<agent_option> options;
    bool complete = false;
};
```

The entry vector is **derived state**, rebuilt from the document text and invalidated by
`lines_changed` — the same pattern as the syntax-highlight cookies, and with the same watermark
trick so an edit does not rescan the whole file. Live protocol state that has no place on disk
(pending JSON-RPC reply ids, the request in flight) is held separately and keyed by entry index.

Streaming appends to the last line through `document::replace_text`, so a token stream costs one
line re-layout, not a rebuild.

### Format

Markdown, because it has to be pleasant to edit by hand and it renders in the preview we already
have. `##` opens an entry and names the role; `###` opens a block inside it.

```markdown
<!-- rethinkify agent session v1 -->

## Session
- model: claude-sonnet-4.5
- yolo: off
- updated: 2026-08-15T09:41:12Z

## You
Fix the wrap cache splice bug.

## Agent
`line_count_changed` splices the parallel arrays, but a pending dirty range is
still in the old numbering.

### Tool: shell — approved
    git status

### Question: which file should I change?
- [x] src/view_doc.h
- [ ] src/document.cpp
```

Rules that make it safe to hand-edit:

- **Parsing is total.** Anything unrecognised becomes a `note` entry and is preserved verbatim. A
  malformed file never errors, never loses text and never blocks the panel.
- **Round-trip is byte-exact.** Serialising a parsed file reproduces it exactly; a test enforces
  this over a corpus including deliberately mangled input. We only ever append, or rewrite the
  specific lines we authored.
- Options are markdown task-list items and the ticked one is the answer, so answering a question in
  the file by hand does the same thing as clicking it.
- Session options are a plain bullet list under `## Session`. An unknown key is left alone.

### Continuity across restarts

The agent's own ACP session is process-scoped and cannot survive a restart, and after the user edits
the file it would be wrong anyway. So on reopen we start a **fresh** ACP session and supply
`session.md` as context on the first prompt of the run. The file is the history, which is exactly
why editing it works.

### Writing

`session.md` is written **once at the end of a turn**, not per token. Between turns it is an ordinary
modified document — red in the file list, on the exit prompt, saveable with `Ctrl+S`.

This is a deliberate, single exception to "no background writes" in `design.md`, and it needs to be
recorded there as one: it is one write per turn, to a file the user can see, caused by an action the
user initiated. Nothing else about the rule changes.

`/clear` empties the file as one undoable edit, so a mis-typed `/clear` is `Ctrl+Z`.

Size is the one real risk: documents are capped at 2 MB and a long-running session will get there.
When the file passes a threshold the oldest entries are moved to `session-<date>.md` beside it and a
`note` entry records the move, so nothing is silently lost.

## 6. Slash commands

Slash commands are parsed **before** anything is sent:

| Input | Effect |
|---|---|
| `/help`, `/h` | Print prompt help into the transcript, generated from the local command table plus the agent's advertised commands, so it can never drift. Never reaches the agent. |
| `/clear`, `/c` | Empty `session.md` and start a fresh ACP session. One undoable edit. Never reaches the agent. |
| `/stop`, `/s` | `session/cancel`. The only way to stop a turn. |
| `/models`, `/m` | List models, render them as a selection block, write the choice to `## Session`. |
| `/yolo` | Toggle auto-approval of tool calls, and record it in `## Session`. See §10. |
| any other `/x` | Forwarded verbatim if the agent advertised `x` in `available_commands_update`; otherwise "unknown command — try /help" locally. Gives us `/context`, `/usage`, `/plan`, `/review` for free. |

`/help` output is built the same way Help ▸ About is: one table of local commands is the single
source of truth for the parser, the help text and the unknown-command message. Advertised agent
commands are appended from the last `available_commands_update`, with their descriptions.

Model selection: prefer `session/set_model` when the agent advertises it; otherwise send `/model`
as a prompt and render the returned option list. Either way the result is the same option-block UI,
and the chosen model is written to `## Session` and re-applied when the session is reopened.

## 7. The pane (`view_agent.h`)

```
┌─ list ─┬─┬── document ──────────────┬─┬─ agent ─────────┐
│ files  │ │ text / markdown / …      │ │ transcript      │
│ search │▓│                          │▓│  (scrollable)   │
│        │ │                          │ ├─────────────────┤
│        │ │                          │ │ input (spell)   │
└────────┴─┴──────────────────────────┴─┴─────────────────┘
   panel splitter                        agent splitter
```

Derives from `read_only_doc_view` so it inherits scrolling, word wrap, zoom, selection-for-copy and
`Ctrl+A`. It renders `session.md` — the same document the editor can open. Two additions:

1. **A reserved footer.** `text_view` already offsets content by `message_bar_height()`; the agent
   view adds a symmetric `bottom_offset()` for the input box. The input is an `edit_box_widget`,
   the same one the search and rename fields use, so caret blink, selection, clipboard, word-skip
   and UTF-8 handling all come along. It grows with content to a cap of **6 rows**, then scrolls.
2. **Interactive option blocks.** A pending permission or question renders its task-list items as
   numbered rows. `1`–`9` picks one, `↑`/`↓`+`Enter` picks one, a click picks one; the answer goes
   back as the JSON-RPC response *and* is ticked in the file. The same widget serves `/models`.

Behaviour:

- **Auto-scroll** to the bottom while streaming, but only if the view was already at the bottom.
  Scrolling up pins the view and shows a "jump to latest" affordance in the message bar.
- **Input history**: `↑`/`↓` in an empty input walks previous prompts.
- `Shift+Enter` inserts a newline, `Enter` submits.
- **`Escape` only moves focus** back to the editor — it never stops the agent, so it is safe to press
  while a turn is running. Stopping is `/s`. `F4` comes straight back to the input from anywhere,
  which is what makes that split work.
- A running turn is visible in the message bar, with the `/s` hint, so "how do I stop this" is
  always on screen.
- **Spell check in the input** is new. `edit_box` gains a `draw_spelling` helper that reuses
  `spell_check_word` and the squiggle drawing already in `view_doc.h`; the search box opts out, the
  agent input opts in. Right-click offers suggestions, as in the editor.

## 8. Integration into `app_state`

- `_agent_window` child window and `_agent_splitter{orientation::vertical, 0.75}`. `layout_views`
  computes the right split first and clamps it so the document pane keeps a minimum width and the
  two splitters cannot cross. Both splitters are hit-tested in `handle_mouse`.
- `view_focus::agent` and `zoom_target::agent`, so `Ctrl+±` and `Ctrl+Wheel` size the agent pane
  independently, and the focus-aware commands (copy, paste, select-all, delete) know about a third
  claimant. `can_edit_document()` stays false while the agent input has focus, exactly as it does
  for the search box.
- New invalidation bits `invalid::agent_layout` and `invalid::agent_populate`, coalesced in
  `app_idle` like the others. The view never repaints directly, including during streaming.
- Commands added to `app_state::make_commands` (one entry each, per the house rule):
  `view_toggle_agent` (`Ctrl+Shift+A`), `agent_focus_input` (**`F4`** — currently unbound; the
  platform layer is missing the `F4` constant and gains it), `agent_clear`, `agent_stop`,
  `agent_send_selection` — "Ask agent about selection", which seeds the input with the selected text
  and a file/line reference. `F4` shows the pane if it is hidden, then focuses the input, so it is
  one key from anywhere.
- **Independent config**, its own section. It holds only what is about the *pane*; everything about
  the *conversation* lives in `session.md`:

  | Section | Keys |
  |---|---|
  | `Agent` | `Visible`, `SplitterRatio` (0.05–0.95), `FontSize` (8–72), `Executable` |

## 9. Editing through the agent

`fs/write_text_file` resolves the path against the index. If the file is open, the change goes
through `document::replace_text` inside an `undo_group`, so `Ctrl+Z` undoes an agent edit like any
other. If it is not open we load it first. Result: agent edits are visible, reviewable and
reversible, and never bypass the read-only checks.

`fs/read_text_file` serves the in-memory document when one exists, so the agent sees unsaved work —
the thing that makes an in-editor agent better than a terminal one.

## 10. Security

The panel runs a program that can modify files and execute shell commands. Non-negotiables:

- **Default is prompt-for-everything.** We start the CLI with no `--allow-all-tools` and no
  `--allow-tool`. Every tool call arrives as `session/request_permission` and is shown to the user
  with the exact command. "Allow for this session" is offered; "always allow" is not.
- **`/yolo` auto-approves**, answering every permission request with the allow option without
  asking. It is a deliberate, visible escape hatch:
  - it is recorded in `## Session`, so it survives a restart — but it is **never re-armed silently**.
    Loading a session whose flag is on shows a confirmation block before the first turn: resume with
    auto-approval, or start with it off. `session.md` is a file on disk that anything can write, so
    the flag is a *request*, not an instruction;
  - turning it on writes a warning entry naming what it permits, and the message bar shows a
    persistent `YOLO` marker while it is on;
  - auto-approved calls are still written to the transcript, so the record of what ran is intact;
  - it turns off on `/clear` and on a failed session.
- **`session.md` is untrusted input.** It is user-editable, agent-written and on disk, so it is a
  prompt-injection surface — and we feed it back to the agent as history. It is parsed as data only:
  a heading in the file never grants a permission, never selects a model without the same
  confirmation UI, and never causes a tool to run. Everything it can express is something the user
  could have typed.
- **Path containment.** `fs/read_text_file` and `fs/write_text_file` reject any path that does not
  canonicalise to inside the root folder, and reject a read-only or truncated document. Checked
  after canonicalisation, so `..` and symlink traversal cannot escape.
- **Agent output is data.** Transcript text is never interpreted as a command, a path or markup with
  side effects. A `/` at the start of an *agent* message is not a slash command.
- **No secrets through us.** We never read, store or log a token; authentication is entirely the
  CLI's. stderr is shown but truncated, and never written to config.
- **Bounded.** Transcript entries, entry length and pending-request count are capped, so a
  misbehaving or hostile agent cannot exhaust memory. The child process is killed on exit.
- Prompt injection from repository content is a real risk here: file content the agent reads can
  contain instructions. The permission gate is the mitigation — which is why it stays on by default.

## 11. Stages

Each stage builds with `msbuild rethinkify.sln /p:Configuration=Debug /p:Platform=x64 /m`, passes
`exe\rethinkify-64d.exe /test`, and is a usable increment.

| # | Stage | Tests added |
|---|---|---|
| 1 | `json.h/.cpp` — our own parser, no dependency | Round-trip, unicode escapes and surrogate pairs, deep nesting, malformed input, number edge cases, huge strings |
| 2 | `child_process` in the platform layer | Echo a known exe, line splitting across read boundaries, exit notification, kill |
| 3 | `acp_client` against a fake transport | Handshake, id correlation, notification dispatch, unknown method, malformed line, split line |
| 4 | `session.md` parse / serialise | Byte-exact round-trip over a corpus, hand-mangled input degrades to `note` entries, entry boundaries, option ticks, `## Session` options, unknown keys preserved |
| 5 | `agent_session` streaming + slash parser | Chunk append hits one line, `/help` `/clear` `/stop` `/models` `/yolo` `/unknown`, help covers every command in the table, tool-call lifecycle |
| 6 | Pane, splitter, config, commands | Splitter clamping, `F4` focus from each pane, `Escape` moves focus without cancelling a turn, config round-trip, zoom independence, input growth to 6 rows |
| 7 | Option blocks: permission, question, `/models` | Keyboard and click selection, reply payload shape, tick written to the file, answering by hand-editing the file |
| 8 | `fs/*` bridge into the document model | Undo of an agent edit, unsaved content served to reads, path containment rejection |
| 9 | Session lifecycle | Reload on reopen, fresh ACP session seeded from the file, edited history reaches the agent, yolo-on-load confirmation, size rollover to `session-<date>.md` |
| 10 | Polish: history, auto-scroll pinning, input spell check, stderr surfacing | Auto-scroll pinning, history navigation |

Stages 1–5 are pure logic and testable headless; the GUI work starts at 6.

`docs/design.md` and `AGENTS.md` are updated at stage 6 (layout, threading, config table, and the
`session.md` exception to "no background writes") and again at stage 10 (commands and keyboard).

## 12. Decisions

- **Own JSON parser.** `json.h/.cpp`, written for this, no third-party code — the project has no
  dependencies by design and this does not start.
- **No terminal capability.** We decline `terminal/*`; live command output is not needed. Shell
  commands still surface as permission requests with the exact command line, so nothing is hidden —
  only the streaming output is absent.
- **`Escape` moves focus, it does not stop.** `/s` stops. `F4` focuses the input from anywhere.
- **Approval is prompt-by-default, with `/yolo`** as an explicit override, recorded in the session
  file and re-confirmed on load.
- **The transcript is `session.md`** in the root folder: persisted, hand-editable, and the source of
  history the agent is given.
- **Input grows to 6 rows**, then scrolls.

## 13. Open questions

1. Should `session.md` be suggested for `.gitignore` when the root folder is a git repo, or is
   committing the conversation a feature? Proposal: leave it alone, say nothing — it is the user's
   file.
2. Size threshold for rolling old entries into `session-<date>.md`. Proposal: 1 MB, half the
   document cap.
