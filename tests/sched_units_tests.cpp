// tests/sched_units_tests.cpp -- the work/duration/units triangle (§2.3).
//
// ORACLE rows come from docs/analysis/oracle/w3-o2.json and w3-o2b.json
// (W3-O2/O2b, measured 2026-10-22) and are quoted in minutes converted to
// ticks (x10, spec §0.2).  The setup: one 3 d task from Mon 2026-10-19 08:00
// with one resource at 100 %, i.e. Duration 1440 min, Work 1440 min, Units 1.0.
//
// INFERENCE tags mark behaviour the oracle has NOT measured:
//   * the .mpp file scales (units double x100, work minutes x1000) -- the brief
//     gives them, no oracle row in this stream reads the raw bytes;
//   * rounding and proportional work distribution;
//   * effort-driven add/remove (W3-U2: add a resource to an existing task and
//     compare Duration with EffortDriven on and off).
//
// The measured rule the code encodes: Units is NEVER derived from Duration and
// Work.  On a fixed-duration task setting Work=4h leaves Duration 1440 and
// Units 1.0; on a fixed-work task setting Duration=5d leaves Work 240 and
// Units 1.0.  Those two under-allocation cells refute §2.3's "Units = Work/Dur"
// columns.
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "sched_test_support.h"
#include "units.h"

namespace sched = core::sched;

namespace {

constexpr sched::ticks mins(std::int64_t minutes) { return minutes * 10; }
constexpr sched::units32 pct(std::int64_t percent) { return percent * 100; }

constexpr sched::ticks kThreeDays = mins(1440);
constexpr sched::ticks kFourHours = mins(240);
constexpr sched::ticks kFiveDays = mins(2400);

sched::TaskWorkUnits one_resource(sched::TaskType type, bool effort_driven, sched::ticks duration,
                                  sched::ticks work, sched::units32 units) {
    sched::TaskWorkUnits task;
    task.type = type;
    task.effort_driven = effort_driven;
    task.duration = duration;
    task.work = work;
    sched::Assignment assignment;
    assignment.resource_id = 1;
    assignment.units = units;
    assignment.work = work;
    task.assignments.push_back(assignment);
    return task;
}

sched::TaskWorkUnits two_resources(sched::TaskType type, bool effort_driven, sched::ticks duration,
                                   sched::ticks work) {
    sched::TaskWorkUnits task;
    task.type = type;
    task.effort_driven = effort_driven;
    task.duration = duration;
    task.work = work;
    for (std::int32_t resource = 1; resource <= 2; ++resource) {
        sched::Assignment assignment;
        assignment.resource_id = resource;
        assignment.units = pct(100);
        assignment.work = work / 2;
        task.assignments.push_back(assignment);
    }
    return task;
}

void check_values(sched_test::TestContext& ctx, const sched::Triangle& actual,
                  const sched::Triangle& expected, const std::string& what) {
    ctx.equal(actual.duration, expected.duration, what + " duration");
    ctx.equal(actual.work, expected.work, what + " work");
    ctx.equal(actual.units, expected.units, what + " units");
}

void check_task(sched_test::TestContext& ctx, const sched::TaskUnitsResult& result,
                sched::ticks duration, sched::ticks work, sched::units32 units,
                const std::string& what) {
    if (!result.ok()) {
        ctx.check(false, what + ": ok (error=" + std::string(sched::to_string(result.error)) + " " +
                             result.message + ")");
        return;
    }
    ctx.check(true, what + ": ok");
    ctx.equal(result.task.duration, duration, what + " duration");
    ctx.equal(result.task.work, work, what + " work");
    ctx.equal(result.task.total_units(), units, what + " total units");
}

void check_error(sched_test::TestContext& ctx, const sched::TaskUnitsResult& result,
                 sched::UnitsError expected, const std::string& what) {
    ctx.equal(static_cast<int>(result.error), static_cast<int>(expected),
              what + " error (message=\"" + result.message + "\")");
    ctx.check(!result.ok(), what + ": not ok()");
}

// One measured cell of the 3x3 edit matrix.  The input holds the values after
// the edit (the edited value is new, the dependent one is stale); the output is
// the expected triangle.
struct EditCell {
    sched::TaskType type;
    sched::Quantity edited;
    sched::ticks duration;
    sched::ticks work;
    sched::units32 units;
    sched::ticks expected_duration;
    sched::ticks expected_work;
    sched::units32 expected_units;
    const char* oracle;
};

}  // namespace

