// document_syntax.cpp — Syntax highlighting parsers for C++, plain text, hex, and markdown

#include "pch.h"
#include "document.h"

static bool is_alnum32(const char c) { return isalnum(static_cast<unsigned char>(c)) != 0; }
static bool is_space32(const char c) { return isspace(static_cast<unsigned char>(c)) != 0; }
static bool is_digit32(const char c) { return isdigit(static_cast<unsigned char>(c)) != 0; }

enum class syntax_lang { cpp, rust, python, ps1 };

static bool is_keyword(const syntax_lang lang, const std::string_view text)
{
	if (text.size() > 30) return false;
	if (lang == syntax_lang::rust)
	{
		static const std::unordered_set<std::string_view> keywords = {
			"as", "break", "const", "continue", "crate", "else", "enum", "extern", "false", "fn",
			"for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub",
			"ref", "return", "self", "Self", "static", "struct", "super", "trait", "true", "type",
			"unsafe", "use", "where", "while", "async", "await", "dyn", "abstract", "become", "box",
			"do", "final", "macro", "override", "priv", "typeof", "unsized", "virtual", "yield",
			"try"
		};
		return keywords.contains(text);
	}
	if (lang == syntax_lang::python)
	{
		static const std::unordered_set<std::string_view> keywords = {
			"False", "None", "True", "and", "as", "assert", "async", "await", "break", "class",
			"continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global",
			"if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
			"return", "try", "while", "with", "yield"
		};
		return keywords.contains(text);
	}
	if (lang == syntax_lang::ps1)
	{
		static const std::unordered_set<std::string_view, pf::ihash, pf::ieq> keywords = {
			"begin", "break", "catch", "class", "continue", "data", "define", "do", "dynamicparam",
			"else", "elseif", "end", "enum", "exit", "filter", "finally", "for", "foreach", "from",
			"function", "hidden", "if", "in", "inlinescript", "parallel", "param", "process",
			"return", "sequence", "switch", "throw", "trap", "try", "until", "using", "var",
			"while", "workflow"
		};
		return keywords.contains(text);
	}
	static const std::unordered_set<std::string_view> keywords =
	{
		"__asm",
		"__based",
		"__cdecl",
		"__declspec",
		"__except",
		"__fastcall",
		"__finally",
		"__inline",
		"__int16",
		"__int32",
		"__int64",
		"__int8",
		"__leave",
		"__multiple_inheritance",
		"__single_inheritance",
		"__stdcall",
		"__try",
		"__uuidof",
		"__virtual_inheritance",
		"alignas",
		"alignof",
		"and",
		"and_eq",
		"asm",
		"auto",
		"bitand",
		"bitor",
		"bool",
		"break",
		"case",
		"catch",
		"char",
		"char16_t",
		"char32_t",
		"class",
		"compl",
		"const",
		"const_cast",
		"constexpr",
		"continue",
		"decltype",
		"default",
		"delete",
		"dllexport",
		"dllimport",
		"do",
		"double",
		"dynamic_cast",
		"else",
		"enum",
		"explicit",
		"export",
		"extern",
		"false",
		"float",
		"for",
		"friend",
		"goto",
		"if",
		"inline",
		"int",
		"interface",
		"long",
		"mutable",
		"naked",
		"namespace",
		"new",
		"noexcept",
		"not",
		"not_eq",
		"nullptr",
		"operator",
		"or",
		"or_eq",
		"private",
		"protected",
		"public",
		"register",
		"reinterpret_cast",
		"return",
		"short",
		"signed",
		"sizeof",
		"static",
		"static_assert",
		"static_cast",
		"struct",
		"switch",
		"template",
		"this",
		"thread_local",
		"throw",
		"true",
		"try",
		"typedef",
		"typeid",
		"typename",
		"union",
		"uint32_t",
		"using",
		"uuid",
		"virtual",
		"void",
		"volatile",
		"wchar_t",
		"while",
		"xor",
		"xor_eq"
	};

	return keywords.contains(text);
}

