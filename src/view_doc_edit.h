// view_doc_edit.h — Editable document view: character input, undo/redo, editing commands

#pragma once

#include "view_doc.h"

class edit_doc_view : public doc_view
{
public:
	edit_doc_view(app_events& events) : doc_view(events)
	{
	}

	~edit_doc_view() override = default;

	[[nodiscard]] bool allows_text_drag() const override { return _doc && !_doc->is_read_only(); }

	std::vector<pf::menu_command> on_popup_menu(const pf::ipoint& client_pt) override
	{
		std::vector<pf::menu_command> items;

		// Spelling suggestions for the word under the cursor
		if (_doc->spell_check())
		{
			const auto text_pos = client_to_text(client_pt);
			const auto word_start = _doc->word_to_left(text_pos);
			const auto word_end = _doc->word_to_right(text_pos);

			if (word_start != word_end && word_start.y == word_end.y)
			{
				std::string line_text;
				(*_doc)[word_start.y].render(line_text);
				const auto word = line_text.substr(word_start.x, word_end.x - word_start.x);

				if (!word.empty() && !spell_check_word(word))
				{
					const text_selection word_sel(word_start, word_end);
					const auto suggestions = spell_suggest(word);

					for (const auto& suggestion : suggestions)
					{
						items.emplace_back(suggestion, 0,
						                   [this, word_sel, s = suggestion]
						                   {
							                   _doc->select(word_sel);
							                   undo_group ug(_doc);
							                   _doc->replace_text(ug, word_sel, s);
						                   });
					}

					if (!suggestions.empty())
						items.emplace_back(); // separator

					items.emplace_back("Add to Dictionary", 0,
					                   [this, w = word]
					                   {
						                   spell_add_word(w);
						                   _events.invalidate(invalid::doc);
					                   });
					items.emplace_back(); // separator
				}
			}
		}

		// Edit commands
		items.push_back(_events.command_menu_item(command_id::edit_undo));
		items.push_back(_events.command_menu_item(command_id::edit_redo));
		items.emplace_back(); // separator
		items.push_back(_events.command_menu_item(command_id::edit_cut));
		items.push_back(_events.command_menu_item(command_id::edit_copy));
		items.push_back(_events.command_menu_item(command_id::edit_paste));
		items.push_back(_events.command_menu_item(command_id::edit_delete));
		items.emplace_back(); // separator
		items.push_back(_events.command_menu_item(command_id::edit_select_all));

		return items;
	}

protected:
	[[nodiscard]] bool can_cut_text() const override
	{
		return _doc->has_selection();
	}

	[[nodiscard]] bool can_paste_text() const override
	{
		return document::can_paste();
	}

	[[nodiscard]] bool can_delete_text() const override
	{
		return _doc->has_selection() || _doc->query_editable();
	}

	bool cut_text_to_clipboard() override
	{
		if (!_doc->has_selection())
			return false;
		return set_clipboard(_doc->edit_cut());
	}

	bool paste_text_from_clipboard() override
	{
		if (!document::can_paste())
			return false;
		_doc->edit_paste(clipboard_text());
		return true;
	}

	bool delete_selected_text() override
	{
		if (!_doc->query_editable())
			return false;
		_doc->edit_delete();
		return true;
	}

	void on_char(pf::window_frame_ptr& window, const char32_t c) override
	{
		if (window->is_key_down_async(pf::platform_key::LButton) ||
			window->is_key_down_async(pf::platform_key::RButton))
			return;

		if (!_doc->query_editable())
			return;

		if (c == pf::platform_key::Return)
		{
			undo_group ug(_doc);
			const auto pos = _doc->delete_text(ug, _doc->selection());
			_doc->select(_doc->insert_text(ug, pos, u8'\n'));
		}
		else if (c > 31 && c != 0x7F)
		{
			undo_group ug(_doc);
			const auto pos = _doc->delete_text(ug, _doc->selection());
			_doc->select(_doc->insert_text(ug, pos, pf::utf8_encode(c)));
		}
	}

	bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;
		const bool ctrl = window->is_key_down(pk::Control);
		const bool shift = window->is_key_down(pk::Shift);
		const bool alt = window->is_key_down(pk::Alt);

		// Only keys the command table does not claim reach here
		if (vk == pk::Back && !ctrl && !alt)
		{
			_doc->edit_delete_back();
			return true;
		}
		if (vk == pk::Back && ctrl)
		{
			_doc->move_word_left(true);
			if (_doc->has_selection())
				_doc->edit_delete();
			return true;
		}
		if (vk == pk::Tab && !shift)
		{
			_doc->edit_tab();
			return true;
		}
		if (vk == pk::Tab && shift)
		{
			_doc->edit_untab();
			return true;
		}

		return doc_view::on_key_down(window, vk);
	}
};
