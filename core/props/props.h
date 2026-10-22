// core/props -- decoder for the MPP `Props` property streams.
//
// WHAT THIS IS, AND WHAT IT IS NOT
//
// `114/Props` is a property stream.  It is NOT the Task/Resource table data and
// it does not carry task names.  The measured layout (see docs/handoff/w50.md
// and memory/props.md) is:
//
//   header (16 bytes)
//     u32 stream_size - 4        // 114/Props and 214/Props; Props14 differs
//     u32 stream_size - 4        //   (deltas 32/56/80 measured, see PropsHeader)
//     u32 format word            // 0x00010000 on 114/214 and probe Props14;
//                                //   corpus B Props14 read 0x0001006f
//     u32 declared_entries       // NOT the number of top-level records
//
//   records, back to back, no padding
//     u32 payload_length
//     u32 id                    // byte 2 is 0x40 on every measured id
//     u32 kind                  // 0, 2, 4, 9 measured (see kind_name)
//     u8  payload[payload_length]
//
//   tail (opaque bytes after the last record)
//     The record parser stops at the first offset that does not look like a
//     record header; everything from there is exposed as tail_offset .. size.
//     For the measured 114/Props fixture the tail is 85,408 bytes and begins
//     with a typed-property prefix (see tail_properties).
//
// Custom-field FORMULAS live in one record's payload, not in VarMeta.  They are
// UTF-16 text with field references rewritten by Project to `[MSPJ<decimal>]`.
// extract_formulas() recovers {field_id, formula, offsets}.  Measured on
// corpus/probe/w10-formulas{,2,3}.mpp; the six formulas are the wave-10 oracle
// set.  The decoder is the primary product; the writer added in wave 14 (W59)
// is deliberately NARROW: it replaces that ONE measured record plus the measured
// 2-byte "custom-field data in use" flag (0x02401392), and refuses any layout
// it does not understand.  There is still no general Props writer -- a stray
// byte at 114/Props+510 is enough for Project to refuse the file
// (spec/mpp-writer section 10.4).
//
// The C++ code below never maps a decimal token to a field NAME; field ids are
// the 0x0b40xxxx numbers.  That keeps client task/calendar names out of code.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace msproject::props {

// The fourth word of a record header.  Kinds other than these are preserved so
// a caller can report them; the meaning is not established beyond the measured
// payload sizes.
enum class ValueKind : std::uint32_t {
    blob = 0,
    int16 = 2,
    int32 = 4,
    int64 = 9,
};

struct PropsHeader {
    // 114/Props and 214/Props store stream_size - 4 here (measured on every
    // probe and corpus file).  Props14 does NOT: its value was stream_size - 32
    // (probe), - 56 and - 80 (the two corpus streams), so the relation is
    // variant-specific and is reported, not assumed.
    std::uint32_t size_word = 0;
    std::uint32_t size_repeat = 0;
    std::uint32_t format_version = 0;
    std::uint32_t declared_entries = 0;
};

struct PropsRecord {
    std::uint32_t offset = 0;
    std::uint32_t payload_length = 0;
    std::uint32_t id = 0;
    std::uint32_t kind = 0;
};

struct PropsDocument {
    std::vector<std::uint8_t> bytes;
    PropsHeader header;
    std::vector<PropsRecord> records;
    std::uint32_t tail_offset = 0;

    [[nodiscard]] std::uint32_t size() const {
        return static_cast<std::uint32_t>(bytes.size());
    }
    [[nodiscard]] std::uint32_t tail_size() const {
        return tail_offset <= bytes.size()
                   ? static_cast<std::uint32_t>(bytes.size() - tail_offset)
                   : 0u;
    }
    [[nodiscard]] const PropsRecord* find(std::uint32_t id) const;
    [[nodiscard]] std::span<const std::uint8_t> payload(const PropsRecord& record) const;
};

struct ParseError {
    std::string message;
    std::size_t offset = 0;
};

// True when `id` has the 0x0040 byte in position 2.  Every measured property id
// (0x0240xxxx, 0x0340xxxx, 0x0440xxxx, 0x0b40xxxx, 0x3540xxxx) satisfies it.
// This is the record-boundary test used by parse_props().
[[nodiscard]] bool is_props_id(std::uint32_t id);

[[nodiscard]] const char* kind_name(std::uint32_t kind);

// UTF-16LE preview.  Returns a text rendering when the bytes look like a
// printable UTF-16 string, otherwise a short hex rendering.  Trailing NUL is
// not part of the result.
[[nodiscard]] std::string utf16_preview(std::span<const std::uint8_t> bytes,
                                        std::size_t max_chars = 48);

// Parses a full Props stream.  Fails only when the container header is
// inconsistent (size words) or no record could be parsed.  A truncated or
// unknown trailing region is NOT an error: it becomes the opaque tail.
[[nodiscard]] bool parse_props(std::span<const std::uint8_t> data,
                               PropsDocument& out,
                               ParseError* error = nullptr);

// Human-readable dump: header, one line per record (offset, length, id, kind,
// preview) and a one-line tail summary.  max_records == 0 means all.
[[nodiscard]] std::string dump_document(const PropsDocument& document,
                                        std::size_t max_records = 0);

// One recovered formula.  `block_offset` is the start of the formula block
// ([13][0xffffffff][7 format words]...); `text_offset` is the UTF-16 text.
struct FormulaBlock {
    std::uint32_t field_id = 0;
    std::uint32_t record_id = 0;
    std::uint32_t block_offset = 0;
    std::uint32_t text_offset = 0;
    std::array<std::uint32_t, 7> format_words{};
    std::string formula;
    bool has_trailer = false;
    std::uint32_t trailer = 0;
};

