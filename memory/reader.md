# Reader stream — memory

_Updated 2026-10-22 (W31). Successor: read this, then `docs/handoff/reader.md`
(W6, still accurate for the Task table and the four-table geometry) and the
header comments in `core/mpp/`. The record spec is `spec/mpp-records.md` (W1);
W6, W18, W19, W24, W30, W31 and W33 each corrected claims in it — see "Spec
corrections" at the end. Oracle files: `docs/analysis/oracle/` (w3-o1.json,
outline-probe.json, milestone-probe.json, schedin-probe.json,
holiday-probe.json, manual-start-edit.json, writer-base.json,
assign-probe.json) and the coordinator's MSPDI exports
`corpus/probe/project_a-export.xml`, `corpus/probe/project_b-export.xml` (the PROJECT_B one
is new in W31; only its link uids are committed, as
`docs/analysis/oracle/project_b_export_links.json`)._

## W31 deliverable on disk (all committed paths are `core/mpp/`,
`tools/reader_dump/`, `tests/`, `memory/reader.md`; no git run from this stream)

| path | what |
|---|---|
| `core/mpp/reader.h` | `Task::start_anchor` (+174), `Task::calendar_uid` (+178), `Task::milestone` / `milestone_stored` (stored FixedMeta flag, duration==0 fallback), `Task::calculated_start`/`calculated_finish` (+96/+100) with `start`/`finish` now the **effective** +104/+108 pair (W33), `Task::free_slack_ticks` (+74) / `start_slack_ticks` (+186) / `total_slack_ticks` (+190, W30), `Assignment::start`/`finish`/`stop` (+52/+56/+60), `Link::endpoints_live()`, `Document::link_count_with_live_endpoints()` / `_with_dead_endpoints()`, `kDateNa`. |
| `core/mpp/reader.cpp` | reads all of the above; `critical` now uses TotalSlack (+190); milestone from FixedMeta entry byte +10 bit 0x02; link liveness stays FixedMeta coverage and endpoint liveness is separate; assignment dates. |
| `tools/reader_dump/main.cpp` | new `--anchor` (start_anchor, calendar_uid) and `--slack` (free/start/total slack ticks) columns; `--links` now prints a `# links N (live L, dead-endpoint D)` line; `--assignments` now prints start/finish/stop. Default 8-column task output untouched. |
| `tests/mpp_reader_tests.cpp` | **581 checks** with corpus (was 489), **239** with `MPP_NO_CORPUS=1` (was 207). New `test_w31_fixed_fields`; W31/W30/W33 assertions in `test_project_a`, `test_project_b`, `test_status_fixture`, `test_outline_fixture`, `test_links`, `test_assignments`, `test_synthetic_fixture`. |
| `memory/reader.md` | this file. |

## W31 measured facts (every one re-runnable; offsets are into the 202-byte row)

### Effective vs calculated dates (W33)

| off | field | evidence |
|---|---|---|
| 96 / 100 | **calculated** Start/Finish | go stale after a manual Start edit or in a not-fully-calculated file. Exposed as `calculated_start`/`calculated_finish`. |
| 104 / 108 | **effective** Start/Finish = `Task::start`/`finish` | what Project's UI and MSPDI export report. PROJECT_B uid 10805 is the only corpus row where the pairs differ: +96/+100 = 2026-10-15 17:00 (stale), +104/+108 = 2026-03-23 08:00 = the export. `manual-start-edited.mpp` uid 1: +96 = 2026-01-05 08:00 stale, +104 = +174 = 2026-01-12 08:00 = COM after the edit; uid 2/3 +104 = 2026-01-04 13:00 = COM (`manual-start-edit.json` before/after). PROJECT_A (83), lagff1 (6), w3-o1 (4), milestone1 (7), holiday1 (3), link1 (4), assign1 (2) have **zero** rows where the pairs differ, so the PROJECT_A golden stays 83/83. Project row 0 keeps +96/+100, equal to its +104/+108 in every corpus file. |

Consequence: `tools/mspdi_cmp` on PROJECT_B is now **MATCH** — 730 field
comparisons (10/task), 0 mismatches, 0 milestone notes, links 70/70,
assignments 63/63, resources 22/22; the 2 dead-endpoint links and 10
dead-endpoint assignments are excluded by name. PROJECT_A stays MATCH (830 fields,
88/88 links, 67/67 assignments). Before W31/W30/W33 PROJECT_B had 2 date
differences and milestone notes.

### Three slack words (W30; W18 had the first wrong)

