// core/viewstate -- decoder and targeted writer for the MPP `214/` view-state
// streams.
//
// WHAT THIS IS
//
// MS Project keeps per-project view/table state under a `214/` storage tree.
// Each class (CTable, CV_iew, CFilter, CGrouping, CEdl, CDrawing, CMap,
// CReport) is a small "table-like" collection with the same four parts and
// framing as the `114/` table streams:
//
//   FixedData   rows of a class-specific fixed size
//   FixedMeta   16-byte header + declared_count entries
//   VarMeta     24-byte header + {u32 uid, u32 offset, u32 key} entries
//   Var2Data    back-to-back blocks, each `u32 payload_length` + payload
//
// Measured header layouts (little-endian; docs/handoff/w56.md has the evidence):
//
//   FixedMeta  +0  u32 magic 0xfadfadba
//              +4  u32 flags (4 in every measured stream)
//              +8  u32 declared_count
//              +12 u32 fixed_data_size
//              +16 entries of entry_size = (size - 16) / declared_count
//                  +0 u16 state (0)
//                  +2 u16 var_count (equals the VarMeta entry count for the uid)
//                  +4 u32 fixed row offset (contiguous, 0-based)
//                  +8 .. entry_size  opaque (one u16 changes when a table is
//                                    edited; semantics unidentified)
//
//   VarMeta    +0  u32 magic 0xfadfadba
//              +4  u32 reserved (0)
//              +8  u32 declared_count
//              +12 u32 reserved (0)
//              +16 u32 reserved (0)
//              +20 u32 var2_size (equals the Var2Data stream size)
//              +24 entries {u32 uid, u32 offset, u32 key}; offset points at the
//                  block's length word inside Var2Data and is NOT necessarily in
//                  block order (CV_iew stores entries grouped by uid)
//
//   FixedData rows: a row starts with u32 ordinal (1-based, matches the VarMeta
//   uid) and a NUL-terminated UTF-16 caption at +4.  Every measured row ends in
//   a 16-byte identity/GUID-like value that changes on every Project save.  The
//   230-byte classes (CTable, CFilter, CGrouping) carry a second UTF-16 caption
//   at +136 (the display name without the accelerator '&').
//
// WHAT IS DECODED (measured, see docs/handoff/w56.md)
//
//   `214/CTable` is the persisted table-definition collection.  Each row's
//   Var2Data block with key 0x20400006 is:
//
//       +0  u32 table ordinal (matches FixedData +0 and the VarMeta uid)
//       +4  u16 last column index (column_count - 1)
//       +6  u8  format word (1 in every measured table)
//       +7  u8  date-format override; 0xff on every non-edited probe file
//               (interpretation: not set) and 0x05 after the Entry table's
//               DateFormat is set to 5 through TableEdit.  This is the only
//               byte that differs in the whole 214 tree between the synthetic
//               col and date fixtures.
//       +8  u32 table flags; 6 for the stock Entry table, 2 after the wave-10
//               TableEdit call that also skipped the column add.  Meaning not
//               established, exposed as opaque.
//       +12 column records, 115 bytes each:
//               +0  u32 field id (0x0b40xxxx task field, 0xc40xxxx resource)
//               +4  9 bytes whose first byte is very likely the column WIDTH in
//                   characters: the same field (Task Name) reads 0x46, 0x2a and
//                   0x18 in three tables of one corpus file, and the wave-14
//                   TableEdit replacement that passed Width=12 kept 0x0c there.
//                   That byte was 0x0c before the call too, so the mapping is
//                   inference, not a controlled measurement.  The other eight
//                   bytes are opaque.
//               +13 UTF-16 NUL-terminated title (51 code units capacity)
//
//   `214/CV_iew` rows are view definitions: ordinal at +0, UTF-16 name at +4,
//   16-byte identity at row_size-16 = +122 for the measured 138-byte row.  Each
//   view owns VarMeta blocks keyed 0x20400006 (main view state), 0x22400014
//   (notes) and 0x2240002f (timeline view data).
//
//   The main block IS item-framed, and that framing is decoded here (wave 15,
//   W62; evidence in docs/handoff/w62.md):
//
//       +0  u32 payload_size - 4
//       +4  u32 the same value
//       +8  u16 opaque (varies per view: 0, 0x14, 0x28f, 0x2ef, 0x51d8 measured)
//       +10 u16 0x0001 in every measured block
//       +12 u16 item count (6..17 measured; the u16 matters: three corpus blocks
//               have a non-zero word at +14, which a u32 read would take as part
//               of the count)
//       +14 u16 opaque (0 in most measured blocks; non-zero in 3 of 25)
//       +16 items, back to back:
//              +0 u32 payload length
//              +4 u32 property id (0x2240xxxx)
//              +8 u32 kind (0, 1, 12, 23, 24, 25, 26 and 27 measured)
//              +12 payload[length], then one pad byte when the item size is odd
//
//   The walk must consume the payload exactly and the item count must match;
//   otherwise the block stays opaque.  Note the 16-byte prefix is NOT a Props
//   header: the count is a u16 at +12, not the u32 at +8.
//
//   Item ids decoded from their payloads (UTF-16 names unless noted):
//
//       0x2240002a  view display name ("Gantt Chart with Timeline", "Timeline")
//       0x2240000a  single view: applied table name ("&Entry", "Rollup Table");
//                   combination view: top pane view name ("&Gantt Chart")
//       0x2240000b  single view: applied filter ("&All Tasks");
//                   combination view: bottom pane view name ("Time&line")
//       0x22400018  applied grouping ("No Group")
//       0x22400014  notes: nine u32 chunk lengths followed by that many bytes
//                   (3x3 layout; verified sum on every measured view)
//       0x22400009  970-byte view-format template; the first four u32 are page
//                   margins in 1/1000 inch (500 = 0.5", Project's default), the
//                   fifth reads 2000.  The rest is opaque.
//       0x22400008  Gantt bar-style blob (contains the bar names "Standard",
//                   "Task", "Split", "Milestone", ...).  Opaque.
//       0x2240002f  timeline XML.  Opaque.
//
//   NOT decoded: row height and splitter position.  They are not in any item
//   this stream can name, and no measured save changes them.  Column widths are
//   not in CV_iew either: the width is the first of the nine opaque bytes of a
//   `214/CTable` column record (see the column record note above).
//
//   A block whose item walk does not validate is left opaque with a warning;
//   the collection still parses, and the encoder still copies it verbatim.
//
//   `214/CEdl` has one fixed row and one Var2Data block whose payload is a
//   complete Props-format document (14 records in the prefs base fixture, 15 in
//   the two later saves).  This module parses it with core/props
//   (props::parse_props) rather than re-implementing the framing.
//
//   `214/Props` is a normal Props stream and is decoded by core/props directly;
//   this module does not duplicate that.
//
// THE WRITER (wave 14 W58, wave 15 W62)
//
// encode_view_state() re-serialises all four parts from a parsed ViewState:
// FixedData verbatim (Project's per-save identity bytes are preserved, never
// regenerated), FixedMeta and VarMeta rebuilt from their parsed headers and
// entries with declared_count, fixed_data_size, var2_size and every VarMeta
// offset recomputed from the byte streams, and Var2Data rebuilt from the block
// walk.  With no mutation the output is byte-identical to the input; the tests
// prove that and `viewstate_report --roundtrip` re-checks it on extracted
// fixture trees.
//
// Three CTable mutators:
//
//   set_table_date_format  writes CTable block payload +7.  0xff is the
//                          default in every measured file; 0x05 is what
//                          Project wrote for DateFormat=5.
//   set_table_column       replaces the 115-byte record at an existing column
//                          index: u32 field id at record +0 and a zero-filled
//                          UTF-16 title at record +13 (51 code-unit capacity).
//                          The nine bytes at +4 are preserved.  The oracle's
//                          TableEdit with ColumnPosition=0 replaced the record
//                          at index 1 and left the 1047-byte block unchanged.
//                          The measured replacement also zero-filled the rest of
//                          the title area after its NUL.
//   append_table_column    inserts a new 115-byte record AFTER the last column,
//                          grows the block's u32 length word by 115, sets
//                          payload +4 (the u16 last-column index) to the new
//                          count-1, shifts every later Var2 block and VarMeta
//                          offset by 115, and re-encodes.  The inserted record's
//                          nine bytes are the ones Project wrote for a fresh
//                          column in wave 14: 0c 10 08 00 08 01 00 00 00.
//
//                          APPEND IS WRITTEN BY US, NOT BY PROJECT.  Project's
//                          automation surface cannot append: TableEdit with
//                          ColumnPosition=0 replaces index 1, ColumnPosition 8/9
//                          is rejected, Application.ColumnInsert hangs this
//                          build and Application.Tables() wedges.  The append
//                          layout is therefore structural (decode-back and a
//                          coordinator Project-resave gate), not an oracle
//                          measurement of Project's own append.
//
// Not provided, because not measured:
//
//   * editing CV_iew, CFilter, CGrouping, CEdl, CDrawing, CMap or CReport
//     payload bodies.  The CV_iew main block is decoded (above) but not edited;
//     every payload is copied verbatim by the encoder.
//   * generating Project identity/GUID values.  Every row/entry identity is
//     preserved verbatim because Project regenerates it on each save.

