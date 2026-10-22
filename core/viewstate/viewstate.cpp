#include "viewstate.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <sstream>
#include <utility>

namespace msproject::viewstate {
namespace {

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset]) |
                                      (static_cast<std::uint16_t>(data[offset + 1]) << 8u));
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

std::string hex32(std::uint32_t value) {
    char buffer[11] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
    return std::string(buffer);
}

std::string byte_hex(const std::uint8_t* data, std::size_t count) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(digits[(data[i] >> 4u) & 0x0fu]);
        out.push_back(digits[data[i] & 0x0fu]);
    }
    return out;
}

bool meta_header_ok(std::span<const std::uint8_t> data,
                    std::uint32_t& magic,
                    ParseError* error,
                    std::string_view what) {
    if (data.size() < kFixedMetaHeaderSize) {
        if (error != nullptr) {
            error->message = std::string(what) + " is shorter than 16 bytes";
            error->offset = data.size();
        }
        return false;
    }
    magic = read_u32(data, 0);
    if (magic != kMetaMagic) {
        if (error != nullptr) {
            error->message = std::string(what) + " has magic " + hex32(magic) +
                             ", expected 0xfadfadba";
            error->offset = 0;
        }
        return false;
    }
    return true;
}

bool write_fail(WriteError* error, std::string message) {
    if (error != nullptr) {
        error->message = std::move(message);
    }
    return false;
}

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

}  // namespace

std::span<const std::uint8_t> ViewState::block_bytes(const VarBlock& block) const {
    if (block.offset + 4 > var2_data.size()) {
        return {};
    }
    const std::size_t start = static_cast<std::size_t>(block.offset) + 4;
    const std::size_t length = std::min<std::size_t>(block.payload_length,
                                                     var2_data.size() - start);
    return std::span<const std::uint8_t>(var2_data.data() + start, length);
}

const ViewBlockRef* ViewState::find_block_ref(std::uint32_t uid, std::uint32_t key) const {
    for (const auto& ref : block_refs) {
        if (ref.uid == uid && ref.key == key) {
            return &ref;
        }
    }
    return nullptr;
}

bool is_known_class(std::string_view klass) {
    return klass == "CTable" || klass == "CV_iew" || klass == "CEdl" ||
           klass == "CFilter" || klass == "CGrouping" || klass == "CDrawing" ||
           klass == "CMap" || klass == "CReport";
}

std::string_view block_kind(std::string_view klass, std::uint32_t key) {
    if (key == kColumnBlockKey) {
        if (klass == "CTable") {
            return "table-columns";
        }
        if (klass == "CV_iew") {
            return "view-state";
        }
        if (klass == "CEdl") {
            return "props-document";
        }
        return "opaque";
    }
    if (key == kViewNotesKey) {
        return "view-notes";
    }
    if (key == kTimelineViewKey) {
        return "timeline-view";
    }
    return "opaque";
}

std::string utf16_ascii(std::span<const std::uint8_t> data,
                        std::size_t offset,
                        std::size_t max_units) {
    std::string out;
    for (std::size_t unit = 0; unit < max_units; ++unit) {
        const std::size_t at = offset + unit * 2;
        if (at + 2 > data.size()) {
            break;
        }
        const std::uint16_t value = read_u16(data, at);
        if (value == 0) {
            break;
        }
        if (value < 0x20 || value > 0x7e) {
            break;
        }
        out.push_back(static_cast<char>(value));
    }
    return out;
}

std::string preview_bytes(std::span<const std::uint8_t> data, std::size_t limit) {
    std::size_t ascii_start = 0;
    std::size_t ascii_len = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        const std::uint8_t value = data[i];
        if (value >= 0x20 && value <= 0x7e) {
            if (ascii_len == 0) {
                ascii_start = i;
            }
            ++ascii_len;
            if (ascii_len >= limit) {
                break;
            }
        } else {
            ascii_len = 0;
        }
    }
    if (ascii_len >= 4) {
        const std::size_t count = std::min<std::size_t>(ascii_len, limit);
        return std::string(reinterpret_cast<const char*>(data.data() + ascii_start), count);
    }

    std::size_t utf16_start = 0;
    std::size_t utf16_len = 0;
    for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
        const bool printable = data[i] >= 0x20 && data[i] <= 0x7e && data[i + 1] == 0;
        if (printable) {
            if (utf16_len == 0) {
                utf16_start = i;
            }
            ++utf16_len;
            if (utf16_len >= limit) {
                break;
            }
        } else {
            utf16_len = 0;
        }
    }
    if (utf16_len >= 4) {
        return utf16_ascii(data, utf16_start, std::min<std::size_t>(utf16_len, limit));
    }

    const std::size_t count = std::min<std::size_t>(data.size(), 12);
    return byte_hex(data.data(), count);
}

bool parse_fixed_meta(std::span<const std::uint8_t> data,
                      FixedMetaStream& out,
                      ParseError* error) {
    out = FixedMetaStream{};
    std::uint32_t magic = 0;
    if (!meta_header_ok(data, magic, error, "FixedMeta")) {
        return false;
    }
    out.magic = magic;
    out.flags = read_u32(data, 4);
    out.declared_count = read_u32(data, 8);
    out.fixed_data_size = read_u32(data, 12);

    const std::size_t available = data.size() - kFixedMetaHeaderSize;
    if (out.declared_count == 0) {
        out.entry_size = 0;
        if (available != 0) {
            if (error != nullptr) {
                error->message = "FixedMeta declares 0 entries but holds " +
                                 std::to_string(available) + " trailing bytes";
                error->offset = kFixedMetaHeaderSize;
            }
            return false;
        }
        return true;
    }
    if (available % out.declared_count != 0) {
        if (error != nullptr) {
            error->message = "FixedMeta size is not 16 + declared_count * entry_size (" +
                             std::to_string(out.declared_count) + " entries, " +
                             std::to_string(available) + " bytes)";
            error->offset = kFixedMetaHeaderSize;
        }
        return false;
    }
    out.entry_size = static_cast<std::uint32_t>(available / out.declared_count);
    if (out.entry_size < 8) {
        if (error != nullptr) {
            error->message = "FixedMeta entry size " + std::to_string(out.entry_size) +
                             " is too small to hold state/var_count/offset";
            error->offset = kFixedMetaHeaderSize;
        }
        return false;
    }
    out.entries.reserve(out.declared_count);
    for (std::uint32_t i = 0; i < out.declared_count; ++i) {
        FixedMetaEntry entry;
        entry.entry_offset = static_cast<std::uint32_t>(kFixedMetaHeaderSize) +
                             i * out.entry_size;
        const std::size_t base = entry.entry_offset;
        entry.state = read_u16(data, base);
        entry.var_count = read_u16(data, base + 2);
        entry.fixed_offset = read_u32(data, base + 4);
        entry.opaque_tail.assign(data.begin() + static_cast<std::ptrdiff_t>(base + 8),
                                 data.begin() + static_cast<std::ptrdiff_t>(
                                                     base + out.entry_size));
        out.entries.push_back(std::move(entry));
    }
    return true;
}

