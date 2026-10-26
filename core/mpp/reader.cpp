/*
 * reader.cpp -- Task table -> task records, for MPP14 files.
 *
 * The container findings are in `spec/mpp-container.md`; the record findings
 * are in `spec/mpp-records.md` (W1).  Three of W1's claims had to be corrected
 * here.  Each correction is demonstrated by the bytes and by MS Project's own
 * export (tests/data/project_a_golden.tsv, 83 tasks, generated over COM):
 *
 *  1. VarMeta layout.  W1 §5.1 reads a 32-byte header and (key, uid, offset)
 *     entries; MS Project writes a 24-byte header and (uid, offset, key)
 *     entries.  Both parses produce plausible-looking triples -- the stream is
 *     periodic -- so the discriminator is block coverage and key semantics:
 *
 *       - base.mpp: 42 blocks in Var2Data; the 24-byte parse references all 42
 *         and every block ends exactly where the next one starts (42/42 exact
 *         tiles); the 32-byte parse references 41 and leaves the project-title
 *         block reachable only through a "split entry" W1 had to invent.
 *       - The declared entry count in the header equals the parsed count on
 *         both undamaged files: base.mpp 42 = 42, PROJECT_B 1437 = 1437.  Under
 *         the 32-byte parse they are 41 and 1436, i.e. "entries + 1".
 *       - The stream length is 24 + 12*n exactly (PROJECT_A: 24 + 1081*12 = 12,996);
 *         the 32-byte parse needs a 4-byte trailer to make it add up.
 *       - Key 0x0b40000e (COM's PjFieldTaskName) lands on the name block for
 *         every row that has one, in base.mpp, PROJECT_A and PROJECT_B, including the
 *         project row ("base", "Project A",
 *         "Project B").  The 32-byte parse pairs names with
 *         0x0b400564 and the same 0x0b40000e with 4-byte non-text blocks.
 *
 *     Consequence: the file key IS the PjField id, so analysis/field_id_map.tsv
 *     decodes Var2Data and W1's "separate file-side key space" does not exist.
 *
 *  2. PercentComplete is a u16 at +92.  A 100 % row stores `64 00 64 00` there,
 *     so W1 §2's u32 reads 0x00640064 = 6,553,700.  +94 holds a second
 *     percentage (equal at 100, zero elsewhere) whose meaning is unmapped.
 *
 *  3. Row 0 is the project row (W1 §6 stands).  Row 67 of PROJECT_A (uid 10856) has
 *     neither a FixedMeta entry nor a VarMeta group: MS Project reports 83
 *     tasks, not 84, so such a row is a dead slot and goes to
 *     Document::skipped_rows.  Its orphaned variable blocks still hold the
 *     deleted task's name, which is reported as evidence, not as a task.
 *
 * STATUS AND OUTLINE FIELDS (W18; slack corrected by W30).  Three slack values
 * and four derived status/outline values are exposed (reader.h has the byte
 * evidence, docs/analysis/oracle/{w3-o1,outline-probe,milestone-probe}.json the
 * COM side):
 *
 *   +74  i32  FreeSlack, 6-second ticks.
 *   +186 i32  StartSlack, 6-second ticks.
 *   +190 i32  TotalSlack, 6-second ticks.  W18 read +74 as TotalSlack; W30
 *             measured the split.  milestone1.mpp ID6 stores +74 = 0 (COM
 *             FreeSlack 0) while +186 = +190 = 220800 ticks (COM StartSlack =
 *             TotalSlack = 22080 min); outline1's summary stores +186 = 19200
 *             (COM StartSlack 1920 min) and +74 = +190 = 0 (FreeSlack =
 *             TotalSlack = 0); w3-o1's four rows have all three equal
 *             (24000/0/3600/19200 ticks) so it could not separate them.  +190
 *             is also where schedin's ALAP task stores 52800 ticks, i.e. the
 *             5280 minutes the engine reported as a divergence when it read +74.
 *   Critical   derived: not manually scheduled && total_slack (+190) <= 0.  Not
 *             stored as a flag: no byte of the 202-byte row and no Fixed2Data
 *             bit is set only on w3-o1's critical task.  The manual-task gate is
 *             what makes base.mpp's Task A (slack 0) come out False, as COM
 *             reports it; the rule reproduces COM on all 10 task rows of base,
 *             w3-o1 and outline1.  The gate itself is inferred from Fixed2Meta
 *             +8 bit 0x80 -- see read_outline() for that evidence.
 *   Milestone  stored (W31): bit 0x02 at byte +10 of the Task FixedMeta entry,
 *             corrected from "duration == 0" by PROJECT_B, whose MSPDI export
 *             says Milestone=1 for uid 10735 (10 d) and uid 10799 (1 d).
 *             Duration == 0 is the fallback when the row has no metadata; see
 *             "W31" below.
 *   OutlineLevel  derived: depth of the parent chain that Fixed2Data stores.
 *             outline1's C1 and C2 both point at S1's row id at record +24, and
 *             COM reports 1/2/2 for S1/C1/C2; a "previous row" reading (spec §7)
 *             would have put C1's id in C2's record.  base and w3-o1 are flat
 *             and their chains are flat too -- 1/1/1 and 1/1/1/1, as COM says.
 *   OutlineNumber derived: sibling index path "1", "1.1", "1.2" in row order.
 *   Summary    derived: child_count > 0.  Fixed2Data record byte +42 bit 0x40 is
 *             Project's own has-children flag and agrees on every row of base,
 *             outline1, w3-o1, PROJECT_A (17 summaries) and PROJECT_B (13); a
 *             disagreement is reported as damage.
 *
 * SCHEDULING INPUTS (W19).  Four fixed-row groups feed the CPM engine; the
 * byte-level proof is in reader.h ("SCHEDULING INPUTS"):
 *
 *   +64  u16  ConstraintType (PjConstraint 0..7).
 *   +66  u32  ConstraintDate, unaligned; 0xffffffff for ASAP/ALAP.
 *   +182 u32  Deadline, unaligned; 0xffffffff when unset.  This is NOT VarMeta
 *             field 0x0b4001b5 -- no corpus file carries that key at all.  Two
 *             fixtures pin the offset: schedin.mpp's DL row holds 0x3d1327d8
 *             (2026-10-21 17:00) and w3-o4-last.mpp's T2 holds 0x3d1227d8
 *             (2026-10-20 17:00), both exactly the deadlines COM reported.
 *   +80  u32  ActualDuration (ticks).  W1's var-pct diff moves it 0 -> 12000
 *             for a 50 %-complete 5-day task; schedin's P50 stores 4800.
 *   +120 u32  ActualStart, +124 u32 ActualFinish.  These are the first two
 *             dates of W1's actual/stop/resume block (+120..+139).  All 38
 *             rows at 100 % in the corpus (PROJECT_A and its recalc copy) set +124
 *             and it equals their stored Finish; none of the 346 rows below
 *             100 % does.  Every Project-written row with progress has +120;
 *             the four rows with progress and no +120 are writer-patched
 *             fixtures that changed PercentComplete alone.  schedin's 50 % P50
 *             has +120 = its start and +124 = NA.  The identity is therefore an
 *             inference, not a COM measurement (`schedin-probe.json` reports
 *             ActualStart null even for P50 -- recorded as the open point).
 *
 * W31: THE EFFECTIVE DATES, TWO MORE FIXED-ROW FIELDS, THE STORED MILESTONE
 * FLAG, AND THE ASSIGNMENT DATES.
 *
 *   +104/+108 u32  Task Start/Finish, the EFFECTIVE pair Project's UI and its
 *             MSPDI export report (W33).  Task::start/finish read these.  The
 *             calculated pair +96/+100 is exposed as Task::calculated_start/
 *             calculated_finish: PROJECT_B uid 10805 is the one corpus row where
 *             they differ (+96/+100 = 2026-10-15 17:00 stale, +104/+108 =
 *             2026-03-23 08:00, which is what the export says), and the
 *             manual-start fixtures agree (+104/+108 carry COM's post-edit
 *             Start/Finish while +96/+100 stays stale).  PROJECT_A, lagff1, w3-o1,
 *             milestone1, holiday1, link1 and assign1 have zero differing rows,
 *             so the PROJECT_A golden stays 83/83.  The project row (row 0) is read
 *             from +96/+100, which equals its +104/+108 in every corpus file.
 *
 *   +112/+116 u32  LateStart/LateFinish (wave 20).  Task::late_start/late_finish.
 *             MEASURED on corpus/probe/w3-o1.mpp: COM reports T1
 *             2026-10-26 08:00/17:00, T3 13:00/17:00 and T4 17:00/17:00
 *             (docs/analysis/oracle/w3-o1-open.json) and those are exactly the
 *             words here.  base.mpp's uid 2 stores 2026-01-06 13:00, which is not
 *             its Start, so this is a separate field.  EarlyStart/EarlyFinish are
 *             NOT separate words: on all 32 COM rows of w3-o4.json and all four of
 *             w3-o1.json they equal Start/Finish, i.e. +104/+108.  See reader.h.
 *
 *   +174 u32  Start anchor (packed date, 0xffffffff = none).  It is the bound
 *             the start was aligned *from*, not the effective Start at +104.
 *             PROJECT_A uid 10759 stores 2026-08-10 17:00 (its predecessor's finish)
 *             while its Start is 2026-08-11 08:00; PROJECT_A uid 10742 stores its
 *             own 17:00 (Start == Finish).  link1.mpp's T2/T3 store T1's
 *             17:00 finish and start the next morning; holiday1.mpp's T2
 *             stores 2026-10-20 17:00 (T1's finish) and starts 10-21 08:00.
 *             manual-start-edited.mpp is the manual-task proof: COM set uid 1's
 *             Start to 2026-01-12 08:00, +174 holds it, and the calculated +96
 *             still holds the stale 2026-01-05 08:00.  Auto tasks with no such
 *             bound store 0xffffffff (w3-o1, schedin, lagff1, assign1).  This
 *             reader does NOT feed it into start.
 *
 *   +178 i32  Task calendar uid; -1 = none (use the project calendar).
 *             holiday1.mpp's three tasks store 3 and holiday-probe.json records
 *             SiteCal as the task calendar with uid 3; every PROJECT_A/PROJECT_B row
 *             stores -1.
 *
 *   Milestone  FixedMeta entry byte +10 bit 0x02.  PROJECT_B's export says
 *             Milestone=1 on 10 rows, 8 of them zero-duration and two not
 *             (10735, 10799); the bit matches exactly (0 false positives on 63
 *             other live rows).  milestone1.mpp sets it on exactly COM's five
 *             milestones (uids 2,3,4,5,7) and w3-o1 only on T4.  Duration == 0
 *             is the fallback for rows the metadata does not cover.
 *
 *   Assignment +52/+56/+60 (packed dates).  +52 Start and +56 Finish equal the
 *             task's +104/+108 on all 67 live PROJECT_A assignments, all 63 of
 *             PROJECT_B's assignments whose task is a live task (10 of PROJECT_B's 73
 *             assignments name deleted task uids), 4/4 link1 and 3/3 real
 *             assign1 rows (base.mpp's unassigned rows store the project row's
 *             date-only start).  +60 is inside [start, finish]: it equals start
 *             on the 51 zero-percent PROJECT_A assignments, finish on all 13 at
 *             100 %, and strictly between on the 3 partial rows (10 %, 40 %,
 *             40 %) -- Project's Stop semantics, marked an inference because no
 *             oracle export carries assignment dates.
 *
 * THE OTHER THREE TABLES (W10).  Strides are measured from FixedData size and
 * from the FixedMeta entry offsets, never guessed; see the "table geometry"
 * block below for the arithmetic that every corpus file satisfies:
 *
 *   TBkndRsc  FixedData = 48 + 172*resources.  FixedMeta = 16 + 37*(resources+3)
 *             (entries 0..2 are the 48-byte header's three 16-byte slots, then
 *             one entry per row at offsets 48, 220, 392, ...).  Row 0 is a uid-0
 *             placeholder; resources are rows 1..n-1.  Name = VarMeta
 *             0x0c400001, Initials = 0x0c400002, MaterialLabel = 0x0c40012b.
 *             MaxUnits is the double at +8, x100: 10000.0 = 100 % and, measured
 *             in wave 11 on w9-resourcetypes.mpp, 5000.0 = 50 % (that fixture
 *             also pins the x100 scale; the old x10000 guess is withdrawn).
 *             Resource Type is a metadata bit, not a row byte: Resource
 *             FixedMeta entry +12 bit 0x10 = Work; when clear, Fixed2Meta entry
 *             +8 bit 0x10 = Cost and clear = Material (w9-resourcetypes gives
 *             Work/Material/Cost; every PROJECT_A/PROJECT_B resource is Work).  See
 *             reader.h's "Resource Type" block.
 *
 *   TBkndCons FixedData = 20*rows, no header.  FixedMeta = 16 + 10*entries,
 *             and the entry count is NOT the row count on PROJECT_A (99 rows, 91
 *             entries); see LIVENESS below.  VarMeta holds its 24-byte header
 *             and zero entries; Var2Data is empty.  Row: uid +0, predecessor
 *             task uid +4, successor task uid +8, type u16 +12 (Project enum
 *             0 FF / 1 FS / 2 SF / 3 SS), lag i32 +14 in 6-second ticks (4800 =
 *             one working day; negative is a lead), u16 +18 = 7.  Lag proof:
 *             PROJECT_A's three live non-zero lags are 96000, 139200 and 240000
 *             ticks, i.e. exactly 20, 29 and 50 days; fixture link1.mpp stores
 *             4800 (1 day) and -4800 (lead) in the same field.  PROJECT_B's 72
 *             links are all FS and chain task 10722 -> 10723 -> 10724, which is
 *             what type 1 has to mean.  (A fourth PROJECT_A row storing 28800 = 6
 *             days is a deleted link slot with no FixedMeta entry; see
 *             LIVENESS.)
 *
 *   TBkndAssn FixedData = 110*assignments, no header.  FixedMeta = 16 +
 *             34*assignments.  Row: assignment uid +0 (equals the VarMeta uid),
 *             task uid +4, resource uid +8, Units double +12 (10000.0 = 100 %),
 *             Work double +20 (minutes x1000: 2,400,000 = 2,400 min = 5 days),
 *             u32 +92 = 7.  Work is measured twice: base's 5/3/2-day tasks hold
 *             2400000/1440000/960000, and probe-v2's 5 d -> 7 d edit moves task
 *             1 to 3360000 (exactly x1.4).  Resource uid is proved by
 *             corpus/probe/assign1.mpp: three real rows with +8 = 1, 2, 1 for
 *             the documented T1/R1, T1/R2, T2/R1 assignments.  The word is
 *             0xffff0001 (== -65535, mpxj's ASSIGNMENT_NULL_RESOURCE_ID) on the
 *             unassigned placeholder rows of base/probe-v1/PROJECT_A/PROJECT_B, so those
 *             rows have no resource; Assignment::has_resource() says so.
 *
 * LIVENESS.  Tasks: a VarMeta group (Name).  Resources: FixedMeta coverage of
 * rows 1..; row 0 is structural.  Assignments: FixedMeta coverage (PROJECT_A has 69
 * rows but only 67 covered; its row 52 belongs to the deleted task 10856 and
 * row 65 is all zero).  Links: FixedMeta coverage, i.e. a link is live only
 * when a FixedMeta entry names its FixedData row (through the entry's +4
 * offset).  PROJECT_A has 99 link rows, 91 FixedMeta entries (the header says 88) and
 * 88 distinct covered rows; 11 rows have no entry at all (6, 9, 33, 38, 59, 60,
 * 63, 68, 85, 86, 89) and entries 88..90 repeat an earlier row's offset.
 * Reading every non-zero row instead returned 97 links and invented two that
 * Project does not schedule: 10758 -> 10859 with a 6-day lag and 10760 ->
 * 10846.  MS Project's own export (corpus/probe/project_a-export.xml, 88
 * <PredecessorLink> elements, uids in docs/analysis/oracle/
 * project_a_export_links.json) has 10859 <- 10858 lag 0 and 10846 <- 10849, 10877,
 * 10759.  Endpoints that do not name a live task are kept but flagged and
 * warned about.
 *
 * LINK LIVENESS HAS TWO LEVELS (W31).  "row-live" is the FixedMeta coverage
 * above; "endpoint-live" means predecessor_uid and successor_uid both name a
 * live task.  PROJECT_B is the measured split: 72 row-live links, 70 with live
 * endpoints, 2 dead-endpoint (10722 -> 10723, 10723 -> 10724 -- both uids exist
 * only in its deleted task slots at rows 6..8).  Project's MSPDI export has 70
 * <PredecessorLink> elements, so Project does not schedule a dead-endpoint
 * link.  The reader keeps those two rows (their bytes are real) and reports
 * them through Link::endpoints_live() and
 * Document::link_count_with_*_endpoints().  PROJECT_A is 88/88 and link1 5/5.
 *
 * ==========================================================================
 * MPP12 (Project 2007).  Wave 18/G1, 2026-10-23.  Everything below was measured
 * on the five fixtures the coordinator placed in corpus/probe/ --
 * mpp12task.mpp, mpp12header.mpp, mpp12relations.mpp, mpp12resource.mpp,
 * mpp12splittask.mpp -- plus mpp12subproject.mpp, mpp12nulltasks.mpp,
 * mpp12duration.mpp, mpp12calendar.mpp, mpp12recurring.mpp,
 * mpp12timephased.mpp, mpp12viewstate.mpp and tf2007-mpp12.mpp, which this
 * stream downloaded from the same MPXJ test corpus.  No oracle (COM) run backs
 * these offsets: the independent check is MS Project's own content as MPXJ
 * asserts it (junit MppTaskTest/MppSubprojectTest expectations for the same
 * files) and the internal consistency of every row.
 *
 * WHERE THE VERSION COMES FROM.  A top-level `Props12` stream means MPP12, a
 * top-level `Props14` means MPP14/16; when neither exists the storage root that
 * holds `TBkndTask/FixedData` decides ("112" or "114"), with a warning.  The
 * version string lives in that props block at key 0x3540000c: every MPP12
 * fixture stores UTF-16 "12,0,4017,1004" (Project 2007, build 4017), a
 * Project-16 MPP14 file stores "16,0,20430,20182".  The same props block's
 * PASSWORD_FLAG (key 0x35400000) is the read/modify password flag; see
 * "PASSWORD" below.
 *
 * TABLE GEOMETRY.  Same four-part framing (FixedData/FixedMeta/VarMeta/
 * Var2Data), same FixedMeta 16-byte header (magic 0xBAADFADB read as
 * 0xFADFADBA, version 4, entry count, FixedData size), same VarMeta 24-byte
 * header + 12-byte (uid, offset, key) entries, same Var2Data length-prefixed
 * blocks.  What differs is the fixed row size and the field offsets:
 *
 *   table        MPP12                                MPP14 (this reader, above)
 *   TBkndTask    48 + 264*rows, FixedMeta 16+47*(rows+3)   48 + 202*rows
 *   TBkndRsc     48 + 188*rows, FixedMeta 16+37*(rows+3)   48 + 172*rows
 *   TBkndCons    20*rows, no header, FixedMeta 16+10*n      same
 *   TBkndAssn    142*rows, no header, FixedMeta 16+34*n     110*rows
 *   TBkndCal     64 + 12*rows, FixedMeta 16+10*(rows+4)     same
 *   Task Fixed2  176*rows, Fixed2Meta 16+86*(rows+3)        64*rows, 96*(rows+3)
 *   Rsc Fixed2   16+49*(rows+3)                        16+51*(rows+3)
 *
 * FixedMeta still starts with the same three 16-byte descriptors (offsets 0,
 * 16, 32); data rows are entries 3.. .  The row stride above is what the
 * entries produce, not an assumption: mpp12task's offsets are 0,16,32,48,312
 * and its FixedData is 576 = 48 + 264*2.  Rows are read BY ENTRY OFFSET (the
 * next entry's offset bounds each row), because MPP12 has 16-byte "null task"
 * blocks in the middle of the table: mpp12nulltasks has entries at
 * 48,312,576(16),592,856,1120(16),1136,1400(16),1416, i.e. three real tasks
 * followed/preceded by 16-byte placeholder rows whose FixedMeta state byte is
 * 0x04.  A MPP12 task row is therefore: size 16 -> null-task placeholder (not a
 * task); size 264 and FixedMeta +0 == 0x00 -> live; +0 == 0x02 -> deleted
 * tombstone (the MPP12 form of the W45 flag); anything else -> refused with a
 * warning naming the row, never guessed at.
 *
 * MPP12 TASK ROW (264 bytes; uid at +0 and ID at +4, the REVERSE of MPP14 --
 * proved by mpp12resource/mpp12calendar, whose first task has uid 2 / ID 1, and
 * by mpp12viewstate, uid 5 / ID 1):
 *
 *   +0    u32  UniqueID
 *   +4    u32  ID
 *   +8    u32  EarlyFinish        +12  u32 LateStart
 *   +16   u32  Stop               +20  u32 Resume
 *   +24   i32  FreeSlack (ticks)  +28  i32 StartSlack   +32  i32 FinishSlack
 *   +36   u32  parent row UniqueID (0xffffffff on the project row; the
 *              project row itself is uid 0, so a top-level task stores 0)
 *   +40   u16  OutlineLevel (STORED here, unlike MPP14; 0 on the project row)
 *   +58   u16  unused (0 in every row of every fixture)
 *   +60   u32  Duration in 6-second ticks
 *   +64   u16  DurationFormat: PjFormatUnit + 0x20 when estimated.  MEASURED
 *              on mpp12duration.mpp, whose ten tasks are one minute/hour/day/
 *              week/month and the elapsed variants: +64 = 3,5,7,9,11 and
 *              4,6,8,10,12 with ticks 10,600,4800,24000,96000 / 10,600,
 *              14400,100800,432000.  Despite the MPP14 name "actual duration
 *              units", this is the task's duration display unit (a fresh 0 %
 *              task stores it and ActualDuration is 0).
 *   +66   u32  ActualDuration (ticks)
 *   +70   u32  RemainingDuration  +74 u32 BaselineDuration
 *   +80   u16  ConstraintType (PjConstraint 0..7)
 *   +88   u32  Start (packed)     +92  u32 Finish (packed)   <- effective pair
 *   +96   u32  ActualStart        +100 u32 ActualFinish
 *   +104  u32  BaselineStart      +108 u32 BaselineFinish
 *   +112  u32  ConstraintDate     +116 u32 ResumeNoEarlierThan
 *   +120  u16  Priority           +122 u16 PercentComplete
 *   +126  u16  Type               +128 u16 FixedCostAccrual
 *   +130  u32  Created            +136 u32 PreleveledStart +140 PreleveledFinish
 *   +148  u32  EarlyStart         +152 u32 LateFinish
 *   +156  u32  SummaryProgress    +160 i32 CalendarUniqueID (-1 = none)
 *   +164  u32  Deadline
 *   +168  f64 Work (minutes x1000)  +200 f64 Cost (hundredths)
 *   (the 8-byte Work/Cost doubles at +168 and +200 are cross-checked against
 *   MPXJ's expectations: mpp12task Task #1 Work = 2,400,000 -> 40 h, Cost =
 *   500,000 -> 5000, ActualCost = 280,000 -> 2800, FixedCost = 100,000 -> 1000.)
 *
 * There is NO calculated Start/Finish pair in an MPP12 row: +96/+100 are the
 * actuals.  `calculated_start/finish` are therefore set equal to
 * start/finish for MPP12 (documented in reader.h) and `start_anchor` stays
 * kDateNa -- MPP12 has no measured equivalent of MPP14's +174.
 *
 * MPP12 RESOURCE ROW (188 bytes): uid +0, ID +4, MaxUnits double +44 (x100,
 * 10000.0 = 100 %).  Resource Type is metadata, as in MPP14, but at different
 * bits: FixedMeta entry +9 bit 0x02 = Work; when clear, Fixed2Meta entry +8
 * bit 0x10 = Cost, else Material.  MEASURED on
 * corpus/probe/resource-type-project2007-mpp12.mpp (downloaded here), whose 15
 * resources are five Cost, five Material and five Work: all five Cost rows have
 * fm+9 = 0x20 and f2+8 = 0x83 (0x10 set), the five Material rows fm+9 = 0x20 /
 * f2+8 = 0x43, and the five Work rows fm+9 = 0x32 (0x02 set).  Name/Initials/
 * MaterialLabel are the same VarMeta keys as MPP14 (0x0c400001/2/0x12b).
 * MPP12 Fixed2Meta entries are 49 bytes (16+49*(rows+3)), not 51.
 *
 * MPP12 LINK ROW (20 bytes, no header): uid +0, predecessor +4, successor +8,
 * type u16 +12 (PjRelationType 0=FF..3=SS), LagFormat u16 +14, lag i32 +16.
 * That is the SAME 20-byte row as MPP14 with the last two words SWAPPED:
 * MPP14 stores lag i32 at +14 and format u16 at +18.  Evidence:
 * mpp12relations.mpp's four links are 1->2 FS, 2->3 SS+1 day, 3->4 FF, 4->5 SF
 * (MPXJ's own test asserts exactly those); the SS row stores +14 = 7 (days) and
 * +16 = 4800 ticks = 1 day, while reading +14 as the lag would give 1,228,807
 * ticks.  Link liveness is FixedMeta coverage + state byte as for MPP14.
 *
 * MPP12 ASSIGNMENT ROW (142 bytes, no header): uid +0, task uid +4, resource
 * uid +8 (0xffff0001 = "no resource", same sentinel as MPP14), Start u32 +12,
 * Finish u32 +16, Units double +54 (x100), Work double +62 (minutes x1000).
 * Measured on mpp12resource.mpp: assignment Start/Finish equal the task's
 * effective Start/Finish on all 5 rows, and the two unassigned rows of
 * mpp12duration.mpp store 0xffff0001.  MPP12 DOES NOT STORE a Resume date for
 * an assignment (there is no such field in the 142-byte row), so
 * Assignment::resume stays kDateNa for MPP12.
 *
 * MPP12 OUTLINE is STORED, not derived: parent uid +36 and OutlineLevel +40.
 * mpp12recurring.mpp is the proof of the chain: its eight summary rows are
 * level 1 with parent 0 (the project row) and their children level 2 with the
 * summary's uid, across uid gaps (uids 1,4,10,16,... because recurring child
 * rows have their own ids).  The reader uses the stored level for
 * outline_level and still derives OutlineNumber from the parent links (sibling
 * index path), so the two agree by construction; a stored level that differs
 * from the chain depth is reported as damage and the stored level wins.
 * Summary = child_count > 0.  Critical: MPP12 has no manual scheduling, so
 * `critical` is simply TotalSlack <= 0; the third slack word (+32) is what
 * MPXJ calls Finish Slack and there is no separate total word, so the reader
 * exposes +32 as total_slack (they coincide for an unconstrained task) and
 * documents that mapping here rather than inventing a field.
 *
 * MPP12 MILESTONE is FixedMeta entry byte +8 bit 0x20 (MPP14 uses +10 bit
 * 0x02).  Evidence: mpp12task.mpp Task #1 has +8 = 0x63 (bit set) and MPXJ's
 * MppTaskTest asserts that task is a milestone despite its 5-day duration;
 * tf2007-mpp12.mpp's 20 flag rows all have +8 = 0x43 (bit clear, not
 * milestones).  Flag1..Flag20 are FixedMeta entry bits starting at byte 37 bit
 * 0x20 and running contiguously through byte 40: MEASURED on
 * tf2007-mpp12.mpp, whose Task "Flag1" sets byte 37 bit 0x20, "Flag2" 0x40,
 * "Flag3" 0x80, "Flag4" byte 38 0x01, ... "Flag20" byte 40 0x01.  The reader
 * exposes all twenty for MPP12 (all bit positions measured); MPP14 exposes
 * Flag1 only (its other bits are still unmeasured).
 *
 * MASTER/SUBPROJECT (MPP12).  An inserted project is a normal task row plus:
 *   - Task VarMeta key 0x0b4001ca (field id 458,
 *     PjTaskSubprojectTasksUniqueIDOffset), a u32 base added to the
 *     subproject's ids: mpp12subproject.mpp stores 0x00800000 on the
 *     "SubprojectA-9" row and 0x00c00000 on "SubprojectB-9".
 *   - The project props' SUBPROJECT_DATA record (112/Props key 0x024000a2),
 *     which lists each inserted source.  Each item is a 20-byte header (16-byte
 *     GUID + 4 flag bytes; the type byte is header+16) followed by offset
 *     words; the source path is a nested block: +18, u32 string-size, ASCII
 *     DOS path (NUL-terminated), +24, u32 block size; when that size is
 *     non-zero a u32 UTF-16 byte length follows at +2, then the full path.
 *     mpp12subproject.mpp decodes to uid 2 -> D:\java\mpxj\junit\data\
 *     SubprojectA-9.mpp and uid 3 -> ...\SubprojectB-9.mpp.  Only the
 *     per-item layout with type byte 0x01/0x08 is implemented (the one on
 *     disk); every other type is reported with its offset and skipped, not
 *     guessed.  The children of the inserted row live in the subproject file
 *     and are NOT in this document: the master stores the summary row only.
 *
 * PASSWORD.  Both props formats store the same record layout: a 16-byte header
 * with the record count as u16 at +12, then (u32 data length, u32 key, u32
 * unused, data), padded to an even length.  Key 0x35400000 is PASSWORD_FLAG
 * (0x00 none, 0x01 read password, 0x02 write reservation, 0x03 both);
 * 0x35400004 is the read-protection hash, 0x35400005 the write-reservation
 * hash, 0x35400007 the single-byte XOR encryption code.  All 12 fixtures on
 * disk store flag 0x00 and no hash record.  The container itself is plain
 * OLE2: the flag does not encrypt anything by itself.  This reader refuses a
 * read-password file (bit 0x01) and any flagged file with a nonzero encryption
 * code (the table streams would be XOR-masked; MPXJ's
 * DocumentInputStreamFactory), reads a write-reservation-only file (bit 0x02)
 * and reports it as read-only via Document::password_required_to_modify.
 *
 * CALENDARS.  Both formats use the same `TBkndCal` geometry (64 + 12*rows)
 * but the 12-byte row's columns are ordered differently: MPP12 is (calendar
 * uid, base uid, resource uid), MPP14 is (base uid, resource uid, calendar
 * uid).  MPXJ's MPP12CalendarFactory declares exactly that (+0 calendar id,
 * +4 base, +8 resource) and the MPP14 factory the reverse; the rows confirm it
 * (mpp12task (1,-1,-1),(2,1,0),(3,1,1) vs base (0,-1,1),(1,0,2),(1,1,3)).
 * Names are VarMeta key 0x0d400001.  The reader exposes the identities only
 * (Document::calendars); core/cal owns the work-week/exception decode and
 * still hard-codes "114/" and the MPP14 column order, so it must take
 * Document::table_root and the version before it can read an MPP12 file;
 * core/mpp does not modify another stream's files.
 */