#pragma once

#include "props.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace msproject::viewstate {

// Stream framing constants.  Every one of these was measured on the three
// synthetic prefs fixtures (96 streams each) and cross-checked on the other
// synthetic probes; see docs/handoff/w56.md.
inline constexpr std::uint32_t kMetaMagic = 0xFADFADBAu;
inline constexpr std::size_t kFixedMetaHeaderSize = 16;
inline constexpr std::size_t kVarMetaHeaderSize = 24;
inline constexpr std::uint32_t kColumnBlockKey = 0x20400006u;
inline constexpr std::uint32_t kViewNotesKey = 0x22400014u;
inline constexpr std::uint32_t kTimelineViewKey = 0x2240002Fu;
inline constexpr std::uint8_t kDefaultDateFormat = 0xFFu;
inline constexpr std::size_t kTableBlockHeaderSize = 12;
inline constexpr std::size_t kColumnRecordSize = 115;
inline constexpr std::size_t kColumnTitleOffset = 13;
inline constexpr std::size_t kColumnTitleCapacity = 51;
inline constexpr std::size_t kIdentitySize = 16;
inline constexpr std::size_t kDisplayCaptionOffset = 136;
inline constexpr std::size_t kDisplayCaptionRowSize = 230;

// The nine bytes Project wrote for a fresh column in wave 14 (Number1 and Text1
// replacements both carried them).  Only the first byte is understood (very
// likely the width, 12 in that call); append copies the whole measured run
// rather than synthesising a header.
inline constexpr std::array<std::uint8_t, 9> kFreshColumnHeader = {0x0C, 0x10, 0x08, 0x00,
                                                                   0x08, 0x01, 0x00, 0x00,
                                                                   0x00};

