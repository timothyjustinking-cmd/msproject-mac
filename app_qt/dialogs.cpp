// app_qt/dialogs.cpp -- see dialogs.h.

#include "dialogs.h"

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
#include <QString>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "field_link_edits.h"
#include "filter_model.h"

namespace msqt {
namespace {

QString to_q(const std::string& text) { return QString::fromStdString(text); }

// A one-line description of a predecessor list row: "3  Design  FS  2d".
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

bool edit_task_fields(QWidget* parent, const app::TaskRow& row,
                      const core::sheet::SheetFormat& format,
                      std::vector<app::StructuralEdit>& edits, QString* error) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Task Fields"));
    auto* outer = new QVBoxLayout(&dialog);

    auto* hint = new QLabel(
        QStringLiteral("The six measured custom fields. A blank field clears the stored value.\n"
                       "Edits are queued and written by Save, like Insert/Delete Task."),
        &dialog);
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto* form = new QFormLayout();
    std::vector<QLineEdit*> inputs;
    for (const app::TaskFieldSpec& spec : app::task_field_specs()) {
        auto* line = new QLineEdit(
            to_q(app::field_value_text(row, spec.field_id, format)), &dialog);
        line->setMinimumWidth(220);
        form->addRow(to_q(spec.title), line);
        inputs.push_back(line);
    }
    outer->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    bool accepted = false;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        std::vector<app::TaskFieldInput> values;
        const std::vector<app::TaskFieldSpec>& specs = app::task_field_specs();
        for (std::size_t index = 0; index < specs.size() && index < inputs.size(); ++index) {
            app::TaskFieldInput input;
            input.field_id = specs[index].field_id;
            input.text = inputs[index]->text().toStdString();
            values.push_back(std::move(input));
        }
        std::vector<app::StructuralEdit> pending;
        std::string failure;
        if (!app::make_task_field_edits(row, values, format, pending, failure)) {
            QMessageBox::warning(&dialog, QStringLiteral("Task Fields"), to_q(failure));
            return;
        }
        edits = std::move(pending);
        accepted = true;
        dialog.accept();
    });

    const int result = dialog.exec();
    if (!accepted || result != QDialog::Accepted) {
        if (error != nullptr) {
            error->clear();
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool edit_predecessors(QWidget* parent, const app::PlanInput& loaded,
                       const app::StructuralEdits& edits, std::uint32_t successor_uid,
                       app::StructuralEdits& out_edits, QString* error) {
    // The dialog works on a copy: a cancelled dialog queues nothing.
    app::StructuralEdits local = edits;

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Predecessors"));
    auto* outer = new QVBoxLayout(&dialog);

    auto* list = new QListWidget(&dialog);
    list->setMinimumSize(420, 140);
    outer->addWidget(list);

    auto* delete_button = new QPushButton(QStringLiteral("Delete Selected"), &dialog);
    delete_button->setEnabled(false);
    outer->addWidget(delete_button);

    auto* add_row = new QHBoxLayout();
    auto* predecessor = new QComboBox(&dialog);
    predecessor->setMinimumWidth(220);
    auto* type = new QComboBox(&dialog);
    auto* lag = new QLineEdit(QStringLiteral("0"), &dialog);
    lag->setMaximumWidth(70);
    auto* add_button = new QPushButton(QStringLiteral("Add"), &dialog);
    add_row->addWidget(new QLabel(QStringLiteral("Predecessor"), &dialog));
    add_row->addWidget(predecessor, 1);
    add_row->addWidget(new QLabel(QStringLiteral("Type"), &dialog));
    add_row->addWidget(type);
    add_row->addWidget(new QLabel(QStringLiteral("Lag"), &dialog));
    add_row->addWidget(lag);
    add_row->addWidget(add_button);
    outer->addLayout(add_row);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    outer->addWidget(buttons);

    // The display plan for the LOCAL pending stack, rebuilt after every change.
    auto display_plan = [&loaded, &local]() { return local.apply(loaded); };

    auto rebuild = [&]() {
        list->clear();
        const app::PlanInput display = display_plan();
        for (const app::PredecessorListItem& item :
             app::predecessors_of(display, local, successor_uid)) {
            auto* row = new QListWidgetItem(predecessor_row_text(item), list);
            row->setData(Qt::UserRole, static_cast<uint>(item.link_uid));
            row->setData(Qt::UserRole + 1, item.pending ? 1 : 0);
        }
        predecessor->clear();
        for (const app::TaskRow& task : display.tasks) {
            if (task.uid == successor_uid) {
                continue;
            }
            predecessor->addItem(to_q(std::to_string(task.id) + "  " + task.name),
                                 QVariant(static_cast<uint>(task.uid)));
        }
        delete_button->setEnabled(list->currentItem() != nullptr);
    };

    QObject::connect(list, &QListWidget::currentItemChanged, &dialog,
                     [delete_button](QListWidgetItem*, QListWidgetItem*) {
                         delete_button->setEnabled(true);
                     });

    const std::vector<core::gantt::LinkType> types = {
        core::gantt::LinkType::finish_to_start, core::gantt::LinkType::start_to_start,
        core::gantt::LinkType::finish_to_finish, core::gantt::LinkType::start_to_finish};
    for (const core::gantt::LinkType link_type : types) {
        type->addItem(QString::fromLatin1(app::link_type_name(link_type)),
                      QVariant(static_cast<int>(link_type)));
        type->setItemData(type->count() - 1, QString::fromLatin1(app::link_type_name(link_type)),
                          Qt::ToolTipRole);
    }

    QObject::connect(add_button, &QPushButton::clicked, &dialog, [&]() {
        if (predecessor->currentIndex() < 0) {
            QMessageBox::information(&dialog, QStringLiteral("Predecessors"),
                                     QStringLiteral("There is no task to link to."));
            return;
        }
        const std::uint32_t predecessor_uid = predecessor->currentData().toUInt();
        const core::gantt::LinkType link_type =
            static_cast<core::gantt::LinkType>(type->currentData().toInt());
        app::StructuralEdit edit;
        std::string failure;
        const app::PlanInput display = display_plan();
        if (!app::make_link_add(display, local, predecessor_uid, successor_uid, link_type,
                                lag->text().toStdString(), edit, failure)) {
            QMessageBox::warning(&dialog, QStringLiteral("Predecessors"), to_q(failure));
            return;
        }
        if (!local.push(edit)) {
            QMessageBox::warning(&dialog, QStringLiteral("Predecessors"),
                                 QStringLiteral("That link is already pending."));
            return;
        }
        rebuild();
    });

    QObject::connect(delete_button, &QPushButton::clicked, &dialog, [&]() {
        QListWidgetItem* item = list->currentItem();
        if (item == nullptr) {
            return;
        }
        const app::PlanInput display = display_plan();
        const std::vector<app::PredecessorListItem> rows =
            app::predecessors_of(display, local, successor_uid);
        const int index = list->row(item);
        if (index < 0 || static_cast<std::size_t>(index) >= rows.size()) {
            return;
        }
        app::StructuralEdit edit;
        std::string failure;
        if (!app::make_link_delete(rows[static_cast<std::size_t>(index)], successor_uid, edit,
                                   failure)) {
            QMessageBox::warning(&dialog, QStringLiteral("Predecessors"), to_q(failure));
            return;
        }
        if (!local.push(edit)) {
            QMessageBox::warning(&dialog, QStringLiteral("Predecessors"),
                                 QStringLiteral("That link delete is already pending."));
            return;
        }
        rebuild();
    });

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    bool accepted = false;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        accepted = true;
        dialog.accept();
    });

    rebuild();
    const int result = dialog.exec();
    if (error != nullptr) {
        error->clear();
    }
    if (!accepted || result != QDialog::Accepted) {
        return false;
    }
    out_edits = std::move(local);
    return true;
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
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Filter"));
    auto* outer = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();

    auto* field = new QComboBox(&dialog);
    for (const app::FilterField& candidate : fields) {
        field->addItem(to_q(candidate.title));
    }
    auto* op = new QComboBox(&dialog);
    for (const app::FilterOperatorChoice& candidate : app::filter_operators()) {
        op->addItem(to_q(candidate.title));
    }
    auto* value = new QLineEdit(&dialog);
    value->setMinimumWidth(200);
    form->addRow(QStringLiteral("Field"), field);
    form->addRow(QStringLiteral("Operator"), op);
    form->addRow(QStringLiteral("Value"), value);
    outer->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("The value is read with the sheet's own parser: \"3d\" for a duration, "
                       "\"2026-10-20\" for a date, \"Yes\" for a flag."),
        &dialog);
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto* buttons = new QHBoxLayout();
    auto* apply_button = new QPushButton(QStringLiteral("Apply"), &dialog);
    auto* clear_button = new QPushButton(QStringLiteral("Clear Filter"), &dialog);
    auto* cancel_button = new QPushButton(QStringLiteral("Cancel"), &dialog);
    buttons->addStretch(1);
    buttons->addWidget(apply_button);
    buttons->addWidget(clear_button);
    buttons->addWidget(cancel_button);
    outer->addLayout(buttons);

    bool accepted = false;
    QObject::connect(cancel_button, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(clear_button, &QPushButton::clicked, &dialog, [&]() {
        *cleared = true;
        definition->name.clear();
        description->clear();
        accepted = true;
        dialog.accept();
    });
    QObject::connect(apply_button, &QPushButton::clicked, &dialog, [&]() {
        if (field->currentIndex() < 0 || op->currentIndex() < 0) {
            QMessageBox::information(&dialog, QStringLiteral("Filter"),
                                     QStringLiteral("Choose a field and an operator."));
            return;
        }
        app::FilterRule rule;
        rule.field_index = static_cast<std::size_t>(field->currentIndex());
        rule.operator_index = static_cast<std::size_t>(op->currentIndex());
        rule.value = value->text().toStdString();
        const std::vector<app::FilterRule> rules = {rule};
        const app::FilterBuild built = app::build_filter(fields, app::filter_operators(), rules,
                                                         format, app::FilterJoin::all);
        if (!built.ok) {
            QMessageBox::warning(&dialog, QStringLiteral("Filter"), to_q(built.error));
            return;
        }
        *definition = built.definition;
        *description = built.description;
        *cleared = false;
        accepted = true;
        dialog.accept();
    });

    const int result = dialog.exec();
    if (error != nullptr) {
        error->clear();
    }
    return accepted && result == QDialog::Accepted;
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

}  // namespace msqt
