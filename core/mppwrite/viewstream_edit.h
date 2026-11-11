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
 *   attribute        Bold set   refused (underline, strikethrough, highlight
 *                  Italic set  colour, font colour).  W36-1.3: Italic ON is
 *                               measured by ONE pair (wave35-g3-font-italic: row 1,
 *                               Task Name; flags 0x02000003, mask +10 = 0x200,
 *                               font word 0x04, top byte pinned 0x90 like Bold's),
 *                               so there is no held-out Italic pair and Italic is
 *                               written for that cell only; Italic OFF has no
 *                               pair and is refused.
 *   selection        cell       refused (whole row)
 *   view             uid 2      refused (the other views are unmeasured)
 *   row, field       3 cells    refused (every other cell).  W36-1b.1: Bold ON is
 *                               written for the THREE cells a pair measures,
 *                               (row 1, Task Name) wave30-textstyle, (row 2, Task
 *                               Name) wave35-g3-font-bold-row2 and -range, and
 *                               (row 1, Start) wave35-g3-font-bold-start; the
 *                               caller's row (payload +0, 0xe389d0) and field key
 *                               (payload +4, 0xe389db) are those pairs' words.
 *                               trace-font-item-r6.md derives the row and field
 *                               rule from these same runs, so none is a held-out
 *                               prediction and no pair predicts a later row or the
 *                               joint cell (row 2, Start): those are refused
 *                               (`bold_on_cell_is_measured`).  The project summary
 *                               row (uid 0) and the all-ones sentinel are refused
 *                               too, and no pair separates the row number from
 *                               the UniqueID (equal in every committed pair).
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
 * refused.  The block digest masks one noise byte (payload offset 42,678)
 * because a control saved by another Project session differs there.  Bold OFF
 * stays pinned to the cell its pair measures, row uid 1 / field 0x0B40000E
 * (Task Name): there is no held-out Bold off pair, so any other cell is refused
 * (W35-0.4, RC-W8).
 *
 * THE TOP BYTE.  The font word's top byte (payload +43) is written by no
 * instruction in any traced run (trace-font-item-r6.md section 2): it is what the
 * buffer held, 0x90, 0x6b or 0x00 in the committed Bold saves.  The writer pins
 * 0x90, and a port test compares a save with Project's by masking that one byte
 * by its exact field (the 0x2240000c item, payload +43), never by stream name.
 *
 * Refusals (all before `doc` or the output path is touched; every one has a
 * test in tests/roundtrip_viewstream.cpp):
 *   - a count other than one, an empty or unusable output path, uid 0,
 *     the all-ones row, field id 0, a field id other than Task Name or Start,
 *     an attribute other than Bold and Italic, Italic on any cell but Task Name
 *     of row 1, Italic off, Bold off on any cell but Task Name of row 1,
 *     Bold off without the one measured Bold item, a whole-row selection,
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
/* W36-1.3: the Italic item (wave35-g3-font-italic): the flags word's byte 3 is
 * the attribute mask 0x02 (payload +10 = 0x0200) and the font word's bit 0x04. */
inline constexpr std::uint32_t kItalicCellFormatFlags = 0x02000003u;
inline constexpr std::uint32_t kMeasuredItalicFontWord = 0x90000004u;
/* The pair's applied view: the CV_iew row whose main block Project grew. */
inline constexpr std::uint32_t kMeasuredViewUid = 2u;
/* The pair's formatted cell: the Task Name cell (field id 0x0b40000e) of the
 * row with UniqueID 1.  Bold OFF and Italic ON are measured on this cell only
 * (no held-out pair); Bold ON is measured on three cells (below). */
inline constexpr std::uint32_t kMeasuredCellUid = 1u;
inline constexpr std::uint32_t kMeasuredCellFieldId = 0x0B40000Eu;
/* W36-1.2: the Start column's field key (StartText), measured by
 * wave35-g3-font-bold-start (row 1 only).  Every other field is unmeasured and
 * refused. */
inline constexpr std::uint32_t kMeasuredStartFieldId = 0x0B400505u;
/* The second measured row: wave35-g3-font-bold-row2 (Task Name only). */
inline constexpr std::uint32_t kMeasuredSecondCellUid = 2u;

/* W36-1b.1 (R36G1-1): the cells Bold ON is measured on, each by its own pair:
 * (row 1, Task Name), (row 2, Task Name) and (row 1, Start).  Every other cell,
 * the joint cell (row 2, Start) included, has no pair and is refused.  The app
 * calls this too, so the plan-side refusal and the writer cannot drift. */
inline constexpr bool bold_on_cell_is_measured(std::uint32_t uid,
                                               std::uint32_t field_id) noexcept {
    return (field_id == kMeasuredCellFieldId &&
            (uid == kMeasuredCellUid || uid == kMeasuredSecondCellUid)) ||
           (field_id == kMeasuredStartFieldId && uid == kMeasuredCellUid);
}

/* The Font group's attributes.  Bold and (W36-1.3) Italic are measured; the rest
 * are refused. */
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

/* One measured cell-format append.  The row and field id are the caller's (Bold
 * ON: one of the three measured cells, `bold_on_cell_is_measured`; Bold OFF: the
 * pair's Task Name cell of row 1, kMeasuredCellUid / kMeasuredCellFieldId); every
 * other word is fixed by the pairs. */
struct ViewCellStyleEdit {
    std::uint32_t view_uid = 0;  /* the CV_iew row; measured 2 */
    std::uint32_t uid = 0;       /* the formatted row; not 0 */
    std::uint32_t field_id = 0;  /* the cell's field id; measured 0x0b40000e, 0x0b400505 */
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
