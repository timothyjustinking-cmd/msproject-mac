// app_qt/task_form.h -- the Task Form pane of the split view (W26-6.3), editable
// since wave 28 (W28-4.1).
//
// Operator decision 16 (docs/ui-parity-spec.md): View > Details docks a task
// form at the bottom, synchronised with the selection, in a resizable splitter.
// This widget is that pane.  It shows the selected task's measured fields --
// Name, Duration, Start, Finish, % Complete, Predecessors and Resources (the
// last two are the same computed texts the stored table shows).
//
// W28-4.1: the fields edit through the SAME model and save paths the sheet
// uses; this widget only reports which field was committed and with what text.
// MainWindow hands the value to app::MainWindow::apply_task_information, the
// one editable path the sheet cells and Task Information also use (a
// begin_edit/commit_edit sheet session for Name/Start/Finish/Duration, and the
// measured set_percent structural edit for % Complete).  The form adds no write
// path of its own.  Predecessors and Resources are disabled: the sheet declares
// their columns read-only and the port has no field-level link/assignment
// writer, so a typed value would be dropped by a save.
//
// No Q_OBJECT: no custom signals; MainWindow pushes values in and sets the
// commit callback.
#pragma once

#include <QString>
#include <QWidget>

#include <functional>

class QFormLayout;
class QLabel;
class QLineEdit;

namespace msqt {

// The fields the form reports on commit.  Predecessors and Resources are not
// listed: they never commit (no sheet edit path).
enum class TaskFormField { name, duration, start, finish, percent };

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
    // W28-4.1: the field a commit is for and the text the field holds.  Called
    // on Return, or on focus-out while the form still shows the same task;
    // MainWindow decides whether the value changed and applies it through the
    // shared edit path, reporting any refusal in the status line.
    using CommitHandler = std::function<void(TaskFormField field, const QString& text)>;

    explicit TaskForm(QWidget* parent = nullptr);

    void set_values(const TaskFormValues& values);
    void clear();

    void set_commit_handler(CommitHandler handler);

    // A field that edits through the sheet's own cell session is enabled
    // exactly when the file's table shows an editable column for it (the same
    // lookup apply_task_information does).  Percent keeps its own structural
    // path and needs no column; Predecessors and Resources are never enabled.
    void set_field_enabled(TaskFormField field, bool enabled);

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
    QLineEdit* field(TaskFormField which) const noexcept;

    QFormLayout* form_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* duration_ = nullptr;
    QLineEdit* start_ = nullptr;
    QLineEdit* finish_ = nullptr;
    QLineEdit* percent_ = nullptr;
    QLineEdit* predecessors_ = nullptr;
    QLineEdit* resources_ = nullptr;
    QLabel* selection_ = nullptr;
    CommitHandler commit_;
};

}  // namespace msqt
