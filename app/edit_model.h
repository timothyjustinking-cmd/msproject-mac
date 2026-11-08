// app/edit_model.h -- the pending edits the shell writes on Save: Insert/Delete
// Task, task custom-field values and link (predecessor) changes.
//
// WHY THE APP KEEPS THEM PENDING
//   `core/mppwrite` can append one Task row and tombstone one Task row
//   (`add_delete.h`, W45; the delete cascades to dependent link/assignment rows,
//   W48/w11-oracle section 2), append/tombstone one link row (add_delete.h, W52)
//   and write/clear one custom-field value (`field_values.h`, W48/W52).  It
//   cannot do several of those inside one writer call, and every call writes a
//   FILE: the app's one save path owns "write once, reload, rebuild".  So the app
//   records what the user asked for (a name to add, a uid to delete, a field
//   value, a link to add or remove), shows the result immediately from a locally
//   built plan, and applies the whole set on Save.  Nothing here touches a file
//   except save_structural_edits().
//
// ONE STACK, CHRONOLOGICAL
//   Wave 13 extends the wave-12 `StructuralEdits` stack with two more edit kinds
//   (field value and link) instead of adding a third stack.  The reason is the
//   save sequence: the writer applies link deletes, link adds, task deletes, task
//   inserts, field values and finally start dates to ONE document, so the edits
//   have to be ordered as one list anyway.  Undo/redo moves an edit between the
//   applied and undone lists exactly as before; the display plan below knows how
//   to show every kind.
//
// WHAT THE DISPLAY PLAN IS
//   StructuralEdits::apply(loaded) returns a copy of the loaded plan with the
//   pending edits applied: deleted tasks removed (with their dependent links and
//   assignments, the same rows the writer tombstones), inserted tasks placed
//   after their anchor task (or at the end), custom-field values set/cleared, and
//   link rows added/removed.  The shell rebuilds its RecordSet/SheetModel/Gantt
//   from that plan, which is why a pending insert has a real row and a pending
//   delete has no row -- and why an undo is just "rebuild from the plan without
//   that edit".
//
//   An inserted placeholder copies the duration and dates of the last live task
//   (or none when the plan is empty).  That mirrors the writer's documented add
//   shape -- `add_task_row` clones the last live row and clears its constraint --
//   so the placeholder sits where the saved row will, without inventing a
//   schedule.  The file is the authority: after a save the shell reloads it.
//
// UNDO / REDO
//   Two stacks: this one, and core::sheet::SheetModel's cell-edit history.  The
//   shell keeps them chronological by clearing the sheet's history when an edit
//   here is pushed (so a cell edit is never undone "past" a later structural
//   edit) and clearing this redo stack when a cell edit is committed.
//   `undo()`/`redo()` only move the edit between the two lists; the caller
//   rebuilds its derived state afterwards.
//
// WHAT THIS FILE DOES NOT DO
//   It does not reorder a row, renumber IDs or UniqueIDs (the file's writer
//   allocates them and the shell reloads, never patches a uid by hand), and it
//   does not schedule: a new row's dates are a display placeholder until the file
//   says otherwise.  A formula definition cannot be written (a 114/Props writer
//   is deliberately absent), so only the six measured stored fields are editable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "document.h"
#include "layout_model.h"

namespace core::sched {
class Calendar;
}

