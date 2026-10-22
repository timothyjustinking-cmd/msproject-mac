// app/start_edit_warning_tests.cpp -- the linked-pin / manual-scheduling warning,
// proved headlessly.
//
// The four cases the brief names:
//   * predecessor finish later than the pin  -> clamped_to_predecessor (with date)
//   * pin later than the predecessor finish  -> none
//   * manual task                            -> manual_task_will_be_rescheduled
//   * unlinked task                          -> none
// plus the no-effect cases (unknown uid, cleared date, summary row, unchanged
// date), the finish-to-start-only rule, lag, the message text and the per-save
// report the shell shows as one NSAlert.
//
// Synthetic rows only: no corpus, no client data, no writer.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "date.h"
#include "gantt.h"
#include "layout_model.h"
#include "sheet.h"
#include "start_edit_warning.h"

namespace {

using app::StartEditWarning;
using app::StartEditWarningKind;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_size(std::size_t got, std::size_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

void check_kind(StartEditWarningKind got, StartEditWarningKind want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: wrong warning kind\n", what);
    }
}

void check_contains(const std::string& got, const std::string& needle, const char* what) {
    ++g_checks;
    if (got.find(needle) == std::string::npos) {
        ++g_failures;
        std::printf("FAIL: %s: \"%s\" not in \"%s\"\n", what, needle.c_str(), got.c_str());
    }
}

app::TaskRow task(std::uint32_t uid, const char* name, std::int32_t month, std::int32_t day,
                  bool manual = false, bool summary = false) {
    app::TaskRow row;
    row.uid = uid;
    row.id = uid;
    row.outline_number = std::to_string(uid);
    row.name = name;
    row.start = core::sched::from_ymd(2026, month, day);
    row.finish = core::sched::add_days(row.start, 1);
    row.duration_ticks = core::view::kTicksPerDay;
    row.manually_scheduled = manual;
    row.summary = summary;
    return row;
}

app::PlanInput make_plan() {
    app::PlanInput plan;
    plan.title = "Synthetic warning plan";
    plan.project_start = core::sched::from_ymd(2026, 1, 5);
    plan.tasks.push_back(task(1, "Pred", 1, 6));
    plan.tasks.push_back(task(2, "Linked", 1, 9));
    plan.tasks.push_back(task(3, "Manual", 1, 9, true));
    plan.tasks.push_back(task(4, "Unlinked", 1, 12));
    plan.tasks.push_back(task(5, "Summary", 1, 5, false, true));

    app::LinkRow link;
    link.uid = 1;
    link.predecessor_uid = 1;
    link.successor_uid = 2;
    link.type = core::gantt::LinkType::finish_to_start;
    link.lag_ticks = 0;
    plan.links.push_back(link);
    return plan;
}

core::sheet::SheetFormat default_format() {
    return core::sheet::SheetFormat{};
}

void test_clamped() {
    const app::PlanInput plan = make_plan();
    // Task 1 finishes on 2026-01-07 00:00, so a pin on 2026-01-05 is behind it.
    const StartEditWarning warning =
        app::warning_for_start_edit(plan, 2, core::sched::from_ymd(2026, 1, 5));
    check_kind(warning.kind, StartEditWarningKind::clamped_to_predecessor,
               "a pin before the predecessor finish is clamped");
    check_size(warning.task_uid, 2, "the warning names the edited task");
    check_size(warning.predecessor_uid, 1, "the warning names the clamping predecessor");
    check(warning.effective_start == core::sched::from_ymd(2026, 1, 7),
          "the clamped date is the predecessor finish");
    check(warning.requested_start == core::sched::from_ymd(2026, 1, 5),
          "the requested date is kept in the warning");
    check(warning.detail.empty(), "a clamp has no no-effect detail");

    // Exactly equal to the predecessor finish is not clamped: max(pin, finish) is
    // the pin, so Project reopens at the requested date.
    const StartEditWarning equal =
        app::warning_for_start_edit(plan, 2, core::sched::from_ymd(2026, 1, 7));
    check_kind(equal.kind, StartEditWarningKind::none,
               "a pin equal to the predecessor finish is not clamped");
    check(equal.effective_start == core::sched::from_ymd(2026, 1, 7),
          "the effective date is the requested date");

    const std::string text =
        app::start_edit_warning_text(warning, default_format());
    check_contains(text, "predecessor uid 1", "the clamp message names the predecessor");
    check_contains(text, "Project will reopen the task",
                   "the clamp message says Project reopens elsewhere");
    check_contains(text, "Task uid 2", "the clamp message names the task");
}

void test_pin_later() {
    const app::PlanInput plan = make_plan();
    const StartEditWarning warning =
        app::warning_for_start_edit(plan, 2, core::sched::from_ymd(2026, 1, 8));
    check_kind(warning.kind, StartEditWarningKind::none,
               "a pin later than the predecessor finish is not clamped");
    check(warning.effective_start == core::sched::from_ymd(2026, 1, 8),
          "the effective date is the pin");
    check(warning.predecessor_uid == 0, "no predecessor is blamed");
    check(app::start_edit_warning_text(warning, default_format()).empty(),
          "no warning means no text");
}

void test_manual() {
    const app::PlanInput plan = make_plan();
    const StartEditWarning warning =
        app::warning_for_start_edit(plan, 3, core::sched::from_ymd(2026, 1, 14));
    check_kind(warning.kind, StartEditWarningKind::manual_task_will_be_rescheduled,
               "a manual task gets the manual-specific warning");
    check(warning.effective_start == core::sched::from_ymd(2026, 1, 14),
          "the manual warning keeps the requested date");
    const std::string text = app::start_edit_warning_text(warning, default_format());
    check_contains(text, "manually scheduled",
                   "the manual message says the task is manual");
    check_contains(text, "Start No Earlier Than",
                   "the manual message says what the writer stores");

    // Manual wins over clamped: the save does not preserve manual mode, so that
    // is the primary consequence.
    app::PlanInput linked = make_plan();
    linked.links.push_back(app::LinkRow{2, 1, 3, core::gantt::LinkType::finish_to_start, 0, false});
    const StartEditWarning both =
        app::warning_for_start_edit(linked, 3, core::sched::from_ymd(2026, 1, 5));
    check_kind(both.kind, StartEditWarningKind::manual_task_will_be_rescheduled,
               "a manual task warns manual even when a predecessor would clamp it");
}

void test_unlinked() {
    const app::PlanInput plan = make_plan();
    const StartEditWarning warning =
        app::warning_for_start_edit(plan, 4, core::sched::from_ymd(2026, 1, 14));
    check_kind(warning.kind, StartEditWarningKind::none,
               "an unlinked task edit is not warned about");
    check(warning.effective_start == core::sched::from_ymd(2026, 1, 14),
          "the unlinked edit keeps its date");
}

void test_no_effect() {
    const app::PlanInput plan = make_plan();
    const StartEditWarning unknown =
        app::warning_for_start_edit(plan, 99, core::sched::from_ymd(2026, 1, 5));
    check_kind(unknown.kind, StartEditWarningKind::no_effect, "an unknown uid has no effect");
    check(!unknown.detail.empty(), "an unknown uid explains itself");
    check_contains(app::start_edit_warning_text(unknown, default_format()), "no effect",
                   "the no-effect message says so");

    const StartEditWarning cleared = app::warning_for_start_edit(plan, 4, core::sched::kDateNa);
    check_kind(cleared.kind, StartEditWarningKind::no_effect, "a cleared start has no effect");

    const StartEditWarning same =
        app::warning_for_start_edit(plan, 4, core::sched::from_ymd(2026, 1, 12));
    check_kind(same.kind, StartEditWarningKind::no_effect,
               "re-entering the current start has no effect");

    const StartEditWarning summary =
        app::warning_for_start_edit(plan, 5, core::sched::from_ymd(2026, 2, 1));
    check_kind(summary.kind, StartEditWarningKind::no_effect,
               "a summary task's start is rolled up, so a start edit has no effect");
    check_contains(summary.detail, "summary", "the summary reason names the roll-up");
}

void test_only_finish_to_start_and_lag() {
    app::PlanInput plan = make_plan();
    // Start-to-start: the predecessor's finish is not the start barrier this
    // function models, so no warning is invented.
    plan.links[0].type = core::gantt::LinkType::start_to_start;
    const StartEditWarning ss =
        app::warning_for_start_edit(plan, 2, core::sched::from_ymd(2026, 1, 5));
    check_kind(ss.kind, StartEditWarningKind::none,
               "a start-to-start predecessor is not treated as a finish barrier");

    // FS with one day of lag moves the barrier a day later (4800 ticks = 8 working
    // hours, the file's lag unit).
    app::PlanInput lagged = make_plan();
    lagged.links[0].type = core::gantt::LinkType::finish_to_start;
    lagged.links[0].lag_ticks = 4800;
    const StartEditWarning lag =
        app::warning_for_start_edit(lagged, 2, core::sched::from_ymd(2026, 1, 7));
    check_kind(lag.kind, StartEditWarningKind::clamped_to_predecessor,
               "a positive lag moves the clamp later");
    check(lag.effective_start == core::sched::from_ymd(2026, 1, 7, 8, 0),
          "the lagged barrier is the predecessor finish plus 8 working hours");
}

void test_report() {
    const app::PlanInput plan = make_plan();
    std::vector<app::StartEdit> edits;
    edits.push_back(app::StartEdit{2, core::sched::from_ymd(2026, 1, 5)});   // clamped
    edits.push_back(app::StartEdit{3, core::sched::from_ymd(2026, 1, 14)});  // manual
    edits.push_back(app::StartEdit{4, core::sched::from_ymd(2026, 1, 14)});  // none
    const app::StartEditWarningReport report =
        app::warn_start_edits(edits, plan, default_format());
    check_size(report.warnings, 2, "two of the three edits warn");
    check_size(report.clamped, 1, "one edit is clamped");
    check_size(report.manual, 1, "one edit is manual");
    check_size(report.no_effect, 0, "none is a no-effect edit");
    check_contains(report.text, "- Task uid 2", "the report lists the clamped edit");
    check_contains(report.text, "- Task uid 3", "the report lists the manual edit");
    check(report.text.find('\n') != std::string::npos, "the report has one line per warning");

    const app::StartEditWarningReport clean =
        app::warn_start_edits({}, plan, default_format());
    check_size(clean.warnings, 0, "no edits means no warnings");
    check(clean.text.empty(), "no warnings means no text");
}

}  // namespace

int main() {
    test_clamped();
    test_pin_later();
    test_manual();
    test_unlinked();
    test_no_effect();
    test_only_finish_to_start_and_lag();
    test_report();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("start_edit_warning_tests: %d checks passed\n", g_checks);
    return 0;
}
