# tests/data — committed test data

Everything in this directory is committed and must stay reproducible from a clean
checkout. Nothing here is client data.

| file | what it is |
|---|---|
| `synthetic-base.mpp` | **synthetic** MPP14 fixture, 237,568 bytes, 3 tasks + project row. Only synthetic `.mpp` files may be committed. |
| `project_a_golden.tsv` | MS Project's own COM export of the PROJECT_A corpus file (83 live tasks), used as the oracle. See `project_a_golden.md`. |
| `project_a_golden.md` | how the golden TSV was produced and what its columns mean |
| `wave23-outline.mpp` | **synthetic** MPP14 fixture built by Project 2021 for wave 23: 3-level outline, FS+2 d / SS+1 d / FF links, one resource. See its provenance below. |
| `wave23-flags.mpp` | the same fixture after Project set Task.Flag10 (0x0b400051) True on ID 3; the measured byte for the SUMMARY column (W23-6). Re-derived under `C:/w28` by `scripts/oracle_w28_privacy.py` in W28-0.1 (the wave-23 save embedded the operator's local path); Task FixedData is byte-identical to `wave23-outline.mpp` and Task FixedMeta differs only at entry byte +25 bit 0x04. SHA256 `72ACC208…3E802A`. |
| `wave24-summary-off.mpp` | synthetic-base saved by Project 2021 with the project-summary row OFF (214/Props 0x03400009 = 0); wave 24 W24-2. |
| `wave24-summary-on.mpp` | the same file with the row ON (0x03400009 = 1); the only difference from the off save is that record and per-save handles. |
| `wave24-timeline-hidden.mpp` | synthetic-base saved with the Timeline strip hidden (214/Props 0x03400008 empty). |
| `wave24-timeline-shown.mpp` | the SAME session's shown save of the same project (0x03400008 = "Time&line"); the pair is what reproduces the "only the Timeline setting differs semantically" claim (W25-10).  `synthetic-base.mpp` is a different session and cannot be the partner. |
| `wave24-order.mpp` | FileNew project Alpha..Delta, then a task inserted above row 1 and saved; stored order differs from ID order (W24-5). |
| `wave24-flags20.mpp` | wave23-outline.mpp with Task.Flag1..Flag20 all True on ID 3 (wave 24 W24-15); the generic flag decode's fixture. |
| `wave24-flag2.mpp` | the same measurement's single-bit control: only Flag2 True on ID 3 (byte +24 bit 0x04). |
| `wave24-flag20.mpp` | only Flag20 True on ID 3 (byte +34 bit 0x04), pinning the non-contiguous id/bit position. |
| `wave25-auto.mpp` | **synthetic** `wave23-outline.mpp` with every task switched to automatic scheduling (`Manual = False`) and saved by Project 2021 with neutral metadata; the parity reference that distinguishes an auto task bar from a manual one (W25-2). |
| `wave25-flag11.mpp` … `wave25-flag17.mpp` | seven single-bit controls, one per flag in the +33 group: wave23-outline.mpp with every flag False on ID 3, then ONLY that flag True, saved through Project 2021 by the committed `scripts/measure_flags.py` (W25-9).  They pin the bit order the all-20 fixture cannot. |
| `wave27-percent-actuals.mpp` | **synthetic** synthetic-base with Task ID 1 set to 50 % PercentComplete by Project 2021, saved with neutral metadata by `scripts/oracle_w27.py`, re-derived under `C:/w28` by `scripts/oracle_w28_privacy.py` in W28-0.1.  Project's own % edit writes the actuals, dates, progress flag, project-row roll-up and Var2Data field 0x0B40015E as well as FixedData +92 (W27-0.1 evidence); also the project-with-actuals fixture the Move Project gate opens. |
| `wave27-priority-control.mpp` / `wave27-priority-750.mpp` | the SAME Project 2021 session's save of synthetic-base before and after the project summary task's Priority was set to 750, with neutral metadata (`scripts/oracle_w27.py`; re-derived under `C:/w28` by `scripts/oracle_w28_privacy.py` in W28-0.1).  Task FixedData differs in exactly the project row's +78 word (500 -> 750); the other stream differences are per-save bookkeeping.  The 750 file is the field-by-field oracle the port's priority save is compared with (W27-0.3). |
| `wave27-unlink-control.mpp` / `wave27-unlink.mpp` | the SAME Project 2021 session's save of synthetic `wave23-outline.mpp` before and after Task > Unlink Tasks on the one task with BOTH a predecessor and a successor (uid 4: 3 -> 4 FS+2 d and 4 -> 6 FF), with neutral metadata (`scripts/oracle_w27_unlink.py`; re-derived under `C:/w28` by `scripts/oracle_w28_privacy.py` in W28-0.1).  Project removes BOTH links touching the task: TaskDependencies 3 -> 1 (the unrelated 3 -> 5 SS+1 d survives, and the count is still 1 after reopen).  The link table's FixedData is untouched; FixedMeta tombstones entries 0 and 2 and sets header flags 4 -> 6.  The pair is the oracle the port's Unlink save is compared with (W27-3.3). |
| `wave28-percent-auto0-control.mpp` / `wave28-percent-auto40.mpp` | the SAME Project 2021 session's save before and after setting Task ID 1 `PercentComplete = 40` on an auto-scheduled leaf, with neutral metadata (`scripts/oracle_w28_actuals.py`, W28-1.1).  Task FixedData gains the actual/remaining durations, actual start, resume/stop dates, the +168 progress bit and the +174 start anchor; the project row gains the rolled-up actual/remaining duration and percent; Task Var2Data gains 0x0B40015E and drops the constant 0x0b4004e5 block; TBkndAssn gains the actual-work step block.  The edited file is the field-by-field oracle the port's actuals writer is compared with (W28-1.2). |
| `wave28-percent-auto40-control.mpp` / `wave28-percent-auto100.mpp` | the same continuous session's next save pair: 40 -> 100 %.  Task row 1 reaches ActualDuration = Duration, RemainingDuration = 0, PercentComplete = PercentWorkComplete = 100 and ActualFinish = finish; the project row rolls up to 50 %; 0x0B40015E gains the 100 % step. |
| `wave28-percent-auto100-control.mpp` / `wave28-percent-auto0.mpp` | the same session's 100 -> 0 % save pair.  Every actual word returns to zero/NA, the +174 anchor clears, the 0x0B40015E entry is removed and the constant 0x0b4004e5 block returns.  The +168 progress bits are NOT cleared: the row keeps 0x9080 (W28-1.1 measured it; Project only ever ORs 0x9000 into that word). |
| `wave28-percent-manual0-control.mpp` / `wave28-percent-manual40.mpp` | a fresh synthetic-base copy's manual row saved at 0 % then 40 % in the same session (`scripts/oracle_w28_actuals.py`, W28-1.1).  Same actuals as the auto pair except the +174 anchor is not written (the manual row already stores it); the project roll-up uses the project's 2160 min duration (20 % -> 432 min actual). |
| `wave28-mark-auto-control.mpp` / `wave28-mark-auto.mpp` | the SAME Project 2021 session's save of a synthetic-base copy whose Task ID 1 was switched `Manual = False` and whose status date is 2026-01-07 00:00, before and after the ribbon's Mark on Track on row 1 (`scripts/oracle_w28_update.py`, W28-1.3; the button is invoked through UI Automation because the type library exposes no Mark on Track member).  Only row 1 moves: 0 -> 40 % (ActualDuration 960, RemainingDuration 1440, ActualStart 2026-01-05 08:00); the project row rolls up 0 -> 20 % (480 min).  Rows 2-3 are untouched.  The edited file is the field-by-field oracle for the port's Mark on Track save. |
| `wave28-mark-manual-control.mpp` / `wave28-mark-manual.mpp` | the same measurement on the fixture's own manual row 2, with the status date set in the same session: Mark on Track moves it 0 -> 83 % (ActualDuration 1200, RemainingDuration 240, ActualStart 2026-01-04 13:00 -- the 240 min start-day tail plus Mon+Tue); the project row rolls up 0 -> 25 % (540 min).  Row 1 stays 0 %. |
| `wave28-update-control.mpp` / `wave28-update.mpp` | a fresh synthetic-base copy saved before and after `Application.UpdateProject(All=True, UpdateDate=2026-01-07 00:00, pj0to100Percent)` -- Update Project's "0% - 100% complete" through that date, entire project (`scripts/oracle_w28_update.py`, W28-1.3).  All three leaf rows move: ID 1 (manual) 50 % (ActualDuration 1200), ID 2 83 %, ID 3 100 % with ActualFinish 2026-01-06 12:00; the project row rolls up to 70 % (1512 min) and the stored status date (114/Props 0x02400045) becomes 2026-01-07, where the control stores NA.  The edited file is the Update Project oracle; the port's Update Project save must write both the actuals and the status date. |
| `wave28-outline-indent-control.mpp` / `wave28-outline-indent.mpp` | the SAME Project 2021 session's save before and after `Tasks(2).OutlineIndent()` on a synthetic-base copy with neutral metadata (`scripts/oracle_w28_outline_edit.py`, W28-2.1): one top-level leaf (ID 2) moved under its preceding sibling (ID 1), which becomes a summary.  Measured: the new summary's FixedData +84/+96/+100 become the children's working-time span/effective dates, +140 (has children) 0 -> 1, +164 7 -> 0x15, +174 (start anchor) cleared, and its Fixed2Data flag becomes 0x40 with its +46 start word cleared; the moved row's +142 (has parent) 0 -> 1 and +172 (outline level) 1 -> 2, and its Fixed2Data parent identity becomes ID 1's row identity.  Task Var2Data caches the old leaf duration in 0x0B40007C, drops 0x0b4004e5/0x505/0x506 and adds 0x0B4004E4 (max descendant ID).  Project also tombstones the new summary's assignment row (TBkndAssn), which the port does not write (see W28-2.1 notes). |
| `wave28-outline-summary-control.mpp` / `wave28-outline-summary.mpp` | the same measurement's second shape, in one continuous session: ID 3 was first indented under ID 2 (so ID 2 is already a summary), then ID 2 with its child was indented under ID 1, moving a whole subtree one level down (`scripts/oracle_w28_outline_edit.py`, W28-2.1).  The moved summary keeps its own roll-up values; its child's +172 gains one and its FixedData is otherwise unchanged; ID 1 gains the summary duration/date roll-up over ID 2's effective dates. |
| `wave28-outline-outdent-control.mpp` / `wave28-outline-outdent.mpp` | the same measurement's inverse: the indent shape (ID 2 under ID 1) saved, then `Tasks(2).OutlineOutdent()` back to the top level, so ID 1 stops being a summary (`scripts/oracle_w28_outline_edit.py`, W28-2.1).  The exact inverse of the indent rules, plus the restored leaf duration (from 0x0B40007C), the start anchor, and Project re-creating the assignment row it tombstoned on indent. |
| `wave28-newtask-manual-control.mpp` / `wave28-newtask-auto.mpp` | the SAME Project 2021 session's save of a synthetic-base copy before and after Options > Schedule > "New tasks created" was changed from Manually Scheduled to Auto Scheduled (`scripts/oracle_w28_new_task_mode.py`, W28-3.1), with neutral metadata.  The option is `114/Props` record `0x024013c8` (kind 2, 2-byte payload; `Project.NewTasksCreatedAsManual`); Project normalises the fixture's stored `0x0001` and writes `0x00FF` for manual and `0x0000` for auto.  The edited file is the field-by-field oracle for the port's new-task-mode writer. |
| `wave28-newtask-auto-control.mpp` / `wave28-newtask-manual.mpp` | the same measurement in the other direction: a fresh synthetic-base copy saved with Auto Scheduled, then the option changed back to Manually Scheduled and saved (`scripts/oracle_w28_new_task_mode.py`, W28-3.1).  The pair proves the byte change is the option alone in both directions.  Project's COM reopen of every file reads the saved mode back. |
| `wave28-split-control.mpp` / `wave28-split.mpp` | the SAME Project 2021 session's save of a synthetic-base copy whose Task ID 1 was switched `Manual = False`, before and after ONE split written through `Task.SplitParts(1).Finish` + `SplitParts.Add` (`scripts/oracle_w28_split.py`, W28-5.1; `Application.SplitTask` is interactive and wedges a COM caller).  The control is 5 days, 2026-01-05 08:00 -> 2026-01-09 17:00, one part; the edited file keeps 5 days (24000 ticks) and stores two parts, Mon 08:00 -> Wed 17:00 and Fri 08:00 -> Mon 2026-01-12 17:00.  Measured: the split is an ASSIGNMENT structure -- the assignment's `0x0F400031` planned-work contour block grows from 100 to 128 bytes (first u32 `0x00200000` -> `0x00200003`, its 28-byte cumulative-work record list 2 -> 3) and its VarMeta entry moves 8 -> 396 (`TBkndAssn/Var2Data` 396 -> 528), and the assignment finish moves +3 days -- plus task row 1's +100/+108/+116 finish dates (+3 days) and +168 bit 0x2000, the project row's Duration/RemainingDuration 24000 -> 28800 ticks and its finish, and rows 2-3's slack/late dates.  No `TBkndTask` variable structure changes: `SplitParts` is not a stored task field.  The pair is the evidence for the W28-5.1 deferral (the port's writer has only the flat assignment contour and never writes `TBkndAssn` work contours). |
| `synthetic-calendars.mpp` | synthetic two-calendar fixture (wave 19, `scripts/build_calendar_fixture.py`).  In W28-0.1 its two embedded save-path strings were replaced in place with a neutral same-length string (`scripts/w28_neutralise_path.py`), and then the operator name in its SummaryInformation properties and one Var2Data value was replaced the same way (`scripts/w28_neutralise_operator.py`), because the engine suite's calendar oracle depends on every other byte. |
| `synthetic-gantt-style.bin` / `synthetic-gantt-fonts.bin` | the wave-26 Gantt-style decoder's first fixtures: one Gantt view's `0x22400008` record and its `214/Props` font table, extracted from a Project save of `synthetic-base.mpp`. |
| `wave26-gantt-saves/` | the wave-26 one-change-per-save evidence for the Gantt style decoder: 14 applied-view `0x22400008` records, one Project save per style change, plus the two `214/Props` font tables the font saves produced. Extracted by wave 27 W27-2.1; provenance below. |

