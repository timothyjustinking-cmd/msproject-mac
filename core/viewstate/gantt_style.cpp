// core/viewstate/gantt_style.cpp -- see gantt_style.h.

#include "gantt_style.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace msproject::viewstate {
namespace {

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8u);
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

std::uint64_t read_u64(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint64_t>(read_u32(data, offset)) |
           (static_cast<std::uint64_t>(read_u32(data, offset + 4)) << 32u);
}

// A NUL-terminated UTF-16LE string of printable ASCII, bounded by `max_bytes`.
// The measured family and view names are ASCII; a non-ASCII unit stops the read
// rather than producing an encoding guess.
std::string read_utf16_name(std::span<const std::uint8_t> data, std::size_t offset,
                            std::size_t max_bytes) {
    std::string out;
    const std::size_t end = std::min(data.size(), offset + max_bytes);
    for (std::size_t at = offset; at + 1 < end; at += 2) {
        const std::uint16_t unit = read_u16(data, at);
        if (unit == 0) {
            break;
        }
        if (unit > 0x7e || unit < 0x20) {
            break;
        }
        out.push_back(static_cast<char>(unit));
    }
    return out;
}

// MPPUtility.getColor's measured rule: the fourth byte is an automatic flag;
// only 0 means the three bytes are a colour.  Returns false when automatic.
bool read_colour(std::span<const std::uint8_t> data, std::size_t offset, Rgb& out) {
    if (offset + 4 > data.size() || data[offset + 3] != 0) {
        return false;
    }
    out.red = data[offset];
    out.green = data[offset + 1];
    out.blue = data[offset + 2];
    return true;
}

std::string without_accelerator(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char ch : name) {
        if (ch != '&') {
            out.push_back(ch);
        }
    }
    return out;
}

std::string lowercase_ascii(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool same_view_name(std::string_view left, std::string_view right) {
    return lowercase_ascii(without_accelerator(left)) ==
           lowercase_ascii(without_accelerator(right));
}

bool is_gantt_style(const ViewState& state, const ViewDef& view) {
    const std::span<const std::uint8_t> data = style_data_from_view(state, view);
    return data.size() >= kGanttStyleMinSize;
}

TimescaleUnit decode_timescale_unit(std::uint8_t byte) {
    // MPXJ's TimescaleUnitsHelper: stored byte + 1 indexes
    // [NONE, MINUTES, HOURS, DAYS, WEEKS, THIRDS_OF_MONTHS, MONTHS, QUARTERS,
    //  HALF_YEARS, YEARS].  255 ("none") and anything past the table map to
    // none.
    constexpr TimescaleUnit kUnits[] = {
        TimescaleUnit::none,     TimescaleUnit::minutes,    TimescaleUnit::hours,
        TimescaleUnit::days,     TimescaleUnit::weeks,      TimescaleUnit::thirds_of_months,
        TimescaleUnit::months,   TimescaleUnit::quarters,   TimescaleUnit::half_years,
        TimescaleUnit::years,
    };
    const int index = static_cast<int>(byte) + 1;
    if (index < 0 || index >= static_cast<int>(std::size(kUnits))) {
        return TimescaleUnit::none;
    }
    return kUnits[index];
}

}  // namespace

const char* timescale_unit_name(TimescaleUnit unit) noexcept {
    switch (unit) {
        case TimescaleUnit::none: return "none";
        case TimescaleUnit::minutes: return "minutes";
        case TimescaleUnit::hours: return "hours";
        case TimescaleUnit::days: return "days";
        case TimescaleUnit::weeks: return "weeks";
        case TimescaleUnit::thirds_of_months: return "thirds of months";
        case TimescaleUnit::months: return "months";
        case TimescaleUnit::quarters: return "quarters";
        case TimescaleUnit::half_years: return "half years";
        case TimescaleUnit::years: return "years";
    }
    return "none";
}

