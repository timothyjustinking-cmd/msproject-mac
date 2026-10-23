# Reader stream — memory

_Updated 2026-10-22 (W40). Successor: read this, then `docs/handoff/reader.md`
(W6, still accurate for the Task table and the four-table geometry) and the
header comments in `core/mpp/`. The record spec is `spec/mpp-records.md` (W1);
W6, W18, W19, W24, W30, W31, W33, W37 and W40 each corrected claims in it — see
"Spec corrections" at the end. Oracle files: `docs/analysis/oracle/` (w3-o1.json,
outline-probe.json, milestone-probe.json, schedin-probe.json,
holiday-probe.json, manual-start-edit.json, writer-base.json,
assign-probe.json, task-fields-w37-snet-auto.json) and the coordinator's MSPDI
exports `corpus/probe/project_a-export.xml`, `corpus/probe/project_b-export.xml`,
`corpus/probe/assign1.xml` (new in W40; only the PROJECT_A assignment dates are
committed, as `tests/mpp_assignment_dates_golden.tsv`)._


## W31 deliverable on disk (all committed paths are `core/mpp/`,
`tools/reader_dump/`, `tests/`, `memory/reader.md`; no git run from this stream)

| path | what |
|---|---|
| `core/mpp/reader.h` | `Task::start_anchor` (+174), `Task::calendar_uid` (+178), `Task::milestone` / `milestone_stored` (stored FixedMeta flag, duration==0 fallback), `Task::calculated_start`/`calculated_finish` (+96/+100) with `start`/`finish` now the **effective** +104/+108 pair (W33), `Task::free_slack_ticks` (+74) / `start_slack_ticks` (+186) / `total_slack_ticks` (+190, W30), `Assignment::start`/`finish`/**`resume`** (+52/+56/+60, W40 corrected the old `stop` name), `Resource::type` / `type_stored` / `max_units()` / `max_units_fraction()`, `Link::endpoints_live()`, `Document::link_count_with_live_endpoints()` / `_with_dead_endpoints()`, `kDateNa`. |
| `core/mpp/reader.cpp` | reads all of the above; `critical` now uses TotalSlack (+190); milestone from FixedMeta entry byte +10 bit 0x02; link liveness stays FixedMeta coverage and endpoint liveness is separate; assignment dates (+60 = **Resume**, measured, not Stop); duplicate-uid and dead-endpoint warnings. |
| `tools/reader_dump/main.cpp` | new `--anchor` (start_anchor, calendar_uid) and `--slack` (free/start/total slack ticks) columns; `--links` now prints a `# links N (live L, dead-endpoint D)` line; `--assignments` now prints start/finish/**resume**. Default 8-column task output untouched. |
| `tests/mpp_reader_tests.cpp` | **663 checks** with corpus, **316** with `MPP_NO_CORPUS=1` (W40: assignment-date golden, manual-bit fixture, warning checks). |
| `tests/mpp_error_paths.cpp` (new) | **28 checks**: truncated/not-CFB/missing table/lying size word/4-billion count/duplicate uid/dead refs/empty streams + 256-byte mutation loop. |
| `tests/mpp_perf_large.cpp` (new) | 4,000-task / 6-level synthetic programme; reader parse, CPM/recalc, RecordSet, sheet, Gantt timings (see "Performance"). |
| `tests/mpp_fixture_builder.h` (new) | in-memory CFB writer used by the two W40 tests. |
| `tests/mpp_assignment_dates_golden.tsv` (new) | Project's PROJECT_A assignment dates (uid, task uid, Start/Finish/Stop/Resume), uids/dates only. |
| `memory/reader.md` | this file. |

## W40 measured facts (2026-10-22)

### Assignment +60 is Resume, NOT Stop (corrects the W31 inference)

Project's PROJECT_A MSPDI export carries **21 non-empty `<Resume>` elements**, and the
reader's +60 equals one on **21/21** rows. It is not Stop:

| uid | +60 | export Stop | export Resume |
|---|---|---|---|
| 9821 | 2026-05-11 08:00 | 2026-05-08 17:00 | 2026-05-11 08:00 |
| 9823 | 2026-04-15 08:00 | 2026-04-14 17:00 | 2026-04-15 08:00 |
| 9729 | 2026-08-05 11:12 | 2026-08-05 11:12 | 2026-08-05 11:12 |

On the other 19 exported rows Stop == Resume, which is why the old reading looked
right; on the two split rows Stop is the previous working instant (Resume minus a
calendar step). The export has neither element on the remaining 46 assignments,
where +60 equals Start on 0 % rows and Finish on 100 % rows (Project's default
resume). **Stop is not stored** in the assignment row; deriving it needs the
calendar, so the reader does not invent it. `Assignment::resume` is the field;
`tools/mspdi_cmp` compares it and reports `resume on 21` for PROJECT_A, 0 for PROJECT_B /
assign1 / lagff1. Evidence command:
`build\vs-w40\core\io\Release\io_mspdi_crosscheck.exe` and the committed golden.

### Resource Type and MaxUnits

* **MaxUnits** is the double at +8, x100. All 46 resources in PROJECT_A/PROJECT_B plus the
  assign1 pair are 10000.0 -> 100 %, and all three exports write
  `<MaxUnits>1.00</MaxUnits>`; `mspdi_cmp` compares `max_units()` (percent) on
  22+22+2 resources, 0 mismatches. No fixture varies it, so the **scale** (+8 is
  x100) remains inference; the value and the export agreement are measured.
* **Resource Type is NOT located.** All 46 exported resources are `<Type>1</Type>`
  (Work); no corpus fixture has a material/cost resource and no row word or
  VarMeta key correlates with an alternative value (`Resource::type` is Work and
  `type_stored == false`). `mspdi_cmp` compares it (1 == Work) on 22+22+2 and
  reports 0 mismatches, but that cannot distinguish "stored Work" from
  "default". **Needs an oracle fixture**: a material and a cost resource, plus
  one Work resource at MaxUnits 50 %, saved to .mpp + MSPDI export.

### Manual-mode bit is measured (W37 oracle)

`corpus/probe/w37-snet-auto.mpp` = base.mpp with Fixed2Meta entry 4 byte +8
`0xff -> 0x7f` (bit 0x80 cleared) and SNET 2026-10-21 08:00 on row 1's +64/+66:
Project opens it with uid 1 **Manual=False**, Start 2026-10-21 08:00, Finish
2026-10-27 17:00, ConstraintType=4; uid 2/3 stay Manual=True. The same edit
without clearing the bit (`w37-snet-save.mpp`) opens Manual=True with Start
unchanged. `mpp_reader_tests` checks base's 3 rows true and w37-snet-auto row 1
false (guarded; the fixture is git-ignored).


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
| 60 | **Resume** (MEASURED W40) | 21/21 PROJECT_A assignments with a non-empty export `<Resume>` match it; the old "Stop" reading is withdrawn (two split rows have Stop one working interval earlier). Where the export has no Resume (46 rows) it equals Start on 0 % rows and Finish on 100 % rows. Stop is not stored. |

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

## Counts (measured 2026-10-22 W40)

PROJECT_A 83 tasks / 22 resources / 88 links (88 endpoint-live) / 67 assignments;
PROJECT_B 73 / 22 / **72 (70 endpoint-live)** / 73 (63 resolve to a live task);
link1 4 / 0 / 5 / 4; assign1 2 / 2 / 0 / 3. PROJECT_A golden 83/83, zero field
mismatches. `mspdi_cmp`: PROJECT_A MATCH (830 fields, 88/88 links, 67/67 assignments,
resources 22/22, type+max units 22/22, assignment Start/Finish 67/67, Resume
21/21), PROJECT_B MATCH (730 fields, 70/70 links, 63/63 assignments, resources
22/22), assign1 MATCH (20 fields, 3/3 assignments, 2/2 resources).
`mpp_reader_tests` 663 with corpus / 316 with `MPP_NO_CORPUS=1`;
`mpp_error_paths` 28; `io_mspdi_crosscheck` 127; `io_error_paths` 21;
`io_mspdi_tests` 145; `io_mspdi_writer_tests` 864.

## Performance (W40 `mpp_perf_large`, Release, MSVC 19.44)

4,000 tasks in a 6-level outline, 732 links, 200 resources, 200 assignments:

| phase | ms (one run, 2026-10-22) |
|---|---|
| reader parse (`mpp::load_bytes` on a synthetic 4,000-row CFB) | 3.0–4.3 |
| CPM/recalc (`core::plan::from_mpp` + `recalculate`) | 3.8–5.0 |
| RecordSet build (`core::io::to_record_set`) | 1.9–2.2 |
| sheet build + visible-row walk + column layout | 0.08–0.17 |
| Gantt build (`build_gantt`) | 1.8–2.7 |
| **total** | **11–15 ms** |

The test asserts a 10 s ceiling per phase and in total, so a slow CI runner
cannot flake; the numbers above are the regression signal.

## Verified commands (from the repo root, W40)

```
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w40 -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w40 --config Release --target mpp_reader_tests mpp_error_paths mpp_perf_large io_mspdi_crosscheck"
build\vs-w40\core\mpp\Release\mpp_reader_tests.exe        -> PASS: 663 checks (PROJECT_A golden 83/83)
$env:MPP_NO_CORPUS='1'; build\vs-w40\core\mpp\Release\mpp_reader_tests.exe -> PASS: 316 checks
build\vs-w40\core\mpp\Release\mpp_error_paths.exe         -> PASS: 28 checks
build\vs-w40\core\mpp\Release\mpp_perf_large.exe          -> PASS, total ~11-15 ms
build\vs-w40\core\io\Release\io_mspdi_crosscheck.exe      -> 127 checks
cmd /c "call `"$vc`" >nul && ctest --test-dir build\vs-w40 -C Release --output-on-failure -R `"mpp|io_`""
  -> 15/15 passed (includes the 7 mppwrite tests built from W37's tree)
build\vs-w40\core\plan\Release\plan_recalc_tests.exe      -> 185 checks; M2 83/83/83/83 = 332/332;
  tests/data/project_a_recalc_golden.tsv SHA256 unchanged (DB81218E...C46A0A)
build\vs-w40\tools\mspdi_cmp\Release\mspdi_cmp.exe corpus\probe\assign1.mpp corpus\probe\assign1.xml -> MATCH
```

## Still open

1. **Resource Type is unlocated and MaxUnits' scale is unproven.** All fixtures
   are Work/100 %. Oracle ask: one .mpp + MSPDI export with a material resource,
   a cost resource, and one Work resource at MaxUnits 50 % (also settles whether
   +8 is x100).
2. **MPP12**: no MPP12 fixture exists on disk (all corpus/probe files are
   MPP14). Unmeasurable here; the reader's container/config path is MPP14-only.
3. **ActualStart/ActualFinish (+120/+124) identity**: pattern-consistent, no
   COM export; `schedin-probe.json` reports ActualStart null for P50.
4. **The Milestone bit** is pinned on PROJECT_B/milestone1/w3-o1 but the exact bit
   position inside the 16-byte FixedMeta blob is demonstrated by correlation
   (plus 0 false positives). A save that toggles Milestone alone would make it a
   byte-diff proof.
5. **OutlineLevel COM export** was only ever obtained for outline1; the
   derivation has not been measured on PROJECT_A's 83 rows.
6. Type names 0 (FF) and 2 (SF) are still inference (1 FS and 3 SS are
   oracle-confirmed); link1 is the only FF/SF evidence.
7. No corpus file lacks `TBkndTask/FixedMeta`, so the "milestone flag absent →
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
* **`+60` is Resume, not Stop** (W40): 21/21 PROJECT_A exported `<Resume>` values
  match it; the two split rows have Stop one working interval earlier. Stop is
  not stored — do not add it from `+60` without a calendar.
* **Warnings are part of the contract**: duplicate uids, dead link/assignment
  endpoints and stale metadata counts are non-fatal and reported; only a
  truncated container, a missing Task table, an empty Task FixedData and a size
  word larger than the file throw `mpp::Error`. `mpp_error_paths` pins both
  classes, including a 256-byte mutation loop.
* **Never trust a size/count word without a bound**: `cfb.cpp` bounds DIFAT/FAT
  sector counts and directory chains by the file's sector count, and the
  FixtureBuilder writes a chain for the bytes actually present even when the
  directory claims 4 GiB (the reader must reject the claim, not allocate).
* **`mpp_perf_large` is a top-level-only target** (it links core/plan, core/sheet,
  core/gantt, which are added after core/mpp); a standalone core/mpp configure
  omits it. The fixture builder is `tests/mpp_fixture_builder.h`.
* **The PROJECT_A assignment-date golden** is
  `tests/mpp_assignment_dates_golden.tsv` (67 uids, no names); it is committed,
  so CI can prove Resume without `corpus/`.
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
8. **§10.2 assignment dates**: +52 Start, +56 Finish, **+60 Resume** (W40:
   measured against Project's export, 21/21; W31's "Stop" label is withdrawn).
   Stop is not stored in the row; it is the previous working instant before
   Resume.
9. **W31: Task row +174 is the start anchor** (manual Start / pre-alignment
   predecessor bound) and **+178 is the task-calendar uid** (-1 = none).
10. **Fixed2Meta entry byte +8 bit 0x80 is the manual-mode bit** (W40, measured
    with w37-snet-auto/w37-snet-save; the W18/W31 "inference" wording is
    withdrawn). Resource Type remains unlocated and MaxUnits is still only
    value-proven — see "Still open".

## W49 addendum (wave 11, 2026-10-22 18:55) — Resource Type, task DurationFormat, link LagFormat

Owned files touched: `core/mpp/reader.h`, `core/mpp/reader.cpp`,
`tests/mpp_reader_tests.cpp`, this file.  `core/mpp/field_value.{h,cpp}` and W48's
`Task::custom_fields` were left alone.  No client name is used below (uids and
counts only).

### Resource Type is per-row METADATA, not a byte in the 172-byte row (MEASURED)

`corpus/probe/w9-resourcetypes.mpp` is the fixture W40 asked for: COM built R1
Work with MaxUnits 50 %, R2 Material with a MaterialLabel, R3 Cost, then saved
the .mpp and an MSPDI export.  Decoding the metadata words gives all three:

| condition | type |
|---|---|
| `TBkndRsc/FixedMeta` entry (37-byte rows) byte **+12 bit 0x10** set | Work |
| that bit clear and `TBkndRsc/Fixed2Meta` entry byte **+8 bit 0x10** set | Cost |
| both clear | Material |

The two streams pair by ENTRY INDEX (the Fixed2Meta entry's +4 is a Fixed2Data
record offset, not the FixedMeta row offset), exactly like the Task outline.
Measured results: w9-resourcetypes = Work / Material / Cost; PROJECT_A 22/22 and
PROJECT_B 22/22 = Work, matching their MSPDI exports.  `Resource::type` uses MS
Project's COM PjResourceType order (Work=0, Material=1, Cost=2);
`Resource::type_stored` is true when the metadata supplied the value.
`material_label` is VarMeta key **0x0c40012b** (R2 = the synthetic label
"metre").  W49 also added `is_material()` / `is_cost()`.

**MaxUnits scale is now measured, not inference**: R1 is COM MaxUnits 50 % and
the +8 double holds **5000.0**, so +8 is x100 (10000.0 = 100 %), not x10000.
`max_units_percent()` returns 50 for that row.

[I]/open: the Project-2010 MPP14 variant puts the Work bit at FixedMeta entry
+9 bit 0x02 (mpxj MPP14ResourceReader); no file on disk is that variant, so the
reader implements the 2013+ rule only.  MSPDI's `<Type>` is a DIFFERENT enum
(1 = Work, 0 = Material, cost marked by `IsCostResource`); do not compare it
numerically with `ResourceType`.  `tools/mspdi_cmp` still only understands
Work/Type=1 and is therefore not used on w9-resourcetypes.

### Task DurationFormat is task row +164 (0xa4) (MEASURED)

`Task::duration_format` is the u16 at row offset **+0xa4**, PjFormatUnit
numbering: 7 = days, 8 = elapsed days, 9 = weeks, 11 = months, 21 = none, the
even values 4..12 elapsed, and 35..44 the estimated variants.
`format_unit_is_elapsed()` and `Task::duration_is_elapsed()` expose the elapsed
rule.

Evidence: every task row's +a4 (the project row included) equals its `<DurationFormat>` in Project's
own MSPDI export — **84 PROJECT_A + 74 PROJECT_B + 4 w9-resourcetypes rows, 0
mismatches** (the count includes the project row).  In
`corpus/probe/w9-elapsedlag.mpp`, T8 ("2 edays") stores 8 and its 28800 ticks
while T1..T7 store 7.

### Link LagFormat is link row +18 (W41 measured; W49 exposed)

`Link::lag_units` was already decoded.  W49 added `Link::lag_is_elapsed()` and
`core/plan` now maps it through that method instead of a local duplicate.
MEASURED on w9-elapsedlag: 3 links store 8 ("2 edays"/"3 edays"), 2 store 7
("2 days"); see `recalc --links` (the `elapsed` column).

### Counts / commands (W49)

```
mpp_reader_tests         685 checks with corpus (was 663), 324 with MPP_NO_CORPUS=1
PROJECT_A 83 tasks / PROJECT_B 73 tasks / resource counts unchanged
ctest -R "mpp|cal|sched|plan"   23/23 passed
build\vs-w49\tools\recalc\Release\recalc.exe corpus\probe\w9-elapsedlag.mpp --fields
  -> uid 8: dur_ticks 28800, dur_fmt 8; uid 1..7: dur_fmt 7
```

### Link/assignment liveness now honours the W45/W48 tombstone (MEASURED, W49)

Before W49, link and assignment liveness was FixedMeta **coverage** only.  W48's
delete cascade keeps the dependent FixedData bytes and only sets the entry's
+0 state byte to 0x02, so the reader returned rows Project has deleted:

| table | before | after (coverage AND state +0 == 0) | Project export |
|---|---|---|---|
| PROJECT_B links | 72 (70 live endpoints, 2 dead) | **70, all 70 live, 2 skipped** | 70 |
| PROJECT_B assignments | 73 (63 resolving to a live task) | **63, all 63 resolve, 10 skipped** | 63 |
| PROJECT_A links | 88 | **88** | 88 |
| PROJECT_A assignments | 67 | **67** | 67 |
| W48 `w48-del-cascade.mpp` | 2 links (0 live), 3 assignments (2 live-task) | **0 links, 2 assignments (both live-task), 2+2 skipped** | Project delete cascade |

The predicate is exactly the Task one: `covered && (state == kStateNoMetaStream
|| state == 0)`.  A missing FixedMeta stream keeps the old all-live assumption
(state == kStateNoMetaStream).  Tombstoned link rows and non-placeholder
tombstoned assignment rows are counted in `skipped_links` /
`skipped_assignments` with reason "FixedMeta entry is a tombstone (+0 = 0x02)";
a tombstoned assignment placeholder (task uid 0) is skipped silently, as before.
The table warning says how many rows were tombstoned.

This SUPERSEDES the W31/W40 counts above: PROJECT_B is 70 links (0 dead-endpoint)
and 63 assignments, not 72/70/2 and 73.  PROJECT_A is unchanged.  The W48 fixture path
is `build\vs-w48\core\mppwrite\test-out\w48-del-cascade.mpp` (build output, not
committed); W48's `tests/roundtrip_add_delete.cpp` owns its own expectations.
The reader test pins the PROJECT_B/PROJECT_A counts; `mpp_reader_tests` is 685 checks
after this change.

## G1 / wave 18 addendum (2026-10-23) — MPP12 (Project 2007) is supported

Owned files touched: `core/mpp/reader.h`, `core/mpp/reader.cpp`,
`core/mpp/CMakeLists.txt`, `tests/mpp_reader_tests.cpp`, this file,
`docs/handoff/g1.md`.  Full byte tables and the fixture inventory are in
`docs/handoff/g1.md`; the same tables are in reader.cpp's "MPP12" comment.
`mpp_reader_tests` is now **958 checks with corpus / 324 with
`MPP_NO_CORPUS=1`** (was 683/324).  `ctest -E plan_recalc` is 49/50: the one
red test, `engine_diff_apply`, is another lane's (mppwrite add_delete tombstones
the placeholder while tests/engine_diff_tests.cpp still expects 4 assignments);
it is not a reader failure — see docs/handoff/g1.md §5.

### Format detection (do not hard-code 114/)

`mpp::MppVersion { mpp12, mpp14 }` in `Document::version`.  `detect_format()`
reads the top-level props stream name (`Props12` vs `Props14`), falls back to
the storage that actually holds `TBkndTask/FixedData` (`112/` vs `114/`), warns
on a disagreement and lets the storage win.  `Document::table_root` is
`"112"`/`"114"`, `Document::view_root` `"212"`/`"214"`, `Document::version_string`
is the JSON-ish writer version (`"12,0,4017,1004"`; `mpp12resource` is
`"12,0,6423,1000"`; Project 16 is `"16,0,20430,20182"`).  Every table helper
takes the root; nothing in reader.cpp assumes 114 any more.

### MPP12 geometry (measured; fixture names in docs/handoff/g1.md)

| table | MPP12 | MPP14 |
|---|---|---|
| Task | 48 + 264·rows, FixedMeta 16+47·(rows+3) | 48 + 202 |
| Rsc | 48 + 188 | 48 + 172 |
| Cons | 20·rows | same |
| Assn | 142·rows | 110·rows |
| Cal | 64 + 12·rows | same |
| Task Fixed2 | 176·rows / 16+86·entries | 64 / 16+96 |
| Rsc Fixed2Meta | 16+49·(rows+3) | 16+51·(rows+3) |

VarMeta (24-byte header, 12-byte (uid, offset, key)) and Var2Data blocks are
UNCHANGED.  Task rows are read BY FixedMeta ENTRY OFFSET, not by stride:
MPP12 interleaves 16-byte null-task blocks (`mpp12nulltasks` offsets
`48,312,576,592,856,1120,1136,1400,1416`).  A task row must be 264 B and its
FixedMeta +0 state 0x00; 16 B blocks are skipped as null tasks, 0x02 is a
tombstone, any other size is refused with a warning naming the row.

### MPP12 task row (264 B) — the offsets are NOT MPP14's

uid **+0**, ID **+4** (REVERSED; proved by uid 2/ID 1 in mpp12resource,
mpp12viewstate uid 5/ID 1), duration +60, duration format **+64** (PjFormatUnit
+ 0x20 estimated; proved by mpp12duration's ten units), actual duration +66,
start/finish +88/+92 (there is NO calculated pair; +96/+100 are actuals, so
`calculated_*` is set equal and `start_anchor` stays kDateNa), constraint
+80/+112, priority +120, percent +122, calendar uid +160, deadline +164,
parent uid **+36**, OutlineLevel **+40** (STORED).  Free/Start/Finish slack at
+24/+28/+32; +32 is MPXJ's FinishSlack and is exposed as `total_slack`
[inference].  Project row = data row 0, same field offsets as a task.

### MPP12 metadata bits

* Milestone = FixedMeta entry byte **+8 bit 0x20** (MPP14: +10/0x02).  Proof:
  mpp12task Task #1 (+8=0x63) is a milestone per MPXJ's MppTaskTest despite a
  5-day duration.
* Flag1..Flag20 = a contiguous bit field from entry byte **+37 bit 0x20**
  through byte +40 bit 0x01.  All twenty are MEASURED on
  `task-flags-project2007-mpp12.mpp` (tasks named "Flag1".."Flag20", one bit
  each) and the reader returns all twenty for MPP12; MPP14 still returns Flag1
  only.
* Row state +0 is the same 0x00/0x02 tombstone; descriptors and null tasks are
  0x04.  There is no manual-scheduling bit (Project 2007 has no manual mode).

### MPP12 resources, links, assignments

* Resource: uid +0, ID +4, MaxUnits f64 **+44** (x100).  Type: FixedMeta
  **+9 bit 0x02** = Work; else Fixed2Meta **+8 bit 0x10** = Cost, else Material.
  Measured on `resource-type-project2007-mpp12.mpp` (5 Cost / 5 Material /
  5 Work, self-labelled).
* Link: type +12, **LagFormat u16 +14, lag i32 +16** — MPP14 has these two
  SWAPPED (lag +14, format +18).  Proof: mpp12relations' SS+1d row stores
  +14=7 and +16=4800, and MPXJ's test asserts the four relation types/1-day lag.
* Assignment: 142 B; uid +0, task +4, resource +8, Start +12, Finish +16,
  Units f64 +54, Work f64 +62.  No Resume/Stop field exists ⇒
  `Assignment::resume` stays kDateNa.

### Calendars, subprojects, password

* Calendar rows: MPP12 is (calendar uid, base uid, resource uid) at +0/+4/+8;
  MPP14 is (base, resource, calendar) — MPXJ's MPP12CalendarFactory confirms.
  `Document::calendars` (new, both formats) exposes uid/base/resource/name.
  **core/cal still hard-codes 114/ and the MPP14 column order** — it must take
  `Document::table_root` + `Document::version`.  Do not change core/cal from
  this lane; it is flagged in docs/handoff/g1.md §7.
* Subproject: Task VarMeta key **0x0b4001ca** = UniqueID offset (mpp12subproject
  uid 2 = 0x00800000, uid 3 = 0x00c00000; MPXJ asserts 8388608).  The source
  path is in `112/Props` key **0x024000a2** (SUBPROJECT_DATA), parsed per
  reader.cpp §MASTER/SUBPROJECT; only item type 0x01/0x08 is decoded, other
  types are warned and stop the parse.  The master carries the summary row
  only; the subproject's tasks are in the other file.
* Password: Props12/Props14 key **0x35400000** is a one-byte flag (0x01 read
  password, 0x02 write reservation, 0x03 both), 0x35400004/5 are hash records,
  **0x35400007** is a one-byte XOR mask.  All 12 fixtures: flag 0, no hashes,
  code 0.  Reader: 0x02 reads + reports read-only; 0x01/0x03 refused; a
  nonzero mask with any flag refused as encryption.  Tested by patching the
  byte in memory through `CfbDocument` (no committed .mpp, no COM).

### Container round-trip

`tests/mpp_reader_tests.cpp` now also compiles `core/mppwrite/cfb_writer.cpp`
(no library link, no add_subdirectory ordering) and `test_mpp12_container`
proves `CfbDocument` re-emits all five fixtures **byte-identically** and the
reader still loads each as MPP12.  `.mpt` is proved to be the same container:
a byte copy of `mpp12task.mpp` named `.mpt` (written under
`build/g1/test-out/`) decodes identically.

### What remains open for MPP12

1. No COM oracle (oracle is coordinator-only): slack word names, the stored
   project dates, and the password flag semantics rest on MPXJ + the bytes.
2. `core/cal` must be taught the table root + MPP12 calendar column order.
3. Baseline fields, timephased data, recurring-task parameters and the `212/`
   view state stay opaque (same scope as MPP14's baselines/`214/`).
4. Subproject read-only flag has no located byte; other subproject item layouts
   are refused.
5. Optional extra fixtures: a MPP12 file with a write-reservation password and
   one with a read password (none exists on disk).



