// app_qt/tests/cell_bold_gate_test.cpp -- wave 34 (W34-4.5).
//
// THE RULE THIS GATES
//   Task tab > Font > B (Bold) is enabled for exactly the measured shapes: ONE
//   cell, the Task Name or the Start cell of a saved task (W36-1.2: Project's
//   pairs bold row 1 and row 2 Name and row 1 Start), on an unformatted view,
//   alone in the save (tests/data/wave30-textstyle-control.mpp ->
//   wave30-textstyle.mpp and the wave35-g3 pairs, Project's own saves; the
//   field-by-field pairs are app_edits_cell_bold).  Every other cell is refused
//   up front with the reason in the status line and queues nothing.  Italic and
//   Underline stay disabled.
//
// WHAT IT PROVES, in the real MainWindow
//   * B is live and wired to RibbonCommand::font_bold; I and U stay disabled;
//   * a column that is neither Task Name nor Start and a two-cell range each
//     queue nothing and name the refusal;
//   * the Task Name cell of row 2 and the Start cell of row 1 each queue one
//     edit and save a file that differs from the control (W36-1.2);
//   * I (W36-1.3) queues Italic for the one measured cell (row 1, Task Name) and
//     refuses row 2, the Start cell and Italic off; Bold on an Italic cell is
//     refused, not read as a Bold toggle;
//   * the measured cell queues exactly one edit and dirties the document;
//   * a second click while it is pending is refused and the queue keeps one;
//   * the real save writes it (the file differs from the control) and a reopen of
//     the saved file reads it: the button now turns Bold OFF (W36-1.1, the one
//     measured Bold item, wave35-g3-font-bold-off), the save writes that, and a
//     third click on the reopened file queues nothing (the view stores a cell
//     format item that is not Bold), so each write is visible to the reopened
//     document.
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
#include "mpp_mutate.h"
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

bool click_font(msqt::MainWindow& window, const QString& label) {
    QAbstractButton* button = font_button(window, label, nullptr);
    if (button == nullptr || !button->isEnabled()) {
        return false;
    }
    button->click();
    QApplication::processEvents();
    QApplication::processEvents();
    return true;
}

