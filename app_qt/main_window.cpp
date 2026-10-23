// app_qt/main_window.cpp -- see main_window.h.

#include "main_window.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QMetaObject>
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
#include <cstdio>
#include <utility>
#include <vector>

#include "dialogs.h"
#include "document.h"
#include "entry_columns.h"
#include "export_pdf.h"
#include "field_link_edits.h"
#include "filter_model.h"
#include "gantt_canvas.h"
#include "layout_model.h"
#include "menus.h"
#include "print_page.h"
#include "ribbon_bar.h"
#include "selection_model.h"
#include "sheet_views.h"
#include "shell_style.h"
#include "start_edit_warning.h"
#include "timeline_strip.h"
#include "usage.h"
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
            set_message("this ribbon command is not wired in the Qt shell yet", false);
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

    sheet_view_ = new SheetView(splitter_);
    sheet_view_->setModel(&sheet_model_);
    // Wave 19: cells edit through SheetTableModel -> SheetModel's real edit
    // model.  Double-click and Return (the product's triggers); a rejected value
    // is reported in the status line.
    sheet_view_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::EditKeyPressed);
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
    // sizes).  The row-number gutter is the FROZEN vertical header
    // (SheetRowHeader): MS Project's row numbers do not scroll horizontally
    // with the sheet, and a model column cannot express that.
    auto* header = new SheetHeaderView(Qt::Horizontal, sheet_view_);
    header->setFixedHeight(kSheetHeaderHeight);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    sheet_view_->setHorizontalHeader(header);

    auto* row_header = new SheetRowHeader(sheet_view_);
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

    splitter_->addWidget(sheet_view_);
    splitter_->addWidget(gantt_canvas_);
    splitter_->setSizes(QList<int>{1, 1});
    outer->addWidget(splitter_, 1);

    central->setLayout(outer);
    setCentralWidget(central);

    // The editing adapter reports each commit here so the window can rebuild the
    // Gantt/status WITHOUT resetting the model from inside setData().
    sheet_model_.set_commit_handler(
        [this](bool ok, const std::string& error) { after_cell_commit(ok, error); });

    // A click selects through the sheet model (the single source of truth), then
    // mirrors the selection onto the Gantt bar.
    connect(sheet_view_, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        if (!doc_.has_document() || prefs_.view != app::ViewMode::task_sheet) {
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

    // View buttons, as the reference's status bar has.  They switch the window
    // through app::ViewModel (wave 19), so a click is never silent.
    auto* views = new QWidget(this);
    auto* view_layout = new QHBoxLayout(views);
    view_layout->setContentsMargins(4, 0, 4, 0);
    view_layout->setSpacing(2);
    view_buttons_.clear();
    for (const ViewButtonSpec& spec : kViewButtons) {
        auto* button = new QToolButton(views);
        button->setText(QString::fromLatin1(spec.label));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setAutoRaise(true);
        button->setFont(fixed_font(kStatusPointSize - 1));
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFixedHeight(kStatusHeight - 6);
        button->setToolTip(button->text());
        const app::ViewMode mode = spec.mode;
        connect(button, &QToolButton::clicked, this,
                [this, mode](bool) { set_view(mode); });
        view_layout->addWidget(button);
        view_buttons_.push_back(button);
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
    refresh_view_buttons();
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
    const int table_width = kGutterWidth + entry_content_width();
    if (sheet < table_width && table_width - sheet <= kSplitHandleWidth &&
        gantt_canvas_ != nullptr &&
        total - kSplitHandleWidth - table_width >= gantt_canvas_->minimumWidth()) {
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
        configure_sheet();
        gantt_canvas_->set_layout(core::gantt::GanttLayout{});
        gantt_canvas_->set_selected_uid(0);
        timeline_->set_dates(QString(), QString());
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

    // The Entry presentation, with no filter.
    auto* header = static_cast<SheetHeaderView*>(sheet_view_->horizontalHeader());
    auto* delegate = static_cast<EntryItemDelegate*>(sheet_view_->itemDelegate());
    if (header != nullptr) {
        header->set_model_titles(false);
    }
    if (delegate != nullptr) {
        delegate->set_entry_mode(true);
    }
    sheet_view_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::EditKeyPressed);
    gantt_canvas_->setVisible(true);

    rebind_sheet();
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

    update_window_title();
    set_message("loaded " + std::to_string(doc_.loaded().tasks.size()) + " task(s), " +
                    std::to_string(doc_.loaded().resources.size()) + " resource(s), " +
                    std::to_string(doc_.loaded().assignments.size()) + " assignment(s)",
                false);
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

void MainWindow::configure_sheet() {
    if (sheet_view_ == nullptr || prefs_.view != app::ViewMode::task_sheet) {
        return;
    }
    // Column widths come from the DATA: the file's `214/CTable` stores each
    // column's width in characters (the first of its nine opaque bytes), and
    // MS Project's column rule is one character = 6 px plus 3 px of padding.
    sheet_view_->setMinimumWidth(0);
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
        sheet_view_->setColumnWidth(static_cast<int>(index), width);
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
    const int name_column = entry_column_index(EntryCellKind::name);
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
    // The clipboard is not wired in the Qt shell yet: those commands stay
    // disabled rather than acting on data the user cannot see.
    state.can_copy = false;
    state.can_paste = false;
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
        case app::MenuAction::show_filter:
            show_filter_dialog();
            return;
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
        sheet_view_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                     QAbstractItemView::EditKeyPressed);
        gantt_canvas_->setVisible(true);
        rebind_sheet();
        configure_sheet();
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
        sheet_view_->horizontalScrollBar()->setValue(0);
        rebind_sheet();
        configure_generic_columns();
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
