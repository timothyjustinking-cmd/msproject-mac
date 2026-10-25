// app_qt/sheet_views.cpp -- see sheet_views.h.

#include "sheet_views.h"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMenu>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QVariant>
#include <QWidget>
#include <QWidgetAction>

#include <algorithm>
#include <cstddef>
#include <utility>

#include "entry_columns.h"
#include "shell_style.h"
#include "sheet_table_model.h"

namespace msqt {
namespace {

// Wave 23 (B-1): Project's outline step and glyph colour, measured from the
// clean capture (row 0 and row 1 text at the same x; each level +16 px).
constexpr int kOutlineIndent = 16;
const QColor kOutlineGlyph(0x5a, 0x5a, 0x5a);

QFont header_font() {
    QFont font;
    font.setPointSize(kHeaderPointSize);
    return font;
}

QFont body_font() {
    QFont font = QApplication::font();
    font.setPointSize(kBodyPointSize);
    return font;
}

// Two small vertical ticks at the right edge of a header section: the column
// resize grip the reference capture shows.  W29-5.1 takes the tick colour from
// the theme's chrome palette.
void paint_resize_grip(QPainter& painter, const QRect& rect, const QColor& colour) {
    painter.setPen(QPen(colour, 1));
    const int x = rect.right() - 5;
    const int y = rect.center().y();
    painter.drawLine(x, y - 4, x, y + 4);
    painter.drawLine(x + 2, y - 4, x + 2, y + 4);
}

}  // namespace

// ---------------------------------------------------------------------------
// HeaderCaptionLayout (wave 21, operator defect)
// ---------------------------------------------------------------------------

HeaderCaptionLayout layout_header_caption(const QRect& section, const QString& title,
                                          const QFontMetrics& metrics) {
    HeaderCaptionLayout layout;
    layout.line_height = metrics.height();
    // The chevron: 10 px wide, 4 px from the section's right edge, vertically
    // centred.  Reserved BEFORE the text rect is derived from it.
    const int arrow_width = 8;
    const int arrow_right = section.right() - 3;
    const int arrow_left = arrow_right - arrow_width + 1;
    layout.arrow_rect = QRect(arrow_left, section.center().y() - 3, arrow_width, 6);
    // The caption starts 4 px in (the reference's own padding) and stops 1 px
    // short of the chevron, so even an antialiased glyph cannot reach the arrow.
    // `Duration` (55 px at 11 pt Carlito) fits an 81 px column; `Elapsed` (46 px)
    // fits the stored-table gate's 63 px column.
    const int text_left = section.left() + 4;
    const int text_right = arrow_left - 1;
    layout.text_rect = QRect(text_left, section.top() + 2,
                             std::max(0, text_right - text_left + 1),
                             std::max(0, section.height() - 4));
    if (title.isEmpty() || layout.text_rect.width() <= 0) {
        return layout;
    }

    // Break opportunities: spaces, and after a hyphen (a hyphen stays with the
    // fragment it ends).  Nothing else breaks a word.
    struct Token {
        QString text;
        bool space_before = false;
    };
    std::vector<Token> tokens;
    QString current;
    bool current_space = false;
    auto flush = [&]() {
        if (!current.isEmpty()) {
            tokens.push_back(Token{current, current_space});
            current.clear();
        }
    };
    for (int index = 0; index < title.size(); ++index) {
        const QChar character = title.at(index);
        if (character == QLatin1Char(' ')) {
            flush();
            current_space = true;
            continue;
        }
        current += character;
        if (character == QLatin1Char('-')) {
            flush();
            current_space = false;
        }
    }
    flush();

    QString line;
    for (const Token& token : tokens) {
        const QString candidate =
            line.isEmpty() ? token.text : line + (token.space_before ? " " : "") + token.text;
        if (!line.isEmpty() && metrics.horizontalAdvance(candidate) > layout.text_rect.width()) {
            layout.lines.push_back(line);
            line = token.text;
        } else {
            line = candidate;
        }
    }
    if (!line.isEmpty()) {
        layout.lines.push_back(line);
    }
    return layout;
}

// ---------------------------------------------------------------------------
// SheetHeaderView
// ---------------------------------------------------------------------------

SheetHeaderView::SheetHeaderView(Qt::Orientation orientation, QWidget* parent)
    : QHeaderView(orientation, parent), display_columns_(entry_display_columns()) {
    setSectionsMovable(false);
    setSectionsClickable(true);
    setHighlightSections(false);
    setStretchLastSection(false);
    setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setFixedHeight(kSheetHeaderHeight);
    // The product opens the column menu from the caption/arrow.
    connect(this, &QHeaderView::sectionClicked, this,
            [this](int logical_index) { show_column_menu(logical_index); });
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        show_column_menu(logicalIndexAt(pos.x()));
    });
}

