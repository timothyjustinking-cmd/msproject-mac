/*
 * group.h -- grouping definitions and the ordered buckets they produce.
 *
 * A group is a tree of keys (group by Resource, then by Duration interval, ...).
 * The definition is data; build_groups() partitions row indices into buckets and
 * computes the label each heading shows.  No summary values are computed here:
 * summing Work or Cost needs the scheduling/rollup layer, not the view layer.
 *
 * INTERVALS are only meaningful for Date fields and Percent; a non-date key
 * with an interval falls back to the ungrouped value rather than inventing a
 * bucket.  Week labels name the week's Monday ("week of 2024-01-01") instead of
 * an ISO week number, because the ISO week-year edge cases are not worth making
 * up an encoding for before the oracle tests exist.
 *
 * ORDERING: buckets are ordered by their key value (ascending or descending as
 * the key says).  A Null key is always last.  Within a bucket, rows keep the
 * input order, so callers sort first (sort.h) and group second, which is the
 * order MS Project applies too.
 */

#ifndef CORE_VIEW_GROUP_H
#define CORE_VIEW_GROUP_H

#include <string>
#include <string_view>
#include <vector>

#include "field_ref.h"
#include "json.h"
#include "record_set.h"
#include "value.h"

namespace core::view {

enum class GroupInterval : std::uint8_t {
    None = 0,
    Day,
    Week,
    Month,
    Quarter,
    Year,
    Percent,
};

const char* group_interval_name(GroupInterval interval) noexcept;
bool group_interval_from_name(std::string_view name, GroupInterval& out) noexcept;

struct GroupKey {
    FieldId field = 0;
    GroupInterval interval = GroupInterval::None;
    bool ascending = true;
};

struct GroupDefinition {
    std::string id;
    std::string name;
    core::model::RecordType record_type = core::model::RecordType::Task;
    std::vector<GroupKey> keys;
    bool show_summary = true;
};

struct GroupBucket {
    Value key;                   // raw value; Null for the "no value" bucket
    std::string label;           // heading text
    std::vector<std::size_t> rows;
    std::vector<GroupBucket> children;
};

/* Recursive bucketing by group.keys[level..]. */
std::vector<GroupBucket> build_groups(const GroupDefinition& group, const RecordSet& records);

/* Label for a single key value at one interval (also used by tests). */
std::string group_value_label(const Value& value, GroupInterval interval);

JsonValue group_to_json(const GroupDefinition& group);
bool group_from_json(const JsonValue& json, GroupDefinition& group, std::string_view path,
                     std::string& error);

}  // namespace core::view

#endif  // CORE_VIEW_GROUP_H
