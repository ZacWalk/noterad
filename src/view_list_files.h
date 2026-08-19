// view_list_files.h — Folder browser panel: tree navigation, expand/collapse, item rendering

#pragma once

#include "commands.h"
#include "view_list.h"


class file_list_view final : public list_view
{
	edit_box_widget _rename_input;
	list_view_item_ptr _renaming_item;

public:
	[[nodiscard]] bool is_renaming() const { return _renaming_item != nullptr; }

private:
	void begin_rename(const pf::window_frame_ptr& window, const list_view_item_ptr& item)
	{
		if (!item || !item->source || item->is_group)
			return;

		_renaming_item = item;
		_rename_input.edit.text = item->source->name;
		_rename_input.edit.sel_anchor = 0;
		_rename_input.edit.cursor_pos = static_cast<int>(item->source->name.size());

		// Select name without extension
		const auto dot = pf::file_path::find_ext(item->source->name);
		if (dot < item->source->name.size())
			_rename_input.edit.cursor_pos = static_cast<int>(dot);

		_rename_input.update_focus(window, true);
		window->invalidate();
	}

	void commit_rename(const pf::window_frame_ptr& window)
	{
		if (!_renaming_item)
			return;

		const auto new_name = _rename_input.edit.text;
		const auto item = _renaming_item;
		cancel_rename(window);

		if (!new_name.empty() && new_name != item->source->name)
			_events.rename_item(item->source, new_name);
	}

	void cancel_rename(const pf::window_frame_ptr& window)
	{
		_renaming_item = nullptr;
		_rename_input.caret.stop(window);
		window->invalidate();
	}

protected:
	std::vector<pf::menu_command> build_context_menu_items(const pf::window_frame_ptr& window,
	                                                       const list_view_item_ptr& hit)
	{
		std::vector<pf::menu_command> items;

		items.emplace_back("New File", 0, [this, hit]
		{
			const pf::file_path save_folder = get_save_folder(hit);
			_events.create_new_file(save_folder.combine("new-file", ".md"), {});
		}, [] { return true; });

		items.emplace_back("New Folder", 0, [this, hit]
		{
			const pf::file_path save_folder = get_save_folder(hit);
			_events.create_new_folder(save_folder);
		}, [] { return true; });

		items.emplace_back();
		items.push_back(_events.command_menu_item(command_id::edit_copy, nullptr,
		                                          [hit] { return hit && hit->source; }, nullptr,
		                                          "Copy &Path"));
		items.emplace_back("Rename", 0, [this, window]
		                   {
			                   begin_selected_rename(window);
		                   }, [hit] { return hit && hit->source && !hit->source->is_folder; }, nullptr,
		                   pf::key_binding{pf::platform_key::F2, pf::key_mod::none});
		items.push_back(_events.command_menu_item(command_id::edit_delete, nullptr,
		                                          [hit] { return hit && hit->source && !hit->source->is_folder; }));

		return items;
	}

	uint32_t on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;

		if (is_renaming())
		{
			if (vk == pk::Return)
			{
				commit_rename(window);
				return 0;
			}
			if (vk == pk::Escape)
			{
				cancel_rename(window);
				return 0;
			}
			bool text_modified = false;
			if (_rename_input.on_key_down(window, vk, text_modified))
			{
				window->invalidate();
				return 0;
			}
			return 0;
		}

		if (vk == pk::F2)
		{
			if (_selected_item && !_selected_item->is_group)
				begin_rename(window, _selected_item);
			return 0;
		}

		if (vk == pk::Return)
		{
			if (_selected_item)
			{
				on_item_selected(window, _selected_item, true);
				if (!_selected_item->is_group)
					_events.set_focus(view_focus::text);
			}
			return 0;
		}

