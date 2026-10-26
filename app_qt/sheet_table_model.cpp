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
#include "layout_model.h"  // app::task_field_id (the stored ID field)
#include "resource_tables.h"  // core::view resource field ids (wave 31)
#include "shell_style.h"
#include "view_style.h"  // app::text_style_for_row (wave 26)

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

QString SheetTableModel::row_number_text(int display_row) const {
    if (sheet_ == nullptr || display_row < 0) {
        return {};
    }
    const std::size_t row = record_row(display_row);
    if (row == core::sheet::kNoIndex) {
        return {};
    }
    // Project's ID column is the frozen gutter.  It is read from the RecordSet
    // by field, not by looking for a visible column: the stored table for a
    // loaded file does not carry ID as a scrolling column.  W31-3.6: a Resource
    // record set stores its ID under Resource.ID (0x0c400000); the task field
    // is absent there, so the task id would fall back to the display ordinal.
    const core::view::FieldId id_field =
        sheet_->record_type() == core::model::RecordType::Resource
            ? core::view::kResourceIdField
            : app::task_field_id("ID");
    const std::string text = sheet_->field_text(row, id_field);
    if (!text.empty()) {
        return QString::fromStdString(text);
    }
    return QString::number(display_row + 1);
}

// Wave 31 (W31-3.6): the Indicators column of the Resource Sheet.  In generic
// mode the display ordinal IS the visible ordinal, so the field lookup is the
// same walk visible_column_for_field() does; a task sheet never matches (the
// resource field id is not in its schema).
int SheetTableModel::indicators_column() const {
    if (sheet_ == nullptr || !generic_ ||
        sheet_->record_type() != core::model::RecordType::Resource) {
        return -1;
    }
    const std::size_t count = sheet_->visible_column_count();
    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
        const std::size_t column = sheet_->visible_column(ordinal);
        if (column != core::sheet::kNoIndex &&
            sheet_->column(column).field == core::view::kResourceIndicatorsField) {
            return static_cast<int>(ordinal);
        }
    }
    return -1;
}

bool SheetTableModel::row_overallocated(int display_row) const {
    return resource_overallocated(record_row(display_row));
}

