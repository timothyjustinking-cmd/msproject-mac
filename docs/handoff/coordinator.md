# Coordinator handoff — current (wave 22 closed)

_The full coordinator history (waves 7–20) is archived at
docs/archive/coordinator-handoff-through-wave20.md.  This file keeps the last two
closed waves (20 and 21); the full chain (7–20) is in the archive.  Entry point for a
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


---

# Wave 21 final (2026-10-23 ~16:45) — HEAD `0f5920a`

The wave gate is `docs/handoff/wave21-complete.md`; the entry point for the next
session is `docs/START-HERE.md`.  One line: **the file's own stored table landed**
(decision 7, with gaps), the operator's four review fixes are in, `mppwrite`
tolerates CFB slack, the Linux build is parallel, and CI run 39904570421 is green
on all three with the three packages.

## Numbers

* ctest `-E plan_recalc` **50/50**; M2 **332/332**; plan_recalc_tests **266**;
  engine self-test **265**; mppwrite CFB **1693**; reader **984**; cal **785**.
* Engine **7/8** — `constraint-mso`'s save-time word is measured and pinned but
  deliberately not scored (the rule lives behind a self-test opt-in; production
  still writes 9600).
* app_qt **14/14** locally, **13/13** in CI; UI parity **9/9 on Windows, macOS
  and Linux**.
* Packages: Windows 48.1 MB zip, macOS 21.5 MB zip, Linux 32.8 MB AppImage
  (offscreen smoke 900x560).  Linux Build 2m55s → **1m42s** with `--parallel 2`.

## Lanes

* **Q6**: the stored `214/CTable` column set/order/titles/widths (model and
  display); editable Name/Duration/Start/Finish, the rest read-only; menu bar
  hidden; backstage back arrow; double-click → six-tab Task Information;
  selection `#A2D3A0`, critical `#FF0000`; header captions never clip; status
  left Ready.
* **E6**: `CfbDocument::parse` warns on a short FAT; `--dump sf248` exits 0 with
  the 191/768/959 warning; slack documents are readable but not re-emittable.
* **C6**: `--parallel 2` on the Linux Build only; the validator proves every
  other step/artifact is unchanged.

## Honest shortfall

The 16-decision contract is **not** complete: decision 1 is **PARTIAL** (our
ribbon tab group/button inventories are not Project's; the clean strips in the
reference set are the target), 7/10/12/14/15 are partial, 16 (View ▸
Timeline/Details) is **not done**.  The wave delivered the stored table and the
operator fixes, not the whole UI contract.

## Open, ranked

1. Ribbon tab inventories from `project-ribbon-*.png` (decision 1) — wave 22's
   first job.
2. Stored-column values for synthetic fields (Pre/Suc/Resource Names/SUMMARY/
   Yes) and the file's bar styles; the loaded file's new-task mode needs a reader
   field.
3. Dialogs 14/15 and the split view 16; then dark surfaces (10) and a per-screen
   pixel diff (12).
4. Non-Start edits in a save; the clipboard display-order limitation; the
   render-selection workaround.
5. macOS launch smoke + notarization; sf248 task recovery.

## Traps added

* Running `plan_recalc_tests` with the corpus rewrites
  `tests/data/project_a_recalc_golden.tsv` (it happened once; the content was
  identical, `git status` clean) — use `MPP_NO_CORPUS=1`.
* P1's rule scan reads the green selection band as a grid rule, so `--render`
  clears the selection; the interactive window keeps the measured green.
* The file's green/navy bars are bar styles, not defaults; only selection and
  critical red were adopted.
* The Linux job's wall clock is dominated by apt when the runner is slow — use
  the Build-step duration (2m55s → 1m42s), not the job total.


---

# Wave 22 final (2026-10-23 ~17:50) — HEAD `abeafd2`

The wave gate is `docs/handoff/wave22-complete.md`; the entry point is
`docs/START-HERE.md`.  One line: **the measured seven-tab ribbon, the stored
columns' values and the project's new-task mode landed**, and CI run 39912313201
is green on all three with the three packages.

## Numbers

* ctest `-E plan_recalc` **50/50**; M2 **332/332**; plan_recalc_tests **266**;
  engine self-test **265**; writer **1693**; reader **1016** (+32); cal **785**.
* Engine **7/8** (the MSO save-time word stays measured/pinned, not scored).
* app_qt **15/15** locally, **14/14** in CI; UI parity **9/9 on all three**.
* Packages: Windows 48.2 MB zip, macOS 21.6 MB zip, Linux 32.8 MB AppImage
  (smoke-rendered 900x560).  Linux Build **2m10s** parallel on this tree.

## Lanes

* **R6**: every tab carries Project's measured group/button inventory and order
  (Task 8/30, Resource 5/13, Report 3/10, Project 6/14, View 7/26, Help 1/3,
  Gantt Format 6/17); two capture corrections; `qt_ribbon_inventory` pins it;
  backlog 113 ribbon + 43 menu rows.
* **S6**: stored Predecessors/Successors/Resource Names/% Complete values render
  (link syntax from the capture); SUMMARY (Flag10) and Resource Names (null
  assignment word) stay blank with byte reasons.
* **E7**: props record `0x024013c8` decoded (0 = Auto, 1 = Manual), exposed on
  `ProjectRow`, wired into the status centre; 133 files scanned, 10/10 MSPDI
  exports agree; no writer yet.

## Honest shortfall

Decision 1 is **DONE** but introduced reachability caveats (About/Keyboard
Shortcuts left Help and the menu bar is hidden, so **About is mouse-unreachable**;
Save-as-PDF/Export-PDF, zoom, shading and three view buttons left their ribbon
groups).  Decision 7 is still **PARTIAL** (SUMMARY Flag10 undecoded, bar styles
not decoded).  **16** still not done; 10/12/14/15 partial.

## Open, ranked

1. Restore reachability through the backstage (About, shortcuts, PDF export,
   zoom/shading/view commands), and decide the Font box's Arial-8-vs-Carlito
   value.
2. Decode Flag10 in `core/mpp` (SUMMARY/Yes columns); add a link+assignment
   fixture through the coordinator oracle so CI shows non-empty Predecessors.
3. Dialogs 14/15 and the split view 16; then dark surfaces 10 and per-screen
   pixel diff 12.
4. Bar-style decoding, clipboard display order, render selection.
5. New-task writer (needs a Project acceptance fixture); macOS smoke/notarization;
   sf248 task recovery.

## Traps added

* Pixel beats prose: the brief's starting inventory was wrong twice; the strips
  won.  Re-read the capture.
* `Task.Flag10 0x0b400051` is the file's SUMMARY, not `Task.Summary`; a blank
  column is better than a wrong one.
* A file's table can be wider than the 1920 pane: project_a clips Pre/Suc at
  1920 (the 2680 render shows them) - not a regression.
* CI has no corpus, so app_qt is 14 there and 15 here.
