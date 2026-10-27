// app_qt/dialogs.cpp -- see dialogs.h.

#include "dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QFont>
#include <QSpinBox>
#include <QString>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "field_link_edits.h"
#include "field_value.h"
#include "filter_model.h"

namespace msqt {
namespace {

using dialog_geometry::kButtonHeight;
using dialog_geometry::kCheckHeight;
using dialog_geometry::kControlHeight;
using dialog_geometry::kFieldWidth;
using dialog_geometry::kLabelWidth;
using dialog_geometry::kMargin;
using dialog_geometry::kSpacing;

QString to_q(const std::string& text) { return QString::fromStdString(text); }

QVBoxLayout* make_outer(QDialog& dialog, const dialog_geometry::DialogSize size) {
    dialog.setFixedSize(size.width, size.height);
    auto* outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    outer->setSpacing(kSpacing);
    return outer;
}

QFormLayout* make_form() {
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(kSpacing);
    form->setVerticalSpacing(kSpacing);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    return form;
}

QLabel* make_form_label(QWidget* parent, const QString& text) {
    auto* label = new QLabel(text, parent);
    label->setFixedWidth(kLabelWidth);
    label->setFixedHeight(kControlHeight);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

QComboBox* make_combo(QWidget* parent) {
    auto* combo = new QComboBox(parent);
    combo->setFixedSize(kFieldWidth, kControlHeight);
    return combo;
}

QLineEdit* make_line_edit(QWidget* parent) {
    auto* line = new QLineEdit(parent);
    line->setFixedSize(kFieldWidth, kControlHeight);
    return line;
}

QSpinBox* make_spin_box(QWidget* parent) {
    auto* spin = new QSpinBox(parent);
    spin->setFixedSize(kFieldWidth, kControlHeight);
    return spin;
}

QPushButton* make_button(QWidget* parent, const QString& text) {
    auto* button = new QPushButton(text, parent);
    button->setFixedHeight(kButtonHeight);
    return button;
}

QDialogButtonBox* make_button_box(QDialog& dialog) {
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return buttons;
}

// A wrapped explanatory line, widened to the dialog's content width so its
// measured geometry is the dialog's, not its text's.
QLabel* make_hint(QWidget* parent, const dialog_geometry::DialogSize size,
                  const QString& text) {
    auto* hint = new QLabel(text, parent);
    hint->setWordWrap(true);
    hint->setFixedWidth(size.width - 2 * kMargin);
    hint->setTextFormat(Qt::PlainText);
    return hint;
}

QString formatted_field_id(std::uint32_t field) {
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(field), 8, 16, QChar('0'));
}

// A one-line description of a predecessor list row: "3  Design  [FS]  lag 2d".
QString predecessor_row_text(const app::PredecessorListItem& item) {
    QString text = to_q(item.predecessor_id) + QStringLiteral("  ") +
                   to_q(item.predecessor_name) + QStringLiteral("  [") +
                   QString::fromLatin1(item.type_name) + QStringLiteral("]");
    if (!item.lag_text.empty() && item.lag_text != "0") {
        text += QStringLiteral("  lag ") + to_q(item.lag_text);
    }
    if (item.pending) {
        text += QStringLiteral("  (pending)");
    }
    return text;
}

}  // namespace

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

PreferencesDialog::PreferencesDialog(const app::Preferences& current, Theme current_theme,
                                     QWidget* parent)
    : QDialog(parent), original_(current), original_theme_(current_theme) {
    setWindowTitle(QStringLiteral("Preferences"));
    auto* outer = make_outer(*this, dialog_geometry::kPreferencesSize);

    auto* form = make_form();
    form->setObjectName(QStringLiteral("preferencesForm"));
    date_style_ = make_combo(this);
    date_style_->addItem(QStringLiteral("Tue 20/10/26 (day/month/year)"));
    date_style_->addItem(QStringLiteral("Tue 10/20/26 (month/day/year)"));
    date_style_->addItem(QStringLiteral("2026-10-20 (ISO)"));
    date_style_->setCurrentIndex(static_cast<int>(current.date_style));
    form->addRow(make_form_label(this, QStringLiteral("Date format")), date_style_);

    duration_style_ = make_combo(this);
    duration_style_->addItem(QStringLiteral("Days and hours (3 days, 4 hrs)"));
    duration_style_->addItem(QStringLiteral("Hours only (24 hrs)"));
    duration_style_->addItem(QStringLiteral("Days only (0.5 days)"));
    duration_style_->setCurrentIndex(static_cast<int>(current.duration_style));
    form->addRow(make_form_label(this, QStringLiteral("Duration format")), duration_style_);

    timescale_ = make_combo(this);
    timescale_->addItem(QStringLiteral("Days"));
    timescale_->addItem(QStringLiteral("Weeks"));
    timescale_->addItem(QStringLiteral("Months"));
    timescale_->setCurrentIndex(static_cast<int>(current.zoom));
    form->addRow(make_form_label(this, QStringLiteral("Timescale")), timescale_);

    theme_combo_ = make_combo(this);
    // W26-6.2: Black is a third resolved palette now, so the compact chooser
    // offers it too (the Account page keeps Project's five measured names).
    theme_combo_->addItem(QStringLiteral("Office Light"));
    theme_combo_->addItem(QStringLiteral("Office Dark"));
    theme_combo_->addItem(QStringLiteral("Office Black"));
    theme_combo_->setCurrentIndex(current_theme == Theme::black
                                      ? 2
                                      : (current_theme == Theme::dark ? 1 : 0));
    form->addRow(make_form_label(this, QStringLiteral("Office theme")), theme_combo_);

    // The shading choice is the form's fifth row, not a detached widget: the
    // per-screen gate measures every control in the same 120/280 grid, and a
    // checkbox is the one field whose pinned height is kCheckHeight.
    shading_ = new QCheckBox(QStringLiteral("Shade non-working time in the Gantt"), this);
    shading_->setChecked(current.working_day_shading);
    shading_->setFixedHeight(kCheckHeight);
    form->addRow(make_form_label(this, QStringLiteral("Non-working time")), shading_);
    outer->addLayout(form);

    outer->addWidget(make_hint(
        this, dialog_geometry::kPreferencesSize,
        QStringLiteral("Stored in this app's preferences. MS Project exposes no global "
                       "date/duration/timescale setter, so nothing here is written to a "
                       ".mpp.")));
    outer->addStretch(1);
    outer->addWidget(make_button_box(*this));
}

app::Preferences PreferencesDialog::preferences() const {
    app::Preferences preferences = original_;
    const int date = date_style_->currentIndex();
    if (date >= 0 && date <= 2) {
        preferences.date_style = static_cast<app::DateStyle>(date);
    }
    const int duration = duration_style_->currentIndex();
    if (duration >= 0 && duration <= 2) {
        preferences.duration_style = static_cast<app::DurationStyle>(duration);
    }
    const int zoom = timescale_->currentIndex();
    if (zoom >= 0 && zoom <= 2) {
        preferences.zoom = static_cast<app::ZoomLevel>(zoom);
    }
    preferences.working_day_shading = shading_->isChecked();
    return preferences;
}

Theme PreferencesDialog::theme() const {
    if (theme_combo_ == nullptr) {
        return original_theme_;
    }
    switch (theme_combo_->currentIndex()) {
        case 1:
            return Theme::dark;
        case 2:
            return Theme::black;
        default:
            return Theme::light;
    }
}

bool edit_preferences(QWidget* parent, app::Preferences* prefs) {
    if (prefs == nullptr) {
        return false;
    }
    PreferencesDialog dialog(*prefs, Theme::light, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    *prefs = dialog.preferences();
    return true;
}

// ---------------------------------------------------------------------------
// Table / Date Format
// ---------------------------------------------------------------------------

TableDateFormatDialog::TableDateFormatDialog(const app::ViewStateEdits& current,
                                             QWidget* parent)
    : QDialog(parent), current_(current) {
    setWindowTitle(QStringLiteral("Table / Date Format"));
    auto* outer = make_outer(*this, dialog_geometry::kTableDateFormatSize);

    auto* form = make_form();
    form->setObjectName(QStringLiteral("tableFormatForm"));

    date_format_ = make_combo(this);
    // The two formats main.mm exposes; 5 is the fixture's measured byte.
    date_format_->addItem(QStringLiteral("4 - mmm dd hh:mm AM"), QVariant(4));
    date_format_->addItem(QStringLiteral("5 - mmm dd 'yy (measured)"), QVariant(5));
    int date_index = 1;
    if (current.set_date_format) {
        date_index = current.date_format == 4 ? 0 : 1;
    }
    date_format_->setCurrentIndex(date_index);
    form->addRow(make_form_label(this, QStringLiteral("Date format")), date_format_);

    replace_column_ = new QCheckBox(QStringLiteral("Replace an existing table column"), this);
    replace_column_->setFixedHeight(kCheckHeight);
    form->addRow(make_form_label(this, QStringLiteral("Column edit")), replace_column_);

    column_index_ = make_spin_box(this);
    column_index_->setRange(0, 63);
    column_index_->setValue(static_cast<int>(current.column_index));
    form->addRow(make_form_label(this, QStringLiteral("Column index")), column_index_);

    field_id_ = make_line_edit(this);
    field_id_->setPlaceholderText(QStringLiteral("e.g. 0x0b400033"));
    field_id_->setText(formatted_field_id(current.field_id));
    form->addRow(make_form_label(this, QStringLiteral("Field id")), field_id_);

    title_ = make_line_edit(this);
    title_->setPlaceholderText(QStringLiteral("new column caption"));
    title_->setMaxLength(51);
    title_->setText(to_q(current.title));
    form->addRow(make_form_label(this, QStringLiteral("Title")), title_);
    outer->addLayout(form);

    outer->addWidget(make_hint(
        this, dialog_geometry::kTableDateFormatSize,
        QStringLiteral("Queued as a 214/CTable view-state edit. File > Save writes it in the "
                       "same container write as the task start dates.")));
    outer->addStretch(1);
    outer->addWidget(make_button_box(*this));

    replace_column_->setChecked(current.replace_column);
    update_enabled();
    QObject::connect(replace_column_, &QCheckBox::toggled, this,
                     [this](bool) { update_enabled(); });
}

void TableDateFormatDialog::update_enabled() {
    const bool enabled = replace_column_->isChecked();
    column_index_->setEnabled(enabled);
    field_id_->setEnabled(enabled);
    title_->setEnabled(enabled);
}

void TableDateFormatDialog::accept() {
    app::ViewStateEdits edits = current_;
    edits.set_date_format = true;
    edits.date_format = static_cast<std::uint8_t>(date_format_->currentData().toInt());
    edits.table_index = 0;
    edits.replace_column = false;
    if (replace_column_->isChecked()) {
        // Accept the writer's "0x0b400033" form and a plain decimal id.  (Qt's
        // QString::toUInt base argument is not relied on for the 0x prefix.)
        bool field_ok = false;
        const QString field_text = field_id_->text().trimmed();
        const std::uint32_t field =
            field_text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                ? field_text.mid(2).toUInt(&field_ok, 16)
                : field_text.toUInt(&field_ok, 10);
        if (!field_ok || field == 0) {
            QMessageBox::warning(this, QStringLiteral("Table / Date Format"),
                                 QStringLiteral("Enter the replacement field as a number, for "
                                                "example 0x0b400033."));
            return;
        }
        const QString title = title_->text();
        if (title.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Table / Date Format"),
                                 QStringLiteral("A replacement column needs a caption."));
            return;
        }
        edits.replace_column = true;
        edits.column_index = static_cast<std::uint32_t>(column_index_->value());
        edits.field_id = field;
        edits.title = title.toStdString();
    }
    edits_ = edits;
    ok_ = true;
    QDialog::accept();
}

bool edit_table_date_format(QWidget* parent, const app::ViewStateEdits& current,
                            app::ViewStateEdits* out) {
    if (out == nullptr) {
        return false;
    }
    TableDateFormatDialog dialog(current, parent);
    if (dialog.exec() != QDialog::Accepted || !dialog.ok()) {
        return false;
    }
    *out = dialog.edits();
    return true;
}

// ---------------------------------------------------------------------------
// Custom Field Formula
// ---------------------------------------------------------------------------

CustomFormulaDialog::CustomFormulaDialog(const app::CustomFormulaEdit& current,
                                         QWidget* parent)
    : QDialog(parent), result_(current) {
    setWindowTitle(QStringLiteral("Custom Field Formula"));
    auto* outer = make_outer(*this, dialog_geometry::kCustomFormulaSize);

    auto* form = make_form();
    form->setObjectName(QStringLiteral("formulaForm"));
    field_ = make_combo(this);
    struct FieldChoice {
        const char* title;
        mpp::FieldKind kind;
        unsigned index;
    };
    const FieldChoice choices[] = {
        {"Text1", mpp::FieldKind::text, 1},       {"Text2", mpp::FieldKind::text, 2},
        {"Number1", mpp::FieldKind::number, 1},   {"Number2", mpp::FieldKind::number, 2},
        {"Duration1", mpp::FieldKind::duration, 1}, {"Date1", mpp::FieldKind::date, 1},
        {"Flag1", mpp::FieldKind::flag, 1},
    };
    int selected = 0;
    for (const FieldChoice& choice : choices) {
        const std::uint32_t field = mpp::custom_field_id(choice.kind, choice.index);
        field_->addItem(QString::fromLatin1(choice.title), QVariant(static_cast<uint>(field)));
        if (current.operation == app::CustomFormulaEdit::Operation::set &&
            current.field_id == field) {
            selected = field_->count() - 1;
        }
    }
    field_->setCurrentIndex(selected);
    form->addRow(make_form_label(this, QStringLiteral("Field")), field_);

    formula_ = make_line_edit(this);
    formula_->setPlaceholderText(QStringLiteral("e.g. [Duration]/240"));
    if (current.operation == app::CustomFormulaEdit::Operation::set) {
        formula_->setText(to_q(current.formula));
    }
    form->addRow(make_form_label(this, QStringLiteral("Formula")), formula_);
    outer->addLayout(form);

    outer->addWidget(make_hint(
        this, dialog_geometry::kCustomFormulaSize,
        QStringLiteral("Sets or clears a formula stored in the project's 114/Props stream. It "
                       "is written when you save. An empty formula is refused.")));
    outer->addStretch(1);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(kSpacing);
    buttons->addStretch(1);
    auto* set_button = make_button(this, QStringLiteral("Set Formula"));
    clear_ = make_button(this, QStringLiteral("Clear Formulas"));
    auto* cancel_button = make_button(this, QStringLiteral("Cancel"));
    buttons->addWidget(set_button);
    buttons->addWidget(clear_);
    buttons->addWidget(cancel_button);
    outer->addLayout(buttons);

    QObject::connect(set_button, &QPushButton::clicked, this, [this](bool) { accept(); });
    QObject::connect(clear_, &QPushButton::clicked, this, [this](bool) {
        result_ = app::CustomFormulaEdit{};
        result_.operation = app::CustomFormulaEdit::Operation::clear;
        accepted_ = true;
        QDialog::accept();
    });
    QObject::connect(cancel_button, &QPushButton::clicked, this, &QDialog::reject);
}

void CustomFormulaDialog::accept() {
    const QString formula = formula_->text();
    if (formula.trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Custom Field Formula"),
                             QStringLiteral("An empty formula is refused. Use Clear Formulas "
                                            "to remove every stored formula."));
        return;
    }
    const uint field = field_->currentData().toUInt();
    if (field == 0) {
        QMessageBox::warning(this, QStringLiteral("Custom Field Formula"),
                             QStringLiteral("Choose a field."));
        return;
    }
    result_ = app::CustomFormulaEdit{};
    result_.operation = app::CustomFormulaEdit::Operation::set;
    result_.field_id = field;
    result_.formula = formula.toStdString();
    accepted_ = true;
    QDialog::accept();
}

