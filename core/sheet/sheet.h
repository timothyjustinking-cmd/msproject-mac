/*
 * sheet.h -- the task sheet's view model and geometry.
 *
 * A "sheet" is what MS Project calls the left-hand grid of the Gantt view (and
 * of the Task Sheet view): an ordered set of columns over the task records,
 * with an outline that collapses, a selection and in-place editing.  This header
 * is everything a Sheet needs that is NOT drawing: no toolkit types, no fonts,
 * no colours, no widget calls.  A macOS shell supplies metrics in points, draws
 * the text this model formats, and calls back in for selection, editing and
 * collapse.
 *
 * WHERE THE DATA COMES FROM
 *   records  core::view::RecordSet, keyed by PjField id -- the same object the
 *            MSPDI importer and the .mpp reader bridge produce.  Values carry
 *            their own kind (duration ticks, civil Date, Number, Text, Bool),
 *            so the sheet never reinterprets storage.  The RecordSet's row
 *            order is the sheet's row order; there is no sort or filter here
 *            (build the sheet from an already-ordered RecordSet).
 *   columns  a core::view::TableDefinition (the configured table, from
 *            core/view/defaults.h or a user table) or an explicit
 *            vector<SheetColumn> when the shell wants per-column hints.
 *   outline  Task.OutlineLevel and Task.Summary when the RecordSet supplies
 *            them; per-row overrides exist for producers that do not yet
 *            (the reader bridge is still growing those fields).
 *
 * WHAT THIS FILE DOES NOT DO
 *   * No drawing.  CellStyle is data -- "right", "bold", "red" -- that the
 *     shell maps to its own attributes.
 *   * No summary rollups, no scheduling, no critical-path computation: the
 *     Critical flag is read, never derived.
 *   * No locale lookup: SheetFormat carries the labels and unit suffixes the
 *     app's own options hold, so the sheet renders the file's text rather than
 *     a hard-coded English one.
 *   * No row insert/delete/move or indent/outdent.  RecordSet has add_row and
 *     clear but no insert, erase or reorder, so the sheet cannot represent those
 *     operations; set_outline_level only changes an existing row's level over
 *     the RecordSet's order.  (See core/sheet/README.md section 6.)
 *   * No pointer or toolkit type crosses this boundary: the model holds a
 *     reference to the RecordSet and otherwise carries plain data, so a view
 *     controller can rebuild it at will.
 *
 * EDITING IS A TRANSACTION LAYER
 *   A single cell has an edit session (begin_edit -> commit_edit/cancel_edit).
 *   Several cells have a transaction: begin_transaction, stage_* (or
 *   push_command), apply_transaction / cancel_transaction.  Apply validates
 *   every staged EditCommand before the first write, so a failure writes
 *   nothing and leaves the transaction open for correction.  A transaction and
 *   a paste each record ONE user-level undo step, holding one UndoEntry per
 *   changed cell; consecutive single-cell commits may coalesce into a Typing
 *   step when the shell asks for it.  Paste copies typed values, not just text.
 *   The full rules -- and which of them are unevidenced decisions -- are in
 *   core/sheet/README.md.
 *
 * FORMATTING IS DISPLAY-ONLY AND LOSSY, THE VALUE IS NOT
 *   Duration text follows MS Project's display convention: the largest unit
 *   that fits, at most two decimals, e.g. "3 days" (14,400 ticks at 8 h/day),
 *   "4 hrs" (2,400 ticks), "0 days" (a milestone).  An odd tick count such as
 *   1.5625 days renders "1.56 days" and is therefore not exact; the RecordSet
 *   always holds the exact tick count, and editing parses back to ticks.  The
 *   value is authoritative, never the text.
 *
 * ALL DIMENSIONS ARE DEVICE-INDEPENDENT POINTS
 *   SheetMetrics and every geometry function return plain integers in points.
 *   Widths stored on a column are characters (view::Column::width, 0 = auto),
 *   matching core/view; the model multiplies by SheetMetrics::char_width and
 *   leaves auto columns for the shell to measure.  DPI, Retina scaling and
 *   font choice are the shell's business.
 */

#ifndef CORE_SHEET_SHEET_H
#define CORE_SHEET_SHEET_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "record_set.h"
#include "table.h"
#include "value.h"

