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
// set.  This module only READS.  There is deliberately no writer: a stray byte
// at 114/Props+510 is enough for Project to refuse the file (spec/mpp-writer
// section 10.4).
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
