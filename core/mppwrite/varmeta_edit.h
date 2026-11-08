/*
 * varmeta_edit.h -- one Var2Data record of one row: append, re-point, remove.
 * W34-1.1 (30 October 2026).
 *
 * WHY THIS EXISTS.  table.h could already replace an existing block
 * (`replace_var_block`) and the outline/actuals writers edit var entries as a
 * side effect of their own commands, but nothing owned the var store as a
 * command of its own: add one record, re-point one record, or remove one
 * record, for one row, with the bookkeeping Project's own saves keep.  This is
 * that writer.  It is the first consumer path for `rename_resource` (W34-1.3)
 * and the basis of the wave's other field writers.
 *
 * THE MEASURED RULES (full derivation in spec/writers/var-meta.md, W34-1.1
 * section; every pair has a null save, so per-save noise is masked and only
 * the edit's effects are compared):
 *
 *   append    A new 12-byte (uid, Var2Data offset, key) entry at the position
 *             `var_entry_insert_offset` measures (inside the uid's contiguous
 *             group, ascending key), header +8 +1, the block [u32 len][payload]
 *             at the current Var2Data high-water mark, header +0x14 = new
 *             size, the row's FixedMeta +2 +1, and the caller's measured
 *             presence bits ORed.  Pair: wave30-notes (count 42 -> 43,
 *             Var2Data 516 -> 718, FixedMeta +2 11 -> 12, +14 |= 0x10,
 *             +44 |= 0x10, 114/Props Task size 516 -> 718).
 *   re-point  One existing entry for (uid, key) moves to the high-water mark
 *             after the new block is appended; the old block stays orphaned;
 *             the count, FixedMeta and the freed word do not move; +0x14 and
 *             the 114/Props size word become the new size.  Pairs:
 *             wave30-resource-rename (Resource table, uid 2 key 0x0c400001,
 *             114 -> 140, 6 -> 8 B payload, size 140 -> 152) and
 *             wave34-note-replace (Task table, 524 -> 725, 197 -> 209 B,
 *             size 725 -> 938).
 *   remove    The 12-byte entry is erased from the logical array, header +8
 *             -1, the row's FixedMeta +2 -1, the caller's presence bits
 *             AND-ed out, and the freed word gains the removed block's
 *             payload length; Var2Data is untouched.  The stream is NOT
 *             truncated: the new header + entries are written over the old
 *             bytes (`merge_logical_var_meta`, the convention outline.cpp
 *             measured), so the freed last slot keeps a byte-identical copy
 *             of the previous last entry.  Pairs: wave34-field-clear (freed
 *             0 -> 40, +21 &= ~0x20), wave34-note-clear (freed 0 -> 188,
 *             +14/+44 cleared), wave31-fill (348 B stream kept).
 *
 * `114/Props` (the DIRECTORY stream, not a per-table one) carries, per table,
 * a block of 16-byte scalar properties [LE u32 4][tag 4 bytes BIG-endian]
 * [LE u32 4][LE u32 value].  The two words a var edit moves are measured:
 *
 *   Task      size tag BE 0x01000100, freed tag BE 0x01000002
 *   Resource  size tag BE 0x02000100, freed tag BE 0x02000002
 *
 * The size word always equals the table's Var2Data length; the freed word
 * increases by the payload bytes the edit unlinks.  A missing or duplicated
 * tag is refused; the other tags in the same block are preserved.
 *
 * THE PROP14 EDIT MARKER (`edit_marker.h`) is written on every save: every
 * measured editor save sets record 0x3540000a's payload to `01 00`.
 *
 * WHAT IS *NOT* TOUCHED.  FixedData, Fixed2Data, Fixed2Meta, the per-table
 * Props registries, 214/CEdl, 214/Props, DocumentSummaryInformation and every
 * other stream.  The writer proves after saving that only
 * {table VarMeta, table Var2Data, table FixedMeta, 114/Props, Props14} moved.
 *
 * THE CAPABILITY MATRIX (brief rule 1: a command is enabled only for a shape
 * a pair measures end to end, with the null save separating noise from the
 * edit, and every other shape is refused up front):
 *
 *   table                  append              re-point (larger)   remove
 *   Task, uid != 0         enabled             enabled             enabled
 *   Resource, uid != 0     refused (no pair)   enabled             refused (no pair)
 *   Assignment             refused             refused             refused
 *   project row (uid 0)    Task key 0x0B40019F refused             refused
 *                          only (W34-4.6)
 *
 * Refusals (all before `doc` or the output path is touched; every one has a
 * test in tests/roundtrip_varmeta.cpp):
 *   - 0 or more than one edit (one record per save; multi-row is unmeasured);
 *   - an empty or unusable output path;
 *   - a table path that is not `114/TBkndTask` or `114/TBkndRsc` (Assignment
 *     and every other table have no measured shape for this writer);
 *   - uid 0 (the project row / the Resource placeholder row);
 *   - no FixedData row with the uid, more than one, or a FixedMeta entry that
 *     is missing, duplicated or not live (state byte != 0);
 *   - a damaged table geometry (the same size/count/offset identity
 *     `check_table` enforces for Task and add_delete.cpp enforces for
 *     Resource), a VarMeta that is not 24 + 12*n, a header count that is not
 *     the physical entry count (a file with a stale tail is refused, not
 *     edited blind), or a +0x14 size word that disagrees with Var2Data;
 *   - append: the row already has an entry for the key (the plain uid or the
 *     0x80000000|uid family), the table has no measured append, or the
 *     payload is empty;
 *   - re-point: no entry, two entries for the same (uid, key), a
 *     0x80000000|uid entry for the key, an offset of 0xFFFFFFFF, a
 *     zero-length old block, or a payload that is not strictly LARGER than
 *     the old one.  A shrink is measured (wave28-outline-indent/summary: in
 *     place, freed += delta) but no pair isolates it, so it is refused until
 *     such a pair exists; an equal-size payload is refused for the same
 *     reason;
 *   - remove: no entry, two entries, a family entry, a 0xFFFFFFFF offset, a
 *     zero-length block, a non-empty payload, or a table with no measured
 *     removal;
 *   - a presence operation outside the row's FixedMeta entry (byte < 8, or
 *     past the entry), a zero or multi-bit mask, or a duplicate operation;
 *   - a Fixed2Meta/Fixed2Data/Props stream the table needs to be intact.
 *
 * ALL-OR-NOTHING AND PROVEN FROM DISK.  The edit is applied to a copy of the
 * document; the package is written to `path`, re-opened and required to hold
 * exactly the bytes built, re-emit byte-identically, pass `CfbDocument::check`,
 * pass the table's geometry check, contain exactly the requested entry state
 * (offset and payload for append/re-point; absent from the logical array for
 * remove, with the stream length and stale tail kept), the recomputed
 * FixedMeta +2, the requested presence bits, the 114/Props words and the
 * Props14 marker.  On success `doc` is replaced by the re-read document.
 */

