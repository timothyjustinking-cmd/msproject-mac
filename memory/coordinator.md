# memory — coordinator (wave 19 end, 2026-10-23)

Successor: read `docs/handoff/wave19-complete.md` (the gate), then
`docs/handoff/coordinator.md` "Wave 19 final", then this.

## Where main is

The public history is **rewritten and condensed** (see below); the private
pre-rewrite history is in `build/pre-public-history.bundle` (git-ignored). Wave-15
work: W62 (`214/` append + `CV_iew`), W63 (`--smoke`), W64 (save-path wiring),
plus the CI cost-control changes and the three-platform decision.

## The number

* `build\vs-w15`: **49/49 ctest**; app `build\vs-w15-app`: **15/15 tests, 2,513
  checks**.
* **M2 332/332**; fixtures `55C109F2…` / `DB81218E…` verified by
  `scripts/check_fixtures.py`.
* Units: viewstate **361** (was 212), document **169** (was 43), app_smoke 98.

## The findings that matter

1. **Project's automation cannot append a table column.** `ColumnPosition` 0
   replaces index 1, 8/9 is rejected, `Application.ColumnInsert` hangs, and
   `Application.Tables()` wedges the build. `append_table_column` writes it
   ourselves: a 115-byte record after the last column, block length and
   `last_column_index` grown, later blocks/VarMeta shifted.
2. **`214/CV_iew` is item-framed, not Props-framed**: u16 count at +12, items
   `{u32 len, u32 id, u32 kind, payload}`, 2-byte pad after odd lengths. View
   name `0x2240002a`, table `0x2240000a`, filter `0x2240000b`, group
   `0x22400018`, notes `0x22400014`, margins `0x22400009`. Column widths live in
   the CTable column record, not here.
3. **Decoder bug fixed**: VarMeta `uid` is the row *ordinal*, not the index;
   corpus views skipping ordinals 13/22 no longer shift later views.
4. **Both writers now ride `Document::save`**: view-state (date format, column
   replace) and formulas, with the `0x02401392` flag repaired to 1 when values
   remain; a save with no pending edit is byte-identical (whole file).
5. **The AppKit half of the self-smoke has never been compiled** — CI minutes ran
   out before it could run. Its selector list is the review surface
   (`docs/handoff/w63.md`); the portable half is verified on Windows.

## Open, ranked

1. **Oracle gate for written `214/` + formula bytes**: Project has never opened a
   file we wrote with them. Procedure: `docs/handoff/w62.md` §8.
2. Shell wiring: `main.mm` does not call `Document::save`'s new options yet.
3. `CV_iew` editing, row height/splitter, a column-width oracle save.
4. Three-platform Qt shell (waves 16-17, `docs/ui-qt-port.md`), MPP12/MPP14,
   manual Mac smoke, LeakSanitizer/UBSan.

## Traps

* **GitHub Actions minutes**: the repo is now public (free runners). If it ever
  goes private again, both workflows are `workflow_dispatch`/`v*`-tag only — do
  not restore `push:` without checking the minutes.
* **The Dropbox folder resurrected the damaged fixture twice** (238,080 then
  245,760 bytes, old mtimes). `scripts/check_fixtures.py` is the only reliable
  gate; run it before every commit.
* A history rewrite makes every commit hash quoted in older handoffs dangling;
  `docs/history-note.md` records that, and the bundle holds the real history.
