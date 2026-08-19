// app.cpp — Application logic: main window, menus, splitter, file I/O commands

#include "pch.h"

#include "ui.h"
#include "app.h"
#include "document.h"
#include "commands.h"

#include "view_list_files.h"
#include "view_list_search.h"
#include "view_text.h"
#include "view_doc_edit.h"
#include "view_doc_markdown.h"
#include "view_doc_hex.h"
#include "view_doc_csv.h"

#include "app_state.h"
#include "test.h"

std::string g_app_name = "Noterad";

extern std::string run_all_tests();
extern tests::run_result run_all_tests_result();

pf::color_t style_to_color(const style style_index)
{
	switch (style_index)
	{
	case style::white_space:
	case style::main_wnd_clr:
		return ui::main_wnd_clr;
	case style::tool_wnd_clr:
		return ui::tool_wnd_clr;
	case style::normal_bkgnd:
		return pf::color_t(30, 30, 30);
	case style::normal_text:
		return pf::color_t(222, 222, 222);
	case style::sel_margin:
		return pf::color_t(44, 44, 44);
	case style::code_preprocessor:
		return pf::color_t(133, 133, 211);
	case style::code_comment:
		return pf::color_t(128, 222, 128);
	case style::code_number:
	case style::code_string:
		return pf::color_t(244, 244, 144);
	case style::code_operator:
		return pf::color_t(128, 255, 128);
	case style::code_keyword:
		return pf::color_t(128, 128, 255);
	case style::sel_bkgnd:
		return pf::color_t(88, 88, 88);
	case style::sel_text:
		return pf::color_t(255, 255, 255);
	case style::error_bkgnd:
		return pf::color_t(128, 0, 0);
	case style::error_text:
		return pf::color_t(255, 100, 100);
	case style::md_heading1:
		return pf::color_t(100, 200, 255);
	case style::md_heading2:
		return pf::color_t(140, 180, 255);
	case style::md_heading3:
		return pf::color_t(180, 160, 255);
	case style::md_bold:
		return pf::color_t(255, 255, 255);
	case style::md_italic:
		return pf::color_t(180, 220, 180);
	case style::md_link_text:
		return pf::color_t(100, 180, 255);
	case style::md_link_url:
		return pf::color_t(120, 120, 120);
	case style::md_marker:
		return pf::color_t(80, 80, 80);
	case style::md_bullet:
		return pf::color_t(200, 200, 100);
	}
	return pf::color_t(222, 222, 222);
}

static std::string make_about_text(const commands& cmds)
{
	std::string text =
		"# Noterad\n"
		"\n"
		"*A lightweight text editor written in C++ by Zac Walker*\n"
		"\n"
		"## Keyboard Shortcuts\n"
		"\n";

	for (const auto& cmd : cmds.defs())
	{
		if (cmd.accel.empty())
			continue;

		auto key_text = pf::format_key_binding(cmd.accel);
		if (!cmd.accel_alt.empty())
			key_text += " or " + pf::format_key_binding(cmd.accel_alt);
		text += std::format("- **{}** {}\n", key_text, cmd.description);
	}

	text +=
		"\n"
		"*Hold Shift with navigation keys to extend selection.*\n";

	return text;
}

namespace
{
	view_content view_content_for_doc_type(const doc_type type)
	{
		switch (type)
		{
		case doc_type::hex:
			return view_content::hex;
		case doc_type::markdown:
			return view_content::markdown;
		case doc_type::csv:
			return view_content::csv;
		case doc_type::text:
			return view_content::edit_text;
		}

		return view_content::edit_text;
	}

	bool is_view_content_supported(const doc_type type, const view_content content)
	{
		switch (type)
		{
		case doc_type::hex:
			return content == view_content::hex;
		case doc_type::markdown:
			return content == view_content::edit_text || content == view_content::markdown;
		case doc_type::csv:
			return content == view_content::edit_text || content == view_content::csv;
		case doc_type::text:
			return content == view_content::edit_text || content == view_content::markdown;
		}

		return content == view_content::edit_text;
	}

	view_content saved_view_content_for_item(const index_item_ptr& item)
	{
		if (!item || !item->doc)
			return view_content::edit_text;

		const auto type = item->doc->get_doc_type();
		if (item->saved_view_content != view_content::none &&
			is_view_content_supported(type, item->saved_view_content))
			return item->saved_view_content;

		return view_content_for_doc_type(type);
	}

	bool compare_index_items(const index_item_ptr& lhs, const index_item_ptr& rhs)
	{
		if (lhs->is_folder != rhs->is_folder)
			return lhs->is_folder > rhs->is_folder;
		return pf::icmp(lhs->name, rhs->name) < 0;
	}

	void sort_index_children(const index_item_ptr& parent)
	{
		if (!parent)
			return;

		std::ranges::sort(parent->children, compare_index_items);
	}

	index_item_ptr find_parent_item(const index_item_ptr& root, const index_item_ptr& target)
	{
		if (!root || !target)
			return nullptr;

		for (const auto& child : root->children)
		{
			if (child == target)
				return root;
			if (child->is_folder)
			{
				if (auto parent = find_parent_item(child, target))
					return parent;
			}
		}

		return nullptr;
	}

	void add_child_sorted(const index_item_ptr& parent, const index_item_ptr& child)
	{
		if (!parent || !child)
			return;

		parent->children.push_back(child);
		sort_index_children(parent);
	}

	bool remove_child_recursive(const index_item_ptr& root, const index_item_ptr& target)
	{
		if (!root || !target)
			return false;

		auto& children = root->children;
		const auto it = std::ranges::find(children, target);
		if (it != children.end())
		{
			children.erase(it);
			return true;
		}

		for (const auto& child : children)
		{
			if (child->is_folder && remove_child_recursive(child, target))
				return true;
		}

		return false;
	}

	pf::file_path make_unique_child_path(const index_item_ptr& root, const pf::file_path& requested_path,
	                                     const bool check_file_system)
	{
		auto is_taken = [&](const pf::file_path& path)
		{
			if (root && find_item_recursively(root, path))
				return true;
			if (!check_file_system)
				return false;
			return path.exists() || pf::is_directory(path);
		};

		if (!is_taken(requested_path))
			return requested_path;

		const auto parent = requested_path.folder();
		const auto leaf = pf::file_path{requested_path.name()};
		const auto stem = leaf.without_extension();
		const auto extension = leaf.extension();

		for (int suffix = 2; suffix <= 10000; ++suffix)
		{
			const auto candidate_name = std::format("{}-{}", stem, suffix);
			const auto candidate = extension.empty()
				                       ? parent.combine(candidate_name)
				                       : parent.combine(candidate_name, extension);

			if (!is_taken(candidate))
				return candidate;
		}

		return requested_path;
	}

