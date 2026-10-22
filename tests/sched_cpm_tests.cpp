// tests/sched_cpm_tests.cpp -- the CPM engine against the measured oracle.
//
// Ground rule: every expected value below that is tagged ORACLE comes from
// docs/analysis/oracle/w3-o1.json, w3-o3.json, w3-o3b.json, w3-o4.json,
// w3-o4b.json, milestone-probe.json or lagff-probe.json and is quoted in minutes
// (the oracle's unit) converted to ticks (x10, spec §0.2).  Everything tagged
// INFERENCE is a hand-computed consequence of the formulas implemented in
// core/sched/cpm.cpp, NOT a measured MS Project result; those are listed in
// core/sched/README.md as still unverified.
//
// Calendars (same as tests/sched_calendar_tests.cpp):
//   standard  Mon-Fri 08:00-12:00 + 13:00-17:00   480 min/day = 4800 ticks
#include <cstdint>
#include <string>
#include <vector>

#include "calendar.h"
#include "cpm.h"
#include "sched_test_support.h"

namespace sched = core::sched;

namespace {

// Day numbers around the week of Monday 2026-10-19 (§0.1: day 1 = 1984-01-01).
constexpr std::int32_t kFri0910 = 15623;   // Fri 2026-10-09
constexpr std::int32_t kMon1210 = 15626;   // Mon 2026-10-12
constexpr std::int32_t kThu1510 = 15629;   // Thu 2026-10-15
constexpr std::int32_t kFri1610 = 15630;   // Fri 2026-10-16
constexpr std::int32_t kMon1910 = 15633;   // Mon 2026-10-19, the project start day
constexpr std::int32_t kTue2010 = 15634;   // Tue 2026-10-20
constexpr std::int32_t kWed2110 = 15635;   // Wed 2026-10-21
constexpr std::int32_t kThu2210 = 15636;   // Thu 2026-10-22
constexpr std::int32_t kFri2310 = 15637;   // Fri 2026-10-23
constexpr std::int32_t kMon2610 = 15640;   // Mon 2026-10-26
constexpr std::int32_t kTue2710 = 15641;   // Tue 2026-10-27
constexpr std::int32_t kWed2810 = 15642;   // Wed 2026-10-28
constexpr std::int32_t kFri3010 = 15644;   // Fri 2026-10-30
constexpr std::int32_t kMon0211 = 15647;   // Mon 2026-11-02
constexpr std::int32_t kTue0311 = 15648;   // Tue 2026-11-03 (schedin's ALAP day)
constexpr std::int32_t kMon2812 = 15703;   // Mon 2026-12-28 (milestone-probe ID3)

constexpr sched::date32 at(std::int32_t day, std::int32_t hour, std::int32_t minute) {
    return sched::pack(day, (hour * 60 + minute) * sched::kTenthsPerMinute);
}

// The oracle reports minutes; internal durations and slack are tenths of a
// minute (§0.2).  Every conversion goes through here, never inline.
constexpr sched::ticks mins(std::int64_t minutes) { return minutes * 10; }

// The measured project start is midnight, not the first working instant
// (w3-o1.json "project_start": "2026-10-19 00:00:00"); the forward pass aligns it.
constexpr sched::date32 kProjectStart = (15633u << 16);

sched::Calendar standard_calendar() {
    const sched::DayPattern day = sched::day_pattern({{8 * 60, 12 * 60}, {13 * 60, 17 * 60}});
    return sched::Calendar(sched::make_week({{1, day}, {2, day}, {3, day}, {4, day}, {5, day}}));
}

// A 4 h working day, used only to prove that a per-task calendar is honoured.
sched::Calendar short_calendar() {
    const sched::DayPattern day = sched::day_pattern({{8 * 60, 12 * 60}});
    return sched::Calendar(sched::make_week({{1, day}, {2, day}, {3, day}, {4, day}, {5, day}}));
}

sched::CpmTask task(std::int32_t id, sched::ticks duration,
                    sched::ConstraintType constraint = sched::ConstraintType::as_soon_as_possible,
                    sched::date32 constraint_date = sched::kDateNa,
                    sched::date32 deadline = sched::kDateNa,
                    const sched::Calendar* calendar = nullptr) {
    sched::CpmTask out;
    out.id = id;
    out.duration = duration;
    out.constraint = constraint;
    out.constraint_date = constraint_date;
    out.deadline = deadline;
    out.calendar = calendar;
    return out;
}

sched::CpmLink link(std::int32_t pred, std::int32_t succ,
                    sched::LinkType type = sched::LinkType::finish_to_start,
                    sched::ticks lag = 0) {
    sched::CpmLink out;
    out.predecessor = pred;
    out.successor = succ;
    out.type = type;
    out.lag = lag;
    return out;
}

// One expected oracle row.
struct Row {
    sched::date32 start;
    sched::date32 finish;
    sched::date32 early_start;
    sched::date32 early_finish;
    sched::date32 late_start;
    sched::date32 late_finish;
    sched::ticks start_slack;
    sched::ticks finish_slack;
    sched::ticks free_slack;
    sched::ticks total_slack;
    bool critical;
    sched::ConstraintType constraint;
};

const sched::CpmTaskResult* find(const sched::CpmResult& result, std::int32_t id) {
    for (const sched::CpmTaskResult& row : result.tasks) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

void check_row(sched_test::TestContext& ctx, const sched::CpmResult& result, std::int32_t id,
               const Row& expected, const std::string& what) {
    const sched::CpmTaskResult* actual = find(result, id);
    if (actual == nullptr) {
        ctx.check(false, what + ": task " + std::to_string(id) + " missing from the result");
        return;
    }
    ctx.equal_date(actual->start, expected.start, what + " start");
    ctx.equal_date(actual->finish, expected.finish, what + " finish");
    ctx.equal_date(actual->early_start, expected.early_start, what + " early start");
    ctx.equal_date(actual->early_finish, expected.early_finish, what + " early finish");
    ctx.equal_date(actual->late_start, expected.late_start, what + " late start");
    ctx.equal_date(actual->late_finish, expected.late_finish, what + " late finish");
    ctx.equal(actual->start_slack, expected.start_slack, what + " start slack");
    ctx.equal(actual->finish_slack, expected.finish_slack, what + " finish slack");
    ctx.equal(actual->free_slack, expected.free_slack, what + " free slack");
    ctx.equal(actual->total_slack, expected.total_slack, what + " total slack");
    ctx.equal(actual->critical, expected.critical, what + " critical");
    ctx.equal(static_cast<int>(actual->constraint), static_cast<int>(expected.constraint),
              what + " effective constraint type");
}

void check_ok(sched_test::TestContext& ctx, const sched::CpmResult& result,
              const std::string& what) {
    ctx.check(result.ok(), what + ": schedule succeeded (error=" +
                                 std::string(sched::to_string(result.error)) + " " +
                                 result.message + ")");
}

void check_error(sched_test::TestContext& ctx, const sched::CpmResult& result,
                 sched::CpmError expected, const std::string& what) {
    ctx.equal(static_cast<int>(result.error), static_cast<int>(expected),
              what + ": error code (message=\"" + result.message + "\")");
    ctx.check(!result.ok(), what + ": not ok()");
}

// --- W3-O1: finishes, weekend skip, mid-day finish, milestone ---------------
// ORACLE: docs/analysis/oracle/w3-o1.json.  Project start Mon 19 Oct 2026 00:00,
// standard calendar, no links.  T2/T3/T4 were stored as SNET(4) because the
// coordinator assigned a start date; T1 has no constraint.
constexpr Row kO1T1{at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0),
                    at(kMon1910, 17, 0), at(kMon2610, 8, 0), at(kMon2610, 17, 0),
                    mins(2400), mins(2400), mins(2400), mins(2400), false,
                    sched::ConstraintType::as_soon_as_possible};
constexpr Row kO1T2{at(kTue2010, 8, 0), at(kMon2610, 17, 0), at(kTue2010, 8, 0),
                    at(kMon2610, 17, 0), at(kTue2010, 8, 0), at(kMon2610, 17, 0),
                    0, 0, 0, 0, true, sched::ConstraintType::start_no_earlier_than};
constexpr Row kO1T3{at(kFri2310, 15, 0), at(kMon2610, 10, 0), at(kFri2310, 15, 0),
                    at(kMon2610, 10, 0), at(kMon2610, 13, 0), at(kMon2610, 17, 0),
                    mins(360), mins(360), mins(360), mins(360), false,
                    sched::ConstraintType::start_no_earlier_than};
constexpr Row kO1T4{at(kWed2110, 8, 0), at(kWed2110, 8, 0), at(kWed2110, 8, 0),
                    at(kWed2110, 8, 0), at(kMon2610, 17, 0), at(kMon2610, 17, 0),
                    mins(1920), mins(1920), mins(1920), mins(1920), false,
                    sched::ConstraintType::start_no_earlier_than};

// --- W3-O4: the eight constraint types, feasible and infeasible -------------
// ORACLE: docs/analysis/oracle/w3-o4.json.  T1 and T2 are 1 d (feasible) or 10 d
// (infeasible) and FS-linked; T2 carries the constraint, date Mon 26 Oct 08:00.
// `constraint` is the type handed to the engine.  The ALAP rows are included on
// purpose: MSP stored them as SNET(4) after the date was assigned, which is what
// the engine's normalise_constraint does too, so they reproduce that migration
// and are NOT a measurement of ALAP scheduling (see the standalone ALAP block).
struct ConstraintCase {
    sched::ConstraintType constraint;
    sched::ticks t1_duration;
    Row t1;
    Row t2;
    bool infeasible;
};

const std::vector<ConstraintCase> kConstraintCases{
    // feasible ASAP (stored as SNET because a date was assigned)
    {sched::ConstraintType::as_soon_as_possible, mins(480),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kFri2310, 8, 0), at(kFri2310, 17, 0), mins(1920), mins(1920), mins(1920), mins(1920),
      false, sched::ConstraintType::as_soon_as_possible},
     {at(kMon2610, 8, 0), at(kMon2610, 17, 0), at(kMon2610, 8, 0), at(kMon2610, 17, 0),
      at(kMon2610, 8, 0), at(kMon2610, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::start_no_earlier_than},
     false},
    // feasible MSO
    {sched::ConstraintType::must_start_on, mins(480),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kFri2310, 8, 0), at(kFri2310, 17, 0), mins(1920), mins(1920), mins(1920), mins(1920),
      false, sched::ConstraintType::as_soon_as_possible},
     {at(kMon2610, 8, 0), at(kMon2610, 17, 0), at(kMon2610, 8, 0), at(kMon2610, 17, 0),
      at(kMon2610, 8, 0), at(kMon2610, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::must_start_on},
     false},
    // feasible MFO: the pinned finish is mid-day (Mon 26 08:00), so the start is
    // the previous Friday 08:00 and the predecessor's late finish Thu 22 17:00.
    {sched::ConstraintType::must_finish_on, mins(480),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kThu2210, 8, 0), at(kThu2210, 17, 0), mins(1440), mins(1440), mins(1440), mins(1440),
      false, sched::ConstraintType::as_soon_as_possible},
     {at(kFri2310, 8, 0), at(kMon2610, 8, 0), at(kFri2310, 8, 0), at(kMon2610, 8, 0),
      at(kFri2310, 8, 0), at(kMon2610, 8, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::must_finish_on},
     false},
    // feasible ALAP: MSP stored the ALAP run as SNET(4) once a date was assigned,
    // so this row reproduces the measured migration -- it is not a test of ALAP
    // scheduling itself (see the standalone ALAP block at the end).
    {sched::ConstraintType::as_late_as_possible, mins(480),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kFri2310, 8, 0), at(kFri2310, 17, 0), mins(1920), mins(1920), mins(1920), mins(1920),
      false, sched::ConstraintType::as_soon_as_possible},
     {at(kMon2610, 8, 0), at(kMon2610, 17, 0), at(kMon2610, 8, 0), at(kMon2610, 17, 0),
      at(kMon2610, 8, 0), at(kMon2610, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::start_no_earlier_than},
     false},
    // feasible SNET: a floor, identical to the ASAP-with-date row here
    {sched::ConstraintType::start_no_earlier_than, mins(480),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kFri2310, 8, 0), at(kFri2310, 17, 0), mins(1920), mins(1920), mins(1920), mins(1920),
      false, sched::ConstraintType::as_soon_as_possible},
     {at(kMon2610, 8, 0), at(kMon2610, 17, 0), at(kMon2610, 8, 0), at(kMon2610, 17, 0),
      at(kMon2610, 8, 0), at(kMon2610, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::start_no_earlier_than},
     false},
    // feasible SNLT: the constraint defines the late dates (Mon 26), and the
    // predecessor's free slack is 0 because the link leaves no gap.
    {sched::ConstraintType::start_no_later_than, mins(480),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kFri2310, 8, 0), at(kFri2310, 17, 0), mins(1920), mins(1920), 0, mins(1920), false,
      sched::ConstraintType::as_soon_as_possible},
     {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
      at(kMon2610, 8, 0), at(kMon2610, 17, 0), mins(1920), mins(1920), mins(1920), mins(1920),
      false, sched::ConstraintType::start_no_later_than},
     false},
    // feasible FNET: a floor on the finish, mid-day (Mon 26 08:00)
    {sched::ConstraintType::finish_no_earlier_than, mins(480),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kThu2210, 8, 0), at(kThu2210, 17, 0), mins(1440), mins(1440), mins(1440), mins(1440),
      false, sched::ConstraintType::as_soon_as_possible},
     {at(kFri2310, 8, 0), at(kMon2610, 8, 0), at(kFri2310, 8, 0), at(kMon2610, 8, 0),
      at(kFri2310, 8, 0), at(kMon2610, 8, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::finish_no_earlier_than},
     false},
    // feasible FNLT: a ceiling later than the schedule, so nothing moves
    {sched::ConstraintType::finish_no_later_than, mins(480),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kMon1910, 8, 0), at(kMon1910, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
      at(kTue2010, 8, 0), at(kTue2010, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::finish_no_later_than},
     false},
    // infeasible ASAP: the 10 d predecessor pushes T2 to Mon 2 Nov
    {sched::ConstraintType::as_soon_as_possible, mins(4800),
     {at(kMon1910, 8, 0), at(kFri3010, 17, 0), at(kMon1910, 8, 0), at(kFri3010, 17, 0),
      at(kMon1910, 8, 0), at(kFri3010, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kMon0211, 8, 0), at(kMon0211, 17, 0), at(kMon0211, 8, 0), at(kMon0211, 17, 0),
      at(kMon0211, 8, 0), at(kMon0211, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::start_no_earlier_than},
     true},
    // infeasible MSO: T2 is pinned 5 working days early -> -2400 slack on both
    {sched::ConstraintType::must_start_on, mins(4800),
     {at(kMon1910, 8, 0), at(kFri3010, 17, 0), at(kMon1910, 8, 0), at(kFri3010, 17, 0),
      at(kMon1210, 8, 0), at(kFri2310, 17, 0), mins(-2400), mins(-2400), 0, mins(-2400), true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kMon2610, 8, 0), at(kMon2610, 17, 0), at(kMon2610, 8, 0), at(kMon2610, 17, 0),
      at(kMon2610, 8, 0), at(kMon2610, 17, 0), mins(-2400), 0, 0, mins(-2400), true,
      sched::ConstraintType::must_start_on},
     true},
    // infeasible MFO: the pinned finish is 6 working days early -> -2880
    {sched::ConstraintType::must_finish_on, mins(4800),
     {at(kMon1910, 8, 0), at(kFri3010, 17, 0), at(kMon1910, 8, 0), at(kFri3010, 17, 0),
      at(kFri0910, 8, 0), at(kThu2210, 17, 0), mins(-2880), mins(-2880), 0, mins(-2880), true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kFri2310, 8, 0), at(kMon2610, 8, 0), at(kFri2310, 8, 0), at(kMon2610, 8, 0),
      at(kFri2310, 8, 0), at(kMon2610, 8, 0), mins(-2880), 0, 0, mins(-2880), true,
      sched::ConstraintType::must_finish_on},
     true},
    // infeasible ALAP: again the stored type was SNET(4), so the row is identical
    // to the SNET one; only the input type differs.
    {sched::ConstraintType::as_late_as_possible, mins(4800),
     {at(kMon1910, 8, 0), at(kFri3010, 17, 0), at(kMon1910, 8, 0), at(kFri3010, 17, 0),
      at(kMon1910, 8, 0), at(kFri3010, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kMon0211, 8, 0), at(kMon0211, 17, 0), at(kMon0211, 8, 0), at(kMon0211, 17, 0),
      at(kMon0211, 8, 0), at(kMon0211, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::start_no_earlier_than},
     true},
    // infeasible SNET: a floor never pulls the task earlier -> no violation
    {sched::ConstraintType::start_no_earlier_than, mins(4800),
     {at(kMon1910, 8, 0), at(kFri3010, 17, 0), at(kMon1910, 8, 0), at(kFri3010, 17, 0),
      at(kMon1910, 8, 0), at(kFri3010, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kMon0211, 8, 0), at(kMon0211, 17, 0), at(kMon0211, 8, 0), at(kMon0211, 17, 0),
      at(kMon0211, 8, 0), at(kMon0211, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::start_no_earlier_than},
     true},
    // infeasible SNLT: the ceiling places T2 on Mon 26 -> -2400
    {sched::ConstraintType::start_no_later_than, mins(4800),
     {at(kMon1910, 8, 0), at(kFri3010, 17, 0), at(kMon1910, 8, 0), at(kFri3010, 17, 0),
      at(kMon1210, 8, 0), at(kFri2310, 17, 0), mins(-2400), mins(-2400), 0, mins(-2400), true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kMon2610, 8, 0), at(kMon2610, 17, 0), at(kMon2610, 8, 0), at(kMon2610, 17, 0),
      at(kMon2610, 8, 0), at(kMon2610, 17, 0), mins(-2400), 0, 0, mins(-2400), true,
      sched::ConstraintType::start_no_later_than},
     true},
    // infeasible FNET: the link-driven finish is later, so the floor is inert
    {sched::ConstraintType::finish_no_earlier_than, mins(4800),
     {at(kMon1910, 8, 0), at(kFri3010, 17, 0), at(kMon1910, 8, 0), at(kFri3010, 17, 0),
      at(kMon1910, 8, 0), at(kFri3010, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kMon0211, 8, 0), at(kMon0211, 17, 0), at(kMon0211, 8, 0), at(kMon0211, 17, 0),
      at(kMon0211, 8, 0), at(kMon0211, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::finish_no_earlier_than},
     true},
    // infeasible FNLT: the ceiling clamps the finish to Mon 26 08:00 -> -2880
    {sched::ConstraintType::finish_no_later_than, mins(4800),
     {at(kMon1910, 8, 0), at(kFri3010, 17, 0), at(kMon1910, 8, 0), at(kFri3010, 17, 0),
      at(kFri0910, 8, 0), at(kThu2210, 17, 0), mins(-2880), mins(-2880), 0, mins(-2880), true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kFri2310, 8, 0), at(kMon2610, 8, 0), at(kFri2310, 8, 0), at(kMon2610, 8, 0),
      at(kFri2310, 8, 0), at(kMon2610, 8, 0), mins(-2880), 0, 0, mins(-2880), true,
      sched::ConstraintType::finish_no_later_than},
     true},
};