bool parse_var_meta(std::span<const std::uint8_t> data,
                    VarMetaStream& out,
                    ParseError* error) {
    out = VarMetaStream{};
    std::uint32_t magic = 0;
    if (!meta_header_ok(data, magic, error, "VarMeta")) {
        return false;
    }
    out.magic = magic;
    out.declared_count = read_u32(data, 8);
    out.var2_size = read_u32(data, 20);

    const std::size_t available = data.size() - kVarMetaHeaderSize;
    if (out.declared_count > available / 12) {
        if (error != nullptr) {
            error->message = "VarMeta declares " + std::to_string(out.declared_count) +
                             " entries but holds only " + std::to_string(available) + " bytes";
            error->offset = kVarMetaHeaderSize;
        }
        return false;
    }
    const std::size_t used = static_cast<std::size_t>(out.declared_count) * 12;
    out.trailing_bytes = static_cast<std::uint32_t>(available - used);
    out.entries.reserve(out.declared_count);
    for (std::uint32_t i = 0; i < out.declared_count; ++i) {
        const std::size_t base = kVarMetaHeaderSize + static_cast<std::size_t>(i) * 12;
        VarMetaEntry entry;
        entry.uid = read_u32(data, base);
        entry.offset = read_u32(data, base + 4);
        entry.key = read_u32(data, base + 8);
        out.entries.push_back(entry);
    }
    return true;
}

bool parse_var2(std::span<const std::uint8_t> data,
                Var2Stream& out,
                ParseError* error) {
    out = Var2Stream{};
    out.size = static_cast<std::uint32_t>(data.size());
    std::size_t offset = 0;
    while (offset < data.size()) {
        if (offset + 4 > data.size()) {
            if (error != nullptr) {
                error->message = "Var2Data has " + std::to_string(data.size() - offset) +
                                 " trailing byte(s) where a block length was expected";
                error->offset = offset;
            }
            return false;
        }
        const std::uint32_t length = read_u32(data, offset);
        if (length > data.size() - offset - 4) {
            if (error != nullptr) {
                error->message = "Var2Data block at " + std::to_string(offset) + " declares " +
                                 std::to_string(length) + " payload bytes but only " +
                                 std::to_string(data.size() - offset - 4) + " remain";
                error->offset = offset;
            }
            return false;
        }
        VarBlock block;
        block.offset = static_cast<std::uint32_t>(offset);
        block.payload_length = length;
        out.blocks.push_back(block);
        offset += 4 + static_cast<std::size_t>(length);
    }
    return true;
}

bool parse_view_records(std::span<const std::uint8_t> payload,
                        std::vector<ViewRecord>& out,
                        ParseError* error) {
    out.clear();
    if (payload.size() < kViewMainHeaderSize) {
        if (error != nullptr) {
            error->message = "CV_iew main block is " + std::to_string(payload.size()) +
                             " bytes, shorter than its 16-byte header";
            error->offset = payload.size();
        }
        return false;
    }
    const std::uint32_t size_word = read_u32(payload, 0);
    // The item count is a u16 at +12: three corpus blocks carry a non-zero word
    // at +14, which a u32 read would turn into a huge count.
    const std::uint16_t count = read_u16(payload, 12);
    if (size_word != payload.size() - 4) {
        if (error != nullptr) {
            error->message = "CV_iew main block size word is " + std::to_string(size_word) +
                             " but the payload is " + std::to_string(payload.size()) + " bytes";
            error->offset = 0;
        }
        return false;
    }
    if (static_cast<std::size_t>(count) >
        (payload.size() - kViewMainHeaderSize) / kViewItemHeaderSize) {
        if (error != nullptr) {
            error->message = "CV_iew main block declares " + std::to_string(count) +
                             " items but is too short to hold them";
            error->offset = 12;
        }
        return false;
    }
    std::size_t pos = kViewMainHeaderSize;
    const std::uint32_t item_count = count;
    out.reserve(item_count);
    for (std::uint32_t i = 0; i < item_count; ++i) {
        if (pos + kViewItemHeaderSize > payload.size()) {
            if (error != nullptr) {
                error->message = "CV_iew item " + std::to_string(i) + " starts past the block end";
                error->offset = pos;
            }
            return false;
        }
        const std::uint32_t length = read_u32(payload, pos);
        const std::uint32_t id = read_u32(payload, pos + 4);
        const std::uint32_t kind = read_u32(payload, pos + 8);
        if (!props::is_props_id(id)) {
            if (error != nullptr) {
                error->message = "CV_iew item " + std::to_string(i) + " has id " + hex32(id) +
                                 ", which is not a view property id";
                error->offset = pos;
            }
            return false;
        }
        if (length > payload.size() - pos - kViewItemHeaderSize) {
            if (error != nullptr) {
                error->message = "CV_iew item " + std::to_string(i) + " declares " +
                                 std::to_string(length) + " payload bytes but only " +
                                 std::to_string(payload.size() - pos - kViewItemHeaderSize) +
                                 " remain";
                error->offset = pos;
            }
            return false;
        }
        ViewRecord record;
        record.record_offset = static_cast<std::uint32_t>(pos);
        record.payload_length = length;
        record.id = id;
        record.kind = kind;
        record.preview = preview_bytes(payload.subspan(pos + kViewItemHeaderSize, length));
        out.push_back(std::move(record));

        // Measured: an odd payload length is followed by one pad byte, so the
        // next item starts on an even offset (the header is 12 bytes).
        std::size_t stride = kViewItemHeaderSize + static_cast<std::size_t>(length);
        if ((stride & 1u) != 0) {
            ++stride;
        }
        if (pos + stride > payload.size()) {
            // Tolerate a missing pad after the LAST item only.
            if (i + 1 == item_count && pos + kViewItemHeaderSize + length == payload.size()) {
                pos = payload.size();
                continue;
            }
            if (error != nullptr) {
                error->message = "CV_iew item " + std::to_string(i) + " runs past the block end";
                error->offset = pos;
            }
            return false;
        }
        pos += stride;
    }
    if (pos != payload.size()) {
        if (error != nullptr) {
            error->message = "CV_iew main block has " + std::to_string(payload.size() - pos) +
                             " trailing byte(s) after " + std::to_string(count) + " item(s)";
            error->offset = pos;
        }
        return false;
    }
    return true;
}