namespace app {

enum class StructuralKind : std::uint8_t {
    insert_task = 0,
    delete_task = 1,
    // Wave 13: one measured custom-field value of one task.  `set_field` stores
    // the value carried in `field_value`; `clear_field` stores the measured
    // empty value (`mppwrite::clear_field_value`).
    set_field = 2,
    clear_field = 3,
    // Wave 13: one TBkndCons link row.  `add_link` writes the relation
    // (predecessor_uid -> uid, link_type, lag_ticks); `delete_link` tombstones
    // the file link row named by `link_uid`.
    add_link = 4,
    delete_link = 5,
    // Wave 26 (W26-1.2), ENABLED by wave 28 (W28-1.2): the ribbon's
    // 0/25/50/75/100 % buttons and Task Information's Percent complete field.
    // `uid` is the task and `percent` is 0..100.  Project's own 50 % save
    // (tests/data/wave27-percent-actuals.mpp) writes ActualDuration (+80),
    // RemainingDuration (+88), ActualStart (+120), two dates (+132/+136), a
    // progress flag (+168), the project row's roll-up (+80/+88/+92) and a
    // Var2Data field (0x0B40015E), so step 5a writes the measured actuals block
    // (core/mppwrite/actuals.h), not the old one-word +92 patch.
    set_percent = 6,
    // Wave 26 (W26-5.1): Project Information's Priority.  The value lives in the
    // project row's FixedData +78 (row 0, uid 0) and is written through
    // mppwrite::TaskTable::set_priority on the project row -- a single-copy
    // table write measured in W27-0.3 (the Project-saved pair differs only at
    // row 0 +78).
    set_project_priority = 7,
    // Wave 28 (W28-1.3): Mark on Track / Update Project -- a date measured
    // progress write over one or more leaf tasks, on the SAME writer as
    // set_percent (mppwrite::write_percent_actuals).  `actuals` carries the
    // computed per-task percent and elapsed working ticks, `through_date` the
    // date they were measured through, and `updates_status_date` says whether
    // the save also moves the project status date (Update Project does; Mark
    // on Track does not).  Project's pairs are tests/data/wave28-mark-*.mpp
    // and wave28-update*.mpp; app::make_update_actuals_edit computes the
    // targets with app::percent_through.
    update_actuals = 8,
    // Wave 28 (W28-2.1): Project's Indent/Outdent on one saved task, written
    // by mppwrite::write_outline_edits (core/mppwrite/outline.h).  `uid` is
    // the moved task; `anchor_uid` is the summary row the display touches --
    // the preceding sibling that becomes the summary on indent, the parent
    // that becomes a leaf again on outdent.  Indent moves a top-level row
    // (leaf or summary) under its preceding sibling; outdent moves a LEAF out
    // of a parent that has no other child (the only shapes Project's three
    // committed pairs measured).  `summary_duration` and the restored_* words
    // are computed by app::make_outline_edit from the file's outline and
    // calendar; a hand-built edit without them is refused by push().
    outline_indent = 9,
    outline_outdent = 10,
    // Wave 28 (W28-3.1): the project's new-task scheduling mode, from Options >
    // Schedule.  `new_tasks_manual` is the stored Project.NewTasksCreatedAsManual
    // value; step 5d writes it into 114/Props record 0x024013c8 (the measured
    // Project save bytes are core/mppwrite/project_props.h).  The mode is also
    // what Insert Task's new row is scheduled with (`manually_scheduled`).
    set_new_task_mode = 11,
    // Wave 30 (W30-1.0): one saved task changed from automatic to manually
    // scheduled, written by mppwrite::write_task_mode (core/mppwrite/
    // task_mode.h).  `uid` is the row; `task_manual` is true for the only
    // measured direction (Manually Schedule) and false is refused -- Auto
    // Schedule needs Project's reschedule, which this port does not have.  The
    // edit is EXCLUSIVE: it may only be queued alone, and no other structural
    // edit may be queued over it (the measured pair is one mode change on an
    // otherwise untouched save).
    set_task_mode = 12,
    // Wave 34 (W34-4.1): one saved resource renamed from the Resource
    // Information dialog, written by mppwrite::rename_resource (core/mppwrite/
    // varmeta_edit.h).  `uid` is the resource; `name` is the new name.  Only
    // the measured shape is enabled: a name whose stored UTF-16 payload is
    // STRICTLY LARGER than the current one (wave30-resource-rename: uid 2,
    // 6 -> 8 B).  Like set_task_mode the edit is EXCLUSIVE -- the measured pair
    // is one rename on an otherwise untouched save.
    rename_resource = 13,
    // Wave 34 (W34-4.2): one work resource with default rates, from Add
    // Resources, written by mppwrite::add_work_resource (core/mppwrite/
    // resource_growth.h; the eleven streams plus Props14).  `uid` and
    // `resource_id` are what the writer will allocate (taken from its dry run
    // when the edit is built), `name` is the typed name and `created` the
    // Created stamp.  Only the measured counts are enabled: a project that
    // already holds one or two resources (wave30-resource-add, wave34-resource-
    // add-third).  Like the two kinds above the edit is EXCLUSIVE.
    add_resource = 14,
    // Wave 34 (W34-4.3): one work resource assigned at 100 % to an unstarted
    // leaf task (`uid` = the task, `resource_uid` = the resource) and the same
    // assignment removed, written by mppwrite::assign_resource /
    // unassign_resource (core/mppwrite/assignment_growth.h) against Project's
    // wave30-assign-add and wave30-assign-delete pairs.  `assignment_uid` is
    // the appended row's UniqueID, taken from the writer's dry run.  Only the
    // first-assignment shape (count 0 -> 1) and the removal of that one
    // assignment are enabled; like the kinds above both are EXCLUSIVE.
    assign_resource = 15,
    unassign_resource = 16,
    // Wave 34 (W34-4.5): Font Bold on ONE cell, written by mppwrite::
    // save_view_cell_style (core/mppwrite/viewstream_edit.h) against Project's
    // wave30-textstyle pair: one `0x2240000c` cell-format item appended to the
    // applied view's main block.  `uid` is the formatted row and `field_id` the
    // cell's stored field id.  Only the measured cell is enabled (row uid 1,
    // Task Name, an unformatted view); like the kinds above it is EXCLUSIVE.
    bold_cell = 17,
    // Wave 34 (W34-4.6): the WBS code "W" with ordered two-digit numbers, from
    // Project > Properties > WBS > Define Code, written by mppwrite::
    // save_wbs_code (core/mppwrite/wbs_code.h) against Project's wave30-wbs
    // pair: four stored WBS number blocks plus the two code records in 114/Props.
    // The edit carries no row (`uid` is 0) and no parameters -- the measured
    // definition is the only one -- and only a project with exactly the pair's
    // three flat tasks is enabled; like the kinds above it is EXCLUSIVE.
    define_wbs_code = 18,
    // Wave 35 (W35-1.3): Project > Calculate Project (F9), written by mppwrite::
    // save_calculate (core/mppwrite/calculate.h) against Project's
    // wave35-calculate-unlinked pair.  The edit carries the recalculated words
    // the engine computed -- `summary_duration` and `restored_finish` hold the
    // project row's Duration and Finish, `slack` one entry per leaf -- and no
    // row of its own (`uid` is 0).  Only the measured shape is enabled: a
    // pending recalculation of the project row plus two unlinked automatic
    // leaves.  Like the kinds above it is EXCLUSIVE.
    calculate_project = 19,
    // Wave 35 (W35-2.1): Project > Sort (Outline = True, Renumber = False) on
    // ONE key, written by mppwrite::save_view_sort (core/mppwrite/
    // viewstate_sort.h) against Project's wave35-viewstate-sort-name-desc /
    // -name-asc / -duration-desc pairs: the sort specification in the applied
    // view and the displayed order in the edit list.  `field_id` is the stored
    // field id of the key (Name 0x0B40000E or Duration 0x0B400507), `ascending`
    // its direction, `sort_base_order` the file's task UniqueIDs in file order
    // and `sort_order` the same tasks in the order the sort produces (computed
    // by make_sort_edit under the pins listed there).  The display is
    // unchanged by the queued edit (the shell shows the sort for the session);
    // like the kinds above it is EXCLUSIVE.
    sort_view = 20,
    // Wave 35 (W35-2.3): View > Outline > Show > Level 1 or 2 on the task sheet,
    // written by mppwrite::save_view_outline (core/mppwrite/viewstate_outline.h)
    // against Project's wave35-viewstate-outline-* pairs: the collapsed and
    // hidden rows' flags in the task table's FixedMeta, the displayed rows in the
    // edit list and the CEdl size words.  `show_level` is the 1-based level,
    // `outline_uids` / `outline_levels` the file's tasks in file order with their
    // 0-based outline levels (recomputed from the plan by push()).  The shell's
    // own display is unchanged by the queued edit; like the kinds above it is
    // EXCLUSIVE.
    show_level = 21,
};

// One task's computed progress in an update_actuals edit.  `actual_ticks` is
// the exact elapsed working time (not Duration * percent / 100) and
// `step_percent` the exact unrounded percent Project stores in the
// 0x0B40015E step block.
struct ActualsTarget {
    std::uint32_t uid = 0;
    std::uint16_t percent = 0;
    std::uint32_t actual_ticks = 0;
    double step_percent = 0.0;
};

// One row's recalculated slack/late-date words in an outline edit (W29-0.4).
// The app copies of mppwrite::OutlineSlack: the same five values Project's
// automatic save writes into +74/+112/+116/+186/+190.  `late_start` and
// `late_finish` are date32 words (the file's packed dates are the same
// counter).
struct OutlineSlackEntry {
    std::uint32_t uid = 0;
    std::uint32_t free_slack = 0;
    std::uint32_t start_slack = 0;
    std::uint32_t total_slack = 0;
    date32 late_start = kDateNa;
    date32 late_finish = kDateNa;
};

// One pending edit, as the user asked for it.
struct StructuralEdit {
    StructuralKind kind = StructuralKind::insert_task;
    // The task the edit is about: delete_task = the task's UniqueID;
    // insert_task = the placeholder's in-app UniqueID (never written; the writer
    // allocates the real one); set_field/clear_field = the field's task;
    // add_link/delete_link = the SUCCESSOR task (the row the dialog was opened
    // on).
    std::uint32_t uid = 0;
    // insert_task: the row is displayed after this task (0 = at the end).
    // Resolved against the current plan on every apply(), so a delete of the
    // anchor cannot leave a dangling position.
    std::uint32_t anchor_uid = 0;
    // insert_task: the task's name (required, non-empty).
    std::string name;
    // insert_task (W28-3.1): Project's Manually Scheduled flag for the appended
    // row, taken from the project's stored mode at the moment the insert was
    // queued.  The writer already measured the Fixed2Meta bit in W45
    // (core/mppwrite/add_delete.h); the shell passes this instead of the
    // writer's auto-scheduled default.
    bool manually_scheduled = false;
    // delete_task: how many dependent rows the writer's cascade will tombstone
    // (links naming the uid + assignments for it), counted when the edit was
    // made so the confirmation can name it.
    std::size_t dependants = 0;
    // add_resource (W34-4.2): the new row's ID and the packed Created stamp
    // (day << 16 | seconds / 6; see current_created_stamp()).
    std::uint32_t resource_id = 0;
    std::uint32_t created = 0;
    // assign_resource / unassign_resource (W34-4.3): the resource, and the
    // UniqueID of the assignment row the writer appends.
    std::uint32_t resource_uid = 0;
    std::uint32_t assignment_uid = 0;
    // sort_view (W35-2.1): the key's direction and the file's task UniqueIDs in
    // file order / in sorted order (the key's field is `field_id`).
    bool ascending = true;
    std::vector<std::uint32_t> sort_base_order;
    std::vector<std::uint32_t> sort_order;
    // show_level (W35-2.3): the 1-based Show Level and the file's tasks in file
    // order with their 0-based outline levels.
    int show_level = 0;
    std::vector<std::uint32_t> outline_uids;
    std::vector<std::uint32_t> outline_levels;

