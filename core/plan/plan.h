// core/plan/plan.h -- the bridge from the .mpp reader to the CPM engine.
//
// WHAT THIS IS.  `mpp::load_file` gives the task table, the link table and the
// project row (core/mpp/reader.h).  `core::sched::schedule_tasks` computes early
// dates, late dates, slack and criticality from a task graph, a calendar and a
// project start (core/sched/cpm.h, spec/scheduling.md §3-§4).  This directory is
// the one place that turns the former into the latter and reports the result in
// the reader's own terms (task UniqueID), so `tools/recalc` and the tests can
// ask the question the M2 milestone asks: does an independent recalculation of a
// real programme reproduce the dates MS Project stored in the file?
//
// ---------------------------------------------------------------- CALENDAR --
// `core::cal` decodes the file's calendar table (114/TBkndCal plus the Standard
// week in 114/Props) and builds core::sched::Calendar objects.  `from_mpp` does
// NOT take a path, so it starts with a HARD-CODED standard week:
//
//     Monday .. Friday, 08:00-12:00 and 13:00-17:00  (480 working minutes/day)
//     every other day non-working, NO holidays and NO dated exceptions.
//
// and it copies every task's +178 calendar uid into PlanTask::calendar_uid.  A
// caller with the path replaces the project week and builds the task calendars:
//
//     PlanInput input = from_mpp(document);
//     core::cal::CalendarTable table = core::cal::decode_file(path);
//     input.calendar = core::cal::build_project_calendar(table);
//     attach_task_calendars(input, table);
//
// attach_task_calendars builds ONE Calendar per distinct referenced uid (spec
// §1.6: the task calendar when one is set, the project calendar otherwise), and
// recalculate() points each CPM node at its task's calendar.  Without that call
// every task uses PlanInput::calendar, which is the pre-wave-11 behaviour.
// MEASURED (wave 11, corpus/probe/holiday1.mpp): T1/T2/T3 store +178 = 3
// (SiteCal: a Wednesday exception and a 4 h Friday).  With it T2 starts Thu
// 2026-10-22 08:00 (not Wed 10-21) and T3's 1 d from Fri 10-23 08:00 ends Mon
// 10-26 12:00; on the project calendar the same rows start 10-21 and finish
// Fri 17:00.  PROJECT_A and PROJECT_B store -1 on every task row, so their M2 dates
// cannot move.  A summary is still rolled up on the project calendar (spec §1.6
// [I]); no file on disk has a summary with a task calendar.
//
// ------------------------------------------------------------- CONSTRAINTS --
// mpp::Task exposes ConstraintType (PjConstraint 0=ASAP .. 7=FNLT),
// ConstraintDate and Deadline.  They are mapped straight through:
//
//     PjConstraint n -> core::sched::ConstraintType n   (same enum, spec §0.4)
//     ConstraintDate -> PlanTask::constraint_date        (NA when not stored)
//     Deadline       -> PlanTask::deadline               (NA when not stored)
//
// Two guards matter.  A date-bearing constraint with no stored date is
// downgraded to ASAP instead of being passed on, because CpmError::
// constraint_date_missing aborts the whole schedule; the downgrade is counted
// and noted (W19 currently leaves every field default, so on today's reader the
// downgrade count equals the number of rows).  ASAP/ALAP with a stored date are
// passed through unchanged and the engine normalises them to SNET, which is what
// MS Project itself does (measured, w3-o4b.json).
//
// ----------------------------------------------------------------- ACTUALS --
// spec/scheduling.md §2.4: a task that has started keeps its actual dates and
// actual duration; a recalculation must respect them, not overwrite them.  The
// bridge models that as follows.
//
//   start:  PlanTask::actual_start pins the task's start.  A started task's
//           start is a fact, not a forecast, so it enters the CPM graph as a
//           hard pin (CpmTask::actual_start) rather than a floor: a link that
//           wants a later date shows as negative slack, exactly as Project
//           reports "task started later than its predecessor allows".  The pin
//           is START-only: the late start is the actual start (StartSlack 0) but
//           the late finish still comes from the successors.  MEASURED on
//           corpus/probe/project_a-recalc.mpp uids 10849/10851: StartSlack 0 while
//           TotalSlack is 16320/39360 = LateFinish - EarlyFinish.  (An MSO pin
//           was used before and forces late = early, which reported 0.)
//   finish: a COMPLETED task (PercentComplete 100) also pins its finish (MFO on
//           actual_finish), so its successors are driven from the date the work
//           actually finished.  It is also marked on CpmTask::actual_finish:
//           a link INTO a complete task does not bound its predecessors' late
//           dates or free slack (measured on corpus/probe/project_a-recalc.mpp uid
//           10851, whose completed successor 10757 is ignored by every stored
//           slack word; see core/sched/cpm.h).  A task that is in progress but
//           not complete has no actual finish; its finish is the forward-pass
//           forecast from the actual start with the stored Duration, because the
//           reader exposes no status date to schedule the remaining work from.
//           That forecast is an INFERENCE and is stated as such in `notes`.
//
// WHERE THE ACTUALS COME FROM.  Preferring the reader: when
// mpp::Task::actual_start / actual_finish are valid they are used verbatim.  The
// W19 decoder that fills them is not in the tree yet, so from_mpp FALLS BACK to
// the fields the reader already exposes and derives them:
//
//     PercentComplete == 0   -> not started, no actuals
//     PercentComplete  > 0   -> actual_start  = the stored Start
//     PercentComplete ==100  -> actual_finish = the stored Finish
//     actual_duration        = Duration x PercentComplete
//
// That fallback is an INFERENCE, and a well-founded one: MS Project's Start
// column on a started task shows the actual start, and the stored Finish of a
// 100 % task is its actual finish ("Completing a task sets RemainingDuration = 0
// and Finish = ActualFinish", §2.4).  It is counted in `notes` (how many were
// reader-supplied, how many derived) so a caller can tell which path ran.  The
// consequence for the M2 measurement is stated where it belongs: a start that
// comes from an actual is a copy of a stored value, so those matches are not
// independent evidence about the graph.
//
// --------------------------------------------------------------- SUMMARIES --
// MS Project recomputes every summary bottom-up after the CPM passes
// (XP_RecomputeOutline, spec §3.1/§3.7).  The bridge now does the same:
//
//   * the parent chain is built from Task::outline_number ("1.2" -> parent
//     "1"), which the reader derives from the stored parent links in Fixed2Data;
//     a task with at least one child in that chain is a summary;
//   * summaries are EXCLUDED from the CPM graph, because their Duration is the
//     working span of their children, not a duration that can be scheduled;
//   * a link that touches a summary is therefore not schedulable and is skipped
//     and counted separately (`summary_links`).  PROJECT_A has 0 such links, so this
//     changes no leaf date there; a file that does link a summary needs a
//     decision the oracle has not measured yet;
//   * `core::sched::rollup_tasks` runs after `schedule_tasks` over the full tree
//     and supplies each summary's start, finish, duration, late dates, slack and
//     critical flag.  Leaves keep their CPM dates.
//
// The summary criticality rule is MEASURED (W3-P7 settled 2026-10-22 on
// corpus/probe/project_a-recalc.mpp, Project's own save): a summary's TotalSlack is
// the rolled-up FINISH slack LF - EF, not min(LS-ES, LF-EF) and not the minimum
// child TotalSlack, and its Critical flag follows from that TotalSlack.
// Summary 8141 stores StartSlack 0 and TotalSlack 6240 with a critical child and
// Critical false -- which refutes the inherited rule; 10720 (10080), 10761
// (28320) and 10798 (start 100800, total 38400, so the start side is not the
// minimum) agree.  `PlanInput::summary_critical_mode` still exposes both
// candidates; the default is the measured winner, recompute.
//
// ---------------------------------------------------------------- MAPPING --
// * duration_ticks, lag_ticks and slack are all 6-second ticks == tenths of a
//   minute (mpp::kTicksPerDay == 4800 == core::sched::kTicksPerDay8h).  No
//   scaling is applied, deliberately: both sides already store the same unit.
// * PackedDate::raw is already the engine's date32 layout, `(day << 16) |
//   seconds/6`, with day 1 = 1984-01-01 on both sides (spec §0.1,
//   spec/mpp-records.md §3).  It is copied, not converted.  Both sides use
//   0xffffffff for "not set", so no translation is needed there either.
// * Link types 0=FF, 1=FS, 2=SF, 3=SS are the same enum on both sides
//   (spec §0.4), so `type_raw & 3` maps directly.
// * The project start is the summary row 0's stored Start (Document::project).
//   For base.mpp that word is 2026-01-04 13:00 ("Sunday afternoon"), which the
//   engine's first forward step aligns forward to Monday 08:00; for PROJECT_A it is
//   already 2026-01-01 08:00, a working instant.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "calendar.h"
#include "cpm.h"
#include "date.h"
#include "duration.h"
#include "reader.h"
#include "rollup.h"

