/*
 * sort.h -- sort definitions and the row order they produce.
 *
 * A sort is an ordered list of (field, direction) keys.  apply_sort() returns
 * row indices rather than reordering the RecordSet: the file order is identity
 * and every view may sort the same data differently.  The sort is stable, so
 * equal keys keep file order, and rows whose key is Null always go last in both
 * directions (a null key is "no value", not the smallest value).
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

std::vector<std::size_t> apply_sort(const SortDefinition& sort, const RecordSet& records);

JsonValue sort_to_json(const SortDefinition& sort);
bool sort_from_json(const JsonValue& json, SortDefinition& sort, std::string_view path,
                    std::string& error);

}  // namespace core::view

#endif  // CORE_VIEW_SORT_H
