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

#include "outline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
    slack(destination.free_slack, supplied.free_slack);
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

/* ---- dependent rows: the delete cascade (w11-oracle §2) ---- */

constexpr std::size_t kDependentMetaHeaderSize = 16;
constexpr std::size_t kConsRowSize = 20;
constexpr std::size_t kConsMetaEntrySize = 10;
constexpr std::size_t kConsPredecessor = 4;
constexpr std::size_t kConsSuccessor = 8;
constexpr std::size_t kAssnRowSize = 110;
constexpr std::size_t kAssnMetaEntrySize = 34;
constexpr std::size_t kAssnTaskUid = 4;

/* One FixedData row of a dependent table (Link = 20 B, Assignment = 110 B) plus
 * the FixedMeta entries that name it.  Project's own delete leaves the row in
 * place and tombstones its entry, so this file does the same. */
struct DependentRow {
    std::size_t row = 0;
    std::uint32_t uid = 0;
    std::uint32_t predecessor = 0; /* Link +4, 0 for an assignment row */
    std::uint32_t successor = 0;   /* Link +8, 0 for an assignment row */
    std::uint32_t task = 0;        /* Assignment +4, 0 for a link row */
    std::vector<std::size_t> entries; /* every FixedMeta entry naming this row */
    bool live = false; /* some naming entry has state +0 == 0 */
};

struct DependentTable {
    std::string fixed_path;
    std::string meta_path;
    std::size_t row_size = 0;
    std::size_t entry_size = 0;
    std::vector<std::uint8_t> fixed;
    std::vector<std::uint8_t> meta;
    std::vector<DependentRow> rows;
};

/*
 * Read and check one dependent table.  A missing FixedData stream means the
 * table has no rows (base.mpp has an empty TBkndCons); a missing FixedMeta next
 * to rows is damage, because without it there is no way to tell which rows
 * Project considers live.  The geometry checks mirror the ones the writer runs
 * on the Task table: FixedData must be a whole number of rows and every
 * FixedMeta entry's +4 word must name a row.  Declared entry counts are NOT
 * checked here: the cascade only rewrites entry bytes, so a project file whose
 * count word is stale (PROJECT_A's link table says 88 and holds 91) can still be
 * tombstoned safely -- unlike an append, which does arithmetic on the count.
 */
DependentTable load_dependent_table(CfbDocument const& doc, std::string_view table,
                                    std::size_t row_size, std::size_t entry_size,
                                    std::string const& function) {
    DependentTable out;
    out.row_size = row_size;
    out.entry_size = entry_size;
    out.fixed_path = table_stream(table, "FixedData");
    out.meta_path = table_stream(table, "FixedMeta");
    if (!doc.has_stream(out.fixed_path)) {
        return out;
    }
    out.fixed = doc.read_stream(out.fixed_path);
    if (out.fixed.size() % row_size != 0) {
        throw Error(function + ": " + out.fixed_path + " is " +
                    std::to_string(out.fixed.size()) + " bytes, not n*" +
                    std::to_string(row_size) +
                    "; refusing to tombstone rows in a table whose geometry is "
                    "broken");
    }
    std::size_t const rows = out.fixed.size() / row_size;
    /* An EMPTY table still has a FixedMeta header (synthetic-base.mpp's Cons
     * table is 0 bytes of FixedData + the 16-byte FixedMeta header).  Read it so
     * a caller that appends the first row can validate and update the header
     * words; a table with rows but no FixedMeta stays damage. */
    if (!doc.has_stream(out.meta_path)) {
        if (rows == 0) {
            return out;
        }
        throw Error(function + ": " + out.fixed_path + " holds " +
                    std::to_string(rows) + " rows but the table has no " +
                    out.meta_path +
                    "; without FixedMeta there is no way to tell which rows are "
                    "live, so the delete cascade cannot run");
    }
    out.meta = doc.read_stream(out.meta_path);
    if (out.meta.size() < kDependentMetaHeaderSize ||
        (out.meta.size() - kDependentMetaHeaderSize) % entry_size != 0) {
        throw Error(function + ": " + out.meta_path + " is " +
                    std::to_string(out.meta.size()) + " bytes, not 16 + n*" +
                    std::to_string(entry_size) +
                    "; refusing to tombstone rows in a damaged dependent table");
    }
    if (rows == 0) {
        return out;
    }
    out.rows.resize(rows);
    for (std::size_t row = 0; row < rows; ++row) {
        DependentRow& entry_row = out.rows[row];
        entry_row.row = row;
        std::size_t const base = row * row_size;
        entry_row.uid = get_u32(out.fixed, base);
        if (row_size == kConsRowSize) {
            entry_row.predecessor = get_u32(out.fixed, base + kConsPredecessor);
            entry_row.successor = get_u32(out.fixed, base + kConsSuccessor);
        } else {
            entry_row.task = get_u32(out.fixed, base + kAssnTaskUid);
        }
    }
    std::size_t const entries =
        (out.meta.size() - kDependentMetaHeaderSize) / entry_size;
    for (std::size_t index = 0; index < entries; ++index) {
        std::size_t const base = kDependentMetaHeaderSize + index * entry_size;
        std::uint32_t const offset = get_u32(out.meta, base + 4);
        if (offset % row_size != 0) {
            throw Error(function + ": " + out.meta_path + " entry " +
                        std::to_string(index) + " has offset " +
                        std::to_string(offset) + ", not a multiple of the " +
                        std::to_string(row_size) + "-byte row size; refusing to "
                        "guess which row it tombstones");
        }
        std::size_t const row = offset / row_size;
        if (row >= rows) {
            throw Error(function + ": " + out.meta_path + " entry " +
                        std::to_string(index) + " names row " +
                        std::to_string(row) + ", past the last row " +
                        std::to_string(rows - 1) +
                        "; refusing to tombstone rows in a damaged dependent "
                        "table");
        }
        out.rows[row].entries.push_back(index);
        if (out.meta[base] == kStateLive) {
            out.rows[row].live = true;
        }
    }
    return out;
}

/* Rows the cascade must tombstone: a link whose predecessor OR successor is
 * `uid`, an assignment whose task uid is `uid`.  Rows whose entry is already a
 * tombstone are left exactly as they are (Project does the same: the deleted
 * task's stale assignment slot in w11-linkdelete-pre.mpp stays state 2). */
std::vector<std::size_t> dependent_entries_naming(DependentTable const& table,
                                                  std::uint32_t uid,
                                                  bool link_table) {
    std::vector<std::size_t> entries;
    for (DependentRow const& row : table.rows) {
        if (!row.live) {
            continue;
        }
        bool const named = link_table ? (row.predecessor == uid || row.successor == uid)
                                      : (row.task == uid);
        if (named) {
            entries.insert(entries.end(), row.entries.begin(), row.entries.end());
        }
    }
    return entries;
}

/* Tombstone FixedMeta entries and set the table's "contains deleted rows" flag.
 * Same contract as the Task table: entry +0 = 0x02, +2 = 0, header +4 |= 0x02.
 * The header bit is only ever set, never cleared (Project's own files carry
 * 0x04 with no tombstone and 0x06 as soon as one row is deleted); a table with
 * nothing to tombstone is not written at all, so it stays byte-identical. */
void apply_entry_tombstones(DependentTable& table,
                            std::vector<std::size_t> const& entries) {
    if (entries.empty()) {
        return;
    }
    for (std::size_t const index : entries) {
        std::size_t const base = kDependentMetaHeaderSize + index * table.entry_size;
        table.meta[base] = kStateDeleted;
        put_u16(table.meta, base + kFixedMetaFieldCount, 0);
    }
    put_u32(table.meta, kFixedMetaHeaderFlags,
            get_u32(table.meta, kFixedMetaHeaderFlags) | kFixedMetaFlagHasDeleted);
}

/*
 * Prove the cascade from the file on disk: the dependent FixedData is
 * byte-identical (no row was removed), every targeted FixedMeta entry reads
 * back 0x02 / field count 0, every other entry is byte-identical, and the
 * table's header gets bit 0x02 at +4 and nothing else.
 */
void verify_dependent_tombstones(CfbDocument const& reread,
                                 DependentTable const& before,
                                 std::vector<std::size_t> const& entries,
                                 std::string const& function) {
    if (before.fixed.empty() && before.meta.empty()) {
        return; /* the table has no rows (or no stream): nothing to prove */
    }
    if (!reread.has_stream(before.fixed_path)) {
        throw Error(function + ": " + before.fixed_path +
                    " vanished; the delete cascade must not remove a dependent "
                    "table");
    }
    std::vector<std::uint8_t> const fixed_after = reread.read_stream(before.fixed_path);
    if (fixed_after != before.fixed) {
        throw Error(function + ": " + before.fixed_path +
                    " changed; the delete cascade must tombstone metadata only, "
                    "never rewrite or remove a dependent row");
    }
    if (entries.empty()) {
        if (reread.read_stream(before.meta_path) != before.meta) {
            throw Error(function + ": " + before.meta_path +
                        " changed but nothing in it named the deleted uid");
        }
        return;
    }
    std::vector<std::uint8_t> const meta_after = reread.read_stream(before.meta_path);
    if (meta_after.size() != before.meta.size()) {
        throw Error(function + ": " + before.meta_path + " changed size (" +
                    std::to_string(meta_after.size()) + " vs " +
                    std::to_string(before.meta.size()) + ")");
    }
    std::size_t const count =
        (before.meta.size() - kDependentMetaHeaderSize) / before.entry_size;
    std::vector<bool> targeted(count, false);
    for (std::size_t const index : entries) {
        if (index >= count) {
            throw Error(function + ": internal error: tombstone entry " +
                        std::to_string(index) + " is outside " + before.meta_path);
        }
        targeted[index] = true;
    }
    for (std::size_t index = 0; index < count; ++index) {
        std::size_t const base = kDependentMetaHeaderSize + index * before.entry_size;
        if (targeted[index]) {
            if (meta_after[base] != kStateDeleted ||
                get_u16(meta_after, base + kFixedMetaFieldCount) != 0) {
                throw Error(function + ": " + before.meta_path + " entry " +
                            std::to_string(index) +
                            " does not read back as a tombstone (+0 0x02, +2 0)");
            }
            continue;
        }
        for (std::size_t byte = 0; byte < before.entry_size; ++byte) {
            if (meta_after[base + byte] != before.meta[base + byte]) {
                throw Error(function + ": " + before.meta_path + " entry " +
                            std::to_string(index) +
                            " changed but its row did not name the deleted uid");
            }
        }
    }
    for (std::size_t offset = 0; offset < kDependentMetaHeaderSize; ++offset) {
        if (offset == kFixedMetaHeaderFlags) {
            continue;
        }
        if (meta_after[offset] != before.meta[offset]) {
            throw Error(function + ": " + before.meta_path + " header byte +" +
                        std::to_string(offset) +
                        " changed; only the +4 flags word may be updated");
        }
    }
    std::uint32_t const flags_before = get_u32(before.meta, kFixedMetaHeaderFlags);
    std::uint32_t const flags_after = get_u32(meta_after, kFixedMetaHeaderFlags);
    if ((flags_after & kFixedMetaFlagHasDeleted) == 0) {
        throw Error(function + ": " + before.meta_path +
                    " header +4 does not have the \"contains deleted rows\" bit "
                    "(0x02); Project would ignore the tombstones");
    }
    if ((flags_after & ~kFixedMetaFlagHasDeleted) !=
        (flags_before & ~kFixedMetaFlagHasDeleted)) {
        throw Error(function + ": " + before.meta_path +
                    " header +4 changed a bit other than 0x02");
    }
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

    /* ---- VarMeta/Var2Data: append the 8 payload blocks and insert the 8
     * entries inside the new uid's group (key-ascending).  Project only honours
     * entries inside a uid's contiguous group (W48, measured: a custom-field
     * write that appended at the end reopened with empty values).  For the
     * usual new highest uid that is the end of the array; for a caller-supplied
     * mid-range uid the entries are inserted in uid order, which is exactly the
     * same rule table.h's insert_var_entry implements for field values. */
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
        insert_var_entry(var_meta, new_uid, key, offset);
    }
    put_u32(var_meta, 8,
            static_cast<std::uint32_t>(read_var_entries(var_meta).size()));
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

/* --- W33-1.2 (A-9): the clone source ------------------------------------- */

std::uint32_t last_live_task_uid(CfbDocument const& doc, std::string_view table) {
    std::string const function = "last_live_task_uid";
    TaskTable const rows = TaskTable::from_document(doc, table);
    std::vector<std::uint8_t> const meta =
        doc.read_stream(table_stream(table, "FixedMeta"));
    if (meta.size() < kFixedMetaHeaderSize ||
        (meta.size() - kFixedMetaHeaderSize) % kFixedMetaEntrySize != 0) {
        throw Error(function + ": FixedMeta is " + std::to_string(meta.size()) +
                    " bytes, not 16 + 47*n; the clone source cannot be read");
    }
    std::vector<MetaEntry> const meta_entries = read_meta_entries(meta);
    std::vector<std::uint8_t> const var_meta =
        doc.read_stream(table_stream(table, "VarMeta"));
    std::vector<VarEntry> const var_entries = read_var_entries(var_meta);
    /* The same scan add_task_row runs to pick its template (keep the two in
     * step): the last row > 0 that Project calls live and that has a variable
     * block group to clone. */
    for (std::size_t row = rows.rows(); row-- > 1;) {
        std::int32_t const index = entry_index_for_offset(
            meta_entries, static_cast<std::uint32_t>(task_row_offset(row)));
        if (index >= 0 &&
            meta_entries[static_cast<std::size_t>(index)].state == kStateLive &&
            uid_has_var_group(var_entries, rows.uid(row))) {
            return rows.uid(row);
        }
    }
    return 0;
}

