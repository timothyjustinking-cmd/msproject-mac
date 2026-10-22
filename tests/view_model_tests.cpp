// tests/view_model_tests.cpp -- the view model: values, record sets, table,
// filter, group, sort definitions, their JSON round-trip, and the default-table
// data checked against the analysis TSV it claims to come from.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "defaults.h"
#include "field_ref.h"
#include "filter.h"
#include "group.h"
#include "json.h"
#include "record_set.h"
#include "sort.h"
#include "table.h"
#include "table_registry.h"
#include "value.h"
#include "view_set.h"
#include "view_test_support.h"

namespace {

using core::view::Column;
using core::view::ColumnAlign;
using core::view::DateValue;
using core::view::FieldId;
using core::view::FilterCriterion;
using core::view::FilterDefinition;
using core::view::FilterOp;
using core::view::GroupDefinition;
using core::view::GroupInterval;
using core::view::JsonValue;
using core::view::RecordSet;
using core::view::SortDefinition;
using core::view::TableDefinition;
using core::view::Value;
using core::view::ValueKind;

constexpr FieldId kName = 0x0b40000e;
constexpr FieldId kId = 0x0b400017;
constexpr FieldId kNotes = 0x0b40000f;
/* Left here for tests that will select by outline level / finish; none
 * currently do, and clang's -Wunused-const-variable (which MSVC /W4 does not
 * implement) treats unused internal-linkage constants as errors under -Werror. */
[[maybe_unused]] constexpr FieldId kOutlineLevel = 0x0b400055;
constexpr FieldId kSummary = 0x0b40005c;
constexpr FieldId kDuration = 0x0b40001d;
constexpr FieldId kStart = 0x0b400023;
[[maybe_unused]] constexpr FieldId kFinish = 0x0b400024;
constexpr FieldId kPercentComplete = 0x0b400020;
constexpr FieldId kCost = 0x0b400005;
constexpr FieldId kResourceNames = 0x0b40018a;

/* Convenience lookup for name-based tests; kept for the same reason and
 * flagged by -Wunused-function until one uses it. */
[[maybe_unused]] FieldId by_name(view_test::TestContext& ctx, const char* name) {
    FieldId field = 0;
    const bool found = core::view::field_id_by_name(core::model::RecordType::Task, name, field);
    ctx.check(found, std::string("descriptor table has Task.") + name);
    return field;
}

DateValue date_of(int year, int month, int day, int hour, int minute) {
    DateValue date;
    date.year = year;
    date.month = static_cast<std::uint8_t>(month);
    date.day = static_cast<std::uint8_t>(day);
    date.hour = static_cast<std::uint8_t>(hour);
    date.minute = static_cast<std::uint8_t>(minute);
    return date;
}

RecordSet sample_tasks() {
    RecordSet records(core::model::RecordType::Task);
    records.add_field(kId);
    records.add_field(kName);
    records.add_field(kDuration);
    records.add_field(kStart);
    records.add_field(kPercentComplete);
    records.add_field(kSummary);
    records.add_field(kNotes);
    records.add_field(kCost);
    records.add_field(kResourceNames);

    auto add = [&records](std::uint32_t uid) { return records.add_row(uid); };

    std::size_t row = add(1);
    records.set_field(row, kId, Value::of_int(1));
    records.set_field(row, kName, Value::of_text("Design"));
    records.set_field(row, kDuration, Value::of_duration(2 * core::view::kTicksPerDay));
    records.set_field(row, kStart, Value::of_date(date_of(2024, 1, 1, 8, 0)));
    records.set_field(row, kPercentComplete, Value::of_int(50));
    records.set_field(row, kSummary, Value::of_bool(false));
    records.set_field(row, kCost, Value::of_number(1200.0));

    row = add(2);
    records.set_field(row, kId, Value::of_int(2));
    records.set_field(row, kName, Value::of_text("Build, install"));
    records.set_field(row, kDuration, Value::of_duration(5 * core::view::kTicksPerDay));
    records.set_field(row, kStart, Value::of_date(date_of(2024, 1, 8, 8, 0)));
    records.set_field(row, kPercentComplete, Value::of_int(0));
    records.set_field(row, kSummary, Value::of_bool(false));
    records.set_field(row, kNotes, Value::of_text("line one\nline \"two\""));
    records.set_field(row, kCost, Value::of_number(5000.0));

    row = add(3);
    records.set_field(row, kId, Value::of_int(3));
    records.set_field(row, kName, Value::of_text("Rollup"));
    records.set_field(row, kDuration, Value::of_duration(10 * core::view::kTicksPerDay));
    records.set_field(row, kStart, Value::of_date(date_of(2024, 1, 1, 8, 0)));
    records.set_field(row, kPercentComplete, Value::of_int(25));
    records.set_field(row, kSummary, Value::of_bool(true));
    records.set_field(row, kCost, Value::of_number(9000.0));

    row = add(4);
    records.set_field(row, kId, Value::of_int(4));
    records.set_field(row, kName, Value::of_text("Milestone"));
    records.set_field(row, kDuration, Value::of_duration(0));
    records.set_field(row, kStart, Value::of_date(date_of(2024, 2, 1, 8, 0)));
    records.set_field(row, kPercentComplete, Value::of_int(100));
    records.set_field(row, kSummary, Value::of_bool(false));
    records.set_field(row, kCost, Value::of_number(0.0));

    row = add(5);
    records.set_field(row, kId, Value::of_int(5));
    // Every other field left Null on purpose.
    return records;
}

void test_values(view_test::TestContext& ctx) {
    std::int64_t ticks = -1;
    ctx.check(core::view::parse_iso_duration("PT16H0M0S", ticks), "parse PT16H0M0S");
    ctx.equal(ticks, 2 * core::view::kTicksPerDay, "PT16H0M0S is two 8h days");
    ctx.equal_string(core::view::format_duration(ticks, core::view::DurationStyle::Iso8601),
                     "PT16H0M0S", "format ISO duration");
    ctx.equal_string(core::view::format_duration(ticks, core::view::DurationStyle::Days), "2d",
                     "format duration as days");

    ctx.check(core::view::parse_iso_duration("P2D", ticks), "parse P2D");
    ctx.equal(ticks, std::int64_t{28800}, "P2D is two 24-hour days of 6s ticks");
    ctx.check(core::view::parse_iso_duration("-PT1H30M", ticks), "parse -PT1H30M");
    ctx.equal(ticks, -900, "negative duration ticks");
    ctx.equal_string(core::view::format_duration(ticks, core::view::DurationStyle::Iso8601),
                     "-PT1H30M0S", "format negative ISO duration");
    ctx.check(core::view::parse_iso_duration("PT0H0M0S", ticks), "parse zero duration");
    ctx.equal(ticks, 0, "zero duration ticks");
    ctx.equal_string(core::view::format_duration(0, core::view::DurationStyle::Iso8601),
                     "PT0H0M0S", "format zero duration");
    ctx.check(!core::view::parse_iso_duration("P1M", ticks), "months are rejected");
    ctx.check(!core::view::parse_iso_duration("16H", ticks), "missing P is rejected");
    ctx.check(!core::view::parse_iso_duration("PT", ticks), "empty duration is rejected");

    DateValue date;
    ctx.check(core::view::parse_iso_datetime("2024-01-01T08:00:00", date), "parse dateTime");
    ctx.equal(date.year, 2024, "year");
    ctx.equal(static_cast<int>(date.month), 1, "month");
    ctx.equal(static_cast<int>(date.day), 1, "day");
    ctx.equal(static_cast<int>(date.hour), 8, "hour");
    ctx.equal(date, date_of(2024, 1, 1, 8, 0), "parsed date equals constructed date");
    ctx.equal_string(core::view::format_date(date), "2024-01-01 08:00", "format dateTime");
    ctx.check(core::view::parse_iso_datetime("2024-02-29", date), "leap day");
    ctx.check(!core::view::parse_iso_datetime("2023-02-29", date), "non-leap 29 Feb rejected");
    ctx.check(core::view::parse_iso_datetime("2024-01-01T08:00:00Z", date), "UTC suffix");
    ctx.check(core::view::parse_iso_datetime("2024-01-01T08:00:00+11:00", date),
              "offset suffix is accepted");
    ctx.equal_string(core::view::format_date(date), "2024-01-01 08:00", "offset not converted");

    ctx.equal(core::view::compare_values(Value::of_text("8"), Value::of_int(8)), 0,
              "numeric text compares equal to integer");
    ctx.equal(core::view::compare_values(Value::of_int(7), Value::of_int(8)), -1,
              "integer ordering");
    ctx.equal(core::view::compare_values(Value::of_bool(false), Value::of_bool(true)), -1,
              "bool ordering");
    const Value coerced = core::view::coerce_value(Value::of_text("12.5"), ValueKind::Number);
    ctx.check(coerced.kind == ValueKind::Number, "coerce text to number kind");
    ctx.equal(coerced.number, 12.5, "coerce text to number value");
    ctx.equal_string(core::view::format_value(Value::of_text("abc")), "abc", "format text");
    ctx.equal_string(core::view::format_value(Value::of_bool(true)), "1", "format bool");
    ctx.check(Value{}.is_null(), "default value is null");
    ctx.equal_string(core::view::format_value(Value::of_duration(9600)), "PT16H0M0S",
                     "format_value duration defaults to ISO");
}

void test_json(view_test::TestContext& ctx) {
    const auto parsed = core::view::parse_json(
        "{\"a\":[1,true,null,\"x\\n\"],\"b\":{},\"c\":-2.5}");
    ctx.check(parsed.ok, "parse mixed JSON");
    ctx.equal_string(core::view::write_json(parsed.value, false),
                     "{\"a\":[1,true,null,\"x\\n\"],\"b\":{},\"c\":-2.5}", "compact JSON output");
    const JsonValue* a = parsed.value.find("a");
    ctx.check(a != nullptr && a->is_array() && a->items().size() == 4, "array shape");
    ctx.check(parsed.value.find("b")->is_object(), "empty object");
    const JsonValue* c = parsed.value.find("c");
    ctx.check(c != nullptr && c->is_number(), "number");
    ctx.equal(c->as_number(), -2.5, "number value");

    const auto unicode = core::view::parse_json("\"\\u00e9\\ud83d\\ude00\"");
    ctx.check(unicode.ok, "parse unicode escapes");
    ctx.equal_string(unicode.value.as_string(), "\xC3\xA9\xF0\x9F\x98\x80",
                     "unicode escapes become UTF-8");

    const auto escaped = core::view::parse_json("\"a\\\"b\\\\c\\td\"");
    ctx.check(escaped.ok, "parse escapes");
    ctx.equal_string(escaped.value.as_string(), "a\"b\\c\td", "escape values");

    const auto trailing = core::view::parse_json("{\"a\":1,}");
    ctx.check(!trailing.ok, "trailing comma rejected");
    ctx.contains(trailing.error, "trailing comma", "trailing comma names the problem");

    const auto duplicate = core::view::parse_json("{\"a\":1,\"a\":2}");
    ctx.check(!duplicate.ok, "duplicate key rejected");
    ctx.contains(duplicate.error, "duplicate", "duplicate key names the problem");

    const auto unterminated = core::view::parse_json("[1");
    ctx.check(!unterminated.ok, "unterminated array rejected");

    const auto missing_value = core::view::parse_json("{\"a\":}");
    ctx.check(!missing_value.ok, "missing value rejected");

    const auto trailing_text = core::view::parse_json("1 2");
    ctx.check(!trailing_text.ok, "trailing text rejected");

    const auto multiline = core::view::parse_json("{\n  \"a\": 1,\n}");
    ctx.check(!multiline.ok, "multiline trailing comma rejected");
    ctx.check(multiline.line >= 2, "error carries a line number");

    JsonValue object = JsonValue::object();
    object.set("k", JsonValue::string("v"));
    const std::string pretty = core::view::write_json(object, true);
    ctx.contains(pretty, "\n", "pretty output has newlines");
    ctx.contains(pretty, "\"k\": \"v\"", "pretty output spaces after colon");
}

void test_record_set(view_test::TestContext& ctx) {
    RecordSet records(core::model::RecordType::Task);
    ctx.equal(records.row_count(), std::size_t{0}, "empty record set");
    const std::size_t column = records.add_field(kName);
    ctx.equal(column, std::size_t{0}, "first column index");
    ctx.equal(records.add_field(kName), std::size_t{0}, "re-adding a field returns its column");
    ctx.check(records.add_field(0) == records.field_count(), "field id 0 rejected");

    const std::size_t row = records.add_row(42);
    ctx.equal(records.uid(row), 42u, "uid stored");
    ctx.equal(records.row_of_uid(42), 0, "row_of_uid");
    ctx.equal(records.row_of_uid(7), -1, "missing uid");

    records.set_field(row, kName, Value::of_text("A"));
    const Value* value = records.get(row, kName);
    ctx.check(value != nullptr && value->kind == ValueKind::Text, "value readable");
    ctx.equal_string(value->text, "A", "value text");

    const std::size_t added = records.add_field(kDuration);
    ctx.equal(added, std::size_t{1}, "field added after rows");
    ctx.equal(records.field_count(), std::size_t{2}, "field count after add");
    const Value* duration = records.get(row, kDuration);
    ctx.check(duration != nullptr && duration->is_null(), "new column is Null for old rows");

    records.add_row(43);
    ctx.check(records.get(1, kName)->is_null(), "new row is Null");
    ctx.check(records.at(99, 0) == nullptr, "out-of-range at() returns null");
    ctx.check(records.get(0, 0x0b4004ff) == nullptr, "unknown field returns null");

    records.set_field(0, static_cast<FieldId>(kDuration), Value::of_duration(4800));
    ctx.equal(records.get(0, kDuration)->integer, std::int64_t{4800}, "set_field value");
    records.clear();
    ctx.equal(records.row_count(), std::size_t{0}, "clear rows");
}

void test_table(view_test::TestContext& ctx) {
    TableDefinition table;
    table.id = "plan";
    table.name = "Plan";
    table.record_type = core::model::RecordType::Task;
    table.source = "test";
    table.columns.push_back({kName, {}, 30, ColumnAlign::Left});
    table.columns.push_back({kDuration, "Length", 8, ColumnAlign::Right});
    table.columns.push_back({kNotes, {}, 0, ColumnAlign::Auto});

    const JsonValue json = core::view::table_to_json(table);
    TableDefinition parsed;
    std::string error;
    ctx.check(core::view::table_from_json(json, parsed, "table", error),
              "table JSON round-trip parses: " + error);
    ctx.equal(parsed.id, table.id, "table id round-trip");
    ctx.equal(parsed.name, table.name, "table name round-trip");
    ctx.equal(parsed.columns.size(), std::size_t{3}, "column count round-trip");
    ctx.equal(parsed.columns[0].field, kName, "column field round-trip");
    ctx.equal(parsed.columns[0].width, 30, "width round-trip");
    ctx.check(parsed.columns[0].align == ColumnAlign::Left, "align round-trip");
    ctx.equal_string(parsed.columns[1].title, "Length", "explicit title round-trip");
    ctx.equal_string(parsed.columns[0].display_title(core::model::RecordType::Task), "Name",
                     "derived title");
    ctx.equal_string(core::view::field_title(0x0b4000c7, core::model::RecordType::Task),
                     "0x0b4000c7", "unnamed field title is the hex id");

    const auto unnamed = core::view::parse_json(
        "{\"id\":\"t\",\"name\":\"T\",\"columns\":[{\"field_id\":188743879}]}");
    ctx.check(unnamed.ok, "unnamed field JSON parses");
    TableDefinition unnamed_table;
    ctx.check(core::view::table_from_json(unnamed.value, unnamed_table, "t", error),
              "unnamed field reference resolves by id: " + error);
    ctx.equal(unnamed_table.columns[0].field, 0x0b4000c7u, "unnamed field id");

    const auto unknown = core::view::parse_json(
        "{\"id\":\"t\",\"name\":\"T\",\"columns\":[{\"field\":\"NoSuchField\"}]}");
    TableDefinition ignored;
    ctx.check(!core::view::table_from_json(unknown.value, ignored, "t", error),
              "unknown field name rejected");
    ctx.contains(error, "unknown Task field name", "unknown field error text");

    const auto duplicate = core::view::parse_json(
        "{\"id\":\"t\",\"name\":\"T\",\"columns\":[{\"field\":\"Name\"},{\"field\":\"Name\"}]}");
    ctx.check(!core::view::table_from_json(duplicate.value, ignored, "t", error),
              "duplicate column rejected");
    ctx.contains(error, "twice", "duplicate column error text");
}

void test_filter(view_test::TestContext& ctx) {
    const RecordSet records = sample_tasks();

    FilterDefinition filter;
    filter.id = "short";
    filter.name = "Short tasks";
    filter.criteria = FilterCriterion::all({
        FilterCriterion::comparison(kSummary, FilterOp::Equals, Value::of_bool(false)),
        FilterCriterion::comparison(kDuration, FilterOp::GreaterOrEqual,
                                     Value::of_text("PT8H0M0S")),
    });
    const std::vector<std::size_t> rows = core::view::matching_rows(filter, records);
    ctx.equal(rows.size(), std::size_t{2}, "two short non-summary tasks");
    if (rows.size() == 2) {
        ctx.equal(rows[0], std::size_t{0}, "first match row");
        ctx.equal(rows[1], std::size_t{1}, "second match row");
    }

    FilterDefinition contains;
    contains.criteria =
        FilterCriterion::comparison(kName, FilterOp::Contains, Value::of_text("Build"));
    const auto hits = core::view::matching_rows(contains, records);
    ctx.equal(hits.size(), std::size_t{1}, "contains names one row");
    if (!hits.empty()) {
        ctx.equal(hits[0], std::size_t{1}, "contains match row");
    }

    FilterDefinition null_notes;
    null_notes.criteria =
        FilterCriterion::comparison(kNotes, FilterOp::Equals, Value{});
    const auto nulls = core::view::matching_rows(null_notes, records);
    ctx.equal(nulls.size(), std::size_t{4}, "null notes match null constant");

    FilterDefinition date_filter;
    date_filter.criteria = FilterCriterion::comparison(
        kStart, FilterOp::GreaterOrEqual, Value::of_text("2024-02-01"));
    const auto date_hits = core::view::matching_rows(date_filter, records);
    ctx.equal(date_hits.size(), std::size_t{1}, "date constant coerced from text");
    if (!date_hits.empty()) {
        ctx.equal(date_hits[0], std::size_t{3}, "date filter row");
    }

    FilterDefinition duration_filter;
    duration_filter.criteria =
        FilterCriterion::comparison(kDuration, FilterOp::Equals,
                                     Value::of_text("PT16H0M0S"));
    const auto duration_hits = core::view::matching_rows(duration_filter, records);
    ctx.equal(duration_hits.size(), std::size_t{1}, "duration constant coerced from text");
    if (!duration_hits.empty()) {
        ctx.equal(duration_hits[0], std::size_t{0}, "duration filter row");
    }

    FilterDefinition not_summary;
    not_summary.criteria = FilterCriterion::negate(
        FilterCriterion::comparison(kSummary, FilterOp::IsTrue, Value{}));
    const auto not_summary_hits = core::view::matching_rows(not_summary, records);
    ctx.equal(not_summary_hits.size(), std::size_t{4}, "negated is_true: null is not true");

    FilterDefinition any_name;
    any_name.criteria = FilterCriterion::any({
        FilterCriterion::comparison(kName, FilterOp::StartsWith, Value::of_text("Roll")),
        FilterCriterion::comparison(kName, FilterOp::EndsWith, Value::of_text("one")),
    });
    const auto any_hits = core::view::matching_rows(any_name, records);
    ctx.equal(any_hits.size(), std::size_t{2}, "any() combines two text predicates");

    const JsonValue json = core::view::filter_to_json(filter);
    FilterDefinition parsed;
    std::string error;
    ctx.check(core::view::filter_from_json(json, parsed, "filter", error),
              "filter JSON round-trip: " + error);
    ctx.equal(parsed.criteria.children.size(), std::size_t{2}, "filter children round-trip");
    ctx.equal(parsed.criteria.children[0].field, kSummary, "filter field round-trip");
    const auto reparsed_hits = core::view::matching_rows(parsed, records);
    ctx.equal(reparsed_hits.size(), rows.size(), "reparsed filter matches the same rows");

    const auto bad_op = core::view::parse_json(
        "{\"id\":\"f\",\"name\":\"F\",\"criteria\":{\"field\":\"Name\",\"op\":\"nope\","
        "\"value\":1}}");
    FilterDefinition ignored;
    ctx.check(!core::view::filter_from_json(bad_op.value, ignored, "f", error),
              "unknown filter op rejected");
    ctx.contains(error, "unknown filter op", "unknown filter op error text");
}

void test_filter_lists_and_auto_filter(view_test::TestContext& ctx) {
    const RecordSet records = sample_tasks();

    // Value list: "name is Design or Rollup".
    FilterDefinition in_names;
    in_names.id = "names";
    in_names.name = "Names";
    in_names.criteria =
        FilterCriterion::in_list(kName, {Value::of_text("Design"), Value::of_text("Rollup")});
    const auto in_rows = core::view::matching_rows(in_names, records);
    ctx.equal(in_rows.size(), std::size_t{2}, "in() matches two names");
    if (in_rows.size() == 2) {
        ctx.equal(in_rows[0], std::size_t{0}, "in() first row");
        ctx.equal(in_rows[1], std::size_t{2}, "in() second row");
    }

    FilterDefinition not_in_names;
    not_in_names.criteria = FilterCriterion::not_in_list(
        kName, {Value::of_text("Design"), Value::of_text("Rollup")});
    const auto not_in_rows = core::view::matching_rows(not_in_names, records);
    ctx.equal(not_in_rows.size(), std::size_t{3}, "not_in() matches the other three rows");

    // Value list with a coerced constant (text "2" against an integer field).
    FilterDefinition in_ids;
    in_ids.criteria = FilterCriterion::in_list(kId, {Value::of_text("2"), Value::of_int(4)});
    const auto in_id_rows = core::view::matching_rows(in_ids, records);
    ctx.equal(in_id_rows.size(), std::size_t{2}, "in() coerces text to the field kind");
    if (in_id_rows.size() == 2) {
        ctx.equal(in_id_rows[0], std::size_t{1}, "in() coerced first row");
        ctx.equal(in_id_rows[1], std::size_t{3}, "in() coerced second row");
    }

    // IsNull / IsNotNull.
    FilterDefinition null_notes;
    null_notes.id = "null-notes";
    null_notes.name = "No notes";
    null_notes.criteria = FilterCriterion::is_null(kNotes);
    ctx.equal(core::view::matching_rows(null_notes, records).size(), std::size_t{4},
              "is_null() finds the four notes-less rows");
    FilterDefinition has_notes;
    has_notes.criteria = FilterCriterion::is_not_null(kNotes);
    const auto has_notes_rows = core::view::matching_rows(has_notes, records);
    ctx.equal(has_notes_rows.size(), std::size_t{1}, "is_not_null() finds one row");
    if (!has_notes_rows.empty()) {
        ctx.equal(has_notes_rows[0], std::size_t{1}, "is_not_null() row");
    }

    // Auto-filter values: distinct, sorted, nulls omitted.
    const std::vector<Value> names = core::view::distinct_values(records, kName);
    ctx.equal(names.size(), std::size_t{4}, "four distinct names");
    if (names.size() == 4) {
        ctx.equal_string(names[0].text, "Build, install", "distinct names sorted: first");
        ctx.equal_string(names[1].text, "Design", "distinct names sorted: second");
        ctx.equal_string(names[2].text, "Milestone", "distinct names sorted: third");
        ctx.equal_string(names[3].text, "Rollup", "distinct names sorted: fourth");
    }
    const std::vector<Value> ids = core::view::distinct_values(records, kId);
    ctx.equal(ids.size(), std::size_t{5}, "five distinct ids");
    if (ids.size() == 5) {
        ctx.equal(ids[0].integer, std::int64_t{1}, "distinct ids sorted ascending");
        ctx.equal(ids[3].integer, std::int64_t{4}, "distinct ids sorted descending end");
        ctx.equal(ids[4].integer, std::int64_t{5}, "distinct ids includes the null-notes row");
    }
    ctx.equal(core::view::distinct_values(records, 0x0b4004ff).size(), std::size_t{0},
              "distinct_values of an absent field is empty");

    FilterDefinition auto_names =
        core::view::auto_filter("auto-name", "AutoFilter Name",
                                core::model::RecordType::Task, kName,
                                {Value::of_text("Design")});
    const auto auto_rows = core::view::matching_rows(auto_names, records);
    ctx.equal(auto_rows.size(), std::size_t{1}, "auto_filter matches one value");
    if (!auto_rows.empty()) {
        ctx.equal(auto_rows[0], std::size_t{0}, "auto_filter row");
    }
    FilterDefinition empty_filter =
        core::view::auto_filter("auto-empty", "AutoFilter empty",
                                core::model::RecordType::Task, kName, {});
    ctx.equal(core::view::matching_rows(empty_filter, records).size(), std::size_t{0},
              "an empty value list matches nothing");

    // JSON: in/not_in carry "values"; is_null needs no value.
    const JsonValue list_json = core::view::filter_to_json(in_names);
    FilterDefinition parsed;
    std::string error;
    ctx.check(core::view::filter_from_json(list_json, parsed, "filter", error),
              "in() JSON round-trip: " + error);
    ctx.equal(parsed.criteria.values.size(), std::size_t{2}, "in() values round-trip");
    ctx.check(parsed.criteria.op == FilterOp::In, "in() op round-trip");
    ctx.equal(core::view::matching_rows(parsed, records).size(), std::size_t{2},
              "reparsed in() matches the same rows");

    const JsonValue null_json = core::view::filter_to_json(null_notes);
    ctx.check(core::view::filter_from_json(null_json, parsed, "filter", error),
              "is_null() JSON round-trip: " + error);
    ctx.check(parsed.criteria.op == FilterOp::IsNull, "is_null() op round-trip");

    const auto array_value = core::view::parse_json(
        "{\"id\":\"f\",\"name\":\"F\",\"criteria\":{\"field\":\"Name\",\"op\":\"in\","
        "\"values\":[\"Design\",\"Rollup\"]}}");
    ctx.check(array_value.ok, "in() JSON parses");
    ctx.check(core::view::filter_from_json(array_value.value, parsed, "f", error),
              "in() values array accepted: " + error);
    ctx.equal(core::view::matching_rows(parsed, records).size(), std::size_t{2},
              "parsed values array matches two rows");

    const auto missing_values = core::view::parse_json(
        "{\"id\":\"f\",\"name\":\"F\",\"criteria\":{\"field\":\"Name\",\"op\":\"in\"}}");
    FilterDefinition ignored;
    ctx.check(!core::view::filter_from_json(missing_values.value, ignored, "f", error),
              "in() without values is rejected");
    ctx.contains(error, "values", "missing values error text");
}

void test_group(view_test::TestContext& ctx) {
    const RecordSet records = sample_tasks();

    GroupDefinition by_month;
    by_month.id = "month";
    by_month.name = "By month";
    by_month.keys.push_back({kStart, GroupInterval::Month, true});
    const auto buckets = core::view::build_groups(by_month, records);
    ctx.equal(buckets.size(), std::size_t{3}, "month grouping has three buckets");
    if (buckets.size() == 3) {
        ctx.equal_string(buckets[0].label, "2024-01", "January bucket");
        ctx.equal(buckets[0].rows.size(), std::size_t{3}, "January rows");
        ctx.equal_string(buckets[1].label, "2024-02", "February bucket");
        ctx.equal(buckets[1].rows.size(), std::size_t{1}, "February rows");
        ctx.equal_string(buckets[2].label, "(none)", "null bucket label");
        ctx.equal(buckets[2].rows.size(), std::size_t{1}, "null bucket rows");
        ctx.check(buckets[2].key.is_null(), "null bucket key");
    }

    GroupDefinition descending = by_month;
    descending.id = "month-desc";
    descending.keys[0].ascending = false;
    const auto descending_buckets = core::view::build_groups(descending, records);
    ctx.equal(descending_buckets.size(), std::size_t{3}, "descending bucket count");
    if (descending_buckets.size() == 3) {
        ctx.equal_string(descending_buckets[0].label, "2024-02", "descending first bucket");
        ctx.equal_string(descending_buckets[2].label, "(none)", "null bucket still last");
    }

    GroupDefinition nested;
    nested.id = "nested";
    nested.name = "Nested";
    nested.keys.push_back({kSummary, GroupInterval::None, true});
    nested.keys.push_back({kStart, GroupInterval::Month, true});
    const auto nested_buckets = core::view::build_groups(nested, records);
    ctx.equal(nested_buckets.size(), std::size_t{3}, "nested top buckets: false/true/null");
    if (nested_buckets.size() == 3) {
        ctx.equal_string(nested_buckets[1].label, "1", "summary bucket label");
        ctx.equal(nested_buckets[1].children.size(), std::size_t{1},
                  "summary bucket has one month child");
        ctx.equal(nested_buckets[0].children.size(), std::size_t{2},
                  "non-summary bucket has January and February children");
    }

    GroupDefinition percent;
    percent.id = "pct";
    percent.keys.push_back({kPercentComplete, GroupInterval::Percent, true});
    const auto percent_buckets = core::view::build_groups(percent, records);
    ctx.equal(percent_buckets.size(), std::size_t{5}, "percent grouping buckets");
    if (percent_buckets.size() == 5) {
        ctx.equal_string(percent_buckets[0].label, "0-9", "percent 0 bucket");
        ctx.equal_string(percent_buckets[1].label, "20-29", "percent 20 bucket");
        ctx.equal_string(percent_buckets[2].label, "50-59", "percent 50 bucket");
        ctx.equal_string(percent_buckets[3].label, "100", "percent 100 bucket");
        ctx.equal_string(percent_buckets[4].label, "(none)", "percent null bucket");
    }

    GroupDefinition by_week;
    by_week.id = "week";
    by_week.keys.push_back({kStart, GroupInterval::Week, true});
    const auto week_buckets = core::view::build_groups(by_week, records);
    ctx.equal(week_buckets.size(), std::size_t{4}, "week buckets");
    if (week_buckets.size() == 4) {
        ctx.equal_string(week_buckets[0].label, "week of 2024-01-01", "first week label");
        ctx.equal_string(week_buckets[1].label, "week of 2024-01-08", "second week label");
        ctx.equal_string(week_buckets[2].label, "week of 2024-01-29", "February week label");
        ctx.equal_string(week_buckets[3].label, "(none)", "week null bucket last");
    }

    const JsonValue json = core::view::group_to_json(nested);
    GroupDefinition parsed;
    std::string error;
    ctx.check(core::view::group_from_json(json, parsed, "group", error),
              "group JSON round-trip: " + error);
    ctx.equal(parsed.keys.size(), std::size_t{2}, "group keys round-trip");
    ctx.check(parsed.keys[1].interval == GroupInterval::Month, "group interval round-trip");
    ctx.check(parsed.show_summary, "show_summary round-trip");
}

void test_group_order_and_summary(view_test::TestContext& ctx) {
    // Five synthetic tasks with a group field, deliberately not in name order.
    RecordSet records(core::model::RecordType::Task);
    records.add_field(kId);
    records.add_field(kName);
    records.add_field(kResourceNames);
    records.add_field(kSummary);
    const char* names[] = {"Zulu", "Alpha", "Bravo", "Yankee", "Charlie"};
    const char* crews[] = {"Crew", "Crew", "Plant", "Plant", "Crew"};
    const bool summary[] = {false, false, false, true, false};
    for (std::size_t i = 0; i < 5; ++i) {
        const std::size_t row = records.add_row(static_cast<std::uint32_t>(i + 1));
        records.set_field(row, kId, Value::of_int(static_cast<std::int64_t>(i + 1)));
        records.set_field(row, kName, Value::of_text(names[i]));
        records.set_field(row, kResourceNames, Value::of_text(crews[i]));
        records.set_field(row, kSummary, Value::of_bool(summary[i]));
    }

    GroupDefinition by_crew;
    by_crew.id = "crew";
    by_crew.name = "By crew";
    by_crew.keys.push_back({kResourceNames, GroupInterval::None, true});

    SortDefinition by_name;
    by_name.id = "name";
    by_name.name = "By name";
    by_name.keys.push_back({kName, true});
    const std::vector<std::size_t> name_order = core::view::apply_sort(by_name, records);

    const std::vector<core::view::GroupBucket> buckets =
        core::view::build_groups(by_crew, records, name_order);
    ctx.equal(buckets.size(), std::size_t{2}, "two crew buckets");
    if (buckets.size() == 2) {
        ctx.equal_string(buckets[0].label, "Crew", "Crew bucket first");
        ctx.equal(buckets[0].row_count, std::size_t{3}, "Crew direct row count");
        ctx.equal(buckets[0].total_row_count, std::size_t{3}, "Crew total row count");
        ctx.equal_string(buckets[0].summary_line, "3 tasks", "Crew summary line");
        // Rows inside a bucket keep the sorted order given to build_groups.
        const std::size_t expected[] = {1, 4, 0};  // Alpha, Charlie, Zulu
        bool order_ok = buckets[0].rows.size() == 3;
        for (std::size_t i = 0; order_ok && i < 3; ++i) {
            order_ok = buckets[0].rows[i] == expected[i];
        }
        ctx.check(order_ok, "rows inside a bucket follow the supplied sort order");
        ctx.equal_string(buckets[1].label, "Plant", "Plant bucket second");
        ctx.equal(buckets[1].total_row_count, std::size_t{2}, "Plant total row count");
        ctx.equal_string(buckets[1].summary_line, "2 tasks", "Plant summary line");
    }

    // A nested group totals the children, and the display lines are heading,
    // summary, then rows (decision, unmeasured).
    GroupDefinition nested;
    nested.id = "crew-summary";
    nested.name = "Crew then summary";
    nested.keys.push_back({kResourceNames, GroupInterval::None, true});
    nested.keys.push_back({kSummary, GroupInterval::None, false});
    const std::vector<core::view::GroupBucket> nested_buckets =
        core::view::build_groups(nested, records, name_order);
    ctx.equal(nested_buckets.size(), std::size_t{2}, "nested group top buckets");
    if (nested_buckets.size() == 2) {
        ctx.equal(nested_buckets[0].children.size(), std::size_t{1},
                  "Crew has one child bucket (all its rows are non-summary)");
        ctx.equal(nested_buckets[1].children.size(), std::size_t{2},
                  "Plant has true and false children");
        ctx.equal(nested_buckets[0].total_row_count, std::size_t{3},
                  "nested total counts children");
        ctx.equal_string(nested_buckets[0].summary_line, "3 tasks", "nested summary line");
        ctx.check(nested_buckets[0].row_count == 0,
                  "a bucket with children has no direct rows of its own here");
    }

    const std::vector<core::view::GroupLine> lines =
        core::view::group_display_lines(nested_buckets);
    ctx.check(!lines.empty(), "group display lines exist");
    if (!lines.empty()) {
        ctx.check(lines[0].kind == core::view::GroupLineKind::heading, "first line is a heading");
        ctx.equal_string(lines[0].text, "Crew", "first heading text");
        ctx.equal(lines[0].depth, 0, "top heading depth");
        ctx.check(lines[1].kind == core::view::GroupLineKind::summary,
                  "second line is the summary");
        ctx.equal_string(lines[1].text, "3 tasks", "second line summary text");
        bool saw_row = false;
        for (const core::view::GroupLine& line : lines) {
            saw_row = saw_row || line.kind == core::view::GroupLineKind::row;
        }
        ctx.check(saw_row, "display lines contain task rows");
    }
    const std::vector<core::view::GroupLine> no_summary =
        core::view::group_display_lines(nested_buckets, false);
    bool has_summary_line = false;
    for (const core::view::GroupLine& line : no_summary) {
        has_summary_line = has_summary_line || line.kind == core::view::GroupLineKind::summary;
    }
    ctx.check(!has_summary_line, "include_summary=false omits summary lines");

    GroupDefinition no_summary_group = by_crew;
    no_summary_group.show_summary = false;
    const std::vector<core::view::GroupBucket> quiet =
        core::view::build_groups(no_summary_group, records, name_order);
    ctx.check(!quiet.empty() && quiet[0].summary_line.empty(),
              "show_summary=false leaves the summary line empty");
}

void test_outline_sort(view_test::TestContext& ctx) {
    RecordSet records(core::model::RecordType::Task);
    const FieldId level = 0x0b400055;  // Task.OutlineLevel
    records.add_field(kName);
    records.add_field(level);
    records.add_field(kSummary);
    const char* names[] = {"Zulu", "Bravo", "Alpha", "Yankee", "Charlie"};
    const std::int64_t levels[] = {1, 2, 2, 1, 2};
    for (std::size_t i = 0; i < 5; ++i) {
        const std::size_t row = records.add_row(static_cast<std::uint32_t>(i + 1));
        records.set_field(row, kName, Value::of_text(names[i]));
        records.set_field(row, level, Value::of_int(levels[i]));
        records.set_field(row, kSummary, Value::of_bool(levels[i] == 1));
    }

    SortDefinition by_name;
    by_name.id = "name";
    by_name.name = "By name";
    by_name.keys.push_back({kName, true});

    const std::vector<std::size_t> flat = core::view::apply_sort(by_name, records);
    const std::size_t flat_expected[] = {2, 1, 4, 3, 0};  // Alpha Bravo Charlie Yankee Zulu
    bool flat_ok = flat.size() == 5;
    for (std::size_t i = 0; flat_ok && i < 5; ++i) {
        flat_ok = flat[i] == flat_expected[i];
    }
    ctx.check(flat_ok, "flat sort ignores the outline");

    core::view::SortOptions options;
    options.keep_outline = true;
    const std::vector<std::size_t> nested = core::view::apply_sort(by_name, records, options);
    // Parents sort among themselves; children sort inside each parent.
    const std::size_t nested_expected[] = {3, 4, 0, 2, 1};  // Yankee Charlie Zulu Alpha Bravo
    bool nested_ok = nested.size() == 5;
    for (std::size_t i = 0; nested_ok && i < 5; ++i) {
        nested_ok = nested[i] == nested_expected[i];
    }
    ctx.check(nested_ok, "outline sort keeps children under their parent");
    ctx.check(nested != flat, "outline sort is not the flat order");

    // Grouping by a field flattens: the outline order is only the input order.
    GroupDefinition by_summary;
    by_summary.id = "summary";
    by_summary.name = "By summary";
    by_summary.keys.push_back({kSummary, GroupInterval::None, false});
    const std::vector<core::view::GroupBucket> buckets =
        core::view::build_groups(by_summary, records, nested);
    ctx.equal(buckets.size(), std::size_t{2}, "grouping over an outline flattens to two buckets");
    if (buckets.size() == 2) {
        ctx.equal(buckets[0].total_row_count, std::size_t{2}, "summary=true bucket count");
        ctx.equal(buckets[1].total_row_count, std::size_t{3}, "summary=false bucket count");
        ctx.check(buckets[0].children.empty() && buckets[1].children.empty(),
                  "no outline nesting survives inside a group bucket");
    }

    // Multi-key sort with equal keys keeps file order (the stable tie-break).
    RecordSet ties(core::model::RecordType::Task);
    ties.add_field(kName);
    ties.add_field(kDuration);
    const char* tie_names[] = {"B", "A", "C", "A", "B"};
    for (std::size_t i = 0; i < 5; ++i) {
        const std::size_t row = ties.add_row(static_cast<std::uint32_t>(i + 1));
        ties.set_field(row, kName, Value::of_text(tie_names[i]));
        ties.set_field(row, kDuration, Value::of_duration(4800));
    }
    SortDefinition two_keys;
    two_keys.id = "name-duration";
    two_keys.name = "Name then duration";
    two_keys.keys.push_back({kName, true});
    two_keys.keys.push_back({kDuration, true});
    const std::vector<std::size_t> tie_order = core::view::apply_sort(two_keys, ties);
    const std::size_t tie_expected[] = {1, 3, 0, 4, 2};  // A(1) A(3) B(0) B(4) C(2)
    bool tie_ok = tie_order.size() == 5;
    for (std::size_t i = 0; tie_ok && i < 5; ++i) {
        tie_ok = tie_order[i] == tie_expected[i];
    }
    ctx.check(tie_ok, "equal keys keep file order (stable tie-break)");
}

void test_sort(view_test::TestContext& ctx) {
    const RecordSet records = sample_tasks();

    SortDefinition by_start;
    by_start.id = "start";
    by_start.name = "By start";
    by_start.keys.push_back({kStart, true});
    const auto order = core::view::apply_sort(by_start, records);
    ctx.equal(order.size(), std::size_t{5}, "sort order size");
    const std::size_t expected_asc[] = {0, 2, 1, 3, 4};
    bool ascending_ok = true;
    for (std::size_t i = 0; i < order.size() && i < 5; ++i) {
        ascending_ok = ascending_ok && order[i] == expected_asc[i];
    }
    ctx.check(ascending_ok, "ascending start sort with null last");

    SortDefinition descending = by_start;
    descending.id = "start-desc";
    descending.keys[0].ascending = false;
    const auto descending_order = core::view::apply_sort(descending, records);
    const std::size_t expected_desc[] = {3, 1, 0, 2, 4};
    bool descending_ok = true;
    for (std::size_t i = 0; i < descending_order.size() && i < 5; ++i) {
        descending_ok = descending_ok && descending_order[i] == expected_desc[i];
    }
    ctx.check(descending_ok, "descending start sort keeps null last and is stable");

    SortDefinition by_summary_then_start;
    by_summary_then_start.id = "two";
    by_summary_then_start.keys.push_back({kSummary, true});
    by_summary_then_start.keys.push_back({kStart, true});
    const auto two_key = core::view::apply_sort(by_summary_then_start, records);
    ctx.equal(two_key.size(), std::size_t{5}, "two-key sort size");
    ctx.equal(two_key[0], std::size_t{0}, "two-key first row");

    const JsonValue json = core::view::sort_to_json(by_summary_then_start);
    SortDefinition parsed;
    std::string error;
    ctx.check(core::view::sort_from_json(json, parsed, "sort", error),
              "sort JSON round-trip: " + error);
    ctx.equal(parsed.keys.size(), std::size_t{2}, "sort keys round-trip");
    ctx.check(parsed.keys[0].ascending && parsed.keys[1].ascending,
              "sort direction round-trip");
}

void test_defaults(view_test::TestContext& ctx) {
    const TableDefinition& recovered = core::view::defaults::recovered_column_map();
    ctx.equal(recovered.columns.size(), std::size_t{41}, "41 recovered columns");
    ctx.check(recovered.source.find("analysis/column_field_map.tsv") != std::string::npos,
              "recovered table cites its evidence");

    std::size_t named = 0;
    for (const auto& column : recovered.columns) {
        const std::string name =
            core::view::field_name(column.field, core::model::RecordType::Task);
        if (!name.empty()) {
            ++named;
        }
        const core::model::FieldInfo* info = core::model::find_field_by_id(column.field);
        ctx.check(info != nullptr, "recovered field exists in the descriptor table");
        if (info != nullptr) {
            ctx.equal(static_cast<int>(info->record_type),
                      static_cast<int>(core::model::RecordType::Task),
                      "recovered field is a Task field");
        }
    }
    ctx.equal(named, std::size_t{10}, "ten recovered columns have descriptor names");

    /* The data must equal the committed TSV, in order.  This is the check that
     * keeps defaults.cpp from drifting away from its evidence. */
    const std::string tsv = view_test::read_text_file(
        std::string(VIEW_REPO_ROOT) + "/analysis/column_field_map.tsv");
    ctx.check(!tsv.empty(), "column_field_map.tsv readable");
    std::size_t line_start = 0;
    std::size_t row_index = 0;
    bool tsv_matches = true;
    while (line_start < tsv.size()) {
        std::size_t line_end = tsv.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = tsv.size();
        }
        std::string line = tsv.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        line_start = line_end + 1;
        if (row_index == 0 || line.empty()) {  // header
            ++row_index;
            continue;
        }
        const std::size_t first_tab = line.find('\t');
        const std::size_t second_tab = line.find('\t', first_tab + 1);
        const std::size_t third_tab = line.find('\t', second_tab + 1);
        if (first_tab == std::string::npos || second_tab == std::string::npos ||
            third_tab == std::string::npos) {
            tsv_matches = false;
            break;
        }
        const std::string id_text = line.substr(second_tab + 1, third_tab - second_tab - 1);
        const FieldId field = static_cast<FieldId>(std::strtoul(id_text.c_str(), nullptr, 16));
        if (row_index - 1 >= recovered.columns.size() ||
            recovered.columns[row_index - 1].field != field) {
            tsv_matches = false;
            break;
        }
        ++row_index;
    }
    ctx.check(tsv_matches, "recovered table equals column_field_map.tsv");
    ctx.equal(row_index - 1, recovered.columns.size(), "TSV row count equals table columns");

