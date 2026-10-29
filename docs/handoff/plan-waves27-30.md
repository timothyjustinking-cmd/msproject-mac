# Plan for the next rounds - waves 27 to 30 (24 October 2026)

_Proposed by the Opus verifier session after the wave-26 assessment
(`assessment-2026-10-24.md`).  Each wave is a DeepSeek work order run in groups, a
fresh session per group, then verified by fresh-tree gates and one independent
review.  Only Critical or High findings jump the queue; everything else goes to the
wave it belongs to.  The workflow changes below were approved and implemented by
the operator's instruction on 24 October 2026 (`standing-rules.md`); the wave order still
awaits the operator's go-ahead._

## How every wave runs (changes from wave 26)

- **Write-path rule.** No save path is enabled until the same edit has been made in
  Project through COM, saved, and its streams diffed against the port's save.  The
  diff is committed as the test's expectation.
- **Lighter sessions.** DeepSeek's auto-compact window drops from 786k to about 200k
  tokens; logs are read with `tail`/`grep`, never whole.
- **Gates.** Incremental gates per commit; the full fresh-tree set once per group,
  with the log named for the exact commit.
- **CI every wave** once the publication hold is lifted: push the wave branch, run
  both workflows on all three OSs.
- **Watching.** The watcher wakes only on BLOCKED, STALLED or done.
- **Builds outside Dropbox sync** (the operator's decision).

## Wave 27 - make the save safe, get CI back (small; then merge)

| group | items |
|---|---|
| G0 | **% Complete:** refuse summary tasks; disable the five buttons until wave 28's actuals writer lands (tooltip says why).  **Priority:** measure the project-row Priority save against Project; keep it only if the bytes match. |
| G1 | **CI:** the first runs on `wave26` (24 Oct 2026, qt-shell 40043956720, ci 40043959284) failed on two things to fix first: (a) `app_qt/ribbon_bar.cpp:535` `widest_line` is unused - an error under GCC and Clang, so the Linux and macOS builds stop there; (b) the oracle-provenance check needs history, but CI checks out one commit - give the fixtures step `fetch-depth: 0` (or skip that sub-check on a shallow clone with an explicit message).  Then dispatch both workflows again and fix whatever the first complete macOS and Linux runs find (`app/main.mm` has not been compiled since wave 24). |
| G2 | **Evidence:** commit the one-change-per-save Project files behind the text-style, bar-style and timescale decoders, with a test per change. |
| G3 | **Queued review fixes, the cheap ones:** the unsaved % edit on a pending inserted task, the delete-discards-pending-edit, Unlink both directions (measure first), the silent `if (load ok)` test guards. |
| CLOSE | gates, review, live check |

Exit: all gates green on all three OSs.  **Then merge `wave26`+`wave27` into
`main`.**  Estimate: 3-4 hours.

## Wave 28 - the editing core (medium)

| group | items |
|---|---|
| G1 | **Actuals writer:** % Complete, Actual Start/Finish, Actual and Remaining Duration and the summary/project roll-up, written as Project writes them.  Re-enables the % buttons, Mark on Track and Update Project. |
| G2 | **Outline editing:** indent and outdent (level, parent, summary roll-up), measured against Project. |
| G3 | **New-task mode:** written to the file and used by Insert Task; Options > Schedule takes effect. |
| G4 | **Task Form editable** through the same save paths as the sheet. |
| G5 | **Split task**, if the measurement shows it is tractable; otherwise deferred with the measurement as evidence. |

Estimate: 6-8 hours.

## Wave 29 - the last visible differences, the missing dialogs (medium)

| group | items |
|---|---|
| G1 | **PROJECT_A gaps:** stored sheet column widths (8 columns, not 5), the yellow Highlighted row fill, bar-label coverage, timescale scroll origin and zoom. |
| G2 | **Format dialogs:** Text Styles, Gridlines, Bar Styles - functional over the styles decoded in wave 26. |
| G3 | **Page Setup**, wired to the shared print layout. |
| G4 | **Visual Reports, Compare Projects:** laid out from captures, primary action disabled. |
| G5 | **Chrome finish:** dark-theme header bands and ribbon ink; menu, QAT and status-bar artwork in the wave-26 icon style. |

Estimate: 6-8 hours.

## Wave 30 - make the commands work (large; may span two waves)

| group | items |
|---|---|
| G1-G4 | The 83 disabled ribbon commands, highest use first: Task tab (Move, Inspect, Deliverable), Resource tab (Assign Resources, Resource Pool, levelling), Project tab (baselines, WBS, Calculate), View tab (Sort, Outline, Filter, Group by, Highlight).  Each group lists its commands with a Project measurement for any that write. |
| G5 | Second view family at Gantt fidelity: Resource Sheet, Task Usage, Resource Usage. |

Estimate: 8-12 hours, likely split into waves 30 and 31.

## Queued from the wave-28 review (Medium/Low; `wave28-verification/review.md`)

- R28-5 Medium: `scripts/check_fixtures.py`'s user-profile pattern is built without
  `re.escape`, so the UTF-16 user-profile check can never
  fire (drive-path detection works).  Fix with a falsification test.
- R28-6 Medium: a percent edit plus a Start edit in one save is unmeasured; a
  percent edit on a task with its own calendar is accepted, shown, then dropped at
  save as a warning (refuse it up front).
- R28-7 Medium: `076438f` keeps the operator's name in local history; the push is
  blocked until the operator rules (verifier's job, not a wave item).
- R28-8 Low: the Task Form offers Name, Duration and Finish as editable but never
  saves them; a typed value can be dropped silently when the selection moves.
- R28-9 Low: new tests print SKIP and pass when a fixture is missing.
- R28-10 Low: two claims worded beyond what the tests show.
- R28-11 Low: `tests/data/README.md` lacks hash rows for 12 new fixtures.
- R28-12 Low: one commit's gate log skipped the core tests.
- Plus the wave-28 record's own "Found, not fixed" list (F4-F7, A1-A9 and the
  carried residuals).

## Queued from the wave-29 review (Medium/Low; `wave29-verification/review.md`)

- R29-8 Medium: refusals the edit model accepts but the writer raises at save fail
  the whole save, and every later Save until undo (move them up front).
- R29-9 Medium: several W29-0.0 refusals have no test.
- R29-10 Medium: the new-task-mode pair test still compares with the control.
- R29-11 Medium: R28-5 still open - the user-profile check in
  `scripts/check_fixtures.py` cannot fire on backslash paths.
- The eight Lows in the review.

## Queued from the wave-30 review (Medium/Low; `wave30-verification/review.md`)

- R30-5 Medium: the task-mode edit should be alone in its save; a queued view-state
  or formula edit is not refused, and a Start edit or Fill on another row fails only
  at save.
- R30-6 Medium: outdent's parent-assignment shape is refused only by the writer (a
  warning), so the edit silently drops from the saved file (W30-C5 fixed indent only).
