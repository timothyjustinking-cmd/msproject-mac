// app/field_link_edits.h -- the portable half of the two wave-13 dialogs: the
// "Task Fields..." form (custom-field values) and the "Predecessors..." form
// (incoming link rows).
//
// WHY THIS IS PURE C++
//   main.mm is AppKit and cannot be compiled on the development box, so every
//   decision the dialogs make lives here: the six field specs, the initial cell
//   text, parsing the text into the writer's types, the predecessor list, the lag
//   syntax, and the validation a proposed link add must pass before it becomes a
//   pending edit.  The shell only builds controls, reads their strings and calls
//   these functions; the headless tests (app/app_edits_fields_tests.cpp,
//   app/app_edits_links_tests.cpp) pin the whole contract.
//
// FIELD TEXT USES THE SHEET'S OWN PARSER
//   The dialog shows a value with core::sheet::format_value_text (the same date
//   and duration preferences as the sheet) and parses the edited text with
//   core::sheet::parse_edit_text.  That is deliberate: the user types the same
//   forms the in-cell editor accepts ("3d", "2026-10-26", "Yes"), and the app
//   does not grow a second date/duration grammar.  The parse result is then
//   converted to the writer's exact storage types (field_values.h): finite f64,
//   packed date word, file ticks (COM minutes * 10, i.e. 10 ticks per minute,
//   4800 per 8-hour day), and a bool.
//
// LINK LAG
//   `parse_link_lag` accepts "0", "2" (bare = working days), "2d", "-1d" (a lead,
//   negative), "4h" and "30m" (case-insensitive suffixes d/h/m).  One 8-hour
//   working day is 4800 six-second ticks, one hour 600, one minute 10.  The
//   writer's own storage is signed i32 ticks; magnitudes beyond
//   mppwrite::kMaxLinkLagTicks are refused with that reason.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "edit_model.h"
#include "layout_model.h"
#include "sheet.h"

namespace app {

// ---------------------------------------------------------------------------
// Task Fields... (the six measured custom fields)
// ---------------------------------------------------------------------------

// One editable field: the PjTask id, the value family, the CellKind the dialog
// parses it as, and the descriptor table's caption.
struct TaskFieldSpec {
    std::uint32_t field_id = 0;
    mpp::FieldKind kind = mpp::FieldKind::unknown;
    core::sheet::CellKind cell_kind = core::sheet::CellKind::Auto;
    std::string title;
};

// Text1, Number1, Number2, Date1, Duration1, Flag1 -- the six fields whose
// storage and 114/Props registration are measured.  Static (process lifetime).
const std::vector<TaskFieldSpec>& task_field_specs();

// The spec for `field_id`, or nullptr when it is not one of the six.
const TaskFieldSpec* task_field_spec(std::uint32_t field_id) noexcept;

// The dialog's initial text: the stored value rendered the way the sheet renders
// that column (the date/duration preferences apply, a flag is Yes/No).  "" when
// the row does not store the field (that includes a Flag1 whose bit is clear).
std::string field_value_text(const TaskRow& row, std::uint32_t field_id,
                             const core::sheet::SheetFormat& format);

// One control of the Task Fields dialog.
struct TaskFieldInput {
    std::uint32_t field_id = 0;
    std::string text;
};

// Parses every input and returns the edits whose value actually changed.  A
// blank input is "clear": it becomes a clear_field edit when the row stores a
// value, and is skipped when it does not (clearing an absent field must not
// register one -- the writer's documented semantics).  A non-blank input is
// parsed with core::sheet::parse_edit_text as the field's kind; any parse error
// refuses the WHOLE form with `error` naming the field (the dialog stays open).
// `out` is cleared first.
bool make_task_field_edits(const TaskRow& row, const std::vector<TaskFieldInput>& inputs,
                           const core::sheet::SheetFormat& format,
                           std::vector<StructuralEdit>& out, std::string& error);

// ---------------------------------------------------------------------------
// Predecessors... (the selected task's incoming link rows)
// ---------------------------------------------------------------------------

// "FF", "FS", "SF", "SS"; "?" never appears (LinkType is exhaustive).
const char* link_type_name(core::gantt::LinkType type) noexcept;

// Accepts "FF"/"FS"/"SF"/"SS", case-insensitive; false otherwise.
bool parse_link_type(std::string_view text, core::gantt::LinkType& out) noexcept;

// One row of the dialog's list.
struct PredecessorListItem {
    std::uint32_t link_uid = 0;  // 0 for a link that is only pending (not in the file)
    std::uint32_t predecessor_uid = 0;
    std::string predecessor_id;    // Task.ID, or the uid when the plan has no row
    std::string predecessor_name;
    core::gantt::LinkType type = core::gantt::LinkType::finish_to_start;
    const char* type_name = "FS";
    std::int32_t lag_ticks = 0;
    std::string lag_text;
    bool pending = false;  // displayed from a pending add, not from the file
};

// The incoming links of `successor_uid` in `plan` order, with pending link
// deletes omitted.  `plan` is normally the shell's display plan (loaded + the
// pending edits), so a pending add shows up with pending == true.
std::vector<PredecessorListItem> predecessors_of(const PlanInput& plan,
                                                 const StructuralEdits& edits,
                                                 std::uint32_t successor_uid);

// "0", "2d", "-1d", "4h", "30m": see the file comment.  `ticks` is 6-second file
// ticks.  On failure `error` names the accepted syntax.
bool parse_link_lag(std::string_view text, std::int32_t& ticks, std::string& error);

// The inverse rendering (whole days if possible, then whole hours, then whole
// minutes, else ticks).
std::string format_link_lag(std::int32_t ticks);

// Validates a proposed add: the successor and predecessor must be live file
// tasks (not a pending insert), different, not pending-deleted, and the
// (predecessor, successor, type) relation must not already exist in `plan` or in
// the pending add list.  Returns "" when the add is acceptable; otherwise a
// message for the dialog.
std::string validate_link_add(const PlanInput& plan, const StructuralEdits& edits,
                              std::uint32_t predecessor_uid, std::uint32_t successor_uid,
                              core::gantt::LinkType type);

// validate_link_add + parse_link_lag + the StructuralEdit fields (uid =
// successor).  False with `error` when either check fails.
bool make_link_add(const PlanInput& plan, const StructuralEdits& edits,
                   std::uint32_t predecessor_uid, std::uint32_t successor_uid,
                   core::gantt::LinkType type, std::string_view lag_text,
                   StructuralEdit& out, std::string& error);

// Builds the delete edit for one listed item (link_uid must be non-zero; a
// pending add is removed by undoing it, which the shell's Delete on the dialog
// list does not do -- it only offers file rows).  False with `error` for a
// pending row or a zero uid.
bool make_link_delete(const PredecessorListItem& item, std::uint32_t successor_uid,
                      StructuralEdit& out, std::string& error);

}  // namespace app