// --- W3-O4b: deadlines ------------------------------------------------------
// ORACLE: docs/analysis/oracle/w3-o4b.json.  A 1 d Mon, B 1 d Tue FS, deadline on
// B only.  The deadline never moves a date; it clamps B's late finish.
struct DeadlineCase {
    sched::date32 deadline;
    Row a;
    Row b;
};

const std::vector<DeadlineCase> kDeadlineCases{
    {sched::pack(kTue2010, 8 * 60 * 10),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kFri1610, 8, 0), at(kFri1610, 17, 0), mins(-480), mins(-480), 0, mins(-480), true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
      at(kMon1910, 8, 0), at(kMon1910, 17, 0), mins(-480), mins(-480), 0, mins(-480), true,
      sched::ConstraintType::as_soon_as_possible}},
    {sched::pack(kMon1910, 17 * 60 * 10),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kFri1610, 8, 0), at(kFri1610, 17, 0), mins(-480), mins(-480), 0, mins(-480), true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
      at(kMon1910, 8, 0), at(kMon1910, 17, 0), mins(-480), mins(-480), 0, mins(-480), true,
      sched::ConstraintType::as_soon_as_possible}},
    {sched::pack(kWed2110, 17 * 60 * 10),
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kMon1910, 8, 0), at(kMon1910, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
      at(kTue2010, 8, 0), at(kTue2010, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::as_soon_as_possible}},
};

