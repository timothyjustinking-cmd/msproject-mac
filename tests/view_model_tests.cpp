// tests/view_model_tests.cpp -- the view model: values, record sets, table,
// filter, group, sort definitions, their JSON round-trip, and the default-table
// data checked against the analysis TSV it claims to come from.
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
#include "value.h"
#include "view_set.h"
#include "view_test_support.h"

namespace {

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
    test_group(ctx);
    test_sort(ctx);
    test_defaults(ctx);
    test_view_set(ctx);
    return ctx.finish("view_model_tests");
}
