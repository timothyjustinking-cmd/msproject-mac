# Coordinator handoff — state at 2026-10-22 ~14:30 (wave 7)

Read this first, then `WAVES.md` (wave-7 board and results), `docs/roadmap.md`,
`docs/cost.md`, `docs/coordination.md`. This file is what the streams cannot
know: the oracle results, the traps, and what is genuinely open.

## Where main is

- `8f6f590` or later (`git log --oneline -12`), pushed. Wave-7 commits:
  `780d5d6` (oracle), `d1981cb` (CI fix), `1ae6840` (board + COM date mapping),
  `d4831e6` (W27), `95844f8` (W28), `84cf76d` (W23+W24 + M2 re-pin),
  `e8a1772` (W25 + calendar wiring), `c7beb50` (W26 + writer spec),
  `8f6f590` (script/memory). **Check `gh run list`**: wave 6's tip `1575b9d`
  was red on clang/gcc (unused `get_u16`) despite the brief saying CI was green.
- Full suite, clean build `build\vs-cal`, MSVC `/W4 /WX`: **24/24 ctest passed**
  (was 20; + `cal`, `plan_attribution`, `io_mspdi_crosscheck`,
  `mppwrite_roundtrip_authority`). `plan_recalc` 186 checks.
- M2: **start 83/83, finish 83/83, slack 47/83, critical 53/83 = 266/332**
  (was 228/332), 47 fully matching uids. Every remaining mismatch is
  slack/critical: 45 link-family, 16 summary (W3-P7), 5 milestone, 0 unexplained.

## What works (with evidence)

| dir | what | evidence |
|---|---|---|
| `core/model` | 3,207-field table + C ABI | 119,614 checks |
| `core/mpp` | task/resource/link/assignment reader | `mpp_reader_tests` 489 (no corpus 207); PROJECT_A golden 83/83; **PROJECT_A links exactly 88, the same multiset Project exports** |
| `core/mppwrite` | CFB round trip, patching, stream replace, consistent row writer, **authority fixtures** | 5/5 ctest; authority 1045; 688 cfb checks |
| `core/sched` | date/duration/calendar/CPM/units/rollup; **0-d milestones, FF lead + floor, free-slack rule** | six binaries, 1884 checks |
| `core/cal` | **`114/TBkndCal` + `114/Props` calendar decoder + builder** | `cal_tests` 165; holiday T1/T2/T3 exact; PROJECT_A 228 week overrides |
| `core/plan` | reader->CPM bridge + **per-family attribution** | 186 + 54 checks; the matrix above |
| `core/io` | MSPDI import/export, CSV, **`mspdi_cmp` cross-check** | io 125, writer 860, crosscheck 23; 830 PROJECT_A fields, 0 mismatches |
| `core/view`, `core/sheet`, `core/gantt` | view/sheet/gantt models | their tests (untouched this wave) |

## The wave-7 oracle results (all re-runnable)

`python scripts/oracle_wave7.py export|holiday|milestone|lagff|open|edit-manual-start`

* **PROJECT_A -> MSPDI** (`FormatID="MSProject.xml"`): 84 Task elements (UID 0 is the
  project summary), **88 PredecessorLink rows** — exactly the 88 FixedMeta
  entries — 13 constraints (12 SNET + 1 FNET), 0 deadlines, 34 actual rows,
  67 assignments, 0 summary links, 0 self-links. `project_a_export_links.json` keeps
  uids only; the XML stays in git-ignored `corpus/probe/`.
* **PROJECT_A links**: 99 rows, 91 FixedMeta entries (header says 88), 88 distinct
  covered rows, 11 uncovered. 10846's real preds are 10849/10877/10759 (not
  10760); 10859's is 10858 lag 0 (not 10758 + 6d). The 9 phantom rows are the
  old cascade cause.
* **Milestones**: 0-d FS+0 keeps the predecessor's 17:00; FS+50d 12-28 17:00;
  SS+0 08:00. An assigned 17:00 Start normalises to SNET next-morning.
* **FF leads**: FF-1d/-3d are applied on a 5-day predecessor; FF-6d is floored
  at the project start (the old "lag dropped" row is that floor). SF+1d is
  `AddWork(08:00,480)=17:00`, not anomalous. The backward FF lead is clamped to
  `succ.LF`; free slack is `min(raw slip, total)`.
