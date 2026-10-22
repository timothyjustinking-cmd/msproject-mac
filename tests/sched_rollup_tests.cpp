// tests/sched_rollup_tests.cpp -- the summary rollup (§3.7).
//
// ORACLE rows come from docs/analysis/oracle/w3-o6.json (W3-O6, measured
// 2026-10-22): S1 becomes the summary of C1 (2 d from Mon 2026-10-19 08:00)
// and C2 (1 d from Mon 2026-10-26 08:00).  The measured summary duration is
// 2880 min, the working SPAN, not the 960 + 480 sum; its late dates and all
// three slack fields are pinned below.  The second oracle state (children
// outdented) proves a summary's rolled-up duration is not stored back onto the
// row, so the test rolls the same ids up twice: once as a tree, once flat.
//
// INFERENCE tags mark what no oracle row measured here:
//   * work / actual work sums and %complete (W3-O6 assigned no resources);
//   * the all-blank summary and the blank-child date rule;
//   * the summary criticality/slack candidates (W3-P7).  Both candidates are
//     recorded, as §3.7 asks: `critical_any_child` and `critical_recomputed`.
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "calendar.h"
#include "cpm.h"
#include "rollup.h"
#include "sched_test_support.h"

namespace sched = core::sched;

namespace {

constexpr std::int32_t kMon1910 = 15633;  // Mon 2026-10-19
constexpr std::int32_t kTue2010 = 15634;  // Tue 2026-10-20
constexpr std::int32_t kWed2110 = 15635;  // Wed 2026-10-21
constexpr std::int32_t kFri2310 = 15637;  // Fri 2026-10-23
constexpr std::int32_t kMon2610 = 15640;  // Mon 2026-10-26
constexpr std::int32_t kFri3010 = 15644;  // Fri 2026-10-30

constexpr sched::date32 at(std::int32_t day, std::int32_t hour, std::int32_t minute) {
    return sched::pack(day, (hour * 60 + minute) * sched::kTenthsPerMinute);
}

constexpr sched::ticks mins(std::int64_t minutes) { return minutes * 10; }

sched::Calendar standard_calendar() {
    const sched::DayPattern day = sched::day_pattern({{8 * 60, 12 * 60}, {13 * 60, 17 * 60}});
    return sched::Calendar(sched::make_week({{1, day}, {2, day}, {3, day}, {4, day}, {5, day}}));
}

sched::CpmTask cpm_task(std::int32_t id, sched::ticks duration,
                        sched::ConstraintType constraint, sched::date32 constraint_date) {
    sched::CpmTask task;
    task.id = id;
    task.duration = duration;
    task.constraint = constraint;
    task.constraint_date = constraint_date;
    return task;
}

sched::RollupTask leaf(std::int32_t id, std::int32_t parent, sched::date32 start,
                       sched::date32 finish, sched::date32 late_start, sched::date32 late_finish,
                       sched::ticks duration, sched::ticks work = 0,
                       sched::ticks actual_duration = 0, sched::ticks actual_work = 0,
                       sched::ticks start_slack = 0, sched::ticks finish_slack = 0,
                       sched::ticks free_slack = 0, sched::ticks total_slack = 0,
                       bool critical = false) {
    sched::RollupTask task;
    task.id = id;
    task.parent = parent;
    task.start = start;
    task.finish = finish;
    task.late_start = late_start;
    task.late_finish = late_finish;
    task.duration = duration;
    task.work = work;
    task.actual_duration = actual_duration;
    task.actual_work = actual_work;
    task.start_slack = start_slack;
    task.finish_slack = finish_slack;
    task.free_slack = free_slack;
    task.total_slack = total_slack;
    task.critical = critical;
    return task;
}

sched::RollupTask blank_leaf(std::int32_t id, std::int32_t parent) {
    sched::RollupTask task;
    task.id = id;
    task.parent = parent;
    return task;
}

const sched::RollupTaskResult* find(const sched::RollupResult& result, std::int32_t id) {
    for (const sched::RollupTaskResult& row : result.tasks) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

const sched::CpmTaskResult* find_cpm(const sched::CpmResult& result, std::int32_t id) {
    for (const sched::CpmTaskResult& row : result.tasks) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

void check_ok(sched_test::TestContext& ctx, const sched::RollupResult& result,
              const std::string& what) {
    ctx.check(result.ok(), what + ": rollup succeeded (error=" +
                                 std::string(sched::to_string(result.error)) + " " +
                                 result.message + ")");
}

void check_error(sched_test::TestContext& ctx, const sched::RollupResult& result,
                 sched::RollupError expected, const std::string& what) {
    ctx.equal(static_cast<int>(result.error), static_cast<int>(expected),
              what + " error (message=\"" + result.message + "\")");
    ctx.check(!result.ok(), what + ": not ok()");
}

void check_leaf(sched_test::TestContext& ctx, const sched::RollupResult& result, std::int32_t id,
                sched::date32 start, sched::date32 finish, sched::date32 late_start,
                sched::date32 late_finish, sched::ticks duration, const std::string& what) {
    const sched::RollupTaskResult* row = find(result, id);
    if (row == nullptr) {
        ctx.check(false, what + ": task " + std::to_string(id) + " missing");
        return;
    }
    ctx.check(!row->summary, what + ": is a leaf");
    ctx.check(!row->blank, what + ": is not blank");
    ctx.equal_date(row->start, start, what + " start");
    ctx.equal_date(row->finish, finish, what + " finish");
    ctx.equal_date(row->late_start, late_start, what + " late start");
    ctx.equal_date(row->late_finish, late_finish, what + " late finish");
    ctx.equal(row->duration, duration, what + " duration");
}

}  // namespace

int main() {
    sched_test::TestContext ctx;
    const sched::Calendar calendar = standard_calendar();

    // The CPM input behind W3-O6: C1 2 d from Mon 19 08:00, C2 1 d from
    // Mon 26 08:00, both as SNET floors.  Running the measured CPM engine
    // gives the late dates and slacks the rollup consumes.
    const std::vector<sched::CpmTask> cpm_tasks{
        cpm_task(2, mins(960), sched::ConstraintType::start_no_earlier_than, at(kMon1910, 8, 0)),
        cpm_task(3, mins(480), sched::ConstraintType::start_no_earlier_than, at(kMon2610, 8, 0)),
    };
    sched::CpmOptions cpm_options;
    cpm_options.project_start = sched::pack(kMon1910, 0);
    const sched::CpmResult cpm = sched::schedule_tasks(cpm_tasks, {}, calendar, cpm_options);
    ctx.check(cpm.ok(), "W3-O6: the CPM input schedules");
    const sched::CpmTaskResult* cpm_c1 = find_cpm(cpm, 2);
    const sched::CpmTaskResult* cpm_c2 = find_cpm(cpm, 3);
    ctx.check(cpm_c1 != nullptr && cpm_c2 != nullptr, "W3-O6: both CPM rows present");

    if (cpm_c1 != nullptr && cpm_c2 != nullptr) {
        // ORACLE W3-O6: C1 slack 1920, C2 slack 0.
        ctx.equal(cpm_c1->total_slack, mins(1920), "ORACLE W3-O6: C1 total slack 1920 min");
        ctx.equal(cpm_c1->late_start, at(kFri2310, 8, 0), "ORACLE W3-O6: C1 late start Fri 23 08:00");
        ctx.equal(cpm_c2->total_slack, sched::ticks{0}, "ORACLE W3-O6: C2 total slack 0");
        ctx.equal(cpm_c2->critical, true, "ORACLE W3-O6: C2 critical");

        // --- W3-O6: the measured summary --------------------------------
        std::vector<sched::RollupTask> tree;
        sched::RollupTask summary;  // its stored pre-outline leaf data is ignored
        summary.id = 1;
        summary.parent = sched::kNoParent;
        summary.start = at(kMon1910, 8, 0);
        summary.finish = at(kTue2010, 17, 0);
        summary.duration = mins(960);
        tree.push_back(summary);
        tree.push_back(sched::rollup_task(1, *cpm_c1, mins(960), 0));
        tree.push_back(sched::rollup_task(1, *cpm_c2, mins(480), 0));

        const sched::RollupResult measured = sched::rollup_tasks(tree, calendar, {});
        check_ok(ctx, measured, "W3-O6");
        const sched::RollupTaskResult* s1 = find(measured, 1);
        if (s1 != nullptr) {
            ctx.check(s1->summary, "W3-O6: S1 is a summary");
            ctx.check(!s1->blank, "W3-O6: S1 is not blank");
            ctx.equal(s1->child_count, std::size_t{2}, "W3-O6: S1 child count");
            ctx.equal(s1->blank_child_count, std::size_t{0}, "W3-O6: S1 blank child count");
            ctx.equal_date(s1->start, at(kMon1910, 8, 0), "ORACLE W3-O6: S1 start");
            ctx.equal_date(s1->finish, at(kMon2610, 17, 0), "ORACLE W3-O6: S1 finish");
            ctx.equal(s1->duration, mins(2880), "ORACLE W3-O6: S1 duration 2880 min (working span)");
            ctx.check(s1->duration != mins(960) + mins(480),
                      "ORACLE W3-O6: S1 duration is NOT the sum of child durations");
            ctx.equal(s1->work, sched::ticks{0}, "ORACLE W3-O6: S1 work 0");
            ctx.equal_date(s1->late_start, at(kFri2310, 8, 0), "ORACLE W3-O6: S1 late start");
            ctx.equal_date(s1->late_finish, at(kMon2610, 17, 0), "ORACLE W3-O6: S1 late finish");
            ctx.equal(s1->start_slack, mins(1920), "ORACLE W3-O6: S1 start slack 1920 min");
            ctx.equal(s1->finish_slack, sched::ticks{0}, "ORACLE W3-O6: S1 finish slack 0");
            ctx.equal(s1->free_slack, sched::ticks{0}, "ORACLE W3-O6: S1 free slack 0");
            ctx.equal(s1->total_slack, sched::ticks{0}, "ORACLE W3-O6: S1 total slack 0");
            ctx.equal(s1->total_slack_inherited, sched::ticks{0},
                      "ORACLE W3-O6: S1 inherited slack 0");
            ctx.equal(s1->total_slack_recomputed, sched::ticks{0},
                      "ORACLE W3-O6: S1 recomputed slack 0");
            ctx.equal(s1->critical, true, "ORACLE W3-O6: S1 critical");
            ctx.equal(s1->critical_any_child, true, "ORACLE W3-O6: S1 any child critical");
            ctx.equal(s1->critical_recomputed, true, "ORACLE W3-O6: S1 recomputed critical");
            ctx.equal(s1->percent_complete_defined, true, "W3-O6: S1 %complete defined");
            ctx.check(std::abs(s1->percent_complete) < 1e-12, "ORACLE W3-O6: S1 %complete 0");
        }
        check_leaf(ctx, measured, 2, at(kMon1910, 8, 0), at(kTue2010, 17, 0), at(kFri2310, 8, 0),
                   at(kMon2610, 17, 0), mins(960), "ORACLE W3-O6 C1");
        const sched::RollupTaskResult* c1 = find(measured, 2);
        if (c1 != nullptr) {
            ctx.equal(c1->start_slack, mins(1920), "ORACLE W3-O6: C1 start slack");
            ctx.equal(c1->finish_slack, mins(1920), "ORACLE W3-O6: C1 finish slack");
            ctx.equal(c1->free_slack, mins(1920), "ORACLE W3-O6: C1 free slack");
            ctx.equal(c1->total_slack, mins(1920), "ORACLE W3-O6: C1 total slack");
            ctx.equal(c1->critical, false, "ORACLE W3-O6: C1 not critical");
        }
        check_leaf(ctx, measured, 3, at(kMon2610, 8, 0), at(kMon2610, 17, 0), at(kMon2610, 8, 0),
                   at(kMon2610, 17, 0), mins(480), "ORACLE W3-O6 C2");
        const sched::RollupTaskResult* c2 = find(measured, 3);
        if (c2 != nullptr) {
            ctx.equal(c2->start_slack, sched::ticks{0}, "ORACLE W3-O6: C2 start slack");
            ctx.equal(c2->finish_slack, sched::ticks{0}, "ORACLE W3-O6: C2 finish slack");
            ctx.equal(c2->free_slack, sched::ticks{0}, "ORACLE W3-O6: C2 free slack");
            ctx.equal(c2->total_slack, sched::ticks{0}, "ORACLE W3-O6: C2 total slack");
            ctx.equal(c2->critical, true, "ORACLE W3-O6: C2 critical");
        }

        // --- W3-O6: children outdented -> S1 is a leaf again -----------
        // ORACLE: S1 reverts to its stored 960 min and its own 2 d dates; the
        // rolled-up 2880 min was never written back onto the row.
        std::vector<sched::RollupTask> flat;
        flat.push_back(leaf(1, sched::kNoParent, at(kMon1910, 8, 0), at(kTue2010, 17, 0),
                            at(kFri2310, 8, 0), at(kMon2610, 17, 0), mins(960), 0, 0, 0, mins(1920),
                            mins(1920), mins(1920), mins(1920), false));
        flat.push_back(sched::rollup_task(sched::kNoParent, *cpm_c1, mins(960), 0));
        flat.push_back(sched::rollup_task(sched::kNoParent, *cpm_c2, mins(480), 0));
        const sched::RollupResult outdented = sched::rollup_tasks(flat, calendar, {});
        check_ok(ctx, outdented, "W3-O6 outdented");
        const sched::RollupTaskResult* flat_s1 = find(outdented, 1);
        if (flat_s1 != nullptr) {
            ctx.check(!flat_s1->summary, "ORACLE W3-O6 outdented: S1 is a leaf");
            ctx.equal(flat_s1->duration, mins(960), "ORACLE W3-O6 outdented: S1 stored duration 960");
            ctx.equal_date(flat_s1->start, at(kMon1910, 8, 0), "ORACLE W3-O6 outdented: S1 start");
            ctx.equal_date(flat_s1->finish, at(kTue2010, 17, 0),
                           "ORACLE W3-O6 outdented: S1 finish");
        }
        check_leaf(ctx, outdented, 2, at(kMon1910, 8, 0), at(kTue2010, 17, 0), at(kFri2310, 8, 0),
                   at(kMon2610, 17, 0), mins(960), "W3-O6 outdented C1");
        check_leaf(ctx, outdented, 3, at(kMon2610, 8, 0), at(kMon2610, 17, 0), at(kMon2610, 8, 0),
                   at(kMon2610, 17, 0), mins(480), "W3-O6 outdented C2");
    }

    // --- work / actual work sums and %complete: INFERENCE ---------------
    {
        std::vector<sched::RollupTask> tree;
        sched::RollupTask summary;
        summary.id = 1;
        summary.parent = sched::kNoParent;
        tree.push_back(summary);
        tree.push_back(leaf(2, 1, at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0),
                            at(kMon1910, 17, 0), mins(480), mins(600), mins(300), mins(150), 0, 0,
                            0, 0, true));
        tree.push_back(leaf(3, 1, at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0),
                            at(kTue2010, 17, 0), mins(240), mins(300), 0, 0, 0, 0, 0, 0, true));
        const sched::RollupResult result = sched::rollup_tasks(tree, calendar, {});
        check_ok(ctx, result, "INFERENCE work sums");
        const sched::RollupTaskResult* s1 = find(result, 1);
        if (s1 != nullptr) {
            ctx.equal(s1->work, mins(900), "INFERENCE summary work = 600 + 300");
            ctx.equal(s1->actual_work, mins(150), "INFERENCE summary actual work = 150 + 0");
            ctx.equal(s1->actual_duration, mins(300),
                      "INFERENCE summary actual duration = 300 + 0");
            // §3.7: %complete = sum(actual duration) / sum(duration)
            // = 300 / (480 + 240), NOT actual / summary span (960).
            ctx.equal(s1->percent_complete_defined, true, "INFERENCE %complete defined");
            ctx.check(std::abs(s1->percent_complete - 300.0 / 720.0) < 1e-12,
                      "INFERENCE summary %complete = 300/720");
            ctx.equal(s1->duration, mins(960), "INFERENCE summary duration is the 2 d span");
            ctx.equal(s1->critical_any_child, true, "INFERENCE both children critical");
            ctx.equal(s1->critical_recomputed, true, "INFERENCE recomputed critical here");
            ctx.equal(s1->total_slack_inherited, sched::ticks{0},
                      "INFERENCE inherited slack min(0,0)");
        }
    }