## `wave23-outline.mpp` — provenance (wave 23)

- **Built by the coordinator through COM automation of MS Project Professional
  2021 (16.0.20430.20182)** on **2026-10-23**, under `scripts/oracle_lock.py`,
  by `scripts/oracle_w23.py`. The file was written to `build/w23/` and copied
  here only after it loaded and the reader showed the intended shape.
- Six synthetic tasks: `Summary A` (level 1), `Sub A1` (2), `Leaf A1a` and
  `Leaf A1b` (3), `Sub A2` (2), `Final Task` (1); links `Leaf A1a -> Leaf A1b`
  FS+2 days, `Leaf A1a -> Sub A2` SS+1 day, `Leaf A1b -> Final Task` FF; resource
  `Resource A` assigned to `Final Task`. No client content.
- **SHA256 055721FF07E93F8C8C06B8428E9EF3495D10A38BE8C432506FCB5FF3A786DA20**,
  pinned in `scripts/check_fixtures.py`. It is the CI fixture for wave 23's
  ID-order/outline gate (`qt_task_order`), the Depends On/Who gate
  (`qt_stored_table`) and the parity reference work.
- Regenerate with `python scripts/oracle_w23.py` (Project must be closed); the
  script writes to `build/w23/`, so copy the file here and update the hash only
  after a deliberate review of the reader dump.

