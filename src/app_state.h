// app_state.h — Application state: document collection, file operations, testable app logic

#pragma once

#include "app.h"
#include "document.h"
#include "commands.h"
#include "gitignore.h"
#include "agent_session.h"
#include "agent_host.h"
#include "ui.h"


struct search_result;
class app_state;

class text_view;
class list_view;
class file_list_view;
class search_list_view;
class doc_view;
class agent_view;
struct list_view_item;

using doc_view_ptr = std::shared_ptr<doc_view>;
using text_view_ptr = std::shared_ptr<text_view>;
using folder_view_ptr = std::shared_ptr<file_list_view>;
using search_view_ptr = std::shared_ptr<search_list_view>;
using agent_view_ptr = std::shared_ptr<agent_view>;
using list_view_item_ptr = std::shared_ptr<list_view_item>;


class app_state final : public app_events, public pf::frame_reactor, public std::enable_shared_from_this<app_state>
{
public:
	static constexpr uint64_t max_search_file_size = 10 * 1024 * 1024;
	static constexpr size_t max_recent_root_folders = 8;
	static constexpr uint32_t search_debounce_timer_id = 2001;
	static constexpr uint32_t search_debounce_ms = 150;

	// Unmodified documents past this count are dropped and reloaded on demand
	static constexpr size_t max_resident_documents = 24;

	pf::window_frame_ptr _app_window;
	pf::window_frame_ptr _doc_window;
	pf::window_frame_ptr _list_window;
	pf::window_frame_ptr _agent_window;

	doc_view_ptr _doc_view;
	folder_view_ptr _files_view;
	search_view_ptr _search_view;
	agent_view_ptr _agent_view;

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
	splitter _agent_splitter{splitter::orientation::vertical, 0.72};
	bool _agent_visible = false;

	// The document pane never shrinks below this, however the splitters are dragged
	[[nodiscard]] int min_pane_width() const { return static_cast<int>(160 * _styles.dpi_scale); }

	void toggle_agent_panel();
	void show_agent_panel(bool visible);
	void focus_agent_input();
	void on_agent_input(std::string text);
	void clear_agent_session();
	[[nodiscard]] bool agent_has_focus() const;
	[[nodiscard]] index_item_ptr session_item();
	void append_agent_lines(const std::vector<std::string>& lines);
	[[nodiscard]] pf::irect agent_splitter_bounds(const pf::irect& bounds) const;

	index_item_ptr _session_item;
	std::shared_ptr<document_events> _agent_doc_events;
	std::shared_ptr<agent_host::events> _agent_sink;
	std::unique_ptr<agent_host> _agent_host;
	std::string _agent_status;

	[[nodiscard]] std::string_view agent_status_text() const override { return _agent_status; }
	void apply_transcript_change(int first, std::span<const std::string> replacement);
	void ensure_agent_host();

	std::vector<command_def> make_commands();
	explicit app_state(async_scheduler_ptr scheduler);

	std::vector<pf::menu_command> build_menu();
	pf::menu_command command_menu_item(command_id id,
	                                   std::function<void()> action_override = nullptr,
	                                   std::function<bool()> is_enabled_override = nullptr,
	                                   std::function<bool()> is_checked_override = nullptr,
	                                   std::string text_override = {}) const override;

	void ensure_visible(const text_location& pt) override;

	void invalidate_lines(int start, int end) override;
	void lines_changed(int start, int end) override;
	void line_count_changed(int at, int delta) override;

	void path_selected(const index_item_ptr& item) override
	{
		if (pf::is_directory(item->path))
			return;
		load_doc(item);
	}

	// The document may still be loading, so the match is selected from the load continuation
	void open_path_and_select(const index_item_ptr& item, int line, int col, int length) override;

	void set_focus(view_focus v) override;

	void set_mode(view_mode m) override;

	void on_search(const std::string& text) override;
	void run_pending_search();

	void toggle_search_mode();

	// The list panel that currently has focus, and the one whose inline edit box is active
	[[nodiscard]] list_view* focused_list_view() const;
	[[nodiscard]] list_view* focused_edit_box_owner() const;
	[[nodiscard]] bool word_wrap() const { return _word_wrap; }
	void set_word_wrap(bool enabled);
	void toggle_word_wrap() { set_word_wrap(!_word_wrap); }

	void set_message(std::string text);

	uint32_t handle_message(pf::window_frame_ptr window, pf::message_type msg,
	                        const pf::message_params& params) override;