namespace core::sheet {

using core::view::FieldId;
using core::view::RecordSet;
using core::view::Value;

/* Returned by every ordinal/index lookup that misses.  Never a valid index. */
inline constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

/*
 * How a cell is rendered.  Auto means "from the data": the kind of the value in
 * that cell, else the kind of the column (the first populated cell's kind, else
 * the descriptor table's storage kind).  A column may name a kind explicitly to
 * render an integer field as a percentage or a number field as money; that is
 * presentation configuration, not storage.
 */
enum class CellKind : std::uint8_t {
    Auto = 0,
    Text,
    Integer,
    Number,
    Duration,
    Date,
    Percent,
    Cost,
    Bool,
};

const char* cell_kind_name(CellKind kind) noexcept;
bool cell_kind_from_name(std::string_view name, CellKind& out) noexcept;

/* The kind a value formats as when the column does not override it. */
CellKind value_cell_kind(const Value& value) noexcept;

/*
 * The CellKind a field edits as, from the descriptor table's storage kind
 * (core::view::field_value_kind).  This is a *storage* mapping, not a semantic
 * one: Task.Duration is stored as i32 ticks, so field_cell_kind(Duration) is
 * Integer unless the column declares CellKind::Duration.  The sheet uses it for
 * an edit staged by field id when no column describes the field.
 */
CellKind field_cell_kind(FieldId field) noexcept;

/*
 * The unit labels MS Project's options let the user set, and that the file's
 * own "duration is entered in" setting implies.  Defaults are the English
 * forms Project displays: "1 day" / "3 days", "4 hrs", "30 mins", "2 wks",
 * "1 mon".  A singular label is used only when the rendered number is exactly
 * "1"; everything else takes the plural.
 */
struct UnitLabels {
    std::string day_singular = "day";
    std::string day_plural = "days";
    std::string hour_singular = "hr";
    std::string hour_plural = "hrs";
    std::string minute_singular = "min";
    std::string minute_plural = "mins";
    std::string week_singular = "wk";
    std::string week_plural = "wks";
    std::string month_singular = "mon";
    std::string month_plural = "mons";
};

enum class DateOrder : std::uint8_t {
    DayMonthYear = 0, /* 20/10/26 */
    MonthDayYear,     /* 10/20/26 */
    YearMonthDay,     /* 2026-10-20 */
};

/*
 * The app's date format setting -- the one that makes a Date cell read
 * "Tue 20/10/26".  show_time_of_day appends " 17:00" only when the value has a
 * non-midnight time (a task Start at 08:00 shows it; a date-only field does
 * not).  Two-digit years are displayed as stored; the century rule for the
 * *parser* is documented on parse_date_text below.
 */
struct DateFormat {
    DateOrder order = DateOrder::DayMonthYear;
    char separator = '/';
    bool two_digit_year = true;
    bool show_weekday = true;
    bool show_time_of_day = false;

    /* "Tue 20/10/26" -- Project's short date with the weekday. */
    static DateFormat short_day_month_year();
    /* "Tue 10/20/26". */
    static DateFormat short_month_day_year();
    /* "2026-10-20", unadorned and unambiguous (the CSV/ISO form). */
    static DateFormat iso();
};

/*
 * Everything that changes how a *value* is rendered, as opposed to how it is
 * stored.  A shell builds this from its options/settings; defaults are the
 * common 8-hour day, 5-day week, 20-day month.
 *
 *   hours_per_day    ticks per working day, 4800 at the default 8 (6-second
 *                    ticks, core::view::kTicksPerDay)
 *   days_per_week    used only to parse "w"/"wk"/"week" input (a display unit
 *                    of weeks is not chosen: no evidence for when Project does)
 *   days_per_month   used only to parse "mo"/"mon"/"month" input
 *   decimal_separator, thousands_separator  both are applied to every numeric
 *                    cell (durations, numbers, percentages, costs) and both are
 *                    honoured on input; they must differ
 *   currency_symbol  prepended to cost text when non-empty; also stripped
 *                    from typed cost input
 */
struct SheetFormat {
    int hours_per_day = 8;
    int days_per_week = 5;
    int days_per_month = 20;

    UnitLabels units{};
    DateFormat date{};

    std::string yes_label = "Yes";
    std::string no_label = "No";

    std::string currency_symbol;
    int cost_decimals = 2;
    bool group_thousands = true;
    char thousands_separator = ',';
    char decimal_separator = '.';
};

/*
 * One sheet column.  Field ids, widths and alignment come from a
 * view::TableDefinition when the sheet is built from a configured table; a
 * shell may instead hand-build columns to attach a CellKind override (a cost
 * column) or to present an unnamed field.
 *
 *   title   empty means "the descriptor table's name", exactly as view::Column
 *   width   characters; 0 means auto, and the shell measures it
 *   visible hidden columns stay in the model so unhiding restores their order
 *   read_only  the host declares the field not editable in this column (an
 *           identity/calculated column, or a locked custom field).  The sheet
 *           does NOT infer which MS Project fields are locked: no evidence for
 *           that rule exists yet (decision, unmeasured).  A read-only column
 *           refuses begin_edit, commit_edit, stage_edit and paste.
 */
struct SheetColumn {
    FieldId field = 0;
    std::string title;
    int width = 0;
    core::view::ColumnAlign align = core::view::ColumnAlign::Auto;
    CellKind kind = CellKind::Auto;
    bool visible = true;
    bool read_only = false;
};

/* A cell coordinate.  Row = RecordSet row index, column = model column index. */
struct CellRef {
    std::size_t row = 0;
    std::size_t column = 0;
};

bool operator==(const CellRef& a, const CellRef& b) noexcept;
bool operator!=(const CellRef& a, const CellRef& b) noexcept;

/*
 * A normalized rectangular selection: begin <= end on both axes, `valid` false
 * when nothing is selected.  Coordinates are RecordSet rows and model column
 * indices (the same space as CellRef), not display ordinals.  The rectangle may
 * contain rows or columns that are hidden; is_selected()/copy_selection() never
 * include those, so a range is a *shape*, not a list of cells it covers.
 */
struct CellRange {
    std::size_t row_begin = 0;
    std::size_t row_end = 0;
    std::size_t column_begin = 0;
    std::size_t column_end = 0;
    bool valid = false;

