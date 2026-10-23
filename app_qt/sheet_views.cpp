// app_qt/sheet_views.cpp -- see sheet_views.h.

#include "sheet_views.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QModelIndex>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStyle>
#include <QStyleOptionViewItem>

#include <algorithm>
#include <cstddef>

#include "entry_columns.h"
#include "shell_style.h"

namespace msqt {
namespace {

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
// resize grip the reference capture shows.
void paint_resize_grip(QPainter& painter, const QRect& rect) {
    painter.setPen(QPen(QColor(0xb0, 0xb0, 0xb0), 1));
    const int x = rect.right() - 5;
    const int y = rect.center().y();
    painter.drawLine(x, y - 4, x, y + 4);
    painter.drawLine(x + 2, y - 4, x + 2, y + 4);
}

}  // namespace

// ---------------------------------------------------------------------------
// SheetHeaderView
// ---------------------------------------------------------------------------

SheetHeaderView::SheetHeaderView(Qt::Orientation orientation, QWidget* parent)
    : QHeaderView(orientation, parent) {
    setSectionsMovable(false);
    setSectionsClickable(false);
    setHighlightSections(false);
    setStretchLastSection(false);
    setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setFixedHeight(kSheetHeaderHeight);
}

QSize SheetHeaderView::sizeHint() const { return QSize(120, kSheetHeaderHeight); }

void SheetHeaderView::paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const {
    if (painter == nullptr || !rect.isValid()) {
        return;
    }
    painter->save();
    painter->fillRect(rect, kHeaderBackground);

    // The caption band's column separators.  The band's TOP rule is the timeline
    // strip's bottom border (directly above), and its BOTTOM rule is drawn by the
    // row-0 cells (EntryItemDelegate) so it lands exactly on the first row's
    // boundary.  Drawing either rule here instead would move the parity
    // harness's band top by a pixel and lengthen the measured header.
    painter->setPen(QPen(kHeaderSeparator, 1));
    painter->drawLine(rect.right(), rect.top(), rect.right(), rect.bottom());
    // The sheet's left border (the frozen gutter's right edge) also runs through
    // the caption band, so the first column's left edge is ruled too.
    if (logicalIndex == 0) {
        painter->drawLine(rect.left(), rect.top(), rect.left(), rect.bottom());
    }

    const std::vector<EntryColumn>& columns = entry_columns();
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
               static_cast<std::size_t>(logicalIndex) < columns.size()) {
        const EntryColumn& column = columns[static_cast<std::size_t>(logicalIndex)];
        stub = column.kind == EntryCellKind::stub;
        title = QString::fromUtf8(column.title);
    }

    QFont font = header_font();
    font.setItalic(stub);
    painter->setFont(font);
    painter->setPen(stub ? kHeaderTextMuted : kHeaderText);

    // Reserve the right ~19 px for the chevron/grip, and let a narrow header
    // wrap ("Task" / "Mode") exactly as the reference does.
    const QRect text_rect = rect.adjusted(8, 2, -19, -2);
    const int text_flags = static_cast<int>(Qt::AlignLeft) |
                           static_cast<int>(Qt::AlignVCenter) |
                           static_cast<int>(Qt::TextWordWrap);
    painter->drawText(text_rect, text_flags, title);

    if (!title.isEmpty() && !model_titles_) {
        // Every titled Entry header carries the sort chevron, the reference's
        // "Add New Column" stub included (muted, like its caption).
        painter->setPen(QPen(stub ? kHeaderTextMuted : QColor(0x6a, 0x6a, 0x6a), 1));
        const int arrow_y = rect.center().y();
        const int arrow_x = rect.right() - 12;
        painter->drawLine(arrow_x - 3, arrow_y - 1, arrow_x, arrow_y + 2);
        painter->drawLine(arrow_x, arrow_y + 2, arrow_x + 3, arrow_y - 1);
    }
    if (!stub) {
        paint_resize_grip(*painter, rect);
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
    // The gutter is white like the sheet, with no frame: its right edge is the
    // sheet's first grid line, painted by SheetView.
    setStyleSheet(QStringLiteral("QHeaderView { background: #ffffff; border: none; }"));
    QPalette palette = QHeaderView::palette();
    palette.setColor(QPalette::Base, kRowGutter);
    palette.setColor(QPalette::Window, kRowGutter);
    palette.setColor(QPalette::Button, kRowGutter);
    palette.setColor(QPalette::WindowText, kRowGutterText);
    palette.setColor(QPalette::ButtonText, kRowGutterText);
    setPalette(palette);
    setAutoFillBackground(true);
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
    painter->save();
    painter->fillRect(rect, kRowGutter);
    painter->setFont(font());
    painter->setPen(kRowGutterText);
    // Right-aligned like the reference's numbers, which stop ~16 px short of
    // the sheet's left edge.
    const QRect text_rect = rect.adjusted(0, 0, -8, 0);
    painter->drawText(text_rect, static_cast<int>(Qt::AlignRight | Qt::AlignVCenter),
                      QString::number(logicalIndex + 1));
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
    // CE_HeaderEmptyArea, which our white Button palette already covers.
    if (viewport() != nullptr) {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), kRowGutter);
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
    QPalette corner_palette = corner_->palette();
    corner_palette.setColor(QPalette::Window, kHeaderBackground);
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
    const int mode = entry_column_index(EntryCellKind::mode);
    if (!entry_mode_ || index.column() != mode) {
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
        painter->setBrush(kBarTask);
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
