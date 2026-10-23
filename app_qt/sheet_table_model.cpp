// app_qt/sheet_table_model.cpp -- see sheet_table_model.h.

#include "sheet_table_model.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QString>
#include <QVariant>

#include <cstddef>
#include <utility>

#include "entry_columns.h"
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
        case core::sheet::CellAlign::Right:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        case core::sheet::CellAlign::Center:
            return static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter);
        case core::sheet::CellAlign::Left:
            break;
    }
    return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
}

// Generic columns carry core/view's ColumnAlign; Auto defers to the cell style.
int alignment_for(core::view::ColumnAlign align) {
    switch (align) {
        case core::view::ColumnAlign::Right:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        case core::view::ColumnAlign::Center:
            return static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter);
        case core::view::ColumnAlign::Left:
        case core::view::ColumnAlign::Auto:
            break;
    }
    return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
}

}  // namespace

SheetTableModel::SheetTableModel(QObject* parent)
    : QAbstractTableModel(parent), display_columns_(entry_display_columns()) {}

void SheetTableModel::set_display_columns(std::vector<DisplayColumn> columns) {
    beginResetModel();
    display_columns_ = std::move(columns);
    endResetModel();
}

int SheetTableModel::display_column_for_field(core::view::FieldId field) const {
    if (field == 0) {
        return -1;
    }
    for (std::size_t index = 0; index < display_columns_.size(); ++index) {
        if (display_columns_[index].field == field) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

EntryCellKind SheetTableModel::display_kind(int display_column) const {
    if (display_column < 0 || static_cast<std::size_t>(display_column) >= display_columns_.size()) {
        return EntryCellKind::data;
    }
    return display_columns_[static_cast<std::size_t>(display_column)].kind;
}

void SheetTableModel::set_sheet(core::sheet::SheetModel* model) {
    beginResetModel();
    sheet_ = model;
    display_rows_.clear();
    filter_active_ = false;
    endResetModel();
}

void SheetTableModel::refresh() {
    beginResetModel();
    endResetModel();
}

void SheetTableModel::set_generic(bool generic) {
    if (generic_ == generic) {
        return;
    }
    beginResetModel();
    generic_ = generic;
    endResetModel();
}

void SheetTableModel::set_display_rows(std::vector<std::size_t> rows, bool active) {
    beginResetModel();
    display_rows_ = std::move(rows);
    filter_active_ = active;
    endResetModel();
}

void SheetTableModel::set_commit_handler(CommitHandler handler) {
    commit_handler_ = std::move(handler);
}

std::size_t SheetTableModel::record_row(int display_row) const {
    if (sheet_ == nullptr || display_row < 0) {
        return core::sheet::kNoIndex;
    }
    if (filter_active_) {
        if (static_cast<std::size_t>(display_row) >= display_rows_.size()) {
            return core::sheet::kNoIndex;
        }
        return display_rows_[static_cast<std::size_t>(display_row)];
    }
    return sheet_->visible_row(static_cast<std::size_t>(display_row));
}

std::size_t SheetTableModel::visible_column_for_field(core::view::FieldId field) const {
    if (sheet_ == nullptr || field == 0) {
        return core::sheet::kNoIndex;
    }
    const std::size_t count = sheet_->visible_column_count();
    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
        const std::size_t column = sheet_->visible_column(ordinal);
        if (column != core::sheet::kNoIndex && sheet_->column(column).field == field) {
            return column;
        }
    }
    return core::sheet::kNoIndex;
}

std::size_t SheetTableModel::column_for_display(int display_column) const {
    if (sheet_ == nullptr || display_column < 0) {
        return core::sheet::kNoIndex;
    }
    if (generic_) {
        return sheet_->visible_column(static_cast<std::size_t>(display_column));
    }
    return model_column(display_column);
}

std::size_t SheetTableModel::model_column(int display_column) const {
    if (sheet_ == nullptr || display_column < 0) {
        return core::sheet::kNoIndex;
    }
    if (static_cast<std::size_t>(display_column) >= display_columns_.size()) {
        return core::sheet::kNoIndex;
    }
    // A synthetic column (field 0: Indicators, Task Mode, Add New Column) has no
    // model column; the delegate paints it or leaves it blank.
    return visible_column_for_field(
        display_columns_[static_cast<std::size_t>(display_column)].field);
}

int SheetTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    if (sheet_ == nullptr) {
        // A NEW project shows the product's first empty row (Task Mode cell and
        // (i) gutter number); there is no RecordSet row behind it.
        return generic_ ? 0 : 1;
    }
    if (filter_active_) {
        return static_cast<int>(display_rows_.size());
    }
    return static_cast<int>(sheet_->visible_row_count());
}

int SheetTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    if (generic_) {
        return sheet_ != nullptr ? static_cast<int>(sheet_->visible_column_count()) : 0;
    }
    // The row-number gutter is the view's frozen vertical header, not a column.
    // An EMPTY project still presents the nine Entry columns: the caption band
    // must exist with its titles (wave 20), and only the row count is zero.  A
    // LOADED file presents its own stored table (wave 21).
    return static_cast<int>(display_columns_.size());
}

QVariant SheetTableModel::data(const QModelIndex& index, int role) const {
    if (sheet_ == nullptr || !index.isValid()) {
        return {};
    }
    const std::size_t row = record_row(index.row());
    const std::size_t column = column_for_display(index.column());
    if (row == core::sheet::kNoIndex || column == core::sheet::kNoIndex) {
        // Indicators, Task Mode, Predecessors, Resource Names and the Add New
        // Column stub hold no SheetModel value; the delegate paints the icons.
        return {};
    }
    switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            // The one formatter: SheetModel formats the value exactly once
            // (core/sheet/sheet.h), so the sheet and the Gantt label agree.  The
            // duration-style preference is applied by app::cell_text on top of
            // that same rendering (app/prefs.h); the date style is already in
            // the sheet's SheetFormat.
            if (preferences_ != nullptr) {
                return QString::fromStdString(app::cell_text(*sheet_, row, column, *preferences_));
            }
            return QString::fromStdString(sheet_->cell_text(row, column));
        case Qt::TextAlignmentRole:
            if (generic_) {
                return alignment_for(sheet_->column(column).align);
            }
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
    if (orientation == Qt::Horizontal) {
        if (role == Qt::DisplayRole) {
            if (generic_) {
                if (sheet_ == nullptr || section < 0) {
                    return {};
                }
                const std::size_t column =
                    sheet_->visible_column(static_cast<std::size_t>(section));
                if (column == core::sheet::kNoIndex) {
                    return {};
                }
                return QString::fromStdString(sheet_->column_title(column));
            }
            // An empty project keeps the Entry captions: the header band is part
            // of the shell's structure, not of the loaded document.  A loaded
            // file's captions are its stored `214/CTable` titles (wave 21).
            if (section < 0 || static_cast<std::size_t>(section) >= display_columns_.size()) {
                return {};
            }
            return QString::fromStdString(
                display_columns_[static_cast<std::size_t>(section)].title);
        }
        if (role == Qt::TextAlignmentRole) {
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
        return {};
    }
    if (sheet_ == nullptr) {
        return {};
    }
    // The row header (SheetRowHeader) paints the numbers itself; this is the
    // fallback the base class would use.
    if (role == Qt::DisplayRole) {
        return section + 1;
    }
    return {};
}

Qt::ItemFlags SheetTableModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags result = QAbstractTableModel::flags(index);
    if (sheet_ == nullptr || !index.isValid() || generic_) {
        return result;
    }
    // Read the column's declared read-only bit; NEVER call begin_edit() here.
    // A view asks for flags while painting and while deciding whether to open an
    // editor, and a probe that opened a session would fight the real one.
    const std::size_t column = column_for_display(index.column());
    if (column != core::sheet::kNoIndex && !sheet_->column_read_only(column)) {
        result |= Qt::ItemIsEditable;
    }
    return result;
}

bool SheetTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || sheet_ == nullptr || !index.isValid()) {
        return false;
    }
    const std::size_t row = record_row(index.row());
    const std::size_t column = column_for_display(index.column());
    if (row == core::sheet::kNoIndex || column == core::sheet::kNoIndex) {
        report_commit(false, "this cell is not editable");
        return false;
    }
    // Move the sheet's own selection to the edited cell first: the selection is
    // the single source of truth the Gantt and the Save diff derive from, and a
    // double-click on another row must move it.
    sheet_->select_cell(row, column);
    if (!sheet_->begin_edit(row, column)) {
        report_commit(false, "this column is read-only");
        return false;
    }
    sheet_->set_edit_text(value.toString().toStdString());
    const core::sheet::EditOutcome outcome = sheet_->commit_edit(false);
    if (!outcome.ok) {
        // A rejected value is reported to the status line; the session is closed
        // here so the next edit starts clean.
        sheet_->cancel_edit();
        report_commit(false, outcome.error);
        emit dataChanged(index, index);
        return false;
    }
    const QModelIndex first = this->index(index.row(), 0);
    const QModelIndex last = this->index(index.row(), columnCount() - 1);
    emit dataChanged(first, last);
    report_commit(true, std::string());
    return true;
}

void SheetTableModel::report_commit(bool ok, const std::string& message) {
    if (commit_handler_) {
        commit_handler_(ok, message);
    }
}

}  // namespace msqt
