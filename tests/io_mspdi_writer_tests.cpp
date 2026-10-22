// tests/io_mspdi_writer_tests.cpp -- the MSPDI export path: round-trip of the
// three committed samples, escaping, UTF-8, empty values, durations, dates, the
// <Project> root/namespace, and one probe per mapped field.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "field_ref.h"
#include "field_table.h"
#include "mspdi.h"
#include "mspdi_writer.h"
#include "value.h"
#include "view_test_support.h"
#include "xml.h"

namespace {

using core::model::RecordType;
using core::view::DateValue;
using core::view::FieldId;
using core::view::RecordSet;
using core::view::Value;
using core::view::ValueKind;

std::string sample(const char* name) {
    return std::string(IO_SAMPLE_DIR) + "/" + name;
}

/* Ids are resolved by name through the descriptor table, like the importer and
 * the views do; a hardcoded id here would hide a regeneration mistake. */
FieldId field_id(view_test::TestContext& ctx, RecordType record_type, const char* name) {
    FieldId field = 0;
    const bool found = core::view::field_id_by_name(record_type, name, field);
    ctx.check(found, std::string("field name resolves: ") + name);
    return field;
}

bool same_value(const Value& a, const Value& b) {
    if (a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
        case ValueKind::Null: return true;
        case ValueKind::Bool: return a.boolean == b.boolean;
        case ValueKind::Integer:
        case ValueKind::Duration: return a.integer == b.integer;
        case ValueKind::Number: return a.number == b.number;
        case ValueKind::Text: return a.text == b.text;
        case ValueKind::Date: return a.date == b.date;
    }
    return false;
}

std::string hex_field(FieldId field) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", field);
    return std::string(buffer);
}

/* Only the empty containers may use the self-closing form; a value element
 * written as <x/> would import as Null and is forbidden by the writer's rules. */
std::size_t count_self_closing(const std::string& xml) {
    std::size_t count = 0;
    for (std::size_t at = xml.find("/>"); at != std::string::npos;
         at = xml.find("/>", at + 2)) {
        ++count;
    }
    return count;
}

void check_records(view_test::TestContext& ctx, const RecordSet& actual,
                   const RecordSet& expected, const std::string& label) {
    ctx.equal(actual.row_count(), expected.row_count(), label + ": row count");
    if (actual.row_count() != expected.row_count()) {
        return;
    }
    ctx.check(actual.fields() == expected.fields(), label + ": field schema");
    if (actual.fields() != expected.fields()) {
        return;
    }
    for (std::size_t row = 0; row < expected.row_count(); ++row) {
        ctx.equal(actual.uid(row), expected.uid(row),
                  label + ": UID of row " + std::to_string(row));
        for (std::size_t column = 0; column < expected.field_count(); ++column) {
            const Value* actual_value = actual.at(row, column);
            const Value* expected_value = expected.at(row, column);
            const bool same = actual_value != nullptr && expected_value != nullptr &&
                              same_value(*actual_value, *expected_value);
            if (!same) {
                ctx.check(false, label + ": row " + std::to_string(row) + " field " +
                                     hex_field(expected.fields()[column]));
            } else {
                ctx.check(true, label);
            }
        }
    }
}

void check_project(view_test::TestContext& ctx, const core::io::MspdiProject& actual,
                   const core::io::MspdiProject& expected, const std::string& label) {
    ctx.equal_string(actual.name, expected.name, label + ": project name");
    ctx.equal_string(actual.title, expected.title, label + ": project title");
    ctx.check(same_value(actual.start, expected.start), label + ": project start");
    ctx.check(same_value(actual.finish, expected.finish), label + ": project finish");
    ctx.equal(actual.has_schedule_from_start, expected.has_schedule_from_start,
              label + ": ScheduleFromStart present");
    ctx.equal(actual.schedule_from_start, expected.schedule_from_start,
              label + ": ScheduleFromStart value");
    ctx.equal(actual.minutes_per_day, expected.minutes_per_day, label + ": minutes per day");
    ctx.equal(actual.minutes_per_week, expected.minutes_per_week,
              label + ": minutes per week");
    ctx.equal(actual.days_per_month, expected.days_per_month, label + ": days per month");
    ctx.equal(actual.calendar_uid, expected.calendar_uid, label + ": calendar uid");
    ctx.equal_string(actual.default_start_time, expected.default_start_time,
                     label + ": default start time");
    ctx.equal_string(actual.default_finish_time, expected.default_finish_time,
                     label + ": default finish time");
}

