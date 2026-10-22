/*
 * sort.h -- sort definitions and the row order they produce.
 *
 * A sort is an ordered list of (field, direction) keys.  apply_sort() returns
 * row indices rather than reordering the RecordSet: the file order is identity
 * and every view may sort the same data differently.  The sort is stable, so
 * equal keys keep file order, and rows whose key is Null always go last in both
 * directions (a null key is "no value", not the smallest value).
 *
 * The SortOptions overload adds outline-aware ordering: children stay beneath
 * their parent and only siblings are compared.  See sort.h's SortOptions comment.
 */

#ifndef CORE_VIEW_SORT_H
#define CORE_VIEW_SORT_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "field_ref.h"
#include "json.h"
#include "record_set.h"

namespace core::view {

struct SortKey {
    FieldId field = 0;
    bool ascending = true;
};

struct SortDefinition {
    std::string id;
    std::string name;
    core::model::RecordType record_type = core::model::RecordType::Task;
    std::vector<SortKey> keys;
};

/*
 * Extra ordering rules.  The default (empty options) is the original flat,
 * stable, multi-key sort.  keep_outline re-nests rows by OutlineLevel first and
 * sorts only siblings, so a child can never leave its parent; a grouping step
 * (group.h) is then a flat partition, which is the "grouping flattens" rule.
 *
 * outline_level_field 0 means "resolve Task OutlineLevel by name, and fall back
 * to the flat sort when the RecordSet has no such column".  A null or
 * non-integer outline level is treated as level 1.
 */
struct SortOptions {
    bool keep_outline = false;
    FieldId outline_level_field = 0;
};

std::vector<std::size_t> apply_sort(const SortDefinition& sort, const RecordSet& records);

std::vector<std::size_t> apply_sort(const SortDefinition& sort, const RecordSet& records,
                                    const SortOptions& options);

JsonValue sort_to_json(const SortDefinition& sort);
bool sort_from_json(const JsonValue& json, SortDefinition& sort, std::string_view path,
                    std::string& error);

}  // namespace core::view

#endif  // CORE_VIEW_SORT_H
