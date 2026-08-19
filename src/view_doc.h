// view_doc.h — Document view: selection, caret, word wrap, syntax-highlighted rendering

#pragma once

#include "view_text.h"
#include "commands.h"


constexpr auto TIMER_DRAGSEL = 1001;

class doc_view : public text_view
{
protected:
	bool _drag_selection = false;
	bool _word_selection = false;
	bool _line_selection = false;
	bool _sel_margin = true;
	uint32_t _drag_sel_timer = 0;

	// Dragging the selection itself: pending until the mouse actually moves
	bool _drag_text_pending = false;
	bool _drag_text = false;
	pf::ipoint _drag_text_origin{};
	text_location _drop_location{};

	document_ptr _doc;

	int _screen_chars = 0;

	caret_blinker _caret;

	int _total_visual_rows = 0;

	// Text layout scratch buffers
	mutable std::vector<text_block> _block_buf;
	mutable std::vector<text_block> _spell_blocks;
	mutable std::string _expand_buf;
	mutable std::string _render_buf; // line text being drawn / hit-tested
	mutable std::string _highlight_buf; // line text being parsed by the highlighter

	// Word wrap state
	bool _word_wrap = true;
	std::vector<int> _wrap_breaks; // all break points, concatenated across all lines
	std::vector<int> _wrap_offsets;
	std::vector<int> _wrap_line_y; // prefix sum: _wrap_line_y[i] = first visual row of doc line i
	std::vector<int> _line_breaks_buf; // scratch for one line's break points

	// Lines whose text changed since the last layout; -1 means none
	int _wrap_dirty_first = -1;
	int _wrap_dirty_last = -1;
	bool _wrap_dirty_all = true;

	// Syntax highlighting — owned by the view, not the document
	highlight_fn _highlight;
	mutable std::vector<uint32_t> _parse_cookies;
	mutable int _cookies_valid_to = 0; // cookies at or above this line are stale

public:
	doc_view(app_events& events) : text_view(events),
	                               _highlight(select_highlighter(doc_type::text, {}))
	{
	}

	~doc_view() override = default;

	[[nodiscard]] virtual bool has_caret() const { return true; }

	// Views that lay text out non-uniformly cannot hit-test a click, so they do not drag-select
	[[nodiscard]] virtual bool allows_drag_selection() const { return true; }

	// Only a writable view can move text by dragging it
	[[nodiscard]] virtual bool allows_text_drag() const { return false; }

	// Where target ends up once the selection above it has been removed
	[[nodiscard]] static text_location adjust_for_removal(const text_selection& sel, const text_location& target)
	{
		if (!(sel._end < target))
			return target;

		auto result = target;
		if (target.y == sel._end.y)
			result.x = sel._start.x + (target.x - sel._end.x);
		result.y = target.y - (sel._end.y - sel._start.y);
		return result;
	}

	void start_caret_blink(const pf::window_frame_ptr& window)
	{
		if (window && has_caret() && !_caret.active)
		{
			_caret.start(window);
			_events.invalidate(invalid::windows);
		}
	}

	void stop_caret_blink(const pf::window_frame_ptr& window) override
	{
		if (window && _caret.active)
		{
			_caret.stop(window);
			_events.invalidate(invalid::windows);
		}
	}

	void reset_caret_blink(const pf::window_frame_ptr& window)
	{
		if (window && has_caret() && _focused)
		{
			_caret.reset(window);
			_events.invalidate(invalid::windows);
		}
	}

	virtual void set_document(const document_ptr& d)
	{
		_scroll_offset = {};
		_doc = d;
		_highlight = select_highlighter(d ? d->get_doc_type() : doc_type::text,
		                                d ? d->path() : pf::file_path{});
		reset_parse_cookies();

		// Clear stale word-wrap state so visual_row_to_line_index
		// won't index into data sized for a previous document.
		_wrap_breaks.clear();
		_wrap_offsets.clear();
		_wrap_line_y.clear();
		_total_visual_rows = 0;
		mark_wrap_dirty_all();
	}

	void mark_wrap_dirty_all() { _wrap_dirty_all = true; }

	void mark_wrap_dirty(const int start, const int end)
	{
		const auto lo = std::max(0, std::min(start, end));
		const auto hi = std::max(start, end);
		_wrap_dirty_first = _wrap_dirty_first < 0 ? lo : std::min(_wrap_dirty_first, lo);
		_wrap_dirty_last = std::max(_wrap_dirty_last, hi);
	}

	// --- Text selection overrides (coordinate with document) ---

	[[nodiscard]] text_selection current_selection() const override
	{
		return _doc ? _doc->selection() : text_selection{};
	}

	void set_selection(const text_selection& sel) override { if (_doc) _doc->select(sel); }
	[[nodiscard]] bool has_current_selection() const override { return _doc && _doc->has_selection(); }

	// --- Syntax highlighting (view-owned) ---

	// Cookies are stored per line but only trusted below a watermark, so an edit
	// invalidates the tail of the document in O(1).
	void reset_parse_cookies() const
	{
		_parse_cookies.assign(_doc ? _doc->size() : 0, invalid_cookie);
		_cookies_valid_to = static_cast<int>(_parse_cookies.size());
	}

	[[nodiscard]] bool cookie_valid(const int lineIndex) const
	{
		return lineIndex < _cookies_valid_to && _parse_cookies[lineIndex] != invalid_cookie;
	}

	// Drop cookies between the watermark and 'lineIndex' so the watermark can move up
	void discard_cookies_below(const int lineIndex) const
	{
		if (lineIndex > _cookies_valid_to)
			std::fill(_parse_cookies.begin() + _cookies_valid_to,
			          _parse_cookies.begin() + lineIndex, invalid_cookie);
		_cookies_valid_to = std::max(_cookies_valid_to, lineIndex);
	}

	void set_parse_cookie(const int lineIndex, const uint32_t cookie) const
	{
		if (lineIndex < 0 || lineIndex >= static_cast<int>(_parse_cookies.size()))
			return;

		discard_cookies_below(lineIndex);
		_parse_cookies[lineIndex] = cookie;
		_cookies_valid_to = std::max(_cookies_valid_to, lineIndex + 1);
	}

	uint32_t highlight_cookie(const int lineIndex) const
	{
		if (!_doc || lineIndex < 0 || lineIndex >= static_cast<int>(_doc->size()))
			return 0;

		if (_parse_cookies.size() != _doc->size())
			reset_parse_cookies();

		auto i = lineIndex;
		const auto scan_limit = std::max(0, i - 1000);
		while (i >= scan_limit && !cookie_valid(i))
			i--;
		if (i < scan_limit) i = scan_limit;
		else i++;

		if (i > lineIndex)
			return _parse_cookies[lineIndex];

		discard_cookies_below(i);

		int nBlocks;

		while (i <= lineIndex)
		{
			const uint32_t dwCookie = i > 0 ? _parse_cookies[i - 1] : 0;

			const auto& line = (*_doc)[i];
			line.render(_highlight_buf);

			_parse_cookies[i] = _highlight(dwCookie, _highlight_buf, nullptr, nBlocks);
			assert(_parse_cookies[i] != invalid_cookie);
			i++;
		}

		_cookies_valid_to = std::max(_cookies_valid_to, i);

		return _parse_cookies[lineIndex];
	}