bool parse_view_state(std::string_view klass,
                      const PartSet& parts,
                      ViewState& out,
                      ParseError* error) {
    out = ViewState{};
    out.klass = std::string(klass);
    out.fixed_data.assign(parts.fixed_data.begin(), parts.fixed_data.end());
    out.fixed_meta_bytes.assign(parts.fixed_meta.begin(), parts.fixed_meta.end());
    out.var_meta_bytes.assign(parts.var_meta.begin(), parts.var_meta.end());
    out.var2_data.assign(parts.var2_data.begin(), parts.var2_data.end());

    if (!parse_fixed_meta(out.fixed_meta_bytes, out.fixed_meta, error)) {
        return false;
    }
    if (!parse_var_meta(out.var_meta_bytes, out.var_meta, error)) {
        return false;
    }
    if (!parse_var2(out.var2_data, out.var2, error)) {
        return false;
    }

    if (out.var_meta.var2_size != out.var2.size) {
        out.warnings.push_back("VarMeta declares var2_size " +
                               std::to_string(out.var_meta.var2_size) +
                               " but Var2Data is " + std::to_string(out.var2.size) + " bytes");
    }
    if (out.var_meta.trailing_bytes != 0) {
        out.warnings.push_back("VarMeta has " + std::to_string(out.var_meta.trailing_bytes) +
                               " trailing byte(s) after the declared entries");
    }
    if (out.fixed_meta.fixed_data_size != out.fixed_data.size()) {
        out.warnings.push_back("FixedMeta declares fixed_data_size " +
                               std::to_string(out.fixed_meta.fixed_data_size) +
                               " but FixedData is " + std::to_string(out.fixed_data.size()) +
                               " bytes");
    }

    // A single row means the row is the whole stream; multiple rows are derived
    // from the first two FixedMeta offsets, then validated against every entry.
    if (!out.fixed_meta.entries.empty()) {
        if (out.fixed_meta.entries.size() == 1) {
            out.row_size = out.fixed_meta.fixed_data_size;
        } else if (out.fixed_meta.entries[1].fixed_offset >
                   out.fixed_meta.entries[0].fixed_offset) {
            out.row_size = out.fixed_meta.entries[1].fixed_offset -
                           out.fixed_meta.entries[0].fixed_offset;
        } else {
            out.warnings.push_back("FixedMeta entry offsets are not increasing; row size unknown");
        }
    }

    std::size_t reported_offset_mismatches = 0;
    if (out.row_size > 0) {
        const std::size_t row_count = out.fixed_data.size() / out.row_size;
        if (out.fixed_data.size() % out.row_size != 0) {
            out.warnings.push_back("FixedData size is not a multiple of the derived row size " +
                                   std::to_string(out.row_size));
        }
        for (std::size_t i = 0; i < row_count; ++i) {
            const std::size_t base = i * out.row_size;
            FixedRow row;
            row.row_index = static_cast<std::uint32_t>(i);
            row.ordinal = read_u32(std::span<const std::uint8_t>(out.fixed_data), base);
            const std::size_t caption_capacity =
                out.row_size > 4 ? std::min<std::size_t>(128, out.row_size - 4) : 0;
            row.caption = utf16_ascii(out.fixed_data, base + 4, caption_capacity / 2);
            if (out.row_size == kDisplayCaptionRowSize) {
                row.display_caption = utf16_ascii(out.fixed_data,
                                                  base + kDisplayCaptionOffset, 39);
            }
            if (out.row_size >= kIdentitySize) {
                const std::size_t identity = base + out.row_size - kIdentitySize;
                std::copy(out.fixed_data.begin() + static_cast<std::ptrdiff_t>(identity),
                          out.fixed_data.begin() + static_cast<std::ptrdiff_t>(identity + 16),
                          row.identity.begin());
            }
            out.rows.push_back(std::move(row));
        }
        for (std::size_t i = 0; i < out.fixed_meta.entries.size(); ++i) {
            const std::uint32_t expected = static_cast<std::uint32_t>(i * out.row_size);
            if (out.fixed_meta.entries[i].fixed_offset != expected &&
                reported_offset_mismatches < 4) {
                out.warnings.push_back("FixedMeta entry " + std::to_string(i) +
                                       " offset " + std::to_string(
                                                           out.fixed_meta.entries[i].fixed_offset) +
                                       " is not the contiguous row offset " +
                                       std::to_string(expected));
                ++reported_offset_mismatches;
            }
        }
    } else if (!out.fixed_data.empty()) {
        out.warnings.push_back("FixedData is non-empty but no row size could be derived");
    }

    // Resolve every VarMeta entry to the block it names.  Blocks are walked in
    // stream order, while VarMeta entries are grouped by uid, so the two orders
    // differ (measured on CV_iew).
    out.blocks = out.var2.blocks;
    out.block_refs.assign(out.blocks.size(), ViewBlockRef{});
    std::vector<bool> referenced(out.blocks.size(), false);
    for (const auto& entry : out.var_meta.entries) {
        std::size_t found = out.blocks.size();
        for (std::size_t i = 0; i < out.blocks.size(); ++i) {
            if (out.blocks[i].offset == entry.offset) {
                found = i;
                break;
            }
        }
        if (found == out.blocks.size()) {
            out.warnings.push_back("VarMeta entry (uid " + std::to_string(entry.uid) + ", key " +
                                   hex32(entry.key) + ") points at offset " +
                                   std::to_string(entry.offset) +
                                   ", which is not a block start");
            continue;
        }
        if (referenced[found]) {
            out.warnings.push_back("VarMeta entry (uid " + std::to_string(entry.uid) + ", key " +
                                   hex32(entry.key) + ") repeats block offset " +
                                   std::to_string(entry.offset));
            continue;
        }
        referenced[found] = true;
        ViewBlockRef& ref = out.block_refs[found];
        ref.uid = entry.uid;
        ref.key = entry.key;
        ref.offset = out.blocks[found].offset;
        ref.payload_length = out.blocks[found].payload_length;
        ref.kind = std::string(block_kind(out.klass, entry.key));
        ref.preview = preview_bytes(out.block_bytes(out.blocks[found]));
        if (!out.rows.empty() &&
            (entry.uid == 0 ||
             std::none_of(out.rows.begin(), out.rows.end(),
                          [uid = entry.uid](const FixedRow& row) { return row.ordinal == uid; }))) {
            out.warnings.push_back("VarMeta entry uid " + std::to_string(entry.uid) +
                                   " has no fixed row ordinal (rows: " +
                                   std::to_string(out.rows.size()) + ")");
        }
    }
    for (std::size_t i = 0; i < out.blocks.size(); ++i) {
        if (referenced[i]) {
            continue;
        }
        ViewBlockRef& ref = out.block_refs[i];
        ref.offset = out.blocks[i].offset;
        ref.payload_length = out.blocks[i].payload_length;
        ref.kind = "unreferenced";
        ref.preview = preview_bytes(out.block_bytes(out.blocks[i]));
        out.warnings.push_back("Var2Data block at " + std::to_string(out.blocks[i].offset) +
                               " is not named by any VarMeta entry");
    }

    if (out.klass == "CTable") {
        for (std::size_t i = 0; i < out.rows.size(); ++i) {
            TableDef table;
            table.row_index = static_cast<std::uint32_t>(i);
            table.ordinal = out.rows[i].ordinal;
            table.caption = out.rows[i].caption;
            table.display_caption = out.rows[i].display_caption;
            table.identity = out.rows[i].identity;

            // VarMeta uid is the row's ORDINAL, not its row index: the corpus
            // fixtures skip ordinals (13 and 22 are absent in 22 rows), so
            // matching on i+1 would shift every later row.
            const ViewBlockRef* ref = out.find_block_ref(table.ordinal, kColumnBlockKey);
            if (ref == nullptr) {
                out.warnings.push_back("table row " + std::to_string(i + 1) +
                                       " has no 0x20400006 column block");
                out.tables.push_back(std::move(table));
                continue;
            }
            const VarBlock* block = nullptr;
            for (const auto& candidate : out.blocks) {
                if (candidate.offset == ref->offset) {
                    block = &candidate;
                    break;
                }
            }
            if (block == nullptr) {
                out.warnings.push_back("table row " + std::to_string(i + 1) +
                                       " lost its column block");
                out.tables.push_back(std::move(table));
                continue;
            }
            table.block_offset = block->offset;
            table.block_length = block->payload_length;
            const std::span<const std::uint8_t> payload = out.block_bytes(*block);
            if (payload.size() < kTableBlockHeaderSize) {
                out.warnings.push_back("table row " + std::to_string(i + 1) + " column block is " +
                                       std::to_string(payload.size()) + " bytes, shorter than 12");
                out.tables.push_back(std::move(table));
                continue;
            }
            table.header_ordinal = read_u32(payload, 0);
            table.last_column_index = read_u16(payload, 4);
            table.format_word = payload[6];
            table.date_format = payload[7];
            table.flags = read_u32(payload, 8);
            if (table.header_ordinal != table.ordinal) {
                out.warnings.push_back("table row " + std::to_string(i + 1) + " ordinal " +
                                       std::to_string(table.ordinal) +
                                       " disagrees with block ordinal " +
                                       std::to_string(table.header_ordinal));
            }
            const std::size_t declared_columns =
                static_cast<std::size_t>(table.last_column_index) + 1;
            const std::size_t expected =
                kTableBlockHeaderSize + declared_columns * kColumnRecordSize;
            if (expected != payload.size()) {
                out.warnings.push_back("table row " + std::to_string(i + 1) +
                                       " column block is " + std::to_string(payload.size()) +
                                       " bytes but its header implies " +
                                       std::to_string(declared_columns) + " columns (" +
                                       std::to_string(expected) + " bytes); left opaque");
                out.tables.push_back(std::move(table));
                continue;
            }
            table.columns.reserve(declared_columns);
            for (std::size_t c = 0; c < declared_columns; ++c) {
                ColumnDef column;
                const std::size_t record =
                    kTableBlockHeaderSize + c * kColumnRecordSize;
                column.record_offset = static_cast<std::uint32_t>(record);
                column.field_id = read_u32(payload, record);
                for (std::size_t b = 0; b < column.opaque_header.size(); ++b) {
                    column.opaque_header[b] = payload[record + 4 + b];
                }
                column.title = utf16_ascii(payload, record + kColumnTitleOffset,
                                           kColumnTitleCapacity);
                table.columns.push_back(std::move(column));
            }
            table.decoded = true;
            out.tables.push_back(std::move(table));
        }
    } else if (out.klass == "CV_iew") {
        for (std::size_t i = 0; i < out.rows.size(); ++i) {
            ViewDef view;
            view.row_index = static_cast<std::uint32_t>(i);
            view.ordinal = out.rows[i].ordinal;
            view.name = out.rows[i].caption;
            view.identity = out.rows[i].identity;
            for (const auto& ref : out.block_refs) {
                if (ref.uid == view.ordinal) {
                    view.blocks.push_back(ref);
                }
            }
            // Decode the main view-state block (wave 15, W62).  A block that
            // does not walk stays opaque with a warning: the collection is
            // still readable and the encoder copies the block verbatim.
            for (const auto& block : view.blocks) {
                if (block.key != kColumnBlockKey) {
                    continue;
                }
                const VarBlock* var_block = nullptr;
                for (const auto& candidate : out.blocks) {
                    if (candidate.offset == block.offset) {
                        var_block = &candidate;
                        break;
                    }
                }
                if (var_block == nullptr) {
                    out.warnings.push_back("view row " + std::to_string(i + 1) +
                                           " lost its 0x20400006 main block");
                    break;
                }
                view.main_block_offset = var_block->offset;
                view.main_payload_length = var_block->payload_length;
                std::vector<ViewRecord> records;
                ParseError record_error;
                if (parse_view_records(out.block_bytes(*var_block), records, &record_error)) {
                    view.main_decoded = true;
                    view.records = std::move(records);
                    for (const auto& record : view.records) {
                        if (record.id == kViewNameKey) {
                            view.display_name = utf16_ascii(
                                out.block_bytes(*var_block),
                                record.record_offset + kViewItemHeaderSize,
                                record.payload_length / 2);
                        } else if (record.id == kViewTableKey) {
                            view.table_or_top = utf16_ascii(
                                out.block_bytes(*var_block),
                                record.record_offset + kViewItemHeaderSize,
                                record.payload_length / 2);
                        } else if (record.id == kViewFilterKey) {
                            view.filter_or_bottom = utf16_ascii(
                                out.block_bytes(*var_block),
                                record.record_offset + kViewItemHeaderSize,
                                record.payload_length / 2);
                        } else if (record.id == kViewGroupKey) {
                            view.group_name = utf16_ascii(
                                out.block_bytes(*var_block),
                                record.record_offset + kViewItemHeaderSize,
                                record.payload_length / 2);
                        } else if (record.id == kViewOptionsKey &&
                                   record.payload_length == kViewOptionsSize) {
                            const std::span<const std::uint8_t> payload = out.block_bytes(*var_block);
                            const std::size_t base = record.record_offset + kViewItemHeaderSize;
                            view.options_decoded = true;
                            for (std::size_t margin = 0; margin < view.page_margins_thousandths.size();
                                 ++margin) {
                                view.page_margins_thousandths[margin] =
                                    read_u32(payload, base + margin * 4);
                            }
                            view.page_margin_extra = read_u32(payload, base + 16);
                        }
                    }
                } else {
                    out.warnings.push_back("view row " + std::to_string(i + 1) +
                                           " main block is opaque: " + record_error.message);
                }
                break;
            }
            // Notes: nine u32 chunk lengths followed by the chunks.
            for (const auto& block : view.blocks) {
                if (block.key != kViewNotesKey) {
                    continue;
                }
                const VarBlock* var_block = nullptr;
                for (const auto& candidate : out.blocks) {
                    if (candidate.offset == block.offset) {
                        var_block = &candidate;
                        break;
                    }
                }
                if (var_block == nullptr) {
                    break;
                }
                const std::span<const std::uint8_t> payload = out.block_bytes(*var_block);
                if (payload.size() >= kViewNotesHeaderSize) {
                    std::uint64_t total = 0;
                    std::array<std::uint32_t, kViewNotesChunks> lengths{};
                    for (std::size_t chunk = 0; chunk < kViewNotesChunks; ++chunk) {
                        lengths[chunk] = read_u32(payload, chunk * 4);
                        total += lengths[chunk];
                    }
                    if (total + kViewNotesHeaderSize == payload.size()) {
                        view.notes_decoded = true;
                        view.notes_chunk_lengths = lengths;
                        std::size_t at = kViewNotesHeaderSize;
                        for (std::size_t chunk = 0; chunk < kViewNotesChunks; ++chunk) {
                            view.notes_previews[chunk] =
                                preview_bytes(payload.subspan(at, lengths[chunk]), 40);
                            at += lengths[chunk];
                        }
                    }
                }
                break;
            }
            out.views.push_back(std::move(view));
        }
        // Inference: a combination view stores view names (not a table name) in
        // 0x2240000a.  If that name matches another view's name, mark it.
        for (auto& view : out.views) {
            if (view.table_or_top.empty()) {
                continue;
            }
            const auto normalize = [](std::string_view text) {
                std::string out_text;
                out_text.reserve(text.size());
                for (const char ch : text) {
                    if (ch != '&') {
                        out_text.push_back(ch);
                    }
                }
                return out_text;
            };
            const std::string needle = normalize(view.table_or_top);
            for (const auto& other : out.views) {
                if (&other == &view) {
                    continue;
                }
                if (normalize(other.name) == needle ||
                    (!other.display_name.empty() && normalize(other.display_name) == needle)) {
                    view.looks_like_combination = true;
                    break;
                }
            }
        }
    } else if (out.klass == "CEdl") {
        // Measured: CEdl's single Var2Data block payload is a complete
        // Props-format document (header, records, no tail).  Parse it with
        // core/props instead of re-implementing the record framing.
        for (const auto& block : out.blocks) {
            props::PropsDocument document;
            props::ParseError props_error;
            if (props::parse_props(out.block_bytes(block), document, &props_error)) {
                EmbeddedProps embedded;
                embedded.block_offset = block.offset;
                embedded.document = std::move(document);
                out.embedded_props.push_back(std::move(embedded));
            } else {
                out.warnings.push_back("CEdl block at " + std::to_string(block.offset) +
                                       " did not parse as a Props document: " +
                                       props_error.message);
            }
        }
    }
    out.parsed = true;
    return true;
}

