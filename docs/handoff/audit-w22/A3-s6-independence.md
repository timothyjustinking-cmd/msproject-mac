# A3 - S6 "83/83 independent oracle" independence audit (wave 22, decision 7)

Read-only audit of git HEAD e252041. Nothing was built; one existing read-only
tool (`build/g1-reader-dump/Release/reader_dump.exe --links/--tasks`) was run on
`corpus/project_a.mpp` to count link types, lag units and linked tasks. The clean
capture `corpus/reference/2026-10-23-msproject/project-gantt-loaded-clean.png`
was viewed to check the pinned literals.

## Verdict: PARTLY INDEPENDENT (the 83/83 sweep itself is TAUTOLOGICAL for formatting and reading)

The "independent mpp::load_file oracle" is the same reader, the same adapter and
the same formatting function the app uses: the test's expected string for every
one of the 83 rows is `msqt::predecessors_text(plan_input_from_document(mpp::load_file(f)), uid, SheetFormat{})`
and the app's shown string is the output of exactly that call chain written into
the RecordSet by `PlanDocument::fill_computed_columns()`. The sweep therefore
proves only the wiring (fill -> RecordSet field id -> model display column ->
unchanged text pass-through, for every row), not the link syntax, the lag text,
the link order or the reader's decode. What IS independent of the app's code is
(a) the 12-row pinned table (21 non-empty literal strings) and the 5 pinned
percents, of which 15 Pre/Suc strings and all 5 percents match the clean
capture exactly; the 4 lagged strings are the app's own "days" rendering, not
the capture's "d"; one is clipped in the capture; one (task 42) is not in the
capture at all; and (b) `case_computed_text_rules()`, a real unit test of the
formatter on a constructed plan, but with expected strings authored by the same
lane (FF/SF/negative-lag syntax is inferred, not measured). Resource Names is
checked blank-vs-blank only (corpus: the column is not even displayed; fixture:
0 of 3 rows name a resource); its only positive proof is the constructed-plan
unit test. The corpus gate is registered only when the git-ignored
`corpus/project_a.mpp` exists, so the 83/83 claim is local-only and never runs
in CI.

## Q1. Where the gate is, and when it is registered

* Test source: `app_qt/tests/stored_table_test.cpp` (981 lines). Target
  `qt_stored_table_tests`, built from that one file
  (`app_qt/CMakeLists.txt:252-254`).
* Two ctest entries share the binary:
  * `qt_stored_table` - always registered, fixture + out-dir only
    (`app_qt/CMakeLists.txt:256-260`). Runs `case_computed_text_rules()`,
    `case_fixture_is_entry()`, `case_synthetic_non_entry()`
    (`stored_table_test.cpp:972-974`).
  * `qt_stored_table_corpus` - registered ONLY inside
    `if(EXISTS "${QT_REPO_ROOT}/corpus/project_a.mpp")`
    (`app_qt/CMakeLists.txt:262-272`); it passes the corpus path as argv[3]
    and `main()` calls `case_corpus()` only when argv[3] is non-empty
    (`stored_table_test.cpp:970, 975-977`).
* `corpus/` is git-ignored (`.gitignore:26-27`), so the corpus entry never
  exists on CI; `docs/handoff/wave22-complete.md:25,30-32` confirm CI is 14/14
  with the corpus entry absent. Every "83/83" number is from the operator's box.

## Q2. Who formats what - the two code paths

App path (what the sheet shows):

1. `MainWindow::open_file()` -> `doc_.load()` (`app_qt/main_window.cpp:794-800`).
2. `PlanDocument::load()`: `mpp::load_file(source_path)` then
   `app::plan_input_from_document(document)` -> `loaded_` -> `base_`
   (`app_qt/plan_document.cpp:196-203`); `format_ = core::sheet::SheetFormat{}`
   (`plan_document.cpp:218`); then `build_gantt(zoom)` (`:230`).
3. `PlanDocument::build_gantt()`: `view_ = app::sync_plan_from_records(base_, records_)`
   (`plan_document.cpp:245`); `sync_plan_from_records` starts with
   `PlanInput input = base;` and only rewrites task scalars from the RecordSet
   (`app/layout_model.cpp:343-401`), so `view_.links/resources/assignments`
   are byte-identical copies of `loaded_`'s. Ends with `fill_computed_columns()`
   (`plan_document.cpp:282`).
4. `PlanDocument::fill_computed_columns()` calls
   `predecessors_text(view_, uid, format_)`, `successors_text(...)`,
   `resource_names_text(view_, uid)` and writes the text into the RecordSet
   under `kPredecessorsField/kSuccessorsField/kResourceNamesField`
   (`plan_document.cpp:285-319`).
5. The formatter: `link_list_text()` -> `link_entry()` -> `endpoint_id()`,
   `link_type_suffix()`, `link_lag_text()` -> `core::sheet::format_duration_text`
   (`plan_document.cpp:60-133`; `core/sheet/sheet.cpp:766`).