// CV_iew main-block item ids (the payloads are decoded, not edited).
inline constexpr std::uint32_t kViewNameKey = 0x2240002Au;
inline constexpr std::uint32_t kViewTableKey = 0x2240000Au;
inline constexpr std::uint32_t kViewFilterKey = 0x2240000Bu;
inline constexpr std::uint32_t kViewGroupKey = 0x22400018u;
inline constexpr std::uint32_t kViewOptionsKey = 0x22400009u;
inline constexpr std::uint32_t kViewFormatKey = 0x22400008u;
inline constexpr std::size_t kViewMainHeaderSize = 16;
inline constexpr std::size_t kViewItemHeaderSize = 12;
inline constexpr std::size_t kViewNotesHeaderSize = 36;
inline constexpr std::size_t kViewNotesChunks = 9;
inline constexpr std::size_t kViewOptionsSize = 970;
// Page margins are the first four u32 of the 0x22400009 template, in 1/1000
// inch; 500 is Project's 0.5" default and is what every synthetic view stores.
inline constexpr std::size_t kViewOptionsMarginOffset = 0;
inline constexpr std::size_t kViewOptionsMarginSize = 16;

struct ParseError {
    std::string message;
    std::size_t offset = 0;
};

// One FixedMeta entry.  `state`, `var_count` and `fixed_offset` are the three
// proved words; `opaque_tail` is everything from entry byte +8 (one measured
// u16 changes when the CTable definition is edited, but the meaning is not
// established, so it is not named).
struct FixedMetaEntry {
    std::uint32_t entry_offset = 0;
    std::uint16_t state = 0;
    std::uint16_t var_count = 0;
    std::uint32_t fixed_offset = 0;
    std::vector<std::uint8_t> opaque_tail;
};

