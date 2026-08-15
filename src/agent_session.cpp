// agent_session.cpp — session.md parsing, options and edits

#include "pch.h"
#include "agent_session.h"

#include <optional>

int agent_entry::chosen_option() const
{
	for (size_t i = 0; i < options.size(); ++i)
	{
		if (options[i].chosen)
			return static_cast<int>(i);
	}

	return -1;
}

namespace agent_session
{
	namespace
	{
		constexpr std::string_view tool_prefix = "### Tool: ";
		constexpr std::string_view question_prefix = "### Question: ";
		constexpr std::string_view plan_heading = "### Plan";

		std::string_view trim_end(std::string_view s)
		{
			while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
				s.remove_suffix(1);

			return s;
		}

		std::string_view trim(std::string_view s)
		{
			while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
				s.remove_prefix(1);

			return trim_end(s);
		}

		bool is_blank(const std::string_view line)
		{
			return trim(line).empty();
		}

		std::optional<agent_entry_kind> role_kind(const std::string_view line)
		{
			const auto text = trim_end(line);

			for (const auto kind : {
				     agent_entry_kind::session, agent_entry_kind::user, agent_entry_kind::agent,
				     agent_entry_kind::thought, agent_entry_kind::error
			     })
			{
				if (text == role_heading(kind))
					return kind;
			}

			return std::nullopt;
		}

		// A trailing "(status)" records how a tool call was resolved
		void split_tool_title(const std::string_view text, std::string& title, std::string& status)
		{
			const auto trimmed = trim_end(text);

			if (trimmed.size() > 2 && trimmed.back() == ')')
			{
				if (const auto open = trimmed.rfind(" ("); open != std::string_view::npos)
				{
					title = trim(trimmed.substr(0, open));
					status = trimmed.substr(open + 2, trimmed.size() - open - 3);
					return;
				}
			}

			title = trimmed;
			status.clear();
		}

		bool parse_option_line(const std::string_view line, std::string& label, bool& chosen)
		{
			const auto text = trim(line);

			if (text.size() < 5 || !text.starts_with("- ["))
				return false;

			const auto mark = text[3];

			if (text[4] != ']')
				return false;

			if (mark == ' ')
				chosen = false;
			else if (mark == 'x' || mark == 'X')
				chosen = true;
			else
				return false;

			label = trim(text.substr(5));
			return true;
		}

		bool parse_bullet(const std::string_view line, std::string_view& key, std::string_view& value)
		{
			const auto text = trim(line);

			if (!text.starts_with("- "))
				return false;

			const auto colon = text.find(':');

			if (colon == std::string_view::npos)
				return false;

			key = trim(text.substr(2, colon - 2));
			value = trim(text.substr(colon + 1));
			return !key.empty();
		}

		bool looks_like_heading(const std::string_view line)
		{
			const auto text = trim_end(line);

			return role_kind(text).has_value() ||
				text.starts_with(tool_prefix) ||
				text.starts_with(question_prefix) ||
				text == plan_heading;
		}
	}

	std::string_view role_heading(const agent_entry_kind kind)
	{
		switch (kind)
		{
		case agent_entry_kind::session: return "## Session";
		case agent_entry_kind::user: return "## You";
		case agent_entry_kind::agent: return "## Agent";
		case agent_entry_kind::thought: return "## Thinking";
		case agent_entry_kind::error: return "## Error";
		case agent_entry_kind::tool_call: return "### Tool: ";
		case agent_entry_kind::question: return "### Question: ";
		case agent_entry_kind::plan: return plan_heading;
		case agent_entry_kind::note: break;
		}

		return {};
	}

	std::vector<agent_entry> parse(const std::span<const std::string> lines)
	{
		std::vector<agent_entry> entries;
		const auto count = static_cast<int>(lines.size());

		for (int i = 0; i < count; ++i)
		{
			const std::string_view line = lines[i];

			agent_entry entry;
			entry.first_line = i;
			entry.last_line = i + 1;
			auto is_heading = true;

			if (const auto kind = role_kind(line))
			{
				entry.kind = *kind;
			}
			else if (line.starts_with(tool_prefix))
			{
				entry.kind = agent_entry_kind::tool_call;
				split_tool_title(line.substr(tool_prefix.size()), entry.title, entry.status);
			}
			else if (line.starts_with(question_prefix))
			{
				entry.kind = agent_entry_kind::question;
				entry.title = trim_end(line.substr(question_prefix.size()));
			}
			else if (trim_end(line) == plan_heading)
			{
				entry.kind = agent_entry_kind::plan;
			}
			else
			{
				is_heading = false;
			}

			if (is_heading)
			{
				if (!entries.empty())
					entries.back().last_line = i;

				entries.push_back(std::move(entry));
				continue;
			}

			// Content before the first heading is kept as a note so nothing is lost
			if (entries.empty())
			{
				if (is_blank(line) || trim_end(line) == file_header)
					continue;

				agent_entry note;
				note.first_line = i;
				note.last_line = i + 1;
				entries.push_back(std::move(note));
			}

			auto& current = entries.back();
			current.last_line = i + 1;

			std::string label;
			auto chosen = false;

			if (parse_option_line(line, label, chosen))
				current.options.push_back({std::move(label), chosen, i});
		}

		if (!entries.empty())
			entries.back().last_line = count;

		return entries;
	}

