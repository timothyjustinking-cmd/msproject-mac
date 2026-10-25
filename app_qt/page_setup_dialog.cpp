// app_qt/page_setup_dialog.cpp -- see page_setup_dialog.h.

#include "page_setup_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <string>

namespace msqt {
namespace {

using namespace page_setup_geometry;

// The Page tab's measured rectangles are client-relative; the tab strip owns
// the top 24 px, so a page control sits that much higher inside its page.
void place_measured(QWidget* widget, int x, int y, int width, int height) {
    widget->setGeometry(x, y - kTabBarHeight, width, height);
}

// The five unmeasured tabs use the same grid, page-relative.
void place(QWidget* widget, int x, int y, int width, int height) {
    widget->setGeometry(x, y, width, height);
}

QLabel* make_label(QWidget* parent, const QString& text, int x, int y, int width,
                   int height, bool wrap = false) {
    auto* label = new QLabel(text, parent);
    place(label, x, y, width, height);
    label->setWordWrap(wrap);
    return label;
}

QLabel* make_measured_label(QWidget* parent, const QString& text, int x, int y,
                            int width, int height) {
    auto* label = new QLabel(text, parent);
    place_measured(label, x, y, width, height);
    return label;
}

QGroupBox* make_group(QWidget* parent, const QString& title, int x, int y, int width,
                      int height) {
    auto* group = new QGroupBox(title, parent);
    place(group, x, y, width, height);
    group->setFlat(false);
    return group;
}

void disable_with_tip(QWidget* widget) {
    widget->setEnabled(false);
    widget->setToolTip(QString::fromUtf8(kNotImplemented));
}

// The gate matches every captured Page-tab control (by its Project control id)
// to the live widget carrying this property, then compares the live rectangle
// with the committed measurement.  Keeping the id on the widget means the gate
// needs no table of names that could drift from the dialog.
void mark_measured(QWidget* widget, int control_id) {
    widget->setProperty("measuredControlId", control_id);
}

QString code_note() {
    return QStringLiteral(
        "Text codes: &p = page number, &P = last page number, &d = date, "
        "&& = a literal ampersand.");
}

}  // namespace

PageSetupDialog::PageSetupDialog(const app::PageSpec& spec, QWidget* parent)
    : QDialog(parent), source_(spec), result_(spec) {
    setWindowTitle(QStringLiteral("Page Setup - Gantt Chart"));
    setFixedSize(kDialogWidth, kDialogHeight);

    // The six Project Page Setup tabs, over a stack whose origin is
    // (0, kTabBarHeight) -- that is what lets the measured Page-tab rectangles
    // be reproduced exactly.
    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("pageSetupTabs"));
    tabs_->setGeometry(0, 0, kDialogWidth, kTabBarHeight);
    tabs_->setExpanding(false);
    tabs_->setUsesScrollButtons(false);
    tabs_->addTab(QStringLiteral("Page"));
    tabs_->addTab(QStringLiteral("Margins"));
    tabs_->addTab(QStringLiteral("Header"));
    tabs_->addTab(QStringLiteral("Footer"));
    tabs_->addTab(QStringLiteral("Legend"));
    tabs_->addTab(QStringLiteral("View"));

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("pageSetupPages"));
    pages_->setGeometry(0, kTabBarHeight, kDialogWidth, kDialogHeight - kTabBarHeight - 32);
    pages_->addWidget(build_page_tab(spec));
    pages_->addWidget(build_margins_tab(spec));
    pages_->addWidget(build_header_tab(spec));
    pages_->addWidget(build_footer_tab(spec));
    pages_->addWidget(build_legend_tab(spec));
    pages_->addWidget(build_view_tab());
    connect(tabs_, &QTabBar::currentChanged, pages_, &QStackedWidget::setCurrentIndex);
    tabs_->setCurrentIndex(0);

    // The captured action row, on the dialog itself so it is shared by every
    // tab.  Print/Preview apply the values first (accept()), then MainWindow
    // runs the command.
    preview_ = new QPushButton(QStringLiteral("Print Previe&w..."), this);
    preview_->setObjectName(QStringLiteral("pageSetupPreview"));
    preview_->setGeometry(11, 332, 93, 23);
    mark_measured(preview_, 4);
    connect(preview_, &QPushButton::clicked, this, [this] {
        preview_requested_ = true;
        accept();
    });

    options_ = new QPushButton(QStringLiteral("&Options..."), this);
    options_->setObjectName(QStringLiteral("pageSetupOptions"));
    options_->setGeometry(113, 332, 75, 23);
    mark_measured(options_, 5);
    disable_with_tip(options_);

    print_ = new QPushButton(QStringLiteral("&Print..."), this);
    print_->setObjectName(QStringLiteral("pageSetupPrint"));
    print_->setGeometry(197, 332, 75, 23);
    mark_measured(print_, 3);
    connect(print_, &QPushButton::clicked, this, [this] {
        print_requested_ = true;
        accept();
    });

    ok_ = new QPushButton(QStringLiteral("OK"), this);
    ok_->setObjectName(QStringLiteral("pageSetupOk"));
    ok_->setGeometry(311, 332, 75, 23);
    mark_measured(ok_, 1);
    ok_->setDefault(true);
    connect(ok_, &QPushButton::clicked, this, [this] { accept(); });

    cancel_ = new QPushButton(QStringLiteral("Cancel"), this);
    cancel_->setObjectName(QStringLiteral("pageSetupCancel"));
    cancel_->setGeometry(395, 332, 75, 23);
    mark_measured(cancel_, 2);
    connect(cancel_, &QPushButton::clicked, this, [this] { reject(); });
}