static bool is_number(const std::string_view text)
{
	const auto len = static_cast<int>(text.size());
	if (len == 0) return false;

	if (len > 2 && text[0] == '0' && text[1] == 'x')
	{
		for (auto i = 2; i < len; i++)
		{
			if (is_digit32(text[i]) || (text[i] >= 'A' && text[i] <= 'F') ||
				(text[i] >= 'a' && text[i] <= 'f'))
				continue;
			return false;
		}
		return true;
	}
	if (!is_digit32(text[0]))
		return false;

	int pos = 1;
	while (pos < len && is_digit32(text[pos])) pos++;

	if (pos < len && text[pos] == '.')
	{
		pos++;
		if (pos >= len || !is_digit32(text[pos])) return false;
		while (pos < len && is_digit32(text[pos])) pos++;
	}

	if (pos < len && (text[pos] == 'e' || text[pos] == 'E'))
	{
		pos++;
		if (pos < len && (text[pos] == '+' || text[pos] == '-')) pos++;
		if (pos >= len || !is_digit32(text[pos])) return false;
		while (pos < len && is_digit32(text[pos])) pos++;
	}

	return pos == len;
}

static constexpr int COOKIE_COMMENT = 0x0001;
static constexpr int COOKIE_PREPROCESSOR = 0x0002;
static constexpr int COOKIE_EXT_COMMENT = 0x0004;
static constexpr int COOKIE_STRING = 0x0008;
static constexpr int COOKIE_CHAR = 0x0010;

static constexpr int max_syntax_blocks = 8192;

// Highlighter contract: every highlight_* function resets nActualItems to 0 and then fills pBuf.
static void add_block(text_block* pBuf, int& nActualItems, const int pos, const style colorindex)
{
	if (pBuf != nullptr)
	{
		if (nActualItems >= max_syntax_blocks)
			return;

		if (nActualItems == 0 || pBuf[nActualItems - 1]._char_pos <= pos)
		{
			pBuf[nActualItems]._char_pos = pos;
			pBuf[nActualItems]._color = colorindex;
			nActualItems++;
		}
	}
}

