/*
 * add_delete.h -- grow or shrink the Task table: append one task row, or
 * tombstone a row that already exists.
 *
 * WHY THIS EXISTS.  Until now the writer could only rewrite values inside a row
 * that already existed: `table.cpp` refuses a row-count change on purpose, and
 * spec/mpp-writer.md §5 listed "adding a table row" as not implemented.  The
 * measured wave-10 oracle (`docs/analysis/oracle/w10-oracle.md` §1) gives the
 * exact shape Project writes for `Tasks.Add` and for delete, so both operations
 * can be implemented against bytes instead of guesses:
 *
 * ADD ONE ROW (all append-only; measured on 5 tasks + one add):
 *   114/TBkndTask/FixedData    +202 B   new row at 48 + 202*n, id = uid = new
 *   114/TBkndTask/FixedMeta    +47 B    new entry: +0 = 0x00, +2 = 0x08 (the
 *                                       row's variable-field count), +4 = the
 *                                       new FixedData offset; header +8 entry
 *                                       count + 1, header +12 = new byte size
 *   114/TBkndTask/Fixed2Data   +64 B    new 64-byte record at index row + 3;
 *                                       +16 f64 = record ordinal + 1 (row + 1),
 *                                       +0 a fresh row identity, +24 the project
 *                                       row's identity (a new task is top level)
 *   114/TBkndTask/Fixed2Meta   +96 B    new entry: +4 = the record offset,
 *                                       +8 = 0x7f auto / 0xff manual; header +8
 *                                       count + 1, header +12 = new byte size
 *   114/TBkndTask/VarMeta      +96 B    8 new entries for the new uid, in this
 *                                       order: 0x0b40000e (Name), 0x0b4000a4,
 *                                       0x0b4000a5, 0x0b4000a8, 0x0b4000a9,
 *                                       0x0b4000aa, 0x0b400563, 0x0b400564;
 *                                       header +8 declared count + 8, header
 *                                       +0x14 = new Var2Data byte size
 *   114/TbkndTask/Var2Data     +blocks  the 8 payload blocks appended at the
 *                                       END; the new entries point at them
 *
 * Project compacts Var2Data on its own add (it moves the project title block
 * and rewrites 33 bytes at offset 76 -- w10-oracle §1).  We do NOT copy that
 * churn.  Appending is self-consistent: every block is length-prefixed and every
 * VarMeta entry carries its own offset, so the old bytes stay valid garbage of
 * the kind real files already contain (spec/mpp-records.md §5.3).
 *
 * DELETE IS A TOMBSTONE, NEVER A SHRINK (measured, w10-oracle §1):
 *   - FixedMeta entry +0 = 0x02, +2 = 0;
 *   - FixedMeta HEADER word +4 is OR-ed with 0x02, the table flag that says
 *     "this table contains deleted rows".  MEASURED by the coordinator's
 *     read-only open of W45's first fixtures: the entry tombstone alone is NOT
 *     honoured -- Project reopened them with the deleted task still visible.
 *     Project's own files carry 0x00000004 with no tombstone (base,
 *     w10-adddel-base/add, PROJECT_A, synthetic-base) and 0x00000006 as soon as one
 *     row is deleted (w10-adddel-back/-del, and PROJECT_B with its 12 deleted
 *     slots).  The bit is only ever set, never cleared;
 *   - every stream keeps its byte size: the FixedData slot, its Fixed2Data
 *     record and its Fixed2Meta entry all stay;
 *   - later live rows' ID (FixedData +0) are renumbered down by one, which is
 *     what Project did (uid 5's ID 5 -> 4 after deleting uid 4);
 *   - the deleted row's VarMeta entries are removed and its Var2Data blocks are
 *     left orphaned.  Project instead re-keys them onto the next uid and keeps
 *     the entry array length while decrementing the declared count (back: 59
 *     entries, declared 51; del: 59 entries, declared 43); that is a quirk, not
 *     a requirement, and copying it would corrupt another task's field set.  A
 *     fixture that keeps the entries and only decrements the declared count is
 *     emitted for the oracle (`w45-del-middle-b.mpp`) but is not the supported
 *     write shape: it leaves VarMeta's declared count stale on purpose, which
 *     `validate_table` (correctly) refuses on a later edit.
 *
 * WHY THE READER NEEDED A FIX (done in W45, core/mpp/reader.cpp): the reader
 * used VarMeta presence as liveness, so a row tombstoned by `delete_task_row`
 * (whose VarMeta group survives, exactly like Project's own add-then-delete
 * file) was read as a live task.  Measured: the add-then-delete file reopens in
 * Project as 5 tasks and the old reader returned 6.  The Task liveness predicate
 * is now "a FixedMeta entry with +0 == 0 AND a VarMeta group", the same flag
 * Project uses.
 *
 * WHAT A NEW ROW CONTAINS (our choice; only id/uid and the structural sizes were
 * measured, so do not read this as Project's default-task fields):
 *   - the row is a clone of the LAST LIVE task row, so every one of its 202 bytes
 *     is a legal value for this file (dates, duration family, slack, calendar);
 *   - +0 ID = (largest live task ID) + 1, +4 UniqueID = caller's or
 *     (largest uid in the whole table, tombstones included) + 1;
 *   - +64/+66 (constraint) and +174 (start anchor) are cleared to ASAP / NA and
 *     to NA: a constraint is an explicit scheduling instruction and must not be
 *     silently inherited by a new task.  Supply `TaskValues::constraint_*` /
 *     `start_anchor` to write them;
 *   - progress (percent, actual duration, actual dates) is cloned with the rest
 *     of the row.  Pass `TaskValues` to clear or change it;
 *   - its Fixed2Meta flags byte is 0x7f (auto-scheduled) by default; request
 *     0xff with `NewTaskRow::manually_scheduled`.  The measured `Tasks.Add` row
 *     was auto even in a file whose other rows were manual, which is why auto is
 *     the default;
 *   - its Fixed2Data record copies the template's, except the identity, the
 *     ordinal and the parent (= the project row's identity) and with the
 *     has-children flag (byte +42, bit 0x40) cleared.
 * `TaskValues` fields are merged onto the cloned row: the kInherit sentinels keep
 * the template's value, and every supplied value is written through
 * `write_task_row`, so the six date copies and the duration family keep their
 * existing cross-copy rules.  Supplying only one member of a copy family is
 * therefore refused by that function, not silently derived -- a new task that
 * moves its dates must supply all six copies.
 *
 * ALL-OR-NOTHING AND PROVEN FROM DISK.  Both entry points validate the table and
 * the request, apply the change to a COPY of the document, save to `path`,
 * re-open the file and require: the exact byte count/stream sizes, the row count,
 * the uid and ID, the written values and name, the FixedMeta/Fixed2Meta entry
 * state (live/tombstone and flags), the FixedMeta header's deleted-rows flag
 * (bit 0x02 at header +4), the VarMeta/Var2Data header words, an
 * empty `CfbDocument::check()` and an empty `check_table()`.  A mismatch throws;
 * on success `doc` is replaced by the re-read document, so `doc` and the file on
 * disk agree.  A refusal leaves both `doc` and the output file untouched.
 *
 * REFUSALS (all exceptions with a message naming the reason):
 *   - uid 0 (reserved for the project row), a duplicate uid, a uid that would
 *     overflow, an empty task name;
 *   - deleting a uid that no row holds, a row with no FixedMeta entry, a row
 *     whose FixedMeta entry is already a tombstone, a row with no VarMeta group
 *     (not a live task under Project's rule);
 *   - a table whose size identity is broken (`check_table` and the explicit
 *     header checks below reject it).  Consequence: the real PROJECT_A file is
 *     REFUSED, because its FixedMeta header declares 87 entries and holds 88;
 *     repairing that damage is out of scope (spec/mpp-writer.md §5.3) and is
 *     exactly the kind of guess that must not be made in a structural write;
 *   - a delete that would leave a live link or a live assignment naming the
 *     deleted uid.  The writer cannot delete link/assignment rows, and a link is
 *     scheduling-visible.  `DeleteOptions::allow_dangling` opts in: the delete
 *     then proceeds and `DeletedRow::warnings` says how many rows were left
 *     behind.  The default refusal is deliberate; pass the flag from the app
 *     only when it has decided to repair (or accept) the orphan rows.  Real
 *     files already contain assignments naming deleted task uids (PROJECT_B: 10 of
 *     73), so Project tolerates the orphan; a link whose endpoint is gone is not
 *     scheduled, which is why the two are reported separately.
 *
 * WHAT THIS FILE DOES NOT DO.  It does not write `214/` (decision and evidence:
 * spec/mpp-writer.md §10.4); the view state is carried through byte-identically
 * and Project rewrites it on its next save.  It does not add or remove directory
 * entries, does not repair damaged metadata, does not re-key VarMeta the way
 * Project does, and does not update the project row's duration, links,
 * assignments or summaries -- Project recalculates those on open.
 */