namespace core::cal {
struct CalendarTable;
}

namespace core::plan {

// One schedulable task, in reader terms.  `uid` is the .mpp UniqueID and is the
// key every result and comparison uses.
struct PlanTask {
    std::int32_t uid = 0;
    core::sched::ticks duration = 0;  // tenths of a minute (reader +84)
    // Reader +178: task calendar uid, -1 when the task uses the project calendar.
    // attach_task_calendars() resolves it; recalculate() gives the CPM node the
    // matching Calendar.
    std::int32_t calendar_uid = -1;
    // True when the reader's DurationFormat (+0xa4) is an elapsed format (8 =
    // elapsed days).  MEASURED on w9-elapsedlag.mpp T8 ("2 edays"); the engine
    // then counts wall-clock time instead of walking the calendar.
    bool elapsed_duration = false;
    // Derived from the outline numbers: true when another task names this one as
    // its parent.  Only those are excluded from the CPM graph and rolled up.
    bool summary = false;
    // UniqueID of the parent task, 0 for a top-level task.  Built by from_mpp
    // from mpp::Task::outline_number.
    std::int32_t parent = 0;
    // Reader fields, passed straight to the engine (see CONSTRAINTS above).
    core::sched::ConstraintType constraint = core::sched::ConstraintType::as_soon_as_possible;
    core::sched::date32 constraint_date = core::sched::kDateNa;
    core::sched::date32 deadline = core::sched::kDateNa;
    // Actuals (see ACTUALS above); kDateNa means "not started" / "not finished".
    core::sched::date32 actual_start = core::sched::kDateNa;
    core::sched::date32 actual_finish = core::sched::kDateNa;
    core::sched::ticks actual_duration = 0;
    // True when the dates above came from mpp::Task's own ActualStart/ActualFinish
    // rather than the PercentComplete fallback.  Reporting only.
    bool actuals_from_reader = false;
};

// One predecessor -> successor link.  `lag` is signed working ticks (negative =
// lead); the reader stores the same unit at TBkndCons +14.
struct PlanLink {
    std::int32_t predecessor = 0;
    std::int32_t successor = 0;
    core::sched::LinkType type = core::sched::LinkType::finish_to_start;
    core::sched::ticks lag = 0;
    // True when the reader's link +18 LagFormat is an elapsed format (8 =
    // elapsed days; MEASURED wave 9b on w9-elapsedlag.mpp).  Elapsed lag is
    // wall-clock time and ignores calendars.
    bool elapsed_lag = false;
};

// One decoded task calendar, keyed by the uid a task stores at +178.  The
// vector of these in PlanInput owns the Calendar objects, so recalculate() can
// hand stable pointers to the CPM engine.
struct TaskCalendar {
    std::int32_t uid = -1;
    core::sched::Calendar calendar;
};

// Everything the engine needs, already in engine terms.
struct PlanInput {
    core::sched::date32 project_start = core::sched::kDateNa;
    std::vector<PlanTask> tasks;  // reader order (FixedData order)
    std::vector<PlanLink> links;
    // THE HARD-CODED STANDARD CALENDAR described at the top of this header.
    core::sched::Calendar calendar;
    // One entry per distinct task-calendar uid built by attach_task_calendars().
    // Empty means every task uses PlanInput::calendar.
    std::vector<TaskCalendar> task_calendars;
    // Project options the reader cannot see; MS Project's defaults are used.
    std::int32_t critical_slack_limit_days = 0;
    core::sched::DurationOptions duration;
    // W3-P7 (measured): report the recomputed summary slack/criticality.  The
    // alternative (inherit = critical if any child is critical) is refuted by
    // PROJECT_A summary 8141; it remains selectable for comparison.
    core::sched::SummaryCriticalMode summary_critical_mode =
        core::sched::SummaryCriticalMode::recompute;
    // What the mapping had to assume or skip, in order.  Always non-empty.
    std::vector<std::string> notes;
    // Links in the reader's table that named a deleted/unknown task or itself.
    std::size_t skipped_links = 0;
    // Links dropped because an endpoint is a summary (not in the CPM graph).
    std::size_t summary_links = 0;
    // Tasks with at least one child in the outline chain.
    std::size_t summaries = 0;
    // Rows whose actuals came from mpp::Task rather than the PercentComplete
    // fallback; actuals_derived is the rest of the started/finished rows.
    std::size_t actuals_from_reader = 0;
    std::size_t actuals_derived = 0;
};

// One task's schedule, keyed by UniqueID.  Dates are date32, slack is in
// working ticks (tenths of a minute) on the standard calendar.
struct PlanTaskResult {
    std::int32_t uid = 0;
    // Rolled up from the children for a summary, forwarded unchanged for a leaf.
    bool summary = false;
    std::int32_t parent = 0;
    core::sched::ticks duration = 0;
    // Copied from the reader/PlanTask: +178 and the elapsed-duration flag.
    std::int32_t calendar_uid = -1;
    bool elapsed_duration = false;
    core::sched::date32 start = core::sched::kDateNa;
    core::sched::date32 finish = core::sched::kDateNa;
    core::sched::date32 early_start = core::sched::kDateNa;
    core::sched::date32 early_finish = core::sched::kDateNa;
    core::sched::date32 late_start = core::sched::kDateNa;
    core::sched::date32 late_finish = core::sched::kDateNa;
    core::sched::ticks total_slack = 0;
    core::sched::ticks start_slack = 0;
    core::sched::ticks finish_slack = 0;
    core::sched::ticks free_slack = 0;
    bool critical = false;
    // The constraint the engine actually applied (ASAP may be migrated to SNET
    // when a date is present; see core/sched/cpm.h).
    core::sched::ConstraintType constraint = core::sched::ConstraintType::as_soon_as_possible;
    // True when this row's start / finish is an actual date (kept, not computed).
    bool start_is_actual = false;
    bool finish_is_actual = false;
    // ---- ATTRIBUTION INPUTS (W28; measurement only, no scheduling effect) --
    // True when the row carries a date-bearing constraint -- a PjConstraint type
    // that needs a date (MSO/MFO/SNET/SNLT/FNET/FNLT), with that date stored --
    // or a deadline.  A date-bearing type whose date is absent was downgraded to
    // ASAP by from_mpp and does not count.  core/plan/attribution.h uses this to
    // attribute a mismatch; the CPM engine never reads it.
    bool has_date_constraint = false;
    // True when at least one mapped, schedulable link names this row as its
    // successor.  Links with a deleted/unknown endpoint or a summary endpoint
    // are dropped before this is computed, so it describes the graph the engine
    // actually scheduled.  Attribution only; the engine never reads it.
    bool has_predecessor = false;
};

struct PlanResult {
    core::sched::CpmError error = core::sched::CpmError::none;
    std::string message;                    // engine message when error != none
    std::vector<PlanTaskResult> tasks;      // same order as PlanInput::tasks
    core::sched::date32 project_start = core::sched::kDateNa;
    core::sched::date32 project_finish = core::sched::kDateNa;
    // Kept so a caller can report the gap between "what the file linked" and
    // "what the engine could schedule".
    std::size_t skipped_links = 0;
    std::size_t summary_links = 0;
    std::size_t summaries = 0;

