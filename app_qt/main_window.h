// app_qt/main_window.h -- the one window: ribbon, timeline strip, the task sheet
// (QTableView over core::sheet::SheetModel through SheetTableModel), the Gantt
// canvas, the menu bar (from app::menu_model) and the status bar.
//
// Q1 built the skeleton; Q2 made it look like the MS Project reference; Q3
// (wave 18) froze the gutter and added the 1920x1032 parity render.
//
// WAVE 19 Q4 adds the command surface behind the same action table:
//   * cell editing through SheetModel's begin_edit/commit_edit (the real undo
//     stack), Insert/Delete Task through app::StructuralEdits;
//   * Save / Save As through the ONE shared writer, app::Document::save /
//     save_structural, with the dirty flag and the discard-changes guard;
//   * Print (QPrinter + app::PageSpec) and Export PDF (core/render bytes);
//   * the filter (app::FilterModel) and the four views (app::ViewModel);
//   * Task Fields, Predecessors, Filter, Insert-name, save-changes, start-edit
//     warning and About dialogs -- all Qt's own dialogs.
//
// No Q_OBJECT: the shell has no custom signals; every connection is a lambda on
// a Qt signal, so CMake does not need AUTOMOC.
#pragma once

#include <QMainWindow>
#include <QString>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "edit_model.h"  // app::StructuralEdits
#include "menu_model.h"
#include "plan_document.h"
#include "prefs.h"
#include "sheet_table_model.h"

class QAction;
class QCloseEvent;
class QLabel;
class QShowEvent;
class QSlider;
class QSplitter;
class QTableView;
class QToolButton;

namespace app {
class Document;
}

namespace core::sheet {
class SheetModel;
}

namespace msqt {

class GanttCanvas;
class RibbonBar;
class TimelineStrip;

// The process's one resolved UI font family (see shell_style.h).  Used by the
// offscreen test and the About box.
QString& ui_font_family();
bool ui_font_is_carlito();

// The whole application's look, in one place and in one order: Fusion style,
// one palette built in code, one fixed body point size and the resolved family.
// Call it once, after the QApplication exists and before any window is
// constructed.  main.cpp and the offscreen tests both call it, so the renders
// cannot drift.
void configure_application();

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Loads a .mpp into this window.  A file that does not parse clears the
    // document (the macOS shell's rule: never half-loaded) and returns the
    // reader's message through `error`; no dialog is shown here, so the
    // offscreen test can call it (the interactive path adds one).  It also
    // starts a fresh pending-edit stack and a fresh app::Document for the file.
    bool open_file(const QString& path, QString* error = nullptr);

    // Wave 19: Save to an explicit path through the shared writer, with no
    // dialog of any kind (the acceptance test calls this).  Returns true when
    // the file was written or there was nothing to write; `error` is set on a
    // writer refusal/failure.
    bool save_document_path(const QString& target, QString* error = nullptr);

    // Wave 19 dirty state: true exactly when a save would write something
    // (a pending start diff, or an Insert/Delete/Task Fields/Predecessors edit).
    bool document_dirty() const;
    std::size_t pending_start_edits() const;
    std::size_t loaded_task_count() const;

    // The last save_document_path() result, for the status line and the test.
    std::size_t last_save_rows_written() const noexcept { return last_save_rows_written_; }

    // The displayed row count and Gantt bar count (what the offscreen test
    // asserts against the committed fixture).
    std::size_t sheet_row_count() const;
    std::size_t gantt_bar_count() const;

    // Wave 19: the active filter/view, for the offscreen tests.
    bool filter_active() const noexcept { return doc_.filter_active(); }
    app::ViewMode view_mode() const noexcept { return prefs_.view; }

    QTableView* sheet_view() const noexcept { return sheet_view_; }
    GanttCanvas* gantt_view() const noexcept { return gantt_canvas_; }
    QSplitter* splitter() const noexcept { return splitter_; }
    RibbonBar* ribbon() const noexcept { return ribbon_; }
    TimelineStrip* timeline() const noexcept { return timeline_; }
    QSlider* zoom_slider() const noexcept { return zoom_slider_; }

    // The `214/CTable` column widths actually read from the open file (the
    // status line reports them; the parity test asserts they reached the shell).
    bool table_widths_read() const;
    int table_column_chars(std::uint32_t table_field) const;

    // The sheet's share of the split, handle included (0.459 measured).
    // Measured from the live QSplitter, not from the constant.
    double split_fraction() const;

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

  private:
    void build_central_widget();
    void build_menus();
    void build_status_bar();
    void configure_sheet();
    void apply_split_fraction();
    void sync_view_selection();
    void update_menu_state();
    void update_status();
    void update_zoom_slider();
    void update_window_title();
    void set_message(std::string text, bool is_error);
    void handle_action(app::MenuAction action);
    void open_dialog();
    void show_about();
    void show_shortcuts();
    void set_zoom(app::ZoomLevel zoom);
    void after_model_change();
    std::string summary_text() const;
    std::string stored_widths_text() const;
    std::string today_text() const;

    // --- wave 19: commands -------------------------------------------------
    bool save_document(bool force_panel);
    void export_pdf(app::ReportKind kind);
    void print_plan();
    void save_as_pdf();
    void show_task_fields();
    void show_predecessors();
    void show_filter_dialog();
    void clear_filter();
    void insert_task();
    void delete_task();
    void set_view(app::ViewMode mode);
    void show_view(app::ViewMode mode);
    void set_usage_period(core::usage::Period period);
    void rebuild_generic_view();

    // --- wave 19: state plumbing -------------------------------------------
    bool confirm_proceed_with_discard();
    void after_structural_change(std::uint32_t keep_uid = 0);
    void after_cell_change();
    void after_cell_commit(bool ok, const std::string& message);
    void rebind_sheet();
    void configure_generic_columns();
    void apply_filter_to_sheet_model();
    void refresh_view_buttons();

    PlanDocument doc_;
    app::Preferences prefs_;
    SheetTableModel sheet_model_;
    QTableView* sheet_view_ = nullptr;
    GanttCanvas* gantt_canvas_ = nullptr;
    RibbonBar* ribbon_ = nullptr;
    TimelineStrip* timeline_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QSlider* zoom_slider_ = nullptr;
    QLabel* status_summary_ = nullptr;
    QLabel* status_message_ = nullptr;
    std::vector<QAction*> actions_;
    std::vector<QToolButton*> view_buttons_;
    std::string message_;
    bool message_is_error_ = false;
    bool splitter_user_moved_ = false;

    // --- wave 19: the document's pending edits and save target --------------
    std::unique_ptr<app::Document> document_;
    app::StructuralEdits structural_;
    std::string last_save_target_;
    bool owns_path_ = false;
    std::vector<std::string> last_save_warnings_;
    std::size_t last_save_rows_written_ = 0;

    // The non-task views own their own RecordSet and SheetModel (the task view
    // uses doc_ directly).  The RecordSet is a member so its address is stable
    // for the SheetModel's reference.
    core::view::RecordSet view_records_;
    std::unique_ptr<core::sheet::SheetModel> view_sheet_;
};

}  // namespace msqt
