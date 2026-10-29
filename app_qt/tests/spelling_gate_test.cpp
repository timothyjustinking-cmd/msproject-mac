// app_qt/tests/spelling_gate_test.cpp -- wave 30 (W30-3.3).
//
// THE TARGET (wave 30 brief, G3): Spelling is a "read-only check over task
// names; no dictionary data from Project".  This gate proves the port's
// Spelling command end to end:
//   * the Project tab / Proofing / Spelling button is enabled, carries
//     RibbonCommand::spelling and is NOT a decision-9 disabled control;
//   * clicking it opens the Spelling dialog, whose summary, headers and rows
//     are app::spelling_issues() for the OPEN PLAN -- the expected rows are
//     recomputed here from MainWindow::plan(), never copied from the dialog;
//   * the dialog is read-only: the table has no edit trigger, every item is
//     non-editable, and Close is the only button;
//   * opening and closing it never dirties the document (the command is a
//     view, not a file write);
//   * a pending Insert Task row is checked too: its typed name appears with
//     its unknown words, while the document's dirty state is unchanged by the
//     spelling command;
//   * with no document open the button opens no dialog.
//
// THE FIXTURE: tests/data/wave23-outline.mpp -- six synthetic task names, so
// the plan-derived rows exercise the real checker without a hard-coded name.
//
// Usage: qt_spelling_gate <fixture.mpp>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "gate_util.h"
#include "main_window.h"
#include "ribbon_bar.h"
#include "spelling.h"
#include "spelling_dialog.h"

