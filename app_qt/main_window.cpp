// app_qt/main_window.cpp -- see main_window.h.

#include "main_window.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QCursor>
#include <QDate>
#include <QDateEdit>
#include <QLocale>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QModelIndex>
#include <QPalette>
#include <QPoint>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStackedWidget>
#include <QStringList>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>
#include <QPushButton>
#include <QRadioButton>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "backstage.h"
#include "command_status.h"
#include "dialogs.h"
#include "document.h"
#include "entry_columns.h"
#include "export_pdf.h"
#include "field_link_edits.h"
#include "fields_model.h"  // app::read_file_formulas (the definition dialog)
#include "format_dialogs_qt.h"  // the Gantt Chart Format tab's dialogs (W29-2.1)
#include "filter_model.h"
#include "gantt_canvas.h"
#include "layout_model.h"
#include "menus.h"
#include "options_dialog.h"
#include "page_setup_dialog.h"
#include "preferences_store.h"
#include "print_page.h"
#include "project_dialogs.h"     // app::ProjectInformation (wave 26 G5)
#include "project_dialogs_qt.h"  // the Project tab's dialogs
#include "reader.h"
#include "reader_adapter.h"  // app::props_stream_from_file
#include "report_dialogs_qt.h"  // the Report tab's dialogs (W29-4.1)
#include "ribbon_bar.h"
#include "selection_model.h"
#include "sheet_views.h"
#include "shell_icons.h"
#include "shell_style.h"
#include "spelling.h"         // app::spelling_issues for the Proofing report
#include "spelling_dialog.h"  // the Proofing Spelling report (W30-3.3)
#include "start_edit_warning.h"
#include "task_form.h"
#include "theme.h"
#include "timeline_strip.h"
#include "usage.h"
#include "usage_grid.h"
#include "usage_sheet.h"
#include "view_label.h"
#include "view_model.h"

namespace msqt {
namespace {

QString& font_family_storage() {
    static QString family;
    return family;
}

QFont fixed_font(int point_size) {
    QFont font = QApplication::font();
    font.setPointSize(point_size);
    return font;
}

// The keyboard-shortcuts panel: the menu model is walked, so the list can never
// drift from the menu bar.
QString shortcuts_text() {
    QString text;
    // W26-6.1: a drop-down's own title is a heading line and its children carry
    // the keys, so the panel lists them too.
    std::function<void(const std::vector<app::MenuItemSpec>&, int)> append_items =
        [&append_items, &text](const std::vector<app::MenuItemSpec>& items, int depth) {
            const QString indent(4 * (depth + 1), QLatin1Char(' '));
            for (const app::MenuItemSpec& item : items) {
                if (item.is_submenu()) {
                    text += indent + QString::fromUtf8(item.title) + QStringLiteral("\n");
                    append_items(item.children, depth + 1);
                    continue;
                }
                const QKeySequence shortcut = menu_shortcut(item);
                if (shortcut.isEmpty()) {
                    continue;
                }
                text += indent + QString::fromUtf8(item.title) + QStringLiteral("    ") +
                        shortcut.toString(QKeySequence::NativeText) + QStringLiteral("\n");
            }
        };
    for (const app::MenuSpec& menu : app::menu_specs()) {
        text += QString::fromUtf8(menu.title) + QStringLiteral("\n");
        append_items(menu.items, 0);
        text += QStringLiteral("\n");
    }
    return text;
}

app::ZoomLevel stepped_zoom(app::ZoomLevel zoom, int delta) {
    int level = static_cast<int>(zoom) + delta;
    if (level < static_cast<int>(app::ZoomLevel::day)) {
        level = static_cast<int>(app::ZoomLevel::day);
    }
    if (level > static_cast<int>(app::ZoomLevel::month)) {
        level = static_cast<int>(app::ZoomLevel::month);
    }
    return static_cast<app::ZoomLevel>(level);
}

QString to_q(const std::string& text) { return QString::fromStdString(text); }

// W26-REV-4 / W29-1.1: the file's default text font -- the first FONT_BASES
// entry (what Project's Font box shows), else the non-critical style.  It is
// both the ribbon's Font value and the font whose metrics OWN the stored sheet
// column width: one stored character is one lowercase-alphabet advance
// (entry_columns.h, column_char_px()).
struct FileFont {
    QString family;
    int point_size = 0;
};

FileFont file_font(const msproject::viewstate::GanttStyle& style) {
    FileFont font;
    if (!style.decoded) {
        return font;
    }
    if (!style.fonts.empty() && style.fonts.front().valid &&
        !style.fonts.front().name.empty()) {
        font.family = to_q(style.fonts.front().name);
        font.point_size = style.fonts.front().point_size;
        return font;
    }
    const std::size_t index =
        static_cast<std::size_t>(msproject::viewstate::TextStyleItem::non_critical);
    if (index < style.text.size() && style.text[index].valid &&
        !style.text[index].font_name.empty()) {
        font.family = to_q(style.text[index].font_name);
        font.point_size = style.text[index].point_size;
    }
    return font;
}

// W28-1.2: Task Information's Percent complete parser.  The measured writer
// stores a u16 percent, so only a whole number 0..100 is accepted; the trailing
// '%' the sheet renders is optional, as in the sheet's own Percent cell.
bool parse_percent_text(const std::string& text, std::uint16_t* percent) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    const auto space = [](char ch) { return ch == ' ' || ch == '\t'; };
    while (begin < end && space(text[begin])) {
        ++begin;
    }
    while (end > begin && space(text[end - 1])) {
        --end;
    }
    if (end > begin && text[end - 1] == '%') {
        --end;
        while (end > begin && space(text[end - 1])) {
            --end;
        }
    }
    if (begin == end) {
        return false;
    }
    unsigned value = 0;
    for (std::size_t index = begin; index < end; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(text[index] - '0');
        if (value > 100) {
            return false;
        }
    }
    *percent = static_cast<std::uint16_t>(value);
    return true;
}

// The status-bar view buttons, in Project 2021's measured order.  Wave 23
// (B-9): five, as in the clean capture -- Gantt Chart, Task Usage, Team Planner,
// Resource Sheet, Report.  Resource Usage is a View-tab command; it was never
// one of the five.  Team Planner and Report have no view in this port yet, so
// they are disabled with the exact decision-9 tooltip (never faked).
struct ViewButtonSpec {
    const char* label;
    app::ViewMode mode;
    bool implemented;
};

const ViewButtonSpec kViewButtons[5] = {
    {"Gantt Chart", app::ViewMode::task_sheet, true},
    {"Task Usage", app::ViewMode::task_usage, true},
    {"Team Planner", app::ViewMode::task_sheet, false},
    {"Resource Sheet", app::ViewMode::resource_sheet, true},
    {"Report", app::ViewMode::task_sheet, false},
};

}  // namespace

QString& ui_font_family() { return font_family_storage(); }

bool ui_font_is_carlito() { return ui_font_family() == QStringLiteral("Carlito"); }

void configure_application() {
    // Fusion everywhere: the same widget painting on Windows, macOS and Linux.
    QApplication::setStyle(QStringLiteral("Fusion"));

    // One palette, built in code: the light Office theme is the parity
    // reference (theme.cpp owns both palettes).  Set after setStyle, because
    // setStyle installs the style's own standard palette.
    apply_theme(Theme::light);

    // Operator decision 13: Carlito ships WITH the shell, it is not assumed to
    // be installed.  The four OFL faces are compiled in as Qt resources
    // (app_qt/CMakeLists.txt), so a bare offscreen runner -- which since Qt 6
    // has no fonts of its own -- measures the same text metrics as a desktop.
    for (const char* face :
         {":/fonts/Carlito-Regular.ttf", ":/fonts/Carlito-Bold.ttf",
          ":/fonts/Carlito-Italic.ttf", ":/fonts/Carlito-BoldItalic.ttf"}) {
        QFontDatabase::addApplicationFont(QString::fromLatin1(face));
    }

    // The fixed family list (shell_style.h).  Carlito is now always first (the
    // bundled faces above); Calibri and the open families remain as fallbacks
    // for a build that somehow did not embed the resource.
    QString family;
    const QStringList available = QFontDatabase::families();
    for (int index = 0; index < kFontFamilyCount; ++index) {
        const QString candidate = QString::fromLatin1(kFontFamilies[index]);
        if (available.contains(candidate, Qt::CaseInsensitive)) {
            family = candidate;
            break;
        }
    }
    if (family.isEmpty()) {
        family = QApplication::font().family();
    }
    font_family_storage() = family;

    QFont font = QApplication::font();
    font.setFamily(family);
    font.setPointSize(kBodyPointSize);
    QApplication::setFont(font);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("MS Project Shell"));
    resize(kWindowWidth, kWindowHeight);
    // The reference opens at the weekly timescale.  A persisted preference may
    // replace this from main.cpp (PreferencesStore); the offscreen tests keep the
    // measured default so the parity numbers cannot depend on the host's store.
    prefs_.zoom = app::ZoomLevel::week;
    sheet_model_.set_preferences(&prefs_);
    // Wave 26 (W26-3.1): the file's text styles.  doc_ is a stable member, so
    // the model keeps this pointer across loads and reads the style record that
    // PlanDocument::load() replaces in place.
    sheet_model_.set_gantt_style(&doc_.gantt_style());
    // Wave 29 (W29-1.2): the same stable-pointer rule for the applied view's
    // stored per-cell fills (PROJECT_A's yellow Highlighted row and the shaded
    // summary rows).
    sheet_model_.set_cell_formats(&doc_.cell_formats());
    // Wave 31 (W31-3.6): the Resource Sheet view's own Text Styles.  `doc_` is
    // a stable member and PlanDocument::load() replaces the record in place, so
    // the pointer survives loads; the adapter only reads it for Resource rows
    // (slot 3 overallocated, slot 2 ordinary).
    sheet_model_.set_resource_style(&doc_.resource_sheet_style().text);
    build_central_widget();
    // Wave 26 (W26-3.2): the same stable pointer for the Gantt's bar palette.
    // GanttCanvas::set_layout() re-reads it on every rebuild.
    gantt_canvas_->set_gantt_style(&doc_.gantt_style());
    build_menus();
    build_quick_access();
    // The File tab opens the backstage shell (operator decision 2).
    ribbon_->set_file_handler([this]() { show_backstage(true); });
    build_status_bar();
    // W26-6.2: paint the shell-owned chrome with the light palette now; every
    // later theme change repaints it through apply_chrome_theme().
    apply_chrome_theme();
    // A new project opens on the current week with the Entry header band, the
    // Start/Finish block and the manual-scheduling callout -- never the engine's
    // 1984 epoch and never an empty caption band.
    if (!doc_.has_document()) {
        configure_task_columns();
        doc_.build_empty_view(prefs_.zoom, app::today_date32());
        gantt_canvas_->set_layout(doc_.gantt());
        refresh_timeline();
        update_chrome();
    }
    // Wave 26: a new project uses Project's defaults (Timeline shown, project
    // row hidden, Critical Tasks OFF).
    ribbon_->set_document_state(doc_.has_document(), timeline_visible_,
                                doc_.project_summary_visible());
    ribbon_->set_critical_tasks(false);
    update_menu_state();
    update_status();
}

MainWindow::~MainWindow() {
    // The model is a value member (destroyed before this window's children), so
    // detach the view first rather than relying on the view's model tracking.
    if (sheet_view_ != nullptr) {
        sheet_view_->setModel(nullptr);
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void MainWindow::build_central_widget() {
    auto* central = new QWidget(this);
    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Ribbon: tabs, groups and labelled buttons (structure and heights, text
    // only; see ribbon_bar.h).
    ribbon_ = new RibbonBar(central);
    ribbon_->set_action_handler([this](app::MenuAction action) {
        if (action == app::MenuAction::none) {
            set_message("this ribbon command is not wired in the Qt shell yet", false);
            update_status();
            return;
        }
        handle_action(action);
    });
    // Wave 26: the %-complete buttons and Link/Unlink are ribbon commands with
    // no menu action.
    ribbon_->set_command_handler([this](RibbonCommand command, bool checked) {
        handle_ribbon_command(command, checked);
    });
    outer->addWidget(ribbon_);

    // The new-project information callout the reference shows under the ribbon.
    // It is a real widget (not painted text) so the chrome gate can measure it.
    info_callout_ = new QLabel(
        QStringLiteral("New tasks are created in Manually Scheduled mode."), central);
    info_callout_->setObjectName(QStringLiteral("manualScheduleCallout"));
    info_callout_->setFixedHeight(kInfoCalloutHeight);
    info_callout_->setStyleSheet(QStringLiteral(
        "background: #fff8d6; color: #5a4a12; padding-left: 12px;"
        " border-bottom: 1px solid #e6d9a0;"));
    outer->addWidget(info_callout_);

    // Timeline strip: Start/Finish block on the left and the timeline bar with
    // the product's prompt (TimelineStrip paints both).
    timeline_ = new TimelineStrip(central);
    outer->addWidget(timeline_);

    // Sheet + Gantt split.  The splitter is the sheet's right edge: 43 % of the
    // client width, with a visible drag thumb (sheet_views.h).
    splitter_ = new SheetSplitter(central);
    splitter_->setHandleWidth(kSplitHandleWidth);
    connect(splitter_, &QSplitter::splitterMoved, this,
            [this](int, int) { splitter_user_moved_ = true; });

    sheet_view_ = new SheetView(splitter_);
    sheet_view_->setModel(&sheet_model_);
    // Wave 19: cells edit through SheetTableModel -> SheetModel's real edit
    // model.  Double-click and Return (the product's triggers); a rejected value
    // is reported in the status line.
    // Wave 21 (operator defect): a DOUBLE-CLICK must not start a cell edit --
    // it opens Task Information, as Project does.  Typing-to-edit (AnyKeyPressed)
    // and F2 (EditKeyPressed) stay the edit triggers, so cell editing is intact.
    sheet_view_->setEditTriggers(QAbstractItemView::AnyKeyPressed |
                                 QAbstractItemView::EditKeyPressed);
    connect(sheet_view_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
            !index.isValid()) {
            return;
        }
        const std::size_t row = sheet_model_.record_row(index.row());
        if (row == core::sheet::kNoIndex) {
            return;
        }
        // Select the row on its Name column (whatever position the file's table
        // gives it), then open the dialog for that task.
        const core::view::FieldId name_field = app::name_field_id();
        std::size_t column = core::sheet::kNoIndex;
        for (std::size_t candidate = 0; candidate < doc_.sheet()->column_count(); ++candidate) {
            if (doc_.sheet()->column(candidate).field == name_field) {
                column = candidate;
                break;
            }
        }
        app::select_cell(*doc_.sheet(), row,
                         column == core::sheet::kNoIndex ? 0 : column);
        gantt_canvas_->set_selected_uid(doc_.selected_uid());
        show_task_information();
    });
    sheet_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    sheet_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    sheet_view_->setWordWrap(false);
    sheet_view_->setTextElideMode(Qt::ElideRight);
    sheet_view_->setShowGrid(true);
    sheet_view_->setGridStyle(Qt::SolidLine);
    sheet_view_->setAlternatingRowColors(false);
    sheet_view_->setCornerButtonEnabled(false);
    sheet_view_->setFrameShape(QFrame::NoFrame);
    sheet_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    sheet_view_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    sheet_view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    sheet_view_->setFont(fixed_font(kBodyPointSize));
    // Wave 21: the cell painter's Task Mode pin follows the live presentation
    // (a loaded file's table can put that column anywhere, or not have one).
    auto* entry_delegate = new EntryItemDelegate(sheet_view_);
    entry_delegate->set_kind_lookup(
        [this](int column) { return sheet_model_.display_kind(column); });
    // Wave 23 (B-1): the Name cell draws the outline (indent, symbol, collapse
    // on click).  The lookups translate the adapter's DISPLAY row to the
    // SheetModel row, so a filter cannot make the triangle act on the wrong row.
    entry_delegate->set_hierarchy_column(
        sheet_model_.display_column_for_field(app::name_field_id()));
    entry_delegate->set_outline_lookups(
        [this](int display_row) {
            const std::size_t row = sheet_model_.record_row(display_row);
            return row == core::sheet::kNoIndex || doc_.sheet() == nullptr
                       ? 0
                       : doc_.sheet()->outline_level(row);
        },
        [this](int display_row) {
            const std::size_t row = sheet_model_.record_row(display_row);
            return row != core::sheet::kNoIndex && doc_.sheet() != nullptr &&
                   doc_.sheet()->has_children(row);
        },
        [this](int display_row) {
            const std::size_t row = sheet_model_.record_row(display_row);
            return row != core::sheet::kNoIndex && doc_.sheet() != nullptr &&
                   doc_.sheet()->is_collapsed(row);
        },
        [this](int display_row) {
            const std::size_t row = sheet_model_.record_row(display_row);
            if (row == core::sheet::kNoIndex || doc_.sheet() == nullptr) {
                return;
            }
            doc_.sheet()->toggle_collapsed(row);
            after_model_change();
        });
    sheet_view_->setItemDelegate(entry_delegate);
    // Wave 31 (W31-4.4): the usage sheet's period columns mix Work/Actual Work
    // (Duration) and Cost sub-rows; the lookup answers per physical row and
    // returns Auto in every other view.
    sheet_model_.set_value_kind_lookup(
        [this](int display_row, int display_column) {
            return usage_sheet_ != nullptr
                       ? usage_sheet_->value_kind(display_row, display_column)
                       : core::sheet::CellKind::Auto;
        });
    // Wave 21: the selected row/cell is Project's measured green (162,211,160),
    // the same value the delegate and the Gantt row highlight use (kSelection).
    sheet_view_->setStyleSheet(QStringLiteral(
        "QTableView { background: #ffffff; gridline-color: #d4d4d4;"
        " selection-background-color: #a2d3a0; selection-color: #101010; }"));

    // The 42 px caption band (paint-only; the model still owns the section
    // sizes).  The row-number gutter is the FROZEN vertical header
    // (SheetRowHeader): MS Project's row numbers do not scroll horizontally
    // with the sheet, and a model column cannot express that.
    auto* header = new SheetHeaderView(Qt::Horizontal, sheet_view_);
    header->setFixedHeight(kSheetHeaderHeight);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->set_column_command_handler([this](const QString& command, int column) {
        if (command == QStringLiteral("hide_column")) {
            sheet_view_->setColumnHidden(column, true);
            set_message("column hidden", false);
            update_status();
            return;
        }
        if (command == QStringLiteral("text_styles")) {
            show_text_styles();
            return;
        }
        if (command == QStringLiteral("insert_column")) {
            // The fixed Entry presentation has no spare column to insert into;
            // the honest action is to reveal a hidden one.
            for (int index = 0; index < sheet_model_.columnCount(); ++index) {
                if (sheet_view_->isColumnHidden(index)) {
                    sheet_view_->setColumnHidden(index, false);
                    set_message("column shown", false);
                    update_status();
                    return;
                }
            }
            set_message("there is no hidden column to insert; column editing needs the "
                        "file-table dialog (backlog)",
                        false);
            update_status();
            return;
        }
        if (doc_.sheet() == nullptr) {
            return;
        }
        const std::size_t model_column = sheet_model_.model_column(column);
        if (model_column == core::sheet::kNoIndex) {
            return;
        }
        if (doc_.sheet()->row_count() != 0) {
            doc_.sheet()->select_cell(0, model_column);
        }
        if (command == QStringLiteral("copy")) {
            copy_selection();
        } else if (command == QStringLiteral("cut")) {
            cut_selection();
        } else if (command == QStringLiteral("paste")) {
            paste_selection();
        }
    });
    sheet_view_->setHorizontalHeader(header);

    auto* row_header = new SheetRowHeader(sheet_view_);
    // The reference's rotated view caption occupies the LEFT 22 px of the sheet
    // band, before the row-number gutter: caption 22 + numbers 38 = the measured
    // 60 px before the first grid line.  Keeping the sum at 60 leaves the
    // sheet's column geometry (and the parity harness's sheet_left) unchanged.
    row_header->setFixedWidth(kGutterWidth - kViewLabelWidth);
    sheet_view_->setVerticalHeader(row_header);
    sheet_view_->verticalHeader()->setVisible(true);

    QPalette sheet_palette = sheet_view_->palette();
    sheet_palette.setColor(QPalette::Base, kSheetBackground);
    sheet_palette.setColor(QPalette::Text, kBodyText);
    sheet_palette.setColor(QPalette::Highlight, kSelection);
    sheet_palette.setColor(QPalette::HighlightedText, kBodyText);
    sheet_view_->setPalette(sheet_palette);

    gantt_canvas_ = new GanttCanvas(splitter_);
    gantt_canvas_->set_shading(prefs_.working_day_shading);

    // The sheet pane is [rotated view caption 22 px][sheet].  The caption sits
    // where the REFERENCE capture shows it: at the far left of the sheet band,
    // not between the table and the chart.  Placing it inside the chart pane
    // would insert 22 px between the last column and the splitter and move the
    // parity harness's sheet/chart boundary by that amount.
    QWidget* sheet_panel = new QWidget(splitter_);
    auto* sheet_layout = new QHBoxLayout(sheet_panel);
    sheet_layout->setContentsMargins(0, 0, 0, 0);
    sheet_layout->setSpacing(0);
    view_label_ = new ViewLabel(QStringLiteral("Gantt Chart"), sheet_panel);
    view_label_->setFixedWidth(kViewLabelWidth);
    sheet_layout->addWidget(view_label_);
    sheet_layout->addWidget(sheet_view_, 1);

    // The chart pane is the canvas alone, starting exactly at the splitter.
    chart_panel_ = new QWidget(splitter_);
    auto* chart_layout = new QHBoxLayout(chart_panel_);
    chart_layout->setContentsMargins(0, 0, 0, 0);
    chart_layout->setSpacing(0);
    chart_layout->addWidget(gantt_canvas_, 1);
    // W30-1.10: the read-only Task Inspector docks at the right of the canvas
    // when Task > Tasks > Inspect is clicked.  Hidden by default, so the chart
    // pane's minimum width (and every parity rectangle) is the canvas alone;
    // a hidden layout item is not part of the layout's minimum size hint.
    inspector_ = new TaskInspector(chart_panel_);
    inspector_->setVisible(false);
    inspector_->set_close_handler([this]() { set_inspector_visible(false); });
    chart_layout->addWidget(inspector_);

    splitter_->addWidget(sheet_panel);
    splitter_->addWidget(chart_panel_);
    splitter_->setSizes(QList<int>{1, 1});

    // W26-6.3: the Details pane is a SECOND, vertical splitter whose first pane
    // is the sheet/Gantt pair.  With the pane hidden the pair fills it exactly,
    // so the measured 0.459 sheet fraction and every parity rectangle are
    // unchanged; showing it docks the Task Form at the bottom with a draggable
    // handle of the measured 6-px width.
    details_splitter_ = new QSplitter(Qt::Vertical, central);
    details_splitter_->setObjectName(QStringLiteral("detailsSplitter"));
    details_splitter_->setHandleWidth(kSplitHandleWidth);
    details_splitter_->setChildrenCollapsible(false);
    details_splitter_->addWidget(splitter_);
    task_form_ = new TaskForm(details_splitter_);
    // W28-4.1: a committed field goes through the one editable path the sheet
    // cells and Task Information use; the form has no writer of its own.
    task_form_->set_commit_handler(
        [this](TaskFormField field, const QString& text) { commit_task_form_field(field, text); });
    details_splitter_->addWidget(task_form_);
    task_form_->setVisible(false);
    // W30-2.2: the Resource Form is the Details pane of the Resource Sheet.
    // Both forms live in the same splitter and only one is shown at a time
    // (the hidden one is not part of the layout's size hint), so a task-view
    // layout is unchanged by the second pane.
    resource_form_ = new ResourceForm(details_splitter_);
    details_splitter_->addWidget(resource_form_);
    resource_form_->setVisible(false);
    outer->addWidget(details_splitter_, 1);

    central->setLayout(outer);

    // The File backstage is a second page of the central stack: the normal
    // chrome (index 0) and the backstage shell (index 1).  Keeping it inside the
    // central widget means the ribbon/timeline/sheet are simply hidden while the
    // backstage is open, exactly like the product.
    backstage_ = new Backstage(this);
    backstage_->set_command_handler([this](const QString& command) {
        if (command == QStringLiteral("open")) {
            show_backstage(false);
            open_dialog();
        } else if (command == QStringLiteral("open_this_pc")) {
            // W26-4.4: This PC starts at the computer root; Browse keeps the
            // dialog's own last location.
            show_backstage(false);
            open_dialog(QDir::rootPath());
        } else if (command == QStringLiteral("new")) {
            // W26-REV-3: Blank Project discards the open document, so the same
            // unsaved-edit guard as File > Open must run first.
            if (!confirm_proceed_with_discard()) {
                return;
            }
            new_project();
        } else if (command == QStringLiteral("new_from_existing")) {
            // W26-4.3: start from an existing project by opening it; the reader
            // never modifies it in place, so Save needs a new target.
            show_backstage(false);
            open_dialog();
        } else if (command == QStringLiteral("save")) {
            show_backstage(false);
            save_document(false);
        } else if (command == QStringLiteral("save_as")) {
            show_backstage(false);
            save_document(true);
        } else if (command == QStringLiteral("save_as_this_pc")) {
            show_backstage(false);
            save_document(true, QDir::rootPath());
        } else if (command == QStringLiteral("print")) {
            show_backstage(false);
            print_plan();
        } else if (command == QStringLiteral("export")) {
            show_backstage(false);
            export_pdf(app::ReportKind::gantt);
        } else if (command == QStringLiteral("save_as_pdf")) {
            // File > Export > Create PDF/XPS (Q17): the same shared renderer as
            // Export PDF, restored to its Project home (W23-10).
            show_backstage(false);
            save_as_pdf();
        } else if (command == QStringLiteral("close")) {
            close();
        }
    });
    backstage_->set_close_handler([this]() { show_backstage(false); });
    backstage_->set_options_handler([this]() { show_options(); });
    // W29-3.1: Page Setup opens on the values every print route already uses.
    backstage_->set_page_setup_handler([this]() { show_page_setup(); });
    backstage_->set_theme_choice_handler(
        [this](ThemeChoice choice) { set_theme_choice(choice, true); });
    backstage_->set_about_handler([this]() { show_about(); });
    backstage_->set_recent_open_handler([this](const QString& path) {
        // W26-4.3: a Recent row opens a local file through the same path as
        // File > Open.  W26-REV-3: it must also run File > Open's unsaved-edit
        // guard first, or a Recent click silently discards pending edits.
        if (!confirm_proceed_with_discard()) {
            return;
        }
        // A failure is reported in the status line, not a modal, so the
        // offscreen gate can drive the row.
        show_backstage(false);
        QString error;
        if (!open_file(path, &error)) {
            set_message("could not open the recent file: " + error.toStdString(), true);
            update_status();
        }
    });
    // W26-4.5: the Print page's preview is one page of the CURRENT document
    // through the shared page layout -- never a second page arithmetic.
    backstage_->set_print_preview_provider(
        [this](const QSize& size, bool landscape, bool letter) -> QImage {
            if (!doc_.has_document() || doc_.sheet() == nullptr) {
                return QImage();
            }
            // W29-3.1: the Page Setup values ride on the live paper choice.
            const app::PrintJob job = current_print_job(print_spec(landscape, letter));
            return render_print_page(job, 0, *doc_.sheet(), doc_.plan(), prefs_, size);
        });
    central_stack_ = new QStackedWidget(this);
    central_stack_->addWidget(central);
    central_stack_->addWidget(backstage_);
    setCentralWidget(central_stack_);

    // The editing adapter reports each commit here so the window can rebuild the
    // Gantt/status WITHOUT resetting the model from inside setData().
    sheet_model_.set_commit_handler(
        [this](bool ok, const std::string& error) { after_cell_commit(ok, error); });

    // A click selects through the sheet model (the single source of truth), then
    // mirrors the selection onto the Gantt bar.  Shift-click extends the model's
    // rectangle: that rectangle is what Copy/Cut read and what Paste fills.
    connect(sheet_view_, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        if (!doc_.has_document()) {
            return;
        }
        // W30-2.2: the Resource Sheet is read-only -- core::sheet's selection
        // model owns task rows only -- but a click still moves the Resource
        // Form, which follows the sheet's current row by the resource UID the
        // generic RecordSet carries.  This branch never selects through the
        // task sheet and never stages an edit.
        if (prefs_.view == app::ViewMode::resource_sheet) {
            if (view_sheet_ == nullptr || sheet_view_ == nullptr) {
                return;
            }
            const std::size_t row = sheet_model_.record_row(index.row());
            if (row == core::sheet::kNoIndex || view_sheet_->row_uid(row) == 0) {
                return;
            }
            // The clicked signal follows the view's own current-index update
            // in the common case; setting it here makes the pane's lookup
            // independent of that ordering.
            sheet_view_->setCurrentIndex(index);
            refresh_resource_form();
            return;
        }
        if (prefs_.view != app::ViewMode::task_sheet) {
            return;
        }
        const std::size_t row = sheet_model_.record_row(index.row());
        const std::size_t column = sheet_model_.model_column(index.column());
        if (row == core::sheet::kNoIndex || column == core::sheet::kNoIndex) {
            return;
        }
        const bool extend = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
        if (extend) {
            if (!doc_.sheet()->extend_to(row, column)) {
                return;
            }
            select_core_range_in_view();
        } else if (!app::select_cell(*doc_.sheet(), row, column)) {
            return;
        }
        gantt_canvas_->set_selected_uid(doc_.selected_uid());
        if (sheet_view_->viewport() != nullptr) {
            sheet_view_->viewport()->update();
        }
        // W26-6.3: the Task Form shows the newly selected task.
        refresh_task_form();
        // W30-1.10: the Task Inspector follows the same selection.
        refresh_inspector();
        update_menu_state();
    });

    // The task-row right-click menu (distinct from the column header menu).
    sheet_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(sheet_view_, &QWidget::customContextMenuRequested, this, [this](const QPoint&) {
        QMenu* menu = build_row_menu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(QCursor::pos());
    });

    apply_split_fraction();
}

