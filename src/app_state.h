// app_state.h — Application state: document collection, file operations, testable app logic

#pragma once

#include "app.h"
#include "document.h"
#include "commands.h"
#include "ui.h"


struct search_result;
class app_state;

class text_view;
class file_list_view;
class search_list_view;
class doc_view;
struct list_view_item;

using doc_view_ptr = std::shared_ptr<doc_view>;
using text_view_ptr = std::shared_ptr<text_view>;
using folder_view_ptr = std::shared_ptr<file_list_view>;
using search_view_ptr = std::shared_ptr<search_list_view>;
using list_view_item_ptr = std::shared_ptr<list_view_item>;


class app_state final : public app_events, public pf::frame_reactor, public std::enable_shared_from_this<app_state>
{
public:
	static const pf::file_path about_path;
	static const pf::file_path test_results_path;

	static constexpr uint64_t max_search_file_size = 10 * 1024 * 1024;
	static constexpr size_t max_recent_root_folders = 8;

	pf::window_frame_ptr _app_window;
	pf::window_frame_ptr _doc_window;
	pf::window_frame_ptr _list_window;

	doc_view_ptr _doc_view;
	folder_view_ptr _files_view;
	search_view_ptr _search_view;

	async_scheduler_ptr _scheduler;

	// Config-based startup state
	pf::file_path _startup_folder;
	pf::file_path _startup_document;
	std::vector<pf::file_path> _recent_root_folders;
	std::vector<pf::file_path> _recent_root_documents;
	pf::window_frame::placement _startup_placement{};
	bool _has_startup_placement = false;
	spell_check_mode _spell_check_mode = spell_check_mode::disabled;

	index_item_ptr _active_item;
	index_item_ptr _root_folder;
	view_styles _styles;
	view_mode _mode = view_mode::edit_text_files;
	commands _commands;

	std::atomic<uint32_t> _invalid = 0;

	splitter _panel_splitter{splitter::orientation::vertical, 0.2};

	std::vector<command_def> make_commands();
	explicit app_state(async_scheduler_ptr scheduler);

	std::vector<pf::menu_command> build_menu();
	pf::menu_command command_menu_item(command_id id,
	                                   std::function<void()> action_override = nullptr,
	                                   std::function<bool()> is_enabled_override = nullptr,
	                                   std::function<bool()> is_checked_override = nullptr,
	                                   std::string text_override = {}) const override;
	bool invoke_menu_accelerator(const pf::window_frame_ptr& window,
	                             const std::vector<pf::menu_command>& items,
	                             unsigned int vk) const override;

	void ensure_visible(const text_location& pt) override;

	void invalidate_lines(int start, int end) override;

	void path_selected(const index_item_ptr& item) override
	{
		if (pf::is_directory(item->path))
			return;
		load_doc(item);
	}

	void open_path_and_select(const index_item_ptr& item, const int line, const int col,
	                          const int length) override
	{
		load_doc(item);
		const text_selection sel(col, line, col + length, line);
		active_item()->doc->select(sel);
		invalidate(invalid::doc | invalid::doc_caret);
	}

	void set_focus(view_focus v) override;

	void set_mode(view_mode m) override;

	void on_search(const std::string& text) override;

	void toggle_search_mode();

	uint32_t handle_message(pf::window_frame_ptr window, pf::message_type msg,
	                        uintptr_t wParam, intptr_t lParam) override;

