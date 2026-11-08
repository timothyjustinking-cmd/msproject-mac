/*
 * varmeta_edit.cpp -- one Var2Data record of one row: append, re-point, remove.
 * W34-1.1 (30 October 2026).  The measured rules and the capability matrix are
 * in varmeta_edit.h; the pair derivation is in spec/writers/var-meta.md.
 */

#include "varmeta_edit.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include "edit_marker.h"
#include "table.h"

namespace mppwrite {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kMetaMagic = 0xFADFADBAu;
constexpr std::size_t kMetaHeaderSize = 16;
constexpr std::size_t kMetaMagicWord = 0;
constexpr std::size_t kMetaCountWord = 8;
constexpr std::size_t kMetaFixedSizeWord = 12;
constexpr std::size_t kMetaRowOffsetWord = 4;
constexpr std::size_t kMetaStateByte = 0;
constexpr std::size_t kMetaFieldCountWord = 2;
constexpr std::uint8_t kStateLive = 0x00;
constexpr std::uint32_t kSecondaryUidFamily = 0x80000000u;
constexpr std::uint32_t kNoBlockOffset = 0xFFFFFFFFu;
constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

std::uint16_t get_u16(Bytes const& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] |
                                      (bytes[offset + 1] << 8));
}

std::uint32_t get_u32(Bytes const& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

std::uint32_t get_big_u32(Bytes const& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::string hex_u32(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%08x", value);
    return buffer;
}

/* One table's measured geometry.  FixedData is header + stride * rows;
 * FixedMeta is 16 + meta_entry * (rows + meta_descriptors) with the row's
 * entry at index row + descriptors and its row offset at +4; Fixed2Data is
 * fixed2_record * (rows + descriptors); Fixed2Meta is 16 + fixed2_meta_entry *
 * (rows + descriptors).  The two 114/Props words are 16-byte scalar
 * properties addressed by their 4-byte BIG-endian tag. */
struct TableShape {
    char const* table;
    std::size_t header;
    std::size_t stride;
    std::size_t meta_entry;
    std::size_t meta_descriptors;
    std::size_t fixed2_record;
    std::size_t fixed2_meta_entry;
    std::uint32_t size_tag;
    std::uint32_t freed_tag;
    bool append;
    bool remove;
};

TableShape const kTaskShape{kVarTaskTable, 48, 202, 47, 3, 64, 96,
                            0x01000100u, 0x01000002u, true, true};
TableShape const kResourceShape{kVarResourceTable, 48, 172, 37, 3, 40, 51,
                                0x02000100u, 0x02000002u, false, false};

TableShape const& shape_for(std::string const& table,
                            std::string const& function) {
    if (table == kVarTaskTable) {
        return kTaskShape;
    }
    if (table == kVarResourceTable) {
        return kResourceShape;
    }
    throw Error(function + ": '" + table +
                "' is not a table this writer has measured a var-store shape "
                "for; only 114/TBkndTask (append, re-point, remove) and "
                "114/TBkndRsc (re-point) are enabled, and the Assignment table "
                "and the project row are refused");
}

/* '114/TBkndTask' -> '114/Props' (the directory stream, not the per-table
 * registry). */
std::string props_path_for(std::string const& table) {
    std::size_t const slash = table.find('/');
    if (slash == std::string::npos) {
        throw Error("var record write: '" + table + "' has no storage prefix");
    }
    return table.substr(0, slash) + "/Props";
}

/* Same policy as save.cpp/add_delete.cpp/field_values.cpp. */
void check_output_path(std::string const& path, std::string const& function) {
    if (path.empty()) {
        throw Error(function + ": the output path is empty");
    }
    std::filesystem::path const out(path);
    if (out.filename().empty()) {
        throw Error(function + ": " + path +
                    " names a directory, not a file to save to");
    }
    std::error_code error;
    if (std::filesystem::is_directory(out, error)) {
        throw Error(function + ": " + path + " is a directory");
    }
    std::filesystem::path const parent = out.parent_path();
    if (parent.empty()) {
        return;
    }
    error.clear();
    bool const parent_exists = std::filesystem::exists(parent, error);
    if (error || !parent_exists) {
        throw Error(function + ": cannot save to " + path + ": the directory " +
                    parent.string() + " does not exist");
    }
}

Bytes read_file_bytes(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw Error("var record write: cannot re-open '" + path + "'");
    }
    input.seekg(0, std::ios::end);
    std::streamoff const length = input.tellg();
    input.seekg(0, std::ios::beg);
    Bytes bytes(length > 0 ? static_cast<std::size_t>(length) : 0);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

/* Byte offset of the VALUE of one 16-byte scalar 114/Props property, located
 * by its big-endian tag.  Exactly one occurrence is the measured shape (the
 * same rule outline.cpp and actuals.cpp use for their counters). */
std::size_t props_value_offset(Bytes const& props, std::uint32_t tag,
                               std::string const& function,
                               std::string const& table) {
    std::size_t found = kNoIndex;
    for (std::size_t at = 0; at + 16 <= props.size(); ++at) {
        if (get_u32(props, at) != 4 || get_u32(props, at + 8) != 4) {
            continue;
        }
        if (get_big_u32(props, at + 4) != tag) {
            continue;
        }
        if (found != kNoIndex) {
            throw Error(function + ": 114/Props holds more than one scalar "
                        "record for tag " + hex_u32(tag) + " (" + table +
                        "); the counter word is ambiguous, so it cannot be "
                        "kept");
        }
        found = at + 12;
    }
    if (found == kNoIndex) {
        throw Error(function + ": 114/Props has no scalar record for tag " +
                    hex_u32(tag) + " (" + table +
                    "), so the var-store counters cannot be kept");
    }
    return found;
}

/* Project keeps a LOGICAL entry count in header +8 and a possibly longer
 * PHYSICAL array; the writer edits the logical array and copies it back into
 * the first slots of the original stream, keeping the stream size and every
 * byte past the new logical count (the rule measured in actuals.cpp W28-1.1
 * and outline.cpp W29-0.1, duplicated here because those helpers are
 * file-local).  That is what leaves the stale tail slot a removal shows. */
Bytes merge_logical_var_meta(Bytes const& original, Bytes const& logical) {
    if (logical.size() >= original.size()) {
        return logical;
    }
    Bytes out = original;
    std::copy(logical.begin() + static_cast<std::ptrdiff_t>(kVarMetaHeaderSize),
              logical.end(),
              out.begin() + static_cast<std::ptrdiff_t>(kVarMetaHeaderSize));
    std::copy(logical.begin(),
              logical.begin() + static_cast<std::ptrdiff_t>(kVarMetaHeaderSize),
              out.begin());
    return out;
}

void require_stream(CfbDocument const& doc, std::string const& path,
                    std::string const& function) {
    if (!doc.has_stream(path)) {
        throw Error(function + ": the table has no " + path +
                    " stream; this edit needs the whole measured table");
    }
}

struct TableState {
    TableShape const* shape = nullptr;
    std::string fixed_path;
    std::string meta_path;
    std::string fixed2_path;
    std::string fixed2_meta_path;
    std::string var_meta_path;
    std::string var2_path;
    std::string props_path;
    Bytes fixed;
    Bytes meta;
    Bytes fixed2;
    Bytes fixed2_meta;
    Bytes var_meta;
    Bytes var2;
    Bytes props;
    std::size_t rows = 0;
    std::size_t declared = 0;
    std::size_t size_word_at = 0;
    std::size_t freed_word_at = 0;
    std::size_t freed_before = 0;
    std::vector<VarEntry> entries;
};

/* Read and strictly validate one table.  Every identity the edit's arithmetic
 * relies on is checked, so a damaged table is refused before anything is
 * touched.  On the way IN the declared VarMeta count must equal the physical
 * entry count: a file that already carries a stale tail is not a shape this
 * writer edits blind.  `after_edit` relaxes that by exactly one slot, because
 * the remove shape LEAVES one (header -8, logical array one shorter, stream
 * length kept, `merge_logical_var_meta`), and the finished file is re-read and
 * proved with that state. */
TableState load_table(CfbDocument const& doc, std::string const& table,
                      std::string const& function, bool after_edit = false) {
    TableState state;
    state.shape = &shape_for(table, function);
    TableShape const& shape = *state.shape;
    state.fixed_path = table + "/FixedData";
    state.meta_path = table + "/FixedMeta";
    state.fixed2_path = table + "/Fixed2Data";
    state.fixed2_meta_path = table + "/Fixed2Meta";
    state.var_meta_path = table + "/VarMeta";
    state.var2_path = table + "/Var2Data";
    state.props_path = props_path_for(table);
    for (std::string const* path :
         {&state.fixed_path, &state.meta_path, &state.fixed2_path,
          &state.fixed2_meta_path, &state.var_meta_path, &state.var2_path,
          &state.props_path}) {
        require_stream(doc, *path, function);
    }
    state.fixed = doc.read_stream(state.fixed_path);
    state.meta = doc.read_stream(state.meta_path);
    state.fixed2 = doc.read_stream(state.fixed2_path);
    state.fixed2_meta = doc.read_stream(state.fixed2_meta_path);
    state.var_meta = doc.read_stream(state.var_meta_path);
    state.var2 = doc.read_stream(state.var2_path);
    state.props = doc.read_stream(state.props_path);

    if (state.fixed.size() < shape.header ||
        (state.fixed.size() - shape.header) % shape.stride != 0) {
        throw Error(function + ": " + state.fixed_path + " is " +
                    std::to_string(state.fixed.size()) + " bytes, not " +
                    std::to_string(shape.header) + " + " +
                    std::to_string(shape.stride) +
                    "*n; refusing to edit a table whose geometry is broken");
    }
    state.rows = (state.fixed.size() - shape.header) / shape.stride;
    std::size_t const meta_entries = state.rows + shape.meta_descriptors;
    if (state.meta.size() !=
        kMetaHeaderSize + shape.meta_entry * meta_entries) {
        throw Error(function + ": " + state.meta_path + " is " +
                    std::to_string(state.meta.size()) +
                    " bytes, not the measured 16 + " +
                    std::to_string(shape.meta_entry) + "*" +
                    std::to_string(meta_entries) +
                    "; refusing to edit a damaged table");
    }
    if (get_u32(state.meta, kMetaMagicWord) != kMetaMagic) {
        throw Error(function + ": " + state.meta_path +
                    " does not start with the measured table magic");
    }
    if (get_u32(state.meta, kMetaCountWord) != meta_entries ||
        get_u32(state.meta, kMetaFixedSizeWord) != state.fixed.size()) {
        throw Error(function + ": " + state.meta_path +
                    " header count/size disagree with the table; refusing to "
                    "edit a damaged table");
    }
    for (std::size_t row = 0; row < state.rows; ++row) {
        std::size_t const base =
            kMetaHeaderSize + (row + shape.meta_descriptors) * shape.meta_entry;
        if (get_u32(state.meta, base + kMetaRowOffsetWord) !=
            shape.header + shape.stride * row) {
            throw Error(function + ": " + state.meta_path + " entry " +
                        std::to_string(row + shape.meta_descriptors) +
                        " does not name its FixedData row; refusing to edit a "
                        "damaged table");
        }
    }
    std::size_t const fixed2_records = state.rows + shape.meta_descriptors;
    if (state.fixed2.size() != shape.fixed2_record * fixed2_records) {
        throw Error(function + ": " + state.fixed2_path + " is " +
                    std::to_string(state.fixed2.size()) + " bytes, not " +
                    std::to_string(fixed2_records) + "*" +
                    std::to_string(shape.fixed2_record));
    }
    if (state.fixed2_meta.size() !=
        kMetaHeaderSize + shape.fixed2_meta_entry * fixed2_records) {
        throw Error(function + ": " + state.fixed2_meta_path + " is " +
                    std::to_string(state.fixed2_meta.size()) + " bytes, not 16 "
                    "+ " + std::to_string(shape.fixed2_meta_entry) + "*" +
                    std::to_string(fixed2_records));
    }
    if (get_u32(state.fixed2_meta, kMetaMagicWord) != kMetaMagic ||
        get_u32(state.fixed2_meta, kMetaCountWord) != fixed2_records ||
        get_u32(state.fixed2_meta, kMetaFixedSizeWord) != state.fixed2.size()) {
        throw Error(function + ": " + state.fixed2_meta_path +
                    " header count/size disagree with the table");
    }
    for (std::size_t index = 0; index < fixed2_records; ++index) {
        std::size_t const base =
            kMetaHeaderSize + index * shape.fixed2_meta_entry;
        if (get_u32(state.fixed2_meta, base + kMetaRowOffsetWord) !=
            index * shape.fixed2_record) {
            throw Error(function + ": " + state.fixed2_meta_path + " entry " +
                        std::to_string(index) +
                        " does not name its Fixed2Data record");
        }
    }

    if (state.var_meta.size() < kVarMetaHeaderSize ||
        (state.var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
        throw Error(function + ": " + state.var_meta_path + " is " +
                    std::to_string(state.var_meta.size()) +
                    " bytes, not 24 + 12*n; refusing to edit a damaged stream");
    }
    if (get_u32(state.var_meta, 0) != kMetaMagic) {
        throw Error(function + ": " + state.var_meta_path +
                    " does not start with the measured magic");
    }
    state.declared = get_u32(state.var_meta, kMetaCountWord);
    state.entries = read_var_entries(state.var_meta);
    bool const stale_tail_ok =
        after_edit && state.entries.size() == state.declared + 1;
    if (state.declared != state.entries.size() && !stale_tail_ok) {
        throw Error(function + ": " + state.var_meta_path + " declares " +
                    std::to_string(state.declared) + " entries but the stream "
                    "holds " + std::to_string(state.entries.size()) +
                    "; a stale tail is not a shape this writer edits (no "
                    "committed pair measures one before an edit)");
    }
    if (get_u32(state.var_meta, kVarMetaVar2SizeOffset) != state.var2.size()) {
        throw Error(function + ": " + state.var_meta_path + " size word says " +
                    std::to_string(
                        get_u32(state.var_meta, kVarMetaVar2SizeOffset)) +
                    " bytes but Var2Data is " + std::to_string(state.var2.size()));
    }
    for (VarEntry const& entry : state.entries) {
        if (entry.offset == kNoBlockOffset) {
            throw Error(function + ": " + state.var_meta_path + " entry " +
                        std::to_string(entry.index) +
                        " has the 0xFFFFFFFF no-block offset; no committed pair "
                        "stores one, so the table is refused");
        }
        if (static_cast<std::size_t>(entry.offset) > state.var2.size() ||
            4 > state.var2.size() - static_cast<std::size_t>(entry.offset)) {
            throw Error(function + ": " + state.var_meta_path + " entry " +
                        std::to_string(entry.index) + " offset " +
                        std::to_string(entry.offset) +
                        " is past the end of Var2Data");
        }
        std::uint32_t const length = get_u32(state.var2, entry.offset);
        if (length > state.var2.size() -
                         static_cast<std::size_t>(entry.offset) - 4) {
            throw Error(function + ": " + state.var_meta_path + " entry " +
                        std::to_string(entry.index) +
                        " block runs past the end of Var2Data");
        }
    }

    state.size_word_at = props_value_offset(state.props, shape.size_tag,
                                            function, table);
    state.freed_word_at = props_value_offset(state.props, shape.freed_tag,
                                             function, table);
    state.freed_before = get_u32(state.props, state.freed_word_at);
    return state;
}

struct RowRef {
    std::size_t row = 0;
    std::size_t meta_index = 0;
    std::size_t meta_base = 0;
    std::size_t entries_before = 0; /* the row's FixedMeta +2 word */
};

std::size_t row_entries(std::vector<VarEntry> const& entries,
                        std::uint32_t uid) {
    std::size_t count = 0;
    for (VarEntry const& entry : entries) {
        if (entry.mid == uid || entry.mid == (uid | kSecondaryUidFamily)) {
            ++count;
        }
    }
    return count;
}

RowRef find_row(TableState const& state, std::uint32_t uid,
                std::string const& function) {
    TableShape const& shape = *state.shape;
    std::size_t found_row = kNoIndex;
    for (std::size_t row = 0; row < state.rows; ++row) {
        if (get_u32(state.fixed, shape.header + shape.stride * row + 4) == uid) {
            if (found_row != kNoIndex) {
                throw Error(function + ": uid " + std::to_string(uid) +
                            " names more than one row of " + state.fixed_path +
                            "; refusing to choose");
            }
            found_row = row;
        }
    }
    if (found_row == kNoIndex) {
        throw Error(function + ": no row of " + state.fixed_path +
                    " has UniqueID " + std::to_string(uid) +
                    "; refusing to edit a row that is not there");
    }
    std::size_t const want = shape.header + shape.stride * found_row;
    RowRef result;
    result.row = found_row;
    result.meta_index = kNoIndex;
    for (std::size_t index = 0; index < state.rows + shape.meta_descriptors;
         ++index) {
        std::size_t const base = kMetaHeaderSize + index * shape.meta_entry;
        if (get_u32(state.meta, base + kMetaRowOffsetWord) == want) {
            if (result.meta_index != kNoIndex) {
                throw Error(function + ": " + state.meta_path +
                            " names the row twice; refusing to choose");
            }
            result.meta_index = index;
            result.meta_base = base;
        }
    }
    if (result.meta_index == kNoIndex) {
        throw Error(function + ": the row for uid " + std::to_string(uid) +
                    " has no " + state.meta_path +
                    " entry; refusing to edit a tombstone");
    }
    if (state.meta[result.meta_base + kMetaStateByte] != kStateLive) {
        throw Error(function + ": the FixedMeta entry for uid " +
                    std::to_string(uid) + " is not live (state 0x" +
                    std::to_string(
                        state.meta[result.meta_base + kMetaStateByte]) +
                    "); refusing to edit a tombstoned row");
    }
    result.entries_before =
        get_u16(state.meta, result.meta_base + kMetaFieldCountWord);
    return result;
}

/* The measured presence-bit operations.  The bit belongs to the field's class
 * slot and is the caller's measurement; the writer only checks it is inside
 * the row's entry and is a single bit. */
void validate_presence(std::vector<VarPresenceBit> const& presence,
                       std::size_t meta_entry, std::string const& function) {
    for (std::size_t i = 0; i < presence.size(); ++i) {
        VarPresenceBit const& op = presence[i];
        if (op.byte < 8 || op.byte >= meta_entry) {
            throw Error(function + ": presence byte +" +
                        std::to_string(op.byte) + " is outside the measured "
                        "row-entry tail (bytes 8.." +
                        std::to_string(meta_entry - 1) + ")");
        }
        if (op.bit == 0 || (op.bit & static_cast<std::uint8_t>(op.bit - 1)) != 0) {
            throw Error(function + ": presence mask 0x" +
                        std::to_string(op.bit) +
                        " is not a single bit; only one field bit per word is "
                        "measured");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (presence[j].byte == op.byte && presence[j].bit == op.bit) {
                throw Error(function + ": presence byte +" +
                            std::to_string(op.byte) + " bit 0x" +
                            std::to_string(op.bit) +
                            " is requested twice; the edit is ambiguous");
            }
        }
    }
}

void apply_presence(Bytes& meta, std::size_t meta_base,
                    std::vector<VarPresenceBit> const& presence) {
    for (VarPresenceBit const& op : presence) {
        std::size_t const at = meta_base + op.byte;
        if (op.set) {
            meta[at] = static_cast<std::uint8_t>(meta[at] | op.bit);
        } else {
            meta[at] = static_cast<std::uint8_t>(meta[at] &
                                                static_cast<std::uint8_t>(~op.bit));
        }
    }
}

/* What the edit did, in the terms the proof re-reads. */
struct Applied {
    VarRecordShape shape = VarRecordShape::append;
    std::uint32_t uid = 0;
    std::uint32_t field_id = 0;
    std::size_t old_offset = 0;
    std::size_t new_offset = 0;
    std::size_t old_payload = 0;
    std::size_t new_payload = 0;
    std::size_t freed_added = 0;
    std::size_t entries_after = 0;
    std::size_t row_entries_after = 0;
    std::size_t insertion_index = kNoIndex; /* append */
    std::size_t removed_index = kNoIndex;   /* remove */
    std::size_t edited_index = kNoIndex;    /* re-point */
};

std::vector<std::size_t> matching_entries(std::vector<VarEntry> const& entries,
                                          std::uint32_t uid,
                                          std::uint32_t field_id) {
    std::vector<std::size_t> out;
    for (VarEntry const& entry : entries) {
        if (entry.field_id != field_id) {
            continue;
        }
        if (entry.mid == uid || entry.mid == (uid | kSecondaryUidFamily)) {
            out.push_back(entry.index);
        }
    }
    return out;
}

/* A 0x80000000|uid entry for the key is NOT the plain row's record: the family
 * is a separate owner of the key (var-meta.md, "the two uid families"), and no
 * committed pair edits one.  `matching_entries` sees it so that an append
 * collision is caught; re-point and remove must refuse it explicitly, because
 * a lone family entry has matching.size() == 1 and would otherwise be adopted
 * as the row's record. */
bool has_family_entry(std::vector<VarEntry> const& entries, std::uint32_t uid,
                      std::uint32_t field_id) {
    for (VarEntry const& entry : entries) {
        if (entry.field_id == field_id &&
            entry.mid == (uid | kSecondaryUidFamily)) {
            return true;
        }
    }
    return false;
}

Applied apply_edit(TableState& state, RowRef const& row,
                   VarRecordEdit const& edit, std::string const& function) {
    TableShape const& shape = *state.shape;
    Applied applied;
    applied.shape = edit.shape;
    applied.uid = edit.uid;
    applied.field_id = edit.field_id;

    std::vector<std::size_t> const matching =
        matching_entries(state.entries, edit.uid, edit.field_id);

    if (edit.shape == VarRecordShape::append) {
        if (!shape.append) {
            throw Error(function + ": appending one record to the " +
                        std::string(shape.table) +
                        " table is not measured (no committed pair adds a "
                        "record to an existing resource row); refused");
        }
        if (edit.payload.empty()) {
            throw Error(function + ": an append needs a non-empty payload; no "
                        "pair stores a zero-length block");
        }
        if (!matching.empty()) {
            throw Error(function + ": the row for uid " +
                        std::to_string(edit.uid) + " already has a record for " +
                        hex_u32(edit.field_id) +
                        " (or its 0x80000000 family); an append that collides "
                        "is not a measured shape, use the re-point shape");
        }
        if (edit.payload.size() > 0xFFFFFFFFu) {
            throw Error(function + ": the payload is larger than the 32-bit "
                        "length word");
        }
        if (state.var2.size() + 4 + edit.payload.size() > 0xFFFFFFFFu) {
            throw Error(function + ": Var2Data would grow past the 32-bit "
                        "size word");
        }
        applied.new_offset = state.var2.size();
        applied.new_payload = edit.payload.size();
        Bytes const block = encode_block(edit.payload);
        state.var2.insert(state.var2.end(), block.begin(), block.end());
        applied.insertion_index =
            (var_entry_insert_offset(state.var_meta, edit.uid, edit.field_id) -
             kVarMetaHeaderSize) /
            kVarMetaEntrySize;
        insert_var_entry(state.var_meta, edit.uid, edit.field_id,
                         static_cast<std::uint32_t>(applied.new_offset));
        applied.entries_after = state.declared + 1;
    } else if (edit.shape == VarRecordShape::repoint) {
        if (has_family_entry(state.entries, edit.uid, edit.field_id)) {
            throw Error(function + ": the key " + hex_u32(edit.field_id) +
                        " is owned by the 0x80000000|uid family, not by uid " +
                        std::to_string(edit.uid) +
                        "; a family entry is not the row's own record and no "
                        "pair measures editing one; refused");
        }
        if (matching.size() != 1) {
            throw Error(function + ": a re-point needs exactly one existing "
                        "record for " + hex_u32(edit.field_id) + " of uid " +
                        std::to_string(edit.uid) + ", found " +
                        std::to_string(matching.size()) +
                        "; no measured file has a duplicate or a family entry "
                        "for one key");
        }
        if (edit.payload.empty()) {
            throw Error(function + ": a re-point needs a non-empty payload; no "
                        "pair stores a zero-length block");
        }
        if (edit.payload.size() > 0xFFFFFFFFu) {
            throw Error(function + ": the payload is larger than the 32-bit "
                        "length word");
        }
        VarEntry const& entry = state.entries[matching.front()];
        applied.edited_index = matching.front();
        applied.old_offset = entry.offset;
        applied.old_payload = get_u32(state.var2, entry.offset);
        if (applied.old_payload == 0) {
            throw Error(function + ": the existing block for " +
                        hex_u32(edit.field_id) + " of uid " +
                        std::to_string(edit.uid) +
                        " has zero payload; no pair stores one, so it is "
                        "refused as a source block");
        }
        if (edit.payload.size() <= applied.old_payload) {
            throw Error(function + ": the new payload is " +
                        std::to_string(edit.payload.size()) + " bytes, not "
                        "larger than the stored " +
                        std::to_string(applied.old_payload) +
                        "; the smaller (in-place) re-point and the equal-size "
                        "re-point are measured only inside the wave-28 outline "
                        "edits and need their own isolated pair before this "
                        "writer can enable them");
        }
        if (state.var2.size() + 4 + edit.payload.size() > 0xFFFFFFFFu) {
            throw Error(function + ": Var2Data would grow past the 32-bit "
                        "size word");
        }
        applied.new_offset = state.var2.size();
        applied.new_payload = edit.payload.size();
        Bytes const block = encode_block(edit.payload);
        state.var2.insert(state.var2.end(), block.begin(), block.end());
        put_u32(state.var_meta,
                kVarMetaHeaderSize +
                    applied.edited_index * kVarMetaEntrySize + 4,
                static_cast<std::uint32_t>(applied.new_offset));
        applied.entries_after = state.declared;
    } else {
        if (!shape.remove) {
            throw Error(function + ": removing one record from the " +
                        std::string(shape.table) +
                        " table is not measured (no committed pair removes a "
                        "resource value); refused");
        }
        if (!edit.payload.empty()) {
            throw Error(function + ": a remove takes no payload; the block "
                        "stays orphaned in Var2Data exactly as it is");
        }
        if (has_family_entry(state.entries, edit.uid, edit.field_id)) {
            throw Error(function + ": the key " + hex_u32(edit.field_id) +
                        " is owned by the 0x80000000|uid family, not by uid " +
                        std::to_string(edit.uid) +
                        "; a family entry is not the row's own record and no "
                        "pair measures removing one; refused");
        }
        if (matching.size() != 1) {
            throw Error(function + ": a remove needs exactly one existing "
                        "record for " + hex_u32(edit.field_id) + " of uid " +
                        std::to_string(edit.uid) + ", found " +
                        std::to_string(matching.size()) +
                        "; no measured file has a duplicate or a family entry "
                        "for one key");
        }
        VarEntry const& entry = state.entries[matching.front()];
        applied.removed_index = matching.front();
        applied.old_offset = entry.offset;
        applied.old_payload = get_u32(state.var2, entry.offset);
        if (applied.old_payload == 0) {
            throw Error(function + ": the removed block for " +
                        hex_u32(edit.field_id) + " of uid " +
                        std::to_string(edit.uid) +
                        " has zero payload; no pair stores one");
        }
        std::size_t const at =
            kVarMetaHeaderSize + applied.removed_index * kVarMetaEntrySize;
        state.var_meta.erase(
            state.var_meta.begin() + static_cast<std::ptrdiff_t>(at),
            state.var_meta.begin() +
                static_cast<std::ptrdiff_t>(at + kVarMetaEntrySize));
        applied.freed_added = applied.old_payload;
        applied.entries_after = state.declared - 1;
    }

    /* The header words and the row's +2 word are recomputed from the array,
     * never assumed. */
    std::size_t const new_row_count =
        row_entries(read_var_entries(state.var_meta), edit.uid);
    applied.row_entries_after = new_row_count;
    put_u32(state.var_meta, kMetaCountWord,
            static_cast<std::uint32_t>(applied.entries_after));
    put_u32(state.var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(state.var2.size()));
    if (new_row_count > 0xFFFFu) {
        throw Error(function + ": the row would carry " +
                    std::to_string(new_row_count) +
                    " var entries, past the 16-bit FixedMeta count word");
    }
    put_u16(state.meta, row.meta_base + kMetaFieldCountWord,
            static_cast<std::uint16_t>(new_row_count));
    apply_presence(state.meta, row.meta_base, edit.presence);

    /* 114/Props: the size word mirrors Var2Data, the freed word gains the
     * unlinked payload bytes. */
    if (state.var2.size() > 0xFFFFFFFFu) {
        throw Error(function + ": Var2Data grew past the 32-bit size word");
    }
    put_u32(state.props, state.size_word_at,
            static_cast<std::uint32_t>(state.var2.size()));
    if (applied.freed_added >
        0xFFFFFFFFu - static_cast<std::size_t>(state.freed_before)) {
        throw Error(function + ": the freed-byte counter would overflow its "
                    "32-bit word");
    }
    put_u32(state.props, state.freed_word_at,
            static_cast<std::uint32_t>(state.freed_before +
                                       applied.freed_added));
    return applied;
}

/* The logical entries the edited stream must hold, from the opened entries. */
std::vector<VarEntry> expected_entries(std::vector<VarEntry> const& before,
                                       Applied const& applied) {
    std::vector<VarEntry> out = before;
    if (applied.shape == VarRecordShape::append) {
        VarEntry entry;
        entry.field_id = applied.field_id;
        entry.mid = applied.uid;
        entry.offset = static_cast<std::uint32_t>(applied.new_offset);
        out.insert(out.begin() +
                       static_cast<std::ptrdiff_t>(applied.insertion_index),
                   entry);
    } else if (applied.shape == VarRecordShape::repoint) {
        out[applied.edited_index].offset =
            static_cast<std::uint32_t>(applied.new_offset);
    } else {
        out.erase(out.begin() +
                  static_cast<std::ptrdiff_t>(applied.removed_index));
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i].index = i;
    }
    return out;
}

void prove_entries(std::vector<VarEntry> const& before, Bytes const& after,
                   Applied const& applied, std::string const& function) {
    std::uint32_t const declared = get_u32(after, kMetaCountWord);
    if (declared != applied.entries_after) {
        throw Error(function + ": the written VarMeta declares " +
                    std::to_string(declared) + " entries, expected " +
                    std::to_string(applied.entries_after));
    }
    std::vector<VarEntry> const physical = read_var_entries(after);
    if (declared > physical.size()) {
        throw Error(function + ": the written VarMeta declares " +
                    std::to_string(declared) + " entries but holds " +
                    std::to_string(physical.size()));
    }
    std::vector<VarEntry> logical(physical.begin(),
                                  physical.begin() +
                                      static_cast<std::ptrdiff_t>(declared));
    std::vector<VarEntry> const expected = expected_entries(before, applied);
    if (logical.size() != expected.size()) {
        throw Error(function + ": the written VarMeta holds " +
                    std::to_string(logical.size()) + " logical entries, "
                    "expected " + std::to_string(expected.size()));
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (logical[i].mid != expected[i].mid ||
            logical[i].field_id != expected[i].field_id ||
            logical[i].offset != expected[i].offset) {
            throw Error(function + ": the written VarMeta entry " +
                        std::to_string(i) + " is not the entry the edit "
                        "measured");
        }
    }
}

/* Bytes of FixedMeta outside {row +2 word, presence bytes} must not move. */
void prove_fixed_meta(Bytes const& before, Bytes const& after,
                      RowRef const& row, std::vector<VarPresenceBit> const& presence,
                      std::size_t expected_count, std::string const& function) {
    if (after.size() != before.size()) {
        throw Error(function + ": the written FixedMeta changed size");
    }
    std::vector<bool> allowed(before.size(), false);
    for (std::size_t byte = 0; byte < 2; ++byte) {
        allowed[row.meta_base + kMetaFieldCountWord + byte] = true;
    }
    for (VarPresenceBit const& op : presence) {
        allowed[row.meta_base + op.byte] = true;
    }
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (after[i] != before[i] && !allowed[i]) {
            throw Error(function + ": the written FixedMeta changed at byte " +
                        std::to_string(i) + ", which this edit does not own");
        }
    }
    std::size_t const count =
        get_u16(after, row.meta_base + kMetaFieldCountWord);
    if (count != expected_count) {
        throw Error(function + ": the written FixedMeta row count is " +
                    std::to_string(count) + ", expected " +
                    std::to_string(expected_count));
    }
    for (VarPresenceBit const& op : presence) {
        std::uint8_t const byte = after[row.meta_base + op.byte];
        bool const is_set = (byte & op.bit) != 0;
        if (is_set != op.set) {
            throw Error(function + ": the written presence bit +" +
                        std::to_string(op.byte) + "/0x" +
                        std::to_string(op.bit) + " is not " +
                        (op.set ? "set" : "clear"));
        }
    }
}

void prove_props(Bytes const& before, Bytes const& after,
                 std::size_t size_word_at, std::size_t freed_word_at,
                 std::size_t expected_size, std::size_t expected_freed,
                 std::string const& function) {
    if (after.size() != before.size()) {
        throw Error(function + ": the written 114/Props changed size");
    }
    for (std::size_t i = 0; i < before.size(); ++i) {
        bool const owned = (i >= size_word_at && i < size_word_at + 4) ||
                           (i >= freed_word_at && i < freed_word_at + 4);
        if (after[i] != before[i] && !owned) {
            throw Error(function + ": the written 114/Props changed at byte " +
                        std::to_string(i) + ", which this edit does not own");
        }
    }
    if (get_u32(after, size_word_at) != expected_size) {
        throw Error(function + ": the written 114/Props size word is " +
                    std::to_string(get_u32(after, size_word_at)) +
                    ", expected " + std::to_string(expected_size));
    }
    if (get_u32(after, freed_word_at) != expected_freed) {
        throw Error(function + ": the written 114/Props freed word is " +
                    std::to_string(get_u32(after, freed_word_at)) +
                    ", expected " + std::to_string(expected_freed));
    }
}

bool owned_stream(std::string const& path, TableState const& state) {
    return path == state.var_meta_path || path == state.var2_path ||
           path == state.meta_path || path == state.props_path ||
           path == kEditMarkerStream;
}

void prove_only_owned_streams_changed(CfbDocument const& opened,
                                      CfbDocument const& reread,
                                      TableState const& state,
                                      std::string const& function) {
    for (std::string const& path : opened.stream_paths()) {
        if (!reread.has_stream(path)) {
            throw Error(function + ": the written file lost the " + path +
                        " stream");
        }
    }
    for (std::string const& path : reread.stream_paths()) {
        if (!opened.has_stream(path)) {
            throw Error(function + ": the written file gained the " + path +
                        " stream");
        }
        if (owned_stream(path, state)) {
            continue;
        }
        if (opened.read_stream(path) != reread.read_stream(path)) {
            throw Error(function + ": " + path +
                        " changed; this edit owns only the table's VarMeta, "
                        "Var2Data and FixedMeta, 114/Props and the Props14 "
                        "marker");
        }
    }
}

} /* namespace */

