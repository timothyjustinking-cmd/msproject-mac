// tests/plan_attribution_tests.cpp -- W28: the mismatch classifier in
// core/plan/attribution.h, unit-tested on synthetic PlanTaskResult / StoredRow
// pairs so every family and the fallback are reachable without a corpus file.
//
// This test deliberately contains NO PROJECT_A counts: the pinned M2 numbers live in
// tests/plan_recalc_tests.cpp and are the coordinator's to update after the
// engine/reader streams land.  What is pinned here is the RULE -- which family a
// row with given inputs must produce -- so a classifier change is a deliberate
// change to this file.
//
// The cases, one per family (and the precedence questions the rules claim):
//   1. actual            start is a kept actual, even when the row also has a
//                        predecessor (actual beats link); same for finish.
//   2. summary           rolled-up row, even when its duration would look like a
//                        milestone (summary beats milestone); the stored Summary
//                        flag alone is enough.
//   3. milestone         duration 0 with a predecessor (milestone beats link).
//   4. constraint        date-bearing constraint with a predecessor (constraint
//                        beats link), on a slack field as well as a date field.
//   5. link              leaf with a predecessor and no other driver.
//   6. calendar_or_start leaf with no constraint, no predecessor and no actual.
//   7. unexplained       the justified fallback: a leaf that keeps an actual but
//                        whose mismatching field is slack (or the finish forecast
//                        of an in-progress row), which no rule above covers.
//
// SAFETY.  Like the other core tests, main catches every exception and returns 2;
// this test never reads a corpus file and never touches the network.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

#include "attribution.h"
#include "plan.h"
#include "sched_test_support.h"

