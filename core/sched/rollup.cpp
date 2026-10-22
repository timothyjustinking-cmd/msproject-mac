// core/sched/rollup.cpp -- the bottom-up summary rollup (§3.7).
//
// The measured contract is in core/sched/rollup.h,
// docs/analysis/oracle/w3-o6.json and corpus/probe/project_a-recalc.mpp (the 16 PROJECT_A
// summaries).  The late-date rollup (min child LS, max child LF) reproduces the
// measured summary rows; the summary TotalSlack is the rolled-up FINISH slack
// LF-EF (W3-P7 is settled by the PROJECT_A summaries: uid 8141 stores TotalSlack 6240
// with StartSlack 0 and a critical child), so the recompute mode is the measured
// one and `critical_any_child` is kept only for the record.
#include "rollup.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <utility>

namespace core::sched {
namespace {

bool is_set_date(date32 value) noexcept { return !is_na(value) && !is_overflow(value); }

// Signed working-time difference, the same convention cpm.cpp uses for slack.
ticks work_delta(const Calendar& calendar, date32 from, date32 to) {
    if (!is_set_date(from) || !is_set_date(to)) {
        return 0;
    }
    if (to >= from) {
        return calendar.work_time_between(from, to);
    }
    return -calendar.work_time_between(to, from);
}

double percent_of(ticks actual_duration, ticks duration, bool& defined) noexcept {
    if (duration <= 0) {
        defined = false;
        return 0.0;
    }
    defined = true;
    return static_cast<double>(actual_duration) / static_cast<double>(duration);
}

RollupResult make_error(RollupError error, std::string message) {
    RollupResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

RollupTaskResult rollup_leaf(const RollupTask& task) {
    RollupTaskResult result;
    result.id = task.id;
    result.parent = task.parent;
    result.summary = false;
    result.blank = !is_set_date(task.start) || !is_set_date(task.finish);
    result.start = task.start;
    result.finish = task.finish;
    result.late_start = task.late_start;
    result.late_finish = task.late_finish;
    result.duration = task.duration;
    result.work = task.work;
    result.actual_duration = task.actual_duration;
    result.actual_work = task.actual_work;
    result.percent_complete = percent_of(task.actual_duration, task.duration,
                                         result.percent_complete_defined);
    result.start_slack = task.start_slack;
    result.finish_slack = task.finish_slack;
    result.free_slack = task.free_slack;
    result.total_slack = task.total_slack;
    result.total_slack_inherited = task.total_slack;
    result.total_slack_recomputed = task.total_slack;
    result.recomputed_defined = true;
    result.critical = task.critical;
    result.critical_any_child = task.critical;
    result.critical_recomputed = task.critical;
    return result;
}

}  // namespace

const char* to_string(SummaryCriticalMode mode) noexcept {
    switch (mode) {
        case SummaryCriticalMode::inherit:
            return "inherit";
        case SummaryCriticalMode::recompute:
            return "recompute";
    }
    return "unknown summary critical mode";
}

const char* to_string(RollupError error) noexcept {
    switch (error) {
        case RollupError::none:
            return "none";
        case RollupError::no_tasks:
            return "no tasks";
        case RollupError::zero_id:
            return "zero task id";
        case RollupError::duplicate_id:
            return "duplicate task id";
        case RollupError::unknown_parent:
            return "unknown parent";
        case RollupError::self_parent:
            return "self parent";
        case RollupError::cycle:
            return "parent cycle";
        case RollupError::overflow:
            return "calendar overflow";
        case RollupError::invalid_option:
            return "invalid option";
    }
    return "unknown error";
}

RollupTask rollup_task(std::int32_t parent, const CpmTaskResult& cpm, ticks duration, ticks work,
                       ticks actual_duration, ticks actual_work) {
    RollupTask task;
    task.id = cpm.id;
    task.parent = parent;
    task.start = cpm.start;
    task.finish = cpm.finish;
    task.late_start = cpm.late_start;
    task.late_finish = cpm.late_finish;
    task.duration = duration;
    task.work = work;
    task.actual_duration = actual_duration;
    task.actual_work = actual_work;
    task.start_slack = cpm.start_slack;
    task.finish_slack = cpm.finish_slack;
    task.free_slack = cpm.free_slack;
    task.total_slack = cpm.total_slack;
    task.critical = cpm.critical;
    return task;
}

RollupResult rollup_tasks(const std::vector<RollupTask>& tasks, const Calendar& project_calendar,
                          const RollupOptions& options) {
    if (tasks.empty()) {
        return make_error(RollupError::no_tasks, "rollup_tasks: the task list is empty");
    }
    if (options.critical_slack_limit_days < 0) {
        return make_error(RollupError::invalid_option,
                          "rollup_tasks: CriticalSlackLimit is negative");
    }

    const std::size_t count = tasks.size();
    std::unordered_map<std::int32_t, std::size_t> index_of;
    index_of.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        if (tasks[i].id == kNoParent) {
            return make_error(RollupError::zero_id,
                              "rollup_tasks: task ids are 1-based; id 0 is the parent sentinel");
        }
        if (!index_of.emplace(tasks[i].id, i).second) {
            return make_error(RollupError::duplicate_id,
                              "rollup_tasks: duplicate task id " + std::to_string(tasks[i].id));
        }
    }

