/*
 * viewstate_outline.cpp -- the Task Sheet outline level persisted in the file.
 * W35-2.3.  The measured rules and the capability matrix are in
 * viewstate_outline.h; the pair derivation is in spec/writers/view-state.md.
 */

#include "viewstate_outline.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <utility>

#include "edit_marker.h"
#include "table.h"
#include "viewstate_sort.h"
#include "viewstream_edit.h"

namespace mppwrite {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr char const* kTaskFixed = "114/TBkndTask/FixedData";
constexpr char const* kTaskMeta = "114/TBkndTask/FixedMeta";
constexpr char const* kEdlVar2 = "214/CEdl/Var2Data";
constexpr char const* kEdlMeta = "214/CEdl/VarMeta";

constexpr std::size_t kEdlHeaderSize = 20;
constexpr std::size_t kEdlItemHeaderSize = 12;
constexpr std::size_t kEdlCountWord = 16;
constexpr std::size_t kEdlCopyPayload = 64;
constexpr std::size_t kOrderTailDwords = 3;
/* 0x2640003d, present in some controls: the displayed rows' layout. */
constexpr std::uint32_t kEdlRowLayoutId = 0x2640003Du;
constexpr std::uint32_t kLayoutRowHeight = 0x16;
constexpr std::size_t kLayoutFixedBytes = 14;

/* The outlines the pairs measure, 0-based levels in file order. */
constexpr std::array<std::uint32_t, 5> kBaseLevels = {0, 1, 1, 0, 0};
constexpr std::array<std::uint32_t, 6> kDeepLevels = {0, 1, 2, 2, 1, 0};

std::uint32_t get_u32(Bytes const& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) |
           (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) |
           (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

std::uint32_t get_big_u32(Bytes const& b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at]) << 24) |
           (static_cast<std::uint32_t>(b[at + 1]) << 16) |
           (static_cast<std::uint32_t>(b[at + 2]) << 8) |
           static_cast<std::uint32_t>(b[at + 3]);
}

void put_u32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[at + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
    }
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
        throw Error("view outline write: cannot re-open '" + path + "'");
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

bool levels_match(std::vector<OutlineViewRow> const& rows,
                  std::uint32_t const* levels, std::size_t count) {
    if (rows.size() != count) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (rows[i].level != levels[i]) {
            return false;
        }
    }
    return true;
}

void validate_edit(ViewOutlineEdit const& edit, std::string const& function) {
    if (edit.view_uid != kMeasuredViewUid) {
        throw Error(function + ": view uid " + number(edit.view_uid) + " is "
                    "unmeasured (the pairs outline view uid " +
                    number(kMeasuredViewUid) + "), so this outline level is "
                    "refused");
    }
    if (edit.show_level < kOutlineMeasuredMinLevel ||
        edit.show_level > kOutlineMeasuredMaxLevel) {
        throw Error(function + ": Show Level " + std::to_string(edit.show_level) +
                    " is unmeasured (the pairs show level " +
                    std::to_string(kOutlineMeasuredMinLevel) + " and " +
                    std::to_string(kOutlineMeasuredMaxLevel) + "), so this "
                    "outline level is refused");
    }
    if (!levels_match(edit.rows, kBaseLevels.data(), kBaseLevels.size()) &&
        !levels_match(edit.rows, kDeepLevels.data(), kDeepLevels.size())) {
        throw Error(function + ": the outline of " + number(edit.rows.size()) +
                    " tasks is not one the pairs measure (levels 1,2,2,1,1 over "
                    "five tasks or 1,2,3,3,2,1 over six), so this outline level "
                    "is refused");
    }
    std::set<std::uint32_t> uids;
    for (OutlineViewRow const& row : edit.rows) {
        uids.insert(row.uid);
    }
    if (uids.size() != edit.rows.size() || uids.count(0) != 0) {
        throw Error(function + ": the rows are not distinct task UniqueIDs");
    }
}

/* One item of the CEdl list: `at` is the offset of its 12-byte header. */
struct EdlItem {
    std::size_t at = 0;
    std::uint32_t id = 0;
    std::uint32_t kind = 0;
    std::size_t length = 0;
    std::size_t payload() const { return at + kEdlItemHeaderSize; }
    std::size_t end() const { return payload() + length + (length % 2); }
};

