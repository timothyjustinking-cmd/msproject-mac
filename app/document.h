// app/document.h -- what the shell does to a document besides drawing it.
//
// Two jobs, both pure C++ (no AppKit, so app/document_tests.cpp proves them on
// Windows as well as on macOS):
//
//   1. working out WHICH task start dates the user changed, by comparing the
//      sheet's RecordSet with the plan as it was loaded;
//   2. writing those dates back through the .mpp writer, into a NEW file.
//
// Wave 15 adds a third: the two non-table save operations the shell can carry on
// the same document -- a `214/CTable` view-state edit (core/viewstate) and a
// custom-field formula edit in `114/Props` (core/props).  See the block at the
// bottom of this file.
//
// Wave 17 adds the structural sibling of that: `Document::save_structural` runs
// the Insert/Delete/field/link cascade (app/edit_model.h) with those same two
// operations inside the SAME container write, after the field-value writers.
//
// WHERE THE WRITE GOES -- AND WHERE IT MUST NOT
//   The source file is never modified.  Cmd-S in the shell asks the user for a
//   target path (NSSavePanel); this helper writes `target_path` and refuses
//   target_path == source_path.  Two reasons: the app is a viewer of files it did
//   not create (the repository's corpus/ is read-only client data), and a
//   failed write must not be able to destroy the only copy.
//
// WHAT PERSISTS, AND WHAT DOES NOT (the honest version; see also app/README.md)
//   Only task START dates are written.  They are written as a Start No Earlier
//   Than constraint (PjConstraint 4 at row +64, date at +66), which is the one
//   date store MS Project was measured to honour on open
//   (memory/writer.md, W29's `pin_task_start`).  Consequences the user will see:
//      * Project recalculates on open, so the task's Start follows the
//        constraint and the rest of the schedule may move with it;
//      * the edit is a *start pin*, not a manual schedule: it does not set
//        Manual=True and it does not preserve a manual task's own date words;
//      * every other edit the sheet supports (Name, Finish, Duration, percent,
//        undo of anything else) is view-only in memory and is NOT written by
//        this helper.
//
// TWO IMPLEMENTATIONS, ONE CALL SITE
//   Stream W37 is adding `core/mppwrite/save.h` with
//   `mppwrite::save_date_edits(CfbDocument&, std::vector<DateEdit> const&,
//   std::string const&, std::string_view)`.  app/CMakeLists.txt detects that
//   header and defines APP_MPPWRITE_SAVE_HEADER; when it is 1 the body below is
//   a single call to it, otherwise the same result is built from
//   `mppwrite::pin_task_start` + `mppwrite::CfbDocument::save`.  Nothing outside
//   this file knows which one ran; SaveOutcome::api says which one did.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "layout_model.h"
#include "record_set.h"

// Forward-declared so this header stays free of the writer's headers; only
// app/document.cpp includes "cfb_writer.h" and the stream modules.
namespace mppwrite {
class CfbDocument;
}

namespace app {

// The pending-edit model and its save result (app/edit_model.h, which includes
// THIS header).  Only `save_structural()` below names them, and a declaration
// needs no definition, so the include graph stays acyclic: document.cpp includes
// edit_model.h, never the other way round.
class StructuralEdits;
struct StructuralSaveResult;

// One changed task start.  `uid` is the task's UniqueID (identity, not a display
// field); `start` may be kDateNa, meaning the sheet's cell was cleared -- there
// is no date to write, so the save reports it as a skipped row.
struct StartEdit {
    std::uint32_t uid = 0;
    date32 start = kDateNa;
};

// Every task whose Start in `records` differs from the same task's start in
// `loaded`.  Rows are matched by UniqueID, not by index: since wave 12 the app can
// have a pending insert (a RecordSet row with no file row) or a pending delete (a
// file row no longer displayed), and an index diff would then blame the wrong
// task.  A RecordSet row whose uid is not in `loaded` is a pending insert: its
// start is not written by the start path (the writer's add clones the row) and it
// is therefore skipped.  An empty result means "nothing to save".
std::vector<StartEdit> collect_start_edits(const core::view::RecordSet& records,
                                           const PlanInput& loaded);

// The file name the shell suggests for a save: "plan.mpp" -> "plan-edited.mpp"
// (the extension is replaced, not appended; an empty stem falls back to
// "project-edited.mpp" so the suggested name is never empty).
std::string default_save_name(std::string_view source_path);

struct SaveOutcome {
    bool ok = false;
    std::size_t requested = 0;    // edits handed in
    std::size_t rows_written = 0; // tasks whose start was written
    // Problems THIS app detected: an edit with no date to write, a table
    // consistency report after the write.  A non-empty list is worth showing.
    std::vector<std::string> warnings;
    // The writer's own messages.  W37's save_date_edits returns a post-save
    // re-read receipt per edited row (and, importantly, a warning when the row is
    // stored as manually scheduled, because Project stores the SNET pin but
    // reopens a manual row at its own start).  Receipts are informational, so the
    // shell counts and logs them rather than raising an alert for every save.
    std::vector<std::string> notes;
    std::string error;  // non-empty exactly when ok == false
    std::string path;   // the target that was written
    std::string api;    // which writer path ran, for the report

