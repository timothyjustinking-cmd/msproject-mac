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

// ---- MANUAL SCHEDULING: the finish a manual row keeps ---------------------
// MEASURED RULE (plan.h "MANUAL SCHEDULING").  A manual row's stored Start is a
// user fact even when it falls in non-working time: the fixture's Start is
// 2026-01-04 13:00, a SUNDAY the project calendar does not work.  Project's
// stored Finish is Start + Duration counted as if the 13:00-17:00 tail of that
// afternoon were working (5 d -> Fri 01-09 12:00, 3 d -> 01-07 12:00,
// 2 d -> 01-06 12:00), which is why calendar-correct AddWork from the aligned
// Monday 08:00 gives the CALCULATED pair's 17:00 finishes instead.
//
// This helper returns the working ticks the start's own day contributes before
// the walk reaches a real working day: 0 when the start is a working instant,
// otherwise the standard-day working time between the start's CLOCK time and
// that day's end, read from the first day at or after the start with a pattern.
ticks manual_partial_day_charge(const core::sched::Calendar& calendar, date32 start) {
    if (is_na(start) || calendar.is_working_instant(start)) {
        return 0;
    }
    const std::int32_t clock = core::sched::time_of(start);
    const std::int32_t day = core::sched::day_of(start);
    for (std::int32_t offset = 0; offset < core::sched::kDaysPerWeek; ++offset) {
        const core::sched::DayPattern& pattern = calendar.pattern_for_day(day + offset);
        if (pattern.empty()) {
            continue;
        }
        ticks charge = 0;
        for (std::size_t i = 0; i < pattern.count; ++i) {
            const std::int32_t begin = pattern.start_ticks(i);
            const std::int32_t end = pattern.end_ticks(i);
            if (end <= clock) {
                continue;
            }
            charge += end - std::max(begin, clock);
        }
        return charge;
    }
    return 0;
}

