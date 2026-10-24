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
#include "cell_format.h"  // msproject::viewstate::CellFormatTable (wave 29)
#include "gantt_style.h"  // msproject::viewstate::GanttStyle (wave 26)
#include "viewstate.h"  // msproject::viewstate::TableDef (wave 21)

namespace app {
class StructuralEdits;
}

namespace msqt {

// ---------------------------------------------------------------------------
// Wave 22 (the other half of decision 7): the columns Project COMPUTES
// ---------------------------------------------------------------------------
//
// A `214/CTable` record for Predecessors/Successors/Resource Names stores a field
// id (0x0b40002f / 0x0b400030 / 0x0b400031) that the descriptor table gives no
// name and the reader produces no value for: Project does not store those texts,
// it derives them from the plan's link and assignment tables.  These functions
// do that derivation.  PlanDocument writes the three results into the RecordSet
// under those same ids, so the sheet model and the delegate paint them exactly
// like any other text column.
//
// The syntax is MEASURED from the clean MS Project capture
// (corpus/reference/2026-10-23-msproject/project-gantt-loaded-clean.png):
//   * entries are comma separated with NO space -- "18,33FS+20", "40,36";
//   * a plain finish-to-start link with NO lag is the bare id -- "9", "33";
//   * a LAGGED finish-to-start link keeps its letters -- "9FS+50 d",
//     "17FS+20 d" -- so the letters are dropped only for the bare case;
//   * the other three types always print theirs -- Project's COM
//     Task.Predecessors for task 42 is "33SS,57" (the clean capture's row is
//     below its fold, so this pin is from COM, not the capture);
//   * the list is in the FILE's link order, NOT sorted by ID: the capture's task
//     35 shows "40,36", task 36 "39,37" and task 17 "18,33..." -- the first two
//     are not ascending, and all three are exactly the stored link order.
// WAVE 23: the lag's unit and label are the LINK's stored PjFormatUnit and the
// FILE's Options > Edit label setting -- "9FS+50 d" on project_a (day label 0,
// measured through COM), "4FS+2 days" on tests/data/wave23-outline.mpp (label
// 2).  Units measured per value: hours/elapsed hours, days/elapsed days,
// weeks/elapsed weeks, months and percent, plus negative lags.  Resource Names
// are comma+space separated with no unit suffix; a file that assigns a resource
// at other than 100 % renders "name[50%]" in Project, which no corpus file
// proves, so the units are NOT invented here.
//
// An endpoint with no live task row contributes nothing (the adapter has already
// dropped such links from the plan, and a structural rebuild keeps that rule); a
// uid the plan does not know gives an empty string rather than a guess.
std::string predecessors_text(const app::PlanInput& plan, std::uint32_t task_uid,
                              const core::sheet::SheetFormat& format);
std::string successors_text(const app::PlanInput& plan, std::uint32_t task_uid,
                            const core::sheet::SheetFormat& format);
std::string resource_names_text(const app::PlanInput& plan, std::uint32_t task_uid);


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

    // Forgets the open document (the File > New page).  The caller then builds an
    // empty view; nothing is written.
    void clear() { reset(); }

    // Re-derives the plan from the RecordSet (a cell edit, undo or a collapse),
    // the displayed row map (which a collapse or the filter changes) and the
    // Gantt.
    void rebuild_views(app::ZoomLevel zoom);

    // Wave 20: applies the presentation preferences to the loaded format (the
    // date style, through app::apply_preferences) and the sheet's formatter.  The
    // caller then rebuilds the views at the preference's zoom.  A no-op without a
    // document.
    void apply_preferences(const app::Preferences& preferences);

    // Wave 20 empty project: a Gantt whose axis starts at `today` (the week
    // containing it at the weekly zoom), because a new project must open on the
    // current week, not the engine's 1984 epoch.  No rows, no bars.
    void build_empty_view(app::ZoomLevel zoom, core::sched::date32 today);

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

    // Wave 24 (W24-1): the task a UID names, or nullptr.  uid 0 is the
    // synthetic project row and is never a task.  Command call sites use this
    // instead of indexing plan().tasks with a RecordSet row: the two differ by
    // one whenever the project row is shown (and the project row is a stored
    // setting -- W24-2), so a row index is not a task index.
    const app::TaskRow* task_by_uid(std::uint32_t uid) const;

