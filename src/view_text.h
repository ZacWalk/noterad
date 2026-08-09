// view_text.h — Text view base: line rendering, selection, font metrics, message routing

#pragma once

#include "ui.h"
#include "view_base.h"
#include "document.h"

pf::color_t style_to_color(style style_index);

class text_view : public view_base
{
protected:
	app_events& _events;

	bool _focused = false;

	pf::isize _font_extent = {10, 10};
	int _screen_lines = 0;

	// --- Scroll position helpers (convert between pixels and line/char units) ---

	void set_scroll_char(const int x) { _scroll_offset.x = x * _font_extent.cx; }

public:
	[[nodiscard]] int scroll_line() const { return _scroll_offset.y / _font_extent.cy; }
	[[nodiscard]] int scroll_char() const { return _scroll_offset.x / _font_extent.cx; }

	[[nodiscard]] int message_bar_height() const
	{
		return _events.message_bar_text().empty() ? 0 : _font_extent.cy + _font_extent.cy / 2;
	}

	text_view(app_events& events) : _events(events)
	{
	}

	virtual void update_focus(pf::window_frame_ptr& window)
	{
		const bool focused = window->has_focus();
		if (_focused != focused)
		{
			_focused = focused;
			if (!_events.message_bar_text().empty()) _events.invalidate(invalid::windows);
		}
	}

	~text_view() override = default;

	// --- Line text access (implemented by derived views) ---
	virtual void render_line(int line_num, std::string& out) const = 0;

	// --- Text selection interface ---
	// Virtual methods for text selection, coordinating with document selection logic.
	[[nodiscard]] virtual text_selection current_selection() const = 0;
	virtual void set_selection(const text_selection& sel) = 0;
	[[nodiscard]] virtual bool has_current_selection() const = 0;
	[[nodiscard]] virtual bool can_copy_text() const { return has_current_selection(); }
	[[nodiscard]] virtual bool can_cut_text() const { return false; }
	[[nodiscard]] virtual bool can_paste_text() const { return false; }
	[[nodiscard]] virtual bool can_delete_text() const { return false; }

	virtual bool copy_text_to_clipboard()
	{
		const auto text = select_text();
		return !text.empty() && set_clipboard(text);
	}

	virtual bool cut_text_to_clipboard() { return false; }
	virtual bool paste_text_from_clipboard() { return false; }
	virtual bool delete_selected_text() { return false; }
	void select_all_text() { select_all(); }

	// --- App-level virtual interface ---
	// Provide no-op defaults so the app can call these uniformly on any view.
	[[nodiscard]] virtual bool word_wrap() const { return false; }

	virtual void set_word_wrap(bool enabled)
	{
	}

	virtual void toggle_word_wrap()
	{
	}

	virtual void layout()
	{
	}

	virtual void update_caret(pf::window_frame_ptr& window)
	{
	}

	virtual void stop_caret_blink(const pf::window_frame_ptr& window)
	{
	}

	virtual void recalc_horz_scrollbar()
	{
	}

	virtual void recalc_vert_scrollbar() = 0;

	void invalidate(const pf::window_frame_ptr& window, const pf::irect r = {}) const
	{
		if (r.width() > 0)
			window->invalidate_rect(r);
		else
			window->invalidate();
	}

	// --- frame_reactor interface ---

	uint32_t handle_message(pf::window_frame_ptr window, const pf::message_type msg,
	                        const uintptr_t wParam, const intptr_t lParam) override
	{
		using mt = pf::message_type;

		if (msg == mt::create) return on_create();
		if (msg == mt::destroy) return on_destroy();
		if (msg == mt::set_focus || msg == mt::kill_focus)
		{
			update_focus(window);
			return 0;
		}
		if (msg == mt::erase_background) return 1;

		return 0;
	}

	uint32_t handle_keyboard(pf::window_frame_ptr window, const pf::keyboard_message_type msg,
	                         const pf::keyboard_params& params) override
	{
		using kt = pf::keyboard_message_type;

		if (msg == kt::key_down)
		{
			(void)on_key_down(window, params.vk);
			return 0;
		}
		if (msg == kt::char_input)
		{
			on_char(window, params.ch);
			return 0;
		}

		return 0;
	}

