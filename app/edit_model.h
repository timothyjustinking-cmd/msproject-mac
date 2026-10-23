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
    // Wave 26 (W26-1.2): the ribbon's 0/25/50/75/100 % buttons.  `uid` is the
    // task and `percent` is 0..100; the writer has exactly one stored copy of
    // PercentComplete (mppwrite::TaskTable::set_percent_complete), so the edit
    // saves through the same cascade as the other structural edits.
    set_percent = 6,
    // Wave 26 (W26-5.1): Project Information's Priority.  The value lives in the
    // project row's FixedData +78 (row 0, uid 0) and is written through
    // mppwrite::TaskTable::set_priority on the project row -- the same
    // single-copy table write set_percent uses.
    set_project_priority = 7,
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
    // set_project_priority: Project's project priority, 0..1000.
    std::uint16_t priority = 500;
    core::gantt::LinkType link_type = core::gantt::LinkType::finish_to_start;
    // The lag in 6-second file ticks (4800 = one 8-hour working day).
    std::int32_t lag_ticks = 0;
};

// True for the six custom-field ids whose storage and 114/Props registration are
// measured: Text1 0x0b400033, Flag1 0x0b400048, Number1 0x0b400057, Number2
// 0x0b400058, Duration1 0x0b400067, Date1 0x0b400109.  Anything else (Cost,
// Flag2..20, an unknown id) is refused rather than guessed.
bool is_measured_task_field(std::uint32_t field_id) noexcept;

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
    // Push is plan-free: it can only refuse what the edit itself proves.  The
    // duplicate-against-the-file and live-endpoint checks need the plan, so the
    // shell calls app::validate_link_add() first (app/field_link_edits.h).
    //
    // Pushing a delete_task also drops the set/clear field edits and add_link
    // edits that name the task: those could never reach the file (the writer
    // writes the fields/links before the delete, and the delete cascade would
    // tombstone the link again).  Documented in app/README.md.
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
//   5a. set_percent    -> one TaskTable::set_percent_complete + store_into and one
//                        container write for all of them (wave 26; PercentComplete
//                        has exactly one stored copy, so no cross-stream fix-up);
//   5b. document edits-> only when `document_edits.hook` is set: the hook runs on
//                        the SAME CfbDocument, after step 5 and before the final
//                        write (this is the wave-17 seam);
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