		return list_view::on_key_down(window, vk);
	}

	void on_item_selected(const pf::window_frame_ptr& window, const list_view_item_ptr& item,
	                      const bool activated) override
	{
		if (is_renaming())
			cancel_rename(window);

		if (item->is_group)
		{
			if (activated)
			{
				item->expanded = !item->expanded;
				populate(window);
			}
		}
		else
		{
			_events.path_selected(item->source);
		}
	}

	uint32_t on_char(pf::window_frame_ptr& window, const char32_t ch) override
	{
		if (is_renaming())
		{
			if (ch == U'\r' || ch == U'\n')
				return 0; // handled by on_key_down
			_rename_input.on_char(window, ch);
			window->invalidate();
		}
		return 0;
	}

	uint32_t on_timer(pf::window_frame_ptr& window, const uint32_t id) override
	{
		if (is_renaming() && _rename_input.on_timer(id))
			window->invalidate();
		return 0;
	}

	void update_focus(pf::window_frame_ptr& window) override
	{
		if (is_renaming() && window && !window->has_focus())
			cancel_rename(window);
		list_view::update_focus(window);
	}

	edit_box_widget* active_edit_box() override { return is_renaming() ? &_rename_input : nullptr; }

public:
	file_list_view(app_events& events) : list_view(events)
	{
	}

	void begin_selected_rename(const pf::window_frame_ptr& window)
	{
		if (_selected_item && !_selected_item->is_group)
			begin_rename(window, _selected_item);
	}

	uint32_t handle_message(pf::window_frame_ptr window, const pf::message_type msg,
	                        const pf::message_params& params) override
	{
		if (msg == pf::message_type::drop_files)
		{
			if (!params.dropped_paths.empty())
			{
				const auto dest = get_save_folder(_selected_item);
				_events.copy_files_to_folder(
					{params.dropped_paths.begin(), params.dropped_paths.end()}, dest);
			}
			return 0;
		}
		return list_view::handle_message(std::move(window), msg, params);
	}

	uint32_t handle_mouse(pf::window_frame_ptr window, const pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override
	{
		if (msg == pf::mouse_message_type::context_menu)
		{
			on_context_menu(window, params.point);
			return 0;
		}
		return list_view::handle_mouse(std::move(window), msg, params);
	}

	void handle_paint(pf::window_frame_ptr& window, pf::draw_context& dc) override
	{
		list_view::handle_paint(window, dc);

		if (is_renaming())
		{
			const auto bounds = _renaming_item->bounds.offset(0, -_scroll_offset.y);

			const auto& styles = _events.styles();
			const auto indent = styles.padding_x + _renaming_item->depth * styles.indent + 4;
			auto edit_rect = bounds;
			edit_rect.left += indent;
			edit_rect = edit_rect.inflate(-styles.padding_x, 0);

			// Background
			constexpr auto bg_color = ui::tool_wnd_clr.darken(16);
			dc.fill_solid_rect(edit_rect, bg_color);
			edit_box::draw_border(dc, edit_rect, true, styles.dpi_scale);

			const auto pad = styles.edit_box_inner_pad;
			const auto font = styles.list_font;
			const auto char_sz = dc.measure_text("X", font);
			const auto text_y = edit_rect.top + (edit_rect.height() - char_sz.cy) / 2;
			const auto text_x = edit_rect.left + pad;

			const auto text_rect = edit_rect.inflate(-pad, -pad);

			_rename_input.edit.draw_selection(dc, text_x, text_y, char_sz.cy, font);
			dc.draw_text(text_x, text_y, text_rect, _rename_input.edit.text, font, ui::text_color, bg_color);

			if (_rename_input.caret.visible)
				_rename_input.edit.draw_caret(dc, text_x, text_y, char_sz.cy, font, styles.dpi_scale);
		}
	}

	pf::file_path get_save_folder(const list_view_item_ptr& hit)
	{
		pf::file_path context_folder;

		if (hit && hit->source)
		{
			if (hit->source->is_folder)
				context_folder = hit->source->path;
			else
				context_folder = hit->source->path.folder();
		}

		if (!context_folder.exists())
		{
			context_folder = _events.save_folder();
		}
		return context_folder;
	}

	void on_context_menu(const pf::window_frame_ptr& window, const pf::ipoint& screen_pt)
	{
		window->set_focus();
		const auto client_pt = window->screen_to_client(screen_pt);
		const auto scroll_pt = pf::ipoint(client_pt.x, client_pt.y + _scroll_offset.y);
		const auto hit = selection_from_point(scroll_pt);

		if (hit)
			select_list_item(window, hit, false);

		const auto items = build_context_menu_items(window, hit ? hit : _selected_item);
		window->show_popup_menu(items, screen_pt);
	}

	static bool compare_items(const list_view_item_ptr& l, const list_view_item_ptr& r)
	{
		if (l->is_group != r->is_group) return l->is_group > r->is_group;
		return pf::icmp(l->name, r->name) < 0;
	}

	list_view_item_ptr make_list_item(const index_item_ptr& src)
	{
		const auto found = _path_to_item.find(src->path);

		if (found != _path_to_item.end())
		{
			// Update the source to point to the new index_item tree to prevent leaking the old tree
			found->second->source = src;
			found->second->name = src->name;
			found->second->is_group = src->is_folder;
			return found->second;
		}

		auto i = std::make_shared<list_view_item>();
		i->name = src->name;
		i->source = src;
		i->depth = 0;
		i->is_group = src->is_folder;
		return i;
	}

	void map_index_items_recursive(std::unordered_map<pf::file_path, list_view_item_ptr, pf::ihash>& items_by_path,
	                               const index_item_ptr& item)
	{
		items_by_path[item->path] = make_list_item(item);

		for (const auto& i : item->children)
		{
			map_index_items_recursive(items_by_path, i);
		}
	}

	void build_folder_items(std::vector<list_view_item_ptr>& items,
	                        const std::vector<index_item_ptr>& children, const int depth)
	{
		for (const auto& child : children)
		{
			auto found = _path_to_item.find(child->path);

			if (found != _path_to_item.end())
			{
				found->second->depth = depth;
				items.push_back(found->second);

				if (found->second->expanded)
					build_folder_items(items, child->children, depth + 1);
			}
		}
	}

	std::unordered_map<pf::file_path, list_view_item_ptr, pf::ihash> _path_to_item;

	void update_selected(const pf::window_frame_ptr& window)
	{
		const auto active = _events.active_item();
		_selected_index = -1;
		_selected_item = nullptr;

		for (int i = 0; i < static_cast<int>(_items.size()); i++)
		{
			if (_items[i]->source == active)
			{
				set_selected(i);
				break;
			}
		}

		ensure_visible(window, _selected_item);
	}

	void populate(const pf::window_frame_ptr& window)
	{
		std::unordered_map<pf::file_path, list_view_item_ptr, pf::ihash> existing;
		const auto root = _events.root_item();

		if (root)
		{
			map_index_items_recursive(existing, root);
		}

		_path_to_item = std::move(existing);

		std::vector<list_view_item_ptr> items;
		if (root)
			build_folder_items(items, root->children, 0);

		_items = std::move(items);

		update_selected(window);
		layout_list();
		window->invalidate();
	}

	bool expand_path_to(const index_item_ptr& node, const index_item_ptr& target)
	{
		for (const auto& child : node->children)
		{
			if (child == target)
				return true;

			if (child->is_folder && expand_path_to(child, target))
			{
				const auto found = _path_to_item.find(child->path);
				if (found != _path_to_item.end())
					found->second->expanded = true;
				return true;
			}
		}
		return false;
	}

	void select_index_item(const pf::window_frame_ptr& window, const index_item_ptr& item)
	{
		for (int i = 0; i < static_cast<int>(_items.size()); i++)
		{
			if (_items[i]->source == item)
			{
				if (_selected_item != _items[i])
				{
					set_selected(i);
					ensure_visible(window, _selected_item);
					window->invalidate();
					on_item_selected(window, _selected_item, false);
				}
				return;
			}
		}

		// Item not visible — expand ancestor folders to reveal it
		const auto root = _events.root_item();
		if (root && expand_path_to(root, item))
		{
			populate(window);

			for (int i = 0; i < static_cast<int>(_items.size()); i++)
			{
				if (_items[i]->source == item)
				{
					set_selected(i);
					ensure_visible(window, _selected_item);
					window->invalidate();
					on_item_selected(window, _selected_item, false);
					return;
				}
			}
		}
	}
};

using folder_view_ptr = std::shared_ptr<file_list_view>;
