/*
 * add_delete.cpp -- append one Task row, or tombstone a row.  See add_delete.h
 * for the measured design, the policies, and what is proved from disk.
 *
 * The shape of both entry points is the same as save.cpp's: validate the whole
 * request against the current document, apply the change to a copy, prove from
 * the copy that only the intended streams move, save, re-open from disk and
 * re-read every fact that was written.  A refusal happens before the output path
 * is touched.
 */

#include "add_delete.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mppwrite {
namespace {

constexpr std::size_t kFixedMetaHeaderSize = 16;
constexpr std::size_t kFixedMetaEntrySize = 47;
constexpr std::size_t kFixedMetaStateByte = 0;
constexpr std::size_t kFixedMetaFieldCount = 2;
constexpr std::size_t kFixedMetaRowOffset = 4;
/* FixedMeta HEADER word +4 is a table flags word.  Bit 0x02 marks "this table
 * contains tombstones": measured on Project's own files, 0x00000004 with no
 * deleted row (base, w10-adddel-base/add, PROJECT_A, synthetic-base) and 0x00000006
 * as soon as one row is tombstoned (w10-adddel-back/-del, PROJECT_B's 12 deleted
 * slots).  Project ignores the per-entry +0 = 0x02 tombstone without this header
 * bit -- the coordinator's read-only open of W45's first delete fixtures showed
 * the deleted rows still visible (3 tasks where 2 were expected). */
constexpr std::size_t kFixedMetaHeaderFlags = 4;
constexpr std::uint32_t kFixedMetaFlagHasDeleted = 0x02;

constexpr std::size_t kFixed2MetaHeaderSize = 16;
constexpr std::size_t kFixed2MetaEntrySize = 96;
constexpr std::size_t kFixed2MetaRecordOffset = 4;
constexpr std::size_t kFixed2MetaFlags = 8;
constexpr std::uint8_t kFixed2FlagsAuto = 0x7F;
constexpr std::uint8_t kFixed2FlagsManual = 0xFF;

constexpr std::size_t kFixed2RecordSize = 64;
constexpr std::size_t kFixed2RecordOrdinal = 16;
constexpr std::size_t kFixed2RecordFlags = 42;
constexpr std::uint8_t kFixed2FlagHasChildren = 0x40;

constexpr std::uint8_t kStateLive = 0x00;
constexpr std::uint8_t kStateDeleted = 0x02;

/* FixedMeta entry byte +2 is the row's variable-field count.  Measured on
 * Project's own add: 0x0008 for the new row, whose 8 VarMeta entries are the
 * ones in `kNewRowKeys` (w10-oracle §1).  It also matches every base.mpp row
 * (uid 1: 8 entries/8, uid 2: 10/10, the project row: 9/9). */
constexpr std::uint16_t kNewRowFieldCount = 8;

/* The 8 VarMeta keys a new task row gets, in Project's own order. */
constexpr std::array<std::uint32_t, 8> kNewRowKeys = {
    kTaskNameFieldId, /* 0x0b40000e Name */
    0x0B4000A4u, 0x0B4000A5u, 0x0B4000A8u, 0x0B4000A9u, 0x0B4000AAu,
    0x0B400563u, 0x0B400564u};

constexpr std::uint32_t kSecondaryUidFamily = 0x80000000u;

std::string table_stream(std::string_view table, std::string_view name) {
    return std::string(table) + "/" + std::string(name);
}

std::string row_label(std::size_t row, std::uint32_t uid) {
    return "row " + std::to_string(row) + " (uid " + std::to_string(uid) + ")";
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

void put_f64(std::vector<std::uint8_t>& b, std::size_t o, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof bits == sizeof value, "IEEE-754 double expected");
    std::memcpy(&bits, &value, sizeof bits);
    for (std::size_t i = 0; i < 8; ++i) {
        b[o + i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu);
    }
}

std::vector<std::uint8_t> read_file_bytes(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw Error("cannot open " + path + " after writing it");
    }
    input.seekg(0, std::ios::end);
    std::streamoff const length = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

/* Same policy as save.cpp: the parent directory must already exist; creating a
 * tree is the caller's decision, not a side effect of saving. */
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

/* One 47-byte FixedMeta entry, decoded far enough for this file. */
struct MetaEntry {
    std::size_t index = 0;
    std::uint32_t offset = 0; /* FixedData byte offset of the row it names */
    std::uint8_t state = 0;   /* +0: 0 = live, 0x02 = tombstone */
    std::uint16_t field_count = 0; /* +2: variable-field count */
};

std::vector<MetaEntry> read_meta_entries(std::vector<std::uint8_t> const& meta) {
    std::vector<MetaEntry> out;
    if (meta.size() < kFixedMetaHeaderSize) {
        return out;
    }
    std::size_t const count = (meta.size() - kFixedMetaHeaderSize) / kFixedMetaEntrySize;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t const base = kFixedMetaHeaderSize + i * kFixedMetaEntrySize;
        MetaEntry entry;
        entry.index = i;
        entry.offset = get_u32(meta, base + kFixedMetaRowOffset);
        entry.state = meta[base + kFixedMetaStateByte];
        entry.field_count = get_u16(meta, base + kFixedMetaFieldCount);
        out.push_back(entry);
    }
    return out;
}

/* The first FixedMeta entry that names `offset`, or -1.  FixedMeta is a list of
 * FixedData offsets, not an array indexed by row (core/mpp/reader.cpp), so every
 * row lookup goes through this. */
std::int32_t entry_index_for_offset(std::vector<MetaEntry> const& entries,
                                    std::uint32_t offset) {
    for (MetaEntry const& entry : entries) {
        if (entry.offset == offset) {
            return static_cast<std::int32_t>(entry.index);
        }
    }
    return -1;
}

bool uid_has_var_group(std::vector<VarEntry> const& entries, std::uint32_t uid) {
    for (VarEntry const& entry : entries) {
        if (entry.mid == uid || entry.mid == (uid | kSecondaryUidFamily)) {
            return true;
        }
    }
    return false;
}

