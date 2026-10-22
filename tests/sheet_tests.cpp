// tests/sheet_tests.cpp -- the task sheet model: columns and titles, value
// formatting per kind, outline collapse and its interaction with selection,
// editing (parsing, validation, write-through) and undo/redo, plus geometry.
//
// No corpus and no MS Project are needed: the fixtures are RecordSets built in
// this file, the same shape the MSPDI importer and the reader bridge produce.
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "defaults.h"
#include "field_ref.h"
#include "record_set.h"
#include "sheet.h"
#include "table.h"
#include "value.h"
#include "view_test_support.h"

namespace {

using core::sheet::CellAlign;
using core::sheet::CellKind;
using core::sheet::CellRange;
using core::sheet::CellRef;
using core::sheet::ClipboardBlock;
using core::sheet::ClipboardCell;
using core::sheet::ColumnSegment;
using core::sheet::DateFormat;
using core::sheet::EditCommand;
using core::sheet::PasteMode;
using core::sheet::SheetColumn;
using core::sheet::SheetFormat;
using core::sheet::SheetMetrics;
using core::sheet::SheetModel;
using core::sheet::UndoEntry;
using core::sheet::UndoKind;
using core::sheet::UndoStep;
using core::view::ColumnAlign;
using core::view::DateValue;
using core::view::FieldId;
using core::view::RecordSet;
using core::view::TableDefinition;
using core::view::Value;
using core::view::ValueKind;

constexpr std::size_t kNoIndex = core::sheet::kNoIndex;

FieldId require_field(view_test::TestContext& ctx, const char* name) {
    FieldId field = 0;
    const bool found = core::view::field_id_by_name(core::model::RecordType::Task, name, field);
    ctx.check(found, std::string("descriptor table has Task.") + name);
    return field;
}

DateValue date_of(int year, int month, int day, int hour = 0, int minute = 0) {
    DateValue date;
    date.year = year;
    date.month = static_cast<std::uint8_t>(month);
    date.day = static_cast<std::uint8_t>(day);
    date.hour = static_cast<std::uint8_t>(hour);
    date.minute = static_cast<std::uint8_t>(minute);
    return date;
}

SheetColumn make_column(FieldId field, CellKind kind = CellKind::Auto) {
    SheetColumn column;
    column.field = field;
    column.kind = kind;
    return column;
}

/*
 * The fixture the whole file shares.  Six tasks with a two-level outline:
 *   row 0  Design  level 0  summary  (children 1, 2)
 *   row 1  Draft   level 1
 *   row 2  Review  level 1
 *   row 3  Build   level 0  summary  (child 4)
 *   row 4  Code    level 1  critical, milestone (0 duration)
 *   row 5  Close   level 0
 */
struct Fixture {
    FieldId id = 0;
    FieldId name = 0;
    FieldId duration = 0;
    FieldId start = 0;
    FieldId finish = 0;
    FieldId percent = 0;
    FieldId cost = 0;
    FieldId summary = 0;
    FieldId outline_level = 0;
    FieldId critical = 0;
    FieldId notes = 0;

    RecordSet records{core::model::RecordType::Task};

    explicit Fixture(view_test::TestContext& ctx) {
        id = require_field(ctx, "ID");
        name = require_field(ctx, "Name");
        duration = require_field(ctx, "Duration");
        start = require_field(ctx, "Start");
        finish = require_field(ctx, "Finish");
        percent = require_field(ctx, "PercentComplete");
        cost = require_field(ctx, "Cost");
        summary = require_field(ctx, "Summary");
        outline_level = require_field(ctx, "OutlineLevel");
        critical = require_field(ctx, "Critical");
        notes = require_field(ctx, "Notes");

        for (FieldId field : {id, name, duration, start, finish, percent, cost, summary,
                              outline_level, critical, notes}) {
            records.add_field(field);
        }

        add_row(1, 1, "Design", 24000, date_of(2026, 10, 19), date_of(2026, 10, 23), 100,
                1234.5, true, 0, false, "top level");
        add_row(2, 2, "Draft", 9600, date_of(2026, 10, 20), date_of(2026, 10, 21, 17, 0), 50,
                0.0, false, 1, false, "");
        add_row(3, 3, "Review", 2400, date_of(2026, 10, 20), date_of(2026, 10, 25), 0, 99.999,
                false, 1, false, "needs review");
        add_row(4, 5, "Build", 14400, date_of(2026, 10, 21), date_of(2026, 10, 28), 0, 0.0, true,
                0, false, "");
        add_row(5, 6, "Code", 0, date_of(2026, 10, 22), date_of(2026, 10, 22), 0, 0.0, false, 1,
                true, "milestone");
        add_row(6, 7, "Close", 4800, date_of(2026, 10, 23), date_of(2026, 10, 23), 0, 10.0,
                false, 0, false, "handover");
    }

    void add_row(std::uint32_t uid, std::int64_t id_value, const char* name_value,
                 std::int64_t duration_ticks, const DateValue& start_value,
                 const DateValue& finish_value, std::int64_t percent_value, double cost_value,
                 bool summary_value, std::int64_t level, bool critical_value,
                 const char* notes_value) {
        const std::size_t row = records.add_row(uid);
        records.set_field(row, id, Value::of_int(id_value));
        records.set_field(row, name, Value::of_text(name_value));
        records.set_field(row, duration, Value::of_duration(duration_ticks));
        records.set_field(row, start, Value::of_date(start_value));
        records.set_field(row, finish, Value::of_date(finish_value));
        records.set_field(row, percent, Value::of_int(percent_value));
        records.set_field(row, cost, Value::of_number(cost_value));
        records.set_field(row, summary, Value::of_bool(summary_value));
        records.set_field(row, outline_level, Value::of_int(level));
        records.set_field(row, critical, Value::of_bool(critical_value));
        records.set_field(row, notes, Value::of_text(notes_value));
    }

