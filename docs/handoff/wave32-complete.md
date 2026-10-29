# Wave 32 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-32-<group>-COMPLETE` line is appended below (wave-32-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the wave-31 verifier findings (High only)

From `docs/handoff/wave31-verification/review.md` (0 Critical, 3 High).

| item | finding | status | evidence |
|---|---|---|---|
| W32-0.1 | R31-1 (High): queued percent/actuals/Start edits bypass the file-wide automatic-outline refusal | **DONE** | commit `4834b40`; gates `build/w32/W32-0.1-*.log` |
| W32-0.2 | R31-2 (High): typed and pasted Start saves through a writer the Fill pair shows cannot match Project | **DONE** | commit `290a73a`; gates `build/w32/W32-0.2-*.log` |
| W32-0.3 | R31-3 (High): Sort/Filter range selection, Copy, Cut and Paste act in file order | **DONE** | commit `5650b53`; gates `build/w32/W32-0.3-*.log`; R31-4 folded in |

No G0 item was deferred.  Each item is a refusal or a disable with the reason in
plain words (decision 9); no new save path or Project pair was built.  The group
raised no new finding.

W32-0.1 detail:

- `make_outline_edit` refuses an automatic indent or outdent while a percent or
  actuals edit is pending on any row, or while any task start edit is pending.
- `StructuralEdits::push` refuses `set_percent` and `update_actuals` while any
  slack-carrying outline edit is applied, on every row, not only the moved
  subtree and anchor; the narrow rule remains for manual edits.
- `save_structural_edits` repeats both refusals before the first write,
  including the Start half `push()` cannot see (a Start edit is not a
  `StructuralEdit`).
- The Qt shell names the pending Start uids when indent/outdent runs and reports
  the automatic-outline refusal in plain words for the percent, update,
  mark-on-track and Task Information paths.
- Tests: `check_pending_edit_cross_refusals()` builds the committed 3-row
  automatic control in both orders for percent, actuals and a typed Start, and
  proves a manual indent is still accepted.  `app_edits_outline_tests`: 3225
  checks.

W32-0.2 detail:

- The shared sheet policy makes Start read-only in both shells
  (`task_column_editable`, `apply_edit_policy`); Task Information and the Task
  Form disable the field with the decision-9 reason; Cut refuses a selection
  touching Start whole; Paste refuses a block covering Start whole (the core
  `SheetModel` refuses before staging); the save funnel and
  `save_structural_edits` refuse a collected or writable start edit.  The writer
  stays in the tree and remains tested through the direct RecordSet/document
  path.
- Tests: the app selection/views/document/app-edit/smoke suites are updated to
  the refusal and to the direct-RecordSet diff; `qt_clipboard` proves the 2x2
  whole-block refusal and the Cut refusal; `qt_save_reopen` proves no `.mpp`
  written after a refused attempt differs from the null save, while a Text1
  edit still persists.

W32-0.3 detail:

- `PlanDocument::sort_reorders_rows()` compares the sorted visible rows with the
  unsorted visible rows: an ID ascending sort that leaves the file order alone
  stays allowed, while ID descending is refused.
  `PlanDocument::display_order_differs_from_file()` adds any active filter.
