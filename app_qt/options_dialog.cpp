// app_qt/options_dialog.cpp -- see options_dialog.h.

#include "options_dialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "shell_style.h"

namespace msqt {
namespace {

constexpr const char* kNotImplemented = "not implemented in this port";
constexpr int kDialogWidth = 860;
constexpr int kDialogHeight = 640;
// The audit capture's page list is 100 px of a 572-px dialog (17.48 %).
constexpr int kPageListWidth = 150;

QFont options_font(int point_size, bool bold = false) {
    QFont font;
    font.setPointSize(point_size);
    font.setBold(bold);
    return font;
}

// Every laid-out control carries optionControl=true, and either
// optionFunctional=true (the port really applies it) or the disabled state and
// the exact decision-9 tooltip.  The gate walks these properties on the live
// widgets.
template <typename T>
T* mark(T* control, const char* name, bool functional = false) {
    control->setObjectName(QString::fromUtf8(name));
    control->setProperty("optionControl", true);
    control->setProperty("optionFunctional", functional);
    if (!functional) {
        control->setEnabled(false);
        control->setToolTip(QString::fromUtf8(kNotImplemented));
    }
    return control;
}

QLabel* section_label(QWidget* parent, const QString& text) {
    auto* label = new QLabel(text, parent);
    label->setFont(options_font(kBodyPointSize, true));
    label->setStyleSheet(QStringLiteral("color: #202020;"));
    return label;
}

QLabel* header_label(QWidget* parent, const QString& text) {
    auto* label = new QLabel(text, parent);
    label->setFont(options_font(kBodyPointSize, false));
    label->setStyleSheet(QStringLiteral("color: #3a3a3a;"));
    label->setWordWrap(true);
    return label;
}

QHBoxLayout* row(QVBoxLayout* layout, QWidget* parent, const QString& label_text,
                 QWidget* control) {
    auto* row_layout = new QHBoxLayout();
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(8);
    auto* label = new QLabel(label_text, parent);
    label->setFont(options_font(kBodyPointSize));
    label->setFixedWidth(170);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    row_layout->addWidget(label);
    row_layout->addWidget(control, 1);
    row_layout->addStretch(1);
    layout->addLayout(row_layout);
    return row_layout;
}

QComboBox* make_combo(QWidget* parent, const QStringList& items, int current = 0) {
    auto* combo = new QComboBox(parent);
    combo->addItems(items);
    combo->setCurrentIndex(current);
    combo->setFixedHeight(24);
    combo->setMinimumWidth(260);
    return combo;
}

QLineEdit* make_line(QWidget* parent, const QString& text) {
    auto* line = new QLineEdit(text, parent);
    line->setFixedHeight(24);
    line->setMinimumWidth(260);
    return line;
}

QSpinBox* make_spin(QWidget* parent, int minimum, int maximum, int value) {
    auto* spin = new QSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setFixedWidth(90);
    return spin;
}

QCheckBox* make_check(QWidget* parent, const QString& text, bool checked = false) {
    auto* box = new QCheckBox(text, parent);
    box->setChecked(checked);
    return box;
}

QPushButton* make_button(QWidget* parent, const QString& text) {
    auto* button = new QPushButton(text, parent);
    button->setFixedHeight(24);
    return button;
}

QListWidget* make_list(QWidget* parent, const QStringList& items) {
    auto* list = new QListWidget(parent);
    list->addItems(items);
    list->setFixedHeight(110);
    return list;
}

}  // namespace

OptionsDialog::OptionsDialog(const app::Preferences& current, ThemeChoice theme_choice,
                             bool new_tasks_manual, const QString& project_name,
                             QWidget* parent)
    : QDialog(parent),
      preferences_(current),
      theme_choice_(theme_choice),
      new_tasks_manual_(new_tasks_manual),
      project_name_(project_name) {
    setWindowTitle(QStringLiteral("Project Options"));
    setFixedSize(kDialogWidth, kDialogHeight);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);
    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    root->addLayout(body, 1);