/*
 * Reject a table whose size identity is broken before any geometry changes.
 * `check_table` covers the size words and the FixedMeta entry count; the extra
 * checks here are the two declared counts it deliberately tolerates (Project's
 * own damaged files) plus the VarMeta declared entry count, because this file
 * does arithmetic on both.
 */
void validate_table(CfbDocument const& doc, std::string_view table,
                    std::string const& function) {
    for (char const* name : {"FixedData", "FixedMeta", "Fixed2Data", "Fixed2Meta",
                             "VarMeta", "Var2Data"}) {
        std::string const path = table_stream(table, name);
        if (!doc.has_stream(path)) {
            throw Error(function + ": the table has no " + path +
                        " stream; a structural write needs all six Task table "
                        "streams");
        }
    }
    std::string const problem = check_table(doc, table);
    if (!problem.empty()) {
        throw Error(function + ": the Task table's size identity is broken (" +
                    problem + "); refusing to change its geometry");
    }
    std::vector<std::uint8_t> const fixed_meta =
        doc.read_stream(table_stream(table, "FixedMeta"));
    if ((fixed_meta.size() - kFixedMetaHeaderSize) % kFixedMetaEntrySize != 0) {
        throw Error(function + ": FixedMeta is " + std::to_string(fixed_meta.size()) +
                    " bytes, not 16 + 47*n; refusing to append an entry");
    }
    std::vector<std::uint8_t> const var_meta =
        doc.read_stream(table_stream(table, "VarMeta"));
    std::vector<VarEntry> const var_entries = read_var_entries(var_meta);
    std::uint32_t const declared_entries = get_u32(var_meta, 8);
    if (declared_entries != var_entries.size()) {
        throw Error(function + ": VarMeta header declares " +
                    std::to_string(declared_entries) + " entries but the stream holds " +
                    std::to_string(var_entries.size()) +
                    "; refusing to add or remove entries in a damaged stream");
    }
    std::vector<std::uint8_t> const fixed2_meta =
        doc.read_stream(table_stream(table, "Fixed2Meta"));
    if ((fixed2_meta.size() - kFixed2MetaHeaderSize) % kFixed2MetaEntrySize != 0) {
        throw Error(function + ": Fixed2Meta is " +
                    std::to_string(fixed2_meta.size()) +
                    " bytes, not 16 + 96*n; refusing to append an entry");
    }
    std::uint32_t const declared_fixed2 =
        get_u32(fixed2_meta, 8);
    std::size_t const fixed2_entries =
        (fixed2_meta.size() - kFixed2MetaHeaderSize) / kFixed2MetaEntrySize;
    if (declared_fixed2 != fixed2_entries) {
        throw Error(function + ": Fixed2Meta header declares " +
                    std::to_string(declared_fixed2) + " entries but the stream holds " +
                    std::to_string(fixed2_entries) +
                    "; refusing to change its geometry");
    }
}

/*
 * Write the document, then prove from the file on disk that (a) it holds exactly
 * the bytes that were built, (b) it re-emits them, (c) the container self-check
 * and the table consistency check are clean.  Returns the re-read document; the
 * caller adds the operation-specific checks before replacing its own document.
 */
CfbDocument save_and_verify(CfbDocument& work, std::string const& path,
                            std::string const& function, std::string_view table) {
    std::vector<std::uint8_t> const intended = work.to_bytes();
    work.save(path);
    std::vector<std::uint8_t> const on_disk = read_file_bytes(path);
    if (on_disk != intended) {
        throw Error(function + ": " + path + " holds " +
                    std::to_string(on_disk.size()) + " bytes, but the document built " +
                    std::to_string(intended.size()) + "; the write did not land");
    }
    CfbDocument reread = CfbDocument::from_file(path);
    if (reread.to_bytes() != on_disk) {
        throw Error(function + ": the written file does not re-emit byte-identically");
    }
    if (!reread.check().empty()) {
        throw Error(function + ": the written container is inconsistent: " +
                    reread.check());
    }
    std::string const table_problem = check_table(reread, table);
    if (!table_problem.empty()) {
        throw Error(function + ": the written table is inconsistent: " + table_problem);
    }
    return reread;
}

/* Merge a caller's TaskValues onto the row's stored values.  Every sentinel keeps
 * what is stored, so `NewTaskRow{}` (no values) is a pure clone. */
void merge_values(TaskValues& destination, TaskValues const& supplied) {
    auto date = [](std::uint32_t& out, std::uint32_t in) {
        if (in != kInvalidDate) {
            out = in;
        }
    };
    date(destination.start, supplied.start);
    date(destination.finish, supplied.finish);
    date(destination.project_start, supplied.project_start);
    date(destination.project_finish, supplied.project_finish);
    date(destination.late_start, supplied.late_start);
    date(destination.late_finish, supplied.late_finish);
    auto ticks = [](std::uint32_t& out, std::uint32_t in) {
        if (in != kInherit) {
            out = in;
        }
    };
    ticks(destination.duration, supplied.duration);
    ticks(destination.remaining_duration, supplied.remaining_duration);
    ticks(destination.actual_duration, supplied.actual_duration);
    auto slack = [](std::int32_t& out, std::int32_t in) {
        if (in != kInheritSlack) {
            out = in;
        }
    };
    slack(destination.total_slack, supplied.total_slack);
    slack(destination.start_slack, supplied.start_slack);
    auto single = [](std::uint16_t& out, std::uint16_t in) {
        if (in != static_cast<std::uint16_t>(kInherit)) {
            out = in;
        }
    };
    single(destination.priority, supplied.priority);
    single(destination.percent_complete, supplied.percent_complete);
    single(destination.constraint_type, supplied.constraint_type);
    auto raw = [](std::uint32_t& out, std::uint32_t in) {
        if (in != kInherit) {
            out = in;
        }
    };
    raw(destination.constraint_date, supplied.constraint_date);
    raw(destination.start_anchor, supplied.start_anchor);
    raw(destination.fixed2_start, supplied.fixed2_start);
    raw(destination.fixed2_finish, supplied.fixed2_finish);
    raw(destination.fixed2_duration, supplied.fixed2_duration);
}