	// line_text must already hold the rendered text of the line being drawn
	uint32_t highlight_line(const uint32_t cookie, const std::string_view line_text,
	                        text_block* pBuf, int& nBlocks) const
	{
		const auto result = _highlight(cookie, line_text, pBuf, nBlocks);

		if (_doc->spell_check() && pBuf)
		{
			const auto len = static_cast<int>(line_text.size());

			auto can_spell_check_style = [](const style color)
			{
				switch (color)
				{
				case style::normal_text:
				case style::md_heading1:
				case style::md_heading2:
				case style::md_heading3:
				case style::md_bold:
				case style::md_italic:
				case style::md_link_text:
					return true;
				default:
					return false;
				}
			};
			auto append_block = [](std::vector<text_block>& blocks, const int pos, const style color)
			{
				if (!blocks.empty())
				{
					if (blocks.back()._char_pos == pos)
					{
						blocks.back()._color = color;
						return;
					}
					if (blocks.back()._color == color)
						return;
				}
				if (pos == 0 && color == style::normal_text)
					return;
				blocks.push_back({pos, color});
			};

			auto& spell_blocks = _spell_blocks;
			spell_blocks.clear();

			auto append_segment = [&](const int start, const int end, const style base_color)
			{
				if (start >= end)
					return;

				if (!can_spell_check_style(base_color))
				{
					append_block(spell_blocks, start, base_color);
					return;
				}

				int pos = start;
				while (pos < end)
				{
					int next = pos + 1;
					auto color = base_color;

					if (is_spell_word_byte(line_text[pos]))
					{
						while (next < end && is_spell_word_byte(line_text[next]))
							next++;

						if (!spell_check_word(line_text.substr(pos, next - pos)))
							color = style::error_text;
					}
					else
					{
						while (next < end && !is_spell_word_byte(line_text[next]))
							next++;
					}

					append_block(spell_blocks, pos, color);
					pos = next;
				}
			};

			if (nBlocks > 0)
			{
				append_segment(0, pBuf[0]._char_pos, style::normal_text);
				for (int i = 0; i < nBlocks; ++i)
				{
					const int start = pBuf[i]._char_pos;
					const int end = i + 1 < nBlocks ? pBuf[i + 1]._char_pos : len;
					append_segment(start, end, pBuf[i]._color);
				}
			}
			else
			{
				append_segment(0, len, style::normal_text);
			}

			nBlocks = static_cast<int>(spell_blocks.size());
			for (int i = 0; i < nBlocks; ++i)
				pBuf[i] = spell_blocks[i];
		}

		return result;
	}

	// --- frame_reactor overrides ---

	uint32_t handle_message(const pf::window_frame_ptr window, const pf::message_type msg,
	                        const pf::message_params& params) override
	{
		using mt = pf::message_type;

		if (msg == mt::timer)
		{
			on_timer(window, params.timer_id);
			return 0;
		}
		if (msg == mt::sys_color_change)
		{
			_events.invalidate(invalid::windows);
			return 0;
		}

		return text_view::handle_message(window, msg, params);
	}