uint32_t highlight_cpp(uint32_t dwCookie, const std::string_view line_view, text_block* pBuf,
                       int& nActualItems)
{
	nActualItems = 0;

	if (line_view.empty())
	{
		return dwCookie & COOKIE_EXT_COMMENT;
	}

	const auto len = static_cast<int>(line_view.size());
	auto bFirstChar = (dwCookie & ~COOKIE_EXT_COMMENT) == 0;
	auto bRedefineBlock = true;
	auto bDecIndex = false;
	auto block_start = -1;
	auto i = 0;

	for (i = 0;; i++)
	{
		if (bRedefineBlock)
		{
			auto nPos = i;
			if (bDecIndex)
				nPos--;

			if (dwCookie & (COOKIE_COMMENT | COOKIE_EXT_COMMENT))
			{
				add_block(pBuf, nActualItems, nPos, style::code_comment);
			}
			else if (dwCookie & (COOKIE_CHAR | COOKIE_STRING))
			{
				add_block(pBuf, nActualItems, nPos, style::code_string);
			}
			else if (dwCookie & COOKIE_PREPROCESSOR)
			{
				add_block(pBuf, nActualItems, nPos, style::code_preprocessor);
			}
			else
			{
				add_block(pBuf, nActualItems, nPos, style::normal_text);
			}

			bRedefineBlock = false;
			bDecIndex = false;
		}

		if (i == len)
			break;

		if (dwCookie & COOKIE_COMMENT)
		{
			add_block(pBuf, nActualItems, i, style::code_comment);
			dwCookie |= COOKIE_COMMENT;
			break;
		}

		const auto c = line_view[i];

		//	String constant "...."
		if (dwCookie & COOKIE_STRING)
		{
			if (c == '"')
			{
				int bs = 0;
				for (int j = i - 1; j >= 0 && line_view[j] == '\\'; j--) bs++;
				if (bs % 2 == 0)
				{
					dwCookie &= ~COOKIE_STRING;
					bRedefineBlock = true;
				}
			}
			continue;
		}

		//	Char constant '..'
		if (dwCookie & COOKIE_CHAR)
		{
			if (c == '\'')
			{
				int bs = 0;
				for (int j = i - 1; j >= 0 && line_view[j] == '\\'; j--) bs++;
				if (bs % 2 == 0)
				{
					dwCookie &= ~COOKIE_CHAR;
					bRedefineBlock = true;
				}
			}
			continue;
		}

		//	Extended comment /*....*/
		if (dwCookie & COOKIE_EXT_COMMENT)
		{
			if (i > 0 && c == '/' && line_view[i - 1] == '*')
			{
				dwCookie &= ~COOKIE_EXT_COMMENT;
				bRedefineBlock = true;
			}
			continue;
		}

		if (i > 0 && c == '/' && line_view[i - 1] == '/')
		{
			add_block(pBuf, nActualItems, i - 1, style::code_comment);
			dwCookie |= COOKIE_COMMENT;
			break;
		}

		//	Preprocessor directive #....
		if (dwCookie & COOKIE_PREPROCESSOR)
		{
			if (i > 0 && c == '*' && line_view[i - 1] == '/')
			{
				add_block(pBuf, nActualItems, i - 1, style::code_comment);
				dwCookie |= COOKIE_EXT_COMMENT;
			}
			continue;
		}

		//	Normal text
		if (c == '"')
		{
			add_block(pBuf, nActualItems, i, style::code_string);
			dwCookie |= COOKIE_STRING;
			continue;
		}

		if (c == '\'')
		{
			add_block(pBuf, nActualItems, i, style::code_string);
			dwCookie |= COOKIE_CHAR;
			continue;
		}

		if (i > 0 && c == '*' && line_view[i - 1] == '/')
		{
			add_block(pBuf, nActualItems, i - 1, style::code_comment);
			dwCookie |= COOKIE_EXT_COMMENT;
			continue;
		}

		if (bFirstChar)
		{
			if (c == '#')
			{
				add_block(pBuf, nActualItems, i, style::code_preprocessor);
				dwCookie |= COOKIE_PREPROCESSOR;
				continue;
			}
			if (!is_space32(c))
				bFirstChar = false;
		}

		if (pBuf == nullptr)
			continue; //	We don't need to extract keywords,
		//	for faster parsing skip the rest of loop

		if (is_alnum32(c) || c == '_' || c == '.')
		{
			if (block_start == -1)
				block_start = i;
		}
		else
		{
			if (block_start >= 0)
			{
				const auto block_len = i - block_start;

				if (is_keyword(syntax_lang::cpp, line_view.substr(block_start, block_len)))
				{
					add_block(pBuf, nActualItems, block_start, style::code_keyword);
				}
				else if (is_number(line_view.substr(block_start, block_len)))
				{
					add_block(pBuf, nActualItems, block_start, style::code_number);
				}

				bRedefineBlock = true;
				bDecIndex = true;
				block_start = -1;
			}
		}
	}

	if (block_start >= 0)
	{
		const auto block_len = i - block_start;

		if (is_keyword(syntax_lang::cpp, line_view.substr(block_start, block_len)))
		{
			add_block(pBuf, nActualItems, block_start, style::code_keyword);
		}
		else if (is_number(line_view.substr(block_start, block_len)))
		{
			add_block(pBuf, nActualItems, block_start, style::code_number);
		}
	}

	if (line_view[len - 1] != '\\')
		dwCookie &= COOKIE_EXT_COMMENT;
	return dwCookie;
}

uint32_t highlight_text(uint32_t dwCookie, const std::string_view line_view, text_block* pBuf,
                        int& nActualItems)
{
	nActualItems = 0;

	if (pBuf)
	{
		const auto len = static_cast<int>(line_view.size());
		auto block_start = -1;

		for (auto i = 0; i <= len; i++)
		{
			if (i < len && is_alnum32(line_view[i]))
			{
				if (block_start == -1)
					block_start = i;
			}
			else
			{
				if (block_start >= 0)
				{
					const auto block_len = i - block_start;

					if (is_number(line_view.substr(block_start, block_len)))
					{
						add_block(pBuf, nActualItems, block_start, style::code_number);
					}
					else
					{
						add_block(pBuf, nActualItems, block_start, style::normal_text);
					}
				}

				block_start = -1;
			}
		}
	}

	return 0;
}

