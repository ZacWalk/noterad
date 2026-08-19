// util.cpp — Core utilities: string search, joining and replacement

#include "pch.h"
#include "util.h"
#include "platform.h"



// ── String utilities ────────────────────────────────────────────────────────────

namespace
{
	// Folds case per codepoint, so a multi-byte character is never compared byte by byte
	bool matches_at(const std::string_view text, const size_t pos,
	                const std::string_view pattern, const bool match_case)
	{
		auto t = text.begin() + pos;
		auto p = pattern.begin();

		while (p != pattern.end())
		{
			if (t == text.end())
				return false;

			const auto pc = pf::pop_utf8_char(p, pattern.end());
			const auto tc = pf::pop_utf8_char(t, text.end());

			if (pc != tc && (match_case || pf::to_lower(pc) != pf::to_lower(tc)))
				return false;
		}

		return true;
	}
}

size_t find_in_text(const std::string_view text, const std::string_view pattern, const bool match_case)
{
	if (text.empty()) return std::string_view::npos;
	if (pattern.empty()) return std::string_view::npos;
	if (pattern.size() > text.size()) return std::string_view::npos;

	const auto first = static_cast<uint8_t>(pattern[0]);

	// An ASCII lead byte can never appear inside a multi-byte sequence, so memchr can
	// skip straight to plausible starts. A multi-byte lead has no single byte to scan
	// for, so every codepoint boundary is tried instead.
	if (first >= 0x80)
	{
		for (size_t pos = 0; pos + pattern.size() <= text.size(); ++pos)
		{
			if (pf::is_utf8_continuation(text[pos]))
				continue;
			if (matches_at(text, pos, pattern, match_case))
				return pos;
		}

		return std::string_view::npos;
	}

	const auto last = text.size() - pattern.size();
	const auto first_lower = static_cast<char>(pf::to_lower(first));
	const auto first_upper = static_cast<char>(pf::to_upper(first));

	for (size_t pos = 0; pos <= last;)
	{
		const auto remaining = last - pos + 1;
		const auto* const hit = static_cast<const char*>(memchr(text.data() + pos, first_lower, remaining));
		const auto* const hit_alt = match_case || first_upper == first_lower
			                            ? nullptr
			                            : static_cast<const char*>(memchr(text.data() + pos, first_upper, remaining));

		const char* start = hit;
		if (!start || (hit_alt && hit_alt < start)) start = hit_alt;
		if (!start) return std::string_view::npos;

		pos = static_cast<size_t>(start - text.data());

		if (matches_at(text, pos, pattern, match_case))
			return pos;

		++pos;
	}

	return std::string_view::npos;
}

std::string combine(const std::vector<std::string>& lines, const std::string_view endl)
{
	return join(lines, [](const std::string& s) -> std::string_view { return s; }, endl);
}

std::string combine(const std::vector<std::string_view>& lines, const std::string_view endl)
{
	return join(lines, [](const std::string_view& s) -> std::string_view { return s; }, endl);
}

std::string replace(const std::string_view s, const std::string_view find,
                    const std::string_view replacement)
{
	if (find.empty())
		return std::string(s);

	std::string result(s);
	size_t pos = 0;
	const auto findLength = find.size();
	const auto replacementLength = replacement.size();

	while ((pos = result.find(find, pos)) != std::string::npos)
	{
		result.replace(pos, findLength, replacement);
		pos += replacementLength;
	}

	return result;
}
