// app_qt/report_dialogs_qt.cpp -- see report_dialogs_qt.h.

#include "report_dialogs_qt.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>

namespace msqt {
namespace {

using namespace report_dialog_geometry;

// Project's dialog font is the 9 pt UI face, not the shell's 11 pt sheet face;
// the captured label boxes (13 px rows, 439 px two-line description) only fit at
// that size.  Children inherit it, so set it before building them.
QFont dialog_font() {
    QFont font = QApplication::font();
    font.setPointSize(9);
    return font;
}

void place(QWidget* widget, int x, int y, int width, int height) {
    widget->setGeometry(x, y, width, height);
}

// The gate matches every captured control (by its Project control id) to the
// live widget carrying this property and compares the live rectangle with the
// committed measurement; the id is also what keeps a rectangle from drifting
// onto a different control.
void mark_measured(QWidget* widget, int control_id) {
    widget->setProperty("measuredControlId", control_id);
}

void disable_with_tip(QWidget* widget) {
    widget->setEnabled(false);
    widget->setToolTip(QString::fromUtf8(kNotImplemented));
}

QLabel* make_label(QWidget* parent, const QString& text, int x, int y, int width,
                   int height, int control_id, bool wrap = false) {
    auto* label = new QLabel(text, parent);
    place(label, x, y, width, height);
    label->setWordWrap(wrap);
    mark_measured(label, control_id);
    return label;
}

QPushButton* make_button(QWidget* parent, const QString& text, int x, int y, int width,
                         int height, int control_id, bool enabled) {
    auto* button = new QPushButton(text, parent);
    place(button, x, y, width, height);
    mark_measured(button, control_id);
    if (!enabled) {
        disable_with_tip(button);
    }
    return button;
}

// The captured combos are owner-drawn: their item strings cannot be read
// (CB_GETLBTEXT returns CB_ERR), so the port keeps the measured count and
// current index and shows the captured visible text at the current index.
QComboBox* make_combo(QWidget* parent, int x, int y, int width, int height, int count,
                      int current, const QString& text, int control_id) {
    auto* combo = new QComboBox(parent);
    place(combo, x, y, width, height);
    mark_measured(combo, control_id);
    for (int index = 0; index < count; ++index) {
        combo->addItem(QString());
    }
    if (current >= 0 && current < count) {
        combo->setItemText(current, text);
        combo->setCurrentIndex(current);
    }
    disable_with_tip(combo);
    return combo;
}

}  // namespace

// ---------------------------------------------------------------------------
// The two-row tab control
// ---------------------------------------------------------------------------
ReportTabStrip::ReportTabStrip(const QStringList& top, const QStringList& bottom,
                               const QString& selected, QWidget* parent)
    : QWidget(parent), top_(top), bottom_(bottom), selected_(selected) {}

QSize ReportTabStrip::sizeHint() const {
    return QSize(375, 50 + 2 * row_height());
}

int ReportTabStrip::row_height() const {
    return std::max(18, QFontMetrics(font()).height() + 6);
}

int ReportTabStrip::tab_width(const QString& label) const {
    return std::max(36, QFontMetrics(font()).horizontalAdvance(label) + 18);
}

void ReportTabStrip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QPalette& palette = this->palette();
    const QPalette::ColorGroup group =
        isEnabled() ? QPalette::Active : QPalette::Disabled;
    const QColor text = palette.color(group, QPalette::WindowText);
    const QColor face = palette.color(group, QPalette::Button);
    const QColor light = palette.color(group, QPalette::Light);
    const QColor mid = palette.color(group, QPalette::Mid);
    const QColor base = palette.color(group, QPalette::Base);

    const int row = row_height();
    const int page_top = 2 * row;
    const QRect page(0, page_top, width(), height() - page_top);
    painter.fillRect(rect(), palette.color(QPalette::Window));
    painter.fillRect(page, base);
    painter.setPen(mid);
    painter.drawLine(page.left(), page.top(), page.right(), page.top());
    painter.drawLine(page.left(), page.top(), page.left(), page.bottom() - 1);
    painter.drawLine(page.right() - 1, page.top(), page.right() - 1, page.bottom() - 1);
    painter.drawLine(page.left(), page.bottom() - 1, page.right(), page.bottom() - 1);

