# Wave 35e, group G1: builder record

Branch `wave35e`. One finding id per commit (1.3 carries the three record ids the brief names
together), each with the trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35e)`
(checked by `build/w35e/commit.sh` before the commit and counted after). No writer added, no
refusal of a persisted shape lifted, no pair measured, no COM, no push, no workflow dispatch, no
history rewrite.

Gate tooling as in `gates-g0.md`: `build/w35e/gates.sh <label> qt` for a commit that changes
source, `build/w35e/docgates.sh <label>` (the scans and the fixture check) for a records-only
commit. Logs are `build/w35e/<parent-sha>-W35e-<label>-*.log` and `*-gates.status`.

| Item | Finding | Commit | Status |
|---|---|---|---|
| 1.1 | R35dG1-1 | `39c8e37` | fixed (both bypasses) |
| 1.2 | R35dG1-2, with R35dW-4 | `1da3997` | fixed (record) |
| 1.3 | R35dG1-3, R35dW-2, R35dW-3 | `02f1900` | fixed (records) |
| 1.4 | R35dG0-2 | `f244d02` | fixed (record) |

## Gate line

- 1.1 (`5d733ae-W35e-1.1-*`): core build 0 / 0 warnings, core ctest `-E plan_recalc` 65/65, app
  build 0 / 0 warnings, app 34/34, app_qt build 0 / 0 warnings, app_qt 52/52, fixtures 0, markers
  0, guard_names 0, scan_client_names 0, client-string grep none.
- 1.2, 1.3, 1.4 (records only, `39c8e37-W35e-1.2-*`, `1da3997-W35e-1.3-*`,
  `02f1900-W35e-1.4-*`): fixtures 0, markers 0, guard_names 0, scan_client_names 0, client-string
  grep none, `git diff --check` 0.
- The group's final tree (`f244d02-W35e-G1-record-*`): the full set again, same numbers (65/65,
  34/34, 52/52, 0 warnings, scans 0).

The measured pairs still write and match with effects on (the save-and-compare blocks of
`qt_sort_persist`, `qt_outline_persist` and `qt_calculate_gate` and the app and core calculate pair
tests are inside the suites above).

## 1.1 (R35dG1-1)

The rule from 1.2: while a Show Level or a Sort is queued, a change to the displayed outline or
order other than the queued kind's own replacement or withdrawal is refused. Two routes got round
it.

(a) A session Sort shown first, nothing queued, then a Level. `apply_outline_level` queued the
level (`make_show_level_edit` and `push()` accept it) and File > Save wrote it over the file's own
order while the rows showed sorted. Decision: refuse. `apply_outline_level` now refuses a Level 1
or deeper while `doc_.sort_active()` (the queued-Sort case is the earlier guard's), with the sort
named ("a session Sort is shown and File > Save would write the file's own order, not a session
Show Level; clear the sort first") and nothing changed. All Subtasks queues nothing and is still
accepted. Clearing the sort lets the same Level queue. The alternative (keep the Level session
only) was not taken: the brief's wording is "refuse", it mirrors the 1.2 table, and a session-only
Level would still leave a sorted display that Redo could re-queue under.

(b) A queued Level, Ctrl+Z, a sort choice, Ctrl+Y. The three sort paths (`apply_sort_choice`,
`apply_session_sort`, `clear_session_sort`) ended the redo list only when its top was a
`sort_view`. A new helper `redo_top_is_view_edit` also matches `show_level`, so a sort choice made
now ends a Show Level on the redo list and Ctrl+Y queues nothing.

Where the routes are reachable: only where the Sort stays session only. On the five-task file the
Sort queues (the measured shape) and `push()` clears the redo list itself, so neither route is a
bypass there. The six-task outline refuses the sort edit and takes it for the session, so the tests
use it.

Tests (`qt_outline_persist`, the block after the W35e-0.1 partial apply): (a) the Sort stays
session only, a Level under it queues nothing, the status line names the shown sort, the sorted
rows are as they were, All Subtasks is not refused and queues nothing, and after clearing the sort
the same Level queues; (b) Level 1 queued, Undo puts it on the redo list, a sort choice disables
Redo, Ctrl+Y queues nothing and the sorted rows are as they were. Red with `main_window.cpp` at the
parent (`build/w35e/1.1-red-run.log`): 6 FAILs ("a Level under a session Sort queues nothing",
the status-line needle, "leaves the sorted rows", "a sort choice ends the Show Level on the redo
list", "Ctrl+Y queues no level", "Ctrl+Y leaves the sorted rows"). Green: `1.1-green-run.log` and
`5d733ae-W35e-1.1-*`.

`spec/writers/view-state.md`: two rows in the 1.2 table (the shown-Sort refusal, the redo
rule) and the test list.

## 1.2 (R35dG1-2, with R35dW-4)

`spec/writers/calculate.md`, the Scope paragraph and the list of port saves now refused: the Notes
and Text1 removals add the freed payload to the Task freed scalar `0x01000002` and so do not match
the pin (only the Task size scalar `0x01000100` is excluded); the percent-complete saves and the
indent and outdent saves are listed with the scalars they move (read from `actuals.cpp` and
`outline.cpp`); and "every other project is refused at the pin" becomes "refused by the first
unvaried-parameter check it fails, or else by the pin", with the Add Resources and custom-field
saves said to meet an earlier check first (R35dW-4, since W35d-2.8 made the pin the last check).

## 1.3 (R35dG1-3, R35dW-2, R35dW-3)

- START-HERE (new wave 35e block with the open queue from `wave35d-verification/coord-gates-close.md`,
  R35dG0-1 first; the stale header; the 35b "not mergeable" sentence marks R35bG0-1 and R35bG0-2
  closed by wave 35c), `memory/coordinator.md` (header; the 35b state; the 35d state; a 35e state)
  and `plan-waves27-30.md` (the 35b queue marked closed for the two Highs; the 35c/35d section;
  a new "Queued from wave 35d" section).
- `wave35c-complete.md` and `wave35d-complete.md`: R35cG1-2, R35cG1-4, R35cW-1, R35cG0-3 and
  R35cW-4 read "partly fixed", each with its R35d remainder and the closing wave 35e commit;
  R35cW-CI1 and R35cW-CI2 read "fixed in source; CI is its gate" in the four files; the Show
  Level "does not diverge" sentence is corrected (the 0.4 row and "Found, not fixed").
- R35cW-1's own 35d row no longer says the lists were cleaned.

## 1.4 (R35dG0-2)

`wave35d-verification/gates-g0.md`: the 0.5 row reads "partly fixed: Task Information partial
apply", closed by W35e-0.1 `b8ee455`, and the 0.5 section's "already clears" carries its correction.
`wave35d-complete.md`'s 0.5 row was corrected in 1.3.

## Guards seen red

- 1.1: the `level != 0 && doc_.sort_active()` refusal and the `redo_top_is_view_edit` redo clear
  together (the parent's `main_window.cpp`), 6 FAILs in `qt_outline_persist`. The two guards were
  not run red one at a time; each of the six needles belongs to one of them (the first three to
  the refusal, the last three to the redo clear).
- 1.2, 1.3, 1.4: records; no guard.

## Deferred

Nothing.

## Found, not fixed

- A Level chosen while a `sort_view` sits on the redo list and no sort is shown (for example
  Sort queued, Ctrl+Z, a Level that stays session only because the builder refuses it) leaves the
  Sort on the redo list; Ctrl+Y then re-queues it over the session Level. The 1.2 rule's refusals
  guard a queued Sort, which this is not until Redo. Not reproduced here and not in the brief; a
  candidate for the coordinator's review.
- The "clearing a session sort" exception to the 1.2 rule sentence (R35dG1-6) is a G2 item and
  is not edited here.
