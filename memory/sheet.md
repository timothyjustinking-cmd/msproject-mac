# memory: sheet stream (W16 + W34)

_2026-10-22. Owner: `core/sheet/`, `tests/sheet_tests.cpp`, `memory/sheet.md`.
No git commands were run; everything is on disk in the main checkout for the
coordinator to commit._

Read this, then `core/sheet/README.md` (map + decisions + verification),
`core/sheet/sheet.h` (the contract is in its comments), and
`docs/handoff/view.md` for the model the sheet sits on.

## State: implemented, built and tested

| File | Contents |
|---|---|
| `core/sheet/sheet.h` | `SheetModel`, `SheetColumn` (+`read_only`), `CellKind`, `CellStyle`, `CellRef`, `CellRange`, `EditOutcome`, `EditCommand`, `TransactionResult`, `UndoEntry`/`UndoStep`/`UndoKind`, `ClipboardCell`/`ClipboardBlock`, `PasteMode`/`PasteResult`, `SheetFormat`, `SheetMetrics`, `ColumnSegment`, free formatters/parser |
| `core/sheet/sheet.cpp` | formatting per kind, parsing per kind, outline visibility, selection + ranges, single-cell editing, transactions, clipboard/TSV, undo/redo with steps and coalescing, column/row geometry |
| `core/sheet/README.md` | the new public surface, transaction/paste/coalescing rules, decisions marked "unmeasured", test map, verification commands |
| `core/sheet/CMakeLists.txt` | self-contained `core_sheet` library + `sheet` test; works under the top-level build and standalone (`-S core/sheet`) |
| `tests/sheet_tests.cpp` | **1,052 checks**, no corpus, no MS Project needed (was 597; +455) |

No drawing, no toolkit types, no dependencies beyond `core/view` (and through it
`core/model`). The model holds a `RecordSet&`; it is deliberately non-copyable
because committing an edit writes through to that RecordSet.

## W34: what is new and what it guarantees

Additive only: no existing type, method or field was renamed or removed.
`SheetColumn` gained `bool read_only` at the end.

| New surface | Guarantee / behaviour | Test |
|---|---|---|
| `CellRange`, `selection_range`, `select_range`, `selected_cells` | normalized rectangle; endpoints in either order; endpoints clamp to visible cells like `extend_to`; hidden rows/columns never selected | `test_selection_range` |
| `copy_selection`, `ClipboardBlock`, `ClipboardCell` | visible rows/columns only, contiguous offsets, typed `Value` + display `text` per cell | `test_selection_range`, `test_paste` |
| `ClipboardBlock::to_text` / `from_text` | TSV with RFC-4180 quoting for tab/newline/quote; CRLF, quoted newlines, doubled quotes, trailing newline | `test_clipboard_text` |
| `paste(block, row, col, mode)`, `paste(block, mode)` | atomic; one undo step; field-first then visible-ordinal column mapping; visible-row walk; never appends; Values vs Text; null clears; read-only fails atomically; selects the pasted rectangle | `test_paste` |
| `begin_transaction` / `stage_edit` / `stage_field_edit` / `stage_value` / `stage_field_value` / `push_command` / `validate_staged` / `set_staged_text` / `set_staged_value` / `discard_staged` / `apply_transaction` / `cancel_transaction` | full validation pass before any write; parse failure, typed-kind mismatch, duplicate `(row, field)` or read-only target writes NOTHING and leaves the transaction open; one undo step (one entry per changed cell); empty transaction applies with changed 0 and no step | `test_transaction`, `test_transaction_rollback` |
| `field_cell_kind(field)` | descriptor **storage** kind as a `CellKind`; `Task.Duration` is i32 ticks so it is `Integer`, not `Duration` | `test_transaction` |
| `UndoKind`, `UndoStep`, `next_undo_step`, `undo_label`, `undo_entry_count`, `undo(n)`, `redo(n)` | one step per commit/typing run/transaction/paste; undo reverses entries, redo replays them; labels are human text | `test_transaction`, `test_undo_coalescing` |
| `commit_edit(bool coalesce_with_previous)` | only a `Cell`/`Typing` top step on the same `(row, field)` merges; merged `after` replaced, original `before` kept; a run returning to its start drops the step; the model has **no clock** — the shell decides | `test_undo_coalescing` |
| `column_read_only`, `set_column_read_only` | host-declared; refused by `begin_edit`, `stage_edit`, `paste`; a column locked after staging fails the apply | `test_transaction`, `test_transaction_rollback`, `test_paste` |

Semantics worth remembering:

* `undo_depth()` now counts **user steps**; `undo_entry_count()` counts raw cell
  changes. For single-cell edits they are equal, so all W16 tests are unchanged.
* `next_undo()` returns the **last** entry of the step undo would apply (the one
  reversed first). Use `next_undo_step()` for the whole action.
* Transactions and a single-cell edit session are mutually exclusive;
  `begin_transaction` is refused while `editing()`.
