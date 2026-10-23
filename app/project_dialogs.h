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
#include <string_view>
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

// ---------------------------------------------------------------------------
// Change Working Time (Project > Properties > Change Working Time)
// ---------------------------------------------------------------------------
//
// The model is a READ-ONLY view of the file's calendars: this port has no
// calendar writer, so the dialog's Details/Delete/Options controls are laid out
// and disabled (decision 9).  What is real is the data: the base-calendar
// picker, the month grid's working/non-working days (core::cal's decoded week,
// exceptions and dated work weeks), the selected day's working intervals and the
// exception/work-week tables.
//
// NAMES.  core/cal deliberately decodes calendar NAMES (it needs them for
// lookup) but never exception or work-week names: those are not decoded at all,
// because in a real file they are free text that can carry client data (cal.h's
// client-data rule).  The table rows therefore carry no name text -- only the
// index and the dates -- and no tool or test here can print one.
struct WorkingTimeRow {
    int index = 0;  // 1-based, the capture's first column
    std::string start_text;
    std::string finish_text;
    int start_day = 0;
    int finish_day = 0;
};

struct WorkingTimeDay {
    int day = 0;
    bool working = false;
    bool exception = false;            // a dated exception covers this day
    bool nondefault_work_week = false; // a dated work week covers this day
};

struct WorkingTimeModel {
    bool decoded = false;
    std::vector<std::string> calendars;  // base calendars, file order
    std::string selected_calendar;
    bool selected_is_base = true;
    std::string based_on_text;
    std::string selected_day_heading;
    std::string selected_day_hours;  // one interval per line, "Nonworking" when none
    int year = 0;
    int month = 0;  // 1..12
    int first_weekday_monday0 = 0;  // the 1st's weekday, Monday = 0
    std::vector<WorkingTimeDay> days;  // 1..month length, in order
    std::vector<WorkingTimeRow> exceptions;
    std::vector<WorkingTimeRow> work_weeks;
};

// Decodes `mpp_path`, selects `calendar_name` (empty = the project default) and
// builds the month grid at `year`/`month` with `selected_day` highlighted.  False
// with `error` when the file's calendar table cannot be decoded; `out` then has
// decoded == false.  Never throws.
bool load_working_time(const std::string& mpp_path, std::string_view calendar_name,
                       int year, int month, int selected_day,
                       const core::sheet::SheetFormat& format, WorkingTimeModel& out,
                       std::string* error = nullptr);

// The month grid's weekday header, Project's Monday-first order (the capture's
// "M T W Th F S S").
const std::vector<std::string>& working_time_weekday_header();

// ---------------------------------------------------------------------------
// Custom Fields (Project > Properties > Custom Fields) -- the field-definition
// dialog, not the per-task value editor (that moved to Task Information's
// Custom Fields tab in W26-5.3)
// ---------------------------------------------------------------------------
//
// This port decodes the SIX field families whose storage W48 measured (Text,
// Flag, Number, Duration, Cost, Date) and the formula table in `114/Props`.
// The list is every field the registry names, in the capture's family order
// (Duration, Cost, Date, Flag, Number, Text); Project's Start/Finish custom
// fields are not in this port's field table and are not invented.
struct CustomFieldDefinition {
    std::uint32_t field_id = 0;
    std::string name;       // "Text1", "Flag20", ...
    std::string kind_name;  // "Text", "Flag", "Number", "Duration", "Cost", "Date"
    bool storage_measured = false;
    std::string formula;  // the file's stored formula text, empty when none
};

std::vector<CustomFieldDefinition> custom_field_definitions();

// Copies the formulas the file's `114/Props` stores onto the matching
// definitions.  A field with no stored formula keeps an empty string; a blob
// that does not parse leaves every formula empty (the values are still shown).
void attach_custom_field_formulas(std::vector<CustomFieldDefinition>& fields,
                                  const std::vector<std::uint8_t>& props_bytes);

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
