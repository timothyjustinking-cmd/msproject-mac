# Wave 35g brief: fix-only, covering wave 35f's open Mediums and Lows (for the Sonnet builder)

_Written Wed 11 Nov 2026 by the Opus coordinator from the wave 35f verification._

**Baseline.**  Branch `wave35g`, cut from `main` at `5863054`.  `main` holds waves 34 to 35f,
merged on 10 November 2026, plus trace evidence and tools under `spec/writers/evidence/` and
`tools/trace/`.

**Sources**, all in `docs/handoff/wave35f-verification/`:
- `review-g0.md` and `review-g1.md` (R35fG0-n, R35fG1-n);
- `review-wave.md` (R35fW-n);
- `coord-gates-close.md`, which holds the open queue ("Open after the merge").

Every finding below has a **Fix:** paragraph in its review.  Read it before the item; this brief
does not repeat it.  `docs/handoff/standing-rules.md` binds this wave.

**No Critical or High is open.**  This wave adds no writer, lifts no refusal that guards a
save, and measures no new Project pair.  Every change is one of:
- a refusal, or a narrower refusal;
- a test or a test fix;
- a shell wiring fix;
- a record.

**The trace has priority on this machine.**  A separate agent is analysing Project traces in the
`msp-trace` VM.  The VM holds most of this machine's spare memory.
- Never start, stop or touch the VM, `vmconnect`, or anything under `build/trace-2021/`.
- Never touch `tools/trace/` or `spec/writers/evidence/`.
- Never run COM or Project.
- Keep builds to the gates the rules require; do not run extra full rebuilds.
- If a build fails with an out-of-memory error (C1060, C1076, C3859, "heap space" or similar), wait
  and rerun that one gate.  Record the error and the rerun in the gate log.  It is not a code fault.

## 0. Publication

The coordinator pushes `wave35g` and dispatches both CI workflows AFTER CLOSE.  No group, and
not CLOSE, pushes, dispatches a workflow or rewrites history.  Never force-push, and never work
round the pre-push guard.

## 1. Rules for this wave

The wave 35f brief's section 1 rules apply unchanged, with `35g` for `35f`.  In short:
- the marker `WAVE-35g-<group>-COMPLETE` (CLOSE: `WAVE-35g-COMPLETE`) in
  `docs/handoff/wave35g-complete.md`, with each group's gate line, "Guards seen red", "Deferred"
  and "Found, not fixed";
- one finding id per commit;
- gates per commit under `build/w35g/`;
- red then green, both quoted;
- specific needles;
- the trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35g)` on EVERY commit;
- no names;
- the three scans before every commit;
- LF line endings;
- no COM;
- the builder writes only `gates-gN.md` in `docs/handoff/wave35g-verification/`;
- the warning classes MSVC does not report;
- a platform-hardened test keeps its idiom;
- "fixed" means fully closed, and a CI-gated id is "fixed in source; CI is its gate" until the
  coordinator's CI run;
- the open-queue lists name the coordinator's findings, and never call an id closed before the
  coordinator's review says so.

**`app/main.mm`'s CI gate is `macos-appkit`** (`app-macos.yml`), not `macos-qt`.

## 2. Groups

### G0: the Mediums

- **W35g-0.1 (R35fG1-1):** Project > Custom Fields... must open beside a queued exclusive edit;
  only an accepted Formula... is refused.  Move the `refuse_document_edit` call out of the
  pre-open path, as the review's Fix paragraph says: into the `set_formula_editor` callback, or
  after `exec()` when `has_formula_edit()`.
  - Test that the dialog opens and cancels cleanly beside a queued Sort, and beside one other
    exclusive kind.
  - Test that an accepted formula edit beside a queued exclusive edit is still refused, with the
    queue unchanged.
  - Keep W35f-0.2's queued-Calculate guard, and its `qt_calculate_gate` case, green.
- **W35g-0.2 (R35fG0-1):** correct the 1.1 record's sentence about bypass (b) on the five-task file.
  - Wherever it appears: `wave35e-verification/gates-g1.md`, `wave35e-complete.md`, and any record
    that copied it.
  - Follow the review's Fix paragraph.  If the review says a test is needed to settle the claim,
    add it.

### G1: the Lows

One commit per id, in this order:
- **Tests and shell:**
  - R35fG0-5 (0.1's read-only case);
  - R35fG0-6 (0.3 asserts the Level is shown);
  - R35fG0-7 (0.2's status needle);
  - R35fG0-4 (a session-only Level after the sort is cleared);
  - R35fG1-2 (the Sort and Show Level wording);
  - R35fG1-4 and R35fW-3 (the 1.3 partial-apply status; one commit each);
  - R35fG1-3 (1.8's untested parts: tests where the review asks for them, else the corrected
    coverage limit).
- **Records:** R35fG0-2, R35fG0-3, R35fG0-8, R35fW-1, R35fW-2.
- **Hygiene:** R35fG0-9, R35fG1-5.

### CLOSE

- Rerun every gate on a fresh tree.
- Write the completion record with each id ticked: fixed (commit), fixed in source with CI as
  its gate, partly fixed (what remains), or deferred (evidence).
- Update START-HERE, `memory/coordinator.md` and the plan's open queue from this wave's record.
- Mark `WAVE-35g-COMPLETE`.
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

**Merge `wave35g` into `main` only when:**
- CI is green on all three OSs;
- no Critical or High is open;
- 4(e) passes.
