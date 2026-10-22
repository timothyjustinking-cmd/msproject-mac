/*
 * field_values.h -- write Task custom-field values, prove them from disk.
 * W48 (2026-10-22).
 *
 * WHY THIS EXISTS.  `row_writer.h` writes the fixed Task row, `add_delete.h`
 * changes the table's rows, and `table.h` can replace one existing VarMeta
 * block -- but nothing could create a custom-field value.  The wave-10 oracle
 * measured the storage (`docs/analysis/oracle/w10-oracle.md` §2,
 * `corpus/probe/w10-fieldvalues.mpp`), so this file implements exactly those
 * encodings and refuses everything else:
 *
 *   Text      VarMeta key Text<n>; payload = UTF-16LE text + NUL.
 *             Text1 = 0x0b400033, blocks measured 12 B for "alpha".
 *   Number    VarMeta key Number<n>; payload = 8-byte little-endian f64.
 *             Number1 = 0x0b400057 (42.5), Number2 = 0x0b400058 (3.25).
 *   Date      VarMeta key Date<n>; payload = 4-byte packed date (the same
 *             (day<<16)|ticks word as Task +66/+104).  Date1 = 0x0b400109,
 *             measured 0x3d1812c0 = 2026-10-26 08:00.
 *   Duration  VarMeta key Duration<n>; payload = 4-byte u32 FILE ticks.
 *             Duration1 = 0x0b400067, measured 4800 = one 8-hour day.  COM's
 *             Task.Duration1 reports 480 minutes for that value, i.e. file
 *             ticks = COM minutes * 10; the caller supplies ticks and can use
 *             `kTaskTicksPerMinute`.
 *   Flag      NOT VarMeta: bit 0x02 of byte +24 of the row's Task FixedMeta
 *             entry.  Only Flag1 (0x0b400048) is measured; Flag2..Flag20's bit
 *             positions are unknown and refused.  `flag = true` sets the bit,
 *             `false` clears it (Project's own False file has the whole tail
 *             byte zero).
 *   Cost      ID range known (Cost1 0x0b40006a .. Cost10 0x0b400108) but
 *             storage is UNMEASURED: no oracle fixture stores a cost.  Refused
 *             with that reason; not guessed.
 *
 * REGISTRATION IN 114/TBkndTask/Props (measured, W48 Project gate).  A VarMeta
 * block is necessary but NOT sufficient: Project only displays a custom field
 * whose definition is also registered in the Task table's Props stream (and
 * mirrored in 114/TBkndAssn/Props).  Project's own w10-fieldvalues.mpp has
 * Task/Props = an 86-byte preamble + one 88-byte record per used field, and
 * Assn/Props = 88 + the same records keyed by assignment property id;
 * synthetic-base.mpp has the preambles only.  `save_field_values` inserts the
 * measured record for every distinct field it writes (a registered field is left
 * alone) and updates each table's five words.  Task: u32 +0/+4 = size-4, u16 +30
 * = 44+88n, u32 +78 = count, u32 +82 = count*88.  Assignment: u32 +0/+4 =
 * size-4, u32 +32 = 44+88n, u32 +80 = count, u32 +84 = count*88.  The records
 * are copied byte-for-byte from Project's synthetic fixture; the writer has
 * templates for exactly SIX fields -- Text1, Flag1, Number1, Number2, Duration1
 * and Date1 -- and refuses every other field with "no measured registration
 * record", because the record encodes the field's format and a template from
 * another family would be a guess.
 *
 * FIXEDMETA TAIL PRESENCE BITS + DURATION MARKER (measured, W48 third Project
 * pass on w11-fields-base.mpp and the single-field probes).  A third store is
 * needed: the 47-byte FixedMeta entry's tail says the field has a value, and a
 * Duration field also brings an internal VarMeta marker.  Without these Project
 * shows the value as empty with VarMeta and Props byte-perfect.
 *
 *     Text1     entry +21 |= 0x20
 *     Flag1     entry +24 |= 0x02   (also the bool itself; False clears it)
 *     Number1   entry +25 |= 0x08
 *     Number2   entry +25 |= 0x10
 *     Duration1 entry +26 |= 0x01, +27 |= 0x20, plus VarMeta key 0x0b4000b7
 *               (2-byte payload 07 00, inserted in ascending-key order after
 *               the overtime entries)
 *     Date1     entry +29 |= 0x40
 *
 * The entry's `+2` u16 is the row's exact VarMeta entry count INCLUDING the
 * duration marker (base 11 -> 17 for the six fields).  The writer recomputes it
 * from the array after every write, so a rewrite is a no-op there.
 *
 * The field ids are the PjTask ids in `analysis/field_id_map.tsv`, same ranges
 * as core/mpp/field_value.h (the reader side).  `field_kind_for_id` and
 * `field_id_for` below are the writer's copy of that table because core/mppwrite
 * deliberately does not link the reader; a mismatch would be caught by
 * tests/roundtrip_field_values.cpp, which checks both on the same ids.
 *
 * WHAT A WRITE DOES.
 *   - text/number/date/duration: append the payload as a new Var2Data block (the
 *     old block, if any, is left behind as unreferenced bytes -- the same
 *     append-only scheme table.h uses) and repoint every VarMeta entry for that
 *     (uid, field id), including the 0x80000000|uid family.
 *     * No entry existed: INSERT one 12-byte (uid, offset, key) entry inside the
 *       uid's contiguous group, in field-id order (`table.h`'s
 *       `insert_var_entry`), and increment that row's FixedMeta +2 field count
 *       and VarMeta's header +8 declared count.  Inserting into the group is not
 *       cosmetic: MEASURED (W48 Project gate, 2026-10-22) Project reopens a file
 *       whose per-uid entries are contiguous with the values, and reports empty
 *       values when the new entries are appended after other uids' groups even
 *       though their blocks and entries are self-consistent.
 *     * An entry existed: the entry count and the row's +2 count stay put; only
 *       the entry's +4 offset moves.  (Measured shape: w10-fieldvalues.mpp's uid
 *       1 has 14 entries and its FixedMeta +2 word is 14.)
 *   - flag: set/clear the FixedMeta entry's +24 bit 0x02 AND mirror Project's own
 *     Fixed2Meta write: bit 0x80 of that entry's +31 and +61 bytes (measured by
 *     `oracle_w10.py flagdiff`, w10-flag-on vs w10-flag-off: 0x00 -> 0x80 in both
 *     bytes when Flag1 became True).  DECISION: we mirror it because it is
 *     Project's own measured write shape, but only the Task FixedMeta bit is
 *     read back by core/mpp -- whether Project needs the Fixed2Meta mirror on a
 *     fresh open is UNMEASURED.  If the row has no Fixed2Meta entry the mirror is
 *     skipped and the write is warned about; the primary store is still written
 *     and verified.  No VarMeta entry or field count changes for a flag.
 *
 * ALL-OR-NOTHING AND PROVEN FROM DISK.  Every edit is validated against the
 * current document before the output path is touched (the usual policy: uid 0,
 * unknown/dead rows, duplicate edits, wrong kind, unmeasured field, invalid
 * value).  The edits are applied to a COPY of the document; then the package is
 * written, re-opened from disk and required to:
 *   - hold exactly the bytes that were built, re-emit byte-identically, pass
 *     `CfbDocument::check()` and `check_table()`;
 *   - keep FixedData and Fixed2Data byte-identical;
 *   - change only the intended bytes: the edited FixedMeta entry words (the tail
 *     presence bits and the `+2` entry count), the mirrored Fixed2Meta bytes, the
 *     repointed/inserted VarMeta entries (including the duration marker) and
 *     header counts, the appended Var2Data blocks, and the added 88-byte Props
 *     records plus each table's five words (FixedMeta/Fixed2Meta/Var2Data
 *     byte-by-byte against an allow-list; VarMeta by ENTRY, because inserting
 *     inside a uid group shifts every later entry by 12 -- the check requires the
 *     same entry multiset, only the edited offsets moved, and the array to be
 *     uid-grouped with ascending field ids, which is the property Project
 *     requires; Props by record, requiring every pre-existing definition
 *     byte-identical);
 *   - read every edited value back with the measured encoding
 *     (`FieldValue::present`, `storage_measured`, and the value equal to what
 *     was requested), and every tail bit / `+2` count back from the fixed row;
 * A refusal leaves `doc` and any pre-existing output file untouched.  On success
 * `doc` is replaced by the re-read document.
 *
 * WARNINGS (never a silent success): the Fixed2Meta mirror skipped for a row
 * with no Fixed2Meta entry, a "registered N field definition(s)" receipt when
 * Props grew, and one informational "re-read from disk" receipt per call.
 *
 * WHAT THIS FILE DOES NOT DO.  It does not remove a field (clearing Text to ""
 * stores an empty string; there is no measured delete shape), does not create
 * the row itself (add_delete.h), does not write formulas (`114/Props`, wave 12),
 * does not write `214/`, and does not recalculate anything.
 */