    // Wave 15: the two non-table operations Document::save performs.  A plain
    // save leaves all three false.
    bool view_state_written = false;          // 214/CTable was re-encoded
    bool formula_written = false;             // 114/Props was rewritten
    bool custom_field_flag_restored = false;  // 0x02401392 forced back to 1
};

// How a save treats the opened file.
struct SaveOptions {
    // False (the default, and the only value document_tests pins): writing
    // `target == source` is refused, so a viewer can never destroy the file it
    // opened.  True is for the one case where the app KNOWS the file is its own:
    // a structural save (Insert/Delete Task) writes a new file and then reloads
    // it, so the app's document becomes that file and saving again must be able
    // to update it in place.
    bool allow_source_overwrite = false;
};

// Writes the edit set through the .mpp writer to `target_path`.
//
// Failure is reported, never thrown: a bad source, a uid that is not in the
// table, a writer refusal (mppwrite::Error) or an unwritable target all come
// back as ok=false with `error` set.  Nothing is written unless the whole set
// succeeded (the fallback path saves once, at the end; the W37 path owns its own
// atomicity).  `target_path` equal to `source_path` is refused unless
// `options.allow_source_overwrite` is true (see SaveOptions).
SaveOutcome save_start_edits(const std::vector<StartEdit>& edits,
                             std::string_view source_path, std::string_view target_path,
                             const SaveOptions& options = {});

// After a successful save the loaded plan is the new baseline, otherwise the
// next Cmd-S would offer to write the same dates again.
void accept_start_edits(PlanInput& loaded, const std::vector<StartEdit>& edits);

// ===========================================================================
// Wave 15: view-state edits and custom-field formulas
// ===========================================================================
//
// WHERE THESE SIT IN THE SAVE PIPELINE
//   The shell's existing save applies, in order: link deletes, link adds, task
//   deletes, task inserts, custom-field values, then start pins -- all on the
//   same in-memory `mppwrite::CfbDocument`.  These two operations belong AFTER
//   every table writer and BEFORE the single container save, because:
//     * the formula step must see any custom-field VALUE a table writer just
//       wrote (the 0x02401392 flag is shared by values and formulas); and
//     * neither operation can invalidate a row/table index, so running last
//       keeps every earlier writer's own re-read proof valid.
//   `apply_document_edits()` is that seam: a caller that already owns a
//   CfbDocument -- the one the field/link/structural writers have already
//   updated -- applies both operations to it and then calls
//   `save_date_edits()` / `save()` exactly as before.  That is the only place
//   the shared 0x02401392 flag can see a custom-field value written earlier in
//   the same save.
//
//   A caller that has no CfbDocument loaded can use `Document` below, which opens
//   the source, applies the pending operations and saves.  `Document::save` with
//   no start-edit list performs only these two operations, and with nothing
//   pending it writes the container back unchanged.  Its overload that takes
//   start edits adds the date pins in the same write; the date streams and these
//   two stream sets are disjoint, so applying the date writer after them produces
//   the same file as applying it first.  `save()`/`save(start_edits, ...)` do NOT
//   write field, link or structural edits: `save_structural()` below is the
//   overload for those, with these two operations riding the same write.

// One `214/CTable` edit request.  `table_index` is the index into the parsed
// CTable collection (0 = the first table; the measured fixture has one, the
// Entry table).  Both operations are optional; `any()` says whether the request
// asks for a write.
struct ViewStateEdits {
    bool set_date_format = false;
    std::uint8_t date_format = 0;  // 0xff = default; 0x05 = DateFormat 5 (measured)
    bool replace_column = false;
    std::uint32_t column_index = 0;  // an EXISTING column; appending is not measured
    std::uint32_t field_id = 0;      // PjTask field id, e.g. 0x0b400033 Text1
    std::string title;               // printable ASCII, <= 51 code units
    std::uint32_t table_index = 0;

    [[nodiscard]] bool any() const noexcept { return set_date_format || replace_column; }
};

// One `114/Props` formula request.  `set` stores/replaces the formula of one
// Task custom field; `clear` removes the whole formula table (core/props has no
// per-field clear; the measured empty form is one 8-byte record).
struct CustomFormulaEdit {
    enum class Operation { none, set, clear };
    Operation operation = Operation::none;
    std::uint32_t field_id = 0;
    std::string formula;  // Project formula text, e.g. "[Duration]/240"

