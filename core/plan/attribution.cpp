// core/plan/attribution.cpp -- the W28 mismatch classifier (rules in the header).
#include "attribution.h"

namespace core::plan {
namespace {

// Both arrays are indexed by the enum value; kAttribFieldCount and
// kDivergenceFamilyCount size them, so adding an enum member without a name is a
// compile error where the array is initialised.
constexpr const char* kFieldNames[kAttribFieldCount] = {"start", "finish", "slack",
                                                        "critical"};
constexpr const char* kFamilyNames[kDivergenceFamilyCount] = {
    "actual",          "summary", "milestone", "constraint",
    "link",            "calendar_or_start", "unexplained",
};

}  // namespace

const char* field_name(AttribField field) noexcept {
    const auto index = static_cast<std::size_t>(field);
    return index < kAttribFieldCount ? kFieldNames[index] : "?";
}

const char* family_name(DivergenceFamily family) noexcept {
    const auto index = static_cast<std::size_t>(family);
    return index < kDivergenceFamilyCount ? kFamilyNames[index] : "?";
}

bool field_matches(const PlanTaskResult& ours, const StoredRow& stored,
                   AttribField field) noexcept {
    switch (field) {
        case AttribField::start:
            return ours.start == stored.start;
        case AttribField::finish:
            return ours.finish == stored.finish;
        case AttribField::slack:
            return ours.total_slack == stored.total_slack;
        case AttribField::critical:
            return ours.critical == stored.critical;
    }
    return false;  // unreachable: every AttribField is handled above
}

DivergenceFamily classify(const PlanTaskResult& ours, const StoredRow& stored,
                          AttribField field) noexcept {
    // 1. actual -- only the two date fields.  A stored actual is copied through,
    //    so a mismatch is a disagreement between the files' recorded facts.
    switch (field) {
        case AttribField::start:
            if (ours.start_is_actual) {
                return DivergenceFamily::actual;
            }
            break;
        case AttribField::finish:
            if (ours.finish_is_actual) {
                return DivergenceFamily::actual;
            }
            break;
        case AttribField::slack:
        case AttribField::critical:
            break;
    }

    // 2. summary -- a rolled-up row in either file.  The stored flag is
    //    consulted as a fallback because it is the one the existing --compare
    //    split uses; a summary's own inputs cannot move its aggregate.
    if (ours.summary || stored.summary) {
        return DivergenceFamily::summary;
    }

    // 3. milestone -- a 0-duration task; core/sched cannot place one at an
    //    interval end (and start == finish for it).
    if (ours.duration == 0) {
        return DivergenceFamily::milestone;
    }

    // 4. constraint -- a date-bearing constraint or a deadline overrides link
    //    logic, so it is checked before `link`.
    if (ours.has_date_constraint) {
        return DivergenceFamily::constraint;
    }

    // 5. link -- the bound comes from a mapped predecessor; not a calendar
    //    question.  A milestone was already taken by rule 3, matching the
    //    family definition "has predecessors and is not a milestone".
    if (ours.has_predecessor) {
        return DivergenceFamily::link;
    }

    // 6. calendar_or_start -- a leaf whose only remaining inputs are the
    //    calendar and the project start.
    if (!ours.start_is_actual && !ours.finish_is_actual) {
        return DivergenceFamily::calendar_or_start;
    }

    // 7. unexplained -- the justified fallback: a leaf that keeps an actual but
    //    whose mismatching field is not that actual (slack/critical on a started
    //    row, or the finish forecast of an in-progress row).  See the header.
    return DivergenceFamily::unexplained;
}

}  // namespace core::plan
