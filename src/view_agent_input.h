// view_agent_input.h — Agent prompt box: the editor in miniature, grown to fit what you type

#pragma once

#include "view_doc_edit.h"

class agent_input_view final : public edit_doc_view
{
public:
	static constexpr int max_rows = 5;
	static constexpr size_t max_history = 64;

	std::function<void(std::string)> on_submit;

	explicit agent_input_view(app_events& events) : edit_doc_view(events)
	{
		_sel_margin = false;
		_word_wrap = true;
	}

	[[nodiscard]] pf::font body_font() const override { return _events.styles().agent_font; }

	// The prompt has no message bar; the transcript above carries the agent's status
	[[nodiscard]] std::string_view status_text() const override { return {}; }

	// Rows the text needs, capped so the prompt cannot swallow the transcript
	[[nodiscard]] int rows() const
	{
		return std::clamp(total_rows(), 1, max_rows);
	}

	[[nodiscard]] int desired_height() const
	{
		return rows() * _font_extent.cy + top_content_padding() + bottom_content_padding();
	}

	void zoom(const pf::window_frame_ptr& window, const int delta) override
	{
		_events.on_zoom(delta, zoom_target::agent);
	}

	// Lets the transcript hand over a keystroke that was typed at it
	void type(pf::window_frame_ptr& window, const char32_t ch) { on_char(window, ch); }

	void set_text(const std::string_view text)
	{
		if (!_doc)
			return;

		{
			undo_group ug(_doc);
			_doc->replace_text(ug, _doc->all(), text);
		}

		move_caret_to_end();
	}

	[[nodiscard]] std::string text() const { return _doc ? _doc->str() : std::string{}; }

	void handle_paint(pf::window_frame_ptr& window, pf::draw_context& draw) override
	{
		edit_doc_view::handle_paint(window, draw);

		const auto box = client_rect();

		// Only while unfocused: draw_text fills its rect, which would swallow the caret
		if (!_focused && _doc && _doc->size() == 1 && (*_doc)[0].empty())
		{
			constexpr std::string_view hint = "Message the agent, or /help";
			const auto y = top_content_padding();
			const auto width = draw.measure_text(hint, body_font()).cx;
			const pf::irect clip(text_left(), y, std::min(box.right, text_left() + width),
			                     y + _font_extent.cy);
			draw.draw_text(clip.left, y, clip, hint, body_font(),
			               ui::handle_hover_color, style_to_color(style::normal_bkgnd));
		}

		edit_box::draw_border(draw, box, window->has_focus(), _events.styles().dpi_scale);
	}

	void update_focus(pf::window_frame_ptr& window) override
	{
		edit_doc_view::update_focus(window);
		window->invalidate(); // the border and the placeholder both change with focus
	}

	// Enter is a key rather than a character, so Shift can mean "new line"
	void on_char(pf::window_frame_ptr& window, const char32_t c) override
	{
		if (c == U'\r' || c == U'\n')
			return;

		edit_doc_view::on_char(window, c);
	}

	bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;
		const auto shift = window->is_key_down(pk::Shift);

		// Escape only moves focus, so it is safe to press while the agent is working
		if (vk == pk::Escape)
		{
			_events.set_focus(view_focus::text);
			return true;
		}

		if (vk == pk::Return)
		{
			if (shift)
				edit_doc_view::on_char(window, U'\r');
			else
				submit();

			return true;
		}

		// History only takes over at the edges, so the caret can still cross a wrapped prompt
		if (vk == pk::Up && at_first_row())
			return recall_history(true);

		if (vk == pk::Down && at_last_row())
			return recall_history(false);

		return edit_doc_view::on_key_down(window, vk);
	}

private:
	std::vector<std::string> _history;
	int _history_pos = -1;
	std::string _draft;

	[[nodiscard]] int top_content_padding() const override
	{
		return std::max(1, _font_extent.cy / 4);
	}

	[[nodiscard]] int bottom_content_padding() const override
	{
		return std::max(1, _font_extent.cy / 4);
	}

	[[nodiscard]] int total_rows() const
	{
		if (!_doc)
			return 1;

		return _word_wrap && _total_visual_rows > 0 ? _total_visual_rows : static_cast<int>(_doc->size());
	}

	[[nodiscard]] int caret_row() const
	{
		const auto pt = _doc->cursor_pos();

		if (_word_wrap && pt.y >= 0 && pt.y < std::ssize(_wrap_line_y))
			return _wrap_line_y[pt.y] + char_to_sub_row(pt.y, pt.x);

		return pt.y;
	}

	[[nodiscard]] bool at_first_row() const
	{
		return _doc && caret_row() == 0;
	}

	[[nodiscard]] bool at_last_row() const
	{
		return _doc && caret_row() >= total_rows() - 1;
	}

	void move_caret_to_end() const
	{
		const auto last = std::max(0, static_cast<int>(_doc->size()) - 1);
		const auto len = static_cast<int>((*_doc)[last].size());
		_doc->select(text_selection(len, last, len, last));
	}

	void submit()
	{
		auto content = text();

		if (content.find_first_not_of(" \t\r\n") == std::string::npos)
			return;

		remember(content);
		set_text({});

		if (on_submit)
			on_submit(std::move(content));
	}

	void remember(const std::string& content)
	{
		std::erase(_history, content);
		_history.push_back(content);

		if (_history.size() > max_history)
			_history.erase(_history.begin());

		_history_pos = -1;
	}

	// Walks previous prompts, keeping whatever was being typed so it can be restored
	bool recall_history(const bool older)
	{
		if (_history.empty())
			return false;

		const auto count = static_cast<int>(_history.size());

		if (older)
		{
			if (_history_pos < 0)
			{
				_draft = text();
				_history_pos = count - 1;
			}
			else if (_history_pos > 0)
			{
				--_history_pos;
			}
			else
			{
				return true;
			}
		}
		else
		{
			if (_history_pos < 0)
				return false;

			if (_history_pos + 1 >= count)
			{
				_history_pos = -1;
				set_text(_draft);
				return true;
			}

			++_history_pos;
		}

		set_text(_history[_history_pos]);
		return true;
	}
};

using agent_input_view_ptr = std::shared_ptr<agent_input_view>;