| off | field | evidence |
|---|---|---|
| 74 | **FreeSlack** (i32, 6-second ticks) | `milestone1.mpp` uid 6 stores 0 = COM FreeSlack 0; `outline1.mpp` summary stores 0 = COM FreeSlack 0. |
| 186 | **StartSlack** | `milestone1` uid 6 = 220800 ticks = COM StartSlack 22080 min; `outline1` summary = 19200 = COM StartSlack 1920 min. |
| 190 | **TotalSlack** (`Task::total_slack_ticks`) | `milestone1` uid 6 = 220800 = COM TotalSlack 22080; w3-o1/lagff1/milestone1-other rows equal in all three; schedin's ALAP task stores 52800 = the engine's old 5280-min "divergence". |

`critical` = `!manually_scheduled && total_slack_ticks (+190) <= 0`. W18/W19
fixtures reproduce COM on all rows (base 0/3, w3-o1 1/4, outline1 2/3).

Plan bridge consequence (measured with `tools/recalc`, read-only, on
`corpus/probe/project_a-recalc.mpp --compare`): start 83/83, finish 83/83, slack
**82/83**, critical 83/83 = **331/332** (was 266/332). The one remaining slack
mismatch is uid 10851 (ours 2400, stored 39360, link family). 45 link-family +
5 milestone mismatches became 1. `tests/plan_recalc_tests.cpp` still pins
92/5 and 73/69/34/52 and rewrites `tests/data/project_a_recalc_golden.tsv`; that is
the **plan stream's** file and must be re-pinned (83/83/82/83). Do not run it
from this stream.

### Start anchor +174 (`Task::start_anchor`, packed date, 0xffffffff = none)

The bound a task's Start is aligned *from*, not the scheduled Start. Manual
task: the manual Start. Auto task: usually the driving predecessor's finish
(hence 17:00 the previous workday). Evidence:

