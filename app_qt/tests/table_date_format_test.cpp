// app_qt/tests/table_date_format_test.cpp -- wave 33 (W33-1.6 / A-11, Medium).
//
// THE DEFECT THIS GATES
//   View > Tables can apply any of the file's stored task tables for the
//   session, but the Table / Date Format dialog hard-coded the request's
//   table_index to 0.  With the second table on screen the save wrote the
//   FIRST table's date-format byte and left the shown table's alone -- the
//   edit silently addressed a table the user was not looking at.
//
// THE FIX
//   The dialog carries the index of the table the view shows, and the main
//   window supplies it from the document's applied table.  The save writes that
//   table's 214/CTable block only.
//
// THE FIXTURE
//   tests/data/wave31-tables.mpp stores two task tables (W31-1.3): Entry (index
//   0, second column Indicators 0x0b4000cd) is the one the file applies, and
//   Review (index 1, second column Text1 0x0b400033) is the session alternative.
//   Both store the 0xff default date format.  The test copies the fixture into
//   the build tree; the committed file is never opened in place.
//
// WHAT IT PROVES
//   * the View tab's Tables control applies Review through the real ribbon
//     popup, and the sheet then shows Text1 and no longer Indicators;
//   * after choosing format 4 in the dialog and saving, the SAVED file's second
//     table carries date format 4;
//   * the first table's date format, its whole stored block, and the applied
//     Entry table's Var2Data date-format byte are unchanged.
//
// Usage: qt_table_date_format <wave31-tables.mpp> <out-dir>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "cfb_writer.h"
#include "dialogs.h"
#include "gate_util.h"
#include "layout_model.h"
#include "main_window.h"
#include "menu_model.h"
#include "ribbon_bar.h"
#include "sheet.h"
#include "sheet_table_model.h"
#include "shell_style.h"
#include "viewstate.h"

namespace {

constexpr std::uint32_t kIndicatorsField = 0x0b4000cdu;  // Entry's second column
constexpr std::uint32_t kText1Field = 0x0b400033u;       // Review's second column

struct StoredTables {
    bool ok = false;
    std::vector<msproject::viewstate::TableDef> tables;
    std::vector<std::uint8_t> var2;
};

// The four 214/CTable parts, parsed exactly as the save path parses them.
StoredTables read_tables(const std::filesystem::path& path) {
    StoredTables out;
    try {
        const mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(path.string());
        const char* const names[4] = {"214/CTable/FixedData", "214/CTable/FixedMeta",
                                      "214/CTable/VarMeta", "214/CTable/Var2Data"};
        std::vector<std::uint8_t> raw[4];
        for (std::size_t index = 0; index < 4; ++index) {
            if (!document.has_stream(names[index])) {
                std::fprintf(stderr, "FAIL: %s has no %s\n", path.string().c_str(),
                             names[index]);
                return out;
            }
            raw[index] = document.read_stream(names[index]);
        }
        msproject::viewstate::PartSet parts;
        parts.fixed_data = raw[0];
        parts.fixed_meta = raw[1];
        parts.var_meta = raw[2];
        parts.var2_data = raw[3];
        msproject::viewstate::ViewState state;
        msproject::viewstate::ParseError parse_error;
        if (!msproject::viewstate::parse_view_state("CTable", parts, state, &parse_error)) {
            std::fprintf(stderr, "FAIL: %s CTable does not parse: %s at %zu\n",
                         path.string().c_str(), parse_error.message.c_str(),
                         parse_error.offset);
            return out;
        }
        out.tables = state.tables;
        out.var2 = std::move(state.var2_data);
        out.ok = true;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s does not read as a CFB: %s\n", path.string().c_str(),
                     error.what());
    }
    return out;
}