void MainWindow::build_menus() {
    QMenuBar* bar = menuBar();
    actions_.clear();
    // W26-6.1: one recursive item builder so a drop-down goes through exactly
    // the same action table as a top-level command.  Every leaf QAction lands in
    // actions_, which update_menu_state() walks; a container QAction has no
    // data and is never enabled/disabled by the table.
    std::function<void(QMenu*, const std::vector<app::MenuItemSpec>&)> add_items =
        [this, &add_items](QMenu* menu, const std::vector<app::MenuItemSpec>& items) {
            bool first = true;
            for (const app::MenuItemSpec& item : items) {
                if (item.separator_before && !first) {
                    menu->addSeparator();
                }
                first = false;
                if (item.is_submenu()) {
                    QMenu* child = menu->addMenu(QString::fromUtf8(item.title));
                    add_items(child, item.children);
                    continue;
                }
                QAction* action = menu->addAction(QString::fromUtf8(item.title));
                action->setData(static_cast<int>(item.action));
                if (item.action != app::MenuAction::none) {
                    // The quick-access toolbar reuses these actions, so give
                    // every one an icon drawn by shell_icons.cpp (never
                    // Microsoft artwork).  W29-5.2: the artwork is one-ink in
                    // the wave-26 style, drawn in the live window ink (the QAT
                    // and menu pop-ups sit on the window surface).
                    action->setIcon(shell_action_icon(
                        item.action, kShellIconPx, chrome_palette(theme_).window_ink));
                }
                const QKeySequence shortcut = menu_shortcut(item);
                if (!shortcut.isEmpty()) {
                    action->setShortcut(shortcut);
                }
                const char* reason = app::menu_disabled_reason(item.action);
                if (!shell_implements(item.action)) {
                    // Operator decision 9: every unimplemented command is disabled
                    // with this exact tooltip (and listed in the generated backlog).
                    action->setToolTip(QStringLiteral("not implemented in this port"));
                    action->setStatusTip(action->toolTip());
                } else if (item.action == app::MenuAction::cut) {
                    // The app's action table marks Cut unsupported because the
                    // macOS shell has no cut path.  The Qt shell implements it as
                    // Copy + a clearing transaction on the same SheetModel, so the
                    // reason is stale here; give it the real description instead.
                    action->setToolTip(QStringLiteral("Cut the selected cells (values only)"));
                    action->setStatusTip(action->toolTip());
                } else if (reason != nullptr && reason[0] != '\0') {
                    action->setToolTip(QString::fromUtf8(reason));
                    action->setStatusTip(QString::fromUtf8(reason));
                }
                connect(action, &QAction::triggered, this,
                        [this, action_id = item.action]() { handle_action(action_id); });
                actions_.push_back(action);
            }
        };
    for (const app::MenuSpec& spec : app::menu_specs()) {
        QMenu* menu = bar->addMenu(QString::fromUtf8(spec.title));
        add_items(menu, spec.items);
    }
    // Wave 21 (operator decision: "hide it for parity"): Project 2021 has no
    // classic menu bar -- its commands live on the ribbon and the QAT.  The bar
    // and every QAction stay alive (shortcuts, enablement, the gate's action
    // lookups); only the bar itself is hidden, so the QAT/ribbon move up.
    bar->setVisible(false);
}

void MainWindow::build_status_bar() {
    status_summary_ = new QLabel(this);
    status_summary_->setFont(fixed_font(kStatusPointSize));
    status_summary_->setTextFormat(Qt::PlainText);
    status_summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    status_message_ = new QLabel(this);
    status_message_->setFont(fixed_font(kStatusPointSize));
    status_message_->setTextFormat(Qt::PlainText);
    status_message_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status_message_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    // The product's centred scheduling-mode indicator (a pin + "New Tasks").
    status_mode_ = new QLabel(QStringLiteral("New Tasks : Manually Scheduled"), this);
    status_mode_->setObjectName(QStringLiteral("statusMode"));
    status_mode_->setFont(fixed_font(kStatusPointSize));
    status_mode_->setTextFormat(Qt::PlainText);
    status_mode_->setAlignment(Qt::AlignCenter);
    status_mode_->setToolTip(
        QStringLiteral("New tasks are created in Manually Scheduled mode"));

    // View buttons, as the reference's status bar has.  They switch the window
    // through app::ViewModel (wave 19), so a click is never silent.
    auto* views = new QWidget(this);
    auto* view_layout = new QHBoxLayout(views);
    view_layout->setContentsMargins(4, 0, 4, 0);
    view_layout->setSpacing(2);
    view_buttons_.clear();
    int view_index = 0;
    for (const ViewButtonSpec& spec : kViewButtons) {
        auto* button = new QToolButton(views);
        button->setText(QString::fromLatin1(spec.label));
        // The product's status bar shows an icon beside each view label; the
        // icon is ours (shell_icons.cpp), never Microsoft artwork, and follows
        // the theme's window ink (W29-5.2).
        button->setIcon(shell_view_icon(view_index, kShellIconPx,
                                        chrome_palette(theme_).window_ink));
        button->setIconSize(QSize(kShellIconPx, kShellIconPx));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setAutoRaise(true);
        button->setFont(fixed_font(kStatusPointSize - 1));
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFixedHeight(kStatusHeight - 4);
        const app::ViewMode mode = spec.mode;
        if (!spec.implemented) {
            // Wave 23 (B-9): Project's five buttons exist; the two views this
            // port does not have yet are disabled with the exact tooltip.
            button->setEnabled(false);
            button->setToolTip(QStringLiteral("not implemented in this port"));
        } else {
            button->setToolTip(button->text());
            connect(button, &QToolButton::clicked, this,
                    [this, mode](bool) { set_view(mode); });
        }
        view_layout->addWidget(button);
        view_buttons_.push_back(button);
        ++view_index;
    }

    // Zoom controls: the slider is wired to the same set_zoom() the menu's
    // Day/Week/Month commands use.
    auto* zoom_out = new QToolButton(this);
    zoom_out->setText(QStringLiteral("\u2212"));
    zoom_out->setAutoRaise(true);
    zoom_out->setFont(fixed_font(kStatusPointSize));
    zoom_out->setFocusPolicy(Qt::NoFocus);
    zoom_out->setFixedWidth(18);
    connect(zoom_out, &QToolButton::clicked, this,
            [this](bool) { set_zoom(stepped_zoom(prefs_.zoom, -1)); });

    zoom_slider_ = new QSlider(Qt::Horizontal, this);
    zoom_slider_->setRange(static_cast<int>(app::ZoomLevel::day),
                           static_cast<int>(app::ZoomLevel::month));
    zoom_slider_->setSingleStep(1);
    zoom_slider_->setPageStep(1);
    zoom_slider_->setTickPosition(QSlider::TicksBelow);
    zoom_slider_->setTickInterval(1);
    zoom_slider_->setFixedWidth(104);
    zoom_slider_->setFocusPolicy(Qt::NoFocus);
    zoom_slider_->setFont(fixed_font(kStatusPointSize - 1));
    connect(zoom_slider_, &QSlider::valueChanged, this, [this](int value) {
        set_zoom(static_cast<app::ZoomLevel>(value));
    });

    auto* zoom_in = new QToolButton(this);
    zoom_in->setText(QStringLiteral("+"));
    zoom_in->setAutoRaise(true);
    zoom_in->setFont(fixed_font(kStatusPointSize));
    zoom_in->setFocusPolicy(Qt::NoFocus);
    zoom_in->setFixedWidth(18);
    connect(zoom_in, &QToolButton::clicked, this,
            [this](bool) { set_zoom(stepped_zoom(prefs_.zoom, 1)); });

    // The zoom group is minus / slider / plus, with no percentage readout
    // (operator decision: the slider maps to Days/Weeks/Months).
    statusBar()->setSizeGripEnabled(false);
    // W26-6.2: the bar's colours are applied by apply_chrome_theme(), so the
    // light measurement and the two dark palettes share one code path.
    statusBar()->addWidget(status_summary_, 1);
    statusBar()->addWidget(status_mode_, 0);
    statusBar()->addPermanentWidget(views, 0);
    statusBar()->addPermanentWidget(zoom_out, 0);
    statusBar()->addPermanentWidget(zoom_slider_, 0);
    statusBar()->addPermanentWidget(zoom_in, 0);
    statusBar()->addPermanentWidget(status_message_, 0);
    update_zoom_slider();
    refresh_view_buttons();
}

