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
#include <QStringList>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "edit_model.h"  // app::StructuralEdits
#include "export_pdf.h"  // app::ReportKind (declared in export_pdf())
#include "menu_model.h"
#include "page_layout.h"  // app::PrintJob (current_print_job)
#include "plan_document.h"
#include "prefs.h"
#include "ribbon_bar.h"  // RibbonCommand (wave 26)
#include "sheet_table_model.h"
#include "theme.h"

class QAction;
class QCloseEvent;
class QLabel;
class QMenu;
class QShowEvent;
class QSlider;
class QSplitter;
class QTableView;
class QToolBar;
class QToolButton;
class QStackedWidget;

namespace app {
class Document;
}

namespace core::sheet {
class SheetModel;
}

namespace msqt {

// Wave 21: the six-tab Task Information dialog's General payload (dialogs.h).
struct TaskInformationValues;

class GanttCanvas;
class Backstage;
class PreferencesStore;
class RibbonBar;
class TimelineStrip;
class ViewLabel;

// The process's one resolved UI font family (see shell_style.h).  Used by the
// offscreen test and the About box.
QString& ui_font_family();
bool ui_font_is_carlito();

// W26-4.2: the port's own version, shown on the Account page and in About.
// Kept in step with the root CMakeLists project version (0.1.0).
inline constexpr const char* kShellVersion = "0.1.0";

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

    // The plan the views draw from (file + applied structural edits + the
    // sheet's committed cells).  Wave 24: the uid-routing gate needs the same
    // input the print path takes.
    const app::PlanInput& plan() const noexcept { return doc_.plan(); }

    // Wave 24 (W24-1 review): the PrintJob File > Print would hand to the
    // printer, built by the SAME production code path.  Public so the gate can
    // assert the subtitle counts tasks (not the project row) without opening a
    // printer dialog.
    app::PrintJob current_print_job() const;

    // Wave 19: the active filter/view, for the offscreen tests.
    bool filter_active() const noexcept { return doc_.filter_active(); }
    app::ViewMode view_mode() const noexcept { return prefs_.view; }

    // --- wave 20: preferences, dialogs and clipboard -----------------------
    const app::Preferences& preferences() const noexcept { return prefs_; }
    // The store the Preferences dialog persists through.  main.cpp installs the
    // native QSettings store; the offscreen tests install an explicit ini file.
    // nullptr means "apply in memory only" (the measured test default).
    void set_preferences_store(PreferencesStore* store) noexcept { preferences_store_ = store; }
    // W26-4.3: the local recent-file list the New/Open/Save As pages show.  It
    // is read from the installed store (empty without one) and updated on a
    // successful open/save; nothing here is synced anywhere.
    QStringList recent_files() const;
    void note_recent_file(const QString& path);
    // Applies `preferences` to the sheet/Gantt and (when persist) writes them to
    // the installed store.  This is the exact path Preferences [OK] runs; the
    // per-screen gate calls it without a modal widget.
    bool apply_preferences(const app::Preferences& preferences, bool persist);

    // --- wave 20: Office themes --------------------------------------------
    Theme theme() const noexcept { return theme_; }
    // Swaps the QPalette between Office light and dark; (persist) writes the
    // choice to the preference store under its own key.
    void set_theme(Theme theme, bool persist);
    // W26-4.2: the Account page's five-name chooser.  Only an implemented
    // choice applies; the palette is the choice's resolution and the existing
    // light/dark store key keeps the resolved palette.
    ThemeChoice theme_choice() const noexcept { return theme_choice_; }
    bool set_theme_choice(ThemeChoice choice, bool persist);

    // Opens/closes the File backstage.  Public so the structural gate can prove
    // the left rail and pages without clicking through a modal.
    void show_backstage(bool open);

    // Re-runs app::menu_item_enabled() for every action after the selection or
    // the clipboard changed.  Public so the clipboard gate can observe the real
    // enablement instead of a copy of the rule.
    void refresh_menu_state() { update_menu_state(); }

    // The sheet model and the current core::sheet::SheetModel, for the clipboard
    // commands and their gate (selection is owned by the core model).
    SheetTableModel* sheet_table_model() noexcept { return &sheet_model_; }
    core::sheet::SheetModel* sheet() noexcept { return doc_.sheet(); }
    const core::sheet::ClipboardBlock& clipboard_block() const noexcept { return clipboard_; }

