// app_qt/tests/assign_resources_gate_test.cpp -- wave 34 (W34-4.3).
//
// THE RULE THIS GATES
//   Resource tab > Assign Resources is enabled for exactly the measured shapes:
//   one work resource at 100 % Max. Units assigned to an unstarted leaf task
//   that holds no assignment, and the removal of that one assignment, alone in
//   the save (tests/data/wave30-assign-add-control.mpp -> wave30-assign-add.mpp
//   and wave30-assign-delete-control.mpp -> wave30-assign-delete.mpp, Project's
//   own saves; the field-by-field pairs are app_edits_assignment).  Every other
//   shape is refused up front with the reason in the status line and queues
//   nothing.
//
// WHAT IT PROVES, in the real MainWindow
//   * Assign Resources is live and wired to RibbonCommand::assign_resources;
//   * with no task selected it opens no prompt and queues nothing;
//   * Cancel queues nothing; choosing the resource for the selected task queues
//     exactly one edit, the document is dirty and the displayed plan holds the
//     real assignment;
//   * a second click while the assignment is pending is refused (the edit is
//     exclusive) and the queue keeps the first;
//   * the real save writes it and a reopen reads the assignment back;
//   * on the port's own save the removal is refused up front (that file is not
//     the settled shape Project's removal pair started from) and queues nothing;
//   * on Project's own settled file (wave30-assign-delete-control.mpp) the same
//     button removes the assignment (a Yes / No prompt), the real save writes it
//     and a reopen reads the task's unassigned placeholder back.
//
// Usage: qt_assign_resources_gate <wave30-assign-add-control.mpp>
//                                 <wave30-assign-delete-control.mpp> <out-dir>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#include "edit_model.h"
#include "gate_util.h"
#include "main_window.h"
#include "menu_model.h"
#include "mpp_mutate.h"
#include "ribbon_bar.h"
#include "shell_style.h"

namespace {

// Finds the live Assignments-group control of the Resource tab.  The ribbon page
// is rebuilt by set_active_tab, so a pointer is never cached.
QAbstractButton* assign_button(msqt::MainWindow& window, msqt::RibbonCommand* command) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int resource_tab = ribbon->tab_titles().indexOf(QStringLiteral("Resource"));
    if (resource_tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(resource_tab);
    QApplication::processEvents();
    const int group = ribbon->group_titles().indexOf(QStringLiteral("Assignments"));
    if (group < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(group); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(group, control));
        if (button != nullptr && button->text().replace(QLatin1Char('\n'), QLatin1Char(' ')) ==
                                     QStringLiteral("Assign Resources")) {
            if (command != nullptr) {
                *command = ribbon->group_control_command(group, control);
            }
            return button;
        }
    }
    return nullptr;
}

struct Prompt {
    bool seen = false;
    bool question = false;
    QString title;
};

// Answers the next prompt: an item list takes `answer` (or Cancel), the removal
// question takes Yes (`accept`) or No.
void answer_prompt(Prompt* prompt, bool accept, const QString& answer) {
    QTimer::singleShot(0, [prompt, accept, answer]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible()) {
                continue;
            }
            if (auto* input = qobject_cast<QInputDialog*>(widget)) {
                prompt->seen = true;
                prompt->title = input->windowTitle();
                if (accept) {
                    input->setTextValue(answer);
                    input->accept();
                } else {
                    input->reject();
                }
                return;
            }
            if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                prompt->seen = true;
                prompt->question = true;
                prompt->title = box->windowTitle();
                QAbstractButton* button =
                    box->button(accept ? QMessageBox::Yes : QMessageBox::No);
                if (button != nullptr) {
                    button->click();
                } else {
                    box->reject();
                }
                return;
            }
        }
    });
}

bool click_assign(msqt::MainWindow& window, Prompt* prompt, bool accept, const QString& answer) {
    QAbstractButton* button = assign_button(window, nullptr);
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    answer_prompt(prompt, accept, answer);
    button->click();
    QApplication::processEvents();
    QApplication::processEvents();
    return true;
}