	doc_view_ptr create_doc_view_for_mode(app_state& app, const view_mode mode)
	{
		switch (view_content_of(mode))
		{
		case view_content::markdown:
			return std::make_shared<markdown_doc_view>(app);
		case view_content::hex:
			return std::make_shared<hex_doc_view>(app);
		case view_content::csv:
			return std::make_shared<csv_doc_view>(app);
		case view_content::edit_text:
			return std::make_shared<edit_doc_view>(app);
		}

		return std::make_shared<edit_doc_view>(app);
	}

	std::string view_message_text(const view_mode mode, const document_ptr& doc)
	{
		if (is_markdown(mode))
			return "Preview mode. Press Escape to edit.";
		if (is_csv(mode))
			return "CSV table view. Press Escape to edit.";
		if (doc && doc->is_truncated())
			return "File exceeds 2 MB and has been truncated. Read-only.";
		return {};
	}

	spell_check_mode parse_spell_check_mode(const std::string_view value)
	{
		if (pf::icmp(value, "1") == 0 || pf::icmp(value, "on") == 0 || pf::icmp(value, "enabled") == 0)
			return spell_check_mode::enabled;
		if (pf::icmp(value, "0") == 0 || pf::icmp(value, "off") == 0 || pf::icmp(value, "disabled") == 0)
			return spell_check_mode::disabled;
		return spell_check_mode::auto_detect;
	}

	std::string_view spell_check_mode_config_value(const spell_check_mode mode)
	{
		switch (mode)
		{
		case spell_check_mode::enabled:
			return "1";
		case spell_check_mode::disabled:
			return "0";
		case spell_check_mode::auto_detect:
		default:
			return "auto";
		}
	}

	std::string recent_root_folder_config_key(const size_t index)
	{
		return std::format("Folder{}", index + 1);
	}

	std::string recent_root_document_config_key(const size_t index)
	{
		return std::format("Document{}", index + 1);
	}

	constexpr int recent_root_folder_menu_id_base = 20000;

	std::string escape_menu_text(const std::string_view text)
	{
		return replace(std::string(text), "&", "&&");
	}

	void find_matches_in_line(std::vector<search_result>& results, const std::string_view line,
	                          const int line_number, const std::string_view text)
	{
		if (line.empty())
			return;

		size_t trim = 0;
		while (trim < line.length() && (line[trim] == u8' ' || line[trim] == u8'\t'))
			trim++;

		auto pos = find_in_text(line, text);
		while (pos != std::string_view::npos)
		{
			search_result item;
			item.line_text = line.substr(trim);
			item.line_number = line_number;
			item.line_match_pos = static_cast<int>(pos);
			item.text_match_start = pos >= trim ? static_cast<int>(pos - trim) : 0;
			item.text_match_length = static_cast<int>(text.length());
			results.push_back(std::move(item));

			const auto next_start = pos + text.length();
			if (next_start >= line.length())
				break;
			const auto next_pos = find_in_text(line.substr(next_start), text);
			if (next_pos == std::string_view::npos)
				break;
			pos = next_start + next_pos;
		}
	}

	std::vector<search_result> search_file_results(const app_state::search_input& input,
	                                               const std::string_view search_text)
	{
		if (search_text.empty())
			return {};
		if (is_binary_extension(input.path))
			return {};

		std::vector<search_result> results;

		if (input.has_snapshot)
		{
			int line_number = 0;
			for (const auto& line : input.lines)
			{
				find_matches_in_line(results, line, line_number, search_text);
				line_number++;
			}
			return results;
		}

		if (input.doc)
		{
			std::string line_text;
			for (int line_number = 0; line_number < static_cast<int>(input.doc->size()); line_number++)
			{
				(*input.doc)[line_number].render(line_text);
				find_matches_in_line(results, line_text, line_number, search_text);
			}
			return results;
		}

		const auto handle = pf::open_for_read(input.path);
		if (!handle)
			return {};

		const auto size = handle->size();
		if (size > app_state::max_search_file_size || size == 0)
			return {};

		const auto info = iterate_file_lines(handle, [&](const std::string& line, const int line_number)
		{
			find_matches_in_line(results, line, line_number, search_text);
		});

		if (info.enc == file_encoding::binary)
			return {};

		return results;
	}

	std::string clipboard_path_text(const pf::file_path& path, const int line_number = -1)
	{
		auto text = std::string(path.view());
		if (line_number >= 0)
			text += std::format(":{}", line_number + 1);
		return text;
	}

	bool is_reserved_device_name(const std::string_view name)
	{
		const auto stem = name.substr(0, name.find(u8'.'));

		for (const std::string_view reserved : {"CON", "PRN", "AUX", "NUL"})
			if (pf::icmp(stem, reserved) == 0)
				return true;

		return stem.size() == 4 && stem[3] >= u8'1' && stem[3] <= u8'9' &&
			(pf::icmp(stem.substr(0, 3), "COM") == 0 || pf::icmp(stem.substr(0, 3), "LPT") == 0);
	}

	// Rejects anything that could escape the containing folder or is illegal on Windows
	bool is_valid_item_name(const std::string_view name)
	{
		if (name.empty() || name.size() > 255)
			return false;
		if (name == "." || name == "..")
			return false;
		if (name.back() == u8'.' || name.back() == u8' ')
			return false;

		for (const auto c : name)
		{
			if (static_cast<unsigned char>(c) < 0x20)
				return false;
			if (c == u8'\\' || c == u8'/' || c == u8':' || c == u8'*' || c == u8'?' ||
				c == u8'"' || c == u8'<' || c == u8'>' || c == u8'|')
				return false;
		}

		return !is_reserved_device_name(name);
	}

	bool parse_int_value(const std::string_view text, int& out)
	{
		if (text.empty())
			return false;
		const auto end = text.data() + text.size();
		const auto result = std::from_chars(text.data(), end, out);
		return result.ec == std::errc{} && result.ptr == end;
	}

	bool parse_double_value(const std::string_view text, double& out)
	{
		if (text.empty())
			return false;
		const auto end = text.data() + text.size();
		const auto result = std::from_chars(text.data(), end, out);
		return result.ec == std::errc{} && result.ptr == end;
	}
}

app_state::app_state(async_scheduler_ptr scheduler) : _doc_view(std::make_shared<edit_doc_view>(*this)),
                                                      _files_view(std::make_shared<file_list_view>(*this)),
                                                      _search_view(std::make_shared<search_list_view>(*this)),
                                                      _scheduler(std::move(scheduler))
{
	_active_item = std::make_shared<index_item>();
	_active_item->doc = std::make_shared<document>(*this);
	_root_folder = std::make_shared<index_item>();
	_doc_view->set_document(active_item()->doc);
	get_commands().set_commands(make_commands());
}