std::vector<EdlItem> parse_edl_items(Bytes const& edl, std::string const& function) {
    if (edl.size() < kEdlHeaderSize || get_u32(edl, 0) + 4 != edl.size() ||
        get_u32(edl, 4) + 8 != edl.size() || get_u32(edl, 8) != get_u32(edl, 4) ||
        get_u32(edl, 12) != 0x00010000u) {
        throw Error(function + ": 214/CEdl/Var2Data does not have the measured "
                    "header");
    }
    std::size_t const count = get_u32(edl, kEdlCountWord);
    std::size_t at = kEdlHeaderSize;
    std::vector<EdlItem> items;
    for (std::size_t i = 0; i < count; ++i) {
        if (at + kEdlItemHeaderSize > edl.size()) {
            throw Error(function + ": 214/CEdl/Var2Data item " + number(i) +
                        " runs past the stream");
        }
        EdlItem item;
        item.at = at;
        item.length = get_u32(edl, at);
        item.id = get_u32(edl, at + 4);
        item.kind = get_u32(edl, at + 8);
        if (item.payload() + item.length > edl.size()) {
            throw Error(function + ": 214/CEdl/Var2Data item " + number(i) +
                        " runs past the stream");
        }
        items.push_back(item);
        at = item.end();
    }
    if (at != edl.size()) {
        throw Error(function + ": 214/CEdl/Var2Data does not end at its last "
                    "item");
    }
    return items;
}

struct EdlRecords {
    std::size_t order = 0; /* payload offset of 0x26400035 */
    std::size_t order_bytes = 0;
    std::size_t first_row = 0; /* payload offset of 0x26400036 */
    std::size_t copy = 0;      /* payload offset of 0x26400024 */
    bool has_layout = false;   /* the optional 0x2640003d record */
    std::size_t layout = 0;    /* its payload offset */
    std::size_t layout_bytes = 0;
};

EdlRecords locate_edl(Bytes const& edl, std::string const& function) {
    EdlRecords records;
    std::size_t order_seen = 0;
    std::size_t first_seen = 0;
    std::size_t copy_seen = 0;
    std::size_t layout_seen = 0;
    for (EdlItem const& item : parse_edl_items(edl, function)) {
        if (item.id == kEdlOrderId) {
            records.order = item.payload();
            records.order_bytes = item.length;
            ++order_seen;
        } else if (item.id == kEdlFirstRowId) {
            records.first_row = item.payload();
            ++first_seen;
            if (item.length != 4) {
                throw Error(function + ": the CEdl first-row record holds " +
                            number(item.length) + " bytes, not 4");
            }
        } else if (item.id == kEdlOrderCopyId) {
            records.copy = item.payload();
            ++copy_seen;
            if (item.length != kEdlCopyPayload) {
                throw Error(function + ": the CEdl order-copy record holds " +
                            number(item.length) + " bytes, not the measured " +
                            number(kEdlCopyPayload));
            }
        } else if (item.id == kEdlRowLayoutId) {
            records.has_layout = true;
            records.layout = item.payload();
            records.layout_bytes = item.length;
            ++layout_seen;
        }
    }
    if (order_seen != 1 || first_seen != 1 || copy_seen != 1 || layout_seen > 1) {
        throw Error(function + ": 214/CEdl/Var2Data holds " + number(order_seen) +
                    " order records, " + number(first_seen) + " first-row "
                    "records, " + number(copy_seen) + " order copies and " +
                    number(layout_seen) + " row-layout records; the pairs "
                    "measure 1, 1, 1 and 0 or 1");
    }
    return records;
}

/* The row-layout record for `rows` displayed rows: the row count, each row's
 * offset (a row is 22 units high), the end offset, the row height and two zero
 * bytes. */
Bytes row_layout_payload(std::size_t rows) {
    Bytes payload(kLayoutFixedBytes + 4 * rows, 0);
    put_u32(payload, 0, static_cast<std::uint32_t>(rows));
    for (std::size_t i = 0; i <= rows; ++i) {
        put_u32(payload, 4 + 4 * i, static_cast<std::uint32_t>(kLayoutRowHeight * i));
    }
    put_u32(payload, 8 + 4 * rows, kLayoutRowHeight);
    return payload;
}

