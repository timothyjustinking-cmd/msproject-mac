// app/app_fields_tests.cpp -- the task sheet's custom-field columns, proved
// headlessly (wave 12).
//
// Three layers:
//   1. the STORED custom fields: the six column specs come from the shared field
//      registry (mpp::custom_field_id), a decoded mpp::CustomFieldValue becomes
//      the right core::view::Value, and make_record_set puts the value in the
//      RecordSet so the sheet formats it with the preference date/duration rules;
//   2. the FORMULA columns: the committed 114/Props blob
//      (tests/data/synthetic-props.bin, W50's synthetic fixture -- NOT client
//      data) yields the project's six formulas, which core::formula parses and
//      evaluates; the expected values are the stored formula text applied to a
//      known row;
//   3. the COLUMN SET and its visibility: which columns exist, that every
//      custom/formula column is read-only, and that the per-column preference
//      round-trips through the one NSUserDefaults line (app/prefs.h).
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\vs-w51 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\vs-w51 --config Release
//   ctest --test-dir build\vs-w51 -C Release -R "^app_fields$" --output-on-failure

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "columns.h"
#include "date.h"
#include "document.h"
#include "field_ref.h"
#include "field_table.h"
#include "field_value.h"
#include "fields_model.h"
#include "fixture_guard.h"
#include "layout_model.h"
#include "prefs.h"
#include "sheet.h"
#include "value.h"

namespace {

using app::date32;
using core::sched::from_ymd;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
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

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

void check_date(date32 got, date32 want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %s want %s\n", what, core::sched::to_string(got).c_str(),
                    core::sched::to_string(want).c_str());
    }
}

std::uint32_t field_id(mpp::FieldKind kind, unsigned index) {
    return mpp::custom_field_id(kind, index);
}

mpp::CustomFieldValue text_value(std::uint32_t field, std::string text) {
    mpp::CustomFieldValue value;
    value.field_id = field;
    value.kind = mpp::FieldKind::text;
    value.present = true;
    value.storage_measured = true;
    value.text = std::move(text);
    return value;
}

mpp::CustomFieldValue number_value(std::uint32_t field, double number) {
    mpp::CustomFieldValue value;
    value.field_id = field;
    value.kind = mpp::FieldKind::number;
    value.present = true;
    value.storage_measured = true;
    value.number = number;
    return value;
}

mpp::CustomFieldValue date_value(std::uint32_t field, std::uint32_t packed) {
    mpp::CustomFieldValue value;
    value.field_id = field;
    value.kind = mpp::FieldKind::date;
    value.present = true;
    value.storage_measured = true;
    value.packed_date = packed;
    return value;
}

mpp::CustomFieldValue duration_value(std::uint32_t field, std::uint32_t ticks) {
    mpp::CustomFieldValue value;
    value.field_id = field;
    value.kind = mpp::FieldKind::duration;
    value.present = true;
    value.storage_measured = true;
    value.duration_ticks = ticks;
    return value;
}

mpp::CustomFieldValue flag_value(std::uint32_t field, bool flag) {
    mpp::CustomFieldValue value;
    value.field_id = field;
    value.kind = mpp::FieldKind::flag;
    value.present = true;
    value.storage_measured = true;
    value.flag = flag;
    return value;
}

app::TaskRow make_task(std::uint32_t uid, const char* name, std::int64_t duration_ticks,
                       std::uint16_t percent, date32 start) {
    app::TaskRow row;
    row.uid = uid;
    row.id = uid;
    row.name = name;
    row.duration_ticks = duration_ticks;
    row.percent_complete = percent;
    row.start = start;
    row.finish = start;
    return row;
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                     std::istreambuf_iterator<char>());
}

