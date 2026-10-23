// app_qt/project_dialogs_qt.cpp -- see project_dialogs_qt.h.

#include "project_dialogs_qt.h"

#include <QAbstractItemView>
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
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
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

// ---------------------------------------------------------------------------
// Change Working Time (wave 26, W26-5.2)
// ---------------------------------------------------------------------------
namespace {

const QString kDisabledTip = QString::fromUtf8(project_dialog_geometry::kNotImplemented);

QColor legend_colour(const char* hex) {
    return QColor(QString::fromLatin1(hex));
}

// One legend swatch: the captured fill, a 1-px frame, and the "31" mark the
// capture shows on the three edited-day kinds.
class LegendSwatch final : public QWidget {
  public:
    LegendSwatch(QColor colour, bool with_day, QWidget* parent)
        : QWidget(parent), colour_(colour), with_day_(with_day) {
        setFixedSize(project_dialog_geometry::kLegendSwatch,
                     project_dialog_geometry::kLegendSwatch);
        setToolTip(QStringLiteral("the fill measured from the audit capture"));
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), colour_);
        painter.setPen(QColor(160, 160, 160));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
        if (with_day_) {
            painter.setPen(Qt::black);
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("31"));
        }
    }

  private:
    QColor colour_;
    bool with_day_ = false;
};

