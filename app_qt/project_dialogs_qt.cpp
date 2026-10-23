// app_qt/project_dialogs_qt.cpp -- see project_dialogs_qt.h.

#include "project_dialogs_qt.h"

#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

#include "date.h"

namespace msqt {
namespace {

using namespace project_dialog_geometry;

QDate to_qdate(app::date32 day) {
    if (day == app::kDateNa) {
        return QDate();
    }
    const core::sched::CivilDate civil = core::sched::to_civil(day << 16);
    return QDate(civil.year, civil.month, civil.day);
}

QLabel* make_label(QWidget* parent, const QString& text, int width) {
    auto* label = new QLabel(text, parent);
    label->setFixedWidth(width);
    label->setFixedHeight(kRowHeight);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

QDateEdit* make_date_edit(QWidget* parent, app::date32 day, int width) {
    auto* edit = new QDateEdit(parent);
    edit->setFixedSize(width, kRowHeight);
    edit->setDisplayFormat(QStringLiteral("ddd dd/MM/yy"));
    edit->setCalendarPopup(true);
    edit->setDateRange(QDate(1984, 1, 1), QDate(2149, 12, 31));
    edit->setSpecialValueText(QStringLiteral("NA"));
    const QDate value = to_qdate(day);
    edit->setDate(value.isValid() ? value : edit->minimumDate());
    edit->setEnabled(false);
    edit->setToolTip(QString::fromUtf8(kNotImplemented));
    return edit;
}

QPushButton* make_button(QWidget* parent, const QString& text) {
    auto* button = new QPushButton(text, parent);
    button->setMinimumHeight(26);
    return button;
}

}  // namespace

ProjectInformationDialog::ProjectInformationDialog(const app::ProjectInformation& current,
                                                   QWidget* parent)
    : QDialog(parent), current_(current), original_priority_(current.priority) {
    setWindowTitle(current.title.empty()
                       ? QStringLiteral("Project Information")
                       : QStringLiteral("Project Information for '%1'")
                             .arg(QString::fromStdString(current.title)));
    setFixedSize(kProjectInformationWidth, kProjectInformationHeight);

    auto* outer = new QVBoxLayout(this);
    // The captured rows are widely spaced (the audit frame's client area puts
    // the first three field rows at 0.047/0.18/0.32 of its height).  The layout
    // matches those measured fractions rather than a generic 8-px form.
    outer->setContentsMargins(kMargin, kTopMargin, kMargin, kMargin);
    outer->setSpacing(kSpacing);

    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(kSpacing);
    grid->setVerticalSpacing(kRowSpacing);
    grid->setColumnMinimumWidth(0, kLabelWidth);
    grid->setColumnMinimumWidth(2, kRightLabelWidth);

    start_ = make_date_edit(this, current.start_day, kLeftFieldWidth);
    finish_ = make_date_edit(this, current.finish_day, kLeftFieldWidth);
    current_date_ = make_date_edit(this, current.current_day, kRightFieldWidth);
    status_ = make_date_edit(this, app::kDateNa, kRightFieldWidth);  // no decoded field

    schedule_from_ = new QComboBox(this);
    schedule_from_->setFixedSize(kLeftFieldWidth, kRowHeight);
    schedule_from_->addItem(QStringLiteral("Project Start Date"));
    schedule_from_->addItem(QStringLiteral("Project Finish Date"));
    schedule_from_->setCurrentIndex(0);
    schedule_from_->setEnabled(false);
    schedule_from_->setToolTip(QString::fromUtf8(kNotImplemented));

    calendar_ = new QComboBox(this);
    calendar_->setFixedSize(kRightFieldWidth, kRowHeight);
    for (const std::string& name : current.calendars) {
        calendar_->addItem(QString::fromStdString(name));
    }
    if (!current.calendar.empty()) {
        calendar_->setCurrentText(QString::fromStdString(current.calendar));
    }
    if (calendar_->count() == 0) {
        calendar_->addItem(QStringLiteral("None"));
    }
    calendar_->setEnabled(false);
    calendar_->setToolTip(QString::fromUtf8(kNotImplemented));

    auto* note = new QLabel(QStringLiteral("All tasks begin as soon as possible."), this);
    note->setFixedHeight(kRowHeight);
    note->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    priority_ = new QSpinBox(this);
    priority_->setFixedSize(kRightFieldWidth, kRowHeight);
    priority_->setRange(app::kMinPriority, app::kMaxPriority);
    priority_->setSingleStep(5);
    priority_->setValue(current.priority);

    grid->addWidget(make_label(this, QStringLiteral("Start date:"), kLabelWidth), 0, 0);
    grid->addWidget(start_, 0, 1);
    grid->addWidget(make_label(this, QStringLiteral("Current date:"), kRightLabelWidth), 0, 2);
    grid->addWidget(current_date_, 0, 3);

    grid->addWidget(make_label(this, QStringLiteral("Finish date:"), kLabelWidth), 1, 0);
    grid->addWidget(finish_, 1, 1);
    grid->addWidget(make_label(this, QStringLiteral("Status date:"), kRightLabelWidth), 1, 2);
    grid->addWidget(status_, 1, 3);

    grid->addWidget(make_label(this, QStringLiteral("Schedule from:"), kLabelWidth), 2, 0);
    grid->addWidget(schedule_from_, 2, 1);
    grid->addWidget(make_label(this, QStringLiteral("Calendar:"), kRightLabelWidth), 2, 2);
    grid->addWidget(calendar_, 2, 3);

    grid->addWidget(note, 3, 0, 1, 2);
    grid->addWidget(make_label(this, QStringLiteral("Priority:"), kRightLabelWidth), 3, 2);
    grid->addWidget(priority_, 3, 3);

    grid->setColumnStretch(1, 1);
    outer->addLayout(grid);

    auto* enterprise_title = new QLabel(QStringLiteral("Enterprise Custom Fields"), this);
    QFont title_font = enterprise_title->font();
    title_font.setBold(true);
    enterprise_title->setFont(title_font);
    enterprise_title->setFixedHeight(20);
    outer->addWidget(enterprise_title);

    auto* enterprise_form = new QFormLayout();
    enterprise_form->setContentsMargins(0, 0, 0, 0);
    enterprise_form->setHorizontalSpacing(kSpacing);
    enterprise_form->setVerticalSpacing(4);
    auto* department = new QComboBox(this);
    department->setObjectName(QStringLiteral("departmentCombo"));
    department->setFixedSize(kLeftFieldWidth, kRowHeight);
    department->setEnabled(false);
    department->setToolTip(QString::fromUtf8(kNotImplemented));
    enterprise_form->addRow(make_label(this, QStringLiteral("Department:"), kLabelWidth),
                            department);
    outer->addLayout(enterprise_form);

    grid_ = new QTableWidget(0, 2, this);
    grid_->setObjectName(QStringLiteral("enterpriseFieldsGrid"));
    grid_->setHorizontalHeaderLabels(
        {QStringLiteral("Custom Field name"), QStringLiteral("Value")});
    grid_->horizontalHeader()->setStretchLastSection(true);
    grid_->verticalHeader()->setVisible(false);
    grid_->setEnabled(false);
    grid_->setToolTip(QString::fromUtf8(kNotImplemented));
    grid_->setMinimumHeight(96);
    outer->addWidget(grid_, 1);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(kSpacing);
    help_ = make_button(this, QStringLiteral("Help"));
    help_->setObjectName(QStringLiteral("helpButton"));
    help_->setEnabled(false);
    help_->setToolTip(QString::fromUtf8(kNotImplemented));
    statistics_ = make_button(this, QStringLiteral("Statistics..."));
    statistics_->setObjectName(QStringLiteral("statisticsButton"));
    statistics_->setEnabled(false);
    statistics_->setToolTip(QString::fromUtf8(kNotImplemented));
    buttons->addWidget(help_);
    buttons->addWidget(statistics_);
    buttons->addStretch(1);
    ok_ = make_button(this, QStringLiteral("OK"));
    ok_->setObjectName(QStringLiteral("okButton"));
    ok_->setDefault(true);
    cancel_ = make_button(this, QStringLiteral("Cancel"));
    cancel_->setObjectName(QStringLiteral("cancelButton"));
    buttons->addWidget(ok_);
    buttons->addWidget(cancel_);
    outer->addLayout(buttons);

    connect(ok_, &QPushButton::clicked, this, &ProjectInformationDialog::accept);
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    connect(priority_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { priority_changed_ = value != original_priority_; });
}

int ProjectInformationDialog::priority() const {
    return priority_ != nullptr ? priority_->value() : original_priority_;
}

void ProjectInformationDialog::accept() {
    if (!app::valid_project_priority(priority())) {
        return;  // the spin box's range makes this unreachable; never fake it
    }
    priority_changed_ = priority() != original_priority_;
    QDialog::accept();
}

bool edit_project_information(QWidget* parent, const app::ProjectInformation& current,
                              int* priority) {
    ProjectInformationDialog dialog(current, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (priority != nullptr) {
        *priority = dialog.priority();
    }
    return true;
}

}  // namespace msqt