namespace {

namespace plan = core::plan;
namespace sched = core::sched;
using sched_test::TestContext;

/* A leaf row with the four compared fields set to a known schedule.  The
 * scheduling-input flags are all off; a case turns on exactly the ones its rule
 * names. */
plan::PlanTaskResult make_leaf(std::int32_t uid, sched::ticks duration) {
    plan::PlanTaskResult row;
    row.uid = uid;
    row.duration = duration;
    row.start = sched::from_ymd(2026, 1, 5, 8, 0);
    row.finish = sched::from_ymd(2026, 1, 5, 17, 0);
    row.total_slack = 0;
    row.critical = true;
    return row;
}

/* The stored side that AGREES with an ours row: every case then changes exactly
 * one value to create the mismatch it is testing. */
plan::StoredRow stored_of(const plan::PlanTaskResult& ours) {
    plan::StoredRow stored;
    stored.start = ours.start;
    stored.finish = ours.finish;
    stored.total_slack = ours.total_slack;
    stored.critical = ours.critical;
    stored.summary = ours.summary;
    return stored;
}

void expect_family(TestContext& context, const plan::PlanTaskResult& ours,
                   const plan::StoredRow& stored, plan::AttribField field,
                   plan::DivergenceFamily expected, const std::string& what) {
    const bool matches = plan::field_matches(ours, stored, field);
    context.check(!matches, what + ": the field really mismatches");
    context.check(plan::classify(ours, stored, field) == expected,
                  what + ": classified as " + plan::family_name(expected));
}

/* ---- field_matches: the comparison definition --------------------------- */
void test_field_matches(TestContext& context) {
    const plan::PlanTaskResult ours = make_leaf(1, 4800);
    const plan::StoredRow same = stored_of(ours);
    context.check(plan::field_matches(ours, same, plan::AttribField::start),
                  "field_matches: equal start matches");
    context.check(plan::field_matches(ours, same, plan::AttribField::finish),
                  "field_matches: equal finish matches");
    context.check(plan::field_matches(ours, same, plan::AttribField::slack),
                  "field_matches: equal total slack matches");
    context.check(plan::field_matches(ours, same, plan::AttribField::critical),
                  "field_matches: equal critical flag matches");

    plan::StoredRow changed = same;
    changed.start = sched::from_ymd(2026, 1, 6, 8, 0);
    context.check(!plan::field_matches(ours, changed, plan::AttribField::start),
                  "field_matches: a different start does not match");
    context.check(plan::field_matches(ours, changed, plan::AttribField::finish),
                  "field_matches: changing start leaves finish matching");

    changed = same;
    changed.finish = sched::from_ymd(2026, 1, 6, 17, 0);
    context.check(!plan::field_matches(ours, changed, plan::AttribField::finish),
                  "field_matches: a different finish does not match");

    changed = same;
    changed.total_slack = 4800;
    context.check(!plan::field_matches(ours, changed, plan::AttribField::slack),
                  "field_matches: a different total slack does not match");

    changed = same;
    changed.critical = false;
    context.check(!plan::field_matches(ours, changed, plan::AttribField::critical),
                  "field_matches: a different critical flag does not match");
}

/* ---- 1. actual ---------------------------------------------------------- */
void test_actual(TestContext& context) {
    /* A started leaf that ALSO has a predecessor: the actual date is the
     * stronger explanation, so actual must win over link. */
    plan::PlanTaskResult ours = make_leaf(10, 4800);
    ours.start_is_actual = true;
    ours.has_predecessor = true;
    plan::StoredRow stored = stored_of(ours);
    stored.start = sched::from_ymd(2026, 1, 4, 17, 0);
    expect_family(context, ours, stored, plan::AttribField::start,
                  plan::DivergenceFamily::actual,
                  "actual start (beats its predecessor link)");

    /* A completed leaf: finish is the actual finish. */
    ours = make_leaf(11, 4800);
    ours.start_is_actual = true;
    ours.finish_is_actual = true;
    ours.has_date_constraint = true;
    stored = stored_of(ours);
    stored.finish = sched::from_ymd(2026, 1, 6, 17, 0);
    expect_family(context, ours, stored, plan::AttribField::finish,
                  plan::DivergenceFamily::actual,
                  "actual finish (beats its constraint)");
}

/* ---- 2. summary --------------------------------------------------------- */
void test_summary(TestContext& context) {
    /* A rolled-up row whose rolled-up duration happens to be 0: summary must be
     * checked before milestone, and before the link rule. */
    plan::PlanTaskResult ours = make_leaf(20, 0);
    ours.summary = true;
    ours.has_predecessor = true;
    plan::StoredRow stored = stored_of(ours);
    stored.finish = sched::from_ymd(2026, 1, 9, 17, 0);
    expect_family(context, ours, stored, plan::AttribField::finish,
                  plan::DivergenceFamily::summary,
                  "summary (beats milestone and link)");

    /* The stored file may be the one that calls the row a summary.  The existing
     * --compare split uses the stored flag, so the classifier accepts either. */
    ours = make_leaf(21, 4800);
    stored = stored_of(ours);
    stored.summary = true;
    stored.total_slack = 2400;
    expect_family(context, ours, stored, plan::AttribField::slack,
                  plan::DivergenceFamily::summary,
                  "summary known only from the stored row");
}

/* ---- 3. milestone ------------------------------------------------------- */
void test_milestone(TestContext& context) {
    /* A 0-duration task stored at an interval end: it has a predecessor, but the
     * family definition excludes a milestone from `link`. */
    plan::PlanTaskResult ours = make_leaf(30, 0);
    ours.has_predecessor = true;
    plan::StoredRow stored = stored_of(ours);
    stored.start = sched::from_ymd(2026, 1, 4, 17, 0);
    expect_family(context, ours, stored, plan::AttribField::start,
                  plan::DivergenceFamily::milestone, "milestone start (beats link)");

    /* start == finish for a milestone, so the finish mismatch is the same
     * placement question. */
    stored = stored_of(ours);
    stored.finish = sched::from_ymd(2026, 1, 4, 17, 0);
    expect_family(context, ours, stored, plan::AttribField::finish,
                  plan::DivergenceFamily::milestone, "milestone finish");
}

/* ---- 4. constraint ------------------------------------------------------ */
void test_constraint(TestContext& context) {
    /* A date-bearing constraint on a row that also has a predecessor: the stored
     * bound is the stronger explanation. */
    plan::PlanTaskResult ours = make_leaf(40, 4800);
    ours.has_date_constraint = true;
    ours.has_predecessor = true;
    plan::StoredRow stored = stored_of(ours);
    stored.start = sched::from_ymd(2026, 1, 4, 17, 0);
    expect_family(context, ours, stored, plan::AttribField::start,
                  plan::DivergenceFamily::constraint,
                  "constraint start (beats link)");

    /* A deadline is a date-bearing input too, and it changes slack; the family
     * is not date-field-only. */
    ours = make_leaf(41, 4800);
    ours.has_date_constraint = true;  // set for a deadline by recalculate()
    ours.has_predecessor = true;
    stored = stored_of(ours);
    stored.total_slack = 480;
    expect_family(context, ours, stored, plan::AttribField::slack,
                  plan::DivergenceFamily::constraint, "deadline slack");
}

/* ---- 5. link ------------------------------------------------------------ */
void test_link(TestContext& context) {
    plan::PlanTaskResult ours = make_leaf(50, 4800);
    ours.has_predecessor = true;
    plan::StoredRow stored = stored_of(ours);
    stored.start = sched::from_ymd(2026, 1, 6, 8, 0);
    expect_family(context, ours, stored, plan::AttribField::start,
                  plan::DivergenceFamily::link, "leaf driven by a predecessor");

    stored = stored_of(ours);
    stored.critical = false;
    expect_family(context, ours, stored, plan::AttribField::critical,
                  plan::DivergenceFamily::link, "link-inherited critical flag");
}

/* ---- 6. calendar_or_start ----------------------------------------------- */
void test_calendar_or_start(TestContext& context) {
    /* A source leaf: nothing but the calendar and the project start can move it. */
    const plan::PlanTaskResult ours = make_leaf(60, 4800);
    plan::StoredRow stored = stored_of(ours);
    stored.start = sched::from_ymd(2026, 1, 6, 8, 0);
    expect_family(context, ours, stored, plan::AttribField::start,
                  plan::DivergenceFamily::calendar_or_start, "source leaf start");

    stored = stored_of(ours);
    stored.finish = sched::from_ymd(2026, 1, 6, 17, 0);
    expect_family(context, ours, stored, plan::AttribField::finish,
                  plan::DivergenceFamily::calendar_or_start,
                  "source leaf finish (same duration, shifted instant)");
}

/* ---- 7. unexplained: the justified fallback ----------------------------- */
void test_unexplained(TestContext& context) {
    /* A started source task whose slack differs.  The actual family is defined
     * for start/finish only and calendar_or_start requires a row with no actual,
     * so this is the honest fallback -- see attribution.h rule 7. */
    plan::PlanTaskResult ours = make_leaf(70, 4800);
    ours.start_is_actual = true;
    plan::StoredRow stored = stored_of(ours);
    stored.total_slack = 6240;
    expect_family(context, ours, stored, plan::AttribField::slack,
                  plan::DivergenceFamily::unexplained,
                  "started source task cannot explain its slack by a rule above");

    /* The other fallback shape: an in-progress task (actual start, no actual
     * finish) whose FORECAST finish differs.  Nothing above claims it. */
    ours = make_leaf(71, 4800);
    ours.start_is_actual = true;
    stored = stored_of(ours);
    stored.finish = sched::from_ymd(2026, 1, 8, 17, 0);
    expect_family(context, ours, stored, plan::AttribField::finish,
                  plan::DivergenceFamily::unexplained,
                  "in-progress task finish forecast");

    /* The fallback is not a catch-all that swallows real families: the same row
     * with a predecessor is a link mismatch. */
    ours.has_predecessor = true;
    expect_family(context, ours, stored, plan::AttribField::finish,
                  plan::DivergenceFamily::link,
                  "the same forecast with a predecessor becomes link");
}

/* ---- the enum/name contract the tool and JSON rely on ------------------- */
void test_names(TestContext& context) {
    context.equal(static_cast<int>(plan::kAttribFieldCount), 4,
                  "there are four attributed fields");
    context.equal(static_cast<int>(plan::kDivergenceFamilyCount), 7,
                  "there are seven divergence families");

    context.equal_string(plan::field_name(plan::AttribField::start), "start",
                         "field name: start");
    context.equal_string(plan::field_name(plan::AttribField::finish), "finish",
                         "field name: finish");
    context.equal_string(plan::field_name(plan::AttribField::slack), "slack",
                         "field name: slack");
    context.equal_string(plan::field_name(plan::AttribField::critical), "critical",
                         "field name: critical");

    context.equal_string(plan::family_name(plan::DivergenceFamily::actual), "actual",
                         "family name: actual");
    context.equal_string(plan::family_name(plan::DivergenceFamily::summary), "summary",
                         "family name: summary");
    context.equal_string(plan::family_name(plan::DivergenceFamily::milestone),
                         "milestone", "family name: milestone");
    context.equal_string(plan::family_name(plan::DivergenceFamily::constraint),
                         "constraint", "family name: constraint");
    context.equal_string(plan::family_name(plan::DivergenceFamily::link), "link",
                         "family name: link");
    context.equal_string(plan::family_name(plan::DivergenceFamily::calendar_or_start),
                         "calendar_or_start", "family name: calendar_or_start");
    context.equal_string(plan::family_name(plan::DivergenceFamily::unexplained),
                         "unexplained", "family name: unexplained");

    /* The enum order IS the precedence order the header documents. */
    context.equal(static_cast<int>(plan::DivergenceFamily::actual), 0,
                  "actual is the first precedence rule");
    context.equal(static_cast<int>(plan::DivergenceFamily::unexplained), 6,
                  "unexplained is the last precedence rule");
}

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);
    sched_test::TestContext context;
    try {
        test_field_matches(context);
        test_actual(context);
        test_summary(context);
        test_milestone(context);
        test_constraint(context);
        test_link(context);
        test_calendar_or_start(context);
        test_unexplained(context);
        test_names(context);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << '\n';
        return 2;
    }
    return context.finish("plan_attribution");
}