void app_state::ensure_visible(const text_location& pt)
{
	_doc_view->ensure_visible(_doc_window, pt);
}

void app_state::open_path_and_select(const index_item_ptr& item, const int line, const int col,
                                     const int length)
{
	load_doc(item, [this, item, line, col, length]
	{
		const auto& d = item->doc;
		if (!d || active_item() != item || line < 0 || line >= static_cast<int>(d->size()))
			return;

		const auto line_len = static_cast<int>((*d)[line].size());
		const auto start = std::clamp(col, 0, line_len);
		const auto end = std::clamp(col + length, start, line_len);

		// Scrolling to the match needs metrics for the document just loaded, not the one it replaced
		_doc_view->layout();
		_doc_view->recalc_vert_scrollbar();

		d->select(text_selection(start, line, end, line));
		ensure_visible(text_location(end, line)); // select() is a no-op when re-opening the same match
		invalidate(invalid::doc | invalid::doc_caret);
	});
}

void app_state::invalidate_lines(const int start, const int end)
{
	_doc_view->invalidate_lines(_doc_window, start, end);
}

void app_state::lines_changed(const int start, const int end)
{
	_doc_view->lines_changed(_doc_window, start, end);
}

void app_state::on_navigate_next(const bool forward)
{
	const bool is_search_mode = is_search(get_mode());

	if (is_search_mode)
		_search_view->navigate_next(_list_window, forward, true);
	else
		_files_view->navigate_next(_list_window, forward);
}

void app_state::load_doc(const index_item_ptr& item, std::function<void()> on_loaded)
{
	auto d = item->doc;
	bool load_from_disk = true;

	if (d)
	{
		const auto current_time = pf::file_modified_time(item->path);
		const uint64_t disk_modified_time = d->disk_modified_time();
		const bool changed_on_disk = disk_modified_time > 1 && current_time != disk_modified_time;

		if (!changed_on_disk)
		{
			load_from_disk = false;
		}
		else if (d->is_modified())
		{
			// Only worth asking when there is something to lose
			const auto id = _app_window->message_box(
				"This file has been modified on disk. Do you want to reload it and lose your local changes?",
				g_app_name,
				pf::msg_box_style::yes_no | pf::msg_box_style::icon_question);

			load_from_disk = id == pf::msg_box_result::yes;
		}
	}
	else
	{
		auto encoding = is_binary_extension(item->path) ? file_encoding::binary : file_encoding::utf8;
		d = std::make_shared<document>(*this, item->path, 1, encoding);
		item->doc = d;
	}

	set_active_item(item);

	if (!load_from_disk)
	{
		if (on_loaded)
			on_loaded();
		return;
	}

	const auto generation = ++item->load_generation;
	const auto was_modified = d->is_modified();
	const auto path = item->path;

	_scheduler->run_async([t = shared_from_this(), item, path, generation, was_modified,
			on_loaded = std::move(on_loaded)]() mutable
		{
			auto lines = load_lines(path);

			t->_scheduler->run_ui([t, item, path, generation, was_modified, lines = std::move(lines),
					on_loaded = std::move(on_loaded)]() mutable
			{
				// Discard the read if a newer load started or the user edited while it was in flight
				if (item->load_generation != generation)
					return;
				if (!item->doc || (!was_modified && item->doc->is_modified()))
					return;

				item->doc->apply_loaded_data(path, std::move(lines));
				t->apply_spell_check_mode(item->doc);

				// Only switch view if this item is still the active one,
				// otherwise we'd override the user's current selection
				if (t->active_item() == item)
					t->set_active_item(item);

				if (on_loaded)
					on_loaded();
			});
		});
}

void app_state::load_doc(const pf::file_path& path)
{
	const auto item = find_item_recursively(root_item(), path);

	if (item)
	{
		load_doc(item);
	}
	else
	{
		// File is outside the current root folder — change root to the file's parent directory
		const auto new_root = path.folder();

		if (!prompt_save_all_modified())
			return; // user cancelled

		refresh_index(new_root, [this, path]
		{
			auto item = find_item_recursively(root_item(), path);

			if (!item)
			{
				const auto encoding = is_binary_extension(path) ? file_encoding::binary : file_encoding::utf8;
				auto d = std::make_shared<document>(*this, path, 1, encoding);
				item = std::make_shared<index_item>(path, std::string(path.name()), false, d);
				add_child_sorted(root_item(), item);
			}

			invalidate(invalid::files_layout | invalid::files_populate);
			load_doc(item);
		}, false);
	}
}

void app_state::set_active_item(const index_item_ptr& item)
{
	_active_item = item;
	apply_spell_check_mode(item ? item->doc : nullptr);
	if (item && root_item() && !root_item()->path.empty() && item->path.is_save_path())
		remember_root_document(root_item()->path, item->path);

	const auto is_search = ::is_search(get_mode());
	const auto content = saved_view_content_for_item(item);

	set_mode(make_view_mode(content, is_search));

	update_info_message();
	invalidate(invalid::app_title);
}

void app_state::update_info_message()
{
	_message_bar_text = view_message_text(get_mode(), doc());

	invalidate(invalid::doc);
}

void app_state::apply_spell_check_mode(const document_ptr& target_doc) const
{
	if (!target_doc)
		return;

	switch (_spell_check_mode)
	{
	case spell_check_mode::enabled:
		target_doc->set_spell_check(true);
		break;
	case spell_check_mode::disabled:
		target_doc->set_spell_check(false);
		break;
	case spell_check_mode::auto_detect:
	default:
		target_doc->set_spell_check(should_spell_check_path(target_doc->path()));
		break;
	}
}

void app_state::set_spell_check_mode(const spell_check_mode mode, const bool persist)
{
	_spell_check_mode = mode;
	apply_spell_check_mode(doc());
	if (mode == spell_check_mode::disabled)
		reset_spell_checker();
	if (persist)
		pf::config_write("View", "SpellCheck", spell_check_mode_config_value(mode));
}

void app_state::set_focus(const view_focus v)
{
	if (v == view_focus::list)
		_list_window->set_focus();
	else
		_doc_window->set_focus();
}

text_view_ptr app_state::focused_text_view() const
{
	if (_doc_window && _doc_window->has_focus())
		return std::static_pointer_cast<text_view>(_doc_view);
	return {};
}

bool app_state::list_has_focus() const
{
	return _list_window && _list_window->has_focus();
}

bool app_state::file_list_has_focus() const
{
	return list_has_focus() && !is_search(get_mode());
}

bool app_state::search_list_has_focus() const
{
	return list_has_focus() && is_search(get_mode());
}

// The search box, or an inline rename box in the file list
bool app_state::inline_edit_has_focus() const
{
	return focused_edit_box_owner() != nullptr;
}

