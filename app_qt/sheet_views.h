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

#include <QFontMetrics>
#include <QHeaderView>
#include <QModelIndex>
#include <QRect>
#include <QString>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QWidget>

#include <functional>
#include <utility>
#include <vector>

#include "entry_columns.h"  // DisplayColumn / EntryCellKind (wave 21)

class QEvent;
class QMenu;
class QPainter;
class QPaintEvent;

namespace msqt {

// Wave 21 (operator defect): the caption band's text/arrow geometry as a PURE
// function, so a gate can prove the layout for any width without painting.
//
// The defect it fixes: the band reserved its chevron space and then let Qt wrap
// with a bounding rect, so a caption could run under the sort chevron ("Task /
// Mod") and a single-word caption was clipped ("Duration" -> "Dura" at the
// measured 81 px).  This layout:
//   * reserves the chevron rect FIRST and derives the text rect from it, so no
//     line can ever intersect the arrow;
//   * wraps at spaces and after hyphens only, greedily;
//   * never elides or shortens a caption -- a line is the text as authored, and a
//     single word wider than the column is wrapped or clipped at the TEXT rect's
//     edge, never under the arrow.
struct HeaderCaptionLayout {
    std::vector<QString> lines;
    QRect text_rect;   // where the lines are painted
    QRect arrow_rect;  // the sort chevron / column-menu arrow
    int line_height = 0;
};

HeaderCaptionLayout layout_header_caption(const QRect& section, const QString& title,
                                          const QFontMetrics& metrics);

// Wave 20: the column header's arrow menu -- a compact format mini-bar
// (font family/size, B/I/U, alignment) followed by the product's measured
// command order (Cut/Copy/Paste, Wrap Text, Insert/Hide Column, Font/Text
// Styles, Field Settings, Custom Fields, Data Type).
class SheetHeaderView final : public QHeaderView {
  public:
    explicit SheetHeaderView(Qt::Orientation orientation, QWidget* parent = nullptr);

    QSize sizeHint() const override;

    // Wave 19: the resource sheet and the usage grids have their own column set,
    // so the caption band asks the model for each title instead of showing the
    // nine Entry titles.
    void set_model_titles(bool enabled) { model_titles_ = enabled; }

    // Wave 21 (operator decision 7): the captions and the Add New Column hint the
    // band paints.  Defaults to the Entry list; a loaded file's window sets the
    // file's own stored columns (entry_columns.h, display_columns_for_sheet()).
    void set_display_columns(std::vector<DisplayColumn> columns) {
        display_columns_ = std::move(columns);
    }
    const std::vector<DisplayColumn>& display_columns() const { return display_columns_; }

    // The arrow menu for `logical_index`, built and returned (not exec'd) so the
    // structural gate can inspect its items and mini format bar.
    QMenu* build_column_menu(int logical_index);
    void show_column_menu(int logical_index);
    void set_column_command_handler(std::function<void(const QString&, int)> handler);

  protected:
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override;

  private:
    bool model_titles_ = false;
    std::vector<DisplayColumn> display_columns_;
    std::function<void(const QString&, int)> column_handler_;
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

    // Wave 23 (B-1): the outline the Name cell draws -- text indent per level,
    // a collapse/expand triangle on summary rows, and a click on it toggles.
    // The lookups take a DISPLAY row (the adapter's row, after any filter).
    using OutlineLevelLookup = std::function<int(int display_row)>;
    using ChildrenLookup = std::function<bool(int display_row)>;
    using CollapsedLookup = std::function<bool(int display_row)>;
    using ToggleHandler = std::function<void(int display_row)>;
    void set_hierarchy_column(int display_column) { hierarchy_column_ = display_column; }
    void set_outline_lookups(OutlineLevelLookup level, ChildrenLookup children,
                             CollapsedLookup collapsed, ToggleHandler toggle) {
        level_lookup_ = std::move(level);
        children_lookup_ = std::move(children);
        collapsed_lookup_ = std::move(collapsed);
        toggle_handler_ = std::move(toggle);
    }

    // Wave 21: which column carries the Task Mode pin (and which is the Add New
    // Column stub) is the live presentation's business -- a loaded file's table
    // can put Task Mode anywhere, or not have it at all.  The window points this
    // at SheetTableModel::display_kind(); without a lookup the Entry index is used.
    void set_kind_lookup(std::function<EntryCellKind(int)> lookup) {
        kind_lookup_ = std::move(lookup);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    // Wave 23: a click in the outline triangle toggles the row.
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

  private:
    bool entry_mode_ = true;
    int hierarchy_column_ = -1;
    OutlineLevelLookup level_lookup_;
    ChildrenLookup children_lookup_;
    CollapsedLookup collapsed_lookup_;
    ToggleHandler toggle_handler_;
    std::function<EntryCellKind(int)> kind_lookup_;
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
