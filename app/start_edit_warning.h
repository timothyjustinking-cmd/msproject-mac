// app/start_edit_warning.h -- the warning a Start edit has to show BEFORE it is
// saved, because Project may not reopen the task at the date the user typed.
//
// THE MEASURED PROBLEM
//   docs/audit-handoff.md §4.2: Project stores a Start No Earlier Than pin but,
//   when the task is auto-scheduled with a predecessor, reopens the row at
//   max(predecessor finish, pin).  So an edit earlier than the predecessor's
//   finish silently does not take effect.  docs/analysis/oracle/w14-oracle.md §5
//   adds the second half: a manual start move removes the explicit Start/Finish
//   VarMeta blocks and carries the dates in FixedData only -- there is no "manual
//   pin" field to write.  app/document.cpp's writer therefore keeps writing the
//   exact SNET constraint (that part is correct and must not change); the app's
//   job is to WARN the user that the edit may not be what Project reopens.
//
// WHAT THIS FILE IS
//   warning_for_start_edit() is a pure function over app::PlanInput: no AppKit,
//   no writer, no file.  It answers one question -- "what will happen to this
//   Start edit?" -- with a small enum and the clamped date, and the shell shows
//   the answer once per save (app/main.mm) and in the status line.
//
//   The four outcomes (the brief's list):
//     none                          the edit will take effect as typed;
//     clamped_to_predecessor        a finish-to-start predecessor finishes
//                                   later, so Project reopens at that finish;
//     manual_task_will_be_rescheduled
//                                   the task is manually scheduled; the save
//                                   writes an auto-scheduled SNET pin instead;
//     no_effect                     nothing to change or nothing to change it
//                                   with (unknown uid, empty date, summary row,
//                                   already at that date).
//
// PRECEDENCE
//   manual wins over clamped: the save converts the row to auto-scheduled, so the
//   manual warning describes the primary consequence; a clamp on top of it is
//   still visible because the clamped date is only computed for an auto task.
//   (The tests pin that order.)
//
// LINK TYPES
//   Only finish-to-start predecessors are treated as a start barrier.  That is
//   the relation the audit's clamp sentence is about ("max(predecessor finish,
//   pin)").  Start-to-start/start-to-finish/start-to-start relations move a start
//   by different rules the oracle has not measured here, so this function does
//   not guess about them.  Lag is included: a +1 day FS link's barrier is the
//   predecessor finish plus one day, in the file's 6-second ticks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "document.h"
#include "layout_model.h"
#include "sheet.h"

namespace app {

enum class StartEditWarningKind : std::uint8_t {
    none = 0,
    clamped_to_predecessor,
    manual_task_will_be_rescheduled,
    no_effect,
};

struct StartEditWarning {
    StartEditWarningKind kind = StartEditWarningKind::none;
    std::uint32_t task_uid = 0;
    date32 requested_start = kDateNa;
    // The date Project will reopen the task at: the clamped date, the requested
    // date for a manual task, or kDateNa for no_effect.
    date32 effective_start = kDateNa;
    // The predecessor whose finish (plus lag) clamps the edit; 0 otherwise.
    std::uint32_t predecessor_uid = 0;
    // Why, for no_effect ("" for the other kinds).
    std::string detail;
};

// What a Start edit on `task_uid` will do, given the plan as loaded (tasks and
// finish-to-start links).  `new_start` is the date32 the sheet holds after the
// edit; kDateNa means the cell was cleared.
StartEditWarning warning_for_start_edit(const PlanInput& plan, std::uint32_t task_uid,
                                        date32 new_start);

// A one-line human-readable message for `warning` (empty for kind == none),
// with dates in the project's chosen date format.
std::string start_edit_warning_text(const StartEditWarning& warning,
                                    const core::sheet::SheetFormat& format);

// Every non-none warning across the starts a save is about to write.
struct StartEditWarningReport {
    std::size_t warnings = 0;
    std::size_t clamped = 0;
    std::size_t manual = 0;
    std::size_t no_effect = 0;
    // One "- <message>" line per warning, newline-separated; "" when none.
    std::string text;
};

StartEditWarningReport warn_start_edits(const std::vector<StartEdit>& edits,
                                        const PlanInput& plan,
                                        const core::sheet::SheetFormat& format);

}  // namespace app
