/*
 * viewstate_sort.cpp -- the Task Sheet sort persisted in the file.
 * W35-2.1.  The measured rules and the capability matrix are in
 * viewstate_sort.h; the pair derivation is in spec/writers/view-state.md.
 */

#include "viewstate_sort.h"

#include <algorithm>
#include <array>
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
/* W35b-1.1: the six-task and flat controls (outline-deep-level2, sort-flat-name-
 * desc) share a second text style; the Show Level pairs measure both. */
constexpr std::uint64_t kMeasuredBlockDigestAlt = 0x6f6082994d512927ull;
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
    /* W35b-1.4 (R35G2-4): the pairs measure Name in both directions and Duration
     * descending only, and the one ascending pair (Name) leaves the rows in the
     * order they were already in. */
    if (edit.keys.front().field == SortField::duration && edit.keys.front().ascending) {
        throw Error(function + ": Duration ascending is unmeasured (the pairs sort "
                    "Duration descending only), so this sort is refused");
    }
    if (edit.keys.front().ascending && edit.sorted_order != edit.base_order) {
        throw Error(function + ": an ascending sort that moves rows is unmeasured "
                    "(the one ascending pair leaves the rows in order), so this "
                    "sort is refused");
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
                          std::string const& function,
                          std::string const& refused = "this sort") {
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
                        "ascending), and replacing a sort is unmeasured, so " +
                        refused + " is refused");
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

/* The task rows' OutlineLevel word (FixedData +172, 1-based in the file). */
constexpr std::size_t kTaskOutlineLevel = 172;

/* The rows 1..N of the file's task table as the sort sees them.  The project row
 * (row 0) is never sorted. */
std::vector<SortRow> read_sort_rows(CfbDocument const& doc, TaskTable const& table,
                                    std::string const& function) {
    std::vector<SortRow> rows;
    for (std::size_t row = 1; row < table.rows(); ++row) {
        SortRow out;
        out.uid = table.uid(row);
        std::uint16_t const level = table.u16(row, kTaskOutlineLevel);
        if (level < 1) {
            throw Error(function + ": task row " + number(row) + " stores outline "
                        "level " + number(level) + "; levels are 1-based in the "
                        "file");
        }
        out.level = static_cast<int>(level) - 1;
        out.duration = table.u32(row, task_row::kDuration);
        out.name = read_task_name(doc, out.uid);
        rows.push_back(std::move(out));
    }
    return rows;
}

} /* namespace */

bool sort_name_pinned(std::string const& left, std::string const& right) {
    std::size_t const shared = std::min(left.size(), right.size());
    auto letter = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };
    for (std::size_t at = 0; at < shared; ++at) {
        if (left[at] == right[at]) {
            continue;
        }
        bool const left_lower = left[at] >= 'a' && left[at] <= 'z';
        bool const right_lower = right[at] >= 'a' && right[at] <= 'z';
        return letter(left[at]) && letter(right[at]) && left_lower == right_lower;
    }
    return false; /* equal, or one a prefix of the other */
}

