#include "props.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <sstream>
#include <utility>

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

[[nodiscard]] std::uint32_t read_u16(const std::vector<std::uint8_t>& bytes,
                                     std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        return 0u;
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u);
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