    std::vector<std::vector<std::size_t>> children(count);
    std::vector<std::size_t> remaining(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const std::int32_t parent = tasks[i].parent;
        if (parent == kNoParent) {
            continue;
        }
        if (parent == tasks[i].id) {
            return make_error(RollupError::self_parent,
                              "rollup_tasks: task " + std::to_string(tasks[i].id) +
                                  " is its own parent");
        }
        const auto found = index_of.find(parent);
        if (found == index_of.end()) {
            return make_error(RollupError::unknown_parent,
                              "rollup_tasks: task " + std::to_string(tasks[i].id) +
                                  " names unknown parent " + std::to_string(parent));
        }
        children[found->second].push_back(i);
        ++remaining[found->second];
    }

    const ticks critical_limit = static_cast<ticks>(options.critical_slack_limit_days) *
                                 options.duration.minutes_per_day() * kTicksPerMinute;

    std::vector<RollupTaskResult> computed(count);
    const auto compute_summary = [&](std::size_t index) {
        const std::vector<std::size_t>& kids = children[index];
        RollupTaskResult result;
        result.id = tasks[index].id;
        result.parent = tasks[index].parent;
        result.summary = true;
        result.child_count = kids.size();
        // A summary has no successors, so its FreeSlack equals its TotalSlack,
        // exactly like a leaf with no outgoing link (cpm.cpp).  MEASURED on the
        // 16 PROJECT_A summaries: rows +74 and +190 are equal on every one (e.g. uid
        // 8141 both 6240); W3-O6's comparison (FreeSlack 0, TotalSlack 0) agrees.
        // Assigned after the total is selected, below.

        bool any_non_blank = false;
        date32 start = kDateNa;
        date32 finish = kDateNa;
        date32 late_start = kDateNa;
        date32 late_finish = kDateNa;
        bool have_late_start = false;
        bool have_late_finish = false;
        bool have_inherited = false;
        ticks inherited_slack = 0;
        bool any_critical = false;
        ticks sum_work = 0;
        ticks sum_actual_work = 0;
        ticks sum_duration = 0;
        ticks sum_actual_duration = 0;

        for (const std::size_t child_index : kids) {
            const RollupTaskResult& child = computed[child_index];
            sum_work += child.work;
            sum_actual_work += child.actual_work;
            sum_duration += child.duration;
            sum_actual_duration += child.actual_duration;
            if (child.blank) {
                ++result.blank_child_count;
                continue;
            }
            any_non_blank = true;
            if (is_set_date(child.start)) {
                start = is_set_date(start) ? std::min(start, child.start) : child.start;
            }
            if (is_set_date(child.finish)) {
                finish = is_set_date(finish) ? std::max(finish, child.finish) : child.finish;
            }
            if (is_set_date(child.late_start)) {
                late_start =
                    have_late_start ? std::min(late_start, child.late_start) : child.late_start;
                have_late_start = true;
            }
            if (is_set_date(child.late_finish)) {
                late_finish =
                    have_late_finish ? std::max(late_finish, child.late_finish) : child.late_finish;
                have_late_finish = true;
            }
            if (!have_inherited || child.total_slack < inherited_slack) {
                inherited_slack = child.total_slack;
                have_inherited = true;
            }
            any_critical = any_critical || child.critical;
        }

        result.work = sum_work;
        result.actual_duration = sum_actual_duration;
        result.actual_work = sum_actual_work;
        result.percent_complete =
            percent_of(sum_actual_duration, sum_duration, result.percent_complete_defined);

        if (!any_non_blank) {
            // §3.7: a summary whose children are all blank stays blank.
            result.blank = true;
            return result;
        }

        result.start = start;
        result.finish = finish;
        result.late_start = have_late_start ? late_start : kDateNa;
        result.late_finish = have_late_finish ? late_finish : kDateNa;
        result.duration = project_calendar.work_time_between(start, finish);
        // MEASURED RULE (W3-P7 settled by corpus/probe/project_a-recalc.mpp): a
        // summary's StartSlack is LS - ES of the rolled-up late dates (uid 10798
        // stores 100800 while its TotalSlack is 38400), but its TotalSlack is the
        // rolled-up FINISH slack, LF - EF -- NOT min(LS-ES, LF-EF):
        //   uid 8141  StartSlack 0, LF-EF 6240, +74/+190 6240, Critical false
        //   uid 10720 StartSlack 0, LF-EF 10080, +190 10080, Critical false
        //   uid 10761 StartSlack 0, LF-EF 28320, +190 28320, Critical false
        // W3-O6 (StartSlack 1920, LF-EF 0, TotalSlack 0) and outline1's summary
        // (StartSlack 1920, LF-EF 0, TotalSlack 0) agree with the finish side.
        // `total_slack_inherited` keeps the other candidate (min child TotalSlack)
        // for the record; it is refuted by 8141, whose children are critical.
        result.start_slack = work_delta(project_calendar, start, result.late_start);
        result.finish_slack = work_delta(project_calendar, finish, result.late_finish);
        result.recomputed_defined = have_late_start && have_late_finish;
        result.total_slack_recomputed =
            result.recomputed_defined ? result.finish_slack : 0;
        result.total_slack_inherited = have_inherited ? inherited_slack : 0;
        result.critical_any_child = any_critical;
        result.critical_recomputed =
            result.recomputed_defined && result.total_slack_recomputed <= critical_limit;

        if (options.critical_mode == SummaryCriticalMode::inherit || !result.recomputed_defined) {
            result.total_slack = result.total_slack_inherited;
            result.critical = result.critical_any_child;
        } else {
            result.total_slack = result.total_slack_recomputed;
            result.critical = result.critical_recomputed;
        }
        result.free_slack = result.total_slack;
        return result;
    };