struct FixedMetaStream {
    std::uint32_t magic = 0;
    std::uint32_t flags = 0;
    std::uint32_t declared_count = 0;
    std::uint32_t fixed_data_size = 0;
    std::uint32_t entry_size = 0;
    std::vector<FixedMetaEntry> entries;
};

struct VarMetaEntry {
    std::uint32_t uid = 0;
    std::uint32_t offset = 0;
    std::uint32_t key = 0;
};

struct VarMetaStream {
    std::uint32_t magic = 0;
    std::uint32_t declared_count = 0;
    std::uint32_t var2_size = 0;
    std::uint32_t trailing_bytes = 0;
    std::vector<VarMetaEntry> entries;
};

// A block as found by walking Var2Data sequentially.  `offset` is the position
// of the length word; the payload starts at offset + 4 and is payload_length
// bytes long.
struct VarBlock {
    std::uint32_t offset = 0;
    std::uint32_t payload_length = 0;
};

struct Var2Stream {
    std::uint32_t size = 0;
    std::vector<VarBlock> blocks;
};

// One decoded CTable column record.  Only the field id and the UTF-16 title are
// proved; the nine bytes between them are preserved verbatim.
struct ColumnDef {
    std::uint32_t record_offset = 0;
    std::uint32_t field_id = 0;
    std::array<std::uint8_t, 9> opaque_header{};
    std::string title;
};

struct TableDef {
    std::uint32_t row_index = 0;
    std::uint32_t ordinal = 0;
    std::string caption;
    std::string display_caption;
    std::array<std::uint8_t, kIdentitySize> identity{};

    std::uint32_t block_offset = 0;
    std::uint32_t block_length = 0;
    bool decoded = false;

    std::uint32_t header_ordinal = 0;
    std::uint16_t last_column_index = 0;
    std::uint8_t format_word = 0;
    std::uint8_t date_format = 0;
    std::uint32_t flags = 0;

    std::vector<ColumnDef> columns;

    [[nodiscard]] std::uint32_t column_count() const {
        return static_cast<std::uint32_t>(columns.size());
    }
};

// One Var2Data block owned by a view, with the class-specific meaning of its
// key exposed as a hint.  The payload stays opaque.
struct ViewBlockRef {
    std::uint32_t uid = 0;
    std::uint32_t key = 0;
    std::uint32_t offset = 0;
    std::uint32_t payload_length = 0;
    std::string kind;
    std::string preview;
};

// One item of a CV_iew main (`0x20400006`) block.  `record_offset` is relative
// to the start of the block payload; the two-byte pad after an odd-sized item
// is not part of `payload_length`.
struct ViewRecord {
    std::uint32_t record_offset = 0;
    std::uint32_t payload_length = 0;
    std::uint32_t id = 0;
    std::uint32_t kind = 0;
    std::string preview;
};

