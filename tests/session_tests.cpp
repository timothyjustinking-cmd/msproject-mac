// tests/session_tests.cpp -- W36: the core/session document model.
//
// What is pinned here:
//   1. load() on the committed synthetic fixture (tests/data/synthetic-base.mpp)
//      builds a consistent document: 3 rows in the RecordSet, the SheetModel and
//      the GanttSource, a successful recalculation, the project span and the
//      status counters.
//   2. The session's rows and RecordSet values agree EXACTLY with an independent
//      core/plan run made by this test (same reader, same calendar decoder, same
//      CPM): every row's start/finish/slack/critical and the match counters.
//      That is the "counts equal core/plan's" requirement, checked without
//      trusting the session's own counters.
//   3. recalculate() refreshes from the plan result without reopening the file:
//      it is idempotent, and a session loaded with Options::recalculate=false
//      shows the stored dates until recalculate() is called.
//   4. The error path: an empty path and a non-.mpp file return LoadResult with
//      a null session and a non-empty error, rather than throwing.
//   5. The real corpus (corpus/project_a.mpp), when present and
//      MPP_NO_CORPUS is not set: 83 tasks, 88 links, the recalculated start and
//      finish counts equal core/plan's (83/83 on the current tree), and every
//      view row count agrees.
//
// CLIENT DATA.  Every corpus assertion is on counts, uids, dates and flags.  No
// task, calendar, resource or assignment name is printed, and the test asserts
// that status_text()/dump() do not contain a single row name or the project
// title.  With corpus/ absent (or MPP_NO_CORPUS=1) the corpus case prints SKIP
// and the rest still runs.
//
// SAFETY.  Like the other core tests, main catches every exception and returns 2;
// nothing here writes a file or touches the network.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cal.h"
#include "fixture_guard.h"
#include "plan.h"
#include "reader.h"
#include "sched_test_support.h"
#include "session.h"

