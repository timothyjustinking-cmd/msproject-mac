// app_qt/tests/cell_bold_gate_test.cpp -- wave 34 (W34-4.5).
//
// THE RULE THIS GATES
//   Task tab > Font > B (Bold) is enabled for exactly the measured shape: ONE
//   cell, the Task Name cell of the task with UniqueID 1, on an unformatted
//   view, alone in the save (tests/data/wave30-textstyle-control.mpp ->
//   wave30-textstyle.mpp, Project's own saves; the field-by-field pair is
//   app_edits_cell_bold).  Every other cell is refused up front with the reason
//   in the status line and queues nothing.  Italic and Underline stay disabled.
//
// WHAT IT PROVES, in the real MainWindow
//   * B is live and wired to RibbonCommand::font_bold; I and U stay disabled;
//   * another row's Name cell, another column of the measured row and a
//     two-cell range each queue nothing and name the refusal;
//   * the measured cell queues exactly one edit and dirties the document;
//   * a second click while it is pending is refused and the queue keeps one;
//   * the real save writes it (the file differs from the control) and a reopen of
//     the saved file refuses a second Bold up front (the view now stores a cell
//     format), so the write is visible to the reopened document.
//
// Usage: qt_cell_bold_gate <wave30-textstyle-control.mpp> <out-dir>
#include <QAbstractButton>
#include <QApplication>

#include <cstddef>
#include <cstdint>
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
#include "menu_model.h"
#include "ribbon_bar.h"
#include "shell_style.h"

