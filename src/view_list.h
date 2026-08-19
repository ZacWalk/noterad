// view_list.h — Base class for panel views: scrolling, selection, keyboard navigation, collapsible items

#pragma once

#include "ui.h"
#include "view_base.h"

struct list_view_item
{
	pf::irect bounds;

	std::string name;
	index_item_ptr source;

	int depth = 0;
	bool is_group = false;
	bool expanded = false;

	int line_number = -1;
	int line_match_pos = -1;
	int text_match_start = 0;
	int text_match_length = 0;
};

using list_view_item_ptr = std::shared_ptr<list_view_item>;

class list_view : public view_base
{
protected:
	app_events& _events;

	std::vector<list_view_item_ptr> _items;
	list_view_item_ptr _selected_item;
	list_view_item_ptr _hover_item;
	bool _focused = false;

	// Position of _selected_item; a hint, revalidated by selected_index()
	mutable int _selected_index = -1;

	pf::isize _font_extent = {10, 10};
	int _header_height = 0;


	// Virtual hooks for derived classes

	virtual void draw_header(pf::window_frame_ptr& window, pf::draw_context& dc, const pf::irect& header_rect)
	{
	}

	void draw_expand_icon(pf::draw_context& dc, const int cx, const int cy, const bool expanded) const
	{
		constexpr auto size = 4;

		if (expanded)
		{
			const pf::ipoint pts[] = {{cx - size, cy - size / 2}, {cx, cy + size / 2}, {cx + size, cy - size / 2}};
			dc.draw_lines(pts, ui::line_color);
		}
		else
		{
			const pf::ipoint pts[] = {{cx - size / 2, cy - size}, {cx + size / 2, cy}, {cx - size / 2, cy + size}};
			dc.draw_lines(pts, ui::line_color);
		}
	}

