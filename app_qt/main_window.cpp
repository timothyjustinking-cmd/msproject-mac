// app_qt/main_window.cpp -- see main_window.h.

#include "main_window.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QModelIndex>
#include <QPalette>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTableView>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "gantt_canvas.h"
#include "layout_model.h"
#include "menus.h"
#include "selection_model.h"
#include "shell_style.h"

namespace msqt {
namespace {

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

}  // namespace

void configure_application() {
    // Fusion everywhere: the same widget painting on Windows, macOS and Linux.
    QApplication::setStyle(QStringLiteral("Fusion"));

    // One palette, built in code (docs/ui-qt-port.md section 2.1).  Set after
    // setStyle, because setStyle installs the style's own standard palette.
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
    palette.setColor(QPalette::WindowText, QColor(0x10, 0x10, 0x10));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(255, 255, 255));
    palette.setColor(QPalette::Text, QColor(0x10, 0x10, 0x10));
    palette.setColor(QPalette::Button, QColor(0xf0, 0xf0, 0xf0));
    palette.setColor(QPalette::ButtonText, QColor(0x10, 0x10, 0x10));
    palette.setColor(QPalette::Highlight, QColor(0x33, 0x66, 0x99));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    QApplication::setPalette(palette);

    QFont font = QApplication::font();
    font.setPointSize(kBodyPointSize);
    QApplication::setFont(font);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("MS Project Shell"));
    resize(kWindowWidth, kWindowHeight);
    build_central_widget();
    build_menus();
    build_status_bar();
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
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const core::sheet::SheetMetrics metrics = app::default_sheet_metrics();

    sheet_view_ = new QTableView(central);
    sheet_view_->setModel(&sheet_model_);
    // Q1 shows the sheet; cell editing is Q2, so no edit trigger is enabled.
    sheet_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sheet_view_->setSelectionBehavior(QAbstractItemView::SelectItems);
    sheet_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    sheet_view_->setWordWrap(false);
    sheet_view_->setTextElideMode(Qt::ElideRight);
    sheet_view_->setShowGrid(true);
    sheet_view_->setGridStyle(Qt::SolidLine);
    sheet_view_->setAlternatingRowColors(false);
    sheet_view_->setCornerButtonEnabled(false);
    sheet_view_->setFrameShape(QFrame::NoFrame);
    sheet_view_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    sheet_view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    sheet_view_->setFont(fixed_font(kBodyPointSize));

    // The row height and header band come from the layout model, so a row in the
    // sheet and a bar in the Gantt sit on the same 20 px line.
    sheet_view_->verticalHeader()->setVisible(false);
    sheet_view_->verticalHeader()->setDefaultSectionSize(metrics.row_height);
    sheet_view_->verticalHeader()->setMinimumSectionSize(metrics.row_height);
    sheet_view_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    sheet_view_->horizontalHeader()->setFixedHeight(metrics.header_height);
    sheet_view_->horizontalHeader()->setStretchLastSection(false);
    sheet_view_->horizontalHeader()->setHighlightSections(false);
    sheet_view_->horizontalHeader()->setSectionsMovable(false);

    // One palette, built in code (docs/ui-qt-port.md section 2.1).
    QPalette palette = sheet_view_->palette();
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(255, 255, 255));
    palette.setColor(QPalette::Text, QColor(0, 0, 0));
    palette.setColor(QPalette::Highlight, QColor(0x33, 0x66, 0x99));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    sheet_view_->setPalette(palette);

    gantt_canvas_ = new GanttCanvas(central);
    gantt_canvas_->set_shading(prefs_.working_day_shading);

    layout->addWidget(sheet_view_, 0);
    layout->addWidget(gantt_canvas_, 1);
    central->setLayout(layout);
    setCentralWidget(central);

    // A click selects through the sheet model (the single source of truth), then
    // mirrors the selection onto the Gantt bar.
    connect(sheet_view_, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        if (!doc_.has_document()) {
            return;
        }
        const std::size_t row = sheet_model_.record_row(index.row());
        const std::size_t column = sheet_model_.model_column(index.column());
        if (row == core::sheet::kNoIndex || column == core::sheet::kNoIndex) {
            return;
        }
        if (!app::select_cell(*doc_.sheet(), row, column)) {
            return;
        }
        gantt_canvas_->set_selected_uid(doc_.selected_uid());
        update_menu_state();
    });
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
            const QKeySequence shortcut = menu_shortcut(item);
            if (!shortcut.isEmpty()) {
                action->setShortcut(shortcut);
            }
            const char* reason = app::menu_disabled_reason(item.action);
            if (reason != nullptr && reason[0] != '\0') {
                action->setToolTip(QString::fromUtf8(reason));
                action->setStatusTip(QString::fromUtf8(reason));
            }
            connect(action, &QAction::triggered, this,
                    [this, action_id = item.action]() { handle_action(action_id); });
            actions_.push_back(action);
        }
    }
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

    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(status_summary_, 1);
    statusBar()->addPermanentWidget(status_message_, 0);
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------

