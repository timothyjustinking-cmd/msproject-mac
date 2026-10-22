// app_qt/main_window.cpp -- see main_window.h.

#include "main_window.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QModelIndex>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "entry_columns.h"
#include "gantt_canvas.h"
#include "layout_model.h"
#include "menus.h"
#include "ribbon_bar.h"
#include "selection_model.h"
#include "sheet_views.h"
#include "shell_style.h"
#include "timeline_strip.h"

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

}  // namespace

QString& ui_font_family() { return font_family_storage(); }

bool ui_font_is_carlito() { return ui_font_family() == QStringLiteral("Carlito"); }

void configure_application() {
    // Fusion everywhere: the same widget painting on Windows, macOS and Linux.
    QApplication::setStyle(QStringLiteral("Fusion"));

    // One palette, built in code (docs/ui-qt-port.md section 2.1).  Set after
    // setStyle, because setStyle installs the style's own standard palette.
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
    palette.setColor(QPalette::WindowText, QColor(0x10, 0x10, 0x10));
    palette.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::AlternateBase, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::Text, QColor(0x10, 0x10, 0x10));
    palette.setColor(QPalette::Button, QColor(0xf0, 0xf0, 0xf0));
    palette.setColor(QPalette::ButtonText, QColor(0x10, 0x10, 0x10));
    palette.setColor(QPalette::Highlight, kSelection);
    palette.setColor(QPalette::HighlightedText, QColor(0x10, 0x10, 0x10));
    QApplication::setPalette(palette);

    // The fixed family list (shell_style.h).  Carlito is preferred when it is
    // installed; otherwise Calibri (the reference's own font) and then the
    // open fallbacks.  If none is present the platform family is kept, and the
    // handoff reports that.
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
    // The reference opens at the weekly timescale.
    prefs_.zoom = app::ZoomLevel::week;
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
    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Ribbon: tabs, groups and labelled buttons (structure and heights, text
    // only; see ribbon_bar.h).
    ribbon_ = new RibbonBar(central);
    ribbon_->set_action_handler([this](app::MenuAction action) {
        if (action == app::MenuAction::none) {
            set_message("this ribbon command is not wired in the Qt shell yet (wave 17)",
                        false);
            update_status();
            return;
        }
        handle_action(action);
    });
    outer->addWidget(ribbon_);

    // Timeline strip: Start/Finish at the ends, the timeline prompt centred.
    timeline_ = new TimelineStrip(central);
    outer->addWidget(timeline_);

    // Sheet + Gantt split.  The splitter is the sheet's right edge: 43 % of the
    // client width, with a visible drag thumb (sheet_views.h).
    splitter_ = new SheetSplitter(central);
    splitter_->setHandleWidth(kSplitHandleWidth);
    connect(splitter_, &QSplitter::splitterMoved, this,
            [this](int, int) { splitter_user_moved_ = true; });

    sheet_view_ = new QTableView(splitter_);
    sheet_view_->setModel(&sheet_model_);
    // Q1 shows the sheet; cell editing is Q2+ work, so no edit trigger is on.
    sheet_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
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
    sheet_view_->setItemDelegate(new EntryItemDelegate(sheet_view_));
    sheet_view_->setStyleSheet(QStringLiteral(
        "QTableView { background: #ffffff; gridline-color: #d4d4d4;"
        " selection-background-color: #dce9f5; selection-color: #101010; }"));

    // The 42 px caption band (paint-only; the model still owns the section
    // sizes).  The row-number gutter is model column 0, so the vertical header
    // is hidden: the gutter scrolls with the columns and its cell borders join
    // the sheet's grid, which is what the parity harness detects as the sheet's
    // left edge.
    auto* header = new SheetHeaderView(Qt::Horizontal, sheet_view_);
    header->setFixedHeight(kSheetHeaderHeight);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    sheet_view_->setHorizontalHeader(header);
    sheet_view_->verticalHeader()->setVisible(false);
    sheet_view_->verticalHeader()->setDefaultSectionSize(kRowHeight);
    sheet_view_->verticalHeader()->setMinimumSectionSize(kRowHeight);
    sheet_view_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    QPalette sheet_palette = sheet_view_->palette();
    sheet_palette.setColor(QPalette::Base, kSheetBackground);
    sheet_palette.setColor(QPalette::Text, kBodyText);
    sheet_palette.setColor(QPalette::Highlight, kSelection);
    sheet_palette.setColor(QPalette::HighlightedText, kBodyText);
    sheet_view_->setPalette(sheet_palette);

    gantt_canvas_ = new GanttCanvas(splitter_);
    gantt_canvas_->set_shading(prefs_.working_day_shading);

    splitter_->addWidget(sheet_view_);
    splitter_->addWidget(gantt_canvas_);
    splitter_->setSizes(QList<int>{1, 1});
    outer->addWidget(splitter_, 1);

    central->setLayout(outer);
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
        if (sheet_view_->viewport() != nullptr) {
            sheet_view_->viewport()->update();
        }
        update_menu_state();
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

    // View buttons, as the reference's status bar has: Gantt Chart / Task
    // Usage / Resource Sheet / Resource Usage.  They report until Q3 wires view
    // switching, so a click is never silent.
    auto* views = new QWidget(this);
    auto* view_layout = new QHBoxLayout(views);
    view_layout->setContentsMargins(4, 0, 4, 0);
    view_layout->setSpacing(2);
    const char* const names[4] = {"Gantt Chart", "Task Usage", "Resource Sheet",
                                 "Resource Usage"};
    for (int index = 0; index < 4; ++index) {
        auto* button = new QToolButton(views);
        button->setText(QString::fromLatin1(names[index]));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setChecked(index == 0);
        button->setAutoRaise(true);
        button->setFont(fixed_font(kStatusPointSize - 1));
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFixedHeight(kStatusHeight - 6);
        const QString label = QString::fromLatin1(names[index]);
        connect(button, &QToolButton::clicked, this, [this, label](bool) {
            set_message("view: " + label.toStdString() + " (view switching is not wired yet)",
                        false);
            update_status();
        });
        view_layout->addWidget(button);
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

    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(status_summary_, 1);
    statusBar()->addPermanentWidget(views, 0);
    statusBar()->addPermanentWidget(zoom_out, 0);
    statusBar()->addPermanentWidget(zoom_slider_, 0);
    statusBar()->addPermanentWidget(zoom_in, 0);
    statusBar()->addPermanentWidget(status_message_, 0);
    update_zoom_slider();

    for (QToolButton* button : views->findChildren<QToolButton*>()) {
        button->setToolTip(button->text());
    }
}

// ---------------------------------------------------------------------------
// Splitter geometry
// ---------------------------------------------------------------------------

void MainWindow::apply_split_fraction() {
    if (splitter_ == nullptr || splitter_->count() < 2) {
        return;
    }
    const int total = splitter_->width();
    if (total <= kSplitHandleWidth + 160) {
        return;
    }
    // The split's centre line sits at 43 % of the client width: the sheet is
    // that width less half the handle, so the draggable thumb straddles 43 %.
    const int sheet = std::max(160, static_cast<int>(std::lround(total * kSplitFraction)) -
                                        kSplitHandleWidth / 2);
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
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    // The first show delivers the layout's real width; pin the split then too,
    // so the measured fraction holds even when the window was constructed at
    // its final size (the offscreen tests do exactly that).
    if (centralWidget() != nullptr && centralWidget()->layout() != nullptr) {
        centralWidget()->layout()->activate();
    }
    if (!splitter_user_moved_) {
        apply_split_fraction();
    }
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
        timeline_->set_dates(QString(), QString());
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

    const app::PlanSummary summary = doc_.summary();
    if (!core::sched::is_na(summary.first_start) && !core::sched::is_na(summary.last_finish)) {
        timeline_->set_dates(
            QString::fromStdString(core::sheet::format_date_text(
                app::date_value_from_date32(summary.first_start), doc_.format())),
            QString::fromStdString(core::sheet::format_date_text(
                app::date_value_from_date32(summary.last_finish), doc_.format())));
    } else {
        timeline_->set_dates(QString(), QString());
    }

    const std::string title = doc_.title().empty() ? std::string("MS Project Shell") : doc_.title();
    setWindowTitle(QString::fromStdString(title) + QStringLiteral(" - ") + path);

    set_message("loaded " + std::to_string(doc_.loaded().tasks.size()) + " task(s), " +
                    std::to_string(doc_.loaded().resources.size()) + " resource(s), " +
                    std::to_string(doc_.loaded().assignments.size()) + " assignment(s)",
                false);
    update_zoom_slider();
    update_menu_state();
    update_status();
    return true;
}

void MainWindow::configure_sheet() {
    if (sheet_view_ == nullptr) {
        return;
    }
    // Column widths come from the DATA: the file's `214/CTable` stores each
    // column's width in characters (the first of its nine opaque bytes), and
    // MS Project's column rule is one character = 6 px plus 3 px of padding.
    // The stock Entry character widths are the fallback when the file has no
    // CTable for the field (entry_columns.h).
    sheet_view_->setMinimumWidth(0);
    sheet_view_->setColumnWidth(kGutterColumn, kGutterWidth);
    const std::vector<EntryColumn>& columns = entry_columns();
    for (std::size_t index = 0; index < columns.size(); ++index) {
        int chars = doc_.table_column_chars(columns[index].table_field);
        if (chars <= 0) {
            chars = columns[index].default_chars;
        }
        int width = entry_width_px(chars);
        if (width <= 0) {
            width = columns[index].width;
        }
        sheet_view_->setColumnWidth(static_cast<int>(index) + 1, width);
    }
    sheet_view_->verticalHeader()->setDefaultSectionSize(kRowHeight);
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
    const int name_column = entry_column_index(EntryCellKind::name) + 1;  // + gutter
    sheet_view_->setCurrentIndex(
        sheet_model_.index(static_cast<int>(ordinal), std::max(1, name_column)));
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

void MainWindow::update_zoom_slider() {
    if (zoom_slider_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(zoom_slider_);
    zoom_slider_->setValue(static_cast<int>(prefs_.zoom));
}

std::string MainWindow::stored_widths_text() const {
    // The `214/CTable` width byte (characters) for each Entry column, in sheet
    // order.  A zero means the file/table did not carry that field.
    std::string text;
    for (const EntryColumn& column : entry_columns()) {
        int chars = doc_.table_column_chars(column.table_field);
        if (chars == 0 && column.kind == EntryCellKind::mode) {
            // Some Entry tables carry the Indicators field (0x0b4000cd) in the
            // mode slot; Report the first width that exists.
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
    text += "  |  timescale " + std::string(app::zoom_level_name(prefs_.zoom)) + ", shading " +
            std::string(prefs_.working_day_shading ? "on" : "off");
    if (doc_.table_widths_read()) {
        text += "  |  Entry widths (chars) " + stored_widths_text();
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
    // Everything else belongs to Q3.  Say so, so a click is never silent;
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
                       "UI font: ") +
            ui_font_family() +
            (ui_font_is_carlito() ? QStringLiteral(" (bundled metric-compatible)")
                                  : QStringLiteral(" (fallback; Carlito not installed)")) +
            QStringLiteral("\n\nOpened file: ") +
            QString::fromStdString(doc_.path().empty() ? std::string("(none)") : doc_.path()));
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
        gantt_canvas_->set_layout(doc_.gantt());
        gantt_canvas_->set_selected_uid(doc_.selected_uid());
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
