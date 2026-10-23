/*
 * field_value.h -- the measured custom-field storage: field ids, value kinds and
 * one decoded value.  W48 (2026-10-22).
 *
 * WHY THIS EXISTS.  `core/mpp/reader.h` returns a Task's name, dates, duration
 * and so on, but not its custom fields, even though the writer can now change
 * them (`core/mppwrite/field_values.h`).  This header is the small shared
 * vocabulary both sides need: which VarMeta key means Text3 or Number12, what
 * encoding the payload has, and whether that encoding is measured at all.
 *
 * FIELD IDS are Project's `PjTask*` field ids, taken from
 * `analysis/field_id_map.tsv` (the `IBkndTaskDispatch` rows: get/put pairs such
 * as `0x0B400033 188743731 Text1`) and cross-checked against the wave-10
 * oracle (`docs/analysis/oracle/w10-oracle.md` §2), which measured the *storage*
 * of Text1, Number1/2, Duration1, Date1 and Flag1 in
 * `corpus/probe/w10-fieldvalues.mpp`.  The ranges, all in the 0x0B40xxxx task
 * field space:
 *
 *     Text1..Text6       0x0b400033 + 3*(n-1)   (0x33, 0x36, 0x39, 0x3c, 0x3f, 0x42)
 *     Text7..Text10      0x0b400042 + (n-6)     (0x43 .. 0x46)
 *     Text11..Text30     0x0b40013d + (n-11)    (0x13d .. 0x150)
 *     Flag1..Flag10      0x0b400048 + (n-1)     (0x48 .. 0x51)
 *     Flag11..Flag20     0x0b400124 + (n-11)    (0x124 .. 0x12d)
 *     Number1..Number5   0x0b400057 + (n-1)     (0x57 .. 0x5b)
 *     Number6..Number20  0x0b40012e + (n-6)     (0x12e .. 0x13c)
 *     Duration1..3       0x0b400067 + (n-1)     (0x67 .. 0x69)
 *     Duration4..10      0x0b400113 + (n-4)     (0x113 .. 0x119)
 *     Cost1..Cost3       0x0b40006a + (n-1)     (0x6a .. 0x6c)
 *     Cost4..Cost10      0x0b400102 + (n-4)     (0x102 .. 0x108)
 *     Date1..Date10      0x0b400109 + (n-1)     (0x109 .. 0x112)
 *
 * ENCODINGS (measured, w10-oracle §2, `corpus/probe/w10-fieldvalues.mpp`):
 *
 *   Text      VarMeta key 0x0b400033, block = UTF-16LE "alpha" + NUL (12 B).
 *   Number    VarMeta key 0x0b400057/0x58, block = 8 B little-endian f64
 *             (42.5, 3.25).
 *   Date      VarMeta key 0x0b400109, block = 4 B packed date (0x3d1812c0 =
 *             2026-10-26 08:00; the same encoding as Task +66/+104 etc.).
 *   Duration  VarMeta key 0x0b400067, block = 4 B u32 **file ticks**; 4800 =
 *             one 8-hour day.  COM's Task.Duration1 reports 480 minutes for the
 *             same value, so file ticks = COM minutes * 10.
 *   Flag      NOT in VarMeta: a bit in the Task FixedMeta entry.  Flag1 is bit
 *             0x02 of byte +24 (measured by `oracle_w10.py flagdiff`: 0x00 ->
 *             0x02; the Fixed2Meta entry's +31/+61 bit 0x80 mirrors it in
 *             Project's own write, but that is the writer's business).
 *             Wave 24 (W24-15) measured EVERY flag, one Project save per flag
 *             (control all false, one Task.FlagN = True on ID 3): Flag1..7 are
 *             byte +24 bits 0x02..0x80, Flag8..10 byte +25 bits 0x01..0x04,
 *             Flag11..17 byte +33 bits 0x02..0x80, Flag18..20 byte +34 bits
 *             0x01..0x04.  The reader exposes them as Task::flags (and Flag10,
 *             Project's SUMMARY column, as Task::flag10); Flag1 is ALSO kept in
 *             custom_fields for the engine dump's existing shape.
 *   Cost      Cost1..Cost10; the VarMeta key IS the field id and the block is
 *             8 B little-endian f64 of the value in HUNDREDTHS (123.45 stored as
 *             12345.0).  Measured by the wave-14 oracle
 *             (`docs/analysis/oracle/w14-oracle.md` §4, w14-cost/w14-cost2).
 *             The writer implements all ten ids
 *             (`core/mppwrite/field_values.h`); this predicate reports the
 *             storage as measured.
 *
 * What is NOT measured here: the VarMeta presence flag beyond the W48/W60
 * measured bits (the reader reports storage_measured = false where it cannot
 * decode).  The twenty Flag bits ARE measured now (wave 24), but only Flag1 is
 * carried as a CustomFieldValue, for the engine dump's stable shape; all twenty
 * are on Task::flags.
 */

