// view_agent.h — Agent pane: the session.md transcript above, an input box below

#pragma once

#include "view_doc_readonly.h"
#include "agent_session.h"

class agent_view final : public read_only_doc_view
{
public:
	static constexpr int max_input_rows = 6;
	static constexpr size_t max_history = 64;

	std::function<void(std::string)> on_submit;

	explicit agent_view(app_events& events) : read_only_doc_view(events)
	{
	}

	[[nodiscard]] pf::font body_font() const override { return _events.styles().agent_font; }

	[[nodiscard]] std::string_view status_text() const override { return _events.agent_status_text(); }

	[[nodiscard]] std::string_view input_text() const { return _input.edit.text; }

	void scroll_to_end() { scroll_content_to_end(); }

	void set_input_text(std::string text)
	{
		_input.edit.text = std::move(text);
		_input.edit.cursor_pos = static_cast<int>(_input.edit.text.size());
		_input.edit.sel_anchor = _input.edit.cursor_pos;
		_events.invalidate(invalid::agent_layout);
	}

	// Rows the input needs for its content, capped so it cannot swallow the transcript
	[[nodiscard]] int input_rows() const
	{
		const auto newlines = static_cast<int>(std::ranges::count(_input.edit.text, '\n'));
		return std::clamp(newlines + 1, 1, max_input_rows);
	}

	[[nodiscard]] int footer_height() const
	{
		const auto& styles = _events.styles();
		return input_rows() * _font_extent.cy + styles.edit_box_inner_pad * 2 + styles.edit_box_margin * 2;
	}

	void handle_size(pf::window_frame_ptr& window, const pf::isize extent,
	                 pf::measure_context& measure) override
	{
		// The base sees only the transcript area, so every scroll calculation excludes the input
		_window_extent = extent;

		auto transcript = extent;
		transcript.cy = std::max(_font_extent.cy, extent.cy - footer_height());

		read_only_doc_view::handle_size(window, transcript, measure);

		transcript.cy = std::max(_font_extent.cy, extent.cy - footer_height());
		_view_extent = transcript;
	}

	void handle_paint(pf::window_frame_ptr& window, pf::draw_context& draw) override
	{
		read_only_doc_view::handle_paint(window, draw);
		draw_input(window, draw);
	}

	void update_focus(pf::window_frame_ptr& window) override
	{
		_input.update_focus(window, window->has_focus());
		read_only_doc_view::update_focus(window);
	}

	void zoom(const pf::window_frame_ptr& window, const int delta) override
	{
		_events.on_zoom(delta, zoom_target::agent);
	}

	uint32_t handle_message(pf::window_frame_ptr window, const pf::message_type msg,
	                        const pf::message_params& params) override
	{
		if (msg == pf::message_type::timer && _input.on_timer(params.timer_id))
		{
			window->invalidate_rect(input_rect());
			return 0;
		}

		return read_only_doc_view::handle_message(window, msg, params);
	}

	void on_char(pf::window_frame_ptr& window, const char32_t ch) override
	{
		if (ch == U'\r' || ch == U'\n')
			return; // Enter is handled as a key so Shift can mean "new line"

		if (_input.on_char(window, ch))
			after_input_changed(window);
	}

	bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;
		const auto shift = window->is_key_down(pk::Shift);
		const auto control = window->is_key_down(pk::Control);

		// Escape only moves focus, so it is safe to press while the agent is working
		if (vk == pk::Escape)
		{
			_events.set_focus(view_focus::text);
			return true;
		}

		if (vk == pk::Return)
		{
			if (shift)
			{
				_input.edit.insert_at_cursor("\n");
				after_input_changed(window);
			}
			else
			{
				submit(window);
			}
			return true;
		}

		// The transcript keeps the keys that cannot mean anything in a one-line field
		if (vk == pk::Prior || vk == pk::Next || (control && (vk == pk::Home || vk == pk::End)))
			return read_only_doc_view::on_key_down(window, vk);

		if (vk == pk::Up || vk == pk::Down)
		{
			if (recall_history(window, vk == pk::Up))
				return true;
		}

		auto text_modified = false;

		if (_input.on_key_down(window, vk, text_modified))
		{
			if (text_modified)
				after_input_changed(window);
			else
				window->invalidate_rect(input_rect());

			return true;
		}