    /* Columns: 0 ID, 1 Name, 2 Duration, 3 Start, 4 Finish, 5 % Complete,
     *          6 Cost, 7 Summary, 8 Notes. */
    std::vector<SheetColumn> columns() const {
        std::vector<SheetColumn> columns;
        columns.push_back(make_column(id));
        columns.push_back(make_column(name));
        columns.push_back(make_column(duration));
        columns.push_back(make_column(start));
        columns.push_back(make_column(finish));
        columns.push_back(make_column(percent, CellKind::Percent));
        columns.push_back(make_column(cost, CellKind::Cost));
        columns.push_back(make_column(summary));
        columns.push_back(make_column(notes));
        return columns;
    }
};

constexpr std::size_t kColumnCount = 9;
constexpr std::size_t kRowCount = 6;

SheetMetrics default_metrics() {
    SheetMetrics metrics;
    metrics.row_height = 20;
    metrics.header_height = 24;
    metrics.char_width = 7;
    metrics.indent_per_level = 12;
    return metrics;
}

/* ---- columns ----------------------------------------------------------- */

void test_columns(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    ctx.equal(sheet.column_count(), kColumnCount, "nine columns");
    ctx.equal(sheet.row_count(), kRowCount, "six rows");
    ctx.equal(sheet.column_of_field(fixture.duration), std::size_t(2), "Duration is column 2");
    ctx.equal(sheet.column_of_field(0), kNoIndex, "field 0 is in no column");
    ctx.equal_string(sheet.column_title(0), "ID", "ID header");
    ctx.equal_string(sheet.column_title(1), "Name", "Name header");
    ctx.equal_string(sheet.column_title(8), "Notes", "Notes header");
    ctx.equal_string(sheet.column_title(99), "", "out-of-range header is empty");

    ctx.equal(sheet.visible_column_count(), kColumnCount, "all columns visible by default");
    ctx.equal(sheet.visible_column(1), std::size_t(1), "visible ordinal 1 is column 1");
    ctx.equal(sheet.visible_column(kColumnCount), kNoIndex, "visible ordinal past the end");
    ctx.equal(sheet.display_column(2), std::size_t(2), "column 2 is at display position 2");
    ctx.equal(sheet.display_column(kColumnCount), kNoIndex, "out-of-range column");

    ctx.check(sheet.set_column_visible(3, false), "hide the Start column");
    ctx.equal(sheet.visible_column_count(), kColumnCount - 1, "one column hidden");
    ctx.equal(sheet.display_column(3), kNoIndex, "hidden column has no display position");
    ctx.equal(sheet.display_column(4), std::size_t(3), "column 4 moves to position 3");
    ctx.equal(sheet.visible_column(3), std::size_t(4), "visible ordinal 3 is column 4");
    ctx.check(!sheet.column_visible(3), "column 3 reports hidden");
    ctx.check(!sheet.column_visible(kColumnCount), "out-of-range column is not visible");
    ctx.equal(sheet.column(kColumnCount).field, FieldId(0), "out-of-range column is empty");

    ctx.check(sheet.set_column_visible(3, true), "unhide the Start column");
    ctx.equal(sheet.display_column(3), std::size_t(3), "column 3 is back at position 3");

    ctx.check(sheet.set_column_width(0, 6), "width 6 characters on ID");
    ctx.equal(sheet.column_width_points(0, default_metrics()), 42, "6 characters at 7 points");
    ctx.check(!sheet.set_column_width(kColumnCount, 6), "width on a bad column fails");
    ctx.check(!sheet.set_column_width(0, -1), "negative width is rejected");
    ctx.equal(sheet.column_width_points(1, default_metrics()), 0,
              "auto width is 0 (the UI measures)");

    ctx.check(sheet.set_column_title(1, "Task Name"), "override the Name header");
    ctx.equal_string(sheet.column_title(1), "Task Name", "authored title wins");
    ctx.check(!sheet.set_column_title(kColumnCount, "x"), "title on a bad column fails");
    ctx.equal_string(sheet.column_title(0), "ID", "other headers unaffected");
}

/* ---- formatting per kind ----------------------------------------------- */

void test_formatting(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    /* Text and integer. */
    ctx.equal_string(sheet.cell_text(0, 0), "1", "ID integer");
    ctx.equal_string(sheet.cell_text(0, 1), "Design", "text raw");
    ctx.equal_string(sheet.cell_text(2, 8), "needs review", "text with a space is raw");
    ctx.equal_string(sheet.cell_text(1, 8), "", "empty text renders empty");

    /* Durations: an 8-hour day, the largest unit that fits, at most 2 decimals. */
    ctx.equal_string(sheet.cell_text(0, 2), "5 days", "five working days");
    ctx.equal_string(sheet.cell_text(1, 2), "2 days", "two days");
    ctx.equal_string(sheet.cell_text(2, 2), "4 hrs", "4 hours (2400 ticks)");
    ctx.equal_string(sheet.cell_text(4, 2), "0 days", "a milestone is 0 days");
    ctx.equal_string(sheet.cell_text(5, 2), "1 day", "singular day");
    ctx.equal(core::sheet::format_duration_text(7200, sheet.format()), std::string("1.5 days"),
              "12 hours is 1.5 days");
    ctx.equal(core::sheet::format_duration_text(300, sheet.format()), std::string("30 mins"),
              "30 minutes");
    ctx.equal(core::sheet::format_duration_text(600, sheet.format()), std::string("1 hr"),
              "singular hour");
    ctx.equal(core::sheet::format_duration_text(10, sheet.format()), std::string("1 min"),
              "singular minute");
    ctx.equal(core::sheet::format_duration_text(5, sheet.format()), std::string("0.5 mins"),
              "half a minute");
    ctx.equal(core::sheet::format_duration_text(-14400, sheet.format()), std::string("-3 days"),
              "negative duration");
    ctx.equal(core::sheet::format_duration_text(0, sheet.format()), std::string("0 days"),
              "zero duration");

    /* Dates: the app's date format, weekday + dd/mm/yy. */
    ctx.equal_string(sheet.cell_text(0, 3), "Mon 19/10/26", "weekday abbreviation and dd/mm/yy");
    ctx.equal_string(sheet.cell_text(1, 3), "Tue 20/10/26", "the brief's example date");
    ctx.equal_string(sheet.cell_text(1, 4), "Wed 21/10/26", "finish date");
    ctx.equal(core::sheet::format_date_text(date_of(2026, 10, 20), sheet.format()),
              std::string("Tue 20/10/26"), "date text helper");

    /* Percent, cost, bool. */
    ctx.equal_string(sheet.cell_text(0, 5), "100%", "100 percent");
    ctx.equal_string(sheet.cell_text(1, 5), "50%", "50 percent");
    ctx.equal_string(sheet.cell_text(0, 6), "1,234.50", "cost grouped with two decimals");
    ctx.equal_string(sheet.cell_text(2, 6), "100.00", "cost rounds to two decimals");
    ctx.equal_string(sheet.cell_text(4, 6), "0.00", "zero cost keeps its decimals");
    ctx.equal_string(sheet.cell_text(0, 7), "Yes", "bool true is Yes");
    ctx.equal_string(sheet.cell_text(1, 7), "No", "bool false is No");

    /* Value-kind fallbacks: a mismatch renders as its own kind, never blank. */
    ctx.equal(core::sheet::format_value_text(Value::of_number(12.5), CellKind::Percent,
                                             sheet.format()),
              std::string("12.5%"), "fractional percent");
    ctx.equal(core::sheet::format_value_text(Value::of_int(50), CellKind::Cost, sheet.format()),
              std::string("50.00"), "integer cost");
    ctx.equal(core::sheet::format_value_text(Value::of_duration(9600), CellKind::Text,
                                             sheet.format()),
              std::string("2 days"), "duration in a text column shows its own kind");
    ctx.equal(core::sheet::format_value_text(Value{}, CellKind::Cost, sheet.format()),
              std::string(""), "null is always empty");

    /* cell_kind resolves as documented. */
    ctx.equal(sheet.cell_kind(0, 0), CellKind::Integer, "ID is an integer cell");
    ctx.equal(sheet.cell_kind(0, 2), CellKind::Duration, "Duration value is a duration cell");
    ctx.equal(sheet.cell_kind(0, 3), CellKind::Date, "Start is a date cell");
    ctx.equal(sheet.cell_kind(0, 5), CellKind::Percent, "declared percent column");
    ctx.equal(sheet.cell_kind(0, 6), CellKind::Cost, "declared cost column");
    ctx.equal(sheet.cell_kind(0, 7), CellKind::Bool, "Summary is a bool cell");
}

/* ---- format settings ---------------------------------------------------- */

void test_format_settings(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    /* A 10-hour day changes the duration text the same ticks get. */
    SheetFormat ten_hour = sheet.format();
    ten_hour.hours_per_day = 10;
    sheet.set_format(ten_hour);
    ctx.equal_string(sheet.cell_text(0, 2), "4 days", "40 hours at a 10-hour day");
    ctx.equal_string(sheet.cell_text(3, 2), "2.4 days", "24 hours at a 10-hour day");
    ctx.equal(core::sheet::format_duration_text(3000, sheet.format()), std::string("5 hrs"),
              "5 hours is 5 hrs, not 0.5 day");
    ctx.equal(core::sheet::format_duration_text(30000, sheet.format()), std::string("5 days"),
              "30,000 ticks is five 10-hour days");

    /* Unit labels are configuration, not English. */
    SheetFormat german = sheet.format();
    german.hours_per_day = 8;
    german.units.day_singular = "Tag";
    german.units.day_plural = "Tage";
    sheet.set_format(german);
    ctx.equal_string(sheet.cell_text(5, 2), "1 Tag", "singular day label");
    ctx.equal_string(sheet.cell_text(1, 2), "2 Tage", "plural day label");

    /* Cost presentation. */
    SheetFormat money = sheet.format();
    money.currency_symbol = "$";
    money.group_thousands = false;
    sheet.set_format(money);
    ctx.equal_string(sheet.cell_text(0, 6), "$1234.50", "currency symbol, no grouping");
    ctx.equal(core::sheet::format_value_text(Value::of_number(-1234.5), CellKind::Cost,
                                             sheet.format()),
              std::string("-$1234.50"), "negative cost puts the minus first");
    SheetFormat no_decimals = sheet.format();
    no_decimals.cost_decimals = 0;
    ctx.equal(core::sheet::format_value_text(Value::of_number(1234.0), CellKind::Cost,
                                             no_decimals),
              std::string("$1234"), "zero cost decimals");

    /* Every numeric cell honours the configured decimal separator, not just
     * cost. */
    SheetFormat european = sheet.format();
    european.units = core::sheet::UnitLabels{};
    european.currency_symbol.clear();
    european.group_thousands = true;
    european.thousands_separator = '.';
    european.decimal_separator = ',';
    sheet.set_format(european);
    ctx.equal_string(sheet.cell_text(0, 6), "1.234,50", "European cost: dots group, comma decimals");
    ctx.equal(core::sheet::format_duration_text(7200, sheet.format()), std::string("1,5 days"),
              "duration decimals use the setting");
    ctx.equal(core::sheet::format_value_text(Value::of_number(12.5), CellKind::Percent,
                                             sheet.format()),
              std::string("12,5%"), "percent decimals use the setting");
    const core::sheet::EditOutcome european_cost = sheet.parse_text("1.234,50", CellKind::Cost);
    ctx.check(european_cost.ok, "European cost input parses");
    ctx.check(european_cost.ok && std::fabs(european_cost.value.number - 1234.5) < 1e-9,
              "European cost value");

    /* Date setting: order, separator, digits, weekday, time. */
    SheetFormat iso = sheet.format();
    iso.date = DateFormat::iso();
    sheet.set_format(iso);
    ctx.equal_string(sheet.cell_text(0, 3), "2026-10-19", "ISO date setting");
    ctx.equal_string(sheet.cell_text(1, 4), "2026-10-21", "ISO finish");

    SheetFormat mdy = sheet.format();
    mdy.date = DateFormat::short_month_day_year();
    sheet.set_format(mdy);
    ctx.equal_string(sheet.cell_text(1, 3), "Tue 10/20/26", "month/day/year order");

    SheetFormat long_year = sheet.format();
    long_year.date = DateFormat::short_day_month_year();
    long_year.date.two_digit_year = false;
    sheet.set_format(long_year);
    ctx.equal_string(sheet.cell_text(1, 3), "Tue 20/10/2026", "four-digit year");

    SheetFormat with_time = long_year;
    with_time.date.two_digit_year = true;
    with_time.date.show_time_of_day = true;
    sheet.set_format(with_time);
    ctx.equal_string(sheet.cell_text(1, 4), "Wed 21/10/26 17:00", "time of day when set");
    ctx.equal_string(sheet.cell_text(0, 3), "Mon 19/10/26", "midnight shows no time");

    SheetFormat no_weekday = with_time;
    no_weekday.date.show_weekday = false;
    sheet.set_format(no_weekday);
    ctx.equal_string(sheet.cell_text(1, 3), "20/10/26", "weekday optional");

    ctx.equal(core::sheet::format_date_text(DateValue{}, sheet.format()), std::string(""),
              "invalid date renders empty");
}

/* ---- outline ------------------------------------------------------------ */

void test_outline(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    ctx.equal(sheet.outline_level(0), 0, "row 0 level 0");
    ctx.equal(sheet.outline_level(1), 1, "row 1 level 1");
    ctx.equal(sheet.outline_level(2), 1, "row 2 level 1");
    ctx.equal(sheet.outline_level(3), 0, "row 3 level 0");
    ctx.equal(sheet.outline_level(4), 1, "row 4 level 1");
    ctx.equal(sheet.outline_level(5), 0, "row 5 level 0");
    ctx.check(sheet.is_summary(0), "row 0 is a summary (flag)");
    ctx.check(!sheet.is_summary(1), "row 1 is not a summary");
    ctx.check(sheet.is_summary(3), "row 3 is a summary");
    ctx.check(!sheet.is_summary(4), "row 4 is not a summary");
    ctx.check(sheet.has_children(0), "row 0 has children");
    ctx.check(!sheet.has_children(1), "row 1 has no children");
    ctx.check(!sheet.has_children(5), "the last row has no children");

    const std::vector<std::size_t> all_rows = {0, 1, 2, 3, 4, 5};
    ctx.check(sheet.visible_rows() == all_rows, "everything is visible initially");
    ctx.equal(sheet.visible_row_count(), kRowCount, "six visible rows");
    ctx.equal(sheet.visible_row(4), std::size_t(4), "visible ordinal 4 is row 4");
    ctx.equal(sheet.visible_row(kRowCount), kNoIndex, "visible ordinal past the end");
    ctx.equal(sheet.display_row(4), std::size_t(4), "row 4 is at display position 4");
    ctx.check(sheet.row_visible(3), "row 3 visible");

    ctx.check(sheet.collapse(0), "collapse the first summary");
    ctx.check(sheet.is_collapsed(0), "row 0 is collapsed");
    const std::vector<std::size_t> after_first = {0, 3, 4, 5};
    ctx.check(sheet.visible_rows() == after_first, "collapsing row 0 hides its two children");
    ctx.check(!sheet.row_visible(1), "row 1 is hidden");
    ctx.check(!sheet.row_visible(2), "row 2 is hidden");
    ctx.check(sheet.row_visible(0), "the collapsed summary itself stays visible");
    ctx.equal(sheet.display_row(1), kNoIndex, "hidden row has no display position");
    ctx.equal(sheet.display_row(5), std::size_t(3), "row 5 moved to display position 3");
    ctx.equal(sheet.visible_row(2), std::size_t(4), "display position 2 is row 4");
    ctx.equal(sheet.visible_row(3), std::size_t(5), "display position 3 is row 5");
    ctx.equal(sheet.visible_row_count(), std::size_t(4), "four visible rows");

    ctx.check(sheet.collapse(3), "collapse the second summary");
    const std::vector<std::size_t> after_second = {0, 3, 5};
    ctx.check(sheet.visible_rows() == after_second, "both summaries collapsed");
    ctx.check(!sheet.row_visible(4), "row 4 hidden by row 3");

    ctx.check(!sheet.collapse(1), "collapsing a non-summary is refused");
    ctx.check(!sheet.collapse(5), "collapsing a leaf is refused");
    ctx.check(!sheet.collapse(99), "collapsing out of range is refused");
    ctx.check(!sheet.toggle_collapsed(2), "toggling a non-summary is refused");

    ctx.check(sheet.expand(0), "expand row 0");
    const std::vector<std::size_t> after_expand = {0, 1, 2, 3, 5};
    ctx.check(sheet.visible_rows() == after_expand, "row 0's children return, row 3 stays closed");
    ctx.check(sheet.row_visible(1), "row 1 visible again");
    ctx.check(!sheet.row_visible(4), "row 4 still hidden by row 3");

    sheet.collapse_all();
    ctx.check(sheet.visible_rows() == after_second, "collapse all matches the two summaries");
    sheet.expand_all();
    ctx.check(sheet.visible_rows() == all_rows, "expand all restores every row");
    ctx.check(!sheet.is_collapsed(0), "row 0 expanded");

    ctx.check(sheet.toggle_collapsed(0), "toggle collapses row 0");
    ctx.check(sheet.is_collapsed(0), "toggle left it collapsed");
    ctx.check(sheet.toggle_collapsed(0), "toggle again expands");
    ctx.check(!sheet.is_collapsed(0), "toggle left it expanded");

    ctx.equal(sheet.indent_points(0, default_metrics()), 0, "level 0 has no indent");
    ctx.equal(sheet.indent_points(1, default_metrics()), 12, "level 1 indent is 12 points");
    ctx.equal(sheet.indent_points(4, default_metrics()), 12, "level 1 indent again");

    ctx.check(sheet.set_row_height(0, 44), "row height override");
    ctx.equal(sheet.row_height_points(0, default_metrics()), 44, "overridden height");
    ctx.equal(sheet.row_height_points(1, default_metrics()), 20, "default height");
    ctx.check(sheet.set_row_height(0, 0), "clear the row height override");
    ctx.equal(sheet.row_height_points(0, default_metrics()), 20, "height back to the default");
}

/* An outline without OutlineLevel/Summary fields: the per-row override and the
 * structural "next row is deeper" rule must still work. */
void test_outline_override(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    RecordSet bare(core::model::RecordType::Task);
    bare.add_field(fixture.name);
    bare.add_field(fixture.duration);
    const std::size_t row0 = bare.add_row(1);
    const std::size_t row1 = bare.add_row(2);
    const std::size_t row2 = bare.add_row(3);
    bare.set_field(row0, fixture.name, Value::of_text("Parent"));
    bare.set_field(row1, fixture.name, Value::of_text("Child A"));
    bare.set_field(row2, fixture.name, Value::of_text("Child B"));

    std::vector<SheetColumn> columns;
    columns.push_back(make_column(fixture.name));
    SheetModel sheet(bare, std::move(columns));

    ctx.equal(sheet.outline_level(0), 0, "no outline fields means level 0");
    ctx.check(!sheet.is_summary(0), "no outline fields means no summary");
    ctx.check(!sheet.collapse(0), "nothing to collapse yet");

    sheet.set_outline_level(1, 1, false);
    sheet.set_outline_level(2, 1, false);
    sheet.set_outline_level(0, 0, true);
    ctx.equal(sheet.outline_level(1), 1, "override sets level 1");
    ctx.check(sheet.is_summary(0), "override sets the summary flag");
    ctx.check(sheet.has_children(0), "structural child detection");
    ctx.check(sheet.collapse(0), "collapse the overridden summary");
    ctx.check(sheet.visible_rows() == std::vector<std::size_t>({0}), "both children hidden");

    sheet.expand(0);
    sheet.clear_outline_level(0);
    ctx.check(sheet.has_children(0), "still structurally a parent after clearing the flag");
    ctx.check(sheet.is_summary(0), "structure alone makes it a summary again");
    sheet.clear_outline_level(1);
    ctx.equal(sheet.outline_level(1), 0, "cleared level override");
    ctx.check(!sheet.has_children(0), "row 1 is level 0, so row 0 has no children");
    ctx.check(!sheet.is_summary(0), "no longer a summary");
}

/* ---- selection ---------------------------------------------------------- */

void test_selection(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    ctx.check(!sheet.has_selection(), "no selection initially");
    ctx.check(sheet.selected_rows().empty(), "no selected rows initially");

    ctx.check(sheet.select_cell(1, 1), "select B2");
    ctx.check(sheet.has_selection(), "has a selection");
    ctx.equal(sheet.anchor(), CellRef{1, 1}, "anchor is the selected cell");
    ctx.equal(sheet.focus(), CellRef{1, 1}, "focus is the selected cell");
    ctx.check(sheet.is_selected(1, 1), "the cell is selected");
    ctx.check(!sheet.is_selected(2, 1), "the row below is not selected");
    ctx.check(!sheet.is_selected(1, 2), "the column to the right is not selected");
    ctx.equal(sheet.selected_rows(), std::vector<std::size_t>({1}), "one selected row");

    ctx.check(sheet.extend_to(4, 2), "extend to C5");
    ctx.equal(sheet.anchor(), CellRef{1, 1}, "anchor unchanged by extend");
    ctx.equal(sheet.focus(), CellRef{4, 2}, "focus moved");
    ctx.equal(sheet.selected_rows(), std::vector<std::size_t>({1, 2, 3, 4}),
              "a rectangular range selects every visible row in it");
    ctx.check(sheet.is_selected(3, 2), "inside the rectangle");
    ctx.check(!sheet.is_selected(3, 3), "outside the rectangle's columns");
    ctx.check(!sheet.is_selected(5, 1), "outside the rectangle's rows");

    /* Outline collapse clamps the selection to what is visible. */
    ctx.check(sheet.collapse(3), "collapse the Build summary");
    ctx.equal(sheet.selected_rows(), std::vector<std::size_t>({1, 2, 3}),
              "the hidden row leaves the selection");
    ctx.equal(sheet.focus().row, std::size_t(3), "focus clamped to the summary above it");

    ctx.check(sheet.collapse(0), "collapse the Design summary");
    ctx.equal(sheet.selected_rows(), std::vector<std::size_t>({0, 3}),
              "only the visible rows 0 and 3 remain selected");
    ctx.equal(sheet.anchor().row, std::size_t(0), "anchor clamped to the summary");
    ctx.check(!sheet.is_selected(1, 1), "hidden row is never selected");

    ctx.check(!sheet.select_cell(1, 1), "cannot select a hidden row");
    ctx.equal(sheet.anchor().row, std::size_t(0), "failed select left the selection alone");

    sheet.expand(0);
    sheet.expand(3);
    ctx.equal(sheet.selected_rows(), std::vector<std::size_t>({0, 1, 2, 3}),
              "expanding restores the visible rows inside the rectangle");
    ctx.equal(sheet.visible_row_count(), kRowCount, "all rows visible again");

    sheet.select_all();
    ctx.equal(sheet.anchor(), CellRef{0, 0}, "select all anchors at A1");
    ctx.equal(sheet.focus(), CellRef{kRowCount - 1, kColumnCount - 1},
              "select all focuses the last visible cell");
    ctx.equal(sheet.selected_rows(), std::vector<std::size_t>({0, 1, 2, 3, 4, 5}),
              "select all selects every visible row");
    ctx.check(sheet.is_selected(5, 8), "the last cell is selected");
    ctx.check(!sheet.is_selected(5, kColumnCount), "past the last column is not selected");

    ctx.check(sheet.set_column_visible(1, false), "hide the Name column");
    ctx.check(!sheet.is_selected(0, 1), "a hidden column is not selected");
    ctx.check(!sheet.select_cell(0, 1), "cannot select a hidden column");
    ctx.check(sheet.is_selected(0, 0), "the rest of the selection survives");

    sheet.select_all();
    ctx.equal(sheet.selected_rows().size(), kRowCount, "select all still selects all rows");
    ctx.equal(sheet.focus().column, std::size_t(8), "select all's focus is the last visible column");
    ctx.check(!sheet.is_selected(0, 1), "hidden column excluded from select all");

    sheet.collapse(0);
    ctx.equal(sheet.selected_rows(), std::vector<std::size_t>({0, 3, 4, 5}),
              "select all then collapse keeps only the visible rows");

    sheet.clear_selection();
    ctx.check(!sheet.has_selection(), "clear_selection clears");
    ctx.check(sheet.selected_rows().empty(), "no rows after clear");
    ctx.check(!sheet.is_selected(0, 0), "nothing selected after clear");

    sheet.expand(0);
    ctx.check(sheet.extend_to(2, 2), "extend with no selection starts one");
    ctx.equal(sheet.anchor(), CellRef{2, 2}, "extend from nothing anchors on the cell");
    ctx.equal(sheet.focus(), CellRef{2, 2}, "and focuses it");
}

/* ---- editing ------------------------------------------------------------ */

void test_editing(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    ctx.check(!sheet.editing(), "not editing initially");
    ctx.check(sheet.begin_edit(2, 2), "edit the 4-hour duration");
    ctx.check(sheet.editing(), "an edit is open");
    ctx.equal(sheet.edit_target(), CellRef{2, 2}, "edit target is the duration cell");
    ctx.equal_string(sheet.edit_text(), "4 hrs", "edit text is seeded with the formatted value");
    sheet.cancel_edit();
    ctx.check(!sheet.editing(), "cancel closes the edit");

    ctx.check(sheet.begin_edit(2, 2), "reopen the duration edit");
    sheet.set_edit_text("3d");
    const core::sheet::EditOutcome preview = sheet.validate_edit();
    ctx.check(preview.ok, "3d validates");
    ctx.check(!preview.changed, "validation never reports a change");
    ctx.equal(preview.value.kind, ValueKind::Duration, "3d parses to a duration");
    ctx.equal(preview.value.integer, std::int64_t(14400), "3d is 14,400 ticks at 8 h/day");
    ctx.check(sheet.editing(), "validation leaves the edit open");

    const core::sheet::EditOutcome committed = sheet.commit_edit();
    ctx.check(committed.ok, "commit succeeds");
    ctx.check(committed.changed, "commit reports a change");
    ctx.check(!sheet.editing(), "the edit closes on commit");
    ctx.equal_string(sheet.cell_text(2, 2), "3 days", "the cell re-renders from the new value");
    const Value* stored = fixture.records.get(2, fixture.duration);
    ctx.check(stored != nullptr, "the RecordSet has the Duration field");
    ctx.check(stored != nullptr && stored->kind == ValueKind::Duration, "stored as a duration");
    ctx.check(stored != nullptr && stored->integer == 14400, "stored value is 14,400 ticks");

    /* A commit with the same value records nothing. */
    const std::size_t depth = sheet.undo_depth();
    ctx.check(sheet.begin_edit(0, 1), "edit the name");
    ctx.equal_string(sheet.edit_text(), "Design", "name edit seeded");
    const core::sheet::EditOutcome same = sheet.commit_edit();
    ctx.check(same.ok, "unchanged commit succeeds");
    ctx.check(!same.changed, "unchanged commit reports no change");
    ctx.equal(sheet.undo_depth(), depth, "unchanged commit records no undo entry");

    /* Dates. */
    ctx.check(sheet.begin_edit(0, 3), "edit the start date");
    sheet.set_edit_text("22/10/26");
    const core::sheet::EditOutcome date_commit = sheet.commit_edit();
    ctx.check(date_commit.ok, "date commit succeeds");
    ctx.equal(date_commit.value.kind, ValueKind::Date, "parsed as a date");
    ctx.check(date_commit.value.date == date_of(2026, 10, 22), "22/10/26 is 2026-10-22");
    ctx.equal_string(sheet.cell_text(0, 3), "Thu 22/10/26", "date re-rendered");

    /* Percent. */
    ctx.check(sheet.begin_edit(1, 5), "edit the percent column");
    sheet.set_edit_text("75%");
    const core::sheet::EditOutcome percent_commit = sheet.commit_edit();
    ctx.check(percent_commit.ok, "percent commit succeeds");
    ctx.equal(percent_commit.value.kind, ValueKind::Integer, "whole percent is an integer");
    ctx.equal(percent_commit.value.integer, std::int64_t(75), "75 percent");
    ctx.equal_string(sheet.cell_text(1, 5), "75%", "percent re-rendered");

    /* Cost. */
    ctx.check(sheet.begin_edit(2, 6), "edit the cost");
    sheet.set_edit_text("1,234.5");
    const core::sheet::EditOutcome cost_commit = sheet.commit_edit();
    ctx.check(cost_commit.ok, "cost commit succeeds");
    ctx.equal(cost_commit.value.kind, ValueKind::Number, "cost parses to a number");
    ctx.check(std::fabs(cost_commit.value.number - 1234.5) < 1e-9, "cost value 1234.5");
    ctx.equal_string(sheet.cell_text(2, 6), "1,234.50", "cost re-rendered with grouping");

    /* Bool. */
    ctx.check(sheet.begin_edit(4, 7), "edit the summary flag");
    ctx.equal_string(sheet.edit_text(), "No", "bool edit seeded with No");
    sheet.set_edit_text("yes");
    const core::sheet::EditOutcome bool_commit = sheet.commit_edit();
    ctx.check(bool_commit.ok, "bool commit succeeds");
    ctx.check(bool_commit.value.kind == ValueKind::Bool && bool_commit.value.boolean,
              "yes parses to true");
    ctx.equal_string(sheet.cell_text(4, 7), "Yes", "bool re-rendered");

    /* Text: trimmed, and clearing is meaningful. */
    ctx.check(sheet.begin_edit(2, 8), "edit the notes");
    sheet.set_edit_text("   tightened scope   ");
    const core::sheet::EditOutcome text_commit = sheet.commit_edit();
    ctx.check(text_commit.ok, "text commit succeeds");
    ctx.equal(text_commit.value.text, std::string("tightened scope"), "text is trimmed");
    ctx.equal_string(sheet.cell_text(2, 8), "tightened scope", "text re-rendered");

    /* Integer. */
    ctx.check(sheet.begin_edit(5, 0), "edit the ID");
    sheet.set_edit_text("42");
    const core::sheet::EditOutcome id_commit = sheet.commit_edit();
    ctx.check(id_commit.ok, "integer commit succeeds");
    ctx.equal(id_commit.value.integer, std::int64_t(42), "ID is 42");
    ctx.equal_string(sheet.cell_text(5, 0), "42", "ID re-rendered");

    /* Clearing a non-text cell writes Null. */
    ctx.check(sheet.begin_edit(3, 2), "edit a duration to clear it");
    sheet.set_edit_text("");
    const core::sheet::EditOutcome clear_commit = sheet.commit_edit();
    ctx.check(clear_commit.ok, "clearing succeeds");
    ctx.check(clear_commit.value.is_null(), "clearing produces Null");
    ctx.equal_string(sheet.cell_text(3, 2), "", "the cleared cell renders empty");

    /* Cancel discards the typed text and writes nothing. */
    ctx.check(sheet.begin_edit(1, 1), "edit a name to cancel");
    sheet.set_edit_text("Scratch");
    sheet.cancel_edit();
    ctx.check(!sheet.editing(), "cancel closes the edit");
    ctx.equal_string(sheet.cell_text(1, 1), "Draft", "cancel wrote nothing");
    ctx.equal(sheet.undo_depth(), std::size_t(8),
              "only the eight changed commits recorded undo entries");

    /* Editing addresses visible cells only. */
    ctx.check(sheet.collapse(0), "collapse Design");
    ctx.check(!sheet.begin_edit(1, 1), "cannot edit a hidden row");
    ctx.check(sheet.set_column_visible(1, false), "hide Name");
    ctx.check(!sheet.begin_edit(0, 1), "cannot edit a hidden column");
    ctx.check(sheet.begin_edit(0, 0), "an unrelated visible cell still opens");
    sheet.cancel_edit();
}

void test_editing_errors(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    ctx.check(sheet.begin_edit(2, 2), "edit the duration");
    sheet.set_edit_text("abc");
    const core::sheet::EditOutcome bad_duration = sheet.commit_edit();
    ctx.check(!bad_duration.ok, "abc is not a duration");
    ctx.contains(bad_duration.error, "duration", "duration error names the kind");
    ctx.check(sheet.editing(), "a rejected commit keeps the edit open");
    ctx.equal_string(sheet.edit_text(), "abc", "the typed text is preserved for correction");
    ctx.check(!sheet.commit_edit().ok, "committing the same bad text fails again");
    ctx.equal_string(sheet.cell_text(2, 2), "4 hrs", "nothing was written");
    sheet.cancel_edit();

    ctx.check(!sheet.parse_text("3x", CellKind::Duration).ok, "unknown unit rejected");
    ctx.contains(sheet.parse_text("3x", CellKind::Duration).error, "unit",
                 "unknown-unit error mentions the unit");
    const core::sheet::EditOutcome negative = sheet.parse_text("-3d", CellKind::Duration);
    ctx.check(!negative.ok, "negative duration rejected");
    ctx.contains(negative.error, "negative", "negative-duration error says so");
    ctx.check(sheet.parse_text("0d", CellKind::Duration).ok, "zero duration is legal");
    ctx.equal(sheet.parse_text("2w", CellKind::Duration).value.integer, std::int64_t(48000),
              "2 weeks is ten 8-hour days");
    ctx.equal(sheet.parse_text("1mo", CellKind::Duration).value.integer, std::int64_t(96000),
              "1 month is twenty 8-hour days");
    ctx.equal(sheet.parse_text("3", CellKind::Duration).value.integer, std::int64_t(14400),
              "a bare number in a duration is days");
    ctx.check(sheet.parse_text("3 days", CellKind::Duration).ok, "long day suffix accepted");

    const core::sheet::EditOutcome bad_date = sheet.parse_text("32/13/26", CellKind::Date);
    ctx.check(!bad_date.ok, "32/13/26 is not a date");
    ctx.contains(bad_date.error, "month", "the month is named as out of range");
    ctx.check(!sheet.parse_text("2026-02-30", CellKind::Date).ok, "30 February rejected");
    ctx.check(!sheet.parse_text("not a date", CellKind::Date).ok, "words are not a date");
    const core::sheet::EditOutcome iso_date = sheet.parse_text("2026-10-20", CellKind::Date);
    ctx.check(iso_date.ok, "ISO date accepted whatever the setting");
    ctx.check(iso_date.value.date == date_of(2026, 10, 20), "ISO date value");
    ctx.check(sheet.parse_text("20/10/2026", CellKind::Date).ok, "four-digit year accepted");
    ctx.check(sheet.parse_text("20/10/26 17:30", CellKind::Date).ok, "date with a time accepted");
    ctx.equal(sheet.parse_text("20/10/26 17:30", CellKind::Date).value.date.hour,
              std::uint8_t(17), "the time is stored");

    ctx.check(!sheet.parse_text("120", CellKind::Percent).ok, "120 percent rejected");
    ctx.check(!sheet.parse_text("-5%", CellKind::Percent).ok, "negative percent rejected");
    ctx.check(sheet.parse_text("0%", CellKind::Percent).ok, "0 percent legal");
    ctx.check(sheet.parse_text("100", CellKind::Percent).ok, "100 percent legal");

    ctx.check(!sheet.parse_text("abc", CellKind::Integer).ok, "words are not an integer");
    ctx.check(!sheet.parse_text("3.5", CellKind::Integer).ok, "a fraction is not an integer");
    ctx.check(sheet.parse_text("-7", CellKind::Integer).ok, "negative integers are legal");
    ctx.equal(sheet.parse_text("1,234", CellKind::Integer).value.integer, std::int64_t(1234),
              "thousands grouping accepted in an integer");

    ctx.check(!sheet.parse_text("maybe", CellKind::Bool).ok, "maybe is not a bool");
    ctx.check(sheet.parse_text("FALSE", CellKind::Bool).ok, "FALSE is accepted");
    ctx.check(!sheet.parse_text("1.2.3", CellKind::Cost).ok, "two decimal points rejected");

    /* No edit in progress. */
    const core::sheet::EditOutcome no_edit = sheet.commit_edit();
    ctx.check(!no_edit.ok, "commit with no edit fails");
    ctx.contains(no_edit.error, "no edit", "the error explains there is no edit");
    ctx.check(!sheet.validate_edit().ok, "validate with no edit fails");

    /* Auto has no cell to resolve against and parses as text. */
    const core::sheet::EditOutcome auto_value =
        core::sheet::parse_edit_text("plain", CellKind::Auto, sheet.format());
    ctx.check(auto_value.ok, "Auto parses");
    ctx.equal(auto_value.value.kind, ValueKind::Text, "Auto is text");
}

/* ---- undo / redo -------------------------------------------------------- */

void test_undo_redo(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    ctx.check(!sheet.can_undo(), "nothing to undo initially");
    ctx.check(!sheet.can_redo(), "nothing to redo initially");
    ctx.check(!sheet.undo(), "undo on an empty stack returns false");
    ctx.check(!sheet.redo(), "redo on an empty stack returns false");
    ctx.check(sheet.next_undo() == nullptr, "no next undo entry");

    ctx.check(sheet.begin_edit(2, 2), "edit the duration");
    sheet.set_edit_text("3d");
    ctx.check(sheet.commit_edit().ok, "commit 3d");
    ctx.equal(sheet.undo_depth(), std::size_t(1), "one undo entry");
    ctx.equal(sheet.redo_depth(), std::size_t(0), "no redo entries");

    const UndoEntry* entry = sheet.next_undo();
    ctx.check(entry != nullptr, "next undo entry exists");
    ctx.check(entry != nullptr && entry->row == std::size_t(2), "undo row is 2");
    ctx.check(entry != nullptr && entry->field == fixture.duration, "undo field is Duration");
    ctx.check(entry != nullptr && entry->before.kind == ValueKind::Duration &&
                  entry->before.integer == 2400,
              "undo remembers the previous value");
    ctx.check(entry != nullptr && entry->after.kind == ValueKind::Duration &&
                  entry->after.integer == 14400,
              "undo remembers the new value");

    ctx.check(sheet.undo(), "undo the duration edit");
    ctx.equal_string(sheet.cell_text(2, 2), "4 hrs", "the previous value is rendered");
    const Value* restored = fixture.records.get(2, fixture.duration);
    ctx.check(restored != nullptr && restored->integer == 2400, "the RecordSet is restored");
    ctx.equal(sheet.undo_depth(), std::size_t(0), "undo stack empty");
    ctx.equal(sheet.redo_depth(), std::size_t(1), "redo has the entry");
    ctx.check(sheet.next_redo() != nullptr, "next redo entry exists");

    ctx.check(sheet.redo(), "redo the duration edit");
    ctx.equal_string(sheet.cell_text(2, 2), "3 days", "redo restores the new value");
    ctx.check(fixture.records.get(2, fixture.duration) != nullptr &&
                  fixture.records.get(2, fixture.duration)->integer == 14400,
              "redo wrote through");
    ctx.equal(sheet.undo_depth(), std::size_t(1), "undo stack back to one");

    /* LIFO order across two edits on different cells. */
    ctx.check(sheet.begin_edit(0, 1), "edit the name");
    sheet.set_edit_text("Outline");
    ctx.check(sheet.commit_edit().ok, "commit the name");
    ctx.equal(sheet.undo_depth(), std::size_t(2), "two undo entries");
    ctx.check(sheet.next_undo() != nullptr && sheet.next_undo()->field == fixture.name,
              "the newest entry is the name");

    ctx.check(sheet.undo(), "undo the name");
    ctx.equal_string(sheet.cell_text(0, 1), "Design", "the name is back");
    ctx.equal_string(sheet.cell_text(2, 2), "3 days", "the duration edit is untouched");
    ctx.check(sheet.undo(), "undo the duration again");
    ctx.equal_string(sheet.cell_text(2, 2), "4 hrs", "the duration is back");
    ctx.equal(sheet.undo_depth(), std::size_t(0), "everything undone");

    ctx.check(sheet.redo(), "redo the duration");
    ctx.check(sheet.redo(), "redo the name");
    ctx.equal_string(sheet.cell_text(0, 1), "Outline", "both redone");

    /* A new edit clears the redo stack. */
    ctx.check(sheet.undo(), "undo the name to make a redo available");
    ctx.equal(sheet.redo_depth(), std::size_t(1), "one redo available");
    ctx.check(sheet.begin_edit(5, 8), "start a different edit");
    sheet.set_edit_text("closed");
    ctx.check(sheet.commit_edit().ok, "commit it");
    ctx.equal(sheet.redo_depth(), std::size_t(0), "the new edit cleared redo");

    sheet.clear_history();
    ctx.check(!sheet.can_undo(), "clear_history empties undo");
    ctx.check(!sheet.can_redo(), "clear_history empties redo");
}

/* A null cell: undo has to restore the absence, not an empty string. */
void test_undo_null(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    RecordSet records(core::model::RecordType::Task);
    records.add_field(fixture.duration);
    const std::size_t row0 = records.add_row(1);
    const std::size_t row1 = records.add_row(2);
    records.set_field(row0, fixture.duration, Value::of_duration(4800));

    std::vector<SheetColumn> columns;
    columns.push_back(make_column(fixture.duration));
    SheetModel sheet(records, std::move(columns));

    ctx.check(records.get(row1, fixture.duration) == nullptr ||
                  records.get(row1, fixture.duration)->is_null(),
              "row 1 starts without a duration");
    ctx.equal(sheet.cell_kind(row1, 0), CellKind::Duration,
              "an empty cell takes the column's resolved duration kind");
    ctx.equal_string(sheet.cell_text(row1, 0), "", "the empty cell renders empty");

    ctx.check(sheet.begin_edit(row1, 0), "edit the empty duration");
    ctx.equal_string(sheet.edit_text(), "", "the editor starts empty");
    sheet.set_edit_text("2d");
    ctx.check(sheet.commit_edit().ok, "2d commits");
    ctx.equal_string(sheet.cell_text(row1, 0), "2 days", "the cell now has a duration");

    ctx.check(sheet.undo(), "undo the fill-in");
    const Value* value = records.get(row1, fixture.duration);
    ctx.check(value != nullptr && value->is_null(), "undo restores Null, not empty text");
    ctx.equal_string(sheet.cell_text(row1, 0), "", "the cell renders empty again");
    ctx.check(sheet.redo(), "redo the fill-in");
    ctx.equal_string(sheet.cell_text(row1, 0), "2 days", "redo works on a null before-value");
}

/* ---- geometry ----------------------------------------------------------- */

void test_geometry(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());
    const SheetMetrics metrics = default_metrics();

