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
bool make_outline_edit(const PlanInput& plan, std::string_view source_path,
                       std::uint32_t uid, bool indent, StructuralEdit& out,
                       std::string& error,
                       const std::vector<std::uint32_t>& pending_actuals = {});

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
    // plan-less set_percent is refused rather than trusted.  `plan` is ignored
    // for the other kinds.
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
    // Pushing a delete_task also removes the set/clear field edits, percent
    // edits, update_actuals edits that name the task and add_link edits: those
    // could never reach the file (the writer writes the fields/links before the
    // delete, and the delete cascade would tombstone the link again).  A date
    // update names a task when any of its targets does; the whole update is
    // displaced, not half-applied.  They are not lost: the delete keeps them in
    // StructuralEdit::displaced, undo() restores them and redo() removes them
    // again (W27-3.2).  Documented in app/README.md.
    bool push(StructuralEdit edit, const PlanInput* plan = nullptr);

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
};

// How many dependent rows a delete of `uid` would tombstone: every link whose
// predecessor OR successor is the uid, plus every assignment for it.  This is
// the number the confirmation dialog names, and it equals the row count
// apply() removes from the display plan.
std::size_t count_delete_dependants(const PlanInput& plan, std::uint32_t uid) noexcept;

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
