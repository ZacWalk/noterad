// view_agent.h — Agent pane: the session.md transcript. The prompt below it is agent_input_view.

#pragma once

#include "view_doc_readonly.h"
#include "agent_session.h"

class agent_view final : public read_only_doc_view
{
public:
	// Raised when a click lands on one of the pending question's options
	std::function<void(size_t)> on_answer;

	// Raised when a character is typed at the transcript, which belongs in the prompt instead
	std::function<void(char32_t)> on_type;

	explicit agent_view(app_events& events) : read_only_doc_view(events)
	{
	}

	[[nodiscard]] pf::font body_font() const override { return _events.styles().agent_font; }

	[[nodiscard]] std::string_view status_text() const override { return _events.agent_status_text(); }

	// True when the newest content is on screen, so streaming should keep following it
	[[nodiscard]] bool at_bottom() const
	{
		const auto max_y = std::max(0, _content_extent.cy - (_view_extent.cy - text_top()));
		return _scroll_offset.y >= max_y - _font_extent.cy;
	}

	void scroll_to_end() { scroll_content_to_end(); }

	// The transcript is swapped when the root folder changes, and nothing raises a size change
	// for that, so the scrollbar would otherwise still describe the document it replaced
	void set_document(const document_ptr& d) override
	{
		read_only_doc_view::set_document(d);
		layout();
		recalc_vert_scrollbar();
	}

	void update_focus(pf::window_frame_ptr& window) override
	{
		read_only_doc_view::update_focus(window);
		window->invalidate();
	}

	void zoom(const pf::window_frame_ptr& window, const int delta) override
	{
		_events.on_zoom(delta, zoom_target::agent);
	}

	// Typing at the transcript means typing at the agent, so the prompt takes it
	void on_char(pf::window_frame_ptr& window, const char32_t ch) override
	{
		if (ch >= U' ' && on_type)
			on_type(ch);
	}

	bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		if (vk == pf::platform_key::Escape)
		{
			_events.set_focus(view_focus::text);
			return true;
		}

		return read_only_doc_view::on_key_down(window, vk);
	}

	uint32_t handle_mouse(pf::window_frame_ptr window, const pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override
	{
		// The scrollbar overlays the text, so a drag there must not count as an answer
		if (msg == pf::mouse_message_type::left_button_down
			&& !_vscroll.hit_test(params.point, scrollbar_rect())
			&& answer_at(params.point))
		{
			window->set_focus();
			return 0;
		}

		return read_only_doc_view::handle_mouse(window, msg, params);
	}

	// The pending question is always the last one, so an old block cannot be answered twice
	[[nodiscard]] bool answer_at(const pf::ipoint& point)
	{
		if (!_doc || !on_answer)
			return false;

		const auto line = client_to_line(point);

		if (line < 0 || line >= static_cast<int>(_doc->size()))
			return false;

		const auto lines = agent_session::to_lines(_doc->str());
		const auto entries = agent_session::parse(lines);

		for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
		{
			if (entry->kind != agent_entry_kind::question)
				continue;

			for (size_t i = 0; i < entry->options.size(); ++i)
			{
				if (entry->options[i].line == line)
				{
					on_answer(i);
					return true;
				}
			}

			return false;
		}

		return false;
	}
};

using agent_view_ptr = std::shared_ptr<agent_view>;
