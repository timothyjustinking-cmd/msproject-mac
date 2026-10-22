// app/smoke.h -- the self-smoke: the shell driving its own models, headlessly,
// with a report a human can read and a caller that can exit non-zero on failure.
//
// WHY THIS EXISTS
//   The only macOS proof in this project is GitHub Actions, and nobody on the
//   project has a Mac, so the old "manual Mac smoke test" could never be run.  The
//   replacement is an automated one: the app runs the real sequences it runs for a
//   user -- load a plan, filter it, open the two dialogs' models, edit a Start,
//   save, reopen and check the file -- plus, in app/main.mm, the AppKit half
//   (window, print-to-PDF, a PDF/PNG snapshot of the window).  `--smoke` runs the
//   whole thing in CI, writes artifacts a human can open on Windows, and exits
//   non-zero if any check failed.
//
// TWO HALVES, ONE REPORT
//   * This header and app/smoke.cpp are plain C++ with no AppKit: `smoke_plan()`
//     runs the portable sequences and is unit-tested on Windows (`app_smoke`,
//     app/smoke_tests.cpp), so the list of checks has a real assertion behind it
//     on the development box and not only on a runner.
//   * app/main.mm calls `smoke_plan()` and then appends its own checks (the window
//     exists, the print job wrote a PDF, the window snapshot wrote a PDF and a
//     PNG) through `smoke_begin_step()`/`smoke_add_check()`, and writes the same
//     report with `smoke_write_json()`.  One report, one exit code: the AppKit
//     checks cannot pass silently by being absent.
//
// WHAT THE PORTABLE HALF DOES **NOT** DO
//   It does not draw, it does not print and it does not write a PNG.  It does not
//   `NSAlert`, it cannot prove a real printer, Retina rendering, mouse feel,
//   multi-select or any dialog's appearance -- app/main.mm and the CI artifacts
//   are the only evidence for those, and docs/handoff/w63.md lists exactly what is
//   still unproven.
//
// THE FIXTURE IS READ-ONLY
//   `smoke_plan()` copies `tests/data/synthetic-base.mpp` into the output
//   directory as `smoke-input.mpp` and never opens the committed fixture for
//   writing.  The edited file is `smoke-edited.mpp` beside it; every written path
//   is under `SmokeOptions::out_dir`, which the caller owns (CI uploads it).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace app {

// The artifact file names, in the `--out` directory.  Exposed so app/main.mm, the
// Windows test and the CI upload step agree on one spelling; a smoke run never
// writes an artifact under another name.
inline constexpr const char* kSmokeInputFile = "smoke-input.mpp";
inline constexpr const char* kSmokeEditedFile = "smoke-edited.mpp";
inline constexpr const char* kSmokeJsonFile = "smoke.json";
inline constexpr const char* kSmokeWindowPdf = "smoke-window.pdf";
inline constexpr const char* kSmokeWindowPng = "smoke-window.png";
inline constexpr const char* kSmokePrintGanttPdf = "smoke-print-gantt.pdf";
inline constexpr const char* kSmokePrintSheetPdf = "smoke-print-sheet.pdf";
// Written by the PORTABLE half (File > Export PDF through core/render), so a
// reviewer on Windows has a real PDF of the same plan and page spec even though
// the AppKit print PDFs above can only be produced on macOS.
inline constexpr const char* kSmokeReportPdf = "smoke-report-export.pdf";

struct SmokeOptions {
    // The committed synthetic fixture (tests/data/synthetic-base.mpp).  Read
    // only: the plan is loaded from the copy this makes under `out_dir`.
    std::string fixture_path;
    // Created if missing.  Every file the smoke writes goes here.
    std::string out_dir;
    // The print job's footer date.  Supplied by the caller so a print job is
    // deterministic (page_layout.h never reads a clock) and so the Windows test
    // and the macOS artifact agree.
    std::string date_text = "2026-10-23";
};

// One assertion.  `step` names the sequence step it belongs to, so a failure says
// where in the sequence it happened ("save", "reopen", "appkit.print"); `name` is
// what was checked, and `detail` carries the observed value on a failure (and is
// usually empty on success).
struct SmokeCheck {
    std::string step;
    std::string name;
    bool ok = false;
    std::string detail;
};

// One file the run produced.  `name` is a stable short key ("input", "edited",
// "json", "print-gantt-pdf", ...); `path` is the absolute path as written.
struct SmokeArtifact {
    std::string name;
    std::string path;
};