    // Wave 20: the View > Filter combined sort/group/filter drop-down with its
    // checkable date tree.  Built for the structural gate; show_filter uses it.
    QMenu* build_filter_menu();
    // The task-row right-click menu (distinct from the column header menu).
    QMenu* build_row_menu();

    QTableView* sheet_view() const noexcept { return sheet_view_; }
    GanttCanvas* gantt_view() const noexcept { return gantt_canvas_; }
    QSplitter* splitter() const noexcept { return splitter_; }
    RibbonBar* ribbon() const noexcept { return ribbon_; }
    TimelineStrip* timeline() const noexcept { return timeline_; }
    QSlider* zoom_slider() const noexcept { return zoom_slider_; }

    // --- wave 20 chrome (the empty-state / structural gates) ----------------
    QToolBar* quick_access() const noexcept { return quick_access_; }
    QWidget* chart_panel() const noexcept { return chart_panel_; }
    ViewLabel* view_label() const noexcept { return view_label_; }
    QLabel* info_callout() const noexcept { return info_callout_; }
    QLabel* status_mode_label() const noexcept { return status_mode_; }
    const std::vector<QToolButton*>& view_buttons() const noexcept { return view_buttons_; }
    Backstage* backstage() const noexcept { return backstage_; }
    QStackedWidget* central_stack() const noexcept { return central_stack_; }
    bool backstage_open() const noexcept { return backstage_open_; }

    // The `214/CTable` column widths actually read from the open file (the
    // status line reports them; the parity test asserts they reached the shell).
    bool table_widths_read() const;
    int table_column_chars(std::uint32_t table_field) const;

    // The sheet's share of the split, handle included (0.459 measured).
    // Measured from the live QSplitter, not from the constant.
    double split_fraction() const;

    // --- wave 26: View > Split View > Timeline (W26-1.3) --------------------
    // The initial state is the loaded file's stored setting (wave 24); a click
    // toggles the strip only, never the file.
    void set_timeline_visible(bool visible);
    bool timeline_visible() const noexcept { return timeline_visible_; }

    // --- wave 26: Gantt Chart Format toggles (W26-1.4) ----------------------
    // Project Summary Task: the initial state is the file's stored setting and a
    // toggle rebuilds the displayed rows (never the file).
    bool project_summary_visible() const noexcept { return doc_.project_summary_visible(); }
    // Critical Tasks highlighting (the measured default is OFF).
    bool critical_tasks_highlight() const noexcept;

    // --- wave 21: Task Information (double-click) --------------------------
    // The General values the dialog edits, read from the selected task.  False
    // when no task is selected.  Public so the offscreen gate can prove the
    // dialog and the apply path without a modal window.
    bool task_information_values(TaskInformationValues* out) const;
    // Applies Name/Duration/Start/Finish through the sheet's edit model -- the
    // same begin_edit/commit_edit path a typed cell edit uses, so undo and the
    // save diff see them.  True when every applied field committed.
    bool apply_task_information(const TaskInformationValues& values);
    // Shows the six-tab dialog for the selected task, then applies OK.
    void show_task_information();

    // Wave 21: the --render/--parity artifact is a STRUCTURAL snapshot.  Project's
    // measured green selection band (162,211,160) has a luminance below P1's
    // background threshold, so the harness's rule scan reads the selected row as a
    // full-width grid rule and loses the 22 px row chain.  The render path
    // therefore clears the cell/row selection first (main.cpp); the interactive
    // window -- and qt_ux_gate's palette check -- keep it.
    void clear_render_selection();

    // The status bar's LEFT label: "Ready" unless a command message is active
    // (operator decision 8; the diagnostic detail moved to the right segment).
    QString status_left_text() const;
    // The right segment's text (task/link/warning counts; the full diagnostic is
    // its tooltip).
    QString status_detail_text() const;

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    // Escape leaves the backstage (Project's own behaviour; wave 21 defect: the
    // backstage had no keyboard way back either).
    void keyPressEvent(QKeyEvent* event) override;