    // --- set_field / clear_field -------------------------------------------
    // One of the six measured ids (app::is_measured_task_field): Text1,
    // Number1, Number2, Date1, Duration1, Flag1.
    std::uint32_t field_id = 0;
    mpp::FieldKind field_kind = mpp::FieldKind::unknown;
    // set_field only: the value to write.  `present`/`storage_measured` are not
    // read on input (they describe a read-back).
    mpp::CustomFieldValue field_value;

    // --- add_link / delete_link --------------------------------------------
    // add_link: the predecessor task uid.  delete_link: the predecessor read from
    // the file row (for the status message; `link_uid` is the identity).
    std::uint32_t predecessor_uid = 0;
    // delete_link: the TBkndCons FixedData +0 uid of the row to tombstone.
    std::uint32_t link_uid = 0;
    // set_percent: the task's new PercentComplete, 0..100.
    std::uint16_t percent = 0;
    // update_actuals: the computed targets, at most one per uid.
    std::vector<ActualsTarget> actuals;
    // update_actuals: the date progress is measured through (midnight, as the
    // status date and the Update Project dialog are stored).
    date32 through_date = kDateNa;
    // update_actuals: true only for Update Project, which stores through_date
    // in the file's project status date.  Mark on Track leaves that record
    // alone (measured on both mark pairs).
    bool updates_status_date = false;
    // update_actuals (W29-0.3): true when the targets cover EVERY live,
    // saved, non-summary task -- Update Project's "Entire project" scope.  Only
    // that scope's save makes the project row's Resume/Stop the earliest
    // per-row date; Mark on Track and Update Project's "Selected tasks" scope
    // keep the project start there, even though the selected scope still moves
    // the status date (measured: wave29-update-60-selected.mpp's project row
    // keeps 3bf11e78 while wave29-update-60-after.mpp's takes 3bf31c20).
    bool update_entire_project = false;
    // set_project_priority: Project's project priority, 0..1000.
    std::uint16_t priority = 500;
    // set_new_task_mode (W28-3.1): true for Manually Scheduled, false for Auto
    // Scheduled -- Project's NewTasksCreatedAsManual.
    bool new_tasks_manual = true;
    // set_task_mode (W30-1.0): true = Manually Scheduled, the measured
    // automatic -> manual direction.  False (Auto Schedule) is refused: the
    // reverse pair shows Project also reschedules the row, and this port has no
    // scheduler.
    bool task_manual = true;

    // --- outline_indent / outline_outdent (W28-2.1) -------------------------
    // outline_indent: the new summary's FixedData +84 -- the project
    // calendar's working time between the moved subtree's earliest +104 and
    // latest +108.  The writer computes those two dates itself; this value is
    // the caller's (outline.h).
    std::uint32_t summary_duration = 0;
    // outline_outdent: the parent's 0x0B40007C marker words.  The writer
    // re-reads the marker and refuses when these no longer match (the file
    // changed after the edit was made), so they are the proof the restore is
    // the shape Project measured.
    std::uint32_t restored_duration = 0;
    std::uint16_t restored_duration_format = 0;
    // outline_outdent: the leaf schedule computed from the marker and the
    // parent's +104 (make_outline_edit); the writer only proves they are
    // valid dates in order.
    date32 restored_start = kDateNa;
    date32 restored_finish = kDateNa;
    // outline_indent/outdent (W29-0.4): the +74/+112/+116/+186/+190 words of
    // every row an AUTOMATIC save recalculates -- every live automatic row, plus
    // the restored manual parent of a hybrid outdent.  make_outline_edit
    // computes them from the file's outline and the project calendar; the
    // manual shape carries none, and the writer refuses a vector that does not
    // name exactly the rows Project recalculates.
    std::vector<OutlineSlackEntry> slack;