uint32_t highlight_hex(uint32_t dwCookie, const std::string_view line_view, text_block* pBuf,
                       int& nActualItems)
{
	nActualItems = 0;
	if (line_view.empty()) return 0;

	const auto len = static_cast<int>(line_view.size());

	// Offset address (first 8 hex chars + 2 spaces) -> number color
	add_block(pBuf, nActualItems, 0, style::code_number);

	// Hex bytes section -> keyword color
	if (len > 10)
		add_block(pBuf, nActualItems, 10, style::code_keyword);

	// Find the ASCII separator "|"
	const auto bar_pos = line_view.find(u8'|', 10);
	if (bar_pos != std::string_view::npos)
	{
		// ASCII section -> string color
		add_block(pBuf, nActualItems, static_cast<int>(bar_pos), style::code_string);
	}

	return 0;
}

static int find_md_marker(const std::string_view text, const int start, const char ch, const int count)
{
	const auto len = static_cast<int>(text.size());
	for (int i = start; i <= len - count; i++)
	{
		bool match = true;
		for (int j = 0; j < count; j++)
		{
			if (text[i + j] != ch)
			{
				match = false;
				break;
			}
		}
		if (match)
			return i;
	}
	return -1;
}

static int find_md_char(const std::string_view text, const int start, const char ch)
{
	const auto len = static_cast<int>(text.size());
	for (int i = start; i < len; i++)
	{
		if (text[i] == ch)
			return i;
	}
	return -1;
}

static void parse_md_inline(const std::string_view text, const int start,
                            text_block* pBuf, int& nActualItems)
{
	const auto len = static_cast<int>(text.size());
	int pos = start;

	while (pos < len)
	{
		// Bold: **text** or __text__
		if (pos + 1 < len && ((text[pos] == L'*' && text[pos + 1] == L'*') ||
			(text[pos] == L'_' && text[pos + 1] == L'_')))
		{
			const auto end = find_md_marker(text, pos + 2, text[pos], 2);
			if (end >= 0)
			{
				add_block(pBuf, nActualItems, pos, style::md_marker);
				add_block(pBuf, nActualItems, pos + 2, style::md_bold);
				add_block(pBuf, nActualItems, end, style::md_marker);
				if (end + 2 < len)
					add_block(pBuf, nActualItems, end + 2, style::normal_text);
				pos = end + 2;
				continue;
			}
		}

		// Italic: *text* or _text_
		if (text[pos] == L'*' || text[pos] == L'_')
		{
			const auto end = find_md_marker(text, pos + 1, text[pos], 1);
			if (end >= 0)
			{
				add_block(pBuf, nActualItems, pos, style::md_marker);
				add_block(pBuf, nActualItems, pos + 1, style::md_italic);
				add_block(pBuf, nActualItems, end, style::md_marker);
				if (end + 1 < len)
					add_block(pBuf, nActualItems, end + 1, style::normal_text);
				pos = end + 1;
				continue;
			}
		}

		// Link: [text](url)
		if (text[pos] == L'[')
		{
			const auto close_bracket = find_md_char(text, pos + 1, L']');
			if (close_bracket >= 0 && close_bracket + 1 < len && text[close_bracket + 1] == L'(')
			{
				const auto close_paren = find_md_char(text, close_bracket + 2, L')');
				if (close_paren >= 0)
				{
					add_block(pBuf, nActualItems, pos, style::md_marker);
					add_block(pBuf, nActualItems, pos + 1, style::md_link_text);
					add_block(pBuf, nActualItems, close_bracket, style::md_marker);
					add_block(pBuf, nActualItems, close_bracket + 2, style::md_link_url);
					add_block(pBuf, nActualItems, close_paren, style::md_marker);
					if (close_paren + 1 < len)
						add_block(pBuf, nActualItems, close_paren + 1, style::normal_text);
					pos = close_paren + 1;
					continue;
				}
			}
		}

		pos++;
	}
}

