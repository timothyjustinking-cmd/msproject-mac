/*
 * reader.h -- read the Task, Resource, Link and Assignment tables of a
 * Microsoft Project .mpp file.
 *
 * SCOPE.  Reader v1: open a real MPP14 file and return one record per *live*
 * task (UniqueID, ID, Name, Duration, Start, Finish, Priority,
 * PercentComplete, OutlineLevel, OutlineNumber, Summary, Milestone, Critical,
 * ConstraintType, ConstraintDate, Deadline, the actual dates/duration, the
 * start anchor and the task-calendar uid), one per live resource (UniqueID, ID,
 * Name, Initials, MaxUnits), one per live link (UniqueID, predecessor/successor,
 * type, lag) and one per live assignment (UniqueID, task UniqueID, Units, Work,
 * Start/Finish/Stop), plus the project row's title, start and finish.  No
 * calendars, baselines or writing.
 *
 * WHERE THE BYTES COME FROM.  Container layout: `spec/mpp-container.md`.
 * Task layout: `spec/mpp-records.md` (W1), with three corrections this stream
 * established against the files and against MS Project's own output
 * (tests/data/project_a_golden.tsv).  See reader.cpp for the evidence of each:
 *
 *   1. VarMeta has a 24-byte header and 12-byte entries laid out
 *      (uid, Var2Data offset, key) -- not (key, uid, offset) after 32 bytes.
 *      W1 §5.1 was misaligned by 8 bytes; that shift pairs every key with the
 *      previous key's block.
 *   2. Task.Name is key 0x0b40000e, which is COM's PjFieldTaskName.  The key
 *      table in analysis/field_id_map.tsv therefore does decode Var2Data, and
 *      there is no separate file-side key space.
 *   3. PercentComplete is a u16 at +92, not a u32 (100 % stores 64 00 64 00).
 *
 * FROM THE FIXED TASK ROW (202 bytes inside TBkndTask/FixedData, 48-byte
 * header):
 *     +0    u32  ID (display/outline order; matches MS Project's ID column)
 *     +4    u32  UniqueID
 *     +64   u16  ConstraintType, Project's PjConstraint 0=ASAP .. 7=FNLT
 *     +66   u32  ConstraintDate (packed date; unaligned; 0xffffffff = NA)
 *     +78   u16  Priority
 *     +80   u32  ActualDuration in 6-second ticks
 *     +84   u32  Duration in 6-second ticks (4800 = one 8-hour day,
 *                10 ticks = one minute)
 *     +92   u16  PercentComplete
 *     +96   u32  calculated Start  (packed date; W33: not the effective one)
 *     +100  u32  calculated Finish (packed date)
 *     +104  u32  effective Start   (packed date; Task::start)
 *     +108  u32  effective Finish  (packed date; Task::finish)
 *     +120  u32  ActualStart (packed date; 0xffffffff until work is recorded)
 *     +124  u32  ActualFinish (packed date; 0xffffffff until 100 % complete)
 *     +182  u32  Deadline (packed date; unaligned; 0xffffffff = NA)
 *
 * SCHEDULING INPUTS (W19).  The row groups above were proved from fixture
 * diffs, not guessed:
 *
 *   ConstraintType / ConstraintDate.  corpus/probe/schedin.mpp has one task
 *   per PjConstraint value in ID order (KASAP..KFNLT): +64 reads
 *   0/1/2/3/4/5/6/7 exactly and +66 reads 0x3d1812c0 (2026-10-26 08:00) on
 *   KMSO..KFNLT and 0xffffffff on KASAP/KALAP.  corpus/probe/w3-o1.mpp is the
 *   independent COM check: its T1 is type 0 / NA date and T2/T3/T4 are type 4
 *   with 2026-10-20 08:00, 2026-10-23 15:00 and 2026-10-21 08:00, exactly what
 *   docs/analysis/oracle/w3-o1.json reports.  spec/scheduling.md §3.6's
 *   "+0x84 short, date at +0x78" are the in-memory TASKNODE offsets, not the
 *   file row; the serialized row puts them at +64/+66.
 *
 *   Deadline.  +182 (unaligned), NOT a VarMeta field: no corpus file stores
 *   field id 0x0b4001b5 in any VarMeta stream.  schedin.mpp's DL row stores
 *   0x3d1327d8 = 2026-10-21 17:00, the COM deadline; w3-o4-last.mpp's T2
 *   stores 0x3d1227d8 = 2026-10-20 17:00, also the COM deadline
 *   (docs/analysis/oracle/w3-o4.json, critical_slack_limit runs).  Every row
 *   without a deadline stores 0xffffffff there.
 *
 *   Start anchor (+174, u32 packed date; 0xffffffff = none).  This is the bound
 *   a task's Start is aligned *from*, not the scheduled Start (+104).  Measured
 *   on corpus/project_a.mpp: uid 10759 stores 2026-08-10 17:00 at
 *   +174, which is its predecessor's finish, while its own Start is
 *   2026-08-11 08:00; uid 10742 stores its own 17:00 (its Start == Finish).
 *   corpus/probe/link1.mpp's T2/T3 store T1's 17:00 finish at +174 and start
 *   the next morning; corpus/probe/holiday1.mpp's T2 stores 2026-10-20 17:00
 *   (T1's finish) and starts 2026-10-21 08:00.  A manually scheduled task's
 *   +174 is its manual Start: corpus/probe/manual-start-edited.mpp's uid 1 was
 *   given 2026-01-12 08:00 over COM and stores it at +174 while the stale
 *   calculated +96 still says 2026-01-05 08:00 (+104 carries the new value);
 *   base.mpp's three manual tasks store the project's date-only start
 *   (2026-01-04 13:00 UTC) there.  Auto tasks with no such bound store
 *   0xffffffff (w3-o1, schedin, lagff1, assign1, ...).  The reader exposes it
 *   as Task::start_anchor and deliberately does NOT use it for Task::start:
 *   Project's Start is the effective +104.
 *
 *   Task calendar (+178, i32; -1 = none).  corpus/probe/holiday1.mpp is the
 *   proof: all three live tasks store 3, and docs/analysis/oracle/
 *   holiday-probe.json records SiteCal as the task calendar with uid 3 (the
 *   project default is Standard).  Every PROJECT_A and PROJECT_B task row stores -1, so
 *   their tasks correctly fall back to the project calendar.  Exposed as
 *   Task::calendar_uid; core/cal can consume it.
 *
 *   Actuals.  +80 ActualDuration (ticks) is the W1 var-pct diff (0 -> 12000
 *   when task 1 became 50 % of 5 d); schedin's P50 is a 2-day task at 50 % and
 *   stores 4800.  +120 ActualStart and +124 ActualFinish are the two dates at
 *   the head of the actual/stop/resume block W1 documented at +120..+139.
 *   Every Project-written row with progress has +120, and +124 is set on all
 *   38 rows at 100 % (PROJECT_A and its recalc copy) but on none of the 346 rows
 *   below 100 %; +124 equals the stored Finish on those 38.  The only rows
 *   with progress and no +120 are four writer-patched fixtures that set
 *   PercentComplete alone, which is what a percent-only patch does.  schedin's
 *   P50 (50 %) stores its 2026-11-02 08:00 start at +120 and NA at +124.  The
 *   *identity* of +120/+124 as ActualStart/ActualFinish is an inference from
 *   that pattern: no oracle JSON exports them (`schedin-probe.json` reports
 *   ActualStart null even for P50, which is the one unresolved discrepancy).
 *   They are decoded anyway because the block is documented and the pattern
 *   matches, but treat the values as unproven against COM.
 *
 * STATUS FIELDS (W18; the slack fields corrected by W30).  Three stored slack
 * words and three values derived because no fixture moves a stored copy:
 *
 *     +74   i32  FreeSlack in 6-second ticks (4800 = one working day, 10 = one
 *                minute).
 *     +186  i32  StartSlack, same units.
 *     +190  i32  TotalSlack, same units.  W18 read +74 as TotalSlack; W30
 *                separated the three.  corpus/probe/milestone1.mpp's ID6 is the
 *                discriminator: +74 = 0 (COM FreeSlack 0) while +186 = +190 =
 *                220800 ticks (COM StartSlack = TotalSlack = 22080 minutes).
 *                outline1.mpp's summary stores +186 = 19200 (COM StartSlack 1920
 *                minutes) with +74 = +190 = 0 (FreeSlack = TotalSlack = 0).
 *                w3-o1's four tasks have all three equal (24000/0/3600/19200
 *                ticks against COM's 2400/0/360/1920 minutes), which is why they
 *                could not separate the fields.  +190 is also where schedin's
 *                ALAP task stores 52800 ticks = the 5280 minutes the engine used
 *                to report as a divergence when it read +74.
 *   Critical   is NOT stored as a flag: no byte of the 202-byte row, and no bit
 *              in Fixed2Data, is set only on w3-o1's critical task T2.  It is
 *              derived as "not manually scheduled && total_slack (+190) <= 0",
 *              which is
 *              MS Project's rule ("critical if slack is less than or equal to 0
 *              days", docs/handoff/coordinator.md) with the manual-task
 *              exception: base.mpp's Task A has slack 0 and COM reports
 *              Critical=false, because it is manually scheduled.  The rule
 *              reproduces COM on all 10 task rows of base, w3-o1 and outline1.
 *              "Manually scheduled" is MEASURED: Fixed2Meta's per-row flags
 *              byte (+8, bit 0x80).  corpus/probe/w37-snet-auto.mpp (base.mpp
 *              with that bit cleared on row 1 only, plus a constraint) opens in
 *              Project as Manual=False for uid 1 while w37-snet-save.mpp (bit
 *              set) opens Manual=True; see reader.cpp.
 *   Milestone  IS stored: bit 0x02 of byte +10 of the Task FixedMeta entry (the
 *              16-byte per-row blob at +8).  Duration == 0 is only the
 *              fallback, because Project lets a milestone keep a duration:
 *              corpus/probe/project_b-export.xml (Project's MSPDI export of
 *              PROJECT_B) says Milestone=1 for uid 10735 (10 days) and uid 10799
 *              (1 day), and the file sets the bit on exactly those two rows in
 *              addition to its 8 zero-day milestones (10 total, 0 false
 *              positives on PROJECT_B's 63 other live rows).  The bit reproduces
 *              COM on milestone1.mpp: its 5 milestones are exactly its 5 rows
 *              with the bit, and docs/analysis/oracle/milestone-probe.json's
 *              Milestone flags agree row for row (uids 2,3,4,5,7) while its
 *              two non-milestone rows are clear; w3-o1's T4 has the bit and is
 *              COM's only milestone there.  A row with no FixedMeta entry
 *              falls back to duration == 0 (Task::milestone_stored says which
 *              path was taken).
 *
 * OUTLINE (Fixed2Data).  OutlineLevel is not stored as a number anywhere; the
 * *hierarchy* is.  `114/TBkndTask/Fixed2Data` holds one 64-byte record per row
 * (spec/mpp-records.md §7), and Fixed2Meta's entry i describes the same row as
 * FixedMeta's entry i, which is what pairs a record with its row:
 *
 *     +0   16 B  row identity.  NOT the UniqueID: it is a GUID-like value that
 *                changes when Project rewrites the row.
 *     +16  f64   sequence number 1.0, 2.0, 3.0, ... in freshly saved files.
 *                spec §7 calls this a "level" double; it is not one (PROJECT_A's 85
 *                rows hold 1.0, 84.0, 46.39, ... and outline1's level-2 rows
 *                hold 3.0 and 4.0).
 *     +24  16 B  identity of the *parent* row; all zero on the project row.
 *     +42  u8    flags.  Bit 0x40 is set exactly on the rows that have children
 *                (outline1's summary and the project row; PROJECT_A 17/17 and PROJECT_B
 *                13/13 rows, checked against the parent links below).
 *
 * Evidence that +24 is the parent and not the previous row (spec §7's reading):
 * base.mpp is flat, so "every task's +24 is the project row's id" fits both.
 * outline1.mpp does not: there C1's +24 is S1's id and C2's is *also* S1's id,
 * not C1's, which a previous-row chain would have produced.  COM reports
 * OutlineLevel 1/2/2 for S1/C1/C2 and the depths of the stored chain are 1/2/2.
 * Every other fixture's chain is flat (base 1/1/1, w3-o1 1/1/1/1) and so is its
 * outline, but COM's OutlineLevel itself was only exported for outline1: one
 * oracle pass over PROJECT_A's 83 rows would turn the derivation into a measured
 * match.  So OutlineLevel is derived as the depth of the stored parent chain
 * (the project row is 0, its children 1) and OutlineNumber as the sibling-index
 * path "1", "1.1", "1.2", ... over live rows in row order, which is COM's
 * Task.OutlineNumber.  Both are marked derived here rather than measured,
 * because no fixture changes a level byte: the evidence is that the chain
 * reproduces COM's numbers on the fixture that has a hierarchy.
 *
 * Summary is derived as child_count > 0.  The stored 0x40 flag agrees with that
 * on every row of every file we have; a disagreement is reported as damage, not
 * silently resolved.
 *
 * RESOURCES (TBkndRsc).  FixedData = 48-byte header + 172 bytes per row; row 0
 * is a uid-0 placeholder, rows 1.. are the resources in table order.  The row
 * offsets proved from the corpus are:
 *     +0    u32     ID (equal to UniqueID in every corpus file; Project hands
 *                   resource uids out sequentially, so the two cannot be told
 *                   apart from these bytes)
 *     +4    u32     UniqueID
 *     +8    double  MaxUnits, x100: 10000.0 is 100 % and 5000.0 is 50 %.
 *                   MEASURED (wave 11) on corpus/probe/w9-resourcetypes.mpp:
 *                   COM set R1 to MaxUnits 50 % and the row holds 5000.0, so the
 *                   scale is x100, not x10000.  PROJECT_A/PROJECT_B/assign1 are all
 *                   100 %; the same fixture carries the Material/Cost resources.
 *     +80   u32     AccrueAt (3 = prorated, the Project default; not exposed)
 *     +154  u16     per-row counter (unmapped; not exposed)
 * Name = VarMeta key 0x0c400001 (UTF-16LE, raw and display forms), Initials =
 * 0x0c400002, MaterialLabel = 0x0c40012b.
 *
 * Resource Type is NOT a byte in the 172-byte row.  It is a bit in the per-row
 * metadata, measured on corpus/probe/w9-resourcetypes.mpp (COM-built Work 50 %,
 * Material with a label, Cost):
 *     Work     <=> the Resource FixedMeta entry (37-byte rows) byte +12 bit 0x10
 *                 is set.  w9-resourcetypes R1 has it set; R2/R3 clear it.
 *     Cost     <=> that bit is clear AND the Resource Fixed2Meta entry byte +8
 *                 bit 0x10 is set.
 *     Material <=> both bits are clear.
 * w9-resourcetypes decodes as Work / Material / Cost, and every one of PROJECT_A's
 * and PROJECT_B's 22 resources decodes as Work, matching their MSPDI exports.
 * (MSPDI's <Type> uses 1 for Work and 0 for Material and marks a cost resource
 * with IsCostResource; the COM/PjResourceType values this struct exposes are
 * Work=0, Material=1, Cost=2.  They are different enums.)  The layout is
 * mpxj's MPP14ResourceReader rule for Project 2013+; the Project-2010 MPP14
 * variant puts the Work bit at entry +9 bit 0x02 and is not on disk, so it is
 * unimplemented.  `type_stored` is true when the file metadata supplied the
 * value.
 *
 * LINKS (TBkndCons, Project's "relationship" table).  FixedData = 20 bytes per
 * row, NO header; FixedMeta = 16 + 10 bytes per entry.  There is no VarMeta
 * content (24-byte header, zero entries) and Var2Data is empty: a link is
 * entirely fixed.  Row:
 *     +0   u32  link UniqueID
 *     +4   u32  predecessor task UniqueID
 *     +8   u32  successor task UniqueID
 *     +12  u16  type: 0 = FF, 1 = FS, 2 = SF, 3 = SS (Project's enum).  PROJECT_B
 *               stores 1 (FS) in all 72 links and its links are sequential
 *               finish-to-start chains; PROJECT_A stores one 3.  The 0/2/3 names are
 *               inference from the documented enum.
 *     +14  i32  lag in 6-second ticks (4800 = one 8-hour day, negative = lead).
 *               PROJECT_A's live rows hold 96000 = 20 d, 139200 = 29 d and
 *               240000 = 50 d; fixture link1.mpp stores 4800 and -4800 in the
 *               same field.
 *     +18  u16  LagFormat (PjFormatUnit): 7 = working days, 8 = elapsed days.
 *               MEASURED (wave 9b, corpus/probe/w9-elapsedlag.mpp): the two
 *               "2 edays"/"3 edays" links store 8 while the matching "2 days"
 *               links store 7.  Link::lag_is_elapsed() decodes it.
 * LIVENESS: a row is a link only when a FixedMeta entry names it (the entry's +4
 * offset).  PROJECT_A has 99 rows, 91 entries and 88 distinct covered rows; the 11
 * rows without an entry (including the all-zero 85/86) go to
 * Document::skipped_links.  Reading every non-zero row instead returned 97 links
 * and invented links MS Project does not schedule -- see reader.cpp's LIVENESS
 * note.
 *
 * LIVENESS HAS TWO LEVELS, and they are not the same question:
 *   (a) row liveness  -- a FixedMeta entry names the row (above);
 *   (b) endpoint liveness -- both the predecessor and successor uids name a
 *       *live task*, which Link::predecessor_known / successor_known report.
 * PROJECT_B is the measured case where they differ: 72 rows are live by (a) but
 * Project exports only 70 <PredecessorLink> elements, because the first two
 * links (10722 -> 10723 and 10723 -> 10724) name uids that only exist in
 * PROJECT_B's 12 deleted task slots (rows 6..8).  PROJECT_B's links are therefore
 * 72 returned / 70 with both endpoints live / 2 dead-endpoint.  Project does
 * not schedule a dead-endpoint link; the reader keeps it (so a caller can see
 * the stored bytes) and Link::endpoints_live() is false for it.  PROJECT_A and
 * link1 are 88/88 and 5/5: no dead endpoints.  Document::
 * link_count_with_live_endpoints() / link_count_with_dead_endpoints() give the
 * split, and reader_dump prints it.
 *
 * ASSIGNMENTS (TBkndAssn).  FixedData = 110 bytes per assignment, NO header;
 * FixedMeta = 16 + 34 bytes per assignment.  Row:
 *     +0   u32     assignment UniqueID (equals the VarMeta entry's uid)
 *     +4   u32     task UniqueID
 *     +8   u32     resource UniqueID.  Proved by the coordinator's
 *                  corpus/probe/assign1.mpp: its three real rows hold 1, 2, 1
 *                  for the documented T1/R1, T1/R2, T2/R1 assignments.
 *                  base/probe-v1/PROJECT_A/PROJECT_B store 0xFFFF0001 there on their
 *                  unassigned placeholder rows; that is -65535, mpxj's
 *                  ASSIGNMENT_NULL_RESOURCE_ID, so Assignment::has_resource()
 *                  is false for them and the raw word is preserved.
 *     +12  double  Units, x100: 10000.0 = 100 %.
 *     +20  double  Work, minutes x1000: 2,400,000 = 2,400 min = 5 days.
 *                  Measured against base.mpp's 5/3/2-day tasks and against
 *                  probe-v2 (task 1 5 d -> 7 d scales exactly x1.4).
 *     +52  u32     assignment Start (packed date).  Measured: equals the
 *                  task's effective +104 on all 67 live PROJECT_A assignments, all 63
 *                  of PROJECT_B's assignments whose task is a live task (10 of
 *                  PROJECT_B's 73 assignments name deleted task uids), 4/4 link1
 *                  and the 3 real assign1 rows.  base.mpp's three unassigned
 *                  rows store the project row's date-only start.
 *     +56  u32     assignment Finish (packed date).  Same counts against the
 *                  task's +108 (67/67 PROJECT_A, 63/63 resolved PROJECT_B, 4/4, 3/3).
 *     +60  u32     the assignment's **Resume** date (packed date).  MEASURED
 *                  (W40) against Project's own MSPDI export of PROJECT_A: the export
 *                  carries 21 non-empty `<Resume>` elements and +60 equals one
 *                  of them on 21/21 rows.  It is NOT Stop: the two split rows
 *                  (uids 9821 and 9823) have Stop = the previous working
 *                  instant (2026-05-08 17:00 / 2026-04-14 17:00) while +60 =
 *                  Resume (2026-05-11 08:00 / 2026-04-15 08:00).  On the other
 *                  19 exported rows Stop and Resume coincide, which is why the
 *                  old "Stop" reading looked right.  Where the export has
 *                  neither element (46 rows), +60 equals Start on the 0 % rows
 *                  and Finish on the 100 % rows: Project's default resume.
 *                  Stop is NOT stored in the assignment row; it is the last
 *                  working instant before Resume and needs the calendar to
 *                  derive, so the reader does not invent it.
 *     +92  u32     7 (unmapped; same value as the in-memory assignment record's
 *                  +116 i16).
 *
 * Row 0 of the Task table is the project row and is returned as
 * Document::project, never as a task.  A task row with no VarMeta group has no
 * Name entry and is a deleted slot: PROJECT_A's row 67 (uid 10856) and PROJECT_B's rows
 * 1..12 are, and MS Project does not report them.  They go to
 * Document::skipped_rows, not to tasks.  Resources are rows 1.. (row 0 is a
 * uid-0 placeholder); links need a FixedMeta entry that names the row (PROJECT_A has
 * 88 covered rows out of 99); assignments need a FixedMeta entry and a non-zero
 * task uid (assign1.mpp stores two covered placeholder rows with task uid 0).
 * See reader.cpp.
 *
 * TOLERANCE.  Real files carry damage: PROJECT_A's VarMeta declares 1,076 entries but
 * holds 1,081, repeats 5 (uid, key) pairs, has 101 entries whose offset
 * (0xffffffff) is "not stored", and one dead task row; its link FixedMeta says
 * 88 entries but holds 91 (88 of 99 rows covered), and its assignment FixedMeta
 * says 67 but holds 68 for 69 rows.  None of that is fatal; everything worked
 * around is reported in Document::warnings with offsets.
 */

