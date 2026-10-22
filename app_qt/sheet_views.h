// app_qt/sheet_views.h -- the sheet's chrome: the caption band, the icon / grid
// cell painter and the splitter handle.
//
// Q1 used a plain QHeaderView; Q2 replaces it with a paint-only subclass so the
// caption band matches the measured reference (white fill, dark caption, the
// column separators, the sort chevron and a visible resize grip).  The
// row-number gutter is display column 0 of SheetTableModel (60 px), so it gets
// the ordinary cell painter and the row selection tint; only the two icon
// columns need a custom paint.
//
// None of these classes declares a signal or a slot, so no Q_OBJECT / AUTOMOC.
#pragma once

#include <QHeaderView>
#include <QModelIndex>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QWidget>

class QPainter;
class QPaintEvent;

namespace msqt {

// The sheet's column header: 42 px, white fill, dark caption, separators,
// chevron and grip (docs/parity.md section 2.1/2.3).
class SheetHeaderView final : public QHeaderView {
  public:
    explicit SheetHeaderView(Qt::Orientation orientation, QWidget* parent = nullptr);

    QSize sizeHint() const override;

  protected:
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override;
};

// Cell painter: the Indicators column draws a small (i) badge, Task Mode a pin;
// every other column is the ordinary styled item (the model supplies text,
// alignment and font).
class EntryItemDelegate final : public QStyledItemDelegate {
  public:
    explicit EntryItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};

// A splitter whose handle paints a visible drag thumb (three dots) on white, so
// the sheet/Gantt divide is grabbable at a glance and adds no weekend-coloured
// band that the parity harness could mistake for Gantt shading.
class SheetSplitterHandle final : public QSplitterHandle {
  public:
    SheetSplitterHandle(Qt::Orientation orientation, QSplitter* parent);

  protected:
    void paintEvent(QPaintEvent* event) override;
};

class SheetSplitter final : public QSplitter {
  public:
    explicit SheetSplitter(QWidget* parent = nullptr);

  protected:
    QSplitterHandle* createHandle() override;
};

}  // namespace msqt
