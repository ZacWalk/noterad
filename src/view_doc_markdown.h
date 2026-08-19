// view_doc_markdown.h — Markdown document view: read-only rendered markdown display

#pragma once

#include "view_doc_readonly.h"

class markdown_doc_view final : public read_only_doc_view
{
	pf::isize _heading_font_extent[3] = {}; // cached char sizes for h1, h2, h3
	std::vector<int> _line_pixel_y;
	int _total_content_height = 0;

public:
	markdown_doc_view(app_events& events) : read_only_doc_view(events)
	{
		_sel_margin = false;
		_word_wrap = true;
	}

	~markdown_doc_view() override = default;

	// Cached tables describe the previous document until the next layout
	void set_document(const document_ptr& d) override
	{
		_tables.clear();
		_line_pixel_y.clear();
		read_only_doc_view::set_document(d);
	}

	void handle_size(pf::window_frame_ptr& window, const pf::isize extent,
	                 pf::measure_context& measure) override
	{
		// Measure heading fonts before text_view::handle_size, which calls layout()
		for (int level = 1; level <= 3; ++level)
			_heading_font_extent[level - 1] = measure.measure_char(font_for_heading(level));

		doc_view::handle_size(window, extent, measure);
	}

	void recalc_vert_scrollbar() override
	{
		_content_extent.cy = top_content_padding() + _total_content_height + bottom_content_padding();

		const int max_y = std::max(0, _content_extent.cy - (_view_extent.cy - text_top()));
		if (_scroll_offset.y > max_y)
		{
			_scroll_offset.y = max_y;
			_events.invalidate(invalid::windows);
		}

		const int visible_height = std::max(0, _view_extent.cy - text_top());
		_vscroll.update(_content_extent.cy, visible_height, _scroll_offset.y);
	}