bool SheetTableModel::resource_overallocated(std::size_t row) const {
    if (sheet_ == nullptr || row == core::sheet::kNoIndex ||
        sheet_->record_type() != core::model::RecordType::Resource) {
        return false;
    }
    const core::view::Value* value =
        sheet_->records().get(row, core::view::kResourceOverallocatedField);
    return value != nullptr && value->kind == core::view::ValueKind::Bool &&
           value->boolean;
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
        // A cell with no SheetModel column: Indicators and Task Mode (painted by
        // the delegate) and the Add New Column stub.  Since wave 22 Predecessors,
        // Successors and Resource Names are NOT in this set -- PlanDocument fills
        // their derived text into the RecordSet, so they have a real column and
        // reach the DisplayRole path below like any other text cell.
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
            // Wave 26 (W26-3.1): the FILE's text style for this row.  Project
            // stores a font family/size and bold/italic/underline/strike per
            // Text Styles item (gantt_style.h); the selected item follows the
            // measured precedence in app/view_style.h (project summary,
            // summary, milestone, critical, noncritical).  Without a decoded
            // record the wave-24 stock fallback (upright, summary bold) stands.
            const msproject::viewstate::TextStyle* file_style = row_text_style(row);
            if (file_style != nullptr) {
                if (!file_style->font_name.empty()) {
                    font.setFamily(QString::fromStdString(file_style->font_name));
                }
                if (file_style->point_size > 0) {
                    font.setPointSize(file_style->point_size);
                }
                font.setBold(file_style->bold);
                font.setItalic(file_style->italic);
                font.setUnderline(file_style->underline);
                font.setStrikeOut(file_style->strikethrough);
            }
            return font;
        }
        case Qt::ForegroundRole: {
            // Wave 26 (W26-3.1): the file's stored colour, when the record
            // names one.  The stock fallback stays no colour: wave 24 measured
            // that a NEW blank project draws critical rows in ordinary text.
            const msproject::viewstate::TextStyle* file_style = row_text_style(row);
            if (file_style != nullptr && file_style->has_colour) {
                return QColor(file_style->colour.red, file_style->colour.green,
                              file_style->colour.blue);
            }
            return {};
        }
        case Qt::BackgroundRole: {
            // Wave 29 (W29-1.2): the view's stored per-cell fill wins over the
            // row's Text Styles background.  The lookup key is the row's
            // UniqueID and the DISPLAY column's stored field id; an automatic
            // entry leaves `set == false`, so the Text Styles background (if
            // any) still applies, which is Project's "Automatic" meaning.
            // Generic presentations (resource/usage sheets) show other record
            // types with their own uids, so the task table's formats do not
            // apply there.
            if (!generic_ && cell_formats_ != nullptr) {
                const std::size_t display_column = static_cast<std::size_t>(index.column());
                if (display_column < display_columns_.size()) {
                    // W29-1.2: key on the STORED field id.  `field` is the
                    // mapped model field (the table's DurationText column shows
                    // Task.Duration), while the record stores the `214/CTable`
                    // id the column record itself carries.
                    const DisplayColumn& shown = display_columns_[display_column];
                    const core::view::FieldId key =
                        shown.table_field != 0 ? shown.table_field : shown.field;
                    const app::CellFill fill =
                        app::cell_fill_for(*cell_formats_, sheet_->row_uid(row), key);
                    if (fill.set) {
                        return QColor(fill.colour.red, fill.colour.green, fill.colour.blue);
                    }
                }
            }
            const msproject::viewstate::TextStyle* file_style = row_text_style(row);
            if (file_style != nullptr && file_style->has_background) {
                return QColor(file_style->background.red, file_style->background.green,
                              file_style->background.blue);
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
                // W31-3.6: Project's Indicators column carries no caption; its
                // header is the (i) badge alone.  SheetModel::column_title()
                // would otherwise fill in the field's generic "Indicators".
                if (sheet_->column(column).field ==
                    core::view::kResourceIndicatorsField) {
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
    // Wave 23 (review finding): the synthetic project-summary row (uid 0) has
    // no file row behind it, and sync_plan_from_records deliberately skips it,
    // so an edit there would be silently dropped.  Refuse the edit up front.
    const std::size_t edited_row = record_row(index.row());
    if (edited_row != core::sheet::kNoIndex && sheet_->row_uid(edited_row) == 0) {
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
    // Wave 25 (W25-1/S1): the uid-0 refusal is app::begin_cell_edit's, so the
    // macOS and Qt shells share one edit-entry policy.
    std::string edit_error;
    if (!app::begin_cell_edit(*sheet_, row, column, edit_error)) {
        report_commit(false, edit_error);
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

// Wave 26 (W26-3.1): the file's Text Styles item for a RecordSet row.  The row
// facts come from the SheetModel (the same is_summary/is_milestone/is_critical
// the stock fallback uses); uid 0 is the synthetic project row, which Project
// styles with the Project Summary item.  The category precedence itself lives
// in app/view_style.h, so the headless app test pins it.
//
// Wave 31 (W31-3.6): a Resource record set reads the Resource Sheet view's own
// record instead.  Project paints the stored/computed overallocated flag with
// slot 3 (bold RGB 165,0,0) and every other resource with slot 2; the task
// sheet's summary/milestone categories do not exist on a resource row.
const msproject::viewstate::TextStyle* SheetTableModel::row_text_style(
    std::size_t row) const {
    if (sheet_ == nullptr) {
        return nullptr;
    }
    if (sheet_->record_type() == core::model::RecordType::Resource) {
        if (resource_style_ == nullptr) {
            return nullptr;
        }
        return app::text_style_for_resource_row(*resource_style_,
                                                resource_overallocated(row));
    }
    if (style_ == nullptr) {
        return nullptr;
    }
    const bool project_row = sheet_->row_uid(row) == 0;
    const bool summary = !project_row && sheet_->is_summary(row);
    const bool milestone = !project_row && !summary && sheet_->is_milestone(row);
    const bool critical = sheet_->is_critical(row);
    return app::text_style_for_row(*style_, project_row, summary, milestone, critical);
}

}  // namespace msqt
