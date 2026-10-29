// tests/io_mspdi_crosscheck.cpp -- the .mpp reader against Project's MSPDI
// export, through the MSPDI importer (core/io) instead of the reader.
//
// Cases:
//
//   1. In memory, no files: an mpp::Document built by hand against an MSPDI
//      string that carries the same tasks, resource, assignment and links.
//      This proves the comparison itself (and, in the negative controls, that
//      it notices a difference) on a machine with no corpus.  The controls also
//      pin the two categories that are reported but excluded from the verdict:
//      dead-endpoint links/assignments (deleted task slots) and milestone notes
//      (the reader derives Milestone from duration).
//   2. corpus/probe/lagff1.mpp + lagff1.xml -- a synthetic fixture made through
//      the COM oracle.  It must agree exactly, so this is a strict assertion;
//      its resources/assignments are compared too.
//   3. corpus/project_a.mpp + corpus/probe/project_a-export.xml -- the real
//      programme and Project's own export.  The XML's counts (84 Task elements
//      including the UID 0 project row, 88 PredecessorLink, 23 Resource
//      elements including the UID 0 placeholder, 67 Assignment elements) are
//      asserted because they are the ground truth this test exists to check.
//      The reader/XML differences are REPORTED, not pinned.
//   4. corpus/project_b.mpp + corpus/probe/project_b-export.xml -- the
//      second real programme.  It has 2 links and 10 assignments whose task
//      slots were deleted; those are named dead-endpoint exclusions, so the
//      live sets must agree (70/70 links, 63/63 assignments, 22/22 resources).
//      Milestone notes are 0 with the stored-bit reader (they were 2 while the
//      reader was duration-only).  The residual date difference on one task is
//      the stored (+96/+100) vs effective (+104/+108) pair; it is reported, not
//      pinned, because a core/mpp fix is expected to change it.
//   5. corpus/probe/assign1.mpp -- a synthetic fixture with 3 real assignments
//      and 2 resources but NO export on disk; it prints that it is not
//      comparable rather than pretending to compare it.
//
// All corpus files are git-ignored client data; a missing one prints
// "SKIP: corpus absent" and the test passes.  No task name is read or printed.
#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "crosscheck.h"
#include "fixture_guard.h"
#include "mspdi.h"
#include "reader.h"
#include "view_test_support.h"

