// app/smoke.cpp -- the portable half of the self-smoke.  See app/smoke.h for why
// it exists and what it deliberately cannot prove.
//
// THE RULE THIS FILE FOLLOWS: every step calls the SAME portable function the
// shell calls for that action, in the same order, with the same arguments --
// there is no second load path, no second save path and no second print layout
// here.  If a function below ever diverges from app/main.mm, the divergence is a
// bug in one of them, not a "test copy" of the behaviour.
//
// THE FIXTURE
//   `tests/data/synthetic-base.mpp` (3 tasks, 3 assignments, 0 links; task 1 is
//   stored as manually scheduled) is the input.  It is copied to
//   `<out>/smoke-input.mpp` and the copy is what is loaded; the saved file is
//   `<out>/smoke-edited.mpp`.  The committed fixture is never opened for writing,
//   and the run checks its bytes are unchanged at the end.
//
// DETERMINISM
//   No clock is read: `SmokeOptions::date_text` supplies the print footer date.
//   The preferences are the defaults (app::Preferences{}), because the shell's
//   persisted preferences are user state and a smoke run must not depend on them.
//
// WHAT A FAILURE LOOKS LIKE
//   "FAIL: [save] the save wrote one row: got 0, want 1".  The step name is part
//   of the message, and the same text goes into smoke.json.

#include "smoke.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "date.h"
#include "document.h"
#include "edit_model.h"
#include "export_pdf.h"
#include "field_link_edits.h"
#include "fields_model.h"
#include "filter_model.h"
#include "gantt.h"
#include "layout_model.h"
#include "page_layout.h"
#include "prefs.h"
#include "reader_adapter.h"
#include "record_set.h"
#include "sheet.h"
#include "start_edit_warning.h"

namespace app {

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void smoke_begin_step(SmokeReport& report, std::string step) {
    if (step.empty()) {
        return;
    }
    if (std::find(report.steps.begin(), report.steps.end(), step) == report.steps.end()) {
        report.steps.push_back(std::move(step));
    }
}

void smoke_add_check(SmokeReport& report, std::string_view step, std::string_view name, bool ok,
                     std::string detail) {
    smoke_begin_step(report, std::string(step));
    SmokeCheck check;
    check.step = std::string(step);
    check.name = std::string(name);
    check.ok = ok;
    check.detail = std::move(detail);
    report.checks.push_back(std::move(check));
    if (ok) {
        ++report.checks_passed;
    } else {
        ++report.checks_failed;
    }
}

void smoke_add_artifact(SmokeReport& report, std::string name, std::string path) {
    for (const SmokeArtifact& existing : report.artifacts) {
        if (existing.name == name) {
            return;  // one entry per artifact name; the first path written wins
        }
    }
    SmokeArtifact artifact;
    artifact.name = std::move(name);
    artifact.path = std::move(path);
    report.artifacts.push_back(std::move(artifact));
}

namespace {

namespace fs = std::filesystem;

using core::sched::add_days;
using core::sched::from_ymd;
using core::sched::is_na;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

bool read_all(const fs::path& path, std::string& bytes) {
    bytes.clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        return false;
    }
    bytes = buffer.str();
    return true;
}

std::size_t bytes_on_disk(const fs::path& path) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    return error ? 0 : static_cast<std::size_t>(size);
}

std::string joined(const std::string& directory, const char* name) {
    return (fs::path(directory) / name).string();
}

std::string date_text(app::date32 day, const core::sheet::SheetFormat& format) {
    if (is_na(day)) {
        return "NA";
    }
    return core::sheet::format_date_text(app::date_value_from_date32(day), format);
}

const char* warning_kind_name(app::StartEditWarningKind kind) noexcept {
    switch (kind) {
        case app::StartEditWarningKind::none: return "none";
        case app::StartEditWarningKind::clamped_to_predecessor: return "clamped_to_predecessor";
        case app::StartEditWarningKind::manual_task_will_be_rescheduled:
            return "manual_task_will_be_rescheduled";
        case app::StartEditWarningKind::no_effect: return "no_effect";
    }
    return "?";
}

// One step of the sequence: registers itself and stamps every check with its name,
// which is what makes a failure say WHERE it happened.
class Step {
  public:
    Step(SmokeReport& report, const char* name) : report_(report), name_(name) {
        smoke_begin_step(report_, name_);
    }

    void check(bool passed, std::string_view what) const {
        smoke_add_check(report_, name_, what, passed);
    }

    void check(bool passed, std::string_view what, std::string detail) const {
        smoke_add_check(report_, name_, what, passed, std::move(detail));
    }

    void count(std::size_t got, std::size_t want, std::string_view what) const {
        const bool same = got == want;
        check(same, what,
              same ? std::string()
                   : "got " + std::to_string(got) + ", want " + std::to_string(want));
    }

    void text(const std::string& got, const std::string& want, std::string_view what) const {
        const bool same = got == want;
        check(same, what,
              same ? std::string() : "got \"" + got + "\", want \"" + want + "\"");
    }

    void date(app::date32 got, app::date32 want, std::string_view what,
              const core::sheet::SheetFormat& format) const {
        const bool same = got == want;
        check(same, what,
              same ? std::string() : "got " + date_text(got, format) + ", want " +
                                         date_text(want, format));
    }

