// app_qt/tests/resource_add_gate_test.cpp -- wave 34 (W34-4.2).
//
// THE RULE THIS GATES
//   Resource tab > Add Resources (and Insert > Resource) is enabled for exactly
//   the measured shapes: one work resource with default rates and a printable-
//   ASCII name, added to a project that already holds one or two resources,
//   alone in the save (tests/data/wave30-resource-add-control.mpp ->
//   wave30-resource-add.mpp and wave34-resource-add-third-control.mpp ->
//   wave34-resource-add-third.mpp, Project's own saves; the field-by-field pair
//   is app_edits_resource_add).  Every other shape is refused up front with the
//   reason in the status line and queues nothing.
//
// WHAT IT PROVES, in the real MainWindow
//   * Add Resources is live and wired to MenuAction::insert_resource, and the
//     Insert menu's Resource entry is enabled;
//   * Cancel, a blank name and a non-ASCII name queue nothing;
//   * a name queues exactly one edit, the document is dirty, the plan and the
//     Resource Sheet show the new row;
//   * a second Add while the first is pending is refused (the edit is exclusive)
//     and the queue keeps the first;
//   * the real save writes it and a reopen reads the new resource back, twice
//     (the second and then the third resource);
//   * a project that already holds three resources is refused up front with
//     "not implemented in this port" and queues nothing.
//
// Usage: qt_resource_add_gate <wave30-resource-add-control.mpp> <out-dir>
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QInputDialog>
#include <QTableView>
#include <QTimer>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "edit_model.h"
#include "gate_util.h"
#include "main_window.h"
#include "menu_model.h"
#include "ribbon_bar.h"
#include "shell_style.h"

namespace {

// Finds the live Insert-group control of the Resource tab by its text.  The
// ribbon page is rebuilt by set_active_tab, so a pointer is never cached.
QAbstractButton* add_resources_button(msqt::MainWindow& window) {
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
    const int insert = ribbon->group_titles().indexOf(QStringLiteral("Insert"));
    if (insert < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(insert); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(insert, control));
        if (button != nullptr && button->text().replace(QLatin1Char('\n'), QLatin1Char(' ')) ==
                                     QStringLiteral("Add Resources")) {
            return button;
        }
    }
    return nullptr;
}

struct Prompt {
    bool seen = false;
    QString title;
};

void answer_prompt(Prompt* prompt, bool accept, const QString& answer) {
    QTimer::singleShot(0, [prompt, accept, answer]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* input = qobject_cast<QInputDialog*>(widget);
            if (input != nullptr && input->isVisible()) {
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
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog != nullptr && dialog->isVisible()) {
                prompt->seen = true;
                dialog->reject();
                return;
            }
        }
    });
}

bool click_add(msqt::MainWindow& window, Prompt* prompt, bool accept, const QString& answer) {
    QAbstractButton* button = add_resources_button(window);
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    answer_prompt(prompt, accept, answer);
    button->click();
    QApplication::processEvents();
    QApplication::processEvents();
    return true;
}

void trigger(msqt::MainWindow& window, app::MenuAction id) {
    window.refresh_menu_state();
    QAction* action = gate::find_action(window, id);
    gate::check(action != nullptr, "the menu carries the action");
    if (action != nullptr) {
        action->trigger();
        QApplication::processEvents();
        QApplication::processEvents();
    }
}

int column_titled(QAbstractItemModel* model, const QString& title) {
    for (int column = 0; column < model->columnCount(); ++column) {
        if (model->headerData(column, Qt::Horizontal).toString() == title) {
            return column;
        }
    }
    return -1;
}

const app::ResourceRow* resource_by_uid(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::ResourceRow& row : plan.resources) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

// gate::check takes a C string; the labelled checks below build std::string.
void checkq(bool ok, const std::string& what) {
    gate::check(ok, what.c_str());
}