// A table's whole stored block: the u32 length prefix is at block_offset and the
// payload (block_length bytes) follows it.
std::vector<std::uint8_t> table_block(const StoredTables& stored, std::size_t index) {
    if (!stored.ok || index >= stored.tables.size()) {
        return {};
    }
    const auto& table = stored.tables[index];
    const std::size_t start = static_cast<std::size_t>(table.block_offset) + 4;
    const std::size_t length = table.block_length;
    if (start > stored.var2.size() || length > stored.var2.size() - start) {
        return {};
    }
    return std::vector<std::uint8_t>(
        stored.var2.begin() + static_cast<std::ptrdiff_t>(start),
        stored.var2.begin() + static_cast<std::ptrdiff_t>(start + length));
}

QAbstractButton* ribbon_button_for(msqt::MainWindow& window, msqt::RibbonCommand command) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int view_tab = ribbon->tab_titles().indexOf(QStringLiteral("View"));
    if (view_tab < 0) {
        return nullptr;
    }
    ribbon->set_active_tab(view_tab);
    QApplication::processEvents();
    const int groups = static_cast<int>(ribbon->group_titles().size());
    for (int group = 0; group < groups; ++group) {
        const int controls = ribbon->group_control_count(group);
        for (int control = 0; control < controls; ++control) {
            if (ribbon->group_control_command(group, control) == command) {
                return qobject_cast<QAbstractButton*>(ribbon->group_control(group, control));
            }
        }
    }
    return nullptr;
}

// Clicks Tables and, inside the popup's nested event loop, triggers the entry
// whose caption is `caption`.  The listed captions are returned for reporting.
// A programmatic QAction::trigger() does not close an exec()ed QMenu, so the
// timer closes the popup itself; a watchdog closes it even if the popup cannot
// be identified, so a failure reports instead of hanging.
bool apply_table_via_ribbon(msqt::MainWindow& window, const QString& caption,
                            QStringList* listed) {
    bool chosen = false;
    QMenu* popup = nullptr;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, [&chosen, &caption, listed, &popup, &poll]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu == nullptr) {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* candidate = qobject_cast<QMenu*>(widget);
                if (candidate != nullptr && candidate->isVisible()) {
                    menu = candidate;
                    break;
                }
            }
        }
        if (menu == nullptr) {
            return;
        }
        popup = menu;
        for (QAction* action : menu->actions()) {
            listed->append(action->text());
            if (action->text() == caption) {
                chosen = true;
                action->trigger();
                break;
            }
        }
        poll.stop();
        menu->close();
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&popup]() {
        if (popup != nullptr) {
            popup->close();
            return;
        }
        if (QWidget* active = QApplication::activePopupWidget(); active != nullptr) {
            active->close();
        }
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* menu = qobject_cast<QMenu*>(widget);
            if (menu != nullptr && menu->isVisible()) {
                menu->close();
            }
        }
    });
    watchdog.start(2000);
    poll.start();
    QAbstractButton* tables = ribbon_button_for(window, msqt::RibbonCommand::data_tables);
    if (tables == nullptr) {
        poll.stop();
        watchdog.stop();
        return false;
    }
    tables->click();
    poll.stop();
    watchdog.stop();
    QApplication::processEvents();
    return chosen;
}