const char* text_style_item_name(TextStyleItem item) noexcept {
    switch (item) {
        case TextStyleItem::highlighted: return "highlighted";
        case TextStyleItem::row_column_titles: return "row and column titles";
        case TextStyleItem::non_critical: return "noncritical";
        case TextStyleItem::critical: return "critical";
        case TextStyleItem::summary: return "summary";
        case TextStyleItem::milestone: return "milestone";
        case TextStyleItem::middle_timescale: return "middle timescale";
        case TextStyleItem::bottom_timescale: return "bottom timescale";
        case TextStyleItem::bar_text_left: return "bar text left";
        case TextStyleItem::bar_text_right: return "bar text right";
        case TextStyleItem::bar_text_top: return "bar text top";
        case TextStyleItem::bar_text_bottom: return "bar text bottom";
        case TextStyleItem::bar_text_inside: return "bar text inside";
        case TextStyleItem::marked: return "marked";
        case TextStyleItem::project_summary: return "project summary";
        case TextStyleItem::external: return "external tasks";
        case TextStyleItem::top_timescale: return "top timescale";
        case TextStyleItem::changed_cells: return "changed cells";
        case TextStyleItem::inactive: return "inactive tasks";
    }
    return "unknown";
}

bool parse_font_bases(std::span<const std::uint8_t> payload, std::vector<FontBase>& out,
                      ParseError* error) {
    out.clear();
    if (payload.size() < kGanttFontBaseHeaderSize) {
        if (error != nullptr) {
            error->message = "FONT_BASES payload is shorter than its 2-byte count";
            error->offset = payload.size();
        }
        return false;
    }
    const std::uint16_t count = read_u16(payload, 0);
    const std::size_t required =
        kGanttFontBaseHeaderSize + kGanttFontBaseRecordSize * count;
    if (required > payload.size()) {
        if (error != nullptr) {
            error->message = "FONT_BASES declares " + std::to_string(count) +
                             " records (" + std::to_string(required) +
                             " bytes) but the payload is " +
                             std::to_string(payload.size()) + " bytes";
            error->offset = payload.size();
        }
        return false;
    }
    out.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t record =
            kGanttFontBaseHeaderSize + index * kGanttFontBaseRecordSize;
        FontBase& font = out[index];
        font.point_size = read_u16(payload, record + 2);
        font.name = read_utf16_name(payload, record + 4, kGanttFontBaseNameBytes);
        font.valid = !font.name.empty();
    }
    return true;
}

