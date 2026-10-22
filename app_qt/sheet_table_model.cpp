// app_qt/sheet_table_model.cpp -- see sheet_table_model.h.

#include "sheet_table_model.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QString>

#include <cstddef>

#include "shell_style.h"

namespace msqt {
namespace {

QFont cell_font() {
    QFont font = QApplication::font();
    font.setPointSize(kBodyPointSize);
    return font;
}

int alignment_for(core::sheet::CellAlign align) {
    switch (align) {
        case core::sheet::CellAlign::Right: return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        case core::sheet::CellAlign::Center: return static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter);
        case core::sheet::CellAlign::Left: break;
    }
    return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
}

}  // namespace

SheetTableModel::SheetTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void SheetTableModel::set_sheet(core::sheet::SheetModel* model) {
    beginResetModel();
    sheet_ = model;
    endResetModel();
}

void SheetTableModel::refresh() {
    beginResetModel();
    endResetModel();
}

std::size_t SheetTableModel::record_row(int display_row) const {
    if (sheet_ == nullptr || display_row < 0) {
        return core::sheet::kNoIndex;
    }
    return sheet_->visible_row(static_cast<std::size_t>(display_row));
}

std::size_t SheetTableModel::model_column(int display_column) const {
    if (sheet_ == nullptr || display_column < 0) {
        return core::sheet::kNoIndex;
    }
    return sheet_->visible_column(static_cast<std::size_t>(display_column));
}

int SheetTableModel::rowCount(const QModelIndex& parent) const {
    if (sheet_ == nullptr || parent.isValid()) {
        return 0;
    }
    return static_cast<int>(sheet_->visible_row_count());
}

int SheetTableModel::columnCount(const QModelIndex& parent) const {
    if (sheet_ == nullptr || parent.isValid()) {
        return 0;
    }
    return static_cast<int>(sheet_->visible_column_count());
}

QVariant SheetTableModel::data(const QModelIndex& index, int role) const {
    if (sheet_ == nullptr || !index.isValid()) {
        return {};
    }
    const std::size_t row = record_row(index.row());
    const std::size_t column = model_column(index.column());
    if (row == core::sheet::kNoIndex || column == core::sheet::kNoIndex) {
        return {};
    }
    switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            // The one formatter: SheetModel formats the value exactly once
            // (core/sheet/sheet.h), so the sheet and the Gantt label agree.
            return QString::fromStdString(sheet_->cell_text(row, column));
        case Qt::TextAlignmentRole:
            return alignment_for(sheet_->cell_style(row, column).align);
        case Qt::FontRole: {
            const core::sheet::CellStyle style = sheet_->cell_style(row, column);
            QFont font = cell_font();
            font.setBold(style.bold);
            return font;
        }
        case Qt::ForegroundRole: {
            const core::sheet::CellStyle style = sheet_->cell_style(row, column);
            if (style.red) {
                return QColor(0xb0, 0x1c, 0x1c);
            }
            return {};
        }
        default:
            return {};
    }
}

QVariant SheetTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (sheet_ == nullptr) {
        return {};
    }
    if (orientation == Qt::Horizontal) {
        if (role == Qt::DisplayRole) {
            if (section < 0) {
                return {};
            }
            const std::size_t column = sheet_->visible_column(static_cast<std::size_t>(section));
            if (column == core::sheet::kNoIndex) {
                return {};
            }
            return QString::fromStdString(sheet_->column_title(column));
        }
        if (role == Qt::TextAlignmentRole) {
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
        return {};
    }
    if (role == Qt::DisplayRole) {
        return section + 1;
    }
    return {};
}

}  // namespace msqt
