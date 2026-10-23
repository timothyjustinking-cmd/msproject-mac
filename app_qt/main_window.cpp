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
#include <QLocale>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
#include "filter_model.h"
#include "gantt_canvas.h"
#include "layout_model.h"
#include "menus.h"
#include "preferences_store.h"
#include "print_page.h"
#include "ribbon_bar.h"
#include "selection_model.h"
#include "sheet_views.h"
#include "shell_icons.h"
#include "shell_style.h"
#include "start_edit_warning.h"
#include "theme.h"
#include "timeline_strip.h"
#include "usage.h"
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
    for (const app::MenuSpec& menu : app::menu_specs()) {
        text += QString::fromUtf8(menu.title) + QStringLiteral("\n");
        for (const app::MenuItemSpec& item : menu.items) {
            const QKeySequence shortcut = menu_shortcut(item);
            if (shortcut.isEmpty()) {
                continue;
            }
            text += QStringLiteral("    ") + QString::fromUtf8(item.title) +
                    QStringLiteral("    ") + shortcut.toString(QKeySequence::NativeText) +
                    QStringLiteral("\n");
        }
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

// The status-bar view buttons, in the reference's order, mapped to the modes.
struct ViewButtonSpec {
    const char* label;
    app::ViewMode mode;
};

const ViewButtonSpec kViewButtons[4] = {
    {"Gantt Chart", app::ViewMode::task_sheet},
    {"Task Usage", app::ViewMode::task_usage},
    {"Resource Sheet", app::ViewMode::resource_sheet},
    {"Resource Usage", app::ViewMode::resource_usage},
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
    build_central_widget();
    build_menus();
    build_quick_access();
    // The File tab opens the backstage shell (operator decision 2).
    ribbon_->set_file_handler([this]() { show_backstage(true); });
    build_status_bar();
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
    sheet_view_->setItemDelegate(entry_delegate);
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

    splitter_->addWidget(sheet_panel);
    splitter_->addWidget(chart_panel_);
    splitter_->setSizes(QList<int>{1, 1});
    outer->addWidget(splitter_, 1);

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
        } else if (command == QStringLiteral("new")) {
            new_project();
        } else if (command == QStringLiteral("save")) {
            show_backstage(false);
            save_document(false);
        } else if (command == QStringLiteral("save_as")) {
            show_backstage(false);
            save_document(true);
        } else if (command == QStringLiteral("print")) {
            show_backstage(false);
            print_plan();
        } else if (command == QStringLiteral("export")) {
            show_backstage(false);
            export_pdf(app::ReportKind::gantt);
        } else if (command == QStringLiteral("close")) {
            close();
        }
    });
    backstage_->set_close_handler([this]() { show_backstage(false); });
    backstage_->set_options_handler([this]() { show_preferences(); });
    backstage_->set_theme_handler(
        [this](Theme theme) { set_theme(theme, true); });
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
        if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
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
    for (const app::MenuSpec& spec : app::menu_specs()) {
        QMenu* menu = bar->addMenu(QString::fromUtf8(spec.title));
        bool first = true;
        for (const app::MenuItemSpec& item : spec.items) {
            if (item.separator_before && !first) {
                menu->addSeparator();
            }
            first = false;
            QAction* action = menu->addAction(QString::fromUtf8(item.title));
            action->setData(static_cast<int>(item.action));
            // The quick-access toolbar reuses these actions, so give every one an
            // icon drawn by shell_icons.cpp (never Microsoft artwork).
            action->setIcon(shell_action_icon(item.action));
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
                // The app's action table marks Cut unsupported because the macOS
                // shell has no cut path.  The Qt shell implements it as
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
        // icon is ours (shell_icons.cpp), never Microsoft artwork.
        button->setIcon(shell_view_icon(view_index));
        button->setIconSize(QSize(kShellIconPx, kShellIconPx));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setAutoRaise(true);
        button->setFont(fixed_font(kStatusPointSize - 1));
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFixedHeight(kStatusHeight - 4);
        button->setToolTip(button->text());
        const app::ViewMode mode = spec.mode;
        connect(button, &QToolButton::clicked, this,
                [this, mode](bool) { set_view(mode); });
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
    quick_access_->setStyleSheet(
        QStringLiteral("QToolBar { background: #f7f7f7; border-bottom: 1px solid #d0d0d0;"
                       " spacing: 2px; padding: 1px 6px; }"));
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
        // Finish -- not the engine's 1984 default axis.
        doc_.build_empty_view(prefs_.zoom, app::today_date32());
        gantt_canvas_->set_layout(doc_.gantt());
        gantt_canvas_->set_selected_uid(0);
        refresh_timeline();
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

    // The persisted date style belongs to the loaded SheetFormat before anything
    // formats a cell or the status line.
    doc_.apply_preferences(prefs_);

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
    sheet_model_.set_display_rows(doc_.display_rows(), doc_.filter_active());
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
        columns = display_columns_for_sheet(*doc_.sheet());
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
    // widths (chars * 6 + 3) for a loaded one.  A stored width of 0 already fell
    // back to the stock width in display_columns_for_sheet(), so every column has
    // a positive width here.
    sheet_view_->setMinimumWidth(0);
    const std::vector<DisplayColumn>& columns = sheet_model_.display_columns();
    for (std::size_t index = 0; index < columns.size(); ++index) {
        if (columns[index].width > 0) {
            sheet_view_->setColumnWidth(static_cast<int>(index), columns[index].width);
        }
    }
    sheet_view_->verticalHeader()->setDefaultSectionSize(kRowHeight);
}

void MainWindow::configure_generic_columns() {
    if (sheet_view_ == nullptr || view_sheet_ == nullptr) {
        return;
    }
    const std::size_t count = view_sheet_->visible_column_count();
    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
        const std::size_t column = view_sheet_->visible_column(ordinal);
        if (column == core::sheet::kNoIndex) {
            continue;
        }
        const int chars = view_sheet_->column(column).width;
        int width = chars > 0 ? chars * 7 : 100;
        width = std::clamp(width, 50, 400);
        sheet_view_->setColumnWidth(static_cast<int>(ordinal), width);
    }
    sheet_view_->verticalHeader()->setDefaultSectionSize(kRowHeight);
}