    std::size_t row_count() const noexcept { return valid ? row_end - row_begin + 1 : 0; }
    std::size_t column_count() const noexcept {
        return valid ? column_end - column_begin + 1 : 0;
    }
    bool contains(std::size_t row, std::size_t column) const noexcept {
        return valid && row >= row_begin && row <= row_end && column >= column_begin &&
               column <= column_end;
    }
};

bool operator==(const CellRange& a, const CellRange& b) noexcept;
bool operator!=(const CellRange& a, const CellRange& b) noexcept;

/*
 * The geometry a sheet needs, supplied by the shell so DPI/font handling stays
 * out of the model.  All values are points.
 *
 *   min_column_width  floor applied to an explicit character width (0 = none);
 *                     auto columns are unaffected
 *   indent_per_level  horizontal offset added per outline level
 */
struct SheetMetrics {
    int row_height = 20;
    int header_height = 24;
    int char_width = 7;
    int indent_per_level = 12;
    int min_column_width = 0;
};

/* One laid-out column: x and width in points, in visible-column order. */
struct ColumnSegment {
    std::size_t column = kNoIndex;
    int x = 0;
    int width = 0;
};

/* Alignment the shell applies to a cell.  Data, not a font or a colour. */
enum class CellAlign : std::uint8_t { Left = 0, Center, Right };

/*
 * Presentation hints for one cell:
 *   align  the column's explicit alignment, else "by kind" (numbers, durations,
 *          costs and percentages right; text, dates and flags left)
 *   bold   the row is a summary
 *   red    the row is critical (Task.Critical) or overdue (Finish before the
 *          status date and not 100% complete; never true without a status date)
 * A shell may also colour critical/overdue rows differently; the model states
 * the fact rather than picking the colour.
 */
struct CellStyle {
    CellAlign align = CellAlign::Left;
    bool bold = false;
    bool red = false;
};

bool operator==(const CellStyle& a, const CellStyle& b) noexcept;

/*
 * The result of parsing or committing an edit.  `ok` false means `error` is
 * non-empty and nothing was written; the edit session (if one was open) is
 * left open so the UI can show the message and let the user correct the text.
 *
 * `changed` is true only for a successful commit that actually altered the
 * value; an unchanged commit records no undo entry.
 */
struct EditOutcome {
    bool ok = false;
    bool changed = false;
    Value value{};
    std::string error;
};

/*
 * One reversible cell edit.  `field` rather than a column index, so the entry
 * stays valid if columns are hidden or reordered, and so undo works against the
 * RecordSet (the source of truth) rather than a cached copy.
 */
struct UndoEntry {
    std::size_t row = 0;
    FieldId field = 0;
    Value before{};
    Value after{};
};

/*
 * Why a step is on the undo stack.  The kind drives the menu text ("Undo
 * Paste") and the coalescing rule: only Cell/Typing steps whose last entry is
 * the same cell may merge (see commit_edit(bool)).
 */
enum class UndoKind : std::uint8_t {
    Cell = 0,     /* one commit_edit */
    Typing,       /* consecutive coalesced commits of the same cell */
    Transaction,  /* one apply_transaction */
    Paste,        /* one paste of a clipboard block */
};

const char* undo_kind_name(UndoKind kind) noexcept;

/*
 * One user-level undo action: a single cell edit, a coalesced typing run, a
 * whole transaction or a paste.  `entries` are applied in forward order by redo
 * and in reverse order by undo, so a transaction that writes B then A undoes A
 * first.  `label` is optional display text; when empty, undo_label() derives
 * one from the last entry's field.
 */
struct UndoStep {
    UndoKind kind = UndoKind::Cell;
    std::string label;
    std::vector<UndoEntry> entries;
};

/*
 * A copied cell.  Offsets are relative to the block's top-left and are
 * contiguous over the cells that were actually copied (hidden rows and columns
 * are not copied, so the block is the visible shape of the selection).
 * `value` is the typed value; `text` is its rendering at copy time, so a paste
 * can use either the value (lossless) or the text (what the user saw).
 */
struct ClipboardCell {
    std::size_t row_offset = 0;
    std::size_t column_offset = 0;
    FieldId field = 0;
    CellKind kind = CellKind::Auto;
    Value value{};
    std::string text;
};

/*
 * The sheet's clipboard block: a rectangle of typed cells.  It is plain data;
 * the shell owns the platform pasteboard.  to_text()/from_text() are the TSV
 * interchange form (tab-separated cells, newline-separated rows, RFC-4180-style
 * quoting for a cell containing a tab, a newline or a quote).  Text is display
 * data and is therefore lossy for a duration with an odd tick count;
 * copy/paste inside the model uses `value` and is exact.
 */
struct ClipboardBlock {
    std::size_t rows = 0;
    std::size_t columns = 0;
    std::vector<ClipboardCell> cells;

