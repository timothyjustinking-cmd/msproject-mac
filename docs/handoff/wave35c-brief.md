# Wave 35c brief: fix-only, covering wave 35b's open Highs, Mediums and Lows (for the Sonnet builder)

_Written Mon 9 Nov 2026 by the Opus coordinator from the wave 35b verification._

**Baseline.**  Branch `wave35c`, cut from `wave35b` at `2a97272`.  `wave35b` was rebuilt and
pushed, so the commit ids in `wave35b-verification/` before `coord-gates-close.md` are
pre-rebuild.  `history-rebuild-wave35b.map` in that folder maps each old id to its new one.

**Sources**, all in `docs/handoff/wave35b-verification/`:
- `review-g0.md` to `review-g4.md` (R35bG0-n to R35bG4-n);
- `review-wave.md` (R35bW-n);
- `coord-gates-g0.md` to `coord-gates-g4.md` and `coord-gates-close.md` (R35bW-CI1, R35bG4-6).

Every finding below has a **Fix:** paragraph in its review.  Read it before the item; this brief
does not repeat it.  `docs/handoff/standing-rules.md` binds this wave.

**This wave adds no writer, lifts no refusal and measures no new Project pair.**  Every change
is one of:
- a refusal;
- a test or a test fix;
- a reader field a refusal needs;
- a shell wiring fix;
- a record.

Where a fix says "refuse", the shape is unmeasured, and refusing it is the whole fix.

## 0. Publication

The coordinator pushes `wave35c` and dispatches both CI workflows AFTER CLOSE.  No group, and
not CLOSE, pushes, dispatches a workflow or rewrites history.  Never force-push, and never work
round the pre-push guard.

## 1. Rules for this wave

The wave 35b brief's section 1 rules apply unchanged, with `35c` for `35b`.  In short:
- groups in order;
- the marker `WAVE-35c-<group>-COMPLETE` (CLOSE: `WAVE-35c-COMPLETE`) in
  `docs/handoff/wave35c-complete.md`, committed;
