// app_commands.cpp — Command definitions and menu construction

#include "pch.h"

#include "app.h"
#include "calc.h"
#include "document.h"
#include "commands.h"
#include "view_doc.h"
#include "view_list.h"
#include "view_text.h"
#include "app_state.h"

pf::menu_command app_state::command_menu_item(const command_id id,
                                              const std::function<void()> action_override,
                                              std::function<bool()> is_enabled_override,
                                              std::function<bool()> is_checked_override,
                                              std::string text_override) const
{
	const auto* def = get_commands().find_by_menu_id(static_cast<int>(id));
	if (!def)
		return {};

	auto is_enabled = def->is_enabled;
	if (is_enabled_override)
	{
		if (is_enabled)
		{
			auto base_enabled = std::move(is_enabled);
			is_enabled = [base_enabled = std::move(base_enabled), is_enabled_override]()
			{
				return base_enabled() && is_enabled_override();
			};
		}
		else
		{
			is_enabled = std::move(is_enabled_override);
		}
	}

	auto is_checked = is_checked_override ? std::move(is_checked_override) : def->is_checked;
	auto action = action_override;
	if (!action)
	{
		action = [this, menu_id = def->menu_id]()
		{
			if (const auto* menu_def = get_commands().find_by_menu_id(menu_id))
				menu_def->execute();
		};
	}

	return {
		text_override.empty() ? def->menu_text : std::move(text_override),
		def->menu_id,
		std::move(action),
		std::move(is_enabled),
		std::move(is_checked),
		def->accel,
		def->accel_alt
	};
}