// The quick-access toolbar above the ribbon: the product's Save/Undo/Redo strip.
// It reuses the menu QActions (same ids, same enablement), so the toolbar and the
// menu can never disagree; the icons are drawn by shell_icons.cpp.
void MainWindow::build_quick_access() {
    quick_access_ = addToolBar(QStringLiteral("Quick Access"));
    quick_access_->setObjectName(QStringLiteral("quickAccessToolbar"));
    quick_access_->setMovable(false);
    quick_access_->setFloatable(false);
    quick_access_->setFixedHeight(kQuickAccessHeight);
    quick_access_->setIconSize(QSize(kShellIconPx, kShellIconPx));
    quick_access_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // W26-6.2: the strip's colours are applied by apply_chrome_theme().
    const app::MenuAction ids[] = {app::MenuAction::save, app::MenuAction::undo,
                                   app::MenuAction::redo};
    for (const app::MenuAction id : ids) {
        for (QAction* action : actions_) {
            if (action->data().toInt() == static_cast<int>(id)) {
                quick_access_->addAction(action);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Splitter geometry
// ---------------------------------------------------------------------------

void MainWindow::apply_split_fraction() {
    if (splitter_ == nullptr || splitter_->count() < 2) {
        return;
    }
    if (prefs_.view != app::ViewMode::task_sheet) {
        return;  // a full-width grid view has no split to place
    }
    const int total = splitter_->width();
    if (total <= kSplitHandleWidth + 160) {
        return;
    }
    // The split's centre line sits at 43 % of the client width: the sheet is
    // that width less half the handle, so the draggable thumb straddles 43 %.
    int sheet = std::max(160, static_cast<int>(std::lround(total * kSplitFraction)) -
                                  kSplitHandleWidth / 2);
    // The measured reference's split is the TABLE's own right edge: gutter +
    // nine Entry columns = 879 px, with the 6 px handle after it, which at 1920
    // is 881 = 0.45885 of the width.  The 0.459 fraction lands 1 px short of
    // that at 1920, which would clip the ninth column's last pixel; when the
    // shortfall is no more than one handle and the chart keeps its minimum
    // width, snap the sheet to the whole table.
    const int table_width =
        kGutterWidth + display_content_width(sheet_model_.display_columns());
    if (sheet < table_width && table_width - sheet <= kSplitHandleWidth &&
        chart_panel_ != nullptr &&
        total - kSplitHandleWidth - table_width >= chart_panel_->minimumWidth()) {
        sheet = table_width;
    }
    const int gantt = std::max(160, total - kSplitHandleWidth - sheet);
    splitter_->setSizes(QList<int>{sheet, gantt});
}

bool MainWindow::table_widths_read() const { return doc_.table_widths_read(); }

int MainWindow::table_column_chars(std::uint32_t table_field) const {
    return doc_.table_column_chars(table_field);
}

double MainWindow::split_fraction() const {
    if (splitter_ == nullptr || splitter_->count() < 2) {
        return 0.0;
    }
    const QList<int> sizes = splitter_->sizes();
    const int total = sizes[0] + sizes[1] + splitter_->handleWidth();
    if (total <= 0) {
        return 0.0;
    }
    return static_cast<double>(sizes[0]) / static_cast<double>(total);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (!splitter_user_moved_) {
        apply_split_fraction();
        QMetaObject::invokeMethod(
            this,
            [this]() {
                if (!splitter_user_moved_) {
                    apply_split_fraction();
                }
            },
            Qt::QueuedConnection);
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (centralWidget() != nullptr && centralWidget()->layout() != nullptr) {
        centralWidget()->layout()->activate();
    }
    if (!splitter_user_moved_) {
        apply_split_fraction();
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event != nullptr && event->key() == Qt::Key_Escape && backstage_open_) {
        show_backstage(false);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirm_proceed_with_discard()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------

bool MainWindow::open_file(const QString& path, QString* error) {
    // Detach the view BEFORE the load: PlanDocument::load destroys the previous
    // SheetModel, and the Qt model must not keep pointing at it.
    sheet_model_.set_sheet(nullptr);
    sheet_model_.set_generic(false);
    std::string failure;
    if (!doc_.load(path.toStdString(), prefs_.zoom, failure)) {
        // The macOS shell leaves the previous state cleared rather than
        // half-loaded (app/README.md); the Qt model is already detached above.
        structural_ = app::StructuralEdits{};
        document_.reset();
        owns_path_ = false;
        // Back to the new-project default: no loaded file says otherwise.
        new_tasks_manual_ = true;
        new_tasks_manual_known_ = false;
        loaded_new_tasks_manual_ = true;
        loaded_new_tasks_manual_known_ = false;
        last_save_target_.clear();
        view_sheet_.reset();
        view_records_.clear();
        prefs_.view = app::ViewMode::task_sheet;
        auto* header = static_cast<SheetHeaderView*>(sheet_view_->horizontalHeader());
        auto* delegate = static_cast<EntryItemDelegate*>(sheet_view_->itemDelegate());
        if (header != nullptr) {
            header->set_model_titles(false);
        }
        if (delegate != nullptr) {
            delegate->set_entry_mode(true);
        }
        gantt_canvas_->setVisible(true);
        if (chart_panel_ != nullptr) {
            chart_panel_->setVisible(true);
        }
        configure_task_columns();
        // Back to a NEW project: current week, Entry headers, today's Start and
        // Finish -- not the engine's 1984 default axis.  Wave 26: Project's
        // ribbon defaults apply (Timeline shown, project row hidden, Critical
        // Tasks OFF).
        timeline_visible_ = true;
        gantt_canvas_->set_critical_highlight(false);
        ribbon_->set_document_state(false, timeline_visible_, false);
        ribbon_->set_critical_tasks(false);
        ribbon_->set_font_default(QString(), 0);
        doc_.build_empty_view(prefs_.zoom, app::today_date32());
        gantt_canvas_->set_layout(doc_.gantt());
        gantt_canvas_->set_selected_uid(0);
        refresh_timeline();
        // W30-1.10: the failed load forgot the document; the inspector must
        // not keep the previous file's task.
        refresh_inspector();
        update_chrome();
        setWindowTitle(QStringLiteral("MS Project Shell"));
        refresh_view_buttons();
        if (error != nullptr) {
            *error = QString::fromStdString(failure);
        }
        set_message("load failed: " + failure, true);
        update_menu_state();
        update_status();
        return false;
    }

    // A new file owns a fresh pending-edit stack and a fresh one-write holder.
    structural_ = app::StructuralEdits{};
    document_ = std::make_unique<app::Document>(path.toStdString());
    owns_path_ = false;
    last_save_target_.clear();
    last_save_warnings_.clear();
    last_save_rows_written_ = 0;
    view_sheet_.reset();
    view_records_.clear();
    prefs_.view = app::ViewMode::task_sheet;

    /* Wave 22 (E7): the loaded document's new-task scheduling default, read
     * from the project props (mpp::ProjectRow::new_tasks_manual).  PlanDocument
     * does not expose the mpp::Document, so the container is reopened for that
     * one record: the accessor reads no table, only `114|112/Props`.  A file
     * that does not store it (an original Project 2007 MPP12 file) or a read
     * failure leaves `known` false, and the status bar keeps its Manual
     * fallback rather than claiming a mode the bytes did not state. */
    const mpp::NewTaskMode new_task_mode = mpp::read_new_task_mode(path.toStdString());
    new_tasks_manual_known_ = new_task_mode.known;
    new_tasks_manual_ = new_task_mode.known ? new_task_mode.manual : true;
    // W28-3.1: the baseline a pending Options > Schedule edit/undo restores to.
    loaded_new_tasks_manual_ = new_tasks_manual_;
    loaded_new_tasks_manual_known_ = new_tasks_manual_known_;

    // The persisted date style belongs to the loaded SheetFormat before anything
    // formats a cell or the status line.  Wave 29 (W29-1.3): the Gantt is
    // rebuilt here as well, because the applied view's stored bar labels format
    // their dates with this same SheetFormat -- load() built them with the
    // default date style.
    doc_.apply_preferences(prefs_);
    doc_.rebuild_views(prefs_.zoom);

    // The Entry presentation, with no filter.
    auto* header = static_cast<SheetHeaderView*>(sheet_view_->horizontalHeader());
    auto* delegate = static_cast<EntryItemDelegate*>(sheet_view_->itemDelegate());
    if (header != nullptr) {
        header->set_model_titles(false);
    }
    if (delegate != nullptr) {
        delegate->set_entry_mode(true);
    }
    sheet_view_->setEditTriggers(QAbstractItemView::AnyKeyPressed |
                                 QAbstractItemView::EditKeyPressed);
    if (chart_panel_ != nullptr) {
        chart_panel_->setVisible(true);
    }
    update_chrome();

    rebind_sheet();
    configure_task_columns();
    sync_view_selection();
    gantt_canvas_->set_layout(doc_.gantt());
    gantt_canvas_->set_selected_uid(doc_.selected_uid());
    gantt_canvas_->set_shading(prefs_.working_day_shading);

    // Wave 26: the file's stored view state drives the ribbon check boxes
    // (Timeline and Project Summary Task); Critical Tasks is Project's default
    // OFF for every document.
    timeline_visible_ = doc_.loaded().show_timeline;
    gantt_canvas_->set_critical_highlight(false);
    ribbon_->set_document_state(true, timeline_visible_, doc_.project_summary_visible());
    ribbon_->set_critical_tasks(false);
    // W26-REV-4: W26-1.1's Font value displays follow the FILE's decoded default
    // text style when the view record carries one; the captured new-project
    // default (Calibri 11) stays the fallback.  The first FONT_BASES entry is
    // the document's default font (PROJECT_A's Arial 8, which is what Project's
    // own Font box shows); an absent table falls back to the non-critical style.
    {
        const FileFont font = file_font(doc_.gantt_style());
        ribbon_->set_font_default(font.family, font.point_size);
    }

    refresh_timeline();

    update_window_title();
    // Operator decision 8 / the reference: the LEFT status segment is "Ready",
    // not the load diagnostic (wave 21: the summary moved to the right segment and
    // its tooltip, which also keeps the reader-warning count visible).
    set_message(std::string(), false);
    update_zoom_slider();
    refresh_view_buttons();
    update_menu_state();
    update_status();
    // W26-4.3: a successful local open joins the recent list (the pages show
    // local paths only; nothing is uploaded or synced).
    note_recent_file(path);
    return true;
}

void MainWindow::rebind_sheet() {
    if (!doc_.has_document()) {
        sheet_model_.set_sheet(nullptr);
        return;
    }
    if (prefs_.view != app::ViewMode::task_sheet) {
        sheet_model_.set_generic(true);
        sheet_model_.set_sheet(view_sheet_.get());
        sheet_model_.set_display_rows({}, false);
        return;
    }
    sheet_model_.set_generic(false);
    sheet_model_.set_sheet(doc_.sheet());
    apply_filter_to_sheet_model();
}

void MainWindow::apply_filter_to_sheet_model() {
    if (prefs_.view != app::ViewMode::task_sheet) {
        sheet_model_.set_display_rows({}, false);
        return;
    }
    sheet_model_.set_display_rows(doc_.display_rows(),
                                  doc_.filter_active() || doc_.sort_active());
}

void MainWindow::configure_task_columns() {
    if (sheet_view_ == nullptr) {
        return;
    }
    // Wave 21 (operator decision 7): a LOADED file's sheet shows the columns its
    // own `214/CTable` stores -- set, order, titles and widths -- and only a
    // new/empty project shows the fixed Entry presentation.  PlanDocument built
    // the SheetModel from the same table (app::table_sheet_columns), so display
    // column N maps to a real model column and editing keeps working.
    std::vector<DisplayColumn> columns;
    if (doc_.has_document() && doc_.sheet() != nullptr) {
        // W29-1.1: the stored widths are CHARACTERS.  Project paints
        // chars * unit + 3 px, where the unit is measured from the file's
        // default font (PROJECT_A's Arial 8 -> 5; the fixture's Segoe UI 10 ->
        // 6).  A fixed 6 px/char made PROJECT_A's eight columns too wide to fit
        // at 1920, which is why the sheet showed only five of them.
        const FileFont font = file_font(doc_.gantt_style());
        columns = display_columns_for_sheet(
            *doc_.sheet(), column_char_px(font.family.toStdString(), font.point_size));
    } else {
        columns = entry_display_columns();
    }
    sheet_model_.set_display_columns(std::move(columns));
    auto* header = static_cast<SheetHeaderView*>(sheet_view_->horizontalHeader());
    if (header != nullptr) {
        header->set_display_columns(sheet_model_.display_columns());
    }
    configure_sheet();
}

void MainWindow::configure_sheet() {
    if (sheet_view_ == nullptr || prefs_.view != app::ViewMode::task_sheet) {
        return;
    }
    // The widths ARE the presentation's: the Entry list's measured 39/57/147/81/
    // 81/81/105/105/123 for a new/empty project, the file's stored character
    // widths at the file's own measured unit (W29-1.1: PROJECT_A's Arial 8 gives
    // 353/48/98/108/38/68/68/88, the fixture's Segoe UI 10 keeps 6 px).  A
    // stored width of 0 is a hidden column (app::table_sheet_columns), so every
    // column here has a positive width.
    sheet_view_->setMinimumWidth(0);
    const std::vector<DisplayColumn>& columns = sheet_model_.display_columns();
    for (std::size_t index = 0; index < columns.size(); ++index) {
        if (columns[index].width > 0) {
            sheet_view_->setColumnWidth(static_cast<int>(index), columns[index].width);
        }
    }
    sheet_view_->verticalHeader()->setMinimumSectionSize(kRowHeight);
    sheet_view_->verticalHeader()->setDefaultSectionSize(kRowHeight);
}

void MainWindow::configure_generic_columns() {
    if (sheet_view_ == nullptr || view_sheet_ == nullptr) {
        return;
    }
    auto* header = static_cast<SheetHeaderView*>(sheet_view_->horizontalHeader());
    // W31-4.4: the usage sheets' widths are measured PIXELS (147/123 label,
    // 48 Details, 64/84/105 period, 84 Total), their rows the measured 21 px
    // pitch, their parent rows the only numbered gutters, their label column
    // merged across each logical row, and their period columns two-tier.
    if (usage_sheet_ != nullptr) {
        const std::vector<int>& widths = usage_sheet_->column_widths();
        const std::size_t count =
            std::min(widths.size(), view_sheet_->visible_column_count());
        for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
            sheet_view_->setColumnWidth(static_cast<int>(ordinal), widths[ordinal]);
        }
        if (auto* gutter =
                static_cast<SheetRowHeader*>(sheet_view_->verticalHeader());
            gutter != nullptr) {
            gutter->setMinimumSectionSize(kUsageRowHeight);
            gutter->setDefaultSectionSize(kUsageRowHeight);
        }
        std::vector<QString> numbers;
        numbers.reserve(usage_sheet_->row_numbers().size());
        for (const std::string& text : usage_sheet_->row_numbers()) {
            numbers.push_back(QString::fromStdString(text));
        }
        sheet_model_.set_row_numbers(std::move(numbers));
        for (const UsageRowSpan& span : usage_sheet_->label_spans()) {
            sheet_view_->setSpan(span.row, UsageSheet::kLabelColumn, span.rows, 1);
        }
        if (header != nullptr) {
            std::vector<HeaderTierCell> cells;
            const std::vector<UsageHeaderCell>& source = usage_sheet_->header_cells();
            cells.reserve(source.size());
            for (const UsageHeaderCell& tier : source) {
                HeaderTierCell cell;
                cell.major = QString::fromStdString(tier.major);
                cell.minor = QString::fromStdString(tier.minor);
                cell.tiered = tier.tiered;
                cell.group_start = tier.group_start;
                cell.major_align = tier.major_align;
                cell.minor_align = tier.minor_align;
                cells.push_back(std::move(cell));
            }
            header->set_tier_cells(std::move(cells));
        }
        return;
    }
    if (header != nullptr) {
        header->clear_tier_cells();
    }
    // Leaving a usage view restores the shared presentation: ordinal gutters
    // and the measured 22 px row pitch of every other sheet.
    sheet_model_.clear_row_numbers();
    sheet_view_->verticalHeader()->setMinimumSectionSize(kRowHeight);
    // W31-3.6: the Resource Sheet's stored widths are CHARACTERS and Project
    // paints chars * unit + 3 px, the same rule the Task Sheet follows
    // (W29-1.1).  The unit is measured from the file's default text font: the
    // Resource Sheet view's own FONT_BASES table (the fixture's Segoe UI 10 ->
    // 6).  A file without that view keeps the stock 6 px unit, which is the
    // measured presentation of the fallback column list.  The usage grids keep
    // their wave-20 rule (G4 owns those widths).
    int char_px = kColumnCharPx;
    if (prefs_.view == app::ViewMode::resource_sheet) {
        const app::ResourceSheetStyle& resource_style = doc_.resource_sheet_style();
        if (resource_style.decoded && !resource_style.text.fonts.empty()) {
            const msproject::viewstate::FontBase& font = resource_style.text.fonts.front();
            if (font.valid) {
                char_px = column_char_px(font.name, font.point_size);
            }
        }
    }
    const bool resource_sheet = prefs_.view == app::ViewMode::resource_sheet;
    const std::size_t count = view_sheet_->visible_column_count();
    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
        const std::size_t column = view_sheet_->visible_column(ordinal);
        if (column == core::sheet::kNoIndex) {
            continue;
        }
        const int chars = view_sheet_->column(column).width;
        int width = chars > 0 ? (resource_sheet ? column_width_px(chars, char_px)
                                               : chars * 7)
                              : 100;
        if (!resource_sheet) {
            width = std::clamp(width, 50, 400);
        }
        sheet_view_->setColumnWidth(static_cast<int>(ordinal), width);
    }
    sheet_view_->verticalHeader()->setDefaultSectionSize(kRowHeight);
}

// The QTableView's own current index is not the sheet's selection (SheetModel
// owns that), so mirror the selected task onto the view.  This is display-only:
// a click still goes through app::select_cell() in the clicked handler.
void MainWindow::sync_view_selection() {
    // W26-6.3: the docked Task Form follows the selection through EVERY path
    // that re-derives it (load, rebuild, click, structural edit), because this
    // is the one place the view selection is mirrored.
    refresh_task_form();
    // W30-1.10: the Task Inspector is a second selection-following pane.
    refresh_inspector();
    // W30-2.2: the Resource Form follows the Resource Sheet's current row.
    refresh_resource_form();
    if (!doc_.has_document() || sheet_view_ == nullptr ||
        prefs_.view != app::ViewMode::task_sheet) {
        return;
    }
    const std::uint32_t uid = doc_.selected_uid();
    if (uid == 0) {
        return;
    }
    const std::size_t ordinal = doc_.rows().ordinal_of_uid(uid);
    if (ordinal == core::sheet::kNoIndex) {
        return;
    }
    const int name_column = sheet_model_.display_column_for_field(app::name_field_id());
    sheet_view_->setCurrentIndex(
        sheet_model_.index(static_cast<int>(ordinal), std::max(0, name_column)));
}

// ---------------------------------------------------------------------------
// Status and menu state
// ---------------------------------------------------------------------------

void MainWindow::update_status() {
    if (status_summary_ == nullptr) {
        return;
    }
    // Wave 21 (operator decision 8, the reference capture): the LEFT segment is
    // "Ready" -- or the command message/error that is active right now.  The
    // diagnostic summary that used to sit here (counts, Entry widths, warnings)
    // moved to the RIGHT segment, whose tooltip carries the whole thing, so the
    // reader-warning count stays visible without taking the Ready slot.
    status_summary_->setText(QString::fromStdString(
        message_.empty() ? std::string("Ready") : message_));
    QPalette left_palette = status_summary_->palette();
    left_palette.setColor(QPalette::WindowText,
                          message_is_error_ ? QColor(0xb0, 0x1c, 0x1c)
                                            : QColor(0x20, 0x20, 0x20));
    status_summary_->setPalette(left_palette);
    if (status_message_ != nullptr) {
        status_message_->setText(status_detail_text());
        status_message_->setToolTip(QString::fromStdString(summary_text()));
    }
    update_status_mode();
}

void MainWindow::update_status_mode() {
    if (status_mode_ == nullptr) {
        return;
    }
    // Wave 22 (E7): the centre follows the loaded file's stored default.  An
    // unknown value (new project, original Project 2007 file, undecodable
    // record) keeps the product's current Manual text -- the documented
    // fallback -- and the tooltip says so, so the display never invents a mode.
    // W28-3.1: `new_tasks_manual_` already includes the last pending Options >
    // Schedule choice (refresh_new_task_mode).
    const bool manual = !new_tasks_manual_known_ || new_tasks_manual_;
    status_mode_->setText(manual ? QStringLiteral("New Tasks : Manually Scheduled")
                                 : QStringLiteral("New Tasks : Auto Scheduled"));
    QString tip = manual ? QStringLiteral("New tasks are created in Manually Scheduled mode")
                         : QStringLiteral("New tasks are created in Auto Scheduled mode");
    if (!new_tasks_manual_known_) {
        tip = doc_.has_document()
                  ? tip + QStringLiteral(
                              " (this file does not store the setting; the default is shown)")
                  : QStringLiteral("New projects start with Manually Scheduled tasks");
    }
    status_mode_->setToolTip(tip);
}

void MainWindow::refresh_new_task_mode() {
    // W28-3.1: the baseline is what the loaded file stored; the LAST pending
    // mode edit is the user's newest choice.  Undo/redo therefore restore the
    // previous choice (or the file's value) without a separate save of the
    // shown state.
    bool manual = loaded_new_tasks_manual_;
    bool known = loaded_new_tasks_manual_known_;
    for (const app::StructuralEdit& edit : structural_.applied()) {
        if (edit.kind == app::StructuralKind::set_new_task_mode) {
            manual = edit.new_tasks_manual;
            known = true;
        }
    }
    new_tasks_manual_ = known ? manual : true;
    new_tasks_manual_known_ = known;
}

void MainWindow::clear_render_selection() {
    if (doc_.sheet() != nullptr) {
        doc_.sheet()->clear_selection();
    }
    if (sheet_view_ != nullptr) {
        sheet_view_->clearSelection();
        sheet_view_->setCurrentIndex(QModelIndex());
    }
    if (gantt_canvas_ != nullptr) {
        gantt_canvas_->set_selected_uid(0);
    }
}

QString MainWindow::status_left_text() const {
    return status_summary_ != nullptr ? status_summary_->text() : QString();
}

QString MainWindow::status_detail_text() const {
    if (!doc_.has_document()) {
        return QString();
    }
    const app::PlanSummary summary = doc_.summary();
    QString text = QString::number(static_cast<qulonglong>(summary.tasks)) +
                   QStringLiteral(" tasks, ") +
                   QString::number(static_cast<qulonglong>(summary.links)) +
                   QStringLiteral(" links, ") +
                   QString::number(static_cast<qulonglong>(doc_.warnings())) +
                   QStringLiteral(" warnings");
    if (doc_.filter_active()) {
        text += QStringLiteral(", filtered");
    }
    return text;
}

void MainWindow::set_message(std::string text, bool is_error) {
    message_ = std::move(text);
    message_is_error_ = is_error;
}

void MainWindow::update_zoom_slider() {
    if (zoom_slider_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(zoom_slider_);
    zoom_slider_->setValue(static_cast<int>(prefs_.zoom));
}

void MainWindow::update_chrome() {
    if (info_callout_ != nullptr) {
        // The reference shows the manual-scheduling notice for a new project.
        info_callout_->setVisible(!doc_.has_document());
    }
    refresh_backstage_info();
}

void MainWindow::refresh_backstage_info() {
    if (backstage_ == nullptr) {
        return;
    }
    if (!doc_.has_document()) {
        backstage_->set_info_text(
            QStringLiteral("New project"),
            QStringLiteral("No file is open. The shell starts on today's date, in Manually "
                           "Scheduled mode, with the Entry table and an empty first row."));
        return;
    }
    const std::string title = doc_.title().empty() ? doc_.path() : doc_.title();
    const app::PlanSummary summary = doc_.summary();
    const QString detail =
        QStringLiteral("File: ") + QString::fromStdString(doc_.path()) +
        QStringLiteral("\nTitle: ") + QString::fromStdString(title) +
        QStringLiteral("\nTasks: ") + QString::number(static_cast<qulonglong>(summary.tasks)) +
        QStringLiteral("   Links: ") + QString::number(static_cast<qulonglong>(summary.links)) +
        QStringLiteral("\nReader warnings: ") +
        QString::number(static_cast<qulonglong>(doc_.warnings())) +
        QStringLiteral("\n\nThese facts are read from the local file only; this port has no "
                       "Microsoft account or cloud project metadata.");
    backstage_->set_info_text(QString::fromStdString(title), detail);
}

void MainWindow::show_backstage(bool open) {
    if (backstage_ == nullptr || central_stack_ == nullptr) {
        return;
    }
    backstage_open_ = open;
    central_stack_->setCurrentWidget(open ? static_cast<QWidget*>(backstage_)
                                          : central_stack_->widget(0));
    // The product's backstage replaces the ribbon, QAT and status bar.
    if (quick_access_ != nullptr) {
        quick_access_->setVisible(!open);
    }
    if (statusBar() != nullptr) {
        statusBar()->setVisible(!open);
    }
    // The menu bar stays hidden in both states (wave 21 parity decision); it
    // exists only as the QAction owner the shortcuts and tests use.
    if (open) {
        refresh_backstage_info();
        // W26-4.1: the clean Account capture (no document) greys Save..Close;
        // a loaded document enables them.  Refresh on every open so the rail
        // never keeps a stale state.
        backstage_->set_document_state(doc_.has_document());
        // W26-4.2: the port's own identity, never Microsoft's.
        backstage_->set_account_info(
            QStringLiteral("MS Project Shell"),
            QStringLiteral("Version ") + QString::fromLatin1(kShellVersion),
            QStringLiteral("A personal three-platform port of MS Project. This port has no "
                           "Microsoft account, cloud service or license."));
        backstage_->set_theme_choice(theme_choice_);
        // W26-4.3: the local Recent list, read from the installed store.
        backstage_->set_recent_files(recent_files());
        backstage_->show_page(Backstage::Page::home);
        backstage_->setFocus();
    } else {
        if (central_stack_->widget(0) != nullptr) {
            central_stack_->widget(0)->setFocus();
        }
    }
    update();
}

void MainWindow::new_project() {
    // The File > New page: forget the open file and rebuild the empty shell.
    sheet_model_.set_sheet(nullptr);
    doc_.clear();
    document_.reset();
    structural_ = app::StructuralEdits{};
    owns_path_ = false;
    // Operator decision 11 / wave 22: a NEW project is Manually Scheduled,
    // whatever the file that was open before said.
    new_tasks_manual_ = true;
    new_tasks_manual_known_ = false;
    loaded_new_tasks_manual_ = true;
    loaded_new_tasks_manual_known_ = false;
    last_save_target_.clear();
    view_sheet_.reset();
    view_records_.clear();
    clipboard_ = core::sheet::ClipboardBlock{};
    prefs_.view = app::ViewMode::task_sheet;
    auto* header = static_cast<SheetHeaderView*>(sheet_view_->horizontalHeader());
    auto* delegate = static_cast<EntryItemDelegate*>(sheet_view_->itemDelegate());
    if (header != nullptr) {
        header->set_model_titles(false);
    }
    if (delegate != nullptr) {
        delegate->set_entry_mode(true);
    }
    if (chart_panel_ != nullptr) {
        chart_panel_->setVisible(true);
    }
    configure_task_columns();
    doc_.build_empty_view(prefs_.zoom, app::today_date32());
    gantt_canvas_->set_layout(doc_.gantt());
    gantt_canvas_->set_selected_uid(0);
    // Wave 26: a new project uses Project's defaults.
    timeline_visible_ = true;
    gantt_canvas_->set_critical_highlight(false);
    ribbon_->set_document_state(false, timeline_visible_, false);
    ribbon_->set_critical_tasks(false);
    ribbon_->set_font_default(QString(), 0);
    refresh_timeline();
    // W30-1.10: a new project has no task to inspect; an open pane shows the
    // cleared state instead of the file that was open before.
    refresh_inspector();
    setWindowTitle(QStringLiteral("MS Project Shell"));
    refresh_view_buttons();
    update_chrome();
    update_menu_state();
    update_status();
    show_backstage(false);
}

void MainWindow::update_window_title() {
    if (!doc_.has_document()) {
        setWindowTitle(QStringLiteral("MS Project Shell"));
        return;
    }
    // Wave 23 (B-10): Project's title is "<file name>.mpp - Project
    // Professional", not the project title plus the full path.  The port's own
    // identity replaces the product name (operator ruling 5).
    std::string file_name = doc_.loaded().source_name;
    if (file_name.empty()) {
        file_name = QFileInfo(to_q(doc_.path())).fileName().toStdString();
    }
    if (file_name.empty()) {
        file_name = "untitled.mpp";
    }
    QString text = to_q(file_name) + QStringLiteral(" - MS Project Shell");
    // The reference shows the standard edited dot; text is the portable form.
    if (document_dirty()) {
        text += QStringLiteral(" *");
    }
    setWindowTitle(text);
}

void MainWindow::refresh_view_buttons() {
    for (std::size_t index = 0; index < view_buttons_.size() &&
                                index < std::size(kViewButtons);
         ++index) {
        QToolButton* button = view_buttons_[index];
        if (button == nullptr) {
            continue;
        }
        const QSignalBlocker blocker(button);
        button->setChecked(kViewButtons[index].implemented &&
                           kViewButtons[index].mode == prefs_.view);
    }
}

std::string MainWindow::stored_widths_text() const {
    // The `214/CTable` width byte (characters) for each Entry column, in sheet
    // order.  A zero means the file/table did not carry that field.
    std::string text;
    for (const EntryColumn& column : entry_columns()) {
        int chars = doc_.table_column_chars(column.table_field);
        if (chars == 0 && column.kind == EntryCellKind::mode) {
            // Some Entry tables carry the Indicators field (0x0b4000cd) in the
            // mode slot; report the first width that exists.
            chars = doc_.table_column_chars(0x0b4000cd);
        }
        if (!text.empty()) {
            text += "/";
        }
        text += std::to_string(chars);
    }
    return text;
}

std::string MainWindow::summary_text() const {
    if (!doc_.has_document()) {
        return "Ready";
    }
    const app::PlanSummary summary = doc_.summary();
    std::string text = std::to_string(summary.tasks) + " tasks, " +
                       std::to_string(summary.links) + " links";
    if (doc_.loaded().skipped_links != 0) {
        text += " (" + std::to_string(doc_.loaded().skipped_links) + " skipped)";
    }
    text += ", " + std::to_string(summary.summaries) + " summaries, " +
            std::to_string(summary.milestones) + " milestones, " +
            std::to_string(summary.critical) + " critical";
    if (!core::sched::is_na(summary.first_start) && !core::sched::is_na(summary.last_finish)) {
        text += "  |  plan " +
                core::sheet::format_date_text(app::date_value_from_date32(summary.first_start),
                                              doc_.format());
        text += " - " +
                core::sheet::format_date_text(app::date_value_from_date32(summary.last_finish),
                                              doc_.format());
    }
    text += "  |  " + std::to_string(doc_.warnings()) + " reader warnings";
    text += "  |  view " + std::string(app::view_mode_title(prefs_.view));
    if (prefs_.view == app::ViewMode::task_usage ||
        prefs_.view == app::ViewMode::resource_usage) {
        text += " (" + std::string(core::usage::period_name(prefs_.usage_period)) + ")";
    }
    if (prefs_.view == app::ViewMode::task_sheet) {
        text += ", timescale " + std::string(app::zoom_level_name(prefs_.zoom)) + ", shading " +
                std::string(prefs_.working_day_shading ? "on" : "off");
    }
    if (doc_.filter_active()) {
        const std::size_t total = doc_.records().row_count();
        text += "  |  filter: " + doc_.filter_description() + " (" +
                std::to_string(doc_.display_rows().size()) + " of " +
                std::to_string(total) + ")";
    }
    if (document_dirty()) {
        text += "  |  unsaved: " + std::to_string(pending_start_edits()) + " start(s), " +
                std::to_string(structural_.size()) + " structural edit(s)";
    }
    if (doc_.table_widths_read() && prefs_.view == app::ViewMode::task_sheet) {
        text += "  |  Entry widths (chars) " + stored_widths_text();
    }
    return text;
}

bool MainWindow::document_dirty() const {
    if (!doc_.has_document()) {
        return false;
    }
    if (!structural_.empty()) {
        return true;
    }
    // Wave 20: a pending 214/CTable view-state or 114/Props formula request is a
    // file change too (app::Document::has_pending_edits()).
    if (document_ != nullptr && document_->has_pending_edits()) {
        return true;
    }
    return !app::collect_start_edits(doc_.records(), doc_.loaded()).empty();
}

std::size_t MainWindow::pending_start_edits() const {
    if (!doc_.has_document()) {
        return 0;
    }
    return app::collect_start_edits(doc_.records(), doc_.loaded()).size();
}

std::size_t MainWindow::loaded_task_count() const {
    return doc_.has_document() ? doc_.loaded().tasks.size() : 0;
}

std::string MainWindow::today_text() const {
    return core::sheet::format_date_text(
        app::date_value_from_date32(app::today_date32()), doc_.format());
}

void MainWindow::refresh_timeline() {
    if (timeline_ == nullptr) {
        return;
    }
    // Wave 24 (W24-3): the strip's visibility is FILE state, stored in the
    // view props as 0x03400008 ("Time&line" shown, empty hidden).  A fresh
    // Project 2021 session reopened a shown/hidden save pair with the strip
    // shown/hidden, and the two saves differ semantically only in that record
    // (plus the per-save GUID).  Wave 23's "application state" conclusion came
    // from a diff that did not decode the Props record framing.  A new project
    // keeps Project's default (shown).
    // Wave 26: the strip's visibility is the RUNTIME state, seeded from the
    // file's stored setting on load (wave 24) and toggled by the View > Split
    // View > Timeline check box.  Nothing here writes the file.
    timeline_->setVisible(timeline_visible_);
    if (!doc_.has_document()) {
        // A new project: Start and Finish are today, and the strip shows the
        // product's prompt because nothing has been added to the timeline.
        const QString today = QString::fromStdString(today_text());
        timeline_->set_dates(today, today);
        timeline_->set_tasks({});
        return;
    }
    const app::PlanSummary summary = doc_.summary();
    if (core::sched::is_na(summary.first_start) || core::sched::is_na(summary.last_finish)) {
        timeline_->set_dates(QString(), QString());
        timeline_->set_tasks({});
        return;
    }
    timeline_->set_dates(
        QString::fromStdString(core::sheet::format_date_text(
            app::date_value_from_date32(summary.first_start), doc_.format())),
        QString::fromStdString(core::sheet::format_date_text(
            app::date_value_from_date32(summary.last_finish), doc_.format())));

    // Draw every task with dates at its fraction of the project window.  The
    // window is inclusive of the last day; a bar's width is its own day count.
    const std::int32_t first_day =
        core::sched::day_of(core::sched::date_only(summary.first_start));
    const std::int32_t last_day =
        core::sched::day_of(core::sched::date_only(summary.last_finish));
    const double span_days = static_cast<double>(std::max<std::int32_t>(1, last_day - first_day + 1));
    std::vector<msqt::TimelineTask> tasks;
    for (const app::TaskRow& task : doc_.plan().tasks) {
        if (core::sched::is_na(task.start) || core::sched::is_na(task.finish)) {
            continue;
        }
        const std::int32_t start_day =
            core::sched::day_of(core::sched::date_only(task.start));
        const std::int32_t finish_day =
            core::sched::day_of(core::sched::date_only(task.finish));
        if (finish_day < start_day) {
            continue;
        }
        msqt::TimelineTask entry;
        entry.label = QString::fromStdString(task.name);
        entry.start = static_cast<double>(start_day - first_day) / span_days;
        entry.width = static_cast<double>(finish_day - start_day + 1) / span_days;
        tasks.push_back(std::move(entry));
    }
    timeline_->set_tasks(std::move(tasks));
}

void MainWindow::update_menu_state() {
    app::MenuState state;
    state.has_document = doc_.has_document();
    const core::sheet::SheetModel* sheet = doc_.sheet();
    const bool task_view = prefs_.view == app::ViewMode::task_sheet;
    state.editing = task_view && sheet != nullptr && sheet->editing();
    state.has_selection = task_view && sheet != nullptr && sheet->has_selection();
    state.can_undo = task_view && ((sheet != nullptr && sheet->can_undo()) ||
                                  structural_.can_undo());
    state.can_redo = task_view && ((sheet != nullptr && sheet->can_redo()) ||
                                  structural_.can_redo());
    // Wave 20: the sheet's clipboard is core::sheet::SheetModel::copy_selection /
    // paste.  Copy/Cut need a selection; Paste also needs a block and a target
    // cell.  Cut is enabled here rather than by app::menu_item_enabled(), because
    // the shared action table still marks it unsupported for the macOS shell.
    state.can_copy = task_view && sheet != nullptr && sheet->has_selection();
    state.can_paste =
        task_view && sheet != nullptr && sheet->has_selection() && !clipboard_.empty();
    state.dirty = document_dirty();
    state.filter_active = doc_.filter_active();
    state.task_view = task_view;
    if (task_view && sheet != nullptr) {
        for (std::size_t row = 0; row < sheet->row_count(); ++row) {
            if (!sheet->is_summary(row)) {
                continue;
            }
            if (sheet->is_collapsed(row)) {
                state.any_collapsed = true;
            } else {
                state.any_expanded = true;
            }
        }
    }
    for (QAction* action : actions_) {
        const app::MenuAction id = static_cast<app::MenuAction>(action->data().toInt());
        bool enabled = app::menu_item_enabled(id, state) && shell_implements(id);
        if (id == app::MenuAction::cut) {
            enabled = state.task_view && state.has_document && state.can_copy && !state.editing;
        }
        action->setEnabled(enabled);
    }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void MainWindow::handle_action(app::MenuAction action) {
    switch (action) {
        case app::MenuAction::none:
            return;
        case app::MenuAction::open:
            open_dialog();
            return;
        case app::MenuAction::quit_app:
        case app::MenuAction::close_window:
            close();
            return;
        case app::MenuAction::about:
            show_about();
            return;
        case app::MenuAction::keyboard_shortcuts:
            show_shortcuts();
            return;
        case app::MenuAction::save:
            save_document(false);
            return;
        case app::MenuAction::save_as:
            save_document(true);
            return;
        case app::MenuAction::print:
            print_plan();
            return;
        case app::MenuAction::save_as_pdf:
            save_as_pdf();
            return;
        case app::MenuAction::export_pdf_sheet:
            export_pdf(app::ReportKind::sheet);
            return;
        case app::MenuAction::export_pdf_gantt:
            export_pdf(app::ReportKind::gantt);
            return;
        case app::MenuAction::show_filter: {
            if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
                set_message("filtering is available in the Task Sheet view", false);
                update_status();
                return;
            }
            QMenu* menu = build_filter_menu();
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(QCursor::pos());
            return;
        }
        case app::MenuAction::clear_filter:
            clear_filter();
            return;
        case app::MenuAction::insert_task:
            insert_task();
            return;
        case app::MenuAction::delete_task:
            delete_task();
            return;
        case app::MenuAction::indent:
            indent_selected(true);
            return;
        case app::MenuAction::outdent:
            indent_selected(false);
            return;
        case app::MenuAction::task_fields:
            // Wave 23 (B-13): Task > Information is Task Information now.
            show_task_information();
            return;
        case app::MenuAction::custom_fields:
            // Wave 26 (W26-5.3): the Custom Fields command is the field
            // DEFINITION dialog; per-task values are Task Information's Custom
            // Fields tab.
            show_custom_fields();
            return;
        case app::MenuAction::predecessors:
            show_predecessors();
            return;
        case app::MenuAction::preferences:
            show_preferences();
            return;
        case app::MenuAction::table_date_format:
            show_table_date_format();
            return;
        case app::MenuAction::custom_field_formula:
            show_custom_formula();
            return;
        case app::MenuAction::cut:
            cut_selection();
            return;
        case app::MenuAction::copy:
            copy_selection();
            return;
        case app::MenuAction::paste:
            paste_selection();
            return;
        case app::MenuAction::view_task_sheet:
            set_view(app::ViewMode::task_sheet);
            return;
        case app::MenuAction::view_resource_sheet:
            set_view(app::ViewMode::resource_sheet);
            return;
        case app::MenuAction::view_task_usage:
            set_view(app::ViewMode::task_usage);
            return;
        case app::MenuAction::view_resource_usage:
            set_view(app::ViewMode::resource_usage);
            return;
        case app::MenuAction::usage_day:
            set_usage_period(core::usage::Period::day);
            return;
        case app::MenuAction::usage_week:
            set_usage_period(core::usage::Period::week);
            return;
        case app::MenuAction::usage_month:
            set_usage_period(core::usage::Period::month);
            return;
        case app::MenuAction::zoom_in:
            set_zoom(stepped_zoom(prefs_.zoom, 1));
            return;
        case app::MenuAction::zoom_out:
            set_zoom(stepped_zoom(prefs_.zoom, -1));
            return;
        case app::MenuAction::zoom_day:
            set_zoom(app::ZoomLevel::day);
            return;
        case app::MenuAction::zoom_week:
            set_zoom(app::ZoomLevel::week);
            return;
        case app::MenuAction::zoom_month:
            set_zoom(app::ZoomLevel::month);
            return;
        case app::MenuAction::toggle_shading:
            prefs_.working_day_shading = !prefs_.working_day_shading;
            gantt_canvas_->set_shading(prefs_.working_day_shading);
            set_message(std::string("working-day shading ") +
                            (prefs_.working_day_shading ? "on" : "off"),
                        false);
            update_status();
            return;
        case app::MenuAction::collapse_all:
            if (doc_.sheet() != nullptr && prefs_.view == app::ViewMode::task_sheet) {
                doc_.sheet()->collapse_all();
                after_model_change();
            }
            return;
        case app::MenuAction::expand_all:
            if (doc_.sheet() != nullptr && prefs_.view == app::ViewMode::task_sheet) {
                doc_.sheet()->expand_all();
                after_model_change();
            }
            return;
        case app::MenuAction::select_all:
            if (doc_.sheet() != nullptr && prefs_.view == app::ViewMode::task_sheet) {
                doc_.sheet()->select_all();
                gantt_canvas_->set_selected_uid(doc_.selected_uid());
                sync_view_selection();
                update_menu_state();
            }
            return;
        case app::MenuAction::undo: {
            if (doc_.sheet() == nullptr) {
                return;
            }
            if (doc_.sheet()->editing()) {
                doc_.sheet()->cancel_edit();
            }
            if (doc_.sheet()->can_undo()) {
                const std::string label = doc_.sheet()->undo_label();
                doc_.sheet()->undo();
                after_cell_change();
                set_message("undo " + label, false);
                update_status();
            } else if (structural_.can_undo()) {
                structural_.undo();
                after_structural_change();
                set_message("undo the last structural edit", false);
                update_status();
            } else {
                set_message("nothing to undo", false);
                update_status();
            }
            return;
        }
        case app::MenuAction::redo: {
            if (doc_.sheet() == nullptr) {
                return;
            }
            if (doc_.sheet()->editing()) {
                doc_.sheet()->cancel_edit();
            }
            if (doc_.sheet()->can_redo()) {
                const std::string label = doc_.sheet()->redo_label();
                doc_.sheet()->redo();
                after_cell_change();
                set_message("redo " + label, false);
                update_status();
            } else if (structural_.can_redo()) {
                structural_.redo();
                after_structural_change();
                set_message("redo the structural edit", false);
                update_status();
            } else {
                set_message("nothing to redo", false);
                update_status();
            }
            return;
        }
        default:
            break;
    }
    set_message(std::string("'") + app::menu_action_name(action) +
                    "' is not wired in the Qt shell yet",
                false);
    update_status();
}

// ---------------------------------------------------------------------------
// Wave 26 (G1): the ribbon controls that are not menu actions
// ---------------------------------------------------------------------------

void MainWindow::handle_ribbon_command(RibbonCommand command, bool checked) {
    switch (command) {
        case RibbonCommand::none:
            return;
        case RibbonCommand::percent_0:
        case RibbonCommand::percent_25:
        case RibbonCommand::percent_50:
        case RibbonCommand::percent_75:
        case RibbonCommand::percent_100:
            // W28-1.2: enabled on the measured actuals writer.  One click queues
            // one set_percent edit through the same make_percent_edit/push path
            // Save uses; the writer stores the whole actuals block, not +92.
            set_selected_percent(command == RibbonCommand::percent_0    ? 0
                                 : command == RibbonCommand::percent_25 ? 25
                                 : command == RibbonCommand::percent_50 ? 50
                                 : command == RibbonCommand::percent_75 ? 75
                                                                        : 100);
            return;
        case RibbonCommand::link_tasks:
            // The shell's direct link command is the Predecessors dialog: the
            // file's link table is edited through app::StructuralEdits there.
            show_predecessors();
            return;
        case RibbonCommand::unlink_tasks:
            unlink_selected_task();
            return;
        case RibbonCommand::toggle_timeline:
            set_timeline_visible(checked);
            return;
        case RibbonCommand::toggle_critical_tasks:
            if (gantt_canvas_ != nullptr) {
                gantt_canvas_->set_critical_highlight(checked);
            }
            set_message(checked ? "critical tasks highlighting on"
                                : "critical tasks highlighting off",
                        false);
            update_status();
            return;
        case RibbonCommand::toggle_project_summary: {
            if (!doc_.has_document() || doc_.sheet() == nullptr) {
                set_message("open a project first", false);
                update_status();
                return;
            }
            // The RecordSet is rebuilt, so detach the adapter first -- the same
            // rule after_structural_change() follows.
            sheet_model_.set_sheet(nullptr);
            doc_.set_project_summary_visible(checked, prefs_.zoom);
            rebind_sheet();
            sync_view_selection();
            gantt_canvas_->set_layout(doc_.gantt());
            gantt_canvas_->set_selected_uid(doc_.selected_uid());
            refresh_timeline();
            update_window_title();
            update_menu_state();
            set_message(checked ? "project summary task shown"
                                : "project summary task hidden",
                        false);
            update_status();
            return;
        }
        case RibbonCommand::task_details:
            // W30-1.5: the Task tab's Details opens the same Task Form pane
            // View > Details toggles (decision 16).  It only SHOWS the pane --
            // the View check box is the toggle -- and it is a view command,
            // never a file write.
            if (ribbon_ != nullptr) {
                ribbon_->set_details_checked(true);
            }
            set_details_visible(true);
            return;
        case RibbonCommand::inspect:
            // W30-1.10: the Task Inspector is a read-only pane of the
            // scheduler's facts.  There is no View menu check box for it, so
            // the ribbon button toggles it.  A view command: nothing is
            // queued, written or marked dirty.
            set_inspector_visible(!inspector_visible_);
            return;
        case RibbonCommand::resource_details:
            // W30-2.2: the Resource tab's Details docks the read-only Resource
            // Form, which follows the Resource Sheet's selected row.  Project's
            // form is a Resource-view pane, so a click from another view first
            // switches to the Resource Sheet (the pane then follows clicks
            // there).  Like the Task tab's Details it only SHOWS the pane; the
            // View check box stays the toggle.  A view command, never a file
            // write.
            if (prefs_.view != app::ViewMode::resource_sheet) {
                set_view(app::ViewMode::resource_sheet);
            }
            if (ribbon_ != nullptr) {
                ribbon_->set_details_checked(true);
            }
            set_details_visible(true);
            return;
        case RibbonCommand::scroll_to_task:
            // W30-1.7: scrolls the sheet and pans the chart; a view command,
            // never a file write.
            scroll_to_task();
            return;
        case RibbonCommand::fill_down:
            // W30-1.8: fills a Start-column selection through the start
            // writer.  W31-0.4 (R30-4): the ribbon button and the row menu are
            // disabled (the committed Project pair shows the save is a
            // scheduler rewrite the port's start writer does not reproduce),
            // so this handler is unreachable from the UI; it stays for the
            // shape refusals (summary rows, U1) behind the disabled control.
            fill_down();
            return;
        case RibbonCommand::toggle_details:
            // W26-6.3: dock/undock the Task Form; a view toggle, never a file
            // write.
            set_details_visible(checked);
            return;
        case RibbonCommand::outdent:
            indent_selected(false);
            return;
        case RibbonCommand::indent:
            indent_selected(true);
            return;
        case RibbonCommand::manually_schedule:
            manually_schedule_selected();
            return;
        case RibbonCommand::task_mode:
            show_task_mode_menu();
            return;
        case RibbonCommand::data_sort:
            show_sort_dialog();
            return;
        case RibbonCommand::data_outline:
            show_outline_menu();
            return;
        case RibbonCommand::data_tables:
            show_tables_menu();
            return;
        case RibbonCommand::zoom_dialog:
            // Wave 31 (W31-2.1): the View tab's Zoom dialog.  Its result is a
            // session-only timescale fit (apply_zoom_choice).
            show_zoom_dialog();
            return;
        case RibbonCommand::zoom_entire: {
            // Wave 31 (W31-2.2): the ribbon's two span commands run the same
            // fits as the dialog's Entire project / Selected tasks choices.
            ZoomChoice choice;
            apply_zoom_choice(choice);
            return;
        }
        case RibbonCommand::zoom_selected: {
            ZoomChoice choice;
            choice.target = ZoomTarget::selected_tasks;
            apply_zoom_choice(choice);
            return;
        }
        case RibbonCommand::split_task:
            // Not wired (no engine split edit); disabled until then.
            return;
        case RibbonCommand::project_information:
            show_project_information();
            return;
        case RibbonCommand::change_working_time:
            show_change_working_time();
            return;
        case RibbonCommand::links_between_projects:
            show_links_between_projects();
            return;
        case RibbonCommand::update_project:
            show_update_project();
            return;
        case RibbonCommand::mark_on_track:
            mark_on_track();
            return;
        case RibbonCommand::move_project:
            show_move_project();
            return;
        case RibbonCommand::wbs_menu:
            set_message("WBS: no command is implemented in this port", false);
            update_status();
            return;
        case RibbonCommand::set_baseline_menu:
            set_message("Set Baseline: no command is implemented in this port", false);
            update_status();
            return;
        case RibbonCommand::text_styles:
            show_text_styles();
            return;
        case RibbonCommand::gridlines:
            show_gridlines();
            return;
        case RibbonCommand::bar_styles:
            show_bar_styles();
            return;
        case RibbonCommand::visual_reports:
            show_visual_reports();
            return;
        case RibbonCommand::compare_projects:
            show_compare_projects();
            return;
        case RibbonCommand::spelling:
            show_spelling();
            return;
    }
}

bool MainWindow::critical_tasks_highlight() const noexcept {
    return gantt_canvas_ != nullptr && gantt_canvas_->critical_highlight();
}

void MainWindow::set_timeline_visible(bool visible) {
    timeline_visible_ = visible;
    if (timeline_ != nullptr) {
        timeline_->setVisible(visible);
    }
    set_message(std::string("timeline ") + (visible ? "shown" : "hidden"), false);
    update_status();
}

void MainWindow::set_details_visible(bool visible) {
    details_visible_ = visible;
    const bool task_view = prefs_.view == app::ViewMode::task_sheet;
    const bool resource_view = prefs_.view == app::ViewMode::resource_sheet;
    if (task_form_ != nullptr) {
        // Details docks the Task Form in the Gantt/Task Sheet view and the
        // Resource Form in the Resource Sheet (W30-2.2).  The preference
        // survives a view switch, so returning re-docks the matching form.
        task_form_->setVisible(visible && task_view);
    }
    if (resource_form_ != nullptr) {
        resource_form_->setVisible(visible && resource_view);
    }
    if (visible && (task_view || resource_view) && details_splitter_ != nullptr) {
        const int total = details_splitter_->height();
        const int form_height = std::max(150, total / 4);
        details_splitter_->setSizes(
            QList<int>{std::max(1, total - form_height -
                                       details_splitter_->handleWidth()),
                       form_height});
    }
    if (visible) {
        if (task_view) {
            refresh_task_form();
        } else if (resource_view) {
            refresh_resource_form();
        }
    }
    set_message(visible ? "details pane shown" : "details pane hidden", false);
    update_status();
}

void MainWindow::set_inspector_visible(bool visible) {
    // W30-1.10: the Task Inspector is a task-view pane.  The preference
    // survives a view switch (show_view() re-applies it), exactly like Details.
    inspector_visible_ = visible;
    if (inspector_ != nullptr) {
        const bool show = visible && prefs_.view == app::ViewMode::task_sheet;
        inspector_->setVisible(show);
        if (show) {
            refresh_inspector();
        }
    }
    set_message(visible ? "task inspector shown" : "task inspector hidden", false);
    update_status();
}

void MainWindow::scroll_to_task() {
    // W30-1.7: Task > Editing > Scroll to Task.  The sheet scrolls the selected
    // row into view (the row menu has run the same call since wave 21) and the
    // chart pans its left edge to the task's start.  Both are view operations:
    // the RecordSet, the plan and the save diff are untouched, so the command
    // can never make the document dirty.
    if (sheet_view_ == nullptr || !doc_.has_document()) {
        return;
    }
    sheet_view_->scrollTo(sheet_view_->currentIndex());
    const std::uint32_t uid = doc_.selected_uid();
    if (uid != 0 && doc_.scroll_gantt_to(uid, prefs_.zoom) &&
        gantt_canvas_ != nullptr) {
        gantt_canvas_->set_layout(doc_.gantt());
        gantt_canvas_->set_selected_uid(doc_.selected_uid());
    }
}

void MainWindow::refresh_task_form() {
    if (task_form_ == nullptr) {
        return;
    }
    // W28-4.1: the uid and the texts are set together, so a commit that arrives
    // after a selection change is either for the row now shown or is dropped by
    // commit_task_form_field.  Never leave a stale uid behind a cleared form.
    task_form_uid_ = 0;
    if (prefs_.view != app::ViewMode::task_sheet || doc_.sheet() == nullptr) {
        task_form_->clear();
        return;
    }
    TaskInformationValues info;
    if (!task_information_values(&info)) {
        task_form_->clear();
        return;
    }
    TaskFormValues values;
    values.has_task = true;
    values.name = to_q(info.name);
    values.duration = to_q(info.duration_text);
    values.start = to_q(info.start_text);
    values.finish = to_q(info.finish_text);
    values.percent = to_q(info.percent_text);
    // Predecessors and Resources are the COMPUTED columns the stored table
    // shows (PlanDocument::fill_computed_columns writes them into the
    // RecordSet), read through the SAME selected RecordSet row as the four
    // fields above -- never a plan index.
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    if (selected.has_selection && selected.row < doc_.records().row_count()) {
        auto computed = [this, &selected](core::view::FieldId field) {
            const core::view::Value* value = doc_.records().get(selected.row, field);
            return value != nullptr ? to_q(value->text) : QString();
        };
        values.predecessors = computed(kPredecessorsField);
        values.resources = computed(kResourceNamesField);
    }
    task_form_->set_values(values);
    task_form_uid_ = selected.uid;

    // W28-4.1: a field edits through the sheet's own cell session, so it is
    // enabled exactly when the file's table shows an editable column for it
    // (the same lookup apply_task_information performs).  Percent complete has
    // no sheet cell path -- its column is read-only -- and queues the measured
    // set_percent edit the ribbon's % buttons and Task Information queue, so it
    // needs no column and stays enabled.
    const auto column_for = [this](std::string_view field_name) -> std::size_t {
        const core::view::FieldId field = app::task_field_id(field_name);
        for (std::size_t column = 0; column < doc_.sheet()->column_count(); ++column) {
            if (doc_.sheet()->column(column).field == field) {
                return column;
            }
        }
        return core::sheet::kNoIndex;
    };
    const auto editable = [this, &column_for](const char* field_name) {
        const std::size_t column = column_for(field_name);
        return column != core::sheet::kNoIndex && !doc_.sheet()->column_read_only(column);
    };
    task_form_->set_field_enabled(TaskFormField::name, editable("Name"));
    task_form_->set_field_enabled(TaskFormField::duration, editable("Duration"));
    task_form_->set_field_enabled(TaskFormField::start, editable("Start"));
    task_form_->set_field_enabled(TaskFormField::finish, editable("Finish"));
    task_form_->set_field_enabled(TaskFormField::percent, true);
}

void MainWindow::refresh_inspector() {
    // W30-1.10: the read-only Task Inspector.  Facts come from the PLAN, by
    // UID: the task form reads the sheet's RecordSet presentation, while this
    // pane reports the scheduler's inputs (the same plan the Gantt and the
    // computed columns are built from).  uid 0 is the synthetic project row;
    // it is not a task and has no file row, so it is presented from the file's
    // project facts instead.
    if (inspector_ == nullptr) {
        return;
    }
    if (!doc_.has_document() || doc_.sheet() == nullptr ||
        prefs_.view != app::ViewMode::task_sheet) {
        inspector_->clear();
        return;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    if (!selected.has_selection) {
        inspector_->clear();
        return;
    }

    const app::PlanInput& plan = doc_.plan();
    TaskInspectorValues values;
    values.has_task = true;

    const app::TaskRow* task = nullptr;
    app::TaskRow project_row;
    if (selected.uid == 0) {
        project_row = app::project_summary_row(plan);
        task = &project_row;
        values.project = true;
        values.type = QStringLiteral("Project summary");
        values.calendar = QStringLiteral("Project calendar");
        values.selection = QStringLiteral("Project summary (ID 0)");
    } else {
        task = doc_.task_by_uid(selected.uid);
        if (task == nullptr) {
            inspector_->clear();
            return;
        }
        values.outline = to_q(task->outline_number);
        if (task->file_row == 0) {
            // A pending Insert Task row has no file row and no stored mode:
            // saying "Automatic" would report a fact the file does not hold.
            values.type = QStringLiteral("Task (not saved yet)");
            values.mode = QStringLiteral("not saved yet");
        } else {
            values.type = task->milestone ? QStringLiteral("Milestone")
                         : task->summary ? QStringLiteral("Summary")
                                         : QStringLiteral("Task");
            values.mode = task->manually_scheduled ? QStringLiteral("Manually Scheduled")
                                                   : QStringLiteral("Auto Scheduled");
        }
        values.calendar = task->calendar_uid < 0
                              ? QStringLiteral("Project calendar")
                              : QStringLiteral("Task calendar (UniqueID %1)")
                                    .arg(task->calendar_uid);
        values.selection = QStringLiteral("Task ID %1 (UniqueID %2)")
                               .arg(task->id)
                               .arg(task->uid);
    }

    values.name = to_q(task->name);
    values.id = QString::number(task->id);
    values.uid = QString::number(task->uid);
    const auto date_text = [this](core::sched::date32 day) {
        return core::sched::is_na(day)
                   ? QString()
                   : to_q(core::sheet::format_date_text(
                         app::date_value_from_date32(day), doc_.format()));
    };
    values.start = date_text(task->start);
    values.finish = date_text(task->finish);
    values.duration = to_q(core::sheet::format_duration_text(task->duration_ticks,
                                                             doc_.format()));
    values.percent = to_q(core::sheet::format_value_text(
        core::view::Value::of_int(task->percent_complete),
        core::sheet::CellKind::Percent, doc_.format()));
    values.critical = task->critical ? QStringLiteral("Yes") : QStringLiteral("No");
    // The three computed texts are the SAME derivations PlanDocument writes
    // into the computed sheet columns, so the pane and the sheet cannot drift.
    values.predecessors = to_q(predecessors_text(plan, task->uid, doc_.format()));
    values.successors = to_q(successors_text(plan, task->uid, doc_.format()));
    values.resources = to_q(resource_names_text(plan, task->uid));
    inspector_->set_values(values);
}

void MainWindow::refresh_resource_form() {
    // W30-2.2: the Resource Form reports the plan's own resource facts, by
    // UID.  The Resource Sheet's generic RecordSet does not participate in the
    // task sheet's selection model, so the selected row is the view's current
    // index mapped through the adapter; the UID it carries is the plan key.
    if (resource_form_ == nullptr) {
        return;
    }
    if (!doc_.has_document() || prefs_.view != app::ViewMode::resource_sheet ||
        sheet_view_ == nullptr || view_sheet_ == nullptr) {
        resource_form_->clear();
        return;
    }
    const QModelIndex current = sheet_view_->currentIndex();
    if (!current.isValid()) {
        resource_form_->clear();
        return;
    }
    const std::size_t row = sheet_model_.record_row(current.row());
    if (row == core::sheet::kNoIndex) {
        resource_form_->clear();
        return;
    }
    const std::uint32_t uid = view_sheet_->row_uid(row);
    const app::ResourceRow* resource = nullptr;
    for (const app::ResourceRow& candidate : doc_.plan().resources) {
        if (candidate.uid == uid) {
            resource = &candidate;
            break;
        }
    }
    if (resource == nullptr) {
        resource_form_->clear();
        return;
    }

    ResourceFormValues values;
    values.has_resource = true;
    values.name = to_q(resource->name);
    values.id = QString::number(resource->id);
    values.uid = QString::number(resource->uid);
    values.type = to_q(core::view::resource_type_name(resource->type));
    values.initials = to_q(resource->initials);
    values.max_units = to_q(core::sheet::format_value_text(
        core::view::Value::of_number(resource->max_units_percent),
        core::sheet::CellKind::Percent, doc_.format()));
    // Work and Assignments are the plan's assignment facts for this UID: the
    // same rows the usage views are built from, totalled in the file's ticks.
    std::int64_t work_ticks = 0;
    std::size_t assignment_count = 0;
    for (const app::AssignmentRow& assignment : doc_.plan().assignments) {
        if (assignment.resource_uid != uid) {
            continue;
        }
        ++assignment_count;
        work_ticks += assignment.work_ticks;
    }
    values.work = to_q(core::sheet::format_duration_text(work_ticks, doc_.format()));
    values.assignments = QString::number(assignment_count);
    values.selection = QStringLiteral("Resource ID %1 (UniqueID %2)")
                           .arg(resource->id)
                           .arg(resource->uid);
    resource_form_->set_values(values);
}

void MainWindow::commit_task_form_field(TaskFormField field, const QString& text) {
    // The typed value belongs to the task the form was populated for.  A
    // selection change re-points the form's texts and task_form_uid_ together
    // (refresh_task_form), so a commit that arrives afterwards is either
    // unchanged for the new row or names a row the form no longer shows: drop
    // it rather than write it to the wrong task.  (The line edit's focus-out
    // can be delivered around a click that moved the sheet's selection.)
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
        doc_.sheet() == nullptr || task_form_uid_ == 0) {
        refresh_task_form();
        return;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    if (!selected.has_selection || selected.uid != task_form_uid_) {
        refresh_task_form();
        return;
    }
    TaskInformationValues values;
    if (!task_information_values(&values)) {
        refresh_task_form();
        return;
    }
    std::string* target = nullptr;
    switch (field) {
        case TaskFormField::name: target = &values.name; break;
        case TaskFormField::duration: target = &values.duration_text; break;
        case TaskFormField::start: target = &values.start_text; break;
        case TaskFormField::finish: target = &values.finish_text; break;
        case TaskFormField::percent: target = &values.percent_text; break;
    }
    const std::string typed = text.toStdString();
    if (target == nullptr || *target == typed) {
        // Return on the value the task already carries is not a file change and
        // must not become an undo step (nor a queued identical percent save).
        refresh_task_form();
        return;
    }
    *target = typed;
    // The same call Task Information's OK makes: the four cell fields commit
    // through the sheet's begin_edit/commit_edit session; a changed percent
    // queues the measured set_percent edit.  A refusal is its status message.
    apply_task_information(values, "task form");
    // A refused value is still in the line edit; show the value the sheet kept.
    // (On success apply_task_information has already refreshed the form.)
    refresh_task_form();
}

void MainWindow::unlink_selected_task() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
        doc_.sheet() == nullptr) {
        set_message("unlinking is available in the Task Sheet view", false);
        update_status();
        return;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    if (!selected.has_selection || selected.uid == 0) {
        set_message("select a task first", false);
        update_status();
        return;
    }
    // Project's Unlink Tasks removes every link touching the selection, not just
    // its incoming ones: the W27-3.3 COM measurement on uid 4 of the outline
    // fixture took TaskDependencies from 3 to 1 by deleting 3 -> 4 FS+2 d AND
    // 4 -> 6 FF.  saved_links_touching is therefore both-directional.
    const std::vector<app::TaskLinkItem> links =
        app::saved_links_touching(doc_.plan(), structural_, selected.uid);
    std::size_t removed = 0;
    std::string error;
    for (const app::TaskLinkItem& item : links) {
        app::StructuralEdit edit;
        if (!app::make_link_delete_edit(item, edit, error)) {
            continue;
        }
        if (structural_.push(edit)) {
            ++removed;
        }
    }
    if (removed == 0) {
        set_message("the selected task has no saved link to unlink", false);
        update_status();
        return;
    }
    after_structural_change(selected.uid);
    set_message("removed " + std::to_string(removed) +
                    " link(s); File > Save writes the change",
                false);
    update_status();
}

void MainWindow::set_selected_percent(std::uint16_t percent) {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
        doc_.sheet() == nullptr) {
        set_message("percent complete is available in the Task Sheet view", false);
        update_status();
        return;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    if (!selected.has_selection || selected.uid == 0) {
        set_message("select a task first", true);
        update_status();
        return;
    }
    // W28-1.2: the leaf/saved-row/summary rules live in the edit model
    // (make_percent_edit); the shell only hands it the displayed plan, which
    // includes a pending insert the model must refuse (file_row == 0).
    app::StructuralEdit edit;
    std::string error;
    if (!app::make_percent_edit(doc_.plan(), selected.uid, percent, edit, error)) {
        set_message("percent complete: " + error, true);
        update_status();
        return;
    }
    // Clicking the value the task already carries asks for no file change; the
    // measured writer has no pair for an identical save, so do not queue one.
    for (const app::TaskRow& task : doc_.plan().tasks) {
        if (task.uid == selected.uid && task.percent_complete == percent) {
            set_message("task uid " + std::to_string(selected.uid) + " is already " +
                            std::to_string(percent) + "% complete",
                        false);
            update_status();
            return;
        }
    }
    if (!structural_.push(std::move(edit), &doc_.plan())) {
        set_message("percent complete: the edit was refused", true);
        update_status();
        return;
    }
    after_structural_change(selected.uid);
    set_message("queued " + std::to_string(percent) + "% complete for task uid " +
                    std::to_string(selected.uid) + "; File > Save writes it",
                false);
    update_status();
}

void MainWindow::indent_selected(bool indent) {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
        doc_.sheet() == nullptr) {
        set_message(std::string(indent ? "indent" : "outdent") +
                        " is available in the Task Sheet view",
                    false);
        update_status();
        return;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    if (!selected.has_selection || selected.uid == 0) {
        set_message("select a task first", true);
        update_status();
        return;
    }
    // W28-2.1: the file-side rules live in make_outline_edit -- a saved row,
    // the measured parent/sibling shape, the marker words and the calendar
    // arithmetic.  The shell only supplies the displayed plan and the path the
    // edit must agree with.
    // W29-0.0: the actuals edits already queued are named too, because an
    // indent under a row whose actuals are queued in the same save is a shape
    // no Project pair measured.
    std::vector<std::uint32_t> pending_actuals;
    for (const app::StructuralEdit& pending : structural_.applied()) {
        if (pending.kind == app::StructuralKind::set_percent) {
            pending_actuals.push_back(pending.uid);
        } else if (pending.kind == app::StructuralKind::update_actuals) {
            for (const app::ActualsTarget& target : pending.actuals) {
                pending_actuals.push_back(target.uid);
            }
        }
    }
    app::StructuralEdit edit;
    std::string error;
    if (!app::make_outline_edit(doc_.plan(), doc_.path(), selected.uid, indent,
                                edit, error, pending_actuals)) {
        set_message(std::string(indent ? "indent: " : "outdent: ") + error, true);
        update_status();
        return;
    }
    if (!structural_.push(std::move(edit), &doc_.plan())) {
        set_message(std::string(indent ? "indent" : "outdent") +
                        ": the edit was refused",
                    true);
        update_status();
        return;
    }
    // The RecordSet is rebuilt, so after_structural_change() detaches the Qt
    // adapter first; the new outline level and number are on the sheet and the
    // Gantt's summary bar on the next rebuild (W28-2.2).
    after_structural_change(selected.uid);
    set_message("queued " + std::string(indent ? "indent" : "outdent") +
                    " for task uid " + std::to_string(selected.uid) +
                    "; File > Save writes it",
                false);
    update_status();
}

void MainWindow::manually_schedule_selected() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
        doc_.sheet() == nullptr) {
        set_message("manually schedule is available in the Task Sheet view", false);
        update_status();
        return;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    if (!selected.has_selection || selected.uid == 0) {
        set_message("select a task first", true);
        update_status();
        return;
    }
    // W30-1.0: the file-side shape (the stored precondition bytes and the
    // whole-file conditions) lives in make_task_mode_edit; the shell supplies
    // the displayed plan and the path the edit must agree with.  Auto Schedule
    // never reaches here: its button is disabled and the Mode menu's automatic
    // entry is disabled, and the edit model would refuse it anyway.
    app::StructuralEdit edit;
    std::string error;
    if (!app::make_task_mode_edit(doc_.plan(), doc_.path(), selected.uid, true, edit,
                                  error)) {
        set_message("manually schedule: " + error, true);
        update_status();
        return;
    }
    if (!structural_.push(std::move(edit), &doc_.plan())) {
        set_message("manually schedule: the edit was refused", true);
        update_status();
        return;
    }
    after_structural_change(selected.uid);
    set_message("queued manually scheduled for task uid " +
                    std::to_string(selected.uid) + "; File > Save writes it",
                false);
    update_status();
}

void MainWindow::show_task_mode_menu() {
    QMenu menu(this);
    QAction* automatic = menu.addAction(QStringLiteral("Automatically Scheduled"));
    // The reverse save pair shows Project also reschedules the row, and this
    // port has no scheduler; the direction stays disabled with decision 9's
    // exact tooltip.
    automatic->setEnabled(false);
    automatic->setToolTip(QStringLiteral("not implemented in this port"));
    QAction* manual = menu.addAction(QStringLiteral("Manually Scheduled"));
    connect(manual, &QAction::triggered, this,
            [this]() { manually_schedule_selected(); });
    menu.exec(QCursor::pos());
}

void MainWindow::open_dialog(const QString& start_dir) {
    if (!confirm_proceed_with_discard()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Project"), start_dir,
        QStringLiteral("MS Project files (*.mpp);;All files (*)"), nullptr,
        QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!open_file(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Open Project"),
                             QStringLiteral("Could not open the file:\n") + error);
    }
}

void MainWindow::show_about() {
    QMessageBox::about(
        this, QStringLiteral("About MS Project Shell"),
        QStringLiteral("MS Project Shell ") + QString::fromLatin1(kShellVersion) +
            QStringLiteral("\n\nA personal three-platform port of MS Project.\n"
                           "Qt 6 Widgets over the portable app/ models.\n\n"
                           "UI font: ") +
            ui_font_family() +
            (ui_font_is_carlito() ? QStringLiteral(" (bundled metric-compatible)")
                                  : QStringLiteral(" (fallback; Carlito not installed)")) +
            QStringLiteral("\n\nOpened file: ") +
            to_q(doc_.path().empty() ? std::string("(none)") : doc_.path()));
}

void MainWindow::show_shortcuts() {
    QMessageBox::information(this, QStringLiteral("Keyboard Shortcuts"), shortcuts_text());
}

void MainWindow::set_zoom(app::ZoomLevel zoom) {
    if (prefs_.zoom == zoom &&
        !(doc_.has_document() && doc_.session_timescale_active())) {
        // Nothing to change -- unless a Zoom-command fit (wave 31, W31-2.1) is
        // active: the shell's own zoom levels are a real choice even when the
        // preference already names them, so the fit yields to them below.
        update_zoom_slider();
        return;
    }
    prefs_.zoom = zoom;
    if (doc_.has_document()) {
        // A user zoom choice replaces the file's stored timescale zoom (W29-1.4)
        // and any Zoom-command session fit (W31-2.1).
        doc_.rebuild_views(prefs_.zoom, true);
        if (prefs_.view == app::ViewMode::task_sheet) {
            gantt_canvas_->set_layout(doc_.gantt());
            gantt_canvas_->set_selected_uid(doc_.selected_uid());
        }
    } else {
        // An empty project still re-centres its axis on the current week at the
        // new zoom; without this the header would keep the old unit.
        doc_.build_empty_view(prefs_.zoom, app::today_date32());
        gantt_canvas_->set_layout(doc_.gantt());
        refresh_timeline();
    }
    update_zoom_slider();
    set_message(std::string("timescale ") + app::zoom_level_name(prefs_.zoom), false);
    update_status();
}

void MainWindow::after_model_change() {
    if (!doc_.has_document()) {
        return;
    }
    doc_.rebuild_views(prefs_.zoom);
    sheet_model_.refresh();
    apply_filter_to_sheet_model();
    sync_view_selection();
    if (prefs_.view == app::ViewMode::task_sheet) {
        gantt_canvas_->set_layout(doc_.gantt());
        gantt_canvas_->set_selected_uid(doc_.selected_uid());
    }
    update_window_title();
    update_menu_state();
    update_status();
}

void MainWindow::after_cell_commit(bool ok, const std::string& message) {
    if (!ok) {
        set_message(message.empty() ? "the edit was rejected" : message, true);
        update_status();
        return;
    }
    // A cell edit ends the structural redo list: the two stacks stay
    // chronological (app/edit_model.h).
    structural_.clear_redo();
    after_cell_change();
    set_message("cell edit applied; File > Save writes task starts", false);
    update_status();
}

void MainWindow::after_cell_change() {
    if (!doc_.has_document()) {
        return;
    }
    doc_.rebuild_views(prefs_.zoom);
    if (prefs_.view == app::ViewMode::task_sheet) {
        gantt_canvas_->set_layout(doc_.gantt());
        gantt_canvas_->set_selected_uid(doc_.selected_uid());
    }
    refresh_timeline();
    // W26-6.3: an edited start/finish/percent is visible in the Task Form at
    // once.
    refresh_task_form();
    // W30-1.10: and the inspector's facts (dates/duration/percent) follow the
    // committed cell, not the RecordSet's uncommitted text.
    refresh_inspector();
    update_window_title();
    update_menu_state();
    update_status();
}

void MainWindow::after_structural_change(std::uint32_t keep_uid) {
    if (!doc_.has_document()) {
        return;
    }
    // W28-3.1: undo/redo/insert all land here; the shown new-task mode is the
    // stack's last mode edit (or the loaded file's value when there is none).
    refresh_new_task_mode();
    // Detach the Qt adapter BEFORE the rebuild: apply_structural_edits replaces
    // PlanDocument::sheet_, and the QAbstractItemModel must not keep pointing at
    // the destroyed SheetModel while the view handles a model reset.
    sheet_model_.set_sheet(nullptr);
    doc_.apply_structural_edits(structural_, prefs_.zoom, keep_uid);
    rebind_sheet();
    sync_view_selection();
    gantt_canvas_->set_layout(doc_.gantt());
    gantt_canvas_->set_selected_uid(doc_.selected_uid());
    refresh_timeline();
    update_window_title();
    update_menu_state();
    update_status();
}

// ---------------------------------------------------------------------------
// Wave 19: save
// ---------------------------------------------------------------------------

bool MainWindow::save_document_path(const QString& target, QString* error) {
    last_save_warnings_.clear();
    last_save_rows_written_ = 0;
    auto fail = [&](const std::string& text) {
        if (error != nullptr) {
            *error = to_q(text);
        }
        set_message(text, true);
        update_status();
        return false;
    };
    if (!doc_.has_document()) {
        return fail("no project to save");
    }
    if (doc_.sheet() != nullptr && doc_.sheet()->editing()) {
        doc_.sheet()->cancel_edit();
    }
    const std::vector<app::StartEdit> edits =
        app::collect_start_edits(doc_.records(), doc_.loaded());
    const bool structural = !structural_.empty();
    const bool document_edits = document_ != nullptr && document_->has_pending_edits();
    if (edits.empty() && !structural && !document_edits) {
        set_message("nothing to save: no task start, insert/delete, custom-field or link edit "
                    "is pending",
                    false);
        update_status();
        return true;
    }
    const std::string target_path = target.toStdString();
    if (target_path.empty()) {
        return fail("no target path");
    }
    if (!document_) {
        document_ = std::make_unique<app::Document>(doc_.path());
    }
    if (structural) {
        // ONE write for the whole cascade (link deletes/adds, task deletes/
        // inserts, custom fields, then start dates), through the shared model.
        const app::StructuralSaveResult result =
            document_->save_structural(structural_, edits, target_path, owns_path_);
        if (!result.ok) {
            return fail(result.error.empty() ? "the save failed" : result.error);
        }
        last_save_rows_written_ = result.start_rows_written;
        last_save_warnings_ = result.warnings;
        std::string summary = "saved " + std::to_string(result.added) + " new task(s), " +
                              std::to_string(result.deleted) + " deleted task(s)";
        if (result.links_added != 0 || result.links_deleted != 0) {
            summary += ", " + std::to_string(result.links_added) + " link(s) added and " +
                       std::to_string(result.links_deleted) + " deleted";
        }
        if (result.fields_written != 0) {
            summary += ", " + std::to_string(result.fields_written) +
                       " custom-field value(s) written";
        }
        if (result.start_rows_written != 0) {
            summary += ", " + std::to_string(result.start_rows_written) + " start(s)";
        }
        summary += " to " + result.path;

        // The added rows' real uids and ids are the file's: reload and adopt it.
        QString reload_error;
        if (!open_file(to_q(target_path), &reload_error)) {
            set_message(summary + " | the saved file could not be re-read: " +
                            reload_error.toStdString(),
                        true);
            update_status();
            return true;  // the file WAS written; the reload is what failed
        }
        owns_path_ = true;
        last_save_target_ = target_path;
        update_window_title();
        set_message(summary + " | reloaded the file", false);
        update_menu_state();
        update_status();
        return true;
    }

    app::SaveOptions options;
    // Saving over a file THIS APP wrote is allowed (a structural save reloads the
    // target and adopts it); the file the user opened is never overwritten.
    options.allow_source_overwrite = owns_path_;
    const app::SaveOutcome outcome = document_->save(edits, target_path, options);
    if (!outcome.ok) {
        return fail(outcome.error.empty() ? "the save failed" : outcome.error);
    }
    doc_.accept_start_edits(edits);
    document_->discard_view_state_edits();
    document_->discard_custom_formula();
    last_save_target_ = outcome.path;
    last_save_rows_written_ = outcome.rows_written;
    last_save_warnings_ = outcome.warnings;
    note_recent_file(to_q(outcome.path));

    std::string message = "saved " + std::to_string(outcome.rows_written) + " start(s) to " +
                          outcome.path + " (" + outcome.api + ")";
    if (!outcome.warnings.empty()) {
        message += ", " + std::to_string(outcome.warnings.size()) + " warning(s)";
    }
    if (!outcome.notes.empty()) {
        message += ", " + std::to_string(outcome.notes.size()) + " writer note(s)";
    }
    for (const std::string& note : outcome.notes) {
        std::printf("msqt: save note: %s\n", note.c_str());
    }
    for (const std::string& warning : outcome.warnings) {
        std::fprintf(stderr, "msqt: save warning: %s\n", warning.c_str());
    }
    update_window_title();
    update_menu_state();
    set_message(message, false);
    update_status();
    return true;
}

bool MainWindow::save_document(bool force_panel, const QString& start_dir) {
    if (!doc_.has_document()) {
        return false;
    }
    if (doc_.sheet() != nullptr && doc_.sheet()->editing()) {
        doc_.sheet()->cancel_edit();
    }
    const std::vector<app::StartEdit> edits =
        app::collect_start_edits(doc_.records(), doc_.loaded());

    // Warn BEFORE the write when a start edit cannot take effect (a linked pin
    // behind the predecessor's finish, or a manual task the writer converts to
    // an auto-scheduled SNET pin).  Once per save, as app/main.mm does.
    if (!edits.empty()) {
        const app::StartEditWarningReport warning =
            app::warn_start_edits(edits, doc_.plan(), doc_.format());
        if (warning.warnings != 0) {
            const QMessageBox::StandardButton choice = QMessageBox::warning(
                this, QStringLiteral("Some start edits may not take effect"),
                to_q(warning.text) +
                    QStringLiteral("\n\nThe writer still stores the exact dates, but Project "
                                   "may reopen a linked task at a later date or reschedule a "
                                   "manual task.\n\nSave anyway?"),
                QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
            if (choice != QMessageBox::Save) {
                set_message("save cancelled at the start-edit warning", false);
                update_status();
                return false;
            }
        }
    }

    if (edits.empty() && structural_.empty() &&
        (!document_ || !document_->has_pending_edits())) {
        set_message("nothing to save: no task start, insert/delete, custom-field or link edit "
                    "is pending",
                    false);
        update_status();
        return true;
    }

    std::string target = last_save_target_;
    if (force_panel || target.empty()) {
        QString directory;
        if (!start_dir.isEmpty()) {
            directory = start_dir;
        } else if (!doc_.path().empty()) {
            directory = QFileInfo(to_q(doc_.path())).absolutePath();
        }
        const QString suggested = to_q(app::default_save_name(doc_.path()));
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Project As"),
            directory.isEmpty() ? suggested : directory + QStringLiteral("/") + suggested,
            QStringLiteral("MS Project files (*.mpp);;All files (*)"), nullptr,
            QFileDialog::DontUseNativeDialog);
        if (path.isEmpty()) {
            set_message("save cancelled", false);
            update_status();
            return false;
        }
        target = path.toStdString();
    }

    QString error;
    if (!save_document_path(to_q(target), &error)) {
        QMessageBox::warning(
            this, QStringLiteral("Could not save the pending edits"),
            error + QStringLiteral("\n\nThe opened file was not modified."));
        return false;
    }
    if (!last_save_warnings_.empty()) {
        QString details;
        for (const std::string& warning : last_save_warnings_) {
            details += to_q(warning) + QStringLiteral("\n");
        }
        QMessageBox::warning(
            this, QStringLiteral("Saved with warnings"),
            QStringLiteral("Wrote ") + QString::number(last_save_rows_written_) +
                QStringLiteral(" task start(s) to:\n") + to_q(target) +
                QStringLiteral("\n\n") + details);
    }
    return true;
}

bool MainWindow::confirm_proceed_with_discard() {
    if (!document_dirty()) {
        return true;
    }
    const std::size_t starts = pending_start_edits();
    const std::size_t structural = structural_.size();
    const QString text =
        QStringLiteral(
            "This project has edits that Save would write: %1 task start(s) and %2 "
            "structural/custom-field edit(s).\n\nThe other edits are view-only in memory.")
            .arg(static_cast<qulonglong>(starts))
            .arg(static_cast<qulonglong>(structural));
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this, QStringLiteral("Unsaved changes"),
        text + QStringLiteral("\n\nSave them before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Save) {
        return save_document(false);
    }
    return choice == QMessageBox::Discard;
}

// ---------------------------------------------------------------------------
// Wave 19: print and export
// ---------------------------------------------------------------------------

app::PrintJob MainWindow::current_print_job() const {
    return current_print_job(app::PageSpec::a4_landscape());
}

app::PrintJob MainWindow::current_print_job(const app::PageSpec& spec) const {
    if (!doc_.has_document()) {
        return {};
    }
    const std::vector<std::size_t> rows =
        print_rows(doc_.records().row_count(),
                   doc_.filter_active() ? doc_.filter_rows() : std::vector<std::size_t>{},
                   doc_.filter_active());
    std::size_t printed_tasks = 0;
    for (const std::size_t row : rows) {
        if (doc_.records().uid(row) != 0) {
            ++printed_tasks;
        }
    }
    return make_print_job(doc_.plan(), rows, printed_tasks,
                          doc_.filter_active() ? doc_.filter_description() : std::string(),
                          today_text(), prefs_.zoom, spec);
}

// W29-3.1: the Page Setup record on the Print page's live paper/orientation.
app::PageSpec MainWindow::print_spec(bool landscape, bool letter) const {
    app::PageSpec spec = print_spec_;
    const app::PageSpec paper =
        letter ? app::PageSpec::letter_landscape() : app::PageSpec::a4_landscape();
    spec.width = landscape ? paper.width : paper.height;
    spec.height = landscape ? paper.height : paper.width;
    return spec;
}

// The Page Setup dialog's OK path, without the modal (the gate calls this).
// The dialog also owns orientation and paper, so the two live Print-page
// controls follow it; the preview re-renders once through set_print_settings().
void MainWindow::apply_page_setup(const app::PageSpec& spec) {
    print_spec_ = spec;
    if (backstage_ == nullptr) {
        return;
    }
    const bool landscape = spec.width > spec.height;
    const double long_side = std::max(spec.width, spec.height);
    const bool letter = std::fabs(long_side - 792.0) < 1.0;
    backstage_->set_print_settings(landscape, letter);
}

void MainWindow::show_page_setup() {
    PageSetupDialog dialog(print_spec_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    apply_page_setup(dialog.spec());
    set_message("page setup applied to the print layout", false);
    update_status();
    if (dialog.preview_requested()) {
        // The backstage Print page's preview already re-rendered with the new
        // spec; open it.
        if (backstage_ != nullptr) {
            show_backstage(true);
            backstage_->show_page(Backstage::Page::print);
        }
    } else if (dialog.print_requested()) {
        print_plan();
    }
}

void MainWindow::print_plan() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("printing is available in the Task Sheet view", true);
        update_status();
        return;
    }
    // W26-4.5: the backstage Print page's live settings choose the paper,
    // orientation, copies and printer; the page geometry stays the shared
    // PageSpec.
    bool landscape = true;
    bool letter = false;
    int copies = 1;
    QString printer_name;
    if (backstage_ != nullptr) {
        landscape = backstage_->print_landscape();
        letter = backstage_->print_paper_letter();
        copies = backstage_->print_copies();
        printer_name = backstage_->print_printer_name();
    }
    app::PageSpec spec = print_spec(landscape, letter);
    const app::PrintJob job = current_print_job(spec);
    QString error;
    if (!print_job(this, job, *doc_.sheet(), doc_.plan(), prefs_, &error, copies,
                   printer_name)) {
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Could not print"), error);
            set_message("print failed: " + error.toStdString(), true);
        } else {
            set_message("print cancelled", false);
        }
        update_status();
        return;
    }
    set_message("printed " + std::to_string(job.pages.size()) + " page(s)", false);
    update_status();
}

void MainWindow::export_pdf(app::ReportKind kind) {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("PDF export is available in the Task Sheet view", true);
        update_status();
        return;
    }
    QString directory;
    if (!doc_.path().empty()) {
        directory = QFileInfo(to_q(doc_.path())).absolutePath();
    }
    const QString suggested = to_q(app::export_default_name(doc_.path(), kind));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export PDF"),
        directory.isEmpty() ? suggested : directory + QStringLiteral("/") + suggested,
        QStringLiteral("PDF files (*.pdf);;All files (*)"), nullptr,
        QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) {
        set_message("export cancelled", false);
        update_status();
        return;
    }

    app::ExportRequest request;
    request.kind = kind;
    request.records = &doc_.records();
    if (doc_.filter_active()) {
        request.rows = doc_.filter_rows();
        request.rows_are_filtered = true;
    }
    for (const std::size_t column : doc_.sheet()->visible_columns()) {
        if (column < doc_.sheet()->column_count()) {
            request.columns.push_back(doc_.sheet()->column(column));
        }
    }
    request.format = doc_.format();
    request.metrics = app::default_sheet_metrics();
    request.preferences = prefs_;
    request.title = doc_.loaded().title.empty() ? "Project report" : doc_.loaded().title;
    // Wave 25 (W25-11/S14): the project-summary row (uid 0) is a row, not a
    // task; Print already says "N of N", so the PDF subtitle must not count it.
    const std::size_t total = app::task_row_count(doc_.records());
    const std::size_t shown = doc_.filter_active()
                                  ? app::task_row_count(doc_.records(), doc_.filter_rows())
                                  : total;
    request.subtitle =
        app::export_subtitle(kind, doc_.filter_active() ? doc_.filter_description() : "",
                             shown, total);
    request.footer = app::export_footer(prefs_, std::string());
    request.page = app::PageSpec::a4_landscape();
    for (const app::LinkRow& row : doc_.plan().links) {
        core::gantt::LinkInput link;
        link.predecessor_uid = row.predecessor_uid;
        link.successor_uid = row.successor_uid;
        link.type = row.type;
        link.critical = row.critical;
        request.links.push_back(link);
    }
    request.timescale = app::timescale_for_zoom(doc_.plan(), app::gantt_zoom(prefs_.zoom));
    request.auto_timescale = true;
    request.working_day_shading = prefs_.working_day_shading;

    const app::ExportOutcome outcome = app::export_report(request, path.toStdString());
    if (!outcome.ok) {
        QMessageBox::warning(this, QStringLiteral("Could not export the PDF"),
                             to_q(outcome.error));
        set_message("export failed: " + outcome.error, true);
        update_status();
        return;
    }
    set_message("exported " + std::to_string(outcome.rows) + " row(s), " +
                    std::to_string(outcome.pages) + " page(s) to " + outcome.path,
                false);
    update_status();
}

void MainWindow::save_as_pdf() {
    // Save as PDF writes the SHARED renderer's bytes: the same app::export_report
    // call (core/render, the same PageSpec) as File > Export PDF (Gantt), so the
    // PDF is byte-identical to the export on every platform.  File > Print
    // remains the QPrinter route on the same page geometry.
    export_pdf(app::ReportKind::gantt);
}

// ---------------------------------------------------------------------------
// Wave 19: structural edits and dialogs
// ---------------------------------------------------------------------------

void MainWindow::show_task_fields() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("Task Fields is available in the Task Sheet view", false);
        update_status();
        return;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    // Wave 24 (W24-1): resolve the selection to its UID and look the task up by
    // UID.  A RecordSet row is only the same number as a plan task index when
    // the project row is hidden; with row 0 shown it is off by one, which used
    // to open this dialog (and queue the save) on the task BELOW the selection.
    const app::TaskRow* selected_task = doc_.task_by_uid(selected.uid);
    if (!selected.has_selection) {
        set_message("select a task first", false);
        update_status();
        return;
    }
    if (selected_task == nullptr) {
        set_message("the project summary row has no editable task fields", false);
        update_status();
        return;
    }
    std::vector<app::StructuralEdit> edits;
    QString error;
    if (!edit_task_fields(this, *selected_task, doc_.format(), edits, &error)) {
        if (!error.isEmpty()) {
            set_message(error.toStdString(), true);
            update_status();
        }
        return;
    }
    if (edits.empty()) {
        set_message("no custom-field value changed", false);
        update_status();
        return;
    }
    for (const app::StructuralEdit& edit : edits) {
        structural_.push(edit);
    }
    after_structural_change();
    set_message("queued " + std::to_string(edits.size()) +
                    " custom-field edit(s); File > Save writes them",
                false);
    update_status();
}

