// app_qt/dialogs.h -- the shell's dialogs as fixed-geometry Qt widgets over the
// portable app/ models.
//
// Every decision a dialog makes lives in app/ and is headless-tested there
// (app/field_link_edits.h, app/filter_model.h, app/document.h); this file only
// builds controls, reads their strings and calls those functions.  That is the
// fidelity contract (docs/ui-qt-port.md section 2.4/2.5): one model, one
// behaviour, three shells.
//
// WAVE 20 Q5 turns each dialog into a class so a per-screen gate can MEASURE it
// before it is shown modally (app_qt/tests/dialog_gate_test.cpp).  The free
// functions at the bottom keep the wave-19 call sites:
//
//   * Preferences...       -- app::Preferences (date/duration/zoom/shading)
//   * Table Date Format... -- app::ViewStateEdits (214/CTable date byte and one
//                             column replacement), written by app::Document::save
//   * Custom Field Formula -- app::CustomFormulaEdit (114/Props), written by
//                             app::Document::save
//   * Task Fields...       -- app::task_field_specs / make_task_field_edits
//   * Predecessors...      -- app::predecessors_of / make_link_add/delete
//   * Filter...            -- app::filter_fields / filter_operators / build_filter
//
// All are plain QDialog/QMessageBox: no native panels, no Q_OBJECT (connections
// are lambdas on Qt signals), so the target stays moc-free.
//
// GEOMETRY CONTRACT
//   The constants in dialog_geometry are the dialog gate's asserted values:
//   every dialog calls setFixedSize() with its Size, the outer layout uses
//   kMargin/kSpacing, every form label is kLabelWidth wide, every form field is
//   kFieldWidth x kControlHeight, and buttons are kButtonHeight tall.  The gate
//   still measures the resulting widget tree; these are the design values it
//   compares against, not guesses read back from one platform.
#pragma once

#include <QDialog>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

#include "document.h"      // app::ViewStateEdits / CustomFormulaEdit
#include "edit_model.h"    // app::StructuralEdit(s)
#include "filter.h"
#include "layout_model.h"  // app::PlanInput / TaskRow
#include "prefs.h"         // app::Preferences (the Preferences dialog's payload)
#include "sheet.h"
#include "sort_model.h"    // app::SortField / app::build_sort (W31-1.1)
#include "theme.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTabWidget;
class QWidget;

namespace app {
struct FilterField;
}

namespace msqt {

// ---------------------------------------------------------------------------
// The fixed geometry contract (see the header comment)
// ---------------------------------------------------------------------------

namespace dialog_geometry {

inline constexpr int kMargin = 12;
inline constexpr int kSpacing = 8;
inline constexpr int kLabelWidth = 120;
inline constexpr int kFieldWidth = 280;
inline constexpr int kControlHeight = 24;
inline constexpr int kCheckHeight = 22;
inline constexpr int kButtonHeight = 26;

// One size per dialog.  The heights leave explicit slack for a wrapped hint at
// the widest fallback font; the gate also checks that no child escapes the
// contents rectangle, so a too-small height is caught rather than hidden.
struct DialogSize {
    int width = 0;
    int height = 0;
};

inline constexpr DialogSize kPreferencesSize{480, 360};
inline constexpr DialogSize kTableDateFormatSize{500, 340};
inline constexpr DialogSize kCustomFormulaSize{520, 280};
inline constexpr DialogSize kTaskFieldsSize{480, 400};
inline constexpr DialogSize kPredecessorsSize{640, 420};
inline constexpr DialogSize kFilterSize{520, 300};
// Wave 31 (W31-1.1): Field, Order and the three buttons.
inline constexpr DialogSize kSortSize{480, 260};
// Wave 31 (W31-2.1): the "Zoom to" radios, the Custom row, the hint and the
// three buttons (Project's dialog 756 inventory, port geometry).
inline constexpr DialogSize kZoomSize{480, 420};
// Wave 21: the six-tab Task Information dialog.
inline constexpr DialogSize kTaskInformationSize{560, 520};

}  // namespace dialog_geometry

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

class PreferencesDialog final : public QDialog {
  public:
    PreferencesDialog(const app::Preferences& current, Theme current_theme = Theme::light,
                      QWidget* parent = nullptr);