std::vector<command_def> app_state::make_commands()
{
	std::vector<command_def> defs = {
		// ── File ───────────────────────────────────────────────────────
		{
			"Create a new document",
			"&New", static_cast<int>(command_id::file_new), {'N', pf::key_mod::ctrl},
			nullptr, nullptr,
			[this] { on_new(); }
		},
		{
			"Open a file",
			"&Open...", static_cast<int>(command_id::file_open), {'O', pf::key_mod::ctrl},
			nullptr, nullptr,
			[this] { on_open(); }
		},
		{
			"Save the current file",
			"&Save", static_cast<int>(command_id::file_save), {'S', pf::key_mod::ctrl},
			[this] { return doc() && !doc()->is_read_only(); }, nullptr,
			[this] { on_save(); }
		},
		{
			"Save the current file as...",
			"Save &As...", static_cast<int>(command_id::file_save_as), {},
			[this] { return doc() && !doc()->is_read_only(); }, nullptr,
			[this] { on_save_as(); }
		},
		{
			"Save all modified files",
			"Save A&ll", static_cast<int>(command_id::file_save_all),
			{'S', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, nullptr,
			[this] { save_all(); }
		},
		{
			"Exit the application",
			"E&xit", static_cast<int>(command_id::app_exit), {},
			nullptr, nullptr,
			[this] { on_close(); }
		},

		// ── Edit ───────────────────────────────────────────────────────
		{
			"Undo the last edit",
			"&Undo", static_cast<int>(command_id::edit_undo), {'Z', pf::key_mod::ctrl},
			[this] { return can_edit_focused_document() && focused_document()->can_undo(); }, nullptr,
			[this]
			{
				const auto d = focused_document();

				if (!d->can_undo())
				{
					set_message("Nothing to undo.");
					return;
				}
				d->edit_undo();
			},
			{pf::platform_key::Back, pf::key_mod::alt}
		},
		{
			"Redo the last undone edit",
			"&Redo", static_cast<int>(command_id::edit_redo), {'Y', pf::key_mod::ctrl},
			[this] { return can_edit_focused_document() && focused_document()->can_redo(); }, nullptr,
			[this]
			{
				const auto d = focused_document();

				if (!d->can_redo())
				{
					set_message("Nothing to redo.");
					return;
				}
				d->edit_redo();
			}
		},
		{
			"Cut selection to clipboard",
			"Cu&t", static_cast<int>(command_id::edit_cut), {'X', pf::key_mod::ctrl},
			[this]
			{
				if (auto* const edit_owner = focused_edit_box_owner())
					return edit_owner->edit_can_copy();
				const auto view = focused_text_view();
				return view && view->can_cut_text();
			},
			nullptr,
			[this]
			{
				if (auto* const edit_owner = focused_edit_box_owner())
				{
					edit_owner->edit_cut();
					return;
				}
				const auto view = focused_text_view();
				if (view && view->cut_text_to_clipboard())
					invalidate(invalid::doc);
			},
			{pf::platform_key::Delete, pf::key_mod::shift}
		},
		{
			"Copy selection or selected path to clipboard",
			"&Copy", static_cast<int>(command_id::edit_copy), {'C', pf::key_mod::ctrl},
			[this]
			{
				return can_copy_current_focus();
			},
			nullptr,
			[this] { (void)copy_current_focus_to_clipboard(); },
			{pf::platform_key::Insert, pf::key_mod::ctrl}
		},
		{
			"Paste from clipboard",
			"&Paste", static_cast<int>(command_id::edit_paste), {'V', pf::key_mod::ctrl},
			[this]
			{
				if (inline_edit_has_focus())
					return document::can_paste();
				const auto view = focused_text_view();
				return view && view->can_paste_text();
			},
			nullptr,
			[this]
			{
				if (auto* const edit_owner = focused_edit_box_owner())
				{
					edit_owner->edit_paste();
					return;
				}
				const auto view = focused_text_view();
				if (view && view->paste_text_from_clipboard())
					invalidate(invalid::doc);
			},
			{pf::platform_key::Insert, pf::key_mod::shift}
		},
		{
			"Delete selection or selected file",
			"&Delete", static_cast<int>(command_id::edit_delete), {pf::platform_key::Delete, pf::key_mod::none},
			[this]
			{
				return can_delete_current_focus();
			},
			nullptr,
			[this]
			{
				if (delete_current_focus())
					invalidate(invalid::doc);
			}
		},
		{
			"Search in files",
			"Search in &Files", static_cast<int>(command_id::edit_search_files),
			{'F', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, [this] { return is_search(get_mode()); },
			[this] { toggle_search_mode(); }
		},
		{
			"Select all text",
			"Select &All", static_cast<int>(command_id::edit_select_all), {'A', pf::key_mod::ctrl},
			nullptr, nullptr,
			[this]
			{
				if (auto* const edit_owner = focused_edit_box_owner())
				{
					edit_owner->edit_select_all();
					invalidate(invalid::search_layout | invalid::files_layout);
					return;
				}
				if (const auto view = focused_text_view())
				{
					view->select_all_text();
					invalidate(invalid::doc);
				}
			}
		},
		{
			"Reformat JSON document",
			"&Reformat", static_cast<int>(command_id::edit_reformat), {'R', pf::key_mod::ctrl},
			[this] { return can_edit_document(); }, nullptr,
			[this]
			{
				on_edit_reformat();
				set_message("Document reformatted.");
			}
		},
		{
			"Sort lines and remove duplicates",
			"Sort && Remove Duplicates", static_cast<int>(command_id::edit_sort_remove_duplicates), {},
			[this] { return can_edit_document(); }, nullptr,
			[this]
			{
				on_edit_remove_duplicates();
				set_message("Sorted and removed duplicates.");
			}
		},
		{
			"Calculate selected expression",
			"&Calculate Selection", static_cast<int>(command_id::edit_calc_selection),
			{'E', pf::key_mod::ctrl},
			[this]
			{
				return can_edit_document() && doc()->has_selection();
			},
			nullptr,
			[this] { calc_selection(); }
		},
		{
			"Toggle spell check",
			"&Spell Check", static_cast<int>(command_id::edit_spell_check),
			{'P', pf::key_mod::ctrl | pf::key_mod::shift},
			[this] { return !inline_edit_has_focus() && is_edit_text(get_mode()); },
			[this] { return doc()->spell_check(); },
			[this]
			{
				doc()->toggle_spell_check();
				const auto on = doc()->spell_check();
				set_spell_check_mode(on ? spell_check_mode::enabled : spell_check_mode::disabled);
			}
		},

		// ── View ───────────────────────────────────────────────────────
		{
			"Toggle word wrap",
			"&Word Wrap", static_cast<int>(command_id::view_word_wrap), {'Z', pf::key_mod::alt},
			[this] { return is_edit_text(get_mode()); }, [this] { return word_wrap(); },
			[this] { toggle_word_wrap(); }
		},
		{
			"Toggle markdown preview",
			"&Markdown Preview", static_cast<int>(command_id::view_toggle_markdown), {'M', pf::key_mod::ctrl},
			nullptr, [this] { return is_markdown(get_mode()); },
			[this] { toggle_markdown_view(); }
		},
		{
			"Refresh the folder index or the current search",
			"&Refresh", static_cast<int>(command_id::view_refresh_folder), {pf::platform_key::F5, pf::key_mod::none},
			nullptr, nullptr,
			[this] { on_refresh_focused_panel(); }
		},
		{
			"Navigate to next search result",
			"&Next Result", static_cast<int>(command_id::view_next_result), {pf::platform_key::F8, pf::key_mod::none},
			[this] { return is_search(get_mode()); }, nullptr,
			[this] { on_navigate_next(true); },
			{pf::platform_key::F3, pf::key_mod::none}
		},
		{
			"Navigate to previous search result",
			"&Previous Result", static_cast<int>(command_id::view_prev_result),
			{pf::platform_key::F8, pf::key_mod::shift},
			[this] { return is_search(get_mode()); }, nullptr,
			[this] { on_navigate_next(false); }
		},
		{
			"Show or hide the agent panel",
			"&Agent Panel", static_cast<int>(command_id::view_toggle_agent),
			{'A', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, [this] { return _agent_visible; },
			[this] { toggle_agent_panel(); }
		},
		{
			"Type a message to the agent",
			"&Message Agent", static_cast<int>(command_id::agent_focus_input),
			{pf::platform_key::F4, pf::key_mod::none},
			nullptr, nullptr,
			[this] { focus_agent_input(); }
		},

		// ── Help ───────────────────────────────────────────────────────
		{
			"Run all tests",
			"Run &Tests", static_cast<int>(command_id::help_run_tests), {'T', pf::key_mod::ctrl},
			nullptr, nullptr,
			[this] { on_run_tests(); }
		},
		{
			"Show about / help",
			"&About", static_cast<int>(command_id::app_about), {pf::platform_key::F1, pf::key_mod::none},
			nullptr, nullptr,
			[this] { on_about(); }
		},
	};
	return defs;
}

void app_state::calc_selection()
{
	if (!doc()->has_selection())
	{
		set_message("No selection.");
		return;
	}

	const auto text = doc()->copy();
	if (text.empty())
	{
		set_message("No selection.");
		return;
	}

	calc_parser parser(text);
	const auto result = parser.parse();
	if (!result.has_value())
	{
		set_message("Invalid expression: " + parser.error());
		return;
	}

	const auto value = result.value();
	std::string result_text;
	if (std::isfinite(value)
		&& value >= static_cast<double>(std::numeric_limits<int64_t>::min())
		&& value <= static_cast<double>(std::numeric_limits<int64_t>::max())
		&& value == static_cast<int64_t>(value))
		result_text = std::to_string(static_cast<int64_t>(value));
	else
		result_text = std::to_string(value);

	const auto sel = doc()->selection();
	undo_group ug(doc());
	doc()->replace_text(ug, sel, result_text);
	invalidate(invalid::doc);
	set_message("= " + result_text);
}


std::vector<pf::menu_command> app_state::build_menu()
{
	using cid = command_id;

	auto sep = []() { return pf::menu_command{}; };
	auto recent_roots = build_recent_root_folder_menu();

	return {
		{
			"&File", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::file_new),
				command_menu_item(cid::file_open),
				{"Open Recent &Root Folder", 0, nullptr, nullptr, nullptr, std::move(recent_roots)},
				sep(),
				command_menu_item(cid::file_save),
				command_menu_item(cid::file_save_as),
				command_menu_item(cid::file_save_all),
				sep(),
				command_menu_item(cid::app_exit),
			}
		},
		{
			"&Edit", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::edit_undo),
				command_menu_item(cid::edit_redo),
				sep(),
				command_menu_item(cid::edit_cut),
				command_menu_item(cid::edit_copy),
				command_menu_item(cid::edit_paste),
				command_menu_item(cid::edit_delete),
				sep(),
				command_menu_item(cid::edit_search_files),
				command_menu_item(cid::edit_select_all),
				sep(),
				command_menu_item(cid::edit_reformat),
				command_menu_item(cid::edit_sort_remove_duplicates),
				command_menu_item(cid::edit_calc_selection),
				sep(),
				command_menu_item(cid::edit_spell_check),
			}
		},
		{
			"&View", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::view_word_wrap),
				command_menu_item(cid::view_toggle_markdown),
				sep(),
				command_menu_item(cid::view_toggle_agent),
				command_menu_item(cid::agent_focus_input),
				sep(),
				command_menu_item(cid::view_refresh_folder),
				sep(),
				command_menu_item(cid::view_next_result),
				command_menu_item(cid::view_prev_result),
			}
		},
		{
			"&Help", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::help_run_tests),
				command_menu_item(cid::app_about),
			}
		},
	};
}