void SheetHeaderView::set_column_command_handler(
    std::function<void(const QString&, int)> handler) {
    column_handler_ = std::move(handler);
}

QMenu* SheetHeaderView::build_column_menu(int logical_index) {
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("columnHeaderMenu"));

    // The compact format mini-bar at the top of the menu: family, size, B/I/U
    // and alignment.  The controls are laid out and editable; applying a font to
    // a column is not wired yet (backlog), so they do not fake an edit.
    auto* format_bar = new QWidget(menu);
    format_bar->setObjectName(QStringLiteral("columnFormatBar"));
    auto* bar_layout = new QHBoxLayout(format_bar);
    bar_layout->setContentsMargins(6, 3, 6, 3);
    bar_layout->setSpacing(3);
    auto* family = new QComboBox(format_bar);
    family->setObjectName(QStringLiteral("columnFontFamily"));
    family->addItem(QStringLiteral("Arial"));
    family->addItem(QStringLiteral("Calibri"));
    family->setFixedWidth(96);
    bar_layout->addWidget(family);
    auto* size = new QComboBox(format_bar);
    size->setObjectName(QStringLiteral("columnFontSize"));
    for (int point = 8; point <= 24; ++point) {
        size->addItem(QString::number(point));
    }
    size->setCurrentText(QStringLiteral("11"));
    size->setFixedWidth(48);
    bar_layout->addWidget(size);
    for (const char* letter : {"B", "I", "U"}) {
        auto* button = new QToolButton(format_bar);
        button->setObjectName(QStringLiteral("columnFormatButton"));
        button->setText(QString::fromLatin1(letter));
        button->setCheckable(true);
        button->setFixedSize(22, 22);
        bar_layout->addWidget(button);
    }
    for (const char* align : {"L", "C", "R"}) {
        auto* button = new QToolButton(format_bar);
        button->setObjectName(QStringLiteral("columnAlignButton"));
        button->setText(QString::fromLatin1(align));
        button->setCheckable(true);
        button->setFixedSize(22, 22);
        bar_layout->addWidget(button);
    }
    bar_layout->addStretch(1);
    auto* format_action = new QWidgetAction(menu);
    format_action->setDefaultWidget(format_bar);
    menu->addAction(format_action);

    auto add_command = [this, menu, logical_index](const QString& text, const QString& command) {
        QAction* action = menu->addAction(text);
        const int column = logical_index;
        if (command.isEmpty()) {
            action->setEnabled(false);
            action->setToolTip(QStringLiteral("not implemented in this port"));
            return action;
        }
        connect(action, &QAction::triggered, this, [this, command, column](bool) {
            if (column_handler_) {
                column_handler_(command, column);
            }
        });
        return action;
    };
    add_command(QStringLiteral("Cut"), QStringLiteral("cut"));
    add_command(QStringLiteral("Copy"), QStringLiteral("copy"));
    add_command(QStringLiteral("Paste"), QStringLiteral("paste"));
    add_command(QStringLiteral("Paste Special..."), QString());
    menu->addSeparator();
    add_command(QStringLiteral("Wrap Text"), QString());
    menu->addSeparator();
    add_command(QStringLiteral("Insert Column"), QStringLiteral("insert_column"));
    add_command(QStringLiteral("Hide Column"), QStringLiteral("hide_column"));
    menu->addSeparator();
    add_command(QStringLiteral("Font..."), QString());
    add_command(QStringLiteral("Text Styles..."), QStringLiteral("text_styles"));
    menu->addSeparator();
    add_command(QStringLiteral("Field Settings"), QString());
    auto* custom = menu->addMenu(QStringLiteral("Custom Fields"));
    for (const char* name : {"Text1", "Number1", "Date1"}) {
        QAction* action = custom->addAction(QString::fromLatin1(name));
        action->setEnabled(false);
        action->setToolTip(QStringLiteral("not implemented in this port"));
    }
    auto* data_type = menu->addMenu(QStringLiteral("Data Type"));
    for (const char* name : {"Text", "Number", "Date", "Duration", "Flag"}) {
        QAction* action = data_type->addAction(QString::fromLatin1(name));
        action->setEnabled(false);
        action->setToolTip(QStringLiteral("not implemented in this port"));
    }
    return menu;
}

