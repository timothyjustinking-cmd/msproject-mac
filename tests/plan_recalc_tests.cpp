// tests/plan_recalc_tests.cpp -- the M2 claim: an independent recalculation of
// a real programme from the reader's task/link tables reproduces the dates and
// slack MS Project stored in the file.
//
// The tests:
//
// 1. base.mpp (committed as tests/data/synthetic-base.mpp, so this runs on a CI
//    runner with no corpus/): 3 tasks, 5/3/2 working days, no links, project
//    start 2026-01-04 13:00.  All three rows are stored MANUALLY SCHEDULED, so
//    the recalculation must keep their dates (2026-01-04 13:00 ->
//    01-09/01-07/01-06 12:00), report the CPM pair Project stores at +96/+100,
//    and reproduce the stored slack (0 / 9600 / 14400).  An auto-cleared control
//    in the same test proves the CPM path is unchanged.
// 2. The manual-scheduling rules on hand-built inputs, the recorded oracles
//    replayed through the plan (duration-change, constraint-snet/mso,
//    link-fs-lag), and free-vs-total slack on a known chain.
// 3. PROJECT_A (corpus-gated, SKIP when corpus/ or corpus/probe/project_a-recalc.mpp is
//    absent, and also skipped when MPP_NO_CORPUS=1): load
//    corpus/project_a.mpp, map it onto the CPM engine and compare
//    every task's start, finish, slack and critical with the values stored in
//    corpus/probe/project_a-recalc.mpp -- Project's own CalculateProject + Save As
//    output.  Every stored row is auto-scheduled, so the manual rule never
//    applies there: the bridge maps constraints, deadlines and actual dates and
//    rolls summaries up after the CPM passes; the counts are printed per field
//    and per leaf/summary, the first divergence is printed with both values,
//    and the fully matching uids are written to tests/data/project_a_recalc_golden.tsv
//    so the golden file cannot silently encode a bug.  The measured counts are
//    ALSO pinned as constants: a change in them must be a deliberate update, not
//    a silent regression.
//
// 3. schedin.mpp (corpus-gated, SKIP when absent): the one-task-per-constraint
//    fixture built for the constraint question -- ASAP, ALAP, MSO, MFO, SNET,
//    SNLT, FNET, FNLT, a 50 % complete task and a deadline task, with the COM
//    values in docs/analysis/oracle/schedin-probe.json.  The file stores
//    Project's own recalculated schedule, so comparing against it proves the
//    constraint/deadline/actual mapping without needing a second .mpp.  All ten
//    slack values and all twenty date fields match.  The ALAP row needs one
//    rule: its stored "effective" pair (+104/+108) still holds the ASAP early
//    dates while the calculated pair (+96/+100), the late pair (+112/+116) and
//    COM's own Start/Finish all hold the late placement, which is what the
//    engine reports (measured wave 9b; see the comment in test_schedin).
//
// THE STORED SLACK.  mpp::Task exposes Project's stored TotalSlack
// (total_slack_ticks, read from task-row +190) and the derived Critical flag
// (slack <= 0 at MS Project's default CriticalSlackLimit of 0, spec §3.5,
// measured in w3-o4b.json), so the comparison needs no raw-row decoding.
//
// SAFETY.  Like tests/mpp_reader_tests.cpp, this test never aborts into a modal
// dialog: main catches every exception and returns 2.

#include <cstdlib>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "cal.h"
#include "plan.h"
#include "reader.h"
#include "sched_test_support.h"