* **Calendar**: holiday1's SiteCal is the **task** calendar (row `+178` = 3);
  the project default is Standard. The Standard week lives in `114/Props`
  (key 37753736, 420 B), not in TBkndCal. PROJECT_A: 33 calendar rows (10 base + 23
  resource), 1331 dated exceptions all 2002..2024 -> 228 week overrides; its
  2026 dates are unchanged, which is why M2 stays 83/83 with the real calendar.
* **COM dates are local wall clock**, not UTC; the six FixedData date words are
  `+96/+100` (calculated Start/Finish), `+104/+108` (**effective** Start/Finish),
  `+112/+116` (LateStart/LateFinish). Proven by opening w3-o1 and by
  `manual-start-edited.mpp`.
* **Writer authority (manual task)**: changing `+96/+100`, `+104/+108`,
  `+112/+116` or `+84/+88` alone does nothing on open; a Fixed2Data change moves
  Finish (`+54`, and `+46/+50` also), never Start. Project's own save after
  setting Start writes `+104/+108`, `+112/+116`, F2 `+50/+54` and leaves
  `+96/+100`, F2 `+46` stale. The manual-task **Start store is still open**:
  the next experiment is a whole-row diff of `manual-start-edited.mpp` vs
  `base.mpp` (Var2 included), not another single word.

## Open (ranked)

1. **Writer**: make a manual task's written dates survive. Two candidates:
   set a real `SNET` constraint (measured to pin an auto task), or reproduce
   Project's manual-save encoding from the `manual-start-edited.mpp` diff.
2. **Slack/critical**: 66 fields, all here. 45 link-family (backward pass /
   stored TotalSlack; uid 8141 ours 0 vs stored 6240; 10763 ours 28320 vs 0),
   16 summary (decide W3-P7 inherit vs recompute with a fixture that separates
   them), 5 milestone. `recalc --attrib-json` prints the matrix.
3. **Reader**: expose task-calendar uid at row `+178` (all PROJECT_A/PROJECT_B rows are
   -1, so project default is correct today); assignment dates `+52/+56/+60`;
   MPP12.
4. **Engine**: ALAP `TotalSlack` (schedin uid 2: Project 0, engine 5280),
   elapsed lag (needs a weekend-crossing fixture), FF floor for a 0-d successor
   (unmeasured, flagged), recurring calendar exceptions (counted, not applied).
5. **Output**: PROJECT_B has no MSPDI export yet; one export runs `mspdi_cmp`
   unchanged. The writer's LinkLag spelling (ISO vs decimal tenths) is a
   decision, not a bug.

## Next three actions

1. Spend one oracle session on the writer: diff `manual-start-edited.mpp`
   against `base.mpp` stream by stream (TBkndTask FixedData/Fixed2Data/VarMeta/
   Var2Data), then build one fixture that writes exactly the changed set; open
   it. If it survives, wire it into `recalculate_write_path`; if not, test the
   SNET-constraint path on an auto task.
2. Attack slack/critical with the attribution: pick the 45 link-family rows,
   read Project's stored TotalSlack vs the backward pass on `project_a-recalc.mpp`,
   and settle whether the engine's late dates or the stored convention is wrong.
3. W3-P7: build a summary fixture with a non-critical child + project slack so
   inherit vs recompute is distinguishable, then pin it.

## Traps (do not relearn)

1. **pywin32 converts a naive datetime to UTC** on input; pass aware UTC
   datetimes (or strings). COM *getters* return local wall clock — the old
   "UTC frame" reading of `+104/+112` is withdrawn.
2. `Application.Calculate` does not exist: `CalculateProject`. `FileClose()`
   defaults to Save=2 and blocks; always `FileClose(0)`.
3. `FileSaveAs` to MSPDI uses `FormatID="MSProject.xml"` (PjFileFormat has no
   XML member); it needs an absolute path.
4. `Application.BaseCalendars` is a **method** returning a collection; a
   WeekDays read-back hung/RPC-failed. The task dates are the proof.
5. **A stale fixture can invalidate a whole oracle run**: `oracle-recalc.json`
   was 40 s older than its `.mpp`. Check `Get-Item` mtimes before trusting a
   result, and name the file in the JSON.