namespace {

namespace plan = core::plan;
namespace sched = core::sched;
namespace session = core::session;
using sched_test::TestContext;

// ---------------------------------------------------------------------------
// Corpus gating (same convention as tests/cal_tests.cpp and mpp_reader_tests)
// ---------------------------------------------------------------------------

std::string environment_value(char const* name) {
    // std::getenv is ISO C++, but MSVC's CRT deprecates it (C4996, fatal under
    // /WX).  _dupenv_s is the sanctioned replacement and owns a malloc buffer.
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

bool file_exists(std::string const& path) {
    if (corpus_hidden()) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    return input.good();
}

// ---------------------------------------------------------------------------
// Small independent helpers (deliberately not the session's own conversions)
// ---------------------------------------------------------------------------

// The comparison core::plan's own tool uses (tools/recalc --compare): the
// task-row word compared as a date32 value, because the two encodings agree
// (see session.h "DATES").  A not-a-date word maps to kDateNa.
sched::date32 stored_date(mpp::PackedDate const& date) {
    return date.valid() ? static_cast<sched::date32>(date.raw) : sched::kDateNa;
}

sched::date32 date_from_value(const core::view::Value& value) {
    if (value.kind != core::view::ValueKind::Date || !value.date.valid()) {
        return sched::kDateNa;
    }
    const std::int32_t tenths = static_cast<std::int32_t>(value.date.hour) * 600 +
                                static_cast<std::int32_t>(value.date.minute) * 10 +
                                static_cast<std::int32_t>(value.date.second) / 6;
    const sched::date32 day = sched::from_ymd(static_cast<std::int32_t>(value.date.year),
                                              static_cast<std::int32_t>(value.date.month),
                                              static_cast<std::int32_t>(value.date.day));
    return sched::at_time(day, tenths);
}

// The independent expectation: reader -> core/plan, with the same calendar the
// session should use.  Counts how many recalculated dates equal the stored ones.
struct ExpectedPlan {
    plan::PlanResult result;
    std::size_t start_matches = 0;
    std::size_t finish_matches = 0;
    std::size_t slack_matches = 0;
    std::size_t critical_matches = 0;
};

ExpectedPlan compute_expected_plan(std::string const& path) {
    const mpp::Document document = mpp::load_file(path);
    plan::PlanInput input = plan::from_mpp(document);
    try {
        input.calendar = core::cal::load_project_calendar(path);
    } catch (const mpp::Error&) {
        // The session falls back to the stock week too; not exercised here.
    }
    ExpectedPlan expected;
    expected.result = plan::recalculate(input);

    // PlanResult::tasks is documented to follow PlanInput::tasks order (reader
    // order).  The loop walks the reader table and indexes the result once.
    for (std::size_t index = 0; index < document.tasks.size(); ++index) {
        if (index >= expected.result.tasks.size()) {
            continue;
        }
        const plan::PlanTaskResult& task = expected.result.tasks[index];
        const mpp::Task& stored = document.tasks[index];
        if (task.start == stored_date(stored.start)) {
            ++expected.start_matches;
        }
        if (task.finish == stored_date(stored.finish)) {
            ++expected.finish_matches;
        }
        if (task.total_slack == static_cast<sched::ticks>(stored.total_slack_ticks)) {
            ++expected.slack_matches;
        }
        if (task.critical == stored.critical) {
            ++expected.critical_matches;
        }
    }
    return expected;
}

const core::view::RecordSet& records_of(const session::Session& loaded) {
    return loaded.records();
}

// Non-empty when every visible column of `row` renders text.
bool row_renders(TestContext& context, const session::Session& loaded, std::size_t row) {
    bool all = true;
    for (std::size_t column = 0; column < loaded.sheet().visible_column_count(); ++column) {
        const std::size_t visible = loaded.sheet().visible_column(column);
        if (loaded.sheet().cell_text(row, visible).empty()) {
            all = false;
        }
    }
    context.check(all, "sheet: every column of row " + std::to_string(row) + " renders");
    return all;
}

// ---------------------------------------------------------------------------
// 1. The committed synthetic fixture
// ---------------------------------------------------------------------------

void test_synthetic_defaults(TestContext& context) {
    const session::LoadResult loaded = session::load(SESSION_BASE_PATH);
    context.check(loaded.session != nullptr, "synthetic: load() returns a session");
    context.check(loaded.error.empty(), "synthetic: load() reports no error");
    if (!loaded.session) {
        return;
    }
    const session::Session& document = *loaded.session;
    const session::Status& status = document.status();

    context.equal(status.tasks, std::size_t{3}, "synthetic: 3 tasks");
    context.equal(status.links, std::size_t{0}, "synthetic: 0 links");
    context.equal(status.skipped_links, std::size_t{0}, "synthetic: 0 skipped links");
    context.equal(status.summaries, std::size_t{0}, "synthetic: 0 summaries");
    context.equal(status.recalculated, true, "synthetic: recalculation succeeded");
    context.equal(status.recalc_tasks, std::size_t{3}, "synthetic: 3 recalculated rows");
    context.check(!status.title.empty(), "synthetic: the project title is exposed");
    context.equal(document.title(), status.title, "synthetic: title() matches the status");

    // The row count is shared by every view, which is the invariant the shell
    // relies on for row/bar alignment.
    context.equal(document.task_count(), std::size_t{3}, "synthetic: task_count is 3");
    context.equal(document.records().row_count(), std::size_t{3},
                  "synthetic: RecordSet has 3 rows");
    context.equal(document.sheet().row_count(), std::size_t{3}, "synthetic: sheet has 3 rows");
    context.equal(document.gantt_source().tasks.size(), std::size_t{3},
                  "synthetic: GanttSource has 3 task inputs");
    const core::gantt::GanttLayout layout = document.gantt_layout();
    context.equal(layout.bars.size(), std::size_t{3}, "synthetic: Gantt layout has 3 bars");
    context.equal(layout.links.size(), std::size_t{0}, "synthetic: Gantt layout has 0 links");
    context.equal(layout.row_count, 3, "synthetic: Gantt row_count is 3");

    // The calendar from the file is attached for shading: Monday is a working
    // day, Sunday is not.
    context.check(layout.calendar != nullptr, "synthetic: the layout carries a calendar");
    context.check(layout.is_working_day(sched::from_ymd(2026, 1, 5)),
                  "synthetic: Monday 2026-01-05 is a working day");
    context.check(!layout.is_working_day(sched::from_ymd(2026, 1, 4)),
                  "synthetic: Sunday 2026-01-04 is not a working day");

    // The sheet's columns are the documented eight, and the identity/computed
    // columns are read-only.
    context.equal(document.sheet().column_count(), std::size_t{8},
                  "synthetic: the sheet has 8 columns");
    context.equal(document.sheet().column_title(0), std::string("UID"),
                  "synthetic: column 0 is UID");
    context.equal(document.sheet().column_title(3), std::string("Name"),
                  "synthetic: column 3 is Name");
    context.equal(document.sheet().column_title(7), std::string("Total Slack"),
                  "synthetic: column 7 is Total Slack");
    context.check(document.sheet().column_read_only(0), "synthetic: UID is read-only");
    context.check(document.sheet().column_read_only(7), "synthetic: Total Slack is read-only");

    context.equal(document.row_uid(0), std::uint32_t{1}, "synthetic: row 0 uid is 1");
    context.equal(document.row_of_uid(3), std::size_t{2}, "synthetic: uid 3 is row 2");
    context.equal(document.row_of_uid(9999), session::kNoRow,
                  "synthetic: an unknown uid is kNoRow");
    row_renders(context, document, 0);

    // Counts only: the project title and task names are client data when the
    // file is a client programme, so they are never printed even here.
    std::cout << "SYNTHETIC: " << status.tasks << " tasks, " << status.links
              << " links, " << status.summaries << " summaries, " << status.milestones
              << " milestones, recalc start " << status.recalc_start_matches << "/"
              << status.recalc_tasks << ", finish " << status.recalc_finish_matches << "/"
              << status.recalc_tasks << '\n';
}

void test_synthetic_against_core_plan(TestContext& context) {
    const ExpectedPlan expected = compute_expected_plan(SESSION_BASE_PATH);
    const session::LoadResult loaded = session::load(SESSION_BASE_PATH);
    context.check(loaded.session != nullptr, "synthetic/plan: session loaded");
    if (!loaded.session) {
        return;
    }
    const session::Session& document = *loaded.session;
    const session::Status& status = document.status();
    const plan::PlanResult& scheduled = expected.result;

    context.equal(status.recalculated, scheduled.ok(),
                  "synthetic/plan: the session agrees that the run succeeded");
    context.equal(status.recalc_tasks, scheduled.tasks.size(),
                  "synthetic/plan: same recalculated row count");
    context.equal(status.recalc_start_matches, expected.start_matches,
                  "synthetic/plan: same start-match count");
    context.equal(status.recalc_finish_matches, expected.finish_matches,
                  "synthetic/plan: same finish-match count");
    context.equal(status.recalc_slack_matches, expected.slack_matches,
                  "synthetic/plan: same slack-match count");
    context.equal(status.recalc_critical_matches, expected.critical_matches,
                  "synthetic/plan: same critical-match count");
    // This fixture's three tasks are MANUALLY scheduled and store a date-only
    // start (2026-01-04 13:00) and a 12:00 finish; the CPM aligns the start to
    // Monday 08:00 and the finish to 17:00, so zero stored dates are reproduced.
    // That is the fixture's documented shape (memory/plan.md), not a session
    // defect: the assertion that matters is the equality with core/plan above.
    context.equal(status.recalc_start_matches, std::size_t{0},
                  "synthetic/plan: the manual fixture reproduces no stored start");
    context.equal(status.recalc_finish_matches, std::size_t{0},
                  "synthetic/plan: the manual fixture reproduces no stored finish");

    context.equal_date(status.project_start, scheduled.project_start,
                       "synthetic/plan: project start == core/plan's");
    context.equal_date(status.project_finish, scheduled.project_finish,
                       "synthetic/plan: project finish == core/plan's");

    // Every row, field by field, against the independent run.
    const core::view::FieldId start_field = session::task_field_id("Start");
    const core::view::FieldId finish_field = session::task_field_id("Finish");
    const core::view::FieldId slack_field = session::task_field_id("TotalSlack");
    const core::view::FieldId critical_field = session::task_field_id("Critical");
    context.check(start_field != 0 && finish_field != 0 && slack_field != 0 &&
                      critical_field != 0,
                  "synthetic/plan: the display fields resolve");
    const std::size_t count =
        std::min(document.task_count(), static_cast<std::size_t>(scheduled.tasks.size()));
    for (std::size_t index = 0; index < count; ++index) {
        const session::Row& row = document.row(index);
        const plan::PlanTaskResult& task = scheduled.tasks[index];
        const std::string label = "synthetic/plan row " + std::to_string(index);
        context.equal(row.uid, static_cast<std::uint32_t>(task.uid), label + ": uid");
        context.equal_date(document.task_start(index), task.start, label + ": start");
        context.equal_date(document.task_finish(index), task.finish, label + ": finish");
        context.equal(document.task_total_slack(index), task.total_slack,
                      label + ": total slack");
        context.equal(document.task_critical(index), task.critical, label + ": critical");
        context.equal_date(date_from_value(*records_of(document).get(index, start_field)),
                           task.start, label + ": RecordSet Start");
        context.equal_date(date_from_value(*records_of(document).get(index, finish_field)),
                           task.finish, label + ": RecordSet Finish");
        if (const core::view::Value* slack = records_of(document).get(index, slack_field)) {
            context.equal(slack->integer, static_cast<std::int64_t>(task.total_slack),
                          label + ": RecordSet TotalSlack");
        } else {
            context.check(false, label + ": RecordSet TotalSlack present");
        }
        if (const core::view::Value* critical = records_of(document).get(index, critical_field)) {
            context.equal(critical->boolean, task.critical, label + ": RecordSet Critical");
        } else {
            context.check(false, label + ": RecordSet Critical present");
        }
    }

    // The Gantt bars carry the same dates as the rows, in the same order.
    const core::gantt::GanttLayout layout = document.gantt_layout();
    context.equal(layout.bars.size(), count, "synthetic/plan: one bar per task");
    for (std::size_t index = 0; index < std::min(layout.bars.size(), count); ++index) {
        context.equal(layout.bars[index].task_uid,
                      static_cast<std::uint32_t>(scheduled.tasks[index].uid),
                      "synthetic/plan: bar uid in row order");
        context.check(layout.bars[index].rect.w >= 0.0,
                      "synthetic/plan: bar width is not negative");
    }

    // The name-free renderings say what they should and quote nothing from the
    // file.
    const std::string summary = document.status_text();
    context.check(summary.find("tasks 3") != std::string::npos,
                  "synthetic/plan: status_text names the task count");
    context.check(summary.rfind("tasks 3, links 0 (0 live), skipped 0 (reader rows 0), "
                                "summary links 0",
                                0) == 0,
                  "synthetic/plan: status_text keeps its documented first line");
    context.check(summary.find("recalc yes: start 0/3, finish 0/3") != std::string::npos,
                  "synthetic/plan: status_text reports the recalculation counts");
    context.check(summary.find(status.title) == std::string::npos,
                  "synthetic/plan: status_text omits the title");
    const std::string dump = document.dump();
    context.check(dump.find("uid\tid\tlevel") != std::string::npos,
                  "synthetic/plan: dump has the row header");
    for (const session::Row& row : document.rows()) {
        context.check(dump.find(row.name) == std::string::npos,
                      "synthetic/plan: dump omits a task name");
    }
}

// ---------------------------------------------------------------------------
// 2. recalculate(), the stored-vs-computed switch and Options
// ---------------------------------------------------------------------------

void test_recalculate_without_reload(TestContext& context) {
    const ExpectedPlan expected = compute_expected_plan(SESSION_BASE_PATH);
    session::Options options;
    options.recalculate = false;
    const session::LoadResult loaded = session::load(SESSION_BASE_PATH, options);
    context.check(loaded.session != nullptr, "recalc: load(recalculate=false) succeeds");
    if (!loaded.session) {
        return;
    }
    session::Session& document = *loaded.session;
    context.equal(document.status().recalculated, false,
                  "recalc: nothing is recalculated at load");
    context.equal(document.status().recalc_tasks, std::size_t{0},
                  "recalc: no rows are reported as scheduled");
    for (std::size_t index = 0; index < document.task_count(); ++index) {
        context.equal_date(document.task_start(index), document.stored_task_start(index),
                           "recalc: before recalculate() the row shows the stored start");
    }

    context.equal(document.recalculate(), true, "recalc: recalculate() succeeds");
    context.equal(document.status().recalculated, true, "recalc: status flips to true");
    context.equal(document.status().recalc_start_matches, expected.start_matches,
                  "recalc: start-match count after recalculate()");
    for (std::size_t index = 0; index < document.task_count(); ++index) {
        context.equal_date(document.task_start(index), expected.result.tasks[index].start,
                           "recalc: the row is refreshed from the plan result");
        context.equal_date(
            date_from_value(*document.records().get(index, session::task_field_id("Start"))),
            expected.result.tasks[index].start, "recalc: the RecordSet is refreshed too");
        context.equal_date(document.gantt_source().tasks[index].start,
                           expected.result.tasks[index].start,
                           "recalc: the Gantt bar is refreshed too");
    }

    // Idempotent: a second run over unchanged input reports the same counters.
    const std::size_t start_matches = document.status().recalc_start_matches;
    const std::size_t finish_matches = document.status().recalc_finish_matches;
    const std::size_t warning_count = document.status().warnings.size();
    context.equal(document.recalculate(), true, "recalc: a second call succeeds");
    context.equal(document.status().recalc_start_matches, start_matches,
                  "recalc: idempotent start-match count");
    context.equal(document.status().recalc_finish_matches, finish_matches,
                  "recalc: idempotent finish-match count");
    context.equal(document.status().warnings.size(), warning_count,
                  "recalc: no duplicate warning is appended");
}

void test_options(TestContext& context) {
    // The status date reaches the Gantt layout.
    session::Options options;
    options.status_date = sched::from_ymd(2026, 1, 6);
    options.pixels_per_day = 30.0;
    const session::LoadResult loaded = session::load(SESSION_BASE_PATH, options);
    context.check(loaded.session != nullptr, "options: load with a status date succeeds");
    if (!loaded.session) {
        return;
    }
    const core::gantt::GanttLayout layout = loaded.session->gantt_layout();
    context.equal(layout.has_status, true, "options: the layout has a status marker");
    context.equal_date(layout.status_date, options.status_date,
                       "options: the marker is on the requested date");
    context.equal(layout.timescale.pixels_per_unit, options.pixels_per_day,
                  "options: pixels_per_day reaches the timescale");
    context.equal(loaded.session->sheet_metrics().row_height,
                  static_cast<int>(layout.style.row_height),
                  "options: sheet and Gantt row heights agree");

    // Skipping the file calendar still loads and schedules (on the stock week).
    session::Options stock;
    stock.use_file_calendar = false;
    const session::LoadResult fallback = session::load(SESSION_BASE_PATH, stock);
    context.check(fallback.session != nullptr, "options: use_file_calendar=false loads");
    if (fallback.session) {
        context.equal(fallback.session->status().recalculated, true,
                      "options: the stock-week run still recalculates");
        context.equal(fallback.session->status().recalc_start_matches, std::size_t{0},
                      "options: the stock week still reproduces no stored start");
    }
}

// ---------------------------------------------------------------------------
// 3. Error paths: no exceptions, a null session and a message
// ---------------------------------------------------------------------------

void test_error_paths(TestContext& context) {
    const session::LoadResult empty = session::load("");
    context.check(empty.session == nullptr, "errors: an empty path yields no session");
    context.check(!empty.error.empty(), "errors: an empty path yields an error message");
    context.equal(static_cast<bool>(empty), false, "errors: the result tests false");

    const session::LoadResult not_mpp = session::load(SESSION_NOT_MPP_PATH);
    context.check(not_mpp.session == nullptr, "errors: a non-.mpp file yields no session");
    context.check(!not_mpp.error.empty(), "errors: a non-.mpp file yields an error message");

    const session::LoadResult missing = session::load("no-such-file-session-tests.mpp");
    context.check(missing.session == nullptr, "errors: a missing file yields no session");
    context.check(!missing.error.empty(), "errors: a missing file yields an error message");
}

// ---------------------------------------------------------------------------
// 4. The real corpus (counts only, never a name)
// ---------------------------------------------------------------------------

void test_corpus(TestContext& context, std::string const& path) {
    const ExpectedPlan expected = compute_expected_plan(path);
    const session::LoadResult loaded = session::load(path);
    context.check(loaded.session != nullptr, "corpus: load() returns a session");
    context.check(loaded.error.empty(), "corpus: load() reports no error");
    if (!loaded.session) {
        return;
    }
    session::Session& document = *loaded.session;
    const session::Status& status = document.status();

    context.equal(status.tasks, std::size_t{83}, "corpus: 83 tasks");
    context.equal(status.links, std::size_t{88}, "corpus: 88 links");
    context.equal(status.links_with_live_endpoints, std::size_t{88},
                  "corpus: 88 links with live endpoints");
    context.equal(status.skipped_links, std::size_t{0}, "corpus: 0 skipped links");
    context.equal(status.resources, std::size_t{22}, "corpus: 22 resources");
    context.equal(status.assignments, std::size_t{67}, "corpus: 67 assignments");
    context.equal(status.summaries, std::size_t{16}, "corpus: 16 summaries");
    context.equal(status.milestones, std::size_t{10}, "corpus: 10 milestones");
    context.equal(status.recalculated, true, "corpus: the recalculation succeeded");
    context.equal(status.recalc_tasks, std::size_t{83}, "corpus: 83 recalculated rows");

    // The independent core/plan run is the authority for the counts.
    context.equal(status.recalc_tasks, expected.result.tasks.size(),
                  "corpus: same recalculated rows as core/plan");
    context.equal(status.recalc_start_matches, expected.start_matches,
                  "corpus: start matches equal core/plan's");
    context.equal(status.recalc_finish_matches, expected.finish_matches,
                  "corpus: finish matches equal core/plan's");
    context.equal(status.recalc_slack_matches, expected.slack_matches,
                  "corpus: slack matches equal core/plan's");
    context.equal(status.recalc_critical_matches, expected.critical_matches,
                  "corpus: critical matches equal core/plan's");
    context.equal(status.recalc_start_matches, std::size_t{83},
                  "corpus: every stored start is reproduced (83)");
    context.equal(status.recalc_finish_matches, std::size_t{83},
                  "corpus: every stored finish is reproduced (83)");
    context.equal_date(status.project_start, expected.result.project_start,
                       "corpus: project start equals core/plan's");
    context.equal_date(status.project_finish, expected.result.project_finish,
                       "corpus: project finish equals core/plan's");

    // Every row agrees with the independent run, and the three views agree on
    // the row count.
    const std::size_t count =
        std::min(document.task_count(), static_cast<std::size_t>(expected.result.tasks.size()));
    context.equal(count, std::size_t{83}, "corpus: 83 rows compared");
    for (std::size_t index = 0; index < count; ++index) {
        const session::Row& row = document.row(index);
        const plan::PlanTaskResult& task = expected.result.tasks[index];
        context.equal_date(document.task_start(index), task.start,
                           "corpus: row " + std::to_string(index) + " start");
        context.equal_date(document.task_finish(index), task.finish,
                           "corpus: row " + std::to_string(index) + " finish");
        context.equal(document.task_total_slack(index), task.total_slack,
                      "corpus: row " + std::to_string(index) + " slack");
        context.equal(document.task_critical(index), task.critical,
                      "corpus: row " + std::to_string(index) + " critical");
        context.equal(row.uid, static_cast<std::uint32_t>(task.uid),
                      "corpus: row " + std::to_string(index) + " uid");
        context.equal_date(
            date_from_value(
                *document.records().get(index, session::task_field_id("Start"))),
            task.start, "corpus: RecordSet Start row " + std::to_string(index));
    }
    context.equal(document.records().row_count(), count, "corpus: RecordSet row count");
    context.equal(document.sheet().row_count(), count, "corpus: sheet row count");
    context.equal(document.records().row_count(), document.gantt_source().tasks.size(),
                  "corpus: sheet rows == gantt task inputs");
    const core::gantt::GanttLayout layout = document.gantt_layout();
    context.equal(layout.bars.size(), count, "corpus: one bar per task");
    context.equal(layout.links.size(), status.links_with_live_endpoints,
                  "corpus: one Gantt route per endpoint-live link");
    context.equal(document.gantt_source().links.size(), status.links_with_live_endpoints,
                  "corpus: GanttSource carries every endpoint-live link");
    context.equal(layout.row_count, static_cast<int>(count), "corpus: Gantt row_count");
    row_renders(context, document, 0);
    row_renders(context, document, count - 1);

    // Client-data guard: neither rendering quotes a name or the project title.
    const std::string summary = document.status_text();
    const std::string dump = document.dump();
    context.check(summary.find(status.title) == std::string::npos,
                  "corpus: status_text omits the project title");
    for (const session::Row& row : document.rows()) {
        context.check(summary.find(row.name) == std::string::npos,
                      "corpus: status_text omits task names");
        context.check(dump.find(row.name) == std::string::npos,
                      "corpus: dump omits task names");
    }
    context.check(dump.size() > summary.size(), "corpus: dump is the longer rendering");

    std::cout << "CORPUS: project_a: " << status.tasks << " tasks, " << status.links
              << " links, " << status.resources << " resources, " << status.assignments
              << " assignments, " << status.summaries << " summaries, " << status.milestones
              << " milestones, " << status.critical << " critical, recalc start "
              << status.recalc_start_matches << "/" << status.recalc_tasks << ", finish "
              << status.recalc_finish_matches << "/" << status.recalc_tasks << ", slack "
              << status.recalc_slack_matches << "/" << status.recalc_tasks << ", critical "
              << status.recalc_critical_matches << "/" << status.recalc_tasks << '\n';

    // A second recalculation is idempotent on the real file too.
    const std::size_t matches = status.recalc_start_matches;
    context.equal(document.recalculate(), true, "corpus: recalculate() again succeeds");
    context.equal(document.status().recalc_start_matches, matches,
                  "corpus: idempotent start-match count");
    context.check(!document.dump().empty(), "corpus: dump stays non-empty");
}

}  // namespace

int main() {
    sched_test::TestContext context;
    try {
        test_synthetic_defaults(context);
        test_synthetic_against_core_plan(context);
        test_recalculate_without_reload(context);
        test_options(context);
        test_error_paths(context);

        if (fixture::corpus_present(SESSION_PROJECT_A_PATH)) {
            test_corpus(context, SESSION_PROJECT_A_PATH);
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL: exception escaped: " << error.what() << '\n';
        return 2;
    } catch (...) {
        std::cerr << "FAIL: unknown exception escaped\n";
        return 2;
    }
    return context.finish("session_tests");
}