    /* Default rows: 6 rows at 20 points plus a 24-point header. */
    ctx.equal(sheet.content_height_points(metrics), 144, "header plus six 20-point rows");
    ctx.equal(sheet.row_top_points(0, metrics), 24, "the first row starts below the header");
    ctx.equal(sheet.row_top_points(1, metrics), 44, "the second row is one row lower");
    ctx.equal(sheet.row_top_points(5, metrics), 124, "the last row's top");
    ctx.equal(sheet.row_top_points(6, metrics), -1, "past the last visible row");
    ctx.equal(sheet.row_top_points(kNoIndex, metrics), -1, "no ordinal is not laid out");

    ctx.check(sheet.set_row_height(0, 44), "make row 0 taller");
    ctx.equal(sheet.content_height_points(metrics), 168, "the taller row adds 24 points");
    ctx.equal(sheet.row_top_points(1, metrics), 68, "row 1 moved down by the extra height");
    ctx.equal(sheet.row_top_points(0, metrics), 24, "the first row still starts at the header");

    ctx.check(sheet.collapse(0), "collapse row 0");
    ctx.equal(sheet.content_height_points(metrics), 128,
              "four visible rows plus the 44-point row");
    ctx.equal(sheet.row_top_points(1, metrics), 68, "display row 1 is now row 3");
    sheet.expand(0);

