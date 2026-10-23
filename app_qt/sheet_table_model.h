// app_qt/sheet_table_model.h -- the Entry table over core::sheet::SheetModel.
//
// Q1 adapted only SheetModel's own seven columns.  Q2 presents the measured MS
// Project Entry table (entry_columns.h, docs/parity.md section 2.2):
//
//   the nine Entry columns: Indicators, Task Mode, Task Name, Duration, Start,
//   Finish, Predecessors, Resource Names, Add New Column.
//
// The row-number gutter is NOT a model column: it is the view's FROZEN vertical
// header (SheetRowHeader in sheet_views.h), so it does not scroll horizontally
// with the columns the way a display column 0 did.  The model therefore exposes
// exactly the nine Entry columns, and display column N is entry column N.
//
// The four data columns map onto SheetModel columns by FIELD ID, so the model
// never invents a value: Name/Duration/Start/Finish come from
// SheetModel::cell_text exactly as Q1's adapter did.  Indicators and Task Mode
// are painted by the delegate; Predecessors/Resources are not in app/'s
// RecordSet (the portable model carries links/assignments separately), so their
// cells are empty -- the same as the reference fixture, which has neither.
//
// Rows are the DISPLAYED ones: visible_row_count()/visible_row(), so a collapse
// removes rows from the table exactly as it removes bars from the Gantt.
//
// No Q_OBJECT: the adapter defines no signals or slots, so CMake needs no AUTOMOC.
#pragma once

#include <QAbstractTableModel>

#include <cstddef>

#include "sheet.h"

namespace msqt {

class SheetTableModel final : public QAbstractTableModel {
  public:
    explicit SheetTableModel(QObject* parent = nullptr);

    // Points the adapter at `model` (nullptr = empty).  Resets the view.
    void set_sheet(core::sheet::SheetModel* model);
    // Re-reads rows/columns after the sheet changed (a collapse, an edit).
    void refresh();

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

  private:
    core::sheet::SheetModel* sheet_ = nullptr;
};

}  // namespace msqt