void SheetHeaderView::show_column_menu(int logical_index) {
    if (logical_index < 0) {
        return;
    }
    QMenu* menu = build_column_menu(logical_index);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(QCursor::pos());
}

QSize SheetHeaderView::sizeHint() const { return QSize(120, kSheetHeaderHeight); }

void SheetHeaderView::set_theme(Theme theme) {
    theme_ = theme;
    update();
}

void SheetHeaderView::paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const {
    if (painter == nullptr || !rect.isValid()) {
        return;
    }
    // W29-5.1: the band, its ink and its rules are chrome, so they come from the
    // theme.  For light every value equals the wave-21 measured constants, so
    // the parity render is byte-identical to before this wave.
    const ChromePalette& chrome = chrome_palette(theme_);
    painter->save();
    painter->fillRect(rect, chrome.sheet_header_background);

    // The caption band's column separators.  The band's TOP rule is the timeline
    // strip's bottom border (directly above), and its BOTTOM rule is drawn by the
    // row-0 cells (EntryItemDelegate) so it lands exactly on the first row's
    // boundary.  Drawing either rule here instead would move the parity
    // harness's band top by a pixel and lengthen the measured header.
    painter->setPen(QPen(chrome.sheet_header_separator, 1));
    painter->drawLine(rect.right(), rect.top(), rect.right(), rect.bottom());
    // The sheet's left border (the frozen gutter's right edge) also runs through
    // the caption band, so the first column's left edge is ruled too.
    if (logicalIndex == 0) {
        painter->drawLine(rect.left(), rect.top(), rect.left(), rect.bottom());
    }

    QString title;
    bool stub = false;
    if (model_titles_) {
        // Wave 19 generic presentation: the sheet model's own column captions
        // (resource sheet, usage grids), so the band never shows Entry titles
        // over a different table.
        if (model() != nullptr) {
            title = model()->headerData(logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString();
        }
    } else if (logicalIndex >= 0 &&
               static_cast<std::size_t>(logicalIndex) < display_columns_.size()) {
        // Wave 21: the live presentation -- the Entry list for a new/empty
        // project, the file's own stored columns for a loaded file.
        const DisplayColumn& column = display_columns_[static_cast<std::size_t>(logicalIndex)];
        stub = column.kind == EntryCellKind::stub;
        title = QString::fromStdString(column.title);
    }

    QFont font = header_font();
    font.setItalic(stub);
    painter->setFont(font);
    painter->setPen(stub ? chrome.sheet_header_text_muted : chrome.sheet_header_text);

    // Wave 21: the chevron's rect is reserved before wrapping, the text is
    // wrapped by layout_header_caption() (spaces/hyphens only) and every line is
    // painted inside its own rect, so a caption can never run under the arrow or
    // be clipped to "Dura".
    const QFontMetrics metrics(font);
    const HeaderCaptionLayout layout = layout_header_caption(rect, title, metrics);
    const int block = layout.line_height * static_cast<int>(layout.lines.size());
    int line_top = layout.text_rect.center().y() - block / 2;
    for (const QString& line : layout.lines) {
        const QRect line_rect(layout.text_rect.left(), line_top, layout.text_rect.width(),
                              layout.line_height);
        painter->drawText(line_rect,
                          static_cast<int>(Qt::AlignLeft) |
                              static_cast<int>(Qt::AlignVCenter),
                          line);
        line_top += layout.line_height;
    }

    if (!title.isEmpty() && !model_titles_) {
        // Every titled Entry header carries the sort chevron, the reference's
        // "Add New Column" stub included (muted, like its caption).  Its rect is
        // the one the layout reserved.
        painter->setPen(QPen(stub ? chrome.sheet_header_text_muted : chrome.sheet_header_arrow,
                             1));
        const int arrow_y = layout.arrow_rect.center().y();
        const int arrow_x = layout.arrow_rect.center().x();
        painter->drawLine(arrow_x - 3, arrow_y - 1, arrow_x, arrow_y + 2);
        painter->drawLine(arrow_x, arrow_y + 2, arrow_x + 3, arrow_y - 1);
    }
    if (!stub) {
        paint_resize_grip(*painter, rect, chrome.sheet_header_grip);
    }
    painter->restore();
}

// ---------------------------------------------------------------------------
// SheetRowHeader -- the FROZEN row-number gutter
// ---------------------------------------------------------------------------

SheetRowHeader::SheetRowHeader(QWidget* parent) : QHeaderView(Qt::Vertical, parent) {
    setSectionsMovable(false);
    setSectionsClickable(false);
    setHighlightSections(false);
    setStretchLastSection(false);
    setSectionResizeMode(QHeaderView::Fixed);
    setDefaultSectionSize(kRowHeight);
    setMinimumSectionSize(kRowHeight);
    setFixedWidth(kGutterWidth);
    setFont(body_font());
    // The gutter is white like the sheet in the light theme, with no frame: its
    // right edge is the sheet's first grid line, painted by SheetView.
    set_theme(Theme::light);
}

void SheetRowHeader::set_theme(Theme theme) {
    // W29-5.1: the gutter is chrome -- in the dark themes its fill and number
    // ink are the measured ones, and the stylesheet/palette follow so the empty
    // area below the last row matches the painted sections.
    theme_ = theme;
    const ChromePalette& chrome = chrome_palette(theme);
    setStyleSheet(QStringLiteral("QHeaderView { background: %1; border: none; }")
                      .arg(chrome.row_header_background.name()));
    QPalette palette = QHeaderView::palette();
    palette.setColor(QPalette::Base, chrome.row_header_background);
    palette.setColor(QPalette::Window, chrome.row_header_background);
    palette.setColor(QPalette::Button, chrome.row_header_background);
    palette.setColor(QPalette::WindowText, chrome.row_header_text);
    palette.setColor(QPalette::ButtonText, chrome.row_header_text);
    setPalette(palette);
    setAutoFillBackground(true);
    update();
}

QSize SheetRowHeader::sizeHint() const {
    // QTableView asks the header for its width; pin the measured 60 px rather
    // than the width of a two-digit number.
    return QSize(kGutterWidth, QHeaderView::sizeHint().height());
}

void SheetRowHeader::paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const {
    if (painter == nullptr || !rect.isValid()) {
        return;
    }
    const ChromePalette& chrome = chrome_palette(theme_);
    painter->save();
    painter->fillRect(rect, chrome.row_header_background);
    painter->setFont(font());
    painter->setPen(chrome.row_header_text);
    // Right-aligned like the reference's numbers, which stop ~16 px short of
    // the sheet's left edge.  Wave 23 (B-1): the number is the FILE's ID column
    // (computed by the model), not the display ordinal -- project_a's rows are
    // stored out of ID order, and the project row is ID 0.
    const QRect text_rect = rect.adjusted(0, 0, -8, 0);
    QString number = QString::number(logicalIndex + 1);
    if (auto* table_model = dynamic_cast<SheetTableModel*>(model()); table_model != nullptr) {
        const QString id_text = table_model->row_number_text(logicalIndex);
        if (!id_text.isEmpty()) {
            number = id_text;
        }
    }
    painter->drawText(text_rect, static_cast<int>(Qt::AlignRight | Qt::AlignVCenter),
                      number);
    // The caption band's bottom rule (the first row's top edge) starts in the
    // gutter; the delegate draws it from x = 60 on.
    if (logicalIndex == 0) {
        painter->setPen(QPen(kGridLine, 1));
        painter->drawLine(rect.left(), rect.top(), rect.right(), rect.top());
    }
    painter->restore();
}

void SheetRowHeader::paintEvent(QPaintEvent* event) {
    // QHeaderView paints its sections on ITS VIEWPORT, not on the header widget
    // (qheaderview.cpp: `QPainter painter(d->viewport)`), so the background fill
    // has to target the same device.  The base class then paints the sections
    // (paintSection, above) and the area below the last row through
    // CE_HeaderEmptyArea, which the theme's Button palette already covers.
    if (viewport() != nullptr) {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), chrome_palette(theme_).row_header_background);
    }
    QHeaderView::paintEvent(event);
}

