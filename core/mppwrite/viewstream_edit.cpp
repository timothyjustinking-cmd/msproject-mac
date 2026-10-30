/*
 * viewstream_edit.cpp -- one cell-format item appended to a 214/CV_iew block.
 * W34-2.1 (30 October 2026).  The measured rules and the capability matrix are
 * in viewstream_edit.h; the pair derivation is in spec/writers/view-stream-214.md.
 */

#include "viewstream_edit.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

#include "edit_marker.h"
#include "table.h"

namespace mppwrite {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kMetaMagic = 0xFADFADBAu;
constexpr std::size_t kBlockHeaderSize = 16;
constexpr std::size_t kBlockSizeWord = 0;
constexpr std::size_t kBlockSizeWord2 = 4;
constexpr std::size_t kBlockFormatWord = 8;
constexpr std::uint32_t kBlockFormat = 0x00010000u;
constexpr std::size_t kBlockCountWord = 12;
constexpr std::size_t kItemCountWord = 14;
constexpr std::uint32_t kPropsSizeTag = 0x01000100u;
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

void append_u32(Bytes& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
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

/* Same policy as the other writers. */
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
        throw Error("view cell style write: cannot re-open '" + path + "'");
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

void require_stream(CfbDocument const& doc, std::string const& path,
                    std::string const& function) {
    if (!doc.has_stream(path)) {
        throw Error(function + ": the document has no " + path +
                    " stream; this edit needs the measured view stream set");
    }
}

/* Byte offset of the VALUE of one 16-byte scalar property, located by its
 * BIG-endian tag.  Exactly one occurrence is the measured shape (the rule
 * varmeta_edit.cpp applies to the directory Props stream). */
std::size_t props_value_offset(Bytes const& props, std::uint32_t tag,
                               std::string const& function) {
    std::size_t found = kNoIndex;
    for (std::size_t at = 0; at + 16 <= props.size(); ++at) {
        if (get_u32(props, at) != 4 || get_u32(props, at + 8) != 4) {
            continue;
        }
        if (get_big_u32(props, at + 4) != tag) {
            continue;
        }
        if (found != kNoIndex) {
            throw Error(function + ": 214/Props holds more than one scalar "
                        "record for tag " + hex_u32(tag) +
                        "; the CV_iew size word is ambiguous, so it cannot be "
                        "kept");
        }
        found = at + 12;
    }
    if (found == kNoIndex) {
        throw Error(function + ": 214/Props has no scalar record for tag " +
                    hex_u32(tag) +
                    ", so the CV_iew Var2Data size cannot be kept");
    }
    return found;
}

struct ViewState {
    Bytes var_meta;
    Bytes var2;
    Bytes props;
    std::size_t declared = 0;
    std::vector<VarEntry> entries;
    std::size_t props_value_at = 0;
    std::size_t props_value = 0;
    std::size_t target_index = kNoIndex;
    std::size_t block_offset = 0;
    std::size_t payload_length = 0;
    std::size_t item_count = 0;
    std::size_t cell_format_items = 0;
    std::size_t last_item_offset = 0; /* payload-relative, payload start 0 */
    std::size_t last_item_bytes = 0;  /* header + payload */
};

/* Read and strictly validate the three streams.  `after_edit` relaxes exactly
 * one rule: the target block may already carry a 0x2240000c item (the write
 * just appended it) and the count of such items is recorded instead of
 * refused. */
ViewState load_view(CfbDocument const& doc, std::uint32_t view_uid,
                    std::string const& function, bool after_edit = false) {
    std::string const var_meta_path = std::string(kViewTable) + "/VarMeta";
    std::string const var2_path = std::string(kViewTable) + "/Var2Data";
    require_stream(doc, var_meta_path, function);
    require_stream(doc, var2_path, function);
    require_stream(doc, kViewProps, function);

    ViewState state;
    state.var_meta = doc.read_stream(var_meta_path);
    state.var2 = doc.read_stream(var2_path);
    state.props = doc.read_stream(kViewProps);

    if (state.var_meta.size() < kVarMetaHeaderSize ||
        (state.var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
        throw Error(function + ": " + var_meta_path + " is " +
                    std::to_string(state.var_meta.size()) +
                    " bytes, not 24 + 12*n; refusing to edit a damaged stream");
    }
    if (get_u32(state.var_meta, 0) != kMetaMagic) {
        throw Error(function + ": " + var_meta_path +
                    " does not start with the measured magic");
    }
    state.declared = get_u32(state.var_meta, 8);
    state.entries = read_var_entries(state.var_meta);
    if (state.declared != state.entries.size()) {
        throw Error(function + ": " + var_meta_path + " declares " +
                    std::to_string(state.declared) + " entries but the stream "
                    "holds " + std::to_string(state.entries.size()) +
                    "; a stale tail is not a shape this writer edits");
    }
    if (get_u32(state.var_meta, kVarMetaVar2SizeOffset) != state.var2.size()) {
        throw Error(function + ": " + var_meta_path + " size word says " +
                    std::to_string(
                        get_u32(state.var_meta, kVarMetaVar2SizeOffset)) +
                    " bytes but Var2Data is " + std::to_string(state.var2.size()));
    }

    /* Every entry must name a block start, and the blocks must fill Var2Data
     * exactly (the seven measured blocks do).  This is what makes the offset
     * rewrite a single "+delta for every later start". */
    std::vector<std::size_t> starts;
    starts.reserve(state.entries.size());
    for (VarEntry const& entry : state.entries) {
        if (entry.offset == kNoBlockOffset) {
            throw Error(function + ": " + var_meta_path + " entry " +
                        std::to_string(entry.index) +
                        " has the 0xFFFFFFFF no-block offset; no committed pair "
                        "stores one, so the view is refused");
        }
        if (static_cast<std::size_t>(entry.offset) + 4 > state.var2.size()) {
            throw Error(function + ": " + var_meta_path + " entry " +
                        std::to_string(entry.index) + " offset " +
                        std::to_string(entry.offset) +
                        " is past the end of Var2Data");
        }
        std::size_t const length = get_u32(state.var2, entry.offset);
        if (static_cast<std::size_t>(entry.offset) + 4 + length >
            state.var2.size()) {
            throw Error(function + ": " + var_meta_path + " entry " +
                        std::to_string(entry.index) +
                        " block runs past the end of Var2Data");
        }
        starts.push_back(entry.offset);
    }
    std::sort(starts.begin(), starts.end());
    if (starts.empty() || starts.front() != 0) {
        throw Error(function + ": " + var2_path +
                    " does not begin with a block start; refusing to edit a "
                    "damaged view stream");
    }
    for (std::size_t i = 1; i < starts.size(); ++i) {
        std::size_t const end =
            starts[i - 1] + 4 + get_u32(state.var2, starts[i - 1]);
        if (starts[i] != end) {
            throw Error(function + ": " + var2_path + " blocks at " +
                        std::to_string(starts[i - 1]) + " and " +
                        std::to_string(starts[i]) +
                        " are not back to back; the measured view stream is a "
                        "strict block walk and this one is damaged");
        }
    }
    if (starts.back() + 4 + get_u32(state.var2, starts.back()) !=
        state.var2.size()) {
        throw Error(function + ": " + var2_path +
                    " has trailing bytes after its last block; the measured "
                    "view stream has none");
    }

    for (VarEntry const& entry : state.entries) {
        if (entry.mid == view_uid && entry.field_id == kViewStateKey) {
            if (state.target_index != kNoIndex) {
                throw Error(function + ": " + var_meta_path + " holds two "
                            "entries for the view main block of uid " +
                            std::to_string(view_uid) +
                            "; refusing to choose");
            }
            state.target_index = entry.index;
            state.block_offset = entry.offset;
        }
    }
    if (state.target_index == kNoIndex) {
        throw Error(function + ": " + var_meta_path +
                    " has no main block entry for the measured view uid " +
                    std::to_string(view_uid) + " and key " +
                    hex_u32(kViewStateKey) +
                    "; the measured view row is not there");
    }

    state.payload_length = get_u32(state.var2, state.block_offset);
    if (state.payload_length < kBlockHeaderSize) {
        throw Error(function + ": the view main block's payload is " +
                    std::to_string(state.payload_length) +
                    " bytes, shorter than its 16-byte header");
    }
    std::size_t const payload_at = state.block_offset + 4;
    if (get_u32(state.var2, payload_at + kBlockSizeWord) !=
            state.payload_length - 4 ||
        get_u32(state.var2, payload_at + kBlockSizeWord2) !=
            state.payload_length - 4) {
        throw Error(function + ": the view main block's size words are not "
                    "payload - 4; refusing to edit a damaged block");
    }
    if (get_u32(state.var2, payload_at + kBlockFormatWord) != kBlockFormat) {
        throw Error(function + ": the view main block's format word is not " +
                    hex_u32(kBlockFormat) + "; refusing to edit a damaged block");
    }
    state.item_count = get_u16(state.var2, payload_at + kBlockCountWord);

    std::size_t pos = kBlockHeaderSize;
    for (std::size_t i = 0; i < state.item_count; ++i) {
        if (pos + kViewItemHeaderSize > state.payload_length) {
            throw Error(function + ": item " + std::to_string(i) +
                        " starts past the view block's end");
        }
        std::uint32_t const length = get_u32(state.var2, payload_at + pos);
        std::uint32_t const id = get_u32(state.var2, payload_at + pos + 4);
        if ((id & 0x00FF0000u) != 0x00400000u) {
            throw Error(function + ": item " + std::to_string(i) + " has id " +
                        hex_u32(id) +
                        ", which is not a view property id");
        }
        if (length > state.payload_length - pos - kViewItemHeaderSize) {
            throw Error(function + ": item " + std::to_string(i) + " declares " +
                        std::to_string(length) +
                        " payload bytes and runs past the view block");
        }
        state.last_item_offset = pos;
        state.last_item_bytes = kViewItemHeaderSize + length;
        if (id == kViewCellFormatItemId) {
            ++state.cell_format_items;
        }
        std::size_t const end = pos + kViewItemHeaderSize + length;
        if (end == state.payload_length) {
            pos = end;
            continue;
        }
        if (end & 1u) {
            if (state.var2[payload_at + end] != 0) {
                throw Error(function + ": item " + std::to_string(i) +
                            " is followed by a non-zero pad byte; the measured "
                            "pad is zero");
            }
            pos = end + 1;
        } else {
            pos = end;
        }
    }
    if (pos != state.payload_length) {
        throw Error(function + ": the view main block has " +
                    std::to_string(state.payload_length - pos) +
                    " trailing byte(s) after " + std::to_string(state.item_count) +
                    " item(s); the item count and the walk disagree");
    }
    if (!after_edit && state.cell_format_items != 0) {
        throw Error(function + ": the view main block already carries a cell "
                    "format item; replacing it (un-bold, a second style on the "
                    "same cell, a row default) is not measured, only the first "
                    "append is");
    }

    state.props_value_at = props_value_offset(state.props, kPropsSizeTag,
                                              function);
    state.props_value = get_u32(state.props, state.props_value_at);
    if (state.props_value != state.var2.size()) {
        throw Error(function + ": 214/Props says the CV_iew Var2Data is " +
                    std::to_string(state.props_value) + " bytes but it is " +
                    std::to_string(state.var2.size()) +
                    "; refusing to edit a stream whose size words disagree");
    }
    return state;
}

/* The measured 44-byte cell-format entry for the caller's cell, plus its item
 * header.  Every constant here is the one wave30-textstyle stores; the font
 * word is accepted only in the measured form. */
Bytes build_item(ViewCellStyleEdit const& edit) {
    Bytes item;
    item.reserve(kViewItemHeaderSize + kCellFormatEntrySize);
    append_u32(item, static_cast<std::uint32_t>(kCellFormatEntrySize));
    append_u32(item, kViewCellFormatItemId);
    append_u32(item, kViewCellFormatItemKind);
    append_u32(item, edit.uid);
    append_u32(item, edit.field_id);
    append_u32(item, kCellFormatFlags);
    append_u32(item, 0xFF000000u); /* background: automatic */
    append_u32(item, 0);
    append_u32(item, 0);
    append_u32(item, 0xFF000000u); /* second colour word: automatic */
    append_u32(item, 0);
    append_u32(item, 0);
    append_u32(item, 1); /* pattern 1 */
    append_u32(item, kMeasuredFontWord);
    return item;
}

void validate_edit(ViewCellStyleEdit const& edit, std::string const& function) {
    if (edit.view_uid != kMeasuredViewUid) {
        throw Error(function + ": view uid " + std::to_string(edit.view_uid) +
                    " is not the measured applied view (uid " +
                    std::to_string(kMeasuredViewUid) +
                    "); every other view is unmeasured and refused");
    }
    if (edit.uid == 0) {
        throw Error(function + ": uid 0 is the project summary row; formatting "
                    "it is not a measured shape");
    }
    if (edit.field_id == 0) {
        throw Error(function + ": field id 0 is not a stored field");
    }
    if (edit.attribute != ViewCellAttribute::bold) {
        throw Error(function + ": only Bold is measured; italic, underline, "
                    "strikethrough and the two colours are unmeasured and "
                    "refused");
    }
    if (!edit.enabled) {
        throw Error(function + ": turning Bold off would replace or remove the "
                    "cell format item; only the append of a Bold item is "
                    "measured");
    }
    if (edit.selection != ViewCellSelection::cell) {
        throw Error(function + ": whole-row selection stores a row-default "
                    "entry, which is not a measured shape; only one cell is");
    }
}

bool owned_stream(std::string const& path) {
    return path == std::string(kViewTable) + "/VarMeta" ||
           path == std::string(kViewTable) + "/Var2Data" ||
           path == kViewProps || path == kEditMarkerStream;
}

void prove_only_owned_streams_changed(CfbDocument const& opened,
                                      CfbDocument const& reread,
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
        if (owned_stream(path)) {
            continue;
        }
        if (opened.read_stream(path) != reread.read_stream(path)) {
            throw Error(function + ": " + path +
                        " changed; this edit owns only 214/CV_iew/VarMeta, "
                        "214/CV_iew/Var2Data, 214/Props and the Props14 marker");
        }
    }
}

std::size_t block_end(ViewState const& state, std::size_t offset) {
    return offset + 4 + get_u32(state.var2, offset);
}

} /* namespace */

ViewCellStyleWrite save_view_cell_style(
    CfbDocument& doc, std::vector<ViewCellStyleEdit> const& edits,
    std::string const& path) {
    std::string const function = "view cell style write";
    check_output_path(path, function);
    if (edits.size() != 1) {
        throw Error(function + ": " + std::to_string(edits.size()) +
                    " items were requested; exactly one cell-format item per "
                    "save is measured, so this edit is refused");
    }
    ViewCellStyleEdit const& edit = edits.front();
    validate_edit(edit, function);

    CfbDocument const opened = doc;
    ViewState state = load_view(doc, edit.view_uid, function);

    std::size_t const var2_before = state.var2.size();
    std::size_t const payload_at = state.block_offset + 4;
    std::size_t const payload_before = state.payload_length;
    Bytes const item = build_item(edit);
    if (item.size() != kViewItemHeaderSize + kCellFormatEntrySize) {
        throw Error(function + ": the built item is " +
                    std::to_string(item.size()) + " bytes, not the measured 56");
    }
    std::size_t const insert_at = block_end(state, state.block_offset);
    if (state.item_count >= 0xFFFFu) {
        throw Error(function + ": the view block already holds " +
                    std::to_string(state.item_count) +
                    " items, past the 16-bit count word");
    }
    if (state.var2.size() + item.size() > 0xFFFFFFFFu) {
        throw Error(function + ": Var2Data would grow past the 32-bit size word");
    }

    /* The block's outer length word, its two inner size words and the count
     * word, plus the appended item; every other byte is the control's.  The
     * outer word is part of the walk (`block_end` reads it), so it grows with
     * the payload exactly as Project's save grows it. */
    Bytes var2 = state.var2;
    var2.insert(var2.begin() + static_cast<std::ptrdiff_t>(insert_at),
                item.begin(), item.end());
    put_u32(var2, state.block_offset,
            static_cast<std::uint32_t>(payload_before + item.size()));
    put_u32(var2, payload_at + kBlockSizeWord,
            static_cast<std::uint32_t>(payload_before + item.size() - 4));
    put_u32(var2, payload_at + kBlockSizeWord2,
            static_cast<std::uint32_t>(payload_before + item.size() - 4));
    put_u16(var2, payload_at + kBlockCountWord,
            static_cast<std::uint16_t>(state.item_count + 1));

    /* Every later block start moves by the item's size; the target's does not. */
    Bytes var_meta = state.var_meta;
    std::size_t repointed = 0;
    for (VarEntry const& entry : state.entries) {
        if (static_cast<std::size_t>(entry.offset) <= state.block_offset) {
            continue;
        }
        put_u32(var_meta,
                kVarMetaHeaderSize + entry.index * kVarMetaEntrySize + 4,
                static_cast<std::uint32_t>(entry.offset + item.size()));
        ++repointed;
    }
    put_u32(var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(var2.size()));

    Bytes props = state.props;
    put_u32(props, state.props_value_at, static_cast<std::uint32_t>(var2.size()));

    CfbDocument work = doc;
    work.replace_stream(std::string(kViewTable) + "/VarMeta", var_meta);
    work.replace_stream(std::string(kViewTable) + "/Var2Data", var2);
    work.replace_stream(kViewProps, props);
    EditMarker const marker = write_edit_marker(work);
    if (marker.record_present && marker.after != kEditMarkerAfterSave) {
        throw Error(function + ": the staged document's Props14 marker is " +
                    std::to_string(marker.after) + ", not 1");
    }

    /* The in-memory edit must already satisfy the geometry. */
    ViewState const staged = load_view(work, edit.view_uid, function, true);
    if (staged.declared != state.declared ||
        staged.var2.size() != var2.size() ||
        staged.item_count != state.item_count + 1 ||
        staged.cell_format_items != 1) {
        throw Error(function + ": the staged view does not hold the edit's "
                    "counts");
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
    ViewState const after = load_view(reread, edit.view_uid, function, true);

    /* Var2Data: the control's bytes, the block's own header words, and one
     * 56-byte item at the insertion point. */
    Bytes const opened_var2 =
        opened.read_stream(std::string(kViewTable) + "/Var2Data");
    if (after.var2.size() != var2_before + item.size()) {
        throw Error(function + ": the written Var2Data is " +
                    std::to_string(after.var2.size()) + " bytes, expected " +
                    std::to_string(var2_before + item.size()));
    }
    if (!std::equal(opened_var2.begin(),
                    opened_var2.begin() +
                        static_cast<std::ptrdiff_t>(state.block_offset),
                    after.var2.begin())) {
        throw Error(function + ": the written Var2Data changed before the view "
                    "block");
    }
    std::size_t const block_header_end = payload_at + kBlockHeaderSize;
    if (get_u32(after.var2, state.block_offset) !=
            payload_before + item.size() ||
        get_u32(after.var2, payload_at + kBlockSizeWord) !=
            payload_before + item.size() - 4 ||
        get_u32(after.var2, payload_at + kBlockSizeWord2) !=
            payload_before + item.size() - 4 ||
        get_u16(after.var2, payload_at + kBlockCountWord) !=
            static_cast<std::uint16_t>(state.item_count + 1) ||
        get_u16(after.var2, payload_at + kItemCountWord) !=
            get_u16(opened_var2, payload_at + kItemCountWord)) {
        throw Error(function + ": the written view main block's header words are "
                    "not the measured length/count rewrite");
    }
    if (!std::equal(
            opened_var2.begin() + static_cast<std::ptrdiff_t>(block_header_end),
            opened_var2.begin() + static_cast<std::ptrdiff_t>(insert_at),
            after.var2.begin() +
                static_cast<std::ptrdiff_t>(block_header_end))) {
        throw Error(function + ": the written view block changed between its "
                    "header and the appended item");
    }
    if (!std::equal(item.begin(), item.end(),
                    after.var2.begin() +
                        static_cast<std::ptrdiff_t>(insert_at))) {
        throw Error(function + ": the written Var2Data does not hold the "
                    "measured cell-format item");
    }
    if (!std::equal(opened_var2.begin() +
                        static_cast<std::ptrdiff_t>(insert_at),
                    opened_var2.end(),
                    after.var2.begin() +
                        static_cast<std::ptrdiff_t>(insert_at + item.size()))) {
        throw Error(function + ": the written Var2Data tail is not the "
                    "control's bytes shifted by the item's size");
    }

    /* The block header and count. */
    if (after.block_offset != state.block_offset ||
        after.payload_length != payload_before + item.size()) {
        throw Error(function + ": the written view main block moved or did not "
                    "grow by the item's size");
    }
    if (after.item_count != state.item_count + 1 ||
        after.cell_format_items != 1) {
        throw Error(function + ": the written view main block's item count is " +
                    std::to_string(after.item_count) + ", expected " +
                    std::to_string(state.item_count + 1));
    }
    if (after.last_item_bytes != item.size() ||
        !std::equal(item.begin(), item.end(),
                    after.var2.begin() +
                        static_cast<std::ptrdiff_t>(
                            after.block_offset + 4 + after.last_item_offset))) {
        throw Error(function + ": the written block's last item is not the "
                    "measured cell-format item");
    }

    /* The VarMeta offsets: every later start +56, the earlier ones unchanged. */
    if (after.var_meta.size() != state.var_meta.size() ||
        after.declared != state.declared) {
        throw Error(function + ": the written VarMeta changed size or count");
    }
    if (after.entries.size() != state.entries.size()) {
        throw Error(function + ": the written VarMeta holds " +
                    std::to_string(after.entries.size()) + " entries, expected " +
                    std::to_string(state.entries.size()));
    }
    for (std::size_t i = 0; i < state.entries.size(); ++i) {
        VarEntry const& before = state.entries[i];
        VarEntry const& now = after.entries[i];
        std::size_t const expected =
            static_cast<std::size_t>(before.offset) +
            (static_cast<std::size_t>(before.offset) > state.block_offset
                 ? item.size()
                 : 0);
        if (now.mid != before.mid || now.field_id != before.field_id ||
            now.offset != expected) {
            throw Error(function + ": the written VarMeta entry " +
                        std::to_string(i) +
                        " is not the control's entry with the measured offset "
                        "rewrite");
        }
    }
    if (get_u32(after.var_meta, kVarMetaVar2SizeOffset) != after.var2.size()) {
        throw Error(function + ": the written VarMeta size word is not the new "
                    "Var2Data size");
    }

    /* 214/Props: the size value and nothing else. */
    if (after.props.size() != state.props.size()) {
        throw Error(function + ": the written 214/Props changed size");
    }
    for (std::size_t i = 0; i < state.props.size(); ++i) {
        bool const owned = i >= after.props_value_at &&
                           i < after.props_value_at + 4;
        if (!owned && after.props[i] != state.props[i]) {
            throw Error(function + ": the written 214/Props changed at byte " +
                        std::to_string(i) + ", which this edit does not own");
        }
    }
    if (after.props_value != after.var2.size()) {
        throw Error(function + ": the written 214/Props size value is " +
                    std::to_string(after.props_value) + ", expected " +
                    std::to_string(after.var2.size()));
    }
    prove_only_owned_streams_changed(opened, reread, function);

    ViewCellStyleWrite written;
    written.view_uid = edit.view_uid;
    written.uid = edit.uid;
    written.field_id = edit.field_id;
    written.block_offset = state.block_offset;
    written.block_payload_before = payload_before;
    written.block_payload_after = after.payload_length;
    written.item_count_before = state.item_count;
    written.item_count_after = after.item_count;
    written.appended_bytes = item.size();
    written.var2_bytes_before = var2_before;
    written.var2_bytes_after = after.var2.size();
    written.repointed_entries = repointed;
    written.props_value_before = state.props_value;
    written.props_value_after = after.props_value;

    doc = std::move(reread);
    return written;
}

} /* namespace mppwrite */
