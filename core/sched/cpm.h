// core/sched/cpm.h -- the CPM engine: task graph, forward pass (early dates),
// backward pass (late dates), slack, criticality, links with lag, and the eight
// constraint types.
//
// spec/scheduling.md §3-§4 is the contract.  The parts of that contract that the
// oracle has now measured (docs/analysis/oracle/w3-o1.json, w3-o3.json,
// w3-o4.json, w3-o4b.json) are cited by their measured value in the comments
// below and are pinned by tests/sched_cpm_tests.cpp.  Everything else is marked
// [I] and, where the behaviour would be a guess, left as TODO(open: <recipe>)
// instead of being hard-coded.
//
// Units.  Durations, lags and slack are all `ticks` = TENTHS OF A MINUTE (§0.2).
// The oracle reports minutes, so a measured "1 day" is 4800 ticks and a measured
// slack of 480 is 4800 ticks.  Do not mix the two: the conversion is in the
// tests, not in the engine.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "calendar.h"
#include "date.h"
#include "duration.h"

namespace core::sched {

// PjConstraint, spec §0.4 [B] (typelib line 39; strings.tsv:6436-6446).
enum class ConstraintType : std::uint8_t {
    as_soon_as_possible = 0,      // ASAP
    as_late_as_possible = 1,      // ALAP
    must_start_on = 2,            // MSO
    must_finish_on = 3,           // MFO
    start_no_earlier_than = 4,    // SNET -- what typing a start date creates
    start_no_later_than = 5,      // SNLT
    finish_no_earlier_than = 6,   // FNET
    finish_no_later_than = 7,     // FNLT
};

// PjTaskLinkType, spec §0.4 [B] (typelib line 4394).  The numeric order is NOT
// the spoken order: 0 = FF, 1 = FS, 2 = SF, 3 = SS.  The default when a link is
// created through Task.LinkPredecessors is 1 = FS (measured: w3-o4.json
// "link_method": "Task.LinkPredecessors(pred)  [Link default = 1]").
enum class LinkType : std::uint8_t {
    finish_to_finish = 0,  // FF
    finish_to_start = 1,   // FS
    start_to_finish = 2,   // SF
    start_to_start = 3,    // SS
};

// One schedulable task.  `calendar == nullptr` means "use the project calendar"
// (spec §1.6: a task calendar is used only when explicitly set).
struct CpmTask {
    std::int32_t id = 0;
    ticks duration = 0;  // tenths of a minute; 0 = milestone; must be >= 0
    // True when `duration` is an ELAPSED duration (Project's PjFormatUnit 8 /
    // "2 edays"), so it counts wall-clock time and the calendar is not walked.
    // MEASURED (wave 11, corpus/probe/w9-elapsedlag.mpp T8): the task row's
    // DurationFormat word (task row +0xa4) is 8 for "2 edays" and 7 for every
    // working-day task in the same file; Project schedules the 28800-tick task
    // Mon 2026-10-19 08:00 -> Wed 2026-10-21 08:00 and stores 106200 ticks
    // (10620 min) of TotalSlack, which is the wall-clock LF-EF, not the working
    // time between the dates.  `false` is the normal working-time duration.
    // [I] scope: the one measured row has no links and no constraints; the
    // engine applies the elapsed rule to its duration/constraint arithmetic and
    // to its slack, and a link bound that mixes an elapsed task with a working
    // task is unmeasured.
    bool elapsed_duration = false;
    const Calendar* calendar = nullptr;
    ConstraintType constraint = ConstraintType::as_soon_as_possible;
    date32 constraint_date = kDateNa;
    date32 deadline = kDateNa;
    // A started task's actual start (spec §2.4), kDateNa when the task has not
    // started.  This is an input FACT, not a constraint: the forward pass pins
    // the early start to it (a link that wants a later date then shows as
    // negative slack) and the backward pass pins the LATE START to it, but the
    // late FINISH still comes from the successors -- the two sides are pinned
    // differently.  Measured from Project's own save corpus/probe/project_a-recalc.mpp:
    // uid 10849 (60 d, 40 % complete) stores EarlyStart = LateStart = 2026-04-07
    // 08:00 (StartSlack 0, row +186), LateFinish 2026-08-14 17:00 (row +116) and
    // TotalSlack 16320 min (row +190) = LateFinish - EarlyFinish; uid 10851
    // (40 %) the same shape with 39360 min.  Encoding a started task as MSO
    // instead (what core/plan did before this field existed) pins BOTH late
    // dates and reports TotalSlack 0 for those two rows.
    // [I]: a started task that ALSO carries a real constraint, and a 0-duration
    // started task, have no oracle row.
    date32 actual_start = kDateNa;
    // A completed task's actual finish (spec §2.4), kDateNa when not complete.
    // MEASURED on the same PROJECT_A save, uid 10851: its successor 10757 (FS+0) is
    // 100 % complete, and every stored slack word for 10851 equals the bound from
    // its OTHER successor 10870 alone (+74 28800 min, +116 2026-08-19 17:00,
    // +190 39360 min); the 10757 bound would be 2400 min / 2026-05-04 17:00.
    // So a link whose SUCCESSOR has no remaining work does not bound the
    // predecessor's late finish, and does not count towards its free slack -- a
    // completed successor has already happened and cannot be delayed.
    // [I] scope: the discriminating row is FS.  The engine applies the rule to
    // every link type (the reason is type-independent) and this is the only
    // unmeasured extension.  The alternative reading of the row -- a stale saved
    // value -- is listed as open in memory/schedcore.md.
    date32 actual_finish = kDateNa;

