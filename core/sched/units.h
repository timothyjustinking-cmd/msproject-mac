// core/sched/units.h -- work, duration and units (§2.3): the task-type edit
// triangle, effort-driven add/remove, and the stored/COM conversions.
//
// MEASURED against docs/analysis/oracle/w3-o2.json and w3-o2b.json (W3-O2/O2b,
// 2026-10-22).  One 3 d task from Mon 2026-10-19 08:00 with one resource at
// 100 % starts at Duration 1440 min, Work 1440 min, Units 1.0.  The oracle rows
// pin every cell of the 3x3 edit matrix:
//
//   type            Duration=          Work=                     Units=
//   fixed units     Work=Dur x U       Dur=Work / U              Dur=Work / U
//   fixed duration  Work=Dur x U       no recompute: Work is     Work=Dur x U
//                                      set, Units is kept
//                                      (under-allocation)
//   fixed work      no recompute:      Dur=Work / U              Dur=Work / U
//                   Duration is set,
//                   Work and Units kept
//                   (under-allocation)
//
// The measured rule is stronger than §2.3's table: **Units is never derived
// from Duration and Work.**  In the two "no recompute" cells MS Project keeps
// the assignment units and accepts Work != Duration x Units (the task is
// under-allocated): setting Work=4h on a fixed-duration 3 d task leaves
// Duration 1440, Work 240 and Units 1.0; setting Duration=5d on a fixed-work
// task with Work 240 leaves Work 240 and Units 1.0.  §2.3's fixed-duration
// Work-edit column ("Units = Work/Dur") and fixed-work Duration-edit column
// ("Units = Work/Dur") are therefore refuted by the oracle.
//
// Two resources at 100 % on the same 3 d task give Work 2880 min (the sum of
// the assignments); setting the task Work to 4 h gives Duration 120 min with
// 120 min on each assignment.  The task-level identity uses
// total_units = sum(assignment units):
//
//   work = duration x total_units / 10000
//
// STILL INFERENCE: effort-driven add/remove (spec §2.3 [I]).  W3-O2 recorded
// identical two-resource states with EffortDriven false and true but did not
// isolate the add/remove step; the recipe that would settle it is "add a
// resource to an existing task and compare Duration with the flag on/off"
// (W3-U2).  The behaviour below is the spec's candidate, tagged INFERENCE in
// tests/sched_units_tests.cpp.
//
// The stored scales are from the stream brief, not from an oracle row measured
// here: units are a .mpp double already scaled x100 (100 % = 10000.0) and work
// is minutes x1000.  The engine keeps ticks (tenths of a minute); both
// conversions are explicit and are the only place that knows the scales.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "duration.h"