/* The CEdl must list exactly `shown` (UniqueIDs in file order), with the order
 * record's tail 0x000ffdc0 - `hidden`, 0, 0. */
void require_edl_state(Bytes const& edl, EdlRecords const& records,
                       std::vector<std::uint32_t> const& shown, std::size_t hidden,
                       std::string const& function, std::string const& which) {
    std::size_t const rows = shown.size();
    if (records.order_bytes != 4 * (rows + kOrderTailDwords)) {
        throw Error(function + ": the " + which + " CEdl order record holds " +
                    number(records.order_bytes) + " bytes, not " +
                    number(4 * (rows + kOrderTailDwords)) + " for " +
                    number(rows) + " displayed rows");
    }
    for (std::size_t i = 0; i < rows; ++i) {
        if (get_u32(edl, records.order + 4 * i) != shown[i]) {
            throw Error(function + ": the " + which + " CEdl order record "
                        "does not hold the expected UniqueID at row " +
                        number(i + 1));
        }
    }
    if (get_u32(edl, records.order + 4 * rows) !=
            kOutlineOrderTail - static_cast<std::uint32_t>(hidden) ||
        get_u32(edl, records.order + 4 * rows + 4) != 0 ||
        get_u32(edl, records.order + 4 * rows + 8) != 0) {
        throw Error(function + ": the " + which + " CEdl order record's tail "
                    "dwords are not 0x000ffdc0 - " + number(hidden) + ", 0, 0");
    }
    if (get_u32(edl, records.first_row) != shown.front()) {
        throw Error(function + ": the " + which + " CEdl first-row record is "
                    "not the first displayed row");
    }
    for (std::size_t i = 0; i < kEdlCopyPayload / 4; ++i) {
        std::uint32_t const expected = i < rows ? shown[i] : kSortSlotNone;
        if (get_u32(edl, records.copy + 4 * i) != expected) {
            throw Error(function + ": the " + which + " CEdl order copy "
                        "does not hold the expected dword " + number(i));
        }
    }
    if (records.has_layout) {
        Bytes const expected = row_layout_payload(rows);
        if (records.layout_bytes != expected.size()) {
            throw Error(function + ": the " + which + " CEdl row-layout record "
                        "holds " + number(records.layout_bytes) + " bytes, not " +
                        number(expected.size()) + " for " + number(rows) +
                        " displayed rows");
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (edl[records.layout + i] != expected[i]) {
                throw Error(function + ": the " + which + " CEdl row-layout "
                            "record is not the measured layout at byte " +
                            number(i));
            }
        }
    }
}

/* The CEdl for `shown` rows: the order, first-row, order-copy and row-layout
 * items rebuilt, every other item copied byte for byte. */
Bytes assemble_edl(Bytes const& edl, std::vector<EdlItem> const& items,
                   std::vector<std::uint32_t> const& shown, std::size_t hidden) {
    Bytes order(4 * (shown.size() + kOrderTailDwords), 0);
    for (std::size_t i = 0; i < shown.size(); ++i) {
        put_u32(order, 4 * i, shown[i]);
    }
    put_u32(order, 4 * shown.size(), kOutlineOrderTail - static_cast<std::uint32_t>(hidden));
    Bytes first(4, 0);
    put_u32(first, 0, shown.front());
    Bytes copy(kEdlCopyPayload, 0);
    for (std::size_t i = 0; i < kEdlCopyPayload / 4; ++i) {
        put_u32(copy, 4 * i, i < shown.size() ? shown[i] : kSortSlotNone);
    }
    Bytes out(edl.begin(), edl.begin() + static_cast<std::ptrdiff_t>(kEdlHeaderSize));
    for (EdlItem const& item : items) {
        Bytes const* replacement = nullptr;
        Bytes layout;
        if (item.id == kEdlOrderId) {
            replacement = &order;
        } else if (item.id == kEdlFirstRowId) {
            replacement = &first;
        } else if (item.id == kEdlOrderCopyId) {
            replacement = &copy;
        } else if (item.id == kEdlRowLayoutId) {
            layout = row_layout_payload(shown.size());
            replacement = &layout;
        }
        if (replacement == nullptr) {
            out.insert(out.end(), edl.begin() + static_cast<std::ptrdiff_t>(item.at),
                       edl.begin() + static_cast<std::ptrdiff_t>(item.end()));
            continue;
        }
        std::size_t const at = out.size();
        out.insert(out.end(), edl.begin() + static_cast<std::ptrdiff_t>(item.at),
                   edl.begin() + static_cast<std::ptrdiff_t>(item.payload()));
        put_u32(out, at, static_cast<std::uint32_t>(replacement->size()));
        out.insert(out.end(), replacement->begin(), replacement->end());
        if (replacement->size() % 2 != 0) {
            out.push_back(0);
        }
    }
    put_u32(out, 0, static_cast<std::uint32_t>(out.size() - 4));
    put_u32(out, 4, static_cast<std::uint32_t>(out.size() - 8));
    put_u32(out, 8, static_cast<std::uint32_t>(out.size() - 8));
    return out;
}