- Range Copy/Cut (more than one cell), multi-row Paste and Shift-click range
  extension are disabled and refused in the handler with the remedy ("clear the
  sort and filter first").  Indent, Outdent and Insert are disabled and refused
  while a sort reorders the rows ("clear the sort first"; R31-4).
- Single-cell copy/cut/paste and single-cell edits stay: `clicked_cell()` maps
  the display row to its record, so the edit reaches the record shown there.
- Tests: `qt_clipboard` drives the real window under an ID-descending sort and
  under an active filter.  In each state a 3-row paste is refused whole with
  every Name unchanged (forced through the handler), range Copy/Cut are refused,
  a single-cell edit on a non-project display row changes exactly the mapped
  record and no other, and Shift-click keeps the one-cell selection and the
  anchor.  Insert/Indent/Outdent are disabled under the sort, and Insert is
  enabled again after clearing it.

Per-commit gates (logs under `build/w32/`, named `W32-0.x-*.log`; every item's
scan reports HEAD 0 files / 0 hits and fixtures OK):

| commit | builds | tests |
|---|---|---|
| W32-0.1 `4834b40` | app build exit 0; app_qt build exit 0 | app ctest 25/25; `app_edits_outline_tests` 3225 checks; app_qt ctest 37/37 |
| W32-0.2 `290a73a` | app build exit 0; app_qt build exit 0 | app ctest 25/25; app_qt ctest 37/37 |
| W32-0.3 `5650b53` | app_qt build exit 0 (the app tree is untouched) | app_qt ctest 37/37 |

W32-0.3 additionally scanned its staged working-tree candidate (`build/w32/W32-
0.3-scan.log`) because the commit message quotes a pre-commit scan; both HEAD
and the candidate report 0 files / 0 hits.  The pre-push `guard_names
290a73a..HEAD` run is `build/w32/W32-G0-guard.log`, exit 0.

G0 group gates (fresh trees configured from scratch on `123ec83` under
`build/w32-fresh{,-app,-qt}`, log `build/w32/W32-G0-fresh-gates.log`; the only
difference from `5650b53` is this file's marker line):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 25/25 / 37/37 passed locally (35/35 in CI: `qt_stored_table_corpus` and `qt_task_order_corpus` are registered only where `corpus/project_a.mpp` exists) |
| `check_fixtures.py` / oracle provenance | fixtures OK (139 documented hashes, 173 tracked `.mpp`); paths OK (222 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave31..HEAD` / `guard_names wave31..HEAD` | 0 files, 0 hits / 0 commits / exit 0 |

The run started 14:54 and finished 15:09 with no lock retries.

The numbers belong to `123ec83`, the marker commit; its code tree is `5650b53`,
and the record commit that follows is docs-only.

## Found, not fixed

- Nothing new.  G0 raised no Critical, High, Medium or Low finding beyond the
  three items above.
- The wave-31 review's Medium/Low findings R31-5..R31-12 remain queued in
  `plan-waves27-30.md` ("Queued from the wave-31 review"); R31-4 was folded into
  W32-0.3 and is closed there.
- CLOSE is out of scope for this group session: the fresh-tree gates on the
  final commit, the review pass over `git diff wave31..wave32`, the push and the
  CI dispatch belong to CLOSE (wave-32-brief section 2).

WAVE-32-G0-COMPLETE

## CLOSE - the wave record

The wave ran as one group plus this close, one fresh session each: G0 closed the
wave-31 verifier's three High findings (R31-1..R31-3) and folded R31-4 into
W32-0.3.  Every item is DONE; none was deferred, and every fix is a refusal or a
disable with a test at each entry point and in each order.  No new writer and no
new Project measurement was built: the wave is fix-only, as the brief says.  The
CLOSE work was three CLOSE fixes (W32-C1..W32-C3), the five fixes the
independent review required (W32-C4..W32-C8), the fresh-tree gate run, both
privacy scans, the review pass over `git diff wave31..wave32`, the push and the
CI dispatch, this record, START-HERE, INDEX, `memory/coordinator.md` and the
regenerated backlog (W32-C2).

The record below belongs to the final compiled tree `7727760`; the CLOSE docs
commit after it changes docs only.

### The CLOSE fixes

| id | finding | commit | gates |
|---|---|---|---|
| W32-C1 | `b16a9fa` rewrote `docs/handoff/plan-waves27-30.md` with CRLF while adding the wave-31 queued-findings note, against the standing LF rule; the file's committed bytes carried 14 CRLF lines | `17e24ef` | docs only, no build; `build/w32/W32-C1-scan.log`: `git diff --check HEAD` exit 0; scan HEAD and candidate tree `d5f8e1e` 0 files / 0 hits; the line-ending diff reports old CR 0 / new CR 0 and the 14 added lines; CRLF audit wave31 vs candidate 373 / 373, 0 added, 0 removed; fixtures OK (139 documented hashes, 173 tracked `.mpp`) |
| W32-C2 | The regenerated backlog was required to state the wave's refusals (Fill, Start editing, the sort-time commands) in their why column; six commands the wave now refuses read `implemented` with an empty reason and Start editing had no row | `017ef44` | generator and regenerated doc only, no build; `build/w32/W32-C2-gates.log`: regenerate exit 0 and idempotent, 114 ribbon / 51 menu rows; backlog-vs-pin 114/114, 0 mismatches; `git diff --check` exit 0; scan HEAD and candidate tree `26e68b5` 0/0; fixtures OK (139/173, 222 paths, 8 oracles); CRLF audit 373/373, 0 added |
| W32-C3 | Three texts still said Start is written after W32-0.2 made it read-only: the Task Information General hint, the `dialogs.h` comment and the wave-19 `docs/parity.md` record of `qt_save_reopen` | `64fa2fc` | `build/w32/W32-C3-gates.log`: app_qt build exit 0; app_qt ctest 37/37 exit 0; scan HEAD and candidate tree `cbe8e28` 0/0; fixtures OK (139/173, 222 paths, 8 oracles); `git diff --check` exit 0; CRLF audit 373 / 372, 0 added, 1 removed (`docs/parity.md`, whose last line was the file's only CRLF) |
| W32-C4 | Review H1: `save_reopen_test.cpp` stored the int `entry_column_index` in a `std::size_t`, so `>= 0` was vacuous and two int parameters tripped MSVC C4267 and GCC/Clang `-Wtype-limits` under `-Werror`; CI configures app_qt with warnings as errors, so all three `qt.yml` jobs would have failed on the first push | `71383a6` | `build/w32/W32-C4-gates.log`: app_qt reconfigured with `CORE_WARNINGS_AS_ERRORS=ON` and rebuilt, exit 0, no C4267 diagnostic; app_qt ctest 37/37 exit 0; fixtures exit 0; scan HEAD 0/0; `git diff --check` exit 0; CRLF audit 373 / 372, 0 added |
| W32-C5 | Review M2: order 1 was tested with percent and Start but not a real `update_actuals` edit, so `push()`'s applied-actuals scan and `make_outline_edit`'s pending-actuals path were unforced, while the brief asks for each edit kind in each order | `786024a` | `build/w32/W32-C5-gates.log`: app reconfigured with `CORE_WARNINGS_AS_ERRORS=ON` and rebuilt, exit 0; app ctest 25/25 exit 0; `app_edits_outline_tests: 3230 checks passed`; fixtures exit 0; scan HEAD 0/0; `git diff --check` exit 0; CRLF audit 373 / 372, 0 added |
| W32-C6 | Review M4: the macOS save funnel collected Start edits and passed them to the writer with no W32-0.2 refusal, and both shells still carried Start-save texts that said the writer stores the dates | `3d0c80f` | `build/w32/W32-C6-gates.log`: app_qt build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0; app_qt ctest 37/37 exit 0; fixtures exit 0; scan HEAD 0/0; `git diff --check` exit 0; CRLF audit 373 / 372, 0 added. `app/main.mm` is Apple-only and not compiled on Windows; the macOS `qt-shell` CI job builds `msproject_app` and is its compile gate |
| W32-C7 | Review M3: the sort-time Insert/Indent/Outdent refusals were proved only through disabled QActions, but ribbon controls stay enabled and call the handlers directly | `0e6e74c` | `build/w32/W32-C7-gates.log`: app_qt build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0; app_qt ctest 37/37 exit 0 (`qt_clipboard` 1.29 s); fixtures exit 0; scan HEAD 0/0; `git diff --check` exit 0; CRLF audit 373 / 372, 0 added |
| W32-C8 | Review M1: no test drives `save_structural_edits` with a percent/actuals edit beside an automatic outline; the pair is unbuildable through `push()` (both scans refuse), so the save check is defence for a state the API cannot produce | `7727760` | `build/w32/W32-C8-gates.log`: app build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0; app ctest 25/25 exit 0; `app_edits_outline_tests: 3230 checks passed`; fixtures exit 0; scan HEAD 0/0; `git diff --check` exit 0; CRLF audit 373 / 372, 0 added |

W32-C1 restores the file's line endings without touching its content; the
queued-findings text `b16a9fa` added is unchanged.  W32-C2 gives the six
refused commands and Start editing the shell's own words: a range copy/cut, a
multi-row paste, Indent/Outdent, Insert Task and Start editing each say why the
shell refuses them and what clears the refusal ("clear the sort and filter
first", "clear the sort first", "not measured against Project").  Fill keeps its
W31-C1 measurement reason.  The pin counts do not move: 114 ribbon rows (69
disabled) and 51 menu rows (7 disabled), 114/114 command controls agree with the
pin, and the committed bidirectional checker stays green.  W32-C3 makes the two
shell texts and the saved record say what the test proves: Start is read-only
and refused, the sheet writes Name/Duration/Finish, and `qt_save_reopen` saves a
queued Text1 custom-field edit.

W32-C4..W32-C8 answer the independent review (below), which landed while CLOSE
was in progress.  The High was a real CI failure that the local fresh run hid
because its app/app_qt trees were configured without `CORE_WARNINGS_AS_ERRORS`;
the fix and the runner change close that hole.  The four Mediums it marked
required are fixed: the order-1 actuals test, the macOS funnel refusal and the
Start-save texts, the ribbon handler clicks, and a recorded reason for the
save-level defence the queue API cannot reach.  The Lows are queued with
directions in "Found, not fixed".

### Errata from the record audit

The CLOSE review read the G0 section against the code and the logs.  Three
statements are corrected here; no earlier claim of a gate result is withdrawn.

- **W32-0.1 detail, pending Start uids.**  The G0 section says "The Qt shell
  names the pending Start uids when indent/outdent runs".  The shell collects
  them (`collect_start_edits` -> `pending_start_uids`) and passes them into
  `make_outline_edit` and `StructuralEdits::push`; the refusal text names the
  kind of edit ("a task start edit is pending in the same save") and the reason,
  not the affected rows.  The behaviour and its test are unchanged.
- **W32-0.2 detail, read-only vs disabled.**  The G0 section says "Task
  Information and the Task Form disable the field".  Task Information sets the
  Start field read-only (`start_->setReadOnly(true)`, `dialogs.cpp`); the Task
  Form sets it read-only and disabled (`task_form.cpp`).  Both refuse the edit;
  the wording now matches the widgets.
- **G0 per-commit exit lines and the guard log.**  The G0 per-commit logs under
  `build/w32/W32-0.*` record the command output but no exit line (0 of 22
  logs), and `build/w32/W32-G0-guard.log` is zero bytes.  The G0 results were
  observed in the session, but the logs do not retain their exit codes; the "exit
  0" in the G0 row for the guard is not evidenced by that file.  From W32-C1 on,
  every CLOSE log writes its own exit lines, and the CLOSE preflight and the
  fresh-tree run below capture exits for the whole branch range.  No log was
  reconstructed.

### Fresh-tree gates (final compiled tree)

The final compiled tree is `7727760`.  `build/w32/run-close-fresh.ps1`
configured three trees from scratch (`build/w32-close-fresh`,
`build/w32-close-fresh-app`, `build/w32-close-fresh-qt`) with
`CORE_WARNINGS_AS_ERRORS=ON` on core, app and app_qt, as `qt.yml` does, and ran
every gate; the log is `build/w32/W32-CLOSE-fresh-gates.log`, started 16:15 and
finished 16:33.  All 22 stage exit lines are 0.

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed, exit 0 |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail `constraint-mso`), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app configure/build/ctest | exit 0 / exit 0 / 25/25 passed, exit 0; the fresh app tree's `app_edits_outline_tests` reports 3230 checks, exit 0 (`build/w32/W32-CLOSE-fresh-app-edits-outline.log`) |
| app_qt configure/build/ctest | exit 0 / exit 0 / 37/37 passed locally (35/35 in CI; the two corpus-gated tests above), exit 0 |
| `check_fixtures.py` / oracle provenance | fixtures OK (139 documented hashes, 173 tracked `.mpp`); paths OK (222 tracked test-data files, no local path); provenance OK (8 oracles), exit 0 |
| backlog vs pin | 114 pin command controls, 114 backlog ribbon rows, 0 mismatch(es), exit 0 |
| client-name scan HEAD / messages `wave31..HEAD` / `guard_names wave31..HEAD` | 0 files, 0 hits / 0 commits / 0 hits, exit 0 |
| CRLF audit `wave31..HEAD` | 373 CRLF files at `wave31`, 372 at HEAD; 0 added, 1 removed (`docs/parity.md`), exit 0 |

The tree is the same one the pre-push preflight checked on `7727760` (all
checks OK, `build/w32/W32-CLOSE-push-preflight-7727760.log`), so the numbers
belong to the code tree that is pushed.  The first full run on `0c0e014`
(before W32-C1..W32-C3) is preserved as
`build/w32/W32-CLOSE-fresh-gates-run1.log`, and the run on `64fa2fc` (before the
review's fixes W32-C4..W32-C8, and with app/app_qt configured without
warnings-as-errors) as `build/w32/W32-CLOSE-fresh-gates-64fa2fc.log`; neither is
the record's tree.

### Privacy

The final tree is clean under both scans and the hex grep: the line-based
`scan_client_names.py HEAD --messages wave31..HEAD` reports **0 files / 0
hits** and **0 commit messages**, the whitespace-normalised `guard_names.py
wave31..HEAD` and `guard_names.py HEAD --not --remotes=origin` are **0 hits,
exit 0**, and `git grep -nE "client-string-[0-9a-f]{8}"` has no match (exit 1,
expected).  Wave 32 adds no fixture: the tree still has 139 documented hashes
and 173 tracked `.mpp`, and `check_fixtures.py` passes.  The known residuals are
unchanged and belong to the operator's decision: two pre-wave fixtures and 69
tracked text files carry the operator's name, the 13 unsalted FNV-1a hashes
removed in wave 27 remain in older wave-26 commits plus
`tests/data/project_a_golden.tsv`, and R31-11 (one wave-31 brief line naming the
operator) stays queued in `plan-waves27-30.md` as the brief directs.  The two
scans are both required and both were run for every CLOSE commit.

### Independent review of `wave31..wave32`

An independent agent reviewed `git diff wave31..wave32` with no edits, no
staging and no build; it first read `017ef44` and re-read the range at
`64fa2fc`, so W32-C1..W32-C3 and the whole G0 diff are covered.  The verdict is
**0 Critical, 1 High, 4 Medium, 6 Low**; all five findings it marked "required
before CLOSE pushes" are fixed in W32-C4..W32-C8, and the Lows are queued with
directions.  It also checked, with no finding, the W32-0.1/0.2/0.3 source, the
save paths beyond M4, the range-operation refusals, unused symbols (the two new
`constexpr`s and every new helper are used; `has_automatic_outline_edit` is
read by the shell), fixtures, names and local paths beyond L5, counts and
claims, LF/CRLF, C1..C3, and the macOS indent/outdent pending lists.

- **H1 (fixed, W32-C4).**  `save_reopen_test.cpp` stored the int
  `entry_column_index` in a `std::size_t`, so `>= 0` was vacuous and the int
  parameters at two call sites tripped MSVC C4267 (visible in the CLOSE
  fresh-gate log) and GCC/Clang `-Wtype-limits`/`-Wtautological-...` under
  `-Werror`.  CI configures app_qt with `CORE_WARNINGS_AS_ERRORS=ON`, so all
  three `qt.yml` jobs would have failed on the first push; the local fresh run
  was green only because its app/app_qt trees were configured without the
  flag.  The local is now `int`, W32-C4 builds app_qt with warnings as errors,
  and `run-close-fresh.ps1` now configures app and app_qt with the flag too.
- **M2 (fixed, W32-C5).**  Order 1 (percent/actuals/Start queued first, then
  the automatic outline) was tested for percent and Start but not with a real
  `update_actuals` edit, while the brief asks for each edit kind in each order
  and the G0 record claims it.  The test now queues a real date update on uid 3
  first and proves both `push` and `make_outline_edit` refuse the automatic
  indent and name the actuals edit.
- **M4 (fixed, W32-C6).**  The macOS save funnel collected Start edits and
  passed them to the writer with no W32-0.2 refusal, and both shells still
  carried Start-save texts that said the writer stores the dates.  The macOS
  funnel now refuses a collected Start edit before the save prompt with the
  decision-9 reason; the unreachable warn-and-save block and the "Wrote N task
  start(s)" texts are gone from both shells, as are the status, discard
  prompt, About and Table Date Format claims, and the "nothing to save" texts
  no longer name a Start edit.  The writer and its result fields stay in the
  tree.  The funnel change is compiled by the macOS `qt-shell` job, not on
  Windows.
- **M3 (fixed, W32-C7).**  The sort-time Insert/Indent/Outdent tests proved
  only the disabled `QAction`s, but ribbon buttons are statically enabled and
  call the handlers directly.  The gate now clicks the live Task-tab ribbon
  controls under the ID-descending sort and proves each handler refuses with
  "clear the sort first", queues no edit and leaves the row count alone.
- **M1 (defence recorded, W32-C8).**  No test drives `save_structural_edits`
  with a percent/actuals edit beside an automatic outline.  That pair cannot
  be built through `StructuralEdits::push`: the forward scan refuses the
  outline while a percent/actuals edit is applied and the reverse scan refuses
  either edit while an automatic outline is applied, so unlike the W31 link
  case there is no stale-plan argument that builds it.  The save check stays as
  defence, and its comment now records why the queue API cannot produce the
  state.
- **L1..L6 (queued).**  L1 the refused three-row paste asserts Names (Duration
  cells are the data) -- force the paste and assert Duration stays; L2 the
  Cut-Start test selects one cell, not a range spanning Start; L3 the Task
  Information Start refusal does not prove it runs before Name/Duration are
  applied (add a Name change to the same edit); L4 the refusal texts say a
  filter "changes the row order" when a filter hides rows (the real test is
  display-vs-file coordinates); L5 is the repository's own `D:\dev\...` path in
  the brief, already recorded as wave-30 U7 and not a client path; L6 the Qt
  funnel's non-empty Start refusal is not forced by a test -- set the RecordSet
  field before `save_document_path`.

### Push and CI

The final code tree `7727760` was pushed to `origin/wave32` without force (a new
branch, `git push -u origin wave32`) after
`build/w32/W32-CLOSE-push-preflight-7727760.log` reported **ALL PREFLIGHT
CHECKS OK** (scan HEAD and messages 0 files / 0 hits; both `guard_names` ranges
exit 0; the hex grep exit 1 as expected; fixtures, the backlog check and
`git diff --check` exit 0; CRLF audit 0 added).  No history was rewritten.

Both workflows were dispatched explicitly on `wave32` with `workflow_dispatch`
(the `workflow_run` trigger fires only from the default branch), and every job
is green on the pushed tree:

| workflow | run | jobs | result |
|---|---|---|---|
| `qt.yml` | **40456047971** | windows-qt, linux-qt, macos-qt | all `success` |
| `ci.yml` | **40456051113** | windows-msvc, ubuntu-gcc | all `success` |

The macOS job builds `msproject_app` from `app/main.mm`, so it is the compile
gate for the W32-C6 funnel refusal; the Linux and macOS Qt jobs build app_qt
with `-Werror`, so they are the gate for the W32-C4 column-index fix.  No run
was red, so there was no fix-and-re-dispatch.  The CLOSE docs commit that
follows changes docs only and is pushed after its own preflight.

### The 16 decisions

Wave 32 is fix-only and re-scores nothing: all 16 decisions stand as wave 31's
record states them.  Decision 9 is stronger in what it *says*, not in what it
counts: the backlog now names the wave-32 session refusals beside the six
commands and gives Start editing a field-refusal row, while the 114/51 command
counts and the 69/7 disabled counts are unchanged and the bidirectional pin
check stays green.

### Found, not fixed

- **The carried review queue.**  The wave-31 review's R31-5..R31-12 remain
  queued in `plan-waves27-30.md`; R31-4 is closed here as part of W32-0.3.
  R31-11 (the one wave-31 brief line naming the operator) is deliberately not
  reopened by this fix-only wave.
- **The G0 evidence gap.**  The G0 per-commit logs did not capture exit lines
  and `W32-G0-guard.log` is empty; the errata record it, and the CLOSE range
  evidence is the preflight and fresh-tree logs above.
- **No new writer or measurement.**  The Start writer stays in the tree for a
  later measured wave; Fill stays disabled on its W31-1 measurement; sort,
  table, outline and zoom remain session-only.  Nothing in this wave moves a
  save path.
- **The independent review's Lows (L1..L6).**  Directions are in the review
  section: force the Duration assertion on the refused three-row paste (L1);
  force a range Cut spanning Start, not one cell (L2); add a Name change to the
  Task Information Start refusal to prove the order (L3); correct the filter
  half of the refusal wording, which says a filter "changes the row order" when
  it hides rows (L4); the repository's own `D:\dev\...` path in the brief (L5,
  already recorded as wave-30 U7 and not a client path); and force the Qt
  funnel's non-empty Start refusal by setting the RecordSet field first (L6).
  None is gating: every one is a test-strength or wording gap, not a reachable
  wrong write.
- **The final tree moved twice.**  The first CLOSE fresh run was on `64fa2fc`
  and the review's High/Medium fixes landed after it; the record's fresh gates
  above are the re-run on `7727760`, and the earlier log is preserved as
  `build/w32/W32-CLOSE-fresh-gates-64fa2fc.log`.

WAVE-32-COMPLETE