bool edit_custom_formula(QWidget* parent, const app::CustomFormulaEdit& current,
                         app::CustomFormulaEdit* out) {
    if (out == nullptr) {
        return false;
    }
    CustomFormulaDialog dialog(current, parent);
    if (dialog.exec() != QDialog::Accepted || !dialog.accepted_result()) {
        return false;
    }
    *out = dialog.result();
    return true;
}

// ---------------------------------------------------------------------------
// Task Fields
// ---------------------------------------------------------------------------

TaskFieldsDialog::TaskFieldsDialog(const app::TaskRow& row,
                                   const core::sheet::SheetFormat& format, QWidget* parent)
    : QDialog(parent), row_(row), format_(format) {
    setWindowTitle(QStringLiteral("Task Fields"));
    auto* outer = make_outer(*this, dialog_geometry::kTaskFieldsSize);

    outer->addWidget(make_hint(
        this, dialog_geometry::kTaskFieldsSize,
        QStringLiteral("The six measured custom fields. A blank field clears the stored "
                       "value.\nEdits are queued and written by Save, like Insert/Delete "
                       "Task.")));

    auto* form = make_form();
    form->setObjectName(QStringLiteral("taskFieldsForm"));
    const std::vector<app::TaskFieldSpec>& specs = app::task_field_specs();
    for (const app::TaskFieldSpec& spec : specs) {
        auto* line = make_line_edit(this);
        line->setText(to_q(app::field_value_text(row, spec.field_id, format)));
        form->addRow(make_form_label(this, to_q(spec.title)), line);
        inputs_.push_back(line);
    }
    outer->addLayout(form);
    outer->addStretch(1);
    outer->addWidget(make_button_box(*this));
}