    // A MANUALLY SCHEDULED task (Project's `Manual`; the reader's
    // `manually_scheduled`, Fixed2Meta per-row flags byte +8 bit 0x80).
    // MEASURED (docs/engine-parity.md "First recording", the seven recorded
    // oracles): Project does NOT schedule such a row.  It keeps its stored
    // effective Start (+104) and the Finish that Start + Duration implies, its
    // own constraints do not move it (constraint-snet / constraint-mso keep the
    // dates while the *calculated* pair jumps to the constraint date), and it is
    // never reported Critical even at zero slack.  Its successors are still
    // driven from it, and its TotalSlack is still LF - EF with LF from the
    // successors or the project finish (link-fs-lag uid 1 = 0; recalculate uid 2
    // = 9600, both pinned by tests/plan_recalc_tests.cpp).
    //
    // `manual_start` is the stored Start (+104) and `manual_finish` the finish
    // the CALLER computed for it (core/plan does the calendar arithmetic; it is
    // not plain AddWork in the one measured boundary case -- see plan.h "MANUAL
    // SCHEDULING").  When set, the engine treats both as facts:
    //   * forward pass: ES = manual_start, EF = manual_finish (the row's own
    //     constraint, deadline, links and actuals do not move it);
    //   * backward pass: the row's own constraint does not pin its late dates;
    //     LF comes from the successors or the project finish, and LS is pinned
    //     to ES (the start is a user fact), so TotalSlack is LF - EF;
    //   * StartSlack is reported equal to TotalSlack (measured: all three slack
    //     words are equal on every manual row of every oracle);
    //   * Critical is always false.
    // Leaving both kDateNa is the normal auto-scheduled path: every measured
    // corpus programme (PROJECT_A/B, schedin, holiday1, w9-elapsedlag) stores
    // the manual bit clear on every row, so this field cannot move M2.
    // [I] scope: a manual task that ALSO has actuals, a different task calendar,
    // an elapsed duration or a deadline has no oracle row (core/plan refuses to
    // pin an elapsed-duration manual row and notes it).
    date32 manual_start = kDateNa;
    date32 manual_finish = kDateNa;

