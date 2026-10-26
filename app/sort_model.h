// app/sort_model.h -- the Sort dialog's portable half: the five fields the View
// tab offers, the choice -> core::view::SortDefinition translation, and the
// display order the shell then draws.
//
// WHAT A SORT DOES IN THE SHELL (WAVE 31, W31-1.1)
//   Project's Sort is a persisted view change: it reorders the tasks in the
//   file.  This port offers it SESSION-ONLY (wave-31 brief section 1): the
//   RecordSet, the loaded plan and the save diff never change; only the
//   displayed row list is reordered.  display_rows() and the Gantt's RowMap are
//   both derived from sort_display_rows(), so the sheet and the chart always
//   agree, and saving the file keeps its bytes.
//
// OUTLINE STRUCTURE
//   A sort keeps the outline: children stay beneath their parent and only
//   siblings are compared (core::view::SortOptions::keep_outline).  A flat
//   sort would detach a child from its summary, which the sheet cannot draw
//   (SheetModel's outline levels are the row's identity) and Project itself
//   never does with "Keep outline structure" checked -- the default.  The
//   synthetic project row (uid 0) is pinned first, as it is in the file order,
//   because Project never sorts its project summary row among the tasks.
//
//   The result is restricted to `visible_rows` (the sheet's rows after any
//   collapse) and keeps that restriction's membership, so a collapsed
//   summary's children stay hidden and a later filter (app/filter_model.h) is
//   applied on top by the caller.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "record_set.h"
#include "sort.h"

namespace app {

// One entry of the dialog's Field combo.  `field` is 0 when the descriptor
// table has no such Task field; build_sort() then refuses the choice rather
// than sorting on a missing column.
struct SortField {
    core::view::FieldId field = 0;
    std::string title;
};

// The brief's five keys, in the order the dialog lists them: ID, Start,
// Finish, Priority, Name.  Resolved once per process through the descriptor
// table (app::task_field_id), so a table regeneration cannot rename a key
// silently.
const std::vector<SortField>& sort_fields();

// The dialog's choice: one primary key and its direction.
struct SortChoice {
    std::size_t field_index = 0;
    bool ascending = true;
};

struct SortBuild {
    bool ok = false;
    core::view::SortDefinition definition{};
    std::string description;  // "Start ascending"
    std::string error;        // non-empty exactly when ok == false
};

// Builds a one-key sort.  An out-of-range index or an unresolved field is
// reported, never thrown.  `definition.id` is stable ("app.sort") so a caller
// can recognize the dialog's sort; `name` is the description.
SortBuild build_sort(const std::vector<SortField>& fields, const SortChoice& choice);

// The displayed RecordSet rows in sorted order: the outline-aware order of
// `records`, restricted to the membership of `visible_rows`.  An empty key list
// returns `visible_rows` unchanged.  Every row of `visible_rows` that exists in
// `records` appears exactly once; rows outside `records` are dropped.
std::vector<std::size_t> sort_display_rows(const core::view::SortDefinition& sort,
                                           const core::view::RecordSet& records,
                                           const std::vector<std::size_t>& visible_rows);

}  // namespace app