void TaskFieldsDialog::accept() {
    std::vector<app::TaskFieldInput> values;
    const std::vector<app::TaskFieldSpec>& specs = app::task_field_specs();
    for (std::size_t index = 0; index < specs.size() && index < inputs_.size(); ++index) {
        app::TaskFieldInput input;
        input.field_id = specs[index].field_id;
        input.text = inputs_[index]->text().toStdString();
        values.push_back(std::move(input));
    }
    std::vector<app::StructuralEdit> pending;
    std::string failure;
    if (!app::make_task_field_edits(row_, values, format_, pending, failure)) {
        QMessageBox::warning(this, QStringLiteral("Task Fields"), to_q(failure));
        return;
    }
    edits_ = std::move(pending);
    QDialog::accept();
}

bool edit_task_fields(QWidget* parent, const app::TaskRow& row,
                      const core::sheet::SheetFormat& format,
                      std::vector<app::StructuralEdit>& edits, QString* error) {
    TaskFieldsDialog dialog(row, format, parent);
    const int result = dialog.exec();
    if (error != nullptr) {
        error->clear();
    }
    if (result != QDialog::Accepted) {
        return false;
    }
    edits = dialog.edits();
    return true;
}

// ---------------------------------------------------------------------------
// Predecessors
// ---------------------------------------------------------------------------

