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
#include <vector>

#include "project_dialogs.h"  // app::ProjectInformation

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

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

}  // namespace msqt