// The stored six, as one plan with every decoded family represented.
app::PlanInput make_stored_plan() {
    const std::uint32_t text1 = field_id(mpp::FieldKind::text, 1);
    const std::uint32_t number1 = field_id(mpp::FieldKind::number, 1);
    const std::uint32_t number2 = field_id(mpp::FieldKind::number, 2);
    const std::uint32_t duration1 = field_id(mpp::FieldKind::duration, 1);
    const std::uint32_t date1 = field_id(mpp::FieldKind::date, 1);
    const std::uint32_t flag1 = field_id(mpp::FieldKind::flag, 1);

    app::PlanInput plan;
    app::TaskRow first = make_task(1, "Alpha", 4800, 75, from_ymd(2026, 10, 19, 8, 0));
    first.custom_fields.push_back(text_value(text1, "alpha"));
    first.custom_fields.push_back(number_value(number1, 42.5));
    first.custom_fields.push_back(number_value(number2, 3.25));
    first.custom_fields.push_back(duration_value(duration1, 4800));
    // The wave-10 oracle measured Date1 0x3d1812c0 = 2026-10-26 08:00.
    first.custom_fields.push_back(date_value(date1, 0x3d1812c0u));
    first.custom_fields.push_back(flag_value(flag1, true));
    plan.tasks.push_back(first);

    app::TaskRow second = make_task(2, "Bravo", 24000, 25, from_ymd(2026, 10, 20, 8, 0));
    second.custom_fields.push_back(flag_value(flag1, false));
    plan.tasks.push_back(second);
    return plan;
}

// ---------------------------------------------------------------------------
// 1. The stored custom fields
// ---------------------------------------------------------------------------

void test_stored_column_specs() {
    const std::vector<app::StoredFieldColumn>& columns = app::stored_field_columns();
    check_size(columns.size(), 6, "six stored custom-field columns");
    if (columns.size() != 6) {
        return;
    }
    const mpp::FieldKind kinds[] = {mpp::FieldKind::text, mpp::FieldKind::number,
                                    mpp::FieldKind::number, mpp::FieldKind::date,
                                    mpp::FieldKind::duration, mpp::FieldKind::flag};
    const unsigned indexes[] = {1, 1, 2, 1, 1, 1};
    for (std::size_t index = 0; index < columns.size(); ++index) {
        check(columns[index].field == field_id(kinds[index], indexes[index]),
              "the stored column id is the registry id");
        check(columns[index].field != 0, "the registry resolved the id");
        const core::model::FieldInfo* info =
            core::model::find_field_by_id(columns[index].field);
        check(info != nullptr, "the stored field is in the descriptor table");
        if (info != nullptr) {
            check(info->record_type == static_cast<std::uint8_t>(core::model::RecordType::Task),
                  "the stored field is a Task field");
        }
        check(app::is_stored_field_column(columns[index].field),
              "is_stored_field_column recognizes it");
        check(reinterpret_cast<const void*>(app::stored_field_columns().data()) ==
                  reinterpret_cast<const void*>(columns.data()),
              "the spec list is a process-lifetime static");
    }
    check(columns[0].field == 0x0b400033u, "Text1 is 0x0b400033 (measured)");
    check(columns[1].field == 0x0b400057u, "Number1 is 0x0b400057 (measured)");
    check(columns[2].field == 0x0b400058u, "Number2 is 0x0b400058 (measured)");
    check(columns[3].field == 0x0b400109u, "Date1 is 0x0b400109 (measured)");
    check(columns[4].field == 0x0b400067u, "Duration1 is 0x0b400067 (measured)");
    check(columns[5].field == 0x0b400048u, "Flag1 is 0x0b400048 (measured)");
    check(!app::is_stored_field_column(app::name_field_id()),
          "Name is not a stored custom-field column");
    check(!app::is_stored_field_column(0), "field 0 is never a column");
}

