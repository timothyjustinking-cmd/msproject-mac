// core/usage/usage_mpp.h -- map the .mpp reader's assignment table onto the
// neutral usage input.  Header-only and inline so core_usage itself does not
// have to link core_mpp; a caller that loads a document already links the
// reader.
//
// The mapping is the measured one from docs/analysis/oracle/w10-oracle.md §3:
// assignment Work is TBkndAssn/FixedData +20 (minutes x1000, exposed as
// mpp::Assignment::work_minutes()), and Start/Finish are working-time wall
// clock packed dates at +52/+56.  A packed date is already the engine's date32
// value by identity (memory/session.md), so no day shift is applied.
//
// Resources with no assignments still get a row: their uids come from the
// reader's live resource table, not from the assignment list.  Assignments
// whose task/resource uid is not live still become AssignmentInput rows; the
// projection totals them but invents no task/resource row (see usage.h).
//
// MEASURED LIMIT OF THE CORPUS FILE: all 67 live assignment rows in
// corpus/project_a.mpp store kNullResourceUid at +8 (the reader
// measured this; tests/mpp_reader_tests.cpp pins it), so this bridge maps
// resource_uid 0 for every one of them and the PROJECT_A resource-usage projection is
// 22 present-but-zero rows.  corpus/probe/assign1.mpp is the measured file with
// real assignment resource uids, but the probe fixtures are not in every
// checkout, so the resource path is tested against a hand-built
// mpp::Document in tests/usage_tests.cpp instead.

#ifndef CORE_USAGE_USAGE_MPP_H
#define CORE_USAGE_USAGE_MPP_H

#include <cstdint>

#include "reader.h"
#include "usage.h"

namespace core::usage {

inline UsageInput usage_input_from_mpp(const mpp::Document& document) {
    UsageInput input;
    input.task_uids.reserve(document.tasks.size());
    for (const mpp::Task& task : document.tasks) {
        if (task.uid != 0) {
            input.task_uids.push_back(task.uid);
        }
    }
    input.resource_uids.reserve(document.resources.size());
    for (const mpp::Resource& resource : document.resources) {
        if (resource.uid != 0) {
            input.resource_uids.push_back(resource.uid);
        }
    }
    input.assignments.reserve(document.assignments.size());
    for (const mpp::Assignment& assignment : document.assignments) {
        AssignmentInput row;
        row.uid = assignment.uid;
        row.task_uid = assignment.task_uid;
        row.resource_uid = assignment.resource_uid();
        row.start = assignment.start.raw;
        row.finish = assignment.finish.raw;
        row.work_minutes = static_cast<std::int64_t>(assignment.work_minutes());
        input.assignments.push_back(row);
    }
    return input;
}

}  // namespace core::usage

#endif  // CORE_USAGE_USAGE_MPP_H