namespace {

using mspdi_cmp::Result;

/* A two-task document used by the in-memory case.  The dates are the reader's
 * packed form; 0x391312c0 is day 14611 = 2024-01-01 08:00 and 0x391427d8 is day
 * 14612 = 2024-01-02 17:00 (see tests/io_mspdi_tests.cpp for the same anchor). */
mpp::Document make_reader_document() {
    mpp::Document document;

    mpp::Task first;
    first.uid = 1;
    first.row_id = 1;
    first.duration_ticks = 9600; /* two 8-hour days */
    first.start.raw = 0x391312c0u;
    first.finish.raw = 0x391427d8u;
    first.summary = true;
    first.child_count = 1;
    first.constraint_type = 4; /* SNET */
    first.constraint_date.raw = 0x391512c0u; /* 2024-01-03 08:00 */
    first.deadline.raw = 0x391627d8u;         /* 2024-01-04 17:00 */
    first.actual_start.raw = 0x391312c0u;
    document.tasks.push_back(first);

    mpp::Task second;
    second.uid = 2;
    second.row_id = 2;
    second.duration_ticks = 9600;
    second.start.raw = 0x391312c0u;
    second.finish.raw = 0x391427d8u;
    document.tasks.push_back(second);

    mpp::Resource resource;
    resource.uid = 1;
    resource.row_id = 1;
    resource.max_units_x100 = 10000.0; /* 100 % */
    document.resources.push_back(resource);

    mpp::Assignment assignment;
    assignment.uid = 10;
    assignment.task_uid = 1;
    assignment.resource_uid_raw = 1;
    assignment.units_x100 = 10000.0;   /* 100 % */
    assignment.work_x1000 = 2400000.0; /* 2400 min = 40 h */
    document.assignments.push_back(assignment);

    /* Two links with the SAME endpoint pair and different type: a set
     * comparison would collapse them, the multiset must not.  The first lag is
     * the decimal form Project writes (240000 tenths of a minute = 50 days),
     * the second is the ISO form the hand-written sample uses. */
    mpp::Link finish_start;
    finish_start.uid = 1;
    finish_start.predecessor_uid = 2;
    finish_start.successor_uid = 1;
    finish_start.type_raw = 1; /* FS */
    finish_start.lag_ticks = 240000;
    document.links.push_back(finish_start);

    mpp::Link start_start;
    start_start.uid = 2;
    start_start.predecessor_uid = 2;
    start_start.successor_uid = 1;
    start_start.type_raw = 3; /* SS */
    start_start.lag_ticks = 4800;
    document.links.push_back(start_start);

    return document;
}

constexpr const char* kSyntheticXml =
    "<Project><Tasks>"
    "<Task><UID>1</UID>"
    "<Start>2024-01-01T08:00:00</Start><Finish>2024-01-02T17:00:00</Finish>"
    "<Duration>PT16H0M0S</Duration><Milestone>0</Milestone><Summary>1</Summary>"
    "<ConstraintType>4</ConstraintType><ConstraintDate>2024-01-03T08:00:00</ConstraintDate>"
    "<Deadline>2024-01-04T17:00:00</Deadline><ActualStart>2024-01-01T08:00:00</ActualStart>"
    "<PredecessorLink><PredecessorUID>2</PredecessorUID><Type>1</Type>"
    "<LinkLag>240000</LinkLag><LagFormat>7</LagFormat></PredecessorLink>"
    "<PredecessorLink><PredecessorUID>2</PredecessorUID><Type>3</Type>"
    "<LinkLag>PT8H0M0S</LinkLag><LagFormat>7</LagFormat></PredecessorLink>"
    "</Task>"
    "<Task><UID>2</UID>"
    "<Start>2024-01-01T08:00:00</Start><Finish>2024-01-02T17:00:00</Finish>"
    "<Duration>PT16H0M0S</Duration><Milestone>0</Milestone><Summary>0</Summary>"
    "<ConstraintType>0</ConstraintType>"
    "</Task>"
    "</Tasks>"
    "<Resources><Resource><UID>1</UID></Resource></Resources>"
    "<Assignments><Assignment><UID>10</UID><TaskUID>1</TaskUID><ResourceUID>1</ResourceUID>"
    "<Units>1</Units><Work>PT40H0M0S</Work></Assignment></Assignments>"
    "</Project>";

void test_in_memory(view_test::TestContext& ctx) {
    const mpp::Document reader = make_reader_document();
    const core::io::MspdiDocument xml = core::io::import_mspdi(kSyntheticXml);

    const Result result = mspdi_cmp::compare(reader, xml);
    ctx.check(result.ok(), "in-memory pair agrees (no mismatch expected)");
    ctx.check(result.problems.empty(), "field names all resolve");
    ctx.equal(result.reader_tasks, std::size_t{2}, "in-memory reader task count");
    ctx.equal(result.xml_tasks, std::size_t{2}, "in-memory xml task count");
    ctx.equal(result.compared_tasks, std::size_t{2}, "in-memory compared task count");
    ctx.equal(result.compared_fields, std::size_t{20}, "in-memory field comparison count");
    ctx.equal(result.reader_links, std::size_t{2}, "in-memory reader link count");
    ctx.equal(result.live_reader_links, std::size_t{2}, "in-memory live link count");
    ctx.equal(result.xml_links, std::size_t{2}, "in-memory xml link count");
    ctx.equal(result.matched_links, std::size_t{2}, "in-memory matched link count");
    ctx.check(result.dead_endpoint_links.empty(), "no dead links in the clean pair");
    ctx.check(result.xml_has_project_row == false, "no project row in the synthetic XML");

    ctx.equal(result.reader_resources, std::size_t{1}, "in-memory reader resource count");
    ctx.equal(result.xml_resources, std::size_t{1}, "in-memory xml resource count");
    ctx.equal(result.compared_resources, std::size_t{1}, "in-memory compared resource count");
    ctx.check(result.xml_has_placeholder_resource == false,
              "no UID 0 placeholder resource in the synthetic XML");
    ctx.equal(result.resource_differences(), std::size_t{0},
              "in-memory resources agree");

    ctx.equal(result.reader_assignments, std::size_t{1}, "in-memory reader assignment count");
    ctx.equal(result.xml_assignments, std::size_t{1}, "in-memory xml assignment count");
    ctx.equal(result.compared_assignments, std::size_t{1},
              "in-memory compared assignment count");
    ctx.equal(result.compared_assignment_units, std::size_t{1},
              "the exported Units value is compared");
    ctx.equal(result.compared_assignment_work, std::size_t{1},
              "the exported Work value is compared");
    ctx.equal(result.assignment_differences(), std::size_t{0},
              "in-memory assignments agree");

    /* Negative control: one changed task value must surface as exactly one field
     * mismatch, with the field named, and must move the exit-condition. */
    mpp::Document changed = reader;
    changed.tasks[0].constraint_type = 3; /* MFO in the file, SNET in the XML */
    const Result broken = mspdi_cmp::compare(changed, xml);
    ctx.check(!broken.ok(), "a changed field is reported as a mismatch");
    ctx.equal(broken.field_mismatches.size(), std::size_t{1},
              "exactly one field mismatch for one changed value");
    if (broken.field_mismatches.size() == 1) {
        ctx.equal_string(broken.field_mismatches[0].field, "ConstraintType",
                         "the mismatch names the ConstraintType element");
    }

    /* Negative control for the multiset: dropping a link must be one difference. */
    mpp::Document unlinked = reader;
    unlinked.links.pop_back();
    const Result link_broken = mspdi_cmp::compare(unlinked, xml);
    ctx.equal(link_broken.link_differences(), std::size_t{1},
              "a missing link is one link difference");

    /* ---- Dead endpoints: links/assignments whose task slot does not exist in
     * either model.  They are counted and listed but are not a parity failure. */
    mpp::Document dead = reader;
    mpp::Link dead_chain;
    dead_chain.uid = 3;
    dead_chain.predecessor_uid = 99;
    dead_chain.successor_uid = 98;
    dead_chain.type_raw = 1;
    dead.links.push_back(dead_chain);
    mpp::Link half_dead;
    half_dead.uid = 4;
    half_dead.predecessor_uid = 1; /* live */
    half_dead.successor_uid = 98;  /* deleted slot */
    half_dead.type_raw = 1;
    dead.links.push_back(half_dead);
    mpp::Assignment dead_assignment;
    dead_assignment.uid = 11;
    dead_assignment.task_uid = 99; /* deleted task slot */
    dead_assignment.resource_uid_raw = mpp::kNullResourceUid;
    dead.assignments.push_back(dead_assignment);

    const Result dead_result = mspdi_cmp::compare(dead, xml);
    ctx.equal(dead_result.dead_endpoint_links.size(), std::size_t{2},
              "both dead-endpoint links are classified as excluded");
    ctx.equal(dead_result.link_differences(), std::size_t{0},
              "dead-endpoint links do not count as link differences");
    ctx.equal(dead_result.dead_endpoint_assignments.size(), std::size_t{1},
              "a dead-endpoint assignment is classified as excluded");
    ctx.equal(dead_result.assignment_differences(), std::size_t{0},
              "a dead-endpoint assignment does not count as an assignment difference");
    ctx.check(dead_result.ok(), "dead-endpoint rows do not fail the verdict");

    /* A reader link whose endpoints are live IS a difference even if the XML
     * has no such link. */
    mpp::Document extra_link = reader;
    mpp::Link live_extra;
    live_extra.uid = 5;
    live_extra.predecessor_uid = 1;
    live_extra.successor_uid = 2;
    live_extra.type_raw = 1;
    extra_link.links.push_back(live_extra);
    const Result extra_link_result = mspdi_cmp::compare(extra_link, xml);
    ctx.equal(extra_link_result.dead_endpoint_links.size(), std::size_t{0},
              "a live extra link is not classified as dead");
    ctx.equal(extra_link_result.link_differences(), std::size_t{1},
              "a live extra link is a link difference");

    /* A live reader assignment the export does not have is a difference. */
    mpp::Document extra_assignment = reader;
    mpp::Assignment live_extra_assignment;
    live_extra_assignment.uid = 12;
    live_extra_assignment.task_uid = 2;
    live_extra_assignment.resource_uid_raw = 1;
    live_extra_assignment.units_x100 = 10000.0;
    extra_assignment.assignments.push_back(live_extra_assignment);
    const Result extra_assignment_result = mspdi_cmp::compare(extra_assignment, xml);
    ctx.equal(extra_assignment_result.assignment_differences(), std::size_t{1},
              "a live extra assignment is one assignment difference");

    /* A resource the export does not have is a difference. */
    mpp::Document extra_resource = reader;
    mpp::Resource live_extra_resource;
    live_extra_resource.uid = 2;
    live_extra_resource.row_id = 2;
    extra_resource.resources.push_back(live_extra_resource);
    const Result extra_resource_result = mspdi_cmp::compare(extra_resource, xml);
    ctx.equal(extra_resource_result.resource_differences(), std::size_t{1},
              "a live extra resource is one resource difference");

    /* The XML side of dead-endpoint accounting: an export that carried a link
     * or assignment to a task slot that does not exist.  It is reported and
     * excluded rather than counted as a reader difference. */
    std::string xml_dead_text(kSyntheticXml);
    const std::string task2_tail = "<ConstraintType>0</ConstraintType>";
    const std::string task2_dead_link =
        "<ConstraintType>0</ConstraintType>"
        "<PredecessorLink><PredecessorUID>99</PredecessorUID><Type>1</Type>"
        "<LinkLag>0</LinkLag></PredecessorLink>";
    const std::size_t task2_at = xml_dead_text.find(task2_tail);
    if (task2_at != std::string::npos) {
        xml_dead_text.replace(task2_at, task2_tail.size(), task2_dead_link);
    }
    const std::string assignments_close = "</Assignments>";
    const std::string dead_assignment_text =
        "<Assignment><UID>11</UID><TaskUID>99</TaskUID><ResourceUID>-65535</ResourceUID>"
        "</Assignment></Assignments>";
    const std::size_t assignments_at = xml_dead_text.find(assignments_close);
    if (assignments_at != std::string::npos) {
        xml_dead_text.replace(assignments_at, assignments_close.size(),
                              dead_assignment_text);
    }
    const Result xml_dead_result =
        mspdi_cmp::compare(reader, core::io::import_mspdi(xml_dead_text));
    ctx.equal(xml_dead_result.xml_dead_endpoint_links.size(), std::size_t{1},
              "an exported link to a missing task is a named excluded link");
    ctx.equal(xml_dead_result.xml_dead_endpoint_assignments.size(), std::size_t{1},
              "an exported assignment to a missing task is excluded");
    ctx.equal(xml_dead_result.link_differences(), std::size_t{0},
              "an excluded XML link is not a link difference");
    ctx.equal(xml_dead_result.assignment_differences(), std::size_t{0},
              "an excluded XML assignment is not an assignment difference");

    /* ---- Assignment values.  Units differ (reader 50 %, export 100 %). */
    mpp::Document unit_broken = reader;
    unit_broken.assignments[0].units_x100 = 5000.0;
    const Result unit_broken_result = mspdi_cmp::compare(unit_broken, xml);
    ctx.equal(unit_broken_result.assignment_mismatches.size(), std::size_t{1},
              "a changed assignment unit is one assignment field mismatch");
    if (unit_broken_result.assignment_mismatches.size() == 1) {
        ctx.equal_string(unit_broken_result.assignment_mismatches[0].field, "Units",
                         "the assignment mismatch names Units");
    }
    mpp::Document work_broken = reader;
    work_broken.assignments[0].work_x1000 = 1200000.0; /* 1200 min = 20 h */
    const Result work_broken_result = mspdi_cmp::compare(work_broken, xml);
    ctx.equal(work_broken_result.assignment_mismatches.size(), std::size_t{1},
              "a changed assignment work is one assignment field mismatch");
    if (work_broken_result.assignment_mismatches.size() == 1) {
        ctx.equal_string(work_broken_result.assignment_mismatches[0].field, "Work",
                         "the assignment mismatch names Work");
    }

    /* When the export omits Units/Work they are not compared (and cannot
     * disagree).  Erase the two elements from the synthetic assignment. */
    std::string sparse_xml(kSyntheticXml);
    const std::string units_element = "<Units>1</Units>";
    const std::string work_element = "<Work>PT40H0M0S</Work>";
    const std::size_t units_at = sparse_xml.find(units_element);
    if (units_at != std::string::npos) {
        sparse_xml.erase(units_at, units_element.size());
    }
    const std::size_t work_at = sparse_xml.find(work_element);
    if (work_at != std::string::npos) {
        sparse_xml.erase(work_at, work_element.size());
    }
    const Result sparse_result =
        mspdi_cmp::compare(work_broken, core::io::import_mspdi(sparse_xml));
    ctx.equal(sparse_result.compared_assignment_units, std::size_t{0},
              "Units is not compared when the export omits it");
    ctx.equal(sparse_result.compared_assignment_work, std::size_t{0},
              "Work is not compared when the export omits it");
    ctx.equal(sparse_result.assignment_differences(), std::size_t{0},
              "an omitted Units/Work value cannot disagree");

    /* ---- Milestone notes: the export marks a non-zero-duration task as a
     * milestone; that is reported as a note, not a field mismatch. */
    std::string milestone_xml(kSyntheticXml);
    const std::string milestone_off = "<Duration>PT16H0M0S</Duration><Milestone>0</Milestone>";
    const std::string milestone_on = "<Duration>PT16H0M0S</Duration><Milestone>1</Milestone>";
    const std::size_t milestone_at = milestone_xml.find(milestone_off);
    if (milestone_at != std::string::npos) {
        milestone_xml.replace(milestone_at, milestone_off.size(), milestone_on);
    }
    const Result milestone_result =
        mspdi_cmp::compare(reader, core::io::import_mspdi(milestone_xml));
    ctx.equal(milestone_result.milestone_differences.size(), std::size_t{1},
              "a Milestone disagreement is a note");
    ctx.equal(milestone_result.field_mismatches.size(), std::size_t{0},
              "a Milestone disagreement is not a field mismatch");
    ctx.check(milestone_result.ok(), "milestone notes do not fail the verdict");
    ctx.check(!milestone_result.strictly_ok(), "--strict would fail on a milestone note");
}

void test_lagff1(view_test::TestContext& ctx) {
    if (!fixture::corpus_present(IO_CROSSCHECK_LAG_MPP) ||
        !fixture::corpus_present(IO_CROSSCHECK_LAG_XML)) {
        return;
    }
    const mpp::Document reader = mpp::load_file(IO_CROSSCHECK_LAG_MPP);
    const core::io::MspdiDocument xml = core::io::import_mspdi_file(IO_CROSSCHECK_LAG_XML);
    const Result result = mspdi_cmp::compare(reader, xml);

    std::cout << "lagff1 (synthetic oracle fixture):\n" << mspdi_cmp::format_report(result);
    ctx.equal(result.reader_tasks, std::size_t{6}, "lagff1 reader task count");
    ctx.equal(result.xml_tasks, std::size_t{7}, "lagff1 xml task count incl project row");
    ctx.equal(result.matched_links, std::size_t{5}, "lagff1 matched link count");
    ctx.equal(result.reader_resources, std::size_t{0}, "lagff1 reader resource count");
    ctx.equal(result.xml_resources, std::size_t{1},
              "lagff1 xml resource count (UID 0 placeholder only)");
    ctx.equal(result.compared_resources, std::size_t{0},
              "lagff1 has no live resource to compare");
    ctx.equal(result.reader_assignments, std::size_t{6}, "lagff1 reader assignment count");
    ctx.equal(result.xml_assignments, std::size_t{6}, "lagff1 xml assignment count");
    ctx.equal(result.compared_assignments, std::size_t{6},
              "lagff1 compared assignment count");
    ctx.equal(result.compared_assignment_units, std::size_t{6},
              "lagff1 assignment units compared");
    ctx.equal(result.compared_assignment_work, std::size_t{6},
              "lagff1 assignment work compared");
    ctx.equal(result.assignment_differences(), std::size_t{0},
              "lagff1 assignments agree");
    ctx.check(result.ok(), "lagff1: reader and Project's export agree exactly");
}

void test_project_a(view_test::TestContext& ctx) {
    if (!fixture::corpus_present(IO_CROSSCHECK_PROJECT_A_MPP) ||
        !fixture::corpus_present(IO_CROSSCHECK_PROJECT_A_XML)) {
        return;
    }
    const mpp::Document reader = mpp::load_file(IO_CROSSCHECK_PROJECT_A_MPP);
    const core::io::MspdiDocument xml = core::io::import_mspdi_file(IO_CROSSCHECK_PROJECT_A_XML);
    const Result result = mspdi_cmp::compare(reader, xml);

    /* The export is the ground truth this test exists to check: 84 Task
     * elements (83 tasks + the UID 0 project row), 88 PredecessorLink, 23
     * Resource elements (22 resources + the UID 0 placeholder), 67 Assignment. */
    ctx.equal(result.xml_tasks, std::size_t{84}, "PROJECT_A export has 84 Task elements");
    ctx.check(result.xml_has_project_row, "PROJECT_A export carries the UID 0 project row");
    ctx.equal(result.xml_links, std::size_t{88}, "PROJECT_A export has 88 PredecessorLink rows");
    ctx.equal(result.xml_resources, std::size_t{23}, "PROJECT_A export has 23 Resource elements");
    ctx.check(result.xml_has_placeholder_resource,
              "PROJECT_A export carries the UID 0 placeholder resource");
    ctx.equal(result.xml_assignments, std::size_t{67},
              "PROJECT_A export has 67 Assignment elements");

    ctx.equal(result.dead_endpoint_links.size(), std::size_t{0},
              "PROJECT_A has no dead-endpoint links");
    ctx.equal(result.dead_endpoint_assignments.size(), std::size_t{0},
              "PROJECT_A has no dead-endpoint assignments");
    ctx.equal(result.reader_assignments, std::size_t{67}, "PROJECT_A reader assignment count");
    ctx.equal(result.compared_assignments, std::size_t{67}, "PROJECT_A compared assignment count");
    ctx.equal(result.compared_assignment_units, std::size_t{67},
              "PROJECT_A assignment units compared");
    ctx.equal(result.compared_assignment_work, std::size_t{67}, "PROJECT_A assignment work compared");
    ctx.equal(result.compared_assignment_dates, std::size_t{67},
              "PROJECT_A assignment Start/Finish compared");
    ctx.equal(result.compared_assignment_resume, std::size_t{21},
              "PROJECT_A assignment Resume compared (21 non-empty <Resume>)");
    ctx.equal(result.assignment_differences(), std::size_t{0},
              "PROJECT_A assignments agree with the export");
    ctx.equal(result.reader_resources, std::size_t{22}, "PROJECT_A reader resource count");
    ctx.equal(result.compared_resources, std::size_t{22}, "PROJECT_A compared resource count");
    ctx.equal(result.compared_resource_types, std::size_t{22},
              "PROJECT_A resource Type compared");
    ctx.equal(result.compared_resource_max_units, std::size_t{22},
              "PROJECT_A resource MaxUnits compared");
    ctx.equal(result.resource_differences(), std::size_t{0},
              "PROJECT_A resources agree with the export");

    if (result.ok()) {
        ctx.equal(result.compared_tasks, std::size_t{83}, "PROJECT_A compared task count");
        ctx.check(true, "PROJECT_A: reader and Project's export agree on every compared field");
    } else {
        std::cout << "PROJECT_A: reader/XML differences present (reported, not an expected "
                     "value)\n"
                  << mspdi_cmp::format_report(result);
        ctx.check(true, "PROJECT_A: mismatch count reported");
    }
}

void test_project_b(view_test::TestContext& ctx) {
    if (!fixture::corpus_present(IO_CROSSCHECK_PROJECT_B_MPP) ||
        !fixture::corpus_present(IO_CROSSCHECK_PROJECT_B_XML)) {
        return;
    }
    const mpp::Document reader = mpp::load_file(IO_CROSSCHECK_PROJECT_B_MPP);
    const core::io::MspdiDocument xml =
        core::io::import_mspdi_file(IO_CROSSCHECK_PROJECT_B_XML);
    const Result result = mspdi_cmp::compare(reader, xml);

    std::cout << "PROJECT_B (real programme, Project's export):\n"
              << mspdi_cmp::format_report(result);

    /* The export's ground truth. */
    ctx.equal(result.xml_tasks, std::size_t{74}, "PROJECT_B export has 74 Task elements");
    ctx.check(result.xml_has_project_row, "PROJECT_B export carries the UID 0 project row");
    ctx.equal(result.xml_links, std::size_t{70}, "PROJECT_B export has 70 PredecessorLink rows");
    ctx.equal(result.xml_resources, std::size_t{23},
              "PROJECT_B export has 23 Resource elements");
    ctx.equal(result.xml_assignments, std::size_t{63},
              "PROJECT_B export has 63 Assignment elements");

    /* Wave 11: the reader applies the FixedMeta tombstone state to link and
     * assignment liveness.  PROJECT_B's 2 deleted-slot links and 10 assignments
     * are now SKIPPED rows, not dead endpoints, so there is nothing left to
     * exclude; the live sets still agree with Project's export exactly. */
    ctx.equal(result.dead_endpoint_links.size(), std::size_t{0},
              "PROJECT_B has no dead-endpoint links after the tombstone fix");
    ctx.equal(result.live_reader_links, std::size_t{70},
              "PROJECT_B has 70 live reader links");
    ctx.equal(result.matched_links, std::size_t{70},
              "PROJECT_B: all 70 live links match the export");
    ctx.equal(result.link_differences(), std::size_t{0},
              "PROJECT_B: no live link difference");

    ctx.equal(result.reader_assignments, std::size_t{63},
              "PROJECT_B reader assignment count (tombstoned rows skipped)");
    ctx.equal(result.dead_endpoint_assignments.size(), std::size_t{0},
              "PROJECT_B has no dead-endpoint assignments after the tombstone fix");
    ctx.equal(result.compared_assignments, std::size_t{63},
              "PROJECT_B has 63 live assignments compared");
    ctx.equal(result.compared_assignment_units, std::size_t{63},
              "PROJECT_B assignment units compared");
    ctx.equal(result.compared_assignment_work, std::size_t{63},
              "PROJECT_B assignment work compared");
    ctx.equal(result.compared_assignment_dates, std::size_t{63},
              "PROJECT_B assignment Start/Finish compared");
    ctx.equal(result.compared_assignment_resume, std::size_t{0},
              "PROJECT_B export carries no <Resume>");
    ctx.equal(result.assignment_differences(), std::size_t{0},
              "PROJECT_B: all 63 live assignments match the export");

    ctx.equal(result.reader_resources, std::size_t{22}, "PROJECT_B reader resource count");
    ctx.equal(result.compared_resources, std::size_t{22},
              "PROJECT_B: all 22 live resources match the export");
    ctx.equal(result.compared_resource_types, std::size_t{22},
              "PROJECT_B resource Type compared");
    ctx.equal(result.compared_resource_max_units, std::size_t{22},
              "PROJECT_B resource MaxUnits compared");
    ctx.equal(result.resource_differences(), std::size_t{0},
              "PROJECT_B: no resource difference");
    ctx.equal(result.uid_differences(), std::size_t{0}, "PROJECT_B: no task UID difference");
    ctx.equal(result.problems.size(), std::size_t{0}, "PROJECT_B: no setup problems");

    /* The reader reads the stored FixedMeta milestone bit and falls back to
     * duration == 0 (core/mpp/reader.h, "Milestone").  With that reader PROJECT_B's
     * two stored-flag milestones agree with the export, so the note count is 0;
     * it was 2 while the reader was duration-only. */
    ctx.equal(result.milestone_differences.size(), std::size_t{0},
              "PROJECT_B: no milestone note with the stored-bit reader");

    /* Residual: one task's Start/Finish.  The reader reads +96/+100 (calculated)
     * while the export carries +104/+108 (effective); described in memory/io.md.
     * Reported, not pinned: a core/mpp change to the effective pair is expected
     * to remove it. */
    for (const mspdi_cmp::FieldMismatch& mismatch : result.field_mismatches) {
        std::cout << "PROJECT_B residual field difference: uid " << mismatch.uid << ' '
                  << mismatch.field << " reader " << mismatch.reader_value << " xml "
                  << mismatch.xml_value << '\n';
    }
    ctx.check(result.field_mismatches.size() <= std::size_t{2},
              "PROJECT_B: at most the stored-vs-effective Start/Finish pair remains");
}

/* assign1.mpp is a synthetic fixture with three REAL assignments (unlike the
 * corpus, whose assignment rows all name no resource).  W40: its MSPDI export
 * assign1.xml is on disk now, so this is the real-resource and Units-convention
 * cross-check. */
void test_assign1(view_test::TestContext& ctx) {
    if (!fixture::corpus_present(IO_CROSSCHECK_ASSIGN_MPP) ||
        !fixture::corpus_present(IO_CROSSCHECK_ASSIGN_XML)) {
        return;
    }
    const mpp::Document reader = mpp::load_file(IO_CROSSCHECK_ASSIGN_MPP);
    const core::io::MspdiDocument xml =
        core::io::import_mspdi_file(IO_CROSSCHECK_ASSIGN_XML);
    const Result result = mspdi_cmp::compare(reader, xml);
    std::cout << "assign1 (synthetic fixture with real resources):\n"
              << mspdi_cmp::format_report(result);

    ctx.equal(reader.assignments.size(), std::size_t{3}, "assign1 reader assignment count");
    ctx.equal(reader.resources.size(), std::size_t{2}, "assign1 reader resource count");
    ctx.equal(result.compared_assignments, std::size_t{3},
              "assign1: all 3 real assignments compared");
    ctx.equal(result.compared_assignment_units, std::size_t{3},
              "assign1 assignment Units compared (export fraction 1 = reader 100 %)");
    ctx.equal(result.compared_assignment_work, std::size_t{3},
              "assign1 assignment Work compared");
    ctx.equal(result.compared_assignment_dates, std::size_t{3},
              "assign1 assignment Start/Finish compared");
    ctx.equal(result.compared_assignment_resume, std::size_t{0},
              "assign1 exports no <Resume> (no progress)");
    ctx.equal(result.compared_resources, std::size_t{2},
              "assign1: both real resources compared (UID 0 placeholder excluded)");
    ctx.equal(result.compared_resource_types, std::size_t{2},
              "assign1 resource Type compared");
    ctx.equal(result.compared_resource_max_units, std::size_t{2},
              "assign1 resource MaxUnits compared");
    ctx.equal(result.assignment_differences(), std::size_t{0},
              "assign1: real resource UIDs, Units and Work agree");
    ctx.equal(result.resource_differences(), std::size_t{0},
              "assign1: resources agree");
    ctx.check(result.ok(), "assign1: reader and Project's export agree");
}

}  // namespace

int main() {
    view_test::TestContext ctx;
    try {
        test_in_memory(ctx);
        test_lagff1(ctx);
        test_project_a(ctx);
        test_project_b(ctx);
        test_assign1(ctx);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
        return 1;
    }
    return ctx.finish("io_mspdi_crosscheck");
}