- R30-7 Medium: the resource-loaded rule does not check the project or resource
  calendar or the contour; the work-not-9600 refusal has no test.
- R30-8 Medium (unverified): the task-mode shape does not check constraint,
  deadline, duration format or task type.
- R30-9..R30-14 Low: SKIP-and-pass tests on a missing fixture; 33 of 66 new
  fixtures without a pinned hash in `check_fixtures.py`; six doc lines naming the
  operator by first name; commit-message counts contradicted by logs; the outline
  progress check sees only percent and work; the R29-11 UTF-16 profile regex.

## Queued from the wave-31 review (Medium/Low; `wave31-verification/review.md`)

- R31-4 Medium: Indent, Outdent and Insert ignore the session sort (folded into
  wave 32's W32-0.3 as a refusal).
- R31-5 Medium: the automatic-outline file check does not pin task calendars,
  inactive rows, elapsed or estimated durations, or manual rows outside the edit.
- R31-6 Medium (performance): `usage_grid.cpp` `column_work` scans columns
  linearly per day bucket; walk both sorted lists together.
- R31-7..R31-12 Low: overallocation is a port-computed approximation; the Resource
  Sheet gate's COM facts are not a committed JSON; four SKIP-and-pass tests on
  missing fixtures; commit-message counts not in the cited logs; one brief line
  naming the operator; the outside-progress test sees only percent, and the usage
  gate accepts a longer or absent series.

## Queued from the wave-32 review (Medium/Low; `wave32-verification/review.md`)

- R32-1 Medium: Task Information queues custom-field edits before the percent push,
  so a refused percent leaves the field edits queued with only the refusal shown.
  **Closed by wave 33 W33-1.5** (both task dialogs now decide on a trial copy).
- R32-2 Medium: the macOS shell's filter still does range select, copy, multi-row
  paste and Indent/Outdent in file order (no saved field is reachable today).
- R32-3..R32-7 Low: `Document::save`/`save_start_edits` still write a Start if handed
  one; a one-row paste into a pre-sort range lands on the first file row; Indent,
  Outdent and Insert under a filter; three weak tests (paste-refusal column, a filter
  that hides nothing, no ID-ascending case); `sort_reorders_rows()` re-sorts on every
  menu update.

## Queued from the wave-33 CLOSE review (Low; the two independent CLOSE reports)

Safety and refusal code (six Lows):

- RW33-1 Low: `mppwrite::write_file_atomically` builds its temp name by
  truncation and renames over a symlink; the cleanup after a failed rename
  removes only the temp (`core/mppwrite/atomic_write.cpp:58-74`).
- RW33-2 Low: the rename replaces the target's mode, owner, ACLs and xattrs
  with defaults, and `atomic_write.h:21-25` does not document that
  (`core/mppwrite/atomic_write.cpp:106-107`).
- RW33-3 Low: the AppKit Task Fields handler still pushes onto the live
  queue with no trial copy (`app/main.mm:3415-3423`), unlike the Qt handler
  W33-1.5 fixed.  **Closed by wave 33b W33b-1.2** (R33-3).
- RW33-4 Low: "The opened file was not modified" can be false once the
  target has been written and a follow-on step fails
  (`app/document.cpp:649-655`, `app/edit_model.cpp:4416-4424`, the alerts at
  `app/main.mm:4197,4299` and `app_qt/main_window.cpp:3404`).
- RW33-5 Low: the FAT-slack refusal is open-side only; no save-side
  backstop re-checks a plan whose file was swapped for a FAT-slack shape.
- RW33-6 Low (test quality): the add/delete roundtrip builds its own clones
  (`tests/roundtrip_add_delete.cpp:796-855`) and the second-save test never
  asserts that no write happened (`app_qt/tests/save_reopen_test.cpp:638,685`).

Gates and records (four Lows):

- RW33-7 Low: CEdl/VarMeta expected-failure rot -- the mask expects the three
  signatures by name but does not pin which rows produce them
  (`tests/save_pair_noise.h:267-268`,
  `app/app_edits_actuals_tests.cpp:1118-1125,2210`).
- RW33-8 Low: three non-corpus tests still SKIP-and-pass on a missing fixture
  (`tests/mpp_tombstone_tests.cpp:168,216`, `tests/props_tests.cpp:1079`).
- RW33-9 Low: `scripts/check_markers.py` prints "commit-message scan skipped"
  and exits 0 when neither `origin/main` nor `main` resolves; CI always has
  one, but a clone without either silently loses the message half.
- RW33-10 Low: `.gitattributes` pins `*.md`, `*.py`, `*.yml`, `*.cpp` and
  `*.h` but not itself, so an edit to it can land CRLF.  It also leaves
  `*.json` unpinned: W33-4.5's changed lines in five CRLF
  `docs/analysis/oracle/*.json` files make `git diff --check main..HEAD`
  report 13 trailing-whitespace hits (the CR is the file's original ending;
  the CR counts are unchanged).  Decide in wave 34 whether to renormalise
  those files to LF or accept CRLF and stop using the range check as a
  whitespace gate.

## Queued from the wave-33 verification review (Low; `wave33-verification/review.md`)

The wave-33b fix-only wave closed R33-1..R33-3; these five stay queued.

- R33-4 Low: `scripts/phase2d_create_project.py:13,86` saves through COM
  straight onto the tracked `analysis/test_project.mpp` and never calls
  `oracle_privacy.neutralise()` (W33-4.6 covered only the `oracle_*.py`
  scripts), so a re-run reintroduces the Author/LastAuthor residual; save
  under `build/` or a neutral root, neutralise before saving, and refuse to
  overwrite a tracked file.
- R33-5 Low (test quality): the "exact" gap matcher accepts any single-effect
  `214/Props` or `214/CEdl/VarMeta` stream at any offset as `fixed`, and
  `open` is not tied to the pairs measured (overlaps RW33-7); require the
  first-effect offset to equal the signature offset and key expected
  failures by pair control name (`tests/save_pair_noise.h:266-289`).
- R33-6 Low: the W33-3.2 detail in `wave33-complete.md` states the three 214
  flag bytes as "Project's editor save resets ONE flag byte", but no
  measurement settles that mechanism; describe the bytes as unexplained (a
  per-save coincidence byte or an editor flag) and leave the mechanism to
  the wave-34 pair work (`tests/save_pair_noise.h:239-256`).
- R33-7 Low: the empty-formula warning still says "Use Clear Formulas to
  remove every stored formula", pointing at a button W33-1.4 disabled;
  reword it (`app_qt/dialogs.cpp:438`).
- R33-8 Low: a regular file already at `<target>.tmp-<pid>` is opened with
  `trunc` and then deleted on any failure, although the header promises to
  remove only a file this call created; create the temp exclusively
  (`O_EXCL` / `CREATE_NEW`) and set `created` only on exclusive creation,
  and extend the test beyond the directory case
  (`core/mppwrite/atomic_write.cpp:69-74`; overlaps RW33-1).

## Queued from the wave-33b CLOSE review (Low; the review of `wave33..wave33b`)

- RW33b-1 Low: the W33b-0.1 backstops key on `set_date_format && table_index
  != 0` only, so a hand-built `ViewStateEdits` with `replace_column = true`
  and a non-zero table index still reaches `set_table_column`, whose write is
  measured only at table 0; no production caller can build it (both shells
  refuse or show table 0), but the save-hook backstop should refuse any
  non-zero table index when a column replacement is measured for another
  table (`app/document.cpp:344,574-583`).
- RW33b-2 Low (test quality): removing the cascade test's no-value control
  leg leaves the retained clear/flag heuristic beneath the W33b-1.1 refusal
  untested; restoring the pair that lifts the refusal restores the test
  (`app/document_tests.cpp`, W33b-1.1 detail).
- R33b-3 Low: the Qt shell's table index counts task tables only, the request's
  index counts every stored table (resource tables included); they agree on both
  committed two-table fixtures (`app_qt/main_window.cpp:4404`).
- R33b-4 Low: a stale flag-decision comment (`app/edit_model.cpp:4321-4323`).
- R33b-5 Low: `wave33b-complete.md:211` says CI-2 was confirmed on `6fdf3af`, but
  that asan build stopped at 76%; the `b914006` run confirms it.
- RF-1 Low: `test_view_state_column_table0_only`'s byte-identical and "writes no
  view state" checks pass without the guard (table 1's column 1 is already Text1);
  use another field or column so they go RED too (`app/document_tests.cpp:773-801`).
- RF-2 Low: the `#else` stub list lacks `test_view_state_column_table0_only`
  (`app/document_tests.cpp:1777-1797`); latent, both macros are always defined.
- RF-3 Low: unchecked double -> uint32 casts of file f64 values
  (`core/mpp/reader.h:706, 787, 790`); no fuzz target reaches them yet.
- RF-4 Low: `static_cast<int>(std::floor(number / 10.0))` before the 0..10 clamp
  (`core/view/group.cpp:155`); NaN or > 2.1e10 is undefined behaviour.

## After wave 30 (not planned in detail)

Team Planner, Network Diagram, Calendar and Report views; the constraint-mso engine
case; macOS launch and notarisation; the hashed task names in
`tests/data/project_a_golden.tsv`; the remaining review Lows.

## Decisions needed from the operator before wave 27

1. ~~Lift the publication hold so waves are pushed and CI runs.~~ Done 24 Oct 2026.
2. ~~Move builds out of Dropbox sync (Dropbox-ignore `build/`).~~ Done 24 Oct 2026.
3. Approve this order, in particular: disable the % buttons in wave 27 and bring
   them back with the actuals writer in wave 28.
