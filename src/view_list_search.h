// view_list_search.h — Search panel: text input, result display, navigation

#pragma once

#include "commands.h"
#include "view_list.h"


class search_list_view final : public list_view
{
	edit_box_widget _input;

	std::string _last_searched;
	int _result_count = 0;

	pf::irect edit_box_rect() const
	{
		const auto& styles = _events.styles();
		const auto m = styles.edit_box_margin;
		const auto ef = styles.edit_font;
		const auto h = ef.size + styles.edit_box_inner_pad * 2;
		return pf::irect(m, m, _view_extent.cx - m, m + h);
	}

	int edit_inner_pad() const
	{
		return _events.styles().edit_box_inner_pad;
	}

	uint32_t on_timer(pf::window_frame_ptr& window, const uint32_t id) override
	{
		if (_input.on_timer(id))
			window->invalidate_rect(edit_box_rect());
		return 0;
	}

protected:
	std::vector<pf::menu_command> build_context_menu_items(const list_view_item_ptr& hit)
	{
		std::vector<pf::menu_command> items;
		if (!hit || !hit->source)
			return items;

		if (hit->is_group)
		{
			items.emplace_back(hit->expanded ? "Collapse" : "Expand", 0, [this, hit]
			{
				toggle_header_collapse(hit);
			});
			items.emplace_back();
		}
		else
		{
			items.emplace_back("Open Result", 0, [this, hit]
			{
				_events.open_path_and_select(hit->source, hit->line_number, hit->line_match_pos,
				                             hit->text_match_length);
			});
			items.emplace_back();
		}

		items.push_back(_events.command_menu_item(command_id::edit_copy, nullptr,
		                                          [hit] { return hit && hit->source; }, nullptr,
		                                          "Copy &Path"));
		return items;
	}

	int calc_header_height() const
	{
		const auto& styles = _events.styles();
		const auto m = styles.edit_box_margin;
		const auto ef = styles.edit_font;
		const auto h = ef.size + styles.edit_box_inner_pad * 2;
		return m + h + m;
	}

	void draw_header(pf::window_frame_ptr& window, pf::draw_context& dc, const pf::irect& header_rect) override
	{
		dc.fill_solid_rect(header_rect, ui::tool_wnd_clr);

		const auto eb = edit_box_rect();

		// Edit box background
		dc.fill_solid_rect(eb, ui::tool_wnd_clr.darken(16));

		const auto has_focus = window->has_focus();
		edit_box::draw_border(dc, eb, has_focus);

		const auto& styles = _events.styles();
		const auto pad = styles.edit_box_inner_pad;
		const auto text_rect = eb.inflate(-pad, -pad);

		const auto ef = styles.edit_font;

		const auto char_sz = dc.measure_text("X", ef);
		const auto text_y = eb.top + (eb.height() - char_sz.cy) / 2;
		constexpr auto bg_color = ui::tool_wnd_clr.darken(16);

		if (_input.edit.text.empty())
		{
			// Placeholder text — centered in edit box
			constexpr std::string_view placeholder = "Search...";
			const auto ph_sz = dc.measure_text(placeholder, ef);
			const auto ph_x = eb.left + (eb.width() - ph_sz.cx) / 2;
			dc.draw_text(ph_x, text_y, text_rect, placeholder, ef, ui::handle_hover_color, bg_color);
		}
		else
		{
			_input.edit.draw_selection(dc, eb.left + pad, text_y, char_sz.cy, ef);
			dc.draw_text(eb.left + pad, text_y, text_rect, _input.edit.text, ef, ui::text_color, bg_color);
		}

		// Draw result count below edit box
		if (!_last_searched.empty())
		{
			const auto count_text = _result_count >= max_search_results
				                        ? std::format("{} results (limit reached)", _result_count)
				                        : std::format("{} results", _result_count);
			auto count_rect = header_rect;
			count_rect.top = eb.bottom + 2;
			count_rect.left = eb.left + pad;
			if (count_rect.top < count_rect.bottom)
				dc.draw_text(count_rect.left, count_rect.top, count_rect, count_text, styles.list_font,
				             ui::darker_text_color, ui::tool_wnd_clr);
		}

		if (has_focus && _input.caret.visible)
			_input.edit.draw_caret(dc, eb.left + edit_inner_pad(), text_y, char_sz.cy, ef);
	}