void test_value_conversion() {
    core::view::Value value = app::value_of_custom_field(text_value(0x0b400033u, "alpha"));
    check(value.kind == core::view::ValueKind::Text, "a text custom field is Text");
    check_text(value.text, "alpha", "the text value survives");

    value = app::value_of_custom_field(number_value(0x0b400057u, 42.5));
    check(value.kind == core::view::ValueKind::Number, "a number custom field is Number");
    check(value.number == 42.5, "the number value survives");

    value = app::value_of_custom_field(duration_value(0x0b400067u, 4800));
    check(value.kind == core::view::ValueKind::Duration, "a duration custom field is Duration");
    check(value.integer == 4800, "file ticks are RecordSet ticks (6 s each)");

    value = app::value_of_custom_field(flag_value(0x0b400048u, true));
    check(value.kind == core::view::ValueKind::Bool, "a flag custom field is Bool");
    check(value.boolean, "the flag value survives");

    value = app::value_of_custom_field(date_value(0x0b400109u, 0x3d1812c0u));
    check(value.kind == core::view::ValueKind::Date, "a date custom field is Date");
    check_date(app::date32_from_date_value(value.date), from_ymd(2026, 10, 26, 8, 0),
               "the packed date decodes to 2026-10-26 08:00 (measured)");

    // The two "not a value" cases are Null, never 0 / false / a phantom date.
    mpp::CustomFieldValue absent;
    absent.field_id = 0x0b400057u;
    absent.kind = mpp::FieldKind::number;
    check(app::value_of_custom_field(absent).is_null(), "an absent value is Null");
    mpp::CustomFieldValue unmeasured = number_value(0x0b40006au, 12.0);
    unmeasured.storage_measured = false;
    check(app::value_of_custom_field(unmeasured).is_null(),
          "an unmeasured (Cost) value is Null, not a guess");
    mpp::CustomFieldValue invalid_date = date_value(0x0b400109u, 0u);
    check(app::value_of_custom_field(invalid_date).is_null(),
          "a not-set packed date is Null");
}

void test_record_set_values() {
    const app::PlanInput plan = make_stored_plan();
    core::view::RecordSet records = app::make_record_set(plan);
    check_size(records.row_count(), 2, "both tasks are in the RecordSet");
    const std::uint32_t text1 = field_id(mpp::FieldKind::text, 1);
    const std::uint32_t number1 = field_id(mpp::FieldKind::number, 1);
    const std::uint32_t number2 = field_id(mpp::FieldKind::number, 2);
    const std::uint32_t duration1 = field_id(mpp::FieldKind::duration, 1);
    const std::uint32_t date1 = field_id(mpp::FieldKind::date, 1);
    const std::uint32_t flag1 = field_id(mpp::FieldKind::flag, 1);
    for (const std::uint32_t field : {text1, number1, number2, duration1, date1, flag1}) {
        check(records.has_field(field), "every stored field is in the RecordSet schema");
    }
    if (const core::view::Value* value = records.get(0, text1); value != nullptr) {
        check(value->kind == core::view::ValueKind::Text, "Text1 is Text in the RecordSet");
        check_text(value->text, "alpha", "Text1 holds the stored text");
    }
    if (const core::view::Value* value = records.get(0, number1); value != nullptr) {
        check(value->number == 42.5, "Number1 holds the stored number");
    }
    if (const core::view::Value* value = records.get(0, number2); value != nullptr) {
        check(value->number == 3.25, "Number2 holds the stored number");
    }
    if (const core::view::Value* value = records.get(0, duration1); value != nullptr) {
        check(value->kind == core::view::ValueKind::Duration, "Duration1 is a Duration");
        check(value->integer == 4800, "Duration1 holds the file ticks");
    }
    if (const core::view::Value* value = records.get(0, date1); value != nullptr) {
        check(value->kind == core::view::ValueKind::Date, "Date1 is a Date");
        check_date(app::date32_from_date_value(value->date), from_ymd(2026, 10, 26, 8, 0),
                   "Date1 decodes to the measured instant");
    }
    if (const core::view::Value* value = records.get(0, flag1); value != nullptr) {
        check(value->kind == core::view::ValueKind::Bool, "Flag1 is a Bool");
        check(value->boolean, "row 1 stores Flag1 = true");
    }
    if (const core::view::Value* value = records.get(1, flag1); value != nullptr) {
        check(!value->boolean, "row 2 stores Flag1 = false");
    }
    // Task 2 stores no Text1: the cell is blank, not empty text or a zero.
    if (const core::view::Value* value = records.get(1, text1); value != nullptr) {
        check(value->is_null(), "a field the row does not store is Null");
    }
}