VarRecordWrite save_var_record(CfbDocument& doc, std::string_view table,
                               std::vector<VarRecordEdit> const& edits,
                               std::string const& path) {
    std::string const function = "var record write";
    std::string const table_path = normalise_path(table);
    TableShape const& shape = shape_for(table_path, function);
    check_output_path(path, function);
    if (edits.size() != 1) {
        throw Error(function + ": " + std::to_string(edits.size()) +
                    " records were requested; exactly one record per save is "
                    "measured, so a multi-row edit is refused");
    }
    VarRecordEdit const& edit = edits.front();
    /* W34-4.6: wave30-wbs-code appends ONE record to the Task table's project
     * row (uid 0, key 0x0B40019F, the last-issued WBS number); that append is
     * the only measured edit of the row's var records. */
    bool const wbs_project_row = edit.uid == 0 && table_path == kVarTaskTable &&
                                 edit.shape == VarRecordShape::append &&
                                 edit.field_id == kTaskProjectWbsFieldId;
    if (edit.uid == 0 && !wbs_project_row) {
        throw Error(function + ": uid 0 is the project row (the Resource "
                    "placeholder row in TBkndRsc); editing its var records is "
                    "not measured");
    }
    if (edit.field_id == 0) {
        throw Error(function + ": field key 0 is not a stored field");
    }
    validate_presence(edit.presence, shape.meta_entry, function);
    /* A table's unavailable shapes are refused before any stream is read or an
     * output path is touched, so a Resource append/remove is refused on any
     * document, not only one whose Resource table happens to hold the uid. */
    if (edit.shape == VarRecordShape::append && !shape.append) {
        throw Error(function + ": appending one record to the " +
                    std::string(shape.table) +
                    " table is not measured (no committed pair adds a record "
                    "to an existing resource row); refused");
    }
    if (edit.shape == VarRecordShape::remove && !shape.remove) {
        throw Error(function + ": removing one record from the " +
                    std::string(shape.table) +
                    " table is not measured (no committed pair removes a "
                    "resource value); refused");
    }

    CfbDocument const opened = doc;
    TableState state = load_table(doc, table_path, function);
    RowRef const row = find_row(state, edit.uid, function);
    std::vector<VarEntry> const entries_before = state.entries;
    std::size_t const var2_before = state.var2.size();
    std::size_t const freed_before = state.freed_before;
    Applied const applied = apply_edit(state, row, edit, function);

    Bytes const final_var_meta =
        merge_logical_var_meta(opened.read_stream(state.var_meta_path),
                               state.var_meta);

    CfbDocument work = doc;
    work.replace_stream(state.var_meta_path, final_var_meta);
    work.replace_stream(state.var2_path, state.var2);
    work.replace_stream(state.meta_path, state.meta);
    work.replace_stream(state.props_path, state.props);
    write_edit_marker(work);

    /* The in-memory edit must already satisfy the table's identities. */
    TableState const staged = load_table(work, table_path, function, true);
    if (staged.declared != applied.entries_after ||
        staged.var2.size() != state.var2.size()) {
        throw Error(function + ": the staged table does not hold the edit's "
                    "declared counts");
    }

    Bytes const intended = work.to_bytes();
    work.save(path);
    Bytes const on_disk = read_file_bytes(path);
    if (on_disk != intended) {
        throw Error(function + ": " + path + " holds " +
                    std::to_string(on_disk.size()) + " bytes, but the document "
                    "built " + std::to_string(intended.size()) +
                    "; the write did not land");
    }
    CfbDocument reread = CfbDocument::from_file(path);
    if (reread.to_bytes() != on_disk) {
        throw Error(function + ": the written file does not re-emit "
                    "byte-identically");
    }
    if (!reread.check().empty()) {
        throw Error(function + ": the written container is inconsistent: " +
                    reread.check());
    }
    TableState const after = load_table(reread, table_path, function, true);

    /* The reread table must be exactly the edit and nothing else. */
    prove_entries(entries_before, after.var_meta, applied, function);
    if (applied.shape == VarRecordShape::append ||
        applied.shape == VarRecordShape::repoint) {
        if (after.var2.size() != var2_before + 4 + applied.new_payload) {
            throw Error(function + ": the written Var2Data is " +
                        std::to_string(after.var2.size()) + " bytes, expected " +
                        std::to_string(var2_before + 4 + applied.new_payload));
        }
        std::vector<std::uint8_t> const block =
            read_var_block(after.var2,
                           static_cast<std::uint32_t>(applied.new_offset));
        if (block != edit.payload) {
            throw Error(function + ": the written block does not hold the "
                        "requested payload");
        }
    } else {
        if (after.var2 != opened.read_stream(state.var2_path)) {
            throw Error(function + ": a remove must leave Var2Data "
                        "byte-identical");
        }
        if (after.var_meta.size() != opened.read_stream(state.var_meta_path).size()) {
            throw Error(function + ": a remove must not truncate VarMeta");
        }
        /* The stale tail is the previous last entry, byte for byte. */
        Bytes const original_var_meta = opened.read_stream(state.var_meta_path);
        std::size_t const logical_end =
            kVarMetaHeaderSize + applied.entries_after * kVarMetaEntrySize;
        if (!std::equal(original_var_meta.begin() +
                            static_cast<std::ptrdiff_t>(logical_end),
                        original_var_meta.end(),
                        after.var_meta.begin() +
                            static_cast<std::ptrdiff_t>(logical_end))) {
            throw Error(function + ": the stale tail a remove keeps is not "
                        "byte-identical to the opened stream's");
        }
    }
    prove_fixed_meta(opened.read_stream(state.meta_path), after.meta, row,
                     edit.presence, applied.row_entries_after, function);
    prove_props(opened.read_stream(state.props_path), after.props,
                after.size_word_at, after.freed_word_at, after.var2.size(),
                freed_before + applied.freed_added, function);
    prove_only_owned_streams_changed(opened, reread, after, function);

    VarRecordWrite written;
    written.uid = edit.uid;
    written.field_id = edit.field_id;
    written.shape = edit.shape;
    written.old_offset = static_cast<std::uint32_t>(applied.old_offset);
    written.new_offset = static_cast<std::uint32_t>(applied.new_offset);
    written.old_payload_bytes = applied.old_payload;
    written.new_payload_bytes =
        applied.shape == VarRecordShape::remove ? 0 : applied.new_payload;
    written.var2_bytes_before = var2_before;
    written.var2_bytes_after = after.var2.size();
    written.entries_before = entries_before.size();
    written.entries_after = after.declared;
    written.row_entries_before = row.entries_before;
    written.row_entries_after = applied.row_entries_after;
    written.freed_bytes_before = freed_before;
    written.freed_bytes_after = get_u32(after.props, after.freed_word_at);
    if (written.entries_after != applied.entries_after ||
        written.row_entries_after !=
            get_u16(after.meta, row.meta_base + kMetaFieldCountWord)) {
        throw Error(function + ": the written row/table counts disagree with "
                    "the edit");
    }

    doc = std::move(reread);
    return written;
}

