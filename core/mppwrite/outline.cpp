/*
 * outline.cpp -- the measured Indent / Outdent writer.  The rules and their
 * evidence are in outline.h; this file keeps the three layers apart:
 *
 *   - the ROW layer writes the FixedData words Project's own save moves;
 *   - the OUTLINE layer keeps Fixed2Data / Fixed2Meta parent identities, flags
 *     and the FixedMeta variable-field counts consistent with them;
 *   - the STORE layer edits the VarMeta/Var2Data summary marker (0x0B40007C),
 *     child list (0x0B4004E4) and leaf block (0x0B4004E5) with Project's
 *     in-place-shrink / append-growth rule.
 *
 * Calendar arithmetic (a summary's working-time duration and an outdent's
 * restored dates) is the caller's: core_mppwrite is standard-library only.
 */

#include "outline.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mppwrite {
namespace {

constexpr std::size_t kFixedMetaHeaderSize = 16;
constexpr std::size_t kFixedMetaEntrySize = 47;
constexpr std::size_t kFixedMetaFieldCount = 2;
constexpr std::size_t kFixedMetaRowOffset = 4;
constexpr std::size_t kFixedMetaSummaryByte = 26; /* bit 0x80 = summary */
constexpr std::uint8_t kFixedMetaFlagSummary = 0x80;
constexpr std::uint8_t kFixedMetaStateDeleted = 0x02;

constexpr std::size_t kFixed2RecordSize = 64;
constexpr std::size_t kFixed2RecordRowOffset = 24;
constexpr std::size_t kFixed2RecordFlags = 42;
constexpr std::size_t kFixed2RecordStartCopy = 46;
constexpr std::uint8_t kFixed2FlagSummary = 0x40;
constexpr std::uint8_t kFixed2FlagTopLevelLeaf = 0x30;
constexpr std::uint8_t kFixed2FlagChildLeaf = 0xA0;

constexpr std::size_t kFixed2MetaHeaderSize = 16;
constexpr std::size_t kFixed2MetaEntrySize = 96;
constexpr std::size_t kFixed2MetaLeafTail = 88;  /* 0x0c leaf / 0x00 summary */
constexpr std::size_t kFixed2MetaKind = 95;      /* 0x10 leaf / 0x08 summary */

constexpr std::size_t kHasChildrenByte = 140;    /* u16 at +140: 1 / 0 */
constexpr std::size_t kParentRowByte = 142;      /* u16 at +142: parent row id */
constexpr std::size_t kOutlineLevelWord = 172;   /* u16 +172: 1-based level */
constexpr std::uint16_t kSummaryDurationFormat = 0x15;

/* The three VarMeta fields an outline edit owns.  0x0B40007C is the summary
 * marker (original Duration, original DurationFormat, 0); 0x0B4004E4 is the
 * per-summary child row list (count, row ids ascending); 0x0B4004E5 is the
 * constant leaf block, and 0x0B400505/0x0B400506 are the two 2-byte leaf
 * fields Project drops when a leaf becomes a summary (see actuals.h, which
 * measured the same fields on percent edits). */
constexpr std::uint32_t kMarkerField = 0x0B40007Cu;
constexpr std::uint32_t kChildListField = 0x0B4004E4u;
constexpr std::uint32_t kLeafField = 0x0B4004E5u;
constexpr std::uint32_t kLeafExtraA = 0x0B400505u;
constexpr std::uint32_t kLeafExtraB = 0x0B400506u;

/* The constant 0x0B4004E5 payload (measured on every leaf row of the pairs). */
constexpr std::uint8_t kLeafBlock[8] = {0x01, 0x00, 0x00, 0x00,
                                        0x01, 0x00, 0xFF, 0xFF};

constexpr std::uint32_t kProjectUid = 0;

std::string table_stream(std::string_view table, std::string_view name) {
    return std::string(table) + "/" + std::string(name);
}

std::uint16_t get_u16(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}

std::uint32_t get_u32(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

void put_u16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

void put_u32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

using RowId = std::array<std::uint8_t, 16>;

bool empty_id(RowId const& id) {
    return std::all_of(id.begin(), id.end(), [](std::uint8_t byte) {
        return byte == 0;
    });
}

/* FixedMeta entry that names this FixedData row, or SIZE_MAX. */
std::size_t meta_entry_for_row(std::vector<std::uint8_t> const& meta,
                               std::size_t row) {
    std::uint32_t const offset =
        static_cast<std::uint32_t>(task_row_offset(row));
    for (std::size_t i = 0;
         i < (meta.size() - kFixedMetaHeaderSize) / kFixedMetaEntrySize; ++i) {
        std::size_t const base =
            kFixedMetaHeaderSize + i * kFixedMetaEntrySize;
        if (get_u32(meta, base + kFixedMetaRowOffset) == offset) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

/* Project's VarMeta has a LOGICAL count in header +8 and a possibly longer
 * physical array; the writer edits only the logical entries and copies them
 * back into the first slots of the original stream (the rule measured in
 * actuals.cpp, W28-1.1; duplicated here because that helper is file-local). */
std::vector<std::uint8_t> merge_logical_var_meta(
    std::vector<std::uint8_t> const& original,
    std::vector<std::uint8_t> const& logical) {
    if (logical.size() >= original.size()) {
        return logical;
    }
    std::vector<std::uint8_t> out = original;
    std::copy(logical.begin() + static_cast<std::ptrdiff_t>(kVarMetaHeaderSize),
              logical.end(),
              out.begin() + static_cast<std::ptrdiff_t>(kVarMetaHeaderSize));
    std::copy(logical.begin(),
              logical.begin() + static_cast<std::ptrdiff_t>(kVarMetaHeaderSize),
              out.begin());
    return out;
}

struct State {
    State(CfbDocument const& doc, std::string_view table);
    State(State const&) = delete;
    State& operator=(State const&) = delete;

    TaskTable rows;
    std::string table_name;
    std::string meta_path;
    std::string fixed2_path;
    std::string fixed2_meta_path;
    std::string var_meta_path;
    std::string var2_path;
    std::vector<std::uint8_t> fixed2;
    std::vector<std::uint8_t> fixed2_meta;
    std::vector<std::uint8_t> meta;
    std::vector<std::uint8_t> original_var_meta;
    std::vector<std::uint8_t> var_meta;
    std::vector<std::uint8_t> var2;
    std::size_t row_count = 0;
};

State::State(CfbDocument const& doc, std::string_view table)
    : rows(TaskTable::from_document(doc, table)),
      table_name(table),
      meta_path(table_stream(table, "FixedMeta")),
      fixed2_path(table_stream(table, "Fixed2Data")),
      fixed2_meta_path(table_stream(table, "Fixed2Meta")),
      var_meta_path(table_stream(table, "VarMeta")),
      var2_path(table_stream(table, "Var2Data")) {
    row_count = rows.rows();
    if (row_count == 0) {
        throw Error("the task table has no project row");
    }
    fixed2 = doc.read_stream(fixed2_path);
    fixed2_meta = doc.read_stream(fixed2_meta_path);
    meta = doc.read_stream(meta_path);
    original_var_meta = doc.read_stream(var_meta_path);
    var_meta = original_var_meta;
    var2 = doc.read_stream(var2_path);

    if (fixed2.size() != kFixed2RecordSize * (row_count + 3)) {
        throw Error("Fixed2Data is " + std::to_string(fixed2.size()) +
                    " bytes, not 64*(rows+3)");
    }
    if (meta.size() < kFixedMetaHeaderSize ||
        (meta.size() - kFixedMetaHeaderSize) % kFixedMetaEntrySize != 0) {
        throw Error("FixedMeta is " + std::to_string(meta.size()) +
                    " bytes, not 16 + 47*n");
    }
    if (fixed2_meta.size() < kFixed2MetaHeaderSize ||
        (fixed2_meta.size() - kFixed2MetaHeaderSize) %
                kFixed2MetaEntrySize != 0) {
        throw Error("Fixed2Meta is " + std::to_string(fixed2_meta.size()) +
                    " bytes, not 16 + 96*n");
    }
    if (var_meta.size() < kVarMetaHeaderSize ||
        (var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
        throw Error("VarMeta is " + std::to_string(var_meta.size()) +
                    " bytes, not 24 + 12*n");
    }
    std::size_t const physical_entries =
        (var_meta.size() - kVarMetaHeaderSize) / kVarMetaEntrySize;
    std::size_t const declared = get_u32(var_meta, 8);
    if (declared > physical_entries) {
        throw Error("VarMeta declares " + std::to_string(declared) +
                    " entries but holds only " +
                    std::to_string(physical_entries));
    }
    (void)read_var_entries(var_meta);
    var_meta.resize(kVarMetaHeaderSize + declared * kVarMetaEntrySize);
}

bool find_row(State const& state, std::uint32_t uid, std::size_t& row) {
    for (std::size_t candidate = 1; candidate < state.row_count; ++candidate) {
        if (state.rows.uid(candidate) == uid) {
            row = candidate;
            return true;
        }
    }
    return false;
}

bool row_is_deleted(State const& state, std::size_t row) {
    std::size_t const index = meta_entry_for_row(state.meta, row);
    if (index == static_cast<std::size_t>(-1)) {
        return false;
    }
    std::size_t const base =
        kFixedMetaHeaderSize + index * kFixedMetaEntrySize;
    return state.meta[base] == kFixedMetaStateDeleted;
}

RowId row_identity(State const& state, std::size_t row) {
    RowId id{};
    std::size_t const at = kFixed2RecordSize * (row + 3);
    std::copy(state.fixed2.begin() + static_cast<std::ptrdiff_t>(at),
              state.fixed2.begin() + static_cast<std::ptrdiff_t>(at + 16),
              id.begin());
    return id;
}

RowId row_parent_identity(State const& state, std::size_t row) {
    RowId id{};
    std::size_t const at =
        kFixed2RecordSize * (row + 3) + kFixed2RecordRowOffset;
    std::copy(state.fixed2.begin() + static_cast<std::ptrdiff_t>(at),
              state.fixed2.begin() + static_cast<std::ptrdiff_t>(at + 16),
              id.begin());
    return id;
}

void set_row_parent(State& state, std::size_t row, RowId const& parent) {
    std::size_t const at =
        kFixed2RecordSize * (row + 3) + kFixed2RecordRowOffset;
    std::copy(parent.begin(), parent.end(),
              state.fixed2.begin() + static_cast<std::ptrdiff_t>(at));
}

void set_row_flags(State& state, std::size_t row, std::uint8_t flags) {
    state.fixed2[kFixed2RecordSize * (row + 3) + kFixed2RecordFlags] = flags;
}

void set_row_start_copy(State& state, std::size_t row, std::uint32_t value) {
    put_u32(state.fixed2,
            kFixed2RecordSize * (row + 3) + kFixed2RecordStartCopy, value);
}

std::size_t fixed2_meta_entry_for_row(State const& state, std::size_t row) {
    std::size_t const index = meta_entry_for_row(state.meta, row);
    if (index == static_cast<std::size_t>(-1)) {
        throw Error("FixedMeta has no entry for FixedData row " +
                    std::to_string(row));
    }
    if (kFixed2MetaHeaderSize + (index + 1) * kFixed2MetaEntrySize >
        state.fixed2_meta.size()) {
        throw Error("Fixed2Meta has no entry for FixedMeta entry " +
                    std::to_string(index) + " (FixedData row " +
                    std::to_string(row) + ")");
    }
    return index;
}

void set_fixed2_meta_byte(State& state, std::size_t row, std::size_t offset,
                          std::uint8_t value) {
    std::size_t const index = fixed2_meta_entry_for_row(state, row);
    state.fixed2_meta[kFixed2MetaHeaderSize +
                      index * kFixed2MetaEntrySize + offset] = value;
}

void adjust_field_count(State& state, std::size_t row, std::int64_t delta) {
    std::size_t const index = meta_entry_for_row(state.meta, row);
    if (index == static_cast<std::size_t>(-1)) {
        throw Error("FixedMeta has no entry for FixedData row " +
                    std::to_string(row));
    }
    std::size_t const base =
        kFixedMetaHeaderSize + index * kFixedMetaEntrySize;
    std::int64_t const old =
        static_cast<std::int64_t>(get_u16(state.meta, base + kFixedMetaFieldCount));
    std::int64_t const next = old + delta;
    if (next < 0 || next > 0xFFFF) {
        throw Error("FixedMeta field count for FixedData row " +
                    std::to_string(row) + " would become " +
                    std::to_string(next));
    }
    put_u16(state.meta, base + kFixedMetaFieldCount,
            static_cast<std::uint16_t>(next));
}

/* The FixedMeta entry's +26 bit 0x80 is Project's stored "this row is a
 * summary" flag.  MEASURED in all three pairs: every summary row in the
 * controls (project row excluded, which stores 0) carries it; an indent sets it
 * on the new summary; an outdent LEAVES it set on the restored leaf (the same
 * way the outdent leaves Fixed2Meta +88 at 0x00).  The entry's 16-byte blob at
 * +8 is opaque per-save data Project rewrites on the rows it touches; the
 * writer leaves it alone and the save-pair test masks it on the edited row. */
void set_fixed_meta_summary(State& state, std::size_t row) {
    std::size_t const index = meta_entry_for_row(state.meta, row);
    if (index == static_cast<std::size_t>(-1)) {
        throw Error("FixedMeta has no entry for FixedData row " +
                    std::to_string(row));
    }
    state.meta[kFixedMetaHeaderSize + index * kFixedMetaEntrySize +
               kFixedMetaSummaryByte] |= kFixedMetaFlagSummary;
}

std::vector<std::uint32_t> decode_child_list(
    std::vector<std::uint8_t> const& payload) {    if (payload.size() < 4) {
        throw Error("the 0x0B4004E4 child list is shorter than its count word");
    }
    std::uint32_t const count = get_u32(payload, 0);
    if (payload.size() != 4u + 4u * static_cast<std::size_t>(count)) {
        throw Error("the 0x0B4004E4 child list is " +
                    std::to_string(payload.size()) + " bytes for " +
                    std::to_string(count) + " children (expected 4 + 4*n)");
    }
    std::vector<std::uint32_t> ids;
    ids.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        ids.push_back(get_u32(payload, 4u + 4u * i));
    }
    return ids;
}

std::vector<std::uint8_t> encode_child_list(
    std::vector<std::uint32_t> const& ids) {
    std::vector<std::uint8_t> out(4 + 4 * ids.size(), 0);
    put_u32(out, 0, static_cast<std::uint32_t>(ids.size()));
    for (std::size_t i = 0; i < ids.size(); ++i) {
        put_u32(out, 4 + 4 * i, ids[i]);
    }
    return out;
}

std::vector<std::uint8_t> encode_marker(std::uint32_t duration,
                                        std::uint16_t format) {
    std::vector<std::uint8_t> out(12, 0);
    put_u32(out, 0, duration);
    put_u32(out, 4, format);
    return out;
}

/* The marker's third word is zero in every measured file; a non-zero value is
 * a shape this writer has not measured. */
OutlineMarker decode_marker(std::vector<std::uint8_t> const& payload) {
    if (payload.size() != 12) {
        throw Error("the 0x0B40007C summary marker is " +
                    std::to_string(payload.size()) +
                    " bytes, not the measured 12");
    }
    if (get_u32(payload, 8) != 0) {
        throw Error("the 0x0B40007C summary marker has a non-zero reserved "
                    "word");
    }
    OutlineMarker marker;
    marker.present = true;
    marker.duration_ticks = get_u32(payload, 0);
    marker.duration_format = static_cast<std::uint16_t>(get_u32(payload, 4));
    return marker;
}

/* Remove every logical VarMeta entry for (uid, key); the block stays in
 * Var2Data as unreferenced bytes, exactly as Project leaves it. */
std::size_t remove_var_entries(std::vector<std::uint8_t>& var_meta,
                               std::uint32_t uid, std::uint32_t field_id) {
    std::vector<VarEntry> const entries = read_var_entries(var_meta);
    std::size_t removed = 0;
    for (std::size_t i = entries.size(); i-- > 0;) {
        if (entries[i].mid == uid && entries[i].field_id == field_id) {
            std::size_t const at = kVarMetaHeaderSize + i * kVarMetaEntrySize;
            var_meta.erase(
                var_meta.begin() + static_cast<std::ptrdiff_t>(at),
                var_meta.begin() +
                    static_cast<std::ptrdiff_t>(at + kVarMetaEntrySize));
            ++removed;
        }
    }
    return removed;
}

/* Project's block rule (measured on all three pairs): an existing block that
 * can hold the new payload is rewritten in place -- length prefix updated and
 * the old trailing bytes left behind; a bigger payload appends a new block at
 * the current end and repoints the entry.  A missing entry is appended at the
 * end and inserted into its uid group in key order. */
void set_var_block(State& state, std::uint32_t uid, std::uint32_t field_id,
                   std::vector<std::uint8_t> const& payload,
                   std::size_t& added) {
    std::vector<std::uint32_t> const offsets =
        find_var_offsets(state.var_meta, field_id, uid);
    if (offsets.empty()) {
        std::uint32_t const offset = static_cast<std::uint32_t>(state.var2.size());
        std::vector<std::uint8_t> const block = encode_block(payload);
        state.var2.insert(state.var2.end(), block.begin(), block.end());
        insert_var_entry(state.var_meta, uid, field_id, offset);
        ++added;
        return;
    }
    std::uint32_t const at = offsets.front();
    std::vector<std::uint8_t> const old = read_var_block(state.var2, at);
    std::uint32_t target = at;
    if (payload.size() <= old.size()) {
        put_u32(state.var2, at, static_cast<std::uint32_t>(payload.size()));
        std::copy(payload.begin(), payload.end(),
                  state.var2.begin() + static_cast<std::ptrdiff_t>(at + 4));
    } else {
        target = static_cast<std::uint32_t>(state.var2.size());
        std::vector<std::uint8_t> const block = encode_block(payload);
        state.var2.insert(state.var2.end(), block.begin(), block.end());
    }
    /* A duplicate (uid, key) entry is not a shape any measured file has, but
     * keeping every matching entry on the same block is safer than editing one
     * and leaving a stale offset behind. */
    std::vector<VarEntry> const entries = read_var_entries(state.var_meta);
    for (VarEntry const& entry : entries) {
        if (entry.mid == uid && entry.field_id == field_id) {
            put_u32(state.var_meta,
                    kVarMetaHeaderSize + entry.index * kVarMetaEntrySize + 4,
                    target);
        }
    }
}

bool stored_marker(State& state, std::uint32_t uid,
                   std::vector<std::uint8_t>& payload) {
    std::vector<std::uint32_t> const offsets =
        find_var_offsets(state.var_meta, kMarkerField, uid);
    if (offsets.empty()) {
        return false;
    }
    if (offsets.size() != 1) {
        throw Error("task uid " + std::to_string(uid) +
                    " has more than one 0x0B40007C summary marker");
    }
    payload = read_var_block(state.var2, offsets.front());
    return true;
}

bool stored_child_list(State& state, std::uint32_t uid,
                       std::vector<std::uint8_t>& payload) {
    std::vector<std::uint32_t> const offsets =
        find_var_offsets(state.var_meta, kChildListField, uid);
    if (offsets.empty()) {
        return false;
    }
    if (offsets.size() != 1) {
        throw Error("task uid " + std::to_string(uid) +
                    " has more than one 0x0B4004E4 child list");
    }
    payload = read_var_block(state.var2, offsets.front());
    return true;
}

/* The live rows of the current (in-memory) state, with parent links resolved
 * against the 16-byte Fixed2Data identities.  Siblings are ordered by the
 * FixedData +0 row id, as core/mpp's reader does. */
std::vector<OutlineRow> build_rows(State& state) {
    std::vector<OutlineRow> out(state.row_count);
    std::map<RowId, std::size_t> row_by_identity;
    for (std::size_t row = 0; row < state.row_count; ++row) {
        OutlineRow& info = out[row];
        info.row = row;
        info.uid = state.rows.uid(row);
        info.row_id = state.rows.u32(row, task_row::kRowId);
        info.level = state.rows.u16(row, kOutlineLevelWord);
        info.duration = state.rows.u32(row, task_row::kDuration);
        info.duration_format = state.rows.u16(row, task_row::kDurationFormat);
        info.start = state.rows.u32(row, task_row::kStart2);
        info.finish = state.rows.u32(row, task_row::kFinish2);
        info.deleted = row_is_deleted(state, row);
        info.stored_children = state.rows.u16(row, kHasChildrenByte) != 0;
        info.fixed2_flags =
            state.fixed2[kFixed2RecordSize * (row + 3) + kFixed2RecordFlags];
        RowId const id = row_identity(state, row);
        if (!empty_id(id)) {
            row_by_identity.emplace(id, row);
        }
        std::vector<std::uint8_t> payload;
        if (stored_child_list(state, info.uid, payload)) {
            info.stored_child_ids = decode_child_list(payload);
        }
    }
    for (std::size_t row = 1; row < state.row_count; ++row) {
        RowId const parent = row_parent_identity(state, row);
        if (empty_id(parent)) {
            continue;
        }
        auto const found = row_by_identity.find(parent);
        if (found == row_by_identity.end() || found->second == row) {
            continue;
        }
        out[row].parent_row = found->second;
        out[found->second].child_rows.push_back(row);
    }
    for (OutlineRow& info : out) {
        std::sort(info.child_rows.begin(), info.child_rows.end(),
                  [&out](std::size_t left, std::size_t right) {
                      return out[left].row_id < out[right].row_id;
                  });
    }
    return out;
}

void collect_subtree(std::vector<OutlineRow> const& rows, std::size_t row,
                     std::vector<std::size_t>& out,
                     std::vector<bool>& visiting) {
    if (visiting[row]) {
        throw Error("the outline parent links form a cycle at FixedData row " +
                    std::to_string(row));
    }
    visiting[row] = true;
    out.push_back(row);
    for (std::size_t child : rows[row].child_rows) {
        collect_subtree(rows, child, out, visiting);
    }
    visiting[row] = false;
}

std::string task_label(std::uint32_t uid) {
    return "task uid " + std::to_string(uid);
}

bool apply_indent(State& state, OutlineEdit const& edit,
                  std::vector<OutlineRow> const& rows, std::size_t row,
                  std::string& warning) {
    OutlineRow const& moved = rows[row];
    if (moved.level != 1) {
        warning = task_label(edit.uid) +
                  " is not a top-level task; the measured indent moves a"
                  " top-level row and this shape is not measured";
        return false;
    }
    if (moved.parent_row != 0) {
        warning = task_label(edit.uid) +
                  " is not stored under the project row; the measured indent"
                  " moves a top-level row";
        return false;
    }
    std::vector<std::size_t> const& project_children = rows[0].child_rows;
    auto const position =
        std::find(project_children.begin(), project_children.end(), row);
    if (position == project_children.end() || position == project_children.begin()) {
        warning = task_label(edit.uid) +
                  " has no preceding sibling to become its summary; the"
                  " measured indent needs one";
        return false;
    }
    std::size_t const parent = *(position - 1);
    OutlineRow const& summary = rows[parent];
    if (!summary.child_rows.empty() || summary.stored_children ||
        (summary.fixed2_flags & kFixed2FlagSummary) != 0) {
        warning = task_label(edit.uid) + ": the preceding sibling (task uid " +
                  std::to_string(summary.uid) +
                  ") is already a summary; indenting under a summary is not"
                  " measured";
        return false;
    }
    if (summary.deleted) {
        warning = task_label(edit.uid) + ": the preceding sibling (task uid " +
                  std::to_string(summary.uid) +
                  ") is a deleted row; that shape is not measured";
        return false;
    }
    if (summary.row_id > 0xFFFFu) {
        warning = task_label(edit.uid) + ": the new summary's row id " +
                  std::to_string(summary.row_id) +
                  " does not fit the u16 +142 parent-row slot";
        return false;
    }
    std::vector<std::uint8_t> marker_payload;
    if (stored_marker(state, summary.uid, marker_payload)) {
        warning = task_label(edit.uid) + ": the preceding sibling (task uid " +
                  std::to_string(summary.uid) +
                  ") already carries a 0x0B40007C summary marker; that shape"
                  " is not measured";
        return false;
    }
    std::vector<std::uint8_t> child_payload;
    if (stored_child_list(state, summary.uid, child_payload)) {
        warning = task_label(edit.uid) + ": the preceding sibling (task uid " +
                  std::to_string(summary.uid) +
                  ") already carries a 0x0B4004E4 child list; that shape is"
                  " not measured";
        return false;
    }
    if (state.rows.u16(parent, kHasChildrenByte) != 0) {
        throw Error("FixedData row " + std::to_string(parent) +
                    " has the has-children byte set but no children");
    }

    std::vector<std::size_t> subtree;
    std::vector<bool> visiting(state.row_count, false);
    collect_subtree(rows, row, subtree, visiting);
    std::uint32_t earliest = kInvalidDate;
    std::uint32_t latest = kInvalidDate;
    for (std::size_t member : subtree) {
        if (!is_date(rows[member].start) || !is_date(rows[member].finish)) {
            warning = task_label(edit.uid) +
                      ": the moved subtree has a row without a valid start or"
                      " finish, so the summary dates cannot be written";
            return false;
        }
        if (!is_date(earliest) || rows[member].start < earliest) {
            earliest = rows[member].start;
        }
        if (!is_date(latest) || rows[member].finish > latest) {
            latest = rows[member].finish;
        }
    }

    /* Validate the project row's child list before any write: this edit only
     * shrinks that payload, so Project rewrites it in place. */
    std::vector<std::uint8_t> project_children_payload;
    if (!stored_child_list(state, kProjectUid, project_children_payload)) {
        warning = task_label(edit.uid) +
                  ": the project row has no 0x0B4004E4 child list, so the"
                  " outline edit is not measured on this file";
        return false;
    }
    std::vector<std::uint32_t> project_ids =
        decode_child_list(project_children_payload);
    auto const project_entry =
        std::find(project_ids.begin(), project_ids.end(), rows[row].row_id);
    if (project_entry == project_ids.end()) {
        warning = task_label(edit.uid) +
                  ": the project row's child list does not name the moved row";
        return false;
    }
    project_ids.erase(project_entry);
    if (project_ids.empty()) {
        warning = task_label(edit.uid) +
                  ": the project row would have no children after the indent;"
                  " that shape is not measured";
        return false;
    }

    /* Capture the leaf's marker values before the roll-up overwrites them. */
    std::uint32_t const original_duration = summary.duration;
    std::uint16_t const original_format = summary.duration_format;

    /* The moved root and its descendants gain one level.  +142 is the u16
     * parent-row slot (its measured values all fit the low byte, but the word
     * is what Project stores; the project row itself holds 0xffff). */
    for (std::size_t member : subtree) {
        std::uint32_t const level = rows[member].level;
        if (level >= 0xFFFFu) {
            throw Error("task uid " + std::to_string(rows[member].uid) +
                        " has an outline level that cannot grow");
        }
        state.rows.set_u16(member, kOutlineLevelWord,
                           static_cast<std::uint16_t>(level + 1));
    }
    state.rows.set_u16(row, kParentRowByte,
                       static_cast<std::uint16_t>(summary.row_id));

    state.rows.set_u32(parent, task_row::kDuration, edit.summary_duration);
    state.rows.set_u32(parent, task_row::kStart, earliest);
    state.rows.set_u32(parent, task_row::kFinish, latest);
    state.rows.set_u16(parent, kHasChildrenByte, 1);
    state.rows.set_u16(parent, task_row::kDurationFormat,
                       kSummaryDurationFormat);
    state.rows.set_u32(parent, task_row::kStartAnchor, kInvalidDate);

    set_row_flags(state, parent, kFixed2FlagSummary);
    set_row_start_copy(state, parent, kInvalidDate);
    set_fixed2_meta_byte(state, parent, kFixed2MetaLeafTail, 0x00);
    set_fixed2_meta_byte(state, parent, kFixed2MetaKind, 0x08);
    set_fixed_meta_summary(state, parent);

    set_row_parent(state, row, row_identity(state, parent));
    set_row_flags(state, row, rows[row].child_rows.empty()
                                  ? kFixed2FlagChildLeaf
                                  : kFixed2FlagSummary);

    std::size_t added = 0;
    std::size_t removed = 0;
    removed += remove_var_entries(state.var_meta, summary.uid, kLeafField);
    removed += remove_var_entries(state.var_meta, summary.uid, kLeafExtraA);
    removed += remove_var_entries(state.var_meta, summary.uid, kLeafExtraB);
    set_var_block(state, summary.uid, kMarkerField,
                  encode_marker(original_duration, original_format), added);
    set_var_block(state, summary.uid, kChildListField,
                  encode_child_list({rows[row].row_id}), added);

    std::size_t project_added = 0;
    set_var_block(state, kProjectUid, kChildListField,
                  encode_child_list(project_ids), project_added);
    added += project_added;

    adjust_field_count(state, parent,
                       static_cast<std::int64_t>(added) -
                           static_cast<std::int64_t>(removed));
    return true;
}

bool apply_outdent(State& state, OutlineEdit const& edit,
                   std::vector<OutlineRow> const& rows, std::size_t row,
                   std::string& warning) {
    OutlineRow const& moved = rows[row];
    if (moved.level < 2) {
        warning = task_label(edit.uid) +
                  " is already at the top level and cannot be outdented";
        return false;
    }
    if (!moved.child_rows.empty()) {
        warning = task_label(edit.uid) +
                  " is a summary; the measured outdent moves a leaf";
        return false;
    }
    if (moved.parent_row == static_cast<std::size_t>(-1) ||
        moved.parent_row == 0) {
        warning = task_label(edit.uid) +
                  " has no parent summary in the stored outline";
        return false;
    }
    std::size_t const parent = moved.parent_row;
    OutlineRow const& summary = rows[parent];
    if (summary.child_rows.size() != 1 || summary.child_rows.front() != row) {
        warning = task_label(edit.uid) +
                  ": its parent has another child; outdenting from a parent"
                  " that stays a summary is not measured";
        return false;
    }
    if (!summary.stored_children) {
        throw Error("FixedData row " + std::to_string(parent) +
                    " is a summary through the Fixed2Data links but its"
                    " has-children byte is clear");
    }
    std::vector<std::uint8_t> marker_payload;
    if (!stored_marker(state, summary.uid, marker_payload)) {
        warning = task_label(edit.uid) + ": the parent summary (task uid " +
                  std::to_string(summary.uid) +
                  ") has no 0x0B40007C marker, so its restored duration is"
                  " unknown";
        return false;
    }
    OutlineMarker const marker = decode_marker(marker_payload);
    if (marker.duration_ticks != edit.restored_duration ||
        marker.duration_format != edit.restored_duration_format) {
        warning = task_label(edit.uid) +
                  ": the stored summary marker does not match the queued"
                  " restore values (the file changed after the edit was made)";
        return false;
    }
    if (!is_date(edit.restored_start) || !is_date(edit.restored_finish)) {
        warning = task_label(edit.uid) +
                  ": the restored start/finish are not valid dates";
        return false;
    }
    if (edit.restored_finish < edit.restored_start) {
        warning = task_label(edit.uid) +
                  ": the restored finish is before the restored start";
        return false;
    }
    std::uint32_t const parent_start =
        state.rows.u32(parent, task_row::kStart2);
    if (!is_date(parent_start)) {
        warning = task_label(edit.uid) +
                  ": the parent summary has no valid effective start";
        return false;
    }
    std::vector<std::uint8_t> parent_children_payload;
    if (!stored_child_list(state, summary.uid, parent_children_payload)) {
        warning = task_label(edit.uid) + ": the parent summary (task uid " +
                  std::to_string(summary.uid) +
                  ") has no 0x0B4004E4 child list";
        return false;
    }
    std::vector<std::uint32_t> const parent_children =
        decode_child_list(parent_children_payload);
    if (parent_children.size() != 1 ||
        parent_children.front() != moved.row_id) {
        warning = task_label(edit.uid) +
                  ": the parent's child list does not name exactly the moved"
                  " row, so the parent's restored leaf shape is not measured";
        return false;
    }
    if (summary.parent_row == static_cast<std::size_t>(-1)) {
        warning = task_label(edit.uid) +
                  ": the parent summary is not linked to a parent row";
        return false;
    }
    std::uint32_t const new_parent_row_id =
        state.rows.u32(summary.parent_row, task_row::kRowId);
    if (new_parent_row_id > 0xFFFFu) {
        warning = task_label(edit.uid) + ": the new parent's row id " +
                  std::to_string(new_parent_row_id) +
                  " does not fit the u16 +142 parent-row slot";
        return false;
    }
    std::vector<std::uint8_t> project_children_payload;
    if (!stored_child_list(state, kProjectUid, project_children_payload)) {
        warning = task_label(edit.uid) +
                  ": the project row has no 0x0B4004E4 child list";
        return false;
    }
    std::vector<std::uint32_t> project_ids =
        decode_child_list(project_children_payload);
    if (std::find(project_ids.begin(), project_ids.end(), moved.row_id) !=
        project_ids.end()) {
        warning = task_label(edit.uid) +
                  ": the project row already names the moved row as a child";
        return false;
    }
    project_ids.push_back(moved.row_id);
    std::sort(project_ids.begin(), project_ids.end());

    std::uint32_t const new_level = moved.level - 1;
    state.rows.set_u32(parent, task_row::kDuration, edit.restored_duration);
    state.rows.set_u32(parent, task_row::kStart, edit.restored_start);
    state.rows.set_u32(parent, task_row::kFinish, edit.restored_finish);
    state.rows.set_u16(parent, kHasChildrenByte, 0);
    state.rows.set_u16(parent, task_row::kDurationFormat,
                       edit.restored_duration_format);
    state.rows.set_u32(parent, task_row::kStartAnchor, parent_start);

    state.rows.set_u16(row, kOutlineLevelWord,
                       static_cast<std::uint16_t>(new_level));
    state.rows.set_u16(row, kParentRowByte,
                       static_cast<std::uint16_t>(new_parent_row_id));

    set_row_flags(state, parent, summary.level == 1
                                     ? kFixed2FlagTopLevelLeaf
                                     : kFixed2FlagChildLeaf);
    set_row_start_copy(state, parent, parent_start);
    set_fixed2_meta_byte(state, parent, kFixed2MetaKind, 0x10);

    set_row_parent(state, row, row_identity(state, summary.parent_row));
    set_row_flags(state, row, new_level == 1 ? kFixed2FlagTopLevelLeaf
                                             : kFixed2FlagChildLeaf);

    std::size_t added = 0;
    std::size_t removed = 0;
    removed += remove_var_entries(state.var_meta, summary.uid, kChildListField);
    /* Project appends the project's grown child list first, then the restored
     * leaf's 0x0B4004E5 block (measured: offsets 548 then 568). */
    std::size_t project_added = 0;
    set_var_block(state, kProjectUid, kChildListField,
                  encode_child_list(project_ids), project_added);
    added += project_added;
    if (find_var_offsets(state.var_meta, kLeafField, summary.uid).empty()) {
        set_var_block(state, summary.uid, kLeafField,
                      std::vector<std::uint8_t>(std::begin(kLeafBlock),
                                                std::end(kLeafBlock)),
                      added);
    }

    adjust_field_count(state, parent,
                       static_cast<std::int64_t>(added) -
                           static_cast<std::int64_t>(removed));
    return true;
}

} /* namespace */

OutlineMarker read_outline_marker(CfbDocument const& doc, std::uint32_t uid,
                                  std::string_view table) {
    try {
        std::string const var_meta_path = table_stream(table, "VarMeta");
        std::string const var2_path = table_stream(table, "Var2Data");
        if (!doc.has_stream(var_meta_path) || !doc.has_stream(var2_path)) {
            return {};
        }
        std::vector<std::uint8_t> const var_meta = doc.read_stream(var_meta_path);
        std::vector<std::uint8_t> const var2 = doc.read_stream(var2_path);
        if (var_meta.size() < kVarMetaHeaderSize ||
            (var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
            return {};
        }
        std::vector<std::uint32_t> const offsets =
            find_var_offsets(var_meta, kMarkerField, uid);
        if (offsets.empty()) {
            return {};
        }
        return decode_marker(read_var_block(var2, offsets.front()));
    } catch (Error const&) {
        return {};
    }
}

bool read_outline_rows(CfbDocument const& doc, std::string_view table,
                       std::vector<OutlineRow>& rows, std::string& error) {
    try {
        State state(doc, table);
        rows = build_rows(state);
        error.clear();
        return true;
    } catch (Error const& caught) {
        rows.clear();
        error = caught.what();
        return false;
    }
}

OutlineWriteResult write_outline_edits(CfbDocument& doc,
                                       std::vector<OutlineEdit> const& edits,
                                       std::string_view table) {
    OutlineWriteResult result;
    State state(doc, table);
    for (OutlineEdit const& edit : edits) {
        if (edit.uid == 0) {
            result.warnings.push_back(
                "uid 0 is the project row, not a task that can be indented");
            continue;
        }
        std::size_t row = 0;
        if (!find_row(state, edit.uid, row)) {
            result.warnings.push_back(
                task_label(edit.uid) +
                " is not in the saved task table, so its outline cannot be"
                " written");
            continue;
        }
        if (row_is_deleted(state, row)) {
            result.warnings.push_back(
                task_label(edit.uid) +
                " is a deleted row; its outline cannot be written");
            continue;
        }
        std::vector<OutlineRow> const rows = build_rows(state);
        std::string warning;
        bool const applied = edit.indent
                                 ? apply_indent(state, edit, rows, row, warning)
                                 : apply_outdent(state, edit, rows, row, warning);
        if (!applied) {
            result.warnings.push_back(std::move(warning));
            continue;
        }
        ++result.rows_written;
    }
    if (result.rows_written == 0) {
        return result;
    }

    std::size_t const entry_count = read_var_entries(state.var_meta).size();
    if (state.var_meta.size() !=
        kVarMetaHeaderSize + entry_count * kVarMetaEntrySize) {
        throw Error("VarMeta lost its 24 + 12*n shape while editing the "
                    "outline");
    }
    put_u32(state.var_meta, 8, static_cast<std::uint32_t>(entry_count));
    put_u32(state.var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(state.var2.size()));
    put_u32(state.meta, 0x0C,
            static_cast<std::uint32_t>(state.rows.byte_size()));
    std::vector<std::uint8_t> const final_var_meta =
        merge_logical_var_meta(state.original_var_meta, state.var_meta);

    state.rows.store_into(doc, table);
    doc.replace_stream(state.meta_path, state.meta);
    doc.replace_stream(state.fixed2_path, state.fixed2);
    doc.replace_stream(state.fixed2_meta_path, state.fixed2_meta);
    doc.replace_stream(state.var2_path, state.var2);
    doc.replace_stream(state.var_meta_path, final_var_meta);

    std::string const inconsistent = check_table(doc, table);
    if (!inconsistent.empty()) {
        throw Error("the outline writer left the task table inconsistent: " +
                    inconsistent);
    }
    return result;
}

} /* namespace mppwrite */