// --- W3-O4b: CriticalSlackLimit --------------------------------------------
// ORACLE: a 1 d task with 480 min of slack is NOT critical at limit 0 and IS
// critical at limit 1 and 2.  Tasks: A 1 d (no link), B 1 d SNET Tue 20 08:00.
struct CriticalCase {
    std::int32_t limit_days;
    Row a;
    Row b;
};

const std::vector<CriticalCase> kCriticalCases{
    {0,
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kTue2010, 8, 0), at(kTue2010, 17, 0), mins(480), mins(480), mins(480), mins(480), false,
      sched::ConstraintType::as_soon_as_possible},
     {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
      at(kTue2010, 8, 0), at(kTue2010, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::start_no_earlier_than}},
    {1,
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kTue2010, 8, 0), at(kTue2010, 17, 0), mins(480), mins(480), mins(480), mins(480), true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
      at(kTue2010, 8, 0), at(kTue2010, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::start_no_earlier_than}},
    {2,
     {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
      at(kTue2010, 8, 0), at(kTue2010, 17, 0), mins(480), mins(480), mins(480), mins(480), true,
      sched::ConstraintType::as_soon_as_possible},
     {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
      at(kTue2010, 8, 0), at(kTue2010, 17, 0), 0, 0, 0, 0, true,
      sched::ConstraintType::start_no_earlier_than}},
};

// ORACLE: w3-o4b "slack_baseline" -- the same two tasks with no link.
constexpr Row kSlackBaselineA{at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0),
                              at(kMon1910, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
                              mins(480), mins(480), mins(480), mins(480), false,
                              sched::ConstraintType::as_soon_as_possible};
constexpr Row kSlackBaselineB{at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0),
                              at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
                              0, 0, 0, 0, true,
                              sched::ConstraintType::start_no_earlier_than};

constexpr sched::date32 kConstraintDate = at(kMon2610, 8, 0);

}  // namespace