void test_column_formatting() {
    const app::PlanInput plan = make_stored_plan();
    core::view::RecordSet records = app::make_record_set(plan);
    const app::FormulaColumnSet no_formulas;  // the stored columns, nothing else
    core::sheet::SheetModel sheet(records, app::task_editor_columns(no_formulas));
    app::Preferences prefs;
    core::sheet::SheetFormat format;
    app::apply_preferences(prefs, format);
    sheet.set_format(format);

    auto column_of = [&sheet](std::uint32_t field) -> std::size_t {
        return sheet.column_of_field(field);
    };
    const std::size_t text_column = column_of(field_id(mpp::FieldKind::text, 1));
    const std::size_t number_column = column_of(field_id(mpp::FieldKind::number, 1));
    const std::size_t duration_column = column_of(field_id(mpp::FieldKind::duration, 1));
    const std::size_t date_column = column_of(field_id(mpp::FieldKind::date, 1));
    const std::size_t flag_column = column_of(field_id(mpp::FieldKind::flag, 1));
    check(text_column != core::sheet::kNoIndex, "the Text1 column exists");
    check(number_column != core::sheet::kNoIndex, "the Number1 column exists");
    check(duration_column != core::sheet::kNoIndex, "the Duration1 column exists");
    check(date_column != core::sheet::kNoIndex, "the Date1 column exists");
    check(flag_column != core::sheet::kNoIndex, "the Flag1 column exists");

    check_text(app::cell_text(sheet, 0, text_column, prefs), "alpha", "Text1 renders as text");
    check_text(app::cell_text(sheet, 0, number_column, prefs), "42.5",
               "Number1 renders as a number");
    check_text(app::cell_text(sheet, 0, duration_column, prefs), "1 day",
               "Duration1 renders as a duration");
    check_text(app::cell_text(sheet, 0, date_column, prefs), "Mon 26/10/26",
               "Date1 honours the date-format preference");
    check_text(app::cell_text(sheet, 0, flag_column, prefs), "Yes", "Flag1 renders Yes");
    check_text(app::cell_text(sheet, 1, flag_column, prefs), "No", "a clear Flag1 renders No");
    check_text(app::cell_text(sheet, 1, text_column, prefs), "",
               "a field the row does not store is blank");

    // The duration preference reaches a custom-field duration too, because it is
    // the same app::cell_text() the Duration column goes through.
    app::Preferences hours = prefs;
    hours.duration_style = app::DurationStyle::hours;
    check_text(app::cell_text(sheet, 0, duration_column, hours), "8 hrs",
               "the hours preference applies to Duration1");
    app::Preferences iso = prefs;
    iso.date_style = app::DateStyle::iso;
    app::apply_preferences(iso, format);
    sheet.set_format(format);
    check_text(app::cell_text(sheet, 0, date_column, iso), "2026-10-26",
               "the ISO date preference applies to Date1");
    app::apply_preferences(prefs, format);
    sheet.set_format(format);

    // Column captions come from the descriptor table, never invented.
    check_text(sheet.column_title(text_column), "Text1", "the Text1 caption is the field name");
    check_text(sheet.column_title(flag_column), "Flag1", "the Flag1 caption is the field name");

    // Every custom column is read-only.
    check(sheet.column_read_only(text_column), "the stored columns are read-only");
    check(!sheet.begin_edit(0, text_column), "a stored custom-field cell cannot be edited");
}

// ---------------------------------------------------------------------------
// 2. The file's own formulas
// ---------------------------------------------------------------------------

#if defined(APP_TEST_DATA_DIR)

