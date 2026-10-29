// app/app_edit_tests.cpp -- Insert Task / Delete Task, proved headlessly (wave 12).
//
// Two layers:
//   1. the pending-edit MODEL (app/edit_model.h) on synthetic plans: what
//      apply() shows, where an inserted row lands, which dependent rows a delete
//      removes, the placeholder uid, push validation and undo/redo;
//   2. the real thing on a COPY of the committed synthetic fixture
//      (tests/data/synthetic-base.mpp, NOT client data -- see tests/data/
//      README.md) under APP_TEST_OUT_DIR: insert one task, save, re-read the file
//      and check the count and the uid the writer allocated; delete a task,
//      save, and check the count and that the cascade tombstoned its link rows;
//      a combined structural + start-date save; the refusals (no target, target ==
//      source without the app's own-file option, an unknown uid); and that the
//      fixture itself is never written.
//
// What this cannot prove: that MS Project reopens the written file with the same
// model.  That was measured through COM in wave 10/11 (w10-oracle §1,
// w11-oracle §2: add = 4 tasks, delete = 2 tasks and no dependants) and is not
// repeated here -- there is no Project on this box and none on the runner.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\vs-w51 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\vs-w51 --config Release
//   ctest --test-dir build\vs-w51 -C Release -R "^app_edit$" --output-on-failure

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "date.h"
#include "edit_model.h"
#include "layout_model.h"
#include "reader_adapter.h"