QWidget* PageSetupDialog::build_page_tab(const app::PageSpec& spec) {
    auto* page = new QWidget(pages_);
    page->setObjectName(QStringLiteral("pageSetupPageTab"));

    // Decorative measured group frames first, so controls paint on top.
    auto* orientation = new QGroupBox(QStringLiteral("Orientation"), page);
    orientation->setObjectName(QStringLiteral("pageSetupOrientationGroup"));
    place_measured(orientation, 11, 34, 459, 63);
    mark_measured(orientation, 8);

    portrait_ = new QRadioButton(QStringLiteral("Por&trait"), page);
    portrait_->setObjectName(QStringLiteral("pageSetupPortrait"));
    place_measured(portrait_, 57, 64, 58, 16);
    mark_measured(portrait_, 9);
    landscape_ = new QRadioButton(QStringLiteral("&Landscape"), page);
    landscape_->setObjectName(QStringLiteral("pageSetupLandscape"));
    place_measured(landscape_, 204, 64, 74, 16);
    mark_measured(landscape_, 11);
    // All four radios share one page parent, and Qt's sibling auto-exclusivity
    // would make Orientation and Scaling one six-way (here four-way) group.
    // Two explicit groups keep Project's two independent choices.
    auto* orientation_buttons = new QButtonGroup(page);
    orientation_buttons->setExclusive(true);
    orientation_buttons->addButton(portrait_);
    orientation_buttons->addButton(landscape_);
    (spec.width > spec.height ? landscape_ : portrait_)->setChecked(true);

    auto* scaling = new QGroupBox(QStringLiteral("Scaling"), page);
    scaling->setObjectName(QStringLiteral("pageSetupScalingGroup"));
    place_measured(scaling, 11, 103, 459, 77);
    mark_measured(scaling, 13);

    adjust_ = new QRadioButton(QStringLiteral("&Adjust to:"), page);
    adjust_->setObjectName(QStringLiteral("pageSetupAdjust"));
    place_measured(adjust_, 20, 125, 70, 16);
    mark_measured(adjust_, 14);
    auto* scaling_buttons = new QButtonGroup(page);
    scaling_buttons->setExclusive(true);
    scaling_buttons->addButton(adjust_);
    adjust_->setChecked(true);

    scale_ = new QSpinBox(page);
    scale_->setObjectName(QStringLiteral("pageSetupScale"));
    place_measured(scale_, 98, 122, 38, 22);
    mark_measured(scale_, 15);
    scale_->setRange(10, 400);
    scale_->setValue(static_cast<int>(
        std::lround(std::clamp(spec.scale_percent, 10.0, 400.0))));

    auto* normal = make_measured_label(page, QStringLiteral("% normal size"), 161, 126,
                                       70, 13);
    normal->setObjectName(QStringLiteral("pageSetupNormalLabel"));
    mark_measured(normal, 16);

    // The capture's Fit to controls are disabled; this port has no fit-to
    // pagination engine, so the radio and both edits stay disabled with the
    // decision-9 tooltip.
    fit_ = new QRadioButton(QStringLiteral("&Fit to:"), page);
    fit_->setObjectName(QStringLiteral("pageSetupFit"));
    place_measured(fit_, 20, 155, 50, 16);
    mark_measured(fit_, 17);
    scaling_buttons->addButton(fit_);
    disable_with_tip(fit_);

    fit_wide_ = new QSpinBox(page);
    fit_wide_->setObjectName(QStringLiteral("pageSetupFitWide"));
    place_measured(fit_wide_, 78, 152, 38, 22);
    mark_measured(fit_wide_, 18);
    fit_wide_->setRange(1, 99);
    disable_with_tip(fit_wide_);

    auto* wide_by = make_measured_label(page, QStringLiteral("pages wide by"), 141, 156,
                                        74, 13);
    wide_by->setObjectName(QStringLiteral("pageSetupPagesWideLabel"));
    mark_measured(wide_by, 19);

    fit_tall_ = new QSpinBox(page);
    fit_tall_->setObjectName(QStringLiteral("pageSetupFitTall"));
    place_measured(fit_tall_, 223, 152, 38, 22);
    mark_measured(fit_tall_, 20);
    fit_tall_->setRange(1, 99);
    disable_with_tip(fit_tall_);

    auto* tall = make_measured_label(page, QStringLiteral("tall"), 286, 156, 16, 13);
    tall->setObjectName(QStringLiteral("pageSetupTallLabel"));
    mark_measured(tall, 21);

    auto* other = new QGroupBox(QStringLiteral("Other"), page);
    other->setObjectName(QStringLiteral("pageSetupOtherGroup"));
    place_measured(other, 11, 186, 459, 78);
    mark_measured(other, 29);

    auto* paper_label = make_measured_label(page, QStringLiteral("Paper Si&ze:"), 20,
                                            210, 55, 13);
    paper_label->setObjectName(QStringLiteral("pageSetupPaperLabel"));
    mark_measured(paper_label, 25);

    paper_ = new QComboBox(page);
    paper_->setObjectName(QStringLiteral("pageSetupPaper"));
    place_measured(paper_, 84, 206, 257, 21);
    mark_measured(paper_, 26);
    paper_->addItem(QStringLiteral("A4"));
    paper_->addItem(QStringLiteral("Letter"));
    // The shared presets are recognised by their long side (points).
    paper_->setCurrentIndex(std::fabs(std::max(spec.width, spec.height) - 792.0) < 1.0
                                ? 1
                                : 0);

    auto* first_label = make_measured_label(page, QStringLiteral("Fi&rst page number:"),
                                            20, 240, 97, 13);
    first_label->setObjectName(QStringLiteral("pageSetupFirstPageLabel"));
    mark_measured(first_label, 27);

    first_page_ = new QSpinBox(page);
    first_page_->setObjectName(QStringLiteral("pageSetupFirstPage"));
    place_measured(first_page_, 125, 236, 110, 22);
    mark_measured(first_page_, 28);
    first_page_->setRange(1, 9999);
    first_page_->setValue(std::max(1, spec.first_page_number));

    page_note_ = make_measured_label(
        page,
        QStringLiteral("Adjust to is the live scaling control. Fit to needs a "
                       "width-based layout engine this port does not have."),
        20, 268, 459, 26);
    page_note_->setObjectName(QStringLiteral("pageSetupPageNote"));
    page_note_->setWordWrap(true);
    return page;
}