    core::gantt::LinkType link_type = core::gantt::LinkType::finish_to_start;
    // The lag in 6-second file ticks (4800 = one 8-hour working day).
    std::int32_t lag_ticks = 0;
    // delete_task only (W27-3.2): the earlier field/percent/link-add edits for
    // this task that pushing the delete removed from the applied list.  They
    // are kept here so undo() can put them back (and redo() remove them again)
    // instead of discarding the user's work silently.  Empty otherwise.
    std::vector<StructuralEdit> displaced;
};

// True for the six custom-field ids whose storage and 114/Props registration are
// measured: Text1 0x0b400033, Flag1 0x0b400048, Number1 0x0b400057, Number2
// 0x0b400058, Duration1 0x0b400067, Date1 0x0b400109.  Anything else (Cost,
// Flag2..20, an unknown id) is refused rather than guessed.
bool is_measured_task_field(std::uint32_t field_id) noexcept;

// Wave 27 (W27-0.2): plan-aware validation for a %-complete edit, in the edit
// MODEL rather than the shell.  A task's own % is meaningful only on a leaf:
// Project computes a summary's % from its children's actual and total durations
// and distributes a summary edit to them, while this port could only write +92
// on the summary row -- a shape Project recalculates away.  uid 0 (the
// project-summary row), an unknown uid and a row with no saved file row (a
// pending insert -- TaskRow::file_row == 0, including one present in the plan
// StructuralEdits::apply() returns) are refused too.  On acceptance `out` is a
// ready-to-push set_percent edit; on refusal `error` names the reason.
bool make_percent_edit(const PlanInput& plan, std::uint32_t uid, std::uint16_t percent,
                       StructuralEdit& out, std::string& error);

// ---------------------------------------------------------------------------
// Wave 28 (W28-1.3): progress through a date -- Mark on Track / Update Project
// ---------------------------------------------------------------------------
//
// The MEASURED rule behind both commands (tests/data/wave28-mark-*.mpp and
// wave28-update*.mpp; the working-time pieces are the same ones the percent
// save uses):
//
//   finish = manual ? manual_finish_of(calendar, start, duration)
//                   : calendar.add_work(start, duration)
//   100 % if through >= finish (ActualDuration = Duration)
//     0 % if through <= start
//   else elapsed = (manual ? the start day's partial-day charge : 0)
//                  + calendar.work_time_between(calendar.next_start(start),
//                                               through)
//        percent = elapsed * 100 / duration, truncated
//        ActualDuration = elapsed
//
// Project also stores the UNROUNDED fraction in the 0x0B40015E step block
// (the measured manual 1200/1440 row stores 83.33333333333333 beside the
// stored 83), which is `step_percent`.  A computed 0 % stores zero elapsed
// ticks and no dates: the writer's row shape has no "0 % with progress".
// A zero-duration row (a milestone) is 100 % once `through` reaches its start
// and 0 % before it; there is no measured update pair for one, and this is
// the only definition that needs no division.
//
// `start` and `through` are packed/date32 words (they are the same counter,
// see layout_model.h) and `duration_ticks` the file's 6-second ticks.  False
// when a date is invalid or the calendar overflows the finish.
struct PercentThrough {
    std::uint16_t percent = 0;
    std::uint32_t actual_ticks = 0;
    double step_percent = 0.0;
};
bool percent_through(const core::sched::Calendar& calendar, std::uint32_t start,
                     std::uint32_t duration_ticks, bool manually_scheduled,
                     std::uint32_t through, PercentThrough& out);

// Builds a ready-to-push update_actuals edit by computing percent_through for
// every uid in `uids` against the project calendar loaded from
// `source_path` (the same core/cal calendar the save path uses).  `uids` must
// be non-empty, distinct, saved leaf tasks (make_percent_edit's rules);
// `through_date` must be a real date.  The result's through_date and
// updates_status_date are always set.  On refusal `error` names the task and
// the reason and `out` is left default-constructed.
bool make_update_actuals_edit(const PlanInput& plan, std::string_view source_path,
                              const std::vector<std::uint32_t>& uids,
                              date32 through_date, bool updates_status_date,
                              StructuralEdit& out, std::string& error);

// ---------------------------------------------------------------------------
// Wave 28 (W28-2.1): Indent / Outdent
// ---------------------------------------------------------------------------
//
// Builds a ready-to-push outline edit for the saved task `uid`, from the
// file's own stored outline (mppwrite::read_outline_rows / read_outline_marker)
// and project calendar.  The file is the authority because the writer derives
// the parent link, the child list and the marker from it; the plan is checked
// too, and an edit whose display row/level disagrees with the file (a pending
// insert above it, a pending outline edit not yet saved) is refused rather
// than queued against a shape the save cannot reproduce.
//
//   indent : `uid` must be a top-level saved row (leaf or summary -- both
//            measured) whose preceding sibling is a live leaf.  The new
//            summary's duration is the calendar working time between the
//            moved subtree's earliest start (+104) and latest finish (+108).
//   outdent: `uid` must be a saved leaf whose parent has exactly one child
//            and a 0x0B40007C marker.  The restored schedule is
//            next_start(parent +104) and add_work(that start, marker
//            duration), and the marker words are carried so the writer can
//            prove the file has not moved under the edit.
//
// W29-0.4: an AUTOMATIC indent (moved row and new summary both automatic) and
// an automatic outdent (the moved row automatic, the parent automatic or
// manual -- the hybrid pair) are enabled for the shapes the four wave-29 save
// pairs measured (wave29-outline-auto-indent/-summary/-auto-outdent/
// -manual-parent).  make_outline_edit computes the resulting schedule and
// fills StructuralEdit::slack: for every live automatic row, and the restored
// manual parent of a hybrid outdent,
//   slack = calendar.work_time_between(row's resulting +108, project +108),
//   +74 = +186 = +190 = slack,
//   +112 = add_work(row +104, slack), +116 = add_work(row +108, slack).
// The indent's new summary and the project row roll up to the moved subtree's
// latest finish; an outdent's project finish is the restored finish.  A mixed
// indent (one row automatic, one manual) and a manual row under an automatic
// parent are still refused: no pair measures those directions.
//
// uid 0, an unknown uid, a pending insert (file_row == 0), a summary for
// outdent, an unreadable file/outline/calendar, a mixed-scheduling shape, an
// automatic shape the writer's guards reject (project row manual, a start that
// is not the measured roll-up start, another top-level row finishing after the
// new project finish), an indent whose new summary carries progress,
// assignment work or an actuals edit queued in the same save (name its uid in
// `pending_actuals`), and the shapes outline.h lists as unmeasured are refused
// with `error` naming the reason.  `out` is left default-constructed on
// refusal.
//
// W32-0.1 (R31-1, High): an AUTOMATIC edit (one that carries slack) is also
// refused while ANY percent or actuals edit is queued in the same save
// (`pending_actuals`, on any row, not only the moved subtree and anchor) or any
// task start edit is pending (`pending_start_uids`, the uids
// app::collect_start_edits would write).  The automatic save rewrites every
// live automatic row's slack and late dates from the FILE's progress and dates,
// so a queued edit changes the shape the file-wide check cannot see.  A manual
// edit carries no slack and keeps the narrower moved-row rule.
bool make_outline_edit(const PlanInput& plan, std::string_view source_path,
                       std::uint32_t uid, bool indent, StructuralEdit& out,
                       std::string& error,
                       const std::vector<std::uint32_t>& pending_actuals = {},
                       const std::vector<std::uint32_t>& pending_start_uids = {});

// ---------------------------------------------------------------------------
// Wave 28 (W28-3.1): the project's new-task scheduling mode
// ---------------------------------------------------------------------------
//
// Builds a ready-to-push set_new_task_mode edit for `manual` (true = Manually
// Scheduled, false = Auto Scheduled -- Project's NewTasksCreatedAsManual).
// The FILE is the authority, as in make_outline_edit: mpp::read_new_task_mode
// must know the stored mode.  A file with no 114/Props record 0x024013c8 --
// every original Project 2007 MPP12 file, and the new-project state, which has
// no file at all -- is refused with `error`, because the port has not measured
// a record to add and the save pair is what proves the write.  `out` is left
// default-constructed on refusal.
bool make_new_task_mode_edit(std::string_view source_path, bool manual,
                             StructuralEdit& out, std::string& error);

// ---------------------------------------------------------------------------
// Wave 30 (W30-1.0): one task's scheduling mode
// ---------------------------------------------------------------------------
//
// Builds a ready-to-push set_task_mode edit that changes `uid` from automatic
// to manually scheduled, the direction Project's committed pair measured
// (tests/data/wave30-task-mode-auto-control.mpp -> wave30-task-mode-manual.mpp;
// core/mppwrite/task_mode.h).  The FILE is the authority: the whole-file shape
// (an empty link table, every task row a top-level leaf with a duration, the
// project calendar, no progress anywhere, one live placeholder assignment per
// task) and the edited row's measured precondition bytes are checked with
// mppwrite::read_task_mode_shape when the edit is built, so the refusal is
// never deferred to save time.  The plan is checked too: the displayed row must
// be a saved, live, non-summary, top-level, automatic row whose start, finish
// and duration are the file's, with no links, no progress and one placeholder
// assignment.  `manual == false` (Auto Schedule) is refused: Project's reverse
// save also reschedules the row, and this port has no scheduler.  `out` is left
// default-constructed on refusal with `error` naming the reason.
bool make_task_mode_edit(const PlanInput& plan, std::string_view source_path,
                         std::uint32_t uid, bool manual, StructuralEdit& out,
                         std::string& error);

// ---------------------------------------------------------------------------
// Wave 34 (W34-4.1): one resource's name
// ---------------------------------------------------------------------------
//
// Builds a ready-to-push rename_resource edit: the Resource Information
// dialog's Resource name, written through mppwrite::rename_resource against
// Project's committed pair (tests/data/wave30-resource-rename-control.mpp ->
// wave30-resource-rename.mpp).  The name is trimmed (ASCII whitespace) and
// must be non-empty, free of control characters and of , [ ], and at most 3
// characters (the "R2b" the pair stores; the writer refuses the same).  The measured shape is a LARGER stored
// payload, so a name that encodes to the same size or smaller than the file's
// current block is refused with that reason: Project's shrink re-point is
// unmeasured.  The FILE is the authority for the current block (it is read
// here, not taken from the display), and the plan must show the row as a saved,
// live resource that NO assignment names: the pair renamed resource uid 2, which
// has no assignment row, so an assigned resource's rename (which Project may
// also reflect in task-side caches of the resource names) is refused.  `out` is
// left default-constructed on refusal with `error` naming the reason (the
// decision-9 "is not implemented in this port" text for the unmeasured shapes).
bool make_rename_resource_edit(const PlanInput& plan, std::string_view source_path,
                               std::uint32_t uid, std::string_view name,
                               StructuralEdit& out, std::string& error);

// ---------------------------------------------------------------------------
// Wave 34 (W34-4.2): Add Resources
// ---------------------------------------------------------------------------
//
// Builds a ready-to-push add_resource edit: one work resource with default
// rates (Standard rate, Overtime rate and Cost/Use zero, 100 % Max. Units, the
// Prorated-free default accrual), named `name`, written through mppwrite::
// add_work_resource against Project's two committed pairs
// (tests/data/wave30-resource-add-control.mpp -> wave30-resource-add.mpp, one
// existing resource; wave34-resource-add-third-control.mpp -> ...-third.mpp, two).
// The name is trimmed (ASCII whitespace) and must be 1..2 printable ASCII
// characters (the longest a committed pair adds), none of , [ ]; `created` is the packed Created stamp (current_created_stamp()
// in a shell, the pair's own stamp in a test).  The FILE is the authority for
// the shape: the writer's dry run validates every stream (the resource count,
// the calendar table, the Props counts, the per-count flag bytes) and its
// refusal text is returned as the reason; the plan must show the same number of
// saved resources the file holds.  `out` is left default-constructed on refusal
// with `error` naming the reason (the decision-9 "is not implemented in this
// port" text for the unmeasured shapes).
bool make_add_resource_edit(const PlanInput& plan, std::string_view source_path,
                            std::string_view name, std::uint32_t created,
                            StructuralEdit& out, std::string& error);

// Builds a ready-to-push assign_resource or unassign_resource edit for ONE
// task and ONE resource (W34-4.3).  The plan-side rules are the measured pair's:
// the task is a saved, unstarted LEAF (0 % complete, no actual start or finish
// or actual duration, not a summary), the resource a saved WORK resource; an
// assign needs a task with no real assignment (the pair's task holds only
// Project's unassigned placeholder), an unassign needs the task's one real
// assignment of exactly that resource.  The FILE is the authority for the rest:
// the writer's dry run validates every stream and its refusal text is returned
// as the reason ("... is not implemented in this port for this project: ...").
// `out` is default-constructed on refusal.
bool make_assignment_edit(const PlanInput& plan, std::string_view source_path,
                          std::uint32_t task_uid, std::uint32_t resource_uid,
                          bool assign, StructuralEdit& out, std::string& error);

// Builds a ready-to-push bold_cell edit for ONE cell (W34-4.5).  The plan-side
// rule is the measured pair's own cell: Project's wave30-textstyle formats the
// Task Name cell of the task with UniqueID 1, and one pair is one data point
// (brief rule 2), so every other row and every other column is refused here with
// the decision-9 "is not implemented in this port" text.  The FILE is the
// authority for the view shape: the writer's dry run validates the three view
// streams (the applied view, its main block, the absence of an existing cell
// format item, the Props size tag) and its refusal text is returned as the
// reason.  `out` is default-constructed on refusal.
bool make_cell_bold_edit(const PlanInput& plan, std::string_view source_path,
                         std::uint32_t task_uid, std::uint32_t field_id,
                         StructuralEdit& out, std::string& error);

// Builds a ready-to-push define_wbs_code edit (W34-4.6).  The plan-side rule is
// the measured pair's own population: Project's wave30-wbs holds exactly three
// saved tasks with UniqueIDs 1, 2 and 3, all top-level and none a summary, and
// one pair is one data point (brief rule 2), so every other count, a nested
// outline or a summary is refused with the decision-9 "is not implemented in
// this port" text.  The FILE is the authority for the rest: the writer's dry run
// validates the task rows, the absence of any stored WBS text or code
// definition, and the two 114/Props records, and its refusal text is returned
// as the reason.  `out` is default-constructed on refusal.
bool make_wbs_code_edit(const PlanInput& plan, std::string_view source_path,
                        StructuralEdit& out, std::string& error);

// Builds a ready-to-push calculate_project edit (W35-1.3).  The plan-side rule
// is the measured pair's own population: the project row plus exactly two
// unlinked, automatically scheduled, top-level leaves with no constraint or
// progress; any link, summary, manual row or other count is refused with the
// "is not implemented in this port" text.  The FILE is the authority for the
// rest: the mppwrite shape check requires the recalculation to be PENDING (the
// project row's Finish is not yet the latest leaf Finish), and the recalculated
// words -- the project Duration and Finish, and each leaf's free/start/total
// slack and late dates -- are computed here from the file's rows and the project
// calendar by the rule build_outline_slack established (the four wave-29 pairs)
// and proved equal to Project's own result for this shape by
// app_edits_calculate_tests.  The mppwrite dry run (validate_calculate) then
// checks the request against the file, and its refusal is returned as the
// reason.  `out` is default-constructed on refusal.
bool make_calculate_edit(const PlanInput& plan, std::string_view source_path,
                         StructuralEdit& out, std::string& error);

// Wave 35 (W35-2.1): the sort keys make_sort_edit can be asked for.  Only Name
// and Duration are measured as single keys with Outline = True and
// Renumber = False; every other choice is refused with its own reason.
enum class SortKeyKind : std::uint8_t {
    name = 0,
    duration = 1,
    id = 2,
    start = 3,
    finish = 4,
    other = 5,
};

struct SortKeyRequest {
    SortKeyKind kind = SortKeyKind::name;
    bool ascending = true;
};

// Builds a ready-to-push sort_view edit (W35-2.1).  The measured shape is Project
// > Sort with ONE key (Name or Duration), "Keep outline structure" checked and
// "Permanently renumber tasks" unchecked, on the three-level pair's five saved
// tasks; two keys, an ID / Start / Finish / other key, Outline = False and
// Renumber = True are each refused up front with their own reason.  The order
// the edit queues is computed here under the pins the pairs support: siblings
// (rows under one parent, from the outline level) are compared; Name only when
// every sibling pair differs at a first position holding two ASCII letters of
// the same case, neither name a prefix of the other (Project's collation of
// case, punctuation and digits is not measured); Duration only when sibling
// durations are pairwise distinct.  The FILE is the authority for the rest: the
// writer's dry run validates the applied view, the default sort specification
// in both copies, the displayed-order records and the measured block, and its
// refusal text is returned as the reason.  `out` is default-constructed on
// refusal.
bool make_sort_edit(const PlanInput& plan, std::string_view source_path,
                    const std::vector<SortKeyRequest>& keys, bool outline, bool renumber,
                    StructuralEdit& out, std::string& error);

// Builds a ready-to-push show_level edit (W35-2.3): View > Outline > Show > Level
// `level` (1-based) on the task sheet.  Measured shape: the pairs' five-task
// outline (levels 1,2,2,1,1) and six-task three-level outline (1,2,3,3,2,1),
// every row shown in the file, Level 1 and Level 2 only.  Refused up front, each
// with its own reason: a level other than 1 or 2 (the shell keeps it for the
// session), a pending task, a file that already collapses or hides a row, a
// level that hides nothing on this outline (there is nothing to persist), and an
// outline of another shape.  The writer's dry run validates the file.  `out` is
// default-constructed on refusal.
bool make_show_level_edit(const PlanInput& plan, std::string_view source_path, int level,
                          StructuralEdit& out, std::string& error);

// The one cell Font Bold is measured for: Task Name of the task with
// UniqueID 1 (core/mppwrite/viewstream_edit.h, wave30-textstyle).
inline constexpr std::uint32_t kBoldMeasuredTaskUid = 1;
inline constexpr std::uint32_t kBoldMeasuredFieldId = 0x0B40000Eu;

// The Created stamp Project writes for a resource added now: the local date
// and time as a packed date (day 0 is 1983-12-31, then seconds / 6).
std::uint32_t current_created_stamp();

// ---------------------------------------------------------------------------
// Wave 34 (W34-4.0): the five paths whose save does not match Project's
// ---------------------------------------------------------------------------
//
// W34-0.5 measured Insert Task, Delete Task, link ADD and the custom-field SET
// and CLEAR against Project on the synthetic base and found that the port's
// save differs from Project's own save of the same edit in 15, 6, 13, 2 and 5
// streams (the named `KnownGap` tables in tests/roundtrip_add_delete.cpp,
// roundtrip_links.cpp and roundtrip_field_values.cpp).  None of those
// differences is per-save noise (the null saves separate the noise first), so
// standing rule 1 applies: the five edit kinds are refused up front, with the
// decision-9 reason, until their writers match.  Every other kind is unaffected.
//
// `unmeasured_path_refusal` returns the reason for exactly those five kinds and
// an empty string for every other kind.  The shared builders (check_insert_task,
// make_delete_edit, app::validate_link_add, app::make_task_field_edits), the
// queue (StructuralEdits::push) and the save cascade (save_structural_edits)
// all consult it, so a hand-built edit cannot slip past a shell.
//
// `allow_unmeasured_paths_for_tests(true)` lifts the refusal.  Only test
// programs call it (the retained writers and the cascade keep their own
// round-trip and shape tests); no shell or library code does.
[[nodiscard]] std::string unmeasured_path_refusal(StructuralKind kind);
void allow_unmeasured_paths_for_tests(bool allowed) noexcept;
[[nodiscard]] bool unmeasured_paths_allowed_for_tests() noexcept;

// The pending edits, in application order, with an undo/redo cursor.
class StructuralEdits {
  public:
    bool empty() const noexcept { return applied_.empty(); }
    std::size_t size() const noexcept { return applied_.size(); }
    bool can_undo() const noexcept { return !applied_.empty(); }
    bool can_redo() const noexcept { return !undone_.empty(); }
    // The edit undo()/redo() would move, or nullptr.
    const StructuralEdit* next_undo() const noexcept;
    const StructuralEdit* next_redo() const noexcept;