    // Wave 26 (W26-1.4): view state for the Format tab's Project Summary Task
    // toggle.  The file's stored setting (loaded_.show_project_summary) is the
    // initial state; a toggle rebuilds the RecordSet and the Gantt from it
    // without touching the file.  The override survives a structural rebuild,
    // which re-derives `base_` from `loaded_`.
    void set_project_summary_visible(bool visible, app::ZoomLevel zoom);
    bool project_summary_visible() const noexcept { return show_project_summary_; }

    // The stored character width of the `214/CTable` column whose field id is
    // `table_field` (0 when the table/file does not carry it).
    int table_column_chars(std::uint32_t table_field) const;
    bool table_widths_read() const noexcept { return !table_.columns.empty(); }

    // Wave 26 (W26-3.1): the applied Gantt view's style record -- text styles,
    // and (from W26-3.2/3.3) its bar styles and timescale tiers.  A file whose
    // record does not decode keeps `decoded == false`, and the sheet/Gantt fall
    // back to their measured stock presentation.
    const msproject::viewstate::GanttStyle& gantt_style() const noexcept {
        return gantt_style_;
    }

    // Wave 29 (W29-1.2): the applied view's stored per-cell formatting, read
    // from the same `214/CV_iew` view as gantt_style().  A file without the
    // record keeps an undecoded table and the sheet paints only the Text
    // Styles backgrounds.
    const msproject::viewstate::CellFormatTable& cell_formats() const noexcept {
        return cell_formats_;
    }

    // Wave 21 (operator decision 7): the file's own first `214/CTable` table.
    // The SheetModel is built from it through app::table_sheet_columns(), so the
    // sheet's column set, order, titles and widths are the file's.  An empty
    // TableDef means the file has no readable CTable (a new project, or a stream
    // this reader cannot decode), and the sheet keeps app::default_sheet_columns().
    const msproject::viewstate::TableDef& table() const noexcept { return table_; }

    // Wave 23 (B-2, corrected): the Timeline strip is an APPLICATION setting,
    // not file state.  Measured 2026-10-23:
    //   * COM reports CurrentView "Gantt Chart" for project_a.mpp,
    //     synthetic-base.mpp and wave23-outline.mpp alike, yet Project shows
    //     the strip for the fixture and not in the operator's project_a
    //     capture -- so no stored view name explains the difference;
    //   * saving the same file with the strip shown and hidden changes no
    //     `214` part (viewstate_report --diff) and no Props record.
    // The shell therefore keeps Project's default (shown, operator Q2) and
    // will offer View > Timeline when the split-view work lands.  The applied
    // view name is still exposed for diagnostics: it is real, it just does not
    // control the strip.
    const std::string& applied_view_name() const noexcept { return applied_view_; }

    const std::string& path() const noexcept { return path_; }
    const std::string& title() const noexcept { return title_; }
    std::size_t warnings() const noexcept { return warnings_; }

  private:
    void reset();
    void build_gantt(app::ZoomLevel zoom);
    // Wave 29 (W29-1.3): fills each source task's `sides` from the applied
    // view's stored bar styles (app::bar_label_texts_for) and marks the bar as
    // stored-labelled, so the layout draws exactly those labels.  A file whose
    // style record did not decode leaves the source untouched (the single
    // task-name label stays).
    void apply_stored_bar_labels(core::gantt::GanttSource& source);
    void rebuild_from_base(app::ZoomLevel zoom, std::uint32_t keep_uid);
    void refresh_filter_rows();
    void read_table(const std::string& source_path);
    void read_applied_view(const std::string& source_path);
    void read_style(const std::string& source_path);
    // Wave 22: writes the three computed texts (predecessors_text() and friends)
    // into the RecordSet for every row, from the CURRENT view plan.  Runs at the
    // end of build_gantt(), i.e. after view_ has been re-derived, so a structural
    // link edit is reflected in the sheet in the same rebuild.
    void fill_computed_columns();
    // The columns the sheet is built from: the file's stored table when it has
    // one, else app::default_sheet_columns().
    std::vector<core::sheet::SheetColumn> stored_columns() const;

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
    // The file's stored `214/CTable` table (wave 21): presentation metadata, never
    // part of the save diff.
    msproject::viewstate::TableDef table_;
    msproject::viewstate::GanttStyle gantt_style_;
    msproject::viewstate::CellFormatTable cell_formats_;
    std::string applied_view_;
    std::string path_;
    std::string title_;
    std::size_t warnings_ = 0;
    // Wave 26: the Project Summary Task toggle.  `override_` is false until the
    // ribbon toggles it; the initial display state is the file's stored one.
    bool show_project_summary_ = false;
    bool project_summary_override_ = false;
};

}  // namespace msqt
