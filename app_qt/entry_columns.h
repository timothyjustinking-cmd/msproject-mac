// app_qt/entry_columns.h -- the task sheet's column presentations.
//
// TWO LISTS, ONE TYPE
//   * entry_columns() is the FIXED MS Project "Entry" table measured from the
//     reference capture (docs/parity.md section 2.2):
//       row-number gutter | Indicators | Task Mode | Task Name | Duration |
//       Start | Finish | Predecessors | Resource Names | Add New Column
//     It is what a NEW/EMPTY project shows (operator decision 11) and what the
//     1920x1032 parity render measures.
//   * display_columns_for_sheet() is a LOADED file's own table (operator
//     decision 7): one column per stored `214/CTable` record, in FILE order, with
//     the file's title and width (app::table_sheet_columns).  The stored ID
//     column is the frozen row-number gutter, exactly as Project draws it, so it
//     is not a scrolling column; the "Add New Column" stub is appended because
//     Project's sheet always offers it and the reference measures it (the stub is
//     a shell affordance, not part of CTable).
//
// WHERE THE WIDTHS COME FROM
//   MS Project stores a column's width in the `214/CTable` column record: the
//   first of its nine opaque bytes is the width in characters.  One character is
//   6 px plus 3 px of padding (entry_width_px), measured from the reference and
//   the fixture together: 6/9/24/13/13/13/17/17 characters -> 39/57/147/81/81/81/
//   105/105 px, and the 20-character stub -> 123 px.
//
// No Qt types here: this is the column data the model, the header band and the
// cell delegate read.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sheet.h"  // core::view::FieldId, core::sheet::SheetColumn

namespace core::sheet {
class SheetModel;
}

namespace msqt {

// What a column shows.  `indicators` is blank unless a task has an indicator
// (the fixture's tasks have none, like the reference capture); `mode` is the one
// painted icon column; `stub` is Add New Column; `data` is an ordinary data
// column of a loaded file's table (wave 21) with no Entry-specific painting.
//
// WAVE 22: `predecessors`, `successors` and `resources` are the stored columns
// whose value Project COMPUTES from the plan (the link and assignment tables)
// rather than reading from a task record -- the `214/CTable` records for them
// carry no storable Task field (0x0b40002f/0x0b400030/0x0b400031 have no
// descriptor name and no RecordSet column).  The text is derived in
// plan_document.{h,cpp} and lives in the RecordSet under those same ids, so the
// model and the delegate paint it exactly like any other text cell; the kind is
// kept so the column layer (and a gate) can tell the three apart from an
// ordinary `data` column.
enum class EntryCellKind : std::uint8_t {
    indicators = 0,
    mode,
    name,
    duration,
    start,
    finish,
    predecessors,
    successors,
    resources,
    stub,
    data,
};

// The `214/CTable` field ids of the columns Project computes (wave 22).  They are
// the ids the stored tables carry for Predecessors/Successors/Resource Names --
// measured, not looked up by name: the descriptor table has no name for the first
// three (core/model/field_table.gen.cpp leaves them ""), which is why they never
// reach the RecordSet from the reader and are derived here.
inline constexpr core::view::FieldId kPredecessorsField = 0x0b40002fu;
inline constexpr core::view::FieldId kSuccessorsField = 0x0b400030u;
inline constexpr core::view::FieldId kResourceNamesField = 0x0b400031u;
// PercentComplete (0x0b400020) IS a stored Task field and is already in the
// RecordSet; it is named here so the gate can assert the sheet column shows it.
inline constexpr core::view::FieldId kPercentCompleteField = 0x0b400020u;
// Task.Flag10 (0x0b400051): the corpus file's "SUMMARY" column.  A flag, and NOT
// one the reader decodes -- see docs/handoff/s6.md.
inline constexpr core::view::FieldId kSummaryFlagField = 0x0b400051u;


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

// Wave 21: one column of the LIVE sheet presentation.  The Entry list and a
// loaded file's stored table both produce this shape, so the model, the caption
// band and the delegate have one contract.
struct DisplayColumn {
    std::string title;                 // caption as painted
    core::view::FieldId field = 0;     // model field; 0 for the Add New Column stub
    int width = 0;                     // pixels
    EntryCellKind kind = EntryCellKind::data;
};

// The pixel width for a stored character width.  `chars <= 0` returns 0.
inline constexpr int entry_width_px(int chars) noexcept {
    return chars > 0 ? chars * kColumnCharPx + kColumnPaddingPx : 0;
}

// The nine display columns, in sheet order.  A stable, static table: the
// header, the model, the delegate and the test all read this one list.
const std::vector<EntryColumn>& entry_columns();

// The same nine columns as a live DisplayColumn list (the new/empty project's
// sheet, and the fallback when no file's table is loaded).
const std::vector<DisplayColumn>& entry_display_columns();

// Wave 21 (operator decision 7): the display for the LOADED file's sheet.
//
// One column per visible sheet column, in the sheet's order (which is the
// stored `214/CTable` order), titled and sized from that column; the stored ID
// column is skipped (the frozen row-number gutter draws it) and the "Add New
// Column" stub is appended.  A column whose stored width is 0 (the file stores
// none) takes the Entry presentation's width for that field, else the measured
// fresh-column width (12 characters = 75 px).
std::vector<DisplayColumn> display_columns_for_sheet(const core::sheet::SheetModel& sheet);

// Sum of the columns' pixel widths: the sheet's table width (the nine Entry
// columns are 819 px, docs/parity.md).
int display_content_width(const std::vector<DisplayColumn>& columns);

// The EntryCellKind a loaded table's column is painted as, from its model field:
// the Indicators field stays blank, Task Mode keeps the pin, the four stock data
// fields keep their kinds, the three computed fields
// (0x0b40002f/0x0b400030/0x0b400031) get their own kinds and everything else is an
// ordinary data column.  Every one of those kinds except `mode` is painted as a
// plain text cell (sheet_views.cpp): the kind exists so the column layer and the
// gates can name the column, not because the delegate draws something else.
EntryCellKind display_kind_for_field(core::view::FieldId field);

// Index of the first column with `kind`, or -1.
int entry_column_index(EntryCellKind kind);

// Sum of the nine widths: 819 px (docs/parity.md: 879 - 60).
int entry_content_width();

// The model field of a kind, or 0.
core::view::FieldId entry_field(EntryCellKind kind);

}  // namespace msqt
