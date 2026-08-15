// view_base.h — Base class for all views: scrolling state, content extents, scrollbar widgets

#pragma once

#include "ui.h"

class view_base : public pf::frame_reactor
{
protected:
	custom_scrollbar _hscroll{custom_scrollbar::orientation::horizontal};
	custom_scrollbar _vscroll{custom_scrollbar::orientation::vertical};

	pf::isize _view_extent = {}; // size of viewable area in pixels (set by on_size)
	pf::isize _content_extent = {}; // total content size in pixels (set by subclasses)
	pf::ipoint _scroll_offset = {}; // current scroll offset in pixels

	void clamp_scroll_offset()
	{
		_scroll_offset.x = std::clamp(_scroll_offset.x, 0, std::max(0, _content_extent.cx - _view_extent.cx));
		_scroll_offset.y = std::clamp(_scroll_offset.y, 0, std::max(0, _content_extent.cy - _view_extent.cy));
	}

	pf::irect client_rect() const
	{
		return pf::irect(0, 0, _view_extent.cx, _view_extent.cy);
	}

	// Scrolling logic — operates on pixel offsets

	int max_scroll_y() const { return std::max(0, _content_extent.cy - _view_extent.cy); }

public:
	// Exposed so a test can assert what the scrollbar was told without drawing
	[[nodiscard]] const custom_scrollbar& vert_scrollbar() const { return _vscroll; }
};
