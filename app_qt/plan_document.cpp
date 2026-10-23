// app_qt/plan_document.cpp -- see plan_document.h.

#include "plan_document.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "edit_model.h"
#include "entry_columns.h"  // kPredecessorsField/kSuccessorsField/kResourceNamesField
#include "filter_model.h"
#include "reader_adapter.h"
#include "selection_model.h"
#include "shell_style.h"
#include "viewstate.h"

namespace msqt {
namespace {

// The column a row is selected on: the Name column, else the first column.  The
// same rule app/main.mm uses, so the Qt shell opens with the same cell selected.
std::size_t name_column_of(const core::sheet::SheetModel& sheet) {
    for (std::size_t column = 0; column < sheet.column_count(); ++column) {
        if (sheet.column(column).field == app::name_field_id()) {
            return column;
        }
    }
    return 0;
}

// The four parts of `214/CTable`, the collection core/viewstate decodes.
const char* const kTableParts[4] = {"214/CTable/FixedData", "214/CTable/FixedMeta",
                                    "214/CTable/VarMeta", "214/CTable/Var2Data"};

// Wave 22: the task a uid names, or nullptr when the plan has no live row for it
// (the reader drops links with a dead endpoint, and a structural rebuild keeps
// that rule, so this is a guard rather than a normal path).
const app::TaskRow* task_row_of(const app::PlanInput& plan, std::uint32_t uid) {
    if (uid == 0) {
        return nullptr;
    }
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

// The display id of a link's other endpoint: Project's Predecessors/Successors
// text names a task by its ID column (not its UniqueID).  Empty when the plan has
// no live row, which SKIPS that entry rather than printing an invented uid.
std::string endpoint_id(const app::PlanInput& plan, std::uint32_t uid) {
    const app::TaskRow* row = task_row_of(plan, uid);
    return row != nullptr ? std::to_string(row->id) : std::string();
}

// Project's link-type suffix.  MEASURED from the clean capture: a plain
// finish-to-start link with no lag is the BARE id ("9", "33"), but a
// finish-to-start link with a lag prints FS too ("9FS+50 d", "17FS+20 d"), and
// the other three types always print their letters -- the capture shows
// "33SS" for a lag-free SS predecessor (task 42's Pre in the reference file).
const char* link_type_suffix(core::gantt::LinkType type) {
    switch (type) {
        case core::gantt::LinkType::finish_to_start: return "FS";
        case core::gantt::LinkType::start_to_start: return "SS";
        case core::gantt::LinkType::finish_to_finish: return "FF";
        case core::gantt::LinkType::start_to_finish: return "SF";
    }
    return "FS";
}

// The lag text of one link, or "" for a zero lag: "+" / "-" and the sheet's own
// duration rendering of the stored tick count, exactly as Project prints the
// signed lag after the type letters ("9FS+50 days").
std::string link_lag_text(std::int32_t lag_ticks, const core::sheet::SheetFormat& format) {
    if (lag_ticks == 0) {
        return {};
    }
    const std::string magnitude =
        core::sheet::format_duration_text(lag_ticks < 0 ? -static_cast<std::int64_t>(lag_ticks)
                                                        : lag_ticks,
                                          format);
    return (lag_ticks < 0 ? "-" : "+") + magnitude;
}

// One link's entry in the predecessor/successor list: the other endpoint's ID,
// the type letters and the lag.  The letters are dropped only for the measured
// bare case -- finish-to-start AND no lag -- so "3" is a plain FS link while
// "5FS+5 days" and "33SS" both keep theirs.
std::string link_entry(const app::PlanInput& plan, std::uint32_t other_uid,
                       core::gantt::LinkType type, std::int32_t lag_ticks,
                       const core::sheet::SheetFormat& format) {
    const std::string id = endpoint_id(plan, other_uid);
    if (id.empty()) {
        return {};
    }
    const std::string lag = link_lag_text(lag_ticks, format);
    const bool bare = type == core::gantt::LinkType::finish_to_start && lag.empty();
    return id + (bare ? std::string() : std::string(link_type_suffix(type))) + lag;
}

// The comma-separated list of one side of every link touching `task_uid`, in the
// plan's own link order -- the order the capture shows ("18,33FS+20").
std::string link_list_text(const app::PlanInput& plan, std::uint32_t task_uid, bool incoming,
                           const core::sheet::SheetFormat& format) {
    std::string text;
    for (const app::LinkRow& link : plan.links) {
        const std::uint32_t mine = incoming ? link.successor_uid : link.predecessor_uid;
        if (mine != task_uid) {
            continue;
        }
        const std::uint32_t other =
            incoming ? link.predecessor_uid : link.successor_uid;
        const std::string entry = link_entry(plan, other, link.type, link.lag_ticks, format);
        if (entry.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += ",";  // Project's list separator in these two columns: no space
        }
        text += entry;
    }
    return text;
}

}  // namespace

std::string predecessors_text(const app::PlanInput& plan, std::uint32_t task_uid,
                              const core::sheet::SheetFormat& format) {
    return link_list_text(plan, task_uid, true, format);
}

std::string successors_text(const app::PlanInput& plan, std::uint32_t task_uid,
                            const core::sheet::SheetFormat& format) {
    return link_list_text(plan, task_uid, false, format);
}

std::string resource_names_text(const app::PlanInput& plan, std::uint32_t task_uid) {
    if (task_uid == 0) {
        return {};
    }
    std::string text;
    for (const app::AssignmentRow& assignment : plan.assignments) {
        if (assignment.task_uid != task_uid || assignment.resource_uid == 0) {
            continue;  // an unassigned row names no resource (the corpus's case)
        }
        std::string name;
        for (const app::ResourceRow& resource : plan.resources) {
            if (resource.uid == assignment.resource_uid) {
                name = resource.name;
                break;
            }
        }
        if (name.empty()) {
            continue;  // a resource the plan does not know: no invented "resource N"
        }
        if (!text.empty()) {
            text += ", ";  // Project's Resource Names separator
        }
        text += name;
    }
    return text;
}

void PlanDocument::reset() {
    sheet_.reset();
    loaded_ = app::PlanInput{};
    base_ = app::PlanInput{};
    view_ = app::PlanInput{};
    records_ = core::view::RecordSet{};
    format_ = core::sheet::SheetFormat{};
    row_map_ = app::RowMap{};
    gantt_ = core::gantt::GanttLayout{};
    filter_definition_ = core::view::FilterDefinition{};
    filter_rows_.clear();
    filter_description_.clear();
    filter_active_ = false;
    table_ = msproject::viewstate::TableDef{};
    path_.clear();
    title_.clear();
    warnings_ = 0;
}

bool PlanDocument::load(const std::string& source_path, app::ZoomLevel zoom,
                        std::string& error) {
    reset();
    error.clear();
    try {
        const mpp::Document document = mpp::load_file(source_path);
        loaded_ = app::plan_input_from_document(document);
        // Wave 23 (B-1): Project's project-summary row, ID 0, is on in the
        // clean reference capture.  It is an APPLICATION setting, not a stored
        // one: saving the same file with DisplayProjectSummaryTask off and on
        // produced byte-identical `214` parts and no meaningful `114/Props`
        // difference (scripts/oracle_w23.py, 2026-10-23).  The shell owns the
        // default until the Format tab's checkbox exists.
        loaded_.show_project_summary = true;
        loaded_.source_name = std::filesystem::path(source_path).filename().string();
        warnings_ = document.warnings.size();
        title_ = loaded_.title;
        path_ = source_path;
        // No pending structural edits yet: the display plan starts as the file.
        base_ = loaded_;

        // Wave 21 (operator decision 7): the file's own table decides the sheet's
        // columns/order/titles/widths.  Reading it BEFORE the sheet is built is
        // what makes the presentation the file's rather than the Entry default.
        // A decode failure only means the sheet keeps app::default_sheet_columns().
        read_table(source_path);

        // The RecordSet is a stable member: SheetModel holds a reference to it,
        // so assignment (not replacement) is what keeps that reference valid.
        records_ = app::make_record_set(base_);
        sheet_ = std::make_unique<core::sheet::SheetModel>(records_, stored_columns());
        // The date format is the SheetFormat default this wave; the preference
        // panel that changes it is a later stream (app/prefs.h's
        // apply_preferences()).
        format_ = core::sheet::SheetFormat{};
        sheet_->set_format(format_);
        // UID, ID and Outline are identity/calculated columns: the layout model
        // declares the policy (core::sheet deliberately does not infer it).
        app::apply_edit_policy(*sheet_);

        // Open with the first visible row selected, so the Gantt shows a
        // highlighted bar and a later keyboard command has somewhere to go.
        if (sheet_->visible_row_count() != 0) {
            sheet_->select_cell(sheet_->visible_row(0), name_column_of(*sheet_));
        }

        build_gantt(zoom);
        return true;
    } catch (const std::exception& failure) {
        reset();
        error = failure.what();
        return false;
    }
}

void PlanDocument::build_gantt(app::ZoomLevel zoom) {
    if (sheet_ == nullptr) {
        return;
    }
    // The RecordSet -- not the loaded plan -- is the source of truth for task
    // fields once a cell is edited, so re-derive the plan the views draw from.
    view_ = app::sync_plan_from_records(base_, records_);
    row_map_ = app::build_row_map(*sheet_);
    if (filter_active_) {
        // Restrict the DISPLAYED row list (never the RecordSet): the same rows
        // stay editable and the Save diff is unchanged (app/filter_model.h).
        app::RowMap filtered;
        filtered.rows = app::restrict_to_filter(row_map_.rows, filter_rows_);
        filtered.uids.reserve(filtered.rows.size());
        for (const std::size_t row : filtered.rows) {
            filtered.uids.push_back(records_.uid(row));
        }
        row_map_ = std::move(filtered);
    }
    // The Gantt is rebuilt from exactly the displayed rows, never panned, so
    // Gantt row N is displayed ordinal N (app/selection_model.h).  The display
    // plan carries the synthetic project row (wave 23) so a RecordSet row index
    // and a plan task index are the same number.
    app::PlanInput display = view_;
    if (base_.show_project_summary) {
        display.tasks.insert(display.tasks.begin(),
                             app::project_summary_row(display));
    }
    const core::gantt::GanttSource source =
        app::make_gantt_source_for_rows(display, row_map_.rows, {}, core::gantt::kDateNa);
    // No status-date marker: app::today_date32() would make the offscreen render
    // change every day, and this wave's render must be deterministic.
    core::gantt::Timescale timescale = app::timescale_for_zoom(view_, app::gantt_zoom(zoom));
    if (zoom == app::ZoomLevel::week) {
        // The reference's weekly view: the *unit* stays week (so the header is a
        // week tier), but its pixels-per-unit is the measured 24 px/day, and the
        // day row starts on Sunday (MS Project's stock calendar week).
        timescale.unit = core::gantt::TimescaleUnit::week;
        timescale.week_start = 0;
        timescale.pixels_per_unit = kPixelsPerWeek;
    }
    // The Gantt's row pitch must equal the sheet's 22 px rows, or the two halves
    // drift apart one row-height style at a time.
    core::gantt::GanttStyle style = app::default_gantt_style();
    style.row_height = static_cast<double>(kRowHeight);
    gantt_ = core::gantt::build_gantt(source, style, timescale);

    // Wave 22: `view_` is now the current plan (file + structural edits + the
    // sheet's committed cells), so this is the one place where the computed
    // column texts can be refreshed without a second derivation path.
    fill_computed_columns();
}

void PlanDocument::fill_computed_columns() {
    // The three columns Project computes from the link/assignment tables.  The
    // reader never produces them (no descriptor name, no RecordSet column; see
    // entry_columns.h), so the sheet's stored column for one of those ids would
    // otherwise render blank.  Filling the RecordSet is what makes the model and
    // the delegate paint them like any other text cell -- no special case in the
    // view layer.  A row with nothing to show keeps Null (not ""), so the cell is
    // empty rather than "a value that is the empty string".
    if (records_.row_count() == 0) {
        return;
    }
    for (const core::view::FieldId field : {kPredecessorsField, kSuccessorsField,
                                            kResourceNamesField}) {
        records_.add_field(field);  // a no-op when the file's table already made it
    }
    const std::size_t rows = records_.row_count();
    for (std::size_t row = 0; row < rows; ++row) {
        const std::uint32_t uid = records_.uid(row);
        std::string predecessors = predecessors_text(view_, uid, format_);
        if (!predecessors.empty()) {
            records_.set_field(row, kPredecessorsField,
                               core::view::Value::of_text(std::move(predecessors)));
        }
        std::string successors = successors_text(view_, uid, format_);
        if (!successors.empty()) {
            records_.set_field(row, kSuccessorsField,
                               core::view::Value::of_text(std::move(successors)));
        }
        std::string resources = resource_names_text(view_, uid);
        if (!resources.empty()) {
            records_.set_field(row, kResourceNamesField,
                               core::view::Value::of_text(std::move(resources)));
        }
    }
}

void PlanDocument::rebuild_views(app::ZoomLevel zoom) { build_gantt(zoom); }

void PlanDocument::apply_preferences(const app::Preferences& preferences) {
    // The date format is a property of the sheet's formatter, not of the data:
    // every date cell, the status line and the PDF report read this one format.
    app::apply_preferences(preferences, format_);
    if (sheet_ != nullptr) {
        sheet_->set_format(format_);
    }
}

void PlanDocument::build_empty_view(app::ZoomLevel zoom, core::sched::date32 today) {
    // No document: a new project.  The chart still needs an axis, and the axis
    // must start on the CURRENT week -- the engine's Timescale default origin is
    // 1984-01-02, which is the defect this replaces.
    core::gantt::GanttSource source;  // no tasks, no bars, no links
    core::gantt::GanttStyle style = app::default_gantt_style();
    style.row_height = static_cast<double>(kRowHeight);
    core::gantt::Timescale timescale =
        app::timescale_for_zoom(view_, app::gantt_zoom(zoom));
    if (zoom == app::ZoomLevel::week) {
        // The reference's weekly view: week unit, 24 px/day, Sunday-first.
        timescale.unit = core::gantt::TimescaleUnit::week;
        timescale.week_start = 0;
        timescale.pixels_per_unit = kPixelsPerWeek;
    }
    core::sched::date32 day = today;
    if (core::sched::is_na(day) || core::sched::is_overflow(day)) {
        day = app::today_date32();
    }
    day = core::sched::date_only(day);
    // Start the visible axis at the week containing today (Sunday 00:00), so the
    // first header tier is the current week for every zoom level.
    const std::int32_t weekday = core::sched::weekday(day);
    timescale.origin = core::sched::add_days(day, -weekday);
    gantt_ = core::gantt::build_gantt(source, style, timescale);
    row_map_ = app::RowMap{};
}

void PlanDocument::refresh_filter_rows() {
    if (!filter_active_ || sheet_ == nullptr) {
        filter_rows_.clear();
        return;
    }
    filter_rows_ = app::filter_rows(filter_definition_, records_);
}

void PlanDocument::rebuild_from_base(app::ZoomLevel zoom, std::uint32_t keep_uid) {
    // Keep the current selection's task across the rebuild by uid; a pending
    // insert asks for its own placeholder uid instead.
    std::uint32_t wanted = keep_uid;
    if (wanted == 0 && sheet_ != nullptr && sheet_->has_selection()) {
        wanted = records_.uid(sheet_->anchor().row);
    }

    records_ = app::make_record_set(base_);
    sheet_ = std::make_unique<core::sheet::SheetModel>(records_, stored_columns());
    sheet_->set_format(format_);
    app::apply_edit_policy(*sheet_);
    refresh_filter_rows();

    if (sheet_->visible_row_count() != 0) {
        std::size_t target = sheet_->visible_row(0);
        if (wanted != 0) {
            for (std::size_t ordinal = 0; ordinal < sheet_->visible_row_count(); ++ordinal) {
                const std::size_t row = sheet_->visible_row(ordinal);
                if (row != core::sheet::kNoIndex && records_.uid(row) == wanted) {
                    target = row;
                    break;
                }
            }
        }
        sheet_->select_cell(target, name_column_of(*sheet_));
    }
    build_gantt(zoom);
}

void PlanDocument::apply_structural_edits(const app::StructuralEdits& edits, app::ZoomLevel zoom,
                                          std::uint32_t keep_uid) {
    if (sheet_ == nullptr) {
        return;
    }
    // Fold the CURRENT cell values back into the plan before the rebuild: the
    // sheet's edits (a Start, say) are still pending file changes, and building
    // from `loaded_` alone would silently drop them.
    // without_structural_edits() recovers "the file plus those edits", so
    // apply() then re-applies this edit list on top without double-counting.
    const app::PlanInput edited = edits.without_structural_edits(view_, loaded_);
    base_ = edits.apply(edited);
    rebuild_from_base(zoom, keep_uid);
}

void PlanDocument::accept_start_edits(const std::vector<app::StartEdit>& edits) {
    app::accept_start_edits(loaded_, edits);
}

void PlanDocument::set_filter(core::view::FilterDefinition definition, std::string description) {
    filter_definition_ = std::move(definition);
    filter_description_ = std::move(description);
    filter_active_ = true;
    refresh_filter_rows();
}

void PlanDocument::clear_filter() {
    filter_active_ = false;
    filter_definition_ = core::view::FilterDefinition{};
    filter_description_.clear();
    filter_rows_.clear();
}

std::vector<std::size_t> PlanDocument::display_rows() const {
    if (sheet_ == nullptr) {
        return {};
    }
    std::vector<std::size_t> rows = sheet_->visible_rows();
    if (filter_active_) {
        rows = app::restrict_to_filter(rows, filter_rows_);
    }
    return rows;
}

void PlanDocument::read_table(const std::string& source_path) {
    table_ = msproject::viewstate::TableDef{};
    try {
        const mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(source_path);
        std::array<std::vector<std::uint8_t>, 4> raw;
        for (std::size_t index = 0; index < raw.size(); ++index) {
            if (!document.has_stream(kTableParts[index])) {
                return;
            }
            raw[index] = document.read_stream(kTableParts[index]);
        }
        msproject::viewstate::PartSet parts;
        parts.fixed_data = raw[0];
        parts.fixed_meta = raw[1];
        parts.var_meta = raw[2];
        parts.var2_data = raw[3];

        msproject::viewstate::ViewState state;
        if (!msproject::viewstate::parse_view_state("CTable", parts, state, nullptr)) {
            return;
        }
        if (state.tables.empty()) {
            return;
        }
        // The file's FIRST table is the one the Gantt view applies (the measured
        // fixture and both corpus files put the task Entry table first).  Keeping
        // the whole TableDef -- caption, date format and every ColumnDef in file
        // order -- is what the sheet, the status line and a later CTable write
        // need; only the column set is used today.
        table_ = state.tables.front();
    } catch (...) {
        // A view-state decode is presentation metadata: never fail the load.
        table_ = msproject::viewstate::TableDef{};
    }
}

std::vector<core::sheet::SheetColumn> PlanDocument::stored_columns() const {
    std::vector<core::sheet::SheetColumn> columns = app::table_sheet_columns(table_);
    // Wave 22: % Complete (0x0b400020) is a PERCENT, and Project prints the sign
    // -- the clean capture's column reads "57%", "100%", "0%".  app::
    // table_sheet_columns gives a field it does not know the sheet's default kind
    // (Auto), which would print the bare stored integer ("57").  Declaring the
    // kind here fixes the display, the clipboard text and any other consumer of
    // the sheet at once, instead of special-casing one painter.
    for (core::sheet::SheetColumn& column : columns) {
        if (column.field == kPercentCompleteField) {
            column.kind = core::sheet::CellKind::Percent;
        }
    }
    return columns;
}

int PlanDocument::table_column_chars(std::uint32_t table_field) const {
    if (table_field == 0) {
        return 0;
    }
    for (const msproject::viewstate::ColumnDef& column : table_.columns) {
        if (column.field_id == table_field) {
            return static_cast<int>(column.opaque_header[0]);
        }
    }
    return 0;
}

std::uint32_t PlanDocument::selected_uid() const {
    if (sheet_ == nullptr) {
        return 0;
    }
    return app::selected_task(*sheet_, row_map_).uid;
}

}  // namespace msqt
