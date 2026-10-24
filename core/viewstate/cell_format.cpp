// core/viewstate/cell_format.cpp -- see cell_format.h.

#include "cell_format.h"

#include <algorithm>
#include <string>

namespace msproject::viewstate {
namespace {

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

}  // namespace

const CellFormat* CellFormatTable::find(std::uint32_t row_uid,
                                        std::uint32_t field_id) const noexcept {
    const CellFormat* row_default = nullptr;
    for (const CellFormat& entry : entries) {
        if (entry.row_uid != row_uid) {
            continue;
        }
        if (entry.field_id == field_id) {
            return &entry;
        }
        if (entry.field_id == kCellFormatRowDefaultField && row_default == nullptr) {
            row_default = &entry;
        }
    }
    return row_default;
}

bool parse_cell_formats(std::span<const std::uint8_t> payload, CellFormatTable& out,
                        ParseError* error) {
    out = CellFormatTable{};
    if (payload.size() % kCellFormatEntrySize != 0) {
        if (error != nullptr) {
            error->message = "cell format record is " + std::to_string(payload.size()) +
                             " bytes; the measured entry is " +
                             std::to_string(kCellFormatEntrySize) + " bytes";
            error->offset = payload.size();
        }
        return false;
    }
    const std::size_t count = payload.size() / kCellFormatEntrySize;
    out.entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t base = index * kCellFormatEntrySize;
        CellFormat entry;
        entry.row_uid = read_u32(payload, base);
        entry.field_id = read_u32(payload, base + 4);
        entry.flags = read_u32(payload, base + kCellFormatFlagsOffset);
        // The same colour rule as the text styles: the fourth byte is an
        // automatic flag and only zero means the three bytes are a colour.
        const std::size_t colour = base + kCellFormatBackgroundOffset;
        entry.has_background = payload[colour + 3] == 0;
        if (entry.has_background) {
            entry.background.red = payload[colour];
            entry.background.green = payload[colour + 1];
            entry.background.blue = payload[colour + 2];
        }
        entry.background_pattern = read_u32(payload, base + kCellFormatPatternOffset);
        entry.font_word = read_u32(payload, base + kCellFormatFontWordOffset);
        out.entries.push_back(entry);
    }
    out.decoded = true;
    return true;
}

std::span<const std::uint8_t> cell_format_data_from_view(const ViewState& state,
                                                         const ViewDef& view) {
    if (!view.main_decoded) {
        return {};
    }
    const auto block = std::find_if(
        state.blocks.begin(), state.blocks.end(),
        [&view](const VarBlock& candidate) {
            return candidate.offset == view.main_block_offset;
        });
    if (block == state.blocks.end()) {
        return {};
    }
    const std::span<const std::uint8_t> payload = state.block_bytes(*block);
    for (const ViewRecord& record : view.records) {
        if (record.id != kViewCellFormatKey) {
            continue;
        }
        const std::size_t start = record.record_offset + kViewItemHeaderSize;
        if (start > payload.size() ||
            record.payload_length > payload.size() - start) {
            return {};
        }
        return payload.subspan(start, record.payload_length);
    }
    return {};
}

}  // namespace msproject::viewstate
