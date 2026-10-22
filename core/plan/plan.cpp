// core/plan/plan.cpp -- reader -> CPM mapping, the recalculation call and the
// summary rollup that follows it.
#include "plan.h"

#include "cal.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core::plan {
namespace {

using core::sched::ConstraintType;
using core::sched::date32;
using core::sched::is_na;
using core::sched::kDateNa;
using core::sched::LinkType;
using core::sched::ticks;

// The reader's u32 UniqueIDs are small and positive; the engine keys tasks by
// int32.  A uid above INT32_MAX cannot be represented and is skipped rather
// than silently wrapped (no corpus file comes close).  uid 0 is the project row
// and never a task.
bool fits_int32(std::uint32_t value) {
    return value <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
}

// PjConstraint 0..7 is the same enum as core::sched::ConstraintType (spec §0.4).
// A value outside that range cannot be interpreted and becomes ASAP.
constexpr std::uint16_t kMaxConstraint = 7;

// Which constraints carry a date; a date-bearing constraint without one would
// make schedule_tasks() return constraint_date_missing for the whole file.
bool constraint_needs_date(ConstraintType type) noexcept {
    switch (type) {
        case ConstraintType::must_start_on:
        case ConstraintType::must_finish_on:
        case ConstraintType::start_no_earlier_than:
        case ConstraintType::start_no_later_than:
        case ConstraintType::finish_no_earlier_than:
        case ConstraintType::finish_no_later_than:
            return true;
        case ConstraintType::as_soon_as_possible:
        case ConstraintType::as_late_as_possible:
            return false;
    }
    return false;
}

// "1.2.3" -> "1.2"; "" when the task is top level.  This is the only parent
// information the reader exposes (Task::outline_number, derived from the stored
// Fixed2Data parent links; see reader.h "OUTLINE").
std::string parent_outline_number(const std::string& number) {
    const std::size_t dot = number.rfind('.');
    if (dot == std::string::npos || dot == 0) {
        return std::string();
    }
    return number.substr(0, dot);
}

core::sched::CpmError map_rollup_error(core::sched::RollupError error) noexcept {
    using core::sched::RollupError;
    switch (error) {
        case RollupError::none:
            return core::sched::CpmError::none;
        case RollupError::no_tasks:
            return core::sched::CpmError::no_tasks;
        case RollupError::cycle:
            return core::sched::CpmError::cycle;
        case RollupError::overflow:
            return core::sched::CpmError::overflow;
        case RollupError::zero_id:
        case RollupError::duplicate_id:
        case RollupError::unknown_parent:
        case RollupError::self_parent:
        case RollupError::invalid_option:
            return core::sched::CpmError::invalid_option;
    }
    return core::sched::CpmError::invalid_option;
}

}  // namespace

core::sched::Calendar standard_calendar() {
    // 08:00-12:00 + 13:00-17:00, Monday through Friday, Sunday/Saturday free.
    const core::sched::DayPattern day =
        core::sched::day_pattern({{8 * 60, 12 * 60}, {13 * 60, 17 * 60}});
    return core::sched::Calendar(core::sched::make_week(
        {{1, day}, {2, day}, {3, day}, {4, day}, {5, day}}));
}