uint32_t highlight_markdown(uint32_t dwCookie, const std::string_view line_view, text_block* pBuf,
                            int& nActualItems)
{
	nActualItems = 0;
	if (line_view.empty()) return 0;

	const auto len = static_cast<int>(line_view.size());
	int pos = 0;

	// Heading: # ## ###
	if (line_view[0] == u8'#')
	{
		int level = 0;
		while (pos < len && line_view[pos] == u8'#' && level < 3)
		{
			level++;
			pos++;
		}
		if (pos < len && line_view[pos] == u8' ')
		{
			add_block(pBuf, nActualItems, 0, style::md_marker);
			const auto heading_style = static_cast<style>(
				static_cast<int>(style::md_heading1) + level - 1);
			add_block(pBuf, nActualItems, pos + 1, heading_style);
			return 0;
		}
		pos = 0;
	}

	// Unordered list: - or *
	if (len >= 2 && (line_view[0] == u8'-' || line_view[0] == u8'*') && line_view[1] == u8' ')
	{
		add_block(pBuf, nActualItems, 0, style::md_bullet);
		add_block(pBuf, nActualItems, 2, style::normal_text);
		parse_md_inline(line_view, 2, pBuf, nActualItems);
		return 0;
	}

	// Ordered list: digits followed by ". "
	if (len >= 3 && line_view[0] >= u8'0' && line_view[0] <= u8'9')
	{
		int p = 0;
		while (p < len && line_view[p] >= u8'0' && line_view[p] <= u8'9') p++;
		if (p < len - 1 && line_view[p] == u8'.' && line_view[p + 1] == u8' ')
		{
			add_block(pBuf, nActualItems, 0, style::md_bullet);
			add_block(pBuf, nActualItems, p + 2, style::normal_text);
			parse_md_inline(line_view, p + 2, pBuf, nActualItems);
			return 0;
		}
	}

	// Normal line with inline formatting
	parse_md_inline(line_view, 0, pBuf, nActualItems);
	return 0;
}