#ifndef MPPWRITE_FIELD_VALUES_H
#define MPPWRITE_FIELD_VALUES_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "table.h"

namespace mppwrite {

/* Project's PjTask custom-field families, named as COM names them.  `unknown`
 * is every id outside the ranges below. */
enum class FieldKind : std::uint8_t {
    text = 0,
    number = 1,
    duration = 2,
    date = 3,
    flag = 4,
    cost = 5,
    unknown = 6,
};

/* One 8-hour working day in file ticks (table.h); COM minutes * 10. */
constexpr std::uint32_t kTaskTicksPerMinute = 10;

/* The first id of each family (all in the 0x0b40xxxx task field space).  The
 * full ranges are in field_id_for()/analysis/field_id_map.tsv. */
constexpr std::uint32_t kText1FieldId = 0x0B400033u;
constexpr std::uint32_t kFlag1FieldId = 0x0B400048u;
constexpr std::uint32_t kNumber1FieldId = 0x0B400057u;
constexpr std::uint32_t kDuration1FieldId = 0x0B400067u;
constexpr std::uint32_t kCost1FieldId = 0x0B40006Au;
constexpr std::uint32_t kDate1FieldId = 0x0B400109u;

/* The id of `kind` index `index` (1-based), or 0 when the index is out of range
 * or the kind has no id space.  Same arithmetic as core/mpp/field_value.h. */
std::uint32_t field_id_for(FieldKind kind, unsigned index);
/* The kind an id belongs to, or FieldKind::unknown. */
FieldKind field_kind_for_id(std::uint32_t field_id);
/* The 1-based index of an id inside its family, or 0. */
unsigned field_index_for_id(std::uint32_t field_id);
/* True when the file storage of this field is measured (all but Cost and
 * Flag2..Flag20, whose bit positions are unknown). */
bool field_storage_measured(FieldKind kind, unsigned index);

/*
 * One field value.  `field_id` selects both the target and the family; `kind`
 * must agree with it (a caller that only has an id can use `field_kind_for_id`).
 * On INPUT only the member for that kind is read.  On read-back (`FieldWrite`)
 * `present`/`storage_measured` say whether the file holds a decodable value, so
 * "not stored" is distinguishable from "stored as 0"; a caller-supplied value
 * must not rely on them.
 */
struct FieldValue {
    std::uint32_t field_id = 0;
    FieldKind kind = FieldKind::unknown;
    std::string text;                 /* text, UTF-8, no embedded NUL */
    double number = 0.0;              /* number, finite */
    std::uint32_t packed_date = 0;    /* date, a valid packed word (table.h) */
    std::uint32_t duration_ticks = 0; /* duration, file ticks (4800 = 1 day) */
    bool flag = false;                /* flag */
    bool present = false;             /* read-back: the file stores the field */
    bool storage_measured = false;    /* read-back: the value above is decoded */
};

/* One row's field edit. */
struct FieldEdit {
    std::uint32_t uid = 0; /* task UniqueID (FixedData +4), not the row index */
    FieldValue value;
};

/* What the file on disk holds for one edit, read back through the same
 * encodings.  `value.present` is true for every successful edit. */
struct FieldWrite {
    std::uint32_t uid = 0;
    FieldValue value;
};

struct FieldWriteResult {
    std::size_t fields_written = 0;
    std::vector<FieldWrite> fields; /* one per edit, in edit order */
    std::vector<std::string> warnings;
};

/*
 * Validate `edits`, apply them, write the package to `path`, re-open it and prove
 * every fact above from the bytes.  Throws Error on any refusal or mismatch; then
 * neither `doc` nor a new output file has been left behind.  The table path
 * defaults to the Task table.
 *
 * Refusals (message names the uid / field / value):
 *   - an empty edit list, or an output path that is empty / a directory / has no
 *     parent directory;
 *   - a uid of 0 (the project row), a uid that has no live task row (no FixedMeta
 *     entry, a tombstone, or no VarMeta group), or a uid that appears twice for
 *     the same field;
 *   - a field id outside the measured families (Cost, Flag2..Flag20 and every
 *     unknown id), a field without one of the six measured
 *     `114/TBkndTask/Props` registration records (Text1, Flag1, Number1,
 *     Number2, Duration1, Date1), or a `kind` that does not match the id;
 *   - text with an embedded NUL or longer than 65,535 bytes, a non-finite
 *     number, an invalid packed date, or duration_ticks == 0xffffffff;
 *   - a task table whose size identity is broken (`check_table`, the FixedMeta /
 *     VarMeta geometry checks), or a Props stream that is missing or does not
 *     carry the measured 86-byte preamble and size words.
 */
FieldWriteResult save_field_values(CfbDocument& doc,
                                   std::vector<FieldEdit> const& edits,
                                   std::string const& path,
                                   std::string_view table = "114/TBkndTask");

/* What `clear_field_value` proved from the file on disk. */
struct ClearedField {
    std::uint32_t uid = 0;
    std::uint32_t field_id = 0;
    FieldKind kind = FieldKind::unknown;
    bool was_present = false; /* the field had a value before the call */
    bool entry_kept = false;  /* a VarMeta entry now points at the empty block */
    bool flag = false;        /* Flag1 after the call (always false) */
    FieldValue value;         /* exactly what the file now holds (the empty value) */
    std::vector<std::string> warnings;
};

/*
 * Store the measured EMPTY value for one custom field of one task, write the
 * package to `path`, re-open it and prove the result from the bytes on disk.
 * W52 (2026-10-22).
 *
 * WHAT "CLEAR" MEANS HERE.  Project has no measured field-delete encoding, so
 * this is the empty-VALUE shape, not entry removal:
 *
 *   Text      VarMeta block = empty UTF-16LE string + NUL (2 bytes)
 *   Number    VarMeta block = 8-byte little-endian f64 0.0
 *   Duration  VarMeta block = u32 0 file ticks
 *   Date      VarMeta block = u32 0xffffffff (table.h's kInvalidDate, Project's
 *             "no date"; the same word an ASAP constraint stores)
 *   Flag1     Task FixedMeta entry byte +24 bit 0x02 cleared, plus the measured
 *             Fixed2Meta mirror (+31/+61 bit 0x80) cleared
 *
 * and the field's FixedMeta TAIL PRESENCE bits are CLEARED.  Those tail bits are
 * what make Project display a value at all (measured, W48 third gate), so a
 * cleared field reads as blank even though its VarMeta entry still exists.
 *
 * The VarMeta entry is KEPT and repointed at the empty block (a new block is
 * appended only when the stored value was not already empty).  FixedMeta +2 is
 * therefore the row's VarMeta entry count, unchanged, and
 * `114/TBkndTask/Props` / `114/TBkndAssn/Props` are not rewritten: the field
 * stays registered exactly as Project keeps it.  Removing the entry (and
 * decrementing +2) is deliberately NOT implemented; there is no measured
 * encoding for it.
 *
 * A field that was never stored gets no new VarMeta entry (clearing an absent
 * field must not register one); only its presence bits are cleared.  Clearing a
 * field that is already empty is a byte-identical no-op (the stream is written
 * and re-read anyway, so the all-or-nothing proof still runs).
 *
 * Refusals (an exception leaves `doc` and any pre-existing output file
 * untouched):
 *   - an empty/unusable output path;
 *   - a uid of 0 or a uid with no live task row (no FixedMeta entry, a
 *     tombstone, or no VarMeta group);
 *   - a field outside the six measured ids (Text1, Flag1, Number1, Number2,
 *     Duration1, Date1): Cost, Flag2..Flag20, unknown ids, and every field whose
 *     FixedMeta tail-presence bit is not measured are refused rather than
 *     guessed;
 *   - a task table whose size identity is broken (`check_table` and the
 *     FixedMeta/VarMeta geometry checks).
 */
ClearedField clear_field_value(CfbDocument& doc, std::uint32_t uid,
                               std::uint32_t field_id, std::string const& path,
                               std::string_view table = "114/TBkndTask");

} /* namespace mppwrite */

#endif /* MPPWRITE_FIELD_VALUES_H */
