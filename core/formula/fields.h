/*
 * fields.h -- the Task custom-field registry and the measured storage
 * encodings.
 *
 * REGISTRY
 *   The custom fields are the Task fields whose COM names are
 *   Text<n>, Number<n>, Date<n>, Flag<n>, Duration<n> and Cost<n>.  The ids are
 *   NOT hard-coded except for the six measured anchors below: the registry
 *   scans core/model's generated descriptor table at first use, so it cannot
 *   drift from the table the reader/writer use, and a test cross-checks every
 *   entry against core::model::find_field_by_id.
 *
 * STORAGE -- measured by the wave-10 oracle on corpus/probe/w10-fieldvalues.mpp
 * (docs/analysis/oracle/w10-oracle.md section 2).  VarMeta entries for uid 1:
 *
 *   field      id            block   encoding
 *   Text1      0x0b400033    12 B    UTF-16LE "alpha" + NUL
 *   Number1    0x0b400057     8 B    little-endian f64 42.5
 *   Number2    0x0b400058     8 B    little-endian f64 3.25
 *   Duration1  0x0b400067     4 B    u32 4800 = file ticks (4800 = 1 day;
 *                                    COM minutes x10: COM read 480 minutes)
 *   Date1      0x0b400109     4 B    u32 packed 0x3d1812c0 = 2026-10-26 08:00,
 *                                    (day << 16) | (seconds / 6),
 *                                    day 0 = 1983-12-31
 *   Flag1      0x0b400048     --     NOT in VarMeta; storage still open
 *
 *   Flag storage is therefore OUR CHOICE (u32 little-endian 0/1), tagged
 *   StorageConfidence::Unmeasured, not a claim about the file.  Cost storage
 *   was not exercised by the oracle either: Cost is carried as f64 like Number
 *   and tagged Unmeasured.  A two-file diff with a flag set and a cost set is
 *   the cheap oracle that would settle both.
 *
 * The packed-date day word is the same day number as the reader's packed date
 * and core::sched::date32 (day 0 = 1983-12-31); core/view's DateValue is civil,
 * so this file owns the conversion between them.
 */

#ifndef CORE_FORMULA_FIELDS_H
#define CORE_FORMULA_FIELDS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "field_table.h"
#include "value.h"

