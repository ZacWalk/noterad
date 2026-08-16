// gitignore.h — .gitignore matching, used to keep build output out of the folder index

#pragma once

#include "platform.h"

// Rules collected from one or more .gitignore files. Each rule remembers the folder
// its file lived in, so nested .gitignore files match relative to themselves.
//
// Supports the subset that .gitignore files actually contain: comments, blank lines,
// '!' negation, a trailing '/' for directory-only, a leading or embedded '/' for
// anchored patterns, and the '*', '**' and '?' wildcards. Character classes are not
// matched, so a pattern using them simply fails to ignore anything — the file stays
// visible rather than silently disappearing.
class gitignore_rules
{
public:
	// base is the folder holding this .gitignore, relative to the index root, '/' separated
	void add_file(const std::string_view text, const std::string_view base)
	{
		size_t pos = 0;

		while (pos < text.size())
		{
			auto end = text.find('\n', pos);
			if (end == std::string_view::npos) end = text.size();

			auto line = text.substr(pos, end - pos);
			pos = end + 1;

			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);

			add_pattern(line, base);
		}
	}

	[[nodiscard]] bool empty() const { return _rules.empty(); }

	// path is relative to the index root, '/' separated, with no leading slash
	[[nodiscard]] bool is_ignored(const std::string_view path, const bool is_directory) const
	{
		auto ignored = false;

		// Git gives the last matching rule the final say, so negation can re-include
		for (const auto& r : _rules)
		{
			if (r.directory_only && !is_directory)
				continue;

			const auto relative = relative_to_base(path, r.base);
			if (!relative)
				continue;

			if (matches(r, *relative))
				ignored = !r.negate;
		}

		return ignored;
	}

private:
	struct rule
	{
		std::string base; // folder of the .gitignore that declared it
		std::string pattern;
		bool negate = false;
		bool directory_only = false;
		bool anchored = false; // matched against the whole relative path, not the name
	};

	std::vector<rule> _rules;

	void add_pattern(std::string_view line, const std::string_view base)
	{
		while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
			line.remove_prefix(1);

		// Trailing spaces are insignificant unless escaped, which nothing in practice does
		while (!line.empty() && line.back() == ' ')
			line.remove_suffix(1);

		if (line.empty() || line.front() == '#')
			return;

		rule r;
		r.base = base;

		if (line.front() == '!')
		{
			r.negate = true;
			line.remove_prefix(1);
		}

		if (!line.empty() && line.back() == '/')
		{
			r.directory_only = true;
			line.remove_suffix(1);
		}

		if (line.empty())
			return;

		if (line.front() == '/')
		{
			r.anchored = true;
			line.remove_prefix(1);
		}
		else if (line.find('/') != std::string_view::npos)
		{
			r.anchored = true;
		}

		if (line.empty())
			return;

		r.pattern.assign(line);
		_rules.push_back(std::move(r));
	}

	// Strips the declaring folder from path, or returns nothing when path is outside it
	[[nodiscard]] static std::optional<std::string_view> relative_to_base(const std::string_view path,
	                                                                     const std::string_view base)
	{
		if (base.empty())
			return path;
		if (path.size() <= base.size() || !path.starts_with(base) || path[base.size()] != '/')
			return {};
		return path.substr(base.size() + 1);
	}

	[[nodiscard]] static bool matches(const rule& r, const std::string_view relative)
	{
		if (r.anchored)
			return glob_match(r.pattern, relative);

		// An unanchored pattern matches the name at any depth
		const auto slash = relative.rfind('/');
		const auto name = slash == std::string_view::npos ? relative : relative.substr(slash + 1);
		return glob_match(r.pattern, name);
	}

	// '*' and '?' do not cross a '/', '**' does
	[[nodiscard]] static bool glob_match(const std::string_view pattern, const std::string_view text)
	{
		size_t p = 0;
		size_t t = 0;

		while (p < pattern.size())
		{
			if (pattern[p] == '*')
			{
				const bool any_depth = p + 1 < pattern.size() && pattern[p + 1] == '*';
				auto rest = p + (any_depth ? 2 : 1);

				// '**/' also stands for no directory at all
				if (any_depth && rest < pattern.size() && pattern[rest] == '/')
					rest++;

				const auto tail = pattern.substr(rest);

				for (auto k = t;; k++)
				{
					if (glob_match(tail, text.substr(k)))
						return true;
					if (k >= text.size())
						return false;
					if (!any_depth && text[k] == '/')
						return false;
				}
			}

			if (t >= text.size())
				return false;

			if (pattern[p] == '?')
			{
				if (text[t] == '/')
					return false;
			}
			else if (pf::to_lower(static_cast<uint8_t>(pattern[p])) !=
				pf::to_lower(static_cast<uint8_t>(text[t])))
			{
				return false;
			}

			p++;
			t++;
		}

		return t == text.size();
	}
};