// The whole result.  `checks_passed`/`checks_failed` are maintained by
// smoke_add_check() so a caller never has to recount them, and `steps` keeps the
// steps in the order they ran.
struct SmokeReport {
    // Copied from SmokeOptions by smoke_plan() so the JSON is self-describing.
    std::string fixture_path;
    std::string out_dir;
    std::vector<std::string> steps;
    std::vector<SmokeCheck> checks;
    std::vector<SmokeArtifact> artifacts;
    std::size_t checks_passed = 0;
    std::size_t checks_failed = 0;
    // Non-empty when the sequence could not run to the end (a missing fixture, a
    // plan that will not load, a save the writer refused).  A failed check that
    // the sequence can continue past does NOT set this.
    std::string error;

    std::size_t steps_run() const noexcept { return steps.size(); }
    // A run is good when every check passed and nothing stopped the sequence.
    // Zero checks is a failure, not a vacuous pass: a smoke that ran nothing
    // proves nothing.
    bool ok() const noexcept {
        return error.empty() && checks_failed == 0 && !checks.empty();
    }
};

// ---------------------------------------------------------------------------
// Recording (used by smoke_plan() and by app/main.mm's AppKit half)
// ---------------------------------------------------------------------------

// Registers a step.  Every check should name a step registered here.
void smoke_begin_step(SmokeReport& report, std::string step);

// Appends one check.  The step is recorded if it was not begun explicitly, so a
// caller can add a check from anywhere without bookkeeping.
void smoke_add_check(SmokeReport& report, std::string_view step, std::string_view name,
                     bool ok, std::string detail = {});

void smoke_add_artifact(SmokeReport& report, std::string name, std::string path);

// ---------------------------------------------------------------------------
// The portable sequence
// ---------------------------------------------------------------------------

// Runs the portable steps, in the order the shell runs them for a user:
//
//   1. fixture   copy tests/data/synthetic-base.mpp -> out/smoke-input.mpp
//   2. load      reader -> PlanInput -> RecordSet -> SheetModel -> GanttLayout
//                (the chart the window draws, built from the displayed rows)
//   3. filter    the filter dialog's model: fields, operators, matching rows,
//                the restricted visible row list, the read-only report copy
//   4. task_fields   the Task Fields dialog's model: the six specs, the initial
//                    text, a queued set_field edit on the display plan
//   5. predecessors  the Predecessors dialog's model: the incoming list, a
//                    validated link add queued on the same stack, its refusal
//                    rule for a pending row, undo
//   6. start_edit    a Start edited through core::sheet (begin/commit), the
//                    pending-edit diff, undo and redo
//   7. warning   warning_for_start_edit()/warn_start_edits(): the save-time
//                warning for the real edit and for a clamped linked start
//   8. save      save_start_edits(): the refusal of target == source, the write,
//                the untouched fixture and input copy
//   9. reopen    the written file read back with the reader: counts, names, the
//                SNET constraint and its date, no residual edit
//  10. print     build_print_job() for the Gantt and sheet requests: page count,
//                rows per page, header/footer, that the printer and PDF routes
//                produce identical pages, and File > Export PDF (core/render) on
//                the same PageSpec agreeing on the page count -- which also leaves
//                out/smoke-report-export.pdf for a reviewer on Windows
//
// A missing fixture or a load/save failure sets `error` and stops the sequence;
// every other failure is recorded and the sequence continues, so one report names
// all of them.  Every write is under `options.out_dir`; the fixture is never
// opened for writing.
SmokeReport smoke_plan(const SmokeOptions& options);

// The report as JSON (a stable, pretty-printed object with steps, counts, checks
// and artifacts).  Exposed so a test can pin the encoding without a second
// serializer.
std::string smoke_json(const SmokeReport& report);

// Writes `smoke.json` into `out_dir`, records it as an artifact, and returns
// false with `error` set when the file could not be written.  The artifact is
// listed in the file itself (it is added before serialising; on a write failure
// the entry is removed again).
bool smoke_write_json(SmokeReport& report, const std::string& out_dir, std::string& error);

// One line for stdout/the CI log: "smoke: 10 steps, 61 checks, 61 passed,
// 0 failed" plus the first failure's step and name.
std::string smoke_summary(const SmokeReport& report);

}  // namespace app