6. The model paints `sheet_->cell_text(row, column)` for DisplayRole
   (`app_qt/sheet_table_model.cpp:209-219`); the delegate explicitly does NOT
   paint the three kinds (`app_qt/sheet_views.cpp:469`).

Test "oracle" path (what the gate expects):

1. `load_plan(path)`: `mpp::load_file(path)` then
   `app::plan_input_from_document(document)` (`stored_table_test.cpp:430-447`).
   The test's own comment says it: "read straight from its bytes by the SAME
   reader the window used" (`stored_table_test.cpp:416-417`).
2. Expected Pre/Suc text for each displayed row:
   `msqt::predecessors_text(plan, uid, format)` / `msqt::successors_text(...)`
   with `const core::sheet::SheetFormat format;` (`stored_table_test.cpp:782,
   806-811`). Same function, same default format as `plan_document.cpp:218`.
3. Expected % Complete: `QString::number(task->percent_complete) + "%"`
   (`stored_table_test.cpp:800-803`) - this string is constructed by hand, so
   the `CellKind::Percent` formatting (`plan_document.cpp:478-491`,
   `core/sheet` format_by_own_kind) IS independently checked; the value itself
   comes from the same reader.

Same reader: yes (`mpp::load_file`, `plan_document.cpp:196` vs
`stored_table_test.cpp:433`). Same plan builder: yes
(`app::plan_input_from_document`, `plan_document.cpp:197` vs
`stored_table_test.cpp:441`; `app/reader_adapter.h:130-172`). Same link
formatter: yes (`msqt::predecessors_text`/`successors_text`,
`plan_document.cpp:135-143`, used at `plan_document.cpp:303/308` and
`stored_table_test.cpp:807/810`). The window's `view_` differs from the
oracle's `plan` only by task scalars synced from the RecordSet (no edits are
made in `case_corpus`), so the two inputs are identical.

Consequence: for the 83-row sweep, expected == actual by construction for any
reader output and any formatter behaviour. What the sweep does prove, per row:
the record->uid mapping (`record_row` / `row_uid`), that `fill_computed_columns`
writes every row under the stored field ids, that
`display_column_for_field()` finds the file's Pre/Suc/% Complete columns, that
a Text value survives the SheetModel/`app::cell_text` path unchanged, and that
`rows_with_links >= 60` and `blank_predecessors > 0`
(`stored_table_test.cpp:819-822`). That is wiring coverage, not value coverage.

Independent parts: (1) the pinned literals at `stored_table_test.cpp:844-857`
and `:885-891` (compared against the SHOWN text only, `:869-876, 903-904`);
(2) `case_computed_text_rules()` (`:507-578`), a genuine unit test of the
formatter on hand-built links/assignments, whose expected strings were written
by the lane (the FS-bare / FS+lag / SS / no-space rules are measured from the
capture; FF, SF and negative lag are not - `plan_document.h:67-70`,
`s6.md:265-266`); (3) `oracle.null_resource_word_rows` counted from
`assignment.resource_uid_raw` (`:434-440`) - raw-word evidence, but still the
same reader's decode.

## Q3. The 12 pinned rows vs the clean capture

`kPinned` (`stored_table_test.cpp:844-857`), 12 rows, 23 slots, 21 non-empty
strings (Suc for id 42 is `nullptr` = not checked). Checked against
`project-gantt-loaded-clean.png` (rows 0-40 visible; rows 38-40 partly
covered by the "New tasks are created in Auto Scheduled mode" toast; row 41+
below the fold):

| id | pinned Pre | pinned Suc | capture Pre | capture Suc | status |
|---|---|---|---|---|---|
| 3 | "" | "4" | (blank) | 4 | exact |
| 4 | "3" | "5" | 3 | 5 | exact |
| 5 | "4" | "" | 4 | (blank) | exact |
| 9 | "8" | "10FS+50 days" | 8 | 10FS+50 d | Pre exact; Suc unit label differs ("days" vs "d") |
| 10 | "9FS+50 days" | "11" | 9FS+50 d | 11 | Pre unit label differs; Suc exact |
| 17 | "15" | "18,33FS+20 days,56,57,25" | 15 | 18,33FS+20 (clipped) | Pre exact; Suc: first 10 chars match, the rest (",56,57,25" and the unit) is from the file's link table, not the capture |
| 33 | "17FS+20 days" | "42SS,34" | 17FS+20 d | 42SS,34 | Pre unit label differs; Suc exact (this is the ONLY SS evidence in the capture) |
| 34 | "33" | "39" | 33 | 39 | exact |
| 35 | "46" | "40,36" | 46 | 40,36 | exact (order evidence) |
| 36 | "52,35" | "39,37" | 52,35 | 39,37 | exact (order evidence) |
| 37 | "36" | "38" | 36 | 38 | exact |
| 42 | "33SS,57" | (not checked) | NOT VISIBLE | NOT VISIBLE | not in the capture; derived from link uid 7437 (10807->10757 SS) and 7470 (10851->10757 FS) in the reader dump |