	void draw_item(pf::draw_context& dc, const list_view_item_ptr& item,
	               const pf::irect& bounds, const bool selected, const bool hovered)
	{
		const auto& styles = _events.styles();
		const auto indent = styles.padding_x + item->depth * styles.indent;

		const auto bg = selected
			                ? (_focused ? ui::focus_handle_color : ui::handle_color)
			                : (hovered ? ui::handle_hover_color : ui::tool_wnd_clr);

		auto font_spec = styles.list_font;

		if (item->is_group)
		{
			const auto icon_x = bounds.left + indent + 4;
			const auto icon_y = (bounds.top + bounds.bottom) / 2;
			draw_expand_icon(dc, icon_x, icon_y, item->expanded);

			auto text_bounds = bounds;
			text_bounds.left += indent + 14;
			text_bounds = text_bounds.inflate(-styles.padding_x, -styles.padding_y);
			dc.draw_text(text_bounds.left, text_bounds.top, text_bounds, item->name,
			             font_spec, ui::folder_text_color, bg);
		}
		else if (item->line_number >= 0)
		{
			auto text_bounds = bounds;
			text_bounds.left += indent + 4;
			text_bounds = text_bounds.inflate(-styles.padding_x, -styles.padding_y);

			const auto line_str = std::format("{}: ", item->line_number + 1);
			const auto line_sz = dc.measure_text(line_str, font_spec);

			auto line_rect = text_bounds;
			line_rect.right = line_rect.left + line_sz.cx;
			dc.draw_text(line_rect.left, line_rect.top, line_rect, line_str, font_spec,
			             ui::darker_text_color, bg);

			auto context_rect = text_bounds;
			context_rect.left += line_sz.cx;

			const auto avail_width = context_rect.width();
			const std::string_view name = item->name;
			const auto name_len = static_cast<int>(name.size());
			const auto full_width = dc.measure_text(name, font_spec).cx;

			std::string_view display_text = name;
			int display_match_start = item->text_match_start;
			bool ellipsis_prefix = false;
			bool ellipsis_suffix = false;

			if (full_width > avail_width && item->text_match_length > 0)
			{
				// Widen a window around the match until it fills the available width.
				// Width grows monotonically with the radius, so binary search it rather
				// than stepping a character at a time — this runs on every paint.
				const auto ellipsis_width = dc.measure_text("...", font_spec).cx;
				const int match_start = std::clamp(item->text_match_start, 0, name_len);
				const int match_end = std::clamp(match_start + item->text_match_length, match_start, name_len);

				int vis_start = match_start;
				int vis_end = match_end;

				const auto width_of_radius = [&](const int radius)
				{
					vis_start = std::max(0, match_start - radius);
					while (vis_start > 0 && pf::is_utf8_continuation(name[vis_start])) vis_start--;

					vis_end = std::min(name_len, match_end + radius);
					while (vis_end < name_len && pf::is_utf8_continuation(name[vis_end])) vis_end++;

					auto w = dc.measure_text(name.substr(vis_start, vis_end - vis_start), font_spec).cx;
					if (vis_start > 0) w += ellipsis_width;
					if (vis_end < name_len) w += ellipsis_width;
					return w;
				};

				int lo = 0;
				int hi = name_len;

				while (lo < hi)
				{
					const auto mid = lo + (hi - lo + 1) / 2;
					if (width_of_radius(mid) <= avail_width)
						lo = mid;
					else
						hi = mid - 1;
				}

				width_of_radius(lo);

				ellipsis_prefix = vis_start > 0;
				ellipsis_suffix = vis_end < name_len;
				display_text = name.substr(vis_start, vis_end - vis_start);
				display_match_start = match_start - vis_start;
			}

			const auto draw_segment = [&](pf::irect& seg, const std::string_view text, const pf::color_t seg_bg)
			{
				if (text.empty()) return;
				seg.right = std::min(seg.left + dc.measure_text(text, font_spec).cx, context_rect.right);
				dc.draw_text(seg.left, seg.top, seg, text, font_spec, ui::text_color, seg_bg);
				seg.left = seg.right;
			};

			auto seg_rect = context_rect;

			if (ellipsis_prefix)
				draw_segment(seg_rect, "...", bg);

			if (item->line_match_pos >= 0 && item->text_match_length > 0 &&
				display_match_start >= 0 && display_match_start < static_cast<int>(display_text.size()))
			{
				constexpr auto highlight_color = pf::color_t(220, 140, 0);
				const auto match_end = std::min(display_match_start + item->text_match_length,
				                                static_cast<int>(display_text.size()));

				draw_segment(seg_rect, display_text.substr(0, display_match_start), bg);
				draw_segment(seg_rect, display_text.substr(display_match_start, match_end - display_match_start),
				             highlight_color);
				draw_segment(seg_rect, display_text.substr(match_end), bg);
			}
			else
			{
				draw_segment(seg_rect, display_text, bg);
			}

			if (ellipsis_suffix)
				draw_segment(seg_rect, "...", bg);

			if (seg_rect.left < context_rect.right)
				dc.fill_solid_rect(seg_rect.left, context_rect.top,
				                   context_rect.right - seg_rect.left, context_rect.height(), bg);
		}
		else
		{
			auto text_bounds = bounds;
			text_bounds.left += indent + 4;
			text_bounds = text_bounds.inflate(-styles.padding_x, -styles.padding_y);

			auto text_color = ui::text_color;
			if (_events.is_path_modified(item->source))
				text_color = pf::color_t{255, 80, 80};

			const auto text_sz = dc.measure_text(item->name, font_spec);
			auto display_text = std::string_view(item->name);
			std::string ellipsized;
			if (text_sz.cx > text_bounds.width())
			{
				const auto ellipsis_sz = dc.measure_text("...", font_spec);
				const auto avail = text_bounds.width() - ellipsis_sz.cx;

				// Binary search the longest whole-codepoint prefix that fits
				int lo = 0;
				int hi = pf::utf8_codepoint_count(display_text);
				while (lo < hi)
				{
					const auto mid = lo + (hi - lo + 1) / 2;
					const auto bytes = pf::utf8_truncate(display_text, mid);
					if (dc.measure_text(display_text.substr(0, bytes), font_spec).cx <= avail)
						lo = mid;
					else
						hi = mid - 1;
				}

				ellipsized = std::string(display_text.substr(0, pf::utf8_truncate(display_text, lo)));
				ellipsized += "...";
				display_text = ellipsized;
			}
			dc.draw_text(text_bounds.left, text_bounds.top, text_bounds, display_text, font_spec, text_color, bg);
		}
	}

	virtual void on_item_selected(const pf::window_frame_ptr& window, const list_view_item_ptr& item, bool activated)
	{
	}