void test_formula_extraction() {
    const std::filesystem::path blob =
        std::filesystem::path(APP_TEST_DATA_DIR) / "synthetic-props.bin";
    fixture::require_committed(blob);
    const std::vector<std::uint8_t> bytes = read_bytes(blob);
    check(!bytes.empty(), "the committed Props blob is not empty");
    const app::FormulaColumnSet load = app::read_file_formulas(bytes);
    check(load.warnings.empty(), "the committed Props blob extracts without warnings");
    check_size(load.columns.size(), 6, "the project stores six formulas");
    if (load.columns.size() != 6) {
        return;
    }
    // The wave-10 oracle's field-id association (measured, memory/props.md).
    const std::uint32_t expected_fields[] = {0x0b400033u, 0x0b400048u, 0x0b400057u,
                                             0x0b400058u, 0x0b400067u, 0x0b400109u};
    for (std::size_t index = 0; index < 6; ++index) {
        check(load.columns[index].field == expected_fields[index],
              "the formula targets the measured field id");
        check(load.columns[index].from_file, "the formula is marked from the file");
        check(load.columns[index].formula.find("[MSPJ") != std::string::npos,
              "the stored formula keeps its [MSPJ] field tokens");
        check(!load.columns[index].title.empty(), "the formula column has a caption");
        const core::formula::ParseResult parsed =
            core::formula::parse(load.columns[index].formula);
        check(parsed.ok, "the stored formula parses");
    }

    // The sheet's formula list is the file's six and no demo duplicate.
    const app::FormulaColumnSet columns = app::formula_columns(load);
    check_size(columns.columns.size(), 6, "no demo column duplicates a file formula");
    for (const app::FormulaColumn& column : columns.columns) {
        check(column.from_file, "every column is the file's own definition");
    }

    // An empty stream is "no formulas", not an error; garbage is reported.
    const app::FormulaColumnSet none = app::read_file_formulas({});
    check(none.columns.empty(), "an empty Props blob has no formulas");
    check(none.warnings.empty(), "an empty Props blob is not an error");
    const std::vector<std::uint8_t> garbage(16, 0u);
    const app::FormulaColumnSet broken = app::read_file_formulas(garbage);
    check(broken.columns.empty(), "an unreadable Props blob yields no formulas");
    check(!broken.warnings.empty(), "an unreadable Props blob is reported");
}

void test_formula_evaluation() {
    const std::filesystem::path blob =
        std::filesystem::path(APP_TEST_DATA_DIR) / "synthetic-props.bin";
    fixture::require_committed(blob);
    const std::vector<std::uint8_t> bytes = read_bytes(blob);
    check(!bytes.empty(), "the committed Props blob is not empty");
    const app::FormulaColumnSet columns = app::formula_columns(app::read_file_formulas(bytes));
    core::formula::ComputedColumns computed;
    std::string error;
    check(app::build_computed_columns(computed, columns.columns, error),
          "every extracted formula registers");
    check(error.empty(), "registration succeeded without an error");

    app::PlanInput plan;
    plan.tasks.push_back(make_task(1, "Alpha", 24000, 75, from_ymd(2026, 10, 19, 8, 0)));
    plan.tasks.push_back(make_task(2, "Bravo", 9600, 25, from_ymd(2026, 10, 20, 8, 0)));
    core::view::RecordSet records = app::make_record_set(plan);
    core::sheet::SheetFormat format;
    app::Preferences prefs;
    app::apply_preferences(prefs, format);
    computed.set_format(format);
    check_size(computed.evaluate(records), 6 * 2, "six formulas over two rows are evaluated");

    const std::uint32_t text1 = field_id(mpp::FieldKind::text, 1);
    const std::uint32_t flag1 = field_id(mpp::FieldKind::flag, 1);
    const std::uint32_t number1 = field_id(mpp::FieldKind::number, 1);
    const std::uint32_t number2 = field_id(mpp::FieldKind::number, 2);
    const std::uint32_t duration1 = field_id(mpp::FieldKind::duration, 1);
    const std::uint32_t date1 = field_id(mpp::FieldKind::date, 1);

    // Number1 = [Duration]*2 -> Duration * 2, still a Duration in ticks.
    core::view::Value value = computed.value_of_field(records, 0, number1);
    check(value.kind == core::view::ValueKind::Duration, "Number1 = [Duration]*2 is a Duration");
    check(value.integer == 48000, "2 x 24000 ticks");
    if (const core::view::Value* stored = records.get(0, number1); stored != nullptr) {
        check(stored->kind == core::view::ValueKind::Duration && stored->integer == 48000,
              "evaluate() writes Number1 into the RecordSet");
    }

    // Number2 = [Duration]/480 -> Duration / number, a Duration in ticks.
    value = computed.value_of_field(records, 0, number2);
    check(value.kind == core::view::ValueKind::Duration, "Number2 = [Duration]/480 is a Duration");
    check(value.integer == 50, "24000 / 480 ticks");

    // Duration1 = [Duration]/2.
    value = computed.value_of_field(records, 0, duration1);
    check(value.kind == core::view::ValueKind::Duration, "Duration1 = [Duration]/2 is a Duration");
    check(value.integer == 12000, "24000 / 2 ticks");

    // Date1 = [Start]+7 -> seven calendar days later.
    value = computed.value_of_field(records, 0, date1);
    check(value.kind == core::view::ValueKind::Date, "Date1 = [Start]+7 is a Date");
    check_date(app::date32_from_date_value(value.date), from_ymd(2026, 10, 26, 8, 0),
               "Start + 7 days preserves the 08:00 time");

    // Text1 = [Name] & "-" & [Duration]: the duration renders as text.
    value = computed.value_of_field(records, 0, text1);
    check(value.kind == core::view::ValueKind::Text, "Text1 concatenates to Text");
    check_text(value.text, "Alpha-" + core::sheet::format_duration_text(24000, format),
               "the name and the duration text are concatenated");

    // Flag1 = IIf([% Complete]>50,1,0).
    check(core::formula::truthy(computed.value_of_field(records, 0, flag1)),
          "75% complete makes the flag true");
    check(!core::formula::truthy(computed.value_of_field(records, 1, flag1)),
          "25% complete makes the flag false");

    // A formula column is read-only in the sheet and outside the save diff.
    core::sheet::SheetModel sheet(records, app::task_editor_columns(columns));
    app::apply_formula_read_only(sheet, columns.columns);
    const std::size_t number1_column = sheet.column_of_field(number1);
    check(number1_column != core::sheet::kNoIndex, "the formula column is in the sheet");
    check(sheet.column_read_only(number1_column), "the formula column is read-only");
    check(!sheet.begin_edit(0, number1_column), "a formula cell cannot be edited");
    check(app::collect_start_edits(records, plan).empty(),
          "computed columns are never a start-date save edit");
}