    // W33-1.7 (A-8 / D-7): an opened container with FAT slack is read-only for
    // the session (mppwrite's to_bytes() refuses to re-emit it, so no save can
    // ever write an edit).  While a reason is set, push() refuses EVERY kind
    // and queues nothing; the shells set this from
    // app::document_has_unreferenced_sectors() and show the reason in the
    // status line.  A load/new-document clears it with the stack.
    void set_read_only_reason(std::string reason) {
        read_only_reason_ = std::move(reason);
    }
    const std::string& read_only_reason() const noexcept { return read_only_reason_; }

    // Records one edit.  False (changing nothing) for a malformed edit: an empty
    // name, uid 0, a duplicate delete, an insert whose placeholder uid is already
    // used, an unmeasured/wrongly typed field value, a self-link, a duplicate
    // (predecessor, successor, type) add, a duplicate link delete, or a link
    // whose endpoint has a pending task delete.  A push clears the redo list.
    //
    // Push is plan-free for the edit kinds whose refusal the edit itself proves.
    // The duplicate-against-the-file and live-endpoint checks need the plan, so
    // the shell calls app::validate_link_add() first (app/field_link_edits.h).
    // An add_link is also refused while a slack-carrying (automatic) outline
    // edit is applied: that edit's save would write the unlinked slack and late
    // dates beside the new link (W31-0.1).
    //
    // A set_percent edit REQUIRES `plan` (W27-0.2): the row must be a live,
    // non-summary, saved file task (make_percent_edit; file_row != 0), and a
    // plan-less set_percent is refused rather than trusted.
    //
    // A delete_task edit REQUIRES `plan` too (W33-1.1, A-3): the uid must name a
    // live saved row, and a SUMMARY row is refused.  The writer tombstones the
    // summary alone, leaving live subtasks with a parent identity pointing at a
    // tombstone -- a shape no Project save pair measured.  make_delete_edit
    // builds the edit and names the refusal for the shell; push() re-checks the
    // same facts so a hand-built queue cannot bypass them.
    //
    // An insert_task edit uses `plan` when one is given (W33-1.2, A-9): the row
    // add_task_row would clone must be a clean fresh task (check_insert_task
    // names the reason for the shell), and push() refuses without it.  A
    // plan-less insert is still accepted for a hand-built display/test queue;
    // the save cascade checks the file's actual clone source.
    //
    // `plan` is ignored for the remaining kinds.
    //
    // An update_actuals edit also REQUIRES `plan` (W28-1.3): every target must
    // pass the same leaf/saved-row/summary rules, uids must be distinct, the
    // actual ticks must agree with the percent and the row's duration, and a
    // target with a pending task delete is refused (the row would be gone when
    // the save ran).  The through date must be a real date.
    //
    // An outline edit REQUIRES `plan` too (W28-2.1): `uid` must be a saved row
    // (file_row != 0) at the display level the shape needs (top level for
    // indent, below the top for outdent), a summary is refused for outdent,
    // `anchor_uid` must be the display row the shape names (the preceding
    // sibling for indent, the parent for outdent), the restored words must be
    // valid dates in order, and a pending delete of the moved row or its
    // anchor is refused.  make_outline_edit performs the file-side checks;
    // push() re-checks everything the plan alone can prove.
    //
    // A set_new_task_mode edit is plan-free: it names no task, and the only
    // refusal (the file does not store the record) is make_new_task_mode_edit's
    // file-side check.  Two pending mode edits are allowed; the LAST one wins at
    // save, matching the display's "the newest user choice is what is shown".
    //
    // A set_task_mode edit (W30-1.0) REQUIRES `plan` and is EXCLUSIVE in both
    // directions: it may only be queued when no other edit is applied, and
    // while it is applied every other push is refused.  It also refuses
    // `task_manual == false`, a row a pending delete names, and (from the plan)
    // a non-saved, non-top-level, summary, already-manual, linked, progressed
    // or non-placeholder-assigned row.  make_task_mode_edit performs the
    // file-side checks when the edit is built.
    //
    // A rename_resource edit (W34-4.1) REQUIRES `plan` and is EXCLUSIVE in the
    // same way (and with set_task_mode: neither may be queued over the other).
    // It refuses uid 0, a name that is empty after trimming or holds a control
    // character (or a non-ASCII one), a resource the plan does not show as a
    // saved row, a resource an assignment names, and a second pending rename.  make_rename_resource_edit performs the file-side
    // checks (the current block, the strictly-larger rule) when it is built.
    //
    // An add_resource edit (W34-4.2) REQUIRES `plan` and is EXCLUSIVE the same
    // way.  It refuses a uid or ID the plan already uses, a plan that does not
    // hold one or two saved resources (the two measured counts), a name that is
    // not 1..2 printable ASCII characters free of , [ ] and a Created stamp that is not a
    // packed date.  make_add_resource_edit performs the file-side checks.
    //
    // Pushing a delete_task also removes the set/clear field edits, percent
    // edits, update_actuals edits that name the task and add_link edits: those
    // could never reach the file (the writer writes the fields/links before the
    // delete, and the delete cascade would tombstone the link again).  A date
    // update names a task when any of its targets does; the whole update is
    // displaced, not half-applied.  They are not lost: the delete keeps them in
    // StructuralEdit::displaced, undo() restores them and redo() removes them
    // again (W27-3.2).  Documented in app/README.md.
    //
    // W32-0.1 (R31-1, High): an AUTOMATIC outline edit (slack non-empty) is
    // exclusive with every percent/actuals edit and every pending task start
    // edit in the same save, in both orders.  Queueing the outline edit while
    // one is applied is refused; queueing a percent/actuals edit while an
    // automatic outline edit is applied is refused on every row (the old
    // subtree/anchor rule, outline_edit_pending_for, remains for manual outline
    // edits).  A start edit is not a StructuralEdit, so the shell names the
    // pending start uids in `pending_start_uids`; save_structural_edits repeats
    // both refusals before the first write.
    bool push(StructuralEdit edit, const PlanInput* plan = nullptr,
              const std::vector<std::uint32_t>& pending_start_uids = {});

