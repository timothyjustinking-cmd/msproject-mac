// core/viewstate/usage_style.cpp -- see usage_style.h.

#include "usage_style.h"

#include <string>

namespace msproject::viewstate {
namespace {

// MPXJ's TimescaleUnitsHelper, the same rule gantt_style.cpp applies: the
// stored byte + 1 indexes [none, minutes, hours, days, weeks, thirds of
// months, months, quarters, half years, years]; 255 ("none") and anything past
// the table map to none.
TimescaleUnit decode_unit(std::uint8_t byte) {
    constexpr TimescaleUnit kUnits[] = {
        TimescaleUnit::none,   TimescaleUnit::minutes,
        TimescaleUnit::hours,  TimescaleUnit::days,
        TimescaleUnit::weeks,  TimescaleUnit::thirds_of_months,
        TimescaleUnit::months, TimescaleUnit::quarters,
        TimescaleUnit::half_years, TimescaleUnit::years,
    };
    const int index = static_cast<int>(byte) + 1;
    constexpr int kUnitCount =
        static_cast<int>(sizeof(kUnits) / sizeof(kUnits[0]));
    if (index < 0 || index >= kUnitCount) {
        return TimescaleUnit::none;
    }
    return kUnits[index];
}

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(data[offset + 1]) << 8u);
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

// One stored selector -> the detail kind this wave can value.
UsageDetailKind detail_kind_for(std::uint8_t stored_id) {
    switch (stored_id) {
        case static_cast<std::uint8_t>(UsageDetailKind::work):
            return UsageDetailKind::work;
        case static_cast<std::uint8_t>(UsageDetailKind::actual_work):
            return UsageDetailKind::actual_work;
        case static_cast<std::uint8_t>(UsageDetailKind::cost):
            return UsageDetailKind::cost;
        default:
            return UsageDetailKind::other;
    }
}

std::string hex_byte(std::uint8_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string out = "0x";
    out.push_back(kDigits[value >> 4u]);
    out.push_back(kDigits[value & 0x0Fu]);
    return out;
}

}  // namespace

const char* usage_detail_kind_name(UsageDetailKind kind) noexcept {
    switch (kind) {
        case UsageDetailKind::work: return "work";
        case UsageDetailKind::actual_work: return "actual work";
        case UsageDetailKind::cost: return "cost";
        case UsageDetailKind::other: return "other";
    }
    return "other";
}

bool parse_usage_view_style(std::span<const std::uint8_t> style_data,
                            std::span<const std::uint8_t> detail_data,
                            UsageViewStyle& out, ParseError* error) {
    out = UsageViewStyle{};
    if (style_data.size() < kUsageTimescaleMinSize) {
        if (error != nullptr) {
            error->message =
                "usage style record is " + std::to_string(style_data.size()) +
                " bytes; the timescale fields need at least " +
                std::to_string(kUsageTimescaleMinSize) + " (the measured " +
                "record is 5199)";
            error->offset = style_data.size();
        }
        return false;
    }

    out.view_flags = style_data[kGanttViewFlagsOffset];
    out.major.unit = decode_unit(style_data[kGanttMiddleUnitsOffset]);
    out.major.count = read_u16(style_data, kGanttMiddleCountOffset);
    out.major.format = read_u16(style_data, kGanttMiddleFormatOffset);
    out.major.align = style_data[kGanttMiddleAlignOffset];
    out.major.tick_lines = (out.view_flags & 0x01) != 0;
    out.major.fiscal_year = (out.view_flags & 0x08) != 0;
    out.minor.unit = decode_unit(style_data[kGanttBottomUnitsOffset]);
    out.minor.count = read_u16(style_data, kGanttBottomCountOffset);
    out.minor.format = read_u16(style_data, kGanttBottomFormatOffset);
    out.minor.align = style_data[kGanttBottomAlignOffset];
    out.minor.tick_lines = (out.view_flags & 0x02) != 0;
    out.minor.fiscal_year = (out.view_flags & 0x10) != 0;
    out.timescale_start = read_u32(style_data, kGanttTimescaleStartOffset);
    out.timescale_size =
        static_cast<int>(read_u16(style_data, kGanttTimescaleSizeOffset));

    if (detail_data.empty()) {
        // The item is absent.  Project's own decoder falls back to a default
        // when the pair of detail items does not validate; on every measured
        // view that default is Work.
        out.details.push_back(UsageDetail{0x00, UsageDetailKind::work});
        out.decoded = true;
        return true;
    }

    // The first 0xff ends the list (Project's decoder treats it as the count);
    // a first byte of 0xff is DetailStylesRemoveAll's measured "no details".
    for (const std::uint8_t stored_id : detail_data) {
        if (stored_id == kUsageDetailPadding) {
            break;
        }
        UsageDetail detail;
        detail.stored_id = stored_id;
        detail.kind = detail_kind_for(stored_id);
        if (!detail.valued()) {
            out.warnings.push_back(
                "usage detail id " + hex_byte(stored_id) +
                " is stored but not decoded this wave (Work, Actual Work and "
                "Cost are); the grid draws no line for it");
        }
        out.details.push_back(detail);
    }
    out.decoded = true;
    return true;
}

}  // namespace msproject::viewstate
