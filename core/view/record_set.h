/*
 * record_set.h -- the shared tabular model that every view surface reads.
 *
 * This is the convergence point for the three producers we have or will have:
 *
 *   core/mpp reader   -> mpp::Document (v0: eight fixed Task fields)
 *   MSPDI XML         -> core::io::MspdiDocument (RecordSets, this header)
 *   future .mpp writer-> reads the same RecordSets
 *
 * A RecordSet is COLUMNAR: one std::vector<Value> per field, not a vector of
 * rows.  That makes the two dominant operations cheap and allocation-light:
 *
 *   * table rendering / CSV export walk one column at a time;
 *   * MSPDI import appends one value per column as it walks a <Task> element.
 *
 * Fields are addressed by PjField id (core/model/field_table.h), so a view
 * definition never needs to know how a field is stored.  A RecordSet can carry
 * any subset of the 1,444 Task fields; a table simply reads the ids it names and
 * gets Value::Null for a column the producer did not supply.
 *
 * UIDs are kept separately from the values because UniqueID is identity, not a
 * display field: it is how links and assignments refer to a task.
 */

#ifndef CORE_VIEW_RECORD_SET_H
#define CORE_VIEW_RECORD_SET_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "field_table.h"  /* core::model::RecordType */
#include "value.h"

namespace core::view {

class RecordSet {
  public:
    RecordSet() = default;
    explicit RecordSet(core::model::RecordType record_type) : record_type_(record_type) {}

    core::model::RecordType record_type() const noexcept { return record_type_; }
    void set_record_type(core::model::RecordType type) noexcept { record_type_ = type; }

    std::size_t row_count() const noexcept { return uids_.size(); }
    std::size_t field_count() const noexcept { return fields_.size(); }
    bool empty() const noexcept { return uids_.empty(); }

    const std::vector<FieldId>& fields() const noexcept { return fields_; }
    FieldId field_id_at(std::size_t column) const;
    int column_of(FieldId field) const noexcept;  // -1 when absent
    bool has_field(FieldId field) const noexcept { return column_of(field) >= 0; }

    /*
     * Adds the field if absent and returns its column index.  The new column is
     * filled with Value::Null for every existing row.  Id 0 is not a field and
     * is rejected (returns fields_.size(), changed nothing).
     */
    std::size_t add_field(FieldId field);

    /* Adds a row with Null in every column; returns the new row index. */
    std::size_t add_row(std::uint32_t uid);

    std::uint32_t uid(std::size_t row) const;
    int row_of_uid(std::uint32_t uid) const noexcept;  // -1 when absent

    /* Null pointer when either index is out of range. */
    const Value* at(std::size_t row, std::size_t column) const noexcept;
    const Value* get(std::size_t row, FieldId field) const noexcept;

    /* Silently ignores out-of-range indices; set_field adds the field if it is
     * absent, so importers do not have to pre-declare the schema. */
    void set(std::size_t row, std::size_t column, Value value);
    void set_field(std::size_t row, FieldId field, Value value);

    void reserve(std::size_t rows, std::size_t columns);
    void clear() noexcept;

    /* A stable token for diagnostics/tests: the schema in field order. */
    const std::vector<FieldId>& schema() const noexcept { return fields_; }

  private:
    core::model::RecordType record_type_ = core::model::RecordType::Unknown;
    std::vector<FieldId> fields_;           // column -> field id
    std::vector<std::uint32_t> uids_;       // row -> UniqueID
    std::vector<std::vector<Value>> cells_; // column -> row -> value
};

}  // namespace core::view

#endif  // CORE_VIEW_RECORD_SET_H