void MainWindow::show_custom_fields() {
    if (!doc_.has_document()) {
        set_message("open a project first", false);
        update_status();
        return;
    }
    // The Task fields whose storage the port knows, with the file's stored
    // formulas attached; Project's Resource/Project scopes are laid out and
    // disabled in the dialog.
    std::vector<app::CustomFieldDefinition> fields = app::custom_field_definitions();
    app::attach_custom_field_formulas(fields, app::props_stream_from_file(doc_.path()));

    CustomFieldsDialog dialog(std::move(fields), this);
    dialog.set_formula_editor([this](std::uint32_t field_id,
                                     const QString& current) -> QString {
        bool accepted = false;
        const QString text = QInputDialog::getText(
            this, QStringLiteral("Custom Field Formula"),
            QStringLiteral("Formula for field 0x%1 (Project syntax, e.g. "
                           "[Duration]/240):")
                .arg(static_cast<qulonglong>(field_id), 8, 16, QChar('0')),
            QLineEdit::Normal, current, &accepted);
        if (!accepted) {
            return QString();  // null = cancelled, nothing queued
        }
        return text;
    });
    if (dialog.exec() != QDialog::Accepted) {
        set_message("custom fields cancelled", false);
        update_status();
        return;
    }
    if (!dialog.has_formula_edit()) {
        set_message("no custom-field definition changed", false);
        update_status();
        return;
    }
    if (!document_) {
        document_ = std::make_unique<app::Document>(doc_.path());
    }
    std::string error;
    if (!document_->apply_custom_formula(dialog.formula_edit().field_id,
                                         dialog.formula_edit().formula, &error)) {
        QMessageBox::warning(this, QStringLiteral("Could not set the formula"),
                             to_q(error));
        set_message("the custom-field formula was refused: " + error, true);
        update_status();
        return;
    }
    update_window_title();
    update_menu_state();
    set_message("queued the formula for field 0x" +
                    QString::number(dialog.formula_edit().field_id, 16).toStdString() +
                    "; File > Save writes it",
                false);
    update_status();
}

