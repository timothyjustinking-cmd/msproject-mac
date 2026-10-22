# memory: app save pipeline -- view state and custom-field formulas (W64)

_2026-10-23. Owner: stream W64. Read this after `memory/app.md` and
`docs/handoff/w64.md`; read `app/document.h`/`app/document.cpp` for the code._

## State

Two previously unwired writers are now reachable from the app's save helper:

- `msproject::viewstate` -- a `214/CTable` date-format edit or one existing
  column replacement;
- `msproject::props` -- a custom-field formula set / whole-table clear in
  `114/Props`, including the measured `0x02401392` "custom-field data in use"
  flag rule.

Both are applied to an in-memory `mppwrite::CfbDocument` and written with one
`CfbDocument::save` (or one `mppwrite::save_date_edits` when start pins ride
along). With nothing pending the container is written back **byte-identically**.

## API (app/document.h)

```cpp
app::ViewStateEdits view;       // set_date_format/date_format OR
                                // replace_column/column_index/field_id/title
app::CustomFormulaEdit formula; // Operation::set(field_id, formula) or ::clear

// In-memory seam for an existing save cascade (field/link/structural writers):
app::DocumentEditOutcome apply_document_edits(mppwrite::CfbDocument&, view, formula);
// ok == false -> a FORMULA refusal; do not save (nothing was written).
// A refused VIEW edit is only a warning; 214/ is untouched.

// Path convenience / one-write pipeline:
app::Document doc(source_path);
doc.request_view_state_edits(view);
doc.apply_custom_formula(0x0b400036u, "[Name]");   // false -> error out param
doc.clear_custom_formula();
app::SaveOutcome out = doc.save(start_edits, target, options);  // or save(target)
```

`SaveOutcome::view_state_written / formula_written /
custom_field_flag_restored` report what happened.

## Pipeline order (do not reorder)

link deletes -> link adds -> task deletes -> task inserts -> field VALUES ->
**formula** -> **view state** -> one container save -> `check_table`.

The formula step must see values written earlier in the same save: it is the
only step that can decide whether `0x02401392` must stay 1. `apply_document_edits`
is the seam for that cascade (`app/edit_model.cpp`, not W64's lane, still has to
call it). `Document::save(start_edits, ...)` covers only start + these two ops.

## What is proved (and where)

`app/document_tests.cpp`, `app_document` (43 -> **169 checks**):
`test_save_view_state_date_format`, `..._column`, `test_save_without_edits`,
`test_save_formula_set`, `..._clear_with_values`, `..._clear_value_free`,
`test_save_refused_view_state`, `test_document_request_validation`,
`test_save_formula_and_view_state`, `test_save_start_date_with_view_and_formula`.

- date edit: CTable Var2Data stream byte 11 (payload +7, 0xff -> 0x05) only;
  no other `214/` stream; reopen parses, date_format 5, 9 columns;
- column edit: index 1 record (bytes 131..245 of the single-block Var2Data)
  changes, 9 columns and last index 8 unchanged, Text1 title reads back;
- no-edit: whole file byte-identical (SHA-256 same as source);
- formula set: `extract_formulas` -> Text2 `[MSPJ188743694]`, flag 1, `214/`
  untouched; external `props_report --formulas` agrees;
- clear with a Text1 value: 8-byte empty formula record, flag **1** (patched
  back), reader still returns Text1 "alpha";
- clear value-free: flag **0**, `114/Props` unchanged;
- bad column index: save ok, warning, `214/` byte-identical;
- combined date+formula+view-state save: one write, all three present on reopen.

Commands (build tree `build\w64-app`):
`cmake -S app -B build\w64-app ... -DAPP_BUILD_TESTS=ON
-DCORE_WARNINGS_AS_ERRORS=ON`, `cmake --build ... --config Release`,
`ctest --test-dir build\w64-app -C Release`. Last observed: **15/15 tests,
2,513 checks, 0 failures**, `/W4 /WX` clean. W63's `app_smoke` (98 checks) is the
15th test; W64's delta is `document_tests` +126.

Fixture `tests/data/synthetic-base.mpp` is never written; SHA-256
`55C109F2F62FD6B9F05789AB002C35991A424AAB18C78F92E4200DF7B3FC6069` re-checked
after the runs.

## Established facts a successor needs

- **`core/mpp/cfb.h` cannot write.** It is read-only. The write surface is
  `mppwrite::CfbDocument::replace_stream(path, bytes)` (same-size reuse of
  sectors; growth appended). `core/mpp/reader.*` was not touched.
- **The flag repair is an app-side two-byte patch.** `core/props` has no public
  setter. Code: `set_custom_field_flag()` in `app/document.cpp`: parse with
  `props::parse_props`, require id `0x02401392`, kind 2, length 2, write the u16
  at `record.offset + 12`. A clean core change would be
  `props::set_custom_field_flag()`; not available.
- **Value detection** (`custom_field_value_remains`): Task VarMeta key is a
  measured custom id (`mpp::custom_field_kind`), OR a task row's Flag1 bit
  (FixedMeta entry +24 & 0x02, entries 3..), OR an assignment VarMeta key
  >= `0x0f40403c`. Unreadable table -> "yes" (keep 1). Conservative on purpose;
  stale cleared entries can keep it 1.
- **The view edits are applied to a copy of the parsed `ViewState`**, so one
  refusal (bad index/title) writes neither edit.
- **Append a column is not offered** (W58: not measured; W62 is adding
  `append_table_column` separately). Only existing columns can be replaced.
- `Document::save` validates `target != source` unless
  `SaveOptions::allow_source_overwrite`, and re-opens the written file requiring
  `reread.to_bytes()` to equal what was written.
- `app/CMakeLists.txt` has W64's three authorised viewstate lines
  (`VIEWSTATE_BUILD_TESTS OFF`, `viewstate` in the foreach, `core_viewstate` in
  `app_layout`). W63's `smoke.cpp` entries coexist.

## Open, ranked

1. **Shell wiring**: `app/main.mm` currently calls none of this (W63's lane).
   Either use `app::Document` for a start+formula+view-state save, or call
   `app::apply_document_edits` inside the full `save_structural_edits` cascade
   after the field-value writers.
2. **Oracle**: coordinator opens one W64 output in Project (table format, formula
   dialog, "custom fields in use"). Nothing in this stream proves Project
   accepts the file.
3. When W62's `append_table_column` lands, decide whether the app exposes adding
   a column; the current `ViewStateEdits` only replaces.

## Traps

- Dropbox mtime churn breaks `edit`/`write` ("file changed since it was read",
  occasional `PermissionError`). Write `<file>.new` then `Move-Item -Force`, or
  use a Python anchor script under `build\w64-scratch\`.
- Do not turn the flag off without the value check; W59 measured value-only
  projects with an empty formula table and flag 1.
- Do not run the view edit on the live `ViewState` before knowing the column
  edit is valid; use the copy.
- `save_date_edits` proves only its own streams changed relative to the
  `CfbDocument` it receives -- replacing `114/Props`/`214/` first is safe and is
  what the combined overload does.
