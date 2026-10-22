// app/filter_tests.cpp -- the filter dialog model, proved headlessly.
//
// The shell builds the dialog from filter_fields()/filter_operators(), turns the
// authored rule into a core::view::FilterDefinition with build_filter(), and then
// displays the rows in filter_rows() by restricting the visible row list.  These
// checks cover that path end to end on synthetic rows: the field/operator lists,
// the value parsing per column kind (the same parser the cell editor uses),
// matching, the error messages a dialog shows, the row-list restriction, and the
// RecordSet copy the PDF export prints from.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "date.h"
#include "filter_model.h"
#include "layout_model.h"
#include "sheet.h"
#include "value.h"

namespace {

using app::FilterBuild;
using app::FilterField;
using app::FilterJoin;
using app::FilterOperatorChoice;
using app::FilterRule;
using core::view::FilterCriterion;
using core::view::FilterOp;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

void check_size(std::size_t got, std::size_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

app::PlanInput make_plan() {
    app::PlanInput input;
    input.title = "Synthetic plan";
    input.project_start = core::sched::from_ymd(2026, 10, 19);
    const char* names[] = {"One", "Two", "Three", "Four"};
    for (std::size_t index = 0; index < 4; ++index) {
        app::TaskRow row;
        row.uid = static_cast<std::uint32_t>(index + 1);
        row.id = static_cast<std::uint32_t>(index + 1);
        row.outline_number = std::to_string(index + 1);
        row.name = names[index];
        row.duration_ticks = static_cast<std::int64_t>(index + 1) * core::view::kTicksPerDay;
        row.start = core::sched::from_ymd(2026, 10, 19 + static_cast<int>(index));
        row.finish = row.start;
        input.tasks.push_back(row);
    }
    return input;
}

struct Fixture {
    app::PlanInput plan;
    core::view::RecordSet records;
    core::sheet::SheetModel sheet;
    std::vector<FilterField> fields;
    const std::vector<FilterOperatorChoice>* operators = nullptr;
    core::sheet::SheetFormat format;

    Fixture()
        : plan(make_plan()),
          records(app::make_record_set(plan)),
          sheet(records, app::default_sheet_columns()),
          operators(&app::filter_operators()) {
        sheet.set_format(format);
        fields = app::filter_fields(sheet);
    }

    std::size_t field_index(const std::string& title) const {
        for (std::size_t index = 0; index < fields.size(); ++index) {
            if (fields[index].title == title) {
                return index;
            }
        }
        return fields.size();
    }

    std::size_t operator_index(FilterOp op) const {
        for (std::size_t index = 0; index < operators->size(); ++index) {
            if ((*operators)[index].op == op) {
                return index;
            }
        }
        return operators->size();
    }

    FilterBuild build(const std::string& field_title, FilterOp op, const std::string& value) const {
        FilterRule rule;
        rule.field_index = field_index(field_title);
        rule.operator_index = operator_index(op);
        rule.value = value;
        return app::build_filter(fields, *operators, {rule}, format);
    }
};

void test_fields_and_operators() {
    Fixture fixture;
    check_size(fixture.fields.size(), 7, "the default sheet offers seven filter fields");
    if (fixture.fields.size() == 7) {
        check_text(fixture.fields[0].title, "UID", "the first field is UID");
        check_text(fixture.fields[3].title, "Name", "the fourth field is Name");
        check_text(fixture.fields[4].title, "Start", "the fifth field is Start");
        check_text(fixture.fields[6].title, "Duration", "the seventh field is Duration");
        check(fixture.fields[4].kind == core::sheet::CellKind::Date,
              "the Start field parses as a Date");
        check(fixture.fields[6].kind == core::sheet::CellKind::Duration,
              "the Duration field parses as a Duration");
        check(fixture.fields[3].kind == core::sheet::CellKind::Text,
              "the Name field parses as Text");
    }
    check(!fixture.operators->empty(), "there are filter operators");
    check_text((*fixture.operators)[0].title, "equals", "the first operator is equals");
    check((*fixture.operators)[0].op == FilterOp::Equals, "the first operator is Equals");
    bool has_blank = false;
    for (const FilterOperatorChoice& choice : *fixture.operators) {
        has_blank = has_blank || choice.op == FilterOp::IsNull;
    }
    check(has_blank, "the operator list offers 'is blank'");
}

void test_simple_comparisons() {
    Fixture fixture;

    const FilterBuild by_name = fixture.build("Name", FilterOp::Equals, "Two");
    check(by_name.ok, "a name filter builds");
    if (by_name.ok) {
        check_text(by_name.description, "Name equals \"Two\"", "the description names the rule");
        check(by_name.definition.record_type == core::model::RecordType::Task,
              "the filter is a task filter");
        check(by_name.definition.criteria.kind == FilterCriterion::Kind::Comparison,
              "one rule is one comparison");
        const std::vector<std::size_t> rows = app::filter_rows(by_name.definition, fixture.records);
        check_size(rows.size(), 1, "exactly one task is named Two");
        if (!rows.empty()) {
            check(rows[0] == 1, "the matching row is the second one");
        }
    }

    const FilterBuild contains = fixture.build("Name", FilterOp::Contains, "T");
    check(contains.ok, "a contains filter builds");
    if (contains.ok) {
        const std::vector<std::size_t> rows = app::filter_rows(contains.definition, fixture.records);
        check_size(rows.size(), 2, "Two and Three contain a capital T");
        if (rows.size() == 2) {
            check(rows[0] == 1 && rows[1] == 2, "the matching rows are 1 and 2");
        }
    }

    const FilterBuild duration = fixture.build("Duration", FilterOp::GreaterOrEqual, "2d");
    check(duration.ok, "a duration filter builds from sheet-style input");
    if (duration.ok) {
        const std::vector<std::size_t> rows = app::filter_rows(duration.definition, fixture.records);
        check_size(rows.size(), 3, "three tasks last two days or more");
        if (rows.size() == 3) {
            check(rows[0] == 1 && rows[1] == 2 && rows[2] == 3, "the three longest tasks match");
        }
    }

    const FilterBuild date = fixture.build("Start", FilterOp::GreaterThan, "2026-10-21");
    check(date.ok, "a date filter builds from an ISO date");
    if (date.ok) {
        const std::vector<std::size_t> rows = app::filter_rows(date.definition, fixture.records);
        check_size(rows.size(), 1, "one task starts after 2026-10-21");
        if (!rows.empty()) {
            check(rows[0] == 3, "the last task starts after 2026-10-21");
        }
    }

    const FilterBuild blank = fixture.build("Outline", FilterOp::IsNull, "");
    check(blank.ok, "'is blank' needs no value");
    if (blank.ok) {
        check_size(app::filter_rows(blank.definition, fixture.records).size(), 0,
                   "every synthetic row has an outline number");
        check_text(blank.description, "Outline is blank", "the description omits the value");
    }
}

void test_two_rule_tree() {
    Fixture fixture;
    FilterRule name_rule;
    name_rule.field_index = fixture.field_index("Name");
    name_rule.operator_index = fixture.operator_index(FilterOp::Contains);
    name_rule.value = "T";
    FilterRule duration_rule;
    duration_rule.field_index = fixture.field_index("Duration");
    duration_rule.operator_index = fixture.operator_index(FilterOp::GreaterOrEqual);
    duration_rule.value = "3d";

    const FilterBuild all = app::build_filter(fixture.fields, *fixture.operators,
                                              {name_rule, duration_rule}, fixture.format,
                                              FilterJoin::all);
    check(all.ok, "a two-rule All filter builds");
    if (all.ok) {
        check(all.definition.criteria.kind == FilterCriterion::Kind::All,
              "two rules joined with All are an All node");
        check_text(all.description, "Name contains \"T\" and Duration is at least \"3d\"",
                   "the description joins the rules with 'and'");
        const std::vector<std::size_t> rows = app::filter_rows(all.definition, fixture.records);
        check_size(rows.size(), 1, "only Three is both a T name and 3 days or more");
        if (!rows.empty()) {
            check(rows[0] == 2, "the All filter matches row 2");
        }
    }

    const FilterBuild any = app::build_filter(fixture.fields, *fixture.operators,
                                              {name_rule, duration_rule}, fixture.format,
                                              FilterJoin::any);
    check(any.ok, "a two-rule Any filter builds");
    if (any.ok) {
        check(any.definition.criteria.kind == FilterCriterion::Kind::Any,
              "two rules joined with Any are an Any node");
        check_size(app::filter_rows(any.definition, fixture.records).size(), 3,
                   "the Any filter matches rows 1, 2 and 3");
    }
}

void test_errors() {
    Fixture fixture;

    FilterRule bad_field;
    bad_field.field_index = fixture.fields.size() + 5;
    bad_field.operator_index = 0;
    bad_field.value = "x";
    const FilterBuild out_of_range =
        app::build_filter(fixture.fields, *fixture.operators, {bad_field}, fixture.format);
    check(!out_of_range.ok, "an out-of-range field index is an error");
    check(!out_of_range.error.empty(), "the field error has a message");

    const FilterBuild no_rules =
        app::build_filter(fixture.fields, *fixture.operators, {}, fixture.format);
    check(!no_rules.ok, "an empty rule list is an error");
    check(!no_rules.error.empty(), "the empty-rule error has a message");

    const FilterBuild empty_value = fixture.build("Name", FilterOp::Equals, "   ");
    check(!empty_value.ok, "an empty comparison value is an error");
    check(empty_value.error.find("Name") != std::string::npos,
          "the empty-value error names the field");

    const FilterBuild bad_date = fixture.build("Start", FilterOp::Equals, "not-a-date");
    check(!bad_date.ok, "a value that does not parse as the column's kind is an error");
    check(bad_date.error.find("Start") != std::string::npos,
          "the parse error names the field");
    check(bad_date.definition.id.empty(), "a refused filter has no definition id");

    // A blank field list cannot be filtered at all.
    const FilterBuild no_fields = app::build_filter({}, *fixture.operators, {}, fixture.format);
    check(!no_fields.ok, "a sheet with no columns cannot build a filter");
}

void test_row_restriction() {
    Fixture fixture;
    const std::vector<std::size_t> rows = {0, 1, 2, 3};

    const std::vector<std::size_t> none = app::restrict_to_filter(rows, {});
    check_size(none.size(), 0, "an empty match set shows nothing");

    const std::vector<std::size_t> some = app::restrict_to_filter(rows, {3, 1});
    check_size(some.size(), 2, "two matches keep two rows");
    if (some.size() == 2) {
        check(some[0] == 1 && some[1] == 3, "the restriction keeps the display order");
    }

    // Collapsed rows are already absent from `rows`, so a filter cannot bring
    // them back.
    const std::vector<std::size_t> visible = {0, 2};
    const std::vector<std::size_t> both = app::restrict_to_filter(visible, {1, 2, 3});
    check_size(both.size(), 1, "a hidden row stays hidden under a filter");
    if (!both.empty()) {
        check(both[0] == 2, "the visible matching row is kept");
    }
}

void test_filtered_record_set() {
    Fixture fixture;
    const std::vector<std::size_t> rows = {1, 2};
    const core::view::RecordSet subset = app::filtered_record_set(fixture.records, rows);
    check_size(subset.row_count(), 2, "the printed copy has two rows");
    check_size(subset.field_count(), fixture.records.field_count(),
               "the printed copy keeps every field");
    if (subset.row_count() == 2) {
        check(subset.uid(0) == fixture.records.uid(1), "the first row keeps its uid");
        check(subset.uid(1) == fixture.records.uid(2), "the second row keeps its uid");
        const core::view::FieldId name = app::task_field_id("Name");
        const core::view::Value* value = subset.get(0, name);
        check(value != nullptr && value->kind == core::view::ValueKind::Text,
              "the name survives the copy");
        if (value != nullptr && value->kind == core::view::ValueKind::Text) {
            check_text(value->text, "Two", "the copied name is the same task");
        }
    }

    const core::view::RecordSet empty = app::filtered_record_set(fixture.records, {});
    check_size(empty.row_count(), 0, "an empty row list copies no rows");
}

}  // namespace

int main() {
    test_fields_and_operators();
    test_simple_comparisons();
    test_two_rule_tree();
    test_errors();
    test_row_restriction();
    test_filtered_record_set();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("filter_tests: %d checks passed\n", g_checks);
    return 0;
}
