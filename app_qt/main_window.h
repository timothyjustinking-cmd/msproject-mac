// app_qt/main_window.h -- the one window: menu bar (from app::menu_model), the
// task sheet (QTableView over core::sheet::SheetModel through SheetTableModel),
// the Gantt canvas, and the status bar.
//
// Q1 scope (docs/ui-qt-port.md section 5): one window that shows a real plan.
// The menu bar, the sheet, the Gantt, the status line and --open are wired.  The
// commands owned by Q2/Q3 (dialogs, save, print, filter, view switching) are
// present and enabled exactly as app::menu_model says, and report in the status
// line that they are not wired yet rather than doing nothing silently.
//
// No Q_OBJECT: the shell has no custom signals; every connection is a lambda on
// a Qt signal, so CMake does not need AUTOMOC.
#pragma once

#include <QMainWindow>
#include <QString>

#include <cstddef>
#include <string>
#include <vector>

#include "menu_model.h"
#include "plan_document.h"
#include "prefs.h"
#include "sheet_table_model.h"

class QAction;
class QLabel;
class QTableView;

namespace msqt {

class GanttCanvas;

// The whole application's look, in one place and in one order: Fusion style,
// one palette built in code, one fixed body point size.  Call it once, after the
// QApplication exists and before any window is constructed.  main.cpp and the
// offscreen test both call it, so the two renders cannot drift.
void configure_application();

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Loads a .mpp into this window.  A file that does not parse clears the
    // document (the macOS shell's rule: never half-loaded) and returns the
    // reader's message through `error`; no dialog is shown here, so the
    // offscreen test can call it (the interactive path adds one).
    bool open_file(const QString& path, QString* error = nullptr);

    // The displayed row count and Gantt bar count (what the offscreen test
    // asserts against the committed fixture).
    std::size_t sheet_row_count() const;
    std::size_t gantt_bar_count() const;

    QTableView* sheet_view() const noexcept { return sheet_view_; }
    GanttCanvas* gantt_view() const noexcept { return gantt_canvas_; }

  private:
    void build_central_widget();
    void build_menus();
    void build_status_bar();
    void configure_sheet();
    void sync_view_selection();
    void update_menu_state();
    void update_status();
    void set_message(std::string text, bool is_error);
    void handle_action(app::MenuAction action);
    void open_dialog();
    void show_about();
    void show_shortcuts();
    void set_zoom(app::ZoomLevel zoom);
    void after_model_change();
    std::string summary_text() const;

    PlanDocument doc_;
    app::Preferences prefs_;
    SheetTableModel sheet_model_;
    QTableView* sheet_view_ = nullptr;
    GanttCanvas* gantt_canvas_ = nullptr;
    QLabel* status_summary_ = nullptr;
    QLabel* status_message_ = nullptr;
    std::vector<QAction*> actions_;
    std::string message_;
    bool message_is_error_ = false;
};

}  // namespace msqt