    bool empty() const noexcept { return rows == 0 || columns == 0; }
    /* Null when the offset is outside the block or no cell was stored there. */
    const ClipboardCell* at(std::size_t row_offset, std::size_t column_offset) const noexcept;
    std::string to_text() const;
    static ClipboardBlock from_text(std::string_view text);
};

/*
 * One staged cell change.  `text` is parsed when the transaction is applied, so
 * a whole pasted block can be validated before anything is written; a command
 * built from an already-typed value sets is_value and stores `value` instead.
 *
 * `kind` is the resolved target kind, never Auto after staging.  `column` is the
 * model column at stage time, or kNoIndex when the command was staged by field;
 * `field` is always the storage key that undo records.
 */
struct EditCommand {
    std::size_t row = 0;
    std::size_t column = kNoIndex;
    FieldId field = 0;
    CellKind kind = CellKind::Auto;
    std::string text;
    Value value{};
    bool is_value = false;
};

/*
 * The result of apply_transaction().  On failure NOTHING has been written:
 * validation is a complete pass before the first write, and a failed apply
 * leaves the transaction open so the shell can correct the entry named by
 * `failed_index` (set_staged_text) or cancel it.  `staged` is the number of
 * commands considered; `changed` is the number of cells whose value altered.
 */
struct TransactionResult {
    bool ok = false;
    std::size_t staged = 0;
    std::size_t changed = 0;
    std::size_t failed_index = kNoIndex;
    CellRef failed_cell{};
    std::string error;
};

/*
 * How a paste interprets the block.  Values (the default) writes the typed
 * value when the target cell has the same kind, and otherwise parses the
 * source's own rendering as the target kind.  Text always parses the copied
 * display text as the target kind.  The two differ only on a lossy rendering (a
 * duration of 1.5625 days is shown "1.56 days").
 */
enum class PasteMode : std::uint8_t { Values = 0, Text };

/*
 * The result of paste().  `cells` counts block cells with a target (changed or
 * not), `skipped` counts block cells that fell outside the sheet or had no
 * matching column -- pasting never adds rows or columns -- and `changed` counts
 * cells whose value actually altered.  On failure nothing was written;
 * `failed_index` indexes block.cells.
 */
struct PasteResult {
    bool ok = false;
    std::size_t cells = 0;
    std::size_t changed = 0;
    std::size_t skipped = 0;
    std::size_t failed_index = kNoIndex;
    CellRef failed_cell{};
    std::string error;
};

/*
 * The sheet model.  Construct it from the RecordSet and a table, then answer
 * questions; it owns no widgets and holds a reference to the RecordSet it was
 * built from (which must outlive the model), because committing an edit writes
 * through to it.
 */
class SheetModel {
  public:
    SheetModel(RecordSet& records, std::vector<SheetColumn> columns);
    SheetModel(RecordSet& records, const core::view::TableDefinition& table);

    SheetModel(const SheetModel&) = delete;
    SheetModel& operator=(const SheetModel&) = delete;

    /* ---- sources ------------------------------------------------------- */

    RecordSet& records() noexcept { return records_; }
    const RecordSet& records() const noexcept { return records_; }
    core::model::RecordType record_type() const noexcept { return records_.record_type(); }

    /* Presentation settings.  Changing them re-renders; it records no undo. */
    SheetFormat& format() noexcept { return format_; }
    const SheetFormat& format() const noexcept { return format_; }
    void set_format(SheetFormat format) { format_ = std::move(format); }

    /*
     * Replaces the column set.  Clears the selection if it no longer addresses
     * a visible cell; keeps collapse state and row heights (they are per row,
     * not per column).
     */
    void set_columns(std::vector<SheetColumn> columns);
    void set_columns(const core::view::TableDefinition& table);

    /* ---- columns ------------------------------------------------------- */