    const TableDefinition& entry = core::view::defaults::inferred_entry();
    ctx.equal(entry.columns.size(), std::size_t{7}, "inferred Entry column count");
    for (const auto& column : entry.columns) {
        ctx.check(core::model::find_field_by_id(column.field) != nullptr,
                  "inferred Entry field resolves");
    }
    ctx.contains(entry.source, "NOT oracle-verified", "inferred Entry is labelled");
    ctx.check(core::view::defaults::find_builtin_table("summary") != nullptr,
              "find_builtin_table finds summary");
    ctx.check(core::view::defaults::find_builtin_table("nope") == nullptr,
              "find_builtin_table misses unknown");
    ctx.equal(core::view::defaults::builtin_tables().size(), std::size_t{4},
              "four builtin tables");
}

std::string field_hex(FieldId field) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08x", field);
    return buffer;
}

char align_letter(ColumnAlign align) {
    switch (align) {
        case ColumnAlign::Left: return 'L';
        case ColumnAlign::Right: return 'R';
        case ColumnAlign::Center: return 'C';
        case ColumnAlign::Auto: return 'A';
    }
    return 'A';
}

struct PinnedTable {
    const char* id;
    const char* fields;  // comma-separated 8-digit PjField ids, in column order
    const char* widths;  // width + alignment letter, in column order
};