	uint32_t handle_mouse(pf::window_frame_ptr window, const pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override
	{
		using mt = pf::mouse_message_type;

		if (msg == mt::set_cursor)
		{
			if (params.hit_test == 1 /*HTCLIENT*/)
			{
				window->set_cursor_shape(pf::cursor_shape::arrow);
				return 1;
			}
			return 0;
		}
		if (msg == mt::mouse_wheel)
		{
			if (params.control)
			{
				zoom(window, params.wheel_delta > 0 ? 2 : -2);
				return 0;
			}
			on_mouse_wheel(window, params.wheel_delta > 0 ? -2 : 2);
			return 0;
		}

		return 0;
	}

	void handle_paint(pf::window_frame_ptr& window, pf::draw_context& draw) override
	{
		draw_view(window, draw);
	}

	void handle_size(pf::window_frame_ptr& window, const pf::isize extent,
	                 pf::measure_context& measure) override
	{
		const auto& styles = _events.styles();

		_view_extent = extent;
		_font_extent = measure.measure_char(styles.text_font);
		_font_extent.cx = std::max(1, _font_extent.cx);
		_font_extent.cy = std::max(1, _font_extent.cy);
		_screen_lines = extent.cy / _font_extent.cy;
		_vscroll.set_dpi_scale(styles.dpi_scale);
		_events.invalidate(invalid::doc_scrollbar);
		window->invalidate();
	}

	void scroll_to_top()
	{
		_scroll_offset = {};
		_events.invalidate(invalid::windows);
	}

	void scroll_to_end()
	{
		_scroll_offset.y = max_scroll_y();
		_events.invalidate(invalid::windows);
	}

	// --- Clipboard ---

	std::string clipboard_text() const
	{
		return pf::platform_text_from_clipboard();
	}

	bool set_clipboard(const std::string_view text) const
	{
		return pf::platform_text_to_clipboard(text);
	}

	virtual void ensure_visible(pf::window_frame_ptr& window, const text_location& pt)
	{
		const int line_top = line_content_offset(pt.y);
		const int line_bottom = line_top + _font_extent.cy;
		const int visible_height = _view_extent.cy - text_top();

		if (line_bottom > _scroll_offset.y + visible_height)
			set_scroll_pixel(line_bottom - visible_height);
		else if (line_top < _scroll_offset.y)
			set_scroll_pixel(line_top);
	}

	virtual void invalidate_lines(pf::window_frame_ptr& window, int start, int end)
	{
		invalidate(window);
	}

	virtual void lines_changed(pf::window_frame_ptr& window, const int start, const int end)
	{
		invalidate_lines(window, start, end);
	}

protected:
	[[nodiscard]] pf::color_t focus_band_color() const
	{
		return ui::tool_wnd_clr.darken(12);
	}

	[[nodiscard]] int focus_band_padding() const
	{
		return std::max(1, _font_extent.cy / 6);
	}

	[[nodiscard]] pf::irect full_width_band_rect(const int top, const int height) const
	{
		return pf::irect(0, top, _view_extent.cx, top + height);
	}

	[[nodiscard]] int centered_text_top(const int top, const int height) const
	{
		return top + std::max(0, (height - _font_extent.cy) / 2);
	}

	[[nodiscard]] int centered_text_top(const pf::irect& rect) const
	{
		return centered_text_top(rect.top, rect.height());
	}

	template <typename AdvanceFn>
	static void calc_word_breaks_into(std::vector<int>& breaks, const std::string_view text, const int max_cols,
	                                  AdvanceFn&& char_advance)
	{
		breaks.clear();
		const auto len = static_cast<int>(text.size());
		int col = 0;
		int last_break_opportunity = -1;
		int col_at_break = 0;

		for (int i = 0; i < len; i++)
		{
			const auto advance = char_advance(i, col);

			if (text[i] == ' ' || text[i] == '\t')
			{
				last_break_opportunity = i + 1;
				col_at_break = col + advance;
			}

			if (col > 0 && col + advance > max_cols)
			{
				if (last_break_opportunity > (breaks.empty() ? 0 : breaks.back()))
				{
					breaks.push_back(last_break_opportunity);
					col = col - col_at_break + advance;
				}
				else
				{
					breaks.push_back(i);
					col = advance;
				}
				last_break_opportunity = -1;
				col_at_break = 0;
			}
			else
			{
				col += advance;
			}
		}
	}

	template <typename AdvanceFn>
	static std::vector<int> calc_word_breaks(const std::string_view text, const int max_cols,
	                                         AdvanceFn&& char_advance)
	{
		std::vector<int> breaks;
		calc_word_breaks_into(breaks, text, max_cols, std::forward<AdvanceFn>(char_advance));
		return breaks;
	}

	virtual void draw_view(pf::window_frame_ptr& window,
	                       pf::draw_context& draw) const = 0;

	virtual void on_char(pf::window_frame_ptr& window, const char32_t c)
	{
	}

	virtual void select_all() = 0;
	virtual std::string select_text() const = 0;

	// Returns true if the key was handled.
	virtual bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk)
	{
		namespace pk = pf::platform_key;
		const bool ctrl = window->is_key_down(pk::Control);
		const bool shift = window->is_key_down(pk::Shift);

		if (vk == pk::Escape)
		{
			_events.on_escape();
			return true;
		}

		if (ctrl && !shift)
		{
			if (vk == 'C' || vk == pk::Insert)
			{
				set_clipboard(select_text());
				return true;
			}
			if (vk == 'A')
			{
				select_all();
				return true;
			}
			if (vk == pk::Home)
			{
				scroll_to_top();
				return true;
			}
			if (vk == pk::End)
			{
				scroll_to_end();
				return true;
			}
			if (vk == 0xBB /*VK_OEM_PLUS*/ || vk == 0x6B /*VK_ADD*/)
			{
				zoom(window, 2);
				return true;
			}
			if (vk == 0xBD /*VK_OEM_MINUS*/ || vk == 0x6D /*VK_SUBTRACT*/)
			{
				zoom(window, -2);
				return true;
			}
		}

		// Basic scroll navigation (overridden by derived views with cursor-based navigation)
		if (!ctrl && !shift)
		{
			if (vk == pk::Up)
			{
				scroll_by(-1);
				return true;
			}
			if (vk == pk::Down)
			{
				scroll_by(1);
				return true;
			}
			if (vk == pk::Prior)
			{
				scroll_by(-_screen_lines);
				return true;
			}
			if (vk == pk::Next)
			{
				scroll_by(_screen_lines);
				return true;
			}
		}

		return false;
	}