PredecessorsDialog::PredecessorsDialog(const app::PlanInput& loaded,
                                       const app::StructuralEdits& edits,
                                       std::uint32_t successor_uid, QWidget* parent)
    : QDialog(parent),
      loaded_(loaded),
      local_(edits),
      successor_uid_(successor_uid) {
    setWindowTitle(QStringLiteral("Predecessors"));
    auto* outer = make_outer(*this, dialog_geometry::kPredecessorsSize);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("predecessorList"));
    list_->setMinimumSize(420, 140);
    outer->addWidget(list_);

    delete_button_ = make_button(this, QStringLiteral("Delete Selected"));
    delete_button_->setEnabled(false);
    outer->addWidget(delete_button_);

    auto* add_row = new QHBoxLayout();
    add_row->setContentsMargins(0, 0, 0, 0);
    add_row->setSpacing(kSpacing);
    predecessor_ = new QComboBox(this);
    predecessor_->setFixedWidth(220);
    predecessor_->setFixedHeight(kControlHeight);
    type_ = new QComboBox(this);
    type_->setFixedWidth(90);
    type_->setFixedHeight(kControlHeight);
    lag_ = new QLineEdit(QStringLiteral("0"), this);
    lag_->setFixedWidth(70);
    lag_->setFixedHeight(kControlHeight);
    add_button_ = make_button(this, QStringLiteral("Add"));
    add_row->addWidget(new QLabel(QStringLiteral("Predecessor"), this));
    add_row->addWidget(predecessor_, 1);
    add_row->addWidget(new QLabel(QStringLiteral("Type"), this));
    add_row->addWidget(type_);
    add_row->addWidget(new QLabel(QStringLiteral("Lag"), this));
    add_row->addWidget(lag_);
    add_row->addWidget(add_button_);
    outer->addLayout(add_row);

    outer->addStretch(1);
    outer->addWidget(make_button_box(*this));

    const std::vector<core::gantt::LinkType> types = {
        core::gantt::LinkType::finish_to_start, core::gantt::LinkType::start_to_start,
        core::gantt::LinkType::finish_to_finish, core::gantt::LinkType::start_to_finish};
    for (const core::gantt::LinkType link_type : types) {
        type_->addItem(QString::fromLatin1(app::link_type_name(link_type)),
                       QVariant(static_cast<int>(link_type)));
    }

    QObject::connect(list_, &QListWidget::currentItemChanged, this,
                     [this](QListWidgetItem*, QListWidgetItem*) {
                         delete_button_->setEnabled(list_->currentItem() != nullptr);
                     });

    QObject::connect(add_button_, &QPushButton::clicked, this, [this](bool) {
        if (predecessor_->currentIndex() < 0) {
            QMessageBox::information(this, QStringLiteral("Predecessors"),
                                     QStringLiteral("There is no task to link to."));
            return;
        }
        const std::uint32_t predecessor_uid = predecessor_->currentData().toUInt();
        const core::gantt::LinkType link_type =
            static_cast<core::gantt::LinkType>(type_->currentData().toInt());
        app::StructuralEdit edit;
        std::string failure;
        const app::PlanInput display = local_.apply(loaded_);
        if (!app::make_link_add(display, local_, predecessor_uid, successor_uid_, link_type,
                                lag_->text().toStdString(), edit, failure)) {
            QMessageBox::warning(this, QStringLiteral("Predecessors"), to_q(failure));
            return;
        }
        if (!local_.push(edit)) {
            QMessageBox::warning(this, QStringLiteral("Predecessors"),
                                 QStringLiteral("That link is already pending."));
            return;
        }
        rebuild();
    });

    QObject::connect(delete_button_, &QPushButton::clicked, this, [this](bool) {
        QListWidgetItem* item = list_->currentItem();
        if (item == nullptr) {
            return;
        }
        const app::PlanInput display = local_.apply(loaded_);
        const std::vector<app::PredecessorListItem> rows =
            app::predecessors_of(display, local_, successor_uid_);
        const int index = list_->row(item);
        if (index < 0 || static_cast<std::size_t>(index) >= rows.size()) {
            return;
        }
        app::StructuralEdit edit;
        std::string failure;
        if (!app::make_link_delete(rows[static_cast<std::size_t>(index)], successor_uid_, edit,
                                   failure)) {
            QMessageBox::warning(this, QStringLiteral("Predecessors"), to_q(failure));
            return;
        }
        if (!local_.push(edit)) {
            QMessageBox::warning(this, QStringLiteral("Predecessors"),
                                 QStringLiteral("That link delete is already pending."));
            return;
        }
        rebuild();
    });

    rebuild();
}

