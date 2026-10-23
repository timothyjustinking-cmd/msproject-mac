// app_qt/project_dialogs.h -- the Project tab's dialogs (wave 26, G5).
//
// Every dialog is a fixed-geometry Qt widget over an app/project_dialogs.h
// payload: the app layer maps the file, this layer builds the captured controls
// and reads them back.  No Q_OBJECT (connections are lambdas), so the target
// stays moc-free.
//
// GEOMETRY.  Sizes and layout fractions are the ones measured from the local
// audit captures and committed in
// tests/data/reference/w26-project-dialogs.measures.json; the gate compares the
// LIVE widget tree with those numbers.  The captures themselves stay local
// (publication hold / client data).
//
// WHAT IS FUNCTIONAL.  Project Information's Priority is the one field this
// port can write; every other control is laid out and disabled with decision
// 9's exact tooltip (the engine has no project-level date, status date, schedule
// from or default-calendar writer).
#pragma once

#include <QDialog>
#include <QString>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "project_dialogs.h"  // app::ProjectInformation / WorkingTimeModel
#include "sheet.h"

class QComboBox;
class QDateEdit;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace msqt {

// The measured geometry of the Project tab's dialogs.  Values are logical
// pixels at Qt scale 1, taken from the captures: capture_px * 2.1968 (the audit
// frames are the 1920x1080 screen scaled to 874x491) and rounded to the nearest
// even pixel.  The fractions are capture-relative and scale-free; the gate uses
// the fractions, the dialogs use the pixel sizes.
namespace project_dialog_geometry {

inline constexpr int kProjectInformationWidth = 616;
inline constexpr int kProjectInformationHeight = 410;
inline constexpr int kMargin = 12;
inline constexpr int kTopMargin = 18;
inline constexpr int kSpacing = 8;
inline constexpr int kRowHeight = 24;
inline constexpr int kRowSpacing = 30;
inline constexpr int kLabelWidth = 104;
inline constexpr int kLeftFieldWidth = 264;
inline constexpr int kRightLabelWidth = 104;
inline constexpr int kRightFieldWidth = 128;

// Change Working Time (captured with the legend, month grid, right pane and the
// two tables).  The fractions are the gate's; the pixel sizes keep the captured
// proportions.
inline constexpr int kChangeWorkingTimeWidth = 750;
inline constexpr int kChangeWorkingTimeHeight = 480;
inline constexpr double kWorkingTimeAspect = 224.0 / 350.0;
inline constexpr double kLegendLeftFraction = 0.020;
inline constexpr double kMonthGridLeftFraction = 0.385;
inline constexpr double kMonthGridWidthFraction = 0.365;
inline constexpr double kRightPaneLeftFraction = 0.780;
inline constexpr double kWorkingTimeTablesTopFraction = 0.520;
inline constexpr double kWorkingTimeButtonsTopFraction = 0.917;
// Legend swatches sampled from the local capture (JPEG values; the gate allows
// +/- 12 per channel because the source is lossy).
inline constexpr const char* kLegendWorkingColour = "#FCFCFC";
inline constexpr const char* kLegendNonworkingColour = "#D2D2D2";
inline constexpr const char* kLegendExceptionColour = "#BFDBDC";
inline constexpr const char* kLegendWorkWeekColour = "#D8E0C8";
inline constexpr int kLegendSwatch = 20;
inline constexpr int kDayCellWidth = 38;
inline constexpr int kDayCellHeight = 28;

// Capture-relative positions the gate asserts (fractions of the dialog's client
// box, which is what the capture's title-bar-excluded area gives).
inline constexpr double kLabelLeftFraction = 0.020;  // 5.5 px of 280 capture px
inline constexpr double kFieldLeftFraction = 0.204;  // 57 px of 280
inline constexpr double kRowFractions[] = {0.047, 0.180, 0.320};
inline constexpr double kButtonRowTopFraction = 0.907;
inline constexpr double kEnterpriseGridTopFraction = 0.651;
inline constexpr double kAspect = 186.0 / 280.0;  // capture client box 280 x 186

// Project's disabled-command tooltip (decision 9), exactly.
inline constexpr const char* kNotImplemented = "not implemented in this port";

}  // namespace project_dialog_geometry

// ---------------------------------------------------------------------------
// Project > Project Information
// ---------------------------------------------------------------------------
class ProjectInformationDialog final : public QDialog {
  public:
    explicit ProjectInformationDialog(const app::ProjectInformation& current,
                                      QWidget* parent = nullptr);

    // Read on OK: the priority the user chose.  The shell queues an edit only
    // when it differs from the opened value.
    int priority() const;
    bool priority_changed() const noexcept { return priority_changed_; }

    void accept() override;