#ifndef MPP_READER_H
#define MPP_READER_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfb.h"         /* mpp::Error */
#include "field_value.h" /* mpp::CustomFieldValue, the measured custom-field ids */

namespace mpp {

/* One 8-hour working day, in 6-second ticks (W1, spec/mpp-records.md §4). */
constexpr std::uint32_t kTicksPerDay = 4800;
/* Ten ticks make one minute: MS Project's internal duration unit is 0.1 min. */
constexpr std::uint32_t kTicksPerMinute = 10;
/* One tick is 6 seconds: value = (day << 16) | (seconds_of_day / 6). */
constexpr std::uint32_t kTickSeconds = 6;
/* The packed-date word Project stores for "no date": 0xffffffff.  (0 is also
 * treated as not-a-date by PackedDate::valid().)  It is the initial value of
 * every optional PackedDate member, e.g. Task::start_anchor. */
constexpr std::uint32_t kDateNa = 0xFFFFFFFFu;

/*
 * PjFormatUnit's elapsed rule, spec/scheduling.md §0.4 [B]: 3/5/7/9/11 are
 * minutes/hours/days/weeks/months, the even value one higher is the elapsed
 * form, and +32 marks the estimated variants.  Used by Task::duration_format
 * (task row +0xa4) and Link::lag_units (link row +18).  MEASURED for
 * 7/8 (wave 9b/11): w9-elapsedlag.mpp stores 8 for an elapsed lag and for the
 * "2 edays" duration, 7 for the working-day controls.
 */
inline constexpr bool format_unit_is_elapsed(std::uint16_t format) noexcept {
    const std::uint16_t unit =
        format >= 32 ? static_cast<std::uint16_t>(format - 32) : format;
    return unit >= 4 && unit <= 12 && (unit % 2u) == 0;
}

/*
 * A date/time as stored in the fixed row: one u32, `(day << 16) | secs/6`,
 * where day 0 is 1983-12-31.  A value of 0 or 0xffffffff is not a date.
 */
struct PackedDate {
    std::uint32_t raw = 0;