* PROJECT_A uid 10759 stores 2026-08-10 17:00 (its predecessor's finish) while Start
  is 2026-08-11 08:00; uid 10742 stores its own 17:00 (Start == Finish).
* link1.mpp T2/T3: 2026-10-19 17:00 (T1's finish), Start next morning.
* holiday1.mpp T2: 2026-10-20 17:00 (T1's finish), Start 2026-10-22 08:00 (the
  21st is the task calendar's holiday). T1/T3 store 0xffffffff.
* manual-start-edited.mpp uid 1: 2026-01-12 08:00 = COM's edited Start; base's
  three manual tasks store the date-only manual start 2026-01-04 13:00 (UTC).
* schedin: only P50 (uid 9) stores one, equal to its own Start.
* w3-o1/schedin/lagff1/assign1 auto tasks mostly store 0xffffffff.

Not fed into `Task::start`.

### Task calendar +178 (`Task::calendar_uid`, i32, -1 = none)

holiday1.mpp's three live tasks store 3; `holiday-probe.json` records SiteCal
as the task calendar with uid 3 (project default Standard). Every PROJECT_A (83) and
PROJECT_B (73) task row stores -1, so the project calendar is correct for them.
`core/cal` can consume this later.

### Stored Milestone flag (FixedMeta entry byte +10, bit 0x02)

`Milestone` is **not** derived from duration. `corpus/probe/project_b-export.xml`
(Project's MSPDI export of PROJECT_B, new in W31) says Milestone=1 for uid 10735
(**10 days**) and 10799 (**1 day**), plus 8 zero-day rows. The bit is set on
exactly those 10 rows (10/10) with 0 false positives on PROJECT_B's 63 other live
rows. `milestone1.mpp` sets it on exactly COM's five milestones (uids
2,3,4,5,7 per `milestone-probe.json`); w3-o1 sets it only on T4. A row the
FixedMeta stream does not cover falls back to `duration_ticks == 0`;
`milestone_stored` says which path was used. PROJECT_A: 10 milestones, all stored,
all 0-day. PROJECT_B: 10 milestones, 2 with non-zero duration.

### Assignment dates +52/+56/+60

| off | field | evidence |
|---|---|---|
| 52 | Start | equals the task's **effective** +104 on all 67 live PROJECT_A assignments, all 63 PROJECT_B assignments whose task is a live task, 4/4 link1 and 3/3 real assign1 rows. base's 3 unassigned rows store the project row's date-only start. |
| 56 | Finish | same files against +108 (67/67, 63/63, 4/4, 3/3). |
| 60 | Stop (INFERENCE) | inside [Start, Finish]: equals Start on the 51 zero-percent PROJECT_A assignments, Finish on all 13 at 100 %, and strictly between on the 3 partial PROJECT_A rows (10 % / 40 % / 40 %). PROJECT_B's 63 resolved ones are all 0 % so all store Start. No oracle export carries assignment dates, hence inference. |

PROJECT_B's assignment table is stale like its links: 73 live rows but 10 name
deleted task uids (8141, 8142, 10637, 10722..10727, 10732), so only 63 resolve
to a live task. `mspdi_cmp` excludes those 10 by name.

### Link liveness: two levels

(a) **row-live** = a FixedMeta entry's +4 offset names the row (W24 rule:
PROJECT_A 99 rows / 91 entries / 88 distinct covered rows). (b) **endpoint-live** =
both endpoint uids name a live task (`Link::predecessor_known/successor_known`,
`Link::endpoints_live()`). PROJECT_B: 72 row-live links, **70 endpoint-live**, 2
dead-endpoint (10722 -> 10723 and 10723 -> 10724; those uids exist only in its
deleted task slots at rows 6..8). Project's export has 70
`<PredecessorLink>` elements, so Project does not schedule them. PROJECT_A 88/88,
link1 5/5. `Document::link_count_with_live_endpoints()` /
`link_count_with_dead_endpoints()` give the split and reader_dump prints it.
Do not change the PROJECT_A 88 or link1 5 counts.

### PROJECT_B uid 10805 = stored-vs-recalculated case (not a reader bug)

Being not fully calculated, PROJECT_B's row 10805 has stale calculated +96/+100 =
2026-10-15 17:00, while effective +104/+108 = 2026-03-23 08:00 = the export and
+174 = the same 03-23 08:00. The reader returns the effective pair (W33) and
keeps the stale pair separately. `mspdi_cmp` PROJECT_B MATCH is the proof.

## W24 (still current): PROJECT_A link liveness geometry

`TBkndCons/FixedData` = 1980 B = 99 x 20 (no header); `FixedMeta` = 926 B = 16
+ 91 x 10, header declares 88 (stale). Entries 0..87 name 88 distinct rows;
entries 88..90 repeat rows 98/93/94. 11 rows have no entry (6, 9, 33, 38, 59,
60, 63, 68, 85, 86, 89): two all-zero (85/86), nine stale non-zero. A row is a
link only when an entry's +4 offset names it. 10846's real predecessors are
10759/10849/10877; 10859 <- 10858 lag 0. PROJECT_A's links are the exact 88-element
multiset of `corpus/probe/project_a-export.xml` (committed as
`tests/data/project_a_links_golden.tsv`, uids/type/lag only).

## W19 (still current): scheduling inputs

| off | type | field | evidence |
|---|---|---|---|
| 64 | u16 | ConstraintType (PjConstraint 0..7) | schedin rows 1..10 read 0..7 plus 4/0; w3-o1 matches COM 0/4/4/4. |
| 66 | u32 | ConstraintDate (packed, unaligned) | schedin `3d1812c0`; w3-o1 = COM's three dates. |
| 182 | u32 | Deadline (packed, unaligned) | schedin `3d1327d8`; w3-o4-last T2 `3d1227d8`; NOT VarMeta 0x0b4001b5 (no corpus file has it). |
| 80 | u32 | ActualDuration (ticks) | var-pct 0 -> 12000 at 50 %; schedin P50 4800. |
| 120 | u32 | ActualStart — identity unproven | schedin P50 = its start; `schedin-probe.json` reports null. |
| 124 | u32 | ActualFinish — identity unproven | set on all 38 corpus rows at 100 %, equal to stored Finish; none below 100 %. |

## W18 (corrected): status and outline

| off | field | evidence |
|---|---|---|
| 74 | FreeSlack (was mislabelled TotalSlack) | see W30 above |
| 186 | StartSlack | see W30 above |
| 190 | TotalSlack | see W30 above |

Fixed2Data (64-byte record, `64*(rows+3)`; Fixed2Meta `16+96*(rows+3)`, entry i
pairs with FixedMeta entry i, never by row index): +0 row identity (not the
uid), +16 sequence double (not a level), +24 parent row identity, +42 bit 0x40
= has children. Fixed2Meta entry +8 bit 0x80 = manually scheduled (INFERENCE:
base/resaved-base set it on exactly COM's Manual=true rows, w3-o1/outline1 clear
it, PROJECT_B sets it on 3 of 86 rows). `critical` = `!manual && total_slack
(+190) <= 0`; the manual gate reproduces base's Task A (slack 0, Critical
false). `summary` = child_count > 0; the stored 0x40 flag agrees on every row of
base/outline1/w3-o1/PROJECT_A(17)/PROJECT_B(13) and a disagreement is warned.
`outline_level` = depth of the stored parent chain (project row 0, children 1);
`outline_number` = sibling-index path over **live** rows only (PROJECT_B's 12
deleted rows would otherwise start the live outline at "6").

## Counts (reader_dump / tests, measured 2026-10-22 W31)

PROJECT_A 83 tasks / 22 resources / 88 links (88 endpoint-live) / 67 assignments;
PROJECT_B 73 / 22 / **72 (70 endpoint-live)** / 73 (63 resolve to a live task);
link1 4 / 0 / 5 / 4; assign1 2 / 2 / 0 / 3. PROJECT_A golden 83/83, zero field
mismatches. PROJECT_B mspdi_cmp MATCH; PROJECT_A mspdi_cmp MATCH.

## Verified commands (from the repo root)

```
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w31 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w31 --config Release --target mpp_reader_tests"   # never --parallel
build\vs-w31\core\mpp\Release\mpp_reader_tests.exe        -> PASS: 581 checks
$env:MPP_NO_CORPUS='1'; build\vs-w31\core\mpp\Release\mpp_reader_tests.exe -> PASS: 239 checks
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build\vs-w31 -C Release -E plan_recalc
  -> 24/24 passed (plan_recalc excluded: it is the plan stream's and rewrites its golden)
# reader_dump is NOT in the top-level build:
cmake -S tools\reader_dump -B build\vs-w31\reader-dump -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\vs-w31\reader-dump --config Release
build\vs-w31\reader-dump\Release\reader_dump.exe --anchor --slack --outline corpus\probe\holiday1.mpp
build\vs-w31\tools\mspdi_cmp\Release\mspdi_cmp.exe corpus\project_b.mpp corpus\probe\project_b-export.xml  -> MATCH
build\vs-w31\tools\mspdi_cmp\Release\mspdi_cmp.exe corpus\project_a.mpp corpus\probe\project_a-export.xml -> MATCH
build\vs-w31\tools\recalc\Release\recalc.exe corpus\probe\project_a-recalc.mpp --compare corpus\probe\project_a-recalc.mpp
  -> start 83/83 finish 83/83 slack 82/83 critical 83/83 (331/332)
```

Results 2026-10-22 (W31): configure OK after the one retry that Dropbox/AV
sometimes requires; MSVC `/W4 /permissive- /utf-8 /WX` build 0 warnings;
full tree rebuilt (24 ctest targets), 24/24 passed with `-E plan_recalc`.

## Still open

1. **Plan re-pin (plan stream, not this one).** `tests/plan_recalc_tests.cpp`
   pins 92 links / 5 skipped and 73/69/34/52 slack; the reader now gives 88
   links / 0 skipped and 83/83/82/83. It also rewrites
   `tests/data/project_a_recalc_golden.tsv`. Re-run and re-pin there; do not run it
   from this stream.
2. **Assignment `stop` (+60) is an inference**: no oracle export carries
   assignment dates. A COM export of `Assignment.Start/Finish/Stop` (or one
   fixture with a 50 %-complete assignment) would prove it.
3. **ActualStart/ActualFinish (+120/+124) identity**: pattern-consistent, no
   COM export; `schedin-probe.json` reports ActualStart null for P50.
4. **Manual flag** is still inferred from Fixed2Meta +8 bit 0x80; one fixture
   flipping `Task.Manual` on mixed-mode PROJECT_B would prove it.
5. **The Milestone bit is pinned on PROJECT_B/milestone1/w3-o1** but the exact
   bit position inside the 16-byte FixedMeta blob is only demonstrated by
   correlation (plus 0 false positives). A save that toggles Milestone alone
   would make it a byte-diff proof.
6. **OutlineLevel COM export** was only ever obtained for outline1; the
   derivation has not been measured on PROJECT_A's 83 rows.
7. Type names 0 (FF) and 2 (SF) are still inference (1 FS and 3 SS are
   oracle-confirmed); link1 is the only FF/SF evidence.
8. Resource Type (Work/Material/Cost) and MaxUnits identity are unchanged from
   W10; no fixture varies them.
9. No corpus file lacks `TBkndTask/FixedMeta`, so the "milestone flag absent →
   duration fallback" path is unexercised (the no-Fixed2Data flat-outline path
   is still unexercised too).

