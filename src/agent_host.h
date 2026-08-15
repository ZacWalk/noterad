// agent_host.h — Owns the agent process, the protocol client and the turn in progress

#pragma once

#include "acp.h"
#include "agent_session.h"

// Carries protocol lines to a spawned agent
class child_transport final : public acp::transport
{
public:
	pf::child_process* process = nullptr;

	bool send_line(const std::string_view line) override
	{
		return process && process->write_line(line);
	}
};

class agent_host
{
public:
	struct events
	{
		virtual ~events() = default;

		// Replaces transcript lines from 'first' onwards with 'replacement'
		virtual void transcript_changed(int first, std::span<const std::string> replacement) = 0;
		virtual void agent_status_changed(std::string_view status) = 0;

		// The agent's file access, so its reads see unsaved work and its writes are undoable.
		// Both refuse a path outside the root folder.
		virtual bool read_file(const pf::file_path& path, std::string& content, std::string& error) = 0;
		virtual bool write_file(const pf::file_path& path, std::string_view content, std::string& error) = 0;

		// The transcript has reached a resting point and is worth writing to disk
		virtual void transcript_settled() = 0;
	};

	explicit agent_host(events& sink) : _events(sink)
	{
	}

	~agent_host();

	// The transcript document belongs to the app, so clearing it is the app's job
	std::function<void()> on_clear;

	// Injectable so tests never launch a real process
	using spawn_function = std::function<pf::child_process_ptr(const pf::file_path& exe,
	                                                           std::span<const std::string> args,
	                                                           const pf::file_path& working_dir,
	                                                           pf::child_process_callbacks callbacks)>;

	spawn_function spawn = pf::spawn_child_process;
	std::function<pf::file_path()> locate = [] { return pf::find_executable("copilot"); };

	agent_host(const agent_host&) = delete;
	agent_host& operator=(const agent_host&) = delete;

	// Takes the transcript the document currently holds, so edits made between turns are kept
	void adopt(std::vector<std::string> lines);

	// The folder the agent runs in. Must be set before the first message.
	void set_working_dir(pf::file_path dir) { _working_dir = std::move(dir); }

	[[nodiscard]] bool connected() const;
	[[nodiscard]] bool busy() const;
	[[nodiscard]] bool yolo() const { return _yolo; }

	// Handles a line typed into the input, including slash commands
	void submit(std::string_view text);

	void stop_turn();
	void shutdown();

	// Answers the pending question, if there is one
	void answer(size_t index);
	[[nodiscard]] bool awaiting_answer() const { return _question.active; }


	// Exposed for tests: drives the protocol without a process
	void connect(std::unique_ptr<acp::transport> wire, const pf::file_path& working_dir);
	void on_agent_line(std::string_view line);
	void on_agent_exit(int code);

private:
	enum class question_kind { permission, model };

	struct pending_question
	{
		question_kind kind = question_kind::permission;
		acp::request_id id = 0; // only meaningful for a permission question
		std::vector<std::string> option_ids;
		bool active = false;
	};

	events& _events;
	std::vector<std::string> _lines;
	agent_stream_state _stream;
	std::vector<advertised_command> _commands;
	pending_question _question;
	pf::child_process_ptr _process;
	std::unique_ptr<acp::transport> _wire;
	std::unique_ptr<acp::client> _client;
	pf::file_path _working_dir;
	std::string _status;
	std::string _queued_prompt;
	bool _yolo = false;
	bool _models_wanted = false; // /m arrived before the session existed

	void ensure_started();
	void wire_client();
	void send_or_queue(std::string_view text);

	void note(agent_entry_kind kind, std::string_view title, std::string_view body);
	void mutate(const std::function<void()>& change);
	void set_status(std::string text);

	void ask_permission(acp::request_id id, const json::value& params);
	void show_models();
	void apply_model(std::string_view model_id);
	void tick_last_question(size_t index);
	[[nodiscard]] bool try_answer(std::string_view text);

	void handle_read_file(acp::request_id id, const json::value& params);
	void handle_write_file(acp::request_id id, const json::value& params);
	void handle_command(const agent_command_parse& parsed, std::string_view raw);
};
