// app_qt/tests/wbs_code_gate_test.cpp -- wave 34 (W34-4.6).
//
// THE RULE THIS GATES
//   Project > Properties > WBS > Define Code... is enabled for exactly the
//   measured shape: a project of THREE flat tasks (UniqueID 1..3, no summary, no
//   nesting) that stores no WBS text yet, alone in the save
//   (tests/data/wave30-wbs-control.mpp -> wave30-wbs-code.mpp, Project's own
//   saves; the field-by-field pair is app_edits_wbs).  Every other project is
//   refused up front with the decision-9 reason in the status line and queues
//   nothing.  Renumber... stays disabled with the exact decision-9 tooltip.
//
// WHAT IT PROVES, in the real MainWindow
//   * Define Code... is live, Renumber... is disabled with the exact tooltip;
//   * on a project of another shape (wave30-insert-milestone.mpp) it queues
//     nothing, leaves the document clean and names the refusal;
//   * on the measured control it queues exactly one edit and dirties the
//     document; a second click while it is pending is refused (queue stays 1);
//   * the real save writes it (the file differs from the control) and a reopen
//     of the saved file refuses a second definition (the file now stores WBS
//     text), so the write is visible to the reopened document.
//
// Usage: qt_wbs_code_gate <wave30-wbs-control.mpp> <other-shape.mpp> <out-dir>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QToolButton>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <system_error>
#include <vector>

#include "edit_model.h"
#include "gate_util.h"
#include "main_window.h"
#include "mpp_mutate.h"
#include "ribbon_bar.h"
#include "shell_style.h"

