/*
 * viewstream_edit.h -- one cell-format item appended to a 214/CV_iew block.
 * W34-2.1 (30 October 2026).
 *
 * WHY THIS EXISTS.  The Font group's Bold button is the first view-state
 * command wave 30 measured (`wave30-textstyle`): Project appends one
 * `0x2240000c` item to the applied view's main (`0x20400006`) block, rewrites
 * the block's own size words and item count, shifts every later block by the
 * item's size, re-points every VarMeta entry that names a later block, and
 * updates the CV_iew Var2Data size in `214/Props`.  The port had no writer for
 * a `214/` stream; this is it, for the one measured shape.
 *
 * THE MEASURED RULES (full derivation in spec/writers/view-stream-214.md; the
 * pair has a null save, so per-save noise is masked and only the edit's effects
 * are compared):
 *
 *   block    `214/CV_iew/Var2Data` is a strict sequence of Var2Data blocks
 *            `[u32 payload_length][payload]`, back to back from offset 0 with
 *            no gap and no trailer (the seven measured blocks fill the stream
 *            exactly).  A block payload is
 *              +0   u32  payload_length - 4
 *              +4   u32  payload_length - 4
 *              +8   u32  0x00010000
 *              +12  u16  item count (the word at +14 is preserved)
 *              +16  items, back to back: u32 length, u32 id, u32 kind,
 *                   byte[length] payload; an odd length is followed by one
 *                   zero pad byte so the next item starts on an even offset.
 *            Pair: block at 42,576 (uid 2, key 0x20400006) 42,692 -> 42,748 B,
 *            size words 0x0000a6c0 -> 0x0000a6f8, count 10 -> 11.
 *   item     The appended item is 56 B: length 44, id 0x2240000c, kind 1, and a
 *            44-byte cell-format entry (core/viewstate/cell_format.h decodes
 *            the entry; its payload is a sequence of 44-byte entries):
 *              +0  u32  the formatted row's UniqueID
 *              +4  u32  the cell's field id
 *              +8  u32  flags 0x01000003 (measured on the Bold save)
 *              +12 u32  0xff000000, +16 0, +20 0
 *              +24 u32  0xff000000, +28 0, +32 0
 *              +36 u32  1 (pattern)
 *              +40 u32  font word 0x90000001 (measured on the Bold save)
 *            Pair: the control's target block carries no `0x2240000c` item at
 *            all; Project appends the first one.
 *   offsets  Every byte after the insertion point is the control's byte 56
 *            positions earlier; every VarMeta entry naming a later block moves
 *            +56 (five measured: uid 1/0x22400014, uid 2/0x22400014,
 *            uid 3/0x20400006, uid 3/0x22400014, uid 3/0x2240002f); the grown
 *            block's own entry keeps its offset; VarMeta +0x14 becomes the new
 *            Var2Data size (100,178 -> 100,234).
 *   props    `214/Props` keeps its size; only the CV_iew Var2Data size word in
 *            its typed-property tail moves (tag BIG-endian 0x01000100, value
 *            100,178 -> 100,234; byte 9,962 0x52 -> 0x8a).
 *   marker   The Props14 edit marker (`edit_marker.h`) is written on every
 *            save, as every measured editor save does.
 *
 * THE FONT WORD.  The W34-0.2 static pass did not settle any bit of the font
 * word: the literal does not appear in the Ghidra model, the `0x2240000c`
 * references it does expose are property get/set calls (FUN_140395150 reads
 * the record; FUN_1402a007c/FUN_1403a3910 fetch it by id), and the only
 * "bold"/"italic" strings are the HTML export serializer (0x140efbe00).  The
 * word is therefore accepted EXACTLY as the pair measured it (0x90000001), and
 * only for Bold; no other bit is claimed.
 *
 * THE CAPABILITY MATRIX (brief rule 1: a command is enabled only for a shape a
 * pair measures end to end, with the null save separating noise from the edit,
 * and every other shape refused up front):
 *
 *   attribute        Bold set   refused (italic, underline, strikethrough,
 *                               highlight colour, font colour)
 *   selection        cell       refused (whole row)
 *   view             uid 2      refused (the other views are unmeasured)
 *   row              uid != 0   refused (the project summary row, uid 0)
 *   block state      no 0x2240000c item in the target block
 *   edits per save   exactly one
 *
 * "Bold set" appends (`wave30-textstyle` starts from an unformatted control).
 * Bold OFF (W35-3.4, `wave35-g3-font-bold-off`) is the second measured
 * direction: on a block that carries exactly one cell format item, the measured
 * Bold item for the measured cell, last in the block, Project rewrites the
 * item IN PLACE (flags 0x01000003 -> 0x00000003, font word 0x90000001 ->
 * 0x90000000; no size, offset or Props change), and so does `enabled = false`
 * here.  Every other un-format (another item, another layout, a second item) is
 * refused.  The shells still build only `enabled = true` edits.  The block
 * digest masks one noise byte (payload offset 42,678) because a control saved
 * by another Project session differs there.  The payload's row uid and
 * field id are pinned to the cell the pair measures, uid 1 / field 0x0B40000E
 * (Task/Name); any other row or field is refused (W35-0.4, RC-W8).
 *
 * Refusals (all before `doc` or the output path is touched; every one has a
 * test in tests/roundtrip_viewstream.cpp):
 *   - a count other than one, an empty or unusable output path, uid 0, a uid
 *     other than the measured 1, field id 0, a field id other than the
 *     measured 0x0B40000E, an attribute other than Bold, Bold off without the one measured Bold item, a whole-row selection,
 *     a view uid other than the measured 2;
 *   - a missing 214/CV_iew/VarMeta, Var2Data or 214/Props stream; a VarMeta
 *     that is not 24 + 12*n, a magic or declared count that is not the physical
 *     entry count, a +0x14 word that disagrees with Var2Data, an entry offset
 *     that is not a block start, overlapping/gapped blocks, or a 0xFFFFFFFF
 *     offset;
 *   - no (uid 2, 0x20400006) entry, two of them, a target block whose payload
 *     is shorter than its 16-byte header, a size word that is not payload - 4,
 *     an item count that is not the walked item count, an item id outside the
 *     view property family, an item that runs past the payload, a non-zero pad
 *     byte after an odd item, a block that already carries a 0x2240000c item
 *     (replace/un-bold/row-default precedence are unmeasured);
 *   - a 214/Props that has no CV_iew size tag or two of them;
 *   - (R34G4-4) a file whose applied view is not uid 2: 214/CEdl names the
 *     applied view (0x26400018) and the CV_iew row with that caption must be
 *     uid 2 (the committed wave31-resource-sheet.mpp and wave31-usage.mpp apply
 *     other views); and a uid-2 main block that is not the measured control's
 *     outside its two size words and item count (an FNV-1a 64 digest of the
 *     42,692 payload bytes), which pins the text style the Bold item sits on.
 *
 * ALL-OR-NOTHING AND PROVEN FROM DISK.  The edit is applied to a copy of the
 * document; the package is written to `path`, re-opened and required to hold
 * exactly the bytes built, re-emit byte-identically, pass `CfbDocument::check`,
 * and contain exactly the requested state: the target block's prefix and the
 * appended 56 bytes, its size words and count, every later VarMeta offset, the
 * VarMeta +0x14 word, the shifted Var2Data tail, the 214/Props size value, the
 * Props14 marker, and no stream outside {214/CV_iew/VarMeta,
 * 214/CV_iew/Var2Data, 214/Props, Props14} changed.  On success `doc` is
 * replaced by the re-read document.
 */

