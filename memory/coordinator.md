# memory — coordinator (2026-10-23, wave 21 closed)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave21-complete.md`.  Full pre-wave-21 history:
`memory/archive/coordinator-through-wave20-full.md`.

## Where main is

`main` = **0f5920a**, pushed and clean (the docs close is the next commit).
Wave 21: Q6 (`0f5920a`), E6 (`d51ab41`), C6 (`e691794`) all committed; CI run
**39904570421** green on Windows, macOS and Linux (13/13 each), three packages.

## The numbers

* ctest `-E plan_recalc` **50/50**; M2 **332/332**; plan_recalc_tests **266**
  (no corpus); engine self-test **265**; mppwrite CFB **1693**; reader **984**;
  cal **785**; fixtures OK.
* Engine **7/8** — `constraint-mso` is measured/pinned but not scored; the
  production save still writes 9600.
* app_qt **14/14** local / **13/13** CI; UI parity **9/9 on all three**.
* Packages: Windows 48.1 MB, macOS 21.5 MB, Linux 32.8 MB AppImage; Linux Build
  2m55s → **1m42s** with `--parallel 2`.

## Wave 21 result (short)

Decision 7 substantially landed: the loaded sheet uses the file's `214/CTable`
columns/order/titles/widths; Name/Duration/Start/Finish editable, everything else
read-only.  Operator fixes: menu bar hidden, backstage back arrow, double-click →
six-tab Task Information, selection `#A2D3A0`, critical `#FF0000`, header
captions, status Ready.  CFB slack fixed in `mppwrite`; Linux CI parallel.

**Not done:** decision 1 (ribbon inventories; Resource 3/5 groups, View 3/7),
stored-column values for synthetic fields, bar-style decoding, the loaded file's
new-task mode, dialogs 14/15, split view 16, dark custom surfaces (10), per-screen
pixel diff (12).

## Open, ranked

1. Ribbon inventories from the clean `project-ribbon-*.png` strips (wave 22 first).
2. Stored-column values + bar styles + the new-task mode reader field.
3. Dialogs 14/15, split view 16, dark surfaces 10, pixel diff 12.
4. Non-Start edits in a save; clipboard display order; render selection.
5. macOS smoke/notarization; sf248 task recovery.

## Traps

* Qt 6 offscreen has no fonts; Carlito is bundled per executable.
* P1 reads the green selection band as a grid rule; `--render` clears it.
* Green/navy bars are file bar styles, not defaults.
* Never run `plan_recalc_tests` with the corpus (rewrites the golden).
* Serial builds; `newline="\n"`; `gh run download` fresh dir.

## Next three actions

1. Wave 22: rebuild the seven ribbon tab inventories from the clean strips.
2. Stored-column values/bar styles + the new-task mode.
3. The dialog inventory, split view and dark surfaces.