/* The payload the template row stores for a variable field, or empty when it has
 * none.  The block is copied, not shared: two uids never point at one block. */
std::vector<std::uint8_t> template_payload(std::vector<std::uint8_t> const& var2,
                                           std::vector<VarEntry> const& entries,
                                           std::uint32_t uid,
                                           std::uint32_t key,
                                           bool& found) {
    found = false;
    for (VarEntry const& entry : entries) {
        if ((entry.mid == uid || entry.mid == (uid | kSecondaryUidFamily)) &&
            entry.field_id == key) {
            if (static_cast<std::size_t>(entry.offset) + 4 > var2.size()) {
                continue;
            }
            found = true;
            return read_var_block(var2, entry.offset);
        }
    }
    return {};
}

/* A field the template row does not store.  The defaults are the measured values
 * of a fresh task row: the five overtime fields are 8 zero bytes, 0x0b400563 and
 * 0x0b400564 are 4 bytes of 0xff (w10-adddel-base.mpp, every task row). */
std::vector<std::uint8_t> default_payload(std::uint32_t key) {
    if (key == 0x0B400563u || key == 0x0B400564u) {
        return std::vector<std::uint8_t>(4, 0xFF);
    }
    return std::vector<std::uint8_t>(8, 0x00);
}

using RowId = std::array<std::uint8_t, 16>;

RowId read_row_id(std::vector<std::uint8_t> const& fixed2, std::size_t offset) {
    RowId id{};
    if (offset + id.size() <= fixed2.size()) {
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(offset),
                  fixed2.begin() + static_cast<std::ptrdiff_t>(offset + id.size()),
                  id.begin());
    }
    return id;
}

/*
 * A deterministic, unique row identity for the new Fixed2Data record.  Project
 * writes a GUID-like 16-byte value whose first bytes are a generation stamp; its
 * meaning is not known (core/mpp/reader.cpp's outline section), so the writer
 * only needs a value that is not zero and not already used by another record.
 * The first 8 bytes are a hash of (salt, uid, row, attempt); the template's last
 * 8 bytes keep the file's own stamp shape.
 */
RowId make_row_identity(RowId const& template_id, std::vector<RowId> const& used,
                        std::uint32_t uid, std::size_t row, std::string const& function) {
    for (std::uint32_t attempt = 0; attempt < 1024; ++attempt) {
        std::uint64_t hash = 1469598103934665603ULL; /* FNV-1a offset basis */
        auto mix = [&hash](std::uint32_t value) {
            for (std::size_t i = 0; i < 4; ++i) {
                hash ^= (value >> (8 * i)) & 0xFFu;
                hash *= 1099511628211ULL;
            }
        };
        mix(0x57343530u); /* "W450" tag, so the value is ours and reproducible */
        mix(uid);
        mix(static_cast<std::uint32_t>(row));
        mix(attempt);
        RowId id = template_id;
        for (std::size_t i = 0; i < 8; ++i) {
            id[i] = static_cast<std::uint8_t>((hash >> (8 * i)) & 0xFFu);
        }
        bool const blank = std::all_of(id.begin(), id.end(),
                                       [](std::uint8_t byte) { return byte == 0; });
        bool const collision =
            std::find(used.begin(), used.end(), id) != used.end();
        if (!blank && !collision) {
            return id;
        }
    }
    throw Error(function + ": could not derive an unused Fixed2Data row identity");
}

/* ---- dependent rows (the delete policy) ---- */

/* Live rows of a table without a FixedData header (Link, Assignment) that name
 * `uid`.  Liveness is the same rule the reader uses: a FixedMeta entry's +4 word
 * points at the row's byte offset. */
std::size_t count_live_references(CfbDocument const& doc, std::string_view table,
                                  std::size_t row_size, std::size_t entry_size,
                                  std::size_t uid_offset, bool link_table,
                                  std::uint32_t uid) {
    std::string const fixed_path = table_stream(table, "FixedData");
    if (!doc.has_stream(fixed_path)) {
        return 0;
    }
    std::vector<std::uint8_t> const fixed = doc.read_stream(fixed_path);
    std::size_t const rows = fixed.size() / row_size;
    std::vector<bool> covered(rows, false);
    std::string const meta_path = table_stream(table, "FixedMeta");
    if (doc.has_stream(meta_path)) {
        std::vector<std::uint8_t> const meta = doc.read_stream(meta_path);
        if (meta.size() >= 16) {
            std::size_t const entries = (meta.size() - 16) / entry_size;
            for (std::size_t i = 0; i < entries; ++i) {
                std::size_t const base = 16 + i * entry_size;
                if (base + 4 > meta.size()) {
                    break;
                }
                std::uint32_t const offset = get_u32(meta, base + 4);
                if (offset % row_size == 0) {
                    std::size_t const row = offset / row_size;
                    if (row < rows) {
                        covered[row] = true;
                    }
                }
            }
        }
    } else {
        std::fill(covered.begin(), covered.end(), true);
    }
    std::size_t matches = 0;
    for (std::size_t row = 0; row < rows; ++row) {
        if (!covered[row]) {
            continue;
        }
        std::size_t const base = row * row_size;
        if (base + uid_offset + 4 > fixed.size()) {
            continue;
        }
        bool const named =
            link_table ? (get_u32(fixed, base + 4) == uid ||
                          get_u32(fixed, base + 8) == uid)
                       : (get_u32(fixed, base + uid_offset) == uid);
        if (named) {
            ++matches;
        }
    }
    return matches;
}

} /* namespace */

/* --- add ----------------------------------------------------------------- */

