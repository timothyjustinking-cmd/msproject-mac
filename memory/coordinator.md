# memory — coordinator (2026-10-27, wave 31 closed on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave31-complete.md`.  History:
`memory/archive/coordinator-through-wave20-full.md`.

## Where the branches are

`main` = **fcb1c10** = `origin/main`.  **`wave23`** (`e1d40b8` =
`origin/wave23`), **`wave24`** (`6dbf126`), **`wave25`** (`c012813`),
**`wave26`** (`1299cc7` = `origin/wave26`), **`wave27`** (`bc52cb8` =
`origin/wave27`), **`wave28`** (`21646f4` = `origin/wave28`), **`wave29`**
(`2a7fa4b` = `origin/wave29`) and **`wave30`** (`ca9fd50` = `origin/wave30`)
are closed and **not merged**.

**Wave 31 is closed on `wave31`, cut from `wave30` (`ca9fd50`), pushed and not
merged**; `origin/wave31` = the CLOSE docs commit, final code tree `072773b`.
Wave 31 commits: brief `aff4710`; G0 0.1 `33fbb3d`, 0.2 `d9d8d8d`, 0.3
`659fd97`, 0.4 `9c3213e`, G0 record `e47ab5f`; G1 1.1 `0707d4d`, 1.2 `eb39bd4`,
1.3 `2852bd6`, G1 record `c0b05cb`; G2 2.1 `712be75`, 2.2 `6439e70`, 2.3
`25121ff`, 2.4 `e477012`, G2 record `1917823`; G3 3.1 `3aefddd`, 3.2 `25a45a5`,
3.3 `af3efd4`, 3.4 `815b974`, 3.5 `d4adbb0`, 3.6 `2b00548`, 3.7 `bb0568d`, G3
record `8a01b11`; G4 4.1 `6a8fd96`, 4.2 `fe35886`, 4.3 `4c32189`, 4.4 `f0ffb6f`,
4.5 `830d414`, G4 record `9d27c7f`; G5 5.1 `45d1206`, G5 record `d222ef3`;
CLOSE fixes W31-C1 `644afe0` (backlog lists disabled Fill), C2 `5fa3d5d` (usage
captions torn down), C3 `165e497` (Open/New tear down too), C4 `113d16a`
(backlog covers every command button, checked both ways), C5 `cb69a0b` (the
disabled View commands named in the ribbon switch for GCC/Clang `-Wswitch`),
C6 `c6e4314` (the Resource Sheet gate scans painted areas, not one pixel),
C7 `072773b` (the unused `row_map_uids` helper removed for GCC/Clang
`-Werror=unused-function`); then the CLOSE docs commit.  Every group marker
(`WAVE-31-G0-COMPLETE` .. `WAVE-31-G5-COMPLETE`, then `WAVE-31-COMPLETE`) is in
`wave31-complete.md`.

## Numbers (wave 31 close, fresh trees build/w31-close-fresh{,-app,-qt}, final code tree 072773b)

* core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0 / exit 0;
  core ctest `-E plan_recalc` **52/52**; plan_recalc **512** with corpus /
  **266** without; reader **1136** with corpus / **456** without; M2
  **332/332**, **268/268** and **64/64**; app **25/25**; app_qt **37/37**;
  fixtures OK (**139** documented hashes, **173** tracked `.mpp`), paths OK
  (222), oracle provenance OK (8).  The final-tree run (072773b) logged 08:26 and
  finished 08:36; the log is `build/w31/W31-CLOSE-fresh-gates.log`.
* Engine **7/8** (`constraint-mso` deliberately expected-failing;
  `mppwrite::save_plan` is test-only); selftest 53 checks, suite 8 recorded
  (7 pass + 1 expected), 0 unrecorded, exit 0.
* Parity: **wave23-outline 10/10** and **wave25-auto colour 1/0**, re-run on
  the fresh Qt shell at 072773b (`build/w31/W31-CLOSE-fresh-gates.log`); every
  measure PASS.  The pixel score is not re-run (no reference PNG locally).
* CI: **green on 072773b, the third dispatch**: qt.yml **40404390641**
  (`windows-qt`, `macos-qt`, `linux-qt`, both parity gates) and ci.yml
  **40404395191** (`windows-msvc`, `ubuntu-gcc`); the first dispatch was red on
  all three Qt jobs (W31-C5/W31-C6) and the second on the two Unix Qt jobs
  (W31-C7), ci.yml green each time.  Snapshots
  `build/w31/close-ci-{qt,ci}-status.json`.