    page_list_ = new QListWidget(this);
    page_list_->setObjectName(QStringLiteral("optionsPageList"));
    page_list_->setFixedWidth(kPageListWidth);
    page_list_->setFont(options_font(kBodyPointSize));
    const QStringList titles = {
        QStringLiteral("General"),           QStringLiteral("Display"),
        QStringLiteral("Schedule"),          QStringLiteral("Proofing"),
        QStringLiteral("Save"),              QStringLiteral("Language"),
        QStringLiteral("Advanced"),          QStringLiteral("Customize Ribbon"),
        QStringLiteral("Quick Access Toolbar"), QStringLiteral("Add-ins"),
        QStringLiteral("Trust Center"),
    };
    page_list_->addItems(titles);
    body->addWidget(page_list_);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("optionsPages"));
    body->addWidget(stack_, 1);
    connect(page_list_, &QListWidget::currentRowChanged, this,
            [this](int row) { stack_->setCurrentIndex(row); });

    page_contents_.assign(static_cast<std::size_t>(titles.size()), nullptr);

    // The eleven pages, in the capture's order.
    build_general(make_page(0, QStringLiteral("General"),
                            QStringLiteral("General options for working with Project.")));
    build_display(make_page(1, QStringLiteral("Display"),
                            QStringLiteral("Change how Project content is displayed on the "
                                           "screen.")));
    build_schedule(make_page(2, QStringLiteral("Schedule"),
                             QStringLiteral("Change options related to scheduling, calendars, "
                                            "and calculations.")));
    build_proofing(make_page(3, QStringLiteral("Proofing"),
                             QStringLiteral("Change how Project corrects and formats your "
                                            "text.")));
    build_save(make_page(4, QStringLiteral("Save"),
                         QStringLiteral("Customize how projects are saved.")));
    build_language(make_page(5, QStringLiteral("Language"),
                             QStringLiteral("Office Language Preferences")));
    build_advanced(make_page(6, QStringLiteral("Advanced"),
                             QStringLiteral("Advanced options for working with Project.")));
    build_customize_ribbon(make_page(7, QStringLiteral("Customize Ribbon"),
                                     QStringLiteral("Customize the Ribbon.")));
    build_qat(make_page(8, QStringLiteral("Quick Access Toolbar"),
                        QStringLiteral("Customize the Quick Access Toolbar.")));
    build_addins(make_page(9, QStringLiteral("Add-ins"),
                           QStringLiteral("View and manage Microsoft Office Add-ins.")));
    build_trust_center(make_page(10, QStringLiteral("Trust Center"),
                                 QStringLiteral("Help keep your documents safe and your "
                                                "computer secure and healthy.")));
    page_list_->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

QWidget* OptionsDialog::make_page(int index, const QString& title,
                                  const QString& subtitle) {
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);
    auto* heading = new QLabel(title, content);
    heading->setFont(options_font(kBodyPointSize + 2, true));
    layout->addWidget(heading);
    layout->addWidget(header_label(content, subtitle));
    page_contents_[static_cast<std::size_t>(index)] = content;

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("optionsPage"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    stack_->addWidget(scroll);
    return content;
}

void OptionsDialog::build_general(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    layout->addWidget(section_label(content, QStringLiteral("User Interface options")));
    row(layout, content, QStringLiteral("ScreenTip style:"),
        mark(make_combo(content,
                        {QStringLiteral("Show feature descriptions in ScreenTips")}),
             "options.general.screentip"));

    layout->addWidget(section_label(content, QStringLiteral("Project view")));
    row(layout, content, QStringLiteral("Default view:"),
        mark(make_combo(content, {QStringLiteral("Gantt with Timeline")}),
             "options.general.default_view"));
    date_format_ = mark(make_combo(
                            content,
                            {QStringLiteral("Tue 20/10/26 (day/month/year)"),
                             QStringLiteral("Tue 10/20/26 (month/day/year)"),
                             QStringLiteral("2026-10-20 (ISO)")},
                            static_cast<int>(preferences_.date_style)),
                        "options.general.date_format", true);
    connect(date_format_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0 && index <= 2) {
            preferences_.date_style = static_cast<app::DateStyle>(index);
        }
    });
    row(layout, content, QStringLiteral("Date format:"), date_format_);

    layout->addWidget(section_label(
        content, QStringLiteral("Personalize your copy of Microsoft Office")));
    row(layout, content, QStringLiteral("User name:"),
        mark(make_line(content, QStringLiteral("Local user")),
             "options.general.user_name"));
    row(layout, content, QStringLiteral("Initials:"),
        mark(make_line(content, QStringLiteral("MS")), "options.general.initials"));
    layout->addWidget(mark(
        make_check(content,
                   QStringLiteral("Always use these values regardless of sign in to Office.")),
        "options.general.always_use"));
    row(layout, content, QStringLiteral("Office Background:"),
        mark(make_combo(content, {QStringLiteral("No Background")}),
             "options.general.office_background"));
    theme_combo_ = mark(make_combo(content, {}), "options.general.office_theme", true);
    for (const ThemeChoiceSpec& spec : office_theme_choices()) {
        theme_combo_->addItem(QString::fromUtf8(spec.name));
        if (!spec.implemented) {
            theme_combo_->setItemData(theme_combo_->count() - 1,
                                      QString::fromUtf8(kNotImplemented),
                                      Qt::ToolTipRole);
            auto* model = qobject_cast<QStandardItemModel*>(theme_combo_->model());
            if (model != nullptr) {
                if (QStandardItem* item = model->item(theme_combo_->count() - 1)) {
                    item->setEnabled(false);
                }
            }
        }
    }
    theme_combo_->setCurrentIndex(static_cast<int>(theme_choice_));
    connect(theme_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0 || index >= static_cast<int>(office_theme_choices().size())) {
            return;
        }
        const ThemeChoice choice = static_cast<ThemeChoice>(index);
        if (theme_choice_implemented(choice)) {
            theme_choice_ = choice;
        }
    });
    row(layout, content, QStringLiteral("Office Theme:"), theme_combo_);

    layout->addWidget(section_label(content, QStringLiteral("Start up options")));
    layout->addWidget(mark(
        make_check(content,
                   QStringLiteral("Show the Start screen when this application starts.")),
        "options.general.start_screen"));
    layout->addStretch(1);
}