  private:
    SmokeReport& report_;
    std::string name_;
};

// ---------------------------------------------------------------------------
// The loaded plan, built exactly as app/main.mm's load path builds it
// (reader -> PlanInput -> RecordSet -> computed columns -> SheetModel), minus the
// AppKit view state.
// ---------------------------------------------------------------------------

struct LoadedPlan {
    mpp::Document document;
    app::PlanInput loaded;
    core::view::RecordSet records{core::model::RecordType::Task};
    std::unique_ptr<core::sheet::SheetModel> sheet;
    core::sheet::SheetFormat format;
    app::FormulaColumnSet formula_set;
    core::formula::ComputedColumns computed;
    std::size_t warnings = 0;
    std::size_t skipped_links = 0;
};

bool load_plan_file(const std::string& path, const app::Preferences& preferences,
                    LoadedPlan& out, std::string& error) {
    try {
        out.document = mpp::load_file(path);
        out.warnings = out.document.warnings.size();
        out.loaded = app::plan_input_from_document(out.document);
        out.loaded.source_name = fs::path(path).filename().string();
        out.skipped_links = out.loaded.skipped_links;
        out.format = core::sheet::SheetFormat{};
        app::apply_preferences(preferences, out.format);
        // The file's own `114/Props` formulas, as app/main.mm's load path does.
        out.formula_set =
            app::formula_columns(app::read_file_formulas(app::props_stream_from_file(path)));
        out.records = app::make_record_set(out.loaded);
        out.computed = core::formula::ComputedColumns{};
        std::string computed_error;
        (void)app::build_computed_columns(out.computed, out.formula_set.columns, computed_error);
        out.computed.set_format(out.format);
        out.computed.evaluate(out.records);
        out.sheet = std::make_unique<core::sheet::SheetModel>(
            out.records, app::task_editor_columns(out.formula_set));
        out.sheet->set_format(out.format);
        app::apply_edit_policy(*out.sheet);
        app::apply_formula_read_only(*out.sheet, out.formula_set.columns);
        app::apply_column_visibility(*out.sheet, out.formula_set, preferences);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

// The model column of a field, or kNoIndex when the sheet has no such column.
std::size_t column_of(const core::sheet::SheetModel& sheet, const char* field_name) {
    const core::view::FieldId field = app::task_field_id(field_name);
    if (field == 0) {
        return core::sheet::kNoIndex;
    }
    return sheet.column_of_field(field);
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

std::string json_escaped(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        switch (character) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte < 0x20) {
                    char buffer[8] = {};
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(byte));
                    out += buffer;
                } else {
                    out += character;
                }
                break;
        }
    }
    return out;
}

std::string json_string(std::string_view text) { return "\"" + json_escaped(text) + "\""; }

std::string json_bool(bool value) { return value ? "true" : "false"; }

}  // namespace

// ---------------------------------------------------------------------------
// The portable sequence
// ---------------------------------------------------------------------------