    // True while an applied outline edit carries slack (the automatic shape
    // whose save rewrites every live automatic row).  The shell uses it to name
    // the reason when push() refuses a percent/actuals edit.
    bool has_automatic_outline_edit() const noexcept;

    // Moves the newest applied edit to the redo list; a delete_task's
    // displaced edits return to the applied list in their original order.
    bool undo();
    // Re-applies the newest undone edit; a delete_task removes its displaced
    // edits again.
    bool redo();
    void clear_redo() noexcept { undone_.clear(); }

    const std::vector<StructuralEdit>& applied() const noexcept { return applied_; }

    // uid is a pending insert's placeholder (so a Delete on it means "drop the
    // insert", never a writer delete of a row that does not exist).
    bool is_pending_insert(std::uint32_t uid) const noexcept;
    // Removes a pending insert (its uid) without touching the undo list of the
    // other edits.  False when no such insert is pending.
    bool drop_insert(std::uint32_t uid);

    std::size_t inserted_count() const noexcept;
    std::size_t deleted_count() const noexcept;
    std::size_t field_edit_count() const noexcept;
    std::size_t link_edit_count() const noexcept;

    // The next placeholder uid for a plan: one past the largest uid in use,
    // including the pending inserts.  0 only when every uid is taken (impossible
    // in practice); the caller must then refuse the insert.
    std::uint32_t next_placeholder_uid(const PlanInput& plan) const noexcept;