bool insert_task_clone_is_clean(CfbDocument const& doc, std::uint32_t uid,
                                std::string& why, std::string_view table) {
    why.clear();
    std::string const function = "insert_task_clone_is_clean";
    TaskTable const rows = TaskTable::from_document(doc, table);
    std::size_t row = 0;
    try {
        row = rows.row_for_uid(uid);
    } catch (const std::exception&) {
        why = "has no saved task row";
        return false;
    }
    if (uid == 0 || row == 0) {
        why = "is the project row, not a task";
        return false;
    }
    std::vector<std::uint8_t> const meta =
        doc.read_stream(table_stream(table, "FixedMeta"));
    if (meta.size() < kFixedMetaHeaderSize ||
        (meta.size() - kFixedMetaHeaderSize) % kFixedMetaEntrySize != 0) {
        throw Error(function + ": FixedMeta is " + std::to_string(meta.size()) +
                    " bytes, not 16 + 47*n; the clone source cannot be read");
    }
    std::vector<MetaEntry> const meta_entries = read_meta_entries(meta);
    std::int32_t const index = entry_index_for_offset(
        meta_entries, static_cast<std::uint32_t>(task_row_offset(row)));
    if (index < 0 ||
        meta_entries[static_cast<std::size_t>(index)].state != kStateLive) {
        why = "is not a live saved task row";
        return false;
    }
    std::vector<std::uint8_t> const var_meta =
        doc.read_stream(table_stream(table, "VarMeta"));
    std::vector<VarEntry> const var_entries = read_var_entries(var_meta);
    if (!uid_has_var_group(var_entries, uid)) {
        why = "has no saved variable-field group";
        return false;
    }

    /* The clone copies every one of these words verbatim (add_delete.h: the
     * 202-byte row is cloned, and only +64/+66 and +174 are cleared). */
    std::uint16_t const percent = rows.u16(row, task_row::kPercentComplete);
    if (percent != 0) {
        why = "is " + std::to_string(percent) + " % complete";
        return false;
    }
    if (rows.u32(row, task_row::kActualDuration) != 0) {
        why = "records actual work time";
        return false;
    }
    std::uint16_t const percent_work =
        rows.u16(row, task_row::kPercentWorkComplete);
    if (percent_work != 0) {
        why = "records percent work complete (" + std::to_string(percent_work) +
              " %)";
        return false;
    }
    if (is_date(rows.u32(row, task_row::kActualStart)) ||
        is_date(rows.u32(row, task_row::kActualFinish))) {
        why = "records actual dates";
        return false;
    }
    if (is_date(rows.u32(row, task_row::kResume)) ||
        is_date(rows.u32(row, task_row::kStop))) {
        why = "records actual dates (Resume/Stop)";
        return false;
    }
    if (rows.u32(row, task_row::kCalendarUid) != 0xFFFFFFFFu) {
        why = "uses a task calendar";
        return false;
    }
    if (rows.u16(row, task_row::kConstraintType) != 0) {
        why = "has a constraint other than ASAP";
        return false;
    }
    /* Project's progressed-row block.  add_task_row copies only kNewRowKeys, so
     * a row that carries one would lose it in the clone -- the A-9 shape. */
    constexpr std::uint32_t kProgressBlockField = 0x0B40015Eu;
    if (!find_var_offsets(var_meta, kProgressBlockField, uid).empty()) {
        why = "carries a 0x0B40015E progress step block";
        return false;
    }
    /* The five overtime work/cost fields ARE among the copied keys; a fresh
     * Project row stores them as eight zero bytes (w10-adddel-base.mpp). */
    std::vector<std::uint8_t> const var2 =
        doc.read_stream(table_stream(table, "Var2Data"));
    for (std::uint32_t const key : {0x0B4000A4u, 0x0B4000A5u, 0x0B4000A8u,
                                    0x0B4000A9u, 0x0B4000AAu}) {
        for (VarEntry const& entry : var_entries) {
            if (entry.field_id != key) {
                continue;
            }
            if (entry.mid != uid && entry.mid != (uid | kSecondaryUidFamily)) {
                continue;
            }
            if (static_cast<std::size_t>(entry.offset) + 4 > var2.size()) {
                continue; /* template_payload skips this block too */
            }
            std::vector<std::uint8_t> const payload =
                read_var_block(var2, entry.offset);
            if (std::any_of(payload.begin(), payload.end(),
                            [](std::uint8_t byte) { return byte != 0; })) {
                why = "carries overtime work or cost values";
                return false;
            }
        }
    }
    return true;
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

    /* ---- Liveness, computed once: the same two facts the reader uses -- a
     * live FixedMeta entry and a VarMeta group.  Both the cascade and the ID
     * renumbering below need it. */
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
    std::vector<std::uint32_t> live_uids;
    live_uids.reserve(rows);
    for (std::size_t candidate = 1; candidate < rows; ++candidate) {
        if (live[candidate] && candidate != row) {
            live_uids.push_back(before.uid(candidate));
        }
    }
    auto is_live_task = [&live_uids](std::uint32_t value) {
        return std::find(live_uids.begin(), live_uids.end(), value) !=
               live_uids.end();
    };

    /* W33-1.1 (A-3, High): a summary row cannot be tombstoned on its own.
     * Project reads a row as a summary from its LIVE subtasks; tombstoning the
     * parent alone leaves them with a parent identity pointing at a tombstone,
     * a shape no Project save pair measured.  app::push() refuses this first;
     * this is defence in depth for every other caller. */
    {
        std::vector<OutlineRow> outline_rows;
        std::string outline_error;
        if (!read_outline_rows(doc, table, outline_rows, outline_error)) {
            throw Error(function + ": the task outline cannot be read, so a "
                        "summary delete cannot be ruled out (" + outline_error +
                        ")");
        }
        std::size_t target = outline_rows.size();
        for (std::size_t candidate = 0; candidate < outline_rows.size();
             ++candidate) {
            if (outline_rows[candidate].uid == uid) {
                target = candidate;
                break;
            }
        }
        std::vector<std::size_t> live_children;
        if (target < outline_rows.size()) {
            for (std::size_t child : outline_rows[target].child_rows) {
                if (child < live.size() && child != target && live[child] &&
                    std::find(live_children.begin(), live_children.end(),
                              child) == live_children.end()) {
                    live_children.push_back(child);
                }
            }
            // A stored child list can name a live row whose parent identity is
            // absent (a stale shape Project also reads), so it is checked too.
            for (std::uint32_t id : outline_rows[target].stored_child_ids) {
                for (std::size_t candidate = 1; candidate < outline_rows.size();
                     ++candidate) {
                    if (candidate == target || !live[candidate] ||
                        outline_rows[candidate].row_id != id) {
                        continue;
                    }
                    if (std::find(live_children.begin(), live_children.end(),
                                  candidate) == live_children.end()) {
                        live_children.push_back(candidate);
                    }
                }
            }
        }
        if (!live_children.empty()) {
            throw Error(function + ": uid " + std::to_string(uid) + " has " +
                        std::to_string(live_children.size()) +
                        " live subtask(s); delete the subtasks first, or "
                        "outdent them");
        }
    }

    /* ---- The delete cascade (measured, w11-oracle §2).  Project tombstones
     * every link row whose predecessor OR successor is the deleted uid and every
     * assignment row whose task uid is, sets each table's FixedMeta header +4
     * bit 0x02, and leaves every row and every FixedData byte in place. */
    DependentTable cons = load_dependent_table(
        doc, "114/TBkndCons", kConsRowSize, kConsMetaEntrySize, function);
    DependentTable assn = load_dependent_table(
        doc, "114/TBkndAssn", kAssnRowSize, kAssnMetaEntrySize, function);
    std::vector<std::size_t> const cons_tombstones =
        dependent_entries_naming(cons, uid, true);
    std::vector<std::size_t> const assn_tombstones =
        dependent_entries_naming(assn, uid, false);

    std::vector<std::string> warnings;

    /* A surviving live link whose OTHER endpoint already names a dead uid is
     * damage that predates this delete.  The writer cannot repair it, so the
     * default refuses; allow_dangling accepts the orphan and reports it.  Links
     * naming the uid being deleted were selected by the cascade above. */
    std::size_t dangling_links = 0;
    std::string first_dangling_link;
    for (DependentRow const& link : cons.rows) {
        if (!link.live || link.predecessor == uid || link.successor == uid) {
            continue;
        }
        bool const predecessor_dead =
            link.predecessor == 0 || !is_live_task(link.predecessor);
        bool const successor_dead =
            link.successor == 0 || !is_live_task(link.successor);
        if (!predecessor_dead && !successor_dead) {
            continue;
        }
        ++dangling_links;
        if (first_dangling_link.empty()) {
            first_dangling_link =
                "link row " + std::to_string(link.row) + " (uid " +
                std::to_string(link.uid) + ", " +
                std::to_string(link.predecessor) + " -> " +
                std::to_string(link.successor) + ")";
        }
    }
    if (dangling_links > 0 && !options.allow_dangling) {
        throw Error(function + ": deleting uid " + std::to_string(uid) +
                    " would leave " + std::to_string(dangling_links) +
                    " live link row(s) whose other endpoint already names a uid "
                    "no live task holds (the first is " + first_dangling_link +
                    "); the file was damaged before this delete. Set "
                    "DeleteOptions::allow_dangling to proceed and warn instead "
                    "(the writer cannot repair a dead link endpoint)");
    }
    if (dangling_links > 0) {
        warnings.push_back("uid " + std::to_string(uid) + ": " +
                           std::to_string(dangling_links) +
                           " live link row(s) name an endpoint that no live task "
                           "holds (the first is " + first_dangling_link +
                           "); they are left in place because the damaged endpoint "
                           "predates this delete");
    }

    /* Live assignments naming an already-dead task uid are tolerated, not
     * refused: PROJECT_B stores 10 of 73 that way and Project reopens it. */
    std::size_t orphan_assignments = 0;
    for (DependentRow const& assignment : assn.rows) {
        if (!assignment.live || assignment.task == uid || assignment.task == 0) {
            continue;
        }
        if (!is_live_task(assignment.task)) {
            ++orphan_assignments;
        }
    }
    if (orphan_assignments > 0) {
        warnings.push_back("uid " + std::to_string(uid) + ": " +
                           std::to_string(orphan_assignments) +
                           " other live assignment row(s) name a task uid no live "
                           "task holds; they are left in place (PROJECT_B stores 10 of "
                           "73 that way and Project reopens it)");
    }
    if (!cons_tombstones.empty() || !assn_tombstones.empty()) {
        warnings.push_back(
            "uid " + std::to_string(uid) + ": cascade tombstoned " +
            std::to_string(cons_tombstones.size()) + " link entry(ies) and " +
            std::to_string(assn_tombstones.size()) +
            " assignment entry(ies) that named this task (FixedMeta entry +0 = "
            "0x02, +2 = 0; header +4 |= 0x02 in each table; no row removed)");
    }

    /* ---- Renumber later live rows' ID down by one (Project: uid 5's 5 -> 4). */
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
    apply_entry_tombstones(cons, cons_tombstones);
    apply_entry_tombstones(assn, assn_tombstones);
    if (!cons_tombstones.empty()) {
        work.replace_stream(cons.meta_path, cons.meta);
    }
    if (!assn_tombstones.empty()) {
        work.replace_stream(assn.meta_path, assn.meta);
    }

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

    /* The cascade, proved from the written file: dependent rows untouched,
     * targeted entries tombstoned, header flags set, nothing else moved. */
    verify_dependent_tombstones(reread, cons, cons_tombstones, function);
    verify_dependent_tombstones(reread, assn, assn_tombstones, function);

    DeletedRow result;
    result.uid = uid;
    result.id = old_id;
    result.row = row;
    result.var_entries_removed = removed;
    result.link_rows_tombstoned = cons_tombstones.size();
    result.assignment_rows_tombstoned = assn_tombstones.size();
    result.warnings = std::move(warnings);
    doc = reread;
    return result;
}

/* ---- link rows: 114/TBkndCons (W52) ------------------------------------ */

namespace {

constexpr std::size_t kLinkMetaMagicOffset = 0;
constexpr std::size_t kLinkMetaCount = 8;
constexpr std::size_t kLinkMetaFixedSize = 12;
constexpr std::size_t kLinkMetaHeaderFlags = 4;
constexpr std::size_t kLinkEntryState = 0;
constexpr std::size_t kLinkEntryFieldCount = 2;
constexpr std::size_t kLinkEntryRowOffset = 4;
constexpr std::size_t kLinkEntryTailA = 8;
constexpr std::size_t kLinkEntryTailB = 9;
constexpr std::uint8_t kLinkTailByteA = 0xDD;
constexpr std::uint8_t kLinkTailByteB = 0x00;

constexpr std::size_t kLinkRowUid = 0;
constexpr std::size_t kLinkRowType = 12;
constexpr std::size_t kLinkRowLag = 14;
constexpr std::size_t kLinkRowLagUnits = 18;

constexpr std::size_t kLinkF2MetaHeaderSize = 16;
constexpr std::size_t kLinkF2MetaMagicOffset = 0;
constexpr std::size_t kLinkF2MetaCount = 8;
constexpr std::size_t kLinkF2MetaFixedSize = 12;
constexpr std::size_t kLinkF2MetaRecordOffset = 4;
constexpr std::size_t kLinkF2MetaFlag = 8;

constexpr std::uint32_t kDependentMagic = 0xFADFADBAu;

using LinkIdentity = std::array<std::uint8_t, kLinkIdentitySize>;

bool identity_is_zero(LinkIdentity const& identity) {
    return std::all_of(identity.begin(), identity.end(),
                       [](std::uint8_t byte) { return byte == 0; });
}

bool identity_family_matches(std::uint8_t const* bytes,
                             LinkIdentity const& templ) {
    for (std::size_t i = 0; i < kLinkIdentitySize; ++i) {
        if (i == 3) {
            continue; /* byte +3 is the varying counter in the measured files */
        }
        if (bytes[i] != templ[i]) {
            return false;
        }
    }
    return true;
}

/*
 * The 16-byte identity of a Task row: Task/Fixed2Data record `row + 3`, bytes
 * +0..+15 (spec/mpp-records.md §7; the same identity Project's link
 * Fixed2Data copies for its two endpoints).
 */
LinkIdentity task_identity(CfbDocument const& doc, std::size_t row,
                           std::string const& function) {
    std::vector<std::uint8_t> const fixed2 = doc.read_stream("114/TBkndTask/Fixed2Data");
    std::size_t const base = (row + 3) * kFixed2RecordSize;
    if (base + kLinkIdentitySize > fixed2.size()) {
        throw Error(function + ": Task/Fixed2Data is " +
                    std::to_string(fixed2.size()) + " bytes, too short for row " +
                    std::to_string(row) + "'s identity record");
    }
    LinkIdentity identity{};
    std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base),
              fixed2.begin() + static_cast<std::ptrdiff_t>(base + kLinkIdentitySize),
              identity.begin());
    if (identity_is_zero(identity)) {
        throw Error(function + ": task row " + std::to_string(row) +
                    " has an all-zero Task/Fixed2Data identity, so there is no "
                    "measured identity to copy into the link's Fixed2Data record");
    }
    return identity;
}

/* The first non-zero task identity: the template for a new link identity. */
LinkIdentity first_task_identity(CfbDocument const& doc,
                                 std::string const& function) {
    std::vector<std::uint8_t> const fixed2 = doc.read_stream("114/TBkndTask/Fixed2Data");
    for (std::size_t record = 3; record * kFixed2RecordSize + kLinkIdentitySize <=
                                     fixed2.size();
         ++record) {
        LinkIdentity identity{};
        std::size_t const base = record * kFixed2RecordSize;
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base),
                  fixed2.begin() +
                      static_cast<std::ptrdiff_t>(base + kLinkIdentitySize),
                  identity.begin());
        if (!identity_is_zero(identity)) {
            return identity;
        }
    }
    throw Error(function + ": the Task table has no non-zero Fixed2Data identity "
                "to derive a link identity from; refusing to invent one");
}

/*
 * Every identity of the same GUID family anywhere in the file (every Fixed2Data
 * stream, every 16-byte slot), so the generated link identity cannot collide
 * with a task, resource, assignment, calendar or already-written link identity.
 */
