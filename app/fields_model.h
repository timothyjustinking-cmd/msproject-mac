// app/fields_model.h -- the task sheet's custom-field columns (wave 12).
//
// THREE KINDS OF COLUMN, ONE COLUMN SET
//   * the seven identity/plan columns (app::default_sheet_columns());
//   * the six STORED custom fields the .mpp reader decodes through W48's
//     measured storage (Text1/Number1/Number2/Date1/Duration1/Flag1).  Their
//     values come from app::make_record_set(), which writes
//     mpp::CustomFieldValue into the RecordSet, so the sheet formats them with
//     the same prefs-driven date/duration rendering it uses for Start/Duration.
//     Read-only: nothing in this wave writes a custom field.
//   * FORMULA columns.  `114/Props` stores the project's own formula text with
//     the field references rewritten to `[MSPJ<decimal>]` tokens (measured,
//     docs/analysis/oracle/w10-oracle.md section 2; decoded by core/props W50).
//     core::formula parses and evaluates that text against the row, so the
//     computed value matches the formula stored in the file.  The two demo
//     columns from wave 11 are kept for a file without formulas (or for a field
//     the file does not define) -- they are the read-only substitute, never a
//     replacement for the file's own definition when there is one.
//
// WHY THE FIELD REGISTRY, NOT HARD-CODED IDS
//   The stored columns are built by asking mpp::custom_field_id(kind, index) (the
//   shared registry core/mpp/field_value.h documents) and their captions come
//   from the descriptor table (core::view::field_title).  A field the table
//   cannot name still gets a column -- titled "0x0b40..." -- rather than being
//   dropped silently.
//
// WHAT THIS FILE DOES NOT DO
//   It never writes a value back to a file.  Stored custom fields and formulas
//   are read-only columns: the save path writes task starts and (wave 12)
//   structural add/delete only.  A formula whose text does not parse is reported
//   in FormulaColumnSet::warnings and skipped; it is never evaluated as a partial
//   guess.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "columns.h"
#include "layout_model.h"
#include "prefs.h"
#include "sheet.h"
#include "view_model.h"

namespace app {

// ---------------------------------------------------------------------------
// Stored custom-field columns
// ---------------------------------------------------------------------------

// One column for a custom field the reader decodes: the PjField id (a real Task
// field, so core/sheet addresses it like any other), the CellKind the value
// renders as, and the column width in characters.
struct StoredFieldColumn {
    core::view::FieldId field = 0;
    core::sheet::CellKind kind = core::sheet::CellKind::Auto;
    core::view::ColumnAlign align = core::view::ColumnAlign::Auto;
    int width = 0;
};

// Text1, Number1, Number2, Date1, Duration1, Flag1 -- the six families whose
// storage W48 measured (Text/Number/Date/Duration in VarMeta, Flag1 in the
// FixedMeta entry tail).  A field whose registry id resolves to 0 is absent from
// this list rather than pointed at a guess.  Static (process lifetime).
const std::vector<StoredFieldColumn>& stored_field_columns();

// True when `field` is one of stored_field_columns()' ids.
bool is_stored_field_column(core::view::FieldId field) noexcept;

// The default visibility of a stored/formula column: the six stored columns use
// Preferences::stored_custom_fields (off), a demo formula column uses its own
// preference flag, and a file formula column is visible.  `column_visible()` in
// app/prefs.h then applies any per-column override on top.
bool custom_column_default_visible(core::view::FieldId field, const Preferences& prefs) noexcept;

// ---------------------------------------------------------------------------
// Formula columns
// ---------------------------------------------------------------------------

// One formula column: a real PjField id (core::formula::ComputedColumns refuses
// anything else), the header caption, and the formula text.  `from_file` is true
// when the formula came from the project's own `114/Props`.
struct FormulaColumn {
    core::view::FieldId field = 0;
    std::string title;
    std::string formula;
    bool from_file = false;
};

struct FormulaColumnSet {
    std::vector<FormulaColumn> columns;
    // One line per formula that could not be used (a parse error, an unknown
    // field, a duplicate definition).  The shell puts these in the status line;
    // the columns that did work stay.
    std::vector<std::string> warnings;
};

// The formulas a `114/Props` stream defines: core/props extracts the blocks and
// this maps each to its PjField id and keeps the formula text verbatim (the
// parser accepts the `[MSPJ<decimal>]` tokens as they are stored).  An empty
// blob is "this file has no Props stream", not an error.
FormulaColumnSet read_file_formulas(const std::vector<std::uint8_t>& props_bytes);

// The sheet's formula column list: the file's formulas (in stored order) first,
// then the app's two demo columns for any field the file does not define.  This
// is what the View > Columns chooser, ComputedColumns and the sheet all iterate,
// so the column indices are the same everywhere.
FormulaColumnSet formula_columns(const FormulaColumnSet& file_formulas);

// The task sheet's complete column set: the seven default columns, then the
// stored custom-field columns no formula defines, then the formula columns.  Every
// custom/formula column is read-only.  The order is deterministic for a given
// formula set, which is what lets the View > Columns tags be model indices.
std::vector<core::sheet::SheetColumn> task_editor_columns(const FormulaColumnSet& formulas);

// Registers every formula column in `out` (a fresh ComputedColumns).  False with
// `error` set when any formula does not parse; the set then holds only the
// columns registered before the failure, and the caller is expected to report it.
bool build_computed_columns(core::formula::ComputedColumns& out,
                            const std::vector<FormulaColumn>& columns, std::string& error);

// Marks every formula column of `sheet` read-only (the belt-and-braces twin of
// task_editor_columns(), for a caller that built a plain column list).
void apply_formula_read_only(core::sheet::SheetModel& sheet,
                             const std::vector<FormulaColumn>& columns);

// Applies the stored/formula column visibility from the preferences: each stored
// column defaults to Preferences::stored_custom_fields, each formula column to
// custom_column_default_visible(), and a recorded override wins.  Ordinary
// columns are left alone.
void apply_column_visibility(core::sheet::SheetModel& sheet, const FormulaColumnSet& formulas,
                             const Preferences& prefs);

// True when the shell should persist this column's visibility (a stored custom
// field or a formula column).  The identity columns are not preference state.
bool is_custom_column(const FormulaColumnSet& formulas, core::view::FieldId field) noexcept;

}  // namespace app