bool parse_gantt_style(std::span<const std::uint8_t> style_data,
                       std::span<const FontBase> fonts, GanttStyle& out,
                       ParseError* error) {
    out = GanttStyle{};
    if (style_data.size() < kGanttStyleMinSize) {
        if (error != nullptr) {
            error->message = "Gantt style record is " +
                             std::to_string(style_data.size()) +
                             " bytes; the measured MPP14 record needs at least " +
                             std::to_string(kGanttStyleMinSize);
            error->offset = style_data.size();
        }
        return false;
    }
    out.fonts.assign(fonts.begin(), fonts.end());
    for (std::size_t index = 0; index < kTextStyleItemCount; ++index) {
        const std::size_t offset = kGanttFontStyleOffset + index * kGanttFontStyleStride;
        TextStyle& style = out.text[index];
        style.font_index = style_data[offset];
        const std::uint8_t bits = style_data[offset + 3];
        style.bold = (bits & 0x01u) != 0;
        style.italic = (bits & 0x02u) != 0;
        style.underline = (bits & 0x04u) != 0;
        style.strikethrough = (bits & 0x08u) != 0;
        style.has_colour = read_colour(style_data, offset + 4, style.colour);
        style.has_background = read_colour(style_data, offset + 16, style.background);
        style.background_pattern = read_u16(style_data, offset + 28);
        if (style.font_index < fonts.size() && fonts[style.font_index].valid) {
            style.valid = true;
            style.font_name = fonts[style.font_index].name;
            style.point_size = fonts[style.font_index].point_size;
        } else {
            out.warnings.push_back(
                std::string("text style '") +
                text_style_item_name(static_cast<TextStyleItem>(index)) +
                "' names font base " + std::to_string(style.font_index) +
                ", which the FONT_BASES table does not define");
        }
    }

    const std::size_t bar_count = style_data[kGanttBarCountOffset];
    const std::size_t bars_end = kGanttBarFirstOffset + bar_count * kGanttBarStride;
    if (bars_end > style_data.size()) {
        if (error != nullptr) {
            error->message = "the bar-style array (" + std::to_string(bar_count) +
                             " records) runs past the " +
                             std::to_string(style_data.size()) + "-byte record";
            error->offset = style_data.size();
        }
        return false;
    }
    out.bars.reserve(bar_count);
    for (std::size_t index = 0; index < bar_count; ++index) {
        const std::size_t base = kGanttBarFirstOffset + index * kGanttBarStride;
        BarStyle bar;
        bar.name = read_utf16_name(style_data, base + kGanttBarNameOffset,
                                   kGanttBarNameBytes);
        bar.id = read_u16(style_data, base + 89);
        bar.row = read_u16(style_data, base + 65);
        bar.middle_shape = style_data[base];
        bar.middle_pattern = style_data[base + 1];
        bar.has_middle_colour = read_colour(style_data, base + 2, bar.middle_colour);
        const std::uint8_t start = style_data[base + 15];
        bar.start_shape = static_cast<std::uint8_t>(start % 25);
        bar.start_type = static_cast<std::uint8_t>(start / 25);
        bar.has_start_colour = read_colour(style_data, base + 16, bar.start_colour);
        const std::uint8_t end = style_data[base + 28];
        bar.end_shape = static_cast<std::uint8_t>(end % 25);
        bar.end_type = static_cast<std::uint8_t>(end / 25);
        bar.has_end_colour = read_colour(style_data, base + 29, bar.end_colour);
        bar.show_for = read_u64(style_data, base + 49);
        bar.show_for_not = read_u64(style_data, base + 57);
        bar.from_field = read_u32(style_data, base + kGanttBarFromFieldOffset);
        bar.to_field = read_u32(style_data, base + kGanttBarToFieldOffset);
        bar.left_text = read_u32(style_data, base + 67);
        bar.right_text = read_u32(style_data, base + 71);
        bar.top_text = read_u32(style_data, base + 75);
        bar.bottom_text = read_u32(style_data, base + 79);
        bar.inside_text = read_u32(style_data, base + 83);
        out.bars.push_back(std::move(bar));
    }

    out.view_flags = style_data[kGanttViewFlagsOffset];
    out.timescale_show = style_data[kGanttShowTiersOffset];
    out.timescale_start = read_u32(style_data, kGanttTimescaleStartOffset);
    out.timescale_size = static_cast<int>(read_u16(style_data, kGanttTimescaleSizeOffset));
    out.top.unit = decode_timescale_unit(style_data[kGanttTopUnitsOffset]);
    out.top.count = style_data[kGanttTopCountOffset];
    out.top.format = read_u16(style_data, kGanttTopFormatOffset);
    out.top.align = style_data[kGanttTopAlignOffset];
    out.top.tick_lines = style_data[kGanttTopTicksOffset] != 0;
    out.top.fiscal_year = (style_data[kGanttTopFiscalOffset] & 0x01u) != 0;
    out.middle.unit = decode_timescale_unit(style_data[kGanttMiddleUnitsOffset]);
    out.middle.count = style_data[kGanttMiddleCountOffset];
    out.middle.format = read_u16(style_data, kGanttMiddleFormatOffset);
    out.middle.align = style_data[kGanttMiddleAlignOffset];
    out.middle.tick_lines = (out.view_flags & 0x01) != 0;
    out.middle.fiscal_year = (out.view_flags & 0x08) != 0;
    out.bottom.unit = decode_timescale_unit(style_data[kGanttBottomUnitsOffset]);
    out.bottom.count = style_data[kGanttBottomCountOffset];
    out.bottom.format = read_u16(style_data, kGanttBottomFormatOffset);
    out.bottom.align = style_data[kGanttBottomAlignOffset];
    out.bottom.tick_lines = (out.view_flags & 0x02) != 0;
    out.bottom.fiscal_year = (out.view_flags & 0x10) != 0;
    out.decoded = true;
    return true;
}

std::span<const std::uint8_t> style_data_from_view(const ViewState& state,
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
        if (record.id != kViewFormatKey) {
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

const ViewDef* find_applied_view(const ViewState& state, std::string_view name) {
    const ViewDef* first_gantt = nullptr;
    const ViewDef* by_display = nullptr;
    for (const ViewDef& view : state.views) {
        if (!is_gantt_style(state, view)) {
            continue;
        }
        if (first_gantt == nullptr) {
            first_gantt = &view;
        }
        if (!name.empty() && same_view_name(view.name, name)) {
            return &view;
        }
        if (!name.empty() && !view.display_name.empty() &&
            same_view_name(view.display_name, name) && by_display == nullptr) {
            by_display = &view;
        }
    }
    if (by_display != nullptr) {
        return by_display;
    }
    return first_gantt;
}

}  // namespace msproject::viewstate