    bool valid() const { return raw != 0 && raw != 0xFFFFFFFFu; }
    int day() const { return static_cast<int>(raw >> 16); }
    int seconds_of_day() const { return static_cast<int>((raw & 0xFFFFu) * kTickSeconds); }
    std::uint32_t ticks_of_day() const { return (raw & 0xFFFFu); }
};

struct Task {
    std::uint32_t row = 0;     /* FixedData row index, 0-based, project row excluded */
    std::uint32_t row_id = 0;  /* u32 at +0: MS Project's ID column */
    std::uint32_t uid = 0;     /* u32 at +4: UniqueID */
    /* name is the display form: ASCII spaces trimmed, which is what MS Project's
     * COM surface reports (PROJECT_A row 1 is stored as "Site establishment ").
     * name_raw is the decoded block verbatim, for a writer's round trip. */
    std::string name;
    std::string name_raw;
    std::uint32_t duration_ticks = 0;   /* +84 */
    /* +164 (0xa4): DurationFormat in PjFormatUnit numbering.  7 = days, 8 =
     * elapsed days, 9 = weeks, 11 = months, 21 = no format; 35..44 are the
     * estimated variants and 4/6/8/10/12 the elapsed ones.  MEASURED: every
     * task's +a4 equals its <DurationFormat> in Project's MSPDI export (84 PROJECT_A
     * + 74 PROJECT_B + 4 w9-resourcetypes rows, 0 mismatches), and
     * w9-elapsedlag.mpp's "2 edays" control (uid 8) stores 8 while that file's
     * working-day tasks store 7. */
    std::uint16_t duration_format = 0;
    std::uint16_t priority = 0;         /* +78 */
    std::uint16_t percent_complete = 0; /* +92 (u16; see note 3 above) */
    /* start/finish are the EFFECTIVE pair (+104/+108, W33): what Project's own
     * UI and MSPDI export report.  calculated_start/calculated_finish are the
     * calculated pair (+96/+100), which goes stale after a manual Start edit or
     * in a file that is not fully calculated (PROJECT_B uid 10805 is the one
     * corpus row where the pairs differ).  Both are packed dates. */
    PackedDate start;                   /* +104 (effective) */
    PackedDate finish;                  /* +108 (effective) */
    PackedDate calculated_start;        /* +96 (calculated copy) */
    PackedDate calculated_finish;       /* +100 (calculated copy) */

