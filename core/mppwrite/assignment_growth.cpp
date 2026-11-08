/*
 * assignment_growth.cpp -- W34-3.1: the measured assignment ADD and REMOVE
 * shapes.  The rules, the measured offsets and the refusals are in
 * assignment_growth.h and spec/writers/table-growth.md; this file is their
 * executable form.
 *
 * WHAT ONE CALL BUILDS (wave30-assign-add, wave30-assign-delete; every byte
 * re-measured against the committed control, null save and edit save):
 *
 * ADD   tombstone the target task's placeholder entry in the assignment
 *       FixedMeta, append a row that copies the placeholder with a new uid and
 *       the resource uid, append the Fixed2Data record {fresh, task, resource},
 *       append the Fixed2Meta entry, move the assignment's VarMeta group to the
 *       new uid and append its four blocks, rewrite the task and resource work
 *       roll-ups, the task flag bits and the task's 0x0B4004E5 leaf, then the
 *       four 114/Props assignment scalars and the summary.
 *
 * REMOVE  tombstone the one real assignment's FixedMeta entry, fill the spare
 *       FixedMeta/Fixed2Meta entries with a copy of the real row's pointer
 *       words, append the placeholder copy, append the Fixed2Data record
 *       {fresh, task, placeholder identity}, re-key the real VarMeta group to
 *       the new uid and append its 124 bytes with the 28-byte contour record
 *       zeroed, zero the work roll-ups and the leaf, then the scalars and the
 *       summary.
 *
 * The build is all-or-nothing on a local copy; `doc` is replaced only after the
 * file on disk has been re-read and every stream this writer built, every
 * scalar and the exact changed-stream set have been proved.  A post-write
 * proof failure leaves the saved file at `path` (the same behaviour as
 * save_props_edit), but never replaces `doc`.
 */

#include "assignment_growth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "edit_marker.h"
#include "props_edit.h"
#include "summary_info.h"
#include "table.h"

namespace mppwrite {
namespace {

using Bytes = std::vector<std::uint8_t>;

/* ---- the streams the edit reads or writes ------------------------------- */
constexpr char const* kAssnFixedData = "114/TBkndAssn/FixedData";
constexpr char const* kAssnFixedMeta = "114/TBkndAssn/FixedMeta";
constexpr char const* kAssnFixed2Data = "114/TBkndAssn/Fixed2Data";
constexpr char const* kAssnFixed2Meta = "114/TBkndAssn/Fixed2Meta";
constexpr char const* kAssnVarMeta = "114/TBkndAssn/VarMeta";
constexpr char const* kAssnVar2Data = "114/TBkndAssn/Var2Data";
constexpr char const* kTaskFixedData = "114/TBkndTask/FixedData";
constexpr char const* kTaskFixedMeta = "114/TBkndTask/FixedMeta";
constexpr char const* kTaskFixed2Data = "114/TBkndTask/Fixed2Data";
constexpr char const* kTaskVarMeta = "114/TBkndTask/VarMeta";
constexpr char const* kTaskVar2Data = "114/TBkndTask/Var2Data";
constexpr char const* kRscFixedData = "114/TBkndRsc/FixedData";
constexpr char const* kRscFixedMeta = "114/TBkndRsc/FixedMeta";
constexpr char const* kRscFixed2Data = "114/TBkndRsc/Fixed2Data";

/* ---- the measured table geometry ---------------------------------------- */
constexpr std::size_t kMetaHeaderBytes = 16;
constexpr std::uint32_t kTableMagic = 0xFADFADBAu;

constexpr std::size_t kAssnRowBytes = 110;
constexpr std::size_t kAssnFmEntryBytes = 34;
constexpr std::size_t kAssnF2mEntryBytes = 53;
constexpr std::size_t kAssnF2RecordBytes = 48;

constexpr std::size_t kTaskHeaderBytes = 48;
constexpr std::size_t kTaskRowBytes = 202;
constexpr std::size_t kTaskFmEntryBytes = 47;
constexpr std::size_t kTaskF2RecordBytes = 64;

constexpr std::size_t kRscHeaderBytes = 48;
constexpr std::size_t kRscRowBytes = 172;
constexpr std::size_t kRscFmEntryBytes = 37;
constexpr std::size_t kRscF2RecordBytes = 40;

/* Task and Resource Fixed2Data start with three records that are not rows. */
constexpr std::size_t kF2SpacerRecords = 3;

constexpr std::size_t kVarMetaHeaderBytes = 24;
constexpr std::size_t kVarMetaEntryBytes = 12;
constexpr std::size_t kVar2SizeOffset = 0x14;

/* ---- the measured row and entry words ----------------------------------- */
constexpr std::size_t kAssnRowUid = 0;
constexpr std::size_t kAssnRowTask = 4;
constexpr std::size_t kAssnRowResource = 8;
constexpr std::size_t kAssnRowUnits = 12;
constexpr std::size_t kAssnRowWork = 20;
constexpr std::size_t kAssnRowZero = 28;
constexpr std::size_t kAssnRowWorkCopy1 = 36;
constexpr std::size_t kAssnRowWorkCopy2 = 44;

/* The task row offsets are task_row::kWork and task_row::kWorkRollup; the
 * measured rows carry the same double at both. */
constexpr std::size_t kTaskRowWork = 8;
constexpr std::size_t kTaskRowWorkRollup = 24;
/* The task FixedMeta entry's flag byte (payload +16). */
constexpr std::size_t kTaskFmFlags = 16;
/* The resource row's three work copies. */
constexpr std::size_t kRscRowWork1 = 48;
constexpr std::size_t kRscRowWork2 = 90;
constexpr std::size_t kRscRowWork3 = 106;
/* The resource FixedMeta entry's measured flag byte (payload +13). */
constexpr std::size_t kRscFmFlag = 13;

/* The field keys this edit owns. */
constexpr std::uint32_t kTaskWorkLeafKey = 0x0B4004E5u;
constexpr std::uint32_t kAssnContourKey = 0x0F400031u;
constexpr std::uint32_t kAssnSessionKey = 0x0F40027Au;
constexpr std::uint32_t kAssnBlock299Key = 0x0F400299u;
constexpr std::uint32_t kAssnBlock29BKey = 0x0F40029Bu;

/* The 114/Props record whose four-byte payload Project copies into the new
 * assignment's 0x0F40027A block (measured on both pairs: 12 2a 18 3d). */
constexpr std::uint32_t kAssnSessionRecord = 0x02400007u;

/* The placeholder resource identity Project writes into an unassigned
 * assignment's Fixed2Data record, measured on every placeholder row. */
std::array<std::uint8_t, 16> const& null_identity() {
    static const std::array<std::uint8_t, 16> kNull = {
        0x78, 0x8B, 0xCB, 0xA0, 0x8C, 0x2A, 0x6D, 0x43,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF};
    return kNull;
}

/* ---- byte helpers ------------------------------------------------------- */
[[noreturn]] void fail(std::string const& message) { throw Error(message); }

std::uint8_t get_u8(Bytes const& bytes, std::size_t offset) {
    return bytes[offset];
}

std::uint16_t get_u16(Bytes const& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1])
                                   << 8));
}

std::uint32_t get_u32(Bytes const& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint32_t get_big_u32(Bytes const& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

double get_f64(Bytes const& bytes, std::size_t offset) {
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        bits |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
    }
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

void put_u8(Bytes& bytes, std::size_t offset, std::uint8_t value) {
    bytes[offset] = value;
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

void put_f64(Bytes& bytes, std::size_t offset, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(bits >> (8 * i)) & 0xFFu);
    }
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

Bytes slice(Bytes const& bytes, std::size_t offset, std::size_t length) {
    if (offset > bytes.size() || length > bytes.size() - offset) {
        fail("a measured block runs past its stream");
    }
    return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
}

std::size_t divided(std::size_t total, std::size_t base, std::size_t unit,
                    char const* what) {
    if (total < base || (total - base) % unit != 0) {
        fail(std::string(what) + " is " + std::to_string(total) +
             " bytes, not " + std::to_string(base) + " + " +
             std::to_string(unit) + " * rows");
    }
    return (total - base) / unit;
}

/* Same policy as the other writers. */
void check_output_path(std::string const& path, std::string const& function) {
    if (path.empty()) {
        fail(function + ": the output path is empty");
    }
    std::filesystem::path const out(path);
    if (out.filename().empty()) {
        fail(function + ": " + path + " names a directory, not a file to save to");
    }
    std::error_code error;
    if (std::filesystem::is_directory(out, error)) {
        fail(function + ": " + path + " is a directory");
    }
    std::filesystem::path const parent = out.parent_path();
    if (parent.empty()) {
        return;
    }
    error.clear();
    bool const parent_exists = std::filesystem::exists(parent, error);
    if (error || !parent_exists) {
        fail(function + ": cannot save to " + path + ": the directory " +
             parent.string() + " does not exist");
    }
}

/* A path with the OLE control prefix and the right-alignment spaces stripped
 * from every component, so "\005DocumentSummaryInformation" and
 * "DocumentSummaryInformation" name the same stream for the changed-set proof
 * while "114/Props" and "214/Props" stay distinct. */
std::string normalise_stream_path(std::string const& path) {
    std::string out;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t const slash = path.find('/', start);
        std::string const component =
            path.substr(start, slash == std::string::npos ? std::string::npos
                                                          : slash - start);
        std::size_t at = 0;
        while (at < component.size() &&
               (component[at] == ' ' ||
                static_cast<unsigned char>(component[at]) < 0x20)) {
            ++at;
        }
        if (!out.empty()) {
            out += '/';
        }
        out += component.substr(at);
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return out;
}

/* The DocumentSummaryInformation stream path as CfbDocument reports it (the
 * OLE control prefix is part of the stored name).  Same search as
 * summary_info.cpp. */
std::string summary_path(CfbDocument const& doc) {
    for (std::string const& path : doc.stream_paths()) {
        std::string const normalised = normalise_stream_path(path);
        std::size_t const slash = normalised.find_last_of('/');
        std::string_view const leaf =
            slash == std::string::npos
                ? std::string_view(normalised)
                : std::string_view(normalised).substr(slash + 1);
        if (leaf == kDocumentSummaryStream) {
            return path;
        }
    }
    return {};
}

/* ---- shape readers ------------------------------------------------------ */
struct TableMeta {
    std::uint32_t flag = 0;
    std::uint32_t count = 0;
    std::uint32_t size = 0;
    std::size_t entries = 0;
};

TableMeta check_meta(Bytes const& stream, std::size_t entry_bytes,
                     std::size_t data_size, char const* what) {
    if (stream.size() < kMetaHeaderBytes) {
        fail(std::string(what) + " is shorter than its 16-byte header");
    }
    if (get_u32(stream, 0) != kTableMagic) {
        fail(std::string(what) + " does not start with the table magic");
    }
    if ((stream.size() - kMetaHeaderBytes) % entry_bytes != 0) {
        fail(std::string(what) + " is not a 16-byte header plus " +
             std::to_string(entry_bytes) + "-byte entries");
    }
    TableMeta meta;
    meta.flag = get_u32(stream, 4);
    meta.count = get_u32(stream, 8);
    meta.size = get_u32(stream, 12);
    meta.entries = (stream.size() - kMetaHeaderBytes) / entry_bytes;
    if (meta.size != data_size) {
        fail(std::string(what) + ": header +12 is " + std::to_string(meta.size) +
             ", but its data stream is " + std::to_string(data_size) +
             " bytes");
    }
    return meta;
}

struct VarEntryView {
    std::uint32_t uid = 0;
    std::uint32_t offset = 0;
    std::uint32_t key = 0;
};

std::vector<VarEntryView> load_var_meta(Bytes const& var_meta,
                                        Bytes const& var2, char const* what) {
    if (var_meta.size() < kVarMetaHeaderBytes) {
        fail(std::string(what) + " is shorter than its 24-byte header");
    }
    if (get_u32(var_meta, 0) != kTableMagic) {
        fail(std::string(what) + " does not start with the table magic");
    }
    if ((var_meta.size() - kVarMetaHeaderBytes) % kVarMetaEntryBytes != 0) {
        fail(std::string(what) + " is not 24 + 12 * entries bytes");
    }
    if (get_u32(var_meta, kVar2SizeOffset) != var2.size()) {
        fail(std::string(what) + ": header +0x14 is " +
             std::to_string(get_u32(var_meta, kVar2SizeOffset)) +
             ", but its Var2Data is " + std::to_string(var2.size()) + " bytes");
    }
    std::vector<VarEntryView> entries;
    std::size_t const count =
        (var_meta.size() - kVarMetaHeaderBytes) / kVarMetaEntryBytes;
    entries.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        VarEntryView entry;
        entry.uid = get_u32(var_meta, kVarMetaHeaderBytes + i * 12);
        entry.offset = get_u32(var_meta, kVarMetaHeaderBytes + i * 12 + 4);
        entry.key = get_u32(var_meta, kVarMetaHeaderBytes + i * 12 + 8);
        if (entry.offset + 4 > var2.size()) {
            fail(std::string(what) + ": entry " + std::to_string(i) +
                 " points past Var2Data");
        }
        std::uint32_t const length = get_u32(var2, entry.offset);
        if (length > var2.size() - entry.offset - 4) {
            fail(std::string(what) + ": entry " + std::to_string(i) +
                 "'s block runs past Var2Data");
        }
        entries.push_back(entry);
    }
    return entries;
}

std::uint32_t block_length(Bytes const& var2, std::uint32_t offset) {
    return get_u32(var2, offset);
}

struct VarGroup {
    std::uint32_t uid = 0;
    std::vector<std::size_t> indices;
};

std::vector<VarGroup> group_var_entries(std::vector<VarEntryView> const& entries,
                                        char const* what) {
    std::vector<VarGroup> groups;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!groups.empty() && groups.back().uid == entries[i].uid) {
            groups.back().indices.push_back(i);
            continue;
        }
        if (!groups.empty() && groups.back().uid > entries[i].uid) {
            fail(std::string(what) + ": the uid " + std::to_string(entries[i].uid) +
                 " group follows a greater uid; the measured array is ascending");
        }
        VarGroup group;
        group.uid = entries[i].uid;
        group.indices.push_back(i);
        groups.push_back(std::move(group));
    }
    return groups;
}