namespace {

namespace plan = core::plan;
namespace sched = core::sched;
using sched_test::TestContext;

// Recalculate with the file's own calendar.  from_mpp still returns the stock
// standard week; this is the plan bridge's consumption point for core/cal.  A
// missing/damaged calendar is reported and falls back to that standard week so
// the test still says what ran.
plan::PlanInput from_mpp_with_calendar(const mpp::Document& document,
                                       const std::string& path) {
    plan::PlanInput input = plan::from_mpp(document);
    try {
        const core::cal::CalendarTable table = core::cal::decode_file(path);
        input.calendar = core::cal::build_project_calendar(table);
        plan::attach_task_calendars(input, table);
    } catch (const std::exception& error) {
        std::cout << "  note: calendar decode failed for " << path << ": "
                  << error.what() << '\n';
    }
    return input;
}

std::string environment_value(const char* name) {
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return std::string();
    }
    const std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

bool corpus_hidden() { return !environment_value("MPP_NO_CORPUS").empty(); }

bool file_exists(const std::string& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

std::string describe(const plan::PlanResult& result) {
    return "error=" + std::string(sched::to_string(result.error)) + " " + result.message;
}

const plan::PlanTaskResult* find_result(const plan::PlanResult& result, std::int32_t uid) {
    const auto found = std::find_if(result.tasks.begin(), result.tasks.end(),
                                    [uid](const plan::PlanTaskResult& row) {
                                        return row.uid == uid;
                                    });
    return found == result.tasks.end() ? nullptr : &*found;
}

const mpp::Task* find_task(const mpp::Document& document, std::uint32_t uid) {
    const auto found =
        std::find_if(document.tasks.begin(), document.tasks.end(),
                     [uid](const mpp::Task& task) { return task.uid == uid; });
    return found == document.tasks.end() ? nullptr : &*found;
}

bool note_contains(const plan::PlanInput& input, const std::string& needle) {
    for (const std::string& note : input.notes) {
        if (note.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/* ---- 1. base.mpp: the small fixture that runs on every platform ---------- */
void test_base(TestContext& context) {
    const std::string path = PLAN_BASE_PATH;
    if (!file_exists(path)) {
        context.check(false, "committed fixture is missing: " + std::string(PLAN_BASE_PATH));
        return;
    }
    const mpp::Document document = mpp::load_file(path);
    context.equal(document.tasks.size(), std::size_t{3},
                  "base.mpp has 3 live tasks (the reader's own count)");

    const plan::PlanInput input = from_mpp_with_calendar(document, path);
    context.equal(input.tasks.size(), std::size_t{3}, "base.mpp maps 3 tasks");
    context.equal(input.links.size(), std::size_t{0}, "base.mpp maps 0 links");
    context.equal(input.summaries, std::size_t{0},
                  "base.mpp is flat: it maps 0 summaries");
    context.check(input.project_start != sched::kDateNa,
                  "base.mpp project row has a start");
    /* The assumptions the bridge makes are printed, not hidden. */
    context.check(note_contains(input, "stock Mon-Fri"),
                  "the mapping records the default standard week");
    context.check(note_contains(input, "constraints:"),
                  "the mapping reports the constraint/deadline coverage");
    context.check(note_contains(input, "actuals:"),
                  "the mapping reports where the actual dates came from");
    context.check(note_contains(input, "outline:"),
                  "the mapping reports the summary/parent chain");
    /* base.mpp has no constraint and no actual dates. */
    bool any_constraint = false;
    bool any_actual = false;
    for (const plan::PlanTask& task : input.tasks) {
        any_constraint = any_constraint ||
                         task.constraint != sched::ConstraintType::as_soon_as_possible ||
                         task.deadline != sched::kDateNa;
        any_actual = any_actual || task.actual_start != sched::kDateNa ||
                     task.actual_finish != sched::kDateNa;
    }
    context.check(!any_constraint, "base.mpp maps no constraint and no deadline");
    context.check(!any_actual, "base.mpp maps no actual dates");

    const plan::PlanResult result = plan::recalculate(input);
    context.check(result.ok(), "base.mpp recalculation succeeds (" + describe(result) + ")");
    if (!result.ok()) {
        return;
    }
    context.equal(result.tasks.size(), std::size_t{3}, "base.mpp result has 3 rows");
    context.equal(result.summaries, std::size_t{0}, "base.mpp has no summary in the result");
    /* MANUAL SCHEDULING (wave 18, docs/engine-parity.md "First recording").  All
     * three fixture rows store the manual bit, so Project keeps their dates: the
     * project finish is the latest KEPT finish (Fri 2026-01-09 12:00), not the
     * CPM latest (17:00).  The oracle records exactly this. */
    context.equal(result.manual_pinned, std::size_t{3},
                  "base.mpp pins all three manual rows in the effective pass");
    context.equal(result.manual_unpinned, std::size_t{0},
                  "base.mpp leaves no manual row on the auto path");
    context.equal_date(result.project_finish, sched::from_ymd(2026, 1, 9, 12, 0),
                       "base.mpp project finish is Fri 2026-01-09 12:00");

    /* Values MS Project stored in base.mpp (tests/data/synthetic-base.mpp is the
     * identical committed copy; see tests/data/README.md) and the values the
     * seven recorded oracles carry for this fixture.  For a manual row:
     *   display_start/finish = the stored effective pair (+104/+108), where the
     *                          finish is Start + Duration under the measured
     *                          manual rule (the Sunday 13:00 start charges
     *                          13:00-17:00);
     *   start/finish         = the CPM pair Project stores at +96/+100;
     *   slack                = LF - EF on the kept dates, all three words equal;
     *   critical             = false. */
    struct Expected {
        std::int32_t uid;
        sched::date32 kept_start;
        sched::date32 kept_finish;
        sched::date32 calculated_start;
        sched::date32 calculated_finish;
        sched::ticks total_slack;
    };
    const Expected expected[] = {
        {1, sched::from_ymd(2026, 1, 4, 13, 0), sched::from_ymd(2026, 1, 9, 12, 0),
         sched::from_ymd(2026, 1, 5, 8, 0), sched::from_ymd(2026, 1, 9, 17, 0), 0},
        {2, sched::from_ymd(2026, 1, 4, 13, 0), sched::from_ymd(2026, 1, 7, 12, 0),
         sched::from_ymd(2026, 1, 5, 8, 0), sched::from_ymd(2026, 1, 7, 17, 0), 9600},
        {3, sched::from_ymd(2026, 1, 4, 13, 0), sched::from_ymd(2026, 1, 6, 12, 0),
         sched::from_ymd(2026, 1, 5, 8, 0), sched::from_ymd(2026, 1, 6, 17, 0), 14400},
    };

    for (const Expected& row : expected) {
        const std::string label = "base uid " + std::to_string(row.uid);
        const plan::PlanTaskResult* actual = find_result(result, row.uid);
        context.check(actual != nullptr, label + " is in the recalculation");
        if (actual == nullptr) {
            continue;
        }
        context.check(actual->manually_scheduled, label + " is reported manually scheduled");
        context.equal_date(actual->display_start, row.kept_start, label + " kept start");
        context.equal_date(actual->display_finish, row.kept_finish, label + " kept finish");
        context.equal_date(actual->start, row.calculated_start,
                           label + " calculated (CPM) start");
        context.equal_date(actual->finish, row.calculated_finish,
                           label + " calculated (CPM) finish");
        context.equal_date(actual->start_anchor, row.kept_start,
                           label + " start anchor is the kept start");
        context.equal(actual->total_slack, row.total_slack, label + " total slack");
        /* MEASURED: the three slack words are one number on a manual row. */
        context.equal(actual->start_slack, actual->total_slack,
                      label + " start slack equals total slack");
        context.equal(actual->free_slack, actual->total_slack,
                      label + " free slack equals total slack");
        context.check(!actual->critical, label + " is never critical when manual");
        context.check(!actual->start_is_actual && !actual->finish_is_actual,
                      label + " has no actual dates to keep");

        /* And the same values as the file stores them (mpp::Task exposes both
         * the effective and the calculated pair). */
        const mpp::Task* stored = find_task(document, static_cast<std::uint32_t>(row.uid));
        context.check(stored != nullptr, label + " is in the stored task table");
        if (stored != nullptr) {
            context.check(stored->manually_scheduled,
                          label + " stores the manual bit (the only driver of the pin)");
            context.equal_date(actual->display_start, stored->start.raw,
                               label + " kept start equals the stored effective pair");
            context.equal_date(actual->display_finish, stored->finish.raw,
                               label + " kept finish equals the stored effective pair");
            context.equal_date(actual->start, stored->calculated_start.raw,
                               label + " start equals the stored +96");
            context.equal_date(actual->finish, stored->calculated_finish.raw,
                               label + " finish equals the stored +100");
            context.equal(actual->total_slack,
                          static_cast<sched::ticks>(stored->total_slack_ticks),
                          label + " total slack equals the stored value");
            context.equal(actual->critical, stored->critical,
                          label + " critical equals the stored flag");
        }
    }

    /* THE AUTO PATH IS UNCHANGED.  Clearing the stored bit on a COPY of the same
     * mapping must reproduce the pre-wave-18 CPM result (the values this test
     * asserted before manual scheduling existed): every row is scheduled from the
     * aligned project start, all three rows are critical-or-slack by the CPM rule
     * and the calculated pair equals the effective pair.  The bit is the only
     * driver, so this is the regression that a manual-mode change cannot silently
     * move an auto-scheduled programme (M2's 332/332 lives on that path). */
    plan::PlanInput auto_input = input;
    for (plan::PlanTask& task : auto_input.tasks) {
        task.manually_scheduled = false;
        task.stored_start = sched::kDateNa;
        task.stored_finish = sched::kDateNa;
        task.start_anchor = sched::kDateNa;
    }
    const plan::PlanResult auto_result = plan::recalculate(auto_input);
    context.check(auto_result.ok(), "the auto control recalculation succeeds");
    if (auto_result.ok()) {
        context.equal(auto_result.manual_pinned, std::size_t{0},
                      "the auto control pins no row");
        context.equal_date(auto_result.project_finish, sched::from_ymd(2026, 1, 9, 17, 0),
                           "the auto control project finish is the CPM Fri 17:00");
        struct AutoExpected {
            std::int32_t uid;
            sched::date32 start;
            sched::date32 finish;
            sched::ticks total_slack;
            bool critical;
        };
        const AutoExpected auto_expected[] = {
            {1, sched::from_ymd(2026, 1, 5, 8, 0), sched::from_ymd(2026, 1, 9, 17, 0), 0, true},
            {2, sched::from_ymd(2026, 1, 5, 8, 0), sched::from_ymd(2026, 1, 7, 17, 0), 9600, false},
            {3, sched::from_ymd(2026, 1, 5, 8, 0), sched::from_ymd(2026, 1, 6, 17, 0), 14400, false},
        };
        for (const AutoExpected& row : auto_expected) {
            const std::string label = "auto control uid " + std::to_string(row.uid);
            const plan::PlanTaskResult* actual = find_result(auto_result, row.uid);
            context.check(actual != nullptr, label + " is in the control");
            if (actual == nullptr) {
                continue;
            }
            context.equal_date(actual->start, row.start, label + " CPM start");
            context.equal_date(actual->finish, row.finish, label + " CPM finish");
            context.equal(actual->total_slack, row.total_slack, label + " CPM total slack");
            context.equal(actual->critical, row.critical, label + " CPM critical");
            context.equal_date(actual->display_start, actual->start,
                               label + " the kept pair equals the CPM pair when auto");
            context.equal_date(actual->display_finish, actual->finish,
                               label + " the kept pair equals the CPM pair when auto (finish)");
        }
    }
}

/* ---- 1b. MANUAL SCHEDULING: the rules on hand-built inputs --------------- */
/* The bit is the only driver: these inputs carry no .mpp at all, so a pass here
 * cannot be the fixture's stored dates leaking through. */
plan::PlanTask synthetic_task(std::int32_t uid, sched::ticks duration) {
    plan::PlanTask task;
    task.uid = uid;
    task.duration = duration;
    return task;
}

plan::PlanLink synthetic_link(std::int32_t predecessor, std::int32_t successor,
                              sched::LinkType type = sched::LinkType::finish_to_start,
                              sched::ticks lag = 0) {
    plan::PlanLink link;
    link.predecessor = predecessor;
    link.successor = successor;
    link.type = type;
    link.lag = lag;
    return link;
}

void test_manual_scheduling(TestContext& context) {
    /* (a) The pin, the boundary finish rule and the calculated pair.
     * MEASURED (recalculate oracle): a manual start of Sun 2026-01-04 13:00 is
     * kept, and 5/3/2 working days from it finish Fri 01-09 12:00 / Wed 01-07
     * 12:00 / Tue 01-06 12:00 -- the 13:00-17:00 tail of the Sunday counts as
     * working, so these are NOT AddWork(Monday 08:00, d) (that is the CPM pair
     * 17:00 the calculated fields carry). */
    {
        plan::PlanInput input;
        input.calendar = plan::standard_calendar();
        input.project_start = sched::from_ymd(2026, 1, 4, 13, 0);
        struct Row {
            std::int32_t uid;
            sched::ticks duration;
            sched::date32 finish;
            sched::ticks slack;
        };
        const Row rows[] = {
            {1, 24000, sched::from_ymd(2026, 1, 9, 12, 0), 0},
            {2, 14400, sched::from_ymd(2026, 1, 7, 12, 0), 9600},
            {3, 9600, sched::from_ymd(2026, 1, 6, 12, 0), 14400},
        };
        for (const Row& row : rows) {
            plan::PlanTask task = synthetic_task(row.uid, row.duration);
            task.manually_scheduled = true;
            task.stored_start = sched::from_ymd(2026, 1, 4, 13, 0);
            input.tasks.push_back(task);
        }
        const plan::PlanResult result = plan::recalculate(input);
        context.check(result.ok(), "synthetic manual input recalculates (" + describe(result) + ")");
        context.equal(result.manual_pinned, std::size_t{3}, "the three manual rows are pinned");
        context.equal_date(result.project_finish, sched::from_ymd(2026, 1, 9, 12, 0),
                           "manual rows: project finish is the latest kept finish");
        for (const Row& row : rows) {
            const std::string label = "synthetic manual uid " + std::to_string(row.uid);
            const plan::PlanTaskResult* actual = find_result(result, row.uid);
            context.check(actual != nullptr, label + " is in the result");
            if (actual == nullptr) {
                continue;
            }
            context.equal_date(actual->display_start, sched::from_ymd(2026, 1, 4, 13, 0),
                               label + " keeps its stored start (a non-working instant)");
            context.equal_date(actual->display_finish, row.finish,
                               label + " finish (manual rule)");
            context.equal_date(actual->start, sched::from_ymd(2026, 1, 5, 8, 0),
                               label + " calculated start is the aligned CPM start");
            context.equal(actual->total_slack, row.slack, label + " total slack");
            context.equal(actual->start_slack, row.slack, label + " start slack = total slack");
            context.equal(actual->free_slack, row.slack, label + " free slack = total slack");
            context.check(!actual->critical, label + " is not critical");
            context.equal_date(actual->start_anchor, actual->display_start,
                               label + " start anchor is the kept start");
        }
    }

    /* (b) A manual successor keeps its dates when its predecessor moves; the same
     * graph with the bit clear reschedules.  This is the "manual task keeps its
     * dates when a predecessor moves" regression, and its auto control. */
    {
        const auto build = [](bool manual_successor) {
            plan::PlanInput input;
            input.calendar = plan::standard_calendar();
            input.project_start = sched::from_ymd(2026, 1, 5, 8, 0);
            input.tasks.push_back(synthetic_task(1, 9600));  // 2 working days
            plan::PlanTask successor = synthetic_task(2, 4800);  // 1 working day
            if (manual_successor) {
                successor.manually_scheduled = true;
                successor.stored_start = sched::from_ymd(2026, 2, 2, 8, 0);
            }
            input.tasks.push_back(successor);
            input.links.push_back(synthetic_link(1, 2));
            return input;
        };
        plan::PlanInput manual_input = build(true);
        const plan::PlanResult short_run = plan::recalculate(manual_input);
        context.check(short_run.ok(), "manual successor: first recalculation succeeds");
        if (short_run.ok()) {
            const plan::PlanTaskResult* successor = find_result(short_run, 2);
            context.check(successor != nullptr, "manual successor is in the first result");
            if (successor != nullptr) {
                context.equal_date(successor->display_start, sched::from_ymd(2026, 2, 2, 8, 0),
                                   "manual successor ignores the link for its kept start");
                context.equal_date(successor->display_finish,
                                   sched::from_ymd(2026, 2, 2, 17, 0),
                                   "manual successor keeps its own finish");
            }
            const plan::PlanTaskResult* predecessor = find_result(short_run, 1);
            context.check(predecessor != nullptr, "predecessor is in the first result");
            if (predecessor != nullptr) {
                context.equal_date(predecessor->finish, sched::from_ymd(2026, 1, 6, 17, 0),
                                   "predecessor finishes after 2 working days");
            }
        }
        // The predecessor grows by three days.  The manual successor does not move.
        manual_input.tasks[0].duration = 24000;  // 5 working days
        const plan::PlanResult long_run = plan::recalculate(manual_input);
        context.check(long_run.ok(), "manual successor: second recalculation succeeds");
        if (long_run.ok()) {
            const plan::PlanTaskResult* predecessor = find_result(long_run, 1);
            context.check(predecessor != nullptr, "predecessor is in the second result");
            if (predecessor != nullptr) {
                context.equal_date(predecessor->finish, sched::from_ymd(2026, 1, 9, 17, 0),
                                   "the auto predecessor moved to Fri 2026-01-09 17:00");
            }
            const plan::PlanTaskResult* successor = find_result(long_run, 2);
            context.check(successor != nullptr, "manual successor is in the second result");
            if (successor != nullptr) {
                context.equal_date(successor->display_start,
                                   sched::from_ymd(2026, 2, 2, 8, 0),
                                   "manual successor still starts 2026-02-02 after the move");
                context.equal_date(successor->display_finish,
                                   sched::from_ymd(2026, 2, 2, 17, 0),
                                   "manual successor still finishes 2026-02-02 after the move");
            }
        }
        // Control: the same graph, auto-scheduled, does move with the predecessor.
        plan::PlanInput auto_input = build(false);
        const plan::PlanResult auto_short = plan::recalculate(auto_input);
        if (auto_short.ok()) {
            const plan::PlanTaskResult* successor = find_result(auto_short, 2);
            context.check(successor != nullptr, "auto control successor is in the short result");
            if (successor != nullptr) {
                context.equal_date(successor->start, sched::from_ymd(2026, 1, 7, 8, 0),
                                   "auto successor starts after the 2-day predecessor");
            }
        }
        auto_input.tasks[0].duration = 24000;
        const plan::PlanResult auto_long = plan::recalculate(auto_input);
        if (auto_long.ok()) {
            const plan::PlanTaskResult* successor = find_result(auto_long, 2);
            context.check(successor != nullptr, "auto control successor is in the long result");
            if (successor != nullptr) {
                context.equal_date(successor->start, sched::from_ymd(2026, 1, 12, 8, 0),
                                   "auto successor moved with the predecessor (M2 path)");
            }
        }
    }

    /* (c) A manual predecessor still drives an auto successor. */
    {
        plan::PlanInput input;
        input.calendar = plan::standard_calendar();
        input.project_start = sched::from_ymd(2026, 1, 12, 8, 0);
        plan::PlanTask predecessor = synthetic_task(1, 4800);
        predecessor.manually_scheduled = true;
        predecessor.stored_start = sched::from_ymd(2026, 1, 12, 8, 0);
        input.tasks.push_back(predecessor);
        input.tasks.push_back(synthetic_task(2, 4800));
        input.links.push_back(synthetic_link(1, 2));
        const plan::PlanResult result = plan::recalculate(input);
        context.check(result.ok(), "manual -> auto recalculation succeeds");
        if (result.ok()) {
            const plan::PlanTaskResult* successor = find_result(result, 2);
            context.check(successor != nullptr, "auto successor of a manual row is in the result");
            if (successor != nullptr) {
                context.equal_date(successor->start, sched::from_ymd(2026, 1, 13, 8, 0),
                                   "successors still respect a manual row's finish");
                context.equal_date(successor->finish, sched::from_ymd(2026, 1, 13, 17, 0),
                                   "auto successor finishes its working day");
            }
            const plan::PlanTaskResult* manual = find_result(result, 1);
            if (manual != nullptr) {
                context.equal_date(manual->display_finish,
                                   sched::from_ymd(2026, 1, 12, 17, 0),
                                   "the manual row keeps its finish");
                context.equal(manual->total_slack, 0,
                              "a manual row followed immediately has no slack");
            }
        }
    }

    /* (d) A manual row's own constraint is stored but does not move it; the
     * calculated pair moves instead (constraint-snet / constraint-mso oracles). */
    {
        plan::PlanInput input;
        input.calendar = plan::standard_calendar();
        input.project_start = sched::from_ymd(2026, 1, 4, 13, 0);
        plan::PlanTask task = synthetic_task(1, 9600);
        task.manually_scheduled = true;
        task.stored_start = sched::from_ymd(2026, 1, 4, 13, 0);
        task.constraint = sched::ConstraintType::start_no_earlier_than;
        task.constraint_date = sched::from_ymd(2026, 2, 2, 8, 0);
        input.tasks.push_back(task);
        const plan::PlanResult result = plan::recalculate(input);
        context.check(result.ok(), "manual row with an SNET constraint recalculates");
        if (result.ok()) {
            const plan::PlanTaskResult* actual = find_result(result, 1);
            context.check(actual != nullptr, "the constrained manual row is in the result");
            if (actual != nullptr) {
                context.equal_date(actual->display_start, sched::from_ymd(2026, 1, 4, 13, 0),
                                   "SNET does not move a manual row's start");
                context.equal_date(actual->display_finish, sched::from_ymd(2026, 1, 6, 12, 0),
                                   "SNET does not move a manual row's finish");
                context.equal_date(actual->start, sched::from_ymd(2026, 2, 2, 8, 0),
                                   "the calculated pair does take the SNET date");
                context.check(actual->constraint == sched::ConstraintType::as_soon_as_possible,
                              "the applied constraint of a manual row is ASAP");
            }
        }
    }
}

/* ---- 1c. The recorded oracles: plan-visible edits on the fixture --------- */
void test_manual_oracle(TestContext& context) {
    const std::string path = PLAN_BASE_PATH;
    if (!file_exists(path)) {
        context.check(false, "committed fixture is missing: " + std::string(PLAN_BASE_PATH));
        return;
    }
    const mpp::Document document = mpp::load_file(path);
    plan::PlanInput input = from_mpp_with_calendar(document, path);

    /* duration-change.oracle.json: task 1 Duration -> 14400 ticks (3 d).  The
     * manual finish is recomputed from the KEPT start, so the oracle's task
     * dates/slack are 01-07 12:00 / 01-07 12:00 / 01-06 12:00 with slack
     * 0 / 0 / 4800 and project finish 01-07 12:00. */
    {
        plan::PlanInput changed = input;
        for (plan::PlanTask& task : changed.tasks) {
            if (task.uid == 1) {
                task.duration = 14400;
            }
        }
        const plan::PlanResult result = plan::recalculate(changed);
        context.check(result.ok(), "duration-change oracle: recalculation succeeds");
        if (result.ok()) {
            struct Row {
                std::int32_t uid;
                sched::date32 finish;
                sched::ticks slack;
            };
            const Row rows[] = {
                {1, sched::from_ymd(2026, 1, 7, 12, 0), 0},
                {2, sched::from_ymd(2026, 1, 7, 12, 0), 0},
                {3, sched::from_ymd(2026, 1, 6, 12, 0), 4800},
            };
            for (const Row& row : rows) {
                const std::string label = "duration-change uid " + std::to_string(row.uid);
                const plan::PlanTaskResult* actual = find_result(result, row.uid);
                context.check(actual != nullptr, label + " is in the result");
                if (actual == nullptr) {
                    continue;
                }
                context.equal_date(actual->display_start, sched::from_ymd(2026, 1, 4, 13, 0),
                                   label + " kept start is unchanged");
                context.equal_date(actual->display_finish, row.finish, label + " kept finish");
                context.equal(actual->total_slack, row.slack, label + " total slack");
            }
            context.equal_date(result.project_finish, sched::from_ymd(2026, 1, 7, 12, 0),
                               "duration-change project finish");
        }
    }

    /* constraint-snet.oracle.json: SNET 2026-02-02 08:00 on uid 2.  The row keeps
     * 01-04 13:00 / 01-07 12:00 and slack 9600; the calculated pair jumps. */
    {
        plan::PlanInput constrained = input;
        for (plan::PlanTask& task : constrained.tasks) {
            if (task.uid == 2) {
                task.constraint = sched::ConstraintType::start_no_earlier_than;
                task.constraint_date = sched::from_ymd(2026, 2, 2, 8, 0);
            }
        }
        const plan::PlanResult result = plan::recalculate(constrained);
        context.check(result.ok(), "constraint-snet oracle: recalculation succeeds");
        if (result.ok()) {
            const plan::PlanTaskResult* actual = find_result(result, 2);
            context.check(actual != nullptr, "constraint-snet uid 2 is in the result");
            if (actual != nullptr) {
                context.equal_date(actual->display_start, sched::from_ymd(2026, 1, 4, 13, 0),
                                   "constraint-snet: the manual start is kept");
                context.equal_date(actual->display_finish, sched::from_ymd(2026, 1, 7, 12, 0),
                                   "constraint-snet: the manual finish is kept");
                context.equal(actual->total_slack, 9600,
                              "constraint-snet: slack is the manual LF - EF (9600)");
                context.equal_date(actual->start,
                                   sched::from_ymd(2026, 2, 2, 8, 0),
                                   "constraint-snet: the calculated start is the SNET date");
                context.equal_date(actual->finish,
                                   sched::from_ymd(2026, 2, 4, 17, 0),
                                   "constraint-snet: the calculated finish is 3 d later");
            }
            context.equal_date(result.project_finish, sched::from_ymd(2026, 1, 9, 12, 0),
                               "constraint-snet: project finish is the kept latest finish");
        }
    }

    /* constraint-mso.oracle.json: MSO 2026-01-20 08:00 on uid 3.  Same shape; the
     * oracle's stored Duration 12000 is a Project-side edit the engine does not
     * perform (it is not a scheduling rule and the scenario half does not write
     * it), so only the dates/slack are compared here. */
    {
        plan::PlanInput constrained = input;
        for (plan::PlanTask& task : constrained.tasks) {
            if (task.uid == 3) {
                task.constraint = sched::ConstraintType::must_start_on;
                task.constraint_date = sched::from_ymd(2026, 1, 20, 8, 0);
            }
        }
        const plan::PlanResult result = plan::recalculate(constrained);
        context.check(result.ok(), "constraint-mso oracle: recalculation succeeds");
        if (result.ok()) {
            const plan::PlanTaskResult* actual = find_result(result, 3);
            context.check(actual != nullptr, "constraint-mso uid 3 is in the result");
            if (actual != nullptr) {
                context.equal_date(actual->display_start, sched::from_ymd(2026, 1, 4, 13, 0),
                                   "constraint-mso: the manual start is kept");
                context.equal_date(actual->display_finish, sched::from_ymd(2026, 1, 6, 12, 0),
                                   "constraint-mso: the manual finish is kept");
                context.equal(actual->total_slack, 14400,
                              "constraint-mso: slack stays the unconstrained 14400");
                context.equal_date(actual->start,
                                   sched::from_ymd(2026, 1, 20, 8, 0),
                                   "constraint-mso: the calculated start is the MSO date");
                context.equal_date(actual->finish,
                                   sched::from_ymd(2026, 1, 21, 17, 0),
                                   "constraint-mso: calculated finish from the 2 d duration");
            }
            context.equal_date(result.project_finish, sched::from_ymd(2026, 1, 9, 12, 0),
                               "constraint-mso: project finish is the kept latest finish");
        }
    }

    /* link-fs-lag.oracle.json: link 1 -> 2 FS+4800.  Both rows are manual, so the
     * kept dates do not move in a pure recalculation (Project's KEPT dates,
     * 01-12/01-15, were written when the link was ADDED and saved; the scenario
     * half applies add_link without that intermediate pass, so the plan cannot
     * reproduce them -- plan.h "MANUAL SCHEDULING" says so).
     *
     * The CALCULATED pair is where this fixture pins the second pass.  Project
     * stores uid 2 calculated 2026-01-12 13:00 -> 01-15 12:00, the FS+1d bound of
     * uid 1's EFFECTIVE finish Fri 01-09 12:00 (the bound of its calculated
     * finish 17:00 would be 01-13 08:00).  So a manual successor's calculated
     * pair is computed from its predecessor's KEPT dates, which is what the
     * engine's CpmTask::link_anchor_start/finish encode and the plan sets for a
     * manual row in the calculated pass. */
    {
        plan::PlanInput linked = input;
        linked.links.push_back(synthetic_link(1, 2, sched::LinkType::finish_to_start, 4800));
        const plan::PlanResult result = plan::recalculate(linked);
        context.check(result.ok(), "link-fs-lag oracle: recalculation succeeds");
        if (result.ok()) {
            const plan::PlanTaskResult* actual = find_result(result, 2);
            context.check(actual != nullptr, "link-fs-lag uid 2 is in the result");
            if (actual != nullptr) {
                context.equal_date(actual->start,
                                   sched::from_ymd(2026, 1, 12, 13, 0),
                                   "link-fs-lag: calculated start matches the oracle");
                context.equal_date(actual->finish,
                                   sched::from_ymd(2026, 1, 15, 12, 0),
                                   "link-fs-lag: calculated finish matches the oracle");
                context.equal_date(actual->display_start, sched::from_ymd(2026, 1, 4, 13, 0),
                                   "link-fs-lag: a recalculation alone keeps the stored start");
            }
        }
    }
}

/* ---- 1d. Free vs total slack on a known chain (auto-scheduled) ----------- */
void test_free_vs_total_slack(TestContext& context) {
    /* Two independent chains.  Chain 1 is 2 d + 1 d, chain 2 is one 4 d task, so
     * the project finish is chain 2's.  Chain 1's first task can slip a day
     * without moving anything (total slack 4800) but its successor starts the
     * next morning, so it has no free slack: free 0 < total 4800.  MEASURED
     * convention: FreeSlack is task row +74, StartSlack +186, TotalSlack +190
     * (spec/mpp-records.md; milestone1.mpp ID 6 stores 0 / 220800 / 220800), and
     * this is the shape M2's auto-scheduled corpus never exercises because its
     * stored rows have the three words equal. */
    plan::PlanInput input;
    input.calendar = plan::standard_calendar();
    input.project_start = sched::from_ymd(2026, 1, 5, 8, 0);
    input.tasks.push_back(synthetic_task(1, 9600));  // 2 d
    input.tasks.push_back(synthetic_task(2, 4800));  // 1 d
    input.tasks.push_back(synthetic_task(3, 19200)); // 4 d, the long chain
    input.links.push_back(synthetic_link(1, 2));
    const plan::PlanResult result = plan::recalculate(input);
    context.check(result.ok(), "free/total slack chain recalculates (" + describe(result) + ")");
    if (!result.ok()) {
        return;
    }
    context.equal_date(result.project_finish, sched::from_ymd(2026, 1, 8, 17, 0),
                       "the 4-day task sets the project finish");
    const plan::PlanTaskResult* first = find_result(result, 1);
    const plan::PlanTaskResult* second = find_result(result, 2);
    const plan::PlanTaskResult* long_chain = find_result(result, 3);
    context.check(first != nullptr && second != nullptr && long_chain != nullptr,
                  "all three slack-chain rows are in the result");
    if (first != nullptr && second != nullptr && long_chain != nullptr) {
        context.equal(first->total_slack, 4800, "chain head total slack is one day");
        context.equal(first->free_slack, 0,
                      "chain head free slack is 0 (its successor starts the next morning)");
        context.equal(first->start_slack, 4800,
                      "chain head start slack equals total slack (LS - ES)");
        context.check(first->free_slack < first->total_slack,
                      "free slack is smaller than total slack on a linked chain");
        context.equal(second->total_slack, 4800, "chain tail total slack equals free slack");
        context.equal(second->free_slack, 4800, "chain tail free slack (no successors)");
        context.equal(long_chain->total_slack, 0, "the longest chain is critical");
        context.check(long_chain->critical, "the longest chain's task is critical");
        context.check(!first->critical && !second->critical,
                      "the short chain is not critical");
    }
}

/* ---- 2. PROJECT_A: the M2 measurement on a real programme --------------------- */
struct FieldStats {
    std::string name{};
    std::size_t match = 0;
    std::size_t mismatch = 0;
    bool divergence_seen = false;
    std::uint32_t first_uid = 0;
    std::string first_actual{};
    std::string first_expected{};

    void record(std::uint32_t uid, const std::string& actual, const std::string& expected) {
        ++mismatch;
        if (!divergence_seen) {
            divergence_seen = true;
            first_uid = uid;
            first_actual = actual;
            first_expected = expected;
        }
    }
};

void print_stats(const std::vector<FieldStats>& stats) {
    std::cout << "  field      match  mismatch  total\n";
    for (const FieldStats& field : stats) {
        const std::string name = field.name;
        const std::size_t pad = name.size() < 10 ? 10 - name.size() : 0;
        const std::string total = std::to_string(field.match + field.mismatch);
        std::cout << "  " << name << std::string(pad, ' ') << field.match
                  << std::string(7, ' ') << field.mismatch
                  << std::string(total.size() < 10 ? 10 - total.size() : 0, ' ') << total
                  << '\n';
    }
    for (const FieldStats& field : stats) {
        if (!field.divergence_seen) {
            continue;
        }
        std::cout << "  first divergence: " << field.name << " uid " << field.first_uid
                  << " recalculated=\"" << field.first_actual << "\" stored=\""
                  << field.first_expected << "\"\n";
    }
    std::cout << std::flush;
}

/* Counts measured on 2026-10-22 (wave 7) with the standard calendar, the
 * reader's constraint/deadline/actual fields, the §3.7 rollup, the W23
 * milestone/FF rules and the W24 link liveness.  These are deliberately exact:
 * if the engine or the mapping improves, this test fails and the number is
 * updated as part of that change instead of a silent drift.
 *
 * The three date families that were open in wave 6 are fixed, and this comment
 * keeps them as the record of what "83/83" means:
 *   (a) 0-duration milestones at 17:00 (10742/10803/10804) -- W23 measured that
 *       a link start bound is not snapped when duration == 0 (milestone-probe).
 *   (b) 10859's phantom FS+6d from 10758 -- W24 measured that a link is live
 *       only when a FixedMeta entry names its row; the real predecessor is
 *       10858 with lag 0.
 *   (c) the 10846 cascade -- the real predecessors are 10849/10877/10759; the
 *       10760 row was uncovered metadata and is no longer returned.
 * Dates are now 83/83 for start and finish.  Every remaining mismatch is slack
 * or criticality: 45 link-family fields (the stored TotalSlack/backward pass),
 * 16 summary fields (W3-P7 inherit-vs-recompute) and 5 milestone fields.  The
 * per-family attribution is printed by tools/recalc --attrib-json and recorded
 * in memory/plan.md.
 */
constexpr std::size_t kRecordedStartMatches = 83;
constexpr std::size_t kRecordedFinishMatches = 83;
constexpr std::size_t kRecordedSlackMatches = 83;
constexpr std::size_t kRecordedCriticalMatches = 83;
constexpr std::size_t kRecordedSummaries = 16;

void test_project_a(TestContext& context) {
    if (corpus_hidden()) {
        std::cout << "SKIP: PROJECT_A recalculation (MPP_NO_CORPUS is set: corpus forced absent)\n";
        return;
    }
    if (!file_exists(PLAN_PROJECT_A_PATH)) {
        std::cout << "SKIP: " << PLAN_PROJECT_A_PATH << " is not present (corpus is git-ignored)\n";
        return;
    }
    if (!file_exists(PLAN_RECALC_PATH)) {
        std::cout << "SKIP: " << PLAN_RECALC_PATH << " is not present\n";
        return;
    }

    const mpp::Document document = mpp::load_file(PLAN_PROJECT_A_PATH);
    context.equal(document.tasks.size(), std::size_t{83}, "PROJECT_A has 83 live tasks");

    const plan::PlanInput input = from_mpp_with_calendar(document, PLAN_PROJECT_A_PATH);
    context.equal(input.tasks.size(), document.tasks.size(),
                  "every live PROJECT_A task reaches the engine");
    context.equal(input.links.size(), std::size_t{88},
                  "all 88 live PROJECT_A links reach the engine");
    context.equal(input.skipped_links, std::size_t{0},
                  "no uncovered link row is returned, so no link is skipped");
    context.equal(input.summaries, kRecordedSummaries,
                  "PROJECT_A's outline chain yields 16 summaries");
    context.equal(input.summary_links, std::size_t{0},
                  "no link touches a PROJECT_A summary, so the rollup moves no leaf date");
    context.equal(input.task_calendars.size(), std::size_t{0},
                  "PROJECT_A names no task calendar (+178 is -1 on every row)");
    std::size_t project_a_elapsed = 0;
    for (const plan::PlanTask& task : input.tasks) {
        project_a_elapsed += task.elapsed_duration ? 1u : 0u;
    }
    context.equal(project_a_elapsed, std::size_t{0},
                  "PROJECT_A has no elapsed-duration task, so its M2 dates cannot move");

    const plan::PlanResult result = plan::recalculate(input);
    context.check(result.ok(), "PROJECT_A recalculation succeeds (" + describe(result) + ")");
    if (!result.ok()) {
        return;
    }
    context.equal(result.tasks.size(), document.tasks.size(),
                  "the recalculation returns one row per live task");
    context.equal(result.summaries, kRecordedSummaries,
                  "the result reports 16 summary rows");
    std::cout << "PROJECT_A recalculation: " << result.tasks.size() << " tasks, project start "
              << plan::format_date(result.project_start) << ", project finish "
              << plan::format_date(result.project_finish) << '\n';

    /* The rollup itself: each summary's dates must be the min start / max finish
     * of its children, whatever the comparison below says. */
    std::size_t summaries_checked = 0;
    for (const plan::PlanTaskResult& summary : result.tasks) {
        if (!summary.summary) {
            continue;
        }
        ++summaries_checked;
        sched::date32 min_start = sched::kDateNa;
        sched::date32 max_finish = sched::kDateNa;
        std::size_t children = 0;
        for (const plan::PlanTaskResult& child : result.tasks) {
            if (child.parent != summary.uid) {
                continue;
            }
            ++children;
            if (min_start == sched::kDateNa || child.start < min_start) {
                min_start = child.start;
            }
            if (max_finish == sched::kDateNa || child.finish > max_finish) {
                max_finish = child.finish;
            }
        }
        const std::string label = "summary uid " + std::to_string(summary.uid);
        context.check(children > 0, label + " has children in the result");
        context.equal_date(summary.start, min_start, label + " start is the earliest child start");
        context.equal_date(summary.finish, max_finish,
                           label + " finish is the latest child finish");
        context.equal(summary.start_is_actual, false, label + " is not an actual date");
    }
    context.equal(summaries_checked, kRecordedSummaries,
                  "every summary in the result was checked against its children");

    const mpp::Document stored = mpp::load_file(PLAN_RECALC_PATH);
    context.equal(stored.tasks.size(), document.tasks.size(),
                  "project_a-recalc.mpp has the same number of live tasks");

    std::vector<FieldStats> stats{{"start"}, {"finish"}, {"slack"}, {"critical"}};
    FieldStats& start_stats = stats[0];
    FieldStats& finish_stats = stats[1];
    FieldStats& slack_stats = stats[2];
    FieldStats& critical_stats = stats[3];

    std::size_t missing = 0;
    std::size_t extra = 0;
    std::size_t actual_starts = 0;
    std::size_t actual_finishes = 0;
    std::map<std::uint32_t, bool> stored_uids;

    /* The golden file: only uids whose start, finish, slack AND critical all
     * matched.  Values are the STORED ones (== the recalculated ones for those
     * uids); no task names, so the file cannot leak client data. */
    std::vector<std::string> golden_lines;
    golden_lines.emplace_back(
        "# project_a_recalc_golden.tsv -- MS Project's stored schedule for the uids that");
    golden_lines.emplace_back(
        "# core::plan recalculated exactly (start, finish, slack and critical all");
    golden_lines.emplace_back(
        "# equal). Source: corpus/probe/project_a-recalc.mpp (open PROJECT_A, CalculateProject,");
    golden_lines.emplace_back(
        "# Save As). slack_min is mpp::Task::total_slack_ticks (row +74) / 10;");
    golden_lines.emplace_back(
        "# critical is Project's derived Critical (false for a manually scheduled");
    golden_lines.emplace_back(
        "# task, otherwise slack_min <= 0 at CriticalSlackLimit 0). No names.");
    golden_lines.emplace_back("uid\tstart\tfinish\tslack_min\tcritical");
    const std::size_t golden_header_lines = golden_lines.size();

    for (const mpp::Task& task : stored.tasks) {
        stored_uids[task.uid] = true;
        const plan::PlanTaskResult* actual =
            find_result(result, static_cast<std::int32_t>(task.uid));
        if (actual == nullptr) {
            ++missing;
            continue;
        }
        const std::string stored_start = plan::format_date(task.start.raw);
        const std::string stored_finish = plan::format_date(task.finish.raw);
        if (actual->start_is_actual) {
            ++actual_starts;
        }
        if (actual->finish_is_actual) {
            ++actual_finishes;
        }

        const bool start_ok = actual->start == task.start.raw;
        if (start_ok) {
            ++start_stats.match;
        } else {
            start_stats.record(task.uid, plan::format_date(actual->start), stored_start);
        }
        const bool finish_ok = actual->finish == task.finish.raw;
        if (finish_ok) {
            ++finish_stats.match;
        } else {
            finish_stats.record(task.uid, plan::format_date(actual->finish), stored_finish);
        }

        const sched::ticks stored_ticks = static_cast<sched::ticks>(task.total_slack_ticks);
        const bool stored_critical = task.critical;
        const bool slack_ok = actual->total_slack == stored_ticks;
        if (slack_ok) {
            ++slack_stats.match;
        } else {
            slack_stats.record(task.uid, plan::format_slack(actual->total_slack),
                               plan::format_slack(stored_ticks));
        }
        const bool critical_ok = actual->critical == stored_critical;
        if (critical_ok) {
            ++critical_stats.match;
        } else {
            critical_stats.record(task.uid, actual->critical ? "yes" : "no",
                                  stored_critical ? "yes" : "no");
        }

        if (start_ok && finish_ok && slack_ok && critical_ok) {
            golden_lines.push_back(std::to_string(task.uid) + '\t' + stored_start + '\t' +
                                   stored_finish + '\t' + plan::format_slack(stored_ticks) +
                                   '\t' + (stored_critical ? "yes" : "no"));
        }
    }

    for (const plan::PlanTaskResult& row : result.tasks) {
        if (stored_uids.count(static_cast<std::uint32_t>(row.uid)) == 0) {
            ++extra;
            if (extra <= 5) {
                std::cout << "  extra uid " << row.uid
                          << " is in the recalculation but not in project_a-recalc.mpp\n";
            }
        }
    }

    std::cout << "PROJECT_A vs corpus/probe/project_a-recalc.mpp (" << stored.tasks.size()
              << " stored tasks, missing " << missing << ", extra " << extra
              << ", actual-start rows " << actual_starts << ", actual-finish rows "
              << actual_finishes << ")\n";
    print_stats(stats);

    /* Structural assertions: the comparison itself is sound. */
    context.equal(missing, std::size_t{0}, "every stored PROJECT_A uid has a recalculation");
    context.equal(extra, std::size_t{0}, "no recalculation row is absent from the file");
    context.check(start_stats.match + start_stats.mismatch == stored.tasks.size(),
                  "every stored task was compared for start");
    context.check(start_stats.match > 0,
                  "at least one start is reproduced by the independent recalculation");
    context.check(finish_stats.match > 0,
                  "at least one finish is reproduced by the independent recalculation");
    /* 33 PROJECT_A tasks are started or complete; the reader supplies their actual
     * dates, so those rows keep them by construction (§2.4).  21 have an actual
     * start and 18 an actual finish (a summary can carry one without the other). */
    context.check(actual_starts > 0 && actual_starts <= 33,
                  "the actual-start rows came from the reader's PercentComplete/actual fields");
    context.check(actual_finishes <= actual_starts,
                  "a task cannot finish without having started");

    /* The recorded measurement: see the constants above. */
    context.equal(start_stats.match, kRecordedStartMatches,
                  "PROJECT_A start matches equal the recorded measurement");
    context.equal(finish_stats.match, kRecordedFinishMatches,
                  "PROJECT_A finish matches equal the recorded measurement");
    context.equal(slack_stats.match, kRecordedSlackMatches,
                  "PROJECT_A slack matches equal the recorded measurement");
    context.equal(critical_stats.match, kRecordedCriticalMatches,
                  "PROJECT_A critical matches equal the recorded measurement");

    /* Emit the golden file.  It can only contain fully matching uids. */
    std::ofstream golden(PLAN_GOLDEN_PATH);
    if (!golden) {
        context.check(false, std::string("the golden file can be written: ") +
                                 std::string(PLAN_GOLDEN_PATH));
        return;
    }
    for (const std::string& line : golden_lines) {
        golden << line << '\n';
    }
    golden.close();
    context.check(static_cast<bool>(golden), "the golden file was written completely");
    std::cout << "golden: " << (golden_lines.size() - golden_header_lines)
              << " fully matching uids written to " << PLAN_GOLDEN_PATH << '\n'
              << std::flush;
}

/* ---- 3. schedin.mpp: one task per constraint type ----------------------- */
/* The fixture (built by the coordinator's COM script, values recorded in
 * docs/analysis/oracle/schedin-probe.json) stores Project's own recalculated
 * schedule, so the stored rows themselves are the oracle.  It is the smallest
 * place where every constraint type is proved through the bridge. */
void test_schedin(TestContext& context) {
    if (corpus_hidden()) {
        std::cout << "SKIP: schedin constraint fixture (MPP_NO_CORPUS is set)\n";
        return;
    }
    const std::string path = PLAN_SCHEDIN_PATH;
    if (!file_exists(path)) {
        std::cout << "SKIP: " << PLAN_SCHEDIN_PATH
                  << " is not present (corpus is git-ignored)\n";
        return;
    }

    const mpp::Document document = mpp::load_file(path);
    context.equal(document.tasks.size(), std::size_t{10}, "schedin.mpp has 10 live tasks");
    const plan::PlanInput input = from_mpp_with_calendar(document, path);
    context.equal(input.tasks.size(), std::size_t{10}, "schedin.mpp maps 10 tasks");
    context.equal(input.links.size(), std::size_t{0}, "schedin.mpp has no links");
    context.equal(input.summaries, std::size_t{0}, "schedin.mpp is flat");

    /* The reader's constraint/deadline/actual decode: one row per PjConstraint
     * value 0..7, one deadline, one 50 % task with an actual start. */
    std::size_t constraints = 0;
    std::size_t deadlines = 0;
    std::size_t started = 0;
    bool types_seen[8] = {false, false, false, false, false, false, false, false};
    for (const mpp::Task& task : document.tasks) {
        if (task.constraint_type <= 7) {
            types_seen[task.constraint_type] = true;
        }
        if (task.constraint_type != 0) {
            ++constraints;
        }
        if (task.deadline.valid()) {
            ++deadlines;
        }
        if (task.percent_complete > 0) {
            ++started;
        }
    }
    for (int type = 0; type < 8; ++type) {
        context.check(types_seen[type], "schedin.mpp carries constraint type " +
                                            std::to_string(type));
    }
    context.equal(constraints, std::size_t{8},
                  "8 of the 10 schedin tasks carry a non-ASAP constraint "
                  "(types 1..7 plus the 50 % task's SNET)");
    context.equal(deadlines, std::size_t{1}, "one schedin task carries a deadline");
    context.equal(started, std::size_t{1}, "one schedin task is 50 % complete");
    /* The bridge must hand each stored type to the engine unchanged. */
    bool mapping_ok = true;
    std::size_t actual_start_rows = 0;
    for (const plan::PlanTask& task : input.tasks) {
        const mpp::Task* stored = find_task(document, static_cast<std::uint32_t>(task.uid));
        if (stored == nullptr || static_cast<int>(task.constraint) !=
                                     static_cast<int>(stored->constraint_type)) {
            mapping_ok = false;
        }
        if (task.actual_start != sched::kDateNa) {
            ++actual_start_rows;
        }
    }
    context.check(mapping_ok, "every stored constraint type reaches the engine unchanged");
    context.equal(actual_start_rows, std::size_t{1},
                  "the 50 % task's actual start is mapped (and nothing else)");

    const plan::PlanResult result = plan::recalculate(input);
    context.check(result.ok(), "schedin.mpp recalculation succeeds (" + describe(result) + ")");
    if (!result.ok()) {
        return;
    }

    /* Every start and finish, one row per constraint type.  Expected values are
     * the ones Project stored (docs/analysis/oracle/schedin-probe.json).
     *
     * The ALAP row (uid 2) stores TWO date pairs, which is why the comparison
     * needs a rule rather than one field (measured, wave 9b, on this fixture):
     *   +96/+100 calculated = 2026-11-03 08:00-17:00 -- the ALAP placement, and
     *                         exactly what COM reports as Start/Finish for that
     *                         row (schedin-probe.json ID 2);
     *   +104/+108 effective = 2026-10-19 08:00-17:00 -- the ASAP early dates.
     * Project's own date for an ALAP task is therefore the CALCULATED pair.  The
     * old pin compared the engine with +104 and recorded an 18/20 "divergence";
     * that is the same field-identification artefact as W30's +74/+190 misread
     * (memory/schedcore.md), not an engine bug.  The two pairs are identical on
     * every other schedin row, so this rule changes only uid 2. */
    std::size_t date_matches = 0;
    std::size_t date_fields = 0;
    std::size_t slack_matches = 0;
    for (const mpp::Task& task : document.tasks) {
        const plan::PlanTaskResult* actual =
            find_result(result, static_cast<std::int32_t>(task.uid));
        const std::string label = "schedin uid " + std::to_string(task.uid) +
                                  " (constraint " + std::to_string(task.constraint_type) + ")";
        context.check(actual != nullptr, label + " is in the recalculation");
        if (actual == nullptr) {
            continue;
        }
        const bool alap_row =
            task.constraint_type == static_cast<std::uint16_t>(sched::ConstraintType::as_late_as_possible);
        const sched::date32 stored_start =
            alap_row ? task.calculated_start.raw : task.start.raw;
        const sched::date32 stored_finish =
            alap_row ? task.calculated_finish.raw : task.finish.raw;
        ++date_fields;
        if (actual->start == stored_start) {
            ++date_matches;
        } else {
            std::cout << "  schedin start mismatch uid " << task.uid << " recalculated="
                      << plan::format_date(actual->start) << " stored="
                      << plan::format_date(stored_start) << '\n';
        }
        context.equal_date(actual->start, stored_start, label + " recalculated start");
        ++date_fields;
        if (actual->finish == stored_finish) {
            ++date_matches;
        } else {
            std::cout << "  schedin finish mismatch uid " << task.uid << " recalculated="
                      << plan::format_date(actual->finish) << " stored="
                      << plan::format_date(stored_finish) << '\n';
        }
        context.equal_date(actual->finish, stored_finish, label + " recalculated finish");
        if (actual->total_slack == static_cast<sched::ticks>(task.total_slack_ticks)) {
            ++slack_matches;
        }
    }
    /* The ALAP row's two stored pairs, and which one the engine matches.  The
     * diverging pair is a storage fact of the fixture, not a scheduling result:
     * both checks below fail if the reader or this rule ever collapses the two
     * pairs. */
    const mpp::Task* alap_stored = find_task(document, 2);
    context.check(alap_stored != nullptr &&
                      alap_stored->calculated_start.raw != alap_stored->start.raw,
                  "schedin ALAP row stores different calculated (+96) and effective (+104) starts");
    context.check(alap_stored != nullptr &&
                      alap_stored->calculated_finish.raw != alap_stored->finish.raw,
                  "schedin ALAP row stores different calculated (+100) and effective (+108) finishes");
    context.equal(date_matches, std::size_t{20},
                  "20 of the 20 schedin date fields match (the ALAP row against +96/+100)");
    context.equal(date_fields, std::size_t{20},
                  "20 date fields were compared on schedin.mpp");

    /* The ALAP slack words (measured, wave 9b): TotalSlack is finish slack
     * (LF-EF) = 5280 min (+190, already matching: the old "engine 5280 vs
     * Project 0" divergence was the +74 FreeSlack misread), while StartSlack
     * (+186) and FreeSlack (+74) are 0 because an ALAP task is placed at its late
     * start.  The engine now reports the same three words. */
    const plan::PlanTaskResult* alap = find_result(result, 2);
    context.check(alap != nullptr, "schedin ALAP task is in the recalculation");
    if (alap != nullptr) {
        context.equal(alap->total_slack, static_cast<sched::ticks>(52800),
                      "schedin ALAP total slack is the stored 5280 min (+190)");
        context.equal(alap->start_slack, sched::ticks{0},
                      "schedin ALAP start slack is the stored 0 (+186)");
        context.equal(alap->free_slack, sched::ticks{0},
                      "schedin ALAP free slack is the stored 0 (+74)");
        const mpp::Task* stored = find_task(document, 2);
        context.check(stored != nullptr &&
                          stored->total_slack_ticks == static_cast<std::int32_t>(52800),
                      "schedin ALAP stored TotalSlack is 5280 min (+190)");
    }
    context.equal(slack_matches, std::size_t{10},
                  "all 10 schedin slack values match (the stored ALAP value is +190)");
    std::cout << "schedin: " << date_matches << " / " << date_fields
              << " date fields, " << slack_matches << " / 10 slack values match\n"
              << std::flush;
}

/* ---- 4. w9-elapsedlag.mpp: elapsed lag through the bridge ---------------- */
/* The elapsed-lag fixture (wave 9b; docs/analysis/oracle/w9-elapsedlag.json).
 * Project built it over COM, recalculated and saved it, and its +96/+104 pairs
 * agree on every row, so the stored dates are Project's own.
 *
 * What it proves HERE is the bridge, not only the engine: the link row's +18
 * LagFormat word (7 = working days, 8 = elapsed days; Project stored 8 for the
 * "2 edays"/"3 edays" links and 7 for the matching "2 days" links) has to reach
 * CpmLink::elapsed_lag through core::plan, and the engine then applies the lag
 * as wall-clock time.  Rows T1..T7 (two 3 d/5 d anchors and five 1 d
 * successors) are compared exactly; the FS+3-edays row is the discriminator,
 * because 3 WORKING days from Wed 17:00 would start Tue 10-27 08:00.
 *
 * T8 is the elapsed-DURATION control (2 edays = 28800 ticks).  Wave 11 made it
 * reproducible: the reader exposes the task row's +0xa4 DurationFormat (8 =
 * elapsed days on this row, 7 on T1..T7), the bridge sets
 * PlanTask::elapsed_duration, and the engine counts wall-clock time, giving
 * Mon 10-19 08:00 -> Wed 10-21 08:00 and 106200 ticks of slack.  All 16 date
 * fields and all 8 slack values are compared now. */
void test_elapsed_lag(TestContext& context) {
    if (corpus_hidden()) {
        std::cout << "SKIP: elapsed-lag fixture (MPP_NO_CORPUS is set)\n";
        return;
    }
    const std::string path = PLAN_ELAPSED_PATH;
    if (!file_exists(path)) {
        std::cout << "SKIP: " << PLAN_ELAPSED_PATH << " is not present (corpus is git-ignored)\n";
        return;
    }

    const mpp::Document document = mpp::load_file(path);
    context.equal(document.tasks.size(), std::size_t{8}, "w9-elapsedlag.mpp has 8 live tasks");
    context.equal(document.links.size(), std::size_t{5}, "w9-elapsedlag.mpp has 5 live links");
    std::size_t elapsed_links = 0;
    std::size_t working_links = 0;
    for (const mpp::Link& link : document.links) {
        if (!link.endpoints_live()) {
            continue;
        }
        if (link.lag_units == 8) {
            ++elapsed_links;
        } else if (link.lag_units == 7) {
            ++working_links;
        }
    }
    context.equal(elapsed_links, std::size_t{3},
                  "3 links store LagFormat 8 (elapsed days): the two 2 edays and the 3 edays");
    context.equal(working_links, std::size_t{2},
                  "2 links store LagFormat 7 (working days): the two '2 days' controls");

    const plan::PlanInput input = from_mpp_with_calendar(document, path);
    context.equal(input.links.size(), std::size_t{5}, "the bridge maps all 5 links");
    std::size_t mapped_elapsed = 0;
    for (const plan::PlanLink& link : input.links) {
        if (link.elapsed_lag) {
            ++mapped_elapsed;
        }
    }
    context.equal(mapped_elapsed, std::size_t{3},
                  "the bridge marks exactly the 3 LagFormat-8 links as elapsed");

    const plan::PlanResult result = plan::recalculate(input);
    context.check(result.ok(), "w9-elapsedlag.mpp recalculation succeeds (" + describe(result) + ")");
    if (!result.ok()) {
        return;
    }
    context.equal_date(result.project_finish, sched::from_ymd(2026, 10, 28, 17, 0),
                       "w9-elapsedlag.mpp project finish is the working-day path (Wed 10-28)");

    std::size_t date_matches = 0;
    std::size_t date_fields = 0;
    for (const mpp::Task& task : document.tasks) {
        const plan::PlanTaskResult* actual =
            find_result(result, static_cast<std::int32_t>(task.uid));
        const std::string label = "elapsed uid " + std::to_string(task.uid);
        context.check(actual != nullptr, label + " is in the recalculation");
        if (actual == nullptr) {
            continue;
        }
        ++date_fields;
        if (actual->start == task.start.raw) {
            ++date_matches;
        }
        ++date_fields;
        if (actual->finish == task.finish.raw) {
            ++date_matches;
        }
        context.equal_date(actual->start, task.start.raw, label + " recalculated start");
        context.equal_date(actual->finish, task.finish.raw, label + " recalculated finish");
        context.equal(actual->total_slack, static_cast<sched::ticks>(task.total_slack_ticks),
                      label + " recalculated total slack");
        if (task.uid == 8) {
            context.check(actual->elapsed_duration,
                          "elapsed uid 8 is flagged elapsed by the bridge");
            context.equal(actual->total_slack, sched::ticks{106200},
                          "elapsed uid 8 total slack is the stored 10620 min");
        }
    }
    context.equal(date_fields, std::size_t{16}, "16 date fields were compared");
    context.equal(date_matches, std::size_t{16},
                  "all 16 date fields match, including the elapsed-duration control");
    std::cout << "elapsed: " << date_matches << " / " << date_fields
              << " date fields match, " << elapsed_links << " of 5 links are elapsed\n"
              << std::flush;
}

/* ---- 5. holiday1.mpp: the per-task calendar in the CPM bridge ------------
 * MEASURED (docs/analysis/oracle/holiday-probe.json): the project calendar is
 * Standard, but T1/T2/T3 store task-calendar uid 3 (SiteCal) at task row +178.
 * SiteCal makes Wednesday 2026-10-21 non-working and Friday a 4 h day (08:00-
 * 12:00).  Project's saved dates are the proof: T2 (5 d, FS after T1) starts
 * Thu 10-22 08:00 and ends Thu 10-29 12:00; T3 (1 d, SNET Fri 10-23 08:00)
 * ends Mon 10-26 12:00.  Neither happens on the project calendar.
 *
 * This test runs the same file twice: once with the task calendars attached and
 * once with every task's +178 reset to -1.  The second run is the "no task
 * calendar" control on exactly the same rows/dates, so the difference is the
 * calendar and nothing else. */
void test_task_calendars(TestContext& context) {
    if (corpus_hidden()) {
        std::cout << "SKIP: per-task calendar fixture (MPP_NO_CORPUS is set)\n";
        return;
    }
    const std::string path = PLAN_HOLIDAY_PATH;
    if (!file_exists(path)) {
        std::cout << "SKIP: " << PLAN_HOLIDAY_PATH
                  << " is not present (corpus is git-ignored)\n";
        return;
    }

    const mpp::Document document = mpp::load_file(path);
    context.equal(document.tasks.size(), std::size_t{3}, "holiday1.mpp has 3 tasks");
    std::size_t named = 0;
    for (const mpp::Task& task : document.tasks) {
        named += task.calendar_uid == 3 ? 1u : 0u;
    }
    context.equal(named, std::size_t{3},
                  "all three holiday1 tasks store task-calendar uid 3");

    const plan::PlanInput input = from_mpp_with_calendar(document, path);
    context.equal(input.task_calendars.size(), std::size_t{1},
                  "the bridge builds one calendar for the one referenced uid");
    if (!input.task_calendars.empty()) {
        context.equal(input.task_calendars.front().uid, std::int32_t{3},
                      "the built task calendar is uid 3");
    }

    const plan::PlanResult result = plan::recalculate(input);
    context.check(result.ok(), "holiday1.mpp recalculation succeeds (" +
                                   describe(result) + ")");
    if (!result.ok()) {
        return;
    }
    struct Expected {
        std::int32_t uid;
        sched::date32 start;
        sched::date32 finish;
        sched::ticks total_slack;
        bool critical;
    };
    const Expected expected[] = {
        {1, sched::from_ymd(2026, 10, 19, 8, 0), sched::from_ymd(2026, 10, 20, 17, 0), 0,
         true},
        {2, sched::from_ymd(2026, 10, 22, 8, 0), sched::from_ymd(2026, 10, 29, 12, 0), 0,
         true},
        {3, sched::from_ymd(2026, 10, 23, 8, 0), sched::from_ymd(2026, 10, 26, 12, 0),
         14400, false},
    };
    for (const Expected& row : expected) {
        const std::string label = "holiday uid " + std::to_string(row.uid);
        const plan::PlanTaskResult* actual = find_result(result, row.uid);
        context.check(actual != nullptr, label + " is in the recalculation");
        if (actual == nullptr) {
            continue;
        }
        context.equal(actual->calendar_uid, std::int32_t{3},
                      label + " kept its +178 calendar uid");
        context.equal_date(actual->start, row.start, label + " start");
        context.equal_date(actual->finish, row.finish, label + " finish");
        context.equal(actual->total_slack, row.total_slack, label + " total slack");
        context.equal(actual->critical, row.critical, label + " critical");
        /* The same values are the ones Project stored in the file. */
        const mpp::Task* stored =
            find_task(document, static_cast<std::uint32_t>(row.uid));
        context.check(stored != nullptr, label + " is in the stored task table");
        if (stored != nullptr) {
            context.equal_date(actual->finish, stored->finish.raw,
                               label + " finish equals the stored effective date");
        }
    }
    context.equal_date(result.project_finish, sched::from_ymd(2026, 10, 29, 12, 0),
                       "holiday1 project finish is T2's Thu 10-29 12:00");

    /* Control: same tasks and constraints, every +178 reset to -1.  This is the
     * project-calendar answer, and it differs exactly where the task calendar
     * has the Wednesday holiday / 4 h Friday. */
    plan::PlanInput control = input;
    control.task_calendars.clear();
    for (plan::PlanTask& task : control.tasks) {
        task.calendar_uid = -1;
    }
    const plan::PlanResult project_only = plan::recalculate(control);
    context.check(project_only.ok(), "holiday1 project-calendar control succeeds");
    if (!project_only.ok()) {
        return;
    }
    struct ControlExpected {
        std::int32_t uid;
        sched::date32 start;
        sched::date32 finish;
    };
    const ControlExpected control_expected[] = {
        {1, sched::from_ymd(2026, 10, 19, 8, 0), sched::from_ymd(2026, 10, 20, 17, 0)},
        {2, sched::from_ymd(2026, 10, 21, 8, 0), sched::from_ymd(2026, 10, 27, 17, 0)},
        {3, sched::from_ymd(2026, 10, 23, 8, 0), sched::from_ymd(2026, 10, 23, 17, 0)},
    };
    for (const ControlExpected& row : control_expected) {
        const std::string label = "holiday control uid " + std::to_string(row.uid);
        const plan::PlanTaskResult* actual = find_result(project_only, row.uid);
        context.check(actual != nullptr, label + " is in the control recalculation");
        if (actual == nullptr) {
            continue;
        }
        context.equal_date(actual->start, row.start, label + " start");
        context.equal_date(actual->finish, row.finish, label + " finish");
    }
    /* The discriminating rows: T3 has the same start and constraint in both runs
     * but a different finish, so the task calendar (not the dates) moved it. */
    const plan::PlanTaskResult* with_calendar = find_result(result, 3);
    const plan::PlanTaskResult* without_calendar = find_result(project_only, 3);
    context.check(with_calendar != nullptr && without_calendar != nullptr &&
                      with_calendar->start == without_calendar->start &&
                      with_calendar->finish != without_calendar->finish,
                  "holiday T3: same start, different finish with vs without its "
                  "task calendar");
    std::cout << "task calendar: T2 " << plan::format_date(result.tasks.at(1).start)
              << " vs control " << plan::format_date(project_only.tasks.at(1).start)
              << "; T3 finish " << plan::format_date(result.tasks.at(2).finish)
              << " vs control " << plan::format_date(project_only.tasks.at(2).finish)
              << '\n'
              << std::flush;
}

/* ---- 5. a recurring calendar exception on the task-calendar path --------- */
//
// corpus/probe/w12-exceptions.mpp carries one exception of every PjExceptionType
// on its project calendar (docs/analysis/oracle/w12-exceptions.json is the COM
// read-back) and a derived calendar uid 2 with no week data of its own.  It has
// no tasks, so this builds two synthetic rows on the bridge's own PlanTask: one
// pinned to uid 2, and a control with no task calendar.
//
// The fixture's project-calendar exceptions cover 2026-11-02 .. 2026-11-30
// completely (the daily record) and every Monday and Wednesday to 2026-12-31
// (the weekly record).  So on uid 2 the first working day on or after Mon
// 2026-11-02 08:00 is Tue 2026-12-01, and a 2 d task finishes Thu 2026-12-03
// 17:00 (Wed 12-02 is a weekly exception).  On the stock week the same row
// finishes Tue 2026-11-03 17:00.  The difference is the expanded recurrence.
void test_recurring_task_calendar(TestContext& context) {
    if (corpus_hidden()) {
        std::cout << "SKIP: w12 exception fixture (MPP_NO_CORPUS is set)\n";
        return;
    }
    const std::string path = PLAN_W12_PATH;
    if (!file_exists(path)) {
        std::cout << "SKIP: " << PLAN_W12_PATH << " is not present (corpus is git-ignored)\n";
        return;
    }

    const mpp::Document document = mpp::load_file(path);
    const core::cal::CalendarTable table = core::cal::decode_file(path);
    context.equal(table.exception_count(), std::size_t{7},
                  "w12: the seven PjExceptionType records are decoded");
    context.equal(table.recurring_exceptions, std::size_t{6}, "w12: six are recurring");

    plan::PlanInput input = plan::from_mpp(document);
    input.tasks.clear();  // the fixture carries calendar data only
    input.project_start = sched::from_ymd(2026, 11, 2, 8, 0);
    plan::PlanTask task;
    task.uid = 1;
    task.duration = 2 * 4800;  // two 8 h days
    task.calendar_uid = 2;     // the derived calendar, base uid 1
    input.tasks.push_back(task);
    plan::attach_task_calendars(input, table);
    context.equal(input.task_calendars.size(), std::size_t{1},
                  "w12: the bridge builds the one referenced task calendar");
    if (!input.task_calendars.empty()) {
        context.equal(input.task_calendars.front().uid, std::int32_t{2},
                      "w12: the built task calendar is uid 2");
        context.check(input.task_calendars.front().calendar.day_total_ticks(15647) == 0,
                      "w12: the task calendar carries the base calendar's exceptions");
    }

    const plan::PlanResult result = plan::recalculate(input);
    context.check(result.ok(), "w12 recalculation succeeds (" + describe(result) + ")");
    if (!result.ok()) {
        return;
    }
    const plan::PlanTaskResult* actual = find_result(result, 1);
    context.check(actual != nullptr, "w12 uid 1 is in the recalculation");
    if (actual != nullptr) {
        context.equal(actual->calendar_uid, std::int32_t{2},
                      "w12 uid 1 kept its +178 calendar uid");
        context.equal_date(actual->start, sched::from_ymd(2026, 12, 1, 8, 0),
                           "w12 uid 1 starts Tue 2026-12-01 (November is all non-working)");
        context.equal_date(actual->finish, sched::from_ymd(2026, 12, 3, 17, 0),
                           "w12 uid 1 finishes Thu 2026-12-03 (Wed is a weekly exception)");
    }

    // Control: the same row with no task calendar, on the stock week.
    plan::PlanInput control = input;
    control.task_calendars.clear();
    control.tasks[0].calendar_uid = -1;
    const plan::PlanResult stock = plan::recalculate(control);
    context.check(stock.ok(), "w12 control recalculation succeeds");
    if (!stock.ok()) {
        return;
    }
    const plan::PlanTaskResult* plain = find_result(stock, 1);
    context.check(plain != nullptr, "w12 control uid 1 is in the recalculation");
    if (plain != nullptr) {
        context.equal_date(plain->start, sched::from_ymd(2026, 11, 2, 8, 0),
                           "w12 control uid 1 starts Mon 2026-11-02");
        context.equal_date(plain->finish, sched::from_ymd(2026, 11, 3, 17, 0),
                           "w12 control uid 1 finishes Tue 2026-11-03");
    }
    std::cout << "w12 recurring task calendar: " << plan::format_date(result.tasks.at(0).start)
              << " .. " << plan::format_date(result.tasks.at(0).finish)
              << " vs control " << plan::format_date(stock.tasks.at(0).start) << " .. "
              << plan::format_date(stock.tasks.at(0).finish) << '\n'
              << std::flush;
}

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);
    sched_test::TestContext context;
    try {
        test_base(context);
        test_manual_scheduling(context);
        test_manual_oracle(context);
        test_free_vs_total_slack(context);
        test_project_a(context);
        test_schedin(context);
        test_elapsed_lag(context);
        test_task_calendars(context);
        test_recurring_task_calendar(context);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << '\n';
        return 2;
    }
    return context.finish("plan_recalc");
}
