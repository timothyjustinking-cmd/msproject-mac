// app_qt/sheet_table_model.h -- a QAbstractTableModel over core::sheet::SheetModel.
//
// The Qt model is deliberately a thin adapter: it owns no data, keeps no second
// selection and formats nothing.  Every cell string, every style flag and every
// header title comes from SheetModel, which is what keeps the three OSes'
// pixels identical (docs/ui-qt-port.md section 2.3).
//
// Rows and columns are the DISPLAYED ones: visible_row_count()/visible_row() and
// visible_column_count()/visible_column(), so a collapse removes rows from the
// table exactly as it removes bars from the Gantt.
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

    // Displayed row -> RecordSet row, displayed column -> model column;
    // core::sheet::kNoIndex for an out-of-range coordinate.
    std::size_t record_row(int display_row) const;
    std::size_t model_column(int display_column) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

  private:
    core::sheet::SheetModel* sheet_ = nullptr;
};

}  // namespace msqt