* Row insert/delete/move and indent/outdent are **not implemented**: `RecordSet`
  has `add_row`/`clear` only (no insert, erase, reorder), and `core/view` is
  another stream's file. `set_outline_level` only changes an existing row's
  level. Do not fake moves in the sheet.

## What the model guarantees (W16, still green)

All of these are exercised in `tests/sheet_tests.cpp`.

| Guarantee | Where test-proven |
|---|---|
| Columns keep table order, fields, widths, alignment, hidden state; `visible_columns()`/`display_column()` are consistent inverses; hidden columns are not laid out | `test_columns`, `test_geometry` |
| Header text is the authored title, else the descriptor name, else the hex id | `test_columns`, `test_table_definition` |
| Duration ticks render as `3 days`, `4 hrs`, `30 mins`, `0 days`, `-3 days`, `1.5 days` at 8 h/day, from configurable unit labels | `test_formatting` |
| A 10-hour day reinterprets the same ticks; date order/separator/2-4 digit year/weekday/time are all settings | `test_formatting`, `test_format_settings` |
| Percent, cost (grouping/decimals/currency/negative), bool and text render per `SheetFormat`; Null always `""` | `test_formatting`, `test_format_settings` |
| Outline level/summary come from `OutlineLevel`/`Summary` with the structural fallback and per-row overrides; collapsing hides every descendant and keeps the summary; `collapse_all`/`expand_all` | `test_outline`, `test_outline_override` |
| `visible_rows()`, `visible_row(ordinal)`, `display_row(row)`, `visible_row_count()` agree; indent is `level * indent_per_level` | `test_outline` |
| Selection is anchor/focus + rectangle; `select_all`; `extend_to`; collapse clamps it; hidden rows/columns never selected | `test_selection` |
| Editing seeds from `cell_text`; `validate_edit` parses without writing; a rejected commit keeps the session open; `cancel_edit` writes nothing | `test_editing`, `test_editing_errors` |
| Typed parsing: duration (`3d`, `3 days`, `4h`, `30m`, `2w`, `1mo`, bare number = days), date (configured/ISO/time), percent 0..100, cost grouping/currency, whole integer, bool words, trimmed text | `test_editing`, `test_editing_errors` |
| Invalid input is rejected with a message naming the kind | `test_editing_errors` |
| A commit writes the typed value through to the RecordSet and records one undo step; an unchanged commit records nothing; a new edit clears redo | `test_editing`, `test_undo_redo` |
| Undo restores Null as Null (not empty text) | `test_undo_null` |
| `CellStyle` data: right for numeric, left for text/date/bool, bold summaries, red critical/overdue | `test_style` |
| Geometry: per-column widths, auto = 0, min floor, layout, row tops, content height, per-row height override | `test_geometry` |
| A configured `view::TableDefinition` (including `defaults::inferred_entry()`) builds a sheet without reinterpretation | `test_table_definition` |