bool app_state::can_edit_document() const
{
	return !inline_edit_has_focus() && is_edit_text(get_mode()) && _active_item && _active_item->doc &&
		!_active_item->doc->is_read_only();
}

list_view_item_ptr app_state::selected_file_list_item() const
{
	return _files_view ? _files_view->selected_item() : nullptr;
}

list_view_item_ptr app_state::selected_search_list_item() const
{
	return _search_view ? _search_view->selected_item() : nullptr;
}

bool app_state::can_copy_current_focus() const
{
	// The search box is always present, so it only wins when something is selected in it
	if (auto* const edit_owner = focused_edit_box_owner(); edit_owner && edit_owner->edit_can_copy())
		return true;

	if (const auto view = focused_text_view())
		return view->can_copy_text();

	if (list_has_focus())
	{
		const auto item = is_search(get_mode()) ? selected_search_list_item() : selected_file_list_item();
		return item && item->source;
	}

	return false;
}

bool app_state::can_delete_current_focus() const
{
	if (auto* const edit_owner = focused_edit_box_owner())
		return edit_owner != nullptr;

	if (const auto view = focused_text_view())
		return view->can_delete_text();

	if (file_list_has_focus())
	{
		const auto item = selected_file_list_item();
		return item && item->source && !item->source->is_folder;
	}

	return false;
}

bool app_state::copy_current_focus_to_clipboard() const
{
	if (auto* const edit_owner = focused_edit_box_owner(); edit_owner && edit_owner->edit_copy())
		return true;

	if (const auto view = focused_text_view())
		return view->copy_text_to_clipboard();

	if (search_list_has_focus())
	{
		const auto item = selected_search_list_item();
		if (!item || !item->source)
			return false;
		return pf::platform_text_to_clipboard(clipboard_path_text(item->source->path, item->line_number));
	}

	if (file_list_has_focus())
	{
		const auto item = selected_file_list_item();
		if (!item || !item->source)
			return false;
		return pf::platform_text_to_clipboard(clipboard_path_text(item->source->path));
	}

	return false;
}

bool app_state::delete_current_focus()
{
	if (auto* const edit_owner = focused_edit_box_owner())
		return edit_owner->edit_delete();

	if (const auto view = focused_text_view())
		return view->delete_selected_text();

	if (file_list_has_focus())
	{
		const auto item = selected_file_list_item();
		if (!item || !item->source || item->source->is_folder)
			return false;

		const bool was_deleted = item->source->is_deleted;
		delete_item(item->source);
		return !was_deleted && item->source->is_deleted;
	}

	return false;
}

bool app_state::can_rename_selected_file() const
{
	const auto item = selected_file_list_item();
	return file_list_has_focus() && item && item->source && !item->source->is_folder;
}

void app_state::begin_rename_selected_file()
{
	if (_files_view)
		_files_view->begin_selected_rename(_list_window);
}

void app_state::update_styles()
{
	_styles.list_font = {_styles.list_font_height, pf::font_name::calibri};
	_styles.edit_font = {(_styles.list_font_height * 3) / 2, pf::font_name::calibri};
	_styles.text_font = {_styles.text_font_height, pf::font_name::consolas};

	_styles.padding_x = static_cast<int>(5 * _styles.dpi_scale);
	_styles.padding_y = static_cast<int>(5 * _styles.dpi_scale);
	_styles.indent = static_cast<int>(16 * _styles.dpi_scale);
	_styles.edit_box_margin = static_cast<int>(6 * _styles.dpi_scale);
	_styles.edit_box_inner_pad = static_cast<int>(4 * _styles.dpi_scale);
	_styles.list_top_pad = static_cast<int>(4 * _styles.dpi_scale);
	_styles.list_scroll_pad = static_cast<int>(64 * _styles.dpi_scale);
}

void app_state::on_zoom(const int delta, const zoom_target target)
{
	switch (target)
	{
	case zoom_target::text:
		_styles.text_font_height = pf::clamp(_styles.text_font_height + delta, 8, 72);
		break;
	case zoom_target::list:
		_styles.list_font_height = pf::clamp(_styles.list_font_height + delta, 8, 72);
		break;
	}

	update_styles();

	_doc_window->notify_size();
	_list_window->notify_size();
}

pf::file_path app_state::save_folder() const
{
	if (_active_item && !_active_item->path.empty())
		return _active_item->path.folder();
	if (_root_folder && !_root_folder->path.empty())
		return _root_folder->path;
	return pf::current_directory();
}

void app_state::set_word_wrap(const bool enabled)
{
	_word_wrap = enabled;
	if (_doc_view)
		_doc_view->set_word_wrap(enabled);
}

void app_state::set_message(std::string text)
{
	_message_bar_text = std::move(text);
	invalidate(invalid::doc);
}

list_view* app_state::focused_list_view() const
{
	if (!list_has_focus())
		return nullptr;
	if (is_search(get_mode()))
		return _search_view.get();
	return _files_view.get();
}

list_view* app_state::focused_edit_box_owner() const
{
	auto* const view = focused_list_view();
	return view && view->has_active_edit_box() ? view : nullptr;
}

void app_state::refresh_index(const pf::file_path& root_path, std::function<void()> on_complete,
                              const bool preserve_in_memory_documents)
{
	index_snapshot_map existing;
	if (_root_folder)
		snapshot_index_items_recursive(existing, _root_folder);

	const bool same_root = !_root_folder || _root_folder->path == root_path;
	const bool preserve = preserve_in_memory_documents && same_root;

	_scheduler->run_async([t = shared_from_this(), root_path, existing = std::move(existing),
			preserve, on_complete = std::move(on_complete)]() mutable
		{
			auto new_root = load_index(root_path, existing);

			t->_scheduler->run_ui([t, new_root = std::move(new_root), preserve,
					on_complete = std::move(on_complete)]() mutable
			{
				// Re-attach in-memory documents that are not on disk. Done on the UI thread
				// because it walks and mutates live index items.
				if (preserve && t->_root_folder)
				{
					std::unordered_map<pf::file_path, index_item_ptr, pf::ihash> old_items;
					map_index_items_recursive(old_items, t->_root_folder);

					std::unordered_map<pf::file_path, index_item_ptr, pf::ihash> new_paths;
					map_index_items_recursive(new_paths, new_root);

					for (const auto& [path, item] : old_items)
					{
						if (!item->doc || item->is_folder || item->is_deleted || new_paths.contains(path))
							continue;

						auto parent = find_item_recursively(new_root, path.folder());
						if (!parent)
							parent = new_root;
						add_child_sorted(parent, item);
					}
				}

				t->set_root(new_root);
				t->remember_root_folder(t->root_item()->path);
				t->invalidate(invalid::files_layout | invalid::files_populate);

				if (on_complete)
					on_complete();
			});
		});
}