#ifndef MPP_FIELD_VALUE_H
#define MPP_FIELD_VALUE_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace mpp {

/* The value families Project stores for a task custom field.  `unknown` covers
 * every id that is not one of the measured families below. */
enum class FieldKind : std::uint8_t {
    text = 0,
    number = 1,
    duration = 2,
    date = 3,
    flag = 4,
    cost = 5,
    unknown = 6,
};

/* 1-based field index range for each family, inclusive.  `custom_field_id`
 * returns 0 for an index outside the family's range. */
constexpr unsigned kTextFieldCount = 30;
constexpr unsigned kFlagFieldCount = 20;
constexpr unsigned kNumberFieldCount = 20;
constexpr unsigned kDurationFieldCount = 10;
constexpr unsigned kCostFieldCount = 10;
constexpr unsigned kDateFieldCount = 10;

/*
 * The PjTask field id for `kind` index `index` (1-based), or 0 when the index is
 * out of range or the kind has no id space.  Ranges are the ones in the file
 * comment; they are compile-time arithmetic, not a lookup table.
 */
constexpr std::uint32_t custom_field_id(FieldKind kind, unsigned index) {
    if (index == 0) {
        return 0;
    }
    switch (kind) {
        case FieldKind::text:
            if (index <= 6) {
                return 0x0B400033u + 3u * (index - 1u);
            }
            if (index <= 10) {
                return 0x0B400042u + (index - 6u);
            }
            if (index <= kTextFieldCount) {
                return 0x0B40013Du + (index - 11u);
            }
            return 0;
        case FieldKind::flag:
            if (index <= 10) {
                return 0x0B400048u + (index - 1u);
            }
            if (index <= kFlagFieldCount) {
                return 0x0B400124u + (index - 11u);
            }
            return 0;
        case FieldKind::number:
            if (index <= 5) {
                return 0x0B400057u + (index - 1u);
            }
            if (index <= kNumberFieldCount) {
                return 0x0B40012Eu + (index - 6u);
            }
            return 0;
        case FieldKind::duration:
            if (index <= 3) {
                return 0x0B400067u + (index - 1u);
            }
            if (index <= kDurationFieldCount) {
                return 0x0B400113u + (index - 4u);
            }
            return 0;
        case FieldKind::cost:
            if (index <= 3) {
                return 0x0B40006Au + (index - 1u);
            }
            if (index <= kCostFieldCount) {
                return 0x0B400102u + (index - 4u);
            }
            return 0;
        case FieldKind::date:
            if (index <= kDateFieldCount) {
                return 0x0B400109u + (index - 1u);
            }
            return 0;
        case FieldKind::unknown:
            return 0;
    }
    return 0;
}

/* The kind a field id belongs to, or FieldKind::unknown.  Implemented by
 * asking each family for its ids, so there is exactly one id table. */
