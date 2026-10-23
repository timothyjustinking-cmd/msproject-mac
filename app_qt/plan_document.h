// app_qt/plan_document.h -- the loaded plan, and everything the window draws from.
//
// This is the Qt shell's small equivalent of app/main.mm's PlanState: the
// RecordSet, the SheetModel over it, the displayed RowMap and the GanttLayout,
// built from the portable app/ layer only.  It owns the RecordSet because
// core::sheet::SheetModel holds a reference to it.
//
// Nothing here reads Qt types: the shell (main_window.cpp) is the only consumer.
//
// WAVE 19 adds the two things the command surface needs:
//   * `apply_structural_edits()` -- the Insert/Delete/Task Fields/Predecessors
//     cascade from app/edit_model.h is applied to the loaded plan and the whole
//     RecordSet/SheetModel/Gantt is rebuilt from it, exactly as app/main.mm's
//     apply_structural_change() does (it folds the sheet's pending cell values
//     back through StructuralEdits::without_structural_edits, so a pending Start
//     edit is not lost);
//   * a FILTER, stored as the app's own FilterDefinition and re-evaluated
//     against the current RecordSet, so a structural rebuild cannot leave stale
//     row indexes behind.  The displayed row list is what SheetTableModel and
//     the Gantt both draw through.
//
// Nothing here writes a file: saves and reloads are MainWindow's job, through
// the shared app::Document path.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "document.h"  // app::StartEdit
#include "filter.h"    // core::view::FilterDefinition
#include "gantt.h"
#include "layout_model.h"
#include "prefs.h"
#include "record_set.h"
#include "selection_model.h"  // app::RowMap
#include "sheet.h"

namespace app {
class StructuralEdits;
}

namespace msqt {

class PlanDocument {
  public:
    PlanDocument() = default;
    PlanDocument(const PlanDocument&) = delete;
    PlanDocument& operator=(const PlanDocument&) = delete;

    // Loads `path` through mpp::load_file -> app::plan_input_from_document and
    // builds the RecordSet, the SheetModel (app::default_sheet_columns() with
    // app::apply_edit_policy) and the Gantt at `zoom`.  False and `error` set on
    // any reader failure; the document is then empty rather than half-loaded.
    bool load(const std::string& source_path, app::ZoomLevel zoom, std::string& error);

    bool has_document() const noexcept { return sheet_ != nullptr; }

    // Re-derives the plan from the RecordSet (a cell edit, undo or a collapse),
    // the displayed row map (which a collapse or the filter changes) and the
    // Gantt.
    void rebuild_views(app::ZoomLevel zoom);

    // Wave 19: applies the pending Insert/Delete/Task Fields/Predecessors edits
    // to the loaded plan and rebuilds from the result.  `keep_uid` is selected
    // after the rebuild when the plan still has that task (a pending insert
    // passes its placeholder uid); 0 keeps the current selection's uid.
    void apply_structural_edits(const app::StructuralEdits& edits, app::ZoomLevel zoom,
                                std::uint32_t keep_uid = 0);

    // After a successful plain save: the loaded baseline is the new one, so the
    // next Save diff does not offer the same dates again.
    void accept_start_edits(const std::vector<app::StartEdit>& edits);

    // Wave 19 filter.  The definition is kept and re-evaluated after a
    // structural rebuild; `rebuild_views()` then redraws the filtered row list.
    void set_filter(core::view::FilterDefinition definition, std::string description);
    void clear_filter();
    bool filter_active() const noexcept { return filter_active_; }
    const std::string& filter_description() const noexcept { return filter_description_; }
    const std::vector<std::size_t>& filter_rows() const noexcept { return filter_rows_; }
    // The displayed RecordSet rows: the sheet's visible rows, restricted to the
    // filter when one is active.  This is what the table and the Gantt draw.
    std::vector<std::size_t> display_rows() const;

    const app::PlanInput& loaded() const noexcept { return loaded_; }
    const app::PlanInput& plan() const noexcept { return view_; }
    app::PlanSummary summary() const { return app::summarize(view_); }

    core::view::RecordSet& records() noexcept { return records_; }
    const core::view::RecordSet& records() const noexcept { return records_; }
    core::sheet::SheetModel* sheet() noexcept { return sheet_.get(); }
    const core::sheet::SheetModel* sheet() const noexcept { return sheet_.get(); }
    const core::sheet::SheetFormat& format() const noexcept { return format_; }
    const app::RowMap& rows() const noexcept { return row_map_; }
    const core::gantt::GanttLayout& gantt() const noexcept { return gantt_; }

    // The selected task's uid, 0 when nothing is selected.  Derived from
    // SheetModel through app::selected_task(), which is the single source of
    // truth for the selection (app/selection_model.h).
    std::uint32_t selected_uid() const;

    // The stored character width of the `214/CTable` column whose field id is
    // `table_field` (0 when the table/file does not carry it).
    int table_column_chars(std::uint32_t table_field) const;
    bool table_widths_read() const noexcept { return !table_widths_.empty(); }

    const std::string& path() const noexcept { return path_; }
    const std::string& title() const noexcept { return title_; }
    std::size_t warnings() const noexcept { return warnings_; }

  private:
    void reset();
    void build_gantt(app::ZoomLevel zoom);
    void rebuild_from_base(app::ZoomLevel zoom, std::uint32_t keep_uid);
    void refresh_filter_rows();
    void read_table_column_widths(const std::string& source_path);

    app::PlanInput loaded_;
    // The plan the current RecordSet was built from: `loaded_` plus any applied
    // structural edits, without the sheet's pending cell edits (those live in
    // the RecordSet and are folded back by build_gantt()).
    app::PlanInput base_;
    app::PlanInput view_;
    core::view::RecordSet records_;
    std::unique_ptr<core::sheet::SheetModel> sheet_;
    core::sheet::SheetFormat format_;
    app::RowMap row_map_;
    core::gantt::GanttLayout gantt_;
    core::view::FilterDefinition filter_definition_{};
    std::vector<std::size_t> filter_rows_;
    std::string filter_description_;
    bool filter_active_ = false;
    std::vector<std::pair<std::uint32_t, int>> table_widths_;
    std::string path_;
    std::string title_;
    std::size_t warnings_ = 0;
};

}  // namespace msqt
