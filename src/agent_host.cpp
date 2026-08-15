// agent_host.cpp — Starting the agent, running a turn and answering its questions

#include "pch.h"
#include "agent_host.h"

namespace
{
	// child_transport — Carries protocol lines to the spawned agent
	class child_transport final : public acp::transport
	{
	public:
		pf::child_process* process = nullptr;

		bool send_line(const std::string_view line) override
		{
			return process && process->write_line(line);
		}
	};

	// An option that lets the agent proceed, as opposed to one that refuses
	bool is_allow_option(const json::value& option)
	{
		return option["kind"].text().starts_with("allow");
	}
}

agent_host::~agent_host()
{
	shutdown();
}

void agent_host::adopt(std::vector<std::string> lines)
{
	_lines = std::move(lines);

	// The user may have edited the file, so nothing recorded about its shape still holds
	_stream.reset();
}

bool agent_host::connected() const
{
	return _client && _client->ready();
}

bool agent_host::busy() const
{
	return _client && _client->turn_in_flight();
}

void agent_host::set_status(std::string text)
{
	if (_status == text)
		return;

	_status = std::move(text);
	_events.agent_status_changed(_status);
}

void agent_host::mutate(const std::function<void()>& change)
{
	_stream.dirty_from = -1;
	change();

	auto first = _stream.dirty_from;

	if (first < 0)
		first = 0;

	first = std::clamp(first, 0, static_cast<int>(_lines.size()));
	_events.transcript_changed(first, std::span(_lines).subspan(first));
}

void agent_host::note(const agent_entry_kind kind, const std::string_view title, const std::string_view body)
{
	mutate([&]
	{
		_stream.dirty_from = agent_session::append_position(_lines);
		agent_session::append_entry(_lines, kind, title, body);
		_stream.entry_open = false;
	});

	_events.transcript_settled();
}

void agent_host::connect(std::unique_ptr<acp::transport> wire, const pf::file_path& working_dir)
{
	_wire = std::move(wire);
	_working_dir = working_dir;
	_client = std::make_unique<acp::client>(*_wire);
	wire_client();
	set_status("Starting the agent...");
	_client->start(working_dir.view(), {.read_text_file = true, .write_text_file = true});
}

void agent_host::wire_client()
{
	_client->on_ready = [this]
	{
		set_status("Ready");

		if (const auto model = agent_session::read_options(_lines, agent_session::parse(_lines)).model;
			!model.empty() && model != "default")
			(void)_client->set_model(model);

		if (!_queued_prompt.empty())
		{
			const auto text = std::exchange(_queued_prompt, {});
			send_or_queue(text);
		}
	};

	_client->on_error = [this](const std::string_view message)
	{
		note(agent_entry_kind::error, {}, message);
		set_status("Not connected");
	};

	_client->on_session_update = [this](const json::value& params)
	{
		if (const auto& update = params["update"]; update["sessionUpdate"].text() == "available_commands_update")
		{
			_commands = agent_session::read_advertised_commands(update);
			return;
		}

		mutate([&] { agent_session::apply_update(_lines, _stream, params["update"]); });
	};

	_client->on_turn_end = [this](const acp::stop_reason reason)
	{
		_stream.entry_open = false;

		if (reason != acp::stop_reason::end_turn)
			note(agent_entry_kind::note, {}, std::format("Turn ended: {}", acp::to_string(reason)));

		set_status("Ready");
		_events.transcript_settled();
	};

	_client->on_permission_request = [this](const acp::request_id id, const json::value& params)
	{
		ask_permission(id, params);
	};

	_client->on_elicitation = [this](const acp::request_id id, const json::value& params)
	{
		ask_permission(id, params);
	};

	_client->on_request = [this](const acp::request_id id, const std::string_view method,
	                             const json::value& params)
	{
		if (method == "fs/read_text_file")
			handle_read_file(id, params);
		else if (method == "fs/write_text_file")
			handle_write_file(id, params);
		else
			_client->respond_error(id, acp::error_code::method_not_found,
			                       std::format("unsupported method '{}'", method));
	};
}