QWidget* PageSetupDialog::build_margins_tab(const app::PageSpec& spec) {
    auto* page = new QWidget(pages_);
    page->setObjectName(QStringLiteral("pageSetupMarginsTab"));

    auto* group = make_group(page, QStringLiteral("Margins"), 11, 10, 459, 160);
    group->setObjectName(QStringLiteral("pageSetupMarginsGroup"));

    auto* top = make_label(page, QStringLiteral("Top:"), 20, 32, 100, 16);
    top->setObjectName(QStringLiteral("pageSetupMarginTopLabel"));
    margin_top_ = new QDoubleSpinBox(page);
    margin_top_->setObjectName(QStringLiteral("pageSetupMarginTop"));
    place(margin_top_, 130, 29, 100, 22);
    margin_top_->setRange(0.0, 200.0);
    margin_top_->setDecimals(1);
    margin_top_->setSuffix(QStringLiteral(" pt"));
    margin_top_->setValue(std::clamp(spec.margin_top, 0.0, 200.0));

    auto* bottom = make_label(page, QStringLiteral("Bottom:"), 20, 62, 100, 16);
    bottom->setObjectName(QStringLiteral("pageSetupMarginBottomLabel"));
    margin_bottom_ = new QDoubleSpinBox(page);
    margin_bottom_->setObjectName(QStringLiteral("pageSetupMarginBottom"));
    place(margin_bottom_, 130, 59, 100, 22);
    margin_bottom_->setRange(0.0, 200.0);
    margin_bottom_->setDecimals(1);
    margin_bottom_->setSuffix(QStringLiteral(" pt"));
    margin_bottom_->setValue(std::clamp(spec.margin_bottom, 0.0, 200.0));

    auto* left = make_label(page, QStringLiteral("Left:"), 20, 92, 100, 16);
    left->setObjectName(QStringLiteral("pageSetupMarginLeftLabel"));
    margin_left_ = new QDoubleSpinBox(page);
    margin_left_->setObjectName(QStringLiteral("pageSetupMarginLeft"));
    place(margin_left_, 130, 89, 100, 22);
    margin_left_->setRange(0.0, 200.0);
    margin_left_->setDecimals(1);
    margin_left_->setSuffix(QStringLiteral(" pt"));
    margin_left_->setValue(std::clamp(spec.margin_left, 0.0, 200.0));

    auto* right = make_label(page, QStringLiteral("Right:"), 20, 122, 100, 16);
    right->setObjectName(QStringLiteral("pageSetupMarginRightLabel"));
    margin_right_ = new QDoubleSpinBox(page);
    margin_right_->setObjectName(QStringLiteral("pageSetupMarginRight"));
    place(margin_right_, 130, 119, 100, 22);
    margin_right_->setRange(0.0, 200.0);
    margin_right_->setDecimals(1);
    margin_right_->setSuffix(QStringLiteral(" pt"));
    margin_right_->setValue(std::clamp(spec.margin_right, 0.0, 200.0));

    auto* note = make_label(
        page,
        QStringLiteral("Margins are in points (1/72 inch). All four feed the shared "
                       "print layout; the preview and the printer use the same bands."),
        20, 176, 440, 30, true);
    note->setObjectName(QStringLiteral("pageSetupMarginsNote"));
    return page;
}

