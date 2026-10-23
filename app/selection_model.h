// app/selection_model.h -- the two-way selection mapping between the sheet and
// the Gantt, as pure C++ (no AppKit, no drawing, no reader types).
//
// WHY THIS FILE EXISTS
//   The shell has two views of one row list: the sheet draws RecordSet rows in
//   visible order, and the Gantt draws bars in the same order.  Selecting a row
//   in one has to select it in the other, and clicking a bar has to select the
//   sheet row -- but the row *ordinals* are not the same thing as RecordSet row
//   indexes once an outline row is collapsed.  That mapping (visible ordinal <->
//   RecordSet row <-> task uid) and the click/selection translation are the part
//   a headless test can prove, so they live here rather than in main.mm.
//
// THE SINGLE SOURCE OF TRUTH IS core::sheet::SheetModel
//   `SheetModel` owns the selected cell (anchor/focus).  This file never keeps a
//   second selection: every answer is derived from the model, on demand, through
//   selected_task().  A "selected task" is the sheet row of the selection's
//   anchor; a rectangular range still has exactly one task row for the Gantt to
//   highlight (the anchor's), which is also what MS Project's Gantt shows.
//
// COLLAPSE CHANGES THE MAPPING, NOT JUST THE PICTURE
//   The Gantt is rebuilt from the visible rows (main.mm calls
//   layout_model.h's make_gantt_source_for_displayed_rows with RowMap::rows, which
//   resolves each RecordSet row through its uid, so Gantt row
//   N is visible ordinal N.  A collapsed summary hides its descendants from both
//   views; links to a hidden task are dropped by build_gantt() (it skips a link
//   whose endpoint has no bar).  RowMap is what makes `ordinal_of` and
//   `row_at` agree with that rebuild.
//
// COORDINATES
//   Everything here is in the core's own spaces, never AppKit's:
//     * sheet: RecordSet row index and model column index (CellRef's space);
//     * Gantt: bar/row space (x = 0 at the timescale origin, y = 0 at the top of
//       row 0), i.e. exactly `GanttLayout::hit_test`'s arguments;
//     * points: SheetMetrics' space, y measured from the top of the document
//       (header included), which is what the drawing code already uses.
//   main.mm converts a mouse location into these spaces before calling in.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gantt.h"
#include "sheet.h"

namespace app {

using core::sheet::kNoIndex;

// The visible row list: display ordinal (== Gantt row) -> RecordSet row, plus
// each row's task uid (UniqueID), which is how a bar maps back to a row.
struct RowMap {
    std::vector<std::size_t> rows;
    std::vector<std::uint32_t> uids;

    std::size_t size() const noexcept { return rows.size(); }
    // kNoIndex when the ordinal is out of range.
    std::size_t row_at(std::size_t ordinal) const noexcept;
    // kNoIndex when the row is hidden (no ordinal) or out of range.
    std::size_t ordinal_of(std::size_t row) const noexcept;
    // kNoIndex when no visible row carries that uid.
    std::size_t ordinal_of_uid(std::uint32_t uid) const noexcept;
};

// The visible rows of `sheet`, in display order, with their uids.  O(rows).
RowMap build_row_map(const core::sheet::SheetModel& sheet);

// What the Gantt highlights and the shell scrolls to.  `has_selection` is the
// model's answer; `visible` is false only in the window between a hide and the
// model's own clamp (SheetModel clamps on collapse, so this is defensive).
struct SelectedTask {
    bool has_selection = false;
    std::size_t row = kNoIndex;      // RecordSet row of the selection's anchor
    std::size_t ordinal = kNoIndex;  // visible ordinal == Gantt row
    std::uint32_t uid = 0;
    bool visible = false;
};

SelectedTask selected_task(const core::sheet::SheetModel& sheet, const RowMap& map);

// Selects one cell through the model (the sheet-side entry point: a row click,
// an arrow key, a programmatic selection).  False when the cell is not visible
// or the model refuses it, in which case the selection is unchanged.
bool select_cell(core::sheet::SheetModel& sheet, std::size_t row, std::size_t column);

// A click in the Gantt body.  `x`/`y` are bar/row space (see the header
// comment); `column` is the sheet column to select on the row that is hit (the
// shell passes the Name column so the selected cell is the task's name).
//   * a bar   -> its sheet row is selected; returns that row.
//   * a link  -> the successor's row is selected (MS Project highlights both
//                ends of a selected link; one row is all the sheet can select).
//   * a miss  -> nothing changes, returns kNoIndex.
std::size_t select_from_gantt(core::sheet::SheetModel& sheet, const RowMap& map,
                              const core::gantt::GanttLayout& layout, double x, double y,
                              std::size_t column);

// Moves `delta` rows through the *visible* list (arrow keys), clamped to it: up
// from the first row stays on the first, down from the last stays on the last.
// kNoIndex when the map is empty or `row` is not visible.
std::size_t row_after(const RowMap& map, std::size_t row, int delta);

// Collapse/expand a summary row through the model.  True when the visible set
// changed (the shell then rebuilds its RowMap and the Gantt); false for a
// non-summary row, an out-of-range row, or an expand that changes nothing.
bool toggle_row(core::sheet::SheetModel& sheet, std::size_t row);

// Space in the sheet: toggles between the single focused cell and the WHOLE row
// (every visible column) that contains it -- the "select the row, then go back"
// toggle the brief asks for.  Returns true when the selection now covers the
// whole row, false when it went back to one cell (or when there is no
// selection, in which case nothing changed).  The original anchor cell is
// restored on the way back, so a keyboard user never loses their column.
bool toggle_row_selection(core::sheet::SheetModel& sheet);

// Where the shell scrolls to show `row`: y and height in points from the top of
// the document (header included), i.e. the rect main.mm hands to
// -[NSView scrollRectToVisible:].  `valid` is false for a hidden row.
struct ScrollTarget {
    bool valid = false;
    double y = 0.0;
    double height = 0.0;
};

ScrollTarget scroll_target(const core::sheet::SheetModel& sheet,
                           const core::sheet::SheetMetrics& metrics, std::size_t row);

}  // namespace app
