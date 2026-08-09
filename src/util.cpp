// util.cpp — Core utilities: string search, joining and replacement

#include "pch.h"
#include "util.h"



// ── String utilities ────────────────────────────────────────────────────────────

size_t find_in_text(const std::string_view text, const std::string_view pattern, const bool match_case)
{
	if (text.empty()) return std::string_view::npos;
	if (pattern.empty()) return std::string_view::npos;

	const auto text_len = text.size();
	const auto pat_len = pattern.size();

	if (pat_len > text_len) return std::string_view::npos;

	const auto last = text_len - pat_len;
	const auto first_lower = static_cast<char>(pf::to_lower(static_cast<uint8_t>(pattern[0])));
	const auto first_upper = static_cast<char>(pf::to_upper(static_cast<uint8_t>(pattern[0])));

	for (size_t pos = 0; pos <= last;)
	{
		// Skip ahead on the first byte so the inner loop only runs on plausible starts
		const auto remaining = last - pos + 1;
		const auto* const hit = static_cast<const char*>(memchr(text.data() + pos, first_lower, remaining));
		const auto* const hit_alt = match_case || first_upper == first_lower
			                            ? nullptr
			                            : static_cast<const char*>(memchr(text.data() + pos, first_upper, remaining));

		const char* start = hit;
		if (!start || (hit_alt && hit_alt < start)) start = hit_alt;
		if (!start) return std::string_view::npos;

		pos = static_cast<size_t>(start - text.data());

		bool found = true;
		for (size_t j = 1; j < pat_len; ++j)
		{
			const auto tc = text[pos + j];
			const auto pc = pattern[j];
			if (tc != pc && (match_case || pf::to_lower(tc) != pf::to_lower(pc)))
			{
				found = false;
				break;
			}
		}
		if (found) return pos;
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
