// app_qt/tests/format_dialog_gate_test.cpp -- wave 29 G2's structural gate for
// the Gantt Chart Format tab's Text Styles, Gridlines and Bar Styles dialogs.
//
// The measured source is the local, git-ignored wave-29 capture set.  The
// numbers the gate compares against are committed in
// tests/data/reference/w29-format-dialogs.measures.json (client-relative
// rectangles plus the captured control counts).  Nothing here reads the port's
// own constants for a measured value: the JSON is parsed and the LIVE widget
// tree is measured.
//
// Each dialog is constructed and SHOWN offscreen without exec(), then its size,
// geometry, control inventories, labels, enabled states and exact disabled
// tooltips are checked.  The functional paths are driven through the live
// widgets, and the session apply path is driven through MainWindow and read
// back from the sheet model's FontRole and the Gantt layout's bar labels.
#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFont>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QListWidget>
#include <QPoint>
#include <QPushButton>
#include <QRadioButton>
#include <QRect>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableView>
#include <QTableWidgetItem>
#include <QWidget>

#include <cstdio>
#include <string>
#include <vector>

#include "format_dialogs_qt.h"
#include "gate_util.h"
#include "main_window.h"  // configure_application()
#include "plan_document.h"

namespace {

using core::gantt::BarLabelSide;
using msproject::viewstate::BarStyle;
using msproject::viewstate::GanttStyle;
using msproject::viewstate::Rgb;
using msproject::viewstate::TextStyle;
using msqt::BarStylesDialog;
using msqt::GridlinesDialog;
using msqt::TextStylesDialog;
using msqt::format_dialog_geometry::kNotImplemented;

const QString kTip = QString::fromUtf8(kNotImplemented);

QJsonObject load_measures(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.object();
}

GanttStyle load_style(const QString& data_dir) {
    msqt::PlanDocument document;
    std::string error;
    const QString path = QStringLiteral("%1/synthetic-base.mpp").arg(data_dir);
    if (!document.load(path.toStdString(), app::ZoomLevel::week, error)) {
        std::fprintf(stderr, "FAIL: the fixture loads: %s\n", error.c_str());
        ++gate::failures;
        return {};
    }
    return document.gantt_style();
}

QLabel* direct_label(QWidget* parent, const QString& text) {
    for (QLabel* label : parent->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (label->text() == text) {
            return label;
        }
    }
    return nullptr;
}

QGroupBox* direct_group(QWidget* parent, const QString& title) {
    for (QGroupBox* box : parent->findChildren<QGroupBox*>(QString(),
                                                          Qt::FindDirectChildrenOnly)) {
        if (box->title() == title) {
            return box;
        }
    }
    return nullptr;
}

void check_rect(const QJsonObject& controls, const char* key, QWidget* widget,
                QWidget* dialog) {
    if (widget == nullptr) {
        std::fprintf(stderr, "FAIL: the live widget for '%s' exists\n", key);
        ++gate::failures;
        return;
    }
    const QJsonArray want = controls.value(QLatin1String(key)).toArray();
    const QPoint origin = widget->mapTo(dialog, QPoint(0, 0));
    const QRect got(origin, widget->size());
    const bool ok = want.size() == 4 && got.x() == want.at(0).toInt() &&
                    got.y() == want.at(1).toInt() &&
                    got.width() == want.at(2).toInt() &&
                    got.height() == want.at(3).toInt();
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL: live rect %s: got [%d, %d, %d, %d] want [%d, %d, %d, %d]\n",
                     key, got.x(), got.y(), got.width(), got.height(),
                     want.size() == 4 ? want.at(0).toInt() : -1,
                     want.size() == 4 ? want.at(1).toInt() : -1,
                     want.size() == 4 ? want.at(2).toInt() : -1,
                     want.size() == 4 ? want.at(3).toInt() : -1);
        ++gate::failures;
    }
}

void check_fixed(QDialog& dialog, const QJsonObject& spec, const char* what) {
    gate::check(dialog.width() == spec.value(QStringLiteral("width")).toInt() &&
                    dialog.height() == spec.value(QStringLiteral("height")).toInt(),
                what);
    gate::check(dialog.minimumSize() == dialog.maximumSize(),
                "the dialog is non-resizable");
}

QStringList combo_items(const QComboBox* combo) {
    QStringList items;
    for (int index = 0; index < combo->count(); ++index) {
        items << combo->itemText(index);
    }
    return items;
}

