# Wave 28 brief - the editing core (for DeepSeek, coordinator role)

_Written 2026-10-24 by the Opus verifier session.  Baseline: branch `wave28`, cut
from `wave27` (its final commit).  Part of the unattended run of waves 27-31 Tim
approved (`plan-waves27-30.md`).  `docs/handoff/standing-rules.md` binds this wave;
rule 1 (every save path measured against a Project-saved file before it is enabled)
is the heart of it.  Sources: the plan, and `wave27-verification/review.md` (its
Critical/High findings, if any, are G0)._

## 0. Publication

The hold is lifted: push `wave28` and dispatch both CI workflows in CLOSE, as the
standing rules say.  Never rewrite or force-push history; never work round the
pre-push guard.

## 1. Rules for this wave

`standing-rules.md` plus the group rules used in waves 26-27: groups in order, one
fresh session each, the marker line `WAVE-28-<group>-COMPLETE` appended to
`docs/handoff/wave28-complete.md` and committed when the group is done, deferral only
with evidence of an attempt (a commit or a Project measurement log under `build/w28/`),
Critical/High findings you discover fixed first, the rest under "Found, not fixed".

**The measurement recipe for every save path in this wave:** on a neutral copy of a
synthetic fixture (neutral Author/Manager/Company/user name, saved under `C:\w28\`),
have Project 2021 make the edit through COM (`scripts/oracle_lock.py`), save, and
commit the before/after pair as fixtures in their own commit.  Then implement the
port's save so that the streams it changes, and the values in them, equal Project's;
the test compares the port's save with Project's file field by field (not by
re-reading through the port's reader alone).  Fields Project changes that the port
cannot yet produce are a failing check, not a skipped one.

## 2. Groups

### G0 - wave-27 verifier findings (Critical/High only)

From `docs/handoff/wave27-verification/review.md` (0 Critical, 2 High) plus two items
the verifier promotes because they undermine every gate:

- **W28-0.1  High (privacy)  Fixtures embed the local save path.**  Seven committed
  `.mpp` fixtures carry the full Windows path of the folder they were saved from
  (UTF-16LE, in Props and `214/CEdl/Var2Data`), which contains the operator's name and
  the company Dropbox folder: `wave27-unlink.mpp`, `wave27-unlink-control.mpp`,
  `wave27-priority-control.mpp`, `wave27-priority-750.mpp`,
  `wave27-percent-actuals.mpp`, `wave23-flags.mpp`, `synthetic-calendars.mpp`.  Fix
  each in the tree: re-save through Project from a neutral folder (`C:\w28\`), or, where
  a recorded oracle or pinned hash depends on the file (`synthetic-calendars.mpp` is the
  engine suite's calendar base), replace each path string in place with a neutral
  string of exactly the same length and record why the hash changed.  Add a check to
  `scripts/check_fixtures.py` that fails on any tracked fixture containing a drive path
  or user-profile path at either byte alignment.  Do not rewrite history (Tim decides).
- **W28-0.2  High (probable)  Outline numbers in `app_qt/tests/task_order_test.cpp`.**
  W27-3.5 pins outline numbers that cannot occur in Project (ID 1 = "2", ID 12 = "1",
  while the rows display in ID order at levels implying 1, 1.1, ... 2); the port derives
  them in `core/mpp/reader.cpp` around line 3014.  Read `Tasks(1).OutlineNumber` and
  `Tasks(12).OutlineNumber` through COM on a copy of the corpus file; if Project says
  "1" and "2", fix the reader's outline-number derivation and the test.
- **W28-0.3  (promoted Medium)  The intermittent `cal` crash.**  `tests/mpp_fixture_builder.h`
  around line 295 sizes the FAT vector as `total*4` entries but reads 128 per FAT
  sector, so it over-reads the heap on every run; CI crashed once on it.  Fix with
  `resize(fat_sectors * (kSector/4))`; confirm with a Debug build.
- **W28-0.4  (promoted Medium)  Unlink under standing rule 1.**  Project's Unlink save
  also changes dates on the two task rows (it reschedules) and a few FixedMeta/Props
  words the port does not write.  Measure Project's reopen of the PORT's save: if
  Project shows the same dates and links as its own save, keep Unlink and record the
  measurement; otherwise make Unlink run the scheduler and write the dates Project
  writes, or disable it.

### G1 - the actuals writer

- **W28-1.1** Measure Project's edits of % Complete on an auto-scheduled leaf task at
  0 -> 40 %, 40 -> 100 % and 100 -> 0 %, and on a manually scheduled task at 40 %; one
  save pair each.  Record which fields change: % Complete, Actual Start/Finish, Actual
  and Remaining Duration, % Work Complete, the summary and project roll-ups, and the
  Var2Data field 0x0B40015E the wave-26 review found.
- **W28-1.2** Implement the actuals writer so the port's save equals each pair; then
  re-enable the five % buttons (W27-0.1) and Task Information's percent field.  First
  make `make_percent_edit` refuse uids that are not saved tasks (the wave-27 review: it
  accepts any uid, and a pending insert's value would be dropped while the save reports
  success), and make every save-pair test also assert that no other stream changed
  against the control file.
- **W28-1.3** Mark on Track and Update Project (update work as complete through a
  date) on the same writer, each with its own Project pair; Update Project's OK becomes
  functional for the options the writer covers, the rest stay disabled.

### G2 - outline editing

- **W28-2.1** Indent and outdent: measure Project's saves (one leaf under a sibling,
  a summary with children, outdent back), implement the writer (outline level, parent,
  the summary flag and roll-up), enable the two ribbon buttons, keyboard shortcuts and
  menu items.  Undo/redo covered.
- **W28-2.2** The sheet and Gantt reflect the new outline at once (collapse triangles,
  indentation, summary bars).

### G3 - new-task mode

- **W28-3.1** Measure Project's save of the project's new-task mode (props
  `0x024013c8`, decoded in wave 22) when changed in Options > Schedule; write it; Insert
  Task uses it; the status bar follows it.

### G4 - the Task Form edits

- **W28-4.1** Make the docked Task Form's fields editable through the same edit model
  and save paths as the sheet (no new write path; disabled where the sheet's is).
  Selection sync and undo covered.

### G5 - split task

- **W28-5.1** Measure Project's Split Task on a leaf task (one split) and decide from
  the bytes whether the writer can produce it this wave.  Implement and enable it if so;
  otherwise defer with the measurement log as evidence (the button stays disabled with
  the exact tooltip).

### CLOSE

Fresh-tree gates on the final commit, logged; the name scan clean; a review pass over
`git diff wave27..wave28`; push and dispatch both workflows (fix and re-dispatch on
red); `wave28-complete.md`, START-HERE, INDEX, `memory/coordinator.md`; the 16
decisions re-scored.  Then append `WAVE-28-COMPLETE`.