/* The byte offset of the VALUE of the one 16-byte scalar property in 214/Props
 * whose big-endian tag is `tag` (u32 4, tag, u32 4, value). */
std::size_t props_value_offset(Bytes const& props, std::uint32_t tag,
                               std::string const& function) {
    std::size_t found = props.size();
    std::size_t matches = 0;
    for (std::size_t at = 0; at + 16 <= props.size(); ++at) {
        if (get_u32(props, at) != 4 || get_u32(props, at + 8) != 4 ||
            get_big_u32(props, at + 4) != tag) {
            continue;
        }
        found = at + 12;
        ++matches;
    }
    if (matches != 1) {
        throw Error(function + ": 214/Props holds " + number(matches) +
                    " scalar records for the CEdl size tag, not one");
    }
    return found;
}

std::size_t meta_flag_at(std::size_t row) {
    return kOutlineMetaHeader + kOutlineMetaEntry * (row + 3) + kOutlineMetaFlag;
}

} /* namespace */

void outline_show_level_sets(std::vector<OutlineViewRow> const& rows, int show_level,
                             std::vector<bool>& hidden, std::vector<bool>& collapsed) {
    hidden.assign(rows.size(), false);
    collapsed.assign(rows.size(), false);
    if (show_level < 1) {
        return;
    }
    std::uint32_t const keep = static_cast<std::uint32_t>(show_level);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        bool const summary = i + 1 < rows.size() && rows[i + 1].level > rows[i].level;
        if (rows[i].level >= keep) {
            hidden[i] = true;
        } else if (rows[i].level + 1 == keep && summary) {
            collapsed[i] = true;
        }
    }
}