PlanInput from_mpp(const mpp::Document& document) {
    PlanInput input;
    input.calendar = standard_calendar();

    input.notes.emplace_back(
        "calendar: from_mpp defaults to the stock Mon-Fri 08:00-12:00/13:00-17:00 "
        "week; a caller with the path replaces PlanInput::calendar with "
        "core::cal::load_project_calendar (tools/recalc and the plan tests do)");

    if (document.project.start.valid()) {
        input.project_start = document.project.start.raw;
    } else {
        input.project_start = kDateNa;
        input.notes.emplace_back(
            "project row 0 has no valid Start; recalculate() will report "
            "missing_project_start");
    }

    // ---- tasks ------------------------------------------------------------
    input.tasks.reserve(document.tasks.size());
    std::vector<std::uint32_t> known_uids;
    known_uids.reserve(document.tasks.size());
    // The outline number and the reader's own Summary flag, in PlanInput order:
    // rows that do not fit int32 are skipped, so document.tasks must never be
    // indexed with a PlanInput index.
    std::vector<std::string> outline_numbers;
    outline_numbers.reserve(document.tasks.size());
    std::vector<char> reader_summary;
    reader_summary.reserve(document.tasks.size());
    // outline number -> PlanInput index, for the parent chain.  Two live rows
    // sharing a number would make parenthood ambiguous; the first wins and the
    // collision is counted.
    std::unordered_map<std::string, std::size_t> index_by_outline;
    // uid -> PlanInput index, for the child counts and the link filter.
    std::unordered_map<std::int32_t, std::size_t> index_by_uid;
    std::size_t outline_collisions = 0;
    std::size_t constraint_rows = 0;
    std::size_t constraint_date_rows = 0;
    std::size_t deadline_rows = 0;
    std::size_t constraint_downgraded = 0;
    std::size_t unknown_constraint = 0;
    std::size_t stored_actuals = 0;
    std::size_t derived_started = 0;
    std::size_t derived_finished = 0;
    std::size_t elapsed_duration_rows = 0;

    for (const mpp::Task& task : document.tasks) {
        if (!fits_int32(task.uid)) {
            input.notes.emplace_back("task uid " + std::to_string(task.uid) +
                                     " does not fit int32 and was skipped");
            continue;
        }
        PlanTask mapped;
        mapped.uid = static_cast<std::int32_t>(task.uid);
        // Both sides count tenths of a minute (4800 ticks = one 8 h day).
        mapped.duration = static_cast<ticks>(task.duration_ticks);
        // +178 task calendar and +0xa4 DurationFormat: MEASURED fields (see
        // reader.h).  attach_task_calendars() turns the uid into a Calendar;
        // an elapsed duration makes the engine count wall-clock time.
        mapped.calendar_uid = task.calendar_uid;
        mapped.elapsed_duration = task.duration_is_elapsed();
        if (mapped.elapsed_duration) {
            ++elapsed_duration_rows;
        }

        // ---- constraints, deadline (reader fields; see reader.h) ----------
        ConstraintType constraint = ConstraintType::as_soon_as_possible;
        if (task.constraint_type <= kMaxConstraint) {
            constraint = static_cast<ConstraintType>(task.constraint_type);
        } else {
            ++unknown_constraint;
        }
        const date32 constraint_date =
            task.constraint_date.valid() ? task.constraint_date.raw : kDateNa;
        if (task.constraint_type != 0) {
            ++constraint_rows;
        }
        if (constraint_needs_date(constraint)) {
            if (!is_na(constraint_date)) {
                ++constraint_date_rows;
            } else {
                // Downgrade rather than hand schedule_tasks a constraint it must
                // reject (it errors the whole file on a missing constraint date).
                constraint = ConstraintType::as_soon_as_possible;
                ++constraint_downgraded;
            }
        }
        mapped.constraint = constraint;
        mapped.constraint_date = constraint_date;
        if (task.deadline.valid()) {
            mapped.deadline = task.deadline.raw;
            ++deadline_rows;
        }

        // ---- actuals -------------------------------------------------------
        // Prefer the reader's own ActualStart/ActualFinish (W19).  While those
        // are still default, derive them from PercentComplete and the stored
        // dates: MS Project's Start column on a started task is its actual
        // start, and a 100 % task's stored Finish is its actual finish
        // (spec/scheduling.md §2.4).  Both paths are counted.
        const bool reader_actual_start = task.actual_start.valid();
        const bool reader_actual_finish = task.actual_finish.valid();
        const std::uint16_t percent = std::min<std::uint16_t>(task.percent_complete, 100);
        if (reader_actual_start) {
            mapped.actual_start = task.actual_start.raw;
        } else if (percent > 0 && task.start.valid()) {
            mapped.actual_start = task.start.raw;
            ++derived_started;
        }
        if (reader_actual_finish) {
            mapped.actual_finish = task.actual_finish.raw;
        } else if (percent >= 100 && task.finish.valid()) {
            mapped.actual_finish = task.finish.raw;
            ++derived_finished;
        }
        if (!is_na(mapped.actual_start)) {
            mapped.actuals_from_reader = reader_actual_start || reader_actual_finish;
            if (task.actual_duration_ticks > 0) {
                mapped.actual_duration = static_cast<ticks>(task.actual_duration_ticks);
            } else {
                mapped.actual_duration =
                    static_cast<ticks>((static_cast<std::uint64_t>(mapped.duration) * percent) /
                                       100u);
            }
            if (mapped.actuals_from_reader) {
                ++stored_actuals;
            }
        }

        const std::size_t index = input.tasks.size();
        if (!task.outline_number.empty()) {
            const auto inserted = index_by_outline.emplace(task.outline_number, index);
            if (!inserted.second) {
                ++outline_collisions;
            }
        }
        index_by_uid.emplace(mapped.uid, index);
        input.tasks.push_back(mapped);
        outline_numbers.push_back(task.outline_number);
        reader_summary.push_back(task.summary ? 1 : 0);
        known_uids.push_back(task.uid);
    }

    // ---- parent chain from the outline numbers ----------------------------
    std::size_t parented = 0;
    std::size_t unknown_parents = 0;
    std::size_t outline_summaries = 0;
    std::size_t reader_summary_disagreements = 0;
    for (std::size_t index = 0; index < input.tasks.size(); ++index) {
        const std::string parent_number = parent_outline_number(outline_numbers[index]);
        if (parent_number.empty()) {
            continue;
        }
        const auto found = index_by_outline.find(parent_number);
        if (found == index_by_outline.end() || found->second == index) {
            ++unknown_parents;
            continue;
        }
        input.tasks[index].parent = input.tasks[found->second].uid;
        ++parented;
        if (!input.tasks[found->second].summary) {
            input.tasks[found->second].summary = true;
            ++outline_summaries;
        }
    }
    for (std::size_t index = 0; index < input.tasks.size(); ++index) {
        if ((reader_summary[index] != 0) != input.tasks[index].summary) {
            ++reader_summary_disagreements;
        }
    }
    input.summaries = outline_summaries;

    input.notes.emplace_back(
        "outline: " + std::to_string(parented) + " of " +
        std::to_string(input.tasks.size()) +
        " tasks have a parent from Task::outline_number, so " +
        std::to_string(outline_summaries) +
        " are summaries; a summary is rolled up after the CPM passes and is not "
        "scheduled itself" +
        (unknown_parents == 0
             ? std::string()
             : " (" + std::to_string(unknown_parents) +
                   " rows name a parent that is not a live task and are treated as "
                   "top level)") +
        (outline_collisions == 0
             ? std::string()
             : " (" + std::to_string(outline_collisions) + " outline numbers collided)"));
    if (reader_summary_disagreements != 0) {
        input.notes.emplace_back(
            "outline: " + std::to_string(reader_summary_disagreements) +
            " rows disagree with the reader's own Summary/child_count derivation");
    }

    input.notes.emplace_back(
        "duration format: " + std::to_string(elapsed_duration_rows) + " of " +
        std::to_string(input.tasks.size()) +
        " task rows store an elapsed DurationFormat (PjFormatUnit 8 etc); the engine "
        "schedules those as wall-clock time (MEASURED, wave 11)");
    input.notes.emplace_back(
        "constraints: " + std::to_string(constraint_rows) + " of " +
        std::to_string(input.tasks.size()) + " tasks carry a stored constraint, " +
        std::to_string(constraint_date_rows) + " with a constraint date, " +
        std::to_string(deadline_rows) + " deadlines" +
        (constraint_downgraded == 0
             ? std::string()
             : "; " + std::to_string(constraint_downgraded) +
                   " date-bearing constraints had no date and became ASAP") +
        (unknown_constraint == 0
             ? std::string()
             : "; " + std::to_string(unknown_constraint) +
                   " rows had a constraint type outside 0..7"));
    input.notes.emplace_back(
        "actuals: " + std::to_string(stored_actuals) +
        " tasks use mpp::Task::actual_start/actual_finish; " +
        std::to_string(derived_started) + " started and " +
        std::to_string(derived_finished) +
        " completed tasks derive them from PercentComplete > 0 and the stored dates "
        "(INFERENCE, spec/scheduling.md §2.4); a started task keeps its start and a "
        "completed task its finish, and their successors are driven from those dates");

    // ---- links ------------------------------------------------------------
    std::sort(known_uids.begin(), known_uids.end());
    const auto is_known = [&known_uids](std::uint32_t uid) {
        return std::binary_search(known_uids.begin(), known_uids.end(), uid);
    };
    const auto is_summary_uid = [&index_by_uid, &input](std::uint32_t uid) {
        const auto found = index_by_uid.find(static_cast<std::int32_t>(uid));
        return found != index_by_uid.end() && input.tasks[found->second].summary;
    };

    input.links.reserve(document.links.size());
    for (const mpp::Link& link : document.links) {
        // The reader only returns links whose FixedMeta entry names a row (W24),
        // so on current files this guard is defensive.  A hand-built or damaged
        // document can still name a deleted task; such a link cannot be
        // scheduled, so count it instead of dropping it silently.
        if (!link.predecessor_known || !link.successor_known ||
            !is_known(link.predecessor_uid) || !is_known(link.successor_uid)) {
            ++input.skipped_links;
            continue;
        }
        if (link.predecessor_uid == link.successor_uid) {
            ++input.skipped_links;
            continue;
        }
        if (!fits_int32(link.predecessor_uid) || !fits_int32(link.successor_uid)) {
            ++input.skipped_links;
            continue;
        }
        // A summary is not in the CPM graph, so a link that touches one cannot
        // be scheduled.  PROJECT_A has none; a file that does needs an oracle decision.
        if (is_summary_uid(link.predecessor_uid) || is_summary_uid(link.successor_uid)) {
            ++input.summary_links;
            continue;
        }
        PlanLink mapped;
        mapped.predecessor = static_cast<std::int32_t>(link.predecessor_uid);
        mapped.successor = static_cast<std::int32_t>(link.successor_uid);
        // PjTaskLinkType: 0 FF, 1 FS, 2 SF, 3 SS on both sides (spec §0.4).
        mapped.type = static_cast<LinkType>(static_cast<unsigned>(link.type_raw) & 3u);
        // The reader's lag is signed 6-second ticks -- the engine's unit exactly.
        mapped.lag = static_cast<ticks>(link.lag_ticks);
        // +18 is LagFormat: 7 = working days, 8 = elapsed (MEASURED wave 9b).
        mapped.elapsed_lag = link.lag_is_elapsed();
        input.links.push_back(mapped);
    }

    input.notes.emplace_back("mapped " + std::to_string(input.tasks.size()) +
                             " tasks and " + std::to_string(input.links.size()) +
                             " links; " + std::to_string(input.skipped_links) +
                             " links skipped (deleted/unknown endpoint or self link), " +
                             std::to_string(input.summary_links) +
                             " links skipped (summary endpoint)");
    return input;
}