AddedRow add_task_row(CfbDocument& doc, NewTaskRow const& task,
                      std::string const& path, std::string_view table) {
    std::string const function = "add_task_row";
    if (task.name.empty()) {
        throw Error(function + ": the task name is empty; a new row needs a Name "
                    "block (VarMeta key 0x0b40000e)");
    }
    check_output_path(path, function);
    validate_table(doc, table, function);

    std::string const fixed_path = table_stream(table, "FixedData");
    std::string const meta_path = table_stream(table, "FixedMeta");
    std::string const fixed2_path = table_stream(table, "Fixed2Data");
    std::string const fixed2_meta_path = table_stream(table, "Fixed2Meta");
    std::string const var_meta_path = table_stream(table, "VarMeta");
    std::string const var2_path = table_stream(table, "Var2Data");

    TaskTable const before = TaskTable::from_document(doc, table);
    std::size_t const rows = before.rows();
    std::vector<std::uint8_t> fixed = doc.read_stream(fixed_path);
    std::vector<std::uint8_t> meta = doc.read_stream(meta_path);
    std::vector<std::uint8_t> fixed2 = doc.read_stream(fixed2_path);
    std::vector<std::uint8_t> fixed2_meta = doc.read_stream(fixed2_meta_path);
    std::vector<std::uint8_t> var_meta = doc.read_stream(var_meta_path);
    std::vector<std::uint8_t> var2 = doc.read_stream(var2_path);

    std::vector<MetaEntry> const meta_entries = read_meta_entries(meta);
    std::vector<VarEntry> const var_entries = read_var_entries(var_meta);

    /* The last live task row is the template: its 202 bytes, its Fixed2Meta
     * entry and its variable payloads are all legal for this file. */
    std::size_t template_row = rows;
    std::int32_t template_entry = -1;
    for (std::size_t row = rows; row-- > 1;) {
        std::int32_t const index = entry_index_for_offset(
            meta_entries, static_cast<std::uint32_t>(task_row_offset(row)));
        if (index >= 0 && meta_entries[static_cast<std::size_t>(index)].state ==
                              kStateLive &&
            uid_has_var_group(var_entries, before.uid(row))) {
            template_row = row;
            template_entry = index;
            break;
        }
    }
    if (template_row >= rows || template_entry < 0) {
        throw Error(function +
                    ": the table has no live task row to use as a template; a new "
                    "row is cloned from the last live task so that every byte it "
                    "starts with is legal for this file");
    }
    std::uint32_t const template_uid = before.uid(template_row);

    /* UniqueID: the caller's, or one past the largest uid in the whole table
     * (tombstones included, so a deleted uid is never reused silently). */
    std::uint32_t max_uid = 0;
    for (std::size_t row = 0; row < rows; ++row) {
        max_uid = std::max(max_uid, before.uid(row));
    }
    std::uint32_t new_uid = task.uid;
    if (new_uid == 0) {
        if (max_uid == 0xFFFFFFFFu) {
            throw Error(function + ": UniqueID space is exhausted");
        }
        new_uid = max_uid + 1;
    }
    if (new_uid == 0) {
        throw Error(function + ": UniqueID 0 is the project row and cannot be used "
                    "for a task");
    }
    for (std::size_t row = 0; row < rows; ++row) {
        if (before.uid(row) == new_uid) {
            throw Error(function + ": uid " + std::to_string(new_uid) +
                        " is already on " + row_label(row, new_uid) +
                        "; a UniqueID must be unique");
        }
    }

    /* The display ID: one past the largest ID among live task rows. */
    std::uint32_t max_live_id = 0;
    for (std::size_t row = 1; row < rows; ++row) {
        std::int32_t const index = entry_index_for_offset(
            meta_entries, static_cast<std::uint32_t>(task_row_offset(row)));
        if (index >= 0 &&
            meta_entries[static_cast<std::size_t>(index)].state == kStateLive) {
            max_live_id = std::max(max_live_id, before.row_id(row));
        }
    }
    if (max_live_id == 0xFFFFFFFFu) {
        throw Error(function + ": the task ID column is exhausted");
    }
    std::uint32_t const new_id = max_live_id + 1;

    /* ---- FixedData: append the cloned row, with a new ID/UniqueID and the
     * constraint/anchor cleared (see add_delete.h: they are not inherited). */
    std::size_t const template_start = task_row_offset(template_row);
    std::vector<std::uint8_t> new_row(
        fixed.begin() + static_cast<std::ptrdiff_t>(template_start),
        fixed.begin() + static_cast<std::ptrdiff_t>(template_start + task_row::kStride));
    put_u32(new_row, task_row::kRowId, new_id);
    put_u32(new_row, task_row::kUniqueId, new_uid);
    put_u16(new_row, task_row::kConstraintType, 0);
    put_u32(new_row, task_row::kConstraintDate, kInvalidDate);
    put_u32(new_row, task_row::kStartAnchor, kInvalidDate);
    fixed.insert(fixed.end(), new_row.begin(), new_row.end());

    /* ---- FixedMeta: append an entry cloned from the template's, with state 0,
     * field count 8 and the new row's offset. */
    std::size_t const template_meta_base =
        kFixedMetaHeaderSize + static_cast<std::size_t>(template_entry) *
                                   kFixedMetaEntrySize;
    std::vector<std::uint8_t> new_meta_entry(
        meta.begin() + static_cast<std::ptrdiff_t>(template_meta_base),
        meta.begin() + static_cast<std::ptrdiff_t>(template_meta_base +
                                                   kFixedMetaEntrySize));
    new_meta_entry[kFixedMetaStateByte] = kStateLive;
    put_u16(new_meta_entry, kFixedMetaFieldCount, kNewRowFieldCount);
    put_u32(new_meta_entry, kFixedMetaRowOffset,
            static_cast<std::uint32_t>(task_row_offset(rows)));
    meta.insert(meta.end(), new_meta_entry.begin(), new_meta_entry.end());
    put_u32(meta, 8, get_u32(meta, 8) + 1);
    put_u32(meta, 12, static_cast<std::uint32_t>(fixed.size()));

    /* ---- Fixed2Data: append a 64-byte record.  Measured: +16 f64 = ordinal + 1
     * (row + 1), +0 a fresh identity, +24 the project row's identity (the new
     * task is top level), no has-children flag. */
    RowId const project_id = read_row_id(fixed2, 3 * kFixed2RecordSize);
    RowId const template_id =
        read_row_id(fixed2, (template_row + 3) * kFixed2RecordSize);
    std::vector<RowId> used;
    used.reserve(rows * 2);
    for (std::size_t row = 0; row < rows; ++row) {
        used.push_back(read_row_id(fixed2, (row + 3) * kFixed2RecordSize));
        used.push_back(read_row_id(fixed2, (row + 3) * kFixed2RecordSize + 24));
    }
    RowId const new_identity =
        make_row_identity(template_id, used, new_uid, rows, function);
    std::size_t const template_record = (template_row + 3) * kFixed2RecordSize;
    std::vector<std::uint8_t> new_record(
        fixed2.begin() + static_cast<std::ptrdiff_t>(template_record),
        fixed2.begin() + static_cast<std::ptrdiff_t>(template_record +
                                                     kFixed2RecordSize));
    std::copy(new_identity.begin(), new_identity.end(), new_record.begin());
    put_f64(new_record, kFixed2RecordOrdinal, static_cast<double>(rows + 1));
    std::copy(project_id.begin(), project_id.end(),
              new_record.begin() + static_cast<std::ptrdiff_t>(24));
    new_record[kFixed2RecordFlags] = static_cast<std::uint8_t>(
        new_record[kFixed2RecordFlags] & ~kFixed2FlagHasChildren);
    fixed2.insert(fixed2.end(), new_record.begin(), new_record.end());

    /* ---- Fixed2Meta: append the paired entry (entry index = the FixedMeta
     * entry index, core/mpp/reader.cpp), pointing at the new record. */
    std::size_t const fixed2_entries =
        (fixed2_meta.size() - kFixed2MetaHeaderSize) / kFixed2MetaEntrySize;
    if (static_cast<std::size_t>(template_entry) >= fixed2_entries) {
        throw Error(function + ": FixedMeta entry " +
                    std::to_string(template_entry) +
                    " has no Fixed2Meta counterpart; the two metadata streams "
                    "disagree");
    }
    std::size_t const template_fixed2_meta =
        kFixed2MetaHeaderSize + static_cast<std::size_t>(template_entry) *
                                    kFixed2MetaEntrySize;
    std::vector<std::uint8_t> new_fixed2_entry(
        fixed2_meta.begin() + static_cast<std::ptrdiff_t>(template_fixed2_meta),
        fixed2_meta.begin() + static_cast<std::ptrdiff_t>(template_fixed2_meta +
                                                          kFixed2MetaEntrySize));
    if (get_u32(new_fixed2_entry, kFixed2MetaRecordOffset) !=
        (template_row + 3) * kFixed2RecordSize) {
        throw Error(function + ": Fixed2Meta entry " +
                    std::to_string(template_entry) +
                    " does not describe the template Fixed2Data record; the two "
                    "metadata streams disagree");
    }
    put_u32(new_fixed2_entry, kFixed2MetaRecordOffset,
            static_cast<std::uint32_t>((rows + 3) * kFixed2RecordSize));
    new_fixed2_entry[kFixed2MetaFlags] =
        task.manually_scheduled ? kFixed2FlagsManual : kFixed2FlagsAuto;
    fixed2_meta.insert(fixed2_meta.end(), new_fixed2_entry.begin(),
                       new_fixed2_entry.end());
    put_u32(fixed2_meta, 8, get_u32(fixed2_meta, 8) + 1);
    put_u32(fixed2_meta, 12, static_cast<std::uint32_t>(fixed2.size()));

    /* ---- VarMeta/Var2Data: append the 8 payload blocks and the 8 entries. */
    for (std::uint32_t const key : kNewRowKeys) {
        std::vector<std::uint8_t> payload;
        if (key == kTaskNameFieldId) {
            payload = encode_utf16z(task.name);
        } else {
            bool found = false;
            payload = template_payload(var2, var_entries, template_uid, key, found);
            if (!found) {
                payload = default_payload(key);
            }
        }
        std::uint32_t const offset = static_cast<std::uint32_t>(var2.size());
        std::vector<std::uint8_t> const block = encode_block(payload);
        var2.insert(var2.end(), block.begin(), block.end());
        std::vector<std::uint8_t> entry(kVarMetaEntrySize, 0);
        put_u32(entry, 0, new_uid);
        put_u32(entry, 4, offset);
        put_u32(entry, 8, key);
        var_meta.insert(var_meta.end(), entry.begin(), entry.end());
    }
    put_u32(var_meta, 8, get_u32(var_meta, 8) +
                             static_cast<std::uint32_t>(kNewRowKeys.size()));
    put_u32(var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(var2.size()));

    /* ---- Apply, on a copy, so a refusal leaves the caller's document alone. */
    CfbDocument work = CfbDocument::from_bytes(doc.to_bytes());
    work.replace_stream(fixed_path, fixed);
    work.replace_stream(meta_path, meta);
    work.replace_stream(fixed2_path, fixed2);
    work.replace_stream(fixed2_meta_path, fixed2_meta);
    work.replace_stream(var_meta_path, var_meta);
    work.replace_stream(var2_path, var2);

    TaskValues merged = read_task_values(work, rows, table);
    merge_values(merged, task.values);
    RowWriteReport report;
    try {
        report = write_task_row(work, rows, merged, table);
    } catch (Error const& error) {
        throw Error(function + ": " + error.what());
    }

    CfbDocument const reread = save_and_verify(work, path, function, table);
    TaskTable const after = TaskTable::from_document(reread, table);
    if (after.rows() != rows + 1) {
        throw Error(function + ": the written file has " +
                    std::to_string(after.rows()) + " rows, expected " +
                    std::to_string(rows + 1));
    }
    if (after.uid(rows) != new_uid || after.row_id(rows) != new_id) {
        throw Error(function + ": the written row reads uid " +
                    std::to_string(after.uid(rows)) + " / id " +
                    std::to_string(after.row_id(rows)) + ", expected uid " +
                    std::to_string(new_uid) + " / id " + std::to_string(new_id));
    }
    TaskValues const stored = read_task_values(reread, rows, table);
    if (!(stored == report.written)) {
        throw Error(function + ": the written row does not read back with the "
                    "values that were written");
    }
    if (read_task_name(reread, new_uid, table) != task.name) {
        throw Error(function + ": the new row's Name does not read back as written");
    }

    std::vector<std::uint8_t> const meta_after = reread.read_stream(meta_path);
    std::vector<MetaEntry> const entries_after = read_meta_entries(meta_after);
    if (entries_after.size() != meta_entries.size() + 1) {
        throw Error(function + ": FixedMeta holds " +
                    std::to_string(entries_after.size()) + " entries, expected " +
                    std::to_string(meta_entries.size() + 1));
    }
    MetaEntry const& added = entries_after.back();
    if (added.state != kStateLive || added.field_count != kNewRowFieldCount ||
        added.offset != task_row_offset(rows)) {
        throw Error(function + ": the new FixedMeta entry reads state " +
                    std::to_string(added.state) + ", field count " +
                    std::to_string(added.field_count) + ", offset " +
                    std::to_string(added.offset));
    }
    if (get_u32(meta_after, 8) != entries_after.size() ||
        get_u32(meta_after, 12) != fixed.size()) {
        throw Error(function + ": FixedMeta's header counts do not match the "
                    "written table");
    }

    std::vector<std::uint8_t> const fixed2_meta_after =
        reread.read_stream(fixed2_meta_path);
    std::size_t const fixed2_entries_after =
        (fixed2_meta_after.size() - kFixed2MetaHeaderSize) / kFixed2MetaEntrySize;
    if (fixed2_entries_after != fixed2_entries + 1 ||
        get_u32(fixed2_meta_after, 8) != fixed2_entries_after ||
        get_u32(fixed2_meta_after, 12) != fixed2.size()) {
        throw Error(function + ": Fixed2Meta's header/entry counts do not match "
                    "the written Fixed2Data");
    }
    std::size_t const new_fixed2_entry_after =
        kFixed2MetaHeaderSize + fixed2_entries * kFixed2MetaEntrySize;
    std::uint8_t const expected_flags =
        task.manually_scheduled ? kFixed2FlagsManual : kFixed2FlagsAuto;
    if (get_u32(fixed2_meta_after, new_fixed2_entry_after +
                                       kFixed2MetaRecordOffset) !=
            (rows + 3) * kFixed2RecordSize ||
        fixed2_meta_after[new_fixed2_entry_after + kFixed2MetaFlags] !=
            expected_flags) {
        throw Error(function + ": the new Fixed2Meta entry does not read back "
                    "with the written record offset/flags");
    }

    std::vector<std::uint8_t> const var_meta_after = reread.read_stream(var_meta_path);
    std::vector<std::uint8_t> const var2_after = reread.read_stream(var2_path);
    std::vector<VarEntry> const var_entries_after =
        read_var_entries(var_meta_after);
    if (var_entries_after.size() != var_entries.size() + kNewRowKeys.size()) {
        throw Error(function + ": VarMeta holds " +
                    std::to_string(var_entries_after.size()) + " entries, expected " +
                    std::to_string(var_entries.size() + kNewRowKeys.size()));
    }
    if (get_u32(var_meta_after, 8) != var_entries_after.size() ||
        get_u32(var_meta_after, kVarMetaVar2SizeOffset) != var2_after.size()) {
        throw Error(function + ": VarMeta's header counts do not match the written "
                    "streams");
    }
    for (std::uint32_t const key : kNewRowKeys) {
        bool found = false;
        for (VarEntry const& entry : var_entries_after) {
            if (entry.mid == new_uid && entry.field_id == key) {
                found = true;
                (void)read_var_block(var2_after, entry.offset); /* bounds-checked */
                if (key == kTaskNameFieldId &&
                    decode_utf16z(read_var_block(var2_after, entry.offset)) !=
                        task.name) {
                    throw Error(function + ": the new row's Name block does not "
                                "decode as written");
                }
                break;
            }
        }
        if (!found) {
            throw Error(function + ": VarMeta has no entry for the new uid " +
                        std::to_string(new_uid) + " and key 0x" +
                        std::to_string(key));
        }
    }

    AddedRow result;
    result.uid = new_uid;
    result.id = new_id;
    result.row = rows;
    result.values = stored;
    result.warnings = report.warnings;
    doc = reread;
    return result;
}

