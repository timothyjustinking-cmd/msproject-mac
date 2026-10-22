/*
 * view_set.h -- all view definitions, and the JSON container that stores them.
 *
 * The single file format is what a test can paste and what the app can persist
 * per project:
 *
 * {
 *   "version": 1,
 *   "tables": [ ... ], "filters": [ ... ],
 *   "groups": [ ... ], "sorts": [ ... ]
 * }
 *
 * Every array is optional and may be empty.  Unknown keys are ignored (forward
 * compatibility); the version number is checked when present so that an
 * incompatible future format fails loudly rather than silently dropping data.
 */

#ifndef CORE_VIEW_VIEW_SET_H
#define CORE_VIEW_VIEW_SET_H

#include <string>
#include <string_view>
#include <vector>

#include "filter.h"
#include "group.h"
#include "json.h"
#include "sort.h"
#include "table.h"

namespace core::view {

struct ViewSet {
    std::vector<TableDefinition> tables;
    std::vector<FilterDefinition> filters;
    std::vector<GroupDefinition> groups;
    std::vector<SortDefinition> sorts;

    const TableDefinition* find_table(std::string_view id) const noexcept;
    const FilterDefinition* find_filter(std::string_view id) const noexcept;
    const GroupDefinition* find_group(std::string_view id) const noexcept;
    const SortDefinition* find_sort(std::string_view id) const noexcept;
};

JsonValue view_set_to_json(const ViewSet& set);
bool view_set_from_json(const JsonValue& json, ViewSet& set, std::string& error);

/* Convenience wrappers over parse_json/write_json. */
bool view_set_parse(std::string_view text, ViewSet& set, std::string& error);
std::string view_set_write(const ViewSet& set, bool pretty = true);

}  // namespace core::view

#endif  // CORE_VIEW_VIEW_SET_H