#else
void test_formula_extraction() {
    check(false, "APP_TEST_DATA_DIR is not defined; configure with "
                 "APP_BUILD_TESTS=ON (app/CMakeLists.txt)");
}
void test_formula_evaluation() {
    check(false, "APP_TEST_DATA_DIR is not defined; configure with "
                 "APP_BUILD_TESTS=ON (app/CMakeLists.txt)");
}
#endif

// ---------------------------------------------------------------------------
// 3. The column set, its visibility and the preference round trip
// ---------------------------------------------------------------------------

void test_column_set() {
    const app::FormulaColumnSet no_formulas;
    const std::vector<core::sheet::SheetColumn> plain = app::task_editor_columns(no_formulas);
    check_size(plain.size(), app::default_sheet_columns().size() + 6,
               "the plain column set is the default columns plus the six stored ones");

    app::FormulaColumnSet one_demo;
    app::FormulaColumn demo;
    demo.field = field_id(mpp::FieldKind::number, 1);
    demo.title = "Duration x2";
    demo.formula = "[Duration]*2";
    one_demo.columns.push_back(demo);
    const std::vector<core::sheet::SheetColumn> covered = app::task_editor_columns(one_demo);
    check_size(covered.size(), app::default_sheet_columns().size() + 6,
               "a formula column replaces the stored column for its own field");
    bool saw_formula = false;
    bool saw_stored_number = false;
    for (const core::sheet::SheetColumn& column : covered) {
        if (column.field == demo.field) {
            saw_formula = saw_formula || column.title == "Duration x2";
            saw_stored_number = true;
        }
    }
    check(saw_formula, "the formula column is present");
    check(saw_stored_number, "its field appears exactly once");
    std::size_t occurrences = 0;
    for (const core::sheet::SheetColumn& column : covered) {
        if (column.field == demo.field) {
            ++occurrences;
        }
    }
    check_size(occurrences, 1, "one column per field");

    check(app::is_custom_column(no_formulas, field_id(mpp::FieldKind::text, 1)),
          "a stored field is a custom column");
    check(app::is_custom_column(one_demo, demo.field), "a formula field is a custom column");
    check(!app::is_custom_column(no_formulas, app::name_field_id()),
          "Name is not a custom column");
}