void MainWindow::show_predecessors() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("Predecessors is available in the Task Sheet view", false);
        update_status();
        return;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    if (!selected.has_selection || selected.uid == 0) {
        set_message("select a task first", false);
        update_status();
        return;
    }
    if (structural_.is_pending_insert(selected.uid)) {
        set_message("a pending inserted task has no file row yet; save it before linking", false);
        update_status();
        return;
    }
    app::StructuralEdits updated;
    QString error;
    if (!edit_predecessors(this, doc_.loaded(), structural_, selected.uid, updated, &error)) {
        if (!error.isEmpty()) {
            set_message(error.toStdString(), true);
            update_status();
        }
        return;
    }
    structural_ = std::move(updated);
    after_structural_change();
    set_message("queued the predecessor change(s); File > Save writes them", false);
    update_status();
}

// ---------------------------------------------------------------------------
// Wave 20: preferences, the two document-edit dialogs, and the clipboard
// ---------------------------------------------------------------------------

bool MainWindow::apply_preferences(const app::Preferences& preferences, bool persist) {
    const app::ZoomLevel previous_zoom = prefs_.zoom;
    prefs_ = preferences;
    if (doc_.has_document()) {
        // The date format is the sheet's, so every date cell, the status line and
        // the PDF report follow this one change (app::apply_preferences).
        doc_.apply_preferences(prefs_);
        // A zoom preference that actually changed is the shell's zoom level
        // taking over, exactly like the slider (wave 31, W31-2.1): it clears a
        // Zoom-command session fit.  Any other preference keeps the fit.
        if (prefs_.zoom != previous_zoom) {
            doc_.clear_session_timescale();
        }
        doc_.rebuild_views(prefs_.zoom);
        if (prefs_.view == app::ViewMode::task_sheet) {
            gantt_canvas_->set_layout(doc_.gantt());
            gantt_canvas_->set_selected_uid(doc_.selected_uid());
        }
    }
    gantt_canvas_->set_shading(prefs_.working_day_shading);
    // The duration-style preference is read by the table model on every cell, so
    // a reset redraws with it; the date style is already in the sheet format.
    sheet_model_.refresh();
    update_zoom_slider();
    update_window_title();
    if (persist) {
        if (preferences_store_ != nullptr) {
            preferences_store_->save(prefs_);
            set_message(app::preferences_summary(prefs_) + " | saved", false);
        } else {
            // No store is installed only in the offscreen tests, which must not
            // write the host's real preferences.
            set_message(app::preferences_summary(prefs_) +
                            " | applied (no preference store installed)",
                        false);
        }
    } else {
        set_message(app::preferences_summary(prefs_), false);
    }
    update_status();
    return true;
}