#ifndef MPPWRITE_ADD_DELETE_H
#define MPPWRITE_ADD_DELETE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "row_writer.h"
#include "table.h"

namespace mppwrite {

/* One row to append, addressed the way a caller can describe a new task. */
struct NewTaskRow {
    /* Required, non-empty.  Stored as UTF-16LE plus a NUL in Var2Data under key
     * 0x0b40000e, the same encoding every measured Task.Name uses. */
    std::string name;

    /* 0 = allocate (largest UniqueID in the table, tombstones included, + 1).
     * A non-zero value must not be held by any existing row, tombstone
     * included.  0 is never written: it is the project row's UniqueID. */
    std::uint32_t uid = 0;

    /* Values to merge onto the cloned template row.  kInherit (or kInvalidDate /
     * kInheritSlack) keeps the template's stored value; the merged set goes
     * through `write_task_row`, with its cross-copy rules. */
    TaskValues values;

    /* Fixed2Meta entry byte +8 bit 0x80.  false writes 0x7f (auto-scheduled,
     * the measured Tasks.Add default) and true writes 0xff (manual).  The byte
     * is written exactly, not OR-ed into the template's flags: this is the
     * row's scheduling mode, and the caller states it. */
    bool manually_scheduled = false;
};

/* What `add_task_row` proved from the file on disk. */
struct AddedRow {
    std::uint32_t uid = 0; /* FixedData +4 of the new row */
    std::uint32_t id = 0;  /* FixedData +0 of the new row */
    std::size_t row = 0;   /* FixedData row index (rows() - 1 after the call) */
    TaskValues values;     /* exactly what the file holds */
    std::vector<std::string> warnings; /* write_task_row's warnings */
};

/* Policy for a delete, because the writer cannot remove dependent rows. */
struct DeleteOptions {
    /* false (default): a live link or assignment naming the uid refuses the
     * delete.  true: proceed and report the orphaned rows in `warnings`. */
    bool allow_dangling = false;
};

/* What `delete_task_row` proved from the file on disk. */
struct DeletedRow {
    std::uint32_t uid = 0;
    std::uint32_t id = 0;  /* the ID the row had before the delete */
    std::size_t row = 0;   /* the FixedData row that is now a tombstone */
    std::size_t var_entries_removed = 0;
    std::vector<std::string> warnings; /* dangling rows left behind, if allowed */
};

/*
 * Append one task row, write the whole package to `path`, re-open it and prove
 * the appended row from the bytes on disk (see the file comment).  On success
 * `doc` is replaced by the re-read document and the return value describes it.
 *
 * `path` must name a file in an existing directory; the parent directory is not
 * created.  The table path defaults to the Task table: "114/TBkndTask".
 */
AddedRow add_task_row(CfbDocument& doc, NewTaskRow const& task,
                      std::string const& path,
                      std::string_view table = "114/TBkndTask");

/*
 * Tombstone the task row whose UniqueID is `uid`, write the package to `path`,
 * re-open it and prove the tombstone and the renumbering from the bytes on disk.
 * No stream changes size; FixedData (later live rows' ID), FixedMeta (the
 * entry's +0/+2) and VarMeta (the uid's entries removed) are the only streams
 * written, and Fixed2Data/Fixed2Meta/Var2Data are required to be byte-identical.
 */
DeletedRow delete_task_row(CfbDocument& doc, std::uint32_t uid,
                           std::string const& path,
                           DeleteOptions const& options = {},
                           std::string_view table = "114/TBkndTask");

} /* namespace mppwrite */

#endif /* MPPWRITE_ADD_DELETE_H */
