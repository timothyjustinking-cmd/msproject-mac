/*
 * viewstate_sort.cpp -- the Task Sheet sort persisted in the file.
 * W35-2.1.  The measured rules and the capability matrix are in
 * viewstate_sort.h; the pair derivation is in spec/writers/view-state.md.
 */

#include "viewstate_sort.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <utility>

#include "edit_marker.h"
#include "table.h"
#include "viewstream_edit.h"

namespace mppwrite {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kMeasuredBlockPayload = 42692;
constexpr std::uint64_t kMeasuredBlockDigest = 0xb2a54e044baa30a2ull;
constexpr std::size_t kSlotBytes = 6;

constexpr std::size_t kEdlHeaderSize = 20;
constexpr std::size_t kEdlItemHeaderSize = 12;
constexpr std::size_t kEdlCountWord = 16;
constexpr std::size_t kEdlOrderPayload = 32; /* N + 3 dwords at N = 5 */
constexpr std::uint32_t kEdlOrderTail0 = 0x000FFDC0u;
constexpr std::size_t kEdlCopyPayload = 64;

std::uint32_t get_u32(Bytes const& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) |
           (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) |
           (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

void put_u32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[at + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

void put_u16(Bytes& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v & 0xFFu);
    b[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

std::string number(std::size_t value) { return std::to_string(value); }

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
        throw Error("view sort write: cannot re-open '" + path + "'");
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

std::uint32_t field_id_of(SortField field, std::string const& function) {
    switch (field) {
    case SortField::name:
        return kSortFieldName;
    case SortField::duration:
        return kSortFieldDuration;
    case SortField::id:
        throw Error(function + ": sorting by ID is the file's default order and "
                    "no explicit ID key is measured, so this sort is refused");
    case SortField::start:
        throw Error(function + ": a Start sort key is unmeasured (only Name and "
                    "Duration are), so this sort is refused");
    case SortField::finish:
        throw Error(function + ": a Finish sort key is unmeasured (only Name "
                    "and Duration are), so this sort is refused");
    case SortField::other:
        break;
    }
    throw Error(function + ": this sort key is unmeasured (only Name and "
                "Duration are), so this sort is refused");
}

Bytes slot_bytes(std::uint32_t field_id, bool ascending) {
    Bytes slot(kSlotBytes, 0);
    put_u32(slot, 0, field_id);
    put_u16(slot, 4, ascending ? 1 : 0);
    return slot;
}

void validate_edit(ViewSortEdit const& edit, std::string const& function,
                   std::uint32_t& field_id) {
    if (edit.keys.size() != 1) {
        throw Error(function + ": " + number(edit.keys.size()) + " sort keys "
                    "were requested; exactly one key per sort is measured, so "
                    "this sort is refused");
    }
    field_id = field_id_of(edit.keys.front().field, function);
    if (edit.view_uid != kMeasuredViewUid) {
        throw Error(function + ": view uid " + number(edit.view_uid) + " is "
                    "unmeasured (the pairs sort view uid " +
                    number(kMeasuredViewUid) + "), so this sort is refused");
    }
    if (!edit.outline) {
        throw Error(function + ": Outline = False is unmeasured as a distinct "
                    "sort (the pair writes the same bytes as Outline = True on "
                    "the measured rows), so this sort is refused");
    }
    if (edit.renumber) {
        throw Error(function + ": Renumber = True is unmeasured (the pairs "
                    "leave the IDs alone), so this sort is refused");
    }
    if (edit.base_order.size() != kSortMeasuredRows) {
        throw Error(function + ": " + number(edit.base_order.size()) +
                    " rows were requested; the order records are measured at " +
                    number(kSortMeasuredRows) + " rows only, so this sort is "
                    "refused");
    }
    if (edit.sorted_order.size() != edit.base_order.size()) {
        throw Error(function + ": the sorted order holds " +
                    number(edit.sorted_order.size()) + " rows, not " +
                    number(edit.base_order.size()));
    }
    std::set<std::uint32_t> const base(edit.base_order.begin(),
                                       edit.base_order.end());
    std::set<std::uint32_t> const sorted(edit.sorted_order.begin(),
                                         edit.sorted_order.end());
    if (base.size() != edit.base_order.size() || base.count(0) != 0 ||
        base.count(kSortSlotNone) != 0) {
        throw Error(function + ": the base order is not a list of distinct "
                    "task UniqueIDs");
    }
    if (sorted != base) {
        throw Error(function + ": the sorted order is not a permutation of the "
                    "base order");
    }
}

struct SortBlock {
    std::size_t offset = 0; /* the block's outer length word in Var2Data */
};

SortBlock locate_block(CfbDocument const& doc, std::string const& function) {
    std::string const meta_path = std::string(kViewTable) + "/VarMeta";
    std::string const var2_path = std::string(kViewTable) + "/Var2Data";
    if (!doc.has_stream(meta_path) || !doc.has_stream(var2_path)) {
        throw Error(function + ": the document has no " + meta_path + " or " +
                    var2_path);
    }
    Bytes const meta = doc.read_stream(meta_path);
    Bytes const var2 = doc.read_stream(var2_path);
    std::vector<VarEntry> const entries = read_var_entries(meta);
    SortBlock block;
    std::size_t found = 0;
    for (VarEntry const& entry : entries) {
        if (entry.mid == kMeasuredViewUid && entry.field_id == kViewStateKey) {
            block.offset = entry.offset;
            ++found;
        }
    }
    if (found != 1) {
        throw Error(function + ": " + number(found) + " view-state blocks "
                    "(uid 2, key 0x20400006) were found, not one");
    }
    if (block.offset + 4 + kMeasuredBlockPayload > var2.size() ||
        get_u32(var2, block.offset) != kMeasuredBlockPayload) {
        throw Error(function + ": the view-state block is not the measured " +
                    number(kMeasuredBlockPayload) + "-byte block");
    }
    return block;
}

std::size_t spec_at(SortBlock const& block, std::size_t which) {
    return block.offset + which;
}

void require_default_spec(Bytes const& var2, SortBlock const& block,
                          std::string const& function) {
    Bytes const none = slot_bytes(kSortSlotNone, true);
    Bytes const first = slot_bytes(kSortFieldId, true);
    for (std::size_t const at : {kSortSpecFirst, kSortSpecSecond}) {
        std::size_t const base = spec_at(block, at);
        bool ok = std::equal(first.begin(), first.end(),
                             var2.begin() + static_cast<std::ptrdiff_t>(base));
        for (std::size_t slot = 1; slot < 3; ++slot) {
            ok = ok && std::equal(none.begin(), none.end(),
                                  var2.begin() + static_cast<std::ptrdiff_t>(
                                                     base + slot * kSlotBytes));
        }
        if (!ok) {
            throw Error(function + ": the file already carries a sort (the spec "
                        "at block + " + number(at) + " is not the default ID "
                        "ascending), and replacing a sort is unmeasured, so "
                        "this sort is refused");
        }
    }
}

struct EdlRecords {
    std::size_t order = 0;     /* payload offset of 0x26400035 */
    std::size_t first_row = 0; /* payload offset of 0x26400036 */
    std::size_t copy = 0;      /* payload offset of 0x26400024 */
};

EdlRecords locate_edl(Bytes const& edl, std::string const& function) {
    if (edl.size() < kEdlHeaderSize || get_u32(edl, 0) + 4 != edl.size() ||
        get_u32(edl, 8) != get_u32(edl, 4) ||
        get_u32(edl, 12) != 0x00010000u) {
        throw Error(function + ": 214/CEdl/Var2Data does not have the measured "
                    "header");
    }
    std::size_t const count = get_u32(edl, kEdlCountWord);
    std::size_t at = kEdlHeaderSize;
    EdlRecords records;
    std::size_t order_seen = 0;
    std::size_t first_seen = 0;
    std::size_t copy_seen = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (at + kEdlItemHeaderSize > edl.size()) {
            throw Error(function + ": 214/CEdl/Var2Data item " + number(i) +
                        " runs past the stream");
        }
        std::size_t const length = get_u32(edl, at);
        std::uint32_t const id = get_u32(edl, at + 4);
        std::size_t const payload = at + kEdlItemHeaderSize;
        if (payload + length > edl.size()) {
            throw Error(function + ": 214/CEdl/Var2Data item " + number(i) +
                        " runs past the stream");
        }
        if (id == kEdlOrderId) {
            records.order = payload;
            ++order_seen;
            if (length != kEdlOrderPayload) {
                throw Error(function + ": the CEdl order record holds " +
                            number(length) + " bytes, not the measured " +
                            number(kEdlOrderPayload));
            }
        } else if (id == kEdlFirstRowId) {
            records.first_row = payload;
            ++first_seen;
            if (length != 4) {
                throw Error(function + ": the CEdl first-row record holds " +
                            number(length) + " bytes, not 4");
            }
        } else if (id == kEdlOrderCopyId) {
            records.copy = payload;
            ++copy_seen;
            if (length != kEdlCopyPayload) {
                throw Error(function + ": the CEdl order-copy record holds " +
                            number(length) + " bytes, not the measured " +
                            number(kEdlCopyPayload));
            }
        }
        at = payload + length + (length % 2);
    }
    if (order_seen != 1 || first_seen != 1 || copy_seen != 1) {
        throw Error(function + ": 214/CEdl/Var2Data holds " + number(order_seen) +
                    " order records, " + number(first_seen) + " first-row "
                    "records and " + number(copy_seen) + " order copies; the "
                    "pairs measure 1, 1 and 1");
    }
    return records;
}

void require_edl_state(Bytes const& edl, EdlRecords const& records,
                       std::vector<std::uint32_t> const& order,
                       std::string const& function, std::string const& which) {
    std::size_t const rows = order.size();
    for (std::size_t i = 0; i < rows; ++i) {
        if (get_u32(edl, records.order + 4 * i) != order[i]) {
            throw Error(function + ": the " + which + " CEdl order record "
                        "does not hold the expected UniqueID at row " +
                        number(i + 1));
        }
    }
    if (get_u32(edl, records.order + 4 * rows) != kEdlOrderTail0 ||
        get_u32(edl, records.order + 4 * rows + 4) != 0 ||
        get_u32(edl, records.order + 4 * rows + 8) != 0) {
        throw Error(function + ": the " + which + " CEdl order record's tail "
                    "dwords are not the measured 0x000ffdc0, 0, 0");
    }
    if (get_u32(edl, records.first_row) != order.front()) {
        throw Error(function + ": the " + which + " CEdl first-row record is "
                    "not the first row of the order");
    }
    for (std::size_t i = 0; i < kEdlCopyPayload / 4; ++i) {
        std::uint32_t const expected = i < rows ? order[i] : kSortSlotNone;
        if (get_u32(edl, records.copy + 4 * i) != expected) {
            throw Error(function + ": the " + which + " CEdl order copy "
                        "does not hold the expected dword " + number(i));
        }
    }
}

void write_order(Bytes& edl, EdlRecords const& records,
                 std::vector<std::uint32_t> const& order) {
    for (std::size_t i = 0; i < order.size(); ++i) {
        put_u32(edl, records.order + 4 * i, order[i]);
    }
    put_u32(edl, records.first_row, order.front());
    for (std::size_t i = 0; i < order.size(); ++i) {
        put_u32(edl, records.copy + 4 * i, order[i]);
    }
}

} /* namespace */

std::uint64_t sort_block_digest(Bytes const& var2, std::size_t block_offset) {
    if (block_offset + 4 + kMeasuredBlockPayload > var2.size()) {
        throw Error("view sort write: the block runs past Var2Data");
    }
    Bytes payload(var2.begin() + static_cast<std::ptrdiff_t>(block_offset + 4),
                  var2.begin() + static_cast<std::ptrdiff_t>(
                                     block_offset + 4 + kMeasuredBlockPayload));
    for (std::size_t const at : {kSortSpecFirst, kSortSpecSecond}) {
        std::fill_n(payload.begin() + static_cast<std::ptrdiff_t>(at - 4),
                    kSortSpecBytes, std::uint8_t{0});
    }
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (std::uint8_t const byte : payload) {
        hash = (hash ^ byte) * 0x100000001b3ull;
    }
    return hash;
}

ViewSortWrite save_view_sort(CfbDocument& doc, ViewSortEdit const& edit,
                             std::string const& path, bool dry_run) {
    std::string const function = "view sort write";
    if (!dry_run) {
        check_output_path(path, function);
    }
    std::uint32_t field_id = 0;
    validate_edit(edit, function, field_id);
    bool const ascending = edit.keys.front().ascending;

    std::string const var2_path = std::string(kViewTable) + "/Var2Data";
    std::string const edl_path = std::string(kEdlTable) + "/Var2Data";
    if (!doc.has_stream(edl_path)) {
        throw Error(function + ": the document has no " + edl_path);
    }
    CfbDocument const opened = doc;
    SortBlock const block = locate_block(doc, function);
    Bytes const var2 = doc.read_stream(var2_path);
    if (sort_block_digest(var2, block.offset) != kMeasuredBlockDigest) {
        throw Error(function + ": the view-state block is not the measured "
                    "control's outside its sort spec (digest mismatch), so the "
                    "text style around it is unmeasured and this sort is "
                    "refused");
    }
    require_default_spec(var2, block, function);
    Bytes const edl = doc.read_stream(edl_path);
    EdlRecords const records = locate_edl(edl, function);
    require_edl_state(edl, records, edit.base_order, function, "control's");

    Bytes const slot = slot_bytes(field_id, ascending);
    Bytes new_var2 = var2;
    for (std::size_t const at : {kSortSpecFirst, kSortSpecSecond}) {
        std::copy(slot.begin(), slot.end(),
                  new_var2.begin() +
                      static_cast<std::ptrdiff_t>(spec_at(block, at)));
    }
    Bytes new_edl = edl;
    write_order(new_edl, records, edit.sorted_order);

    CfbDocument work = doc;
    work.replace_stream(var2_path, new_var2);
    work.replace_stream(edl_path, new_edl);
    EditMarker const marker = write_edit_marker(work);
    if (marker.record_present && marker.after != kEditMarkerAfterSave) {
        throw Error(function + ": the staged document's Props14 marker is " +
                    number(marker.after) + ", not 1");
    }

    ViewSortWrite result;
    result.view_uid = edit.view_uid;
    result.field_id = field_id;
    result.ascending = ascending;
    result.block_offset = block.offset;
    result.order_rows = edit.sorted_order.size();
    result.order_records_written = 3;
    result.first_row_before = edit.base_order.front();
    result.first_row_after = edit.sorted_order.front();
    for (std::size_t const at : {kSortSpecFirst, kSortSpecSecond}) {
        for (std::size_t i = 0; i < kSortSpecBytes; ++i) {
            if (new_var2[spec_at(block, at) + i] !=
                var2[spec_at(block, at) + i]) {
                ++result.spec_bytes_changed;
            }
        }
    }

    /* The staged streams must already hold the requested state. */
    if (dry_run) {
        Bytes const staged_edl = work.read_stream(edl_path);
        require_edl_state(staged_edl, locate_edl(staged_edl, function),
                          edit.sorted_order, function, "staged");
        return result;
    }

    Bytes const intended = work.to_bytes();
    work.save(path);
    Bytes const on_disk = read_file_bytes(path);
    if (on_disk != intended) {
        throw Error(function + ": " + path + " holds " + number(on_disk.size()) +
                    " bytes, but the document built " + number(intended.size()) +
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

    /* Var2Data: the control's bytes except slot 0 of both spec copies. */
    Bytes const after_var2 = reread.read_stream(var2_path);
    if (after_var2.size() != var2.size()) {
        throw Error(function + ": the written Var2Data changed size");
    }
    for (std::size_t i = 0; i < var2.size(); ++i) {
        bool owned = false;
        for (std::size_t const at : {kSortSpecFirst, kSortSpecSecond}) {
            std::size_t const base = spec_at(block, at);
            owned = owned || (i >= base && i < base + kSlotBytes);
        }
        if (!owned && after_var2[i] != var2[i]) {
            throw Error(function + ": the written Var2Data changed at byte " +
                        number(i) + ", which this sort does not own");
        }
    }
    for (std::size_t const at : {kSortSpecFirst, kSortSpecSecond}) {
        if (!std::equal(slot.begin(), slot.end(),
                        after_var2.begin() +
                            static_cast<std::ptrdiff_t>(spec_at(block, at)))) {
            throw Error(function + ": the written spec at block + " +
                        number(at) + " is not the requested key");
        }
    }

    /* CEdl: the control's bytes except the order dwords and the first row. */
    Bytes const after_edl = reread.read_stream(edl_path);
    if (after_edl.size() != edl.size()) {
        throw Error(function + ": the written CEdl Var2Data changed size");
    }
    EdlRecords const after_records = locate_edl(after_edl, function);
    if (after_records.order != records.order ||
        after_records.first_row != records.first_row ||
        after_records.copy != records.copy) {
        throw Error(function + ": the written CEdl records moved");
    }
    for (std::size_t i = 0; i < edl.size(); ++i) {
        bool owned = (i >= records.order &&
                      i < records.order + 4 * edit.sorted_order.size()) ||
                     (i >= records.first_row && i < records.first_row + 4) ||
                     (i >= records.copy &&
                      i < records.copy + 4 * edit.sorted_order.size());
        if (!owned && after_edl[i] != edl[i]) {
            throw Error(function + ": the written CEdl Var2Data changed at "
                        "byte " + number(i) + ", which this sort does not own");
        }
    }
    require_edl_state(after_edl, after_records, edit.sorted_order, function,
                      "written");

    /* No other stream moved; Props14 changes by the marker only. */
    for (std::string const& stream : opened.stream_paths()) {
        if (stream == var2_path || stream == edl_path ||
            stream == kEditMarkerStream) {
            continue;
        }
        if (!reread.has_stream(stream) ||
            reread.read_stream(stream) != opened.read_stream(stream)) {
            throw Error(function + ": the unowned stream " + stream +
                        " changed or vanished");
        }
    }
    if (reread.stream_paths().size() != opened.stream_paths().size()) {
        throw Error(function + ": the written file holds a different number of "
                    "streams");
    }

    doc = std::move(reread);
    return result;
}

} /* namespace mppwrite */
