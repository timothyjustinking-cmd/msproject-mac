// app_qt/page_setup_dialog.h -- File > Print > Page Setup (wave 29, W29-3.1).
//
// The dialog is a view of app::PageSpec: the Page tab's orientation, paper,
// scaling and first page number, the four Margins, the Header/Footer switches,
// text and alignment, and the Legend band.  OK hands the edited PageSpec back
// to MainWindow, which stores it in the one spec every print route uses
// (current_print_job -> app::build_print_job -> page_layout).  The backstage
// preview re-renders from that same spec, so the preview cannot drift from the
// printer.
//
// MEASUREMENTS.  The Page tab's rectangles are Project 2021's, from the local
// capture in build/w29 (numbers only are committed to
// tests/data/reference/w29-page-setup.measures.json):
//
//   * dialog client 476x362 (the captured tab control's rect);
//   * Orientation group [11,34,459,63], Portrait [57,64,58,16],
//     Landscape [204,64,74,16];
//   * Scaling group [11,103,459,77], Adjust to [20,125,70,16],
//     edit [98,122,38,22], "% normal size" [161,126,70,13],
//     Fit to [20,155,50,16], the two fit edits [78,152,38,22] and
//     [223,152,38,22] (disabled in the capture), "pages wide by"
//     [141,156,74,13], "tall" [286,156,16,13];
//   * Other group [11,186,459,78], Paper Size label [20,210,55,13],
//     paper combo [84,206,257,21], First page number label [20,240,97,13],
//     edit [125,236,110,22];
//   * the action row [11,332,93,23] Print Preview, [113,332,75,23] Options,
//     [197,332,75,23] Print, [311,332,75,23] OK, [395,332,75,23] Cancel.
//
// The captured tab control has six owner-drawn tabs whose labels could not be
// read (TCM_GETITEMW returned empty strings and the tab-switch probe crashed
// Project); the six names below are Project's Page Setup tab set, and the
// port's tab strip is the 24 px band above the measured Page-tab rectangles.
// The other five tabs are laid out on the same 459 px group grid; their
// controls were not pixel-measured, so the gate checks structure and wiring,
// not rectangles, for them.
//
// WHAT IS FUNCTIONAL.  Orientation, paper (the shared A4/Letter PageSpecs),
// Adjust to %, first page number, all four margins, header/footer visibility,
// text (with &p/&P/&d/&& codes) and alignment, and the legend band all apply.
// "Fit to: pages wide by ... tall" has no layout engine here (the shared
// layout paginates by row height, not by chart width) and the View tab's
// print-notes/blank-pages options have no engine, so those controls are laid
// out disabled with decision 9's exact tooltip; the dialog never reports
// success for them.
//
// No Q_OBJECT: lambdas only, like the rest of the shell.
#pragma once

#include <QDialog>
#include <QString>

#include "page_layout.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QTabBar;
class QWidget;

namespace msqt {

namespace page_setup_geometry {

inline constexpr int kDialogWidth = 476;
inline constexpr int kDialogHeight = 362;
// The port's tab strip; the measured Page-tab rectangles start 34 px below the
// client top, so page-relative positions are measured y - kTabBarHeight.
inline constexpr int kTabBarHeight = 24;

// Decision 9 (operator): the exact tooltip every disabled command carries.
inline constexpr const char* kNotImplemented = "not implemented in this port";

}  // namespace page_setup_geometry

class PageSetupDialog final : public QDialog {
  public:
    explicit PageSetupDialog(const app::PageSpec& spec, QWidget* parent = nullptr);

    // The edited spec, valid after accept().  Before OK it is the source spec.
    const app::PageSpec& spec() const noexcept { return result_; }
    void accept() override;

    // True when the user asked for Print.../Print Preview... rather than OK;
    // MainWindow applies the spec and then runs that command.
    bool print_requested() const noexcept { return print_requested_; }
    bool preview_requested() const noexcept { return preview_requested_; }

    // Live accessors for the gate (the same widgets a user drives).
    QTabBar* tabs() const noexcept { return tabs_; }
    QStackedWidget* pages() const noexcept { return pages_; }

