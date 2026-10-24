// core/viewstate/cell_format.h -- decoder for a view's per-cell formatting.
//
// `214/CV_iew`'s main (`0x20400006`) block of a Gantt view carries, besides the
// style record gantt_style.h decodes, the item with id `0x2240000c`: the
// Format > Font formatting saved per cell.  The item header's kind word is not
// part of the identity -- PROJECT_A's record stores 26, a controlled synthetic
// save stores 1 -- so the decoder frames the item by its id alone, exactly like
// the style record.
//
// The payload is a sequence of 44-byte entries with no count header
// (`length % 44 == 0`), measured from PROJECT_A and from controlled Project
// 2021 saves (`build/w29/cellformat_probe.py`: SelectRow + Font32Ex with a
// yellow background, one cell and one whole row):
//
//   +0   u32  task row UniqueID (`TBkndTask/FixedData` +4)
//   +4   u32  field id `0x0b40xxxx`, or 0xffffffff for the row default
//   +8   u32  flags (opaque; 3 in the whole-row yellow saves, 6 in a
//             font-only save, 0x01000000 family on PROJECT_A's project row)
//   +12  12 B not interpreted
//   +24  4 B  background colour R, G, B, automatic flag (0 = stored)
//   +28  8 B  not interpreted
//   +36  u32  background pattern (1 = solid on every measured fill)
//   +40  u32  font word (opaque; the font formatting is not decoded this wave)
//
// A field-specific entry overrides the same row's 0xffffffff row-default entry.
// PROJECT_A's project row (uid 0) carries both and proves the precedence: its
// visible fields store the grey (82,87,91) while the default stores the dark
// blue (0,55,103).  The automatic flag is MPPUtility.getColor's rule, the same
// one gantt_style.cpp applies to the text styles: any non-zero fourth byte
// means "automatic", i.e. no stored colour.
//
// PROJECT_A's applied Gantt view stores 213 entries: 13 for the project row
// (including the row default) and 10 for each of 20 task rows -- exactly the
// applied `214/CTable` table's fields.  The yellow Highlighted row (uid 10742,
// the capture's ID 10) stores (255,242,0) on nine fields; its tenth, the
// hidden zero-width Indicators field 0x0b4000cd, stores automatic.  The same
// property id also appears in PROJECT_A's view 4 with a 484-byte payload whose
// words do NOT follow this layout; that view is not the applied Gantt view
// (find_applied_view requires a >= 41 KB style record), so this decoder is
// only ever handed the Gantt view's record.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "gantt_style.h"  // Rgb
#include "viewstate.h"    // ParseError, ViewState, ViewDef

namespace msproject::viewstate {

inline constexpr std::uint32_t kViewCellFormatKey = 0x2240000Cu;
inline constexpr std::size_t kCellFormatEntrySize = 44;
inline constexpr std::uint32_t kCellFormatRowDefaultField = 0xFFFFFFFFu;
inline constexpr std::size_t kCellFormatFlagsOffset = 8;
inline constexpr std::size_t kCellFormatBackgroundOffset = 24;
inline constexpr std::size_t kCellFormatPatternOffset = 36;
inline constexpr std::size_t kCellFormatFontWordOffset = 40;

// One decoded 44-byte entry.  `has_background` is false for an automatic
// colour (the fourth colour byte non-zero); the other fields keep what the
// record stores, including the words this decoder does not interpret.
struct CellFormat {
    std::uint32_t row_uid = 0;
    std::uint32_t field_id = 0;
    std::uint32_t flags = 0;
    bool has_background = false;
    Rgb background{};
    std::uint32_t background_pattern = 0;
    std::uint32_t font_word = 0;
};

// The decoded record for one view.  `decoded` is true when the view carried
// the record and parse_cell_formats() accepted it; a view without the record
// (the committed synthetic fixture does not carry one) leaves it false with
// no entries, which is not an error.
struct CellFormatTable {
    bool decoded = false;
    std::vector<CellFormat> entries;

    // The entry that formats (row_uid, field_id): the field-specific entry
    // when the record stores one, otherwise the row's 0xffffffff entry.
    // nullptr when the row has no entry at all.  The first match wins; every
    // measured record stores one entry per (row, field).
    [[nodiscard]] const CellFormat* find(std::uint32_t row_uid,
                                         std::uint32_t field_id) const noexcept;
};

// Decodes a `0x2240000c` payload.  A payload that is not a multiple of the
// measured 44-byte entry is refused (nothing is guessed from a partial entry);
// an empty payload decodes as an empty table.
[[nodiscard]] bool parse_cell_formats(std::span<const std::uint8_t> payload,
                                      CellFormatTable& out,
                                      ParseError* error = nullptr);

// The `0x2240000c` record payload of a decoded CV_iew view, or an empty span
// when the view's main block did not decode or carries no such record.
[[nodiscard]] std::span<const std::uint8_t> cell_format_data_from_view(
    const ViewState& state, const ViewDef& view);

}  // namespace msproject::viewstate