uint32_t highlight_rust(uint32_t dwCookie, const std::string_view line_view, text_block* pBuf,
                        int& nActualItems)
{
	nActualItems = 0;
	if (line_view.empty()) return dwCookie & COOKIE_EXT_COMMENT;
	const auto len = static_cast<int>(line_view.size());
	auto bRedefineBlock = true;
	auto bDecIndex = false;
	auto block_start = -1;
	auto i = 0;
	for (i = 0;; i++)
	{
		if (bRedefineBlock)
		{
			auto nPos = i;
			if (bDecIndex) nPos--;
			if (dwCookie & (COOKIE_COMMENT | COOKIE_EXT_COMMENT))
				add_block(
					pBuf, nActualItems, nPos, style::code_comment);
			else if (dwCookie & (COOKIE_CHAR | COOKIE_STRING)) add_block(pBuf, nActualItems, nPos, style::code_string);
			else if (dwCookie & COOKIE_PREPROCESSOR) add_block(pBuf, nActualItems, nPos, style::code_preprocessor);
			else add_block(pBuf, nActualItems, nPos, style::normal_text);
			bRedefineBlock = false;
			bDecIndex = false;
		}
		if (i == len) break;
		if (dwCookie & COOKIE_COMMENT)
		{
			add_block(pBuf, nActualItems, i, style::code_comment);
			dwCookie |= COOKIE_COMMENT;
			break;
		}
		const auto c = line_view[i];
		if (dwCookie & COOKIE_STRING)
		{
			if (c == '"')
			{
				int bs = 0;
				for (int j = i - 1; j >= 0 && line_view[j] == '\\'; j--) bs++;
				if (bs % 2 == 0)
				{
					dwCookie &= ~COOKIE_STRING;
					bRedefineBlock = true;
				}
			}
			continue;
		}
		if (dwCookie & COOKIE_CHAR)
		{
			if (c == '\'')
			{
				int bs = 0;
				for (int j = i - 1; j >= 0 && line_view[j] == '\\'; j--) bs++;
				if (bs % 2 == 0)
				{
					dwCookie &= ~COOKIE_CHAR;
					bRedefineBlock = true;
				}
			}
			continue;
		}
		if (dwCookie & COOKIE_EXT_COMMENT)
		{
			if (i > 0 && c == '/' && line_view[i - 1] == '*')
			{
				dwCookie &= ~COOKIE_EXT_COMMENT;
				bRedefineBlock = true;
			}
			continue;
		}
		if (i > 0 && c == '/' && line_view[i - 1] == '/')
		{
			add_block(pBuf, nActualItems, i - 1, style::code_comment);
			dwCookie |= COOKIE_COMMENT;
			break;
		}
		if (dwCookie & COOKIE_PREPROCESSOR)
		{
			if (i > 0 && c == '*' && line_view[i - 1] == '/')
			{
				add_block(pBuf, nActualItems, i - 1, style::code_comment);
				dwCookie |= COOKIE_EXT_COMMENT;
			}
			continue;
		}
		if (c == '"')
		{
			add_block(pBuf, nActualItems, i, style::code_string);
			dwCookie |= COOKIE_STRING;
			continue;
		}
		if (c == '\'')
		{
			add_block(pBuf, nActualItems, i, style::code_string);
			dwCookie |= COOKIE_CHAR;
			continue;
		}
		if (i > 0 && c == '*' && line_view[i - 1] == '/')
		{
			add_block(pBuf, nActualItems, i - 1, style::code_comment);
			dwCookie |= COOKIE_EXT_COMMENT;
			continue;
		}
		// Rust uses # for attributes, not preprocessor
		if (pBuf == nullptr) continue;
		if (is_alnum32(c) || c == '_' || c == '.') { if (block_start == -1) block_start = i; }
		else
		{
			if (block_start >= 0)
			{
				const auto block_len = i - block_start;
				if (is_keyword(syntax_lang::rust, line_view.substr(block_start, block_len)))
					add_block(
						pBuf, nActualItems, block_start, style::code_keyword);
				else if (is_number(line_view.substr(block_start, block_len)))
					add_block(
						pBuf, nActualItems, block_start, style::code_number);
				bRedefineBlock = true;
				bDecIndex = true;
				block_start = -1;
			}
		}
	}
	if (block_start >= 0)
	{
		const auto block_len = i - block_start;
		if (is_keyword(syntax_lang::rust, line_view.substr(block_start, block_len)))
			add_block(
				pBuf, nActualItems, block_start, style::code_keyword);
		else if (is_number(line_view.substr(block_start, block_len)))
			add_block(
				pBuf, nActualItems, block_start, style::code_number);
	}
	dwCookie &= COOKIE_EXT_COMMENT;
	return dwCookie;
}

