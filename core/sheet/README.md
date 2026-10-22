# core/sheet — the task sheet model

The left-hand grid of the Gantt/Task Sheet views: columns over a
`core::view::RecordSet`, an outline that collapses, a selection, in-place
editing, and now an explicit edit-transaction / undo / clipboard layer.  No
drawing, no fonts, no toolkit types; a macOS shell supplies points and maps the
data hints to its own attributes.

Read `sheet.h` first — every public type and method carries its contract there.
This file is the map, the semantics that are not obvious from the signatures,
the decisions that are ours (there is no oracle evidence for them), and the
verification commands.

## Build and test

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w34 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w34 --config Release --target sheet_tests"
& "build\vs-w34\core\sheet\Release\sheet_tests.exe"
```

* `sheet_tests: 1052 checks passed` (was 597 before the edit/undo/clipboard
  work; the old checks are unchanged and still green).
* `/W4 /permissive- /utf-8 /WX` clean.  No corpus and no MS Project are needed.
* `core/sheet` depends only on `core/view` (and through it `core/model`).  It can
  also be configured standalone with `-S core/sheet` (see its CMakeLists.txt).

## What was already here (W16)

`SheetModel` owns columns (`SheetColumn`, titles, widths, alignment, visibility),
the outline (`OutlineLevel`/`Summary` plus per-row overrides, collapse/expand,
visible-row flattening), per-row heights, per-cell formatting/parsing for
`CellKind` (text, integer, number, duration, date, percent, cost, bool), a
rectangular selection, a single-cell edit session (`begin_edit` /
`set_edit_text` / `validate_edit` / `commit_edit` / `cancel_edit`), and geometry
in points.  Formatting is display-only and lossy; the value in the `RecordSet`
is authoritative.  Those guarantees are demonstrated in `test_formatting`,
`test_format_settings`, `test_outline`, `test_selection`, `test_editing`,
`test_undo_redo`, `test_geometry`, `test_table_definition` and `test_style`.

## New public surface (W34)

Everything below is **additive**: no existing type, method or field was renamed
or removed.  `SheetColumn` gained one field (`read_only`) at the end; all other
changes are new types/functions.

### 1. Read-only columns

```cpp
SheetColumn::read_only;                  // host-declared, default false
SheetModel::column_read_only(col);
SheetModel::set_column_read_only(col, bool);
```

`begin_edit`, `commit_edit`, `stage_edit`, `stage_value` and `paste` refuse a
read-only target.  A transaction staged before a column became read-only fails
atomically at `apply_transaction`.  The field-based stages
(`stage_field_edit` / `stage_field_value`) treat a field as read-only only when
**every** column showing it is read-only.

**Decision, unmeasured.** MS Project locks some fields (identity/computed
columns, some values on summary tasks).  No evidence for that rule has been
captured, so the sheet does not infer it; the host marks a column read-only.

### 2. Selection ranges

```cpp
struct CellRange { row_begin, row_end, column_begin, column_end, valid; ... };
SheetModel::selection_range();            // normalized rectangle, valid=false without one
SheetModel::select_range(r0, r1, c0, c1); // endpoints in either order; clamped to visible cells
SheetModel::selected_cells();             // visible cells only, row-major
```

`select_range` uses the same clamping as `extend_to`: an endpoint on a hidden
row/column moves to the nearest visible one at or above/left, else the first.
The older `selected_rows()` remains and still returns visible rows only.
Hidden rows/columns inside a range are never selected or copied.

### 3. Edit commands and transactions

```cpp
struct EditCommand {
    std::size_t row;
    std::size_t column;    // model column at stage time, kNoIndex when staged by field
    FieldId field;
    CellKind kind;         // resolved target kind, never Auto after staging
    std::string text;      // authoritative when is_value == false (parsed at apply)
    Value value;           // authoritative when is_value == true
    bool is_value;
};