void check_group_keys(std::vector<VarEntryView> const& entries,
                      std::vector<VarGroup> const& groups, std::size_t group,
                      std::vector<std::uint32_t> const& keys, char const* what) {
    if (groups[group].indices.size() != keys.size()) {
        fail(std::string(what) + ": uid " + std::to_string(groups[group].uid) +
             " has " + std::to_string(groups[group].indices.size()) +
             " entries; the measured shape has " + std::to_string(keys.size()));
    }
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (entries[groups[group].indices[i]].key != keys[i]) {
            fail(std::string(what) + ": uid " + std::to_string(groups[group].uid) +
                 " entry " + std::to_string(i) + " is key 0x" + [&] {
                     char buffer[16];
                     std::snprintf(buffer, sizeof buffer, "%08x",
                                   entries[groups[group].indices[i]].key);
                     return std::string(buffer);
                 }() + "; the measured key order is different");
        }
    }
}

/* The measured assignment FixedMeta entry image. */
Bytes fm_entry(std::uint16_t state, std::uint16_t count, std::uint32_t offset,
               std::uint8_t tail_13) {
    Bytes entry(kAssnFmEntryBytes, 0);
    put_u16(entry, 0, state);
    put_u16(entry, 2, count);
    put_u32(entry, 4, offset);
    entry[8] = 0xF1;
    entry[9] = 0xFF;
    entry[10] = 0x23;
    entry[11] = 0xF0;
    entry[12] = 0x69;
    entry[13] = tail_13;
    entry[14] = 0x02;
    return entry;
}

/* The measured assignment Fixed2Meta entry image. */
Bytes f2m_entry(std::uint32_t offset, std::uint8_t byte_9,
                std::uint8_t byte_52) {
    Bytes entry(kAssnF2mEntryBytes, 0);
    put_u32(entry, 4, offset);
    entry[8] = 0xCF;
    entry[9] = byte_9;
    entry[35] = 0x10;
    entry[52] = byte_52;
    return entry;
}

std::vector<std::uint32_t> const& placeholder_keys() {
    static const std::vector<std::uint32_t> kKeys = {
        kAssnContourKey, kAssnSessionKey, kAssnBlock299Key};
    return kKeys;
}

/* The measured block layout of a new or copied four-block group, relative to
 * the group's base offset: 0x0F40027A at +0 (4 bytes), 0x0F400031 at +8 (72),
 * 0x0F400299 at +84 (16) and 0x0F40029B at +104 (16). */
struct BlockLayout {
    std::uint32_t key;
    std::uint32_t relative;
    std::uint32_t length;
};

BlockLayout const kFourBlockLayout[4] = {
    {kAssnSessionKey, 0, 4},
    {kAssnContourKey, 8, 72},
    {kAssnBlock299Key, 84, 16},
    {kAssnBlock29BKey, 104, 16},
};
constexpr std::size_t kFourBlockBytes = 124;
/* The VarMeta entries are stored in ascending key order, so the group's entry
 * array lists 0x0F400031 before 0x0F40027A even though its block comes second. */
BlockLayout const kFourEntryLayout[4] = {
    {kAssnContourKey, 8, 72},
    {kAssnSessionKey, 0, 4},
    {kAssnBlock299Key, 84, 16},
    {kAssnBlock29BKey, 104, 16},
};
/* The 0x0F400031 contour block's single 28-byte record, relative to the group
 * base (the 44-byte header precedes it). */
constexpr std::size_t kContourRecordOffset = 56;
constexpr std::size_t kContourRecordBytes = 28;

/* The 44-byte contour header plus one 28-byte record, exactly as
 * actuals.cpp's build_work_block writes it. */
Bytes build_contour(double units16, double work, std::uint32_t ticks8) {
    constexpr std::size_t kHeader = 44;
    constexpr std::size_t kRecord = 28;
    Bytes out(kHeader + kRecord, 0);
    put_u32(out, 0, 0x00200000u);
    put_u32(out, 4, static_cast<std::uint32_t>(kHeader));
    put_f64(out, 8, units16);
    put_f64(out, 16, work);
    put_u32(out, 24, ticks8);
    put_u32(out, 28, 0);
    put_u32(out, 32, 1);
    put_u32(out, 36, 0);
    put_u32(out, 40, 0);
    put_f64(out, kHeader, work);
    put_f64(out, kHeader + 8, units16);
    put_u32(out, kHeader + 16, 0);
    put_u32(out, kHeader + 20, 0);
    put_u32(out, kHeader + 24, ticks8);
    return out;
}

/* ---- 114/Props ---------------------------------------------------------- */
constexpr std::size_t kPropsHeaderSize = 16;

std::uint32_t props_scalar(Bytes const& props, std::uint32_t tag,
                           std::string const& function) {
    std::size_t found = static_cast<std::size_t>(-1);
    for (std::size_t at = kPropsHeaderSize; at + 16 <= props.size(); ++at) {
        if (get_u32(props, at) != 4 || get_u32(props, at + 8) != 4) {
            continue;
        }
        if (get_big_u32(props, at + 4) != tag) {
            continue;
        }
        if (found != static_cast<std::size_t>(-1)) {
            fail(std::string(function) + ": the Props scalar tag 0x" + [&] {
                     char buffer[16];
                     std::snprintf(buffer, sizeof buffer, "%08x", tag);
                     return std::string(buffer);
                 }() + " appears more than once");
        }
        found = at;
    }
    if (found == static_cast<std::size_t>(-1)) {
        fail(std::string(function) + ": the Props stream has no 0x" + [&] {
                 char buffer[16];
                 std::snprintf(buffer, sizeof buffer, "%08x", tag);
                 return std::string(buffer);
             }() + " scalar");
    }
    return get_u32(props, found + 12);
}

/* The payload of one directory record, walked from the 16-byte header. */
Bytes props_record(Bytes const& props, std::uint32_t id, char const* function) {
    std::size_t at = kPropsHeaderSize;
    std::size_t found = static_cast<std::size_t>(-1);
    while (at + 12 <= props.size()) {
        std::uint32_t const record_id = get_u32(props, at + 4);
        if ((record_id & 0x00FF0000u) != 0x00400000u) {
            break;
        }
        std::uint32_t const length = get_u32(props, at);
        if (length > props.size() - at - 12) {
            fail(std::string(function) + ": a 114/Props record runs past the "
                 "stream");
        }
        if (record_id == id) {
            if (found != static_cast<std::size_t>(-1)) {
                fail(std::string(function) + ": the 114/Props record 0x" + [&] {
                         char buffer[16];
                         std::snprintf(buffer, sizeof buffer, "%08x", id);
                         return std::string(buffer);
                     }() + " appears more than once");
            }
            found = at;
        }
        at += 12 + length;
    }
    if (found == static_cast<std::size_t>(-1)) {
        fail(std::string(function) + ": the 114/Props stream has no 0x" + [&] {
                 char buffer[16];
                 std::snprintf(buffer, sizeof buffer, "%08x", id);
                 return std::string(buffer);
             }() + " record");
    }
    return slice(props, found + 12, get_u32(props, found));
}

/* ---- the resource table ------------------------------------------------- */
std::size_t find_resource_row(Bytes const& fixed, std::size_t rows,
                              std::uint32_t uid,
                              std::string const& function) {
    for (std::size_t r = 0; r < rows; ++r) {
        if (get_u32(fixed, kRscHeaderBytes + r * kRscRowBytes) == uid) {
            return r;
        }
    }
    fail(std::string(function) + ": the resource table has no UniqueID " +
         std::to_string(uid));
}

/* ---- the task leaf ------------------------------------------------------ */
struct TaskLeaf {
    std::uint32_t offset = 0;
    /* The entry's index in the VarMeta array (for the append shape's re-point). */
    std::size_t entry_index = 0;
};

TaskLeaf find_task_leaf(std::vector<VarEntryView> const& entries,
                        std::uint32_t task_uid, std::string const& function) {
    std::size_t found = 0;
    TaskLeaf leaf;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].uid == task_uid && entries[i].key == kTaskWorkLeafKey) {
            ++found;
            leaf.offset = entries[i].offset;
            leaf.entry_index = i;
        }
    }
    if (found != 1) {
        fail(std::string(function) + ": the task table has " +
             std::to_string(found) + " 0x0B4004E5 blocks for UniqueID " +
             std::to_string(task_uid) + "; the measured shape has one");
    }
    return leaf;
}

/* ---- the measured target shape (W34b-0.1) --------------------------------
 * Every assignment pair (wave30-assign-add / -delete, wave34-assign-add-second
 * / -third) assigns a Work resource to task UniqueID 1 of one fixture family:
 * a MANUAL, top-level, 0 % leaf of 24000 ticks (5 days) on the project
 * calendar, ASAP, no constraint, and a Work resource at 100 % Max. Units with
 * zero rates and cost per use, the default accrual and its own base calendar,
 * each adding 40 hours (2400000.0 in the 60000-per-hour unit).  A task or
 * resource that differs anywhere outside the words below is a save no pair
 * measured, so the edit is refused rather than guessed.  The templates are the
 * target task's and the resource's stored images in
 * wave30-assign-add-control.mpp (the same bytes sit in the other three
 * controls, bar the words masked here).
 *
 * Masked task FixedData words: row id and UniqueID (0..7), the two work words
 * (8..15, 24..31; checked against the measured 40 h by check_measured_work),
 * the free slack (74..77) and the start slack and total slack words
 * (186..193) that depend on the other tasks, the six start/finish copies
 * (96..119) and the start anchor (174..177).  Masked resource FixedData words:
 * row id and UniqueID (0..7), the three work copies (48..55, 90..97,
 * 106..113; the callers check them with check_resource_work) and the
 * per-resource base calendar word (154..157, uid + 2 on every resource of
 * every pair, checked below). */
constexpr double kMeasuredAssignmentWork = 2400000.0;
constexpr std::uint32_t kMeasuredTaskDuration = 24000;

struct SkipRange {
    std::size_t first;
    std::size_t last; /* exclusive */
};

Bytes from_hex(std::string_view hex) {
    auto nibble = [](char c) -> std::uint8_t {
        return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    };
    Bytes out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>((nibble(hex[i]) << 4) |
                                                nibble(hex[i + 1])));
    }
    return out;
}

Bytes const& measured_task_row() {
    static const Bytes kRow = from_hex("0100000001000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000ffffffff0000000000000000f40100000000c05d0000c05d000000000000"
        "c012f23bd827f63b781ef13b201cf63b781ef13b201cf63bffffffffffffffff"
        "8214143dffffffffffffffff0000000000000000000000000000000000000000"
        "0000080007000300800000000100781ef13bffffffffffffffff000000000000"
        "00000000000000000000");
    return kRow;
}

Bytes const& measured_task_fixed2() {
    static const Bytes kRecord = from_hex("89b9c44ed10a11f19f641cc10c054c03000000000000004089b9c44dd10a11f1"
        "9f641cc10c054c03030030000000781ef13b781ef13b201cf63bc05d00000700");
    return kRecord;
}

Bytes const& measured_task_fixed2_meta() {
    static const Bytes kEntry = from_hex("0000000000010000ff4f80010000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000c00000000000010");
    return kEntry;
}

Bytes const& measured_task_fixed_meta() {
    static const Bytes kEntry = from_hex("00000b00fa000000e3bffdfe71c16701074ccf1f100000000000000000000000"
        "000000000000000000000000000000");
    return kEntry;
}

Bytes const& measured_resource_row() {
    static const Bytes kRow = from_hex("0100000001000000000000000088c34000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000003000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000003000000ffff"
        "ffffffffffff020002000000");
    return kRow;
}