#ifndef MPPWRITE_VARMETA_EDIT_H
#define MPPWRITE_VARMETA_EDIT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"

namespace mppwrite {

/* The two tables this writer has measured var-store shapes for. */
inline constexpr char const* kVarTaskTable = "114/TBkndTask";
inline constexpr char const* kVarResourceTable = "114/TBkndRsc";

/* Resource/Name: the first consumer's key (W34-1.3). */
inline constexpr std::uint32_t kResourceNameFieldId = 0x0C400001u;

/* The two Task keys wave30-wbs-code appends (W34-4.6): each task's stored WBS
 * number text (UTF-16LE + NUL, e.g. "01") and the project row's (uid 0) last
 * issued number.  The project-row append is the one measured edit of uid 0's
 * var records. */
inline constexpr std::uint32_t kTaskWbsFieldId = 0x0B40018Du;
inline constexpr std::uint32_t kTaskProjectWbsFieldId = 0x0B40019Fu;

/* The three measured shapes. */
enum class VarRecordShape : std::uint8_t {
    append = 0,  /* a new record for a key the row does not have */
    repoint = 1, /* a strictly larger payload for an existing record */
    remove = 2,  /* the entry is removed; the block stays orphaned */
};

/* One FixedMeta presence-bit operation the CALLER measured for the field.  The
 * bit is the field's class-slot bitmap, not derivable from the var store
 * (field_values.h measures seven of them); `byte` is relative to the row's
 * FixedMeta entry. */
struct VarPresenceBit {
    std::size_t byte = 0; /* >= 8 (the structural words are +0, +2, +4) */
    std::uint8_t bit = 0; /* exactly one bit set */
    bool set = false;     /* true: OR it in; false: AND it out */
};

/* One var-store edit: one record of one row. */
struct VarRecordEdit {
    std::uint32_t uid = 0;      /* the row's FixedData +4 UniqueID */
    std::uint32_t field_id = 0; /* the VarMeta key */
    VarRecordShape shape = VarRecordShape::repoint;
    /* append/re-point: the new block payload (non-empty; stored verbatim).
     * remove: must be empty. */
    std::vector<std::uint8_t> payload;
    std::vector<VarPresenceBit> presence; /* the caller's measured fixed bits */
};

/* What the write proved from the file on disk. */
struct VarRecordWrite {
    std::uint32_t uid = 0;
    std::uint32_t field_id = 0;
    VarRecordShape shape = VarRecordShape::append;
    std::uint32_t old_offset = 0; /* re-point/remove: the old block; else 0 */
    std::uint32_t new_offset = 0; /* append/re-point: the block now; else 0 */
    std::size_t old_payload_bytes = 0;
    std::size_t new_payload_bytes = 0;
    std::size_t var2_bytes_before = 0;
    std::size_t var2_bytes_after = 0;
    std::size_t entries_before = 0; /* table-wide, declared */
    std::size_t entries_after = 0;
    std::size_t row_entries_before = 0; /* the row's FixedMeta +2 */
    std::size_t row_entries_after = 0;
    std::size_t freed_bytes_before = 0; /* the 114/Props freed word */
    std::size_t freed_bytes_after = 0;
};

/*
 * Apply the ONE edit in `edits` to `doc`, write the package to `path`,
 * re-open it and prove every fact above from the bytes.  Throws Error on a
 * refusal or a mismatch; then neither `doc` nor a new output file has been
 * left behind.  `edits.size() != 1` is refused (multi-record is unmeasured).
 *
 * `table` must be kVarTaskTable or kVarResourceTable (a stored path with the
 * OLE leading spaces in its first component is normalised first).
 */
VarRecordWrite save_var_record(CfbDocument& doc, std::string_view table,
                               std::vector<VarRecordEdit> const& edits,
                               std::string const& path);

/*
 * W34-1.3: rename one resource through the measured re-point shape.
 *
 * The name is stored UTF-16LE + NUL under key 0x0C400001 (add_delete.h's
 * measured Resource/Name encoding).  Because this writer enables only the
 * LARGER re-point, and because the renamed block is the only edit, a name
 * whose encoded payload is not strictly larger than the current one is
 * refused: Project's shorter-name shape is measured only inside the wave-28
 * outline edits and needs its own pair before it can be enabled here.
 *
 * Measured against wave30-resource-rename (uid 2, 6 -> 8 B, Var2Data 140 ->
 * 152, 114/Props Resource size 140 -> 152, every other stream unchanged
 * except the Props14 marker).
 *
 * Refusals: everything save_var_record refuses for a Resource re-point, an
 * empty name, a name with an embedded NUL, a name longer than 3 bytes (the
 * "R2b" the pair stores), or a name holding ',', '[' or ']'.
 */
VarRecordWrite rename_resource(CfbDocument& doc, std::uint32_t uid,
                               std::string_view name, std::string const& path);

} /* namespace mppwrite */

#endif /* MPPWRITE_VARMETA_EDIT_H */