std::string describe_view_state(const ViewState& state,
                                std::size_t max_columns,
                                std::size_t max_blocks) {
    std::ostringstream out;
    out << "class " << state.klass << ": fixed_data=" << state.fixed_data.size()
        << " fixed_meta=" << state.fixed_meta_bytes.size()
        << " var_meta=" << state.var_meta_bytes.size()
        << " var2=" << state.var2_data.size() << "\n";
    out << "  fixed_meta: entries=" << state.fixed_meta.declared_count
        << " entry_size=" << state.fixed_meta.entry_size
        << " data_size=" << state.fixed_meta.fixed_data_size
        << " flags=" << state.fixed_meta.flags << "\n";
    out << "  var_meta: entries=" << state.var_meta.declared_count
        << " var2_size=" << state.var_meta.var2_size
        << " trailing=" << state.var_meta.trailing_bytes << "\n";
    out << "  blocks=" << state.blocks.size() << " rows=" << state.rows.size()
        << " row_size=" << state.row_size << "\n";
    for (const auto& warning : state.warnings) {
        out << "  warning: " << warning << "\n";
    }
    for (const auto& table : state.tables) {
        out << "  table " << table.ordinal << " \"" << table.caption << "\"";
        if (!table.display_caption.empty()) {
            out << " display=\"" << table.display_caption << "\"";
        }
        out << " columns=" << table.columns.size();
        if (table.decoded) {
            out << " date_format=" << hex32(table.date_format) << " flags=" << table.flags
                << " block=" << table.block_offset << "+" << table.block_length;
        } else {
            out << " undecoded";
        }
        out << "\n";
        std::size_t shown = 0;
        for (const auto& column : table.columns) {
            if (max_columns != 0 && shown >= max_columns) {
                out << "    ... " << (table.columns.size() - shown) << " more column(s)\n";
                break;
            }
            out << "    col " << shown << " field=" << hex32(column.field_id) << " title=\""
                << column.title << "\" header="
                << byte_hex(column.opaque_header.data(), column.opaque_header.size()) << "\n";
            ++shown;
        }
    }
    for (const auto& view : state.views) {
        out << "  view " << view.ordinal << " \"" << view.name << "\" blocks="
            << view.blocks.size() << "\n";
        if (view.main_decoded) {
            out << "    main block=" << view.main_payload_length << " bytes items="
                << view.records.size();
            if (!view.display_name.empty()) {
                out << " name=\"" << view.display_name << "\"";
            }
            out << "\n";
            if (!view.table_or_top.empty()) {
                out << "      table-or-top=\"" << view.table_or_top << "\""
                    << (view.looks_like_combination ? " (combination-view inference)" : "")
                    << "\n";
            }
            if (!view.filter_or_bottom.empty()) {
                out << "      filter-or-bottom=\"" << view.filter_or_bottom << "\"\n";
            }
            if (!view.group_name.empty()) {
                out << "      group=\"" << view.group_name << "\"\n";
            }
            if (view.options_decoded) {
                out << "      options=970 bytes margins=" << view.page_margins_thousandths[0]
                    << "," << view.page_margins_thousandths[1] << ","
                    << view.page_margins_thousandths[2] << ","
                    << view.page_margins_thousandths[3]
                    << " extra=" << view.page_margin_extra << "\n";
            }
            if (view.notes_decoded) {
                out << "      notes=9 chunks ";
                for (std::size_t chunk = 0; chunk < view.notes_chunk_lengths.size(); ++chunk) {
                    out << view.notes_chunk_lengths[chunk];
                    if (chunk + 1 != view.notes_chunk_lengths.size()) {
                        out << ",";
                    }
                }
                out << "\n";
            }
            std::size_t shown_records = 0;
            for (const auto& record : view.records) {
                if (max_blocks != 0 && shown_records >= max_blocks) {
                    out << "      ... " << (view.records.size() - shown_records)
                        << " more item(s)\n";
                    break;
                }
                out << "      item off=" << record.record_offset << " id=" << hex32(record.id)
                    << " kind=" << record.kind << " len=" << record.payload_length
                    << " preview=\"" << record.preview << "\"\n";
                ++shown_records;
            }
        } else {
            out << "    main block not item-decoded\n";
        }
        std::size_t shown = 0;
        for (const auto& block : view.blocks) {
            if (max_blocks != 0 && shown >= max_blocks) {
                out << "    ... " << (view.blocks.size() - shown) << " more block(s)\n";
                break;
            }
            out << "    key=" << hex32(block.key) << " off=" << block.offset
                << " len=" << block.payload_length << " kind=" << block.kind
                << " preview=\"" << block.preview << "\"\n";
            ++shown;
        }
    }
    for (const auto& embedded : state.embedded_props) {
        out << "  props block off=" << embedded.block_offset
            << " records=" << embedded.document.records.size()
            << " tail=" << embedded.document.tail_size() << "\n";
    }
    return out.str();
}