int main() {
    sched_test::TestContext ctx;
    const sched::Calendar calendar = standard_calendar();

    // --- the two primitives the CPM bounds are built on ---------------------
    // Calendar::align_back is the backward counterpart of next_start; the CPM
    // oracle pins it through the late link bound (a successor late start of
    // Mon 08:00 gives a predecessor late finish of the previous Friday 17:00)
    // and through the deadline clamp.
    ctx.equal_date(calendar.align_back(at(kMon2610, 8, 0)), at(kFri2310, 17, 0),
                   "align_back: an interval start resolves to the previous finish");
    ctx.equal_date(calendar.align_back(at(kMon2610, 17, 0)), at(kMon2610, 17, 0),
                   "align_back: a finish instant is its own answer");
    ctx.equal_date(calendar.align_back(at(kMon2610, 10, 0)), at(kMon2610, 10, 0),
                   "align_back: strictly inside an interval is its own answer");
    ctx.equal_date(calendar.align_back(at(kMon2610, 12, 0)), at(kMon2610, 12, 0),
                   "align_back: an interval end inside the lunch gap is unchanged");
    ctx.equal_date(calendar.align_back(at(15638, 10, 0)), at(kFri2310, 17, 0),
                   "align_back: Saturday 10:00 resolves to Friday 17:00");
    ctx.equal(calendar.align_back(sched::kDateNa), sched::kDateNa, "align_back: NA stays NA");

    // normalise_constraint: measured migration ASAP+date -> SNET(4).
    ctx.equal(static_cast<int>(sched::normalise_constraint(
                  sched::ConstraintType::as_soon_as_possible, at(kMon2610, 8, 0))),
              static_cast<int>(sched::ConstraintType::start_no_earlier_than),
              "normalise_constraint: ASAP with a date becomes SNET");
    ctx.equal(static_cast<int>(sched::normalise_constraint(
                  sched::ConstraintType::as_soon_as_possible, sched::kDateNa)),
              static_cast<int>(sched::ConstraintType::as_soon_as_possible),
              "normalise_constraint: ASAP without a date stays ASAP");
    ctx.equal(static_cast<int>(sched::normalise_constraint(
                  sched::ConstraintType::must_start_on, at(kMon2610, 8, 0))),
              static_cast<int>(sched::ConstraintType::must_start_on),
              "normalise_constraint: a date-bearing type is unchanged");
    // The w3-o4.json ALAP run also came back stored as 4; same rule, marked [I]
    // for ALAP because no clean re-test exists yet.
    ctx.equal(static_cast<int>(sched::normalise_constraint(
                  sched::ConstraintType::as_late_as_possible, at(kMon2610, 8, 0))),
              static_cast<int>(sched::ConstraintType::start_no_earlier_than),
              "normalise_constraint: ALAP with a date becomes SNET");
    ctx.equal(static_cast<int>(sched::normalise_constraint(
                  sched::ConstraintType::as_late_as_possible, sched::kDateNa)),
              static_cast<int>(sched::ConstraintType::as_late_as_possible),
              "normalise_constraint: ALAP without a date stays ALAP");

    // --- W3-O1 ORACLE: finishes, weekend skip, mid-day finish, milestone ----
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        // T2/T3/T4 are declared ASAP *with a date* on purpose: the oracle stored
        // them as SNET(4) after the date was assigned, and the engine reproduces
        // that migration (normalise_constraint).
        const std::vector<sched::CpmTask> tasks{
            task(1, mins(480)),
            task(2, mins(2400), sched::ConstraintType::as_soon_as_possible, at(kTue2010, 8, 0)),
            task(3, mins(240), sched::ConstraintType::as_soon_as_possible, at(kFri2310, 15, 0)),
            task(4, 0, sched::ConstraintType::as_soon_as_possible, at(kWed2110, 8, 0)),
        };
        const sched::CpmResult result = sched::schedule_tasks(tasks, {}, calendar, options);
        check_ok(ctx, result, "O1");
        check_row(ctx, result, 1, kO1T1, "O1/T1 Monday 1 day");
        check_row(ctx, result, 2, kO1T2, "O1/T2 5 days from Tuesday");
        check_row(ctx, result, 3, kO1T3, "O1/T3 4 h from Friday 15:00");
        check_row(ctx, result, 4, kO1T4, "O1/T4 milestone");
        ctx.equal_date(result.project_finish, at(kMon2610, 17, 0), "O1 project finish");
        ctx.equal(result.order.size(), std::size_t{4}, "O1 topological order size");
    }

    // --- milestone-probe ORACLE: a 0-duration task at an interval end -------
    // ORACLE: docs/analysis/oracle/milestone-probe.json (Project's own save
    // corpus/probe/milestone1.mpp).  Project start Mon 2026-10-19 00:00, standard
    // calendar, one link per row:
    //   ID1 1 d, no link                     -> Mon 19 08:00-17:00 (anchor)
    //   ID2 0 d FS+0  after ID1              -> start == finish Mon 19 17:00
    //   ID3 0 d FS+50d after ID1             -> start == finish Mon 12/28 17:00
    //   ID4 0 d, Start assigned Wed 21 17:00 -> MSP normalised it: the stored
    //                                          constraint is SNET Thu 22 08:00
    //   ID5 0 d SS+0 after ID1               -> start == finish Mon 19 08:00
    //   ID6 1 d SNET Fri 23 08:00            -> Fri 23 08:00-17:00
    //   ID7 0 d FS+0 after ID6               -> start == finish Fri 23 17:00
    //
    // MEASURED RULE: a start bound that lands on an interval end is aligned
    // forward for duration > 0 (W3-O3: the same FS+0 on a 1 d successor is
    // Tue 08:00) but NOT for duration == 0 -- a milestone keeps the bound instant
    // even at 17:00.  ID2/ID3/ID7 are the discriminating rows; ID3 additionally
    // rules out aligning the *result* (AddWork already leaves 17:00 for the 50 d
    // lag, and snapping it would give Tue 12/29 08:00).
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{
            task(1, mins(480)),
            task(2, 0),
            task(3, 0),
            // ID4: the oracle's stored value, not the 17:00 the coordinator
            // assigned: Project rewrote the assigned start and stored SNET(4) at
            // Thu 22 08:00 (so a 17:00 SNET input is NOT what this row measures).
            task(4, 0, sched::ConstraintType::start_no_earlier_than, at(kThu2210, 8, 0)),
            task(5, 0),
            task(6, mins(480), sched::ConstraintType::start_no_earlier_than, at(kFri2310, 8, 0)),
            task(7, 0),
        };
        const std::vector<sched::CpmLink> links{
            link(1, 2, sched::LinkType::finish_to_start, 0),
            link(1, 3, sched::LinkType::finish_to_start, mins(24000)),
            link(1, 5, sched::LinkType::start_to_start, 0),
            link(6, 7, sched::LinkType::finish_to_start, 0),
        };
        const sched::CpmResult result = sched::schedule_tasks(tasks, links, calendar, options);
        check_ok(ctx, result, "MS");
        const Row id1{at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0),
                      at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
                      0, 0, 0, 0, true, sched::ConstraintType::as_soon_as_possible};
        const Row id2{at(kMon1910, 17, 0), at(kMon1910, 17, 0), at(kMon1910, 17, 0),
                      at(kMon1910, 17, 0), at(kMon2812, 17, 0), at(kMon2812, 17, 0),
                      mins(24000), mins(24000), mins(24000), mins(24000), false,
                      sched::ConstraintType::as_soon_as_possible};
        const Row id3{at(kMon2812, 17, 0), at(kMon2812, 17, 0), at(kMon2812, 17, 0),
                      at(kMon2812, 17, 0), at(kMon2812, 17, 0), at(kMon2812, 17, 0),
                      0, 0, 0, 0, true, sched::ConstraintType::as_soon_as_possible};
        const Row id4{at(kThu2210, 8, 0), at(kThu2210, 8, 0), at(kThu2210, 8, 0),
                      at(kThu2210, 8, 0), at(kMon2812, 17, 0), at(kMon2812, 17, 0),
                      mins(23040), mins(23040), mins(23040), mins(23040), false,
                      sched::ConstraintType::start_no_earlier_than};
        const Row id5{at(kMon1910, 8, 0), at(kMon1910, 8, 0), at(kMon1910, 8, 0),
                      at(kMon1910, 8, 0), at(kMon2812, 17, 0), at(kMon2812, 17, 0),
                      mins(24480), mins(24480), mins(24480), mins(24480), false,
                      sched::ConstraintType::as_soon_as_possible};
        const Row id6{at(kFri2310, 8, 0), at(kFri2310, 17, 0), at(kFri2310, 8, 0),
                      at(kFri2310, 17, 0), at(kMon2812, 8, 0), at(kMon2812, 17, 0),
                      mins(22080), mins(22080), 0, mins(22080), false,
                      sched::ConstraintType::start_no_earlier_than};
        const Row id7{at(kFri2310, 17, 0), at(kFri2310, 17, 0), at(kFri2310, 17, 0),
                      at(kFri2310, 17, 0), at(kMon2812, 17, 0), at(kMon2812, 17, 0),
                      mins(22080), mins(22080), mins(22080), mins(22080), false,
                      sched::ConstraintType::as_soon_as_possible};
        check_row(ctx, result, 1, id1, "MS/ID1 1 d anchor");
        check_row(ctx, result, 2, id2, "MS/ID2 0 d FS+0 -> 17:00");
        check_row(ctx, result, 3, id3, "MS/ID3 0 d FS+50d -> 17:00");
        check_row(ctx, result, 4, id4, "MS/ID4 0 d stored SNET Thu 08:00");
        check_row(ctx, result, 5, id5, "MS/ID5 0 d SS+0 -> 08:00");
        check_row(ctx, result, 6, id6, "MS/ID6 1 d SNET Fri 08:00");
        check_row(ctx, result, 7, id7, "MS/ID7 0 d FS+0 -> 17:00");
        ctx.equal_date(result.project_finish, at(kMon2812, 17, 0), "MS project finish");
        // The discriminating contrast: the SAME FS+0 bound on a 1 d successor is
        // displayed two hours earlier, at the next working morning (O3/type1/lag0
        // pins Tue 08:00 after a Mon 17:00 finish).
        const std::vector<sched::CpmTask> d1{task(1, mins(480)), task(2, mins(480))};
        const sched::CpmResult contrast =
            sched::schedule_tasks(d1, {link(1, 2)}, calendar, options);
        check_ok(ctx, contrast, "MS/contrast");
        ctx.check(contrast.tasks.at(1).start != id2.start,
                  "MS/contrast: FS+0 snaps for a 1 d successor but not for a milestone");
    }

    // --- a started task: actual start pins START, not FINISH ----------------
    // ORACLE-shaped, from Project's own save corpus/probe/project_a-recalc.mpp:
    //   uid 10849 (leaf, 60 d, 40 % complete): row +186 StartSlack 0, row +190
    //   TotalSlack 16320 min = (row +116 LateFinish 08-14 17:00) - (row +100
    //   EarlyFinish 06-29 17:00); row +112 LateStart == the actual start.
    //   uid 10851 the same shape with 39360 min.
    // The measured rule is that an actual start pins the LATE START to the
    // actual start (StartSlack 0) but the LATE FINISH still comes from the
    // successors -- unlike an MSO constraint, which pins both and reports
    // TotalSlack 0 (w3-o4 MSO rows).  The graph below is a small stand-in for
    // 10849's chain: 1 d started Mon 10/19 -> B 1 d, plus an independent 5 d C
    // that makes the project finish Fri 10/23 17:00.  B's late start is Fri
    // 10/23 08:00, so A's late finish is Thu 10/22 17:00 and A's finish slack is
    // 3 working days (1440 min).  The dates are INFERENCE arithmetic; the SLACK
    // SHAPE (StartSlack 0 with TotalSlack = LF-EF > 0) is the measured rule.
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        sched::CpmTask started = task(1, mins(480));
        started.actual_start = at(kMon1910, 8, 0);
        const std::vector<sched::CpmTask> tasks{started, task(2, mins(480)),
                                                task(3, mins(2400))};
        const std::vector<sched::CpmLink> links{link(1, 2)};
        const sched::CpmResult result = sched::schedule_tasks(tasks, links, calendar, options);
        check_ok(ctx, result, "ACTUAL");
        const sched::CpmTaskResult* a = find(result, 1);
        ctx.check(a != nullptr, "ACTUAL: the started task is present");
        if (a != nullptr) {
            ctx.equal_date(a->start, at(kMon1910, 8, 0), "ACTUAL: start is the actual start");
            ctx.equal_date(a->finish, at(kMon1910, 17, 0), "ACTUAL: finish is start + duration");
            ctx.equal_date(a->late_start, at(kMon1910, 8, 0),
                           "ACTUAL: late start is pinned to the actual start");
            ctx.equal_date(a->late_finish, at(kThu2210, 17, 0),
                           "ACTUAL: late finish stays successor-derived");
            ctx.equal(a->start_slack, sched::ticks{0}, "ACTUAL: start slack 0");
            ctx.equal(a->finish_slack, mins(1440), "ACTUAL: finish slack 1440 min");
            // The measured rule: TotalSlack is LF-EF, not min(LS-ES, LF-EF).
            ctx.equal(a->total_slack, mins(1440), "ACTUAL: total slack is the finish slack");
            ctx.equal(a->critical, false, "ACTUAL: 960 min of slack is not critical");
        }
        // Contrast: the same graph with a REAL MSO constraint pins both sides
        // (the measured w3-o4 MSO shape), so the total slack is 0.
        sched::CpmTask mso = task(1, mins(480), sched::ConstraintType::must_start_on,
                                  at(kMon1910, 8, 0));
        const std::vector<sched::CpmTask> mso_tasks{mso, task(2, mins(480)),
                                                   task(3, mins(2400))};
        const sched::CpmResult mso_result =
            sched::schedule_tasks(mso_tasks, links, calendar, options);
        check_ok(ctx, mso_result, "ACTUAL/MSO contrast");
        const sched::CpmTaskResult* m = find(mso_result, 1);
        ctx.check(m != nullptr, "ACTUAL/MSO contrast: task present");
        if (m != nullptr) {
            ctx.equal(m->total_slack, sched::ticks{0},
                      "ACTUAL/MSO contrast: MSO pins the late finish too");
        }
    }

    // --- a COMPLETED successor does not bound its predecessor --------------
    // ORACLE: Project's own save corpus/probe/project_a-recalc.mpp, uid 10851 (40 %
    // complete) with successors 10757 (FS+0, 100 % complete) and 10870 (FS+0,
    // not started).  Every stored slack word for 10851 equals the bound from
    // 10870 ALONE (+74 28800 min, +116 2026-08-19 17:00, +190 39360 min); the
    // 10757 bound would be 2400 min / 2026-05-04 17:00.  A completed successor
    // has no remaining work, so it cannot be delayed.  The graph below is a
    // small stand-in: A (2 d, started Mon 10/19) -> B (1 d, complete Wed 10/21)
    // and A -> C (1 d, SNET Fri 10/23).  B alone would leave A no slack (LF
    // 10/20 17:00); with B complete, A's LF comes from C and is 10/22 17:00
    // (2 working days = 960 min of total and free slack).  The dates are
    // INFERENCE arithmetic; the RULE is the measured 10851 row.
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        sched::CpmTask a = task(1, mins(960));
        a.actual_start = at(kMon1910, 8, 0);
        sched::CpmTask b = task(2, mins(480), sched::ConstraintType::must_finish_on,
                                at(kWed2110, 17, 0));
        b.actual_start = at(kWed2110, 8, 0);
        b.actual_finish = at(kWed2110, 17, 0);
        const std::vector<sched::CpmTask> tasks{
            a, b, task(3, mins(480), sched::ConstraintType::start_no_earlier_than,
                       at(kFri2310, 8, 0))};
        const std::vector<sched::CpmLink> links{link(1, 2), link(1, 3)};
        const sched::CpmResult result = sched::schedule_tasks(tasks, links, calendar, options);
        check_ok(ctx, result, "COMPLETED");
        const sched::CpmTaskResult* pred = find(result, 1);
        ctx.check(pred != nullptr, "COMPLETED: the in-progress predecessor is present");
        if (pred != nullptr) {
            ctx.equal_date(pred->late_finish, at(kThu2210, 17, 0),
                           "COMPLETED: late finish comes from the unfinished successor");
            ctx.equal(pred->total_slack, mins(960),
                      "COMPLETED: total slack is the C bound, not B's 0");
            ctx.equal(pred->free_slack, mins(960),
                      "COMPLETED: free slack ignores the completed successor");
        }
        // Contrast: the same graph with B merely pinned (no actual finish) gives
        // A the tight late finish from B and no slack.
        std::vector<sched::CpmTask> open_tasks = tasks;
        open_tasks[1].actual_finish = sched::kDateNa;
        const sched::CpmResult open = sched::schedule_tasks(open_tasks, links, calendar, options);
        check_ok(ctx, open, "COMPLETED/contrast");
        const sched::CpmTaskResult* open_pred = find(open, 1);
        ctx.check(open_pred != nullptr, "COMPLETED/contrast: predecessor present");
        if (open_pred != nullptr) {
            ctx.equal(open_pred->total_slack, sched::ticks{0},
                      "COMPLETED/contrast: an unfinished MFO successor does bound it");
        }
    }

    // --- W3-O4 ORACLE: the eight constraint types ---------------------------
    for (const ConstraintCase& test_case : kConstraintCases) {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{
            task(1, test_case.t1_duration),
            task(2, mins(480), test_case.constraint, kConstraintDate),
        };
        const std::vector<sched::CpmLink> links{link(1, 2)};
        const sched::CpmResult result = sched::schedule_tasks(tasks, links, calendar, options);
        const std::string name = "O4/" + std::string(test_case.infeasible ? "infeasible" : "feasible") +
                                 "/type" + std::to_string(static_cast<int>(test_case.constraint));
        check_ok(ctx, result, name);
        check_row(ctx, result, 1, test_case.t1, name + "/T1");
        check_row(ctx, result, 2, test_case.t2, name + "/T2");
    }

    // --- W3-O4b ORACLE: deadlines ------------------------------------------
    for (const DeadlineCase& test_case : kDeadlineCases) {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{
            task(1, mins(480)),
            task(2, mins(480), sched::ConstraintType::as_soon_as_possible, sched::kDateNa,
                 test_case.deadline),
        };
        const std::vector<sched::CpmLink> links{link(1, 2)};
        const sched::CpmResult result = sched::schedule_tasks(tasks, links, calendar, options);
        const std::string name = "O4b/deadline/" + sched::to_string(test_case.deadline);
        check_ok(ctx, result, name);
        check_row(ctx, result, 1, test_case.a, name + "/A");
        check_row(ctx, result, 2, test_case.b, name + "/B");
    }

    // --- W3-O4b ORACLE: CriticalSlackLimit ---------------------------------
    for (const CriticalCase& test_case : kCriticalCases) {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        options.critical_slack_limit_days = test_case.limit_days;
        const std::vector<sched::CpmTask> tasks{
            task(1, mins(480)),
            task(2, mins(480), sched::ConstraintType::start_no_earlier_than, at(kTue2010, 8, 0)),
        };
        const sched::CpmResult result = sched::schedule_tasks(tasks, {}, calendar, options);
        const std::string name = "O4b/critical-limit/" + std::to_string(test_case.limit_days);
        check_ok(ctx, result, name);
        check_row(ctx, result, 1, test_case.a, name + "/A");
        check_row(ctx, result, 2, test_case.b, name + "/B");
    }

    // ORACLE: w3-o4b slack_baseline (no link, B pinned to Tue 20 08:00 by SNET).
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{
            task(1, mins(480)),
            task(2, mins(480), sched::ConstraintType::start_no_earlier_than, at(kTue2010, 8, 0)),
        };
        const sched::CpmResult result = sched::schedule_tasks(tasks, {}, calendar, options);
        check_ok(ctx, result, "O4b/slack-baseline");
        check_row(ctx, result, 1, kSlackBaselineA, "O4b/slack-baseline/A");
        check_row(ctx, result, 2, kSlackBaselineB, "O4b/slack-baseline/B");
    }

    // --- the ASAP + ConstraintDate migration, measured as type 0 -> 4 -------
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{
            task(1, mins(480), sched::ConstraintType::as_soon_as_possible, at(kMon2610, 8, 0)),
        };
        const sched::CpmResult result = sched::schedule_tasks(tasks, {}, calendar, options);
        check_ok(ctx, result, "ASAP+date");
        ctx.equal(static_cast<int>(result.tasks.at(0).constraint),
                  static_cast<int>(sched::ConstraintType::start_no_earlier_than),
                  "ASAP+date becomes SNET(4)");
        ctx.equal_date(result.tasks.at(0).start, at(kMon2610, 8, 0), "ASAP+date start");
        // No constraint at all keeps ASAP and starts at the project start.
        const std::vector<sched::CpmTask> plain{task(1, mins(480))};
        const sched::CpmResult plain_result = sched::schedule_tasks(plain, {}, calendar, options);
        check_ok(ctx, plain_result, "ASAP");
        ctx.equal(static_cast<int>(plain_result.tasks.at(0).constraint),
                  static_cast<int>(sched::ConstraintType::as_soon_as_possible),
                  "no date keeps ASAP(0)");
        ctx.equal_date(plain_result.tasks.at(0).start, at(kMon1910, 8, 0),
                       "ASAP starts at the first working instant, not at midnight");
    }

    // --- W3-O3 ORACLE: FS / SS / FF / SF with lag and lead ------------------
    // ORACLE: docs/analysis/oracle/w3-o3.json (and w3-o3b.json, the clean
    // re-run from one fresh project per row).  T1 = 1 d Mon 2026-10-19
    // 08:00-17:00, T2 = 1 d, one link T1->T2 per row, CalculateProject after
    // each.  Every row below is the measured Start/Finish, Early*, Late* and
    // slack of BOTH tasks.
    //
    // The measured link rules (each one disagreed with the spec §4.2 formula):
    //   * FS is anchored on the next working instant after the predecessor's
    //     finish (FS+0 -> Tue 08:00, FS-1d -> Mon 08:00, FS+2h -> Tue 10:00);
    //   * an SS start bound PLACES the start with no project-start floor, so
    //     SS-1d starts Fri 2026-10-16 08:00, BEFORE the project start;
    //   * an FF bound is anchored on the predecessor's FINISH INSTANT, so FF+0
    //     finishes Mon 17:00 and FF+1d Tue 17:00, not the next morning;
    //   * an SF bound is a PLACED finish: SF+0 finishes Mon 08:00, a mid-day
    //     instant, while T2 starts Fri 16 08:00;
    //   * on the backward pass a finish bound is lagged then aligned BACK to a
    //     finish instant, so FS+1d gives T1 LateFinish Mon 19 17:00 (the old
    //     start-aligned inverse gave Tue 20 08:00).
    //
    // Both rows the spec §4.2 calls open are now pinned too:
    //   * FF-1d is explained by docs/analysis/oracle/lagff-probe.json: the lead IS
    //     applied (it lands on the previous interval end, Fri 16 17:00) and the
    //     project-start floor lifts that to Mon 17:00, which is why the old row
    //     looked like "the lag was dropped";
    //   * SF+1d is arithmetic, not an anomaly: AddWork(pred.ES Mon 08:00, 1 d) is
    //     Mon 17:00 (a working day ends the same day), so the placed finish is
    //     exactly the measured Mon 08:00-17:00.
    {
        struct LinkCase {
            sched::LinkType type;
            sched::ticks lag;
            Row t1;
            Row t2;
        };
        // Every measured link row has the same T1: it never moves.
        const Row t1_mon{at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0),
                         at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
                         0, 0, 0, 0, true, sched::ConstraintType::as_soon_as_possible};
        const std::vector<LinkCase> cases{
            // FS: T2 starts the next working morning; a lead may move it back to
            // the predecessor's own day (FS-1d -> Mon 08:00).
            {sched::LinkType::finish_to_start, 0, t1_mon,
             {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
              at(kTue2010, 8, 0), at(kTue2010, 17, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            {sched::LinkType::finish_to_start, mins(480), t1_mon,
             {at(kWed2110, 8, 0), at(kWed2110, 17, 0), at(kWed2110, 8, 0), at(kWed2110, 17, 0),
              at(kWed2110, 8, 0), at(kWed2110, 17, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            {sched::LinkType::finish_to_start, -mins(480), t1_mon,
             {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
              at(kMon1910, 8, 0), at(kMon1910, 17, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            {sched::LinkType::finish_to_start, mins(120), t1_mon,
             {at(kTue2010, 10, 0), at(kWed2110, 10, 0), at(kTue2010, 10, 0), at(kWed2110, 10, 0),
              at(kTue2010, 10, 0), at(kWed2110, 10, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            // SS: the bound is a start; SS-1d starts before the project start and
            // therefore carries two days of slack (the measured value).
            {sched::LinkType::start_to_start, 0, t1_mon,
             {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
              at(kMon1910, 8, 0), at(kMon1910, 17, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            {sched::LinkType::start_to_start, mins(480), t1_mon,
             {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
              at(kTue2010, 8, 0), at(kTue2010, 17, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            {sched::LinkType::start_to_start, -mins(480), t1_mon,
             {at(kFri1610, 8, 0), at(kFri1610, 17, 0), at(kFri1610, 8, 0), at(kFri1610, 17, 0),
              at(kMon1910, 8, 0), at(kMon1910, 17, 0), mins(480), mins(480), mins(480), mins(480),
              false, sched::ConstraintType::as_soon_as_possible}},
            {sched::LinkType::start_to_start, mins(120), t1_mon,
             {at(kMon1910, 10, 0), at(kTue2010, 10, 0), at(kMon1910, 10, 0), at(kTue2010, 10, 0),
              at(kMon1910, 10, 0), at(kTue2010, 10, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            // FF: the bound is the predecessor's finish INSTANT (17:00), so FF+0
            // is the same day, not the next one.
            {sched::LinkType::finish_to_finish, 0, t1_mon,
             {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
              at(kMon1910, 8, 0), at(kMon1910, 17, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            {sched::LinkType::finish_to_finish, mins(480), t1_mon,
             {at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0), at(kTue2010, 17, 0),
              at(kTue2010, 8, 0), at(kTue2010, 17, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            {sched::LinkType::finish_to_finish, mins(120), t1_mon,
             {at(kMon1910, 10, 0), at(kTue2010, 10, 0), at(kMon1910, 10, 0), at(kTue2010, 10, 0),
              at(kMon1910, 10, 0), at(kTue2010, 10, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            // FF-1d: the row spec §4.2 called "unchanged / lag dropped".  The lead
            // is applied -- Fri 16 17:00 -- and the project-start floor lifts it to
            // Mon 17:00, the zero-lag date (lagff-probe).
            {sched::LinkType::finish_to_finish, -mins(480), t1_mon,
             {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
              at(kMon1910, 8, 0), at(kMon1910, 17, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
            // SF: the bound is a PLACED finish, so the start is back-computed
            // with SubWork and can sit before the project start (SF+0 finishes
            // Mon 08:00 with the start Fri 16 08:00).
            {sched::LinkType::start_to_finish, 0, t1_mon,
             {at(kFri1610, 8, 0), at(kMon1910, 8, 0), at(kFri1610, 8, 0), at(kMon1910, 8, 0),
              at(kMon1910, 8, 0), at(kMon1910, 17, 0), mins(480), mins(480), mins(480), mins(480),
              false, sched::ConstraintType::as_soon_as_possible}},
            {sched::LinkType::start_to_finish, -mins(480), t1_mon,
             {at(kThu1510, 8, 0), at(kFri1610, 8, 0), at(kThu1510, 8, 0), at(kFri1610, 8, 0),
              at(kMon1910, 8, 0), at(kMon1910, 17, 0), mins(960), mins(960), mins(960), mins(960),
              false, sched::ConstraintType::as_soon_as_possible}},
            {sched::LinkType::start_to_finish, mins(120), t1_mon,
             {at(kFri1610, 10, 0), at(kMon1910, 10, 0), at(kFri1610, 10, 0), at(kMon1910, 10, 0),
              at(kMon1910, 8, 0), at(kMon1910, 17, 0), mins(360), mins(360), mins(360), mins(360),
              false, sched::ConstraintType::as_soon_as_possible}},
            // SF+1d: not anomalous (spec §4.2's "instead of a 10/20 08:00 finish"
            // is an arithmetic slip): AddWork(Mon 08:00, 1 d) = Mon 17:00 the same
            // day, so the placed finish is the measured Mon 08:00-17:00.
            {sched::LinkType::start_to_finish, mins(480), t1_mon,
             {at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0), at(kMon1910, 17, 0),
              at(kMon1910, 8, 0), at(kMon1910, 17, 0), 0, 0, 0, 0, true,
              sched::ConstraintType::as_soon_as_possible}},
        };
        for (const LinkCase& test_case : cases) {
            sched::CpmOptions options;
            options.project_start = kProjectStart;
            const std::vector<sched::CpmTask> tasks{task(1, mins(480)), task(2, mins(480))};
            const std::vector<sched::CpmLink> links{
                link(1, 2, test_case.type, test_case.lag)};
            const sched::CpmResult result = sched::schedule_tasks(tasks, links, calendar, options);
            const std::string name = "O3/type" +
                                     std::to_string(static_cast<int>(test_case.type)) + "/lag" +
                                     std::to_string(test_case.lag);
            check_ok(ctx, result, name);
            check_row(ctx, result, 1, test_case.t1, name + "/T1");
            check_row(ctx, result, 2, test_case.t2, name + "/T2");
        }
    }

    // --- lagff-probe ORACLE: FF lag, including a lead -----------------------
    // ORACLE: docs/analysis/oracle/lagff-probe.json (Project's own save
    // corpus/probe/lagff1.mpp and its MSPDI export lagff1.xml).  T1 = 5 d
    // Mon 2026-10-19 08:00 -> Fri 2026-10-23 17:00; each successor is 1 d with
    // exactly one FF link to T1:
    //   T2 FF-1d  -> Thu 22 08:00-17:00  (the lead IS applied; not dropped)
    //   T3 FF-3d  -> Tue 20 08:00-17:00
    //   T4 FF-6d  -> Mon 19 08:00-17:00  (the bound is Thu 10/15 17:00, before
    //                                     the project start; floored to Mon 19)
    //   T5 FF+0   -> Fri 23 08:00-17:00
    //   T6 FF-1ed -> Thu 22 08:00-17:00  (LagFormat 8 = elapsed day; pinned as
    //                                     the GATE, see below)
    //
    // MEASURED RULE: the FF finish bound is AddWork(pred.EF, lag) on the RAW
    // finish instant, and a lead lands on the previous interval END (FF-1d Thu
    // 22 17:00, FF-3d Tue 20, FF-6d Thu 10/15).  The successor's start is
    // SubWork(finish, duration) and is FLOORED at the project start (FF-6d).
    // The floor is what makes the old w3-o3 FF-1d row look like "the lag was
    // dropped" (O3/type0/lag-4800 pins that row).
    {
        // All four working-lag rows in ONE graph, exactly as the fixture was
        // measured (T1 has four successors at once).  The per-link late dates
        // cannot be checked link by link: T1's late finish is the minimum over
        // the four FF bounds, so the oracle's T1 row only exists with all four.
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{task(1, mins(2400)), task(2, mins(480)),
                                                task(3, mins(480)), task(4, mins(480)),
                                                task(5, mins(480))};
        const std::vector<sched::CpmLink> links{
            link(1, 2, sched::LinkType::finish_to_finish, -mins(480)),
            link(1, 3, sched::LinkType::finish_to_finish, -mins(1440)),
            link(1, 4, sched::LinkType::finish_to_finish, -mins(2880)),
            link(1, 5, sched::LinkType::finish_to_finish, 0),
        };
        const sched::CpmResult result = sched::schedule_tasks(tasks, links, calendar, options);
        check_ok(ctx, result, "LAGFF");
        // T1: the predecessor.  Its late finish is the minimum over the four FF
        // bounds; the FF+0 successor (T5) leaves no gap, so T1 is critical with
        // 0 slack even though its three leads alone would allow slack (the
        // lead-clamp rule in cpm.cpp's backward FF case).
        const Row t1{at(kMon1910, 8, 0), at(kFri2310, 17, 0), at(kMon1910, 8, 0),
                     at(kFri2310, 17, 0), at(kMon1910, 8, 0), at(kFri2310, 17, 0),
                     0, 0, 0, 0, true, sched::ConstraintType::as_soon_as_possible};
        // T2: FF-1d.  The lead is applied: finish Thu 22 17:00, one day of slack.
        const Row t2{at(kThu2210, 8, 0), at(kThu2210, 17, 0), at(kThu2210, 8, 0),
                     at(kThu2210, 17, 0), at(kFri2310, 8, 0), at(kFri2310, 17, 0),
                     mins(480), mins(480), mins(480), mins(480), false,
                     sched::ConstraintType::as_soon_as_possible};
        // T3: FF-3d -> Tue 20, three days of slack.
        const Row t3{at(kTue2010, 8, 0), at(kTue2010, 17, 0), at(kTue2010, 8, 0),
                     at(kTue2010, 17, 0), at(kFri2310, 8, 0), at(kFri2310, 17, 0),
                     mins(1440), mins(1440), mins(1440), mins(1440), false,
                     sched::ConstraintType::as_soon_as_possible};
        // T4: FF-6d would finish Thu 10/15 17:00, i.e. before the project start;
        // the placement is floored to Mon 19 08:00-17:00 (four days of slack).
        const Row t4{at(kMon1910, 8, 0), at(kMon1910, 17, 0), at(kMon1910, 8, 0),
                     at(kMon1910, 17, 0), at(kFri2310, 8, 0), at(kFri2310, 17, 0),
                     mins(1920), mins(1920), mins(1920), mins(1920), false,
                     sched::ConstraintType::as_soon_as_possible};
        // T5: FF+0 -> the predecessor's own finish, critical.
        const Row t5{at(kFri2310, 8, 0), at(kFri2310, 17, 0), at(kFri2310, 8, 0),
                     at(kFri2310, 17, 0), at(kFri2310, 8, 0), at(kFri2310, 17, 0),
                     0, 0, 0, 0, true, sched::ConstraintType::as_soon_as_possible};
        check_row(ctx, result, 1, t1, "LAGFF/T1 5 d anchor");
        check_row(ctx, result, 2, t2, "LAGFF/T2 FF-1d");
        check_row(ctx, result, 3, t3, "LAGFF/T3 FF-3d");
        check_row(ctx, result, 4, t4, "LAGFF/T4 FF-6d floored");
        check_row(ctx, result, 5, t5, "LAGFF/T5 FF+0");
        ctx.equal_date(result.project_finish, at(kFri2310, 17, 0), "LAGFF project finish");
        // T6: FF-1ed (LagFormat 8), MEASURED (lagff-probe.json) as Thu 22 08:00-17:00
        // -- exactly the FF-1d row, because Thursday is a working day.  That
        // fixture does NOT separate elapsed from working lag.
        // MEASURED (wave 9b, corpus/probe/w9-elapsedlag.mpp): an elapsed lag is
        // plain wall-clock arithmetic on the finish instant -- see the dedicated
        // ELAPSED block below.  This test used to assert that the engine REFUSED
        // an elapsed lag (CpmError::elapsed_lag_unsupported); wave 9b measured the
        // rule and removed the refusal, so the old assertion encoded a rule that
        // is now known to be incomplete rather than wrong.
    }

    // --- ELAPSED lag: measured on corpus/probe/w9-elapsedlag.mpp ------------
    // ORACLE: docs/analysis/oracle/w9-elapsedlag.json.  Standard calendar,
    // project start Mon 2026-10-19 08:00.  T1 3 d finishes Wed 10-21 17:00;
    // T5 5 d finishes Fri 10-23 17:00.  Project's own save stores link-row +18
    // (LagFormat) = 8 for "2 edays"/"3 edays" and 7 for "2 days", and the lag
    // value is already elapsed ticks (2 edays = 28800 = 48 h, 3 edays = 43200).
    // Measured placements (Start/Finish, minutes of slack):
    //   FS+2 edays from T1 -> Mon 10-26 08:00-17:00   (Fri 17:00 -> next start)
    //   FS+3 edays from T1 -> Mon 10-26 08:00-17:00   (Sat 17:00 -> next start)
    //   FS+2 days  from T1 -> Mon 10-26 08:00-17:00   (Fri 17:00 -> next start)
    //   FS+2 edays from T5 -> Mon 10-26 08:00-17:00   (Sun 17:00 -> next start)
    //   FS+2 days  from T5 -> Wed 10-28 08:00-17:00   (Tue 17:00 -> next start)
    // The FS+3-edays row separates the readings: 3 WORKING days from Wed 17:00
    // would start Tue 10-27, and no elapsed row would land on Mon 10-26.
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{task(1, mins(1440)), task(2, mins(480)),
                                                task(3, mins(480)), task(4, mins(480)), task(5, mins(2400)),
                                                task(6, mins(480)), task(7, mins(480))};
        sched::CpmLink two_elapsed = link(1, 2, sched::LinkType::finish_to_start, mins(2880));
        two_elapsed.elapsed_lag = true;
        sched::CpmLink three_elapsed = link(1, 3, sched::LinkType::finish_to_start, mins(4320));
        three_elapsed.elapsed_lag = true;
        const sched::CpmLink two_working = link(1, 4, sched::LinkType::finish_to_start, mins(960));
        sched::CpmLink five_elapsed = link(5, 6, sched::LinkType::finish_to_start, mins(2880));
        five_elapsed.elapsed_lag = true;
        const sched::CpmLink five_working = link(5, 7, sched::LinkType::finish_to_start, mins(960));
        const std::vector<sched::CpmLink> links{two_elapsed, three_elapsed, two_working,
                                               five_elapsed, five_working};
        const sched::CpmResult result = sched::schedule_tasks(tasks, links, calendar, options);
        check_ok(ctx, result, "ELAPSED");
        const auto start_of = [&result](std::int32_t id) {
            const sched::CpmTaskResult* row = find(result, id);
            return row != nullptr ? row->early_start : sched::kDateNa;
        };
        const auto finish_of = [&result](std::int32_t id) {
            const sched::CpmTaskResult* row = find(result, id);
            return row != nullptr ? row->early_finish : sched::kDateNa;
        };
        ctx.equal_date(finish_of(1), at(kWed2110, 17, 0), "ELAPSED: T1 finishes Wed 17:00");
        ctx.equal_date(start_of(2), at(kMon2610, 8, 0), "ELAPSED: FS+2 edays starts Mon 08:00");
        ctx.equal_date(finish_of(2), at(kMon2610, 17, 0), "ELAPSED: FS+2 edays finishes Mon 17:00");
        ctx.equal_date(start_of(3), at(kMon2610, 8, 0),
                       "ELAPSED: FS+3 edays starts Mon 08:00 (Sat 17:00 + 72 h)");
        ctx.equal_date(start_of(4), at(kMon2610, 8, 0), "ELAPSED: FS+2 days control starts Mon 08:00");
        ctx.equal_date(finish_of(5), at(kFri2310, 17, 0), "ELAPSED: T5 finishes Fri 17:00");
        ctx.equal_date(start_of(6), at(kMon2610, 8, 0), "ELAPSED: FS+2 edays from Fri starts Mon");
        ctx.equal_date(start_of(7), at(kWed2810, 8, 0),
                       "ELAPSED: FS+2 days from Fri starts Wed 10-28");
        // The elapsed read of a 3-elapsed-day lag is NOT the working one.
        ctx.check(start_of(3) != at(kTue2710, 8, 0),
                  "ELAPSED: a 3 edays lag is not scheduled as 3 working days");
    }

    // --- ELAPSED duration: measured on w9-elapsedlag.mpp T8 ------------------
    // ORACLE: docs/analysis/oracle/w9-elapsedlag.json.  T8 is "2 edays": the row
    // stores 28800 ticks (48 h) and DurationFormat (task row +0xa4) = 8, while
    // the working-day tasks in the same file store 7.  Project schedules it Mon
    // 2026-10-19 08:00 -> Wed 2026-10-21 08:00 (wall-clock, not six working
    // days) and stores TotalSlack = 106200 ticks = 10620 min, which is the
    // wall-clock LateFinish - EarlyFinish.
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        sched::CpmTask elapsed;
        elapsed.id = 8;
        elapsed.duration = mins(2880);  // 2 edays = 48 h = 28800 ticks
        elapsed.elapsed_duration = true;
        // A normal task that fixes the project finish at Wed 10-28 17:00, the
        // fixture's finish (T7).
        sched::CpmTask anchor;
        anchor.id = 9;
        anchor.duration = mins(480);
        anchor.constraint = sched::ConstraintType::start_no_earlier_than;
        anchor.constraint_date = at(kWed2810, 8, 0);
        const std::vector<sched::CpmTask> tasks{elapsed, anchor};
        const sched::CpmResult result = sched::schedule_tasks(tasks, {}, calendar, options);
        check_ok(ctx, result, "ELAPSED DURATION");
        const sched::CpmTaskResult* row = find(result, 8);
        ctx.check(row != nullptr, "ELAPSED DURATION: T8 is present");
        if (row != nullptr) {
            ctx.check(row->elapsed_duration, "ELAPSED DURATION: the row is flagged elapsed");
            ctx.equal_date(row->early_start, at(kMon1910, 8, 0),
                           "ELAPSED DURATION: 2 edays starts Mon 10-19 08:00");
            ctx.equal_date(row->early_finish, at(kWed2110, 8, 0),
                           "ELAPSED DURATION: 2 edays finishes Wed 10-21 08:00 (48 h)");
            ctx.equal_date(row->late_finish, at(kWed2810, 17, 0),
                           "ELAPSED DURATION: late finish is the project finish");
            ctx.equal_date(row->late_start, at(kMon2610, 17, 0),
                           "ELAPSED DURATION: late start is 48 wall-clock hours earlier");
            ctx.equal(row->total_slack, mins(10620),
                      "ELAPSED DURATION: TotalSlack is 10620 wall-clock minutes");
            ctx.equal(row->start_slack, mins(10620),
                      "ELAPSED DURATION: StartSlack is wall-clock too");
            ctx.equal(row->free_slack, mins(10620),
                      "ELAPSED DURATION: no successors, so free slack equals total");
        }
        // Control: the same 28800 ticks WITHOUT the elapsed flag is six working
        // days (Mon 10-19 08:00 -> Mon 10-26 17:00), which is the old answer.
        sched::CpmTask working = elapsed;
        working.elapsed_duration = false;
        const sched::CpmResult working_result =
            sched::schedule_tasks({working, anchor}, {}, calendar, options);
        check_ok(ctx, working_result, "ELAPSED DURATION control");
        const sched::CpmTaskResult* control = find(working_result, 8);
        ctx.check(control != nullptr, "ELAPSED DURATION control: T8 is present");
        if (control != nullptr) {
            ctx.equal_date(control->early_finish, at(kMon2610, 17, 0),
                           "ELAPSED DURATION control: without the flag 28800 ticks is "
                           "six working days");
        }
    }

    // --- a chain: slack is 0 all the way down -------------------------------
    // INFERENCE (spec §3.5 W3-P5, unmeasured): A 2 d Mon-Tue -> B 3 d.
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{task(1, mins(960)), task(2, mins(1440))};
        const std::vector<sched::CpmLink> links{link(1, 2)};
        const sched::CpmResult result = sched::schedule_tasks(tasks, links, calendar, options);
        check_ok(ctx, result, "chain");
        const sched::CpmTaskResult* a = find(result, 1);
        const sched::CpmTaskResult* b = find(result, 2);
        ctx.check(a != nullptr && b != nullptr, "chain: both tasks present");
        if (a != nullptr && b != nullptr) {
            ctx.equal_date(a->early_finish, at(kTue2010, 17, 0), "chain: A early finish");
            ctx.equal_date(b->early_start, at(kWed2110, 8, 0), "chain: B early start");
            ctx.equal_date(b->early_finish, at(kFri2310, 17, 0), "chain: B early finish");
            ctx.equal(a->total_slack, sched::ticks{0}, "chain: A total slack");
            ctx.equal(b->total_slack, sched::ticks{0}, "chain: B total slack");
            ctx.equal(a->free_slack, sched::ticks{0}, "chain: A free slack");
            ctx.equal(b->free_slack, sched::ticks{0}, "chain: B free slack");
            ctx.equal(result.project_finish, at(kFri2310, 17, 0), "chain: project finish");
        }
    }

    // --- each task may carry its own calendar (§1.6) -----------------------
    // INFERENCE, plumbing only: a 4 h/day task with a 1 d (480 min) duration
    // spans two of its own working days.
    {
        const sched::Calendar short_cal = short_calendar();
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{task(1, mins(480), sched::ConstraintType::as_soon_as_possible,
                                                     sched::kDateNa, sched::kDateNa, &short_cal)};
        const sched::CpmResult result = sched::schedule_tasks(tasks, {}, calendar, options);
        check_ok(ctx, result, "task calendar");
        ctx.equal_date(result.tasks.at(0).early_start, at(kMon1910, 8, 0),
                       "task calendar: start");
        ctx.equal_date(result.tasks.at(0).early_finish, at(kTue2010, 12, 0),
                       "task calendar: 480 min over 240 min days ends Tue 12:00");
    }

    // --- ALAP ---------------------------------------------------------------
    // MEASURED (wave 9b): corpus/probe/schedin.mpp uid 2 is an ALAP task with no
    // links in a project whose finish is driven by a 2 d SNET 2026-11-02 task.
    // docs/analysis/oracle/schedin-probe.json reports the ALAP task's COM
    // Start/Finish as 2026-11-03 08:00 / 17:00 (the LATE placement), and the
    // file stores +96/+100 = 11-03, +104/+108 = the ASAP early dates 10-19,
    // +112/+116 = 11-03, +186 StartSlack 0, +190 TotalSlack 52800 ticks.
    // So: display = late, early dates unchanged, StartSlack/FreeSlack 0,
    // TotalSlack = LF - EF.
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{
            task(1, mins(480)),
            task(2, mins(480), sched::ConstraintType::as_late_as_possible),
            task(3, mins(960), sched::ConstraintType::start_no_earlier_than, at(kMon0211, 8, 0)),
        };
        const sched::CpmResult result = sched::schedule_tasks(tasks, {}, calendar, options);
        check_ok(ctx, result, "ALAP (schedin shape)");
        ctx.equal_date(result.project_finish, at(kTue0311, 17, 0),
                       "ALAP (schedin): project finish is the SNET 11-02 task's finish");
        const sched::CpmTaskResult* b = find(result, 2);
        ctx.check(b != nullptr, "ALAP (schedin): task present");
        if (b != nullptr) {
            ctx.equal_date(b->early_start, at(kMon1910, 8, 0),
                           "ALAP (schedin): early start is the ASAP date (+104 pair)");
            ctx.equal_date(b->early_finish, at(kMon1910, 17, 0),
                           "ALAP (schedin): early finish is the ASAP date (+108 pair)");
            ctx.equal_date(b->late_start, at(kTue0311, 8, 0), "ALAP (schedin): late start (+112)");
            ctx.equal_date(b->late_finish, at(kTue0311, 17, 0), "ALAP (schedin): late finish (+116)");
            ctx.equal_date(b->start, at(kTue0311, 8, 0),
                           "ALAP (schedin): displayed start is the late start (COM Start)");
            ctx.equal_date(b->finish, at(kTue0311, 17, 0),
                           "ALAP (schedin): displayed finish is the late finish (COM Finish)");
            ctx.equal(b->total_slack, mins(5280), "ALAP (schedin): total slack 5280 min (+190)");
            ctx.equal(b->start_slack, sched::ticks{0}, "ALAP (schedin): start slack 0 (+186)");
            ctx.equal(b->free_slack, sched::ticks{0}, "ALAP (schedin): free slack 0 (+74)");
        }
        const sched::CpmTaskResult* a = find(result, 1);
        ctx.check(a != nullptr, "ALAP (schedin): the ASAP sibling is present");
        if (a != nullptr) {
            // The control row: an ASAP 1 d task in the same project keeps its
            // early dates as the display and reports StartSlack = TotalSlack =
            // 5280 min (+186 = +190 = 52800, schedin uid 1).
            ctx.equal_date(a->start, at(kMon1910, 8, 0), "ALAP (schedin): ASAP display is early");
            ctx.equal(a->total_slack, mins(5280), "ALAP (schedin): ASAP total slack 5280 min");
            ctx.equal(a->start_slack, mins(5280), "ALAP (schedin): ASAP start slack 5280 min");
            ctx.equal(a->free_slack, mins(5280), "ALAP (schedin): ASAP free slack 5280 min");
        }
    }
    // PARTLY VERIFIED: the predecessor shape is a hand-computed consequence, not
    // a measured row (the fixture's ALAP task has no links).
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        const std::vector<sched::CpmTask> tasks{
            task(1, mins(480)),
            task(2, mins(480), sched::ConstraintType::as_late_as_possible),
            task(3, mins(1440), sched::ConstraintType::start_no_earlier_than, at(kWed2110, 8, 0)),
        };
        const sched::CpmResult result = sched::schedule_tasks(tasks, {link(1, 2)}, calendar, options);
        check_ok(ctx, result, "ALAP");
        const sched::CpmTaskResult* b = find(result, 2);
        ctx.check(b != nullptr, "ALAP: task present");
        if (b != nullptr) {
            // C (3 d from Wed) makes the project finish Fri 23 17:00.
            ctx.equal_date(result.project_finish, at(kFri2310, 17, 0), "ALAP: project finish");
            ctx.equal_date(b->early_start, at(kTue2010, 8, 0), "ALAP: early start is unchanged");
            ctx.equal_date(b->start, at(kFri2310, 8, 0), "ALAP: displayed start is the late start");
            ctx.equal_date(b->finish, at(kFri2310, 17, 0), "ALAP: displayed finish is the late finish");
            ctx.equal(b->total_slack, mins(1440), "ALAP: total slack (engine formula, unverified)");
        }
    }

    // --- schedule from the finish ------------------------------------------
    // INFERENCE: no oracle fixture measured the from-finish direction.
    // TODO(open: W3-P5).
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        options.schedule_from_start = false;
        options.project_finish = at(kWed2110, 17, 0);
        const std::vector<sched::CpmTask> tasks{task(1, mins(480))};
        const sched::CpmResult result = sched::schedule_tasks(tasks, {}, calendar, options);
        check_ok(ctx, result, "from-finish");
        ctx.equal_date(result.tasks.at(0).start, at(kWed2110, 8, 0),
                       "from-finish: displayed start is the late start");
        ctx.equal_date(result.tasks.at(0).finish, at(kWed2110, 17, 0),
                       "from-finish: displayed finish is the late finish");
        ctx.equal_date(result.project_finish, at(kWed2110, 17, 0),
                       "from-finish: project finish anchor");
        // Without an anchor the engine refuses rather than guessing.
        sched::CpmOptions missing = options;
        missing.project_finish = sched::kDateNa;
        const sched::CpmResult bad = sched::schedule_tasks(tasks, {}, calendar, missing);
        check_error(ctx, bad, sched::CpmError::missing_project_finish, "from-finish without anchor");
    }

    // --- errors are reported, never thrown ---------------------------------
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        // A cycle: 1 -> 2 -> 3 -> 1.
        const std::vector<sched::CpmTask> tasks{task(1, mins(480)), task(2, mins(480)),
                                               task(3, mins(480))};
        const std::vector<sched::CpmLink> links{link(1, 2), link(2, 3), link(3, 1)};
        const sched::CpmResult cycle = sched::schedule_tasks(tasks, links, calendar, options);
        check_error(ctx, cycle, sched::CpmError::cycle, "cycle is reported");
        ctx.equal(cycle.cycle_tasks.size(), std::size_t{3}, "cycle: three tasks reported");
        ctx.check(cycle.tasks.empty(), "cycle: no partial schedule is claimed");

        // A self link.
        const std::vector<sched::CpmLink> self{link(1, 1)};
        const std::vector<sched::CpmTask> one{task(1, mins(480))};
        check_error(ctx, sched::schedule_tasks(one, self, calendar, options),
                    sched::CpmError::self_link, "self link");

        // An unknown id in a link.
        const std::vector<sched::CpmTask> two{task(1, mins(480)), task(2, mins(480))};
        const std::vector<sched::CpmLink> unknown{link(1, 9)};
        check_error(ctx, sched::schedule_tasks(two, unknown, calendar, options),
                    sched::CpmError::unknown_task, "unknown successor");

        // A duplicate id.
        const std::vector<sched::CpmTask> dup{task(1, mins(480)), task(1, mins(480))};
        check_error(ctx, sched::schedule_tasks(dup, {}, calendar, options),
                    sched::CpmError::duplicate_task_id, "duplicate id");

        // A negative duration.
        const std::vector<sched::CpmTask> neg{task(1, -mins(480))};
        check_error(ctx, sched::schedule_tasks(neg, {}, calendar, options),
                    sched::CpmError::negative_duration, "negative duration");

        // A date-bearing constraint without a date.
        const std::vector<sched::CpmTask> no_date{
            task(1, mins(480), sched::ConstraintType::must_start_on)};
        check_error(ctx, sched::schedule_tasks(no_date, {}, calendar, options),
                    sched::CpmError::constraint_date_missing, "missing constraint date");

        // An elapsed lag is NOT an error any more (wave 9b measured the rule on
        // corpus/probe/w9-elapsedlag.mpp; the old assertion pinned the refusal).
        // A zero elapsed lag must behave exactly like a zero working lag.
        sched::CpmLink elapsed = link(1, 2);
        elapsed.elapsed_lag = true;
        const sched::CpmResult elapsed_ok =
            sched::schedule_tasks(two, {elapsed}, calendar, options);
        check_ok(ctx, elapsed_ok, "zero elapsed lag");
        ctx.equal_date(elapsed_ok.tasks.at(1).early_start, at(kTue2010, 8, 0),
                       "zero elapsed lag: the successor still starts the next morning");
        ctx.equal_date(elapsed_ok.tasks.at(0).early_finish, at(kMon1910, 17, 0),
                       "zero elapsed lag: the predecessor is unaffected");

        // An empty task list and a missing project start.
        check_error(ctx, sched::schedule_tasks({}, {}, calendar, options), sched::CpmError::no_tasks,
                    "empty task list");
        sched::CpmOptions no_start;
        check_error(ctx, sched::schedule_tasks(one, {}, calendar, no_start),
                    sched::CpmError::missing_project_start, "missing project start");

        // A negative CriticalSlackLimit is an option error, not a schedule.
        sched::CpmOptions bad_limit = options;
        bad_limit.critical_slack_limit_days = -1;
        check_error(ctx, sched::schedule_tasks(one, {}, calendar, bad_limit),
                    sched::CpmError::invalid_option, "negative CriticalSlackLimit");
    }

    // --- determinism and idempotence (§3.8, §5.3) --------------------------
    {
        sched::CpmOptions options;
        options.project_start = kProjectStart;
        // Input order 3, 1, 2 with independent tasks: the topological order is by
        // id, and the results still come back in input order.
        const std::vector<sched::CpmTask> tasks{task(3, mins(480)), task(1, mins(480)),
                                               task(2, mins(480))};
        const sched::CpmResult first = sched::schedule_tasks(tasks, {}, calendar, options);
        check_ok(ctx, first, "determinism");
        ctx.equal(first.order.size(), std::size_t{3}, "determinism: order size");
        if (first.order.size() == 3) {
            ctx.equal(first.order[0], std::int32_t{1}, "determinism: order[0] is id 1");
            ctx.equal(first.order[1], std::int32_t{2}, "determinism: order[1] is id 2");
            ctx.equal(first.order[2], std::int32_t{3}, "determinism: order[2] is id 3");
        }
        ctx.equal(first.tasks.at(0).id, std::int32_t{3}, "determinism: results follow input order");
        ctx.equal_date(first.tasks.at(0).start, at(kMon1910, 8, 0), "determinism: task 3 start");

        // The link list order must not matter: 2 -> 3 listed before 1 -> 2.
        const std::vector<sched::CpmTask> chained{task(1, mins(480)), task(2, mins(480)),
                                                 task(3, mins(480))};
        const sched::CpmResult forward_links =
            sched::schedule_tasks(chained, {link(1, 2), link(2, 3)}, calendar, options);
        const sched::CpmResult reverse_links =
            sched::schedule_tasks(chained, {link(2, 3), link(1, 2)}, calendar, options);
        check_ok(ctx, forward_links, "link order");
        check_ok(ctx, reverse_links, "link order (reversed)");
        ctx.equal_date(forward_links.project_finish, reverse_links.project_finish,
                       "link order: the same project finish");
        ctx.equal_date(forward_links.project_finish, at(kWed2110, 17, 0),
                       "link order: three 1 d tasks end Wed 17:00");

        // A 200-task chain exercises the pass loop and the topological order at a
        // size where a quadratic mistake would show.
        std::vector<sched::CpmTask> many;
        std::vector<sched::CpmLink> many_links;
        for (std::int32_t i = 1; i <= 200; ++i) {
            many.push_back(task(i, mins(480)));
            if (i > 1) {
                many_links.push_back(link(i - 1, i));
            }
        }
        const sched::CpmResult scale = sched::schedule_tasks(many, many_links, calendar, options);
        check_ok(ctx, scale, "scale");
        ctx.equal(scale.order.size(), std::size_t{200}, "scale: 200 tasks ordered");
        ctx.check(scale.order.size() == 200 && scale.order.front() == 1 &&
                      scale.order.back() == 200,
                  "scale: the order runs 1..200");
        if (scale.order.size() == 200) {
            ctx.equal_date(scale.project_finish, scale.tasks.back().early_finish,
                           "scale: the project finishes with the last task");
            ctx.equal(scale.tasks.back().total_slack, sched::ticks{0},
                      "scale: the last task is critical");
        }

        const sched::CpmResult second = sched::schedule_tasks(tasks, {}, calendar, options);
        check_ok(ctx, second, "idempotence");
        bool identical = first.tasks.size() == second.tasks.size();
        for (std::size_t i = 0; identical && i < first.tasks.size(); ++i) {
            const sched::CpmTaskResult& a = first.tasks[i];
            const sched::CpmTaskResult& b = second.tasks[i];
            identical = a.id == b.id && a.start == b.start && a.finish == b.finish &&
                        a.early_start == b.early_start && a.early_finish == b.early_finish &&
                        a.late_start == b.late_start && a.late_finish == b.late_finish &&
                        a.total_slack == b.total_slack && a.free_slack == b.free_slack &&
                        a.start_slack == b.start_slack && a.finish_slack == b.finish_slack &&
                        a.critical == b.critical;
        }
        ctx.check(identical, "idempotence: a second run reproduces every field");
    }

    return ctx.finish("sched_cpm");
}

