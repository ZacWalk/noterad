// commands.cpp — Command system implementation

#include "pch.h"
#include "commands.h"

#include "app_state.h"

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
