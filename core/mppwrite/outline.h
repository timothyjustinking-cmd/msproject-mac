/*
 * outline.h -- Project's stored task outline for an Indent / Outdent edit
 * (wave 28, W28-2.1; wave 29, W29-0.1 added TBkndAssn and 114/Props).
 *
 * MEASURED SOURCE.  tests/data/wave28-outline-{indent,summary,outdent}.mpp are
 * three Project-2021 save pairs (a control file and the file Project saved
 * after ONE outline edit, all made from the same neutral synthetic project):
 *
 *   indent   Task 1 (leaf) becomes a summary of the indented Task 2.
 *   summary  Task 1 (leaf) becomes a summary of the indented Task 2, which is
 *            itself the summary of Task 3.
 *   outdent  Task 2 is outdented from its parent Task 1; Task 1 becomes a leaf
 *            again.
 *
 * The byte diff and per-row dumps are build/w28/recheck-{indent,summary,
 * outdent}.log and build/w28/outline-rows.log (not committed).  The stream
 * facts below are read from those pairs, not inferred from the reader.
 *
 * WHAT ONE INDENT WRITES (moved row U under its preceding sibling P):
 *   FixedData
 *     P (the new summary): +84 Duration       := the caller's working-time
 *                            span of the new children (see below);
 *                          +96 Start          := the children's earliest +104;
 *                          +100 Finish        := the children's latest +108;
 *                          +140 (u16)         := 1 (Project's has-children
 *                                                byte; its neighbour +141 is 0);
 *                          +164 DurationFormat:= 0x15 (elapsed days);
 *                          +174 StartAnchor   := NA;
 *                          +88 RemainingDuration is NOT rolled up (measured:
 *                            Task 1 keeps 0x5dc0 while +84 becomes 0x2ee0).
 *     U and every descendant: +172 OutlineLevel += 1.
 *     U: +142 (u16) := P's FixedData +0 row id.  MEASURED and easy to get
 *        wrong: the byte is the PARENT row id, not the level and not a
 *        level-1 delta.  The summary pair's descendant Task 3 sits at level 3
 *        in the edited file and keeps +142 = 2 (its parent's row id); the
 *        indent pair's moved Task 2 has +142 = 1.
 *   Fixed2Data
 *     P: record flags +42  := 0x40 (summary), and the start-copy bytes +46..49
 *        := FF FF FF FF.
 *     U: parent identity +24..39 := P's identity (+0..15, copied verbatim);
 *        flags +42 := 0x40 for a moved summary, 0xA0 for a moved child leaf.
 *        A leaf's start copy +46..49 is unchanged.
 *   Fixed2Meta (entry index paired through FixedMeta)
 *     P: +88 := 0x00, +95 := 0x08 (summary).  U's entry is unchanged.
 *   FixedMeta
 *     P: +2 variable-field count moves by the NET VarMeta entry change
 *        (measured 11 -> 10: three leaf fields removed, two summary fields
 *        added); +26 bit 0x80 is SET (the stored summary flag -- every summary
 *        row of the controls carries it; the project row stores 0).  The
 *        entry's 16-byte cache blob at +8..23 moves five bytes, measured on
 *        both indent pairs: +9 |= 0x40, +12 |= 0x0C, +16 |= 0x10,
 *        +17 := (+17 & ~0x04) | 0x01, +18 |= 0x20 (0xbf->0xff, 0x71->0x7d,
 *        0x07->0x17, 0x4c->0x49, 0xcf->0xef).  set_fixed_meta_blob writes
 *        them; the save-pair test compares the whole entry (blob included)
 *        with Project's file -- wave 28 masked it and compared the control.
 *   VarMeta / Var2Data (logical entries; physical tail preserved)
 *     P loses 0x0B4004E5 and, when present, 0x0B400505/0x0B400506, and gains
 *       0x0B40007C = { original Duration (+84), original DurationFormat (+164),
 *                     0 }                                 12 bytes, and
 *       0x0B4004E4 = { 1, U's row id }                     8 bytes.
 *       New blocks append at the end of Var2Data in (uid, key) order; entries
 *       are inserted inside P's uid group in key order.
 *     The PROJECT row's 0x0B4004E4 child list loses U's row id.  Its payload
 *       shrinks, so Project rewrites it IN PLACE (length prefix updated, old
 *       trailing bytes left behind) and Var2Data does not grow.
 *
 * WHAT ONE OUTDENT WRITES (the measured shape: a LEAF U whose parent P has no
 * other child; P becomes a leaf again):
 *   FixedData
 *     P: +84 := the duration stored in P's 0x0B40007C marker; +96 := the
 *        caller's next_start(P +104); +100 := the working-time finish from
 *        that start and restored duration (+104/+108 are NOT changed);
 *        +140 := 0; +164 := the marker's format word; +174 := P +104.
 *     U: +142 := the new parent's row id (0 for the project row);
 *        +172 := level - 1.  (+142 is written as a u16: both bytes were 0 in
 *        every measured row.)
 *   Fixed2Data
 *     P: flags := 0x30 at level 1 (0xA0 below it), start copy +46..49 := P +104.
 *     U: parent identity := the new parent's identity (the project row's for a
 *        top-level move); flags := 0x30 at level 1 (0xA0 below it).
 *   Fixed2Meta
 *     P: +95 := 0x10; +88 is LEFT at its stored 0x00 (measured; do not
 *        "repair" it to a fresh leaf's 0x0C).
 *   FixedMeta
 *     P: +2 moves by the net change (measured 10 -> 10: E4 removed, 4E5 added);
 *        +26 bit 0x80 is LEFT SET (measured: Project does not clear the summary
 *        flag when a summary becomes a leaf again; do not "repair" it).  The
 *        +8..23 blob reverses four of the indent's bytes (+12 &= ~0x0C,
 *        +16 &= ~0x10, +17 := (+17 & ~0x01) | 0x04, +18 &= ~0x20); +9's 0x40
 *        is sticky, so it is left set.
 *   VarMeta / Var2Data
 *     P loses 0x0B4004E4 and gains the constant 0x0B4004E5 block
 *       { 01 00 00 00 01 00 FF FF } when it has none.  0x0B40007C IS KEPT
 *       (measured: the restored leaf still carries its marker).
 *     The PROJECT row's child list gains U's row id, sorted ascending; the
 *       payload grows, so the new block is appended and the old one stays as
 *       garbage (measured: the old block at offset 90 is untouched and the
 *       entry points at 568).
 *
 * WHAT CHANGES WHEN THE ROWS ARE AUTOMATICALLY SCHEDULED (W29-0.4)
 * Four more committed pairs, made from the same neutral synthetic project with
 * the rows' Fixed2Meta manual bits clear (Project's COM facts say Manual=False):
 *
 *   wave29-outline-auto-indent        all rows automatic; Task 2 (leaf) is
 *                                     indented under Task 1.
 *   wave29-outline-auto-summary       all rows automatic; Task 2 (summary of
 *                                     Task 3) is indented under Task 1.
 *   wave29-outline-auto-outdent       all rows automatic; Task 2 is outdented
 *                                     from its parent Task 1.
 *   wave29-outline-manual-parent      Task 1 (the parent) and Task 3 are
 *                                     manual, Task 2 is automatic; Task 2 is
 *                                     outdented from Task 1.
 *
 * The command set is the same as the manual cases, but Project additionally
 * rolls the duration/remaining-duration and both effective finishes up the
 * chain, recalculates slack, and (for an outdent) rewrites the moved row's
 * Fixed2Data start copy.  MEASURED bytes:
 *
 *   AUTOMATIC INDENT (both the moved row and the new summary P automatic)
 *     P: +88 RemainingDuration := summary_duration (the manual shape leaves
 *        +88 alone); +108 Finish2 := the children's latest +108 (the manual
 *        shape leaves +108 alone).
 *     the PROJECT row: +84/+88 := summary_duration; +100/+108 := that latest
 *        date.  Measured only when the project's +96 Start equals the
 *        summary's earliest date and every other top-level row finishes no
 *        later than the latest child; the writer checks both, because the
 *        project's duration is the calendar span from its own start.
 *     U: Fixed2Data +42 is LEFT at its stored value for a moved automatic
 *        LEAF (0x30; the manual leaf becomes 0xA0).  A moved automatic
 *        SUMMARY still takes 0x40, as in the manual shape.  U's +174 stays NA.
 *     every LIVE automatic row (the project row included): +74/+186/+190 :=
 *        the calendar working time between that row's resulting +108 and the
 *        project's resulting +100, and +112/+116 := the backward pass from
 *        that project finish (see OutlineSlack).  The caller computes all five
 *        (OutlineSlack); the writer checks the uid set.  Measured: uid2
 *        Wed 17:00 -> project Wed 17:00 = 0; uid3 Tue 17:00 -> Wed 17:00 =
 *        4800 (0x12c0) with +112 Tue 08:00 and +116 Wed 17:00.
 *     The FixedMeta/Fixed2Meta/VarMeta/assn/Props changes are the manual
 *     indent's.
 *
 *   AUTOMATIC OUTDENT, PARENT AUTOMATIC (wave29-outline-auto-outdent)
 *     the restored parent P: +88 := restored_duration; +108 := the restored
 *        finish; +174 StartAnchor := NA (an automatic leaf; the manual shape
 *        stores P +104).  The rest of P's words are the manual shape's.
 *     the PROJECT row: +84/+88 := restored_duration; +100/+108 := the
 *        restored finish (same guards as the automatic indent).
 *     the moved row U: +174 := NA and Fixed2Meta +95 := 0x10 (both were
 *        already so in this pair); Fixed2Data +42 := 0x30 (level 1, manual
 *        rule); Fixed2Data +46 start copy := the project start from Props
 *        record 0x02400002 (already equal here, so the diff does not show it).
 *     every LIVE automatic row: +186/+190 as above.
 *
 *   AUTOMATIC MOVED ROW, MANUAL PARENT (wave29-outline-manual-parent)
 *     P stays MANUAL and is restored by the manual rules (+84, +96, +100,
 *        +140, +164, +174 := P +104, flags 0x30, meta kind 0x10, blob), and
 *        +88/+108 are LEFT alone.
 *     P has NO 0x0B4004E4 child list in the pair's control (an automatic child
 *        under a manual parent does not get one), so Project does NOT remove a
 *        child list and does NOT touch the PROJECT row's child list: it stays
 *        `{3}`.  A parent that carries a child list in this hybrid shape is
 *        unmeasured and refused.
 *     P gains 0x0B4004E5 (append; FixedMeta count 9 -> 10).  The MOVED row U
 *        also gains 0x0B4004E5 when it has none (append after P's).
 *     U: +174 := NA; Fixed2Meta +95 := 0x10; Fixed2Data +46 := the project
 *        start record (measured 0x3bf212c0 -> 0x3bf11e78); +42 := 0x30.
 *     Slack: every LIVE automatic row as above, PLUS the manual restored
 *        parent.  Measured: U 9600/9600 (0x2580), P 0/0 (P's StartSlack was
 *        9600 and becomes 0), the project row 0/0.  The unrelated MANUAL row
 *        (uid3) keeps 16800/16800: manual rows other than the restored parent
 *        are not recalculated.
 *
 *   The props project-finish record 0x02400003 (payload u32, little-endian,
 *   the project row's +100) moves with the project roll-up and is compared
 *   byte for byte by the save-pair tests.  The project START record 0x02400002
 *   is the value a top-level automatic row's Fixed2Data +46 carries; it does
 *   not change in any pair.
 *
 * WHAT THE SAME SAVES DO TO TBkndAssn AND 114/Props (W29-0.1)
 *   The assignment table is OWNED now: write_outline_edits writes its six
 *   streams and the 114/Props counters, and the save-pair tests compare them
 *   with PROJECT's file (wave 28 compared them with the control, which the
 *   wave-28 review's R28-1 rejected).  Geometry: FixedData rows 110 bytes
 *   (uid +0, task uid +4, planned work f64 +20, actual +28, remaining +44);
 *   FixedMeta entries 34 bytes (state +0: 0x00 live / 0x02 tombstoned, field
 *   count +2, row offset +4 = 110*row); Fixed2Data records 48 bytes; Fixed2Meta
 *   entries 53 bytes (+4 = 48*row, +9 new-row bit, +52 last byte); VarMeta
 *   entries the usual (uid, Var2Data offset, key) triple.  The FixedMeta
 *   header's u32 at +4 is 4 before any tombstone and 6 once one exists (it is
 *   sticky: an outdent whose tombstone remains keeps 6).
 *
 *   INDENT (both measured indents): the new summary P's assignment is
 *   TOMBSTONED, measured as
 *     FixedMeta: entry +0 := 0x02, +2 field count := 0.  The row stays in
 *       FixedData unchanged -- the state, not a removal, hides it;
 *     header +4 := 6 (4 -> 6 on the first tombstone);
 *     VarMeta: every logical entry of P's uid is removed.  Project does NOT
 *       shrink the stream: it writes the reduced logical count at +8 and
 *       leaves the last slots as stale copies of the entries that used to be
 *       there (9 entries -> logical 6; the last three still name uid 3).  The
 *       writer reproduces that tail (merge_logical_var_meta);
 *     Fixed2Data / Fixed2Meta: LEFT as stored (+9 = 0x01, +52 = 0x08 in both
 *       pairs); a tombstone does not touch either record.
 *
 *   OUTDENT: P's tombstoned assignment is RE-CREATED as a NEW row appended to
 *   FixedData; the tombstone row stays where it was.  Measured as
 *     FixedData: the tombstone's row copied with +0 uid := max(uid) + 1 (the
 *       +4 task word stays P's uid and every plan word, +20 included, is the
 *       tombstone's);
 *     FixedMeta: a new 34-byte entry, +0 := 0x00, +2 := 4, +4 := 110*new_row;
 *     Fixed2Data: a new 48-byte record -- a fresh own identity, P's Task
 *       Fixed2Data identity as the parent +16..31, and the tombstone record's
 *       constant +32..47 tail;
 *     Fixed2Meta: a new entry, +4 := 48*new_row, +9 := 0x00 (new row),
 *       +52 := 0x28;
 *     VarMeta/Var2Data: four blocks for the new uid -- 0x0F40027A (the 4-byte
 *       per-session token Project stores in 114/Props records 0x02400007 and
 *       0x024013B1; 0C 30 16 3D in wave 28, 3D 17 0E 1A in the wave-29 pairs),
 *       0x0F400031 (flat planned-work contour:
 *       {0x00200000, 44, 160000.0, work_ticks*100.0, work_ticks*8, 0, 1, 0, 0}
 *       then a 28-byte zero record), 0x0F400299 and 0x0F40029B (16 zero bytes
 *       each).  Blocks append in that order; the entries are inserted by
 *       ascending key (31, 27A, 299, 29B).  work_ticks = planned work / 100 and
 *       must be a positive integer that fits the u32 words (the writer refuses
 *       anything else).
 *
 *   114/Props (length-prefixed [u32 length][u32 id][u32 kind][payload] records):
 *     0x01000002 / 0x04000002 count the freed Var2 bytes: every removed
 *       logical entry's block length plus any in-place shrink of a block the
 *       edit rewrites.  Measured: indent task 0->16 / assignment 0->120,
 *       summary task 16->32 / assignment 104->224, outdent task 16->24 /
 *       assignment 104->104 (the re-created blocks are new; nothing is freed);
 *     0x01000100 / 0x04000100 are the two tables' Var2 lengths (indent 520->
 *       548 / 396->396, summary 548->576 / 376->376, outdent 548->580 /
 *       376->500);
 *     0x04000001 is the assignment row count (outdent 3 -> 4);
 *     0x04008000 := 1 iff some tombstoned assignment's task has no LIVE
 *       ASSIGNMENT row left (indent 0 -> 1, the summary pair stays 1, the
 *       outdent's re-created row makes it 0).
 *     Project also reallocates two per-save opaque id records (0x02400029,
 *     0x024013c2); the writer leaves the control's bytes and the save-pair
 *     tests name those two words as expected failures.
 *
 * ROLL-UP ARITHMETIC IS THE CALLER'S.  A summary's +84 is not the sum of its
 * children's durations: it is the project calendar's working time between the
 * children's earliest +104 and latest +108 (the indent pair's +84 = 0x2ee0 for
 * children spanning Sun 13:00 .. Wed 12:00).  core_mppwrite is standard-library
 * only and does not own the calendar, so `summary_duration` is an input.  The
 * writer computes the two dates itself and refuses a subtree whose dates are
 * not valid.  Outdent's restored start/finish are likewise calendar arithmetic
 * supplied by the caller; the writer verifies the two marker words they came
 * from.
 *
 * SHAPES THE WRITER REFUSES (with a warning, leaving that edit unwritten)
 * because no Project pair measured them:
 *   - indent of a non-top-level row, into a preceding sibling that is already
 *     a summary or already carries a 0x7C marker / child list, or a row whose
 *     row id does not fit the measured one-byte +142 slot;
 *   - indent whose new summary P does not have exactly one LIVE assignment row
 *     (the tombstone shape the pairs measured);
 *   - outdent of a summary, of a row whose parent has another child, or of a
 *     row whose parent has no 0x7C marker;
 *   - outdent whose parent P does not have exactly one TOMBSTONED assignment
 *     row, or whose planned work is not a positive integral number of ticks
 *     that fits the contour's f64/u32 words;
 *   - an indent whose moved row and new summary are scheduled DIFFERENTLY
 *     (one automatic, one manual), or an outdent of a MANUAL row from an
 *     AUTOMATIC parent: no pair measures a mixed edit in those directions;
 *   - an automatic indent/outdent that is not the measured automatic shape:
 *     the project row manual, the summary not top-level, the project's +96
 *     start different from the summary's earliest/restored start, another
 *     top-level row finishing after the new project finish, a missing
 *     project-start / project-finish Props record, or a slack vector whose
 *     uids are not exactly the rows Project recalculates;
 *   - a hybrid outdent whose manual parent carries a 0x0B4004E4 child list
 *     (the measured one has none) or whose moved row is not the level-2 leaf
 *     the pairs measured.
 * The writer reads the manual bit itself (Fixed2Meta +8 & 0x80, the same bit
 * task_is_manually_scheduled reads) and picks the manual or automatic rules
 * from that; the APP uses the same bit to gate which command it offers
 * (app::make_outline_edit, W29-0.4).  The app also refuses an indent whose new
 * summary carries progress, a resource-loaded work assignment, or an actuals
 * edit queued in the same save (W29-0.0).  The wave-28 text here said the
 * writer refused an outdent "from a manually-scheduled parent" -- the opposite
 * of the measured manual pair (its parent is manual) and a check that did not
 * exist; W29-0.4 measured that direction explicitly.
 * A damaged task/outline table (Fixed2Data or metadata not the measured
 * geometry, a malformed E4/marker block) THROWS instead: that is a file the
 * writer cannot keep consistent, not an edit the user asked for.
 *
 * EVIDENCE (wave 29, W29-0.1): the wave-28 save-pair test did NOT assert
 * "every owned stream against Project" -- it compared the Task streams with the
 * CONTROL, checked only some bytes of FixedMeta/Fixed2Data, asserted every
 * other stream equalled the control, and merely printed Project's TBkndAssn
 * row counts.  Rule 1 does not accept that, and no Project COM reopen of the
 * port's outline save existed.  W29-0.1:
 *   - turns Project's changed bytes into named checks: the core save-pair test
 *     (tests/roundtrip_outline.cpp) and the app test
 *     (app/app_edits_outline_tests.cpp) compare the six Task streams, the six
 *     TBkndAssn streams and 114/Props with PROJECT's file, field by field
 *     (FixedMeta whole, Fixed2Data after identity canonicalisation, the six
 *     Props counters by name), and every stream outside that set is either
 *     "port == Project" or a named EXPECTED FAILURE -- never "port == control";
 *   - implements the tombstone, the re-create and the FixedMeta blob rules
 *     above, so the whole owned set matches Project's save in all three pairs
 *     (only the two per-save Props id words stay expected failures).
 *
 * REOPEN (wave 29, W29-0.1): scripts/oracle_w29_reopen.py had Project 16.0
 * open the port's three outline saves and Project's own saves of the same
 * edits (copies under build/oracle-work) and record every row by ID; the facts
 * are in build/w29/W29-0.1-project-reopen.json, sha256
 * cc68d5957124b86611477f954270e57945820d3b45515ee4ba9afd69b00c8d1f.  Each
 * pair -- app-outline-indent, app-outline-summary, app-outline-outdent -- has
 * 0 differing fields over the project summary row and IDs 1..3 (Manual,
 * Summary, OutlineNumber, Duration,
 * Start, Finish, percent complete and percent work complete, actual and
 * remaining duration, ActualStart/ActualFinish, Work/ActualWork/RemainingWork).
 * The same run reopens the actuals and new-task-mode saves the rest of W29-0.1
 * covers (10 pairs, all 0 differing fields; see the record).  The fixtures'
 * TBkndAssn rows are resource-less (resource word 0xFFFF0001), so Project
 * exposes no Assignment objects for them; that layer is covered by the
 * field-by-field TBkndAssn comparison above, not by the reopen.
 */