void OptionsDialog::build_display(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    layout->addWidget(section_label(content, QStringLiteral("Calendar")));
    row(layout, content, QStringLiteral("Calendar Type:"),
        mark(make_combo(content, {QStringLiteral("Gregorian Calendar")}),
             "options.display.calendar_type"));
    row(layout, content, QStringLiteral("Currency options for this project:"),
        mark(make_combo(content, {project_name_}),
             "options.display.currency_project"));
    row(layout, content, QStringLiteral("Symbol:"),
        mark(make_line(content, QStringLiteral("$")), "options.display.symbol"));
    row(layout, content, QStringLiteral("Decimal digits:"),
        mark(make_spin(content, 0, 9, 2), "options.display.decimals"));
    row(layout, content, QStringLiteral("Placement:"),
        mark(make_combo(content, {QStringLiteral("$1")}), "options.display.placement"));
    row(layout, content, QStringLiteral("Currency:"),
        mark(make_combo(content, {QStringLiteral("AUD")}), "options.display.currency"));

    layout->addWidget(section_label(
        content, QStringLiteral("Show indicators and options buttons for:")));
    layout->addWidget(mark(make_check(content, QStringLiteral("Resource assignments"), true),
                           "options.display.resource_assignments"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Edits to work, units, or duration"), true),
        "options.display.edits_work"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Edits to start and finish dates"), true),
        "options.display.edits_dates"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Deletions in the Name column"), true),
        "options.display.deletions"));
    layout->addWidget(section_label(content, QStringLiteral("Show these elements:")));
    layout->addWidget(mark(make_check(content, QStringLiteral("Entry bar")),
                           "options.display.entry_bar"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Online status next to name"), true),
        "options.display.online_status"));
    layout->addStretch(1);
}

