# Wave 35 brief - the scheduler save path, view-state persistence and the wave 34 deferrals (for the Sonnet builder, coordinator role)

_Written Sun 8 Nov 2026 by the Opus coordinator, on the operator's word the same day
("start it in sonnet automatically").  Baseline: branch `wave35`, cut from `wave34b`'s tip
after its CLOSE marker, so wave 35 builds on the fixed wave 34 code.  Wave 34 and 34b merge
into `main` separately once their CI is green and their review has no Critical or High.  A
finding the coordinator's 34b verification raises after this launch goes into G0 here.
Sources: the wave 34 brief's G5 and G6 (not reached; the 12-hour stop rule), the "Queued
from wave 34" section of `docs/handoff/plan-waves27-30.md`, and the "Found, not fixed (G4)"
list in `docs/handoff/wave34-complete.md`.  `docs/handoff/standing-rules.md` binds this
wave.  The repo lives at `D:\dev\msproject-mac`.  The neutral root for every Project save is
`C:\w35\`, which already exists._

## 0. Publication

The coordinator pushes `wave35` and dispatches both CI workflows after each group marker.
CLOSE does neither.  Never rewrite or force-push history, and never work round the
pre-push guard.

## 1. Rules for this wave

`standing-rules.md` plus the wave 34 brief's section 1 rules 1-5 apply, with `C:\w34\`
read as `C:\w35\`:

- rule 1, Rule 1 is the whole wave;
- rule 2, one factor per pair;
- rule 3, decode before you write;
- rule 4, COM only through `scripts/oracle_lock.py` with `neutralise()`;
- rule 5, no exoproc.

The wave 34 brief's section 4 verification gate also applies.  In addition:

1. **The usual run rules.**
   - Groups in order, one fresh session each.
   - The marker `WAVE-35-<group>-COMPLETE` (CLOSE: `WAVE-35-COMPLETE`) is appended to
     `docs/handoff/wave35-complete.md` and committed.
   - One item id per commit, and gates per commit under `build/w35/`.
   - Deferral only with evidence of an attempt (the command, the error, the log path), never
     "with a reason".
2. **Commit trailer.**  Every commit ends with
   `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35)`.
3. **Plan quota.**  A rate-limit or usage-limit error is a pause the coordinator handles,
   never a deferral.
4. **Enable only what matches.**  A command is enabled only for a shape whose port save
   matches Project's file field by field, with `effects` on and a null save.  Every other
   shape is refused up front, with a test per refusal and a needle naming the specific gate.
   This holds in both the shared edit model and the writer (wave 34b's rule).
5. **Pin every parameter.**  When a pair is measured, the record lists every parameter a
   user can vary for that command, and which value the pair pinned for each:
   - task mode, duration, calendar, constraint, type and effort-driven;
   - resource type, rates, accrual and calendar;
   - view, row order and outline level.

   The refusal covers every unpinned value.  Wave 34's seven Highs were all unpinned
   parameters.
6. **Fixtures.**  Every new fixture is a Project save made on `C:\w35\` with neutral
   Author, Manager, Company, UserName and Title.
   - Scan it for UTF-16 and UTF-8 strings of names and company paths before its first
     `git add`.
   - Commit it in its own commit, with its SHA-256 in `tests/data/README.md` and
     `scripts/check_fixtures.py`, and its null save.
   - Run `python -I build/audit-w22/guard_names.py main..HEAD`,
     `python -I build/audit-w22/scan_client_names.py HEAD` and
     `python scripts/check_markers.py --range main..HEAD` before every commit.
7. **No names.**  Never name the operator or anyone else in docs, commits, fixtures or
   tests; refer to rows by ID.  LF line endings.
8. **Before any push-bound commit,** check for unused functions, variables and constants,
   `/*` inside block comments, and pointer compares of strings.  GCC, Clang and AppleClang
   reject them and MSVC does not; this is the class that broke wave 34's first CI run.

## 2. Groups

### G0 - carry-over fixes from the wave 34 verification

- **W35-0.1  (R34b-1 = RW34b-1, High)  Assign Resources and its removal never check the
  resource's base calendar.**
  - The coordinator's independent review of `023c858..3c69150` is in
    `docs/handoff/wave34-verification/review-34b.md`. The coordinator re-read the code at
    source and CONFIRMED the finding.
  - The writer (`core/mppwrite/assignment_growth.cpp:846-849`) only checks that resource row
    word 154 equals the resource UniqueID + 2. That is the calendar's identity, not its
    contents.
  - The edit model (`app/edit_model.cpp` ~2868-2962) checks only the task's own calendar.
  - So a resource calendar that is based on another calendar, or that carries an exception
    inside the task's five days, passes `push()` and the save. Project schedules that
    assignment on the resource calendar; the port writes a flat 40 h over the task's own
    dates.
  - Fix:
    - In both layers, read the resource's `TBkndCal` row and its data, and refuse unless they
      equal the measured pair's resource calendar: same base calendar, same working times,
      no exceptions.
    - Add one refusal test per shape (based on another calendar; an exception inside the
      task's dates), each with a specific needle, at both levels and for add and removal.
    - The four measured pairs still write and match.
- **W35-0.2  (R34b-2 to R34b-6; Mediums of the same review, with RW34b-2 and RW34b-3 from
  `wave34b-complete.md`)**, one commit per id:
  - **R34b-2:** task type, effort-driven, the resource base calendar and the removed
    assignment's units and work are checked only in the writer. Add the same refusals to the
    shared edit-model builder, so the user is refused before anything is queued.
  - **R34b-3:** the writer's task-row template compares a timestamp-like word at +128, so
    Assign refuses every real file not derived from the fixtures, with a misleading message.
    Decode the word against the pairs and their null saves. If it is per-save noise, drop it
    from the comparison and cite the null-save offsets. Otherwise make the refusal message
    name it.
  - **R34b-4:** the resource-pair Fixed2Data +24 identity is still masked. Compare it with
    Project's file, as W34b-0.2 did for +16/+32, and show a planted wrong value going red.
  - **R34b-5:** Add Resources writes old + 1 as the new resource's calendar reference
    (word 154) without checking it against the calendar uid it appends. Assert they agree,
    in the writer and in a test.
  - **R34b-6:** the wave 30 task-mode exclusive edit lacks the pending-document-edit refusal
    that W34b-1.3 added to the five other kinds. Add it, with a test.
  - **RW34b-2 / RW34b-3:** sharpen the remaining generic Qt needles, and add a Qt gate test for
    each new 34b refusal.
- **W35-0.3  (CI-n)  Any red job in the CI run of `wave34b` at `3c69150`.**
  - The run is `qt` 42479681310 and `ci` 42479685193. It is the first run with macos-qt on
    `macos-15`, because GitHub retired `macos-14`.
  - Result: `ci` is green on all three jobs. In `qt-shell`, linux-qt and windows-qt are
    green, and **macos-qt fails one test (CI-4).**
  - **CI-4: `qt_assign_resources_gate` fails on macOS only.**
    - The failing check is "an assigned task is offered the removal"
      (`app_qt/tests/assign_resources_gate_test.cpp`, the first of the two checks with that
      text).
    - Log: run 42479681310, job macos-qt, step "Test (offscreen renders + parity geometry)".
      Also saved as `build/verify-w34b/ci-macos-qt.log` on the coordinator's machine.
    - The test arrived in W34-4.3. macos-qt has not run since 29 Oct 2026, so this is its
      first macOS run.
    - The test's `Prompt` hook does not see the Yes/No question on macOS. Decide, from the
      hook and from the handler in `app_qt/main_window.cpp`, which of two causes it is:
      - a harness difference: on macOS a `QMessageBox` may be window-modal (a sheet), so
        `QApplication::activeModalWidget()` or the timer-based catch behaves differently;
      - a real shell difference: the removal prompt is not shown on macOS.
    - Fix whichever it is, mechanically. The Windows and Linux runs stay green, and macos-qt
      goes green at this wave's first CI dispatch. If it is a harness fix, also check every
      other gate test that uses the same `Prompt` hook.
    - No macOS machine is available here, so CI is the only check. Record that.
- **W35-0.4  (RC-W1, RC-W2, RC-W6, RC-W7, RC-W8)  The writer-width Mediums from
  `docs/handoff/wave34-verification/close-review.md`.**  Make each writer refuse what the
  shell builder already refuses, with a test for each:
  - **RC-W1:** the first-assignment add has no gate on the placeholder row count.
  - **RC-W2:** a lower new resource uid in a settled add.  Refuse it, unless
    `wave34-assign-add-third` measures that order; then cite the pair.
  - **RC-W6:** `save_var_record` accepts any field id.  Whitelist the measured ids.
  - **RC-W7:** `props_edit` accepts any record id and tag.  Whitelist the measured ones.
  - **RC-W8:** Bold stores any row uid and field id.  Pin the measured ones.

### G1 - the scheduler save path, stage 1 (was wave 34 G5)

- **W35-1.1  Decode note for Calculate Project.**  Write it in `spec/writers/calculate.md`,
  from `wave30-calculate-*` and `wave34-calculate-second`.
  - Sources: the recalculation engine's facts (`plan_recalc` already reproduces Project's
    numbers 332/332 on the real file) and the decompiled `TBkndRecalc` entry points.
  - It states which task, assignment and project words Project rewrites after F9, the nine
    new VarMeta entries, and the summary strings.
- **W35-1.2  `mppwrite::save_plan` becomes a measured writer for ONE shape.**  It moves
  from test-only, and the shape is a pending Duration change on an unlinked, automatically
  scheduled leaf, then F9, then save.
  - Measure two more pairs before enabling it for linked files: a linked pair with a lag,
    and a constraint.
  - If the writer cannot match Project field by field after the decode and three pairs,
    defer with the decode note as evidence and list the differing fields.
- **W35-1.3  Calculate Project enabled for the measured shapes only.**  Respect Links, Move,
  Auto Schedule and Level All stay disabled until their own pairs match.

### G2 - view-state persistence (was wave 34 G6)

- **W35-2.1 to 2.4**  These four settings are session-only today, with the file's bytes
  kept.  Measure one pair each, through COM (`214/CV_iew` and the `CTable` applied index),
  and write only what the pair shows:
  - 2.1 sort order;
  - 2.2 applied table;
  - 2.3 outline level;
  - 2.4 zoom.
- Measure the Zoom Custom span before any zoom write.

### G3 - the wave 34 deferrals, decode first

Each item starts from the evidence its wave 34 deferral recorded (`wave34-complete.md`,
"Found, not fixed (G4)").  Each decodes and measures until the port's save matches, then
enables the measured shape only; otherwise it defers again with new evidence.

- **W35-3.1  Insert Summary and Insert Milestone (W34-4.4).**  Decode the insert rule set
  from the two wave 30 pairs, plus at least one insert pair with a non-trivial outline:
  - ID renumbering and reparenting;
  - the 2.5 ordinal;
  - the summary and milestone words;
  - the assignment placeholder rows;
  - CEdl and the Props tail.
- **W35-3.2  Set Baseline 0 for the entire project (W34-4.7).**  Decode the 94 new var
  blocks (26 undecoded), the new calendar row, the four tables' presence bits and the Props
  tail growth.
- **W35-3.3  Resource Notes and Task Notes (W34-4.1, W30-1.5).**  Project's RichEdit
  serialisation needs four things worked out: the RTF header, the font table, escaping and
  the saving machine's generator string.
  - If the generator string cannot be reproduced, record exactly which bytes depend on the
    saving machine.
  - In that case Notes stays disabled.
- **W35-3.4  Font controls beyond one Bold cell, and WBS Renumber.**  One pair per factor:
  - another cell, a range, Bold off;
  - Italic and Underline, then font name, size and colour;
  - Renumber on the measured three-task file.
- **W35-3.5  Removing an assignment from a file the port itself saved (W34-4.3).**  Measure
  it with a pair; until it matches, it stays refused.

### G4 - the macOS shell (RC-R4, RC-R5)

- **W35-4.1**  `app/main.mm` today wires only Add Resources from wave 34, and that handler
  is read but not run.  Wire rename, Assign Resources, Bold and Define Code, plus each
  command G1-G3 enables, through the same shared builders the Qt shell uses.  Or record, per
  command, why it stays unwired.
  - No refusal may exist in one shell only.
  - Keep each change mechanical; CI compiles it.

### CLOSE

- Fresh-tree gates on all trees, with warnings as errors and the wave 33 CI set.
- Both name scans and the marker check.
- Reviews of `git diff wave34b..wave35`: one agent on the writers (rule 1, and every
  parameter pinned or refused) and one on the refusals and shells.
- The decode notes indexed from `spec/README.md`.
- `wave35-complete.md`, START-HERE, INDEX, `memory/coordinator.md` and the regenerated
  backlog.
- Record "push and CI: by the coordinator".
- Then append `WAVE-35-COMPLETE`.

## 3. Queued (not for this wave)

These stay in `plan-waves27-30.md`:

- the Lows (R34G3-5 to R34G3-9, RC-W9, RC-W10, RC-R4 to RC-R8, R34G4-10, R34G4-11);
- Respect Links, Move, Auto Schedule and Level All;
- the Team Planner, Network Diagram, Calendar and Report views;
- the constraint-mso engine case;
- macOS launch and notarisation.

## 4. Coordinator's part after each marker

- Fresh-tree gates in the verify worktree.
- Mutations at the call site and in the helper of every new gate.
- One independent read-only review per group that adds or widens a writer.
- The brief section 4(e) Project reopen check of each newly enabled save.
- Push and CI after each marker.
- Merge only with CI green on all three OSs and no Critical or High; otherwise a fix-only
  `wave35b` first.
