// core/viewstate/gantt_style -- decoder for the Gantt view's style record.
//
// `214/CV_iew`'s main (`0x20400006`) block is a Props-format document.  The
// record with id `0x22400008` (PropsKey.STYLE_DATA) carries the Gantt chart's
// text styles, bar styles and timescale tiers.  This module decodes it; the
// layout below was proved against MPXJ's MPP14 reader
// (`GanttChartView14.processViewProperties`) and then verified byte-for-byte
// against controlled Project 2021 saves (scripts/oracle_w26.py, wave 26):
//
//   text style records, 32 bytes each, first at +26, stride +32:
//       +0   u8   font index into the `214/Props` FONT_BASES table
//       +3   u8   style bits: 0x01 bold, 0x02 italic, 0x04 underline,
//                0x08 strikethrough
//       +4   4 B  foreground colour: R, G, B, automatic flag (0 = set)
//       +16  4 B  background colour, same layout
//       +28  u16  background pattern (1 = solid)
//
//   The 17 records, in file order, are the Text Styles dialog's items:
//       highlighted, row/column titles, noncritical, critical, summary,
//       milestone, middle timescale, bottom timescale, bar text left/right/
//       top/bottom/inside, marked, project summary, external, top timescale.
//
//   font bases (`214/Props` record id 0x03400000):
//       +0 u16 count, then count records of
//       +0 u16 opaque, +2 u16 point size, +4 64-byte NUL-terminated UTF-16LE
//       family name.
//
// The colour byte order is measured: the controlled save that sets the
// milestone style to Project's red stores R=255, G=0, B=0 in the first three
// bytes, and the flag byte is 0.  A non-zero flag means "automatic" and is
// decoded as no colour (MPPUtility.getColor's rule; the stock synthetic file
// stores 0xFF there for every automatic colour).
//
// Bar styles and the timescale tiers live in the same record; they are decoded
// by wave 26's W26-3.2/W26-3.3 additions below.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "viewstate.h"  // ParseError, ViewState, ViewDef