- one finding id per commit;
- gates per commit under `build/w35c/`;
- deferral only with evidence of an attempt;
- red then green, both quoted;
- specific needles: never the command prefix, and never "not implemented in this port" alone;
- both layers: the edit-model builder and `push()`, and the writer;
- the measured shape still writes and matches its pair with `effects` on;
- the trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35c)` on EVERY commit,
  checked by script before each commit (wave 35b G2 missed it on 11 commits);
- no names;
- the three scans before every commit;
- LF line endings;
- **no COM**.

Also:
- **Name your records so they don't clash with the coordinator's.**  The coordinator's files
  are `coord-gates-*.md` and `review-*.md` in `docs/handoff/wave35c-verification/`.  The
  builder writes `gates-gN.md` there, and nothing else in that folder.
- **The completion file holds more than the marker.**  Each group's row in
  `wave35c-complete.md` carries the group's gate line, "Guards seen red", "Deferred" and
  "Found, not fixed", then the marker.  Waves 35 and 35b wrote the marker line alone.
- **"Fixed" means fully closed.**  An id a fix closes only in part is recorded as "partly
  fixed: <what remains>", never "fixed" (R35bW-5).
- **The GCC/Clang warning classes MSVC does not report** must be read for in everything you
  touch:
  - `-Wswitch`;
  - unused results;
  - sign compares;
  - **`-Wdangling-reference`**: a function returning a reference while a temporary binds to
    one of its reference parameters.  This is R35bW-CI1, below.
- **Run ctest by its real name**, and treat "No tests were found" as a failure.
- **`app/main.mm` cannot be compiled here.**  CI is its only compile gate.

## 2. Groups

### G0: the Linux build break and the two Calculate Project Highs

- **W35c-0.1 (R35bW-CI1, blocks merge): GCC 13 stops at `-Werror=dangling-reference`.**
  - The lines are `core/mppwrite/viewstate_outline.cpp:435, 442, 449` (`require_applied_view`).
  - `unique_edl_item` returns `EdlItem const&` into the caller's `items`.  Its `what`
    parameter is `std::string const&`, bound to a string literal's temporary.
  - **Fix:** take `what` as `char const*` or `std::string_view` (or return a pointer or an
    index), and sweep `core/` for the same pattern.
  - **Done when:** the coordinator's CI run shows ubuntu-gcc and linux-qt green.  Windows cannot
    show this warning, so record the change and the sweep as "CI is its gate".
- **W35c-0.2 (R35bG0-1, High): the "leaf starts at the project start" check compares against the
  stale row 0 roll-up.**
  - Read Props `0x02400002` (the project start the writer stamps into Fixed2Data +46) in the
    writer's `read_calculate_shape`.
  - Add a reader-filled `PlanInput` field for it.
  - Refuse, in both layers, unless the leaf Start is the first working instant at or after
    that Props start on the pinned Standard calendar.  This is the measured relation:
    Sunday 21:00 maps to Monday 08:00.
  - Tests, in both layers: the control with only Props `0x02400002` moved by a week, with a
    needle such as "project start in 114/Props".
- **W35c-0.3 (R35bG0-2, High): the project's calculation options in 114/Props are not pinned.**
  - Pin `114/Props` to the measured control in the writer: every record id and payload equal
    to the control's, except:
    - the records the save itself writes (`0x02400003`, `0x02400024`, the tail key
      `0x00010001`);
    - the 0.2 start relation;
    - records the null save shows to be per-save noise.
  - Use a needle such as "114/Props record ... differs from the measured control".
  - Add a writer test that flips one non-written Props byte.
  - For the plan layer, either carry a Props digest on `PlanInput`, or state in
    `spec/writers/calculate.md` section 8 that this pin is writer-only and why.

### G1: the Mediums

Items 1.1 to 1.4 share one design: **one guard over every change to the displayed outline or
order while a Show Level or Sort is queued.**
- Land the guard in 1.1.  Each later item adds the paths and tests for its own id.
- The rule:
  - refuse the change with a message that names the queued edit;
  - after a withdrawal (All Subtasks, a replaced or refused level, a cleared or replaced
    Sort), call `clear_redo()`;
  - re-apply the queued level or sort on every sheet rebuild;
  - check the 1.5 "plan carries unsaved edits" rule at queue time AND on every later grid edit.
- Test each path in `qt_outline_persist` or `qt_sort_persist`, with needles that only the new
  messages carry.

- **W35c-1.1 (R35bG3-1):** the shared guard.  Its first path is the project-summary toggle.
- **W35c-1.2 (R35bG4-1):**
  - Redo after Undo of a queued Show Level.  Redo must go through `push()`, or redo must be
    cleared.
  - Redo after All Subtasks or a refused replacement level.
- **W35c-1.3 (R35bW-1):** a grid rename, Duration or Summary edit after a Sort or Show Level is
  queued.  Also: undo in the sheet, then Sort, then Ctrl+Y.
- **W35c-1.4 (R35bW-2):** a replaced or cleared queued Sort is withdrawn without `clear_redo()`.
- **W35c-1.5 (R35bG0-3):**
  - The critical-path guard cannot tell one Duration edit from two.
  - Record the limit in `calculate.md` section 8.
  - Find a before-state witness if one exists, and refuse a two-edit critical-path move.
- **W35c-1.6 (R35bG0-4):** test the new plan-layer fields through the reader.  Removing a
  `reader_adapter` line must turn `app_edits_calculate` red.
- **W35c-1.7 (R35bG0-5):** bring the plan layer up to the writer for the 0.3 and 0.5 shapes the
  review lists.
- **W35c-1.8 (R35bG1-1):** `sorted_order_of` reads `sorted[index]` after `sorted.clear()`.
- **W35c-1.9 (R35bG1-2):** the 1.6 resave guard reads only the target resource's FixedMeta byte 12.
- **W35c-1.10 (R35bG1-3):** give 1.1, 1.3 and 1.6 a guard in each layer, not one shared guard.
- **W35c-1.11 (R35bG2-1):** 2.7's tie and swapped-order refusals in the plan layer too.  This
  also closes R35bG3-2; record it there.

### G2: the Lows

One commit per id, in this order:
- **Calculate:** R35bG0-6, R35bG0-7, R35bG0-8, R35bG2-2, R35bG2-5, R35bG2-6, R35bG2-7,
  R35bG4-2, R35bG4-3, R35bG4-4.
- **R35bG4-6 (coordinator):** add a test that builds an `app::TaskRow{}` from its defaults and
  asserts the "was not read from the file" refusal.  Mutation G7c (default flipped to `true`)
  must turn it red.
- **View state and shell:** R35bG1-4, R35bG1-5, R35bG1-6, R35bG1-9, R35bG3-3, R35bG3-5 (record
  the second-session Bold pair as a measuring-wave item; no new pair here), R35bG4-5.
- **Records:**
  - R35bG1-7, R35bG1-8, R35bG2-3, R35bG2-8, R35bG3-4.
  - R35bW-4: make `plan-waves27-30.md`, START-HERE and `memory/coordinator.md` list every open
    id from `coord-gates-close.md`.
  - R35bW-5: correct the wave 35b CLOSE id table.
  - R35bG2-4 and R35bW-6: record only.  No history rewrite; say so in the record.
- **Not this wave's:** R35bW-3, answered by the coordinator's map.  R35bG2-9 is watch-only,
  and green on macOS at `e95c6ca`.

### CLOSE

- Rerun every gate on a fresh tree.
- Write the completion record with each id ticked: fixed (commit), partly fixed (what
  remains), or deferred (evidence).
- Mark `WAVE-35c-COMPLETE`.
- Push nothing.

## 3. The coordinator's part

After each group marker:
- fresh-tree gates;
- mutations at each new refusal's call site and helper;
- an independent review.

After CLOSE:
- push, and dispatch both workflows (0.1 is gated there);
- a whole-wave review.

**Merge waves 34, 34b, 35, 35b and 35c into `main` only when:**
- CI is green on all three OSs;
- no Critical or High is open;
- the 4(e) reopen check passes for every enabled save.  It passed at `e95c6ca`; rerun it if a
  writer changes.

**The trace VM.**  The `msp-trace` trace notes (`spec/writers/evidence/trace-*.md`), if
committed by then, are evidence the builder may cite.  The builder never runs the trace or COM.