namespace {

using app::date32;
using app::kDateNa;
using core::sched::from_ymd;

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

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

std::size_t bytes_on_disk(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    return error ? 0 : static_cast<std::size_t>(size);
}

app::TaskRow make_task(std::uint32_t uid, std::int64_t duration_ticks, date32 start) {
    app::TaskRow row;
    row.uid = uid;
    row.id = uid;
    // These helpers model rows the file already stores (the reader gives real
    // tasks file_row >= 1; 0 is a pending insert).  Wave 28 makes the percent
    // rules depend on that distinction, so a hand-built live row must carry it.
    row.file_row = uid;
    row.name = "Task";
    row.duration_ticks = duration_ticks;
    row.start = start;
    row.finish = start;
    return row;
}

app::PlanInput make_plan() {
    app::PlanInput plan;
    plan.tasks.push_back(make_task(1, 4800, from_ymd(2026, 1, 5, 8, 0)));
    plan.tasks.push_back(make_task(2, 4800, from_ymd(2026, 1, 6, 8, 0)));
    plan.tasks.push_back(make_task(3, 4800, from_ymd(2026, 1, 7, 8, 0)));
    app::LinkRow first;
    first.predecessor_uid = 1;
    first.successor_uid = 2;
    app::LinkRow second;
    second.predecessor_uid = 2;
    second.successor_uid = 3;
    plan.links.push_back(first);
    plan.links.push_back(second);
    // W30-0.0 (R29-8): the actuals writer requires exactly one TBkndAssn row
    // per edited task, so every live task of a real file carries one -- the
    // fixtures' rows are Project's placeholder (resource uid 0) except the one
    // real assignment below, which models the W29-0.2 work shape on task 2.
    app::AssignmentRow first_placeholder;
    first_placeholder.uid = 8;
    first_placeholder.task_uid = 1;
    first_placeholder.work_ticks = 4800;
    plan.assignments.push_back(first_placeholder);
    app::AssignmentRow assignment;
    assignment.uid = 10;
    assignment.task_uid = 2;
    assignment.resource_uid = 100;
    assignment.work_ticks = 4800;
    plan.assignments.push_back(assignment);
    app::AssignmentRow third_placeholder;
    third_placeholder.uid = 9;
    third_placeholder.task_uid = 3;
    third_placeholder.work_ticks = 4800;
    plan.assignments.push_back(third_placeholder);
    return plan;
}

app::StructuralEdit insert_after(const std::string& name, std::uint32_t uid,
                                 std::uint32_t anchor) {
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::insert_task;
    edit.name = name;
    edit.uid = uid;
    edit.anchor_uid = anchor;
    return edit;
}

app::StructuralEdit delete_uid(std::uint32_t uid) {
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::delete_task;
    edit.uid = uid;
    return edit;
}

app::StructuralEdit set_text1(std::uint32_t uid, const std::string& text) {
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::set_field;
    edit.uid = uid;
    edit.field_id = mpp::custom_field_id(mpp::FieldKind::text, 1);
    edit.field_value.text = text;
    return edit;
}

// ---------------------------------------------------------------------------
// The pending-edit model
// ---------------------------------------------------------------------------

void test_push_and_undo() {
    // W33-1.1 (A-3): a delete names the plan it is checked against, so the
    // model can refuse a row with live subtasks.
    const app::PlanInput plan = make_plan();
    app::StructuralEdits edits;
    check(edits.empty(), "a fresh edit list is empty");
    check(!edits.can_undo() && !edits.can_redo(), "nothing to undo or redo");

    check(!edits.push(insert_after("", 4, 0)), "an empty name is refused");
    check(!edits.push(insert_after("   ", 4, 0)), "a whitespace-only name is refused");
    check(!edits.push(insert_after("New", 0, 0)), "uid 0 is refused");
    check(edits.empty(), "a refused push changes nothing");

    check(edits.push(insert_after("New Task", 4, 1)), "a valid insert is accepted");
    check(edits.size() == 1, "one edit");
    check(!edits.push(insert_after("Other", 4, 0)), "a duplicate placeholder uid is refused");
    check(edits.push(delete_uid(2), &plan), "a delete is accepted");
    check(!edits.push(delete_uid(2), &plan), "a duplicate delete is refused");
    check(edits.inserted_count() == 1 && edits.deleted_count() == 1, "one insert and one delete");

    check(edits.undo(), "undo moves the delete to the redo list");
    check(edits.can_redo(), "the redo list is not empty");
    check(edits.deleted_count() == 0, "the delete is no longer applied");
    check(edits.redo(), "redo applies it again");
    check(edits.deleted_count() == 1, "the delete is applied again");

    check(edits.push(delete_uid(3), &plan), "a new edit clears the redo list");
    check(!edits.can_redo(), "the redo list is empty");

    check(edits.is_pending_insert(4), "the insert placeholder is recognized");
    check(!edits.is_pending_insert(2), "a real uid is not a placeholder");
    check(edits.drop_insert(4), "dropping the pending insert works");
    check(!edits.is_pending_insert(4), "the insert is gone");
    check(!edits.drop_insert(4), "dropping it twice is refused");
}

void test_percent_edit_model() {
    // Wave 27 (W27-0.2): the leaf/summary rule lives in the edit MODEL, so no
    // shell path (and no future one) can queue a %-complete edit Project would
    // recalculate away.
    app::PlanInput plan = make_plan();
    app::StructuralEdit edit;
    std::string error;

    check(!app::make_percent_edit(plan, 0, 50, edit, error),
          "uid 0 is refused by the model");
    check(error.find("project summary") != std::string::npos,
          "the refusal names the project-summary row");

    plan.tasks[0].summary = true;
    check(!app::make_percent_edit(plan, plan.tasks[0].uid, 50, edit, error),
          "a summary row is refused by the model");
    check(error.find("summary") != std::string::npos,
          "the refusal names the summary task");
    plan.tasks[0].summary = false;

    check(!app::make_percent_edit(plan, 99, 50, edit, error),
          "an unknown uid is refused (a pending insert has no saved row)");
    check(!app::make_percent_edit(plan, plan.tasks[1].uid, 101, edit, error),
          "a percent above 100 is refused");

    check(app::make_percent_edit(plan, plan.tasks[0].uid, 40, edit, error),
          "a leaf task is accepted on the measured 0 -> 40 transition");
    check(edit.kind == app::StructuralKind::set_percent &&
              edit.uid == plan.tasks[0].uid && edit.percent == 40,
          "the accepted edit is a ready-to-push set_percent");

    // W29-0.0 (the wave-28 review's R28-2, High): a command that writes the
    // .mpp is enabled only for the row shapes and transitions a committed
    // Project pair measured.  W29-0.2 lifted the leaf-under-summary and
    // work-bearing shapes for their measured 0 -> 40 transition; one check per
    // refusal and one per lift.
    {
        app::PlanInput below = make_plan();
        below.tasks[0].outline_level = 1;
        app::StructuralEdit accepted_edit;
        check(app::make_percent_edit(below, below.tasks[0].uid, 40,
                                     accepted_edit, error),
              "a leaf under a summary is accepted on the measured 0 -> 40 "
              "shape (W29-0.2)");
        check(accepted_edit.kind == app::StructuralKind::set_percent &&
                  accepted_edit.percent == 40,
              "the accepted leaf-under-summary edit is a set_percent");

        app::PlanInput milestone = make_plan();
        milestone.tasks[0].milestone = true;
        app::StructuralEdit refused_edit;
        check(!app::make_percent_edit(milestone, milestone.tasks[0].uid, 40,
                                      refused_edit, error),
              "a milestone is refused");
        check(error.find("milestone") != std::string::npos,
              "the milestone refusal names the shape");

        // make_plan() gives task 2 a real assignment; W29-0.2 measured that row
        // shape at 0 -> 40, and W30-0.6 pins the ONE measured point: a
        // 9600-tick task whose 100 % assignment carries 9600 ticks of work.
        auto measured_work_plan = []() {
            app::PlanInput plan = make_plan();
            plan.tasks[1].duration_ticks = 9600;
            for (app::AssignmentRow& assignment : plan.assignments) {
                if (assignment.task_uid == plan.tasks[1].uid) {
                    assignment.work_ticks = 9600;
                }
            }
            return plan;
        };
        app::PlanInput working = measured_work_plan();
        check(app::make_percent_edit(working, working.tasks[1].uid, 40,
                                     accepted_edit, error),
              "a task with a work-bearing assignment is accepted on the "
              "measured 0 -> 40 shape (W29-0.2)");

        // W30-0.6 (R29-6, High): the resource-loaded contour rule has one data
        // point.  One check per refused axis -- duration, units, work and the
        // task calendar -- and the measured plan above stays accepted.
        app::StructuralEdit work_refused;
        app::PlanInput other_duration = measured_work_plan();
        other_duration.tasks[1].duration_ticks = 4800;
        check(!app::make_percent_edit(other_duration, other_duration.tasks[1].uid,
                                      40, work_refused, error),
              "a work-bearing row at another duration is refused");
        check(error.find("9600") != std::string::npos &&
                  error.find("contour") != std::string::npos,
              "the duration refusal names the measured ticks and the contour");

        app::PlanInput other_units = measured_work_plan();
        for (app::AssignmentRow& assignment : other_units.assignments) {
            if (assignment.task_uid == other_units.tasks[1].uid) {
                assignment.units_percent = 50.0;
            }
        }
        check(!app::make_percent_edit(other_units, other_units.tasks[1].uid, 40,
                                      work_refused, error),
              "a work-bearing row at non-measured units is refused");
        check(error.find("100 % units") != std::string::npos,
              "the units refusal names the measured 100 %");

        app::PlanInput other_work = measured_work_plan();
        for (app::AssignmentRow& assignment : other_work.assignments) {
            if (assignment.task_uid == other_work.tasks[1].uid) {
                assignment.work_ticks = 4800;
            }
        }
        check(!app::make_percent_edit(other_work, other_work.tasks[1].uid, 40,
                                      work_refused, error),
              "a work-bearing row carrying other work is refused");
        check(error.find("9600 ticks of work") != std::string::npos,
              "the work refusal names the measured ticks of work");

        app::PlanInput task_calendar = measured_work_plan();
        task_calendar.tasks[1].calendar_uid = 3;
        check(!app::make_percent_edit(task_calendar, task_calendar.tasks[1].uid,
                                      40, work_refused, error),
              "a work-bearing row with its own task calendar is refused");
        check(error.find("task calendar") != std::string::npos,
              "the calendar refusal names the task calendar");

        // R29-1's residual on the percent path: a real assignment with ZERO
        // work still takes the writer's real-assignment branch.
        app::PlanInput zero_work = measured_work_plan();
        for (app::AssignmentRow& assignment : zero_work.assignments) {
            if (assignment.task_uid == zero_work.tasks[1].uid) {
                assignment.work_ticks = 0;
            }
        }
        check(!app::make_percent_edit(zero_work, zero_work.tasks[1].uid, 40,
                                      work_refused, error),
              "a real assignment that carries no work is refused");
        check(error.find("carries no work") != std::string::npos,
              "the zero-work refusal names the missing work");

        // The four measured transitions: auto 0->40, 40->100, 100->0 and
        // manual 0->40.  0->50, 40->0 and either manual transition are not.
        app::PlanInput transitions = make_plan();
        check(!app::make_percent_edit(transitions, transitions.tasks[0].uid, 50,
                                      refused_edit, error),
              "0 -> 50 is refused (no measured transition)");
        check(error.find("transition") != std::string::npos,
              "the transition refusal names the rule");
        transitions.tasks[0].percent_complete = 40;
        check(app::make_percent_edit(transitions, transitions.tasks[0].uid, 100,
                                     refused_edit, error),
              "an automatic 40 -> 100 is accepted");
        check(!app::make_percent_edit(transitions, transitions.tasks[0].uid, 0,
                                      refused_edit, error),
              "an automatic 40 -> 0 is refused");
        transitions.tasks[0].percent_complete = 100;
        check(app::make_percent_edit(transitions, transitions.tasks[0].uid, 0,
                                     refused_edit, error),
              "an automatic 100 -> 0 is accepted");
        transitions.tasks[0].percent_complete = 0;
        transitions.tasks[0].manually_scheduled = true;
        check(app::make_percent_edit(transitions, transitions.tasks[0].uid, 40,
                                     refused_edit, error),
              "a manual 0 -> 40 stays accepted");
        transitions.tasks[0].percent_complete = 40;
        check(!app::make_percent_edit(transitions, transitions.tasks[0].uid, 100,
                                      refused_edit, error),
              "a manual 40 -> 100 is refused");
    }

    // W30-0.0 (R29-8): the shapes the actuals writer throws on at save time are
    // refused up front, one check per branch.  A throw there fails the WHOLE
    // save, so the queue would block every later Save.
    {
        app::StructuralEdit refused_edit;
        app::PlanInput no_assignment = make_plan();
        no_assignment.assignments.erase(
            std::remove_if(no_assignment.assignments.begin(),
                           no_assignment.assignments.end(),
                           [](const app::AssignmentRow& assignment) {
                               return assignment.task_uid == 1;
                           }),
            no_assignment.assignments.end());
        check(!app::make_percent_edit(no_assignment, 1, 40, refused_edit, error),
              "a row with no assignment row is refused");
        check(error.find("assignment") != std::string::npos,
              "the no-assignment refusal names the assignment table");

        app::PlanInput two_assignments = make_plan();
        app::AssignmentRow extra;
        extra.uid = 11;
        extra.task_uid = 1;
        extra.work_ticks = 4800;
        two_assignments.assignments.push_back(extra);
        check(!app::make_percent_edit(two_assignments, 1, 40, refused_edit,
                                      error),
              "a row with two assignment rows is refused");
        check(error.find("assignment") != std::string::npos,
              "the two-assignment refusal names the assignment table");

        // W30-C5 (C3): the writer counts PHYSICAL TBkndAssn rows, tombstones
        // included, while the plan carries only live rows.  One live row over a
        // tombstoned sibling (wave30-assign-delete.mpp's task 1) looks like the
        // measured shape here and throws at save.
        app::PlanInput hidden_assignment = make_plan();
        hidden_assignment.tasks[0].assignment_rows = 2;
        check(!app::make_percent_edit(hidden_assignment, 1, 40, refused_edit,
                                      error),
              "a row with two PHYSICAL assignment rows is refused");
        check(error.find("physical") != std::string::npos,
              "the physical-row refusal names the file's physical rows");
        hidden_assignment.tasks[0].assignment_rows = 1;
        check(app::make_percent_edit(hidden_assignment, 1, 40, refused_edit,
                                     error),
              "one physical row matching the one live row stays accepted");

        // W30-C5: raw resource word 0 reads as "no resource" like Project's
        // 0xFFFF0001 placeholder, but the placeholder rules were measured only
        // on the sentinel.
        app::PlanInput raw_zero = make_plan();
        for (app::AssignmentRow& assignment : raw_zero.assignments) {
            if (assignment.task_uid == 1) {
                assignment.resource_uid_raw = 0;
            }
        }
        check(!app::make_percent_edit(raw_zero, 1, 40, refused_edit, error),
              "a placeholder with raw resource word 0 is refused");
        check(error.find("0xFFFF0001") != std::string::npos,
              "the raw-zero refusal names the measured placeholder word");

        // W30-C5: a link whose endpoint task is not live is dropped from the
        // plan but still counted by the writer's link scan.
        app::PlanInput skipped_link = make_plan();
        skipped_link.skipped_links = 1;
        skipped_link.unreadable_links = 1;
        check(!app::make_percent_edit(skipped_link, 1, 40, refused_edit, error),
              "a plan with a skipped link is refused");
        check(error.find("not live") != std::string::npos,
              "the skipped-link refusal names the dropped link");

        app::LinkRow first_incoming;
        first_incoming.predecessor_uid = 3;
        first_incoming.successor_uid = 1;
        app::PlanInput two_links = make_plan();
        two_links.links.push_back(first_incoming);
        app::LinkRow second_incoming;
        second_incoming.predecessor_uid = 2;
        second_incoming.successor_uid = 1;
        two_links.links.push_back(second_incoming);
        check(!app::make_percent_edit(two_links, 1, 40, refused_edit, error),
              "a row with two live incoming links is refused");
        check(error.find("incoming link") != std::string::npos,
              "the two-link refusal names the incoming links");

        // The writer's ONE measured link shape: one incoming link on a row
        // that enters progress (0 % -> non-zero).  The same link with any other
        // transition throws at save time.
        app::PlanInput one_link = make_plan();
        one_link.links.push_back(first_incoming);
        check(app::make_percent_edit(one_link, 1, 40, refused_edit, error),
              "one incoming link with 0 -> 40 is the measured link-flag shape");
        one_link.tasks[0].percent_complete = 40;
        check(!app::make_percent_edit(one_link, 1, 100, refused_edit, error),
              "one incoming link with 40 -> 100 is refused");
        check(error.find("incoming link") != std::string::npos,
              "the non-entering refusal names the incoming link");
    }

    app::StructuralEdits planless;
    check(!planless.push(edit), "a plan-less set_percent is refused");

    app::PlanInput with_summary = make_plan();
    with_summary.tasks[0].summary = true;
    app::StructuralEdit summary_edit;
    check(!app::make_percent_edit(with_summary, with_summary.tasks[0].uid, 50, summary_edit,
                                  error),
          "the helper refuses the summary row too");
    app::StructuralEdit bypass;
    bypass.kind = app::StructuralKind::set_percent;
    bypass.uid = with_summary.tasks[0].uid;
    bypass.percent = 50;
    app::StructuralEdits refused;
    check(!refused.push(bypass, &with_summary),
          "push with the plan refuses a hand-built summary edit");

    // W27-3.1: no path can queue a % edit for a row that exists only as a
    // pending insert -- its placeholder uid is not in the loaded plan.
    app::StructuralEdit pending;
    pending.kind = app::StructuralKind::set_percent;
    pending.uid = 99;
    pending.percent = 25;
    app::StructuralEdits no_pending;
    check(!no_pending.push(pending, &plan),
          "push refuses a percent edit for a uid with no file row (a pending insert)");

    // W28-1.2 (wave-27 review finding 6): the shell's display plan is
    // StructuralEdits::apply(), which CONTAINS the pending insert.  Presence in
    // that plan must not be mistaken for a saved row: the save would drop the
    // edit and still report success.
    app::StructuralEdits with_insert;
    check(with_insert.push(insert_after("Pending Insert", 77, 0)),
          "the pending insert is queued");
    const app::PlanInput with_pending = with_insert.apply(plan);
    bool pending_seen = false;
    for (const app::TaskRow& row : with_pending.tasks) {
        if (row.uid == 77) {
            pending_seen = true;
            check(row.file_row == 0, "the applied plan's insert has no file row");
        }
    }
    check(pending_seen, "apply() includes the pending insert in the display plan");
    app::StructuralEdit on_pending;
    std::string pending_error;
    check(!app::make_percent_edit(with_pending, 77, 25, on_pending, pending_error),
          "make_percent_edit refuses the applied plan's pending insert");
    check(pending_error.find("saved") != std::string::npos,
          "the refusal says the row is not saved");
    app::StructuralEdit pending_percent;
    pending_percent.kind = app::StructuralKind::set_percent;
    pending_percent.uid = 77;
    pending_percent.percent = 25;
    check(!with_insert.push(pending_percent, &with_pending),
          "push with the applied plan also refuses the pending insert");

    app::StructuralEdits accepted;
    check(accepted.push(edit, &plan), "push with the plan accepts a leaf task");
    check(accepted.apply(plan).tasks[0].percent_complete == 40,
          "the display plan shows the accepted 40 %");
}

void test_delete_undo_restores_edits() {
    // W27-3.2: a delete removes the pending field/percent/link-add edits for
    // its task (they could never reach the file), but undo must restore them.
    const app::PlanInput plan = make_plan();
    app::StructuralEdits edits;

    app::StructuralEdit percent_edit;
    std::string error;
    check(app::make_percent_edit(plan, 1, 40, percent_edit, error),
          "the percent edit is built for the delete/undo test");
    check(edits.push(percent_edit, &plan), "the percent edit is queued");
    check(edits.push(set_text1(1, "note")), "a field edit for the same task is queued");
    app::StructuralEdit link;
    link.kind = app::StructuralKind::add_link;
    link.uid = 1;
    link.predecessor_uid = 3;
    check(edits.push(link), "a link add naming the task is queued");
    check(edits.field_edit_count() == 1 && edits.link_edit_count() == 1,
          "the task's field and link edits are applied");
    check(edits.apply(plan).tasks[0].percent_complete == 40,
          "the display plan carries the queued percent");

    check(edits.push(delete_uid(1), &plan), "deleting the task is accepted");
    check(edits.field_edit_count() == 0 && edits.link_edit_count() == 0,
          "the delete displaces the task's field and link edits");
    check_size(edits.size(), 1, "only the delete remains applied");
    const app::StructuralEdit* deleted = edits.next_undo();
    check(deleted != nullptr && deleted->displaced.size() == 3,
          "the delete keeps all three displaced edits");
    check_size(edits.apply(plan).tasks.size(), 2,
               "the display plan hides the deleted task");

    check(edits.undo(), "undoing the delete works");
    check(edits.field_edit_count() == 1 && edits.link_edit_count() == 1,
          "undo restores the displaced field and link edits");
    check_size(edits.size(), 3, "the three displaced edits are applied again");
    const app::PlanInput restored = edits.apply(plan);
    check(restored.tasks[0].percent_complete == 40,
          "the restored percent edit shows again");
    bool restored_link = false;
    for (const app::LinkRow& row : restored.links) {
        if (row.predecessor_uid == 3 && row.successor_uid == 1) {
            restored_link = true;
        }
    }
    check(restored_link, "the restored link add shows again");

    check(edits.redo(), "redoing the delete works");
    check(edits.field_edit_count() == 0 && edits.link_edit_count() == 0,
          "redo displaces the edits again");
    check_size(edits.size(), 1, "only the delete is applied after redo");
    check(edits.undo(), "undo after redo works");
    check_size(edits.size(), 3, "undo after redo restores them again");
}

void test_without_structural_edits() {
    app::PlanInput plan = make_plan();
    // A cell edit the user already made: task 1 has a new Start.
    const date32 edited_start = from_ymd(2026, 2, 2, 8, 0);
    plan.tasks[0].start = edited_start;
    plan.tasks[0].name = "Renamed";

    app::StructuralEdits edits;
    edits.push(insert_after("New Task", 4, 2));
    edits.push(delete_uid(3), &plan);
    const app::PlanInput shown = edits.apply(plan);
    const app::PlanInput recovered = edits.without_structural_edits(shown, plan);

    // Placeholders are gone and the deleted task is back, in file order.
    check_size(recovered.tasks.size(), 3, "the recovered plan has the file's task count");
    check(recovered.tasks[0].uid == 1 && recovered.tasks[1].uid == 2 &&
              recovered.tasks[2].uid == 3,
          "the recovered plan is in file order");
    check(recovered.tasks[0].start == edited_start, "the cell edit survives the recovery");
    check_text(recovered.tasks[0].name, "Renamed", "the renamed value survives");
    check_size(recovered.links.size(), 2, "the links the delete would drop are restored");
    check_size(recovered.assignments.size(), 3, "the assignments are restored");

    // W28-4.1: a set_percent edit owns the row's percent_complete in the display
    // plan, and undoing it must give the file's value back -- the sheet and the
    // docked Task Form both read this plan, so a stale percent would keep showing
    // a value the file does not have.
    app::StructuralEdits percent_edits;
    app::StructuralEdit percent;
    percent.kind = app::StructuralKind::set_percent;
    percent.uid = plan.tasks[0].uid;
    // W29-0.0: 0 -> 40 is the measured automatic transition; 50 is not.
    percent.percent = 40;
    check(percent_edits.push(percent, &plan), "a percent edit on a leaf is accepted");
    const app::PlanInput percent_shown = percent_edits.apply(plan);
    check(percent_shown.tasks[0].percent_complete == 40,
          "the display plan shows the queued percent");
    check(percent_edits.undo(), "the percent edit is undone");
    const app::PlanInput percent_recovered =
        percent_edits.without_structural_edits(percent_shown, plan);
    check(percent_recovered.tasks[0].percent_complete ==
              plan.tasks[0].percent_complete,
          "an undone percent edit restores the file's value");

    // Re-applying the edits on the recovered plan is the display plan again, and
    // the cell edit is still there.
    const app::PlanInput rebuilt = edits.apply(recovered);
    check_size(rebuilt.tasks.size(), 3, "the rebuilt display plan has the same shape");
    check(rebuilt.tasks[0].start == edited_start, "the rebuilt plan keeps the cell edit");
    check_size(rebuilt.links.size(), 1, "only the deleted task's link is dropped");
}

void test_apply_placement() {
    const app::PlanInput plan = make_plan();
    app::StructuralEdits edits;
    check(edits.apply(plan).tasks.size() == 3, "an empty edit list changes nothing");
    check(edits.next_placeholder_uid(plan) == 4, "the next placeholder uid is one past the max");

    edits.push(insert_after("Middle", 4, 1));
    app::PlanInput shown = edits.apply(plan);
    check_size(shown.tasks.size(), 4, "the insert is in the display plan");
    check(shown.tasks[0].uid == 1 && shown.tasks[1].uid == 4 && shown.tasks[2].uid == 2,
          "the insert is displayed after its anchor");
    check_text(shown.tasks[1].name, "Middle", "the placeholder carries the name");
    check(shown.tasks[1].start == plan.tasks.back().start,
          "the placeholder copies the last live task's start (the writer's add shape)");
    check(shown.tasks[1].duration_ticks == plan.tasks.back().duration_ticks,
          "the placeholder copies the last live task's duration");
    check(shown.tasks[1].id == 0, "the placeholder has no file id yet");
    check(edits.next_placeholder_uid(plan) == 5, "the next placeholder uid accounts for it");

    app::StructuralEdits at_end;
    at_end.push(insert_after("End", 9, 0));
    shown = at_end.apply(plan);
    check_size(shown.tasks.size(), 4, "an unanchored insert is in the plan");
    check(shown.tasks.back().uid == 9, "anchor 0 puts the row at the end");

    // A delete removes the task and exactly the rows the writer's cascade
    // tombstones: its links (either endpoint) and its assignments.
    app::StructuralEdits deletion;
    deletion.push(delete_uid(2), &plan);
    shown = deletion.apply(plan);
    check_size(shown.tasks.size(), 2, "the deleted task is gone");
    check_size(shown.links.size(), 0, "both links naming the task are gone");
    check_size(shown.assignments.size(), 2, "its assignment is gone");
    check_size(shown.skipped_links, plan.skipped_links + 2,
               "the two dropped links are counted as skipped");
    check_size(app::count_delete_dependants(plan, 2), 3,
               "a delete of uid 2 would tombstone 2 links and 1 assignment");
    check_size(app::count_delete_dependants(plan, 1), 2,
               "uid 1 has one link and one assignment row");
    check_size(app::count_delete_dependants(plan, 99), 0, "an unknown uid has none");

    // An insert anchored to a task that the same edit set deletes lands at the
    // end rather than disappearing.
    app::StructuralEdits mixed;
    mixed.push(delete_uid(2), &plan);
    mixed.push(insert_after("Orphan", 7, 2));
    shown = mixed.apply(plan);
    check_size(shown.tasks.size(), 3, "the delete and the insert both apply");
    check(shown.tasks.back().uid == 7, "an insert with a dead anchor lands at the end");
    check(shown.tasks[0].uid == 1 && shown.tasks[1].uid == 3, "the surviving order is kept");
}

// ---------------------------------------------------------------------------
// The real writer sequence
// ---------------------------------------------------------------------------

#if defined(APP_TEST_DATA_DIR) && defined(APP_TEST_OUT_DIR)

struct Loaded {
    app::PlanInput plan;
    std::string path;
    bool ok = false;
};

Loaded load(const std::filesystem::path& path) {
    Loaded loaded;
    loaded.path = path.string();
    try {
        const mpp::Document document = mpp::load_file(loaded.path);
        loaded.plan = app::plan_input_from_document(document);
        loaded.ok = true;
    } catch (const std::exception& error) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: cannot load %s: %s\n", loaded.path.c_str(), error.what());
    }
    return loaded;
}