Bytes const& measured_resource_fixed_meta() {
    static const Bytes kEntry = from_hex("00000500dc000000e3fffd3f1c46000000000000000000000000000000000000"
        "0000000000");
    return kEntry;
}

/* The first byte of `actual` that differs from `want` outside `skip`, or -1. */
long first_difference(Bytes const& actual, Bytes const& want,
                      std::vector<SkipRange> const& skip) {
    for (std::size_t at = 0; at < want.size() && at < actual.size(); ++at) {
        bool skipped = false;
        for (SkipRange const& range : skip) {
            skipped = skipped || (at >= range.first && at < range.last);
        }
        if (!skipped && actual[at] != want[at]) {
            return static_cast<long>(at);
        }
    }
    return -1;
}

void check_measured_work(double work, std::string const& function) {
    if (work != kMeasuredAssignmentWork) {
        fail(function + ": the assignment's work is not the measured 40 hours "
             "(2400000.0); every Project pair adds 40 hours per assignment, so "
             "another amount is a save no pair measured");
    }
}

/* The task's and the resource's stored images must be the measured ones. */
void check_measured_shape(CfbDocument const& doc, Bytes const& task_fixed,
                          Bytes const& task_meta, std::size_t task_row,
                          std::vector<VarEntryView> const& task_entries,
                          std::uint32_t task_uid, Bytes const& rsc_fixed,
                          Bytes const& rsc_meta, std::size_t rsc_row,
                          std::uint32_t resource_uid,
                          std::string const& function) {
    /* The task's FixedData row. */
    Bytes const task_image =
        slice(task_fixed, kTaskHeaderBytes + task_row * kTaskRowBytes,
              kTaskRowBytes);
    long at = first_difference(
        task_image, measured_task_row(),
        {{0, 32}, {74, 78}, {96, 120}, {174, 178}, {186, 194}});
    if (at >= 0) {
        std::string const detail =
            at == 84 || at == 88
                ? "the task's duration is not the measured 24000 ticks (5 days)"
                : "a duration, constraint, calendar, percent or other word "
                  "differs from the measured task";
        fail(function + ": the target task's FixedData row differs from the "
             "measured task row at byte " + std::to_string(at) + "; " + detail);
    }
    if (get_u32(task_image, task_row::kDuration) != kMeasuredTaskDuration) {
        fail(function + ": the task's duration is not the measured 24000 ticks");
    }

    /* The task's Fixed2Meta entry carries the manual-scheduling byte. */
    Bytes const f2m = doc.read_stream("114/TBkndTask/Fixed2Meta");
    constexpr std::size_t kTaskF2mEntryBytes = 96;
    std::size_t const f2m_at =
        kMetaHeaderBytes + kTaskF2mEntryBytes * (task_row + kF2SpacerRecords);
    if (f2m.size() < f2m_at + kTaskF2mEntryBytes) {
        fail(function + ": the task Fixed2Meta has no entry for the target "
             "task");
    }
    Bytes const f2m_image = slice(f2m, f2m_at, kTaskF2mEntryBytes);
    if (f2m_image[8] != measured_task_fixed2_meta()[8]) {
        fail(function + ": the target task is not manually scheduled (its "
             "Fixed2Meta flags byte is not the measured 0xFF); the measured "
             "Project pairs assign a manually scheduled task");
    }
    at = first_difference(f2m_image, measured_task_fixed2_meta(), {{4, 8}});
    if (at >= 0) {
        fail(function + ": the target task's Fixed2Meta entry differs from the "
             "measured entry at byte " + std::to_string(at));
    }

    /* The task's FixedMeta entry (summary, effort-driven and type bits). */
    Bytes const fm_image =
        slice(task_meta,
              kMetaHeaderBytes + kTaskFmEntryBytes * (task_row + kF2SpacerRecords),
              kTaskFmEntryBytes);
    at = first_difference(fm_image, measured_task_fixed_meta(),
                          {{4, 8}, {16, 17}});
    if (at >= 0) {
        fail(function + ": the target task's FixedMeta entry differs from the "
             "measured entry at byte " + std::to_string(at) + " (summary, "
             "effort-driven and task-type bits are compared)");
    }

    /* The task's Fixed2Data record. */
    Bytes const f2_image =
        slice(doc.read_stream(kTaskFixed2Data),
              kTaskF2RecordBytes * (task_row + kF2SpacerRecords),
              kTaskF2RecordBytes);
    at = first_difference(f2_image, measured_task_fixed2(),
                          {{0, 16}, {22, 23}, {24, 40}, {46, 58}});
    if (at >= 0) {
        fail(function + ": the target task's Fixed2Data record differs from the "
             "measured record at byte " + std::to_string(at));
    }

    /* The task's stored custom-value keys: the measured task holds these
     * eleven blocks and no other (no note, calendar, deadline or type block). */
    static const std::uint32_t kTaskKeys[] = {
        0x0B40000Eu, 0x0B4000A4u, 0x0B4000A5u, 0x0B4000A8u, 0x0B4000A9u,
        0x0B4000AAu, kTaskWorkLeafKey, 0x0B400505u, 0x0B400506u, 0x0B400563u,
        0x0B400564u};
    std::vector<std::uint32_t> task_keys;
    for (VarEntryView const& entry : task_entries) {
        if (entry.uid == task_uid) {
            task_keys.push_back(entry.key);
        }
    }
    std::sort(task_keys.begin(), task_keys.end());
    std::vector<std::uint32_t> const want_task_keys(std::begin(kTaskKeys),
                                                    std::end(kTaskKeys));
    if (task_keys != want_task_keys) {
        fail(function + ": the target task's VarMeta blocks are not the "
             "measured eleven keys (a note, calendar, deadline or other stored "
             "field changes the shape)");
    }

    /* The resource's FixedData row. */
    Bytes const rsc_image =
        slice(rsc_fixed, kRscHeaderBytes + rsc_row * kRscRowBytes, kRscRowBytes);
    at = first_difference(rsc_image, measured_resource_row(),
                          {{0, 8}, {48, 56}, {90, 98}, {106, 114}, {154, 158}});
    if (at >= 0) {
        auto const named = [](long byte) -> char const* {
            if (byte >= 8 && byte < 16) return "Max. Units";
            if (byte >= 16 && byte < 24) return "standard rate";
            if (byte >= 24 && byte < 32) return "overtime rate";
            if (byte >= 72 && byte < 80) return "cost per use";
            if (byte >= 80 && byte < 84) return "accrual";
            return "stored word";
        };
        fail(function + ": the resource's " + named(at) + " is not the "
             "measured one (resource FixedData row byte " + std::to_string(at) +
             "); the type, Max. Units, rates, cost per use and accrual words "
             "are all compared with the measured resource row");
    }
    if (get_u32(rsc_image, 154) != resource_uid + 2) {
        fail(function + ": the resource is not on its own base calendar (word "
             "154 is not the measured resource UniqueID + 2)");
    }
    Bytes const rm_image =
        slice(rsc_meta,
              kMetaHeaderBytes + kRscFmEntryBytes * (rsc_row + kF2SpacerRecords),
              kRscFmEntryBytes);
    /* Byte 12 is 0x1C in the wave-30 controls and 0x5C once the session has
     * saved the file (bit 0x40; every resource row of the wave-34 controls and
     * of the null saves), so it is a save-session bit, not a resource fact. */
    at = first_difference(rm_image, measured_resource_fixed_meta(),
                          {{4, 8}, {12, 13}});
    if (at >= 0) {
        fail(function + ": the resource's FixedMeta entry differs from the "
             "measured entry at byte " + std::to_string(at));
    }

    /* The resource's stored blocks: name, initials, calendar link and the two
     * measured keys, and nothing else (no group, code, email or note). */
    static const std::uint32_t kRscKeys[] = {0x0C400001u, 0x0C400002u,
                                             0x0C400055u, 0x0C4002D6u,
                                             0x0C4002E3u};
    Bytes const rsc_var2 = doc.read_stream("114/TBkndRsc/Var2Data");
    std::vector<VarEntryView> const rsc_entries = load_var_meta(
        doc.read_stream("114/TBkndRsc/VarMeta"), rsc_var2,
        "the resource VarMeta");
    std::vector<std::uint32_t> rsc_keys;
    for (VarEntryView const& entry : rsc_entries) {
        if (entry.uid == resource_uid) {
            rsc_keys.push_back(entry.key);
        }
    }
    std::sort(rsc_keys.begin(), rsc_keys.end());
    std::vector<std::uint32_t> const want_rsc_keys(std::begin(kRscKeys),
                                                   std::end(kRscKeys));
    if (rsc_keys != want_rsc_keys) {
        fail(function + ": the resource's VarMeta blocks are not the measured "
             "five keys (a group, code, label or other stored field changes the "
             "shape)");
    }
}

/* ---- identity ----------------------------------------------------------- */
bool contains(Bytes const& haystack, Bytes const& needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(),
                       needle.end()) != haystack.end();
}

/* Project renumbers every identity's first dword on save; the trailing twelve
 * bytes are stable.  The new record takes the greatest assignment identity's
 * suffix, the first dword one past the greatest, and the first value whose
 * 16-byte image appears in no stream. */
Bytes fresh_identity(CfbDocument const& doc, Bytes const& assn_f2,
                     char const* function) {
    if (assn_f2.empty() || assn_f2.size() % kAssnF2RecordBytes != 0) {
        fail(std::string(function) + ": the assignment Fixed2Data is not a "
             "whole number of 48-byte records");
    }
    std::size_t best = 0;
    std::uint32_t greatest = 0;
    bool have = false;
    for (std::size_t at = 0; at < assn_f2.size(); at += kAssnF2RecordBytes) {
        std::uint32_t const first = get_u32(assn_f2, at);
        if (!have || first > greatest) {
            have = true;
            greatest = first;
            best = at;
        }
    }
    Bytes const suffix = slice(assn_f2, best + 4, 12);
    for (std::uint64_t candidate = static_cast<std::uint64_t>(greatest) + 1;
         candidate <= 0xFFFFFFFFull; ++candidate) {
        Bytes image(16, 0);
        put_u32(image, 0, static_cast<std::uint32_t>(candidate));
        std::copy(suffix.begin(), suffix.end(), image.begin() + 4);
        bool clash = false;
        for (std::string const& path : doc.stream_paths()) {
            if (contains(doc.read_stream(path), image)) {
                clash = true;
                break;
            }
        }
        if (!clash) {
            return image;
        }
    }
    fail(std::string(function) + ": no free assignment identity is left");
}

/* ---- the changed-stream proof ------------------------------------------ */
void check_changed_set(CfbDocument const& before, CfbDocument const& after,
                       std::vector<std::string> const& expected,
                       std::string const& function) {
    std::vector<std::string> before_paths = before.stream_paths();
    std::vector<std::string> after_paths = after.stream_paths();
    std::sort(before_paths.begin(), before_paths.end());
    std::sort(after_paths.begin(), after_paths.end());
    if (before_paths != after_paths) {
        fail(function + ": the written file's stream set is not the opened "
             "file's");
    }
    std::vector<bool> seen(expected.size(), false);
    for (std::string const& path : after_paths) {
        bool const changed =
            before.read_stream(path) != after.read_stream(path);
        std::size_t match = static_cast<std::size_t>(-1);
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (normalise_stream_path(path) == expected[i]) {
                match = i;
            }
        }
        if (match != static_cast<std::size_t>(-1)) {
            if (!changed) {
                fail(function + ": the edit did not change " + expected[match]);
            }
            seen[match] = true;
        } else if (changed) {
            fail(function + ": " + path +
                 " changed; this edit owns only the measured assignment, task, "
                 "resource, Props and summary streams");
        }
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (!seen[i]) {
            fail(function + ": the expected stream " + expected[i] +
                 " is not in the written file");
        }
    }
}

/* ---- shared task/resource checks --------------------------------------- */
void check_task_tables(CfbDocument const& doc, TaskTable const& table,
                       Bytes const& task_fixed, Bytes& task_meta) {
    task_meta = doc.read_stream(kTaskFixedMeta);
    TableMeta const meta = check_meta(task_meta, kTaskFmEntryBytes,
                                      task_fixed.size(), "the task FixedMeta");
    if (meta.entries != table.rows() + kF2SpacerRecords) {
        fail("the task FixedMeta holds " + std::to_string(meta.entries) +
             " entries, not rows + 3");
    }
    if (doc.read_stream(kTaskFixed2Data).size() !=
        kTaskF2RecordBytes * (table.rows() + kF2SpacerRecords)) {
        fail("the task Fixed2Data is not 64 * (rows + 3) bytes");
    }
}