bool queue_table_date_format(msqt::MainWindow& window, int format) {
    bool seen = false;
    QTimer::singleShot(0, [&seen, format]() {
        auto* dialog =
            dynamic_cast<msqt::TableDateFormatDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        seen = true;
        const int index = dialog->date_format_combo()->findData(QVariant(format));
        if (index >= 0) {
            dialog->date_format_combo()->setCurrentIndex(index);
        }
        dialog->accept();
    });
    QAction* action = gate::find_action(window, app::MenuAction::table_date_format);
    if (action == nullptr || !action->isEnabled()) {
        return false;
    }
    action->trigger();
    QApplication::processEvents();
    return seen;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();
    if (argc < 3) {
        std::fprintf(stderr, "usage: qt_table_date_format <wave31-tables.mpp> <out-dir>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);

    // A missing committed fixture FAILS the gate (wave-33 brief section 1).
    gate::check(std::filesystem::exists(fixture.toStdString()),
                "the committed fixture wave31-tables.mpp is present");
    if (!std::filesystem::exists(fixture.toStdString())) {
        return gate::finish();
    }

    const QString copy = out_dir + QStringLiteral("/w33-table-date-format-source.mpp");
    std::error_code copy_error;
    std::filesystem::copy_file(fixture.toStdString(), copy.toStdString(),
                               std::filesystem::copy_options::overwrite_existing,
                               copy_error);
    gate::check(!copy_error, "the fixture copy is written under the build tree");
    if (copy_error) {
        return gate::finish();
    }

    const StoredTables source = read_tables(copy.toStdString());
    gate::check(source.ok, "the copied fixture's 214/CTable parses");
    gate::check(source.tables.size() == 2, "the fixture stores two task tables");
    if (!source.ok || source.tables.size() != 2) {
        return gate::finish();
    }
    gate::check(source.tables[0].date_format == msproject::viewstate::kDefaultDateFormat,
                "Entry starts with the 0xff default date format");
    gate::check(source.tables[1].date_format == msproject::viewstate::kDefaultDateFormat,
                "Review starts with the 0xff default date format");

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    gate::check(window.open_file(copy, &error), "the copied fixture opens");
    window.show();
    QApplication::processEvents();
    if (window.sheet() == nullptr) {
        return gate::finish();
    }

    msqt::SheetTableModel* model = window.sheet_table_model();
    gate::check(model->visible_column_for_field(kIndicatorsField) != core::sheet::kNoIndex,
                "the load shows Entry's Indicators column");

    // --- switch the view to Review through the real Tables control ------------
    QStringList listed;
    gate::check(apply_table_via_ribbon(window, QStringLiteral("Review"), &listed),
                "the Tables control lists Review and applies it");
    gate::check(listed.contains(QStringLiteral("Entry")) &&
                    listed.contains(QStringLiteral("Review")),
                "the Tables popup lists both stored tables");
    gate::check(model->visible_column_for_field(kText1Field) != core::sheet::kNoIndex,
                "the view shows Review's Text1 column after the switch");
    gate::check(model->visible_column_for_field(kIndicatorsField) == core::sheet::kNoIndex,
                "the view no longer shows Entry's Indicators column");

    // --- the dialog's format 4 must address the SHOWN (second) table ----------
    gate::check(queue_table_date_format(window, 4),
                "the Table / Date Format dialog is live and accepts format 4");
    gate::check(window.document_dirty(), "the date-format request makes the document dirty");

    const QString target = out_dir + QStringLiteral("/w33-table-date-format.mpp");
    std::filesystem::remove(target.toStdString(), copy_error);
    gate::check(window.save_document_path(target, &error), "the date-format save succeeds");

    const StoredTables saved = read_tables(target.toStdString());
    gate::check(saved.ok, "the saved file's 214/CTable parses");
    gate::check(saved.tables.size() == 2, "the saved file still stores two task tables");
    if (!saved.ok || saved.tables.size() != 2) {
        return gate::finish();
    }
    gate::check(saved.tables[1].date_format == 4,
                "the second (shown) table's date format is 4 after the save");
    gate::check(saved.tables[0].date_format == source.tables[0].date_format,
                "the first table's date format is unchanged");
    gate::check(table_block(saved, 0) == table_block(source, 0),
                "the first table's stored block is byte-identical");
    gate::check(saved.var2.size() > 11 && source.var2.size() > 11 &&
                    saved.var2[11] == source.var2[11],
                "the applied Entry table's Var2Data date-format byte is unchanged");
    gate::check(saved.tables[1].block_offset == source.tables[1].block_offset &&
                    saved.tables[1].block_length == source.tables[1].block_length,
                "the second table's block keeps its place and length");

    return gate::finish();
}
