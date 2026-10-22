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
//               +4  9 opaque bytes
//               +13 UTF-16 NUL-terminated title (51 code units capacity)
//
//   `214/CV_iew` rows are view definitions: ordinal at +0, UTF-16 name at +4,
//   16-byte identity at row_size-16 = +122 for the measured 138-byte row.  Each
//   view owns VarMeta blocks keyed 0x20400006 (main view state), 0x22400014
//   (notes/RTF) and 0x2240002f (timeline view data).  The block payloads are
//   exposed as opaque blobs with a preview; their internal encoding is NOT
//   decoded (the main block's 16-byte prefix resembles a Props header but its
//   item encoding is different).
//
//   `214/CEdl` has one fixed row and one Var2Data block whose payload is a
//   complete Props-format document (14 records in the prefs base fixture, 15 in
//   the two later saves).  This module parses it with core/props
//   (props::parse_props) rather than re-implementing the framing.
//
//   `214/Props` is a normal Props stream and is decoded by core/props directly;
//   this module does not duplicate that.
//
// THE WRITER (wave 14, W58)
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
// Two targeted mutators are proved by the wave-14 oracle
// (docs/analysis/oracle/w14-oracle.md section 1):
//
//   set_table_date_format  writes CTable block payload +7.  0xff is the
//                          default in every measured file; 0x05 is what
//                          Project wrote for DateFormat=5.
//   set_table_column       replaces the 115-byte record at an existing column
//                          index: u32 field id at record +0 and a zero-filled
//                          UTF-16 title at record +13 (51 code-unit capacity).
//                          The nine opaque bytes at +4 are preserved.  The
//                          oracle's TableEdit with ColumnPosition=0 replaced
//                          the record at index 1 and left the 1047-byte block
//                          unchanged.  The measured replacement also zero-filled
//                          the rest of the title area after its NUL.
//
// Not provided, because not measured:
//
//   * appending a column.  Project rejects ColumnPosition 8 and 9, and the
//     append layout (block +4, block length, VarMeta/Var2 growth) is unproved.
//   * editing CV_iew, CFilter, CGrouping, CEdl, CDrawing, CMap or CReport
//     payload bodies.  Those stay opaque; only the generic framing is written.
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

struct ViewDef {
    std::uint32_t row_index = 0;
    std::uint32_t ordinal = 0;
    std::string name;
    std::array<std::uint8_t, kIdentitySize> identity{};
    std::vector<ViewBlockRef> blocks;
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