std::size_t check_resource_tables(CfbDocument const& doc, Bytes& rsc_fixed,
                                  Bytes& rsc_meta) {
    rsc_fixed = doc.read_stream(kRscFixedData);
    rsc_meta = doc.read_stream(kRscFixedMeta);
    std::size_t const rows = divided(rsc_fixed.size(), kRscHeaderBytes,
                                     kRscRowBytes, "the resource FixedData");
    TableMeta const meta = check_meta(rsc_meta, kRscFmEntryBytes,
                                      rsc_fixed.size(), "the resource FixedMeta");
    if (meta.entries != rows + kF2SpacerRecords) {
        fail("the resource FixedMeta holds " + std::to_string(meta.entries) +
             " entries, not rows + 3");
    }
    if (doc.read_stream(kRscFixed2Data).size() !=
        kRscF2RecordBytes * (rows + kF2SpacerRecords)) {
        fail("the resource Fixed2Data is not 40 * (rows + 3) bytes");
    }
    return rows;
}

void check_assn_row_work(Bytes const& fixed, std::size_t row,
                         char const* what) {
    std::size_t const at = row * kAssnRowBytes;
    if (get_f64(fixed, at + kAssnRowUnits) != 10000.0) {
        fail(std::string(what) + ": row " + std::to_string(row) +
             " is not at the measured 100% units");
    }
    double const work = get_f64(fixed, at + kAssnRowWork);
    if (!std::isfinite(work) || !(work > 0.0)) {
        fail(std::string(what) + ": row " + std::to_string(row) +
             " has no positive finite work");
    }
    if (get_f64(fixed, at + kAssnRowZero) != 0.0) {
        fail(std::string(what) + ": row " + std::to_string(row) +
             " has a non-zero word where the measured rows store 0.0");
    }
    if (get_f64(fixed, at + kAssnRowWorkCopy1) != work ||
        get_f64(fixed, at + kAssnRowWorkCopy2) != work) {
        fail(std::string(what) + ": row " + std::to_string(row) +
             "'s work copies disagree");
    }
}

void check_placeholder_row(Bytes const& fixed, std::size_t row,
                           char const* what) {
    if (get_u32(fixed, row * kAssnRowBytes + kAssnRowResource) !=
        kPlaceholderResourceUid) {
        fail(std::string(what) + ": row " + std::to_string(row) +
             " is not an unassigned placeholder");
    }
    check_assn_row_work(fixed, row, what);
}

/* The task work roll-up words the edit derives from the assignment's work. */
void check_task_work(Bytes const& task_fixed, std::size_t row, double work,
                     char const* what) {
    std::size_t const at = kTaskHeaderBytes + row * kTaskRowBytes;
    if (get_f64(task_fixed, at + kTaskRowWork) != work ||
        get_f64(task_fixed, at + kTaskRowWorkRollup) != work) {
        fail(std::string(what) + ": task row " + std::to_string(row) +
             "'s work words are not the measured value");
    }
}

std::uint8_t task_flags(Bytes const& task_meta, std::size_t row) {
    return get_u8(task_meta, kMetaHeaderBytes +
                                 kTaskFmEntryBytes * (row + kF2SpacerRecords) +
                                 kTaskFmFlags);
}

void check_resource_work(Bytes const& rsc_fixed, std::size_t row, double work,
                         char const* what) {
    std::size_t const at = kRscHeaderBytes + row * kRscRowBytes;
    if (get_f64(rsc_fixed, at + kRscRowWork1) != work ||
        get_f64(rsc_fixed, at + kRscRowWork2) != work ||
        get_f64(rsc_fixed, at + kRscRowWork3) != work) {
        fail(std::string(what) + ": resource row " + std::to_string(row) +
             "'s work words are not the measured value");
    }
}

std::uint8_t rsc_flag(Bytes const& rsc_meta, std::size_t row) {
    return get_u8(rsc_meta, kMetaHeaderBytes +
                                kRscFmEntryBytes * (row + kF2SpacerRecords) +
                                kRscFmFlag);
}

/* Work in 60000-per-hour units -> the contour's ticks * 8. */
std::uint32_t contour_ticks8(double work, char const* what) {
    double const ticks = work / 100.0;
    double const rounded = std::round(ticks);
    if (!std::isfinite(ticks) || ticks < 1.0 ||
        std::fabs(ticks - rounded) > 1e-6) {
        fail(std::string(what) + ": the assignment's work is not a whole "
             "number of ticks");
    }
    if (rounded > 0x1FFFFFFFu) {
        fail(std::string(what) + ": the assignment's work is too large for the "
             "measured contour");
    }
    return static_cast<std::uint32_t>(rounded) * 8u;
}

/* ---- the shared tail: build, save, prove -------------------------------- */
struct Built {
    Bytes assn_fixed;
    Bytes assn_meta;
    Bytes assn_f2_meta;
    Bytes assn_f2;
    Bytes assn_var_meta;
    Bytes assn_var2;
    Bytes task_fixed;
    Bytes task_meta;
    Bytes task_var_meta;
    Bytes task_var2;
    Bytes rsc_fixed;
    Bytes rsc_meta;
    PropsEdit props;
    std::vector<std::string> expected_changed;
};

/* Replace every built stream on `work`, rebuild the summary from the edited
 * task row, write the package through save_props_edit, re-read it and prove
 * every built stream, every scalar and the exact changed-stream set. */
void save_and_prove(CfbDocument& doc, CfbDocument& work, Built const& built,
                    std::string const& path, std::string const& function,
                    bool dry_run) {
    work.replace_stream(kAssnFixedData, built.assn_fixed);
    work.replace_stream(kAssnFixedMeta, built.assn_meta);
    work.replace_stream(kAssnFixed2Meta, built.assn_f2_meta);
    work.replace_stream(kAssnFixed2Data, built.assn_f2);
    work.replace_stream(kAssnVarMeta, built.assn_var_meta);
    work.replace_stream(kAssnVar2Data, built.assn_var2);
    work.replace_stream(kTaskFixedData, built.task_fixed);
    work.replace_stream(kTaskFixedMeta, built.task_meta);
    work.replace_stream(kTaskVarMeta, built.task_var_meta);
    work.replace_stream(kTaskVar2Data, built.task_var2);
    work.replace_stream(kRscFixedData, built.rsc_fixed);
    work.replace_stream(kRscFixedMeta, built.rsc_meta);

    TaskTable const before = TaskTable::from_document(doc);
    TaskTable const after = TaskTable::from_document(work);
    /* The project Work string is "<n> hrs"; every pair's n is a multiple of 40
     * (0, 40, 80, 120), one 40-hour assignment at a time. */
    {
        double const total = get_f64(after.fixed_data(),
                                     task_row_offset(0) + kTaskRowWork);
        double const steps = total / kMeasuredAssignmentWork;
        if (!std::isfinite(steps) || std::fabs(steps - std::round(steps)) > 1e-9) {
            fail(function + ": the project's total work is not a whole number "
                 "of the measured 40-hour steps, so its Work summary string "
                 "would be a hand-made value no pair measured");
        }
    }
    write_project_summary(work, before, after);
    std::string const summary = summary_path(work);
    if (summary.empty()) {
        fail(function + ": the written document has no summary stream");
    }
    Bytes const summary_built = work.read_stream(summary);

    if (dry_run) {
        /* Every refusal the build and the summary derivation can raise has run;
         * nothing is written and `doc` is not replaced. */
        return;
    }
    (void)save_props_edit(work, built.props, path);

    /* The edit marker is the one stream whose change is conditional: the
     * marker writer stores 1 and leaves a file that already holds 1 untouched.
     * Project's own save after an edit writes 1, so a control taken from such a
     * save already carries it; drop the marker from the changed set only in
     * exactly that byte-identical case. */
    std::vector<std::string> expected_changed = built.expected_changed;
    auto const marker =
        std::find(expected_changed.begin(), expected_changed.end(),
                  std::string(kEditMarkerStream));
    if (marker != expected_changed.end() && doc.has_stream(kEditMarkerStream) &&
        work.has_stream(kEditMarkerStream) &&
        doc.read_stream(kEditMarkerStream) ==
            work.read_stream(kEditMarkerStream)) {
        expected_changed.erase(marker);
    }

    auto require_stream = [&](char const* stream_path, Bytes const& expected) {
        if (!work.has_stream(stream_path) ||
            work.read_stream(stream_path) != expected) {
            fail(function + ": the written " + stream_path +
                 " does not hold the bytes the edit built");
        }
    };
    require_stream(kAssnFixedData, built.assn_fixed);
    require_stream(kAssnFixedMeta, built.assn_meta);
    require_stream(kAssnFixed2Meta, built.assn_f2_meta);
    require_stream(kAssnFixed2Data, built.assn_f2);
    require_stream(kAssnVarMeta, built.assn_var_meta);
    require_stream(kAssnVar2Data, built.assn_var2);
    require_stream(kTaskFixedData, built.task_fixed);
    require_stream(kTaskFixedMeta, built.task_meta);
    require_stream(kTaskVarMeta, built.task_var_meta);
    require_stream(kTaskVar2Data, built.task_var2);
    require_stream(kRscFixedData, built.rsc_fixed);
    require_stream(kRscFixedMeta, built.rsc_meta);
    require_stream(summary.c_str(), summary_built);

    Bytes const props_now = work.read_stream(kPropsBackEnd);
    for (PropsScalarEdit const& edit : built.props.scalars) {
        if (props_scalar(props_now, edit.tag, function) != edit.value) {
            fail(function + ": the written 114/Props scalar 0x" + [&] {
                     char buffer[16];
                     std::snprintf(buffer, sizeof buffer, "%08x", edit.tag);
                     return std::string(buffer);
                 }() + " is not " + std::to_string(edit.value));
        }
    }

    check_changed_set(doc, work, expected_changed, function);
    doc = std::move(work);
}

} /* namespace */

