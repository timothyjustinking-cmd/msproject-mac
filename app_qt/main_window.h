// app_qt/main_window.h -- the one window: ribbon, timeline strip, the task sheet
// (QTableView over core::sheet::SheetModel through SheetTableModel), the Gantt
// canvas, the menu bar (from app::menu_model) and the status bar.
//
// Q1 built the skeleton.  Q2 makes it look like the MS Project reference: the
// ribbon-like toolbar, the timeline strip, the Entry table's column set, the
// 42 px caption band, the 60 px row-number gutter, the 0.459 splitter with a
// drag thumb, the two-tier weekly timescale, and a status bar with view buttons
// and a zoom slider wired to the existing zoom actions.  Q3 freezes the gutter
// (it is the sheet view's vertical header now, so it does not scroll sideways),
// sizes the ribbon buttons from their labels, and can render at the reference's
// own 1920x1032 for the parity harness.
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
class QShowEvent;
class QSlider;
class QSplitter;
class QTableView;

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
    // offscreen test can call it (the interactive path adds one).
    bool open_file(const QString& path, QString* error = nullptr);

    // The displayed row count and Gantt bar count (what the offscreen test
    // asserts against the committed fixture).
    std::size_t sheet_row_count() const;
    std::size_t gantt_bar_count() const;

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
    void set_message(std::string text, bool is_error);
    void handle_action(app::MenuAction action);
    void open_dialog();
    void show_about();
    void show_shortcuts();
    void set_zoom(app::ZoomLevel zoom);
    void after_model_change();
    std::string summary_text() const;
    std::string stored_widths_text() const;

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
    std::string message_;
    bool message_is_error_ = false;
    bool splitter_user_moved_ = false;
};

}  // namespace msqt