    std::size_t column_count() const noexcept { return columns_.size(); }
    /* Out-of-range returns an empty column (field 0, auto, hidden is NOT
     * implied -- visible false is reserved for a real column). */
    const SheetColumn& column(std::size_t column) const noexcept;
    bool column_visible(std::size_t column) const noexcept;
    bool set_column_visible(std::size_t column, bool visible);
    bool set_column_width(std::size_t column, int width);
    bool set_column_kind(std::size_t column, CellKind kind);
    bool set_column_title(std::size_t column, std::string title);
    /* Host-declared; the sheet never infers it (decision, unmeasured). */
    bool column_read_only(std::size_t column) const noexcept;
    bool set_column_read_only(std::size_t column, bool read_only);

    /* The header text: authored title, else the descriptor name, else "0x...". */
    std::string column_title(std::size_t column) const;

    std::size_t visible_column_count() const noexcept;
    std::vector<std::size_t> visible_columns() const;
    /* Visible ordinal -> model column index; kNoIndex when ordinal is too big. */
    std::size_t visible_column(std::size_t ordinal) const noexcept;
    /* Model column index -> visible ordinal; kNoIndex when hidden. */
    std::size_t display_column(std::size_t column) const noexcept;
    /* Model column index -> first column using a field; kNoIndex when absent. */
    std::size_t column_of_field(FieldId field) const noexcept;

    /* ---- rows and the outline ------------------------------------------ */

    std::size_t row_count() const noexcept { return records_.row_count(); }
    std::uint32_t row_uid(std::size_t row) const noexcept;

    /*
     * Outline facts.  outline_level is 0 for a top-level task and 1 for its
     * first child.  is_summary is true when the Task.Summary flag is set, or
     * when the row is structurally the parent of the next row (a producer that
     * omits the flag still gets a working outline).
     */
    int outline_level(std::size_t row) const noexcept;
    bool is_summary(std::size_t row) const noexcept;
    bool has_children(std::size_t row) const noexcept;
    /* Per-row override for producers that do not supply OutlineLevel/Summary. */
    void set_outline_level(std::size_t row, int level, bool summary);
    void clear_outline_level(std::size_t row);

    /* Collapsed summaries hide every descendant until a row at the same or a
     * shallower level.  Collapsing a non-summary is refused (returns false). */
    bool is_collapsed(std::size_t row) const noexcept;
    bool collapse(std::size_t row);
    bool expand(std::size_t row);
    bool toggle_collapsed(std::size_t row);
    void collapse_all();
    void expand_all();

    /* True when no collapsed summary above the row hides it.  O(rows) per
     * query: call visible_rows() once for a whole layout pass rather than this
     * per cell. */
    bool row_visible(std::size_t row) const noexcept;
    /* The flattened visible order: index -> RecordSet row index. */
    std::vector<std::size_t> visible_rows() const;
    std::size_t visible_row_count() const noexcept;
    std::size_t visible_row(std::size_t ordinal) const noexcept;  /* kNoIndex */
    std::size_t display_row(std::size_t row) const noexcept;      /* kNoIndex */

    /* ---- per-row height ------------------------------------------------- */

    /* 0 means "the metrics default"; a positive value overrides it. */
    bool set_row_height(std::size_t row, int height);
    int row_height_override(std::size_t row) const noexcept;
    int row_height_points(std::size_t row, const SheetMetrics& metrics) const noexcept;

    /* ---- cells and formatting ------------------------------------------ */

    /* Nullptr when the row is out of range or the field is absent. */
    const Value* cell_value(std::size_t row, std::size_t column) const noexcept;

    /* The kind this cell renders as: the column's override, else the value's
     * kind, else the column's resolved kind. */
    CellKind cell_kind(std::size_t row, std::size_t column) const noexcept;

    /* The display text, formatted exactly once, here.  "" for Null or an
     * invalid coordinate (a blank cell, not an error). */
    std::string cell_text(std::size_t row, std::size_t column) const;

    CellStyle cell_style(std::size_t row, std::size_t column) const;

    /* The same rendering the model uses, for the shell's own use (a formatter
     * for the status bar, the clipboard, a test). */
    std::string format_value_as(const Value& value, CellKind kind) const;

    /* Row facts the style depends on. */
    bool is_critical(std::size_t row) const noexcept;
    bool is_overdue(std::size_t row) const noexcept;
    void set_status_date(const core::view::DateValue& date);
    void clear_status_date() noexcept;
    bool has_status_date() const noexcept { return has_status_date_; }
    const core::view::DateValue& status_date() const noexcept { return status_date_; }

    /* ---- selection ------------------------------------------------------ */

    bool has_selection() const noexcept { return has_selection_; }
    CellRef anchor() const noexcept { return anchor_; }
    CellRef focus() const noexcept { return focus_; }

    /* Selects one cell; anchor and focus become the same cell.  Ignored when
     * the coordinate does not address a visible cell. */
    bool select_cell(std::size_t row, std::size_t column);
    /* Moves the focus, keeping the anchor: a rectangular range. */
    bool extend_to(std::size_t row, std::size_t column);
    void set_anchor(std::size_t row, std::size_t column);
    void select_all();
    void clear_selection() noexcept;