namespace {

// Validates that `table_index` names a decoded CTable whose column block is
// wholly inside Var2Data, and returns it with the block payload's stream
// offset.  Shared by both mutators so they cannot disagree about the target.
const TableDef* writable_table(const ViewState& state,
                               std::size_t table_index,
                               WriteError* error,
                               std::size_t& payload_start) {
    if (!state.parsed) {
        write_fail(error,
                   "view state did not parse: parse_view_state() must succeed before an edit");
        return nullptr;
    }
    if (table_index >= state.tables.size()) {
        write_fail(error, "table index " + std::to_string(table_index) + " is out of range (" +
                              std::to_string(state.tables.size()) + " table(s))");
        return nullptr;
    }
    const TableDef& table = state.tables[table_index];
    if (!table.decoded) {
        write_fail(error, "table " + std::to_string(table_index) +
                              " has no decoded 0x20400006 column block");
        return nullptr;
    }
    const std::size_t start = static_cast<std::size_t>(table.block_offset) + 4;
    if (start > state.var2_data.size() ||
        static_cast<std::size_t>(table.block_length) < kTableBlockHeaderSize ||
        static_cast<std::size_t>(table.block_length) > state.var2_data.size() - start) {
        write_fail(error, "table " + std::to_string(table_index) +
                              " column block lies outside Var2Data");
        return nullptr;
    }
    const std::size_t declared = static_cast<std::size_t>(table.last_column_index) + 1;
    if (declared != table.columns.size() ||
        static_cast<std::size_t>(table.block_length) !=
            kTableBlockHeaderSize + declared * kColumnRecordSize) {
        write_fail(error, "table " + std::to_string(table_index) +
                              " column block does not match its decoded column list");
        return nullptr;
    }
    payload_start = start;
    return &table;
}

}  // namespace

