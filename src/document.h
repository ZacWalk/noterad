// document.h — Text document model: lines, selections, undo/redo, syntax highlighting, transforms

#pragma once

#include "platform.h"

class undo_group;
class doc_view;
class document_line;
class text_location;
class view_base;
class document_events;

constexpr auto invalid_length = -1;
constexpr uint32_t invalid_cookie = UINT32_MAX;

bool is_binary_extension(const pf::file_path& path);
bool is_binary_data(std::span<const uint8_t> data);

inline bool is_markdown_path(const pf::file_path& path)
{
	const auto ext = path.extension();
	if (ext.empty()) return false;
	const auto e = ext.starts_with('.') ? ext.substr(1) : ext;
	return pf::icmp(e, "md") == 0 || pf::icmp(e, "markdown") == 0;
}

inline bool is_csv_path(const pf::file_path& path)
{
	const auto ext = path.extension();
	if (ext.empty()) return false;
	const auto e = ext.starts_with('.') ? ext.substr(1) : ext;
	return pf::icmp(e, "csv") == 0;
}

bool should_spell_check_path(const pf::file_path& path);

enum class doc_type
{
	text,
	markdown,
	hex,
	csv,
};

enum class style
{
	main_wnd_clr,
	tool_wnd_clr,

	white_space,
	normal_bkgnd,
	normal_text,

	sel_margin,
	sel_bkgnd,
	sel_text,

	error_bkgnd,
	error_text,

	code_keyword,
	code_comment,
	code_number,
	code_operator,
	code_string,
	code_preprocessor,

	md_heading1,
	md_heading2,
	md_heading3,
	md_bold,
	md_italic,
	md_link_text,
	md_link_url,
	md_marker,
	md_bullet,
};

struct text_block
{
	int _char_pos;
	style _color;
};

using highlight_fn = std::function<uint32_t(uint32_t dwCookie, std::string_view line_view, text_block* pBuf,
                                            int& nActualItems)>;


enum class line_endings
{
	crlf_style_automatic = -1,
	crlf_style_dos = 0,
	crlf_style_unix = 1,
	crlf_style_mac = 2,
	binary,
};

enum class file_encoding
{
	utf32,
	utf32be,
	utf8,
	utf16be,
	utf16,
	ascii,
	binary,
};

struct file_buffer
{
	std::vector<uint8_t> data;
	file_encoding encoding = file_encoding::utf8;
	int bom_length = 0;
};

using file_buffer_ptr = std::shared_ptr<const file_buffer>;

constexpr uint32_t max_document_load_size = 2 * 1024 * 1024;

file_encoding detect_encoding(const uint8_t* header, size_t filesize, int& headerLen);
line_endings detect_line_endings(const uint8_t* buffer, int len);

struct file_lines_info
{
	file_encoding enc = file_encoding::utf8;
	line_endings endings = line_endings::crlf_style_dos;
};

file_lines_info iterate_file_lines(const pf::file_handle_ptr& handle,
                                   const std::function<void(const std::string&, int)>& on_line);

struct loaded_file_data
{
	std::vector<document_line> lines;
	file_buffer_ptr buffer;
	line_endings endings = line_endings::crlf_style_dos;
	file_encoding encoding = file_encoding::utf8;
	uint64_t disk_modified_time = 0;
	bool truncated = false;
};

loaded_file_data load_lines(const pf::file_path& path);

class text_location
{
public:
	int x = 0;
	int y = 0;

	constexpr text_location(const int xx = 0, const int yy = 0) : x(xx), y(yy)
	{
	}

	bool operator==(const text_location& other) const = default;

	auto operator<=>(const text_location& other) const
	{
		if (const auto cmp = y <=> other.y; cmp != 0) return cmp;
		return x <=> other.x;
	}
};

class text_selection
{
public:
	text_location _start;
	text_location _end;

	text_selection() = default;

	text_selection(const text_location& start, const text_location& end) : _start(start), _end(end)
	{
	}

	text_selection(const text_location& loc) : _start(loc), _end(loc)
	{
	}

	text_selection(const int x1, const int y1, const int x2, const int y2) : _start(x1, y1), _end(x2, y2)
	{
	}

	bool operator==(const text_selection& other) const = default;

	[[nodiscard]] bool empty() const
	{
		return _start == _end;
	}

	[[nodiscard]] bool is_valid() const
	{
		return _start.x >= 0 && _start.y >= 0 && _end.x >= 0 && _end.y >= 0;
	}

	[[nodiscard]] text_selection normalize() const
	{
		text_selection result;

		if (_start < _end)
		{
			result._start = _start;
			result._end = _end;
		}
		else
		{
			result._start = _end;
			result._end = _start;
		}

		return result;
	}
};