/*
 * The expected data is the decoded RT_RCDATA 330/311 table definition, not a
 * copy of table_registry.cpp: the resource records are (field id, width,
 * alignment code), so a drift in either order, id, width or alignment fails.
 */
constexpr PinnedTable kPinnedTables[] = {
    {"entry",
     "0b400017,0b4000cd,0b40000e,0b40001d,0b400023,0b400024,0b40002f,0b400031",
     "6R,6L,24L,9R,12R,12R,14L,17L"},
    {"variance",
     "0b400017,0b40000e,0b400023,0b400024,0b40002b,0b40002c,0b40002d,0b40002e",
     "6R,20L,12R,12R,15R,15R,11R,11R"},
    {"earned-value",
     "0b400017,0b40000e,0b40000c,0b40000b,0b400078,0b40000d,0b400053,0b40021d,"
     "0b400006,0b4001b9",
     "6R,24L,11R,11R,11R,11R,11R,11R,11R,11R"},
    {"baseline",
     "0b400017,0b40000e,0b40001b,0b40002b,0b40002c,0b400001,0b400006",
     "6R,24L,13R,15R,15R,14R,14R"},
    {"tracking",
     "0b400017,0b40000e,0b400029,0b40002a,0b400020,0b40045f,0b40001c,0b40001f,"
     "0b400007,0b400002",
     "6R,20L,12R,12R,9R,9C,9R,10R,11R,10R"},
    {"schedule",
     "0b400017,0b40000e,0b400023,0b400024,0b400027,0b400028,0b400015,0b400016",
     "6R,20L,12R,12R,12R,12R,11R,11R"},
    {"work",
     "0b400017,0b40000e,0b400000,0b400001,0b400003,0b400002,0b400004,0b400021",
     "6R,24L,11R,11R,11R,11R,11R,12R"},
    {"cost",
     "0b400017,0b40000e,0b400008,0b4000c8,0b400005,0b400006,0b400009,0b400007,"
     "0b40000a",
     "6R,24L,11R,17R,11R,11R,11R,11R,11R"},
    {"summary",
     "0b400017,0b40000e,0b40001d,0b400023,0b400024,0b400020,0b400005,0b400000",
     "6R,24L,12R,12R,12R,9R,11R,10R"},
    {"constraint-dates", "0b400017,0b40000e,0b40001d,0b400011,0b400012",
     "6R,24L,9R,19R,15R"},
    {"delay",
     "0b400017,0b4000cd,0b40000e,0b400014,0b40001d,0b400023,0b400024,0b400030,"
     "0b400031",
     "6R,6L,24L,13R,9R,12R,12R,14L,17L"},
    {"usage", "0b400017,0b4000cd,0b40000e,0b400000,0b40001d,0b400023,0b400024",
     "6R,6L,24L,10R,10R,12R,12R"},
    {"hyperlink", "0b400017,0b4000cd,0b40000e,0b4000d9,0b4000da,0b4000db",
     "6R,6L,24L,17L,24L,17L"},
    {"earned-value-schedule-indicators",
     "0b400017,0b40000e,0b40000c,0b40000b,0b40000d,0b40021c,0b40021a",
     "6R,24L,11R,11R,11R,9R,9R"},
    {"earned-value-cost-indicators",
     "0b400017,0b40000e,0b40000c,0b40000b,0b400053,0b40021b,0b400219,0b400006,"
     "0b40021d,0b4001b9,0b40021e",
     "6R,24L,11R,11R,11R,9R,9R,11R,11R,11R,9R"},
    {"resource-entry",
     "0c400000,0c400056,0c400001,0c4000fc,0c40012c,0c40012b,0c400002,0c400003,"
     "0c400004,0c400006,0c400007,0c400012,0c400013,0c400005,0c40000a",
     "6R,6L,20L,20L,11L,13L,11L,10L,10R,11R,11R,11R,10L,14L,13L"},
    {"resource-usage", "0c400000,0c400056,0c400001,0c40000d", "6R,6L,20L,10R"},
    {"resource-cost", "0c400000,0c400001,0c40000c,0c400011,0c400018,0c40000b,0c400015",
     "6R,20L,14R,14R,14R,14R,14R"},
    {"resource-work",
     "0c400000,0c400001,0c40001d,0c40000d,0c400010,0c40000f,0c400017,0c40000e,"
     "0c400016",
     "6R,20L,9R,11R,11R,11R,11R,11R,11R"},
    {"resource-summary",
     "0c400000,0c400001,0c400003,0c400004,0c40001a,0c400006,0c400007,0c40000c,"
     "0c40000d",
     "6R,20L,10L,10R,5R,11R,11R,14R,11R"},
    {"resource-hyperlink", "0c400000,0c400056,0c400001,0c40008a,0c40008b,0c40008c",
     "6R,6L,24L,17L,24L,17L"},
    {"resource-earned-value",
     "0c400000,0c400001,0c400033,0c400034,0c400035,0c400036,0c40010c,0c40000c,"
     "0c400011,0c40012d",
     "6R,24L,11R,11R,11R,11R,11R,11R,11R,11R"},
    {"resource-entry-work-resources",
     "0c400000,0c400056,0c400001,0c40012c,0c4000fc,0c400002,0c400003,0c400004,"
     "0c400006,0c400007,0c400012,0c400013,0c400005,0c40000a",
     "6R,6L,20L,11L,20L,11L,10L,10R,11R,11R,11R,10L,14L,13L"},
    {"resource-entry-material-resources",
     "0c400000,0c400056,0c400001,0c40012c,0c40012b,0c4000fc,0c400002,0c400003,"
     "0c400006,0c400012,0c400013,0c40000a",
     "6R,6L,20L,11L,13L,20L,11L,10L,11R,11R,10L,13L"},
};

