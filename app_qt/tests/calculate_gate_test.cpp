// app_qt/tests/calculate_gate_test.cpp -- wave 35 (W35-1.3).
//
// THE RULE THIS GATES
//   Project > Schedule > Calculate Project is enabled for exactly the measured
//   shape: the project row plus two unlinked, automatically scheduled leaves
//   whose recalculation is PENDING, alone in the save
//   (tests/data/wave35-calculate-unlinked-control.mpp ->
//   wave35-calculate-unlinked.mpp, Project's own saves; the field-by-field pair
//   is app_edits_calculate).  Every other project is refused up front with the
//   decision-9 reason in the status line and queues nothing.
//
// WHAT IT PROVES, in the real MainWindow
//   * the Calculate Project button is live and carries a tooltip other than the
//     disabled one;
//   * on a linked project (wave35-calculate-linked-control.mpp) it queues
//     nothing, leaves the document clean and names the refusal;
//   * on the measured control it queues exactly one edit and dirties the
//     document; a second click while it is pending is refused (queue stays 1);
//   * the real save writes it (the file differs from the control) and a reopen
//     of the saved file refuses a second calculation (nothing is pending).
//
//   * (W35b-2.6) the saved file equals Project's own save of the pair stream by stream under the
//     null-save discipline (tests/save_pair_compare.h), not merely "differs from the control".
//
// Usage: qt_calculate_gate <unlinked-control.mpp> <linked-control.mpp> <out-dir>
//                          <project-result.mpp> <control-null-save.mpp>
#include <QAbstractButton>
#include <QApplication>

#include <array>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "cfb_writer.h"
#include "dialogs.h"
#include "edit_model.h"
#include "entry_columns.h"
#include "fixture_guard.h"
#include "gate_util.h"
#include "main_window.h"
#include "ribbon_bar.h"
#include "save_pair_compare.h"
#include "sheet_table_model.h"
#include "shell_style.h"

namespace {

// The Calculate Project button of Project > Schedule.  The ribbon page is
// rebuilt by set_active_tab, so a pointer is never cached.
QAbstractButton* calculate_button(msqt::MainWindow& window) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int tab = ribbon->tab_titles().indexOf(QStringLiteral("Project"));
    if (tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(tab);
    QApplication::processEvents();
    const int group = ribbon->group_titles().indexOf(QStringLiteral("Schedule"));
    if (group < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(group); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(group, control));
        if (button == nullptr) {
            continue;
        }
        QString text = button->text().isEmpty() ? button->accessibleName() : button->text();
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (text.simplified() == QStringLiteral("Calculate Project")) {
            return button;
        }
    }
    return nullptr;
}

bool click_calculate(msqt::MainWindow& window) {
    QAbstractButton* button = calculate_button(window);
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    button->click();
    QApplication::processEvents();
    QApplication::processEvents();
    return true;
}

struct PairContext {
    void check(bool ok, const std::string& what) const { gate::check(ok, what.c_str()); }
    void expected_failure(bool unexpected_pass, const std::string& what) const {
        gate::check(false, ("unexpected KnownGap report: " + what +
                            (unexpected_pass ? " (gap gone)" : ""))
                               .c_str());
    }
};

