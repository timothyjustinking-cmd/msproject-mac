// app/project_dialogs.h -- the portable payloads behind the Project tab's
// dialogs (wave 26, G5).
//
// Each function here is a pure mapping from the engine's data to the exact
// values a dialog shows, plus the pending edit an OK queues.  The Qt shell only
// builds controls, fills them from these structs and calls the edit maker; that
// is the fidelity contract (docs/ui-qt-port.md section 2.4/2.5) and it makes
// every dialog's content headlessly testable on Windows.
//
// WHAT IS FUNCTIONAL, AND WHAT IS NOT
//   * Project Information's PRIORITY is functional: the file stores the project
//     row's priority in one FixedData word (mppwrite::TaskTable::set_priority),
//     and the edit is saved through the shared StructuralEdits cascade.
//   * Start/Finish/Calendar are DISPLAYED from the file (no writer exists for a
//     project-level date or the default calendar), Status date and Schedule
//     from have no decoded field at all, and Statistics has no engine.  They are
//     laid out and disabled with the exact decision-9 tooltip -- never faked.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "edit_model.h"  // StructuralEdit
#include "layout_model.h"
#include "sheet.h"

namespace app {

// ---------------------------------------------------------------------------
// Project Information (Project > Properties > Project Information)
// ---------------------------------------------------------------------------

// The capture's fields, filled from the file:
//   Start date     -- PlanInput::project_start
//   Finish date    -- PlanInput::project_finish (read-only in Project)
//   Current date   -- the shell's today
//   Status date    -- no decoded field (shown NA, disabled)
//   Schedule from  -- no decoded field; Project's new-project default text
//   Calendar       -- the project default calendar name from core/cal
//   Priority       -- PlanInput::project_priority, editable and saveable
struct ProjectInformation {
    std::string title;          // the file's project title (runtime value)
    std::string start_text;
    std::string finish_text;
    std::string current_text;
    std::string status_text = "NA";
    // The raw day numbers behind the three rendered dates, for a date widget
    // that wants a QDate rather than the rendered string (kDateNa = no value).
    date32 start_day = kDateNa;
    date32 finish_day = kDateNa;
    date32 current_day = kDateNa;
    std::string schedule_from = "Project Start Date";
    std::string calendar;
    std::vector<std::string> calendars;
    int priority = 500;
    bool has_start = false;
    bool has_finish = false;
    bool has_calendar = false;
};

// `format` renders the dates the way the sheet does (the user's date style).
// `calendars` is the file's base-calendar name list and `default_calendar` its
// project default (both from core/cal; empty when the file has no readable
// table).  `today` is the shell's current date, never read from a clock here.
ProjectInformation project_information_from(const PlanInput& plan,
                                            std::vector<std::string> calendars,
                                            std::string default_calendar, date32 today,
                                            const core::sheet::SheetFormat& format);

// The pending edit Project Information's OK queues for a new priority.
StructuralEdit make_project_priority_edit(int priority);

// ---------------------------------------------------------------------------
// The file's base-calendar names (Project Information's Calendar field and
// Change Working Time's picker)
// ---------------------------------------------------------------------------

// The names core/cal decodes from the file's TBkndCal table, plus the project
// default calendar name.  A file whose calendar table does not decode is
// reported with `decoded == false` and empty lists -- the caller shows a blank,
// disabled Calendar control rather than inventing a name.  Never throws.
struct CalendarNames {
    std::vector<std::string> names;  // base calendars, table order
    std::string default_name;
    bool decoded = false;
};

CalendarNames read_calendar_names(const std::string& mpp_path);

// Project's priority domain (Project > Project Information > Priority).
inline constexpr int kMinPriority = 0;
inline constexpr int kMaxPriority = 1000;

// True when the priority control's value may be accepted.
bool valid_project_priority(int priority) noexcept;

// True when the plan stores an actual value on any row (a non-zero Percent
// Complete).  This is the port's evidence for Project's "This project contains
// tasks with actual values" prompt on Move Project; the file has no separate
// actual-start/finish decode, so this predicate is deliberately narrow and
// documented rather than guessed.
bool project_has_actual_values(const PlanInput& plan) noexcept;

}  // namespace app
