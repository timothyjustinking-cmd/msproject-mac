// app_qt/tests/resource_information_gate_test.cpp -- wave 34 (W34-4.1).
//
// THE RULE THIS GATES
//   Resource tab > Information is enabled for exactly one measured shape: a
//   rename of an unassigned resource to a longer printable-ASCII name, alone in
//   the save (tests/data/wave30-resource-rename-control.mpp ->
//   wave30-resource-rename.mpp, Project's own save; the field-by-field pair is
//   app_edits_resource_rename).  Every other shape is refused up front with the
//   reason in the status line and queues nothing.  Notes stays disabled with the
//   exact tooltip (the note payload embeds the saving machine's RichEdit
//   generator string, which the port cannot reproduce: spec/writers/note-richtext.md).
//
// WHAT IT PROVES, in the real MainWindow
//   * Information is live and wired to RibbonCommand::resource_information;
//     Notes is disabled and carries "not implemented in this port";
//   * from the Task Sheet, or with no resource selected, the click opens no
//     prompt and queues nothing, and says why;
//   * Cancel queues nothing; a shorter name and a same-size name are refused
//     with the builder's reason and queue nothing;
//   * a longer name queues exactly one edit, the document is dirty, the
//     Resource Sheet cell and the plan show the new name;
//   * the real save writes it and a reopen reads the new name back.
//
// Usage: qt_resource_information_gate <wave30-resource-rename-control.mpp> <out-dir>
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QInputDialog>
#include <QModelIndex>
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

// Finds a live Properties-group control of the Resource tab by its text.  The
// ribbon page is rebuilt by set_active_tab, so a pointer is never cached.
QAbstractButton* resource_properties_button(msqt::MainWindow& window, const QString& text,
                                            msqt::RibbonCommand* command) {
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
    const int properties = ribbon->group_titles().indexOf(QStringLiteral("Properties"));
    if (properties < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(properties); ++control) {
        auto* button =
            qobject_cast<QAbstractButton*>(ribbon->group_control(properties, control));
        if (button != nullptr && button->text() == text) {
            if (command != nullptr) {
                *command = ribbon->group_control_command(properties, control);
            }
            return button;
        }
    }
    return nullptr;
}

// What a click on Information opened: the prompt (if any) is answered with
// `answer` (or cancelled when `accept` is false) so the test cannot hang.
struct Prompt {
    bool seen = false;
    QString initial;
};

