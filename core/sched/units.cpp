// core/sched/units.cpp -- the §2.3 triangle, conversions, effort-driven edits.
//
// The dependency table in units.h is the measured one (w3-o2/w3-o2b); the
// effort-driven branches are the spec's [I] candidate.
#include "units.h"

#include <cmath>
#include <limits>
#include <utility>

namespace core::sched {
namespace {

constexpr ticks kTickMax = std::numeric_limits<ticks>::max();

// round(value * num / den), half away from zero.  All inputs are non-negative.
bool mul_div_round(ticks value, std::int64_t num, std::int64_t den, ticks& out) noexcept {
    if (value < 0 || num < 0 || den <= 0) {
        return false;
    }
    if (value != 0 && num > kTickMax / value) {
        return false;
    }
    const ticks product = value * num;
    const ticks quotient = product / den;
    const ticks remainder = product % den;
    // remainder >= den - remainder  <=>  2 * remainder >= den, without overflowing.
    out = quotient + (remainder >= den - remainder ? 1 : 0);
    return true;
}

UnitsResult units_failure(UnitsError error, std::string message) {
    UnitsResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

TaskUnitsResult task_failure(UnitsError error, std::string message) {
    TaskUnitsResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

TaskUnitsResult task_success(TaskWorkUnits task) {
    TaskUnitsResult result;
    result.task = std::move(task);
    return result;
}

// [B] FUN_1404e9400: fixed work asserts "expect effort driven".
bool effort_flag_ok(const TaskWorkUnits& task) noexcept {
    return task.type != TaskType::fixed_work || task.effort_driven;
}

std::size_t find_assignment(const TaskWorkUnits& task, std::int32_t resource_id) noexcept {
    for (std::size_t i = 0; i < task.assignments.size(); ++i) {
        if (task.assignments[i].resource_id == resource_id) {
            return i;
        }
    }
    return task.assignments.size();
}

}  // namespace

const char* to_string(TaskType type) noexcept {
    switch (type) {
        case TaskType::fixed_units:
            return "fixed units";
        case TaskType::fixed_duration:
            return "fixed duration";
        case TaskType::fixed_work:
            return "fixed work";
    }
    return "unknown task type";
}

const char* to_string(Quantity quantity) noexcept {
    switch (quantity) {
        case Quantity::duration:
            return "duration";
        case Quantity::work:
            return "work";
        case Quantity::units:
            return "units";
        case Quantity::none:
            return "none";
    }
    return "unknown quantity";
}

const char* to_string(UnitsError error) noexcept {
    switch (error) {
        case UnitsError::none:
            return "none";
        case UnitsError::negative_duration:
            return "negative duration";
        case UnitsError::negative_work:
            return "negative work";
        case UnitsError::non_positive_units:
            return "non-positive units";
        case UnitsError::division_by_zero:
            return "division by zero";
        case UnitsError::overflow:
            return "overflow";
        case UnitsError::duplicate_resource:
            return "duplicate resource";
        case UnitsError::unknown_resource:
            return "unknown resource";
        case UnitsError::fixed_work_requires_effort_driven:
            return "fixed work requires effort-driven";
    }
    return "unknown error";
}

// --- units scale -----------------------------------------------------------

units32 units_from_ratio(double ratio) {
    return static_cast<units32>(std::llround(ratio * static_cast<double>(kUnitsScale)));
}

double units_to_ratio(units32 units) {
    return static_cast<double>(units) / static_cast<double>(kUnitsScale);
}

units32 units_from_percent(double percent) {
    return static_cast<units32>(std::llround(percent * 100.0));
}

double units_to_percent(units32 units) {
    return static_cast<double>(units) / 100.0;
}

units32 units_from_file_value(double stored) {
    return static_cast<units32>(std::llround(stored));
}

double units_to_file_value(units32 units) {
    return static_cast<double>(units);
}

// --- work scale ------------------------------------------------------------

ticks work_from_file_units(std::int64_t file_units) {
    constexpr std::int64_t kDivisor = kWorkFileUnitsPerTick;
    if (file_units >= 0) {
        return (file_units + kDivisor / 2) / kDivisor;
    }
    return -((-file_units + kDivisor / 2) / kDivisor);
}

std::int64_t work_to_file_units(ticks work) {
    return work * kWorkFileUnitsPerTick;
}

ticks work_from_minutes(double minutes) {
    return static_cast<ticks>(std::llround(minutes * static_cast<double>(kTicksPerMinute)));
}

double work_to_minutes(ticks work) {
    return static_cast<double>(work) / static_cast<double>(kTicksPerMinute);
}

// --- the triangle ----------------------------------------------------------

UnitsValue work_for(ticks duration, units32 units) {
    UnitsValue result;
    if (duration < 0) {
        result.error = UnitsError::negative_duration;
        return result;
    }
    if (units < 0) {
        result.error = UnitsError::non_positive_units;
        return result;
    }
    if (units == 0) {
        result.value = 0;
        return result;
    }
    if (!mul_div_round(duration, units, kUnitsScale, result.value)) {
        result.error = UnitsError::overflow;
    }
    return result;
}

UnitsValue duration_for(ticks work, units32 units) {
    UnitsValue result;
    if (work < 0) {
        result.error = UnitsError::negative_work;
        return result;
    }
    if (units < 0) {
        result.error = UnitsError::non_positive_units;
        return result;
    }
    if (units == 0) {
        result.error = UnitsError::division_by_zero;
        return result;
    }
    if (!mul_div_round(work, kUnitsScale, units, result.value)) {
        result.error = UnitsError::overflow;
    }
    return result;
}

UnitsValue units_for(ticks work, ticks duration) {
    UnitsValue result;
    if (work < 0) {
        result.error = UnitsError::negative_work;
        return result;
    }
    if (duration <= 0) {
        result.error = UnitsError::division_by_zero;
        return result;
    }
    if (!mul_div_round(work, kUnitsScale, duration, result.value)) {
        result.error = UnitsError::overflow;
    }
    return result;
}

UnitsResult recompute(TaskType type, const Triangle& values, Quantity edited) {
    UnitsResult result;
    result.values = values;
    if (values.duration < 0) {
        return units_failure(UnitsError::negative_duration,
                             std::string("recompute: negative duration for ") + to_string(type));
    }
    if (values.work < 0) {
        return units_failure(UnitsError::negative_work,
                             std::string("recompute: negative work for ") + to_string(type));
    }
    if (values.units < 0) {
        return units_failure(UnitsError::non_positive_units,
                             std::string("recompute: negative units for ") + to_string(type));
    }
    switch (dependent_of(type, edited)) {
        case Quantity::duration: {
            const UnitsValue value = duration_for(values.work, values.units);
            if (!value.ok()) {
                return units_failure(value.error,
                                     std::string("recompute: cannot derive duration for ") +
                                         to_string(type));
            }
            result.values.duration = value.value;
            break;
        }
        case Quantity::work: {
            const UnitsValue value = work_for(values.duration, values.units);
            if (!value.ok()) {
                return units_failure(value.error,
                                     std::string("recompute: cannot derive work for ") +
                                         to_string(type));
            }
            result.values.work = value.value;
            break;
        }
        case Quantity::units: {
            // The measured table never asks for this; kept for completeness.
            const UnitsValue value = units_for(values.work, values.duration);
            if (!value.ok()) {
                return units_failure(value.error,
                                     std::string("recompute: cannot derive units for ") +
                                         to_string(type));
            }
            result.values.units = value.value;
            break;
        }
        case Quantity::none:
            break;
    }
    return result;
}

// --- assignments and effort-driven scheduling ------------------------------

units32 TaskWorkUnits::total_units() const noexcept {
    units32 total = 0;
    for (const Assignment& assignment : assignments) {
        total += assignment.units;
    }
    return total;
}

Triangle triangle_of(const TaskWorkUnits& task) noexcept {
    Triangle values;
    values.duration = task.duration;
    values.work = task.work;
    values.units = task.total_units();
    return values;
}

void distribute_work(TaskWorkUnits& task) {
    if (task.assignments.empty()) {
        return;
    }
    const units32 total = task.total_units();
    if (total <= 0) {
        for (Assignment& assignment : task.assignments) {
            assignment.work = 0;
        }
        return;
    }
    ticks remaining = task.work;
    for (std::size_t i = 0; i < task.assignments.size(); ++i) {
        Assignment& assignment = task.assignments[i];
        if (i + 1 == task.assignments.size()) {
            assignment.work = remaining;
            break;
        }
        ticks share = 0;
        if (!mul_div_round(task.work, assignment.units, total, share)) {
            share = 0;
        }
        if (share > remaining) {
            share = remaining;
        }
        assignment.work = share;
        remaining -= share;
    }
}

TaskUnitsResult set_duration(TaskWorkUnits task, ticks duration) {
    if (!effort_flag_ok(task)) {
        return task_failure(UnitsError::fixed_work_requires_effort_driven,
                            "set_duration: a fixed-work task must be effort-driven");
    }
    if (duration < 0) {
        return task_failure(UnitsError::negative_duration, "set_duration: negative duration");
    }
    Triangle values = triangle_of(task);
    values.duration = duration;
    const UnitsResult result = recompute(task.type, values, Quantity::duration);
    if (!result.ok()) {
        return task_failure(result.error, "set_duration: " + result.message);
    }
    task.duration = result.values.duration;
    task.work = result.values.work;
    distribute_work(task);
    return task_success(std::move(task));
}

TaskUnitsResult set_work(TaskWorkUnits task, ticks work) {
    if (!effort_flag_ok(task)) {
        return task_failure(UnitsError::fixed_work_requires_effort_driven,
                            "set_work: a fixed-work task must be effort-driven");
    }
    if (work < 0) {
        return task_failure(UnitsError::negative_work, "set_work: negative work");
    }
    Triangle values = triangle_of(task);
    values.work = work;
    const UnitsResult result = recompute(task.type, values, Quantity::work);
    if (!result.ok()) {
        return task_failure(result.error, "set_work: " + result.message);
    }
    task.duration = result.values.duration;
    task.work = result.values.work;
    distribute_work(task);
    return task_success(std::move(task));
}

TaskUnitsResult set_assignment_units(TaskWorkUnits task, std::int32_t resource_id, units32 units) {
    if (!effort_flag_ok(task)) {
        return task_failure(UnitsError::fixed_work_requires_effort_driven,
                            "set_assignment_units: a fixed-work task must be effort-driven");
    }
    if (units <= 0) {
        return task_failure(UnitsError::non_positive_units,
                            "set_assignment_units: units must be positive");
    }
    const std::size_t index = find_assignment(task, resource_id);
    if (index == task.assignments.size()) {
        return task_failure(UnitsError::unknown_resource,
                            "set_assignment_units: resource " + std::to_string(resource_id) +
                                " is not assigned");
    }
    task.assignments[index].units = units;
    const Triangle values = triangle_of(task);
    const UnitsResult result = recompute(task.type, values, Quantity::units);
    if (!result.ok()) {
        return task_failure(result.error, "set_assignment_units: " + result.message);
    }
    task.duration = result.values.duration;
    task.work = result.values.work;
    distribute_work(task);
    return task_success(std::move(task));
}

TaskUnitsResult add_assignment(TaskWorkUnits task, std::int32_t resource_id, units32 units) {
    if (!effort_flag_ok(task)) {
        return task_failure(UnitsError::fixed_work_requires_effort_driven,
                            "add_assignment: a fixed-work task must be effort-driven");
    }
    if (units <= 0) {
        return task_failure(UnitsError::non_positive_units,
                            "add_assignment: units must be positive");
    }
    if (find_assignment(task, resource_id) != task.assignments.size()) {
        return task_failure(UnitsError::duplicate_resource,
                            "add_assignment: resource " + std::to_string(resource_id) +
                                " is already assigned");
    }

    Assignment entry;
    entry.resource_id = resource_id;
    entry.units = units;

    if (task.assignments.empty()) {
        // INFERENCE (W3-U2): one resource makes no difference -- the duration is
        // unchanged and work becomes duration x units (the measured initial
        // state, Duration 1440 / Work 1440 / Units 1.0).
        const UnitsValue value = work_for(task.duration, units);
        if (!value.ok()) {
            return task_failure(value.error, "add_assignment: first assignment");
        }
        task.assignments.push_back(entry);
        task.work = value.value;
        distribute_work(task);
        return task_success(std::move(task));
    }

    const units32 total = task.total_units() + units;
    if (task.effort_driven && task.type != TaskType::fixed_duration) {
        // INFERENCE: total work held, duration re-derived from the new units.
        const UnitsValue value = duration_for(task.work, total);
        if (!value.ok()) {
            return task_failure(value.error, "add_assignment: effort-driven duration");
        }
        task.duration = value.value;
    } else {
        // Duration held, work re-derived.  The measured two-resource end state
        // (3 d, two 100 % resources -> Work 2880 min) is this branch.
        const UnitsValue value = work_for(task.duration, total);
        if (!value.ok()) {
            return task_failure(value.error, "add_assignment: work");
        }
        task.work = value.value;
    }
    task.assignments.push_back(entry);
    distribute_work(task);
    return task_success(std::move(task));
}

TaskUnitsResult remove_assignment(TaskWorkUnits task, std::int32_t resource_id) {
    if (!effort_flag_ok(task)) {
        return task_failure(UnitsError::fixed_work_requires_effort_driven,
                            "remove_assignment: a fixed-work task must be effort-driven");
    }
    const std::size_t index = find_assignment(task, resource_id);
    if (index == task.assignments.size()) {
        return task_failure(UnitsError::unknown_resource,
                            "remove_assignment: resource " + std::to_string(resource_id) +
                                " is not assigned");
    }
    task.assignments.erase(task.assignments.begin() + static_cast<std::ptrdiff_t>(index));
    if (task.assignments.empty()) {
        // INFERENCE: no resources left -> no work, duration unchanged.
        task.work = 0;
        return task_success(std::move(task));
    }
    const units32 total = task.total_units();
    if (task.effort_driven && task.type != TaskType::fixed_duration) {
        const UnitsValue value = duration_for(task.work, total);
        if (!value.ok()) {
            return task_failure(value.error, "remove_assignment: effort-driven duration");
        }
        task.duration = value.value;
    } else {
        const UnitsValue value = work_for(task.duration, total);
        if (!value.ok()) {
            return task_failure(value.error, "remove_assignment: work");
        }
        task.work = value.value;
    }
    distribute_work(task);
    return task_success(std::move(task));
}

}  // namespace core::sched