AssignmentGrowthWrite assign_resource(CfbDocument& doc,
                                      AssignmentKey const& key,
                                      std::string const& path, bool dry_run) {
    std::string const function = "assignment add";
    if (!dry_run) {
        check_output_path(path, function);
    }
    if (key.task_uid == 0) {
        fail(function + ": UniqueID 0 is the project summary row, not a task");
    }
    if (key.resource_uid == 0 || key.resource_uid == kPlaceholderResourceUid) {
        fail(function + ": UniqueID " + std::to_string(key.resource_uid) +
             " is not a resource this command can assign");
    }

    TaskTable const table = TaskTable::from_document(doc);
    std::size_t const task_row = table.row_for_uid(key.task_uid);
    if (task_row == 0) {
        fail(function + ": UniqueID 0 is the project summary row");
    }
    Bytes const task_fixed = table.fixed_data();
    Bytes task_meta;
    check_task_tables(doc, table, task_fixed, task_meta);
    Bytes const task_f2 = doc.read_stream(kTaskFixed2Data);
    Bytes const task_var_meta = doc.read_stream(kTaskVarMeta);
    Bytes task_var2 = doc.read_stream(kTaskVar2Data);
    std::vector<VarEntryView> const task_entries =
        load_var_meta(task_var_meta, task_var2, "the task VarMeta");
    TaskLeaf const leaf = find_task_leaf(task_entries, key.task_uid, function);

    Bytes rsc_fixed;
    Bytes rsc_meta;
    std::size_t const rsc_rows = check_resource_tables(doc, rsc_fixed, rsc_meta);
    std::size_t const rsc_row =
        find_resource_row(rsc_fixed, rsc_rows, key.resource_uid, function);
    check_measured_shape(doc, task_fixed, task_meta, task_row, task_entries,
                         key.task_uid, rsc_fixed, rsc_meta, rsc_row,
                         key.resource_uid, function);

    Bytes const assn_fixed = doc.read_stream(kAssnFixedData);
    Bytes assn_meta = doc.read_stream(kAssnFixedMeta);
    Bytes assn_f2_meta = doc.read_stream(kAssnFixed2Meta);
    Bytes const assn_f2 = doc.read_stream(kAssnFixed2Data);
    Bytes assn_var_meta = doc.read_stream(kAssnVarMeta);
    Bytes assn_var2 = doc.read_stream(kAssnVar2Data);
    Bytes const props = doc.read_stream(kPropsBackEnd);

    std::size_t const rows = divided(assn_fixed.size(), 0, kAssnRowBytes,
                                     "the assignment FixedData");
    TableMeta const fm = check_meta(assn_meta, kAssnFmEntryBytes,
                                    assn_fixed.size(), "the assignment FixedMeta");
    TableMeta const f2m = check_meta(assn_f2_meta, kAssnF2mEntryBytes,
                                     assn_f2.size(),
                                     "the assignment Fixed2Meta");
    if (assn_f2.size() != rows * kAssnF2RecordBytes) {
        fail(function + ": the assignment Fixed2Data is not 48 * rows bytes");
    }

    std::uint32_t const max_uid = [&] {
        std::uint32_t greatest = 0;
        for (std::size_t r = 0; r < rows; ++r) {
            greatest = std::max(
                greatest, get_u32(assn_fixed, r * kAssnRowBytes + kAssnRowUid));
        }
        return greatest;
    }();
    std::vector<std::size_t> target_rows;
    std::vector<std::size_t> real_rows;
    for (std::size_t r = 0; r < rows; ++r) {
        if (get_u32(assn_fixed, r * kAssnRowBytes + kAssnRowTask) !=
            key.task_uid) {
            continue;
        }
        target_rows.push_back(r);
        if (get_u32(assn_fixed, r * kAssnRowBytes + kAssnRowResource) !=
            kPlaceholderResourceUid) {
            real_rows.push_back(r);
        }
    }

    if (!real_rows.empty()) {
        /* ---- the settled append shape (count 1 -> 2, 2 -> 3) -----------
         * Measured on wave34-assign-add-second and wave34-assign-add-third:
         * the task already holds real assignments plus one free placeholder
         * row; Project appends a copy of a real row with the new uid and
         * resource, appends one FixedMeta and one Fixed2Meta entry (no
         * tombstone), appends a fresh four-block VarMeta group, appends the
         * task leaf array and re-points its entry, and raises the roll-ups.
         * No existing FixedMeta entry, Fixed2Data record or Var2Data byte
         * moves. */
        if (real_rows.size() > 2) {
            fail(function + ": the task already has " +
                 std::to_string(real_rows.size()) +
                 " real assignments; the measured counts are 1 and 2");
        }
        if (target_rows.size() != real_rows.size() + 1) {
            fail(function + ": the task has " +
                 std::to_string(target_rows.size()) + " assignment rows for " +
                 std::to_string(real_rows.size()) +
                 " real assignments; the measured append shape is one real row "
                 "per assignment plus one free placeholder row");
        }
        std::size_t placeholder_row = static_cast<std::size_t>(-1);
        for (std::size_t r : target_rows) {
            if (get_u32(assn_fixed, r * kAssnRowBytes + kAssnRowResource) !=
                kPlaceholderResourceUid) {
                continue;
            }
            if (placeholder_row != static_cast<std::size_t>(-1)) {
                fail(function + ": the task has more than one free placeholder "
                     "row; the measured append shape has one");
            }
            placeholder_row = r;
        }
        if (placeholder_row == static_cast<std::size_t>(-1)) {
            fail(function + ": the task has no free placeholder row; the "
                 "measured append shape has one");
        }
        if (fm.count != rows || fm.entries != rows) {
            fail(function + ": the assignment FixedMeta is not the measured "
                 "rows / rows shape");
        }
        if (f2m.count != rows || f2m.entries != rows) {
            fail(function + ": the assignment Fixed2Meta is not the measured "
                 "rows / rows shape");
        }
        /* Every row is either an unassigned placeholder or a real row of the
         * target task, and each row's two meta entries match its kind. */
        for (std::size_t r = 0; r < rows; ++r) {
            std::uint32_t const row_task =
                get_u32(assn_fixed, r * kAssnRowBytes + kAssnRowTask);
            std::uint32_t const row_resource =
                get_u32(assn_fixed, r * kAssnRowBytes + kAssnRowResource);
            bool const placeholder = row_resource == kPlaceholderResourceUid;
            if (!placeholder && row_task != key.task_uid) {
                fail(function + ": assignment row " + std::to_string(r) +
                     " names task UniqueID " + std::to_string(row_task) +
                     " and a real resource; the measured append shape has real "
                     "rows only for the target task");
            }
            Bytes const want_fm =
                placeholder
                    ? (r == placeholder_row
                           ? fm_entry(2, 0,
                                      static_cast<std::uint32_t>(
                                          r * kAssnRowBytes),
                                      0x01)
                           : fm_entry(0, 3,
                                      static_cast<std::uint32_t>(
                                          r * kAssnRowBytes),
                                      0x01))
                    : fm_entry(0, 4,
                               static_cast<std::uint32_t>(r * kAssnRowBytes),
                               0x00);
            if (slice(assn_meta, kMetaHeaderBytes + r * kAssnFmEntryBytes,
                      kAssnFmEntryBytes) != want_fm) {
                fail(function + ": assignment FixedMeta entry " +
                     std::to_string(r) +
                     " is not the measured entry for its row kind");
            }
            Bytes const want_f2m =
                placeholder
                    ? f2m_entry(static_cast<std::uint32_t>(r * kAssnF2RecordBytes),
                                0x01, 0x08)
                    : f2m_entry(static_cast<std::uint32_t>(r * kAssnF2RecordBytes),
                                0x09, 0x28);
            if (slice(assn_f2_meta, kMetaHeaderBytes + r * kAssnF2mEntryBytes,
                      kAssnF2mEntryBytes) != want_f2m) {
                fail(function + ": assignment Fixed2Meta entry " +
                     std::to_string(r) +
                     " is not the measured entry for its row kind");
            }
        }
        /* The real rows are interchangeable copies: identical apart from uid
         * and resource.  The new row copies the first. */
        Bytes const model = slice(assn_fixed, real_rows[0] * kAssnRowBytes,
                                  kAssnRowBytes);
        for (std::size_t r : real_rows) {
            check_assn_row_work(assn_fixed, r, function.c_str());
            if (r == real_rows[0]) {
                continue;
            }
            Bytes const other = slice(assn_fixed, r * kAssnRowBytes,
                                      kAssnRowBytes);
            for (std::size_t at = 0; at < kAssnRowBytes; ++at) {
                if ((at >= kAssnRowUid && at < kAssnRowUid + 4) ||
                    (at >= kAssnRowResource && at < kAssnRowResource + 4)) {
                    continue;
                }
                if (other[at] != model[at]) {
                    fail(function + ": the task's real assignment rows differ at "
                         "row byte " + std::to_string(at) +
                         " outside uid and resource; the measured rows are "
                         "identical there");
                }
            }
        }
        std::vector<std::uint32_t> assigned;
        double old_total = 0.0;
        for (std::size_t r : real_rows) {
            std::uint32_t const uid =
                get_u32(assn_fixed, r * kAssnRowBytes + kAssnRowResource);
            if (uid == key.resource_uid) {
                fail(function + ": resource UniqueID " +
                     std::to_string(key.resource_uid) +
                     " is already assigned to the task");
            }
            if (std::find(assigned.begin(), assigned.end(), uid) !=
                assigned.end()) {
                fail(function + ": two real assignment rows name resource "
                     "UniqueID " + std::to_string(uid));
            }
            assigned.push_back(uid);
            old_total += get_f64(assn_fixed, r * kAssnRowBytes + kAssnRowWork);
        }
        std::sort(assigned.begin(), assigned.end());
        double const work = get_f64(model, kAssnRowWork);
        check_measured_work(work, function);
        double const units = get_f64(model, kAssnRowUnits);
        std::uint32_t const ticks8 = contour_ticks8(work, function.c_str());
        std::size_t const leaf_count = real_rows.size();
        if (block_length(task_var2, leaf.offset) != 4 + 4 * leaf_count ||
            get_u32(task_var2, leaf.offset + 4) != leaf_count) {
            fail(function + ": the task's 0x0B4004E5 leaf is not the measured " +
                 std::to_string(leaf_count) + "-resource array");
        }
        for (std::size_t i = 0; i < leaf_count; ++i) {
            if (get_u32(task_var2, leaf.offset + 8 + 4 * i) != assigned[i]) {
                fail(function + ": the task's 0x0B4004E5 leaf does not name the "
                     "task's real assignments in ascending resource order");
            }
        }
        check_task_work(task_fixed, 0, old_total, function.c_str());
        check_task_work(task_fixed, task_row, old_total, function.c_str());
        if (task_flags(task_meta, 0) != 0x17 ||
            task_flags(task_meta, task_row) != 0x97) {
            fail(function + ": the project or target task flag byte is not the "
                 "measured 0x17 / 0x97 of a task that already has assignments");
        }
        check_resource_work(rsc_fixed, rsc_row, 0.0, function.c_str());

        std::uint32_t const count_before =
            props_scalar(props, kPropsAssignmentCountTag, function.c_str());
        std::uint32_t const freed_before =
            props_scalar(props, kPropsAssignmentFreedTag, function.c_str());
        std::uint32_t const dirty_before =
            props_scalar(props, kPropsAssignmentDirtyTag, function.c_str());
        std::uint32_t const size_before =
            props_scalar(props, kPropsAssignmentSizeTag, function.c_str());
        std::uint32_t const task_size_before =
            props_scalar(props, kPropsTaskSizeTag, function.c_str());
        if (count_before != fm.count || size_before != assn_var2.size() ||
            task_size_before != task_var2.size()) {
            fail(function + ": the 114/Props assignment scalars do not agree "
                 "with the measured control state");
        }
        Bytes const session = props_record(props, kAssnSessionRecord,
                                           function.c_str());
        if (session.size() != 4) {
            fail(function + ": the 114/Props record 0x02400007 is not the "
                 "measured four-byte session word");
        }

        /* ---- build ------------------------------------------------ */
        Built built;
        std::uint32_t const new_uid = max_uid + 1;
        std::size_t const new_rows = rows + 1;
        std::size_t const var2_base = assn_var2.size();
        std::size_t const task_var2_base = task_var2.size();
        std::size_t const new_leaf_count = leaf_count + 1;
        double const new_total = old_total + work;

        built.assn_fixed = assn_fixed;
        {
            Bytes row = model;
            put_u32(row, kAssnRowUid, new_uid);
            put_u32(row, kAssnRowResource, key.resource_uid);
            built.assn_fixed.insert(built.assn_fixed.end(), row.begin(),
                                    row.end());
        }
        built.assn_meta = assn_meta;
        {
            Bytes const entry =
                fm_entry(0, 4, static_cast<std::uint32_t>(rows * kAssnRowBytes),
                         0x00);
            built.assn_meta.insert(built.assn_meta.end(), entry.begin(),
                                   entry.end());
        }
        put_u32(built.assn_meta, 8, static_cast<std::uint32_t>(new_rows));
        put_u32(built.assn_meta, 12, static_cast<std::uint32_t>(
                                         new_rows * kAssnRowBytes));
        built.assn_f2_meta = assn_f2_meta;
        {
            Bytes const entry = f2m_entry(
                static_cast<std::uint32_t>(rows * kAssnF2RecordBytes), 0x09,
                0x28);
            built.assn_f2_meta.insert(built.assn_f2_meta.end(), entry.begin(),
                                      entry.end());
        }
        put_u32(built.assn_f2_meta, 8, static_cast<std::uint32_t>(new_rows));
        put_u32(built.assn_f2_meta, 12,
                static_cast<std::uint32_t>(new_rows * kAssnF2RecordBytes));

        built.assn_f2 = assn_f2;
        {
            Bytes const fresh = fresh_identity(doc, assn_f2, function.c_str());
            Bytes const task_identity =
                slice(task_f2, (task_row + kF2SpacerRecords) * kTaskF2RecordBytes,
                      16);
            Bytes const resource_identity =
                slice(doc.read_stream(kRscFixed2Data),
                      static_cast<std::size_t>(rsc_row + kF2SpacerRecords) *
                              kRscF2RecordBytes,
                      16);
            built.assn_f2.insert(built.assn_f2.end(), fresh.begin(),
                                 fresh.end());
            built.assn_f2.insert(built.assn_f2.end(), task_identity.begin(),
                                 task_identity.end());
            built.assn_f2.insert(built.assn_f2.end(), resource_identity.begin(),
                                 resource_identity.end());
        }

        /* Every existing group stays; the new uid's four entries are appended
         * in ascending key order with the blocks at the end of Var2Data. */
        built.assn_var_meta = assn_var_meta;
        for (BlockLayout const& block : kFourEntryLayout) {
            append_u32(built.assn_var_meta, new_uid);
            append_u32(built.assn_var_meta,
                       static_cast<std::uint32_t>(var2_base + block.relative));
            append_u32(built.assn_var_meta, block.key);
        }
        put_u32(built.assn_var_meta, 8,
                static_cast<std::uint32_t>(
                    (assn_var_meta.size() - kVarMetaHeaderBytes) /
                        kVarMetaEntryBytes +
                    4));
        put_u32(built.assn_var_meta, kVar2SizeOffset,
                static_cast<std::uint32_t>(var2_base + kFourBlockBytes));

        built.assn_var2 = assn_var2;
        append_u32(built.assn_var2, 4);
        built.assn_var2.insert(built.assn_var2.end(), session.begin(),
                               session.end());
        Bytes const contour = build_contour(units * 16.0, work, ticks8);
        append_u32(built.assn_var2, static_cast<std::uint32_t>(contour.size()));
        built.assn_var2.insert(built.assn_var2.end(), contour.begin(),
                               contour.end());
        append_u32(built.assn_var2, 16);
        built.assn_var2.resize(built.assn_var2.size() + 16);
        append_u32(built.assn_var2, 16);
        built.assn_var2.resize(built.assn_var2.size() + 16);

        built.task_fixed = task_fixed;
        put_f64(built.task_fixed, kTaskHeaderBytes + kTaskRowWork, new_total);
        put_f64(built.task_fixed, kTaskHeaderBytes + kTaskRowWorkRollup,
                new_total);
        put_f64(built.task_fixed,
                kTaskHeaderBytes + task_row * kTaskRowBytes + kTaskRowWork,
                new_total);
        put_f64(built.task_fixed,
                kTaskHeaderBytes + task_row * kTaskRowBytes + kTaskRowWorkRollup,
                new_total);
        built.task_meta = task_meta;
        built.task_var_meta = task_var_meta;
        put_u32(built.task_var_meta,
                kVarMetaHeaderBytes + leaf.entry_index * kVarMetaEntryBytes + 4,
                static_cast<std::uint32_t>(task_var2_base));
        put_u32(built.task_var_meta, kVar2SizeOffset,
                static_cast<std::uint32_t>(task_var2_base + 8 +
                                           4 * new_leaf_count));
        built.task_var2 = task_var2;
        append_u32(built.task_var2,
                   static_cast<std::uint32_t>(4 + 4 * new_leaf_count));
        append_u32(built.task_var2,
                   static_cast<std::uint32_t>(new_leaf_count));
        {
            std::vector<std::uint32_t> leaf_uids = assigned;
            leaf_uids.push_back(key.resource_uid);
            std::sort(leaf_uids.begin(), leaf_uids.end());
            for (std::uint32_t uid : leaf_uids) {
                append_u32(built.task_var2, uid);
            }
        }
        built.rsc_fixed = rsc_fixed;
        for (std::size_t offset : {kRscRowWork1, kRscRowWork2, kRscRowWork3}) {
            put_f64(built.rsc_fixed,
                    kRscHeaderBytes + rsc_row * kRscRowBytes + offset, work);
        }
        built.rsc_meta = rsc_meta;

        built.props.stream = kPropsBackEnd;
        built.props.scalars = {
            {kPropsAssignmentCountTag, static_cast<std::uint32_t>(new_rows)},
            {kPropsAssignmentSizeTag,
             static_cast<std::uint32_t>(var2_base + kFourBlockBytes)},
            {kPropsTaskSizeTag, static_cast<std::uint32_t>(
                                    task_var2_base + 8 + 4 * new_leaf_count)},
        };
        if (dirty_before != 0) {
            built.props.scalars.push_back({kPropsAssignmentDirtyTag, 0});
        }

        built.expected_changed = {
            kAssnFixedData,     kAssnFixedMeta,  kAssnFixed2Data,
            kAssnFixed2Meta,    kAssnVarMeta,    kAssnVar2Data,
            kTaskFixedData,     kTaskVarMeta,    kTaskVar2Data,
            kRscFixedData,      kPropsBackEnd,   kDocumentSummaryStream,
            kEditMarkerStream};

        CfbDocument edited = doc;
        save_and_prove(doc, edited, built, path, function, dry_run);

        AssignmentGrowthWrite written;
        written.task_uid = key.task_uid;
        written.resource_uid = key.resource_uid;
        written.assignment_uid = new_uid;
        written.tombstoned_uid = 0;
        written.rows_before = rows;
        written.rows_after = new_rows;
        written.work = work;
        written.work_ticks = static_cast<std::uint32_t>(ticks8 / 8u);
        written.var2_bytes_before = var2_base;
        written.var2_bytes_after = var2_base + kFourBlockBytes;
        written.var_entries_before =
            (assn_var_meta.size() - kVarMetaHeaderBytes) / kVarMetaEntryBytes;
        written.var_entries_after = written.var_entries_before + 4;
        written.props_count_before = count_before;
        written.props_count_after = static_cast<std::uint32_t>(new_rows);
        written.props_freed_before = freed_before;
        written.props_freed_after = freed_before;
        written.props_dirty_before = dirty_before;
        written.props_dirty_after = 0;
        written.props_size_before = size_before;
        written.props_size_after =
            static_cast<std::uint32_t>(var2_base + kFourBlockBytes);
        return written;
    }

    /* The measured placeholder-only add shape (count 0 -> 1): every row an
     * unassigned placeholder, one FixedMeta and one Fixed2Meta entry per row,
     * no spare. */
    if (fm.flag != 4 || fm.count != rows || fm.entries != rows) {
        fail(function + ": the assignment FixedMeta is not the measured "
             "rows / rows / flag 4 shape of a task with no real assignment");
    }
    if (f2m.count != rows || f2m.entries != rows) {
        fail(function + ": the assignment Fixed2Meta is not the measured "
             "rows / rows shape");
    }
    if (block_length(task_var2, leaf.offset) != 8) {
        fail(function + ": the task's 0x0B4004E5 block is " +
             std::to_string(block_length(task_var2, leaf.offset)) +
             " bytes, not the measured 8");
    }
    if (get_u32(task_var2, leaf.offset + 4) != 1 ||
        get_u32(task_var2, leaf.offset + 8) != kPlaceholderResourceUid) {
        fail(function + ": the task's 0x0B4004E5 leaf is not the measured "
             "[1][placeholder]");
    }
    std::size_t const target_row =
        [&] {
            std::size_t found = static_cast<std::size_t>(-1);
            for (std::size_t r = 0; r < rows; ++r) {
                if (get_u32(assn_fixed,
                            r * kAssnRowBytes + kAssnRowTask) != key.task_uid) {
                    continue;
                }
                if (found != static_cast<std::size_t>(-1)) {
                    fail(function + ": more than one assignment row names "
                         "UniqueID " + std::to_string(key.task_uid));
                }
                found = r;
            }
            if (found == static_cast<std::size_t>(-1)) {
                fail(function + ": no assignment row names UniqueID " +
                     std::to_string(key.task_uid));
            }
            return found;
        }();
    std::uint32_t const placeholder_uid =
        get_u32(assn_fixed, target_row * kAssnRowBytes + kAssnRowUid);
    for (std::size_t r = 0; r < rows; ++r) {
        check_placeholder_row(assn_fixed, r, function.c_str());
        if (slice(assn_meta, kMetaHeaderBytes + r * kAssnFmEntryBytes,
                  kAssnFmEntryBytes) !=
            fm_entry(0, 3, static_cast<std::uint32_t>(r * kAssnRowBytes), 0x01)) {
            fail(function + ": assignment FixedMeta entry " + std::to_string(r) +
                 " is not the measured placeholder entry");
        }
        if (slice(assn_f2_meta, kMetaHeaderBytes + r * kAssnF2mEntryBytes,
                  kAssnF2mEntryBytes) !=
            f2m_entry(static_cast<std::uint32_t>(r * kAssnF2RecordBytes), 0x01,
                      0x08)) {
            fail(function + ": assignment Fixed2Meta entry " + std::to_string(r) +
                 " is not the measured placeholder entry");
        }
        Bytes const identity =
            slice(assn_f2, r * kAssnF2RecordBytes + 32, 16);
        if (!std::equal(identity.begin(), identity.end(),
                        null_identity().begin())) {
            fail(function + ": assignment Fixed2Data record " +
                 std::to_string(r) + " does not name the placeholder resource");
        }
    }

    double const work = get_f64(assn_fixed,
                                target_row * kAssnRowBytes + kAssnRowWork);
    check_measured_work(work, function);
    std::uint32_t const ticks8 = contour_ticks8(work, function.c_str());
    double const units = get_f64(
        assn_fixed, target_row * kAssnRowBytes + kAssnRowUnits);

    /* The task and resource roll-ups the edit moves from zero. */
    check_task_work(task_fixed, 0, 0.0, function.c_str());
    check_task_work(task_fixed, task_row, 0.0, function.c_str());
    if (task_flags(task_meta, 0) != 0x07 ||
        task_flags(task_meta, task_row) != 0x07) {
        fail(function + ": the project or target task flag byte is not the "
             "measured 0x07");
    }
    check_resource_work(rsc_fixed, rsc_row, 0.0, function.c_str());
    if (rsc_flag(rsc_meta, 0) != 0x01) {
        fail(function + ": the project resource row's flag byte is not the "
             "measured 0x01");
    }

    /* The assignment's VarMeta: one three-entry group per row, all placeholder
     * keys ascending; the target group is removed and its payloads counted. */
    std::vector<VarEntryView> const assn_entries =
        load_var_meta(assn_var_meta, assn_var2, "the assignment VarMeta");
    std::vector<VarGroup> const groups =
        group_var_entries(assn_entries, function.c_str());
    if (groups.size() != rows) {
        fail(function + ": the assignment VarMeta has " +
             std::to_string(groups.size()) + " uid groups for " +
             std::to_string(rows) + " rows");
    }
    for (std::size_t g = 0; g < groups.size(); ++g) {
        check_group_keys(assn_entries, groups, g, placeholder_keys(),
                         function.c_str());
        if (get_u32(assn_fixed, g * kAssnRowBytes + kAssnRowUid) !=
            groups[g].uid) {
            fail(function + ": assignment row " + std::to_string(g) +
                 "'s uid has no matching VarMeta group in row order");
        }
    }
    std::size_t const target_group = [&] {
        for (std::size_t g = 0; g < groups.size(); ++g) {
            if (groups[g].uid == placeholder_uid) {
                return g;
            }
        }
        fail(function + ": the placeholder uid has no VarMeta group");
    }();
    std::uint32_t removed_payload = 0;
    for (std::size_t index : groups[target_group].indices) {
        removed_payload += block_length(assn_var2, assn_entries[index].offset);
    }

    /* The scalars whose movement the pair measured. */
    std::uint32_t const count_before =
        props_scalar(props, kPropsAssignmentCountTag, function.c_str());
    std::uint32_t const freed_before =
        props_scalar(props, kPropsAssignmentFreedTag, function.c_str());
    std::uint32_t const dirty_before =
        props_scalar(props, kPropsAssignmentDirtyTag, function.c_str());
    std::uint32_t const size_before =
        props_scalar(props, kPropsAssignmentSizeTag, function.c_str());
    if (count_before != fm.count || size_before != assn_var2.size() ||
        dirty_before != 0) {
        fail(function + ": the 114/Props assignment scalars do not agree with "
             "the measured control state");
    }

    Bytes const session = props_record(props, kAssnSessionRecord,
                                       function.c_str());
    if (session.size() != 4) {
        fail(function + ": the 114/Props record 0x02400007 is not the "
             "measured four-byte session word");
    }

    /* ---- build ------------------------------------------------------ */
    Built built;
    std::uint32_t const new_uid = max_uid + 1;
    std::size_t const new_rows = rows + 1;
    std::size_t const var2_base = assn_var2.size();

    built.assn_fixed = assn_fixed;
    {
        Bytes row = slice(assn_fixed, target_row * kAssnRowBytes,
                          kAssnRowBytes);
        put_u32(row, kAssnRowUid, new_uid);
        put_u32(row, kAssnRowResource, key.resource_uid);
        built.assn_fixed.insert(built.assn_fixed.end(), row.begin(), row.end());
    }
    built.assn_meta = assn_meta;
    put_u16(built.assn_meta, kMetaHeaderBytes + target_row * kAssnFmEntryBytes,
            2);
    put_u16(built.assn_meta,
            kMetaHeaderBytes + target_row * kAssnFmEntryBytes + 2, 0);
    {
        Bytes const entry =
            fm_entry(0, 4, static_cast<std::uint32_t>(rows * kAssnRowBytes),
                     0x00);
        built.assn_meta.insert(built.assn_meta.end(), entry.begin(),
                               entry.end());
    }
    put_u32(built.assn_meta, 4, 6);
    put_u32(built.assn_meta, 8, static_cast<std::uint32_t>(new_rows));
    put_u32(built.assn_meta, 12, static_cast<std::uint32_t>(new_rows *
                                                           kAssnRowBytes));
    built.assn_f2_meta = assn_f2_meta;
    {
        Bytes const entry =
            f2m_entry(static_cast<std::uint32_t>(rows * kAssnF2RecordBytes),
                      0x09, 0x28);
        built.assn_f2_meta.insert(built.assn_f2_meta.end(), entry.begin(),
                                  entry.end());
    }
    put_u32(built.assn_f2_meta, 8, static_cast<std::uint32_t>(new_rows));
    put_u32(built.assn_f2_meta, 12, static_cast<std::uint32_t>(
                                       new_rows * kAssnF2RecordBytes));

    built.assn_f2 = assn_f2;
    {
        Bytes const fresh = fresh_identity(doc, assn_f2, function.c_str());
        Bytes const task_identity =
            slice(task_f2, (task_row + kF2SpacerRecords) * kTaskF2RecordBytes,
                  16);
        Bytes const resource_identity =
            slice(doc.read_stream(kRscFixed2Data),
                  static_cast<std::size_t>(rsc_row + kF2SpacerRecords) *
                          kRscF2RecordBytes,
                  16);
        built.assn_f2.insert(built.assn_f2.end(), fresh.begin(), fresh.end());
        built.assn_f2.insert(built.assn_f2.end(), task_identity.begin(),
                             task_identity.end());
        built.assn_f2.insert(built.assn_f2.end(),
                             resource_identity.begin(),
                             resource_identity.end());
    }

    /* The new uid's four entries sit at the end of the assembled array (its
     * uid is the greatest); their blocks tile [base, base + 124). */
    built.assn_var_meta.assign(kVarMetaHeaderBytes, 0);
    std::copy(assn_var_meta.begin(),
              assn_var_meta.begin() + static_cast<std::ptrdiff_t>(
                                          kVarMetaHeaderBytes),
              built.assn_var_meta.begin());
    for (VarEntryView const& entry : assn_entries) {
        if (entry.uid == placeholder_uid) {
            continue;
        }
        append_u32(built.assn_var_meta, entry.uid);
        append_u32(built.assn_var_meta, entry.offset);
        append_u32(built.assn_var_meta, entry.key);
    }
    for (BlockLayout const& block : kFourEntryLayout) {
        append_u32(built.assn_var_meta, new_uid);
        append_u32(built.assn_var_meta,
                   static_cast<std::uint32_t>(var2_base + block.relative));
        append_u32(built.assn_var_meta, block.key);
    }
    put_u32(built.assn_var_meta, 8,
            static_cast<std::uint32_t>(assn_entries.size() - 3 + 4));
    put_u32(built.assn_var_meta, kVar2SizeOffset,
            static_cast<std::uint32_t>(var2_base + kFourBlockBytes));

    built.assn_var2 = assn_var2;
    append_u32(built.assn_var2, 4);
    built.assn_var2.insert(built.assn_var2.end(), session.begin(),
                           session.end());
    Bytes const contour = build_contour(units * 16.0, work, ticks8);
    append_u32(built.assn_var2,
               static_cast<std::uint32_t>(contour.size()));
    built.assn_var2.insert(built.assn_var2.end(), contour.begin(),
                           contour.end());
    append_u32(built.assn_var2, 16);
    built.assn_var2.resize(built.assn_var2.size() + 16);
    append_u32(built.assn_var2, 16);
    built.assn_var2.resize(built.assn_var2.size() + 16);

    built.task_fixed = task_fixed;
    put_f64(built.task_fixed, kTaskHeaderBytes + kTaskRowWork, work);
    put_f64(built.task_fixed, kTaskHeaderBytes + kTaskRowWorkRollup, work);
    put_f64(built.task_fixed,
            kTaskHeaderBytes + task_row * kTaskRowBytes + kTaskRowWork, work);
    put_f64(built.task_fixed,
            kTaskHeaderBytes + task_row * kTaskRowBytes + kTaskRowWorkRollup,
            work);
    built.task_meta = task_meta;
    built.task_var_meta = task_var_meta;
    put_u8(built.task_meta,
           kMetaHeaderBytes + kTaskFmEntryBytes * kF2SpacerRecords +
               kTaskFmFlags,
           0x17);
    put_u8(built.task_meta,
           kMetaHeaderBytes + kTaskFmEntryBytes * (task_row + kF2SpacerRecords) +
               kTaskFmFlags,
           0x97);
    built.task_var2 = task_var2;
    put_u32(built.task_var2, leaf.offset + 4, 1);
    put_u32(built.task_var2, leaf.offset + 8, key.resource_uid);

    built.rsc_fixed = rsc_fixed;
    for (std::size_t offset : {kRscRowWork1, kRscRowWork2, kRscRowWork3}) {
        put_f64(built.rsc_fixed,
                kRscHeaderBytes + rsc_row * kRscRowBytes + offset, work);
    }
    built.rsc_meta = rsc_meta;
    put_u8(built.rsc_meta,
           kMetaHeaderBytes + kRscFmEntryBytes * kF2SpacerRecords + kRscFmFlag,
           0x00);

    built.props.stream = kPropsBackEnd;
    built.props.scalars = {
        {kPropsAssignmentCountTag, static_cast<std::uint32_t>(new_rows)},
        {kPropsAssignmentFreedTag, freed_before + removed_payload},
        {kPropsAssignmentDirtyTag, 1},
        {kPropsAssignmentSizeTag,
         static_cast<std::uint32_t>(var2_base + kFourBlockBytes)},
    };

    std::uint32_t const props_count_after =
        static_cast<std::uint32_t>(new_rows);
    std::uint32_t const props_freed_after = freed_before + removed_payload;
    std::uint32_t const props_size_after =
        static_cast<std::uint32_t>(var2_base + kFourBlockBytes);
    if (props_count_after == count_before ||
        props_freed_after == freed_before || props_size_after == size_before) {
        fail(function + ": a measured scalar would not change");
    }

    built.expected_changed = {
        kAssnFixedData,     kAssnFixedMeta,  kAssnFixed2Data,
        kAssnFixed2Meta,    kAssnVarMeta,    kAssnVar2Data,
        kTaskFixedData,     kTaskFixedMeta,  kTaskVar2Data,
        kRscFixedData,      kRscFixedMeta,   kPropsBackEnd,
        kDocumentSummaryStream, kEditMarkerStream};

    CfbDocument edited = doc;
    save_and_prove(doc, edited, built, path, function, dry_run);

    AssignmentGrowthWrite written;
    written.task_uid = key.task_uid;
    written.resource_uid = key.resource_uid;
    written.assignment_uid = new_uid;
    written.tombstoned_uid = placeholder_uid;
    written.rows_before = rows;
    written.rows_after = new_rows;
    written.work = work;
    written.work_ticks = static_cast<std::uint32_t>(ticks8 / 8u);
    written.var2_bytes_before = var2_base;
    written.var2_bytes_after = var2_base + kFourBlockBytes;
    written.var_entries_before = assn_entries.size();
    written.var_entries_after = assn_entries.size() + 1;
    written.props_count_before = count_before;
    written.props_count_after = props_count_after;
    written.props_freed_before = freed_before;
    written.props_freed_after = props_freed_after;
    written.props_dirty_before = dirty_before;
    written.props_dirty_after = 1;
    written.props_size_before = size_before;
    written.props_size_after = props_size_after;
    return written;
}