    bool ok() const noexcept { return error == core::sched::CpmError::none; }
};

// The default calendar from_mpp puts in PlanInput: Monday-Friday,
// 08:00-12:00 and 13:00-17:00.  Callers with the path replace it with
// core::cal::load_project_calendar; exposed so a test can assert against it.
core::sched::Calendar standard_calendar();

// Map a loaded .mpp document onto the engine: tasks (duration, outline parent,
// constraints, deadline, actuals), links, project start and the hard-coded
// standard calendar.  Never throws; a missing project start is reported by
// recalculate() as CpmError::missing_project_start.
PlanInput from_mpp(const mpp::Document& document);

// Build one core::sched::Calendar for every distinct PlanTask::calendar_uid > 0
// in `input`, using the file's decoded 114/TBkndCal table.  Never throws: a uid
// that is absent from the table or a calendar that fails to build is counted in
// the appended PlanInput::notes and the affected tasks keep the project
// calendar.  Does not touch PlanInput::calendar itself (the caller sets it).
void attach_task_calendars(PlanInput& input, core::cal::CalendarTable const& table);

// Run the CPM engine on the leaves, then roll the summaries up.  Deterministic
// and idempotent (spec §5.3); an error is returned, never thrown, and no partial
// schedule is claimed.
PlanResult recalculate(const PlanInput& input);

// "YYYY-MM-DD HH:MM" (":SS" when the tick is not a whole minute), "NA" for an
// unset date.  Display only -- all comparisons use the packed value.
std::string format_date(core::sched::date32 date);

// Slack for display: minutes with one decimal place ("-240.0").
std::string format_slack(core::sched::ticks ticks);

}  // namespace core::plan