uint32_t highlight_python(uint32_t dwCookie, const std::string_view line_view, text_block* pBuf,
                          int& nActualItems)
{
	nActualItems = 0;
	if (line_view.empty()) return dwCookie & COOKIE_EXT_COMMENT;
	const auto len = static_cast<int>(line_view.size());
	auto bRedefineBlock = true;
	auto bDecIndex = false;
	auto block_start = -1;
	auto i = 0;
	for (i = 0;; i++)
	{
		if (bRedefineBlock)
		{
			auto nPos = i;
			if (bDecIndex) nPos--;
			if (dwCookie & (COOKIE_COMMENT | COOKIE_EXT_COMMENT))
				add_block(
					pBuf, nActualItems, nPos, style::code_comment);
			else if (dwCookie & (COOKIE_CHAR | COOKIE_STRING)) add_block(pBuf, nActualItems, nPos, style::code_string);
			else add_block(pBuf, nActualItems, nPos, style::normal_text);
			bRedefineBlock = false;
			bDecIndex = false;
		}
		if (i == len) break;
		if (dwCookie & COOKIE_COMMENT)
		{
			add_block(pBuf, nActualItems, i, style::code_comment);
			dwCookie |= COOKIE_COMMENT;
			break;
		}
		const auto c = line_view[i];
		// Simplified quote escaping for Python
		if (dwCookie & COOKIE_STRING)
		{
			if (c == '"')
			{
				int bs = 0;
				for (int j = i - 1; j >= 0 && line_view[j] == '\\'; j--) bs++;
				if (bs % 2 == 0)
				{
					dwCookie &= ~COOKIE_STRING;
					bRedefineBlock = true;
				}
			}
			continue;
		}
		if (dwCookie & COOKIE_CHAR)
		{
			if (c == '\'')
			{
				int bs = 0;
				for (int j = i - 1; j >= 0 && line_view[j] == '\\'; j--) bs++;
				if (bs % 2 == 0)
				{
					dwCookie &= ~COOKIE_CHAR;
					bRedefineBlock = true;
				}
			}
			continue;
		}
		if (c == '#')
		{
			add_block(pBuf, nActualItems, i, style::code_comment);
			dwCookie |= COOKIE_COMMENT;
			break;
		}
		if (c == '"')
		{
			add_block(pBuf, nActualItems, i, style::code_string);
			dwCookie |= COOKIE_STRING;
			continue;
		}
		if (c == '\'')
		{
			add_block(pBuf, nActualItems, i, style::code_string);
			dwCookie |= COOKIE_CHAR;
			continue;
		}
		if (pBuf == nullptr) continue;
		if (is_alnum32(c) || c == '_' || c == '.') { if (block_start == -1) block_start = i; }
		else
		{
			if (block_start >= 0)
			{
				const auto block_len = i - block_start;
				if (is_keyword(syntax_lang::python, line_view.substr(block_start, block_len)))
					add_block(
						pBuf, nActualItems, block_start, style::code_keyword);
				else if (is_number(line_view.substr(block_start, block_len)))
					add_block(
						pBuf, nActualItems, block_start, style::code_number);
				bRedefineBlock = true;
				bDecIndex = true;
				block_start = -1;
			}
		}
	}
	if (block_start >= 0)
	{
		const auto block_len = i - block_start;
		if (is_keyword(syntax_lang::python, line_view.substr(block_start, block_len)))
			add_block(
				pBuf, nActualItems, block_start, style::code_keyword);
		else if (is_number(line_view.substr(block_start, block_len)))
			add_block(
				pBuf, nActualItems, block_start, style::code_number);
	}
	return dwCookie & COOKIE_EXT_COMMENT;
}