// ---------------------------------------------------------------------------
// SheetView
// ---------------------------------------------------------------------------

SheetView::SheetView(QWidget* parent) : QTableView(parent) {
    // The caption band's top-left corner is white, not the style's grey corner
    // panel.  It is a real child widget at the view's origin, sized from the two
    // fixed header constants; QTableView lays the vertical header out below it
    // and the horizontal header out to its right, so the rectangle is free.
    corner_ = new QWidget(this);
    corner_->setObjectName(QStringLiteral("sheetCorner"));
    corner_->setAutoFillBackground(true);
    // W29-5.1: the corner belongs to the frozen gutter, not the caption band --
    // in the dark captures it shares the gutter's fill (light: both white).
    QPalette corner_palette = corner_->palette();
    corner_palette.setColor(QPalette::Window,
                            chrome_palette(Theme::light).row_header_background);
    corner_->setPalette(corner_palette);
    corner_->setFixedSize(kGutterWidth, kSheetHeaderHeight);
    corner_->move(0, 0);
    corner_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    corner_->show();
    // QTableView owns a corner button in the same place (disabled by
    // setCornerButtonEnabled(false), but still painted); this widget is created
    // after it, and raising it makes the white corner unconditionally topmost.
    corner_->raise();
}

void SheetView::set_theme(Theme theme) {
    // W29-5.1: the caption band, the frozen gutter and the corner are chrome;
    // the viewport (cells, grid lines) is not touched -- decision 10 keeps the
    // sheet's own colours under every theme.
    theme_ = theme;
    const ChromePalette& chrome = chrome_palette(theme);
    if (corner_ != nullptr) {
        QPalette corner_palette = corner_->palette();
        corner_palette.setColor(QPalette::Window, chrome.row_header_background);
        corner_->setPalette(corner_palette);
        corner_->update();
    }
    if (auto* header = dynamic_cast<SheetHeaderView*>(horizontalHeader());
        header != nullptr) {
        header->set_theme(theme);
    }
    if (auto* gutter = dynamic_cast<SheetRowHeader*>(verticalHeader()); gutter != nullptr) {
        gutter->set_theme(theme);
    }
    update();
}