int find_bar_row(const QTableWidget* table, const QString& name) {
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* item = table->item(row, 0);
        if (item != nullptr && item->text() == name) {
            return row;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Text Styles
// ---------------------------------------------------------------------------
void check_text_styles(const QJsonObject& measures, const GanttStyle& style) {
    const QJsonObject spec = measures.value(QStringLiteral("text_styles")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Text Styles");
    gate::check(style.decoded, "the fixture's Gantt style decodes");
    if (!style.decoded) {
        return;
    }
    const QJsonObject controls = spec.value(QStringLiteral("controls")).toObject();

    TextStylesDialog dialog(style);
    dialog.show();
    QApplication::processEvents();

    check_fixed(dialog, spec, "Text Styles has the measured fixed size");
    gate::check(dialog.windowTitle() == QStringLiteral("Text Styles"),
                "Text Styles has the captured title");

    check_rect(controls, "item_label", direct_label(&dialog, QStringLiteral("&Item to Change:")),
               &dialog);
    check_rect(controls, "item_combo", dialog.item_combo(), &dialog);
    check_rect(controls, "font_label", direct_label(&dialog, QStringLiteral("&Font:")), &dialog);
    check_rect(controls, "font_combo", dialog.font_combo(), &dialog);
    check_rect(controls, "font_style_label",
               direct_label(&dialog, QStringLiteral("Font st&yle:")), &dialog);
    check_rect(controls, "font_style_combo", dialog.font_style_combo(), &dialog);
    check_rect(controls, "size_label", direct_label(&dialog, QStringLiteral("&Size:")), &dialog);
    check_rect(controls, "size_combo", dialog.size_combo(), &dialog);
    check_rect(controls, "ok", dialog.ok_button(), &dialog);
    check_rect(controls, "cancel", dialog.cancel_button(), &dialog);
    check_rect(controls, "underline", dialog.underline_box(), &dialog);
    check_rect(controls, "strikethrough", dialog.strikethrough_box(), &dialog);
    check_rect(controls, "colour_label", direct_label(&dialog, QStringLiteral("&Color:")),
               &dialog);
    check_rect(controls, "colour_combo", dialog.colour_combo(), &dialog);
    check_rect(controls, "sample_box", direct_group(&dialog, QStringLiteral("Sample")), &dialog);
    check_rect(controls, "sample_label", dialog.sample_label(), &dialog);
    check_rect(controls, "background_label",
               direct_label(&dialog, QStringLiteral("&Background Color:")), &dialog);
    check_rect(controls, "background_combo", dialog.background_combo(), &dialog);
    check_rect(controls, "pattern_label",
               direct_label(&dialog, QStringLiteral("Background &Pattern:")), &dialog);
    check_rect(controls, "pattern_combo", dialog.pattern_combo(), &dialog);
    check_rect(controls, "note", dialog.note_label(), &dialog);

    // The captured item list, in Project's order, and the three captured lists.
    const QStringList items = combo_items(dialog.item_combo());
    gate::check(items.size() == spec.value(QStringLiteral("item_count")).toInt(),
                "Text Styles lists the 19 captured items");
    const QStringList expected_items = {
        QStringLiteral("All"),
        QStringLiteral("Row & Column Titles"),
        QStringLiteral("Critical Tasks"),
        QStringLiteral("External Tasks"),
        QStringLiteral("Highlighted Tasks"),
        QStringLiteral("Marked Tasks"),
        QStringLiteral("Noncritical Tasks"),
        QStringLiteral("Milestone Tasks"),
        QStringLiteral("Summary Tasks"),
        QStringLiteral("Top Timescale Tier"),
        QStringLiteral("Middle Timescale Tier"),
        QStringLiteral("Bottom Timescale Tier"),
        QStringLiteral("Bar Text - Left"),
        QStringLiteral("Bar Text - Right"),
        QStringLiteral("Bar Text - Top"),
        QStringLiteral("Bar Text - Bottom"),
        QStringLiteral("Bar Text - Inside"),
        QStringLiteral("Changed Cells"),
        QStringLiteral("Inactive Tasks"),
    };
    gate::check(items == expected_items, "the Text Styles item order is the capture's");

    gate::check(combo_items(dialog.font_style_combo()) ==
                    QStringList({QStringLiteral("Regular"), QStringLiteral("Italic"),
                                 QStringLiteral("Bold"), QStringLiteral("Bold Italic")}),
                "the font-style list is the captured four");
    gate::check(dialog.font_style_combo()->count() ==
                    spec.value(QStringLiteral("font_style_count")).toInt(),
                "the font-style list has the captured count");
    const QStringList expected_sizes = {
        QStringLiteral("8"),  QStringLiteral("9"),  QStringLiteral("10"),
        QStringLiteral("11"), QStringLiteral("12"), QStringLiteral("14"),
        QStringLiteral("16"), QStringLiteral("18"), QStringLiteral("20"),
        QStringLiteral("22"), QStringLiteral("24"), QStringLiteral("26"),
        QStringLiteral("28"), QStringLiteral("36"), QStringLiteral("48"),
        QStringLiteral("72")};
    gate::check(combo_items(dialog.size_combo()) == expected_sizes,
                "the font-size list is the captured sixteen");
    gate::check(dialog.size_combo()->count() ==
                    spec.value(QStringLiteral("size_count")).toInt(),
                "the font-size list has the captured count");
    gate::check(dialog.pattern_combo()->count() ==
                    spec.value(QStringLiteral("pattern_count")).toInt(),
                "the background-pattern list has the captured count");
    gate::check(dialog.pattern_combo()->itemText(0) == QStringLiteral("Hollow") &&
                    dialog.pattern_combo()->itemText(13) == QStringLiteral("Dark Dither"),
                "the background-pattern list is the capture's Hollow..Dark Dither");

    // Background patterns are laid out but disabled (no pattern painter).
    gate::check(!dialog.pattern_combo()->isEnabled(),
                "the background-pattern control is disabled");
    gate::check(dialog.pattern_combo()->toolTip() == kTip,
                "the background-pattern control carries decision 9's exact tooltip");

    // W29's last two decoded slots: Changed Cells and Inactive Tasks.
    dialog.select_item(17);
    gate::check(dialog.item_combo()->currentText() == QStringLiteral("Changed Cells"),
                "item 17 is Changed Cells");
    gate::check(dialog.colour_combo()->currentText() == QStringLiteral("RGB(0, 0, 0)"),
                "Changed Cells shows its explicit black foreground");
    gate::check(dialog.background_combo()->currentText() ==
                    QStringLiteral("RGB(239, 243, 255)"),
                "Changed Cells shows its stored pale-blue background");
    dialog.select_item(18);
    gate::check(dialog.item_combo()->currentText() == QStringLiteral("Inactive Tasks"),
                "item 18 is Inactive Tasks");
    gate::check(dialog.strikethrough_box()->isChecked(),
                "Inactive Tasks shows its stored strikethrough");
    gate::check(dialog.colour_combo()->currentText() ==
                    QStringLiteral("RGB(192, 192, 192)"),
                "Inactive Tasks shows its stored grey foreground");
    gate::check(dialog.background_combo()->currentText() == QStringLiteral("Automatic"),
                "Inactive Tasks background is automatic");

    // The session-only note and a live OK.
    gate::check(dialog.ok_button()->isEnabled(), "Text Styles OK is enabled for a decoded style");
    gate::check(dialog.note_label()->text().contains(
                    QStringLiteral("Save keeps the file's bytes")),
                "Text Styles says the session apply leaves the file's bytes");
    gate::check(dialog.note_label()->text().contains(
                    QStringLiteral("not implemented in this port")),
                "Text Styles names the unimplemented background patterns");

    // All applies a changed field to every stored style.
    dialog.select_item(0);
    gate::check(dialog.item_combo()->currentText() == QStringLiteral("All"),
                "item 0 is All");
    const int size_20 = dialog.size_combo()->findText(QStringLiteral("20"));
    gate::check(size_20 >= 0, "the All edit finds size 20 in the live combo");
    dialog.size_combo()->setCurrentIndex(size_20);
    gate::check(dialog.changed(), "changing a field marks Text Styles changed");
    dialog.ok_button()->click();
    bool all_20 = true;
    for (const TextStyle& text : dialog.style().text) {
        all_20 = all_20 && text.point_size == 20;
    }
    gate::check(all_20, "All writes the changed size to all 19 stored styles");

    // A single item edits only its own slot.
    TextStylesDialog one(style);
    one.show();
    QApplication::processEvents();
    one.select_item(17);
    const bool inactive_bold = style.text[18].bold;
    one.font_style_combo()->setCurrentIndex(2);  // Bold
    one.ok_button()->click();
    gate::check(one.style().text[17].bold, "Changed Cells takes the Bold edit");
    gate::check(one.style().text[18].bold == inactive_bold,
                "a single-item edit leaves Inactive Tasks alone");

    // A file without the style record: everything disabled, exact tooltip.
    GanttStyle undecoded;
    TextStylesDialog none(undecoded);
    none.show();
    QApplication::processEvents();
    gate::check(!none.style_available(), "a file without Text Styles says so");
    gate::check(!none.ok_button()->isEnabled(), "Text Styles OK is disabled with no style");
    gate::check(!none.item_combo()->isEnabled() && none.item_combo()->toolTip() == kTip,
                "the item combo is disabled with decision 9's exact tooltip");
    gate::check(none.note_label()->text().contains(QStringLiteral("No Gantt style record")),
                "the no-style note is the honest one");
}

// ---------------------------------------------------------------------------
// Gridlines
// ---------------------------------------------------------------------------
void check_gridlines(const QJsonObject& measures) {
    const QJsonObject spec = measures.value(QStringLiteral("gridlines")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Gridlines");
    const QJsonObject controls = spec.value(QStringLiteral("controls")).toObject();

    GridlinesDialog dialog;
    dialog.show();
    QApplication::processEvents();

    check_fixed(dialog, spec, "Gridlines has the measured fixed size");
    gate::check(dialog.windowTitle() == QStringLiteral("Gridlines"),
                "Gridlines has the captured title");

    QGroupBox* normal = direct_group(&dialog, QStringLiteral("Normal"));
    QGroupBox* interval = direct_group(&dialog, QStringLiteral("At interval"));
    gate::check(normal != nullptr && interval != nullptr,
                "Gridlines has the two captured groups");

    check_rect(controls, "line_label",
               direct_label(&dialog, QStringLiteral("&Line to change:")), &dialog);
    check_rect(controls, "line_list", dialog.line_list(), &dialog);
    check_rect(controls, "normal_group", normal, &dialog);
    check_rect(controls, "normal_type_label",
               normal != nullptr ? direct_label(normal, QStringLiteral("&Type:")) : nullptr,
               &dialog);
    check_rect(controls, "normal_type_combo", dialog.normal_type_combo(), &dialog);
    check_rect(controls, "normal_colour_label",
               normal != nullptr ? direct_label(normal, QStringLiteral("&Color:")) : nullptr,
               &dialog);
    check_rect(controls, "normal_colour_combo", dialog.normal_colour_combo(), &dialog);
    check_rect(controls, "interval_group", interval, &dialog);
    check_rect(controls, "none_radio", dialog.none_radio(), &dialog);
    check_rect(controls, "interval_2_radio", dialog.interval_2_radio(), &dialog);
    check_rect(controls, "interval_3_radio", dialog.interval_3_radio(), &dialog);
    check_rect(controls, "interval_4_radio", dialog.interval_4_radio(), &dialog);
    check_rect(controls, "other_radio", dialog.other_radio(), &dialog);
    check_rect(controls, "other_spin", dialog.other_spin(), &dialog);
    check_rect(controls, "interval_type_label",
               interval != nullptr ? direct_label(interval, QStringLiteral("T&ype:")) : nullptr,
               &dialog);
    check_rect(controls, "interval_type_combo", dialog.interval_type_combo(), &dialog);
    check_rect(controls, "interval_colour_label",
               interval != nullptr ? direct_label(interval, QStringLiteral("Colo&r:")) : nullptr,
               &dialog);
    check_rect(controls, "interval_colour_combo", dialog.interval_colour_combo(), &dialog);
    check_rect(controls, "help", dialog.help_button(), &dialog);
    check_rect(controls, "ok", dialog.ok_button(), &dialog);
    check_rect(controls, "cancel", dialog.cancel_button(), &dialog);

    const QStringList lines = {
        QStringLiteral("Gantt Rows"),       QStringLiteral("Bar Rows"),
        QStringLiteral("Middle Tier Column"), QStringLiteral("Bottom Tier Column"),
        QStringLiteral("Current Date"),     QStringLiteral("Sheet Rows"),
        QStringLiteral("Sheet Columns"),    QStringLiteral("Title Vertical"),
        QStringLiteral("Title Horizontal"), QStringLiteral("Page Breaks"),
        QStringLiteral("Project Start"),    QStringLiteral("Project Finish"),
        QStringLiteral("Status Date"),      QStringLiteral("Top Tier Column")};
    QStringList live_lines;
    for (int row = 0; row < dialog.line_list()->count(); ++row) {
        live_lines << dialog.line_list()->item(row)->text();
    }
    gate::check(live_lines == lines, "the line list is the capture's fourteen");
    gate::check(dialog.line_list()->count() ==
                    spec.value(QStringLiteral("line_count")).toInt(),
                "the line list has the captured count");

    const QStringList types = {QStringLiteral("No Line"), QStringLiteral("Continuous"),
                               QStringLiteral("Dash"), QStringLiteral("Dot"),
                               QStringLiteral("Dash Dot")};
    gate::check(combo_items(dialog.normal_type_combo()) == types,
                "the Normal type list is the capture's five");
    gate::check(combo_items(dialog.interval_type_combo()) == types,
                "the At-interval type list is the capture's five");
    gate::check(dialog.normal_type_combo()->count() ==
                    spec.value(QStringLiteral("type_count")).toInt(),
                "the type list has the captured count");
    gate::check(dialog.normal_colour_combo()->currentText() == QStringLiteral("Automatic") &&
                    dialog.interval_colour_combo()->currentText() == QStringLiteral("Automatic"),
                "both colour combos show Automatic");

    // Every control is disabled with decision 9's exact tooltip; OK too.
    gate::check(!dialog.line_list()->isEnabled() &&
                    dialog.line_list()->toolTip() == kTip,
                "the line list is disabled with the exact tooltip");
    gate::check(!dialog.normal_type_combo()->isEnabled() &&
                    dialog.normal_type_combo()->toolTip() == kTip,
                "the Normal type combo is disabled with the exact tooltip");
    gate::check(!dialog.normal_colour_combo()->isEnabled() &&
                    dialog.normal_colour_combo()->toolTip() == kTip,
                "the Normal colour combo is disabled with the exact tooltip");
    gate::check(!dialog.interval_type_combo()->isEnabled() &&
                    !dialog.interval_colour_combo()->isEnabled(),
                "the At-interval combos are disabled");
    gate::check(!dialog.none_radio()->isEnabled() &&
                    dialog.none_radio()->toolTip() == kTip,
                "the interval radios are disabled with the exact tooltip");
    gate::check(dialog.none_radio()->isChecked(),
                "the captured default interval is None");
    gate::check(!dialog.interval_2_radio()->isEnabled() &&
                    !dialog.interval_3_radio()->isEnabled() &&
                    !dialog.interval_4_radio()->isEnabled() &&
                    !dialog.other_radio()->isEnabled(),
                "every interval radio is disabled");
    gate::check(!dialog.other_spin()->isEnabled() && dialog.other_spin()->toolTip() == kTip,
                "the Other spin box is disabled with the exact tooltip");
    gate::check(!dialog.help_button()->isEnabled() &&
                    dialog.help_button()->toolTip() == kTip,
                "Help is disabled with the exact tooltip");
    gate::check(!dialog.ok_button()->isEnabled() && dialog.ok_button()->toolTip() == kTip,
                "OK is disabled with the exact tooltip");
    gate::check(dialog.cancel_button()->isEnabled(), "Cancel is enabled");

    gate::check(dialog.note_label()->text() ==
                    QStringLiteral(
                        "Gridlines are not implemented in this port; OK is disabled."),
                "the inline note is the honest decision-15 note");
    gate::check(dialog.note_label()->isVisible(), "the inline note is visible");
    gate::check(interval != nullptr && dialog.note_label()->parentWidget() == interval,
                "the note sits inside the At-interval group as captured");
}

// ---------------------------------------------------------------------------
// Bar Styles
// ---------------------------------------------------------------------------
void check_bar_styles(const QJsonObject& measures, const GanttStyle& style) {
    const QJsonObject spec = measures.value(QStringLiteral("bar_styles")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Bar Styles");
    gate::check(style.decoded && !style.bars.empty(),
                "the fixture's bar styles decode");
    if (!style.decoded || style.bars.empty()) {
        return;
    }
    const QJsonObject controls = spec.value(QStringLiteral("controls")).toObject();

    BarStylesDialog dialog(style);
    dialog.show();
    QApplication::processEvents();

    check_fixed(dialog, spec, "Bar Styles has the measured fixed size");
    gate::check(dialog.windowTitle() == QStringLiteral("Bar Styles"),
                "Bar Styles has the captured title");

    check_rect(controls, "cut", dialog.cut_button(), &dialog);
    check_rect(controls, "paste", dialog.paste_button(), &dialog);
    check_rect(controls, "insert", dialog.insert_button(), &dialog);
    check_rect(controls, "table", dialog.style_table(), &dialog);
    check_rect(controls, "tabs", dialog.tabs(), &dialog);
    check_rect(controls, "note", dialog.note_label(), &dialog);
    check_rect(controls, "help", dialog.help_button(), &dialog);
    check_rect(controls, "ok", dialog.ok_button(), &dialog);
    check_rect(controls, "cancel", dialog.cancel_button(), &dialog);

    // The six captured columns and their measured widths.
    QTableWidget* table = dialog.style_table();
    const QStringList headers = {
        QStringLiteral("Name"), QStringLiteral("Appearance"),
        QStringLiteral("Show For ... Tasks"), QStringLiteral("Row"),
        QStringLiteral("From"), QStringLiteral("To")};
    QStringList live_headers;
    for (int column = 0; column < table->columnCount(); ++column) {
        live_headers << table->horizontalHeaderItem(column)->text();
    }
    gate::check(live_headers == headers, "Bar Styles has the six captured columns");
    gate::check(table->columnWidth(0) == spec.value(QStringLiteral("name_column")).toInt() &&
                    table->columnWidth(1) ==
                        spec.value(QStringLiteral("appearance_column")).toInt() &&
                    table->columnWidth(2) ==
                        spec.value(QStringLiteral("show_for_column")).toInt() &&
                    table->columnWidth(3) ==
                        spec.value(QStringLiteral("row_column")).toInt() &&
                    table->columnWidth(4) ==
                        spec.value(QStringLiteral("from_column")).toInt() &&
                    table->columnWidth(5) ==
                        spec.value(QStringLiteral("to_column")).toInt(),
                "the six column widths are the capture's");

    gate::check(table->rowCount() == static_cast<int>(style.bars.size()),
                "the table lists every decoded bar style");
    // W29's corrected row decode: every stock record stores row 0 (display 1).
    bool every_row_is_1 = true;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* item = table->item(row, 3);
        every_row_is_1 = every_row_is_1 && item != nullptr &&
                         item->text() == QStringLiteral("1");
    }
    gate::check(every_row_is_1,
                "every stock bar row shows the stored 0 as Row 1");

    const int task_row = find_bar_row(table, QStringLiteral("Task"));
    gate::check(task_row >= 0, "the stock Task bar style is in the table");
    if (task_row >= 0) {
        gate::check(table->item(task_row, 4)->text() == QStringLiteral("Task Start") &&
                        table->item(task_row, 5)->text() == QStringLiteral("Task Finish"),
                    "the Task row shows Task Start -> Task Finish");
    }
    const int milestone_row = find_bar_row(table, QStringLiteral("Milestone"));
    gate::check(milestone_row >= 0, "the stock Milestone bar style is in the table");
    if (milestone_row >= 0) {
        gate::check(table->item(milestone_row, 4)->text() ==
                            QStringLiteral("Task Finish") &&
                        table->item(milestone_row, 5)->text() ==
                            QStringLiteral("Task Finish"),
                    "the Milestone row shows Task Finish on both sides");
    }

    // The two captured tabs and the Text page's five rows.
    gate::check(dialog.tabs()->count() == 2 &&
                    dialog.tabs()->tabText(0) == QStringLiteral("Text") &&
                    dialog.tabs()->tabText(1) == QStringLiteral("Bars"),
                "the dialog has the captured Text and Bars tabs");
    QTableWidget* text_table = dialog.text_table();
    gate::check(text_table->rowCount() == spec.value(QStringLiteral("text_rows")).toInt(),
                "the Text page has the five captured rows");
    const QStringList sides = {QStringLiteral("Left"), QStringLiteral("Right"),
                               QStringLiteral("Top"), QStringLiteral("Bottom"),
                               QStringLiteral("Inside")};
    QStringList live_sides;
    for (int row = 0; row < text_table->rowCount(); ++row) {
        live_sides << text_table->item(row, 0)->text();
    }
    gate::check(live_sides == sides, "the Text page rows are Left..Inside");
    QComboBox* fields[5] = {dialog.left_field_combo(), dialog.right_field_combo(),
                            dialog.top_field_combo(), dialog.bottom_field_combo(),
                            dialog.inside_field_combo()};
    bool fields_ok = true;
    for (QComboBox* combo : fields) {
        fields_ok = fields_ok && combo != nullptr &&
                    combo->count() == spec.value(QStringLiteral("text_field_count")).toInt() &&
                    combo->itemText(0) == QStringLiteral("None");
    }
    gate::check(fields_ok, "the five Text page combos carry the captured field list");

    // Bars page: the three captured groups and their control lists.
    QWidget* bars_page = dialog.tabs()->widget(1);
    gate::check(bars_page != nullptr, "the Bars page exists");
    QGroupBox* start_group =
        bars_page != nullptr ? direct_group(bars_page, QStringLiteral("Start")) : nullptr;
    QGroupBox* middle_group =
        bars_page != nullptr ? direct_group(bars_page, QStringLiteral("Middle")) : nullptr;
    QGroupBox* end_group =
        bars_page != nullptr ? direct_group(bars_page, QStringLiteral("End")) : nullptr;
    gate::check(start_group != nullptr && middle_group != nullptr && end_group != nullptr,
                "the Bars page has the Start, Middle and End groups");
    if (start_group != nullptr && middle_group != nullptr && end_group != nullptr) {
        const QJsonArray start_size =
            spec.value(QStringLiteral("start_group_size")).toArray();
        const QJsonArray middle_size =
            spec.value(QStringLiteral("middle_group_size")).toArray();
        const QJsonArray end_size = spec.value(QStringLiteral("end_group_size")).toArray();
        gate::check(start_size.size() == 2 &&
                        start_group->width() == start_size.at(0).toInt() &&
                        start_group->height() == start_size.at(1).toInt(),
                    "the Start group has the captured size");
        gate::check(middle_size.size() == 2 &&
                        middle_group->width() == middle_size.at(0).toInt() &&
                        middle_group->height() == middle_size.at(1).toInt(),
                    "the Middle group has the captured size");
        gate::check(end_size.size() == 2 &&
                        end_group->width() == end_size.at(0).toInt() &&
                        end_group->height() == end_size.at(1).toInt(),
                    "the End group has the captured size");
    }
    gate::check(dialog.start_shape_combo()->count() ==
                    spec.value(QStringLiteral("shape_count")).toInt() &&
                    dialog.end_shape_combo()->count() ==
                        spec.value(QStringLiteral("shape_count")).toInt(),
                "the start and end shape lists have the captured 25 entries");
    gate::check(dialog.middle_shape_combo()->count() ==
                    spec.value(QStringLiteral("middle_shape_count")).toInt(),
                "the middle shape list has the captured eight entries");
    gate::check(dialog.middle_pattern_combo()->count() ==
                    spec.value(QStringLiteral("middle_pattern_count")).toInt(),
                "the middle pattern list has the captured twelve entries");
    gate::check(dialog.start_type_combo()->count() ==
                        spec.value(QStringLiteral("type_count")).toInt() &&
                    dialog.end_type_combo()->count() ==
                        spec.value(QStringLiteral("type_count")).toInt(),
                "the start and end type lists have the captured three entries");

    // Appearance editing has no engine: shape/type/pattern disabled, exact tip.
    gate::check(!dialog.start_shape_combo()->isEnabled() &&
                    dialog.start_shape_combo()->toolTip() == kTip,
                "the start shape combo is disabled with the exact tooltip");
    gate::check(!dialog.start_type_combo()->isEnabled() &&
                    !dialog.end_shape_combo()->isEnabled() &&
                    !dialog.end_type_combo()->isEnabled(),
                "the other shape/type combos are disabled");
    gate::check(!dialog.middle_shape_combo()->isEnabled() &&
                    !dialog.middle_pattern_combo()->isEnabled(),
                "the middle shape/pattern combos are disabled");
    gate::check(dialog.start_colour_combo()->isEnabled() &&
                    dialog.middle_colour_combo()->isEnabled() &&
                    dialog.end_colour_combo()->isEnabled(),
                "the three colour combos are functional");

    // Row operations have no engine; Help is inert; OK is live.
    gate::check(!dialog.cut_button()->isEnabled() && dialog.cut_button()->toolTip() == kTip &&
                    !dialog.paste_button()->isEnabled() && !dialog.insert_button()->isEnabled(),
                "Cut/Paste/Insert Row are disabled with the exact tooltip");
    gate::check(!dialog.help_button()->isEnabled() && dialog.help_button()->toolTip() == kTip,
                "Bar Styles Help is disabled with the exact tooltip");
    gate::check(dialog.ok_button()->isEnabled(), "Bar Styles OK is enabled for a decoded style");
    gate::check(dialog.note_label()->text().contains(
                    QStringLiteral("Save keeps the file's bytes")) &&
                    dialog.note_label()->text().contains(
                        QStringLiteral("not implemented in this port")),
                "Bar Styles carries the session note and names the appearance gap");

    // Functional path: the Task row's text fields and start colour.
    dialog.select_row(task_row);
    const int name_index = dialog.left_field_combo()->findText(QStringLiteral("Name"));
    const int finish_index = dialog.right_field_combo()->findText(QStringLiteral("Finish"));
    const int red_index = dialog.start_colour_combo()->findText(
        QStringLiteral("RGB(255, 0, 0)"));
    gate::check(name_index >= 0 && finish_index >= 0 && red_index >= 0,
                "the Bar Styles edits find their live combo entries");
    dialog.left_field_combo()->setCurrentIndex(name_index);
    dialog.right_field_combo()->setCurrentIndex(finish_index);
    dialog.start_colour_combo()->setCurrentIndex(red_index);
    gate::check(dialog.changed(), "editing a bar field marks Bar Styles changed");
    dialog.ok_button()->click();
    bool task_edited = false;
    for (const BarStyle& bar : dialog.style().bars) {
        if (bar.name == "Task") {
            task_edited = bar.left_text == 0x0b40000eu &&
                          bar.right_text == 0x0b400504u && bar.has_start_colour &&
                          bar.start_colour == (Rgb{255, 0, 0});
        }
    }
    gate::check(task_edited,
                "OK keeps the five text fields and the colour the live controls showed");

    // A file without a decoded bar style: nothing to apply.
    GanttStyle undecoded;
    BarStylesDialog none(undecoded);
    none.show();
    QApplication::processEvents();
    gate::check(!none.style_available(), "a file without bar styles says so");
    gate::check(!none.ok_button()->isEnabled(), "Bar Styles OK is disabled with no style");
    gate::check(!none.style_table()->isEnabled(), "the empty style table is disabled");
    gate::check(none.note_label()->text().contains(QStringLiteral("No decoded bar style")),
                "the no-bar-style note is the honest one");
}

// ---------------------------------------------------------------------------
// The session apply path (MainWindow -> PlanDocument -> sheet and Gantt)
// ---------------------------------------------------------------------------
void check_session_apply(const GanttStyle& style, const QString& data_dir) {
    const QString path = QStringLiteral("%1/synthetic-base.mpp").arg(data_dir);
    msqt::MainWindow window;
    QString error;
    gate::check(window.open_file(path, &error),
                "the session-apply fixture opens in the shell");
    if (window.sheet_row_count() == 0) {
        return;
    }
    QAbstractItemModel* model = window.sheet_view()->model();
    gate::check(model != nullptr && model->rowCount() > 0,
                "the sheet model shows the opened rows");
    if (model == nullptr || model->rowCount() == 0) {
        return;
    }

    std::vector<QString> before;
    for (int row = 0; row < model->rowCount(); ++row) {
        before.push_back(model->data(model->index(row, 0), Qt::FontRole)
                             .value<QFont>()
                             .family());
    }

    // Every text style to a measured-different family and size: the sheet's
    // FontRole must read the new values for every task row.
    GanttStyle text_edit = style;
    for (TextStyle& text : text_edit.text) {
        text.font_name = "Courier New";
        text.point_size = 20;
        // A real family edit (TextStylesDialog::apply_field) marks the style
        // usable once it names a family; the gate drives the apply path
        // directly, so it makes the same change the dialog's edit makes.
        text.valid = true;
    }
    window.apply_session_gantt_style(text_edit);
    QApplication::processEvents();

    bool every_row_changed = true;
    for (int row = 0; row < model->rowCount(); ++row) {
        const QFont font =
            model->data(model->index(row, 0), Qt::FontRole).value<QFont>();
        every_row_changed = every_row_changed && font.family() == QStringLiteral("Courier New") &&
                            font.pointSize() == 20;
    }
    gate::check(every_row_changed,
                "Text Styles OK repaints every sheet row from the session style");
    gate::check(before.size() == static_cast<std::size_t>(model->rowCount()),
                "the sheet still has the same rows after the session apply");
    gate::check(!window.document_dirty(),
                "a session style apply is not a file edit");
    gate::check(window.gantt_bar_count() > 0,
                "the Gantt still has bars after the session apply");

    // Every bar style's left label to Name: the rebuilt layout must carry the
    // task name on the left of each bar.
    GanttStyle bar_edit = text_edit;
    for (BarStyle& bar : bar_edit.bars) {
        bar.left_text = 0x0b40000eu;
    }
    window.apply_session_gantt_style(bar_edit);
    QApplication::processEvents();

    const auto task_name = [&window](std::uint32_t uid) -> QString {
        for (const app::TaskRow& task : window.plan().tasks) {
            if (task.uid == uid) {
                return QString::fromStdString(task.name);
            }
        }
        return {};
    };
    bool every_bar_has_its_name = true;
    std::size_t checked_bars = 0;
    for (const core::gantt::Bar& bar : window.gantt().bars) {
        const QString name = task_name(bar.task_uid);
        if (name.isEmpty()) {
            continue;  // the synthetic project row is not a task
        }
        ++checked_bars;
        bool found = false;
        for (const core::gantt::BarText& text : bar.texts) {
            if (text.side == BarLabelSide::left && text.text == name.toStdString()) {
                found = true;
            }
        }
        every_bar_has_its_name = every_bar_has_its_name && found;
    }
    gate::check(checked_bars > 0,
                "the rebuilt Gantt has task bars to read the session labels from");
    gate::check(every_bar_has_its_name,
                "Bar Styles OK rebuilds every bar with the changed left label");
    gate::check(!window.document_dirty(),
                "a session bar-style apply is not a file edit");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: format_dialog_gate <w29-format-dialogs.measures.json> "
                     "<tests/data>\n");
        return 2;
    }
    const QJsonObject measures = load_measures(QString::fromLocal8Bit(argv[1]));
    gate::check(!measures.isEmpty(), "the measures file parses");
    const QString data_dir = QString::fromLocal8Bit(argv[2]);

    const GanttStyle style = load_style(data_dir);
    check_text_styles(measures, style);
    check_gridlines(measures);
    check_bar_styles(measures, style);
    check_session_apply(style, data_dir);

    std::printf("qt_format_dialog_gate: ");
    return gate::finish();
}