void collect_used_identity_counters(CfbDocument const& doc,
                                    LinkIdentity const& templ,
                                    std::array<bool, 256>& used) {
    constexpr std::string_view suffix = "/Fixed2Data";
    for (std::string const& path : doc.stream_paths()) {
        if (path.size() < suffix.size() ||
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        std::vector<std::uint8_t> const data = doc.read_stream(path);
        for (std::size_t offset = 0; offset + kLinkIdentitySize <= data.size();
             offset += kLinkIdentitySize) {
            if (identity_family_matches(data.data() + offset, templ)) {
                used[data[offset + 3]] = true;
            }
        }
    }
}

LinkIdentity make_link_identity(CfbDocument const& doc,
                                std::string const& function) {
    LinkIdentity identity = first_task_identity(doc, function);
    std::array<bool, 256> used{};
    collect_used_identity_counters(doc, identity, used);
    for (unsigned value = 1; value < 256; ++value) {
        if (!used[value]) {
            identity[3] = static_cast<std::uint8_t>(value);
            return identity;
        }
    }
    throw Error(function + ": every identity counter value (1..255) of this "
                "file's GUID family is already used; the link table has no room "
                "for a new unique identity");
}

/* The four link-table streams plus the strictly validated geometry. */
struct LinkTable {
    DependentTable rows;
    std::string fixed2_path;
    std::string fixed2_meta_path;
    std::vector<std::uint8_t> fixed2;
    std::vector<std::uint8_t> fixed2_meta;
};

/*
 * Read and strictly validate the link table.  Unlike the delete cascade (which
 * only rewrites entry bytes and therefore tolerates a stale declared count),
 * a link ADD does arithmetic on every count/size word, so this refuses any
 * disagreement instead of guessing.  Measured shapes: FixedData 20*n,
 * FixedMeta 16+10*n, Fixed2Data 48*n, Fixed2Meta 16+9*n (link1.mpp, w12-linkadd
 * and w11-linkdelete-pre/-post.mpp).
 */
LinkTable load_link_table(CfbDocument const& doc, std::string const& function) {
    LinkTable out;
    out.rows = load_dependent_table(doc, "114/TBkndCons", kLinkRowSize,
                                    kLinkMetaEntrySize, function);
    if (!doc.has_stream(out.rows.fixed_path)) {
        throw Error(function + ": the file has no " + out.rows.fixed_path +
                    " stream; refusing to add a row to a missing link table");
    }
    if (out.rows.meta.empty()) {
        throw Error(function + ": the file has no " + out.rows.meta_path +
                    " stream; a link table without FixedMeta is damaged");
    }
    std::vector<std::uint8_t> const& fixed = out.rows.fixed;
    std::vector<std::uint8_t> const& meta = out.rows.meta;
    std::size_t const rows = fixed.size() / kLinkRowSize;
    std::size_t const entries =
        (meta.size() - kDependentMetaHeaderSize) / kLinkMetaEntrySize;
    if (entries != rows) {
        throw Error(function + ": " + out.rows.meta_path + " holds " +
                    std::to_string(entries) + " entries for " +
                    std::to_string(rows) +
                    " rows; the link table has exactly one entry per row, so this "
                    "table is damaged");
    }
    if (get_u32(meta, kLinkMetaMagicOffset) != kDependentMagic) {
        throw Error(function + ": " + out.rows.meta_path +
                    " does not start with the measured table magic");
    }
    if (get_u32(meta, kLinkMetaCount) != rows) {
        throw Error(function + ": " + out.rows.meta_path + " declares " +
                    std::to_string(get_u32(meta, kLinkMetaCount)) + " entries but "
                    "the table has " + std::to_string(rows) +
                    "; refusing to append to a damaged link table");
    }
    if (get_u32(meta, kLinkMetaFixedSize) != fixed.size()) {
        throw Error(function + ": " + out.rows.meta_path + " declares a " +
                    std::to_string(get_u32(meta, kLinkMetaFixedSize)) +
                    "-byte FixedData but the stream is " +
                    std::to_string(fixed.size()) +
                    " bytes; refusing to append to a damaged link table");
    }
    for (std::size_t row = 0; row < rows; ++row) {
        if (out.rows.rows[row].entries.size() != 1) {
            throw Error(function + ": link row " + std::to_string(row) + " has " +
                        std::to_string(out.rows.rows[row].entries.size()) +
                        " FixedMeta entries; the link table has exactly one per row");
        }
        std::size_t const index = out.rows.rows[row].entries.front();
        std::uint8_t const state =
            meta[kDependentMetaHeaderSize + index * kLinkMetaEntrySize];
        if (state != kStateLive && state != kStateDeleted) {
            throw Error(function + ": link row " + std::to_string(row) +
                        " has FixedMeta state 0x" + std::to_string(state) +
                        "; only 0x00 (live) and 0x02 (tombstone) are measured");
        }
    }

    out.fixed2_path = "114/TBkndCons/Fixed2Data";
    out.fixed2_meta_path = "114/TBkndCons/Fixed2Meta";
    if (!doc.has_stream(out.fixed2_path) ||
        !doc.has_stream(out.fixed2_meta_path)) {
        throw Error(function + ": the link table is missing " +
                    out.fixed2_path + " or " + out.fixed2_meta_path +
                    "; Project's own link tables always carry both");
    }
    out.fixed2 = doc.read_stream(out.fixed2_path);
    out.fixed2_meta = doc.read_stream(out.fixed2_meta_path);
    if (out.fixed2.size() != rows * kLinkFixed2RecordSize) {
        throw Error(function + ": " + out.fixed2_path + " is " +
                    std::to_string(out.fixed2.size()) + " bytes, not " +
                    std::to_string(rows) + "*48; refusing to append to a damaged "
                    "link table");
    }
    if (out.fixed2_meta.size() !=
        kLinkF2MetaHeaderSize + rows * kLinkFixed2MetaEntrySize) {
        throw Error(function + ": " + out.fixed2_meta_path + " is " +
                    std::to_string(out.fixed2_meta.size()) + " bytes, not 16 + " +
                    std::to_string(rows) + "*9; refusing to append to a damaged "
                    "link table");
    }
    if (get_u32(out.fixed2_meta, kLinkF2MetaMagicOffset) != kDependentMagic ||
        get_u32(out.fixed2_meta, kLinkF2MetaCount) != rows ||
        get_u32(out.fixed2_meta, kLinkF2MetaFixedSize) != out.fixed2.size()) {
        throw Error(function + ": " + out.fixed2_meta_path +
                    " header counts do not match the link table; refusing to "
                    "append to a damaged link table");
    }
    for (std::size_t row = 0; row < rows; ++row) {
        std::size_t const base =
            kLinkF2MetaHeaderSize + row * kLinkFixed2MetaEntrySize;
        if (get_u32(out.fixed2_meta, base + kLinkF2MetaRecordOffset) !=
            row * kLinkFixed2RecordSize) {
            throw Error(function + ": " + out.fixed2_meta_path + " entry " +
                        std::to_string(row) + " does not name Fixed2Data record " +
                        std::to_string(row) +
                        "; refusing to append to a damaged link table");
        }
    }
    return out;
}

/* A live task row, as the link endpoint check needs it. */
struct LiveTaskRow {
    std::uint32_t uid = 0;
    std::size_t row = 0;
};

/*
 * The link add only READS the Task table (endpoint liveness and two identities),
 * so it validates only the geometry it touches.  It deliberately does NOT run the
 * full `validate_table`: Project's own corpus/probe/w12-linkadd.mpp declares 38
 * VarMeta entries while the array holds 42 (Project's normal stale-count save
 * shape, also seen on its add/delete files).  A write that never touches VarMeta
 * is safe on such a file; genuinely broken geometry is still refused.
 */
void validate_link_task_table(CfbDocument const& doc,
                              std::string const& function) {
    TaskTable const table = TaskTable::from_document(doc);
    std::size_t const rows = table.rows();
    for (char const* name : {"FixedData", "FixedMeta", "Fixed2Data"}) {
        std::string const path = std::string("114/TBkndTask/") + name;
        if (!doc.has_stream(path)) {
            throw Error(function + ": the Task table has no " + path +
                        " stream; the link endpoint identities and liveness "
                        "cannot be read");
        }
    }
    std::vector<std::uint8_t> const meta =
        doc.read_stream("114/TBkndTask/FixedMeta");
    if (meta.size() < kFixedMetaHeaderSize ||
        meta.size() != kFixedMetaHeaderSize + kFixedMetaEntrySize * (rows + 3)) {
        throw Error(function + ": Task/FixedMeta is " +
                    std::to_string(meta.size()) + " bytes, not 16 + 47*" +
                    std::to_string(rows + 3) +
                    "; refusing to add a link to a damaged Task table");
    }
    std::vector<std::uint8_t> const var_meta =
        doc.read_stream("114/TBkndTask/VarMeta");
    if (var_meta.size() < kVarMetaHeaderSize ||
        (var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
        throw Error(function + ": Task/VarMeta is " +
                    std::to_string(var_meta.size()) +
                    " bytes, not 24 + 12*n; refusing to add a link to a damaged "
                    "Task table");
    }
    (void)read_var_entries(var_meta);
    std::vector<std::uint8_t> const fixed2 =
        doc.read_stream("114/TBkndTask/Fixed2Data");
    if (fixed2.size() != kFixed2RecordSize * (rows + 3)) {
        throw Error(function + ": Task/Fixed2Data is " +
                    std::to_string(fixed2.size()) + " bytes, not 64*" +
                    std::to_string(rows + 3) +
                    "; refusing to add a link to a damaged Task table");
    }
}

std::vector<LiveTaskRow> live_task_rows(CfbDocument const& doc) {
    TaskTable const table = TaskTable::from_document(doc);
    std::vector<MetaEntry> const meta_entries =
        read_meta_entries(doc.read_stream("114/TBkndTask/FixedMeta"));
    std::vector<VarEntry> const var_entries =
        read_var_entries(doc.read_stream("114/TBkndTask/VarMeta"));
    std::vector<LiveTaskRow> out;
    for (std::size_t row = 1; row < table.rows(); ++row) {
        std::int32_t const index = entry_index_for_offset(
            meta_entries, static_cast<std::uint32_t>(task_row_offset(row)));
        if (index < 0 ||
            meta_entries[static_cast<std::size_t>(index)].state != kStateLive) {
            continue;
        }
        std::uint32_t const uid = table.uid(row);
        if (!uid_has_var_group(var_entries, uid)) {
            continue;
        }
        LiveTaskRow live;
        live.uid = uid;
        live.row = row;
        out.push_back(live);
    }
    return out;
}

std::string link_type_name(std::uint16_t type) {
    switch (type) {
        case kLinkTypeFF:
            return "FF";
        case kLinkTypeFS:
            return "FS";
        case kLinkTypeSF:
            return "SF";
        case kLinkTypeSS:
            return "SS";
        default:
            return "?";
    }
}

} /* namespace */

AddedLink add_task_link(CfbDocument& doc, std::uint32_t predecessor_uid,
                        std::uint32_t successor_uid, std::uint16_t type,
                        std::int32_t lag_ticks, std::string const& path,
                        std::uint16_t lag_format) {
    std::string const function = "add_task_link";
    check_output_path(path, function);
    if (type > kLinkTypeSS) {
        throw Error(function + ": link type " + std::to_string(type) +
                    " is not one of the measured values 0 (FF), 1 (FS), 2 (SF), "
                    "3 (SS)");
    }
    if (lag_ticks < -kMaxLinkLagTicks || lag_ticks > kMaxLinkLagTicks) {
        throw Error(function + ": lag " + std::to_string(lag_ticks) +
                    " ticks is outside the writer's +/-" +
                    std::to_string(kMaxLinkLagTicks) +
                    "-tick bound (a policy guard for an implausible lag, not a "
                    "measured Project limit)");
    }
    if (predecessor_uid == 0 || successor_uid == 0) {
        throw Error(function + ": uid 0 is the project row and cannot be a link "
                    "endpoint");
    }
    if (predecessor_uid == successor_uid) {
        throw Error(function + ": uid " + std::to_string(predecessor_uid) +
                    " cannot be its own predecessor and successor (a self-link)");
    }
    validate_link_task_table(doc, function);
    LinkTable links = load_link_table(doc, function);

    std::vector<LiveTaskRow> const live_tasks = live_task_rows(doc);
    auto find_row = [&live_tasks](std::uint32_t uid) {
        for (LiveTaskRow const& live : live_tasks) {
            if (live.uid == uid) {
                return live.row;
            }
        }
        return std::size_t{0};
    };
    std::size_t const predecessor_row = find_row(predecessor_uid);
    if (predecessor_row == 0) {
        throw Error(function + ": predecessor uid " + std::to_string(predecessor_uid) +
                    " has no live task row (unknown uid, a tombstone, or a row "
                    "with no VarMeta group)");
    }
    std::size_t const successor_row = find_row(successor_uid);
    if (successor_row == 0) {
        throw Error(function + ": successor uid " + std::to_string(successor_uid) +
                    " has no live task row (unknown uid, a tombstone, or a row "
                    "with no VarMeta group)");
    }

    std::size_t const rows = links.rows.rows.size();
    bool successor_has_live_link = false;
    for (std::size_t row = 0; row < rows; ++row) {
        if (!links.rows.rows[row].live) {
            continue;
        }
        std::size_t const base = row * kLinkRowSize;
        if (get_u32(links.rows.fixed, base + kConsSuccessor) == successor_uid) {
            successor_has_live_link = true;
        }
        if (get_u32(links.rows.fixed, base + kConsPredecessor) != predecessor_uid ||
            get_u32(links.rows.fixed, base + kConsSuccessor) != successor_uid) {
            continue;
        }
        if (get_u16(links.rows.fixed, base + kLinkRowType) == type) {
            throw Error(function + ": a live link row already carries " +
                        std::to_string(predecessor_uid) + " -> " +
                        std::to_string(successor_uid) + " " + link_type_name(type) +
                        " (row " + std::to_string(row) + ", uid " +
                        std::to_string(get_u32(links.rows.fixed, base + kLinkRowUid)) +
                        "); a duplicate relation is refused");
        }
    }

    /* W30-0.7 (R29-7): Wave 29 measured Fixed2Meta +8 as the successor's
     * driving state, not a first/later position -- in the percent-mid25 control
     * the first of three links into distinct successors is 0x07 because that
     * successor has progress, and the work pair moves 0x0f -> 0x07 when its
     * successor enters progress.  The wave-47 rule this function used to write
     * (0x07 whenever a live link already names the successor, 0x0f for a
     * progressed successor's first link) is falsified, and no Project save pair
     * adds either shape.  Both are refused instead of inventing the byte; the
     * two measured adds (a first link into a 0 % successor) still write 0x0f. */
    TaskTable const successor_table = TaskTable::from_document(doc);
    std::uint16_t const successor_percent =
        successor_table.u16(successor_row, task_row::kPercentComplete);
    if (successor_has_live_link) {
        throw Error(function + ": successor uid " + std::to_string(successor_uid) +
                    " already has a live incoming link; the Fixed2Meta +8 byte "
                    "for a second incoming link is not measured (Wave 29 "
                    "falsified the later-link rule), so the add is refused");
    }
    if (successor_percent != 0) {
        throw Error(function + ": successor uid " + std::to_string(successor_uid) +
                    " is " + std::to_string(successor_percent) +
                    "% complete; no Project save pair adds a link into a "
                    "progressed successor, so the add is refused");
    }

    std::uint32_t max_uid = 0;
    for (std::size_t row = 0; row < rows; ++row) {
        std::uint32_t const uid =
            get_u32(links.rows.fixed, row * kLinkRowSize + kLinkRowUid);
        max_uid = std::max(max_uid, uid);
    }
    if (max_uid == 0xFFFFFFFFu) {
        throw Error(function + ": the link table's largest uid is 0xffffffff, so "
                    "a new link uid would overflow");
    }
    std::uint32_t const uid = max_uid + 1;

    LinkIdentity const predecessor_identity =
        task_identity(doc, predecessor_row, function);
    LinkIdentity const successor_identity =
        task_identity(doc, successor_row, function);
    LinkIdentity const link_identity = make_link_identity(doc, function);

    /* Fixed2Meta +8: every add this function allows is a first link into a
     * 0 % successor, the only shape the two measured adds cover, and both
     * wrote 0x0f.  0x07 is the measured progress-transition state, written by
     * the actuals path when a successor enters progress -- not here. */
    std::uint8_t const flag = kLinkFixed2BaseFlag;

    std::vector<std::uint8_t> fixed = links.rows.fixed;
    std::vector<std::uint8_t> meta = links.rows.meta;
    std::vector<std::uint8_t> fixed2 = links.fixed2;
    std::vector<std::uint8_t> fixed2_meta = links.fixed2_meta;

    std::vector<std::uint8_t> row_bytes(kLinkRowSize, 0);
    put_u32(row_bytes, kLinkRowUid, uid);
    put_u32(row_bytes, kConsPredecessor, predecessor_uid);
    put_u32(row_bytes, kConsSuccessor, successor_uid);
    put_u16(row_bytes, kLinkRowType, type);
    put_u32(row_bytes, kLinkRowLag, static_cast<std::uint32_t>(lag_ticks));
    put_u16(row_bytes, kLinkRowLagUnits, lag_format);
    fixed.insert(fixed.end(), row_bytes.begin(), row_bytes.end());

    std::vector<std::uint8_t> entry(kLinkMetaEntrySize, 0);
    entry[kLinkEntryState] = kStateLive;
    put_u16(entry, kLinkEntryFieldCount, 0);
    put_u32(entry, kLinkEntryRowOffset,
            static_cast<std::uint32_t>(rows * kLinkRowSize));
    entry[kLinkEntryTailA] = kLinkTailByteA;
    entry[kLinkEntryTailB] = kLinkTailByteB;
    meta.insert(meta.end(), entry.begin(), entry.end());
    put_u32(meta, kLinkMetaCount, static_cast<std::uint32_t>(rows + 1));
    put_u32(meta, kLinkMetaFixedSize, static_cast<std::uint32_t>(fixed.size()));

    LinkIdentity const* identities[3] = {&link_identity, &predecessor_identity,
                                         &successor_identity};
    for (LinkIdentity const* identity : identities) {
        fixed2.insert(fixed2.end(), identity->begin(), identity->end());
    }
    std::vector<std::uint8_t> fixed2_entry(kLinkFixed2MetaEntrySize, 0);
    put_u32(fixed2_entry, kLinkF2MetaRecordOffset,
            static_cast<std::uint32_t>(rows * kLinkFixed2RecordSize));
    fixed2_entry[kLinkF2MetaFlag] = flag;
    fixed2_meta.insert(fixed2_meta.end(), fixed2_entry.begin(), fixed2_entry.end());
    put_u32(fixed2_meta, kLinkF2MetaCount, static_cast<std::uint32_t>(rows + 1));
    put_u32(fixed2_meta, kLinkF2MetaFixedSize,
            static_cast<std::uint32_t>(fixed2.size()));

    CfbDocument work = CfbDocument::from_bytes(doc.to_bytes());
    work.replace_stream(links.rows.fixed_path, fixed);
    work.replace_stream(links.rows.meta_path, meta);
    work.replace_stream(links.fixed2_path, fixed2);
    work.replace_stream(links.fixed2_meta_path, fixed2_meta);

    CfbDocument const reread = save_and_verify(work, path, function, "114/TBkndTask");

    /* Prove the four written streams byte for byte, and that nothing else moved. */
    if (reread.read_stream(links.rows.fixed_path) != fixed ||
        reread.read_stream(links.rows.meta_path) != meta ||
        reread.read_stream(links.fixed2_path) != fixed2 ||
        reread.read_stream(links.fixed2_meta_path) != fixed2_meta) {
        throw Error(function + ": the written link streams do not read back with "
                    "the bytes that were built");
    }
    for (std::string const& path_name : doc.stream_paths()) {
        if (path_name == links.rows.fixed_path || path_name == links.rows.meta_path ||
            path_name == links.fixed2_path ||
            path_name == links.fixed2_meta_path) {
            continue;
        }
        if (!reread.has_stream(path_name) ||
            reread.read_stream(path_name) != doc.read_stream(path_name)) {
            throw Error(function + ": " + path_name +
                        " changed; adding a link may only touch the four "
                        "TBkndCons streams");
        }
    }

    std::vector<std::uint8_t> const fixed_after = reread.read_stream(links.rows.fixed_path);
    std::vector<std::uint8_t> const meta_after = reread.read_stream(links.rows.meta_path);
    std::vector<std::uint8_t> const fixed2_after =
        reread.read_stream(links.fixed2_path);
    std::vector<std::uint8_t> const fixed2_meta_after =
        reread.read_stream(links.fixed2_meta_path);
    std::size_t const new_row = fixed_after.size() / kLinkRowSize - 1;
    if (get_u32(meta_after, kLinkMetaCount) != rows + 1 ||
        get_u32(meta_after, kLinkMetaFixedSize) != fixed_after.size() ||
        get_u32(fixed2_meta_after, kLinkF2MetaCount) != rows + 1 ||
        get_u32(fixed2_meta_after, kLinkF2MetaFixedSize) != fixed2_after.size()) {
        throw Error(function + ": a link-table header count/size word does not "
                    "read back as written");
    }
    std::size_t const new_base = new_row * kLinkRowSize;
    std::size_t const new_entry_base =
        kDependentMetaHeaderSize + new_row * kLinkMetaEntrySize;
    if (get_u32(fixed_after, new_base + kLinkRowUid) != uid ||
        get_u32(fixed_after, new_base + kConsPredecessor) != predecessor_uid ||
        get_u32(fixed_after, new_base + kConsSuccessor) != successor_uid ||
        get_u16(fixed_after, new_base + kLinkRowType) != type ||
        static_cast<std::int32_t>(get_u32(fixed_after, new_base + kLinkRowLag)) !=
            lag_ticks ||
        get_u16(fixed_after, new_base + kLinkRowLagUnits) !=
            lag_format) {
        throw Error(function + ": the new link row does not read back as written");
    }
    if (meta_after[new_entry_base + kLinkEntryState] != kStateLive ||
        get_u16(meta_after, new_entry_base + kLinkEntryFieldCount) != 0 ||
        get_u32(meta_after, new_entry_base + kLinkEntryRowOffset) !=
            static_cast<std::uint32_t>(new_base) ||
        meta_after[new_entry_base + kLinkEntryTailA] != kLinkTailByteA ||
        meta_after[new_entry_base + kLinkEntryTailB] != kLinkTailByteB) {
        throw Error(function + ": the new link FixedMeta entry does not read back "
                    "as written");
    }
    if (!std::equal(link_identity.begin(), link_identity.end(),
                    fixed2_after.begin() +
                        static_cast<std::ptrdiff_t>(new_row * kLinkFixed2RecordSize)) ||
        !std::equal(predecessor_identity.begin(), predecessor_identity.end(),
                    fixed2_after.begin() + static_cast<std::ptrdiff_t>(
                                               new_row * kLinkFixed2RecordSize + 16)) ||
        !std::equal(successor_identity.begin(), successor_identity.end(),
                    fixed2_after.begin() + static_cast<std::ptrdiff_t>(
                                               new_row * kLinkFixed2RecordSize + 32))) {
        throw Error(function + ": the new link Fixed2Data record does not read back "
                    "as written");
    }
    std::size_t const new_f2_entry_base =
        kLinkF2MetaHeaderSize + new_row * kLinkFixed2MetaEntrySize;
    if (get_u32(fixed2_meta_after, new_f2_entry_base + kLinkF2MetaRecordOffset) !=
            static_cast<std::uint32_t>(new_row * kLinkFixed2RecordSize) ||
        fixed2_meta_after[new_f2_entry_base + kLinkF2MetaFlag] != flag) {
        throw Error(function + ": the new link Fixed2Meta entry does not read back "
                    "as written");
    }

    AddedLink result;
    result.uid = uid;
    result.predecessor_uid = predecessor_uid;
    result.successor_uid = successor_uid;
    result.type = type;
    result.lag_ticks = lag_ticks;
    result.lag_format = lag_format;
    result.row = new_row;
    result.warnings.push_back(
        "re-read from disk: link uid " + std::to_string(uid) + " (" +
        std::to_string(predecessor_uid) + " -> " + std::to_string(successor_uid) +
        " " + link_type_name(type) + ", lag " + std::to_string(lag_ticks) +
        " ticks) matches the written row, FixedMeta entry and Fixed2Data record");
    doc = reread;
    return result;
}

DeletedLink delete_task_link(CfbDocument& doc, std::uint32_t link_uid,
                             std::string const& path) {
    std::string const function = "delete_task_link";
    check_output_path(path, function);
    if (link_uid == 0) {
        throw Error(function + ": uid 0 is the project row, not a link row");
    }
    LinkTable const links = load_link_table(doc, function);
    std::size_t const rows = links.rows.rows.size();
    std::size_t row = rows;
    for (std::size_t candidate = 0; candidate < rows; ++candidate) {
        if (get_u32(links.rows.fixed, candidate * kLinkRowSize + kLinkRowUid) ==
            link_uid) {
            row = candidate;
            break;
        }
    }
    if (row >= rows) {
        throw Error(function + ": no link row has uid " + std::to_string(link_uid));
    }
    std::size_t const entry_index = links.rows.rows[row].entries.front();
    std::size_t const entry_base =
        kDependentMetaHeaderSize + entry_index * kLinkMetaEntrySize;
    if (links.rows.meta[entry_base] != kStateLive) {
        throw Error(function + ": link uid " + std::to_string(link_uid) +
                    " is already a tombstone (FixedMeta +0 = 0x02)");
    }

    std::vector<std::uint8_t> meta = links.rows.meta;
    meta[entry_base] = kStateDeleted;
    put_u16(meta, entry_base + kLinkEntryFieldCount, 0);
    put_u32(meta, kLinkMetaHeaderFlags,
            get_u32(meta, kLinkMetaHeaderFlags) | kFixedMetaFlagHasDeleted);

    CfbDocument work = CfbDocument::from_bytes(doc.to_bytes());
    work.replace_stream(links.rows.meta_path, meta);

    CfbDocument const reread = save_and_verify(work, path, function, "114/TBkndTask");

    /* A delete is a tombstone: the other three streams and every other stream in
     * the file keep every byte, and every size word stays put. */
    if (reread.read_stream(links.rows.fixed_path) != links.rows.fixed ||
        reread.read_stream(links.fixed2_path) != links.fixed2 ||
        reread.read_stream(links.fixed2_meta_path) != links.fixed2_meta) {
        throw Error(function + ": FixedData/Fixed2Data/Fixed2Meta changed; a link "
                    "delete must tombstone the FixedMeta entry only");
    }
    std::vector<std::uint8_t> const meta_after = reread.read_stream(links.rows.meta_path);
    if (meta_after != meta) {
        throw Error(function + ": the written link FixedMeta does not read back as "
                    "built");
    }
    for (std::string const& path_name : doc.stream_paths()) {
        if (path_name == links.rows.meta_path) {
            continue;
        }
        if (!reread.has_stream(path_name) ||
            reread.read_stream(path_name) != doc.read_stream(path_name)) {
            throw Error(function + ": " + path_name +
                        " changed; deleting a link may only tombstone its "
                        "FixedMeta entry and set the table's header flag");
        }
    }
    if (get_u32(meta_after, kLinkMetaCount) != rows ||
        get_u32(meta_after, kLinkMetaFixedSize) != links.rows.fixed.size() ||
        get_u32(meta_after, kLinkMetaHeaderFlags) !=
            (get_u32(links.rows.meta, kLinkMetaHeaderFlags) |
             kFixedMetaFlagHasDeleted)) {
        throw Error(function + ": the link FixedMeta header does not read back as "
                    "written");
    }
    std::size_t const base = row * kLinkRowSize;
    if (meta_after[entry_base] != kStateDeleted ||
        get_u16(meta_after, entry_base + kLinkEntryFieldCount) != 0) {
        throw Error(function + ": link uid " + std::to_string(link_uid) +
                    " does not read back as a tombstone (+0 0x02, +2 0)");
    }

    DeletedLink result;
    result.uid = link_uid;
    result.predecessor_uid = get_u32(links.rows.fixed, base + kConsPredecessor);
    result.successor_uid = get_u32(links.rows.fixed, base + kConsSuccessor);
    result.type = get_u16(links.rows.fixed, base + kLinkRowType);
    result.lag_ticks =
        static_cast<std::int32_t>(get_u32(links.rows.fixed, base + kLinkRowLag));
    result.lag_format = get_u16(links.rows.fixed, base + kLinkRowLagUnits);
    result.row = row;
    result.warnings.push_back(
        "re-read from disk: link uid " + std::to_string(link_uid) +
        " is a tombstone (FixedMeta +0 0x02, +2 0) and TBkndCons/FixedMeta header "
        "+4 has bit 0x02; every stream size is unchanged");
    doc = reread;
    return result;
}

/* ---- link Fixed2Meta driving-state byte (W29-1.0b) ---------------------- */

ProgressLinks stage_progress_link_flags(
    CfbDocument const& doc, std::vector<ProgressLinkEdit> const& edits,
    std::string_view table) {
    std::string const function = "stage_progress_link_flags";
    ProgressLinks out;
    out.flags.reserve(edits.size());
    if (edits.empty()) {
        return out;
    }
    DependentTable const links = load_dependent_table(
        doc, table, kLinkRowSize, kLinkMetaEntrySize, function);
    std::size_t const rows = links.rows.size();
    std::string const fixed2_meta_path = table_stream(table, "Fixed2Meta");
    std::vector<std::uint8_t> staged;
    bool have_meta = false;
    bool any_changed = false;
    for (ProgressLinkEdit const& edit : edits) {
        ProgressLinkFlag flag;
        flag.successor_uid = edit.uid;
        for (DependentRow const& row : links.rows) {
            if (row.live && row.successor == edit.uid) {
                ++flag.live_incoming;
            }
        }
        out.flags.push_back(flag);
        if (flag.live_incoming == 0) {
            continue;
        }
        ProgressLinkFlag& record = out.flags.back();
        if (!edit.entered_progress) {
            throw Error(function + ": task uid " + std::to_string(edit.uid) +
                        " has " + std::to_string(record.live_incoming) +
                        " live incoming link(s) but this edit does not take it "
                        "from 0 % to non-zero; no Project save measures the "
                        "link flag for that shape, so the command is refused");
        }
        if (record.live_incoming > 1) {
            throw Error(function + ": task uid " + std::to_string(edit.uid) +
                        " has " + std::to_string(record.live_incoming) +
                        " live incoming links; the one-link progress effect is "
                        "the only measured shape, so the command is refused");
        }
        if (!have_meta) {
            std::string const fixed2_path = table_stream(table, "Fixed2Data");
            if (!doc.has_stream(fixed2_path)) {
                throw Error(function + ": the link table has a live incoming "
                            "link but no " + fixed2_path +
                            "; refusing to guess the flag it carries");
            }
            std::vector<std::uint8_t> const fixed2 = doc.read_stream(fixed2_path);
            if (fixed2.size() != rows * kLinkFixed2RecordSize) {
                throw Error(function + ": " + fixed2_path + " is " +
                            std::to_string(fixed2.size()) + " bytes, not " +
                            std::to_string(rows) + "*48; refusing to change a "
                            "damaged link table");
            }
            if (!doc.has_stream(fixed2_meta_path)) {
                throw Error(function + ": the link table has a live incoming "
                            "link but no " + fixed2_meta_path +
                            "; refusing to guess the flag it carries");
            }
            staged = doc.read_stream(fixed2_meta_path);
            if (staged.size() !=
                kLinkF2MetaHeaderSize + rows * kLinkFixed2MetaEntrySize) {
                throw Error(function + ": " + fixed2_meta_path + " is " +
                            std::to_string(staged.size()) + " bytes, not 16 + " +
                            std::to_string(rows) + "*9; refusing to change a "
                            "damaged link table");
            }
            if (get_u32(staged, kLinkF2MetaMagicOffset) != kDependentMagic ||
                get_u32(staged, kLinkF2MetaCount) != rows ||
                get_u32(staged, kLinkF2MetaFixedSize) != fixed2.size()) {
                throw Error(function + ": " + fixed2_meta_path +
                            " header counts do not match the link table; "
                            "refusing to change a damaged link table");
            }
            for (std::size_t row = 0; row < rows; ++row) {
                std::size_t const base =
                    kLinkF2MetaHeaderSize + row * kLinkFixed2MetaEntrySize;
                if (get_u32(staged, base + kLinkF2MetaRecordOffset) !=
                    row * kLinkFixed2RecordSize) {
                    throw Error(function + ": " + fixed2_meta_path + " entry " +
                                std::to_string(row) +
                                " does not name Fixed2Data record " +
                                std::to_string(row) +
                                "; refusing to change a damaged link table");
                }
            }
            have_meta = true;
        }
        std::size_t link_row = rows;
        for (DependentRow const& row : links.rows) {
            if (row.live && row.successor == edit.uid) {
                link_row = row.row;
                break;
            }
        }
        std::size_t const base =
            kLinkF2MetaHeaderSize + link_row * kLinkFixed2MetaEntrySize;
        record.before = staged[base + kLinkF2MetaFlag];
        if (record.before != kLinkFixed2BaseFlag &&
            record.before != kLinkFixed2EnteredProgressFlag) {
            char before_hex[8] = {};
            std::snprintf(before_hex, sizeof(before_hex), "%02x",
                          static_cast<unsigned>(record.before));
            throw Error(function + ": task uid " + std::to_string(edit.uid) +
                        "'s one live incoming link has Fixed2Meta +8 = 0x" +
                        before_hex +
                        "; only 0x0f and 0x07 are measured, so the command is "
                        "refused");
        }
        record.written = true;
        record.after = kLinkFixed2EnteredProgressFlag;
        if (record.before != record.after) {
            staged[base + kLinkF2MetaFlag] = record.after;
            record.changed = true;
            any_changed = true;
        }
    }
    if (any_changed) {
        out.fixed2_meta = staged;
    }
    return out;
}

/* ---- resource rows: 114/TBkndRsc (W54) ---------------------------------- */

namespace {

constexpr std::size_t kRscHeaderSize = 48;
constexpr std::size_t kRscRowSize = 172;
constexpr std::size_t kRscMetaEntrySize = 37;
constexpr std::size_t kRscF2RecordSize = 40;
constexpr std::size_t kRscF2MetaEntrySize = 51;
constexpr std::size_t kMetaMagicWord = 0;
constexpr std::size_t kMetaCountWord = 8;
constexpr std::size_t kMetaFixedSizeWord = 12;
constexpr std::size_t kMetaStateByte = 0;
constexpr std::size_t kMetaFieldCountWord = 2;
constexpr std::size_t kMetaRowOffsetWord = 4;
constexpr std::size_t kRscMetaTypeByte = 12;
constexpr std::uint8_t kRscWorkTypeBit = 0x10;
/* Set in every resource row Project itself wrote; meaning unmeasured (0x1c on
 * synthetic-base's untouched row, 0x5c/0x48 on Project-written rows). */
constexpr std::uint8_t kRscRowWrittenBit = 0x40;
constexpr std::size_t kRscF2MetaRecordOffsetWord = 4;
constexpr std::size_t kRscF2MetaTypeByte = 8;
constexpr std::uint8_t kRscCostTypeBit = 0x10;
[[maybe_unused]] constexpr std::size_t kRscF2OrdinalOffset = 16;
constexpr std::size_t kRscRowId = 0;
constexpr std::size_t kRscRowUid = 4;
constexpr std::size_t kRscRowMaxUnits = 8;
constexpr std::size_t kRscRowCounter = 154;
constexpr std::size_t kRscMetaDescriptors = 3;

constexpr std::uint32_t kRscNameFieldId = 0x0C400001u;
constexpr std::uint32_t kRscInitialsFieldId = 0x0C400002u;
constexpr std::uint32_t kRscRateFieldId = 0x0C400055u;
constexpr std::uint32_t kRscDateFieldId = 0x0C4002D6u;
constexpr std::uint32_t kRscBlobFieldId = 0x0C4002E3u;
constexpr std::size_t kRscNewRowFieldCount = 5;

/* Assignment table (W54). */
constexpr std::size_t kAsnRowSize = 110;
constexpr std::size_t kAsnMetaEntrySize = 34;
constexpr std::size_t kAsnF2RecordSize = 48;
constexpr std::size_t kAsnF2MetaEntrySize = 53;
constexpr std::size_t kAsnF2MetaRecordOffsetWord = 4;
constexpr std::size_t kAsnRowUid = 0;
constexpr std::size_t kAsnRowTask = 4;
constexpr std::size_t kAsnRowResource = 8;
constexpr std::size_t kAsnRowUnits = 12;
constexpr std::size_t kAsnRowWork = 20;
constexpr std::size_t kAsnRowZeroWork = 28;
constexpr std::size_t kAsnRowWorkCopy2 = 36;
constexpr std::size_t kAsnRowWorkCopy = 44;
constexpr std::size_t kAsnRowStart = 52;
constexpr std::size_t kAsnRowFinish = 56;
constexpr std::size_t kAsnRowResume = 60;
constexpr std::size_t kAsnRowUnmapped = 92;
constexpr std::uint32_t kAsnUnmappedValue = 7;
constexpr std::size_t kAsnRowStartCopy = 104;
constexpr std::size_t kAsnNewRowFieldCount = 4;

constexpr std::uint32_t kAsnContourFieldId = 0x0F400031u;
constexpr std::uint32_t kAsnCreatedFieldId = 0x0F40027Au;
constexpr std::uint32_t kAsnInternalAFieldId = 0x0F400199u;
constexpr std::uint32_t kAsnInternalBFieldId = 0x0F40019Bu;

/* The measured fresh-add FixedMeta tail (w12-assignadd.mpp row 3): +8..+15 =
 * f1 ff 23 f0 69 00 02 00, everything after +16 zero.  These bytes are per-row
 * cached data whose meaning is not decoded; copying Project's own bytes is the
 * measured choice. */
constexpr std::array<std::uint8_t, 8> kAsnMetaTail = {0xF1, 0xFF, 0x23, 0xF0,
                                                      0x69, 0x00, 0x02, 0x00};
/* The measured fresh-add Fixed2Meta entry (same row): +8/+9 = cf 09,
 * +36 = 0x10, +52 = 0x28. */
constexpr std::size_t kAsnF2MetaFlagA = 8;
constexpr std::size_t kAsnF2MetaFlagB = 9;
constexpr std::size_t kAsnF2MetaMidByte = 36;
constexpr std::size_t kAsnF2MetaLastByte = 52;

/* Flat planned-work contour, measured on assign1.mpp, w10-usage-flat.mpp and
 * w12-assignadd.mpp: 72 bytes, u32 0x00200000, u32 0x2c, f64 160000.0,
 * f64 work (ticks x100), u32 work_ticks*8, u32 0, u32 1, zeros. */
constexpr std::size_t kAsnContourSize = 72;
constexpr std::uint32_t kAsnContourWord0 = 0x00200000u;
constexpr std::uint32_t kAsnContourWord4 = 0x2Cu;
constexpr double kAsnContourConstant = 160000.0;

std::vector<MetaEntry> read_table_entries(std::vector<std::uint8_t> const& meta,
                                          std::size_t entry_size) {
    std::vector<MetaEntry> out;
    if (meta.size() < kDependentMetaHeaderSize) {
        return out;
    }
    std::size_t const count =
        (meta.size() - kDependentMetaHeaderSize) / entry_size;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t const base = kDependentMetaHeaderSize + i * entry_size;
        MetaEntry entry;
        entry.index = i;
        entry.offset = get_u32(meta, base + kMetaRowOffsetWord);
        entry.state = meta[base + kMetaStateByte];
        entry.field_count = get_u16(meta, base + kMetaFieldCountWord);
        out.push_back(entry);
    }
    return out;
}

void require_stream(CfbDocument const& doc, std::string const& path,
                    std::string const& function) {
    if (!doc.has_stream(path)) {
        throw Error(function + ": the file has no " + path +
                    " stream; a structural write needs it");
    }
}

std::vector<std::uint8_t> var_block_payload(std::vector<std::uint8_t> const& var_meta,
                                            std::vector<std::uint8_t> const& var2,
                                            std::uint32_t uid, std::uint32_t key) {
    std::vector<std::uint32_t> const offsets = find_var_offsets(var_meta, key, uid);
    if (offsets.empty()) {
        return {};
    }
    return read_var_block(var2, offsets.front());
}

struct ResourceTable {
    std::string fixed_path;
    std::string meta_path;
    std::string fixed2_path;
    std::string fixed2_meta_path;
    std::string var_meta_path;
    std::string var2_path;
    std::vector<std::uint8_t> fixed;
    std::vector<std::uint8_t> meta;
    std::vector<std::uint8_t> fixed2;
    std::vector<std::uint8_t> fixed2_meta;
    std::vector<std::uint8_t> var_meta;
    std::vector<std::uint8_t> var2;
    std::vector<MetaEntry> entries;
    std::size_t rows = 0;
};

/*
 * Read and strictly validate 114/TBkndRsc.  The shape is measured (see the
 * header): FixedData 48 + 172*n, FixedMeta 16 + 37*(n+3) with three header
 * descriptors, Fixed2Data 40*(n+3), Fixed2Meta 16 + 51*(n+3), VarMeta 24 + 12*k.
 * Unlike the delete path, an add does arithmetic on every count/size word, so
 * any disagreement is refused instead of tolerated.
 */
ResourceTable load_resource_table(CfbDocument const& doc,
                                  std::string const& function) {
    ResourceTable out;
    out.fixed_path = "114/TBkndRsc/FixedData";
    out.meta_path = "114/TBkndRsc/FixedMeta";
    out.fixed2_path = "114/TBkndRsc/Fixed2Data";
    out.fixed2_meta_path = "114/TBkndRsc/Fixed2Meta";
    out.var_meta_path = "114/TBkndRsc/VarMeta";
    out.var2_path = "114/TBkndRsc/Var2Data";
    for (std::string const* path : {&out.fixed_path, &out.meta_path,
                                    &out.fixed2_path, &out.fixed2_meta_path,
                                    &out.var_meta_path, &out.var2_path}) {
        require_stream(doc, *path, function);
    }
    out.fixed = doc.read_stream(out.fixed_path);
    if (out.fixed.size() < kRscHeaderSize ||
        (out.fixed.size() - kRscHeaderSize) % kRscRowSize != 0) {
        throw Error(function + ": " + out.fixed_path + " is " +
                    std::to_string(out.fixed.size()) + " bytes, not 48 + 172*n; "
                    "refusing to change the resource table's geometry");
    }
    out.rows = (out.fixed.size() - kRscHeaderSize) / kRscRowSize;
    out.meta = doc.read_stream(out.meta_path);
    std::size_t const meta_entries = out.rows + kRscMetaDescriptors;
    if (out.meta.size() != kDependentMetaHeaderSize +
                                kRscMetaEntrySize * meta_entries) {
        throw Error(function + ": " + out.meta_path + " is " +
                    std::to_string(out.meta.size()) + " bytes, not 16 + 37*" +
                    std::to_string(meta_entries) +
                    "; refusing to append to a damaged resource table");
    }
    if (get_u32(out.meta, kMetaMagicWord) != kDependentMagic) {
        throw Error(function + ": " + out.meta_path +
                    " does not start with the measured table magic");
    }
    if (get_u32(out.meta, kMetaCountWord) != meta_entries ||
        get_u32(out.meta, kMetaFixedSizeWord) != out.fixed.size()) {
        throw Error(function + ": " + out.meta_path +
                    " header count/size disagree with the resource table");
    }
    out.entries = read_table_entries(out.meta, kRscMetaEntrySize);
    for (std::size_t index = kRscMetaDescriptors; index < out.entries.size();
         ++index) {
        std::size_t const want =
            kRscHeaderSize + kRscRowSize * (index - kRscMetaDescriptors);
        if (out.entries[index].offset != want) {
            throw Error(function + ": " + out.meta_path + " entry " +
                        std::to_string(index) + " names offset " +
                        std::to_string(out.entries[index].offset) + ", expected " +
                        std::to_string(want) +
                        "; refusing to append to a damaged resource table");
        }
    }
    out.fixed2 = doc.read_stream(out.fixed2_path);
    out.fixed2_meta = doc.read_stream(out.fixed2_meta_path);
    std::size_t const fixed2_records = out.rows + kRscMetaDescriptors;
    if (out.fixed2.size() != fixed2_records * kRscF2RecordSize) {
        throw Error(function + ": " + out.fixed2_path + " is " +
                    std::to_string(out.fixed2.size()) + " bytes, not " +
                    std::to_string(fixed2_records) + "*40");
    }
    if (out.fixed2_meta.size() !=
        kDependentMetaHeaderSize + kRscF2MetaEntrySize * fixed2_records) {
        throw Error(function + ": " + out.fixed2_meta_path + " is " +
                    std::to_string(out.fixed2_meta.size()) + " bytes, not 16 + " +
                    std::to_string(fixed2_records) + "*51");
    }
    if (get_u32(out.fixed2_meta, kMetaMagicWord) != kDependentMagic ||
        get_u32(out.fixed2_meta, kMetaCountWord) != fixed2_records ||
        get_u32(out.fixed2_meta, kMetaFixedSizeWord) != out.fixed2.size()) {
        throw Error(function + ": " + out.fixed2_meta_path +
                    " header count/size disagree with the resource table");
    }
    for (std::size_t index = 0; index < fixed2_records; ++index) {
        std::size_t const base =
            kDependentMetaHeaderSize + index * kRscF2MetaEntrySize;
        if (get_u32(out.fixed2_meta, base + kRscF2MetaRecordOffsetWord) !=
            index * kRscF2RecordSize) {
            throw Error(function + ": " + out.fixed2_meta_path + " entry " +
                        std::to_string(index) +
                        " does not name its Fixed2Data record");
        }
    }
    out.var_meta = doc.read_stream(out.var_meta_path);
    out.var2 = doc.read_stream(out.var2_path);
    if (out.var_meta.size() < kVarMetaHeaderSize ||
        (out.var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
        throw Error(function + ": " + out.var_meta_path + " is " +
                    std::to_string(out.var_meta.size()) + " bytes, not 24 + 12*k");
    }
    if (get_u32(out.var_meta, 8) !=
            (out.var_meta.size() - kVarMetaHeaderSize) / kVarMetaEntrySize ||
        get_u32(out.var_meta, kVarMetaVar2SizeOffset) != out.var2.size()) {
        throw Error(function + ": " + out.var_meta_path +
                    " header count/size disagree with the resource table");
    }
    return out;
}

struct LiveResourceRow {
    std::size_t row = 0;
    std::uint32_t uid = 0;
};

/* Resource liveness: a FixedMeta entry with state 0 names the row.  Row 0 is the
 * table's uid-0 placeholder and is never a resource. */
std::vector<LiveResourceRow> live_resource_rows(ResourceTable const& table) {
    std::vector<LiveResourceRow> out;
    for (std::size_t row = 1; row < table.rows; ++row) {
        std::uint32_t const offset =
            static_cast<std::uint32_t>(kRscHeaderSize + row * kRscRowSize);
        std::int32_t const index = entry_index_for_offset(table.entries, offset);
        if (index < 0 ||
            table.entries[static_cast<std::size_t>(index)].state != kStateLive) {
            continue;
        }
        LiveResourceRow live;
        live.row = row;
        live.uid = get_u32(table.fixed, offset + kRscRowUid);
        out.push_back(live);
    }
    return out;
}

std::string first_character(std::string const& text) {
    return text.empty() ? std::string() : text.substr(0, 1);
}

std::string resource_kind_name(ResourceKind kind) {
    switch (kind) {
        case ResourceKind::work:
            return "Work";
        case ResourceKind::material:
            return "Material";
        case ResourceKind::cost:
            return "Cost";
    }
    return "Work";
}

void apply_resource_type(std::vector<std::uint8_t>& meta_entry,
                         std::vector<std::uint8_t>& fixed2_entry,
                         ResourceKind kind) {
    /* Bit 0x40 is set in the FixedMeta +12 byte of EVERY resource row Project
     * itself wrote (w9-resourcetypes R1/R2/R3, assign1's R1/R2, w12-assignadd's
     * R1: 0x5c Work / 0x48 Material/Cost), while synthetic-base's untouched
     * "Crew" row carries 0x1c.  Project's own save sets it on the existing rows
     * too, so it is a row-written marker, not a property of one kind.  Its exact
     * meaning is unmeasured; setting it reproduces Project's measured new-row
     * byte and the reader only needs bit 0x10 for Work. */
    meta_entry[kRscMetaTypeByte] =
        static_cast<std::uint8_t>(meta_entry[kRscMetaTypeByte] |
                                  kRscRowWrittenBit);
    if (kind == ResourceKind::work) {
        meta_entry[kRscMetaTypeByte] =
            static_cast<std::uint8_t>(meta_entry[kRscMetaTypeByte] |
                                      kRscWorkTypeBit);
    } else {
        meta_entry[kRscMetaTypeByte] =
            static_cast<std::uint8_t>(meta_entry[kRscMetaTypeByte] &
                                      ~kRscWorkTypeBit);
    }
    if (kind == ResourceKind::cost) {
        fixed2_entry[kRscF2MetaTypeByte] =
            static_cast<std::uint8_t>(fixed2_entry[kRscF2MetaTypeByte] |
                                      kRscCostTypeBit);
    } else {
        fixed2_entry[kRscF2MetaTypeByte] =
            static_cast<std::uint8_t>(fixed2_entry[kRscF2MetaTypeByte] &
                                      ~kRscCostTypeBit);
    }
}

} /* namespace */

AddedResource add_resource_row(CfbDocument& doc, ResourceSpec const& resource,
                               std::string const& path) {
    std::string const function = "add_resource_row";
    check_output_path(path, function);
    if (resource.name.empty()) {
        throw Error(function + ": a resource name is required; Project stores it "
                    "as the row's Name field (VarMeta key 0x0c400001)");
    }
    if (!std::isfinite(resource.max_units_percent) ||
        resource.max_units_percent <= 0.0 ||
        resource.max_units_percent > 10000.0) {
        throw Error(function + ": MaxUnits " +
                    std::to_string(resource.max_units_percent) +
                    " % is outside the writer's (0, 10000] bound (100 % is the "
                    "measured Project default; this is a policy bound)");
    }

    ResourceTable const table = load_resource_table(doc, function);
    std::vector<LiveResourceRow> const live = live_resource_rows(table);

    std::uint32_t max_uid = 0;
    std::uint32_t max_live_id = 0;
    for (std::size_t row = 1; row < table.rows; ++row) {
        std::size_t const base = kRscHeaderSize + row * kRscRowSize;
        max_uid = std::max(max_uid, get_u32(table.fixed, base + kRscRowUid));
    }
    for (LiveResourceRow const& row : live) {
        std::size_t const base = kRscHeaderSize + row.row * kRscRowSize;
        max_live_id = std::max(max_live_id, get_u32(table.fixed, base + kRscRowId));
    }
    if (max_uid == 0xFFFFFFFFu) {
        throw Error(function + ": the resource table's largest uid is 0xffffffff, "
                    "so a new uid would overflow");
    }
    std::uint32_t const uid = max_uid + 1;
    std::uint32_t const id = max_live_id + 1;

    /* Template: the last live resource row (row 0's placeholder when the table
     * has no live resource).  Index 0..2 of Resource FixedMeta are descriptors,
     * so the entry for row r is entry r + 3. */
    std::size_t const template_row = live.empty() ? 0 : live.back().row;
    std::size_t const template_index = template_row + kRscMetaDescriptors;
    std::uint32_t const template_uid =
        get_u32(table.fixed, kRscHeaderSize + template_row * kRscRowSize + kRscRowUid);
    for (VarEntry const& entry : read_var_entries(table.var_meta)) {
        if (entry.mid > uid) {
            throw Error(function + ": the resource table already holds a uid (" +
                        std::to_string(entry.mid) +
                        ") larger than the allocated uid " + std::to_string(uid) +
                        "; refusing to append a variable-entry group out of order");
        }
    }

    std::size_t const new_row = table.rows;
    std::size_t const new_offset = kRscHeaderSize + new_row * kRscRowSize;

    std::vector<std::uint8_t> row(table.fixed.begin() +
                                      static_cast<std::ptrdiff_t>(
                                          kRscHeaderSize +
                                          template_row * kRscRowSize),
                                  table.fixed.begin() +
                                      static_cast<std::ptrdiff_t>(
                                          kRscHeaderSize +
                                          (template_row + 1) * kRscRowSize));
    put_u32(row, kRscRowId, id);
    put_u32(row, kRscRowUid, uid);
    put_f64(row, kRscRowMaxUnits, resource.max_units_percent * 100.0);
    std::uint16_t const counter = get_u16(row, kRscRowCounter);
    put_u16(row, kRscRowCounter,
            counter == 0xFFFFu ? 1u : static_cast<std::uint16_t>(counter + 1u));

    std::size_t const template_entry_base =
        kDependentMetaHeaderSize + template_index * kRscMetaEntrySize;
    std::vector<std::uint8_t> meta_entry(
        table.meta.begin() + static_cast<std::ptrdiff_t>(template_entry_base),
        table.meta.begin() +
            static_cast<std::ptrdiff_t>(template_entry_base + kRscMetaEntrySize));
    meta_entry[kMetaStateByte] = kStateLive;
    put_u16(meta_entry, kMetaFieldCountWord,
            static_cast<std::uint16_t>(kRscNewRowFieldCount));
    put_u32(meta_entry, kMetaRowOffsetWord,
            static_cast<std::uint32_t>(new_offset));

    std::size_t const template_f2_base =
        kDependentMetaHeaderSize + template_index * kRscF2MetaEntrySize;
    std::vector<std::uint8_t> fixed2_entry(
        table.fixed2_meta.begin() + static_cast<std::ptrdiff_t>(template_f2_base),
        table.fixed2_meta.begin() + static_cast<std::ptrdiff_t>(
                                         template_f2_base + kRscF2MetaEntrySize));
    put_u32(fixed2_entry, kRscF2MetaRecordOffsetWord,
            static_cast<std::uint32_t>(
                (new_row + kRscMetaDescriptors) * kRscF2RecordSize));
    apply_resource_type(meta_entry, fixed2_entry, resource.kind);

    /* Name / Initials plus the three measured default blocks. */
    std::string const initials = resource.initials.empty()
                                     ? first_character(resource.name)
                                     : resource.initials;
    std::vector<std::vector<std::uint8_t>> payloads;
    payloads.push_back(encode_utf16z(resource.name));
    payloads.push_back(encode_utf16z(initials));
    std::vector<std::uint8_t> rate =
        var_block_payload(table.var_meta, table.var2, template_uid,
                          kRscRateFieldId);
    if (rate.size() != 4) {
        rate.assign(4, 0);
        put_u32(rate, 0, 2);
    }
    payloads.push_back(rate);
    std::vector<std::uint8_t> date =
        var_block_payload(table.var_meta, table.var2, template_uid,
                          kRscDateFieldId);
    if (date.size() != 4) {
        date = var_block_payload(table.var_meta, table.var2, 0, kRscDateFieldId);
    }
    if (date.size() != 4) {
        date.assign(4, 0xFF);
    }
    payloads.push_back(date);
    std::vector<std::uint8_t> blob =
        var_block_payload(table.var_meta, table.var2, template_uid,
                          kRscBlobFieldId);
    if (blob.size() != 16) {
        blob.assign(16, 0);
    }
    payloads.push_back(blob);
    std::array<std::uint32_t, 5> const keys = {
        kRscNameFieldId, kRscInitialsFieldId, kRscRateFieldId, kRscDateFieldId,
        kRscBlobFieldId};

    std::vector<std::uint8_t> fixed = table.fixed;
    fixed.insert(fixed.end(), row.begin(), row.end());
    std::vector<std::uint8_t> meta = table.meta;
    meta.insert(meta.end(), meta_entry.begin(), meta_entry.end());
    put_u32(meta, kMetaCountWord,
            static_cast<std::uint32_t>(table.rows + 1 + kRscMetaDescriptors));
    put_u32(meta, kMetaFixedSizeWord, static_cast<std::uint32_t>(fixed.size()));

    LinkIdentity const identity = make_link_identity(doc, function);
    std::vector<std::uint8_t> fixed2 = table.fixed2;
    fixed2.insert(fixed2.end(), identity.begin(), identity.end());
    std::vector<std::uint8_t> ordinal(8, 0);
    put_f64(ordinal, 0, static_cast<double>(new_row + 1));
    fixed2.insert(fixed2.end(), ordinal.begin(), ordinal.end());
    fixed2.insert(fixed2.end(), identity.begin(), identity.end());

    std::vector<std::uint8_t> fixed2_meta = table.fixed2_meta;
    fixed2_meta.insert(fixed2_meta.end(), fixed2_entry.begin(),
                       fixed2_entry.end());
    put_u32(fixed2_meta, kMetaCountWord,
            static_cast<std::uint32_t>(table.rows + 1 + kRscMetaDescriptors));
    put_u32(fixed2_meta, kMetaFixedSizeWord,
            static_cast<std::uint32_t>(fixed2.size()));

    std::vector<std::uint8_t> var_meta = table.var_meta;
    std::vector<std::uint8_t> var2 = table.var2;
    /* The new uid is strictly the largest (checked above), so appending the
     * entries at the end keeps Project's measured uid grouping.  The key order
     * is the table's measured order, not a sorted one (the Assignment keys are
     * deliberately not ascending in Project's own file), so this does not go
     * through insert_var_entry's sort. */
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        std::size_t const offset = var2.size();
        std::vector<std::uint8_t> const block = encode_block(payloads[i]);
        var2.insert(var2.end(), block.begin(), block.end());
        std::vector<std::uint8_t> entry(kVarMetaEntrySize, 0);
        put_u32(entry, 0, uid);
        put_u32(entry, 4, static_cast<std::uint32_t>(offset));
        put_u32(entry, 8, keys[i]);
        var_meta.insert(var_meta.end(), entry.begin(), entry.end());
    }
    put_u32(var_meta, 8,
            static_cast<std::uint32_t>(
                (var_meta.size() - kVarMetaHeaderSize) / kVarMetaEntrySize));
    put_u32(var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(var2.size()));

    CfbDocument work = CfbDocument::from_bytes(doc.to_bytes());
    work.replace_stream(table.fixed_path, fixed);
    work.replace_stream(table.meta_path, meta);
    work.replace_stream(table.fixed2_path, fixed2);
    work.replace_stream(table.fixed2_meta_path, fixed2_meta);
    work.replace_stream(table.var_meta_path, var_meta);
    work.replace_stream(table.var2_path, var2);

    CfbDocument const reread = save_and_verify(work, path, function,
                                               "114/TBkndTask");

    if (reread.read_stream(table.fixed_path) != fixed ||
        reread.read_stream(table.meta_path) != meta ||
        reread.read_stream(table.fixed2_path) != fixed2 ||
        reread.read_stream(table.fixed2_meta_path) != fixed2_meta ||
        reread.read_stream(table.var_meta_path) != var_meta ||
        reread.read_stream(table.var2_path) != var2) {
        throw Error(function + ": the six written resource streams do not read "
                    "back with the bytes that were built");
    }
    for (std::string const& stream : doc.stream_paths()) {
        if (stream == table.fixed_path || stream == table.meta_path ||
            stream == table.fixed2_path || stream == table.fixed2_meta_path ||
            stream == table.var_meta_path || stream == table.var2_path) {
            continue;
        }
        if (!reread.has_stream(stream) ||
            reread.read_stream(stream) != doc.read_stream(stream)) {
            throw Error(function + ": " + stream +
                        " changed; adding a resource may only touch the six "
                        "TBkndRsc streams");
        }
    }
    ResourceTable const after = load_resource_table(reread, function);
    if (after.rows != table.rows + 1 || after.fixed.size() != fixed.size() ||
        after.meta.size() != meta.size() ||
        after.fixed2.size() != fixed2.size() ||
        after.fixed2_meta.size() != fixed2_meta.size()) {
        throw Error(function + ": the resource table's geometry does not read back "
                    "as written");
    }
    std::vector<std::uint8_t> const row_after = reread.read_stream(table.fixed_path);
    if (get_u32(row_after, new_offset + kRscRowUid) != uid ||
        get_u32(row_after, new_offset + kRscRowId) != id ||
        get_u16(row_after, new_offset + kRscRowCounter) !=
            get_u16(row, kRscRowCounter)) {
        throw Error(function + ": the new resource row does not read back as "
                    "written");
    }
    std::uint64_t units_bits = 0;
    double const units = resource.max_units_percent * 100.0;
    std::memcpy(&units_bits, &units, sizeof units_bits);
    std::uint64_t stored_bits = 0;
    std::memcpy(&stored_bits, row_after.data() + new_offset + kRscRowMaxUnits,
                sizeof stored_bits);
    if (units_bits != stored_bits) {
        throw Error(function + ": the new resource row's MaxUnits does not read "
                    "back as written");
    }
    std::vector<std::uint8_t> const meta_after = reread.read_stream(table.meta_path);
    std::size_t const new_entry_base =
        kDependentMetaHeaderSize +
        (table.rows + kRscMetaDescriptors) * kRscMetaEntrySize;
    if (meta_after[new_entry_base + kMetaStateByte] != kStateLive ||
        get_u16(meta_after, new_entry_base + kMetaFieldCountWord) !=
            kRscNewRowFieldCount ||
        get_u32(meta_after, new_entry_base + kMetaRowOffsetWord) !=
            static_cast<std::uint32_t>(new_offset)) {
        throw Error(function + ": the new resource FixedMeta entry does not read "
                    "back as written");
    }
    std::vector<std::uint8_t> const fixed2_after =
        reread.read_stream(table.fixed2_path);
    std::size_t const new_record =
        (new_row + kRscMetaDescriptors) * kRscF2RecordSize;
    if (!std::equal(identity.begin(), identity.end(),
                    fixed2_after.begin() +
                        static_cast<std::ptrdiff_t>(new_record)) ||
        !std::equal(identity.begin(), identity.end(),
                    fixed2_after.begin() + static_cast<std::ptrdiff_t>(
                                               new_record + 24))) {
        throw Error(function + ": the new resource Fixed2Data record does not read "
                    "back as written");
    }
    std::vector<std::uint8_t> const var_meta_after =
        reread.read_stream(table.var_meta_path);
    std::vector<std::uint8_t> const var2_after = reread.read_stream(table.var2_path);
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        std::vector<std::uint32_t> const offsets =
            find_var_offsets(var_meta_after, keys[i], uid);
        if (offsets.size() != 1 ||
            read_var_block(var2_after, offsets.front()) != payloads[i]) {
            throw Error(function + ": the new resource variable entry 0x" +
                        std::to_string(keys[i]) +
                        " does not read back with the payload that was written");
        }
    }
    if (get_u32(var_meta_after, 8) !=
        (var_meta_after.size() - kVarMetaHeaderSize) / kVarMetaEntrySize) {
        throw Error(function + ": the resource VarMeta entry count does not read "
                    "back as written");
    }

    AddedResource result;
    result.uid = uid;
    result.id = id;
    result.row = new_row;
    result.name = resource.name;
    result.initials = initials;
    result.max_units_percent = resource.max_units_percent;
    result.kind = resource.kind;
    result.warnings.push_back(
        "re-read from disk: resource uid " + std::to_string(uid) + " (row " +
        std::to_string(new_row) + ", " + resource_kind_name(resource.kind) +
        ", MaxUnits " + std::to_string(resource.max_units_percent) +
        " %) matches the written row and its five variable entries");
    if (resource.max_units_percent != 100.0) {
        result.warnings.push_back(
            "MaxUnits other than 100 % is not measured on a Project add; the row "
            "is a clone of the last live resource with only ID/uid/MaxUnits "
            "changed");
    }
    doc = reread;
    return result;
}