void OptionsDialog::build_schedule(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    layout->addWidget(
        section_label(content, QStringLiteral("Calendar options for this project:")));
    row(layout, content, QString(),
        mark(make_combo(content, {project_name_}),
             "options.schedule.calendar_project"));
    row(layout, content, QStringLiteral("Week starts on:"),
        mark(make_combo(content, {QStringLiteral("Monday")}),
             "options.schedule.week_starts"));
    row(layout, content, QStringLiteral("Fiscal year starts in:"),
        mark(make_combo(content, {QStringLiteral("January")}),
             "options.schedule.fiscal_year"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Use starting year for FY numbering")),
        "options.schedule.fy_numbering"));
    row(layout, content, QStringLiteral("Default start time:"),
        mark(new QTimeEdit(QTime(8, 0), content), "options.schedule.start_time"));
    row(layout, content, QStringLiteral("Default end time:"),
        mark(new QTimeEdit(QTime(17, 0), content), "options.schedule.end_time"));
    row(layout, content, QStringLiteral("Hours per day:"),
        mark(make_spin(content, 1, 24, 8), "options.schedule.hours_day"));
    row(layout, content, QStringLiteral("Hours per week:"),
        mark(make_spin(content, 1, 168, 40), "options.schedule.hours_week"));
    row(layout, content, QStringLiteral("Days per month:"),
        mark(make_spin(content, 1, 31, 20), "options.schedule.days_month"));

    layout->addWidget(section_label(content, QStringLiteral("Schedule")));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Show scheduling messages"), true),
        "options.schedule.show_messages"));
    row(layout, content, QStringLiteral("Show assignment units as a:"),
        mark(make_combo(content, {QStringLiteral("Percentage")}),
             "options.schedule.assignment_units"));

    layout->addWidget(section_label(
        content, QStringLiteral("Scheduling options for this project:")));
    row(layout, content, QString(),
        mark(make_combo(content, {project_name_}),
             "options.schedule.scheduling_project"));
    new_tasks_ = mark(
        make_combo(content, {QStringLiteral("Auto Scheduled"),
                             QStringLiteral("Manually Scheduled")},
                   new_tasks_manual_ ? 1 : 0),
        "options.schedule.new_tasks", true);
    connect(new_tasks_, &QComboBox::currentIndexChanged, this, [this](int index) {
        new_tasks_manual_ = index == 1;
    });
    row(layout, content, QStringLiteral("New tasks created:"), new_tasks_);
    row(layout, content, QStringLiteral("Auto scheduled tasks scheduled on:"),
        mark(make_combo(content, {QStringLiteral("Project Start Date")}),
             "options.schedule.auto_scheduled_on"));
    row(layout, content, QStringLiteral("Duration is entered in:"),
        mark(make_combo(content, {QStringLiteral("Days")}),
             "options.schedule.duration_unit"));
    row(layout, content, QStringLiteral("Work is entered in:"),
        mark(make_combo(content, {QStringLiteral("Hours")}),
             "options.schedule.work_unit"));
    row(layout, content, QStringLiteral("Default task type:"),
        mark(make_combo(content, {QStringLiteral("Fixed Duration")}),
             "options.schedule.default_type"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("New tasks are effort driven"), true),
        "options.schedule.effort_driven"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Autolink inserted or moved tasks"), true),
        "options.schedule.autolink"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Tasks will always honor their constraint dates"),
                   true),
        "options.schedule.honor_constraints"));
    layout->addWidget(mark(
        make_check(
            content,
            QStringLiteral("Show that scheduled tasks have estimated durations"), true),
        "options.schedule.estimated_durations"));
    layout->addStretch(1);
}

void OptionsDialog::build_proofing(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    layout->addWidget(section_label(content, QStringLiteral("AutoCorrect options")));
    layout->addWidget(mark(make_button(content, QStringLiteral("AutoCorrect Options...")),
                           "options.proofing.autocorrect"));
    layout->addWidget(section_label(
        content, QStringLiteral("When correcting spelling in Microsoft Office programs")));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Ignore words in UPPERCASE"), true),
        "options.proofing.ignore_upper"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Ignore words that contain numbers"), true),
        "options.proofing.ignore_numbers"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Ignore Internet and file addresses"), true),
        "options.proofing.ignore_addresses"));
    layout->addWidget(mark(make_check(content, QStringLiteral("Flag repeated words"), true),
                           "options.proofing.flag_repeated"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Enforce accented uppercase in French")),
        "options.proofing.french_uppercase"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Suggest from main dictionary only")),
        "options.proofing.main_dictionary"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Custom Dictionaries...")),
                           "options.proofing.dictionaries"));
    row(layout, content, QStringLiteral("French modes:"),
        mark(make_combo(content, {QStringLiteral("Traditional and new spellings")}),
             "options.proofing.french_modes"));
    row(layout, content, QStringLiteral("Spanish modes:"),
        mark(make_combo(content, {QStringLiteral("Tuteo verb forms only")}),
             "options.proofing.spanish_modes"));
    row(layout, content, QStringLiteral("Dictionary language:"),
        mark(make_combo(content, {QStringLiteral("English (Australia)")}),
             "options.proofing.dictionary_language"));
    layout->addWidget(section_label(
        content, QStringLiteral("When correcting spelling in Project")));
    layout->addWidget(header_label(content, QStringLiteral("Fields to check")));
    const std::vector<std::pair<const char*, const char*>> fields = {
        {"Name-Task", "options.proofing.name_task"},
        {"Notes-Task", "options.proofing.notes_task"},
        {"Text1-Task", "options.proofing.text1_task"},
        {"Text2-Task", "options.proofing.text2_task"},
        {"Text3-Task", "options.proofing.text3_task"},
        {"Text4-Task", "options.proofing.text4_task"},
    };
    for (const auto& field : fields) {
        layout->addWidget(mark(
            make_check(content, QString::fromUtf8(field.first), true), field.second));
    }
    layout->addStretch(1);
}