	void layout() override
	{
		if (!_doc)
		{
			_wrap_breaks.clear();
			_wrap_offsets.clear();
			_wrap_line_y.clear();
			_line_pixel_y.clear();
			_total_visual_rows = 0;
			_total_content_height = 0;
			return;
		}

		const auto line_count = static_cast<int>(_doc->size());
		_wrap_breaks.clear();
		_wrap_offsets.clear();
		_tables.clear();
		_wrap_line_y.resize(line_count + 1);
		_line_pixel_y.resize(line_count + 1);
		_wrap_line_y[0] = 0;
		_line_pixel_y[0] = 0;

		const auto base_cy = _font_extent.cy > 0 ? _font_extent.cy : 1;
		const auto left_pad = _font_extent.cx * 2;
		const auto avail_width = _view_extent.cx > left_pad ? _view_extent.cx - left_pad : 1;
		const auto line_pad = base_cy / 4;
		const auto table_cols = safe_cols(avail_width, _font_extent.cx);

		int cumulative = 0;
		int cumulative_px = 0;
		int i = 0;

		while (i < line_count)
		{
			(*_doc)[i].render(_line_buf);

			// Table block handling
			if (is_table_row(_line_buf))
			{
				const auto table = find_table(i, table_cols);
				if (table.start_line >= 0 && table.start_line <= i)
				{
					_tables.push_back(table);

					for (int tl = i; tl < table.end_line; tl++)
					{
						_wrap_line_y[tl] = cumulative;
						_line_pixel_y[tl] = cumulative_px;

						if (tl == table.separator_line)
						{
							cumulative += 1;
							cumulative_px += base_cy;
						}
						else
						{
							(*_doc)[tl].render(_line_buf);
							split_cells(_cells, _line_buf);
							int max_rows = 1;
							for (size_t c = 0; c < table.col_widths.size() && c < _cells.size(); c++)
							{
								const auto vr = table_layout::cell_visual_rows(
									trim_cell(_cells[c]), table.col_widths[c], _cell_breaks, cell_breaks_fn);
								if (vr > max_rows) max_rows = vr;
							}
							cumulative += max_rows;
							cumulative_px += max_rows * base_cy;
						}
					}
					i = table.end_line;
					continue;
				}
			}

			// Normal markdown line
			_wrap_line_y[i] = cumulative;
			_line_pixel_y[i] = cumulative_px;

			parse_line(_line_buf, _line_info);
			const auto& info = _line_info;
			auto font_cx = _font_extent.cx > 0 ? _font_extent.cx : 1;
			auto font_cy = base_cy;

			if (info.heading_level > 0 && info.heading_level <= 3)
			{
				const auto& sz = _heading_font_extent[info.heading_level - 1];
				if (sz.cx > 0) font_cx = sz.cx;
				if (sz.cy > 0) font_cy = sz.cy;
			}

			int pixel_height = 0;

			// Extra space above headings (except first line)
			if (info.heading_level > 0 && i > 0)
				pixel_height += font_cy / 2;

			// Word-wrapped content
			const auto chars_per_row = safe_cols(avail_width, font_cx);
			const auto indent_cols = info.is_list ? info.content_start : 0;
			const auto wrap_cols = std::max(1, chars_per_row - indent_cols);
			wrap_line_into(_line_buf, wrap_cols);
			const auto num_rows = static_cast<int>(_breaks.size()) + 1;
			pixel_height += num_rows * font_cy;

			// Extra space below
			if (info.heading_level > 0)
				pixel_height += font_cy / 3;
			else
				pixel_height += line_pad;

			// Convert pixel height to equivalent base font rows (ceiling)
			cumulative += (pixel_height + base_cy - 1) / base_cy;
			cumulative_px += pixel_height;
			i++;
		}

		_wrap_line_y[line_count] = cumulative;
		_line_pixel_y[line_count] = cumulative_px;
		_total_visual_rows = cumulative;
		_total_content_height = cumulative_px;
	}

protected:
	void draw_view(pf::window_frame_ptr& window, pf::draw_context& draw) const override
	{
		const auto rcClient = client_rect();
		const auto line_count = static_cast<int>(_doc->size());
		const auto pad_top = text_top();

		draw.fill_solid_rect(rcClient, style_to_color(style::normal_bkgnd));

		const auto left_pad = _font_extent.cx * 2;
		const auto avail_width = rcClient.right - left_pad;

		// Find first visible document line using pixel-based scroll offset
		int nCurrentLine;
		if (!_line_pixel_y.empty())
		{
			const int content_y = std::max(0, _scroll_offset.y - top_content_padding());
			const auto it = std::upper_bound(_line_pixel_y.begin(), _line_pixel_y.end(), content_y);
			nCurrentLine = std::max(0, static_cast<int>(it - _line_pixel_y.begin()) - 1);
			nCurrentLine = std::min(nCurrentLine, line_count);
		}
		else
		{
			nCurrentLine = 0;
		}
		auto y = top_content_padding();
		if (nCurrentLine < static_cast<int>(_line_pixel_y.size()))
			y += _line_pixel_y[nCurrentLine];
		y = y - _scroll_offset.y + pad_top;

		while (y < rcClient.bottom && nCurrentLine < line_count)
		{
			(*_doc)[nCurrentLine].render(_line_buf);

			// ── Table block rendering ───────────────────────────────
			if (is_table_row(_line_buf))
			{
				const auto* const table = table_at(nCurrentLine);
				if (table)
				{
					for (int tl = nCurrentLine; tl < table->end_line && y < rcClient.bottom; tl++)
					{
						(*_doc)[tl].render(_line_buf);

						if (tl == table->separator_line)
						{
							draw_table_separator_row(draw, y, left_pad, rcClient.right, *table,
							                         _events.styles().text_font,
							                         _font_extent.cx, _font_extent.cy);
							y += _font_extent.cy;
						}
						else
						{
							const auto vis_rows = draw_table_row(draw, y, left_pad, rcClient.right,
							                                     _line_buf, *table,
							                                     _events.styles().text_font,
							                                     _font_extent.cx, _font_extent.cy,
							                                     tl == table->start_line);
							y += vis_rows * _font_extent.cy;
						}
					}
					nCurrentLine = table->end_line;
					continue;
				}
			}

			// ── Normal markdown line ────────────────────────────────
			parse_line(_line_buf, _line_info);
			const auto& info = _line_info;
			const auto font = font_for_heading(info.heading_level);
			auto font_cx = _font_extent.cx;
			auto font_cy = _font_extent.cy;

			if (info.heading_level > 0)
			{
				const auto& sz = _heading_font_extent[info.heading_level - 1];
				font_cx = sz.cx;
				font_cy = sz.cy;
			}

			// vertical spacing: extra padding before headings
			const auto line_pad = _font_extent.cy / 4; // baseline inter-line padding
			if (info.heading_level > 0 && nCurrentLine > 0)
				y += font_cy / 2; // extra space above headings

			const auto chars_per_row = safe_cols(avail_width, font_cx);
			const auto indent_cols = info.is_list ? info.content_start : 0;
			const auto wrap_cols = std::max(1, chars_per_row - indent_cols);
			const auto line_len = static_cast<int>(_line_buf.size());

			wrap_line_into(_line_buf, wrap_cols);
			const auto num_rows = static_cast<int>(_breaks.size()) + 1;

			for (int row = 0; row < num_rows && y < rcClient.bottom; row++)
			{
				const auto row_start = row == 0 ? 0 : _breaks[row - 1];
				const auto row_end = row < static_cast<int>(_breaks.size()) ? _breaks[row] : line_len;

				const auto row_left = (row > 0 && indent_cols > 0)
					                      ? left_pad + indent_cols * font_cx
					                      : left_pad;

				draw_md_line(draw, pf::irect(row_left, y, rcClient.right, y + font_cy),
				             _line_buf, info, font, font_cx, nCurrentLine, row_start, row_end);
				y += font_cy;
			}

			// vertical spacing after the line
			if (info.heading_level > 0)
				y += font_cy / 3; // extra space below headings
			else
				y += line_pad;

			nCurrentLine++;
		}

		_vscroll.draw(draw, scrollbar_rect());
		draw_message_bar(draw);
	}

private:
	// Draw pipe character(s) at x for the given number of sub-rows — delegates to table_layout
	static void draw_pipe(pf::draw_context& draw, const int x, const int y, const int rows,
	                      const pf::font& font, const int font_cx, const int font_cy,
	                      const pf::color_t fg, const pf::color_t bg)
	{
		table_layout::draw_pipe(draw, x, y, rows, font, font_cx, font_cy, fg, bg);
	}

