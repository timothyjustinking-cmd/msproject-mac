// app/edit_model.h -- pending structural edit of the task list: insert and delete.
//
// WHY THE APP KEEPS THEM PENDING
//   `core/mppwrite` can append one Task row and tombstone one Task row
//   (`add_delete.h`, W45; the delete cascades to dependent link/assignment rows,
//   W48/w11-oracle section 2).  It cannot do both inside one writer call, and it
//   writes a FILE: the app's one save path owns "write once, reload, rebuild".
//   So the app records what the user asked for (a name to add, a uid to delete),
//   shows the result immediately from a locally built plan, and applies the
//   whole set on Save.  Nothing here touches a file except
//   save_structural_edits().
//
// WHAT THE DISPLAY PLAN IS
//   StructuralEdits::apply(loaded) returns a copy of the loaded plan with the
//   pending edits applied: deleted tasks removed, a delete's dependent links and
//   assignments removed too (the same rows the writer tombstones, so the Gantt
//   and the usage grids do not show a link or assignment the saved file will not
//   have), inserted tasks placed after their anchor task (or at the end).  The
//   shell rebuilds its RecordSet/SheetModel/Gantt from that plan, which is why a
//   pending insert has a real row and a pending delete has no row -- and why an
//   undo is just "rebuild from the plan without that edit".
//
//   An inserted placeholder copies the duration and dates of the last live task
//   (or none when the plan is empty).  That mirrors the writer's documented add
//   shape -- `add_task_row` clones the last live row and clears its constraint --
//   so the placeholder sits where the saved row will, without inventing a
//   schedule.  The file is the authority: after a save the shell reloads it.
//
// UNDO / REDO
//   Two stacks: this one, and core::sheet::SheetModel's cell-edit history.  The
//   shell keeps them chronological by clearing the sheet's history when a
//   structural edit is pushed (so a cell edit is never undone "past" a later
//   structural edit) and clearing this redo stack when a cell edit is committed.
//   `undo()`/`redo()` only move the edit between the two lists; the caller
//   rebuilds its derived state afterwards.
//
// WHAT THIS FILE DOES NOT DO
//   It does not write links or assignments (the cascade is the writer's), does not
//   renumber IDs or UniqueIDs (the file's writer allocates them and the shell
//   reloads, never patches a uid by hand), and does not schedule: a new row's
//   dates are a display placeholder until the file says otherwise.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "document.h"
#include "layout_model.h"

namespace app {

enum class StructuralKind : std::uint8_t {
    insert_task = 0,
    delete_task = 1,
};

// One pending structural edit, as the user asked for it.
struct StructuralEdit {
    StructuralKind kind = StructuralKind::insert_task;
    // delete: the task's UniqueID.  insert: the placeholder's in-app UniqueID
    // (never written; the writer allocates the real one).
    std::uint32_t uid = 0;
    // insert: the row is displayed after this task (0 = at the end).  Resolved
    // against the current plan on every apply(), so a delete of the anchor cannot
    // leave a dangling position.
    std::uint32_t anchor_uid = 0;
    // insert: the task's name (required, non-empty).
    std::string name;
    // delete: how many dependent rows the writer's cascade will tombstone
    // (links naming the uid + assignments for it), counted when the edit was
    // made so the confirmation can name it.
    std::size_t dependants = 0;
};

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
    // name, uid 0, a duplicate delete, or an insert whose placeholder uid is
    // already used.  A push clears the redo list.
    bool push(StructuralEdit edit);

    bool undo();
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
    PlanInput without_structural_edits(const PlanInput& shown, const PlanInput& loaded) const;

  private:
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
    // The uid the writer allocated for each added row, in edit order (read back
    // from the file; the placeholder uid is NOT reused).
    std::vector<std::uint32_t> added_uids;
    std::size_t links_tombstoned = 0;
    std::size_t assignments_tombstoned = 0;
    std::size_t start_rows_written = 0;
    std::vector<std::string> warnings;  // writer + app warnings worth showing
    std::vector<std::string> notes;     // informational re-read receipts
    std::string error;                  // non-empty exactly when ok == false
    std::string path;                   // the target that was written
    std::string api;                    // which writer calls ran
};

// Applies every pending edit to `source_path` and writes `target_path`:
//   1. a delete is `mppwrite::delete_task_row` (the writer tombstones the task
//      and cascade-tombstones its links/assignments);
//   2. an insert is `mppwrite::add_task_row` with the edit's name and an
//      allocated uid (the writer clones the last live row, clears its
//      constraint and re-reads the file);
//   3. when there are also pending start edits they are applied to the same
//      document through the normal `mppwrite::save_date_edits` path, so one Save
//      writes both.
// The structural writers each write a file, so when a start edit follows they
// write an intermediate file beside the target and it is removed afterwards; a
// refusal removes it too and leaves the target untouched.
//
// Refused (ok = false, nothing left behind): an empty edit list, an empty
// target, target == source unless `allow_source_overwrite` (the shell passes true
// only for a file this app itself saved), a writer refusal or any failed re-read.
// The caller must reload the target afterwards and rebuild from the file: the
// added rows' real uids and ids are the file's, never patched here.
StructuralSaveResult save_structural_edits(const StructuralEdits& edits,
                                           const std::vector<StartEdit>& start_edits,
                                           std::string_view source_path,
                                           std::string_view target_path,
                                           bool allow_source_overwrite);

}  // namespace app