QStringList MainWindow::recent_files() const {
    return preferences_store_ != nullptr ? preferences_store_->load_recent_files()
                                         : QStringList();
}

void MainWindow::note_recent_file(const QString& path) {
    if (preferences_store_ != nullptr && !path.isEmpty()) {
        preferences_store_->add_recent_file(path);
    }
}

void MainWindow::set_theme(Theme theme, bool persist) {
    theme_ = theme;
    // A programmatic palette change keeps the chooser honest; a choice that
    // resolves to the same palette (Use system setting -> light) stays itself.
    if (theme_for_choice(theme_choice_) != theme) {
        theme_choice_ = theme_choice_for_theme(theme);
    }
    apply_theme(theme_);
    // W26-6.2: the shell's custom-painted chrome does not follow QPalette, so
    // repaint it from the same theme (live, no restart).
    if (ribbon_ != nullptr) {
        ribbon_->set_theme(theme_);
    }
    if (timeline_ != nullptr) {
        timeline_->set_theme(theme_);
    }
    // W29-5.1: the sheet's caption band/frozen gutter and the chart's timescale
    // band are chrome too.  Their paint code reads the same chrome palette; the
    // sheet cells and the chart body are deliberately not touched.
    if (auto* sheet = dynamic_cast<SheetView*>(sheet_view_); sheet != nullptr) {
        sheet->set_theme(theme_);
    }
    if (gantt_canvas_ != nullptr) {
        gantt_canvas_->set_theme(theme_);
    }
    // W29-5.2: menu/QAT/status artwork follows the same theme ink.
    apply_icon_ink();
    apply_chrome_theme();
    update();
    if (persist && preferences_store_ != nullptr) {
        preferences_store_->save_theme(theme_);
    }
    set_message(std::string("Office theme: ") + theme_name(theme_), false);
    update_status();
}

void MainWindow::apply_icon_ink() {
    // W29-5.2: the menu actions, the quick-access toolbar (which reuses those
    // same QActions) and the status-bar view buttons are chrome; their one-ink
    // artwork is redrawn in the live WINDOW ink, because those surfaces are the
    // window/status background rather than the ribbon's light group row.
    // Container QActions carry no data and no glyph.
    const QColor ink = chrome_palette(theme_).window_ink;
    for (QAction* action : actions_) {
        if (action == nullptr || !action->data().isValid()) {
            continue;
        }
        const auto id = static_cast<app::MenuAction>(action->data().toInt());
        if (id == app::MenuAction::none) {
            continue;
        }
        action->setIcon(shell_action_icon(id, kShellIconPx, ink));
    }
    for (std::size_t index = 0; index < view_buttons_.size(); ++index) {
        QToolButton* button = view_buttons_[index];
        if (button != nullptr) {
            button->setIcon(shell_view_icon(static_cast<int>(index), kShellIconPx, ink));
        }
    }
}

void MainWindow::apply_chrome_theme() {
    const ChromePalette& chrome = chrome_palette(theme_);
    if (quick_access_ != nullptr) {
        quick_access_->setStyleSheet(QStringLiteral(
            "QToolBar { background: %1; border-bottom: 1px solid %2;"
            " spacing: 2px; padding: 1px 6px; }")
                                         .arg(chrome.quick_access_background.name(),
                                              chrome.ribbon_border.name()));
    }
    if (statusBar() != nullptr) {
        statusBar()->setStyleSheet(
            QStringLiteral("QStatusBar { background: %1; border-top: 1px solid %2; }"
                           "QStatusBar QLabel { color: %3; }")
                .arg(chrome.status_background.name(), chrome.ribbon_border.name(),
                     chrome.status_text.name()));
    }
}

bool MainWindow::set_theme_choice(ThemeChoice choice, bool persist) {
    if (!theme_choice_implemented(choice)) {
        return false;
    }
    theme_choice_ = choice;
    set_theme(theme_for_choice(choice), persist);
    if (backstage_ != nullptr) {
        backstage_->set_theme_choice(theme_choice_);
    }
    return true;
}

void MainWindow::show_preferences() {
    PreferencesDialog dialog(prefs_, theme_, this);
    if (dialog.exec() != QDialog::Accepted) {
        set_message("preferences cancelled", false);
        update_status();
        return;
    }
    apply_preferences(dialog.preferences(), true);
    set_theme(dialog.theme(), true);
}

void MainWindow::show_project_information() {
    if (!doc_.has_document()) {
        set_message("open a project first", false);
        update_status();
        return;
    }
    const app::CalendarNames names = app::read_calendar_names(doc_.path());
    const app::ProjectInformation info = app::project_information_from(
        doc_.plan(), names.names, names.default_name, app::today_date32(), doc_.format());
    int priority = info.priority;
    if (!edit_project_information(this, info, &priority)) {
        set_message("project information cancelled", false);
        update_status();
        return;
    }
    if (priority == info.priority) {
        set_message("no project information changed", false);
        update_status();
        return;
    }
    if (!app::valid_project_priority(priority) ||
        !structural_.push(app::make_project_priority_edit(priority))) {
        set_message("the project priority was refused", true);
        update_status();
        return;
    }
    after_structural_change();
    set_message("queued project priority " + std::to_string(priority) +
                    "; File > Save writes it",
                false);
    update_status();
}

void MainWindow::show_links_between_projects() {
    if (!doc_.has_document()) {
        set_message("open a project first", false);
        update_status();
        return;
    }
    LinksBetweenProjectsDialog dialog(doc_.path(), this);
    dialog.exec();
    set_message("links between projects: this port decodes no external project links, "
                "nothing was changed",
                false);
    update_status();
}

void MainWindow::show_update_project() {
    if (!doc_.has_document()) {
        set_message("open a project first", false);
        update_status();
        return;
    }
    const app::PlanInput& plan = doc_.plan();
    // The writer patches the file's stored status date; a file without the
    // record has no measured shape, so refuse before the dialog rather than
    // letting the save throw later.
    if (!plan.status_date_stored) {
        set_message("update project: this file has no project status-date record, which "
                    "this writer does not measure; nothing was changed",
                    true);
        update_status();
        return;
    }
    UpdateProjectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        set_message("update project cancelled", false);
        update_status();
        return;
    }
    if (!dialog.progress_radio()->isChecked()) {
        // The other choices are disabled in the dialog; this is defensive.
        set_message("update project: only 0% - 100% complete is implemented; nothing was "
                    "changed",
                    true);
        update_status();
        return;
    }
    std::vector<std::uint32_t> uids;
    std::uint32_t focus_uid = 0;
    if (dialog.selected_radio()->isChecked()) {
        const app::SelectedTask selected =
            doc_.sheet() != nullptr
                ? app::selected_task(*doc_.sheet(), doc_.rows())
                : app::SelectedTask{};
        if (!selected.has_selection || selected.uid == 0) {
            set_message("update project: select a task first", true);
            update_status();
            return;
        }
        uids.push_back(selected.uid);
        focus_uid = selected.uid;
    } else {
        // Entire project: every saved live leaf row.  A pending delete's row is
        // still in doc_.plan(), but push() refuses an update naming it; a
        // pending insert has no saved row.
        for (const app::TaskRow& task : plan.tasks) {
            if (task.uid == 0 || task.summary || task.file_row == 0) {
                continue;
            }
            bool deleted = false;
            for (const app::StructuralEdit& pending : structural_.applied()) {
                if (pending.kind == app::StructuralKind::delete_task &&
                    pending.uid == task.uid) {
                    deleted = true;
                    break;
                }
            }
            if (!deleted) {
                uids.push_back(task.uid);
            }
        }
        if (uids.empty()) {
            set_message("update project: the project has no saved leaf tasks to update",
                        true);
            update_status();
            return;
        }
    }
    const QDate through = dialog.through_date()->date();
    const app::date32 through_date =
        core::sched::from_ymd(through.year(), through.month(), through.day());
    app::StructuralEdit edit;
    std::string error;
    if (!app::make_update_actuals_edit(plan, doc_.path(), uids, through_date, true,
                                       edit, error)) {
        set_message("update project: " + error, true);
        update_status();
        return;
    }
    if (!structural_.push(std::move(edit), &plan)) {
        set_message("update project: the edit was refused", true);
        update_status();
        return;
    }
    after_structural_change(focus_uid);
    set_message("queued the update of " + std::to_string(uids.size()) +
                    " task(s) through " +
                    through.toString(QStringLiteral("yyyy-MM-dd")).toStdString() +
                    "; File > Save writes it",
                false);
    update_status();
}

void MainWindow::mark_on_track() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
        doc_.sheet() == nullptr) {
        set_message("mark on track is available in the Task Sheet view", false);
        update_status();
        return;
    }
    const app::PlanInput& plan = doc_.plan();
    if (!plan.status_date_stored || plan.status_date == app::kDateNa) {
        set_message("mark on track: this file stores no project status date, so there is "
                    "no date to mark work complete through; nothing was changed",
                    true);
        update_status();
        return;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    if (!selected.has_selection || selected.uid == 0) {
        set_message("select a task first", true);
        update_status();
        return;
    }
    // W28-1.3: the measured Mark on Track pair edits one selected row through
    // the file's stored status date and leaves the status date itself alone.
    app::StructuralEdit edit;
    std::string error;
    const std::vector<std::uint32_t> uids = {selected.uid};
    if (!app::make_update_actuals_edit(plan, doc_.path(), uids, plan.status_date,
                                       false, edit, error)) {
        set_message("mark on track: " + error, true);
        update_status();
        return;
    }
    const std::uint16_t percent =
        edit.actuals.empty() ? 0 : edit.actuals.front().percent;
    if (!structural_.push(std::move(edit), &plan)) {
        set_message("mark on track: the edit was refused", true);
        update_status();
        return;
    }
    after_structural_change(selected.uid);
    set_message("queued task uid " + std::to_string(selected.uid) + " at " +
                    std::to_string(percent) + "% complete through " +
                    core::sched::to_string(plan.status_date) +
                    "; File > Save writes it",
                false);
    update_status();
}

// ---------------------------------------------------------------------------
// Wave 29 (W29-2.1): the Gantt Chart Format tab's three dialogs
// ---------------------------------------------------------------------------

void MainWindow::apply_session_gantt_style(
    const msproject::viewstate::GanttStyle& style) {
    if (!doc_.has_document()) {
        return;
    }
    // The style member is assigned in place, so the sheet model's and the
    // canvas's pointers stay valid; after_model_change() rebuilds the Gantt
    // (and its stored bar labels) and repaints the sheet from the new styles.
    doc_.set_session_gantt_style(style, prefs_.zoom);
    after_model_change();
}

void MainWindow::show_text_styles() {
    if (!doc_.has_document()) {
        set_message("open a project first", false);
        update_status();
        return;
    }
    TextStylesDialog dialog(doc_.gantt_style(), this);
    if (dialog.exec() != QDialog::Accepted || !dialog.changed()) {
        set_message(dialog.style_available()
                        ? "text styles cancelled; nothing changed"
                        : "this project has no decoded text styles to edit",
                    false);
        update_status();
        return;
    }
    apply_session_gantt_style(dialog.style());
    set_message("text styles applied to this view for the session; there is no "
                "Project style save pair, so File > Save keeps the file's bytes",
                false);
    update_status();
}

void MainWindow::show_gridlines() {
    GridlinesDialog dialog(this);
    dialog.exec();
    set_message("gridlines are not implemented in this port; nothing was changed",
                false);
    update_status();
}

void MainWindow::show_bar_styles() {
    if (!doc_.has_document()) {
        set_message("open a project first", false);
        update_status();
        return;
    }
    BarStylesDialog dialog(doc_.gantt_style(), this);
    if (dialog.exec() != QDialog::Accepted || !dialog.changed()) {
        set_message(dialog.style_available()
                        ? "bar styles cancelled; nothing changed"
                        : "this project has no decoded bar styles to edit",
                    false);
        update_status();
        return;
    }
    apply_session_gantt_style(dialog.style());
    set_message("bar styles applied to this view for the session; there is no "
                "Project style save pair, so File > Save keeps the file's bytes",
                false);
    update_status();
}

void MainWindow::show_visual_reports() {
    VisualReportsDialog dialog(this);
    dialog.exec();
    set_message("visual reports are not generated in this port; nothing was changed",
                false);
    update_status();
}

void MainWindow::show_spelling() {
    // W30-3.3: Project > Proofing > Spelling.  The brief's Spelling is a
    // read-only check over task names with no dictionary data from Project;
    // this port's check uses its own word list (app::spelling_issues) and
    // reports, per task, the words the list does not know.  The dialog has no
    // correction, replacement or dictionary path -- it reads a const plan --
    // so the command can never write the file or dirty the document.
    if (!doc_.has_document()) {
        set_message("open a project first", false);
        update_status();
        return;
    }
    const std::vector<app::SpellingIssue> issues = app::spelling_issues(doc_.plan());
    const std::size_t checked = app::spelling_names_checked(doc_.plan());
    SpellingDialog dialog(issues, checked, this);
    dialog.exec();
    set_message("spelling check is read-only; nothing was changed", false);
    update_status();
}

void MainWindow::show_compare_projects() {
    // Project's captured group caption names the open project by its file name;
    // the same three-step fallback as the window title keeps the two in step.
    std::string name = doc_.loaded().source_name;
    if (name.empty() && doc_.has_document()) {
        name = QFileInfo(to_q(doc_.path())).fileName().toStdString();
    }
    if (name.empty()) {
        name = "untitled.mpp";
    }
    CompareProjectsDialog dialog(to_q(name), this);
    dialog.exec();
    set_message("project comparison is not implemented in this port; nothing was "
                "changed",
                false);
    update_status();
}

void MainWindow::show_move_project() {
    if (!doc_.has_document()) {
        set_message("open a project first", false);
        update_status();
        return;
    }
    if (!app::project_has_actual_values(doc_.plan())) {
        set_message("this project has no tasks with actual values; Project would not ask "
                    "to move it",
                    false);
        update_status();
        return;
    }
    // Project's prompt, verbatim (the title is the port's own: no Microsoft
    // identity, decision on identity).  Moving a project is not implemented:
    // Yes is answered honestly, never as a fake success.
    QMessageBox prompt(QMessageBox::Question, QStringLiteral("Move Project"),
                       QString::fromUtf8(app::move_project_prompt_text()),
                       QMessageBox::Yes | QMessageBox::No, this);
    const int choice = prompt.exec();
    if (choice == QMessageBox::Yes) {
        set_message("moving a project is not implemented in this port; the file was not changed",
                    false);
    } else {
        set_message("move project cancelled", false);
    }
    update_status();
}