	uint32_t handle_mouse(pf::window_frame_ptr window, pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override;

	uint32_t on_create(const pf::window_frame_ptr& window);

	uint32_t on_window_dpi_changed(const uintptr_t wParam, const intptr_t lParam)
	{
		const auto scale_factor = (wParam & 0xFFFF) / static_cast<double>(96);
		on_scale(scale_factor);

		const auto new_bounds = reinterpret_cast<pf::irect*>(lParam);

		if (new_bounds)
		{
			_app_window->move_window(*new_bounds);
		}

		_doc_window->notify_size();
		_list_window->notify_size();

		return 0;
	}

	void handle_paint(pf::window_frame_ptr& window, pf::draw_context& dc) override;

	void layout_views() const;

	void handle_size(pf::window_frame_ptr& window, pf::isize extent,
	                 pf::measure_context& measure) override
	{
		layout_views();
	}

	void save_config() const;
	void apply_spell_check_mode(const document_ptr& target_doc) const;
	void set_spell_check_mode(spell_check_mode mode, bool persist = true);
	void remember_root_folder(const pf::file_path& folder, const pf::file_path& document = {});
	void remember_root_document(const pf::file_path& folder, const pf::file_path& document);
	void update_recent_root_menu();
	[[nodiscard]] std::vector<pf::menu_command> build_recent_root_folder_menu();
	[[nodiscard]] const std::vector<pf::file_path>& recent_root_folders() const { return _recent_root_folders; }
	[[nodiscard]] const std::vector<pf::file_path>& recent_root_documents() const { return _recent_root_documents; }

	uint32_t on_close()
	{
		if (!prompt_save_all_modified())
			return 0; // user cancelled

		save_config();
		_app_window->close();
		return 0;
	}

	uint32_t on_about();

	void on_escape() override
	{
		if (is_markdown(get_mode()))
		{
			close_markdown();
		}
		else if (is_csv(get_mode()))
		{
			close_csv();
		}
		else if (is_search(get_mode()))
		{
			toggle_search_mode();
		}
	}

	std::string _message_bar_text;

	std::string_view message_bar_text() const override { return _message_bar_text; }

	void select_alternative();

	void close_markdown()
	{
		if (is_markdown(get_mode()))
			toggle_markdown_view();
	}

	void close_csv()
	{
		if (is_csv(get_mode()))
			set_mode(with_view_content(get_mode(), view_content::edit_text));

		update_info_message();
	}

	void toggle_markdown_view()
	{
		if (is_markdown(get_mode()))
			set_mode(with_view_content(get_mode(), view_content::edit_text));
		else if (is_edit_text(get_mode()))
			set_mode(with_view_content(get_mode(), view_content::markdown));

		update_info_message();
	}

	uint32_t on_run_tests();

	uint32_t on_open()
	{
		const auto path = pf::open_file_path("Open File", "");

		if (!path.empty())
		{
			load_doc(path);
		}
		return 0;
	}

	uint32_t on_save()
	{
		const auto& path = active_item()->path;
		if (path.is_save_path())
		{
			if (save_active_doc(path))
				invalidate(invalid::files_layout | invalid::app_title);
		}
		else
		{
			on_save_as();
		}
		return 0;
	}

	uint32_t on_save_as()
	{
		if (save_doc())
		{
			refresh_index(root_item()->path, [this]
			{
				invalidate(invalid::files_populate);
				invalidate(invalid::app_title);
			});
		}
		return 0;
	}

	uint32_t on_new()
	{
		create_new_file(save_folder().combine("new", "md"), "");
		return 0;
	}

	uint32_t on_edit_reformat()
	{
		doc()->reformat_json();
		return 0;
	}

	uint32_t on_edit_remove_duplicates()
	{
		doc()->sort_remove_duplicates();
		return 0;
	}

	void update_title() const
	{
		auto name = active_item()->path.name();
		if (name.empty()) name = active_item()->path.view();
		const auto title = name.empty() ? g_app_name : std::format("{} - {}", name, g_app_name);
		_app_window->set_text(title);
	}

	void set_active_item(const index_item_ptr& item);
	void update_info_message();


	uint32_t on_refresh()
	{
		refresh_index(root_item()->path, [this]
		{
			invalidate(invalid::files_populate | invalid::search_populate | invalid::app_title);
		});
		return 0;
	}

	void on_navigate_next(bool forward);

	void load_doc(const index_item_ptr& item);
	void load_doc(const pf::file_path& path);

	bool is_path_modified(const index_item_ptr& item) const override
	{
		return is_item_modified(item);
	}

	bool prompt_save_all_modified()
	{
		if (!has_any_modified())
			return true;

		std::vector<std::string> modified_names;
		collect_modified_names(_root_folder->children, modified_names);

		auto msg = std::string("Save changes to the following files?\n\n");
		for (const auto& name : modified_names)
		{
			msg += "  \u2022 ";
			msg += name;
			msg += "\n";
		}

		const auto id = _app_window->message_box(msg,
		                                         g_app_name,
		                                         pf::msg_box_style::yes_no_cancel |
		                                         pf::msg_box_style::icon_question);

		if (id == pf::msg_box_result::yes)
		{
			save_all();
			return true;
		}
		if (id == pf::msg_box_result::cancel)
		{
			return false;
		}
		// No — discard changes
		return true;
	}

	void save_all()
	{
		save_all_items(_root_folder->children);
		invalidate(invalid::files_layout | invalid::app_title);
	}

	bool save_doc()
	{
		const auto path = pf::save_file_path("Save File", active_item()->path, "");

		return !path.empty() && save_active_doc(path);
	}

	void copy_files_to_folder(const std::vector<pf::file_path>& sources, const pf::file_path& dest_folder) override;
	void delete_item(const index_item_ptr& item) override;
	void rename_item(const index_item_ptr& item, const std::string& new_name) override;
	create_path_result create_new_file(const pf::file_path& folder, std::string content) override;
	create_path_result create_new_folder(const pf::file_path& folder) override;

	void on_idle();


	[[nodiscard]] index_item_ptr active_item() const override { return _active_item; }
	[[nodiscard]] index_item_ptr root_item() const override { return _root_folder; }
	[[nodiscard]] view_styles styles() const override { return _styles; }
	[[nodiscard]] view_mode get_mode() const { return _mode; }
	[[nodiscard]] commands& get_commands() { return _commands; }
	[[nodiscard]] const commands& get_commands() const { return _commands; }
	[[nodiscard]] text_view_ptr focused_text_view() const;
	[[nodiscard]] bool list_has_focus() const;
	[[nodiscard]] bool file_list_has_focus() const;
	[[nodiscard]] bool search_list_has_focus() const;
	[[nodiscard]] list_view_item_ptr selected_file_list_item() const;
	[[nodiscard]] list_view_item_ptr selected_search_list_item() const;
	[[nodiscard]] bool can_copy_current_focus() const;
	[[nodiscard]] bool can_delete_current_focus() const;
	[[nodiscard]] bool copy_current_focus_to_clipboard() const;
	[[nodiscard]] bool delete_current_focus();
	[[nodiscard]] bool can_rename_selected_file() const;
	void begin_rename_selected_file();

	void invalidate(const uint32_t i) override
	{
		_invalid |= i;
	}

	[[nodiscard]] uint32_t validate()
	{
		return _invalid.exchange(0);
	}

	[[nodiscard]] document_ptr& doc() { return _active_item->doc; }
	[[nodiscard]] const document_ptr& doc() const { return _active_item->doc; }

	bool save_active_doc(const pf::file_path& path)
	{
		if (_active_item->doc->save_to_file(path))
		{
			_active_item->doc->path(path);
			_active_item->path = path;
			_active_item->name = path.name();
			return true;
		}
		return false;
	}

	[[nodiscard]] bool has_any_modified() const
	{
		return any_doc_modified(_root_folder->children);
	}

	[[nodiscard]] bool is_item_modified(const index_item_ptr& item) const
	{
		if (!item->path.is_save_path())
			return true;
		if (item->doc)
			return item->doc->is_modified();
		return false;
	}

	void update_styles();

	void on_scale(const double scale_factor)
	{
		_styles.dpi_scale = scale_factor;
		_panel_splitter.set_dpi_scale(scale_factor);
		update_styles();
	}

	void on_zoom(int delta, zoom_target target) override;

	void initialize_styles(const int lh, const int th)
	{
		_styles.list_font_height = std::clamp(lh, 8, 72);
		_styles.text_font_height = std::clamp(th, 8, 72);
		update_styles();
	}

	pf::file_path save_folder() const override;

	static void map_index_items_recursive(std::unordered_map<pf::file_path, index_item_ptr, pf::ihash>& items_by_path,
	                                      const index_item_ptr& item)
	{
		items_by_path[item->path] = item;

		for (const auto& i : item->children)
		{
			map_index_items_recursive(items_by_path, i);
		}
	}

	static index_item_ptr make_item(const std::unordered_map<pf::file_path, index_item_ptr, pf::ihash>& existing,
	                                pf::file_path path, bool is_folder)
	{
		auto item = std::make_shared<index_item>(path, std::string(path.name()), is_folder);

		const auto found = existing.find(path);
		if (found != existing.end())
		{
			item->doc = found->second->doc;
			item->saved_view_content = found->second->saved_view_content;
		}

		return item;
	}

	static index_item_ptr load_index(const pf::file_path& root_path,
	                                 const std::unordered_map<pf::file_path, index_item_ptr, pf::ihash>& existing)
	{
		index_item_ptr root = make_item(existing, root_path, true);
		std::vector<index_item_ptr> folders_to_load{root};

		while (!folders_to_load.empty())
		{
			const auto current = folders_to_load.back();
			folders_to_load.pop_back();

			const auto contents = pf::iterate_file_items(current->path, false);
			current->children.clear();

			for (const auto& f : contents.folders)
			{
				auto item = make_item(existing, f.path, true);
				item->is_folder = true;
				current->children.push_back(item);
				folders_to_load.push_back(item);
			}

			for (const auto& f : contents.files)
			{
				auto item = make_item(existing, f.path, false);
				item->is_folder = false;
				current->children.push_back(item);
			}

			std::ranges::sort(current->children, [](const index_item_ptr& l, const index_item_ptr& r)
			{
				if (l->is_folder != r->is_folder) return l->is_folder > r->is_folder;
				return pf::icmp(l->name, r->name) < 0;
			});
		}

		return root;
	}

	void refresh_index(const pf::file_path& root_path, std::function<void()> on_complete = {},
	                   bool preserve_in_memory_documents = true);

	void set_root(const index_item_ptr& root)
	{
		_root_folder = root;
	}

	// ── Search ─────────────────────────────────────────────────────────────

	struct search_input
	{
		pf::file_path path;
		document_ptr doc;
	};

	using search_results_map = std::unordered_map<pf::file_path, std::vector<search_result>, pf::ihash>;

	static constexpr int max_search_results = 5000;

	void execute_search(const std::string& text, std::function<void()> on_complete = {});
	static search_results_map perform_search(const std::vector<search_input>& inputs, const std::string& text);

private:
	[[nodiscard]] std::string relative_name(const pf::file_path& path) const
	{
		auto rel_name = std::string(path.view());
		const auto root_view = _root_folder->path.view();
		if (rel_name.length() > root_view.length() && pf::icmp(
			std::string_view(rel_name).substr(0, root_view.length()), root_view) == 0)
		{
			rel_name = rel_name.substr(root_view.length());
			if (!rel_name.empty() && (rel_name.starts_with(u8'\\') || rel_name.starts_with(u8'/')))
				rel_name = rel_name.substr(1);
		}
		return rel_name;
	}

	static bool any_doc_modified(const std::vector<index_item_ptr>& items)
	{
		for (const auto& item : items)
		{
			if (item->doc && item->doc->is_modified())
				return true;
			if (item->is_folder && !item->children.empty() && any_doc_modified(item->children))
				return true;
		}
		return false;
	}

	static void collect_modified_names(const std::vector<index_item_ptr>& items,
	                                   std::vector<std::string>& names)
	{
		for (const auto& item : items)
		{
			if (item->doc && item->doc->is_modified())
				names.push_back(item->name);
			if (item->is_folder && !item->children.empty())
				collect_modified_names(item->children, names);
		}
	}

	static void save_all_items(const std::vector<index_item_ptr>& items)
	{
		for (const auto& item : items)
		{
			if (item->doc && item->doc->is_modified() &&
				!item->path.empty() && item->path.is_save_path())
				item->doc->save_to_file(item->path);
			if (item->is_folder && !item->children.empty())
				save_all_items(item->children);
		}
	}

	static index_item_ptr find_first_saved_file(const index_item_ptr& node)
	{
		if (!node) return nullptr;
		if (!node->is_folder && node->path.is_save_path())
			return node;
		for (const auto& child : node->children)
		{
			if (auto found = find_first_saved_file(child))
				return found;
		}
		return nullptr;
	}

	static void collect_search_inputs(const std::vector<index_item_ptr>& items, std::vector<search_input>& inputs)
	{
		for (const auto& item : items)
		{
			if (!item->is_folder)
				inputs.push_back({item->path, item->doc});
			if (item->is_folder)
				collect_search_inputs(item->children, inputs);
		}
	}

	static void apply_search_results(const std::vector<index_item_ptr>& items,
	                                 const search_results_map& results)
	{
		for (const auto& item : items)
		{
			if (!item->is_folder)
			{
				const auto it = results.find(item->path);
				item->search_results = (it != results.end()) ? it->second : std::vector<search_result>{};
			}
			apply_search_results(item->children, results);
		}
	}
};