	uint32_t handle_mouse(pf::window_frame_ptr window, const pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override
	{
		using mt = pf::mouse_message_type;

		if (msg == mt::set_cursor)
		{
			if (on_set_cursor(window, params.hit_test)) return 1;
			return 0;
		}
		if (msg == mt::mouse_leave) return on_mouse_leave();
		if (msg == mt::left_button_dbl_clk)
		{
			on_left_button_dbl_clk(window, params.point);
			return 0;
		}
		if (msg == mt::left_button_down)
		{
			on_left_button_down(window, params.point);
			return 0;
		}
		if (msg == mt::right_button_down)
		{
			on_right_button_down(window, params.point);
			return 0;
		}
		if (msg == mt::left_button_up)
		{
			on_left_button_up(window, params.point);
			return 0;
		}
		if (msg == mt::mouse_move)
		{
			on_mouse_move(window, params.point);
			return 0;
		}
		if (msg == mt::context_menu)
		{
			on_context_menu(window, params.point);
			return 0;
		}

		return text_view::handle_mouse(window, msg, params);
	}

	void handle_size(pf::window_frame_ptr& window, const pf::isize extent,
	                 pf::measure_context& measure) override
	{
		text_view::handle_size(window, extent, measure);
		_screen_chars = extent.cx / _font_extent.cx;
		_hscroll.set_dpi_scale(_events.styles().dpi_scale);

		// A width change invalidates every break point
		mark_wrap_dirty_all();

		if (_word_wrap)
			layout();

		_events.invalidate(invalid::doc_scrollbar | invalid::doc_layout);
	}

	// --- App-level interface overrides ---

	[[nodiscard]] bool word_wrap() const override { return _word_wrap; }

	void set_word_wrap(const bool enabled) override
	{
		if (_word_wrap == enabled)
			return;

		_word_wrap = enabled;
		_scroll_offset.x = 0;
		mark_wrap_dirty_all();
		_events.invalidate(invalid::doc);
	}

	void toggle_word_wrap() override
	{
		set_word_wrap(!_word_wrap);
	}

	[[nodiscard]] int wrap_width() const
	{
		if (_view_extent.cx <= 0 || _font_extent.cx <= 0) return 80;
		return std::max(1, (_view_extent.cx - text_left()) / _font_extent.cx);
	}

	void calc_line_wrap_into(const document_line& line, std::vector<int>& breaks)
	{
		breaks.clear();

		const auto ww = wrap_width();
		if (ww <= 0) return;

		const auto tab_sz = _doc->tab_size();

		auto& line_view = _render_buf;
		line.render(line_view);

		auto char_advance = [&](const int i, const int col) -> int
		{
			if (line_view[i] == u8'\t') return tab_sz - col % tab_sz;
			if (pf::is_utf8_continuation(line_view[i])) return 0;
			return 1;
		};

		calc_word_breaks_into(breaks, line_view, ww, char_advance);
	}

	std::span<const int> line_breaks(const int lineIndex) const
	{
		if (_wrap_offsets.empty() || lineIndex < 0
			|| lineIndex >= static_cast<int>(_wrap_offsets.size()) - 1)
			return {};
		return {
			_wrap_breaks.data() + _wrap_offsets[lineIndex],
			_wrap_breaks.data() + _wrap_offsets[lineIndex + 1]
		};
	}

	int line_break_count(const int lineIndex) const
	{
		if (_wrap_offsets.empty() || lineIndex < 0
			|| lineIndex >= static_cast<int>(_wrap_offsets.size()) - 1)
			return 0;
		return _wrap_offsets[lineIndex + 1] - _wrap_offsets[lineIndex];
	}

	int char_to_sub_row(const int lineIndex, const int charIndex) const
	{
		const auto breaks = line_breaks(lineIndex);
		if (!_word_wrap || breaks.empty())
			return 0;
		for (int b = 0; b < static_cast<int>(breaks.size()); b++)
		{
			if (charIndex < breaks[b])
				return b;
		}
		return static_cast<int>(breaks.size());
	}

	int visual_row_to_line_index(const int visual_row) const
	{
		if (!_word_wrap || _wrap_line_y.empty()) return visual_row;
		// The array lags the document between an edit and the next layout
		const int line_count = std::min(static_cast<int>(_doc->size()), static_cast<int>(_wrap_line_y.size()));
		int lo = 0, hi = line_count - 1;
		while (lo < hi)
		{
			const int mid = (lo + hi + 1) / 2;
			if (_wrap_line_y[mid] <= visual_row)
				lo = mid;
			else
				hi = mid - 1;
		}
		return lo;
	}

	int line_visual_rows(const int lineIndex) const
	{
		if (!_word_wrap || _wrap_offsets.empty()
			|| lineIndex < 0 || lineIndex >= static_cast<int>(_wrap_offsets.size()) - 1)
			return 1;
		return line_break_count(lineIndex) + 1;
	}

	void ensure_visible(pf::window_frame_ptr& window, const text_location& pt) override
	{
		if (_word_wrap && !_wrap_offsets.empty() && pt.y >= 0 && pt.y < std::ssize(_wrap_line_y))
		{
			const int sub_row = char_to_sub_row(pt.y, pt.x);
			const int cursor_vrow = _wrap_line_y[pt.y] + sub_row;
			const int cursor_top = line_offset_vrow(cursor_vrow);
			const int cursor_bottom = cursor_top + _font_extent.cy;
			const int visible_height = _view_extent.cy - text_top();

			if (cursor_bottom > _scroll_offset.y + visible_height)
				set_scroll_pixel(cursor_bottom - visible_height);
			else if (cursor_top < _scroll_offset.y)
				set_scroll_pixel(cursor_top);
		}
		else
		{
			const int cursor_top = line_offset(pt.y);
			const int cursor_bottom = cursor_top + _font_extent.cy;
			const int visible_height = _view_extent.cy - text_top();

			if (cursor_bottom > _scroll_offset.y + visible_height)
				set_scroll_pixel(cursor_bottom - visible_height);
			else if (cursor_top < _scroll_offset.y)
				set_scroll_pixel(cursor_top);

			const auto actual_pos = _doc->calc_offset(pt.y, pt.x);
			auto new_offset = scroll_char();

			if (actual_pos > new_offset + _screen_chars)
				new_offset = actual_pos - _screen_chars;
			if (actual_pos < new_offset)
				new_offset = actual_pos;

			if (new_offset >= _doc->max_line_length())
				new_offset = _doc->max_line_length() - 1;
			if (new_offset < 0)
				new_offset = 0;

			if (scroll_char() != new_offset)
				scroll_to_char(new_offset);
		}

		update_caret(window);
	}

	void select_all() override
	{
		if (_doc) set_selection(_doc->all());
	}

	std::string select_text() const override
	{
		return has_current_selection() ? _doc->copy() : std::string{};
	}

	void update_focus(pf::window_frame_ptr& window) override
	{
		const bool was_focused = _focused;
		text_view::update_focus(window);

		if (_focused != was_focused)
		{
			if (_focused)
				start_caret_blink(window);
			else
				stop_caret_blink(window);

			invalidate_selection(window);
			update_caret(window);

			if (!_focused)
				cancel_text_drag(window);

			if (!_focused && _drag_selection)
			{
				window->release_capture();
				window->kill_timer(_drag_sel_timer);
				_drag_selection = false;
			}
		}
	}

protected:
	void on_timer(const pf::window_frame_ptr& window, const uint32_t nIDEvent)
	{
		if (_caret.on_timer(nIDEvent))
		{
			const auto r = caret_bounds();
			if (r.width() > 0)
				window->invalidate_rect(r);
			else
				window->invalidate();
			return;
		}

		if (nIDEvent == TIMER_DRAGSEL)
		{
			assert(_drag_selection);
			auto pt = pf::platform_cursor_pos();
			pt = window->screen_to_client(pt);

			const auto rcClient = client_rect();
			auto bChanged = false;
			auto y = scroll_line();
			const auto line_count = static_cast<int>(_doc->size());

			if (pt.y < rcClient.top)
			{
				y--;

				if (pt.y < rcClient.top - _font_extent.cy)
					y -= 2;
			}
			else if (pt.y >= rcClient.bottom)
			{
				y++;

				if (pt.y >= rcClient.bottom + _font_extent.cy)
					y += 2;
			}

			y = std::clamp(y, 0, std::max(0, line_count - 1));

			if (scroll_line() != y)
			{
				scroll_to_line(y);
				bChanged = true;
			}

			//	Scroll horizontally, if necessary
			auto x = scroll_char();
			const auto nMaxLineLength = _doc->max_line_length();

			if (pt.x < rcClient.left)
			{
				x--;
			}
			else if (pt.x >= rcClient.right)
			{
				x++;
			}

			x = std::clamp(x, 0, std::max(0, nMaxLineLength - 1));

			if (scroll_char() != x)
			{
				scroll_to_char(x);
				bChanged = true;
			}

			//	Fix changes
			if (bChanged)
			{
				const auto pos = client_to_text(pt);

				if (_line_selection)
				{
					_doc->select(_doc->line_selection(pos, true));
				}
				else
				{
					const auto sel = _word_selection
						                 ? _doc->word_selection(pos, true)
						                 : _doc->pos_selection(pos, true);
					_doc->select(sel);
				}
			}
		}
	}

	void on_left_button_down(const pf::window_frame_ptr& window, const pf::ipoint& point)
	{
		const bool bShift = window->is_key_down(pf::platform_key::Shift);
		const bool bControl = window->is_key_down(pf::platform_key::Control);

		window->set_focus();

		// Check scrollbar clicks first
		const auto rc = scrollbar_rect();
		if (_vscroll.begin_tracking(point, rc, window) || _hscroll.begin_tracking(point, rc, window))
		{
			_events.invalidate(invalid::windows);
			return;
		}

		// Pressing inside an existing selection may start a drag instead of a new selection
		if (allows_text_drag() && !bShift && !bControl && point.x >= margin_width()
			&& _doc->has_selection() && _doc->is_inside_selection(client_to_text(point)))
		{
			_drag_text_pending = true;
			_drag_text_origin = point;
			window->set_capture();
			return;
		}

		if (point.x < margin_width())
		{
			if (bControl)
			{
				_doc->select(_doc->all());
			}
			else if (allows_drag_selection())
			{
				const auto sel = _doc->line_selection(client_to_text(point), bShift);
				_doc->select(sel);

				_drag_sel_timer = window->set_timer(TIMER_DRAGSEL, 100);
				if (_drag_sel_timer == 0) return;
				window->set_capture();
				_word_selection = false;
				_line_selection = true;
				_drag_selection = true;
			}
		}
		else if (allows_drag_selection())
		{
			const auto pos = client_to_text(point);
			const auto sel = bControl ? _doc->word_selection(pos, bShift) : _doc->pos_selection(pos, bShift);
			_doc->select(sel);

			_drag_sel_timer = window->set_timer(TIMER_DRAGSEL, 100);
			if (_drag_sel_timer == 0) return;
			window->set_capture();
			_word_selection = bControl;
			_line_selection = false;
			_drag_selection = true;
		}
	}

	bool can_scroll() const
	{
		if (_word_wrap && _total_visual_rows > 0)
			return _screen_lines < _total_visual_rows;
		const auto line_count = static_cast<int>(_doc->size());
		return line_count > 1 && _screen_lines < line_count - 1;
	}

	// Client rect with top adjusted past the message bar, so scrollbars
	// don't overlap the info bar.
	pf::irect scrollbar_rect() const
	{
		auto rc = client_rect();
		rc.top += text_top();
		return rc;
	}

	void on_mouse_wheel(pf::window_frame_ptr& window, const int zDelta) override
	{
		if (can_scroll())
		{
			if (_word_wrap && !_wrap_line_y.empty())
			{
				set_scroll_pixel(_scroll_offset.y + zDelta * _font_extent.cy);
			}
			else
			{
				scroll_to_line(std::clamp(scroll_line() + zDelta, 0, static_cast<int>(_doc->size())));
			}
			update_caret(window);
		}
	}

	void on_mouse_move(pf::window_frame_ptr& window, const pf::ipoint& point)
	{
		// Handle scrollbar tracking
		if (_vscroll._tracking)
		{
			const auto new_pos = _vscroll.track_to(point, scrollbar_rect());
			set_scroll_pixel(new_pos);
			update_caret(window);
			return;
		}
		if (_hscroll._tracking)
		{
			const auto new_pos = _hscroll.track_to(point, client_rect());
			scroll_to_char(std::clamp(new_pos, 0, std::max(0, _doc->max_line_length() - 1)));
			update_caret(window);
			return;
		}

		if (_drag_text_pending)
		{
			const auto slop = std::max(4, _font_extent.cx / 2);
			if (std::abs(point.x - _drag_text_origin.x) > slop ||
				std::abs(point.y - _drag_text_origin.y) > slop)
			{
				_drag_text_pending = false;
				_drag_text = true;
			}
		}

		if (_drag_text)
		{
			_drop_location = client_to_text(point);
			window->set_cursor_shape(pf::cursor_shape::arrow);
			_events.invalidate(invalid::windows);
			return;
		}

		if (_drag_selection)
		{
			const auto bOnMargin = point.x < margin_width();
			const auto pos = client_to_text(point);

			if (_line_selection)
			{
				if (bOnMargin)
				{
					const auto sel = _doc->line_selection(pos, true);
					_doc->select(sel);
					return;
				}

				_line_selection = _word_selection = false;
				update_cursor(window);
			}

			const auto sel = _word_selection ? _doc->word_selection(pos, true) : _doc->pos_selection(pos, true);
			_doc->select(sel);
		}

		// Update scrollbar hover
		if (!_drag_selection)
		{
			const auto rc = scrollbar_rect();
			bool need_redraw = _vscroll.set_hover(_vscroll.hit_test(point, rc));
			need_redraw |= _hscroll.set_hover(_hscroll.hit_test(point, rc));
			if (need_redraw) window->invalidate();

			if (!_vscroll._hover_tracking)
			{
				window->track_mouse_leave();
				_vscroll._hover_tracking = true;
			}
		}
	}

	void on_left_button_up(const pf::window_frame_ptr& window, const pf::ipoint& point)
	{
		if (_vscroll._tracking || _hscroll._tracking)
		{
			_vscroll.end_tracking(window);
			_hscroll.end_tracking(window);
			window->invalidate();
			return;
		}

		// A press inside the selection that never moved just places the caret
		if (_drag_text_pending)
		{
			_drag_text_pending = false;
			window->release_capture();
			const auto pos = client_to_text(point);
			_doc->select(text_selection(pos, pos));
			_events.invalidate(invalid::doc);
			return;
		}

		if (_drag_text)
		{
			_drag_text = false;
			window->release_capture();
			drop_selected_text(client_to_text(point), window->is_key_down(pf::platform_key::Control));
			return;
		}

		if (_drag_selection)
		{
			const auto pos = client_to_text(point);

			if (_line_selection)
			{
				const auto sel = _doc->line_selection(pos, true);
				_doc->select(sel);
			}
			else
			{
				const auto sel = _word_selection ? _doc->word_selection(pos, true) : _doc->pos_selection(pos, true);
				_doc->select(sel);
			}

			window->release_capture();
			window->kill_timer(_drag_sel_timer);
			_drag_selection = false;
		}
	}

	void on_left_button_dbl_clk(const pf::window_frame_ptr& window, const pf::ipoint& point)
	{
		if (!_drag_selection && allows_drag_selection())
		{
			const bool bShift = window->is_key_down(pf::platform_key::Shift);
			_doc->select(_doc->word_selection(client_to_text(point), bShift));

			_drag_sel_timer = window->set_timer(TIMER_DRAGSEL, 100);
			if (_drag_sel_timer == 0) return;
			window->set_capture();
			_word_selection = true;
			_line_selection = false;
			_drag_selection = true;
		}
	}

	uint32_t on_right_button_down(pf::window_frame_ptr& window, const pf::ipoint& point)
	{
		if (!allows_drag_selection())
			return 0;

		const auto pt = client_to_text(point);

		if (!_doc->is_inside_selection(pt))
		{
			_doc->select(text_selection(pt, pt));
			ensure_visible(window, pt);
		}

		return 0;
	}

	void invalidate_selection(pf::window_frame_ptr& window)
	{
		const auto sel = current_selection();

		if (!sel.empty())
			invalidate_lines(window, sel._start.y, sel._end.y);
	}

	// Moves the selection to target, or copies it when copy is set
	void drop_selected_text(const text_location& target, const bool copy)
	{
		const auto sel = _doc->selection();

		if (sel.empty() || !_doc->query_editable())
			return;

		// Dropping anywhere on the selection, including either edge, moves nothing
		if (!(target < sel._start) && !(sel._end < target))
			return;

		const auto text = _doc->copy();
		if (text.empty())
			return;

		undo_group ug(_doc);

		auto insert_at = target;

		if (!copy)
		{
			insert_at = adjust_for_removal(sel, target);
			_doc->delete_text(ug, sel);
		}

		const auto end = _doc->insert_text(ug, insert_at, text);
		_doc->select(text_selection(insert_at, end));
		_events.invalidate(invalid::doc);
	}

	void cancel_text_drag(const pf::window_frame_ptr& window)
	{
		if (!_drag_text && !_drag_text_pending)
			return;

		_drag_text = false;
		_drag_text_pending = false;
		window->release_capture();
		_events.invalidate(invalid::windows);
	}

	bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;
		const bool ctrl = window->is_key_down(pk::Control);
		const bool shift = window->is_key_down(pk::Shift);

		if (vk == pk::Escape && (_drag_text || _drag_text_pending))
		{
			cancel_text_drag(window);
			return true;
		}

		// Document cursor navigation — these replace the base class scroll behavior
		if (vk == pk::Left && !ctrl && !shift)
		{
			_doc->move_char_left(false);
			return true;
		}
		if (vk == pk::Left && !ctrl && shift)
		{
			_doc->move_char_left(true);
			return true;
		}
		if (vk == pk::Right && !ctrl && !shift)
		{
			_doc->move_char_right(false);
			return true;
		}
		if (vk == pk::Right && !ctrl && shift)
		{
			_doc->move_char_right(true);
			return true;
		}
		if (vk == pk::Left && ctrl && !shift)
		{
			_doc->move_word_left(false);
			return true;
		}
		if (vk == pk::Left && ctrl && shift)
		{
			_doc->move_word_left(true);
			return true;
		}
		if (vk == pk::Right && ctrl && !shift)
		{
			_doc->move_word_right(false);
			return true;
		}
		if (vk == pk::Right && ctrl && shift)
		{
			_doc->move_word_right(true);
			return true;
		}
		if (vk == pk::Up && !ctrl && !shift)
		{
			_doc->move_lines(-1, false);
			return true;
		}
		if (vk == pk::Up && !ctrl && shift)
		{
			_doc->move_lines(-1, true);
			return true;
		}
		if (vk == pk::Down && !ctrl && !shift)
		{
			_doc->move_lines(1, false);
			return true;
		}
		if (vk == pk::Down && !ctrl && shift)
		{
			_doc->move_lines(1, true);
			return true;
		}
		if (vk == pk::Up && ctrl)
		{
			wrap_scroll_by(-1);
			return true;
		}
		if (vk == pk::Down && ctrl)
		{
			wrap_scroll_by(1);
			return true;
		}
		if (vk == pk::Prior && !shift)
		{
			_doc->move_lines(-_screen_lines, false);
			return true;
		}
		if (vk == pk::Prior && shift)
		{
			_doc->move_lines(-_screen_lines, true);
			return true;
		}
		if (vk == pk::Next && !shift)
		{
			_doc->move_lines(_screen_lines, false);
			return true;
		}
		if (vk == pk::Next && shift)
		{
			_doc->move_lines(_screen_lines, true);
			return true;
		}
		if (vk == pk::End && !ctrl && !shift)
		{
			_doc->move_line_end(false);
			return true;
		}
		if (vk == pk::End && !ctrl && shift)
		{
			_doc->move_line_end(true);
			return true;
		}
		if (vk == pk::Home && !ctrl && !shift)
		{
			_doc->move_line_home(false);
			return true;
		}
		if (vk == pk::Home && !ctrl && shift)
		{
			_doc->move_line_home(true);
			return true;
		}
		if (vk == pk::Home && ctrl && !shift)
		{
			_doc->move_doc_home(false);
			return true;
		}
		if (vk == pk::Home && ctrl && shift)
		{
			_doc->move_doc_home(true);
			return true;
		}
		if (vk == pk::End && ctrl && !shift)
		{
			_doc->move_doc_end(false);
			return true;
		}
		if (vk == pk::End && ctrl && shift)
		{
			_doc->move_doc_end(true);
			return true;
		}

		// Delegate common keys (escape, copy, select-all, zoom) to base
		return text_view::on_key_down(window, vk);
	}