void app_state::execute_search(const std::string& text, std::function<void()> on_complete)
{
	std::vector<search_input> inputs;
	collect_search_inputs(_root_folder->children, inputs);

	const auto generation = _search_generation.fetch_add(1) + 1;

	_scheduler->run_async(
		[t = shared_from_this(), inputs = std::move(inputs), text, generation,
			on_complete = std::move(on_complete)]() mutable
		{
			const auto is_cancelled = [t, generation] { return t->_search_generation.load() != generation; };

			auto results = perform_search(inputs, text, is_cancelled);

			if (is_cancelled())
				return;

			t->_scheduler->run_ui(
				[t, results = std::move(results), generation, on_complete = std::move(on_complete)]()
				{
					if (t->_search_generation.load() != generation)
						return; // a newer search has superseded this one

					apply_search_results(t->_root_folder->children, results);
					if (on_complete)
						on_complete();
				});
		});
}

void app_state::on_search(const std::string& text)
{
	_pending_search_text = text;

	// Coalesce keystrokes so a full-tree scan is not queued per character
	if (_app_window && _app_window->set_timer(search_debounce_timer_id, search_debounce_ms))
		return;

	run_pending_search();
}

void app_state::run_pending_search()
{
	execute_search(_pending_search_text, [this]() { _search_view->populate(); });
}

void app_state::set_mode(const view_mode m)
{
	doc_view_ptr new_view;

	if (get_mode() != m && view_content_of(get_mode()) != view_content_of(m))
		new_view = create_doc_view_for_mode(*this, m);

	if (active_item())
		active_item()->saved_view_content = view_content_of(m);

	_mode = m;

	if (new_view)
	{
		if (_doc_view)
			_doc_view->stop_caret_blink(_doc_window);

		new_view->set_document(active_item()->doc);
		if (view_content_of(m) == view_content::edit_text)
			new_view->set_word_wrap(_word_wrap);

		_doc_view = new_view;
		_doc_window->set_reactor(new_view);
		_doc_window->notify_size();
		_doc_view->scroll_to_top();
		_doc_view->update_focus(_doc_window);
	}
	else
	{
		_doc_view->set_document(active_item()->doc);
	}

	_list_window->show(true);
	_list_window->set_reactor(is_search(m) ? std::static_pointer_cast<frame_reactor>(_search_view) : _files_view);
	_list_window->notify_size();
	_files_view->select_index_item(_list_window, active_item());
	invalidate(invalid::doc | invalid::windows);
	layout_views();
}

void app_state::toggle_search_mode()
{
	const auto next_mode = with_search(get_mode(), !is_search(get_mode()));
	const auto focus_list = !is_search(get_mode());

	set_mode(next_mode);
	if (focus_list)
		_list_window->set_focus();
}

app_state::search_results_map app_state::perform_search(const std::vector<search_input>& inputs,
                                                        const std::string& text,
                                                        const std::function<bool()>& is_cancelled)
{
	search_results_map results;
	int total = 0;

	for (const auto& input : inputs)
	{
		if (total >= max_search_results) break;
		if (is_cancelled && is_cancelled()) break;

		auto file_results = search_file_results(input, text);

		if (total + static_cast<int>(file_results.size()) > max_search_results)
			file_results.resize(max_search_results - total);

		total += static_cast<int>(file_results.size());

		if (!file_results.empty())
			results[input.path] = std::move(file_results);
	}

	return results;
}

void app_state::copy_files_to_folder(const std::vector<pf::file_path>& sources, const pf::file_path& dest_folder)
{
	if (sources.empty() || dest_folder.empty())
		return;

	pf::file_path first_copied;

	for (const auto& src : sources)
	{
		if (pf::is_directory(src))
			continue;

		const auto dest = dest_folder.combine(src.name());

		if (dest.exists())
		{
			const auto id = _app_window->message_box(
				std::format("'{}' already exists. Overwrite?", dest.name()),
				g_app_name,
				pf::msg_box_style::yes_no | pf::msg_box_style::icon_question);

			if (id != pf::msg_box_result::yes)
				continue;

			if (!pf::platform_copy_file(src, dest, false))
				continue;
		}
		else
		{
			if (!pf::platform_copy_file(src, dest, true))
				continue;
		}

		if (first_copied.empty())
			first_copied = dest;
	}

	refresh_index(root_item()->path, [this, first_copied]
	{
		invalidate(invalid::files_populate);

		if (!first_copied.empty())
			load_doc(first_copied);
	});
}

void app_state::delete_item(const index_item_ptr& item)
{
	if (!item || item->path.empty() || item->is_folder)
		return;

	const bool exists_on_disk = item->path.is_save_path() && item->path.exists();

	const auto id = _app_window->message_box(
		exists_on_disk
			? std::format("Send '{}' to Recycle Bin?", item->name)
			: std::format("Delete unsaved document '{}'?", item->name),
		g_app_name,
		pf::msg_box_style::yes_no | pf::msg_box_style::icon_question);

	if (id != pf::msg_box_result::yes)
		return;

	// If deleting the active document, switch away first
	if (active_item() == item)
	{
		select_alternative();
	}

	bool removed = false;

	if (exists_on_disk)
	{
		if (pf::platform_recycle_file(item->path))
			removed = remove_child_recursive(root_item(), item);
	}
	else
	{
		// In-memory or unsaved documents have no disk file to recycle
		removed = remove_child_recursive(root_item(), item);
	}

	if (removed)
	{
		item->is_deleted = true;
		invalidate(invalid::files_populate);
	}
}

create_path_result app_state::create_new_file(const pf::file_path& new_path, std::string content)
{
	if (new_path.empty())
		return {};

	const auto unique_path = make_unique_child_path(root_item(), new_path, true);
	const auto d = std::make_shared<document>(*this, content, true);
	d->path(unique_path);
	apply_spell_check_mode(d);

	const auto item = std::make_shared<index_item>(
		unique_path, std::string(unique_path.name()), false, d);
	item->saved_view_content = view_content::edit_text;

	auto parent = find_item_recursively(root_item(), unique_path.folder());
	if (!parent) parent = _root_folder;
	add_child_sorted(parent, item);

	set_active_item(item);
	invalidate(invalid::files_populate);

	return {true, unique_path, unique_path.name()};
}