    QRadioButton* portrait_radio() const noexcept { return portrait_; }
    QRadioButton* landscape_radio() const noexcept { return landscape_; }
    QRadioButton* adjust_radio() const noexcept { return adjust_; }
    QSpinBox* scale_spin() const noexcept { return scale_; }
    QRadioButton* fit_radio() const noexcept { return fit_; }
    QSpinBox* fit_wide_spin() const noexcept { return fit_wide_; }
    QSpinBox* fit_tall_spin() const noexcept { return fit_tall_; }
    QComboBox* paper_combo() const noexcept { return paper_; }
    QSpinBox* first_page_spin() const noexcept { return first_page_; }

    QDoubleSpinBox* margin_top_spin() const noexcept { return margin_top_; }
    QDoubleSpinBox* margin_bottom_spin() const noexcept { return margin_bottom_; }
    QDoubleSpinBox* margin_left_spin() const noexcept { return margin_left_; }
    QDoubleSpinBox* margin_right_spin() const noexcept { return margin_right_; }

    QCheckBox* show_header_box() const noexcept { return show_header_; }
    QLineEdit* header_text_edit() const noexcept { return header_text_; }
    QComboBox* header_align_combo() const noexcept { return header_align_; }
    QCheckBox* show_footer_box() const noexcept { return show_footer_; }
    QLineEdit* footer_text_edit() const noexcept { return footer_text_; }
    QComboBox* footer_align_combo() const noexcept { return footer_align_; }

    QCheckBox* show_legend_box() const noexcept { return show_legend_; }
    QLineEdit* legend_text_edit() const noexcept { return legend_text_; }

    QCheckBox* print_notes_box() const noexcept { return print_notes_; }
    QCheckBox* print_blank_box() const noexcept { return print_blank_; }
    QLabel* view_note_label() const noexcept { return view_note_; }
    QLabel* page_note_label() const noexcept { return page_note_; }

    QPushButton* preview_button() const noexcept { return preview_; }
    QPushButton* options_button() const noexcept { return options_; }
    QPushButton* print_button() const noexcept { return print_; }
    QPushButton* ok_button() const noexcept { return ok_; }
    QPushButton* cancel_button() const noexcept { return cancel_; }

  private:
    app::PageSpec collect() const;
    QWidget* build_page_tab(const app::PageSpec& spec);
    QWidget* build_margins_tab(const app::PageSpec& spec);
    QWidget* build_header_tab(const app::PageSpec& spec);
    QWidget* build_footer_tab(const app::PageSpec& spec);
    QWidget* build_legend_tab(const app::PageSpec& spec);
    QWidget* build_view_tab();

    app::PageSpec source_;
    app::PageSpec result_;
    bool print_requested_ = false;
    bool preview_requested_ = false;

    QTabBar* tabs_ = nullptr;
    QStackedWidget* pages_ = nullptr;

    QRadioButton* portrait_ = nullptr;
    QRadioButton* landscape_ = nullptr;
    QRadioButton* adjust_ = nullptr;
    QSpinBox* scale_ = nullptr;
    QRadioButton* fit_ = nullptr;
    QSpinBox* fit_wide_ = nullptr;
    QSpinBox* fit_tall_ = nullptr;
    QComboBox* paper_ = nullptr;
    QSpinBox* first_page_ = nullptr;

    QDoubleSpinBox* margin_top_ = nullptr;
    QDoubleSpinBox* margin_bottom_ = nullptr;
    QDoubleSpinBox* margin_left_ = nullptr;
    QDoubleSpinBox* margin_right_ = nullptr;

    QCheckBox* show_header_ = nullptr;
    QLineEdit* header_text_ = nullptr;
    QComboBox* header_align_ = nullptr;
    QCheckBox* show_footer_ = nullptr;
    QLineEdit* footer_text_ = nullptr;
    QComboBox* footer_align_ = nullptr;

    QCheckBox* show_legend_ = nullptr;
    QLineEdit* legend_text_ = nullptr;

    QCheckBox* print_notes_ = nullptr;
    QCheckBox* print_blank_ = nullptr;
    QLabel* view_note_ = nullptr;
    QLabel* page_note_ = nullptr;

    QPushButton* preview_ = nullptr;
    QPushButton* options_ = nullptr;
    QPushButton* print_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

}  // namespace msqt