## Verification (last run 2026-10-22, MSVC 19.44, `/W4 /permissive- /utf-8 /WX`)

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w34 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w34 --config Release --target sheet_tests"
& "build\vs-w34\core\sheet\Release\sheet_tests.exe"
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build\vs-w34 -C Release -R "^sheet$" --output-on-failure
```

Result: configure exit 0 (all nine stream subdirectories included), build exit 0
with no warnings, `sheet_tests: 1052 checks passed`, ctest `1/1 Test #24: sheet
... Passed`. **Never `--parallel`.** `build\vs-w34` is this stream's tree.

## Choices made here, not oracle-verified (all "decision, unmeasured")

There is no oracle evidence for any display or interaction convention
(coordinator-only; `spec/ui-inventory.md` §8.2.3 says the sheet's interaction
semantics are native code and are not in the resources).

1. **Duration display**: largest unit that fits, at most 2 decimals; the value in
   the RecordSet is exact ticks, only the text is approximate.
2. **Date parse pivot**: 00-29 → 2000-2029, 30-99 → 1930-1999; three-digit years
   rejected; ISO/4-digit accepted regardless of display order.
3. **Negative durations are rejected on input**; they still format if supplied.
4. **Percent input is 0..100**, stored Integer when whole else Number; cost input
   defaults to two decimals.
5. **A bare number in a duration cell means days**; weeks/months are parse-only.
6. **An empty Auto column** resolves from the descriptor storage kind;
   `Task.Duration` is i32, so an empty duration column needs an explicit
   `CellKind::Duration`.
7. **Overdue** needs a status date; "now" is never inferred.
8. **Undo does not remove a schema field** added by `set_field`; it restores Null.
9. **Read-only is host configuration**, not inferred from field identity or
   summary state. (W34)
10. **Range endpoints clamp to visible cells** and hidden cells are never copied.
    (W34)
11. **Transaction validation is a full pass before writes**, and duplicate
    `(row, field)` targets are refused rather than last-wins. (W34)
12. **Coalescing has no timer**: the shell passes `coalesce_with_previous`.
    (W34)
13. **Paste maps by field first, then by visible column ordinal**, walks visible
    rows, never appends rows/columns, and selects the pasted rectangle. (W34)
14. **Paste Values vs Text**: Values is lossless for a matching kind, Text
    re-parses the display text (lossy for odd tick counts). (W34)
15. **TSV quoting** for tab/newline/quote is our interchange choice; Project's
    own clipboard encoding is unmeasured. (W34)
16. **`undo_depth()` counts user steps**, not entries. (W34, additive semantics)

## Open, ranked

1. **Row insert/delete/move and indent/outdent** are blocked on `RecordSet`
   (no insert/erase/reorder) and on `core/view` ownership. Needs a coordinator
   decision; the sheet must not fake row order. This is the largest missing
   piece of "editing" for a real Gantt.
2. **Default table layouts** are still unevidenced (`core/view/defaults.cpp`,
   another stream's). The sheet consumes whatever table it gets. When they land,
   nothing in `core/sheet` needs to change.
3. **Duration/date display and parse conventions** remain ours. Highest-value
   oracle capture: a real sheet with a fractional duration, a milestone, a
   critical task and a status date.
4. **Interaction semantics for undo grouping, paste mapping and read-only
   fields** are decisions. Oracle recipes needed: paste across a different table,
   paste over collapsed rows, a locked field, and Project's undo granularity.
5. **No sort/filter/group**: the sheet uses RecordSet row order. `view::apply_sort`
   returns row indices; a future `SheetModel` may accept an explicit row order.
6. **No summary rollups in cells**; `core/sched` has rollup support.
7. **Selection is a rectangle only**: no discontiguous selection, keyboard map,
   fill handle or drag semantics. Clipboard now exists, but the shell wires it.
8. **Geometry is a first cut**: no wrapped text, auto columns unmeasured by the
   model; per-row height is a shell decision.
9. **Reader bridge** still maps only eight fields, so a `.mpp`-loaded sheet has no
   OutlineLevel/Summary unless the shell calls `set_outline_level` per row.

## What the macOS shell still has to provide

- Measure text/fonts; `SheetMetrics` and the font are shell-owned.
- Measure auto columns (`width == 0`); the model never measures text.
- Draw the outline gutter/disclosure triangles from `is_summary`,
  `has_children`, `is_collapsed`, `indent_points`.
- Map `CellStyle` to attributes and draw.
- Translate clicks/keys into `select_cell`, `extend_to`, `select_range`,
  `select_all`, `begin_edit` / `set_edit_text` / `commit_edit` / `cancel_edit`.
  Pass `commit_edit(true)` while a typing run continues, `false` when it ends.
- Show `EditOutcome::error` on a rejected commit (session stays open).
- Use `begin_transaction`/`stage_*`/`apply_transaction` for multi-cell forms or
  operations; show `TransactionResult::error` and fix `failed_index`.
- Own the platform pasteboard and call `ClipboardBlock::to_text` / `from_text` to
  bridge it; `copy_selection` / `paste` do the sheet work.
- Provide the `SheetFormat` settings from app options and the file's own
  duration/date settings where recoverable.
- Convert points to its coordinate space (DPI, Retina).

## Traps hit

- **`stage_*/push_command` in a `/* */` comment terminates the comment.** A
  header comment containing `stage_*/push_command` produced a cascade of C3646
  errors at `sheet.h:651`; write `stage_* / push_command` instead.
- **`scan_decimal` grouping bug (fixed in W16)**: the thousands branch skipped
  digits, so `1,234.5` parsed as `1.5`. Regression-tested.
- **Do not compare formatted duration text for equality**; compare parsed ticks
  or the RecordSet value. The text is lossy by design (7,500 ticks → "1.56 days"
  → 7,488 on re-parse; `test_paste` pins the difference).
- **`undo_depth()` changed meaning** in W34: steps, not entries. Use
  `undo_entry_count()` for cells.
- **Paste field-first mapping**: copying B2:C3 and pasting at D6 puts Name in
  Name, not in Start. If a paste "moves" values to unexpected columns, check
  whether the fields exist in the target table; the ordinal fallback only fires
  when the field has no visible column.
- **Paste walks visible rows**: a collapsed child is not written; the next
  visible row receives the following block row. Tests pin this.
- **A failed apply keeps the transaction open** (like a rejected commit keeps the
  edit session open). Call `cancel_transaction` to discard; `staged_count` is the
  proof.
- **Dropbox mtime churn**: `edit`/`write` often fail with "file changed since it
  was read" on this tree. Re-read immediately before each edit; retry.
- **Concurrent build trees**: use `build\vs-w34` and build `--target
  sheet_tests`; if another stream's project breaks an all-target build, this
  target has no dependency on `core/plan`, `core/mpp`, `core/io` or `core/gantt`.
- **MSB6003** on a `.tlog` is Dropbox/AV churn; retry once.
- **Empty-column kind**: see choice 6. An empty Auto duration column with no
  values edits as an integer unless the column declares `CellKind::Duration`.
- **`row_visible` is O(rows)**; use `visible_rows()` once for a layout pass.
- **Treated as read-only?** `field_read_only` is true only when every column
  showing the field is read-only; a field with no column is editable.