    // The values the controls show.  Read on OK by the shell, which applies and
    // persists them through app::apply_preferences() + PreferencesStore.
    app::Preferences preferences() const;
    Theme theme() const;

    // Accessors for the per-screen gate and for a caller that wants to preset a
    // control (the tests set them; the dialog itself only reads them).
    QComboBox* date_style_combo() const noexcept { return date_style_; }
    QComboBox* duration_style_combo() const noexcept { return duration_style_; }
    QComboBox* timescale_combo() const noexcept { return timescale_; }
    QCheckBox* shading_check() const noexcept { return shading_; }
    QComboBox* theme_combo() const noexcept { return theme_combo_; }

  private:
    app::Preferences original_;
    Theme original_theme_ = Theme::light;
    QComboBox* date_style_ = nullptr;
    QComboBox* duration_style_ = nullptr;
    QComboBox* timescale_ = nullptr;
    QCheckBox* shading_ = nullptr;
    QComboBox* theme_combo_ = nullptr;
};

// Applies on OK: shows the dialog and writes the chosen preferences to `prefs`.
bool edit_preferences(QWidget* parent, app::Preferences* prefs);

// ---------------------------------------------------------------------------
// Table / Date Format (214/CTable through app::Document::save)
// ---------------------------------------------------------------------------

class TableDateFormatDialog final : public QDialog {
  public:
    explicit TableDateFormatDialog(const app::ViewStateEdits& current,
                                   QWidget* parent = nullptr);

    // The request the controls describe.  Only valid after accept() succeeded;
    // `ok()` says whether it did.
    const app::ViewStateEdits& edits() const noexcept { return edits_; }
    bool ok() const noexcept { return ok_; }

    // QDialog::accept is virtual: this validates the column fields and keeps the
    // dialog open (with a message) when they are malformed.
    void accept() override;

    QComboBox* date_format_combo() const noexcept { return date_format_; }
    QCheckBox* replace_column_check() const noexcept { return replace_column_; }
    QSpinBox* column_index_spin() const noexcept { return column_index_; }
    QLineEdit* field_id_edit() const noexcept { return field_id_; }
    QLineEdit* title_edit() const noexcept { return title_; }

  private:
    void update_enabled();
    app::ViewStateEdits current_;
    app::ViewStateEdits edits_;
    bool ok_ = false;
    QComboBox* date_format_ = nullptr;
    QCheckBox* replace_column_ = nullptr;
    QSpinBox* column_index_ = nullptr;
    QLineEdit* field_id_ = nullptr;
    QLineEdit* title_ = nullptr;
};

// Shows the dialog; on OK writes the request to `out` and returns true.
bool edit_table_date_format(QWidget* parent, const app::ViewStateEdits& current,
                            app::ViewStateEdits* out);

// ---------------------------------------------------------------------------
// Custom Field Formula (114/Props through app::Document::save)
// ---------------------------------------------------------------------------

class CustomFormulaDialog final : public QDialog {
  public:
    explicit CustomFormulaDialog(const app::CustomFormulaEdit& current,
                                 QWidget* parent = nullptr);

    app::CustomFormulaEdit result() const { return result_; }
    bool accepted_result() const noexcept { return accepted_; }

    void accept() override;

    QComboBox* field_combo() const noexcept { return field_; }
    QLineEdit* formula_edit() const noexcept { return formula_; }
    QPushButton* clear_button() const noexcept { return clear_; }

  private:
    app::CustomFormulaEdit result_;
    bool accepted_ = false;
    QComboBox* field_ = nullptr;
    QLineEdit* formula_ = nullptr;
    QPushButton* clear_ = nullptr;
};

// Shows the dialog; on OK writes the request (set or clear) to `out`.
bool edit_custom_formula(QWidget* parent, const app::CustomFormulaEdit& current,
                         app::CustomFormulaEdit* out);

// ---------------------------------------------------------------------------
// Task Fields
// ---------------------------------------------------------------------------

class TaskFieldsDialog final : public QDialog {
  public:
    TaskFieldsDialog(const app::TaskRow& row, const core::sheet::SheetFormat& format,
                     QWidget* parent = nullptr);