	// ── Table support ──────────────────────────────────────────────────

	using table_block = table_layout::table_block;

	// Tables found by the last layout, in document order
	std::vector<table_block> _tables;

	[[nodiscard]] const table_block* table_at(const int line) const
	{
		for (const auto& t : _tables)
			if (line >= t.start_line && line < t.end_line)
				return &t;
		return nullptr;
	}

	static bool is_table_row(const std::string_view line)
	{
		if (line.empty()) return false;
		const auto pos = line.find_first_not_of(u8' ');
		return pos != std::string_view::npos && line[pos] == u8'|';
	}

	static bool is_table_separator(const std::string_view line)
	{
		bool has_dash = false, has_pipe = false;
		for (const auto ch : line)
		{
			if (ch == u8'-') has_dash = true;
			else if (ch == u8'|') has_pipe = true;
			else if (ch != u8' ' && ch != u8':') return false;
		}
		return has_dash && has_pipe;
	}

	static std::string_view trim_cell(const std::string_view s) { return table_layout::trim_cell(s); }

	static std::vector<std::string_view>& split_cells(std::vector<std::string_view>& cells,
	                                                 const std::string_view line)
	{
		return table_layout::split_pipe_cells(cells, line);
	}

	static bool is_right_align_cell(const std::string_view text) { return table_layout::is_right_align_cell(text); }