void app_state::rename_item(const index_item_ptr& item, const std::string& new_name)
{
	if (!item || new_name.empty() || item->is_folder)
		return;

	if (!is_valid_item_name(new_name))
	{
		_app_window->message_box(
			std::format("'{}' is not a valid file name.", new_name),
			g_app_name,
			pf::msg_box_style::ok | pf::msg_box_style::icon_warning);
		return;
	}

	const auto old_path = item->path;
	const auto new_path = old_path.folder().combine(new_name);

	if (old_path == new_path)
		return;

	const auto conflicting_item = find_item_recursively(root_item(), new_path);
	if ((conflicting_item && conflicting_item != item) || new_path.exists() || pf::is_directory(new_path))
	{
		_app_window->message_box(
			std::format("A file named '{}' already exists.", new_name),
			g_app_name,
			pf::msg_box_style::ok | pf::msg_box_style::icon_warning);
		return;
	}

	if (!pf::platform_rename_file(old_path, new_path))
	{
		_app_window->message_box(
			std::format("Failed to rename '{}'.", item->name),
			g_app_name,
			pf::msg_box_style::ok | pf::msg_box_style::icon_warning);
		return;
	}

	// Update the in-memory item
	item->path = new_path;
	item->name = new_name;

	// Update the document path if loaded
	if (item->doc)
		item->doc->path(new_path);

	sort_index_children(find_parent_item(root_item(), item));

	invalidate(invalid::files_populate | invalid::app_title);
}

create_path_result app_state::create_new_folder(const pf::file_path& folder)
{
	if (folder.empty())
		return {};

	const auto new_path = make_unique_child_path(root_item(), folder.combine("new-folder"), true);

	if (!pf::platform_create_directory(new_path))
		return {};

	invalidate(invalid::index);

	return {true, new_path, new_path.name()};
}

uint32_t app_state::handle_message(const pf::window_frame_ptr window,
                                   const pf::message_type msg, const uintptr_t wParam, const intptr_t lParam)
{
	_app_window = window;
	using mt = pf::message_type;

	if (msg == mt::create)
		return on_create(window);
	if (msg == mt::erase_background)
		return 1;
	if (msg == mt::set_focus)
	{
		_doc_window->set_focus();
		return 0;
	}
	if (msg == mt::close)
		return on_close();
	if (msg == mt::command)
		return 0;
	if (msg == mt::timer)
	{
		if (static_cast<uint32_t>(wParam) == search_debounce_timer_id)
		{
			_app_window->kill_timer(search_debounce_timer_id);
			run_pending_search();
		}
		return 0;
	}
	if (msg == mt::dpi_changed)
		return on_window_dpi_changed(wParam, lParam);
	if (msg == mt::drop_files)
	{
		const auto paths = pf::dropped_file_paths(wParam);
		if (!paths.empty())
			load_doc(paths.front());
		return 0;
	}

	return 0;
}

uint32_t app_state::handle_mouse(const pf::window_frame_ptr window,
                                 const pf::mouse_message_type msg, const pf::mouse_params& params)
{
	_app_window = window;
	using mt = pf::mouse_message_type;

	if (msg == mt::left_button_down)
	{
		const auto rect = window->get_client_rect();
		_panel_splitter.begin_tracking(rect, params.point, window);
	}

	if (msg == mt::mouse_leave)
	{
		_panel_splitter.clear_hover(window);
	}

	if (msg == mt::mouse_move)
	{
		const auto rect = window->get_client_rect();

		if (params.left_button)
		{
			if (_panel_splitter.track_to(rect, params.point, window))
				layout_views();
		}

		_panel_splitter.update_hover(rect, params.point, window);
	}

	if (msg == mt::left_button_up)
	{
		_panel_splitter.end_tracking(window);
	}

	return 0;
}

uint32_t app_state::on_create(const pf::window_frame_ptr& window)
{
	pf::debug_trace("app_state::on_create ENTERED\n");

	_app_window = window;
	window->accept_drop_files(true);

	// Query initial DPI scale before creating child windows
	on_scale(window->get_dpi_scale());

	_doc_window = window->create_child("TEXT_FRAME",
	                                   pf::window_style::child | pf::window_style::visible |
	                                   pf::window_style::clip_children,
	                                   ui::window_background);
	_doc_window->set_reactor(_doc_view);

	_list_window = window->create_child("LIST_FRAME",
	                                    pf::window_style::child | pf::window_style::visible |
	                                    pf::window_style::clip_children,
	                                    ui::window_background);
	_list_window->accept_drop_files(true);
	_list_window->set_reactor(_files_view);

	// Restore font sizes from config
	const auto text_size = pf::config_read("Font", "TextSize");
	const auto list_size = pf::config_read("Font", "ListSize");

	int lh = 0;
	int th = 0;
	if (parse_int_value(list_size, lh) && parse_int_value(text_size, th))
	{
		initialize_styles(lh, th);
	}

	invalidate(invalid::doc);
	update_title();

	// Restore splitter positions from config
	const auto panel_ratio = pf::config_read("Splitter", "PanelRatio");
	const auto word_wrap = pf::config_read("View", "WordWrap");
	const auto spell_check = pf::config_read("View", "SpellCheck");

	if (double ratio = 0.0; parse_double_value(panel_ratio, ratio))
		_panel_splitter._ratio = std::clamp(ratio, splitter::min_ratio, splitter::max_ratio);

	if (!word_wrap.empty())
		_word_wrap = word_wrap != "0";
	set_word_wrap(_word_wrap);

	_spell_check_mode = parse_spell_check_mode(spell_check);
	apply_spell_check_mode(doc());

	// Restore window placement from config
	if (_has_startup_placement)
	{
		_app_window->set_placement(_startup_placement);
	}

	// Determine root folder: startup folder from config or cwd
	auto root = _startup_folder;
	auto doc_path = _startup_document;

	if (root.empty())
		root = pf::current_directory();
	remember_root_folder(root);

	pf::debug_trace(std::format("on_create: root='{}'\n", root.view()));
	if (!root.empty())
	{
		refresh_index(root, [this, doc_path]
		{
			invalidate(invalid::files_populate);

			if (!doc_path.empty())
				load_doc(doc_path);
		});
	}

	return 0;
}

void app_state::handle_paint(pf::window_frame_ptr& window, pf::draw_context& dc)
{
	const auto bounds = window->get_client_rect();
	_panel_splitter.draw(dc, bounds);
}

void app_state::layout_views() const
{
	if (!_app_window)
		return;

	const auto is_list_visible = _list_window && _list_window->is_visible();
	const auto bounds = _app_window->get_client_rect();
	const auto panel_split = _panel_splitter.split_pos(bounds);

	auto text_bounds = bounds;
	text_bounds.left = panel_split + _panel_splitter.bar_width();
	_doc_window->move_window(text_bounds);

	auto panel_bounds = bounds;
	panel_bounds.right = panel_split - _panel_splitter.bar_width();

	if (is_list_visible)
	{
		_list_window->move_window(panel_bounds);
	}
}

