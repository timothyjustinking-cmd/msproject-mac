# Wave 33b brief - fix-only: the wave 33 review's High (for DeepSeek, coordinator role)

_Written 30 Oct 2026 by the Opus coordinator.  Baseline: branch `wave33b`, cut from
`wave33` after its verification record (`docs/handoff/wave33-verification/`).  Source:
`docs/handoff/wave33-verification/review.md` (findings R33-1 to R33-9) and the post-replay
CI run of `wave33`.  `docs/handoff/standing-rules.md` binds this wave.  The repo lives at
`D:\dev\msproject-mac`.  This wave adds no writer, lifts no refusal and measures no new
Project pair: every change is a refusal, a test or a record._

## 0. Publication

The coordinator pushes `wave33b` and dispatches both CI workflows AFTER CLOSE; CLOSE does
neither.  Never rewrite or force-push history and never work round the pre-push guard.
Before the first push, check for unused functions and constants that GCC and Clang reject.

## 1. Rules for this wave

`standing-rules.md` plus the wave 33 group rules: groups in order, one fresh session each,
the marker line `WAVE-33b-<group>-COMPLETE` (CLOSE: `WAVE-33b-COMPLETE`) appended to
`docs/handoff/wave33b-complete.md` and committed when the group is done, one finding id per
commit (R33-n, or CI-n for a CI failure), gates logged per commit under `build/w33b/`,
deferral only with evidence of an attempt.  Each behaviour fix has its test seen RED before
the fix and GREEN after; quote both runs in the commit message from the log.  Never name
the operator or anyone else in docs, commits, fixtures or tests; refer to rows by ID.  Run
`python -I build/audit-w22/guard_names.py main..HEAD` and
`python scripts/check_markers.py --range main..HEAD` before every commit that adds a
fixture or a record.  LF line endings everywhere.

## 2. Groups

### G0 - the review's High, and any CI failure

- **W33b-0.1  (R33-1, High)  Table / Date Format writes an unmeasured table.**  W33-1.6
  (A-11) made the Qt dialog carry the table the view shows
  (`app_qt/dialogs.cpp:316` `edits.table_index = table_index_`, called from
  `app_qt/main_window.cpp:4402`), so File > Save now writes the `214/CTable` date-format
  byte of ANY table.  Project's own save of this edit is measured for table index 0 only
  (`tests/viewstate_tests.cpp:2261-2320`); `docs/handoff/w64.md:318` lists "`table_index`
  other than 0" as unmeasured.  Rule 1 forbids the write.  Fix: when the shown table index
  is not 0, refuse Table / Date Format in the Qt shell with the decision-9 reason in the
  status line (the command is disabled or refuses before the dialog opens; the
  `request_view_state_edits` path gets a backstop that refuses a non-zero `table_index`
  for a date-format edit, the way W33-1.3 refuses an unmeasured SET).  Keep the index
  plumbing so a later measured pair can lift the refusal.  Check the macOS shell
  (`app/main.mm:3736` sets `table_index = 0`): if its view can show a table other than the
  first, it must refuse the same way rather than writing table 0 while another table is
  shown (that is A-11 on macOS; keep the change mechanical, CI compiles it).  Tests: the
  Qt test with a second table applied asserts the refusal and that the saved file is
  byte-identical to the source; a model-level test asserts the backstop; the table-0 edit
  still writes and still matches the measured pair.  Correct the CLOSE sentence in
  `docs/handoff/wave33-complete.md` that says the wave "added no writer" (add an erratum
  line; do not rewrite the record's history).
- **W33b-0.2  (CI-n)  Any red job in the post-replay CI run of `wave33`.**  The
  coordinator adds the failing job, step and log excerpt here before launch (one item per
  failure).  If the section below is empty, CI was green and this item is "none".

_CI failures from the `wave33` run (coordinator fills in before launch):_ none recorded.

### G1 - the review's Mediums

- **W33b-1.1  (R33-2, Medium)  Formula CLEAR is still live in the save hook.**  W33-1.4
  disabled `Document::clear_custom_formula` and the shell buttons, but
  `apply_formula_edit` (`app/document.cpp:249-296`) still runs
  `clear_custom_field_formula` and the `custom_field_value_remains` flag heuristic
  (`:128`, `:283`) for an `Operation::clear` reaching `app::apply_document_edits` /
  `save_structural_edits`.  Fix: refuse `Operation::clear` there with the decision-9
  reason (target untouched).  Change `test_cascade_with_document_edits_in_one_write`
  (`app/document_tests.cpp:1306-1324`) to assert the refusal and an unchanged target
  instead of "the formula clear was written".
- **W33b-1.2  (R33-3, Medium)  macOS Task Fields leaves refused edits queued.**  The
  handler at `app/main.mm:3414-3423` pushes onto the live queue; when a later edit in the
  same batch is refused, the earlier ones stay queued and the next Save writes edits the
  user was told failed.  Fix: validate the whole batch first (or push onto a copy and
  commit it only when every edit is accepted), matching the Qt shell's behaviour.  Keep
  it mechanical; CI compiles it.  Record the before/after behaviour in the commit message.

### CLOSE

Fresh-tree gates on the final commit, logged (core, app with `CORE_WARNINGS_AS_ERRORS=ON`,
app_qt, M2, engine suite, check_fixtures, check_markers); both name scans and the marker
check clean on `main..HEAD`; one review pass over `git diff wave33..wave33b`; record
"push and CI: by the coordinator" in `wave33b-complete.md`; update START-HERE and
`memory/coordinator.md` (wave 33 + 33b, not merged until CI is green and the review has
no Critical/High).  Then append `WAVE-33b-COMPLETE`.

## 3. Queued (not for this wave)

The review's Lows R33-4 to R33-9 and the CLOSE review's ten Lows (already queued in
`plan-waves27-30.md`).  Add R33-4 to R33-8 to that queue with their file:line from
`review.md` in CLOSE; R33-9 closes when the post-replay CI run is green.