	void on_context_menu(const pf::window_frame_ptr& window, const pf::ipoint& screen_pt)
	{
		const auto client_pt = window->screen_to_client(screen_pt);
		const auto items = on_popup_menu(client_pt);
		if (!items.empty())
			window->show_popup_menu(items, screen_pt);
	}

	virtual std::vector<pf::menu_command> on_popup_menu(const pf::ipoint& client_pt)
	{
		std::vector<pf::menu_command> items;
		items.push_back(_events.command_menu_item(command_id::edit_copy));
		items.emplace_back(); // separator
		items.push_back(_events.command_menu_item(command_id::edit_select_all));
		return items;
	}

	void scroll_to_char(const int x)
	{
		if (scroll_char() != x)
		{
			set_scroll_char(x);
			recalc_horz_scrollbar();
			_events.invalidate(invalid::windows);
		}
	}

	void scroll_to_line(const int y)
	{
		set_scroll_pixel(y * _font_extent.cy);
	}

public:
	void recalc_vert_scrollbar() override
	{
		// Content extent includes half-line top padding and a full line below the document.
		if (_word_wrap && _total_visual_rows > 0)
			_content_extent.cy = top_content_padding() + _total_visual_rows * _font_extent.cy +
				bottom_content_padding();
		else
			_content_extent.cy = top_content_padding() + static_cast<int>(_doc->size()) * _font_extent.cy +
				bottom_content_padding();

		const int max_y = std::max(0, _content_extent.cy - (_view_extent.cy - text_top()));

		if (_scroll_offset.y > max_y)
		{
			_scroll_offset.y = max_y;
			_events.invalidate(invalid::windows | invalid::doc_caret);
		}

		const int visible_height = std::max(0, _view_extent.cy - text_top());
		_vscroll.update(_content_extent.cy, visible_height, _scroll_offset.y);
	}

