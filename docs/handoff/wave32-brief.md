# Wave 32 brief - fix-only: the wave-31 High findings (for DeepSeek, coordinator role)

_Written 2026-10-27 by the Opus verifier session.  Baseline: branch `wave32`, cut
from `wave31` (its verification commit 3a18e64).  The operator asked for a short
fix-only wave: close R31-1, R31-2 and R31-3 from
`docs/handoff/wave31-verification/review.md`, refusing first, so the stack
wave27..wave32 can merge into main.  No new features.
`docs/handoff/standing-rules.md` binds this wave.  The repo lives at
`D:\dev\msproject-mac`; the neutral root for any Project save is `C:\w32\`._

## 0. Publication

Push `wave32` and dispatch both CI workflows in CLOSE; fix and re-dispatch on red
(check for unused functions/constants GCC and Clang reject before the first push).
Never rewrite or force-push history; never work round the pre-push guard.

## 1. Rules for this wave

`standing-rules.md` plus the group rules of waves 26-31: groups in order, one fresh
session each, the marker line `WAVE-32-<group>-COMPLETE` appended to
`docs/handoff/wave32-complete.md` and committed when the group is done, one finding
id per commit, gates logged per commit under `build/w32/`.

**Refuse first.**  Each fix below is a refusal or a disable, in the edit model or
the shell, with a test for each entry point and each order.  Do not build new
writers or measure new Project pairs in this wave.  A refusal's message names the
reason in plain words (for example "clear the sort first").  Tests that need a
committed fixture fail when it is missing (no SKIP-and-pass).  Do not quote check
counts in a commit message unless the cited log shows them.  Never name the
operator or anyone else in docs, commits or fixtures.

## 2. Groups

### G0 - wave-31 verifier findings (High only)

- **W32-0.1  (R31-1, High)  Queued edits bypass the file-wide automatic-outline
  refusal.**  `check_automatic_outline_file` reads only the file on disk, so a
  percent, Mark on Track, Update or typed-Start edit queued in the same save as an
  automatic outline edit gets past it, in either order.  Fix, in both orders:
  (a) refuse an automatic outline edit (in `make_outline_edit`/`push`) while any
  `set_percent`, `update_actuals` or Start edit is queued on any row;
  (b) refuse a percent, actuals or Start commit while an automatic outline edit is
  queued, on every row, not just the moved subtree and anchor
  (`outline_edit_pending_for` is too narrow for this);
  (c) mirror both in `save_structural_edits` before the first write.
  A test for each order and each edit kind, on the committed 3-row automatic
  control (indent uid 2 under uid 1, then 40 % on uid 3; and the reverse; and the
  same with a typed Start on uid 3).

- **W32-0.2  (R31-2, High)  Typed and pasted Start save through a writer the Fill
  pair shows cannot match Project.**  `save_date_edits` writes only the constraint
  window and the manual bit; the committed `wave31-fill*.mpp` pair shows Project
  also rewrites the calculated and effective dates, slack, the project-row rollup,
  a Var2 record and DSI.  Apply rule 1: make the Start column read-only in the
  sheet (`task_column_editable`), refuse Paste and Cut into Start, and refuse Start
  in Task Information / Task Form and any other shell entry point that reaches
  `collect_start_edits`, with the decision-9 reason ("not measured against
  Project").  Keep the edit model's start-edit path refusing as defence in depth
  (an explicit refusal in `push` for a start edit, with a test).  The writer itself
  stays in the tree for a later wave.  Test: the column is not editable, a paste
  block covering Start is refused whole (no partial write), and no `.mpp` saved
  after the attempt differs from the null save.

- **W32-0.3  (R31-3, High)  Under session Sort (or Filter), range selection, Copy,
  Cut and Paste act in file order.**  The core sheet's selection and `paste` walk
  record order, while the screen shows `ordered_display_rows()`, so a multi-row
  paste writes, and saves, rows other than those shown.  Refuse first: while a
  non-ID sort or any filter is active, disable multi-row Paste, Cut, Copy of a
  range and Shift-click range extension (single-cell edits of the clicked row stay,
  after checking that the clicked display row maps to the right record).  Also
  refuse Indent, Outdent and Insert while a non-ID sort is active ("clear the sort
  first"; this is R31-4, same root cause, cheap to close here).  Tests under an
  ID-descending sort and under a filter: a 3-row paste is refused and leaves every
  row unchanged; a single-cell edit on display row k changes exactly the record
  shown at row k; Shift-click does not extend.  If mapping through
  `display_rows()` is genuinely trivial and fully tested you may do that instead of
  disabling, but the refusal is the default.

### CLOSE

Fresh-tree gates on the final commit, logged (core, qt, app, M2, engine suite,
check_fixtures); the name scan clean; a review pass over `git diff wave31..wave32`;
push and dispatch as section 0 says; `wave32-complete.md`, START-HERE, INDEX,
`memory/coordinator.md` and the regenerated `docs/ui-command-backlog.md` (Fill,
Start editing and the sort-time commands listed with their reasons).  Then append
`WAVE-32-COMPLETE`.

## 3. Queued

The wave-31 review's Medium/Low findings (R31-5..R31-12) are queued in
`plan-waves27-30.md` ("Queued from the wave-31 review"); R31-4 is folded into
W32-0.3.
