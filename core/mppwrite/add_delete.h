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
 * DEPENDENT ROWS: THE DELETE CASCADE (measured, w11-oracle §2).  A task delete
 * in Project does not stop at the Task table.  `w11-linkdelete-pre.mpp` (T1 ->
 * T2 -> T3, one assignment on T2) was saved after deleting T2 and every
 * dependent row is still in place with a tombstone in its own table:
 *
 *   TBkndCons/FixedMeta   header +4 4 -> 6; BOTH link entries (1->2 and 2->3)
 *                         +0 00 -> 02 and +2 -> 0; FixedData byte-identical
 *   TBkndAssn/FixedMeta   header +4 4 -> 6; the T2/R1 entry +0 -> 02, +2 -> 0;
 *                         FixedData byte-identical (the surviving rows' cached
 *                         values were recalculated, which we do not copy)
 *   TBkndTask/FixedMeta   header +4 4 -> 6; the task's own entry +0 -> 02,
 *                         +2 -> 0
 *
 * So the cascade is the SAME 16-byte-header contract as the task tombstone:
 * every FixedMeta entry whose row names the deleted uid gets +0 = 0x02 and
 * +2 = 0, and that table's FixedMeta header +4 gets bit 0x02.  No stream
 * shrinks.  A link row is selected when its predecessor (+4) OR successor (+8)
 * is the deleted uid; an assignment row is selected when its task uid (+4) is.
 * Project then reopens with those rows gone from its model (2 tasks, no link or
 * assignment naming the deleted uid).
 *
 * `DeleteOptions::allow_dangling` therefore no longer covers the normal case --
 * the cascade handles a live link/assignment naming the deleted uid by itself.
 * It is kept for ONE damaged-file case: a surviving live link row whose OTHER
 * endpoint already names a uid that no live task holds (a file that was already
 * inconsistent before this delete).  The default refuses that, because the
 * writer cannot tell whether the orphan endpoint is intentional; with the flag
 * the delete proceeds and the orphan rows are reported in `warnings`.  Live
 * assignment rows naming an already-dead task uid are tolerated and warned
 * about, not refused: PROJECT_B stores 10 of 73 that way and Project reopens it
 * (w10-oracle §1).  The cascade writes only the two dependent FixedMeta
 * streams; their FixedData, Fixed2Data, Fixed2Meta, VarMeta and Var2Data are
 * required to read back byte-identically (Project's own save also rewrites the
 * dead assignment's VarMeta blocks and the survivors' cached values -- that is
 * save churn, not part of the contract).
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
 *   - a link or assignment table whose geometry is broken (FixedData not a
 *     whole number of rows, FixedMeta not 16 + n*entry, a FixedMeta entry whose
 *     +4 word does not name a row, or a missing FixedMeta next to a non-empty
 *     FixedData).  The cascade needs trustworthy entry offsets, so it refuses
 *     rather than guessing which entries to tombstone;
 *   - a delete that would leave a live link whose OTHER endpoint already names
 *     a uid no live task holds (a file that was damaged before the delete).
 *     `DeleteOptions::allow_dangling` opts in; the delete then proceeds and
 *     `DeletedRow::warnings` names the orphan rows.  This is the flag's only
 *     remaining use -- a link or assignment naming the uid being deleted is
 *     cascade-tombstoned by default.
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

/* Policy for a delete.  The normal case -- live links or assignments naming the
 * uid -- is handled by the cascade (see the file comment) and needs no flag. */
struct DeleteOptions {
    /* false (default): a surviving live link whose other endpoint already names
     * a uid no live task holds refuses the delete (a file that was damaged
     * before it).  true: proceed and report those orphan rows in `warnings`.
     * This does not affect rows naming the deleted uid: those are always
     * cascade-tombstoned. */
    bool allow_dangling = false;
};

