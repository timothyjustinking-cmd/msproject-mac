# Wave 35f brief: fix-only, covering wave 35e's open Mediums and Lows (for the Sonnet builder)

_Written Tue 10 Nov 2026 by the Opus coordinator from the wave 35e verification._

**Baseline.**  Branch `wave35f`, cut from `main` at `47665e1`.  `main` holds waves 34 to 35e,
merged on 10 November 2026.

**Sources**, all in `docs/handoff/wave35e-verification/`:
- `review-g0.md`, `review-g1.md` and `review-g2.md` (R35eG0-n, R35eG1-n, R35eG2-n);
- `review-wave.md` (R35eW-n);
- `coord-gates-close.md`, which holds the open queue.

Every finding below has a **Fix:** paragraph in its review.  Read it before the item; this brief
does not repeat it.  `docs/handoff/standing-rules.md` binds this wave.

**No Critical or High is open.**  This wave adds no writer, lifts no refusal that guards a
save, and measures no new Project pair.  Every change is one of:
- a refusal, or a narrower refusal;
- a test or a test fix;
- a shell wiring fix;
- a record.

**The trace has priority on this machine.**  A separate agent is running Project traces in the
`msp-trace` VM until about 15 November 2026.
- Never start, stop or touch the VM, `vmconnect`, or anything under `build/trace-2021/`.
- Never run COM or Project.
- Keep builds to the gates the rules require; do not run extra full rebuilds.

## 0. Publication

The coordinator pushes `wave35f` and dispatches both CI workflows AFTER CLOSE.  No group, and
not CLOSE, pushes, dispatches a workflow or rewrites history.  Never force-push, and never work
round the pre-push guard.

## 1. Rules for this wave

The wave 35e brief's section 1 rules apply unchanged, with `35f` for `35e`.  In short:
- the marker `WAVE-35f-<group>-COMPLETE` (CLOSE: `WAVE-35f-COMPLETE`) in
  `docs/handoff/wave35f-complete.md`, with each group's gate line, "Guards seen red", "Deferred"
  and "Found, not fixed";
- one finding id per commit;
- gates per commit under `build/w35f/`;
- red then green, both quoted;
- specific needles;
- the trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35f)` on EVERY commit;
- no names;
- the three scans before every commit;
- LF line endings;
- no COM;
- the builder writes only `gates-gN.md` in `docs/handoff/wave35f-verification/`;
- the warning classes MSVC does not report;
- a platform-hardened test keeps its idiom;
- "fixed" means fully closed, and a CI-gated id is "fixed in source; CI is its gate" until the
  coordinator's CI run;
- the open-queue lists name the coordinator's findings, and never call an id closed before the
  coordinator's review says so (R35eG1-5).

**`app/main.mm`'s CI gate is `macos-appkit`** (`app-macos.yml`), not `macos-qt` (R35eG2-5).

## 2. Groups

### G0: the Mediums

- **W35f-0.1 (R35eG1-1):** move 1.1(a)'s session-Sort refusal in `apply_outline_level` onto the
  queue branch only.
  - A Level that would only ever be session-only is shown for the session again, as in wave 31.
    That covers a read-only file, Level 3 to 9, and a file the builder refuses.
  - Test both: a refused queue under a session Sort, and an accepted session-only Level under a
    session Sort.
- **W35f-0.2 (R35eG2-1):** Project > Custom Fields..., Formula...: the same queued-Calculate
  guard as the other formula surfaces, before `apply_custom_formula`.  Add a `custom_fields`
  case to `qt_calculate_gate`.
- **W35f-0.3 (R35eG1-3):** correct the 1.1 record in `wave35e-verification/gates-g1.md` and
  `wave35e-complete.md`.
  - Add the five-task Undo-Sort-then-Level test the review describes.
  - Withdraw the wrong "Found, not fixed" item.
- **W35f-0.4 (R35eG1-2, with R35eG2-9 and R35eG1-5):** the records.
  - START-HERE, `memory/coordinator.md` and `plan-waves27-30.md` list the open queue from
    `wave35e-verification/coord-gates-close.md`.  R35dG0-1 is closed.  The four partly fixed
    35b ids (R35bW-1, R35bW-2, R35bG2-2, R35bG3-1) appear with their remainders.
  - R35cW-3 gets one status, and R35cW-4's remainder names R35dW-5.

### G1: the Lows

One commit per id, in this order:
- **Shell:** R35eG0-1, R35eG0-2, R35eG0-3, R35eG1-4, R35eG2-2, R35eG2-4, R35eG2-6, R35eW-1,
  R35eW-3.
- **AppKit:** R35eG2-3.  Make the change only if it mirrors the Qt shell's refusal; otherwise
  record it.  `macos-appkit` is its gate.
- **Calculate seams:** R35eG2-7, R35eG2-8.
- **Records:** R35eG1-6, R35eG2-5 with R35eW-4, R35eW-2, R35eG2-10.

### CLOSE

- Rerun every gate on a fresh tree.
- Write the completion record with each id ticked: fixed (commit), fixed in source with CI as
  its gate, partly fixed (what remains), or deferred (evidence).
- Mark `WAVE-35f-COMPLETE`.
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

**Merge `wave35f` into `main` only when:**
- CI is green on all three OSs;
- no Critical or High is open;
- 4(e) passes.