VarRecordWrite rename_resource(CfbDocument& doc, std::uint32_t uid,
                               std::string_view name, std::string const& path) {
    std::string const function = "rename resource";
    if (name.empty()) {
        throw Error(function + ": the name is empty; an empty string is a "
                    "shrink of the stored block, which this writer refuses "
                    "until an isolated pair measures it");
    }
    if (name.find('\0') != std::string_view::npos) {
        throw Error(function + ": the name holds an embedded NUL");
    }
    if (name.size() > 65535) {
        throw Error(function + ": the name is " + std::to_string(name.size()) +
                    " bytes, past the measured 65,535-byte cap");
    }
    VarRecordEdit edit;
    edit.uid = uid;
    edit.field_id = kResourceNameFieldId;
    edit.shape = VarRecordShape::repoint;
    edit.payload = encode_utf16z(name);
    std::vector<VarRecordEdit> const edits{edit};
    VarRecordWrite const written =
        save_var_record(doc, kVarResourceTable, edits, path);

    /* Read the name back through the measured encoding.  table.cpp's
     * read_task_name is hard-wired to the TASK name key, so the Resource key is
     * looked up here. */
    std::string const var_meta_path = std::string(kVarResourceTable) + "/VarMeta";
    std::string const var2_path = std::string(kVarResourceTable) + "/Var2Data";
    std::vector<std::uint32_t> const offsets = find_var_offsets(
        doc.read_stream(var_meta_path), kResourceNameFieldId, uid);
    if (offsets.size() != 1) {
        throw Error(function + ": the written file holds " +
                    std::to_string(offsets.size()) +
                    " entries for the resource name key, expected 1");
    }
    std::string const stored =
        decode_utf16z(read_var_block(doc.read_stream(var2_path),
                                     offsets.front()));
    if (stored != name) {
        throw Error(function + ": the written file decodes the name as " +
                    std::to_string(stored.size()) + " bytes, not the " +
                    std::to_string(name.size()) + " requested");
    }
    return written;
}

} /* namespace mppwrite */
