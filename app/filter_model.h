// app/filter_model.h -- the filter dialog's portable half: the fields and
// operators it offers, the rule -> core::view::FilterDefinition translation
// (with an error a dialog can show), and the row set the shell then displays.
//
// WHAT A FILTER DOES IN THE SHELL
//   A filter never removes a row from the RecordSet and never touches the model:
//   it restricts the VISIBLE ROW LIST.  app::RowMap already maps display ordinal
//   to RecordSet row (app/selection_model.h), and the sheet and the Gantt both
//   draw through it, so filtering is `build_row_map()` + restrict_to_filter()
//   and everything else -- navigation, selection, editing, collapse, the save
//   diff -- keeps working on the full model.  Editing a filtered row therefore
//   edits the same RecordSet row the save diff compares against `loaded`; there
//   is no second copy of the data to diverge.  The one place a copy is needed is
//   the PDF export, which builds its own SheetModel; filtered_record_set() is
//   that copy and it is read-only.
//
// THE CRITERIA TREE
//   core::view::FilterCriterion is a tree (comparison / All / Any / Not); this
//   wave's dialog authors a list of comparison rules joined with All (or Any),
//   which is the common case and is what the tests pin.  build_filter() takes
//   the list so a multi-rule dialog needs no new model code.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "filter.h"
#include "record_set.h"
#include "sheet.h"

namespace app {

// One column the dialog can filter on: the sheet's own columns, in sheet order.
struct FilterField {
    core::view::FieldId field = 0;
    std::string title;
    core::sheet::CellKind kind = core::sheet::CellKind::Text;
};

// One row of the operator pop-up.
struct FilterOperatorChoice {
    core::view::FilterOp op = core::view::FilterOp::Equals;
    std::string title;
};

// The operators the dialog offers, in display order.  Generic titles, because
// the same list is used for every column: the VALUE is what the field's kind
// interprets (see build_filter()).
const std::vector<FilterOperatorChoice>& filter_operators();

// The sheet's visible columns as filter fields (hidden columns excluded; a
// column whose field is 0 is skipped).  The kind is the column's declared kind,
// falling back to the descriptor table's storage kind for an Auto column.
std::vector<FilterField> filter_fields(const core::sheet::SheetModel& sheet);

// One authored rule.  `value` is display text, parsed by the field's kind with
// core::sheet::parse_edit_text (so "3d" works for a Duration column and
// "2026-10-20" for a Date column -- the same parser the cell editor uses).
struct FilterRule {
    std::size_t field_index = 0;
    std::size_t operator_index = 0;
    std::string value;
};

enum class FilterJoin : std::uint8_t { all = 0, any };

struct FilterBuild {
    bool ok = false;
    core::view::FilterDefinition definition{};
    std::string description;  // "Start is after 2026-10-20 and Duration is at least 3 days"
    std::string error;        // non-empty exactly when ok == false
};

// Builds the filter.  A one-rule list yields that comparison directly; a longer
// list yields All/Any of the comparisons.  Failure (bad indexes, an empty value,
// a value that does not parse as the column's kind, no rules at all) is
// reported, never thrown.
FilterBuild build_filter(const std::vector<FilterField>& fields,
                         const std::vector<FilterOperatorChoice>& operators,
                         const std::vector<FilterRule>& rules,
                         const core::sheet::SheetFormat& format,
                         FilterJoin join = FilterJoin::all);

// The human-readable form of the rule list, mirroring definition.name.
std::string describe_filter(const std::vector<FilterField>& fields,
                            const std::vector<FilterOperatorChoice>& operators,
                            const std::vector<FilterRule>& rules, FilterJoin join);

// RecordSet rows (in record order) that pass `filter`.
std::vector<std::size_t> filter_rows(const core::view::FilterDefinition& filter,
                                     const core::view::RecordSet& records);

// A copy of `records` holding only `rows`, in the given order.  Every field and
// every uid is preserved, so a report printed from the copy still names the same
// tasks in the same order.
core::view::RecordSet filtered_record_set(const core::view::RecordSet& records,
                                          const std::vector<std::size_t>& rows);

// `rows` (ascending RecordSet rows, e.g. SheetModel::visible_rows()) restricted
// to the membership of `filter_rows` (ascending, as matching_rows() returns).
// Order is `rows`' order.  An empty filter_rows therefore means "show nothing",
// which is why the shell passes an empty OPTIONAL set as "no filter" instead of
// calling this at all.
std::vector<std::size_t> restrict_to_filter(const std::vector<std::size_t>& rows,
                                            const std::vector<std::size_t>& filter_rows);

}  // namespace app