void OptionsDialog::build_save(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    layout->addWidget(section_label(content, QStringLiteral("Save projects")));
    row(layout, content, QStringLiteral("Save files in this format:"),
        mark(make_combo(content, {QStringLiteral("Project (*.mpp)")}),
             "options.save.format"));
    row(layout, content, QStringLiteral("Default File location:"),
        mark(make_line(content, QString()), "options.save.default_location"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Browse...")),
                           "options.save.browse_location"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Auto save every")),
        "options.save.auto_save"));
    row(layout, content, QStringLiteral("minutes"),
        mark(make_spin(content, 1, 120, 10), "options.save.auto_minutes"));
    layout->addWidget(mark(
        new QRadioButton(QStringLiteral("Save active project only"), content),
        "options.save.active_only"));
    layout->addWidget(mark(
        new QRadioButton(QStringLiteral("Save all open projects"), content),
        "options.save.all_open"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Prompt before saving"), true),
        "options.save.prompt"));
    layout->addWidget(mark(
        make_check(content,
                   QStringLiteral("Don't show the Backstage when opening or saving files "
                                  "with keyboard shortcuts")),
        "options.save.no_backstage"));
    layout->addWidget(mark(
        make_check(content,
                   QStringLiteral("Show additional places for saving, even if sign-in may "
                                  "be required")),
        "options.save.additional_places"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Save to Computer by default")),
        "options.save.to_computer"));
    row(layout, content,
        QStringLiteral("Default personal templates location:"),
        mark(make_line(content, QString()), "options.save.templates_location"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Browse...")),
                           "options.save.browse_templates"));
    layout->addWidget(section_label(content, QStringLiteral("Cache")));
    row(layout, content, QStringLiteral("Cache size limit (MB):"),
        mark(make_spin(content, 1, 99999, 50), "options.save.cache_size"));
    row(layout, content, QStringLiteral("Cache location:"),
        mark(make_line(content, QString()), "options.save.cache_location"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Browse...")),
                           "options.save.browse_cache"));
    layout->addWidget(mark(make_button(content, QStringLiteral("View Cache Status")),
                           "options.save.view_cache"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Clean Up Cache...")),
                           "options.save.clean_cache"));
    layout->addStretch(1);
}

void OptionsDialog::build_language(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    layout->addWidget(
        section_label(content, QStringLiteral("Office display language")));
    layout->addWidget(mark(
        make_list(content, {QStringLiteral("1. Match Microsoft Windows [English] "
                                           "<preferred>"),
                            QStringLiteral("2. English")}),
        "options.language.display_list"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Add a Language...")),
                           "options.language.add_display"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Move Up")),
                           "options.language.move_up"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Move Down")),
                           "options.language.move_down"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Set as Preferred")),
                           "options.language.set_preferred_display"));
    layout->addWidget(section_label(
        content, QStringLiteral("Office authoring languages and proofing")));
    layout->addWidget(mark(
        make_list(content, {QStringLiteral("English (Australia) <preferred>  Proofing "
                                           "installed"),
                            QStringLiteral("English (United States)  Proofing installed")}),
        "options.language.authoring_list"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Add a Language...")),
                           "options.language.add_authoring"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Remove")),
                           "options.language.remove"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Set as Preferred")),
                           "options.language.set_preferred_authoring"));
    layout->addWidget(mark(
        make_check(content,
                   QStringLiteral("Store my authoring languages in the cloud for my "
                                  "account.")),
        "options.language.store_cloud"));
    layout->addStretch(1);
}