	void recalc_horz_scrollbar() override
	{
		if (_word_wrap)
		{
			_scroll_offset.x = 0;
			_hscroll.update(0, 0, 0);
			return;
		}

		if (_screen_chars >= _doc->max_line_length() && _scroll_offset.x > 0)
		{
			_scroll_offset.x = 0;
			_events.invalidate(invalid::windows | invalid::doc_caret);
		}

		const auto margin_chars = _sel_margin ? 4 : 1; // +1 for text padding
		_hscroll.update(_doc->max_line_length() + margin_chars, _screen_chars, scroll_char());
	}

	bool on_set_cursor(const pf::window_frame_ptr& window, const uint32_t nHitTest) const
	{
		if (nHitTest == 1 /*HTCLIENT*/)
		{
			update_cursor(window);
			return true;
		}
		return false;
	}

	void update_cursor(const pf::window_frame_ptr& window) const
	{
		auto pt = pf::platform_cursor_pos();
		pt = window->screen_to_client(pt);

		const auto rc = scrollbar_rect();

		if (_vscroll.hit_test(pt, rc) || _hscroll.hit_test(pt, rc))
		{
			window->set_cursor_shape(pf::cursor_shape::arrow);
		}
		else if (pt.x < margin_width())
		{
			window->set_cursor_shape(pf::cursor_shape::arrow);
		}
		else if (_doc->is_inside_selection(client_to_text(pt)))
		{
			window->set_cursor_shape(pf::cursor_shape::arrow);
		}
		else if (!allows_drag_selection() || _drag_text)
		{
			window->set_cursor_shape(pf::cursor_shape::arrow);
		}
		else
		{
			window->set_cursor_shape(pf::cursor_shape::ibeam);
		}
	}

	void update_caret(pf::window_frame_ptr& window) override
	{
		if (!has_caret()) return;
		reset_caret_blink(window);
	}

protected:
	pf::irect caret_bounds() const
	{
		const auto pos = _doc->cursor_pos();
		if (_doc->calc_offset(pos.y, pos.x) < scroll_char())
			return {};
		const auto pt = text_to_client(pos);
		return {pt.x, pt.y, pt.x + 2, pt.y + _font_extent.cy};
	}

	// TODO: Drag-drop needs platform abstraction

	void wrap_scroll_by(const int delta)
	{
		if (_word_wrap && !_wrap_line_y.empty() && can_scroll())
		{
			set_scroll_pixel(_scroll_offset.y + delta * _font_extent.cy);
		}
		else
		{
			scroll_by(delta);
		}
	}

	uint32_t on_mouse_leave()
	{
		_vscroll._hover_tracking = false;
		bool need_redraw = _vscroll.set_hover(false);
		need_redraw |= _hscroll.set_hover(false);
		if (need_redraw) _events.invalidate(invalid::windows);
		return 0;
	}

	int client_to_line(const pf::ipoint& point) const
	{
		const auto line_count = static_cast<int>(_doc->size());

		if (_font_extent.cy <= 0 || line_count == 0)
			return 0;

		// Convert screen Y to content Y, then to line index
		const int content_y = point.y - text_top() + _scroll_offset.y;

		if (_word_wrap && !_wrap_line_y.empty())
		{
			const int visual_row = (content_y - top_content_padding()) / _font_extent.cy;
			return std::clamp(visual_row_to_line_index(std::max(0, visual_row)), 0, line_count - 1);
		}

		const int line = (content_y - top_content_padding()) / _font_extent.cy;
		return std::clamp(line, 0, line_count - 1);
	}

	text_location client_to_text(const pf::ipoint& point) const
	{
		const auto line_count = static_cast<int>(_doc->size());

		text_location pt{0, client_to_line(point)};

		if (_font_extent.cx <= 0 || _font_extent.cy <= 0)
			return pt;

		if (pt.y >= 0 && pt.y < line_count)
		{
			const auto tabSize = _doc->tab_size();
			const auto& line = (*_doc)[pt.y];
			auto& line_view = _render_buf;
			line.render(line_view);

			const auto lineSize = static_cast<int>(line_view.size());

			if (_word_wrap && !_wrap_offsets.empty() && pt.y < static_cast<int>(_wrap_offsets.size()) - 1 && pt.y <
				static_cast<
					int>(_wrap_line_y.size()))
			{
				// Determine which visual sub-row was clicked
				const int line_pixel_y = line_offset(pt.y);
				const int pixel_y = point.y - text_top() + _scroll_offset.y;
				const auto breaks = line_breaks(pt.y);
				const int num_visual = static_cast<int>(breaks.size()) + 1;
				const int sub_row = std::clamp((pixel_y - line_pixel_y) / _font_extent.cy, 0, num_visual - 1);

				const int row_start = sub_row == 0 ? 0 : breaks[sub_row - 1];
				const int row_end = sub_row < static_cast<int>(breaks.size())
					                    ? breaks[sub_row]
					                    : lineSize;

				auto x = (point.x - text_left()) / _font_extent.cx;
				if (x < 0) x = 0;

				int abs_col = _doc->calc_offset(pt.y, row_start);
				int rel_col = 0;
				int i = row_start;

				while (i < row_end)
				{
					int advance = 0;
					if (line_view[i] == u8'\t')
						advance = tabSize - abs_col % tabSize;
					else if (!pf::is_utf8_continuation(line_view[i]))
						advance = 1;
					abs_col += advance;
					rel_col += advance;
					if (rel_col > x) break;
					i++;
				}

				pt.x = std::clamp(i, row_start, row_end);
			}
			else
			{
				auto x = scroll_char() + (point.x - text_left()) / _font_extent.cx;

				if (x < 0)
					x = 0;

				auto i = 0;
				auto xx = 0;

				while (i < lineSize)
				{
					if (line_view[i] == u8'\t')
					{
						xx += tabSize - xx % tabSize;
					}
					else if (!pf::is_utf8_continuation(line_view[i]))
					{
						xx++;
					}

					if (xx > x)
						break;

					i++;
				}

				pt.x = std::clamp(i, 0, lineSize);
			}
		}

		return pt;
	}

	pf::ipoint text_to_client(const text_location& point) const
	{
		pf::ipoint pt;

		if (point.y >= 0 && point.y < static_cast<int>(_doc->size()))
		{
			if (_word_wrap && !_wrap_offsets.empty() && point.y < static_cast<int>(_wrap_offsets.size()) - 1)
			{
				const auto breaks = line_breaks(point.y);
				const int sub_row = char_to_sub_row(point.y, point.x);
				const int row_start = sub_row == 0 ? 0 : breaks[sub_row - 1];

				pt.y = line_offset_vrow(_wrap_line_y[point.y] + sub_row)
					- _scroll_offset.y + text_top();

				const auto tabSize = _doc->tab_size();
				const auto& line = (*_doc)[point.y];
				auto& line_view = _render_buf;
				line.render(line_view);

				int abs_col = _doc->calc_offset(point.y, row_start);
				int rel_col = 0;
				for (int i = row_start; i < point.x && i < static_cast<int>(line_view.size()); i++)
				{
					int advance = 0;
					if (line_view[i] == u8'\t')
						advance = tabSize - abs_col % tabSize;
					else if (!pf::is_utf8_continuation(line_view[i]))
						advance = 1;
					abs_col += advance;
					rel_col += advance;
				}
				pt.x = rel_col * _font_extent.cx + text_left();
			}
			else
			{
				pt.y = line_offset(point.y) - top_offset() + text_top();
				pt.x = 0;

				const auto tabSize = _doc->tab_size();
				const auto& line = (*_doc)[point.y];
				auto& line_view = _render_buf;
				line.render(line_view);

				for (auto i = 0; i < point.x && i < static_cast<int>(line_view.size()); i++)
				{
					if (line_view[i] == u8'\t')
					{
						pt.x += tabSize - pt.x % tabSize;
					}
					else if (!pf::is_utf8_continuation(line_view[i]))
					{
						pt.x++;
					}
				}

				pt.x = (pt.x - scroll_char()) * _font_extent.cx + text_left();
			}
		}

		return pt;
	}

