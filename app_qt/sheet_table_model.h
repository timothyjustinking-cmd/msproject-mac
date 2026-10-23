// app_qt/sheet_table_model.h -- the table view over core::sheet::SheetModel.
//
// TWO PRESENTATIONS, ONE ADAPTER
//   * ENTRY mode (the Task Sheet, wave 17/18): exactly the nine measured Entry
//     columns (entry_columns.h, docs/parity.md section 2.2).  Rows are the
//     DISPLAYED rows: visible_row_count()/visible_row(), so a collapse removes
//     rows from the table exactly as it removes bars from the Gantt, and the
//     wave-19 filter removes them from the display list without touching the
//     model.
//   * GENERIC mode (wave 19: the resource sheet and the two usage grids): the
//     sheet model's own visible columns, in its own order, titled by
//     SheetModel::column_title().  Read-only (app/view_model.h declares every
//     such column read-only).
//
// EDITING (wave 19)
//   setData() is the one edit path and it runs the shell's real edit model:
//   SheetModel::begin_edit -> set_edit_text -> commit_edit, i.e. the same
//   parser/undo/RecordSet write the macOS shell's keyboard path uses
//   (app/README.md "Editing").  flags() marks a cell editable exactly when its
//   model column is not declared read-only; it never opens a session (a view
//   asks for flags constantly), so it stays side-effect free.
//
// The row-number gutter is NOT a model column: it is the view's FROZEN vertical
// header (SheetRowHeader in sheet_views.h).  Display column N is Entry column N.
//
// No Q_OBJECT: the adapter defines no signals or slots (it emits the base
// class's dataChanged), so CMake needs no AUTOMOC.
#pragma once

#include <QAbstractTableModel>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "sheet.h"

namespace msqt {

class SheetTableModel final : public QAbstractTableModel {
  public:
    // Called after a setData() attempt: ok, and the message to show ("" when ok).
    using CommitHandler = std::function<void(bool ok, const std::string& message)>;

    explicit SheetTableModel(QObject* parent = nullptr);

    // Points the adapter at `model` (nullptr = empty).  Resets the view.
    void set_sheet(core::sheet::SheetModel* model);
    // Re-reads rows/columns after the sheet changed (a collapse, an edit).
    void refresh();

    // Wave 19: which presentation this adapter serves.  `generic` shows the
    // sheet's visible columns (read-only); the default is the Entry table.
    void set_generic(bool generic);
    bool generic() const noexcept { return generic_; }

    // Wave 19: the filtered display list (RecordSet rows in display order).
    // `active == false` means "no filter" and falls back to the sheet's own
    // visible rows; an active filter with an empty list shows zero rows.
    void set_display_rows(std::vector<std::size_t> rows, bool active);

    void set_commit_handler(CommitHandler handler);

    core::sheet::SheetModel* sheet() const noexcept { return sheet_; }

    // Displayed row -> RecordSet row.  core::sheet::kNoIndex when out of range.
    std::size_t record_row(int display_row) const;
    // Displayed column -> SheetModel column; kNoIndex for a synthetic column
    // (Indicators, Task Mode, Predecessors, Resources, stub).
    std::size_t model_column(int display_column) const;

    // The SheetModel column showing `field`, or kNoIndex.
    std::size_t visible_column_for_field(core::view::FieldId field) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

  private:
    // The SheetModel column a display column maps to.  In generic mode the
    // display ordinal is the visible ordinal; in Entry mode the field id of the
    // Entry column resolves through visible_column_for_field().
    std::size_t column_for_display(int display_column) const;
    void report_commit(bool ok, const std::string& message);

    core::sheet::SheetModel* sheet_ = nullptr;
    bool generic_ = false;
    bool filter_active_ = false;
    std::vector<std::size_t> display_rows_;
    CommitHandler commit_handler_;
};

}  // namespace msqt