QWidget* PageSetupDialog::build_header_tab(const app::PageSpec& spec) {
    auto* page = new QWidget(pages_);
    page->setObjectName(QStringLiteral("pageSetupHeaderTab"));

    show_header_ = new QCheckBox(QStringLiteral("Show header on every page"), page);
    show_header_->setObjectName(QStringLiteral("pageSetupShowHeader"));
    place(show_header_, 11, 10, 300, 20);
    show_header_->setChecked(spec.show_header);

    auto* text_label = make_label(page, QStringLiteral("Header text:"), 11, 40, 120, 16);
    text_label->setObjectName(QStringLiteral("pageSetupHeaderTextLabel"));
    header_text_ = new QLineEdit(page);
    header_text_->setObjectName(QStringLiteral("pageSetupHeaderText"));
    place(header_text_, 11, 60, 459, 22);
    header_text_->setText(QString::fromStdString(spec.header_text));
    header_text_->setPlaceholderText(QStringLiteral("(automatic: project - report title)"));

    auto* align_label = make_label(page, QStringLiteral("Alignment:"), 11, 92, 120, 16);
    align_label->setObjectName(QStringLiteral("pageSetupHeaderAlignLabel"));
    header_align_ = new QComboBox(page);
    header_align_->setObjectName(QStringLiteral("pageSetupHeaderAlign"));
    place(header_align_, 11, 112, 120, 22);
    header_align_->addItem(QStringLiteral("Left"));
    header_align_->addItem(QStringLiteral("Center"));
    header_align_->addItem(QStringLiteral("Right"));
    header_align_->setCurrentIndex(std::clamp(spec.header_align, 0, 2));

    auto* note = make_label(page, code_note(), 11, 146, 459, 44, true);
    note->setObjectName(QStringLiteral("pageSetupHeaderNote"));
    return page;
}