bool click_bold(msqt::MainWindow& window) {
    return click_font(window, QStringLiteral("B"));
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

// The visible column whose stored `214/CTable` field id is `table_field` (the key
// the view stores a cell format under; the Start column's model field differs).
std::size_t column_for_table_field(core::sheet::SheetModel& sheet, std::uint32_t table_field) {
    for (std::size_t ordinal = 0; ordinal < sheet.visible_column_count(); ++ordinal) {
        const std::size_t column = sheet.visible_column(ordinal);
        if (column != core::sheet::kNoIndex && sheet.column(column).table_field == table_field) {
            return column;
        }
    }
    return core::sheet::kNoIndex;
}

// Selects the cell of `uid` in the column stored under `table_field`.
bool select_stored_cell(msqt::MainWindow& window, std::uint32_t uid, std::uint32_t table_field) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return false;
    }
    const std::size_t row = row_of(*sheet, uid);
    const std::size_t column = column_for_table_field(*sheet, table_field);
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
    const QString unbolded = out_dir + QStringLiteral("/w36-bold-off-saved.mpp");
    std::filesystem::remove(bolded.toStdString(), file_error);
    std::filesystem::remove(unbolded.toStdString(), file_error);
    if (file_error) {
        return gate::finish();
    }
    const std::uint32_t measured_uid = app::kBoldMeasuredTaskUid;
    const std::uint32_t name_field = app::kBoldMeasuredFieldId;
    const std::uint32_t start_field = app::kBoldMeasuredStartFieldId;

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
        // W36-1.3: I is live too (the one measured Italic cell); U stays disabled.
        msqt::RibbonCommand italic_command = msqt::RibbonCommand::none;
        QAbstractButton* italic = font_button(window, QStringLiteral("I"), &italic_command);
        gate::check(italic != nullptr && italic->isEnabled(), "Italic is live (W36-1.3)");
        gate::check(italic_command == msqt::RibbonCommand::font_italic,
                    "Italic is wired to RibbonCommand::font_italic");
        {
            QAbstractButton* button = font_button(window, QStringLiteral("U"), nullptr);
            gate::check(button != nullptr && !button->isEnabled(), "U stays disabled");
        }

        core::sheet::SheetModel* sheet = window.sheet();
        gate::check(sheet != nullptr, "the control has a sheet");
        if (sheet == nullptr) {
            return gate::finish();
        }

        // A column the pairs did not measure (neither Task Name nor Start), on
        // the measured row.
        std::uint32_t other_field = 0;
        for (std::size_t ordinal = 0; ordinal < sheet->visible_column_count(); ++ordinal) {
            const std::size_t column = sheet->visible_column(ordinal);
            const std::uint32_t field = sheet->column(column).table_field;
            if (column != core::sheet::kNoIndex && sheet->column(column).field != 0 &&
                field != name_field && field != start_field) {
                other_field = field;
                break;
            }
        }
        gate::check(other_field != 0, "the sheet shows a column besides Task Name and Start");
        if (other_field != 0 && select_stored_cell(window, measured_uid, other_field)) {
            gate::check(click_bold(window), "Bold clicks on another column");
            gate::check(window.pending_edits().empty(),
                        "an unmeasured column of the measured row queues nothing");
            gate::check(!window.document_dirty(), "the refusal leaves the document clean");
            gate::check(window.status_left_text().contains(QStringLiteral(
                            "Font Bold on a cell outside the Task Name and Start columns")),
                        "the status line names the other-column refusal");
        } else {
            gate::check(false, "an unmeasured column of the measured row can be selected");
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

    // --- 1b. W36-1.2: Bold on another task row and on the Start column ---------
    // Pairs wave35-g3-font-bold-row2 (uid 2, Name) and wave35-g3-font-bold-start
    // (uid 1, Start): each a fresh window on the control, one cell, one edit, a
    // save that differs from the control.
    struct MeasuredCell {
        const char* label;
        std::uint32_t uid;
        std::uint32_t table_field;
        const char* status;
        const char* file;
    };
    const MeasuredCell cells[] = {
        {"row 2 Name", 2, name_field, "queued Font Bold on the Task Name cell of task uid 2",
         "/w36-bold-row2-saved.mpp"},
        {"row 1 Start", 1, start_field, "queued Font Bold on the Start cell of task uid 1",
         "/w36-bold-start-saved.mpp"},
    };
    for (const MeasuredCell& cell : cells) {
        const std::string label = cell.label;
        const QString saved = out_dir + QString::fromLatin1(cell.file);
        std::filesystem::remove(saved.toStdString(), file_error);
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(copy, &error);
        gate::check(opened, (label + ": the control opens").c_str());
        if (!opened) {
            return gate::finish();
        }
        window.show();
        QApplication::processEvents();
        gate::check(select_stored_cell(window, cell.uid, cell.table_field),
                    (label + ": the cell can be selected").c_str());
        gate::check(click_bold(window), (label + ": Bold clicks").c_str());
        gate::check(window.pending_edits().size() == 1, (label + ": Bold queues one edit").c_str());
        gate::check(window.pending_edits().size() == 1 &&
                        window.pending_edits().applied().front().cell_bold_on,
                    (label + ": the queued edit turns Bold ON").c_str());
        gate::check(window.document_dirty(), (label + ": the queued Bold dirties the document").c_str());
        gate::check(window.status_left_text().contains(QString::fromLatin1(cell.status)),
                    (label + ": the status line names the cell").c_str());
        QString save_error;
        const bool ok = window.save_document_path(saved, &save_error);
        gate::check(ok, (label + ": the Bold saves").c_str());
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
            return gate::finish();
        }
        gate::check(read_all(saved) != read_all(copy),
                    (label + ": the saved file differs from the control").c_str());
    }

    // --- 1c. W36-1.3: Italic on the one measured cell --------------------------
    // wave35-g3-font-italic is the only Italic pair (row 1, Task Name), so every
    // other cell is refused, Italic off is refused, and the saved file reopens
    // with the item present.
    const QString italicised = out_dir + QStringLiteral("/w36-italic-saved.mpp");
    std::filesystem::remove(italicised.toStdString(), file_error);
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(copy, &error);
        gate::check(opened, "Italic: the control opens");
        if (!opened) {
            return gate::finish();
        }
        window.show();
        QApplication::processEvents();
        const QString italic_label = QStringLiteral("I");
        gate::check(select_stored_cell(window, 2, name_field),
                    "Italic: the Task Name cell of row 2 can be selected");
        gate::check(click_font(window, italic_label), "Italic clicks on row 2");
        gate::check(window.pending_edits().empty(), "Italic on row 2 queues nothing");
        gate::check(window.status_left_text().contains(QStringLiteral(
                        "Font Italic on this row is not implemented in this port")),
                    "the status line names the Italic row refusal");
        gate::check(select_stored_cell(window, measured_uid, start_field),
                    "Italic: the Start cell of row 1 can be selected");
        gate::check(click_font(window, italic_label), "Italic clicks on the Start cell");
        gate::check(window.pending_edits().empty(), "Italic on the Start cell queues nothing");
        gate::check(window.status_left_text().contains(QStringLiteral(
                        "Font Italic on a cell outside the Task Name column")),
                    "the status line names the Italic column refusal");
        gate::check(select_stored_cell(window, measured_uid, name_field),
                    "Italic: the measured Task Name cell can be selected");
        gate::check(click_font(window, italic_label), "Italic clicks on the measured cell");
        gate::check(window.pending_edits().size() == 1, "Italic queues one edit");
        gate::check(window.document_dirty(), "the queued Italic dirties the document");
        gate::check(window.status_left_text().contains(QStringLiteral(
                        "queued Font Italic on the Task Name cell of task uid 1")),
                    "the status line reports the queued Italic");
        QString save_error;
        const bool ok = window.save_document_path(italicised, &save_error);
        gate::check(ok, "the Italic saves");
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
            return gate::finish();
        }
        gate::check(read_all(italicised) != read_all(copy),
                    "the Italic file differs from the control");
    }
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(italicised, &error);
        gate::check(opened, "the saved Italic reopens");
        if (!opened) {
            return gate::finish();
        }
        window.show();
        QApplication::processEvents();
        gate::check(select_stored_cell(window, measured_uid, name_field),
                    "the Italic cell can be selected on the reopened file");
        gate::check(click_font(window, QStringLiteral("I")), "Italic clicks on the Italic file");
        gate::check(window.pending_edits().empty(),
                    "Italic on a view that stores a format item queues nothing");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("turning Italic off is not measured")),
                    "the status line says Italic off is not measured");
        // Bold on the Italic cell is a different attribute on a formatted block:
        // Bold's toggle must not read the Italic item as its own.
        gate::check(click_bold(window), "Bold clicks on the Italic file");
        gate::check(window.pending_edits().empty(),
                    "Bold on a cell that carries an Italic item queues nothing");
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
        // W36-1.1: Bold is a toggle.  The saved file carries the cell's one Bold
        // item, so the button queues Bold OFF (wave35-g3-font-bold-off), saved
        // in place.
        gate::check(click_bold(window), "Bold clicks on the reopened file");
        gate::check(window.pending_edits().size() == 1,
                    "Bold on the saved file queues one edit (Bold off)");
        gate::check(window.pending_edits().size() == 1 &&
                        !window.pending_edits().applied().front().cell_bold_on,
                    "the queued edit turns Bold OFF");
        gate::check(window.status_left_text().contains(QStringLiteral("queued Font Bold off")),
                    "the status line reports the queued Bold off");
        gate::check(window.document_dirty(), "the queued Bold off dirties the document");
        QString save_error;
        const bool ok = window.save_document_path(unbolded, &save_error);
        gate::check(ok, "the Bold off saves");
        if (!ok) {
            std::fprintf(stderr, "note: save error: %s\n", save_error.toStdString().c_str());
            return gate::finish();
        }
        gate::check(read_all(unbolded) != read_all(bolded),
                    "the Bold off file differs from the Bold file");
    }

    // --- 2b. reopen again: the item is there but no longer Bold ----------------
    // A third click has nothing to turn on or off: the writer refuses with its
    // reason and nothing is queued.
    {
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        const bool opened = window.open_file(unbolded, &error);
        gate::check(opened, "the saved Bold off reopens");
        if (!opened) {
            return gate::finish();
        }
        window.show();
        QApplication::processEvents();
        gate::check(select_cell(window, measured_uid, name_field),
                    "the measured cell can be selected on the Bold off file");
        gate::check(click_bold(window), "Bold clicks on the Bold off file");
        gate::check(window.pending_edits().empty(),
                    "a Bold on a view that stores a format item queues nothing");
        gate::check(!window.document_dirty(), "the refused Bold leaves the reopened file clean");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("Font Bold is not implemented in this port for this project")),
                    "the status line names the refusal on the Bold off file");
        gate::check(window.status_left_text().contains(
                        QStringLiteral("already carries a cell format item")),
                    "the status line gives the writer's reason on the Bold off file");
    }

    // --- 3. W35-0.2 (R34G4-4) / RW34b-3: the applied view is not the measured one
    // The control with one byte of the applied view's main block flipped: the
    // writer's dry run refuses ("differs from the measured control's"), the
    // refusal reaches the status line and nothing is queued.
    {
        const QString changed = out_dir + QStringLiteral("/w35-cell-bold-changed-view.mpp");
        const bool made = mppmutate::write_edited_copy(
            fixture.toStdString(), changed.toStdString(), [](mppwrite::CfbDocument& doc) {
                const char* const path = "214/CV_iew/Var2Data";
                mppmutate::Bytes var2 = doc.read_stream(path);
                var2[42576 + 4 + 20000] ^= 0x01;
                doc.replace_stream(path, var2);
            });
        gate::check(made, "the changed-view control is written");
        msqt::MainWindow window;
        window.resize(msqt::kParityWidth, 800);
        QString error;
        if (made && window.open_file(changed, &error)) {
            window.show();
            QApplication::processEvents();
            gate::check(select_cell(window, measured_uid, name_field),
                        "the measured cell can be selected on the changed-view file");
            gate::check(click_bold(window), "Bold clicks on the changed-view file");
            gate::check(window.pending_edits().empty(), "a changed applied view queues nothing");
            gate::check(!window.document_dirty(), "the refusal leaves the file clean");
            gate::check(window.status_left_text().contains(
                            QStringLiteral("differs from the measured control's")),
                        "the status line names the changed-view refusal");
        } else {
            gate::check(false, "the changed-view control opens");
        }
    }
    return gate::finish();
}