    // The plan to display: `loaded` with every applied edit.  See the header
    // comment; the inputs are not modified.
    PlanInput apply(const PlanInput& loaded) const;

    // The INVERSE for the rows a cell edit can touch.  `shown` is the plan the
    // editor displays (loaded + these edits + every in-memory cell edit); the
    // result is that plan's values for the tasks the file still has, in `loaded`'s
    // order, with the pending-deleted tasks restored from `loaded` and the pending
    // inserted placeholders omitted.  apply() on the result therefore rebuilds the
    // display WITHOUT discarding a Start edit the user made before the structural
    // edit.  (A pending insert's own cell values are not carried: the writer's add
    // writes the name it was given and clones the file's last row for the rest, so
    // a placeholder's other cells are a display aid, not a value to write.)
    //
    // Link and field edits are re-derived by apply() from `loaded`, so the
    // recovered plan always carries the file's links and stored custom fields;
    // only the cell values of the surviving tasks come from `shown`.
    PlanInput without_structural_edits(const PlanInput& shown, const PlanInput& loaded) const;

  private:
    // Removes from applied_ every edit a delete of `uid` displaces (the same
    // set push has always dropped), preserving their relative order.  push()
    // and redo() store the result in the delete's `displaced` (W27-3.2).
    std::vector<StructuralEdit> detach_edits_naming(std::uint32_t uid);

    std::vector<StructuralEdit> applied_;  // in application order
    std::vector<StructuralEdit> undone_;   // a redo pops its back
    // W33-1.7 (A-8 / D-7): non-empty only for a document the shell opened from
    // a container with unreferenced sectors; push() then refuses every edit.
    std::string read_only_reason_;
};

// How many dependent rows a delete of `uid` would tombstone: every link whose
// predecessor OR successor is the uid, plus every assignment for it.  This is
// the number the confirmation dialog names, and it equals the row count
// apply() removes from the display plan.
std::size_t count_delete_dependants(const PlanInput& plan, std::uint32_t uid) noexcept;

// W33-1.1 (A-3, High): builds a ready-to-push delete_task edit for `uid` in the
// display plan `plan`.  A summary row is refused because the writer tombstones
// the row alone, leaving its live subtasks with a parent identity pointing at a
// tombstone; no Project save pair measures that shape.  The refusal names the
// remedy in plain words: "delete the subtasks first, or outdent them".  An
// unknown uid (including a pending insert's placeholder, which has no saved
// row) is refused too.  On acceptance `out` carries the uid and the dependent
// row count; on refusal it is left default-constructed and `error` names the
// reason.  The shells call this before asking for confirmation; push() repeats
// the plan checks.
bool make_delete_edit(const PlanInput& plan, std::uint32_t uid, StructuralEdit& out,
                      std::string& error);

// W33-1.2 (A-9, High): Insert Task clones the file's LAST live row
// (mppwrite::add_task_row: its 202 FixedData bytes and its five overtime VarMeta
// payloads, with only the constraint and the start anchor cleared).  A clone of a
// row that carries recorded state saves the new task with that state -- after a
// progressed last row, in progress and without the 0x0B40015E block Project
// writes.  Project's own Insert Task pair measured a fresh 0 % row, so this
// refuses up front instead.
//
// `plan` supplies the row the save would clone: the live saved row with the
// largest file_row, i.e. the display plan with pending deletes already removed
// (a pending insert has file_row 0 and is not cloned by the edit being checked).
// The plan facts are checked first (progress, actual dates and work time, a task
// calendar, a constraint other than ASAP, an assignment beyond Project's
// 0xFFFF0001 placeholder).  `source_path`, when non-empty, is the saved file the
// plan was loaded from; it supplies the words the plan does not carry
// (Resume/Stop, percent work complete, the 0x0B40015E progress block and the
// overtime work/cost payloads) through mppwrite::insert_task_clone_is_clean.
// On refusal `error` names the row and the fact in plain words; on acceptance it
// is cleared.  The shells call this before asking for a name; push() repeats the
// plan half and the save cascade repeats the file half (an undo can restore a
// row after the insert was queued).
bool check_insert_task(const PlanInput& plan, std::string_view source_path,
                       std::string& error);

// What save_structural_edits() proved / reported.
struct StructuralSaveResult {
    bool ok = false;
    std::size_t added = 0;
    std::size_t deleted = 0;
    // Wave 13: link rows added/tombstoned and custom-field values written
    // (including cleared ones).
    std::size_t links_added = 0;
    std::size_t links_deleted = 0;
    std::size_t fields_written = 0;
    // The uid the writer allocated for each added row, in edit order (read back
    // from the file; the placeholder uid is NOT reused).
    std::vector<std::uint32_t> added_uids;
    // The link uid the writer allocated for each added link, in edit order.
    std::vector<std::uint32_t> added_link_uids;
    std::size_t links_tombstoned = 0;
    std::size_t assignments_tombstoned = 0;
    std::size_t start_rows_written = 0;
    // Wave 26 (W26-1.2): tasks whose PercentComplete was written.
    std::size_t percent_rows_written = 0;
    // Wave 26 (W26-5.1): 1 when the project row's Priority was written.
    std::size_t priority_rows_written = 0;
    // Wave 28 (W28-2.1): outline edits the writer applied.  A refused edit is
    // reported in `warnings` and is not counted; the rest of the cascade lands
    // (the W27-3.1 refusal rule).
    std::size_t outline_rows_written = 0;
    // Wave 28 (W28-3.1): 1 when the project's new-task mode was written.
    std::size_t new_task_mode_written = 0;
    // Wave 30 (W30-1.0): 1 when a task's scheduling mode was written.
    std::size_t task_mode_written = 0;
    // Wave 34 (W34-4.1): 1 when a resource's name was written.
    std::size_t resources_renamed = 0;
    // Wave 34 (W34-4.2): 1 when a work resource (and its calendar row) was added.
    std::size_t resources_added = 0;
    // Wave 34 (W34-4.3): 1 when an assignment was added or removed.
    std::size_t assignments_written = 0;
    // Wave 34 (W34-4.5): 1 when a Font Bold cell-format item was appended.
    std::size_t cell_styles_written = 0;
    // Wave 34 (W34-4.6): 1 when the WBS code was defined (four number blocks).
    std::size_t wbs_codes_written = 0;
    // Wave 35 (W35-1.3): 1 when Calculate Project was written (the project row,
    // two leaves and five overtime records).
    std::size_t calculations_written = 0;
    // Wave 35 (W35-2.1): 1 when a sort specification and displayed order were
    // written.
    std::size_t sorts_written = 0;
    // Wave 35 (W35-2.3): 1 when an outline level was written.
    std::size_t outlines_written = 0;