void app_state::save_config() const
{
	// Save window position
	if (_app_window)
	{
		const auto p = _app_window->get_placement();
		pf::config_write("Window", "Left", to_str(p.normal_bounds.left));
		pf::config_write("Window", "Top", to_str(p.normal_bounds.top));
		pf::config_write("Window", "Right", to_str(p.normal_bounds.right));
		pf::config_write("Window", "Bottom", to_str(p.normal_bounds.bottom));
		pf::config_write("Window", "Maximized", p.maximized ? "1" : "0");
	}

	// Save font sizes
	const auto& styles = _styles;
	pf::config_write("Font", "TextSize", to_str(styles.text_font_height));
	pf::config_write("Font", "ListSize", to_str(styles.list_font_height));

	// Save splitter positions as ratios
	pf::config_write("Splitter", "PanelRatio", to_str(_panel_splitter._ratio));
	pf::config_write("View", "WordWrap", _word_wrap ? "1" : "0");
	pf::config_write("View", "SpellCheck", spell_check_mode_config_value(_spell_check_mode));

	// Save current root folder and document
	if (root_item() && !root_item()->path.empty())
		pf::config_write("Recent", "Folder", root_item()->path.view());

	for (size_t i = 0; i < max_recent_root_folders; ++i)
	{
		const auto folder_key = recent_root_folder_config_key(i);
		const auto folder_value = i < _recent_root_folders.size()
			                          ? _recent_root_folders[i].view()
			                          : std::string_view{};
		pf::config_write("RecentFolders", folder_key, folder_value);

		const auto document_key = recent_root_document_config_key(i);
		const auto document_value = i < _recent_root_documents.size()
			                            ? _recent_root_documents[i].view()
			                            : std::string_view{};
		pf::config_write("RecentFolders", document_key, document_value);
	}

	// Prefer recent_item if it has a saveable path; otherwise find any saved file in the tree
	const auto active = active_item();
	if (active && !active->path.empty() && active->path.is_save_path())
		pf::config_write("Recent", "Document", active->path.view());

	pf::config_flush();
}

void app_state::remember_root_folder(const pf::file_path& folder, const pf::file_path& document)
{
	if (folder.empty())
		return;

	const auto previous_folders = _recent_root_folders;

	pf::file_path remembered_document = document;
	for (size_t i = 0; i < _recent_root_folders.size(); ++i)
	{
		if (_recent_root_folders[i] == folder)
		{
			if (remembered_document.empty() && i < _recent_root_documents.size())
				remembered_document = _recent_root_documents[i];
			_recent_root_folders.erase(_recent_root_folders.begin() + static_cast<ptrdiff_t>(i));
			if (i < _recent_root_documents.size())
				_recent_root_documents.erase(_recent_root_documents.begin() + static_cast<ptrdiff_t>(i));
			break;
		}
	}

	_recent_root_folders.insert(_recent_root_folders.begin(), folder);
	_recent_root_documents.insert(_recent_root_documents.begin(), remembered_document);
	if (_recent_root_folders.size() > max_recent_root_folders)
		_recent_root_folders.resize(max_recent_root_folders);
	if (_recent_root_documents.size() > max_recent_root_folders)
		_recent_root_documents.resize(max_recent_root_folders);

	// Rebuilding the menu recreates every command closure and the accelerator table
	if (previous_folders != _recent_root_folders)
		update_recent_root_menu();
}

void app_state::remember_root_document(const pf::file_path& folder, const pf::file_path& document)
{
	if (folder.empty() || document.empty())
		return;

	for (size_t i = 0; i < _recent_root_folders.size(); ++i)
	{
		if (_recent_root_folders[i] == folder)
		{
			if (i >= _recent_root_documents.size())
				_recent_root_documents.resize(i + 1);
			_recent_root_documents[i] = document;
			return;
		}
	}

	remember_root_folder(folder, document);
}

void app_state::restore_recent_root_folders(const std::span<const recent_root_entry> entries)
{
	// Each entry is pushed to the front, so replay oldest first to end up most-recent-first
	for (auto i = entries.size(); i > 0; --i)
	{
		const auto& entry = entries[i - 1];
		if (!entry.folder.empty())
			remember_root_folder(entry.folder, entry.document);
	}
}

void app_state::update_recent_root_menu()
{
	if (_app_window)
		_app_window->set_menu(build_menu());
}

std::vector<pf::menu_command> app_state::build_recent_root_folder_menu()
{
	std::vector<pf::menu_command> items;
	items.reserve(_recent_root_folders.size());

	for (size_t i = 0; i < _recent_root_folders.size(); ++i)
	{
		const auto path = _recent_root_folders[i];
		items.emplace_back(
			std::format("&{} {}", i + 1, escape_menu_text(path.view())),
			recent_root_folder_menu_id_base + static_cast<int>(i),
			[this, path]
			{
				if (root_item() && path == root_item()->path)
				{
					remember_root_folder(path);
					return;
				}
				if (!prompt_save_all_modified())
					return;

				// Resolved now rather than captured, so the menu survives document changes
				const auto doc_path = recent_root_document(path);

				refresh_index(path, [this, doc_path]
				{
					invalidate(
						invalid::files_populate | invalid::files_layout | invalid::search_populate |
						invalid::app_title);
					if (!doc_path.empty())
					{
						if (const auto item = find_item_recursively(root_item(), doc_path))
							load_doc(item);
					}
				}, false);
			},
			[path, this]
			{
				return !path.empty() && (!root_item() || path != root_item()->path);
			});
	}

	if (items.empty())
		items.emplace_back("(Empty)", 0, nullptr, [] { return false; });

	return items;
}

// About and test output are generated, not authored: read-only and never dirty
void app_state::show_generated_document(const pf::file_path& path, std::string content)
{
	const auto unique_path = make_unique_child_path(root_item(), path, false);
	const auto d = std::make_shared<document>(*this, content, false);
	d->path(unique_path);
	d->read_only(true);

	const auto item = std::make_shared<index_item>(
		unique_path, std::string(unique_path.name()), false, d);
	item->saved_view_content = view_content::markdown;

	auto parent = find_item_recursively(root_item(), unique_path.folder());
	if (!parent) parent = _root_folder;
	add_child_sorted(parent, item);

	set_active_item(item);
	invalidate(invalid::files_populate);
}

uint32_t app_state::on_about()
{
	show_generated_document(save_folder().combine("about", "md"), make_about_text(get_commands()));
	return 0;
}

void app_state::select_alternative()
{
	const auto current = active_item();

	// Try list navigation first
	on_navigate_next(true);
	if (active_item() != current)
		return;

	// Search the entire tree for any file that isn't the current item
	std::function<index_item_ptr(const index_item_ptr&)> find_file = [&](const index_item_ptr& node) -> index_item_ptr
	{
		if (!node->is_folder && node != current)
			return node;
		for (const auto& child : node->children)
		{
			if (auto found = find_file(child))
				return found;
		}
		return nullptr;
	};

	if (const auto alt = find_file(root_item()))
		load_doc(alt);
	else
		create_new_file(save_folder().combine("new", "md"), "");
}