void PredecessorsDialog::rebuild() {
    if (list_ == nullptr || predecessor_ == nullptr) {
        return;
    }
    list_->clear();
    const app::PlanInput display = local_.apply(loaded_);
    for (const app::PredecessorListItem& item :
         app::predecessors_of(display, local_, successor_uid_)) {
        auto* row = new QListWidgetItem(predecessor_row_text(item), list_);
        row->setData(Qt::UserRole, static_cast<uint>(item.link_uid));
        row->setData(Qt::UserRole + 1, item.pending ? 1 : 0);
    }
    predecessor_->clear();
    for (const app::TaskRow& task : display.tasks) {
        if (task.uid == successor_uid_) {
            continue;
        }
        predecessor_->addItem(to_q(std::to_string(task.id) + "  " + task.name),
                              QVariant(static_cast<uint>(task.uid)));
    }
    delete_button_->setEnabled(list_->currentItem() != nullptr);
}

bool edit_predecessors(QWidget* parent, const app::PlanInput& loaded,
                       const app::StructuralEdits& edits, std::uint32_t successor_uid,
                       app::StructuralEdits& out_edits, QString* error) {
    PredecessorsDialog dialog(loaded, edits, successor_uid, parent);
    const int result = dialog.exec();
    if (error != nullptr) {
        error->clear();
    }
    if (result != QDialog::Accepted) {
        return false;
    }
    out_edits = dialog.pending_edits();
    return true;
}

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

FilterDialog::FilterDialog(const std::vector<app::FilterField>& fields,
                           const core::sheet::SheetFormat& format, QWidget* parent)
    : QDialog(parent), fields_(fields), format_(format) {
    setWindowTitle(QStringLiteral("Filter"));
    auto* outer = make_outer(*this, dialog_geometry::kFilterSize);

    auto* form = make_form();
    form->setObjectName(QStringLiteral("filterForm"));
    field_ = make_combo(this);
    for (const app::FilterField& candidate : fields_) {
        field_->addItem(to_q(candidate.title));
    }
    op_ = make_combo(this);
    for (const app::FilterOperatorChoice& candidate : app::filter_operators()) {
        op_->addItem(to_q(candidate.title));
    }
    value_ = make_line_edit(this);
    form->addRow(make_form_label(this, QStringLiteral("Field")), field_);
    form->addRow(make_form_label(this, QStringLiteral("Operator")), op_);
    form->addRow(make_form_label(this, QStringLiteral("Value")), value_);
    outer->addLayout(form);

    outer->addWidget(make_hint(
        this, dialog_geometry::kFilterSize,
        QStringLiteral("The value is read with the sheet's own parser: \"3d\" for a duration, "
                       "\"2026-10-20\" for a date, \"Yes\" for a flag.")));
    outer->addStretch(1);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(kSpacing);
    buttons->addStretch(1);
    apply_ = make_button(this, QStringLiteral("Apply"));
    clear_ = make_button(this, QStringLiteral("Clear Filter"));
    cancel_ = make_button(this, QStringLiteral("Cancel"));
    buttons->addWidget(apply_);
    buttons->addWidget(clear_);
    buttons->addWidget(cancel_);
    outer->addLayout(buttons);

    QObject::connect(apply_, &QPushButton::clicked, this, [this](bool) { apply(); });
    QObject::connect(clear_, &QPushButton::clicked, this, [this](bool) { clear(); });
    QObject::connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
}

void FilterDialog::apply() {
    if (field_->currentIndex() < 0 || op_->currentIndex() < 0) {
        QMessageBox::information(this, QStringLiteral("Filter"),
                                 QStringLiteral("Choose a field and an operator."));
        return;
    }
    app::FilterRule rule;
    rule.field_index = static_cast<std::size_t>(field_->currentIndex());
    rule.operator_index = static_cast<std::size_t>(op_->currentIndex());
    rule.value = value_->text().toStdString();
    const std::vector<app::FilterRule> rules = {rule};
    const app::FilterBuild built =
        app::build_filter(fields_, app::filter_operators(), rules, format_, app::FilterJoin::all);
    if (!built.ok) {
        QMessageBox::warning(this, QStringLiteral("Filter"), to_q(built.error));
        return;
    }
    definition_ = built.definition;
    description_ = built.description;
    cleared_ = false;
    QDialog::accept();
}

void FilterDialog::clear() {
    cleared_ = true;
    definition_ = core::view::FilterDefinition{};
    description_.clear();
    QDialog::accept();
}

bool edit_filter(QWidget* parent, const std::vector<app::FilterField>& fields,
                 const core::sheet::SheetFormat& format,
                 core::view::FilterDefinition* definition, std::string* description,
                 bool* cleared, QString* error) {
    if (definition == nullptr || description == nullptr || cleared == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("internal error: no filter output");
        }
        return false;
    }
    FilterDialog dialog(fields, format, parent);
    const int result = dialog.exec();
    if (error != nullptr) {
        error->clear();
    }
    if (result != QDialog::Accepted) {
        return false;
    }
    *definition = dialog.definition();
    *description = dialog.description();
    *cleared = dialog.cleared();
    return true;
}

// ---------------------------------------------------------------------------
// Sort (wave 31, W31-1.1)
// ---------------------------------------------------------------------------