ViewOutlineWrite save_view_outline(CfbDocument& doc, ViewOutlineEdit const& edit,
                                   std::string const& path, bool dry_run) {
    std::string const function = "view outline write";
    if (!dry_run) {
        check_output_path(path, function);
    }
    validate_edit(edit, function);
    std::size_t const n = edit.rows.size();

    for (char const* stream : {kTaskFixed, kTaskMeta, kEdlVar2, kEdlMeta, kViewProps}) {
        if (!doc.has_stream(stream)) {
            throw Error(function + ": the document has no " + stream);
        }
    }
    CfbDocument const opened = doc;

    /* The rows are the file's task rows 1..N, in order. */
    TaskTable const table = TaskTable::from_document(doc);
    if (table.rows() != n + 1) {
        throw Error(function + ": the file holds " + number(table.rows()) +
                    " task rows (with the project row), not the " + number(n + 1) +
                    " the request describes");
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (table.uid(i + 1) != edit.rows[i].uid) {
            throw Error(function + ": task row " + number(i + 1) + " is "
                        "UniqueID " + number(table.uid(i + 1)) + ", not " +
                        number(edit.rows[i].uid) + "; the request is not the "
                        "file's row order");
        }
    }

    /* FixedMeta: the control shows every row. */
    Bytes const meta = doc.read_stream(kTaskMeta);
    if (meta.size() != kOutlineMetaHeader + kOutlineMetaEntry * (n + 1 + 3)) {
        throw Error(function + ": 114/TBkndTask/FixedMeta holds " +
                    number(meta.size()) + " bytes, not " +
                    number(kOutlineMetaHeader + kOutlineMetaEntry * (n + 4)) +
                    " for " + number(n + 1) + " rows");
    }
    for (std::size_t row = 1; row <= n; ++row) {
        if (meta[meta_flag_at(row)] != kOutlineFlagShown) {
            throw Error(function + ": task row " + number(row) + " is already "
                        "collapsed or hidden (flag " +
                        number(meta[meta_flag_at(row)]) + "), and replacing an "
                        "outline state is unmeasured, so this outline level is "
                        "refused");
        }
    }

    /* CEdl: the control lists rows 1..N in file order. */
    Bytes const edl = doc.read_stream(kEdlVar2);
    EdlRecords const records = locate_edl(edl, function);
    std::vector<std::uint32_t> all;
    for (OutlineViewRow const& row : edit.rows) {
        all.push_back(row.uid);
    }
    require_edl_state(edl, records, all, 0, function, "control's");
    Bytes const edl_meta = doc.read_stream(kEdlMeta);
    if (edl_meta.size() < kVarMetaHeaderSize ||
        get_u32(edl_meta, kVarMetaVar2SizeOffset) != edl.size()) {
        throw Error(function + ": 214/CEdl/VarMeta does not carry the CEdl "
                    "Var2Data size");
    }
    Bytes const props = doc.read_stream(kViewProps);
    std::size_t const props_at = props_value_offset(props, kOutlineEdlSizeTag, function);
    if (get_u32(props, props_at) != edl.size()) {
        throw Error(function + ": 214/Props says the CEdl Var2Data is " +
                    number(get_u32(props, props_at)) + " bytes but it is " +
                    number(edl.size()));
    }

    /* The requested state. */
    std::vector<bool> hidden;
    std::vector<bool> collapsed;
    outline_show_level_sets(edit.rows, edit.show_level, hidden, collapsed);
    std::vector<std::uint32_t> shown;
    std::size_t hidden_rows = 0;
    std::size_t collapsed_rows = 0;
    Bytes new_meta = meta;
    for (std::size_t i = 0; i < n; ++i) {
        if (hidden[i]) {
            ++hidden_rows;
            new_meta[meta_flag_at(i + 1)] = kOutlineFlagHidden;
        } else {
            shown.push_back(edit.rows[i].uid);
            if (collapsed[i]) {
                ++collapsed_rows;
                new_meta[meta_flag_at(i + 1)] = kOutlineFlagCollapsed;
            }
        }
    }

    Bytes const new_edl = assemble_edl(edl, parse_edl_items(edl, function), shown,
                                       hidden_rows);
    Bytes new_edl_meta = edl_meta;
    put_u32(new_edl_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(new_edl.size()));
    Bytes new_props = props;
    put_u32(new_props, props_at, static_cast<std::uint32_t>(new_edl.size()));

    CfbDocument work = doc;
    work.replace_stream(kTaskMeta, new_meta);
    work.replace_stream(kEdlVar2, new_edl);
    work.replace_stream(kEdlMeta, new_edl_meta);
    work.replace_stream(kViewProps, new_props);
    EditMarker const marker = write_edit_marker(work);
    if (marker.record_present && marker.after != kEditMarkerAfterSave) {
        throw Error(function + ": the staged document's Props14 marker is " +
                    number(marker.after) + ", not 1");
    }

    ViewOutlineWrite result;
    result.show_level = edit.show_level;
    result.rows = n;
    result.hidden_rows = hidden_rows;
    result.collapsed_rows = collapsed_rows;
    result.edl_bytes_before = edl.size();
    result.edl_bytes_after = new_edl.size();
    for (std::size_t i = 0; i < meta.size(); ++i) {
        if (new_meta[i] != meta[i]) {
            ++result.flags_written;
        }
    }

    /* The staged streams must already hold the requested state. */
    Bytes const staged_edl = work.read_stream(kEdlVar2);
    EdlRecords const staged_records = locate_edl(staged_edl, function);
    require_edl_state(staged_edl, staged_records, shown, hidden_rows, function,
                      "staged");
    if (dry_run) {
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

    /* FixedMeta: the control's bytes except the flag of each changed row. */
    Bytes const after_meta = reread.read_stream(kTaskMeta);
    if (after_meta.size() != meta.size()) {
        throw Error(function + ": the written FixedMeta changed size");
    }
    for (std::size_t i = 0; i < meta.size(); ++i) {
        bool owned = false;
        for (std::size_t row = 1; row <= n; ++row) {
            owned = owned || i == meta_flag_at(row);
        }
        if (!owned && after_meta[i] != meta[i]) {
            throw Error(function + ": the written FixedMeta changed at byte " +
                        number(i) + ", which this outline level does not own");
        }
    }
    for (std::size_t row = 1; row <= n; ++row) {
        std::uint8_t expected = kOutlineFlagShown;
        if (hidden[row - 1]) {
            expected = kOutlineFlagHidden;
        } else if (collapsed[row - 1]) {
            expected = kOutlineFlagCollapsed;
        }
        if (after_meta[meta_flag_at(row)] != expected) {
            throw Error(function + ": the written flag of task row " +
                        number(row) + " is not the requested state");
        }
    }

    /* CEdl: the order, first-row, order-copy and row-layout items are owned;
     * every other item, and the id and kind words of the owned ones, are the
     * control's byte for byte. */
    Bytes const after_edl = reread.read_stream(kEdlVar2);
    EdlRecords const after_records = locate_edl(after_edl, function);
    require_edl_state(after_edl, after_records, shown, hidden_rows, function,
                      "written");
    std::vector<EdlItem> const before_items = parse_edl_items(edl, function);
    std::vector<EdlItem> const after_items = parse_edl_items(after_edl, function);
    if (after_items.size() != before_items.size()) {
        throw Error(function + ": the written CEdl Var2Data holds " +
                    number(after_items.size()) + " items, not " +
                    number(before_items.size()));
    }
    for (std::size_t i = 0; i < before_items.size(); ++i) {
        EdlItem const& was = before_items[i];
        EdlItem const& now = after_items[i];
        if (now.id != was.id || now.kind != was.kind) {
            throw Error(function + ": the written CEdl item " + number(i) +
                        " changed its id or kind word");
        }
        bool const owned = was.id == kEdlOrderId || was.id == kEdlFirstRowId ||
                           was.id == kEdlOrderCopyId || was.id == kEdlRowLayoutId;
        if (!owned &&
            (now.length != was.length ||
             !std::equal(edl.begin() + static_cast<std::ptrdiff_t>(was.payload()),
                         edl.begin() + static_cast<std::ptrdiff_t>(was.end()),
                         after_edl.begin() + static_cast<std::ptrdiff_t>(now.payload())))) {
            throw Error(function + ": the written CEdl item " + number(i) +
                        " (id " + number(was.id) + ") changed, and this outline "
                        "level does not own it");
        }
    }

    /* VarMeta and Props: the size word and nothing else. */
    Bytes const after_edl_meta = reread.read_stream(kEdlMeta);
    Bytes const after_props = reread.read_stream(kViewProps);
    if (after_edl_meta.size() != edl_meta.size() ||
        after_props.size() != props.size()) {
        throw Error(function + ": the written CEdl VarMeta or 214/Props changed "
                    "size");
    }
    for (std::size_t i = 0; i < edl_meta.size(); ++i) {
        bool const owned = i >= kVarMetaVar2SizeOffset &&
                           i < kVarMetaVar2SizeOffset + 4;
        if (!owned && after_edl_meta[i] != edl_meta[i]) {
            throw Error(function + ": the written CEdl VarMeta changed at byte " +
                        number(i) + ", which this outline level does not own");
        }
    }
    for (std::size_t i = 0; i < props.size(); ++i) {
        bool const owned = i >= props_at && i < props_at + 4;
        if (!owned && after_props[i] != props[i]) {
            throw Error(function + ": the written 214/Props changed at byte " +
                        number(i) + ", which this outline level does not own");
        }
    }
    if (get_u32(after_edl_meta, kVarMetaVar2SizeOffset) != after_edl.size() ||
        get_u32(after_props, props_at) != after_edl.size()) {
        throw Error(function + ": the written size words are not the new CEdl "
                    "Var2Data size");
    }

    /* No other stream moved; Props14 changes by the marker only. */
    for (std::string const& stream : opened.stream_paths()) {
        if (stream == kTaskMeta || stream == kEdlVar2 || stream == kEdlMeta ||
            stream == kViewProps || stream == kEditMarkerStream) {
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