	void on_item_selected(const pf::window_frame_ptr& window, const list_view_item_ptr& item,
	                      const bool activated) override
	{
		if (item->is_group && activated)
		{
			toggle_header_collapse(item);
			return;
		}

		if (!item->is_group)
		{
			_events.open_path_and_select(item->source, item->line_number, item->line_match_pos,
			                             item->text_match_length);
		}
	}

	void handle_size(pf::window_frame_ptr& window, const pf::isize extent,
	                 pf::measure_context& measure) override
	{
		_header_height = calc_header_height();
		list_view::handle_size(window, extent, measure);
	}

	void update_focus(pf::window_frame_ptr& window) override
	{
		_input.update_focus(window, window->has_focus());
		list_view::update_focus(window);
	}

	edit_box_widget* active_edit_box() override { return &_input; }

	void on_edit_text_changed() override
	{
		_events.invalidate(invalid::search_layout);
		trigger_search();
	}

	uint32_t on_char(pf::window_frame_ptr& window, const char32_t ch) override
	{
		if (ch == U'\r' || ch == U'\n')
		{
			if (_selected_item && _selected_item->is_group)
			{
				toggle_header_collapse(_selected_item);
			}
			else
			{
				_events.set_focus(view_focus::text);
			}
			return 0;
		}

		const bool modified = _input.on_char(window, ch);
		_events.invalidate(invalid::search_layout);
		if (modified) trigger_search();
		return 0;
	}

	uint32_t on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;

		bool text_modified = false;
		if (_input.on_key_down(window, vk, text_modified))
		{
			_events.invalidate(invalid::search_layout);
			if (text_modified) trigger_search();
			return 0;
		}

		if (vk == pk::Down)
		{
			navigate_next(window, true);
			return 0;
		}

		if (vk == pk::Up)
		{
			navigate_next(window, false);
			return 0;
		}

		if (vk == pk::Escape)
		{
			_events.on_escape();
			return 0;
		}

		return 0;
	}

