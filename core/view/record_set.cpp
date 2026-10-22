#include "record_set.h"

#include <algorithm>

namespace core::view {

FieldId RecordSet::field_id_at(std::size_t column) const {
    return column < fields_.size() ? fields_[column] : 0;
}

int RecordSet::column_of(FieldId field) const noexcept {
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        if (fields_[i] == field) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::size_t RecordSet::add_field(FieldId field) {
    if (field == 0) {
        return fields_.size();
    }
    const int existing = column_of(field);
    if (existing >= 0) {
        return static_cast<std::size_t>(existing);
    }
    fields_.push_back(field);
    cells_.emplace_back(uids_.size(), Value{});
    return fields_.size() - 1;
}

std::size_t RecordSet::add_row(std::uint32_t unique_id) {
    uids_.push_back(unique_id);
    for (auto& column : cells_) {
        column.emplace_back(Value{});
    }
    return uids_.size() - 1;
}

std::uint32_t RecordSet::uid(std::size_t row) const {
    return row < uids_.size() ? uids_[row] : 0;
}

int RecordSet::row_of_uid(std::uint32_t unique_id) const noexcept {
    for (std::size_t i = 0; i < uids_.size(); ++i) {
        if (uids_[i] == unique_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const Value* RecordSet::at(std::size_t row, std::size_t column) const noexcept {
    if (row >= uids_.size() || column >= cells_.size()) {
        return nullptr;
    }
    return &cells_[column][row];
}

const Value* RecordSet::get(std::size_t row, FieldId field) const noexcept {
    const int column = column_of(field);
    if (column < 0) {
        return nullptr;
    }
    return at(row, static_cast<std::size_t>(column));
}

void RecordSet::set(std::size_t row, std::size_t column, Value value) {
    if (row >= uids_.size() || column >= cells_.size()) {
        return;
    }
    cells_[column][row] = std::move(value);
}

void RecordSet::set_field(std::size_t row, FieldId field, Value value) {
    if (row >= uids_.size()) {
        return;
    }
    const std::size_t column = add_field(field);
    if (column >= cells_.size()) {
        return;
    }
    cells_[column][row] = std::move(value);
}

void RecordSet::reserve(std::size_t rows, std::size_t columns) {
    uids_.reserve(rows);
    fields_.reserve(columns);
    cells_.reserve(columns);
}

void RecordSet::clear() noexcept {
    fields_.clear();
    uids_.clear();
    cells_.clear();
}

}  // namespace core::view