SortDialog::SortDialog(const std::vector<app::SortField>& fields, QWidget* parent)
    : QDialog(parent), fields_(fields) {
    setWindowTitle(QStringLiteral("Sort"));
    auto* outer = make_outer(*this, dialog_geometry::kSortSize);

    auto* form = make_form();
    form->setObjectName(QStringLiteral("sortForm"));
    field_ = make_combo(this);
    for (const app::SortField& candidate : fields_) {
        field_->addItem(to_q(candidate.title));
    }
    order_ = make_combo(this);
    order_->addItem(QStringLiteral("Ascending"));
    order_->addItem(QStringLiteral("Descending"));
    form->addRow(make_form_label(this, QStringLiteral("Sort by")), field_);
    form->addRow(make_form_label(this, QStringLiteral("Order")), order_);
    outer->addLayout(form);

    outer->addWidget(make_hint(
        this, dialog_geometry::kSortSize,
        QStringLiteral("This sort applies to this session only; the file keeps its own "
                       "row order.  Children stay under their summary.")));
    outer->addStretch(1);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(kSpacing);
    buttons->addStretch(1);
    sort_ = make_button(this, QStringLiteral("Sort"));
    clear_ = make_button(this, QStringLiteral("Clear Sort"));
    cancel_ = make_button(this, QStringLiteral("Cancel"));
    buttons->addWidget(sort_);
    buttons->addWidget(clear_);
    buttons->addWidget(cancel_);
    outer->addLayout(buttons);

    QObject::connect(sort_, &QPushButton::clicked, this, [this](bool) { apply(); });
    QObject::connect(clear_, &QPushButton::clicked, this, [this](bool) { clear(); });
    QObject::connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
}

void SortDialog::apply() {
    if (field_->currentIndex() < 0 || order_->currentIndex() < 0) {
        QMessageBox::information(this, QStringLiteral("Sort"),
                                 QStringLiteral("Choose a field and an order."));
        return;
    }
    app::SortChoice choice;
    choice.field_index = static_cast<std::size_t>(field_->currentIndex());
    choice.ascending = order_->currentIndex() == 0;
    const app::SortBuild built = app::build_sort(fields_, choice);
    if (!built.ok) {
        QMessageBox::warning(this, QStringLiteral("Sort"), to_q(built.error));
        return;
    }
    definition_ = built.definition;
    description_ = built.description;
    cleared_ = false;
    QDialog::accept();
}

void SortDialog::clear() {
    cleared_ = true;
    definition_ = core::view::SortDefinition{};
    description_.clear();
    QDialog::accept();
}

bool edit_sort(QWidget* parent, const std::vector<app::SortField>& fields,
               core::view::SortDefinition* definition, std::string* description,
               bool* cleared, QString* error) {
    if (definition == nullptr || description == nullptr || cleared == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("internal error: no sort output");
        }
        return false;
    }
    SortDialog dialog(fields, parent);
    const int result = dialog.exec();
    if (error != nullptr) {
        error->clear();
    }
    if (result != QDialog::Accepted) {
        return false;
    }
    *definition = dialog.definition();
    *description = dialog.description();
    *cleared = dialog.cleared();
    return true;
}

// ---------------------------------------------------------------------------
// Zoom (wave 31, W31-2.1)
// ---------------------------------------------------------------------------

double ZoomChoice::span_days() const noexcept {
    switch (target) {
        case ZoomTarget::week:
            return 7.0;
        case ZoomTarget::two_weeks:
            return 14.0;
        case ZoomTarget::month:
            return app::kZoomMonthDays;
        case ZoomTarget::three_months:
            return 3.0 * app::kZoomMonthDays;
        case ZoomTarget::custom: {
            double unit_days = 1.0;
            if (custom_unit == 1) {
                unit_days = 7.0;
            } else if (custom_unit == 2) {
                unit_days = app::kZoomMonthDays;
            }
            return static_cast<double>(custom_count) * unit_days;
        }
        case ZoomTarget::selected_tasks:
        case ZoomTarget::entire_project:
            return 0.0;
    }
    return 0.0;
}

ZoomDialog::ZoomDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Zoom"));
    auto* outer = make_outer(*this, dialog_geometry::kZoomSize);

    // The seven "Zoom to" radios.  They share one parent so Qt's auto-exclusive
    // group applies; Project's dialog has the same seven choices (dialog 756).
    auto* choices = new QWidget(this);
    choices->setObjectName(QStringLiteral("zoomChoices"));
    auto* rows = new QVBoxLayout(choices);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(4);
    week_ = new QRadioButton(QStringLiteral("&1 week"), choices);
    two_weeks_ = new QRadioButton(QStringLiteral("&2 weeks"), choices);
    month_ = new QRadioButton(QStringLiteral("1 &month"), choices);
    three_months_ = new QRadioButton(QStringLiteral("&3 months"), choices);
    selected_ = new QRadioButton(QStringLiteral("&Selected tasks"), choices);
    entire_ = new QRadioButton(QStringLiteral("&Entire project"), choices);
    custom_ = new QRadioButton(QStringLiteral("&Custom:"), choices);
    for (QRadioButton* radio :
         {week_, two_weeks_, month_, three_months_, selected_, entire_, custom_}) {
        rows->addWidget(radio);
    }
    entire_->setChecked(true);
    outer->addWidget(choices);

    auto* custom_row = new QHBoxLayout();
    custom_row->setContentsMargins(0, 0, 0, 0);
    custom_row->setSpacing(kSpacing);
    custom_row->addSpacing(kLabelWidth + kSpacing);
    custom_count_ = new QSpinBox(this);
    custom_count_->setRange(1, 9999);
    custom_count_->setValue(1);
    custom_count_->setFixedSize(80, kControlHeight);
    custom_row->addWidget(custom_count_);
    custom_units_ = new QComboBox(this);
    custom_units_->addItem(QStringLiteral("Days"));
    custom_units_->addItem(QStringLiteral("Weeks"));
    custom_units_->addItem(QStringLiteral("Months"));
    custom_units_->setFixedSize(130, kControlHeight);
    custom_row->addWidget(custom_units_);
    custom_row->addStretch(1);
    outer->addLayout(custom_row);

    // The session-only rule is said where the user makes the choice (wave-31
    // brief section 1).
    outer->addWidget(make_hint(
        this, dialog_geometry::kZoomSize,
        QStringLiteral("This zoom applies to this session only; the file keeps its own "
                       "timescale.  Entire project and Selected tasks fit the plan's span.")));
    outer->addStretch(1);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(kSpacing);
    buttons->addStretch(1);
    reset_ = make_button(this, QStringLiteral("Reset"));
    ok_ = make_button(this, QStringLiteral("OK"));
    cancel_ = make_button(this, QStringLiteral("Cancel"));
    buttons->addWidget(reset_);
    buttons->addWidget(ok_);
    buttons->addWidget(cancel_);
    outer->addLayout(buttons);

    QObject::connect(reset_, &QPushButton::clicked, this, [this](bool) { reset_controls(); });
    QObject::connect(ok_, &QPushButton::clicked, this, &QDialog::accept);
    QObject::connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
}