    /* ---- Scheduling inputs for core::plan and the CPM engine (wave 6).
     * Decoded by W19 from the fixed row; see "SCHEDULING INPUTS" in the file
     * comment for the byte evidence.  0xffffffff means "not set"
     * (PackedDate::valid() is false for both 0 and 0xffffffff). */
    std::uint16_t constraint_type = 0;         /* +64: PjConstraint 0=ASAP .. 7=FNLT */
    PackedDate constraint_date{0xFFFFFFFFu};   /* +66: only for the types that carry one */
    PackedDate deadline{0xFFFFFFFFu};          /* +182, not VarMeta 0x0b4001b5 */
    /* +174: the start anchor.  NOT the scheduled Start (+96).  For a manually
     * scheduled task it is the manual Start; for an auto task it is the bound
     * the start was aligned from (usually a predecessor's finish, hence 17:00
     * the previous workday); kDateNa when the file stores none.  See "Start
     * anchor" in the file comment for the PROJECT_A/link1/holiday1/manual fixture
     * evidence.  Deliberately not used for start. */
    PackedDate start_anchor{kDateNa};
    /* +178: task calendar UniqueID (i32).  -1 means "no task calendar", i.e.
     * the task uses the project calendar.  holiday1.mpp's three tasks store 3
     * (SiteCal); every PROJECT_A/PROJECT_B row stores -1. */
    std::int32_t calendar_uid = -1;
    /* +120/+124.  Unproven against COM (no oracle export); see the file
     * comment.  A 50 %-complete task has an actual start and no actual
     * finish. */
    PackedDate actual_start{0xFFFFFFFFu};
    PackedDate actual_finish{0xFFFFFFFFu};
    std::uint32_t actual_duration_ticks = 0;   /* +80 */

