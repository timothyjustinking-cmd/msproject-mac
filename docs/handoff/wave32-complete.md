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
| app ctest / app_qt ctest | 25/25 / 37/37 passed |
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