    /* Column layout: explicit widths become points, auto columns take the
     * shell's measurement. */
    ctx.check(sheet.set_column_width(0, 6), "ID 6 characters");
    ctx.check(sheet.set_column_width(5, 4), "percent 4 characters");
    const std::vector<ColumnSegment> segments = sheet.layout_columns(metrics, 100);
    ctx.equal(segments.size(), kColumnCount, "one segment per visible column");
    ctx.equal(segments[0].column, std::size_t(0), "first segment is ID");
    ctx.equal(segments[0].x, 0, "first segment at x 0");
    ctx.equal(segments[0].width, 42, "ID is 42 points");
    ctx.equal(segments[1].x, 42, "Name starts after ID");
    ctx.equal(segments[1].width, 100, "auto column takes the given width");
    ctx.equal(segments[5].width, 28, "four characters of percent");
    ctx.equal(sheet.sheet_width_points(metrics, 100), 42 + 100 * 7 + 28,
              "total width is explicit plus auto columns");
    ctx.equal(sheet.layout_columns(metrics, 0)[1].width, 0, "auto width 0 means zero");

    SheetMetrics floored = metrics;
    floored.min_column_width = 50;
    ctx.equal(sheet.column_width_points(0, floored), 50,
              "min_column_width floors explicit widths");
    ctx.equal(sheet.column_width_points(1, floored), 0, "auto columns are not floored");

    ctx.check(sheet.set_column_visible(3, false), "hide Start");
    const std::vector<ColumnSegment> fewer = sheet.layout_columns(metrics, 100);
    ctx.equal(fewer.size(), kColumnCount - 1, "hidden columns are not laid out");
    ctx.equal(fewer[3].column, std::size_t(4), "the segment after the gap is column 4");
    ctx.equal(fewer[3].x, 242, "x skips the hidden column");