    /* ---- Outline.  Derived from the stored parent links in Fixed2Data; see
     * "OUTLINE" in the file comment.  outline_level is MS Project's
     * Task.OutlineLevel (1 for a top-level task, 0 only for the project row),
     * outline_number its "1"/"1.1"/"1.2" sibling path.  Both stay at the flat
     * default when a file has no usable Fixed2Data. */
    std::uint32_t outline_level = 1;
    std::string outline_number;
    /* Rows whose stored parent is this task.  summary == child_count > 0. */
    std::uint32_t child_count = 0;
    bool summary = false;

    /* ---- Status.  Three stored slack words, in 6-second ticks (4800 = one
     * working day); negative is possible in Project (an SNLT/FNLT clamp can give
     * negative slack) but no corpus file has one.  W30 corrected the mapping:
     * +74 is FreeSlack, +186 StartSlack, +190 TotalSlack (W18 had called +74
     * TotalSlack).  milestone1.mpp's ID6 separates them: +74 = 0 with +186 =
     * +190 = 220800; outline1's summary has +74 = +190 = 0 with +186 = 19200.
     * manually_scheduled is MEASURED from bit 0x80 of Fixed2Meta's per-row
     * flags byte (+8): corpus/probe/w37-snet-auto.mpp clears it on row 1 and
     * Project reports uid 1 Manual=False, while w37-snet-save.mpp (bit set)
     * reports Manual=True with the same constraint.  See reader.cpp.  critical
     * is derived as "not manually scheduled and
     * total_slack_ticks (+190) <= 0": a manually scheduled task is never reported
     * critical, which is why base.mpp's Task A (slack 0) is Critical=false over
     * COM while w3-o1's T2 (slack 0) is Critical=true. */
    std::int32_t free_slack_ticks = 0;  /* +74 */
    std::int32_t start_slack_ticks = 0; /* +186 */
    std::int32_t total_slack_ticks = 0; /* +190 */
    bool manually_scheduled = false;
    bool critical = false;
    /* Milestone, stored: bit 0x02 at byte +10 of the Task FixedMeta entry.
     * Project lets a milestone keep a duration (PROJECT_B uids 10735 10 d and
     * 10799 1 d are Milestone=1 in its MSPDI export), so duration == 0 is only
     * the fallback.  `milestone` is the effective value; `milestone_stored` is
     * true when the file carried the flag for this row and false when the
     * duration fallback was used.  See "Milestone" in the file comment. */
    bool milestone = false;
    bool milestone_stored = false;