PartSet EncodedParts::parts() const {
    PartSet out;
    out.fixed_data = fixed_data;
    out.fixed_meta = fixed_meta;
    out.var_meta = var_meta;
    out.var2_data = var2_data;
    return out;
}

bool encode_view_state(const ViewState& state, EncodedParts& out, WriteError* error) {
    out = EncodedParts{};
    if (!state.parsed) {
        return write_fail(error,
                          "view state did not parse: encode_view_state() only writes a "
                          "ViewState produced by a successful parse_view_state()");
    }

    constexpr std::size_t kMaxU32 = 0xffffffffu;

    // FixedData is copied verbatim.  Its 16-byte identity values belong to
    // Project and change on every save; regenerating them would invent data.
    if (state.fixed_data.size() > kMaxU32) {
        return write_fail(error, "FixedData is too large to describe with a u32 size");
    }
    std::vector<std::uint8_t> fixed_data = state.fixed_data;

    // FixedMeta: 16-byte header + one entry per parsed entry.
    const std::size_t fixed_entry_count = state.fixed_meta.entries.size();
    std::uint32_t fixed_entry_size = 0;
    if (fixed_entry_count != 0) {
        if (state.fixed_meta.entry_size < 8) {
            return write_fail(error, "FixedMeta entry size " +
                                         std::to_string(state.fixed_meta.entry_size) +
                                         " is too small to hold state/var_count/fixed_offset");
        }
        fixed_entry_size = state.fixed_meta.entry_size;
    }
    if (fixed_entry_count > kMaxU32) {
        return write_fail(error, "FixedMeta has too many entries to describe with a u32 count");
    }
    std::vector<std::uint8_t> fixed_meta(
        kFixedMetaHeaderSize + fixed_entry_count * fixed_entry_size, 0);
    put_u32(fixed_meta, 0, kMetaMagic);
    put_u32(fixed_meta, 4, state.fixed_meta.flags);
    put_u32(fixed_meta, 8, static_cast<std::uint32_t>(fixed_entry_count));
    put_u32(fixed_meta, 12, static_cast<std::uint32_t>(fixed_data.size()));
    for (std::size_t i = 0; i < fixed_entry_count; ++i) {
        const FixedMetaEntry& entry = state.fixed_meta.entries[i];
        const std::size_t base = kFixedMetaHeaderSize + i * fixed_entry_size;
        const std::size_t opaque = fixed_entry_size - 8;
        if (entry.opaque_tail.size() != opaque) {
            return write_fail(error, "FixedMeta entry " + std::to_string(i) + " has " +
                                         std::to_string(entry.opaque_tail.size()) +
                                         " opaque byte(s) but entry size " +
                                         std::to_string(fixed_entry_size) + " needs " +
                                         std::to_string(opaque));
        }
        put_u16(fixed_meta, base, entry.state);
        put_u16(fixed_meta, base + 2, entry.var_count);
        put_u32(fixed_meta, base + 4, entry.fixed_offset);
        std::copy(entry.opaque_tail.begin(), entry.opaque_tail.end(),
                  fixed_meta.begin() + static_cast<std::ptrdiff_t>(base + 8));
    }

    // Var2Data: re-frame every block in stream order.  The new offsets are the
    // positions the length words land at, so VarMeta offsets below are derived
    // from the bytes rather than trusted from the input.
    if (state.block_refs.size() != state.blocks.size()) {
        return write_fail(error, "ViewState block_refs do not match its Var2Data blocks");
    }
    std::vector<std::uint8_t> var2_data;
    var2_data.reserve(state.var2_data.size());
    std::vector<std::uint32_t> new_offsets(state.blocks.size(), 0);
    for (std::size_t i = 0; i < state.blocks.size(); ++i) {
        const VarBlock& block = state.blocks[i];
        const std::size_t start = static_cast<std::size_t>(block.offset) + 4;
        if (start > state.var2_data.size() ||
            static_cast<std::size_t>(block.payload_length) >
                state.var2_data.size() - start) {
            return write_fail(error, "Var2Data block " + std::to_string(i) + " at offset " +
                                         std::to_string(block.offset) +
                                         " runs past the stream");
        }
        if (var2_data.size() > kMaxU32) {
            return write_fail(error, "Var2Data is too large to describe with a u32 size");
        }
        new_offsets[i] = static_cast<std::uint32_t>(var2_data.size());
        append_u32(var2_data, block.payload_length);
        var2_data.insert(
            var2_data.end(),
            state.var2_data.begin() + static_cast<std::ptrdiff_t>(start),
            state.var2_data.begin() +
                static_cast<std::ptrdiff_t>(start + block.payload_length));
    }
    if (var2_data.size() > kMaxU32) {
        return write_fail(error, "Var2Data is too large to describe with a u32 size");
    }

    // VarMeta: preserve the three reserved header words and any trailing bytes
    // the parser found, but recompute the count, offsets and var2_size.
    if (state.var_meta_bytes.size() < kVarMetaHeaderSize) {
        return write_fail(error, "VarMeta is shorter than its 24-byte header");
    }
    const std::size_t var_entry_count = state.var_meta.entries.size();
    const std::size_t var_entries_size = kVarMetaHeaderSize + var_entry_count * 12;
    if (state.var_meta_bytes.size() < var_entries_size) {
        return write_fail(error, "VarMeta is shorter than its decoded entry list");
    }
    if (var_entry_count > kMaxU32) {
        return write_fail(error, "VarMeta has too many entries to describe with a u32 count");
    }
    const std::size_t trailing = state.var_meta_bytes.size() - var_entries_size;
    std::vector<std::uint8_t> var_meta(var_entries_size + trailing, 0);
    put_u32(var_meta, 0, kMetaMagic);
    put_u32(var_meta, 4, read_u32(state.var_meta_bytes, 4));
    put_u32(var_meta, 8, static_cast<std::uint32_t>(var_entry_count));
    put_u32(var_meta, 12, read_u32(state.var_meta_bytes, 12));
    put_u32(var_meta, 16, read_u32(state.var_meta_bytes, 16));
    put_u32(var_meta, 20, static_cast<std::uint32_t>(var2_data.size()));
    std::vector<bool> claimed(state.blocks.size(), false);
    for (std::size_t i = 0; i < var_entry_count; ++i) {
        const VarMetaEntry& entry = state.var_meta.entries[i];
        std::size_t matched = state.blocks.size();
        // Prefer the block the entry originally named: two blocks can share a
        // (uid, key) pair, and then only the old offset distinguishes them.
        for (std::size_t j = 0; j < state.blocks.size(); ++j) {
            if (!claimed[j] && state.blocks[j].offset == entry.offset &&
                state.block_refs[j].uid == entry.uid &&
                state.block_refs[j].key == entry.key) {
                matched = j;
                break;
            }
        }
        if (matched == state.blocks.size()) {
            for (std::size_t j = 0; j < state.blocks.size(); ++j) {
                if (!claimed[j] && state.block_refs[j].uid == entry.uid &&
                    state.block_refs[j].key == entry.key) {
                    matched = j;
                    break;
                }
            }
        }
        if (matched == state.blocks.size()) {
            return write_fail(error, "VarMeta entry " + std::to_string(i) + " (uid " +
                                         std::to_string(entry.uid) + ", key " +
                                         hex32(entry.key) + ") has no matching Var2Data block");
        }
        claimed[matched] = true;
        const std::size_t base = kVarMetaHeaderSize + i * 12;
        put_u32(var_meta, base, entry.uid);
        put_u32(var_meta, base + 4, new_offsets[matched]);
        put_u32(var_meta, base + 8, entry.key);
    }
    if (trailing != 0) {
        std::copy(state.var_meta_bytes.end() - static_cast<std::ptrdiff_t>(trailing),
                  state.var_meta_bytes.end(),
                  var_meta.begin() + static_cast<std::ptrdiff_t>(var_entries_size));
    }

    out.fixed_data = std::move(fixed_data);
    out.fixed_meta = std::move(fixed_meta);
    out.var_meta = std::move(var_meta);
    out.var2_data = std::move(var2_data);
    return true;
}