    bool is_selected(std::size_t row, std::size_t column) const noexcept;
    /* Visible rows inside the selection rectangle, ascending. */
    std::vector<std::size_t> selected_rows() const;

    /* The selection as one normalized rectangle; valid=false when none. */
    CellRange selection_range() const noexcept;
    /*
     * Selects an explicit rectangle.  The endpoints are clamped like extend_to:
     * a hidden row/column moves to the nearest visible one at or above/left,
     * else the first.  Returns false (and changes nothing) when the sheet has no
     * visible row or no visible column.  The endpoints may be given in either
     * order.
     */
    bool select_range(std::size_t row_begin, std::size_t row_end,
                      std::size_t column_begin, std::size_t column_end);
    /* The visible cells inside the selection, row-major.  Empty without one. */
    std::vector<CellRef> selected_cells() const;

    /* ---- editing -------------------------------------------------------- */

    bool editing() const noexcept { return editing_; }
    CellRef edit_target() const noexcept { return edit_target_; }
    const std::string& edit_text() const noexcept { return edit_text_; }
    void set_edit_text(std::string text) { edit_text_ = std::move(text); }

    /* Starts an edit on a visible, editable cell, seeding edit_text with
     * cell_text.  False for a read-only column. */
    bool begin_edit(std::size_t row, std::size_t column);
    /* Parses edit_text for the edit target without writing.  ok=false and an
     * error message the UI can show when the text is not a valid value. */
    EditOutcome validate_edit() const;
    /* Parses and writes through to the RecordSet, recording one undo step.
     * On failure the edit session stays open and nothing is written. */
    EditOutcome commit_edit();
    /*
     * As commit_edit(), but when `coalesce_with_previous` is true a successful
     * change of the SAME cell as the top undo step appends to that step instead
     * of starting a new one (kind becomes Typing).  The model has no clock: the
     * shell decides when a typing run ends and passes false for the first commit
     * of a new cell or after any other action.
     */
    EditOutcome commit_edit(bool coalesce_with_previous);
    void cancel_edit() noexcept;

    /* Parses `text` as a value of `kind`.  Empty text is Null for every kind
     * except Text, where it is an empty string; Auto is treated as Text here
     * (see parse_edit_text). */
    EditOutcome parse_text(std::string_view text, CellKind kind) const;

    /* ---- transactions (multi-cell, atomic edits) ------------------------ */

    /*
     * A transaction stages one or more EditCommands and applies them all or
     * none:
     *
     *   begin_transaction()   opens a staging list; false when one is already
     *                         open or a single-cell edit session is open
     *   stage_* / push_command appends a command; false when no transaction is
     *                         open, the target is malformed or read-only, or
     *                         stage_edit names a hidden row/column.  The
     *                         field-based stages and push_command address the
     *                         field directly and only require a valid row, so a
     *                         task-information form can edit a field that no
     *                         visible column shows.
     *   validate_staged(i)    parses command i without writing (preview/error)
     *   set_staged_text/value correct a staged command in place
     *   discard_staged(i)     remove one staged command (later indices shift)
     *   apply_transaction()   validate every command, then write; one undo step
     *   cancel_transaction()  discard the staging list; nothing is written
     *
     * Validation before the first write is what makes rollback trivial: a parse
     * error, an incompatible typed value, a duplicate (row, field) target or a
     * column that became read-only leaves the RecordSet untouched and the
     * transaction open.  Applying an empty transaction succeeds with changed 0
     * and records no undo step.
     */
    bool begin_transaction(UndoKind kind = UndoKind::Transaction, std::string label = {});
    bool transaction_active() const noexcept { return transaction_active_; }
    UndoKind transaction_kind() const noexcept { return transaction_kind_; }
    const std::string& transaction_label() const noexcept { return transaction_label_; }
    std::size_t staged_count() const noexcept { return staged_.size(); }
    const EditCommand* staged(std::size_t index) const noexcept;
    EditOutcome validate_staged(std::size_t index) const;
    /* Stages raw text against a visible editable column; parsed at apply. */
    bool stage_edit(std::size_t row, std::size_t column, std::string text);
    /* Stages raw text against a field; `kind` Auto resolves via column/field. */
    bool stage_field_edit(std::size_t row, FieldId field, std::string text,
                          CellKind kind = CellKind::Auto);
    /* Stages an already-typed value; kind compatibility is checked at apply. */
    bool stage_value(std::size_t row, std::size_t column, Value value);
    bool stage_field_value(std::size_t row, FieldId field, Value value,
                           CellKind kind = CellKind::Auto);
    bool push_command(EditCommand command);
    bool set_staged_text(std::size_t index, std::string text);
    bool set_staged_value(std::size_t index, Value value);
    bool discard_staged(std::size_t index);
    TransactionResult apply_transaction();
    void cancel_transaction() noexcept;