namespace msproject::viewstate {

// One RGB colour as stored in the style record.
struct Rgb {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    friend bool operator==(const Rgb& left, const Rgb& right) noexcept {
        return left.red == right.red && left.green == right.green &&
               left.blue == right.blue;
    }
    friend bool operator!=(const Rgb& left, const Rgb& right) noexcept {
        return !(left == right);
    }
};

// One entry of the FONT_BASES table.  An empty name is a hole in the measured
// table (the stock file has many); `valid` marks a usable entry.
struct FontBase {
    std::string name;
    int point_size = 0;
    bool valid = false;
};

// The Text Styles dialog's items, in the order the style record stores them.
enum class TextStyleItem : std::uint8_t {
    highlighted = 0,
    row_column_titles,
    non_critical,
    critical,
    summary,
    milestone,
    middle_timescale,
    bottom_timescale,
    bar_text_left,
    bar_text_right,
    bar_text_top,
    bar_text_bottom,
    bar_text_inside,
    marked,
    project_summary,
    external,
    top_timescale,
};

inline constexpr std::size_t kTextStyleItemCount = 17;

[[nodiscard]] const char* text_style_item_name(TextStyleItem item) noexcept;

// One decoded text style.  `valid` is false when the record's font index does
// not resolve to a usable FONT_BASES entry; the other fields are still read.
struct TextStyle {
    bool valid = false;
    std::uint8_t font_index = 0;
    std::string font_name;
    int point_size = 0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool has_colour = false;
    Rgb colour{};
    bool has_background = false;
    Rgb background{};
    std::uint16_t background_pattern = 1;
};

// The decoded style record.  `decoded` is true only when parse_gantt_style()
// validated the measured minimum size and every record it reads.
// One Gantt bar style of the active view.  The field layout is MPXJ's
// GanttBarStyleFactory14 and was verified by wave 26's controlled saves
// (GanttBarStyleEdit changed exactly the summary and milestone records'
// middle/start/end colours and start shape in build/w26/g3/trees4).
struct BarStyle {
    std::string name;
    std::uint16_t id = 0;
    std::uint16_t row = 0;
    std::uint8_t middle_shape = 0;    // GanttBarMiddleShape (MPXJ's naming)
    std::uint8_t middle_pattern = 0;  // ChartPattern
    bool has_middle_colour = false;
    Rgb middle_colour{};
    std::uint8_t start_shape = 0;     // byte % 25
    std::uint8_t start_type = 0;      // byte / 25
    bool has_start_colour = false;
    Rgb start_colour{};
    std::uint8_t end_shape = 0;
    std::uint8_t end_type = 0;
    bool has_end_colour = false;
    Rgb end_colour{};
    std::uint64_t show_for = 0;       // GanttBarShowForTasks bits
    std::uint64_t show_for_not = 0;
    std::uint32_t left_text = 0;
    std::uint32_t right_text = 0;
    std::uint32_t top_text = 0;
    std::uint32_t bottom_text = 0;
    std::uint32_t inside_text = 0;
};

struct GanttStyle {
    bool decoded = false;
    std::vector<FontBase> fonts;
    std::array<TextStyle, kTextStyleItemCount> text{};
    std::vector<BarStyle> bars;
    std::vector<std::string> warnings;
};

// The measured shape of the MPP14 style record.  MPXJ requires >
// 41360 bytes; the last field read is the top-tier fiscal-year byte at 41361,
// so a usable record is at least 41362 bytes long.
inline constexpr std::size_t kGanttStyleMinSize = 41362;
inline constexpr std::size_t kGanttFontStyleOffset = 26;
inline constexpr std::size_t kGanttFontStyleStride = 32;
inline constexpr std::size_t kGanttFontBaseHeaderSize = 2;
inline constexpr std::size_t kGanttFontBaseRecordSize = 68;
inline constexpr std::size_t kGanttFontBaseNameBytes = 64;

// The bar style array, measured from the same record:
//   +2243 u8   style count (MPPUtility.getByte)
//   +2255      first 195-byte record
inline constexpr std::size_t kGanttBarCountOffset = 2243;
inline constexpr std::size_t kGanttBarFirstOffset = 2255;
inline constexpr std::size_t kGanttBarStride = 195;
inline constexpr std::size_t kGanttBarNameOffset = 91;   // NUL-terminated UTF-16
inline constexpr std::size_t kGanttBarNameBytes = kGanttBarStride - kGanttBarNameOffset;

// Decodes the `214/Props` FONT_BASES payload (record id 0x03400000).  The
// table's declared count must fit the payload; a count that does not is a
// hard error.  Entries with an empty family name are kept in place (so a font
// index still addresses the same slot) with `valid == false`.
[[nodiscard]] bool parse_font_bases(std::span<const std::uint8_t> payload,
                                    std::vector<FontBase>& out,
                                    ParseError* error = nullptr);

// Decodes the text-style part of the Gantt style record.  A record shorter
// than kGanttStyleMinSize is refused (the file is not an MPP14 Gantt view).
// A style whose font index does not resolve is kept (all fields decoded) with
// `valid == false` and a warning, because a malformed font index is
// presentation damage, not a reason to lose the whole view.
[[nodiscard]] bool parse_gantt_style(std::span<const std::uint8_t> style_data,
                                     std::span<const FontBase> fonts,
                                     GanttStyle& out,
                                     ParseError* error = nullptr);

// The `0x22400008` record payload of a decoded CV_iew view, or an empty span
// when the view's main block did not decode or carries no such record.
[[nodiscard]] std::span<const std::uint8_t> style_data_from_view(
    const ViewState& state, const ViewDef& view);

// The view the file was left in, or the first decodable Gantt view.  `name` is
// the applied view name from `214/CEdl` (`0x26400018`); the comparison ignores
// the accelerator (`&`) and ASCII case, which is how the two records agree on
// every measured file.  A Gantt view is recognised by a `0x22400008` record at
// or above kGanttStyleMinSize (a Task Form's record is ~1.5 KB).
[[nodiscard]] const ViewDef* find_applied_view(const ViewState& state,
                                               std::string_view name);

}  // namespace msproject::viewstate