void attach_task_calendars(PlanInput& input, core::cal::CalendarTable const& table) {
    std::size_t named_rows = 0;
    std::size_t built = 0;
    std::size_t missing = 0;
    std::size_t failed = 0;
    for (const PlanTask& task : input.tasks) {
        if (task.calendar_uid <= 0) {
            continue;
        }
        ++named_rows;
        const bool already_built =
            std::any_of(input.task_calendars.begin(), input.task_calendars.end(),
                        [&task](const TaskCalendar& entry) {
                            return entry.uid == task.calendar_uid;
                        });
        if (already_built) {
            continue;
        }
        const core::cal::CalendarDefinition* definition =
            core::cal::find_calendar_by_uid(table, task.calendar_uid);
        if (definition == nullptr) {
            ++missing;
            continue;
        }
        try {
            TaskCalendar entry;
            entry.uid = task.calendar_uid;
            entry.calendar = core::cal::build_calendar(table, *definition);
            input.task_calendars.push_back(std::move(entry));
            ++built;
        } catch (const std::exception&) {
            ++failed;
        }
    }
    input.notes.emplace_back(
        "task calendars: " + std::to_string(named_rows) +
        " task rows name a +178 calendar uid; " + std::to_string(built) +
        " distinct calendars built; " + std::to_string(missing) +
        " uids are absent from 114/TBkndCal and " + std::to_string(failed) +
        " failed to build (those rows use the project calendar)");
}