	options read_options(const std::span<const std::string> lines, const std::vector<agent_entry>& entries)
	{
		options result;

		for (const auto& entry : entries)
		{
			if (entry.kind != agent_entry_kind::session)
				continue;

			for (auto i = entry.first_line + 1; i < entry.last_line && i < static_cast<int>(lines.size()); ++i)
			{
				std::string_view key;
				std::string_view value;

				if (!parse_bullet(lines[i], key, value))
					continue;

				if (key == "model")
					result.model = value;
				else if (key == "yolo")
					result.yolo = value == "on" || value == "true" || value == "1";
			}
		}

		return result;
	}

	std::string escape_body_line(const std::string_view line)
	{
		return looks_like_heading(line) ? "\\" + std::string(line) : std::string(line);
	}

	std::vector<std::string> split_body(const std::string_view body)
	{
		std::vector<std::string> result;

		if (body.empty())
			return result;

		size_t pos = 0;

		for (;;)
		{
			const auto newline = body.find('\n', pos);
			const auto piece = body.substr(pos, newline == std::string_view::npos
				                               ? std::string_view::npos
				                               : newline - pos);

			result.push_back(escape_body_line(trim_end(piece)));

			if (newline == std::string_view::npos)
				break;

			pos = newline + 1;
		}

		return result;
	}

	void ensure_header(std::vector<std::string>& lines)
	{
		const auto has_content = std::ranges::any_of(lines, [](const std::string& l) { return !is_blank(l); });

		if (has_content)
			return;

		lines.clear();
		lines.emplace_back(file_header);
		lines.emplace_back();
		lines.emplace_back(role_heading(agent_entry_kind::session));
		lines.emplace_back("- model: default");
		lines.emplace_back("- yolo: off");
	}

	void append_entry(std::vector<std::string>& lines, const agent_entry_kind kind,
	                  const std::string_view title, const std::string_view body)
	{
		while (!lines.empty() && is_blank(lines.back()))
			lines.pop_back();

		if (!lines.empty())
			lines.emplace_back();

		auto heading = std::string(role_heading(kind));

		if (kind == agent_entry_kind::tool_call || kind == agent_entry_kind::question)
			heading += title;

		lines.push_back(std::move(heading));

		// Always leave a blank line, so a streamed chunk cannot land on the heading itself
		lines.emplace_back();

		for (auto& line : split_body(body))
			lines.push_back(std::move(line));
	}

	void append_chunk(std::vector<std::string>& lines, const std::string_view text)
	{
		if (text.empty())
			return;

		auto pieces = split_body(text);

		if (pieces.empty())
			return;

		// The first piece continues the line the previous chunk left open
		if (!lines.empty() && !is_blank(lines.back()))
		{
			lines.back() += pieces.front();
			pieces.erase(pieces.begin());
		}

		for (auto& piece : pieces)
			lines.push_back(std::move(piece));
	}

	void set_option(std::vector<std::string>& lines, const std::string_view key, const std::string_view value)
	{
		ensure_header(lines);

		const auto entries = parse(lines);
		const auto session = std::ranges::find_if(entries, [](const agent_entry& e)
		{
			return e.kind == agent_entry_kind::session;
		});

		const auto bullet = std::format("- {}: {}", key, value);

		if (session == entries.end())
		{
			const auto at = lines.begin() + (lines.empty() ? 0 : 1);
			lines.insert(at, {std::string(), std::string(role_heading(agent_entry_kind::session)), bullet});
			return;
		}

		auto insert_at = session->first_line + 1;

		for (auto i = session->first_line + 1; i < session->last_line && i < static_cast<int>(lines.size()); ++i)
		{
			std::string_view existing_key;
			std::string_view existing_value;

			if (!parse_bullet(lines[i], existing_key, existing_value))
				continue;

			if (existing_key == key)
			{
				lines[i] = bullet;
				return;
			}

			insert_at = i + 1;
		}

		lines.insert(lines.begin() + insert_at, bullet);
	}

	void choose_option(std::vector<std::string>& lines, const agent_entry& entry, const size_t index)
	{
		if (index >= entry.options.size())
			return;

		for (size_t i = 0; i < entry.options.size(); ++i)
		{
			const auto& option = entry.options[i];

			if (option.line < 0 || option.line >= static_cast<int>(lines.size()))
				continue;

			const auto& line = lines[option.line];
			const auto bracket = line.find("- [");

			if (bracket == std::string::npos || bracket + 3 >= line.size())
				continue;

			lines[option.line][bracket + 3] = i == index ? 'x' : ' ';
		}
	}

	std::string to_text(const std::span<const std::string> lines)
	{
		std::string result;

		for (size_t i = 0; i < lines.size(); ++i)
		{
			if (i != 0)
				result += '\n';

			result += lines[i];
		}

		return result;
	}

	std::vector<std::string> to_lines(const std::string_view text)
	{
		std::vector<std::string> result;
		size_t pos = 0;

		for (;;)
		{
			const auto newline = text.find('\n', pos);

			if (newline == std::string_view::npos)
			{
				result.emplace_back(text.substr(pos));
				break;
			}

			auto piece = text.substr(pos, newline - pos);

			if (!piece.empty() && piece.back() == '\r')
				piece.remove_suffix(1);

			result.emplace_back(piece);
			pos = newline + 1;
		}

		return result;
	}
}