#ifndef MPPWRITE_VIEWSTREAM_EDIT_H
#define MPPWRITE_VIEWSTREAM_EDIT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cfb_writer.h"

namespace mppwrite {

/* The measured view stream and the item this writer appends. */
inline constexpr char const* kViewTable = "214/CV_iew";
inline constexpr char const* kViewProps = "214/Props";
inline constexpr char const* kEdlTable = "214/CEdl";
/* The CEdl record that names the applied view (UTF-16). */
inline constexpr std::uint32_t kEdlViewNameId = 0x26400018u;
inline constexpr std::uint32_t kViewStateKey = 0x20400006u;
inline constexpr std::uint32_t kViewCellFormatItemId = 0x2240000Cu;
inline constexpr std::uint32_t kViewCellFormatItemKind = 1u;
inline constexpr std::size_t kViewItemHeaderSize = 12;
inline constexpr std::size_t kCellFormatEntrySize = 44;
inline constexpr std::uint32_t kCellFormatFlags = 0x01000003u;
inline constexpr std::uint32_t kMeasuredFontWord = 0x90000001u;
/* The pair's applied view: the CV_iew row whose main block Project grew. */
inline constexpr std::uint32_t kMeasuredViewUid = 2u;
/* The pair's formatted cell: the Task Name cell (field id 0x0b40000e) of the
 * row with UniqueID 1.  Every other row and field is unmeasured and refused. */
inline constexpr std::uint32_t kMeasuredCellUid = 1u;
inline constexpr std::uint32_t kMeasuredCellFieldId = 0x0B40000Eu;

/* The Font group's attributes.  Only Bold is measured; the rest are refused. */
enum class ViewCellAttribute : std::uint8_t {
    bold = 0,
    italic = 1,
    underline = 2,
    strikethrough = 3,
    highlight_colour = 4,
    font_colour = 5,
};

/* One cell or the whole row.  Only the cell is measured. */
enum class ViewCellSelection : std::uint8_t {
    cell = 0,
    whole_row = 1,
};

/* One measured cell-format append.  The cell (row uid + field id) is pinned to
 * the pair's (kMeasuredCellUid, kMeasuredCellFieldId); every other word is
 * fixed by the pair. */
struct ViewCellStyleEdit {
    std::uint32_t view_uid = 0;  /* the CV_iew row; measured 2 */
    std::uint32_t uid = 0;       /* the formatted row's UniqueID; not 0 */
    std::uint32_t field_id = 0;  /* the cell's field id; measured 0x0b40000e */
    ViewCellAttribute attribute = ViewCellAttribute::bold;
    ViewCellSelection selection = ViewCellSelection::cell;
    bool enabled = true; /* true appends the Bold item; false turns the one Bold
                          * item the block carries off in place (W35-3.4) */
};

/* What the write proved from the file on disk. */
struct ViewCellStyleWrite {
    std::uint32_t view_uid = 0;
    std::uint32_t uid = 0;
    std::uint32_t field_id = 0;
    std::size_t block_offset = 0;         /* the target var block */
    std::size_t block_payload_before = 0; /* payload bytes, control */
    std::size_t block_payload_after = 0;  /* payload bytes, written */
    std::size_t item_count_before = 0;
    std::size_t item_count_after = 0;
    std::size_t appended_bytes = 0;       /* 12 + 44 */
    std::size_t var2_bytes_before = 0;
    std::size_t var2_bytes_after = 0;
    std::size_t repointed_entries = 0;    /* entries moved +56 */
    std::size_t props_value_before = 0;   /* 214/Props CV_iew size */
    std::size_t props_value_after = 0;
};

/*
 * Apply the ONE edit in `edits` to `doc`, write the package to `path`,
 * re-open it and prove every fact above from the bytes.  Throws Error on a
 * refusal or a mismatch; then neither `doc` nor a new output file has been
 * left behind.  `edits.size() != 1` is refused (one item per save measured).
 *
 * `dry_run` (W34-4.5) runs every check and builds and re-checks the staged
 * streams, then returns the planned counts without writing `path` (which may be
 * empty) or replacing `doc`.  The app shells use it to refuse a file whose view
 * stream is not the measured shape before a Font Bold edit is queued.
 */
ViewCellStyleWrite save_view_cell_style(
    CfbDocument& doc, std::vector<ViewCellStyleEdit> const& edits,
    std::string const& path, bool dry_run = false);

} /* namespace mppwrite */

#endif /* MPPWRITE_VIEWSTREAM_EDIT_H */