bool begin_transaction(UndoKind kind = UndoKind::Transaction, std::string label = {});
bool stage_edit(row, column, text);                    // visible editable column
bool stage_field_edit(row, field, text, kind = Auto);  // any field, valid row
bool stage_value(row, column, Value);                  // typed value
bool stage_field_value(row, field, Value, kind = Auto);
bool push_command(EditCommand);                        // lowest-level; resolves Auto kind
EditOutcome validate_staged(index);                    // parse/preview, no write
bool set_staged_text(index, text);
bool set_staged_value(index, Value);
bool discard_staged(index);
TransactionResult apply_transaction();
void cancel_transaction();
```

The semantics that matter:

* **Atomic.** `apply_transaction` is a complete validation pass (parse every
  text command, check typed-kind compatibility, read-only state, duplicate
  targets), and only then a write pass.  A failure writes **nothing**, records
  no undo step, and leaves the transaction open so the UI can correct the
  `failed_index` entry with `set_staged_text` / `set_staged_value` or cancel it.
* **Typed values per kind.** Text commands parse as `EditCommand::kind`, which
  is resolved at stage time from the column (or the descriptor storage kind for
  a field-only command, `field_cell_kind`).  A typed value is accepted only when
  it fits the target kind: Null clears; Integer fits Integer/Number/Cost/Percent;
  Number fits Number/Cost/Percent; Text/Date/Duration/Bool fit only themselves.
  A Duration is never silently treated as an Integer (units would be ambiguous).
* **No duplicate targets.** Two commands for the same `(row, field)` are
  rejected before any write; their order would otherwise be ambiguous.
* **One undo step.** A successful transaction records one `UndoStep` holding one
  `UndoEntry` per changed cell (unchanged commands are validated but not
  recorded).  Undo reverses the entries in reverse write order.
* **Empty transaction** applies successfully with `changed == 0` and records no
  step.
* A transaction and a single-cell edit session cannot be open together;
  `begin_transaction` is refused while `editing()` and `begin_edit` is not
  special-cased beyond the read-only check.
* `transaction_kind()` / `transaction_label()` are exposed for menu text;
  `TransactionResult::{ok, staged, changed, failed_index, failed_cell, error}`.

### 4. Undo/redo steps, coalescing, and labels

```cpp
enum class UndoKind { Cell, Typing, Transaction, Paste };
struct UndoStep { UndoKind kind; std::string label; std::vector<UndoEntry> entries; };

EditOutcome commit_edit(bool coalesce_with_previous);
std::size_t undo_depth();        // user-level steps (was: entries; equal for single edits)
std::size_t undo_entry_count();  // raw cell changes
const UndoStep* next_undo_step();
std::string undo_label();        // "Edit Duration", "Paste 2 cells", ...
bool undo(); bool undo(std::size_t steps);
bool redo(); bool redo(std::size_t steps);
```

**Coalescing rules (documented, unmeasured against Project):**

1. Only a top step of kind `Cell` or `Typing` may absorb a new commit, and only
   when that step's last entry targets the same `(row, field)`.
2. The absorbed commit replaces the step's `after` value, keeps its original
   `before`, and changes the kind to `Typing`.
3. If the run returns to its original value (`before` equals the new `after`),
   the entry — and the step, if it becomes empty — is dropped: there is nothing
   to undo.
4. Any other action (a different cell, a transaction, a paste) starts a new
   step, even when `coalesce_with_previous` is true.
5. The model has **no clock**.  The shell decides where a typing run ends and
   passes `true` for continuation commits, `false` otherwise.  `undo(n)` /
   `redo(n)` apply whole steps, not entries.

A new action clears redo, including a coalesced continuation.

**Decision, unmeasured.** MS Project's exact coalescing window and its undo
labels are not evidenced.  This is a defensible policy that the shell can drive.

### 5. Clipboard (copy / paste)

```cpp
struct ClipboardCell { row_offset, column_offset, field, kind, Value value, std::string text };
struct ClipboardBlock {
    rows, columns, std::vector<ClipboardCell> cells;
    bool empty(); const ClipboardCell* at(r, c);
    std::string to_text(); static ClipboardBlock from_text(std::string_view);
};

