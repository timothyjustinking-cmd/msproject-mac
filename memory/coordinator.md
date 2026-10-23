# memory — coordinator (2026-10-23, wave 22 closed)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave22-complete.md`.  History:
`memory/archive/coordinator-through-wave20-full.md`.

## Where main is

`main` = **abeafd2**, pushed (the docs close follows).  Wave 22 lanes R6
(`a60453b`), E7 (`f7f168b`), S6 (`abeafd2`) all committed.  CI run
**39912313201** green on Windows, macOS and Linux (14/14 each), three packages.

## Numbers

* ctest **50/50**; M2 **332/332**; plan 266; engine self-test 265; writer 1693;
  reader **1016**; cal **785**; app_qt **15/15** local / 14 CI; parity **9/9**.
* Engine **7/8** (MSO word measured/pinned, not scored).
* Packages: Windows 48.2 MB, macOS 21.6 MB, Linux 32.8 MB AppImage; Linux Build
  2m10s parallel.

## Wave 22 result

* Decision 1 **DONE**: seven tabs carry the measured inventory/order; gate
  `qt_ribbon_inventory`; backlog 113 ribbon + 43 menu rows.
* Stored-column values: Pre/Suc/Resource Names/% Complete from the plan; SUMMARY
  (Flag10) and Resource Names (null assignments) blank with evidence.
* New-task mode: props `0x024013c8` (0 Auto/1 Manual), reader + status bar.
* Reachability caveat: About/Keyboard Shortcuts left Help and the menu bar is
  hidden -> About mouse-unreachable; Save-as-PDF/Export-PDF, zoom, shading and
  three view buttons left their ribbon groups.

## Open, ranked

1. Restore reachability via the backstage; decide the Font box (Arial 8 vs Carlito).
2. Decode Flag10; a link/assignment fixture for CI.
3. Dialogs 14/15, split view 16, dark surfaces 10, pixel diff 12.
4. Bar styles, clipboard order, render selection, new-task writer.
5. macOS smoke/notarization; sf248.

## Traps

* Pixel beats prose; re-read the capture.
* Flag10 is the file's SUMMARY, not Task.Summary.
* A wide stored table clips at 1920 (project_a) — not a regression.
* CI has no corpus: app_qt 14 there, 15 here.
* Serial builds, newline="\n", gh download fresh dir, plan_recalc never with corpus.

## Next three actions

1. Wave 23: reachability (About/shortcuts/PDF/zoom) through the backstage.
2. Flag10 decode + a link/assignment fixture through the oracle.
3. Dialogs 14/15 and the split view 16.
