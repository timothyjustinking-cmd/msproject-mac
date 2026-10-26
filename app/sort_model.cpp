#include "sort_model.h"

#include <cstdint>

#include "layout_model.h"  // app::task_field_id

namespace app {
namespace {

// The RecordSet's OutlineLevel is the sheet's 0-based port level (top level 0),
// while core::view::apply_sort's outline tree expects Project's 1-based
// OutlineLevel (top level 1, children 2, ...): its clamp turns every raw value
// below 1 into 1, so feeding it the 0-based column would make top-level rows
// and their children siblings.  The sort therefore runs on a short-lived copy
// whose app-only column carries level + 1; nothing else reads it.  The id is
// past every real Task field and is never written to a file.
constexpr core::view::FieldId kSortOutlineLevelField = 0x0b4fff02u;

SortField make_field(const char* name) {
    SortField field;
    field.field = task_field_id(name);
    field.title = name;
    return field;
}

}  // namespace

const std::vector<SortField>& sort_fields() {
    static const std::vector<SortField> fields = {
        make_field("ID"), make_field("Start"), make_field("Finish"),
        make_field("Priority"), make_field("Name"),
    };
    return fields;
}

SortBuild build_sort(const std::vector<SortField>& fields, const SortChoice& choice) {
    SortBuild build;
    if (choice.field_index >= fields.size()) {
        build.error = "choose a field to sort by";
        return build;
    }
    const SortField& field = fields[choice.field_index];
    if (field.field == 0) {
        build.error = "the field \"" + field.title + "\" is not available";
        return build;
    }
    build.definition.id = "app.sort";
    build.definition.name =
        field.title + (choice.ascending ? " ascending" : " descending");
    build.definition.record_type = core::model::RecordType::Task;
    core::view::SortKey key;
    key.field = field.field;
    key.ascending = choice.ascending;
    build.definition.keys.push_back(key);
    build.description = build.definition.name;
    build.ok = true;
    return build;
}

std::vector<std::size_t> sort_display_rows(const core::view::SortDefinition& sort,
                                           const core::view::RecordSet& records,
                                           const std::vector<std::size_t>& visible_rows) {
    if (sort.keys.empty() || visible_rows.size() < 2) {
        return visible_rows;
    }
    core::view::RecordSet outline_records = records;
    core::view::SortOptions options;
    const core::view::FieldId port_level = task_field_id("OutlineLevel");
    if (port_level != 0 && records.has_field(port_level)) {
        outline_records.add_field(kSortOutlineLevelField);
        for (std::size_t row = 0; row < records.row_count(); ++row) {
            std::int64_t level = 0;
            if (const core::view::Value* value = records.get(row, port_level);
                value != nullptr && value->kind == core::view::ValueKind::Integer) {
                level = value->integer;
            }
            outline_records.set_field(row, kSortOutlineLevelField,
                                      core::view::Value::of_int(level + 1));
        }
        options.keep_outline = true;
        options.outline_level_field = kSortOutlineLevelField;
    }
    const std::vector<std::size_t> order =
        core::view::apply_sort(sort, outline_records, options);
    std::vector<bool> visible(records.row_count(), false);
    for (const std::size_t row : visible_rows) {
        if (row < visible.size()) {
            visible[row] = true;
        }
    }
    std::vector<std::size_t> result;
    result.reserve(visible_rows.size());
    // The synthetic project-summary row always stays first (see the header).
    for (const std::size_t row : order) {
        if (row < visible.size() && visible[row] && records.uid(row) == 0) {
            result.push_back(row);
        }
    }
    for (const std::size_t row : order) {
        if (row < visible.size() && visible[row] && records.uid(row) != 0) {
            result.push_back(row);
        }
    }
    return result;
}

}  // namespace app