void OptionsDialog::build_advanced(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    layout->addWidget(section_label(content, QStringLiteral("General")));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Set AutoFilter on for new projects"), true),
        "options.advanced.autofilter"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Prompt for project info for new projects")),
        "options.advanced.prompt_info"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Show add-in user interface errors")),
        "options.advanced.addin_errors"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Open last file on startup")),
        "options.advanced.open_last"));
    row(layout, content, QStringLiteral("Undo levels:"),
        mark(make_spin(content, 0, 99, 20), "options.advanced.undo_levels"));
    layout->addWidget(section_label(content, QStringLiteral("Project Web App")));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Allow team members to reassign tasks")),
        "options.advanced.reassign"));
    layout->addWidget(section_label(content, QStringLiteral("Planning Wizard")));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Advice from Planning Wizard"), true),
        "options.advanced.advice_wizard"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Advice about using Project"), true),
        "options.advanced.advice_using"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Advice about scheduling"), true),
        "options.advanced.advice_scheduling"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Advice about errors"), true),
        "options.advanced.advice_errors"));
    row(layout, content, QStringLiteral("General options for this project:"),
        mark(make_combo(content, {project_name_}), "options.advanced.project_options"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Automatically add new resources and tasks"),
                   true),
        "options.advanced.auto_add"));
    row(layout, content, QStringLiteral("Default standard rate:"),
        mark(make_line(content, QStringLiteral("$0.00/h")),
             "options.advanced.standard_rate"));
    row(layout, content, QStringLiteral("Default overtime rate:"),
        mark(make_line(content, QStringLiteral("$0.00/h")),
             "options.advanced.overtime_rate"));
    layout->addWidget(section_label(content, QStringLiteral("Edit")));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Allow cell drag and drop"), true),
        "options.advanced.drag_drop"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Ask to update automatic links"), true),
        "options.advanced.update_links"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Move selection after enter"), true),
        "options.advanced.move_after_enter"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Edit directly in cell"), true),
        "options.advanced.edit_in_cell"));
    layout->addStretch(1);
}

void OptionsDialog::build_customize_ribbon(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    row(layout, content, QStringLiteral("Choose commands from:"),
        mark(make_combo(content, {QStringLiteral("Popular Commands")}),
             "options.ribbon.commands_from"));
    layout->addWidget(mark(
        make_list(content,
                  {QStringLiteral("100% Complete"), QStringLiteral("Add Existing Tasks to "
                                                                  "Timeline"),
                   QStringLiteral("Assign Resources..."), QStringLiteral("Auto Schedule"),
                   QStringLiteral("Background Color"), QStringLiteral("Copy"),
                   QStringLiteral("Copy Picture"), QStringLiteral("Custom Fields"),
                   QStringLiteral("Cut"), QStringLiteral("Email")}),
        "options.ribbon.command_list"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Add >>")),
                           "options.ribbon.add"));
    layout->addWidget(mark(make_button(content, QStringLiteral("<< Remove")),
                           "options.ribbon.remove"));
    row(layout, content, QStringLiteral("Customize the Ribbon:"),
        mark(make_combo(content, {QStringLiteral("Main Tabs")}),
             "options.ribbon.tabs"));
    layout->addWidget(mark(
        make_list(content,
                  {QStringLiteral("Main Tabs"), QStringLiteral("Compare Projects"),
                   QStringLiteral("Task"), QStringLiteral("Resource"),
                   QStringLiteral("Report"), QStringLiteral("Project"),
                   QStringLiteral("View"), QStringLiteral("Add-ins"),
                   QStringLiteral("Developer"), QStringLiteral("Help")}),
        "options.ribbon.tree"));
    layout->addWidget(mark(make_button(content, QStringLiteral("New Tab")),
                           "options.ribbon.new_tab"));
    layout->addWidget(mark(make_button(content, QStringLiteral("New Group")),
                           "options.ribbon.new_group"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Rename...")),
                           "options.ribbon.rename"));
    row(layout, content, QStringLiteral("Customizations:"),
        mark(make_combo(content, {QStringLiteral("Reset")}),
             "options.ribbon.reset"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Import/Export...")),
                           "options.ribbon.import_export"));
    layout->addStretch(1);
}