// One day cell of the month grid.  The captured fills: white working, grey
// non-working, pale cyan exception day, pale yellow nondefault work week, black
// for the selected day.  The state is also exposed as dynamic properties so the
// gate can prove the LIVE widget, not the model alone.
class WorkingTimeDayCell final : public QWidget {
  public:
    WorkingTimeDayCell(const app::WorkingTimeDay& day, bool selected, QWidget* parent)
        : QWidget(parent), day_(day), selected_(selected) {
        setFixedSize(project_dialog_geometry::kDayCellWidth,
                     project_dialog_geometry::kDayCellHeight);
        setObjectName(QStringLiteral("workingDay%1").arg(day.day));
        setProperty("working", day.working);
        setProperty("exception", day.exception);
        setProperty("nondefaultWorkWeek", day.nondefault_work_week);
        setProperty("selected", selected);
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void(int)> on_click;

  protected:
    void mousePressEvent(QMouseEvent*) override {
        if (on_click) {
            on_click(day_.day);
        }
    }

    void paintEvent(QPaintEvent*) override {
        QColor fill(255, 255, 255);
        if (!day_.working) {
            fill = QColor(0xD2, 0xD2, 0xD2);
        }
        if (day_.exception) {
            fill = QColor(0xBF, 0xDB, 0xDC);
        }
        if (day_.nondefault_work_week) {
            fill = QColor(0xD8, 0xE0, 0xC8);
        }
        if (selected_) {
            fill = QColor(0, 0, 0);
        }
        QPainter painter(this);
        painter.fillRect(rect(), fill);
        painter.setPen(selected_ ? QColor(255, 255, 255) : QColor(0, 0, 0));
        painter.drawText(rect(), Qt::AlignCenter, QString::number(day_.day));
        painter.setPen(QColor(160, 160, 160));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

  private:
    app::WorkingTimeDay day_;
    bool selected_ = false;
};

QString month_title(int year, int month) {
    static const char* const kMonths[] = {"January", "February", "March",     "April",
                                          "May",     "June",     "July",      "August",
                                          "September", "October", "November", "December"};
    if (month < 1 || month > 12) {
        return QStringLiteral("NA");
    }
    return QStringLiteral("%1 %2").arg(QString::fromLatin1(kMonths[month - 1])).arg(year);
}

QTableWidget* make_table(QWidget* parent) {
    auto* table = new QTableWidget(0, 4, parent);
    table->setHorizontalHeaderLabels({QString(), QStringLiteral("Name"),
                                      QStringLiteral("Start"), QStringLiteral("Finish")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setColumnWidth(0, 28);
    return table;
}

}  // namespace

ChangeWorkingTimeDialog::ChangeWorkingTimeDialog(std::string mpp_path,
                                                 const core::sheet::SheetFormat& format,
                                                 int year, int month, int selected_day,
                                                 QWidget* parent)
    : QDialog(parent), path_(std::move(mpp_path)), format_(format), year_(year),
      month_(month), selected_day_(selected_day) {
    setWindowTitle(QStringLiteral("Change Working Time"));
    setFixedSize(project_dialog_geometry::kChangeWorkingTimeWidth,
                 project_dialog_geometry::kChangeWorkingTimeHeight);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(project_dialog_geometry::kMargin,
                              project_dialog_geometry::kMargin,
                              project_dialog_geometry::kMargin,
                              project_dialog_geometry::kMargin);
    outer->setSpacing(6);

    auto* picker_row = new QHBoxLayout();
    picker_row->setContentsMargins(0, 0, 0, 0);
    picker_row->setSpacing(6);
    auto* picker_label = new QLabel(QStringLiteral("For calendar:"), this);
    picker_label->setFixedWidth(82);
    picker_row->addWidget(picker_label);
    calendar_ = new QComboBox(this);
    calendar_->setObjectName(QStringLiteral("calendarCombo"));
    calendar_->setFixedWidth(260);
    picker_row->addWidget(calendar_);
    picker_row->addStretch(1);
    outer->addLayout(picker_row);

    calendar_line_ = new QLabel(this);
    calendar_line_->setObjectName(QStringLiteral("calendarLine"));
    outer->addWidget(calendar_line_);

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(10);

    auto* legend_box = new QGroupBox(QStringLiteral("Legend"), this);
    legend_box->setObjectName(QStringLiteral("legendBox"));
    legend_box->setFixedWidth(150);
    auto* legend_layout = new QVBoxLayout(legend_box);
    legend_layout->setContentsMargins(8, 16, 8, 8);
    legend_layout->setSpacing(4);
    const struct {
        const char* label;
        const char* colour;
        bool with_day;
    } kLegend[] = {
        {"Working", project_dialog_geometry::kLegendWorkingColour, false},
        {"Nonworking", project_dialog_geometry::kLegendNonworkingColour, false},
        {"Edited working hours", project_dialog_geometry::kLegendWorkingColour, true},
        {"Exception day", project_dialog_geometry::kLegendExceptionColour, true},
        {"Nondefault work week", project_dialog_geometry::kLegendWorkWeekColour, true},
    };
    for (int index = 0; index < 5; ++index) {
        if (index == 3) {
            auto* divider = new QLabel(QStringLiteral("On this calendar:"), legend_box);
            legend_layout->addWidget(divider);
        }
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* swatch = new LegendSwatch(legend_colour(kLegend[index].colour),
                                        kLegend[index].with_day, legend_box);
        swatch->setObjectName(QStringLiteral("legendSwatch%1").arg(index));
        swatches_.push_back(swatch);
        row->addWidget(swatch);
        auto* text = new QLabel(QString::fromLatin1(kLegend[index].label), legend_box);
        text->setWordWrap(true);
        row->addWidget(text, 1);
        legend_layout->addLayout(row);
    }
    legend_layout->addStretch(1);
    body->addWidget(legend_box);

    month_host_ = new QWidget(this);
    month_host_->setObjectName(QStringLiteral("monthHost"));
    month_host_->setFixedWidth(274);
    auto* month_layout = new QVBoxLayout(month_host_);
    month_layout->setContentsMargins(0, 0, 0, 0);
    month_layout->setSpacing(4);
    auto* hint = new QLabel(QStringLiteral("Click on a day to see its working times:"),
                            month_host_);
    hint->setWordWrap(true);
    month_layout->addWidget(hint);
    auto* month_header = new QHBoxLayout();
    month_header->setContentsMargins(0, 0, 0, 0);
    month_header->setSpacing(4);
    previous_ = new QPushButton(QStringLiteral("<"), month_host_);
    previous_->setFixedSize(26, 22);
    month_label_ = new QLabel(month_title(year, month), month_host_);
    month_label_->setObjectName(QStringLiteral("monthLabel"));
    month_label_->setAlignment(Qt::AlignCenter);
    next_ = new QPushButton(QStringLiteral(">"), month_host_);
    next_->setFixedSize(26, 22);
    month_header->addWidget(previous_);
    month_header->addWidget(month_label_, 1);
    month_header->addWidget(next_);
    month_layout->addLayout(month_header);

    day_grid_ = new QGridLayout();
    day_grid_->setContentsMargins(0, 0, 0, 0);
    day_grid_->setHorizontalSpacing(0);
    day_grid_->setVerticalSpacing(0);
    const auto& weekdays = app::working_time_weekday_header();
    for (int column = 0; column < 7; ++column) {
        auto* header = new QLabel(
            QString::fromStdString(weekdays[static_cast<std::size_t>(column)]), month_host_);
        header->setAlignment(Qt::AlignCenter);
        header->setFixedSize(project_dialog_geometry::kDayCellWidth,
                             project_dialog_geometry::kDayCellHeight);
        QFont bold = header->font();
        bold.setBold(true);
        header->setFont(bold);
        day_grid_->addWidget(header, 0, column);
    }
    month_layout->addLayout(day_grid_);
    month_layout->addStretch(1);
    body->addWidget(month_host_);

    auto* right_host = new QWidget(this);
    right_host->setObjectName(QStringLiteral("workingTimesHost"));
    auto* right_layout = new QVBoxLayout(right_host);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(10);
    right_layout->addWidget(new QLabel(QStringLiteral("Working times"), right_host));
    hours_ = new QLabel(right_host);
    hours_->setObjectName(QStringLiteral("workingTimesLabel"));
    hours_->setWordWrap(true);
    right_layout->addWidget(hours_);
    based_on_ = new QLabel(right_host);
    based_on_->setObjectName(QStringLiteral("basedOnLabel"));
    based_on_->setWordWrap(true);
    right_layout->addWidget(based_on_);
    right_layout->addStretch(1);
    body->addWidget(right_host, 1);

    outer->addLayout(body);

    auto* tables_row = new QHBoxLayout();
    tables_row->setContentsMargins(0, 0, 0, 0);
    tables_row->setSpacing(8);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("workingTables"));
    exceptions_ = make_table(tabs_);
    exceptions_->setObjectName(QStringLiteral("exceptionsTable"));
    exceptions_->setToolTip(
        QStringLiteral("exception names are not decoded in this port (client-data rule)"));
    work_weeks_ = make_table(tabs_);
    work_weeks_->setObjectName(QStringLiteral("workWeeksTable"));
    work_weeks_->setToolTip(
        QStringLiteral("work-week names are not decoded in this port (client-data rule)"));
    tabs_->addTab(exceptions_, QStringLiteral("Exceptions"));
    tabs_->addTab(work_weeks_, QStringLiteral("Work Weeks"));
    tables_row->addWidget(tabs_, 1);
    auto* table_buttons = new QVBoxLayout();
    table_buttons->setContentsMargins(0, 0, 0, 0);
    table_buttons->setSpacing(6);
    details_ = make_button(this, QStringLiteral("Details..."));
    details_->setObjectName(QStringLiteral("detailsButton"));
    details_->setEnabled(false);
    details_->setToolTip(kDisabledTip);
    delete_ = make_button(this, QStringLiteral("Delete"));
    delete_->setObjectName(QStringLiteral("deleteButton"));
    delete_->setEnabled(false);
    delete_->setToolTip(kDisabledTip);
    table_buttons->addWidget(details_);
    table_buttons->addWidget(delete_);
    table_buttons->addStretch(1);
    tables_row->addLayout(table_buttons);
    outer->addLayout(tables_row, 1);

    auto* bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(8);
    options_ = make_button(this, QStringLiteral("Options..."));
    options_->setObjectName(QStringLiteral("optionsButton"));
    options_->setEnabled(false);
    options_->setToolTip(kDisabledTip);
    bottom->addWidget(options_);
    bottom->addStretch(1);
    ok_ = make_button(this, QStringLiteral("OK"));
    ok_->setObjectName(QStringLiteral("okButton"));
    ok_->setDefault(true);
    cancel_ = make_button(this, QStringLiteral("Cancel"));
    cancel_->setObjectName(QStringLiteral("cancelButton"));
    bottom->addWidget(ok_);
    bottom->addWidget(cancel_);
    outer->addLayout(bottom);

    connect(ok_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    connect(calendar_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        reload(text.toStdString(), year_, month_, 1);
    });
    connect(previous_, &QPushButton::clicked, this, [this]() {
        const int month = month_ == 1 ? 12 : month_ - 1;
        const int year = month_ == 1 ? year_ - 1 : year_;
        set_month(year, month);
    });
    connect(next_, &QPushButton::clicked, this, [this]() {
        const int month = month_ == 12 ? 1 : month_ + 1;
        const int year = month_ == 12 ? year_ + 1 : year_;
        set_month(year, month);
    });

    reload(std::string(), year, month, selected_day);
}

void ChangeWorkingTimeDialog::reload(const std::string& calendar, int year, int month, int day) {
    app::WorkingTimeModel next;
    std::string error;
    const bool loaded = app::load_working_time(path_, calendar, year, month, day, format_, next,
                                               &error);
    error_ = loaded ? std::string() : error;
    model_ = std::move(next);
    year_ = model_.year != 0 ? model_.year : year;
    month_ = model_.month != 0 ? model_.month : month;
    selected_day_ = day;

    {
        const QSignalBlocker blocker(calendar_);
        calendar_->clear();
        for (const std::string& name : model_.calendars) {
            calendar_->addItem(QString::fromStdString(name));
        }
        if (!model_.selected_calendar.empty()) {
            calendar_->setCurrentText(QString::fromStdString(model_.selected_calendar));
        }
        calendar_->setEnabled(model_.decoded && !model_.calendars.empty());
    }
    if (!model_.decoded) {
        calendar_line_->setText(QString::fromStdString(error_));
    } else if (model_.selected_is_base) {
        calendar_line_->setText(
            QStringLiteral("Calendar '%1' is a base calendar.")
                .arg(QString::fromStdString(model_.selected_calendar)));
    } else {
        calendar_line_->setText(
            QStringLiteral("Calendar '%1' is a derived calendar.")
                .arg(QString::fromStdString(model_.selected_calendar)));
    }
    month_label_->setText(month_title(year_, month_));
    hours_->setText(QString::fromStdString(model_.selected_day_hours));
    based_on_->setText(QStringLiteral("Based on\n%1")
                           .arg(QString::fromStdString(model_.based_on_text)));

    for (QWidget* cell : cells_) {
        day_grid_->removeWidget(cell);
        delete cell;
    }
    cells_.clear();
    for (std::size_t index = 0; index < model_.days.size(); ++index) {
        const app::WorkingTimeDay& day_model = model_.days[index];
        const int slot = model_.first_weekday_monday0 + static_cast<int>(index);
        auto* cell = new WorkingTimeDayCell(day_model, day_model.day == selected_day_,
                                            month_host_);
        cell->on_click = [this](int chosen) { select_day(chosen); };
        day_grid_->addWidget(cell, 1 + slot / 7, slot % 7);
        cells_.push_back(cell);
    }

    const auto fill_table = [](QTableWidget* table,
                               const std::vector<app::WorkingTimeRow>& rows) {
        table->setRowCount(static_cast<int>(rows.size()));
        for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
            const app::WorkingTimeRow& value = rows[static_cast<std::size_t>(row)];
            // Column 0 is the capture's unlabelled row number; the Name column
            // stays empty by design (core/cal decodes no exception names).
            table->setItem(row, 0, new QTableWidgetItem(QString::number(value.index)));
            table->setItem(row, 1, new QTableWidgetItem(QString()));
            table->setItem(row, 2,
                           new QTableWidgetItem(QString::fromStdString(value.start_text)));
            table->setItem(row, 3,
                           new QTableWidgetItem(QString::fromStdString(value.finish_text)));
        }
    };
    fill_table(exceptions_, model_.exceptions);
    fill_table(work_weeks_, model_.work_weeks);
    exceptions_->resizeColumnsToContents();
    work_weeks_->resizeColumnsToContents();
}

void ChangeWorkingTimeDialog::select_day(int day) {
    const std::string calendar = model_.selected_calendar;
    reload(calendar, year_, month_, day);
}

void ChangeWorkingTimeDialog::set_month(int year, int month) {
    const std::string calendar = model_.selected_calendar;
    reload(calendar, year, month, 1);
}

}  // namespace msqt
