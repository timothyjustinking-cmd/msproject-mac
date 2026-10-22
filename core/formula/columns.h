/*
 * columns.h -- computed columns: a named formula bound to a PjField id, and the
 * ONE accessor every surface reads through.
 *
 * The wave-10 gate says the sheet and the reports must not diverge.  So there
 * is exactly one function that answers "what is this field's value for this
 * row": ComputedColumns::value_of_field(records, row, field).  It returns the
 * computed result when a formula defines that field and the RecordSet value
 * otherwise, recursing through formulas that reference other computed fields.
 * A sheet built from the RecordSet after evaluate() and a report that calls
 * value_of_field() see the same number by construction.
 *
 * A computed column TARGETS A REAL PjField id -- it does not invent a field.
 * That is deliberate: the descriptor table gives the field its name, kind and
 * storage, and the sheet's editing/undo path already addresses fields by id.
 * The id is validated at add() time.
 *
 * Two ways to use it:
 *   * batch   evaluate(records) writes every formula's value into the RecordSet
 *             (adding the column when absent), then the sheet shows it.
 *   * single  value_of_field() for a report/row without materialising a column.
 *
 * add_to_sheet() only uses core::sheet::SheetModel's existing public API --
 * records(), column(), column_count(), set_columns(), refresh() -- because
 * core/sheet is another stream's file and this wave does not modify it.
 */

#ifndef CORE_FORMULA_COLUMNS_H
#define CORE_FORMULA_COLUMNS_H

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "eval.h"
#include "expr.h"
#include "record_set.h"
#include "sheet.h"
#include "value.h"

namespace core::formula {

struct ComputedColumn {
    core::view::FieldId field_id = 0;
    std::string name;
    std::string formula;
};

class ComputedColumns {
  public:
    ComputedColumns() = default;

    /*
     * Parses `column.formula` and registers it.  Fails (leaving the set
     * unchanged) for a zero/unknown field id, a field already defined by a
     * computed column, or a formula that does not parse -- `error` is the
     * parser's positioned message.
     */
    bool add(const ComputedColumn& column, std::string& error);
    bool add(core::view::FieldId field, std::string name, std::string_view formula,
             std::string& error);

    std::size_t size() const noexcept { return entries_.size(); }
    /* A static empty column for an out-of-range index. */
    const ComputedColumn& column(std::size_t index) const noexcept;
    /* The parsed formula for an index, or nullptr. */
    const Expr* expression(std::size_t index) const noexcept;
    /* -1 when no computed column defines the field. */
    int index_of(core::view::FieldId field) const noexcept;

    /*
     * THE accessor.  See the header comment.  Null when the row is out of
     * range, the field is absent and not computed, or a formula recursion
     * limit was hit.
     */
    core::view::Value value_of_field(const core::view::RecordSet& records, std::size_t row,
                                     core::view::FieldId field) const;

    /*
     * Evaluates every computed column over every row, writing the result into
     * `records` (the field is added when absent).  Returns the number of cell
     * values written.
     */
    std::size_t evaluate(core::view::RecordSet& records) const;

    /*
     * Evaluates and adds a computed column to a sheet through SheetModel's
     * public API only (records, column, column_count, set_columns, refresh).
     * `title` is the column header; the formula behaves exactly as add().
     * This method does not mutate `*this` (it works on a copy), so a caller
     * that also wants the formula retained should call add() first.
     */
    bool add_to_sheet(core::sheet::SheetModel& sheet, core::view::FieldId field, std::string title,
                      std::string_view formula, std::string& error) const;

    const core::sheet::SheetFormat& format() const noexcept { return format_; }
    void set_format(core::sheet::SheetFormat format) { format_ = std::move(format); }

    int max_depth() const noexcept { return max_depth_; }
    void set_max_depth(int depth) noexcept { max_depth_ = depth < 1 ? 1 : depth; }

  private:
    struct Entry {
        ComputedColumn column;
        Expr ast;
    };

    core::view::Value value_at(const core::view::RecordSet& records, std::size_t row,
                               core::view::FieldId field, int depth) const;

    std::vector<Entry> entries_;
    core::sheet::SheetFormat format_{};
    int max_depth_ = 32;
};

}  // namespace core::formula

#endif  // CORE_FORMULA_COLUMNS_H