namespace core::formula {

using core::view::FieldId;

/* ------------------------------------------------------------------------- */
/* Measured Task field ids and the formula tokens the oracle saw              */
/* ------------------------------------------------------------------------- */

/* Custom-field anchors, each measured in w10-fieldvalues.mpp VarMeta. */
inline constexpr FieldId kTaskText1 = 0x0b400033u;      /* "Text1" */
inline constexpr FieldId kTaskFlag1 = 0x0b400048u;      /* "Flag1" */
inline constexpr FieldId kTaskNumber1 = 0x0b400057u;    /* "Number1" */
inline constexpr FieldId kTaskDuration1 = 0x0b400067u;  /* "Duration1" */
inline constexpr FieldId kTaskCost1 = 0x0b40006au;      /* "Cost1" */
inline constexpr FieldId kTaskDate1 = 0x0b400109u;      /* "Date1" */

/* 114/Props stores [Duration] as [MSPJ188744967] = Task.DurationText. */
inline constexpr FieldId kTaskDurationTextField = 0x0b400507u;  /* 188744967 */
inline constexpr FieldId kTaskDurationField = 0x0b40001du;      /* 188743709 */
/* 114/Props stores [% Complete] as [MSPJ188743712]. */
inline constexpr FieldId kTaskPercentCompleteField = 0x0b400020u; /* 188743712 */

/* ------------------------------------------------------------------------- */
/* Registry                                                                   */
/* ------------------------------------------------------------------------- */

enum class FieldKind : std::uint8_t {
    Text = 0,
    Number,
    Date,
    Flag,
    Duration,
    Cost,
};

const char* field_kind_name(FieldKind kind) noexcept;
bool field_kind_from_name(std::string_view name, FieldKind& out) noexcept;

/* The name prefix Project's object model uses for the kind ("Text", ...). */
const char* field_kind_prefix(FieldKind kind) noexcept;

/* How much evidence backs the encode/decode of a kind. */
enum class StorageConfidence : std::uint8_t {
    Measured = 0,   /* an oracle file stores this exact encoding */
    Inferred,       /* inferred from a measured sibling, not from this kind */
    Unmeasured,     /* our choice; no oracle observation yet */
};

const char* storage_confidence_name(StorageConfidence confidence) noexcept;

struct CustomField {
    FieldId field = 0;
    FieldKind kind = FieldKind::Text;
    int index = 0;          /* 1..N within the kind */
    std::string name;       /* "Number1" */
};

/*
 * Every Task custom field the descriptor table names, ordered by kind (Text,
 * Number, Date, Flag, Duration, Cost) then index.  Built once from the table;
 * the returned reference is valid for the process lifetime.
 */
const std::vector<CustomField>& custom_fields();

/* Nullptr when the id is not a recognised custom field / index out of range. */
const CustomField* find_custom_field(FieldId field) noexcept;
const CustomField* find_custom_field(FieldKind kind, int index) noexcept;

/*
 * Resolves the text inside a "[...]" reference:
 *   "MSPJ188744967"  -> that PjField id (0 when it is not in the table)
 *   "Duration"       -> Task.Duration, by descriptor name
 *   "% Complete"     -> Task.PercentComplete (see below)
 *   "Number1"        -> Task.Number1
 * Name matching is case-insensitive and ignores spaces, '-' and '_', and
 * rewrites '%' to "percent", so the formula UI spellings "% Complete" and
 * "Duration" resolve.  Returns 0 when nothing matches; never guesses an id.
 */
FieldId resolve_field_token(std::string_view token,
                            core::model::RecordType record_type) noexcept;

/* Decimal "[MSPJ<n>]" payload -> field id.  False when malformed or unknown. */
bool field_id_from_mspj(std::string_view token, FieldId& out) noexcept;

/* ------------------------------------------------------------------------- */
/* Civil dates <-> the packed day number (day 0 = 1983-12-31)                 */
/* ------------------------------------------------------------------------- */

/* Days since 1970-01-01 (Howard Hinnant's civil calendar algorithm). */
constexpr std::int32_t days_from_civil(int year, unsigned month, unsigned day) noexcept {
    const std::int32_t y = static_cast<std::int32_t>(year) - (month <= 2 ? 1 : 0);
    const std::int32_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);                 /* [0, 399] */
    const unsigned doy = (153u * (month > 2 ? month - 3u : month + 9u) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;         /* [0, 146096] */
    return era * 146097 + static_cast<std::int32_t>(doe) - 719468;
}

constexpr core::view::DateValue civil_from_days(std::int32_t days) noexcept {
    core::view::DateValue out;
    const std::int32_t z = days + 719468;
    const std::int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);              /* [0, 146096] */
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const std::int32_t y = static_cast<std::int32_t>(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;                            /* [0, 11] */
    const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned m = mp < 10u ? mp + 3u : mp - 9u;
    out.year = y + (m <= 2u ? 1 : 0);
    out.month = static_cast<std::uint8_t>(m);
    out.day = static_cast<std::uint8_t>(d);
    return out;
}

/* The packed-date epoch: day 0 is 1983-12-31 (measured). */
inline constexpr std::int32_t kPackedDateEpochDay = days_from_civil(1983, 12, 31);
static_assert(kPackedDateEpochDay == 5112, "packed date epoch must be 1983-12-31");

/*
 * (day << 16) | (seconds / 6), day relative to kPackedDateEpochDay.  ok=false
 * for an invalid DateValue, a day outside 0..0xfffe, or a time that does not
 * fit 0..14399 (the reader treats a word >= 14400 as "no date").
 */
std::uint32_t encode_packed_date(const core::view::DateValue& date, bool& ok) noexcept;
core::view::DateValue decode_packed_date(std::uint32_t raw, bool& ok) noexcept;

/* ------------------------------------------------------------------------- */
/* The x10 COM/file duration boundary                                         */
/* ------------------------------------------------------------------------- */

/*
 * File ticks are 6-second units: 4800 ticks = 1 eight-hour day = 480 minutes,
 * i.e. ticks = minutes x 10.  COM's Task.Duration1 attribute reads the same
 * value as 480 minutes, which is the measured x10 difference.
 */
constexpr std::int64_t duration_ticks_to_com_minutes(std::int64_t ticks) noexcept {
    return ticks / core::view::kTicksPerMinute;
}
constexpr std::int64_t com_minutes_to_duration_ticks(std::int64_t minutes) noexcept {
    return minutes * core::view::kTicksPerMinute;
}

/* ------------------------------------------------------------------------- */
/* encode_value / decode_value                                                */
/* ------------------------------------------------------------------------- */

struct EncodedValue {
    bool ok = false;
    std::string error;
    std::vector<std::uint8_t> bytes;
};

struct DecodedValue {
    bool ok = false;
    std::string error;
    core::view::Value value;
};

/* The size a fixed-width VarMeta block has; 0 means variable (Text). */
std::size_t storage_block_size(FieldKind kind) noexcept;

StorageConfidence storage_confidence(FieldKind kind) noexcept;

/*
 * Encodes one typed value in the kind's measured storage form.  The input kind
 * must be compatible:
 *   Text       Value::Text only; UTF-16LE, NUL-terminated
 *   Number     Number/Integer/Bool; LE f64
 *   Duration   Duration only (ticks as u32; a bare number is NOT accepted, so
 *              a caller cannot silently pass minutes where ticks are stored)
 *   Date       Date only; packed u32
 *   Flag       Bool (or Integer/Number 0/1); u32 LE 0/1 -- our choice
 *   Cost       Number/Integer; LE f64 -- our choice
 *
 * An empty Text value encodes as just the NUL (2 bytes), matching the
 * measured shape.
 */
EncodedValue encode_value(FieldKind kind, const core::view::Value& value);

/* The inverse.  `size` must equal the block's measured length unless Text. */
DecodedValue decode_value(FieldKind kind, const std::uint8_t* data, std::size_t size);

inline DecodedValue decode_value(FieldKind kind, const std::vector<std::uint8_t>& bytes) {
    return decode_value(kind, bytes.data(), bytes.size());
}

/* UTF-16LE helpers (exposed for tests of the text encoding on their own). */
std::string encode_text_utf16le(std::string_view utf8);
bool decode_text_utf16le(const std::uint8_t* data, std::size_t size, std::string& out) noexcept;

}  // namespace core::formula

#endif  // CORE_FORMULA_FIELDS_H