	virtual uint32_t on_timer(pf::window_frame_ptr& window, uint32_t id) { return 0; }

	virtual uint32_t on_char(pf::window_frame_ptr& window, char32_t ch) { return 0; }

	virtual uint32_t on_key_down(pf::window_frame_ptr& window, const unsigned int vk)
	{
		namespace pk = pf::platform_key;

		if (vk == pk::Down)
		{
			navigate_next(window, true);
			return 0;
		}

		if (vk == pk::Up)
		{
			navigate_next(window, false);
			return 0;
		}

		if (vk == pk::Return)
		{
			_events.set_focus(view_focus::text);
			return 0;
		}

		if (vk == pk::Escape)
		{
			_events.on_escape();
			return 0;
		}

		return 0;
	}

	virtual void update_focus(pf::window_frame_ptr& window)
	{
		const bool focused = window && window->has_focus();
		if (_focused != focused)
		{
			_focused = focused;
			window->invalidate();
		}
	}

	// The inline text field currently accepting input, if any (search box, rename box)
	virtual edit_box_widget* active_edit_box() { return nullptr; }

	virtual void on_edit_text_changed()
	{
	}

public:
	// --- Inline edit field operations, invoked by the application command table ---

	[[nodiscard]] bool has_active_edit_box() { return active_edit_box() != nullptr; }

	bool edit_select_all()
	{
		auto* e = active_edit_box();
		if (!e) return false;
		e->edit.select_all();
		return true;
	}

	[[nodiscard]] bool edit_can_copy()
	{
		auto* e = active_edit_box();
		return e && e->edit.has_selection();
	}

	bool edit_copy()
	{
		auto* e = active_edit_box();
		return e && e->edit.copy_to_clipboard();
	}

	bool edit_cut()
	{
		auto* e = active_edit_box();
		if (!e || !e->edit.cut_to_clipboard()) return false;
		on_edit_text_changed();
		return true;
	}

	bool edit_paste()
	{
		auto* e = active_edit_box();
		if (!e || !e->edit.paste_from_clipboard()) return false;
		on_edit_text_changed();
		return true;
	}

	bool edit_delete()
	{
		auto* e = active_edit_box();
		if (!e || !e->edit.delete_forward()) return false;
		on_edit_text_changed();
		return true;
	}

	list_view(app_events& events) : _events(events)
	{
	}

	[[nodiscard]] list_view_item_ptr selected_item() const
	{
		return _selected_item;
	}

	// O(1) while the hint holds, falling back to a scan after the list is rebuilt
	[[nodiscard]] int selected_index() const
	{
		if (!_selected_item)
			return -1;

		if (_selected_index >= 0 && _selected_index < static_cast<int>(_items.size())
			&& _items[_selected_index] == _selected_item)
			return _selected_index;

		for (int i = 0; i < static_cast<int>(_items.size()); i++)
		{
			if (_items[i] == _selected_item)
			{
				_selected_index = i;
				return i;
			}
		}

		_selected_index = -1;
		return -1;
	}

	void set_selected(const int index)
	{
		_selected_index = index;
		_selected_item = index >= 0 && index < static_cast<int>(_items.size()) ? _items[index] : nullptr;
	}

	void navigate_next(const pf::window_frame_ptr& window, const bool forward, const bool skip_groups = false)
	{
		if (_items.empty()) return;

		const auto count = static_cast<int>(_items.size());
		const auto current = selected_index();
		const auto step = forward ? 1 : -1;

		auto next = current < 0 ? (forward ? 0 : count - 1) : current + step;

		while (next >= 0 && next < count && skip_groups && _items[next]->is_group)
			next += step;

		if (next < 0 || next >= count)
			return;

		set_selected(next);
		ensure_visible(window, _selected_item);
		window->invalidate();
		on_item_selected(window, _selected_item, false);
	}

	[[nodiscard]] int scroll_y() const { return _scroll_offset.y; }

	~list_view() override = default;

	uint32_t handle_message(pf::window_frame_ptr window, const pf::message_type msg,
	                        const pf::message_params& params) override
	{
		using mt = pf::message_type;

		if (msg == mt::create) return 0;
		if (msg == mt::erase_background) return 1;
		if (msg == mt::timer) return on_timer(window, params.timer_id);
		if (msg == mt::set_focus || msg == mt::kill_focus)
		{
			update_focus(window);
			return 0;
		}
		return 0;
	}

