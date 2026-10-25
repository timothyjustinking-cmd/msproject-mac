// app_qt/format_dialogs_qt.h -- the Gantt Chart Format tab's dialogs (wave 29,
// W29-2.1): Text Styles, Gridlines and Bar Styles.
//
// Every dialog is a fixed-geometry Qt widget laid out from wave 29's local
// captures (the captures stay local; tests/data/reference/
// w29-format-dialogs.measures.json carries the numbers).  No Q_OBJECT:
// connections are lambdas, so the shell stays moc-free.
//
// WHAT IS FUNCTIONAL.  Text Styles edits the 19 decoded text styles the view
// holds (font family, style, size, underline, strike, foreground and background
// colour; the background pattern is laid out but disabled because no painter
// renders a pattern other than solid).  Bar Styles edits the selected bar's
// three colours and its five bar-text fields; the shape/type/pattern controls
// and Cut/Paste/Insert have no engine and stay disabled with decision 9's exact
// tooltip.  Gridlines is laid out in full and inert: this port has no gridline
// record writer or renderer, so its OK is disabled with the same tooltip and an
// inline note.
//
// SESSION ONLY.  OK applies the edited style to the open view (the sheet and
// the Gantt repaint); it is not a file write.  A save of a changed style needs a
// controlled Project pair, which wave 29 does not have, so the dialogs say in
// their inline note that Save keeps the file's bytes.
#pragma once

#include <QDialog>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gantt_style.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace msqt {

// The measured geometry of the three dialogs, in logical pixels at Qt scale 1.
// They are the captured client rectangles exactly (the wave-29 capture harness
// reports client-relative rects), so the gate compares them with no scaling.
namespace format_dialog_geometry {

inline constexpr int kTextStylesWidth = 408;
inline constexpr int kTextStylesHeight = 352;
inline constexpr int kGridlinesWidth = 472;
inline constexpr int kGridlinesHeight = 188;
inline constexpr int kBarStylesWidth = 747;
inline constexpr int kBarStylesHeight = 536;

// Project's disabled-command tooltip (operator decision 9), exactly.
inline constexpr const char* kNotImplemented = "not implemented in this port";

}  // namespace format_dialog_geometry

using GanttStyle = msproject::viewstate::GanttStyle;

// ---------------------------------------------------------------------------
// Gantt Chart Format > Format > Text Styles
// ---------------------------------------------------------------------------
class TextStylesDialog final : public QDialog {
  public:
    explicit TextStylesDialog(const GanttStyle& style, QWidget* parent = nullptr);

    // The style after OK; the opened style before it.
    const GanttStyle& style() const noexcept { return result_; }
    bool changed() const noexcept { return changed_; }
    bool style_available() const noexcept { return source_.decoded; }

    void accept() override;

    // Live accessors for the gate.
    QComboBox* item_combo() const noexcept { return item_; }
    QComboBox* font_combo() const noexcept { return font_; }
    QComboBox* font_style_combo() const noexcept { return font_style_; }
    QComboBox* size_combo() const noexcept { return size_; }
    QCheckBox* underline_box() const noexcept { return underline_; }
    QCheckBox* strikethrough_box() const noexcept { return strikethrough_; }
    QComboBox* colour_combo() const noexcept { return colour_; }
    QComboBox* background_combo() const noexcept { return background_; }
    QComboBox* pattern_combo() const noexcept { return pattern_; }
    QLabel* sample_label() const noexcept { return sample_; }
    QLabel* note_label() const noexcept { return note_; }
    QPushButton* ok_button() const noexcept { return ok_; }
    QPushButton* cancel_button() const noexcept { return cancel_; }

    // Selects a Text Styles item (0 = All) through the live combo, so the gate
    // drives the same path a user click does.
    void select_item(int index);

  private:
    enum class Field {
        family,
        font_style,
        size,
        underline,
        strikethrough,
        colour,
        background,
    };

    void load_item(int index);
    void apply_field(Field field);
    void refresh_sample();
    std::vector<std::size_t> targets() const;

    GanttStyle source_;
    GanttStyle working_;
    GanttStyle result_;
    bool changed_ = false;
    bool loading_ = false;

    QComboBox* item_ = nullptr;
    QComboBox* font_ = nullptr;
    QComboBox* font_style_ = nullptr;
    QComboBox* size_ = nullptr;
    QCheckBox* underline_ = nullptr;
    QCheckBox* strikethrough_ = nullptr;
    QComboBox* colour_ = nullptr;
    QComboBox* background_ = nullptr;
    QComboBox* pattern_ = nullptr;
    QLabel* sample_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

// ---------------------------------------------------------------------------
// Gantt Chart Format > Format > Gridlines
// ---------------------------------------------------------------------------
//
// Laid out from the capture: the 14 line kinds, the Normal and At interval
// groups, both type/colour pairs and the interval radio/edit row.  The port
// decodes and stores no gridline record, so every control and OK are disabled
// with decision 9's exact tooltip plus an inline note; Cancel works.
class GridlinesDialog final : public QDialog {
  public:
    explicit GridlinesDialog(QWidget* parent = nullptr);

