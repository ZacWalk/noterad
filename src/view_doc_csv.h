// view_doc_csv.h — CSV document view: read-only table display for comma-separated value files

#pragma once

#include "view_doc_readonly.h"

class csv_doc_view final : public read_only_doc_view
{
	table_layout::table_block _table; // cached column layout for entire document

	// Paint and layout scratch, reused across every row
	mutable std::string _line_buf;
	mutable std::vector<std::string_view> _cells;
	mutable std::vector<int> _cell_breaks;

	// Fills the caller's buffer, so wrapping a cell costs no allocation
	static void cell_breaks(std::vector<int>& out, const std::string_view text, const int col_w)
	{
		calc_word_breaks_into(out, text, col_w, [](int, int) { return 1; });
	}

public:
	csv_doc_view(app_events& events) : read_only_doc_view(events)
	{
		_sel_margin = false;
		_word_wrap = true;
	}

	~csv_doc_view() override = default;

	void set_document(const document_ptr& d) override
	{
		doc_view::set_document(d);
		rebuild_table();
	}

	void recalc_vert_scrollbar() override
	{
		_content_extent.cy = top_content_padding() + _total_visual_rows * _font_extent.cy +
			bottom_content_padding();

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
		rebuild_table();

		if (!_doc || _table.col_widths.empty())
		{
			_wrap_breaks.clear();
			_wrap_offsets.clear();
			_wrap_line_y.clear();
			_total_visual_rows = 0;
			return;
		}

		const auto line_count = static_cast<int>(_doc->size());
		_wrap_breaks.clear();
		_wrap_offsets.clear();
		_wrap_line_y.resize(line_count + 1);
		_wrap_line_y[0] = 0;

		int cumulative = 0;

		for (int i = 0; i < line_count; i++)
		{
			_wrap_line_y[i] = cumulative;

			(*_doc)[i].render(_line_buf);
			table_layout::split_csv_cells(_cells, _line_buf);

			int max_rows = 1;
			for (size_t c = 0; c < _table.col_widths.size() && c < _cells.size(); c++)
			{
				const auto vr = table_layout::cell_visual_rows(
					table_layout::trim_cell(_cells[c]), _table.col_widths[c], _cell_breaks, cell_breaks);
				if (vr > max_rows) max_rows = vr;
			}
			cumulative += max_rows;

			// Add one visual row for the separator after the header
			if (i == 0 && line_count > 1)
				cumulative += 1;
		}

		_wrap_line_y[line_count] = cumulative;
		_total_visual_rows = cumulative;
	}

protected:
	void draw_view(pf::window_frame_ptr& window, pf::draw_context& draw) const override
	{
		const auto rcClient = client_rect();
		const auto line_count = static_cast<int>(_doc->size());
		const auto pad_top = text_top();
		const auto left_pad = _font_extent.cx * 2;
		const auto font_cx = _font_extent.cx;
		const auto font_cy = _font_extent.cy;
		const auto& font = _events.styles().text_font;

		const auto bg = style_to_color(style::normal_bkgnd);
		const auto pipe_clr = style_to_color(style::md_marker);
		const auto header_clr = style_to_color(style::md_bold);
		const auto text_clr = style_to_color(style::normal_text);

		draw.fill_solid_rect(rcClient, bg);

		if (_table.col_widths.empty())
		{
			_vscroll.draw(draw, scrollbar_rect());
			draw_message_bar(draw);
			return;
		}

		// Find first visible line
		int nCurrentLine;
		if (!_wrap_line_y.empty())
		{
			const int first_vrow = font_cy > 0
				                       ? std::max(0, (_scroll_offset.y - top_content_padding()) / font_cy)
				                       : 0;
			nCurrentLine = visual_row_to_line_index(first_vrow);
		}
		else
		{
			nCurrentLine = 0;
		}

		auto y = line_offset(nCurrentLine) - _scroll_offset.y + pad_top;

		while (y < rcClient.bottom && nCurrentLine < line_count)
		{
			(*_doc)[nCurrentLine].render(_line_buf);
			table_layout::split_csv_cells(_cells, _line_buf);

			const bool is_header = (nCurrentLine == 0);
			const auto tx = is_header ? header_clr : text_clr;

			const auto vis_rows = table_layout::draw_table_row(
				draw, y, left_pad, rcClient.right, _cells, _table,
				font, font_cx, font_cy, is_header, bg, pipe_clr, tx, _cell_breaks, cell_breaks);

			y += vis_rows * font_cy;

			// Draw a separator row after the header
			if (is_header && line_count > 1 && y < rcClient.bottom)
			{
				table_layout::draw_separator_row(draw, y, left_pad, rcClient.right, _table,
				                                 font, font_cx, font_cy, bg, pipe_clr);
				y += font_cy;
			}

			nCurrentLine++;
		}

		_vscroll.draw(draw, scrollbar_rect());
		draw_message_bar(draw);
	}

private:
	void rebuild_table()
	{
		_table = {};

		if (!_doc || _doc->empty()) return;

		const auto line_count = static_cast<int>(_doc->size());
		const auto left_pad = _font_extent.cx * 2;
		const auto avail_width = _view_extent.cx > left_pad ? _view_extent.cx - left_pad : 1;
		const auto avail_cols = safe_cols(avail_width, _font_extent.cx);

		_table.start_line = 0;
		_table.end_line = line_count;
		_table.separator_line = -1; // separator is drawn visually, not from a document line

		// Compute natural column widths from all rows
		for (int i = 0; i < line_count; i++)
		{
			(*_doc)[i].render(_line_buf);
			table_layout::split_csv_cells(_cells, _line_buf);

			while (_table.col_widths.size() < _cells.size())
				_table.col_widths.push_back(0);

			for (size_t c = 0; c < _cells.size(); c++)
			{
				const auto w = pf::utf8_codepoint_count(table_layout::trim_cell(_cells[c]));
				if (w > _table.col_widths[c]) _table.col_widths[c] = w;
			}
		}

		table_layout::cap_col_widths(_table.col_widths, avail_cols);
	}
};