uint32_t highlight_ps1(uint32_t dwCookie, const std::string_view line_view, text_block* pBuf,
                       int& nActualItems)
{
	nActualItems = 0;
	if (line_view.empty()) return dwCookie & COOKIE_EXT_COMMENT;
	const auto len = static_cast<int>(line_view.size());
	auto bRedefineBlock = true;
	auto bDecIndex = false;
	auto block_start = -1;
	auto i = 0;
	for (i = 0;; i++)
	{
		if (bRedefineBlock)
		{
			auto nPos = i;
			if (bDecIndex) nPos--;
			if (dwCookie & (COOKIE_COMMENT | COOKIE_EXT_COMMENT))
				add_block(
					pBuf, nActualItems, nPos, style::code_comment);
			else if (dwCookie & (COOKIE_CHAR | COOKIE_STRING)) add_block(pBuf, nActualItems, nPos, style::code_string);
			else add_block(pBuf, nActualItems, nPos, style::normal_text);
			bRedefineBlock = false;
			bDecIndex = false;
		}
		if (i == len) break;
		if (dwCookie & COOKIE_COMMENT)
		{
			add_block(pBuf, nActualItems, i, style::code_comment);
			dwCookie |= COOKIE_COMMENT;
			break;
		}
		const auto c = line_view[i];
		// PS uses backtick  to escape but let's keep it simple here or standard slash
		if (dwCookie & COOKIE_STRING)
		{
			if (c == '"')
			{
				int bs = 0;
				for (int j = i - 1; j >= 0 && line_view[j] == '`'; j--) bs++;
				if (bs % 2 == 0)
				{
					dwCookie &= ~COOKIE_STRING;
					bRedefineBlock = true;
				}
			}
			continue;
		}
		if (dwCookie & COOKIE_CHAR)
		{
			if (c == '\'')
			{
				int bs = 0;
				for (int j = i - 1; j >= 0 && line_view[j] == '`'; j--) bs++;
				if (bs % 2 == 0)
				{
					dwCookie &= ~COOKIE_CHAR;
					bRedefineBlock = true;
				}
			}
			continue;
		}
		if (dwCookie & COOKIE_EXT_COMMENT)
		{
			if (i > 0 && c == '>' && line_view[i - 1] == '#')
			{
				dwCookie &= ~COOKIE_EXT_COMMENT;
				bRedefineBlock = true;
			}
			continue;
		}
		if (i > 0 && c == '#' && line_view[i - 1] == '<')
		{
			add_block(pBuf, nActualItems, i - 1, style::code_comment);
			dwCookie |= COOKIE_EXT_COMMENT;
			continue;
		}
		if (c == '#' && (i == 0 || line_view[i - 1] != '<'))
		{
			add_block(pBuf, nActualItems, i, style::code_comment);
			dwCookie |= COOKIE_COMMENT;
			break;
		}
		if (c == '"')
		{
			add_block(pBuf, nActualItems, i, style::code_string);
			dwCookie |= COOKIE_STRING;
			continue;
		}
		if (c == '\'')
		{
			add_block(pBuf, nActualItems, i, style::code_string);
			dwCookie |= COOKIE_CHAR;
			continue;
		}
		if (pBuf == nullptr) continue;
		if (is_alnum32(c) || c == '_' || c == '.' || c == '-') { if (block_start == -1) block_start = i; }
		else
		{
			if (block_start >= 0)
			{
				const auto block_len = i - block_start;
				if (is_keyword(syntax_lang::ps1, line_view.substr(block_start, block_len)))
					add_block(
						pBuf, nActualItems, block_start, style::code_keyword);
				else if (is_number(line_view.substr(block_start, block_len)))
					add_block(
						pBuf, nActualItems, block_start, style::code_number);
				bRedefineBlock = true;
				bDecIndex = true;
				block_start = -1;
			}
		}
	}
	if (block_start >= 0)
	{
		const auto block_len = i - block_start;
		if (is_keyword(syntax_lang::ps1, line_view.substr(block_start, block_len)))
			add_block(
				pBuf, nActualItems, block_start, style::code_keyword);
		else if (is_number(line_view.substr(block_start, block_len)))
			add_block(
				pBuf, nActualItems, block_start, style::code_number);
	}
	return dwCookie & COOKIE_EXT_COMMENT;
}

static bool is_ps1_extension(const std::string_view ext)
{
	static const std::set<std::string_view, pf::iless> extensions = {
		"ps1", "psm1", "psd1"
	};
	return extensions.contains(ext);
}

static bool is_python_extension(const std::string_view ext)
{
	static const std::set<std::string_view, pf::iless> extensions = {
		"py", "pyw"
	};
	return extensions.contains(ext);
}

static bool is_rust_extension(const std::string_view ext)
{
	static const std::set<std::string_view, pf::iless> extensions = {
		"rs"
	};
	return extensions.contains(ext);
}

static bool is_cpp_extension(const std::string_view ext)
{
	static const std::set<std::string_view, pf::iless> extensions = {
		"c", "cpp", "cxx", "cc", "h", "hh", "hpp", "hxx", "in"
	};

	return extensions.contains(ext);
}

highlight_fn select_highlighter(const doc_type type, const pf::file_path& path)
{
	switch (type)
	{
	case doc_type::hex:
		return highlight_hex;
	case doc_type::markdown:
		return highlight_markdown;
	case doc_type::csv:
		return highlight_text;
	default:
		break;
	}

	auto ext = path.extension();
	if (!ext.empty() && ext.starts_with('.')) ext = ext.substr(1);

	if (is_cpp_extension(ext))
		return highlight_cpp;
	if (is_rust_extension(ext))
		return highlight_rust;
	if (is_python_extension(ext))
		return highlight_python;
	if (is_ps1_extension(ext))
		return highlight_ps1;
	return highlight_text;
}
