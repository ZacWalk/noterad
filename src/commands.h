// commands.h — Unified command system: command definitions, parsing, registration, help text

#pragma once

#include "app.h"
#include "platform.h"

class app_state;

struct command_def
{
	std::string description;
	std::string menu_text;
	int menu_id = 0;
	pf::key_binding accel;
	std::function<bool()> is_enabled;
	std::function<bool()> is_checked;
	std::function<void()> execute;
	pf::key_binding accel_alt; // second binding for the same command; the menu still shows accel

	command_def() = default;

	command_def(std::string in_description,
	            std::string in_menu_text,
	            const int in_menu_id,
	            const pf::key_binding in_accel,
	            std::function<bool()> in_is_enabled,
	            std::function<bool()> in_is_checked,
	            std::function<void()> in_execute,
	            const pf::key_binding in_accel_alt = {})
		: description(std::move(in_description)),
		  menu_text(std::move(in_menu_text)),
		  menu_id(in_menu_id),
		  accel(in_accel),
		  is_enabled(std::move(in_is_enabled)),
		  is_checked(std::move(in_is_checked)),
		  execute(std::move(in_execute)),
		  accel_alt(in_accel_alt)
	{
	}
};

enum class command_id : int
{
	none = 0,

	// File
	file_new = 1001,
	file_open,
	file_save,
	file_save_as,
	file_save_all,

	// Edit (menu)
	edit_undo,
	edit_redo,
	edit_cut,
	edit_copy,
	edit_paste,
	edit_delete,
	edit_select_all,
	edit_reformat,
	edit_sort_remove_duplicates,
	edit_calc_selection,

	// App
	app_about,
	app_exit,
	help_run_tests,

	edit_spell_check,
	edit_search_files,

	// View
	view_word_wrap,
	view_toggle_markdown,
	view_refresh_folder,
	view_next_result,
	view_prev_result,
	view_toggle_agent,
	agent_focus_input,
};

class commands
{
public:
	void set_commands(std::vector<command_def> cmds)
	{
		_defs = std::move(cmds);
	}

	[[nodiscard]] const std::vector<command_def>& defs() const { return _defs; }
	[[nodiscard]] const command_def* find_by_menu_id(int id) const;

private:
	std::vector<command_def> _defs;
};

index_item_ptr find_item_recursively(const index_item_ptr& item, const pf::file_path& path);