/* ---- assignment rows: 114/TBkndAssn (W54) ------------------------------- */

namespace {

struct AssignmentTable {
    std::string fixed_path;
    std::string meta_path;
    std::string fixed2_path;
    std::string fixed2_meta_path;
    std::string var_meta_path;
    std::string var2_path;
    std::vector<std::uint8_t> fixed;
    std::vector<std::uint8_t> meta;
    std::vector<std::uint8_t> fixed2;
    std::vector<std::uint8_t> fixed2_meta;
    std::vector<std::uint8_t> var_meta;
    std::vector<std::uint8_t> var2;
    std::vector<MetaEntry> entries;
    std::size_t rows = 0;
};

/*
 * Read and strictly validate 114/TBkndAssn: FixedData 110*n (no header),
 * FixedMeta 16 + 34*n, Fixed2Data 48*n, Fixed2Meta 16 + 53*n, VarMeta 24 + 12*k.
 * A row may lack a FixedMeta entry (PROJECT_A has 69 rows and 68 entries), which is
 * how Project records a row it does not treat as an assignment; a duplicate
 * entry offset is damage and is refused.
 */
AssignmentTable load_assignment_table(CfbDocument const& doc,
                                      std::string const& function) {
    AssignmentTable out;
    out.fixed_path = "114/TBkndAssn/FixedData";
    out.meta_path = "114/TBkndAssn/FixedMeta";
    out.fixed2_path = "114/TBkndAssn/Fixed2Data";
    out.fixed2_meta_path = "114/TBkndAssn/Fixed2Meta";
    out.var_meta_path = "114/TBkndAssn/VarMeta";
    out.var2_path = "114/TBkndAssn/Var2Data";
    for (std::string const* path : {&out.fixed_path, &out.meta_path,
                                    &out.fixed2_path, &out.fixed2_meta_path,
                                    &out.var_meta_path, &out.var2_path}) {
        require_stream(doc, *path, function);
    }
    out.fixed = doc.read_stream(out.fixed_path);
    if (out.fixed.size() % kAsnRowSize != 0) {
        throw Error(function + ": " + out.fixed_path + " is " +
                    std::to_string(out.fixed.size()) + " bytes, not n*110; "
                    "refusing to change the assignment table's geometry");
    }
    out.rows = out.fixed.size() / kAsnRowSize;
    out.meta = doc.read_stream(out.meta_path);
    if (out.meta.size() !=
        kDependentMetaHeaderSize + kAsnMetaEntrySize * out.rows) {
        throw Error(function + ": " + out.meta_path + " is " +
                    std::to_string(out.meta.size()) + " bytes, not 16 + 34*" +
                    std::to_string(out.rows) +
                    "; refusing to change the assignment table's geometry");
    }
    if (get_u32(out.meta, kMetaMagicWord) != kDependentMagic ||
        get_u32(out.meta, kMetaCountWord) != out.rows ||
        get_u32(out.meta, kMetaFixedSizeWord) != out.fixed.size()) {
        throw Error(function + ": " + out.meta_path +
                    " header count/size disagree with the assignment table");
    }
    out.entries = read_table_entries(out.meta, kAsnMetaEntrySize);
    std::vector<bool> covered(out.rows, false);
    for (MetaEntry const& entry : out.entries) {
        if (entry.offset % kAsnRowSize != 0) {
            throw Error(function + ": " + out.meta_path + " entry " +
                        std::to_string(entry.index) + " has offset " +
                        std::to_string(entry.offset) + ", not a whole row");
        }
        std::size_t const row = entry.offset / kAsnRowSize;
        if (row >= out.rows) {
            throw Error(function + ": " + out.meta_path + " entry " +
                        std::to_string(entry.index) + " names row " +
                        std::to_string(row) + ", past the last row");
        }
        if (covered[row]) {
            throw Error(function + ": " + out.meta_path + " has more than one "
                        "entry for assignment row " + std::to_string(row) +
                        "; refusing to change a damaged table");
        }
        covered[row] = true;
    }
    out.fixed2 = doc.read_stream(out.fixed2_path);
    out.fixed2_meta = doc.read_stream(out.fixed2_meta_path);
    if (out.fixed2.size() != out.rows * kAsnF2RecordSize) {
        throw Error(function + ": " + out.fixed2_path + " is " +
                    std::to_string(out.fixed2.size()) + " bytes, not " +
                    std::to_string(out.rows) + "*48");
    }
    if (out.fixed2_meta.size() !=
        kDependentMetaHeaderSize + kAsnF2MetaEntrySize * out.rows) {
        throw Error(function + ": " + out.fixed2_meta_path + " is " +
                    std::to_string(out.fixed2_meta.size()) + " bytes, not 16 + " +
                    std::to_string(out.rows) + "*53");
    }
    if (get_u32(out.fixed2_meta, kMetaMagicWord) != kDependentMagic ||
        get_u32(out.fixed2_meta, kMetaCountWord) != out.rows ||
        get_u32(out.fixed2_meta, kMetaFixedSizeWord) != out.fixed2.size()) {
        throw Error(function + ": " + out.fixed2_meta_path +
                    " header count/size disagree with the assignment table");
    }
    for (std::size_t row = 0; row < out.rows; ++row) {
        if (get_u32(out.fixed2_meta, kDependentMetaHeaderSize +
                                        row * kAsnF2MetaEntrySize +
                                        kAsnF2MetaRecordOffsetWord) !=
            row * kAsnF2RecordSize) {
            throw Error(function + ": " + out.fixed2_meta_path + " entry " +
                        std::to_string(row) +
                        " does not name its Fixed2Data record");
        }
    }
    out.var_meta = doc.read_stream(out.var_meta_path);
    out.var2 = doc.read_stream(out.var2_path);
    if (out.var_meta.size() < kVarMetaHeaderSize ||
        (out.var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
        throw Error(function + ": " + out.var_meta_path + " is " +
                    std::to_string(out.var_meta.size()) + " bytes, not 24 + 12*k");
    }
    if (get_u32(out.var_meta, 8) !=
            (out.var_meta.size() - kVarMetaHeaderSize) / kVarMetaEntrySize ||
        get_u32(out.var_meta, kVarMetaVar2SizeOffset) != out.var2.size()) {
        throw Error(function + ": " + out.var_meta_path +
                    " header count/size disagree with the assignment table");
    }
    return out;
}

std::size_t live_assignment_count(AssignmentTable const& table) {
    std::size_t live = 0;
    for (std::size_t row = 0; row < table.rows; ++row) {
        std::uint32_t const offset = static_cast<std::uint32_t>(row * kAsnRowSize);
        std::int32_t const index = entry_index_for_offset(table.entries, offset);
        if (index < 0 ||
            table.entries[static_cast<std::size_t>(index)].state != kStateLive) {
            continue;
        }
        if (get_u32(table.fixed, offset + kAsnRowTask) == 0) {
            continue; /* placeholder row */
        }
        ++live;
    }
    return live;
}

/* The Task table values an assignment needs: liveness, the duration and the
 * effective date pair (+104/+108, W33's measured effective Start/Finish). */
struct AssignmentTask {
    std::size_t row = 0;
    std::uint32_t uid = 0;
    std::uint32_t duration_ticks = 0;
    std::uint32_t start = kInvalidDate;
    std::uint32_t finish = kInvalidDate;
    LinkIdentity identity{};
};

AssignmentTask find_assignment_task(CfbDocument const& doc, std::uint32_t uid,
                                    std::string const& function) {
    validate_link_task_table(doc, function);
    TaskTable const table = TaskTable::from_document(doc);
    std::vector<LiveTaskRow> const live = live_task_rows(doc);
    std::size_t row = 0;
    for (LiveTaskRow const& candidate : live) {
        if (candidate.uid == uid) {
            row = candidate.row;
            break;
        }
    }
    if (row == 0) {
        throw Error(function + ": task uid " + std::to_string(uid) +
                    " has no live task row (unknown uid, a tombstone, or a row "
                    "with no VarMeta group)");
    }
    AssignmentTask out;
    out.row = row;
    out.uid = uid;
    out.duration_ticks = table.u32(row, task_row::kDuration);
    out.start = table.u32(row, task_row::kStart2);
    out.finish = table.u32(row, task_row::kFinish2);
    out.identity = task_identity(doc, row, function);
    return out;
}

AssignmentTask find_assignment_resource(CfbDocument const& doc,
                                        std::uint32_t uid,
                                        std::string const& function) {
    ResourceTable const table = load_resource_table(doc, function);
    std::vector<LiveResourceRow> const live = live_resource_rows(table);
    std::size_t row = 0;
    for (LiveResourceRow const& candidate : live) {
        if (candidate.uid == uid) {
            row = candidate.row;
            break;
        }
    }
    if (row == 0) {
        throw Error(function + ": resource uid " + std::to_string(uid) +
                    " has no live resource row");
    }
    AssignmentTask out;
    out.row = row;
    out.uid = uid;
    std::size_t const base = (row + kRscMetaDescriptors) * kRscF2RecordSize;
    if (base + kLinkIdentitySize > table.fixed2.size()) {
        throw Error(function + ": Rsc/Fixed2Data is too short for resource row " +
                    std::to_string(row) + "'s identity record");
    }
    std::copy(table.fixed2.begin() + static_cast<std::ptrdiff_t>(base),
              table.fixed2.begin() +
                  static_cast<std::ptrdiff_t>(base + kLinkIdentitySize),
              out.identity.begin());
    if (identity_is_zero(out.identity)) {
        throw Error(function + ": resource row " + std::to_string(row) +
                    " has an all-zero Rsc/Fixed2Data identity, so there is no "
                    "measured identity to copy into the assignment record");
    }
    return out;
}

void require_valid_date(std::uint32_t date, std::string const& what,
                        std::string const& function) {
    if (!is_date(date)) {
        throw Error(function + ": " + what + " is not a valid packed date (it is " +
                    format_date(date) + ")");
    }
}

/* Flat planned-work contour block (see the header comment). */
std::vector<std::uint8_t> assignment_contour(std::uint32_t work_ticks) {
    std::vector<std::uint8_t> block(kAsnContourSize, 0);
    put_u32(block, 0, kAsnContourWord0);
    put_u32(block, 4, kAsnContourWord4);
    put_f64(block, 8, kAsnContourConstant);
    put_f64(block, 16, static_cast<double>(work_ticks) * 100.0);
    put_u32(block, 24,
            static_cast<std::uint32_t>(
                std::min<std::uint64_t>(0xFFFFFFFFu,
                                        static_cast<std::uint64_t>(work_ticks) *
                                            8u)));
    put_u32(block, 32, 1);
    return block;
}

} /* namespace */

AddedAssignment add_assignment_row(CfbDocument& doc,
                                   AssignmentSpec const& assignment,
                                   std::string const& path) {
    std::string const function = "add_assignment_row";
    check_output_path(path, function);
    if (assignment.task_uid == 0) {
        throw Error(function + ": task uid 0 is the project row and cannot own an "
                    "assignment");
    }
    if (assignment.resource_uid == 0) {
        throw Error(function + ": resource uid 0 is the resource table's "
                    "placeholder and cannot be assigned");
    }
    double units_percent =
        assignment.units_percent == 0.0 ? 100.0 : assignment.units_percent;
    if (!std::isfinite(units_percent) || units_percent <= 0.0 ||
        units_percent > 10000.0) {
        throw Error(function + ": assignment units " +
                    std::to_string(assignment.units_percent) +
                    " % are outside the writer's (0, 10000] bound");
    }

    AssignmentTable const table = load_assignment_table(doc, function);
    AssignmentTask const task = find_assignment_task(doc, assignment.task_uid,
                                                     function);
    AssignmentTask const resource = find_assignment_resource(
        doc, assignment.resource_uid, function);

    std::uint32_t max_uid = 0;
    for (std::size_t row = 0; row < table.rows; ++row) {
        max_uid = std::max(
            max_uid, get_u32(table.fixed, row * kAsnRowSize + kAsnRowUid));
    }
    if (max_uid == 0xFFFFFFFFu) {
        throw Error(function + ": the assignment table's largest uid is "
                    "0xffffffff, so a new uid would overflow");
    }
    std::uint32_t const uid =
        assignment.uid == 0 ? max_uid + 1 : assignment.uid;
    if (assignment.uid != 0 && assignment.uid <= max_uid) {
        throw Error(function + ": uid " + std::to_string(assignment.uid) +
                    " is not greater than the assignment table's largest uid " +
                    std::to_string(max_uid) +
                    "; this writer appends the new uid's VarMeta group at the end");
    }
    for (std::size_t row = 0; row < table.rows; ++row) {
        std::uint32_t const offset = static_cast<std::uint32_t>(row * kAsnRowSize);
        std::int32_t const index = entry_index_for_offset(table.entries, offset);
        if (index < 0 ||
            table.entries[static_cast<std::size_t>(index)].state != kStateLive) {
            continue;
        }
        if (get_u32(table.fixed, offset + kAsnRowTask) == assignment.task_uid &&
            get_u32(table.fixed, offset + kAsnRowResource) ==
                assignment.resource_uid) {
            throw Error(function + ": a live assignment already names task " +
                        std::to_string(assignment.task_uid) + " and resource " +
                        std::to_string(assignment.resource_uid) + " (row " +
                        std::to_string(row) + ", uid " +
                        std::to_string(get_u32(table.fixed, offset + kAsnRowUid)) +
                        "); a duplicate assignment is refused");
        }
    }
    for (VarEntry const& entry : read_var_entries(table.var_meta)) {
        if (entry.mid > uid) {
            throw Error(function + ": the assignment table already holds a uid (" +
                        std::to_string(entry.mid) +
                        ") larger than the new uid " + std::to_string(uid) +
                        "; refusing to append a variable-entry group out of order");
        }
    }

    std::uint32_t work_ticks = assignment.work_ticks;
    if (work_ticks == 0) {
        work_ticks = static_cast<std::uint32_t>(
            std::lround(static_cast<double>(task.duration_ticks) * units_percent /
                        100.0));
    }
    std::uint32_t start = assignment.start;
    std::uint32_t finish = assignment.finish;
    if (start == kInvalidDate) {
        start = task.start;
    }
    if (finish == kInvalidDate) {
        finish = task.finish;
    }
    std::uint32_t resume = assignment.resume;
    if (resume == kInvalidDate) {
        resume = start;
    }
    require_valid_date(start, "the assignment start", function);
    require_valid_date(finish, "the assignment finish", function);
    require_valid_date(resume, "the assignment resume", function);
    if (start > finish) {
        throw Error(function + ": the assignment start " + format_date(start) +
                    " is after its finish " + format_date(finish));
    }

    for (VarEntry const& entry : read_var_entries(table.var_meta)) {
        if (entry.field_id == kAsnContourFieldId && entry.mid == uid) {
            throw Error(function + ": uid " + std::to_string(uid) +
                        " already has a planned-work contour entry");
        }
    }

    std::size_t const new_row = table.rows;
    std::size_t const new_offset = new_row * kAsnRowSize;

    /* Template: the last live non-placeholder assignment row (or a zero row when
     * the table has none).  Only the measured row fields are overwritten. */
    std::size_t template_row = table.rows;
    for (std::size_t row = 0; row < table.rows; ++row) {
        std::uint32_t const offset = static_cast<std::uint32_t>(row * kAsnRowSize);
        std::int32_t const index = entry_index_for_offset(table.entries, offset);
        if (index < 0 ||
            table.entries[static_cast<std::size_t>(index)].state != kStateLive) {
            continue;
        }
        if (get_u32(table.fixed, offset + kAsnRowTask) == 0) {
            continue;
        }
        template_row = row;
    }
    std::vector<std::uint8_t> row(kAsnRowSize, 0);
    if (template_row < table.rows) {
        row.assign(table.fixed.begin() +
                       static_cast<std::ptrdiff_t>(template_row * kAsnRowSize),
                   table.fixed.begin() + static_cast<std::ptrdiff_t>(
                                             (template_row + 1) * kAsnRowSize));
    }
    put_u32(row, kAsnRowUid, uid);
    put_u32(row, kAsnRowTask, assignment.task_uid);
    put_u32(row, kAsnRowResource, assignment.resource_uid);
    put_f64(row, kAsnRowUnits, units_percent * 100.0);
    put_f64(row, kAsnRowWork, static_cast<double>(work_ticks) * 100.0);
    put_f64(row, kAsnRowZeroWork, 0.0);
    put_f64(row, kAsnRowWorkCopy2, static_cast<double>(work_ticks) * 100.0);
    put_f64(row, kAsnRowWorkCopy, static_cast<double>(work_ticks) * 100.0);
    put_u32(row, kAsnRowStart, start);
    put_u32(row, kAsnRowFinish, finish);
    put_u32(row, kAsnRowResume, resume);
    put_u32(row, kAsnRowUnmapped, kAsnUnmappedValue);
    /* +104 is a second Start copy: measured on assign1.mpp's real rows
     * (0x3d1112c0 = T1's +104) and on w12-assignadd.mpp's added row. */
    put_u32(row, kAsnRowStartCopy, start);

    std::vector<std::uint8_t> meta_entry(kAsnMetaEntrySize, 0);
    meta_entry[kMetaStateByte] = kStateLive;
    put_u16(meta_entry, kMetaFieldCountWord,
            static_cast<std::uint16_t>(kAsnNewRowFieldCount));
    put_u32(meta_entry, kMetaRowOffsetWord,
            static_cast<std::uint32_t>(new_offset));
    std::copy(kAsnMetaTail.begin(), kAsnMetaTail.end(),
              meta_entry.begin() + 8);

    LinkIdentity const assignment_identity = make_link_identity(doc, function);
    std::vector<std::uint8_t> fixed2_record;
    fixed2_record.insert(fixed2_record.end(), assignment_identity.begin(),
                         assignment_identity.end());
    fixed2_record.insert(fixed2_record.end(), task.identity.begin(),
                         task.identity.end());
    fixed2_record.insert(fixed2_record.end(), resource.identity.begin(),
                         resource.identity.end());

    std::vector<std::uint8_t> fixed2_entry(kAsnF2MetaEntrySize, 0);
    put_u32(fixed2_entry, kAsnF2MetaRecordOffsetWord,
            static_cast<std::uint32_t>(new_row * kAsnF2RecordSize));
    fixed2_entry[kAsnF2MetaFlagA] = 0xCF;
    fixed2_entry[kAsnF2MetaFlagB] = 0x09;
    fixed2_entry[kAsnF2MetaMidByte] = 0x10;
    fixed2_entry[kAsnF2MetaLastByte] = 0x28;

    /* The four measured variable entries, in Project's own order. */
    std::vector<std::uint8_t> created = var_block_payload(
        table.var_meta, table.var2,
        template_row < table.rows
            ? get_u32(table.fixed, template_row * kAsnRowSize + kAsnRowUid)
            : 0,
        kAsnCreatedFieldId);
    if (created.size() != 4) {
        created.assign(4, 0xFF);
    }
    std::vector<std::vector<std::uint8_t>> payloads;
    payloads.push_back(assignment_contour(work_ticks));
    payloads.push_back(created);
    payloads.push_back(std::vector<std::uint8_t>(16, 0));
    payloads.push_back(std::vector<std::uint8_t>(16, 0));
    std::array<std::uint32_t, 4> const keys = {
        kAsnContourFieldId, kAsnCreatedFieldId, kAsnInternalAFieldId,
        kAsnInternalBFieldId};

    std::vector<std::uint8_t> fixed = table.fixed;
    fixed.insert(fixed.end(), row.begin(), row.end());
    std::vector<std::uint8_t> meta = table.meta;
    meta.insert(meta.end(), meta_entry.begin(), meta_entry.end());
    put_u32(meta, kMetaCountWord, static_cast<std::uint32_t>(table.rows + 1));
    put_u32(meta, kMetaFixedSizeWord, static_cast<std::uint32_t>(fixed.size()));

    std::vector<std::uint8_t> fixed2 = table.fixed2;
    fixed2.insert(fixed2.end(), fixed2_record.begin(), fixed2_record.end());
    std::vector<std::uint8_t> fixed2_meta = table.fixed2_meta;
    fixed2_meta.insert(fixed2_meta.end(), fixed2_entry.begin(),
                       fixed2_entry.end());
    put_u32(fixed2_meta, kMetaCountWord,
            static_cast<std::uint32_t>(table.rows + 1));
    put_u32(fixed2_meta, kMetaFixedSizeWord,
            static_cast<std::uint32_t>(fixed2.size()));

    std::vector<std::uint8_t> var_meta = table.var_meta;
    std::vector<std::uint8_t> var2 = table.var2;
    /* The new uid is strictly the largest (checked above), so appending the
     * entries at the end keeps Project's measured uid grouping.  The key order
     * is the table's measured order, not a sorted one (the Assignment keys are
     * deliberately not ascending in Project's own file), so this does not go
     * through insert_var_entry's sort. */
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        std::size_t const offset = var2.size();
        std::vector<std::uint8_t> const block = encode_block(payloads[i]);
        var2.insert(var2.end(), block.begin(), block.end());
        std::vector<std::uint8_t> entry(kVarMetaEntrySize, 0);
        put_u32(entry, 0, uid);
        put_u32(entry, 4, static_cast<std::uint32_t>(offset));
        put_u32(entry, 8, keys[i]);
        var_meta.insert(var_meta.end(), entry.begin(), entry.end());
    }
    put_u32(var_meta, 8,
            static_cast<std::uint32_t>(
                (var_meta.size() - kVarMetaHeaderSize) / kVarMetaEntrySize));
    put_u32(var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(var2.size()));