## `wave26-gantt-saves/` — provenance (wave 27, W27-2.1)

The wave-26 Gantt-text-style, bar-style and timescale decoders were verified
against controlled saves that lived only under the git-ignored `build/w26/g3/`.
Wave 27 committed the decoded evidence instead of the `.mpp` files: each
`*.style.bin` is the applied view's `0x22400008` record — the record containing
the text-style, bar-style and timescale fields — and each `*.fonts.bin` is the
`214/Props` `0x03400000` FONT_BASES payload.  A record carries no path, author,
project title or task text; a printable-string scan finds only Project's stock
bar-style names and font families.  The source `.mpp` files stay local.

Every save was made by Project 2021 COM automation on a **fresh copy of
`synthetic-base.mpp`** (`SHA256 55C109F2…B3FC6069`), through the local scripts
`build/w26/g3/oracle_styles.py`, `oracle_probe.py` and `oracle_probe3.py`; each
save changed one thing.  `control.style.bin` is the first session's unedited
save; its font table is byte-identical to `synthetic-gantt-fonts.bin`, so it is
not duplicated here.  Every save except the two font saves has a FONT_BASES
payload byte-identical to that same table.

| stem | source save (local, under `build/w26/g3/`) | source `.mpp` SHA-256 | decoded change vs control |
|---|---|---|---|
| `control` | `mpp/control-a.mpp` | `8F9FA0D330112DA092CE93FCF17D645C8094DF584B457D5130FA930240D2CCC1` | none (baseline) |
| `arial` | `mpp/t-all-arial.mpp` | `27EDC9858764F2268932F3F899768439ED34004A806C258FC453F55DF0722356` | all 17 text items' `font` → Arial |
| `size8` | `mpp2/t-all-size8.mpp` | `9DEFEBD839AD359E2B91CB7706210CDBB4D14B708FF6B46B38BE6C68305EC263` | all 17 `font` → Calibri 8, plus the bold/foreground resets the explicit-argument call made |
| `critical-blue` | `mpp4/t3-critical-blue.mpp` | `BF3C52B20B96FA32A9EFC9BBACE1E8CC6B8B3D48A278AAECB65905A9F38535F3` | `critical.foreground` automatic → (0,0,255) |
| `milestone-italic` | `mpp4/t3-milestone-italic.mpp` | `1080B1EE88E27072301715F9E9688A453D214E5E438AECC086AC1C579F478FE5` | `external.italic` on; `external.foreground` explicit grey → automatic |
| `milestone-bold` | `mpp4/t3-bold-test.mpp` | `E85EF239280C31309428433821BEDA2ECDA1CEC2D4DA29457DF092E25E99CC52` | `external.bold` on; `external.foreground` explicit grey → automatic |
| `milestone-underline` | `mpp4/t3-underline-test.mpp` | `89DD21710904E1F8A436221037D0B8955F02796C9C448A8E8067E0CA6AF2F0AF` | `external.underline` on; `external.foreground` explicit grey → automatic |
| `highlight-fill` | `mpp4/t3-highlight-fill.mpp` | `95899A823093CEA0E72F53A8782ACCAE77287D11CA61FE3066C6B219B78DA4E3` | `noncritical.background` automatic → (255,255,0) |
| `task-red` | `mpp3/b2-task-red-int.mpp` | `67E9B4E6CBCA99025B042FCDF7CF5F44272D0F9EBCB35FD6EE0C367B85EE8708` | `bar[0].Task`: middle colour → (255,0,0), start/end explicit black → automatic, id 0 → 40 |
| `summary-green` | `mpp4/b3-summary-green.mpp` | `4C1237FCBB9C7516B56E67031C39B4793F644F2A5C62FF13D098439F95DBAC27` | `bar[3].Summary`: all three colours → (0,128,0), id 3 → 40 |
| `milestone-blue` | `mpp4/b3-milestone-blue.mpp` | `E904000F1CEA48149B05F352B2F3E983247F0AE40E4B2DD01F729F0CC95BFF7B` | `bar[2].Milestone`: start colour → (0,0,255), middle/end explicit black → automatic, id 2 → 40 |
| `ts-yqm` | `mpp2/ts-yqm.mpp` | `3738033E58F21F162FDF0AB0413F07791C564AE80ED34EA21B48A2A9F39D94E9` | middle weeks/13 → quarters/2; bottom days/20 → months/10 |
| `ts-ymd` | `mpp2/ts-ymd.mpp` | `D051EEBB8F46F48C84B9D0A3611847D961B1A604A2337012AE2D0757B8870D9D` | middle weeks/13 → months/9 (bottom unchanged) |
| `ts-yhq` | `mpp2/ts-yhq.mpp` | `0E4903349FAD233AE3278A343E5105DF67864DA3D2216FBE593F0D817AA13E10` | middle weeks/13 → half_years/123; bottom days/20 → quarters/5 |