	int top_offset() const
	{
		return _scroll_offset.y;
	}

public:
	void invalidate_lines(pf::window_frame_ptr& window, int start, int end) override
	{
		auto rcInvalid = client_rect();
		const auto top = top_offset() - text_top();

		if (end == -1)
		{
			rcInvalid.top = line_offset(start) - top;
		}
		else
		{
			if (end < start)
			{
				std::swap(start, end);
			}

			rcInvalid.top = line_offset(start) - top;
			rcInvalid.bottom = line_offset(end) + line_height(end) - top;
		}

		invalidate(window, rcInvalid);
	}

	// Later lines inherit the highlighter cookie, so everything below 'start' becomes stale
	void lines_changed(pf::window_frame_ptr& window, const int start, const int end) override
	{
		mark_wrap_dirty(start, end);

		if (start >= 0)
			_cookies_valid_to = std::min(_cookies_valid_to, start);

		if (_word_wrap)
		{
			// A changed line can gain or lose rows, moving everything below it
			_events.invalidate(invalid::doc_layout);
			invalidate_lines(window, start, -1);
		}
		else
		{
			invalidate_lines(window, start, end);
		}
	}

	// Splice the per-line arrays rather than rebuilding them, so inserting or
	// removing a few lines stays proportional to the edit, not to the document.
	void line_count_changed(pf::window_frame_ptr& window, const int at, const int delta) override
	{
		// A dirty range recorded before this edit is in the old line numbering, and only one
		// entirely below 'at' still means what it said. A multi-step undo gets here.
		if (_wrap_dirty_last > at)
			mark_wrap_dirty_all();

		splice_parse_cookies(at, delta);
		splice_wrap(at, delta);
		mark_wrap_dirty(at, at + std::max(0, delta));
		invalidate_lines(window, at, -1);
	}

	void layout() override
	{
		if (!_doc || !_word_wrap)
		{
			_wrap_breaks.clear();
			_wrap_offsets.clear();
			_wrap_line_y.clear();
			_total_visual_rows = _doc ? static_cast<int>(_doc->size()) : 0;
			clear_wrap_dirty();
			return;
		}

		const auto line_count = static_cast<int>(_doc->size());

		// The arrays no longer describe the document, so a splice cannot patch them
		if (_wrap_dirty_all || static_cast<int>(_wrap_offsets.size()) != line_count + 1)
		{
			full_layout(line_count);
		}
		else if (_wrap_dirty_first >= 0)
		{
			const auto last = std::min(_wrap_dirty_last, line_count - 1);
			for (int i = _wrap_dirty_first; i <= last; i++)
				relayout_line(i);
		}

		clear_wrap_dirty();
	}

private:
	void clear_wrap_dirty()
	{
		_wrap_dirty_all = false;
		_wrap_dirty_first = -1;
		_wrap_dirty_last = -1;
	}

	// Cookies below 'at' stay valid; the rest is rebuilt lazily by highlight_cookie.
	void splice_parse_cookies(const int at, const int delta) const
	{
		const auto count = static_cast<int>(_parse_cookies.size());
		const auto before_edit = static_cast<int>(_doc ? _doc->size() : 0) - delta;

		if (count != before_edit || at < 0 || at >= count)
		{
			reset_parse_cookies();
			return;
		}

		if (delta > 0)
			_parse_cookies.insert(_parse_cookies.begin() + at + 1, delta, invalid_cookie);
		else if (delta < 0)
			_parse_cookies.erase(_parse_cookies.begin() + at + 1, _parse_cookies.begin() + at + 1 - delta);

		_cookies_valid_to = std::min(_cookies_valid_to, at);
	}

	// Inserted lines start with no break points; layout() re-wraps them because
	// line_count_changed marks them dirty.
	void splice_wrap(const int at, const int delta)
	{
		const auto line_count = static_cast<int>(_wrap_offsets.size()) - 1;
		const auto before_edit = static_cast<int>(_doc ? _doc->size() : 0) - delta;

		if (!_word_wrap || line_count <= 0 || line_count != before_edit
			|| at < 0 || at >= line_count
			|| static_cast<int>(_wrap_line_y.size()) != line_count + 1
			|| (delta < 0 && at + 1 - delta > line_count))
		{
			mark_wrap_dirty_all();
			return;
		}

		if (delta > 0)
		{
			const auto first_row = _wrap_line_y[at + 1];

			for (size_t i = at + 1; i < _wrap_line_y.size(); i++)
				_wrap_line_y[i] += delta;

			_wrap_line_y.insert(_wrap_line_y.begin() + at + 1, delta, 0);
			for (int k = 0; k < delta; k++)
				_wrap_line_y[at + 1 + k] = first_row + k;

			const auto first_break = _wrap_offsets[at + 1];
			_wrap_offsets.insert(_wrap_offsets.begin() + at + 1, delta, first_break);
		}
		else if (delta < 0)
		{
			const auto removed = -delta;
			const auto first_break = _wrap_offsets[at + 1];
			const auto last_break = _wrap_offsets[at + 1 + removed];
			const auto rows = _wrap_line_y[at + 1 + removed] - _wrap_line_y[at + 1];

			_wrap_breaks.erase(_wrap_breaks.begin() + first_break, _wrap_breaks.begin() + last_break);

			_wrap_offsets.erase(_wrap_offsets.begin() + at + 1, _wrap_offsets.begin() + at + 1 + removed);
			for (size_t i = at + 1; i < _wrap_offsets.size(); i++)
				_wrap_offsets[i] -= last_break - first_break;

			_wrap_line_y.erase(_wrap_line_y.begin() + at + 1, _wrap_line_y.begin() + at + 1 + removed);
			for (size_t i = at + 1; i < _wrap_line_y.size(); i++)
				_wrap_line_y[i] -= rows;
		}

		_total_visual_rows = _wrap_line_y.back();
	}

	void full_layout(const int line_count)
	{
		_wrap_breaks.clear();
		_wrap_offsets.resize(line_count + 1);
		_wrap_line_y.resize(line_count + 1);
		_wrap_line_y[0] = 0;

		for (int i = 0; i < line_count; i++)
		{
			_wrap_offsets[i] = static_cast<int>(_wrap_breaks.size());
			calc_line_wrap_into((*_doc)[i], _line_breaks_buf);
			_wrap_breaks.insert(_wrap_breaks.end(), _line_breaks_buf.begin(), _line_breaks_buf.end());
			_wrap_line_y[i + 1] = _wrap_line_y[i] + static_cast<int>(_line_breaks_buf.size()) + 1;
		}

		_wrap_offsets[line_count] = static_cast<int>(_wrap_breaks.size());
		_total_visual_rows = _wrap_line_y[line_count];
	}

	// Re-wraps one line; only shifts the tail arrays when its row count actually changed
	void relayout_line(const int line_index)
	{
		calc_line_wrap_into((*_doc)[line_index], _line_breaks_buf);

		const auto begin = _wrap_offsets[line_index];
		const auto end = _wrap_offsets[line_index + 1];
		const auto old_count = end - begin;
		const auto new_count = static_cast<int>(_line_breaks_buf.size());

		if (new_count == old_count)
		{
			std::copy(_line_breaks_buf.begin(), _line_breaks_buf.end(), _wrap_breaks.begin() + begin);
			return;
		}

		_wrap_breaks.erase(_wrap_breaks.begin() + begin, _wrap_breaks.begin() + end);
		_wrap_breaks.insert(_wrap_breaks.begin() + begin, _line_breaks_buf.begin(), _line_breaks_buf.end());

		const auto delta = new_count - old_count;
		for (size_t i = line_index + 1; i < _wrap_offsets.size(); i++)
		{
			_wrap_offsets[i] += delta;
			_wrap_line_y[i] += delta;
		}

		_total_visual_rows = _wrap_line_y.back();
	}

protected:
	[[nodiscard]] int top_content_padding() const
	{
		return _font_extent.cy > 0 ? std::max(1, _font_extent.cy / 2) : 0;
	}

