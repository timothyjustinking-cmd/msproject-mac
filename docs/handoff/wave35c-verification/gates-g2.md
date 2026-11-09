# Wave 35c, group G2: builder record

Branch `wave35c`. The Lows: 27 items, in the brief's order, one finding id per commit, each with
the trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35c)` (checked by
`build/w35c/commit.sh` before the commit). No writer added, no refusal lifted (2.3, 2.10, 2.12
and 2.16 add refusals or pins), no pair measured, no COM, no push, no workflow dispatch, no
history rewrite.

Gate tooling: `build/w35c/gates.sh <label> [qt]` (Visual Studio 17 2022, Release,
`CORE_WARNINGS_AS_ERRORS=ON`; trees `build/w35b-close-fresh`, `-app`, `-qt`). Logs are
`build/w35c/<parent-sha>-W35c-<label>-*.log` and `*-gates.status` (git-ignored `build/`).
Scans each time: `check_fixtures.py`, `check_markers.py --range main..HEAD`, `guard_names.py`,
`scan_client_names.py`, and `git grep "client-string-[0-9a-f]{8}"` (none).

| Item | Finding | Commit | Status |
|---|---|---|---|
| 2.1 | R35bG0-6 | `4e957ae` | fixed: the push() refusal tests name the guard that fires |
| 2.2 | R35bG0-7 | `ecc51d1` | fixed: leaf date copies, remaining duration, FixedMeta bytes 8 to 11 pinned |
| 2.3 | R35bG0-8 | `8e41da3` | fixed in the writer; `push()` has no file, so the save refuses a hand-built edit (recorded) |
| 2.4 | R35bG2-2 | `21b61d6` | partly fixed (tests only); had also been closed incidentally earlier.  Correction (W35d-1.3, R35cG2-1): `d8cfb6c` (2.7) put the up-front scratch refusal ahead of the chain, so no test reached the chain again; the fault-seam test in `roundtrip_calculate` does |
| 2.5 | R35bG2-5 | `cb40334` | fixed (statements and a comment); no behaviour change |
| 2.6 | R35bG2-6 | `fc9cae3` | partly fixed (the app test asserts the digest refusal); the second suggestion is unreachable and recorded as defence in depth (one status for 2.6 here and in `wave35c-complete.md`, W35d-1.6, R35cG2-8) |
| 2.7 | R35bG2-7 | `d8cfb6c` | fixed: an existing `<target>.calculate.tmp` is refused, not deleted |
| 2.8 | R35bG4-2 | `6f95c27` | fixed |
| 2.9 | R35bG4-3 | `36c1492` | fixed, both Fixed2Meta sites |
| 2.10 | R35bG4-4 | `aab8a26` | fixed (defence in depth: not reachable today) |
| 2.11 | R35bG4-6 | `d776901` | fixed: the `TaskRow{}` test; mutation G7c turns it red |
| 2.12 | R35bG1-4 | `d829206` | fixed as a pin (a refusal): the five-task outline accepts the first digest only |
| 2.13 | R35bG1-5 | `6f28860` | fixed |
| 2.14 | R35bG1-6 | `cea97e1` | fixed |
| 2.15 | R35bG1-9 | `d0100dd` | fixed |
| 2.16 | R35bG3-3 | `0222978` | fixed |
| 2.17 | R35bG3-5 | `8aa61f5` | recorded: the second-session Bold pair is a measuring-wave item; no pair taken |
| 2.18 | R35bG4-5 | `c235973` | fixed |
| 2.19 | R35bG1-7 | `dbef0ef` | fixed (one backlog row) |
| 2.20 | R35bG1-8 | `e9e4657` | fixed for the wording; which fields Save writes was not re-traced (below) |
| 2.21 | R35bG2-3 | `f074f1c` | fixed for the wording; the name-trim difference recorded, not changed |
| 2.22 | R35bG2-8 | `b588f05` | fixed (README rows); the g3 rows are found, not fixed |
| 2.23 | R35bG3-4 | `f8be7ba` | recorded: gates-g3.md says Show Level 1 and 2 |
| 2.24 | R35bW-4 | `f4065f2` | recorded: every open id listed in three documents |
| 2.25 | R35bW-5 | `cd7dc10` | recorded: the CLOSE id table corrected |
| 2.26 | R35bG2-4 | `4d3c943` | recorded; no history rewrite |
| 2.27 | R35bW-6 | `9a38f2e` | recorded as an accepted defect; no history rewrite |

## Gate lines

Each row is the run at the parent of the item's commit (the label names the item). All rows:
core build 0, core `ctest -E plan_recalc` 65/65, app build 0, app ctest 34/34, fixtures 0,
markers 0, guard_names 0, scan_client_names 0, client-string grep none (status 1), 0 compiler
warnings.

| Item | Log prefix | app_qt (52 tests) |
|---|---|---|
| 2.1 | `ea54226-W35c-2.1-*` | 52/52 |
| 2.2 | `4e957ae-W35c-2.2-*` | 52/52 |
| 2.3 | `ecc51d1-W35c-2.3-*` | not built (no app or Qt source) |
| 2.4 | `8e41da3-W35c-2.4-*` | not built (tests only, core) |
| 2.5 | `21b61d6-W35c-2.5-*` | 52/52 |
| 2.6 | `cb40334-W35c-2.6-*` | 52/52 |
| 2.7 | `fc9cae3-W35c-2.7-*` | not built (core only) |
| 2.8 | `d8cfb6c-W35c-2.8-*` | not built (core only) |
| 2.9 | `6f95c27-W35c-2.9-*` | not built (core only) |
| 2.10 | `36c1492-W35c-2.10-*` | 52/52 |
| 2.11 | `aab8a26-W35c-2.11-*` | not built (app test only) |
| 2.12 | `d776901-W35c-2.12-*` | 52/52 |
| 2.13 | `d829206-W35c-2.13-*` | not built (app test only) |
| 2.14 | `6f28860-W35c-2.14-*` | 52/52 |
| 2.15 | `cea97e1-W35c-2.15-*` | not built (app test only) |
| 2.16 | `d0100dd-W35c-2.16-*` | not built (app test only) |
| 2.17 | `0222978-W35c-2.17-*` | not built (record) |
| 2.18 | `8aa61f5-W35c-2.18-*` | 52/52 |
| 2.19 | `c235973-W35c-2.19-*` | not built (a Python table and a generated doc) |
| 2.20 | `dbef0ef-W35c-2.20-*` | 52/52 |
| 2.21 | `e9e4657-W35c-2.21-*` | 52/52 |
| 2.22 | `f074f1c-W35c-2.22-*` | not built (record) |
| 2.23 | `b588f05-W35c-2.23-*` | not built (record) |
| 2.24 | `f8be7ba-W35c-2.24-*` | not built (record) |
| 2.25 | `f4065f2-W35c-2.25-*` | not built (record) |
| 2.26 | `cd7dc10-W35c-2.26-*` | not built (record) |
| 2.27 | `4d3c943-W35c-2.27-*` | not built (record) |

The Qt tree was built wherever app or Qt source, or a core file the Qt tree links, changed
(2.1, 2.2, 2.5, 2.6, 2.10, 2.12, 2.14, 2.18, 2.20, 2.21), and the 2.21 run is the last code
change of the group, so no state of the branch's code was left without a Qt build.

## Red and green, per item

Every red is the new tests against the code without the fix (or with the guard switched off by a
non-constant false, because MSVC here treats constant-condition warnings as errors); the files
were restored after each run. Logs are in `build/w35c/` (git-ignored). Where nothing can go red
the item says so.

- 2.1: `2.1-old-test-red-run.log`, `2.1-new-test-red-run.log`: with the "does not start at the
  project start" guard unreachable, the old test has 1 failure (the builder check; `push()`
  stays green because the 0.4 check refuses the plan) and the new test has 2 (the builder check
  and the `push()` check).
- 2.2: `2.2-red-run.log`: production change reverted, tests kept: 11 "expected a refusal, none
  thrown" failures and "request refusals leave no file". `2.2-green-run.log` is the fixed run.
- 2.3: `2.3-red-run.log`: production check reverted: 3 failures (deadline, constraint, no file).
- 2.4: `2.4-scratch-red-run.log` (the first append written to the target instead of the scratch
  file: the scratch-unusable check fails) and `2.4-kind-red-run.log` (with the 0.3 pin call also
  commented out: the pending-record-kind check fails).
- 2.5: no red run: statements and a comment only; the existing tests already pin the
  recomputation.
- 2.6: `2.6-red-red-run.log`: the digest comparison in the 5k save made false: the new "names
  the changed file" check fails with the writer's "not pending" text, as does the stale-slack one.
- 2.7: `2.7-red-run.log`: `calculate.cpp` reverted: the squat test's needle, "expected a refusal,
  none thrown", "no target written" and "kept" fail.
- 2.8: `2.8-red-run.log`: `task_mode.cpp` reverted: the needle fails, the text still reading "the
  task-mode save needs ...".
- 2.9: `2.9-red-run.log`: `calculate.cpp` reverted: (a) fails, blaming "task uid 1".
- 2.10: `2.10-red-run.log`: `app/edit_model.cpp` reverted: three checks fail (the plan-layer
  refusal and both halves of the `push()` check).
- 2.11: `2.11-red-red-run.log`: mutation G7c (the `type_block_known` default flipped to `true` in
  `app/layout_model.h`): the default assertion and both refusal checks fail.
- 2.12: `2.12-red-red-run.log`: the pin made always-true: the five-task case writes a file and
  the builder check passes.
- 2.13: `2.13-red-red-run.log`: the sort refusal's text changed in `viewstate_sort.cpp`: the new
  per-fixture needle fails and the old prefix check stays green (the finding's weakness).
- 2.14: `2.14-red-red-run.log`: the Font Bold case made to call `define_wbs_code()`: "the Font
  Bold handler queued its edit and said so" fails and the old `not_wired` check stays green.
- 2.15: `2.15-red-red-run.log`: the digest compare in `save_view_sort` made `== 0`: the new
  zoom-only case fails, `app_edits_sort` 0/1.
- 2.16: `2.16-red-red-run.log`: the `outline_shape_measured` call in `push()` replaced by a
  non-empty check: the Level 2 case and its "nothing was queued" check fail and the relabelled
  Level 3 cap case stays green.
- 2.17: no red run: a record.
- 2.18: `2.18-red-red-run.log`: the About text changed: "the About box is the port's own" fails
  through the answerer's recorded text.
- 2.19: no red run: no check pins a menu row's gap text (`check_command_backlog.py` compares the
  ribbon pins only and stays green with the stale doc too). The evidence is the one-row
  regenerated diff.
- 2.20: `2.20-red-sort-red-run.log` (5 failures) and `2.20-red-level-red-run.log` (1 failure):
  `app/edit_model.cpp` reverted under the updated tests.
- 2.21: `2.21-red-red-run.log`: `app/edit_model.cpp` reverted under the updated tests: 8
  failures (four cases each for assign and for removal).
- 2.22: no red run: a README record. The evidence is the fixture read (Props 0x02400002
  `3831f13b` in every `wave35-viewstate` fixture).
- 2.23 to 2.27: no red run: records.

## Found, not fixed

- The 24 `wave35-g3-*` fixtures store project start 2026-01-04 13:00 (Props `781ef13b`), not the
  `3831f13b` of the viewstate fixtures; their README rows (602 to 605 among them) still say
  2026-01-05 08:00.
  **Corrected by W35d-2.15 (R35cG2-7)**, from a decode of the Props record 0x02400002 in each
  file (key, length 4, value; `build/w35d/props_scan.py`, output `build/w35d/2.15-g3-scan.tsv`),
  not a substring scan: it is 12 pairs (36 files, the control, its null save and the result of
  each) at `781ef13b` (2026-01-04 13:00) and 5 pairs (15 files) at `3831f13b` (2026-01-04
  21:00), and no file has both.  `781ef13b`: `assign-remove-port-saved`, `font-bold-off`,
  `font-bold-range`, `font-bold-row2`, `font-bold-start`, `font-colour`, `font-italic`,
  `font-name`, `font-size`, `font-underline`, `notes-generator`, `wbs-renumber-defined`.
  `3831f13b`: `baseline-set-nested`, `insert-milestone-first`, `insert-milestone-last`,
  `insert-milestone-nested`, `insert-summary-nested`.  The 51 `wave35-viewstate-*` files all
  carry `3831f13b`.  README rows 602 to 605 are four of the `insert-*` pairs, which are in the
  second group; they say Start 2026-01-05 08:00 (the leaf's Start, the first working instant
  after either Props start), not the Props word, so they are not shown wrong by this.  No
  fixture or README row was changed.
- The plan layer trims the Props calendar name and the writer does not (2.21): the writer is
  the stricter, and the builder's dry run reaches it, so no file is mis-written.
- The stale "asks to save or undo" labels in the sort and show_level test names remain.
- No guard pins a menu row's gap text (2.19).
- 2.20 did not re-trace which fields Save writes; it changed the wording only.
- R35bG2-6's second suggestion (the save-time dry run) is unreachable through the public API; it
  is defence in depth.
- R35bG2-2 and R35bG2-5 had been closed incidentally by earlier work (tests and wording); 2.4 and
  2.5 make the closure explicit.
- Not this wave's: R35bW-3 (answered by the coordinator's map); R35bG2-9 is watch-only, green on
  macOS at `e95c6ca`.