Errata recorded by wave 26 and visible in these records: Project's
`TextStyles` `Item` index is not the slot index — `Item = 3` edited the
**external** slot (hence the `milestone-*` names) and `Item = 7` the
**noncritical** slot (hence `highlight-fill`).  The eight digit groups of a
timescale format word are not the displayed format string; only the units,
counts, alignment and tier flags are asserted.

Saves from the same session that carry identical record bytes were committed
once: `control-a`/`control-b`, `t3-milestone-italic`/`-int`,
`b2-task-red-int`/`-name`, `ts-ymd`/`ts-2tier`; the size8 and Arial saves share
a style record (only the font table differs).  Failed or no-op COM calls
(`b2-task-red-explicit`, `ts3-toplabels`, the wave-23 timescale variants) are
not committed.  `ts3-count2` has distinct bytes but decodes to the same tiers as
`ts-yqm`, so it adds no field evidence.

## Every tracked file and its SHA-256 (wave 23, A-8/A-7d)

`scripts/check_fixtures.py` pins the six files whose content is quoted in
docs or used as an oracle reference; this table is the complete inventory so a
silent substitution is visible even for the small binaries.  These are the
hashes updated at the wave-25 close (branch `wave25`; the wave-23 originals for
the files wave 25 did not touch); regenerate the table in the
same commit as any deliberate change -- never edit a fixture to make a test
pass.  `README.md` cannot carry its own hash (it changes when this table does).