// Adds `name` through the ribbon button, saves to `saved` and returns whether the
// save succeeded.  `expected_count` is the resource count the window shows before.
bool add_and_save(msqt::MainWindow& window, const QString& label, const QString& name,
                  std::uint32_t expected_uid, std::size_t expected_count, const QString& saved) {
    checkq(window.plan().resources.size() == expected_count,
                (label + ": the project shows the expected resource count").toStdString());
    Prompt prompt;
    checkq(click_add(window, &prompt, true, name),
                (label + ": Add Resources clicks").toStdString());
    checkq(prompt.seen && prompt.title == QStringLiteral("Add Resources"),
                (label + ": the click opens the Add Resources prompt").toStdString());
    checkq(window.pending_edits().size() == 1,
                (label + ": a name queues exactly one edit").toStdString());
    checkq(window.document_dirty(),
                (label + ": the queued add dirties the document").toStdString());
    checkq(window.status_left_text().contains(QStringLiteral("queued a new resource")),
                (label + ": the status line reports the queued resource").toStdString());
    const app::ResourceRow* now = resource_by_uid(window.plan(), expected_uid);
    checkq(window.plan().resources.size() == expected_count + 1 && now != nullptr &&
                    now->name == name.toStdString(),
                (label + ": the displayed plan carries the new resource").toStdString());

    // The Resource Sheet shows the row.
    trigger(window, app::MenuAction::view_resource_sheet);
    QTableView* view = window.sheet_view();
    QAbstractItemModel* model = view != nullptr ? view->model() : nullptr;
    const int name_column =
        model != nullptr ? column_titled(model, QStringLiteral("Resource Name")) : -1;
    checkq(model != nullptr && name_column >= 0 &&
                    model->rowCount() == static_cast<int>(expected_count + 1) &&
                    model->index(static_cast<int>(expected_count), name_column)
                            .data(Qt::DisplayRole)
                            .toString() == name,
                (label + ": the Resource Sheet shows the new row").toStdString());

    // A second Add while the first is pending is refused; the queue keeps one.
    {
        Prompt again;
        checkq(click_add(window, &again, true, name + QStringLiteral("2")),
                    (label + ": a second Add clicks").toStdString());
        checkq(window.pending_edits().size() == 1,
                    (label + ": a second Add queues nothing").toStdString());
        checkq(window.plan().resources.size() == expected_count + 1,
                    (label + ": the second Add leaves the display alone").toStdString());
        checkq(window.status_left_text().contains(
                   QStringLiteral("add resources: the project file no longer matches the open plan")),
                    (label + ": the status line names the refused second add").toStdString());
    }

    QString save_error;
    const bool ok = window.save_document_path(saved, &save_error);
    checkq(ok, (label + ": the add saves").toStdString());
    if (!ok) {
        std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
    }
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 3) {
        std::fprintf(stderr, "usage: qt_resource_add_gate <control.mpp> <out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);
    gate::check(!app::unmeasured_paths_allowed_for_tests(),
                "the shipped refusal default is on in this program");
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed control wave30-resource-add-control.mpp is present");
    if (!std::filesystem::exists(fixture.toStdString())) {
        return gate::finish();
    }

    const QString copy = out_dir + QStringLiteral("/w34-resource-add.mpp");
    const QString second = out_dir + QStringLiteral("/w34-resource-add-second.mpp");
    const QString third = out_dir + QStringLiteral("/w34-resource-add-third.mpp");
    std::error_code file_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the control copy is written under the build tree");
    std::filesystem::remove(second.toStdString(), file_error);
    std::filesystem::remove(third.toStdString(), file_error);
    if (file_error) {
        return gate::finish();
    }

    // --- 1. the controls and the refusals that queue nothing ---------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control opens");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        QAbstractButton* add = add_resources_button(window);
        gate::check(add != nullptr, "the Resource tab's Insert group carries Add Resources");
        gate::check(add != nullptr && add->isEnabled(), "Add Resources is live (W34-4.2)");
        gate::check(add != nullptr && add->toolTip() != QStringLiteral("not implemented in this port"),
                    "the live Add Resources button does not carry the disabled tooltip");
        window.refresh_menu_state();
        QAction* menu_action = gate::find_action(window, app::MenuAction::insert_resource);
        gate::check(menu_action != nullptr && menu_action->isEnabled(),
                    "Insert > Resource is enabled with a document");

        {
            Prompt prompt;
            gate::check(click_add(window, &prompt, false, QString()), "Add Resources clicks");
            gate::check(prompt.seen, "the click opens the Add Resources prompt");
            gate::check(window.pending_edits().empty(), "Cancel queues nothing");
            gate::check(!window.document_dirty(), "Cancel leaves the document clean");
        }
        {
            Prompt prompt;
            gate::check(click_add(window, &prompt, true, QStringLiteral("   ")), "Add clicks");
            gate::check(window.pending_edits().empty(), "a blank name queues nothing");
            gate::check(!window.document_dirty(), "the refused blank name leaves the file clean");
            gate::check(window.status_left_text().contains(
                            QStringLiteral("add resources: the resource name is empty")),
                        "the status line names the blank-name refusal");
        }
        {
            Prompt prompt;
            gate::check(click_add(window, &prompt, true, QString::fromUtf8("Caf\xC3\xA9")),
                        "Add clicks");
            gate::check(window.pending_edits().empty(), "a non-ASCII name queues nothing");
            gate::check(window.status_left_text().contains(
                            QStringLiteral("is not implemented in this port")),
                        "the status line names the non-ASCII refusal");
        }

        // --- 2. the measured shape: the second resource -------------------
        gate::check(add_and_save(window, QStringLiteral("second"), QStringLiteral("R2"), 2, 1,
                                 second),
                    "the second resource is added and saved");
    }

    // --- 3. reopen: the second resource is there; add the third ------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(second, &error);
        gate::check(opened, "the saved second-resource file reopens");
        if (!opened) {
            return gate::finish();
        }
        const app::ResourceRow* back = resource_by_uid(window.plan(), 2);
        gate::check(window.plan().resources.size() == 2 && back != nullptr && back->name == "R2",
                    "the reopened file carries the new resource R2");
        gate::check(add_and_save(window, QStringLiteral("third"), QStringLiteral("R3"), 3, 2,
                                 third),
                    "the third resource is added and saved");
    }

    // --- 4. reopen: three resources are not a measured shape --------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(third, &error);
        gate::check(opened, "the saved third-resource file reopens");
        if (!opened) {
            return gate::finish();
        }
        const app::ResourceRow* back = resource_by_uid(window.plan(), 3);
        gate::check(window.plan().resources.size() == 3 && back != nullptr && back->name == "R3",
                    "the reopened file carries the new resource R3");
        Prompt prompt;
        gate::check(click_add(window, &prompt, true, QStringLiteral("R4")), "Add clicks");
        gate::check(window.pending_edits().empty(), "a fourth resource queues nothing");
        gate::check(!window.document_dirty(), "the refused fourth resource leaves the file clean");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("is not implemented in this port")),
                    "the status line names the unmeasured-count refusal");
        gate::check(window.plan().resources.size() == 3, "the display still shows three resources");
    }
    return gate::finish();
}