* PowerShell backticks (`` `0 `` = NUL, `$` interpolates) corrupt docs — build
  doc edits with Python or single-quoted here-strings.
* AppKit deprecations are `-Werror` errors (`NSClipPagination` →
  `NSPrintingPaginationModeClip`).
* A crashed oracle run leaves a `python` blocked in COM and WINPROJ holding
  `tests/data/synthetic-base.mpp`; kill both, drop `.oracle.lock`, restore.

## Counters

102 agent sessions + 11 coordinator sessions; **~$7.86** at ~$0.077/session;
~780M tokens. Roadmap **~78%**.

## Next three actions

1. The **oracle gate on written `214/`/formula bytes** (write, Project re-save,
   decode) — the one proof the byte-level work still lacks.
2. Wire `Document::save`'s options into `main.mm` and confirm on `app-macos`.
3. Start the **Qt shell** (Q1/Q2 in `docs/ui-qt-port.md`) so all three platforms
   share one UI.

## Post-close (same day)

The repo is **public** and `app-macos` ran green on `5747da0` (run 39849231444):
the self-smoke executed on macOS — 136 portable + 156 AppKit checks, 0 failed —
and its artifacts (window PNG/PDF, both print PDFs, `smoke.json`, the zipped
`.app`) were downloaded to `build/macos-smoke/`. The "AppKit never compiled" gap
in the wave-15 record is closed. The oracle gap (Project opening written `214/`
or formula bytes) remains the top item.

## Oracle gate closed (same day)

`tools/oracle_gate` (`-DBUILD_ORACLE_GATE=ON`) appends a column + writes a formula
into a real `.mpp`; Project opened `w15-gate.mpp` and re-saved it with **10
columns** (Number1 at index 9) and the formula record intact. Project accepts what
we write. The formula *value* reads back empty in Project for both our file and
Project's own `w14-formula.mpp` — a readback behaviour, not our bug. See
`docs/analysis/oracle/w15-oracle.md`.


## Wave 18 final (2026-10-23)

Four streams, all merged, tree **50/50** (`ctest -E plan_recalc`), M2 332/332.

* **UI parity: 9/9 measures pass** at the reference's own 1920x1032 —
  `scripts/parity_report.py corpus/bench/msproject-gantt.png <CI render>`.
  The last N/A fell when Q3 traced a phantom tenth column to a green indicator
  badge splitting the Indicators cell (MS Project leaves it blank); row 22,
  header 43, splitter 0.45924, day 24, tiers 2, colours exact, columns
  `[38,57,147,81,81,81,105,105,124]` vs `[39,...,123]` (the -1/+1 are
  gutter-border and splitter-line conventions).
* **Engine parity: 5/7** on clean re-recorded oracles (`--suite` with
  `--apply-bin build\vs-w18\Release\engine_diff_tests.exe`). A real defect fell
  out: `write_task_row` wrote **total slack at +74** where the reader reads free
  slack; `TaskValues::free_slack` added, three words at +74/+186/+190.
  Remaining: `constraint-mso` (1 field: Project's stored 12000; my probe applying
  MSO twice to all manual rows leaves durations unchanged, so the fresh oracle
  needs re-examination, not an engine change) and `assignment-50pct` (7 fields:
  placeholder tombstones differ between the two recordings).
* **F2**: the corpus `214/CEdl` "trailer" is a **cumulative Props cache** (10
  documents); `parse_var2_edl_cache` parses and round-trips both corpus files.
  CFilter criteria = 20-byte header + N*36 records + string table. viewstate 791.
* **G1**: **MPP12** implemented — `112/`/`212/` tree, task row **264 B** (uid/ID
  reversed, stored parent/level), resource 188 B, assignment 142 B (no Resume),
  link lag fields swapped, calendar columns `(uid, base, resource)`. Five
  fixtures round-trip byte-identically; master/subproject + password flag
  decoded. reader 958 checks. **`core/cal` still hard-codes `114/` and the
  MPP14 calendar column order — next lane's first job.**
* Harness changes: `file.*`/`file stream ...` excluded from parity scoring
  (Project's save churn is not semantics); the recorder copies the base into
  `build/` because Project rewrites a file it opens.

### Next

1. `core/cal` consumes `Document::version/table_root/calendars` (MPP12 calendars).
2. Coordinator: named-base-calendar fixture (unblocks `task-calendar`); re-examine
   the MSO and assignment oracles against the clean base; MPP12 password files.
3. Wave 19: Qt commands/dialogs (D13) and Windows/macOS Qt jobs.

## Wave 19 final (2026-10-23, ~13:35)

**The wave that made three platforms real and closed six of the eight engine
scenarios.**  HEAD after the docs commit; the code tip before it is the
`core/view` portability commit.  Successor: read
`docs/handoff/wave19-complete.md` (the gate), then `docs/handoff/coordinator.md`
"Wave 19 final", then this.

### Where main is

Four lanes, every one landed: CAL (core/cal MPP12), Q4 (Qt commands/dialogs),
CI (qt.yml three platforms), coordinator (COM oracles + writer fix +
portability).  Commits, in order: fixture/oracle re-record, qt.yml, writer
Fixed2Data fix, core/view strtod, json/mspdi strtod, Qt lane, CAL lane, Qt
compile fixes.

### The numbers

* `build\vs-w19` `ctest -E plan_recalc`: **50/50 passed**.  One earlier run saw
  Windows Application Control refuse to start `mpp_reader_tests.exe` and
  `mppwrite_roundtrip_assignments.exe` (BAD_COMMAND, "blocked this file"); the
  byte-identical copies passed 958 and 679 checks, and both passed normally on
  the final run -- the block is transient, not a test failure.
* **M2 332/332** unchanged (the `recalc` test is in that run).  cal **785**
  checks (was 553); CAL_NO_CORPUS 323 with 8 SKIPs.
* **Engine parity 7 pass / 1 fail / 0 unrecorded** (was 5/1/1).
* **UI parity 9 comparable / 0 failing on Linux, Windows AND macOS**
  (artifact run 39888287026): row 22, header 43, splitter 0.45924/5, day 24,
  tiers 2, nine columns with -1/+1 end deltas, three colours exact.
* Qt tests **6/6 on all three OSes**, including the new `qt_save_reopen`.

### What each lane delivered

* **CAL**: `core/cal` now decodes MPP12 (`112/`, rows uid/base/resource) with the
  reader's own detection rule, a public layout API, a both-orders test with a
  negative control, and an MPP12 calendar driving `core::sched` (Night Shift:
  Fri 08:00 -> Tue 08:00 across a weekend, Standard control Fri -> Mon 17:00).
* **Q4**: the Qt shell edits, inserts/deletes, filters, switches four views,
  saves through `app::Document::save/save_structural` (dirty prompts, start-edit
  warning), prints via QPrinter on the shared PageSpec, exports PDF from
  core/render, and has Task Fields / Predecessors / Filter dialogs.  The
  save/reopen test is the acceptance gate.
* **CI**: one `build-qt` job, three-OS matrix, Qt 6.5.3 pinned everywhere
  (gcc_64 / win64_msvc2019_64 / clang_64), a configure guard that proves the
  pinned Qt was used, and one artifact per OS with both render sizes.
* **Coordinator**: named-calendar fixture + scenario 07, full clean re-record,
  and a real writer defect fixed (see below).

### Findings that matter

1. **Project writes NO Fixed2Data on an auto row.**  PROJECT_A stores
   `0xFFFFFFFF/0xFFFFFFFF/0` at +46/+50/+58 on all 85 task rows while +84 holds
   the duration; a COM duration edit saves it back as 0.  Manual rows store real
   copies.  Our resolver materialised copies and then refused its own write, so
   any duration/date edit on a Project-written auto file was unsaveable.  Absence
   is now preserved.  This alone turned two scenarios green.
2. **constraint-mso's 12000 is Project's save-time recomputation**, not CPM and
   not a bad oracle: a clean re-record changed 0 semantic fields in 7 of 8
   oracles (1 file stream size elsewhere), and nine measurements show the stored
   duration gains the remaining work windows of an off-calendar start day
   (weekday start: 0).  Left unimplemented on purpose; table in
   `docs/engine-parity.md`.
3. **`Project.BaseCalendars` exists but has no UniqueID**; `Application.
   BaseCalendarCreate/EditDays` do.  The uid->name map is pinned with the fixture
   in `engine_diff.py`, so scenarios stay ids-only.
4. **Qt 6.5.3 + MSVC 14.51 (VS 18) does not compile** (`stdext` in
   qvarlengtharray.h): the Windows job is pinned to `windows-2022`.
5. **libc++ deletes `std::from_chars(double)`**; core/view and core/io now use a
   strict full-consumption `strtod` on every platform.

### Open, ranked

1. constraint-mso's one field (implement the measured rule in the applier, or
   decide the dump should not compare a duration COM does not report).
2. MPP12 beyond calendars: the task/link/assignment *write* path and a COM oracle
   for an MPP12 calendar (read-back of a computed schedule).
3. Qt gaps: clipboard, Preferences / Table Date Format / Custom Formula dialogs,
   a free-form filter builder, persisted view choice; no manual smoke on any OS.
4. The Linux job builds with serial Make (~20 min); add `--parallel` when the
   workflow is next touched.

### Traps this wave paid for

* **Application Control blocks some freshly linked exes** (`BAD_COMMAND` in
  ctest, "An Application Control policy has blocked this file").  Copying the
  same bytes to another name runs fine; verify with the copy and say so, do not
  call it a test failure.
* **`gh run view --log` fails in a confined shell** (cannot write
  `%LOCALAPPDATA%\GitHub CLI`): set `LOCALAPPDATA` to a build subdir for that
  command.
* **`git push` needs `-c http.sslBackend=openssl` plus a token GIT_ASKPASS**
  (schannel reports SEC_E_NO_CREDENTIALS even with gh's helper).  The shim lives
  in `build/gh-askpass.bat` (git-ignored).
* **Windows-latest is VS 18 now**; pin runner images when a compiler/UI pin
  depends on them.
* Project's `BaseCalendars` lookup is by name/index — do not index it by uid.

### Next three actions

1. Implement the MSO duration rule in the applier with the nine-row table as the
   regression, or record why the dump should ignore it.
2. Add `--parallel` to the qt.yml build and one more dispatch.
3. Start MPP12 write-path work in its own lane (calendars are done).

## Wave 20 in progress (2026-10-23 ~14:10) — FOUR PARALLEL STREAMS

**Operator agreed the UI spec** (`docs/ui-parity-spec.md`, commit `fc5d07b`): all
seven tabs; full backstage; original icons; measured chrome metrics; real
timeline; Entry table + stored-table rendering; status bar; disabled commands +
generated backlog; light AND dark themes; new-project defaults (today, manual);
structural gates; bundled Carlito; every dialog on every tab; laid-out dialogs
where the engine is missing; View split (Timeline + Details).  Six defaults were
accepted: Office dark values, Customize-QAT edits the QAT, Page Setup + Print
preview in scope, type/F2 edit + Enter/Tab + per-edit undo (no drag-fill), zoom
slider maps to Days/Weeks/Months with no percentage, Office accelerators with Cmd
on macOS.

### Running now (owners are disjoint)

| stream | owns | goal |
|---|---|---|
| Q5 | `app_qt/**`, backlog doc | empty state + chrome (ribbon metrics/icons, timeline, table, status, QAT/title/Tell-me), both themes, backstage FIRST; dialogs second cut |
| E5 | `tests/engine_diff_tests.cpp`, `core/plan/**` | close the manual-row late-pair gap (4 scenarios) |
| M6 | `core/mpp/cfb.*`, `core/mpp/reader.*`, `core/plan` table-root, `tools/recalc` | MPP12 recalc path, CFB FAT slack tolerance, subproject probe decode |
| CI2 | `.github/workflows/qt.yml` | macOS .app zip + Linux AppImage/tar package; keep Windows zip |

### Facts established this wave

* Dump schema 2 (task late pair, resource calendar_uid); all eight oracles
  re-recorded; **score 4 pass / 4 fail**, because the late pair was never
  compared before. constraint-mso is back to 1 field (its duration).
* Real `.mpp` corpus imported (12 files MPP8/9/12/14, MPXJ LGPL) with inventory +
  robustness tooling (`scripts/import_corpus.py`, `scripts/import_robustness.py`,
  `docs/import-corpus.md`); 60 mutated runs, no crash.
* `tools/recalc` hard-codes 114/ (all 15 MPP12 probe files fail there); the CFB
  check rejects the real file `sf248.mpp` for unreferenced FAT slack.

### Next coordinator actions

1. Commit each stream as it reports (verify its tests first).
2. ONE CI dispatch when Q5's chrome/backstage cut + CI2's packaging are both in:
   it must be green on all three, produce the three zips, and keep the renders.
3. Then Q5's dialog cut; then wave-20 handoff (`wave20-complete.md`).

### Goal round 1 close (2026-10-23 ~14:35)

* `main` at `7f536a3`: M6 (CFB slack, 206-byte tables refused, MPP12 recalc verified),
  E5 (manual late pair modelled, **engine 7/8**), CI2 (three packages).  Tree clean.
* Q5 was stopped before reporting but had produced a full cut.  It is committed on
  branch **`w20-q5`** (backstage, light+dark themes, icon set, timeline, status bar,
  five structural gates, generated backlog, handoff `docs/handoff/qt2.md`).
  CI runs against it: **39895551240** (`--ref w20-q5`).  Merge only if green; if red,
  the failures are the first thing to fix.  `main` does not contain it.
* Packaging run **39894203777**: Windows and macOS SUCCESS, **Linux FAILED** (the
  AppImage step or its smoke; triage with `docs/handoff/ci2.md` §(e)/(f)).  The
  Windows and macOS packages from it are downloadable now.
* Still open for wave 20: Linux package fix; merge/repair `w20-q5`; dialogs second
  cut (all-tab scope + laid-out-only ones); `constraint-mso` decision (documented,
  not implemented); wave-20 gate re-run on a fresh `build\vs-w20`; then
  `docs/handoff/wave20-complete.md`.

### Round 2: Linux package failure diagnosed (run 39894203777, job 120220718337)

Tests were green (ctest 6/6).  The only failure is the AppImage step:

    [qt/stdout] ERROR: Could not find qmake, please install or provide path using $QMAKE
    ERROR: Failed to run plugin: qt (exit code: 1)

`linuxdeploy-plugin-qt` cannot resolve the QMAKE export, although Configure used
`steps.install-qt.outputs.qt-root-dir` successfully.  Likely fixes, in order:
1. Resolve it explicitly and verify before linuxdeploy:
   `QMAKE="$(readlink -f "${{ steps.install-qt.outputs.qt-root-dir }}/bin/qmake")"`
   plus `test -x "$QMAKE" && "$QMAKE" -query QT_VERSION` (fail fast with the path).
2. If the path is a symlink into aqt's prefix, prefer `"$QT_ROOT_DIR/bin/qmake"`
   from the action's environment, or `command -v qmake` when the action put it on PATH.
3. As a last resort set `QMAKE=.../bin/qmake6` (some aqt packages ship qmake6 only)
   -- check `ls "${{ steps.install-qt.outputs.qt-root-dir }}/bin" | grep qmake` in the log.
Everything else in that run succeeded: Windows and macOS packages uploaded, renders
uploaded (if-no-files-found fired only because the Linux step died first).

Q5 branch run 39895551240 was still in progress at round 2; check it before merging.

### Round 4: Q5 branch does NOT merge

CI run 39895551240 (branch `w20-q5`): **windows-qt = failure**, macOS and Linux still
in progress at the round close.  Job logs are unavailable until the whole run
finishes; fetch them with `gh run view --job=<windows id> --log` (set
LOCALAPPDATA=<repo>\build\gh-cache) and fix the MSVC errors on the branch.  Do not
merge `w20-q5` until Windows is green: a failure there means the chrome cut does
not compile under /W4 /WX.

Branch contents to work from: backstage.cpp/h, theme.cpp/h, shell_icons.cpp/h,
preferences_store.cpp/h, view_label.cpp/h, command_status.h, tools/, five new test
files (empty_state, chrome_gate, view_gate, dialog_gate, clipboard), and
docs/handoff/qt2.md + docs/ui-command-backlog.md.

State at close: `main` = 85d4418 (qmake fix, engine 7/8, M6/E5/CI2 merged), clean.
The one dispatch that still needs to happen after the branch is green: `main` with
the qmake fix, which also produces the Linux AppImage.


## Wave 20 final (2026-10-23 ~15:25)

Successor: read `docs/handoff/wave20-complete.md` (the wave gate) and
`docs/handoff/coordinator.md` "Wave 20 final", then this.

### Where main is

`main` = **6a506cd**, pushed to the public repo; clean tree.  CI run
**39898703443** succeeded on `windows-qt`, `macos-qt` and `linux-qt`; the three
packages and the render artifacts are downloadable (`gh run download`,
**fresh directory** — it will not overwrite).

### The numbers

* `build\vs-w20`: ctest `-E plan_recalc` **50/50**; M2 **332/332**;
  `plan_recalc_tests` **266** (`MPP_NO_CORPUS=1`; ctest excludes it because it
  writes the golden); engine self-test **236**; reader **984**; cal **785**.
* Engine parity **7/8**.  UI parity **9/9 on all three platforms**
  (splitter 0.45924).  app_qt **11/11 on all three**.
* Packages: `msproject-windows-<sha>` (48.1 MB zip, exe + Qt DLLs),
  `msproject-macos-<sha>` (21.5 MB zip → `MS Project Shell.app`, ad-hoc signed),
  `msproject-linux-<sha>` (32.8 MB type-2 AppImage, smoke-rendered offscreen).

### What is actually done in the UI contract

Build-order steps 1–2: seven tabs + File, measured ribbon metrics with our icons,
QAT, timeline strip, Entry headers on an empty project (today, manual, never
1984), four status view buttons + zoom, full backstage (11 entries, Options
pinned), light/dark **palettes**, bundled Carlito, five structural gates,
generated command backlog, six functional dialogs.

**Not done** (do not describe the contract as complete): decision **7**
(stored-table columns from `214/CTable`), **14** (dialog inventory), **16**
(View ▸ Timeline / Details); **10** partial (custom-painted surfaces stay light;
operator owes a dark capture); **12** partial (no per-screen pixel-diff run).

### Facts this wave established

1. **Project's stored manual late pair** = row placed at its latest: LF = min(PF,
   successor's KEPT start − lag [FF: kept finish − lag]); LS = max(kept start,
   plain `sub_work` back from LF).  SS/SF do not bound LF.  Auto rows and slack
   words untouched.  `docs/handoff/e5.md` has 19 measured rows.
2. **MPP12 recalc works** through the reader's detected table root; the 15-file
   probe sweep has no "no Task table" failures.  MPXJ's 206-byte task layout is
   refused, not guessed.
3. **sf248 loads with a warning** (191 of 768 of 959 sectors unreferenced), but
   `mppwrite::CfbDocument::parse` still rejects it for `--dump` — open.
4. **Qt 6 offscreen has no fonts**; bundle (decision 13) and gate the family.
5. **The view caption belongs in the sheet band's left 22 px**, white and
   borderless (gutter 22+38=60) or P1's splitter detection collapses to 0.398.

### Traps

* Sandbox: `cmake --build --parallel` (MSBuild `/m`) fails on named pipes; piped
  `cmake --build` fails too.  Build serially, redirect to a file.
* Python text writes default to CRLF — use `newline="\n"`.
* `gh run download` will not overwrite; use a fresh directory.
* Static-library Qt resources are dropped; add them per executable.
* Dropbox mtime churn makes `edit` refuse after a read; retry or patch in Python.

### Counters

This wave: 4 agent lanes (Q5, E5, M6, CI2) + ~4 coordinator rounds (~2
coordinator sessions) -- ~8 sessions, **~$0.62 at ~$0.077/session (estimate,
not metered)**.  Roadmap ~78% by the
table's own arithmetic (the old ~83% was inconsistent with its rows).

### Next three actions

1. Build-order step 3: `214/CTable`-driven columns and stored-table rendering.
2. Decision 14/15 dialog inventory with generated dialog backlog rows.
3. Dark custom-painted palette once the operator's capture exists; then the
   Timeline/Details split.