#ifndef MPPWRITE_OUTLINE_H
#define MPPWRITE_OUTLINE_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "table.h"

namespace mppwrite {

/* The 0x0B40007C summary marker of one task, if it has one.  `present` is
 * false for a task without the block or with a block that is not the measured
 * 12-byte shape; the writer then refuses to outdent that task. */
struct OutlineMarker {
    bool present = false;
    std::uint32_t duration_ticks = 0;
    std::uint16_t duration_format = 0;
};

OutlineMarker read_outline_marker(CfbDocument const& doc, std::uint32_t uid,
                                  std::string_view table = "114/TBkndTask");

/* W29-0.4: one row's recalculated schedule words, as the caller's calendar
 * computed them.  Project's automatic save rewrites, for every row it
 * recalculates:
 *   +74  FreeSlack,
 *   +112 LateStart,
 *   +116 LateFinish,
 *   +186 StartSlack,
 *   +190 TotalSlack.
 * The writer checks that the vector names exactly the rows Project
 * recalculates for the edit's measured shape -- every live automatic row, plus
 * the restored manual parent of a hybrid outdent -- and writes all five words.
 * MEASURED (the four W29-0.4 pairs): FreeSlack == StartSlack == TotalSlack on
 * every recalculated row, LateFinish is the resulting project finish (+108),
 * and LateStart is the BACKWARD pass -- sub_work(resulting project finish,
 * the row's resulting +84 duration) -- except on the project row, whose
 * LateStart is the project start record (in the hybrid pair that is earlier
 * than the backward pass lands).  Nothing here does calendar arithmetic: the
 * caller supplies the packed dates.  The manual shape (both rows manual) takes
 * no slack block. */
struct OutlineSlack {
    std::uint32_t uid = 0;
    std::uint32_t free_slack = 0;  /* +74, ticks */
    std::uint32_t start_slack = 0; /* +186, ticks */
    std::uint32_t total_slack = 0; /* +190, ticks */
    std::uint32_t late_start = 0;  /* +112, packed date */
    std::uint32_t late_finish = 0; /* +116, packed date */
};

/* One requested outline edit.  Indent uses summary_duration; outdent uses the
 * four restored_* values.  `uid` is the moved task's UniqueID.  For an edit
 * whose rows are automatically scheduled the caller also supplies `slack`
 * (see OutlineSlack). */
struct OutlineEdit {
    std::uint32_t uid = 0;
    bool indent = true;
    /* Indent: the new summary's +84 (calendar working time across the moved
     * subtree).  The writer refuses when the subtree has no valid dates. */
    std::uint32_t summary_duration = 0;
    /* Outdent: the parent's 0x0B40007C words (which the writer re-reads and
     * compares) and the restored schedule the caller computed from them. */
    std::uint32_t restored_duration = 0;
    std::uint16_t restored_duration_format = 0;
    std::uint32_t restored_start = kInvalidDate;
    std::uint32_t restored_finish = kInvalidDate;
    /* Automatic shapes only (W29-0.4); empty for the manual shape. */
    std::vector<OutlineSlack> slack;
};

/* Fixed2Data +42 bit that marks a summary row (see the byte list above). */
inline constexpr std::uint8_t kOutlineSummaryFlag = 0x40;

struct OutlineWriteResult {
    /* Number of edits actually applied; a refused edit is not counted. */
    std::size_t rows_written = 0;
    /* One message per refused edit. */
    std::vector<std::string> warnings;
};

/*
 * Apply every edit to `doc` in order and write the owned streams back: the six
 * TBkndTask streams, the six TBkndAssn streams and 114/Props.  Each edit sees
 * the tables as the previous edit left it.  All streams are built on copies
 * first, so a throw leaves `doc` byte-identical.  A refusal changes only that
 * edit (the rest still apply) and is reported in `warnings`; every measured-
 * shape check for an edit runs before that edit's first mutation.
 */
OutlineWriteResult write_outline_edits(
    CfbDocument& doc, std::vector<OutlineEdit> const& edits,
    std::string_view table = "114/TBkndTask");

/* One live row of the file's outline, for the app's calendar arithmetic and
 * validation.  `parent_row` is SIZE_MAX when no identity matched; top-level
 * task rows name the PROJECT row (0), as Project stores them. */
struct OutlineRow {
    std::size_t row = 0;
    std::uint32_t uid = 0;
    std::uint32_t row_id = 0;
    std::uint16_t level = 0;        /* +172 */
    std::uint32_t duration = 0;     /* +84 */
    std::uint16_t duration_format = 0; /* +164 */
    std::uint32_t start = kInvalidDate;  /* +104 */
    std::uint32_t finish = kInvalidDate; /* +108 */
    /* Fixed2Meta +8 & 0x80, the same Manually Scheduled bit
     * task_is_manually_scheduled reads (W29-0.4).  The automatic rule set
     * recalculates every row with this false. */
    bool manual = false;
    bool deleted = false;
    bool stored_children = false;   /* +140 */
    std::uint8_t fixed2_flags = 0;  /* Fixed2Data +42 */
    std::size_t parent_row = static_cast<std::size_t>(-1);
    std::vector<std::size_t> child_rows;
    /* The 0x0B4004E4 payload's child ids, when the row has one (empty when the
     * block is absent).  Kept for the writer's and the app's validation. */
    std::vector<std::uint32_t> stored_child_ids;
};

/*
 * Decode the outline of `doc`: parent links from Fixed2Data, siblings ordered
 * by FixedData +0 as core/mpp does, levels from +172.  Returns false (and an
 * error message) for a table whose geometry the writer cannot trust.
 */
bool read_outline_rows(CfbDocument const& doc, std::string_view table,
                       std::vector<OutlineRow>& rows, std::string& error);

} /* namespace mppwrite */

#endif /* MPPWRITE_OUTLINE_H */