void OptionsDialog::build_qat(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    row(layout, content, QStringLiteral("Choose commands from:"),
        mark(make_combo(content, {QStringLiteral("Popular Commands")}),
             "options.qat.commands_from"));
    layout->addWidget(mark(
        make_list(content,
                  {QStringLiteral("<Separator>"), QStringLiteral("100% Complete"),
                   QStringLiteral("Add Existing Tasks to Timeline"),
                   QStringLiteral("Assign Resources..."), QStringLiteral("Auto Schedule"),
                   QStringLiteral("Background Color"), QStringLiteral("Copy"),
                   QStringLiteral("Copy Picture"), QStringLiteral("Custom Fields"),
                   QStringLiteral("Cut"), QStringLiteral("Email")}),
        "options.qat.command_list"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Add >>")),
                           "options.qat.add"));
    layout->addWidget(mark(make_button(content, QStringLiteral("<< Remove")),
                           "options.qat.remove"));
    row(layout, content, QStringLiteral("Customize Quick Access Toolbar:"),
        mark(make_combo(content, {QStringLiteral("For all documents (default)")}),
             "options.qat.target"));
    layout->addWidget(mark(
        make_list(content, {QStringLiteral("<Separator>"), QStringLiteral("100% Complete"),
                            QStringLiteral("Undo"), QStringLiteral("Redo")}),
        "options.qat.list"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Modify...")),
                           "options.qat.modify"));
    row(layout, content, QStringLiteral("Customizations:"),
        mark(make_combo(content, {QStringLiteral("Reset")}), "options.qat.reset"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Import/Export...")),
                           "options.qat.import_export"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Show Quick Access Toolbar"), true),
        "options.qat.show"));
    row(layout, content, QStringLiteral("Toolbar Position:"),
        mark(make_combo(content, {QStringLiteral("Above Ribbon")}),
             "options.qat.position"));
    layout->addWidget(mark(
        make_check(content, QStringLiteral("Always show command labels")),
        "options.qat.labels"));
    layout->addStretch(1);
}

void OptionsDialog::build_addins(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    layout->addWidget(section_label(content, QStringLiteral("Add-ins")));
    auto* table = new QTableWidget(0, 3, content);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Location"), QStringLiteral("Type")});
    const QStringList rows = {
        QStringLiteral("Active Application Add-ins"),
        QStringLiteral("Inactive Application Add-ins"),
        QStringLiteral("Document Related Add-ins"),
        QStringLiteral("Disabled Application Add-ins"),
    };
    table->setRowCount(rows.size());
    for (int row_index = 0; row_index < rows.size(); ++row_index) {
        table->setItem(row_index, 0, new QTableWidgetItem(rows[row_index]));
        table->setItem(row_index, 1, new QTableWidgetItem(QStringLiteral("No add-ins")));
    }
    table->setFixedHeight(180);
    layout->addWidget(mark(table, "options.addins.table"));
    row(layout, content, QStringLiteral("Manage:"),
        mark(make_combo(content, {QStringLiteral("COM Add-ins")}),
             "options.addins.manage"));
    layout->addWidget(mark(make_button(content, QStringLiteral("Go...")),
                           "options.addins.go"));
    layout->addStretch(1);
}

void OptionsDialog::build_trust_center(QWidget* content) {
    auto* layout = static_cast<QVBoxLayout*>(content->layout());
    layout->addWidget(section_label(content, QStringLiteral("Security & more")));
    layout->addWidget(header_label(
        content, QStringLiteral("Visit Office.com to learn more about protecting your "
                                "privacy and security. Microsoft Trust Center")));
    layout->addWidget(section_label(content, QStringLiteral("Microsoft Project Trust Center")));
    layout->addWidget(header_label(
        content, QStringLiteral("The Trust Center contains security and privacy settings. "
                                "These settings help keep your computer secure. We "
                                "recommend that you do not change these settings.")));
    layout->addWidget(mark(make_button(content, QStringLiteral("Trust Center Settings...")),
                           "options.trust.settings"));
    layout->addStretch(1);
}

int OptionsDialog::page_count() const {
    return static_cast<int>(page_contents_.size());
}

QString OptionsDialog::page_title(int index) const {
    if (index < 0 || index >= page_count() || page_list_ == nullptr) {
        return QString();
    }
    QListWidgetItem* item = page_list_->item(index);
    return item != nullptr ? item->text() : QString();
}

QWidget* OptionsDialog::page_widget(int index) const {
    if (index < 0 || index >= page_count()) {
        return nullptr;
    }
    return page_contents_[static_cast<std::size_t>(index)];
}

ThemeChoice OptionsDialog::theme_choice() const { return theme_choice_; }

bool OptionsDialog::new_tasks_manual() const { return new_tasks_manual_; }

}  // namespace msqt