Tally: 15 of 21 non-empty strings match the capture character-for-character;
4 (the lagged ones) are the app's own `days` label where Project prints `d`;
1 is only partially visible; 1 is outside the capture. All 7 strings the brief
lists ("9FS+50 days", "33SS,57", "17FS+20 days", "3", "10FS+50 days",
"18,33FS+20 days,56,57,25", "40,36") are in `kPinned` (ids 10, 42, 33, 4, 9,
17, 35). The 5 percent pins (`:885-891`: 13->57%, 1->99%, 12->29%, 5->100%,
10->0%) all match the capture, even though the test comment (`:879-880`) says
they were taken from "the coordinator's render of this file", not the capture.

Note that the pinned lag strings encode the current app rendering: the capture
reads "9FS+50 d". `s6.md:268-271` admits the `days`/`d` divergence, but
`s6.md:248-249` and `memory/s6.md` ("12 pinned Pre/Suc strings ... match the
capture") state the opposite. `app_qt/plan_document.cpp:67-68` claims "the
capture shows '33SS' for a lag-free SS predecessor (task 42's Pre in the
reference file)" - task 42 is not in the capture; the capture shows "42SS" in
task 33's Suc. `s6.md:104` itself says 42 is "below the capture's fold".

## Q4. Coverage counts; Resource Names vacuity

From `reader_dump --links` on the corpus (88 live links, all `pred_known`/
`succ_known`): 61 distinct successor uids (tasks with a non-empty Pre), 60
distinct predecessor uids (non-empty Suc), 67 distinct tasks with either;
83 - 61 = 22 blank Pre. This matches the gate's printed
`83 rows checked, 67 with a predecessor/successor, 22 blank Pre`
(`s6.md:194`). So 16 of the 83 rows are blank-vs-blank in both columns, and
the sweep's value assertions on the other 67 are tautological as explained in Q2.

Resource Names:
* Corpus: the gate asserts the column is NOT displayed
  (`resources_column == -1`, `stored_table_test.cpp:772-773`), so no shown cell
  is ever compared. The remaining checks are that every assignment's mapped
  `resource_uid == 0`, that the raw words are all null/zero, and that
  `resource_names_text()` returns "" for every task (`:927-946`). That is a
  proof of "nothing to show", i.e. vacuous for rendering.
* Committed fixture (CI): "Who" IS display column 7 (`:623`) but the fixture's
  3 assignment rows all carry the null word (gate print `s6.md:181`), so every
  row is blank-vs-blank (`:653-656`). Likewise "Depends On": the fixture has 0
  links (`s6.md:179-180`), so the CI-visible Predecessors check is
  blank-vs-blank too; the only positive requirement (`rows_with_links > 0`,
  `:663-665`) is guarded by `if (!plan.links.empty())` and never fires.
* The only positive Resource Names proof is `case_computed_text_rules()`
  (`:571-577`: "Alice, Bob", "Alice", "", ""), a unit test of the pure
  derivation on a constructed `app::PlanInput`. Nothing anywhere renders a
  non-empty Resource Names cell through the sheet. `wave22-complete.md:127`
  ("Pre/Suc/Resource Names/% Complete values now render") overstates this.

## Q5. Regressions that would pass the gate

The corpus has: 87 FS + 1 SS link, 3 lagged links (96000/139200/240000 ticks =
20/29/50 working days), and `lag_units == 7` (working days) on all 88 rows
(reader dump). File link order equals ascending link-uid order (0 out-of-order
rows in the dump). Hence:

1. Lag units. `app::LinkRow` carries `lag_ticks` only (`app/layout_model.h:121`);
   `plan_input_from_document` drops `mpp::Link::lag_units`
   (`app/reader_adapter.h:152` copies `lag_ticks`, nothing else; the reader
   exposes `lag_units`/`lag_is_elapsed()` at `core/mpp/reader.h:676-693`).
   `link_lag_text()` always prints through `format_duration_text`, which picks
   the largest unit that fits (`core/sheet/sheet.cpp:766-796`). So an elapsed
   lag ("+2 edays"), an hours lag stored as hours ("+8 hrs" -> app "+1 day"),
   a weeks lag ("+1 wk" -> app "+5 days") or a percent lag ("+50%") would all
   be mis-rendered, and the oracle would agree because it is the same code.
   No corpus row and no constructed-plan case covers any of these.
2. SS/FF/SF letters. SS is pinned on the corpus (33's Suc "42SS,34"). FF and SF
   exist only in `case_computed_text_rules()` (`:528-529, 559-562, 567-568`)
   with lane-authored expectations; a reader regression mapping type 0/2 to the
   wrong enum would pass everything (same reader both sides; the enum names
   0=FF, 2=SF are themselves "inference from the documented enum",
   `core/mpp/reader.h:255-258`).
3. Negative lag (lead) "-2 days": constructed plan only; no corpus row.
4. Link order. A regression that sorts links by task ID would be caught by the
   35/36 pins ("40,36" -> "36,40"). A regression that sorts by link uid, or
   any reordering inside the reader, would pass: the file's order already equals
   uid order, and the oracle re-derives from the same reader.
5. Bare-FS rule. A regression that printed "4FS" for a lag-free FS would be
   caught by the pins and the unit test. A regression that dropped "FS" from a
   lagged entry ("9+50 days") would be caught by the pins.
6. Separator. "," vs ", " in Pre/Suc is pinned; ", " inside Resource Names is
   unit-test only.
7. `name[50%]` units suffix in Resource Names: not implemented, not tested
   (`plan_document.h:69-70`, `s6.md:267-268`).
8. Lag unit label itself ("days" vs Project's "d"): pinned to the app's
   current output, so the gate enforces the divergence rather than detecting it.
9. Any reader regression in link liveness (FixedMeta coverage, dead endpoints)
   changes both sides equally; only `rows_with_links >= 60` (`:821`) bounds it.

## Q6. Other overclaims in s6.md relative to the code

* `s6.md:247` "the reader is the oracle, loaded independently in the test" and
  `s6.md:313-315` / `memory/s6.md` "the fill path cannot self-certify" - true
  only for the RecordSet fill/wiring. The reader, the adapter and the formatter
  are shared; the test's own comment at `stored_table_test.cpp:416-417` says
  "the SAME reader". `wave22-complete.md:75-76` repeats "independent
  mpp::load_file oracle" without that qualification.
* `s6.md:248-249` "12 pinned exact strings match the clean capture's rows
  (... 42's Pre)" - 42 is not in the capture (contradicted by `s6.md:104`);
  the 4 lagged strings do not match the capture's text; 17's Suc is clipped.
* `app_qt/plan_document.cpp:67-68` attributes "33SS" to the capture's task 42;
  the capture shows "42SS" on task 33.
* `s6.md:69` / `wave22-complete.md:74-75` "SS/FF/SF always print" presented as
  measured syntax; FF/SF are inferred (`s6.md:265-266` admits it).
* `s6.md:9` "byte-level assertions" for the two blank columns: SUMMARY is
  asserted as "the RecordSet has no 0x0b400051 field and every cell is empty"
  (`stored_table_test.cpp:912-921`) - an assertion about the app's output, not
  about the file's bytes; nothing reads Flag10's bit. The byte-level part is
  the assignment null word, read through the same reader.
* `wave22-complete.md:127` "Pre/Suc/Resource Names/% Complete values now
  render": Resource Names has never rendered a non-empty value in any test or
  on any file (Q4).
* `s6.md:20` "every row of the corpus file's Pre/Suc/% Complete ... 12 pinned
  exact strings": the pinned table is 12 rows / 21 strings, 11 Pre + 10 Suc;
  the count "12" is rows, not strings.
* The % Complete sweep is the one place the expected string is built
  independently of core::sheet (`:800-803`), so the `CellKind::Percent` change
  is genuinely gated; this is correctly described.
* Minor: `MainWindow::open_file` calls `doc_.apply_preferences(prefs_)` AFTER
  `doc_.load()` has already run `fill_computed_columns()` with the default
  `SheetFormat` (`main_window.cpp:800, 868`; `plan_document.cpp:218-230,
  323-330`). A non-default persisted duration label would therefore apply to
  Pre/Suc lag text only after the next `build_gantt()`, not on open. The gate
  cannot see this because the test runs with default preferences and compares
  against the default format.

## Summary of what is actually proven

| claim | status |
|---|---|
| Pre/Suc text reaches the sheet under the file's stored field ids, every row | proven (wiring) |
| Pre/Suc syntax = Project's (bare FS, FS+lag, SS, "," no space, file order) | proven for 15 capture-matched strings on 11 visible rows + 2 order rows; FF/SF/lead/elapsed/hours/weeks/percent lags unproven |
| 83/83 rows "against an independent oracle" | tautological: same reader, adapter, formatter, format |
| % Complete printed with "%" | proven (independent string construction, 83 rows + 5 capture-matched pins) |
| Resource Names renders | not proven anywhere; derivation unit-tested on a constructed plan only |
| SUMMARY blank with byte reason | proven blank; "byte reason" is documentation, not an assertion on bytes |
| CI coverage of a non-empty derived value | none (0 links, 0 named assignments in the fixture; corpus entry not registered in CI) |