6. `ctest -R cal` also matches `core_sched_calendar`/`plan_recalc`; use `^cal$`.
   Target names are `core_sched_<module>_tests`, not `sched_cpm_tests`.
7. MSVC is not on PATH; `vcvars64.bat`, "Visual Studio 17 2022", never
   `cmake --build --parallel`; `MSB6003` on a `.tlog` is Dropbox/AV — retry.
8. clang/GCC reject unused file-scope constants/functions and missing
   initializers; MSVC does not. CI is the only place this shows up.
9. `plan_recalc_tests` rewrites `tests/data/project_a_recalc_golden.tsv` on every run
   with corpus present. Re-run it after any reader/engine change and commit the
   golden; the pinned counts in the test are exact (83/83/47/53 now).
10. `corpus/` is client data: never commit a task, calendar, exception or
    work-week name. PROJECT_A's default calendar name is client data; keep it out.

## Counters

58 agent sessions (51 at the wave-7 start + 6 streams + 1 W26 resume) + 3
coordinator sessions; ~$4.45; ~445M tokens; ~$0.077/session. Roadmap ~42%.

## Wave 8 addendum (in flight, 2026-10-22 ~14:50)

Landed and verified: **W34** sheet editing/undo (`ae3719c`, 1052 checks), **W35**
Gantt real-calendar shading (`9a83d41`, 644 checks), **W29** writer
(`c9243b7`): `pin_task_start` (SNET + date at +64/+66) IS honoured by Project
(`w29-snet.mpp`: uid 1 on 2026-10-21 08:00-17:00); the manual 7-word set plus
Fixed2Meta +88=0 and StartText/FinishText VarMeta removal still does NOT move
Start, so manual scheduling stays open. **W33** MSPDI parity (`a5c6588`):
assignments/resources now compared; PROJECT_A and assign1 MATCH with 0 differences.
PROJECT_B's 2 links/10 assignments to deleted slots are named exclusions; its only
real diff is uid 10805 (+96/+100 calculated vs +104/+108 effective) -> W31 is
switching Task.start/finish to +104/+108.

**Slack offsets corrected (W30):** +74 = FreeSlack, +186 = StartSlack, +190 =
TotalSlack (spec `b568be3`). w3-o1 hid this because all fields are equal there.
Projected M2 once reader+plan land: 83/83/83/83 = 332/332.

Still running: W30 (core/sched + core/plan: summary mode recompute, actual_start),
W31 (reader: +190 TotalSlack, +104/+108 dates, +174 start anchor, +178 calendar,
stored milestone), W32 (app/ AppKit shell + .github/workflows/app-macos.yml).

Next: verify/commit those three; full clean build + ctest; re-run
`recalc --compare`, re-pin plan_recalc_tests and regenerate the golden; update
roadmap/cost; then launch wave 9 (W36 session/document model, W37 writer
end-to-end, W38 app shell v2, W39 PDF output, W40 robustness/parity tail).

### Wave 8 near-final (2026-10-22 ~14:55)

**W31 committed `04f150d`**: reader now gives recalc **331/332** — start 83/83,
finish 83/83, slack **82/83**, critical 83/83; the only mismatch is uid 10851
(ours 2400 vs stored 39360). mspdi_cmp: PROJECT_A and PROJECT_B both MATCH (PROJECT_B 730
fields, 70/70 links, 63/63 assignments). mpp_reader_tests 581 (no corpus 239).
W32 is committed `66a14a9` and the new `app-macos` CI job is green (AppKit
target builds and links). Only **W30** (core/sched + core/plan: summary
`recompute`, `CpmTask::actual_start`) is still running.

Next (successor): verify/commit W30; re-pin `tests/plan_recalc_tests.cpp`
(current pins are 92 links/5 skipped and 73/69/34/52; actual now 88/0 and
83/83/82/83, or 332/332 if W30's plan fixes land) and regenerate
`tests/data/project_a_recalc_golden.tsv`; run the clean full suite and M2 recount;
update roadmap/cost; then launch wave 9 (W36 session, W37 writer end-to-end,
W38 shell v2, W39 PDF, W40 robustness).

### Wave 8 final (2026-10-22 ~15:00)

M2: **332/332** (start/finish/slack/critical 83/83 each), golden 83 uids,
plan_recalc 185 checks. Commits: W29 `c9243b7`, W30 `1431991`, W31 `04f150d`,
W32 `66a14a9` (app-macos CI green), W33 `a5c6588`, W34 `ae3719c`, W35 `9a83d41`,
re-pin `28ce66c`. W31's ci run was red only on the old plan_recalc pins (base
manual-task dates); `28ce66c` fixes it and its CI run is the check.

