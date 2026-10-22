# Wave 14 complete — the successor's gate

**This file is the wait gate.** Its existence means the wave-14 coordinator
finished: the tree is quiet, every wave-14 stream is merged, and a successor or
auditor may work.

* **Final code commit:** `1684718` (`NSPrintingPaginationModeClip` fix, pushed).
  The docs commit that adds this file is its child.
* **CI:** `app-macos` green on `1684718`; check `gh run list` on the docs tip for
  the core `ci` run before quoting ids.
* **Full local suite:** `build\vs-w14` (clean MSVC `/W4 /WX`) — **48/48 ctest**
  (wave 13: 47; the new test is `viewstate_writer`). App `build\vs-w14-app` —
  **14/14 tests, 2,289 checks** (wave 13: 12 / 2,132; `app_print` 66,
  `app_start_warning` 41, menu 744 -> 794).
* **M2:** **332/332** (83/83 × 4, 0 unexplained); schedin 20/20 + 10/10.
* **Unit checks:** viewstate **212**, props **720** (was 520), field_values
  **802** (was 477), `mpp_field_value` **290**.
* **Project gate:** five writer fixtures reopen in Project with the expected
  values — `$123.45`; Cost1..10 = 1.11..11.10; 1.23 / 0.07; assignment Number1
  7.5 / Text1 "assign-text"; Number20 2.5 / Text30 "t30".
* **Counters:** 99 agent sessions + 10 coordinator sessions, ~$7.63, ~760M
  tokens, ~$0.077/session. Roadmap **~76%**.

## What wave 14 delivered

| # | Stream | Deliverable |
|---|---|---|
| W58 | `214/` view-state writer | four-part encoder (identity bytes preserved, header words and VarMeta offsets recomputed), date-format and 115-byte column-record mutators; no-op encode byte-identical on 72 extracted collections |
| W59 | `114/Props` formula writer | inverted block grammar; the emitted 168-byte two-formula record is **byte-identical to Project's**; maintains the custom-field-in-use record `0x02401392` |
| W60 | assignment + Cost fields | assignment Number1..20 / Text1..30 (keys `0x0f40403c`/`0x0f404064`), Cost1..10 (key = field id, f64 hundredths, presence bits); emitted streams byte-identical to Project's own pairs |
| W61 | app output/UX | `File > Print…` and `File > Save as PDF…` share one layout with Export PDF (wave-13 PDFs byte-identical); `warning_for_start_edit` + one save-time alert |
| — | oracle (coordinator) | five control/edited fixture pairs: column replace, formula record, assignment keys, Cost encoding, manual start move |

## The correction this wave paid for

**A table column cannot be appended with `TableEdit`.** The wave-13 plan assumed
a corrected `TableEdit(Create=True, OverwriteExisting=True)` call would append a
column. It creates a *table copy* instead: `FieldName` is coerced to a string (a
Long errors), and only a field NAME plus `ColumnPosition` writes a column — which
**replaces** the record at that position. `ColumnPosition` 8/9 is rejected, so the
append layout is still unmeasured and the writer deliberately does not claim it.

A second correction: the `114/Props` stream delta is **+144**, not +160 (the
project-name record shrinks by 16), and a `0x02401392` flag record tracks "custom
field data in use" — both would have produced a writer Project silently ignores.

## What wave 14 did not do (next)

1. **`214/` append** and `CV_iew` item encoding, `CEdl`/Props semantics; the
   view-state encoder exists but **nothing in the app calls it yet**.
2. **Formula writes are not wired into the app save**; clearing formulas while
   custom-field values remain must preserve `0x02401392`.
3. MPP12, MPP14 resource Type, summary-calendar rollup, elapsed-task mixes.
4. App multi-window, the **manual Mac smoke test**, LeakSanitizer/UBSan (MSVC has
   neither), a visual print oracle.

## Traps this wave paid for

* **A crashed oracle run poisons the tree.** It leaves a `python` blocked in COM
  holding `.oracle.lock` and **WINPROJ holding `tests/data/synthetic-base.mpp`
  open**: the fixture reads 238,080 bytes and every delete/write fails with a
  share violation. Kill both pids, remove the lock, delete +
  `git checkout HEAD -- tests/data/synthetic-base.mpp`, verify SHA256
  `55C109F2…`. `\\?\D:\...` reads where the plain path does not.
* **`FileSaveAs` reports a padded size** (344,064 / 360,448) and Project compacts
  on close (239,104). Only the post-close file is evidence.
* **AppKit deprecations are `-Werror` errors** on `app-macos`
  (`NSClipPagination` -> `NSPrintingPaginationModeClip`).
* `Assignment.SetField` does not exist; assignment custom-field VarMeta keys are
  `0x0f4040xx`, not the PjField ids.
* PowerShell backticks (`` `0 `` = NUL, `$` interpolates) corrupt doc files —
  use single-quoted here-strings.
