// app_qt/spelling_dialog.h -- the Spelling dialog (wave 30, W30-3.3).
//
// THE TARGET (wave 30 brief, G3): Spelling is a "read-only check over task
// names; no dictionary data from Project".  This dialog presents the result of
// app::spelling_issues() over the open plan: one row per task name that has a
// word the port's own word list does not know.  It is NOT a captured Project
// dialog (no capture of Project's Spelling window exists in this wave), so no
// measurement is claimed for its geometry or captions; it is a port-owned,
// read-only report.
//
// WHAT IT DOES NOT DO: it has no suggestion list, no Change/Change All/Ignore
// buttons and no dictionary edit.  Nothing in it can alter a task name, and it
// is opened with a const plan, so it can never dirty the document.  The note
// on the dialog says whose word list is used (the port's own) and that a
// finding is not a correction.
//
// No Q_OBJECT: the Close button connects through a lambda, so the shell stays
// moc-free.
#pragma once

#include <QDialog>
#include <QString>

#include <cstddef>
#include <vector>

#include "spelling.h"  // app::SpellingIssue

class QLabel;
class QPushButton;
class QTableWidget;

namespace msqt {

class SpellingDialog final : public QDialog {
  public:
    // `issues` and `names_checked` are the values MainWindow got from
    // app::spelling_issues()/spelling_names_checked() for the open plan.  The
    // dialog copies nothing from the document; it only renders them.
    SpellingDialog(const std::vector<app::SpellingIssue>& issues,
                   std::size_t names_checked, QWidget* parent = nullptr);

    // Live accessors for the gate.  The table is read-only by construction
    // (NoEditTriggers, non-editable items); the gate checks both.
    QString summary_text() const;
    QLabel* summary_label() const noexcept { return summary_; }
    QTableWidget* table() const noexcept { return table_; }
    QLabel* note_label() const noexcept { return note_; }
    QPushButton* close_button() const noexcept { return close_; }
    int issue_count() const noexcept;

  private:
    QLabel* summary_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* close_ = nullptr;
};

}  // namespace msqt