    // --- blank summaries: INFERENCE -------------------------------------
    {
        std::vector<sched::RollupTask> tree;
        sched::RollupTask summary;
        summary.id = 1;
        summary.parent = sched::kNoParent;
        tree.push_back(summary);
        tree.push_back(blank_leaf(2, 1));
        tree.push_back(leaf(3, 1, at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0),
                            at(kMon1910, 17, 0), mins(480), mins(120), 0, 0, 0, 0, 0, 0, true));
        const sched::RollupResult result = sched::rollup_tasks(tree, calendar, {});
        check_ok(ctx, result, "INFERENCE blank child");
        const sched::RollupTaskResult* s1 = find(result, 1);
        if (s1 != nullptr) {
            ctx.check(!s1->blank, "INFERENCE blank child: summary is not blank");
            ctx.equal(s1->blank_child_count, std::size_t{1}, "INFERENCE blank child count");
            ctx.equal_date(s1->start, at(kMon1910, 8, 0),
                           "INFERENCE blank child: date comes from the non-blank child");
            ctx.equal_date(s1->finish, at(kMon1910, 17, 0),
                           "INFERENCE blank child: finish comes from the non-blank child");
            ctx.equal(s1->work, mins(120), "INFERENCE blank child: work still summed");
        }
        const sched::RollupTaskResult* blank = find(result, 2);
        if (blank != nullptr) {
            ctx.check(blank->blank, "INFERENCE blank leaf: blank");
            ctx.equal_date(blank->start, sched::kDateNa, "INFERENCE blank leaf: start NA");
        }