ZoomChoice ZoomDialog::choice() const {
    ZoomChoice result;
    if (week_->isChecked()) {
        result.target = ZoomTarget::week;
    } else if (two_weeks_->isChecked()) {
        result.target = ZoomTarget::two_weeks;
    } else if (month_->isChecked()) {
        result.target = ZoomTarget::month;
    } else if (three_months_->isChecked()) {
        result.target = ZoomTarget::three_months;
    } else if (selected_->isChecked()) {
        result.target = ZoomTarget::selected_tasks;
    } else if (custom_->isChecked()) {
        result.target = ZoomTarget::custom;
        result.custom_count = custom_count_->value();
        result.custom_unit = custom_units_->currentIndex();
    } else {
        result.target = ZoomTarget::entire_project;
    }
    return result;
}

void ZoomDialog::reset_controls() {
    // Project's Reset returns the dialog's fields to the state it opened in;
    // the view itself changes only on OK.  The port's initial state is Entire
    // project with Custom at 1 Day (see the header's comment).
    entire_->setChecked(true);
    custom_count_->setValue(1);
    custom_units_->setCurrentIndex(0);
}

bool ask_task_name(QWidget* parent, QString* name) {
    if (name == nullptr) {
        return false;
    }
    bool ok = false;
    const QString text =
        QInputDialog::getText(parent, QStringLiteral("Insert Task"),
                              QStringLiteral("Task name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return false;
    }
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        QMessageBox::information(parent, QStringLiteral("Insert Task"),
                                 QStringLiteral("A new task needs a name."));
        return false;
    }
    *name = trimmed;
    return true;
}


// ---------------------------------------------------------------------------
// Task Information (wave 21)
// ---------------------------------------------------------------------------