Known open: (1) ALAP display dates -- schedin uid 2 engine 2026-11-03 vs stored
2026-10-19, pinned as a documented divergence in plan_recalc (slack itself is
10/10 now); (2) manual scheduling writes still fail (SNET is the path);
(3) assignment Stop (+60) inferred; (4) app shell is display-only.

Wave 9 to launch from here: W36 session/document model, W37 writer end-to-end
(SNET save flow + manual follow-up), W38 app shell v2, W39 PDF output, W40
robustness/parity tail.

## Wave 9/9b final (2026-10-22 ~17:15)

Seven streams in two launches (W36-W40 together, then W41/W42 while the first
five were still landing, because their modules were untouched). Coordinator:
this session. **Read `memory/coordinator.md` next** — it is the short version of
everything below, plus the traps.

### Where main is

`f35a5ab` (wave-9 prep) then, in order: `6e0dadf` W36, `2d5d1cc` W37, `9141946`
W39, `3baeae4` W38, `80a78d9` oracle tooling, plus the W40/W42 commits named in
`docs/handoff/wave9-complete.md`. Pushed. CI green on `windows-msvc`,
`macos-appleclang`, `ubuntu-gcc`; the separate `app-macos` workflow green too
(that is the only proof for ObjC++).

### M2 and the verification numbers

* M2 **332/332** — start 83/83, finish 83/83, slack 83/83, critical 83/83, 0
  unexplained; golden 83 uids; `plan_recalc` 185 checks.
* schedin (ALAP) **20/20 dates, 10/10 slack** — see the ALAP finding below.
* Test registration: 24 ctest tests at wave 8 -> **30** with the wave-9 core
  modules, plus 3 in the standalone `app/` project.

### Per stream

| # | Stream | Deliverable | Verified by |
|---|---|---|---|
| W36 | session/document model | `core/session/`: load -> reader -> plan (decoded calendar) -> RecordSet -> `SheetModel` + `GanttSource` + copyable `Status`; `recalculate()` with no file I/O; name-free `status_text()`/`dump()` | `session_tests` **821 checks** (corpus) / **121** (`MPP_NO_CORPUS=1`); `ctest -R ^session$` 1/1; sheet rows == Gantt bars == 83; every row equals an independent `core/plan` run; 167 client-data checks |
| W37 | writer end-to-end | `core/mppwrite/save.h`: `save_date_edits` (SNET pin + mode-bit conversion), `save_plan`; validate-all-before-write, then **re-open from disk** and re-read constraint + values | writer suite **7/7**; `mppwrite_save_flow` **101 checks** (76 without corpus); fixtures regenerated by the API are byte-identical (FNV `0xc917b3380e0d660c`) |
| W38 | app shell v2 | open/save panels + menus, two-way sheet<->Gantt selection with dependency hit test, in-cell editing + undo/redo, disclosure collapse, save through W37, **date identity fix** | `build\vs-w9-app`: **3/3** tests (`app_layout` 253, `app_selection` 90, `app_document` 43); `app-macos` CI green; README states what persists (starts only) |
| W39 | PDF output | `core/render/`: dependency-free PDF 1.7 writer (xref, WinAnsi, clipping, determinism) + sheet/Gantt report renderer | `render_tests` **169 checks**; externally validated with **pypdf** (6-page and 2-page samples, text extraction); corpus 83 tasks -> 3+3 pages |
| W40 | robustness/parity tail | see `docs/handoff/wave9-complete.md`; assignment Stop, resource Type/MaxUnits, error paths, large-programme perf harness | `mpp_reader_tests`, `mpp_error_paths`, `mpp_perf_large`, `io_*` |
| W41 | engine tail | ALAP resolved as a stored-word misread (not an engine bug); plan_recalc now compares the ALAP row against `+96/+100` and pins the two pairs as different; ALAP slack words 0/0 with TotalSlack LF-EF | schedin 20/20 + 10/10; M2 332/332 unchanged; golden TSV untouched |
| W42 | views breadth | task-table registry, filters/groups/multi-key sorts, Gantt zoom + labels | `view_model_tests`, `gantt_tests` (counts in the marker) |

