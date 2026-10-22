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
 *   Cost      VarMeta key IS the field id (Cost1 = 0x0b40006a measured,
 *             w14-cost; Cost1..Cost10 ids per `field_id_for` below).  Payload =
 *             8-byte little-endian f64 of the value in HUNDREDTHS, exactly
 *             rounded: 123.45 -> 12345.0 (w14-cost) and 0.07 -> 7.0, not
 *             7.000000000000001 (w14-cost2, coordinator measurement
 *             2026-10-22).  A Cost also has a FixedMeta tail presence bit (see
 *             the tail table below) and its own Props records.
 *
 * W60 (2026-10-22) added Cost and ASSIGNMENT custom fields.  Assignment
 * Number/Text fields do NOT live in the Task table and their VarMeta keys are
 * NOT the PjField ids: `Assignment.Number1` (PjField 0x0f40006c) stores under
 * key 0x0f40403c, `Assignment.Text1` (0x0f400058) under 0x0f404064 (measured,
 * w14-assignfield).  `assignment_property_id` below is that key table; see
 * `save_assignment_field_values` for the measured layout.
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
 * templates for SEVEN fields -- Text1, Flag1, Number1, Number2, Duration1, Date1
 * and Cost1 -- and Cost2..Cost10 reuse the measured Cost1 record with their own
 * id/key (the same shape, inferred).  Every other field is refused with "no
 * measured registration record", because the record encodes the field's format
 * and a template from another family would be a guess.  (Assignment Number/Text
 * records are built from the measured type words; see the assignment section.)
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
 *     Cost1     entry +26 |= 0x08   (measured, w14-cost)
 *     Cost2     entry +26 |= 0x10
 *     Cost3     entry +26 |= 0x20
 *     Cost4     entry +28 |= 0x80
 *     Cost5     entry +29 |= 0x01
 *     Cost6     entry +29 |= 0x02
 *     Cost7     entry +29 |= 0x04
 *     Cost8     entry +29 |= 0x08
 *     Cost9     entry +29 |= 0x10
 *     Cost10    entry +29 |= 0x20
 *
 * THE COST BITS ARE AN INFERENCE FROM THE BINARY'S CLASS-SLOT TABLE, except
 * Cost1 which is oracle-measured.  The 27-byte tail (entry +21..+47) is a
 * presence bitmap indexed by the field's class slot: byte = +21 + slot/8, mask
 * = 1 << (slot%8).  The decompiled descriptors
 * (`analysis/field_definitive_map.tsv`, storage class 5) give the slots
 * Text1=5, Flag1=25, Number1=35, Number2=36, Duration1=40, Cost1=43, Date1=70
 * and the bitmap rule reproduces all seven measured bits exactly; Cost2..10's
 * slots are 44,45,63,64,65,66,67,68,69, which is where the bits above come
 * from.  A single oracle fixture with Cost4 (the first field in the +28 byte)
 * would turn the inference into a measurement; until then Cost2..Cost10 are
 * written with the derived bits and the tests pin them.
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
/* True when the file storage of this field is measured (all but Flag2..Flag20,
 * whose bit positions are unknown).  Cost1..Cost10 are written; Cost1's
 * presence bit and the whole Cost family encoding are measured. */
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
    double cost = 0.0;                /* cost, currency units (stored * 100) */
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
 *   - a field id outside the measured families (Flag2..Flag20 and every unknown
 *     id), a field without one of the measured
 *     `114/TBkndTask/Props` registration records (Text1, Flag1, Number1,
 *     Number2, Duration1, Date1, Cost1..Cost10), or a `kind` that does not match
 *     the id;
 *   - text with an embedded NUL or longer than 65,535 bytes, a non-finite
 *     number, an invalid packed date, duration_ticks == 0xffffffff, or a cost
 *     that is not finite / not representable in hundredths (Project stores
 *     cost * 100 as an f64);
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
 *   Cost      VarMeta block = 8-byte little-endian f64 0.0 (W60)
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
 *   - a field outside the measured ids (Text1, Flag1, Number1, Number2,
 *     Duration1, Date1, Cost1..Cost10): Flag2..Flag20 and unknown ids, and every
 *     field whose FixedMeta tail-presence bit is not measured, are refused rather
 *     than guessed;
 *   - a task table whose size identity is broken (`check_table` and the
 *     FixedMeta/VarMeta geometry checks).
 */