class document_line
{
public:
	document_line() = default;

	explicit document_line(std::string line) : _text(std::move(line))
	{
	}

	document_line(const std::string_view text) : _text(text)
	{
	}

	document_line(file_buffer_ptr buffer, const uint32_t offset, const uint32_t length)
		: _buffer(std::move(buffer)), _offset(offset), _length(length)
	{
	}

	[[nodiscard]] bool empty() const
	{
		return _buffer ? _length == 0 : _text.empty();
	}

	// Byte length of the rendered UTF-8 text; O(1) after the first call.
	[[nodiscard]] size_t size() const
	{
		if (_byte_length == invalid_length)
			_byte_length = static_cast<int>(compute_byte_length());
		return static_cast<size_t>(_byte_length);
	}

	void render(std::string& out) const;
	void update(std::string_view text);

	void invalidate_expanded_length() const { _expanded_length = invalid_length; }
	[[nodiscard]] int expanded_length_cache() const { return _expanded_length; }
	void set_expanded_length(const int len) const { _expanded_length = len; }

private:
	[[nodiscard]] size_t compute_byte_length() const
	{
		if (!_buffer)
			return _text.size();

		if (_buffer->encoding == file_encoding::utf16 || _buffer->encoding == file_encoding::utf16be)
		{
			std::string line_text;
			render(line_text);
			return line_text.size();
		}

		return _length;
	}

	std::string _text;
	file_buffer_ptr _buffer;
	uint32_t _offset = 0;
	uint32_t _length = 0;
	mutable int _expanded_length = invalid_length;
	mutable int _byte_length = invalid_length;
};


// --- Undo types ---

enum class undo_action { insert, erase };

struct undo_step
{
	text_selection _selection;
	undo_action _action = undo_action::insert;
	std::string _text;

	undo_step() = default;

	undo_step(const text_location& location, const char& c, const undo_action action) :
		_selection(location, location), _action(action), _text(1, c)
	{
	}

	undo_step(const text_selection& selection, std::string text, const undo_action action) :
		_selection(selection), _action(action), _text(std::move(text))
	{
	}

	undo_step(const text_selection& selection, const std::string_view text, const undo_action action) :
		_selection(selection), _action(action), _text(text)
	{
	}

	[[nodiscard]] bool is_insert() const { return _action == undo_action::insert; }
	[[nodiscard]] bool is_erase() const { return _action == undo_action::erase; }
	[[nodiscard]] bool is_single_char() const { return _text.size() == 1; }

	// For a single-char step, compute the position just past the inserted character.
	// Newline -> (0, y+1), other char -> (x+1, y).
	[[nodiscard]] text_location char_end_location() const
	{
		assert(!_text.empty());
		if (_text[0] == '\n')
			return text_location(0, _selection._start.y + 1);
		return text_location(_selection._start.x + 1, _selection._start.y);
	}
};

struct undo_item
{
	void add_insert(const text_location& location, const char& c)
	{
		_steps.emplace_back(location, c, undo_action::insert);
	}

	void add_insert(const text_selection& selection, std::string_view text)
	{
		_steps.emplace_back(selection, text, undo_action::insert);
	}

	void add_erase(const text_location& location, const char& c)
	{
		_steps.emplace_back(location, c, undo_action::erase);
	}

	void add_erase(const text_selection& selection, std::string_view text)
	{
		_steps.emplace_back(selection, text, undo_action::erase);
	}

	[[nodiscard]] bool empty() const { return _steps.empty(); }
	[[nodiscard]] auto begin() const { return _steps.begin(); }
	[[nodiscard]] auto end() const { return _steps.end(); }
	[[nodiscard]] auto rbegin() const { return _steps.rbegin(); }
	[[nodiscard]] auto rend() const { return _steps.rend(); }

private:
	std::vector<undo_step> _steps;
};

// --- Document ---

class document : public std::enable_shared_from_this<document>
{
	document_events& _events;

	text_location _anchor_loc;
	text_location _cursor_loc;
	text_selection _selection;
	bool _read_only = false;
	bool _spell_check = false;
	int _ideal_char_pos = 0;
	int _tab_size = 4;
	mutable int _max_line_len = -1;

	pf::file_path _path;
	mutable uint64_t _disk_modified_time = 0;

	mutable bool _modified = false;
	line_endings _line_ending = line_endings::crlf_style_automatic;
	file_encoding _encoding = file_encoding::utf8;

	std::vector<document_line> _lines;
	std::vector<undo_item> _undo;

	// Reused by the measurement helpers below; they run per line on every layout and paint
	mutable std::string _line_scratch;