/* What `delete_task_row` proved from the file on disk. */
struct DeletedRow {
    std::uint32_t uid = 0;
    std::uint32_t id = 0;  /* the ID the row had before the delete */
    std::size_t row = 0;   /* the FixedData row that is now a tombstone */
    std::size_t var_entries_removed = 0;
    /* Cascade receipts: dependent FixedData rows whose FixedMeta entries are now
     * tombstones because they named this uid (w11-oracle §2).  Rows already
     * tombstoned before the delete are not counted again. */
    std::size_t link_rows_tombstoned = 0;       /* TBkndCons predecessor or successor */
    std::size_t assignment_rows_tombstoned = 0; /* TBkndAssn task uid */
    std::vector<std::string> warnings; /* cascade receipt + damaged-file rows */
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
 * Tombstone the task row whose UniqueID is `uid` AND cascade the delete into the
 * dependent tables, write the package to `path`, re-open it and prove the
 * tombstones and the renumbering from the bytes on disk.  No stream changes
 * size.  The written streams are FixedData (later live rows' ID), FixedMeta (the
 * task entry's +0/+2 and the header +4 flag), VarMeta (the uid's entries
 * removed) and -- when the uid is named -- TBkndCons/FixedMeta and
 * TBkndAssn/FixedMeta (the dependent entries' +0/+2 and their header +4 flags).
 * Everything else, including the dependent tables' FixedData and the Task
 * table's Fixed2Data/Fixed2Meta/Var2Data, is required to be byte-identical.
 */
DeletedRow delete_task_row(CfbDocument& doc, std::uint32_t uid,
                           std::string const& path,
                           DeleteOptions const& options = {},
                           std::string_view table = "114/TBkndTask");

/* --- link rows: 114/TBkndCons (W52) ------------------------------------- */

/*
 * A link row is 20 bytes (spec/mpp-records.md §10.1, measured on Project's own
 * corpus/probe/link1.mpp, five links created over COM):
 *
 *   +0  u32  link uid (dense, 1..N)
 *   +4  u32  predecessor task UniqueID
 *   +8  u32  successor task UniqueID
 *   +12 u16  link type: 0 = FF, 1 = FS, 2 = SF, 3 = SS
 *   +14 i32  lag in 6-second ticks (4800 = one 8-hour working day)
 *   +18 u16  lag format unit: 7 in every live row of link1.mpp
 *
 * Unlike the Task table, TBkndCons has NO 48-byte FixedData header and NO
 * rows+3 padding (measured, link1.mpp and w11-linkdelete-pre/-post.mpp):
 *
 *   FixedData = 20 * rows
 *   FixedMeta = 16 + 10 * rows          ONE entry per row
 *
 * and every live FixedMeta entry is `00 00 00 00 <row offset u32> dd 00`:
 * +0 state 0 (live), +2 field count 0 (the table has no variable values), +4 the
 * row's byte offset, +8/+9 the measured tail bytes 0xdd 0x00.  A Project
 * tombstone is `02 00 00 00 <offset u32> dd 00` -- only state +0 changes; the
 * tail bytes and the row offset stay.  There is no VarMeta/Var2Data content:
 * both streams exist (24-byte empty header + empty data) and stay untouched.
 *
 * The link table has its own Fixed2Data/Fixed2Meta pair, and Project's own COM
 * add grows all four streams (measured twice: corpus/probe/link1.mpp and the
 * coordinator's corpus/probe/w12-linkadd.mpp, built from synthetic-base.mpp with
 * two links 1->2 FS+0 and 1->3 SS+1d):
 *
 *   Fixed2Data = 48 * rows          three 16-byte identities per link row:
 *                                   [link identity, predecessor task identity,
 *                                    successor task identity]
 *   Fixed2Meta = 16 + 9 * rows      one 9-byte entry per row:
 *                                   +0..3 = 0, +4 = record offset (row * 48),
 *                                   +8 = 0x0f / 0x07 (see below)
 *
 * The identities are 16 bytes that share a per-file suffix; only byte +3 varies
 * in both measured files (link1: `63 ac ca XX ...` with task ids 0x41/0x50/0x52/
 * 0x54/0x56 and link ids 0x58..0x5c; w12-linkadd: `13 67 bc XX ...` with task
 * ids 0x09/0x0a/0x0b/0x0c and link ids 0x12/0x13).  Project allocates them from a
 * file-global counter we have not identified, so `add_task_link` does NOT copy
 * its numbers: the two endpoint identities are the predecessor/successor task
 * rows' measured Task/Fixed2Data identities (copied byte for byte), and the link
 * identity is generated deterministically -- the same 16-byte template with its
 * counter byte set to the lowest value not used by any identity of the same GUID
 * family anywhere in this file.  That guarantees uniqueness (which is what the
 * identity is for) without inventing Project's counter.  The Project gate is the
 * proof that Project accepts this.
 *
 * Fixed2Meta entry byte +8 is a DRIVING-STATE byte, not a position marker.  The
 * wave-47 reading ("0x0f for the first link row naming a successor, 0x07 for
 * later ones") is falsified by the wave-29 corpus: wave29-percent-mid25-control
 * has three live links into three distinct successors, yet the first is 0x07
 * and the other two are 0x0f, and wave29-percent-work40 shows the one live
 * incoming link to the edited row move 0x0f -> 0x07 when that row enters
 * progress (W29-1.0b).  `add_task_link` writes 0x0f only for a first link into a
 * 0 % successor -- the shape the two measured adds cover.  It refuses a
 * successor that already has a live incoming link (the old rule's 0x07 shape)
 * and a progressed successor (the old rule's 0x0f shape), because no pair adds
 * either (W30-0.7, R29-7).  `stage_progress_link_flags` performs the measured
 * edit-time change.
 *
 * A delete changes only FixedMeta: the entry's +0 becomes 0x02, +2 stays 0, the
 * table header +4 gets bit 0x02, and FixedData/Fixed2Data/Fixed2Meta keep every
 * byte (measured on Project's own w11-linkdelete-post.mpp and required by the
 * W48 task-delete cascade).
 */

inline constexpr std::size_t kLinkRowSize = 20;
inline constexpr std::size_t kLinkMetaEntrySize = 10;

inline constexpr std::uint16_t kLinkTypeFF = 0; /* finish-to-finish */
inline constexpr std::uint16_t kLinkTypeFS = 1; /* finish-to-start  */
inline constexpr std::uint16_t kLinkTypeSF = 2; /* start-to-finish  */
inline constexpr std::uint16_t kLinkTypeSS = 3; /* start-to-start   */

/* The lag format unit every live row of Project's link1.mpp stores.  The spec
 * calls it the likely PjFormatUnit (7 = working days), but LagFormat is not
 * proven.  `add_task_link` writes it by default.
 *
 * kLinkLagFormatMinutes = 3 is what Project stores when the lag was expressed in
 * MINUTES: the scenario applier passes COM a lag as "480m" (scripts/
 * engine_diff.py `_lag_text`), and the link-fs-lag oracle stores LagFormat 3
 * where our writer wrote 7.  The same PjFormatUnit numbering shows up as
 * Task.DurationFormat 3 after a "1440m" duration edit (duration-change oracle).
 * INFERENCE that 3 = minutes, from those two measured files; the value is
 * written only when the caller says the edit was in minutes. */
inline constexpr std::uint16_t kLinkLagFormatWorkingDays = 7;
inline constexpr std::uint16_t kLinkLagFormatMinutes = 3;

/* WRITER POLICY, not a measured Project limit: the file field is a signed i32 of
 * 6-second ticks, so every int32 is representable, but a lag beyond ~100 years is
 * a caller bug rather than a schedule.  Larger magnitudes are refused. */
inline constexpr std::int32_t kMaxLinkLagTicks = 175200000;

/* Link Fixed2Data: 48 bytes per row, three 16-byte identities. */
inline constexpr std::size_t kLinkFixed2RecordSize = 48;
inline constexpr std::size_t kLinkIdentitySize = 16;
/* Link Fixed2Meta: a 16-byte header plus one 9-byte entry per row. */
inline constexpr std::size_t kLinkFixed2MetaEntrySize = 9;
/* Entry byte +8 is the successor's driving state, not a position marker (see
 * the note above): 0x0f is the ordinary state, 0x07 the state the measured
 * progress transition writes (the wave-29 percent pair moves 0x0f -> 0x07 when
 * its successor enters progress; the mid25 control's first of three links is
 * already 0x07 because that successor has progress).  `add_task_link` writes
 * only the ordinary 0x0f: W30-0.7 refuses the two shapes whose byte the wave-47
 * first/later rule got wrong -- a second incoming link and a progressed
 * successor -- until a Project pair measures them. */
inline constexpr std::uint8_t kLinkFixed2BaseFlag = 0x0F;
inline constexpr std::uint8_t kLinkFixed2EnteredProgressFlag = 0x07;

/* What `add_task_link` proved from the file on disk. */
struct AddedLink {
    std::uint32_t uid = 0;             /* FixedData +0 of the new row */
    std::uint32_t predecessor_uid = 0; /* +4, equals the argument */
    std::uint32_t successor_uid = 0;   /* +8, equals the argument */
    std::uint16_t type = 0;            /* +12, equals the argument */
    std::int32_t lag_ticks = 0;        /* +14, equals the argument */
    std::uint16_t lag_format = 0;      /* +18, kLinkLagFormatWorkingDays */
    std::size_t row = 0;               /* FixedData row index (rows() - 1) */
    std::vector<std::string> warnings;
};

/* What `delete_task_link` proved from the file on disk.  The row itself is still
 * there (a tombstone never shrinks a stream); only its FixedMeta state changed. */
struct DeletedLink {
    std::uint32_t uid = 0;
    std::uint32_t predecessor_uid = 0;
    std::uint32_t successor_uid = 0;
    std::uint16_t type = 0;
    std::int32_t lag_ticks = 0;
    std::uint16_t lag_format = 0;
    std::size_t row = 0; /* the now-tombstoned FixedData row */
    std::vector<std::string> warnings;
};

/*
 * Append one link row -- `114/TBkndCons/FixedData` + `FixedMeta` and the
 * link table's own `Fixed2Data` + `Fixed2Meta` -- write the package to `path`,
 * re-open it and prove every fact from the bytes on disk.
 *
 * - Link uid = largest uid in the whole link table (tombstones included) + 1,
 *   so a uid is never reused.  A table with no rows gets uid 1.
 * - FixedMeta entry: +0 = 0 live, +2 = 0 (the measured per-row count; the link
 *   table has no variable fields), +4 = the new row's byte offset, +8/+9 =
 *   the measured tail bytes 0xdd 0x00.  Header +8 (entry count) + 1 and header
 *   +12 = the new FixedData byte size.  Header +4 keeps its value (the
 *   "contains deleted rows" bit is only ever added by a delete).
 * - Fixed2Data gains 48 bytes: [link identity, predecessor task identity,
 *   successor task identity].  The endpoint identities are copied from the two
 *   task rows' Task/Fixed2Data identities; the link identity is deterministic
 *   and unique (see the identity paragraph above).  Fixed2Meta gains one 9-byte
 *   entry (+4 = the new record offset, +8 = 0x0f/0x07), and its header +8 count
 *   and +12 size are bumped.
 * - Only those four TBkndCons streams change; every other stream (Task, Assn,
 *   the link table's VarMeta/Var2Data, 214/) is required to be byte-identical.
 *   No VarMeta/Var2Data row is written.
 *
 * Refusals (exception before `doc` or the output path is touched):
 *   - an empty/unusable output path;
 *   - predecessor or successor uid 0, equal uids (a self-link), or a uid with no
 *     LIVE task row (fixed-meta entry tombstoned/missing or no VarMeta group);
 *   - a task row whose Task/Fixed2Data identity is all zero (no measured
 *     identity to copy into the link record);
 *   - a link type outside 0..3;
 *   - |lag_ticks| > kMaxLinkLagTicks;
 *   - a live link row already carrying the same (predecessor, successor, type)
 *     -- the lag is not part of the identity, so a second FS 1->2 with a
 *     different lag is the same relation and is refused.  A tombstoned row with
 *     that triple does not block a re-add;
 *   - any live link row already naming the successor (a second incoming link)
 *     -- Wave 29 falsified the first/later reading of Fixed2Meta +8 and no pair
 *     adds a second incoming link (W30-0.7);
 *   - a successor whose stored percent is non-zero -- no pair adds a link into a
 *     progressed successor (W30-0.7);
 *   - a damaged link table: FixedData not 20*n, FixedMeta not 16 + 10*n,
 *     Fixed2Data not 48*n, Fixed2Meta not 16 + 9*n, a declared entry count
 *     (+8) that disagrees with the entries present, a declared size (+12) that
 *     disagrees, a row with zero or more than one FixedMeta entry, an entry
 *     whose +4 word is not a whole row offset, or an unknown entry state byte.
 */
AddedLink add_task_link(CfbDocument& doc, std::uint32_t predecessor_uid,
                        std::uint32_t successor_uid, std::uint16_t type,
                        std::int32_t lag_ticks, std::string const& path,
                        std::uint16_t lag_format = kLinkLagFormatWorkingDays);

/*
 * Tombstone the link row whose uid is `link_uid`: its FixedMeta entry
 * `+0 = 0x02`, `+2 = 0`, and `TBkndCons/FixedMeta` header `+4 |= 0x02` (the
 * measured "this table contains deleted rows" gate -- the same contract as a
 * task delete).  No stream changes size and the FixedData row keeps every byte,
 * so `core/mpp` reads one fewer LIVE link.
 *
 * Refusals: an empty/unusable output path; a damaged link table (see
 * add_task_link); no row with that uid; a row already tombstoned.
 */
DeletedLink delete_task_link(CfbDocument& doc, std::uint32_t link_uid,
                             std::string const& path);

/* --- link Fixed2Meta driving-state byte, measured through a percent edit --- */

/* One applied edit, as the link-flag rule needs it. */
struct ProgressLinkEdit {
    std::uint32_t uid = 0;
    /* True when the edit takes the row from stored percent 0 to non-zero (the
     * wave-29 measured transition). */
    bool entered_progress = false;
};

struct ProgressLinkFlag {
    std::uint32_t successor_uid = 0;
    std::size_t live_incoming = 0; /* live link rows whose successor is the uid */
    std::uint8_t before = 0;       /* Fixed2Meta entry +8 of the one live link */
    std::uint8_t after = 0;
    bool written = false; /* the measured edit value 0x07 is now in effect */
    bool changed = false; /* a byte actually changed (0x0f -> 0x07) */
};

struct ProgressLinks {
    std::vector<ProgressLinkFlag> flags; /* one per edit, in input order */
    /* The new 114/TBkndCons/Fixed2Meta bytes when any flag changed; empty when
     * no stream needs replacing.  The caller stores it. */
    std::vector<std::uint8_t> fixed2_meta;
};

/*
 * Stage the link table's measured edit effect on Fixed2Meta +8 (W29-1.0b).
 *
 * Project's wave29-percent-work40 save takes the one LIVE incoming link to the
 * edited row from 0x0f to 0x07 when that row enters progress.  This function
 * reads `114/TBkndCons` from `doc`, applies that rule to a copy of
 * `114/TBkndCons/Fixed2Meta`, and returns the bytes to store; `doc` is not
 * modified.  It is called before the actuals writer stores anything, so a
 * refusal leaves the file untouched.
 *
 * Rules:
 *   - no live incoming link: no-op (every other measured pair);
 *   - exactly one live incoming link and the edit enters progress: require the
 *     entry's +8 byte to be 0x0f or 0x07 and stage 0x07;
 *   - more than one live incoming link, an edit on a linked row that does NOT
 *     enter progress, or any other +8 value: no Project save measures the
 *     shape, so throw Error and let the caller refuse the command.
 */
ProgressLinks stage_progress_link_flags(
    CfbDocument const& doc, std::vector<ProgressLinkEdit> const& edits,
    std::string_view table = "114/TBkndCons");

/* --- resource rows: 114/TBkndRsc (W54) ---------------------------------- */

/*
 * A resource row is 172 bytes after a 48-byte header (spec/mpp-records.md §9,
 * core/mpp/reader.h "RESOURCES"):
 *
 *   +0   u32     ID (equals the UniqueID in every corpus file)
 *   +4   u32     UniqueID
 *   +8   f64     MaxUnits x100 (10000.0 = 100 %; w9-resourcetypes' 50 % stores
 *                5000.0)
 *   +80  u32     AccrueAt (3 = prorated, the Project default)
 *   +154 u16     per-row counter (unmapped; Project writes last row + 1)
 *
 * The row's Type is NOT a row byte.  It is a metadata bit pair (W49's measured
 * rule): Work <=> FixedMeta entry byte +12 bit 0x10; when that is clear, Cost
 * <=> Fixed2Meta entry byte +8 bit 0x10 and Material <=> both clear.
 *
 * The table also has the rows+3 padding the Task table has, with its own
 * strides (measured on synthetic-base.mpp, corpus/probe/assign1.mpp,
 * corpus/probe/w9-resourcetypes.mpp and the coordinator's
 * corpus/probe/w12-assignadd.mpp):
 *
 *   FixedData   48 + 172 * rows
 *   FixedMeta   16 + 37 * (rows + 3)   entries: +0 state, +2 field count,
 *                                      +4 row offset, +12 type flags;
 *                                      the first three entries are descriptors
 *   Fixed2Data  40 * (rows + 3)        records: 16-byte identity, f64 row + 1,
 *                                      the same identity again
 *   Fixed2Meta  16 + 51 * (rows + 3)   entries: +4 record offset (entry i ->
 *                                      record i), +8 type flags
 *   VarMeta     24 + 12 * n            (uid, Var2Data offset, field key)
 *   Var2Data    concatenation of u32-length-prefixed blocks
 *
 * Project's own add of one Work resource (w12-assignadd.mpp, from
 * tests/data/synthetic-base.mpp) grew exactly
 *
 *   FixedData +172, FixedMeta +37, Fixed2Data +40, Fixed2Meta +51,
 *   VarMeta +5 entries, Var2Data +54 bytes
 *
 * and wrote five variable entries for the new uid, in this order and with these
 * measured meanings:
 *
 *   0x0c400001 Name       UTF-16LE + NUL          (w12: "R1")
 *   0x0c400002 Initials   UTF-16LE + NUL          (w12: "R")
 *   0x0c400055 u32 2      a small integer (cost-rate table / calendar index?)
 *   0x0c4002d6 4 bytes    a packed date (a save/creation stamp)
 *   0x0c4002e3 16 bytes   all zero in every measured file
 *
 * The new row's FixedData was a byte-for-byte copy of the last live resource row
 * with only ID, UniqueID and the +154 counter changed (MaxUnits stayed 100 %).
 * `add_resource_row` does the same: it clones the last live row (row 0 when the
 * table has no live resource), then writes ID, UniqueID, MaxUnits and the type
 * bits.  For 0x0c400055 / 0x0c4002d6 / 0x0c4002e3 the payloads are copied from
 * the same file (template row's block, else the first block with that key, else
 * the measured defaults 2 / 0xffffffff / 16 zero bytes); this keeps the file's
 * own conventions instead of inventing values.  Name and Initials come from the
 * caller; an empty `initials` takes the first character of the name (Project's
 * own "R1" -> "R").
 *
 * INFERRED, NOT MEASURED: that Project accepts a deterministic Fixed2Data
 * identity for a resource (the writer uses the same generator as
 * `add_task_link`: an existing file identity family with the lowest unused
 * counter byte), and that a non-100 % MaxUnits is accepted with the cloned row
 * (only 100 % is measured on an add).  The Project gate is the proof.
 */
enum class ResourceKind : std::uint8_t {
    work = 0,     /* PjResourceType Work: FixedMeta +12 bit 0x10 set */
    material = 1, /* both type bits clear */
    cost = 2,     /* Fixed2Meta +8 bit 0x10 set, FixedMeta +12 bit 0x10 clear */
};

struct ResourceSpec {
    /* Required, non-empty.  UTF-16LE + NUL under key 0x0c400001. */
    std::string name;
    /* Empty = the first character of `name`. */
    std::string initials;
    /* MaxUnits in percent: 100.0 is 100 % (the measured Project default and the
     * only value measured on a Project add).  Must be finite and in (0, 10000]. */
    double max_units_percent = 100.0;
    ResourceKind kind = ResourceKind::work;
};

/* What `add_resource_row` proved from the file on disk. */
struct AddedResource {
    std::uint32_t uid = 0; /* FixedData +4 */
    std::uint32_t id = 0;  /* FixedData +0 */
    std::size_t row = 0;   /* FixedData row index (rows() - 1 after the call) */
    std::string name;
    std::string initials;
    double max_units_percent = 0.0;
    ResourceKind kind = ResourceKind::work;
    std::vector<std::string> warnings;
};

/*
 * Append one resource row (the six TBkndRsc streams above), write the package to
 * `path`, re-open it and prove every fact from the bytes on disk.  Only those
 * six streams change; every other stream, 214/ included, is required to be
 * byte-identical.
 *
 * Refusals (exception before `doc` or the output path is touched):
 *   - an empty/unusable output path; an empty name; a MaxUnits that is not
 *     finite or outside (0, 10000];
 *   - a table whose geometry is broken: FixedData not 48 + 172*n, FixedMeta not
 *     16 + 37*(n+3), Fixed2Data not 40*(n+3), Fixed2Meta not 16 + 51*(n+3),
 *     VarMeta not 24 + 12*k, a header count/size word that disagrees with the
 *     streams, a FixedMeta/Fixed2Meta entry that does not name a whole record,
 *     or a missing stream.
 */
AddedResource add_resource_row(CfbDocument& doc, ResourceSpec const& resource,
                               std::string const& path);

/* --- assignment rows: 114/TBkndAssn (W54) -------------------------------- */

/*
 * An assignment row is 110 bytes with NO header (spec/mpp-records.md §10.2):
 *
 *   +0   u32     assignment UniqueID
 *   +4   u32     task UniqueID
 *   +8   u32     resource UniqueID (0xFFFF0001 is the null-resource sentinel of
 *                an unassigned placeholder row)
 *   +12  f64     Units x100  (10000.0 = 100 %)
 *   +20  f64     Work, minutes x1000 (2400000.0 = 2400 min = 5 days)
 *   +28  f64     zero in every measured fresh row (unmapped)
 *   +36  f64     the same value as +20 in every measured row (a work copy)
 *   +44  f64     the same value as +20 in every measured row (a work copy)
 *   +52  u32     Start (packed date; the task's effective +104)
 *   +56  u32     Finish (packed date; the task's effective +108)
 *   +60  u32     Resume (the measured default is Start)
 *   +92  u32     7 in every measured live row
 *   +104 u32     a second Start copy: assign1.mpp's real rows and
 *                w12-assignadd.mpp's added row all repeat Start here
 *
 * FixedMeta = 16 + 34 * rows (one entry per row, no descriptors):
 *   +0 state (0 live / 0x02 tombstone), +2 variable-field count,
 *   +4 row offset (= row * 110), +8..+15 and +16..+33 = Project's per-row
 *   cached data.  A fresh Project add wrote the entry bytes
 *   `00 00 04 00 <off u32> f1 ff 23 f0 69 00 02 00` followed by zeros; the
 *   bytes after +8 are coalesced/unmapped, so `add_assignment_row` writes that
 *   measured fresh-add template verbatim (only +2 and +4 are computed).
 *
 * Fixed2Data = 48 * rows: three 16-byte identities per row
 *   [assignment identity, task identity, resource identity].  The task identity
 *   is Task/Fixed2Data record (task row + 3); the resource identity is
 *   Rsc/Fixed2Data record (resource row + 3); the assignment identity is
 *   generated deterministically (same helper as a link identity).
 *
 * Fixed2Meta = 16 + 53 * rows: entry +4 = record offset (= row * 48), +8/+9 =
 *   0xcf/0x09 on a fresh Project add, +36 byte = 0x10, +52 = 0x28 (a fresh real
 *   row) -- again written as the measured fresh-add template.
 *
 * VarMeta/Var2Data: Project's own add wrote FOUR entries for the new uid, in
 * this order (w12-assignadd.mpp):
 *
 *   0x0f400031  planned-work contour step list, flat = 72 bytes.  Measured
 *               contents: u32 0x00200000, u32 0x2c, f64 160000.0, f64 work
 *               (ticks x100, the same scale as FixedData +20), u32 work_ticks*8,
 *               u32 0, u32 1, then zeros.  Reproduced from that measured shape;
 *               the constant 160000.0 was 100 % units in every measured row.
 *   0x0f40027a  Created (4 bytes; copied from the template assignment row)
 *   0x0f400199  16 zero bytes
 *   0x0f40019b  16 zero bytes
 *
 * The assignment table is append-only in this implementation: a new row's uid
 * must be greater than every uid in the table (tombstones included), which also
 * keeps the VarMeta array's measured uid grouping intact.
 *
 * This is what Project's own `Task.Assignments.Add` did to
 * tests/data/synthetic-base.mpp (measured diff, w12-assignadd.mpp): it appended
 * the row (uid 4, row 3) rather than reusing a slot, tombstoned the live
 * null-resource placeholder row of the SAME task (entry +0 00 -> 02), gave the
 * other two surviving rows a third variable entry, and set the FixedMeta header
 * +4 to 6 ("contains deleted rows").  The placeholder tombstone and the extra
 * variable entry on OTHER rows are Project's model churn: `add_assignment_row`
 * appends the new row and leaves every existing row byte-identical, so a file
 * that had an unassigned placeholder for the task keeps it.  `core/mpp` reports
 * the placeholder as an assignment with no resource, which is what it is.
 */
struct AssignmentSpec {
    /* Required: a live task row's UniqueID. */
    std::uint32_t task_uid = 0;
    /* Required: a live resource row's UniqueID. */
    std::uint32_t resource_uid = 0;
    /* 0 = allocate (largest assignment uid in the table, tombstones included,
     * + 1).  A non-zero value must be greater than every existing uid; the
     * assignment table is append-only here. */
    std::uint32_t uid = 0;
    /* Units in percent: 0 = inherit (100.0).  Must be finite and in (0, 10000]. */
    double units_percent = 0.0;
    /* Work in 6-second ticks.  0 = infer: the task's stored duration (+84) times
     * units_percent / 100, rounded.  FixedData +20 and +44 are written as
     * work_ticks * 100 (minutes x1000). */
    std::uint32_t work_ticks = 0;
    /* Packed dates.  kInvalidDate = inherit: Start/Finish from the task's
     * effective pair (+104/+108) and Resume = Start. */
    std::uint32_t start = kInvalidDate;
    std::uint32_t finish = kInvalidDate;
    std::uint32_t resume = kInvalidDate;
};

/* What `add_assignment_row` proved from the file on disk. */
struct AddedAssignment {
    std::uint32_t uid = 0;
    std::uint32_t task_uid = 0;
    std::uint32_t resource_uid = 0;
    std::size_t row = 0;
    double units_percent = 0.0;
    std::uint32_t work_ticks = 0;
    std::uint32_t start = kInvalidDate;
    std::uint32_t finish = kInvalidDate;
    std::uint32_t resume = kInvalidDate;
    std::vector<std::string> warnings;
};

/* What `delete_assignment_row` proved from the file on disk.  The row is still
 * there (a tombstone never shrinks a stream); only its FixedMeta state changed. */
struct DeletedAssignment {
    std::uint32_t uid = 0;
    std::uint32_t task_uid = 0;
    std::uint32_t resource_uid = 0;
    double units_percent = 0.0;
    std::uint32_t work_ticks = 0;
    std::size_t row = 0; /* the now-tombstoned FixedData row */
    std::vector<std::string> warnings;
};

/*
 * Append one assignment row (FixedData +110, FixedMeta +34, Fixed2Data +48,
 * Fixed2Meta +53, VarMeta +4 entries, Var2Data + the four blocks), write the
 * package to `path`, re-open it and prove every fact from the bytes on disk.
 * Only those six TBkndAssn streams change; every other stream, 214/ included,
 * is required to be byte-identical.
 *
 * Refusals (exception before `doc` or the output path is touched):
 *   - an empty/unusable output path;
 *   - task uid 0 or no live task row; resource uid 0 or no live resource row;
 *   - a live assignment already naming the same (task, resource) -- Project
 *     stores one row per resource per task;
 *   - an explicit uid that is 0xffffffff or not greater than the largest uid in
 *     the assignment table;
 *   - units_percent outside (0, 10000] or a non-finite value; an inferred work
 *     of 0 from a task with no stored duration is allowed (0 work) but a caller
 *     work_ticks that is not a whole number of ticks cannot happen (u32);
 *   - a damaged assignment table (see the geometry above), a missing
 *     Task/Fixed2Data or Rsc/Fixed2Data identity for an endpoint, or a
 *     Fixed2Data endpoint identity that is all zero;
 *   - `start`/`finish`/`resume` that are not valid packed dates (0 or
 *     0xffffffff) when supplied, or a start after the finish.
 */
AddedAssignment add_assignment_row(CfbDocument& doc, AssignmentSpec const& assignment,
                                   std::string const& path);

/*
 * Tombstone the assignment row whose uid is `uid`: its FixedMeta entry
 * `+0 = 0x02`, `+2 = 0`, and `TBkndAssn/FixedMeta` header `+4 |= 0x02` (the
 * measured "this table contains deleted rows" contract -- the same one W48's
 * task-delete cascade writes).  No stream changes size; FixedData, Fixed2Data,
 * Fixed2Meta, VarMeta and Var2Data keep every byte, and `core/mpp` then reports
 * one fewer live assignment.
 *
 * Refusals: an empty/unusable output path; a damaged assignment table; no row
 * with that uid; a row with no FixedMeta entry, an entry already tombstoned, a
 * placeholder row (task uid 0) or a row naming a task uid that no live task
 * holds (Project would not show it as an assignment either).
 */
DeletedAssignment delete_assignment_row(CfbDocument& doc, std::uint32_t uid,
                                        std::string const& path);

} /* namespace mppwrite */

#endif /* MPPWRITE_ADD_DELETE_H */