| file | SHA-256 |
|---|---|
| `project_a_golden.md` | `03BA7AB54910BB8A766E69326A0C61ED9F14C18DDD5295C1EF1326B570A01DEC` |
| `project_a_golden.tsv` | `A1B57D7E48E86B3A0536562521ED9962F34D3BDD2DE768FA86BEBD86830769B6` |
| `project_a_links_golden.tsv` | `8C38C4F09CEC09028DA61BBBFA24EA1FD47D29452CE77D3E5AD2E9828B60FAD6` |
| `project_a_preds_golden.sha256` | `BF9A9E184830AD66E2C510F4EF91DC5CBA213EAD1024A2C326CA1F8827F7D820` |
| `project_a_recalc_golden.tsv` | `36F156F618C94C92D70FBA6D754340D374DDE72E7D2FF136D1E22DBEE5400DB8` |
| `reference/README.md` | `65364F2B553D0D80CAC7459409FA88E04E01B21CF1CF3552C69C350ECFC51ED4` |
| `reference/wave23-outline-project.measures.json` | `3CA8261885BD930F234149C8C65894B892F797A89FAE0DFB249FEA85271D60E0` |
| `scenarios/assignment-50pct.oracle.json` | `84AD1FD9897AC4C0E750B4C47A95E7654980D7BE97340DA15C038FE9DDFF6607` |
| `scenarios/constraint-mso.oracle.json` | `1EE253E931F90A8C27694A46D24C518E090397BFD1FAAAB7A2DB13F8B39DBBE0` |
| `scenarios/constraint-snet.oracle.json` | `0DD549FB8A60D0A873B022BEF46C15A46F5156C1DE4F87AF7B2D61295486A85D` |
| `scenarios/delete-task-with-successor.oracle.json` | `55E910F499DF66B9E729CA17F1318DB1D117D97E87B8FF6AA4E47547B16119C6` |
| `scenarios/duration-change.oracle.json` | `93588E74E2C970227CCE7D25AD2F4FC7D3C42408791EBBD43786B5DCA0C9C5EA` |
| `scenarios/link-fs-lag.oracle.json` | `95F837781F314AC657F2045D810BFFA1BFED18DF54BC0B5FCB2F205FED76C9DC` |
| `scenarios/recalculate.oracle.json` | `457CE73B3895BA1A1FDF9A2F4F163EE9D6C504BAD6485D6E158DE69FA2384F9B` |
| `scenarios/task-calendar.oracle.json` | `855A7EED5E0A2F5C1DDFA9B8195216462B75017F8EDB45CB3987F901A90D58BE` |
| `synthetic-base.mpp` | `55C109F2F62FD6B9F05789AB002C35991A424AAB18C78F92E4200DF7B3FC6069` |
| `synthetic-calendars.mpp` | `BAF83A50C19A82F834CD7CCBF833E53C2DB9130C9627F2EAB6EBE44F3C5D68AE` |
| `synthetic-gantt-fonts.bin` | `BEC0B057A37AE959A123BBC28C052366C6134DCC7DF6C5DE308FA897C568F478` |
| `synthetic-gantt-style.bin` | `B9FD3240CCB0EBD7B94AF17824C1FE104F44BC860AD4DCE093AF7BFD75B23CF4` |
| `synthetic-props.bin` | `8739CDB83D96210ED3874AD14055F02C88B97F9C5FF74C343AD04D548B8E0334` |
| `synthetic-viewstate-ctable-base.bin` | `80573060BBACC24B656F9A6FF70BF907A421BEE0677C2F99A8F7D3AB5993F4B9` |
| `synthetic-viewstate-ctable-col.bin` | `7B753DFE1C930CF95C6C2E3D75A821505A2B6F43C35FB42C30979D824E5FF52E` |
| `synthetic-viewstate-ctable-date.bin` | `E106E86452C37F4EA73263F043AE5EF1280BEEF46999C62271933960705EB3CB` |
| `wave23-outline.mpp` | `055721FF07E93F8C8C06B8428E9EF3495D10A38BE8C432506FCB5FF3A786DA20` |
| `wave23-flags.mpp` | `72ACC2086FA9A97781F96EBBD243D4C2CF91E7902BEE9FE19D8139303E3E802A` |
| `wave24-order.mpp` | `515537D2B86415F85A3D15FB320C3F5A0EBB7A0951FF43CBCD00DC6B2BF6DDAF` |
| `wave24-summary-off.mpp` | `0EF4DE69CC9C2CF6D3C6568C86B9A8B708DE6E8CCC8D404BC4FBE82AE4A6C391` |
| `wave24-summary-on.mpp` | `90BF1055E1FF415E453208C04123C2A2F869E38D950237DF116D59D2E53C7C05` |
| `wave24-timeline-hidden.mpp` | `9BE6D2E9343149F58DE7800F3F840B158AC90C4F0403423BF8167396EC2E61E4` |
| `wave24-timeline-shown.mpp` | `829AE5E8DBAF6C33FF4043B5F210CCAB78730E18866BE491186BA2B3EE288EC5` |
| `wave24-flags20.mpp` | `1C74D403B7EB5280D57F1D6264D6A17B28A2F9578AF15F40282FDDC44CC471FF` |
| `wave24-flag2.mpp` | `614097D1031CA82B2F171170A5EB613349DA9BEEF6432CFB61900FBD91294BC6` |
| `wave24-flag20.mpp` | `1C1C9F2433962AFE309535074D92CAEAFA8052572B7AA7AFEC22541F7479F64F` |
| `wave25-auto.mpp` | `BD8503EE4BDF7E117491E3C245D1A84E64ECF91F51B1AA0625AE4F064E96ED16` |
| `reference/wave25-auto.measures.json` | `054CDBFAA0048F8FAECC20FD73A431E0CB0F57F919B27585351270CA0F8E7637` |
| `wave25-flag11.mpp` | `A420D4C1639DCB8E1C16F6606417E7DBD48A0A95D444C64CF8889C5092E06965` |
| `wave25-flag12.mpp` | `AD0C9F7A588F6A89945A0A3EBAD3B53D7DA1B7C522A1157B44A928FBA9151987` |
| `wave25-flag13.mpp` | `023D40E4E568C244C3F3319FC64605BBFF52C31F75794FAFD1CDCDA4615DF521` |
| `wave25-flag14.mpp` | `736090E534CC69AF08EB467D672B985E901C2B0F96A1C4720555D020862F22D4` |
| `wave25-flag15.mpp` | `9873616512655C002AE9D0FAEC172D46E7D85216F4D259323616E6D82DBD36AB` |
| `wave25-flag16.mpp` | `0D1F5FF13FB55F98D3AAE8647FB7FAA725C5B85E561B92B99A3D78E5065A92AE` |
| `wave25-flag17.mpp` | `B7F9EDF6A35D61E6B17FEBFFC8FB6378325CD29E9F01FAB9D8002A23330AC0EB` |
| `wave26-gantt-saves/control.style.bin` | `37A84A7963DD671CEADF9CFE2DAB06D1CBD798F00A0614420B9238418E849686` |
| `wave26-gantt-saves/arial.style.bin` | `D5E493B8AB72EE8DAFA2FBA0006A8F4F734886167D59D343FAE32253CE26C98C` |
| `wave26-gantt-saves/arial.fonts.bin` | `3CBE0D5459C1E30B5F37552D13669064768523C598003CBFFE7FA504C0C29A16` |
| `wave26-gantt-saves/size8.style.bin` | `43E14459F7D37FEFC56C36B065D66CA4705C868508AC1DF3C7AB78CADB39E88B` |
| `wave26-gantt-saves/size8.fonts.bin` | `DFDD77C6D17133401510414B75AE175244A4C24D5E9111CBCFED074796D8210A` |
| `wave26-gantt-saves/critical-blue.style.bin` | `FAF026BB99504B7A8C20A9504FDB4740C5C5FA395A03E0BF84C6402C0FEC3A0E` |
| `wave26-gantt-saves/milestone-italic.style.bin` | `B3C4F76B11244AF4DEB7D18DDB6D4C1E58C9D89ED7E5BA6F57A749BB6521620E` |
| `wave26-gantt-saves/milestone-bold.style.bin` | `873EDE49F8C21681265B5DCC53B2B221CA13DFF4FED716A76014E4EFDFFED722` |
| `wave26-gantt-saves/milestone-underline.style.bin` | `3E250AF7329D9BA3E843BF0A21E1F89BBE237A60ABE2789A3A2D3B1F9D1174B9` |
| `wave26-gantt-saves/highlight-fill.style.bin` | `DECD741A59CAE93EFE5027B1B1FA2FCE1D950C517E521C1C2C07136C900A308A` |
| `wave26-gantt-saves/task-red.style.bin` | `9CA275EA9945F362E3B369B491F05799E07E215FA70A82430AB0E9D40CAD5610` |
| `wave26-gantt-saves/summary-green.style.bin` | `42C316FCFED265E4703846373C75502747E4BB142FE6A49DC426D34E906A3BAB` |
| `wave26-gantt-saves/milestone-blue.style.bin` | `C13550BC657B240198B88CDFEFCCF89A940AEAE61E0187C601BA091E78513C39` |
| `wave26-gantt-saves/ts-yqm.style.bin` | `24332C5A66BDEA6A101E17FB6EC32471362F98DF08DE3D734696ECA323D61E6B` |
| `wave26-gantt-saves/ts-ymd.style.bin` | `E13B487DDAF942C3DB40E7FF98EA61926765C234CFB3DBAEFEA4F2F9768DEC3C` |
| `wave26-gantt-saves/ts-yhq.style.bin` | `78C8D721A02628517821FC4A88F4F6F1DF4F2A5250B63B16BF5A4991690E0A43` |
| `wave27-percent-actuals.mpp` | `56198DEA93A518B32E8813858855368C33C472B191D49AAD5E23E27CE8196C2C` |
| `wave27-priority-control.mpp` | `5ACA3BBA6E4B49CF6E91F8E582F1E1DCF45E23CFC0C424C3632E10A2F2C72152` |
| `wave27-priority-750.mpp` | `C5A6F06E39665323323BE796EA484FDBD1C0ED2CF9261F4F104565DA48FB6252` |
| `wave27-unlink-control.mpp` | `55CF09C7EAE1CD29E8A707394C1F3E49D065E368E6AF4FF113BB071A9D91C171` |
| `wave27-unlink.mpp` | `83DFA7677E75CEE5F4810F07E86149E372BA71FEB0D57BE4C84D23B27E6DB178` |
| `wave28-percent-auto0-control.mpp` | `69BC2E31A7663479109F6137E92EFAA99426C576224EEF78373B18112915E974` |
| `wave28-percent-auto40.mpp` | `079ECF4A2B402FBF819A4DE28EFF6F2A334F850369724D2FC778C52C72DF3024` |
| `wave28-percent-auto40-control.mpp` | `174CC3E3E56A793215BC9D5FD6928A199FEB2C20523C8C1D828AA4D0DBE09EF4` |
| `wave28-percent-auto100.mpp` | `B72297F7DE3E320008C34FC5E3755A23987B734773565CCD08982CD2469E211A` |
| `wave28-percent-auto100-control.mpp` | `E37F8ECD42FC56C91FD341DAA7BC20CB54CF71F971EEDC5A12E291B9F828F10F` |
| `wave28-percent-auto0.mpp` | `0978235B44C99C82F8943DC74DFF86D39B1C5F9FE7E88427E63B156ED8F9C93E` |
| `wave28-percent-manual0-control.mpp` | `17BA9E3FEC598ABB736FB6723E50A331E29FBEA447FB21EDD1F05493743774A9` |
| `wave28-percent-manual40.mpp` | `4EF4CB7FD7B115E7FDB2F9F2CC02EC026FBD73E3E28F78725E84B1F0230438C7` |
| `wave28-newtask-manual-control.mpp` | `DFD3812D419E390CB25C2A67436E1C9FC0CD57AC092741514EC4CF13799F0E41` |
| `wave28-newtask-auto.mpp` | `269EEB5707B20E257533E05F56C89F338D9B0F567D2D927706F05403CE572A2F` |
| `wave28-newtask-auto-control.mpp` | `923FC593ABC10F521BBC6429D94823FC1DAF1ABFF66C762D30D26ED6E53247CB` |
| `wave28-newtask-manual.mpp` | `988AE84E0E644F024D515E5E90A97807E8311145CE26C5EBAA4BE9C91B3F39FA` |
| `wave28-split-control.mpp` | `7C4243FE1548B16C4CAF2EBCB91D6753D913D228ECB2257944FF06C557D7B072` |
| `wave28-split.mpp` | `F65E36B93117B6962679027282F85EBB20AB60CFB0448CD4755B7B53A6901EEC` |