## Traps (do not relearn)

* **Effective vs calculated dates**: `start`/`finish` are +104/+108; +96/+100
  is the calculated copy. PROJECT_B 10805 and the manual-start fixtures are the
  only differing rows. Do not "fix" 10805 back to +96/+100.
* **Slack is three fields**: +74 FreeSlack, +186 StartSlack, +190 TotalSlack.
  W18's "+74 is TotalSlack" was wrong. `critical` uses +190.
* **Milestone is stored**: FixedMeta entry byte +10 bit 0x02, fallback
  duration==0. Do not revert to duration-only (PROJECT_B 10735/10799 would flip).
* **Link liveness is two different questions**: row-live (FixedMeta coverage)
  vs endpoint-live (both uids are live tasks). PROJECT_B is 72/70/2; do not drop
  the 2 dead-endpoint links and do not call PROJECT_A 88 wrong.
* **FixedMeta/Fixed2Meta pair by entry index**, never by row; PROJECT_A has no entry
  for row 67 and stale header counts.
* **Deleted rows keep Fixed2Data records and parent links**; number live rows
  only or PROJECT_B's outline starts at "6".
* **Fixed2Data +16 is a sequence, not a level**; +24 is the parent identity.
* **Project row dates are UTC-encoded only when the project start is a
  date-only property**; task rows store local wall clock. Do not "fix"
  `format_date` to add an offset.