void MainWindow::show_change_working_time() {
    if (!doc_.has_document()) {
        set_message("open a project first", false);
        update_status();
        return;
    }
    const app::date32 today = app::today_date32();
    const core::sched::CivilDate civil = core::sched::to_civil(today);
    ChangeWorkingTimeDialog dialog(doc_.path(), doc_.format(), civil.year, civil.month,
                                   civil.day, this);
    if (dialog.exec() != QDialog::Accepted) {
        set_message("change working time cancelled", false);
        update_status();
        return;
    }
    set_message("change working time: the port has no calendar writer, nothing was changed",
                false);
    update_status();
}

void MainWindow::show_options() {
    // W26-4.6: File > Options.  The dialog is laid out from the eleven audit
    // captures; only General's Date format/Office Theme and Schedule's
    // "New tasks created" are live.
    const QString project =
        doc_.has_document()
            ? QString::fromStdString(doc_.title().empty() ? doc_.path() : doc_.title())
            : QStringLiteral("Current project");
    OptionsDialog dialog(prefs_, theme_choice_, new_tasks_manual_, project, this);
    if (dialog.exec() != QDialog::Accepted) {
        set_message("options cancelled", false);
        update_status();
        return;
    }
    apply_preferences(dialog.preferences(), true);
    set_theme_choice(dialog.theme_choice(), true);
    set_new_task_mode(dialog.new_tasks_manual());
}

void MainWindow::set_new_task_mode(bool manual) {
    // W28-3.1: with a saved document, Options > Schedule's choice is a pending
    // structural edit written to 114/Props record 0x024013c8 at File > Save
    // (Project's measured bytes: core/mppwrite/project_props.h).  A file that
    // does not store the record (an original Project 2007 MPP12 file) cannot be
    // written by this port, so the change is refused with the reason rather
    // than showing a mode the file will not carry.
    if (doc_.has_document()) {
        app::StructuralEdit edit;
        std::string error;
        if (!app::make_new_task_mode_edit(doc_.path(), manual, edit, error) ||
            !structural_.push(std::move(edit))) {
            set_message("new tasks: " + (error.empty()
                                             ? std::string("could not queue the change")
                                             : error),
                        true);
            update_status();
            return;
        }
        // The stack now holds the newest mode; after_structural_change refreshes
        // the shown mode, the edited dot and Save's enabled state.
        after_structural_change();
        set_message(std::string("new tasks: ") +
                        (manual ? "manually scheduled" : "auto scheduled") +
                        "; File > Save writes it",
                    false);
        update_status();
        return;
    }
    // No document: the shell's live mode.  A new project has no file to record
    // it in yet; the status bar and the new-project callout follow it, exactly
    // as Project's Options page changes the mode of the project it shows.
    new_tasks_manual_ = manual;
    new_tasks_manual_known_ = true;
    loaded_new_tasks_manual_ = manual;
    loaded_new_tasks_manual_known_ = true;
    if (info_callout_ != nullptr) {
        info_callout_->setText(
            manual ? QStringLiteral("New tasks are created in Manually Scheduled mode.")
                   : QStringLiteral("New tasks are created in Auto Scheduled mode."));
    }
    update_status_mode();
    set_message(std::string("new tasks: ") +
                    (manual ? "manually scheduled" : "auto scheduled"),
                false);
    update_status();
}

void MainWindow::show_table_date_format() {
    if (!doc_.has_document()) {
        set_message("open a project first", false);
        update_status();
        return;
    }
    if (!document_) {
        document_ = std::make_unique<app::Document>(doc_.path());
    }
    TableDateFormatDialog dialog(document_->view_state_edits(), this);
    if (dialog.exec() != QDialog::Accepted || !dialog.ok()) {
        set_message("table / date format cancelled", false);
        update_status();
        return;
    }
    document_->request_view_state_edits(dialog.edits());
    update_menu_state();
    update_window_title();
    set_message("table / date format edit pending; File > Save writes 214/CTable", false);
    update_status();
}

void MainWindow::show_custom_formula() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("Custom Field Formula is available in the Task Sheet view", false);
        update_status();
        return;
    }
    if (!document_) {
        document_ = std::make_unique<app::Document>(doc_.path());
    }
    CustomFormulaDialog dialog(document_->custom_formula(), this);
    if (dialog.exec() != QDialog::Accepted || !dialog.accepted_result()) {
        set_message("custom field formula cancelled", false);
        update_status();
        return;
    }
    const app::CustomFormulaEdit edit = dialog.result();
    if (edit.operation == app::CustomFormulaEdit::Operation::clear) {
        document_->clear_custom_formula();
        set_message("clearing every stored custom-field formula is pending; Save writes it",
                    false);
    } else {
        std::string failure;
        if (!document_->apply_custom_formula(edit.field_id, edit.formula, &failure)) {
            QMessageBox::warning(this, QStringLiteral("Custom Field Formula"), to_q(failure));
            set_message("custom field formula refused: " + failure, true);
            update_status();
            return;
        }
        set_message("custom-field formula for field " + std::to_string(edit.field_id) +
                        " is pending; Save writes it",
                    false);
    }
    update_menu_state();
    update_window_title();
    update_status();
}

void MainWindow::copy_selection() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
        doc_.sheet() == nullptr) {
        set_message("copying is available in the Task Sheet view", false);
        update_status();
        return;
    }
    core::sheet::SheetModel* sheet = doc_.sheet();
    if (!sheet->has_selection()) {
        set_message("select a cell or a range first", false);
        update_status();
        return;
    }
    clipboard_ = sheet->copy_selection();
    if (clipboard_.empty()) {
        set_message("nothing to copy", false);
        update_status();
        return;
    }
    // The internal block is the authoritative, typed copy (values only).  Put
    // the TSV rendering on the platform clipboard as well so another application
    // can receive the visible text; a paste with no internal block reads it back.
    if (QClipboard* platform = QGuiApplication::clipboard(); platform != nullptr) {
        platform->setText(QString::fromStdString(clipboard_.to_text()));
    }
    update_menu_state();
    set_message("copied " + std::to_string(clipboard_.rows) + "x" +
                    std::to_string(clipboard_.columns) + " cell(s), values only",
                false);
    update_status();
}

void MainWindow::cut_selection() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
        doc_.sheet() == nullptr) {
        set_message("cutting is available in the Task Sheet view", false);
        update_status();
        return;
    }
    core::sheet::SheetModel* sheet = doc_.sheet();
    if (!sheet->has_selection()) {
        set_message("select a cell or a range first", false);
        update_status();
        return;
    }
    // Cut is Copy + one clearing transaction: the shared action table has no cut
    // operation, and the sheet's own transaction records one undoable step.
    clipboard_ = sheet->copy_selection();
    if (clipboard_.empty()) {
        set_message("nothing to cut", false);
        update_status();
        return;
    }
    if (QClipboard* platform = QGuiApplication::clipboard(); platform != nullptr) {
        platform->setText(QString::fromStdString(clipboard_.to_text()));
    }
    if (!sheet->begin_transaction(core::sheet::UndoKind::Transaction, "Cut")) {
        set_message("the sheet is busy; finish the edit or transaction first", true);
        update_status();
        return;
    }
    std::size_t staged = 0;
    for (const core::sheet::CellRef& cell : sheet->selected_cells()) {
        // Wave 25 review: Cut writes the RecordSet directly and must obey the
        // same uid-0 rule as begin_cell_edit -- an edit on the synthetic
        // project row is shown by the sheet but never saved.
        if (cell.row >= sheet->row_count() || sheet->row_uid(cell.row) == 0) {
            continue;
        }
        if (cell.column >= sheet->column_count() || sheet->column_read_only(cell.column)) {
            continue;
        }
        if (sheet->stage_value(cell.row, cell.column, core::view::Value{})) {
            ++staged;
        }
    }
    if (staged == 0) {
        sheet->cancel_transaction();
        set_message("nothing cuttable in the selection (read-only columns)", true);
        update_status();
        return;
    }
    const core::sheet::TransactionResult result = sheet->apply_transaction();
    if (!result.ok) {
        sheet->cancel_transaction();
        set_message("cut failed: " + result.error, true);
        update_status();
        return;
    }
    after_cell_change();
    set_message("cut " + std::to_string(clipboard_.rows) + "x" +
                    std::to_string(clipboard_.columns) + " cell(s), values only",
                false);
    update_status();
}

void MainWindow::paste_selection() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
        doc_.sheet() == nullptr) {
        set_message("pasting is available in the Task Sheet view", false);
        update_status();
        return;
    }
    core::sheet::SheetModel* sheet = doc_.sheet();
    if (clipboard_.empty()) {
        // Nothing copied in this window: accept a TSV block from another
        // application (the same text copy_selection puts on the clipboard).
        const QClipboard* platform = QGuiApplication::clipboard();
        const QString text = platform != nullptr ? platform->text() : QString();
        if (text.isEmpty()) {
            set_message("the clipboard is empty", false);
            update_status();
            return;
        }
        clipboard_ = core::sheet::ClipboardBlock::from_text(text.toStdString());
    }
    if (clipboard_.empty()) {
        set_message("the clipboard holds no cells", true);
        update_status();
        return;
    }
    if (!sheet->has_selection()) {
        set_message("select the target cell first", false);
        update_status();
        return;
    }
    // Wave 25 review: SheetModel::paste writes the RecordSet directly, so the
    // uid-0 guard must be checked here too (the project row would show the
    // paste but the save diff never writes it).
    if (sheet->row_uid(sheet->anchor().row) == 0) {
        set_message("the project summary row is read-only", true);
        update_status();
        return;
    }
    const core::sheet::PasteResult result =
        sheet->paste(clipboard_, core::sheet::PasteMode::Values);
    if (!result.ok) {
        set_message("paste failed: " + result.error, true);
        update_status();
        return;
    }
    after_cell_change();
    select_core_range_in_view();
    std::string message = "pasted " + std::to_string(result.changed) + " changed cell(s)";
    if (result.skipped != 0) {
        message += ", " + std::to_string(result.skipped) + " skipped";
    }
    set_message(message, false);
    update_status();
}

void MainWindow::fill_down() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet ||
        doc_.sheet() == nullptr) {
        set_message("Fill Down is available in the Task Sheet view", false);
        update_status();
        return;
    }
    core::sheet::SheetModel* sheet = doc_.sheet();
    if (sheet->editing()) {
        set_message("finish the cell edit before Fill Down", true);
        update_status();
        return;
    }
    const core::sheet::CellRange range = sheet->selection_range();
    if (!range.valid) {
        set_message("select the Start cell to fill and the cells below it first", false);
        update_status();
        return;
    }
    // Only the visible columns in the rectangle are filled: a hidden column
    // shows no cell to copy.  In this port's stored table exactly one column
    // (Start) has a measured cell writer; every other column is refused up
    // front rather than filled and silently dropped by the save diff.
    std::vector<std::size_t> columns;
    for (std::size_t column : sheet->visible_columns()) {
        if (column >= range.column_begin && column <= range.column_end) {
            columns.push_back(column);
        }
    }
    if (columns.empty()) {
        set_message("the selection holds no visible column", false);
        update_status();
        return;
    }
    const core::view::FieldId start_field = app::task_field_id("Start");
    for (std::size_t column : columns) {
        if (start_field == 0 || sheet->column(column).field != start_field) {
            set_message("Fill Down fills only the Start column: " +
                            sheet->column_title(column) +
                            " has no measured cell writer in this port",
                        true);
            update_status();
            return;
        }
    }
    const std::vector<std::size_t> rows = sheet->selected_rows();
    if (rows.size() < 2) {
        set_message("select the Start cell and at least one cell below it", false);
        update_status();
        return;
    }
    // A contiguous selection's top visible row is the source.  The synthetic
    // project summary row is always the first row, so it can only be the
    // source here; its start is a roll-up and the row is read-only for every
    // other cell write (paste/cut), so refuse it before any staging.
    const std::size_t column = columns.front();
    const std::size_t source_row = rows.front();
    if (sheet->row_uid(source_row) == 0) {
        set_message("the project summary row is read-only", true);
        update_status();
        return;
    }
    // W31-0.4 (R30-4's U1): a summary's dates are Project's roll-up, not an
    // input, so filling one would stage a start the file does not own.  Refuse
    // the WHOLE selection before staging (the same before-any-staging rule the
    // other refusals above follow), source and targets alike.
    for (std::size_t row : rows) {
        if (sheet->is_summary(row)) {
            set_message("Fill Down cannot fill a summary row: its dates are a roll-up",
                        true);
            update_status();
            return;
        }
    }
    const core::view::Value* found = sheet->cell_value(source_row, column);
    if (found == nullptr || found->kind != core::view::ValueKind::Date) {
        set_message("the top selected Start cell has no date to fill", false);
        update_status();
        return;
    }
    const core::view::Value source = *found;
    const core::sched::date32 source_date = app::date32_from_date_value(source.date);
    if (core::sched::is_na(source_date) || core::sched::is_overflow(source_date)) {
        set_message("the top selected Start cell has no date to fill", false);
        update_status();
        return;
    }
    // Refuse before staging anything: collect_start_edits() skips a row that
    // is not in the loaded file (a pending insert), so filling one would show
    // a value the save silently drops.
    for (std::size_t index = 1; index < rows.size(); ++index) {
        const std::uint32_t uid = sheet->row_uid(rows[index]);
        bool loaded = false;
        for (const app::TaskRow& task : doc_.loaded().tasks) {
            if (task.uid == uid) {
                loaded = true;
                break;
            }
        }
        if (!loaded) {
            set_message("Fill Down cannot fill a row that has not been saved yet", true);
            update_status();
            return;
        }
    }
    // ONE transaction for every target cell: validation is atomic and the
    // whole fill is one undo step, the way Paste and Cut stage their ranges.
    if (!sheet->begin_transaction(core::sheet::UndoKind::Transaction, "Fill Down")) {
        set_message("the sheet is busy; finish the edit or transaction first", true);
        update_status();
        return;
    }
    std::size_t staged = 0;
    for (std::size_t index = 1; index < rows.size(); ++index) {
        if (sheet->stage_value(rows[index], column, source)) {
            ++staged;
        }
    }
    if (staged != rows.size() - 1) {
        sheet->cancel_transaction();
        set_message("Fill Down could not stage every selected cell", true);
        update_status();
        return;
    }
    const core::sheet::TransactionResult result = sheet->apply_transaction();
    if (!result.ok) {
        sheet->cancel_transaction();
        set_message("Fill Down failed: " + result.error, true);
        update_status();
        return;
    }
    // A cell edit ends the structural redo list: the two stacks stay
    // chronological (app/edit_model.h), as after_cell_commit() does.
    structural_.clear_redo();
    after_cell_change();
    set_message("filled " + std::to_string(result.changed) + " Start cell(s) down", false);
    update_status();
}

void MainWindow::select_core_range_in_view() {
    if (sheet_view_ == nullptr || doc_.sheet() == nullptr || sheet_view_->model() == nullptr ||
        prefs_.view != app::ViewMode::task_sheet) {
        return;
    }
    const core::sheet::CellRange range = doc_.sheet()->selection_range();
    if (!range.valid) {
        return;
    }
    auto display_row_of = [this](std::size_t record_row) {
        for (int row = 0; row < sheet_model_.rowCount(); ++row) {
            if (sheet_model_.record_row(row) == record_row) {
                return row;
            }
        }
        return -1;
    };
    auto display_column_of = [this](std::size_t model_column) {
        for (int column = 0; column < sheet_model_.columnCount(); ++column) {
            if (sheet_model_.model_column(column) == model_column) {
                return column;
            }
        }
        return -1;
    };
    const int first_row = display_row_of(range.row_begin);
    const int last_row = display_row_of(range.row_end);
    const int first_column = display_column_of(range.column_begin);
    const int last_column = display_column_of(range.column_end);
    if (first_row < 0 || last_row < 0 || first_column < 0 || last_column < 0) {
        return;
    }
    const QModelIndex top_left = sheet_model_.index(first_row, first_column);
    const QModelIndex bottom_right = sheet_model_.index(last_row, last_column);
    if (!top_left.isValid() || !bottom_right.isValid()) {
        return;
    }
    if (sheet_view_->selectionModel() != nullptr) {
        const QItemSelection selection(top_left, bottom_right);
        sheet_view_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    }
    sheet_view_->setCurrentIndex(top_left);
    sheet_view_->scrollTo(top_left);
}

void MainWindow::insert_task() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("Insert Task is available in the Task Sheet view", false);
        update_status();
        return;
    }
    QString name;
    if (!ask_task_name(this, &name)) {
        return;
    }
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::insert_task;
    edit.uid = structural_.next_placeholder_uid(doc_.plan());
    edit.anchor_uid = doc_.selected_uid();
    edit.name = name.toStdString();
    // W28-3.1: "Insert Task uses it" -- the row takes the project's shown
    // new-task mode (the loaded file's stored mode plus any pending Options >
    // Schedule choice).  An unknown mode shows/displays Manual (operator
    // decision 11), so the inserted row is manual there too.
    edit.manually_scheduled = !new_tasks_manual_known_ || new_tasks_manual_;
    if (edit.uid == 0 || !structural_.push(edit)) {
        set_message("could not queue the insert", true);
        update_status();
        return;
    }
    after_structural_change(edit.uid);
    set_message("queued a new task; File > Save writes it", false);
    update_status();
}

void MainWindow::delete_task() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("Delete Task is available in the Task Sheet view", false);
        update_status();
        return;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    if (!selected.has_selection || selected.uid == 0) {
        set_message("select a task first", false);
        update_status();
        return;
    }
    if (structural_.is_pending_insert(selected.uid)) {
        structural_.drop_insert(selected.uid);
        after_structural_change();
        set_message("dropped the pending insert", false);
        update_status();
        return;
    }
    const std::size_t dependants = app::count_delete_dependants(doc_.plan(), selected.uid);
    // Wave 24 (W24-1): name the task the delete will actually tombstone -- the
    // one the selected UID names, not plan().tasks[RecordSet row].
    QString name = to_q(std::to_string(selected.uid));
    if (const app::TaskRow* task = doc_.task_by_uid(selected.uid)) {
        name = to_q(task->name);
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, QStringLiteral("Delete Task"),
        QStringLiteral("Delete \"%1\"?\n\nThe writer also tombstones %2 dependent "
                       "link/assignment row(s). This is a pending edit; File > Save writes it.")
            .arg(name)
            .arg(static_cast<qulonglong>(dependants)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return;
    }
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::delete_task;
    edit.uid = selected.uid;
    edit.dependants = dependants;
    if (!structural_.push(edit)) {
        set_message("could not queue the delete", true);
        update_status();
        return;
    }
    after_structural_change();
    set_message("queued the delete; File > Save writes it", false);
    update_status();
}

// ---------------------------------------------------------------------------
// Wave 19: filter and views
// ---------------------------------------------------------------------------

QMenu* MainWindow::build_filter_menu() {
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("filterDropdown"));
    auto disabled = [](QAction* action) {
        action->setEnabled(false);
        action->setToolTip(QStringLiteral("not implemented in this port"));
        return action;
    };
    disabled(menu->addAction(QStringLiteral("Sort Earliest to Latest")));
    disabled(menu->addAction(QStringLiteral("Sort Latest to Earliest")));
    menu->addSeparator();
    disabled(menu->addAction(QStringLiteral("Group by: No Group")));
    menu->addSeparator();
    QAction* clear_start = menu->addAction(QStringLiteral("Clear Filter from Start"));
    connect(clear_start, &QAction::triggered, this, [this](bool) { clear_filter(); });
    menu->addSeparator();
    auto* filters = menu->addMenu(QStringLiteral("Filters"));
    for (const char* name : {"Task Name", "Duration", "Start", "Finish"}) {
        disabled(filters->addAction(QString::fromLatin1(name)));
    }
    QAction* select_all = menu->addAction(QStringLiteral("Select All"));
    select_all->setCheckable(true);
    menu->addSeparator();

    // The checkable date tree: year > months > days, with OK / Cancel.
    auto* panel = new QWidget(menu);
    panel->setObjectName(QStringLiteral("filterDatePanel"));
    auto* panel_layout = new QVBoxLayout(panel);
    panel_layout->setContentsMargins(6, 4, 6, 4);
    panel_layout->setSpacing(4);
    auto* tree = new QTreeWidget(panel);
    tree->setObjectName(QStringLiteral("filterDateTree"));
    tree->setHeaderHidden(true);
    tree->setFixedHeight(220);
    const QDate today = QDate::currentDate();
    auto* year = new QTreeWidgetItem(tree);
    year->setText(0, QString::number(today.year()));
    year->setFlags(year->flags() | Qt::ItemIsUserCheckable);
    year->setCheckState(0, Qt::Unchecked);
    std::vector<QTreeWidgetItem*> days;
    for (int month = 1; month <= 12; ++month) {
        auto* month_item = new QTreeWidgetItem(year);
        // QDate has no longMonthName in Qt 6; the locale owns the month name.
        month_item->setText(0, QLocale().monthName(month, QLocale::LongFormat));
        month_item->setFlags(month_item->flags() | Qt::ItemIsUserCheckable);
        month_item->setCheckState(0, Qt::Unchecked);
        for (int day = 1; day <= 31; ++day) {
            auto* day_item = new QTreeWidgetItem(month_item);
            day_item->setText(0, QString::number(day));
            day_item->setFlags(day_item->flags() | Qt::ItemIsUserCheckable);
            day_item->setCheckState(0, Qt::Unchecked);
            day_item->setData(0, Qt::UserRole, month);
            day_item->setData(0, Qt::UserRole + 1, day);
            days.push_back(day_item);
        }
    }
    year->setExpanded(true);
    panel_layout->addWidget(tree);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* ok = new QPushButton(QStringLiteral("OK"), panel);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), panel);
    ok->setObjectName(QStringLiteral("filterDateOk"));
    cancel->setObjectName(QStringLiteral("filterDateCancel"));
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    panel_layout->addLayout(buttons);
    auto* tree_action = new QWidgetAction(menu);
    tree_action->setDefaultWidget(panel);
    menu->addAction(tree_action);

    connect(select_all, &QAction::toggled, this, [days](bool checked) {
        for (QTreeWidgetItem* day : days) {
            day->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        }
    });
    connect(cancel, &QPushButton::clicked, menu, [menu](bool) { menu->close(); });
    connect(ok, &QPushButton::clicked, menu, [this, menu, days](bool) {
        // Apply the checked days as an Any-of-equals filter on Start, through the
        // same app::filter_model path the Filter dialog uses.
        if (doc_.has_document() && doc_.sheet() != nullptr) {
            const std::vector<app::FilterField> fields = app::filter_fields(*doc_.sheet());
            std::size_t field_index = core::sheet::kNoIndex;
            for (std::size_t index = 0; index < fields.size(); ++index) {
                if (fields[index].title == "Start") {
                    field_index = index;
                    break;
                }
            }
            const std::vector<app::FilterOperatorChoice>& operators = app::filter_operators();
            std::size_t equals_index = core::sheet::kNoIndex;
            for (std::size_t index = 0; index < operators.size(); ++index) {
                if (operators[index].op == core::view::FilterOp::Equals) {
                    equals_index = index;
                    break;
                }
            }
            std::vector<app::FilterRule> rules;
            for (QTreeWidgetItem* day : days) {
                if (day->checkState(0) != Qt::Checked) {
                    continue;
                }
                const int month = day->data(0, Qt::UserRole).toInt();
                const int number = day->data(0, Qt::UserRole + 1).toInt();
                const QDate date(QDate::currentDate().year(), month, number);
                if (!date.isValid()) {
                    continue;
                }
                app::FilterRule rule;
                rule.field_index = field_index;
                rule.operator_index = equals_index;
                rule.value = date.toString(QStringLiteral("yyyy-MM-dd")).toStdString();
                rules.push_back(std::move(rule));
            }
            if (field_index != core::sheet::kNoIndex && equals_index != core::sheet::kNoIndex &&
                !rules.empty()) {
                const app::FilterBuild built = app::build_filter(
                    fields, operators, rules, doc_.format(), app::FilterJoin::any);
                if (built.ok) {
                    doc_.set_filter(built.definition, built.description);
                    doc_.rebuild_views(prefs_.zoom);
                    apply_filter_to_sheet_model();
                    sync_view_selection();
                    gantt_canvas_->set_layout(doc_.gantt());
                    gantt_canvas_->set_selected_uid(doc_.selected_uid());
                    set_message("filter: " + built.description, false);
                    update_menu_state();
                    update_status();
                } else {
                    set_message(built.error, true);
                    update_status();
                }
            }
        }
        menu->close();
    });
    return menu;
}