	table_block find_table(const int line_hint, const int avail_cols) const
	{
		table_block result;
		const auto line_count = static_cast<int>(_doc->size());
		if (line_hint < 0 || line_hint >= line_count) return result;

		auto& tmp = _table_buf;
		(*_doc)[line_hint].render(tmp);
		if (!is_table_row(tmp)) return result;

		// search backward for table start
		int start = line_hint;
		while (start > 0)
		{
			(*_doc)[start - 1].render(tmp);
			if (!is_table_row(tmp)) break;
			start--;
		}

		// search forward for table end
		int end = line_hint + 1;
		while (end < line_count)
		{
			(*_doc)[end].render(tmp);
			if (!is_table_row(tmp)) break;
			end++;
		}

		// need at least header + separator
		if (end - start < 2) return result;

		// second row must be a separator
		(*_doc)[start + 1].render(tmp);
		if (!is_table_separator(tmp)) return result;

		result.start_line = start;
		result.end_line = end;
		result.separator_line = start + 1;

		// compute natural column widths from all non-separator rows
		for (int i = start; i < end; i++)
		{
			if (i == result.separator_line) continue;
			(*_doc)[i].render(tmp);
			split_cells(_cells, tmp);
			while (result.col_widths.size() < _cells.size())
				result.col_widths.push_back(0);
			for (size_t c = 0; c < _cells.size(); c++)
			{
				const auto w = pf::utf8_codepoint_count(trim_cell(_cells[c]));
				if (w > result.col_widths[c]) result.col_widths[c] = w;
			}
		}

		// cap column widths to fit available screen width
		table_layout::cap_col_widths(result.col_widths, avail_cols);

		return result;
	}

	// Returns the total height in visual rows consumed by this table row
	int draw_table_row(pf::draw_context& draw, const int y, const int left_pad, const int right,
	                   const std::string_view line_text, const table_block& table,
	                   const pf::font& font, const int font_cx, const int font_cy,
	                   const bool is_header) const
	{
		const auto bg = style_to_color(style::normal_bkgnd);
		const auto mk = style_to_color(style::md_marker);
		const auto tx = is_header ? style_to_color(style::md_bold) : style_to_color(style::normal_text);

		split_cells(_cells, line_text);

		return table_layout::draw_table_row(draw, y, left_pad, right, _cells, table,
		                                    font, font_cx, font_cy, is_header, bg, mk, tx,
		                                    _cell_breaks, cell_breaks_fn);
	}

	void draw_table_separator_row(pf::draw_context& draw, const int y, const int left_pad,
	                              const int right, const table_block& table,
	                              const pf::font& font, const int font_cx, const int font_cy) const
	{
		const auto bg = style_to_color(style::normal_bkgnd);
		const auto mk = style_to_color(style::md_marker);
		table_layout::draw_separator_row(draw, y, left_pad, right, table, font, font_cx, font_cy, bg, mk);
	}

	// ── Inline markdown ───────────────────────────────────────────────────

	enum class span_type
	{
		plain,
		marker,
		bold,
		italic,
		bold_italic,
		link_text,
		link_url,
		bullet
	};

	struct md_span
	{
		int start;
		int length;
		span_type type;
	};

	struct line_info
	{
		int heading_level = 0; // 0=none, 1-3
		bool is_list = false;
		int content_start = 0;
		std::vector<md_span> spans;
	};

	static pf::color_t color_for_span(const span_type type, const int heading_level)
	{
		switch (type)
		{
		case span_type::marker:
			return style_to_color(style::md_marker);
		case span_type::bold:
			return style_to_color(heading_level > 0
				                      ? static_cast<style>(static_cast<int>(style::md_heading1) + heading_level - 1)
				                      : style::md_bold);
		case span_type::italic:
			return style_to_color(style::md_italic);
		case span_type::bold_italic:
			return style_to_color(style::md_bold);
		case span_type::link_text:
			return style_to_color(style::md_link_text);
		case span_type::link_url:
			return style_to_color(style::md_link_url);
		case span_type::bullet:
			return style_to_color(style::md_bullet);
		case span_type::plain:
		default:
			if (heading_level == 1) return style_to_color(style::md_heading1);
			if (heading_level == 2) return style_to_color(style::md_heading2);
			if (heading_level == 3) return style_to_color(style::md_heading3);
			return style_to_color(style::normal_text);
		}
	}

	static bool spell_check_span(const span_type type)
	{
		switch (type)
		{
		case span_type::marker:
		case span_type::link_url:
		case span_type::bullet:
			return false;
		case span_type::plain:
		case span_type::bold:
		case span_type::italic:
		case span_type::bold_italic:
		case span_type::link_text:
		default:
			return true;
		}
	}