uint32_t app_state::on_run_tests()
{
	const auto results = run_all_tests();
	show_generated_document(save_folder().combine("tests", "md"), results);
	return 0;
}

void app_state::on_idle()
{
	const auto invalids = validate();

	if (invalids & invalid::index)
	{
		refresh_index(root_item()->path);
	}

	if (invalids & invalid::app_title)
	{
		update_title();
	}

	if (invalids & invalid::doc_layout)
	{
		_doc_view->layout();
		_doc_window->invalidate();
	}

	if (invalids & invalid::doc_caret)
	{
		_doc_view->update_caret(_doc_window);
	}

	if (invalids & invalid::doc_scrollbar)
	{
		_doc_view->recalc_horz_scrollbar();
		_doc_view->recalc_vert_scrollbar();
	}

	if (invalids & invalid::files_populate)
	{
		_files_view->populate(_list_window);
		_list_window->invalidate();
	}

	if (invalids & invalid::search_populate)
	{
		_search_view->populate();
		_list_window->invalidate();
	}

	if (invalids & invalid::files_layout)
	{
		_files_view->layout_list();
		_list_window->invalidate();
	}

	if (invalids & invalid::search_layout)
	{
		_search_view->layout_list();
		_list_window->invalidate();
	}

	if (invalids & invalid::windows)
	{
		_doc_window->invalidate();
		_list_window->invalidate();
	}
}

static std::shared_ptr<app_state> g_main_app;

// platform_scheduler — Production implementation that delegates to pf::run_async / pf::run_ui.
class platform_scheduler final : public async_scheduler
{
public:
	void run_async(std::function<void()> task) override { pf::run_async(std::move(task)); }
	void run_ui(std::function<void()> task) override { pf::run_ui(std::move(task)); }
};

namespace
{
	struct cli_mode_result
	{
		bool handled = false;
		app_init_result result;
	};

	std::string spell_check_diagnostics(const std::string_view word)
	{
		auto checker = pf::create_spell_checker();
		std::string out;
		out += std::format("Word: {}\n", word);
		const std::string_view available_text = checker && checker->available() ? "yes" : "no";
		const std::string diagnostics =
			checker ? checker->diagnostics() : std::string("No checker instance.");
		out += std::format("Available: {}\n", available_text);
		out += std::format("Diagnostics: {}\n", diagnostics);

		if (checker && checker->available())
		{
			const auto valid = checker->is_word_valid(word);
			const std::string_view valid_text = valid ? "yes" : "no";
			out += std::format("Valid: {}\n", valid_text);
			const auto suggestions = checker->suggest(word);
			out += "Suggestions:";
			if (suggestions.empty())
			{
				out += " (none)\n";
			}
			else
			{
				out += "\n";
				for (const auto& suggestion : suggestions)
					out += std::format("- {}\n", suggestion);
			}
		}

		return out;
	}

	// Handles the non-GUI command-line modes; also reports any file argument to open.
	cli_mode_result run_cli_mode(const std::span<const std::string_view> params, std::string_view& file_to_open)
	{
		for (const auto& param : params)
		{
			if (pf::icmp(param, "/test") == 0 || pf::icmp(param, "--test") == 0)
			{
				const auto results = run_all_tests_result();
				pf::write_stdout(results.output);
				return {true, {.start_gui = false, .exit_code = results.fail_count == 0 ? 0 : 1}};
			}

			auto try_prefix = [&](const std::string_view p1, const std::string_view p2) -> std::string_view
			{
				if (param.size() > p1.size() && pf::icmp(param.substr(0, p1.size()), p1) == 0)
					return param.substr(p1.size());
				if (param.size() > p2.size() && pf::icmp(param.substr(0, p2.size()), p2) == 0)
					return param.substr(p2.size());
				return {};
			};

			if (const auto word = try_prefix("/spell:", "--spell:"); !word.empty())
			{
				pf::write_stdout(spell_check_diagnostics(word));
				return {true, {.start_gui = false}};
			}

			if (!param.starts_with(u8'/') && !param.starts_with(u8'-'))
			{
				file_to_open = param;
			}
		}

		return {};
	}

	void restore_session(app_state& app)
	{
		std::vector<app_state::recent_root_entry> entries;
		entries.reserve(app_state::max_recent_root_folders);

		for (size_t i = 0; i < app_state::max_recent_root_folders; ++i)
		{
			// Kept even when currently unreachable, so an offline drive is not forgotten
			entries.emplace_back(
				pf::file_path{pf::config_read("RecentFolders", recent_root_folder_config_key(i))},
				pf::file_path{pf::config_read("RecentFolders", recent_root_document_config_key(i))});
		}

		app.restore_recent_root_folders(entries);

		const auto folder = pf::file_path{pf::config_read("Recent", "Folder")};
		const auto document = pf::file_path{pf::config_read("Recent", "Document")};

		const bool folder_ok = pf::is_directory(folder);
		const bool document_ok = !document.empty() && document.exists();

		if (folder_ok)
			app._startup_folder = folder;
		if (document_ok)
			app._startup_document = document;
		if (folder_ok && document_ok)
			app.remember_root_document(folder, document);
	}

	void restore_window_placement(app_state& app)
	{
		int left = 0;
		int top = 0;
		int right = 0;
		int bottom = 0;

		if (!parse_int_value(pf::config_read("Window", "Left"), left) ||
			!parse_int_value(pf::config_read("Window", "Top"), top) ||
			!parse_int_value(pf::config_read("Window", "Right"), right) ||
			!parse_int_value(pf::config_read("Window", "Bottom"), bottom))
			return;

		app._startup_placement.normal_bounds = {left, top, right, bottom};
		app._startup_placement.maximized = pf::config_read("Window", "Maximized") == "1";
		app._has_startup_placement = true;
	}
}

app_init_result app_init(const pf::window_frame_ptr& main_frame,
                         const std::span<const std::string_view> params)
{
	pf::config_set_app_name("rethinkify");

	std::string_view file_to_open;

	if (const auto cli = run_cli_mode(params, file_to_open); cli.handled)
		return cli.result;

	g_main_app = std::make_shared<app_state>(std::make_shared<platform_scheduler>());

	// Create the main window via platform
	main_frame->set_reactor(g_main_app);
	main_frame->set_menu(g_main_app->build_menu());

	if (!file_to_open.empty())
	{
		g_main_app->_startup_document = file_to_open;
	}
	else
	{
		restore_session(*g_main_app);
	}

	restore_window_placement(*g_main_app);

	return {};
}

void app_idle()
{
	if (g_main_app)
		g_main_app->on_idle();
}

void app_destroy()
{
	// Release the COM spell checker while COM is still initialised
	reset_spell_checker();
	g_main_app.reset();
}
