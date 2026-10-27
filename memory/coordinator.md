# memory — coordinator (2026-10-27, wave 32 closed on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave32-complete.md`.  History:
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

**Wave 32 is closed on `wave32`, cut from `wave31` (`3a18e64`), pushed and not
merged**; `origin/wave32` = the CLOSE docs commit, final code tree `7727760`.
Wave 32 commits: brief `b16a9fa`; W32-0.1 `4834b40`, W32-0.2 `290a73a`, W32-0.3
`5650b53`, G0 record `123ec83` and `0c0e014`; CLOSE fixes W32-C1 `17e24ef`
(plan-waves27-30 back to LF), W32-C2 `017ef44` (backlog states the wave-32
refusals), W32-C3 `64fa2fc` (the three texts no longer say Start is written),
then the review fixes W32-C4 `71383a6` (the Entry column index is int; app_qt
builds with warnings as errors), W32-C5 `786024a` (order-1 actuals test), W32-C6
`3d0c80f` (macOS funnel refuses Start; stale Start-save texts gone), W32-C7
`0e6e74c` (ribbon handlers clicked under the sort), W32-C8 `7727760` (the save
check's unreachability recorded); then the CLOSE docs commit.  The markers are
`WAVE-32-G0-COMPLETE` and `WAVE-32-COMPLETE` in `wave32-complete.md`.

## Numbers (wave 32 close, fresh trees build/w32-close-fresh{,-app,-qt}, final code tree 7727760)

* core, app and app_qt configured with `CORE_WARNINGS_AS_ERRORS=ON` (the
  review's H1 hole is closed); core configure/build exit 0 / exit 0; core ctest
  `-E plan_recalc` **52/52**; plan_recalc **512** with corpus / **266** without;
  reader **1136** with corpus / **456** without; M2 **332/332**, **268/268** and
  **64/64**; app **25/25** and `app_edits_outline_tests` **3230 checks**;
  app_qt **37/37**; fixtures OK (**139** documented hashes, **173** tracked
  `.mpp`), paths OK (222), oracle provenance OK (8).  The log is
  `build/w32/W32-CLOSE-fresh-gates.log`; the earlier runs are preserved as
  `...-64fa2fc.log` and `...-run1.log`.
* Engine **7/8** (`constraint-mso` deliberately expected-failing;
  `mppwrite::save_plan` is test-only); selftest 53 checks, suite 8 recorded
  (7 pass + 1 expected), 0 unrecorded, exit 0.
* CI: `origin/wave32` = `7727760` pushed without force after a clean preflight
  (`build/w32/W32-CLOSE-push-preflight-7727760.log`), then `qt.yml` run
  **40456047971** (windows-qt, linux-qt, macos-qt) and `ci.yml` run
  **40456051113** (windows-msvc, ubuntu-gcc) dispatched explicitly on `wave32`;
  all five jobs `success` on `7727760`, no re-dispatch needed.  The CLOSE docs
  commit that follows changes docs only.
* Privacy: line scan **0 files / 0 hits** on HEAD and **0 commit messages** on
  `wave31..HEAD`; normalised `guard_names.py` **0 hits, exit 0** on
  `wave31..HEAD` and on the push range `HEAD --not --remotes=origin`; no
  `client-string-<8 hex>`.  Wave 32 adds no fixture.  Residuals (the operator's
  decision): two pre-wave fixtures (`synthetic-base.mpp`, `wave23-outline.mpp`)
  carry the operator's name three times each, 69 tracked text files (historical
  docs) carry it, the 13 unsalted FNV-1a name hashes remain in older wave-26
  commits plus `tests/data/project_a_golden.tsv`, and R31-11 (one wave-31 brief
  line) stays queued.  **Run BOTH scans.**
* Backlog: **114 ribbon commands with 69 disabled** (45 enabled) and 51 menu
  commands with 7 disabled.  `scripts/check_command_backlog.py` checks the
  generated file against the pin both ways (114 rows, 0 mismatches); the six
  wave-32 session refusals and the Start editing row carry their reasons.

## Wave 32 result (what a later session must not re-break)

* **The automatic-outline cross-refusal is file-wide, both orders.**  An
  automatic (slack-carrying) indent/outdent is refused while any percent,
  actuals or typed-Start edit is queued on any row, and `push` refuses a
  percent/actuals edit while an automatic outline edit is applied;
  `save_structural_edits` repeats both (plus the Start half no `StructuralEdit`
  sees).  The manual outline rules stay narrower.
* **Start is read-only at every shell entry** (W32-0.2): `task_column_editable`,
  Task Information (read-only), the Task Form (read-only + disabled), Cut of a
  selection touching Start and Paste of a block covering Start, plus the save
  funnel and a `push` refusal for a collected/writable start edit.  The writer
  stays in the tree; **do not enable Start without the measured Project pair**
  (the wave-31 Fill pair shows the writer cannot reproduce the save).
* **A sorted or filtered display refuses range operations** (W32-0.3):
  multi-cell Copy/Cut, multi-row Paste and Shift-click extension are disabled
  and refused whole while `display_order_differs_from_file()`; Indent, Outdent
  and Insert are refused while `sort_reorders_rows()`.  Single-cell copy/cut,
  a one-row paste and single-cell edits stay and are mapped from the display row
  to its record.
* **No writer and no measurement were added.**  The wave is fix-only; Fill
  stays disabled on its W31-1 measurement and the view state stays session-only.
* **The independent review** (0 Critical / 1 High / 4 Medium / 6 Low) was
  triaged in CLOSE: W32-C4 fixed the High (the `std::size_t` Entry column index
  that would have failed all three qt jobs under warnings-as-errors) and closed
  the gate hole by configuring app/app_qt with `CORE_WARNINGS_AS_ERRORS=ON`;
  W32-C5..W32-C8 fixed the four required Mediums.  L1..L6 (test strength and
  the filter wording) are queued in the record with directions.
* **The G0 record has three errata** in `wave32-complete.md`: the shell passes
  Start uids rather than naming them in the refusal; Task Information makes
  Start read-only while the Task Form also disables it; the G0 per-commit logs
  captured no exit lines and the G0 guard log is empty.  CLOSE logs capture
  exits.

## Next

Wave 32 closes the wave-31 verifier's High findings.  The operator's next step
is to **verify and merge the unmerged branches** (wave23 through wave32), then
choose the next scope from `wave32-complete.md` "Found, not fixed" and
`wave31-complete.md` "What remains after this run": Team Planner, Network
Diagram, Calendar and Report views, the `constraint-mso` engine case, macOS
launch/notarisation, the hashed task names, and the queued review Lows
(R31-5..R31-12, the four core Lows C1/C2/S1/S2, and the carried wave-30/29
queues).  Keep the CLOSE gates green before every commit and run BOTH name
scans.
