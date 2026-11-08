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
// Usage: qt_calculate_gate <unlinked-control.mpp> <linked-control.mpp> <out-dir>
#include <QAbstractButton>
#include <QApplication>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "edit_model.h"
#include "gate_util.h"
#include "main_window.h"
#include "ribbon_bar.h"
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
        std::fprintf(stderr,
                     "usage: qt_calculate_gate <unlinked-control.mpp> <linked-control.mpp> "
                     "<out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString other_fixture = QString::fromLocal8Bit(argv[2]);
    const QString out_dir = QString::fromLocal8Bit(argv[3]);
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

        QString save_error;
        const bool ok = window.save_document_path(calculated, &save_error);
        gate::check(ok, "the calculation saves");
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
            return gate::finish();
        }
        gate::check(read_all(calculated) != read_all(copy),
                    "the saved file differs from the control");
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
