#include "viewstate.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sstream>

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
        if (!out.rows.empty() && (entry.uid == 0 || entry.uid > out.rows.size())) {
            out.warnings.push_back("VarMeta entry uid " + std::to_string(entry.uid) +
                                   " has no fixed row (rows: " +
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

            const ViewBlockRef* ref = out.find_block_ref(static_cast<std::uint32_t>(i + 1),
                                                         kColumnBlockKey);
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
                if (ref.uid == static_cast<std::uint32_t>(i + 1)) {
                    view.blocks.push_back(ref);
                }
            }
            out.views.push_back(std::move(view));
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

}  // namespace msproject::viewstate
