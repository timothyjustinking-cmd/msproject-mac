# Wave 34b brief - fix-only: the wave 34 reviews' Highs and the first CI run (for the Sonnet builder, coordinator role)

_Written Sun 8 Nov 2026 by the Opus coordinator.  Baseline: branch `wave34b`, cut from
`wave34` after its verification record (`docs/handoff/wave34-verification/`).  Sources:
`review-g3.md` (R34G3-n), `close-review.md` (RC-n), `review-g4-close.md` (R34G4-n, the
coordinator's independent review of `3a8a31c..8400c2c`), and the first CI run of `wave34`
(`ci` 42453370829, `qt-shell` 42453367272, `app-macos` 42453351442, all on `8400c2c`).
`docs/handoff/standing-rules.md` binds this wave.  The repo lives at `D:\dev\msproject-mac`.
**This wave adds no writer, lifts no refusal and measures no new Project pair: every change
is a refusal, a test, a bounds fix, a compile fix or a record.**  Where a fix below says
"refuse", the shape is unmeasured; refusing it is the whole fix, and a later wave may lift
it with a pair._

## 0. Publication

The coordinator pushes `wave34b` and dispatches both CI workflows AFTER CLOSE.  CLOSE does
neither.  Never rewrite or force-push history, and never work round the pre-push guard.

## 1. Rules for this wave

`standing-rules.md` plus the wave 34 group rules apply:

- **Groups.** Run the groups in order, one fresh session each.
- **Markers.** Append the marker line `WAVE-34b-<group>-COMPLETE` (CLOSE: `WAVE-34b-COMPLETE`) to
  `docs/handoff/wave34b-complete.md` and commit it when the group is done.
- **Commits.** One finding id per commit (an item may close several ids of one family; list them
  all in the message).
- **Logs.** Log the gates per commit under `build/w34b/`.
- **Deferral.** Only with evidence of an attempt: the command, the error and the log path.
- **Red, then green.** Each behaviour fix has its test seen RED before the fix and GREEN after.
  Quote both runs in the commit message from the log.
- **Specific needles.** Every refusal test asserts a needle that names the specific gate (the
  shape it refuses). Never use the command's prefix or "not implemented in this port" alone
  (R34G4-9).
- **Both layers.** Refuse in the shared edit-model builder that both shells call, AND in the
  writer (defence in depth), so a refused shape fails in `push()` and in the core writer alike.
  Tests at both levels.
- **The measured shape still writes.** It must still match Project's pair field by field, with
  `effects` on.
- **Trailer.** Commit messages carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 34b)`.
- **No names.** Never name the operator or anyone else in docs, commits, fixtures or tests; refer
  to rows by ID.
- **Scans.** Run `python -I build/audit-w22/guard_names.py main..HEAD`,
  `python -I build/audit-w22/scan_client_names.py HEAD` and
  `python scripts/check_markers.py --range main..HEAD` before every commit.
- **Line endings.** LF everywhere.
- **No COM.** Do not open Project or run any `oracle_*` script in this wave.

Read the cited review rows in full before each item; the file:line references are at
`8400c2c`.

## 2. Groups

### G0 - the Highs and the CI failures

- **W34b-0.1  (R34G3-1 = RC-R1, R34G4-1, R34G4-2; High)  Assign Resources and its removal
  accept unmeasured shapes.**  Every assignment pair is the same narrow shape:
  - pairs: `wave30-assign-add`, `wave30-assign-delete`, `wave34-assign-add-second` and
    `wave34-assign-add-third`;
  - fixture conditions: `tests/data/README.md` rows 78 and 103.

  The shape is:
  - a MANUAL, top-level, 0 % leaf task of the measured duration on the project calendar
    (no task calendar);
  - a Work resource at 100 % Max. Units with default (zero) standard, overtime and per-use
    cost and the default accrual, on its own base calendar;
  - Units 1.0, adding exactly the measured work per assignment (40 h on the 5-day task).

  `assignment_plan_refusal` (`app/edit_model.cpp` ~2816-2860) checks only summary, started,
  material, Max. Units and an existing assignment. The writer's `check_assn_row_work`
  (`core/mppwrite/assignment_growth.cpp:748-770, 815-830`) and `summary_info.cpp:629-663`
  accept any positive whole-tick work.

  Fix: read the measured values from the pairs (the README rows and the pair tests) and
  refuse, with one test each:
  - an automatically scheduled task;
  - a task whose duration or resulting assignment work differs from the measured one;
  - a task with its own calendar;
  - a resource with any non-zero rate or cost per use, a non-default accrual or a
    non-standard base calendar;
  - any task constraint, type or effort-driven setting that differs from the pair's.

  Apply this to both the add and the removal builder, and to the writer's work and
  summary-string checks. The four measured shapes still write and still match their pairs.
- **W34b-0.2  (R34G3-2, RC-W4; High)  Identity references masked in the harness.**
  `tests/roundtrip_assignment_growth.cpp:343-350` masks +0, +16 and +32 of the appended
  assignment Fixed2Data record (used at :637, :822, :1232).
  - Mask only what the null save proves is per-save noise (+0 if it is the record's own
    counter).
  - Compare +16 (task reference) and +32 (resource reference) with Project's file in every
    pair.
  - Plant a wrong resource reference in a scratch copy of the port's output and show the
    comparison goes RED. Quote the run.
  - RC-W4 (`tests/roundtrip_resource_growth.cpp:280-292`, the calendar Fixed2Data +32):
    decide from the null save and the pair whether it is a counter or a reference. If it
    is a reference, compare it. If it is noise, cite the null-save offsets that prove it
    in a comment and in the record.
- **W34b-0.3  (R34G4-3, RC-W3; High)  Add Resources clones the last resource's whole row.**
  `core/mppwrite/resource_growth.cpp:496-609` copies the 172-byte row of the last live
  resource, checking only MaxUnits and two var blocks. The edit model checks only type and
  rates.
  - Reproduction: `tests/data/wave30-assign-add.mpp`, whose last resource carries 40 h of
    work, passes every check and yields a new resource with 40 h of work and no assignment.
  - Fix: in the writer and the edit model, refuse unless every template-row field outside
    the ones the writer rewrites (uid, id, name and its var refs) equals the measured
    template row in Project's pair: work, cost, actual and remaining figures, accrual,
    type, calendar reference and flags.
  - Tests: the reproduction above is refused at both levels, with a specific needle; the
    measured pair still writes and matches.
- **W34b-0.4  (R34G4-4; High)  Font Bold always writes into the view with UniqueID 2.**
  - `kMeasuredViewUid` (`core/mppwrite/viewstream_edit.cpp:396`) is pinned, but nothing
    proves view 2 is the file's applied task view, or that its text style matches the
    pair's. The font word `0x90000001` is undecoded.
  - Fix: refuse unless the file's applied view is UniqueID 2 AND the view record the
    writer edits is byte-equal to the measured control's, outside the bytes the edit
    changes. Apply this in the writer and in the shell builder.
  - Tests: a file whose applied view is not uid 2 (any committed fixture with another
    applied view, or a scratch copy) is refused at both levels, and the measured cell still
    writes and matches.
- **W34b-0.5  (R34G4-5, RC-W5; High)  Define WBS Code numbers tasks by UniqueID and the
  writer accepts nested files.**
  - `core/mppwrite/wbs_code.cpp:191` writes `number_payload(uid)`.
  - Fix: refuse unless every task's ID equals its UniqueID and the rows are in that order.
  - Also refuse in the writer (`wbs_code.cpp:66-107`) any task with outline level other
    than 1, the summary flag, or a FixedMeta state other than the pair's (RC-W5). The
    shell already refuses that; the writer must too.
  - Tests: a three-task copy whose rows are reordered (ID != UniqueID) is refused, and so is
    a nested three-task copy, at both levels. The measured pair still writes and matches.
- **W34b-0.6  (CI-1; gcc, clang, AppleClang)  `core/mppwrite/props_edit.cpp:245`: unused
  function `owned_stream` (`-Werror=unused-function`).**  It breaks `ubuntu-gcc`,
  `ubuntu-asan` and `macos-appkit` at the first compile error, so more may sit behind it.
  - Remove the function or use it.
  - Then sweep every file wave 34 added or changed (`git diff --name-only main..wave34 --
    core app app_qt tests`) for unused `static` and anonymous-namespace functions,
    variables and constants, set-but-unused variables, `/*` inside block comments, and
    pointer compares of strings. GCC and Clang reject all of these; MSVC does not.
  - List what the sweep found in the commit.
- **W34b-0.7  (CI-2; gcc 15)  `core/gantt/gantt.cpp:390` `append_calendar_day_spans`:
  `-Werror=array-bounds` in `std::sort` over a prefix of the 8-element `working` array**
  (`linux-qt`, run 42453367272, log `build/verify-w34close/ci-linux-qt.log`).
  - The code did not change in wave 34. The runner's compiler moved to gcc 15 since
    `main`'s last green qt run (27 Oct 2026), so this is drift on old code.
  - `count` is bounded by `working.size()`, so the read cannot happen. This is a gcc 15
    false positive after inlining.
  - Fix it mechanically, with no behaviour change. For example, sort a `std::span` of
    `count` elements, or replace the sort with a bounded insertion sort over at most
    `kMaxDayIntervals` entries. Do not use a blanket `-Wno-array-bounds`.
  - The gantt tests stay green. Sweep `core/` for the same pattern (`std::sort` over
    `array.begin() + count`) and fix each match the same way.
- **W34b-0.8  (CI-n)  Any further red job.**  The coordinator re-dispatches CI after G0 and
  adds each new failure here as its own item before G1 starts. If none is listed, this item
  is "none".

### G1 - the Mediums

- **W34b-1.1  (R34G3-3, Medium)  32-bit wrap in bounds checks.**
  - `assignment_growth.cpp:367` `entry.offset + 4 > var2.size()` wraps for offsets at or
    above `0xFFFFFFFC`, and `get_u32` then reads out of bounds.
  - Do the check in `std::size_t` with no overflow, of the form `offset > size || 4 > size
    - offset`.
  - Sweep every loader wave 34 added (`assignment_growth`, `resource_growth`, `wbs_code`,
    `viewstream_edit`, `varmeta_edit`, `props_edit`, `summary_info`) for the same pattern.
  - Test: a crafted copy with offset `0xFFFFFFFD` is refused cleanly.
- **W34b-1.2  (R34G3-4, R34G4-9; Medium)  Untested refusals and generic needles.**
  - Add tests for the count-3-to-4 real-assignment refusal, the real-assignment count
    mismatch, "more than one free placeholder row" and "no free placeholder row"
    (`assignment_growth.cpp:1025-1053`).
  - Sharpen every wave 34 refusal needle that matches any refusal: "add resources:",
    "assign resources:", "measured" and "not implemented in this port" alone.
- **W34b-1.3  (RC-R3, R34G4-8; Medium)  An exclusive edit can share a save with pending
  document edits.**
  - The "exclusive edit alone in the save" rule (`app/edit_model.cpp` cascade ~4510-4585,
    5243) ignores pending `document_edits` (table format, formula).
  - Refuse that combination for the five new single-edit kinds: rename, Add Resources,
    Assign or remove, Bold and Define Code.
  - Test each kind, with the queue unchanged after the refusal.
- **W34b-1.4  (R34G4-6; Medium)  Names Project's UI rejects.**
  - Resource Information rename and Add Resources accept `,`, `[` and `]` in a name, and
    the rename has no length cap below 65,535 bytes.
  - Refuse those three characters.
  - Refuse any name longer than the longest name a committed pair measures. Cite the pair
    and the length in the refusal's comment.
  - Test each.
- **W34b-1.5  (R34G4-7; Medium)  Records that overstate the code.**  Append dated erratum
  lines, never rewriting earlier text, to:
  - `docs/handoff/wave34-verification/close-review.md`: "refuses every other shape" in the
    shell was wrong for Assign (now fixed in 0.1), Add Resources, Bold and Define Code
    (0.3-0.5). And no G4 entry carried Project-reopen evidence for brief section 4(e);
    that check is the coordinator's.
  - `docs/handoff/wave34-complete.md`, the 4.3 row: the identity dwords were masked, not
    compared, until 0.2.

### CLOSE

- **Gates.** Run fresh-tree gates on the final commit, logged: core, app and app_qt with
  `CORE_WARNINGS_AS_ERRORS=ON`, M2, the engine suite, `check_fixtures`, `check_markers`.
- **Scans.** Both name scans and the marker check clean on `main..HEAD`.
- **Review.** One review pass over `git diff wave34..wave34b`, with one agent on the
  writers and one on the refusals.
- **Push and CI.** Record "push and CI: by the coordinator" in `wave34b-complete.md`.
- **Docs.** Update START-HERE and `memory/coordinator.md`: wave 34 and 34b, not merged until
  CI is green and the review has no Critical or High.
- **Marker.** Then append `WAVE-34b-COMPLETE`.

## 3. Queued (not for this wave)

These stay in `plan-waves27-30.md` for wave 35; CLOSE adds any that are missing, with
file:line:

- RC-W1, RC-W2, RC-W6, RC-W7 and RC-W8;
- R34G4-10 (UniqueID issue counter);
- R34G4-11 (the WBS dry run's predictable temp path);
- R34G3-5 to R34G3-9;
- the CLOSE review's Lows.

The brief section 4(e) Project reopen check of the enabled saves is the coordinator's,
after this wave.