SmokeReport smoke_plan(const SmokeOptions& options) {
    SmokeReport report;
    report.fixture_path = options.fixture_path;
    report.out_dir = options.out_dir;

    const app::Preferences preferences{};
    const fs::path out_dir(options.out_dir);

    // ---- 1. fixture -------------------------------------------------------
    {
        const Step step(report, "fixture");
        std::string fixture_bytes;
        const bool fixture_read = read_all(options.fixture_path, fixture_bytes);
        step.check(fixture_read, "the committed fixture can be read", options.fixture_path);
        if (!fixture_read) {
            report.error = "fixture: cannot read " + options.fixture_path;
            return report;
        }

        std::error_code file_error;
        fs::create_directories(out_dir, file_error);
        step.check(!file_error && fs::is_directory(out_dir),
                   "the output directory exists",
                   file_error ? file_error.message() : std::string());

        const fs::path input_path = out_dir / kSmokeInputFile;
        fs::remove(input_path, file_error);
        file_error.clear();
        fs::copy_file(fs::path(options.fixture_path), input_path,
                      fs::copy_options::overwrite_existing, file_error);
        step.check(!file_error, "the fixture is copied into the output directory",
                   file_error ? file_error.message() : std::string());
        step.count(bytes_on_disk(input_path), fixture_bytes.size(),
                   "the copy is the same size as the fixture");

        std::string copy_bytes;
        step.check(read_all(input_path, copy_bytes) && copy_bytes == fixture_bytes,
                   "the copy is byte-identical to the fixture");
        smoke_add_artifact(report, "input", input_path.string());
    }

    const fs::path input_path = out_dir / kSmokeInputFile;
    std::string fixture_bytes;
    (void)read_all(options.fixture_path, fixture_bytes);

    // ---- 2. load ----------------------------------------------------------
    LoadedPlan plan;
    {
        const Step step(report, "load");
        std::string load_error;
        if (!load_plan_file(input_path.string(), preferences, plan, load_error)) {
            step.check(false, "the copied fixture loads through the reader", load_error);
            report.error = "load: " + load_error;
            return report;
        }
        step.check(true, "the copied fixture loads through the reader");
        step.count(plan.loaded.tasks.size(), 3, "the fixture has three tasks");
        step.count(plan.loaded.assignments.size(), 3, "the fixture has three assignments");
        step.count(plan.loaded.links.size(), 0, "the fixture has no link rows");
        step.count(plan.skipped_links, 0, "no link endpoint is missing");
        step.check(!plan.loaded.resources.empty(), "the fixture has resource rows");
        step.check(!plan.loaded.title.empty(), "the project has a title");
        step.count(plan.records.row_count(), plan.loaded.tasks.size(),
                   "the task RecordSet has one row per task");
        step.count(plan.sheet->row_count(), 3, "the sheet model has three rows");
        step.count(plan.sheet->visible_row_count(), 3, "the sheet shows every row");
        if (plan.loaded.tasks.size() < 2) {
            step.check(false, "the fixture has at least two tasks to drive the sequence",
                       "got " + std::to_string(plan.loaded.tasks.size()));
            report.error = "load: the fixture needs at least two tasks";
            return report;
        }
        step.check(plan.sheet->column_count() >= 7,
                   "the sheet has the seven plan columns plus any custom ones",
                   "got " + std::to_string(plan.sheet->column_count()));
        step.check(app::task_field_id("Start") != 0, "Task.Start resolves to a field id");
        step.check(!is_na(plan.loaded.tasks.front().start), "the first task has a start date");
        step.count(plan.records.uid(0), plan.loaded.tasks.front().uid,
                   "the RecordSet row carries the task's UniqueID");
        const std::size_t start_column = column_of(*plan.sheet, "Start");
        step.check(start_column != core::sheet::kNoIndex, "the sheet has a Start column");
        if (start_column != core::sheet::kNoIndex) {
            const app::SheetRowText expected = app::sheet_row_text(plan.loaded.tasks.front(),
                                                                   plan.format);
            step.text(plan.sheet->cell_text(0, start_column), expected.start,
                      "the sheet's Start cell matches the documented mapping");
        }
        // The fixture's first row is stored as MANUALLY SCHEDULED (the writer's
        // own re-read receipt says "was manual; written as SNET and converted").
        // The save-time warning depends on it, so the smoke pins it here.
        step.check(plan.loaded.tasks.front().manually_scheduled,
                   "the fixture's first task is stored as manually scheduled");

        // The Gantt the window draws (app/main.mm rebuild_view): a source for the
        // DISPLAYED rows, the zoom timescale, build_gantt().  The AppKit half only
        // DRAWS this layout, so building it here means the AppKit step's bar-count
        // check is not a guess (and a task with no date gets no bar, which is why
        // the count is checked against the task count on Windows first).
        // Wave 25 (W25-1/S1): displayed rows are RecordSet rows and are
        // resolved through their uid.  When the file shows the project row the
        // chart has one more bar than plan.loaded.tasks, so the expectation is
        // the number of DISPLAYED rows whose row carries a task or uid 0.
        const core::gantt::GanttSource display_source =
            app::make_gantt_source_for_displayed_rows(plan.loaded, plan.records,
                                                      plan.sheet->visible_rows(), {}, kDateNa);
        const core::gantt::GanttLayout chart = core::gantt::build_gantt(
            display_source, app::default_gantt_style(),
            app::timescale_for_zoom(plan.loaded, app::gantt_zoom(preferences.zoom)));
        // Wave 25 review: compare against an INDEPENDENT expectation (the
        // fixture's tasks plus the project row when the file shows it), not
        // against display_source.tasks.size() -- a source that drops a task
        // would otherwise agree with itself.
        const std::size_t expected_bars =
            plan.loaded.tasks.size() + (plan.loaded.show_project_summary ? 1u : 0u);
        step.count(chart.bars.size(), expected_bars,
                   "the Gantt builds one bar per displayed task");
        step.check(chart.content_width > 0.0, "the Gantt has a non-zero content width",
                   "got " + std::to_string(chart.content_width));
    }

    const std::vector<app::TaskRow>& tasks = plan.loaded.tasks;
    const std::uint32_t first_uid = tasks.front().uid;
    const std::uint32_t second_uid = tasks.size() > 1 ? tasks[1].uid : 0;

    // ---- 3. filter --------------------------------------------------------
    std::vector<std::size_t> shown_rows;
    {
        const Step step(report, "filter");
        const std::vector<app::FilterField> fields = app::filter_fields(*plan.sheet);
        const std::vector<app::FilterOperatorChoice>& operators = app::filter_operators();
        step.count(fields.size(), plan.sheet->visible_column_count(),
                   "every visible column is a filter field");
        step.check(operators.size() >= 10, "the operator list is populated",
                   "got " + std::to_string(operators.size()));

        const core::view::FieldId id_field = app::task_field_id("ID");
        std::size_t id_index = fields.size();
        for (std::size_t index = 0; index < fields.size(); ++index) {
            if (fields[index].field == id_field) {
                id_index = index;
                break;
            }
        }
        step.check(id_index < fields.size(), "the ID column is offered as a filter field");

        std::size_t at_least = operators.size();
        for (std::size_t index = 0; index < operators.size(); ++index) {
            if (operators[index].op == core::view::FilterOp::GreaterOrEqual) {
                at_least = index;
                break;
            }
        }
        step.check(at_least < operators.size(), "the 'is at least' operator is offered");

        if (id_index < fields.size() && at_least < operators.size()) {
            app::FilterRule rule;
            rule.field_index = id_index;
            rule.operator_index = at_least;
            rule.value = "2";
            std::vector<app::FilterRule> rules{rule};
            const app::FilterBuild build = app::build_filter(fields, operators, rules,
                                                             plan.format);
            step.check(build.ok, "the dialog's ID >= 2 filter builds", build.error);
            step.check(build.description.find("ID") != std::string::npos,
                       "the filter description names the field", build.description);
            if (build.ok) {
                shown_rows = app::filter_rows(build.definition, plan.records);
                step.count(shown_rows.size(), 2, "the filter matches two of the three tasks");
                const std::vector<std::size_t> restricted =
                    app::restrict_to_filter(plan.sheet->visible_rows(), shown_rows);
                step.count(restricted.size(), 2,
                           "the displayed row list is restricted to the filter");
                const core::view::RecordSet subset =
                    app::filtered_record_set(plan.records, shown_rows);
                step.count(subset.row_count(), 2, "the report copy holds the filtered rows");
                if (shown_rows.size() == 2) {
                    step.check(subset.uid(0) == plan.records.uid(shown_rows[0]),
                               "the report copy keeps the first matching row's uid");
                    step.check(subset.uid(1) == plan.records.uid(shown_rows[1]),
                               "the report copy keeps the second matching row's uid");
                }
            }
        }

        const app::FilterBuild empty =
            app::build_filter(fields, operators, std::vector<app::FilterRule>{}, plan.format);
        step.check(!empty.ok && !empty.error.empty(),
                   "an empty rule list is refused by the dialog model");
    }

    // ---- 4. task_fields (the Task Fields dialog's model) -------------------
    app::StructuralEdits edits;
    std::uint32_t text_field = 0;
    {
        const Step step(report, "task_fields");
        const std::vector<app::TaskFieldSpec>& specs = app::task_field_specs();
        step.count(specs.size(), 6, "the dialog offers the six measured fields");
        bool named = true;
        for (const app::TaskFieldSpec& spec : specs) {
            named = named && spec.field_id != 0 && !spec.title.empty();
        }
        step.check(named, "every field spec has an id and a caption");

        std::size_t text_index = specs.size();
        std::size_t date_index = specs.size();
        for (std::size_t index = 0; index < specs.size(); ++index) {
            if (specs[index].kind == mpp::FieldKind::text) {
                text_index = index;
            }
            if (specs[index].kind == mpp::FieldKind::date) {
                date_index = index;
            }
        }
        step.check(text_index < specs.size(), "the Text field is one of the six");
        if (text_index < specs.size()) {
            text_field = specs[text_index].field_id;
            step.check(app::task_field_spec(text_field) != nullptr,
                       "the Text field resolves by id");
            step.check(app::field_value_text(plan.loaded.tasks.front(), text_field, plan.format)
                           .empty(),
                       "the fixture stores no Text value on the first task");

            std::vector<app::TaskFieldInput> inputs;
            inputs.push_back(app::TaskFieldInput{text_field, "smoke-alpha"});
            std::vector<app::StructuralEdit> queued;
            std::string dialog_error;
            step.check(app::make_task_field_edits(plan.loaded.tasks.front(), inputs,
                                                  plan.format, queued, dialog_error),
                       "the Task Fields form accepts a text value", dialog_error);
            step.count(queued.size(), 1, "the form queues one field edit");
            step.check(!queued.empty() && queued.front().kind == app::StructuralKind::set_field,
                       "the queued edit is a set_field");
            step.check(!queued.empty() &&
                           queued.front().field_value.text == "smoke-alpha",
                       "the queued edit carries the typed text");
            for (const app::StructuralEdit& edit : queued) {
                step.check(edits.push(edit), "the field edit is queued on the pending stack");
            }
            const app::PlanInput display = edits.apply(plan.loaded);
            const mpp::CustomFieldValue* stored =
                display.tasks.front().custom_field(text_field);
            step.check(stored != nullptr && stored->text == "smoke-alpha",
                       "the display plan shows the queued value");
        }

        if (date_index < specs.size()) {
            std::vector<app::TaskFieldInput> bad;
            bad.push_back(app::TaskFieldInput{specs[date_index].field_id, "not-a-date"});
            std::vector<app::StructuralEdit> refused;
            std::string parse_error;
            step.check(!app::make_task_field_edits(plan.loaded.tasks.front(), bad, plan.format,
                                                   refused, parse_error),
                       "a value that does not parse refuses the whole form");
            step.check(!parse_error.empty() && refused.empty(),
                       "a refused form queues nothing and explains itself", parse_error);
        }
    }

    // ---- 5. predecessors (the Predecessors dialog's model) -----------------
    {
        const Step step(report, "predecessors");
        if (second_uid == 0) {
            step.check(false, "the fixture has a second task to link");
        } else {
            step.count(app::predecessors_of(plan.loaded, edits, second_uid).size(), 0,
                       "the second task has no incoming links in the fixture");

            const std::string accepted =
                app::validate_link_add(plan.loaded, edits, first_uid, second_uid,
                                       core::gantt::LinkType::finish_to_start);
            step.text(accepted, "", "the dialog accepts the first -> second FS add");

            app::StructuralEdit link_edit;
            std::string link_error;
            step.check(app::make_link_add(plan.loaded, edits, first_uid, second_uid,
                                          core::gantt::LinkType::finish_to_start, "1d",
                                          link_edit, link_error),
                       "the link add parses a one-day lag", link_error);
            step.check(link_edit.lag_ticks == 4800, "one day of lag is 4800 file ticks",
                       "got " + std::to_string(link_edit.lag_ticks));
            step.check(edits.push(link_edit), "the link add is queued on the same stack");
            step.count(edits.link_edit_count(), 1, "one link edit is pending");

            const app::PlanInput display = edits.apply(plan.loaded);
            step.count(display.links.size(), 1, "the display plan shows the linked row");
            const std::vector<app::PredecessorListItem> incoming =
                app::predecessors_of(display, edits, second_uid);
            step.count(incoming.size(), 1, "the dialog lists the pending predecessor");
            if (!incoming.empty()) {
                step.check(incoming.front().pending, "the listed row is marked pending");
                step.check(incoming.front().type_name == std::string("FS"),
                           "the listed row shows the FS type");
                step.text(incoming.front().lag_text, "1d", "the listed row shows the lag");
                app::StructuralEdit pending_delete;
                std::string delete_error;
                step.check(!app::make_link_delete(incoming.front(), second_uid, pending_delete,
                                                  delete_error),
                           "the dialog refuses to delete a row that is only pending");
                step.check(!delete_error.empty(), "that refusal explains itself", delete_error);
            }

            step.check(edits.undo(), "the link add undoes");
            step.count(edits.apply(plan.loaded).links.size(), 0,
                       "the undone link leaves the display plan");
            step.check(edits.undo(), "the field edit undoes");
            step.check(edits.empty(), "the pending stack is empty again");
            step.check(text_field == 0 ||
                           edits.apply(plan.loaded).tasks.front().custom_field(text_field) ==
                               nullptr,
                       "the undone field edit leaves no value");
        }
    }

    // ---- 6. start_edit: a Start edited through the sheet, as the keyboard does
    app::date32 new_start = kDateNa;
    std::vector<app::StartEdit> start_edits;
    {
        const Step step(report, "start_edit");
        const std::size_t start_column = column_of(*plan.sheet, "Start");
        step.check(start_column != core::sheet::kNoIndex, "the sheet has a Start column");
        if (start_column != core::sheet::kNoIndex) {
            new_start = from_ymd(2026, 3, 2, 8, 0);
            step.check(plan.sheet->begin_edit(0, start_column),
                       "the first task's Start cell opens for editing");
            plan.sheet->set_edit_text("2026-03-02 08:00");
            const core::sheet::EditOutcome committed = plan.sheet->commit_edit(false);
            step.check(committed.ok, "the typed date is accepted", committed.error);
            step.check(committed.changed, "the typed date changes the cell");
            step.text(plan.sheet->cell_text(0, start_column),
                      date_text(new_start, plan.format),
                      "the Start cell shows the typed date");

            start_edits = app::collect_start_edits(plan.records, plan.loaded);
            step.count(start_edits.size(), 1, "exactly one start edit is pending");
            if (!start_edits.empty()) {
                step.check(start_edits.front().uid == first_uid,
                           "the pending edit names the first task");
                step.date(start_edits.front().start, new_start,
                          "the pending edit carries the typed date", plan.format);
            }

            step.check(plan.sheet->undo(), "the edit undoes through the sheet model");
            step.count(app::collect_start_edits(plan.records, plan.loaded).size(), 0,
                       "an undone start edit is no longer pending");
            step.check(plan.sheet->redo(), "the edit redoes");
            step.count(app::collect_start_edits(plan.records, plan.loaded).size(), 1,
                       "the redone start edit is pending again");
        }
    }

    // ---- 7. warning: what the save will tell the user ----------------------
    {
        const Step step(report, "warning");
        const app::StartEditWarningReport save_warning =
            app::warn_start_edits(start_edits, plan.loaded, plan.format);
        // The fixture's edited row is stored as manual, so the real save DOES warn:
        // the writer converts it to auto-scheduled (document_tests prints the same
        // receipt).  This is the app's once-per-save warning firing for a real file.
        step.count(save_warning.warnings, 1,
                   "the save about to run warns once for the edited row");
        step.count(save_warning.manual, 1,
                   "the warning is the manual-task-will-be-rescheduled one");
        step.check(save_warning.text.find("manually scheduled") != std::string::npos,
                   "the warning text names manual scheduling", save_warning.text);

        // A clamped start needs a finish-to-start predecessor, and the committed
        // fixture has ZERO link rows, so this case is built on an in-memory copy
        // with one synthetic FS link.  The dates are still the fixture's.
        app::PlanInput linked_plan = plan.loaded;
        app::LinkRow link;
        link.uid = 9001;  // synthetic: a file link uid is allocated by the writer
        link.predecessor_uid = first_uid;
        link.successor_uid = second_uid;
        link.type = core::gantt::LinkType::finish_to_start;
        link.lag_ticks = 0;
        linked_plan.links.push_back(link);
        // The clamp rule is about an AUTO-scheduled successor; the fixture's rows
        // may be stored manual, so this synthetic case says so explicitly.
        if (linked_plan.tasks.size() > 1) {
            linked_plan.tasks[1].manually_scheduled = false;
        }
        const app::date32 predecessor_finish = linked_plan.tasks.front().finish;
        const app::date32 early = add_days(predecessor_finish, -2);
        const app::StartEditWarning clamped =
            app::warning_for_start_edit(linked_plan, second_uid, early);
        step.check(clamped.kind == app::StartEditWarningKind::clamped_to_predecessor,
                   "an FS predecessor clamps an earlier start",
                   std::string("kind=") + warning_kind_name(clamped.kind));
        step.date(clamped.effective_start, predecessor_finish,
                  "the clamped date is the predecessor's finish", plan.format);
        step.check(clamped.predecessor_uid == first_uid, "the warning names the predecessor");
        step.check(!app::start_edit_warning_text(clamped, plan.format).empty(),
                   "the clamped warning has a message the shell can show");

        app::PlanInput manual_plan = plan.loaded;
        manual_plan.tasks.front().manually_scheduled = true;
        const app::StartEditWarning manual = app::warning_for_start_edit(
            manual_plan, first_uid, add_days(manual_plan.tasks.front().start, 1));
        step.check(manual.kind == app::StartEditWarningKind::manual_task_will_be_rescheduled,
                   "a manual task's start edit warns, not clamps",
                   std::string("kind=") + warning_kind_name(manual.kind));

        const app::StartEditWarning unchanged =
            app::warning_for_start_edit(plan.loaded, first_uid, plan.loaded.tasks.front().start);
        step.check(unchanged.kind == app::StartEditWarningKind::no_effect,
                   "an unchanged start has no effect",
                   std::string("kind=") + warning_kind_name(unchanged.kind));

        std::vector<app::StartEdit> linked_edits;
        linked_edits.push_back(app::StartEdit{second_uid, early});
        const app::StartEditWarningReport linked_report =
            app::warn_start_edits(linked_edits, linked_plan, plan.format);
        step.count(linked_report.warnings, 1, "the save-time report counts the clamped edit");
        step.count(linked_report.clamped, 1, "the report counts it as clamped");
    }

    // ---- 8. save -----------------------------------------------------------
    const fs::path edited_path = out_dir / kSmokeEditedFile;
    {
        const Step step(report, "save");
        step.check(!start_edits.empty(), "there is a start edit to save");
        if (!start_edits.empty()) {
            // The contract the shell relies on: the opened file is never written.
            const app::SaveOutcome refused =
                app::save_start_edits(start_edits, input_path.string(), input_path.string());
            step.check(!refused.ok, "saving over the opened file is refused");
            step.check(refused.error.find("refusing") != std::string::npos,
                       "the refusal explains itself", refused.error);

            std::error_code file_error;
            fs::remove(edited_path, file_error);
            const app::SaveOutcome saved =
                app::save_start_edits(start_edits, input_path.string(), edited_path.string());
            step.check(saved.ok, "the save reports success", saved.error);
            step.count(saved.rows_written, 1, "the save wrote one row");
            step.count(saved.requested, 1, "the save was asked for one row");
            step.check(!saved.api.empty(), "the outcome names the writer path", saved.api);
            step.text(saved.path, edited_path.string(), "the outcome names the target");
            step.check(fs::is_regular_file(edited_path), "the edited file exists");
            step.check(bytes_on_disk(edited_path) > 0, "the edited file is not empty");

            // The write is the app's normal post-save step: the baseline moves, so
            // the same Save cannot offer to write the same dates again.
            app::accept_start_edits(plan.loaded, start_edits);
            step.count(app::collect_start_edits(plan.records, plan.loaded).size(), 0,
                       "the accepted edits leave nothing pending");

            std::string edited_bytes;
            step.check(read_all(edited_path, edited_bytes), "the edited file can be read back");
            step.check(edited_bytes != fixture_bytes,
                       "the edited file differs from the input bytes");
            step.check(bytes_on_disk(input_path) == fixture_bytes.size(),
                       "the input copy is untouched by the save");
            std::string fixture_after;
            step.check(read_all(options.fixture_path, fixture_after) &&
                           fixture_after == fixture_bytes,
                       "the committed fixture is untouched by the save");
            smoke_add_artifact(report, "edited", edited_path.string());
        }
    }

    // ---- 9. reopen ---------------------------------------------------------
    {
        const Step step(report, "reopen");
        LoadedPlan reopened;
        std::string reopen_error;
        if (!load_plan_file(edited_path.string(), preferences, reopened, reopen_error)) {
            step.check(false, "the edited file reopens through the reader", reopen_error);
            report.error = "reopen: " + reopen_error;
            return report;
        }
        step.check(true, "the edited file reopens through the reader");
        step.count(reopened.loaded.tasks.size(), plan.loaded.tasks.size(),
                   "the edited file has the same task count");
        step.count(reopened.loaded.assignments.size(), 3,
                   "the edited file has the same assignment count");
        step.count(reopened.loaded.links.size(), 0, "the edited file still has no links");
        step.text(reopened.loaded.title, plan.loaded.title, "the project title survives the save");

        bool names_match = reopened.loaded.tasks.size() == plan.loaded.tasks.size();
        bool durations_match = names_match;
        for (std::size_t index = 0; names_match && index < reopened.loaded.tasks.size();
             ++index) {
            names_match = reopened.loaded.tasks[index].uid == plan.loaded.tasks[index].uid &&
                          reopened.loaded.tasks[index].name == plan.loaded.tasks[index].name;
            durations_match = durations_match &&
                              reopened.loaded.tasks[index].duration_ticks ==
                                  plan.loaded.tasks[index].duration_ticks;
        }
        step.check(names_match, "every task's uid and name survives the save");
        step.check(durations_match, "every task's duration survives the save");

        const mpp::Task* pinned = nullptr;
        for (const mpp::Task& task : reopened.document.tasks) {
            if (task.uid == first_uid) {
                pinned = &task;
                break;
            }
        }
        step.check(pinned != nullptr, "the edited task is in the edited file");
        if (pinned != nullptr) {
            step.count(pinned->constraint_type, 4,
                       "the edited start is stored as a Start No Earlier Than constraint");
            step.check(pinned->constraint_date.raw == app::packed_from_date32(new_start),
                       "the constraint date is the typed date",
                       "got " + mpp::format_date(pinned->constraint_date) + ", want " +
                           date_text(new_start, plan.format));
            step.check(!pinned->manually_scheduled,
                       "the writer converted the manual row to auto-scheduled");
        }
        step.count(app::collect_start_edits(reopened.records, reopened.loaded).size(), 0,
                   "a fresh load of the edited file has no pending edit");
        const std::size_t reopened_start = column_of(*reopened.sheet, "Start");
        step.check(reopened_start != core::sheet::kNoIndex,
                   "the reopened sheet has a Start column");
    }

    // ---- 10. print ---------------------------------------------------------
    {
        const Step step(report, "print");

        app::PrintRequest request;
        request.kind = app::PrintKind::gantt;
        request.project_name = plan.document.project.title;
        if (request.project_name.empty()) {
            request.project_name = plan.loaded.source_name;
        }
        request.report_title = "Gantt report";
        const std::size_t total = plan.records.row_count();
        request.subtitle = app::export_subtitle(app::ReportKind::gantt, std::string(), total,
                                                total);
        request.date_text = options.date_text;
        request.spec = app::PageSpec::a4_landscape();
        for (std::size_t row = 0; row < total; ++row) {
            request.rows.push_back(row);
        }

        const app::PrintJob printer_job =
            app::build_print_job(request, app::PrintRoute::printer);
        step.count(printer_job.rows.size(), total, "the print job holds every row");
        step.count(printer_job.pages.size(), 1, "a three-row plan is one printed page");
        step.count(printer_job.layout.rows_per_page, 35,
                   "the Gantt page holds 35 rows at the A4 landscape defaults");
        if (!printer_job.pages.empty()) {
            step.count(printer_job.pages.front().row_count, 3,
                       "the single page holds all three rows");
            step.count(printer_job.pages.front().first_row, 0, "the page starts at row 0");
            step.text(printer_job.pages.front().header,
                      app::print_header_text(request.project_name, "Gantt report"),
                      "the page header is the project and the report title");
            step.text(printer_job.pages.front().footer,
                      app::print_footer_text(options.date_text, 1, 1),
                      "the page footer is the date and the page number");
        }

        // The seam: the printer route and the PDF route must paginate identically.
        const app::PrintJob pdf_job = app::build_print_job(request, app::PrintRoute::pdf);
        step.check(pdf_job.pages == printer_job.pages,
                   "the printer and PDF routes produce identical pages");
        step.check(pdf_job.route == app::PrintRoute::pdf &&
                       printer_job.route == app::PrintRoute::printer,
                   "each job carries its own route tag");

        app::PrintRequest sheet_request = request;
        sheet_request.kind = app::PrintKind::sheet;
        sheet_request.report_title = "Sheet report";
        sheet_request.subtitle = app::export_subtitle(app::ReportKind::sheet, std::string(),
                                                      total, total);
        const app::PrintJob sheet_job =
            app::build_print_job(sheet_request, app::PrintRoute::pdf);
        step.count(sheet_job.pages.size(), 1, "the sheet report is one page");
        step.count(sheet_job.layout.rows_per_page, 36,
                   "the sheet page holds 36 rows at the A4 landscape defaults");

        // A filter restricts the printed rows exactly as it restricts the view.
        if (shown_rows.size() == 2) {
            app::PrintRequest filtered = request;
            filtered.rows = shown_rows;
            filtered.subtitle =
                app::export_subtitle(app::ReportKind::gantt, "ID is at least 2", 2, total);
            const app::PrintJob filtered_job =
                app::build_print_job(filtered, app::PrintRoute::pdf);
            step.count(filtered_job.rows.size(), 2, "the filtered print job holds two rows");
            step.count(filtered_job.pages.size(), 1, "the filtered report is one page");
            if (!filtered_job.pages.empty()) {
                step.count(filtered_job.pages.front().row_count, 2,
                           "the filtered page holds the two matching rows");
            }
        }

        // File > Export PDF (core/render) on the same request and the same PageSpec.
        // Two reasons it belongs here: it is the second half of the page-layout
        // seam (Export PDF and Print must agree on the page count), and it leaves a
        // PDF a reviewer can open on Windows, where no AppKit print path exists.
        app::ExportRequest export_request;
        export_request.kind = app::ReportKind::gantt;
        export_request.records = &plan.records;
        for (const std::size_t column : plan.sheet->visible_columns()) {
            if (column < plan.sheet->column_count()) {
                export_request.columns.push_back(plan.sheet->column(column));
            }
        }
        export_request.format = plan.format;
        export_request.metrics = app::default_sheet_metrics();
        export_request.preferences = preferences;
        export_request.page = request.spec;
        export_request.title = request.project_name;
        export_request.subtitle = request.subtitle;
        export_request.footer = app::export_footer(preferences, std::string());
        for (const app::LinkRow& link_row : plan.loaded.links) {
            core::gantt::LinkInput link_input;
            link_input.predecessor_uid = link_row.predecessor_uid;
            link_input.successor_uid = link_row.successor_uid;
            link_input.type = link_row.type;
            link_input.critical = link_row.critical;
            export_request.links.push_back(link_input);
        }
        export_request.timescale = app::timescale_for_zoom(
            plan.loaded, app::gantt_zoom(preferences.zoom));
        export_request.auto_timescale = true;
        export_request.working_day_shading = preferences.working_day_shading;

        const std::string export_path = joined(options.out_dir, kSmokeReportPdf);
        const app::ExportOutcome exported = app::export_report(export_request, export_path);
        step.check(exported.ok, "the same page spec exports a PDF through core/render",
                   exported.error);
        step.count(exported.rows, total, "the exported report prints every row");
        step.count(exported.pages, printer_job.pages.size(),
                   "Export PDF and Print agree on the page count");
        step.check(bytes_on_disk(export_path) > 0,
                   "the exported PDF exists and is not empty",
                   "bytes=" + std::to_string(bytes_on_disk(export_path)));
        smoke_add_artifact(report, "report-export-pdf", export_path);
    }

    return report;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

std::string smoke_json(const SmokeReport& report) {
    std::string out;
    out += "{\n";
    out += "  \"tool\": \"msproject_app --smoke\",\n";
    out += "  \"fixture\": " + json_string(report.fixture_path) + ",\n";
    out += "  \"out_dir\": " + json_string(report.out_dir) + ",\n";
    out += "  \"ok\": " + json_bool(report.ok()) + ",\n";
    out += "  \"steps_run\": " + std::to_string(report.steps_run()) + ",\n";
    out += "  \"steps\": [";
    for (std::size_t index = 0; index < report.steps.size(); ++index) {
        if (index != 0) {
            out += ", ";
        }
        out += json_string(report.steps[index]);
    }
    out += "],\n";
    out += "  \"checks_passed\": " + std::to_string(report.checks_passed) + ",\n";
    out += "  \"checks_failed\": " + std::to_string(report.checks_failed) + ",\n";
    out += "  \"checks\": [\n";
    for (std::size_t index = 0; index < report.checks.size(); ++index) {
        const SmokeCheck& check = report.checks[index];
        out += "    {\"step\": " + json_string(check.step) + ", \"name\": " +
               json_string(check.name) + ", \"ok\": " + json_bool(check.ok) +
               ", \"detail\": " + json_string(check.detail) + "}";
        out += index + 1 == report.checks.size() ? "\n" : ",\n";
    }
    out += "  ],\n";
    out += "  \"artifacts\": [\n";
    for (std::size_t index = 0; index < report.artifacts.size(); ++index) {
        const SmokeArtifact& artifact = report.artifacts[index];
        out += "    {\"name\": " + json_string(artifact.name) + ", \"path\": " +
               json_string(artifact.path) + "}";
        out += index + 1 == report.artifacts.size() ? "\n" : ",\n";
    }
    out += "  ],\n";
    out += "  \"error\": " + json_string(report.error) + "\n";
    out += "}\n";
    return out;
}

bool smoke_write_json(SmokeReport& report, const std::string& out_dir, std::string& error) {
    error.clear();
    const fs::path directory(out_dir);
    std::error_code file_error;
    fs::create_directories(directory, file_error);
    if (file_error) {
        error = "cannot create " + out_dir + ": " + file_error.message();
        return false;
    }
    const fs::path path = directory / kSmokeJsonFile;
    // Self-describing: the file lists itself among the artifacts, so a reviewer
    // who only has smoke.json knows what else the run produced.
    smoke_add_artifact(report, "json", path.string());
    const std::string text = smoke_json(report);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "cannot write " + path.string();
        if (!report.artifacts.empty() && report.artifacts.back().name == "json") {
            report.artifacts.pop_back();
        }
        return false;
    }
    stream << text;
    stream.flush();
    if (!stream) {
        error = "the write to " + path.string() + " failed";
        if (!report.artifacts.empty() && report.artifacts.back().name == "json") {
            report.artifacts.pop_back();
        }
        return false;
    }
    return true;
}

std::string smoke_summary(const SmokeReport& report) {
    std::string text = "smoke: " + std::to_string(report.steps_run()) + " step(s), " +
                       std::to_string(report.checks.size()) + " check(s), " +
                       std::to_string(report.checks_passed) + " passed, " +
                       std::to_string(report.checks_failed) + " failed";
    for (const SmokeCheck& check : report.checks) {
        if (!check.ok) {
            text += "  |  first failure [" + check.step + "] " + check.name;
            if (!check.detail.empty()) {
                text += ": " + check.detail;
            }
            break;
        }
    }
    if (!report.error.empty()) {
        text += "  |  error: " + report.error;
    }
    return text;
}

}  // namespace app
