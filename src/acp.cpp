// acp.cpp — Agent Client Protocol client: framing, correlation and dispatch

#include "pch.h"
#include "acp.h"

namespace acp
{
	std::string_view to_string(const stop_reason reason)
	{
		switch (reason)
		{
		case stop_reason::end_turn: return "end_turn";
		case stop_reason::cancelled: return "cancelled";
		case stop_reason::refusal: return "refusal";
		case stop_reason::max_tokens: return "max_tokens";
		case stop_reason::max_turns: return "max_turn_requests";
		case stop_reason::error: return "error";
		case stop_reason::unknown: break;
		}

		return "unknown";
	}

	stop_reason parse_stop_reason(const std::string_view text)
	{
		if (text == "end_turn") return stop_reason::end_turn;
		if (text == "cancelled") return stop_reason::cancelled;
		if (text == "refusal") return stop_reason::refusal;
		if (text == "max_tokens") return stop_reason::max_tokens;
		if (text == "max_turn_requests") return stop_reason::max_turns;
		if (text == "error") return stop_reason::error;
		return stop_reason::unknown;
	}

	void client::start(const std::string_view working_dir, const client_capabilities capabilities)
	{
		_working_dir = working_dir;
		_capabilities = capabilities;
		_state = connection_state::initializing;

		auto fs = json::object();
		fs.set("readTextFile", capabilities.read_text_file);
		fs.set("writeTextFile", capabilities.write_text_file);

		auto client_caps = json::object();
		client_caps.set("fs", std::move(fs));
		client_caps.set("terminal", false);

		auto params = json::object();
		params.set("protocolVersion", protocol_version);
		params.set("clientCapabilities", std::move(client_caps));

		send_request("initialize", std::move(params),
		             [this](const json::value& result, const json::value* error)
		             {
			             if (error)
			             {
				             fail(std::format("initialize failed: {}", (*error)["message"].text("unknown error")));
				             return;
			             }

			             _agent_capabilities = result["agentCapabilities"];
			             begin_session();
		             });
	}

	void client::begin_session()
	{
		_state = connection_state::creating_session;

		auto params = json::object();
		params.set("cwd", _working_dir);
		params.set("mcpServers", json::array());

		send_request("session/new", std::move(params),
		             [this](const json::value& result, const json::value* error)
		             {
			             if (error)
			             {
				             fail(std::format("session/new failed: {}", (*error)["message"].text("unknown error")));
				             return;
			             }

			             _session_id = result["sessionId"].text();

			             if (_session_id.empty())
			             {
				             fail("agent did not return a session id");
				             return;
			             }

			             _state = connection_state::ready;

			             if (on_ready)
				             on_ready();
		             });
	}

	bool client::send_prompt(const std::string_view text)
	{
		if (!ready() || turn_in_flight())
			return false;

		auto block = json::object();
		block.set("type", "text");
		block.set("text", text);

		auto prompt = json::array();
		prompt.add(std::move(block));

		auto params = json::object();
		params.set("sessionId", _session_id);
		params.set("prompt", std::move(prompt));

		_prompt_id = send_request("session/prompt", std::move(params),
		                          [this](const json::value& result, const json::value* error)
		                          {
			                          _prompt_id = 0;

			                          if (error)
			                          {
				                          if (on_error)
					                          on_error((*error)["message"].text("prompt failed"));

				                          if (on_turn_end)
					                          on_turn_end(stop_reason::error);

				                          return;
			                          }

			                          if (on_turn_end)
				                          on_turn_end(parse_stop_reason(result["stopReason"].text()));
		                          });

		return _prompt_id != 0;
	}

	void client::cancel()
	{
		if (!turn_in_flight())
			return;

		auto params = json::object();
		params.set("sessionId", _session_id);
		send_notification("session/cancel", std::move(params));
	}

	bool client::set_model(const std::string_view model_id)
	{
		if (!ready() || model_id.empty())
			return false;

		auto params = json::object();
		params.set("sessionId", _session_id);
		params.set("modelId", model_id);

		send_request("session/set_model", std::move(params),
		             [this](const json::value&, const json::value* error)
		             {
			             if (error && on_error)
				             on_error((*error)["message"].text("could not select that model"));
		             });

		return true;
	}

	void client::respond(const request_id id, json::value result)
	{
		const auto it = _awaiting_reply.find(id);

		if (it == _awaiting_reply.end())
			return;

		auto message = json::object();
		message.set("jsonrpc", "2.0");
		message.set("id", it->second);
		message.set("result", std::move(result));

		_awaiting_reply.erase(it);
		send_message(message);
	}