void agent_host::handle_read_file(const acp::request_id id, const json::value& params)
{
	const pf::file_path path{params["path"].text()};
	std::string content;
	std::string error;

	if (!_events.read_file(path, content, error))
	{
		_client->respond_error(id, acp::error_code::invalid_request, error);
		return;
	}

	// A window into the file, counted in 1-based lines
	if (params.contains("line") || params.contains("limit"))
	{
		const auto all = agent_session::to_lines(content);
		const auto first = std::max<int64_t>(1, params["line"].integer(1)) - 1;
		const auto limit = params["limit"].integer(static_cast<int64_t>(all.size()));
		const auto start = std::min<size_t>(static_cast<size_t>(first), all.size());
		const auto count = limit <= 0 ? size_t{0} : std::min<size_t>(static_cast<size_t>(limit), all.size() - start);

		content = agent_session::to_text(std::span(all).subspan(start, count));
	}

	_client->respond(id, json::object().set("content", std::move(content)));
}

void agent_host::handle_write_file(const acp::request_id id, const json::value& params)
{
	const pf::file_path path{params["path"].text()};
	std::string error;

	if (!_events.write_file(path, params["content"].text(), error))
	{
		_client->respond_error(id, acp::error_code::invalid_request, error);
		return;
	}

	_client->respond(id, json::object());
}

void agent_host::ensure_started()
{
	if (_client)
		return;

	if (_working_dir.empty())
	{
		note(agent_entry_kind::error, {}, "Open a folder before starting the agent.");
		set_status("Not connected");
		return;
	}

	const auto exe = locate ? locate() : pf::file_path{};

	if (exe.empty())
	{
		note(agent_entry_kind::error, {},
		     "Could not find the `copilot` command. Install it with `winget install GitHub.Copilot`.");
		set_status("Not connected");
		return;
	}

	auto wire = std::make_unique<child_transport>();
	auto* raw_wire = wire.get();

	pf::child_process_callbacks callbacks;
	callbacks.on_stdout_line = [this](const std::string_view line) { on_agent_line(line); };
	callbacks.on_stderr_line = [this](const std::string_view line)
	{
		if (!line.empty())
			pf::debug_trace(std::format("agent: {}\n", line));
	};
	callbacks.on_exit = [this](const int code) { on_agent_exit(code); };

	const std::string args[] = {"--acp", "--stdio"};
	_process = spawn ? spawn(exe, args, _working_dir, std::move(callbacks)) : nullptr;

	if (!_process)
	{
		note(agent_entry_kind::error, {}, "The agent could not be started.");
		set_status("Not connected");
		return;
	}

	raw_wire->process = _process.get();
	connect(std::move(wire), _working_dir);
}

void agent_host::on_agent_line(const std::string_view line)
{
	if (_client)
		_client->on_line(line);
}

void agent_host::on_agent_exit(const int code)
{
	if (_client)
		_client->on_disconnect(std::format("the agent stopped (exit code {})", code));

	_client.reset();
	_wire.reset();
	_process.reset();
	_question.active = false;
	set_status("Not connected");
}

void agent_host::shutdown()
{
	_client.reset();
	_wire.reset();
	_process.reset();
}

void agent_host::stop_turn()
{
	if (_client && _client->turn_in_flight())
	{
		_client->cancel();
		set_status("Stopping...");
	}
}

void agent_host::send_or_queue(const std::string_view text)
{
	if (!_client)
	{
		_queued_prompt = text;
		ensure_started();
		return;
	}

	if (!_client->ready())
	{
		_queued_prompt = text;
		return;
	}

	if (!_client->send_prompt(text))
	{
		note(agent_entry_kind::error, {}, "The agent is still working — use /s to stop it.");
		return;
	}

	set_status("Working... /s to stop");
}

void agent_host::submit(const std::string_view text)
{
	if (try_answer(text))
		return;

	const auto parsed = agent_session::parse_command(text, _commands);

	if (parsed.command == agent_command::prompt)
	{
		note(agent_entry_kind::user, {}, text);
		send_or_queue(text);
		return;
	}

	handle_command(parsed, text);
}