    /* ---- clipboard (copy / paste) --------------------------------------- */

    /*
     * Copies the selected rectangle.  Only visible rows and columns inside it
     * are copied, so a collapsed row or a hidden column never contributes a
     * cell; the block's offsets close the gaps.  Empty block without a
     * selection.
     */
    ClipboardBlock copy_selection() const;
    /*
     * Pastes at the selection's normalized top-left cell (the no-coordinate
     * overload).  The explicit-coordinate overload requires a visible target
     * cell; it is the one the shell uses for a click.  Pasting is one atomic
     * transaction and one undo step:
     *
     *   rows     block row offsets walk visible rows from the target, so a
     *            paste never writes to a hidden row and rows past the end are
     *            skipped (counted, never appended)
     *   columns  a block column is matched to the first visible target column
     *            with the same field id; if there is none it falls back to the
     *            target column's visible ordinal + offset.  Columns with no
     *            target are skipped.
     *   values   PasteMode::Values keeps the typed value when the target kind
     *            matches, else parses the source's rendering as the target kind;
     *            PasteMode::Text parses the copied display text.
     *   failure  a parse failure, an incompatible typed value or a read-only
     *            target writes nothing and returns ok=false.
     *
     * On success the pasted target rectangle becomes the selection.  Refused
     * while a transaction or an edit session is open.
     */
    PasteResult paste(const ClipboardBlock& block, std::size_t row, std::size_t column,
                      PasteMode mode = PasteMode::Values);
    PasteResult paste(const ClipboardBlock& block, PasteMode mode = PasteMode::Values);

    /* ---- undo / redo ----------------------------------------------------- */

    bool can_undo() const noexcept { return !undo_.empty(); }
    bool can_redo() const noexcept { return !redo_.empty(); }
    /* User-level steps: one per commit / coalesced typing run / transaction /
     * paste.  undo_entry_count() is the raw number of cell changes. */
    std::size_t undo_depth() const noexcept { return undo_.size(); }
    std::size_t redo_depth() const noexcept { return redo_.size(); }
    std::size_t undo_entry_count() const noexcept;
    std::size_t redo_entry_count() const noexcept;
    /* The last entry of the step undo() would apply (nullptr when empty).  For
     * a multi-cell step use next_undo_step() to see the whole action. */
    const UndoEntry* next_undo() const noexcept;
    const UndoEntry* next_redo() const noexcept;
    /* The whole step undo()/redo() would apply; nullptr when empty. */
    const UndoStep* next_undo_step() const noexcept;
    const UndoStep* next_redo_step() const noexcept;
    /* "Edit Duration", "Paste", ...; "" when the stack is empty. */
    std::string undo_label() const;
    std::string redo_label() const;

    /* Undo restores the previous value, not the previous schema: an edit that
     * added a field to the RecordSet leaves the field present (holding Null
     * again after undo).  Edits of the same value are not recorded.  The
     * overloads apply `steps` whole steps. */
    bool undo();
    bool undo(std::size_t steps);
    bool redo();
    bool redo(std::size_t steps);
    void clear_history() noexcept;

    /* ---- geometry -------------------------------------------------------- */

    /* A column's width in points; 0 means auto (the shell measures it). */
    int column_width_points(std::size_t column, const SheetMetrics& metrics) const noexcept;
    /* Visible columns in order, each with its x offset and width.  auto_width
     * is the width the shell gives an auto column (<= 0 means zero). */
    std::vector<ColumnSegment> layout_columns(const SheetMetrics& metrics,
                                              int auto_width) const;
    /* Sum of the laid-out column widths. */
    int sheet_width_points(const SheetMetrics& metrics, int auto_width) const;
    /* Left/top of the cell in points.  row_top is relative to the top of the
     * grid body (header included), so the header height is added once. */
    int row_top_points(std::size_t display_ordinal, const SheetMetrics& metrics) const;
    /* Y of the first row *below* the last visible row, header included. */
    int content_height_points(const SheetMetrics& metrics) const;
    int indent_points(std::size_t row, const SheetMetrics& metrics) const noexcept;

    /*
     * Recomputes cached per-row and per-column facts.  Call it after mutating
     * `records()` structurally (adding rows or writing cells directly instead
     * of through commit_edit).  Edits through this model refresh themselves.
     */
    void refresh();

  private:
    struct RowOutline {
        int level = 0;
        bool summary = false;
        bool has_override = false;
    };

