// app_qt/plan_document.cpp -- see plan_document.h.

#include "plan_document.h"

#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "cfb_writer.h"
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

}  // namespace

void PlanDocument::reset() {
    sheet_.reset();
    loaded_ = app::PlanInput{};
    view_ = app::PlanInput{};
    records_ = core::view::RecordSet{};
    format_ = core::sheet::SheetFormat{};
    row_map_ = app::RowMap{};
    gantt_ = core::gantt::GanttLayout{};
    table_widths_.clear();
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
        loaded_.source_name = std::filesystem::path(source_path).filename().string();
        warnings_ = document.warnings.size();
        title_ = loaded_.title;
        path_ = source_path;

        // The RecordSet is a stable member: SheetModel holds a reference to it,
        // so assignment (not replacement) is what keeps that reference valid.
        records_ = app::make_record_set(loaded_);
        sheet_ = std::make_unique<core::sheet::SheetModel>(records_,
                                                           app::default_sheet_columns());
        // The date format is the SheetFormat default this wave; the preference
        // panel that changes it is Q2 (app/prefs.h's apply_preferences()).
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

        // Read the persisted column widths (read-only; a decode failure only
        // means the status line has no stored widths to report).
        read_table_column_widths(source_path);

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
    view_ = app::sync_plan_from_records(loaded_, records_);
    row_map_ = app::build_row_map(*sheet_);
    // The Gantt is rebuilt from exactly the displayed rows, never panned, so
    // Gantt row N is displayed ordinal N (app/selection_model.h).
    const core::gantt::GanttSource source =
        app::make_gantt_source_for_rows(view_, row_map_.rows, {}, core::gantt::kDateNa);
    // No status-date marker: app::today_date32() would make the offscreen render
    // change every day, and this wave's render must be deterministic.  Q2 can
    // add it once the golden harness pins a date.
    core::gantt::Timescale timescale = app::timescale_for_zoom(view_, app::gantt_zoom(zoom));
    if (zoom == app::ZoomLevel::week) {
        // The reference's weekly view: the *unit* stays week (so the header is a
        // week tier), but its pixels-per-unit is the measured 24 px/day, and the
        // day row starts on Sunday (MS Project's stock calendar week).  Snapping
        // the origin to a Sunday also makes the chart's left edge a weekend
        // column, as in the reference capture.
        timescale.unit = core::gantt::TimescaleUnit::week;
        timescale.week_start = 0;
        timescale.pixels_per_unit = kPixelsPerWeek;
    }
    // The Gantt's row pitch must equal the sheet's 22 px rows, or the two halves
    // drift apart one row-height style at a time.
    core::gantt::GanttStyle style = app::default_gantt_style();
    style.row_height = static_cast<double>(kRowHeight);
    gantt_ = core::gantt::build_gantt(source, style, timescale);
}

void PlanDocument::rebuild_views(app::ZoomLevel zoom) { build_gantt(zoom); }

void PlanDocument::read_table_column_widths(const std::string& source_path) {
    table_widths_.clear();
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
        for (const msproject::viewstate::ColumnDef& column : state.tables.front().columns) {
            if (column.field_id == 0) {
                continue;
            }
            table_widths_.emplace_back(column.field_id,
                                       static_cast<int>(column.opaque_header[0]));
        }
    } catch (...) {
        // A view-state decode is presentation metadata: never fail the load.
        table_widths_.clear();
    }
}

int PlanDocument::table_column_chars(std::uint32_t table_field) const {
    for (const std::pair<std::uint32_t, int>& entry : table_widths_) {
        if (entry.first == table_field) {
            return entry.second;
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