    // Wave 17: the document-edit seam (DocumentEditHook below).  All three stay
    // at their defaults when no hook was supplied -- that is exactly the
    // pre-wave-17 result.
    bool document_edits_requested = false;  // view/formula edits were passed in
    bool document_edits_written = false;    // the hook ran and reported ok
    DocumentEditOutcome document_edits;     // exactly what the hook reported

    std::vector<std::string> warnings;  // writer + app warnings worth showing
    std::vector<std::string> notes;     // informational re-read receipts
    std::string error;                  // non-empty exactly when ok == false
    std::string path;                   // the target that was written
    std::string api;                    // which writer calls ran
};

// ===========================================================================
// Wave 17: the document-edit seam -- structural + field + view state + formula
// in ONE container write
// ===========================================================================
//
// `save_structural_edits` applies one `core/mppwrite` writer per step and only
// the LAST call writes the target; every earlier one writes (and then deletes)
// an intermediate file beside it.  The two document-level operations of wave 15
// -- the `214/CTable` view state and the `114/Props` custom-field formula, applied
// by `app::apply_document_edits` in app/document.cpp -- belong on the
// `CfbDocument` that sequence already owns, AFTER the field-value writers and
// BEFORE that one final write:
//
//   * the formula step is the only one that can decide the shared 0x02401392
//     "custom-field data in use" flag, and it must see any custom-field VALUE
//     the field writers just wrote to the same document (docs/handoff/w64.md
//     section 5); and
//   * neither operation can invalidate a row/table index, so running last keeps
//     every earlier writer's own post-save re-read proof valid.
//
// `DocumentEditHook` is that seam.  It mutates `document` in memory only (the
// cascade owns every file write) and reports what it did; a refusal
// (`ok == false`) fails the save before the target is written.
// `app::apply_document_edits` has exactly this signature, so it is passed
// directly:
//
//   app::StructuralDocumentEdits request;
//   request.view = document.view_state_edits();
//   request.formula = document.custom_formula();
//   request.hook = app::apply_document_edits;
//   app::save_structural_edits(edits, start_edits, source, target, false, request);
//
// `app::Document::save_structural()` (document.h) is the one-call form of this
// for a shell that already owns a Document.
using DocumentEditHook =
    std::function<DocumentEditOutcome(mppwrite::CfbDocument& document,
                                      const ViewStateEdits& view,
                                      const CustomFormulaEdit& formula)>;

// The pending document-level edits plus the hook that applies them inside the
// cascade.  The default (an empty request) means "no document edits" and leaves
// the cascade exactly as it was before wave 17: same writer calls, same write
// routing, byte-identical output.
struct StructuralDocumentEdits {
    ViewStateEdits view;
    CustomFormulaEdit formula;
    // Null means "not supplied".  Supplying edits WITHOUT a hook is refused
    // rather than silently dropped (see `result.error`).
    DocumentEditHook hook;

    [[nodiscard]] bool any() const noexcept { return view.any() || formula.any(); }
};

// Applies every pending edit to `source_path` and writes `target_path`, in this
// order (the cascade order the brief fixes):
//   1. delete_link    -> mppwrite::delete_task_link (tombstone the link row);
//   2. add_link       -> mppwrite::add_task_link (the writer allocates the uid);
//   3. delete_task    -> mppwrite::delete_task_row (the task plus its
//                        link/assignment cascade);
//   4. insert_task    -> mppwrite::add_task_row (the writer clones the last live
//                        row, clears its constraint and re-reads the file);
//   5. set_field      -> one mppwrite::save_field_values call for every stored
//                        value (one file);
//      clear_field    -> one mppwrite::clear_field_value call per field, because
//                        clearing is the measured empty-value shape;
//   5a. set_percent    -> the measured actuals writer
//                        (mppwrite::write_percent_actuals, wave 28 W28-1.2):
//                        ActualDuration/RemainingDuration/% words, the four
//                        actual dates, the progress flag, the project row's
//                        roll-up and the row's Var2Data steps -- Project's own
//                        save shape (tests/data/wave28-percent-*.mpp), not the
//                        wave-26 one-word +92 patch.  A percent edit whose uid
//                        the source table does not carry is REFUSED with a
//                        warning and does not fail the save (W27-3.1): the rest
//                        of the cascade still lands, so a bad edit can never
//                        block every later Save;
//   5b. outline edits  -> mppwrite::write_outline_edits (W28-2.1): one call for
//                        every queued indent/outdent, in application order,
//                        after the actuals write (so a date update's changed
//                        child dates are the ones the summary dates come from)
//                        and before the hook.  A refused edit is a warning, not
//                        a failed save, exactly like step 5a;
//   5c. document edits-> only when `document_edits.hook` is set: the hook runs on
//                        the SAME CfbDocument, after steps 5/5a/5b and before
//                        the final write (this is the wave-17 seam);
//   5d. new-task mode  -> mppwrite::write_new_task_mode (W28-3.1): the LAST
//                        pending set_new_task_mode edit writes 114/Props
//                        0x024013c8 (measured bytes: project_props.h).  It runs
//                        after the hook, which cannot depend on it; a missing or
//                        wrongly shaped record fails the save (this is the one
//                        step whose refusal is not a warning, because there is
//                        no measured record to add);
//   5e. task mode      -> mppwrite::write_task_mode (W30-1.0): the ONE pending
//                        set_task_mode edit writes the measured automatic ->
//                        manual bytes (core/mppwrite/task_mode.h).  The step
//                        REQUIRES the edit to be the only structural edit in
//                        the save and refuses any writable start edit: the
//                        Project pair is one mode change on an otherwise
//                        untouched save, and combining it with a date write
//                        would produce a shape no pair measures.  It runs after
//                        the hook and before the final write.  The writer's
//                        shape refusal is a warning, like steps 5a/5b;
//   6. start dates    -> the normal mppwrite::save_date_edits path; when no start
//                        edit is writable but a hook ran, the final write is an
//                        explicit mppwrite::CfbDocument::save instead.
//
// W32-0.2 (R31-2, High): a writable start edit is REFUSED before the first
// write.  The wave-31 Fill pair shows Project's Start save rewrites the
// calculated and effective dates, slack, the project roll-up, a Var2 record and
// DSI; mppwrite::save_date_edits stores only the constraint window and the
// manual bit, so no Start save (alone or beside a structural edit) is measured
// against Project.  The shell makes Start read-only and the writer stays in the
// tree for a later wave; this refusal is the model's defence in depth.  The
// task-mode check's start combination (5e) is refused earlier, with its own
// message, because the mode pair's exclusivity is the narrower rule.
// Every writer call writes a file, so all but the LAST one write an intermediate
// file beside the target and it is removed afterwards (a refusal removes it too).
// With a hook the LAST write is always the one after it -- step 6, or that
// explicit container save -- so the target is written once, hook included.
// If an earlier step fails, no later step runs and the target is not written at
// all: the original file is never touched (the app also refuses target ==
// source unless `allow_source_overwrite`, the shell passes true only for a file
// this app itself saved).  A field edit on a task a pending delete removes is
// dropped when the delete is pushed, so step 5 can never address a dead uid.
//
// The caller must reload the target afterwards and rebuild from the file: the
// added rows'/links' real uids and ids are the file's, never patched here.
StructuralSaveResult save_structural_edits(const StructuralEdits& edits,
                                           const std::vector<StartEdit>& start_edits,
                                           std::string_view source_path,
                                           std::string_view target_path,
                                           bool allow_source_overwrite,
                                           const StructuralDocumentEdits& document_edits = {});

}  // namespace app
