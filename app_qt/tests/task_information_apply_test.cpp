// app_qt/tests/task_information_apply_test.cpp -- wave 33 (W33-1.5 / A-7, Medium).
//
// THE DEFECT THIS GATES
//   Task Information's Custom Fields tab (the Task Fields surface, W26-5.3) and
//   Task Fields pushed their structural edits one by one onto the live queue and
//   ignored push()'s return.  A push refused after an earlier edit was accepted
//   left that earlier edit queued even though the dialog reported a refusal --
//   the status line said the edit was refused while part of it was applied.
//
// THE FIX
//   Every structural push is decided on a TRIAL copy of the queue first.  Only
//   when every push succeeds is the trial committed (the cells are committed
//   after the trial); the first refusal reports its reason and the live queue is
//   untouched.
//
// THE FIXTURE
//   tests/data/wave29-outline-auto-indent-control.mpp is the three-row automatic
//   control: an automatic indent of uid 2 under uid 1 carries slack and is
//   exclusive with every percent edit (W32-0.1).  The test copies it into the
//   build tree; the committed file is never opened in place.
//
// WHAT IT PROVES
//   * with an automatic indent pending, a Task Information apply carrying a
//     Text1 (Custom Fields) value and 40 % is refused;
//   * the status line names the pending-automatic-outline refusal;
//   * the refused apply leaves pending_edits().size() unchanged (the Text1 edit
//     is not left behind) and the automatic indent is still the only edit.
//
// Usage: qt_task_information_apply <wave29-outline-auto-indent-control.mpp> <out-dir>

#include <QAction>
#include <QApplication>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#include "dialogs.h"
#include "gate_util.h"
#include "layout_model.h"
#include "main_window.h"
#include "menu_model.h"
#include "reader.h"
#include "sheet.h"
#include "shell_style.h"

namespace {

std::size_t row_of_uid(const core::sheet::SheetModel& sheet, std::uint32_t uid) {
    for (std::size_t row = 0; row < sheet.row_count(); ++row) {
        if (sheet.row_uid(row) == uid) {
            return row;
        }
    }
    return core::sheet::kNoIndex;
}

void select_uid(msqt::MainWindow& window, std::uint32_t uid) {
    core::sheet::SheetModel* sheet = window.sheet();
    if (sheet == nullptr) {
        return;
    }
    const std::size_t row = row_of_uid(*sheet, uid);
    if (row == core::sheet::kNoIndex) {
        return;
    }
    const std::size_t column =
        window.sheet_table_model()->visible_column_for_field(app::name_field_id());
    sheet->select_cell(row, column == core::sheet::kNoIndex ? 0 : column);
    window.refresh_menu_state();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: qt_task_information_apply "
                     "<wave29-outline-auto-indent-control.mpp> <out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);

    // A missing committed fixture FAILS the gate (wave-33 brief section 1).
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed fixture wave29-outline-auto-indent-control.mpp is present");
    if (!std::filesystem::exists(fixture.toStdString())) {
        return gate::finish();
    }

    const QString copy = out_dir + QStringLiteral("/w33-task-information-apply.mpp");
    std::error_code copy_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing,
                               copy_error);
    gate::check(!copy_error, "the fixture copy is written under the build tree");
    if (copy_error) {
        return gate::finish();
    }

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(copy, &error), "the copied fixture opens");
    if (window.sheet() == nullptr) {
        return gate::finish();
    }

    // --- queue the automatic indent exactly as the menu does -----------------
    QAction* indent = gate::find_action(window, app::MenuAction::indent);
    gate::check(indent != nullptr, "the Indent Task command exists");
    if (indent == nullptr) {
        return gate::finish();
    }
    select_uid(window, 2);
    gate::check(indent->isEnabled(), "Indent Task is enabled on uid 2");
    indent->trigger();
    QApplication::processEvents();
    gate::check(window.pending_edits().has_automatic_outline_edit(),
                "the automatic indent is pending before the dialog apply");
    const std::size_t pending_before = window.pending_edits().size();

    // --- the apply carries a Text1 value and 40 % -----------------------------
    select_uid(window, 3);
    msqt::TaskInformationValues values;
    gate::check(window.task_information_values(&values),
                "uid 3's Task Information values are readable");
    gate::check(!values.custom_field_ids.empty() && !values.custom_field_texts.empty(),
                "the Custom Fields tab carries its measured fields");
    if (!values.custom_field_ids.empty()) {
        values.custom_field_texts.front() = "w33-1-5";
    }
    values.percent_text = "40%";
    gate::check(!window.apply_task_information(values),
                "the apply is refused while the automatic indent is pending");
    gate::check(
        window.status_left_text().contains(
            QStringLiteral("save or undo the pending automatic outline edit first")),
        "the status line names the pending-automatic-outline refusal");
    gate::check(window.pending_edits().size() == pending_before,
                "the refused apply queues nothing (the Text1 edit is not left behind)");
    gate::check(window.pending_edits().has_automatic_outline_edit(),
                "the automatic indent is still the only pending edit");

    return gate::finish();
}