// The QTableView's own current index is not the sheet's selection (SheetModel
// owns that), so mirror the selected task onto the view.  This is display-only:
// a click still goes through app::select_cell() in the clicked handler.
void MainWindow::sync_view_selection() {
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
    refresh_timeline();
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
    const std::string title =
        doc_.title().empty() ? std::string("MS Project Shell") : doc_.title();
    QString text = to_q(title) + QStringLiteral(" - ") + to_q(doc_.path());
    // The reference shows the standard edited dot; text is the portable form.
    if (document_dirty()) {
        text += QStringLiteral(" *");
    }
    setWindowTitle(text);
}

void MainWindow::refresh_view_buttons() {
    for (std::size_t index = 0; index < view_buttons_.size() && index < 4; ++index) {
        QToolButton* button = view_buttons_[index];
        if (button == nullptr) {
            continue;
        }
        const QSignalBlocker blocker(button);
        button->setChecked(kViewButtons[index].mode == prefs_.view);
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
        case app::MenuAction::task_fields:
            show_task_fields();
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

void MainWindow::open_dialog() {
    if (!confirm_proceed_with_discard()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Project"), QString(),
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
        QStringLiteral("MS Project Shell\n\nA personal three-platform port of MS Project.\n"
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
    if (prefs_.zoom == zoom) {
        update_zoom_slider();
        return;
    }
    prefs_.zoom = zoom;
    if (doc_.has_document()) {
        doc_.rebuild_views(prefs_.zoom);
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
    update_window_title();
    update_menu_state();
    update_status();
}

void MainWindow::after_structural_change(std::uint32_t keep_uid) {
    if (!doc_.has_document()) {
        return;
    }
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

bool MainWindow::save_document(bool force_panel) {
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
        if (!doc_.path().empty()) {
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

void MainWindow::print_plan() {
    if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
        set_message("printing is available in the Task Sheet view", true);
        update_status();
        return;
    }
    const std::vector<std::size_t> rows =
        print_rows(doc_.records().row_count(),
                   doc_.filter_active() ? doc_.filter_rows() : std::vector<std::size_t>{},
                   doc_.filter_active());
    const app::PrintJob job =
        make_print_job(doc_.plan(), rows,
                       doc_.filter_active() ? doc_.filter_description() : std::string(),
                       today_text(), prefs_.zoom);
    QString error;
    if (!print_job(this, job, *doc_.sheet(), doc_.plan(), prefs_, &error)) {
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
    const std::size_t total = doc_.records().row_count();
    const std::size_t shown = doc_.filter_active() ? doc_.filter_rows().size() : total;
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
    if (!selected.has_selection || selected.row >= doc_.plan().tasks.size()) {
        set_message("select a task first", false);
        update_status();
        return;
    }
    std::vector<app::StructuralEdit> edits;
    QString error;
    if (!edit_task_fields(this, doc_.plan().tasks[selected.row], doc_.format(), edits, &error)) {
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
    prefs_ = preferences;
    if (doc_.has_document()) {
        // The date format is the sheet's, so every date cell, the status line and
        // the PDF report follow this one change (app::apply_preferences).
        doc_.apply_preferences(prefs_);
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

void MainWindow::set_theme(Theme theme, bool persist) {
    theme_ = theme;
    apply_theme(theme_);
    update();
    if (persist && preferences_store_ != nullptr) {
        preferences_store_->save_theme(theme_);
    }
    set_message(std::string("Office theme: ") + theme_name(theme_), false);
    update_status();
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
    QString name = to_q(std::to_string(selected.uid));
    if (selected.row < doc_.plan().tasks.size()) {
        name = to_q(doc_.plan().tasks[selected.row].name);
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
    connect(scroll, &QAction::triggered, this, [this](bool) {
        if (sheet_view_ != nullptr) {
            sheet_view_->scrollTo(sheet_view_->currentIndex());
        }
    });
    menu->addSeparator();
    command(QStringLiteral("Insert Task"), app::MenuAction::insert_task);
    command(QStringLiteral("Delete Task"), app::MenuAction::delete_task);
    disabled(menu->addAction(QStringLiteral("Inactivate Task")));
    menu->addSeparator();
    bool manual = true;
    if (doc_.has_document() && doc_.sheet() != nullptr) {
        const app::SelectedTask selected = app::selected_task(*doc_.sheet(), doc_.rows());
        if (selected.has_selection && selected.row < doc_.plan().tasks.size()) {
            manual = doc_.plan().tasks[selected.row].manually_scheduled;
        }
    }
    // The item reflects the selected row's current mode.
    disabled(menu->addAction(manual ? QStringLiteral("Auto Schedule")
                                    : QStringLiteral("Manually Schedule")));
    menu->addSeparator();
    disabled(menu->addAction(QStringLiteral("Assign Resources...")));
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
    if (!selected.has_selection) {
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
    return true;
}

bool MainWindow::apply_task_information(const TaskInformationValues& values) {
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
    struct FieldEdit {
        const char* name;
        const std::string* text;
    };
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
            set_message(std::string("task information: the ") + edit.name +
                            " column is read-only",
                        true);
            continue;
        }
        sheet->set_edit_text(*edit.text);
        const core::sheet::EditOutcome outcome = sheet->commit_edit(false);
        if (!outcome.ok) {
            sheet->cancel_edit();
            ok = false;
            set_message("task information: " + outcome.error, true);
            continue;
        }
        applied = applied || outcome.changed;
    }
    if (ok) {
        if (applied) {
            structural_.clear_redo();
            after_cell_change();
        }
        set_message("task information applied", false);
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
        set_message("select a task first", true);
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

void MainWindow::rebuild_generic_view() {
    view_sheet_.reset();
    if (!doc_.has_document()) {
        return;
    }
    if (prefs_.view == app::ViewMode::resource_sheet) {
        view_records_ = app::make_resource_view_records(doc_.plan(), nullptr);
        view_sheet_ = std::make_unique<core::sheet::SheetModel>(view_records_,
                                                               app::resource_sheet_columns());
        return;
    }
    // The two usage grids: the same core::usage projection the macOS shell draws
    // (app/view_model.h), on the stock standard week the app documents.
    const core::usage::UsageProjection projection = core::usage::project_usage(
        app::usage_input_from_plan(doc_.plan()), app::standard_calendar(),
        prefs_.usage_period);
    const bool resource = prefs_.view == app::ViewMode::resource_usage;
    const app::UsageView usage =
        app::make_usage_view(projection, resource, doc_.plan(), doc_.format());
    view_records_ = app::make_usage_record_set(usage);
    view_sheet_ = std::make_unique<core::sheet::SheetModel>(view_records_,
                                                           app::usage_sheet_columns(usage));
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
        configure_generic_columns();
    }
    if (ribbon_ != nullptr) {
        // The contextual Gantt Chart Format tab exists only in the Gantt view.
        ribbon_->set_contextual_visible(mode == app::ViewMode::task_sheet);
    }
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