	// Reused by the edit paths so a keystroke does not allocate
	std::string _edit_scratch;
	std::string _edit_scratch2;

	size_t _undo_pos = 0;
	mutable size_t _saved_undo_pos = 0;
	bool _is_truncated = false;
	file_buffer_ptr _buffer;

	text_location apply_undo_step(const undo_step& step);
	text_location apply_redo_step(const undo_step& step);
	text_location apply_undo(const undo_item& item);
	text_location apply_redo(const undo_item& item);

	text_location insert_text(const text_location& location, std::string_view text);
	text_location insert_text(const text_location& location, const char& c);
	text_location delete_text(const text_selection& selection);
	text_location delete_text(const text_location& location);

	struct block_range
	{
		int start_line;
		int end_line;
	};

	block_range prepare_block_selection(text_selection& sel);

public:
	document(document_events& events, std::string_view text = {},
	         bool is_modified = false,
	         line_endings nCrlfStyle = line_endings::crlf_style_dos);
	document(document_events& events, const pf::file_path& path, uint64_t disk_modified_time, file_encoding encoding);
	~document();

	void apply_loaded_data(const pf::file_path& path, loaded_file_data data);
	bool save_to_file(const pf::file_path& path, line_endings nCrlfStyle = line_endings::crlf_style_automatic,
	                  bool bClearModifiedFlag = true) const;
	void clear();

	[[nodiscard]] file_encoding encoding() const
	{
		return _encoding;
	}

	[[nodiscard]] bool is_modified() const
	{
		return _modified;
	}

	[[nodiscard]] bool is_read_only() const
	{
		return _read_only;
	}

	[[nodiscard]] bool is_truncated() const
	{
		return _is_truncated;
	}

	void read_only(const bool ro)
	{
		_read_only = ro;
	}

	[[nodiscard]] bool empty() const
	{
		return _lines.empty();
	}

	[[nodiscard]] size_t size() const
	{
		return _lines.size();
	}

	const document_line& operator[](const int n) const
	{
		return _lines[n];
	}

	document_line& operator[](const int n)
	{
		return _lines[n];
	}

	std::vector<std::string> text(const text_selection& selection) const;

	void path(const pf::file_path& path_in);

	[[nodiscard]] pf::file_path path() const
	{
		return _path;
	}

	void append_line(std::string_view text);

	text_selection replace_text(undo_group& ug, const text_selection& selection, std::string_view text);

	text_location insert_text(undo_group& ug, const text_location& location, std::string_view text);
	text_location insert_text(undo_group& ug, const text_location& location, const char& c);
	text_location delete_text(undo_group& ug, const text_selection& selection);
	text_location delete_text(undo_group& ug, const text_location& location);


	[[nodiscard]] bool can_undo() const;
	[[nodiscard]] bool can_redo() const;
	text_location undo();
	text_location redo();
	void record_undo(undo_item ui);

	[[nodiscard]] const std::vector<document_line>& lines() const
	{
		return _lines;
	}

	static bool can_paste();

	[[nodiscard]] bool has_selection() const
	{
		return !_selection.empty();
	}

	[[nodiscard]] bool query_editable() const;

	std::string edit_cut();
	std::string copy() const;
	void edit_delete();
	void edit_delete_back();
	void edit_redo();
	void edit_tab();
	void edit_undo();
	void edit_untab();
	void edit_paste(std::string_view text);

	[[nodiscard]] bool is_json() const;
	void reformat_json();
	void sort_remove_duplicates();

	[[nodiscard]] text_selection selection() const
	{
		return _selection.normalize();
	}

	void move_doc_end(bool selecting);
	void move_doc_home(bool selecting);
	void move_line_end(bool selecting);
	void move_line_home(bool selecting);
	void move_char_left(bool selecting);
	void move_char_right(bool selecting);
	void move_word_left(bool selecting);
	void move_word_right(bool selecting);
	void move_lines(int lines_to_move, bool selecting);

	bool is_inside_selection(const text_location& loc) const;
	void reset();
	void finalize_move(bool selecting);

	std::string str() const;

	[[nodiscard]] text_location end() const
	{
		const auto last_line = static_cast<int>(_lines.size()) - 1;
		return text_location(static_cast<int>(_lines[last_line].size()), last_line);
	}

	[[nodiscard]] text_selection all() const
	{
		return text_selection(text_location(0, 0), end());
	}

	[[nodiscard]] int tab_size() const
	{
		return _tab_size;
	}

	int max_line_length() const;
	text_location word_to_left(text_location pt) const;
	text_location word_to_right(text_location pt) const;

	int calc_offset(int lineIndex, int nCharIndex) const;
	int calc_offset_approx(int lineIndex, int nOffset) const;
	int expanded_line_length(int line_index) const;
	void expanded_chars(std::string_view text, int offset_in, int count_in, std::string& result) const;

