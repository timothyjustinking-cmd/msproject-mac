// app_qt/report_dialogs_qt.h -- the Report tab's two dialogs (wave 29, W29-4.1):
// Visual Reports - Create Report and Compare Project Versions.
//
// Both are fixed-geometry Qt widgets laid out from wave 29's local Project 2021
// captures (the captures stay local; tests/data/reference/
// w29-report-dialogs.measures.json carries the numbers and the captured UI
// strings).  No Q_OBJECT: connections are lambdas, so the shell stays moc-free.
//
// WHAT IS FUNCTIONAL.  Nothing but dismissal.  This port has no report engine
// and no project-comparison engine, so every current-affecting control (View,
// Save Data, the template commands, Modify, the level and table combos, Browse,
// OK) is present, disabled and carries decision 9's exact tooltip; Close and
// Cancel dismiss the dialog and say that nothing was changed.  Each dialog
// carries an inline note where the capture leaves room, so the reason is on
// screen and not only in a tooltip.
//
// The Visual Reports tab control is one captured control whose seven owner-drawn
// items Project draws on two rows; the port paints the same two rows and selects
// All, as captured.  The captured template list, check-box states and combo
// counts/current values are reproduced; the combo item strings themselves are
// owner-drawn and unreadable, so only the visible text is filled in.
#pragma once

#include <QDialog>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace msqt {

// The measured geometry, in logical pixels at Qt scale 1.  Both values are the
// captured client rectangles exactly, so the gate compares them unscaled.
namespace report_dialog_geometry {

inline constexpr int kVisualReportsWidth = 625;
inline constexpr int kVisualReportsHeight = 541;
inline constexpr int kCompareProjectsWidth = 469;
inline constexpr int kCompareProjectsHeight = 251;

// Project's disabled-command tooltip (operator decision 9), exactly.
inline constexpr const char* kNotImplemented = "not implemented in this port";

}  // namespace report_dialog_geometry

// ---------------------------------------------------------------------------
// Visual Reports' two-row tab control
// ---------------------------------------------------------------------------
//
// A paint-only control: Project's tab control is owner-drawn and inert in this
// port (no report engine behind any tab), so it carries no pages.  The rows are
// the capture's visible layout: three summary tabs over All and the three usage
// tabs, with All selected.  The class exposes its rows so the gate can read the
// labels it paints.
class ReportTabStrip final : public QWidget {
  public:
    ReportTabStrip(const QStringList& top, const QStringList& bottom,
                   const QString& selected, QWidget* parent = nullptr);

    QStringList top_row() const { return top_; }
    QStringList bottom_row() const { return bottom_; }
    QString selected_label() const { return selected_; }
    int tab_count() const { return top_.size() + bottom_.size(); }

    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    int row_height() const;
    int tab_width(const QString& label) const;

    QStringList top_;
    QStringList bottom_;
    QString selected_;
};

// ---------------------------------------------------------------------------
// Report > Export > Visual Reports
// ---------------------------------------------------------------------------
class VisualReportsDialog final : public QDialog {
  public:
    explicit VisualReportsDialog(QWidget* parent = nullptr);

    // Live accessors for the gate.
    ReportTabStrip* tabs() const noexcept { return tabs_; }
    QListWidget* template_list() const noexcept { return list_; }
    QCheckBox* excel_box() const noexcept { return excel_; }
    QCheckBox* visio_box() const noexcept { return visio_; }
    QComboBox* level_combo() const noexcept { return level_; }
    QCheckBox* include_box() const noexcept { return include_; }
    QLineEdit* include_edit() const noexcept { return include_edit_; }
    QPushButton* new_template_button() const noexcept { return new_template_; }
    QPushButton* edit_template_button() const noexcept { return edit_template_; }
    QPushButton* manage_template_button() const noexcept { return manage_template_; }
    QPushButton* modify_button() const noexcept { return modify_; }
    QPushButton* help_button() const noexcept { return help_; }
    QPushButton* save_data_button() const noexcept { return save_data_; }
    QPushButton* view_button() const noexcept { return view_; }
    QPushButton* close_button() const noexcept { return close_; }
    QLabel* note_label() const noexcept { return note_; }

  private:
    ReportTabStrip* tabs_ = nullptr;
    QListWidget* list_ = nullptr;
    QCheckBox* excel_ = nullptr;
    QCheckBox* visio_ = nullptr;
    QComboBox* level_ = nullptr;
    QCheckBox* include_ = nullptr;
    QLineEdit* include_edit_ = nullptr;
    QPushButton* new_template_ = nullptr;
    QPushButton* edit_template_ = nullptr;
    QPushButton* manage_template_ = nullptr;
    QPushButton* modify_ = nullptr;
    QPushButton* help_ = nullptr;
    QPushButton* save_data_ = nullptr;
    QPushButton* view_ = nullptr;
    QPushButton* close_ = nullptr;
    QLabel* note_ = nullptr;
};

// ---------------------------------------------------------------------------
// Report > Project > Compare Projects
// ---------------------------------------------------------------------------
//
// The group caption carries the current project's display name inside Project's
// captured pattern; a name too wide for the captured frame is elided in the
// middle rather than widening the fixed dialog (Project grows the dialog
// instead -- the one knowing deviation from the capture, recorded with the
// item).
class CompareProjectsDialog final : public QDialog {
  public:
    explicit CompareProjectsDialog(const QString& project_name,
                                   QWidget* parent = nullptr);

    QGroupBox* compare_group() const noexcept { return compare_group_; }
    QGroupBox* fields_group() const noexcept { return fields_group_; }
    QComboBox* previous_combo() const noexcept { return previous_; }
    QPushButton* browse_button() const noexcept { return browse_; }
    QComboBox* task_table_combo() const noexcept { return task_table_; }
    QComboBox* resource_table_combo() const noexcept { return resource_table_; }
    QPushButton* ok_button() const noexcept { return ok_; }
    QPushButton* cancel_button() const noexcept { return cancel_; }
    QLabel* note_label() const noexcept { return note_; }

  private:
    QGroupBox* compare_group_ = nullptr;
    QGroupBox* fields_group_ = nullptr;
    QComboBox* previous_ = nullptr;
    QPushButton* browse_ = nullptr;
    QComboBox* task_table_ = nullptr;
    QComboBox* resource_table_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QLabel* note_ = nullptr;
};

}  // namespace msqt
