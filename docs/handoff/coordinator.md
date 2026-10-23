# Coordinator handoff — current (wave 21 in progress)

_The full coordinator history (waves 7–20) is archived at
docs/archive/coordinator-handoff-through-wave20.md.  This file keeps the last
closed wave (20) and takes the wave-21 section at its close.  Entry point for a
new session: docs/START-HERE.md._

# Wave 20 final (2026-10-23 ~15:25) — HEAD `6a506cd`

The wave gate is `docs/handoff/wave20-complete.md`; read that first.  One-line
state: **build-order steps 1–2 of the UI spec landed** (chrome, backstage,
themes, bundled Carlito, five structural gates), engine **7/8**, and **three
runnable packages** from one green CI run.

## Numbers (measured, not carried over)

* `ctest -E plan_recalc` **50/50**; M2 **332/332**; `plan_recalc_tests` **266**;
  engine self-test **236**; reader **984**; cal **785**.
* Engine parity **7/8** — only `constraint-mso`'s duration word.
* UI parity **9 comparable / 0 failing on Windows, macOS AND Linux** (CI run
  39898703443); splitter 0.45924, columns −1…+1, three colours exact.
* app_qt **11/11 on all three**; packages: Windows zip 48.1 MB, macOS .app
  21.5 MB, Linux AppImage 32.8 MB (type 2, offscreen smoke `rendered 900x560`).

## What this wave changed in the plan

* **Q5 was stopped mid-work on a branch and did not compile.**  It is finished,
  not reset: the three CI compile stops, the caption placement (parity 0.398 →
  0.459) and **bundled Carlito** are in `6a506cd`.  The first branch run failed
  all three jobs; the second failed only on the absence of fonts; the third is
  green.  Three dispatches, each triaged.
* **Decision 13 moved from "not done" to done** during the red triage: Qt 6
  ships no fonts, so the offscreen gates were measuring nothing.  Carlito is now
  four OFL faces compiled into every executable.
* **Honest shortfall**: decisions **7** (stored-table rendering), **14** (dialog
  inventory), **16** (Timeline/Details split) are NOT done; **10** and **12** are
  partial.  The wave delivered steps 1–2, not the whole 16-decision contract.
  `wave20-complete.md` has the row-by-row scoreboard — do not describe the UI
  contract as complete.

## Open, ranked (detail in the gate file)

1. Build-order step 3: `214/CTable`-driven columns.
2. Decision 14/15 dialog inventory (+ dialog rows in the generated backlog).
3. Decision 16 split view.
4. Dark palette for custom-painted surfaces (operator owes the capture).
5. `constraint-mso`'s one field; **mppwrite CFB slack** for `--dump sf248`
   (M6's out-of-scope finding, still open); clipboard display-order range.
6. macOS launch smoke + notarization (procurement).

## Traps added

* Qt 6 offscreen runners have **no fonts**; gates that measure text must bundle
  one.  Resources in a static library can be dropped — add them per executable.
* P1 reads grey strips below x=60 as weekend/columns; the view caption must be
  white, borderless and inside the sheet band.
* Sandbox: `cmake --build --parallel`/MSBuild `/m` and piped `cmake --build`
  fail; build serially and redirect to a file.
* Python text writes default to CRLF; `gh run download` refuses existing files.