std::string sorted_order_of(std::vector<SortRow> const& rows, std::uint32_t field_id,
                            bool ascending, std::vector<std::uint32_t>& sorted) {
    sorted.clear();
    bool const by_name = field_id == kSortFieldName;
    if (!by_name && field_id != kSortFieldDuration) {
        return "the measured Project pairs sort by Name or by Duration";
    }
    if (!by_name && ascending) {
        return "ascending Duration sorts are unmeasured: the measured Project pairs "
               "sort Duration descending only";
    }
    struct Node {
        SortRow const* row = nullptr;
        std::vector<std::size_t> children;
    };
    std::vector<Node> nodes(rows.size());
    std::vector<std::size_t> roots;
    std::vector<std::size_t> stack; /* the open ancestors */
    for (std::size_t index = 0; index < rows.size(); ++index) {
        nodes[index].row = &rows[index];
        int const level = rows[index].level;
        if (level < 0 || (level > 0 && static_cast<std::size_t>(level) > stack.size())) {
            return "this outline has a row deeper than its parent allows";
        }
        stack.resize(static_cast<std::size_t>(level));
        if (stack.empty()) {
            roots.push_back(index);
        } else {
            nodes[stack.back()].children.push_back(index);
        }
        stack.push_back(index);
    }
    /* W35b-1.3 (R35G2-3): the pairs measure one outline, a summary with two
     * children and then two leaves.  The flat-name-desc pair is the same five rows
     * with the Outline option off (its file stores the same levels), so it adds no
     * outline; any other nesting, a three-level one included, is unmeasured. */
    {
        constexpr std::array<int, kSortMeasuredRows> kMeasuredLevels{0, 1, 1, 0, 0};
        bool measured = rows.size() == kMeasuredLevels.size();
        for (std::size_t index = 0; measured && index < rows.size(); ++index) {
            measured = rows[index].level == kMeasuredLevels[index];
        }
        if (!measured) {
            std::string levels;
            for (SortRow const& row : rows) {
                levels += (levels.empty() ? "" : ", ") +
                          number(static_cast<std::size_t>(row.level + 1));
            }
            return "the measured Project pairs sort one outline (a summary with two "
                   "children, then two leaves: levels 1, 2, 2, 1, 1), and this one has "
                   "levels " + levels;
        }
    }
    std::string refusal;
    auto order_group = [&](std::vector<std::size_t>& group) {
        for (std::size_t a = 0; a < group.size() && refusal.empty(); ++a) {
            for (std::size_t b = a + 1; b < group.size() && refusal.empty(); ++b) {
                SortRow const& left = *nodes[group[a]].row;
                SortRow const& right = *nodes[group[b]].row;
                if (by_name) {
                    if (!sort_name_pinned(left.name, right.name)) {
                        refusal = "the measured Project pairs order sibling names that "
                                  "differ at a letter of the same case, and \"" +
                                  left.name + "\" / \"" + right.name + "\" do not";
                    }
                } else if (left.duration == right.duration) {
                    refusal = "equal durations among siblings: the measured Project "
                              "pairs have distinct sibling durations";
                }
            }
        }
        std::stable_sort(group.begin(), group.end(), [&](std::size_t a, std::size_t b) {
            SortRow const& left = *nodes[a].row;
            SortRow const& right = *nodes[b].row;
            bool const less = by_name ? left.name < right.name : left.duration < right.duration;
            bool const greater =
                by_name ? right.name < left.name : right.duration < left.duration;
            return ascending ? less : greater;
        });
    };
    order_group(roots);
    for (Node& node : nodes) {
        order_group(node.children);
    }
    if (!refusal.empty()) {
        return refusal;
    }
    std::vector<std::size_t> walk(roots.rbegin(), roots.rend());
    while (!walk.empty()) {
        std::size_t const at = walk.back();
        walk.pop_back();
        sorted.push_back(nodes[at].row->uid);
        for (auto child = nodes[at].children.rbegin(); child != nodes[at].children.rend();
             ++child) {
            walk.push_back(*child);
        }
    }
    /* W35b-1.4 (R35G2-4): the one ascending pair (Name) moves no row. */
    if (ascending) {
        for (std::size_t index = 0; index < rows.size(); ++index) {
            if (sorted[index] != rows[index].uid) {
                /* W35c-1.8 (R35bG1-1): build the reason before clearing `sorted`; it used to
                 * read `sorted[index]` after the clear. */
                std::string reason =
                    "an ascending sort that reorders rows is unmeasured: the "
                    "measured Project pair for an ascending key leaves the rows in "
                    "the order they were in, and this one would put UniqueID " +
                    number(sorted[index]) + " at row " + number(index + 1) +
                    " where the file has UniqueID " + number(rows[index].uid);
                sorted.clear();
                return reason;
            }
        }
    }
    return {};
}

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

void require_measured_view_block(CfbDocument const& doc, std::string const& function,
                                 std::string const& refused) {
    SortBlock const block = locate_block(doc, function);
    Bytes const var2 = doc.read_stream(std::string(kViewTable) + "/Var2Data");
    std::uint64_t const digest = sort_block_digest(var2, block.offset);
    if (digest != kMeasuredBlockDigest && digest != kMeasuredBlockDigestAlt) {
        throw Error(function + ": the view-state block is not one of the two "
                    "measured controls' outside its sort spec (digest mismatch: "
                    "a zoom, timescale or other view-state edit changed it), so "
                    "the view it sits in is unmeasured and " + refused +
                    " is refused");
    }
    require_default_spec(var2, block, function, refused);
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

    /* W35b-1.2 (R35G2-2): the request is the file's own.  The rows are the task
     * rows 1..N in order, and the sorted order is the one the file's outline, names
     * and durations give under the pinned rules, not whatever the caller holds. */
    TaskTable const table = TaskTable::from_document(doc);
    if (table.rows() != edit.base_order.size() + 1) {
        throw Error(function + ": the file holds " + number(table.rows()) +
                    " task rows (with the project row), not the " +
                    number(edit.base_order.size() + 1) + " the request describes");
    }
    for (std::size_t i = 0; i < edit.base_order.size(); ++i) {
        if (table.uid(i + 1) != edit.base_order[i]) {
            throw Error(function + ": task row " + number(i + 1) + " is UniqueID " +
                        number(table.uid(i + 1)) + ", not " +
                        number(edit.base_order[i]) + "; the request is not the "
                        "file's row order");
        }
    }
    std::vector<SortRow> const file_rows = read_sort_rows(doc, table, function);
    std::vector<std::uint32_t> from_file;
    if (std::string const reason = sorted_order_of(file_rows, field_id, ascending, from_file);
        !reason.empty()) {
        throw Error(function + ": " + reason + ", so this sort is refused");
    }
    if (from_file != edit.sorted_order) {
        throw Error(function + ": the sorted order does not match the order the file's "
                    "own outline, names and durations give, so this sort is refused");
    }

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