    CfbDocument work = CfbDocument::from_bytes(doc.to_bytes());
    work.replace_stream(table.fixed_path, fixed);
    work.replace_stream(table.meta_path, meta);
    work.replace_stream(table.fixed2_path, fixed2);
    work.replace_stream(table.fixed2_meta_path, fixed2_meta);
    work.replace_stream(table.var_meta_path, var_meta);
    work.replace_stream(table.var2_path, var2);

    CfbDocument const reread = save_and_verify(work, path, function,
                                               "114/TBkndTask");

    if (reread.read_stream(table.fixed_path) != fixed ||
        reread.read_stream(table.meta_path) != meta ||
        reread.read_stream(table.fixed2_path) != fixed2 ||
        reread.read_stream(table.fixed2_meta_path) != fixed2_meta ||
        reread.read_stream(table.var_meta_path) != var_meta ||
        reread.read_stream(table.var2_path) != var2) {
        throw Error(function + ": the six written assignment streams do not read "
                    "back with the bytes that were built");
    }
    for (std::string const& stream : doc.stream_paths()) {
        if (stream == table.fixed_path || stream == table.meta_path ||
            stream == table.fixed2_path || stream == table.fixed2_meta_path ||
            stream == table.var_meta_path || stream == table.var2_path) {
            continue;
        }
        if (!reread.has_stream(stream) ||
            reread.read_stream(stream) != doc.read_stream(stream)) {
            throw Error(function + ": " + stream +
                        " changed; adding an assignment may only touch the six "
                        "TBkndAssn streams");
        }
    }
    AssignmentTable const after = load_assignment_table(reread, function);
    if (after.rows != table.rows + 1) {
        throw Error(function + ": the assignment table's row count does not read "
                    "back as written");
    }
    std::vector<std::uint8_t> const fixed_after =
        reread.read_stream(table.fixed_path);
    if (get_u32(fixed_after, new_offset + kAsnRowUid) != uid ||
        get_u32(fixed_after, new_offset + kAsnRowTask) != assignment.task_uid ||
        get_u32(fixed_after, new_offset + kAsnRowResource) !=
            assignment.resource_uid ||
        get_u32(fixed_after, new_offset + kAsnRowStart) != start ||
        get_u32(fixed_after, new_offset + kAsnRowFinish) != finish ||
        get_u32(fixed_after, new_offset + kAsnRowResume) != resume) {
        throw Error(function + ": the new assignment row does not read back as "
                    "written");
    }
    std::vector<std::uint8_t> const meta_after = reread.read_stream(table.meta_path);
    std::size_t const new_entry_base =
        kDependentMetaHeaderSize + table.rows * kAsnMetaEntrySize;
    if (meta_after[new_entry_base + kMetaStateByte] != kStateLive ||
        get_u16(meta_after, new_entry_base + kMetaFieldCountWord) !=
            kAsnNewRowFieldCount ||
        get_u32(meta_after, new_entry_base + kMetaRowOffsetWord) !=
            static_cast<std::uint32_t>(new_offset)) {
        throw Error(function + ": the new assignment FixedMeta entry does not "
                    "read back as written");
    }
    std::vector<std::uint8_t> const fixed2_after =
        reread.read_stream(table.fixed2_path);
    if (!std::equal(assignment_identity.begin(), assignment_identity.end(),
                    fixed2_after.begin() + static_cast<std::ptrdiff_t>(
                                               new_row * kAsnF2RecordSize)) ||
        !std::equal(task.identity.begin(), task.identity.end(),
                    fixed2_after.begin() + static_cast<std::ptrdiff_t>(
                                               new_row * kAsnF2RecordSize + 16)) ||
        !std::equal(resource.identity.begin(), resource.identity.end(),
                    fixed2_after.begin() + static_cast<std::ptrdiff_t>(
                                               new_row * kAsnF2RecordSize + 32))) {
        throw Error(function + ": the new assignment Fixed2Data record does not "
                    "read back as written");
    }
    std::vector<std::uint8_t> const fixed2_meta_after =
        reread.read_stream(table.fixed2_meta_path);
    if (get_u32(fixed2_meta_after,
                kDependentMetaHeaderSize + table.rows * kAsnF2MetaEntrySize +
                    kAsnF2MetaRecordOffsetWord) !=
        static_cast<std::uint32_t>(new_row * kAsnF2RecordSize)) {
        throw Error(function + ": the new assignment Fixed2Meta entry does not "
                    "read back as written");
    }
    std::vector<std::uint8_t> const var_meta_after =
        reread.read_stream(table.var_meta_path);
    std::vector<std::uint8_t> const var2_after = reread.read_stream(table.var2_path);
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        std::vector<std::uint32_t> const offsets =
            find_var_offsets(var_meta_after, keys[i], uid);
        if (offsets.size() != 1 ||
            read_var_block(var2_after, offsets.front()) != payloads[i]) {
            throw Error(function + ": the new assignment variable entry 0x" +
                        std::to_string(keys[i]) +
                        " does not read back with the payload that was written");
        }
    }

    AddedAssignment result;
    result.uid = uid;
    result.task_uid = assignment.task_uid;
    result.resource_uid = assignment.resource_uid;
    result.row = new_row;
    result.units_percent = units_percent;
    result.work_ticks = work_ticks;
    result.start = start;
    result.finish = finish;
    result.resume = resume;
    result.warnings.push_back(
        "re-read from disk: assignment uid " + std::to_string(uid) + " (task " +
        std::to_string(assignment.task_uid) + " / resource " +
        std::to_string(assignment.resource_uid) + ", row " +
        std::to_string(new_row) + ", " + std::to_string(units_percent) +
        " %, " + std::to_string(work_ticks) +
        " ticks) matches the written row, FixedMeta entry, Fixed2Data record and "
        "four variable entries");
    if (assignment.work_ticks == 0 && work_ticks != 0) {
        result.warnings.push_back(
            "work was inferred from the task's stored duration (+84, " +
            std::to_string(task.duration_ticks) + " ticks) x " +
            std::to_string(units_percent) + " % = " + std::to_string(work_ticks) +
            " ticks");
    }
    if (units_percent != 100.0) {
        result.warnings.push_back(
            "only 100 % assignment units are measured; the flat contour block's "
            "constant +8 word is copied from the measured 100 % shape");
    }
    doc = reread;
    return result;
}