void select_task(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return;
    }
    for (std::size_t row = 0; row < sheet->row_count(); ++row) {
        if (sheet->row_uid(row) == uid) {
            const std::size_t column =
                window.sheet_table_model()->visible_column_for_field(app::name_field_id());
            sheet->select_cell(row, column == core::sheet::kNoIndex ? 0 : column);
            window.refresh_menu_state();
            QApplication::processEvents();
            return;
        }
    }
}

const app::AssignmentRow* task_assignment(const app::PlanInput& plan, std::uint32_t task,
                                          bool real) {
    for (const app::AssignmentRow& row : plan.assignments) {
        if (row.task_uid == task && (row.resource_uid != 0) == real) {
            return &row;
        }
    }
    return nullptr;
}

std::string resource_name(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::ResourceRow& row : plan.resources) {
        if (row.uid == uid) {
            return row.name;
        }
    }
    return std::string();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: qt_assign_resources_gate <add-control.mpp> <delete-control.mpp> "
                     "<out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString remove_fixture = QString::fromLocal8Bit(argv[2]);
    const QString out_dir = QString::fromLocal8Bit(argv[3]);
    gate::check(!app::unmeasured_paths_allowed_for_tests(),
                "the shipped refusal default is on in this program");
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed control wave30-assign-add-control.mpp is present");
    gate::check(std::filesystem::exists(remove_fixture.toStdString()),
                "the committed control wave30-assign-delete-control.mpp is present");
    if (!std::filesystem::exists(fixture.toStdString()) ||
        !std::filesystem::exists(remove_fixture.toStdString())) {
        return gate::finish();
    }

    const QString copy = out_dir + QStringLiteral("/w34-assign-add-control.mpp");
    const QString remove_copy = out_dir + QStringLiteral("/w34-assign-delete-control.mpp");
    const QString assigned = out_dir + QStringLiteral("/w34-assign-added.mpp");
    const QString removed = out_dir + QStringLiteral("/w34-assign-removed.mpp");
    std::error_code file_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the control copy is written under the build tree");
    std::filesystem::copy_file(remove_fixture.toStdString(), remove_copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the delete control copy is written under the build tree");
    std::filesystem::remove(assigned.toStdString(), file_error);
    std::filesystem::remove(removed.toStdString(), file_error);
    if (file_error) {
        return gate::finish();
    }
    const std::uint32_t task = 1;
    const std::uint32_t resource = 2;

    // --- 1. the control, the refusals that queue nothing, the measured add ----
    std::string name;
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control opens");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        msqt::RibbonCommand command = msqt::RibbonCommand::none;
        QAbstractButton* button = assign_button(window, &command);
        gate::check(button != nullptr, "the Resource tab's Assignments group carries Assign Resources");
        gate::check(button != nullptr && button->isEnabled(), "Assign Resources is live (W34-4.3)");
        gate::check(button != nullptr &&
                        button->toolTip() != QStringLiteral("not implemented in this port"),
                    "the live Assign Resources button does not carry the disabled tooltip");
        gate::check(command == msqt::RibbonCommand::assign_resources,
                    "Assign Resources is wired to RibbonCommand::assign_resources");
        name = resource_name(window.plan(), resource);
        gate::check(!name.empty(), "the control carries resource uid 2");
        gate::check(task_assignment(window.plan(), task, false) != nullptr &&
                        task_assignment(window.plan(), task, true) == nullptr,
                    "the control's task 1 holds only the unassigned placeholder");

        select_task(window, task);
        {
            Prompt prompt;
            gate::check(click_assign(window, &prompt, false, QString()), "Assign Resources clicks");
            gate::check(prompt.seen && prompt.title == QStringLiteral("Assign Resources"),
                        "the click opens the Assign Resources prompt");
            gate::check(!prompt.question, "an unassigned task is offered the resource list");
            gate::check(window.pending_edits().empty(), "Cancel queues nothing");
            gate::check(!window.document_dirty(), "Cancel leaves the document clean");
        }

        Prompt prompt;
        gate::check(click_assign(window, &prompt, true, QString::fromStdString(name)),
                    "Assign Resources clicks");
        gate::check(prompt.seen && !prompt.question, "the resource list opens");
        gate::check(window.pending_edits().size() == 1, "choosing a resource queues one edit");
        gate::check(window.document_dirty(), "the queued assignment dirties the document");
        gate::check(window.status_left_text().contains(QStringLiteral("queued an assignment")),
                    "the status line reports the queued assignment");
        const app::AssignmentRow* shown = task_assignment(window.plan(), task, true);
        gate::check(shown != nullptr && shown->resource_uid == resource &&
                        task_assignment(window.plan(), task, false) == nullptr,
                    "the displayed plan holds the real assignment instead of the placeholder");

        // A second click while the assignment is pending offers the removal; the
        // builder refuses it (the file still holds the placeholder) and the queue
        // keeps the first edit.
        {
            Prompt again;
            gate::check(click_assign(window, &again, true, QString()), "a second click clicks");
            gate::check(again.seen && again.question, "a held assignment offers the removal");
            gate::check(window.pending_edits().size() == 1, "a second click queues nothing");
            gate::check(window.status_left_text().contains(
                            QStringLiteral("assign resources: Removing an assignment is not implemented "
                                           "in this port for this project: assignment remove: the "
                                           "task's 0x0B4004E5 leaf does not name the resource")),
                        "the status line names the refused second edit");
            gate::check(task_assignment(window.plan(), task, true) != nullptr,
                        "the refused second edit leaves the display alone");
        }

        QString save_error;
        const bool ok = window.save_document_path(assigned, &save_error);
        gate::check(ok, "the assignment saves");
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
            return gate::finish();
        }
    }

    // --- 2. reopen: the port's own save is not a settled shape ---------------
    // Project's removal pair starts from a file Project saved after the
    // assignment (wave30-assign-delete-control.mpp); the port's save of the
    // assignment leaves the assignment FixedMeta in the freshly-appended state,
    // which is a different, unmeasured start.  The removal is refused up front.
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(assigned, &error);
        gate::check(opened, "the saved assignment reopens");
        if (!opened) {
            return gate::finish();
        }
        window.show();
        QApplication::processEvents();
        const app::AssignmentRow* back = task_assignment(window.plan(), task, true);
        gate::check(back != nullptr && back->resource_uid == resource &&
                        task_assignment(window.plan(), task, false) == nullptr,
                    "the reopened file holds the real assignment");
        select_task(window, task);
        Prompt prompt;
        gate::check(click_assign(window, &prompt, true, QString()), "the removal clicks");
        gate::check(prompt.seen && prompt.question, "an assigned task is offered the removal");
        gate::check(window.pending_edits().empty(),
                    "the removal on the port's own save queues nothing");
        gate::check(!window.document_dirty(), "the refused removal leaves the document clean");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("Removing an assignment is not implemented in this port "
                                       "for this project")),
                    "the status line names the refusal");
        gate::check(task_assignment(window.plan(), task, true) != nullptr,
                    "the display still shows the assignment");
    }

    // --- 3. Project's own settled file: the removal --------------------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(remove_copy, &error);
        gate::check(opened, "the delete control opens");
        if (!opened) {
            return gate::finish();
        }
        window.show();
        QApplication::processEvents();
        const app::AssignmentRow* held = task_assignment(window.plan(), task, true);
        gate::check(held != nullptr && held->resource_uid == resource,
                    "the delete control's task 1 holds the real assignment");
        select_task(window, task);
        {
            Prompt prompt;
            gate::check(click_assign(window, &prompt, false, QString()), "the removal clicks");
            gate::check(prompt.seen && prompt.question && prompt.title == QStringLiteral("Assign Resources"),
                        "an assigned task is offered the removal");
            gate::check(window.pending_edits().empty(), "answering No queues nothing");
            gate::check(!window.document_dirty(), "answering No leaves the document clean");
        }
        Prompt prompt;
        gate::check(click_assign(window, &prompt, true, QString()), "the removal clicks");
        gate::check(prompt.seen && prompt.question, "the removal is asked");
        gate::check(window.pending_edits().size() == 1, "answering Yes queues one edit");
        gate::check(window.document_dirty(), "the queued removal dirties the document");
        gate::check(window.status_left_text().contains(QStringLiteral("queued the removal")),
                    "the status line reports the queued removal");
        gate::check(task_assignment(window.plan(), task, true) == nullptr &&
                        task_assignment(window.plan(), task, false) != nullptr,
                    "the displayed plan shows the placeholder again");

        QString save_error;
        const bool ok = window.save_document_path(removed, &save_error);
        gate::check(ok, "the removal saves");
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
            return gate::finish();
        }
    }

    // --- 4. reopen: the placeholder is back ---------------------------------
    {
        msqt::MainWindow window;
        QString error;
        const bool opened = window.open_file(removed, &error);
        gate::check(opened, "the saved removal reopens");
        if (opened) {
            gate::check(task_assignment(window.plan(), task, true) == nullptr &&
                            task_assignment(window.plan(), task, false) != nullptr,
                        "the reopened file holds only the placeholder again");
        }
    }

    // --- 5. W35-0.1 / RW34b-3: a resource calendar the writer would not copy --
    // Each shape is Project's own control with one calendar edit; the click is
    // refused with the shape's own text, nothing is queued, and the file stays
    // clean.  Add runs on the add control, removal on the delete control.
    {
        struct Shape {
            const char* label;
            std::function<void(mppwrite::CfbDocument&)> edit;
            const char* needle;
        };
        const std::vector<Shape> shapes = {
            {"a resource calendar based on another calendar",
             [](mppwrite::CfbDocument& doc) {
                 const char* const path = "114/TBkndCal/FixedData";
                 mppmutate::Bytes rows = doc.read_stream(path);
                 for (std::size_t at = 64; at + 12 <= rows.size(); at += 12) {
                     if (mppmutate::get_u32(rows, at + 4) == 2u) {
                         mppmutate::put_u32(rows, at, 3);
                     }
                 }
                 doc.replace_stream(path, rows);
             },
             "whose calendar is based on another calendar"},
            {"a resource calendar with an exception",
             [](mppwrite::CfbDocument& doc) {
                 mppmutate::add_calendar_block(doc, 4, 0x0D400008u, 540);
             },
             "has working-time changes or exceptions"},
        };
        for (const Shape& shape : shapes) {
            for (const bool add : {true, false}) {
                const QString source = add ? copy : remove_copy;
                const QString target = out_dir + QStringLiteral("/w35-assign-shape.mpp");
                const std::string what = std::string(shape.label) + (add ? " (add)" : " (remove)");
                const bool made = mppmutate::write_edited_copy(source.toStdString(),
                                                               target.toStdString(), shape.edit);
                gate::check(made, ("the mutated control is written: " + what).c_str());
                msqt::MainWindow window;
                window.resize(msqt::kParityWidth, 800);
                QString error;
                if (!made || !window.open_file(target, &error)) {
                    gate::check(false, ("the mutated control opens: " + what).c_str());
                    continue;
                }
                window.show();
                QApplication::processEvents();
                select_task(window, task);
                Prompt prompt;
                gate::check(click_assign(window, &prompt, true, QString::fromStdString(name)),
                            ("the click runs: " + what).c_str());
                gate::check(window.pending_edits().empty(),
                            ("the refusal queues nothing: " + what).c_str());
                gate::check(!window.document_dirty(),
                            ("the refusal leaves the file clean: " + what).c_str());
                gate::check(window.status_left_text().contains(QString::fromUtf8(shape.needle)),
                            ("the status line names the shape: " + what).c_str());
            }
        }
    }
    return gate::finish();
}