ClearedField clear_field_value(CfbDocument& doc, std::uint32_t uid,
                               std::uint32_t field_id, std::string const& path,
                               std::string_view table = "114/TBkndTask");

/* ---- W60: assignment custom fields (114/TBkndAssn) ----------------------- */

/*
 * ASSIGNMENT CUSTOM FIELDS (measured, W60: docs/analysis/oracle/w14-oracle.md
 * §3, corpus/probe/w14-assignfield-control.mpp -> w14-assignfield.mpp).
 *
 * The Task-table writer does not apply: an assignment's custom value lives in
 * `114/TBkndAssn` and its VarMeta key is the field's ASSIGNMENT PROPERTY ID,
 * not the PjField id.  Measured:
 *
 *   Assignment.Number1 (PjField 0x0f40006c)  VarMeta key 0x0f40403c
 *   Assignment.Text1   (PjField 0x0f400058)  VarMeta key 0x0f404064
 *
 * `assignment_property_id` is the full key table.  Its stride-1 bases are
 * measured in Project's own files: the assignment-side Props records carry the
 * key at +0 and the PjTask field id at +8, which pins Number1 (0x3c), Number2
 * (0x3d), Number15 (0x4a) and Text1 (0x64), Text2 (0x65), Text4 (0x67), Text6
 * (0x69), Text10 (0x6d), Text11 (0x6e), Text20 (0x77) exactly (same-key
 * records also in w10-fieldvalues.mpp, w14-assignfield.mpp,
 * project_b.mpp and project_a.mpp).  Number3..20 and Text2..30
 * here are the stride-1 continuation of those measured bases.
 *
 * WHAT ONE ASSIGNMENT VALUE WRITE CHANGES (measured, w14-assignfield):
 *   - `114/TBkndAssn/VarMeta`: one new 12-byte (assignment uid, Var2Data
 *     offset, property key) entry, APPENDED after the uid's existing entries --
 *     unlike a Task field, the assignment group is NOT key-sorted (Project's own
 *     group runs 0x0f400010, 0x0f400020, ..., 0x0f40029b, then the two custom
 *     entries), so the writer appends after the group's last entry.  Header +8
 *     (entry count) and +0x14 (Var2Data size) are updated.
 *   - `114/TBkndAssn/Var2Data`: the block appended at the end: Number = u32
 *     length 8 + the f64 value (7.5 stored as 7.5, NOT x100); Text = u32 length
 *     + UTF-16LE + NUL ("assign-text" = 24-byte payload).
 *   - `114/TBkndAssn/FixedMeta`: ONLY the row entry's +2 u16 (its exact VarMeta
 *     entry count, 9 -> 11).  NO tail presence bit changes for an assignment
 *     (measured).
 *   - `114/TBkndTask/Props` and `114/TBkndAssn/Props`: one new 88-byte record
 *     per field.  The Task record is keyed by the PjTask field id (0x0b400033
 *     for Text1) and carries the assignment key at +8; the Assn record is keyed
 *     by the assignment key and carries the PjTask id at +8.  Both are the
 *     measured type shape: text = {id/key, 8, key/id, 7, 0, 0, 0x39b7,
 *     0x4ceb8bbe, 0x8cfac482, id/key big-endian at +32, zeros}; number = the
 *     same with {5, 5}.  The two opaque 32-bit stamps are Project's measured
 *     bytes, not explained.
 *
 * FixedData/Fixed2Data/Fixed2Meta are never touched, and neither is the Task
 * table (only its Props registry grows).
 */

/* The assignment-side VarMeta key (the "assignment property id") for a
 * Number1..Number20 / Text1..Text30 field, or 0 when the family/index has no
 * measured assignment storage.  Other families are refused for assignments. */