	[[nodiscard]] int bottom_content_padding() const
	{
		return _font_extent.cy > 0 ? _font_extent.cy : 0;
	}

	// Content-space Y position of a document line (includes top padding)
	int line_offset(const int lineIndex) const
	{
		if (_word_wrap && !_wrap_line_y.empty())
		{
			const auto idx = std::clamp(lineIndex, 0, static_cast<int>(_wrap_line_y.size()) - 1);
			return top_content_padding() + _wrap_line_y[idx] * _font_extent.cy;
		}
		return top_content_padding() + lineIndex * _font_extent.cy;
	}

	// Content-space Y position of a visual row (includes top padding)
	int line_offset_vrow(const int vrow) const
	{
		return top_content_padding() + vrow * _font_extent.cy;
	}

	int line_height(int lineIndex) const
	{
		if (_word_wrap && _wrap_offsets.size() > 1)
		{
			lineIndex = std::clamp(lineIndex, 0, static_cast<int>(_wrap_offsets.size()) - 2);
			return (line_break_count(lineIndex) + 1) * _font_extent.cy;
		}
		return _font_extent.cy;
	}


	int margin_width() const
	{
		if (!_sel_margin) return 1;
		const auto line_count = _doc ? static_cast<int>(_doc->size()) : 0;
		int digits = 1;
		auto n = line_count;
		while (n >= 10)
		{
			n /= 10;
			digits++;
		}
		return (digits + 1) * _font_extent.cx + 4;
	}

	int text_left() const
	{
		return margin_width() + _font_extent.cx;
	}

	[[nodiscard]] pf::color_t line_bg_color(const int lineIndex) const
	{
		if (_focused && _doc && lineIndex == _doc->cursor_pos().y)
			return focus_band_color();
		return style_to_color(style::normal_bkgnd);
	}

	void draw_line(pf::draw_context& draw, pf::ipoint& ptOrigin, const pf::irect& rcClip,
	               const std::string_view pszChars, const int nOffset, const int nCount,
	               const pf::font& f, const pf::color_t text_color, const pf::color_t bg_color) const
	{
		if (nCount > 0)
		{
			_doc->expanded_chars(pszChars, nOffset, nCount, _expand_buf);
			const auto nWidth = rcClip.right - ptOrigin.x;
			const auto nCodepoints = pf::utf8_codepoint_count(_expand_buf);

			if (nWidth > 0)
			{
				const auto nCharWidth = _font_extent.cx;
				const auto nCountFit = nWidth / nCharWidth + 1;

				auto nDrawBytes = static_cast<int>(_expand_buf.size());
				auto nDrawCodepoints = nCodepoints;

				if (nCodepoints > nCountFit)
				{
					nDrawBytes = static_cast<int>(pf::utf8_truncate(_expand_buf, nCountFit));
					nDrawCodepoints = nCountFit;
				}

				auto rcClipBlock = rcClip;
				rcClipBlock.left = ptOrigin.x;
				rcClipBlock.right = ptOrigin.x + nDrawCodepoints * nCharWidth;
				rcClipBlock.bottom = ptOrigin.y + _font_extent.cy;

				draw.draw_text(ptOrigin.x, ptOrigin.y, rcClipBlock,
				               std::string_view(_expand_buf.c_str(), nDrawBytes),
				               f, text_color, bg_color);
			}

			ptOrigin.x += _font_extent.cx * nCodepoints;
		}
	}

	void draw_line(pf::draw_context& draw, pf::ipoint& ptOrigin, const pf::irect& rcClip, style nColorIndex,
	               const std::string_view pszChars, const int nOffset, const int nCount,
	               const text_location& ptTextPos,
	               const pf::font& f, const pf::color_t text_color, const pf::color_t bg_color) const
	{
		if (nCount > 0)
		{
			const auto sel = _doc->selection();
			auto nSelBegin = 0, nSelEnd = 0;

			if (sel._start.y > ptTextPos.y)
			{
				nSelBegin = nCount;
			}
			else if (sel._start.y == ptTextPos.y)
			{
				nSelBegin = std::clamp(sel._start.x - ptTextPos.x, 0, nCount);
			}
			if (sel._end.y > ptTextPos.y)
			{
				nSelEnd = nCount;
			}
			else if (sel._end.y == ptTextPos.y)
			{
				nSelEnd = std::clamp(sel._end.x - ptTextPos.x, 0, nCount);
			}

			assert(nSelBegin >= 0 && nSelBegin <= nCount);
			assert(nSelEnd >= 0 && nSelEnd <= nCount);
			assert(nSelBegin <= nSelEnd);

			//	Draw part of the text before selection
			if (nSelBegin > 0)
			{
				draw_line(draw, ptOrigin, rcClip, pszChars, nOffset, nSelBegin,
				          f, text_color, bg_color);
			}
			if (nSelBegin < nSelEnd)
			{
				draw_line(draw, ptOrigin, rcClip, pszChars, nOffset + nSelBegin, nSelEnd - nSelBegin,
				          f, style_to_color(style::sel_text), style_to_color(style::sel_bkgnd));
			}
			if (nSelEnd < nCount)
			{
				draw_line(draw, ptOrigin, rcClip, pszChars, nOffset + nSelEnd, nCount - nSelEnd,
				          f, text_color, bg_color);
			}
		}
	}

	void draw_wrapped_line(pf::draw_context& draw, const pf::irect& rc, const int lineIndex,
	                       const pf::font& f) const
	{
		const auto bg_color = line_bg_color(lineIndex);
		const auto& line = (*_doc)[lineIndex];
		const auto breaks = line_breaks(lineIndex);

		// Resolve the inherited cookie first: it scans backwards using _highlight_buf
		const auto cookie = highlight_cookie(lineIndex - 1);

		auto& line_view = _render_buf;
		line.render(line_view);
		const auto nLength = static_cast<int>(line_view.size());

		// Get syntax highlighting blocks for the full line
		const auto needed = static_cast<size_t>(nLength) * 2 + 128;
		if (_block_buf.size() < needed)
			_block_buf.resize(needed);
		auto* pBuf = _block_buf.data();
		auto nBlocks = 0;
		set_parse_cookie(lineIndex, highlight_line(cookie, line_view, pBuf, nBlocks));

		const int num_rows = static_cast<int>(breaks.size()) + 1;

		for (int row = 0; row < num_rows; row++)
		{
			const int row_start = row == 0 ? 0 : breaks[row - 1];
			const int row_end = row < static_cast<int>(breaks.size()) ? breaks[row] : nLength;
			const int row_y = rc.top + row * _font_extent.cy;

			if (row_y >= rc.bottom) break;

			const pf::irect row_rc(rc.left, row_y, rc.right, row_y + _font_extent.cy);
			pf::ipoint origin(row_rc.left, row_y);

			// Lambda to draw a segment clipped to this visual row
			auto draw_clipped = [&](const int seg_start, const int seg_end, const style color)
			{
				const int cs = std::max(seg_start, row_start);
				const int ce = std::min(seg_end, row_end);
				if (cs < ce)
				{
					draw_line(draw, origin, row_rc, color, line_view, cs, ce - cs,
					          text_location(cs, lineIndex), f, style_to_color(color), bg_color);
				}
			};

			if (nBlocks > 0)
			{
				draw_clipped(0, pBuf[0]._char_pos, style::normal_text);
				for (int i = 0; i < nBlocks; i++)
				{
					const int block_end = i + 1 < nBlocks ? pBuf[i + 1]._char_pos : nLength;
					draw_clipped(pBuf[i]._char_pos, block_end, pBuf[i]._color);
				}
			}
			else
			{
				draw_clipped(0, nLength, style::normal_text);
			}

			// Selection indicator after text on last visual row
			if (row == num_rows - 1 && _doc->is_inside_selection(text_location(nLength, lineIndex)))
			{
				const auto sel_x = std::max(origin.x, row_rc.left);
				draw.fill_solid_rect(sel_x, row_rc.top, _font_extent.cx, row_rc.height(),
				                     style_to_color(style::sel_bkgnd));
			}
		}
	}