void SheetView::paintEvent(QPaintEvent* event) {
    QTableView::paintEvent(event);

    // The sheet's left border (the frozen gutter's right edge), painted on the
    // viewport so it stays at x = 60 while the columns scroll under it.  P1's
    // harness takes this line as the sheet's left edge and measures the nine
    // column widths from it.  The viewport is the device QTableView paints its
    // cells on, so this is the same painter target, not a second one.
    if (viewport() != nullptr && verticalHeader() != nullptr &&
        verticalHeader()->isVisible()) {
        QPainter painter(viewport());
        painter.setPen(QPen(kGridLine, 1));
        painter.drawLine(0, 0, 0, viewport()->height() - 1);
    }
}

// ---------------------------------------------------------------------------
// EntryItemDelegate
// ---------------------------------------------------------------------------

EntryItemDelegate::EntryItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void EntryItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    if (painter == nullptr) {
        return;
    }
    // Task Mode is the one painted icon column.  The Indicators column is
    // DELIBERATELY BLANK, like the reference capture's (MS Project shows an
    // indicator only when a task has one; the fixture's tasks have none).  It
    // is also load-bearing for P1's harness: a 16 px icon in a 22 px row spans
    // ~80 % of the measured body band, so the column detector reads the icon's
    // edge as an extra vertical grid line and reports 10 sheet columns instead
    // of 9 (wave 18, CI run f4a4e4a).  Keep every non-boundary column of the
    // body band under that threshold: no tall glyphs, no full-height rules.
    //
    // WAVE 22: Predecessors/Successors/Resource Names are NOT painted here.  They
    // are computed TEXT cells (plan_document.h derives the dependency list and
    // the assigned resource names), so they must take the ordinary
    // styled-item path below: the model's DisplayRole is the whole product
    // behaviour, exactly as Project renders them left-aligned with its own grid.
    EntryCellKind kind = EntryCellKind::data;
    if (kind_lookup_) {
        kind = kind_lookup_(index.column());
    } else if (index.column() == entry_column_index(EntryCellKind::mode)) {
        kind = EntryCellKind::mode;
    }
    // Wave 23 (B-1): the outline.  Project reserves one symbol column before
    // every Name cell and one 16 px step per outline level above it; the
    // project summary row (-1) shares the top-level tasks' text x, which is
    // what the clean capture shows (row 0 and row 1 both start at x = 82).
    const bool hierarchy =
        entry_mode_ && level_lookup_ != nullptr && index.column() == hierarchy_column_;
    if (hierarchy) {
        const bool selected = (option.state & QStyle::State_Selected) != 0;
        const int level = level_lookup_(index.row());
        const int indent = kOutlineIndent + (level > 0 ? level * kOutlineIndent : 0);
        const bool children = children_lookup_ && children_lookup_(index.row());
        painter->save();
        // Wave 29 (W29-1.2): the Name cell takes the model's stored fill too
        // (PROJECT_A's yellow Highlighted row, the shaded summary rows).  The
        // styled-item paint below would draw the same brush; filling it here
        // keeps the cell's edges under the outline symbol and indent
        // consistent with the text area.
        const QVariant background = index.data(Qt::BackgroundRole);
        const QColor fill = selected
                                ? kSelection
                                : (background.canConvert<QColor>()
                                       ? background.value<QColor>()
                                       : kSheetBackground);
        painter->fillRect(option.rect, fill);
        QStyleOptionViewItem text_option = option;
        text_option.rect.setLeft(text_option.rect.left() + indent);
        QStyledItemDelegate::paint(painter, text_option, index);
        if (children) {
            // Project's grey outline glyph: down when expanded, right when
            // collapsed, sitting in the symbol column just left of the text.
            const QRect symbol(option.rect.left() + indent - kOutlineIndent + 5,
                               option.rect.center().y() - 4, 9, 8);
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(kOutlineGlyph);
            QPolygon points;
            if (collapsed_lookup_ && collapsed_lookup_(index.row())) {
                points << QPoint(symbol.left() + 2, symbol.top())
                       << QPoint(symbol.left() + 2, symbol.bottom())
                       << QPoint(symbol.right(), symbol.center().y());
            } else {
                points << QPoint(symbol.left(), symbol.top() + 2)
                       << QPoint(symbol.right(), symbol.top() + 2)
                       << QPoint(symbol.center().x(), symbol.bottom());
            }
            painter->drawPolygon(points);
            painter->setRenderHint(QPainter::Antialiasing, false);
        }
        painter->restore();
    } else if (!entry_mode_ || kind != EntryCellKind::mode) {
        // The data columns and the blank Indicators cells are ordinary styled
        // items: the model's text/alignment/font and the selection palette do
        // the work, and Qt draws the grid over them.
        QStyledItemDelegate::paint(painter, option, index);
    } else {
        const bool selected = (option.state & QStyle::State_Selected) != 0;
        painter->save();
        painter->fillRect(option.rect, selected ? kSelection : kSheetBackground);
        painter->setRenderHint(QPainter::Antialiasing, true);
        // Task Mode: a small blue pin (12 px tall, well under the band
        // threshold above).
        const QRect box(option.rect.left() + 18, option.rect.center().y() - 6, 14, 12);
        painter->setPen(Qt::NoPen);
        // Wave 25 review: the pin is chrome, not a task bar -- it keeps its
        // pre-wave-25 colour instead of following the measured task palette.
        painter->setBrush(kTaskModePin);
        painter->drawRoundedRect(box, 2, 2);
        painter->setPen(QPen(QColor(0xff, 0xff, 0xff), 1));
        painter->drawLine(box.left() + 4, box.center().y(), box.right() - 4,
                          box.center().y());
        painter->restore();
    }

    // The 42 px caption band's bottom rule is the first row's top edge.  Drawing
    // it here, on every column of row 0, puts it on the 22 px row grid the
    // parity harness measures the header height against.
    if (index.row() == 0) {
        painter->setPen(QPen(kGridLine, 1));
        painter->drawLine(option.rect.left(), option.rect.top(), option.rect.right(),
                          option.rect.top());
    }
}