TaskInformationDialog::TaskInformationDialog(const TaskInformationValues& current,
                                             QWidget* parent)
    : QDialog(parent), values_(current) {
    setWindowTitle(QStringLiteral("Task Information"));
    const dialog_geometry::DialogSize size = dialog_geometry::kTaskInformationSize;
    auto* outer = make_outer(*this, size);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("taskInformationTabs"));

    // --- General -----------------------------------------------------------
    auto* general = new QWidget(tabs_);
    auto* general_layout = new QVBoxLayout(general);
    general_layout->setContentsMargins(kMargin / 2, kMargin / 2, kMargin / 2, kMargin / 2);
    general_layout->setSpacing(kSpacing);
    QFormLayout* form = make_form();

    name_ = make_line_edit(general);
    name_->setText(to_q(current.name));
    form->addRow(make_form_label(general, QStringLiteral("&Name:")), name_);

    duration_ = make_line_edit(general);
    duration_->setText(to_q(current.duration_text));
    form->addRow(make_form_label(general, QStringLiteral("D&uration:")), duration_);

    // Estimated has no writer in this port: shown, disabled (decision 15).
    auto* estimated = new QCheckBox(QStringLiteral("&Estimated"), general);
    estimated->setFixedHeight(kCheckHeight);
    estimated->setEnabled(false);
    estimated->setToolTip(QStringLiteral("not implemented in this port"));
    form->addRow(make_form_label(general, QStringLiteral("Estimated:")), estimated);

    // W28-1.2: editable on the measured actuals writer.  A changed value is
    // read by accept() and queued by MainWindow::apply_task_information as the
    // same set_percent edit the ribbon's five % buttons make.
    percent_ = make_line_edit(general);
    percent_->setText(to_q(current.percent_text));
    percent_->setToolTip(
        QStringLiteral("A whole number from 0 to 100; OK queues the edit and File > Save "
                       "writes Project's measured actuals"));
    form->addRow(make_form_label(general, QStringLiteral("Percent c&omplete:")), percent_);

    auto* priority = make_spin_box(general);
    priority->setRange(0, 1000);
    priority->setEnabled(false);
    priority->setToolTip(QStringLiteral("not implemented in this port"));
    form->addRow(make_form_label(general, QStringLiteral("&Priority:")), priority);

    auto* schedule = make_combo(general);
    schedule->addItem(QStringLiteral("Manually Scheduled"));
    schedule->addItem(QStringLiteral("Auto Scheduled"));
    schedule->setCurrentIndex(current.manually_scheduled ? 0 : 1);
    schedule->setEnabled(false);
    schedule->setToolTip(QStringLiteral("not implemented in this port"));
    form->addRow(make_form_label(general, QStringLiteral("Schedule &mode:")), schedule);

    start_ = make_line_edit(general);
    start_->setText(to_q(current.start_text));
    // W32-0.2 (R31-2, High): Start is read-only.  Project's measured Start save
    // (the wave-31 Fill pair) rewrites calculated and effective dates, slack,
    // the project roll-up, a Var2 record and DSI; this port's writer stores only
    // the constraint window, so a Start edit here would save a shape Project
    // does not produce.  Refuse first with the decision-9 reason.
    start_->setReadOnly(true);
    start_->setToolTip(QStringLiteral(
        "Read-only: a Start edit's save is not measured against Project"));
    form->addRow(make_form_label(general, QStringLiteral("S&tart:")), start_);

    finish_ = make_line_edit(general);
    finish_->setText(to_q(current.finish_text));
    form->addRow(make_form_label(general, QStringLiteral("&Finish:")), finish_);
    general_layout->addLayout(form);

    auto* timeline_label = new QLabel(QStringLiteral("Display on Timeline"), general);
    QFont group_font = timeline_label->font();
    group_font.setBold(true);
    timeline_label->setFont(group_font);
    general_layout->addWidget(timeline_label);
    auto* timeline_row = new QHBoxLayout();
    timeline_row->setContentsMargins(0, 0, 0, 0);
    timeline_row->setSpacing(kSpacing);
    hide_bar_ = new QCheckBox(QStringLiteral("Hide &Bar"), general);
    hide_bar_->setFixedHeight(kCheckHeight);
    hide_bar_->setEnabled(false);
    hide_bar_->setToolTip(QStringLiteral("not implemented in this port"));
    rollup_ = new QCheckBox(QStringLiteral("&Rollup"), general);
    rollup_->setFixedHeight(kCheckHeight);
    rollup_->setEnabled(false);
    rollup_->setToolTip(QStringLiteral("not implemented in this port"));
    timeline_row->addWidget(hide_bar_);
    timeline_row->addWidget(rollup_);
    timeline_row->addStretch(1);
    general_layout->addLayout(timeline_row);
    general_layout->addWidget(make_hint(
        general, size,
        QStringLiteral("Name, Duration, Start and Finish are written through the sheet's "
                       "edit model; Percent complete is queued as Project's measured "
                       "actuals edit.  Estimated, Priority, Schedule mode and the Display "
                       "on Timeline options are read-only in this port: there is no writer "
                       "for them yet (decision 15).")));
    general_layout->addStretch(1);
    tabs_->addTab(general, QStringLiteral("General"));

    // --- the five laid-out-only tabs (decision 15) --------------------------
    struct TabSpec {
        const char* title;
        const char* body;
        const char* action;
    };
    const TabSpec specs[] = {
        {"Predecessors",
         "This task's links are edited by Task > Predecessors...; the six-tab "
         "form does not write them yet.",
         "Add Predecessor"},
        {"Resources",
         "Resource assignments are edited by the Assign Resources command; this "
         "port does not write assignments from Task Information.",
         "Assign Resources"},
        {"Advanced",
         "Constraint type, deadline, task calendar, effort-driven and the WBS code "
         "are not decoded into an editable model yet.",
         "Set Constraint"},
        {"Notes", "Task notes are not written by this port yet.", "Add Notes"},
    };
    for (const TabSpec& spec : specs) {
        auto* page = new QWidget(tabs_);
        auto* page_layout = new QVBoxLayout(page);
        page_layout->setContentsMargins(kMargin / 2, kMargin / 2, kMargin / 2, kMargin / 2);
        page_layout->setSpacing(kSpacing);
        auto* body = new QLabel(QString::fromUtf8(spec.body), page);
        body->setWordWrap(true);
        body->setTextFormat(Qt::PlainText);
        page_layout->addWidget(body);
        QPushButton* action = make_button(page, QString::fromUtf8(spec.action));
        action->setEnabled(false);
        action->setToolTip(QStringLiteral("not implemented in this port"));
        page_layout->addWidget(action, 0, Qt::AlignLeft);
        page_layout->addWidget(
            make_hint(page, size,
                      QStringLiteral("Laid out only: the engine for this tab does not exist "
                                     "in this port yet (decision 15).  Cancel works.")));
        page_layout->addStretch(1);
        tabs_->addTab(page, QString::fromUtf8(spec.title));
    }

    // --- Custom Fields (wave 26, W26-5.3) ----------------------------------
    // The per-task values that used to be the Custom Fields command.  The six
    // fields are the ones whose storage is measured (app::task_field_specs);
    // OK turns changed texts into set_field/clear_field edits, written by the
    // shared save cascade.
    {
        auto* page = new QWidget(tabs_);
        auto* page_layout = new QVBoxLayout(page);
        page_layout->setContentsMargins(kMargin / 2, kMargin / 2, kMargin / 2, kMargin / 2);
        page_layout->setSpacing(kSpacing);
        QFormLayout* custom_form = make_form();
        for (std::size_t index = 0; index < values_.custom_field_ids.size(); ++index) {
            const app::TaskFieldSpec* spec =
                app::task_field_spec(values_.custom_field_ids[index]);
            if (spec == nullptr) {
                continue;
            }
            auto* edit = new QLineEdit(page);
            edit->setFixedSize(kFieldWidth, kControlHeight);
            edit->setObjectName(
                QStringLiteral("customField%1").arg(static_cast<qulonglong>(spec->field_id)));
            edit->setText(index < values_.custom_field_texts.size()
                              ? to_q(values_.custom_field_texts[index])
                              : QString());
            custom_form->addRow(make_form_label(page, to_q(spec->title) + QStringLiteral(":")),
                                edit);
            custom_field_inputs_.push_back(edit);
        }
        page_layout->addLayout(custom_form);
        page_layout->addWidget(make_hint(
            page, size,
            QStringLiteral("These are the six custom fields whose file storage is "
                           "measured.  A blank value clears a stored value; a changed "
                           "value is queued and written by Save.")));
        page_layout->addStretch(1);
        tabs_->addTab(page, QStringLiteral("Custom Fields"));
    }

    outer->addWidget(tabs_, 1);
    outer->addWidget(make_button_box(*this));
}

void TaskInformationDialog::accept() {
    values_.name = name_->text().toStdString();
    values_.duration_text = duration_->text().toStdString();
    values_.start_text = start_->text().toStdString();
    values_.finish_text = finish_->text().toStdString();
    if (percent_ != nullptr) {
        values_.percent_text = percent_->text().toStdString();
    }
    values_.custom_field_texts.clear();
    for (QLineEdit* edit : custom_field_inputs_) {
        values_.custom_field_texts.push_back(edit->text().toStdString());
    }
    QDialog::accept();
}

bool edit_task_information(QWidget* parent, TaskInformationValues* values) {
    if (values == nullptr) {
        return false;
    }
    TaskInformationDialog dialog(*values, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    *values = dialog.values();
    return true;
}

}  // namespace msqt