	virtual uint32_t on_create() { return 0; }
	virtual uint32_t on_destroy() { return 0; }


	virtual void on_mouse_wheel(pf::window_frame_ptr& window, const int zDelta)
	{
		set_scroll_pixel(_scroll_offset.y + zDelta * _font_extent.cy);
	}

	virtual void zoom(const pf::window_frame_ptr& window, const int delta)
	{
		_events.on_zoom(delta, zoom_target::text);
	}

	void scroll_to_line(const int y)
	{
		const int target = line_content_offset(y);
		set_scroll_pixel(target);
	}

	// Content-space Y offset for a given line index (includes top padding)
	int line_content_offset(const int line) const
	{
		return _font_extent.cy + line * _font_extent.cy;
	}

	// Set scroll offset in pixels with clamping
	void set_scroll_pixel(const int new_y)
	{
		const int max_y = std::max(0, _content_extent.cy - (_view_extent.cy - text_top()));
		const int clamped = std::clamp(new_y, 0, max_y);

		if (_scroll_offset.y != clamped)
		{
			_scroll_offset.y = clamped;
			recalc_vert_scrollbar();
			_events.invalidate(invalid::windows);
		}
	}

	int text_top() const
	{
		return message_bar_height();
	}

	void draw_message_bar(pf::draw_context& draw) const
	{
		const auto text = _events.message_bar_text();
		if (text.empty()) return;

		const auto& styles = _events.styles();
		const auto rcClient = client_rect();
		const auto bar_h = message_bar_height();
		const auto pad_y = _font_extent.cy / 4;
		const auto bg = _focused ? ui::focus_handle_color : ui::handle_color;
		const pf::irect bar_rc(0, 0, rcClient.right, bar_h);
		const auto text_w = pf::utf8_codepoint_count(text) * _font_extent.cx;
		const auto text_x = (rcClient.right - text_w) / 2;

		draw.fill_solid_rect(bar_rc, bg);
		draw.draw_text(text_x, pad_y, bar_rc, text,
		               styles.text_font, ui::text_color, bg);
	}

	void scroll_by(const int delta)
	{
		set_scroll_pixel(_scroll_offset.y + delta * _font_extent.cy);
	}
};