	uint32_t handle_keyboard(pf::window_frame_ptr window, const pf::keyboard_message_type msg,
	                         const pf::keyboard_params& params) override
	{
		using kt = pf::keyboard_message_type;

		if (msg == kt::char_input) return on_char(window, params.ch);
		if (msg == kt::key_down) return on_key_down(window, params.vk);
		return 0;
	}

	uint32_t handle_mouse(const pf::window_frame_ptr window, const pf::mouse_message_type msg,
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
		if (msg == mt::left_button_down)
			return on_left_button_down(window, params.point);
		if (msg == mt::left_button_up) return on_left_button_up(window);
		if (msg == mt::left_button_dbl_clk)
			return 0; // ignore double-click; single click already handled it
		if (msg == mt::mouse_move)
			return on_mouse_move(window, params.point);
		if (msg == mt::mouse_leave) return on_mouse_leave(window);
		if (msg == mt::mouse_wheel)
		{
			if (params.control)
			{
				zoom(window, params.wheel_delta > 0 ? 1 : -1);
				return 0;
			}
			scroll_to(window, _scroll_offset.y - params.wheel_delta / 2);
			return 0;
		}
		return 0;
	}

	void handle_size(pf::window_frame_ptr& window, const pf::isize extent,
	                 pf::measure_context& measure) override
	{
		_view_extent = extent;
		_vscroll.set_dpi_scale(_events.styles().dpi_scale);
		layout_list(measure);
		clamp_scroll_offset();
		window->invalidate();
	}

	void layout_list(const pf::measure_context& measure)
	{
		const auto& styles = _events.styles();
		const auto font_spec = styles.list_font;
		_font_extent = measure.measure_char(font_spec);
		layout_list(styles);
	}

	void layout_list()
	{
		layout_list(_events.styles());
	}

	void layout_list(const view_styles& styles)
	{
		int y = _header_height + styles.list_top_pad;
		const auto single_line_height = _font_extent.cy;
		const auto item_height = single_line_height + styles.padding_y * 2;

		for (const auto& i : _items)
		{
			pf::irect bounds;
			bounds.left = 0;
			bounds.top = y;
			bounds.right = _view_extent.cx;
			bounds.bottom = y + item_height;

			i->bounds = bounds;
			y += item_height;
		}

		_content_extent.cy = y + styles.list_scroll_pad;
	}

	void handle_paint(pf::window_frame_ptr& window, pf::draw_context& dc) override
	{
		const auto r = client_rect();
		dc.fill_solid_rect(r, ui::tool_wnd_clr);

		const auto hh = _header_height;
		if (hh > 0)
		{
			const pf::irect header_rect = {r.left, r.top, r.right, r.top + hh};
			draw_header(window, dc, header_rect);
		}

		const auto items_top = r.top + hh;

		int first_visible = 0;
		{
			int lo = 0, hi = static_cast<int>(_items.size()) - 1;
			while (lo <= hi)
			{
				const int mid = lo + (hi - lo) / 2;
				if (_items[mid]->bounds.bottom <= _scroll_offset.y)
					lo = mid + 1;
				else
					hi = mid - 1;
			}
			first_visible = lo;
		}

		for (int idx = first_visible; idx < static_cast<int>(_items.size()); idx++)
		{
			const auto& i = _items[idx];
			auto bounds = i->bounds.offset(0, -_scroll_offset.y);

			if (bounds.top >= r.bottom)
				break;
			if (bounds.bottom <= items_top)
				continue;
			if (bounds.top < items_top)
				bounds.top = items_top;

			const bool selected = i == _selected_item;
			const bool hovered = i == _hover_item;

			if (hovered)
				dc.fill_solid_rect(bounds, ui::handle_hover_color);
			if (selected)
				dc.fill_solid_rect(bounds, _focused ? ui::focus_handle_color : ui::handle_color);

			draw_item(dc, i, bounds, selected, hovered);
		}

		auto scrollbar_rect = r;
		scrollbar_rect.top = items_top;
		_vscroll.update(_content_extent.cy, _view_extent.cy - hh, _scroll_offset.y);
		_vscroll.draw(dc, scrollbar_rect);
	}

