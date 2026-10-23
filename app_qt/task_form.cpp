// app_qt/task_form.cpp -- see task_form.h.

#include "task_form.h"

#include <QFont>
#include <QFormLayout>
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
    // Display only this wave: the values come from the sheet model, and the
    // editable path is Task Information (see the header).
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
    return label;
}

}  // namespace

TaskForm::TaskForm(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("taskForm"));
    // The pane is a form surface: Base is white in the light theme and the
    // theme's dark surface in Dark Gray/Black, so it follows the live palette.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 6, 10, 6);
    outer->setSpacing(2);

    form_ = new QFormLayout();
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setHorizontalSpacing(8);
    form_->setVerticalSpacing(2);
    form_->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    name_ = make_field(this, QStringLiteral("taskFormName"));
    duration_ = make_field(this, QStringLiteral("taskFormDuration"));
    start_ = make_field(this, QStringLiteral("taskFormStart"));
    finish_ = make_field(this, QStringLiteral("taskFormFinish"));
    percent_ = make_field(this, QStringLiteral("taskFormPercent"));
    predecessors_ = make_field(this, QStringLiteral("taskFormPredecessors"));
    resources_ = make_field(this, QStringLiteral("taskFormResources"));

    form_->addRow(make_caption(this, QStringLiteral("Name")), name_);
    form_->addRow(make_caption(this, QStringLiteral("Duration")), duration_);
    form_->addRow(make_caption(this, QStringLiteral("Start")), start_);
    form_->addRow(make_caption(this, QStringLiteral("Finish")), finish_);
    form_->addRow(make_caption(this, QStringLiteral("% Complete")), percent_);
    form_->addRow(make_caption(this, QStringLiteral("Predecessors")), predecessors_);
    form_->addRow(make_caption(this, QStringLiteral("Resources")), resources_);
    outer->addLayout(form_);

    selection_ = new QLabel(this);
    selection_->setObjectName(QStringLiteral("taskFormSelection"));
    selection_->setFont(QFont(QString(), kStatusPointSize));
    outer->addWidget(selection_);
    outer->addStretch(1);

    clear();
}

void TaskForm::set_values(const TaskFormValues& values) {
    if (!values.has_task) {
        clear();
        return;
    }
    name_->setText(values.name);
    duration_->setText(values.duration);
    start_->setText(values.start);
    finish_->setText(values.finish);
    percent_->setText(values.percent);
    predecessors_->setText(values.predecessors);
    resources_->setText(values.resources);
    selection_->clear();
}

void TaskForm::clear() {
    for (QLineEdit* field :
         {name_, duration_, start_, finish_, percent_, predecessors_, resources_}) {
        field->clear();
    }
    selection_->setText(QStringLiteral("No task selected"));
}

}  // namespace msqt
