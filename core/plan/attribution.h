// core/plan/attribution.h -- W28: attribute every --compare mismatch to exactly
// one family, so the M2 number (228/332 on PROJECT_A) can be read as "what is left
// and why" instead of one aggregate.
//
// WHAT THIS IS.  `core::plan::recalculate` produces a PlanTaskResult per uid;
// `tools/recalc --compare` compares four fields (start, finish, TotalSlack,
// Critical) with the values STORED in another .mpp.  This header classifies one
// (row, field) mismatch into exactly one DivergenceFamily.  The classifier is a
// pure function of the recalculated row (its scheduling inputs, which
// recalculate() records in PlanTaskResult) and the stored row (values plus its
// Summary flag).  It has no I/O and no dependency on the tool, so the rules are
// unit-tested in tests/plan_attribution_tests.cpp and the tool prints the same
// answer the test asserts.
//
// THE RULES, in precedence order (the FIRST one that applies wins).  Every rule
// below is testable from PlanTaskResult alone except the Summary fallback, which
// also consults the stored row:
//
//  1. actual          field == start and row start_is_actual, or field == finish
//                     and row finish_is_actual.  The displayed date is a STORED
//                     actual copied through unchanged (spec/scheduling.md §2.4),
//                     not a forecast: if two files disagree there, the two files
//                     recorded different actuals, and no scheduling rule can
//                     reconcile them.  Deliberately date-fields only.  A
//                     slack/critical mismatch on a started task is still a
//                     consequence of the network around its pin, so it is
//                     classified by the rules below, not by "actual".
//  2. summary         recalculated row is a summary, or the stored row says it
//                     is one.  Its values are rolled up from its children after
//                     the CPM passes (§3.7); the divergence belongs to a child
//                     or to the rollup rule, never to this row's own inputs.
//  3. milestone       recalculated duration == 0.  A 0-duration task is placed
//                     by the interval-end rule core/sched currently does not
//                     reproduce (PROJECT_A 10742/10803/10804), and start == finish for
//                     it, so one placement question appears in both date
//                     fields.  Checked after `summary`: a roll-up has no
//                     duration of its own.
//  4. constraint      has_date_constraint: the row carries a date-bearing
//                     PjConstraint (MSO/MFO/SNET/SNLT/FNET/FNLT) whose date is
//                     stored, or a deadline.  The bound is a date the file
//                     states, so the mismatch is a constraint/deadline
//                     interpretation question (PROJECT_A 10859).
//  5. link            has_predecessor: at least one mapped, schedulable link
//                     names this row as its successor.  The row's bound comes
//                     from that predecessor, so the mismatch is a link question
//                     -- existence, type or lag -- as against a calendar
//                     question (the PROJECT_A 10846 cascade).
//  6. calendar_or_start  a leaf (not a summary), not a milestone, with no
//                     date-bearing constraint or deadline, no predecessor and
//                     no actual date.  Its only remaining scheduling inputs are
//                     the hard-coded calendar and the project start, so a
//                     mismatch is attributed there.
//  7. unexplained     THE FALLBACK.  It is reachable only by a leaf that keeps
//                     at least one actual date while the MISMATCHING field has
//                     no actual of its own: slack/critical on a started row, or
//                     the finish forecast of a task that has started but not
//                     finished.  Rule 1 covers actuals for the two date fields
//                     only; rule 6 requires a row with no actual at all.  It is
//                     not called `actual` because the field is not a date, and
//                     not `calendar_or_start` because neither the calendar nor
//                     the project start moved this row.  This is exactly the
//                     honest gap: the current rule set cannot say whether the
//                     divergence is Project's slack treatment for a pinned task
//                     or a duration/remaining-work effect.  The count is printed
//                     rather than hidden inside a wrong family; a non-zero value
//                     is a question for the oracle, not a code path to paper
//                     over.
//
// WHAT THE FAMILIES DO NOT CLAIM.  A family names the row's INPUT that the
// divergence belongs to, not the precise cause inside it: `link` covers "a link
// row says X but Project scheduled Y" (which can be a bad link row, a lag
// interpretation or a link Project ignored), and `calendar_or_start` covers both
// the hard-coded calendar and the project-start instant.  The per-family first
// uids in the tool output are the evidence for narrowing that down.
#pragma once

#include <cstddef>
#include <cstdint>

#include "plan.h"

namespace core::plan {

// The four fields --compare compares, in the order it prints them.
enum class AttribField : std::uint8_t { start = 0, finish = 1, slack = 2, critical = 3 };
inline constexpr std::size_t kAttribFieldCount = 4;

// The seven families, in precedence order (see the rules above).  The numeric
// order IS the precedence order, which is why `unexplained` is last.
enum class DivergenceFamily : std::uint8_t {
    actual = 0,
    summary = 1,
    milestone = 2,
    constraint = 3,
    link = 4,
    calendar_or_start = 5,
    unexplained = 6,
};
inline constexpr std::size_t kDivergenceFamilyCount = 7;

// Stable names for the table and the JSON ("start", "calendar_or_start", ...).
// A value outside the enum (only possible through a cast) prints "?".
const char* field_name(AttribField field) noexcept;
const char* family_name(DivergenceFamily family) noexcept;

// The stored side of one uid: the four compared values and the stored Summary
// flag.  It is deliberately only what the comparison needs -- no task name, no
// text, no exceptions.  `total_slack` is mpp::Task::total_slack_ticks (row +74),
// the same unit as PlanTaskResult::total_slack (tenths of a minute).
struct StoredRow {
    core::sched::date32 start = core::sched::kDateNa;
    core::sched::date32 finish = core::sched::kDateNa;
    core::sched::ticks total_slack = 0;
    bool critical = false;
    bool summary = false;
};

// Exact equality on one compared field (the definition --compare already used).
bool field_matches(const PlanTaskResult& ours, const StoredRow& stored,
                   AttribField field) noexcept;

// Classify ONE mismatching (row, field).  Call it only when field_matches() is
// false; on a matching field the answer is meaningless.  Never throws.
DivergenceFamily classify(const PlanTaskResult& ours, const StoredRow& stored,
                          AttribField field) noexcept;

}  // namespace core::plan