    /* ---- Custom fields (W48).  The values the file stores, decoded with the
     * measured ids/encodings in `field_value.h`: Text (UTF-16LE+NUL in
     * VarMeta), Number (f64), Date (packed u32), Duration (u32 file ticks,
     * COM minutes * 10) and Flag1 (bit 0x02 of the Task FixedMeta entry's +24
     * byte -- a flag is NOT a VarMeta block).  Only stored fields are listed;
     * `present` is always true here.  Cost and Flag2..20 are listed only when a
     * block exists and carry `storage_measured == false`, because their storage
     * is not measured (w10-oracle §2) and must not be decoded as if it were.
     * Lookups: `custom_field(id)` returns nullptr when the file does not store
     * that field for this row.  See field_value.h for the evidence. */
    std::vector<CustomFieldValue> custom_fields;

    CustomFieldValue const* custom_field(std::uint32_t field_id) const {
        for (CustomFieldValue const& field : custom_fields) {
            if (field.field_id == field_id) {
                return &field;
            }
        }
        return nullptr;
    }
    bool has_custom_field(std::uint32_t field_id) const {
        return custom_field(field_id) != nullptr;
    }

    bool duration_is_elapsed() const { return format_unit_is_elapsed(duration_format); }
    double duration_days() const {
        return static_cast<double>(duration_ticks) / static_cast<double>(kTicksPerDay);
    }
    std::uint32_t duration_minutes() const { return duration_ticks / kTicksPerMinute; }
    /* The effective milestone value: the stored FixedMeta bit when the file has
     * one for this row, otherwise the duration == 0 fallback. */
    bool is_milestone() const { return milestone; }
    double total_slack_days() const {
        return static_cast<double>(total_slack_ticks) / static_cast<double>(kTicksPerDay);
    }
    std::int32_t total_slack_minutes() const { return total_slack_ticks / 10; }
    double free_slack_days() const {
        return static_cast<double>(free_slack_ticks) / static_cast<double>(kTicksPerDay);
    }
    std::int32_t free_slack_minutes() const { return free_slack_ticks / 10; }
    double start_slack_days() const {
        return static_cast<double>(start_slack_ticks) / static_cast<double>(kTicksPerDay);
    }
    std::int32_t start_slack_minutes() const { return start_slack_ticks / 10; }
};

/*
 * Resource type, MS Project's COM PjResourceType order (Work=0, Material=1,
 * Cost=2).  This is NOT MSPDI's <Type> numbering (that XML writes Work as 1 and
 * Material as 0, and marks a cost resource with IsCostResource).  MEASURED on
 * corpus/probe/w9-resourcetypes.mpp; see "Resource Type" in the file comment.
 */
enum class ResourceType : std::uint8_t { work = 0, material = 1, cost = 2 };

struct Resource {
    std::uint32_t row = 0;     /* FixedData row index; row 0 is the placeholder */
    std::uint32_t row_id = 0;  /* u32 at +0 (== uid in every corpus file) */
    std::uint32_t uid = 0;     /* u32 at +4 */
    std::string name;          /* VarMeta 0x0c400001, display form (trimmed) */
    std::string name_raw;      /* the same block verbatim */
    std::string initials;      /* VarMeta 0x0c400002 */
    /* VarMeta 0x0c40012b; display form trimmed like name, raw form verbatim.
     * Only a Material resource carries one (w9-resourcetypes R2 = "metre"). */
    std::string material_label;
    std::string material_label_raw;
    /* MaxUnits as stored: double at +8, x100 (10000.0 = 100 %, 5000.0 = 50 %;
     * the 50 % row is the wave-11 measurement of the scale). */
    double max_units_x100 = 0.0;
    /* MEASURED on w9-resourcetypes (Work / Material / Cost); see "Resource
     * Type" in the file comment.  `type_stored` is false only when the file's
     * per-row metadata did not supply the value. */
    ResourceType type = ResourceType::work;
    bool type_stored = false;