namespace {

using msqt::SpellingDialog;

QAbstractButton* spelling_button(msqt::MainWindow& window) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int project_tab = ribbon->tab_titles().indexOf(QStringLiteral("Project"));
    if (project_tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(project_tab);
    QApplication::processEvents();
    const QStringList groups = ribbon->group_titles();
    const int proofing = groups.indexOf(QStringLiteral("Proofing"));
    if (proofing < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(proofing); ++control) {
        auto* button =
            qobject_cast<QAbstractButton*>(ribbon->group_control(proofing, control));
        if (button != nullptr && button->text() == QStringLiteral("Spelling")) {
            return button;
        }
    }
    return nullptr;
}

struct DialogSeen {
    bool seen = false;
    QString title;
    QString summary;
    QString note;
    QStringList headers;
    int rows = -1;
    bool no_edit_triggers = false;
    bool all_items_read_only = false;
    bool close_live = false;
    int buttons = 0;
    std::vector<QString> ids;
    std::vector<QString> names;
    std::vector<QString> words;
};

// Click the live button and capture the modal from the event loop it starts.
// The capture runs in the dialog's own exec(), then Close dismisses it.
bool open_spelling(msqt::MainWindow& window, DialogSeen* seen) {
    QAbstractButton* button = spelling_button(window);
    gate::check(button != nullptr && button->isEnabled(),
                "the Project tab's Spelling button is live");
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    QTimer::singleShot(0, [seen]() {
        QWidget* modal = QApplication::activeModalWidget();
        auto* dialog = dynamic_cast<SpellingDialog*>(modal);
        if (dialog == nullptr) {
            if (modal != nullptr) {
                modal->close();
            }
            return;
        }
        seen->seen = true;
        seen->title = dialog->windowTitle();
        seen->summary = dialog->summary_text();
        seen->note = dialog->note_label() != nullptr ? dialog->note_label()->text()
                                                     : QString();
        QTableWidget* table = dialog->table();
        if (table != nullptr) {
            seen->rows = table->rowCount();
            seen->no_edit_triggers =
                table->editTriggers() == QAbstractItemView::NoEditTriggers;
            bool read_only = true;
            seen->ids.reserve(static_cast<std::size_t>(table->rowCount()));
            seen->names.reserve(static_cast<std::size_t>(table->rowCount()));
            seen->words.reserve(static_cast<std::size_t>(table->rowCount()));
            for (const QString& header : {QStringLiteral("ID"), QStringLiteral("Task name"),
                                          QStringLiteral("Not in the port's word list")}) {
                for (int column = 0; column < table->columnCount(); ++column) {
                    if (table->horizontalHeaderItem(column) != nullptr &&
                        table->horizontalHeaderItem(column)->text() == header) {
                        seen->headers.append(header);
                    }
                }
            }
            for (int row = 0; row < table->rowCount(); ++row) {
                QString cells[3];
                for (int column = 0; column < 3; ++column) {
                    QTableWidgetItem* item = table->item(row, column);
                    if (item == nullptr) {
                        read_only = false;
                        continue;
                    }
                    if (item->flags() & Qt::ItemIsEditable) {
                        read_only = false;
                    }
                    cells[column] = item->text();
                }
                seen->ids.push_back(cells[0]);
                seen->names.push_back(cells[1]);
                seen->words.push_back(cells[2]);
            }
            seen->all_items_read_only = read_only;
        }
        for (QWidget* widget : dialog->findChildren<QWidget*>()) {
            if (qobject_cast<QPushButton*>(widget) != nullptr) {
                ++seen->buttons;
            }
        }
        seen->close_live = dialog->close_button() != nullptr &&
                           dialog->close_button()->isEnabled();
        dialog->close_button()->click();
    });
    button->click();
    QApplication::processEvents();
    return seen->seen;
}

QString join(const std::vector<std::string>& words) {
    QStringList list;
    for (const std::string& word : words) {
        list.append(QString::fromStdString(word));
    }
    return list.join(QStringLiteral(", "));
}

std::vector<app::SpellingIssue> expected_issues(msqt::MainWindow& window) {
    return app::spelling_issues(window.plan());
}

// The dialog's every row is the plan-derived issue at the same position.
void check_rows(const DialogSeen& seen, const std::vector<app::SpellingIssue>& want) {
    gate::check(seen.rows == static_cast<int>(want.size()),
                "the dialog lists one row per task name with an unknown word");
    if (seen.rows != static_cast<int>(want.size())) {
        return;
    }
    bool ids_ok = true;
    bool names_ok = true;
    bool words_ok = true;
    for (std::size_t row = 0; row < want.size(); ++row) {
        const auto index = static_cast<std::size_t>(row);
        if (seen.ids[index] != QString::number(want[row].id)) {
            ids_ok = false;
        }
        if (seen.names[index] != QString::fromStdString(want[row].name)) {
            names_ok = false;
        }
        if (seen.words[index] != join(want[row].unknown_words)) {
            words_ok = false;
        }
    }
    gate::check(ids_ok, "each row's ID is the task's ID");
    gate::check(names_ok, "each row's Name is the task's name as stored");
    gate::check(words_ok, "each row's words are the plan check's unknown words");
}

void check_summary(const DialogSeen& seen, std::size_t checked,
                   const std::vector<app::SpellingIssue>& want) {
    if (checked == 0) {
        gate::check(seen.summary.contains(QStringLiteral("No task names to check")),
                    "the summary says there is nothing to check");
        return;
    }
    gate::check(seen.summary.contains(QString::number(static_cast<qulonglong>(checked))),
                "the summary reports how many task names were checked");
    if (want.empty()) {
        gate::check(seen.summary.contains(QStringLiteral("no words outside")),
                    "a clean plan's summary says no word was outside the list");
    } else {
        gate::check(seen.summary.contains(QString::number(
                        static_cast<qulonglong>(want.size())) +
                        QStringLiteral(" of ")),
                    "the summary reports how many task names have findings");
        gate::check(seen.summary.contains(QStringLiteral("words outside the port's "
                                                        "word list")),
                    "the summary names whose word list the check used");
    }
}

void check_dialog_chrome(const DialogSeen& seen) {
    gate::check(seen.seen, "the Spelling button opens the Spelling dialog");
    gate::check(seen.title == QStringLiteral("Spelling"),
                "the dialog is titled Spelling");
    gate::check(seen.headers.size() == 3, "the table carries the three column captions");
    gate::check(seen.no_edit_triggers,
                "the table has no edit trigger: it is a report, not an editor");
    gate::check(seen.all_items_read_only,
                "every table item is non-editable");
    gate::check(seen.buttons == 1, "Close is the dialog's only button");
    gate::check(seen.close_live, "the Close button is live");
    gate::check(seen.note.contains(QStringLiteral("Read-only")),
                "the dialog says the check is read-only");
    gate::check(seen.note.contains(QStringLiteral("not Microsoft Project's dictionary")),
                "the dialog says it does not use Project's dictionary");
    gate::check(seen.note.contains(QString::number(
                    static_cast<qulonglong>(app::spelling_word_count()))),
                "the dialog reports the port's own word-list size");
}

struct InputDialogResult {
    bool seen = false;
};

void accept_input_dialog(InputDialogResult* seen, const QString& text) {
    QTimer::singleShot(0, [seen, text]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QInputDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible()) {
                continue;
            }
            seen->seen = true;
            dialog->setTextValue(text);
            dialog->accept();
            return;
        }
    });
}

void trigger(msqt::MainWindow& window, app::MenuAction id) {
    window.refresh_menu_state();
    QAction* action = gate::find_action(window, id);
    gate::check(action != nullptr, "the menu carries the action");
    if (action != nullptr) {
        action->trigger();
        QApplication::processEvents();
    }
}