DeletedAssignment delete_assignment_row(CfbDocument& doc, std::uint32_t uid,
                                        std::string const& path) {
    std::string const function = "delete_assignment_row";
    check_output_path(path, function);
    if (uid == 0) {
        throw Error(function + ": uid 0 is the project row, not an assignment row");
    }
    AssignmentTable const table = load_assignment_table(doc, function);
    std::size_t row = table.rows;
    for (std::size_t candidate = 0; candidate < table.rows; ++candidate) {
        if (get_u32(table.fixed, candidate * kAsnRowSize + kAsnRowUid) == uid) {
            row = candidate;
            break;
        }
    }
    if (row == table.rows) {
        throw Error(function + ": no assignment row holds uid " + std::to_string(uid));
    }
    std::size_t const offset = row * kAsnRowSize;
    std::int32_t const index =
        entry_index_for_offset(table.entries, static_cast<std::uint32_t>(offset));
    if (index < 0) {
        throw Error(function + ": assignment uid " + std::to_string(uid) +
                    " (row " + std::to_string(row) +
                    ") has no FixedMeta entry, so Project does not treat it as a "
                    "row; there is nothing to tombstone");
    }
    std::size_t const entry_index = static_cast<std::size_t>(index);
    if (table.entries[entry_index].state != kStateLive) {
        throw Error(function + ": assignment uid " + std::to_string(uid) +
                    " is already tombstoned (FixedMeta entry +0 = 0x" +
                    std::to_string(table.entries[entry_index].state) + ")");
    }
    std::uint32_t const task_uid = get_u32(table.fixed, offset + kAsnRowTask);
    if (task_uid == 0) {
        throw Error(function + ": assignment row " + std::to_string(row) +
                    " is a placeholder row (task uid 0); Project does not show "
                    "it as an assignment, so a delete would be invisible");
    }
    std::vector<LiveTaskRow> const live_tasks = live_task_rows(doc);
    bool task_live = false;
    for (LiveTaskRow const& live : live_tasks) {
        if (live.uid == task_uid) {
            task_live = true;
            break;
        }
    }
    if (!task_live) {
        throw Error(function + ": assignment uid " + std::to_string(uid) +
                    " names task " + std::to_string(task_uid) +
                    ", which no live task row holds; refusing to tombstone an "
                    "assignment Project would already hide");
    }

    std::vector<std::uint8_t> meta = table.meta;
    std::size_t const entry_base =
        kDependentMetaHeaderSize + entry_index * kAsnMetaEntrySize;
    meta[entry_base + kMetaStateByte] = kStateDeleted;
    put_u16(meta, entry_base + kMetaFieldCountWord, 0);
    put_u32(meta, kFixedMetaHeaderFlags,
            get_u32(meta, kFixedMetaHeaderFlags) | kFixedMetaFlagHasDeleted);

    CfbDocument work = CfbDocument::from_bytes(doc.to_bytes());
    work.replace_stream(table.meta_path, meta);

    CfbDocument const reread = save_and_verify(work, path, function,
                                               "114/TBkndTask");

    if (reread.read_stream(table.meta_path) != meta) {
        throw Error(function + ": the written assignment FixedMeta does not read "
                    "back as built");
    }
    if (reread.read_stream(table.fixed_path) != table.fixed ||
        reread.read_stream(table.fixed2_path) != table.fixed2 ||
        reread.read_stream(table.fixed2_meta_path) != table.fixed2_meta ||
        reread.read_stream(table.var_meta_path) != table.var_meta ||
        reread.read_stream(table.var2_path) != table.var2) {
        throw Error(function + ": FixedData/Fixed2Data/Fixed2Meta/VarMeta/Var2Data "
                    "changed; an assignment delete must tombstone the FixedMeta "
                    "entry only");
    }
    for (std::string const& stream : doc.stream_paths()) {
        if (stream == table.meta_path) {
            continue;
        }
        if (!reread.has_stream(stream) ||
            reread.read_stream(stream) != doc.read_stream(stream)) {
            throw Error(function + ": " + stream +
                        " changed; deleting an assignment may only tombstone its "
                        "FixedMeta entry and set the table's header flag");
        }
    }
    AssignmentTable const after = load_assignment_table(reread, function);
    if (after.rows != table.rows || after.meta.size() != table.meta.size()) {
        throw Error(function + ": the assignment table's geometry changed size; a "
                    "delete must not shrink a stream");
    }
    std::uint32_t const flags_before =
        get_u32(table.meta, kFixedMetaHeaderFlags);
    std::uint32_t const flags_after = get_u32(after.meta, kFixedMetaHeaderFlags);
    if ((flags_after & kFixedMetaFlagHasDeleted) == 0 ||
        (flags_after & ~kFixedMetaFlagHasDeleted) !=
            (flags_before & ~kFixedMetaFlagHasDeleted)) {
        throw Error(function + ": TBkndAssn/FixedMeta header +4 does not read "
                    "back with bit 0x02 added and no other bit changed");
    }
    if (after.meta[entry_base + kMetaStateByte] != kStateDeleted ||
        get_u16(after.meta, entry_base + kMetaFieldCountWord) != 0) {
        throw Error(function + ": assignment uid " + std::to_string(uid) +
                    " does not read back as a tombstone (+0 0x02, +2 0)");
    }
    if (live_assignment_count(after) + 1 != live_assignment_count(table)) {
        throw Error(function + ": the assignment table does not read back with "
                    "exactly one fewer live assignment");
    }

    DeletedAssignment result;
    result.uid = uid;
    result.task_uid = task_uid;
    result.resource_uid = get_u32(table.fixed, offset + kAsnRowResource);
    result.units_percent =
        [&table, offset]() {
            double value = 0.0;
            std::memcpy(&value, table.fixed.data() + offset + kAsnRowUnits,
                        sizeof value);
            return value / 100.0;
        }();
    result.work_ticks = static_cast<std::uint32_t>(
        std::llround([&table, offset]() {
            double value = 0.0;
            std::memcpy(&value, table.fixed.data() + offset + kAsnRowWork,
                        sizeof value);
            return value;
        }() /
                     100.0));
    result.row = row;
    result.warnings.push_back(
        "re-read from disk: assignment uid " + std::to_string(uid) + " (row " +
        std::to_string(row) +
        ") is a tombstone (FixedMeta +0 0x02, +2 0) and TBkndAssn/FixedMeta "
        "header +4 has bit 0x02; every stream size is unchanged");
    doc = reread;
    return result;
}

} /* namespace mppwrite */