	void client::respond_error(const request_id id, const int code, const std::string_view message)
	{
		const auto it = _awaiting_reply.find(id);

		if (it == _awaiting_reply.end())
			return;

		const auto original_id = it->second;
		_awaiting_reply.erase(it);
		send_error_reply(original_id, code, message);
	}

	void client::send_error_reply(const json::value& id, const int code, const std::string_view message)
	{
		auto error = json::object();
		error.set("code", code);
		error.set("message", message);

		auto reply = json::object();
		reply.set("jsonrpc", "2.0");
		reply.set("id", id);
		reply.set("error", std::move(error));

		send_message(reply);
	}

	request_id client::send_request(const std::string_view method, json::value params, reply_handler handler)
	{
		if (_pending.size() >= max_pending_requests)
		{
			fail("too many requests in flight");
			return 0;
		}

		const auto id = _next_id++;

		auto message = json::object();
		message.set("jsonrpc", "2.0");
		message.set("id", id);
		message.set("method", method);
		message.set("params", std::move(params));

		_pending[id] = std::move(handler);
		send_message(message);
		return id;
	}

	void client::send_notification(const std::string_view method, json::value params)
	{
		auto message = json::object();
		message.set("jsonrpc", "2.0");
		message.set("method", method);
		message.set("params", std::move(params));

		send_message(message);
	}

	void client::send_message(const json::value& message)
	{
		if (!_wire.send_line(message.to_string()) && on_error)
			on_error("could not write to the agent");
	}

	void client::on_line(const std::string_view line)
	{
		if (line.find_first_not_of(" \t\r\n") == std::string_view::npos)
			return;

		const auto parsed = json::parse(line);

		if (!parsed.ok)
		{
			if (on_error)
				on_error(std::format("could not parse agent output: {}", parsed.error));
			return;
		}

		const auto& message = parsed.root;

		if (!message.is_object())
		{
			if (on_error)
				on_error("agent sent a message that is not an object");
			return;
		}

		if (message.contains("method"))
		{
			if (message.contains("id"))
				handle_request(message);
			else
				handle_notification(message["method"].text(), message["params"]);

			return;
		}

		if (message.contains("id"))
		{
			handle_response(message);
			return;
		}

		if (on_error)
			on_error("agent sent a message with neither a method nor an id");
	}

	void client::handle_response(const json::value& message)
	{
		const auto it = _pending.find(message["id"].integer(0));

		// A response to an id we never sent, or already handled, is ignored
		if (it == _pending.end())
			return;

		auto handler = std::move(it->second);
		_pending.erase(it);

		if (!handler)
			return;

		if (message.contains("error"))
			handler(json::value::null_value(), &message["error"]);
		else
			handler(message["result"], nullptr);
	}

	void client::handle_request(const json::value& message)
	{
		const auto method = message["method"].text();
		const auto& params = message["params"];

		if (_awaiting_reply.size() >= max_pending_replies)
		{
			send_error_reply(message["id"], error_code::internal_error, "too many requests outstanding");
			return;
		}

		const auto handle = _next_reply_handle++;
		_awaiting_reply[handle] = message["id"];

		if (method == "session/request_permission" && on_permission_request)
		{
			on_permission_request(handle, params);
			return;
		}

		if (method == "elicitation/create" && on_elicitation)
		{
			on_elicitation(handle, params);
			return;
		}

		if (on_request)
		{
			on_request(handle, method, params);
			return;
		}

		// Never leave the agent waiting on a method we do not implement
		respond_error(handle, error_code::method_not_found, std::format("unsupported method '{}'", method));
	}

	void client::handle_notification(const std::string_view method, const json::value& params)
	{
		if (method == "session/update" && on_session_update)
			on_session_update(params);
	}

	void client::on_disconnect(const std::string_view reason)
	{
		auto pending = std::move(_pending);
		_pending.clear();
		_awaiting_reply.clear();
		_prompt_id = 0;
		_state = connection_state::failed;

		auto error = json::object();
		error.set("code", error_code::internal_error);
		error.set("message", reason);

		for (auto& [id, handler] : pending)
		{
			if (handler)
				handler(json::value::null_value(), &error);
		}

		// The handlers report their own failure, so only a silent disconnect needs announcing
		if (on_error && pending.empty())
			on_error(reason);
	}

	void client::fail(const std::string_view message)
	{
		_state = connection_state::failed;

		if (on_error)
			on_error(message);
	}
}