bool MainWindow::open_file(const QString& path, QString* error) {
    // Detach the view BEFORE the load: PlanDocument::load destroys the previous
    // SheetModel, and the Qt model must not keep pointing at it.
    sheet_model_.set_sheet(nullptr);
    std::string failure;
    if (!doc_.load(path.toStdString(), prefs_.zoom, failure)) {
        // The macOS shell leaves the previous state cleared rather than
        // half-loaded (app/README.md); the Qt model is already detached above.
        configure_sheet();
        gantt_canvas_->set_layout(core::gantt::GanttLayout{});
        gantt_canvas_->set_selected_uid(0);
        setWindowTitle(QStringLiteral("MS Project Shell"));
        if (error != nullptr) {
            *error = QString::fromStdString(failure);
        }
        set_message("load failed: " + failure, true);
        update_menu_state();
        update_status();
        return false;
    }

    sheet_model_.set_sheet(doc_.sheet());
    configure_sheet();
    sync_view_selection();
    gantt_canvas_->set_layout(doc_.gantt());
    gantt_canvas_->set_selected_uid(doc_.selected_uid());
    gantt_canvas_->set_shading(prefs_.working_day_shading);

    const std::string title = doc_.title().empty() ? std::string("MS Project Shell") : doc_.title();
    setWindowTitle(QString::fromStdString(title) + QStringLiteral(" - ") + path);

    set_message("loaded " + std::to_string(doc_.loaded().tasks.size()) + " task(s), " +
                    std::to_string(doc_.loaded().resources.size()) + " resource(s), " +
                    std::to_string(doc_.loaded().assignments.size()) + " assignment(s)",
                false);
    update_menu_state();
    update_status();
    return true;
}

void MainWindow::configure_sheet() {
    if (!doc_.has_document()) {
        sheet_view_->setMinimumWidth(0);
        return;
    }
    const core::sheet::SheetMetrics metrics = app::default_sheet_metrics();
    const int auto_width = metrics.char_width * 20;
    int total = 0;
    for (int display = 0; display < sheet_model_.columnCount(); ++display) {
        const std::size_t column = sheet_model_.model_column(display);
        int column_width = 0;
        if (column != core::sheet::kNoIndex) {
            // Widths are characters in the model; this is the one conversion to
            // points (integer pixels, never font-dependent).
            column_width = doc_.sheet()->column_width_points(column, metrics);
        }
        if (column_width <= 0) {
            column_width = auto_width;
        }
        sheet_view_->setColumnWidth(display, column_width);
        total += column_width;
    }
    sheet_view_->setMinimumWidth(total + 1);
}