	static bool is_spell_word(const std::string_view text)
	{
		return !text.empty() && std::ranges::all_of(text, is_spell_word_byte);
	}

	struct text_run
	{
		int start = 0;
		int length = 0;
		pf::color_t color;
	};

	void build_text_runs(const std::string_view text, const span_type type,
	                     const pf::color_t base_color, std::vector<text_run>& runs) const
	{
		runs.clear();
		const auto spell_enabled = _doc && _doc->spell_check() && spell_check_span(type);
		const auto error_color = style_to_color(style::error_text);

		const auto push_run = [&](const int start, const int length, const pf::color_t color)
		{
			if (length <= 0) return;
			if (!runs.empty() && runs.back().start + runs.back().length == start && runs.back().color == color)
			{
				runs.back().length += length;
				return;
			}
			runs.push_back({start, length, color});
		};

		int pos = 0;
		const auto len = static_cast<int>(text.size());
		while (pos < len)
		{
			int next = pos + 1;
			auto color = base_color;
			if (is_spell_word_byte(text[pos]))
			{
				while (next < len && is_spell_word_byte(text[next]))
					next++;
				const auto word = text.substr(pos, next - pos);
				if (spell_enabled && is_spell_word(word) && !spell_check_word(word))
					color = error_color;
			}
			else
			{
				while (next < len && !is_spell_word_byte(text[next]))
					next++;
			}
			push_run(pos, next - pos, color);
			pos = next;
		}
	}

	void draw_run(pf::draw_context& draw, const pf::irect& rc, pf::ipoint& origin,
	              const std::string_view text, const int absolute_start,
	              const pf::font& font, const int font_cx, const pf::color_t color,
	              const pf::color_t bg_color, const int sel_begin, const int sel_end,
	              const pf::color_t sel_text_color, const pf::color_t sel_bg_color) const
	{
		const auto run_len = static_cast<int>(text.size());
		const auto s0 = std::clamp(sel_begin - absolute_start, 0, run_len);
		const auto s1 = std::clamp(sel_end - absolute_start, 0, run_len);
		auto clip = rc;

		if (s0 > 0 && origin.x < rc.right)
		{
			clip.left = origin.x;
			draw.draw_text(origin.x, origin.y, clip, text.substr(0, s0), font, color, bg_color);
			origin.x += font_cx * s0;
		}
		if (s1 > s0 && origin.x < rc.right)
		{
			clip.left = origin.x;
			draw.draw_text(origin.x, origin.y, clip, text.substr(s0, s1 - s0),
			               font, sel_text_color, sel_bg_color);
			origin.x += font_cx * (s1 - s0);
		}
		if (s1 < run_len && origin.x < rc.right)
		{
			clip.left = origin.x;
			draw.draw_text(origin.x, origin.y, clip, text.substr(s1), font, color, bg_color);
			origin.x += font_cx * (run_len - s1);
		}
	}

	pf::font font_for_heading(const int level) const
	{
		const auto& styles = _events.styles();
		switch (level)
		{
		case 1: return {styles.text_font_height + 12, pf::font_name::consolas};
		case 2: return {styles.text_font_height + 8, pf::font_name::consolas};
		case 3: return {styles.text_font_height + 4, pf::font_name::consolas};
		default: return styles.text_font;
		}
	}