    auto draw_row = [&](const QStringList& labels, int top) {
        int x = 1;
        for (const QString& label : labels) {
            const int width = tab_width(label);
            const QRect tab(x, top + 1, width, row);
            const bool selected = label == selected_;
            if (selected) {
                // The selected tab merges with the page: fill it and erase the
                // page's top border under it.
                painter.fillRect(QRect(x, top + 1, width, row + 2), base);
                painter.setPen(light);
                painter.drawLine(tab.left(), tab.top(), tab.right() - 1, tab.top());
                painter.drawLine(tab.left(), tab.top(), tab.left(), tab.bottom() - 1);
                painter.setPen(mid);
                painter.drawLine(tab.right() - 1, tab.top(), tab.right() - 1,
                                 tab.bottom() - 1);
            } else {
                painter.fillRect(tab, face);
                painter.setPen(light);
                painter.drawLine(tab.left(), tab.top(), tab.right() - 1, tab.top());
                painter.drawLine(tab.left(), tab.top(), tab.left(), tab.bottom() - 1);
                painter.setPen(mid);
                painter.drawLine(tab.right() - 1, tab.top(), tab.right() - 1,
                                 tab.bottom() - 1);
                painter.drawLine(tab.left(), tab.bottom() - 1, tab.right(),
                                 tab.bottom() - 1);
            }
            painter.setPen(text);
            painter.drawText(tab, Qt::AlignCenter, label);
            x += width;
        }
    };
    draw_row(top_, 0);
    draw_row(bottom_, row);
}

// ---------------------------------------------------------------------------
// Visual Reports - Create Report (625x541)
// ---------------------------------------------------------------------------
VisualReportsDialog::VisualReportsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Visual Reports - Create Report"));
    setFixedSize(kVisualReportsWidth, kVisualReportsHeight);
    setFont(dialog_font());

    // The captured tab control is one control [15,44,375,317] with seven
    // owner-drawn items on two rows; the list below sits over its page area, so
    // the tab strip is created first and the list paints above it.
    tabs_ = new ReportTabStrip(
        {QStringLiteral("Task Summary"), QStringLiteral("Resource Summary"),
         QStringLiteral("Assignment Summary")},
        {QStringLiteral("All"), QStringLiteral("Task Usage"),
         QStringLiteral("Resource Usage"), QStringLiteral("Assignment Usage")},
        QStringLiteral("All"), this);
    tabs_->setObjectName(QStringLiteral("visualReportsTabs"));
    place(tabs_, 15, 44, 375, 317);
    mark_measured(tabs_, 8);
    disable_with_tip(tabs_);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("visualReportsTemplates"));
    place(list_, 26, 94, 352, 255);
    mark_measured(list_, 9);
    list_->addItems({QStringLiteral("Baseline Cost Report"),
                     QStringLiteral("Baseline Work Report"),
                     QStringLiteral("Budget Cost Report"),
                     QStringLiteral("Budget Work Report"),
                     QStringLiteral("Cash Flow Report"),
                     QStringLiteral("Earned Value Over Time Report"),
                     QStringLiteral("Resource Cost Summary Report"),
                     QStringLiteral("Resource Remaining Work Report"),
                     QStringLiteral("Resource Work Availability Report"),
                     QStringLiteral("Resource Work Summary Report")});
    list_->setCurrentRow(0);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    disable_with_tip(list_);

    make_label(this, QStringLiteral("Show report templates created in:"), 15, 28, 174,
               13, 17);

    excel_ = new QCheckBox(QStringLiteral("Microsoft &Excel"), this);
    place(excel_, 198, 27, 97, 16);
    mark_measured(excel_, 15);
    excel_->setChecked(true);
    disable_with_tip(excel_);

    visio_ = new QCheckBox(QStringLiteral("Microsoft &Visio"), this);
    place(visio_, 304, 27, 97, 16);
    mark_measured(visio_, 16);
    visio_->setChecked(true);
    disable_with_tip(visio_);

    new_template_ = make_button(this, QStringLiteral("&New Template..."), 399, 84, 155,
                                23, 1, false);
    edit_template_ = make_button(this, QStringLiteral("E&dit Template..."), 399, 114,
                                 155, 23, 2, false);
    manage_template_ = make_button(this, QStringLiteral("&Manage Template..."), 399,
                                   144, 155, 23, 20, false);

    auto* sample = new QGroupBox(QStringLiteral("Sample"), this);
    place(sample, 399, 170, 202, 191);
    mark_measured(sample, 6);

    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("visualReportsSampleFrame"));
    frame->setFrameShape(QFrame::Panel);
    frame->setFrameShadow(QFrame::Sunken);
    frame->setLineWidth(1);
    place(frame, 408, 190, 184, 164);
    mark_measured(frame, 12);

    // The capture shows a sample chart for the selected template.  The port has
    // no report engine, so it shows the capture's own alternate state instead of
    // drawing a chart it cannot produce.
    auto* sample_note = new QLabel(QStringLiteral("No sample available."), this);
    place(sample_note, 447, 265, 105, 13);
    mark_measured(sample_note, 14);

    make_label(this, QStringLiteral("Select level of &usage data to include in the report:"),
               15, 367, 256, 13, 24);

    level_ = make_combo(this, 280, 367, 76, 21, 5, 3, QStringLiteral("Weeks"), 25);

    include_ = new QCheckBox(QStringLiteral("&Include report templates from:"), this);
    place(include_, 15, 391, 176, 16);
    mark_measured(include_, 21);
    include_->setChecked(false);
    disable_with_tip(include_);

    include_edit_ = new QLineEdit(this);
    place(include_edit_, 32, 410, 495, 22);
    mark_measured(include_edit_, 22);
    disable_with_tip(include_edit_);

    modify_ = make_button(this, QStringLiteral("M&odify..."), 535, 410, 75, 23, 23,
                          false);

    auto* separator = new QFrame(this);
    separator->setObjectName(QStringLiteral("visualReportsSeparator"));
    separator->setStyleSheet(QStringLiteral("border: 0; border-top: 1px solid #d9d9d9;"));
    place(separator, 6, 484, 613, 21);
    mark_measured(separator, 18);

    help_ = make_button(this, QStringLiteral("&Help"), 6, 511, 75, 23, 19, false);
    save_data_ = make_button(this, QStringLiteral("&Save Data..."), 90, 511, 77, 23, 3,
                             false);
    view_ = make_button(this, QStringLiteral("Vie&w"), 460, 511, 75, 23, 4, false);
    close_ = make_button(this, QStringLiteral("&Close"), 544, 511, 75, 23, 5, true);

    // The captured blank status static (id 11) carries the port's inline note:
    // the reason is on screen, not only in the disabled controls' tooltips.
    note_ = new QLabel(
        QStringLiteral("Visual reports are not generated in this port; the controls "
                       "are laid out and inert."),
        this);
    place(note_, 15, 444, 595, 13);
    mark_measured(note_, 11);

    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
}