void test_no_document() {
    msqt::MainWindow window;
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();
    QAbstractButton* button = spelling_button(window);
    gate::check(button != nullptr && button->isEnabled(),
                "the Spelling button is enabled with no document (it explains itself)");
    if (button != nullptr) {
        button->click();
        QApplication::processEvents();
    }
    gate::check(QApplication::activeModalWidget() == nullptr,
                "with no document open Spelling opens no dialog");
}

void test_fixture(const QString& fixture) {
    msqt::MainWindow window;
    window.resize(1400, 900);
    window.show();
    QString error;
    gate::check(window.open_file(fixture, &error),
                "the fixture opens for the Spelling gate");
    QApplication::processEvents();
    gate::check(!window.document_dirty(),
                "the loaded document starts clean");

    DialogSeen seen;
    const bool opened = open_spelling(window, &seen);
    gate::check(opened, "the Spelling dialog opened from the ribbon");
    check_dialog_chrome(seen);
    const std::vector<app::SpellingIssue> want = expected_issues(window);
    check_rows(seen, want);
    check_summary(seen, app::spelling_names_checked(window.plan()), want);
    gate::check(!window.document_dirty(),
                "opening and closing Spelling leaves the document clean");

    // The dialog's summary and rows are the plan's, not a copy of a stale
    // array: a second open on the same document must agree exactly.
    DialogSeen again;
    gate::check(open_spelling(window, &again), "Spelling opens a second time");
    check_rows(again, want);
    gate::check(!window.document_dirty(),
                "the second spelling check leaves the document clean");
}

void test_pending_insert(const QString& fixture) {
    // W33-1.2 (A-9): Insert Task clones the file's LAST live saved row, and
    // wave23-outline's last row carries a real resource assignment, so the
    // command is now (correctly) refused there.  This window exists to check
    // the pending row's NAME, so it opens a file whose last row is Project's
    // clean placeholder row; synthetic-base sits beside the gate fixture and
    // is the committed fixture of that shape.
    const QString clean_fixture =
        QFileInfo(fixture).absolutePath() + QStringLiteral("/synthetic-base.mpp");
    msqt::MainWindow window;
    window.resize(1400, 900);
    window.show();
    QString error;
    gate::check(window.open_file(clean_fixture, &error),
                "the fixture opens in the pending-insert window");
    QApplication::processEvents();
    gate::check(window.sheet() != nullptr, "the pending-insert window has a sheet");
    InputDialogResult input;
    accept_input_dialog(&input, QStringLiteral("Speling Chek"));
    trigger(window, app::MenuAction::insert_task);
    gate::check(input.seen, "Insert Task asks for a name");
    QApplication::processEvents();

    const bool dirty_before = window.document_dirty();
    DialogSeen seen;
    gate::check(open_spelling(window, &seen),
                "Spelling opens with a pending Insert Task row");
    gate::check(window.document_dirty() == dirty_before,
                "the spelling command does not change the document's dirty state");
    gate::check(window.pending_start_edits() == 0,
                "the spelling command stages no start edit");

    int pending_row = -1;
    for (std::size_t row = 0; row < seen.names.size(); ++row) {
        if (seen.names[row] == QStringLiteral("Speling Chek")) {
            pending_row = static_cast<int>(row);
        }
    }
    gate::check(pending_row >= 0,
                "the pending row's typed name is checked and listed");
    // The plan-based check is the dialog's source: the pending row is in the
    // display plan and must be the same row this gate found on screen.
    const std::vector<app::SpellingIssue> want = expected_issues(window);
    bool pending_in_plan = false;
    for (const app::SpellingIssue& issue : want) {
        if (issue.name == "Speling Chek") {
            pending_in_plan = true;
        }
    }
    gate::check(pending_in_plan, "the display plan carries the pending row's name");
    check_rows(seen, want);
    if (pending_row >= 0) {
        gate::check(seen.words[static_cast<std::size_t>(pending_row)] ==
                        QStringLiteral("Speling, Chek"),
                    "the pending name's unknown words are reported in order");
        gate::check(seen.summary.contains(QString::number(
                        static_cast<qulonglong>(want.size())) +
                        QStringLiteral(" of ")),
                    "the summary counts the task names with findings");
        gate::check(want.size() == 1
                        ? seen.summary.contains(QStringLiteral(" has words outside"))
                        : seen.summary.contains(QStringLiteral(" have words outside")),
                    "the summary's singular/plural form follows the finding count");
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_spelling_gate_tests <fixture.mpp>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);

    test_no_document();
    test_fixture(fixture);
    test_pending_insert(fixture);

    std::printf("qt_spelling_gate: ");
    return gate::finish();
}