    [[nodiscard]] bool any() const noexcept { return operation != Operation::none; }
};

// What applying the two operations to an in-memory container did.
//
// `ok == false` means a FORMULA operation was refused: the container is
// untouched and the caller must not save (a formula the user asked for must
// never be silently dropped).  A refused VIEW-STATE edit is a warning, not a
// failure: the request is a presentation preference, the file is still valid, and
// the container's `214/` tree is left byte-identical.  `warnings` always explains
// a refusal in either case.
struct DocumentEditOutcome {
    bool ok = true;
    bool view_state_written = false;
    bool formula_written = false;
    bool custom_field_flag_restored = false;
    std::vector<std::string> warnings;
    std::string error;
};

// Applies the formula operation and then the view-state operation to
// `document`, with no file I/O.  Formula first, deliberately: a formula refusal
// (ok == false) must not leave a view-state edit behind in a document the caller
// then refuses to save.  Nothing is written to `document` unless an operation
// fully succeeds; a refused view-state request only appends a warning.
DocumentEditOutcome apply_document_edits(mppwrite::CfbDocument& document,
                                         const ViewStateEdits& view,
                                         const CustomFormulaEdit& formula);

// Holds the pending general-save operations for one source file.  Open it with
// the path the shell loaded; accumulate requests; `save()` them into a new file.
//
//   app::Document document(source_path);
//   document.request_view_state_edits(view);              // optional
//   document.apply_custom_formula(0x0b400036, "[Name]");  // optional
//   app::SaveOutcome saved = document.save(target_path);
//
// `save()` refuses target == source unless SaveOptions::allow_source_overwrite,
// writes nothing when it reports ok == false, and verifies the written container
// by re-opening it and requiring it to re-emit byte-identically.  With no
// pending operation it writes the source container back unchanged: `214/` and
// `114/Props` are byte-identical to the source.
class Document {
  public:
    explicit Document(std::string source_path);

    [[nodiscard]] const std::string& source_path() const noexcept { return source_path_; }

    // Replaces the pending view-state request (there is at most one per save).
    void request_view_state_edits(const ViewStateEdits& edits);
    void discard_view_state_edits();
    [[nodiscard]] const ViewStateEdits& view_state_edits() const noexcept {
        return view_state_edits_;
    }

    // Queues a formula for one Task custom field.  Rejects an empty formula or a
    // field id of 0 here (the writer validates the rest at save time against the
    // file's actual formula table) and leaves any previous request alone.
    [[nodiscard]] bool apply_custom_formula(std::uint32_t field_id, std::string_view formula,
                                            std::string* error = nullptr);

    // Queues "remove every stored formula".  Always succeeds; the writer refuses
    // at save time only if the stream is not a layout it understands.
    void clear_custom_formula();

    void discard_custom_formula();
    [[nodiscard]] const CustomFormulaEdit& custom_formula() const noexcept {
        return custom_formula_;
    }

    [[nodiscard]] bool has_pending_edits() const noexcept {
        return view_state_edits_.any() || custom_formula_.any();
    }

    // Applies the pending operations and writes `target_path`.
    [[nodiscard]] SaveOutcome save(std::string_view target_path,
                                   const SaveOptions& options = {}) const;

    // The start-date save PLUS the same two operations, applied to ONE
    // CfbDocument and written once.  The dates go through
    // `mppwrite::save_date_edits` exactly as `save_start_edits` does (a manual
    // row is converted and warned about the same way), on the document that
    // already holds the formula/view-state replacements.  The two operations and
    // the date writer touch disjoint streams, so this is the same file as running
    // the date pins first -- but it costs one container save instead of two.
    // Field/link/structural edits are NOT part of this overload; a save that has
    // them should call `apply_document_edits()` on its own CfbDocument instead.
    [[nodiscard]] SaveOutcome save(const std::vector<StartEdit>& start_edits,
                                   std::string_view target_path,
                                   const SaveOptions& options = {}) const;

    // =======================================================================
    // Wave 17: structural + field + view state + formula in ONE container write
    // =======================================================================
    // Runs `save_structural_edits()` (app/edit_model.h) on `source_path()` and
    // gives that cascade's `CfbDocument` -- after its field-value writers and
    // before its one final write -- the pending view-state/formula edits through
    // `apply_document_edits()`.  This is the structural sibling of the
    // `save(start_edits, ...)` overload above: one container write for the task/
    // link/field edits AND the two wave-15 operations, keeping w64.md section 5's
    // "field values first, then formula" order -- so the shared 0x02401392
    // "custom-field data in use" decision sees a value written in the SAME save.
    //
    // With nothing pending it is exactly `save_structural_edits(edits,
    // start_edits, source_path(), target_path, allow_source_overwrite)`: the same
    // writer calls, the same write routing, byte-identical output.
    //
    // `allow_source_overwrite` is the structural save's own rule (true only for a
    // file this app itself wrote).  The result carries the cascade counters plus
    // `document_edits` / `document_edits_written`; a hook refusal (a formula the
    // file cannot take) fails the whole save with no target written.
    [[nodiscard]] StructuralSaveResult save_structural(
        const StructuralEdits& edits, const std::vector<StartEdit>& start_edits,
        std::string_view target_path, bool allow_source_overwrite) const;

  private:
    std::string source_path_;
    ViewStateEdits view_state_edits_;
    CustomFormulaEdit custom_formula_;
};

}  // namespace app
