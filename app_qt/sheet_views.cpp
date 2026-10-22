// app_qt/sheet_views.cpp -- see sheet_views.h.

#include "sheet_views.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QModelIndex>
#include <QPainter>
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
#include "sheet_table_model.h"
#include "shell_style.h"

namespace msqt {
namespace {

QFont header_font() {
    QFont font;
    font.setPointSize(kHeaderPointSize);
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

    const std::vector<EntryColumn>& columns = entry_columns();
    const int entry = logicalIndex - 1;
    if (entry >= 0 && static_cast<std::size_t>(entry) < columns.size()) {
        const EntryColumn& column = columns[static_cast<std::size_t>(entry)];
        const bool stub = column.kind == EntryCellKind::stub;

        QFont font = header_font();
        font.setItalic(stub);
        painter->setFont(font);
        painter->setPen(stub ? kHeaderTextMuted : kHeaderText);

        // Reserve the right ~18 px for the chevron/grip, and let a narrow header
        // wrap ("Task" / "Mode") exactly as the reference does.
        const QRect text_rect = rect.adjusted(8, 2, -19, -2);
        const int text_flags = static_cast<int>(Qt::AlignLeft) |
                               static_cast<int>(Qt::AlignVCenter) |
                               static_cast<int>(Qt::TextWordWrap);
        painter->drawText(text_rect, text_flags, QString::fromUtf8(column.title));

        if (!stub && column.title[0] != '\0') {
            painter->setPen(QPen(QColor(0x6a, 0x6a, 0x6a), 1));
            const int arrow_y = rect.center().y();
            const int arrow_x = rect.right() - 12;
            painter->drawLine(arrow_x - 3, arrow_y - 1, arrow_x, arrow_y + 2);
            painter->drawLine(arrow_x, arrow_y + 2, arrow_x + 3, arrow_y - 1);
        }
        if (!stub) {
            paint_resize_grip(*painter, rect);
        }
    }
    painter->restore();
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
    const int indicators = entry_column_index(EntryCellKind::indicators) + 1;
    const int mode = entry_column_index(EntryCellKind::mode) + 1;
    const bool custom = index.column() == indicators || index.column() == mode;
    if (!custom) {
        // The row-number gutter and the data columns are ordinary styled items:
        // the model's alignment, font and the selection palette do the work.
        QStyledItemDelegate::paint(painter, option, index);
    } else {
        const bool selected = (option.state & QStyle::State_Selected) != 0;
        painter->save();
        painter->fillRect(option.rect, selected ? kSelection : kSheetBackground);
        painter->setRenderHint(QPainter::Antialiasing, true);

        if (index.column() == indicators) {
            // The (i) indicator badge of the reference's first column.
            const int diameter = 16;
            const QRect box(option.rect.left() + 11,
                            option.rect.center().y() - diameter / 2, diameter, diameter);
            painter->setPen(QPen(QColor(0x2f, 0x7a, 0x3a), 1));
            painter->setBrush(QColor(0xe6, 0xf3, 0xe7));
            painter->drawEllipse(box);
            painter->setPen(QColor(0x2f, 0x7a, 0x3a));
            QFont font;
            font.setPointSize(kBodyPointSize - 2);
            font.setBold(true);
            painter->setFont(font);
            painter->drawText(box, Qt::AlignCenter, QStringLiteral("i"));
        } else {
            // Task Mode: a small blue pin.
            const QRect box(option.rect.left() + 18, option.rect.center().y() - 6, 14, 12);
            painter->setPen(Qt::NoPen);
            painter->setBrush(kBarTask);
            painter->drawRoundedRect(box, 2, 2);
            painter->setPen(QPen(QColor(0xff, 0xff, 0xff), 1));
            painter->drawLine(box.left() + 4, box.center().y(), box.right() - 4,
                              box.center().y());
        }
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
