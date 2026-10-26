// app_qt/resource_form.cpp -- see resource_form.h.

#include "resource_form.h"

#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QVBoxLayout>

#include "shell_style.h"

namespace msqt {
namespace {

QLineEdit* make_field(QWidget* parent, const QString& object_name) {
    auto* field = new QLineEdit(parent);
    field->setObjectName(object_name);
    // Read-only, never disabled: the value is a FACT the user may select and
    // copy, not an edit.  The Resource Sheet is read-only and this port has no
    // resource-field writer, so an editable field would offer a write path with
    // no model behind it.
    field->setReadOnly(true);
    field->setFrame(true);
    field->setMinimumWidth(140);
    field->setFixedHeight(22);
    field->setFont(QFont(QString(), kBodyPointSize));
    return field;
}

QLabel* make_caption(QWidget* parent, const QString& text) {
    auto* label = new QLabel(text, parent);
    label->setFont(QFont(QString(), kStatusPointSize));
    label->setFixedWidth(78);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

}  // namespace

ResourceForm::ResourceForm(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("resourceForm"));
    // The pane is a form surface: Base is white in the light theme and the
    // theme's dark surface in Dark Gray/Black, so it follows the live palette.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 6, 10, 6);
    outer->setSpacing(2);

    // Two caption/field pairs per row keep the eight facts inside the same
    // quarter-height pane the Task Form uses.
    grid_ = new QGridLayout();
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(8);
    grid_->setVerticalSpacing(2);

    name_ = make_field(this, QStringLiteral("resourceFormName"));
    id_ = make_field(this, QStringLiteral("resourceFormId"));
    uid_ = make_field(this, QStringLiteral("resourceFormUid"));
    type_ = make_field(this, QStringLiteral("resourceFormType"));
    initials_ = make_field(this, QStringLiteral("resourceFormInitials"));
    max_units_ = make_field(this, QStringLiteral("resourceFormMaxUnits"));
    work_ = make_field(this, QStringLiteral("resourceFormWork"));
    assignments_ = make_field(this, QStringLiteral("resourceFormAssignments"));

    const auto add = [this](int row, int column, const QString& caption,
                            QLineEdit* field) {
        grid_->addWidget(make_caption(this, caption), row, column * 2);
        grid_->addWidget(field, row, column * 2 + 1);
    };
    add(0, 0, QStringLiteral("Name"), name_);
    add(0, 1, QStringLiteral("Initials"), initials_);
    add(1, 0, QStringLiteral("ID"), id_);
    add(1, 1, QStringLiteral("Type"), type_);
    add(2, 0, QStringLiteral("Unique ID"), uid_);
    add(2, 1, QStringLiteral("Max Units"), max_units_);
    add(3, 0, QStringLiteral("Work"), work_);
    add(3, 1, QStringLiteral("Assignments"), assignments_);
    grid_->setColumnStretch(1, 1);
    grid_->setColumnStretch(3, 1);
    outer->addLayout(grid_);

    selection_ = new QLabel(this);
    selection_->setObjectName(QStringLiteral("resourceFormSelection"));
    selection_->setFont(QFont(QString(), kStatusPointSize));
    selection_->setWordWrap(true);
    outer->addWidget(selection_);
    outer->addStretch(1);

    clear();
}

void ResourceForm::set_values(const ResourceFormValues& values) {
    if (!values.has_resource) {
        clear();
        return;
    }
    name_->setText(values.name);
    id_->setText(values.id);
    uid_->setText(values.uid);
    type_->setText(values.type);
    initials_->setText(values.initials);
    max_units_->setText(values.max_units);
    work_->setText(values.work);
    assignments_->setText(values.assignments);
    selection_->setText(values.selection);
}

void ResourceForm::clear() {
    // `clear()` on a QLineEdit also clears any selection the user made, so a
    // cleared pane cannot keep a copied fragment of the previous resource.
    for (QLineEdit* field :
         {name_, id_, uid_, type_, initials_, max_units_, work_, assignments_}) {
        field->clear();
    }
    selection_->setText(QStringLiteral("No resource selected"));
}

}  // namespace msqt