    double max_units() const { return max_units_x100 / 100.0; }
    /* The MSPDI spelling: a fraction, 1.0 == 100 %.  Project's exports write
     * `<MaxUnits>1.00</MaxUnits>` for every resource we have, and that is the
     * same quantity as max_units()/100. */
    double max_units_fraction() const { return max_units_x100 / 10000.0; }
    std::uint32_t max_units_percent() const {
        return static_cast<std::uint32_t>(max_units_x100 / 100.0 + 0.5);
    }
    bool is_work() const { return type == ResourceType::work; }
    bool is_material() const { return type == ResourceType::material; }
    bool is_cost() const { return type == ResourceType::cost; }
};

/* Project's relation type values (PjRelationType order). */
enum class LinkType : std::uint16_t {
    finish_finish = 0, /* FF */
    finish_start = 1,  /* FS */
    start_finish = 2,  /* SF */
    start_start = 3,   /* SS */
};

struct Link {
    std::uint32_t row = 0;
    std::uint32_t uid = 0;              /* +0 */
    std::uint32_t predecessor_uid = 0;  /* +4: task UniqueID */
    std::uint32_t successor_uid = 0;    /* +8: task UniqueID */
    std::uint16_t type_raw = 0;         /* +12 */
    /* +14: signed 6-second ticks, 4800 = one working day; negative = lead.
     * fixture link1.mpp stores both 4800 and -4800 here. */
    std::int32_t lag_ticks = 0;
    /* +18: PjFormatUnit.  7 = working days, 8 = elapsed days (MEASURED on
     * w9-elapsedlag.mpp's two single-variable pairs); 4/6/8/10/12 are elapsed. */
    std::uint16_t lag_units = 0;
    /* Set after the task table is known: the endpoint names one of the live
     * tasks this reader returns (PROJECT_A has links to its deleted task 10856,
     * PROJECT_B to its deleted slots 10722..10724). */
    bool predecessor_known = false;
    bool successor_known = false;

    LinkType type() const { return static_cast<LinkType>(type_raw & 0x3u); }
    std::string type_name() const; /* "FF", "FS", "SF", "SS" or "?" */
    /* Both endpoints name a live task.  PROJECT_B's two dead-endpoint links
     * (10722 -> 10723, 10723 -> 10724) are returned but not scheduled by
     * Project; they are the difference between the reader's 72 and the 70
     * <PredecessorLink> elements in Project's export. */
    bool endpoints_live() const { return predecessor_known && successor_known; }
    bool lag_is_elapsed() const { return format_unit_is_elapsed(lag_units); }
    double lag_days() const {
        return static_cast<double>(lag_ticks) / static_cast<double>(kTicksPerDay);
    }
};

/* 0xFFFF0001 == -65535, mpxj's ASSIGNMENT_NULL_RESOURCE_ID: "no resource". */
constexpr std::uint32_t kNullResourceUid = 0xFFFF0001u;

struct Assignment {
    std::uint32_t row = 0;
    std::uint32_t uid = 0;         /* +0; equals the VarMeta entry's uid */
    std::uint32_t task_uid = 0;    /* +4: task UniqueID */
    /* Raw u32 at +8.  kNullResourceUid in every corpus row; see reader.h. */
    std::uint32_t resource_uid_raw = 0;
    double units_x100 = 0.0;   /* +12: 10000.0 = 100 % */
    double work_x1000 = 0.0;   /* +20: minutes x1000 (2,400,000 = 5 days) */
    /* +52/+56: the assignment's own Start and Finish (packed dates).  They
     * equal the task's +104/+108 on all 67 live PROJECT_A assignments, all 63 of
     * PROJECT_B's assignments whose task is live, 4/4 link1 and 3/3 real assign1 rows;
     * base.mpp's unassigned rows store the project row's date-only start. */
    PackedDate start{kDateNa};
    PackedDate finish{kDateNa};
    /* +60: the assignment's **Resume** date (packed).  MEASURED (W40) against
     * Project's MSPDI export of PROJECT_A: non-empty `<Resume>` on 21 assignments,
     * and +60 equals it on 21/21.  It is not Stop -- on the two split rows
     * (uids 9821, 9823) Stop is the previous working instant while +60 is the
     * next working start.  Where the export has no Resume element (46 rows) the
     * stored value is Start on 0 % rows and Finish on 100 % rows.  Stop itself
     * is not stored (it is derivable from Resume + calendar, so not invented
     * here).  See "ASSIGNMENTS" in the file comment. */
    PackedDate resume{kDateNa};

