// view_doc_readonly.h — Base for read-only document views: scroll-only navigation, no caret

#pragma once

#include "view_doc.h"

class read_only_doc_view : public doc_view
{
public:
	read_only_doc_view(app_events& events) : doc_view(events)
	{
	}

	~read_only_doc_view() override = default;

	[[nodiscard]] bool has_caret() const override { return false; }

	[[nodiscard]] bool allows_drag_selection() const override { return false; }

	void set_word_wrap(bool enabled) override
	{
	}

	void toggle_word_wrap() override
	{
	}

	void recalc_horz_scrollbar() override
	{
		_scroll_offset.x = 0;
		_hscroll.update(0, 0, 0);
	}

	void on_mouse_wheel(pf::window_frame_ptr& window, const int zDelta) override
	{
		if (!can_scroll()) return;
		set_scroll_pixel(_scroll_offset.y + zDelta * _font_extent.cy);
	}

	bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;

		// Read-only views scroll rather than move a caret, so Shift is ignored here
		if (vk == pk::Up)
		{
			wrap_scroll_by(-1);
			return true;
		}
		if (vk == pk::Down)
		{
			wrap_scroll_by(1);
			return true;
		}
		if (vk == pk::Prior)
		{
			wrap_scroll_by(-_screen_lines);
			return true;
		}
		if (vk == pk::Next)
		{
			wrap_scroll_by(_screen_lines);
			return true;
		}
		if (vk == pk::Home)
		{
			scroll_content_to_top();
			return true;
		}
		if (vk == pk::End)
		{
			scroll_content_to_end();
			return true;
		}

		// Skip doc_view's caret navigation; the base still handles escape and zoom
		return text_view::on_key_down(window, vk);
	}

protected:
	void scroll_content_to_top()
	{
		_scroll_offset = {};
		recalc_vert_scrollbar();
		_events.invalidate(invalid::windows);
	}

	void scroll_content_to_end()
	{
		set_scroll_pixel(std::max(0, _content_extent.cy - (_view_extent.cy - text_top())));
	}

	// Columns that fit in a given pixel width
	static int safe_cols(const int width, const int char_width)
	{
		return char_width > 0 ? std::max(1, width / char_width) : 1;
	}
};