/* --- delete -------------------------------------------------------------- */

DeletedRow delete_task_row(CfbDocument& doc, std::uint32_t uid,
                           std::string const& path,
                           DeleteOptions const& options, std::string_view table) {
    std::string const function = "delete_task_row";
    check_output_path(path, function);
    if (uid == 0) {
        throw Error(function + ": uid 0 is the project row and cannot be deleted");
    }
    validate_table(doc, table, function);

    std::string const fixed_path = table_stream(table, "FixedData");
    std::string const meta_path = table_stream(table, "FixedMeta");
    std::string const fixed2_path = table_stream(table, "Fixed2Data");
    std::string const fixed2_meta_path = table_stream(table, "Fixed2Meta");
    std::string const var_meta_path = table_stream(table, "VarMeta");
    std::string const var2_path = table_stream(table, "Var2Data");

    TaskTable const before = TaskTable::from_document(doc, table);
    std::size_t const rows = before.rows();
    std::size_t row = rows;
    for (std::size_t candidate = 0; candidate < rows; ++candidate) {
        if (before.uid(candidate) == uid) {
            row = candidate;
            break;
        }
    }
    if (row >= rows) {
        throw Error(function + ": no task row has UniqueID " + std::to_string(uid));
    }
    if (row == 0) {
        throw Error(function + ": uid 0 is the project row and cannot be deleted");
    }

    std::vector<std::uint8_t> fixed = doc.read_stream(fixed_path);
    std::vector<std::uint8_t> meta = doc.read_stream(meta_path);
    std::vector<std::uint8_t> const fixed2 = doc.read_stream(fixed2_path);
    std::vector<std::uint8_t> const fixed2_meta = doc.read_stream(fixed2_meta_path);
    std::vector<std::uint8_t> var_meta = doc.read_stream(var_meta_path);
    std::vector<std::uint8_t> const var2 = doc.read_stream(var2_path);

    std::vector<MetaEntry> const meta_entries = read_meta_entries(meta);
    std::int32_t const entry_index = entry_index_for_offset(
        meta_entries, static_cast<std::uint32_t>(task_row_offset(row)));
    if (entry_index < 0) {
        throw Error(function + ": " + row_label(row, uid) +
                    " has no FixedMeta entry; Project treats such a row as deleted "
                    "already");
    }
    MetaEntry const& entry = meta_entries[static_cast<std::size_t>(entry_index)];
    if (entry.state != kStateLive) {
        throw Error(function + ": uid " + std::to_string(uid) + " is already a "
                    "tombstone (FixedMeta +0 = 0x02)");
    }
    std::vector<VarEntry> const var_entries = read_var_entries(var_meta);
    if (!uid_has_var_group(var_entries, uid)) {
        throw Error(function + ": uid " + std::to_string(uid) +
                    " has no VarMeta group, so Project does not read it as a live "
                    "task; there is nothing to tombstone");
    }

    /* Dependent rows the writer cannot remove.  A link is scheduling-visible, so
     * the default is a refusal for either kind (add_delete.h). */
    std::size_t const live_links =
        count_live_references(doc, "114/TBkndCons", 20, 10, 0, true, uid);
    std::size_t const live_assignments =
        count_live_references(doc, "114/TBkndAssn", 110, 34, 4, false, uid);
    if ((live_links > 0 || live_assignments > 0) && !options.allow_dangling) {
        throw Error(function + ": uid " + std::to_string(uid) + " is named by " +
                    std::to_string(live_links) + " live link(s) and " +
                    std::to_string(live_assignments) +
                    " live assignment(s); deleting it would leave them dangling. "
                    "Set DeleteOptions::allow_dangling to proceed and warn instead "
                    "(the writer cannot delete link/assignment rows)");
    }

    std::vector<std::string> warnings;
    if (live_links > 0) {
        warnings.push_back("uid " + std::to_string(uid) + ": " +
                           std::to_string(live_links) +
                           " live link row(s) still name this task; Project will not "
                           "schedule a link with a dead endpoint (PROJECT_B is the "
                           "measured precedent)");
    }
    if (live_assignments > 0) {
        warnings.push_back("uid " + std::to_string(uid) + ": " +
                           std::to_string(live_assignments) +
                           " live assignment row(s) still name this task; they are "
                           "left in place (PROJECT_B stores 10 assignments naming deleted "
                           "task uids)");
    }

    /* ---- Renumber later live rows' ID down by one (Project: uid 5's 5 -> 4). */
    std::vector<bool> live(rows, false);
    live[0] = true;
    for (std::size_t candidate = 1; candidate < rows; ++candidate) {
        std::int32_t const index = entry_index_for_offset(
            meta_entries, static_cast<std::uint32_t>(task_row_offset(candidate)));
        live[candidate] =
            index >= 0 &&
            meta_entries[static_cast<std::size_t>(index)].state == kStateLive &&
            uid_has_var_group(var_entries, before.uid(candidate));
    }
    std::uint32_t const old_id = before.row_id(row);
    for (std::size_t candidate = row + 1; candidate < rows; ++candidate) {
        if (!live[candidate]) {
            continue;
        }
        std::uint32_t const id = before.row_id(candidate);
        if (id == 0) {
            throw Error(function + ": " + row_label(candidate, before.uid(candidate)) +
                        " has ID 0 and cannot be renumbered");
        }
        put_u32(fixed, task_row_offset(candidate) + task_row::kRowId, id - 1);
    }
    for (std::size_t first = 1; first < rows; ++first) {
        if (!live[first] || first == row) {
            continue;
        }
        std::uint32_t const first_id = get_u32(
            fixed, task_row_offset(first) + task_row::kRowId);
        for (std::size_t second = first + 1; second < rows; ++second) {
            if (!live[second] || second == row) {
                continue;
            }
            std::uint32_t const second_id = get_u32(
                fixed, task_row_offset(second) + task_row::kRowId);
            if (first_id == second_id) {
                throw Error(function + ": renumbering would give uid " +
                            std::to_string(before.uid(first)) + " and uid " +
                            std::to_string(before.uid(second)) + " the same ID " +
                            std::to_string(first_id) + "; refusing to guess");
            }
        }
    }

    /* ---- Tombstone the FixedMeta entry; every stream keeps its size. */
    std::size_t const meta_base =
        kFixedMetaHeaderSize + static_cast<std::size_t>(entry_index) *
                                   kFixedMetaEntrySize;
    meta[meta_base + kFixedMetaStateByte] = kStateDeleted;
    put_u16(meta, meta_base + kFixedMetaFieldCount, 0);
    /* The per-entry tombstone is only honoured when the table's header flags
     * word says the table contains a deleted row (measured: without this bit
     * Project reopened W45's delete fixtures with the deleted task still
     * visible).  OR it in; never clear it. */
    put_u32(meta, kFixedMetaHeaderFlags,
            get_u32(meta, kFixedMetaHeaderFlags) | kFixedMetaFlagHasDeleted);

    /* ---- Remove the uid's VarMeta entries; Var2Data keeps its blocks. */
    std::vector<std::uint8_t> rebuilt(var_meta.begin(),
                                      var_meta.begin() + static_cast<std::ptrdiff_t>(
                                                             kVarMetaHeaderSize));
    std::size_t removed = 0;
    for (VarEntry const& var_entry : var_entries) {
        if (var_entry.mid == uid || var_entry.mid == (uid | kSecondaryUidFamily)) {
            ++removed;
            continue;
        }
        std::size_t const base =
            kVarMetaHeaderSize + var_entry.index * kVarMetaEntrySize;
        rebuilt.insert(rebuilt.end(),
                       var_meta.begin() + static_cast<std::ptrdiff_t>(base),
                       var_meta.begin() +
                           static_cast<std::ptrdiff_t>(base + kVarMetaEntrySize));
    }
    if (removed == 0) {
        throw Error(function + ": uid " + std::to_string(uid) +
                    " has no VarMeta entries to remove");
    }
    std::uint32_t const declared = get_u32(var_meta, 8);
    if (declared < removed) {
        throw Error(function + ": VarMeta declares " + std::to_string(declared) +
                    " entries but " + std::to_string(removed) + " belong to uid " +
                    std::to_string(uid));
    }
    put_u32(rebuilt, 8, declared - static_cast<std::uint32_t>(removed));
    put_u32(rebuilt, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(var2.size()));

    CfbDocument work = CfbDocument::from_bytes(doc.to_bytes());
    work.replace_stream(fixed_path, fixed);
    work.replace_stream(meta_path, meta);
    work.replace_stream(var_meta_path, rebuilt);

    CfbDocument const reread = save_and_verify(work, path, function, table);
    TaskTable const after = TaskTable::from_document(reread, table);
    if (after.rows() != rows) {
        throw Error(function + ": the written file has " +
                    std::to_string(after.rows()) + " rows, expected " +
                    std::to_string(rows) + " (delete never shrinks the table)");
    }
    if (after.uid(row) != uid) {
        throw Error(function + ": row " + std::to_string(row) + " reads uid " +
                    std::to_string(after.uid(row)) + ", expected " +
                    std::to_string(uid));
    }
    if (read_task_values(reread, row, table) != read_task_values(doc, row, table)) {
        throw Error(function + ": the tombstoned row's values changed");
    }
    if (!read_task_name(reread, uid, table).empty()) {
        throw Error(function + ": uid " + std::to_string(uid) +
                    " still has a Name entry after the delete");
    }

    std::vector<std::uint8_t> const meta_after = reread.read_stream(meta_path);
    std::vector<MetaEntry> const entries_after = read_meta_entries(meta_after);
    if (entries_after.size() != meta_entries.size()) {
        throw Error(function + ": FixedMeta entry count changed (" +
                    std::to_string(entries_after.size()) + " vs " +
                    std::to_string(meta_entries.size()) + ")");
    }
    std::int32_t const entry_after = entry_index_for_offset(
        entries_after, static_cast<std::uint32_t>(task_row_offset(row)));
    if (entry_after < 0) {
        throw Error(function + ": the tombstoned row lost its FixedMeta entry");
    }
    MetaEntry const& tombstone = entries_after[static_cast<std::size_t>(entry_after)];
    if (tombstone.state != kStateDeleted || tombstone.field_count != 0) {
        throw Error(function + ": the FixedMeta entry reads state " +
                    std::to_string(tombstone.state) + " / field count " +
                    std::to_string(tombstone.field_count) +
                    ", expected 0x02 / 0");
    }
    if (get_u32(meta_after, 8) != entries_after.size() ||
        get_u32(meta_after, 12) != fixed.size()) {
        throw Error(function + ": FixedMeta's header counts changed");
    }
    if ((get_u32(meta_after, kFixedMetaHeaderFlags) & kFixedMetaFlagHasDeleted) == 0) {
        throw Error(function + ": FixedMeta header +4 does not have the "
                    "\"contains deleted rows\" bit (0x02); Project would ignore "
                    "the tombstone");
    }

    std::vector<std::uint8_t> const var_meta_after = reread.read_stream(var_meta_path);
    std::vector<VarEntry> const var_entries_after = read_var_entries(var_meta_after);
    if (var_entries_after.size() != var_entries.size() - removed) {
        throw Error(function + ": VarMeta holds " +
                    std::to_string(var_entries_after.size()) + " entries, expected " +
                    std::to_string(var_entries.size() - removed));
    }
    for (VarEntry const& var_entry : var_entries_after) {
        if (var_entry.mid == uid || var_entry.mid == (uid | kSecondaryUidFamily)) {
            throw Error(function + ": a VarMeta entry for uid " + std::to_string(uid) +
                        " survived the delete");
        }
    }
    if (get_u32(var_meta_after, 8) != var_entries_after.size() ||
        get_u32(var_meta_after, kVarMetaVar2SizeOffset) !=
            reread.read_stream(var2_path).size()) {
        throw Error(function + ": VarMeta's header counts do not match the written "
                    "streams");
    }

    /* Fixed2Data, Fixed2Meta and Var2Data must be byte-identical: a delete is a
     * tombstone, not a layout change. */
    if (reread.read_stream(fixed2_path) != fixed2 ||
        reread.read_stream(fixed2_meta_path) != fixed2_meta ||
        reread.read_stream(var2_path) != var2) {
        throw Error(function + ": Fixed2Data/Fixed2Meta/Var2Data changed; a delete "
                    "must not rewrite them");
    }

    DeletedRow result;
    result.uid = uid;
    result.id = old_id;
    result.row = row;
    result.var_entries_removed = removed;
    result.warnings = std::move(warnings);
    doc = reread;
    return result;
}

} /* namespace mppwrite */