ClipboardBlock copy_selection();
PasteResult paste(const ClipboardBlock&, row, column, PasteMode = Values);
PasteResult paste(const ClipboardBlock&, PasteMode = Values);  // at the selection's top-left
```

* **Copy** takes the selected rectangle, visible rows/columns only, and stores
  both the typed `Value` and the display `text` for every cell.  Hidden rows and
  columns close their gap, so offsets are contiguous.  Empty without a
  selection.  `to_text()` is TSV with RFC-4180-style quoting for a field
  containing a tab, newline or quote; `from_text()` parses that form, including
  quoted newlines, CRLF and a trailing newline.  The TSV path is display text
  and therefore lossy for an odd tick count; in-process copy/paste uses `value`
  and is exact.
* **Paste target.** The explicit overload requires a visible target cell.  The
  no-argument overload uses the selection's normalized top-left cell.
* **Row mapping.** Block row offsets walk *visible* row ordinals from the target,
  so a paste never writes to a hidden row.  Rows past the last visible row are
  skipped and counted in `PasteResult::skipped`; a paste never appends a row.
* **Column mapping.** For each block column, the first visible target column
  with the same `FieldId` wins; if the sheet has no such column it falls back to
  the target column's visible ordinal + offset.  Columns with no target are
  skipped.  This is why copy B2:C3 and paste at D6 still puts Name in Name and
  Duration in Duration.
* **Modes.** `PasteMode::Values` writes the typed value when the source and
  target kinds match, and otherwise parses the source's own rendering as the
  target kind.  `PasteMode::Text` parses the copied display text.  The two differ
  only on a lossy rendering: 7,500 ticks (1.5625 days) pasted as Values stays
  7,500; pasted as Text re-parses "1.56 days" to 7,488.
* **Null source** clears the target (`Null`), in both modes.
* **Atomic.** A parse failure, an incompatible typed value or a read-only target
  fails the whole paste with nothing written; `failed_index` indexes
  `block.cells`.  A successful paste is one `UndoKind::Paste` step and becomes
  the new selection (the pasted target rectangle).
* Pasting is refused while a transaction or an edit session is open.

**Decision, unmeasured.** Field-first-then-positional column mapping, the
visible-row walk, skipping instead of appending, and selecting the pasted range
afterwards are spreadsheet/Project-like choices, not captured behaviour.
`spec/ui-inventory.md` §8.2.3 is explicit that the sheet's interaction semantics
(selection model, inline editing, drag/drop, keyboard map) are native code and
are *not* in the resources, so there is nothing in the resource spec to copy.

### 6. Row / outline operations — deliberately not added

Indent/outdent, insert/delete and move are **not** implemented.  `RecordSet`
(`core/view/record_set.h`) has `add_row` and `clear` but no insert, erase,
reorder or outline-level mutation, and `core/view` is another stream's file.
The sheet can therefore represent a row's outline level only as a per-row
override (`set_outline_level`) over the existing order; it cannot insert, delete
or move rows, and it must not fake them.  When `RecordSet` grows those
operations, indent/outdent and move can be layered on top without changing the
sheet's edit/undo model (they would be transaction kinds with their own undo
entries).

## Decisions and evidence status

| Choice | Status | Why |
|---|---|---|
| Read-only is host configuration, not inferred | decision, unmeasured | No oracle evidence for Project's lock rule |
| CellRange endpoints clamp to visible cells | decision, unmeasured | Consistent with `extend_to`; keeps hidden cells out |
| Transaction validation is a full pass before writes | implementation choice | Gives exact rollback; tested in `test_transaction_rollback` |
| Duplicate `(row, field)` targets are rejected | decision, unmeasured | Ambiguous write order; safer to refuse |
| Coalescing window is shell-driven, model has no clock | decision, unmeasured | Keeps the model deterministic and testable |
| `undo_depth()` counts user steps, `undo_entry_count()` raw cells | additive semantics | Existing single-cell tests are unchanged (one entry = one step) |
| Paste maps field first, then visible ordinal | decision, unmeasured | Matches Project's cross-table field identity better than position alone |
| Paste walks visible rows and never appends | decision, unmeasured | Matches a grid with collapsed rows; avoids silent row creation |
| Paste selects the pasted rectangle | decision, unmeasured | Typical spreadsheet behaviour |
| TSV quoting (tab/newline/quote) | decision, unmeasured | Makes the shell's text pasteboard round-trip |
| No row insert/delete/indent | blocker in another stream | `RecordSet` has no such operation today |

Everything else (formatter output, parser grammar, outline visibility, geometry)
is covered by the existing W16 evidence in `sheet.h` and `memory/sheet.md`.

## Test map (new cases)

| Test | Covers |
|---|---|
| `test_selection_range` | normalized ranges, reversed endpoints, clamping to visible rows/columns, `selected_cells`, copy skipping hidden cells |
| `test_clipboard_text` | TSV quoting/round-trip, CRLF, empty fields, uneven rows, missing offsets |
| `test_transaction` | begin/stage/validate/apply, typed percent/cost/bool, read-only refusal, field staging, push_command, set_staged_value, empty transaction, one undo step |
| `test_transaction_rollback` | parse failure leaves everything unwritten and the transaction open; correction; duplicate targets; incompatible typed value; typed Null; read-only introduced after staging; cancel; `validate_staged` |
| `test_paste` | field-first mapping, clipping, positional fallback, Values vs Text on a lossy duration, TSV, null clearing, hidden-row walk, read-only atomicity, no selection / open transaction, one undo step |
| `test_undo_coalescing` | same-cell merge, `Typing` kind, no-op run dropped, split commits, `undo(n)`/`redo(n)`, paste does not merge, redo cleared, labels |

## Open

1. The decisions above need oracle captures when the coordinator can run
   Project: a paste across columns with a different table layout, a paste into
   collapsed rows, a read-only-locked field, and Project's own undo grouping
   (one keystroke vs one commit).  Until then they are marked unmeasured.
2. Row insert/delete/move and indent/outdent wait on `RecordSet` support (or a
   coordinator decision to extend it), as above.
3. `field_cell_kind` maps storage kinds only.  `Task.Duration` is stored as i32
   ticks, so an empty Auto duration column still edits as Integer; a table must
   declare `CellKind::Duration`.  This is unchanged from W16 and remains the
   sharpest display-vs-storage trap in the sheet.
4. No sort/filter/group, no summary rollups in cells, no wrapped text — all
   unchanged from W16.
5. `ClipboardBlock::from_text` drops characters between a closing quote and the
   next delimiter instead of erroring.  Total parser, deliberately lenient; a
   stricter mode can be added if a shell needs to diagnose malformed pasteboards.