bool EntryItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index) {
    if (event != nullptr && event->type() == QEvent::MouseButtonRelease && entry_mode_ &&
        level_lookup_ != nullptr && toggle_handler_ != nullptr &&
        index.column() == hierarchy_column_ &&
        children_lookup_ && children_lookup_(index.row())) {
        const auto* mouse = static_cast<const QMouseEvent*>(event);
        const int level = level_lookup_(index.row());
        const int indent = kOutlineIndent + (level > 0 ? level * kOutlineIndent : 0);
        const int left = option.rect.left() + indent - kOutlineIndent;
        if (mouse->position().x() >= left &&
            mouse->position().x() <= left + kOutlineIndent &&
            option.rect.contains(mouse->position().toPoint())) {
            toggle_handler_(index.row());
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

// ---------------------------------------------------------------------------
// Splitter
// ---------------------------------------------------------------------------

SheetSplitterHandle::SheetSplitterHandle(Qt::Orientation orientation, QSplitter* parent)
    : QSplitterHandle(orientation, parent) {}

void SheetSplitterHandle::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0xff, 0xff, 0xff));
    painter.setPen(QPen(QColor(0xa8, 0xa8, 0xa8), 1));
    if (orientation() == Qt::Horizontal) {
        painter.drawLine(0, 0, 0, height());
        painter.drawLine(width() - 1, 0, width() - 1, height());
    } else {
        painter.drawLine(0, 0, width(), 0);
        painter.drawLine(0, height() - 1, width(), height() - 1);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x6a, 0x6a, 0x6a));
    if (orientation() == Qt::Horizontal) {
        const int cx = width() / 2;
        const int cy = height() / 2;
        painter.drawEllipse(QPoint(cx, cy - 6), 1, 1);
        painter.drawEllipse(QPoint(cx, cy), 1, 1);
        painter.drawEllipse(QPoint(cx, cy + 6), 1, 1);
    }
}

SheetSplitter::SheetSplitter(QWidget* parent) : QSplitter(Qt::Horizontal, parent) {
    setHandleWidth(kSplitHandleWidth);
    setChildrenCollapsible(false);
}

QSplitterHandle* SheetSplitter::createHandle() {
    return new SheetSplitterHandle(orientation(), this);
}

}  // namespace msqt