    // Only the fields whose value changed; valid after accept() succeeded.
    const std::vector<app::StructuralEdit>& edits() const noexcept { return edits_; }
    // The six value controls, in app::task_field_specs() order (the gate fills
    // one and proves the accept path; the dialog itself only reads them).
    const std::vector<QLineEdit*>& inputs() const noexcept { return inputs_; }

    void accept() override;

  private:
    const app::TaskRow& row_;
    core::sheet::SheetFormat format_;
    std::vector<app::StructuralEdit> edits_;
    std::vector<QLineEdit*> inputs_;
};

bool edit_task_fields(QWidget* parent, const app::TaskRow& row,
                      const core::sheet::SheetFormat& format,
                      std::vector<app::StructuralEdit>& edits, QString* error);

// ---------------------------------------------------------------------------
// Predecessors
// ---------------------------------------------------------------------------

class PredecessorsDialog final : public QDialog {
  public:
    PredecessorsDialog(const app::PlanInput& loaded, const app::StructuralEdits& edits,
                       std::uint32_t successor_uid, QWidget* parent = nullptr);

    // The pending stack after the user's add/delete operations (a copy of the
    // incoming stack until one is made).
    const app::StructuralEdits& pending_edits() const noexcept { return local_; }

    // Accessors for the per-screen gate.
    QListWidget* list() const noexcept { return list_; }
    QPushButton* delete_button() const noexcept { return delete_button_; }
    QComboBox* predecessor_combo() const noexcept { return predecessor_; }
    QComboBox* type_combo() const noexcept { return type_; }
    QLineEdit* lag_edit() const noexcept { return lag_; }
    QPushButton* add_button() const noexcept { return add_button_; }

  private:
    void rebuild();

    const app::PlanInput& loaded_;
    app::StructuralEdits local_;
    std::uint32_t successor_uid_ = 0;
    QListWidget* list_ = nullptr;
    QPushButton* delete_button_ = nullptr;
    QComboBox* predecessor_ = nullptr;
    QComboBox* type_ = nullptr;
    QLineEdit* lag_ = nullptr;
    QPushButton* add_button_ = nullptr;
};

bool edit_predecessors(QWidget* parent, const app::PlanInput& loaded,
                       const app::StructuralEdits& edits, std::uint32_t successor_uid,
                       app::StructuralEdits& out_edits, QString* error);

// ---------------------------------------------------------------------------
// Task Information (wave 21; the operator's Project capture shows six tabs)
//
// GENERAL is functional over app/: Name, Duration and Finish are the task fields
// the sheet's edit model writes; Start is read-only (W32-0.2: a Start edit's
// save is not measured against Project), and Percent complete (wave 28, W28-1.2)
// is queued by OK as the same
// app::StructuralKind::set_percent edit the ribbon's five % buttons make -- the
// measured actuals writer, not a sheet cell.  Estimated, Priority, Schedule mode
// and the Display on Timeline options have no writer in this port, so they are
// shown READ-ONLY with an inline note -- decision 15, no fake success.
// PREDECESSORS and RESOURCES state that the task's links/assignments are edited
// by their own commands; ADVANCED, NOTES and CUSTOM FIELDS are laid out with
// their primary action disabled and the note.  Cancel always discards.
// ---------------------------------------------------------------------------

struct TaskInformationValues {
    std::string name;
    std::string duration_text;
    std::string start_text;
    std::string finish_text;
    // W28-1.2: the rendered Percent complete text.  A changed value is applied
    // by MainWindow::apply_task_information as a set_percent structural edit.
    std::string percent_text;
    bool manually_scheduled = false;
    // Wave 26 (W26-5.3): the per-task custom-field values, moved here from the
    // old Task Fields command.  `custom_field_ids` is app::task_field_specs()
    // order; `custom_field_texts` is the matching initial text.  OK writes the
    // changed ones through app::make_task_field_edits (the same writer the Task
    // Fields dialog used).
    std::vector<std::uint32_t> custom_field_ids;
    std::vector<std::string> custom_field_texts;
};

class TaskInformationDialog final : public QDialog {
  public:
    explicit TaskInformationDialog(const TaskInformationValues& current,
                                   QWidget* parent = nullptr);

    // Read on OK: the five editable texts, as typed.
    const TaskInformationValues& values() const noexcept { return values_; }

