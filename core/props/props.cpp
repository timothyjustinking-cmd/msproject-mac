#include "props.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <optional>
#include <sstream>
#include <utility>

#include "expr.h"
#include "fields.h"

namespace msproject::props {
namespace {

constexpr std::uint32_t kPropsIdMarker = 0x00400000u;
constexpr std::uint32_t kPropsIdMask = 0x00ff0000u;
constexpr std::uint32_t kFormulaHeader = 13u;
constexpr std::uint32_t kSentinel = 0xffffffffu;

[[nodiscard]] std::uint32_t read_u32(std::span<const std::uint8_t> bytes,
                                     std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        return 0u;
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

[[nodiscard]] std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                                     std::size_t offset) {
    return read_u32(std::span<const std::uint8_t>(bytes), offset);
}

[[nodiscard]] std::uint32_t read_u16(std::span<const std::uint8_t> bytes,
                                     std::size_t offset) {
    if (offset + 2u > bytes.size()) {
        return 0u;
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u);
}

[[nodiscard]] std::uint32_t read_u16(const std::vector<std::uint8_t>& bytes,
                                     std::size_t offset) {
    return read_u16(std::span<const std::uint8_t>(bytes), offset);
}

[[nodiscard]] bool contains_mspj(std::span<const std::uint8_t> bytes) {
    static constexpr std::uint8_t kToken[] = {0x4d, 0x00, 0x53, 0x00,
                                              0x50, 0x00, 0x4a, 0x00};
    if (bytes.size() < sizeof(kToken)) {
        return false;
    }
    const auto* begin = bytes.data();
    const auto* end = begin + bytes.size();
    const auto* found = std::search(begin, end, std::begin(kToken), std::end(kToken));
    return found != end;
}

void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point <= 0x7fu) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ffu) {
        out.push_back(static_cast<char>(0xc0u | (code_point >> 6u)));
        out.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    } else if (code_point <= 0xffffu) {
        out.push_back(static_cast<char>(0xe0u | (code_point >> 12u)));
        out.push_back(static_cast<char>(0x80u | ((code_point >> 6u) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    } else {
        out.push_back(static_cast<char>(0xf0u | (code_point >> 18u)));
        out.push_back(static_cast<char>(0x80u | ((code_point >> 12u) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | ((code_point >> 6u) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    }
}

[[nodiscard]] std::string hex_preview(std::span<const std::uint8_t> bytes,
                                      std::size_t limit = 12) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out = "hex:";
    const std::size_t count = std::min(limit, bytes.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            out.push_back(' ');
        }
        out.push_back(kDigits[bytes[i] >> 4u]);
        out.push_back(kDigits[bytes[i] & 0x0fu]);
    }
    if (bytes.size() > count) {
        out += " ...";
    }
    return out;
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

[[nodiscard]] std::string hex_u32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

// One raw formula block, copied or generated.  Keeping the block bytes opaque
// is what lets an existing trailer survive a set/clear of a DIFFERENT field.
struct FormulaTableEntry {
    std::uint32_t field_id = 0;
    std::vector<std::uint8_t> block;
};

// Validates and splits a formula-table payload.  The measured layout is strict:
// the table is contiguous and tiles the payload exactly, so anything else is
// refused rather than guessed at.  Blocks are returned verbatim.
[[nodiscard]] bool parse_formula_table(std::span<const std::uint8_t> payload,
                                       std::vector<FormulaTableEntry>& entries,
                                       std::string* error) {
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    entries.clear();
    if (payload.size() < kEmptyFormulaPayloadSize) {
        return fail("formula payload is shorter than the 8-byte empty table");
    }
    const std::uint32_t inner_size = read_u32(payload, 0);
    if (static_cast<std::size_t>(inner_size) != payload.size()) {
        return fail("formula payload inner size word is " + std::to_string(inner_size) +
                    " but the payload is " + std::to_string(payload.size()) + " bytes");
    }
    const std::uint32_t count = read_u32(payload, 4);
    if (count == 0) {
        if (payload.size() != kEmptyFormulaPayloadSize) {
            return fail("formula table has count 0 but is " +
                        std::to_string(payload.size()) + " bytes, not the measured 8");
        }
        return true;
    }
    const std::size_t entry_count = static_cast<std::size_t>(count);
    if ((payload.size() - kEmptyFormulaPayloadSize) / 8u < entry_count) {
        return fail("formula table count " + std::to_string(count) +
                    " does not fit in the payload");
    }
    const std::size_t table_size = kEmptyFormulaPayloadSize + entry_count * 8u;
    std::vector<std::size_t> block_starts(entry_count, 0u);
    for (std::size_t i = 0; i < entry_count; ++i) {
        const std::uint32_t field_id = read_u32(payload, 8u + i * 8u);
        const std::uint32_t relative = read_u32(payload, 8u + i * 8u + 4u);
        if (field_id == 0u) {
            return fail("formula entry " + std::to_string(i) + " has field id 0");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (read_u32(payload, 8u + j * 8u) == field_id) {
                return fail("formula table repeats field id " + hex_u32(field_id));
            }
        }
        block_starts[i] = static_cast<std::size_t>(relative);
    }
    if (block_starts.front() != table_size) {
        return fail("first formula block starts at " +
                    std::to_string(block_starts.front()) + ", expected " +
                    std::to_string(table_size));
    }
    for (std::size_t i = 0; i < entry_count; ++i) {
        const std::size_t start = block_starts[i];
        const std::size_t end =
            i + 1u < entry_count ? block_starts[i + 1u] : payload.size();
        if (end <= start || end > payload.size()) {
            return fail("formula block " + std::to_string(i) + " has an invalid span");
        }
        const std::size_t span = end - start;
        if (span < 38u) {
            return fail("formula block " + std::to_string(i) +
                        " is shorter than a header plus an empty NUL");
        }
        if (read_u32(payload, start) != kFormulaHeader ||
            read_u32(payload, start + 4u) != kSentinel) {
            return fail("formula block " + std::to_string(i) +
                        " does not start with the measured 13/0xffffffff header");
        }
        // The measured reader trusts the UTF-16 NUL, not a fixed stride.
        bool terminated = false;
        std::size_t units = 0;
        for (std::size_t pos = start + 36u; pos + 2u <= end; pos += 2u) {
            const std::uint32_t unit = static_cast<std::uint32_t>(payload[pos]) |
                                       (static_cast<std::uint32_t>(payload[pos + 1u]) << 8u);
            if (unit == 0u) {
                terminated = true;
                break;
            }
            ++units;
        }
        if (!terminated) {
            return fail("formula block " + std::to_string(i) +
                        " has no UTF-16 NUL inside its span");
        }
        if (units == 0u) {
            return fail("formula block " + std::to_string(i) + " has empty formula text");
        }
        FormulaTableEntry entry;
        entry.field_id = read_u32(payload, 8u + i * 8u);
        entry.block.assign(payload.begin() + static_cast<std::ptrdiff_t>(start),
                           payload.begin() + static_cast<std::ptrdiff_t>(end));
        entries.push_back(std::move(entry));
    }
    return true;
}

// Rebuilds a formula-table payload from entries: table, then blocks, with the
// relative offsets recomputed.  `inner_size` is patched to the payload length.
[[nodiscard]] bool build_formula_payload(const std::vector<FormulaTableEntry>& entries,
                                         std::vector<std::uint8_t>& payload,
                                         std::string* error) {
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (entries.size() > (0xffffffffull - kEmptyFormulaPayloadSize) / 8u) {
        return fail("too many formula entries");
    }
    std::uint64_t offset = kEmptyFormulaPayloadSize + entries.size() * 8u;
    for (const auto& entry : entries) {
        if (entry.block.size() < 38u) {
            return fail("a generated formula block is too short");
        }
        offset += entry.block.size();
        if (offset > 0xffffffffull) {
            return fail("formula table does not fit a 32-bit payload size");
        }
    }
    std::vector<std::uint8_t> built;
    built.reserve(static_cast<std::size_t>(offset));
    append_u32(built, 0u);  // patched below
    append_u32(built, static_cast<std::uint32_t>(entries.size()));
    std::uint64_t relative = kEmptyFormulaPayloadSize + entries.size() * 8u;
    for (const auto& entry : entries) {
        append_u32(built, entry.field_id);
        append_u32(built, static_cast<std::uint32_t>(relative));
        relative += entry.block.size();
    }
    for (const auto& entry : entries) {
        built.insert(built.end(), entry.block.begin(), entry.block.end());
    }
    write_u32(built, 0u, static_cast<std::uint32_t>(built.size()));
    payload = std::move(built);
    return true;
}

// One generated block: 13, 0xffffffff, 7 * span, UTF-16LE text, u16 NUL.  All
// seven span words equal the block's own byte span on every measured block; a
// generated block carries no trailer (both formulas Project wrote in the
// wave-14 pair have none).
[[nodiscard]] bool build_formula_block(std::string_view encoded,
                                       std::vector<std::uint8_t>& block,
                                       std::string* error) {
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    const std::string text = core::formula::encode_text_utf16le(encoded);
    if (text.empty() || text.size() > 0xffffffffull - 36u) {
        return fail("formula text is too long to store");
    }
    const std::uint32_t span = 36u + static_cast<std::uint32_t>(text.size());
    block.clear();
    append_u32(block, kFormulaHeader);
    append_u32(block, kSentinel);
    for (int word = 0; word < 7; ++word) {
        append_u32(block, span);
    }
    block.insert(block.end(), text.begin(), text.end());
    return true;
}

// The inverse of extract_formulas()'s text handling: parse the caller's
// formula, then rewrite every resolved field reference to the decimal MSPJ
// token Project stores.  `[Duration]` is the one measured mismatch between the
// parser's resolved id and the stored id (188744967, not 188743709).
[[nodiscard]] bool encode_formula_text(std::string_view formula, std::string& encoded,
                                       std::string* error) {
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    const core::formula::ParseResult parsed = core::formula::parse(formula);
    if (!parsed.ok) {
        return fail("formula does not parse: " + parsed.error);
    }
    std::string out;
    out.reserve(formula.size() + 32u);
    std::size_t pos = 0;
    for (const auto& reference : parsed.field_refs) {
        if (reference.position < pos || reference.position >= formula.size() ||
            formula[reference.position] != '[') {
            return fail("internal error: field-reference position does not point at '['");
        }
        const std::size_t close = formula.find(']', reference.position + 1u);
        if (close == std::string_view::npos) {
            return fail("internal error: field reference is not closed");
        }
        std::uint32_t stored = reference.field;
        if (stored == core::formula::kTaskDurationField) {
            stored = core::formula::kTaskDurationTextField;
        }
        out.append(formula.substr(pos, reference.position - pos));
        out += "[MSPJ";
        out += std::to_string(stored);
        out.push_back(']');
        pos = close + 1u;
    }
    out.append(formula.substr(pos));
    const core::formula::ParseResult stored_parse = core::formula::parse(out);
    if (!stored_parse.ok) {
        return fail("stored formula does not parse: " + stored_parse.error);
    }
    encoded = std::move(out);
    return true;
}

// Finds the single measured formula record.
[[nodiscard]] const PropsRecord* find_formula_record(const PropsDocument& document,
                                                     std::string* error) {
    const PropsRecord* found = nullptr;
    std::size_t matches = 0;
    for (const auto& record : document.records) {
        if (record.id == kFormulaRecordId) {
            found = &record;
            ++matches;
        }
    }
    if (matches == 0u) {
        if (error != nullptr) {
            *error = "no formula record (0x02401390) in this Props stream";
        }
        return nullptr;
    }
    if (matches > 1u) {
        if (error != nullptr) {
            *error = "more than one formula record (0x02401390); refusing the layout";
        }
        return nullptr;
    }
    if (found->kind != 0u) {
        if (error != nullptr) {
            *error = "formula record has kind " + std::to_string(found->kind) +
                     ", expected 0";
        }
        return nullptr;
    }
    return found;
}

// The second record the writer must touch: a u16 "custom fields in use" flag.
// Measured: 0 in files with no custom-field data (synthetic base, the wave-14
// control); 1 in every file that has custom-field values OR formulas
// (w10-fieldvalues, w10-flag-on, w14-cost, w14-assignfield, w10-formulas1..3,
// the wave-14 edited file).  It is 1 with an EMPTY formula table when only
// values are set, so it is not a formula count; it means "custom-field data is
// in use".  The writer sets it to 1 when a formula is added and to 0 only when
// it actually removes formulas -- a no-op clear leaves it alone, and a project
// that still has custom-field VALUES elsewhere must keep it at 1 (core/props
// cannot see those other streams; see docs/handoff/w59.md).
[[nodiscard]] const PropsRecord* find_custom_field_flag_record(
    const PropsDocument& document, std::string* error) {
    const PropsRecord* found = nullptr;
    std::size_t matches = 0;
    for (const auto& record : document.records) {
        if (record.id == kCustomFieldFlagRecordId) {
            found = &record;
            ++matches;
        }
    }
    if (matches == 0u) {
        if (error != nullptr) {
            *error = "no custom-field flag record (0x02401392) in this Props stream";
        }
        return nullptr;
    }
    if (matches > 1u) {
        if (error != nullptr) {
            *error = "more than one custom-field flag record (0x02401392); refusing the layout";
        }
        return nullptr;
    }
    if (found->kind != 2u || found->payload_length != 2u) {
        if (error != nullptr) {
            *error = "custom-field flag record has kind " + std::to_string(found->kind) +
                     " and length " + std::to_string(found->payload_length) +
                     ", expected kind 2 and length 2";
        }
        return nullptr;
    }
    return found;
}

// Replaces one record payload and re-checks the result structurally.  `out` is
// assigned only after every check passes.  The header size words keep the
// input's own delta (114/Props: stream_size - 4; Props14 differs), so the same
// helper works for both variants without asserting an unmeasured relation.
// `flag_value` optionally rewrites the u16 custom-field flag record (the only
// other record this module ever changes; its length never changes).
[[nodiscard]] bool write_formula_table(std::span<const std::uint8_t> props,
                                       const PropsDocument& document,
                                       const PropsRecord& record,
                                       std::span<const std::uint8_t> payload,
                                       std::optional<std::uint16_t> flag_value,
                                       std::vector<std::uint8_t>& out,
                                       std::string* error) {
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (payload.size() > 0xffffffffull) {
        return fail("formula payload does not fit a 32-bit record length");
    }
    const PropsRecord* flag = find_custom_field_flag_record(document, error);
    if (flag == nullptr) {
        return false;
    }
    const std::size_t old_size = props.size();
    const std::size_t record_end =
        static_cast<std::size_t>(record.offset) + 12u +
        static_cast<std::size_t>(record.payload_length);
    if (record_end > old_size) {
        return fail("internal error: formula record ends outside the stream");
    }
    const std::int64_t shift = static_cast<std::int64_t>(payload.size()) -
                               static_cast<std::int64_t>(record.payload_length);
    const std::int64_t flag_offset =
        static_cast<std::int64_t>(flag->offset) + shift;

    std::vector<std::uint8_t> built;
    built.reserve(old_size + payload.size());
    built.insert(built.end(), props.begin(),
                 props.begin() + static_cast<std::ptrdiff_t>(record.offset));
    append_u32(built, static_cast<std::uint32_t>(payload.size()));
    append_u32(built, record.id);
    append_u32(built, record.kind);
    built.insert(built.end(), payload.begin(), payload.end());
    built.insert(built.end(), props.begin() + static_cast<std::ptrdiff_t>(record_end),
                 props.end());

    if (flag_offset < 0 ||
        static_cast<std::size_t>(flag_offset) + 14u > built.size()) {
        return fail("internal error: custom-field flag record moved outside the stream");
    }
    if (flag_value.has_value()) {
        write_u16(built, static_cast<std::size_t>(flag_offset) + 12u, *flag_value);
    }

    const std::size_t delta = old_size - document.header.size_word;
    if (built.size() < delta + 16u) {
        return fail("rebuilt stream is too short for the header size relation");
    }
    const std::uint64_t new_word64 = built.size() - delta;
    if (new_word64 > 0xffffffffull) {
        return fail("rebuilt stream does not fit a 32-bit header size word");
    }
    const std::uint32_t new_word = static_cast<std::uint32_t>(new_word64);
    write_u32(built, 0u, new_word);
    write_u32(built, 4u, new_word);

    // Re-parse and prove that only the intended records moved.
    PropsDocument check;
    ParseError parse_error;
    if (!parse_props(built, check, &parse_error)) {
        return fail("rebuilt Props stream does not re-parse: " + parse_error.message);
    }
    if (check.records.size() != document.records.size()) {
        return fail("rebuilt stream re-parses to " +
                    std::to_string(check.records.size()) + " records, expected " +
                    std::to_string(document.records.size()));
    }
    if (check.header.size_word != new_word || check.header.size_repeat != new_word) {
        return fail("rebuilt header size words do not match the new stream size");
    }
    const PropsRecord* rebuilt_formula = find_formula_record(check, error);
    if (rebuilt_formula == nullptr) {
        return false;
    }
    if (rebuilt_formula->offset != record.offset ||
        rebuilt_formula->payload_length != payload.size() ||
        rebuilt_formula->kind != record.kind) {
        return fail("rebuilt formula record is not where the new payload was written");
    }
    const PropsRecord* rebuilt_flag = find_custom_field_flag_record(check, error);
    if (rebuilt_flag == nullptr) {
        return false;
    }
    if (rebuilt_flag->offset != static_cast<std::uint32_t>(flag_offset)) {
        return fail("rebuilt custom-field flag record moved unexpectedly");
    }
    const std::uint16_t expected_flag =
        flag_value.has_value() ? *flag_value : static_cast<std::uint16_t>(
                                                   read_u16(document.payload(*flag), 0u));
    if (read_u16(check.payload(*rebuilt_flag), 0u) != expected_flag) {
        return fail("rebuilt custom-field flag value is not what was requested");
    }
    const std::int64_t expected_tail =
        static_cast<std::int64_t>(document.tail_offset) + shift;
    if (expected_tail < 0 ||
        static_cast<std::size_t>(expected_tail) != check.tail_offset) {
        return fail("rebuilt tail offset was not shifted by the record size change");
    }
    if (props.size() - document.tail_offset != built.size() - check.tail_offset) {
        return fail("rebuilt tail changed size");
    }
    if (!std::equal(props.begin() +
                        static_cast<std::ptrdiff_t>(document.tail_offset),
                    props.end(),
                    built.begin() + static_cast<std::ptrdiff_t>(check.tail_offset))) {
        return fail("rebuilt opaque tail is not byte-identical");
    }
    for (std::size_t i = 0; i < document.records.size(); ++i) {
        const auto& before = document.records[i];
        const auto& after = check.records[i];
        if (before.id == kFormulaRecordId) {
            continue;
        }
        if (before.id != after.id || before.kind != after.kind ||
            before.payload_length != after.payload_length) {
            return fail("rebuilt stream changed a record other than the formula table");
        }
        const auto before_begin =
            props.begin() + static_cast<std::ptrdiff_t>(before.offset);
        const auto after_begin =
            built.begin() + static_cast<std::ptrdiff_t>(after.offset);
        if (before.id == kCustomFieldFlagRecordId) {
            // Length/kind checked above; the 2-byte payload may be rewritten.
            if (before.payload_length != 2u ||
                read_u16(check.payload(after), 0u) != expected_flag) {
                return fail("rebuilt custom-field flag record is not as requested");
            }
            continue;
        }
        const std::size_t record_size =
            12u + static_cast<std::size_t>(before.payload_length);
        if (!std::equal(before_begin,
                        before_begin + static_cast<std::ptrdiff_t>(record_size),
                        after_begin)) {
            return fail("rebuilt stream changed a record other than the formula table");
        }
    }
    out = std::move(built);
    return true;
}

}  // namespace

bool is_props_id(std::uint32_t id) {
    return (id & kPropsIdMask) == kPropsIdMarker;
}

const char* kind_name(std::uint32_t kind) {
    switch (kind) {
        case 0:
            return "blob";
        case 2:
            return "int16";
        case 4:
            return "int32";
        case 9:
            return "int64";
        default:
            return "unknown";
    }
}

std::string utf16_preview(std::span<const std::uint8_t> bytes, std::size_t max_chars) {
    if (bytes.size() < 2 || (bytes.size() % 2u) != 0u) {
        return hex_preview(bytes);
    }

    bool printable = true;
    bool terminated = false;
    std::size_t text_units = 0;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const std::uint32_t unit =
            static_cast<std::uint32_t>(bytes[i]) |
            (static_cast<std::uint32_t>(bytes[i + 1]) << 8u);
        if (unit == 0u) {
            terminated = true;
            break;
        }
        ++text_units;
        if (unit < 0x20u || (unit >= 0x7fu && unit < 0xa0u)) {
            printable = false;
            break;
        }
    }

    if (!printable || !terminated) {
        return hex_preview(bytes);
    }
    if (text_units < 2u) {
        return hex_preview(bytes);
    }

    std::string out;
    std::size_t written = 0;
    bool truncated = false;
    for (std::size_t i = 0; i + 1 < bytes.size();) {
        std::uint32_t unit = static_cast<std::uint32_t>(bytes[i]) |
                             (static_cast<std::uint32_t>(bytes[i + 1]) << 8u);
        i += 2;
        if (unit == 0u) {
            break;
        }
        if (written >= max_chars) {
            truncated = true;
            break;
        }
        std::uint32_t code_point = unit;
        if (unit >= 0xd800u && unit <= 0xdbffu && i + 1 < bytes.size()) {
            const std::uint32_t low = static_cast<std::uint32_t>(bytes[i]) |
                                      (static_cast<std::uint32_t>(bytes[i + 1]) << 8u);
            if (low >= 0xdc00u && low <= 0xdfffu) {
                i += 2;
                code_point = 0x10000u + ((unit - 0xd800u) << 10u) + (low - 0xdc00u);
            }
        }
        append_utf8(out, code_point);
        ++written;
    }
    if (truncated) {
        out += "...";
    }
    return out;
}

bool parse_props(std::span<const std::uint8_t> data, PropsDocument& out,
                 ParseError* error) {
    auto fail = [&](std::string message, std::size_t offset) {
        if (error != nullptr) {
            error->message = std::move(message);
            error->offset = offset;
        }
        return false;
    };

    if (data.size() < 16u) {
        return fail("Props stream is shorter than the 16-byte header", data.size());
    }

    const std::uint32_t size_first = read_u32(data, 0);
    const std::uint32_t size_second = read_u32(data, 4);
    if (size_first != size_second || size_first < 16u ||
        size_first > data.size()) {
        std::ostringstream message;
        message << "Props size words are invalid: header says 0x" << std::hex
                << size_first << "/0x" << size_second << std::dec << " for a "
                << data.size() << "-byte stream";
        return fail(message.str(), 0);
    }

    PropsDocument parsed;
    parsed.bytes.assign(data.begin(), data.end());
    parsed.header.size_word = size_first;
    parsed.header.size_repeat = size_second;
    parsed.header.format_version = read_u32(data, 8);
    parsed.header.declared_entries = read_u32(data, 12);

    std::size_t offset = 16;
    while (offset + 12u <= data.size()) {
        const std::uint32_t length = read_u32(data, offset);
        const std::uint32_t id = read_u32(data, offset + 4);
        const std::uint32_t kind = read_u32(data, offset + 8);
        if (!is_props_id(id)) {
            break;
        }
        const std::size_t remaining = data.size() - offset - 12u;
        if (length > remaining) {
            break;
        }
        PropsRecord record;
        record.offset = static_cast<std::uint32_t>(offset);
        record.payload_length = length;
        record.id = id;
        record.kind = kind;
        parsed.records.push_back(record);
        offset += 12u + length;
    }

    if (parsed.records.empty()) {
        return fail("Props stream has no property records", 16);
    }

    parsed.tail_offset = static_cast<std::uint32_t>(offset);
    out = std::move(parsed);
    return true;
}

const PropsRecord* PropsDocument::find(std::uint32_t id) const {
    for (const auto& record : records) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

std::span<const std::uint8_t> PropsDocument::payload(const PropsRecord& record) const {
    const std::size_t begin = record.offset + 12u;
    if (begin > bytes.size()) {
        return {};
    }
    const std::size_t available = bytes.size() - begin;
    const std::size_t length = std::min<std::size_t>(record.payload_length, available);
    return std::span<const std::uint8_t>(bytes.data() + begin, length);
}

std::string dump_document(const PropsDocument& document, std::size_t max_records) {
    std::ostringstream out;
    out << "Props stream: " << document.size() << " bytes\n";
    out << "  header: size-word=" << document.header.size_word
        << " repeat=" << document.header.size_repeat << " format=0x" << std::hex
        << document.header.format_version << std::dec
        << " declared_entries=" << document.header.declared_entries << "\n";
    out << "  records: " << document.records.size() << "\n";

    const std::size_t shown =
        max_records == 0u ? document.records.size()
                          : std::min(max_records, document.records.size());
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& record = document.records[i];
        out << "  [" << i << "] offset=" << record.offset
            << " length=" << record.payload_length << " id=0x" << std::hex
            << record.id << std::dec << " kind=" << record.kind << " ("
            << kind_name(record.kind) << ") preview=\""
            << utf16_preview(document.payload(record)) << "\"\n";
    }
    if (shown < document.records.size()) {
        out << "  ... " << (document.records.size() - shown) << " more records\n";
    }

    out << "  tail: offset=" << document.tail_offset
        << " length=" << document.tail_size() << " (opaque)\n";
    const auto properties = tail_properties(document);
    if (!properties.empty()) {
        out << "  tail typed properties: " << properties.size() << "\n";
        for (const auto& property : properties) {
            out << "    +" << (property.offset - document.tail_offset) << " key=0x"
                << std::hex << property.key << std::dec << " value=" << property.value
                << " (0x" << std::hex << property.value << std::dec << ")\n";
        }
    }
    return out.str();
}

std::vector<FormulaBlock> extract_formulas(const PropsDocument& document) {
    std::vector<FormulaBlock> out;
    for (const auto& record : document.records) {
        if (record.kind != 0u) {
            continue;
        }
        const auto payload_bytes = document.payload(record);
        if (!contains_mspj(payload_bytes)) {
            continue;
        }
        const std::uint32_t inner_size = read_u32(payload_bytes, 0);
        const std::uint32_t count = read_u32(payload_bytes, 4);
        if (count == 0u || count > 4096u) {
            continue;
        }
        if (inner_size > payload_bytes.size()) {
            continue;
        }
        if (8u + static_cast<std::size_t>(count) * 8u > payload_bytes.size()) {
            continue;
        }

        const std::uint32_t payload_start = record.offset + 12u;
        const std::uint32_t record_end = payload_start + record.payload_length;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t pair_offset = 8u + static_cast<std::size_t>(i) * 8u;
            const std::uint32_t field_id = read_u32(payload_bytes, pair_offset);
            const std::uint32_t relative = read_u32(payload_bytes, pair_offset + 4u);
            const std::uint64_t block64 =
                static_cast<std::uint64_t>(payload_start) + relative;
            if (block64 + 36u > record_end) {
                continue;
            }
            const std::uint32_t block = static_cast<std::uint32_t>(block64);
            if (read_u32(document.bytes, block) != kFormulaHeader ||
                read_u32(document.bytes, block + 4u) != kSentinel) {
                continue;
            }

            FormulaBlock formula;
            formula.field_id = field_id;
            formula.record_id = record.id;
            formula.block_offset = block;
            formula.text_offset = block + 36u;
            for (std::size_t word = 0; word < formula.format_words.size(); ++word) {
                formula.format_words[word] =
                    read_u32(document.bytes, block + 8u + word * 4u);
            }

            std::string text;
            std::size_t pos = formula.text_offset;
            bool terminated = false;
            while (pos + 1u < record_end) {
                const std::uint32_t unit = read_u16(document.bytes, pos);
                if (unit == 0u) {
                    terminated = true;
                    break;
                }
                append_utf8(text, unit);
                pos += 2u;
            }
            if (!terminated || text.empty()) {
                continue;
            }
            formula.formula = std::move(text);

            const std::size_t after = pos + 2u;
            if (after + 4u <= record_end) {
                const std::uint32_t possible = read_u32(document.bytes, after);
                if (possible != kFormulaHeader) {
                    formula.has_trailer = true;
                    formula.trailer = possible;
                }
            }
            out.push_back(std::move(formula));
        }
    }
    return out;
}

bool replace_formula_record(std::span<const std::uint8_t> props,
                            std::span<const std::uint8_t> new_payload,
                            std::vector<std::uint8_t>& out, std::string* error) {
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    PropsDocument document;
    ParseError parse_error;
    if (!parse_props(props, document, &parse_error)) {
        return fail("input Props stream does not parse: " + parse_error.message);
    }
    const PropsRecord* record = find_formula_record(document, error);
    if (record == nullptr) {
        return false;
    }
    std::vector<FormulaTableEntry> entries;
    if (!parse_formula_table(new_payload, entries, error)) {
        return false;
    }
    return write_formula_table(props, document, *record, new_payload, std::nullopt, out,
                               error);
}

bool set_custom_field_formula(std::span<const std::uint8_t> props,
                              std::uint32_t field_id, std::string_view formula,
                              std::vector<std::uint8_t>& out, std::string* error) {
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (field_id == 0u) {
        return fail("field id 0 is not a Task custom field");
    }
    if (core::formula::find_custom_field(field_id) == nullptr) {
        if (core::model::find_field_by_id(field_id) == nullptr) {
            return fail("unknown field id " + hex_u32(field_id));
        }
        return fail("field id " + hex_u32(field_id) + " is not a Task custom field");
    }
    std::string encoded;
    if (!encode_formula_text(formula, encoded, error)) {
        return false;
    }
    PropsDocument document;
    ParseError parse_error;
    if (!parse_props(props, document, &parse_error)) {
        return fail("input Props stream does not parse: " + parse_error.message);
    }
    const PropsRecord* record = find_formula_record(document, error);
    if (record == nullptr) {
        return false;
    }
    std::vector<FormulaTableEntry> entries;
    if (!parse_formula_table(document.payload(*record), entries, error)) {
        return false;
    }
    std::vector<std::uint8_t> block;
    if (!build_formula_block(encoded, block, error)) {
        return false;
    }
    bool replaced = false;
    for (auto& entry : entries) {
        if (entry.field_id == field_id) {
            entry.block = block;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        FormulaTableEntry entry;
        entry.field_id = field_id;
        entry.block = std::move(block);
        entries.push_back(std::move(entry));
    }
    // Project writes the entries in ascending field-id order (measured: the
    // wave-14 pair is Text2 0x0b400036 then Number2 0x0b400058; the six
    // wave-10 entries are ascending too).
    std::stable_sort(entries.begin(), entries.end(),
                     [](const FormulaTableEntry& lhs, const FormulaTableEntry& rhs) {
                         return lhs.field_id < rhs.field_id;
                     });
    std::vector<std::uint8_t> payload;
    if (!build_formula_payload(entries, payload, error)) {
        return false;
    }
    std::vector<std::uint8_t> built;
    if (!write_formula_table(props, document, *record, payload,
                             std::optional<std::uint16_t>(std::uint16_t{1u}), built,
                             error)) {
        return false;
    }
    // Prove the decoder reads back exactly what was written.  extract_formulas
    // only scans payloads containing the `MSPJ` token, so a formula with no
    // field reference is refused here rather than written unreadably.
    PropsDocument check;
    if (!parse_props(built, check, &parse_error)) {
        return fail("rebuilt Props stream does not re-parse: " + parse_error.message);
    }
    const auto formulas = extract_formulas(check);
    if (formulas.size() != entries.size()) {
        return fail("rebuilt formula table does not round-trip: extract_formulas found " +
                    std::to_string(formulas.size()) + " of " +
                    std::to_string(entries.size()) +
                    " formulas (a formula with no field reference cannot be recovered)");
    }
    const FormulaBlock* stored = nullptr;
    for (const auto& candidate : formulas) {
        if (candidate.field_id == field_id) {
            stored = &candidate;
        }
    }
    if (stored == nullptr || stored->formula != encoded) {
        return fail("rebuilt formula text does not read back as written");
    }
    out = std::move(built);
    return true;
}

bool clear_custom_field_formula(std::span<const std::uint8_t> props,
                                std::vector<std::uint8_t>& out, std::string* error) {
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    PropsDocument document;
    ParseError parse_error;
    if (!parse_props(props, document, &parse_error)) {
        return fail("input Props stream does not parse: " + parse_error.message);
    }
    const PropsRecord* record = find_formula_record(document, error);
    if (record == nullptr) {
        return false;
    }
    std::vector<FormulaTableEntry> entries;
    if (!parse_formula_table(document.payload(*record), entries, error)) {
        return false;
    }
    std::vector<std::uint8_t> payload(kEmptyFormulaPayloadSize, 0u);
    write_u32(payload, 0u, static_cast<std::uint32_t>(kEmptyFormulaPayloadSize));
    // Only clear the "custom fields in use" flag when formulas were actually
    // removed.  A project may still have custom-field VALUES stored elsewhere
    // (measured: w10-fieldvalues has flag 1 and an empty formula table), and
    // this module cannot see those streams; a no-op clear must not guess.
    std::optional<std::uint16_t> flag_value;
    if (!entries.empty()) {
        flag_value = static_cast<std::uint16_t>(0u);
    }
    std::vector<std::uint8_t> built;
    if (!write_formula_table(props, document, *record, payload, flag_value, built,
                             error)) {
        return false;
    }
    PropsDocument check;
    if (!parse_props(built, check, &parse_error)) {
        return fail("rebuilt Props stream does not re-parse: " + parse_error.message);
    }
    if (!extract_formulas(check).empty()) {
        return fail("rebuilt stream still reads back formulas after clear");
    }
    out = std::move(built);
    return true;
}

bool parse_mspj_field(std::string_view token, std::uint32_t& field_id) {
    std::string_view text = token;
    if (!text.empty() && text.front() == '[' && text.back() == ']') {
        text.remove_prefix(1);
        text.remove_suffix(1);
    }
    if (text.size() <= 4u || text.substr(0, 4) != "MSPJ") {
        return false;
    }
    text.remove_prefix(4);
    std::uint64_t value = 0;
    bool any = false;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        any = true;
        value = value * 10u + static_cast<std::uint64_t>(ch - '0');
        if (value > 0xffffffffull) {
            return false;
        }
    }
    if (!any) {
        return false;
    }
    field_id = static_cast<std::uint32_t>(value);
    return true;
}

std::string formula_with_field_ids(std::string_view formula) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    std::size_t pos = 0;
    while (pos < formula.size()) {
        const std::size_t open = formula.find('[', pos);
        if (open == std::string_view::npos) {
            out.append(formula.substr(pos));
            break;
        }
        const std::size_t close = formula.find(']', open + 1);
        if (close == std::string_view::npos) {
            out.append(formula.substr(pos));
            break;
        }
        out.append(formula.substr(pos, open - pos));
        const std::string_view token = formula.substr(open, close - open + 1);
        std::uint32_t field_id = 0;
        if (parse_mspj_field(token, field_id)) {
            out += "[0x";
            for (int shift = 28; shift >= 0; shift -= 4) {
                out.push_back(kDigits[(field_id >> static_cast<unsigned>(shift)) & 0x0fu]);
            }
            out.push_back(']');
        } else {
            out.append(token);
        }
        pos = close + 1;
    }
    return out;
}

std::vector<TailProperty> tail_properties(const PropsDocument& document) {
    std::vector<TailProperty> out;
    std::size_t pos = document.tail_offset;
    while (pos + 16u <= document.bytes.size()) {
        const std::uint32_t lead = read_u32(document.bytes, pos);
        const std::uint32_t key = read_u32(document.bytes, pos + 4u);
        const std::uint32_t lead_two = read_u32(document.bytes, pos + 8u);
        const std::uint32_t value = read_u32(document.bytes, pos + 12u);
        if (lead != 4u || lead_two != 4u || key == 0u) {
            break;
        }
        TailProperty property;
        property.key = key;
        property.value = value;
        property.offset = static_cast<std::uint32_t>(pos);
        out.push_back(property);
        pos += 16u;
    }
    return out;
}

std::vector<FieldReference> field_references(const PropsDocument& document) {
    std::vector<FieldReference> out;
    const std::size_t size = document.bytes.size();
    for (std::size_t pos = document.tail_offset; pos + 4u <= size; pos += 2u) {
        const std::uint32_t id = read_u32(document.bytes, pos);
        if ((id & 0xffff0000u) != 0x0b400000u) {
            continue;
        }
        if (pos < document.tail_offset + 8u) {
            continue;
        }
        const std::size_t entry = pos - 8u;
        if (entry + 28u > size) {
            continue;
        }
        const std::uint32_t length_word = read_u32(document.bytes, entry + 4u);
        if (length_word < 4u || length_word > 4096u) {
            continue;
        }
        FieldReference reference;
        reference.entry_offset = static_cast<std::uint32_t>(entry);
        reference.index_word = read_u32(document.bytes, entry);
        reference.length_word = length_word;
        reference.field_id = id;
        for (std::size_t word = 0; word < reference.words.size(); ++word) {
            reference.words[word] = read_u32(document.bytes, entry + 12u + word * 4u);
        }
        out.push_back(reference);
    }
    return out;
}

std::string describe_tail(const PropsDocument& document) {
    std::ostringstream out;
    out << "tail offset=" << document.tail_offset
        << " length=" << document.tail_size() << "\n";
    const auto properties = tail_properties(document);
    out << "typed properties: " << properties.size() << "\n";
    for (const auto& property : properties) {
        out << "  key=0x" << std::hex << property.key << std::dec
            << " value=" << property.value << " (0x" << std::hex << property.value
            << std::dec << ") at +" << (property.offset - document.tail_offset) << "\n";
    }
    const auto references = field_references(document);
    out << "field-reference entries: " << references.size();
    if (!references.empty()) {
        out << " first id=0x" << std::hex << references.front().field_id
            << std::dec << " at +" << (references.front().entry_offset - document.tail_offset)
            << " last id=0x" << std::hex << references.back().field_id << std::dec;
    }
    out << "\n";
    return out.str();
}

}  // namespace msproject::props