struct ViewDef {
    std::uint32_t row_index = 0;
    std::uint32_t ordinal = 0;
    std::string name;
    std::array<std::uint8_t, kIdentitySize> identity{};
    std::vector<ViewBlockRef> blocks;

    // The main block, when its item framing validated.  `main_decoded == false`
    // means the block stays opaque (with a warning on the ViewState).
    bool main_decoded = false;
    std::uint32_t main_block_offset = 0;
    std::uint32_t main_payload_length = 0;
    std::vector<ViewRecord> records;

    // UTF-16 names carried by the main block (accelerators included).
    std::string display_name;      // 0x2240002a
    std::string table_or_top;      // 0x2240000a
    std::string filter_or_bottom;  // 0x2240000b
    std::string group_name;        // 0x22400018

    // Inference, not a measured flag: true when `table_or_top` matches the name
    // of a view in the same collection (accelerators ignored), which is the
    // measured shape of a combination view.  Single views name a table there.
    bool looks_like_combination = false;

    // 0x22400014: nine u32 chunk lengths followed by the chunks (3x3 layout).
    bool notes_decoded = false;
    std::array<std::uint32_t, kViewNotesChunks> notes_chunk_lengths{};
    std::array<std::string, kViewNotesChunks> notes_previews{};

    // 0x22400009: the 970-byte view-format template.  Only the page margins are
    // decoded; `options_decoded` means the template had the measured size.
    bool options_decoded = false;
    std::array<std::uint32_t, 4> page_margins_thousandths{};
    std::uint32_t page_margin_extra = 0;
};

struct FixedRow {
    std::uint32_t row_index = 0;
    std::uint32_t ordinal = 0;
    std::string caption;
    std::string display_caption;
    std::array<std::uint8_t, kIdentitySize> identity{};
};

// A Props-format document found inside a class block (measured for CEdl only).
struct EmbeddedProps {
    std::uint32_t block_offset = 0;
    props::PropsDocument document;
};

// The four decoded parts of one `214/<Class>` collection.  Byte copies are kept
// so a ViewState stays valid after the caller's buffers go away.
struct ViewState {
    std::string klass;

    // True only when parse_view_state() returned true.  The writer refuses a
    // hand-built or partially parsed state, because the four raw byte copies
    // below are what it round-trips.
    bool parsed = false;

    std::vector<std::uint8_t> fixed_data;
    std::vector<std::uint8_t> fixed_meta_bytes;
    std::vector<std::uint8_t> var_meta_bytes;
    std::vector<std::uint8_t> var2_data;

    FixedMetaStream fixed_meta;
    VarMetaStream var_meta;
    Var2Stream var2;

    std::uint32_t row_size = 0;
    std::vector<FixedRow> rows;
    std::vector<VarBlock> blocks;
    std::vector<ViewBlockRef> block_refs;
    std::vector<TableDef> tables;
    std::vector<ViewDef> views;
    std::vector<EmbeddedProps> embedded_props;
    std::vector<std::string> warnings;

    [[nodiscard]] std::span<const std::uint8_t> block_bytes(const VarBlock& block) const;
    [[nodiscard]] const ViewBlockRef* find_block_ref(std::uint32_t uid,
                                                     std::uint32_t key) const;
};

struct PartSet {
    std::span<const std::uint8_t> fixed_data;
    std::span<const std::uint8_t> fixed_meta;
    std::span<const std::uint8_t> var_meta;
    std::span<const std::uint8_t> var2_data;
};

// ---------------------------------------------------------------------------
// Writer (wave 14, W58).  See the file header for what is and is not proved.
// ---------------------------------------------------------------------------

struct WriteError {
    std::string message;
};