    // Live accessors for the gate (the dialog itself only reads them).
    QDateEdit* start_edit() const noexcept { return start_; }
    QDateEdit* finish_edit() const noexcept { return finish_; }
    QDateEdit* current_edit() const noexcept { return current_date_; }
    QDateEdit* status_edit() const noexcept { return status_; }
    QComboBox* schedule_from_combo() const noexcept { return schedule_from_; }
    QComboBox* calendar_combo() const noexcept { return calendar_; }
    QSpinBox* priority_spin() const noexcept { return priority_; }
    QTableWidget* enterprise_grid() const noexcept { return grid_; }
    QPushButton* help_button() const noexcept { return help_; }
    QPushButton* statistics_button() const noexcept { return statistics_; }
    QPushButton* ok_button() const noexcept { return ok_; }
    QPushButton* cancel_button() const noexcept { return cancel_; }

  private:
    app::ProjectInformation current_;
    int original_priority_ = 500;
    bool priority_changed_ = false;
    QDateEdit* start_ = nullptr;
    QDateEdit* finish_ = nullptr;
    QDateEdit* current_date_ = nullptr;
    QDateEdit* status_ = nullptr;
    QComboBox* schedule_from_ = nullptr;
    QComboBox* calendar_ = nullptr;
    QSpinBox* priority_ = nullptr;
    QTableWidget* grid_ = nullptr;
    QPushButton* help_ = nullptr;
    QPushButton* statistics_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

// Shows the dialog; on OK returns true with `priority` set to the chosen value.
// The app layer owns the actual edit; the shell queues it.
bool edit_project_information(QWidget* parent, const app::ProjectInformation& current,
                              int* priority);

// ---------------------------------------------------------------------------
// Project > Change Working Time
// ---------------------------------------------------------------------------
//
// READ-ONLY: the file's calendars are decoded and rendered (picker, legend,
// month grid, the selected day's hours and the exception/work-week tables); the
// port has no calendar writer, so Details/Delete/Options are laid out and
// disabled with the exact decision-9 tooltip.  Calendar/day/month selection is
// live navigation, not a file edit.
class ChangeWorkingTimeDialog final : public QDialog {
  public:
    ChangeWorkingTimeDialog(std::string mpp_path, const core::sheet::SheetFormat& format,
                            int year, int month, int selected_day,
                            QWidget* parent = nullptr);

    const app::WorkingTimeModel& model() const noexcept { return model_; }
    std::string_view load_error() const noexcept { return error_; }
    int selected_day() const noexcept { return selected_day_; }

    // Live accessors for the gate.
    QComboBox* calendar_combo() const noexcept { return calendar_; }
    QLabel* calendar_line() const noexcept { return calendar_line_; }
    QLabel* month_label() const noexcept { return month_label_; }
    QLabel* hours_label() const noexcept { return hours_; }
    QLabel* based_on_label() const noexcept { return based_on_; }
    QTabWidget* tabs() const noexcept { return tabs_; }
    QTableWidget* exceptions_table() const noexcept { return exceptions_; }
    QTableWidget* work_weeks_table() const noexcept { return work_weeks_; }
    QPushButton* details_button() const noexcept { return details_; }
    QPushButton* delete_button() const noexcept { return delete_; }
    QPushButton* options_button() const noexcept { return options_; }
    QPushButton* ok_button() const noexcept { return ok_; }
    QPushButton* cancel_button() const noexcept { return cancel_; }
    QPushButton* previous_month_button() const noexcept { return previous_; }
    QPushButton* next_month_button() const noexcept { return next_; }
    // The live day-cell widgets in month order (day 1 first).
    const std::vector<QWidget*>& day_cells() const noexcept { return cells_; }
    // The five legend swatches in the capture's order.
    const std::vector<QWidget*>& legend_swatches() const noexcept { return swatches_; }

    void select_day(int day);
    void set_month(int year, int month);

  private:
    void reload(const std::string& calendar, int year, int month, int day);

    std::string path_;
    core::sheet::SheetFormat format_;
    app::WorkingTimeModel model_;
    std::string error_;
    int year_ = 0;
    int month_ = 0;
    int selected_day_ = 1;
    QComboBox* calendar_ = nullptr;
    QLabel* calendar_line_ = nullptr;
    QLabel* month_label_ = nullptr;
    QLabel* hours_ = nullptr;
    QLabel* based_on_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTableWidget* exceptions_ = nullptr;
    QTableWidget* work_weeks_ = nullptr;
    QPushButton* details_ = nullptr;
    QPushButton* delete_ = nullptr;
    QPushButton* options_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* previous_ = nullptr;
    QPushButton* next_ = nullptr;
    QWidget* month_host_ = nullptr;
    QGridLayout* day_grid_ = nullptr;
    std::vector<QWidget*> cells_;
    std::vector<QWidget*> swatches_;
};

}  // namespace msqt