bool set_table_date_format(ViewState& state,
                           std::size_t table_index,
                           std::uint8_t date_format,
                           WriteError* error) {
    std::size_t payload_start = 0;
    const TableDef* table = writable_table(state, table_index, error, payload_start);
    if (table == nullptr) {
        return false;
    }
    const std::size_t at = payload_start + 7;
    if (at >= state.var2_data.size()) {
        return write_fail(error, "table " + std::to_string(table_index) +
                                     " date-format byte lies outside Var2Data");
    }
    state.var2_data[at] = date_format;
    state.tables[table_index].date_format = date_format;
    return true;
}

bool set_table_column(ViewState& state,
                      std::size_t table_index,
                      std::size_t column_index,
                      std::uint32_t field_id,
                      std::string_view title,
                      WriteError* error) {
    std::size_t payload_start = 0;
    const TableDef* table = writable_table(state, table_index, error, payload_start);
    if (table == nullptr) {
        return false;
    }
    if (column_index >= table->columns.size()) {
        return write_fail(error, "column index " + std::to_string(column_index) +
                                     " is out of range (" +
                                     std::to_string(table->columns.size()) + " column(s))");
    }
    if (field_id == 0) {
        return write_fail(error, "field id 0 is not a valid column field");
    }
    if (title.size() > kColumnTitleCapacity) {
        return write_fail(error, "title is " + std::to_string(title.size()) +
                                     " code units; the CTable column title capacity is " +
                                     std::to_string(kColumnTitleCapacity));
    }
    for (const char ch : title) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte > 0x7e) {
            return write_fail(error,
                              "title must be printable ASCII (the decoder reads ASCII titles only)");
        }
    }
    const ColumnDef& column = table->columns[column_index];
    const std::size_t record = payload_start + column.record_offset;
    if (record > state.var2_data.size() ||
        kColumnRecordSize > state.var2_data.size() - record) {
        return write_fail(error, "table " + std::to_string(table_index) + " column " +
                                     std::to_string(column_index) +
                                     " record lies outside Var2Data");
    }
    put_u32(state.var2_data, record, field_id);
    // Project zero-filled the rest of the title area after the replacement's
    // NUL (measured on the w14 tablecol2-I/J fixture pair), so do the same.
    std::fill(
        state.var2_data.begin() + static_cast<std::ptrdiff_t>(record + kColumnTitleOffset),
        state.var2_data.begin() + static_cast<std::ptrdiff_t>(record + kColumnRecordSize),
        std::uint8_t{0});
    for (std::size_t i = 0; i < title.size(); ++i) {
        state.var2_data[record + kColumnTitleOffset + i * 2] =
            static_cast<std::uint8_t>(title[i]);
    }
    ColumnDef& target = state.tables[table_index].columns[column_index];
    target.field_id = field_id;
    target.title = std::string(title);
    return true;
}