std::uint32_t assignment_property_id(FieldKind kind, unsigned index);

/* One assignment field edit.  `assignment_uid` is the assignment row's
 * FixedData +0 UniqueID (not the task uid, not the row index). */
struct AssignmentFieldEdit {
    std::uint32_t assignment_uid = 0;
    FieldValue value;
};

struct AssignmentFieldWrite {
    std::uint32_t assignment_uid = 0;
    FieldValue value;
};

struct AssignmentFieldWriteResult {
    std::size_t fields_written = 0;
    std::vector<AssignmentFieldWrite> fields; /* one per edit, in edit order */
    std::vector<std::string> warnings;
};

/*
 * Validate `edits`, write the assignment custom values, save to `path`, re-open
 * from disk and prove the result.  Refusals (before `doc` or the output path is
 * touched):
 *   - an empty edit list or an unusable output path;
 *   - an assignment uid of 0, a uid with no assignment row, a row whose
 *     FixedMeta entry is a tombstone, or a placeholder row (task uid 0);
 *   - a field outside Number1..Number20 / Text1..Text30: assignment Cost, Flag,
 *     Date, Duration, unknown ids and every other family are refused (their
 *     assignment storage is unmeasured);
 *   - text with an embedded NUL / over 65,535 bytes, a non-finite number, or a
 *     duplicate (assignment uid, field) edit;
 *   - a damaged assignment table (any of the six stream geometries, a header
 *     count/size word, a duplicate FixedMeta entry, or the Task/Assn Props
 *     preamble and size words), or a missing Props stream (the writer cannot
 *     create one).
 *
 * Only the three Assn streams plus the two Props registries change; every other
 * stream, `214/` included, is required to be byte-identical.
 */
AssignmentFieldWriteResult save_assignment_field_values(
    CfbDocument& doc, std::vector<AssignmentFieldEdit> const& edits,
    std::string const& path);

/*
 * Read one assignment custom field from the file's measured stores.  `present`
 * is true when its VarMeta entry exists and its block decodes; `text`/`number`
 * carry the value.  For a field whose assignment storage is unmeasured this
 * returns present=false with storage_measured=false rather than guessing.
 */
FieldValue read_assignment_field_value(CfbDocument const& doc,
                                       std::uint32_t assignment_uid,
                                       std::uint32_t field_id);

/*
 * WHICH STORES HOLD A VALUE (diagnostic; the W48/W60 Project gates).  Project
 * displays a custom value only when every measured store agrees:
 *
 *   task field       VarMeta entry + decodable Var2Data block + its FixedMeta
 *                    tail presence bit + the Props registration;
 *   assignment field VarMeta entry + decodable Var2Data block + the Props
 *                    registrations (no FixedMeta tail bit exists for these);
 *   Flag1            the row's FixedMeta entry (the bit IS the value, so either
 *                    state is stored) + the Props registration.
 *
 * A partially written value still re-reads cleanly, so this predicate is how a
 * caller detects the known failure mode.  `complete()` is our defined rule; it
 * is not a decode.
 */
struct FieldStores {
    bool var_meta = false;   /* a VarMeta entry for (uid, key) exists */
    bool var2_block = false; /* its block is present and decodes */
    bool presence = false;   /* the tail bits are set (values) / the entry exists
                              * (Flag1, whose bit carries the value) */
    bool registered = false; /* a Props record for the field exists */
    bool flag_field = false; /* Flag1: the bit IS the value, no VarMeta block */
    bool complete() const {
        return registered && presence &&
               (flag_field || (var_meta && var2_block));
    }
};

FieldStores field_stores(CfbDocument const& doc, std::uint32_t uid,
                         std::uint32_t field_id,
                         std::string_view table = "114/TBkndTask");

FieldStores assignment_field_stores(CfbDocument const& doc,
                                    std::uint32_t assignment_uid,
                                    std::uint32_t field_id);

} /* namespace mppwrite */

#endif /* MPPWRITE_FIELD_VALUES_H */