int main() {
    sched_test::TestContext ctx;

    // --- conversions -------------------------------------------------------
    // COM ratio: the measured rows read / write Assignment.Units as 0.5, 1.0
    // and 2.0 (w3-o2b).
    ctx.equal(sched::units_from_ratio(1.0), pct(100), "ORACLE units 1.0 -> 10000");
    ctx.equal(sched::units_from_ratio(0.5), pct(50), "ORACLE units 0.5 -> 5000");
    ctx.equal(sched::units_from_ratio(2.0), pct(200), "ORACLE units 2.0 -> 20000");
    ctx.equal(sched::units_to_ratio(pct(50)), 0.5, "ORACLE 5000 -> 0.5");
    ctx.equal(sched::units_to_ratio(pct(200)), 2.0, "ORACLE 20000 -> 2.0");
    ctx.equal(sched::units_from_percent(50.0), pct(50), "units_from_percent 50");
    ctx.equal(sched::units_from_percent(200.0), pct(200), "units_from_percent 200");
    ctx.equal(sched::units_to_percent(pct(50)), 50.0, "units_to_percent 5000");
    // INFERENCE (stream brief W15, not an oracle row): the .mpp units double is
    // already x100 percent-scaled, so 100 % is 10000.0.
    ctx.equal(sched::units_from_file_value(10000.0), pct(100),
              "INFERENCE units_from_file_value(10000.0)");
    ctx.equal(sched::units_from_file_value(5000.0), pct(50),
              "INFERENCE units_from_file_value(5000.0)");
    ctx.equal(sched::units_to_file_value(pct(100)), 10000.0, "INFERENCE units_to_file_value");

    // Work: COM minutes (w3-o2 Work 1440.0 for 3 d) and engine ticks.
    ctx.equal(sched::work_from_minutes(1440.0), mins(1440), "ORACLE work 1440 min -> 14400 ticks");
    ctx.equal(sched::work_to_minutes(mins(1440)), 1440.0, "ORACLE 14400 ticks -> 1440 min");
    ctx.equal(sched::work_from_minutes(240.0), mins(240), "ORACLE work 240 min -> 2400 ticks");
    // INFERENCE (stream brief W15): the .mpp Work field is minutes x1000, i.e.
    // 100 file units per engine tick.
    ctx.equal(sched::work_from_file_units(1440000), mins(1440),
              "INFERENCE work_from_file_units(3 d)");
    ctx.equal(sched::work_to_file_units(mins(1440)), 1440000, "INFERENCE work_to_file_units(3 d)");
    ctx.equal(sched::work_from_file_units(240000), mins(240),
              "INFERENCE work_from_file_units(4 h)");

    // --- the measured dependency table -------------------------------------
    // 12 direct checks; the 3x3 rows below exercise the same table through the
    // arithmetic.
    ctx.equal(static_cast<int>(sched::dependent_of(sched::TaskType::fixed_units,
                                                   sched::Quantity::duration)),
              static_cast<int>(sched::Quantity::work), "ORACLE fixed units + duration -> work");
    ctx.equal(static_cast<int>(sched::dependent_of(sched::TaskType::fixed_units,
                                                   sched::Quantity::work)),
              static_cast<int>(sched::Quantity::duration), "ORACLE fixed units + work -> duration");
    ctx.equal(static_cast<int>(sched::dependent_of(sched::TaskType::fixed_units,
                                                   sched::Quantity::units)),
              static_cast<int>(sched::Quantity::duration), "ORACLE fixed units + units -> duration");
    ctx.equal(static_cast<int>(sched::dependent_of(sched::TaskType::fixed_duration,
                                                   sched::Quantity::duration)),
              static_cast<int>(sched::Quantity::work), "ORACLE fixed duration + duration -> work");
    ctx.equal(static_cast<int>(sched::dependent_of(sched::TaskType::fixed_duration,
                                                   sched::Quantity::work)),
              static_cast<int>(sched::Quantity::none),
              "ORACLE fixed duration + work -> none (under-allocated)");
    ctx.equal(static_cast<int>(sched::dependent_of(sched::TaskType::fixed_duration,
                                                   sched::Quantity::units)),
              static_cast<int>(sched::Quantity::work), "ORACLE fixed duration + units -> work");
    ctx.equal(static_cast<int>(sched::dependent_of(sched::TaskType::fixed_work,
                                                   sched::Quantity::duration)),
              static_cast<int>(sched::Quantity::none),
              "ORACLE fixed work + duration -> none (under-allocated)");
    ctx.equal(static_cast<int>(sched::dependent_of(sched::TaskType::fixed_work,
                                                   sched::Quantity::work)),
              static_cast<int>(sched::Quantity::duration), "ORACLE fixed work + work -> duration");
    ctx.equal(static_cast<int>(sched::dependent_of(sched::TaskType::fixed_work,
                                                   sched::Quantity::units)),
              static_cast<int>(sched::Quantity::duration), "ORACLE fixed work + units -> duration");

    // --- the 3x3 edit matrix, all measured --------------------------------
    const std::vector<EditCell> cells{
        // fixed units (w3-o2 / w3-o2b)
        {sched::TaskType::fixed_units, sched::Quantity::duration, kFiveDays, kFourHours, pct(100),
         kFiveDays, kFiveDays, pct(100), "w3-o2 fixed units Duration=5d"},
        {sched::TaskType::fixed_units, sched::Quantity::work, kThreeDays, kFourHours, pct(100),
         kFourHours, kFourHours, pct(100), "w3-o2 fixed units Work=4h"},
        {sched::TaskType::fixed_units, sched::Quantity::units, kThreeDays, kThreeDays, pct(50),
         mins(2880), kThreeDays, pct(50), "w3-o2b fixed units Units=50%"},
        {sched::TaskType::fixed_units, sched::Quantity::units, kThreeDays, kThreeDays, pct(200),
         mins(720), kThreeDays, pct(200), "w3-o2b fixed units Units=200%"},
        // fixed duration (w3-o2 / w3-o2b)
        {sched::TaskType::fixed_duration, sched::Quantity::duration, kFiveDays, kFourHours,
         pct(100), kFiveDays, kFiveDays, pct(100), "w3-o2 fixed duration Duration=5d"},
        {sched::TaskType::fixed_duration, sched::Quantity::work, kThreeDays, kFourHours, pct(100),
         kThreeDays, kFourHours, pct(100),
         "w3-o2 fixed duration Work=4h (under-allocated: Work 240, Units 1.0)"},
        {sched::TaskType::fixed_duration, sched::Quantity::units, kThreeDays, kThreeDays, pct(50),
         kThreeDays, mins(720), pct(50), "w3-o2b fixed duration Units=50%"},
        {sched::TaskType::fixed_duration, sched::Quantity::units, kThreeDays, kThreeDays, pct(200),
         kThreeDays, mins(2880), pct(200), "w3-o2b fixed duration Units=200%"},
        // fixed work (w3-o2 / w3-o2b)
        {sched::TaskType::fixed_work, sched::Quantity::duration, kFiveDays, kFourHours, pct(100),
         kFiveDays, kFourHours, pct(100),
         "w3-o2 fixed work Duration=5d (under-allocated: Work 240, Units 1.0)"},
        {sched::TaskType::fixed_work, sched::Quantity::work, kThreeDays, kFourHours, pct(100),
         kFourHours, kFourHours, pct(100), "w3-o2 fixed work Work=4h"},
        {sched::TaskType::fixed_work, sched::Quantity::units, kThreeDays, kThreeDays, pct(50),
         mins(2880), kThreeDays, pct(50), "w3-o2b fixed work Units=50%"},
        {sched::TaskType::fixed_work, sched::Quantity::units, kThreeDays, kThreeDays, pct(200),
         mins(720), kThreeDays, pct(200), "w3-o2b fixed work Units=200%"},
    };
    for (const EditCell& cell : cells) {
        sched::Triangle values;
        values.duration = cell.duration;
        values.work = cell.work;
        values.units = cell.units;
        const sched::UnitsResult result = sched::recompute(cell.type, values, cell.edited);
        if (!result.ok()) {
            ctx.check(false, std::string(cell.oracle) + ": recompute failed: " + result.message);
            continue;
        }
        ctx.check(true, std::string(cell.oracle) + ": recompute ok");
        const sched::Triangle expected{cell.expected_duration, cell.expected_work,
                                       cell.expected_units};
        check_values(ctx, result.values, expected, cell.oracle);
    }

    // --- arithmetic identities and their error paths ----------------------
    {
        const sched::UnitsValue work = sched::work_for(kThreeDays, pct(50));
        ctx.check(work.ok(), "work_for ok");
        ctx.equal(work.value, mins(720), "work_for 3 d x 50 % = 720 min");
        ctx.equal(sched::work_for(kThreeDays, pct(200)).value, mins(2880),
                  "work_for 3 d x 200 % = 2880 min");
        ctx.equal(sched::duration_for(kThreeDays, pct(50)).value, mins(2880),
                  "duration_for 3 d / 50 % = 2880 min");
        ctx.equal(sched::duration_for(kThreeDays, pct(200)).value, mins(720),
                  "duration_for 3 d / 200 % = 720 min");
        // ORACLE two-resource row: 4 h total over 2.0 units -> 120 min.
        ctx.equal(sched::duration_for(kFourHours, pct(200)).value, mins(120),
                  "ORACLE duration_for 4 h over 2.0 units = 120 min");
        // §2.3: summary Units is derived Work/Duration (INFERENCE, no summary
        // units row in the oracle).
        ctx.equal(sched::units_for(mins(1440), mins(2880)).value, pct(50),
                  "INFERENCE units_for 1440 min over 2880 min = 50 %");

        ctx.equal(static_cast<int>(sched::work_for(-1, pct(100)).error),
                  static_cast<int>(sched::UnitsError::negative_duration),
                  "work_for: negative duration refused");
        ctx.equal(static_cast<int>(sched::duration_for(mins(1), 0).error),
                  static_cast<int>(sched::UnitsError::division_by_zero),
                  "duration_for: zero units refused");
        ctx.equal(static_cast<int>(sched::units_for(mins(1), 0).error),
                  static_cast<int>(sched::UnitsError::division_by_zero),
                  "units_for: zero duration refused");
        // INFERENCE: rounding is half away from zero; no oracle row is inexact.
        ctx.equal(sched::units_for(mins(1000), mins(3000)).value, pct(33) + 33,
                  "INFERENCE units_for rounds 33.33 % to 3333");
    }

    // --- task-level edits, one resource: measured rows --------------------
    {
        const sched::TaskWorkUnits fixed_units =
            one_resource(sched::TaskType::fixed_units, false, kThreeDays, kThreeDays, pct(100));

        const sched::TaskUnitsResult four_hours = sched::set_work(fixed_units, kFourHours);
        check_task(ctx, four_hours, kFourHours, kFourHours, pct(100),
                   "ORACLE fixed units Work=4h");
        if (four_hours.ok()) {
            ctx.equal(four_hours.task.assignments.at(0).work, kFourHours,
                      "ORACLE fixed units Work=4h assignment work");
        }
        if (four_hours.ok()) {
            const sched::TaskUnitsResult five_days =
                sched::set_duration(four_hours.task, kFiveDays);
            check_task(ctx, five_days, kFiveDays, kFiveDays, pct(100),
                       "ORACLE fixed units Duration=5d");
        }
        const sched::TaskUnitsResult units_50 =
            sched::set_assignment_units(fixed_units, 1, pct(50));
        check_task(ctx, units_50, mins(2880), kThreeDays, pct(50),
                   "ORACLE fixed units Units=50%");
        if (units_50.ok()) {
            ctx.equal(units_50.task.assignments.at(0).work, kThreeDays,
                      "ORACLE fixed units Units=50% assignment work stays 3 d");
        }
        const sched::TaskUnitsResult units_200 =
            sched::set_assignment_units(fixed_units, 1, pct(200));
        check_task(ctx, units_200, mins(720), kThreeDays, pct(200),
                   "ORACLE fixed units Units=200%");
    }
    {
        const sched::TaskWorkUnits fixed_duration =
            one_resource(sched::TaskType::fixed_duration, false, kThreeDays, kThreeDays, pct(100));

        const sched::TaskUnitsResult units_50 =
            sched::set_assignment_units(fixed_duration, 1, pct(50));
        check_task(ctx, units_50, kThreeDays, mins(720), pct(50),
                   "ORACLE fixed duration Units=50%");
        if (units_50.ok()) {
            ctx.equal(units_50.task.assignments.at(0).work, mins(720),
                      "ORACLE fixed duration Units=50% assignment work");
        }
        const sched::TaskUnitsResult units_200 =
            sched::set_assignment_units(fixed_duration, 1, pct(200));
        check_task(ctx, units_200, kThreeDays, mins(2880), pct(200),
                   "ORACLE fixed duration Units=200%");

        // The under-allocation cell: Work is set, Duration and Units are kept.
        const sched::TaskUnitsResult four_hours = sched::set_work(fixed_duration, kFourHours);
        check_task(ctx, four_hours, kThreeDays, kFourHours, pct(100),
                   "ORACLE fixed duration Work=4h (under-allocated)");
        if (four_hours.ok()) {
            ctx.equal(four_hours.task.assignments.at(0).work, kFourHours,
                      "ORACLE fixed duration Work=4h assignment work");
            const sched::TaskUnitsResult five_days =
                sched::set_duration(four_hours.task, kFiveDays);
            check_task(ctx, five_days, kFiveDays, kFiveDays, pct(100),
                       "ORACLE fixed duration Duration=5d");
        }
    }
    {
        const sched::TaskWorkUnits fixed_work =
            one_resource(sched::TaskType::fixed_work, true, kThreeDays, kThreeDays, pct(100));

        const sched::TaskUnitsResult units_50 =
            sched::set_assignment_units(fixed_work, 1, pct(50));
        check_task(ctx, units_50, mins(2880), kThreeDays, pct(50),
                   "ORACLE fixed work Units=50%");
        const sched::TaskUnitsResult units_200 =
            sched::set_assignment_units(fixed_work, 1, pct(200));
        check_task(ctx, units_200, mins(720), kThreeDays, pct(200),
                   "ORACLE fixed work Units=200%");

        const sched::TaskUnitsResult four_hours = sched::set_work(fixed_work, kFourHours);
        check_task(ctx, four_hours, kFourHours, kFourHours, pct(100),
                   "ORACLE fixed work Work=4h");
        if (four_hours.ok()) {
            // The second under-allocation cell: Duration is set, Work and Units
            // are kept.
            const sched::TaskUnitsResult five_days =
                sched::set_duration(four_hours.task, kFiveDays);
            check_task(ctx, five_days, kFiveDays, kFourHours, pct(100),
                       "ORACLE fixed work Duration=5d (under-allocated)");
        }
    }

    // --- two resources: measured, total_units = 2.0 -----------------------
    {
        const sched::TaskWorkUnits task =
            two_resources(sched::TaskType::fixed_units, false, kThreeDays, mins(2880));
        ctx.equal(task.total_units(), pct(200), "ORACLE two assignment units sum to 20000");
        const sched::Triangle values = sched::triangle_of(task);
        ctx.equal(values.duration, kThreeDays, "ORACLE two resources duration 3 d");
        ctx.equal(values.work, mins(2880), "ORACLE two resources work 2880 min");
        ctx.equal(values.units, pct(200), "ORACLE two resources units 2.0");

        const sched::TaskUnitsResult four_hours = sched::set_work(task, kFourHours);
        check_task(ctx, four_hours, mins(120), kFourHours, pct(200),
                   "ORACLE two resources Work=4h -> 120 min");
        if (four_hours.ok()) {
            ctx.equal(four_hours.task.assignments.at(0).work, mins(120),
                      "ORACLE two resources Work=4h: R1 120 min");
            ctx.equal(four_hours.task.assignments.at(1).work, mins(120),
                      "ORACLE two resources Work=4h: R2 120 min");
        }
    }

    // --- effort-driven add/remove: INFERENCE (W3-U2) ----------------------
    {
        const sched::TaskWorkUnits task =
            one_resource(sched::TaskType::fixed_units, true, kThreeDays, kThreeDays, pct(100));

        const sched::TaskUnitsResult added = sched::add_assignment(task, 2, pct(100));
        check_task(ctx, added, mins(720), kThreeDays, pct(200),
                   "INFERENCE effort-driven add: work held, duration halved");
        if (added.ok()) {
            ctx.equal(added.task.assignments.at(0).work, mins(720),
                      "INFERENCE effort-driven add: R1 720 min");
            ctx.equal(added.task.assignments.at(1).work, mins(720),
                      "INFERENCE effort-driven add: R2 720 min");

            const sched::TaskUnitsResult removed =
                sched::remove_assignment(added.task, 2);
            check_task(ctx, removed, kThreeDays, kThreeDays, pct(100),
                       "INFERENCE effort-driven remove: work held, duration restored");
        }

        const sched::TaskUnitsResult last = sched::remove_assignment(task, 1);
        check_task(ctx, last, kThreeDays, 0, 0,
                   "INFERENCE removing the last resource: work 0, duration kept");
    }
    {
        const sched::TaskWorkUnits task =
            one_resource(sched::TaskType::fixed_units, false, kThreeDays, kThreeDays, pct(100));
        const sched::TaskUnitsResult added = sched::add_assignment(task, 2, pct(100));
        // The end state matches the measured W3-O2 two-resource row; the
        // transition with the flag off is still only an inference.
        check_task(ctx, added, kThreeDays, mins(2880), pct(200),
                   "INFERENCE non-effort add: duration held, work grows (end state measured)");
        if (added.ok()) {
            ctx.equal(added.task.assignments.at(0).work, kThreeDays,
                      "INFERENCE non-effort add: R1 1440 min");
            ctx.equal(added.task.assignments.at(1).work, kThreeDays,
                      "INFERENCE non-effort add: R2 1440 min");
        }
    }
    {
        const sched::TaskWorkUnits task =
            one_resource(sched::TaskType::fixed_duration, true, kThreeDays, kThreeDays, pct(100));
        const sched::TaskUnitsResult added = sched::add_assignment(task, 2, pct(100));
        check_task(ctx, added, kThreeDays, mins(2880), pct(200),
                   "INFERENCE effort-driven fixed-duration add: duration wins");
    }
    {
        const sched::TaskWorkUnits task =
            one_resource(sched::TaskType::fixed_work, false, kThreeDays, kThreeDays, pct(100));
        check_error(ctx, sched::set_work(task, kFourHours),
                    sched::UnitsError::fixed_work_requires_effort_driven,
                    "fixed work without effort-driven: set_work");
        check_error(ctx, sched::add_assignment(task, 2, pct(100)),
                    sched::UnitsError::fixed_work_requires_effort_driven,
                    "fixed work without effort-driven: add_assignment");
    }

    // --- multi-resource edge cases and distribution: INFERENCE ------------
    {
        const sched::TaskWorkUnits task =
            two_resources(sched::TaskType::fixed_units, false, kThreeDays, mins(2880));
        const sched::TaskUnitsResult changed =
            sched::set_assignment_units(task, 1, pct(50));
        check_task(ctx, changed, mins(1920), mins(2880), pct(150),
                   "INFERENCE two resources, R1 to 50 %: duration = work / 1.5 units");
        if (changed.ok()) {
            ctx.equal(changed.task.assignments.at(0).work, mins(960),
                      "INFERENCE R1 50 % gets 960 min");
            ctx.equal(changed.task.assignments.at(1).work, mins(1920),
                      "INFERENCE R2 100 % gets 1920 min");
        }

        sched::TaskWorkUnits three;
        three.type = sched::TaskType::fixed_units;
        three.duration = mins(480);
        three.work = 1000;
        for (std::int32_t resource = 1; resource <= 3; ++resource) {
            sched::Assignment assignment;
            assignment.resource_id = resource;
            assignment.units = pct(100);
            three.assignments.push_back(assignment);
        }
        sched::distribute_work(three);
        ctx.equal(three.assignments.at(0).work, sched::ticks{333},
                  "INFERENCE distribution: first share 333");
        ctx.equal(three.assignments.at(1).work, sched::ticks{333},
                  "INFERENCE distribution: second share 333");
        ctx.equal(three.assignments.at(2).work, sched::ticks{334},
                  "INFERENCE distribution: last share takes the remainder 334");

        // No assignments: no unit total, so a derived duration is division by zero.
        sched::TaskWorkUnits empty;
        empty.type = sched::TaskType::fixed_units;
        empty.duration = kThreeDays;
        check_error(ctx, sched::set_work(empty, kFourHours), sched::UnitsError::division_by_zero,
                    "set_work with no assignments");
    }
    {
        const sched::TaskWorkUnits task =
            one_resource(sched::TaskType::fixed_units, false, kThreeDays, kThreeDays, pct(100));
        check_error(ctx, sched::add_assignment(task, 1, pct(100)),
                    sched::UnitsError::duplicate_resource, "add_assignment duplicate");
        check_error(ctx, sched::remove_assignment(task, 9), sched::UnitsError::unknown_resource,
                    "remove_assignment unknown");
        check_error(ctx, sched::set_assignment_units(task, 9, pct(50)),
                    sched::UnitsError::unknown_resource, "set_assignment_units unknown");
        check_error(ctx, sched::set_assignment_units(task, 1, 0),
                    sched::UnitsError::non_positive_units, "set_assignment_units 0 %");
        check_error(ctx, sched::set_duration(task, -1), sched::UnitsError::negative_duration,
                    "set_duration negative");
        check_error(ctx, sched::set_work(task, -1), sched::UnitsError::negative_work,
                    "set_work negative");
    }

    // --- the error names are stable ---------------------------------------
    ctx.equal_string(sched::to_string(sched::TaskType::fixed_units), "fixed units",
                     "to_string(TaskType)");
    ctx.equal_string(sched::to_string(sched::Quantity::none), "none", "to_string(Quantity)");
    ctx.equal_string(sched::to_string(sched::UnitsError::division_by_zero), "division by zero",
                     "to_string(UnitsError)");

    return ctx.finish("sched_units");
}