    QListWidget* line_list() const noexcept { return list_; }
    QComboBox* normal_type_combo() const noexcept { return normal_type_; }
    QComboBox* normal_colour_combo() const noexcept { return normal_colour_; }
    QRadioButton* none_radio() const noexcept { return none_; }
    QRadioButton* interval_2_radio() const noexcept { return interval_2_; }
    QRadioButton* interval_3_radio() const noexcept { return interval_3_; }
    QRadioButton* interval_4_radio() const noexcept { return interval_4_; }
    QRadioButton* other_radio() const noexcept { return other_; }
    QSpinBox* other_spin() const noexcept { return other_spin_; }
    QComboBox* interval_type_combo() const noexcept { return interval_type_; }
    QComboBox* interval_colour_combo() const noexcept { return interval_colour_; }
    QLabel* note_label() const noexcept { return note_; }
    QPushButton* help_button() const noexcept { return help_; }
    QPushButton* ok_button() const noexcept { return ok_; }
    QPushButton* cancel_button() const noexcept { return cancel_; }

  private:
    QListWidget* list_ = nullptr;
    QComboBox* normal_type_ = nullptr;
    QComboBox* normal_colour_ = nullptr;
    QRadioButton* none_ = nullptr;
    QRadioButton* interval_2_ = nullptr;
    QRadioButton* interval_3_ = nullptr;
    QRadioButton* interval_4_ = nullptr;
    QRadioButton* other_ = nullptr;
    QSpinBox* other_spin_ = nullptr;
    QComboBox* interval_type_ = nullptr;
    QComboBox* interval_colour_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* help_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

// ---------------------------------------------------------------------------
// Gantt Chart Format > Bar Styles > Format
// ---------------------------------------------------------------------------
class BarStylesDialog final : public QDialog {
  public:
    explicit BarStylesDialog(const GanttStyle& style, QWidget* parent = nullptr);

    const GanttStyle& style() const noexcept { return result_; }
    bool changed() const noexcept { return changed_; }
    bool style_available() const noexcept { return source_.decoded; }

    void accept() override;

    QTableWidget* style_table() const noexcept { return table_; }
    QTabWidget* tabs() const noexcept { return tabs_; }
    QTableWidget* text_table() const noexcept { return text_table_; }
    QComboBox* left_field_combo() const noexcept { return left_field_; }
    QComboBox* right_field_combo() const noexcept { return right_field_; }
    QComboBox* top_field_combo() const noexcept { return top_field_; }
    QComboBox* bottom_field_combo() const noexcept { return bottom_field_; }
    QComboBox* inside_field_combo() const noexcept { return inside_field_; }
    QComboBox* start_shape_combo() const noexcept { return start_shape_; }
    QComboBox* start_type_combo() const noexcept { return start_type_; }
    QComboBox* start_colour_combo() const noexcept { return start_colour_; }
    QComboBox* middle_shape_combo() const noexcept { return middle_shape_; }
    QComboBox* middle_pattern_combo() const noexcept { return middle_pattern_; }
    QComboBox* middle_colour_combo() const noexcept { return middle_colour_; }
    QComboBox* end_shape_combo() const noexcept { return end_shape_; }
    QComboBox* end_type_combo() const noexcept { return end_type_; }
    QComboBox* end_colour_combo() const noexcept { return end_colour_; }
    QPushButton* cut_button() const noexcept { return cut_; }
    QPushButton* paste_button() const noexcept { return paste_; }
    QPushButton* insert_button() const noexcept { return insert_; }
    QPushButton* help_button() const noexcept { return help_; }
    QLabel* note_label() const noexcept { return note_; }
    QPushButton* ok_button() const noexcept { return ok_; }
    QPushButton* cancel_button() const noexcept { return cancel_; }

    int selected_row() const;
    void select_row(int row);

  private:
    void populate_table();
    void load_row(int row);
    void apply_text_field(int side);
    void apply_colour(int side, const QComboBox* combo);
    void refresh_text_table(int row);

    GanttStyle source_;
    GanttStyle working_;
    GanttStyle result_;
    bool changed_ = false;
    bool loading_ = false;

    QTableWidget* table_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTableWidget* text_table_ = nullptr;
    QComboBox* left_field_ = nullptr;
    QComboBox* right_field_ = nullptr;
    QComboBox* top_field_ = nullptr;
    QComboBox* bottom_field_ = nullptr;
    QComboBox* inside_field_ = nullptr;
    QComboBox* start_shape_ = nullptr;
    QComboBox* start_type_ = nullptr;
    QComboBox* start_colour_ = nullptr;
    QComboBox* middle_shape_ = nullptr;
    QComboBox* middle_pattern_ = nullptr;
    QComboBox* middle_colour_ = nullptr;
    QComboBox* end_shape_ = nullptr;
    QComboBox* end_type_ = nullptr;
    QComboBox* end_colour_ = nullptr;
    QPushButton* cut_ = nullptr;
    QPushButton* paste_ = nullptr;
    QPushButton* insert_ = nullptr;
    QPushButton* help_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

}  // namespace msqt