    // Bottom-up, deterministic: process leaves in id order; a summary becomes
    // ready when its last child has been computed.
    const auto greater_id = [&tasks](std::size_t a, std::size_t b) {
        return tasks[a].id > tasks[b].id;
    };
    std::priority_queue<std::size_t, std::vector<std::size_t>, decltype(greater_id)> ready(
        greater_id);
    for (std::size_t i = 0; i < count; ++i) {
        if (remaining[i] == 0) {
            ready.push(i);
        }
    }
    std::size_t processed = 0;
    while (!ready.empty()) {
        const std::size_t index = ready.top();
        ready.pop();
        computed[index] = children[index].empty() ? rollup_leaf(tasks[index])
                                                  : compute_summary(index);
        ++processed;
        if (tasks[index].parent != kNoParent) {
            const std::size_t parent_index = index_of.at(tasks[index].parent);
            if (--remaining[parent_index] == 0) {
                ready.push(parent_index);
            }
        }
    }

    if (processed != count) {
        RollupResult cycle =
            make_error(RollupError::cycle,
                       "rollup_tasks: the parent ids form a cycle; no tree is claimed");
        for (std::size_t i = 0; i < count; ++i) {
            if (remaining[i] != 0) {
                cycle.cycle_tasks.push_back(tasks[i].id);
            }
        }
        std::sort(cycle.cycle_tasks.begin(), cycle.cycle_tasks.end());
        return cycle;
    }

    RollupResult result;
    result.tasks.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        result.tasks.push_back(computed[i]);
    }
    return result;
}

}  // namespace core::sched
