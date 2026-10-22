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
 *             0x0c400001, Initials = 0x0c400002.  The row's only work-value is
 *             the double at +8: 10000.0 = MaxUnits 100 % (the x10000 encoding
 *             COM-validated for the in-memory assignment record).  No fixture
 *             varies MaxUnits, so the *value* is measured and the *identity* is
 *             inference.  Resource type is not in the row; reported as Work.
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
constexpr std::size_t kRscRowId = 0;
constexpr std::size_t kRscRowUid = 4;
constexpr std::size_t kRscRowMaxUnits = 8; /* double, x100 */

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
 * row 1 is "Site establishment " in the file) but its COM surface returns the
 * trimmed string, and tests/data/project_a_golden.tsv was exported that way.  The
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

std::vector<bool> fixed_meta_coverage(const CompoundFile& cfb, std::string_view table,
                                      std::string_view label, std::size_t header_size,
                                      std::size_t row_size, std::size_t entry_size,
                                      std::size_t descriptors, std::uint32_t rows,
                                      const std::vector<std::uint32_t>& uid_by_row,
                                      std::vector<std::string>& warnings,
                                      std::vector<std::uint32_t>* entry_offsets = nullptr,
                                      std::vector<std::uint8_t>* row_flag_byte = nullptr) {
    std::vector<bool> covered(rows, false);
    if (row_flag_byte != nullptr) {
        row_flag_byte->assign(rows, 0);
    }
    const std::string path = "114/" + std::string(table) + "/FixedMeta";
    if (!cfb.has_stream(path)) {
        warnings.emplace_back(std::string(label) +
                              " table has no FixedMeta stream; every row is "
                              "assumed live");
        /* Make that assumption true: a table without metadata has no marker for
         * a deleted row, and returning all-false would drop the whole table
         * (Link and Assignment liveness read this vector). */
        std::fill(covered.begin(), covered.end(), true);
        return covered;
    }
    std::vector<std::uint8_t> meta;
    try {
        meta = cfb.read_stream(path);
    } catch (const Error& error) {
        warnings.emplace_back(std::string(label) + " FixedMeta is unreadable: " +
                              error.what());
        return covered;
    }
    if (meta.size() < kFixedMetaHeaderSize) {
        warnings.emplace_back(std::string(label) + " FixedMeta is " +
                              std::to_string(meta.size()) +
                              " bytes, too short for its 16-byte header");
        return covered;
    }
    if (u32_at(meta, 0) != kMetaMagic) {
        warnings.emplace_back(std::string(label) + " FixedMeta has magic " +
                              hex32(u32_at(meta, 0)) +
                              " instead of 0xfadfadba; its contents are not checked");
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

std::vector<std::uint8_t> read_optional_stream(const CompoundFile& cfb,
                                               std::string_view table,
                                               std::string_view name,
                                               std::vector<std::string>& warnings) {
    const std::string path = "114/" + std::string(table) + "/" + std::string(name);
    if (!cfb.has_stream(path)) {
        warnings.emplace_back("114/" + std::string(table) + " has no " +
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
bool read_table_fixed(const CompoundFile& cfb, std::string_view table,
                      std::string_view label, std::size_t header_size,
                      std::size_t row_size, std::vector<std::uint8_t>& fixed,
                      std::uint32_t& rows, std::vector<std::string>& warnings) {
    const std::string path = "114/" + std::string(table) + "/FixedData";
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

Outline read_outline(const CompoundFile& cfb, std::uint32_t rows,
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
        read_optional_stream(cfb, "TBkndTask", "Fixed2Data", warnings);
    const std::vector<std::uint8_t> fixed2_meta =
        read_optional_stream(cfb, "TBkndTask", "Fixed2Meta", warnings);
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
    for (std::vector<std::uint32_t>& kids : children) {
        std::sort(kids.begin(), kids.end());
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

Document parse_document(const CompoundFile& cfb) {
    Document document;

    /* ---- Task table (required).  Geometry: 48 + 202*rows, FixedMeta 47. */
    std::vector<std::uint8_t> fixed;
    try {
        fixed = cfb.read_stream("114/TBkndTask/FixedData");
    } catch (const Error& error) {
        throw Error(std::string("no Task table: ") + error.what() +
                    " (an MPP14 file stores it at 114/TBkndTask/FixedData)");
    }
    const std::vector<std::uint8_t> var_meta =
        read_optional_stream(cfb, "TBkndTask", "VarMeta", document.warnings);
    const std::vector<std::uint8_t> var2 =
        read_optional_stream(cfb, "TBkndTask", "Var2Data", document.warnings);

    if (fixed.size() < kFixedHeaderSize) {
        throw Error("TBkndTask/FixedData is " + std::to_string(fixed.size()) +
                    " bytes, smaller than its 48-byte header");
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
        return document;
    }

    const std::vector<std::uint32_t> uid_by_row =
        uids_by_row(fixed, kFixedHeaderSize, kFixedRowSize, rows, kRowUid);
    warn_duplicate_uids(uid_by_row, "Task", true, document.warnings);

    const VarTable table = parse_var_meta(var_meta, var2, "Task", document.warnings);
    std::vector<std::uint32_t> task_meta_offsets;
    std::vector<std::uint8_t> task_meta_flag_bytes;
    const bool task_has_fixed_meta = cfb.has_stream("114/TBkndTask/FixedMeta");
    const std::vector<bool> fixed_meta_live =
        fixed_meta_coverage(cfb, "TBkndTask", "Task", kFixedHeaderSize, kFixedRowSize,
                            kFixedMetaEntrySize, kFixedMetaDescriptors, rows, uid_by_row,
                            document.warnings, &task_meta_offsets, &task_meta_flag_bytes);

    /* Liveness, computed once: a row with no VarMeta group has no Name entry and
     * MS Project does not report it (PROJECT_A row 67, uid 10856; PROJECT_B rows 1..12).
     * Deleted tasks leave their FixedData slot and their Fixed2Data record
     * behind, so the task loop and the outline walk must agree on which rows are
     * live -- otherwise a deleted slot would consume an outline number. */
    std::vector<bool> live_row(rows, false);
    live_row[0] = true;
    for (std::uint32_t row = 1; row < rows; ++row) {
        const std::uint32_t uid = uid_by_row[row];
        const bool has_var_group = table.usable &&
                                   (table.group_span.count(uid) != 0 ||
                                    table.group_span.count(uid | kSecondaryUidFamily) != 0);
        live_row[row] = !table.usable || has_var_group;
    }

    /* ---- Outline: Fixed2Data's stored parent links -> level, number, summary. */
    const Outline outline =
        read_outline(cfb, rows, task_meta_offsets, live_row, document.warnings);

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
            skipped.reason =
                "no VarMeta group (no Name entry): deleted row slot";
            skipped.orphan_name = orphan_name_for_row(table, var2, row, uid_by_row);
            std::string detail = "row " + std::to_string(row) + " (uid " +
                                 std::to_string(uid) +
                                 ") has no VarMeta group and is treated as a deleted row "
                                 "slot, not a task";
            if (!fixed_meta_live[row]) {
                detail += " (FixedMeta has no entry for it either)";
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
        task.priority = u16_at(fixed, base + kRowPriority);
        task.percent_complete = u16_at(fixed, base + kRowPercent);
        task.start.raw = u32_at(fixed, base + kRowStart);
        task.finish.raw = u32_at(fixed, base + kRowFinish);
        task.calculated_start.raw = u32_at(fixed, base + kRowCalculatedStart);
        task.calculated_finish.raw = u32_at(fixed, base + kRowCalculatedFinish);
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
        if (read_table_fixed(cfb, "TBkndRsc", "Resource", kRscHeaderSize, kRscRowSize,
                             rsc_fixed, rsc_rows, document.warnings) &&
            rsc_rows > 0) {
            const std::vector<std::uint32_t> rsc_uid_by_row =
                uids_by_row(rsc_fixed, kRscHeaderSize, kRscRowSize, rsc_rows,
                            kRscRowUid);
            warn_duplicate_uids(rsc_uid_by_row, "Resource", false, document.warnings);
            const std::vector<std::uint8_t> rsc_var_meta =
                read_optional_stream(cfb, "TBkndRsc", "VarMeta", document.warnings);
            const std::vector<std::uint8_t> rsc_var2 =
                read_optional_stream(cfb, "TBkndRsc", "Var2Data", document.warnings);
            const VarTable rsc_table =
                parse_var_meta(rsc_var_meta, rsc_var2, "Resource", document.warnings);
            const std::vector<bool> rsc_live =
                fixed_meta_coverage(cfb, "TBkndRsc", "Resource", kRscHeaderSize,
                                    kRscRowSize, kRscFixedMetaEntrySize,
                                    kFixedMetaDescriptors, rsc_rows, rsc_uid_by_row,
                                    document.warnings);
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
        if (read_table_fixed(cfb, "TBkndCons", "Link", 0, kConsRowSize, cons_fixed,
                             cons_rows, document.warnings) &&
            cons_rows > 0) {
            const std::vector<std::uint32_t> cons_uid_by_row =
                uids_by_row(cons_fixed, 0, kConsRowSize, cons_rows, kConsRowUid);
            warn_duplicate_uids(cons_uid_by_row, "Link", false, document.warnings);
            const std::vector<bool> cons_live =
                fixed_meta_coverage(cfb, "TBkndCons", "Link", 0, kConsRowSize,
                                    kConsFixedMetaEntrySize, 0, cons_rows,
                                    cons_uid_by_row, document.warnings);
            std::size_t zero_rows = 0;
            std::size_t uncovered_rows = 0;
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
                if (!cons_live[row]) {
                    /* A non-zero row that no FixedMeta entry names is a deleted
                     * link slot.  Returning it would invent a dependency that
                     * MS Project does not schedule. */
                    ++uncovered_rows;
                    SkippedRow skipped;
                    skipped.row = row;
                    skipped.uid = cons_uid_by_row[row];
                    skipped.reason =
                        "no FixedMeta entry: MS Project does not schedule this row";
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
        if (read_table_fixed(cfb, "TBkndAssn", "Assignment", 0, kAssnRowSize,
                             assn_fixed, assn_rows, document.warnings) &&
            assn_rows > 0) {
            const std::vector<std::uint32_t> assn_uid_by_row =
                uids_by_row(assn_fixed, 0, kAssnRowSize, assn_rows, kAssnRowUid);
            warn_duplicate_uids(assn_uid_by_row, "Assignment", false, document.warnings);
            const std::vector<bool> assn_live =
                fixed_meta_coverage(cfb, "TBkndAssn", "Assignment", 0, kAssnRowSize,
                                    kAssnFixedMetaEntrySize, 0, assn_rows,
                                    assn_uid_by_row, document.warnings);
            for (std::uint32_t row = 0; row < assn_rows; ++row) {
                const std::size_t base = row * kAssnRowSize;
                if (!assn_live[row]) {
                    SkippedRow skipped;
                    skipped.row = row;
                    skipped.uid = assn_uid_by_row[row];
                    skipped.reason = "no FixedMeta entry";
                    document.skipped_assignments.push_back(std::move(skipped));
                    continue;
                }
                Assignment assignment;
                assignment.row = row;
                assignment.uid = assn_uid_by_row[row];
                assignment.task_uid = u32_at(assn_fixed, base + kAssnRowTaskUid);
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
        }
    }

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

}  // namespace mpp