// Owning output of encode_view_state().  PartSet is non-owning, so the writer
// needs somewhere to put the bytes; parts() exposes them in the decoder's form
// and the individual vectors stay available for tests and callers.
//
// The four parts are independent top-level streams.  The caller is responsible
// for putting them back into a .mpp (this module does not open CFB files).
struct EncodedParts {
    std::vector<std::uint8_t> fixed_data;
    std::vector<std::uint8_t> fixed_meta;
    std::vector<std::uint8_t> var_meta;
    std::vector<std::uint8_t> var2_data;

    [[nodiscard]] PartSet parts() const;
};

// Re-serialises all four parts from a parsed ViewState, recomputing the
// FixedMeta/VarMeta header words and every VarMeta offset from the byte
// streams.  FixedData and the 16-byte per-row identities are copied verbatim;
// no identity is synthesised.  With no mutation the output is byte-identical to
// the input that produced `state`.  Returns false (and leaves `out` empty) if
// the state did not come from a successful parse_view_state() or if the parsed
// structure cannot be re-serialised consistently.
[[nodiscard]] bool encode_view_state(const ViewState& state,
                                     EncodedParts& out,
                                     WriteError* error = nullptr);

// Replaces the CTable date-format byte at block payload +7 (0xff = default;
// 0x05 is the value Project wrote for DateFormat=5).  Refuses a table index
// that is out of range or whose column block was not decoded, and leaves the
// ViewState untouched on failure.
[[nodiscard]] bool set_table_date_format(ViewState& state,
                                         std::size_t table_index,
                                         std::uint8_t date_format,
                                         WriteError* error = nullptr);

// Replaces the 115-byte column record at `column_index` in `table_index`:
// the u32 field id at record +0 and the UTF-16 title at record +13.  The nine
// opaque bytes at +4 are preserved.  `title` must be printable ASCII and at
// most 51 code units; the title is zero-filled to the end of the record, which
// is what the measured TableEdit replacement wrote.  This edits an existing
// column only; appending is not supported (it is not measured).  Refuses a bad
// index, field id 0, a too-long title or a table whose column block was not
// decoded, and leaves the ViewState untouched on failure.
[[nodiscard]] bool set_table_column(ViewState& state,
                                    std::size_t table_index,
                                    std::size_t column_index,
                                    std::uint32_t field_id,
                                    std::string_view title,
                                    WriteError* error = nullptr);

// Inserts a new 115-byte column record AFTER the last column of `table_index`:
// field id at +0, the measured fresh-column nine bytes at +4, a UTF-16 title at
// +13 (zero-filled, 51 code-unit capacity), then grows the block's u32 length
// word by 115, sets payload +4 (`last_column_index`) to the new count - 1,
// shifts every later Var2Data block and every later VarMeta offset by 115 and
// updates the parsed table.  encode_view_state() then recomputes the Var2Data
// frame, VarMeta `var2_size` and header words; nothing else is touched.
//
// This is OURS: Project's automation surface cannot append a column (wave-14
// measurement; see the file header).  The record is the one Project wrote for a
// fresh column, but Project never appends one through COM here.
//
// Refuses (and leaves the ViewState untouched) when: the state did not come
// from parse_view_state(), the table index is out of range or the table block
// was not decoded, the field id is 0, the title is longer than 51 code units or
// is not printable ASCII, or the table already has 0x10000 columns (the u16
// `last_column_index` cannot grow further).
[[nodiscard]] bool append_table_column(ViewState& state,
                                       std::size_t table_index,
                                       std::uint32_t field_id,
                                       std::string_view title,
                                       WriteError* error = nullptr);

// ---------------------------------------------------------------------------
// The seam for the app (wave 15, W62).
//
// app/document.cpp should apply table edits through apply_table_edits() and
// never touch ViewState fields.  A TableEdit is one of the three measured
// CTable mutations; build one with the helpers below.
// ---------------------------------------------------------------------------

struct TableEdit {
    enum class Kind : std::uint8_t {
        date_format,     // set_table_date_format
        replace_column,  // set_table_column
        append_column,   // append_table_column
    };