QMenu* MainWindow::build_row_menu() {
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("taskRowMenu"));
    auto disabled = [](QAction* action) {
        action->setEnabled(false);
        action->setToolTip(QStringLiteral("not implemented in this port"));
        return action;
    };
    auto command = [this, menu](const QString& text, app::MenuAction id) {
        QAction* action = menu->addAction(text);
        connect(action, &QAction::triggered, this,
                [this, id](bool) { handle_action(id); });
        return action;
    };
    command(QStringLiteral("Cut"), app::MenuAction::cut);
    command(QStringLiteral("Copy"), app::MenuAction::copy);
    command(QStringLiteral("Paste"), app::MenuAction::paste);
    disabled(menu->addAction(QStringLiteral("Paste Special...")));
    menu->addSeparator();
    QAction* scroll = menu->addAction(QStringLiteral("Scroll to Task"));
    // W30-1.7: the row menu and the Task tab's button share one view path.
    connect(scroll, &QAction::triggered, this, [this](bool) { scroll_to_task(); });
    menu->addSeparator();
    command(QStringLiteral("Insert Task"), app::MenuAction::insert_task);
    command(QStringLiteral("Delete Task"), app::MenuAction::delete_task);
    disabled(menu->addAction(QStringLiteral("Inactivate Task")));
    menu->addSeparator();
    bool manual = true;
    if (doc_.has_document() && doc_.sheet() != nullptr) {
        const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
        // Wave 24 (W24-1): the label belongs to the selected UID's task.  The
        // project row (uid 0) has no task and keeps the default label.
        if (selected.has_selection) {
            if (const app::TaskRow* task = doc_.task_by_uid(selected.uid)) {
                manual = task->manually_scheduled;
            }
        }
    }
    // The item reflects the selected row's current mode.
    disabled(menu->addAction(manual ? QStringLiteral("Auto Schedule")
                                    : QStringLiteral("Manually Schedule")));
    menu->addSeparator();
    disabled(menu->addAction(QStringLiteral("Assign Resources...")));
    // W30-1.8: the row menu and the Task tab's Fill button shared one path.
    // W31-0.4 (R30-4): disabled with the decision-9 tooltip; the measured
    // Fill Down pair (tests/data/wave31-fill-*.mpp) is a scheduler rewrite the
    // port's start writer does not reproduce.
    disabled(menu->addAction(QStringLiteral("Fill Down")));
    disabled(menu->addAction(QStringLiteral("Clear Contents")));
    menu->addSeparator();
    QAction* information = menu->addAction(QStringLiteral("Information..."));
    connect(information, &QAction::triggered, this, [this](bool) { show_task_information(); });
    disabled(menu->addAction(QStringLiteral("Notes...")));
    menu->addSeparator();
    disabled(menu->addAction(QStringLiteral("Add to Timeline")));
    command(QStringLiteral("Link"), app::MenuAction::predecessors);
    return menu;
}

bool MainWindow::task_information_values(TaskInformationValues* out) const {
    if (out == nullptr || doc_.sheet() == nullptr) {
        return false;
    }
    const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
    // Wave 24 (W24-1/R5): the synthetic project row has no plan task.  Task
    // Information used to write through the sheet model on RecordSet row 0 and
    // the value was then dropped by sync_plan_from_records -- an edit that
    // looked accepted but never reached the file.  Refuse it here.
    if (!selected.has_selection || selected.uid == 0) {
        return false;
    }
    const std::size_t row = selected.row;
    if (row >= doc_.records().row_count()) {
        return false;
    }
    auto cell_text = [this, row](std::string_view field_name) {
        const core::view::FieldId field = app::task_field_id(field_name);
        for (std::size_t column = 0; column < doc_.sheet()->column_count(); ++column) {
            if (doc_.sheet()->column(column).field == field) {
                return app::cell_text(*doc_.sheet(), row, column, prefs_);
            }
        }
        return std::string();
    };
    out->name = cell_text("Name");
    out->duration_text = cell_text("Duration");
    out->start_text = cell_text("Start");
    out->finish_text = cell_text("Finish");
    out->percent_text = cell_text("PercentComplete");
    if (out->percent_text.empty()) {
        for (const app::TaskRow& task : doc_.plan().tasks) {
            if (task.uid == selected.uid) {
                out->percent_text = std::to_string(task.percent_complete) + "%";
                break;
            }
        }
    }
    out->manually_scheduled = false;
    for (const app::TaskRow& task : doc_.plan().tasks) {
        if (task.uid == selected.uid) {
            out->manually_scheduled = task.manually_scheduled;
            break;
        }
    }
    // Wave 26 (W26-5.3): the Custom Fields tab's six stored values.  The row is
    // resolved by UID (never by RecordSet row); an unstored field reads "".
    out->custom_field_ids.clear();
    out->custom_field_texts.clear();
    const app::TaskRow* task = doc_.task_by_uid(selected.uid);
    if (task != nullptr) {
        for (const app::TaskFieldSpec& spec : app::task_field_specs()) {
            out->custom_field_ids.push_back(spec.field_id);
            out->custom_field_texts.push_back(
                app::field_value_text(*task, spec.field_id, doc_.format()));
        }
    }
    return true;
}

bool MainWindow::apply_task_information(const TaskInformationValues& values,
                                        const char* source) {
    // The status prefix names the surface: Task Information's dialog or (W28-4.1)
    // the docked Task Form.  The applied edits are the same either way.
    const std::string prefix(source);
    if (doc_.sheet() == nullptr) {
        return false;
    }
    core::sheet::SheetModel* sheet = doc_.sheet();
    const app::SelectedTask selected = app::selected_task(*sheet, doc_.rows());
    if (!selected.has_selection) {
        set_message("select a task first", true);
        update_status();
        return false;
    }
    // Wave 24 (W24-1/R5): the project row is not a task and its sheet cells
    // are dropped by sync_plan_from_records; refuse to write through it.
    if (selected.uid == 0 || doc_.task_by_uid(selected.uid) == nullptr) {
        set_message("the project summary row is not editable in this port", true);
        update_status();
        return false;
    }
    // Wave 28 (W28-1.2): Percent complete is not a sheet cell here -- it is the
    // same set_percent structural edit the ribbon's five % buttons make, so the
    // save writes Project's measured actuals (write_percent_actuals).  Parse and
    // validate it BEFORE any sheet cell is committed: an invalid or refused
    // percent must leave the dialog's whole edit unapplied (decision 15).
    app::StructuralEdit percent_edit;
    bool has_percent_edit = false;
    {
        std::string current_text;
        const core::view::FieldId percent_field =
            app::task_field_id("PercentComplete");
        for (std::size_t column = 0; column < sheet->column_count(); ++column) {
            if (sheet->column(column).field == percent_field) {
                current_text = app::cell_text(*sheet, selected.row, column, prefs_);
                break;
            }
        }
        const app::TaskRow* task = doc_.task_by_uid(selected.uid);
        if (current_text.empty() && task != nullptr) {
            current_text = std::to_string(task->percent_complete) + "%";
        }
        if (values.percent_text != current_text) {
            std::uint16_t percent = 0;
            std::string error;
            if (!parse_percent_text(values.percent_text, &percent)) {
                set_message(prefix +
                                ": percent complete must be a whole number from 0 to 100",
                            true);
                update_status();
                return false;
            }
            if (task == nullptr ||
                !app::make_percent_edit(doc_.plan(), selected.uid, percent, percent_edit,
                                        error)) {
                set_message(prefix + ": " +
                                (task == nullptr ? std::string("no task row") : error),
                            true);
                update_status();
                return false;
            }
            // A different spelling of the same value ("50" for "50%") is not a
            // file change; the measured writer has no pair for an identical save.
            has_percent_edit = task->percent_complete != percent;
        }
    }
    struct FieldEdit {
        const char* name;
        const std::string* text;
    };
    // (The percent edit computed above is pushed with the other structural
    // edits, after every part of the dialog has validated.)
    const FieldEdit edits[] = {{"Name", &values.name},
                               {"Duration", &values.duration_text},
                               {"Start", &values.start_text},
                               {"Finish", &values.finish_text}};
    bool ok = true;
    bool applied = false;
    for (const FieldEdit& edit : edits) {
        const core::view::FieldId field = app::task_field_id(edit.name);
        std::size_t column = core::sheet::kNoIndex;
        for (std::size_t candidate = 0; candidate < sheet->column_count(); ++candidate) {
            if (sheet->column(candidate).field == field) {
                column = candidate;
                break;
            }
        }
        if (column == core::sheet::kNoIndex) {
            continue;  // the file's table does not show this field
        }
        // Only a CHANGED field is written: an untouched field keeps whatever
        // rendering the sheet gave it, so a formatted value the edit parser does
        // not accept can never turn an untouched field into an apply failure.
        if (sheet->cell_text(selected.row, column) == *edit.text) {
            continue;
        }
        if (!sheet->begin_edit(selected.row, column)) {
            ok = false;
            set_message(prefix + ": the " + edit.name + " column is read-only", true);
            continue;
        }
        sheet->set_edit_text(*edit.text);
        const core::sheet::EditOutcome outcome = sheet->commit_edit(false);
        if (!outcome.ok) {
            sheet->cancel_edit();
            ok = false;
            set_message(prefix + ": " + outcome.error, true);
            continue;
        }
        applied = applied || outcome.changed;
    }

    // Wave 26 (W26-5.3): the Custom Fields tab's six stored values, moved here
    // from the old Task Fields command.  app::make_task_field_edits parses the
    // texts and produces set_field/clear_field edits; the shared save cascade
    // writes them.  A parse error refuses the whole tab (decision 15: never a
    // partial, silently dropped edit).
    bool structural_applied = false;
    if (ok && !values.custom_field_ids.empty()) {
        const app::TaskRow* row = doc_.task_by_uid(selected.uid);
        std::vector<app::TaskFieldInput> inputs;
        inputs.reserve(values.custom_field_ids.size());
        for (std::size_t index = 0; index < values.custom_field_ids.size(); ++index) {
            inputs.push_back(app::TaskFieldInput{
                values.custom_field_ids[index],
                index < values.custom_field_texts.size() ? values.custom_field_texts[index]
                                                         : std::string()});
        }
        std::vector<app::StructuralEdit> field_edits;
        std::string field_error;
        if (row == nullptr ||
            !app::make_task_field_edits(*row, inputs, doc_.format(), field_edits,
                                        field_error)) {
            ok = false;
            set_message(prefix + ": " +
                            (field_error.empty() ? std::string("no task row")
                                                 : field_error),
                        true);
        } else {
            for (const app::StructuralEdit& edit : field_edits) {
                structural_.push(edit);
            }
            structural_applied = !field_edits.empty();
        }
    }
    // Wave 28 (W28-1.2): push the validated Percent complete edit once every
    // other part of the dialog has parsed, so a refused dialog queues nothing.
    if (ok && has_percent_edit) {
        if (!structural_.push(percent_edit, &doc_.plan())) {
            ok = false;
            set_message(prefix + ": the percent edit was refused", true);
        } else {
            structural_applied = true;
        }
    }

    if (ok) {
        if (structural_applied) {
            structural_.clear_redo();
            // Rebuilds the display plan from the custom-field edits and folds
            // the general cell edits back through without_structural_edits.
            after_structural_change(selected.uid);
        } else if (applied) {
            structural_.clear_redo();
            after_cell_change();
        }
        set_message(prefix + " applied", false);
    }
    update_status();
    return ok;
}

void MainWindow::show_task_information() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("Task Information is available in the Task Sheet view", true);
        update_status();
        return;
    }
    TaskInformationValues values;
    if (!task_information_values(&values)) {
        const app::SelectedTask selected =
            doc_.sheet() != nullptr ? app::selected_task(*doc_.sheet(), doc_.rows())
                                    : app::SelectedTask{};
        set_message(selected.has_selection && selected.uid == 0
                        ? "the project summary row is not editable in this port"
                        : "select a task first",
                    true);
        update_status();
        return;
    }
    if (!edit_task_information(this, &values)) {
        return;  // Cancel: nothing applied, nothing recorded
    }
    apply_task_information(values);
}

void MainWindow::show_filter_dialog() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("filtering is available in the Task Sheet view", false);
        update_status();
        return;
    }
    const std::vector<app::FilterField> fields = app::filter_fields(*doc_.sheet());
    if (fields.empty()) {
        set_message("this table has no filterable column", false);
        update_status();
        return;
    }
    core::view::FilterDefinition definition;
    std::string description;
    bool cleared = false;
    QString error;
    if (!edit_filter(this, fields, doc_.format(), &definition, &description, &cleared, &error)) {
        if (!error.isEmpty()) {
            set_message(error.toStdString(), true);
            update_status();
        }
        return;
    }
    if (cleared) {
        doc_.clear_filter();
        set_message("filter cleared", false);
    } else {
        doc_.set_filter(std::move(definition), description);
        set_message("filter: " + description, false);
    }
    doc_.rebuild_views(prefs_.zoom);
    apply_filter_to_sheet_model();
    sync_view_selection();
    gantt_canvas_->set_layout(doc_.gantt());
    gantt_canvas_->set_selected_uid(doc_.selected_uid());
    update_menu_state();
    update_status();
}

void MainWindow::clear_filter() {
    if (!doc_.has_document() || !doc_.filter_active()) {
        set_message("no filter is active", false);
        update_status();
        return;
    }
    doc_.clear_filter();
    doc_.rebuild_views(prefs_.zoom);
    apply_filter_to_sheet_model();
    sync_view_selection();
    gantt_canvas_->set_layout(doc_.gantt());
    gantt_canvas_->set_selected_uid(doc_.selected_uid());
    update_menu_state();
    set_message("filter cleared", false);
    update_status();
}

// Wave 31 (W31-1.1): the View tab's Sort.  Project persists a sort (it reorders
// the tasks in the file); this port applies it for the session only, as the
// wave-31 brief allows, so the RecordSet and the file keep their order.  The
// draft display order is app::sort_display_rows (outline-aware, project row
// pinned); after_model_change() rebuilds the table and the Gantt from it.
void MainWindow::show_sort_dialog() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("sorting is available in the Task Sheet view", false);
        update_status();
        return;
    }
    core::view::SortDefinition definition;
    std::string description;
    bool cleared = false;
    QString error;
    if (!edit_sort(this, app::sort_fields(), &definition, &description, &cleared, &error)) {
        if (!error.isEmpty()) {
            set_message(error.toStdString(), true);
            update_status();
        }
        return;
    }
    if (cleared) {
        doc_.clear_sort();
        set_message("sort cleared", false);
    } else {
        doc_.set_sort(std::move(definition), description);
        set_message("sort: " + description + " (session only)", false);
    }
    after_model_change();
}

// Wave 31 (W31-1.2): Project's View > Outline.  "Level N" leaves levels 1..N
// visible and "All Subtasks" expands everything; both are session-only view
// state (PlanDocument reuses the sheet's collapse state, so the file's rows and
// bytes never change).  The Gantt follows the same visible row list.
void MainWindow::show_outline_menu() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("outline levels are available in the Task Sheet view", false);
        update_status();
        return;
    }
    QMenu menu(this);
    for (int level = 1; level <= 9; ++level) {
        QAction* action = menu.addAction(QStringLiteral("Level %1").arg(level));
        connect(action, &QAction::triggered, this,
                [this, level]() { apply_outline_level(level); });
    }
    menu.addSeparator();
    QAction* all = menu.addAction(QStringLiteral("All Subtasks"));
    connect(all, &QAction::triggered, this, [this]() { apply_outline_level(0); });
    menu.exec(QCursor::pos());
}

void MainWindow::apply_outline_level(int level) {
    if (!doc_.has_document()) {
        return;
    }
    if (level == 0) {
        doc_.show_all_subtasks();
        set_message("outline: all subtasks", false);
    } else if (doc_.show_outline_level(level)) {
        set_message("outline: level " + std::to_string(level), false);
    } else {
        set_message("outline: choose a level from 1 to 9", true);
        update_status();
        return;
    }
    after_model_change();
}

// Wave 31 (W31-1.3): Project's View > Tables.  The list is the TASK tables the
// open file stores (never a built-in catalogue), and applying one changes the
// sheet's columns for this session only: the file keeps its own applied-table
// view state and its bytes.  Project persists that choice on save; this wave
// did not measure that save path, so the command is session-only.
void MainWindow::show_tables_menu() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("tables are available in the Task Sheet view", false);
        update_status();
        return;
    }
    if (doc_.table_count() == 0) {
        set_message("this file stores no task table", false);
        update_status();
        return;
    }
    QMenu menu(this);
    for (std::size_t index = 0; index < doc_.table_count(); ++index) {
        QAction* action = menu.addAction(QString::fromStdString(doc_.table_caption(index)));
        action->setCheckable(true);
        action->setChecked(index == doc_.selected_table());
        connect(action, &QAction::triggered, this, [this, index]() { apply_table(index); });
    }
    menu.exec(QCursor::pos());
}

void MainWindow::apply_table(std::size_t index) {
    if (!doc_.select_table(index)) {
        set_message("that table is not available", true);
        update_status();
        return;
    }
    // The sheet model's columns changed: rebuild the adapter's display columns,
    // the header band and the widths from the newly applied table before the
    // row refresh and selection sync.
    configure_task_columns();
    set_message("table: " + doc_.table_caption(index) + " (session only)", false);
    after_model_change();
}

// ---------------------------------------------------------------------------
// Wave 31 (W31-2.1): View > Zoom
// ---------------------------------------------------------------------------

void MainWindow::show_zoom_dialog() {
    if (!doc_.has_document()) {
        set_message("zoom needs an open project", false);
        update_status();
        return;
    }
    ZoomDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    apply_zoom_choice(dialog.choice());
}

void MainWindow::apply_zoom_choice(const ZoomChoice& choice) {
    if (!doc_.has_document()) {
        return;
    }
    // Project measures its 85 % fit against the chart pane (W31-2.2 measured
    // 1020 px at its window width); the shell asks the widget it draws into.
    const double width =
        gantt_canvas_ != nullptr ? static_cast<double>(gantt_canvas_->width()) : 0.0;
    bool applied = false;
    switch (choice.target) {
        case ZoomTarget::entire_project:
            applied = doc_.zoom_entire_project(width, prefs_.zoom);
            break;
        case ZoomTarget::selected_tasks:
            applied = doc_.zoom_selected_tasks(width, prefs_.zoom);
            break;
        default:
            // The fixed presets and Custom span from the chart's current left
            // edge, which is where Project's dialog anchors them (W31-2.2).
            applied = doc_.zoom_fixed_span(doc_.gantt().timescale.origin, choice.span_days(),
                                           width, prefs_.zoom);
            break;
    }
    if (!applied) {
        set_message("the chart has no span to fit", true);
        update_status();
        return;
    }
    if (prefs_.view == app::ViewMode::task_sheet) {
        gantt_canvas_->set_layout(doc_.gantt());
        gantt_canvas_->set_selected_uid(doc_.selected_uid());
    }
    refresh_timeline();
    const char* what = "requested span";
    if (choice.target == ZoomTarget::entire_project) {
        what = "entire project";
    } else if (choice.target == ZoomTarget::selected_tasks) {
        what = "selected tasks";
    }
    set_message(std::string("timescale fitted to the ") + what + " (session only)", false);
    update_status();
}

void MainWindow::rebuild_generic_view() {
    view_sheet_.reset();
    usage_sheet_.reset();
    if (!doc_.has_document()) {
        return;
    }
    if (prefs_.view == app::ViewMode::resource_sheet) {
        view_records_ = app::make_resource_view_records(doc_.plan(), nullptr);
        // W31-3.5: a loaded file's Resource Sheet uses the columns its own
        // stored `214/CTable` resource table carries (widths and captions
        // measured); a file without one keeps the registry fallback.
        if (doc_.resource_table().decoded) {
            view_sheet_ = std::make_unique<core::sheet::SheetModel>(
                view_records_, app::resource_sheet_columns(doc_.resource_table()));
        } else {
            view_sheet_ = std::make_unique<core::sheet::SheetModel>(
                view_records_, app::resource_sheet_columns());
        }
        return;
    }
    // W31-4.4: the two usage grids render app::make_usage_grid -- the file's own
    // stored details (Work by default; Actual Work, Cost when stored) and
    // timescale tiers, with the shell's Day/Week/Month choice as the fallback
    // for a file that stores no usage view.  Work is phased on the same stock
    // standard week the macOS shell uses.
    const bool resource = prefs_.view == app::ViewMode::resource_usage;
    const app::UsageViewStyle& style =
        resource ? doc_.resource_usage_style() : doc_.task_usage_style();
    const app::UsageGrid grid = app::make_usage_grid(
        doc_.plan(), style, resource, app::standard_calendar(),
        prefs_.usage_period);
    usage_sheet_ = std::make_unique<UsageSheet>(grid);
    view_records_ = usage_sheet_->records();
    view_sheet_ = std::make_unique<core::sheet::SheetModel>(view_records_,
                                                           usage_sheet_->columns());
    // Project's measured currency symbol on the fixture ("$"; W31-3.5 measured
    // the same application option for the Resource Sheet).  Every other format
    // value follows the document's own.
    core::sheet::SheetFormat format = doc_.format();
    if (format.currency_symbol.empty()) {
        format.currency_symbol = "$";
    }
    view_sheet_->set_format(std::move(format));
}

void MainWindow::show_view(app::ViewMode mode) {
    prefs_.view = mode;
    if (sheet_view_ == nullptr) {
        return;
    }
    auto* header = static_cast<SheetHeaderView*>(sheet_view_->horizontalHeader());
    auto* delegate = static_cast<EntryItemDelegate*>(sheet_view_->itemDelegate());
    if (header == nullptr || delegate == nullptr) {
        return;
    }
    // Detach the adapter first: the branches below destroy or replace the
    // SheetModel the adapter points at (view_sheet_/doc_.sheet_).
    sheet_model_.set_sheet(nullptr);
    // W31-4.4: the usage presentation's spans and per-cell value kinds belong
    // to the view being left; drop them before the branches rebuild.
    usage_sheet_.reset();
    sheet_view_->clearSpans();
    if (mode == app::ViewMode::task_sheet) {
        view_sheet_.reset();
        view_records_.clear();
        header->set_model_titles(false);
        delegate->set_entry_mode(true);
        sheet_view_->setEditTriggers(QAbstractItemView::AnyKeyPressed |
                                     QAbstractItemView::EditKeyPressed);
        gantt_canvas_->setVisible(true);
        if (chart_panel_ != nullptr) {
            chart_panel_->setVisible(true);
        }
        rebind_sheet();
        configure_task_columns();
        if (!splitter_user_moved_) {
            apply_split_fraction();
        }
        sync_view_selection();
        gantt_canvas_->set_layout(doc_.gantt());
        gantt_canvas_->set_selected_uid(doc_.selected_uid());
    } else {
        rebuild_generic_view();
        header->set_model_titles(true);
        delegate->set_entry_mode(false);
        sheet_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        gantt_canvas_->setVisible(false);
        if (chart_panel_ != nullptr) {
            chart_panel_->setVisible(false);
        }
        sheet_view_->horizontalScrollBar()->setValue(0);
        rebind_sheet();
        // W30-2.2: a generic view has no selection model of its own, and the
        // Resource Form follows the view's current row; start a freshly shown
        // view with no current row so the pane says nothing is selected rather
        // than carrying a stale index over from the previous view.
        sheet_view_->setCurrentIndex(QModelIndex());
        configure_generic_columns();
    }
    if (ribbon_ != nullptr) {
        // The contextual Gantt Chart Format tab exists only in the Gantt view.
        ribbon_->set_contextual_visible(mode == app::ViewMode::task_sheet);
    }
    // W26-6.3: the Task Form belongs to the task view.  The Details preference
    // survives a switch, so returning to the Gantt re-docks it.
    if (task_form_ != nullptr) {
        task_form_->setVisible(details_visible_ && mode == app::ViewMode::task_sheet);
    }
    // W30-2.2: the Resource Form is the same Details preference in the
    // Resource Sheet view; only one of the two forms is ever visible.
    if (resource_form_ != nullptr) {
        resource_form_->setVisible(details_visible_ && mode == app::ViewMode::resource_sheet);
    }
    // W30-1.10: the Task Inspector is a task-view pane too; its preference
    // survives the switch the same way.
    if (inspector_ != nullptr) {
        inspector_->setVisible(inspector_visible_ && mode == app::ViewMode::task_sheet);
    }
    refresh_task_form();
    refresh_inspector();
    refresh_resource_form();
    refresh_view_buttons();
    update_menu_state();
    update_status();
}

void MainWindow::set_view(app::ViewMode mode) {
    if (!doc_.has_document()) {
        prefs_.view = mode;
        refresh_view_buttons();
        return;
    }
    show_view(mode);
    set_message(std::string("view: ") + app::view_mode_title(mode), false);
    update_status();
}

void MainWindow::set_usage_period(core::usage::Period period) {
    prefs_.usage_period = period;
    if (doc_.has_document() && (prefs_.view == app::ViewMode::task_usage ||
                                prefs_.view == app::ViewMode::resource_usage)) {
        show_view(prefs_.view);
    }
    set_message(std::string("usage period: ") + core::usage::period_name(period), false);
    update_status();
}

std::size_t MainWindow::sheet_row_count() const {
    const int count = sheet_model_.rowCount();
    return count > 0 ? static_cast<std::size_t>(count) : 0;
}

std::size_t MainWindow::gantt_bar_count() const { return doc_.gantt().bars.size(); }

}  // namespace msqt
