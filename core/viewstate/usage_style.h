// core/viewstate/usage_style.h -- what a Task Usage / Resource Usage view
// stores about its time-phased grid.
//
// The two usage views are CV_iew views like any other; their main
// (`0x20400006`) block carries the same item framing.  Wave 31 (W31-4.2)
// decodes the two items that decide the grid:
//
//   0x22400008  the style record.  A usage view's record is 5,199 bytes on the
//               measured fixture (tests/data/wave31-usage.mpp), and the
//               timescale tier fields sit at exactly the offsets gantt_style.h
//               measures for a Gantt record:
//                 +1152 u8   middle-tier unit (stored byte + 1 indexes MPXJ's
//                            [none, minutes, ... years] table)
//                 +1154 u8   bottom-tier unit
//                 +1156 u16  middle count       +1158 u16 bottom count
//                 +1160 u16  middle format      +1162 u16 bottom format
//                 +1166 u8   middle alignment   +1164 u8  bottom alignment
//                 +1176 u32  saved scroll origin (tenths of a minute since
//                            1983-12-31 00:00)
//                 +1180 u16  saved size percentage
//               and the middle-tier tick/fiscal-year bits are 0x01/0x08 of the
//               view-flags byte +1086, the bottom tier's 0x02/0x10.
//
//               For a usage view the stored MIDDLE tier is the rendered TOP row
//               and the stored BOTTOM tier the rendered bottom row.  The
//               measured fixture stores weeks at +1152 and days at +1154 with
//               formats 13 (week date) and 20 (single-letter day), and Project
//               renders the week date above the day letter (W31-4.1's pixel
//               measurements: week label text rows 466-473, day letters
//               482-489).  UsageViewStyle therefore names them `major` and
//               `minor`, not `middle`/`bottom`.
//
//   0x2240003c  the Details list: 111 raw bytes, no count prefix, 0xff-padded.
//               Project's decoder (the 5,199-byte record's sibling
//               `VPID_ASSNFIELDORDER`) treats the FIRST 0xff as the end of the
//               list, so the bytes before it are the shown details in order.
//               MEASURED against DetailStylesAdd + the rendered Details column
//               (scripts/oracle_w31_usage.py and the wave-31 probes):
//                 0x00 -> Work            (DetailStylesAdd(0))
//                 0x02 -> Actual Work     (DetailStylesAdd(4))
//                 0x09 -> Cost            (DetailStylesAdd(1); the rendered
//                                         Details label reads "Cost")
//               A stock, untouched view stores two ids -- `00 48` in Task
//               Usage and `00 43` in Resource Usage -- but Project renders ONE
//               Work line for them: adding detail 24 (stored 0x48) alone
//               renders no detail line at all, so those bytes are stored
//               details with no rendered line, not a second Work.  Ids outside
//               the measured three are kept with kind `other` and a warning;
//               the grid draws no line for them, which is what Project does on
//               the measured files.
//
// The 19 Text Styles slots in the same record are NOT decoded here: no wave-31
// measurement says which slot Project paints a usage sheet row with, so the
// shell keeps its stock row font.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "gantt_style.h"  // TimescaleTier, TimescaleUnit
#include "viewstate.h"    // ParseError, ViewState, ViewDef

namespace msproject::viewstate {

// CV_iew main-block item carrying the usage Details list.
inline constexpr std::uint32_t kViewUsageDetailsKey = 0x2240003Cu;

// The measured 0x2240003c payload is 111 bytes.  The decoder does not require
// that exact size -- the first 0xff ends the list, and a short item is accepted
// as a short list -- but the constant names what the fixture stores.
inline constexpr std::size_t kUsageDetailsMeasuredSize = 111;
// The list's padding byte; Project's decoder treats the first one as the end.
inline constexpr std::uint8_t kUsageDetailPadding = 0xFFu;

// The timescale fields need 1182 bytes (the +1180 size word's last byte).
inline constexpr std::size_t kUsageTimescaleMinSize =
    kGanttTimescaleSizeOffset + 2;

// The stored detail selectors this wave decodes.
enum class UsageDetailKind : std::uint8_t {
    work = 0x00,
    actual_work = 0x02,
    cost = 0x09,
    // Any other stored id: kept (and reported) but no value rule, so the grid
    // draws no line for it -- Project itself drew none for the two stock ids
    // (0x48/0x43) measured in wave 31.
    other = 0xFF,
};

[[nodiscard]] const char* usage_detail_kind_name(UsageDetailKind kind) noexcept;

// One stored Details-list entry.
struct UsageDetail {
    std::uint8_t stored_id = 0;
    UsageDetailKind kind = UsageDetailKind::other;

    // True for the three kinds this wave can value.
    [[nodiscard]] bool valued() const noexcept {
        return kind != UsageDetailKind::other;
    }
};

struct UsageViewStyle {
    bool decoded = false;

    // The details list, in stored order, ending at the first 0xff.  An ABSENT
    // 0x2240003c item decodes as one Work entry (Project's "Work by default",
    // the fallback its own decoder takes when the item is missing); a stored
    // list whose first byte is already 0xff is RemoveAll's measured state and
    // decodes as no details at all.
    std::vector<UsageDetail> details;

    // The stored middle (+1152) and bottom (+1154) tiers.  For a usage view
    // these are the rendered top and bottom header rows; see the file header.
    TimescaleTier major;
    TimescaleTier minor;

    // Opaque outside the two tick/fiscal-year bits; kept so the shell can
    // report what the record holds.
    int view_flags = 0;
    std::uint32_t timescale_start = 0;
    int timescale_size = 0;

    std::vector<std::string> warnings;
};

// Decodes a usage view's `0x22400008` (style/timescale) and `0x2240003c`
// (details) payloads.
//
//   * `style_data` shorter than kUsageTimescaleMinSize is refused; the
//     measured record is 5,199 bytes.
//   * `detail_data` may be empty (the view stores no such item): the result is
//     one Work entry.  A present list is read to its first 0xff; a stored list
//     that is empty from its first byte (DetailStylesRemoveAll) yields no
//     details.  Each id is mapped through the measured table; an unmapped id
//     keeps `other` and adds a warning (it is not an error: the record is well
//     formed, this wave just has no value rule for it).
//
// Nothing here fails on a value that only limits what can be drawn: the
// warnings say what was not understood.
[[nodiscard]] bool parse_usage_view_style(std::span<const std::uint8_t> style_data,
                                          std::span<const std::uint8_t> detail_data,
                                          UsageViewStyle& out,
                                          ParseError* error = nullptr);

}  // namespace msproject::viewstate
