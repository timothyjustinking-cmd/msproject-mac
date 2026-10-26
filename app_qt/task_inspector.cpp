// app_qt/task_inspector.cpp -- see task_inspector.h.

#include "task_inspector.h"

#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QToolButton>
#include <QVBoxLayout>

#include "shell_style.h"

namespace msqt {
namespace {

QLineEdit* make_field(QWidget* parent, const QString& object_name) {
    auto* field = new QLineEdit(parent);
    field->setObjectName(object_name);
    // Read-only, never disabled: the value is a FACT the user may select and
    // copy, not an edit.  A disabled QLineEdit would grey the text and imply
    // the field could be enabled somewhere; there is no write path here.
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
    label->setFixedWidth(82);
    return label;
}

}  // namespace

TaskInspector::TaskInspector(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("taskInspector"));
    // The pane follows the light/dark palette exactly like the Task Form.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    // The chart pane gives the Gantt the stretch; the inspector keeps the
    // inspector width Project's pane uses and never squeezes the form fields.
    setFixedWidth(260);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 6, 10, 6);
    outer->setSpacing(2);

    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(4);
    auto* title = new QLabel(QStringLiteral("Task Inspector"), this);
    title->setObjectName(QStringLiteral("taskInspectorTitle"));
    QFont title_font(QString(), kBodyPointSize);
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch(1);
    close_ = new QToolButton(this);
    close_->setObjectName(QStringLiteral("taskInspectorClose"));
    close_->setText(QStringLiteral("Close"));
    close_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    close_->setAutoRaise(true);
    close_->setFocusPolicy(Qt::NoFocus);
    close_->setFixedHeight(18);
    close_->setToolTip(QStringLiteral("Close the Task Inspector"));
    connect(close_, &QToolButton::clicked, this, [this](bool) {
        if (close_handler_) {
            close_handler_();
        }
    });
    header->addWidget(close_);
    outer->addLayout(header);

    form_ = new QFormLayout();
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setHorizontalSpacing(8);
    form_->setVerticalSpacing(2);
    form_->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    name_ = make_field(this, QStringLiteral("taskInspectorName"));
    id_ = make_field(this, QStringLiteral("taskInspectorId"));
    uid_ = make_field(this, QStringLiteral("taskInspectorUid"));
    outline_ = make_field(this, QStringLiteral("taskInspectorOutline"));
    type_ = make_field(this, QStringLiteral("taskInspectorType"));
    mode_ = make_field(this, QStringLiteral("taskInspectorMode"));
    start_ = make_field(this, QStringLiteral("taskInspectorStart"));
    finish_ = make_field(this, QStringLiteral("taskInspectorFinish"));
    duration_ = make_field(this, QStringLiteral("taskInspectorDuration"));
    percent_ = make_field(this, QStringLiteral("taskInspectorPercent"));
    critical_ = make_field(this, QStringLiteral("taskInspectorCritical"));
    calendar_ = make_field(this, QStringLiteral("taskInspectorCalendar"));
    predecessors_ = make_field(this, QStringLiteral("taskInspectorPredecessors"));
    successors_ = make_field(this, QStringLiteral("taskInspectorSuccessors"));
    resources_ = make_field(this, QStringLiteral("taskInspectorResources"));

    form_->addRow(make_caption(this, QStringLiteral("Name")), name_);
    form_->addRow(make_caption(this, QStringLiteral("ID")), id_);
    form_->addRow(make_caption(this, QStringLiteral("Unique ID")), uid_);
    form_->addRow(make_caption(this, QStringLiteral("Outline")), outline_);
    form_->addRow(make_caption(this, QStringLiteral("Type")), type_);
    form_->addRow(make_caption(this, QStringLiteral("Mode")), mode_);
    form_->addRow(make_caption(this, QStringLiteral("Start")), start_);
    form_->addRow(make_caption(this, QStringLiteral("Finish")), finish_);
    form_->addRow(make_caption(this, QStringLiteral("Duration")), duration_);
    form_->addRow(make_caption(this, QStringLiteral("% Complete")), percent_);
    form_->addRow(make_caption(this, QStringLiteral("Critical")), critical_);
    form_->addRow(make_caption(this, QStringLiteral("Calendar")), calendar_);
    form_->addRow(make_caption(this, QStringLiteral("Predecessors")), predecessors_);
    form_->addRow(make_caption(this, QStringLiteral("Successors")), successors_);
    form_->addRow(make_caption(this, QStringLiteral("Resources")), resources_);
    outer->addLayout(form_);

    selection_ = new QLabel(this);
    selection_->setObjectName(QStringLiteral("taskInspectorSelection"));
    selection_->setFont(QFont(QString(), kStatusPointSize));
    selection_->setWordWrap(true);
    outer->addWidget(selection_);
    outer->addStretch(1);

    clear();
}

void TaskInspector::set_close_handler(CloseHandler handler) {
    close_handler_ = std::move(handler);
}

void TaskInspector::set_values(const TaskInspectorValues& values) {
    if (!values.has_task) {
        clear();
        return;
    }
    name_->setText(values.name);
    id_->setText(values.id);
    uid_->setText(values.uid);
    outline_->setText(values.outline);
    type_->setText(values.type);
    mode_->setText(values.mode);
    start_->setText(values.start);
    finish_->setText(values.finish);
    duration_->setText(values.duration);
    percent_->setText(values.percent);
    critical_->setText(values.critical);
    calendar_->setText(values.calendar);
    predecessors_->setText(values.predecessors);
    successors_->setText(values.successors);
    resources_->setText(values.resources);
    selection_->setText(values.selection);
}

void TaskInspector::clear() {
    // `clear()` on a QLineEdit also clears any selection the user made, so a
    // cleared pane cannot keep a copied fragment of the previous task.
    name_->clear();
    id_->clear();
    uid_->clear();
    outline_->clear();
    type_->clear();
    mode_->clear();
    start_->clear();
    finish_->clear();
    duration_->clear();
    percent_->clear();
    critical_->clear();
    calendar_->clear();
    predecessors_->clear();
    successors_->clear();
    resources_->clear();
    selection_->setText(QStringLiteral("No task selected"));
}

}  // namespace msqt