        // A summary whose children are all blank stays blank.
        std::vector<sched::RollupTask> all_blank;
        sched::RollupTask parent;
        parent.id = 1;
        parent.parent = sched::kNoParent;
        all_blank.push_back(parent);
        all_blank.push_back(blank_leaf(2, 1));
        all_blank.push_back(blank_leaf(3, 1));
        const sched::RollupResult blank_result = sched::rollup_tasks(all_blank, calendar, {});
        check_ok(ctx, blank_result, "INFERENCE all-blank summary");
        const sched::RollupTaskResult* b1 = find(blank_result, 1);
        if (b1 != nullptr) {
            ctx.check(b1->blank, "INFERENCE all-blank summary: stays blank");
            ctx.equal_date(b1->start, sched::kDateNa, "INFERENCE all-blank summary: start NA");
            ctx.equal_date(b1->finish, sched::kDateNa, "INFERENCE all-blank summary: finish NA");
            ctx.equal(b1->duration, sched::ticks{0}, "INFERENCE all-blank summary: duration 0");
            ctx.equal(b1->critical, false, "INFERENCE all-blank summary: not critical");
            ctx.equal(b1->percent_complete_defined, false,
                      "INFERENCE all-blank summary: %complete undefined");
        }
    }

    // --- the two summary criticality candidates: INFERENCE (W3-P7) -----
    {
        // Each child reports critical (slack 0) while the rolled-up late dates
        // leave the summary 3 working days of slack.  Inherited says critical,
        // recomputed says not critical -- that is the W3-P7 ambiguity.
        std::vector<sched::RollupTask> tree;
        sched::RollupTask summary;
        summary.id = 1;
        summary.parent = sched::kNoParent;
        tree.push_back(summary);
        tree.push_back(leaf(2, 1, at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kFri2310, 8, 0),
                            at(kFri2310, 17, 0), mins(480), 0, 0, 0, 0, 0, 0, 0, true));
        tree.push_back(leaf(3, 1, at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kFri2310, 8, 0),
                            at(kFri2310, 17, 0), mins(480), 0, 0, 0, 0, 0, 0, 0, true));

        sched::RollupOptions inherit;
        inherit.critical_mode = sched::SummaryCriticalMode::inherit;
        const sched::RollupResult inherited = sched::rollup_tasks(tree, calendar, inherit);
        check_ok(ctx, inherited, "INFERENCE critical inherit");
        const sched::RollupTaskResult* s1 = find(inherited, 1);
        if (s1 != nullptr) {
            ctx.equal(s1->total_slack_inherited, sched::ticks{0},
                      "INFERENCE inherit mode: min child slack 0");
            ctx.equal(s1->total_slack_recomputed, mins(1440),
                      "INFERENCE inherit mode: recomputed slack 1440 recorded too");
            ctx.equal(s1->critical_any_child, true, "INFERENCE inherit mode: any child critical");
            ctx.equal(s1->critical_recomputed, false,
                      "INFERENCE inherit mode: recomputed critical false recorded too");
            ctx.equal(s1->total_slack, sched::ticks{0}, "INFERENCE inherit mode: selected slack 0");
            ctx.equal(s1->critical, true, "INFERENCE inherit mode: selected critical true");
        }

        sched::RollupOptions recompute;
        recompute.critical_mode = sched::SummaryCriticalMode::recompute;
        const sched::RollupResult recomputed = sched::rollup_tasks(tree, calendar, recompute);
        check_ok(ctx, recomputed, "INFERENCE critical recompute");
        const sched::RollupTaskResult* r1 = find(recomputed, 1);
        if (r1 != nullptr) {
            ctx.equal(r1->total_slack, mins(1440), "INFERENCE recompute mode: slack 1440");
            ctx.equal(r1->critical, false, "INFERENCE recompute mode: not critical");
            ctx.equal(r1->critical_any_child, true,
                      "INFERENCE recompute mode: inherited candidate still true");
        }
    }

    // --- W3-P7 SETTLED: summary TotalSlack is the rolled-up FINISH slack ----
    // ORACLE: corpus/probe/project_a-recalc.mpp (Project's own save), summaries
    //   8141   StartSlack 0 (+186), FinishSlack 6240 (LF 10-30 - EF 10-13),
    //          TotalSlack 6240 (+190), Critical false -- while its children are
    //          critical (StartSlack/TotalSlack 0)
    //   10720  0 / 10080 / 10080 / false
    //   10761  0 / 28320 / 28320 / false
    //   10798  StartSlack 100800, FinishSlack 38400, TotalSlack 38400 (so the
    //          start side is NOT the min rule when it is the larger)
    // W3-O6 and outline1's summary (StartSlack 1920, FinishSlack 0, TotalSlack 0)
    // agree with the finish side.  So a summary's TotalSlack is LF - EF, not
    // min(LS-ES, LF-EF) and not the minimum child TotalSlack.
    {
        std::vector<sched::RollupTask> tree;
        sched::RollupTask summary;
        summary.id = 1;
        summary.parent = sched::kNoParent;
        tree.push_back(summary);
        // C2: starts early and is critical, but its late finish is much later, so
        // the summary's LateStart comes from it (StartSlack 0) and its LateFinish
        // is the summary's LateFinish (4 working days after the summary finish).
        tree.push_back(leaf(2, 1, at(kMon1910, 8, 0), at(kTue2010, 17, 0), at(kMon1910, 8, 0),
                            at(kFri3010, 17, 0), mins(960), 0, 0, 0, 0, 0, 0, 0, true));
        // C3: the latest child finish (Mon 10/26 17:00); its late start is later
        // than C2's, so the summary StartSlack stays 0.
        tree.push_back(leaf(3, 1, at(kMon1910, 8, 0), at(kMon2610, 17, 0), at(kWed2110, 8, 0),
                            at(kFri3010, 17, 0), mins(2880), 0, 0, 0, 0, 0, 0, 0, true));

        sched::RollupOptions recompute;
        recompute.critical_mode = sched::SummaryCriticalMode::recompute;
        const sched::RollupResult result = sched::rollup_tasks(tree, calendar, recompute);
        check_ok(ctx, result, "W3-P7 PROJECT_A shape");
        const sched::RollupTaskResult* s1 = find(result, 1);
        ctx.check(s1 != nullptr, "W3-P7 PROJECT_A shape: summary present");
        if (s1 != nullptr) {
            ctx.equal_date(s1->start, at(kMon1910, 8, 0), "W3-P7 PROJECT_A shape: start");
            ctx.equal_date(s1->finish, at(kMon2610, 17, 0), "W3-P7 PROJECT_A shape: finish");
            ctx.equal_date(s1->late_start, at(kMon1910, 8, 0), "W3-P7 PROJECT_A shape: late start");
            ctx.equal_date(s1->late_finish, at(kFri3010, 17, 0), "W3-P7 PROJECT_A shape: late finish");
            ctx.equal(s1->start_slack, sched::ticks{0}, "W3-P7 PROJECT_A shape: start slack 0");
            ctx.equal(s1->finish_slack, mins(1920), "W3-P7 PROJECT_A shape: finish slack 1920 min");
            ctx.equal(s1->total_slack_inherited, sched::ticks{0},
                      "W3-P7 PROJECT_A shape: inherited candidate is 0 (refuted)");
            ctx.equal(s1->total_slack_recomputed, mins(1920),
                      "W3-P7 PROJECT_A shape: recomputed total is the finish slack");
            ctx.equal(s1->total_slack, mins(1920), "W3-P7 PROJECT_A shape: selected total 1920");
            // +74 and +190 are equal on all 16 PROJECT_A summaries: a summary has no
            // successors, so its free slack equals its total slack.
            ctx.equal(s1->free_slack, mins(1920),
                      "W3-P7 PROJECT_A shape: summary free slack equals total slack");
            ctx.equal(s1->critical_any_child, true,
                      "W3-P7 PROJECT_A shape: a child is critical (inherit would say critical)");
            ctx.equal(s1->critical_recomputed, false,
                      "W3-P7 PROJECT_A shape: recomputed critical is false");
            ctx.equal(s1->critical, false, "W3-P7 PROJECT_A shape: measured critical false");
        }
        // The refuted candidate still records the old answer, so the difference
        // is visible rather than hidden.
        sched::RollupOptions inherit;
        inherit.critical_mode = sched::SummaryCriticalMode::inherit;
        const sched::RollupResult inherited = sched::rollup_tasks(tree, calendar, inherit);
        check_ok(ctx, inherited, "W3-P7 PROJECT_A shape inherit candidate");
        const sched::RollupTaskResult* i1 = find(inherited, 1);
        if (i1 != nullptr) {
            ctx.equal(i1->total_slack, sched::ticks{0},
                      "W3-P7 PROJECT_A shape: inherit selects 0 (the refuted candidate)");
            ctx.equal(i1->critical, true, "W3-P7 PROJECT_A shape: inherit selects critical");
        }
    }

    // --- nested summaries: INFERENCE ------------------------------------
    {
        std::vector<sched::RollupTask> tree;
        sched::RollupTask s1;
        s1.id = 1;
        s1.parent = sched::kNoParent;
        sched::RollupTask s2;
        s2.id = 2;
        s2.parent = 1;
        // Deliberately not in topological order: the rollup must sort it out.
        tree.push_back(leaf(3, 2, at(kMon1910, 8, 0), at(kTue2010, 17, 0), at(kMon1910, 8, 0),
                            at(kTue2010, 17, 0), mins(960), mins(100), 0, 0, 0, 0, 0, 0, true));
        tree.push_back(leaf(4, 2, at(kWed2110, 8, 0), at(kWed2110, 17, 0), at(kWed2110, 8, 0),
                            at(kWed2110, 17, 0), mins(480), mins(200), 0, 0, 0, 0, 0, 0, true));
        tree.push_back(s2);
        tree.push_back(leaf(5, 1, at(kFri2310, 8, 0), at(kFri2310, 17, 0), at(kFri2310, 8, 0),
                            at(kFri2310, 17, 0), mins(480), mins(300), 0, 0, 0, 0, 0, 0, true));
        tree.push_back(s1);
        const sched::RollupResult result = sched::rollup_tasks(tree, calendar, {});
        check_ok(ctx, result, "INFERENCE nesting");
        ctx.equal(find(result, 1) != nullptr ? find(result, 1)->duration : -1, mins(2400),
                  "INFERENCE S1 duration = Mon 08:00 -> Fri 17:00, 5 days");
        ctx.equal(find(result, 1) != nullptr ? find(result, 1)->work : -1, mins(600),
                  "INFERENCE S1 work = 100 + 200 + 300");
        ctx.equal(find(result, 1) != nullptr ? find(result, 1)->child_count : 0, std::size_t{2},
                  "INFERENCE S1 has 2 children");
        ctx.equal(find(result, 2) != nullptr ? find(result, 2)->duration : -1, mins(1440),
                  "INFERENCE S2 duration = Mon 08:00 -> Wed 17:00, 3 days");
        ctx.equal(find(result, 2) != nullptr ? find(result, 2)->work : -1, mins(300),
                  "INFERENCE S2 work = 100 + 200");
        ctx.equal(find(result, 2) != nullptr ? find(result, 2)->child_count : 0, std::size_t{2},
                  "INFERENCE S2 has 2 children");
        // Results follow input order, not the topological order.
        ctx.equal(result.tasks.at(0).id, std::int32_t{3}, "INFERENCE results keep input order");
        ctx.equal(result.tasks.at(4).id, std::int32_t{1}, "INFERENCE parent stays last");

        const sched::RollupResult again = sched::rollup_tasks(tree, calendar, {});
        check_ok(ctx, again, "INFERENCE determinism");
        bool identical = again.tasks.size() == result.tasks.size();
        for (std::size_t i = 0; identical && i < result.tasks.size(); ++i) {
            identical = result.tasks[i].id == again.tasks[i].id &&
                        result.tasks[i].start == again.tasks[i].start &&
                        result.tasks[i].finish == again.tasks[i].finish &&
                        result.tasks[i].duration == again.tasks[i].duration &&
                        result.tasks[i].work == again.tasks[i].work &&
                        result.tasks[i].critical == again.tasks[i].critical;
        }
        ctx.check(identical, "INFERENCE determinism: a second run is identical");
    }

    // --- tree errors: INFERENCE -----------------------------------------
    {
        check_error(ctx, sched::rollup_tasks({}, calendar, {}), sched::RollupError::no_tasks,
                    "empty tree");
        check_error(ctx, sched::rollup_tasks({leaf(0, 0, at(kMon1910, 8, 0), at(kMon1910, 17, 0),
                                                  sched::kDateNa, sched::kDateNa, mins(480))},
                                             calendar, {}),
                    sched::RollupError::zero_id, "id 0");

        std::vector<sched::RollupTask> duplicate{leaf(1, 0, at(kMon1910, 8, 0),
                                                      at(kMon1910, 17, 0), sched::kDateNa,
                                                      sched::kDateNa, mins(480)),
                                                 leaf(1, 0, at(kMon1910, 8, 0),
                                                      at(kMon1910, 17, 0), sched::kDateNa,
                                                      sched::kDateNa, mins(480))};
        check_error(ctx, sched::rollup_tasks(duplicate, calendar, {}),
                    sched::RollupError::duplicate_id, "duplicate id");

        std::vector<sched::RollupTask> unknown{leaf(1, 0, at(kMon1910, 8, 0), at(kMon1910, 17, 0),
                                                    sched::kDateNa, sched::kDateNa, mins(480)),
                                               leaf(2, 9, at(kMon1910, 8, 0), at(kMon1910, 17, 0),
                                                    sched::kDateNa, sched::kDateNa, mins(480))};
        check_error(ctx, sched::rollup_tasks(unknown, calendar, {}),
                    sched::RollupError::unknown_parent, "unknown parent");

        check_error(ctx, sched::rollup_tasks({leaf(1, 1, at(kMon1910, 8, 0), at(kMon1910, 17, 0),
                                                  sched::kDateNa, sched::kDateNa, mins(480))},
                                             calendar, {}),
                    sched::RollupError::self_parent, "self parent");

        std::vector<sched::RollupTask> cycle;
        sched::RollupTask a;
        a.id = 1;
        a.parent = 2;
        sched::RollupTask b;
        b.id = 2;
        b.parent = 1;
        cycle.push_back(a);
        cycle.push_back(b);
        const sched::RollupResult cycle_result = sched::rollup_tasks(cycle, calendar, {});
        check_error(ctx, cycle_result, sched::RollupError::cycle, "parent cycle");
        ctx.equal(cycle_result.cycle_tasks.size(), std::size_t{2}, "parent cycle: two ids");
        ctx.check(cycle_result.tasks.empty(), "parent cycle: no partial tree is claimed");

        sched::RollupOptions bad_option;
        bad_option.critical_slack_limit_days = -1;
        check_error(ctx, sched::rollup_tasks({leaf(1, 0, at(kMon1910, 8, 0), at(kMon1910, 17, 0),
                                                  sched::kDateNa, sched::kDateNa, mins(480))},
                                             calendar, bad_option),
                    sched::RollupError::invalid_option, "negative CriticalSlackLimit");
    }

    ctx.equal_string(sched::to_string(sched::RollupError::cycle), "parent cycle",
                     "to_string(RollupError)");
    ctx.equal_string(sched::to_string(sched::SummaryCriticalMode::recompute), "recompute",
                     "to_string(SummaryCriticalMode)");

    return ctx.finish("sched_rollup");
}