    /* Both the null sentinel and 0 occur in the corpus for "no resource";
     * real resource uids start at 1. */
    bool has_resource() const {
        return resource_uid_raw != kNullResourceUid && resource_uid_raw != 0u;
    }
    std::uint32_t resource_uid() const { return has_resource() ? resource_uid_raw : 0u; }
    double units() const { return units_x100 / 100.0; }
    std::uint32_t units_percent() const {
        return static_cast<std::uint32_t>(units_x100 / 100.0 + 0.5);
    }
    std::uint32_t work_minutes() const {
        return static_cast<std::uint32_t>(work_x1000 / 1000.0 + 0.5);
    }
    std::uint32_t work_ticks() const { return work_minutes() * kTicksPerMinute; }
    double work_days() const {
        return static_cast<double>(work_ticks()) / static_cast<double>(kTicksPerDay);
    }
};

/* Row 0: the project row (spec/mpp-records.md §6).  start/finish are the row's
 * +96/+100, i.e. MS Project's project-summary row dates:
 *   - corpus/probe/w3-o1.mpp and outline1.mpp store the earliest task start and
 *     the latest task finish (2026-10-19 08:00 / 2026-10-26 17:00), which is
 *     exactly the span COM reports for the summary task;
 *   - base.mpp and var-projstart.mpp store the project's *date-only* start and
 *     finish instead: base holds 2026-01-04 13:00 and 2026-01-09 12:00, i.e.
 *     2026-01-05 00:00 in Sydney, and var-projstart's 2026-05-31 14:00 is
 *     2026-06-01 00:00, the project start the oracle set.  That is the UTC
 *     encoding of a date-only field (spec §3), so COM's Project.Start is the
 *     local reading of the same instant.
 * The reader formats the stored wall-clock value and does not convert
 * timezones; the UTC-vs-local rule is spec §3's open question, not this
 * reader's.  These are the project row's +96/+100 pair, which equals its
 * +104/+108 pair in every corpus file probed, so the calculated/effective
 * distinction does not arise for the project summary. */
struct ProjectRow {
    std::uint32_t row_id = 0;
    std::uint32_t uid = 0;
    std::string title;           /* the project row's Name, trimmed (key 0x0b40000e) */
    std::string title_raw;       /* same block, untrimmed */
    std::uint32_t duration_ticks = 0;
    std::uint16_t priority = 0;
    std::uint16_t percent_complete = 0;
    PackedDate start;
    PackedDate finish;
};

/* A row that is present in FixedData but is not a live record. */
struct SkippedRow {
    std::uint32_t row = 0;
    std::uint32_t uid = 0;
    std::string reason;
    /* When the slot still has variable blocks but no metadata, the name block
     * in the gap between its neighbours is reported here as evidence. */
    std::string orphan_name;
};

struct Document {
    ProjectRow project;
    std::vector<Task> tasks;           /* live task rows, FixedData order */
    std::vector<Resource> resources;   /* live resources, FixedData order */
    std::vector<Link> links;           /* live links, FixedData order */
    std::vector<Assignment> assignments; /* live assignments, FixedData order */
    std::vector<SkippedRow> skipped_rows;
    std::vector<SkippedRow> skipped_resources;
    std::vector<SkippedRow> skipped_links;
    std::vector<SkippedRow> skipped_assignments;
    std::vector<std::string> warnings; /* recoverable anomalies, in file order */

    /* Link liveness split: a link is counted "live" only when BOTH endpoints
     * name a live task (Link::endpoints_live()).  PROJECT_B: 72 links, 70 live,
     * 2 dead-endpoint (Project's export has 70).  PROJECT_A: 88/88.  link1: 5/5. */
    std::size_t link_count_with_live_endpoints() const;
    std::size_t link_count_with_dead_endpoints() const;

    /*
     * One task's custom-field value by PjTask field id (e.g. 0x0b400033 Text1).
     * `present` is false when the row does not store the field, and the whole
     * struct is default except field_id/kind/storage_measured, so a caller can
     * tell "not stored" from "stored as 0".  `uid` unknown is the same
     * not-present answer.  This is the W48 read side of `save_field_values`
     * (core/mppwrite/field_values.h); the encodings are field_value.h's.
     */
    CustomFieldValue custom_field(std::uint32_t uid,
                                  std::uint32_t field_id) const {
        CustomFieldValue absent;
        absent.field_id = field_id;
        absent.kind = custom_field_kind(field_id);
        absent.storage_measured = custom_field_storage_measured(
            absent.kind, custom_field_index(field_id));
        for (Task const& task : tasks) {
            if (task.uid != uid) {
                continue;
            }
            if (CustomFieldValue const* const found = task.custom_field(field_id)) {
                return *found;
            }
            return absent;
        }
        return absent;
    }
};

/* Both throw mpp::Error on anything fatal; non-fatal anomalies are warnings. */
Document load_file(std::string const& path);
Document load_bytes(std::vector<std::uint8_t> bytes);

/*
 * "YYYY-MM-DD HH:MM", with ":SS" appended when the stored time is not a whole
 * minute; "" for an invalid packed date.  The value is the wall-clock time as
 * stored -- no timezone conversion is applied (spec/mpp-records.md §3 records
 * the open UTC-vs-local question; MS Project's own export prints the same
 * wall-clock values with a +00:00 suffix).
 */
std::string format_date(const PackedDate& date);

/* Same, with seconds always present and a "+00:00" suffix, matching the ISO
 * form MS Project reports in tests/data/project_a_golden.tsv. */
std::string format_date_iso(const PackedDate& date);

/*
 * Working days: "204" when the tick count is a whole number of 8-hour days,
 * otherwise up to four decimals ("1.5").  A reader must divide by 4800; it
 * must not report minutes.
 */
std::string format_duration_days(std::uint32_t ticks);

/* Duration in minutes, the unit MS Project's COM surface uses. */
std::uint32_t duration_minutes(std::uint32_t ticks);

}  // namespace mpp

#endif /* MPP_READER_H */