namespace {

// The WBS split button of Project > Properties.  The ribbon page is rebuilt by
// set_active_tab, so a pointer is never cached.
QToolButton* wbs_button(msqt::MainWindow& window) {
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
    const int group = ribbon->group_titles().indexOf(QStringLiteral("Properties"));
    if (group < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(group); ++control) {
        auto* button = qobject_cast<QToolButton*>(ribbon->group_control(group, control));
        if (button == nullptr) {
            continue;
        }
        QString text = button->text().isEmpty() ? button->accessibleName() : button->text();
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (text.simplified() == QStringLiteral("WBS")) {
            return button;
        }
    }
    return nullptr;
}

QAction* menu_item(msqt::MainWindow& window, const QString& text) {
    QToolButton* button = wbs_button(window);
    if (button == nullptr || button->menu() == nullptr) {
        return nullptr;
    }
    for (QAction* action : button->menu()->actions()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

bool trigger_define_code(msqt::MainWindow& window) {
    QAction* action = menu_item(window, QStringLiteral("Define Code..."));
    if (action == nullptr || !action->isEnabled()) {
        return false;
    }
    action->trigger();
    QApplication::processEvents();
    QApplication::processEvents();
    return true;
}

std::vector<char> read_all(const QString& path) {
    std::ifstream in(path.toStdString(), std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 4) {
        std::fprintf(stderr, "usage: qt_wbs_code_gate <wbs-control.mpp> <other.mpp> <out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString other_fixture = QString::fromLocal8Bit(argv[2]);
    const QString out_dir = QString::fromLocal8Bit(argv[3]);
    gate::check(!app::unmeasured_paths_allowed_for_tests(),
                "the shipped refusal default is on in this program");
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed control wave30-wbs-control.mpp is present");
    gate::check(std::filesystem::exists(other_fixture.toStdString()),
                "the committed other-shape fixture is present");
    if (!std::filesystem::exists(fixture.toStdString()) ||
        !std::filesystem::exists(other_fixture.toStdString())) {
        return gate::finish();
    }
    const QString copy = out_dir + QStringLiteral("/w34-wbs-control.mpp");
    const QString other_copy = out_dir + QStringLiteral("/w34-wbs-other.mpp");
    const QString coded = out_dir + QStringLiteral("/w34-wbs-saved.mpp");
    std::error_code file_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the control copy is written under the build tree");
    std::filesystem::copy_file(other_fixture.toStdString(), other_copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the other-shape copy is written under the build tree");
    std::filesystem::remove(coded.toStdString(), file_error);
    if (file_error) {
        return gate::finish();
    }

    // --- 1. the menu, the refusal that queues nothing ------------------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(other_copy, &error), "the other-shape project opens");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        // menu_item() rebuilds the ribbon page, so each action is read before the
        // next lookup and no pointer is kept across one.
        {
            QAction* define = menu_item(window, QStringLiteral("Define Code..."));
            gate::check(define != nullptr && define->isEnabled(),
                        "Define Code... is live (W34-4.6)");
            gate::check(define != nullptr &&
                            define->toolTip() != QStringLiteral("not implemented in this port"),
                        "the live item does not carry the disabled tooltip");
        }
        {
            QAction* renumber = menu_item(window, QStringLiteral("Renumber..."));
            gate::check(renumber != nullptr && !renumber->isEnabled() &&
                            renumber->toolTip() == QStringLiteral("not implemented in this port"),
                        "Renumber... stays disabled with the exact tooltip");
        }

        gate::check(trigger_define_code(window), "Define Code... triggers on another shape");
        gate::check(window.pending_edits().empty(), "another shape queues nothing");
        gate::check(!window.document_dirty(), "the refusal leaves the document clean");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("Define WBS Code is not implemented in this port for "
                                       "a project with")),
                    "the status line names the refusal");
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

        gate::check(trigger_define_code(window), "Define Code... triggers on the control");
        gate::check(window.pending_edits().size() == 1, "Define Code queues one edit");
        gate::check(window.document_dirty(), "the queued definition dirties the document");
        gate::check(window.status_left_text().contains(QStringLiteral("queued the WBS code")),
                    "the status line reports the queued definition");

        // A second click while it is pending: the edit is exclusive.
        gate::check(trigger_define_code(window), "a second Define Code triggers");
        gate::check(window.pending_edits().size() == 1, "a second click keeps the queue at one");

        QString save_error;
        const bool ok = window.save_document_path(coded, &save_error);
        gate::check(ok, "the definition saves");
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
            return gate::finish();
        }
        gate::check(read_all(coded) != read_all(copy), "the saved file differs from the control");
    }

    // --- 3. reopen: the file now stores WBS text -----------------------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(coded, &error);
        gate::check(opened, "the saved definition reopens");
        if (!opened) {
            return gate::finish();
        }
        window.show();
        QApplication::processEvents();
        gate::check(trigger_define_code(window), "Define Code... triggers on the reopened file");
        gate::check(window.pending_edits().empty(),
                    "a second definition on the saved file queues nothing (the file stores one)");
        gate::check(!window.document_dirty(), "the refused definition leaves the file clean");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("redefining a code is not implemented in this port")),
                    "the status line names the refusal on the reopened file");
    }

    // --- 4. W35-0.2 (R34G4-5) / RW34b-3: the ID and the row are the UniqueID ---
    // The control with the ID words of tasks 1 and 2 swapped (an ID differs from
    // its UniqueID), and with the two whole rows swapped (the tasks are stored out
    // of UniqueID order): each is refused with its own text before the writer is
    // asked, and nothing is queued.
    {
        struct Shape {
            const char* label;
            std::size_t words;  // bytes swapped between rows 1 and 2 (row 0 is the summary):
                                // 4 = the ID word, 202 = the whole row
            const char* needle;
        };
        const Shape shapes[] = {
            {"a task whose ID differs from its UniqueID", 4, "ID differs from its UniqueID"},
            {"tasks stored out of UniqueID order", 202, "stored out of UniqueID order"},
        };
        for (const Shape& shape : shapes) {
            const QString changed = out_dir + QStringLiteral("/w35-wbs-shape.mpp");
            const std::size_t swap_bytes = shape.words;
            const bool made = mppmutate::write_edited_copy(
                fixture.toStdString(), changed.toStdString(),
                [swap_bytes](mppwrite::CfbDocument& doc) {
                    const char* const path = "114/TBkndTask/FixedData";
                    mppmutate::Bytes rows = doc.read_stream(path);
                    for (std::size_t i = 0; i < swap_bytes; ++i) {
                        std::swap(rows[48 + 202 + i], rows[48 + 404 + i]);
                    }
                    doc.replace_stream(path, rows);
                });
            gate::check(made, shape.label);
            msqt::MainWindow window;
            window.resize(msqt::kParityWidth, 800);
            QString error;
            if (made && window.open_file(changed, &error)) {
                window.show();
                QApplication::processEvents();
                gate::check(trigger_define_code(window), shape.label);
                gate::check(window.pending_edits().empty(), "the refused shape queues nothing");
                gate::check(!window.document_dirty(), "the refused shape leaves the file clean");
                gate::check(window.status_left_text().contains(QString::fromUtf8(shape.needle)),
                            shape.label);
            } else {
                gate::check(false, shape.label);
            }
        }
    }
    return gate::finish();
}