## `synthetic-base.mpp` — provenance

- **Generated by the coordinator via COM automation of MS Project** on
  **2026-10-22**, under `scripts/oracle_lock.py`, writing only to
  `corpus/probe/`. It is a fresh `FileNew()` project: project row titled `base`
  plus three tasks (`Task A` 5d, `Task B` 3d, `Task C` 2d, all starting
  2026-01-05 08:00, FS chain). No resources, no notes, no client content.
- **Not client data.** `corpus/` is git-ignored because it holds a client's real
  construction programme; `corpus/probe/` is not — those files were produced by
  driving MS Project, and this one is a synthetic minimum.
- **Copied byte-for-byte** from `corpus/probe/base.mpp`, which is the smallest
  probe fixture with more than one task. Verified at copy time:
  `SHA256 55C109F2F62FD6B9F05789AB002C35991A424AAB18C78F92E4200DF7B3FC6069`,
  237,568 bytes, source and destination hashes equal.
- **Do not replace this file with a client file**, and do not "refresh" it from
  `corpus/project_a.mpp` or `corpus/project_b.mpp`. The client
  corpus is read-only and must never enter a commit.
- If the fixture genuinely has to change (a new synthetic probe, a new field),
  regenerate it with MS Project through `oracle_lock.py`, write it to
  `corpus/probe/`, copy it here, update the hash above and the expected values in
  `tests/mpp_reader_tests.cpp` (`test_synthetic_fixture`).

