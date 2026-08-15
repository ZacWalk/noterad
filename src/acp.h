// acp.h — Agent Client Protocol: JSON-RPC 2.0 over a line-oriented transport

#pragma once

#include "json.h"

namespace acp
{
	using request_id = int64_t;

	constexpr int protocol_version = 1;

	// Outstanding work is bounded so a misbehaving agent cannot grow these without limit
	constexpr size_t max_pending_requests = 256;
	constexpr size_t max_pending_replies = 256;

	namespace error_code
	{
		constexpr int parse_error = -32700;
		constexpr int invalid_request = -32600;
		constexpr int method_not_found = -32601;
		constexpr int internal_error = -32603;
	}

	enum class stop_reason { end_turn, cancelled, refusal, max_tokens, max_turns, error, unknown };

	[[nodiscard]] std::string_view to_string(stop_reason reason);
	[[nodiscard]] stop_reason parse_stop_reason(std::string_view text);

	enum class connection_state { idle, initializing, creating_session, ready, failed };

	struct client_capabilities
	{
		bool read_text_file = false;
		bool write_text_file = false;
	};

	// The wire. Injectable so the protocol can be driven without a child process.
	struct transport
	{
		virtual ~transport() = default;

		// The line never contains a newline; the implementation appends the delimiter
		virtual bool send_line(std::string_view line) = 0;
	};

	// Speaks the client half of ACP. Knows nothing about windows, documents or processes.
	class client
	{
	public:
		explicit client(transport& wire) : _wire(wire)
		{
		}

		// Begins the initialize -> session/new handshake
		void start(std::string_view working_dir, client_capabilities capabilities = {});

		// One received line of NDJSON
		void on_line(std::string_view line);

		// The agent has gone away; every pending request fails
		void on_disconnect(std::string_view reason);

		[[nodiscard]] bool send_prompt(std::string_view text);
		void cancel();
		[[nodiscard]] bool set_model(std::string_view model_id);

		// Answers a request the agent made of us
		void respond(request_id id, json::value result);
		void respond_error(request_id id, int code, std::string_view message);

		[[nodiscard]] connection_state state() const { return _state; }
		[[nodiscard]] bool ready() const { return _state == connection_state::ready; }
		[[nodiscard]] bool turn_in_flight() const { return _prompt_id != 0; }
		[[nodiscard]] std::string_view session_id() const { return _session_id; }
		[[nodiscard]] const json::value& agent_capabilities() const { return _agent_capabilities; }
		[[nodiscard]] size_t pending_request_count() const { return _pending.size(); }
		[[nodiscard]] size_t pending_reply_count() const { return _awaiting_reply.size(); }

		std::function<void()> on_ready;
		std::function<void(std::string_view)> on_error;
		std::function<void(const json::value& update)> on_session_update;
		std::function<void(stop_reason)> on_turn_end;

		// Requests from the agent. Each must be answered with respond or respond_error.
		std::function<void(request_id, const json::value& params)> on_permission_request;
		std::function<void(request_id, const json::value& params)> on_elicitation;
		std::function<void(request_id, std::string_view method, const json::value& params)> on_request;

	private:
		using reply_handler = std::function<void(const json::value& result, const json::value* error)>;

		transport& _wire;
		connection_state _state = connection_state::idle;
		client_capabilities _capabilities;
		std::string _working_dir;
		std::string _session_id;
		json::value _agent_capabilities;
		request_id _next_id = 1;
		request_id _next_reply_handle = 1;
		request_id _prompt_id = 0;
		std::map<request_id, reply_handler> _pending;

		// The agent's own id is kept verbatim, since JSON-RPC allows a string there
		std::map<request_id, json::value> _awaiting_reply;

		request_id send_request(std::string_view method, json::value params, reply_handler handler);
		void send_notification(std::string_view method, json::value params);
		void send_message(const json::value& message);
		void send_error_reply(const json::value& id, int code, std::string_view message);

		void handle_response(const json::value& message);
		void handle_request(const json::value& message);
		void handle_notification(std::string_view method, const json::value& params);

		void begin_session();
		void fail(std::string_view message);
	};
}
