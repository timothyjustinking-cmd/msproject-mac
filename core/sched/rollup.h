// core/sched/rollup.h -- the §3.7 summary rollup (XP_RecomputeOutline): a
// bottom-up pass over an explicit parent/child tree, run after the CPM passes.
//
// The tree is NOT parsed from an outline: every task carries an explicit
// parent id (0 = top level).  A task with at least one child is a summary; a
// task with no children is a leaf and keeps its stored values.
//
// MEASURED against docs/analysis/oracle/w3-o6.json (W3-O6, 2026-10-22) and
// corpus/probe/project_a-recalc.mpp (the 16 PROJECT_A summaries, 2026-10-22).
// S1 (a 2 d task, 960 min) becomes the summary of C1 (2 d from Mon 2026-10-19
// 08:00) and C2 (1 d from Mon 2026-10-26 08:00).  After outlining:
//
//   S1  Start Mon 19 08:00, Finish Mon 26 17:00,
//       Duration 2880 min (= the working span, NOT the 960+480=1440 sum),
//       LateStart Fri 23 08:00, LateFinish Mon 26 17:00,
//       StartSlack 1920, FinishSlack 0, TotalSlack 0, Critical true
//   C1  960 min, LS Fri 23 08:00, LF Mon 26 17:00, slack 1920, not critical
//   C2  480 min, LS/LF Mon 26 08:00 / 17:00, slack 0, critical
//
// After OutlineOutdent S1 is a leaf again and reads its stored 960 min: a
// summary's rolled-up duration is never stored back onto the row.  The test
// pins both measured states.
//
// MEASURED (W3-P7 settled, corpus/probe/project_a-recalc.mpp): a summary's TotalSlack
// is the rolled-up FINISH slack, LF - EF, not min(LS-ES, LF-EF) and not the
// minimum child TotalSlack.  uid 8141 stores StartSlack 0, FinishSlack 6240,
// TotalSlack 6240 and Critical false while its children are critical; uid 10720
// is 0 / 10080 / 10080 / false; uid 10761 is 0 / 28320 / 28320 / false; uid
// 10798 stores StartSlack 100800 and TotalSlack 38400, i.e. the finish side even
// when the start slack is the larger.  W3-O6 (StartSlack 1920, LF-EF 0,
// TotalSlack 0) and outline1's summary (1920 / 0 / 0) agree.  So the `recompute`
// mode below is the measured rule; `inherit` (critical if any child is critical)
// is REFUTED by 8141 and is kept only for the record.
//
// Still INFERENCE: the work / actual-work sums and %complete (W3-O6 assigned
// no resources, so only 0 / 0 / 0 was observed) and the blank-summary rule (no
// fixture).
//
// Both candidates are still recorded on every summary, as §3.7 asks:
//   * critical_any_child: critical if any non-blank child is critical;
//   * critical_recomputed: the rolled-up late dates give StartSlack = LS-ES,
//     FinishSlack = LF-EF and TotalSlack = FinishSlack.  `critical` follows
//     RollupOptions::critical_mode.
//
// Dates are on the project calendar (a summary has no task calendar of its
// own, §1.6).  All slack values are working ticks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "calendar.h"
#include "cpm.h"
#include "date.h"
#include "duration.h"

namespace core::sched {

inline constexpr std::int32_t kNoParent = 0;

// One node of the explicit parent/child tree.  Ids are 1-based and unique; a
// parent id of kNoParent (0) means top level.  For a node with children the
// leaf fields are ignored (they are recomputed from the children).
struct RollupTask {
    std::int32_t id = 0;
    std::int32_t parent = kNoParent;
    date32 start = kDateNa;
    date32 finish = kDateNa;
    date32 late_start = kDateNa;
    date32 late_finish = kDateNa;
    ticks duration = 0;
    ticks work = 0;
    ticks actual_duration = 0;
    ticks actual_work = 0;
    ticks start_slack = 0;
    ticks finish_slack = 0;
    ticks free_slack = 0;
    ticks total_slack = 0;
    bool critical = false;
};

// Seeds a leaf from a CPM row plus the work/progress numbers the CPM pass does
// not carry.
RollupTask rollup_task(std::int32_t parent, const CpmTaskResult& cpm, ticks duration, ticks work,
                       ticks actual_duration = 0, ticks actual_work = 0);

enum class SummaryCriticalMode : std::uint8_t {
    inherit = 0,    // critical if any non-blank child is critical  [REFUTED, W3-P7]
    recompute = 1,  // critical from the rolled-up finish slack     [MEASURED, W3-P7]
};

const char* to_string(SummaryCriticalMode mode) noexcept;

struct RollupOptions {
    // Critical = TotalSlack <= limit_days * minutes_per_day (§3.5, measured).
    std::int32_t critical_slack_limit_days = 0;
    DurationOptions duration;
    // The measured rule (W3-P7, PROJECT_A summaries) is recompute; inherit is refuted.
    SummaryCriticalMode critical_mode = SummaryCriticalMode::recompute;
};

struct RollupTaskResult {
    std::int32_t id = 0;
    std::int32_t parent = kNoParent;
    bool summary = false;
    // A leaf is blank when either date is NA; a summary is blank when it has no
    // non-blank children.  A blank summary stays blank (§3.7).
    bool blank = false;
    date32 start = kDateNa;
    date32 finish = kDateNa;
    date32 late_start = kDateNa;
    date32 late_finish = kDateNa;
    ticks duration = 0;
    ticks work = 0;
    ticks actual_duration = 0;
    ticks actual_work = 0;
    // 0..1 fraction.  %complete = sum(actual duration) / sum(duration) over the
    // immediate children (INFERENCE; defined == false when the denominator is 0).
    double percent_complete = 0.0;
    bool percent_complete_defined = false;
    // Leaves copy their CPM slacks; summaries roll up the late dates and derive
    // start/finish slack from them (MEASURED, W3-P7).
    ticks start_slack = 0;
    ticks finish_slack = 0;
    ticks free_slack = 0;  // a summary has no successors: equals total_slack
    // total_slack follows options.critical_mode.  Both candidates are recorded:
    ticks total_slack = 0;             // selected
    ticks total_slack_inherited = 0;   // min over non-blank children (refuted)
    ticks total_slack_recomputed = 0;  // the rolled-up finish slack, LF - EF
    bool recomputed_defined = false;
    bool critical = false;             // selected
    bool critical_any_child = false;   // inherited candidate
    bool critical_recomputed = false;  // rolled-late-dates candidate
    std::size_t child_count = 0;
    std::size_t blank_child_count = 0;
};

enum class RollupError : std::uint8_t {
    none = 0,
    no_tasks,
    zero_id,
    duplicate_id,
    unknown_parent,
    self_parent,
    cycle,
    overflow,
    invalid_option,
};

const char* to_string(RollupError error) noexcept;

struct RollupResult {
    RollupError error = RollupError::none;
    std::string message;
    std::vector<RollupTaskResult> tasks;  // same order as the input
    std::vector<std::int32_t> cycle_tasks;
    bool ok() const noexcept { return error == RollupError::none; }
};

// Rolls the tree up bottom-up.  Leaves are copied; summaries take
//   start = min(child start), finish = max(child finish),
//   duration = project_calendar.work_time_between(start, finish),
//   work = sum, actual_* = sum, late dates = min / max of the children.
// Errors are returned, never thrown; a parent cycle is reported like a CPM
// cycle and no partial tree is claimed.
RollupResult rollup_tasks(const std::vector<RollupTask>& tasks, const Calendar& project_calendar,
                          const RollupOptions& options);

}  // namespace core::sched