    // The dates a SUCCESSOR's link bound reads for this task when they must
    // differ from this task's own early dates.  MEASURED need: Project computes a
    // manual successor's CALCULATED pair (+96/+100) from its predecessor's
    // EFFECTIVE finish, not from the predecessor's own calculated finish.
    // link-fs-lag: uid 1 stores calculated finish 2026-01-09 17:00 and kept
    // finish 01-09 12:00; uid 2's calculated start is 01-12 13:00, the FS+1d
    // bound of the KEPT finish (the calculated one would give 01-13 08:00).
    // core/plan sets these on manual rows for its calculated pass only; leaving
    // them kDateNa (the whole auto path) changes nothing.
    date32 link_anchor_start = kDateNa;
    date32 link_anchor_finish = kDateNa;
};

// A predecessor -> successor link.  `lag` is in working ticks on the successor's
// calendar and may be negative (negative lag = lead, spec §4.1).
//
// Measured bound rules (docs/analysis/oracle/w3-o3.json, milestone-probe.json,
// lagff-probe.json; pinned by tests/sched_cpm_tests.cpp `O3/type*`, `MS/*`,
// `LAGFF/*`):
//   * FS/SS bounds are START bounds: lag from the aligned working instant, then
//     aligned to a working start for a task that consumes time.  A 0-duration
//     milestone keeps the bound instant instead (FS+0 at 17:00 stays 17:00,
//     milestone-probe IDs 2/7).  A start bound is PLACED, not floored at the
//     project start -- SS-1d starts Fri 2026-10-16 08:00.
//   * FF/SF bounds are FINISH bounds: anchored on the predecessor's finish or
//     start instant and PLACED exactly (FF+0 finishes Mon 17:00; SF+0 finishes
//     Mon 19 08:00, a mid-day instant, with Start Fri 16 08:00).  An FF lead is
//     applied on the raw finish instant and lands on the previous interval end
//     (FF-1d from Fri 23 17:00 -> Thu 22 17:00), and the resulting placement is
//     floored at the project start (FF-6d -> Mon 10/19 08:00-17:00).  That floor
//     is FF-only; SF is not floored.
//   * Backward, a finish-side bound is align_back(sub_lag(succ late, lag)) --
//     lag first, then align back to a finish instant.  An FF lead is clamped to
//     the successor's late finish (FF-1d T1 LF Mon 17:00, 0 slack).
struct CpmLink {
    std::int32_t predecessor = 0;
    std::int32_t successor = 0;
    LinkType type = LinkType::finish_to_start;
    ticks lag = 0;
    // Elapsed lag ignores calendars (§4.2): a plain wall-clock offset from the
    // predecessor's finish/start instant.  MEASURED (wave 9b,
    // corpus/probe/w9-elapsedlag.mpp): the stored lag is already in elapsed
    // ticks ("2 edays" = 28800 = 48 h, "3 edays" = 43200 = 72 h) and Project's
    // save carries link-row +18 (LagFormat) = 8 for them vs 7 for the matching
    // "2 days" links; FS+3 edays from Wed 17:00 starts Mon 08:00 (Sat 17:00 ->
    // next working instant) where 3 working days would start Tue 08:00.
    bool elapsed_lag = false;
};

struct CpmOptions {
    date32 project_start = kDateNa;
    // Schedule from the project start (default).  When false, the backward pass
    // is anchored on `project_finish` and the displayed dates are the late dates,
    // mirroring "schedule from finish".  The order of the two passes and this
    // display rule are [I]: no oracle fixture measured the from-finish
    // direction, so no test depends on the exact numbers.
    // TODO(open: W3-P5) schedule-from-finish fixture.
    bool schedule_from_start = true;
    date32 project_finish = kDateNa;  // required when schedule_from_start == false
    // Project.CriticalSlackLimit in DAYS (spec §3.5).  Critical is
    // `TotalSlack <= limit * minutes_per_day`, in ticks.  Measured: slack 480 min
    // is not critical at limit 0 and is critical at limit 1 and 2
    // (w3-o4b.json critical_slack_limit).
    std::int32_t critical_slack_limit_days = 0;
    // HoursPerDay/Week/DaysPerMonth are conversion constants only (§1.5); the
    // minutes-per-day used for the criticality test comes from here.
    DurationOptions duration;
};

// One task's schedule.  `start`/`finish` are the DISPLAYED dates (the early dates
// for a normal task; the late dates for ALAP and for schedule-from-finish).
//
// MEASURED (wave 9b, corpus/probe/schedin.mpp uid 2 + docs/analysis/oracle/
// schedin-probe.json): an ALAP task's displayed Start/Finish ARE its late dates.
// The fixture's ALAP row stores +96/+100 (calculated) = 2026-11-03 08:00/17:00
// and COM reports exactly those as Start/Finish, while its +104/+108
// ("effective") pair still holds the ASAP early dates 2026-10-19 08:00/17:00.
// So `early_start`/`early_finish` below are the dates that pair holds, and
// `late_start`/`late_finish` the ones the ALAP display uses.
struct CpmTaskResult {
    std::int32_t id = 0;
    date32 start = kDateNa;
    date32 finish = kDateNa;
    date32 early_start = kDateNa;
    date32 early_finish = kDateNa;
    date32 late_start = kDateNa;
    date32 late_finish = kDateNa;
    // Working ticks (§3.5).  MEASURED CONVENTION (Project's own save
    // corpus/probe/project_a-recalc.mpp, rows +186/+190 and the oracle fixtures):
    // TotalSlack is the FINISH slack, LF - EF.  StartSlack is LS - ES, but never
    // more than TotalSlack on a leaf (a hard-constraint conflict can make it
    // negative).  FinishSlack is LF - EF.  The spec's `min(LS-ES, LF-EF)` is
    // wrong for a task whose start is pinned: uid 10849 stores StartSlack 0 and
    // TotalSlack 16320 = LF - EF; summary uid 8141 stores StartSlack 0,
    // TotalSlack 6240 = LF - EF.  TotalSlack goes below LF - EF only for a
    // NEGATIVE start-side conflict (w3-o4 MSO/SNLT rows: TotalSlack -2400 while
    // LF - EF is 0).
    // MEASURED (wave 9b, schedin.mpp uid 2): an ALAP task is placed at its late
    // start, so its StartSlack and FreeSlack are 0 (row +186 and +74) while its
    // TotalSlack is still LF - EF on the unconstrained early dates (row +190 =
    // 5280 min).  [I] scope: that one row, the only ALAP row in the corpus.
    ticks start_slack = 0;
    ticks finish_slack = 0;
    ticks free_slack = 0;
    ticks total_slack = 0;
    bool critical = false;
    // True when this row's duration was scheduled as elapsed time (see
    // CpmTask::elapsed_duration).  Reporting only; the dates/slack already
    // reflect it.
    bool elapsed_duration = false;
    // The constraint type the engine actually applied.  Measured: assigning
    // ConstraintDate to an ASAP task silently changes its type to SNET(4), so
    // this can differ from CpmTask::constraint.
    ConstraintType constraint = ConstraintType::as_soon_as_possible;
};

enum class CpmError : std::uint8_t {
    none = 0,
    no_tasks,                  // empty task list
    missing_project_start,     // options.project_start is NA
    missing_project_finish,    // schedule_from_start == false and no finish anchor
    duplicate_task_id,
    unknown_task,              // a link names an id that is not in `tasks`
    self_link,                 // predecessor == successor
    negative_duration,
    constraint_date_missing,   // a date-bearing constraint with constraint_date == NA
    overflow,                  // a walk left the calendar domain (§0.1)
    cycle,                     // links form a cycle: reported, never iterated (§3.1)
    did_not_converge,          // the pass loop hit its cap (§3.8)
    invalid_option,            // a negative CriticalSlackLimit, say
};

const char* to_string(CpmError error) noexcept;

struct CpmResult {
    CpmError error = CpmError::none;
    std::string message;                 // empty when error == none
    std::vector<std::int32_t> cycle_tasks;  // ids left unordered when error == cycle
    std::vector<CpmTaskResult> tasks;    // same order as the input `tasks`
    std::vector<std::int32_t> order;     // deterministic topological order of ids
    date32 project_start = kDateNa;
    date32 project_finish = kDateNa;

    bool ok() const noexcept { return error == CpmError::none; }
};

// Measured (w3-o4b.json "asap_constraintdate_probe"): an ASAP task whose
// ConstraintDate is assigned becomes SNET, and the date then acts as a floor.
// ALAP gets the same treatment: the w3-o4.json ALAP run also came back stored as
// type 4 after a date was assigned.  [I] for ALAP until a clean re-test; ALAP
// takes no date in MS Project, so a normal ALAP task is unaffected.
ConstraintType normalise_constraint(ConstraintType type, date32 constraint_date) noexcept;

// Runs the two CPM passes.  Deterministic: the topological order is by task id,
// and a repeated call with unchanged input returns identical dates (§5.3).
//
// Errors are returned, never thrown: a cycle is `CpmError::cycle` with the
// offending ids in `cycle_tasks`, and no partial schedule is claimed.
CpmResult schedule_tasks(const std::vector<CpmTask>& tasks,
                         const std::vector<CpmLink>& links,
                         const Calendar& project_calendar,
                         const CpmOptions& options);

}  // namespace core::sched
