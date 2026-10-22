// app/fields_model.cpp -- see fields_model.h for the contract and the measured
// storage/formula facts this file relies on.
//
// No AppKit, no file I/O: the shell hands in the `114/Props` bytes and the
// RecordSet this module decorates, which is what lets app/app_fields_tests.cpp
// prove it on Windows.

#include "fields_model.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "expr.h"
#include "field_ref.h"
#include "field_table.h"
#include "field_value.h"
#include "props.h"

namespace app {
namespace {

using core::view::FieldId;
using core::sheet::CellKind;
using core::view::ColumnAlign;

// The descriptor table names the field ("Text1", "Number2", ...) or falls back
// to "0x0b40xxxx"; either way the caption is never invented here.
std::string registry_title(FieldId field) {
    return core::view::field_title(field, core::model::RecordType::Task);
}

bool is_task_field(FieldId field) noexcept {
    const core::model::FieldInfo* info = core::model::find_field_by_id(field);
    return info != nullptr &&
           info->record_type == static_cast<std::uint8_t>(core::model::RecordType::Task);
}

core::sheet::SheetColumn make_column(FieldId field, std::string title, int width,
                                     ColumnAlign align, CellKind kind, bool read_only) {
    core::sheet::SheetColumn column;
    column.field = field;
    column.title = std::move(title);
    column.width = width;
    column.align = align;
    column.kind = kind;
    column.visible = true;
    column.read_only = read_only;
    return column;
}

// Does any formula column target `field`?  A formula defines the field's value,
// so the stored column for the same field is omitted (one column per field, and
// the computed value is the one Project would show).
bool formula_covers(const std::vector<FormulaColumn>& columns, FieldId field) noexcept {
    for (const FormulaColumn& column : columns) {
        if (column.field == field) {
            return true;
        }
    }
    return false;
}

bool same_field(const FormulaColumn& column, FieldId field) noexcept {
    return column.field == field;
}

}  // namespace

// ---------------------------------------------------------------------------
// Stored custom-field columns
// ---------------------------------------------------------------------------

const std::vector<StoredFieldColumn>& stored_field_columns() {
    static const std::vector<StoredFieldColumn> columns = []() {
        std::vector<StoredFieldColumn> out;
        auto add = [&out](mpp::FieldKind kind, unsigned index, CellKind cell,
                          ColumnAlign align, int width) {
            StoredFieldColumn column;
            column.field = mpp::custom_field_id(kind, index);
            column.kind = cell;
            column.align = align;
            column.width = width;
            if (column.field != 0) {
                out.push_back(column);
            }
        };
        // The six families whose storage W48 measured.  Number1/Number2 and
        // Text1 are the ids the wave-10 oracle wrote; Flag1 is the one measured
        // FixedMeta tail bit, so Flag2..20 are deliberately absent.
        add(mpp::FieldKind::text, 1, CellKind::Text, ColumnAlign::Left, 12);
        add(mpp::FieldKind::number, 1, CellKind::Number, ColumnAlign::Right, 9);
        add(mpp::FieldKind::number, 2, CellKind::Number, ColumnAlign::Right, 9);
        add(mpp::FieldKind::date, 1, CellKind::Date, ColumnAlign::Left, 14);
        add(mpp::FieldKind::duration, 1, CellKind::Duration, ColumnAlign::Right, 10);
        add(mpp::FieldKind::flag, 1, CellKind::Bool, ColumnAlign::Center, 7);
        return out;
    }();
    return columns;
}

bool is_stored_field_column(FieldId field) noexcept {
    for (const StoredFieldColumn& column : stored_field_columns()) {
        if (column.field == field) {
            return true;
        }
    }
    return false;
}

bool custom_column_default_visible(FieldId field, const Preferences& prefs) noexcept {
    if (is_stored_field_column(field)) {
        return prefs.stored_custom_fields;
    }
    const std::vector<FormulaColumnSpec>& specs = formula_column_specs();
    for (std::size_t index = 0; index < specs.size(); ++index) {
        if (specs[index].field != field) {
            continue;
        }
        if (index == 0) {
            return prefs.formula_duration_column;
        }
        if (index == 1) {
            return prefs.formula_date_column;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Formula columns from 114/Props
// ---------------------------------------------------------------------------

FormulaColumnSet read_file_formulas(const std::vector<std::uint8_t>& props_bytes) {
    FormulaColumnSet set;
    if (props_bytes.empty()) {
        return set;  // no stream: a file without formulas, not an error
    }
    msproject::props::PropsDocument document;
    msproject::props::ParseError parse_error;
    if (!msproject::props::parse_props(props_bytes, document, &parse_error)) {
        set.warnings.push_back("114/Props did not parse: " + parse_error.message);
        return set;
    }
    for (const msproject::props::FormulaBlock& block :
         msproject::props::extract_formulas(document)) {
        FormulaColumn column;
        column.field = block.field_id;
        column.formula = block.formula;
        column.title = registry_title(block.field_id);
        column.from_file = true;
        if (!is_task_field(column.field)) {
            set.warnings.push_back("formula for field 0x" +
                                   std::to_string(column.field) +
                                   " skipped: not a Task field");
            continue;
        }
        if (formula_covers(set.columns, column.field)) {
            set.warnings.push_back("duplicate formula for " + column.title +
                                   " ignored (the first one is used)");
            continue;
        }
        const core::formula::ParseResult parsed = core::formula::parse(column.formula);
        if (!parsed.ok) {
            set.warnings.push_back("formula for " + column.title +
                                   " does not parse: " + parsed.error);
            continue;
        }
        set.columns.push_back(std::move(column));
    }
    return set;
}

FormulaColumnSet formula_columns(const FormulaColumnSet& file_formulas) {
    FormulaColumnSet set;
    set.warnings = file_formulas.warnings;
    set.columns = file_formulas.columns;
    // The app's two demo columns keep working for a field the file does not
    // define.  A file formula for the same field wins (the file is the project's
    // own definition), which is why this only fills gaps.
    for (const FormulaColumnSpec& spec : formula_column_specs()) {
        if (spec.field == 0 || formula_covers(set.columns, spec.field)) {
            continue;
        }
        FormulaColumn column;
        column.field = spec.field;
        column.title = spec.title;
        column.formula = spec.formula;
        const core::formula::ParseResult parsed = core::formula::parse(column.formula);
        if (!parsed.ok) {
            set.warnings.push_back(std::string("demo formula \"") + column.title +
                                   "\" does not parse: " + parsed.error);
            continue;
        }
        set.columns.push_back(std::move(column));
    }
    return set;
}

std::vector<core::sheet::SheetColumn> task_editor_columns(const FormulaColumnSet& formulas) {
    std::vector<core::sheet::SheetColumn> columns = default_sheet_columns();
    for (const StoredFieldColumn& spec : stored_field_columns()) {
        if (formula_covers(formulas.columns, spec.field)) {
            continue;
        }
        columns.push_back(make_column(spec.field, registry_title(spec.field), spec.width,
                                      spec.align, spec.kind, true));
    }
    for (const FormulaColumn& formula : formulas.columns) {
        // A computed column's value kind is whatever the formula evaluates to, so
        // the column stays Auto; the header is the field's own name, exactly as
        // Project labels a custom field.
        columns.push_back(make_column(formula.field, formula.title, 14, ColumnAlign::Auto,
                                      CellKind::Auto, true));
    }
    return columns;
}

bool build_computed_columns(core::formula::ComputedColumns& out,
                            const std::vector<FormulaColumn>& columns, std::string& error) {
    out = core::formula::ComputedColumns{};
    error.clear();
    for (const FormulaColumn& column : columns) {
        if (!out.add(column.field, column.title, column.formula, error)) {
            return false;
        }
    }
    return true;
}

void apply_formula_read_only(core::sheet::SheetModel& sheet,
                             const std::vector<FormulaColumn>& columns) {
    for (std::size_t index = 0; index < sheet.column_count(); ++index) {
        if (std::any_of(columns.begin(), columns.end(), [&sheet, index](const FormulaColumn& c) {
                return same_field(c, sheet.column(index).field);
            })) {
            sheet.set_column_read_only(index, true);
        }
    }
}

void apply_column_visibility(core::sheet::SheetModel& sheet, const FormulaColumnSet& formulas,
                             const Preferences& prefs) {
    for (std::size_t index = 0; index < sheet.column_count(); ++index) {
        const FieldId field = sheet.column(index).field;
        if (!is_custom_column(formulas, field)) {
            continue;
        }
        sheet.set_column_visible(
            index, column_visible(prefs, field, custom_column_default_visible(field, prefs)));
    }
}

bool is_custom_column(const FormulaColumnSet& formulas, FieldId field) noexcept {
    return is_stored_field_column(field) || formula_covers(formulas.columns, field);
}

}  // namespace app
