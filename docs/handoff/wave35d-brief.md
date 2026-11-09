# Wave 35d brief: fix-only, covering wave 35c's CI failures, Highs, Mediums and Lows (for the Sonnet builder)

_Written Tue 10 Nov 2026 by the Opus coordinator from the wave 35c verification._

**Baseline.**  Branch `wave35d`, cut from `wave35c` at `d3b68fa`.

**Sources**, all in `docs/handoff/wave35c-verification/`:
- `review-g0.md`, `review-g1.md` and `review-g2.md` (R35cG0-n, R35cG1-n, R35cG2-n);
- `review-wave.md` (R35cW-n);
- `coord-gates-g0.md` to `coord-gates-g2.md` and `coord-gates-close.md`.  The last holds
  R35cW-CI1, R35cW-CI2 and the open queue.

Every review finding below has a **Fix:** paragraph in its review.  Read it before the item;
this brief does not repeat it.  `docs/handoff/standing-rules.md` binds this wave.

**This wave adds no writer, lifts no refusal and measures no new Project pair.**  Every change
is one of:
- a refusal;
- a test or a test fix;
- a shell wiring fix;
- a record.

## 0. Publication

The coordinator pushes `wave35d` and dispatches both CI workflows AFTER CLOSE.  No group, and
not CLOSE, pushes, dispatches a workflow or rewrites history.  Never force-push, and never work
round the pre-push guard.

## 1. Rules for this wave

The wave 35c brief's section 1 rules apply unchanged, with `35d` for `35c`.  In short:
- groups in order;
- the marker `WAVE-35d-<group>-COMPLETE` (CLOSE: `WAVE-35d-COMPLETE`) in
  `docs/handoff/wave35d-complete.md`, committed;
- each group's row there carries its gate line, "Guards seen red", "Deferred" and "Found, not
  fixed", then the marker;
