// app_qt/sheet_table_model.h -- the Entry table over core::sheet::SheetModel.
//
// Q1 adapted only SheetModel's own seven columns.  Q2 presents the measured MS
// Project Entry table (entry_columns.h, docs/parity.md section 2.2):
//
//   display column 0 : the row-number gutter (60 px, the sheet's left border)
//   display 1..9     : Indicators, Task Mode, Task Name, Duration, Start,
//                      Finish, Predecessors, Resource Names, Add New Column
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

// Display column 0 is the row-number gutter; the Entry columns start at 1.
inline constexpr int kGutterColumn = 0;

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
    // Displayed column -> SheetModel column; kNoIndex for the gutter and for a
    // synthetic column (Indicators, Task Mode, Predecessors, Resources, stub).
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