    ctx.equal(sheet.column_width_points(3, metrics), 0, "a hidden column has no width");
    ctx.equal(sheet.column_width_points(kColumnCount, metrics), 0, "bad column width 0");
}

/* ---- a configured table ------------------------------------------------ */

void test_table_definition(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    TableDefinition table;
    table.id = "test";
    table.name = "Test";
    table.record_type = core::model::RecordType::Task;
    core::view::Column id_column;
    id_column.field = fixture.id;
    id_column.title = "Task ID";
    id_column.width = 6;
    id_column.align = ColumnAlign::Right;
    table.columns.push_back(id_column);
    core::view::Column name_column;
    name_column.field = fixture.name;
    name_column.width = 20;
    name_column.align = ColumnAlign::Center;
    table.columns.push_back(name_column);
    core::view::Column duration_column;
    duration_column.field = fixture.duration;
    table.columns.push_back(duration_column);

    SheetModel sheet(fixture.records, table);
    ctx.equal(sheet.column_count(), std::size_t(3), "the table's three columns");
    ctx.equal_string(sheet.column_title(0), "Task ID", "authored title from the table");
    ctx.equal_string(sheet.column_title(1), "Name", "derived title from the descriptor table");
    ctx.equal(sheet.column(0).width, 6, "width carried from the table");
    ctx.equal(sheet.column_width_points(0, default_metrics()), 42, "width in points");
    ctx.equal(sheet.cell_style(0, 0).align, CellAlign::Right, "explicit right alignment");
    ctx.equal(sheet.cell_style(0, 1).align, CellAlign::Center, "explicit centre alignment");
    ctx.equal(sheet.cell_style(0, 2).align, CellAlign::Right, "auto alignment by kind");
    ctx.equal_string(sheet.cell_text(0, 2), "5 days", "table columns format like any other");

    /* Replacing the columns keeps the records and drops the old schema. */
    std::vector<SheetColumn> replacement;
    replacement.push_back(make_column(fixture.name));
    sheet.set_columns(std::move(replacement));
    ctx.equal(sheet.column_count(), std::size_t(1), "columns replaced");
    ctx.equal_string(sheet.column_title(0), "Name", "new column title");
    ctx.equal(sheet.row_count(), kRowCount, "rows are unaffected");

    /* The configured Entry table (inferred, from core/view/defaults.h) is
     * accepted as-is: the sheet does not reinterpret it. */
    Fixture second(ctx);
    const TableDefinition& entry_table = core::view::defaults::inferred_entry();
    SheetModel entry(second.records, entry_table);
    ctx.equal(entry.column_count(), entry_table.columns.size(), "the Entry table's column count");
    ctx.equal(entry.visible_column_count(), entry.column_count(), "all Entry columns visible");
    bool titles_present = true;
    for (std::size_t i = 0; i < entry.column_count(); ++i) {
        if (entry.column_title(i).empty()) {
            titles_present = false;
        }
    }
    ctx.check(titles_present, "every Entry column has a title (name or hex id)");
    ctx.equal_string(entry.cell_text(0, 0), "1", "Entry's first column (ID) formats");
    ctx.equal_string(entry.cell_text(0, 2), "5 days", "Entry's Duration column formats");
}

/* ---- align/bold/red hints ---------------------------------------------- */

void test_style(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    ctx.equal(sheet.cell_style(0, 2).align, CellAlign::Right, "durations align right");
    ctx.equal(sheet.cell_style(0, 5).align, CellAlign::Right, "percentages align right");
    ctx.equal(sheet.cell_style(0, 6).align, CellAlign::Right, "costs align right");
    ctx.equal(sheet.cell_style(0, 0).align, CellAlign::Right, "integers align right");
    ctx.equal(sheet.cell_style(0, 1).align, CellAlign::Left, "text aligns left");
    ctx.equal(sheet.cell_style(0, 3).align, CellAlign::Left, "dates align left");
    ctx.equal(sheet.cell_style(0, 7).align, CellAlign::Left, "flags align left");

    ctx.check(sheet.cell_style(0, 1).bold, "summary rows are bold");
    ctx.check(sheet.cell_style(3, 0).bold, "the second summary is bold");
    ctx.check(!sheet.cell_style(1, 1).bold, "child rows are not bold");

    ctx.check(!sheet.is_critical(1), "row 1 is not critical");
    ctx.check(sheet.is_critical(4), "row 4 is critical");
    ctx.check(sheet.cell_style(4, 1).red, "the critical row is red");
    ctx.check(!sheet.cell_style(1, 1).red, "no status date means no overdue red");

    sheet.set_status_date(date_of(2026, 10, 22));
    ctx.check(sheet.has_status_date(), "status date set");
    ctx.check(sheet.is_overdue(1), "row 1 finished before the status date and is incomplete");
    ctx.check(!sheet.is_overdue(0), "row 0 is 100% complete, so not overdue");
    ctx.check(sheet.cell_style(1, 1).red, "the overdue row is red");
    ctx.check(!sheet.cell_style(3, 1).red, "row 3 finishes after the status date");

    sheet.set_status_date(DateValue{});
    ctx.check(!sheet.has_status_date(), "an invalid status date clears it");
    ctx.check(!sheet.is_overdue(1), "no status date, no overdue");

    sheet.clear_status_date();
    ctx.check(!sheet.has_status_date(), "clear_status_date clears");

    /* An explicit column kind beats the per-value kind for formatting. */
    ctx.check(sheet.set_column_kind(1, CellKind::Text), "declare the name column text");
    ctx.equal(sheet.cell_kind(0, 1), CellKind::Text, "declared text kind");
    ctx.equal(sheet.cell_style(0, 0).align, CellAlign::Right, "unrelated columns unchanged");
}

/* ---- selection ranges and the clipboard -------------------------------- */

void test_selection_range(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    ctx.check(!sheet.selection_range().valid, "no range without a selection");
    ctx.equal(sheet.selection_range().row_count(), std::size_t(0), "an empty range has no rows");
    ctx.equal(sheet.selection_range().column_count(), std::size_t(0),
              "an empty range has no columns");

    ctx.check(sheet.select_cell(2, 2), "select C3");
    CellRange range = sheet.selection_range();
    ctx.check(range.valid, "one cell is a valid range");
    ctx.equal(range.row_begin, std::size_t(2), "range row begin");
    ctx.equal(range.row_end, std::size_t(2), "range row end");
    ctx.equal(range.column_begin, std::size_t(2), "range column begin");
    ctx.equal(range.column_end, std::size_t(2), "range column end");
    ctx.equal(range.row_count(), std::size_t(1), "one row in the range");
    ctx.check(range.contains(2, 2), "the cell is inside the range");
    ctx.check(!range.contains(1, 2), "a neighbouring row is outside");

    ctx.check(sheet.extend_to(4, 4), "extend to E5");
    range = sheet.selection_range();
    ctx.equal(range.row_count(), std::size_t(3), "three rows");
    ctx.equal(range.column_count(), std::size_t(3), "three columns");
    ctx.check(range.contains(3, 3), "the middle is inside");
    ctx.check(!range.contains(5, 3), "a row past the end is outside");

    const std::vector<CellRef> cells = sheet.selected_cells();
    ctx.equal(cells.size(), std::size_t(9), "the 3x3 block has nine visible cells");
    ctx.check(cells.front() == CellRef{2, 2}, "selected_cells is row-major from the top-left");
    ctx.check(cells.back() == CellRef{4, 4}, "selected_cells ends at the bottom-right");

    ctx.check(sheet.select_range(1, 3, 1, 3), "select B2:D4 explicitly");
    ctx.check(sheet.is_selected(2, 2), "the explicit range covers C3");
    ctx.check(!sheet.is_selected(0, 0), "the explicit range excludes A1");
    ctx.check(sheet.select_range(4, 2, 4, 2), "reversed endpoints are accepted");
    range = sheet.selection_range();
    ctx.equal(range.row_begin, std::size_t(2), "reversed rows normalize to the top");
    ctx.equal(range.row_end, std::size_t(4), "reversed rows normalize to the bottom");
    ctx.equal(range.column_begin, std::size_t(2), "reversed columns normalize to the left");

    /* A collapsed summary hides row 4.  A range ending on it clamps to the
     * nearest visible row (the summary itself) and the hidden row is never
     * selected or copied. */
    ctx.check(sheet.collapse(3), "collapse Build");
    ctx.check(sheet.select_range(3, 4, 1, 2), "select the summary and its hidden child");
    range = sheet.selection_range();
    ctx.equal(range.row_begin, std::size_t(3), "range starts at the summary");
    ctx.equal(range.row_end, std::size_t(3), "the hidden child clamps onto the summary");
    ctx.check(!sheet.is_selected(4, 1), "a hidden row is never selected");
    ClipboardBlock block = sheet.copy_selection();
    ctx.equal(block.rows, std::size_t(1), "copy skips the hidden row");
    ctx.equal(block.columns, std::size_t(2), "copy keeps both visible columns");
    ctx.equal(block.cells.size(), std::size_t(2), "one visible row, two visible columns");
    ctx.check(block.at(0, 0) != nullptr && block.at(0, 0)->field == fixture.name,
              "the first copied cell is Name");
    sheet.expand(3);

    /* A hidden column is never copied and closes the gap in the block. */
    ctx.check(sheet.set_column_visible(2, false), "hide Duration");
    ctx.check(sheet.select_range(3, 4, 1, 3), "select across the hidden Duration column");
    block = sheet.copy_selection();
    ctx.equal(block.rows, std::size_t(2), "both visible rows are copied");
    ctx.equal(block.columns, std::size_t(2), "the hidden column contributes nothing");
    ctx.equal(block.cells.size(), std::size_t(4), "2x2 visible cells");
    ctx.check(block.at(0, 0) != nullptr && block.at(0, 0)->field == fixture.name,
              "block column 0 is Name");
    ctx.check(block.at(0, 1) != nullptr && block.at(0, 1)->field == fixture.start,
              "block column 1 is Start, not Duration");
    ctx.equal(block.at(1, 1) != nullptr ? block.at(1, 1)->kind : CellKind::Auto, CellKind::Date,
              "the copied Start cell keeps its date kind");
    const std::vector<CellRef> visible_cells = sheet.selected_cells();
    ctx.equal(visible_cells.size(), std::size_t(4), "selected_cells skips the hidden column");
    ctx.check(!sheet.is_selected(3, 2), "the hidden cell is not selected");

    ctx.check(sheet.select_range(0, 99, 0, 99), "an over-large range clamps");
    range = sheet.selection_range();
    ctx.equal(range.row_end, std::size_t(5), "it ends at the last visible row");
    ctx.equal(range.column_end, std::size_t(8), "it ends at the last visible column");

    sheet.clear_selection();
    ctx.check(sheet.copy_selection().empty(), "copy without a selection is empty");
}