	bool can_scroll() const
	{
		return _content_extent.cy > _view_extent.cy;
	}

	list_view_item_ptr selection_from_point(const pf::ipoint& pt) const
	{
		if (_items.empty()) return nullptr;

		int lo = 0;
		int hi = static_cast<int>(_items.size()) - 1;

		while (lo <= hi)
		{
			const int mid = lo + (hi - lo) / 2;
			const auto& b = _items[mid]->bounds;

			if (pt.y < b.top)
				hi = mid - 1;
			else if (pt.y >= b.bottom)
				lo = mid + 1;
			else
				return _items[mid];
		}

		return nullptr;
	}

	void set_hover(const pf::window_frame_ptr& window, const list_view_item_ptr& h)
	{
		if (_hover_item != h)
		{
			_hover_item = h;
			window->invalidate();
		}
	}

	void select_list_item(const pf::window_frame_ptr& window, const list_view_item_ptr& item,
	                      const bool activated = true)
	{
		for (int i = 0; i < static_cast<int>(_items.size()); i++)
		{
			if (_items[i] == item)
			{
				set_selected(i);
				ensure_visible(window, item);
				window->invalidate();
				on_item_selected(window, item, activated);
				return;
			}
		}
	}

	uint32_t on_left_button_down(const pf::window_frame_ptr& window, const pf::ipoint& point)
	{
		window->set_focus();

		const auto hh = _header_height;
		if (point.y < hh)
			return 0;

		const auto rc = pf::irect(0, hh, _view_extent.cx, _view_extent.cy);

		if (!_vscroll.handle_mouse(pf::mouse_message_type::left_button_down, point, rc, window,
		                           [this, &window](const int pos) { scroll_to(window, pos); }))
		{
			const auto scroll_pt = pf::ipoint(point.x, point.y + _scroll_offset.y);
			const auto i = selection_from_point(scroll_pt);

			if (i)
			{
				set_hover(window, i);
				select_list_item(window, i);
			}

			window->invalidate();
		}

		return 0;
	}

	uint32_t on_mouse_move(const pf::window_frame_ptr& window, const pf::ipoint& point)
	{
		const auto hh = _header_height;
		const auto rc = pf::irect(0, hh, _view_extent.cx, _view_extent.cy);

		_vscroll.handle_mouse(pf::mouse_message_type::mouse_move, point, rc, window, [this, &window](const int pos)
		{
			scroll_to(window, pos);
		});

		if (!_vscroll._tracking)
		{
			if (point.y >= _header_height)
			{
				const auto scroll_pt = pf::ipoint(point.x, point.y + _scroll_offset.y);
				set_hover(window, selection_from_point(scroll_pt));
			}
			else
			{
				set_hover(window, nullptr);
			}
		}
		return 0;
	}

	uint32_t on_left_button_up(const pf::window_frame_ptr& window)
	{
		_vscroll.handle_mouse(pf::mouse_message_type::left_button_up, {}, {}, window,
		                      [this, &window](const int pos) { scroll_to(window, pos); });
		return 0;
	}

	uint32_t on_mouse_leave(const pf::window_frame_ptr& window)
	{
		_hover_item = nullptr;
		_vscroll.handle_mouse(pf::mouse_message_type::mouse_leave, {}, {}, window);
		window->invalidate();
		return 0;
	}

	void scroll_to(const pf::window_frame_ptr& window, int offset)
	{
		offset = std::clamp(offset, 0, std::max(0, _content_extent.cy - _view_extent.cy));

		if (_scroll_offset.y != offset)
		{
			_scroll_offset.y = offset;
			window->invalidate();
		}
	}

	void zoom(const pf::window_frame_ptr& window, const int delta)
	{
		_events.on_zoom(delta, zoom_target::list);
	}

	void ensure_visible(const pf::window_frame_ptr& window, const list_view_item_ptr& item)
	{
		if (!item) return;

		const auto hh = _header_height;
		const auto visible_top = _scroll_offset.y + hh;
		const auto visible_bottom = _scroll_offset.y + _view_extent.cy;

		if (item->bounds.top < visible_top)
			scroll_to(window, item->bounds.top - hh);
		else if (item->bounds.bottom > visible_bottom)
			scroll_to(window, item->bounds.bottom - _view_extent.cy);
	}
};
