// app_qt/plan_document.cpp -- see plan_document.h.

#include "plan_document.h"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <utility>

#include "reader_adapter.h"
#include "selection_model.h"

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

}  // namespace

void PlanDocument::reset() {
    sheet_.reset();
    loaded_ = app::PlanInput{};
    view_ = app::PlanInput{};
    records_ = core::view::RecordSet{};
    format_ = core::sheet::SheetFormat{};
    row_map_ = app::RowMap{};
    gantt_ = core::gantt::GanttLayout{};
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
    gantt_ = core::gantt::build_gantt(source, app::default_gantt_style(),
                                      app::timescale_for_zoom(view_, app::gantt_zoom(zoom)));
}

void PlanDocument::rebuild_views(app::ZoomLevel zoom) { build_gantt(zoom); }

std::uint32_t PlanDocument::selected_uid() const {
    if (sheet_ == nullptr) {
        return 0;
    }
    return app::selected_task(*sheet_, row_map_).uid;
}

}  // namespace msqt
