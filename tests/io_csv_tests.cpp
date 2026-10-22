// tests/io_csv_tests.cpp -- CSV export is the table definition applied to a
// record set: column order, titles, quoting and the filter/order overloads.
#include <sstream>
#include <string>
#include <vector>

#include "csv.h"
#include "filter.h"
#include "record_set.h"
#include "table.h"
#include "value.h"
#include "view_test_support.h"

namespace {

using core::view::ColumnAlign;
using core::view::DateValue;
using core::view::FilterCriterion;
using core::view::FilterDefinition;
using core::view::FilterOp;
using core::view::RecordSet;
using core::view::TableDefinition;
using core::view::Value;

constexpr core::view::FieldId kName = 0x0b40000e;
constexpr core::view::FieldId kDuration = 0x0b40001d;
constexpr core::view::FieldId kStart = 0x0b400023;
constexpr core::view::FieldId kNotes = 0x0b40000f;
constexpr core::view::FieldId kCost = 0x0b400005;
constexpr core::view::FieldId kSummary = 0x0b40005c;
constexpr core::view::FieldId kPercentComplete = 0x0b400020;
constexpr core::view::FieldId kActive = 0x0b4004ff;  // deliberately absent from the records

DateValue date_of(int year, int month, int day, int hour) {
    DateValue date;
    date.year = year;
    date.month = static_cast<std::uint8_t>(month);
    date.day = static_cast<std::uint8_t>(day);
    date.hour = static_cast<std::uint8_t>(hour);
    return date;
}

RecordSet sample_records() {
    RecordSet records(core::model::RecordType::Task);
    /* Schema deliberately in a different order from the table. */
    records.add_field(kSummary);
    records.add_field(kNotes);
    records.add_field(kCost);
    records.add_field(kPercentComplete);
    records.add_field(kStart);
    records.add_field(kName);
    records.add_field(kDuration);

    std::size_t row = records.add_row(1);
    records.set_field(row, kName, Value::of_text("Design, phase 1"));
    records.set_field(row, kDuration, Value::of_duration(9600));
    records.set_field(row, kStart, Value::of_date(date_of(2024, 1, 1, 8)));
    records.set_field(row, kNotes, Value::of_text("line one\nline \"two\""));
    records.set_field(row, kCost, Value::of_number(1200.5));
    records.set_field(row, kSummary, Value::of_bool(false));
    records.set_field(row, kPercentComplete, Value::of_int(50));

    row = records.add_row(2);
    records.set_field(row, kName, Value::of_text("Build"));
    records.set_field(row, kDuration, Value::of_duration(24000));
    records.set_field(row, kStart, Value::of_date(date_of(2024, 1, 8, 8)));
    records.set_field(row, kCost, Value::of_number(5000.0));
    records.set_field(row, kSummary, Value::of_bool(true));
    records.set_field(row, kPercentComplete, Value::of_int(0));
    return records;
}

TableDefinition sample_table() {
    TableDefinition table;
    table.id = "export";
    table.name = "Export";
    table.record_type = core::model::RecordType::Task;
    table.columns.push_back({kStart, {}, 0, ColumnAlign::Auto});
    table.columns.push_back({kName, {}, 0, ColumnAlign::Auto});
    table.columns.push_back({kDuration, {}, 0, ColumnAlign::Right});
    table.columns.push_back({kNotes, {}, 0, ColumnAlign::Auto});
    table.columns.push_back({kCost, {}, 0, ColumnAlign::Right});
    table.columns.push_back({kSummary, {}, 0, ColumnAlign::Auto});
    table.columns.push_back({kActive, {}, 0, ColumnAlign::Auto});
    return table;
}

void test_basic_export(view_test::TestContext& ctx) {
    const RecordSet records = sample_records();
    const TableDefinition table = sample_table();
    const std::string csv = core::io::export_csv(records, table);

    const std::string expected =
        "Start,Name,Duration,Notes,Cost,Summary,Active\r\n"
        "2024-01-01 08:00,\"Design, phase 1\",PT16H0M0S,\"line one\nline \"\"two\"\"\",1200.5,0,\r\n"
        "2024-01-08 08:00,Build,PT40H0M0S,,5000,1,\r\n";
    ctx.equal_string(csv, expected, "CSV honours table column order and quotes fields");

    ctx.equal_string(core::io::csv_field("plain", {}), "plain", "plain field not quoted");
    ctx.equal_string(core::io::csv_field("a,b", {}), "\"a,b\"", "comma field quoted");
    ctx.equal_string(core::io::csv_field("a\"b", {}), "\"a\"\"b\"", "quote doubled");
    ctx.equal_string(core::io::csv_field("a\nb", {}), "\"a\nb\"", "newline field quoted");
    ctx.equal_string(core::io::csv_cell(nullptr, {}), "", "null cell is empty");
    ctx.equal_string(core::io::csv_cell(records.get(0, kSummary), {}), "0",
                     "bool renders as 0/1");
}

void test_options(view_test::TestContext& ctx) {
    const RecordSet records = sample_records();
    const TableDefinition table = sample_table();

    core::io::CsvOptions no_header;
    no_header.header = false;
    no_header.crlf = false;
    const std::string without_header = core::io::export_csv(records, table, no_header);
    ctx.contains(without_header, "2024-01-01 08:00", "rows exported without header");
    ctx.check(without_header.find("Start,Name") == std::string::npos, "header omitted");
    ctx.check(without_header.find("\r\n") == std::string::npos, "LF-only option");

    core::io::CsvOptions yes_no;
    yes_no.bool_as_yes_no = true;
    const std::string yes_no_csv = core::io::export_csv(records, table, yes_no);
    ctx.contains(yes_no_csv, ",No,\r\n", "bool as No");
    ctx.contains(yes_no_csv, ",Yes,\r\n", "bool as Yes");

    core::io::CsvOptions days;
    days.duration_style = core::view::DurationStyle::Days;
    const std::string days_csv = core::io::export_csv(records, table, days);
    ctx.check(days_csv.find("PT16H0M0S") == std::string::npos,
              "Days style does not use ISO durations");
    ctx.contains(days_csv, ",2d,", "duration as days");
    ctx.contains(days_csv, ",5d,", "duration as 5 days");

    core::io::CsvOptions semicolon;
    semicolon.delimiter = ';';
    const std::string semicolon_csv = core::io::export_csv(records, table, semicolon);
    ctx.contains(semicolon_csv, "Design, phase 1", "comma not quoted with ';' delimiter");
    ctx.check(semicolon_csv.find(";Name;") != std::string::npos, "delimiter changed");
}

void test_filter_and_order(view_test::TestContext& ctx) {
    const RecordSet records = sample_records();
    const TableDefinition table = sample_table();

    FilterDefinition filter;
    filter.id = "not-summary";
    filter.name = "Not summary";
    filter.criteria =
        FilterCriterion::comparison(kSummary, FilterOp::Equals, Value::of_bool(false));
    const std::string filtered = core::io::export_csv(records, table, filter);
    ctx.contains(filtered, "Design, phase 1", "filtered export keeps the matching row");
    ctx.check(filtered.find("Build") == std::string::npos, "filtered export drops the other");

    const std::string reversed = core::io::export_csv_rows(records, table, {1, 0});
    const std::size_t build_at = reversed.find("Build");
    const std::size_t design_at = reversed.find("Design, phase 1");
    ctx.check(build_at != std::string::npos && design_at != std::string::npos,
              "reversed order contains both rows");
    ctx.check(build_at < design_at, "export_csv_rows honours the supplied order");

    const std::string out_of_range = core::io::export_csv_rows(records, table, {0, 99});
    ctx.contains(out_of_range, "Design", "out-of-range order rows skipped");
    ctx.equal(out_of_range.find("99"), std::string::npos, "no phantom row");
    std::ostringstream stream;
    const bool ok = core::io::write_csv(stream, records, table, {0, 1}, {});
    ctx.check(ok, "write_csv returns true");
    ctx.equal_string(stream.str(), core::io::export_csv(records, table),
                     "write_csv matches export_csv");
}

}  // namespace

int main() {
    view_test::TestContext ctx;
    test_basic_export(ctx);
    test_options(ctx);
    test_filter_and_order(ctx);
    return ctx.finish("io_csv_tests");
}