QWidget* PageSetupDialog::build_footer_tab(const app::PageSpec& spec) {
    auto* page = new QWidget(pages_);
    page->setObjectName(QStringLiteral("pageSetupFooterTab"));

    show_footer_ = new QCheckBox(QStringLiteral("Show footer on every page"), page);
    show_footer_->setObjectName(QStringLiteral("pageSetupShowFooter"));
    place(show_footer_, 11, 10, 300, 20);
    show_footer_->setChecked(spec.show_footer);

    auto* text_label = make_label(page, QStringLiteral("Footer text:"), 11, 40, 120, 16);
    text_label->setObjectName(QStringLiteral("pageSetupFooterTextLabel"));
    footer_text_ = new QLineEdit(page);
    footer_text_->setObjectName(QStringLiteral("pageSetupFooterText"));
    place(footer_text_, 11, 60, 459, 22);
    footer_text_->setText(QString::fromStdString(spec.footer_text));
    footer_text_->setPlaceholderText(QStringLiteral("(automatic: date | Page p of P)"));

    auto* align_label = make_label(page, QStringLiteral("Alignment:"), 11, 92, 120, 16);
    align_label->setObjectName(QStringLiteral("pageSetupFooterAlignLabel"));
    footer_align_ = new QComboBox(page);
    footer_align_->setObjectName(QStringLiteral("pageSetupFooterAlign"));
    place(footer_align_, 11, 112, 120, 22);
    footer_align_->addItem(QStringLiteral("Left"));
    footer_align_->addItem(QStringLiteral("Center"));
    footer_align_->addItem(QStringLiteral("Right"));
    footer_align_->setCurrentIndex(std::clamp(spec.footer_align, 0, 2));

    auto* note = make_label(page, code_note(), 11, 146, 459, 44, true);
    note->setObjectName(QStringLiteral("pageSetupFooterNote"));
    return page;
}