## Why it is committed

`mpp_reader` used to be excluded from CI because on a clean checkout `corpus/`
does not exist, so every `.mpp` check printed `SKIP:`. With this fixture the
reader test builds and runs on all three runners; the PROJECT_A/PROJECT_B/corpus-probe
checks still skip when `corpus/` is absent, as they must.

The fixture is not allowed to go missing quietly: if `project_a_golden.tsv` is present
in this directory but `synthetic-base.mpp` is not, the test **fails** (it does not
skip), so a forgotten `git add` or a too-broad ignore rule cannot turn CI green
with the reader unexercised.

## Proving the fallback on a machine that has `corpus/`

The reader test never needs the working directory: it looks for the fixture
beside the committed golden TSV, whose absolute path `core/mpp/CMakeLists.txt`
already configures into the test (`MPP_GOLDEN_PATH`). `tests/test_paths.h.in`
also carries `TEST_DATA_DIR` for any target that wires the configured header in;
when it is defined, the test uses it instead. To exercise the no-corpus path
without touching the client files, set `MPP_NO_CORPUS` to any non-empty value:

```bat
REM Windows, from the repo root, after configuring build\vs
set MPP_NO_CORPUS=1
ctest --test-dir build\vs -C Release -R "^mpp_reader$" --output-on-failure
set MPP_NO_CORPUS=
```

```sh
# macOS / Linux
MPP_NO_CORPUS=1 ctest --test-dir build -R '^mpp_reader$' --output-on-failure
```

The run must pass, print `SYNTHETIC: 3 tasks`, and print `SKIP:` lines for PROJECT_A,
PROJECT_B and the `corpus/probe` sweep. CI runs that exact invocation as a second,
focused step, so the fallback cannot rot even if a runner ever gains a `corpus/`.