	uint32_t handle_mouse(pf::window_frame_ptr window, pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override;

	uint32_t on_create(const pf::window_frame_ptr& window);

	uint32_t on_window_dpi_changed(const pf::message_params& params)
	{
		on_scale(params.dpi_scale);

		if (params.suggested_bounds.width() > 0)
			_app_window->move_window(params.suggested_bounds);

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

	struct recent_root_entry
	{
		pf::file_path folder;
		pf::file_path document;
	};

	// Entries arrive most-recent-first, the order they are written to config
	void restore_recent_root_folders(std::span<const recent_root_entry> entries);
	void update_recent_root_menu();
	[[nodiscard]] std::vector<pf::menu_command> build_recent_root_folder_menu();
	[[nodiscard]] const std::vector<pf::file_path>& recent_root_folders() const { return _recent_root_folders; }
	[[nodiscard]] const std::vector<pf::file_path>& recent_root_documents() const { return _recent_root_documents; }

	[[nodiscard]] pf::file_path recent_root_document(const pf::file_path& folder) const
	{
		for (size_t i = 0; i < _recent_root_folders.size(); ++i)
		{
			if (_recent_root_folders[i] == folder)
				return i < _recent_root_documents.size() ? _recent_root_documents[i] : pf::file_path{};
		}
		return {};
	}

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

	uint32_t on_refresh_focused_panel()
	{
		// F5 refreshes whatever the focused panel shows
		if (search_list_has_focus() && !_pending_search_text.empty())
		{
			run_pending_search();
			return 0;
		}
		return on_refresh();
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

	void calc_selection();

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

	void load_doc(const index_item_ptr& item, std::function<void()> on_loaded = {});
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
	void show_generated_document(const pf::file_path& path, std::string content);

	void on_idle();


	[[nodiscard]] index_item_ptr active_item() const override { return _active_item; }
	[[nodiscard]] index_item_ptr root_item() const override { return _root_folder; }
	[[nodiscard]] const view_styles& styles() const override { return _styles; }
	[[nodiscard]] view_mode get_mode() const { return _mode; }
	[[nodiscard]] commands& get_commands() { return _commands; }
	[[nodiscard]] const commands& get_commands() const { return _commands; }
	[[nodiscard]] text_view_ptr focused_text_view() const;
	[[nodiscard]] bool list_has_focus() const;
	[[nodiscard]] bool file_list_has_focus() const;
	[[nodiscard]] bool search_list_has_focus() const;
	[[nodiscard]] bool inline_edit_has_focus() const;
	[[nodiscard]] list_view_item_ptr selected_file_list_item() const;
	[[nodiscard]] list_view_item_ptr selected_search_list_item() const;
	[[nodiscard]] bool can_copy_current_focus() const;
	[[nodiscard]] bool can_delete_current_focus() const;
	[[nodiscard]] bool copy_current_focus_to_clipboard() const;
	[[nodiscard]] bool delete_current_focus();
	[[nodiscard]] bool can_rename_selected_file() const;
	// True when a text-editing command should apply to the document pane
	[[nodiscard]] bool can_edit_document() const;
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

	// False when the file changed on disk since it was loaded and the user declined to overwrite
	[[nodiscard]] bool confirm_overwrite_external_changes(const pf::file_path& path) const
	{
		const auto& d = _active_item->doc;
		if (!d || !_app_window || !(path == _active_item->path))
			return true;

		const auto loaded_time = d->disk_modified_time();
		if (loaded_time <= 1)
			return true;

		const auto current_time = pf::file_modified_time(path);
		if (current_time == 0 || current_time == loaded_time)
			return true;

		return _app_window->message_box(
			"This file has been modified on disk since it was opened. Overwrite those changes?",
			g_app_name,
			pf::msg_box_style::yes_no | pf::msg_box_style::icon_question) == pf::msg_box_result::yes;
	}

	bool save_active_doc(const pf::file_path& path)
	{
		if (!confirm_overwrite_external_changes(path))
			return false;

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
		return item->doc && item->doc->is_modified();
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

	// index_snapshot — Plain copy of the per-path state the index loader needs.
	// The loader runs on a worker thread and must never touch live index_item objects.
	struct index_snapshot
	{
		document_ptr doc;
		view_content saved_view_content = view_content::none;
	};

	using index_snapshot_map = std::unordered_map<pf::file_path, index_snapshot, pf::ihash>;

	static void snapshot_index_items_recursive(index_snapshot_map& items_by_path,
	                                           const index_item_ptr& item)
	{
		items_by_path[item->path] = {item->doc, item->saved_view_content};

		for (const auto& i : item->children)
		{
			snapshot_index_items_recursive(items_by_path, i);
		}
	}

	static void map_index_items_recursive(std::unordered_map<pf::file_path, index_item_ptr, pf::ihash>& items_by_path,
	                                      const index_item_ptr& item)
	{
		items_by_path[item->path] = item;

		for (const auto& i : item->children)
		{
			map_index_items_recursive(items_by_path, i);
		}
	}

	static index_item_ptr make_item(const index_snapshot_map& existing,
	                                pf::file_path path, bool is_folder)
	{
		auto item = std::make_shared<index_item>(path, std::string(path.name()), is_folder);

		const auto found = existing.find(path);
		if (found != existing.end())
		{
			item->doc = found->second.doc;
			item->saved_view_content = found->second.saved_view_content;
		}

		return item;
	}

	// Reads a whole small file; used for .gitignore on the indexing thread
	static std::string read_text_file(const pf::file_path& path)
	{
		const auto handle = pf::open_for_read(path);
		if (!handle)
			return {};

		const auto size = handle->size();
		if (size == 0 || size > 1024 * 1024)
			return {};

		std::string text(size, '\0');
		uint32_t total = 0;

		while (total < size)
		{
			uint32_t read = 0;
			if (!handle->read(reinterpret_cast<uint8_t*>(text.data()) + total, size - total, &read) || read == 0)
				break;
			total += read;
		}

		text.resize(total);
		return text;
	}

	static index_item_ptr load_index(const pf::file_path& root_path,
	                                 const index_snapshot_map& existing)
	{
		index_item_ptr root = make_item(existing, root_path, true);

		struct pending_folder
		{
			index_item_ptr item;
			std::string relative; // '/' separated, relative to the root, empty at the root
		};

		gitignore_rules ignores;
		std::vector<pending_folder> folders_to_load{{root, {}}};

		const auto join_relative = [](const std::string_view base, const std::string_view name)
		{
			return base.empty() ? std::string(name) : std::format("{}/{}", base, name);
		};

		while (!folders_to_load.empty())
		{
			const auto current = folders_to_load.back();
			folders_to_load.pop_back();

			// A folder's own .gitignore applies to everything below it
			if (const auto text = read_text_file(current.item->path.combine(".gitignore")); !text.empty())
				ignores.add_file(text, current.relative);

			const auto contents = pf::iterate_file_items(current.item->path, false);
			current.item->children.clear();

			for (const auto& f : contents.folders)
			{
				const auto name = f.path.name();
				if (pf::icmp(name, ".git") == 0)
					continue;

				const auto relative = join_relative(current.relative, name);
				if (ignores.is_ignored(relative, true))
					continue;

				auto item = make_item(existing, f.path, true);
				item->is_folder = true;
				current.item->children.push_back(item);
				folders_to_load.push_back({item, relative});
			}

			for (const auto& f : contents.files)
			{
				if (ignores.is_ignored(join_relative(current.relative, f.path.name()), false))
					continue;

				auto item = make_item(existing, f.path, false);
				item->is_folder = false;
				current.item->children.push_back(item);
			}

			std::ranges::sort(current.item->children, [](const index_item_ptr& l, const index_item_ptr& r)
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
		document_ptr doc; // only used for direct in-process searches (tests); never crosses a thread
		std::vector<std::string> lines; // UI-thread snapshot of a modified document
		bool has_snapshot = false;
	};

	using search_results_map = std::unordered_map<pf::file_path, std::vector<search_result>, pf::ihash>;
	using path_set = std::unordered_set<pf::file_path, pf::ihash, pf::ieq>;

	void execute_search(const std::string& text, std::function<void()> on_complete = {});
	static search_results_map perform_search(const std::vector<search_input>& inputs, const std::string& text,
	                                         const std::function<bool()>& is_cancelled = {});

private:
	bool _word_wrap = true;
	std::string _pending_search_text;
	std::atomic<uint32_t> _search_generation = 0;

	// Bumped by any edit or reindex; a search may only be narrowed while it is unchanged
	uint32_t _content_generation = 0;
	uint32_t _searched_generation = 0;
	std::string _searched_text;
	std::vector<pf::file_path> _searched_matches;

	uint64_t _use_counter = 0;

public:
	void note_content_changed() { ++_content_generation; }
	void evict_unused_documents();
	[[nodiscard]] size_t resident_document_count() const;

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

	static std::vector<std::string> snapshot_document_lines(const document& d)
	{
		std::vector<std::string> lines;
		lines.reserve(d.size());

		for (int i = 0; i < static_cast<int>(d.size()); i++)
		{
			std::string line_text;
			d[i].render(line_text);
			lines.push_back(std::move(line_text));
		}

		return lines;
	}

	// Snapshots unsaved edits so the search worker never reads a live document.
	// Unmodified documents match the file on disk, so the worker reads those instead.
	static void collect_search_inputs(const std::vector<index_item_ptr>& items, std::vector<search_input>& inputs,
	                                  const path_set* only = nullptr)
	{
		for (const auto& item : items)
		{
			if (item->is_folder)
			{
				collect_search_inputs(item->children, inputs, only);
				continue;
			}

			if (only && !only->contains(item->path))
				continue;

			search_input input;
			input.path = item->path;

			if (item->doc && (item->doc->is_modified() || !item->path.is_save_path()))
			{
				input.lines = snapshot_document_lines(*item->doc);
				input.has_snapshot = true;
			}

			inputs.push_back(std::move(input));
		}
	}

	void collect_evictable_documents(const index_item_ptr& item, std::vector<index_item_ptr>& out) const
	{
		// Modified work is unsaved, and a generated document has a path but no matching file,
		// so neither can be recovered by reloading
		if (item->doc && item != _active_item && !item->doc->is_modified()
			&& !item->doc->is_read_only() && item->path.exists())
			out.push_back(item);

		for (const auto& child : item->children)
			collect_evictable_documents(child, out);
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