    QTabWidget* tabs() const noexcept { return tabs_; }
    QLineEdit* name_edit() const noexcept { return name_; }
    QLineEdit* duration_edit() const noexcept { return duration_; }
    QLineEdit* start_edit() const noexcept { return start_; }
    QLineEdit* finish_edit() const noexcept { return finish_; }
    // W28-1.2: enabled on the measured actuals writer (see the class comment).
    QLineEdit* percent_edit() const noexcept { return percent_; }
    QCheckBox* hide_bar_check() const noexcept { return hide_bar_; }
    QCheckBox* rollup_check() const noexcept { return rollup_; }
    // W26-5.3: the Custom Fields tab's six value edits, in
    // app::task_field_specs() order (the gate fills one to prove the path).
    const std::vector<QLineEdit*>& custom_field_inputs() const noexcept {
        return custom_field_inputs_;
    }

    void accept() override;

  private:
    TaskInformationValues values_;
    std::vector<QLineEdit*> custom_field_inputs_;
    QTabWidget* tabs_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* duration_ = nullptr;
    QLineEdit* start_ = nullptr;
    QLineEdit* finish_ = nullptr;
    QLineEdit* percent_ = nullptr;
    QCheckBox* hide_bar_ = nullptr;
    QCheckBox* rollup_ = nullptr;
};

// Shows the dialog modally.  True when OK was pressed, with `values` replaced by
// the edited set; the caller applies the four texts through the sheet's edit
// model (the same begin_edit/commit_edit path a typed cell edit uses).
bool edit_task_information(QWidget* parent, TaskInformationValues* values);

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

class FilterDialog final : public QDialog {
  public:
    FilterDialog(const std::vector<app::FilterField>& fields,
                 const core::sheet::SheetFormat& format, QWidget* parent = nullptr);

    bool cleared() const noexcept { return cleared_; }
    const core::view::FilterDefinition& definition() const noexcept { return definition_; }
    const std::string& description() const noexcept { return description_; }

    QComboBox* field_combo() const noexcept { return field_; }
    QComboBox* operator_combo() const noexcept { return op_; }
    QLineEdit* value_edit() const noexcept { return value_; }
    QPushButton* apply_button() const noexcept { return apply_; }
    QPushButton* clear_button() const noexcept { return clear_; }
    QPushButton* cancel_button() const noexcept { return cancel_; }

  private:
    void apply();
    void clear();

    std::vector<app::FilterField> fields_;
    core::sheet::SheetFormat format_;
    core::view::FilterDefinition definition_{};
    std::string description_;
    bool cleared_ = false;
    QComboBox* field_ = nullptr;
    QComboBox* op_ = nullptr;
    QLineEdit* value_ = nullptr;
    QPushButton* apply_ = nullptr;
    QPushButton* clear_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

bool edit_filter(QWidget* parent, const std::vector<app::FilterField>& fields,
                 const core::sheet::SheetFormat& format,
                 core::view::FilterDefinition* definition, std::string* description,
                 bool* cleared, QString* error);

// ---------------------------------------------------------------------------
// Sort (wave 31, W31-1.1)
//
// Project's Sort is a persisted view change; this port applies it for the
// session only (wave-31 brief section 1), so OK reorders the displayed rows and
// the file keeps its bytes.  The Field combo is the brief's five keys
// (ID/Start/Finish/Priority/Name) and the Order combo Ascending/Descending;
// Clear Sort returns the display to the file's order.  The outline order is
// kept (app/sort_model.h), so the dialog has no "keep outline" switch to get
// wrong.
// ---------------------------------------------------------------------------

class SortDialog final : public QDialog {
  public:
    explicit SortDialog(const std::vector<app::SortField>& fields,
                        QWidget* parent = nullptr);

    bool cleared() const noexcept { return cleared_; }
    const core::view::SortDefinition& definition() const noexcept { return definition_; }
    const std::string& description() const noexcept { return description_; }

    QComboBox* field_combo() const noexcept { return field_; }
    QComboBox* order_combo() const noexcept { return order_; }
    QPushButton* sort_button() const noexcept { return sort_; }
    QPushButton* clear_button() const noexcept { return clear_; }
    QPushButton* cancel_button() const noexcept { return cancel_; }