- one finding id per commit;
- gates per commit under `build/w35d/`;
- deferral only with evidence of an attempt;
- red then green, both quoted;
- specific needles: never the command prefix, and never "not implemented in this port" alone;
- both layers: the edit-model builder and `push()`, and the writer;
- the measured shape still writes and matches its pair with `effects` on;
- the trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35d)` on EVERY commit,
  checked by script before each commit;
- no names;
- the three scans before every commit;
- LF line endings;
- **no COM**;
- the builder writes only `gates-gN.md` in `docs/handoff/wave35d-verification/`.  The
  coordinator's files there are `coord-gates-*.md` and `review-*.md`;
- **"fixed" means fully closed.**  Anything less is "partly fixed: <what remains>".  Wave 35c's
  CLOSE broke this rule four times (R35cW-2).

Two rules are new this wave:
- **The warning classes MSVC does not report.**  Read for them in everything you touch:
  `-Wswitch`, unused results, sign compares, `-Wdangling-reference`, and
  **`-Wmissing-field-initializers`**.  The last is R35cW-CI1: a designated initializer that
  names only some members is an error on GCC with `-Werror`.
- **A test whose comment records a platform failure keeps that behaviour.**  macOS cannot be
  run here, and CI is its only gate.  Wave 35c's 2.18 replaced the About box's macOS-hardened
  top-level walk with `gate::PromptAnswerer`, and macos-qt went red (R35cW-CI2).  Before
  changing any such test, quote its platform comment in the commit and keep the idiom it
  describes.

Run ctest by its real name, and treat "No tests were found" as a failure.  `app/main.mm` cannot
be compiled here.

## 2. Groups

### G0: CI and the three Highs

- **W35d-0.1 (R35cW-CI1, blocks merge): ubuntu-gcc stops at `-Werror=missing-field-initializers`.**
  - The line is `app/app_edits_assignment_tests.cpp:638`:
    `row = app::ResourceRow{.uid = row.uid, .id = row.id};`.
  - **Fix:** build the row from its defaults, then assign `uid` and `id`.  Then sweep `app/`,
    `app_qt/` and `tests/` for other designated initializers that name only some members of a
    struct with default member initializers.
  - **Done when:** the coordinator's CI run shows ubuntu-gcc green.  Record it as "CI is its
    gate".
- **W35d-0.2 (R35cW-CI2, blocks merge): macos-qt fails `qt_backstage_gate`'s About box.**
  - The checks are "the About button opens a modal About box" and "the About box is the port's
    own".  They regressed in `c235973` (wave 35c item 2.18).
  - **Fix:** restore the About case's top-level widget walk and its text check, as before
    `c235973`, with its comment.  The other two dialogs 2.18 moved to the answerer may stay;
    they were green on macOS.
  - **Done when:** the coordinator's CI run shows macos-qt 50/50.
- **W35d-0.3 (R35cG0-1, High): three `check_props_pin` branches are untested.**
  - The branches are the header words, more than the 161 measured records, and fewer.
  - Add three writer tests, each with a needle only its branch produces.
  - The coordinator's mutation P3 (the record-count check switched off) must then turn
    `mppwrite_roundtrip_calculate` red.
- **W35d-0.4 (R35cG1-1, High): a withdrawn Sort comes back on Redo after an Undo.**
  - The route: Sort Name descending, Ctrl+Z, then a refused or cleared Sort, then Ctrl+Y.
  - The causes:
    - `apply_sort_choice` calls `clear_redo()` only when a Sort is queued at that moment;
    - the sheet rebuild re-applies a queued Show Level, but never a queued Sort.
  - **Fix both:**
    - any sort choice made while a Sort sits on the redo list clears it;
    - the rebuild re-applies a queued Sort, as it does a queued Show Level.
  - Check the same route for Show Level (`apply_outline_level`'s `clear_redo()` sits inside
    `if (had_queued_level)` too).  Fix it if it reproduces, and record it either way.
  - Test the Undo-first route in `qt_sort_persist` (and in `qt_outline_persist` if Show Level
    reproduces).
- **W35d-0.5 (R35cG1-2, High): Cut and Paste leave the structural redo list.**
  - `cut_selection` and `paste_selection` call `after_cell_change()` directly.
    `after_cell_commit` clears the structural redo list first.
  - **Fix:** clear it on every cell path that changes the plan.
  - Test the route "a queued Sort or Level, Ctrl+Z, a cut or pasted Name, Ctrl+Y" in both persist
    tests.  Assert that Redo is disabled and nothing is re-queued.

### G1: the Mediums

- **W35d-1.1 (R35cG1-3):** a test for Paste's queued-view guard, through the window's own Copy or
  the clipboard (`QGuiApplication::clipboard()->setText`).  The coordinator's mutation Q10 must
  then turn it red.
- **W35d-1.2 (R35cG1-4):** make the queued-view guard consistent.
  - Choose a rule and record it in `spec/writers/view-state.md`: either a session Level or Sort
    chosen while the other kind is queued is refused, or the Sort branch's outline refusals are
    lifted.
  - Test every refusal you keep (Expand All, Collapse All, the row toggle, the summary toggle
    under a queued Sort).
- **W35d-1.3 (R35cG2-1):** the `save_calculate` scratch chain needs a test again.
  - Add a test-only fault seam that throws after the first append.
  - Assert that the existing target is byte-identical afterwards.
  - Correct the test comment at `tests/roundtrip_calculate.cpp:1163`, and the two record rows
    that call R35bG2-2 closed.
- **W35d-1.4 (R35cG0-3):** the Props pin also pins per-file identity records.
  - Follow the review's Fix paragraph:
    - correct the refusal text, which blames "calculation options";
    - state in `calculate.md` that Calculate Project is enabled for the measured control only;
    - name the files the port itself saved that are now refused.
  - Lift no refusal.
- **W35d-1.5 (R35cW-2):** correct the wave 35c CLOSE id table.  R35bW-1, R35bW-2, R35bG2-2 and
  R35bG3-1 become "partly fixed", each naming its remaining R35c id.
- **W35d-1.6 (R35cW-1, with R35cG2-8):** make START-HERE, `memory/coordinator.md` and
  `plan-waves27-30.md` list every open id from `wave35c-verification/coord-gates-close.md`,
  Highs first.  Then remove the ids wave 35c closed from those files' open lists.

### G2: the Lows

One commit per id, in this order:
- **Calculate:** R35cG0-2 together with R35cW-3, records only: 2.2 closed the writer half, so
  say so and fix the stale test comment.  Then R35cG0-4, R35cG0-5, R35cG0-6, R35cG2-2,
  R35cG2-3, R35cG2-6, R35cW-5.
- **View state and shell:** R35cG2-4, R35cG2-5, R35cW-4.
- **Assignment removal:** R35cG1-6.
- **Records:** R35cG1-5, R35cG1-7 (or record why the second scan stays), R35cG2-7.

### CLOSE

- Rerun every gate on a fresh tree.
- Write the completion record with each id ticked: fixed (commit), partly fixed (what
  remains), or deferred (evidence).
- Mark `WAVE-35d-COMPLETE`.
- Push nothing.

## 3. The coordinator's part

After each group marker:
- fresh-tree gates;
- mutations at each new guard, including re-runs of P3 (G0) and Q10 (G1);
- an independent review.

After CLOSE:
- push, and dispatch both workflows (0.1 and 0.2 are gated there);
- a whole-wave review;
- 4(e), rerun if a writer changes.

**Merge waves 34, 34b, 35, 35b, 35c and 35d into `main` only when:**
- CI is green on all three OSs;
- no Critical or High is open;
- the 4(e) reopen check passes for every enabled save (it passed at `319d187`).