void test_visibility_preferences() {
    app::Preferences prefs;
    const std::uint32_t text1 = field_id(mpp::FieldKind::text, 1);
    const std::uint32_t number1 = field_id(mpp::FieldKind::number, 1);
    check(!app::custom_column_default_visible(text1, prefs),
          "the stored columns start hidden");
    check(app::custom_column_default_visible(app::name_field_id(), prefs),
          "an ordinary column is visible by default");
    check(!app::column_visible(prefs, text1, false), "no override, default hidden");
    check(app::column_visible(prefs, text1, true), "no override, default shown");

    check(app::set_column_visible(prefs, text1, true, false), "an override is recorded");
    check(app::column_visible(prefs, text1, false), "the override wins over the default");
    check_size(prefs.column_visibility.size(), 1, "one override");
    const std::string line = app::serialize_preferences(prefs);
    check(line.find("columns=") != std::string::npos, "the line carries the column list");
    check(line.find("0x0b400033:1") != std::string::npos, "the override is the text1 field");
    app::Preferences parsed;
    check(app::parse_preferences(line, parsed), "the line parses");
    check(app::column_visible(parsed, text1, false), "the override round-trips");

    // A choice equal to the default is not an override.
    check(app::set_column_visible(prefs, text1, false, false), "resetting to the default works");
    check(prefs.column_visibility.empty(), "the override list is empty again");
    check(app::serialize_preferences(prefs).find("0x0b400033") == std::string::npos,
          "the stored line no longer names the field");

    // A malformed entry is skipped; the good entries still parse.
    app::Preferences tolerant;
    check(app::parse_preferences("columns=zzz:1,0x0b400057:1,0x0b400058:maybe", tolerant),
          "the parser reads the good entries");
    check_size(tolerant.column_visibility.size(), 1, "only the well-formed entry is kept");
    check(app::column_visible(tolerant, number1, false), "the good override applies");
    check(!app::set_column_visible(tolerant, 0, true, false), "field 0 is not storable");

    // The two demo formula flags still work and still serialize.
    check(app::formula_column_visible(prefs, 0) && app::formula_column_visible(prefs, 1),
          "the demo formula columns start visible");
    check(app::set_formula_column_visible(prefs, 0, false), "the demo column is storable");
    check(!app::formula_column_visible(prefs, 0), "the demo column is now hidden");
    check(app::serialize_preferences(prefs).find("formula1=0") != std::string::npos,
          "the demo flag is in the stored line");
    check(app::formula_column_visible(prefs, 7), "an index past the demos is visible");
    check(!app::set_formula_column_visible(prefs, 7, true),
          "an index past the demos is not storable");
}

void test_visibility_applies_to_sheet() {
    const app::PlanInput plan = make_stored_plan();
    core::view::RecordSet records = app::make_record_set(plan);
    const app::FormulaColumnSet no_formulas;
    core::sheet::SheetModel sheet(records, app::task_editor_columns(no_formulas));
    app::Preferences prefs;
    app::apply_column_visibility(sheet, no_formulas, prefs);
    const std::size_t text_column = sheet.column_of_field(field_id(mpp::FieldKind::text, 1));
    check(text_column != core::sheet::kNoIndex, "the Text1 column is in the sheet");
    check(!sheet.column_visible(text_column), "a stored column is hidden by default");
    check(sheet.column_visible(3), "the Name column is still visible");

    prefs.stored_custom_fields = true;
    app::apply_column_visibility(sheet, no_formulas, prefs);
    check(sheet.column_visible(text_column), "the stored columns follow their preference");
}

void test_build_computed_columns_errors() {
    core::formula::ComputedColumns computed;
    std::string error;
    app::FormulaColumn bad;
    bad.field = field_id(mpp::FieldKind::text, 1);
    bad.title = "Bad";
    bad.formula = "[[[";
    check(!app::build_computed_columns(computed, {bad}, error), "a bad formula is refused");
    check(!error.empty(), "the refusal explains itself");

    std::vector<app::FormulaColumn> good;
    app::FormulaColumn number;
    number.field = field_id(mpp::FieldKind::number, 1);
    number.title = "Duration x2";
    number.formula = "[Duration]*2";
    good.push_back(number);
    check(app::build_computed_columns(computed, good, error), "a good formula registers");
    check(error.empty(), "no error for a good formula");
    check_size(computed.size(), 1, "one computed column");
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_stored_column_specs();
    test_value_conversion();
    test_record_set_values();
    test_column_formatting();
    test_formula_extraction();
    test_formula_evaluation();
    test_column_set();
    test_visibility_preferences();
    test_visibility_applies_to_sheet();
    test_build_computed_columns_errors();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("app_fields_tests: %d checks passed\n", g_checks);
    return 0;
}
