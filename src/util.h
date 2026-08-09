// util.h — Core utilities: string ops, search, joining

#pragma once

[[nodiscard]] size_t find_in_text(std::string_view text, std::string_view pattern, bool match_case = false);

template <typename T, typename Fn>
std::string join(const std::vector<T>& items, Fn text_of, const std::string_view endl = "\n")
{
	if (items.empty())
	{
		return {};
	}

	if (items.size() == 1)
	{
		return std::string(text_of(items[0]));
	}

	size_t total = 0;
	for (const auto& item : items)
		total += text_of(item).size();
	total += (items.size() - 1) * endl.size();

	std::string result;
	result.reserve(total);
	auto first = true;

	for (const auto& item : items)
	{
		if (first)
		{
			result.append(text_of(item));
			first = false;
		}
		else
		{
			result.append(endl);
			result.append(text_of(item));
		}
	}

	return result;
}

[[nodiscard]] std::string combine(const std::vector<std::string>& lines, std::string_view endl = "\n");
[[nodiscard]] std::string combine(const std::vector<std::string_view>& lines, std::string_view endl = "\n");

[[nodiscard]] std::string replace(std::string_view s, std::string_view find, std::string_view replacement);

[[nodiscard]] constexpr std::string_view to_str(const bool val)
{
	return val ? "true" : "false";
}

[[nodiscard]] inline std::string to_str(const int val)
{
	return std::to_string(val);
}

[[nodiscard]] inline std::string to_str(const double val)
{
	return std::to_string(val);
}