	[[nodiscard]] const text_location& cursor_pos() const
	{
		return _cursor_loc;
	}

	[[nodiscard]] const text_location& anchor_pos() const
	{
		return _anchor_loc;
	}

	void anchor_pos(const text_location& ptNewAnchor);
	void cursor_pos(const text_location& ptCursorPos);

	void update_max_line_length(int lineIndex) const;

	void move_to(text_location pos, const bool selecting)
	{
		const auto limit = static_cast<int>(_lines[pos.y].size());

		if (pos.x > limit)
		{
			pos.x = limit;
		}

		_cursor_loc = pos;

		if (!selecting)
		{
			_anchor_loc = _cursor_loc;
		}

		select(text_selection(_anchor_loc, _cursor_loc));
	}

	text_selection word_selection(const text_location& pos, const bool from_anchor) const
	{
		const auto ptStart = from_anchor ? _anchor_loc : pos;
		const auto ptEnd = pos;

		if (ptStart < ptEnd || ptStart == ptEnd)
		{
			return text_selection(word_to_left(ptStart), word_to_right(ptEnd));
		}
		return text_selection(word_to_right(ptStart), word_to_left(ptEnd));
	}

	text_selection word_selection() const
	{
		if (_cursor_loc < _anchor_loc)
		{
			return text_selection(word_to_left(_cursor_loc), word_to_right(_anchor_loc));
		}
		return text_selection(word_to_left(_anchor_loc), word_to_right(_cursor_loc));
	}

	text_selection line_selection(const text_location& pos, const bool from_anchor) const
	{
		auto ptStart = from_anchor ? _anchor_loc : pos;
		auto ptEnd = pos;

		ptEnd.x = 0; //	Force beginning of the line

		if (ptStart.y < static_cast<int>(_lines.size()))
		{
			ptStart.x = static_cast<int>(_lines[ptStart.y].size());
		}
		else
		{
			ptStart.y = static_cast<int>(_lines.size()) - 1;
			ptStart.x = static_cast<int>(_lines[ptStart.y].size());
		}

		return text_selection(ptStart, ptEnd);
	}

	text_selection pos_selection(const text_location& pos, const bool from_anchor) const
	{
		return text_selection(from_anchor ? _anchor_loc : pos, pos);
	}

	void select(const text_selection& selection);

	void invalidate_line(int index);

	[[nodiscard]] bool spell_check() const
	{
		return _spell_check;
	}

	void set_spell_check(bool enabled);
	void toggle_spell_check();

	[[nodiscard]] doc_type get_doc_type() const
	{
		if (_encoding == file_encoding::binary)
			return doc_type::hex;
		if (is_csv_path(_path))
			return doc_type::csv;
		if (is_markdown_path(_path))
			return doc_type::markdown;
		return doc_type::text;
	}

	uint64_t disk_modified_time() const
	{
		return _disk_modified_time;
	}
};

using document_ptr = std::shared_ptr<document>;


class undo_group
{
	document& _doc;
	undo_item _undo;

public:
	undo_group(const undo_group&) = delete;
	undo_group(undo_group&&) = delete;
	undo_group& operator=(const undo_group&) = delete;
	undo_group& operator=(undo_group&&) = delete;

	undo_group(document& d) : _doc(d)
	{
	}

	undo_group(const document_ptr& d) : _doc(*d)
	{
	}

	~undo_group()
	{
		if (!_undo.empty())
			_doc.record_undo(std::move(_undo));
	}

	void insert(const text_location& location, const char& c)
	{
		_undo.add_insert(location, c);
	}

	void insert(const text_selection& selection, const std::string_view text)
	{
		_undo.add_insert(selection, text);
	}

	void erase(const text_location& location, const char& c)
	{
		_undo.add_erase(location, c);
	}

	void erase(const text_selection& selection, const std::string_view text)
	{
		_undo.add_erase(selection, text);
	}
};

// --- Highlighting and spell checking utilities (used by views) ---

// Select the appropriate syntax highlighter based on file path/extension.
highlight_fn select_highlighter(doc_type type, const pf::file_path& path);

// Spell checking helpers — thin wrappers around the platform spell checker.
// Non-ASCII bytes count as word bytes, so a scan never stops inside a codepoint.
inline bool is_spell_word_byte(const char ch)
{
	const auto b = static_cast<unsigned char>(ch);
	return b >= 0x80 || isalnum(b) != 0;
}

bool spell_check_word(std::string_view word);
std::vector<std::string> spell_suggest(std::string_view word);
void spell_add_word(std::string_view word);
void reset_spell_checker();
