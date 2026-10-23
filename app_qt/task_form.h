// app_qt/task_form.h -- the Task Form pane of the split view (W26-6.3).
//
// Operator decision 16 (docs/ui-parity-spec.md): View > Details docks a task
// form at the bottom, synchronised with the selection, in a resizable splitter.
// This widget is that pane.  It shows the selected task's measured fields --
// Name, Duration, Start, Finish, % Complete, Predecessors and Resources (the
// last two are the same computed texts the stored table shows) -- and is
// DISPLAY ONLY in this wave: the fields are read-only because the form's edit
// path would need its own save semantics; MainWindow::apply_task_information()
// remains the one editable path (Task Information).
//
// No Q_OBJECT: no signals; MainWindow pushes values in when the selection or the
// model changes.
#pragma once

#include <QString>
#include <QWidget>

class QFormLayout;
class QLabel;
class QLineEdit;

namespace msqt {

// The values the Task Form shows.  `has_task` is false when nothing (or the
// synthetic project row) is selected; the form then clears and says so.
struct TaskFormValues {
    bool has_task = false;
    QString name;
    QString duration;
    QString start;
    QString finish;
    QString percent;
    QString predecessors;
    QString resources;
};

class TaskForm final : public QWidget {
  public:
    explicit TaskForm(QWidget* parent = nullptr);

    void set_values(const TaskFormValues& values);
    void clear();

    // The live fields, for the gate and for MainWindow's status line.
    QLineEdit* name_field() const noexcept { return name_; }
    QLineEdit* duration_field() const noexcept { return duration_; }
    QLineEdit* start_field() const noexcept { return start_; }
    QLineEdit* finish_field() const noexcept { return finish_; }
    QLineEdit* percent_field() const noexcept { return percent_; }
    QLineEdit* predecessors_field() const noexcept { return predecessors_; }
    QLineEdit* resources_field() const noexcept { return resources_; }
    QLabel* selection_label() const noexcept { return selection_; }

  private:
    QFormLayout* form_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* duration_ = nullptr;
    QLineEdit* start_ = nullptr;
    QLineEdit* finish_ = nullptr;
    QLineEdit* percent_ = nullptr;
    QLineEdit* predecessors_ = nullptr;
    QLineEdit* resources_ = nullptr;
    QLabel* selection_ = nullptr;
};

}  // namespace msqt