// Scans every kind-0 record payload that contains UTF-16 "MSPJ".  The payload
// is the measured custom-field table:
//   u32 inner_size (== payload_length)
//   u32 count
//   count * { u32 field_id, u32 relative_block_offset }
//   formula blocks at payload_start + relative_block_offset
// Each block:
//   u32 13, u32 0xffffffff, 7 * u32 format words, UTF-16 text, u16 NUL,
//   optional u32 trailer (present when the next u32 is not the next block's 13)
// Returns an empty vector if no such record exists.  Malformed blocks are
// skipped, never guessed.
[[nodiscard]] std::vector<FormulaBlock> extract_formulas(const PropsDocument& document);

/* ------------------------------------------------------------------------- */
/* Writing the formula table (W59, wave 14)                                   */
/* ------------------------------------------------------------------------- */

// The measured formula-table record: id 0x02401390, kind 0.  The empty table
// is the 8-byte payload `08 00 00 00 00 00 00 00`; a table holding two formulas
// was measured at 168 bytes (wave-14 fixture pair, see docs/handoff/w59.md).
inline constexpr std::uint32_t kFormulaRecordId = 0x02401390u;
inline constexpr std::size_t kEmptyFormulaPayloadSize = 8u;

// The measured "custom-field data in use" flag: id 0x02401392, kind 2, a u16
// payload.  It reads 0 in files without custom-field data (synthetic base and
// the wave-14 control) and 1 in files with custom-field VALUES or formulas
// (w10-fieldvalues has 1 with an empty formula table; all w10/w14 formula
// fixtures have 1).  `set_custom_field_formula` writes 1;
// `clear_custom_field_formula` writes 0 only when it actually removes formulas
// (a no-op clear leaves it).  The flag is shared with custom-field values, so a
// caller that clears formulas from a project that still stores custom-field
// values must restore it -- core/props cannot see those other streams.
inline constexpr std::uint32_t kCustomFieldFlagRecordId = 0x02401392u;

// Replaces the payload of the formula record, changing nothing else.  Every
// other record (including the project-name record 0x02400008) and the opaque
// tail are copied byte-for-byte; the two header size words are recomputed with
// the same variant delta the input used (114/Props: stream_size - 4) and the
// record's own payload length is written.  `new_payload` must be a structurally
// valid formula table (the empty 8-byte form is valid).  On any failure `out`
// is untouched and `false` is returned with a message in `*error`.
[[nodiscard]] bool replace_formula_record(std::span<const std::uint8_t> props,
                                          std::span<const std::uint8_t> new_payload,
                                          std::vector<std::uint8_t>& out,
                                          std::string* error = nullptr);

// Sets (or replaces) the formula of one Task custom field and re-serialises the
// stream.  The formula is written exactly as Project stores it: field
// references become `[MSPJ<decimal field id>]`, and `[Duration]` is stored as
// 188744967 (Task.DurationText), the measured Project rewrite, not 188743709.
// `formula` must parse with core::formula::parse (same depth guard as the app).
// On failure `out` is untouched.
[[nodiscard]] bool set_custom_field_formula(std::span<const std::uint8_t> props,
                                            std::uint32_t field_id,
                                            std::string_view formula,
                                            std::vector<std::uint8_t>& out,
                                            std::string* error = nullptr);

// Removes every formula from the table (there is no per-field clear): the
// record becomes the measured 8-byte empty form and the stream shrinks by the
// table's size.  Clearing an already-empty table succeeds and returns the input
// unchanged.  On failure `out` is untouched.
[[nodiscard]] bool clear_custom_field_formula(std::span<const std::uint8_t> props,
                                              std::vector<std::uint8_t>& out,
                                              std::string* error = nullptr);

// Parses one `MSPJ` field token, with or without brackets, to its decimal value.
[[nodiscard]] bool parse_mspj_field(std::string_view token, std::uint32_t& field_id);

// Replaces `[MSPJ<decimal>]` with `[0x0b40xxxx]` for readability.  Tokens that
// do not parse are left untouched.
[[nodiscard]] std::string formula_with_field_ids(std::string_view formula);

// Typed-property prefix at the start of the opaque tail:
//   u32 4, u32 key, u32 4, u32 value
// The KEY is not a field id and its meaning is inferred; the VALUE changed in
// the wave-10 prefs fixtures (see memory/props.md).  Parsing stops at the first
// non-matching 16-byte group, so this only reports the proven prefix.
struct TailProperty {
    std::uint32_t key = 0;
    std::uint32_t value = 0;
    std::uint32_t offset = 0;
};

[[nodiscard]] std::vector<TailProperty> tail_properties(const PropsDocument& document);

// Candidate field-reference entries in the opaque tail.  Layout measured on
// 114/Props: a 7-word entry
//   u32 index_word, u32 length_word, u32 field_id, u32 words[4]
// every 28 bytes, with field ids sorted ascending after the first few entries
// (one 40-byte gap was measured).  The interpretation of the entry (column
// definition, field property, ...) is NOT proven, so the name stays
// "reference".
struct FieldReference {
    std::uint32_t entry_offset = 0;
    std::uint32_t index_word = 0;
    std::uint32_t length_word = 0;
    std::uint32_t field_id = 0;
    std::array<std::uint32_t, 4> words{};
};

[[nodiscard]] std::vector<FieldReference> field_references(const PropsDocument& document);

// Summary of the tail: typed properties plus the count and first/last field
// reference.  Intended for the CLI report.
[[nodiscard]] std::string describe_tail(const PropsDocument& document);

}  // namespace msproject::props
