// app_qt/entry_columns.h -- the MS Project "Entry" table column set.
//
// Q2's target is the Entry table measured from the reference capture
// (docs/parity.md section 2.2):
//
//   row-number gutter | Indicators | Task Mode | Task Name | Duration | Start |
//   Finish | Predecessors | Resource Names | Add New Column
//
// The row-number gutter is the sheet view's FROZEN vertical header (60 px,
// SheetRowHeader in sheet_views.h); this table lists the nine *sheet* columns
// that follow it, in order, with the measured boundaries
// 60, 99, 156, 303, 384, 465, 546, 651, 756, 879.
//
// WHERE THE WIDTHS COME FROM
//   MS Project stores a column's width in the `214/CTable` column record: the
//   first of its nine opaque bytes is the width in characters (core/viewstate/
//   viewstate.h, docs/handoff/w62.md).  PlanDocument reads those bytes on load
//   (PlanDocument::table_column_chars) and the status line reports them.
//
//   The reference's pixel widths are 39/57/147/81/81/81/105/105/123, and they
//   agree with the fixture's stored character widths under MS Project's column
//   rule (one character = 6 px, plus 3 px of padding): 6*6+3 = 39, 9*6+3 = 57,
//   24*6+3 = 147, 13*6+3 = 81, 17*6+3 = 105, 20*6+3 = 123.  The shell therefore
//   takes each column's character width FROM THE FILE (`214/CTable`, through
//   PlanDocument::table_column_chars) and converts it with entry_width_px(); the
//   defaults in entry_columns() are the stock Entry widths used when a file has
//   no CTable for that field (or the readback failed).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sheet.h"  // core::view::FieldId

namespace msqt {

// What a column shows.  `indicators` is blank unless a task has an indicator
// (the fixture's tasks have none, like the reference capture); `mode` is the one
// painted icon column; `stub` is Add New Column.
enum class EntryCellKind : std::uint8_t {
    indicators = 0,
    mode,
    name,
    duration,
    start,
    finish,
    predecessors,
    resources,
    stub,
};

// MS Project's column width unit: one stored character is 6 px and every
// column carries 3 px of padding.  The rule was derived from the reference and
// the fixture TOGETHER: the fixture's `214/CTable` stores 6/9/24/13/13/13/17/17
// characters for Indicators/Task Mode/Name/Duration/Start/Finish/Predecessors/
// Resources, and 6*6+3 = 39, 9*6+3 = 57, 24*6+3 = 147, 13*6+3 = 81, 17*6+3 =
// 105 -- exactly the measured boundaries.  The "Add New Column" stub is 20
// characters: 20*6+3 = 123.
inline constexpr int kColumnCharPx = 6;
inline constexpr int kColumnPaddingPx = 3;

struct EntryColumn {
    const char* title;
    core::view::FieldId field;    // model field; 0 for a synthetic column
    int default_chars;            // stored width when the file has none
    int width;                    // pixels for default_chars (reference-measured)
    EntryCellKind kind;
    std::uint32_t table_field;    // `214/CTable` field id used for the width byte
};

// The pixel width for a stored character width.  `chars <= 0` returns 0.
inline constexpr int entry_width_px(int chars) noexcept {
    return chars > 0 ? chars * kColumnCharPx + kColumnPaddingPx : 0;
}

// The nine display columns, in sheet order.  A stable, static table: the
// header, the model, the delegate and the test all read this one list.
const std::vector<EntryColumn>& entry_columns();

// Index of the first column with `kind`, or -1.
int entry_column_index(EntryCellKind kind);

// Sum of the nine widths: 819 px (docs/parity.md: 879 - 60).
int entry_content_width();

// The model field of a kind, or 0.
core::view::FieldId entry_field(EntryCellKind kind);

}  // namespace msqt
