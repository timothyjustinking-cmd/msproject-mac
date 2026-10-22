// tests/io_mspdi_tests.cpp -- the MSPDI path: XML parser, project header,
// tasks/resources/assignments into RecordSets, links, warnings and errors.
#include <string>
#include <vector>

#include "mpp_bridge.h"
#include "mspdi.h"
#include "value.h"
#include "view_test_support.h"
#include "xml.h"

namespace {

using core::view::Value;
using core::view::ValueKind;

constexpr core::view::FieldId kName = 0x0b40000e;
constexpr core::view::FieldId kDuration = 0x0b40001d;
constexpr core::view::FieldId kStart = 0x0b400023;
constexpr core::view::FieldId kFinish = 0x0b400024;
constexpr core::view::FieldId kPercentComplete = 0x0b400020;
constexpr core::view::FieldId kMilestone = 0x0b400018;
constexpr core::view::FieldId kSummary = 0x0b40005c;
constexpr core::view::FieldId kCritical = 0x0b400013;
constexpr core::view::FieldId kNotes = 0x0b40000f;
constexpr core::view::FieldId kCost = 0x0b400005;
constexpr core::view::FieldId kFixedCost = 0x0b400008;
constexpr core::view::FieldId kText1 = 0x0b400033;
constexpr core::view::FieldId kNumber1 = 0x0b400057;
constexpr core::view::FieldId kFlag1 = 0x0b400048;
constexpr core::view::FieldId kDate1 = 0x0b400109;

std::string sample(const char* name) {
    return std::string(IO_SAMPLE_DIR) + "/" + name;
}

void test_xml_parser(view_test::TestContext& ctx) {
    const auto parsed = core::io::parse_xml(
        "<?xml version=\"1.0\"?><!-- comment --><root a=\"1\" b='x&amp;y'>"
        "<child>text &lt;ok&gt; &#65;</child><empty/></root>");
    ctx.check(parsed.ok, "parse XML document: " + parsed.error);
    if (!parsed.ok) {
        return;
    }
    ctx.equal_string(parsed.root.name, "root", "root name");
    ctx.equal(parsed.root.attributes.size(), std::size_t{2}, "attribute count");
    ctx.equal_string(core::io::text_of(*core::io::child(parsed.root, "child")),
                     "text <ok> A", "entities decoded in text");
    ctx.check(core::io::child(parsed.root, "empty") != nullptr, "self-closing element");
    ctx.equal_string(core::io::attribute(parsed.root, "b")->value, "x&y",
                     "entities decoded in attribute");

    const auto cdata = core::io::parse_xml("<root><![CDATA[a<b&c]]></root>");
    ctx.check(cdata.ok, "parse CDATA");
    ctx.equal_string(core::io::text_of(cdata.root), "a<b&c", "CDATA text");

    const auto prefixed =
        core::io::parse_xml("<m:root xmlns:m=\"urn:x\"><m:child>v</m:child></m:root>");
    ctx.check(prefixed.ok, "parse namespaced document");
    ctx.equal_string(prefixed.root.name, "root", "prefix stripped from element");
    ctx.equal_string(prefixed.root.children[0].name, "child", "prefix stripped from child");

    const auto mismatched = core::io::parse_xml("<a></b>");
    ctx.check(!mismatched.ok, "mismatched closing tag rejected");
    ctx.contains(mismatched.error, "mismatched", "mismatch names the problem");

    const auto unterminated = core::io::parse_xml("<a><b></a>");
    ctx.check(!unterminated.ok, "unterminated element rejected");

    const auto unclosed_comment = core::io::parse_xml("<a><!-- x</a>");
    ctx.check(!unclosed_comment.ok, "unclosed comment rejected");

    const auto bad_entity = core::io::parse_xml("<a>&nbsp;</a>");
    ctx.check(!bad_entity.ok, "unknown entity rejected");
    ctx.contains(bad_entity.error, "unknown entity", "unknown entity message");

    const auto duplicate = core::io::parse_xml("<a x=\"1\" x=\"2\"/>");
    ctx.check(!duplicate.ok, "duplicate attribute rejected");

    const auto trailing = core::io::parse_xml("<a/>text");
    ctx.check(!trailing.ok, "trailing text rejected");
    ctx.contains(trailing.error, "after the document element", "trailing text message");

    ctx.equal_string(core::io::escape_xml("<a & \"b\">"), "&lt;a &amp; &quot;b&quot;&gt;",
                     "escape_xml");

    bool flag = false;
    ctx.check(core::io::parse_bool_text("1", flag) && flag, "parse bool 1");
    ctx.check(core::io::parse_bool_text("false", flag) && !flag, "parse bool false");
    ctx.check(!core::io::parse_bool_text("yes", flag), "parse bool rejects yes");
}

void test_minimal(view_test::TestContext& ctx) {
    const core::io::MspdiDocument document =
        core::io::import_mspdi_file(sample("io_sample_minimal.xml"));
    ctx.equal_string(document.project.name, "Minimal", "project name");
    ctx.equal_string(document.project.title, "Minimal project", "project title");
    ctx.check(document.project.start.kind == ValueKind::Date, "project start is a date");
    ctx.equal(document.project.start.date.year, 2024, "project start year");
    ctx.equal(document.project.start.date.hour, 8, "project start hour");
    ctx.check(document.project.finish.kind == ValueKind::Date, "project finish is a date");
    ctx.check(document.project.has_schedule_from_start, "ScheduleFromStart present");
    ctx.check(document.project.schedule_from_start, "ScheduleFromStart true");
    ctx.equal(document.project.minutes_per_day, 480, "minutes per day");
    ctx.equal(document.project.minutes_per_week, 2400, "minutes per week");
    ctx.equal(document.project.days_per_month, 20, "days per month");
    ctx.equal(document.project.calendar_uid, 3, "calendar uid");
    ctx.equal(document.tasks.row_count(), std::size_t{1}, "minimal task count");
    ctx.equal(document.tasks.uid(0), 1u, "task uid");
    ctx.equal_string(document.tasks.get(0, kName)->text, "Task one", "task name");
    ctx.equal(document.tasks.get(0, kDuration)->integer, std::int64_t{4800},
              "task duration in 6s ticks");
    ctx.equal(document.tasks.get(0, kStart)->date.hour, 8, "task start hour");
    ctx.equal(document.tasks.get(0, kFinish)->date.hour, 17, "task finish hour");
    ctx.equal(document.tasks.get(0, kPercentComplete)->integer, std::int64_t{25},
              "percent complete");
    ctx.check(!document.tasks.get(0, kMilestone)->boolean, "milestone false");
    ctx.check(!document.tasks.get(0, kSummary)->boolean, "summary false");
    ctx.check(document.tasks.get(0, kCritical)->boolean, "critical true");
    ctx.check(document.tasks.has_field(kDuration), "duration is in the task schema");
    ctx.check(document.tasks.has_field(kStart), "start is in the task schema");
    ctx.equal(document.warnings.size(), std::size_t{0}, "minimal file has no warnings");
}

void test_tasks(view_test::TestContext& ctx) {
    const core::io::MspdiDocument document =
        core::io::import_mspdi_file(sample("io_sample_tasks.xml"));
    ctx.equal(document.tasks.row_count(), std::size_t{3}, "three tasks");
    ctx.equal_string(document.project.name, "Tasks sample", "tasks project name");

    ctx.equal_string(document.tasks.get(0, kName)->text, "Phase & design",
                     "attribute-free entity decoded in name");
    ctx.check(document.tasks.get(0, kSummary)->boolean, "task 1 is a summary");
    ctx.equal(document.tasks.get(0, kDuration)->integer, std::int64_t{24000},
              "summary duration 40h");
    ctx.equal(document.tasks.get(0, kPercentComplete)->integer, std::int64_t{0},
              "summary percent");

    ctx.equal_string(document.tasks.get(1, kName)->text, "Draft <spec>", "escaped name");
    ctx.equal(document.tasks.get(1, kPercentComplete)->integer, std::int64_t{50},
              "percent complete");
    ctx.equal(document.tasks.get(1, kCost)->number, 1250.5, "cost");
    ctx.equal(document.tasks.get(1, kFixedCost)->number, 100.0, "fixed cost");
    ctx.equal_string(document.tasks.get(1, kNotes)->text, "Line one & line two",
                     "CDATA notes preserved");
    ctx.equal_string(document.tasks.get(1, kText1)->text, "draft", "custom text field");
    ctx.equal(document.tasks.get(1, kNumber1)->number, 7.0, "custom number field");
    ctx.check(document.tasks.get(1, kFlag1)->boolean, "custom flag field");
    ctx.check(document.tasks.get(1, kDate1)->kind == ValueKind::Date, "custom date field");
    ctx.check(document.tasks.get(1, kSummary) != nullptr &&
                  !document.tasks.get(1, kSummary)->boolean,
              "task 2 is not a summary");

    ctx.check(document.tasks.get(2, kMilestone)->boolean, "task 3 is a milestone");
    ctx.equal(document.tasks.get(2, kDuration)->integer, std::int64_t{0},
              "milestone duration zero");

    ctx.equal(document.links.size(), std::size_t{1}, "one predecessor link");
    if (!document.links.empty()) {
        ctx.equal(document.links[0].predecessor_uid, 2u, "link predecessor uid");
        ctx.equal(document.links[0].successor_uid, 3u, "link successor uid");
        ctx.equal(document.links[0].type, 1, "link type");
        ctx.equal(document.links[0].lag_ticks, std::int64_t{4800}, "link lag ticks");
        ctx.equal(document.links[0].lag_format, 7, "link lag format");
    }

    bool duration_format_warned = false;
    for (const auto& warning : document.warnings) {
        if (warning.find("<DurationFormat>") != std::string::npos) {
            duration_format_warned = true;
        }
        ctx.check(warning.find("Task[UID=") != std::string::npos ||
                      warning.find("unsupported Task element") != std::string::npos,
                  "warning is attributable: " + warning);
    }
    ctx.check(duration_format_warned, "unsupported DurationFormat is warned once");
}

/* The shapes Project's own export uses (corpus/probe/project_a-export.xml is the
 * reference file): <ConstraintType> is an integer element, and <LinkLag> is a
 * decimal count of tenths of a minute, which is the reader's 6-second tick.
 * The hand-written samples keep the ISO duration spelling, so both are covered
 * here and in test_tasks(). */
void test_project_export_shapes(view_test::TestContext& ctx) {
    constexpr core::view::FieldId kConstraintType = 0x0b400011;

    const core::io::MspdiDocument document = core::io::import_mspdi(
        "<Project><Tasks><Task><UID>7</UID>"
        "<ConstraintType>4</ConstraintType>"
        "<ConstraintDate>2026-10-20T08:00:00</ConstraintDate>"
        "<PredecessorLink><PredecessorUID>3</PredecessorUID><Type>1</Type>"
        "<LinkLag>240000</LinkLag><LagFormat>7</LagFormat></PredecessorLink>"
        "</Task></Tasks></Project>");

    ctx.equal(document.tasks.row_count(), std::size_t{1}, "project-form task imported");
    const Value* type = document.tasks.get(0, kConstraintType);
    ctx.check(type != nullptr && type->kind == ValueKind::Integer,
              "ConstraintType is an Integer field");
    if (type != nullptr) {
        ctx.equal(type->integer, std::int64_t{4}, "ConstraintType value");
    }
    ctx.equal(document.links.size(), std::size_t{1}, "decimal LinkLag link imported");
    if (!document.links.empty()) {
        ctx.equal(document.links[0].predecessor_uid, 3u, "decimal link predecessor");
        ctx.equal(document.links[0].type, 1, "decimal link type");
        /* 240000 tenths of a minute = 24000 min = 50 eight-hour days. */
        ctx.equal(document.links[0].lag_ticks, std::int64_t{240000},
                  "decimal LinkLag is tenths of a minute, i.e. ticks");
        ctx.equal(document.links[0].lag_format, 7, "decimal link lag format");
    }
    ctx.equal(document.warnings.size(), std::size_t{0}, "project-form link has no warnings");

    /* Project writes the "no resource" sentinel as -65535 in <ResourceUID>.
     * Its two's-complement bits (0xFFFF0001) are the reader's
     * kNullResourceUid, so the importer must keep them instead of warning and
     * folding the value to 0. */
    const core::io::MspdiDocument assigned = core::io::import_mspdi(
        "<Project><Tasks><Task><UID>1</UID></Task></Tasks>"
        "<Assignments><Assignment><UID>5</UID><TaskUID>1</TaskUID>"
        "<ResourceUID>-65535</ResourceUID><Units>1</Units><Work>PT8H0M0S</Work>"
        "</Assignment></Assignments></Project>");
    ctx.equal(assigned.assignment_refs.size(), std::size_t{1},
              "assignment ref imported from a Project-shaped document");
    if (!assigned.assignment_refs.empty()) {
        ctx.equal(assigned.assignment_refs[0].resource_uid, 0xFFFF0001u,
                  "-65535 ResourceUID keeps the no-resource sentinel bits");
    }
    ctx.equal(assigned.warnings.size(), std::size_t{0},
              "the -65535 ResourceUID does not warn");
}

/* The canonical LinkLag rule (core/io/mspdi.h): decimal tenths of a minute is
 * the Project-export spelling and the writer's output; ISO is the legacy
 * hand-written-sample spelling and is still accepted on import.  Both exact
 * strings are pinned so neither can drift silently. */
void test_link_lag_spellings(view_test::TestContext& ctx) {
    std::int64_t ticks = -1;
    ctx.check(core::io::parse_link_lag("240000", ticks) ==
                  core::io::LinkLagSpelling::decimal,
              "decimal LinkLag is the canonical spelling");
    ctx.equal(ticks, std::int64_t{240000}, "decimal LinkLag value in ticks");

    ctx.check(core::io::parse_link_lag("PT40H0M0S", ticks) == core::io::LinkLagSpelling::iso,
              "ISO LinkLag is recognised as the legacy spelling");
    ctx.equal(ticks, std::int64_t{24000}, "ISO PT40H0M0S is five 8-hour days");

    ctx.check(core::io::parse_link_lag("-4800", ticks) == core::io::LinkLagSpelling::decimal,
              "a negative decimal LinkLag (lead) is recognised");
    ctx.equal(ticks, std::int64_t{-4800}, "negative decimal LinkLag value");

    ctx.check(core::io::parse_link_lag("PT0H0M0S", ticks) == core::io::LinkLagSpelling::iso,
              "zero ISO LinkLag is recognised");
    ctx.equal(ticks, std::int64_t{0}, "zero ISO LinkLag value");

    std::int64_t untouched = 12345;
    ctx.check(core::io::parse_link_lag("2 days", untouched) ==
                  core::io::LinkLagSpelling::none,
              "an unknown LinkLag spelling is rejected");
    ctx.equal(untouched, std::int64_t{12345},
              "a rejected LinkLag leaves the output value untouched");

    ctx.equal_string(core::io::format_link_lag_decimal(4800), "4800",
                     "canonical decimal text for one day");
    ctx.equal_string(core::io::format_link_lag_decimal(-4800), "-4800",
                     "canonical decimal text for a lead");
    ctx.equal_string(core::io::format_link_lag_iso(4800), "PT8H0M0S", "legacy ISO text");
    ctx.equal_string(core::io::format_link_lag_iso(24000), "PT40H0M0S",
                     "legacy ISO text for five days");
    ctx.equal_string(core::io::format_link_lag_iso(0), "PT0H0M0S", "legacy ISO text for zero");
    ctx.equal_string(core::io::format_link_lag_iso(-4800), "-PT8H0M0S",
                     "legacy ISO text for a lead");
}

void test_resources(view_test::TestContext& ctx) {
    const core::io::MspdiDocument document =
        core::io::import_mspdi_file(sample("io_sample_full.xml"));
    ctx.equal_string(document.project.name, "Assignments sample", "namespaced project name");
    ctx.equal(document.tasks.row_count(), std::size_t{2}, "namespaced tasks");
    ctx.equal(document.resources.row_count(), std::size_t{2}, "two resources");
    ctx.equal(document.assignments.row_count(), std::size_t{3}, "three assignments");
    ctx.equal(document.assignment_refs.size(), std::size_t{3}, "three assignment refs");

    constexpr core::view::FieldId kResourceName = 0x0c400001;
    constexpr core::view::FieldId kMaxUnits = 0x0c400004;
    constexpr core::view::FieldId kStandardRate = 0x0c400006;
    ctx.equal_string(document.resources.get(0, kResourceName)->text, "Alice", "resource name");
    ctx.equal(document.resources.get(0, kMaxUnits)->number, 100.0, "max units");
    ctx.equal(document.resources.get(0, kStandardRate)->number, 120.0, "standard rate");
    ctx.equal(document.resources.get(1, kMaxUnits)->number, 50.0, "second resource max units");

    constexpr core::view::FieldId kUnits = 0x0f400007;
    constexpr core::view::FieldId kWork = 0x0f400008;
    constexpr core::view::FieldId kAssigneeTaskId = 0x0f400003;
    ctx.equal(document.assignments.get(0, kUnits)->number, 100.0, "assignment units");
    ctx.equal(document.assignments.get(0, kWork)->integer, std::int64_t{14400},
              "assignment work 24h");
    ctx.equal(document.assignments.get(0, kAssigneeTaskId)->integer, std::int64_t{1},
              "assignment task id");
    if (document.assignment_refs.size() == 3) {
        ctx.equal(document.assignment_refs[0].task_uid, 1u, "assignment task uid");
        ctx.equal(document.assignment_refs[0].resource_uid, 1u, "assignment resource uid");
        ctx.equal(document.assignment_refs[2].task_uid, 2u, "third assignment task uid");
    }

    bool email_warned = false;
    for (const auto& warning : document.warnings) {
        if (warning.find("<EmailAddress>") != std::string::npos) {
            email_warned = true;
        }
    }
    ctx.check(email_warned, "unsupported EmailAddress is warned");

    core::io::MspdiLoadOptions no_resources;
    no_resources.import_resources = false;
    no_resources.import_assignments = false;
    const auto tasks_only = core::io::import_mspdi(
        view_test::read_text_file(sample("io_sample_full.xml")), no_resources);
    ctx.equal(tasks_only.tasks.row_count(), std::size_t{2}, "tasks still imported");
    ctx.equal(tasks_only.resources.row_count(), std::size_t{0}, "resources skipped");
    ctx.equal(tasks_only.assignments.row_count(), std::size_t{0}, "assignments skipped");
}

void test_errors(view_test::TestContext& ctx) {
    bool threw = false;
    try {
        static_cast<void>(core::io::import_mspdi("<Foo/>"));
    } catch (const core::io::MspdiError& error) {
        threw = true;
        ctx.contains(error.what(), "expected <Project>", "wrong root error names Project");
    }
    ctx.check(threw, "wrong root throws MspdiError");

    threw = false;
    try {
        static_cast<void>(core::io::import_mspdi("<Project><Tasks>"));
    } catch (const core::io::MspdiError& error) {
        threw = true;
        ctx.contains(error.what(), "line", "malformed XML error carries a line");
    }
    ctx.check(threw, "malformed XML throws MspdiError");

    threw = false;
    try {
        static_cast<void>(core::io::import_mspdi_file(sample("does-not-exist.xml")));
    } catch (const core::io::MspdiError& error) {
        threw = true;
        ctx.contains(error.what(), "does-not-exist.xml", "missing file error names the path");
    }
    ctx.check(threw, "missing file throws MspdiError");

    threw = false;
    try {
        static_cast<void>(core::io::import_mspdi("<Project><Tasks><Task><UID>x</UID>"
                                                 "<Duration>nope</Duration>"
                                                 "</Task></Tasks></Project>"));
    } catch (const core::io::MspdiError&) {
        threw = true;
    }
    ctx.check(!threw, "bad values are warnings, not a thrown error");
}

#ifdef CORE_IO_HAVE_MPP_READER
/* The bridge is compiled only when core/mpp/reader.h is on the include path.
 * It needs no .mpp fixture and links no reader symbol: mpp::Document is a
 * header-defined value type, so the adapter itself is what is under test. */
void test_reader_bridge(view_test::TestContext& ctx) {
    mpp::Document document;
    document.project.title = "Bridge project";

    mpp::Task task;
    task.row = 0;
    task.row_id = 0;
    task.uid = 7;
    task.name = "From MPP";
    task.duration_ticks = 9600;
    task.priority = 500;
    task.percent_complete = 25;
    task.start.raw = 0x391312c0u;  /* day 14611 = 2024-01-01, 08:00 */
    task.finish.raw = 0x391412c0u; /* day 14612 = 2024-01-02, 08:00 */
    document.tasks.push_back(task);

    const core::view::RecordSet records = core::io::to_record_set(document);
    ctx.equal(records.row_count(), std::size_t{1}, "bridge row count");
    ctx.equal(records.uid(0), 7u, "bridge uid");
    ctx.equal_string(records.get(0, kName)->text, "From MPP", "bridge name");
    ctx.equal(records.get(0, kDuration)->integer, std::int64_t{9600}, "bridge duration");
    ctx.check(records.get(0, kDuration)->kind == ValueKind::Duration,
              "bridge duration keeps Duration kind");
    ctx.equal(records.get(0, 0x0b400019)->integer, std::int64_t{500}, "bridge priority");
    ctx.equal(records.get(0, kPercentComplete)->integer, std::int64_t{25},
              "bridge percent complete");
    ctx.check(records.get(0, kStart)->kind == ValueKind::Date, "bridge start is a date");
    ctx.equal(records.get(0, kStart)->date.year, 2024, "bridge start year");
    ctx.equal(static_cast<int>(records.get(0, kStart)->date.month), 1, "bridge start month");
    ctx.equal(static_cast<int>(records.get(0, kStart)->date.day), 1, "bridge start day");
    ctx.equal(static_cast<int>(records.get(0, kStart)->date.hour), 8, "bridge start hour");
    ctx.equal(static_cast<int>(records.get(0, kFinish)->date.day), 2, "bridge finish day");

    mpp::Document empty;
    const core::view::RecordSet empty_records = core::io::to_record_set(empty);
    ctx.equal(empty_records.row_count(), std::size_t{0}, "empty mpp document");
    ctx.check(empty_records.has_field(kName), "bridge schema is present even with no rows");
}
#endif

}  // namespace

int main() {
    view_test::TestContext ctx;
    test_xml_parser(ctx);
    test_minimal(ctx);
    test_tasks(ctx);
    test_project_export_shapes(ctx);
    test_link_lag_spellings(ctx);
    test_resources(ctx);
    test_errors(ctx);
#ifdef CORE_IO_HAVE_MPP_READER
    test_reader_bridge(ctx);
#endif
    return ctx.finish("io_mspdi_tests");
}