void test_table_registry(view_test::TestContext& ctx) {
    using core::view::registry::all_tables;
    using core::view::registry::find_table;
    using core::view::registry::resource_tables;
    using core::view::registry::task_tables;

    ctx.equal(task_tables().size(), std::size_t{15}, "15 measured task tables");
    ctx.equal(resource_tables().size(), std::size_t{9}, "9 measured resource tables");
    ctx.equal(all_tables().size(), std::size_t{24}, "24 tables in the registry");

    for (const PinnedTable& pinned : kPinnedTables) {
        const TableDefinition* table = find_table(pinned.id);
        ctx.check(table != nullptr, std::string("registry has table ") + pinned.id);
        if (table == nullptr) {
            continue;
        }
        ctx.contains(table->source, "RT_RCDATA",
                     std::string(pinned.id) + " cites the resource");
        ctx.check(!table->columns.empty(), std::string(pinned.id) + " has columns");
        std::string fields;
        std::string widths;
        bool duplicate = false;
        for (std::size_t i = 0; i < table->columns.size(); ++i) {
            const Column& column = table->columns[i];
            if (i != 0) {
                fields += ',';
                widths += ',';
            }
            fields += field_hex(column.field);
            widths += std::to_string(column.width);
            widths += align_letter(column.align);
            const core::model::FieldInfo* info = core::model::find_field_by_id(column.field);
            ctx.check(info != nullptr,
                      std::string(pinned.id) + " field exists in the descriptor table");
            if (info != nullptr) {
                ctx.equal(static_cast<int>(info->record_type),
                          static_cast<int>(table->record_type),
                          std::string(pinned.id) + " field belongs to the table's record type");
            }
            const std::string title = column.display_title(table->record_type);
            ctx.check(!title.empty(), std::string(pinned.id) + " column has a title");
            ctx.check(title.rfind("0x", 0) != 0,
                      std::string(pinned.id) + " column title is not a hex id");
            if (table->find(column.field) != &column) {
                duplicate = true;
            }
        }
        ctx.check(!duplicate, std::string(pinned.id) + " has no duplicate field");
        ctx.equal_string(fields, pinned.fields, std::string(pinned.id) + " column list");
        ctx.equal_string(widths, pinned.widths,
                         std::string(pinned.id) + " widths and alignment");
    }

    // Lookup forms: id, name, and name scoped by record type.
    const TableDefinition* entry = find_table("entry");
    ctx.check(entry != nullptr && entry->record_type == core::model::RecordType::Task,
              "find_table by id returns the Task Entry table");
    const TableDefinition* by_name = find_table("Entry");
    ctx.check(by_name != nullptr && by_name->id == "entry",
              "find_table by name finds the first Entry");
    const TableDefinition* resource_entry =
        find_table("Entry", core::model::RecordType::Resource);
    ctx.check(resource_entry != nullptr && resource_entry->id == "resource-entry",
              "find_table scoped to Resource finds the Resource Entry table");
    ctx.check(find_table("Entry", core::model::RecordType::Assignment) == nullptr,
              "find_table returns null for an unknown record type");
    ctx.check(find_table("no-such-table") == nullptr, "find_table misses an unknown id");

    // The measured Entry table is the canonical 8-column sheet, including the
    // Indicators column the old inferred table omitted.
    ctx.equal(entry->columns.size(), std::size_t{8}, "Entry has eight columns");
    if (entry != nullptr && entry->columns.size() == 8) {
        const char* titles[] = {"ID",       "Indicators", "Task Name",   "Duration",
                                "Start",    "Finish",     "Predecessors", "Resource Names"};
        for (std::size_t i = 0; i < 8; ++i) {
            ctx.equal_string(entry->columns[i].display_title(entry->record_type), titles[i],
                             "Entry title " + std::to_string(i));
        }
        ctx.equal(entry->columns[0].width, 6, "Entry ID width");
        ctx.check(entry->columns[0].align == ColumnAlign::Right, "Entry ID is right aligned");
        ctx.equal(entry->columns[2].width, 24, "Entry Task Name width");
        ctx.check(entry->columns[2].align == ColumnAlign::Left, "Entry Task Name is left aligned");
    }

    // Tracking pins the resource's abbreviated captions, not the field names.
    const TableDefinition* tracking = find_table("tracking");
    ctx.check(tracking != nullptr, "Tracking table exists");
    if (tracking != nullptr && tracking->columns.size() == 10) {
        ctx.equal_string(tracking->columns[2].display_title(tracking->record_type), "Act. Start",
                         "Tracking ActualStart caption");
        ctx.equal_string(tracking->columns[4].display_title(tracking->record_type), "% Comp.",
                         "Tracking PercentComplete caption");
        ctx.equal_string(tracking->columns[5].display_title(tracking->record_type),
                         "Phys. % Comp.", "Tracking PhysicalPercentComplete caption");
        ctx.check(tracking->columns[5].align == ColumnAlign::Center,
                  "Tracking Phys. % Comp. is centred (resource alignment code 2)");
    }
    const TableDefinition* schedule = find_table("schedule");
    ctx.check(schedule != nullptr && schedule->columns.size() == 8,
              "Schedule has eight columns");
    if (schedule != nullptr && schedule->columns.size() == 8) {
        ctx.equal_string(schedule->columns[4].display_title(schedule->record_type), "LateStart",
                         "Schedule LateStart title");
        ctx.equal_string(schedule->columns[6].display_title(schedule->record_type), "FreeSlack",
                         "Schedule FreeSlack title");
    }

    // The old (inferred) API is untouched, so the macOS shell keeps compiling.
    ctx.equal(core::view::defaults::builtin_tables().size(), std::size_t{4},
              "legacy builtin table list is unchanged");
    ctx.equal(core::view::defaults::inferred_entry().columns.size(), std::size_t{7},
              "legacy inferred Entry is unchanged");
}