void agent_host::handle_command(const agent_command_parse& parsed, const std::string_view raw)
{
	switch (parsed.command)
	{
	case agent_command::help:
		note(agent_entry_kind::note, {}, agent_session::help_text(_commands));
		return;

	case agent_command::clear:
		_question.active = false;
		_stream.reset();

		if (on_clear)
			on_clear();

		return;

	case agent_command::stop:
		if (busy())
			stop_turn();
		else
			note(agent_entry_kind::note, {}, "The agent is not working on anything.");
		return;

	case agent_command::yolo:
		{
			_yolo = !_yolo;
			mutate([&]
			{
				_stream.dirty_from = 0;
				agent_session::set_option(_lines, "yolo", _yolo ? "on" : "off");
			});
			note(agent_entry_kind::note, {},
			     _yolo
				     ? "YOLO on — tools will run without asking. Everything they do is still recorded here."
				     : "YOLO off — every tool will ask first.");
			return;
		}

	case agent_command::models:
		if (parsed.text.empty())
		{
			note(agent_entry_kind::user, {}, "/model");
			send_or_queue("/model");
		}
		else
		{
			mutate([&]
			{
				_stream.dirty_from = 0;
				agent_session::set_option(_lines, "model", parsed.text);
			});

			if (_client && _client->set_model(parsed.text))
				note(agent_entry_kind::note, {}, std::format("Model set to {}.", parsed.text));
			else
				note(agent_entry_kind::note, {},
				     std::format("Model recorded as {}; it applies when the agent connects.", parsed.text));
		}
		return;

	case agent_command::forward:
		note(agent_entry_kind::user, {}, raw);
		send_or_queue(parsed.text);
		return;

	case agent_command::unknown:
		note(agent_entry_kind::error, {}, std::format("Unknown command /{} — try /help", parsed.name));
		return;

	default:
		return;
	}
}

void agent_host::ask_permission(const acp::request_id id, const json::value& params)
{
	const auto& options = params["options"];
	const auto title = params["toolCall"]["title"].text(params["message"].text("The agent needs an answer"));

	if (_yolo)
	{
		for (const auto& option : options.items())
		{
			if (!is_allow_option(option))
				continue;

			auto outcome = json::object();
			outcome.set("outcome", "selected");
			outcome.set("optionId", option["optionId"].text());
			_client->respond(id, json::object().set("outcome", std::move(outcome)));

			note(agent_entry_kind::note, {}, std::format("Allowed automatically: {}", title));
			return;
		}
	}

	_question.id = id;
	_question.option_ids.clear();
	_question.active = true;

	std::string body;
	auto number = 1;

	for (const auto& option : options.items())
	{
		_question.option_ids.emplace_back(option["optionId"].text());
		body += std::format("- [ ] {}. {}\n", number, option["name"].text(option["optionId"].text()));
		++number;
	}

	if (_question.option_ids.empty())
	{
		// Nothing to choose from, so refuse rather than leaving the agent waiting
		_client->respond_error(id, acp::error_code::invalid_request, "no options were offered");
		_question.active = false;
		return;
	}

	body += "\nReply with the number of your choice.";
	note(agent_entry_kind::question, title, body);
	set_status("Waiting for your answer");
}

bool agent_host::try_answer(const std::string_view text)
{
	if (!_question.active)
		return false;

	const auto value = pf::stoi(text);

	if (value < 1 || static_cast<size_t>(value) > _question.option_ids.size())
		return false;

	answer_question(static_cast<size_t>(value) - 1);
	return true;
}

void agent_host::answer_question(const size_t index)
{
	if (!_question.active || index >= _question.option_ids.size() || !_client)
		return;

	auto outcome = json::object();
	outcome.set("outcome", "selected");
	outcome.set("optionId", _question.option_ids[index]);
	_client->respond(_question.id, json::object().set("outcome", std::move(outcome)));

	// Tick the choice in the file, so the record matches what was sent
	mutate([&]
	{
		const auto entries = agent_session::parse(_lines);

		for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
		{
			if (entry->kind != agent_entry_kind::question || entry->options.empty())
				continue;

			_stream.dirty_from = entry->first_line;
			agent_session::choose_option(_lines, *entry, index);
			break;
		}
	});

	_question.active = false;
	set_status(busy() ? "Working... /s to stop" : "Ready");
}