* **PROJECT_A's link FixedMeta is not one entry per row**; always take the row from
  the entry's +4 offset.
* **`reader_dump` is not in the top-level build**; configure `tools\reader_dump`
  in its own tree (`build\vs-w31\reader-dump`).
* **`plan_recalc_tests` rewrites `tests/data/project_a_recalc_golden.tsv`** whenever
  corpus is present. Do not run it from this stream.
* **Dropbox rewrites file mtimes in this checkout**, so the editor's
  "file changed since read" guard trips; re-read before editing (or pipe a
  one-off Python probe to `python -` instead of writing a script).
* `+64/+66/+182/+174/+178` are deliberately unaligned reads (`u16_at`/`u32_at`);
  do not "align" them.
* `corpus/probe/project_a_golden_full.tsv` is not in reader_dump's row order; compare
  sorted row sets, not bytes.
* VarMeta header counts can be stale (use stream length); never index FixedMeta
  by row; the toolchain is off PATH; never `--parallel`; a `C1083 Permission
  denied` from a concurrent build is fixed by re-running the identical build;
  never let a test throw out of `main` on MSVC. clang/GCC reject unused
  file-scope constants (`kConsFixedMetaEntrySize` is load-bearing; every
  constant added in W31/W30 is used).

## Spec corrections this stream established (the coordinator owns spec/)

1. §2: `+74` is **FreeSlack**, `+186` **StartSlack**, `+190` **TotalSlack**
   (W30; W18's +74-TotalSlack claim is withdrawn).
2. §2: `+96/+100` are the **calculated** Start/Finish; **`+104/+108` are the
   effective pair** Project reports (W33). §3's UTC discussion is unaffected.
3. §7: Fixed2Data +16 is a **row sequence**, +24 the **parent identity**, +42
   bit 0x40 **has children**; OutlineLevel is derived from that chain.
4. §2/§10 list `Summary`, `Milestone` and `Critical` as unlocated: `Summary`
   and `Critical` are derived, but **Milestone IS stored** — FixedMeta entry
   byte +10 bit 0x02 (W31).
5. **W19 row layout**: ConstraintType +64 (u16), ConstraintDate +66 (u32),
   ActualDuration +80, ActualStart +120, ActualFinish +124, Deadline +182.
   `spec/scheduling.md` §3.6's `+0x84`/`+0x78` are in-memory TASKNODE offsets.
6. Deadline is NOT VarMeta `0x0b4001b5`; that key occurs in no corpus file.
7. **§10.1 link liveness** is FixedMeta coverage, and it has a second level:
   endpoint liveness (PROJECT_B 72/70/2, Project exports 70).
8. **§10.2 assignment dates**: +52 Start, +56 Finish, +60 a within-span date
   (Project's Stop; inference) — measured against PROJECT_B/PROJECT_A/link1/assign1.
9. **W31: Task row +174 is the start anchor** (manual Start / pre-alignment
   predecessor bound) and **+178 is the task-calendar uid** (-1 = none).