void test_clipboard_text(view_test::TestContext& ctx) {
    ClipboardBlock block;
    block.rows = 2;
    block.columns = 2;
    ClipboardCell plain;
    plain.row_offset = 0;
    plain.column_offset = 0;
    plain.text = "plain";
    plain.kind = CellKind::Text;
    plain.value = Value::of_text(plain.text);
    block.cells.push_back(plain);

    ClipboardCell tabbed;
    tabbed.row_offset = 0;
    tabbed.column_offset = 1;
    tabbed.text = "tab\there";
    tabbed.kind = CellKind::Text;
    tabbed.value = Value::of_text(tabbed.text);
    block.cells.push_back(tabbed);

    ClipboardCell multiline;
    multiline.row_offset = 1;
    multiline.column_offset = 0;
    multiline.text = "line1\nline2";
    multiline.kind = CellKind::Text;
    multiline.value = Value::of_text(multiline.text);
    block.cells.push_back(multiline);

    ClipboardCell quoted;
    quoted.row_offset = 1;
    quoted.column_offset = 1;
    quoted.text = "say \"hi\"";
    quoted.kind = CellKind::Text;
    quoted.value = Value::of_text(quoted.text);
    block.cells.push_back(quoted);

    const std::string text = block.to_text();
    ctx.equal_string(text, "plain\t\"tab\there\"\n\"line1\nline2\"\t\"say \"\"hi\"\"\"\n",
                     "TSV quotes tabs, newlines and quotes");
    const ClipboardBlock parsed = ClipboardBlock::from_text(text);
    ctx.equal(parsed.rows, std::size_t(2), "round trip keeps two rows");
    ctx.equal(parsed.columns, std::size_t(2), "round trip keeps two columns");
    ctx.check(parsed.at(0, 0) != nullptr && parsed.at(0, 0)->text == "plain", "plain field");
    ctx.check(parsed.at(0, 1) != nullptr && parsed.at(0, 1)->text == "tab\there",
              "a quoted tab survives");
    ctx.check(parsed.at(1, 0) != nullptr && parsed.at(1, 0)->text == "line1\nline2",
              "a quoted newline survives");
    ctx.check(parsed.at(1, 1) != nullptr && parsed.at(1, 1)->text == "say \"hi\"",
              "a doubled quote survives");
    ctx.equal(parsed.at(0, 0) != nullptr ? parsed.at(0, 0)->kind : CellKind::Auto,
              CellKind::Text, "parsed cells are text");
    ctx.check(parsed.at(0, 0) != nullptr && parsed.at(0, 0)->value.kind == ValueKind::Text,
              "parsed cells carry a Text value");

    const ClipboardBlock crlf = ClipboardBlock::from_text("1\t2\r\n3\t4\r\n");
    ctx.equal(crlf.rows, std::size_t(2), "CRLF terminates two rows");
    ctx.equal(crlf.columns, std::size_t(2), "CRLF keeps two columns");
    ctx.check(crlf.at(1, 1) != nullptr && crlf.at(1, 1)->text == "4", "the last field of row 2");
    ctx.equal(ClipboardBlock::from_text("solo").rows, std::size_t(1), "one field, one row");
    ctx.check(ClipboardBlock::from_text("").empty(), "empty text is an empty block");
    const ClipboardBlock quoted_empty = ClipboardBlock::from_text("\"\"\tx\n");
    ctx.equal(quoted_empty.rows, std::size_t(1), "a quoted empty field still makes a row");
    ctx.check(quoted_empty.at(0, 0) != nullptr && quoted_empty.at(0, 0)->text.empty(),
              "the quoted empty field is empty");
    ctx.check(quoted_empty.at(0, 1) != nullptr && quoted_empty.at(0, 1)->text == "x",
              "the field after it is intact");

    const ClipboardBlock uneven = ClipboardBlock::from_text("a\tb\nc");
    ctx.equal(uneven.rows, std::size_t(2), "uneven rows parse as two rows");
    ctx.equal(uneven.columns, std::size_t(2), "the widest row sets the column count");
    ctx.check(uneven.at(1, 1) == nullptr, "the missing cell is absent");
    ctx.equal(uneven.cells.size(), std::size_t(3), "only the supplied cells are stored");
    ctx.check(block.at(2, 0) == nullptr, "at() outside the block is null");
}

/* ---- transactions ------------------------------------------------------- */

void test_transaction(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    ctx.equal(core::sheet::field_cell_kind(fixture.name), CellKind::Text,
              "a text field's storage kind is Text");
    ctx.equal(core::sheet::field_cell_kind(fixture.start), CellKind::Date,
              "a date field's storage kind is Date");
    ctx.equal(core::sheet::field_cell_kind(fixture.duration), CellKind::Integer,
              "Duration is stored as i32 ticks, so its storage kind is Integer");
    ctx.equal(core::sheet::field_cell_kind(0), CellKind::Text,
              "an unknown field falls back to Text");

    ctx.check(!sheet.transaction_active(), "no transaction initially");
    ctx.check(!sheet.stage_edit(1, 2, "3d"), "staging without a transaction fails");
    ctx.check(sheet.begin_transaction(), "begin_transaction succeeds");
    ctx.check(sheet.transaction_active(), "a transaction is open");
    ctx.equal(sheet.transaction_kind(), UndoKind::Transaction, "the default kind");
    ctx.check(!sheet.begin_transaction(), "a second begin is refused while one is open");

    ctx.check(sheet.stage_edit(1, 2, "3d"), "stage a duration");
    ctx.check(sheet.stage_edit(1, 3, "2026-11-02"), "stage a date");
    ctx.check(sheet.stage_edit(1, 8, "done"), "stage text");
    ctx.check(sheet.stage_edit(0, 1, "Design"), "stage an unchanged value");
    ctx.equal(sheet.staged_count(), std::size_t(4), "four staged commands");
    ctx.check(sheet.staged(4) == nullptr, "staged() past the end is null");
    const EditCommand* first = sheet.staged(0);
    ctx.check(first != nullptr && first->field == fixture.duration, "command 0 targets Duration");
    ctx.check(first != nullptr && first->kind == CellKind::Duration,
              "the target kind is resolved at stage time");
    ctx.check(first != nullptr && first->column == std::size_t(2),
              "the command remembers the column");

    const core::sheet::EditOutcome preview = sheet.validate_staged(0);
    ctx.check(preview.ok, "the staged duration validates");
    ctx.check(preview.ok && preview.value.kind == ValueKind::Duration &&
                  preview.value.integer == 14400,
              "the preview is the parsed value");
    ctx.check(!preview.changed, "validation never reports a change");
    ctx.equal(sheet.undo_depth(), std::size_t(0), "staging records no undo step");

    const core::sheet::TransactionResult applied = sheet.apply_transaction();
    ctx.check(applied.ok, "the transaction applies");
    ctx.equal(applied.staged, std::size_t(4), "four commands considered");
    ctx.equal(applied.changed, std::size_t(3), "three values changed; the unchanged one did not");
    ctx.check(!sheet.transaction_active(), "a successful apply closes the transaction");
    ctx.equal(sheet.staged_count(), std::size_t(0), "the staging list is cleared");

    const Value* duration = fixture.records.get(1, fixture.duration);
    ctx.check(duration != nullptr && duration->integer == 14400, "the duration was written");
    const Value* start = fixture.records.get(1, fixture.start);
    ctx.check(start != nullptr && start->date == date_of(2026, 11, 2), "the date was written");
    const Value* notes = fixture.records.get(1, fixture.notes);
    ctx.check(notes != nullptr && notes->text == "done", "the text was written");

    ctx.equal(sheet.undo_depth(), std::size_t(1), "one undo step for the whole transaction");
    ctx.equal(sheet.undo_entry_count(), std::size_t(3), "three cell entries inside it");
    const UndoStep* step = sheet.next_undo_step();
    ctx.check(step != nullptr && step->kind == UndoKind::Transaction,
              "the step is a transaction");
    ctx.check(step != nullptr && step->entries.size() == std::size_t(3),
              "the step holds every changed cell");
    ctx.check(sheet.next_undo() != nullptr && sheet.next_undo()->field == fixture.notes,
              "next_undo() is the last entry the undo reverses first");
    ctx.contains(sheet.undo_label(), "edit", "the undo label is human text");

    ctx.check(sheet.undo(), "undo the transaction");
    duration = fixture.records.get(1, fixture.duration);
    start = fixture.records.get(1, fixture.start);
    notes = fixture.records.get(1, fixture.notes);
    ctx.check(duration != nullptr && duration->integer == 9600, "the duration was restored");
    ctx.check(start != nullptr && start->date == date_of(2026, 10, 20), "the date was restored");
    ctx.check(notes != nullptr && notes->text.empty(), "the text was restored");
    ctx.equal(sheet.undo_depth(), std::size_t(0), "the undo stack is empty");
    ctx.equal(sheet.redo_entry_count(), std::size_t(3), "redo holds the same entries");

    ctx.check(sheet.redo(), "redo the transaction");
    duration = fixture.records.get(1, fixture.duration);
    ctx.check(duration != nullptr && duration->integer == 14400, "redo rewrote the duration");

    /* Percent, cost and bool target their own typed kinds. */
    ctx.check(sheet.begin_transaction(), "begin a mixed-kind transaction");
    ctx.check(sheet.stage_edit(2, 5, "75"), "stage a percent");
    ctx.check(sheet.stage_edit(2, 6, "99.5"), "stage a cost");
    ctx.check(sheet.stage_edit(5, 7, "Yes"), "stage a bool");
    const core::sheet::TransactionResult mixed = sheet.apply_transaction();
    ctx.check(mixed.ok, "the mixed-kind transaction applies");
    ctx.equal(mixed.changed, std::size_t(3), "three mixed-kind cells changed");
    const Value* percent = fixture.records.get(2, fixture.percent);
    ctx.check(percent != nullptr && percent->kind == ValueKind::Integer &&
                  percent->integer == 75,
              "the percent is stored as the integer 75");
    const Value* cost = fixture.records.get(2, fixture.cost);
    ctx.check(cost != nullptr && cost->kind == ValueKind::Number &&
                  std::fabs(cost->number - 99.5) < 1e-9,
              "the cost is stored as the number 99.5");
    const Value* flag = fixture.records.get(5, fixture.summary);
    ctx.check(flag != nullptr && flag->kind == ValueKind::Bool && flag->boolean,
              "the bool is stored as true");
    ctx.check(sheet.undo(), "undo the mixed-kind transaction");

    /* A command can be pushed pre-built; Auto resolves to the field kind. */
    ctx.check(sheet.begin_transaction(), "begin a push_command transaction");
    EditCommand pushed;
    pushed.row = 1;
    pushed.field = fixture.notes;
    pushed.text = "pushed";
    ctx.check(sheet.push_command(pushed), "push_command accepts a field-only command");
    ctx.check(sheet.staged(0) != nullptr && sheet.staged(0)->kind == CellKind::Text,
              "push_command resolved the kind from the field");
    ctx.check(sheet.apply_transaction().ok, "the pushed command applies");
    const Value* pushed_notes = fixture.records.get(1, fixture.notes);
    ctx.check(pushed_notes != nullptr && pushed_notes->text == "pushed",
              "the pushed value was written");
    ctx.check(sheet.undo(), "undo the pushed command");

    /* set_staged_value replaces the raw text with a typed value. */
    ctx.check(sheet.begin_transaction(), "begin a typed replacement transaction");
    ctx.check(sheet.stage_edit(0, 2, "1d"), "stage text first");
    ctx.check(sheet.set_staged_value(0, Value::of_duration(9600)),
              "replace it with a typed duration");
    ctx.check(sheet.apply_transaction().ok, "the replacement applies");
    ctx.check(fixture.records.get(0, fixture.duration) != nullptr &&
                  fixture.records.get(0, fixture.duration)->integer == 9600,
              "the typed value won");
    ctx.check(sheet.undo(), "undo the replacement");

    /* A read-only column refuses staging and editing. */
    ctx.check(sheet.set_column_read_only(1, true), "declare Name read-only");
    ctx.check(!sheet.begin_edit(0, 1), "begin_edit refuses a read-only column");
    ctx.check(sheet.begin_transaction(), "open a second transaction");
    ctx.check(!sheet.stage_edit(0, 1, "x"), "stage_edit refuses a read-only column");
    ctx.check(sheet.stage_edit(0, 0, "7"), "an unrelated column still stages");
    ctx.check(sheet.apply_transaction().ok, "the editable command applies");
    const Value* id = fixture.records.get(0, fixture.id);
    ctx.check(id != nullptr && id->integer == 7, "the ID was written");
    ctx.check(sheet.undo(), "undo the ID edit");
    ctx.check(sheet.set_column_read_only(1, false), "unlock Name");

    /* A field can be staged without a visible column addressing it. */
    ctx.check(sheet.begin_transaction(UndoKind::Transaction, "notes"), "begin a labelled transaction");
    ctx.equal_string(sheet.transaction_label(), "notes", "the label is kept");
    ctx.check(sheet.stage_field_edit(5, fixture.notes, "by field"), "stage by field id");
    ctx.check(sheet.apply_transaction().ok, "the field edit applies");
    const Value* handover = fixture.records.get(5, fixture.notes);
    ctx.check(handover != nullptr && handover->text == "by field", "the notes field was written");
    ctx.check(sheet.undo(), "undo the field edit");

    /* A collapsed row cannot be staged by column; cancelling writes nothing. */
    ctx.check(sheet.collapse(0), "collapse Design");
    ctx.check(sheet.begin_transaction(), "open a transaction on a collapsed sheet");
    ctx.check(!sheet.stage_edit(1, 2, "3d"), "a hidden row cannot be staged by column");
    sheet.cancel_transaction();
    ctx.check(!sheet.transaction_active(), "cancel closes the transaction");
    ctx.check(sheet.expand(0), "expand Design again");

    /* An edit session and a transaction cannot be open together. */
    ctx.check(sheet.begin_edit(2, 2), "open a cell edit");
    ctx.check(!sheet.begin_transaction(), "a transaction is refused while editing");
    sheet.cancel_edit();
    ctx.check(sheet.begin_transaction(), "the transaction opens after the edit is cancelled");

    sheet.clear_history();
    const core::sheet::TransactionResult empty = sheet.apply_transaction();
    ctx.check(empty.ok, "an empty transaction applies");
    ctx.equal(empty.changed, std::size_t(0), "an empty transaction changes nothing");
    ctx.equal(sheet.undo_depth(), std::size_t(0), "an empty transaction records no undo step");
}