    void rebuild_outline();
    void rebuild_column_kinds();
    void clamp_selection();
    std::size_t clamp_row(std::size_t row) const;
    std::size_t clamp_column(std::size_t column) const;
    void drop_edit_if_hidden() noexcept;
    int field_outline_level(std::size_t row) const noexcept;
    bool field_summary(std::size_t row) const noexcept;
    bool field_flag(std::size_t row, FieldId field) const noexcept;
    void refresh_column_kind(std::size_t column);
    void refresh_field_columns(FieldId field);
    bool field_read_only(FieldId field) const noexcept;
    bool resolve_column_command(std::size_t row, std::size_t column, EditCommand& command,
                                std::string& error) const;
    bool resolve_field_command(std::size_t row, FieldId field, CellKind kind,
                               EditCommand& command, std::string& error) const;
    bool validate_command(const EditCommand& command, Value& value, std::string& error) const;
    TransactionResult apply_commands(UndoKind kind, const std::string& label);
    bool push_undo_step(UndoStep step);
    void apply_step(const UndoStep& step, bool forward);
    void clear_transaction_state() noexcept;
    std::string step_label(const UndoStep& step) const;

    RecordSet& records_;
    std::vector<SheetColumn> columns_;
    std::vector<CellKind> column_kinds_; /* resolved per column, Auto resolved */
    std::vector<RowOutline> outline_;
    std::vector<bool> collapsed_;
    std::vector<int> row_heights_; /* 0 = metrics default */

    SheetFormat format_{};

    FieldId outline_level_field_ = 0;
    FieldId summary_field_ = 0;
    FieldId critical_field_ = 0;
    FieldId finish_field_ = 0;
    FieldId percent_field_ = 0;

    bool has_status_date_ = false;
    core::view::DateValue status_date_{};

    bool has_selection_ = false;
    CellRef anchor_{};
    CellRef focus_{};

    bool editing_ = false;
    CellRef edit_target_{};
    std::string edit_text_;

    std::vector<EditCommand> staged_;
    bool transaction_active_ = false;
    UndoKind transaction_kind_ = UndoKind::Transaction;
    std::string transaction_label_;

    std::vector<UndoStep> undo_;
    std::vector<UndoStep> redo_;
};

/* ---- free formatting functions (one implementation, used by the model) --- */

/*
 * Duration ticks -> display text in `format`'s units: the largest unit that
 * fits, at most two decimals, pluralised by the unit labels.
 *     "3 days" (14400, 8 h/day), "4 hrs" (2400), "30 mins" (300),
 *     "0 days" (0, a milestone), "-3 days" (-14400).
 */
std::string format_duration_text(std::int64_t ticks, const SheetFormat& format);

/*
 * Civil date -> display text per format.date: "Tue 20/10/26",
 * "Thu 01/01/26", "2026-10-20".  "" for an invalid date.
 */
std::string format_date_text(const core::view::DateValue& date, const SheetFormat& format);

/*
 * Any value rendered as `kind`:
 *   Null      "" (any kind)
 *   Text      the text unchanged (other kinds fall back to their own kind)
 *   Integer   "50"
 *   Number    "50" / "12.5"
 *   Duration  format_duration_text (an Integer/Number is taken as ticks)
 *   Date      format_date_text
 *   Percent   "50%" / "12.5%"
 *   Cost      "$1,234.50" with the configured symbol, grouping and decimals
 *   Bool      format.yes_label / format.no_label ("Yes"/"No")
 *   Auto      the value's own kind
 * A value whose kind does not fit the requested kind renders as its own kind
 * rather than as a misleading blank.
 */
std::string format_value_text(const Value& value, CellKind kind, const SheetFormat& format);

/*
 * Parses edit text as `kind`; the one parser behind SheetModel::commit_edit.
 * Accepted forms (documented choices -- there is no oracle evidence yet):
 *
 *   Duration  "3d", "3 days", "4h", "30m", "2w", "1mo", "1.5d"; a bare number
 *             is days.  Negative durations are rejected (a lead is a link lag,
 *             not a negative duration).  Rounded to the nearest 6-second tick.
 *   Date      the configured order ("20/10/26", "20-10-2026"), ISO
 *             ("2026-10-20"), an optional time ("20/10/26 17:00" or
 *             "2026-10-20T17:00").  Two-digit years: 00-29 -> 2000-2029,
 *             30-99 -> 1930-1999 (a display/parse choice, not file data).
 *   Percent   "50", "50%", "12.5%" -> 0..100; outside that range is an error.
 *   Cost      "1234.5", "1,234.50", "$1,234.50" (currency symbol optional).
 *   Integer   a whole number ("7"); a fractional input is an error.
 *   Bool      "Yes"/"No"/"y"/"n"/"true"/"false"/"1"/"0" (case-insensitive).
 *   Text      the text, with surrounding whitespace trimmed.
 *   Auto      treated as Text: this free function has no cell to resolve
 *             against.  SheetModel::validate_edit passes the cell's resolved
 *             kind, so Auto never reaches the parser from the sheet.
 */
EditOutcome parse_edit_text(std::string_view text, CellKind kind, const SheetFormat& format);

}  // namespace core::sheet

#endif  // CORE_SHEET_SHEET_H