		return read_only_doc_view::on_key_down(window, vk);
	}

	uint32_t handle_mouse(pf::window_frame_ptr window, const pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override
	{
		// A click anywhere in the pane focuses it; the input is always the keyboard target
		if (msg == pf::mouse_message_type::left_button_down && params.point.y >= _view_extent.cy)
		{
			window->set_focus();
			return 0;
		}

		return read_only_doc_view::handle_mouse(window, msg, params);
	}

private:
	edit_box_widget _input;
	pf::isize _window_extent = {};
	std::vector<std::string> _history;
	int _history_pos = -1;
	std::string _draft;

	[[nodiscard]] pf::irect input_rect() const
	{
		const auto& styles = _events.styles();
		const auto m = styles.edit_box_margin;
		return {m, _view_extent.cy + m, std::max(m + 1, _window_extent.cx - m), _window_extent.cy - m};
	}

	void after_input_changed(const pf::window_frame_ptr& window)
	{
		_history_pos = -1;
		_events.invalidate(invalid::agent_layout);
		window->notify_size();
		window->invalidate();
	}

	void submit(const pf::window_frame_ptr& window)
	{
		auto text = _input.edit.text;

		if (text.find_first_not_of(" \t\r\n") == std::string::npos)
			return;

		remember(text);
		_input.edit.text.clear();
		_input.edit.cursor_pos = 0;
		_input.edit.sel_anchor = 0;
		after_input_changed(window);

		if (on_submit)
			on_submit(std::move(text));
	}

	void remember(const std::string& text)
	{
		std::erase(_history, text);
		_history.push_back(text);

		if (_history.size() > max_history)
			_history.erase(_history.begin());

		_history_pos = -1;
	}

	// Walks previous prompts, keeping whatever was being typed so it can be restored
	bool recall_history(const pf::window_frame_ptr& window, const bool older)
	{
		if (_history.empty())
			return false;

		const auto count = static_cast<int>(_history.size());

		if (older)
		{
			if (_history_pos < 0)
			{
				_draft = _input.edit.text;
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

			if (_history_pos + 1 < count)
			{
				++_history_pos;
			}
			else
			{
				_history_pos = -1;
				set_input_text(_draft);
				window->notify_size();
				window->invalidate();
				return true;
			}
		}

		const auto remembered = _history[_history_pos];
		const auto position = _history_pos;
		set_input_text(remembered);
		_history_pos = position;
		window->notify_size();
		window->invalidate();
		return true;
	}

	void draw_input(const pf::window_frame_ptr& window, pf::draw_context& draw) const
	{
		const auto& styles = _events.styles();
		const auto box = input_rect();

		const pf::irect footer(0, _view_extent.cy, _window_extent.cx, _window_extent.cy);
		draw.fill_solid_rect(footer, ui::tool_wnd_clr);

		constexpr auto bg = ui::tool_wnd_clr.darken(16);
		draw.fill_solid_rect(box, bg);

		const auto focused = window->has_focus();
		edit_box::draw_border(draw, box, focused, styles.dpi_scale);

		const auto pad = styles.edit_box_inner_pad;
		const auto text_x = box.left + pad;
		const auto font = body_font();
		const std::string_view text = _input.edit.text;

		if (text.empty())
		{
			draw.draw_text(text_x, box.top + pad, box, "Message the agent, or /help",
			               font, ui::handle_hover_color, bg);
			return;
		}

		auto y = box.top + pad;
		size_t start = 0;
		auto caret_x = text_x;
		auto caret_y = y;

		for (;;)
		{
			const auto newline = text.find('\n', start);
			const auto piece = text.substr(start, newline == std::string_view::npos
				                               ? std::string_view::npos
				                               : newline - start);

			draw.draw_text(text_x, y, box, piece, font, ui::text_color, bg);

			const auto cursor = static_cast<size_t>(_input.edit.cursor_pos);

			if (cursor >= start && cursor <= start + piece.size())
			{
				caret_y = y;
				caret_x = text_x + draw.measure_text(piece.substr(0, cursor - start), font).cx;
			}

			if (newline == std::string_view::npos)
				break;

			start = newline + 1;
			y += _font_extent.cy;
		}

		if (focused && _input.caret.visible)
		{
			const auto caret_w = std::max(1, static_cast<int>(2 * styles.dpi_scale));
			draw.fill_solid_rect(caret_x, caret_y, caret_w, _font_extent.cy, ui::text_color);
		}
	}
};

using agent_view_ptr = std::shared_ptr<agent_view>;