// The QTableView's own current index is not the sheet's selection (SheetModel
// owns that), so mirror the selected task onto the view.  This is display-only:
// a click still goes through app::select_cell() in the clicked handler.
void MainWindow::sync_view_selection() {
    if (!doc_.has_document() || sheet_view_ == nullptr) {
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
    int display_column = 0;
    const core::view::FieldId name_field = app::name_field_id();
    for (int index = 0; index < sheet_model_.columnCount(); ++index) {
        const std::size_t column = sheet_model_.model_column(index);
        if (column != core::sheet::kNoIndex && doc_.sheet()->column(column).field == name_field) {
            display_column = index;
            break;
        }
    }
    sheet_view_->setCurrentIndex(
        sheet_model_.index(static_cast<int>(ordinal), display_column));
}

// ---------------------------------------------------------------------------
// Status and menu state
// ---------------------------------------------------------------------------

void MainWindow::update_status() {
    if (status_summary_ == nullptr) {
        return;
    }
    status_summary_->setText(QString::fromStdString(summary_text()));
    if (status_message_ != nullptr) {
        status_message_->setText(QString::fromStdString(message_));
        QPalette palette = status_message_->palette();
        palette.setColor(QPalette::WindowText,
                         message_is_error_ ? QColor(0xb0, 0x1c, 0x1c) : QColor(0x20, 0x20, 0x20));
        status_message_->setPalette(palette);
    }
}

void MainWindow::set_message(std::string text, bool is_error) {
    message_ = std::move(text);
    message_is_error_ = is_error;
}

std::string MainWindow::summary_text() const {
    if (!doc_.has_document()) {
        return "No project open";
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
    text += "  |  timescale " + std::string(app::zoom_level_name(prefs_.zoom)) + ", shading " +
            std::string(prefs_.working_day_shading ? "on" : "off");
    if (!doc_.path().empty()) {
        text += "  |  " + doc_.path();
    }
    return text;
}

void MainWindow::update_menu_state() {
    app::MenuState state;
    state.has_document = doc_.has_document();
    state.editing = false;
    const core::sheet::SheetModel* sheet = doc_.sheet();
    state.has_selection = sheet != nullptr && sheet->has_selection();
    state.can_undo = sheet != nullptr && sheet->can_undo();
    state.can_redo = sheet != nullptr && sheet->can_redo();
    // Q1 has no clipboard wiring and no save path: those commands stay disabled
    // rather than acting on data the user cannot see, and the menu tells the
    // truth (docs/ui-qt-port.md section 2.4).
    state.can_copy = false;
    state.can_paste = false;
    state.dirty = false;
    state.filter_active = false;
    state.task_view = true;
    if (sheet != nullptr) {
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
        action->setEnabled(app::menu_item_enabled(id, state));
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
            if (doc_.sheet() != nullptr) {
                doc_.sheet()->collapse_all();
                after_model_change();
            }
            return;
        case app::MenuAction::expand_all:
            if (doc_.sheet() != nullptr) {
                doc_.sheet()->expand_all();
                after_model_change();
            }
            return;
        case app::MenuAction::select_all:
            if (doc_.sheet() != nullptr) {
                doc_.sheet()->select_all();
                gantt_canvas_->set_selected_uid(doc_.selected_uid());
                sync_view_selection();
                update_menu_state();
            }
            return;
        case app::MenuAction::undo:
            if (doc_.sheet() != nullptr && doc_.sheet()->undo()) {
                after_model_change();
            }
            return;
        case app::MenuAction::redo:
            if (doc_.sheet() != nullptr && doc_.sheet()->redo()) {
                after_model_change();
            }
            return;
        default:
            break;
    }
    // Everything else belongs to Q2/Q3.  Say so, so a click is never silent;
    // the menu item itself is enabled exactly as app::menu_model says.
    set_message(std::string("'") + app::menu_action_name(action) +
                    "' is not wired in the Qt shell yet (wave 17)",
                false);
    update_status();
}

void MainWindow::open_dialog() {
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
                       "Opened file: ") +
            QString::fromStdString(doc_.path().empty() ? std::string("(none)") : doc_.path()));
}

void MainWindow::show_shortcuts() {
    QMessageBox::information(this, QStringLiteral("Keyboard Shortcuts"), shortcuts_text());
}

void MainWindow::set_zoom(app::ZoomLevel zoom) {
    if (!doc_.has_document() || prefs_.zoom == zoom) {
        return;
    }
    prefs_.zoom = zoom;
    doc_.rebuild_views(prefs_.zoom);
    gantt_canvas_->set_layout(doc_.gantt());
    gantt_canvas_->set_selected_uid(doc_.selected_uid());
    set_message(std::string("timescale ") + app::zoom_level_name(prefs_.zoom), false);
    update_status();
}

void MainWindow::after_model_change() {
    if (!doc_.has_document()) {
        return;
    }
    doc_.rebuild_views(prefs_.zoom);
    sheet_model_.refresh();
    sync_view_selection();
    gantt_canvas_->set_layout(doc_.gantt());
    gantt_canvas_->set_selected_uid(doc_.selected_uid());
    update_menu_state();
    update_status();
}

std::size_t MainWindow::sheet_row_count() const {
    const int count = sheet_model_.rowCount();
    return count > 0 ? static_cast<std::size_t>(count) : 0;
}

std::size_t MainWindow::gantt_bar_count() const { return doc_.gantt().bars.size(); }

}  // namespace msqt
