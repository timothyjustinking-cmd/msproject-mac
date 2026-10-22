// app_qt/plan_document.h -- the loaded plan, and everything the window draws from.
//
// This is the Qt shell's small equivalent of app/main.mm's PlanState: the
// RecordSet, the SheetModel over it, the displayed RowMap and the GanttLayout,
// built from the portable app/ layer only.  It owns the RecordSet because
// core::sheet::SheetModel holds a reference to it.
//
// Nothing here reads Qt types: the shell (main_window.cpp) is the only consumer.
// The loaded file is never written to; this wave has no save path.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "gantt.h"
#include "layout_model.h"
#include "prefs.h"
#include "record_set.h"
#include "selection_model.h"  // app::RowMap
#include "sheet.h"

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
    // the displayed row map (which a collapse changes) and the Gantt.
    void rebuild_views(app::ZoomLevel zoom);

    const app::PlanInput& loaded() const noexcept { return loaded_; }
    const app::PlanInput& plan() const noexcept { return view_; }
    app::PlanSummary summary() const { return app::summarize(view_); }

    core::sheet::SheetModel* sheet() noexcept { return sheet_.get(); }
    const core::sheet::SheetModel* sheet() const noexcept { return sheet_.get(); }
    const core::sheet::SheetFormat& format() const noexcept { return format_; }
    const app::RowMap& rows() const noexcept { return row_map_; }
    const core::gantt::GanttLayout& gantt() const noexcept { return gantt_; }

    // The selected task's uid, 0 when nothing is selected.  Derived from
    // SheetModel through app::selected_task(), which is the single source of
    // truth for the selection (app/selection_model.h).
    std::uint32_t selected_uid() const;

    const std::string& path() const noexcept { return path_; }
    const std::string& title() const noexcept { return title_; }
    std::size_t warnings() const noexcept { return warnings_; }

  private:
    void reset();
    void build_gantt(app::ZoomLevel zoom);

    app::PlanInput loaded_;
    app::PlanInput view_;
    core::view::RecordSet records_;
    std::unique_ptr<core::sheet::SheetModel> sheet_;
    core::sheet::SheetFormat format_;
    app::RowMap row_map_;
    core::gantt::GanttLayout gantt_;
    std::string path_;
    std::string title_;
    std::size_t warnings_ = 0;
};

}  // namespace msqt
