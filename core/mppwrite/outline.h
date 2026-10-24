/*
 * outline.h -- Project's stored task outline for an Indent / Outdent edit
 * (wave 28, W28-2.1).
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
 *        entry's 16-byte blob at +8 is opaque per-save data Project rewrites on
 *        the row it touches; the writer leaves it and the save-pair test masks
 *        it on that row.
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
 *        flag when a summary becomes a leaf again; do not "repair" it).
 *   VarMeta / Var2Data
 *     P loses 0x0B4004E4 and gains the constant 0x0B4004E5 block
 *       { 01 00 00 00 01 00 FF FF } when it has none.  0x0B40007C IS KEPT
 *       (measured: the restored leaf still carries its marker).
 *     The PROJECT row's child list gains U's row id, sorted ascending; the
 *       payload grows, so the new block is appended and the old one stays as
 *       garbage (measured: the old block at offset 90 is untouched and the
 *       entry points at 568).
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
 *   - outdent of a summary, of a row whose parent has another child, of a row
 *     whose parent has no 0x7C marker, or from a manually-scheduled parent.
 * A damaged task/outline table (Fixed2Data or metadata not the measured
 * geometry, a malformed E4/marker block) THROWS instead: that is a file the
 * writer cannot keep consistent, not an edit the user asked for.
 *
 * NOT WRITTEN: Project's own outline save also tombstones the assignment row
 * of the task that becomes a summary (and, on outdent, creates a new assignment
 * for the restored leaf).  TBkndAssn is outside this writer, exactly as it is
 * outside the W28-1.2 actuals writer; the save-pair tests assert every Task
 * stream against Project and treat TBkndAssn separately, and Project's reopen
 * of the port's save is the acceptance test (the W28-0.4 precedent).
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

/* One requested outline edit.  Indent uses summary_duration; outdent uses the
 * four restored_* values.  `uid` is the moved task's UniqueID. */
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
};

struct OutlineWriteResult {
    /* Number of edits actually applied; a refused edit is not counted. */
    std::size_t rows_written = 0;
    /* One message per refused edit. */
    std::vector<std::string> warnings;
};

/*
 * Apply every edit to `doc` in order and write the six owned TBkndTask streams
 * back.  Each edit sees the table as the previous edit left it.  All streams
 * are built on copies first, so a throw leaves `doc` byte-identical.  A
 * refusal changes only that edit (the rest still apply) and is reported in
 * `warnings`.
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
