// app_qt/task_inspector.h -- the Task Inspector pane (W30-1.10), read-only.
//
// THE TARGET (wave 30 brief, G1): Inspect is a "read-only pane from the
// scheduler's facts".  Project's Task Inspector lists the stored scheduling
// inputs for the selected task -- its calendar, mode, dates, duration, links
// and assignments -- so the user can see WHY the scheduler placed a bar where
// it is.  This port has no scheduler and no Task Inspector; this widget is the
// facts half only: every value is read from app::PlanInput (the same plan the
// Gantt and the computed sheet columns are built from) and nothing in this
// widget can be edited, queued or saved.
//
// The pane is a VIEW: MainWindow shows/hides it from Task > Tasks > Inspect
// (RibbonCommand::inspect) and refreshes it with the selection, exactly like
// the docked Task Form (W26-6.3/W28-4.1).  It has no Q_OBJECT and no signals:
// the close button reports through a std::function handler MainWindow sets.
//
// THE ONE DERIVATION RULE: the values come from the PLAN, by UID -- never from
// a RecordSet row index, never from the sheet's pending (uncommitted) cell
// text.  Predecessors/Successors/Resources are the same app::*_text helpers
// PlanDocument::fill_computed_columns uses, so the inspector and the computed
// columns cannot disagree.  A pending Insert Task row is shown as
// "(not saved yet)" because its mode and file row are not stored yet.
#pragma once

#include <QString>
#include <QWidget>

#include <functional>

class QFormLayout;
class QLabel;
class QLineEdit;
class QToolButton;

namespace msqt {

// One pane's worth of facts.  `has_task` is false when nothing is selected (or
// the view has no task selection); the pane then clears and says so.  `project`
// marks the synthetic project summary row (uid 0), whose values come from the
// file's project row instead of a task.
struct TaskInspectorValues {
    bool has_task = false;
    bool project = false;
    QString name;
    QString id;
    QString uid;
    QString outline;
    QString type;
    QString mode;
    QString start;
    QString finish;
    QString duration;
    QString percent;
    QString critical;
    QString calendar;
    QString predecessors;
    QString successors;
    QString resources;
    QString selection;
};

class TaskInspector final : public QWidget {
  public:
    // The pane's close button.  MainWindow hides the pane; the widget has no
    // state of its own to save.
    using CloseHandler = std::function<void()>;

    explicit TaskInspector(QWidget* parent = nullptr);

    void set_values(const TaskInspectorValues& values);
    void clear();

    void set_close_handler(CloseHandler handler);

    // The live fields, for the gate.  Every one is read-only; the gate checks
    // that through QLineEdit::isReadOnly().
    QLineEdit* name_field() const noexcept { return name_; }
    QLineEdit* id_field() const noexcept { return id_; }
    QLineEdit* uid_field() const noexcept { return uid_; }
    QLineEdit* outline_field() const noexcept { return outline_; }
    QLineEdit* type_field() const noexcept { return type_; }
    QLineEdit* mode_field() const noexcept { return mode_; }
    QLineEdit* start_field() const noexcept { return start_; }
    QLineEdit* finish_field() const noexcept { return finish_; }
    QLineEdit* duration_field() const noexcept { return duration_; }
    QLineEdit* percent_field() const noexcept { return percent_; }
    QLineEdit* critical_field() const noexcept { return critical_; }
    QLineEdit* calendar_field() const noexcept { return calendar_; }
    QLineEdit* predecessors_field() const noexcept { return predecessors_; }
    QLineEdit* successors_field() const noexcept { return successors_; }
    QLineEdit* resources_field() const noexcept { return resources_; }
    QLabel* selection_label() const noexcept { return selection_; }
    QToolButton* close_button() const noexcept { return close_; }

  private:
    QFormLayout* form_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* id_ = nullptr;
    QLineEdit* uid_ = nullptr;
    QLineEdit* outline_ = nullptr;
    QLineEdit* type_ = nullptr;
    QLineEdit* mode_ = nullptr;
    QLineEdit* start_ = nullptr;
    QLineEdit* finish_ = nullptr;
    QLineEdit* duration_ = nullptr;
    QLineEdit* percent_ = nullptr;
    QLineEdit* critical_ = nullptr;
    QLineEdit* calendar_ = nullptr;
    QLineEdit* predecessors_ = nullptr;
    QLineEdit* successors_ = nullptr;
    QLineEdit* resources_ = nullptr;
    QLabel* selection_ = nullptr;
    QToolButton* close_ = nullptr;
    CloseHandler close_handler_;
};

}  // namespace msqt