// The uid whose delete would tombstone the most dependent rows (the middle of the
// fixture's FS chain); 0 when the fixture has no links.
std::uint32_t busiest_uid(const app::PlanInput& plan, std::size_t& dependants) {
    std::uint32_t best = 0;
    dependants = 0;
    for (const app::TaskRow& row : plan.tasks) {
        const std::size_t count = app::count_delete_dependants(plan, row.uid);
        if (count > dependants) {
            dependants = count;
            best = row.uid;
        }
    }
    return best;
}

void test_real_save_round_trip() {
    const std::filesystem::path fixture =
        std::filesystem::path(APP_TEST_DATA_DIR) / "synthetic-base.mpp";
    if (!std::filesystem::exists(fixture)) {
        std::printf("SKIP: %s is not present; the edit round trip needs the committed fixture\n",
                    fixture.string().c_str());
        return;
    }
    const std::size_t fixture_bytes = bytes_on_disk(fixture);
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);

    // Work on a COPY under the build tree: the committed fixture is read-only for
    // this test (and the copy proves the "source is never written" contract below).
    const std::filesystem::path copy =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-source.mpp";
    std::filesystem::remove(copy, error);
    std::filesystem::copy_file(fixture, copy, std::filesystem::copy_options::overwrite_existing,
                               error);
    check(!error, "the fixture was copied into the build tree");
    const std::size_t copy_bytes = bytes_on_disk(copy);

    Loaded base = load(copy);
    if (!base.ok || base.plan.tasks.empty()) {
        return;
    }
    check_size(base.plan.tasks.size(), 3, "the fixture has three tasks");
    // The committed fixture's dependent rows are assignments; the writer's delete
    // cascade covers both kinds (the pure test above pins the link half).
    check_size(base.plan.links.size(), 0, "the fixture has no link rows");
    check_size(base.plan.assignments.size(), 3, "the fixture has one assignment per task");
    check(base.plan.assignments.front().task_uid == base.plan.tasks.front().uid,
          "the first assignment names the first task");

    // ---- insert -----------------------------------------------------------
    const std::uint32_t anchor = base.plan.tasks[1].uid;
    app::StructuralEdits inserts;
    check(inserts.push(insert_after("New Task", inserts.next_placeholder_uid(base.plan),
                                    anchor)),
          "the insert is recorded");
    const app::PlanInput shown = inserts.apply(base.plan);
    check_size(shown.tasks.size(), 4, "the display plan shows the new row");

    const std::filesystem::path added =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-added.mpp";
    std::filesystem::remove(added, error);
    const app::StructuralSaveResult add_outcome =
        app::save_structural_edits(inserts, {}, copy.string(), added.string(), false);
    check(add_outcome.ok, "the insert saves");
    if (!add_outcome.ok) {
        std::printf("SAVE ERROR: %s\n", add_outcome.error.c_str());
        return;
    }
    check(add_outcome.error.empty(), "a successful structural save has no error");
    check_size(add_outcome.added, 1, "one row was added");
    check_size(add_outcome.deleted, 0, "nothing was deleted");
    check_size(add_outcome.added_uids.size(), 1, "the writer reported the new uid");
    check(!add_outcome.api.empty(), "the outcome names the writer calls");
    std::printf("EDIT: %s\n", add_outcome.api.c_str());
    for (const std::string& note : add_outcome.notes) {
        std::printf("EDIT NOTE: %s\n", note.c_str());
    }
    for (const std::string& warning : add_outcome.warnings) {
        std::printf("EDIT WARNING: %s\n", warning.c_str());
    }
    check_size(bytes_on_disk(copy), copy_bytes, "the source copy is untouched by the insert save");
    check(std::filesystem::exists(added), "the inserted file exists");
    check(!std::filesystem::exists(added.string() + ".w51-structural-tmp"),
          "no intermediate file is left behind");

    Loaded after_add = load(added);
    check(after_add.ok, "the file after the insert re-loads");
    if (!after_add.ok) {
        return;
    }
    check_size(after_add.plan.tasks.size(), 4, "the saved file has four tasks");
    const std::uint32_t new_uid =
        add_outcome.added_uids.empty() ? 0u : add_outcome.added_uids.front();
    check(new_uid != 0, "the writer allocated a non-zero uid");
    check(new_uid != anchor, "the new uid is not the anchor's");
    bool found = false;
    bool found_last = false;
    for (std::size_t index = 0; index < after_add.plan.tasks.size(); ++index) {
        if (after_add.plan.tasks[index].uid != new_uid) {
            continue;
        }
        found = true;
        found_last = index + 1 == after_add.plan.tasks.size();
        check_text(after_add.plan.tasks[index].name, "New Task", "the new row has its name");
    }
    check(found, "the new task reads back from the file");
    check(found_last, "the writer appends, so the new row is last in the file");

    // ---- delete, with the cascade -----------------------------------------
    std::size_t dependants = 0;
    const std::uint32_t doomed = busiest_uid(after_add.plan, dependants);
    check(dependants >= 1, "the fixture has a dependent row to cascade");
    std::size_t dependant_links = 0;
    std::size_t dependant_assignments = 0;
    for (const app::LinkRow& link : after_add.plan.links) {
        if (link.predecessor_uid == doomed || link.successor_uid == doomed) {
            ++dependant_links;
        }
    }
    for (const app::AssignmentRow& assignment : after_add.plan.assignments) {
        if (assignment.task_uid == doomed) {
            ++dependant_assignments;
        }
    }
    check_size(dependant_links + dependant_assignments, dependants,
               "the dependant count is links plus assignments");
    app::StructuralEdits deletes;
    app::StructuralEdit remove;
    remove.kind = app::StructuralKind::delete_task;
    remove.uid = doomed;
    remove.dependants = dependants;
    check(deletes.push(remove, &after_add.plan), "the delete is recorded");
    const app::PlanInput delete_shown = deletes.apply(after_add.plan);
    check_size(delete_shown.tasks.size(), after_add.plan.tasks.size() - 1,
               "the display plan hides the deleted task");
    check_size(delete_shown.links.size(), 0, "the display plan hides its links");

    const std::filesystem::path deleted =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-deleted.mpp";
    std::filesystem::remove(deleted, error);
    const app::StructuralSaveResult delete_outcome =
        app::save_structural_edits(deletes, {}, added.string(), deleted.string(), false);
    check(delete_outcome.ok, "the delete saves");
    if (!delete_outcome.ok) {
        std::printf("SAVE ERROR: %s\n", delete_outcome.error.c_str());
        return;
    }
    check_size(delete_outcome.deleted, 1, "one row was tombstoned");
    check_size(delete_outcome.added, 0, "nothing was added");
    check_size(delete_outcome.links_tombstoned, dependant_links,
               "every dependent link was tombstoned");
    check_size(delete_outcome.assignments_tombstoned, dependant_assignments,
               "every dependent assignment was tombstoned");
    const std::size_t added_bytes = bytes_on_disk(added);
    check(added_bytes != 0, "the file that was deleted from exists");
    check_size(bytes_on_disk(deleted) != 0 ? 1u : 0u, 1u, "the delete wrote a file");

    Loaded after_delete = load(deleted);
    check(after_delete.ok, "the file after the delete re-loads");
    if (!after_delete.ok) {
        return;
    }
    check_size(after_delete.plan.tasks.size(), 3, "the saved file has three tasks again");
    check_size(after_delete.plan.links.size(),
               after_add.plan.links.size() - dependant_links,
               "the cascade removed the dependent links");
    check_size(after_delete.plan.assignments.size(),
               after_add.plan.assignments.size() - dependant_assignments,
               "the cascade removed the dependent assignments");
    for (const app::TaskRow& row : after_delete.plan.tasks) {
        check(row.uid != doomed, "the deleted uid is not a live task");
    }

    // ---- structural + start-date edits: REFUSED (W32-0.2, R31-2) -----------
    // Start is read-only in this port: Project's measured Start save also
    // rewrites calculated and effective dates, slack, the project roll-up, a
    // Var2 record and DSI, while this port's writer stores only the constraint
    // window.  The edit model refuses the combination before the first write,
    // so the insert beside it does not land either.
    Loaded combined_base = load(copy);
    check(combined_base.ok, "the source re-loads for the combined save");
    check(!combined_base.plan.tasks.empty(),
          "the source has a task for the combined save");
    if (combined_base.ok && !combined_base.plan.tasks.empty()) {
        app::StructuralEdits combined;
        check(combined.push(insert_after("Combined", combined.next_placeholder_uid(combined_base.plan),
                                         0)),
              "the combined insert is recorded");
        const date32 new_start = from_ymd(2026, 3, 2, 8, 0);
        std::vector<app::StartEdit> starts;
        app::StartEdit start_edit;
        start_edit.uid = combined_base.plan.tasks.front().uid;
        start_edit.start = new_start;
        starts.push_back(start_edit);
        const std::filesystem::path both =
            std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-combined.mpp";
        std::filesystem::remove(both, error);
        const app::StructuralSaveResult outcome = app::save_structural_edits(
            combined, starts, copy.string(), both.string(), false);
        check(!outcome.ok, "a structural and a start edit are refused together");
        check(outcome.error.find("Start is read-only") != std::string::npos,
              "the refusal names Start");
        check(outcome.error.find("not measured against Project") != std::string::npos,
              "the refusal names decision 9");
        check(!std::filesystem::exists(both), "the refused save writes no target file");
        check(!std::filesystem::exists(both.string() + ".w51-structural-tmp"),
              "the refused save leaves no intermediate file");
    }

    // ---- percent complete (wave 26, W26-1.2; ENABLED by wave 28, W28-1.2) --
    // The model applies the edit last-one-wins; the save cascade writes the
    // measured actuals block (mppwrite::write_percent_actuals) in the one final
    // container -- Project's own save shape, not the old +92 word.  The four
    // Project save pairs are app_edits_actuals_tests' job.
    Loaded percent_base = load(copy);
    check(percent_base.ok, "the source re-loads for the percent save");
    check(!percent_base.plan.tasks.empty(),
          "the source has a task for the percent save");
    if (percent_base.ok && !percent_base.plan.tasks.empty()) {
        const std::uint32_t percent_uid = percent_base.plan.tasks.front().uid;
        app::StructuralEdits percent_edits;
        app::StructuralEdit percent_edit;
        percent_edit.kind = app::StructuralKind::set_percent;
        percent_edit.uid = percent_uid;
        percent_edit.percent = 40;
        check(percent_edits.push(percent_edit, &percent_base.plan),
              "the percent edit is recorded");
        const app::PlanInput percent_shown = percent_edits.apply(percent_base.plan);
        check(!percent_shown.tasks.empty() &&
                  percent_shown.tasks.front().percent_complete == 40,
              "apply() shows the task at 40 % complete");

        percent_edit.percent = 101;
        check(!percent_edits.push(percent_edit, &percent_base.plan),
              "a percent above 100 is refused");

        const std::filesystem::path percent_path =
            std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-percent.mpp";
        std::filesystem::remove(percent_path, error);
        const app::StructuralSaveResult percent_outcome = app::save_structural_edits(
            percent_edits, {}, copy.string(), percent_path.string(), false);
        check(percent_outcome.ok, "the percent edit saves");
        check_size(percent_outcome.percent_rows_written, 1,
                   "one task's PercentComplete was written");
        check(!std::filesystem::exists(percent_path.string() + ".w51-structural-tmp"),
              "the percent save leaves no intermediate file");
        Loaded percent_reread = load(percent_path);
        check(percent_reread.ok, "the percent save re-loads");
        if (percent_reread.ok) {
            bool percent_found = false;
            for (const app::TaskRow& row : percent_reread.plan.tasks) {
                if (row.uid == percent_uid) {
                    percent_found = true;
                    check_size(row.percent_complete, 40,
                               "the saved file reads back 40 % complete");
                }
            }
            check(percent_found, "the percent-edited task is in the saved file");
        }

        // W27-3.1: the save layer refuses ONE unwritable percent edit instead
        // of failing the whole write, so a stale/hand-built queue (the shape of
        // a % edit on a pending insert) cannot poison every later Save.  The
        // plan carries uid 999; the source file does not.
        app::PlanInput stale = percent_base.plan;
        stale.tasks.push_back(make_task(999, 4800, from_ymd(2026, 1, 8, 8, 0)));
        // W30-0.0: the model now requires one assignment row per edited task,
        // so the stale row carries the placeholder every real row has; the
        // refusal under test is still the source's missing task row.
        app::AssignmentRow orphan_assignment;
        orphan_assignment.uid = 998;
        orphan_assignment.task_uid = 999;
        orphan_assignment.work_ticks = 4800;
        stale.assignments.push_back(orphan_assignment);
        app::StructuralEdits stale_edits;
        app::StructuralEdit stale_edit;
        stale_edit.kind = app::StructuralKind::set_percent;
        stale_edit.uid = 999;
        // W29-0.0: a measured transition, so the queue reaches the save layer
        // and the save-layer (not the model's transition rule) is what refuses.
        stale_edit.percent = 40;
        check(stale_edits.push(stale_edit, &stale),
              "a percent edit for a plan row the source lacks is queued");
        const std::filesystem::path stale_path =
            std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-percent-stale.mpp";
        std::filesystem::remove(stale_path, error);
        const app::StructuralSaveResult stale_outcome = app::save_structural_edits(
            stale_edits, {}, copy.string(), stale_path.string(), false);
        check(stale_outcome.ok,
              "a percent edit the source cannot carry does not fail the save");
        check_size(stale_outcome.percent_rows_written, 0,
                   "the refused percent edit writes no row");
        bool refused_named = false;
        for (const std::string& warning : stale_outcome.warnings) {
            if (warning.find("999") != std::string::npos) {
                refused_named = true;
            }
        }
        check(refused_named, "the refused percent edit is reported and names its uid");
        check(bytes_on_disk(stale_path) != 0,
              "the refused edit's save still wrote the target");
        const app::StructuralSaveResult stale_again = app::save_structural_edits(
            stale_edits, {}, copy.string(), stale_path.string(), false);
        check(stale_again.ok,
              "the same queue saves again: the refused edit blocks no later Save");

        // W27-3.2: 25 %, delete the task, undo the delete, Save -> the saved
        // file has the percent (the delete used to discard the edit for good).
        app::StructuralEdits undo_edits;
        app::StructuralEdit undo_percent;
        undo_percent.kind = app::StructuralKind::set_percent;
        undo_percent.uid = percent_uid;
        undo_percent.percent = 40;
        check(undo_edits.push(undo_percent, &percent_base.plan),
              "the percent-before-delete edit is queued");
        check(undo_edits.push(delete_uid(percent_uid), &percent_base.plan),
              "the task is deleted");
        check(undo_edits.undo(), "the delete is undone");
        const std::filesystem::path undo_path =
            std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-percent-undo.mpp";
        std::filesystem::remove(undo_path, error);
        const app::StructuralSaveResult undo_outcome = app::save_structural_edits(
            undo_edits, {}, copy.string(), undo_path.string(), false);
        check(undo_outcome.ok, "the restored percent edit saves");
        check_size(undo_outcome.percent_rows_written, 1,
                   "the restored percent edit reached the writer");
        Loaded undo_reread = load(undo_path);
        check(undo_reread.ok, "the undo-save re-reads");
        if (undo_reread.ok) {
            bool undo_found = false;
            for (const app::TaskRow& row : undo_reread.plan.tasks) {
                if (row.uid == percent_uid) {
                    undo_found = true;
                    check_size(row.percent_complete, 40,
                               "the saved file has the percent from before the delete");
                }
            }
            check(undo_found, "the task survives the delete/undo save");
        }
    }

    // ---- refusals ---------------------------------------------------------
    app::StructuralEdits empty_edits;
    const app::StructuralSaveResult none =
        app::save_structural_edits(empty_edits, {}, copy.string(), added.string(), false);
    check(!none.ok, "an empty edit list is refused");
    check(!none.error.empty(), "the empty-edit refusal explains itself");

    app::StructuralEdits one;
    check(one.push(insert_after("No Target", one.next_placeholder_uid(base.plan), 0)),
          "the refusal fixture is recorded");
    const app::StructuralSaveResult no_target =
        app::save_structural_edits(one, {}, copy.string(), std::string(), false);
    check(!no_target.ok, "an empty target is refused");
    check(no_target.error.find("target") != std::string::npos,
          "the no-target refusal names the target");

    const std::filesystem::path untouched =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-refused.mpp";
    std::filesystem::remove(untouched, error);
    const app::StructuralSaveResult same_file =
        app::save_structural_edits(one, {}, copy.string(), copy.string(), false);
    check(!same_file.ok, "writing over the opened file is refused");
    check(same_file.error.find("refusing") != std::string::npos,
          "the overwrite refusal explains itself");
    check_size(bytes_on_disk(copy), copy_bytes, "the refused save left the source untouched");

    // The app's own file may be updated in place (the shell reloads it after a
    // structural save, so the next Save must be able to write it).
    const std::filesystem::path own =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-own.mpp";
    std::filesystem::remove(own, error);
    std::filesystem::copy_file(fixture, own, std::filesystem::copy_options::overwrite_existing,
                               error);
    app::StructuralEdits own_edits;
    Loaded own_loaded = load(own);
    check(own_loaded.ok, "the in-place source re-loads");
    if (own_loaded.ok) {
        check(own_edits.push(insert_after("In Place", own_edits.next_placeholder_uid(own_loaded.plan),
                                          0)),
              "the in-place insert is recorded");
        const app::StructuralSaveResult in_place = app::save_structural_edits(
            own_edits, {}, own.string(), own.string(), true);
        check(in_place.ok, "allow_source_overwrite lets the app update its own file");
        if (in_place.ok) {
            Loaded reread = load(own);
            check(reread.ok && reread.plan.tasks.size() == 4,
                  "the in-place file has the new task");
        }
    }

    // A delete of a uid that is not in the file fails cleanly.  The display plan
    // names the row (push is plan-dependent, W33-1.1); the FILE does not, so the
    // writer's own unknown-uid refusal is what this case measures.
    app::StructuralEdits bogus;
    app::PlanInput bogus_plan;
    bogus_plan.tasks.push_back(make_task(999999u, 4800, from_ymd(2026, 1, 5, 8, 0)));
    check(bogus.push(delete_uid(999999u), &bogus_plan),
          "the bogus delete is recorded");
    const std::filesystem::path bogus_target =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-edit-bogus.mpp";
    std::filesystem::remove(bogus_target, error);
    const app::StructuralSaveResult bogus_outcome = app::save_structural_edits(
        bogus, {}, copy.string(), bogus_target.string(), false);
    check(!bogus_outcome.ok, "deleting an unknown uid is refused");
    check(!bogus_outcome.error.empty(), "the unknown-uid refusal explains itself");
    check(!std::filesystem::exists(bogus_target), "a failed save leaves no file behind");

    check_size(bytes_on_disk(fixture), fixture_bytes,
               "the committed fixture is never written");
    check_size(bytes_on_disk(copy), copy_bytes, "the source copy is untouched by every save");
}

#else
void test_real_save_round_trip() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
}
#endif

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_push_and_undo();
    test_percent_edit_model();
    test_delete_undo_restores_edits();
    test_without_structural_edits();
    test_apply_placement();
    test_real_save_round_trip();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("app_edit_tests: %d checks passed\n", g_checks);
    return 0;
}
