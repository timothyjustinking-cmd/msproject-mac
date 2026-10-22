// app/start_edit_warning.cpp -- see start_edit_warning.h.

#include "start_edit_warning.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "date.h"
#include "gantt.h"

namespace app {
namespace {

const TaskRow* find_task(const PlanInput& plan, std::uint32_t uid) noexcept {
    for (const TaskRow& task : plan.tasks) {
        if (task.uid == uid) {
            return &task;
        }
    }
    return nullptr;
}

StartEditWarning no_effect(std::uint32_t uid, date32 requested, std::string detail) {
    StartEditWarning warning;
    warning.kind = StartEditWarningKind::no_effect;
    warning.task_uid = uid;
    warning.requested_start = requested;
    warning.effective_start = kDateNa;
    warning.detail = std::move(detail);
    return warning;
}

std::string date_text(date32 date, const core::sheet::SheetFormat& format) {
    if (core::sched::is_na(date)) {
        return "NA";
    }
    return core::sheet::format_date_text(date_value_from_date32(date), format);
}

}  // namespace

StartEditWarning warning_for_start_edit(const PlanInput& plan, std::uint32_t task_uid,
                                        date32 new_start) {
    const TaskRow* task = find_task(plan, task_uid);
    if (task == nullptr) {
        return no_effect(task_uid, new_start,
                         "no task with uid " + std::to_string(task_uid));
    }
    if (core::sched::is_na(new_start)) {
        return no_effect(task_uid, new_start, "the start is not set");
    }

    StartEditWarning warning;
    warning.task_uid = task_uid;
    warning.requested_start = new_start;
    warning.effective_start = new_start;

    if (task->summary) {
        return no_effect(task_uid, new_start,
                         "a summary task's start is rolled up from its subtasks");
    }
    if (task->start == new_start) {
        return no_effect(task_uid, new_start, "the start is already that date");
    }
    if (task->manually_scheduled) {
        warning.kind = StartEditWarningKind::manual_task_will_be_rescheduled;
        return warning;
    }

    // Auto-scheduled: a finish-to-start predecessor later than the edited start
    // is the barrier Project's recalculation places the row behind.  Lag moves
    // the barrier by the same amount (positive = later).
    std::int64_t barrier = 0;
    std::uint32_t barrier_uid = 0;
    bool have_barrier = false;
    for (const LinkRow& link : plan.links) {
        if (link.successor_uid != task_uid ||
            link.type != core::gantt::LinkType::finish_to_start) {
            continue;
        }
        const TaskRow* predecessor = find_task(plan, link.predecessor_uid);
        if (predecessor == nullptr || core::sched::is_na(predecessor->finish)) {
            continue;
        }
        const std::int64_t candidate =
            static_cast<std::int64_t>(predecessor->finish) + link.lag_ticks;
        if (candidate <= 0 ||
            candidate > static_cast<std::int64_t>(core::sched::kDateNa)) {
            continue;
        }
        if (!have_barrier || candidate > barrier) {
            barrier = candidate;
            barrier_uid = link.predecessor_uid;
            have_barrier = true;
        }
    }

    if (have_barrier && barrier > static_cast<std::int64_t>(new_start)) {
        warning.kind = StartEditWarningKind::clamped_to_predecessor;
        warning.effective_start = static_cast<date32>(barrier);
        warning.predecessor_uid = barrier_uid;
        return warning;
    }
    return warning;
}

std::string start_edit_warning_text(const StartEditWarning& warning,
                                    const core::sheet::SheetFormat& format) {
    switch (warning.kind) {
        case StartEditWarningKind::none:
            return {};
        case StartEditWarningKind::clamped_to_predecessor: {
            std::string text = "Task uid " + std::to_string(warning.task_uid) +
                               ": start " + date_text(warning.requested_start, format) +
                               " is before predecessor uid " +
                               std::to_string(warning.predecessor_uid) + "'s finish (" +
                               date_text(warning.effective_start, format) +
                               "); Project will reopen the task no earlier than " +
                               date_text(warning.effective_start, format) + ".";
            return text;
        }
        case StartEditWarningKind::manual_task_will_be_rescheduled:
            return "Task uid " + std::to_string(warning.task_uid) +
                   " is manually scheduled: saving this start writes a Start No Earlier "
                   "Than constraint and Project will reopen the task auto-scheduled.";
        case StartEditWarningKind::no_effect:
            return "Task uid " + std::to_string(warning.task_uid) + ": " +
                   warning.detail + "; this start edit has no effect.";
    }
    return {};
}

StartEditWarningReport warn_start_edits(const std::vector<StartEdit>& edits,
                                        const PlanInput& plan,
                                        const core::sheet::SheetFormat& format) {
    StartEditWarningReport report;
    for (const StartEdit& edit : edits) {
        const StartEditWarning warning =
            warning_for_start_edit(plan, edit.uid, edit.start);
        if (warning.kind == StartEditWarningKind::none) {
            continue;
        }
        ++report.warnings;
        switch (warning.kind) {
            case StartEditWarningKind::none:
                break;
            case StartEditWarningKind::clamped_to_predecessor:
                ++report.clamped;
                break;
            case StartEditWarningKind::manual_task_will_be_rescheduled:
                ++report.manual;
                break;
            case StartEditWarningKind::no_effect:
                ++report.no_effect;
                break;
        }
        if (!report.text.empty()) {
            report.text += "\n";
        }
        report.text += "- " + start_edit_warning_text(warning, format);
    }
    return report;
}

}  // namespace app