public:
	search_list_view(app_events& events) : list_view(events)
	{
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

	void trigger_search()
	{
		if (_input.edit.text != _last_searched)
		{
			_last_searched = _input.edit.text;
			_events.on_search(_input.edit.text);
		}
	}

	struct search_key
	{
		pf::file_path path;
		int line_number = -1;
		int match_pos = -1;

		bool operator==(const search_key& other) const
		{
			return path == other.path && line_number == other.line_number && match_pos == other.match_pos;
		}
	};

	struct search_key_hash
	{
		size_t operator()(const search_key& key) const
		{
			size_t h = pf::ihash{}(key.path);
			h ^= std::hash<int>{}(key.line_number) + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= std::hash<int>{}(key.match_pos) + 0x9e3779b9 + (h << 6) + (h >> 2);
			return h;
		}
	};

	std::unordered_map<search_key, list_view_item_ptr, search_key_hash> _key_to_item;

	search_key make_key(const index_item_ptr& i, const search_result& s)
	{
		return {i->path, s.line_number, s.line_match_pos};
	}

	search_key make_key(const index_item_ptr& i)
	{
		return {i->path, -1, -1};
	}

	list_view_item_ptr make_list_item(const index_item_ptr& item, const search_result& s)
	{
		const auto found = _key_to_item.find(make_key(item, s));

		if (found != _key_to_item.end())
		{
			found->second->source = item; // Update source to prevent memory leak
			found->second->name = s.line_text;
			found->second->is_group = false;
			found->second->line_number = s.line_number;
			found->second->line_match_pos = s.line_match_pos;
			found->second->text_match_start = s.text_match_start;
			found->second->text_match_length = s.text_match_length;
			return found->second;
		}

		auto i = std::make_shared<list_view_item>();
		i->name = s.line_text;
		i->source = item;
		i->depth = 0;
		i->is_group = false;
		i->line_number = s.line_number;
		i->line_match_pos = s.line_match_pos;
		i->text_match_start = s.text_match_start;
		i->text_match_length = s.text_match_length;
		return i;
	}

	std::string relative_path(const index_item_ptr& item) const
	{
		const auto root = _events.root_item();
		if (!root) return item->name;
		const auto root_view = root->path.view();
		const auto item_view = item->path.view();
		if (item_view.length() > root_view.length() &&
			pf::icmp(item_view.substr(0, root_view.length()), root_view) == 0)
		{
			auto rel = std::string(item_view.substr(root_view.length()));
			if (!rel.empty() && (rel[0] == '\\' || rel[0] == '/'))
				rel = rel.substr(1);
			return rel;
		}
		return item->name;
	}

	list_view_item_ptr make_list_item(const index_item_ptr& item)
	{
		const auto found = _key_to_item.find(make_key(item));

		if (found != _key_to_item.end())
		{
			found->second->source = item; // prevents keeping old index_item_ptr alive
			found->second->is_group = true;
			found->second->name = std::format("{} ({})", relative_path(item),
			                                  static_cast<int>(item->search_results.size()));
			return found->second;
		}

		auto i = std::make_shared<list_view_item>();
		i->name = std::format("{} ({})", relative_path(item),
		                      static_cast<int>(item->search_results.size()));
		i->source = item;
		i->depth = 0;
		i->is_group = true;
		i->expanded = true;
		return i;
	}

	// Only files with matches get list items; the tree may hold tens of thousands of files
	void map_index_items_recursive(std::unordered_map<search_key, list_view_item_ptr, search_key_hash>& items_by_path,
	                               const index_item_ptr& item)
	{
		if (!item->search_results.empty())
		{
			items_by_path[make_key(item)] = make_list_item(item);

			for (const auto& s : item->search_results)
			{
				items_by_path[make_key(item, s)] = make_list_item(item, s);
			}
		}

		for (const auto& i : item->children)
		{
			map_index_items_recursive(items_by_path, i);
		}
	}

	void build_folder_items(std::vector<list_view_item_ptr>& items,
	                        const std::vector<index_item_ptr>& children)
	{
		for (const auto& child : children)
		{
			if (!child->is_folder && !child->search_results.empty())
			{
				const auto found = _key_to_item.find(make_key(child));
				if (found != _key_to_item.end())
				{
					found->second->depth = 0;
					items.push_back(found->second);

					if (found->second->expanded)
					{
						for (const auto& s : child->search_results)
						{
							auto val = make_list_item(child, s);
							val->depth = 1;
							items.push_back(val);
						}
					}
				}
			}

			build_folder_items(items, child->children);
		}
	}

	void populate()
	{
		std::unordered_map<search_key, list_view_item_ptr, search_key_hash> existing;
		const auto root = _events.root_item();

		if (root)
		{
			map_index_items_recursive(existing, root);
		}

		_key_to_item = std::move(existing);

		std::vector<list_view_item_ptr> items;
		if (root)
			build_folder_items(items, root->children);
		_items = std::move(items);

		_result_count = 0;
		for (const auto& i : _items)
			if (!i->is_group) _result_count++;

		_events.invalidate(invalid::search_layout);
		layout_list();
	}

private:
	void on_context_menu(const pf::window_frame_ptr& window, const pf::ipoint& screen_pt)
	{
		window->set_focus();
		const auto client_pt = window->screen_to_client(screen_pt);
		const auto scroll_pt = pf::ipoint(client_pt.x, client_pt.y + _scroll_offset.y);
		const auto hit = selection_from_point(scroll_pt);
		if (hit)
			select_list_item(window, hit, false);
		const auto items = build_context_menu_items(hit ? hit : _selected_item);
		if (!items.empty())
			window->show_popup_menu(items, screen_pt);
	}

	void toggle_header_collapse(const list_view_item_ptr& header_item)
	{
		const auto si = header_item;
		si->expanded = !si->expanded;
		populate();
	}
};

using search_view_ptr = std::shared_ptr<search_list_view>;