void test_transaction_rollback(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    /* A parse failure in the middle of the list: nothing is written and the
     * list stays open so the bad entry can be corrected. */
    ctx.check(sheet.begin_transaction(), "begin the first transaction");
    ctx.check(sheet.stage_edit(1, 2, "3d"), "stage a good duration");
    ctx.check(sheet.stage_edit(1, 3, "not a date"), "stage a bad date");
    ctx.check(sheet.stage_edit(1, 8, "done"), "stage good text");
    const core::sheet::TransactionResult bad = sheet.apply_transaction();
    ctx.check(!bad.ok, "the bad transaction is refused");
    ctx.equal(bad.failed_index, std::size_t(1), "the failing command is named");
    ctx.equal(bad.failed_cell.row, std::size_t(1), "the failing row is named");
    ctx.equal(bad.failed_cell.column, std::size_t(3), "the failing column is named");
    ctx.contains(bad.error, "date", "the error names the field kind");
    ctx.check(sheet.transaction_active(), "a rejected apply keeps the transaction open");
    ctx.equal(sheet.staged_count(), std::size_t(3), "the staging list is intact");
    const Value* duration = fixture.records.get(1, fixture.duration);
    ctx.check(duration != nullptr && duration->integer == 9600,
              "no earlier command was written (rollback)");
    const Value* rolled_back_notes = fixture.records.get(1, fixture.notes);
    ctx.check(rolled_back_notes != nullptr && rolled_back_notes->text.empty(),
              "no later command was written either");
    ctx.equal(sheet.undo_depth(), std::size_t(0), "a failed apply records no undo step");

    ctx.check(sheet.set_staged_text(1, "2026-11-02"), "correct the bad command");
    const core::sheet::TransactionResult fixed = sheet.apply_transaction();
    ctx.check(fixed.ok, "the corrected transaction applies");
    ctx.equal(fixed.changed, std::size_t(3), "all three now change");
    const Value* fixed_start = fixture.records.get(1, fixture.start);
    ctx.check(fixed_start != nullptr && fixed_start->date == date_of(2026, 11, 2),
              "the corrected date was written");
    ctx.check(sheet.undo(), "undo the corrected transaction");
    ctx.equal(sheet.undo_depth(), std::size_t(0), "history is empty again");

    /* Two commands for one cell are ambiguous and rejected before any write. */
    ctx.check(sheet.begin_transaction(), "begin a duplicate transaction");
    ctx.check(sheet.stage_edit(0, 2, "1d"), "stage the first duration");
    ctx.check(sheet.stage_field_edit(0, fixture.duration, "2d"), "stage the same field again");
    const core::sheet::TransactionResult duplicate = sheet.apply_transaction();
    ctx.check(!duplicate.ok, "the duplicate transaction is refused");
    ctx.equal(duplicate.failed_index, std::size_t(1), "the second command is the duplicate");
    ctx.contains(duplicate.error, "duplicate", "the error explains the collision");
    ctx.check(fixture.records.get(0, fixture.duration) != nullptr &&
                  fixture.records.get(0, fixture.duration)->integer == 24000,
              "the duplicate wrote nothing");
    ctx.check(sheet.discard_staged(1), "discard the duplicate");
    ctx.check(sheet.apply_transaction().ok, "the remaining command applies");
    ctx.check(fixture.records.get(0, fixture.duration) != nullptr &&
                  fixture.records.get(0, fixture.duration)->integer == 4800,
              "1d is 4,800 ticks");
    ctx.check(sheet.undo(), "undo the remaining command");

    /* A typed value that does not fit the target kind is rejected, not coerced. */
    ctx.check(sheet.begin_transaction(), "begin a typed-value transaction");
    ctx.check(sheet.stage_value(0, 1, Value::of_int(7)), "stage an integer on a text cell");
    const core::sheet::TransactionResult misfit = sheet.apply_transaction();
    ctx.check(!misfit.ok, "the misfit transaction is refused");
    ctx.contains(misfit.error, "does not fit", "the error explains the kind mismatch");
    const Value* name = fixture.records.get(0, fixture.name);
    ctx.check(name != nullptr && name->text == "Design", "the text cell is untouched");
    sheet.cancel_transaction();

    /* Typed Null is legal and clears the cell. */
    ctx.check(sheet.begin_transaction(), "begin a clear transaction");
    ctx.check(sheet.stage_value(0, 2, Value{}), "stage Null on a duration cell");
    ctx.check(sheet.apply_transaction().ok, "a typed Null applies");
    const Value* cleared = fixture.records.get(0, fixture.duration);
    ctx.check(cleared != nullptr && cleared->is_null(), "the duration is Null");
    ctx.check(sheet.undo(), "undo the clear");
    ctx.check(fixture.records.get(0, fixture.duration) != nullptr &&
                  fixture.records.get(0, fixture.duration)->integer == 24000,
              "the duration came back");

    /* A column that becomes read-only after staging fails the apply. */
    ctx.check(sheet.begin_transaction(), "begin a transaction");
    ctx.check(sheet.stage_edit(0, 2, "2d"), "stage a duration");
    ctx.check(sheet.set_column_read_only(2, true), "lock Duration after staging");
    const core::sheet::TransactionResult locked = sheet.apply_transaction();
    ctx.check(!locked.ok, "the read-only transaction is refused");
    ctx.contains(locked.error, "read-only", "the error names the lock");
    ctx.check(fixture.records.get(0, fixture.duration) != nullptr &&
                  fixture.records.get(0, fixture.duration)->integer == 24000,
              "the locked cell is untouched");
    ctx.check(sheet.set_column_read_only(2, false), "unlock Duration");
    ctx.check(sheet.transaction_active(), "the locked transaction stays open");
    sheet.cancel_transaction();
    ctx.check(!sheet.transaction_active(), "cancel discards it");

    /* validate_staged reports the parse error without writing. */
    ctx.check(sheet.begin_transaction(), "begin a validation transaction");
    ctx.check(sheet.stage_edit(2, 2, "abc"), "stage unparsable duration text");
    const core::sheet::EditOutcome invalid = sheet.validate_staged(0);
    ctx.check(!invalid.ok, "the staged text does not validate");
    ctx.contains(invalid.error, "duration", "the validation error names the kind");
    ctx.check(sheet.staged(0) != nullptr && sheet.staged(0)->text == "abc",
              "validation leaves the staged text alone");
    const core::sheet::EditOutcome missing = sheet.validate_staged(9);
    ctx.check(!missing.ok, "an out-of-range staged index fails");
    ctx.contains(missing.error, "no staged edit", "the error explains the index");
    sheet.cancel_transaction();

    /* Cancel is a true discard: no write, no undo entry. */
    ctx.check(sheet.begin_transaction(), "begin a discard transaction");
    ctx.check(sheet.stage_edit(0, 2, "9d"), "stage a value");
    sheet.cancel_transaction();
    ctx.check(!sheet.transaction_active(), "cancel closed it");
    ctx.equal(sheet.staged_count(), std::size_t(0), "cancel emptied the list");
    ctx.check(fixture.records.get(0, fixture.duration) != nullptr &&
                  fixture.records.get(0, fixture.duration)->integer == 24000,
              "cancel wrote nothing");
    ctx.equal(sheet.undo_depth(), std::size_t(0), "cancel recorded no undo step");
}

/* ---- paste -------------------------------------------------------------- */

void test_paste(view_test::TestContext& ctx) {
    Fixture fixture(ctx);
    SheetModel sheet(fixture.records, fixture.columns());

    ctx.check(sheet.select_range(1, 2, 1, 2), "select B2:C3");
    const ClipboardBlock block = sheet.copy_selection();
    ctx.equal(block.rows, std::size_t(2), "the copied block has two rows");
    ctx.equal(block.columns, std::size_t(2), "the copied block has two columns");
    ctx.equal(block.cells.size(), std::size_t(4), "four cells copied");
    ctx.check(block.at(0, 0) != nullptr && block.at(0, 0)->field == fixture.name &&
                  block.at(0, 0)->text == "Draft",
              "the first cell is Draft's name");
    ctx.check(block.at(0, 1) != nullptr && block.at(0, 1)->kind == CellKind::Duration &&
                  block.at(0, 1)->value.integer == 9600,
              "the second cell is Draft's duration (typed)");

    /* Field-first mapping: pasting at D6 still puts Name in the Name column and
     * Duration in the Duration column.  Row 2 of the block falls past the last
     * row, so it is skipped rather than appended. */
    const core::sheet::PasteResult pasted = sheet.paste(block, 5, 3);
    ctx.check(pasted.ok, "the paste succeeds");
    ctx.equal(pasted.cells, std::size_t(2), "two block cells found a target row");
    ctx.equal(pasted.skipped, std::size_t(2), "the second block row fell past the end");
    ctx.equal(pasted.changed, std::size_t(2), "both target cells changed");
    const Value* pasted_name = fixture.records.get(5, fixture.name);
    ctx.check(pasted_name != nullptr && pasted_name->text == "Draft",
              "Name landed in the Name column");
    ctx.check(fixture.records.get(5, fixture.duration) != nullptr &&
                  fixture.records.get(5, fixture.duration)->integer == 9600,
              "Duration landed in the Duration column");
    const CellRange after_paste = sheet.selection_range();
    ctx.check(after_paste.valid, "the pasted range is selected");
    ctx.equal(after_paste.row_begin, std::size_t(5), "the selection starts at the target row");
    ctx.equal(after_paste.column_begin, std::size_t(1), "the selection starts at Name");
    ctx.equal(after_paste.column_end, std::size_t(2), "the selection ends at Duration");
    ctx.equal(sheet.undo_depth(), std::size_t(1), "a paste is one undo step");
    ctx.check(sheet.next_undo_step() != nullptr &&
                  sheet.next_undo_step()->kind == UndoKind::Paste,
              "the step is a paste");
    ctx.equal_string(sheet.undo_label(), "Paste 2 cells", "the paste label counts the cells");
    ctx.check(sheet.undo(), "undo the paste");
    pasted_name = fixture.records.get(5, fixture.name);
    ctx.check(pasted_name != nullptr && pasted_name->text == "Close", "the name was restored");
    ctx.check(fixture.records.get(5, fixture.duration) != nullptr &&
                  fixture.records.get(5, fixture.duration)->integer == 4800,
              "the duration was restored");

    /* A block with no source field falls back to the target's visible ordinal. */
    ClipboardBlock untyped;
    untyped.rows = 1;
    untyped.columns = 1;
    ClipboardCell x;
    x.kind = CellKind::Text;
    x.text = "X";
    x.value = Value::of_text("X");
    untyped.cells.push_back(x);
    const core::sheet::PasteResult positional = sheet.paste(untyped, 1, 1);
    ctx.check(positional.ok, "the positional paste succeeds");
    ctx.equal(positional.cells, std::size_t(1), "one cell targeted");
    pasted_name = fixture.records.get(1, fixture.name);
    ctx.check(pasted_name != nullptr && pasted_name->text == "X",
              "the fallback used the visible ordinal");
    ctx.check(sheet.undo(), "undo the positional paste");

    /* Values mode keeps the typed tick count exactly; Text mode is lossy. */
    ClipboardBlock lossy;
    lossy.rows = 1;
    lossy.columns = 1;
    ClipboardCell odd;
    odd.field = fixture.duration;
    odd.kind = CellKind::Duration;
    odd.value = Value::of_duration(7500);
    odd.text = "1.56 days";
    lossy.cells.push_back(odd);
    ctx.check(sheet.paste(lossy, 0, 2, PasteMode::Values).ok, "values paste succeeds");
    ctx.check(fixture.records.get(0, fixture.duration) != nullptr &&
                  fixture.records.get(0, fixture.duration)->integer == 7500,
              "values paste writes the exact 7,500 ticks");
    ctx.check(sheet.undo(), "undo the values paste");
    ctx.check(sheet.paste(lossy, 0, 2, PasteMode::Text).ok, "text paste succeeds");
    ctx.check(fixture.records.get(0, fixture.duration) != nullptr &&
                  fixture.records.get(0, fixture.duration)->integer == 7488,
              "text paste re-parses the lossy '1.56 days'");
    ctx.check(sheet.undo(), "undo the text paste");

    /* TSV text pasted as typed text parses per target column. */
    const ClipboardBlock from_text = ClipboardBlock::from_text("4d\t2026-12-01");
    const core::sheet::PasteResult text_paste = sheet.paste(from_text, 2, 2, PasteMode::Text);
    ctx.check(text_paste.ok, "the TSV paste succeeds");
    ctx.equal(text_paste.cells, std::size_t(2), "two cells targeted");
    ctx.check(fixture.records.get(2, fixture.duration) != nullptr &&
                  fixture.records.get(2, fixture.duration)->integer == 19200,
              "'4d' parsed to 19,200 ticks in the Duration column");
    ctx.check(fixture.records.get(2, fixture.start) != nullptr &&
                  fixture.records.get(2, fixture.start)->date == date_of(2026, 12, 1),
              "'2026-12-01' parsed in the Start column");
    ctx.check(sheet.undo(), "undo the TSV paste");

    /* The same TSV block in Values mode converts each source text through its
     * type, so it behaves the same for a lossless rendering. */
    ctx.check(sheet.paste(from_text, 2, 2).ok, "the TSV values paste succeeds");
    ctx.check(fixture.records.get(2, fixture.duration) != nullptr &&
                  fixture.records.get(2, fixture.duration)->integer == 19200,
              "values mode parsed the duration text");
    ctx.check(sheet.undo(), "undo the values-mode TSV paste");

    /* An empty source cell clears the target. */
    ClipboardBlock empty_cell;
    empty_cell.rows = 1;
    empty_cell.columns = 1;
    ClipboardCell blank;
    blank.field = fixture.duration;
    blank.kind = CellKind::Duration;
    empty_cell.cells.push_back(blank);
    ctx.check(sheet.paste(empty_cell, 0, 2).ok, "the empty-cell paste succeeds");
    const Value* cleared = fixture.records.get(0, fixture.duration);
    ctx.check(cleared != nullptr && cleared->is_null(), "the target was cleared to Null");
    ctx.check(sheet.undo(), "undo the clear");

    /* Pasting walks visible rows: a collapsed child is never written. */
    ctx.check(sheet.collapse(0), "collapse Design");
    ClipboardBlock two_names;
    two_names.rows = 2;
    two_names.columns = 1;
    ClipboardCell a;
    a.field = fixture.name;
    a.kind = CellKind::Text;
    a.text = "A";
    a.value = Value::of_text("A");
    two_names.cells.push_back(a);
    ClipboardCell b;
    b.field = fixture.name;
    b.kind = CellKind::Text;
    b.text = "B";
    b.value = Value::of_text("B");
    b.row_offset = 1;
    two_names.cells.push_back(b);
    const core::sheet::PasteResult hidden = sheet.paste(two_names, 0, 1);
    ctx.check(hidden.ok, "the paste over a collapsed row succeeds");
    ctx.equal(hidden.cells, std::size_t(2), "both visible rows target a cell");
    ctx.check(fixture.records.get(0, fixture.name) != nullptr &&
                  fixture.records.get(0, fixture.name)->text == "A",
              "the summary row received A");
    ctx.check(fixture.records.get(1, fixture.name) != nullptr &&
                  fixture.records.get(1, fixture.name)->text == "Draft",
              "the hidden child was not written");
    ctx.check(fixture.records.get(3, fixture.name) != nullptr &&
                  fixture.records.get(3, fixture.name)->text == "B",
              "the next visible row received B");
    ctx.check(sheet.undo(), "undo the hidden-row paste");
    ctx.check(sheet.expand(0), "expand Design again");

    /* One read-only target fails the whole paste, atomically. */
    ctx.check(sheet.set_column_read_only(2, true), "lock Duration");
    const core::sheet::PasteResult locked = sheet.paste(block, 5, 1);
    ctx.check(!locked.ok, "the paste into a read-only column is refused");
    ctx.equal(locked.failed_index, std::size_t(1), "the offending block cell is named");
    ctx.contains(locked.error, "read-only", "the error names the lock");
    ctx.check(fixture.records.get(5, fixture.name) != nullptr &&
                  fixture.records.get(5, fixture.name)->text == "Close",
              "the editable Name target was not written either (atomic)");
    ctx.check(sheet.set_column_read_only(2, false), "unlock Duration");

    /* Pasting without a selection, or during a transaction, is refused. */
    sheet.clear_selection();
    const core::sheet::PasteResult no_target = sheet.paste(block);
    ctx.check(!no_target.ok, "a paste without a selection fails");
    ctx.contains(no_target.error, "no selection", "the error explains the missing selection");
    ctx.check(sheet.select_cell(0, 0), "select A1");
    ctx.check(sheet.begin_transaction(), "open a transaction");
    const core::sheet::PasteResult busy = sheet.paste(block, 0, 0);
    ctx.check(!busy.ok, "a paste during a transaction fails");
    ctx.contains(busy.error, "transaction", "the error explains the open transaction");
    sheet.cancel_transaction();

    /* A block taller than the remaining rows is clipped, never appended. */
    ClipboardBlock three_rows;
    three_rows.rows = 3;
    three_rows.columns = 1;
    for (std::size_t i = 0; i < 3; ++i) {
        ClipboardCell cell;
        cell.row_offset = i;
        cell.kind = CellKind::Text;
        cell.text = std::string(1, static_cast<char>('P' + i));
        cell.value = Value::of_text(cell.text);
        three_rows.cells.push_back(std::move(cell));
    }
    const core::sheet::PasteResult clipped = sheet.paste(three_rows, 4, 1);
    ctx.check(clipped.ok, "the clipped paste succeeds");
    ctx.equal(clipped.cells, std::size_t(2), "two rows fit");
    ctx.equal(clipped.skipped, std::size_t(1), "the third row was skipped");
    ctx.equal(sheet.row_count(), kRowCount, "the paste added no row");
    ctx.check(fixture.records.get(4, fixture.name) != nullptr &&
                  fixture.records.get(4, fixture.name)->text == "P",
              "row 4 received P");
    ctx.check(fixture.records.get(5, fixture.name) != nullptr &&
                  fixture.records.get(5, fixture.name)->text == "Q",
              "row 5 received Q");
    ctx.check(sheet.undo(), "undo the clipped paste");
}