PlanResult recalculate(const PlanInput& input) {
    PlanResult result;
    result.project_start = input.project_start;
    result.skipped_links = input.skipped_links;
    result.summary_links = input.summary_links;

    const std::size_t count = input.tasks.size();
    if (count == 0) {
        result.error = core::sched::CpmError::no_tasks;
        result.message = "recalculate: the task list is empty";
        return result;
    }

    // ---- validate ids and resolve the tree -------------------------------
    std::unordered_map<std::int32_t, std::size_t> index_of;
    index_of.reserve(count * 2);
    for (std::size_t index = 0; index < count; ++index) {
        const std::int32_t uid = input.tasks[index].uid;
        if (uid == 0) {
            result.error = core::sched::CpmError::invalid_option;
            result.message = "recalculate: task uid 0 is the project row, not a task";
            return result;
        }
        if (!index_of.emplace(uid, index).second) {
            result.error = core::sched::CpmError::duplicate_task_id;
            result.message = "recalculate: duplicate task uid " + std::to_string(uid);
            return result;
        }
    }

    std::vector<char> is_summary(count, 0);
    for (std::size_t index = 0; index < count; ++index) {
        const std::int32_t parent = input.tasks[index].parent;
        if (parent == 0) {
            continue;
        }
        if (parent == input.tasks[index].uid) {
            result.error = core::sched::CpmError::invalid_option;
            result.message =
                "recalculate: task " + std::to_string(parent) + " is its own parent";
            return result;
        }
        const auto found = index_of.find(parent);
        if (found == index_of.end()) {
            result.error = core::sched::CpmError::invalid_option;
            result.message = "recalculate: task " + std::to_string(input.tasks[index].uid) +
                             " names unknown parent " + std::to_string(parent);
            return result;
        }
        if (is_summary[found->second] == 0) {
            is_summary[found->second] = 1;
            ++result.summaries;
        }
    }

    // Task calendars, from attach_task_calendars().  The entries live in the
    // caller-owned PlanInput, so the pointers stay valid for this call.
    std::unordered_map<std::int32_t, const core::sched::Calendar*> calendar_by_uid;
    calendar_by_uid.reserve(input.task_calendars.size() * 2);
    for (const TaskCalendar& entry : input.task_calendars) {
        calendar_by_uid.emplace(entry.uid, &entry.calendar);
    }

    // ---- CPM on the leaves ------------------------------------------------
    std::vector<core::sched::CpmTask> cpm_tasks;
    std::vector<std::size_t> cpm_input_index;
    cpm_tasks.reserve(count - result.summaries);
    cpm_input_index.reserve(count - result.summaries);
    for (std::size_t index = 0; index < count; ++index) {
        if (is_summary[index] != 0) {
            continue;
        }
        const PlanTask& task = input.tasks[index];
        core::sched::CpmTask mapped;
        mapped.id = task.uid;
        mapped.duration = task.duration;
        mapped.elapsed_duration = task.elapsed_duration;
        mapped.calendar = nullptr;  // task.calendar_uid == -1: project calendar
        if (task.calendar_uid > 0) {
            const auto found = calendar_by_uid.find(task.calendar_uid);
            if (found != calendar_by_uid.end()) {
                mapped.calendar = found->second;
            }
        }
        mapped.constraint = task.constraint;
        mapped.constraint_date = task.constraint_date;
        mapped.deadline = task.deadline;
        // ACTUALS (§2.4).  A started task's start is a fact: pin it rather than
        // letting a link move it (a violated link then shows as negative slack,
        // which is what Project reports).  A completed task's finish is a fact
        // too, so pin that instead and drive its successors from the real finish.
        //
        // MEASURED (Project's own save corpus/probe/project_a-recalc.mpp, uids
        // 10849/10851): an in-progress task stores StartSlack 0 (row +186) but
        // TotalSlack 16320 / 39360 min (row +190) = LateFinish - EarlyFinish.
        // Its LATE START is the actual start while its LATE FINISH still comes
        // from the successors, so it must NOT be encoded as MSO: an MSO pin
        // forces late = early and reported TotalSlack 0 for those rows.  The
        // engine's CpmTask::actual_start is exactly this pin-the-start-only rule.
        if (!is_na(task.actual_start)) {
            if (!is_na(task.actual_finish)) {
                mapped.constraint = ConstraintType::must_finish_on;
                mapped.constraint_date = task.actual_finish;
                mapped.duration =
                    task.actual_duration > 0 ? task.actual_duration : task.duration;
                // Mark the row complete: a link INTO a complete task does not
                // bound its predecessors' late dates or free slack (measured,
                // PROJECT_A uid 10851 -> completed 10757; see CpmTask::actual_finish).
                mapped.actual_finish = task.actual_finish;
            } else {
                mapped.actual_start = task.actual_start;
            }
        }
        cpm_tasks.push_back(mapped);
        cpm_input_index.push_back(index);
    }
    if (cpm_tasks.empty()) {
        result.error = core::sched::CpmError::no_tasks;
        result.message = "recalculate: every task is a summary, so the CPM graph has no leaves";
        return result;
    }

    std::vector<core::sched::CpmLink> links;
    links.reserve(input.links.size());
    // Which rows a schedulable link feeds.  W28 attribution reads this through
    // PlanTaskResult::has_predecessor; the CPM engine does not.
    std::vector<char> has_predecessor(count, 0);
    for (const PlanLink& link : input.links) {
        const auto pred = index_of.find(link.predecessor);
        const auto succ = index_of.find(link.successor);
        if (pred == index_of.end() || succ == index_of.end()) {
            result.error = core::sched::CpmError::unknown_task;
            result.message = "recalculate: link " + std::to_string(link.predecessor) + "->" +
                             std::to_string(link.successor) + " names an unknown task";
            return result;
        }
        if (is_summary[pred->second] != 0 || is_summary[succ->second] != 0) {
            // from_mpp already drops these; a hand-built input may not.
            ++result.summary_links;
            continue;
        }
        core::sched::CpmLink mapped;
        mapped.predecessor = link.predecessor;
        mapped.successor = link.successor;
        mapped.type = link.type;
        mapped.lag = link.lag;
        mapped.elapsed_lag = link.elapsed_lag;
        links.push_back(mapped);
        has_predecessor[succ->second] = 1;
    }

    core::sched::CpmOptions options;
    options.project_start = input.project_start;
    options.schedule_from_start = true;
    options.critical_slack_limit_days = input.critical_slack_limit_days;
    options.duration = input.duration;

    const core::sched::CpmResult scheduled =
        core::sched::schedule_tasks(cpm_tasks, links, input.calendar, options);
    result.error = scheduled.error;
    result.message = scheduled.message;
    result.project_start = scheduled.project_start;
    result.project_finish = scheduled.project_finish;
    if (!scheduled.ok()) {
        return result;
    }

    // ---- summary rollup (§3.7) -------------------------------------------
    std::vector<const core::sched::CpmTaskResult*> leaf_result(count, nullptr);
    for (std::size_t i = 0; i < cpm_tasks.size(); ++i) {
        leaf_result[cpm_input_index[i]] = &scheduled.tasks[i];
    }

    std::vector<core::sched::RollupTask> tree;
    tree.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const PlanTask& task = input.tasks[index];
        core::sched::RollupTask node;
        node.id = task.uid;
        node.parent = task.parent;
        if (is_summary[index] == 0) {
            node = core::sched::rollup_task(task.parent, *leaf_result[index], task.duration,
                                            /*work*/ 0, task.actual_duration,
                                            /*actual_work*/ 0);
            // Keep the actual dates: the CPM pin puts the start at the same
            // instant for MSO, but MFO derives it from the finish, and the
            // displayed value must be the actual one either way.
            if (!is_na(task.actual_start)) {
                node.start = task.actual_start;
            }
            if (!is_na(task.actual_finish)) {
                node.finish = task.actual_finish;
            }
        }
        tree.push_back(node);
    }

    core::sched::RollupOptions rollup_options;
    rollup_options.critical_slack_limit_days = input.critical_slack_limit_days;
    rollup_options.duration = input.duration;
    rollup_options.critical_mode = input.summary_critical_mode;
    const core::sched::RollupResult rolled =
        core::sched::rollup_tasks(tree, input.calendar, rollup_options);
    if (!rolled.ok()) {
        result.error = map_rollup_error(rolled.error);
        result.message = "recalculate: summary rollup failed: " + rolled.message;
        result.tasks.clear();
        return result;
    }

    // ---- results, in input order -----------------------------------------
    result.tasks.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const PlanTask& task = input.tasks[index];
        const core::sched::RollupTaskResult& row = rolled.tasks[index];
        PlanTaskResult out;
        out.uid = task.uid;
        out.summary = is_summary[index] != 0;
        out.parent = task.parent;
        out.duration = row.duration;
        out.calendar_uid = task.calendar_uid;
        out.elapsed_duration = task.elapsed_duration;
        out.start = row.start;
        out.finish = row.finish;
        out.late_start = row.late_start;
        out.late_finish = row.late_finish;
        // W28 attribution inputs.  Measurement only: has_date_constraint records
        // a stored date-bearing constraint/deadline (a downgraded constraint has
        // no date and is not one), has_predecessor the mapped graph above.
        out.has_date_constraint =
            (constraint_needs_date(task.constraint) && !is_na(task.constraint_date)) ||
            !is_na(task.deadline);
        out.has_predecessor = has_predecessor[index] != 0;
        if (!out.summary) {
            const core::sched::CpmTaskResult& cpm = *leaf_result[index];
            // A leaf's displayed dates are the CPM dates, except that an actual
            // date is never overwritten by the recalculation (§2.4).
            out.start = is_na(task.actual_start) ? cpm.start : task.actual_start;
            out.finish = is_na(task.actual_finish) ? cpm.finish : task.actual_finish;
            out.early_start = cpm.early_start;
            out.early_finish = cpm.early_finish;
            out.late_start = cpm.late_start;
            out.late_finish = cpm.late_finish;
            out.start_slack = cpm.start_slack;
            out.finish_slack = cpm.finish_slack;
            out.free_slack = cpm.free_slack;
            out.total_slack = cpm.total_slack;
            out.critical = cpm.critical;
            out.constraint = cpm.constraint;
            out.start_is_actual = !is_na(task.actual_start);
            out.finish_is_actual = !is_na(task.actual_finish);
        } else {
            // A summary has no constraint of its own (§3.7) and no free slack.
            out.early_start = row.start;
            out.early_finish = row.finish;
            out.start_slack = row.start_slack;
            out.finish_slack = row.finish_slack;
            out.free_slack = row.free_slack;
            out.total_slack = row.total_slack;
            out.critical = row.critical;
            out.constraint = ConstraintType::as_soon_as_possible;
        }
        // The project finish must include a summary's rolled-up finish and an
        // actual finish the forward pass did not see.
        if (!is_na(out.finish) &&
            (is_na(result.project_finish) || out.finish > result.project_finish)) {
            result.project_finish = out.finish;
        }
        result.tasks.push_back(out);
    }
    return result;
}

std::string format_date(core::sched::date32 date) {
    std::string text = core::sched::to_string(date);
    // Drop ":00" so whole-minute instants read like MS Project's own export
    // ("2026-01-05 08:00"); keep the seconds when they are not zero.
    if (text.size() > 3 && text.compare(text.size() - 3, 3, ":00") == 0) {
        text.resize(text.size() - 3);
    }
    return text;
}

std::string format_slack(core::sched::ticks ticks) {
    const double minutes = static_cast<double>(ticks) /
                           static_cast<double>(core::sched::kTenthsPerMinute);
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.1f", minutes);
    return std::string(buffer);
}

}  // namespace core::plan