  private:
    void build_central_widget();
    void build_menus();
    void build_status_bar();
    void build_quick_access();
    // Wave 21: installs the live task-sheet presentation (the file's stored table
    // when a document is open, the Entry list otherwise) and its widths.
    void configure_task_columns();
    void configure_sheet();
    void apply_split_fraction();
    void sync_view_selection();
    void update_menu_state();
    void update_status();
    // Wave 22 (E7): the status bar's centred "New Tasks : <mode>" text follows
    // the LOADED file's stored default (Project's QNEWTASKSAREMANUAL property,
    // mpp::ProjectRow::new_tasks_manual).  A new/empty project, a file that
    // does not store the setting (an original Project 2007 MPP12 file) or an
    // undecodable value keeps the Manually Scheduled text -- operator decision
    // 11 and the documented fallback.
    void update_status_mode();
    void update_zoom_slider();
    void update_window_title();
    void update_chrome();
    void new_project();
    void refresh_backstage_info();
    void set_message(std::string text, bool is_error);
    void handle_action(app::MenuAction action);
    // Wave 26: the ribbon controls that are not menu actions (the %-complete
    // buttons and Link/Unlink in W26-1.2; the view/format toggles arrive with
    // W26-1.3/1.4).
    void handle_ribbon_command(RibbonCommand command, bool checked);
    void set_percent_complete(int percent);
    void unlink_selected_task();
    void open_dialog();
    void show_about();
    void show_shortcuts();
    void set_zoom(app::ZoomLevel zoom);
    void after_model_change();
    std::string summary_text() const;
    std::string stored_widths_text() const;
    std::string today_text() const;
    void refresh_timeline();

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

    // --- wave 20: the new dialogs and the clipboard -------------------------
    void show_preferences();
    void show_table_date_format();
    void show_custom_formula();
    void copy_selection();
    void cut_selection();
    void paste_selection();
    // Mirrors the core model's rectangle onto the QTableView (one cell when the
    // rectangle is one cell; a clear-and-select range otherwise).
    void select_core_range_in_view();

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
    // Wave 26: View > Split View > Timeline; starts at the file's stored setting
    // (a new project keeps Project's shown default).
    bool timeline_visible_ = true;

    // --- wave 20: the chrome the operator's screenshot found missing ---------
    QStackedWidget* central_stack_ = nullptr;
    Backstage* backstage_ = nullptr;
    bool backstage_open_ = false;
    QToolBar* quick_access_ = nullptr;
    // The chart pane: the rotated view caption + the Gantt canvas in one splitter
    // widget, so the sheet's measured gutter/columns are untouched.
    QWidget* chart_panel_ = nullptr;
    ViewLabel* view_label_ = nullptr;
    QLabel* info_callout_ = nullptr;  // new-project manual-scheduling callout
    QLabel* status_mode_ = nullptr;  // "New Tasks : Manually Scheduled/Auto Scheduled"
    // Wave 22 (E7): the loaded file's stored new-task default.  `known` is false
    // for a new project and for a file that does not store the property (every
    // original Project 2007 MPP12 file); the label then falls back to Manual.
    bool new_tasks_manual_ = true;
    bool new_tasks_manual_known_ = false;
    Theme theme_ = Theme::light;
    // W26-4.2: the Office Theme chooser's current entry.  "Use system setting"
    // resolves through the platform colour scheme at apply time.
    ThemeChoice theme_choice_ = ThemeChoice::colorful;

    // --- wave 19: the document's pending edits and save target --------------
    std::unique_ptr<app::Document> document_;
    app::StructuralEdits structural_;
    std::string last_save_target_;
    bool owns_path_ = false;
    std::vector<std::string> last_save_warnings_;
    std::size_t last_save_rows_written_ = 0;

    // --- wave 20: preference persistence and the sheet clipboard -------------
    PreferencesStore* preferences_store_ = nullptr;
    // The last Copy/Cut block.  Values only: core::sheet::ClipboardBlock keeps
    // the typed value and its rendering, never a font or a colour.
    core::sheet::ClipboardBlock clipboard_;

    // The non-task views own their own RecordSet and SheetModel (the task view
    // uses doc_ directly).  The RecordSet is a member so its address is stable
    // for the SheetModel's reference.
    core::view::RecordSet view_records_;
    std::unique_ptr<core::sheet::SheetModel> view_sheet_;
};

}  // namespace msqt