constexpr FieldKind custom_field_kind(std::uint32_t field_id) {
    if (field_id == 0) {
        return FieldKind::unknown;
    }
    constexpr FieldKind kinds[] = {FieldKind::text,   FieldKind::flag,
                                   FieldKind::number, FieldKind::duration,
                                   FieldKind::cost,   FieldKind::date};
    constexpr unsigned counts[] = {kTextFieldCount, kFlagFieldCount,
                                   kNumberFieldCount, kDurationFieldCount,
                                   kCostFieldCount, kDateFieldCount};
    for (std::size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k) {
        for (unsigned index = 1; index <= counts[k]; ++index) {
            if (custom_field_id(kinds[k], index) == field_id) {
                return kinds[k];
            }
        }
    }
    return FieldKind::unknown;
}

/* The 1-based index of a field id inside its family, or 0 when unknown. */
constexpr unsigned custom_field_index(std::uint32_t field_id) {
    FieldKind const kind = custom_field_kind(field_id);
    if (kind == FieldKind::unknown) {
        return 0;
    }
    for (unsigned index = 1; index <= kTextFieldCount; ++index) {
        if (custom_field_id(kind, index) == field_id) {
            return index;
        }
    }
    return 0;
}

/*
 * True when the file storage of this field is MEASURED and may be decoded or
 * written.  Cost1..Cost10 are measured and written by W60; Flag1's bit is
 * measured and is the one flag carried as a CustomFieldValue (all twenty flag
 * bits are measured and live on Task::flags).  Everything else here has a
 * measured encoding from w10-oracle §2 / w14-oracle §4.
 */
constexpr bool custom_field_storage_measured(FieldKind kind, unsigned index) {
    switch (kind) {
        case FieldKind::text:
        case FieldKind::number:
        case FieldKind::duration:
        case FieldKind::date:
        case FieldKind::cost:
            return index != 0;
        case FieldKind::flag:
            return index == 1;
        case FieldKind::unknown:
            return false;
    }
    return false;
}

/*
 * One decoded custom-field value.  `present` is true when the file stores the
 * field for the row: a VarMeta block for text/number/date/duration/cost, the
 * FixedMeta tail bit for a flag (a clear bit is a stored False).  `value` members
 * are meaningful only when `present && storage_measured`; the others stay at
 * their default.  (core/mpp/reader.cpp decodes every family above as of wave 14,
 * including the Cost block it previously reported as unmeasured; the writer's own
 * decoder in core/mppwrite/field_values.cpp remains the second opinion.
 * All twenty Flag bits are measured as of wave 24 and live on Task::flags;
 * Flag1 is the one flag ALSO carried here for the engine dump's stable shape.)
 */
struct CustomFieldValue {
    std::uint32_t field_id = 0;
    FieldKind kind = FieldKind::unknown;
    bool present = false;
    bool storage_measured = false;
    std::string text;                    /* text: the decoded UTF-8 string */
    double number = 0.0;                 /* number */
    std::uint32_t packed_date = 0;       /* date: the raw u32 packed word */
    std::uint32_t duration_ticks = 0;    /* duration: file ticks (6 s each) */
    double cost = 0.0;                   /* cost: currency (stored f64 / 100) */
    bool flag = false;                   /* flag */

    /* The COM-facing duration unit: file ticks / 10. */
    std::uint32_t duration_minutes() const { return duration_ticks / 10u; }
};

/* The two flags whose bit positions are measured.
 *   Flag1  = 0x0b400048 at Task FixedMeta entry byte +24 bit 0x02.
 *   Flag10 = 0x0b400051 at Task FixedMeta entry byte +25 bit 0x04 (wave 23). */
constexpr std::uint32_t kTaskFlag1FieldId = 0x0B400048u;
constexpr std::uint32_t kTaskFlag10FieldId = 0x0B400051u;
constexpr std::size_t kFixedMetaFlag1Byte = 24;
constexpr std::uint8_t kFixedMetaFlag1Bit = 0x02;
constexpr std::size_t kFixedMetaFlag10Byte = 25;
constexpr std::uint8_t kFixedMetaFlag10Bit = 0x04;

} /* namespace mpp */

#endif /* MPP_FIELD_VALUE_H */