AssignmentGrowthWrite unassign_resource(CfbDocument& doc,
                                        AssignmentKey const& key,
                                        std::string const& path, bool dry_run) {
    std::string const function = "assignment remove";
    if (!dry_run) {
        check_output_path(path, function);
    }
    if (key.task_uid == 0) {
        fail(function + ": UniqueID 0 is the project summary row, not a task");
    }
    if (key.resource_uid == 0 || key.resource_uid == kPlaceholderResourceUid) {
        fail(function + ": UniqueID " + std::to_string(key.resource_uid) +
             " is not a resource this command can remove");
    }

    TaskTable const table = TaskTable::from_document(doc);
    std::size_t const task_row = table.row_for_uid(key.task_uid);
    if (task_row == 0) {
        fail(function + ": UniqueID 0 is the project summary row");
    }
    Bytes const task_fixed = table.fixed_data();
    Bytes task_meta;
    check_task_tables(doc, table, task_fixed, task_meta);
    Bytes const task_var_meta = doc.read_stream(kTaskVarMeta);
    Bytes task_var2 = doc.read_stream(kTaskVar2Data);
    std::vector<VarEntryView> const task_entries =
        load_var_meta(task_var_meta, task_var2, "the task VarMeta");
    TaskLeaf const leaf = find_task_leaf(task_entries, key.task_uid, function);
    if (block_length(task_var2, leaf.offset) != 8 ||
        get_u32(task_var2, leaf.offset + 4) != 1 ||
        get_u32(task_var2, leaf.offset + 8) != key.resource_uid) {
        fail(function + ": the task's 0x0B4004E5 leaf does not name the "
             "resource");
    }

    Bytes rsc_fixed;
    Bytes rsc_meta;
    std::size_t const rsc_rows = check_resource_tables(doc, rsc_fixed, rsc_meta);
    std::size_t const rsc_row =
        find_resource_row(rsc_fixed, rsc_rows, key.resource_uid, function);
    check_measured_shape(doc, task_fixed, task_meta, task_row, task_entries,
                         key.task_uid, rsc_fixed, rsc_meta, rsc_row,
                         key.resource_uid, function);

    Bytes const assn_fixed = doc.read_stream(kAssnFixedData);
    Bytes assn_meta = doc.read_stream(kAssnFixedMeta);
    Bytes assn_f2_meta = doc.read_stream(kAssnFixed2Meta);
    Bytes const assn_f2 = doc.read_stream(kAssnFixed2Data);
    Bytes assn_var_meta = doc.read_stream(kAssnVarMeta);
    Bytes assn_var2 = doc.read_stream(kAssnVar2Data);
    Bytes const props = doc.read_stream(kPropsBackEnd);

    std::size_t const rows = divided(assn_fixed.size(), 0, kAssnRowBytes,
                                     "the assignment FixedData");
    if (rows < 2) {
        fail(function + ": the measured removal shape needs at least one "
             "placeholder row plus the real row");
    }
    TableMeta const fm = check_meta(assn_meta, kAssnFmEntryBytes,
                                    assn_fixed.size(), "the assignment FixedMeta");
    TableMeta const f2m = check_meta(assn_f2_meta, kAssnF2mEntryBytes,
                                     assn_f2.size(),
                                     "the assignment Fixed2Meta");
    if (assn_f2.size() != rows * kAssnF2RecordBytes) {
        fail(function + ": the assignment Fixed2Data is not 48 * rows bytes");
    }
    if (fm.flag != 4 || fm.count != rows - 1 || fm.entries != rows) {
        fail(function + ": the assignment FixedMeta is not the measured "
             "(rows - 1) / rows / flag 4 settled shape");
    }
    if (f2m.count != rows - 1 || f2m.entries != rows) {
        fail(function + ": the assignment Fixed2Meta is not the measured "
             "(rows - 1) / rows settled shape");
    }
    std::size_t const real_row = rows - 1;
    std::uint32_t const real_uid =
        get_u32(assn_fixed, real_row * kAssnRowBytes + kAssnRowUid);
    std::uint32_t const max_uid = [&] {
        std::uint32_t greatest = 0;
        for (std::size_t r = 0; r < rows; ++r) {
            greatest = std::max(
                greatest, get_u32(assn_fixed, r * kAssnRowBytes + kAssnRowUid));
        }
        return greatest;
    }();
    if (real_uid != max_uid) {
        fail(function + ": the real assignment is not the greatest uid");
    }
    for (std::size_t r = 0; r + 1 < rows; ++r) {
        check_placeholder_row(assn_fixed, r, function.c_str());
    }
    if (get_u32(assn_fixed, real_row * kAssnRowBytes + kAssnRowTask) !=
            key.task_uid ||
        get_u32(assn_fixed, real_row * kAssnRowBytes + kAssnRowResource) !=
            key.resource_uid) {
        fail(function + ": the last assignment row is not the (task, resource) "
             "the caller named");
    }
    check_assn_row_work(assn_fixed, real_row, function.c_str());
    check_measured_work(get_f64(assn_fixed, real_row * kAssnRowBytes + kAssnRowWork),
                        function);

    for (std::size_t r = 0; r < rows; ++r) {
        Bytes expected;
        if (r + 2 < rows) {
            expected = fm_entry(0, 3,
                                static_cast<std::uint32_t>((r + 1) *
                                                           kAssnRowBytes),
                                0x01);
        } else {
            expected = fm_entry(0, 4,
                                static_cast<std::uint32_t>(real_row *
                                                           kAssnRowBytes),
                                0x00);
        }
        Bytes const entry = slice(assn_meta,
                                  kMetaHeaderBytes + r * kAssnFmEntryBytes,
                                  kAssnFmEntryBytes);
        if (r == rows - 1) {
            if (entry != slice(assn_meta,
                               kMetaHeaderBytes + (rows - 2) * kAssnFmEntryBytes,
                               kAssnFmEntryBytes)) {
                fail(function + ": the assignment FixedMeta spare entry is not "
                     "a byte duplicate of the real row's entry");
            }
        } else if (entry != expected) {
            fail(function + ": assignment FixedMeta entry " + std::to_string(r) +
                 " is not the measured settled entry");
        }
        Bytes expected_f2;
        if (r + 2 < rows) {
            expected_f2 = f2m_entry(
                static_cast<std::uint32_t>((r + 1) * kAssnF2RecordBytes), 0x01,
                0x08);
        } else {
            expected_f2 = f2m_entry(
                static_cast<std::uint32_t>(real_row * kAssnF2RecordBytes),
                0x09, 0x28);
        }
        Bytes const f2_entry = slice(assn_f2_meta,
                                     kMetaHeaderBytes + r * kAssnF2mEntryBytes,
                                     kAssnF2mEntryBytes);
        if (r == rows - 1) {
            if (f2_entry !=
                slice(assn_f2_meta,
                      kMetaHeaderBytes + (rows - 2) * kAssnF2mEntryBytes,
                      kAssnF2mEntryBytes)) {
                fail(function + ": the assignment Fixed2Meta spare entry is not "
                     "a byte duplicate of the real row's entry");
            }
        } else if (f2_entry != expected_f2) {
            fail(function + ": assignment Fixed2Meta entry " +
                 std::to_string(r) + " is not the measured settled entry");
        }
    }
    for (std::size_t r = 0; r + 1 < rows; ++r) {
        Bytes const identity =
            slice(assn_f2, r * kAssnF2RecordBytes + 32, 16);
        if (!std::equal(identity.begin(), identity.end(),
                        null_identity().begin())) {
            fail(function + ": assignment Fixed2Data record " +
                 std::to_string(r) + " does not name the placeholder resource");
        }
    }
    {
        Bytes const identity =
            slice(assn_f2, real_row * kAssnF2RecordBytes + 32, 16);
        if (std::equal(identity.begin(), identity.end(),
                       null_identity().begin())) {
            fail(function + ": the real assignment's Fixed2Data record names "
                 "the placeholder resource");
        }
    }

    double const work = get_f64(
        assn_fixed, real_row * kAssnRowBytes + kAssnRowWork);
    std::uint32_t const ticks8 = contour_ticks8(work, function.c_str());

    check_task_work(task_fixed, 0, work, function.c_str());
    check_task_work(task_fixed, task_row, work, function.c_str());
    if (task_flags(task_meta, 0) != 0x17 ||
        task_flags(task_meta, task_row) != 0x97) {
        fail(function + ": the project or target task flag byte is not the "
             "measured 0x17 / 0x97");
    }
    check_resource_work(rsc_fixed, rsc_row, work, function.c_str());
    if (rsc_flag(rsc_meta, 0) != 0x00) {
        fail(function + ": the project resource row's flag byte is not the "
             "measured 0x00");
    }

    std::vector<VarEntryView> const assn_entries =
        load_var_meta(assn_var_meta, assn_var2, "the assignment VarMeta");
    std::vector<VarGroup> const groups =
        group_var_entries(assn_entries, function.c_str());
    if (groups.size() < 2) {
        fail(function + ": the assignment VarMeta has no placeholder and real "
             "groups");
    }
    for (std::size_t g = 0; g + 1 < groups.size(); ++g) {
        check_group_keys(assn_entries, groups, g, placeholder_keys(),
                         function.c_str());
    }
    {
        std::vector<std::uint32_t> const real_keys = {
            kAssnContourKey, kAssnSessionKey, kAssnBlock299Key,
            kAssnBlock29BKey};
        check_group_keys(assn_entries, groups, groups.size() - 1, real_keys,
                         function.c_str());
    }
    if (groups.back().uid != real_uid) {
        fail(function + ": the last VarMeta group is not the real "
             "assignment's");
    }
    if (assn_var2.size() < kFourBlockBytes) {
        fail(function + ": the assignment Var2Data is shorter than the group");
    }
    /* The old group's base (the copy source) and the append point for the new
     * one; the re-keyed entries point at the appended copy. */
    std::size_t const var2_base = assn_var2.size();
    std::size_t const group_base = var2_base - kFourBlockBytes;
    for (BlockLayout const& block : kFourBlockLayout) {
        bool present = false;
        for (std::size_t index : groups.back().indices) {
            VarEntryView const& entry = assn_entries[index];
            if (entry.key != block.key) {
                continue;
            }
            present = true;
            if (entry.offset != group_base + block.relative) {
                fail(function + ": the real group's 0x" + [&] {
                         char buffer[16];
                         std::snprintf(buffer, sizeof buffer, "%08x",
                                       block.key);
                         return std::string(buffer);
                     }() + " block is not at the measured offset");
            }
            if (block_length(assn_var2, entry.offset) != block.length) {
                fail(function + ": the real group's 0x" + [&] {
                         char buffer[16];
                         std::snprintf(buffer, sizeof buffer, "%08x",
                                       block.key);
                         return std::string(buffer);
                     }() + " block is not the measured length");
            }
        }
        if (!present) {
            fail(function + ": the real group has no measured block");
        }
    }

    std::uint32_t const count_before =
        props_scalar(props, kPropsAssignmentCountTag, function.c_str());
    std::uint32_t const freed_before =
        props_scalar(props, kPropsAssignmentFreedTag, function.c_str());
    std::uint32_t const dirty_before =
        props_scalar(props, kPropsAssignmentDirtyTag, function.c_str());
    std::uint32_t const size_before =
        props_scalar(props, kPropsAssignmentSizeTag, function.c_str());
    if (count_before != fm.count || size_before != assn_var2.size() ||
        dirty_before != 0) {
        fail(function + ": the 114/Props assignment scalars do not agree with "
             "the measured control state");
    }

    /* ---- build ------------------------------------------------------ */
    Built built;
    std::uint32_t const new_uid = max_uid + 1;
    std::size_t const new_rows = rows + 1;
    std::uint32_t copied_payload = 0;
    for (std::size_t index : groups.back().indices) {
        copied_payload += block_length(assn_var2, assn_entries[index].offset);
    }

    built.assn_fixed = assn_fixed;
    {
        Bytes row = slice(assn_fixed, real_row * kAssnRowBytes, kAssnRowBytes);
        put_u32(row, kAssnRowUid, new_uid);
        put_u32(row, kAssnRowResource, kPlaceholderResourceUid);
        built.assn_fixed.insert(built.assn_fixed.end(), row.begin(), row.end());
    }
    built.assn_meta = assn_meta;
    put_u16(built.assn_meta,
            kMetaHeaderBytes + (rows - 2) * kAssnFmEntryBytes, 2);
    put_u16(built.assn_meta,
            kMetaHeaderBytes + (rows - 2) * kAssnFmEntryBytes + 2, 0);
    put_u32(built.assn_meta,
            kMetaHeaderBytes + (rows - 1) * kAssnFmEntryBytes + 4,
            static_cast<std::uint32_t>(rows * kAssnRowBytes));
    put_u8(built.assn_meta,
           kMetaHeaderBytes + (rows - 1) * kAssnFmEntryBytes + 13, 0x01);
    put_u32(built.assn_meta, 4, 6);
    put_u32(built.assn_meta, 8, static_cast<std::uint32_t>(new_rows - 1));
    put_u32(built.assn_meta, 12, static_cast<std::uint32_t>(new_rows *
                                                           kAssnRowBytes));
    built.assn_f2_meta = assn_f2_meta;
    put_u32(built.assn_f2_meta,
            kMetaHeaderBytes + (rows - 1) * kAssnF2mEntryBytes + 4,
            static_cast<std::uint32_t>(rows * kAssnF2RecordBytes));
    put_u8(built.assn_f2_meta,
           kMetaHeaderBytes + (rows - 1) * kAssnF2mEntryBytes + 9, 0x01);
    put_u32(built.assn_f2_meta, 8, static_cast<std::uint32_t>(new_rows - 1));
    put_u32(built.assn_f2_meta, 12, static_cast<std::uint32_t>(
                                       new_rows * kAssnF2RecordBytes));

    built.assn_f2 = assn_f2;
    {
        Bytes const fresh = fresh_identity(doc, assn_f2, function.c_str());
        Bytes const task_identity =
            slice(doc.read_stream(kTaskFixed2Data),
                  (task_row + kF2SpacerRecords) * kTaskF2RecordBytes, 16);
        Bytes placeholder(16, 0);
        std::copy(null_identity().begin(), null_identity().end(),
                  placeholder.begin());
        built.assn_f2.insert(built.assn_f2.end(), fresh.begin(), fresh.end());
        built.assn_f2.insert(built.assn_f2.end(), task_identity.begin(),
                             task_identity.end());
        built.assn_f2.insert(built.assn_f2.end(), placeholder.begin(),
                             placeholder.end());
    }

    built.assn_var_meta.assign(kVarMetaHeaderBytes, 0);
    std::copy(assn_var_meta.begin(),
              assn_var_meta.begin() + static_cast<std::ptrdiff_t>(
                                          kVarMetaHeaderBytes),
              built.assn_var_meta.begin());
    for (VarEntryView const& entry : assn_entries) {
        if (entry.uid == real_uid) {
            continue;
        }
        append_u32(built.assn_var_meta, entry.uid);
        append_u32(built.assn_var_meta, entry.offset);
        append_u32(built.assn_var_meta, entry.key);
    }
    for (BlockLayout const& block : kFourEntryLayout) {
        append_u32(built.assn_var_meta, new_uid);
        append_u32(built.assn_var_meta,
                   static_cast<std::uint32_t>(var2_base + block.relative));
        append_u32(built.assn_var_meta, block.key);
    }
    put_u32(built.assn_var_meta, 8,
            static_cast<std::uint32_t>(assn_entries.size()));
    put_u32(built.assn_var_meta, kVar2SizeOffset,
            static_cast<std::uint32_t>(var2_base + kFourBlockBytes));

    built.assn_var2 = assn_var2;
    {
        /* The appended group is the real group's copy with its contour record
         * zeroed: Project leaves the header's units/work/ticks but writes a
         * zeroed record for the assignment that is no longer live. */
        Bytes copied = slice(assn_var2, group_base, kFourBlockBytes);
        std::fill(copied.begin() +
                      static_cast<std::ptrdiff_t>(kContourRecordOffset),
                  copied.begin() + static_cast<std::ptrdiff_t>(
                                       kContourRecordOffset +
                                       kContourRecordBytes),
                  std::uint8_t{0});
        built.assn_var2.insert(built.assn_var2.end(), copied.begin(),
                               copied.end());
    }

    built.task_fixed = task_fixed;
    put_f64(built.task_fixed, kTaskHeaderBytes + kTaskRowWork, 0.0);
    put_f64(built.task_fixed, kTaskHeaderBytes + kTaskRowWorkRollup, 0.0);
    put_f64(built.task_fixed,
            kTaskHeaderBytes + task_row * kTaskRowBytes + kTaskRowWork, 0.0);
    put_f64(built.task_fixed,
            kTaskHeaderBytes + task_row * kTaskRowBytes + kTaskRowWorkRollup,
            0.0);
    built.task_meta = task_meta;
    built.task_var_meta = task_var_meta;
    put_u8(built.task_meta,
           kMetaHeaderBytes + kTaskFmEntryBytes * (task_row + kF2SpacerRecords) +
               kTaskFmFlags,
           0x87);
    built.task_var2 = task_var2;
    put_u32(built.task_var2, leaf.offset + 4, 1);
    put_u32(built.task_var2, leaf.offset + 8, kPlaceholderResourceUid);

    built.rsc_fixed = rsc_fixed;
    for (std::size_t offset : {kRscRowWork1, kRscRowWork2, kRscRowWork3}) {
        put_f64(built.rsc_fixed,
                kRscHeaderBytes + rsc_row * kRscRowBytes + offset, 0.0);
    }
    built.rsc_meta = rsc_meta;

    built.props.stream = kPropsBackEnd;
    built.props.scalars = {
        {kPropsAssignmentCountTag, static_cast<std::uint32_t>(new_rows - 1)},
        {kPropsAssignmentFreedTag, freed_before + copied_payload},
        {kPropsAssignmentDirtyTag, 1},
        {kPropsAssignmentSizeTag,
         static_cast<std::uint32_t>(var2_base + kFourBlockBytes)},
    };
    std::uint32_t const props_count_after =
        static_cast<std::uint32_t>(new_rows - 1);
    std::uint32_t const props_freed_after = freed_before + copied_payload;
    std::uint32_t const props_size_after =
        static_cast<std::uint32_t>(var2_base + kFourBlockBytes);
    if (props_count_after == count_before ||
        props_freed_after == freed_before || props_size_after == size_before) {
        fail(function + ": a measured scalar would not change");
    }

    built.expected_changed = {
        kAssnFixedData,  kAssnFixedMeta, kAssnFixed2Data, kAssnFixed2Meta,
        kAssnVarMeta,    kAssnVar2Data,  kTaskFixedData,  kTaskFixedMeta,
        kTaskVar2Data,   kRscFixedData,  kPropsBackEnd,   kDocumentSummaryStream,
        kEditMarkerStream};

    CfbDocument edited = doc;
    save_and_prove(doc, edited, built, path, function, dry_run);

    AssignmentGrowthWrite written;
    written.task_uid = key.task_uid;
    written.resource_uid = key.resource_uid;
    written.assignment_uid = new_uid;
    written.tombstoned_uid = real_uid;
    written.rows_before = rows;
    written.rows_after = new_rows;
    written.work = work;
    written.work_ticks = static_cast<std::uint32_t>(ticks8 / 8u);
    written.var2_bytes_before = var2_base;
    written.var2_bytes_after = var2_base + kFourBlockBytes;
    written.var_entries_before = assn_entries.size();
    written.var_entries_after = assn_entries.size();
    written.props_count_before = count_before;
    written.props_count_after = props_count_after;
    written.props_freed_before = freed_before;
    written.props_freed_after = props_freed_after;
    written.props_dirty_before = dirty_before;
    written.props_dirty_after = 1;
    written.props_size_before = size_before;
    written.props_size_after = props_size_after;
    return written;
}

} /* namespace mppwrite */