	static void parse_line(const std::string_view text, line_info& info)
	{
		info.heading_level = 0;
		info.is_list = false;
		info.content_start = 0;
		info.spans.clear();

		if (text.empty())
			return;

		int pos = 0;
		const auto len = static_cast<int>(text.size());

		// Check for heading
		if (text[0] == u8'#')
		{
			int level = 0;
			while (pos < len && text[pos] == u8'#' && level < 3)
			{
				level++;
				pos++;
			}
			if (pos < len && text[pos] == u8' ')
			{
				info.heading_level = level;
				info.spans.push_back({0, pos + 1, span_type::marker});
				info.content_start = pos + 1;
				parse_inline(text, pos + 1, info);
				return;
			}
			pos = 0; // not a valid heading
		}

		// Check for unordered list
		if (len >= 2 && (text[0] == u8'-' || text[0] == u8'*') && text[1] == u8' ')
		{
			info.is_list = true;
			info.spans.push_back({0, 1, span_type::bullet});
			info.spans.push_back({1, 1, span_type::plain});
			info.content_start = 2;
			parse_inline(text, 2, info);
			return;
		}

		// Check for ordered list (digit(s) followed by ". ")
		if (len >= 3 && text[0] >= u8'0' && text[0] <= u8'9')
		{
			int p = 0;
			while (p < len && text[p] >= u8'0' && text[p] <= u8'9') p++;
			if (p < len - 1 && text[p] == u8'.' && text[p + 1] == u8' ')
			{
				info.is_list = true;
				info.spans.push_back({0, p + 1, span_type::bullet});
				info.spans.push_back({p + 1, 1, span_type::plain});
				info.content_start = p + 2;
				parse_inline(text, p + 2, info);
				return;
			}
		}

		// Plain line with inline formatting
		parse_inline(text, 0, info);
	}

	static void parse_inline(const std::string_view text, const int start, line_info& info)
	{
		const auto len = static_cast<int>(text.size());
		int pos = start;
		int plain_start = pos;

		auto flush_plain = [&](const int end)
		{
			if (end > plain_start)
				info.spans.push_back({plain_start, end - plain_start, span_type::plain});
		};

		while (pos < len)
		{
			// Bold+italic: ***text*** or ___text___
			if (pos + 2 < len && ((text[pos] == u8'*' && text[pos + 1] == u8'*' && text[pos + 2] == u8'*') ||
				(text[pos] == u8'_' && text[pos + 1] == u8'_' && text[pos + 2] == u8'_')))
			{
				const auto marker = text[pos];
				const auto end = find_marker(text, pos + 3, marker, 3);
				if (end >= 0)
				{
					flush_plain(pos);
					info.spans.push_back({pos, 3, span_type::marker});
					info.spans.push_back({pos + 3, end - pos - 3, span_type::bold_italic});
					info.spans.push_back({end, 3, span_type::marker});
					pos = end + 3;
					plain_start = pos;
					continue;
				}
			}

			// Bold: **text** or __text__
			if (pos + 1 < len && ((text[pos] == u8'*' && text[pos + 1] == u8'*') ||
				(text[pos] == u8'_' && text[pos + 1] == u8'_')))
			{
				const auto marker = text[pos];
				const auto end = find_marker(text, pos + 2, marker, 2);
				if (end >= 0)
				{
					flush_plain(pos);
					info.spans.push_back({pos, 2, span_type::marker});
					info.spans.push_back({pos + 2, end - pos - 2, span_type::bold});
					info.spans.push_back({end, 2, span_type::marker});
					pos = end + 2;
					plain_start = pos;
					continue;
				}
			}

			// Italic: *text* or _text_
			if (text[pos] == u8'*' || text[pos] == u8'_')
			{
				const auto marker = text[pos];
				const auto end = find_marker(text, pos + 1, marker, 1);
				if (end >= 0)
				{
					flush_plain(pos);
					info.spans.push_back({pos, 1, span_type::marker});
					info.spans.push_back({pos + 1, end - pos - 1, span_type::italic});
					info.spans.push_back({end, 1, span_type::marker});
					pos = end + 1;
					plain_start = pos;
					continue;
				}
			}

			// Link: [text](url)
			if (text[pos] == u8'[')
			{
				const auto close_bracket = find_char(text, pos + 1, u8']');
				if (close_bracket >= 0 && close_bracket + 1 < len && text[close_bracket + 1] == u8'(')
				{
					const auto close_paren = find_char(text, close_bracket + 2, u8')');
					if (close_paren >= 0)
					{
						flush_plain(pos);
						info.spans.push_back({pos, 1, span_type::marker}); // [
						info.spans.push_back({pos + 1, close_bracket - pos - 1, span_type::link_text});
						info.spans.push_back({close_bracket, 2, span_type::marker}); // ](
						info.spans.push_back({close_bracket + 2, close_paren - close_bracket - 2, span_type::link_url});
						info.spans.push_back({close_paren, 1, span_type::marker}); // )
						pos = close_paren + 1;
						plain_start = pos;
						continue;
					}
				}
			}

			pos++;
		}

		flush_plain(len);
	}