#include "reader.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "utf16.h"

namespace mpp {
namespace {

/* ---- Task table geometry (spec/mpp-records.md §1, corrected §5.1). */
constexpr std::size_t kFixedHeaderSize = 48;
constexpr std::size_t kFixedRowSize = 202;
constexpr std::size_t kFixedMetaHeaderSize = 16;
constexpr std::size_t kFixedMetaEntrySize = 47;
constexpr std::size_t kVarMetaHeaderSize = 24;
constexpr std::size_t kVarMetaEntrySize = 12;
constexpr std::size_t kFixedMetaDescriptors = 3; /* FixedData offsets 0, 16, 32 */
/* Stream bytes are ba ad df fa; read little-endian that is 0xFADFADBA. */
constexpr std::uint32_t kMetaMagic = 0xFADFADBAu;
/* VarMeta header word 5 is the Var2Data size (W1 §5.1, confirmed). */
constexpr std::size_t kVarMetaSizeWord = 0x14;

/* ---- Table geometry.  Every number below is measured from FixedData size
 * and from the FixedMeta entry offsets in corpus/probe/base.mpp,
 * corpus/probe/probe-v1.mpp, corpus/probe/var-rsc2.mpp, n1..n6,
 * corpus/project_a.mpp and corpus/project_b.mpp.
 *
 *   table        FixedData              FixedMeta                    live rows
 *   TBkndTask    48 + 202*rows          16 + 47*(rows+3)              tasks+1
 *   TBkndRsc     48 + 172*rows          16 + 37*(rows+3)              resources+1
 *   TBkndCons    20*rows, no header     16 + 10*entries               links
 *   TBkndAssn    110*rows, no header    16 + 34*rows                  assignments
 *
 * Sizes that pin each stride down:
 *   Rsc  base 392=48+172*2, var-rsc2 564=48+172*3, n1 220=48+172*1,
 *        PROJECT_A/PROJECT_B 4004=48+172*23; FixedMeta offsets 0,16,32 then 48,220,...
 *   Cons PROJECT_B 1440=20*72 (FixedMeta 736=16+10*72), PROJECT_A 1980=20*99
 *        (FixedMeta 926=16+91*10; 88 distinct row offsets).
 *   Assn PROJECT_B 8030=110*73 (FixedMeta 2498=16+34*73), PROJECT_A 7590=110*69,
 *        base 330=110*3; FixedMeta offsets 0,110,220,...
 */
constexpr std::size_t kRscHeaderSize = 48;
constexpr std::size_t kRscRowSize = 172;
constexpr std::size_t kRscFixedMetaEntrySize = 37;
constexpr std::uint32_t kFieldRscName = 0x0C400001u;
constexpr std::uint32_t kFieldRscInitials = 0x0C400002u;
/* MaterialLabel is VarMeta key 0x0c40012b (a Material resource only; measured
 * on corpus/probe/w9-resourcetypes.mpp, COM-built R2 with "metre"). */
constexpr std::uint32_t kFieldRscMaterialLabel = 0x0C40012Bu;
constexpr std::size_t kRscRowId = 0;
constexpr std::size_t kRscRowUid = 4;
constexpr std::size_t kRscRowMaxUnits = 8; /* double, x100: 5000.0 = 50 % */
/* Resource Type is not a row byte: FixedMeta entry +12 bit 0x10 = Work, and
 * when that is clear Fixed2Meta entry +8 bit 0x10 = Cost (clear = Material).
 * Both words are measured on w9-resourcetypes.mpp (Work/Material/Cost).  The
 * Fixed2Meta entry is 51 bytes in every file on disk (16 + (rows+3)*51). */
constexpr std::size_t kRscFixedMetaTypeByte = 12;
constexpr std::uint8_t kRscFixedMetaWorkFlag = 0x10;
constexpr std::size_t kRscFixed2MetaEntrySize = 51;
constexpr std::size_t kRscFixed2MetaCostByte = 8;
constexpr std::uint8_t kRscFixed2MetaCostFlag = 0x10;

constexpr std::size_t kConsRowSize = 20;
/* Measured: PROJECT_B 736 = 16 + 10*72; PROJECT_A 926 = 16 + 91*10.  Link liveness is
 * FixedMeta coverage, so this stride is load-bearing. */
constexpr std::size_t kConsFixedMetaEntrySize = 10;
constexpr std::size_t kConsRowUid = 0;
constexpr std::size_t kConsRowPredecessor = 4;
constexpr std::size_t kConsRowSuccessor = 8;
constexpr std::size_t kConsRowType = 12;     /* u16 */
constexpr std::size_t kConsRowLag = 14;      /* i32, 6-second ticks */
constexpr std::size_t kConsRowLagUnits = 18; /* u16 */

constexpr std::size_t kAssnRowSize = 110;
constexpr std::size_t kAssnFixedMetaEntrySize = 34;
constexpr std::size_t kAssnRowUid = 0;
constexpr std::size_t kAssnRowTaskUid = 4;
constexpr std::size_t kAssnRowResourceUid = 8;
constexpr std::size_t kAssnRowUnits = 12; /* double, x100 */
constexpr std::size_t kAssnRowWork = 20;  /* double, minutes x1000 */
/* Assignment dates (W31, corrected W40): +52 Start, +56 Finish, +60 Resume.
 * +52/+56 were measured against the task's effective pair; +60 is the Resume
 * date, proved against Project's MSPDI export of PROJECT_A (21 non-empty <Resume>
 * elements, 21/21 equal).  It is NOT Stop: on the two split rows Stop is the
 * preceding working instant.  See reader.h's ASSIGNMENTS block. */
constexpr std::size_t kAssnRowStart = 52;  /* u32: packed date */
constexpr std::size_t kAssnRowFinish = 56; /* u32: packed date */
constexpr std::size_t kAssnRowResume = 60; /* u32: packed date */

/* ---- Fixed-row field offsets, relative to the start of a 202-byte Task row. */
constexpr std::size_t kRowId = 0;        /* u32: MS Project ID */
constexpr std::size_t kRowUid = 4;       /* u32: UniqueID */
/* Scheduling inputs (W19); the proof is in reader.h. */
constexpr std::size_t kRowConstraintType = 64; /* u16: PjConstraint 0..7 */
constexpr std::size_t kRowConstraintDate = 66; /* u32: packed date, unaligned */
/* Three slack fields, corrected by W30 (2026-10-22).  +74 was long read as
 * TotalSlack; milestone1.mpp separates them: its ID6 stores +74 = 0 (COM
 * FreeSlack 0) while +186 and +190 both hold 220800 ticks (COM StartSlack and
 * TotalSlack 22080 min).  outline1's summary stores +186 = 19200 (COM StartSlack
 * 1920 min) with +74 = +190 = 0 (FreeSlack and TotalSlack 0).  w3-o1, lagff1
 * and milestone1's other rows have all three equal, which is why they could not
 * tell them apart.  +190 also resolves the old "ALAP divergence": schedin's ALAP
 * task stores 52800 ticks there = the engine's 5280 min. */
constexpr std::size_t kRowFreeSlack = 74;   /* i32: FreeSlack, 6-second ticks */
constexpr std::size_t kRowStartSlack = 186; /* i32: StartSlack, 6-second ticks */
constexpr std::size_t kRowTotalSlack = 190; /* i32: TotalSlack, 6-second ticks */
constexpr std::size_t kRowPriority = 78; /* u16 */
constexpr std::size_t kRowActualDuration = 80; /* u32: 6-second ticks */
constexpr std::size_t kRowDuration = 84; /* u32: 6-second ticks */
constexpr std::size_t kRowPercent = 92;  /* u16 */
/* +164 (0xa4): DurationFormat in PjFormatUnit numbering; 7 = days, 8 = elapsed
 * days.  Measured against Project's MSPDI <DurationFormat> on every live task
 * row of PROJECT_A (84), PROJECT_B (74) and w9-resourcetypes (4), and against
 * w9-elapsedlag.mpp's "2 edays" control (8 vs 7 on its working tasks). */
constexpr std::size_t kRowDurationFormat = 164; /* u16 */
/* Two Start/Finish pairs.  +96/+100 is the calculated pair; +104/+108 is the
 * EFFECTIVE pair Project's own UI/COM surface reports (W33).  PROJECT_B uid 10805
 * is the one row on disk where they differ -- +96/+100 = 2026-10-15 17:00,
 * +104/+108 = 2026-03-23 08:00, and the MSPDI export reports the latter; the
 * manual-start fixtures agree (+104/+108 = COM's Start/Finish after the edit,
 * +96/+100 stays stale).  PROJECT_A, lagff1, w3-o1, milestone1, holiday1, link1 and
 * assign1 have zero differing rows, so Task::start/finish = +104/+108 keeps the
 * PROJECT_A golden at 83/83. */
constexpr std::size_t kRowCalculatedStart = 96;   /* u32: packed date */
constexpr std::size_t kRowCalculatedFinish = 100; /* u32: packed date */
constexpr std::size_t kRowStart = 104;            /* u32: effective, packed date */
constexpr std::size_t kRowFinish = 108;           /* u32: effective, packed date */
/* LATE PAIR (W33 follow-up, wave 20).  +112/+116 are the stored LateStart/
 * LateFinish fields.  MEASURED, not inferred: COM read of
 * corpus/probe/w3-o1.mpp (docs/analysis/oracle/w3-o1-open.json) reports
 * T1 2026-10-26 08:00/17:00, T3 2026-10-26 13:00/17:00 and T4
 * 2026-10-26 17:00/17:00, which are exactly the words here on all four rows;
 * Project's own re-save of base.mpp (corpus/probe/resaved-base.mpp) keeps
 * base's three values, and uid 2's late start (2026-01-06 13:00) is not its
 * Start -- so this is a separate field, not a copy of +104/+108.  Every COM
 * read-back on disk reports EarlyStart == Start and EarlyFinish == Finish
 * (32/32 w3-o4 rows, 4/4 w3-o1), so the +104/+108 pair already carries the
 * early fields and no early word is decoded separately (see reader.h). */
constexpr std::size_t kRowLateStart = 112;        /* u32: packed date */
constexpr std::size_t kRowLateFinish = 116;       /* u32: packed date */
constexpr std::size_t kRowActualStart = 120;  /* u32: packed date */
constexpr std::size_t kRowActualFinish = 124; /* u32: packed date */
constexpr std::size_t kRowDeadline = 182;     /* u32: packed date, unaligned */
/* W31: +174 is the start anchor and +178 the task-calendar uid; see reader.h. */
constexpr std::size_t kRowStartAnchor = 174;  /* u32: packed date, 0xffffffff = none */
constexpr std::size_t kRowCalendarUid = 178;  /* i32: -1 = use the project calendar */
/* The largest valid PjConstraint value (7 = FNLT, spec/scheduling.md §0.4). */
constexpr std::uint16_t kConstraintFnlt = 7;

/* Fixed2Data / Fixed2Meta: the outline table (spec/mpp-records.md §7).  Sizes
 * are 64 * (rows + 3) and 16 + 96 * (rows + 3) in every file examined; base
 * 448/688 with 4 rows, w3-o1 512/784 with 5, PROJECT_A 5,632/8,464 with 85. */
constexpr std::size_t kFixed2RowSize = 64;
constexpr std::size_t kFixed2MetaEntrySize = 96;
constexpr std::size_t kF2RowId = 0;         /* 16 B: row identity */
constexpr std::size_t kF2RowParentId = 24;  /* 16 B: parent row identity */
constexpr std::size_t kF2RowFlags = 42;     /* u8: bit 0x40 = has children */
constexpr std::uint8_t kF2FlagHasChildren = 0x40;
/* Fixed2Meta's 96-byte entry carries a per-row flags byte at +8.  Bit 0x80 is
 * the best candidate for Project's "manually scheduled" task mode (see the
 * evidence in read_outline()); it is an INFERENCE, not a proved field. */
constexpr std::size_t kF2MetaFlags = 8;
constexpr std::uint8_t kF2MetaFlagManual = 0x80;
/* FixedMeta's 47-byte entry carries a 16-byte per-row blob at +8; its byte
 * +10 bit 0x02 is Project's stored Milestone flag.  Evidence (W31): PROJECT_B
 * sets it on exactly its 10 Milestone=1 rows -- 8 zero-day and 2 with a
 * duration (10735 10 d, 10799 1 d) -- with no false positives on its 63 other
 * live rows; milestone1.mpp sets it on exactly the 5 rows COM calls
 * milestones (uids 2,3,4,5,7); w3-o1 sets it only on T4.  Duration == 0 is
 * only the fallback. */
constexpr std::size_t kFixedMetaMilestoneByte = 10;
constexpr std::uint8_t kFixedMetaFlagMilestone = 0x02;
/* FixedMeta entry bytes +24/+25 and +33/+34 carry Task Flag1..Flag20 as one-bit
 * fields.  Wave 24 (W24-15) measured EVERY bit with a controlled Project save:
 * a copy of wave23-outline.mpp with Task.FlagN = True on ID 3 (one flag at a
 * time, control all false) changes exactly one FixedMeta byte:
 *   Flag1..7  = byte +24, bits 0x02 0x04 0x08 0x10 0x20 0x40 0x80
 *   Flag8..10 = byte +25, bits 0x01 0x02 0x04
 *   Flag11..17= byte +33, bits 0x02 0x04 0x08 0x10 0x20 0x40 0x80
 *   Flag18..20= byte +34, bits 0x01 0x02 0x04
 * (Flag1 = +24/0x02 was first measured in W48; Flag10 = +25/0x04 in wave 23.
 * The 11-17 group starts at +33, not +26: the two seven-bit groups are not
 * contiguous.)  The reader names the field Flag10 (0x0b400051), not
 * "summary_flag": Project's SUMMARY column is just Flag10's title. */
constexpr std::size_t kFixedMetaFlagGroupBytes[4] = {24, 25, 33, 34};
struct FlagBitPosition {
    std::size_t group;  // index into kFixedMetaFlagGroupBytes
    std::uint8_t mask;
};
constexpr FlagBitPosition kFixedMetaFlagBits[20] = {
    {0, 0x02}, {0, 0x04}, {0, 0x08}, {0, 0x10}, {0, 0x20}, {0, 0x40}, {0, 0x80},
    {1, 0x01}, {1, 0x02}, {1, 0x04},
    {2, 0x02}, {2, 0x04}, {2, 0x08}, {2, 0x10}, {2, 0x20}, {2, 0x40}, {2, 0x80},
    {3, 0x01}, {3, 0x02}, {3, 0x04},
};
constexpr std::size_t kFlagCountMpp14 = 20;
/* FixedMeta entry byte +0 is the row's state: 0x00 for a live row, 0x02 for a
 * tombstone.  MEASURED 2026-10-22 (W45): corpus/probe/w10-adddel-back.mpp is
 * Project's own file after Tasks.Add and then a delete; the deleted row keeps
 * its FixedData slot, its Fixed2Data record and all 8 of its VarMeta entries,
 * and only this byte changes 0x00 -> 0x02 (with the variable-field count at +2
 * cleared to 0).  Project reports 5 tasks for it, not 6.  PROJECT_B stores 0x02 on
 * exactly its 12 deleted slots (0x00 on the other 74); PROJECT_A and every other probe
 * file store 0x00 on all of their rows.  Liveness must use this flag, not the
 * presence of a VarMeta group -- see the liveness block in parse_document. */
constexpr std::size_t kFixedMetaStateByte = 0;
constexpr std::uint8_t kFixedMetaStateDeleted = 0x02;
/* Per-row state-report sentinels for fixed_meta_coverage.  kStateNoMetaStream
 * means "there is no readable FixedMeta, so the flag cannot decide"; the VarMeta
 * group alone decides liveness then, as it did before W45. */
constexpr std::int16_t kStateNoMetaEntry = -1;
constexpr std::int16_t kStateNoMetaStream = -2;

/* ---- MPP12 (Project 2007) geometry.  Every constant below is measured on the
 * fixtures named in the file comment; the row layouts are listed there.  The
 * three descriptors and the 16-byte null-task block are MPP12's own features,
 * not MPP14's, so they are separate constants rather than parameters. */
constexpr std::size_t k12TaskRowSize = 264;
constexpr std::size_t k12TaskNullRowSize = 16; /* FixedMeta state 0x04, no name */
constexpr std::size_t k12TaskUid = 0;
constexpr std::size_t k12TaskId = 4;
constexpr std::size_t k12TaskFreeSlack = 24;
constexpr std::size_t k12TaskStartSlack = 28;
/* The third stored slack word.  MPXJ calls it FinishSlack; it is exposed as
 * total_slack because MPP12 has no separate total word and the two coincide on
 * an unconstrained task (see "MPP12 OUTLINE" in the file comment). */
constexpr std::size_t k12TaskTotalSlack = 32;
constexpr std::size_t k12TaskParentUid = 36;
constexpr std::size_t k12TaskOutlineLevel = 40;
constexpr std::size_t k12TaskDuration = 60;
constexpr std::size_t k12TaskDurationFormat = 64; /* PjFormatUnit (+0x20 est.) */
constexpr std::size_t k12TaskActualDuration = 66;
constexpr std::size_t k12TaskConstraintType = 80;
constexpr std::size_t k12TaskStart = 88;
constexpr std::size_t k12TaskFinish = 92;
constexpr std::size_t k12TaskActualStart = 96;
constexpr std::size_t k12TaskActualFinish = 100;
/* The stored late pair (the same fields as MPP14's +112/+116, at different row
 * offsets).  The MPP12 row layout in the file comment lists +12 LateStart and
 * +152 LateFinish; the coherence check on disk is mpp12task.mpp's zero-slack
 * task, where +12 == +88 (start) and +152 == +92 (finish), while
 * mpp12duration.mpp's rows store a distinct late start and the project finish at
 * +152.  No COM read-back of an MPP12 file exists, so these offsets rest on
 * that documented layout (itself cross-checked against MPXJ's MPP12 reader). */
constexpr std::size_t k12TaskLateStart = 12;
constexpr std::size_t k12TaskLateFinish = 152;
constexpr std::size_t k12TaskConstraintDate = 112;
constexpr std::size_t k12TaskPriority = 120;
constexpr std::size_t k12TaskPercent = 122;
constexpr std::size_t k12TaskCalendarUid = 160;
constexpr std::size_t k12TaskDeadline = 164;
/* Milestone is a metadata bit, as in MPP14, at a different place. */
constexpr std::size_t k12FixedMetaMilestoneByte = 8;
constexpr std::uint8_t k12FixedMetaFlagMilestone = 0x20;
/* Flag1..Flag20 are a contiguous bit field starting at entry byte 37 bit 0x20
 * (measured on tf2007-mpp12.mpp: Flag1 = byte 37/0x20 ... Flag20 = byte 40/0x01). */
constexpr std::size_t k12FixedMetaFlagFirstByte = 37;
constexpr unsigned k12FixedMetaFlagFirstBitIndex = 5; /* 0x20 in the first byte */

constexpr std::size_t k12RscRowSize = 188;
constexpr std::size_t k12RscUid = 0;
constexpr std::size_t k12RscId = 4;
constexpr std::size_t k12RscMaxUnits = 44; /* double, x100 */
constexpr std::size_t k12RscFixedMetaWorkByte = 9;
constexpr std::uint8_t k12RscFixedMetaWorkFlag = 0x02;
constexpr std::uint8_t k12RscFixed2MetaCostFlag = 0x10;

constexpr std::size_t k12ConsRowSize = 20; /* same as MPP14 */
constexpr std::size_t k12ConsType = 12;
constexpr std::size_t k12ConsLagUnits = 14; /* SWAPPED vs MPP14: units first */
constexpr std::size_t k12ConsLag = 16;

constexpr std::size_t k12AssnRowSize = 142;
constexpr std::size_t k12AssnUid = 0;
constexpr std::size_t k12AssnTaskUid = 4;
constexpr std::size_t k12AssnResourceUid = 8;
constexpr std::size_t k12AssnStart = 12;
constexpr std::size_t k12AssnFinish = 16;
constexpr std::size_t k12AssnUnits = 54; /* double, x100 */
constexpr std::size_t k12AssnWork = 62;  /* double, minutes x1000 */

/* Calendar table: both formats, 64-byte header then 12-byte rows.  core/cal
 * documents the same geometry; the reader only needs uid + name. */
constexpr std::size_t kCalHeaderSize = 64;
constexpr std::size_t kCalRowSize = 12;
constexpr std::uint32_t kFieldCalName = 0x0D400001u;

/* Task VarMeta key for the inserted-project UniqueID offset (field id 458,
 * PjTaskSubprojectTasksUniqueIDOffset). */
constexpr std::uint32_t kFieldSubprojectUidOffset = 0x0B4001CAu;

/* ---- Top-level props record keys (Props12/Props14 share the block layout). */
constexpr std::uint32_t kPropsPasswordFlag = 0x35400000u;
constexpr std::uint32_t kPropsProtectionHash = 0x35400004u;
constexpr std::uint32_t kPropsWriteReservationHash = 0x35400005u;
constexpr std::uint32_t kPropsEncryptionCode = 0x35400007u;
constexpr std::uint32_t kPropsVersion = 0x3540000Cu;
/* Project props (112/114/Props): the inserted-project table and its counts. */
constexpr std::uint32_t kProjectPropsSubprojectData = 0x024000A2u;
constexpr std::uint32_t kProjectPropsSubprojectCount = 0x02400084u;
/* Project props: the project STATUS DATE (Project > Project Information >
 * Status date).  Kind 4, one packed date; 0xffffffff means no status date.
 * Measured on the W28-1.3 save pairs; see ProjectRow in reader.h. */
constexpr std::uint32_t kProjectPropsStatusDate = 0x02400045u;
/* Project props: Project's internal property QNEWTASKSAREMANUAL -- File >
 * Options > Schedule > "New tasks: Manually Scheduled / Auto Scheduled".
 * Kind 2 (u16), 0 = Auto Scheduled, nonzero = Manually Scheduled.  The id is
 * derived from Project's own property-name table (index 64 of the name list in
 * decompiled/part_0000.c FUN_1401b8a70, whose record id is 0x02400000 + 5000
 * + index) and measured on both sides: the corpus files whose MSPDI export
 * says <NewTasksAreManual>0 store 0 and those that say 1 store 1.  See
 * ProjectRow in reader.h for the full evidence. */
constexpr std::uint32_t kProjectPropsNewTasksManual = 0x024013C8u;
/* Project VIEW props (214/212/Props) decoded in wave 24.  Both are measured
 * record ids, not names from the decompiled table:
 *   0x03400009  int16   project-summary row shown (1) / hidden (0)
 *   0x03400008  string  top pane view name: "Time&line" when the Timeline strip
 *                       is shown, empty when it is hidden.
 * The evidence (fresh Project 2021 save pairs, every CFB stream diffed) is in
 * ProjectRow in reader.h. */
constexpr std::uint32_t kProjectPropsSummaryRow = 0x03400009u;
constexpr std::uint32_t kProjectPropsTimelinePane = 0x03400008u;

/* Task.Name: COM's PjFieldTaskName, the same id in analysis/field_id_map.tsv.
 * Evidence in the file header comment above. */
constexpr std::uint32_t kFieldTaskName = 0x0B40000Eu;

/* Real files add a second `mid`/uid family, 0x80000000 | uid (W1 §5.2). */
constexpr std::uint32_t kSecondaryUidFamily = 0x80000000u;

/* ---- Bounds-checked little-endian accessors.  A malformed stream must yield
 * zeroes and a warning, never a read past the end. */
std::uint32_t u32_at(const std::vector<std::uint8_t>& buffer, std::size_t offset) {
    if (offset + 4 > buffer.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(buffer[offset]) |
           (static_cast<std::uint32_t>(buffer[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(buffer[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(buffer[offset + 3]) << 24);
}

std::uint16_t u16_at(const std::vector<std::uint8_t>& buffer, std::size_t offset) {
    if (offset + 2 > buffer.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(buffer[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(buffer[offset + 1]) << 8));
}

/* Little-endian IEEE-754 double.  Used for MaxUnits (+8 of a 172-byte resource
 * row, x100) and for Units (+12) and Work (+20) of a 110-byte assignment row,
 * which are not 8-byte aligned in the serialized record. */
double f64_at(const std::vector<std::uint8_t>& buffer, std::size_t offset) {
    if (offset + 8 > buffer.size()) {
        return 0.0;
    }
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        bits |= static_cast<std::uint64_t>(buffer[offset + i]) << (8 * i);
    }
    double value = 0.0;
    static_assert(sizeof value == sizeof bits, "IEEE-754 double expected");
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

/* ---- Proleptic Gregorian calendar arithmetic (Howard Hinnant's algorithms).
 * Packed dates count days from 1983-12-31 (day 15346 = 2026-01-05, W1 §3). */
struct CivilDate {
    int year = 1970;
    unsigned month = 1;
    unsigned day = 1;
};

constexpr long long days_from_civil(int year, unsigned month, unsigned day) {
    year -= static_cast<int>(month <= 2);
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned month_prime = month > 2u ? month - 3u : month + 9u;
    const unsigned day_of_year = (153u * month_prime + 2u) / 5u + day - 1u;
    const unsigned day_of_era =
        year_of_era * 365u + year_of_era / 4u - year_of_era / 100u + day_of_year;
    return static_cast<long long>(era) * 146097LL + static_cast<long long>(day_of_era) -
           719468LL;
}

CivilDate civil_from_days(long long days) {
    days += 719468LL;
    const long long era = (days >= 0 ? days : days - 146096LL) / 146097LL;
    const unsigned day_of_era = static_cast<unsigned>(days - era * 146097LL);
    const unsigned year_of_era =
        (day_of_era - day_of_era / 1460u + day_of_era / 36524u - day_of_era / 146096u) /
        365u;
    const long long year = static_cast<long long>(year_of_era) + era * 400LL;
    const unsigned day_of_year =
        day_of_era - (365u * year_of_era + year_of_era / 4u - year_of_era / 100u);
    const unsigned month_prime = (5u * day_of_year + 2u) / 153u;
    const unsigned day = day_of_year - (153u * month_prime + 2u) / 5u + 1u;
    const unsigned month = month_prime < 10u ? month_prime + 3u : month_prime - 9u;
    CivilDate out;
    out.year = static_cast<int>(year + (month <= 2 ? 1 : 0));
    out.month = month;
    out.day = day;
    return out;
}

const long long kPackedDateEpochDay = days_from_civil(1983, 12, 31);

/* ---- Var2Data blocks: u32 byte length, then payload (W1 §5.3). */
bool read_block(const std::vector<std::uint8_t>& var2, std::uint32_t offset,
                std::vector<std::uint8_t>& payload) {
    if (offset > var2.size() || var2.size() - offset < 4) {
        return false;
    }
    const std::uint32_t length = u32_at(var2, offset);
    if (static_cast<std::uint64_t>(length) + 4u > var2.size() - offset) {
        return false;
    }
    payload.assign(var2.begin() + static_cast<std::ptrdiff_t>(offset) + 4,
                   var2.begin() + static_cast<std::ptrdiff_t>(offset) + 4 +
                       static_cast<std::ptrdiff_t>(length));
    return true;
}

/* A conservative "this is an ASCII UTF-16LE string" test, used only to explain
 * a dead row's orphaned blocks, never to read a live field. */
bool looks_like_utf16_ascii(const std::uint8_t* data, std::size_t bytes) {
    if (bytes < 8 || bytes % 2 != 0 || data[bytes - 1] != 0) {
        return false;
    }
    std::size_t printable = 0;
    for (std::size_t i = 0; i + 1 < bytes; i += 2) {
        if (data[i + 1] != 0) {
            return false;
        }
        const std::uint8_t ch = data[i];
        if (ch == 0) {
            break; /* the terminator; anything after it is padding */
        }
        if (ch < 0x20 || ch > 0x7E) {
            return false;
        }
        ++printable;
    }
    return printable >= 4;
}

std::string decode_utf16_block(const std::vector<std::uint8_t>& payload) {
    return utf16le_to_utf8(payload.data(), payload.size());
}

std::string hex32(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%08x", value);
    return std::string(buffer);
}

/* ---- VarMeta: 24-byte header + n * (uid, offset, key) ----------------- */
struct VarEntry {
    std::uint32_t uid = 0;
    std::uint32_t offset = 0;
    std::uint32_t key = 0;
};

struct VarTable {
    std::vector<VarEntry> entries;
    std::unordered_map<std::uint64_t, std::uint32_t> first_offset; /* (uid,key) -> offset */
    std::unordered_map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>>
        group_span; /* uid -> [begin, end) over readable blocks */
    bool usable = false;
};

std::uint64_t var_key(std::uint32_t uid, std::uint32_t key) {
    return (static_cast<std::uint64_t>(uid) << 32) | static_cast<std::uint64_t>(key);
}

std::uint32_t block_end(const std::vector<std::uint8_t>& var2, std::uint32_t offset) {
    if (offset > var2.size() || var2.size() - offset < 4) {
        return offset;
    }
    const std::uint32_t length = u32_at(var2, offset);
    const std::uint64_t end = static_cast<std::uint64_t>(offset) + 4u + length;
    if (end > var2.size()) {
        return offset;
    }
    return static_cast<std::uint32_t>(end);
}

VarTable parse_var_meta(const std::vector<std::uint8_t>& var_meta,
                        const std::vector<std::uint8_t>& var2,
                        std::string_view label,
                        std::vector<std::string>& warnings) {
    VarTable table;
    if (var_meta.empty()) {
        return table;
    }
    if (var_meta.size() < kVarMetaHeaderSize) {
        warnings.emplace_back(std::string(label) + " VarMeta is " +
                              std::to_string(var_meta.size()) +
                              " bytes, too short for its 24-byte header; " +
                              std::string(label) + " variable values will be missing");
        return table;
    }
    if (u32_at(var_meta, 0) != kMetaMagic) {
        warnings.emplace_back(std::string(label) + " VarMeta has magic " +
                              hex32(u32_at(var_meta, 0)) +
                              " instead of 0xfadfadba; " + std::string(label) +
                              " variable values will be missing");
        return table;
    }
    const std::uint32_t declared = u32_at(var_meta, 8);
    const std::uint32_t declared_var2_size = u32_at(var_meta, kVarMetaSizeWord);
    const std::size_t body = var_meta.size() - kVarMetaHeaderSize;
    const std::size_t count = body / kVarMetaEntrySize;
    if (body % kVarMetaEntrySize != 0) {
        warnings.emplace_back(std::string(label) + " VarMeta is " +
                              std::to_string(var_meta.size()) +
                              " bytes, not 24 + 12*n; trailing bytes ignored");
    }
    if (declared != count) {
        std::string message =
            std::string(label) + " VarMeta header declares " + std::to_string(declared) +
            " entries but the stream holds " + std::to_string(count);
        if (label == "Task") {
            message += " (PROJECT_A is damaged this way: 1,076 vs 1,081)";
        }
        message += "; the stream wins";
        warnings.emplace_back(std::move(message));
    }
    if (declared_var2_size != var2.size()) {
        warnings.emplace_back(std::string(label) + " VarMeta says Var2Data is " +
                              std::to_string(declared_var2_size) + " bytes, the stream is " +
                              std::to_string(var2.size()));
    }

    table.entries.reserve(count);
    std::size_t invalid_offsets = 0;
    std::size_t duplicates = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t base = kVarMetaHeaderSize + i * kVarMetaEntrySize;
        VarEntry entry;
        entry.uid = u32_at(var_meta, base);
        entry.offset = u32_at(var_meta, base + 4);
        entry.key = u32_at(var_meta, base + 8);
        table.entries.push_back(entry);

        const std::uint64_t key = var_key(entry.uid, entry.key);
        const auto inserted = table.first_offset.emplace(key, entry.offset);
        if (!inserted.second) {
            ++duplicates;
        }
        if (static_cast<std::uint64_t>(entry.offset) + 4u > var2.size()) {
            /* PROJECT_A has 101 such entries, all in the 0x80000000|uid family, with
             * offset 0xffffffff: "this value is not stored". */
            ++invalid_offsets;
            continue;
        }
        const std::uint32_t end = block_end(var2, entry.offset);
        auto span = table.group_span.find(entry.uid);
        if (span == table.group_span.end()) {
            table.group_span.emplace(entry.uid, std::make_pair(entry.offset, end));
        } else {
            span->second.first = std::min(span->second.first, entry.offset);
            span->second.second = std::max(span->second.second, end);
        }
    }
    if (invalid_offsets > 0) {
        warnings.emplace_back("VarMeta holds " + std::to_string(invalid_offsets) +
                              " entries whose Var2Data offset is past the end of the "
                              "stream (0xffffffff: not stored); they are ignored");
    }
    if (duplicates > 0) {
        warnings.emplace_back("VarMeta repeats " + std::to_string(duplicates) +
                              " (uid, key) pairs; the first offset for each is used");
    }
    table.usable = true;
    return table;
}

/* Offsets for (uid, key), trying the normal and the 0x80000000|uid family. */
std::vector<std::uint32_t> offsets_for(const VarTable& table, std::uint32_t uid,
                                       std::uint32_t key) {
    std::vector<std::uint32_t> found;
    for (const std::uint32_t family_uid : {uid, uid | kSecondaryUidFamily}) {
        const auto it = table.first_offset.find(var_key(family_uid, key));
        if (it != table.first_offset.end()) {
            found.push_back(it->second);
        }
    }
    return found;
}

/*
 * MS Project stores names with whatever trailing spaces the user typed (PROJECT_A
 * row 1 stores its client task name with one trailing space -- not quoted) but
 * its COM surface returns the trimmed string, and tests/data/project_a_golden.tsv
 * was exported that way.  The
 * reader trims ASCII spaces so its output is the name a user sees; without
 * this, the golden comparison disagrees on that one task and only that one.
 */
std::string trim_ascii_spaces(std::string text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && text[begin] == ' ') {
        ++begin;
    }
    while (end > begin && text[end - 1] == ' ') {
        --end;
    }
    return text.substr(begin, end - begin);
}

/* Reads one UTF-16LE text block keyed by (uid, field id).  Callers keep it
 * verbatim in *_raw and the trimmed form for display; trimming happens in the
 * caller so a writer can round-trip the stored bytes.  Used for Task.Name
 * (0x0b40000e), Resource.Name (0x0c400001) and Resource.Initials (0x0c400002). */
bool read_text_field(const VarTable& table, const std::vector<std::uint8_t>& var2,
                     std::uint32_t uid, std::uint32_t key, std::string& raw) {
    for (const std::uint32_t offset : offsets_for(table, uid, key)) {
        std::vector<std::uint8_t> payload;
        if (read_block(var2, offset, payload)) {
            raw = decode_utf16_block(payload);
            return true;
        }
    }
    return false;
}

/* ---- FixedMeta is per-row bookkeeping, not a field map (W1 §1.1).  It is
 * never indexed by row: its entries are a list of FixedData offsets, and PROJECT_A
 * both drops rows and repeats offsets.  The useful product is the set of rows
 * that have a record at all.  The four tables share the 16-byte header but not
 * the entry size or the number of header descriptors (Task/Resource have three
 * 16-byte slots before the first row; Link/Assignment have none).
 *
 * `entry_offsets`, when given, receives one FixedData offset per FixedMeta entry
 * (indexed by entry number, so index i is entry i) so a caller can pair entry i
 * with Fixed2Meta entry i; entries that failed validation get kNoOffset.  The
 * Task table uses that to attach each row's Fixed2Data outline record. */
constexpr std::uint32_t kNoOffset = 0xFFFFFFFFu;

std::vector<bool> fixed_meta_coverage(const CompoundFile& cfb, std::string_view root,
                                      std::string_view table,
                                      std::string_view label, std::size_t header_size,
                                      std::size_t row_size, std::size_t entry_size,
                                      std::size_t descriptors, std::uint32_t rows,
                                      const std::vector<std::uint32_t>& uid_by_row,
                                      std::vector<std::string>& warnings,
                                      std::vector<std::uint32_t>* entry_offsets = nullptr,
                                      std::vector<std::uint8_t>* row_flag_byte = nullptr,
                                      std::vector<std::int16_t>* row_state = nullptr,
                                      std::vector<std::array<std::uint8_t, 4>>*
                                          row_flag_group = nullptr,
                                      std::vector<std::uint8_t>* row_flag10_byte = nullptr,
                                      std::vector<std::uint8_t>* row_type_flag = nullptr) {
    std::vector<bool> covered(rows, false);
    if (row_flag_byte != nullptr) {
        row_flag_byte->assign(rows, 0);
    }
    if (row_flag_group != nullptr) {
        row_flag_group->assign(rows, std::array<std::uint8_t, 4>{0, 0, 0, 0});
    }
    if (row_flag10_byte != nullptr) {
        row_flag10_byte->assign(rows, 0);
    }
    if (row_type_flag != nullptr) {
        row_type_flag->assign(rows, 0);
    }
    if (row_state != nullptr) {
        row_state->assign(rows, kStateNoMetaEntry);
    }
    const std::string path = std::string(root) + "/" + std::string(table) + "/FixedMeta";
    if (!cfb.has_stream(path)) {
        warnings.emplace_back(std::string(label) +
                              " table has no FixedMeta stream; every row is "
                              "assumed live");
        /* Make that assumption true: a table without metadata has no marker for
         * a deleted row, and returning all-false would drop the whole table
         * (Link and Assignment liveness read this vector). */
        std::fill(covered.begin(), covered.end(), true);
        if (row_state != nullptr) {
            std::fill(row_state->begin(), row_state->end(), kStateNoMetaStream);
        }
        return covered;
    }
    std::vector<std::uint8_t> meta;
    try {
        meta = cfb.read_stream(path);
    } catch (const Error& error) {
        warnings.emplace_back(std::string(label) + " FixedMeta is unreadable: " +
                              error.what());
        if (row_state != nullptr) {
            std::fill(row_state->begin(), row_state->end(), kStateNoMetaStream);
        }
        return covered;
    }
    if (meta.size() < kFixedMetaHeaderSize) {
        warnings.emplace_back(std::string(label) + " FixedMeta is " +
                              std::to_string(meta.size()) +
                              " bytes, too short for its 16-byte header");
        if (row_state != nullptr) {
            std::fill(row_state->begin(), row_state->end(), kStateNoMetaStream);
        }
        return covered;
    }
    if (u32_at(meta, 0) != kMetaMagic) {
        warnings.emplace_back(std::string(label) + " FixedMeta has magic " +
                              hex32(u32_at(meta, 0)) +
                              " instead of 0xfadfadba; its contents are not checked");
        if (row_state != nullptr) {
            std::fill(row_state->begin(), row_state->end(), kStateNoMetaStream);
        }
        return covered;
    }
    const std::uint32_t declared = u32_at(meta, 8);
    const std::size_t count = (meta.size() - kFixedMetaHeaderSize) / entry_size;
    if ((meta.size() - kFixedMetaHeaderSize) % entry_size != 0) {
        warnings.emplace_back(std::string(label) + " FixedMeta is " +
                              std::to_string(meta.size()) + " bytes, not 16 + " +
                              std::to_string(entry_size) + "*n; trailing bytes ignored");
    }
    if (declared != count) {
        std::string message = std::string(label) + " FixedMeta header declares " +
                              std::to_string(declared) +
                              " entries but the stream holds " + std::to_string(count);
        if (label == "Task") {
            message += " (PROJECT_A is damaged this way: 87 vs 88)";
        }
        warnings.emplace_back(std::move(message));
    }
    if (count != static_cast<std::size_t>(rows) + descriptors) {
        warnings.emplace_back(std::string(label) + " FixedMeta holds " +
                              std::to_string(count) + " entries; " +
                              std::to_string(rows) + " rows + " +
                              std::to_string(descriptors) +
                              " header descriptors would be " +
                              std::to_string(rows + descriptors));
    }
    std::unordered_map<std::uint32_t, std::size_t> offset_owner;
    if (entry_offsets != nullptr) {
        entry_offsets->assign(count, kNoOffset);
    }
    for (std::size_t i = descriptors; i < count; ++i) {
        const std::size_t base = kFixedMetaHeaderSize + i * entry_size;
        const std::uint32_t offset = u32_at(meta, base + 4);
        if (offset < header_size || (offset - header_size) % row_size != 0) {
            warnings.emplace_back(std::string(label) + " FixedMeta entry " +
                                  std::to_string(i) + " has the non-row offset " +
                                  std::to_string(offset) + "; ignored");
            continue;
        }
        const std::uint32_t row =
            (offset - static_cast<std::uint32_t>(header_size)) /
            static_cast<std::uint32_t>(row_size);
        if (row >= rows) {
            warnings.emplace_back(std::string(label) + " FixedMeta entry " +
                                  std::to_string(i) + " points at row " +
                                  std::to_string(row) + ", past the last row " +
                                  std::to_string(rows - 1));
            continue;
        }
        if (entry_offsets != nullptr) {
            (*entry_offsets)[i] = offset;
        }
        if (row_flag_byte != nullptr && base + kFixedMetaMilestoneByte < meta.size()) {
            (*row_flag_byte)[row] = meta[base + kFixedMetaMilestoneByte];
        }
        if (row_type_flag != nullptr && base + kRscFixedMetaTypeByte < meta.size()) {
            (*row_type_flag)[row] = meta[base + kRscFixedMetaTypeByte];
        }
        if (row_flag_group != nullptr) {
            for (std::size_t group = 0; group < 4; ++group) {
                const std::size_t byte = kFixedMetaFlagGroupBytes[group];
                if (base + byte < meta.size()) {
                    (*row_flag_group)[row][group] = meta[base + byte];
                }
            }
        }
        if (row_flag10_byte != nullptr &&
            base + kFixedMetaFlag10Byte < meta.size()) {
            (*row_flag10_byte)[row] = meta[base + kFixedMetaFlag10Byte];
        }
        const auto previous = offset_owner.find(offset);
        if (previous != offset_owner.end()) {
            warnings.emplace_back(std::string(label) + " FixedMeta entry " +
                                  std::to_string(i) + " repeats offset " +
                                  std::to_string(offset) + " (row " +
                                  std::to_string(row) + "), already used by entry " +
                                  std::to_string(previous->second));
            continue;
        }
        offset_owner.emplace(offset, i);
        covered[row] = true;
        if (row_state != nullptr) {
            (*row_state)[row] = static_cast<std::int16_t>(meta[base + kFixedMetaStateByte]);
        }
    }
    for (std::uint32_t row = 0; row < rows; ++row) {
        if (!covered[row]) {
            warnings.emplace_back(std::string(label) + " FixedMeta has no entry for row " +
                                  std::to_string(row) + " (uid " +
                                  std::to_string(uid_by_row[row]) + ")");
        }
    }
    return covered;
}

/* For a dead row, the name block left behind in the gap between the groups of
 * its immediate neighbours.  Evidence only: it is never returned as a task.
 * Requiring both immediate neighbours to have groups is deliberate -- in
 * PROJECT_B a whole run of rows is dead, and any looser rule would attribute one
 * orphaned name block to every row in the run. */
std::string orphan_name_for_row(const VarTable& table,
                                const std::vector<std::uint8_t>& var2,
                                std::uint32_t row,
                                const std::vector<std::uint32_t>& uid_by_row) {
    if (row == 0 || static_cast<std::size_t>(row) + 1 >= uid_by_row.size()) {
        return {};
    }
    const auto previous = table.group_span.find(uid_by_row[row - 1]);
    if (previous == table.group_span.end()) {
        return {};
    }
    const auto next = table.group_span.find(uid_by_row[row + 1]);
    if (next == table.group_span.end()) {
        return {};
    }
    const std::uint32_t begin = previous->second.second;
    const std::uint32_t end = next->second.first;
    if (begin >= end) {
        return {};
    }
    std::vector<std::uint8_t> payload;
    if (!read_block(var2, begin, payload)) {
        return {};
    }
    if (begin + 4 + payload.size() > end) {
        return {};
    }
    if (!looks_like_utf16_ascii(payload.data(), payload.size())) {
        return {};
    }
    return decode_utf16_block(payload);
}

std::vector<std::uint8_t> read_optional_stream(const CompoundFile& cfb, std::string_view root,
                                               std::string_view table,
                                               std::string_view name,
                                               std::vector<std::string>& warnings) {
    const std::string path = std::string(root) + "/" + std::string(table) + "/" +
                             std::string(name);
    if (!cfb.has_stream(path)) {
        warnings.emplace_back(std::string(root) + "/" + std::string(table) + " has no " +
                              std::string(name) + " stream; reading without it");
        return {};
    }
    try {
        return cfb.read_stream(path);
    } catch (const Error& error) {
        warnings.emplace_back(std::string(table) + "/" + std::string(name) +
                              " is unreadable: " + error.what());
        return {};
    }
}

/* Reads an optional FixedData table.  Returns false when the stream is absent,
 * unreadable or has no complete row.  For Link and Assignment header_size is 0
 * and rows == 0 is a valid empty table (no links / no assignments). */
bool read_table_fixed(const CompoundFile& cfb, std::string_view root, std::string_view table,
                      std::string_view label, std::size_t header_size,
                      std::size_t row_size, std::vector<std::uint8_t>& fixed,
                      std::uint32_t& rows, std::vector<std::string>& warnings) {
    const std::string path = std::string(root) + "/" + std::string(table) + "/FixedData";
    rows = 0;
    if (!cfb.has_stream(path)) {
        warnings.emplace_back("the " + std::string(label) +
                              " table has no FixedData stream; skipped");
        return false;
    }
    try {
        fixed = cfb.read_stream(path);
    } catch (const Error& error) {
        warnings.emplace_back(std::string(label) + " FixedData is unreadable: " +
                              error.what() + "; the table is skipped");
        return false;
    }
    if (fixed.size() < header_size) {
        warnings.emplace_back(std::string(label) + " FixedData is " +
                              std::to_string(fixed.size()) +
                              " bytes, smaller than its " + std::to_string(header_size) +
                              "-byte header; the table is skipped");
        return false;
    }
    std::size_t body = fixed.size() - header_size;
    if (body % row_size != 0) {
        warnings.emplace_back(std::string(label) + " FixedData is " +
                              std::to_string(fixed.size()) + " bytes = " +
                              std::to_string(header_size) + " + " +
                              std::to_string(row_size) + "*n + " +
                              std::to_string(body % row_size) +
                              "; the trailing bytes are ignored");
        body -= body % row_size;
    }
    rows = static_cast<std::uint32_t>(body / row_size);
    return true;
}

std::vector<std::uint32_t> uids_by_row(const std::vector<std::uint8_t>& fixed,
                                       std::size_t header_size, std::size_t row_size,
                                       std::uint32_t rows, std::size_t uid_offset) {
    std::vector<std::uint32_t> uids(rows, 0);
    for (std::uint32_t row = 0; row < rows; ++row) {
        uids[row] = u32_at(fixed, header_size + row * row_size + uid_offset);
    }
    return uids;
}

/* A duplicate UniqueID is not fatal (a damaged file can still carry the rows)
 * but it is reported: every consumer keys records by uid, so two rows with the
 * same uid are ambiguous.  `skip_first` lets the task table leave row 0 (the
 * project row, whose uid is 0) out of the check. */
void warn_duplicate_uids(const std::vector<std::uint32_t>& uids, std::string_view label,
                         bool skip_first, std::vector<std::string>& warnings) {
    std::unordered_map<std::uint32_t, std::uint32_t> seen;
    seen.reserve(uids.size());
    for (std::size_t row = skip_first ? 1u : 0u; row < uids.size(); ++row) {
        std::uint32_t& count = seen[uids[row]];
        ++count;
        if (count == 2) {
            warnings.emplace_back(std::string(label) + " UniqueID " +
                                  std::to_string(uids[row]) +
                                  " appears on more than one row; records keyed by uid "
                                  "are ambiguous");
        }
    }
}

/* ---- Outline hierarchy (Fixed2Data).  See "OUTLINE" in reader.h for the byte
 * evidence.  Fixed2Meta entry i describes the same row as FixedMeta entry i, so
 * the pairing goes through the two metadata streams' entry *index*, not the row
 * index: PROJECT_A's row 67 has no entry in either stream and its FixedMeta header
 * count is stale, which is exactly the case a row-indexed read would get wrong.
 * Everything below degrades to a flat outline plus a warning, never to a guess.
 *
 * The same pass reads Fixed2Meta's per-entry flags byte, whose bit 0x80 is
 * Project's "manually scheduled" task mode -- needed because a manually
 * scheduled task is never reported as critical (base.mpp's Task A: TotalSlack 0,
 * Critical false, Manual true in docs/analysis/oracle/writer-base.json).
 * MEASURED (coordinator oracle, 2026-10-22, wave 9): corpus/probe/
 * w37-snet-auto.mpp is base.mpp with six bytes changed -- FixedData row 1
 * +64/+66 set to SNET 2026-10-21 08:00 AND Fixed2Meta stream offset 408
 * (entry 4, byte +8: 0xff -> 0x7f, bit 0x80 cleared).  Project opens it with
 * uid 1 Manual=False, Start 2026-10-21 08:00, Finish 2026-10-27 17:00,
 * Duration 2400, ConstraintType=4, while uids 2/3 stay Manual=True.  The
 * otherwise identical w37-snet-save.mpp (bit left set) opens with uid 1
 * Manual=True and Start unchanged at 2026-01-04 13:00.  Same constraint,
 * different bit, different scheduling: the bit is the mode switch.
 * Supporting rows: base.mpp/resaved-base.mpp set it on rows 1..3 (the three
 * writer-base.json Manual=true tasks) and clear row 0; w3-o1.mpp and
 * outline1.mpp clear it on every row (Manual=false); PROJECT_B sets it on 3 of its
 * 86 rows (42, 55, 85), so it is per-row, not a file-level save mode; no
 * VarMeta key 0x0b400500 exists in any corpus file and no byte of the 202-byte
 * row splits manual from auto. */
struct Outline {
    std::vector<std::uint32_t> level;        /* per FixedData row; 0 = project row */
    std::vector<std::string> number;         /* per row; "" for the project row */
    std::vector<std::uint32_t> child_count;  /* per row */
    std::vector<bool> manually_scheduled;    /* per row; see above */
    bool usable = false;
};

Outline read_outline(const CompoundFile& cfb, std::string_view root, std::uint32_t rows,
                     const std::vector<std::uint32_t>& id_by_row,
                     const std::vector<std::uint32_t>& meta_offsets,
                     const std::vector<bool>& live_row,
                     std::vector<std::string>& warnings) {
    Outline outline;
    outline.level.assign(rows, 0);
    outline.number.assign(rows, std::string());
    outline.child_count.assign(rows, 0);
    outline.manually_scheduled.assign(rows, false);
    if (rows == 0) {
        return outline;
    }

    const std::vector<std::uint8_t> fixed2 =
        read_optional_stream(cfb, root, "TBkndTask", "Fixed2Data", warnings);
    const std::vector<std::uint8_t> fixed2_meta =
        read_optional_stream(cfb, root, "TBkndTask", "Fixed2Meta", warnings);
    if (fixed2.size() < kFixed2RowSize || fixed2_meta.size() < kFixedMetaHeaderSize ||
        u32_at(fixed2_meta, 0) != kMetaMagic) {
        warnings.emplace_back(
            "the Task table has no usable Fixed2Data/Fixed2Meta pair; OutlineLevel, "
            "OutlineNumber and Summary are reported as a flat outline (level 1, "
            "numbered in row order, nothing a summary) and are not the file's outline");
        return outline;
    }

    using RowId = std::array<std::uint8_t, 16>;
    const auto read_id = [&fixed2](std::uint32_t record, std::size_t offset) {
        RowId id{};
        for (std::size_t i = 0; i < id.size(); ++i) {
            id[i] = fixed2[record + offset + i];
        }
        return id;
    };
    const RowId no_id{};

    std::vector<RowId> identity(rows);
    std::vector<bool> have_record(rows, false);
    std::vector<bool> has_parent(rows, false);
    std::vector<RowId> parent(rows);
    std::vector<bool> stored_has_children(rows, false);

    const std::size_t entries =
        (fixed2_meta.size() - kFixedMetaHeaderSize) / kFixed2MetaEntrySize;
    std::size_t bad_records = 0;
    std::size_t unpaired = 0;
    for (std::size_t i = kFixedMetaDescriptors; i < entries; ++i) {
        if (i >= meta_offsets.size() || meta_offsets[i] == kNoOffset) {
            ++unpaired;
            continue;
        }
        const std::uint32_t row =
            (meta_offsets[i] - static_cast<std::uint32_t>(kFixedHeaderSize)) /
            static_cast<std::uint32_t>(kFixedRowSize);
        if (row >= rows) {
            continue;
        }
        const std::uint32_t record =
            u32_at(fixed2_meta, kFixedMetaHeaderSize + i * kFixed2MetaEntrySize + 4);
        if (static_cast<std::uint64_t>(record) + kFixed2RowSize > fixed2.size()) {
            ++bad_records;
            continue;
        }
        identity[row] = read_id(record, kF2RowId);
        if (identity[row] == no_id) {
            ++bad_records;
            continue;
        }
        have_record[row] = true;
        std::array<std::uint8_t, 16> parent_id = read_id(record, kF2RowParentId);
        has_parent[row] = parent_id != no_id;
        parent[row] = parent_id;
        stored_has_children[row] =
            (fixed2[record + kF2RowFlags] & kF2FlagHasChildren) != 0;
        outline.manually_scheduled[row] =
            (fixed2_meta[kFixedMetaHeaderSize + i * kFixed2MetaEntrySize +
                         kF2MetaFlags] &
             kF2MetaFlagManual) != 0;
    }

    std::map<RowId, std::uint32_t> row_by_id;
    for (std::uint32_t row = 0; row < rows; ++row) {
        if (have_record[row]) {
            row_by_id.emplace(identity[row], row); /* duplicates: first row wins */
        }
    }

    std::vector<std::vector<std::uint32_t>> children(rows);
    std::size_t unknown_parents = 0;
    for (std::uint32_t row = 1; row < rows; ++row) {
        /* Deleted slots keep their records and their parent links, but MS
         * Project does not number them: PROJECT_B's 12 deleted rows sit at rows
         * 1..12 under rows 3 and 5, and counting them would start the live
         * outline at "6" instead of "1".  Live rows only, therefore. */
        if (!have_record[row] || !has_parent[row] || !live_row[row]) {
            continue;
        }
        const auto found = row_by_id.find(parent[row]);
        if (found == row_by_id.end() || found->second == row || !live_row[found->second]) {
            ++unknown_parents; /* dangling, self-referencing or deleted: not a level */
            continue;
        }
        children[found->second].push_back(row);
    }
    /* Project numbers each sibling group by the ID column, not by the order the
     * rows happen to be stored in.  W28-0.2 measured this on the corpus capture
     * through COM: ID 1 is OutlineNumber "1" and ID 12 is "2", although the
     * stored row order puts ID 12's subtree first.  Sort by the +0 ID; the row
     * index is only a tie-breaker (IDs are unique in a valid file). */
    for (std::vector<std::uint32_t>& kids : children) {
        std::sort(kids.begin(), kids.end(),
                  [&id_by_row](std::uint32_t a, std::uint32_t b) {
                      if (id_by_row[a] != id_by_row[b]) {
                          return id_by_row[a] < id_by_row[b];
                      }
                      return a < b;
                  });
    }

    /* Depth-first from the project row, numbering each row by its 1-based index
     * among its siblings: "1", "1.1", "1.2", ...  An explicit stack keeps a
     * corrupt file from overflowing the C stack; rows caught in a cycle are never
     * reached and fall into the flat fallback below. */
    struct Pending {
        std::uint32_t row;
        std::uint32_t level;
        std::string prefix;
    };
    std::vector<bool> reached(rows, false);
    std::vector<Pending> stack;
    stack.push_back(Pending{0, 0, std::string()});
    while (!stack.empty()) {
        const Pending node = stack.back();
        stack.pop_back();
        if (node.row >= rows || reached[node.row]) {
            continue;
        }
        reached[node.row] = true;
        outline.level[node.row] = node.level;
        outline.number[node.row] = node.prefix;
        const std::vector<std::uint32_t>& kids = children[node.row];
        outline.child_count[node.row] = static_cast<std::uint32_t>(kids.size());
        for (std::size_t i = kids.size(); i-- > 0;) {
            const std::string part = std::to_string(i + 1);
            stack.push_back(Pending{kids[i], node.level + 1,
                                    node.prefix.empty() ? part : node.prefix + "." + part});
        }
    }

    std::size_t unreachable = 0;
    std::uint32_t next_top = static_cast<std::uint32_t>(children[0].size()) + 1;
    for (std::uint32_t row = 0; row < rows; ++row) {
        if (reached[row] || !live_row[row]) {
            continue;
        }
        ++unreachable;
        outline.level[row] = 1;
        outline.number[row] = std::to_string(next_top++);
    }

    std::size_t flag_disagreements = 0;
    for (std::uint32_t row = 0; row < rows; ++row) {
        if (live_row[row] && have_record[row] &&
            stored_has_children[row] != (outline.child_count[row] > 0)) {
            ++flag_disagreements;
        }
    }
    if (bad_records > 0) {
        warnings.emplace_back(
            "Fixed2Meta holds " + std::to_string(bad_records) +
            " entries whose Fixed2Data record is missing or blank; those rows keep a "
            "flat outline");
    }
    if (unknown_parents > 0) {
        warnings.emplace_back(
            "Fixed2Data names a parent that no row record holds on " +
            std::to_string(unknown_parents) +
            " row(s); those rows are treated as top level for OutlineLevel");
    }
    if (unreachable > 0) {
        warnings.emplace_back(
            std::to_string(unreachable) +
            " row(s) are not reachable from the project row through Fixed2Data's parent "
            "links (deleted rows and cycles are); they are given outline level 1 and "
            "numbered after the reachable top-level rows");
    }
    if (flag_disagreements > 0) {
        warnings.emplace_back(
            "Fixed2Data's has-children flag (byte +42, bit 0x40) disagrees with the "
            "parent links on " + std::to_string(flag_disagreements) +
            " row(s); Summary still follows the parent links");
    }
    if (unpaired > 0) {
        warnings.emplace_back(
            "Fixed2Meta holds " + std::to_string(unpaired) +
            " entries that no FixedMeta entry pairs with; they are ignored");
    }
    outline.usable = true;
    return outline;
}

/* MPP14 TASK RECORD STRIDE.  The Project-2010+ task fixed row is 202 bytes and
 * parse_mpp14 slices FixedData with that stride.  MPXJ's own writer emits a
 * DIFFERENT record layout in its MPP14 containers: 206-byte rows whose fields
 * are not the MPP14 offsets.  corpus/probe/SubprojectA-9.mpp is the measured
 * case: FixedData 872 = 48 + 206*4 and its FixedMeta data entries start at
 * 48,254,460,666; SubprojectB-9.mpp is 1078 = 48 + 206*5 with the same stride.
 * That is the same MPXJ variant the MPP12 reader already refuses for the
 * `*-from14.mpp` files (docs/handoff/g1.md §3.12), so it must not be decoded as
 * four 202-byte rows plus 16 trailing bytes: that path emits UniqueID 0 rows
 * with nonsense dates.  The metadata offsets are the evidence; when they
 * describe a uniform non-202 stride that consumes FixedData exactly, refuse
 * with a positioned error instead of guessing at the field offsets.
 */
struct UnsupportedTaskStride {
    std::size_t rows = 0;
    std::size_t stride = 0;
};

bool detect_unsupported_task_stride(const CompoundFile& cfb, std::string_view root,
                                    const std::vector<std::uint8_t>& fixed,
                                    UnsupportedTaskStride& out) {
    const std::string path = std::string(root) + "/TBkndTask/FixedMeta";
    if (!cfb.has_stream(path)) {
        return false;
    }
    std::vector<std::uint8_t> meta;
    try {
        meta = cfb.read_stream(path);
    } catch (const Error&) {
        return false;
    }
    if (meta.size() < kFixedMetaHeaderSize || u32_at(meta, 0) != kMetaMagic) {
        return false;
    }
    const std::size_t count =
        (meta.size() - kFixedMetaHeaderSize) / kFixedMetaEntrySize;
    if (count <= kFixedMetaDescriptors) {
        return false;
    }
    std::vector<std::uint32_t> offsets;
    offsets.reserve(count - kFixedMetaDescriptors);
    for (std::size_t i = kFixedMetaDescriptors; i < count; ++i) {
        offsets.push_back(
            u32_at(meta, kFixedMetaHeaderSize + i * kFixedMetaEntrySize + 4));
    }
    if (offsets.size() < 2 || offsets.front() != kFixedHeaderSize) {
        return false;
    }
    const std::size_t stride = offsets[1] - offsets[0];
    if (stride == 0 || stride == kFixedRowSize) {
        return false;
    }
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        if (offsets[i] < offsets[i - 1] || offsets[i] - offsets[i - 1] != stride) {
            return false;
        }
    }
    if (kFixedHeaderSize + stride * offsets.size() != fixed.size()) {
        return false;
    }
    out.rows = offsets.size();
    out.stride = stride;
    return true;
}

/* MPP14 (Project 2010+) task/resource/link/assignment tables.  `root` is the
 * table storage the container carries ("114" for every MPP14 file measured);
 * the caller has already put the format metadata in `document`. */
void parse_mpp14(const CompoundFile& cfb, std::string_view root, Document& document) {
    /* ---- Task table (required).  Geometry: 48 + 202*rows, FixedMeta 47. */
    std::vector<std::uint8_t> fixed;
    try {
        fixed = cfb.read_stream(std::string(root) + "/TBkndTask/FixedData");
    } catch (const Error& error) {
        throw Error(std::string("no Task table: ") + error.what() +
                    " (an MPP14 file stores it at " + std::string(root) +
                    "/TBkndTask/FixedData)");
    }
    const std::vector<std::uint8_t> var_meta =
        read_optional_stream(cfb, root, "TBkndTask", "VarMeta", document.warnings);
    const std::vector<std::uint8_t> var2 =
        read_optional_stream(cfb, root, "TBkndTask", "Var2Data", document.warnings);

    if (fixed.size() < kFixedHeaderSize) {
        throw Error("TBkndTask/FixedData is " + std::to_string(fixed.size()) +
                    " bytes, smaller than its 48-byte header");
    }
    /* A non-202-byte row layout is a different record format, not a shifted
     * 202-byte row; refuse it before the slice below turns it into zero-uid
     * rows with nonsense dates.  See detect_unsupported_task_stride(). */
    {
        UnsupportedTaskStride unsupported;
        if (detect_unsupported_task_stride(cfb, root, fixed, unsupported)) {
            throw Error(
                "MPP14 Task table at " + std::string(root) +
                "/TBkndTask/FixedData holds " + std::to_string(unsupported.rows) +
                " data rows of " + std::to_string(unsupported.stride) +
                " bytes each (row 0 is the project row); the Project-2010+ layout "
                "this reader implements is 202 bytes.  The file may be an "
                "MPXJ-written MPP14 or a different variant; its record size is not "
                "guessed at.");
        }
    }
    std::size_t body = fixed.size() - kFixedHeaderSize;
    if (body % kFixedRowSize != 0) {
        document.warnings.emplace_back(
            "TBkndTask/FixedData is " + std::to_string(fixed.size()) +
            " bytes = 48 + 202*n + " + std::to_string(body % kFixedRowSize) +
            "; the trailing bytes are ignored");
        body -= body % kFixedRowSize;
    }
    const std::uint32_t rows = static_cast<std::uint32_t>(body / kFixedRowSize);
    if (rows == 0) {
        document.warnings.emplace_back("TBkndTask/FixedData has no rows, not even the "
                                       "project row");
        return;
    }

    const std::vector<std::uint32_t> uid_by_row =
        uids_by_row(fixed, kFixedHeaderSize, kFixedRowSize, rows, kRowUid);
    warn_duplicate_uids(uid_by_row, "Task", true, document.warnings);

    const VarTable table = parse_var_meta(var_meta, var2, "Task", document.warnings);
    std::vector<std::uint32_t> task_meta_offsets;
    std::vector<std::uint8_t> task_meta_flag_bytes;
    std::vector<std::int16_t> task_meta_state;
    std::vector<std::array<std::uint8_t, 4>> task_flag_group;
    std::vector<std::uint8_t> task_meta_flag10_bytes;
    const bool task_has_fixed_meta = cfb.has_stream(std::string(root) +
                                                    "/TBkndTask/FixedMeta");
    const std::vector<bool> fixed_meta_live =
        fixed_meta_coverage(cfb, root, "TBkndTask", "Task", kFixedHeaderSize,
                            kFixedRowSize, kFixedMetaEntrySize, kFixedMetaDescriptors,
                            rows, uid_by_row, document.warnings, &task_meta_offsets,
                            &task_meta_flag_bytes, &task_meta_state,
                            &task_flag_group, &task_meta_flag10_bytes);
    /* Wave 24 (W24-9/R11): the project row's SUMMARY column is its OWN stored
     * Flag10 bit (FixedMeta row 0, entry +25 bit 0x04), not a constant read off
     * one capture.  A file whose row 0 does not cover the bit reports
     * not-stored, and the sheet then leaves the cell blank. */
    if (task_has_fixed_meta && !fixed_meta_live.empty() && fixed_meta_live[0] &&
        !task_meta_flag10_bytes.empty()) {
        document.project.flag10_stored = true;
        document.project.flag10 =
            (task_meta_flag10_bytes[0] & kFixedMetaFlag10Bit) != 0;
    }

    /* Liveness, computed once.  Two facts must both hold for a task row:
     *
     *   (a) it has a VarMeta group.  A row without one has no Name entry and MS
     *       Project does not report it (PROJECT_A row 67, uid 10856; PROJECT_B rows
     *       1..12).  Deleted tasks leave their FixedData slot and their
     *       Fixed2Data record behind, so the task loop and the outline walk must
     *       agree on which rows are live -- otherwise a deleted slot would
     *       consume an outline number.
     *
     *   (b) its FixedMeta entry's +0 state byte is 0x00, not the 0x02 tombstone.
     *       A delete does NOT remove the row's VarMeta group (Project's own
     *       add-then-delete file, corpus/probe/w10-adddel-back.mpp, keeps all
     *       8 entries for the deleted uid), so VarMeta presence alone reads a
     *       tombstoned row as a live task: measured 6 tasks where Project
     *       reports 5.  This is W45's reader fix; PROJECT_B (12 tombstones) and
     *       PROJECT_A (none) are unchanged by it.
     *
     * When FixedMeta is absent or unreadable the state is unknown and the
     * VarMeta group alone decides, exactly as before. */
    std::vector<bool> has_var_group(rows, false);
    std::vector<bool> live_row(rows, false);
    live_row[0] = true;
    for (std::uint32_t row = 1; row < rows; ++row) {
        const std::uint32_t uid = uid_by_row[row];
        has_var_group[row] =
            table.usable &&
            (table.group_span.count(uid) != 0 ||
             table.group_span.count(uid | kSecondaryUidFamily) != 0);
        const std::int16_t state =
            row < task_meta_state.size() ? task_meta_state[row] : kStateNoMetaEntry;
        const bool state_live = state == kStateNoMetaStream ||
                                state == static_cast<std::int16_t>(0);
        live_row[row] = !table.usable || (has_var_group[row] && state_live);
    }

    /* ---- Outline: Fixed2Data's stored parent links -> level, number, summary. */
    std::vector<std::uint32_t> id_by_row(rows, 0);
    for (std::uint32_t row = 0; row < rows; ++row) {
        id_by_row[row] = u32_at(fixed, kFixedHeaderSize + row * kFixedRowSize + kRowId);
    }
    const Outline outline = read_outline(cfb, root, rows, id_by_row, task_meta_offsets,
                                         live_row, document.warnings);

    /* ---- Row 0: the project row. */
    {
        const std::size_t base = kFixedHeaderSize;
        document.project.row_id = u32_at(fixed, base + kRowId);
        document.project.uid = u32_at(fixed, base + kRowUid);
        document.project.duration_ticks = u32_at(fixed, base + kRowDuration);
        document.project.priority = u16_at(fixed, base + kRowPriority);
        document.project.percent_complete = u16_at(fixed, base + kRowPercent);
        document.project.start.raw = u32_at(fixed, base + kRowCalculatedStart);
        document.project.finish.raw = u32_at(fixed, base + kRowCalculatedFinish);
        if (table.usable && read_text_field(table, var2, document.project.uid,
                                            kFieldTaskName,
                                            document.project.title_raw)) {
            document.project.title = trim_ascii_spaces(document.project.title_raw);
        }
    }

    /* ---- Rows 1..n-1: live tasks, and dead slots. */
    for (std::uint32_t row = 1; row < rows; ++row) {
        const std::uint32_t uid = uid_by_row[row];

        const std::size_t base = kFixedHeaderSize + row * kFixedRowSize;
        if (!live_row[row]) {
            SkippedRow skipped;
            skipped.row = row;
            skipped.uid = uid;
            const std::int16_t state = row < task_meta_state.size()
                                           ? task_meta_state[row]
                                           : kStateNoMetaEntry;
            const bool tombstoned =
                state == static_cast<std::int16_t>(kFixedMetaStateDeleted);
            skipped.reason =
                tombstoned ? "FixedMeta entry is a tombstone (+0 = 0x02): deleted row"
                           : "no VarMeta group (no Name entry): deleted row slot";
            if (!tombstoned) {
                skipped.orphan_name = orphan_name_for_row(table, var2, row, uid_by_row);
            }
            std::string detail =
                tombstoned
                    ? "row " + std::to_string(row) + " (uid " + std::to_string(uid) +
                          ") has FixedMeta's tombstone flag (+0 = 0x02) and is treated "
                          "as a deleted row slot, not a task"
                    : "row " + std::to_string(row) + " (uid " + std::to_string(uid) +
                          ") has no VarMeta group and is treated as a deleted row "
                          "slot, not a task";
            if (!tombstoned && !fixed_meta_live[row]) {
                detail += " (FixedMeta has no entry for it either)";
            }
            if (tombstoned && has_var_group[row]) {
                detail += " (its VarMeta group is still present; Project ignores it "
                          "because of the flag)";
            }
            if (!skipped.orphan_name.empty()) {
                detail += "; the unreferenced Var2Data block between its neighbours "
                          "holds the name '" +
                          skipped.orphan_name + "'";
            }
            document.warnings.emplace_back(std::move(detail));
            document.skipped_rows.push_back(std::move(skipped));
            continue;
        }

        Task task;
        task.row = row;
        task.row_id = u32_at(fixed, base + kRowId);
        task.uid = uid;
        task.duration_ticks = u32_at(fixed, base + kRowDuration);
        task.duration_format = u16_at(fixed, base + kRowDurationFormat);
        task.priority = u16_at(fixed, base + kRowPriority);
        task.percent_complete = u16_at(fixed, base + kRowPercent);
        task.start.raw = u32_at(fixed, base + kRowStart);
        task.finish.raw = u32_at(fixed, base + kRowFinish);
        task.calculated_start.raw = u32_at(fixed, base + kRowCalculatedStart);
        task.calculated_finish.raw = u32_at(fixed, base + kRowCalculatedFinish);
        task.late_start.raw = u32_at(fixed, base + kRowLateStart);
        task.late_finish.raw = u32_at(fixed, base + kRowLateFinish);
        /* Scheduling inputs (W19).  A value outside PjConstraint's 0..7 is
         * damage, not a type: report it and treat the task as ASAP with no
         * date rather than handing the engine an impossible enum.  The one
         * corpus file this happens in is a writer experiment
         * (pv-duration-both-tables.mpp stores 33600 at +64). */
        const std::uint16_t constraint_type =
            u16_at(fixed, base + kRowConstraintType);
        if (constraint_type <= kConstraintFnlt) {
            task.constraint_type = constraint_type;
            task.constraint_date.raw = u32_at(fixed, base + kRowConstraintDate);
        } else {
            document.warnings.emplace_back(
                "Task row " + std::to_string(row) + " (uid " + std::to_string(uid) +
                ") stores constraint type " + std::to_string(constraint_type) +
                " at +64; PjConstraint is 0..7, so it is reported as ASAP with no "
                "constraint date");
        }
        task.deadline.raw = u32_at(fixed, base + kRowDeadline);
        task.actual_duration_ticks = u32_at(fixed, base + kRowActualDuration);
        task.actual_start.raw = u32_at(fixed, base + kRowActualStart);
        task.actual_finish.raw = u32_at(fixed, base + kRowActualFinish);
        /* W31: the pre-alignment start anchor (+174) and the task-calendar uid
         * (+178).  Neither feeds Task::start; see reader.h. */
        task.start_anchor.raw = u32_at(fixed, base + kRowStartAnchor);
        task.calendar_uid =
            static_cast<std::int32_t>(u32_at(fixed, base + kRowCalendarUid));
        /* W31: Milestone is a stored flag (FixedMeta entry +10 bit 0x02), not
         * "duration == 0".  PROJECT_B's export says Milestone=1 for uid 10735
         * (10 days) and uid 10799 (1 day); Project lets a milestone keep a
         * duration.  Duration remains the fallback for a row the metadata does
         * not cover. */
        if (task_has_fixed_meta && fixed_meta_live[row]) {
            task.milestone =
                (task_meta_flag_bytes[row] & kFixedMetaFlagMilestone) != 0;
            task.milestone_stored = true;
        } else {
            task.milestone = task.duration_ticks == 0;
            task.milestone_stored = false;
        }
        // Wave 24 (W24-15): Flag10 (Project's SUMMARY column) is part of the
        // generic Flag1..Flag20 decode in the custom-fields block below.
        task.free_slack_ticks =
            static_cast<std::int32_t>(u32_at(fixed, base + kRowFreeSlack));
        task.start_slack_ticks =
            static_cast<std::int32_t>(u32_at(fixed, base + kRowStartSlack));
        task.total_slack_ticks =
            static_cast<std::int32_t>(u32_at(fixed, base + kRowTotalSlack));
        /* A manually scheduled task is never on Project's critical path, even
         * with zero slack (base.mpp's Task A: slack 0, COM Critical false). */
        task.manually_scheduled = outline.usable && outline.manually_scheduled[row];
        task.critical = !task.manually_scheduled && task.total_slack_ticks <= 0;
        /* A file without a usable Fixed2Data is reported as a flat outline
         * (read_outline() warned); nothing in the fixed row carries a level. */
        task.outline_level = outline.usable ? outline.level[row] : 1u;
        task.outline_number =
            outline.usable ? outline.number[row] : std::to_string(row);
        task.child_count = outline.usable ? outline.child_count[row] : 0u;
        task.summary = task.child_count > 0;
        if (table.usable &&
            read_text_field(table, var2, uid, kFieldTaskName, task.name_raw)) {
            task.name = trim_ascii_spaces(task.name_raw);
        }
        /* ---- Custom fields (W48; encodings in field_value.h).  A text/number/
         * date/duration/cost value is a VarMeta block under the field's PjTask id
         * (Cost's key is the PjTask id itself and the f64 is hundredths, W60).
         * Wave 24 (W24-15): ALL TWENTY Flag fields are FixedMeta one-bit fields
         * with the measured positions in kFixedMetaFlagBits; a clear bit is a
         * stored False, so every flag is listed and Flag10 (Project's SUMMARY
         * column) is also exposed as Task::flag10. */
        {
            std::unordered_set<std::uint32_t> seen_field_ids;
            for (const VarEntry& entry : table.entries) {
                if (entry.uid != uid &&
                    entry.uid != (uid | kSecondaryUidFamily)) {
                    continue;
                }
                const FieldKind kind = custom_field_kind(entry.key);
                if (kind == FieldKind::unknown) {
                    continue;
                }
                /* A repeated (uid, key) pair reads its first entry, as Name does. */
                if (!seen_field_ids.insert(entry.key).second) {
                    continue;
                }
                CustomFieldValue field;
                field.field_id = entry.key;
                field.kind = kind;
                field.present = true;
                field.storage_measured = custom_field_storage_measured(
                    kind, custom_field_index(entry.key));
                if (field.storage_measured) {
                    std::vector<std::uint8_t> payload;
                    bool decoded = read_block(var2, entry.offset, payload);
                    std::size_t const expected =
                        kind == FieldKind::number || kind == FieldKind::cost
                            ? 8u
                            : (kind == FieldKind::date ||
                                       kind == FieldKind::duration
                                   ? 4u
                                   : 0u);
                    if (decoded && expected != 0 && payload.size() != expected) {
                        decoded = false;
                    }
                    if (!decoded) {
                        field.storage_measured = false;
                    } else {
                        switch (kind) {
                            case FieldKind::text:
                                field.text = decode_utf16_block(payload);
                                break;
                            case FieldKind::number:
                                field.number = f64_at(payload, 0);
                                break;
                            case FieldKind::date:
                                field.packed_date = u32_at(payload, 0);
                                break;
                            case FieldKind::duration:
                                field.duration_ticks = u32_at(payload, 0);
                                break;
                            case FieldKind::cost:
                                /* Stored as an f64 in hundredths (123.45 ->
                                 * 12345.0); expose currency units. */
                                field.cost = f64_at(payload, 0) / 100.0;
                                break;
                            default:
                                field.storage_measured = false;
                                break;
                        }
                    }
                }
                task.custom_fields.push_back(std::move(field));
            }
            if (task_has_fixed_meta && fixed_meta_live[row]) {
                CustomFieldValue flag;
                flag.field_id = kTaskFlag1FieldId;
                flag.kind = FieldKind::flag;
                flag.present = true;
                flag.storage_measured = true;
                flag.flag =
                    row < task_flag_group.size() &&
                    (task_flag_group[row][0] & kFixedMetaFlagBits[0].mask) != 0;
                task.custom_fields.push_back(std::move(flag));
            }
        }
        /* Wave 24 (W24-15): Flag1..Flag20, all measured one-bit FixedMeta
         * fields (kFixedMetaFlagBits).  They live on Task::flags rather than in
         * custom_fields so the engine dump's custom-field set does not grow by
         * 19 fields; Flag10 ALSO drives Task::flag10 (Project's SUMMARY). */
        if (task_has_fixed_meta && fixed_meta_live[row] &&
            row < task_flag_group.size()) {
            task.flags_stored = true;
            for (std::size_t index = 0; index < kFlagCountMpp14; ++index) {
                const FlagBitPosition& bit = kFixedMetaFlagBits[index];
                task.flags[index] =
                    (task_flag_group[row][bit.group] & bit.mask) != 0;
            }
            task.flag10 = task.flags[9];
            task.flag10_stored = true;
        }
        document.tasks.push_back(std::move(task));
    }

    std::unordered_set<std::uint32_t> live_task_uids;
    live_task_uids.reserve(document.tasks.size());
    for (const Task& task : document.tasks) {
        live_task_uids.insert(task.uid);
    }
    std::unordered_set<std::uint32_t> all_task_uids;
    all_task_uids.reserve(uid_by_row.size());
    for (const std::uint32_t uid : uid_by_row) {
        all_task_uids.insert(uid);
    }

    /* ---- Resources (TBkndRsc): 48 + 172*rows, FixedMeta 37*(rows+3).  Row 0
     * is the table's uid-0 placeholder; rows 1.. are the resources. */
    {
        std::vector<std::uint8_t> rsc_fixed;
        std::uint32_t rsc_rows = 0;
        if (read_table_fixed(cfb, root, "TBkndRsc", "Resource", kRscHeaderSize,
                             kRscRowSize, rsc_fixed, rsc_rows, document.warnings) &&
            rsc_rows > 0) {
            const std::vector<std::uint32_t> rsc_uid_by_row =
                uids_by_row(rsc_fixed, kRscHeaderSize, kRscRowSize, rsc_rows,
                            kRscRowUid);
            warn_duplicate_uids(rsc_uid_by_row, "Resource", false, document.warnings);
            const std::vector<std::uint8_t> rsc_var_meta =
                read_optional_stream(cfb, root, "TBkndRsc", "VarMeta",
                                     document.warnings);
            const std::vector<std::uint8_t> rsc_var2 =
                read_optional_stream(cfb, root, "TBkndRsc", "Var2Data",
                                     document.warnings);
            const VarTable rsc_table =
                parse_var_meta(rsc_var_meta, rsc_var2, "Resource", document.warnings);
            const bool rsc_meta_stream =
                cfb.has_stream(std::string(root) + "/TBkndRsc/FixedMeta");
            std::vector<std::uint32_t> rsc_meta_offsets;
            std::vector<std::uint8_t> rsc_meta_type;
            const std::vector<bool> rsc_live =
                fixed_meta_coverage(cfb, root, "TBkndRsc", "Resource", kRscHeaderSize,
                                    kRscRowSize, kRscFixedMetaEntrySize,
                                    kFixedMetaDescriptors, rsc_rows, rsc_uid_by_row,
                                    document.warnings, &rsc_meta_offsets, nullptr,
                                    nullptr, nullptr, nullptr, &rsc_meta_type);
            /* W49: the Cost/Material half of the type is the Fixed2Meta entry's
             * +8 bit 0x10 (measured, see reader.h "Resource Type").  The two
             * metadata streams pair by ENTRY INDEX, as the Task outline does. */
            std::vector<std::uint8_t> rsc_fixed2_type(rsc_rows, 0);
            std::vector<bool> rsc_fixed2_known(rsc_rows, false);
            const std::vector<std::uint8_t> rsc_fixed2_meta =
                read_optional_stream(cfb, root, "TBkndRsc", "Fixed2Meta",
                                     document.warnings);
            if (rsc_fixed2_meta.size() >= kFixedMetaHeaderSize &&
                u32_at(rsc_fixed2_meta, 0) == kMetaMagic) {
                const std::uint32_t declared = u32_at(rsc_fixed2_meta, 8);
                const std::size_t available =
                    rsc_fixed2_meta.size() - kFixedMetaHeaderSize;
                const std::size_t count =
                    declared != 0 ? declared : available / kRscFixed2MetaEntrySize;
                if (count != 0 && available % count == 0) {
                    const std::size_t stride = available / count;
                    for (std::size_t i = kFixedMetaDescriptors; i < count; ++i) {
                        if (i >= rsc_meta_offsets.size() ||
                            rsc_meta_offsets[i] == kNoOffset) {
                            continue;
                        }
                        const std::uint32_t offset = rsc_meta_offsets[i];
                        if (offset < kRscHeaderSize ||
                            (offset - kRscHeaderSize) % kRscRowSize != 0) {
                            continue;
                        }
                        const std::uint32_t row =
                            (offset - static_cast<std::uint32_t>(kRscHeaderSize)) /
                            static_cast<std::uint32_t>(kRscRowSize);
                        if (row >= rsc_rows) {
                            continue;
                        }
                        const std::size_t entry = kFixedMetaHeaderSize + i * stride;
                        if (entry + kRscFixed2MetaCostByte >= rsc_fixed2_meta.size()) {
                            continue;
                        }
                        rsc_fixed2_type[row] =
                            rsc_fixed2_meta[entry + kRscFixed2MetaCostByte];
                        rsc_fixed2_known[row] = true;
                    }
                }
            }
            for (std::uint32_t row = 1; row < rsc_rows; ++row) {
                const std::uint32_t uid = rsc_uid_by_row[row];
                const std::size_t base = kRscHeaderSize + row * kRscRowSize;
                if (uid == 0 || !rsc_live[row]) {
                    SkippedRow skipped;
                    skipped.row = row;
                    skipped.uid = uid;
                    skipped.reason = uid == 0 ? "UniqueID 0" : "no FixedMeta entry";
                    document.skipped_resources.push_back(std::move(skipped));
                    document.warnings.emplace_back(
                        "Resource row " + std::to_string(row) + " (uid " +
                        std::to_string(uid) + ") is treated as a deleted slot: " +
                        (uid == 0 ? std::string("its UniqueID is 0")
                                  : std::string("FixedMeta has no entry for it")));
                    continue;
                }
                Resource resource;
                resource.row = row;
                resource.row_id = u32_at(rsc_fixed, base + kRscRowId);
                resource.uid = uid;
                resource.max_units_x100 = f64_at(rsc_fixed, base + kRscRowMaxUnits);
                const bool work_bit =
                    row < rsc_meta_type.size() &&
                    (rsc_meta_type[row] & kRscFixedMetaWorkFlag) != 0;
                if (!rsc_meta_stream) {
                    resource.type = ResourceType::work;
                    resource.type_stored = false;
                } else if (work_bit) {
                    resource.type = ResourceType::work;
                    resource.type_stored = true;
                } else if (rsc_fixed2_known[row]) {
                    resource.type =
                        (rsc_fixed2_type[row] & kRscFixed2MetaCostFlag) != 0
                            ? ResourceType::cost
                            : ResourceType::material;
                    resource.type_stored = true;
                } else {
                    resource.type = ResourceType::work;
                    resource.type_stored = false;
                    document.warnings.emplace_back(
                        "Resource row " + std::to_string(row) + " (uid " +
                        std::to_string(uid) + ") is non-Work in FixedMeta but has no "
                        "Fixed2Meta type word; its type is reported as Work with "
                        "type_stored=false");
                }
                if (rsc_table.usable &&
                    read_text_field(rsc_table, rsc_var2, uid, kFieldRscName,
                                    resource.name_raw)) {
                    resource.name = trim_ascii_spaces(resource.name_raw);
                }
                std::string initials_raw;
                if (rsc_table.usable &&
                    read_text_field(rsc_table, rsc_var2, uid, kFieldRscInitials,
                                    initials_raw)) {
                    resource.initials = trim_ascii_spaces(initials_raw);
                }
                if (rsc_table.usable &&
                    read_text_field(rsc_table, rsc_var2, uid, kFieldRscMaterialLabel,
                                    resource.material_label_raw)) {
                    resource.material_label = trim_ascii_spaces(resource.material_label_raw);
                }
                document.resources.push_back(std::move(resource));
            }
        }
    }

    /* ---- Links (TBkndCons): 20*rows, no header, no variable data.  Liveness
     * is FixedMeta coverage, NOT "the endpoint is non-zero": MS Project writes a
     * FixedMeta entry per live link and leaves deleted link rows in FixedData
     * with no entry (PROJECT_A: 99 rows, 91 entries, 88 distinct covered rows).
     * Reading every non-zero row returned 97 links and invented a 10758 ->
     * 10859 link with a 6-day lag and a 10760 -> 10846 link that Project does
     * not schedule.  The header comment above records the MSPDI evidence. */
    {
        std::vector<std::uint8_t> cons_fixed;
        std::uint32_t cons_rows = 0;
        if (read_table_fixed(cfb, root, "TBkndCons", "Link", 0, kConsRowSize,
                             cons_fixed, cons_rows, document.warnings) &&
            cons_rows > 0) {
            const std::vector<std::uint32_t> cons_uid_by_row =
                uids_by_row(cons_fixed, 0, kConsRowSize, cons_rows, kConsRowUid);
            warn_duplicate_uids(cons_uid_by_row, "Link", false, document.warnings);
            std::vector<std::int16_t> cons_state;
            const std::vector<bool> cons_live =
                fixed_meta_coverage(cfb, root, "TBkndCons", "Link", 0, kConsRowSize,
                                    kConsFixedMetaEntrySize, 0, cons_rows,
                                    cons_uid_by_row, document.warnings, nullptr,
                                    nullptr, &cons_state);
            std::size_t zero_rows = 0;
            std::size_t uncovered_rows = 0;
            std::size_t tombstoned_rows = 0;
            for (std::uint32_t row = 0; row < cons_rows; ++row) {
                const std::size_t base = row * kConsRowSize;
                const std::uint32_t predecessor =
                    u32_at(cons_fixed, base + kConsRowPredecessor);
                const std::uint32_t successor =
                    u32_at(cons_fixed, base + kConsRowSuccessor);
                if (predecessor == 0 && successor == 0) {
                    ++zero_rows;
                    SkippedRow skipped;
                    skipped.row = row;
                    skipped.uid = cons_uid_by_row[row];
                    skipped.reason = "predecessor and successor are both 0";
                    document.skipped_links.push_back(std::move(skipped));
                    continue;
                }
                const std::int16_t state =
                    row < cons_state.size() ? cons_state[row] : kStateNoMetaEntry;
                const bool state_live = state == kStateNoMetaStream || state == 0;
                if (!cons_live[row] || !state_live) {
                    /* A non-zero row that no FixedMeta entry names is a deleted
                     * link slot.  So is a row whose entry carries the W45
                     * tombstone (+0 = 0x02): the delete cascade keeps the row's
                     * bytes and only marks its metadata.  Returning either would
                     * invent a dependency that MS Project does not schedule.
                     * PROJECT_B is the measured case (2 tombstoned link rows; its
                     * export has 70 links, not 72). */
                    if (cons_live[row]) {
                        ++tombstoned_rows;
                    } else {
                        ++uncovered_rows;
                    }
                    SkippedRow skipped;
                    skipped.row = row;
                    skipped.uid = cons_uid_by_row[row];
                    skipped.reason =
                        cons_live[row]
                            ? "FixedMeta entry is a tombstone (+0 = 0x02): deleted "
                              "link row"
                            : "no FixedMeta entry: MS Project does not schedule this row";
                    document.skipped_links.push_back(std::move(skipped));
                    continue;
                }
                Link link;
                link.row = row;
                link.uid = cons_uid_by_row[row];
                link.predecessor_uid = predecessor;
                link.successor_uid = successor;
                link.type_raw = u16_at(cons_fixed, base + kConsRowType);
                link.lag_ticks =
                    static_cast<std::int32_t>(u32_at(cons_fixed, base + kConsRowLag));
                link.lag_units = u16_at(cons_fixed, base + kConsRowLagUnits);
                link.predecessor_known = live_task_uids.count(predecessor) != 0;
                link.successor_known = live_task_uids.count(successor) != 0;
                document.links.push_back(link);
            }
            if (zero_rows > 0) {
                document.warnings.emplace_back(
                    "TBkndCons/FixedData holds " + std::to_string(zero_rows) +
                    " all-zero rows; they are treated as deleted link slots");
            }
            if (uncovered_rows > 0) {
                document.warnings.emplace_back(
                    "TBkndCons/FixedData holds " + std::to_string(uncovered_rows) +
                    " non-zero rows that no FixedMeta entry names; they are "
                    "treated as deleted link slots, not links");
            }
            if (tombstoned_rows > 0) {
                document.warnings.emplace_back(
                    "TBkndCons/FixedData holds " + std::to_string(tombstoned_rows) +
                    " rows whose FixedMeta entry is a tombstone (+0 = 0x02); they "
                    "are treated as deleted link slots, not links");
            }
            for (const Link& link : document.links) {
                if (live_task_uids.count(link.predecessor_uid) == 0) {
                    document.warnings.emplace_back(
                        "Link uid " + std::to_string(link.uid) +
                        " names predecessor task " +
                        std::to_string(link.predecessor_uid) +
                        ", which no live task holds" +
                        (all_task_uids.count(link.predecessor_uid) != 0
                             ? " (only a deleted row slot)"
                             : ""));
                }
                if (live_task_uids.count(link.successor_uid) == 0) {
                    document.warnings.emplace_back(
                        "Link uid " + std::to_string(link.uid) +
                        " names successor task " + std::to_string(link.successor_uid) +
                        ", which no live task holds" +
                        (all_task_uids.count(link.successor_uid) != 0
                             ? " (only a deleted row slot)"
                             : ""));
                }
            }
        }
    }

    /* ---- Assignments (TBkndAssn): 110*rows, no header.  Liveness is
     * FixedMeta coverage: PROJECT_A has 69 rows but 68 entries (row 52 belongs to the
     * deleted task 10856 and row 65 is all zero), PROJECT_B has 73/73.  VarMeta
     * presence is NOT a liveness test here -- 10 of PROJECT_B's live assignments
     * have no variable entries. */
    {
        std::vector<std::uint8_t> assn_fixed;
        std::uint32_t assn_rows = 0;
        if (read_table_fixed(cfb, root, "TBkndAssn", "Assignment", 0, kAssnRowSize,
                             assn_fixed, assn_rows, document.warnings) &&
            assn_rows > 0) {
            const std::vector<std::uint32_t> assn_uid_by_row =
                uids_by_row(assn_fixed, 0, kAssnRowSize, assn_rows, kAssnRowUid);
            warn_duplicate_uids(assn_uid_by_row, "Assignment", false, document.warnings);
            std::vector<std::int16_t> assn_state;
            const std::vector<bool> assn_live =
                fixed_meta_coverage(cfb, root, "TBkndAssn", "Assignment", 0,
                                    kAssnRowSize, kAssnFixedMetaEntrySize, 0,
                                    assn_rows, assn_uid_by_row, document.warnings,
                                    nullptr, nullptr, &assn_state);
            std::size_t assn_tombstoned = 0;
            for (std::uint32_t row = 0; row < assn_rows; ++row) {
                const std::size_t base = row * kAssnRowSize;
                const std::uint32_t task_uid = u32_at(assn_fixed, base + kAssnRowTaskUid);
                const std::int16_t state =
                    row < assn_state.size() ? assn_state[row] : kStateNoMetaEntry;
                const bool state_live = state == kStateNoMetaStream || state == 0;
                if (!assn_live[row] || !state_live) {
                    /* W48's delete cascade tombstones the assignment row's
                     * FixedMeta entry (+0 = 0x02) and leaves its FixedData bytes
                     * behind.  PROJECT_B has 10 such rows; Project exports 63
                     * assignments, not 73.  A placeholder row (task uid 0) is
                     * also tombstoned in some fixtures; it is skipped silently,
                     * exactly as the placeholder path below always did. */
                    if (assn_live[row] && task_uid != 0) {
                        ++assn_tombstoned;
                    }
                    SkippedRow skipped;
                    skipped.row = row;
                    skipped.uid = assn_uid_by_row[row];
                    skipped.task_uid = task_uid;
                    skipped.reason =
                        assn_live[row]
                            ? "FixedMeta entry is a tombstone (+0 = 0x02): deleted "
                              "assignment row"
                            : "no FixedMeta entry";
                    document.skipped_assignments.push_back(std::move(skipped));
                    continue;
                }
                Assignment assignment;
                assignment.row = row;
                assignment.uid = assn_uid_by_row[row];
                assignment.task_uid = task_uid;
                assignment.resource_uid_raw =
                    u32_at(assn_fixed, base + kAssnRowResourceUid);
                assignment.units_x100 = f64_at(assn_fixed, base + kAssnRowUnits);
                assignment.work_x1000 = f64_at(assn_fixed, base + kAssnRowWork);
                assignment.start.raw = u32_at(assn_fixed, base + kAssnRowStart);
                assignment.finish.raw = u32_at(assn_fixed, base + kAssnRowFinish);
                assignment.resume.raw = u32_at(assn_fixed, base + kAssnRowResume);
                /* assign1.mpp keeps a FixedMeta entry for its two placeholder
                 * rows (uid 1 and 2 but task uid 0, no resource, no work); the
                 * task uid is what tells them apart from real assignments. */
                if (assignment.task_uid == 0) {
                    SkippedRow skipped;
                    skipped.row = row;
                    skipped.uid = assignment.uid;
                    skipped.task_uid = 0;
                    skipped.reason = "placeholder row with task UniqueID 0";
                    document.skipped_assignments.push_back(std::move(skipped));
                    continue;
                }
                if (live_task_uids.count(assignment.task_uid) == 0) {
                    document.warnings.emplace_back(
                        "Assignment uid " + std::to_string(assignment.uid) +
                        " names task " + std::to_string(assignment.task_uid) +
                        ", which no live task holds" +
                        (all_task_uids.count(assignment.task_uid) != 0
                             ? " (only a deleted row slot)"
                             : ""));
                }
                document.assignments.push_back(assignment);
            }
            if (assn_tombstoned > 0) {
                document.warnings.emplace_back(
                    "TBkndAssn/FixedData holds " + std::to_string(assn_tombstoned) +
                    " rows whose FixedMeta entry is a tombstone (+0 = 0x02); they "
                    "are treated as deleted assignment slots, not assignments");
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * MPP12 (Project 2007) table reading.  Geometry and evidence: "MPP12" in the
 * file comment.  FixedData rows are bounded by the NEXT FixedMeta entry's
 * offset (not by a fixed stride) because MPP12 mixes 264-byte task rows with
 * 16-byte null-task placeholders.
 * ------------------------------------------------------------------------- */

/* One FixedData row, recovered from FixedMeta's entry offsets.  `entry` holds
 * the matching FixedMeta entry verbatim (its +0 state byte and the flag bytes
 * are read from it). */
struct RowTable {
    bool usable = false;
    bool meta_present = false;
    std::size_t entry_size = 0;
    std::size_t descriptors = 0;       /* FixedMeta entries before data row 0 */
    std::vector<std::uint32_t> offset; /* absolute FixedData offset per row */
    std::vector<std::uint32_t> size;   /* bytes, bounded by the next entry */
    std::vector<std::uint8_t> entries; /* n * entry_size, FixedMeta entries */
    std::uint32_t declared = 0;

    /* A byte of the FixedMeta entry that describes DATA row `row` (entry
     * descriptors + row).  Zero when the metadata stream is absent. */
    std::uint8_t meta_byte(std::size_t row, std::size_t at) const {
        const std::size_t index = (row + descriptors) * entry_size + at;
        return index < entries.size() ? entries[index] : 0;
    }
    std::uint8_t state(std::size_t row) const { return meta_byte(row, 0); }
    std::size_t count() const { return offset.size(); }
};

/* Reads FixedData+FixMeta for one MPP12 table.  `descriptors` is 3 for the
 * tables whose FixedData starts with the three 16-byte descriptor slots
 * (Task, Resource) and 0 for Link/Assignment.  When FixedMeta is absent or
 * unusable the rows are recovered arithmetically from the FixedData size and
 * every row is assumed live -- the same fallback the MPP14 path uses. */
bool read_row_table(const CompoundFile& cfb, std::string_view root, std::string_view table,
                    std::string_view label, std::size_t header_size, std::size_t row_size,
                    std::size_t entry_size, std::size_t descriptors, RowTable& out,
                    std::vector<std::string>& warnings) {
    std::vector<std::uint8_t> fixed;
    try {
        fixed = cfb.read_stream(std::string(root) + "/" + std::string(table) + "/FixedData");
    } catch (const Error& error) {
        warnings.emplace_back(std::string(root) + "/" + std::string(table) +
                              "/FixedData is unreadable: " + error.what() +
                              "; the table is skipped");
        return false;
    }
    out = RowTable{};
    out.entry_size = entry_size;
    out.descriptors = descriptors;
    if (fixed.size() < header_size) {
        warnings.emplace_back(std::string(label) + " FixedData is " +
                              std::to_string(fixed.size()) +
                              " bytes, smaller than its " + std::to_string(header_size) +
                              "-byte header; the table is skipped");
        return false;
    }

    std::vector<std::uint8_t> meta;
    try {
        if (cfb.has_stream(std::string(root) + "/" + std::string(table) + "/FixedMeta")) {
            meta = cfb.read_stream(std::string(root) + "/" + std::string(table) +
                                   "/FixedMeta");
        }
    } catch (const Error& error) {
        warnings.emplace_back(std::string(label) + " FixedMeta is unreadable: " +
                              error.what() + "; row sizes are assumed");
    }
    if (meta.size() >= kFixedMetaHeaderSize && u32_at(meta, 0) == kMetaMagic) {
        out.meta_present = true;
        out.declared = u32_at(meta, 8);
        const std::size_t available = meta.size() - kFixedMetaHeaderSize;
        const std::size_t count = available / entry_size;
        if (available % entry_size != 0) {
            warnings.emplace_back(std::string(label) + " FixedMeta is " +
                                  std::to_string(meta.size()) + " bytes, not 16 + " +
                                  std::to_string(entry_size) + "*n; trailing bytes ignored");
        }
        if (out.declared != count) {
            warnings.emplace_back(std::string(label) + " FixedMeta header declares " +
                                  std::to_string(out.declared) +
                                  " entries but the stream holds " + std::to_string(count) +
                                  "; the stream wins");
        }
        if (count < descriptors) {
            warnings.emplace_back(std::string(label) + " FixedMeta holds " +
                                  std::to_string(count) + " entries, fewer than its " +
                                  std::to_string(descriptors) +
                                  " header descriptors; the table is skipped");
            return false;
        }
        out.entries.assign(meta.begin() + static_cast<std::ptrdiff_t>(kFixedMetaHeaderSize),
                           meta.end());
        out.offset.reserve(count - descriptors);
        std::vector<std::uint32_t> entry_offsets;
        entry_offsets.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            entry_offsets.push_back(u32_at(meta, kFixedMetaHeaderSize + i * entry_size + 4));
        }
        for (std::size_t i = descriptors; i < count; ++i) {
            const std::uint32_t begin = entry_offsets[i];
            const std::uint32_t end =
                i + 1 < count ? entry_offsets[i + 1]
                              : static_cast<std::uint32_t>(fixed.size());
            if (begin < header_size || begin > fixed.size() || end < begin ||
                end > fixed.size()) {
                warnings.emplace_back(std::string(label) + " FixedMeta entry " +
                                      std::to_string(i) + " has the invalid FixedData "
                                      "range [" + std::to_string(begin) + ", " +
                                      std::to_string(end) + "); the table is skipped");
                return false;
            }
            out.offset.push_back(begin);
            out.size.push_back(end - begin);
        }
        out.usable = true;
        return true;
    }

    /* No usable metadata: fall back to the row stride. */
    if (meta.size() != 0) {
        warnings.emplace_back(std::string(label) + " FixedMeta has magic " +
                              hex32(u32_at(meta, 0)) +
                              " instead of 0xfadfadba; row sizes are assumed");
    }
    if ((fixed.size() - header_size) % row_size != 0) {
        warnings.emplace_back(std::string(label) + " FixedData is " +
                              std::to_string(fixed.size()) + " bytes = " +
                              std::to_string(header_size) + " + " +
                              std::to_string(row_size) + "*n + " +
                              std::to_string((fixed.size() - header_size) % row_size) +
                              "; the trailing bytes are ignored");
    }
    const std::size_t rows = (fixed.size() - header_size) / row_size;
    out.offset.reserve(rows);
    out.size.reserve(rows);
    for (std::size_t i = 0; i < rows; ++i) {
        out.offset.push_back(static_cast<std::uint32_t>(header_size + i * row_size));
        out.size.push_back(static_cast<std::uint32_t>(row_size));
    }
    out.usable = true;
    return true;
}

/* The raw bytes of one row, or empty when the row's block is shorter than the
 * caller needs.  Never reads past FixedData. */
std::vector<std::uint8_t> row_bytes(const std::vector<std::uint8_t>& fixed,
                                    const RowTable& table, std::size_t row,
                                    std::size_t need) {
    if (row >= table.offset.size()) {
        return {};
    }
    const std::uint32_t begin = table.offset[row];
    const std::uint32_t size = table.size[row];
    if (begin > fixed.size() || size > fixed.size() - begin || size < need) {
        return {};
    }
    return std::vector<std::uint8_t>(fixed.begin() + begin, fixed.begin() + begin + size);
}

/* Decodes the VarMeta custom-field block set for one uid (Text/Number/Date/
 * Duration/Cost).  Shared by both formats: the key is the PjField id in both.
 * Flag values are NOT here -- they are FixedMeta bits, and their position
 * differs by format. */
std::vector<CustomFieldValue> decode_var_custom_fields(const VarTable& table,
                                                       const std::vector<std::uint8_t>& var2,
                                                       std::uint32_t uid) {
    std::vector<CustomFieldValue> fields;
    std::unordered_set<std::uint32_t> seen_field_ids;
    for (const VarEntry& entry : table.entries) {
        if (entry.uid != uid && entry.uid != (uid | kSecondaryUidFamily)) {
            continue;
        }
        const FieldKind kind = custom_field_kind(entry.key);
        if (kind == FieldKind::unknown) {
            continue;
        }
        /* A repeated (uid, key) pair reads its first entry, as Name does. */
        if (!seen_field_ids.insert(entry.key).second) {
            continue;
        }
        CustomFieldValue field;
        field.field_id = entry.key;
        field.kind = kind;
        field.present = true;
        field.storage_measured = custom_field_storage_measured(
            kind, custom_field_index(entry.key));
        if (field.storage_measured) {
            std::vector<std::uint8_t> payload;
            bool decoded = read_block(var2, entry.offset, payload);
            std::size_t const expected =
                kind == FieldKind::number || kind == FieldKind::cost
                    ? 8u
                    : (kind == FieldKind::date || kind == FieldKind::duration ? 4u : 0u);
            if (decoded && expected != 0 && payload.size() != expected) {
                decoded = false;
            }
            if (!decoded) {
                field.storage_measured = false;
            } else {
                switch (kind) {
                    case FieldKind::text:
                        field.text = decode_utf16_block(payload);
                        break;
                    case FieldKind::number:
                        field.number = f64_at(payload, 0);
                        break;
                    case FieldKind::date:
                        field.packed_date = u32_at(payload, 0);
                        break;
                    case FieldKind::duration:
                        field.duration_ticks = u32_at(payload, 0);
                        break;
                    case FieldKind::cost:
                        /* Stored as an f64 in hundredths (123.45 -> 12345.0). */
                        field.cost = f64_at(payload, 0) / 100.0;
                        break;
                    default:
                        field.storage_measured = false;
                        break;
                }
            }
        }
        fields.push_back(std::move(field));
    }
    return fields;
}

/* MPP12 Flag1..Flag20: one contiguous bit field starting at FixedMeta entry
 * byte 37 bit 0x20 (measured on tf2007-mpp12.mpp; see the file comment).  A
 * clear bit is a stored False, so every flag is reported present, as MPP14
 * does for Flag1. */
void append_mpp12_flags(const RowTable& table, std::size_t row,
                        std::vector<CustomFieldValue>& fields) {
    for (unsigned index = 1; index <= kFlagFieldCount; ++index) {
        const unsigned bit_index =
            (index - 1u) + k12FixedMetaFlagFirstBitIndex;
        const std::size_t byte = k12FixedMetaFlagFirstByte + bit_index / 8u;
        const std::uint8_t mask = static_cast<std::uint8_t>(1u << (bit_index % 8u));
        CustomFieldValue flag;
        flag.field_id = custom_field_id(FieldKind::flag, index);
        flag.kind = FieldKind::flag;
        flag.present = true;
        flag.storage_measured = true;
        flag.flag = (table.meta_byte(row, byte) & mask) != 0;
        fields.push_back(std::move(flag));
    }
}

/* Applies the top-level props records: version string, password flag and the
 * encryption code.  See "PASSWORD" in the file comment. */
void read_version_and_password(const CompoundFile& cfb, std::string_view props_stream,
                               Document& document) {
    if (!cfb.has_stream(std::string(props_stream))) {
        document.warnings.emplace_back("no top-level " + std::string(props_stream) +
                                       " stream; the writer version and the password "
                                       "flag are unknown");
        return;
    }
    std::vector<std::uint8_t> props;
    try {
        props = cfb.read_stream(std::string(props_stream));
    } catch (const Error& error) {
        document.warnings.emplace_back(std::string(props_stream) + " is unreadable: " +
                                       error.what());
        return;
    }
    if (props.size() < 16) {
        document.warnings.emplace_back(std::string(props_stream) + " is " +
                                       std::to_string(props.size()) +
                                       " bytes, too short for its 16-byte header");
        return;
    }
    const std::size_t count = u16_at(props, 12);
    std::size_t offset = 16;
    for (std::size_t i = 0; i < count; ++i) {
        if (offset + 12 > props.size()) {
            document.warnings.emplace_back(std::string(props_stream) + " ends after " +
                                           std::to_string(i) + " of " +
                                           std::to_string(count) + " props records");
            return;
        }
        const std::uint32_t length = u32_at(props, offset);
        const std::uint32_t key = u32_at(props, offset + 4);
        if (offset + 12u + length > props.size()) {
            document.warnings.emplace_back(std::string(props_stream) + " record " +
                                           std::to_string(i) + " (key " + hex32(key) +
                                           ") claims " + std::to_string(length) +
                                           " bytes past the stream; ignored");
            return;
        }
        const std::uint8_t* data = props.data() + offset + 12;
        if (key == kPropsVersion && length >= 2 && (length % 2) == 0) {
            std::vector<std::uint8_t> text(data, data + length);
            document.version_string = decode_utf16_block(text);
        } else if (key == kPropsPasswordFlag && length >= 1) {
            document.password_flag = data[0];
        } else if (key == kPropsEncryptionCode && length >= 1) {
            document.encryption_code = data[0];
        } else if (key == kPropsProtectionHash) {
            document.password_hash_present = true;
        } else if (key == kPropsWriteReservationHash) {
            document.write_reservation_hash_present = true;
        }
        offset += 12u + length + (length % 2u);
    }
    document.password_required_to_read = (document.password_flag & 0x01u) != 0;
    document.password_required_to_modify = (document.password_flag & 0x02u) != 0;
    document.streams_encrypted =
        document.password_flag != 0 && document.encryption_code != 0;
    if (document.password_required_to_read) {
        throw Error(std::string(props_stream) + " stores PASSWORD_FLAG = " +
                    hex32(document.password_flag) + " (key 0x35400000): a password is "
                    "required to read this file and this reader cannot authenticate "
                    "one.  The container is not encrypted by the flag alone; the "
                    "table streams would be XOR-masked by the code in key 0x35400007.");
    }
    if (document.streams_encrypted) {
        throw Error(std::string(props_stream) + " stores PASSWORD_FLAG = " +
                    hex32(document.password_flag) + " with ENCRYPTION_CODE = " +
                    hex32(document.encryption_code) +
                    ": the table streams are encrypted (single-byte XOR), which this "
                    "reader refuses rather than decoding without evidence.");
    }
    if (document.password_required_to_modify) {
        document.warnings.emplace_back(
            std::string(props_stream) + " stores PASSWORD_FLAG = " +
            hex32(document.password_flag) +
            " (write-reservation password): the file is read-only, not encrypted");
    }
}

/* Calendar identities for both formats (see Calendar in reader.h).  The
 * 12-byte row is the same SIZE but not the same column order: MPP12 stores
 * (calendar uid, base uid, resource uid) while MPP14 stores
 * (base uid, resource uid, calendar uid).  Evidence: MPXJ's
 * MPP12CalendarFactory.getCalendarIDOffset() = 0 / getBaseIDOffset() = 4 /
 * getResourceIDOffset() = 8, versus the MPP14 factory's calendar uid at +8;
 * and mpp12task.mpp's rows (1,-1,-1), (2,1,0), (3,1,1) name calendar 1, while
 * base.mpp's (0,-1,1), (1,0,2), (1,1,3) name calendars 1,2,3. */
void parse_calendars(const CompoundFile& cfb, std::string_view root, MppVersion version,
                     Document& document) {
    const std::string path = std::string(root) + "/TBkndCal/FixedData";
    if (!cfb.has_stream(path)) {
        document.warnings.emplace_back(std::string(root) +
                                       "/TBkndCal has no FixedData stream; no calendars "
                                       "are reported");
        return;
    }
    std::vector<std::uint8_t> fixed;
    try {
        fixed = cfb.read_stream(path);
    } catch (const Error& error) {
        document.warnings.emplace_back("TBkndCal/FixedData is unreadable: " +
                                       std::string(error.what()));
        return;
    }
    if (fixed.size() < kCalHeaderSize ||
        (fixed.size() - kCalHeaderSize) % kCalRowSize != 0) {
        document.warnings.emplace_back(
            "TBkndCal/FixedData is " + std::to_string(fixed.size()) +
            " bytes, not 64 + 12*n; calendars are not reported");
        return;
    }
    const std::vector<std::uint8_t> cal_var_meta =
        read_optional_stream(cfb, root, "TBkndCal", "VarMeta", document.warnings);
    const std::vector<std::uint8_t> cal_var2 =
        read_optional_stream(cfb, root, "TBkndCal", "Var2Data", document.warnings);
    const VarTable table = parse_var_meta(cal_var_meta, cal_var2, "Calendar",
                                          document.warnings);
    const std::size_t rows = (fixed.size() - kCalHeaderSize) / kCalRowSize;
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t base = kCalHeaderSize + row * kCalRowSize;
        Calendar calendar;
        if (version == MppVersion::mpp12) {
            calendar.uid = static_cast<std::int32_t>(u32_at(fixed, base));
            calendar.base_uid = static_cast<std::int32_t>(u32_at(fixed, base + 4));
            calendar.resource_uid = static_cast<std::int32_t>(u32_at(fixed, base + 8));
        } else {
            calendar.base_uid = static_cast<std::int32_t>(u32_at(fixed, base));
            calendar.resource_uid = static_cast<std::int32_t>(u32_at(fixed, base + 4));
            calendar.uid = static_cast<std::int32_t>(u32_at(fixed, base + 8));
        }
        if (calendar.uid <= 0) {
            continue; /* placeholder row; core/cal skips the same rows */
        }
        if (table.usable) {
            std::string raw;
            if (read_text_field(table, cal_var2, static_cast<std::uint32_t>(calendar.uid),
                                kFieldCalName, raw)) {
                calendar.name = trim_ascii_spaces(raw);
            }
        }
        document.calendars.push_back(std::move(calendar));
    }
}

/* MASTER/SUBPROJECT: the project props' SUBPROJECT_DATA record.  See the file
 * comment for the byte layout.  Only the item layout with type byte 0x01/0x08
 * is decoded; every other type is reported with its offset and skipped. */
void parse_subprojects_mpp12(const CompoundFile& cfb, std::string_view root,
                             Document& document) {
    const std::string path = std::string(root) + "/Props";
    if (!cfb.has_stream(path)) {
        return; /* no project props: no inserted-project table */
    }
    std::vector<std::uint8_t> props;
    try {
        props = cfb.read_stream(path);
    } catch (const Error& error) {
        document.warnings.emplace_back(std::string(root) + "/Props is unreadable: " +
                                       error.what() + "; subprojects are not reported");
        return;
    }
    if (props.size() < 16) {
        return;
    }
    const std::size_t count = u16_at(props, 12);
    std::size_t offset = 16;
    const std::vector<std::uint8_t>* subproject_data = nullptr;
    std::vector<std::uint8_t> data_holder;
    std::uint32_t declared_count = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (offset + 12 > props.size()) {
            break;
        }
        const std::uint32_t length = u32_at(props, offset);
        const std::uint32_t key = u32_at(props, offset + 4);
        if (offset + 12u + length > props.size()) {
            break;
        }
        if (key == kProjectPropsSubprojectData) {
            data_holder.assign(props.begin() + static_cast<std::ptrdiff_t>(offset + 12),
                               props.begin() +
                                   static_cast<std::ptrdiff_t>(offset + 12 + length));
            subproject_data = &data_holder;
        } else if (key == kProjectPropsSubprojectCount && length >= 4) {
            declared_count = u32_at(props, offset + 12);
        }
        offset += 12u + length + (length % 2u);
    }
    if (subproject_data == nullptr) {
        return;
    }
    const std::vector<std::uint8_t>& data = *subproject_data;
    if (data.size() < 12) {
        document.warnings.emplace_back("112/Props SUBPROJECT_DATA is " +
                                       std::to_string(data.size()) +
                                       " bytes, too short for its header");
        return;
    }
    const std::size_t item_count_offset = u32_at(data, 8);
    if (item_count_offset > data.size() || item_count_offset < 12) {
        document.warnings.emplace_back(
            "112/Props SUBPROJECT_DATA names item table offset " +
            std::to_string(item_count_offset) + ", outside the record; ignored");
        return;
    }
    std::size_t at = 12;
    std::size_t items = 0;
    while (at + 4 <= item_count_offset) {
        const std::uint32_t header =
            u32_at(data, at) & 0x1FFFFu; /* the record masks the offset */
        at += 4;
        ++items;
        if (static_cast<std::size_t>(header) + 20 > data.size()) {
            document.warnings.emplace_back(
                "112/Props SUBPROJECT_DATA item " + std::to_string(items) +
                " names header offset " + std::to_string(header) +
                ", outside the record; it is skipped");
            break;
        }
        const std::uint8_t type = data[header + 16];
        if (type == 0x00 || type == 0x10) {
            at += 8; /* deleted/placeholder entry, no path */
            continue;
        }
        if (type != 0x01 && type != 0x08) {
            document.warnings.emplace_back(
                "112/Props SUBPROJECT_DATA item " + std::to_string(items) +
                " uses layout type " + hex32(type) +
                " (item header at offset " + std::to_string(header) +
                "); only type 0x01/0x08 is implemented, so its source path is not "
                "reported and the remaining items are not parsed");
            break;
        }
        if (at + 12 > data.size()) {
            document.warnings.emplace_back(
                "112/Props SUBPROJECT_DATA item " + std::to_string(items) +
                " is truncated before its offsets; it is skipped");
            break;
        }
        const std::uint32_t uid_offset = u32_at(data, at) & 0x1FFFFu;
        const std::uint32_t path_offset = u32_at(data, at + 4) & 0x1FFFFu;
        at += 12;
        /* The uids whose rows are inserted run from uid_offset up to
         * path_offset; each is a 4-byte UniqueID followed by its type word. */
        std::string full_path;
        bool path_failed = false;
        auto decode_path = [&]() -> std::string {
            if (static_cast<std::size_t>(path_offset) + 22 > data.size()) {
                path_failed = true;
                return {};
            }
            /* Layout (MPXJ MPP12Reader.readSubProject): +18 = the DOS string's
             * byte length, then the NUL-terminated DOS path, +24, a u32 nested
             * block size (0 = no Unicode copy), a u32 UTF-16 byte length, two
             * bytes, then the UTF-16 path. */
            std::size_t cursor = path_offset + 18;
            const std::uint32_t dos_size = u32_at(data, cursor);
            cursor += 4;
            if (dos_size == 0 || cursor + dos_size > data.size()) {
                path_failed = true;
                return {};
            }
            std::string dos;
            for (std::size_t i = 0; i < dos_size; ++i) {
                const char ch = static_cast<char>(data[cursor + i]);
                if (ch == '\0') {
                    break;
                }
                dos.push_back(ch);
            }
            cursor += dos_size;
            cursor += 24;
            if (cursor + 8 > data.size()) {
                path_failed = true;
                return {};
            }
            const std::uint32_t nested_size = u32_at(data, cursor);
            cursor += 4;
            if (nested_size == 0) {
                return dos;
            }
            const std::uint32_t unicode_size = u32_at(data, cursor);
            cursor += 4;
            cursor += 2;
            if (cursor + unicode_size > data.size()) {
                path_failed = true;
                return {};
            }
            std::vector<std::uint8_t> text(data.begin() +
                                               static_cast<std::ptrdiff_t>(cursor),
                                           data.begin() + static_cast<std::ptrdiff_t>(
                                                               cursor + unicode_size));
            return decode_utf16_block(text);
        };
        full_path = decode_path();
        if (path_failed) {
            document.warnings.emplace_back(
                "112/Props SUBPROJECT_DATA item " + std::to_string(items) +
                " (type 0x01/0x08) has a path block this reader cannot decode "
                "(item header " + std::to_string(header) + ", path offset " +
                std::to_string(path_offset) + "); it is skipped");
            continue;
        }
        std::size_t uid_at = uid_offset;
        while (uid_at + 8 <= path_offset && uid_at + 8 <= data.size()) {
            const std::uint32_t task_uid = u32_at(data, uid_at);
            if (task_uid != 0 && task_uid <= 0x3FFFFFFFu) {
                SubprojectRecord record;
                record.task_uid = task_uid;
                record.path = full_path;
                document.subprojects.push_back(std::move(record));
            }
            uid_at += 4;
        }
    }
    if (declared_count != 0 && document.subprojects.empty()) {
        document.warnings.emplace_back(
            "112/Props says the file has " + std::to_string(declared_count) +
            " subprojects but SUBPROJECT_DATA yielded no readable record");
    }
}

/* Sibling-index outline numbers over MPP12's STORED parent links (row +36) and
 * outline levels (row +40).  The stored level is authoritative; a disagreement
 * with the chain depth is reported.  Sibling order is the ID column (row +0),
 * not the stored row order: Project numbers each child by its displayed ID.
 * Measured on the corpus capture through COM (W28-0.2): ID 1 is "1" and ID 12
 * is "2" even though the file stores ID 12's subtree first. */
void number_mpp12_outline(Document& document) {
    std::unordered_map<std::uint32_t, std::size_t> row_by_uid;
    row_by_uid.reserve(document.tasks.size());
    for (std::size_t i = 0; i < document.tasks.size(); ++i) {
        row_by_uid[document.tasks[i].uid] = i;
    }
    /* The ID column orders siblings; the stored index is only a tie-breaker
     * (IDs are unique in a valid file, so it is never reached in practice). */
    const auto by_id = [&document](std::size_t a, std::size_t b) {
        if (document.tasks[a].row_id != document.tasks[b].row_id) {
            return document.tasks[a].row_id < document.tasks[b].row_id;
        }
        return a < b;
    };
    std::vector<std::vector<std::size_t>> children(document.tasks.size() + 1);
    std::vector<std::size_t> top_level;
    std::size_t unknown_parents = 0;
    for (std::size_t i = 0; i < document.tasks.size(); ++i) {
        const std::uint32_t parent = document.tasks[i].parent_uid;
        if (parent == 0 || parent == 0xFFFFFFFFu) {
            if (parent == 0xFFFFFFFFu) {
                ++unknown_parents;
            }
            top_level.push_back(i);
            continue;
        }
        const auto found = row_by_uid.find(parent);
        if (found == row_by_uid.end() || found->second == i) {
            ++unknown_parents;
            top_level.push_back(i);
            continue;
        }
        children[found->second + 1].push_back(i);
    }
    std::sort(top_level.begin(), top_level.end(), by_id);
    for (std::vector<std::size_t>& kids : children) {
        std::sort(kids.begin(), kids.end(), by_id);
    }
    std::vector<bool> reached(document.tasks.size(), false);
    struct Pending {
        std::size_t row;
        std::size_t level;
        std::string prefix;
    };
    std::vector<Pending> stack;
    for (std::size_t i = top_level.size(); i-- > 0;) {
        stack.push_back(Pending{top_level[i], 1, std::to_string(i + 1)});
    }
    std::size_t mismatched_levels = 0;
    while (!stack.empty()) {
        const Pending node = stack.back();
        stack.pop_back();
        if (node.row >= document.tasks.size() || reached[node.row]) {
            continue;
        }
        reached[node.row] = true;
        Task& task = document.tasks[node.row];
        /* The stored +40 level is authoritative; the parent chain supplies the
         * number.  A disagreement is reported once below. */
        task.outline_level = task.stored_outline_level != 0
                                 ? task.stored_outline_level
                                 : static_cast<std::uint32_t>(node.level);
        task.outline_number = node.prefix;
        if (task.stored_outline_level != 0 &&
            task.stored_outline_level != node.level) {
            ++mismatched_levels;
        }
        task.child_count = static_cast<std::uint32_t>(children[node.row + 1].size());
        task.summary = task.child_count > 0;
        const std::vector<std::size_t>& kids = children[node.row + 1];
        for (std::size_t i = kids.size(); i-- > 0;) {
            const std::string part = std::to_string(i + 1);
            stack.push_back(Pending{kids[i], node.level + 1,
                                    node.prefix + "." + part});
        }
    }
    std::size_t unreachable = 0;
    for (std::size_t i = 0; i < document.tasks.size(); ++i) {
        if (!reached[i]) {
            ++unreachable;
        }
    }
    if (mismatched_levels > 0) {
        document.warnings.emplace_back(
            "MPP12 Task rows store an OutlineLevel at +40 that disagrees with the "
            "parent-uid chain on " + std::to_string(mismatched_levels) +
            " row(s); the stored level is reported and the chain is used for "
            "OutlineNumber");
    }
    if (unknown_parents > 0) {
        document.warnings.emplace_back(
            "MPP12 Task rows name a parent UniqueID that no live task holds on " +
            std::to_string(unknown_parents) +
            " row(s); those rows are numbered as top level");
    }
    if (unreachable > 0) {
        document.warnings.emplace_back(
            std::to_string(unreachable) +
            " MPP12 task row(s) are not reachable from the project row through the "
            "stored parent links; they are still returned and numbered by their "
            "stored Level");
        for (std::size_t i = 0; i < document.tasks.size(); ++i) {
            if (reached[i]) {
                continue;
            }
            Task& task = document.tasks[i];
            task.outline_level = task.stored_outline_level != 0
                                     ? task.stored_outline_level
                                     : 1u;
        }
    }
}

void parse_mpp12(const CompoundFile& cfb, std::string_view root, Document& document) {
    /* ---- Task table (required).  Geometry: 48 + 264*rows but rows are read by
     * FixedMeta entry offset because 16-byte null-task blocks are interleaved. */
    RowTable task_rows;
    if (!read_row_table(cfb, root, "TBkndTask", "Task", kFixedHeaderSize,
                        k12TaskRowSize, kFixedMetaEntrySize, kFixedMetaDescriptors,
                        task_rows, document.warnings)) {
        throw Error("no MPP12 Task table at " + std::string(root) +
                    "/TBkndTask/FixedData");
    }
    if (task_rows.count() == 0) {
        throw Error(std::string(root) +
                    "/TBkndTask/FixedMeta describes no data rows (an MPP12 file "
                    "carries at least the project row)");
    }
    std::vector<std::uint8_t> task_fixed;
    try {
        task_fixed = cfb.read_stream(std::string(root) + "/TBkndTask/FixedData");
    } catch (const Error& error) {
        throw Error(std::string("no MPP12 Task table: ") + error.what());
    }
    const std::vector<std::uint8_t> task_var_meta =
        read_optional_stream(cfb, root, "TBkndTask", "VarMeta", document.warnings);
    const std::vector<std::uint8_t> task_var2 =
        read_optional_stream(cfb, root, "TBkndTask", "Var2Data", document.warnings);
    const VarTable task_var =
        parse_var_meta(task_var_meta, task_var2, "Task", document.warnings);

    /* Descriptor rows (entries 0..2) are not tasks; data row 0 is the project
     * row.  The three descriptor blocks are 16 bytes each and their FixedMeta
     * state is 0x04; warn if that is not what the file holds. */
    if (task_rows.meta_present && task_rows.entries.size() >= task_rows.entry_size) {
        const std::uint8_t descriptor_state = task_rows.entries[0];
        if (descriptor_state != 0x04) {
            document.warnings.emplace_back(
                "MPP12 Task FixedMeta descriptor state is " + hex32(descriptor_state) +
                " instead of 0x04; the first three entries are still treated as "
                "descriptors");
        }
    }

    /* ---- Row 0: the project row. */
    {
        const std::vector<std::uint8_t> row =
            row_bytes(task_fixed, task_rows, 0, k12TaskRowSize);
        if (row.empty()) {
            document.warnings.emplace_back(
                "MPP12 Task row 0 (the project row) is shorter than 264 bytes; the "
                "project summary is empty");
        } else {
            document.project.uid = u32_at(row, k12TaskUid);
            document.project.row_id = u32_at(row, k12TaskId);
            document.project.duration_ticks = u32_at(row, k12TaskDuration);
            document.project.priority = u16_at(row, k12TaskPriority);
            document.project.percent_complete = u16_at(row, k12TaskPercent);
            document.project.start.raw = u32_at(row, k12TaskStart);
            document.project.finish.raw = u32_at(row, k12TaskFinish);
            if (task_var.usable &&
                read_text_field(task_var, task_var2, document.project.uid,
                                kFieldTaskName, document.project.title_raw)) {
                document.project.title = trim_ascii_spaces(document.project.title_raw);
            }
        }
    }

    /* ---- Rows 1..: live tasks, null-task placeholders and tombstones. */
    std::size_t unsupported_size_rows = 0;
    std::uint32_t first_unsupported_size = 0;
    for (std::size_t row = 1; row < task_rows.count(); ++row) {
        const std::uint8_t state = task_rows.state(row);
        const std::uint32_t size = task_rows.size[row];
        if (size == k12TaskNullRowSize) {
            const std::vector<std::uint8_t> bytes =
                row_bytes(task_fixed, task_rows, row, k12TaskNullRowSize);
            SkippedRow skipped;
            skipped.row = static_cast<std::uint32_t>(row);
            skipped.uid = bytes.empty() ? 0 : u32_at(bytes, 0);
            skipped.reason = "16-byte null-task block (FixedMeta state 0x04)";
            document.skipped_rows.push_back(std::move(skipped));
            continue;
        }
        if (size < k12TaskRowSize) {
            if (unsupported_size_rows == 0) {
                first_unsupported_size = size;
            }
            ++unsupported_size_rows;
            document.warnings.emplace_back(
                "MPP12 Task row " + std::to_string(row) + " is " +
                std::to_string(size) + " bytes, neither a 16-byte null-task block "
                "nor a 264-byte task row; it is skipped rather than guessed at");
            SkippedRow skipped;
            skipped.row = static_cast<std::uint32_t>(row);
            skipped.uid = 0;
            skipped.reason = "unsupported row size " + std::to_string(size);
            document.skipped_rows.push_back(std::move(skipped));
            continue;
        }
        const std::vector<std::uint8_t> row_bytes_value =
            row_bytes(task_fixed, task_rows, row, k12TaskRowSize);
        const std::uint32_t uid = u32_at(row_bytes_value, k12TaskUid);
        if (state == kFixedMetaStateDeleted) {
            SkippedRow skipped;
            skipped.row = static_cast<std::uint32_t>(row);
            skipped.uid = uid;
            skipped.reason = "FixedMeta entry is a tombstone (+0 = 0x02): deleted row";
            document.skipped_rows.push_back(std::move(skipped));
            document.warnings.emplace_back(
                "MPP12 Task row " + std::to_string(row) + " (uid " +
                std::to_string(uid) +
                ") has FixedMeta's tombstone flag (+0 = 0x02) and is treated as a "
                "deleted row slot, not a task");
            continue;
        }
        if (state != 0) {
            SkippedRow skipped;
            skipped.row = static_cast<std::uint32_t>(row);
            skipped.uid = uid;
            skipped.reason = "FixedMeta state byte " + hex32(state) +
                             " is not a live row (0x00) or a tombstone (0x02)";
            document.skipped_rows.push_back(std::move(skipped));
            document.warnings.emplace_back(
                "MPP12 Task row " + std::to_string(row) + " (uid " +
                std::to_string(uid) + ") has FixedMeta state " + hex32(state) +
                "; it is skipped rather than read as a task");
            continue;
        }
        Task task;
        task.row = static_cast<std::uint32_t>(row);
        task.uid = uid;
        task.row_id = u32_at(row_bytes_value, k12TaskId);
        task.duration_ticks = u32_at(row_bytes_value, k12TaskDuration);
        task.duration_format = u16_at(row_bytes_value, k12TaskDurationFormat);
        task.priority = u16_at(row_bytes_value, k12TaskPriority);
        task.percent_complete = u16_at(row_bytes_value, k12TaskPercent);
        task.start.raw = u32_at(row_bytes_value, k12TaskStart);
        task.finish.raw = u32_at(row_bytes_value, k12TaskFinish);
        /* MPP12 has no calculated pair (+96/+100 are the actuals); report the
         * effective pair twice rather than invent a second date. */
        task.calculated_start = task.start;
        task.calculated_finish = task.finish;
        task.late_start.raw = u32_at(row_bytes_value, k12TaskLateStart);
        task.late_finish.raw = u32_at(row_bytes_value, k12TaskLateFinish);
        const std::uint16_t constraint_type =
            u16_at(row_bytes_value, k12TaskConstraintType);
        if (constraint_type <= kConstraintFnlt) {
            task.constraint_type = constraint_type;
            task.constraint_date.raw = u32_at(row_bytes_value, k12TaskConstraintDate);
        } else {
            document.warnings.emplace_back(
                "MPP12 Task row " + std::to_string(row) + " (uid " +
                std::to_string(uid) + ") stores constraint type " +
                std::to_string(constraint_type) +
                " at +80; PjConstraint is 0..7, so it is reported as ASAP with no "
                "constraint date");
        }
        task.deadline.raw = u32_at(row_bytes_value, k12TaskDeadline);
        task.actual_duration_ticks = u32_at(row_bytes_value, k12TaskActualDuration);
        task.actual_start.raw = u32_at(row_bytes_value, k12TaskActualStart);
        task.actual_finish.raw = u32_at(row_bytes_value, k12TaskActualFinish);
        task.calendar_uid =
            static_cast<std::int32_t>(u32_at(row_bytes_value, k12TaskCalendarUid));
        task.start_anchor.raw = kDateNa; /* no measured MPP12 equivalent */
        task.parent_uid = u32_at(row_bytes_value, k12TaskParentUid);
        task.stored_outline_level = u16_at(row_bytes_value, k12TaskOutlineLevel);
        task.free_slack_ticks =
            static_cast<std::int32_t>(u32_at(row_bytes_value, k12TaskFreeSlack));
        task.start_slack_ticks =
            static_cast<std::int32_t>(u32_at(row_bytes_value, k12TaskStartSlack));
        task.total_slack_ticks =
            static_cast<std::int32_t>(u32_at(row_bytes_value, k12TaskTotalSlack));
        /* Project 2007 has no manual scheduling, so nothing gates critical. */
        task.manually_scheduled = false;
        task.critical = task.total_slack_ticks <= 0;
        if (task_rows.meta_present) {
            task.milestone =
                (task_rows.meta_byte(row, k12FixedMetaMilestoneByte) &
                 k12FixedMetaFlagMilestone) != 0;
            task.milestone_stored = true;
        } else {
            task.milestone = task.duration_ticks == 0;
            task.milestone_stored = false;
        }
        if (task_var.usable &&
            read_text_field(task_var, task_var2, uid, kFieldTaskName, task.name_raw)) {
            task.name = trim_ascii_spaces(task.name_raw);
        }
        task.custom_fields = decode_var_custom_fields(task_var, task_var2, uid);
        if (task_rows.meta_present) {
            append_mpp12_flags(task_rows, row, task.custom_fields);
            // Wave 24 (W24-15): the same Flag1..Flag20 view as MPP14.  The
            // field ids are NOT contiguous: Flag11..20 are 0x0B400124..12D,
            // not Flag10+1.., so map through custom_field_index() rather than
            // subtracting ids (review finding: the first cut silently left
            // flags[10..19] false and could copy Number1..5 into their slots).
            task.flags_stored = true;
            for (const CustomFieldValue& value : task.custom_fields) {
                if (custom_field_kind(value.field_id) != FieldKind::flag) {
                    continue;
                }
                const unsigned flag_index = custom_field_index(value.field_id);
                if (flag_index >= 1 && flag_index <= task.flags.size()) {
                    task.flags[flag_index - 1] = value.flag;
                }
            }
            task.flag10 = task.flags[9];
            task.flag10_stored = true;
        }
        /* Inserted-project tag: Task VarMeta key 0x0b4001ca. */
        if (task_var.usable) {
            for (const std::uint32_t offset : offsets_for(task_var, uid,
                                                          kFieldSubprojectUidOffset)) {
                std::vector<std::uint8_t> payload;
                if (read_block(task_var2, offset, payload) && payload.size() == 4) {
                    task.subproject_uid_offset = u32_at(payload, 0);
                    task.subproject_uid_offset_present = true;
                    break;
                }
            }
        }
        document.tasks.push_back(std::move(task));
    }

    /* If EVERY data row is an unsupported size, this is not the Project 2007
     * task layout at all: refuse with a positioned error instead of returning a
     * project with no tasks.  (MPXJ-written "MPP12" files are such a variant:
     * they carry 206-byte task rows.  A real Project 2007 file with no tasks
     * has only the project row, so it does not trip this.) */
    if (unsupported_size_rows > 0 && unsupported_size_rows == task_rows.count() - 1) {
        throw Error("MPP12 Task table at " + std::string(root) +
                    "/TBkndTask/FixedData holds " +
                    std::to_string(unsupported_size_rows) +
                    " data rows of " + std::to_string(first_unsupported_size) +
                    " bytes each (row 0 is the project row); the Project-2007 "
                    "layout this reader implements is 264 bytes.  The file may be "
                    "an MPXJ-written MPP12 or a different variant; its record size "
                    "is not guessed at.");
    }

    /* Sibling numbering + stored-level cross-check. */
    number_mpp12_outline(document);

    /* The subproject source paths, matched to the rows by task UniqueID. */
    parse_subprojects_mpp12(cfb, root, document);
    for (Task& task : document.tasks) {
        for (const SubprojectRecord& record : document.subprojects) {
            if (record.task_uid == task.uid) {
                task.subproject_file = record.path;
                break;
            }
        }
    }

    std::unordered_set<std::uint32_t> live_task_uids;
    live_task_uids.reserve(document.tasks.size());
    for (const Task& task : document.tasks) {
        live_task_uids.insert(task.uid);
    }

    /* ---- Resources: 48 + 188*rows, FixedMeta 37*(rows+3).  Row 0 is the uid-0
     * placeholder. */
    {
        RowTable rsc_rows;
        if (read_row_table(cfb, root, "TBkndRsc", "Resource", kRscHeaderSize,
                           k12RscRowSize, kRscFixedMetaEntrySize,
                           kFixedMetaDescriptors, rsc_rows, document.warnings) &&
            rsc_rows.count() > 1) {
            std::vector<std::uint8_t> rsc_fixed;
            try {
                rsc_fixed = cfb.read_stream(std::string(root) + "/TBkndRsc/FixedData");
            } catch (const Error&) {
                rsc_fixed.clear();
            }
            std::vector<std::uint8_t> rsc_var2 =
                read_optional_stream(cfb, root, "TBkndRsc", "Var2Data",
                                     document.warnings);
            const VarTable rsc_var = parse_var_meta(
                read_optional_stream(cfb, root, "TBkndRsc", "VarMeta",
                                     document.warnings),
                rsc_var2, "Resource", document.warnings);
            /* Resource type: FixedMeta +9 bit 0x02 = Work; when clear, the
             * Fixed2Meta entry's +8 bit 0x10 = Cost, else Material.  The two
             * metadata streams pair by ENTRY INDEX (as in MPP14). */
            std::vector<std::uint8_t> rsc_fixed2_type(rsc_rows.count(), 0);
            std::vector<bool> rsc_fixed2_known(rsc_rows.count(), false);
            const std::vector<std::uint8_t> rsc_fixed2_meta =
                read_optional_stream(cfb, root, "TBkndRsc", "Fixed2Meta",
                                     document.warnings);
            if (rsc_fixed2_meta.size() >= kFixedMetaHeaderSize &&
                u32_at(rsc_fixed2_meta, 0) == kMetaMagic) {
                const std::uint32_t declared = u32_at(rsc_fixed2_meta, 8);
                const std::size_t available =
                    rsc_fixed2_meta.size() - kFixedMetaHeaderSize;
                const std::size_t count =
                    declared != 0 ? declared : available / kRscFixed2MetaEntrySize;
                /* The entries include the three descriptors (as FixedMeta does),
                 * so data row `row` is entry row + 3. */
                if (count != 0 && available % count == 0 &&
                    count >= rsc_rows.count() + kFixedMetaDescriptors) {
                    const std::size_t stride = available / count;
                    for (std::size_t row = 0; row < rsc_rows.count(); ++row) {
                        const std::size_t entry =
                            kFixedMetaHeaderSize +
                            (row + kFixedMetaDescriptors) * stride;
                        if (entry + 8 >= rsc_fixed2_meta.size()) {
                            continue;
                        }
                        rsc_fixed2_type[row] = rsc_fixed2_meta[entry + 8];
                        rsc_fixed2_known[row] = true;
                    }
                } else if (count != 0) {
                    document.warnings.emplace_back(
                        "MPP12 Resource Fixed2Meta holds " + std::to_string(count) +
                        " entries for " + std::to_string(rsc_rows.count()) +
                        " rows (expected " +
                        std::to_string(rsc_rows.count() + kFixedMetaDescriptors) +
                        "); Resource Type is reported as Work with "
                        "type_stored=false");
                }
            }
            for (std::size_t row = 1; row < rsc_rows.count(); ++row) {
                const std::vector<std::uint8_t> bytes =
                    row_bytes(rsc_fixed, rsc_rows, row, k12RscRowSize);
                const std::uint32_t uid = u32_at(bytes, k12RscUid);
                const std::uint8_t state = rsc_rows.state(row);
                if (bytes.empty() || uid == 0 || state != 0) {
                    SkippedRow skipped;
                    skipped.row = static_cast<std::uint32_t>(row);
                    skipped.uid = uid;
                    skipped.reason = uid == 0 ? "UniqueID 0" : "not live";
                    document.skipped_resources.push_back(std::move(skipped));
                    continue;
                }
                Resource resource;
                resource.row = static_cast<std::uint32_t>(row);
                resource.uid = uid;
                resource.row_id = u32_at(bytes, k12RscId);
                resource.max_units_x100 = f64_at(bytes, k12RscMaxUnits);
                const bool work_bit =
                    rsc_rows.meta_present &&
                    (rsc_rows.meta_byte(row, k12RscFixedMetaWorkByte) &
                     k12RscFixedMetaWorkFlag) != 0;
                if (!rsc_rows.meta_present) {
                    resource.type = ResourceType::work;
                    resource.type_stored = false;
                } else if (work_bit) {
                    resource.type = ResourceType::work;
                    resource.type_stored = true;
                } else if (rsc_fixed2_known[row]) {
                    resource.type =
                        (rsc_fixed2_type[row] & k12RscFixed2MetaCostFlag) != 0
                            ? ResourceType::cost
                            : ResourceType::material;
                    resource.type_stored = true;
                } else {
                    resource.type = ResourceType::work;
                    resource.type_stored = false;
                    document.warnings.emplace_back(
                        "MPP12 Resource row " + std::to_string(row) + " (uid " +
                        std::to_string(uid) + ") is non-Work in FixedMeta but has no "
                        "Fixed2Meta type word; its type is reported as Work with "
                        "type_stored=false");
                }
                if (rsc_var.usable &&
                    read_text_field(rsc_var, rsc_var2, uid, kFieldRscName,
                                    resource.name_raw)) {
                    resource.name = trim_ascii_spaces(resource.name_raw);
                }
                std::string initials;
                if (rsc_var.usable &&
                    read_text_field(rsc_var, rsc_var2, uid, kFieldRscInitials,
                                    initials)) {
                    resource.initials = trim_ascii_spaces(initials);
                }
                if (rsc_var.usable &&
                    read_text_field(rsc_var, rsc_var2, uid, kFieldRscMaterialLabel,
                                    resource.material_label_raw)) {
                    resource.material_label =
                        trim_ascii_spaces(resource.material_label_raw);
                }
                document.resources.push_back(std::move(resource));
            }
        }
    }

    /* ---- Links: 20-byte rows, no header.  type +12, LagFormat +14, lag +16
     * (MPP14 swaps the last two). */
    {
        RowTable cons_rows;
        if (read_row_table(cfb, root, "TBkndCons", "Link", 0, k12ConsRowSize,
                           kConsFixedMetaEntrySize, 0, cons_rows,
                           document.warnings) &&
            cons_rows.count() > 0) {
            std::vector<std::uint8_t> cons_fixed;
            try {
                cons_fixed = cfb.read_stream(std::string(root) +
                                             "/TBkndCons/FixedData");
            } catch (const Error&) {
                cons_fixed.clear();
            }
            for (std::size_t row = 0; row < cons_rows.count(); ++row) {
                const std::vector<std::uint8_t> bytes =
                    row_bytes(cons_fixed, cons_rows, row, k12ConsRowSize);
                const std::uint32_t predecessor = u32_at(bytes, 4);
                const std::uint32_t successor = u32_at(bytes, 8);
                const std::uint8_t state = cons_rows.state(row);
                if (predecessor == 0 && successor == 0) {
                    SkippedRow skipped;
                    skipped.row = static_cast<std::uint32_t>(row);
                    skipped.uid = u32_at(bytes, 0);
                    skipped.reason = "predecessor and successor are both 0";
                    document.skipped_links.push_back(std::move(skipped));
                    continue;
                }
                if (cons_rows.meta_present && state == kFixedMetaStateDeleted) {
                    SkippedRow skipped;
                    skipped.row = static_cast<std::uint32_t>(row);
                    skipped.uid = u32_at(bytes, 0);
                    skipped.reason = "FixedMeta entry is a tombstone (+0 = 0x02)";
                    document.skipped_links.push_back(std::move(skipped));
                    continue;
                }
                Link link;
                link.row = static_cast<std::uint32_t>(row);
                link.uid = u32_at(bytes, 0);
                link.predecessor_uid = predecessor;
                link.successor_uid = successor;
                link.type_raw = u16_at(bytes, k12ConsType);
                link.lag_units = u16_at(bytes, k12ConsLagUnits);
                link.lag_ticks =
                    static_cast<std::int32_t>(u32_at(bytes, k12ConsLag));
                link.predecessor_known = live_task_uids.count(predecessor) != 0;
                link.successor_known = live_task_uids.count(successor) != 0;
                document.links.push_back(link);
            }
            for (const Link& link : document.links) {
                if (!link.predecessor_known) {
                    document.warnings.emplace_back(
                        "Link uid " + std::to_string(link.uid) +
                        " names predecessor task " +
                        std::to_string(link.predecessor_uid) +
                        ", which no live task holds");
                }
                if (!link.successor_known) {
                    document.warnings.emplace_back(
                        "Link uid " + std::to_string(link.uid) +
                        " names successor task " +
                        std::to_string(link.successor_uid) +
                        ", which no live task holds");
                }
            }
        }
    }

    /* ---- Assignments: 142-byte rows, no header.  uid +0, task +4, resource
     * +8, Start +12, Finish +16, Units +54, Work +62.  MPP12 stores no Resume
     * date in the row: Assignment::resume stays kDateNa. */
    {
        RowTable assn_rows;
        if (read_row_table(cfb, root, "TBkndAssn", "Assignment", 0, k12AssnRowSize,
                           kAssnFixedMetaEntrySize, 0, assn_rows,
                           document.warnings) &&
            assn_rows.count() > 0) {
            std::vector<std::uint8_t> assn_fixed;
            try {
                assn_fixed = cfb.read_stream(std::string(root) +
                                             "/TBkndAssn/FixedData");
            } catch (const Error&) {
                assn_fixed.clear();
            }
            for (std::size_t row = 0; row < assn_rows.count(); ++row) {
                const std::vector<std::uint8_t> bytes =
                    row_bytes(assn_fixed, assn_rows, row, k12AssnRowSize);
                const std::uint8_t state = assn_rows.state(row);
                if (state != 0) {
                    SkippedRow skipped;
                    skipped.row = static_cast<std::uint32_t>(row);
                    skipped.uid = u32_at(bytes, k12AssnUid);
                    skipped.task_uid = u32_at(bytes, k12AssnTaskUid);
                    skipped.reason = state == kFixedMetaStateDeleted
                                         ? "FixedMeta entry is a tombstone (+0 = 0x02)"
                                         : "FixedMeta state " + hex32(state);
                    document.skipped_assignments.push_back(std::move(skipped));
                    continue;
                }
                Assignment assignment;
                assignment.row = static_cast<std::uint32_t>(row);
                assignment.uid = u32_at(bytes, k12AssnUid);
                assignment.task_uid = u32_at(bytes, k12AssnTaskUid);
                assignment.resource_uid_raw = u32_at(bytes, k12AssnResourceUid);
                assignment.units_x100 = f64_at(bytes, k12AssnUnits);
                assignment.work_x1000 = f64_at(bytes, k12AssnWork);
                assignment.start.raw = u32_at(bytes, k12AssnStart);
                assignment.finish.raw = u32_at(bytes, k12AssnFinish);
                assignment.resume.raw = kDateNa;
                if (assignment.task_uid == 0) {
                    SkippedRow skipped;
                    skipped.row = static_cast<std::uint32_t>(row);
                    skipped.uid = assignment.uid;
                    skipped.task_uid = 0;
                    skipped.reason = "placeholder row with task UniqueID 0";
                    document.skipped_assignments.push_back(std::move(skipped));
                    continue;
                }
                if (live_task_uids.count(assignment.task_uid) == 0) {
                    document.warnings.emplace_back(
                        "Assignment uid " + std::to_string(assignment.uid) +
                        " names task " + std::to_string(assignment.task_uid) +
                        ", which no live task holds");
                }
                document.assignments.push_back(assignment);
            }
        }
    }
}

/* Version detection.  Never assume a root: the props stream says which format,
 * and the storage that actually holds TBkndTask/FixedData is the fallback.  A
 * disagreement is a warning plus the storage root winning, because that is
 * where the bytes are. */
struct FormatInfo {
    MppVersion version = MppVersion::mpp14;
    std::string table_root;
    std::string view_root;
    std::string props_stream;
};
FormatInfo detect_format(const CompoundFile& cfb, std::vector<std::string>& warnings) {
    const bool has12 = cfb.has_stream("Props12");
    const bool has14 = cfb.has_stream("Props14");
    const bool tables12 = cfb.has_stream("112/TBkndTask/FixedData") ||
                          cfb.has_stream("112/TBkndTask/FixedMeta");
    const bool tables14 = cfb.has_stream("114/TBkndTask/FixedData") ||
                          cfb.has_stream("114/TBkndTask/FixedMeta");
    FormatInfo info;
    if (has12 && !has14) {
        info.version = MppVersion::mpp12;
        info.table_root = "112";
        info.view_root = "212";
        info.props_stream = "Props12";
    } else if (has14 && !has12) {
        info.version = MppVersion::mpp14;
        info.table_root = "114";
        info.view_root = "214";
        info.props_stream = "Props14";
    } else if (has12 && has14) {
        warnings.emplace_back("the file carries both Props12 and Props14; the table "
                              "storage decides the format");
        if (tables12 && !tables14) {
            info = {MppVersion::mpp12, "112", "212", "Props12"};
        } else {
            info = {MppVersion::mpp14, "114", "214", "Props14"};
        }
    } else {
        warnings.emplace_back(
            "the file has neither Props12 nor Props14; the table storage decides "
            "the format");
        if (tables12 && !tables14) {
            info = {MppVersion::mpp12, "112", "212", "Props12"};
        } else if (tables14) {
            info = {MppVersion::mpp14, "114", "214", "Props14"};
        } else {
            throw Error("not a readable .mpp container: no Props12/Props14 stream and "
                        "no 112/TBkndTask or 114/TBkndTask table");
        }
    }
    const std::string expected_root = info.version == MppVersion::mpp12 ? "112" : "114";
    const std::string other_root = info.version == MppVersion::mpp12 ? "114" : "112";
    if (!cfb.has_stream(expected_root + "/TBkndTask/FixedData") &&
        cfb.has_stream(other_root + "/TBkndTask/FixedData")) {
        warnings.emplace_back(
            "the props stream says " + std::string(version_name(info.version)) +
            " but only the " + other_root +
            "/ table tree exists; the tree decides the format");
        if (other_root == "112") {
            info = {MppVersion::mpp12, "112", "212", "Props12"};
        } else {
            info = {MppVersion::mpp14, "114", "214", "Props14"};
        }
    }
    return info;
}

/* True when the container has a STORAGE (not a stream) with this name, e.g.
 * "214".  CompoundFile::has_stream only matches stream entries. */
bool has_storage(const CompoundFile& cfb, std::string_view name) {
    const std::string prefix = std::string(name) + "/";
    for (const std::string& path : cfb.stream_paths()) {
        if (path.size() > prefix.size() &&
            cfb_path_matches(path.substr(0, path.find('/')), name)) {
            return true;
        }
    }
    return false;
}

/* RESOURCE CALENDAR.  A resource's calendar is not a word in the 172-byte
 * (MPP14) / 188-byte (MPP12) resource row: it is a TBkndCal/FixedData row whose
 * resource column (+4 MPP14, +8 MPP12) equals the resource's uid, and the
 * calendar's own uid is that row's key column (+8 MPP14, +0 MPP12).  The
 * mapping is MEASURED against Project's own MSPDI export of
 * corpus/probe/w9-resourcetypes.mpp: the table holds (resource 1 -> uid 3),
 * (2 -> 4), (3 -> 5) and the export writes <CalendarUID>3/4/5</CalendarUID>.
 * Files also carry a row with resource column 0 (a calendar for the null
 * resource); no live resource has uid 0, so it never matches.  A resource with
 * no such row keeps -1: the file stores no word to read, and the project
 * calendar then applies -- reporting a guessed uid would be invented state. */
void resolve_resource_calendars(Document& document) {
    for (Resource& resource : document.resources) {
        resource.calendar_uid = -1;
        for (Calendar const& calendar : document.calendars) {
            if (calendar.resource_uid != static_cast<std::int32_t>(resource.uid)) {
                continue;
            }
            if (resource.calendar_uid != -1) {
                document.warnings.emplace_back(
                    "resource uid " + std::to_string(resource.uid) +
                    " is named by more than one calendar row (uids " +
                    std::to_string(resource.calendar_uid) + " and " +
                    std::to_string(calendar.uid) +
                    "); the first is used for Resource::calendar_uid");
                break;
            }
            resource.calendar_uid = calendar.uid;
        }
    }
}

/* ---- The project props records this reader decodes.  See ProjectRow in
 * reader.h for the evidence.
 *
 * The props streams use core/props' measured framing: a 16-byte header, then
 * records back to back (u32 payload_length, u32 id, u32 kind, payload), scanned
 * until an id whose byte 2 is not 0x40 (core/props `is_props_id`).  The
 * new-task default is in the TABLE props (114/112); the project-summary row and
 * the Timeline pane name are in the VIEW props (214/212).  core/mpp does not
 * link core/props for this, so the framing is the same five lines here. */
struct ProjectPropsRecords {
    bool new_tasks_manual = false;
    bool new_tasks_manual_stored = false;
    bool summary_row_shown = false;
    bool summary_row_stored = false;
    bool timeline_shown = true;
    bool timeline_stored = false;
    PackedDate status_date;
    bool status_date_stored = false;
};

std::vector<std::uint8_t> read_props_stream(const CompoundFile& cfb,
                                            const std::string& root) {
    const std::string path = root + "/Props";
    if (!cfb.has_stream(path)) {
        return {};
    }
    try {
        return cfb.read_stream(path);
    } catch (const Error&) {
        return {};
    }
}

template <typename Visit>
void for_each_props_record(const std::vector<std::uint8_t>& props, Visit&& visit) {
    if (props.size() < 16) {
        return;
    }
    std::size_t offset = 16;
    while (offset + 12 <= props.size()) {
        const std::uint32_t length = u32_at(props, offset);
        const std::uint32_t id = u32_at(props, offset + 4);
        if ((id & 0x00FF0000u) != 0x00400000u) {
            break; /* record framing has ended; the rest is the opaque tail */
        }
        if (static_cast<std::uint64_t>(offset) + 12u + length > props.size()) {
            break;
        }
        visit(id, u32_at(props, offset + 8), offset + 12, length);
        offset += 12u + length;
    }
}

void warn_props_shape(std::vector<std::string>* warnings, const std::string& path,
                      std::uint32_t id, std::uint32_t kind, std::uint32_t length,
                      const char* expected) {
    if (warnings == nullptr) {
        return;
    }
    warnings->emplace_back(path + " stores record " + hex32(id) + " as kind " +
                           std::to_string(kind) + " / " + std::to_string(length) +
                           " bytes, not the measured " + expected + "; it is not decoded");
}

ProjectPropsRecords decode_project_props(const CompoundFile& cfb,
                                         const FormatInfo& format,
                                         std::vector<std::string>* warnings) {
    ProjectPropsRecords out;
    const std::string table_path = format.table_root + "/Props";
    const std::vector<std::uint8_t> table_props = read_props_stream(cfb, format.table_root);
    for_each_props_record(table_props, [&](std::uint32_t id, std::uint32_t kind,
                                           std::size_t payload, std::uint32_t length) {
        if (id == kProjectPropsStatusDate) {
            if (kind != 4u || length < 4u) {
                warn_props_shape(warnings, table_path, id, kind, length,
                                 "kind 4 / 4 bytes (packed date)");
                return;
            }
            out.status_date.raw = u32_at(table_props, payload);
            out.status_date_stored = true;
            return;
        }
        if (id != kProjectPropsNewTasksManual) {
            return;
        }
        if (kind != 2u || length < 2u) {
            warn_props_shape(warnings, table_path, id, kind, length, "kind 2 / 2 bytes");
            return;
        }
        out.new_tasks_manual = u16_at(table_props, payload) != 0;
        out.new_tasks_manual_stored = true;
    });

    const std::string view_path = format.view_root + "/Props";
    const std::vector<std::uint8_t> view_props = read_props_stream(cfb, format.view_root);
    for_each_props_record(view_props, [&](std::uint32_t id, std::uint32_t kind,
                                          std::size_t payload, std::uint32_t length) {
        if (id == kProjectPropsSummaryRow) {
            if (kind != 2u || length < 2u) {
                warn_props_shape(warnings, view_path, id, kind, length,
                                 "kind 2 / 2 bytes");
                return;
            }
            out.summary_row_shown = u16_at(view_props, payload) != 0;
            out.summary_row_stored = true;
            return;
        }
        if (id == kProjectPropsTimelinePane) {
            if (kind != 0u) {
                warn_props_shape(warnings, view_path, id, kind, length,
                                 "kind 0 (UTF-16 text)");
                return;
            }
            // "Time&line" when the strip is shown; a zero-length/zero payload
            // when it is hidden.  Decoding stops at the first NUL.
            std::string name;
            for (std::size_t index = 0; index + 1u < length; index += 2u) {
                const std::uint16_t unit = u16_at(view_props, payload + index);
                if (unit == 0) {
                    break;
                }
                name.push_back(unit < 0x80u ? static_cast<char>(unit) : '?');
            }
            out.timeline_shown = !name.empty();
            out.timeline_stored = true;
        }
    });
    return out;
}

Document parse_document(const CompoundFile& cfb) {
    Document document;
    /* Container-level anomalies (FAT slack) come first: they describe the file
     * before any table is parsed and are also visible to a caller that only
     * wants the container. */
    for (const std::string& warning : cfb.warnings()) {
        document.warnings.push_back(warning);
    }
    const FormatInfo format = detect_format(cfb, document.warnings);
    document.version = format.version;
    document.table_root = format.table_root;
    document.view_root = format.view_root;
    if (!has_storage(cfb, format.view_root)) {
        document.warnings.emplace_back("the file has no " + format.view_root +
                                       "/ view-state storage (it is opaque to this "
                                       "reader either way)");
    }
    if (cfb.has_stream(format.props_stream)) {
        read_version_and_password(cfb, format.props_stream, document);
    } else {
        document.warnings.emplace_back("no top-level " + format.props_stream +
                                       " stream; the writer version and the password "
                                       "flag are unknown");
    }
    parse_calendars(cfb, format.table_root, format.version, document);
    if (format.version == MppVersion::mpp12) {
        parse_mpp12(cfb, format.table_root, document);
    } else {
        parse_mpp14(cfb, format.table_root, document);
    }
    /* The project-level records that live in props, not in a task row.
     * Decoded for BOTH versions: an original Project 2007 MPP12 file has no
     * new-task record (the field reports not-stored), but a 2010+ project
     * down-saved to MPP12 keeps it in 112/Props. */
    const ProjectPropsRecords project_props =
        decode_project_props(cfb, format, &document.warnings);
    document.project.new_tasks_manual = project_props.new_tasks_manual;
    document.project.new_tasks_manual_stored = project_props.new_tasks_manual_stored;
    document.project.summary_row_shown = project_props.summary_row_shown;
    document.project.summary_row_stored = project_props.summary_row_stored;
    document.project.timeline_shown = project_props.timeline_shown;
    document.project.timeline_stored = project_props.timeline_stored;
    document.project.status_date = project_props.status_date;
    document.project.status_date_stored = project_props.status_date_stored;
    resolve_resource_calendars(document);
    return document;
}

}  // namespace

std::size_t Document::link_count_with_live_endpoints() const {
    return static_cast<std::size_t>(
        std::count_if(links.begin(), links.end(),
                      [](const Link& link) { return link.endpoints_live(); }));
}

std::size_t Document::link_count_with_dead_endpoints() const {
    return links.size() - link_count_with_live_endpoints();
}

std::string Link::type_name() const {
    switch (type_raw & 0x3u) {
        case 0:
            return "FF";
        case 1:
            return "FS";
        case 2:
            return "SF";
        case 3:
            return "SS";
        default:
            return "?";
    }
}

std::string format_date(const PackedDate& date) {
    if (!date.valid()) {
        return {};
    }
    const CivilDate civil =
        civil_from_days(static_cast<long long>(date.day()) + kPackedDateEpochDay);
    const int seconds = date.seconds_of_day();
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02u %02d:%02d", civil.year,
                  civil.month, civil.day, seconds / 3600, (seconds % 3600) / 60);
    std::string out(buffer);
    if (seconds % 60 != 0) {
        std::snprintf(buffer, sizeof buffer, ":%02d", seconds % 60);
        out += buffer;
    }
    return out;
}

std::string format_date_iso(const PackedDate& date) {
    if (!date.valid()) {
        return {};
    }
    const CivilDate civil =
        civil_from_days(static_cast<long long>(date.day()) + kPackedDateEpochDay);
    const int seconds = date.seconds_of_day();
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02u %02d:%02d:%02d+00:00",
                  civil.year, civil.month, civil.day, seconds / 3600,
                  (seconds % 3600) / 60, seconds % 60);
    return std::string(buffer);
}

std::string format_duration_days(std::uint32_t ticks) {
    const std::uint32_t days = ticks / kTicksPerDay;
    const std::uint32_t remainder = ticks % kTicksPerDay;
    if (remainder == 0) {
        return std::to_string(days);
    }
    std::uint64_t fraction =
        (static_cast<std::uint64_t>(remainder) * 10000u + kTicksPerDay / 2u) / kTicksPerDay;
    std::uint64_t whole = days;
    if (fraction >= 10000u) {
        fraction -= 10000u;
        ++whole;
    }
    std::string digits = std::to_string(fraction);
    digits.insert(0, 4 - std::min<std::size_t>(4, digits.size()), '0');
    while (!digits.empty() && digits.back() == '0') {
        digits.pop_back();
    }
    return std::to_string(whole) + "." + digits;
}

std::uint32_t duration_minutes(std::uint32_t ticks) {
    return ticks / kTicksPerMinute;
}

Document load_file(std::string const& path) {
    const CompoundFile file = CompoundFile::from_file(path);
    return parse_document(file);
}

Document load_bytes(std::vector<std::uint8_t> bytes) {
    const CompoundFile file = CompoundFile::from_bytes(std::move(bytes));
    return parse_document(file);
}

NewTaskMode read_new_task_mode(std::string const& path) {
    NewTaskMode mode;
    try {
        const CompoundFile file = CompoundFile::from_file(path);
        std::vector<std::string> warnings;
        const FormatInfo format = detect_format(file, warnings);
        const ProjectPropsRecords props = decode_project_props(file, format, nullptr);
        if (props.new_tasks_manual_stored) {
            mode.known = true;
            mode.manual = props.new_tasks_manual;
        }
    } catch (const Error&) {
        /* Unreadable container: unknown, so the caller keeps its fallback. */
    }
    return mode;
}

}  // namespace mpp
