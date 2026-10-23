// app_qt/sheet_views.h -- the sheet's chrome: the caption band, the frozen
// row-number gutter, the icon / grid cell painter and the splitter handle.
//
// Q1 used a plain QHeaderView; Q2 replaced it with a paint-only subclass so the
// caption band matches the measured reference (white fill, dark caption, the
// column separators, the sort chevron and a visible resize grip).
//
// WAVE 18 Q3 makes the row-number gutter FROZEN, as MS Project's is: it is the
// view's vertical header (SheetRowHeader), not a model column, so it cannot
// scroll horizontally with the columns.  The model therefore exposes only the
// nine Entry columns (sheet_table_model.h) and the gutter holds no data.
//
// None of these classes declares a signal or a slot, so no Q_OBJECT / AUTOMOC.
#pragma once

#include <QHeaderView>
#include <QModelIndex>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableView>
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

    // Wave 19: the resource sheet and the usage grids have their own column set,
    // so the caption band asks the model for each title instead of showing the
    // nine Entry titles.
    void set_model_titles(bool enabled) { model_titles_ = enabled; }

  protected:
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override;

  private:
    bool model_titles_ = false;
};

// The row-number gutter: the view's vertical header, 60 px, frozen.  QTableView
// scrolls the rows vertically through it (so the numbers stay on their rows) but
// never scrolls it horizontally, which is the product's frozen gutter.  The
// numbers are display ordinals: the model has no gutter column.
class SheetRowHeader final : public QHeaderView {
  public:
    explicit SheetRowHeader(QWidget* parent = nullptr);

    QSize sizeHint() const override;

  protected:
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override;
    void paintEvent(QPaintEvent* event) override;
};

// The sheet view: a QTableView whose top-left corner (left of the column
// captions, above the frozen gutter) is a white child widget instead of the
// style's grey corner panel, and which paints the sheet's own left border on
// the viewport so the frozen gutter keeps a border while the columns scroll
// under it.
class SheetView final : public QTableView {
  public:
    explicit SheetView(QWidget* parent = nullptr);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    // The caption band's top-left corner: kGutterWidth x kSheetHeaderHeight at
    // the view's origin, i.e. exactly the cell QTableView leaves between the two
    // headers.  A child widget rather than a painted rectangle, because a
    // QTableView paints its cells on the viewport and a second painter on the
    // view from inside that paint event is not a supported device.
    QWidget* corner_ = nullptr;
};

// Cell painter: Task Mode draws its pin; every other column -- Indicators
// included -- is the ordinary styled item (the model supplies text, alignment
// and font).  The Indicators cells are deliberately BLANK: the reference's are,
// and a tall icon there is read by P1's column detector as an extra grid line.
class EntryItemDelegate final : public QStyledItemDelegate {
  public:
    explicit EntryItemDelegate(QObject* parent = nullptr);

    // Wave 19: the Task Mode pin belongs to the Entry table only; the generic
    // resource/usage grids are ordinary text cells.
    void set_entry_mode(bool enabled) { entry_mode_ = enabled; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

  private:
    bool entry_mode_ = true;
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