namespace core::sched {

// PjTaskFixedType (§0.4, typelib line 4398).
enum class TaskType : std::uint8_t {
    fixed_units = 0,
    fixed_duration = 1,
    fixed_work = 2,
};

const char* to_string(TaskType type) noexcept;

// The quantity a user edit changed.
enum class Quantity : std::uint8_t {
    duration = 0,
    work = 1,
    units = 2,
    none = 3,  // the type recomputes nothing after this edit (the under-allocation cells)
};

const char* to_string(Quantity quantity) noexcept;

// --- units -----------------------------------------------------------------
// Fixed-point ratio: 10000 = 100 %.  The COM surface reports the ratio 0..1
// (w3-o2b: Assignment.Units reads 0.5 / 1.0 / 2.0); the .mpp double already
// carries the x100 percent scale (100 % = 10000.0).
using units32 = std::int64_t;

inline constexpr units32 kUnitsScale = 10000;  // 100 %
inline constexpr units32 kUnitsHalf = 5000;    // 50 %
inline constexpr units32 kUnitsDouble = 20000; // 200 %

units32 units_from_ratio(double ratio);  // COM 0..1 -> x10000
double units_to_ratio(units32 units);
units32 units_from_percent(double percent);  // 50.0 -> 5000
double units_to_percent(units32 units);
// The .mpp double: 100 % is 10000.0 (the brief's "double x100").  A reader that
// holds a 0..100 percentage should use units_from_percent instead.
units32 units_from_file_value(double stored);
double units_to_file_value(units32 units);

// --- work ------------------------------------------------------------------
// Engine: ticks (tenths of a minute).  .mpp: minutes x1000 (stream brief W15).
// COM: minutes (w3-o2 "Work": 1440.0 for 3 d).
inline constexpr std::int64_t kWorkFileUnitsPerMinute = 1000;
inline constexpr std::int64_t kWorkFileUnitsPerTick = 100;  // 1000 / 10

ticks work_from_file_units(std::int64_t file_units);
std::int64_t work_to_file_units(ticks work);
ticks work_from_minutes(double minutes);
double work_to_minutes(ticks work);

// --- the triangle ----------------------------------------------------------
// The three task-level quantities.  `units` is the TOTAL units of the task,
// i.e. the sum of the assignment units; for one resource it is that
// assignment's units.
struct Triangle {
    ticks duration = 0;  // tenths of a minute
    ticks work = 0;      // tenths of a minute
    units32 units = 0;   // x10000; 10000 = 100 %
};

enum class UnitsError : std::uint8_t {
    none = 0,
    negative_duration,
    negative_work,
    non_positive_units,  // a units value < 0 (or <= 0 when it is an input)
    division_by_zero,    // deriving a duration from 0 units, or units from 0 duration
    overflow,            // the fixed-point multiply left the ticks range
    duplicate_resource,
    unknown_resource,
    fixed_work_requires_effort_driven,  // [B] FUN_1404e9400 asserts "expect effort driven"
};

const char* to_string(UnitsError error) noexcept;

// A single arithmetic result.  For units_for() the units32 value is returned in
// `value` (same 64-bit representation).
struct UnitsValue {
    UnitsError error = UnitsError::none;
    ticks value = 0;
    bool ok() const noexcept { return error == UnitsError::none; }
};

struct UnitsResult {
    UnitsError error = UnitsError::none;
    std::string message;
    Triangle values;
    bool ok() const noexcept { return error == UnitsError::none; }
};

// The measured dependency table: which quantity the task type recomputes after
// `edited` changed.  `Quantity::none` is the under-allocation cell where MSP
// leaves the third value alone (it never derives Units).
constexpr Quantity dependent_of(TaskType type, Quantity edited) noexcept {
    switch (type) {
        case TaskType::fixed_units:
            switch (edited) {
                case Quantity::duration:
                    return Quantity::work;
                case Quantity::work:
                    return Quantity::duration;
                case Quantity::units:
                    return Quantity::duration;
                case Quantity::none:
                    return Quantity::none;
            }
            return Quantity::none;
        case TaskType::fixed_duration:
            switch (edited) {
                case Quantity::duration:
                    return Quantity::work;
                case Quantity::work:
                    // MEASURED under-allocation: Work is set, Units is kept.
                    return Quantity::none;
                case Quantity::units:
                    return Quantity::work;
                case Quantity::none:
                    return Quantity::none;
            }
            return Quantity::none;
        case TaskType::fixed_work:
            switch (edited) {
                case Quantity::duration:
                    // MEASURED under-allocation: Duration is set, Work and Units kept.
                    return Quantity::none;
                case Quantity::work:
                    return Quantity::duration;
                case Quantity::units:
                    return Quantity::duration;
                case Quantity::none:
                    return Quantity::none;
            }
            return Quantity::none;
    }
    return Quantity::none;
}

// Recomputes the quantity dependent_of(type, edited) names, leaving the edited
// quantity and the third one alone.  `values` must already hold the edited
// value.  This is the pure 3x3 matrix; effort_driven does not enter it.
//
// Rounding (half away from zero) is the only part of the arithmetic the oracle
// has not exercised; the measured rows are all exact.
UnitsResult recompute(TaskType type, const Triangle& values, Quantity edited);

// §2.1 identities, MEASURED in the worked rows:
//   work = duration x units / 10000
//   duration = work x 10000 / units
//   units = work x 10000 / duration (summary tasks only; the edit matrix never
//           derives Units -- see the header comment)
UnitsValue work_for(ticks duration, units32 units);
UnitsValue duration_for(ticks work, units32 units);
UnitsValue units_for(ticks work, ticks duration);

// --- assignments and effort-driven scheduling ------------------------------
struct Assignment {
    std::int32_t resource_id = 0;
    units32 units = kUnitsScale;
    ticks work = 0;  // this assignment's share, filled by the task helpers
};

struct TaskWorkUnits {
    TaskType type = TaskType::fixed_units;
    bool effort_driven = false;
    ticks duration = 0;
    ticks work = 0;  // total task work
    std::vector<Assignment> assignments;

    // Sum of the assignment units (0 with no assignments).  MEASURED: two
    // resources at 100 % -> 20000 and task work 2880 min on a 3 d task.
    units32 total_units() const noexcept;
};

struct TaskUnitsResult {
    UnitsError error = UnitsError::none;
    std::string message;
    TaskWorkUnits task;
    bool ok() const noexcept { return error == UnitsError::none; }
};

Triangle triangle_of(const TaskWorkUnits& task) noexcept;

// Splits task.work across the assignments in proportion to their units; the
// last assignment takes the remainder so the shares add up exactly.  The
// proportional split itself is INFERENCE (the measured two-resource row has
// equal units, so every split rule gives 120 / 120).
void distribute_work(TaskWorkUnits& task);

// Task-level edits.  Each one applies the measured dependency table and then
// redistributes the total work.  All reject a fixed-work task that is not
// effort-driven (the binary's assertion).
TaskUnitsResult set_duration(TaskWorkUnits task, ticks duration);
TaskUnitsResult set_work(TaskWorkUnits task, ticks work);
TaskUnitsResult set_assignment_units(TaskWorkUnits task, std::int32_t resource_id, units32 units);

// INFERENCE (W3-U2): effort-driven add/remove.  The spec's candidate:
//   * effort-driven and not fixed-duration: total work is held, the new unit
//     total re-derives the duration;
//   * otherwise: duration is held and work re-derives from the new unit total;
//   * the first assignment leaves the duration alone and sets
//     work = duration x units (the measured initial state);
//   * removing the last assignment leaves the duration alone and sets work 0.
// A task that is fixed-work and not effort-driven is refused.  The measured
// two-resource end states (W3-O2) agree with the "otherwise" branch, but the
// add/remove transition with the flag toggled has not been isolated.
TaskUnitsResult add_assignment(TaskWorkUnits task, std::int32_t resource_id, units32 units);
TaskUnitsResult remove_assignment(TaskWorkUnits task, std::int32_t resource_id);

}  // namespace core::sched
