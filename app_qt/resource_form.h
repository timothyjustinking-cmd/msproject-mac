// app_qt/resource_form.h -- the Resource Form pane (W30-2.2), read-only.
//
// THE TARGET (wave 30 brief, G2): the Resource tab's Details opens Project's
// Resource Form -- the selected resource's stored fields in a docked form.
// The Resource Sheet is read-only in this port and there is no resource-field
// writer, so this widget is the VIEW half only: every value is read from
// app::PlanInput (the same plan the Resource Sheet and the usage views are
// built from) and nothing here can be edited, queued or saved.
//
// It shares the Details splitter with the Task Form (W26-6.3): MainWindow
// shows exactly one of them, for the view that owns it -- task_sheet shows the
// Task Form, resource_sheet shows the Resource Form.  The Details preference
// is one setting, so a view switch re-docks the form that belongs to the new
// view and a click in the Resource Sheet moves this pane to that row.
//
// Work and Assignments are derived from app::PlanInput::assignments for the
// resource's UID: the pane reports the plan's assignment facts rather than
// reading a second store.
#pragma once

#include <QString>
#include <QWidget>

class QGridLayout;
class QLabel;
class QLineEdit;

namespace msqt {

// One pane's worth of facts.  `has_resource` is false when nothing is selected
// (or the view has no resource selection); the pane then clears and says so.
struct ResourceFormValues {
    bool has_resource = false;
    QString name;
    QString id;
    QString uid;
    QString type;
    QString initials;
    QString max_units;
    QString work;
    QString assignments;
    QString selection;
};

class ResourceForm final : public QWidget {
  public:
    explicit ResourceForm(QWidget* parent = nullptr);

    void set_values(const ResourceFormValues& values);
    void clear();

    // The live fields, for the gate.  Every one is read-only; the gate checks
    // that through QLineEdit::isReadOnly().
    QLineEdit* name_field() const noexcept { return name_; }
    QLineEdit* id_field() const noexcept { return id_; }
    QLineEdit* uid_field() const noexcept { return uid_; }
    QLineEdit* type_field() const noexcept { return type_; }
    QLineEdit* initials_field() const noexcept { return initials_; }
    QLineEdit* max_units_field() const noexcept { return max_units_; }
    QLineEdit* work_field() const noexcept { return work_; }
    QLineEdit* assignments_field() const noexcept { return assignments_; }
    QLabel* selection_label() const noexcept { return selection_; }

  private:
    QGridLayout* grid_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* id_ = nullptr;
    QLineEdit* uid_ = nullptr;
    QLineEdit* type_ = nullptr;
    QLineEdit* initials_ = nullptr;
    QLineEdit* max_units_ = nullptr;
    QLineEdit* work_ = nullptr;
    QLineEdit* assignments_ = nullptr;
    QLabel* selection_ = nullptr;
};

}  // namespace msqt