	void draw_line(pf::draw_context& draw, const pf::irect& rc, const int lineIndex, const pf::font& f) const
	{
		const auto bg_color = line_bg_color(lineIndex);
		const auto& line = (*_doc)[lineIndex];

		// Dispatch to wrapped drawing if this line wraps to multiple visual rows
		if (_word_wrap && !_wrap_offsets.empty()
			&& lineIndex < static_cast<int>(_wrap_offsets.size()) - 1
			&& line_break_count(lineIndex) > 0)
		{
			draw_wrapped_line(draw, rc, lineIndex, f);
			return;
		}

		if (line.empty())
		{
			if (_doc->is_inside_selection(text_location(0, lineIndex)))
			{
				draw.fill_solid_rect(rc.left, rc.top, _font_extent.cx, rc.height(),
				                     style_to_color(style::sel_bkgnd));
			}
			return;
		}

		const auto nLength = static_cast<int>(line.size());
		const auto needed = static_cast<size_t>(nLength) * 2 + 128;
		if (_block_buf.size() < needed)
			_block_buf.resize(needed);
		auto* pBuf = _block_buf.data();
		auto nBlocks = 0;

		// Resolve the inherited cookie first: it scans backwards using _highlight_buf
		const auto cookie = highlight_cookie(lineIndex - 1);

		auto& line_view = _render_buf;
		line.render(line_view);
		set_parse_cookie(lineIndex, highlight_line(cookie, line_view, pBuf, nBlocks));

		pf::ipoint origin(rc.left - scroll_char() * _font_extent.cx, rc.top);

		if (nBlocks > 0)
		{
			assert(pBuf[0]._char_pos >= 0 && pBuf[0]._char_pos <= nLength);

			draw_line(draw, origin, rc, style::normal_text, line_view, 0, pBuf[0]._char_pos,
			          text_location(0, lineIndex),
			          f, style_to_color(style::normal_text), bg_color);

			for (auto i = 0; i < nBlocks - 1; i++)
			{
				assert(pBuf[i]._char_pos >= 0 && pBuf[i]._char_pos <= nLength);

				draw_line(draw, origin, rc, pBuf[i]._color, line_view,
				          pBuf[i]._char_pos, pBuf[i + 1]._char_pos - pBuf[i]._char_pos,
				          text_location(pBuf[i]._char_pos, lineIndex),
				          f, style_to_color(pBuf[i]._color), bg_color);
			}

			assert(pBuf[nBlocks - 1]._char_pos >= 0 && pBuf[nBlocks - 1]._char_pos <= nLength);

			draw_line(draw, origin, rc, pBuf[nBlocks - 1]._color, line_view,
			          pBuf[nBlocks - 1]._char_pos, nLength - pBuf[nBlocks - 1]._char_pos,
			          text_location(pBuf[nBlocks - 1]._char_pos, lineIndex),
			          f, style_to_color(pBuf[nBlocks - 1]._color), bg_color);
		}
		else
		{
			draw_line(draw, origin, rc, style::normal_text, line_view, 0, nLength,
			          text_location(0, lineIndex),
			          f, style_to_color(style::normal_text), bg_color);
		}

		// Draw selection indicator after end of text
		if (_doc->is_inside_selection(text_location(nLength, lineIndex)))
		{
			const auto sel_x = std::max(origin.x, rc.left);
			draw.fill_solid_rect(sel_x, rc.top, _font_extent.cx, rc.height(),
			                     style_to_color(style::sel_bkgnd));
		}
	}


	void draw_margin(pf::draw_context& draw, const pf::irect& rect, const int lineIndex,
	                 const pf::font& f) const
	{
		if (_sel_margin)
		{
			auto text_rect = rect;
			text_rect.right -= 4;
			// When wrapping, show line number only in the first visual row
			if (_word_wrap && rect.height() > _font_extent.cy)
				text_rect.bottom = text_rect.top + _font_extent.cy;
			const auto num = to_str(lineIndex + 1);
			const auto sz = draw.measure_text(num, f);
			const auto x = text_rect.right - sz.cx;
			const auto y = text_rect.top + (text_rect.height() - sz.cy) / 2;
			draw.draw_text(x, y, text_rect, num, f, pf::color_t{120, 120, 120},
			               style_to_color(style::sel_margin));
		}
	}

	void draw_view(pf::window_frame_ptr& window, pf::draw_context& draw) const override
	{
		const auto& styles = _events.styles();
		const auto rcClient = client_rect();
		const auto clip = draw.clip_rect();
		const auto line_count = static_cast<int>(_doc->size());
		const auto margin_w = margin_width();
		const auto pad_left = margin_w + _font_extent.cx;

		// Erase entire viewport
		draw.fill_solid_rect(rcClient, style_to_color(style::normal_bkgnd));

		// Paint margin background
		const pf::irect rcMargin(0, 0, margin_w, rcClient.bottom);
		draw.fill_solid_rect(rcMargin, style_to_color(_sel_margin ? style::sel_margin : style::normal_bkgnd));

		if (_focused && _doc)
		{
			const auto caret_pt = text_to_client(_doc->cursor_pos());
			const int band_pad = focus_band_padding();
			auto band = full_width_band_rect(caret_pt.y - band_pad, _font_extent.cy + band_pad * 2);
			band.top = std::max(band.top, text_top());
			band.bottom = std::min(band.bottom, rcClient.bottom);
			if (band.bottom > band.top)
				draw.fill_solid_rect(band, focus_band_color());
		}

		// Draw content lines — pixel-based positioning
		// Screen Y = line_offset(line) - _scroll_offset.y + text_top()
		int nCurrentLine;

		if (_word_wrap && !_wrap_line_y.empty())
		{
			// Find first document line with content potentially visible
			const int first_vrow = _font_extent.cy > 0
				                       ? std::max(0, (_scroll_offset.y - top_content_padding()) / _font_extent.cy)
				                       : 0;
			nCurrentLine = visual_row_to_line_index(first_vrow);
		}
		else
		{
			nCurrentLine = std::max(0, scroll_line() - 1);
		}
		int y = line_offset(nCurrentLine) - _scroll_offset.y + text_top();

		while (y < rcClient.bottom && nCurrentLine < line_count)
		{
			const auto nLineHeight = line_height(nCurrentLine);

			if (y + nLineHeight > clip.top && y < clip.bottom)
			{
				draw_margin(draw, pf::irect(0, y, margin_w, y + nLineHeight), nCurrentLine, styles.text_font);
				draw_line(draw, pf::irect(pad_left, y, rcClient.right, y + nLineHeight), nCurrentLine,
				          styles.text_font);
			}

			nCurrentLine++;
			y += nLineHeight;
		}

		const auto sb_rc = scrollbar_rect();
		_vscroll.draw(draw, sb_rc);
		_hscroll.draw(draw, sb_rc);
		draw_caret(draw);
		draw_drop_marker(draw);
		draw_message_bar(draw);
	}

	// Shows where a dragged selection will land
	void draw_drop_marker(pf::draw_context& draw) const
	{
		if (!_drag_text)
			return;

		const auto pt = text_to_client(_drop_location);
		const auto rc = client_rect();

		if (pt.y + _font_extent.cy > rc.top && pt.y < rc.bottom)
			draw.fill_solid_rect(pt.x, pt.y, 2, _font_extent.cy, style_to_color(style::md_bullet));
	}

	virtual void draw_caret(pf::draw_context& draw) const
	{
		if (!has_caret() || !_focused || !_caret.visible)
			return;

		const auto pos = _doc->cursor_pos();

		if (_doc->calc_offset(pos.y, pos.x) < scroll_char())
			return;

		const auto pt = text_to_client(pos);
		const auto caret_rect = pf::irect(pt.x, pt.y, pt.x + 2, pt.y + _font_extent.cy);
		const auto rc = client_rect();

		if (caret_rect.top < rc.bottom && caret_rect.bottom > rc.top)
			draw.fill_solid_rect(caret_rect, ui::text_color);
	}
};