bool append_table_column(ViewState& state,
                         std::size_t table_index,
                         std::uint32_t field_id,
                         std::string_view title,
                         WriteError* error) {
    std::size_t payload_start = 0;
    const TableDef* table = writable_table(state, table_index, error, payload_start);
    if (table == nullptr) {
        return false;
    }
    if (field_id == 0) {
        return write_fail(error, "field id 0 is not a valid column field");
    }
    if (title.size() > kColumnTitleCapacity) {
        return write_fail(error, "title is " + std::to_string(title.size()) +
                                     " code units; the CTable column title capacity is " +
                                     std::to_string(kColumnTitleCapacity));
    }
    for (const char ch : title) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte > 0x7e) {
            return write_fail(error,
                              "title must be printable ASCII (the decoder reads ASCII titles only)");
        }
    }
    // `last_column_index` is a u16, so the table can hold at most 0x10000
    // columns; one more would not be representable.
    if (table->columns.size() >= static_cast<std::size_t>(0x10000u)) {
        return write_fail(error, "table " + std::to_string(table_index) + " already holds " +
                                     std::to_string(table->columns.size()) +
                                     " columns; the u16 last-column index cannot grow further");
    }
    if (table->block_length > 0xffffffffu - kColumnRecordSize) {
        return write_fail(error, "table " + std::to_string(table_index) +
                                     " column block would overflow its u32 length word");
    }
    const std::size_t column_count = table->columns.size();
    const std::size_t insert_at =
        payload_start + kTableBlockHeaderSize + column_count * kColumnRecordSize;
    if (insert_at > state.var2_data.size()) {
        return write_fail(error, "table " + std::to_string(table_index) +
                                     " append position lies outside Var2Data");
    }

    // Build the measured fresh-column record: field id, the nine bytes Project
    // wrote for a fresh column, then the zero-filled UTF-16 title area.
    std::vector<std::uint8_t> record(kColumnRecordSize, std::uint8_t{0});
    put_u32(record, 0, field_id);
    for (std::size_t i = 0; i < kFreshColumnHeader.size(); ++i) {
        record[4 + i] = kFreshColumnHeader[i];
    }
    for (std::size_t i = 0; i < title.size(); ++i) {
        record[kColumnTitleOffset + i * 2] = static_cast<std::uint8_t>(title[i]);
    }

    const std::uint32_t block_offset = table->block_offset;
    const std::uint32_t new_block_length = table->block_length + kColumnRecordSize;
    std::size_t block_index = state.blocks.size();
    for (std::size_t i = 0; i < state.blocks.size(); ++i) {
        if (state.blocks[i].offset == block_offset) {
            block_index = i;
            break;
        }
    }
    if (block_index == state.blocks.size()) {
        return write_fail(error, "table " + std::to_string(table_index) +
                                     " column block is not in the Var2Data walk");
    }

    // Everything validated; mutate.  Insert first, then repair every offset that
    // moved (later Var2 blocks and their VarMeta entries).
    state.var2_data.insert(state.var2_data.begin() + static_cast<std::ptrdiff_t>(insert_at),
                           record.begin(), record.end());
    put_u32(state.var2_data, block_offset, new_block_length);
    // Block payload +4 is the u16 last-column index; the payload starts four
    // bytes after the length word.
    put_u16(state.var2_data, static_cast<std::size_t>(block_offset) + 8,
            static_cast<std::uint16_t>(column_count));
    state.blocks[block_index].payload_length = new_block_length;
    for (auto& block : state.blocks) {
        if (block.offset > block_offset) {
            block.offset += kColumnRecordSize;
        }
    }
    state.var2.blocks = state.blocks;
    state.var2.size = static_cast<std::uint32_t>(state.var2_data.size());
    for (std::size_t i = 0; i < state.block_refs.size() && i < state.blocks.size(); ++i) {
        state.block_refs[i].offset = state.blocks[i].offset;
    }
    for (auto& entry : state.var_meta.entries) {
        if (entry.offset > block_offset) {
            entry.offset += kColumnRecordSize;
        }
    }
    // Keep the decoded per-table / per-view block offsets consistent too, so a
    // second edit that targets a later table still finds it in Var2Data.
    for (auto& other : state.tables) {
        if (other.block_offset > block_offset) {
            other.block_offset += kColumnRecordSize;
        }
    }
    for (auto& other : state.views) {
        if (other.main_block_offset > block_offset) {
            other.main_block_offset += kColumnRecordSize;
        }
    }
    state.var_meta.var2_size = state.var2.size;

    TableDef& target = state.tables[table_index];
    target.block_length = new_block_length;
    target.last_column_index = static_cast<std::uint16_t>(column_count);
    ColumnDef column;
    column.record_offset =
        static_cast<std::uint32_t>(kTableBlockHeaderSize + column_count * kColumnRecordSize);
    column.field_id = field_id;
    column.opaque_header = kFreshColumnHeader;
    column.title = std::string(title);
    target.columns.push_back(std::move(column));
    return true;
}

TableEdit table_date_format_edit(std::size_t table_index, std::uint8_t date_format) {
    TableEdit edit;
    edit.kind = TableEdit::Kind::date_format;
    edit.table_index = table_index;
    edit.date_format = date_format;
    return edit;
}

TableEdit table_column_replace_edit(std::size_t table_index,
                                    std::size_t column_index,
                                    std::uint32_t field_id,
                                    std::string_view title) {
    TableEdit edit;
    edit.kind = TableEdit::Kind::replace_column;
    edit.table_index = table_index;
    edit.column_index = column_index;
    edit.field_id = field_id;
    edit.title = std::string(title);
    return edit;
}

TableEdit table_column_append_edit(std::size_t table_index,
                                   std::uint32_t field_id,
                                   std::string_view title) {
    TableEdit edit;
    edit.kind = TableEdit::Kind::append_column;
    edit.table_index = table_index;
    edit.field_id = field_id;
    edit.title = std::string(title);
    return edit;
}

bool apply_table_edits(const ViewState& state,
                       std::span<const TableEdit> edits,
                       EncodedParts& out,
                       WriteError* error) {
    out = EncodedParts{};
    if (!state.parsed) {
        return write_fail(error,
                          "view state did not parse: apply_table_edits() only accepts a "
                          "ViewState produced by a successful parse_view_state()");
    }
    // Work on a copy: the caller keeps its state even if a later edit refuses.
    ViewState work = state;
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const TableEdit& edit = edits[i];
        WriteError local;
        local.message = "unknown edit kind";
        bool ok = false;
        switch (edit.kind) {
            case TableEdit::Kind::date_format:
                ok = set_table_date_format(work, edit.table_index, edit.date_format, &local);
                break;
            case TableEdit::Kind::replace_column:
                ok = set_table_column(work, edit.table_index, edit.column_index, edit.field_id,
                                      edit.title, &local);
                break;
            case TableEdit::Kind::append_column:
                ok = append_table_column(work, edit.table_index, edit.field_id, edit.title, &local);
                break;
        }
        if (!ok) {
            return write_fail(error, "table edit " + std::to_string(i) + ": " + local.message);
        }
    }
    return encode_view_state(work, out, error);
}

}  // namespace msproject::viewstate