/* ---- undo coalescing and multi-step undo ------------------------------- */

void test_undo_coalescing(view_test::TestContext& ctx) {
    ctx.equal_string(core::sheet::undo_kind_name(UndoKind::Cell), "cell", "cell kind name");
    ctx.equal_string(core::sheet::undo_kind_name(UndoKind::Typing), "typing", "typing kind name");
    ctx.equal_string(core::sheet::undo_kind_name(UndoKind::Transaction), "transaction",
                     "transaction kind name");
    ctx.equal_string(core::sheet::undo_kind_name(UndoKind::Paste), "paste", "paste kind name");

    /* Two coalesced commits on the same cell are one step. */
    {
        Fixture fixture(ctx);
        SheetModel sheet(fixture.records, fixture.columns());
        ctx.check(sheet.begin_edit(2, 2), "edit the duration");
        sheet.set_edit_text("3d");
        ctx.check(sheet.commit_edit(true).ok, "commit 3d with coalescing");
        ctx.equal(sheet.undo_depth(), std::size_t(1), "one step after the first commit");
        ctx.equal(sheet.next_undo_step()->kind, UndoKind::Cell, "a plain cell step");
        ctx.check(sheet.begin_edit(2, 2), "edit the same duration again");
        sheet.set_edit_text("2d");
        ctx.check(sheet.commit_edit(true).ok, "commit 2d coalescing into the same cell");
        ctx.equal(sheet.undo_depth(), std::size_t(1), "still one step");
        ctx.equal(sheet.undo_entry_count(), std::size_t(1), "one entry");
        ctx.equal(sheet.next_undo_step()->kind, UndoKind::Typing, "the step became a typing run");
        ctx.check(sheet.undo(), "undo the typing run");
        ctx.check(fixture.records.get(2, fixture.duration) != nullptr &&
                      fixture.records.get(2, fixture.duration)->integer == 2400,
                  "one undo goes back to the value before the run");
        ctx.check(sheet.redo(), "redo the typing run");
        ctx.check(fixture.records.get(2, fixture.duration) != nullptr &&
                      fixture.records.get(2, fixture.duration)->integer == 9600,
                  "redo applies the final value");

        ctx.check(sheet.begin_edit(0, 1), "edit a different cell");
        sheet.set_edit_text("Outline");
        ctx.check(sheet.commit_edit(true).ok, "coalescing does not cross cells");
        ctx.equal(sheet.undo_depth(), std::size_t(2), "a different cell starts a new step");
    }

    /* A typing run that returns to its starting value disappears entirely. */
    {
        Fixture fixture(ctx);
        SheetModel sheet(fixture.records, fixture.columns());
        ctx.check(sheet.begin_edit(2, 2), "edit the duration");
        sheet.set_edit_text("3d");
        ctx.check(sheet.commit_edit(true).ok, "commit 3d");
        ctx.check(sheet.begin_edit(2, 2), "edit it again");
        sheet.set_edit_text("4 hrs");
        ctx.check(sheet.commit_edit(true).ok, "type the original value back");
        ctx.equal(sheet.undo_depth(), std::size_t(0), "the no-op typing run left no step");
        ctx.check(!sheet.can_undo(), "there is nothing to undo");
        ctx.check(fixture.records.get(2, fixture.duration) != nullptr &&
                      fixture.records.get(2, fixture.duration)->integer == 2400,
                  "the value is the original");
    }

    /* commit_edit(false) splits; undo(n)/redo(n) walk whole steps. */
    {
        Fixture fixture(ctx);
        SheetModel sheet(fixture.records, fixture.columns());
        ctx.check(sheet.begin_edit(2, 2), "edit the duration");
        sheet.set_edit_text("3d");
        ctx.check(sheet.commit_edit(true).ok, "commit 3d");
        ctx.check(sheet.begin_edit(2, 2), "edit it again");
        sheet.set_edit_text("2d");
        ctx.check(sheet.commit_edit(false).ok, "commit 2d without coalescing");
        ctx.equal(sheet.undo_depth(), std::size_t(2), "two separate steps");
        ctx.check(sheet.undo(std::size_t(2)), "undo two steps at once");
        ctx.equal(sheet.undo_depth(), std::size_t(0), "everything is undone");
        ctx.check(fixture.records.get(2, fixture.duration) != nullptr &&
                      fixture.records.get(2, fixture.duration)->integer == 2400,
                  "the original value is back");
        ctx.check(!sheet.redo(std::size_t(0)), "redo(0) is a no-op");
        ctx.check(!sheet.undo(std::size_t(0)), "undo(0) is a no-op");
        ctx.check(sheet.redo(std::size_t(5)), "redo past the end applies the steps that exist");
        ctx.equal(sheet.undo_depth(), std::size_t(2), "both steps are back");
        ctx.check(fixture.records.get(2, fixture.duration) != nullptr &&
                      fixture.records.get(2, fixture.duration)->integer == 9600,
                  "the final value is back");
        ctx.check(!sheet.redo(std::size_t(5)), "redo on an empty stack fails");
    }

    /* A paste is its own step and never coalesces into a typing run. */
    {
        Fixture fixture(ctx);
        SheetModel sheet(fixture.records, fixture.columns());
        ctx.check(sheet.begin_edit(2, 2), "edit the duration");
        sheet.set_edit_text("3d");
        ctx.check(sheet.commit_edit(true).ok, "commit 3d");
        ClipboardBlock block;
        block.rows = 1;
        block.columns = 1;
        ClipboardCell cell;
        cell.field = fixture.duration;
        cell.kind = CellKind::Duration;
        cell.value = Value::of_duration(9600);
        cell.text = "2 days";
        block.cells.push_back(cell);
        ctx.check(sheet.paste(block, 2, 2).ok, "paste 2d over the same cell");
        ctx.equal(sheet.undo_depth(), std::size_t(2), "the paste started a new step");
        ctx.equal(sheet.next_undo_step()->kind, UndoKind::Paste, "the newest step is the paste");
        ctx.equal(sheet.undo_entry_count(), std::size_t(2), "two entries across two steps");
        ctx.check(sheet.undo(), "undo the paste");
        ctx.check(fixture.records.get(2, fixture.duration) != nullptr &&
                      fixture.records.get(2, fixture.duration)->integer == 14400,
                  "the paste step restored the typed value");
    }

    /* A new action after undo clears redo. */
    {
        Fixture fixture(ctx);
        SheetModel sheet(fixture.records, fixture.columns());
        ctx.check(sheet.begin_edit(2, 2), "edit the duration");
        sheet.set_edit_text("3d");
        ctx.check(sheet.commit_edit().ok, "commit 3d");
        ctx.check(sheet.undo(), "undo it");
        ctx.equal(sheet.redo_depth(), std::size_t(1), "one redo is available");
        ctx.check(sheet.begin_edit(0, 1), "edit another cell");
        sheet.set_edit_text("Outline");
        ctx.check(sheet.commit_edit().ok, "commit it");
        ctx.equal(sheet.redo_depth(), std::size_t(0), "the new action cleared redo");
        ctx.contains(sheet.undo_label(), "Name", "the undo label names the field");
    }
}

}  // namespace

int main() {
    view_test::TestContext ctx;

    test_columns(ctx);
    test_formatting(ctx);
    test_format_settings(ctx);
    test_outline(ctx);
    test_outline_override(ctx);
    test_selection(ctx);
    test_editing(ctx);
    test_editing_errors(ctx);
    test_undo_redo(ctx);
    test_undo_null(ctx);
    test_selection_range(ctx);
    test_clipboard_text(ctx);
    test_transaction(ctx);
    test_transaction_rollback(ctx);
    test_paste(ctx);
    test_undo_coalescing(ctx);
    test_geometry(ctx);
    test_table_definition(ctx);
    test_style(ctx);

    return ctx.finish("sheet_tests");
}