void check_document(view_test::TestContext& ctx, const core::io::MspdiDocument& actual,
                    const core::io::MspdiDocument& expected, const std::string& label) {
    check_project(ctx, actual.project, expected.project, label);
    check_records(ctx, actual.tasks, expected.tasks, label + " tasks");
    check_records(ctx, actual.resources, expected.resources, label + " resources");
    check_records(ctx, actual.assignments, expected.assignments, label + " assignments");

    ctx.equal(actual.links.size(), expected.links.size(), label + ": link count");
    if (actual.links.size() == expected.links.size()) {
        for (std::size_t i = 0; i < expected.links.size(); ++i) {
            const core::io::PredecessorLink& link = actual.links[i];
            const core::io::PredecessorLink& want = expected.links[i];
            const std::string at = label + ": link " + std::to_string(i);
            ctx.equal(link.predecessor_uid, want.predecessor_uid, at + " predecessor");
            ctx.equal(link.successor_uid, want.successor_uid, at + " successor");
            ctx.equal(link.type, want.type, at + " type");
            ctx.equal(link.lag_ticks, want.lag_ticks, at + " lag ticks");
            ctx.equal(link.lag_format, want.lag_format, at + " lag format");
        }
    }

    ctx.equal(actual.assignment_refs.size(), expected.assignment_refs.size(),
              label + ": assignment ref count");
    if (actual.assignment_refs.size() == expected.assignment_refs.size()) {
        for (std::size_t i = 0; i < expected.assignment_refs.size(); ++i) {
            const core::io::MspdiAssignmentRef& ref = actual.assignment_refs[i];
            const core::io::MspdiAssignmentRef& want = expected.assignment_refs[i];
            const std::string at = label + ": assignment ref " + std::to_string(i);
            ctx.equal(ref.uid, want.uid, at + " uid");
            ctx.equal(ref.task_uid, want.task_uid, at + " task uid");
            ctx.equal(ref.resource_uid, want.resource_uid, at + " resource uid");
        }
    }
}

struct RoundTripResult {
    std::string xml;
    core::io::MspdiDocument document;
    bool parsed = false;
};

/* parse -> write -> parse, checking well-formedness, the root and namespace,
 * warning-free re-import, and an equal model. */