* Privacy: line scan **0 files / 0 hits** on HEAD and **0 commit messages** on
  `wave30..HEAD`; normalised `guard_names.py` **0 hits, exit 0** on
  `wave30..HEAD` and on the push range `HEAD --not --remotes=origin`; no
  `client-string-<8 hex>`.  The six new fixtures use the neutral `C:/w31` root
  and neutral metadata.  Residuals (the operator's decision): two pre-wave
  fixtures (`synthetic-base.mpp`, `wave23-outline.mpp`) carry the operator's
  name three times each, 69 tracked text files (historical docs) carry it, and
  the 13 unsalted FNV-1a name hashes removed in wave 27 remain in older
  wave-26 commits plus `tests/data/project_a_golden.tsv`.  **Run BOTH scans.**
* Backlog: **114 ribbon commands with 69 disabled** (45 enabled; 95/64 at
  wave 30), and 51 menu commands with 7 disabled.  `scripts/check_command_backlog.py`
  is committed as ctest `qt_command_backlog` and checks the generated file
  against the pin both ways (114 rows, 0 mismatches).

## Wave 31 result (what a later session must not re-break)

* **No persisted view-state writer exists or was enabled.**  The View tab's
  Sort (five keys + Sort dialog), Outline (Level 1..9 / All Subtasks) and Tables
  (the file's stored task tables) are applied to the loaded session, and
  `qt_data_commands` asserts the file's SHA-256 is unchanged.  Zoom (Zoom
  dialog), Entire Project and Selected Tasks fit Project's measured windows on
  two fixtures (`tests/data/reference/w31-zoom.measures.json`) and are also
  session-only.  New Window, Switch Windows, Arrange All, Hide and Macros stay
  disabled with the exact tooltip and their own backlog reasons.
* **G3 Resource Sheet fidelity (presentation only):** the file's stored
  resource Entry table, view Text Styles (overallocated rows bold RGB 165,0,0),
  stored widths under `chars * unit + 3`, the tombstone-skipped base resource
  (`FixedMeta` liveness, W31-3.2), Project's computed PeakUnits and
  overallocation, and the (i) indicators badge.  `qt_resource_sheet_gate` reads
  the live window back against Project's COM facts.
* **G4 Task Usage and Resource Usage fidelity (presentation only):** stored
  Details lists (Work + Actual Work / Work + Cost), stored two-tier timescale,
  assignment rows under each task/resource, typed per-period Work/Cost values
  and measured widths.  `qt_usage_gate` compares every bucket with COM
  `TimeScaleData` on `tests/data/reference/wave31-usage.measures.json`.
* **G0 closed the wave-30 review's four Highs:** a link added after a queued
  automatic outline edit is refused in both orders; the automatic outline save
  refuses file shapes no Project pair measured (progress/actuals, constraints,
  deadlines, other summaries); percent/date edits refuse progress outside the
  edited rows unless the edit is the measured Entire project update; Fill Down
  was measured and its refused save is why Fill stays disabled.
* **G5:** Network Diagram, Calendar and the two Other Views entries stay
  disabled with the exact tooltip and the backlog's scope reason.
* **G3/G4 left presentation gaps** (in the record's "Found, not fixed"): Add
  New Column is a stub, other stored usage fields are not painted, the port's
  Total row/column, Actual Work/Cost approximations, selected tier size, and a
  decoded-but-not-applied scroll origin.
* **The CLOSE review is binding**: 1 Critical / 4 High / 3 Medium / 8 Low
  (C1/C2/S1/S2 core, U1-U3 UI, A1-A9 record audit).  The Critical/High are
  closed (A1/A3 by committing the record and docs, A2 by push + CI, U1
  `5fa3d5d`, U2 `165e497`); the Mediums are closed (U3 `113d16a`, the A4-A9
  errata).  The queued Lows have fix directions: C1 (MPP12 Accrue At 0 prints
  "0"), C2 (caller-set `update_entire_project`), S1 (unchecked rate reaches
  `llround`/cost multiply), S2 (Resource Sheet style fallback can take a
  non-resource view).  A5 (per-commit gate logs not written for three items)
  is recorded as a gap.  The CI dispatches added three platform findings --
  W31-C5 `cb69a0b` (the disabled View command ids named in the ribbon switch,
  because GCC/Clang build with `-Werror=switch`), W31-C6 `c6e4314` (the
  Resource Sheet gate scans the painted badge/glyph areas scaled by the
  device-pixel ratio instead of sampling one pixel, which failed only on
  windows-qt) and W31-C7 `072773b` (the unused `row_map_uids` helper in
  `data_commands_test.cpp` failed Linux/macOS under `-Werror=unused-function`);
  all are fixed and the third dispatch is green on the final tree.
* **The wave-30 measured-and-deferred commands still wait on their shared
  writers** (VarMeta insert/remove, 214 view stream, TBkndCal + Props counts,
  RichEdit notes, append-and-re-key, scheduler/DSI, baseline, WBS code
  definition/renumber).  The view-state saves (sort/table/outline/zoom/window
  arrangement) were never measured against Project.

## Next

Wave 31 is the last wave of the approved run of waves 27-31.  The operator's
next step is to **verify and merge the unmerged branches** (wave23 through
wave31), then choose the next scope from `wave31-complete.md` "What remains
after this run": Team Planner, Network Diagram, Calendar and Report views, the
`constraint-mso` engine case, macOS launch/notarisation, the hashed task names,
and the queued review Lows (including the carried wave-30/29 queues).  Keep the
CLOSE gates green before every commit and run BOTH name scans.