// ---------------------------------------------------------------------------
// Compare Project Versions (469x251)
// ---------------------------------------------------------------------------
CompareProjectsDialog::CompareProjectsDialog(const QString& project_name,
                                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Compare Project Versions"));
    setFixedSize(kCompareProjectsWidth, kCompareProjectsHeight);
    setFont(dialog_font());

    const QString pattern =
        QStringLiteral("Compare the current &project (%1) to this previous version:");
    const QString fixed_text = pattern.arg(QString());
    const QFontMetrics metrics(font());
    const int available = kCompareProjectsWidth - 12 - 16;
    QString name = project_name;
    if (metrics.horizontalAdvance(pattern.arg(name)) > available) {
        // Project grows the dialog with a long name; the port keeps the measured
        // width and elides the middle of the name in the captured caption.
        const int room =
            std::max(30, available - metrics.horizontalAdvance(fixed_text));
        name = metrics.elidedText(name, Qt::ElideMiddle, room);
    }
    compare_group_ = new QGroupBox(pattern.arg(name), this);
    place(compare_group_, 6, 7, 457, 50);
    mark_measured(compare_group_, 1);

    fields_group_ =
        new QGroupBox(QStringLiteral("Select the fields to be used in the comparison:"),
                      this);
    place(fields_group_, 6, 60, 457, 150);
    mark_measured(fields_group_, 4);

    previous_ = make_combo(this, 15, 28, 355, 21, 0, -1, QString(), 2);
    browse_ = make_button(this, QStringLiteral("&Browse..."), 379, 27, 75, 23, 3, false);

    auto* description =
        new QLabel(QStringLiteral("For each column in the specified tables, the report "
                                  "will display a column with the data from both "
                                  "versions and a column showing the difference between "
                                  "the values."),
                   this);
    place(description, 15, 88, 439, 26);
    description->setWordWrap(true);
    mark_measured(description, 5);

    make_label(this, QStringLiteral("&Task Table:"), 15, 154, 53, 13, 6);
    task_table_ = make_combo(this, 103, 150, 351, 21, 22, 10, QStringLiteral("Entry"), 8);
    make_label(this, QStringLiteral("&Resource Table:"), 15, 186, 79, 13, 7);
    resource_table_ =
        make_combo(this, 103, 182, 351, 21, 12, 3, QStringLiteral("Entry"), 9);

    ok_ = make_button(this, QStringLiteral("&OK"), 304, 221, 75, 23, 10, false);
    cancel_ = make_button(this, QStringLiteral("&Cancel"), 388, 221, 75, 23, 11, true);

    note_ = new QLabel(
        QStringLiteral("Project comparison is not implemented in this port; the "
                       "controls are laid out and inert."),
        this);
    place(note_, 15, 118, 439, 26);
    note_->setObjectName(QStringLiteral("compareProjectsNote"));
    note_->setWordWrap(true);

    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
}

}  // namespace msqt
