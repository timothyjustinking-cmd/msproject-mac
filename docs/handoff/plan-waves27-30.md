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

## Queued from wave 34 (CLOSE): wave 35 and the wave-34 review findings

Wave 34 stopped after G4 (the brief's stop rule: the 12-hour point passed during
G4).  Wave 35 takes, in order, subject to the operator's word:

- **G5, the scheduler save path, stage 1** (brief section 2, W34-5.1..5.3): the
  decode note for Calculate Project from `wave30-calculate-*` and
  `wave34-calculate-second`, `mppwrite::save_plan` as a measured writer for one
  shape (a pending Duration change on an unlinked automatic leaf, F9, save),
  two more pairs before linked files, then Calculate Project enabled for the
  measured shapes only.  Respect Links, Move, Auto Schedule and Level All stay
  disabled until their own pairs match.
- **G6, view-state persistence** (brief section 2): sort order, applied table,
  outline level and zoom, one pair each; the Zoom Custom span measured first.
- **The deferrals from G4** with the pairs they still need, each in the
  `wave34-complete.md` "Found, not fixed (G4)" list: Insert Summary and Insert
  Milestone (4.4: renumbering, reparenting, the 2.5 ordinal, assignment
  placeholder rows, CEdl and Props changes), Set Baseline 0 (4.7: 94 new var
  blocks, 26 undecoded), Resource and Task Notes (4.1: the RichEdit generator
  string), Font controls other than one Bold cell, WBS Renumber, port-saved
  assignment removal.
- **The macOS shell** (`app/main.mm`) wires only Add Resources (4.2, read not run)
  of the wave-34 items; it builds no UI for rename, assign, bold or Define Code
  (RC-R4, RC-R5).  Wire them or record why they stay unwired; no refusal exists
  in one shell only.
- **The wave-34 CLOSE review** (`wave34-verification/close-review.md`): the two
  Highs (R34G3-1 / RC-R1, R34G3-2) and the Mediums R34G3-3, R34G3-4, RC-W5 and
  RC-R3 go to the fix-only `wave34b`; the other Mediums (RC-W1..W4, RC-W6..W8,
  RC-R2) and the Lows (RC-W9, RC-W10, RC-R4..R8, R34G3-5..R34G3-9) are queued
  here for wave 35 with the directions in that file.

### Added at the wave 34b CLOSE (8 November 2026)

Wave 34b is fix-only and closed no item below; each is queued for wave 35 with its
location at `980ce9d` (the line numbers of the review at `8400c2c` have moved; grep the
symbol).  The reviews hold the full rows.

- **RC-W1, RC-W2** (`core/mppwrite/assignment_growth.cpp`, the settled and first-assignment
  adds: the placeholder-count constants near the FixedMeta rewrite, and the sorted
  resource-uid leaf insertion near `std::sort(assigned...)`, line 1431): unmeasured count
  and ordering; wave 34b 1.2 tests the count refusals, not the ordering.
- **RC-W6** (`core/mppwrite/varmeta_edit.cpp:921` `save_var_record`): accepts any field id,
  payload and presence bits for the Task table; safety rests on the callers.
- **RC-W7** (`core/mppwrite/props_edit.cpp:273` `save_props_edit`): any existing record id
  with an equal-or-larger payload; four record ids are measured.
- **RC-W8** (`core/mppwrite/viewstream_edit.cpp`, the bold row uid and field id; the view uid
  is now pinned at :569 by W34b-0.4): the row uid and field id are stored verbatim.
- **R34G4-10** (`core/mppwrite/resource_growth.cpp` new resource uid; `assignment_growth.cpp:1481`
  and `:1799` `new_uid = max_uid + 1`): Project takes the next UniqueID from its own counter,
  which a compacted file can hold above max + 1; unmeasured, not refused.
- **R34G4-11 = RC-W9** (`core/mppwrite/wbs_code.cpp:230`): the WBS dry run writes a full project
  copy to `temp_directory_path()/w34-wbs-dry-<n>.mpp`, a predictable shared-temp name.
- **R34G3-6, R34G3-7, R34G3-8** (Lows of `review-g3.md`: the DSI test outside the save-pair
  harness, a declared tail range skipping the length check, the W34-B5 leftovers) and
  **RC-R6, RC-R7, RC-R8** (Lows of `close-review.md`: shell-level tests, the prompt before
  the builder, record wording); R34G3-5 and R34G3-9 and RC-W10, RC-R4, RC-R5 were queued
  above.
