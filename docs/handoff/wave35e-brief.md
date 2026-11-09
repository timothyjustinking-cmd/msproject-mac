# Wave 35e brief: fix-only, covering wave 35d's open High, Mediums and Lows (for the Sonnet builder)

_Written Tue 10 Nov 2026 by the Opus coordinator from the wave 35d verification._

**Baseline.**  Branch `wave35e`, cut from `wave35d` at `2ea87e5`.

**Sources**, all in `docs/handoff/wave35d-verification/`:
- `review-g0.md`, `review-g1.md` and `review-g2.md` (R35dG0-n, R35dG1-n, R35dG2-n);
- `review-wave.md` (R35dW-n);
- `coord-gates-g0.md` to `coord-gates-g2.md` and `coord-gates-close.md`, which holds the open
  queue.

Every finding below has a **Fix:** paragraph in its review.  Read it before the item; this brief
does not repeat it.  `docs/handoff/standing-rules.md` binds this wave.

**This wave adds no writer, lifts no refusal and measures no new Project pair.**  Every change
is one of:
- a refusal;
- a test or a test fix;
- a shell wiring fix;
- a record.

## 0. Publication

The coordinator pushes `wave35e` and dispatches both CI workflows AFTER CLOSE.  No group, and
not CLOSE, pushes, dispatches a workflow or rewrites history.  Never force-push, and never work
round the pre-push guard.

## 1. Rules for this wave

The wave 35d brief's section 1 rules apply unchanged, with `35e` for `35d`.  In short:
- the marker `WAVE-35e-<group>-COMPLETE` (CLOSE: `WAVE-35e-COMPLETE`) in
  `docs/handoff/wave35e-complete.md`, with each group's gate line, "Guards seen red", "Deferred"
  and "Found, not fixed";
- one finding id per commit;
- gates per commit under `build/w35e/`;
- red then green, both quoted;
- specific needles;
- the trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35e)` on EVERY commit;
- no names;
- the three scans before every commit;
- LF line endings;
- no COM;
- the builder writes only `gates-gN.md` in `docs/handoff/wave35e-verification/`;
- the warning classes MSVC does not report, `-Wmissing-field-initializers` included;
- a platform-hardened test keeps its idiom.

Two rules are tightened this wave:
- **"Fixed" means fully closed, and CI ids are never "fixed" before CI.**  A CI-gated id is
  "fixed in source; CI is its gate" until the coordinator's CI run is green.  Waves 35c and 35d
  both broke this rule (R35cW-2, R35dW-3, R35dG1-7).
- **The open-queue lists name the coordinator's findings.**  START-HERE,
  `memory/coordinator.md` and `plan-waves27-30.md` are updated from
  `wave35d-verification/coord-gates-close.md`.  They never read "no High open" while that file
  lists one.

## 2. Groups

### G0: the High

- **W35e-0.1 (R35dG0-1, High, with R35dW-1):** a partly applied Task Information or Task Form edit
  keeps the structural redo list.
  - The route: a Sort, Show Level or Calculate Project; Ctrl+Z; a Task Information edit where one
    field commits and a later field fails; then Ctrl+Y.  The withdrawn edit is re-queued without
    `push()`.
  - **Fix:** in `apply_task_information`, whenever any field committed with `outcome.changed`,
    clear the structural redo list and run `after_cell_change()`, whatever `ok` is.
  - Test the route in `qt_sort_persist`, `qt_outline_persist` AND `qt_calculate_gate`.  Each test
    asserts that Redo is disabled after the partial apply and nothing is re-queued.
  - Sweep every other shell path that commits a cell and can then fail (the form, any batch
    apply) for the same pattern.  Fix or record each one.

### G1: the Mediums

- **W35e-1.1 (R35dG1-1):** close the two bypasses of the 1.2 cross-kind rule.
  - (a) A session Sort applied first, then a Level queued: `apply_outline_level` must refuse, or
    record why not.
  - (b) A queued Level, Ctrl+Z, a Sort, then Ctrl+Y: a sort choice must also end a Show Level on
    the redo list.
  - Test both.
- **W35e-1.2 (R35dG1-2):** correct `calculate.md`'s Scope paragraph.
  - The Notes and Text1 removals move the Task freed counter `0x01000002`.
  - List the percent-complete and indent/outdent saves among the refused port saves.
  - Take in R35dW-4: the pin is now the last check, so the paragraph cannot say every other
    project is refused "at the pin".
- **W35e-1.3 (R35dG1-3, R35dW-2, R35dW-3):** the records.
  - START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` drop R35bG0-1 and R35bG0-2 from
    their open lists, and list the open queue in `wave35d-verification/coord-gates-close.md`,
    R35dG0-1 first until 0.1 closes it.
  - Correct `wave35c-complete.md` and `wave35d-complete.md`: R35cG1-2, R35cG1-4, R35cW-1,
    R35cG0-3 and R35cW-4 become "partly fixed: <the R35d id>".
  - R35cW-CI1 and R35cW-CI2 become "fixed in source; CI is its gate".
  - Correct the "Show Level route non-divergent" sentence, which R35dG1-1(b) contradicts.
- **W35e-1.4 (R35dG0-2):** 0.5's record says "partly fixed: Task Information partial apply";
  update it once 0.1 closes it.

### G2: the Lows

One commit per id, in this order:
- **Shell:** R35dG0-3, R35dG0-4, R35dG1-5, R35dG1-6, R35dG2-1, R35dG2-2.
- **AppKit:** R35dW-5.  `app/main.mm` cannot be compiled here.  Make the change only if it is a
  refusal that mirrors the Qt shell's; otherwise record it.  CI is its gate.
- **Calculate:** R35dG0-5, R35dG1-4, R35dG2-3, R35dG2-4.
- **Records:** R35dG2-5, R35dW-6.

### CLOSE

- Rerun every gate on a fresh tree.
- Write the completion record with each id ticked: fixed (commit), fixed in source with CI as
  its gate, partly fixed (what remains), or deferred (evidence).
- Mark `WAVE-35e-COMPLETE`.
- Push nothing.

## 3. The coordinator's part

After each group marker:
- fresh-tree gates;
- mutations at each new guard;
- an independent review.

After CLOSE:
- push, and dispatch both workflows;
- a whole-wave review;
- 4(e), if a writer changes.

**Merge waves 34 to 35e into `main` only when:**
- CI is green on all three OSs;
- no Critical or High is open;
- 4(e) passes.
