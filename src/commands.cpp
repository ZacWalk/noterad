// commands.cpp — Command system implementation

#include "pch.h"
#include "commands.h"

#include "app_state.h"
#include "calc.h"
#include "document.h"
#include "view_doc.h"
#include "view_text.h"

namespace
{
	command_result execute_file_new(app_state& app)
	{
		app.on_new();
		return {"New document created.", true};
	}

	command_result execute_file_open(app_state& app)
	{
		app.on_open();
		return {"Open dialog shown.", true};
	}

	command_result execute_file_save(app_state& app)
	{
		app.on_save();
		return {"File saved.", true};
	}

	command_result execute_file_save_as(app_state& app)
	{
		app.on_save_as();
		return {"Save As dialog shown.", true};
	}

	command_result execute_file_save_all(app_state& app)
	{
		app.save_all();
		return {"All files saved.", true};
	}

	command_result execute_app_exit(app_state& app)
	{
		app.on_close();
		return {"", true};
	}

	command_result execute_edit_undo(app_state& app)
	{
		if (!app.doc()->can_undo())
			return {"Nothing to undo.", false};
		app.doc()->edit_undo();
		return {"Undone.", true};
	}

	command_result execute_edit_redo(app_state& app)
	{
		if (!app.doc()->can_redo())
			return {"Nothing to redo.", false};
		app.doc()->edit_redo();
		return {"Redone.", true};
	}

	command_result execute_edit_cut(app_state& app)
	{
		const auto view = app.focused_text_view();
		if (!view || !view->cut_text_to_clipboard())
			return {"No selection to cut.", false};
		app.invalidate(invalid::doc);
		return {"Selection cut.", true};
	}

	command_result execute_edit_copy(const app_state& app)
	{
		if (!app.copy_current_focus_to_clipboard())
			return {"Nothing to copy.", false};
		return {"Copied.", true};
	}

	command_result execute_edit_paste(app_state& app)
	{
		const auto view = app.focused_text_view();
		if (!view || !view->paste_text_from_clipboard())
			return {"Nothing to paste.", false};
		app.invalidate(invalid::doc);
		return {"Pasted.", true};
	}

	command_result execute_edit_delete(app_state& app)
	{
		if (!app.delete_current_focus())
			return {"Nothing to delete.", false};
		app.invalidate(invalid::doc);
		return {"Deleted.", true};
	}

	command_result execute_edit_search_files(app_state& app)
	{
		app.toggle_search_mode();
		return {"Search mode toggled.", true};
	}

	command_result execute_edit_select_all(app_state& app)
	{
		const auto view = app.focused_text_view();
		if (!view)
			return {"No text view focused.", false};
		view->select_all_text();
		app.invalidate(invalid::doc);
		return {"All text selected.", true};
	}

	command_result execute_edit_reformat(app_state& app)
	{
		app.on_edit_reformat();
		return {"Document reformatted.", true};
	}

	command_result execute_edit_sort_remove_duplicates(app_state& app)
	{
		app.on_edit_remove_duplicates();
		return {"Sorted and removed duplicates.", true};
	}

	command_result execute_edit_calc_selection(app_state& app)
	{
		if (!app.doc()->has_selection())
			return {"No selection.", false};

		const auto text = app.doc()->copy();
		if (text.empty())
			return {"No selection.", false};

		calc_parser parser(text);
		const auto result = parser.parse();
		if (!result.has_value())
			return {"Invalid expression: " + parser.error(), false};

		const auto value = result.value();
		std::string result_text;
		if (std::isfinite(value)
			&& value >= static_cast<double>(std::numeric_limits<int64_t>::min())
			&& value <= static_cast<double>(std::numeric_limits<int64_t>::max())
			&& value == static_cast<int64_t>(value))
			result_text = std::to_string(static_cast<int64_t>(value));
		else
			result_text = std::to_string(value);

		const auto sel = app.doc()->selection();
		undo_group ug(app.doc());
		app.doc()->replace_text(ug, sel, result_text);
		app.invalidate(invalid::doc);
		return {"= " + result_text, true};
	}