- The brief section 4(e) Project-reopen check of the five enabled saves is the
  coordinator's and is still open.
- **The wave 34b CLOSE review** (two read-only passes over `git diff wave34..wave34b`, one
  on the writers and one on the refusals; 0 Critical, 0 High).  Mediums:
  - **RW34b-1** (`core/mppwrite/assignment_growth.cpp`, `check_measured_shape`, base-calendar
    check): the resource's "own base calendar" test is only that resource word 154 equals
    `uid + 2`; the calendar's content (hours, exceptions) is not compared with the pair's.
    Partly mitigated by the 40 h work pin.  Direction: compare the calendar record bytes or
    refuse any file whose calendar table differs from the pair's.
  - **RW34b-2** (Qt gate tests with a generic needle, lines not touched by wave 34b):
    `app_qt/tests/cell_bold_gate_test.cpp:193,214,277`, `wbs_code_gate_test.cpp:173,224`,
    `assign_resources_gate_test.cpp:321`, `resource_add_gate_test.cpp:288,334`,
    `resource_information_gate_test.cpp:281,292` still accept "not implemented in this
    port"; the app-level needles were sharpened in 1.2.
  - **RW34b-3** (no Qt gate test for the new refusals): the Add Resources template refusals,
    the assignment shape refusals, the Bold applied-view refusal, the WBS ID/row-order
    refusal and the 1.3 pending-document-edit combination are covered at app and core level
    only.
  Lows: **RW34b-4** the task-row masks `{0,32}` (bytes 16..23) and `{96,120}` and the
  FixedMeta/Fixed2Data masks in `assignment_growth.cpp` carry no stated reason;
  **RW34b-5** `tests/roundtrip_resource_growth.cpp` ~668 uses an empty needle
  ("a resource row without its meta entry"); **RW34b-6** `viewstream_edit.cpp`
  `applied_view_name` (~460) walks items without the odd-length pad byte that `load_view`
  has, so it fails closed on an odd item; **RW34b-7** `resource_growth.cpp` ~653 template
  mask `{154,158}` skips the calendar-reference word (confirm the writer rewrites it);
  **RW34b-8** `app/edit_model.cpp:2889-2954` and `:3069-3100`: the type, effort-driven,
  base-calendar and applied-view refusals exist only through the writer dry run (the plan
  cannot carry those fields, so `push()` has no plan-side equivalent); **RW34b-9**
  `add_resource_template_refusal` (`edit_model.cpp:2744-2761`) checks only the last
  resource and its assignments at plan level, with the row fields left to the writer.

## After wave 30 (not planned in detail)

Team Planner, Network Diagram, Calendar and Report views; the constraint-mso engine
case; macOS launch and notarisation; the hashed task names in
`tests/data/project_a_golden.tsv`; the remaining review Lows.

## Decisions needed from the operator before wave 27

1. ~~Lift the publication hold so waves are pushed and CI runs.~~ Done 24 Oct 2026.
2. ~~Move builds out of Dropbox sync (Dropbox-ignore `build/`).~~ Done 24 Oct 2026.
3. Approve this order, in particular: disable the % buttons in wave 27 and bring
   them back with the actuals writer in wave 28.

## Queued from wave 35 (CLOSE): the fix-only `wave35b`

Wave 35 closed on branch `wave35` (`WAVE-35-COMPLETE`) with High findings open, so a
fix-only `wave35b` (no writer added, no refusal lifted, no Project pair needed unless a
finding says so) comes before any merge.  Highs first, in this order:

- **Calculate Project** (R35G1-1, R35G1-2, R35G1-3, RW35-W2): refuse a deadline; check
  every leaf's Start and Finish against the project start and Start + Duration so only the
  measured cause (the critical leaf's Duration lengthened) is accepted; write the rule 5 pin
  record and refuse every unpinned parameter (task type, effort-driven, milestone, project
  calendar, equal finishes) in the writer AND the edit model, with a test per refusal.
- **View state** (R35G2-1..4, RW35-R2): pin the applied table, zoom and sort state a Show
  Level save requires; read the outline, names and order from the file, not the caller;
  refuse Sort on every shape but the measured one and Duration ascending without a pair;
  build Sort and Show Level from the file's rows, or refuse while record edits are pending,
  with a gate test.
- **Removal from a port-saved file** (R35G3-1, RW35-W1): refuse any file whose origin is not
  the raw port output (an origin check in the edit model and the writer's settled-shape gate
  reading what Project's resave changes: Rsc Fixed2Meta, the Assn Fixed2Data counter, Assn
  Var2Data, CEdl), with a test that runs the removal on the null save.
- **The 9fa599b history defect** (operator or coordinator): rewrite the commit so the two
  `wave35-g3-notes-generator*` blobs carry the neutral identity before any push.

Mediums and Lows: RW35-W3..W7, RW35-R3..R5 (specific needles for the Calculate linked case and
Level 3, 4 and 9), and the Lows in `wave35-verification/close-review-writers.md` and
`close-review-shells.md`; R35G0-1..R35G0-13, R35G1-4..14, R35G2 and R35G3-2..12 in
`review-g0.md`..`review-g3.md`.  Still queued as before: the wave-34 Lows, Respect Links, Move,
Auto Schedule and Level All, the extra views, the `constraint-mso` engine case and macOS
launch and notarisation.  RW35-R1 (the `-Wswitch` break in `app_qt/command_status.h`) is
closed by `513de29`.

## Queued from wave 35b (CLOSE)

Wave 35b closed on branch `wave35b` (`WAVE-35b-COMPLETE`) with its brief done, but the per-group
reviews (`docs/handoff/wave35b-verification/review-g0.md` to `review-g3.md`) left two Highs on
Calculate Project.  **Both were fixed in wave 35c (`7d923dc`, `743ad8b`) and are closed; the
list below is history (W35e-1.3, R35dG1-3).**  Wave 35c took them first:

- **R35bG0-1 (High; closed by `7d923dc`)**: read the `114/Props` project start (`0x02400002`) and require every
  leaf's Start to equal it in `read_calculate_shape` and `calculate_plan_refusal`, not row 0's
  roll-up (`core/mppwrite/calculate.cpp`, `app/edit_model.cpp`); a test that moves the project
  start under manual calculation.
- **R35bG0-2 (High; closed by `743ad8b`)**: pin the `114/Props` calculation options (multiple critical paths, the
  critical-slack limit, minutes per day) to the control's values in both layers.

Then, as `review-g0.md` to `review-g3.md` give them (**the Mediums and Lows here that wave 35c fixed are closed; the ones it did not are named in the paragraph below, and wave 35d's open ids are in `wave35d-verification/coord-gates-close.md`**): Mediums R35bG0-3 (two-edit critical-path
move; record the limit in `spec/writers/calculate.md`), R35bG0-4 (reader tests for the new plan
fields), R35bG0-5 (plan layer narrower than the writer), R35bG1-1 (`sorted_order_of` reads
`sorted[index]` after `sorted.clear()`), R35bG1-2 (the 1.6 guard reads FixedMeta byte 12 only),
R35bG1-3 (one shared guard serves both layers for 1.1, 1.3, 1.6), R35bG2-1 (the tie and
swapped-order refusals are writer-only), R35bG3-1 (other commands change the outline under a
queued Show Level); Lows R35bG0-6 to -8, R35bG1-4 to -9, R35bG2-2 to -8 and R35bG3-2 to -5.
Coordinator mutations that stayed green and need a test: G0 W11; G1 S1, S4 (in the Sort lane),
A6, Q1, Q2.  Also still queued: R35G3-8 (six wave 35 commits lack the trailer; recorded, no
rewrite), RW35-W13, and the measuring-wave list at the end of the wave 35b brief.  The
`9fa599b` blobs (`wave35-g3-notes-generator*.mpp`) still sit in `wave35b` history and fail
`guard_names`: the operator's and the coordinator's call.

Wave 35c closed the G4 and CLOSE review queue (`wave35b-verification/coord-gates-close.md`) except R35bG0-9 and R35bG1-10 (the untested guards recorded in `gates-g0.md` and `gates-g1.md`; assigned to no item), R35bG2-9 (watch: the intermittent `qt_assign_resources_gate`, green on macOS) and R35bW-3 (answered by the coordinator's map); R35bG1-3 and R35bG2-6 are partly fixed and recorded as defence in depth.

The four wave 35b ids that wave 35c recorded **partly fixed**, with their remainders (W35f-0.4,
R35eG1-2): R35bW-1 (remainders R35cG1-2, through R35dG0-1 closed by W35e-0.1, and R35cG1-3, closed
by W35d-1.1), R35bW-2 (R35cG1-1, closed by W35d-0.4), R35bG2-2 (R35cG2-1, closed by W35d-1.3) and
R35bG3-1 (R35cG1-4, through R35dG1-1 fixed by W35e-1.1, whose follow-ons are R35eG1-1 and
R35eG1-3, below).  See `wave35c-complete.md`.

## Queued from wave 35c (CLOSE): the fix-only `wave35d` (10 November 2026)

The complete queue is `wave35c-verification/coord-gates-close.md`, Highs first; `wave35d` (branch
`wave35d`, brief `wave35d-brief.md`, record `wave35d-complete.md`) worked it and closed (`WAVE-35d-COMPLETE`).  State at CLOSE:

- **Highs:** R35cG0-1 (fixed `abc8a60`), R35cG1-1 (fixed `61a451b`), R35cG1-2 (**partly fixed**
  `6458203`; remainder R35dG0-1, fixed in source by W35e-0.1 `b8ee455`, awaiting review).
- **CI:** R35cW-CI1 (`23de6e2`) and R35cW-CI2 (`171831b`) are fixed in source; CI on GCC 13 and
  macOS is the only proof of each and its gate, until the coordinator's run is green.
- **Mediums:** fixed R35cG1-3 (`e967858`), R35cG2-1 (`41f7a2c`), R35cW-2 (`454521d`), R35cG2-8;
  **partly fixed** R35cG1-4 (`e2eca38`; remainder R35dG1-1), R35cG0-3 (`6552f11`; remainder
  R35dG1-2 and R35dW-4) and R35cW-1 (`4410935`; remainder R35dG1-3 and R35dW-2).
- **G2 (the Lows and the Calculate Medium):** fixed R35cG0-5, R35cG0-6, R35cG1-5 to -7, R35cG2-2
  to -7 and R35cW-3 and -5 (commits in `wave35d-complete.md`); R35cW-4 is **partly fixed**
  (`4dcfcd7`; remainders R35dW-1 and R35dW-5).  **Partly fixed, still open:**
  R35cG0-2 (the plan reads Start word +104 only; the writer half is closed) and R35cG0-4 (the plan
  has no Props digest, a recorded gap); both are refused at the writer or the save.
- Every 35d fix awaits an independent review.

## Queued from wave 35d (CLOSE): the fix-only `wave35e` (10 November 2026)

The complete queue is `wave35d-verification/coord-gates-close.md` (the coordinator's list, High
first); `wave35e` (branch `wave35e`, brief `wave35e-brief.md`, record `wave35e-complete.md`) works
it.  The open ids are:

- **High:** R35dG0-1, with R35dW-1 (a partly applied Task Information or Task Form edit keeps the
  structural redo list).  Fixed by W35e-0.1 `b8ee455` and **closed by the coordinator's wave 35e
  G0 review** (`wave35e-verification/coord-gates-g0.md`); no High is open.
- **Mediums:** R35dG0-2, R35dG1-1, R35dG1-2, R35dG1-3, R35dW-2, R35dW-3 (G1: W35e-1.1
  `39c8e37`, 1.2 `1da3997`, 1.3 the records `02f1900`, 1.4 `f244d02`).  The coordinator's review
  found follow-ons, which are the wave 35f queue below (R35eG1-1 and R35eG1-3 against 1.1,
  R35eG1-2 against 1.3).
- **Lows:** R35dG0-3 to -5, R35dG1-4 to -7, R35dG2-1 to -5, R35dW-4 to -6 (W35e-1.2 covers
  R35dW-4).  Wave 35e is closed (`WAVE-35e-COMPLETE`): all are fixed in the builder's tree except
  R35dG2-1 (partly fixed: Fill Down's guard has no test route) and R35dW-5 (fixed in source;
  CI's `macos-appkit` job is its gate and was green at `c0c81e5`).
- **CI:** R35cW-CI1 and R35cW-CI2 were proved by the green wave 35d run (`3d22c39`); wave 35e's
  run (`c0c81e5`) is green on all seven jobs.

## Queued from wave 35e (CLOSE): the fix-only `wave35f` (10 November 2026)

Waves 34 to 35e are merged into `main` (`5e1c8ea`).  The complete queue is
`wave35e-verification/coord-gates-close.md` (the coordinator's list; no Critical or High); `wave35f`
(branch `wave35f`, brief `wave35f-brief.md`, record `wave35f-complete.md`) works it.  Only the
coordinator's review closes an id.

- **Mediums:** R35eG1-1 (W35f-0.1 `fd457b4`: a session-only Level under a session Sort is shown
  again, only the queue is refused), R35eG2-1 (W35f-0.2 `08b83f9`: the Custom Fields dialog's
  Formula... button guards a queued exclusive edit of any kind since W35f-1.8), R35eG1-3
  (W35f-0.3 `1a2436e`: the five-task Undo-Sort-then-Level test and the corrected 1.1 record) and
  R35eG1-2 (W35f-0.4: these records).  All four are fixed in the builder's tree and were reviewed
  by `coord-gates-g0.md`.
- **Lows:** R35eG0-1 to -3, R35eG1-4 to -6, R35eG2-2 to -10, R35eW-1 to -4 (wave 35f G1: shell,
  AppKit, Calculate seams, records), fixed and reviewed by `coord-gates-g1.md`.  Open from those
  reviews: R35fG0-* and R35fG1-*.
- **Still partly fixed:** R35dG2-1 (Fill Down's guard has no test route), R35cG0-2 (the plan
  reads Start word +104 only), R35cG0-4 (no plan-side Props digest), R35cW-4 (remainders R35dW-1,
  closed, and R35dW-5, fixed in source with `macos-appkit` as its gate), and the four 35b ids
  above (R35bW-1, R35bW-2, R35bG2-2, R35bG3-1).  Still open and unassigned: R35bG0-9, R35bG1-10
  and R35bG2-9.

## Queued from wave 35f (CLOSE): the fix-only `wave35g` (11 November 2026)

Waves 34 to 35f are merged into `main`.  The queue was `wave35f-verification/coord-gates-close.md`
("Open after the merge"; no Critical or High); `wave35g` (branch `wave35g`, brief
`wave35g-brief.md`, record `wave35g-complete.md`, `WAVE-35g-COMPLETE`) works it.  Only the
coordinator's review closes an id.

- **Mediums:** R35fG1-1 (W35g-0.1 `284c3cb`: Custom Fields... opens beside a queued exclusive edit,
  only Formula... is refused) and R35fG0-1 (W35g-0.2 `24a5c1d`: the 1.1 record's bypass (b)
  corrected and tested).  Fixed in the builder's tree; to be reviewed.
- **Lows:** R35fG0-2 to -9, R35fG1-2 to -5, R35fW-1 to -3 (wave 35g G1: tests, status wording,
  records, rewraps; `1663aaf` to `0c35d05`), fixed in the builder's tree; to be reviewed.
- **Open gate:** `check_markers.py` reports one hit in `spec/writers/evidence/trace-captures-r5.md`
  (arrived in `5863054`; a directory the wave may not touch).
- **Still partly fixed or open, unchanged:** R35dG2-1, R35cG0-2, R35cG0-4, R35cW-4 (remainder
  R35dW-5, fixed in source with `macos-appkit` as its gate), the four 35b ids (R35bW-1, R35bW-2,
  R35bG2-2, R35bG3-1), R35bG0-9, R35bG1-10 and R35bG2-9.

## Queued from wave 35g (CLOSE): no wave 35h (11 November 2026)

Waves 34 to 35g are merged into `main`.  Fable's work order of 11 November 2026 makes 35g the last
fix-only wave: under standing rule 5, only a Critical or High goes to the front of the next wave,
and wave 36 is the writer wave.  None is open.  The record is
`wave35g-verification/coord-gates-close.md`.

- **Lows (queued, no wave assigned):**
  - R35gG0-1 to -4: no Formula...-then-Cancel test; two narrow assertions; the input-dialog
    helper's timers; a silent refused Formula... while the dialog is open.
  - R35gG1-1 to -5: 1.4's advice holds only in some cases; 1.7's note on the all-fail path; 1.1's
    read-only test; the rest of R35fW-1 (state files); one more commit-message misattribution.
  - R35gW-1 to -5: state files at the CLOSE tip; the R35eG1-3 and R35eW-1 rows unannotated in
    `wave35f-complete.md`; the Calculate refusal's "would write it, not X" wording; 1.6's test
    cannot see an Undo that does nothing.
  - R35gW-1's state-file part goes to W36-0.6 (START-HERE rewritten).
- **Closed by the coordinator:** R35dW-5 (`macos-appkit` green three times since its fix), which
  closes R35cW-4's last remainder.
- **Still partly fixed or open, unchanged:** R35dG2-1, R35cG0-2, R35cG0-4 (wave 36 G2), the four
  35b ids (R35bW-1, R35bW-2, R35bG2-2, R35bG3-1), R35bG0-9, R35bG1-10 and R35bG2-9.  Revisit at
  wave 37 planning.

## Queued from wave 36 (CLOSE): the G0f review's Mediums and Lows (22 November 2026)

The G0f review is `wave36-verification/review-g0f.md`.  CLOSE fixed R36G0f-1 (W36-C.1 `de97271`), -2 (W36-C.2
`1a581b2`), -3 (W36-C.3 `79109bb`) and -8 (W36-C.4 `429f241`).  The rest are queued, not fixed, with no wave
assigned:

- **R36G0f-4 (Medium):** the VM's newer saves lie above three spans: 108 of 210 VM copies with a committed
  reference PASS, not 210.  `LASTSAVE_SPAN`'s top (11 November 2026) is below 115 of 231 VM LastSave values
  (94 `?SummaryInformation` FAILs); `STAMP_SPAN` is below 33 Props save words and `CVIEW_STAMP_SPAN` below 21
  CV_iew stamps.  A false FAIL, not a wrong file passing.  Fix: widen by the VM evidence, as W36-0f.4.3 did, or
  state the cut-off date; correct the "the VM's lie within" claims in the evidence note.
- **R36G0f-5 (Medium):** the stale-memory heap bytes (+58..59 of a Task block, +58..61 of an assignment block)
  are masked on the block's shape alone, with no value check, and the resource block has neither a high-half
  check nor heap bytes.  Fix: bound them to a user-mode address range or leave them compared and record the
  pairs OPEN.
- **R36G0f-6 (Medium):** two meta-test survivors: `stale-half-compare` (no case with +38's high half changed
  alone, +40..41) and `view-width-neighbour` (no case touching +1182..+1183 beside a masked width with the date
  unset on both sides).  Fix: add the two cases.  Also `slot-capacity-lowerbound` (a zero-length slot table,
  no committed file has the shape).
- **R36G0f-7 (Low):** the note machine fields are checked as a format, not as the VM's value: a `deflang` of 1
  to 65535 and a Riched20 build of 1 to 5 digits pass.  The narrow form accepts the three known builds and
  languages (2057 and 16.0.20430 for the VM; 1033 and 20527 for the hosts).
- **R36G0f-9 (Low):** the evidence note says the view word's upper u32 span is that "of the 624 committed
  values and the VM's"; the committed minimum is 25035936 and the lower bound is the VM's.  Wrong as evidence
  text, harmless to the verdict.
- **Found at W36-C.2, queued:** the VM recordings that rewrite the stamp of a row or block the control and the
  reference share (the 15 Task, 6 assignment and 5 resource targets of 0f.4.7) FAIL again as data.  Either a
  control/null pair shows the stamp moving in an existing row, or those recordings stay OPEN.

## Queued from wave 36b (CLOSE): the wave 36 review's four Lows (22 November 2026)

The whole-wave review is `wave36-verification/review-wave.md`.  Wave 36b fixed R36W-1 to -6 (`2f1c174`, `9e5fb70`,
`8cb3e52`, `f0b55b1`, `e02a030`, `c3a4be8`; R36W-2 is `9e5fb70`, R36W-1 `2f1c174`).  The four Lows are queued, not
fixed, because the brief names six items and no more; no wave is assigned:

- **R36W-7 (Low):** the removed trace notes are still cited by name in `core/mppwrite/viewstream_edit.h`,
  `core/mppwrite/props_edit.h`, `app/app_edits_cell_bold_tests.cpp`, `tests/roundtrip_viewstream.cpp`,
  `tools/trace/new-trace-vm.ps1`, `tools/trace/odt-project-pinned.xml`, `tools/trace/trace_gate.py` and many
  "DOSSIER" cites.  Nothing reads a removed file at run time.  Fix: say "(private trace notes)" at each.
- **R36W-8 (Low):** the 44 deleted trace notes and the runbook stay in `main`'s history (`b2c0b19` and earlier).
  Fix: say so in START-HERE, or rewrite before the public push (the operator's call; history is not rewritten here).
- **R36W-9 (Low):** `trace_gate.py` `streams()` lists streams only, so a save missing an empty storage, or with
  other storage names or directory order, passes.  Fix: compare the storage names, or state the limit in the
  docstring.
- **R36W-10 (Low):** `review-g0e.md` and `review-g0f.md` list local worktree paths (`D:/dev/msproject-mac-wt-*`),
  the machine's layout.  Fix: replace them with a label.

## Queued from wave 36c (CLOSE): the wave 36b G0 review's seven findings (22 November 2026)

The review is `wave36b-verification/review-g0.md` (diff `e1b2ee5..706078b`; commit ids there and in the 36b records are
pre-rewrite).  No Critical or High.  Wave 36c is fix-only for the three CI failures and fixes none of these; no wave is
assigned:

- **R36bG0-1 (Medium):** the host's node is still printed in 19 notes under `spec/` and `docs/` as spaced GUID byte dumps
  (the node is the last six bytes of a version-1 GUID), and in `analysis/{com_validation,unnamed_inferred,unnamed_probe}.tsv`,
  `core/mppwrite/assignment_growth.cpp` and `tests/data/README.md`.  The W36-C.4 check's comment calls a spaced dump "data
  and out of scope", so it passes; its message and the 36b record's "Found, not fixed" say more than the tip holds.  The
  VM's node is gone from the tip in every form.  Fix: say plainly in the record that spaced GUID dumps keep the host's
  node (evidence bytes), or redact the last six bytes in the dumps and add the spaced and byte-reversed forms to
  `address_hits`.
- **R36bG0-2 (Low):** the view date mask blinds the gate in every pair whose control and null differ in a date word (21 of
  196 pairs; none of the five zoom references).  A zoom writer save recorded in such a session, with a wrong date at that
  word, passes.  Fix: one line in `trace-gate-fields-g0b.md`; if a zoom pair is ever recorded in such a session, require
  the target's word to equal one of the pair's two values or lie in the span of the pair's own dates.
- **R36bG0-3 (Low):** the address check (`address_hits` in `test_trace_gate.py`) walks only `.md` and `.txt` (not the 60
  `.json`, `.tsv`, `.csv`, `.py` under `spec/` and `docs/`), strips only `:` and `-`, and finds no dotted, spaced or
  byte-reversed form.  Confirmed by the reviewer's own scan: no VM hit, no host hit beyond R36bG0-1.  Fix with R36bG0-1.
- **R36bG0-4 (Low):** nothing pins the CI wiring: no test reads `ci.yml`, so deleting `MPP_REQUIRE_OLEFILE` silently
  restores the skip; the shard union was by construction (wave 36c's `trace_gate_meta_shards` now tests the derived
  union); the job names changed from `trace-gate-meta` (a required-status-check setting naming the old one would wait
  forever).  Fix: a test that parses `ci.yml` for the variable and the job names.
- **R36bG0-5 (Low):** the 36b CI limits were thin on a 2-vCPU runner (superseded for the derived shard by wave 36c's four
  shards; `trace_gate_meta_named` is unchanged: 24 min local, ctest 75 min, job 90).  Fix: measure the named shard on a
  runner, then set the limits.
- **R36bG0-6 (Low):** a stale heading above the correction in `spec/writers/props-records.md` (line 297, "Supersedes the
  insert refusal above (W34-2.2) for two ids"); the closing paragraph corrects it.  Fix: reword the heading.
- **R36bG0-7 (Info):** the insert refusal of `save_props_edit` is tested only through `props_insert_framing` (no save calls
  it); no test covers an id already in the typed-property section of a real stream.  Fix: if the framing is ever called by
  a save, add that case first.

## Queued from wave 36c (coordinator): the G0 review and the CLOSE check (22 November 2026)

The review is `wave36c-verification/review-g0.md`, the record `wave36c-verification/coord-verify.md`.  No Critical or
High; no wave is assigned:

- **R36cG0-1 (Medium):** the baseline (unmutated) run of `test_gate_guards_meta.py` is plain `subprocess.run`, not
  under the 2 GiB hold, so a gate that only fails under the hold shows as stale REDUNDANT entries, not as a red baseline.
  Fix: run the baseline through `run_limited`.
- **R36cG0-2 (Medium):** nothing goes red if `run_mutant` goes back to plain `subprocess.run` (the coordinator planted it:
  every test green); `--check-limit` tests `run_limited` directly.  Fix: a test that the real mutant path is held (for
  example, a mutant that allocates past the limit ends `red (raise)` with MemoryError).
- **R36cG0-3 (Low):** POSIX `preexec_fn` from pool threads (Python warns it is unsafe); a container limit under 2 GiB
  would abort the run only after the queue drains.
- **R36cG0-4 (Low):** Windows job object: the handle leaks if `SetInformationJobObject` fails; `CloseHandle` has no
  argtypes; `p._handle` is private.
- **R36cG0-5 (Low):** the 36c record cites `mem/cand136-capped.*` for "red in about two seconds" (that log ran 0
  candidates) and "N = 1 to 5" from a log holding N = 3 only; the coordinator saw 2 s and the reviewer N = 1 to 8 clean.
- **R36cG0-6 (Low):** `trace_gate_meta_shards` proves `partition`, not the `mine` filter `main` uses for `--shard`.
- **R36cG0-7 (Low):** stale comments in CMakeLists.txt (about lines 207 to 221) and ci.yml (about 355 to 363) still
  describe one derived test of 41 minutes; the derived job's name is now "(K of 4)", so a required check naming the
  old job would wait forever.
- **R36cG0-8 (Low):** edges: a `--shard` pick in another shard runs 0 and exits 0; `--check-shards 0` falls through to a
  full run; `--list` ignores `--shard`; a REDUNDANT cover in another shard relies on that shard; still no per-mutant
  timeout.
- **R36cC-1 (Low):** the START-HERE rewrite line says the host's node is "gone from text"; it stays in spaced GUID byte
  dumps and recorded data (R36bG0-1).

## Queued at the wave 36 merge (coordinator, 22 November 2026)

`main` = `2052d36` holds waves 36, 36b and 36c.  The id ledger of the 11 and 15 November work orders is
`wave36c-verification/id-ledger.md`; these are its open rows.  No Critical or High; no wave is assigned:

- **Closed, the lists were stale:** R35cG0-4 by `afdc3f2` (W36-2.3, the plan's Props digest; the G2 review's mutation went
  red).  R36cC-1 by this commit (the START-HERE rewrite line now says the host's node stays in dumps and data).
- **R36G0b-8:** no file the real Project wrote has met the gate through the positive sweep; the W-5 re-gate below is the
  first such run, so close it against that at wave 37 planning.
- **R36G0d-5 (R36G0c-10):** the control null proves nothing; changing it changes the capture specs (the coordinator's call).
- **R36G0e-1 to -5:** the G0e review's Medium and four Lows carry no id in a later record; the W36-0f.5 commits (derived
  and named guards) look like R36G0e-1.  Check each against `review-g0e.md` before wave 37.  R36G0e-6 reads as fixed by
  `464b742` (inferred, the record does not name it).
- **The OPEN pairs:** 22 of 202 (was 56): `test_trace_gate.py` positive sweep at `2052d36`, 180 PASS, 22 OPEN, 0 FAIL.
- **R36W5-1 to -11:** the W-5 re-gate's 46 FAILs (`wave36-verification/regate-w5/README.md`): R36W5-1 (17, VM row
  stamps), -2 (6), -3 (6, host references without the edit), -4 (2), -5 (3, noise without a mask), -6 (2), -7 (1), -8
  (6), -9 (1), -10 (1), -11 (1); 14 targets are undecided where the 15 November order counted six (D-6: findings).
- **A-5 residual:** `wave30-inactivate-second` and `wave30-level` still FAIL at the re-gate (R36W5-4, R36W5-10).
- **C-6 / W-4:** the round 7 insert pairs were not tested byte for byte against an insert rule (only a document re-check,
  `wave36-verification/gates-close.md`); G3 (Insert Task, Milestone, Summary) is not started.
- **The font item's 4(e) read-back** (W36-1.4): planned, not run; it needs Project, so the licence (D-7).
- **D-7:** no new recording without a Project 2021 licence; no decision recorded.  Default: no purchase without the
  operator; decide at wave 37 planning.
- **The CI limits:** `trace-gate-meta-named` took 60 minutes on the runner against a 90-minute job (R36bG0-5).
