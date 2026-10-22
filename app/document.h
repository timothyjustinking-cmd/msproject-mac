// app/document.h -- what the shell does to a document besides drawing it.
//
// Two jobs, both pure C++ (no AppKit, so app/document_tests.cpp proves them on
// Windows as well as on macOS):
//
//   1. working out WHICH task start dates the user changed, by comparing the
//      sheet's RecordSet with the plan as it was loaded;
//   2. writing those dates back through the .mpp writer, into a NEW file.
//
// WHERE THE WRITE GOES -- AND WHERE IT MUST NOT
//   The source file is never modified.  Cmd-S in the shell asks the user for a
//   target path (NSSavePanel); this helper writes `target_path` and refuses
//   target_path == source_path.  Two reasons: the app is a viewer of files it
//   did not create (the repository's corpus/ is read-only client data), and a
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

namespace app {

// One changed task start.  `uid` is the task's UniqueID (identity, not a display
// field); `start` may be kDateNa, meaning the sheet's cell was cleared -- there
// is no date to write, so the save reports it as a skipped row.
struct StartEdit {
    std::uint32_t uid = 0;
    date32 start = kDateNa;
};

// Every task whose Start in `records` differs from the same task's start in
// `loaded` (row order is the RecordSet's, which is the loaded order).  Rows that
// only exist on one side are ignored.  An empty result means "nothing to save".
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
};

// Writes the edit set through the .mpp writer to `target_path`.
//
// Failure is reported, never thrown: a bad source, a uid that is not in the
// table, a writer refusal (mppwrite::Error) or an unwritable target all come
// back as ok=false with `error` set.  Nothing is written unless the whole set
// succeeded (the fallback path saves once, at the end; the W37 path owns its own
// atomicity).  `target_path` equal to `source_path` is refused.
SaveOutcome save_start_edits(const std::vector<StartEdit>& edits,
                             std::string_view source_path, std::string_view target_path);

// After a successful save the loaded plan is the new baseline, otherwise the
// next Cmd-S would offer to write the same dates again.
void accept_start_edits(PlanInput& loaded, const std::vector<StartEdit>& edits);

}  // namespace app
