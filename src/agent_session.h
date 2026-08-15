// agent_session.h — session.md: the conversation as an editable markdown file

#pragma once

#include "json.h"

enum class agent_entry_kind
{
	note,
	session,
	user,
	agent,
	thought,
	error,
	tool_call,
	question,
	plan,
};

struct agent_option
{
	std::string label;
	bool chosen = false;
	int line = 0;
};

// An entry owns a range of lines; the text itself always stays in the file
struct agent_entry
{
	agent_entry_kind kind = agent_entry_kind::note;
	int first_line = 0;
	int last_line = 0; // exclusive
	std::string title;
	std::string status;
	std::vector<agent_option> options;

	[[nodiscard]] bool has_options() const { return !options.empty(); }
	[[nodiscard]] int chosen_option() const;
};

namespace agent_session
{
	inline constexpr std::string_view file_name = "session.md";
	inline constexpr std::string_view file_header = "<!-- rethinkify agent session v1 -->";

	// Only these exact headings open an entry, so a heading inside agent prose does not split it
	[[nodiscard]] std::string_view role_heading(agent_entry_kind kind);

	// Parsing is total: anything unrecognised stays in the entry it appears in
	[[nodiscard]] std::vector<agent_entry> parse(std::span<const std::string> lines);

	struct options
	{
		std::string model;
		bool yolo = false;
	};

	[[nodiscard]] options read_options(std::span<const std::string> lines,
	                                   const std::vector<agent_entry>& entries);

	// The conversation so far, rendered for an agent that was not there for it. Bounded to
	// 'max_bytes' keeping the most recent entries, because only the tail still informs the
	// next turn. Local chatter — the session block, notes and thinking — is left out.
	[[nodiscard]] std::string transcript_digest(std::span<const std::string> lines, size_t max_bytes);

	// Escapes a line that would otherwise be read as a heading
	[[nodiscard]] std::string escape_body_line(std::string_view line);

	[[nodiscard]] std::vector<std::string> split_body(std::string_view body);

	void ensure_header(std::vector<std::string>& lines);

	// Appends an entry, separated from what came before by exactly one blank line
	void append_entry(std::vector<std::string>& lines, agent_entry_kind kind,
	                  std::string_view title, std::string_view body);

	// Line index where append_entry would start writing
	[[nodiscard]] int append_position(std::span<const std::string> lines);

	// Appends to the last entry, continuing its final line when it is still open
	void append_chunk(std::vector<std::string>& lines, std::string_view text);

	// Writes a key in the Session block, adding the block or the key when missing.
	// Any key it does not recognise is left untouched.
	void set_option(std::vector<std::string>& lines, std::string_view key, std::string_view value);

	void choose_option(std::vector<std::string>& lines, const agent_entry& entry, size_t index);

	[[nodiscard]] std::string to_text(std::span<const std::string> lines);
	[[nodiscard]] std::vector<std::string> to_lines(std::string_view text);
}

// ── Slash commands ──────────────────────────────────────────────────────────────

enum class agent_command
{
	prompt, // ordinary text for the agent
	help,
	clear,
	stop,
	models,
	yolo,
	forward, // a slash command the agent advertised
	unknown,
};

struct agent_command_def
{
	std::string_view name;
	std::string_view alias;
	std::string_view description;
	agent_command command;
};

struct agent_command_parse
{
	agent_command command = agent_command::prompt;
	std::string name; // for forward and unknown, without the leading slash
	std::string text; // the prompt, or the argument that followed the command
};

// A command the agent advertised through available_commands_update
struct advertised_command
{
	std::string name;
	std::string description;
};

namespace agent_session
{
	// One table drives the parser, the help text and the unknown-command message
	[[nodiscard]] std::span<const agent_command_def> command_table();

	[[nodiscard]] agent_command_parse parse_command(std::string_view input,
	                                                std::span<const advertised_command> advertised = {});

	[[nodiscard]] std::string help_text(std::span<const advertised_command> advertised = {});

	[[nodiscard]] std::vector<advertised_command> read_advertised_commands(const json::value& update);
}

// ── Streaming ───────────────────────────────────────────────────────────────────

// Live state that has no place on disk: what the current turn is still writing
struct agent_stream_state
{
	agent_entry_kind open_kind = agent_entry_kind::note;
	bool entry_open = false;
	std::map<std::string, int> tool_lines; // toolCallId -> heading line

	// First line the last edit touched, so a view can patch instead of rebuilding
	int dirty_from = -1;

	void reset()
	{
		entry_open = false;
		open_kind = agent_entry_kind::note;
		tool_lines.clear();
		dirty_from = -1;
	}
};

namespace agent_session
{
	// Applies one session/update payload to the file. 'update' is params["update"].
	void apply_update(std::vector<std::string>& lines, agent_stream_state& state, const json::value& update);

	// Marks every tool call still shown as pending or running as cancelled, for a turn the
	// agent is abandoning and will therefore never report on again
	void mark_unfinished_tools_cancelled(std::vector<std::string>& lines, agent_stream_state& state);
}