	static int find_marker(const std::string_view text, const int start, const char ch, const int count)
	{
		const auto len = static_cast<int>(text.size());
		if (start > len - count) return -1;

		auto pos = static_cast<size_t>(start);
		while (pos != std::string_view::npos && static_cast<int>(pos) <= len - count)
		{
			pos = text.find(ch, pos);
			if (pos == std::string_view::npos || static_cast<int>(pos) > len - count)
				return -1;

			bool match = true;
			for (int j = 1; j < count; j++)
			{
				if (text[pos + j] != ch)
				{
					match = false;
					break;
				}
			}
			if (match)
				return static_cast<int>(pos);
			pos++;
		}
		return -1;
	}

	static int find_char(const std::string_view text, const int start, const char ch)
	{
		const auto pos = text.find(ch, start);
		return pos == std::string_view::npos ? -1 : static_cast<int>(pos);
	}

	void draw_md_line(pf::draw_context& draw, const pf::irect& rc,
	                  const std::string_view line_text, const line_info& info,
	                  const pf::font& font, const int font_cx, const int line_index,
	                  const int row_start = 0, int row_end = -1) const
	{
		if (row_end < 0) row_end = static_cast<int>(line_text.size());

		const auto bg_color = style_to_color(style::normal_bkgnd);
		pf::ipoint origin(rc.left, rc.top);

		if (info.spans.empty())
			return;

		const auto sel = _doc->selection();
		int sel_begin = 0, sel_end = 0;

		if (!sel.empty())
		{
			const auto line_len = static_cast<int>(line_text.size());
			sel_begin = sel._start.y > line_index
				            ? line_len
				            : sel._start.y == line_index
				            ? std::clamp(sel._start.x, 0, line_len)
				            : 0;
			sel_end = sel._end.y > line_index
				          ? line_len
				          : sel._end.y == line_index
				          ? std::clamp(sel._end.x, 0, line_len)
				          : 0;
		}

		const auto sel_text_color = style_to_color(style::sel_text);
		const auto sel_bg_color = style_to_color(style::sel_bkgnd);

		for (const auto& span : info.spans)
		{
			const auto span_end = span.start + span.length;

			// Skip spans entirely outside this visual row
			if (span_end <= row_start || span.start >= row_end)
				continue;
			if (span.length <= 0 || span.start >= static_cast<int>(line_text.size()))
				continue;

			// Clip span to this visual row
			const auto vis_start = std::max(span.start, row_start);
			const auto vis_end = std::min(span_end, row_end);
			const auto vis_len = vis_end - vis_start;

			const auto color = color_for_span(span.type, info.heading_level);
			const auto text = line_text.substr(vis_start, vis_len);

			build_text_runs(text, span.type, color, _runs);
			for (const auto& run : _runs)
			{
				draw_run(draw, rc, origin,
				         text.substr(run.start, run.length),
				         vis_start + run.start,
				         font, font_cx, run.color, bg_color,
				         sel_begin, sel_end, sel_text_color, sel_bg_color);
			}
		}
	}

	// Layout and paint scratch, reused for every line so drawing allocates nothing
	mutable std::string _line_buf;
	mutable std::string _table_buf;
	mutable line_info _line_info;
	mutable std::vector<int> _breaks;
	mutable std::vector<int> _cell_breaks;
	mutable std::vector<text_run> _runs;
	mutable std::vector<std::string_view> _cells;

	// Fills the caller's buffer, so wrapping a cell costs no allocation
	static void cell_breaks_fn(std::vector<int>& out, const std::string_view text, const int col_w)
	{
		calc_word_breaks_into(out, text, col_w, [](int, int) { return 1; });
	}

	void wrap_line_into(const std::string_view text, const int cols) const
	{
		calc_word_breaks_into(_breaks, text, cols, [](int, int) { return 1; });
	}
};