// The Finish a manual row keeps: Start + Duration under the measured boundary
// rule above.  `duration == 0` finishes at the Start (a milestone).
date32 manual_finish_of(const core::sched::Calendar& calendar, date32 start,
                        ticks duration) {
    if (is_na(start)) {
        return kDateNa;
    }
    if (duration <= 0) {
        return start;
    }
    const ticks charge = manual_partial_day_charge(calendar, start);
    if (charge >= duration) {
        // [I] A duration shorter than the start day's tail has no oracle row;
        // keep the row at its start rather than walking into the calendar.
        return start;
    }
    return calendar.add_work(start, duration - charge);
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
    std::size_t manual_rows = 0;
    std::size_t manual_without_start = 0;

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

        // ---- manual scheduling (see MANUAL SCHEDULING in plan.h) -----------
        // The stored bit is the only driver: no caller flag, no name, no guess.
        mapped.manually_scheduled = task.manually_scheduled;
        if (task.start.valid()) {
            mapped.stored_start = task.start.raw;
        }
        if (task.finish.valid()) {
            mapped.stored_finish = task.finish.raw;
        }
        if (task.start_anchor.valid()) {
            mapped.start_anchor = task.start_anchor.raw;
        }
        if (task.manually_scheduled) {
            ++manual_rows;
            if (!task.start.valid()) {
                ++manual_without_start;
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
    input.notes.emplace_back(
        "manual scheduling: " + std::to_string(manual_rows) + " of " +
        std::to_string(input.tasks.size()) +
        " task rows store the manual bit (Fixed2Meta +8 0x80).  Such a row keeps its "
        "stored Start (+104) and the Finish its Duration implies, its own constraint "
        "does not move it, its slack is computed on those dates and it is never "
        "critical (MEASURED, docs/engine-parity.md); the CPM dates Project keeps at "
        "+96/+100 are reported separately as start/finish and the kept dates as "
        "display_start/display_finish" +
        (manual_without_start == 0
             ? std::string()
             : "; " + std::to_string(manual_without_start) +
                   " manual rows store no Start and fall back to the CPM path"));

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
        " uids are absent from " +
        (table.table_root.empty() ? std::string("TBkndCal")
                                  : table.table_root + "/TBkndCal") +
        " and " + std::to_string(failed) +
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
    // Manual rows the effective pass saw, pinned, and could not pin.  An unpinned
    // manual row (no stored Start, an elapsed duration or actuals) is scheduled
    // like an auto row, which must be visible in PlanResult rather than silently
    // assumed away.
    std::size_t manual_rows = 0;
    std::size_t manual_pinned = 0;
    std::size_t manual_pin_failures = 0;
    // Which input rows the effective pass pinned (indexed like input.tasks).  The
    // stored late pair's manual rule (MANUAL LATE PAIR, plan.h) applies only to a
    // pinned row: an unpinned manual row is scheduled like an auto row.
    std::vector<char> pinned_row(count, 0);
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
        // MANUAL SCHEDULING (plan.h): a stored-manual row is pinned to its stored
        // Start and to the Finish its Duration implies, and the engine ignores the
        // row's own constraint for it.  An elapsed duration has no measured manual
        // rule (the finish is wall-clock, the calendar is not walked), so such a
        // row keeps the CPM path and is counted; a row with actuals also keeps the
        // actuals path (an in-progress manual row has no oracle row).
        if (task.manually_scheduled) {
            ++manual_rows;
        }
        /* An EDIT-TIME link add moves a manual successor (PlanInput::
         * manual_rows_follow_links); a recalculation does not.  "Moves" means
         * the graph places the row for this pass, so it is not pinned. */
        const bool follow_links =
            input.manual_rows_follow_links && !is_na(task.stored_start) &&
            std::any_of(input.links.begin(), input.links.end(), [&task](const PlanLink& link) {
                return link.successor == task.uid;
            });
        if (task.manually_scheduled && !follow_links && !task.elapsed_duration &&
            is_na(task.actual_start) && is_na(task.actual_finish) &&
            !is_na(task.stored_start)) {
            mapped.manual_start = task.stored_start;
            mapped.manual_finish = manual_finish_of(
                mapped.calendar != nullptr ? *mapped.calendar : input.calendar,
                task.stored_start, task.duration);
            if (is_na(mapped.manual_finish)) {
                // add_work ran off the calendar domain; fall back rather than pin
                // an unset date (the engine would report overflow on a bad pin).
                mapped.manual_start = kDateNa;
                mapped.manual_finish = kDateNa;
                ++manual_pin_failures;
            } else {
                ++manual_pinned;
                pinned_row[index] = 1;
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

    // ---- the EFFECTIVE pass (manual rows pinned) -------------------------
    // This is the schedule the result's start/finish, late dates, slack and
    // project finish report.  For an auto-scheduled programme it is the only pass
    // and is exactly the pre-wave-18 behaviour.
    const core::sched::CpmResult scheduled =
        core::sched::schedule_tasks(cpm_tasks, links, input.calendar, options);
    result.error = scheduled.error;
    result.message = scheduled.message;
    result.project_start = scheduled.project_start;
    result.project_finish = scheduled.project_finish;
    if (!scheduled.ok()) {
        return result;
    }

    // ---- the CALCULATED pass (Project's +96/+100) ------------------------
    // MEASURED (plan.h "MANUAL SCHEDULING"): a manual row's +96/+100 pair is the
    // date a CPM pass WOULD place it at -- the row's own constraint included,
    // its stored Start ignored.  That is this second run with the pins cleared.
    // It is skipped when nothing is manual, so an auto-scheduled programme pays
    // nothing and cannot change (M2).  A failure here is a failure of the whole
    // recalculation: no partial schedule is claimed.
    bool have_calculated = false;
    core::sched::CpmResult calculated;
    if (manual_pinned > 0) {
        std::vector<core::sched::CpmTask> calculated_tasks = cpm_tasks;
        for (std::size_t i = 0; i < calculated_tasks.size(); ++i) {
            core::sched::CpmTask& task = calculated_tasks[i];
            const core::sched::CpmTaskResult& effective_row = scheduled.tasks[i];
            const PlanTask& row = input.tasks[cpm_input_index[i]];
            task.manual_start = kDateNa;
            task.manual_finish = kDateNa;
            // A manual predecessor still drives a successor from its KEPT dates:
            // Project's calculated pair for the successor is the bound of the
            // predecessor's effective finish (link-fs-lag uid 2).  See
            // CpmTask::link_anchor_start.
            if (row.manually_scheduled) {
                task.link_anchor_start = effective_row.start;
                task.link_anchor_finish = effective_row.finish;
            }
        }
        calculated =
            core::sched::schedule_tasks(calculated_tasks, links, input.calendar, options);
        if (!calculated.ok()) {
            result.error = calculated.error;
            result.message = "recalculate: calculated (non-manual) pass failed: " +
                             calculated.message;
            result.tasks.clear();
            return result;
        }
        have_calculated = true;
    }
    result.manual_pinned = manual_pinned;
    result.manual_unpinned = manual_rows - manual_pinned;

    // ---- summary rollup (§3.7) -------------------------------------------
    std::vector<const core::sched::CpmTaskResult*> leaf_result(count, nullptr);
    for (std::size_t i = 0; i < cpm_tasks.size(); ++i) {
        leaf_result[cpm_input_index[i]] = &scheduled.tasks[i];
    }
    // The calculated pair per leaf, indexed like cpm_tasks.  Empty when every
    // row is auto-scheduled: the effective pair IS the calculated pair then.
    std::vector<const core::sched::CpmTaskResult*> calculated_result(count, nullptr);
    if (have_calculated) {
        for (std::size_t i = 0; i < cpm_tasks.size(); ++i) {
            calculated_result[cpm_input_index[i]] = &calculated.tasks[i];
        }
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
        out.manually_scheduled = task.manually_scheduled;
        if (!out.summary) {
            const core::sched::CpmTaskResult& effective = *leaf_result[index];
            // The KEPT/effective pair (+104/+108): an actual date is never
            // overwritten by the recalculation (§2.4), a manual row keeps its
            // stored dates, and every other row is the CPM result.
            out.display_start =
                is_na(task.actual_start) ? effective.start : task.actual_start;
            out.display_finish =
                is_na(task.actual_finish) ? effective.finish : task.actual_finish;
            out.early_start = effective.early_start;
            out.early_finish = effective.early_finish;
            // Project's CALCULATED pair (+96/+100): the pin-free pass.  When no
            // row is manual there is only one pass and the two pairs coincide.
            const core::sched::CpmTaskResult& calculated_row =
                (have_calculated && calculated_result[index] != nullptr)
                    ? *calculated_result[index]
                    : effective;
            out.start = calculated_row.start;
            out.finish = calculated_row.finish;
            // Late dates belong to the EFFECTIVE schedule (Project's +112/+116):
            // they pair with display_* and with the slack below.  For an AUTO row
            // this is the CPM late pair (unchanged since wave 3).  For a MANUAL
            // row Project stores a different pair; the measured rule is applied
            // after this loop (search "MANUAL LATE PAIR"), where every row's kept
            // dates are known.
            out.late_start = effective.late_start;
            out.late_finish = effective.late_finish;
            out.manually_pinned = pinned_row[index] != 0;
            // +174: every measured manual row stores its KEPT start there.  An
            // auto row's anchor is the bound its Start was aligned from, which the
            // engine does not report; leave it unset so a writer inherits.
            out.start_anchor = task.manually_scheduled ? out.display_start : kDateNa;
            out.start_slack = effective.start_slack;
            out.finish_slack = effective.finish_slack;
            out.free_slack = effective.free_slack;
            out.total_slack = effective.total_slack;
            out.critical = effective.critical;
            out.constraint = effective.constraint;
            out.start_is_actual = !is_na(task.actual_start);
            out.finish_is_actual = !is_na(task.actual_finish);
        } else {
            // A summary has no constraint of its own (§3.7) and no free slack.
            out.early_start = row.start;
            out.early_finish = row.finish;
            // Project stores the same rolled-up span at +96/+100 and +104/+108
            // for a summary.
            out.display_start = row.start;
            out.display_finish = row.finish;
            out.start_slack = row.start_slack;
            out.finish_slack = row.finish_slack;
            out.free_slack = row.free_slack;
            out.total_slack = row.total_slack;
            out.critical = row.critical;
            out.constraint = ConstraintType::as_soon_as_possible;
        }
        // The project finish must include a summary's rolled-up finish and an
        // actual finish the forward pass did not see.  It is the latest KEPT
        // finish (Project's project row), so a manual row's kept finish counts,
        // not its calculated one.
        if (!is_na(out.display_finish) &&
            (is_na(result.project_finish) || out.display_finish > result.project_finish)) {
            result.project_finish = out.display_finish;
        }
        result.tasks.push_back(out);
    }

    // ---- MANUAL LATE PAIR: Project's stored +112/+116 -----------------------
    // plan.h "MANUAL LATE PAIR" carries the evidence and the rule.  A manual row
    // is stored as "the row placed at its latest": the late finish is the project
    // finish capped by each successor's KEPT date (minus the link lag), and the
    // late start is that finish walked back by the row's Duration, never before
    // the start the row keeps.  It is deliberately NOT the CPM late pair (the
    // engine's manual rule forces late start = early start for the slack words)
    // and NOT `manual_finish_of()` inverted (that has no solution for a late
    // finish whose clock is not the finish family's).  Auto rows are untouched;
    // the slack words above are untouched too, so M2 cannot move.
    for (std::size_t index = 0; index < count; ++index) {
        PlanTaskResult& out = result.tasks[index];
        if (!out.manually_pinned) {
            continue;
        }
        const PlanTask& task = input.tasks[index];
        const core::sched::Calendar* calendar = &input.calendar;
        if (task.calendar_uid > 0) {
            const auto found = calendar_by_uid.find(task.calendar_uid);
            if (found != calendar_by_uid.end()) {
                calendar = found->second;
            }
        }
        // The project finish is the bound a manual row without successors stores
        // (recalculate / duration-change / delete / constraint-* oracles).
        date32 finish = result.project_finish;
        bool modelled = !is_na(finish);
        for (const PlanLink& link : input.links) {
            if (link.predecessor != task.uid) {
                continue;
            }
            const auto successor = index_of.find(link.successor);
            if (successor == index_of.end() || is_summary[successor->second] != 0) {
                continue;  // not in the graph (mirrors the link mapping above)
            }
            // FS bounds the row's finish from the successor's kept START, FF from
            // its kept FINISH.  SS/SF bound the successor's own placement and are
            // not measured to bound this row's late finish (w12-linkadd uid 1).
            date32 anchor = kDateNa;
            if (link.type == LinkType::finish_to_start) {
                anchor = result.tasks[successor->second].display_start;
            } else if (link.type == LinkType::finish_to_finish) {
                anchor = result.tasks[successor->second].display_finish;
            } else {
                continue;
            }
            // An elapsed lag is wall-clock in the engine; the manual rule is only
            // measured on working-time lags, so such a row falls back whole.
            if (link.elapsed_lag || is_na(anchor)) {
                modelled = false;
                continue;
            }
            const date32 bound =
                link.lag >= 0 ? calendar->sub_work(anchor, link.lag)
                              : calendar->add_work(anchor, -link.lag);
            if (is_na(bound) || core::sched::is_overflow(bound)) {
                modelled = false;
                continue;
            }
            if (is_na(finish) || bound < finish) {
                finish = bound;
            }
        }
        const date32 start =
            modelled ? calendar->sub_work(finish, out.duration) : kDateNa;
        if (is_na(start) || core::sched::is_overflow(start)) {
            ++result.manual_late_unmodelled;  // keeps the CPM pair
            continue;
        }
        // The clamp: a manual row's latest start never precedes the start it
        // keeps (link-fs-lag uid 1 / w12-linkadd uid 1-2 store their kept start).
        out.late_start =
            (!is_na(out.display_start) && start < out.display_start) ? out.display_start
                                                                     : start;
        out.late_finish = finish;
    }
    // The project row's duration (+84): the working time between the project
    // start and finish on the project calendar.  MEASURED on the seven oracles --
    // 21600 / 12000 / 40800 / 21600 / 21600 / 21600 / 45600 -- each equal to
    // work_time_between(project_start, project_finish), with the Sunday 13:00
    // start contributing nothing (plan.h).
    if (!is_na(result.project_start) && !is_na(result.project_finish)) {
        result.project_duration =
            input.calendar.work_time_between(result.project_start, result.project_finish);
        if (result.project_duration < 0) {
            result.project_duration = 0;
        }
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