	command_result execute_edit_spell_check(app_state& app)
	{
		app.doc()->toggle_spell_check();
		const auto on = app.doc()->spell_check();
		app.set_spell_check_mode(on ? spell_check_mode::enabled : spell_check_mode::disabled);
		return {on ? "Spell check enabled." : "Spell check disabled.", true};
	}

	command_result execute_view_word_wrap(const app_state& app)
	{
		app._doc_view->toggle_word_wrap();
		const auto on = app._doc_view->word_wrap();
		return {on ? "Word wrap enabled." : "Word wrap disabled.", true};
	}

	command_result execute_view_toggle_markdown(app_state& app)
	{
		app.toggle_markdown_view();
		const auto on = is_markdown(app.get_mode());
		return {on ? "Markdown preview on." : "Markdown preview off.", true};
	}

	command_result execute_view_refresh_folder(app_state& app)
	{
		app.on_refresh();
		return {"Folder refreshed.", true};
	}

	command_result execute_view_next_result(app_state& app)
	{
		app.on_navigate_next(true);
		return {"", true};
	}

	command_result execute_view_prev_result(app_state& app)
	{
		app.on_navigate_next(false);
		return {"", true};
	}

	command_result execute_help_run_tests(app_state& app)
	{
		app.on_run_tests();
		return {"Tests complete.", true};
	}

	command_result execute_app_about(app_state& app)
	{
		app.on_about();
		return {"", true};
	}

	command_result dispatch_command_handler(app_state& app, const app_command_handler handler)
	{
		switch (handler)
		{
		case app_command_handler::file_new:
			return execute_file_new(app);
		case app_command_handler::file_open:
			return execute_file_open(app);
		case app_command_handler::file_save:
			return execute_file_save(app);
		case app_command_handler::file_save_as:
			return execute_file_save_as(app);
		case app_command_handler::file_save_all:
			return execute_file_save_all(app);
		case app_command_handler::app_exit:
			return execute_app_exit(app);
		case app_command_handler::edit_undo:
			return execute_edit_undo(app);
		case app_command_handler::edit_redo:
			return execute_edit_redo(app);
		case app_command_handler::edit_cut:
			return execute_edit_cut(app);
		case app_command_handler::edit_copy:
			return execute_edit_copy(app);
		case app_command_handler::edit_paste:
			return execute_edit_paste(app);
		case app_command_handler::edit_delete:
			return execute_edit_delete(app);
		case app_command_handler::edit_search_files:
			return execute_edit_search_files(app);
		case app_command_handler::edit_select_all:
			return execute_edit_select_all(app);
		case app_command_handler::edit_reformat:
			return execute_edit_reformat(app);
		case app_command_handler::edit_sort_remove_duplicates:
			return execute_edit_sort_remove_duplicates(app);
		case app_command_handler::edit_calc_selection:
			return execute_edit_calc_selection(app);
		case app_command_handler::edit_spell_check:
			return execute_edit_spell_check(app);
		case app_command_handler::view_word_wrap:
			return execute_view_word_wrap(app);
		case app_command_handler::view_toggle_markdown:
			return execute_view_toggle_markdown(app);
		case app_command_handler::view_refresh_folder:
			return execute_view_refresh_folder(app);
		case app_command_handler::view_next_result:
			return execute_view_next_result(app);
		case app_command_handler::view_prev_result:
			return execute_view_prev_result(app);
		case app_command_handler::help_run_tests:
			return execute_help_run_tests(app);
		case app_command_handler::app_about:
			return execute_app_about(app);
		}

		return {"Unknown command handler.", false};
	}
}

std::function<command_result()> bind_command_handler(
	app_state& app, const app_command_handler handler)
{
	return [&app, handler]()
	{
		return dispatch_command_handler(app, handler);
	};
}

const command_def* commands::find_by_menu_id(const int id) const
{
	for (const auto& def : _defs)
		if (def.menu_id == id)
			return &def;
	return nullptr;
}

index_item_ptr find_item_recursively(const index_item_ptr& item, const pf::file_path& path)
{
	if (!item)
		return nullptr;
	if (item->path == path)
		return item;
	for (const auto& child : item->children)
	{
		if (const auto found = find_item_recursively(child, path))
			return found;
	}
	return nullptr;
}