void answer_prompt(Prompt* prompt, bool accept, const QString& answer) {
    QTimer::singleShot(0, [prompt, accept, answer]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* input = qobject_cast<QInputDialog*>(widget);
            if (input != nullptr && input->isVisible()) {
                prompt->seen = true;
                prompt->initial = input->textValue();
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

bool click_information(msqt::MainWindow& window, Prompt* prompt, bool accept,
                       const QString& answer) {
    QAbstractButton* button = resource_properties_button(window, QStringLiteral("Information"),
                                                         nullptr);
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

const app::ResourceRow* resource_by_uid(const app::PlanInput& plan, std::uint32_t uid,
                                        int* index) {
    for (std::size_t i = 0; i < plan.resources.size(); ++i) {
        if (plan.resources[i].uid == uid) {
            if (index != nullptr) {
                *index = static_cast<int>(i);
            }
            return &plan.resources[i];
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 3) {
        std::fprintf(stderr, "usage: qt_resource_information_gate <control.mpp> <out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);
    gate::check(!app::unmeasured_paths_allowed_for_tests(),
                "the shipped refusal default is on in this program");
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed control wave30-resource-rename-control.mpp is present");
    if (!std::filesystem::exists(fixture.toStdString())) {
        return gate::finish();
    }

    const QString copy = out_dir + QStringLiteral("/w34-resource-information.mpp");
    const QString saved = out_dir + QStringLiteral("/w34-resource-information-saved.mpp");
    std::error_code copy_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, copy_error);
    gate::check(!copy_error, "the control copy is written under the build tree");
    std::filesystem::remove(saved.toStdString(), copy_error);
    if (copy_error) {
        return gate::finish();
    }

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, 800);
    QString error;
    gate::check(window.open_file(copy, &error), "the control opens");
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    int plan_index = -1;
    const app::ResourceRow* target = resource_by_uid(window.plan(), 2, &plan_index);
    gate::check(target != nullptr, "the control carries resource uid 2");
    if (target == nullptr) {
        return gate::finish();
    }
    const std::string original = target->name;

    // --- 1. the controls ---------------------------------------------------
    msqt::RibbonCommand command = msqt::RibbonCommand::none;
    QAbstractButton* information =
        resource_properties_button(window, QStringLiteral("Information"), &command);
    gate::check(information != nullptr, "the Resource tab's Properties group carries Information");
    gate::check(information != nullptr && information->isEnabled(),
                "Information is live (W34-4.1)");
    gate::check(command == msqt::RibbonCommand::resource_information,
                "Information is wired to RibbonCommand::resource_information");
    gate::check(information != nullptr && information->toolTip() == QStringLiteral("Information"),
                "the enabled Information button carries its live tooltip");
    QAbstractButton* notes = resource_properties_button(window, QStringLiteral("Notes"), nullptr);
    gate::check(notes != nullptr && !notes->isEnabled(), "Notes stays disabled");
    gate::check(notes != nullptr && notes->toolTip() == QStringLiteral("not implemented in this port"),
                "Notes carries the exact decision-9 tooltip");

    // --- 2. refused up front ----------------------------------------------
    {
        Prompt prompt;
        gate::check(click_information(window, &prompt, true, QStringLiteral("Anything")),
                    "Information clicks from the Task Sheet");
        gate::check(!prompt.seen, "the Task Sheet click opens no prompt");
        gate::check(window.pending_edits().empty(), "the Task Sheet click queues nothing");
        gate::check(window.status_left_text().contains(QStringLiteral("Resource Sheet view")),
                    "the status line says Information needs the Resource Sheet view");
    }
    trigger(window, app::MenuAction::view_resource_sheet);
    gate::check(window.view_mode() == app::ViewMode::resource_sheet,
                "the View menu switches to the Resource Sheet");
    QTableView* view = window.sheet_view();
    QAbstractItemModel* model = view != nullptr ? view->model() : nullptr;
    gate::check(model != nullptr && model->rowCount() > plan_index,
                "the Resource Sheet shows the resource's row");
    const int name_column =
        model != nullptr ? column_titled(model, QStringLiteral("Resource Name")) : -1;
    gate::check(name_column >= 0, "the Resource Sheet shows the Resource Name column");
    if (model == nullptr || name_column < 0 || model->rowCount() <= plan_index) {
        return gate::finish();
    }
    gate::check(model->index(plan_index, name_column).data(Qt::DisplayRole).toString() ==
                    QString::fromStdString(original),
                "the sheet row is the plan's resource uid 2");
    {
        Prompt prompt;
        view->setCurrentIndex(QModelIndex());
        QApplication::processEvents();
        gate::check(click_information(window, &prompt, true, QStringLiteral("Anything")),
                    "Information clicks with no resource selected");
        gate::check(!prompt.seen, "no selection opens no prompt");
        gate::check(window.pending_edits().empty(), "no selection queues nothing");
        gate::check(window.status_left_text().contains(QStringLiteral("select a resource")),
                    "the status line asks for a resource");
    }

    // --- 3. the prompt, Cancel and the refused names ------------------------
    view->setCurrentIndex(model->index(plan_index, name_column));
    QApplication::processEvents();
    {
        Prompt prompt;
        gate::check(click_information(window, &prompt, false, QString()), "Information clicks");
        gate::check(prompt.seen, "the click opens the Resource Information prompt");
        gate::check(prompt.initial == QString::fromStdString(original),
                    "the prompt starts with the resource's own name");
        gate::check(window.pending_edits().empty(), "Cancel queues nothing");
        gate::check(!window.document_dirty(), "Cancel leaves the document clean");
    }
    {
        Prompt prompt;
        const QString shorter = QString::fromStdString(original).left(1);
        gate::check(click_information(window, &prompt, true, shorter), "Information clicks");
        gate::check(prompt.seen, "the prompt opens for the shorter name");
        gate::check(window.pending_edits().empty(), "a shorter name queues nothing");
        gate::check(!window.document_dirty(), "the refused shorter name leaves the document clean");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("is not implemented in this port")),
                    "the status line names the shorter-name refusal");
    }
    {
        Prompt prompt;
        QString same = QString::fromStdString(original);
        same[same.size() - 1] = same.at(same.size() - 1) == QLatin1Char('Z') ? QLatin1Char('Y')
                                                                              : QLatin1Char('Z');
        gate::check(click_information(window, &prompt, true, same), "Information clicks");
        gate::check(window.pending_edits().empty(), "a same-size name queues nothing");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("is not implemented in this port")),
                    "the status line names the same-size refusal");
    }
    {
        Prompt prompt;
        gate::check(click_information(window, &prompt, true,
                                      QString::fromStdString(original) +
                                          QString::fromUtf8(" \xC3\xA9")),
                    "Information clicks");
        gate::check(window.pending_edits().empty(), "a non-ASCII name queues nothing");
        gate::check(!window.document_dirty(), "the refused non-ASCII name leaves the file clean");
    }

    // --- 4. the measured shape: a longer name -------------------------------
    const QString renamed = QString::fromStdString(original) + QStringLiteral(" Renamed");
    {
        Prompt prompt;
        gate::check(click_information(window, &prompt, true, renamed), "Information clicks");
        gate::check(prompt.seen, "the prompt opens for the longer name");
        gate::check(!window.pending_edits().empty(), "a longer name queues an edit");
        gate::check(window.document_dirty(), "the queued rename dirties the document");
        gate::check(window.status_left_text().contains(QStringLiteral("queued rename")),
                    "the status line reports the queued rename");
        const app::ResourceRow* now = resource_by_uid(window.plan(), 2, nullptr);
        gate::check(now != nullptr && now->name == renamed.toStdString(),
                    "the displayed plan carries the new name");
        QAbstractItemModel* after = window.sheet_view()->model();
        gate::check(after != nullptr && after->index(plan_index, name_column)
                                                .data(Qt::DisplayRole)
                                                .toString() == renamed,
                    "the Resource Sheet cell shows the new name");
    }

    // --- 5. the real save and an independent reopen ------------------------
    {
        QString save_error;
        const bool ok = window.save_document_path(saved, &save_error);
        gate::check(ok, "the rename saves");
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
        }
        msqt::MainWindow reopened;
        QString reopen_error;
        gate::check(ok && reopened.open_file(saved, &reopen_error), "the saved file reopens");
        const app::ResourceRow* back = resource_by_uid(reopened.plan(), 2, nullptr);
        gate::check(back != nullptr && back->name == renamed.toStdString(),
                    "the reopened file carries the new name");
    }
    return gate::finish();
}