namespace {

// Finds a live Font-group control of the Task tab by its text.  The ribbon page
// is rebuilt by set_active_tab, so a pointer is never cached.
QAbstractButton* font_button(msqt::MainWindow& window, const QString& text,
                             msqt::RibbonCommand* command) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int task_tab = ribbon->tab_titles().indexOf(QStringLiteral("Task"));
    if (task_tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(task_tab);
    QApplication::processEvents();
    const int group = ribbon->group_titles().indexOf(QStringLiteral("Font"));
    if (group < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(group); ++control) {
        auto* button = qobject_cast<QAbstractButton*>(ribbon->group_control(group, control));
        // B, I and U are icon-only: the pin label is the accessible name.
        if (button != nullptr && button->accessibleName() == text) {
            if (command != nullptr) {
                *command = ribbon->group_control_command(group, control);
            }
            return button;
        }
    }
    return nullptr;
}

bool click_bold(msqt::MainWindow& window) {
    QAbstractButton* button = font_button(window, QStringLiteral("B"), nullptr);
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    button->click();
    QApplication::processEvents();
    QApplication::processEvents();
    return true;
}

std::size_t row_of(core::sheet::SheetModel& sheet, std::uint32_t uid) {
    for (std::size_t row = 0; row < sheet.row_count(); ++row) {
        if (sheet.row_uid(row) == uid) {
            return row;
        }
    }
    return core::sheet::kNoIndex;
}

// Selects the cell of `uid` under `field`; false when the row or column is absent.
bool select_cell(msqt::MainWindow& window, std::uint32_t uid, std::uint32_t field) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return false;
    }
    const std::size_t row = row_of(*sheet, uid);
    const std::size_t column = window.sheet_table_model()->visible_column_for_field(field);
    if (row == core::sheet::kNoIndex || column == core::sheet::kNoIndex) {
        return false;
    }
    const bool ok = sheet->select_cell(row, column);
    window.refresh_menu_state();
    QApplication::processEvents();
    return ok;
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
    if (argc < 3) {
        std::fprintf(stderr, "usage: qt_cell_bold_gate <textstyle-control.mpp> <out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);
    gate::check(!app::unmeasured_paths_allowed_for_tests(),
                "the shipped refusal default is on in this program");
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed control wave30-textstyle-control.mpp is present");
    if (!std::filesystem::exists(fixture.toStdString())) {
        return gate::finish();
    }
    const QString copy = out_dir + QStringLiteral("/w34-bold-control.mpp");
    const QString bolded = out_dir + QStringLiteral("/w34-bold-saved.mpp");
    std::error_code file_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing, file_error);
    gate::check(!file_error, "the control copy is written under the build tree");
    std::filesystem::remove(bolded.toStdString(), file_error);
    if (file_error) {
        return gate::finish();
    }
    const std::uint32_t measured_uid = app::kBoldMeasuredTaskUid;
    const std::uint32_t name_field = app::kBoldMeasuredFieldId;

    // --- 1. the control, the refusals that queue nothing, the measured cell ----
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        gate::check(window.open_file(copy, &error), "the control opens");
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        msqt::RibbonCommand command = msqt::RibbonCommand::none;
        QAbstractButton* bold = font_button(window, QStringLiteral("B"), &command);
        gate::check(bold != nullptr, "the Task tab's Font group carries B");
        gate::check(bold != nullptr && bold->isEnabled(), "Bold is live (W34-4.5)");
        gate::check(bold != nullptr &&
                        bold->toolTip() != QStringLiteral("not implemented in this port"),
                    "the live Bold button does not carry the disabled tooltip");
        gate::check(command == msqt::RibbonCommand::font_bold,
                    "Bold is wired to RibbonCommand::font_bold");
        for (const char* other : {"I", "U"}) {
            QAbstractButton* button = font_button(window, QString::fromLatin1(other), nullptr);
            gate::check(button != nullptr && !button->isEnabled(),
                        (std::string(other) + " stays disabled").c_str());
        }

        // Another row's Name cell: the pair measured one row only.
        core::sheet::SheetModel* sheet = window.sheet();
        gate::check(sheet != nullptr, "the control has a sheet");
        if (sheet == nullptr) {
            return gate::finish();
        }
        std::uint32_t other_uid = 0;
        for (std::size_t row = 0; row < sheet->row_count(); ++row) {
            if (sheet->row_uid(row) != 0 && sheet->row_uid(row) != measured_uid) {
                other_uid = sheet->row_uid(row);
                break;
            }
        }
        gate::check(other_uid != 0, "the control carries a second task");
        if (other_uid != 0 && select_cell(window, other_uid, name_field)) {
            gate::check(click_bold(window), "Bold clicks on another row");
            gate::check(window.pending_edits().empty(), "another row's Name cell queues nothing");
            gate::check(!window.document_dirty(), "the refusal leaves the document clean");
            gate::check(window.status_left_text().contains(
                            QStringLiteral("is not implemented in this port")),
                        "the status line names the other-row refusal");
        } else {
            gate::check(false, "another row's Name cell can be selected");
        }

        // Another column of the measured row.
        std::uint32_t other_field = 0;
        for (std::size_t column = 0; column < sheet->column_count(); ++column) {
            const std::uint32_t field = sheet->column(column).field;
            if (field != 0 && field != name_field && sheet->column_visible(column)) {
                other_field = field;
                break;
            }
        }
        gate::check(other_field != 0, "the sheet shows a column besides Task Name");
        if (other_field != 0 && select_cell(window, measured_uid, other_field)) {
            gate::check(click_bold(window), "Bold clicks on another column");
            gate::check(window.pending_edits().empty(),
                        "another column of the measured row queues nothing");
            gate::check(window.status_left_text().contains(
                            QStringLiteral("is not implemented in this port")),
                        "the status line names the other-column refusal");
        } else {
            gate::check(false, "another column of the measured row can be selected");
        }

        // A range that starts on the measured cell: the pair bolds one cell.
        {
            const std::size_t row = row_of(*sheet, measured_uid);
            const std::size_t column = window.sheet_table_model()->visible_column_for_field(name_field);
            gate::check(select_cell(window, measured_uid, name_field) && row + 1 < sheet->row_count() &&
                            sheet->extend_to(row + 1, column) &&
                            !(sheet->anchor() == sheet->focus()),
                        "a two-cell range anchored on the measured cell can be selected");
            window.refresh_menu_state();
            gate::check(click_bold(window), "Bold clicks on a range");
            gate::check(window.pending_edits().empty(), "a two-cell range queues nothing");
            gate::check(window.status_left_text().contains(QStringLiteral("more than one cell")),
                        "the status line names the range refusal");
        }

        // The measured cell.
        gate::check(select_cell(window, measured_uid, name_field),
                    "the measured Task Name cell can be selected");
        gate::check(click_bold(window), "Bold clicks on the measured cell");
        gate::check(window.pending_edits().size() == 1, "Bold queues one edit");
        gate::check(window.document_dirty(), "the queued Bold dirties the document");
        gate::check(window.status_left_text().contains(QStringLiteral("queued Font Bold")),
                    "the status line reports the queued Bold");

        // A second click while it is pending: the edit is exclusive.
        gate::check(click_bold(window), "a second Bold clicks");
        gate::check(window.pending_edits().size() == 1, "a second click keeps the queue at one");

        QString save_error;
        const bool ok = window.save_document_path(bolded, &save_error);
        gate::check(ok, "the Bold saves");
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
            return gate::finish();
        }
        gate::check(read_all(bolded) != read_all(copy), "the saved file differs from the control");
    }

    // --- 2. reopen: the view now stores the cell format -----------------------
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(bolded, &error);
        gate::check(opened, "the saved Bold reopens");
        if (!opened) {
            return gate::finish();
        }
        window.show();
        QApplication::processEvents();
        gate::check(select_cell(window, measured_uid, name_field),
                    "the measured cell can be selected on the reopened file");
        gate::check(click_bold(window), "Bold clicks on the reopened file");
        gate::check(window.pending_edits().empty(),
                    "a second Bold on the saved file queues nothing (the view stores a format)");
        gate::check(!window.document_dirty(), "the refused Bold leaves the reopened file clean");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("is not implemented in this port")),
                    "the status line names the refusal on the reopened file");
    }
    return gate::finish();
}