QWidget* PageSetupDialog::build_legend_tab(const app::PageSpec& spec) {
    auto* page = new QWidget(pages_);
    page->setObjectName(QStringLiteral("pageSetupLegendTab"));

    show_legend_ = new QCheckBox(QStringLiteral("Show legend on every page"), page);
    show_legend_->setObjectName(QStringLiteral("pageSetupShowLegend"));
    place(show_legend_, 11, 10, 300, 20);
    show_legend_->setChecked(spec.show_legend);

    auto* text_label = make_label(page, QStringLiteral("Legend title:"), 11, 40, 120, 16);
    text_label->setObjectName(QStringLiteral("pageSetupLegendTextLabel"));
    legend_text_ = new QLineEdit(page);
    legend_text_->setObjectName(QStringLiteral("pageSetupLegendText"));
    place(legend_text_, 11, 60, 300, 22);
    legend_text_->setText(QString::fromStdString(spec.legend_text));
    legend_text_->setPlaceholderText(QStringLiteral("Legend"));

    auto* note = make_label(
        page,
        QStringLiteral("The legend reserves a 26 pt band above the footer and lists "
                       "the bar kinds the page draws: Task, Summary, Milestone and "
                       "Critical."),
        11, 94, 459, 44, true);
    note->setObjectName(QStringLiteral("pageSetupLegendNote"));
    return page;
}

QWidget* PageSetupDialog::build_view_tab() {
    auto* page = new QWidget(pages_);
    page->setObjectName(QStringLiteral("pageSetupViewTab"));

    print_notes_ = new QCheckBox(QStringLiteral("Print notes"), page);
    print_notes_->setObjectName(QStringLiteral("pageSetupPrintNotes"));
    place(print_notes_, 11, 10, 220, 20);
    disable_with_tip(print_notes_);

    print_blank_ = new QCheckBox(QStringLiteral("Print blank pages"), page);
    print_blank_->setObjectName(QStringLiteral("pageSetupPrintBlank"));
    place(print_blank_, 11, 36, 220, 20);
    disable_with_tip(print_blank_);

    view_note_ = make_label(
        page,
        QStringLiteral("Notes and blank-page printing have no engine in this port, "
                       "so these controls are laid out and left inert. The other "
                       "Page Setup tabs are functional."),
        11, 70, 459, 48, true);
    view_note_->setObjectName(QStringLiteral("pageSetupViewNote"));
    return page;
}

app::PageSpec PageSetupDialog::collect() const {
    app::PageSpec spec = source_;
    const app::PageSpec paper =
        paper_ != nullptr && paper_->currentIndex() == 1 ? app::PageSpec::letter_landscape()
                                                         : app::PageSpec::a4_landscape();
    const bool landscape = landscape_ != nullptr && landscape_->isChecked();
    spec.width = landscape ? paper.width : paper.height;
    spec.height = landscape ? paper.height : paper.width;
    spec.scale_percent = scale_ != nullptr ? scale_->value() : spec.scale_percent;
    spec.first_page_number =
        first_page_ != nullptr ? first_page_->value() : spec.first_page_number;
    spec.margin_top = margin_top_ != nullptr ? margin_top_->value() : spec.margin_top;
    spec.margin_bottom =
        margin_bottom_ != nullptr ? margin_bottom_->value() : spec.margin_bottom;
    spec.margin_left = margin_left_ != nullptr ? margin_left_->value() : spec.margin_left;
    spec.margin_right =
        margin_right_ != nullptr ? margin_right_->value() : spec.margin_right;
    spec.show_header = show_header_ != nullptr ? show_header_->isChecked() : spec.show_header;
    spec.show_footer = show_footer_ != nullptr ? show_footer_->isChecked() : spec.show_footer;
    spec.show_legend = show_legend_ != nullptr ? show_legend_->isChecked() : spec.show_legend;
    if (header_text_ != nullptr) {
        spec.header_text = header_text_->text().toStdString();
    }
    if (footer_text_ != nullptr) {
        spec.footer_text = footer_text_->text().toStdString();
    }
    if (legend_text_ != nullptr) {
        spec.legend_text = legend_text_->text().toStdString();
    }
    spec.header_align =
        header_align_ != nullptr ? std::clamp(header_align_->currentIndex(), 0, 2)
                                 : spec.header_align;
    spec.footer_align =
        footer_align_ != nullptr ? std::clamp(footer_align_->currentIndex(), 0, 2)
                                 : spec.footer_align;
    return spec;
}

void PageSetupDialog::accept() {
    result_ = collect();
    QDialog::accept();
}

}  // namespace msqt