std::vector<char> read_all(const QString& path) {
    std::ifstream in(path.toStdString(), std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 6) {
        std::fprintf(stderr,
                     "usage: qt_calculate_gate <unlinked-control.mpp> <linked-control.mpp> "
                     "<out-dir> <project-result.mpp> <control-null-save.mpp>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString other_fixture = QString::fromLocal8Bit(argv[2]);
    const QString out_dir = QString::fromLocal8Bit(argv[3]);
    const std::string project_result = argv[4];
    const std::string null_save_path = argv[5];
    gate::check(!app::unmeasured_paths_allowed_for_tests(),
                "the shipped refusal default is on in this program");
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed control wave35-calculate-unlinked-control.mpp is present");
    gate::check(std::filesystem::exists(other_fixture.toStdString()),
                "the committed linked control is present");
    if (!std::filesystem::exists(fixture.toStdString()) ||
        !std::filesystem::exists(other_fixture.toStdString())) {
        return gate::finish();
    }
    const QString copy = out_dir + QStringLiteral("/w35-calculate-control.mpp");
    const QString other_copy = out_dir + QStringLiteral("/w35-calculate-linked.mpp");
    const QString calculated = out_dir + QStringLiteral("/w35-calculate-saved.mpp");
    std::error_code file_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the control copy is written under the build tree");
    std::filesystem::copy_file(other_fixture.toStdString(), other_copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the linked copy is written under the build tree");
    std::filesystem::remove(calculated.toStdString(), file_error);
    if (file_error) {
        return gate::finish();
    }

    // --- 1. the button, the refusal that queues nothing ----------------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(other_copy, &error), "the linked project opens");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        {
            QAbstractButton* button = calculate_button(window);
            gate::check(button != nullptr && button->isEnabled(),
                        "Calculate Project is live (W35-1.3)");
            gate::check(button != nullptr &&
                            button->toolTip() != QStringLiteral("not implemented in this port"),
                        "the live button does not carry the disabled tooltip");
        }
        gate::check(click_calculate(window), "Calculate Project clicks on a linked project");
        gate::check(window.pending_edits().empty(), "a linked project queues nothing");
        gate::check(!window.document_dirty(), "the refusal leaves the document clean");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("Calculate Project is not implemented in this port")),
                    "the status line names the refusal");
        // W35b-2.5 (RW35-R5): the prefix is on every refusal; the plan-side link gate is the one
        // that must fire for a linked project.
        gate::check(window.status_left_text().contains(
                        QStringLiteral("for a project with links")),
                    "the status line names the link gate, not another refusal");
    }

    // --- 2. the measured control, the save -----------------------------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control opens");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        gate::check(click_calculate(window), "Calculate Project clicks on the control");
        gate::check(window.pending_edits().size() == 1, "Calculate Project queues one edit");
        gate::check(window.document_dirty(), "the queued calculation dirties the document");
        gate::check(window.status_left_text().contains(QStringLiteral("queued Calculate Project")),
                    "the status line reports the queued calculation");

        // A second click while it is pending: the edit is exclusive.
        gate::check(click_calculate(window), "a second Calculate Project clicks");
        gate::check(window.pending_edits().size() == 1, "a second click keeps the queue at one");

        // W35d-2.11 (R35cW-4): a grid edit made AFTER Calculate Project is queued is refused with
        // the queued Calculate named, the same guard as a queued Show Level or Sort; the queue
        // stays at the one Calculate edit and the document keeps its values.
        {
            const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
            const int duration_column = msqt::entry_column_index(msqt::EntryCellKind::duration);
            msqt::SheetTableModel* model = window.sheet_table_model();
            gate::check(model != nullptr && name_column >= 0 && duration_column >= 0,
                        "the Entry table has Name and Duration");
            if (model != nullptr && name_column >= 0 && duration_column >= 0) {
                const QString name_before = model->data(model->index(1, name_column)).toString();
                gate::check(!model->setData(model->index(1, name_column),
                                            QStringLiteral("Renamed after the calculation"),
                                            Qt::EditRole),
                            "a rename after a queued Calculate Project is refused");
                gate::check(model->data(model->index(1, name_column)).toString() == name_before,
                            "the refused rename leaves the name as it was");
                gate::check(window.status_left_text().contains(QStringLiteral(
                                "a Calculate Project is queued and File > Save would write it, "
                                "not an edited cell")),
                            "a rename after a queued Calculate Project: the status line names it");
                gate::check(!model->setData(model->index(1, duration_column),
                                            QStringLiteral("9d"), Qt::EditRole),
                            "a Duration edit after a queued Calculate Project is refused");
                gate::check(window.pending_edits().size() == 1,
                            "the refused edits leave the one queued Calculate Project");
            }
        }

        QString save_error;
        const bool ok = window.save_document_path(calculated, &save_error);
        gate::check(ok, "the calculation saves");
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
            return gate::finish();
        }
        gate::check(read_all(calculated) != read_all(copy),
                    "the saved file differs from the control");

        // W35b-2.6: "differs from the control" holds for any write, so the save is compared
        // with Project's own pair, every stream under the null-save discipline (as
        // app_edits_calculate does for the cascade).  A wrong leaf stamp, a missing record or a
        // stray byte fails here.
        try {
            fixture::require_committed(project_result);
            fixture::require_committed(null_save_path);
            const mppwrite::CfbDocument control_doc =
                mppwrite::CfbDocument::from_file(copy.toStdString());
            const mppwrite::CfbDocument null_doc =
                mppwrite::CfbDocument::from_file(null_save_path);
            const mppwrite::CfbDocument project_doc =
                mppwrite::CfbDocument::from_file(project_result);
            const mppwrite::CfbDocument saved_doc =
                mppwrite::CfbDocument::from_file(calculated.toStdString());
            PairContext context;
            const std::array<const char*, 8> declared{
                {"114/Props", "114/TBkndTask/FixedData", "114/TBkndTask/FixedMeta",
                 "114/TBkndTask/Fixed2Data", "114/TBkndTask/Var2Data", "114/TBkndTask/VarMeta",
                 "\005DocumentSummaryInformation", "Props14"}};
            save_pair::compare_all(context, "qt calculate", control_doc, null_doc, project_doc,
                                   saved_doc, nullptr, 0, declared.data(), declared.size());
        } catch (const std::exception& pair_error) {
            std::fprintf(stderr, "note: pair comparison error: %s\n", pair_error.what());
            gate::check(false, "the saved file is compared with Project's pair");
        }
    }

    // --- 2b. W35e-0.1 (R35dG0-1, R35dW-1): a partly applied Task Information edit ----------
    // Calculate Project, Ctrl+Z (nothing queued, the cell gate open), a Name that commits and a
    // Duration that does not parse, Ctrl+Y.  The Name stays committed, and left on the structural
    // redo list Redo re-queued the withdrawn Calculate over the changed task without push().
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control reopens for the partial apply");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();
        QAction* undo = gate::find_action(window, app::MenuAction::undo);
        QAction* redo = gate::find_action(window, app::MenuAction::redo);
        const int name_column = msqt::entry_column_index(msqt::EntryCellKind::name);
        msqt::SheetTableModel* model = window.sheet_table_model();
        gate::check(undo != nullptr && redo != nullptr && name_column >= 0 && model != nullptr,
                    "Undo, Redo and a Name column exist (partial apply)");
        if (undo != nullptr && redo != nullptr && name_column >= 0 && model != nullptr) {
            gate::check(click_calculate(window) && window.pending_edits().size() == 1,
                        "partial apply: Calculate Project is queued");
            undo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty() && redo->isEnabled(),
                        "partial apply: Undo withdraws Calculate Project onto the redo list");
            window.sheet()->select_cell(1, 0);
            msqt::TaskInformationValues values;
            gate::check(window.task_information_values(&values),
                        "partial apply: a task is selected for the form");
            values.name = "Renamed by a partial apply";
            values.duration_text = "not a duration";
            gate::check(!window.apply_task_information(values),
                        "partial apply: the form edit fails on the Duration");
            gate::check(model->data(model->index(1, name_column)).toString() ==
                            QStringLiteral("Renamed by a partial apply"),
                        "partial apply: the Name field stays committed");
            gate::check(!redo->isEnabled(),
                        "partial apply: the committed Name ends the structural redo list "
                        "(Redo is disabled)");
            redo->trigger();
            QApplication::processEvents();
            gate::check(window.pending_edits().empty(),
                        "partial apply: Redo re-queues no Calculate Project over the edited task");
            gate::check(model->data(model->index(1, name_column)).toString() ==
                            QStringLiteral("Renamed by a partial apply"),
                        "partial apply: Redo leaves the edited name as it was");
        }
    }

    // --- 3. reopen: nothing is pending any more ------------------------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(calculated, &error);
        gate::check(opened, "the calculated project reopens");
        if (!opened) {
            return gate::finish();
        }
        window.show();
        QApplication::processEvents();
        gate::check(click_calculate(window), "Calculate Project clicks on the reopened file");
        gate::check(window.pending_edits().empty(),
                    "a second calculation on the saved file queues nothing");
        gate::check(!window.document_dirty(), "the refused calculation leaves the file clean");
        gate::check(window.status_left_text().contains(QStringLiteral("not pending")),
                    "the status line names the refusal on the reopened file");
    }
    return gate::finish();
}