RoundTripResult round_trip(view_test::TestContext& ctx, const char* name) {
    const std::string label(name);
    RoundTripResult result;
    result.document = core::io::import_mspdi_file(sample(name));
    result.xml = core::io::mspdi::write(result.document);

    ctx.check(result.xml.rfind(
                  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n", 0) == 0,
              label + ": XML declaration");

    const core::io::XmlParseResult parsed = core::io::parse_xml(result.xml);
    ctx.check(parsed.ok, label + ": writer output is well-formed XML: " + parsed.error);
    if (!parsed.ok) {
        return result;
    }
    result.parsed = true;
    ctx.equal_string(parsed.root.name, "Project", label + ": root element");
    const core::io::XmlAttribute* xmlns = core::io::attribute(parsed.root, "xmlns");
    ctx.check(xmlns != nullptr, label + ": root declares a default namespace");
    if (xmlns != nullptr) {
        ctx.equal_string(xmlns->value, "http://schemas.microsoft.com/project",
                         label + ": namespace URI");
    }
    ctx.contains(result.xml, "<Tasks", label + ": <Tasks> present");
    ctx.contains(result.xml, "<Resources", label + ": <Resources> present");
    ctx.contains(result.xml, "<Assignments", label + ": <Assignments> present");

    const core::io::MspdiDocument reparsed = core::io::import_mspdi(result.xml);
    std::string warnings;
    for (const std::string& warning : reparsed.warnings) {
        warnings += "\n  " + warning;
    }
    ctx.check(reparsed.warnings.empty(), label + ": re-import has no warnings" + warnings);
    check_document(ctx, reparsed, result.document, label);
    result.document = reparsed;
    return result;
}

void test_round_trip_samples(view_test::TestContext& ctx) {
    const RoundTripResult minimal = round_trip(ctx, "io_sample_minimal.xml");
    const RoundTripResult tasks = round_trip(ctx, "io_sample_tasks.xml");
    static_cast<void>(round_trip(ctx, "io_sample_full.xml"));

    /* The minimal project has tasks but no resources/assignments, so exactly
     * those two empty containers may be self-closing. */
    ctx.equal(count_self_closing(minimal.xml), std::size_t{2},
              "minimal sample: only the empty containers are self-closing");

    /* Element order the samples establish and the writer keeps: UID first,
     * ID second, PredecessorLink last inside its successor task. */
    const core::io::XmlParseResult parsed = core::io::parse_xml(tasks.xml);
    if (parsed.ok) {
        const core::io::XmlElement* container = core::io::child(parsed.root, "Tasks");
        ctx.check(container != nullptr, "tasks sample: <Tasks> element");
        if (container != nullptr) {
            const auto task_list = core::io::children(*container, "Task");
            ctx.equal(task_list.size(), std::size_t{3}, "tasks sample: three <Task>");
            if (task_list.size() == 3) {
                const core::io::XmlElement& last = *task_list[2];
                ctx.check(!last.children.empty() &&
                              last.children.front().name == "UID",
                          "tasks sample: <UID> is the first child");
                ctx.check(last.children.size() > 1 && last.children[1].name == "ID",
                          "tasks sample: <ID> is the second child");
                ctx.check(!last.children.empty() &&
                              last.children.back().name == "PredecessorLink",
                          "tasks sample: <PredecessorLink> is last in its successor");
            }
        }
    }
}

void test_root_and_empty_document(view_test::TestContext& ctx) {
    core::io::MspdiDocument empty;
    const std::string xml = core::io::mspdi::write(empty);
    ctx.check(xml.rfind(
                  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n", 0) == 0,
              "empty document: XML declaration");
    ctx.contains(xml, "<Project xmlns=\"http://schemas.microsoft.com/project\">",
                 "empty document: root and default namespace");
    ctx.contains(xml, "<Tasks/>", "empty document: <Tasks/>");
    ctx.contains(xml, "<Resources/>", "empty document: <Resources/>");
    ctx.contains(xml, "<Assignments/>", "empty document: <Assignments/>");
    ctx.equal(count_self_closing(xml), std::size_t{3},
              "empty document: exactly the three containers are self-closing");

    const core::io::MspdiDocument reparsed = core::io::import_mspdi(xml);
    ctx.equal(reparsed.tasks.row_count(), std::size_t{0}, "empty document: no tasks");
    ctx.equal(reparsed.resources.row_count(), std::size_t{0}, "empty document: no resources");
    ctx.equal(reparsed.assignments.row_count(), std::size_t{0},
              "empty document: no assignments");
    ctx.equal(reparsed.warnings.size(), std::size_t{0}, "empty document: no warnings");
    ctx.check(!reparsed.project.has_schedule_from_start,
              "empty document: ScheduleFromStart stays absent");
}

void test_escaping(view_test::TestContext& ctx) {
    const FieldId name = field_id(ctx, RecordType::Task, "Name");
    const std::string raw = "Tom & Jerry <\"quoted\"> it's";
    core::io::MspdiDocument document;
    document.tasks.add_row(1);
    document.tasks.set_field(0, name, Value::of_text(raw));

    const std::string xml = core::io::mspdi::write(document);
    ctx.contains(xml, "Tom &amp; Jerry &lt;&quot;quoted&quot;&gt; it&apos;s",
                 "all five XML entities are escaped");

    const core::io::XmlParseResult parsed = core::io::parse_xml(xml);
    ctx.check(parsed.ok, "escaped output is well-formed XML: " + parsed.error);
    if (parsed.ok) {
        const core::io::XmlElement* container = core::io::child(parsed.root, "Tasks");
        const core::io::XmlElement* task =
            container != nullptr ? core::io::child(*container, "Task") : nullptr;
        const core::io::XmlElement* name_element =
            task != nullptr ? core::io::child(*task, "Name") : nullptr;
        ctx.check(name_element != nullptr, "escaped task has a <Name>");
        if (name_element != nullptr) {
            ctx.equal_string(core::io::text_of(*name_element), raw,
                             "escaped text decodes back to the original");
        }
    }

    const core::io::MspdiDocument reparsed = core::io::import_mspdi(xml);
    const Value* value = reparsed.tasks.get(0, name);
    ctx.check(value != nullptr && same_value(*value, Value::of_text(raw)),
              "escaped text round-trips through the importer");
}

void test_utf8(view_test::TestContext& ctx) {
    const FieldId name = field_id(ctx, RecordType::Task, "Name");
    const FieldId notes = field_id(ctx, RecordType::Task, "Notes");
    const std::string raw = "Café — 日本語 🚀";
    const std::string note_text = "naïve\t·\nsecond line";
    core::io::MspdiDocument document;
    document.tasks.add_row(1);
    document.tasks.set_field(0, name, Value::of_text(raw));
    document.tasks.set_field(0, notes, Value::of_text(note_text));

    const std::string xml = core::io::mspdi::write(document);
    ctx.check(xml.find(raw) != std::string::npos,
              "UTF-8 text is written as UTF-8 bytes, not escaped");
    const core::io::MspdiDocument reparsed = core::io::import_mspdi(xml);
    const Value* value = reparsed.tasks.get(0, name);
    ctx.check(value != nullptr && value->kind == ValueKind::Text && value->text == raw,
              "UTF-8 text round-trips byte for byte");
    const Value* note = reparsed.tasks.get(0, notes);
    ctx.check(note != nullptr && note->kind == ValueKind::Text && note->text == note_text,
              "UTF-8 with tab and newline round-trips");
}

void test_empty_values(view_test::TestContext& ctx) {
    const FieldId name = field_id(ctx, RecordType::Task, "Name");
    const FieldId notes = field_id(ctx, RecordType::Task, "Notes");

    core::io::MspdiDocument document;
    document.tasks.add_row(1);
    document.tasks.set_field(0, name, Value::of_text(""));
    /* Notes is absent (Value::Null in the model). */
    const std::string xml = core::io::mspdi::write(document);
    ctx.check(xml.find("<Name") == std::string::npos,
              "empty Text emits no element");
    ctx.check(xml.find("<Notes") == std::string::npos,
              "Null emits no element");

    /* <x/> and <x></x> are both Null to the importer, so "no element" is the
     * consistent spelling and the writer's choice matches the model. */
    const core::io::MspdiDocument alternate = core::io::import_mspdi(
        "<Project><Tasks><Task><UID>1</UID><Name/><Notes></Notes></Task></Tasks></Project>");
    const Value* alternate_name = alternate.tasks.get(0, name);
    const Value* alternate_notes = alternate.tasks.get(0, notes);
    ctx.check(alternate_name != nullptr && alternate_name->is_null(),
              "<Name/> imports as Null");
    ctx.check(alternate_notes != nullptr && alternate_notes->is_null(),
              "<Notes></Notes> imports as Null");

    const std::string alternate_xml = core::io::mspdi::write(alternate);
    ctx.check(alternate_xml.find("<Name") == std::string::npos,
              "re-writing a Null Text value emits no element");
    ctx.check(alternate_xml.find("<Notes") == std::string::npos,
              "re-writing a Null value emits no element");
}

void test_durations(view_test::TestContext& ctx) {
    const FieldId duration = field_id(ctx, RecordType::Task, "Duration");
    const FieldId remaining = field_id(ctx, RecordType::Task, "RemainingDuration");
    const FieldId work = field_id(ctx, RecordType::Task, "Work");
    const FieldId slack = field_id(ctx, RecordType::Task, "TotalSlack");

    core::io::MspdiDocument document;
    document.project.minutes_per_day = 600; /* ten-hour days, same ticks */
    document.tasks.add_row(1);
    document.tasks.set_field(0, duration, Value::of_duration(4800));  /* 8 h */
    document.tasks.set_field(0, remaining, Value::of_duration(0));
    document.tasks.set_field(0, work, Value::of_duration(1));         /* 6 s */
    document.tasks.set_field(0, slack, Value::of_duration(-4800));

    const std::string xml = core::io::mspdi::write(document);
    ctx.contains(xml, "<Duration>PT8H0M0S</Duration>", "4800 ticks is PT8H0M0S");
    ctx.contains(xml, "<RemainingDuration>PT0H0M0S</RemainingDuration>",
                 "zero duration is a value, not an omission");
    ctx.contains(xml, "<Work>PT0H0M6S</Work>", "one 6-second tick is PT0H0M6S");
    ctx.contains(xml, "<TotalSlack>-PT8H0M0S</TotalSlack>", "negative duration is -PT...");

    const core::io::MspdiDocument reparsed = core::io::import_mspdi(xml);
    ctx.equal(reparsed.tasks.get(0, duration)->integer, std::int64_t{4800},
              "duration round-trips in ticks");
    ctx.equal(reparsed.tasks.get(0, remaining)->integer, std::int64_t{0},
              "zero remaining duration round-trips");
    ctx.equal(reparsed.tasks.get(0, work)->integer, std::int64_t{1},
              "one tick round-trips");
    ctx.equal(reparsed.tasks.get(0, slack)->integer, std::int64_t{-4800},
              "negative slack round-trips");

    /* The 480-minute/8-hour convention the port uses: one day is 4800 ticks,
     * which is 480 minutes at 10 ticks per minute. */
    ctx.equal(core::view::kTicksPerDay, std::int64_t{480 * 10},
              "a 480-minute day is 4800 six-second ticks");
    ctx.equal(core::view::kTicksPerHour, std::int64_t{600}, "an hour is 600 ticks");
}

void test_dates(view_test::TestContext& ctx) {
    const FieldId start = field_id(ctx, RecordType::Task, "Start");
    const FieldId finish = field_id(ctx, RecordType::Task, "Finish");
    const FieldId date1 = field_id(ctx, RecordType::Task, "Date1");

    core::io::MspdiDocument document;
    document.project.start = Value::of_date(DateValue{2024, 1, 1, 8, 0, 0});
    document.tasks.add_row(1);
    document.tasks.set_field(0, start, Value::of_date(DateValue{2024, 3, 4, 8, 0, 0}));
    document.tasks.set_field(0, finish, Value::of_date(DateValue{2024, 3, 4, 17, 30, 15}));
    document.tasks.set_field(0, date1, Value::of_date(DateValue{2024, 12, 31, 0, 0, 0}));

    const std::string xml = core::io::mspdi::write(document);
    ctx.contains(xml, "<StartDate>2024-01-01T08:00:00</StartDate>",
                 "project start is YYYY-MM-DDTHH:MM:SS");
    ctx.contains(xml, "<Start>2024-03-04T08:00:00</Start>", "task start form");
    ctx.contains(xml, "<Finish>2024-03-04T17:30:15</Finish>",
                 "seconds are always written");
    ctx.contains(xml, "<Date1>2024-12-31T00:00:00</Date1>", "midnight is T00:00:00");

    const core::io::MspdiDocument reparsed = core::io::import_mspdi(xml);
    const Value* finish_value = reparsed.tasks.get(0, finish);
    ctx.check(finish_value != nullptr && finish_value->kind == ValueKind::Date,
              "finish re-imports as a Date");
    if (finish_value != nullptr) {
        ctx.equal(static_cast<int>(finish_value->date.hour), 17, "finish hour");
        ctx.equal(static_cast<int>(finish_value->date.minute), 30, "finish minute");
        ctx.equal(static_cast<int>(finish_value->date.second), 15, "finish second");
    }
    const Value* date1_value = reparsed.tasks.get(0, date1);
    ctx.check(date1_value != nullptr &&
                  static_cast<int>(date1_value->date.month) == 12,
              "Date1 month");
    ctx.check(same_value(reparsed.project.start, document.project.start),
              "project start round-trips");

    /* An invalid DateValue has no MSPDI spelling; it is omitted, not written as
     * a garbage date. */
    core::io::MspdiDocument invalid;
    invalid.tasks.add_row(1);
    invalid.tasks.set_field(0, start, Value::of_date(DateValue{}));
    ctx.check(core::io::mspdi::write(invalid).find("<Start>") == std::string::npos,
              "an invalid date emits no element");
}

constexpr ValueKind kProbeKinds[] = {
    ValueKind::Text, ValueKind::Integer, ValueKind::Number,
    ValueKind::Bool, ValueKind::Duration, ValueKind::Date,
};

Value sample_of(ValueKind kind) {
    switch (kind) {
        case ValueKind::Bool: return Value::of_bool(true);
        case ValueKind::Integer: return Value::of_int(7);
        case ValueKind::Number: return Value::of_number(1.5);
        case ValueKind::Text: return Value::of_text("x1");
        case ValueKind::Duration: return Value::of_duration(4800);
        case ValueKind::Date: return Value::of_date(DateValue{2024, 3, 4, 8, 0, 0});
        case ValueKind::Null: break;
    }
    return Value{};
}

/* One probe per mapped field: set the field to a value of each candidate kind
 * and keep the kind that survives write -> parse with an exactly equal value.
 * A field with no surviving kind is a field the writer cannot carry. */
void check_field_coverage(view_test::TestContext& ctx, const std::string& label,
                          std::size_t expected_fields, const std::string& source,
                          RecordSet core::io::MspdiDocument::*member) {
    const core::io::MspdiDocument base = core::io::import_mspdi(source);
    const std::vector<FieldId> schema = (base.*member).schema();
    ctx.equal(schema.size(), expected_fields, label + ": mapped field count");
    for (const FieldId field : schema) {
        bool covered = false;
        for (const ValueKind kind : kProbeKinds) {
            const Value probe = sample_of(kind);
            core::io::MspdiDocument candidate = base;
            (candidate.*member).set_field(0, field, probe);
            const core::io::MspdiDocument reparsed =
                core::io::import_mspdi(core::io::mspdi::write(candidate));
            const Value* actual = (reparsed.*member).get(0, field);
            if (actual != nullptr && same_value(*actual, probe)) {
                covered = true;
                break;
            }
        }
        if (!covered) {
            ctx.check(false, label + ": field " + hex_field(field) + " does not round-trip");
        } else {
            ctx.check(true, label);
        }
    }
}

void test_every_mapped_field_round_trips(view_test::TestContext& ctx) {
    check_field_coverage(
        ctx, "task fields", 64,
        "<Project><Tasks><Task><UID>1</UID></Task></Tasks></Project>",
        &core::io::MspdiDocument::tasks);
    check_field_coverage(
        ctx, "resource fields", 17,
        "<Project><Resources><Resource><UID>1</UID></Resource></Resources></Project>",
        &core::io::MspdiDocument::resources);
    check_field_coverage(
        ctx, "assignment fields", 20,
        "<Project><Tasks><Task><UID>1</UID></Task></Tasks>"
        "<Resources><Resource><UID>1</UID></Resource></Resources>"
        "<Assignments><Assignment><UID>1</UID><TaskUID>1</TaskUID>"
        "<ResourceUID>1</ResourceUID></Assignment></Assignments></Project>",
        &core::io::MspdiDocument::assignments);
}

void test_file_helper(view_test::TestContext& ctx) {
    const core::io::MspdiDocument original =
        core::io::import_mspdi_file(sample("io_sample_minimal.xml"));
    const std::string stem =
        "io_mspdi_writer_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    /* The sample directory, not the system temp directory: this test may run
     * confined to the workspace.  The file is removed again below. */
    const std::filesystem::path directory(IO_SAMPLE_DIR);
    const std::filesystem::path path = directory / (stem + ".xml");

    bool wrote = true;
    try {
        core::io::mspdi::write_file(original, path.string());
    } catch (const std::exception& error) {
        wrote = false;
        ctx.check(false, std::string("write_file failed: ") + error.what());
    }
    if (wrote) {
        ctx.check(std::filesystem::exists(path), "write_file creates the file");
        if (std::filesystem::exists(path)) {
            const core::io::MspdiDocument reparsed =
                core::io::import_mspdi_file(path.string());
            check_document(ctx, reparsed, original, "file helper");
        }
    }
    std::error_code removed;
    std::filesystem::remove(path, removed);
    ctx.check(!std::filesystem::exists(path), "temporary file removed");

    bool threw = false;
    try {
        const std::filesystem::path missing =
            directory / (stem + "-missing-dir") / "out.xml";
        core::io::mspdi::write_file(original, missing.string());
    } catch (const core::io::MspdiError& error) {
        threw = true;
        ctx.contains(error.what(), stem + "-missing-dir", "write error names the path");
    }
    ctx.check(threw, "write_file throws MspdiError for an unwritable path");
}

void test_unrepresentable_records(view_test::TestContext& ctx) {
    const FieldId units = field_id(ctx, RecordType::Assignment, "Units");
    core::io::MspdiDocument document;
    document.tasks.add_row(1);
    document.resources.add_row(1);
    document.assignments.add_row(1);
    document.assignments.set_field(0, units, Value::of_number(100.0));

    std::string xml = core::io::mspdi::write(document);
    ctx.check(xml.find("<Assignment>") == std::string::npos,
              "an assignment with no ref is not written");
    ctx.contains(xml, "<Assignments/>", "the empty container is still present");

    // Value-initialised: appleclang/gcc treat a missing member initializer as an
    // error under -Wmissing-field-initializers (MSVC does not).
    core::io::MspdiAssignmentRef assignment_ref{};
    assignment_ref.uid = 1;
    assignment_ref.task_uid = 1;
    assignment_ref.resource_uid = 1;
    document.assignment_refs.push_back(assignment_ref);
    xml = core::io::mspdi::write(document);
    ctx.contains(xml, "<Assignment>", "an assignment with a ref is written");
    ctx.contains(xml, "<TaskUID>1</TaskUID>", "TaskUID from the ref");
    ctx.contains(xml, "<ResourceUID>1</ResourceUID>", "ResourceUID from the ref");
    /* The model holds percent; Project's export writes a fraction.  Pinned
     * exactly, like LinkLag: 100 % -> <Units>1</Units>. */
    ctx.contains(xml, "<Units>1</Units>", "Units written as the MSPDI fraction");
    ctx.check(xml.find("<Units>100</Units>") == std::string::npos,
              "Units is not written as percent");

    /* A link whose successor is not a task row has nowhere to go. */
    document.links.push_back(core::io::PredecessorLink{2, 99, 1, 4800, 7});
    xml = core::io::mspdi::write(document);
    ctx.check(xml.find("<PredecessorLink>") == std::string::npos,
              "a dangling link is not written");

    document.links.push_back(core::io::PredecessorLink{1, 1, 1, 4800, 7});
    xml = core::io::mspdi::write(document);
    ctx.contains(xml, "<PredecessorLink>", "an attached link is written");
    ctx.contains(xml, "<PredecessorUID>1</PredecessorUID>", "predecessor UID");
    /* The writer's exact LinkLag string, pinned: decimal tenths of a minute,
     * the form Project's own export uses (core/io/mspdi.h, "LinkLag spelling").
     * ISO remains an accepted import spelling but is no longer emitted. */
    ctx.contains(xml, "<LinkLag>4800</LinkLag>", "lag as decimal tenths of a minute");
    ctx.check(xml.find("<LinkLag>PT8H0M0S</LinkLag>") == std::string::npos,
              "the writer does not emit the ISO LinkLag form");
    ctx.equal_string(core::io::format_link_lag_iso(4800), "PT8H0M0S",
                     "the ISO spelling stays available and exact");
    ctx.contains(xml, "<CrossProject>0</CrossProject>", "CrossProject is emitted as 0");

    const core::io::MspdiDocument reparsed = core::io::import_mspdi(xml);
    ctx.equal(reparsed.links.size(), std::size_t{1}, "one link survives re-import");
    ctx.equal(reparsed.assignments.row_count(), std::size_t{1},
              "one assignment survives re-import");
}

}  // namespace

int main() {
    view_test::TestContext ctx;
    test_round_trip_samples(ctx);
    test_root_and_empty_document(ctx);
    test_escaping(ctx);
    test_utf8(ctx);
    test_empty_values(ctx);
    test_durations(ctx);
    test_dates(ctx);
    test_every_mapped_field_round_trips(ctx);
    test_file_helper(ctx);
    test_unrepresentable_records(ctx);
    return ctx.finish("io_mspdi_writer_tests");
}
