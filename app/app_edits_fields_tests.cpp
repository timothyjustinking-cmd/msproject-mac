// app/app_edits_fields_tests.cpp -- writing task custom fields (wave 13).
//
// Two layers:
//   1. the Task Fields dialog MODEL (app/field_link_edits.h): the six measured
//      field specs, the initial text, parsing the text through the sheet's own
//      parser into the writer's types, the clear-vs-set rule, the push
//      validation and the display plan;
//   2. the real writer on a COPY of the committed synthetic fixture
//      (tests/data/synthetic-base.mpp, NOT client data -- see tests/data/
//      README.md) under APP_TEST_OUT_DIR: write all six fields through
//      mppwrite::save_field_values, re-read the file and check every value;
//      clear them through mppwrite::clear_field_value and check the reader
//      reports them absent; a field write combined with a task insert and a
//      start pin in one Save; and the refusals (unmeasured field, unknown uid,
//      target == source) leaving the source/target untouched.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\vs-w55 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\vs-w55 --config Release
//   ctest --test-dir build\vs-w55 -C Release -R "^app_edits_fields$" --output-on-failure

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "date.h"
#include "edit_model.h"
#include "field_link_edits.h"
#include "layout_model.h"
#include "reader_adapter.h"

namespace {

using app::date32;
using app::kDateNa;
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

std::size_t bytes_on_disk(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    return error ? 0 : static_cast<std::size_t>(size);
}

const std::uint32_t kText1 = mpp::custom_field_id(mpp::FieldKind::text, 1);
const std::uint32_t kNumber1 = mpp::custom_field_id(mpp::FieldKind::number, 1);
const std::uint32_t kNumber2 = mpp::custom_field_id(mpp::FieldKind::number, 2);
const std::uint32_t kDate1 = mpp::custom_field_id(mpp::FieldKind::date, 1);
const std::uint32_t kDuration1 = mpp::custom_field_id(mpp::FieldKind::duration, 1);
const std::uint32_t kFlag1 = mpp::custom_field_id(mpp::FieldKind::flag, 1);
// The measured Date1 value from the W10 oracle: 2026-10-26 08:00.
const std::uint32_t kMeasuredDate1 = 0x3d1812c0u;

mpp::CustomFieldValue make_value(std::uint32_t field_id, mpp::FieldKind kind) {
    mpp::CustomFieldValue value;
    value.field_id = field_id;
    value.kind = kind;
    value.present = true;
    value.storage_measured = true;
    return value;
}

app::TaskRow make_full_row() {
    app::TaskRow row;
    row.uid = 7;
    row.id = 2;
    row.name = "Task";
    row.start = from_ymd(2026, 10, 26, 8, 0);
    row.finish = row.start;
    mpp::CustomFieldValue text = make_value(kText1, mpp::FieldKind::text);
    text.text = "alpha";
    mpp::CustomFieldValue number1 = make_value(kNumber1, mpp::FieldKind::number);
    number1.number = 42.5;
    mpp::CustomFieldValue number2 = make_value(kNumber2, mpp::FieldKind::number);
    number2.number = 3.25;
    mpp::CustomFieldValue date = make_value(kDate1, mpp::FieldKind::date);
    date.packed_date = kMeasuredDate1;
    mpp::CustomFieldValue duration = make_value(kDuration1, mpp::FieldKind::duration);
    duration.duration_ticks = 4800;
    mpp::CustomFieldValue flag = make_value(kFlag1, mpp::FieldKind::flag);
    flag.flag = true;
    row.custom_fields = {text, number1, number2, date, duration, flag};
    return row;
}

const std::vector<app::TaskFieldInput> all_inputs(const std::string& text,
                                                  const std::string& number1,
                                                  const std::string& number2,
                                                  const std::string& date,
                                                  const std::string& duration,
                                                  const std::string& flag) {
    return {{kText1, text},   {kNumber1, number1}, {kNumber2, number2},
            {kDate1, date},   {kDuration1, duration}, {kFlag1, flag}};
}

const mpp::CustomFieldValue* find_field(const mpp::Task& task, std::uint32_t field_id) {
    for (const mpp::CustomFieldValue& value : task.custom_fields) {
        if (value.field_id == field_id) {
            return &value;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// The dialog model
// ---------------------------------------------------------------------------

void test_specs() {
    const std::vector<app::TaskFieldSpec>& specs = app::task_field_specs();
    check_size(specs.size(), 6, "the dialog has exactly the six measured fields");
    const std::uint32_t ids[] = {kText1, kNumber1, kNumber2, kDate1, kDuration1, kFlag1};
    const mpp::FieldKind kinds[] = {mpp::FieldKind::text, mpp::FieldKind::number,
                                    mpp::FieldKind::number, mpp::FieldKind::date,
                                    mpp::FieldKind::duration, mpp::FieldKind::flag};
    for (std::size_t index = 0; index < specs.size() && index < 6; ++index) {
        ++g_checks;
        if (specs[index].field_id != ids[index] || specs[index].kind != kinds[index] ||
            specs[index].title.empty()) {
            ++g_failures;
            std::printf("FAIL: field spec %llu\n",
                        static_cast<unsigned long long>(index));
        }
        check(app::task_field_spec(ids[index]) != nullptr, "the spec is addressable by id");
        check(app::is_measured_task_field(ids[index]), "the id is measured");
    }
    check_text(specs[0].title, "Text1", "Text1's caption comes from the descriptor table");
    check_text(specs[5].title, "Flag1", "Flag1's caption comes from the descriptor table");
    check(!app::is_measured_task_field(0), "id 0 is not measured");
    check(!app::is_measured_task_field(mpp::custom_field_id(mpp::FieldKind::cost, 1)),
          "Cost1 has no measured storage");
    check(!app::is_measured_task_field(mpp::custom_field_id(mpp::FieldKind::flag, 2)),
          "Flag2's bit is not measured");
}

void test_initial_text() {
    const app::TaskRow row = make_full_row();
    const core::sheet::SheetFormat format{};
    check_text(app::field_value_text(row, kText1, format), "alpha", "Text1 initial text");
    check_text(app::field_value_text(row, kNumber1, format), "42.5", "Number1 initial text");
    check_text(app::field_value_text(row, kNumber2, format), "3.25", "Number2 initial text");
    check_text(app::field_value_text(row, kDate1, format), "Mon 26/10/26",
               "Date1 initial text (the date preference)");
    check_text(app::field_value_text(row, kDuration1, format), "1 day",
               "Duration1 initial text (the duration preference)");
    check_text(app::field_value_text(row, kFlag1, format), "Yes", "Flag1 initial text");
    app::TaskRow empty;
    empty.uid = 1;
    check_text(app::field_value_text(empty, kText1, format), "", "an absent field shows blank");
    check_text(app::field_value_text(row, 0, format), "", "an unknown field shows blank");
}

void test_make_edits() {
    const app::TaskRow row = make_full_row();
    const core::sheet::SheetFormat format{};
    std::vector<app::StructuralEdit> edits;
    std::string error;

    // Unchanged text -> no pending edit at all.  The date carries its time (the
    // measured Date1 is 08:00), so the unchanged form really is byte-identical.
    check(app::make_task_field_edits(row, all_inputs("alpha", "42.5", "3.25",
                                                     "2026-10-26T08:00", "1d", "Yes"),
                                     format, edits, error),
          "the unchanged form is accepted");
    check(edits.empty(), "an unchanged form queues nothing");
    check(error.empty(), "an unchanged form has no error");

    // Every field changed.
    check(app::make_task_field_edits(row, all_inputs("beta", "7", "2.5", "2026-10-27",
                                                     "4h", "No"),
                                     format, edits, error),
          "the changed form is accepted");
    check_size(edits.size(), 6, "six fields changed -> six pending edits");
    if (edits.size() == 6) {
        check(edits[0].kind == app::StructuralKind::set_field, "Text1 is a set edit");
        check(edits[0].uid == 7, "the edit carries the task uid");
        check(edits[0].field_id == kText1, "the edit carries the field id");
        check_text(edits[0].field_value.text, "beta", "the text value is parsed");
        check(edits[1].field_value.number == 7.0, "an integer is a valid number");
        check(edits[2].field_value.number == 2.5, "Number2 is parsed");
        check(edits[3].field_value.packed_date ==
                  app::packed_from_date32(from_ymd(2026, 10, 27, 0, 0)),
              "Date1 is stored as the packed date word");
        check(edits[4].field_value.duration_ticks == 2400,
              "4h is 2400 file ticks (COM 240 minutes * 10)");
        check(!edits[5].field_value.flag, "Flag1 No is false");
    }

    // Blank clears a stored value; blank on an absent value queues nothing.
    check(app::make_task_field_edits(row, all_inputs("", "", "", "", "", ""), format, edits,
                                     error),
          "an all-blank form is accepted");
    check_size(edits.size(), 6, "six stored values -> six clears");
    for (const app::StructuralEdit& edit : edits) {
        check(edit.kind == app::StructuralKind::clear_field, "a blank field is a clear edit");
    }
    app::TaskRow empty;
    empty.uid = 8;
    check(app::make_task_field_edits(empty, all_inputs("", "", "", "", "", ""), format, edits,
                                     error),
          "an all-blank form on an empty row is accepted");
    check(edits.empty(), "clearing absent values queues nothing");

    // Parse errors refuse the whole form and name the field.
    check(!app::make_task_field_edits(row, all_inputs("x", "not-a-number", "", "", "", ""),
                                      format, edits, error),
          "a bad number is refused");
    check(error.find("Number1") != std::string::npos, "the number error names the field");
    check(edits.empty(), "a refused form queues nothing");
    check(!app::make_task_field_edits(row, all_inputs("x", "", "", "not-a-date", "", ""),
                                      format, edits, error),
          "a bad date is refused");
    check(error.find("Date1") != std::string::npos, "the date error names the field");
    check(!app::make_task_field_edits(row, all_inputs("x", "", "", "", "nope", ""), format,
                                      edits, error),
          "a bad duration is refused");
    check(!app::make_task_field_edits(row, all_inputs("x", "", "", "", "", "maybe"), format,
                                      edits, error),
          "a bad Yes/No value is refused");
    std::vector<app::TaskFieldInput> unknown = {{0x0b40006a, "1"}};  // Cost1
    check(!app::make_task_field_edits(row, unknown, format, edits, error),
          "an unmeasured field is refused");
    check(!error.empty(), "the unmeasured-field refusal explains itself");
}

void test_push_validation() {
    app::StructuralEdits edits;
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::set_field;
    edit.uid = 7;
    edit.field_id = kNumber1;
    edit.field_value.number = 1.0;
    check(edits.push(edit), "a valid number edit is accepted");
    edit.field_value.number = std::nan("");
    app::StructuralEdit nan_edit = edit;
    check(!edits.push(nan_edit), "a non-finite number is refused");
    app::StructuralEdit cost;
    cost.kind = app::StructuralKind::set_field;
    cost.uid = 7;
    cost.field_id = mpp::custom_field_id(mpp::FieldKind::cost, 1);
    check(!edits.push(cost), "an unmeasured field id is refused");
    app::StructuralEdit bad_date;
    bad_date.kind = app::StructuralKind::set_field;
    bad_date.uid = 7;
    bad_date.field_id = kDate1;
    bad_date.field_value.packed_date = 0xffffffffu;
    check(!edits.push(bad_date), "an invalid packed date is refused");
    app::StructuralEdit bad_text;
    bad_text.kind = app::StructuralKind::set_field;
    bad_text.uid = 7;
    bad_text.field_id = kText1;
    bad_text.field_value.text = std::string("a\0b", 3);
    check(!edits.push(bad_text), "text with an embedded NUL is refused");
    app::StructuralEdit clear;
    clear.kind = app::StructuralKind::clear_field;
    clear.uid = 7;
    clear.field_id = kFlag1;
    check(edits.push(clear), "a clear is accepted");
    check_size(edits.field_edit_count(), 2, "two field edits are pending");
    check(edits.undo(), "undo moves the clear");
    check(edits.redo(), "redo applies it again");
    check_size(edits.field_edit_count(), 2, "the edit count is unchanged by undo/redo");

    // A pending delete drops the field edits for that task.  W33-1.1 (A-3):
    // the delete is checked against the plan, which holds the leaf row it
    // names.
    app::PlanInput plan;
    app::TaskRow leaf;
    leaf.uid = 7;
    leaf.file_row = 1;
    plan.tasks.push_back(leaf);
    app::StructuralEdit remove;
    remove.kind = app::StructuralKind::delete_task;
    remove.uid = 7;
    check(edits.push(remove, &plan), "the delete is accepted");
    check_size(edits.field_edit_count(), 0, "the delete dropped its field edits");
}

void test_display_plan() {
    app::PlanInput plan;
    plan.tasks.push_back(make_full_row());
    app::TaskRow other;
    other.uid = 9;
    other.id = 3;
    other.name = "Other";
    plan.tasks.push_back(other);

    app::StructuralEdits edits;
    app::StructuralEdit set;
    set.kind = app::StructuralKind::set_field;
    set.uid = 7;
    set.field_id = kText1;
    set.field_value.text = "beta";
    check(edits.push(set), "the Text1 set is queued");
    app::StructuralEdit clear;
    clear.kind = app::StructuralKind::clear_field;
    clear.uid = 7;
    clear.field_id = kNumber1;
    check(edits.push(clear), "the Number1 clear is queued");

    const app::PlanInput shown = edits.apply(plan);
    check_size(shown.tasks.size(), 2, "the display plan keeps both tasks");
    const mpp::CustomFieldValue* text = shown.tasks[0].custom_field(kText1);
    check(text != nullptr && text->present && text->text == "beta",
          "the pending Text1 value is displayed");
    check(shown.tasks[0].custom_field(kNumber1) == nullptr,
          "the pending Number1 clear is displayed as absent");
    check(plan.tasks[0].custom_field(kText1)->text == "alpha",
          "apply() does not modify the loaded plan");
    check(edits.undo(), "undo the clear");
    check(edits.undo(), "undo the set");
    const app::PlanInput original = edits.apply(plan);
    check(original.tasks[0].custom_field(kText1)->text == "alpha",
          "undo restores the displayed value");
    check(original.tasks[0].custom_field(kNumber1) != nullptr, "undo restores the number");
    check(edits.redo() && edits.redo(), "redo both edits");
    check(edits.apply(plan).tasks[0].custom_field(kText1)->text == "beta",
          "redo applies the value again");
}

// ---------------------------------------------------------------------------
// The real writer sequence
// ---------------------------------------------------------------------------

#if defined(APP_TEST_DATA_DIR) && defined(APP_TEST_OUT_DIR)

struct Loaded {
    app::PlanInput plan;
    mpp::Document document;
    std::string path;
    bool ok = false;
};

Loaded load(const std::filesystem::path& path) {
    Loaded loaded;
    loaded.path = path.string();
    try {
        loaded.document = mpp::load_file(loaded.path);
        loaded.plan = app::plan_input_from_document(loaded.document);
        loaded.ok = true;
    } catch (const std::exception& error) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: cannot load %s: %s\n", loaded.path.c_str(), error.what());
    }
    return loaded;
}

void test_real_round_trip() {
    const std::filesystem::path fixture =
        std::filesystem::path(APP_TEST_DATA_DIR) / "synthetic-base.mpp";
    if (!std::filesystem::exists(fixture)) {
        std::printf("SKIP: %s is not present; the field round trip needs the fixture\n",
                    fixture.string().c_str());
        return;
    }
    const std::size_t fixture_bytes = bytes_on_disk(fixture);
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);
    const std::filesystem::path copy =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-fields-source.mpp";
    std::filesystem::remove(copy, error);
    std::filesystem::copy_file(fixture, copy, std::filesystem::copy_options::overwrite_existing,
                               error);
    check(!error, "the fixture was copied into the build tree");
    const std::size_t copy_bytes = bytes_on_disk(copy);

    Loaded base = load(copy);
    if (!base.ok || base.plan.tasks.empty()) {
        return;
    }
    check_size(base.plan.tasks.size(), 3, "the fixture has three tasks");
    const std::uint32_t uid = base.plan.tasks.front().uid;
    const core::sheet::SheetFormat format{};

    // Write all six values.
    std::vector<app::TaskFieldInput> inputs = {
        {kText1, "alpha"}, {kNumber1, "42.5"}, {kNumber2, "3.25"},
        {kDate1, "2026-10-26T08:00"}, {kDuration1, "1d"}, {kFlag1, "Yes"}};
    std::vector<app::StructuralEdit> form_edits;
    std::string form_error;
    check(app::make_task_field_edits(base.plan.tasks.front(), inputs, format, form_edits,
                                     form_error),
          "the six-field form is accepted");
    check_size(form_edits.size(), 6, "six values are queued");
    app::StructuralEdits edits;
    for (const app::StructuralEdit& edit : form_edits) {
        check(edits.push(edit), "each field edit is accepted");
    }
    check_size(edits.field_edit_count(), 6, "six field edits are pending");
    check_size(edits.apply(base.plan).tasks.front().custom_fields.size(), 6,
               "the display plan shows six values");

    const std::filesystem::path written =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-fields-written.mpp";
    std::filesystem::remove(written, error);
    const app::StructuralSaveResult outcome =
        app::save_structural_edits(edits, {}, copy.string(), written.string(), false);
    check(outcome.ok, "the field write saves");
    if (!outcome.ok) {
        std::printf("SAVE ERROR: %s\n", outcome.error.c_str());
        return;
    }
    check_size(outcome.fields_written, 6, "six field values were written");
    check(outcome.error.empty(), "a successful field save has no error");
    check(outcome.api.find("save_field_values") != std::string::npos,
          "the outcome names save_field_values");
    check_size(bytes_on_disk(copy), copy_bytes, "the source copy is untouched");
    check(std::filesystem::exists(written), "the written file exists");
    check(!std::filesystem::exists(written.string() + ".w51-structural-tmp"),
          "no intermediate file is left behind");

    // Reload (the shell's adopt step) and check every stored value from the file.
    Loaded reread = load(written);
    if (reread.ok) {
        check_size(reread.plan.tasks.size(), 3, "the written file still has three tasks");
        const mpp::Task* task = nullptr;
        for (const mpp::Task& candidate : reread.document.tasks) {
            if (candidate.uid == uid) {
                task = &candidate;
            }
        }
        check(task != nullptr, "the edited task reads back");
        const mpp::Task& row = task != nullptr ? *task : reread.document.tasks.front();
        const mpp::CustomFieldValue* text = find_field(row, kText1);
        const mpp::CustomFieldValue* number1 = find_field(row, kNumber1);
        const mpp::CustomFieldValue* number2 = find_field(row, kNumber2);
        const mpp::CustomFieldValue* date = find_field(row, kDate1);
        const mpp::CustomFieldValue* duration = find_field(row, kDuration1);
        const mpp::CustomFieldValue* flag = find_field(row, kFlag1);
        check(text != nullptr && text->present && text->text == "alpha",
              "the reader shows Text1");
        check(number1 != nullptr && number1->present && number1->number == 42.5,
              "the reader shows Number1");
        check(number2 != nullptr && number2->present && number2->number == 3.25,
              "the reader shows Number2");
        check(date != nullptr && date->present && date->packed_date == kMeasuredDate1,
              "the reader shows Date1 as the measured packed date");
        check(duration != nullptr && duration->present &&
                  duration->duration_ticks == 4800u,
              "the reader shows Duration1 as 4800 file ticks");
        check(duration != nullptr && duration->duration_minutes() == 480u,
              "Duration1 is 480 COM minutes (ticks = minutes * 10)");
        check(flag != nullptr && flag->present && flag->flag, "the reader shows Flag1 Yes");
        const app::TaskRow& plan_row = reread.plan.tasks.front();
        check(plan_row.custom_field(kText1) != nullptr,
              "the app plan carries the written value too");
    }

    // Clear all six through the measured empty-value shape.
    std::vector<app::TaskFieldInput> blanks = {{kText1, ""}, {kNumber1, ""}, {kNumber2, ""},
                                               {kDate1, ""}, {kDuration1, ""}, {kFlag1, ""}};
    std::vector<app::StructuralEdit> clear_form;
    check(app::make_task_field_edits(reread.plan.tasks.front(), blanks, format, clear_form,
                                     form_error),
          "the clear form is accepted");
    check_size(clear_form.size(), 6, "six clears are queued");
    app::StructuralEdits clears;
    for (const app::StructuralEdit& edit : clear_form) {
        check(clears.push(edit), "each clear is accepted");
    }
    const std::filesystem::path cleared =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-fields-cleared.mpp";
    std::filesystem::remove(cleared, error);
    const app::StructuralSaveResult clear_outcome =
        app::save_structural_edits(clears, {}, written.string(), cleared.string(), false);
    check(clear_outcome.ok, "the clear saves");
    if (clear_outcome.ok) {
        check_size(clear_outcome.fields_written, 6, "six fields were cleared");
        check(clear_outcome.api.find("clear_field_value") != std::string::npos,
              "the outcome names clear_field_value");
        Loaded after_clear = load(cleared);
        if (after_clear.ok) {
            const mpp::Task& row = after_clear.document.tasks.front();
            // clear_field_value keeps the VarMeta entry and points it at the
            // measured empty block (W52: Project shows blank because the
            // FixedMeta tail presence bits are cleared).  This reader lists the
            // entry, so the reader-level contract is the EMPTY value; the app's
            // value mapping turns Date1 into Null and Text1 into blank text.
            const mpp::CustomFieldValue* text = find_field(row, kText1);
            check(text != nullptr && text->text.empty(),
                  "Text1 reads as empty after the clear");
            const mpp::CustomFieldValue* number1 = find_field(row, kNumber1);
            check(number1 != nullptr && number1->number == 0.0,
                  "Number1 reads as the empty number after the clear");
            const mpp::CustomFieldValue* number2 = find_field(row, kNumber2);
            check(number2 != nullptr && number2->number == 0.0,
                  "Number2 reads as the empty number after the clear");
            const mpp::CustomFieldValue* date = find_field(row, kDate1);
            check(date != nullptr && date->packed_date == 0xffffffffu,
                  "Date1 reads as the writer's not-set word after the clear");
            check(date != nullptr && app::value_of_custom_field(*date).is_null(),
                  "the app shows a cleared Date1 as blank");
            const mpp::CustomFieldValue* duration = find_field(row, kDuration1);
            check(duration != nullptr && duration->duration_ticks == 0u,
                  "Duration1 reads as zero ticks after the clear");
            const mpp::CustomFieldValue* flag = find_field(row, kFlag1);
            check(flag == nullptr || !flag->flag, "Flag1 is No after the clear");
        }
    }

    // Field + structural in one Save: the field write runs after the insert on
    // one target.  W32-0.2 (R31-2, High): a Start edit used to ride along; it is
    // refused before the first write now (checked below), so this save carries
    // no start.
    app::StructuralEdits combined;
    app::StructuralEdit set_text;
    set_text.kind = app::StructuralKind::set_field;
    set_text.uid = uid;
    set_text.field_id = kText1;
    set_text.field_value.text = "combined";
    check(combined.push(set_text), "the combined field edit is queued");
    app::StructuralEdit insert;
    insert.kind = app::StructuralKind::insert_task;
    insert.name = "Field Task";
    insert.uid = combined.next_placeholder_uid(base.plan);
    check(combined.push(insert), "the combined insert is queued");
    const date32 new_start = from_ymd(2026, 3, 2, 8, 0);
    const std::filesystem::path both =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-fields-combined.mpp";
    std::filesystem::remove(both, error);
    const app::StructuralSaveResult combined_outcome =
        app::save_structural_edits(combined, {}, copy.string(), both.string(), false);
    check(combined_outcome.ok, "field + insert save together");
    if (combined_outcome.ok) {
        check_size(combined_outcome.added, 1, "the insert landed");
        check_size(combined_outcome.fields_written, 1, "the field landed");
        check_size(combined_outcome.start_rows_written, 0, "no start was written");
        check(combined_outcome.api.find("save_field_values") != std::string::npos,
              "the combined save ran the field writer");
        Loaded after_combined = load(both);
        if (after_combined.ok) {
            check_size(after_combined.plan.tasks.size(), 4, "the combined file has four tasks");
            bool found = false;
            for (const mpp::Task& task : after_combined.document.tasks) {
                if (task.uid != uid) {
                    continue;
                }
                found = true;
                const mpp::CustomFieldValue* text = find_field(task, kText1);
                check(text != nullptr && text->text == "combined",
                      "the field value is in the combined file");
            }
            check(found, "the edited task is in the combined file");
        }
    }

    // W32-0.2 (R31-2, High): the same queue plus a Start edit is refused before
    // the first write, so neither the field nor the insert lands.
    {
        std::vector<app::StartEdit> starts;
        app::StartEdit start_edit;
        start_edit.uid = uid;
        start_edit.start = new_start;
        starts.push_back(start_edit);
        const std::filesystem::path refused_target =
            std::filesystem::path(APP_TEST_OUT_DIR) / "app-fields-start-refused.mpp";
        std::filesystem::remove(refused_target, error);
        const app::StructuralSaveResult refused = app::save_structural_edits(
            combined, starts, copy.string(), refused_target.string(), false);
        check(!refused.ok, "a field + insert + start save is refused");
        check(refused.error.find("Start is read-only") != std::string::npos,
              "the refusal names Start");
        check(refused.error.find("not measured against Project") != std::string::npos,
              "the refusal names decision 9");
        check(!std::filesystem::exists(refused_target),
              "the refused save leaves no target file");
    }

    // Refusals -----------------------------------------------------------------
    // (a) an unknown uid fails before the target is written.
    app::StructuralEdits bogus;
    app::StructuralEdit bogus_edit;
    bogus_edit.kind = app::StructuralKind::set_field;
    bogus_edit.uid = 999999u;
    bogus_edit.field_id = kText1;
    bogus_edit.field_value.text = "x";
    check(bogus.push(bogus_edit), "the bogus field edit is queued");
    const std::filesystem::path bogus_target =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-fields-bogus.mpp";
    std::filesystem::remove(bogus_target, error);
    const app::StructuralSaveResult bogus_outcome =
        app::save_structural_edits(bogus, {}, copy.string(), bogus_target.string(), false);
    check(!bogus_outcome.ok, "a field write to an unknown uid is refused");
    check(!bogus_outcome.error.empty(), "the unknown-uid refusal explains itself");
    check(!std::filesystem::exists(bogus_target),
          "the refused field save leaves no target file");

    // (b) target == source is refused and the source is untouched.
    const app::StructuralSaveResult same = app::save_structural_edits(
        edits, {}, copy.string(), copy.string(), false);
    check(!same.ok, "writing fields over the opened file is refused");
    check_size(bytes_on_disk(copy), copy_bytes, "the refused save left the source untouched");

    // (c) a pre-existing target is left byte-identical when the writer refuses.
    const std::filesystem::path guarded =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-fields-guarded.mpp";
    std::filesystem::remove(guarded, error);
    std::filesystem::copy_file(fixture, guarded, std::filesystem::copy_options::overwrite_existing,
                               error);
    const std::size_t guarded_bytes = bytes_on_disk(guarded);
    const app::StructuralSaveResult guarded_outcome = app::save_structural_edits(
        bogus, {}, copy.string(), guarded.string(), false);
    check(!guarded_outcome.ok, "the refusal fires with a pre-existing target");
    check_size(bytes_on_disk(guarded), guarded_bytes,
               "the pre-existing target is byte-identical after the refusal");
    check(!std::filesystem::exists(guarded.string() + ".w51-structural-tmp"),
          "the refusal removed the intermediate file");

    check_size(bytes_on_disk(fixture), fixture_bytes,
               "the committed fixture is never written");
    check_size(bytes_on_disk(copy), copy_bytes, "the source copy is untouched by every save");
}

#else
void test_real_round_trip() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
}
#endif

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_specs();
    test_initial_text();
    test_make_edits();
    test_push_validation();
    test_display_plan();
    test_real_round_trip();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("app_edits_fields_tests: %d checks passed\n", g_checks);
    return 0;
}