### The writer gap is closed, and the manual mode bit is measured

`save_date_edits` writes a real SNET constraint at FixedData `+64/+66` **and
clears Fixed2Meta entry byte `+8` bit `0x80`**. Three measured outcomes, all
coordinator opens (`scripts/oracle_task_fields.py`):

| fixture | changed | `Manual` after open | con/date | Start |
|---|---|---|---|---|
| `base.mpp` | — | True | 0 / NA | 2026-01-04 13:00 |
| `w37-snet-save.mpp` | `+64`/`+66` only | True | 4 / 2026-10-21 08:00 | **unchanged 2026-01-04 13:00** |
| `w37-snet-auto.mpp` | same + mode bit | **False** | 4 / 2026-10-21 08:00 | **2026-10-21 08:00** |

So: SNET pins an auto-scheduled row; on a manual row it is stored and ignored;
SNET + the bit cleared is honoured and the row becomes auto-scheduled. Manual
scheduling is **not** preserved — the API warns per converted row, and that is
the documented product consequence (`spec/mpp-writer.md` §10.8). The honoured
fixture is the API's own output, byte-identical to the file that was opened.

### Two corrections this wave

1. **`app::date32_from_packed` was one day late.** File day 0 = 1983-12-31 and
   date32 day 1 = 1984-01-01 with `weekday_of_day(d) = (d-1)%7`, so file day D
   and date32 day D are the same date. The `+1`/`-1` pair is gone; pinned by the
   two spec anchors (15346, 15636) and PROJECT_A's Monday (15633).
2. **ALAP's "divergence" was our comparison, not the engine.** On an ALAP row
   Project's displayed Start/Finish are the calculated `+96/+100`, while
   `+104/+108` holds the early dates — the two pairs are not interchangeable
   there. W31's "start/finish are the effective pair" holds for ordinary rows and
   fails for ALAP; `core/mpp` needs that caveat (W41 reported it, W40 owns the
   file).

### Open after wave 9 (ranked)

1. **Exact-date writes on a linked row are unmeasured.** SNET is "no earlier
   than", so an FS successor should reopen at `max(pred finish, pin)`. One
   fixture (`w37-snet-link.mpp`) settles it; inference is flagged in `save.h`.
2. **Recurring calendar exceptions** are decoded but not applied; the COM object
   model does expose `Project.Calendar.Exceptions` + `Years`/`WorkWeeks`, so the
   five `w9-r-*` fixtures are scriptable once `Exceptions.Add`'s argument order
   is introspected.
3. **Elapsed lag**: `corpus/probe/w9-elapsedlag.mpp` exists; decode `LagFormat`
   and say whether "2 edays" was stored as elapsed or converted to working days.
   (T2/T3/T4 land on the same date — the dates alone do not discriminate.)
4. Per-task calendars exposed but not scheduled (fine for PROJECT_A/PROJECT_B).
5. PDF layout has no visual oracle; the >260-day month-unit Gantt path is
   untested; non-WinAnsi text is a counted `?`.
6. PROJECT_B's two dead-endpoint links are named exclusions.
7. App: only **start dates** persist; Name/Finish/Duration are view-only. No
   clipboard, row insert/delete, column resize, IME, zoom or printing.

### Traps added this wave

`call` is cmd syntax (use `cmd /c '"...vcvars64.bat" >nul && cmake ...'`);
`git push` can need wider access (`SEC_E_NO_CREDENTIALS` under the sandbox);
`git status --cached` does not exist; Dropbox mtime churn makes `edit` fail
seconds after a read; one lane's `CMakeLists.txt` naming an unwritten file
breaks every lane's configure (retry, never edit their file); `ctest -R cal`
over-matches; `plan_recalc_tests` rewrites the golden whenever corpus is present;
`app/` is a standalone project whose ObjC++ half only compiles on macOS.

### Next three actions

1. `docs/handoff/wave9-complete.md` is the successor's gate (do not delete it).
   It names the final commit, the CI run ids and the counters.
2. Build and open `w37-snet-link.mpp` — closes open item 1.
3. Introspect `Calendar.Exceptions.Add` and build the five `w9-r-*` fixtures
   (also oracle agenda item 4 in the wave-10 board).