  private:
    void apply();
    void clear();

    std::vector<app::SortField> fields_;
    core::view::SortDefinition definition_{};
    std::string description_;
    bool cleared_ = false;
    QComboBox* field_ = nullptr;
    QComboBox* order_ = nullptr;
    QPushButton* sort_ = nullptr;
    QPushButton* clear_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

bool edit_sort(QWidget* parent, const std::vector<app::SortField>& fields,
               core::view::SortDefinition* definition, std::string* description,
               bool* cleared, QString* error);

// ---------------------------------------------------------------------------
// Zoom (wave 31, W31-2.1)
//
// Project's View > Zoom > Zoom... opens dialog 756: a "Zoom to" group of seven
// radios (1 week, 2 weeks, 1 month, 3 months, Selected tasks, Entire project,
// Custom) with a numeric field and a units combo, then OK / Cancel / Reset.
//
// MEASURED (W31-2.2, tests/data/reference/w31-zoom.measures.json): the fixed
// presets cover 7, 14, ~30.97 and ~90.18 days in Project and anchor at the
// current view's left edge; the port uses 7, 14, kZoomMonthDays and
// 3 * kZoomMonthDays (the same mean month its month column uses) and anchors
// at the chart's present origin.  Entire project and Selected tasks apply the
// same fits as the ribbon commands.  The custom units offered are the three
// the port's timescale model has (Days, Weeks, Months); Project's own unit list
// was not extracted, so no other unit is offered.  The initial radio is Entire
// project -- the live dialog's initial choice was not readable (its Win32
// radios expose no SelectionItem pattern) -- and Reset restores the initial
// controls without touching the view.
//
// Like the other wave-31 view commands, the result is SESSION-ONLY: the chart's
// timescale changes, the file's bytes do not.
// ---------------------------------------------------------------------------

enum class ZoomTarget {
    week,
    two_weeks,
    month,
    three_months,
    selected_tasks,
    entire_project,
    custom,
};

struct ZoomChoice {
    ZoomTarget target = ZoomTarget::entire_project;
    int custom_count = 1;
    // 0 = day, 1 = week, 2 = month (the combo's index).
    int custom_unit = 0;

    // The fixed window this choice asks for in calendar days, or 0 for the two
    // span commands (Selected tasks / Entire project), which measure the plan.
    double span_days() const noexcept;
};

class ZoomDialog final : public QDialog {
  public:
    explicit ZoomDialog(QWidget* parent = nullptr);

    // The live controls' choice.  A caller reads it after exec() accepted; the
    // gate reads it too, which is why it is computed rather than cached.
    ZoomChoice choice() const;

    QRadioButton* week_radio() const noexcept { return week_; }
    QRadioButton* two_weeks_radio() const noexcept { return two_weeks_; }
    QRadioButton* month_radio() const noexcept { return month_; }
    QRadioButton* three_months_radio() const noexcept { return three_months_; }
    QRadioButton* selected_radio() const noexcept { return selected_; }
    QRadioButton* entire_radio() const noexcept { return entire_; }
    QRadioButton* custom_radio() const noexcept { return custom_; }
    QSpinBox* custom_count_spin() const noexcept { return custom_count_; }
    QComboBox* custom_units_combo() const noexcept { return custom_units_; }
    QPushButton* reset_button() const noexcept { return reset_; }
    QPushButton* ok_button() const noexcept { return ok_; }
    QPushButton* cancel_button() const noexcept { return cancel_; }

  private:
    void reset_controls();

    QRadioButton* week_ = nullptr;
    QRadioButton* two_weeks_ = nullptr;
    QRadioButton* month_ = nullptr;
    QRadioButton* three_months_ = nullptr;
    QRadioButton* selected_ = nullptr;
    QRadioButton* entire_ = nullptr;
    QRadioButton* custom_ = nullptr;
    QSpinBox* custom_count_ = nullptr;
    QComboBox* custom_units_ = nullptr;
    QPushButton* reset_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

// Insert > Task...: the name to add.  QInputDialog is a Qt dialog (never the
// platform's), so it matches the rest of the shell.
bool ask_task_name(QWidget* parent, QString* name);

}  // namespace msqt