void test_view_set(view_test::TestContext& ctx) {
    const char* text = R"JSON({
  "version": 1,
  "tables": [
    {"id": "entry", "name": "Entry",
     "columns": [{"field": "ID", "width": 6, "align": "right"},
                  {"field": "Name"},
                  {"field": "Duration", "align": "right"}]}
  ],
  "filters": [
    {"id": "critical", "name": "Critical tasks",
     "criteria": {"and": [
        {"field": "Summary", "op": "equals", "value": false},
        {"or": [{"field": "Duration", "op": "greater_than", "value": "PT0H0M0S"},
                 {"field": "Name", "op": "contains", "value": "milestone"}]}
     ]}}
  ],
  "groups": [
    {"id": "by-resource", "name": "By resource",
     "keys": [{"field": "ResourceNames", "order": "ascending"}]}
  ],
  "sorts": [
    {"id": "by-start", "name": "By start",
     "keys": [{"field": "Start", "order": "descending"}]}
  ]
})JSON";

    core::view::ViewSet set;
    std::string error;
    ctx.check(core::view::view_set_parse(text, set, error),
              "view set parses: " + error);
    ctx.equal(set.tables.size(), std::size_t{1}, "one table");
    ctx.equal(set.filters.size(), std::size_t{1}, "one filter");
    ctx.equal(set.groups.size(), std::size_t{1}, "one group");
    ctx.equal(set.sorts.size(), std::size_t{1}, "one sort");
    ctx.check(set.find_table("entry") != nullptr, "find_table");
    ctx.check(set.find_filter("critical") != nullptr, "find_filter");
    ctx.check(set.find_group("by-resource") != nullptr, "find_group");
    ctx.check(set.find_sort("by-start") != nullptr, "find_sort");

    const std::string written = core::view::view_set_write(set, true);
    core::view::ViewSet reparsed;
    ctx.check(core::view::view_set_parse(written, reparsed, error),
              "view set round-trip parses: " + error);
    ctx.equal(reparsed.tables.size(), set.tables.size(), "round-trip tables");
    ctx.equal(reparsed.filters.size(), set.filters.size(), "round-trip filters");
    ctx.equal(reparsed.groups.size(), set.groups.size(), "round-trip groups");
    ctx.equal(reparsed.sorts.size(), set.sorts.size(), "round-trip sorts");
    ctx.equal(reparsed.tables[0].columns.size(), std::size_t{3}, "round-trip columns");
    ctx.check(reparsed.tables[0].columns[0].align == ColumnAlign::Right,
              "round-trip explicit alignment");
    ctx.check(reparsed.filters[0].criteria.kind == FilterCriterion::Kind::All,
              "round-trip filter kind");

    core::view::ViewSet ignored;
    ctx.check(!core::view::view_set_parse("{\"version\": 2, \"tables\": []}", ignored, error),
              "unsupported version rejected");
    ctx.contains(error, "version", "version error text");
    ctx.check(!core::view::view_set_parse("{\"tables\": [}", ignored, error),
              "malformed JSON rejected");
    ctx.contains(error, "JSON line", "parse error names the line");

    const JsonValue empty = core::view::parse_json("{}").value;
    ctx.check(core::view::view_set_from_json(empty, ignored, error),
              "empty view set is valid");
    ctx.equal(ignored.tables.size(), std::size_t{0}, "empty view set has no tables");
}

}  // namespace

int main() {
    view_test::TestContext ctx;
    test_values(ctx);
    test_json(ctx);
    test_record_set(ctx);
    test_table(ctx);
    test_filter(ctx);
    test_filter_lists_and_auto_filter(ctx);
    test_group(ctx);
    test_group_order_and_summary(ctx);
    test_outline_sort(ctx);
    test_sort(ctx);
    test_defaults(ctx);
    test_table_registry(ctx);
    test_view_set(ctx);
    return ctx.finish("view_model_tests");
}