    Kind kind = Kind::date_format;
    std::size_t table_index = 0;
    std::size_t column_index = 0;       // replace_column only
    std::uint8_t date_format = kDefaultDateFormat;
    std::uint32_t field_id = 0;         // replace_column / append_column
    std::string title;                  // replace_column / append_column
};

[[nodiscard]] TableEdit table_date_format_edit(std::size_t table_index,
                                               std::uint8_t date_format);
[[nodiscard]] TableEdit table_column_replace_edit(std::size_t table_index,
                                                  std::size_t column_index,
                                                  std::uint32_t field_id,
                                                  std::string_view title);
[[nodiscard]] TableEdit table_column_append_edit(std::size_t table_index,
                                                 std::uint32_t field_id,
                                                 std::string_view title);

// Applies `edits` in order to a copy of `state` and re-encodes all four parts.
// `state` is not modified.  On failure `out` is left empty, `error` carries a
// message prefixed with the edit index, and the caller still has its original
// state.  An empty edit list is the no-op encode.
[[nodiscard]] bool apply_table_edits(const ViewState& state,
                                     std::span<const TableEdit> edits,
                                     EncodedParts& out,
                                     WriteError* error = nullptr);

// The class names measured under 214/.  Unknown classes still parse with the
// generic framing and every block stays opaque.
[[nodiscard]] bool is_known_class(std::string_view klass);

// Class-specific meaning of a Var2Data key, as a stable short label.  It is a
// report hint only; no payload layout is claimed for any of these keys.
[[nodiscard]] std::string_view block_kind(std::string_view klass, std::uint32_t key);

[[nodiscard]] bool parse_fixed_meta(std::span<const std::uint8_t> data,
                                    FixedMetaStream& out,
                                    ParseError* error = nullptr);
[[nodiscard]] bool parse_var_meta(std::span<const std::uint8_t> data,
                                  VarMetaStream& out,
                                  ParseError* error = nullptr);
[[nodiscard]] bool parse_var2(std::span<const std::uint8_t> data,
                              Var2Stream& out,
                              ParseError* error = nullptr);

// Parses a CV_iew main (`0x20400006`) block payload into its item list.  The
// header's item count must match the walk, every id must carry the 0x0040 byte
// in position 2, and the walk must land exactly on the payload end (odd-sized
// items are followed by one pad byte).  Returns false with a message otherwise;
// an empty payload (header only, count 0) parses.
[[nodiscard]] bool parse_view_records(std::span<const std::uint8_t> payload,
                                      std::vector<ViewRecord>& out,
                                      ParseError* error = nullptr);

// Decodes one whole `214/<Class>` collection.  Structural failures (bad magic,
// truncated meta, a block length that runs past the stream) return false and
// fill `error`; inconsistencies that do not prevent reading (a missing column
// block, a header count that disagrees with the block size, a VarMeta entry that
// points inside a block) are reported in `out.warnings` and the affected table
// is left `decoded == false`.
[[nodiscard]] bool parse_view_state(std::string_view klass,
                                    const PartSet& parts,
                                    ViewState& out,
                                    ParseError* error = nullptr);

// Human-readable summary.  max_columns == 0 means every column; max_blocks == 0
// means every block.  Intended for viewstate_report.
[[nodiscard]] std::string describe_view_state(const ViewState& state,
                                              std::size_t max_columns = 0,
                                              std::size_t max_blocks = 0);

// UTF-16LE to printable ASCII, stopping at the first NUL, non-printable unit or
// `max_units`.  View/table names in this fixture family are ASCII.
[[nodiscard]] std::string utf16_ascii(std::span<const std::uint8_t> data,
                                      std::size_t offset,
                                      std::size_t max_units);

// Short printable preview of an opaque block: the first run of at least four
// printable ASCII characters, else a short UTF-16 run, else hex.  Display only;
// never parse the result.
[[nodiscard]] std::string preview_bytes(std::span<const std::uint8_t> data,
                                        std::size_t limit = 48);

}  // namespace msproject::viewstate
