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

#include "edit_marker.h"
#include "summary_info.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

/* TBkndAssn, the table Project's outline save also edits (W29-0.1):
 *   indent / summary   the live assignment of the task that becomes a summary
 *                      is TOMBSTONED: FixedMeta entry +0 := 0x02, +2 := 0, the
 *                      table header's +4 word 4 -> 6 when the table had no
 *                      tombstone yet, and every logical VarMeta entry of that
 *                      assignment uid is removed (its Var2Data block stays).
 *   outdent            the tombstoned assignment of the summary that becomes a
 *                      leaf again is RE-CREATED as a new row at the end of the
 *                      table (see apply_outdent and the header's byte list).
 * The shape words (110-byte rows, 34-byte FixedMeta entries, 48-byte
 * Fixed2Data records, 53-byte Fixed2Meta entries) are the ones actuals.cpp
 * measured on the same files. */
constexpr std::size_t kAssnStride = 110;
constexpr std::size_t kAssnUid = 0;
constexpr std::size_t kAssnTask = 4;
constexpr std::size_t kAssnPlannedWork = 20;
/* +52 Start / +56 Finish / +60 Resume copy the assigned task's effective +104 /
 * +108 (reader.h), and +104 repeats the start.  Measured on the re-created
 * outdent assignment in all three pairs: the automatic wave-29 pairs mirror the
 * recomputed effective pair, the manual wave-28 pair keeps the effective pair
 * the control already stored. */
constexpr std::size_t kAssnStart = 52;
constexpr std::size_t kAssnFinish = 56;
constexpr std::size_t kAssnResume = 60;
constexpr std::size_t kAssnStartCopy = 104;
constexpr std::size_t kAssnMetaHeader = 16;
constexpr std::size_t kAssnMetaEntry = 34;
constexpr std::size_t kAssnMetaState = 0;
constexpr std::size_t kAssnMetaFieldCount = 2;
constexpr std::size_t kAssnMetaRowOffset = 4;
constexpr std::size_t kAssnMetaHeaderWord4 = 4; /* 4, or 6 once tombstoned */
constexpr std::uint8_t kAssnMetaStateLive = 0x00;
constexpr std::uint8_t kAssnMetaStateDeleted = 0x02;
constexpr std::size_t kAssnFixed2Record = 48;
constexpr std::size_t kAssnFixed2MetaEntry = 53;
constexpr std::size_t kAssnFixed2MetaRowWord = 4;
/* +8 mirrors the assigned task's scheduling mode: 0x80 set on a manual task
 * (the wave-28 pair and the wave-29 hybrid pair), clear on an automatic one
 * (the wave-29 automatic-parent pair); the other bits come from the tombstone. */
constexpr std::size_t kAssnFixed2MetaManual = 8;
constexpr std::uint8_t kAssnFixed2MetaManualBit = 0x80;
constexpr std::size_t kAssnFixed2MetaFlags = 9;
constexpr std::size_t kAssnFixed2MetaLastByte = 52;
constexpr std::uint8_t kAssnFixed2MetaNewLastByte = 0x28;
constexpr std::uint32_t kAssnContourField = 0x0F400031u;
constexpr std::uint32_t kAssnCreatedField = 0x0F40027Au;
constexpr std::uint32_t kAssnZeroFieldA = 0x0F400299u;
constexpr std::uint32_t kAssnZeroFieldB = 0x0F40029Bu;
constexpr std::size_t kAssnContourSize = 72; /* 44-byte header + 28-byte rec */
constexpr std::uint32_t kAssnContourMagic = 0x00200000u;
constexpr std::size_t kAssnContourSizeWord = 4;
constexpr std::size_t kAssnContourConstant = 8;
constexpr std::size_t kAssnContourWork = 16;
constexpr std::size_t kAssnContourTicks8 = 24;
constexpr std::size_t kAssnContourFlatWord = 32;
constexpr std::size_t kAssnNewRowFieldCount = 4;

/* 0x0F40027A carries the file's current per-session word: 114/Props records
 * 0x02400007 and 0x024013B1 each store the same 4 bytes in every measured file
 * (wave-28 outdent 0x3D16300C, the two wave-29 outdent pairs 0x3D170E1A), and
 * Project's re-created assignment block copies that word.  The writer reads it
 * from record 0x02400007 instead of hard-coding one session's value. */

/* 114/Props scalar counters an outline save moves (16-byte records, the same
 * shape actuals.cpp measured). */
constexpr std::uint32_t kPropTaskFreed = 0x01000002u;
constexpr std::uint32_t kPropTaskVar2Size = 0x01000100u;
constexpr std::uint32_t kPropAssnRows = 0x04000001u;
constexpr std::uint32_t kPropAssnFreed = 0x04000002u;
constexpr std::uint32_t kPropAssnVar2Size = 0x04000100u;
constexpr std::uint32_t kPropAssnDeleted = 0x04008000u;
constexpr std::size_t kPropScalarSize = 16;

/* W29-0.4: the two per-save 4-byte DATE records in 114/Props, framed like the
 * counters but with their id stored LITTLE-endian (the scalar tags above are
 * read big-endian; the measured record ids are the other way round):
 *   0x02400002  the project start -- the value a top-level automatic row's
 *               Fixed2Data +46 carries (3bf11e78 in every outline fixture);
 *   0x02400003  the project finish -- equal to the project row's +100 in every
 *               outline pair, control and Project result, and moved by the
 *               automatic roll-ups of W29-0.4;
 *   0x02400007  the current per-session word the re-created assignment's
 *               0x0F40027A block copies (0x024013B1 stores the same 4 bytes).
 */
constexpr std::uint32_t kPropProjectStart = 0x02400002u;
constexpr std::uint32_t kPropProjectFinish = 0x02400003u;
constexpr std::uint32_t kPropSessionWord = 0x02400007u;

/* Fixed2Meta byte +8 bit 0x80: Project's Manually Scheduled flag, the same bit
 * actuals.cpp's task_is_manually_scheduled reads. */
constexpr std::size_t kFixed2MetaFlags = 8;
constexpr std::uint8_t kFixed2MetaFlagManual = 0x80;

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

void put_f64(std::vector<std::uint8_t>& b, std::size_t o, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof bits == sizeof value, "IEEE-754 double expected");
    std::memcpy(&bits, &value, sizeof bits);
    for (std::size_t i = 0; i < 8; ++i) {
        b[o + i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu);
    }
}

double get_f64(std::vector<std::uint8_t> const& b, std::size_t o) {
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        bits |= static_cast<std::uint64_t>(b[o + i]) << (8 * i);
    }
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof value);
    return value;
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

/* The storage's Props stream: '114/TBkndTask' -> '114/Props'. */
std::string props_path_for(std::string_view table) {
    std::size_t const slash = table.find('/');
    if (slash == std::string_view::npos) {
        throw Error("'" + std::string(table) + "' has no storage prefix");
    }
    return std::string(table.substr(0, slash)) + "/Props";
}

/* Byte offset of the VALUE of one 16-byte scalar Props entry, located by its
 * big-endian tag.  Requiring exactly one occurrence is the measured shape: each
 * outline pair moves one record per counter (W29-0.1, the same rule actuals.cpp
 * measured for its four counters). */
std::size_t props_value_offset(std::vector<std::uint8_t> const& props,
                               std::uint32_t tag) {
    std::size_t found = static_cast<std::size_t>(-1);
    for (std::size_t at = 0; at + kPropScalarSize <= props.size(); ++at) {
        if (get_u32(props, at) != 4 || get_u32(props, at + 8) != 4) {
            continue;
        }
        std::uint32_t const stored =
            (static_cast<std::uint32_t>(props[at + 4]) << 24) |
            (static_cast<std::uint32_t>(props[at + 5]) << 16) |
            (static_cast<std::uint32_t>(props[at + 6]) << 8) |
            static_cast<std::uint32_t>(props[at + 7]);
        if (stored != tag) {
            continue;
        }
        if (found != static_cast<std::size_t>(-1)) {
            throw Error("114/Props holds more than one scalar record for tag " +
                        std::to_string(tag));
        }
        found = at + 12;
    }
    if (found == static_cast<std::size_t>(-1)) {
        throw Error("114/Props has no scalar record for tag 0x" +
                    std::to_string(tag) +
                    ", so the outline save counters cannot be kept");
    }
    return found;
}

/* Byte offset of the 4-byte PAYLOAD of one per-save 114/Props record, or
 * SIZE_MAX when the record is absent.  The records are the [u32 length][u32 id]
 * [u32 kind][payload] chain the test parses from offset 16; this family's id is
 * stored LITTLE-endian (see kPropProjectStart).  A record of the requested id
 * with another payload length, or two copies of it, is a shape no measured file
 * has and throws. */
std::size_t props_record_payload(std::vector<std::uint8_t> const& props,
                                 std::uint32_t id) {
    std::size_t found = static_cast<std::size_t>(-1);
    std::size_t at = 16;
    while (at + 12 <= props.size()) {
        std::uint32_t const length = get_u32(props, at);
        std::uint32_t const stored = get_u32(props, at + 4);
        if ((stored & 0x00FF0000u) != 0x00400000u) {
            break;
        }
        if (static_cast<std::uint64_t>(at) + 12u + length > props.size()) {
            throw Error("a 114/Props record runs past the stream");
        }
        if (stored == id) {
            if (length != 4) {
                throw Error("114/Props record for id 0x" +
                            std::to_string(id) + " is " +
                            std::to_string(length) +
                            " bytes, not the measured 4");
            }
            if (found != static_cast<std::size_t>(-1)) {
                throw Error("114/Props holds more than one record for id 0x" +
                            std::to_string(id));
            }
            found = at + 12;
        }
        at += 12u + length;
    }
    return found;
}

/* The sibling assignment table in the same storage ("114/TBkndAssn"). */
std::string assn_table_for(std::string_view table) {
    std::size_t const slash = table.rfind('/');
    if (slash == std::string_view::npos) {
        throw Error("'" + std::string(table) + "' has no storage prefix");
    }
    return std::string(table.substr(0, slash)) + "/TBkndAssn";
}

/* The six TBkndAssn streams, validated to the geometry every measured file
 * has.  An outline edit can tombstone or re-create an assignment row, so the
 * writer now owns this table too (W29-0.1). */
struct AssnState {
    AssnState(CfbDocument const& doc, std::string_view table);
    AssnState(AssnState const&) = delete;
    AssnState& operator=(AssnState const&) = delete;

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
    std::vector<std::uint8_t> original_var_meta;
    std::vector<std::uint8_t> var_meta;
    std::vector<std::uint8_t> var2;
    std::size_t row_count = 0;
    /* MEASURED: the FixedMeta header's +4 word is 4 in a table with no
     * tombstone and 6 once one exists; Project sets it when it writes the
     * first tombstone and leaves it at 6 afterwards. */
    bool had_tombstone = false;
};

AssnState::AssnState(CfbDocument const& doc, std::string_view table)
    : fixed_path(table_stream(table, "FixedData")),
      meta_path(table_stream(table, "FixedMeta")),
      fixed2_path(table_stream(table, "Fixed2Data")),
      fixed2_meta_path(table_stream(table, "Fixed2Meta")),
      var_meta_path(table_stream(table, "VarMeta")),
      var2_path(table_stream(table, "Var2Data")) {
    for (std::string const* path :
         {&fixed_path, &meta_path, &fixed2_path, &fixed2_meta_path,
          &var_meta_path, &var2_path}) {
        if (!doc.has_stream(*path)) {
            throw Error("the assignment table is missing " + *path +
                        "; an outline save must tombstone or re-create the "
                        "assignment row Project's save changes");
        }
    }
    fixed = doc.read_stream(fixed_path);
    meta = doc.read_stream(meta_path);
    fixed2 = doc.read_stream(fixed2_path);
    fixed2_meta = doc.read_stream(fixed2_meta_path);
    original_var_meta = doc.read_stream(var_meta_path);
    var_meta = original_var_meta;
    var2 = doc.read_stream(var2_path);

    if (fixed.size() % kAssnStride != 0) {
        throw Error("TBkndAssn/FixedData is " + std::to_string(fixed.size()) +
                    " bytes, not 110*n");
    }
    row_count = fixed.size() / kAssnStride;
    if (meta.size() < kAssnMetaHeader ||
        (meta.size() - kAssnMetaHeader) % kAssnMetaEntry != 0 ||
        (meta.size() - kAssnMetaHeader) / kAssnMetaEntry != row_count) {
        throw Error("TBkndAssn/FixedMeta is " + std::to_string(meta.size()) +
                    " bytes, not 16 + 34*" + std::to_string(row_count));
    }
    if (fixed2.size() != kAssnFixed2Record * row_count) {
        throw Error("TBkndAssn/Fixed2Data is " +
                    std::to_string(fixed2.size()) + " bytes, not 48*" +
                    std::to_string(row_count));
    }
    if (fixed2_meta.size() < kFixed2MetaHeaderSize ||
        (fixed2_meta.size() - kFixed2MetaHeaderSize) %
                kAssnFixed2MetaEntry !=
            0 ||
        (fixed2_meta.size() - kFixed2MetaHeaderSize) /
                kAssnFixed2MetaEntry !=
            row_count) {
        throw Error("TBkndAssn/Fixed2Meta is " +
                    std::to_string(fixed2_meta.size()) + " bytes, not 16 + 53*" +
                    std::to_string(row_count));
    }
    if (var_meta.size() < kVarMetaHeaderSize ||
        (var_meta.size() - kVarMetaHeaderSize) % kVarMetaEntrySize != 0) {
        throw Error("TBkndAssn/VarMeta is " + std::to_string(var_meta.size()) +
                    " bytes, not 24 + 12*n");
    }
    std::size_t const physical_entries =
        (var_meta.size() - kVarMetaHeaderSize) / kVarMetaEntrySize;
    std::size_t const declared = get_u32(var_meta, 8);
    if (declared > physical_entries) {
        throw Error("TBkndAssn/VarMeta declares " + std::to_string(declared) +
                    " entries but holds only " +
                    std::to_string(physical_entries));
    }
    (void)read_var_entries(var_meta);
    var_meta.resize(kVarMetaHeaderSize + declared * kVarMetaEntrySize);
    for (std::size_t i = 0; i < row_count; ++i) {
        std::size_t const base = kAssnMetaHeader + i * kAssnMetaEntry;
        if (meta[base + kAssnMetaState] == kAssnMetaStateDeleted) {
            had_tombstone = true;
        }
    }
}

struct State {
    State(CfbDocument const& doc, std::string_view table);
    State(State const&) = delete;
    State& operator=(State const&) = delete;

    TaskTable rows;
    AssnState assn;
    /* Payload bytes this save stops referencing, per table: the bytes Project
     * adds to 114/Props 0x01000002 / 0x04000002 (see set_var_block). */
    std::size_t task_freed_bytes = 0;
    std::size_t assn_freed_bytes = 0;
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
      assn(doc, assn_table_for(table)),
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

/* ---- TBkndAssn helpers ------------------------------------------------ */

std::size_t assn_meta_entry_for_row(AssnState const& assn, std::size_t row) {
    std::uint32_t const offset = static_cast<std::uint32_t>(row * kAssnStride);
    for (std::size_t i = 0;
         i < (assn.meta.size() - kAssnMetaHeader) / kAssnMetaEntry; ++i) {
        std::size_t const base = kAssnMetaHeader + i * kAssnMetaEntry;
        if (get_u32(assn.meta, base + kAssnMetaRowOffset) == offset) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

bool assn_row_deleted(AssnState const& assn, std::size_t row) {
    std::size_t const index = assn_meta_entry_for_row(assn, row);
    if (index == static_cast<std::size_t>(-1)) {
        throw Error("TBkndAssn/FixedMeta has no entry for row " +
                    std::to_string(row));
    }
    return assn.meta[kAssnMetaHeader + index * kAssnMetaEntry +
                     kAssnMetaState] == kAssnMetaStateDeleted;
}

/* Every FixedData row whose +4 task word is `task_uid`, live or tombstoned. */
std::vector<std::size_t> assn_rows_for_task(AssnState const& assn,
                                            std::uint32_t task_uid) {
    std::vector<std::size_t> rows;
    for (std::size_t row = 0; row < assn.row_count; ++row) {
        if (get_u32(assn.fixed, row * kAssnStride + kAssnTask) == task_uid) {
            rows.push_back(row);
        }
    }
    return rows;
}

/* Remove every logical VarMeta entry of one assignment uid and add the payload
 * bytes of the blocks it stops referencing to `freed`.  The Var2Data bytes stay,
 * as Project leaves them (measured: the three indent-pair entries 0x0F400031
 * 100 B, 0x0F40027A 4 B, 0x0F400299 16 B are still there after the tombstone). */
std::size_t remove_assn_var_entries(AssnState& assn, std::uint32_t uid,
                                   std::size_t& freed) {
    std::vector<VarEntry> const entries = read_var_entries(assn.var_meta);
    std::size_t removed = 0;
    for (std::size_t i = entries.size(); i-- > 0;) {
        VarEntry const& entry = entries[i];
        if (entry.mid != uid) {
            continue;
        }
        if (static_cast<std::size_t>(entry.offset) + 4 > assn.var2.size()) {
            throw Error("TBkndAssn/VarMeta entry for uid " +
                        std::to_string(uid) + " points past Var2Data");
        }
        freed += get_u32(assn.var2, entry.offset);
        std::size_t const at =
            kVarMetaHeaderSize + i * kVarMetaEntrySize;
        assn.var_meta.erase(
            assn.var_meta.begin() + static_cast<std::ptrdiff_t>(at),
            assn.var_meta.begin() +
                static_cast<std::ptrdiff_t>(at + kVarMetaEntrySize));
        ++removed;
    }
    return removed;
}

/* MEASURED 0x04008000: 1 iff some tombstoned assignment row's task has no live
 * row left.  Indent 0 -> 1, the summary pair stays 1, outdent 1 -> 0. */
bool assn_has_unresolved_tombstone(AssnState const& assn) {
    for (std::size_t row = 0; row < assn.row_count; ++row) {
        if (!assn_row_deleted(assn, row)) {
            continue;
        }
        std::uint32_t const task =
            get_u32(assn.fixed, row * kAssnStride + kAssnTask);
        bool live = false;
        for (std::size_t other = 0; other < assn.row_count; ++other) {
            if (assn_row_deleted(assn, other)) {
                continue;
            }
            if (get_u32(assn.fixed, other * kAssnStride + kAssnTask) == task) {
                live = true;
                break;
            }
        }
        if (!live) {
            return true;
        }
    }
    return false;
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

/* Project's Manually Scheduled bit for one FixedData row (W29-0.4).  The
 * FixedMeta entry index names the same Fixed2Meta entry; byte +8 bit 0x80 is
 * the bit core/mpp reads and actuals.cpp's task_is_manually_scheduled tests. */
bool row_is_manual(State const& state, std::size_t row) {
    std::size_t const index = fixed2_meta_entry_for_row(state, row);
    return (state.fixed2_meta[kFixed2MetaHeaderSize +
                              index * kFixed2MetaEntrySize +
                              kFixed2MetaFlags] &
            kFixed2MetaFlagManual) != 0;
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
 * +8..23 is written by set_fixed_meta_blob below; the save-pair test compares
 * the whole entry, blob included, with Project's file. */
void set_fixed_meta_summary(State& state, std::size_t row) {
    std::size_t const index = meta_entry_for_row(state.meta, row);
    if (index == static_cast<std::size_t>(-1)) {
        throw Error("FixedMeta has no entry for FixedData row " +
                    std::to_string(row));
    }
    state.meta[kFixedMetaHeaderSize + index * kFixedMetaEntrySize +
               kFixedMetaSummaryByte] |= kFixedMetaFlagSummary;
}

/* The FixedMeta entry's 16-byte cache blob at +8..23.  MEASURED (W29-0.1) on
 * both indent pairs and reversed by the outdent pair, five entry bytes move
 * (+9/+12/+16/+17/+18 are entry-relative, i.e. blob +1/+4/+8/+9/+10):
 *   indent   +9 |= 0x40   +12 |= 0x0C   +16 |= 0x10
 *            +17 := (+17 & ~0x04) | 0x01   +18 |= 0x20
 *   outdent  +9 unchanged (0x40 is sticky, like the +26 summary flag)
 *            +12 &= ~0x0C  +16 &= ~0x10
 *            +17 := (+17 & ~0x01) | 0x04   +18 &= ~0x20
 * The two indent pairs move the same five bytes by the same amounts
 * (0xbf->0xff, 0x71->0x7d, 0x07->0x17, 0x4c->0x49, 0xcf->0xef); the outdent
 * pair's control carries the indent result's bytes and its result is the four
 * arithmetic reverses.  Before W29-0.1 the writer left the control's blob and
 * the save-pair test masked it -- rule 1 does not accept that.
 *
 * W29-0.4 measured the AUTOMATIC variants on the four new pairs.  The cache
 * byte-1 0x40 bit and the +17/+18 field-state bits behave differently when the
 * row is automatically scheduled:
 *   automatic indent  +9 |= 0x40, +12 |= 0x0C, +16 |= 0x10; +17/+18 are
 *                     left exactly as stored (0x48/0xef in both pairs, where
 *                     the manual rule would have turned 0x48 into 0x49).
 *   automatic outdent +12 &= ~0x0C, +16 &= ~0x10; +9, +17 and +18 are left
 *                     as stored (0x48/0xef survive).
 * A restored MANUAL parent keeps the manual outdent rule (the hybrid pair's
 * 0x49->0x4c and 0xef->0xcf prove it).  The moved automatic row's +9 0x40 bit
 * is written by set_moved_fixed_meta_blob below. */
void set_fixed_meta_blob(State& state, std::size_t row, bool indent,
                         bool automatic) {
    std::size_t const index = meta_entry_for_row(state.meta, row);
    if (index == static_cast<std::size_t>(-1)) {
        throw Error("FixedMeta has no entry for FixedData row " +
                    std::to_string(row));
    }
    std::size_t const blob =
        kFixedMetaHeaderSize + index * kFixedMetaEntrySize + 8;
    if (indent) {
        state.meta[blob + 1] |= 0x40;
        state.meta[blob + 4] |= 0x0C;
        state.meta[blob + 8] |= 0x10;
        if (!automatic) {
            state.meta[blob + 9] = static_cast<std::uint8_t>(
                (state.meta[blob + 9] & ~0x04u) | 0x01u);
            state.meta[blob + 10] |= 0x20;
        }
    } else {
        state.meta[blob + 4] &= static_cast<std::uint8_t>(~0x0Cu);
        state.meta[blob + 8] &= static_cast<std::uint8_t>(~0x10u);
        if (!automatic) {
            state.meta[blob + 9] = static_cast<std::uint8_t>(
                (state.meta[blob + 9] & ~0x01u) | 0x04u);
            state.meta[blob + 10] &= static_cast<std::uint8_t>(~0x20u);
        }
    }
}

/* The moved rows' FixedMeta +9 0x40 bit, measured on the automatic pairs:
 *   indent:   set on the moved root AND every row of the moved subtree
 *             (auto-indent 0xbf->0xff; auto-summary sets the moved summary and
 *             its child, both 0xbf->0xff)
 *   outdent:  clear on the moved leaf (auto-outdent 0xff->0xbf; the hybrid
 *             pair's moved row is already clear and stays clear)
 * The W29-0.1 manual shape leaves the moved rows' blobs alone. */
void set_moved_fixed_meta_blob(State& state, std::size_t row, bool indent) {
    std::size_t const index = meta_entry_for_row(state.meta, row);
    if (index == static_cast<std::size_t>(-1)) {
        throw Error("FixedMeta has no entry for FixedData row " +
                    std::to_string(row));
    }
    std::size_t const bit =
        kFixedMetaHeaderSize + index * kFixedMetaEntrySize + 8 + 1;
    if (indent) {
        state.meta[bit] |= 0x40;
    } else {
        state.meta[bit] &= static_cast<std::uint8_t>(~0x40u);
    }
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
 * Var2Data as unreferenced bytes, exactly as Project leaves it.  The payload
 * bytes go to the state's freed counter (W29-0.1: the indent pair's
 * 0x01000002 moves by 8 + 2 + 2 = 12 for the removed leaf fields). */
std::size_t remove_var_entries(State& state, std::uint32_t uid,
                               std::uint32_t field_id) {
    std::vector<VarEntry> const entries = read_var_entries(state.var_meta);
    std::size_t removed = 0;
    for (std::size_t i = entries.size(); i-- > 0;) {
        VarEntry const& entry = entries[i];
        if (entry.mid == uid && entry.field_id == field_id) {
            if (static_cast<std::size_t>(entry.offset) + 4 >
                state.var2.size()) {
                throw Error("TBkndTask/VarMeta entry for field 0x" +
                            std::to_string(field_id) + " points past Var2Data");
            }
            state.task_freed_bytes += get_u32(state.var2, entry.offset);
            std::size_t const at = kVarMetaHeaderSize + i * kVarMetaEntrySize;
            state.var_meta.erase(
                state.var_meta.begin() + static_cast<std::ptrdiff_t>(at),
                state.var_meta.begin() +
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
        /* The tail becomes unreferenced bytes.  MEASURED (W29-0.1): the indent
         * pair's project child list shrinks 16 -> 12 in place and 0x01000002
         * gains 4 on top of the 12 removed leaf bytes. */
        state.task_freed_bytes += old.size() - payload.size();
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
        info.manual = row_is_manual(state, row);
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

/* W29-0.4: the rows Project recalculates +186/+190 for on an automatic outline
 * save -- every LIVE automatic row, the project row included.  The hybrid
 * outdent also recalculates its restored MANUAL parent, which the caller adds
 * with add_slack_uid. */
std::vector<std::uint32_t> automatic_slack_uids(State const& state,
                                                std::vector<OutlineRow> const& rows) {
    std::vector<std::uint32_t> uids;
    for (std::size_t row = 0; row < rows.size(); ++row) {
        if (rows[row].deleted) {
            continue;
        }
        if (!row_is_manual(state, row)) {
            uids.push_back(rows[row].uid);
        }
    }
    std::sort(uids.begin(), uids.end());
    return uids;
}

void add_slack_uid(std::vector<std::uint32_t>& uids, std::uint32_t uid) {
    if (std::find(uids.begin(), uids.end(), uid) == uids.end()) {
        uids.push_back(uid);
        std::sort(uids.begin(), uids.end());
    }
}

/* The caller's slack vector must name exactly `expected`, once each: the
 * writer cannot tell which row a stray uid refers to, and Project's save
 * rewrote the pair-measured set and nothing else.  The late dates are packed
 * dates the caller computed with its calendar; the writer refuses a vector
 * that cannot be stored as two dates before the edit's first mutation. */
bool check_slack_set(std::vector<OutlineSlack> const& slack,
                     std::vector<std::uint32_t> const& expected,
                     std::string& warning) {
    std::vector<std::uint32_t> got;
    got.reserve(slack.size());
    for (OutlineSlack const& entry : slack) {
        got.push_back(entry.uid);
        if (!is_date(entry.late_start) || !is_date(entry.late_finish) ||
            entry.late_finish < entry.late_start) {
            warning = "the queued slack for task uid " +
                      std::to_string(entry.uid) +
                      " does not carry a valid LateStart/LateFinish pair, so"
                      " the automatic outline save cannot write +112/+116";
            return false;
        }
    }
    std::sort(got.begin(), got.end());
    if (got != expected) {
        warning = "the automatic outline roll-up needs the +74/+112/+116/"
                  "+186/+190 words of exactly the live automatic rows (plus"
                  " the restored manual parent of a hybrid outdent), and the"
                  " queued slack list does not match them";
        return false;
    }
    return true;
}

void apply_slack(State& state, std::vector<OutlineRow> const& rows,
                 std::vector<OutlineSlack> const& slack) {
    for (OutlineSlack const& entry : slack) {
        std::size_t at = static_cast<std::size_t>(-1);
        for (std::size_t row = 0; row < rows.size(); ++row) {
            if (rows[row].uid == entry.uid && !rows[row].deleted) {
                at = row;
                break;
            }
        }
        if (at == static_cast<std::size_t>(-1)) {
            throw Error("the queued slack names task uid " +
                        std::to_string(entry.uid) +
                        ", which has no live FixedData row");
        }
        state.rows.set_u32(at, task_row::kFreeSlack, entry.free_slack);
        state.rows.set_u32(at, task_row::kStart3, entry.late_start);
        state.rows.set_u32(at, task_row::kFinish3, entry.late_finish);
        state.rows.set_u32(at, task_row::kStartSlack, entry.start_slack);
        state.rows.set_u32(at, task_row::kTotalSlack, entry.total_slack);
    }
}

std::string task_label(std::uint32_t uid) {
    return "task uid " + std::to_string(uid);
}

/* A deterministic, non-zero Fixed2Data identity no Task or assignment record
 * uses.  Project writes a session GUID whose meaning the reader never uses, so
 * the writer keeps the template's last 8 bytes (the file's stamp shape) and
 * hashes the first 8, exactly as add_delete.cpp's make_row_identity does for a
 * new task row. */
RowId fresh_assn_identity(State const& state, RowId const& template_id,
                          std::uint32_t uid, std::size_t row) {
    std::vector<RowId> used;
    for (std::size_t r = 0; r < state.row_count; ++r) {
        RowId const id = row_identity(state, r);
        if (!empty_id(id)) {
            used.push_back(id);
        }
    }
    for (std::size_t r = 0; r < state.assn.row_count; ++r) {
        RowId id{};
        std::copy(state.assn.fixed2.begin() +
                      static_cast<std::ptrdiff_t>(r * kAssnFixed2Record),
                  state.assn.fixed2.begin() +
                      static_cast<std::ptrdiff_t>(r * kAssnFixed2Record + 16),
                  id.begin());
        if (!empty_id(id)) {
            used.push_back(id);
        }
    }
    for (std::uint32_t attempt = 0; attempt < 1024; ++attempt) {
        std::uint64_t hash = 1469598103934665603ULL; /* FNV-1a offset basis */
        auto mix = [&hash](std::uint32_t value) {
            for (std::size_t i = 0; i < 4; ++i) {
                hash ^= (value >> (8 * i)) & 0xFFu;
                hash *= 1099511628211ULL;
            }
        };
        mix(0x5732394Fu); /* "W29O" tag: ours and reproducible */
        mix(uid);
        mix(static_cast<std::uint32_t>(row));
        mix(attempt);
        RowId id = template_id;
        for (std::size_t i = 0; i < 8; ++i) {
            id[i] = static_cast<std::uint8_t>((hash >> (8 * i)) & 0xFFu);
        }
        bool const blank = std::all_of(id.begin(), id.end(),
                                       [](std::uint8_t byte) {
                                           return byte == 0;
                                       });
        if (!blank && std::find(used.begin(), used.end(), id) == used.end()) {
            return id;
        }
    }
    throw Error("could not derive an unused TBkndAssn Fixed2Data identity");
}

/* Flat planned-work contour, the measured 72-byte shape (0x00200000, 44,
 * 160000.0, work ticks * 100, work ticks * 8, 0, 1, 0, 0, then a 28-byte zero
 * record).  The outdent pair's re-created assignment carries exactly this block
 * for work_ticks 24000 (the restored leaf's duration). */
std::vector<std::uint8_t> assn_contour(std::uint32_t work_ticks) {
    std::vector<std::uint8_t> out(kAssnContourSize, 0);
    put_u32(out, 0, kAssnContourMagic);
    put_u32(out, kAssnContourSizeWord, 44);
    put_f64(out, kAssnContourConstant, 160000.0);
    put_f64(out, kAssnContourWork, static_cast<double>(work_ticks) * 100.0);
    put_u32(out, kAssnContourTicks8, work_ticks * 8u);
    put_u32(out, kAssnContourFlatWord, 1);
    return out;
}

/* Tombstone one live assignment row (indent / summary).  MEASURED on both
 * pairs: the row keeps its FixedData and Fixed2Data bytes, its FixedMeta entry
 * goes state 0x02 / count 0, every logical VarMeta entry of the uid goes, and
 * the table header's +4 word is 6 once a tombstone exists (4 -> 6 on the first
 * one, unchanged at 6 after). */
void tombstone_assignment(State& state, std::size_t row) {
    AssnState& assn = state.assn;
    std::size_t const index = assn_meta_entry_for_row(assn, row);
    if (index == static_cast<std::size_t>(-1)) {
        throw Error("TBkndAssn/FixedMeta has no entry for row " +
                    std::to_string(row));
    }
    std::size_t const base = kAssnMetaHeader + index * kAssnMetaEntry;
    if (assn.meta[base + kAssnMetaState] != kAssnMetaStateLive) {
        throw Error("TBkndAssn row " + std::to_string(row) +
                    " is not live, so it cannot be tombstoned");
    }
    if (!assn.had_tombstone) {
        put_u32(assn.meta, kAssnMetaHeaderWord4, 6);
        assn.had_tombstone = true;
    }
    assn.meta[base + kAssnMetaState] = kAssnMetaStateDeleted;
    put_u16(assn.meta, base + kAssnMetaFieldCount, 0);
    std::uint32_t const uid = get_u32(assn.fixed, row * kAssnStride + kAssnUid);
    remove_assn_var_entries(assn, uid, state.assn_freed_bytes);
}

/* Re-create the tombstoned assignment of the summary that becomes a leaf again
 * (outdent).  Every byte below is measured on the three outline-outdent pairs
 * (wave-28 manual, wave-29 automatic parent and wave-29 hybrid manual parent);
 * the new row is a copy of the tombstone's FixedData with a fresh uid, its
 * +52/+56/+60/+104 date words mirror the restored task's effective +104/+108,
 * its Fixed2Meta +8 manual bit follows that task's scheduling mode, and its
 * four Var2 blocks are appended in Project's own order (0F40027A, 0F400031,
 * 0F400299, 0F40029B) while the VarMeta entries are inserted in ascending key
 * order.  The 0F40027A payload is the file's current per-session word from
 * 114/Props record 0x02400007. */
void recreate_assignment(State& state, std::size_t tombstone_row,
                         std::size_t task_row, std::uint32_t work_ticks,
                         std::uint32_t effective_start,
                         std::uint32_t effective_finish, bool manual,
                         std::array<std::uint8_t, 4> const& session_word) {
    AssnState& assn = state.assn;
    std::uint32_t new_uid = 0;
    for (std::size_t row = 0; row < assn.row_count; ++row) {
        new_uid = std::max(
            new_uid, get_u32(assn.fixed, row * kAssnStride + kAssnUid));
    }
    if (new_uid == 0xFFFFFFFFu) {
        throw Error("TBkndAssn's largest assignment uid is 0xffffffff, so a "
                    "re-created assignment would overflow");
    }
    ++new_uid;
    for (VarEntry const& entry : read_var_entries(assn.var_meta)) {
        if (entry.mid > new_uid) {
            throw Error("TBkndAssn/VarMeta already holds uid " +
                        std::to_string(entry.mid) +
                        ", above the new assignment uid " +
                        std::to_string(new_uid) +
                        "; the entry group would be out of order");
        }
    }
    std::size_t const new_row = assn.row_count;
    std::size_t const tombstone_meta = assn_meta_entry_for_row(assn, tombstone_row);
    if (tombstone_meta == static_cast<std::size_t>(-1)) {
        throw Error("TBkndAssn/FixedMeta has no entry for row " +
                    std::to_string(tombstone_row));
    }

    std::size_t const row_at = tombstone_row * kAssnStride;
    std::vector<std::uint8_t> row(
        assn.fixed.begin() + static_cast<std::ptrdiff_t>(row_at),
        assn.fixed.begin() + static_cast<std::ptrdiff_t>(row_at + kAssnStride));
    put_u32(row, kAssnUid, new_uid);
    put_u32(row, kAssnStart, effective_start);
    put_u32(row, kAssnFinish, effective_finish);
    put_u32(row, kAssnResume, effective_start);
    put_u32(row, kAssnStartCopy, effective_start);
    assn.fixed.insert(assn.fixed.end(), row.begin(), row.end());

    std::size_t const meta_at =
        kAssnMetaHeader + tombstone_meta * kAssnMetaEntry;
    std::vector<std::uint8_t> meta_entry(
        assn.meta.begin() + static_cast<std::ptrdiff_t>(meta_at),
        assn.meta.begin() + static_cast<std::ptrdiff_t>(meta_at + kAssnMetaEntry));
    meta_entry[kAssnMetaState] = kAssnMetaStateLive;
    put_u16(meta_entry, kAssnMetaFieldCount,
            static_cast<std::uint16_t>(kAssnNewRowFieldCount));
    put_u32(meta_entry, kAssnMetaRowOffset,
            static_cast<std::uint32_t>(new_row * kAssnStride));
    assn.meta.insert(assn.meta.end(), meta_entry.begin(), meta_entry.end());

    RowId const parent_identity = row_identity(state, task_row);
    if (empty_id(parent_identity)) {
        throw Error("the summary task row has an all-zero Fixed2Data identity, "
                    "so the re-created assignment cannot name it");
    }
    RowId template_id{};
    std::size_t const f2_at = tombstone_row * kAssnFixed2Record;
    std::copy(assn.fixed2.begin() + static_cast<std::ptrdiff_t>(f2_at),
              assn.fixed2.begin() +
                  static_cast<std::ptrdiff_t>(f2_at + 16),
              template_id.begin());
    if (empty_id(template_id)) {
        template_id = parent_identity;
    }
    RowId const own_identity =
        fresh_assn_identity(state, template_id, new_uid, new_row);
    std::vector<std::uint8_t> record(kAssnFixed2Record, 0);
    std::copy(own_identity.begin(), own_identity.end(), record.begin());
    std::copy(parent_identity.begin(), parent_identity.end(),
              record.begin() + 16);
    std::copy(assn.fixed2.begin() + static_cast<std::ptrdiff_t>(f2_at + 32),
              assn.fixed2.begin() +
                  static_cast<std::ptrdiff_t>(f2_at + kAssnFixed2Record),
              record.begin() + 32);
    assn.fixed2.insert(assn.fixed2.end(), record.begin(), record.end());

    std::size_t const f2m_at =
        kFixed2MetaHeaderSize + tombstone_meta * kAssnFixed2MetaEntry;
    std::vector<std::uint8_t> f2m_entry(
        assn.fixed2_meta.begin() + static_cast<std::ptrdiff_t>(f2m_at),
        assn.fixed2_meta.begin() +
            static_cast<std::ptrdiff_t>(f2m_at + kAssnFixed2MetaEntry));
    put_u32(f2m_entry, kAssnFixed2MetaRowWord,
            static_cast<std::uint32_t>(new_row * kAssnFixed2Record));
    f2m_entry[kAssnFixed2MetaManual] =
        manual ? static_cast<std::uint8_t>(
                     f2m_entry[kAssnFixed2MetaManual] |
                     kAssnFixed2MetaManualBit)
               : static_cast<std::uint8_t>(
                     f2m_entry[kAssnFixed2MetaManual] &
                     static_cast<std::uint8_t>(~kAssnFixed2MetaManualBit));
    f2m_entry[kAssnFixed2MetaFlags] = 0x00;
    f2m_entry[kAssnFixed2MetaLastByte] = kAssnFixed2MetaNewLastByte;
    assn.fixed2_meta.insert(assn.fixed2_meta.end(), f2m_entry.begin(),
                            f2m_entry.end());

    /* Project appends the blocks in this order (offsets 376, 384, 460, 480 in
     * the measured pair) but inserts the entries by ascending key. */
    std::vector<std::uint8_t> created(session_word.begin(), session_word.end());
    std::array<std::uint32_t, 4> const keys = {
        kAssnCreatedField, kAssnContourField, kAssnZeroFieldA, kAssnZeroFieldB};
    std::array<std::vector<std::uint8_t>, 4> const payloads = {
        created, assn_contour(work_ticks), std::vector<std::uint8_t>(16, 0),
        std::vector<std::uint8_t>(16, 0)};
    std::map<std::uint32_t, std::uint32_t> offset_for_key;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        offset_for_key[keys[i]] =
            static_cast<std::uint32_t>(assn.var2.size());
        std::vector<std::uint8_t> const block = encode_block(payloads[i]);
        assn.var2.insert(assn.var2.end(), block.begin(), block.end());
    }
    for (std::uint32_t const key :
         {kAssnContourField, kAssnCreatedField, kAssnZeroFieldA,
          kAssnZeroFieldB}) {
        insert_var_entry(assn.var_meta, new_uid, key, offset_for_key[key]);
    }
    assn.row_count = new_row + 1;
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

    /* TBkndAssn (W29-0.1): the new summary's assignment is TOMBSTONED, which
     * the pairs measured only for a task with exactly one live assignment.
     * Checked here, before the first mutation, so a refusal changes nothing. */
    std::vector<std::size_t> const assn_rows =
        assn_rows_for_task(state.assn, summary.uid);
    if (assn_rows.size() != 1) {
        warning = task_label(edit.uid) + ": task uid " +
                  std::to_string(summary.uid) + " has " +
                  std::to_string(assn_rows.size()) +
                  " assignment rows; only the one-assignment shape is measured"
                  " for an indent";
        return false;
    }
    if (assn_row_deleted(state.assn, assn_rows.front())) {
        warning = task_label(edit.uid) + ": task uid " +
                  std::to_string(summary.uid) +
                  " already carries a tombstoned assignment; that shape is not"
                  " measured for an indent";
        return false;
    }

    /* W29-0.4: the scheduling mode of the moved row and the new summary picks
     * the rule set.  Both manual = the wave-28/29-0.1 shape; both automatic =
     * the W29-0.4 shape; a MIX is not a pair Project ever saved and is
     * refused.  The slack vector is validated here, before the first write. */
    bool const moved_manual = row_is_manual(state, row);
    bool const summary_manual = row_is_manual(state, parent);
    if (moved_manual != summary_manual) {
        warning = task_label(edit.uid) +
                  " and its new summary (task uid " +
                  std::to_string(summary.uid) +
                  ") are scheduled differently (one automatic, one manual);"
                  " no Project save pair measures a mixed-scheduling indent";
        return false;
    }
    bool const automatic = !moved_manual;
    if (automatic) {
        if (row_is_manual(state, 0)) {
            warning = task_label(edit.uid) +
                      ": the project row is manually scheduled; the measured"
                      " automatic roll-up needs the automatic project row";
            return false;
        }
        if (summary.level != 1) {
            warning = task_label(edit.uid) +
                      ": the new summary is not a top-level row; the measured"
                      " automatic roll-up needs a level-1 summary";
            return false;
        }
        /* The project's own duration is the span from +96, and Project's save
         * leaves +96 alone; the measured pairs have +96 == +104 == earliest. */
        std::uint32_t const project_start =
            state.rows.u32(0, task_row::kStart);
        if (!is_date(project_start) || project_start != earliest ||
            rows[0].start != earliest) {
            warning = task_label(edit.uid) +
                      ": the project row's start is not the moved subtree's"
                      " earliest date; that automatic roll-up shape is not"
                      " measured";
            return false;
        }
        for (std::size_t child : rows[0].child_rows) {
            if (child == row || child == parent || rows[child].deleted) {
                continue;
            }
            if (!is_date(rows[child].finish) || rows[child].finish > latest) {
                warning = task_label(edit.uid) +
                          ": another top-level row finishes after the new"
                          " summary's last date; that automatic roll-up shape"
                          " is not measured";
                return false;
            }
        }
        std::vector<std::uint32_t> const expected =
            automatic_slack_uids(state, rows);
        if (!check_slack_set(edit.slack, expected, warning)) {
            return false;
        }
    } else if (!edit.slack.empty()) {
        warning = task_label(edit.uid) +
                  ": the manual indent takes no +186/+190 slack, but the queued"
                  " edit carries some";
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

    if (automatic) {
        /* W29-0.4 measured: the new summary's +88 and +108 roll up, and the
         * project row takes the same duration/finish.  The guards above make
         * the project's span identical to the summary's, so Project's +84 is
         * this same summary_duration. */
        state.rows.set_u32(parent, task_row::kRemainingDuration,
                           edit.summary_duration);
        state.rows.set_u32(parent, task_row::kFinish2, latest);
        state.rows.set_u32(0, task_row::kDuration, edit.summary_duration);
        state.rows.set_u32(0, task_row::kRemainingDuration,
                           edit.summary_duration);
        state.rows.set_u32(0, task_row::kFinish, latest);
        state.rows.set_u32(0, task_row::kFinish2, latest);
    }

    set_row_flags(state, parent, kFixed2FlagSummary);
    set_row_start_copy(state, parent, kInvalidDate);
    set_fixed2_meta_byte(state, parent, kFixed2MetaLeafTail, 0x00);
    set_fixed2_meta_byte(state, parent, kFixed2MetaKind, 0x08);
    set_fixed_meta_summary(state, parent);
    set_fixed_meta_blob(state, parent, true, automatic);

    set_row_parent(state, row, row_identity(state, parent));
    if (!automatic || !rows[row].child_rows.empty()) {
        /* A moved automatic LEAF keeps its stored Fixed2Data +42 (0x30 in both
         * automatic pairs); every other moved row takes the manual flags
         * (0xA0 child leaf / 0x40 moved summary). */
        set_row_flags(state, row, rows[row].child_rows.empty()
                                      ? kFixed2FlagChildLeaf
                                      : kFixed2FlagSummary);
    }
    if (automatic) {
        for (std::size_t member : subtree) {
            set_moved_fixed_meta_blob(state, member, true);
        }
        apply_slack(state, rows, edit.slack);
    }

    std::size_t added = 0;
    std::size_t removed = 0;
    removed += remove_var_entries(state, summary.uid, kLeafField);
    removed += remove_var_entries(state, summary.uid, kLeafExtraA);
    removed += remove_var_entries(state, summary.uid, kLeafExtraB);
    set_var_block(state, summary.uid, kMarkerField,
                  encode_marker(original_duration, original_format), added);
    set_var_block(state, summary.uid, kChildListField,
                  encode_child_list({rows[row].row_id}), added);

    std::size_t project_added = 0;
    set_var_block(state, kProjectUid, kChildListField,
                  encode_child_list(project_ids), project_added);
    added += project_added;
    tombstone_assignment(state, assn_rows.front());

    adjust_field_count(state, parent,
                       static_cast<std::int64_t>(added) -
                           static_cast<std::int64_t>(removed));
    return true;
}

bool apply_outdent(State& state, OutlineEdit const& edit,
                   std::vector<OutlineRow> const& rows, std::size_t row,
                   bool have_project_start, std::uint32_t project_start,
                   bool have_session_word,
                   std::array<std::uint8_t, 4> const& session_word,
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

    /* W29-0.4: which rule set applies.  A MANUAL moved row needs a MANUAL
     * parent (the wave-28 shape); an AUTOMATIC moved row was measured with
     * both an automatic parent and a manual parent (the hybrid pair).  A
     * manual row under an automatic parent is not measured. */
    bool const moved_manual = row_is_manual(state, row);
    bool const parent_manual = row_is_manual(state, parent);
    if (moved_manual && !parent_manual) {
        warning = task_label(edit.uid) +
                  " is manually scheduled and its parent (task uid " +
                  std::to_string(summary.uid) +
                  ") is automatic; no Project save pair measures that mixed"
                  " outdent";
        return false;
    }
    bool const automatic = !moved_manual;
    std::vector<std::uint32_t> expected_slack;
    if (automatic) {
        if (moved.level != 2 || summary.level != 1) {
            warning = task_label(edit.uid) +
                      ": the automatic outdent is measured only for a level-2"
                      " leaf whose parent is a level-1 summary";
            return false;
        }
        if (row_is_manual(state, 0)) {
            warning = task_label(edit.uid) +
                      ": the project row is manually scheduled; the measured"
                      " automatic roll-up needs the automatic project row";
            return false;
        }
        if (!have_project_start || !is_date(project_start)) {
            warning = task_label(edit.uid) +
                      ": 114/Props has no project-start record (0x02400002),"
                      " so the automatic row's Fixed2Data start copy cannot be"
                      " written";
            return false;
        }
        /* Project's save leaves the project row's own +96 alone.  In the
         * automatic-parent pair the project row and the restored summary start
         * with the moved row (restored start); in the hybrid pair the manual
         * parent already starts at the restored start while the project row
         * keeps the project-start record's date. */
        std::uint32_t const wanted_project_start =
            parent_manual ? project_start : edit.restored_start;
        if (state.rows.u32(0, task_row::kStart) != wanted_project_start ||
            rows[0].start != wanted_project_start ||
            rows[parent].start != edit.restored_start ||
            state.rows.u32(parent, task_row::kStart2) != edit.restored_start) {
            warning = task_label(edit.uid) +
                      ": the project row's or the parent's start (stored or"
                      " effective) is not the measured automatic-outdent start;"
                      " that roll-up shape is not measured";
            return false;
        }
        for (std::size_t child : rows[0].child_rows) {
            if (child == parent || rows[child].deleted) {
                continue;
            }
            if (!is_date(rows[child].finish) ||
                rows[child].finish > edit.restored_finish) {
                warning = task_label(edit.uid) +
                          ": another top-level row finishes after the restored"
                          " finish; that automatic roll-up shape is not"
                          " measured";
                return false;
            }
        }
        if (!is_date(moved.finish) ||
            moved.finish > edit.restored_finish) {
            warning = task_label(edit.uid) +
                      ": the moved row's finish is after the restored finish;"
                      " that automatic roll-up shape is not measured";
            return false;
        }
        if (parent_manual &&
            (rows[0].finish != edit.restored_finish ||
             rows[parent].finish != edit.restored_finish ||
             state.rows.u32(parent, task_row::kRemainingDuration) !=
                 edit.restored_duration)) {
            warning = task_label(edit.uid) +
                      ": the project row's finish or the manual parent's"
                      " finish/remaining duration is not the restored one; the"
                      " measured hybrid outdent leaves those words alone";
            return false;
        }
        expected_slack = automatic_slack_uids(state, rows);
        if (parent_manual) {
            add_slack_uid(expected_slack, summary.uid);
        }
        if (!check_slack_set(edit.slack, expected_slack, warning)) {
            return false;
        }
    } else if (!edit.slack.empty()) {
        warning = task_label(edit.uid) +
                  ": the manual outdent takes no +186/+190 slack, but the"
                  " queued edit carries some";
        return false;
    }

    std::vector<std::uint8_t> parent_children_payload;
    bool const parent_has_list =
        stored_child_list(state, summary.uid, parent_children_payload);
    if (automatic && parent_manual) {
        /* MEASURED hybrid shape: an automatic child under a manual parent has
         * no 0x0B4004E4 block, and Project does not remove one. */
        if (parent_has_list) {
            warning = task_label(edit.uid) + ": the manual parent summary"
                      " (task uid " + std::to_string(summary.uid) +
                      ") carries a 0x0B4004E4 child list; the measured hybrid"
                      " outdent has none";
            return false;
        }
    } else {
        if (!parent_has_list) {
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
                      ": the parent's child list does not name exactly the"
                      " moved row, so the parent's restored leaf shape is not"
                      " measured";
            return false;
        }
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
    bool const hybrid = automatic && parent_manual;
    if (!hybrid) {
        project_ids.push_back(moved.row_id);
        std::sort(project_ids.begin(), project_ids.end());
    }

    /* TBkndAssn (W29-0.1): Project RE-CREATES the assignment of the summary
     * that becomes a leaf again.  Measured only for exactly one tombstoned row
     * for that task (and no live row); the new row's contour is built from the
     * tombstoned row's planned work, so that value must be a whole work tick.
     * W29-0.4: its date words mirror the restored task's effective pair, and
     * its 0x0F40027A block copies the current per-session word from 114/Props
     * record 0x02400007. */
    std::vector<std::size_t> const assn_rows =
        assn_rows_for_task(state.assn, summary.uid);
    if (assn_rows.size() != 1 ||
        !assn_row_deleted(state.assn, assn_rows.front())) {
        warning = task_label(edit.uid) + ": task uid " +
                  std::to_string(summary.uid) + " has " +
                  std::to_string(assn_rows.size()) +
                  " assignment rows and none is the single tombstone the"
                  " measured outdent re-creates";
        return false;
    }
    std::size_t const tombstone_assn_row = assn_rows.front();
    double const planned_work =
        get_f64(state.assn.fixed,
                tombstone_assn_row * kAssnStride + kAssnPlannedWork);
    double const work_ticks = planned_work / 100.0;
    if (!std::isfinite(planned_work) || planned_work <= 0.0 ||
        !std::isfinite(work_ticks) || work_ticks != std::floor(work_ticks) ||
        work_ticks > 4294967295.0 || work_ticks * 8.0 > 4294967295.0) {
        warning = task_label(edit.uid) + ": the tombstoned assignment's planned"
                  " work is not a positive whole number of work ticks, so the"
                  " 0x0F400031 contour Project writes is not measured";
        return false;
    }
    std::uint32_t const work_tick_count =
        static_cast<std::uint32_t>(work_ticks);

    /* The re-created assignment's +52/+56/+60/+104 mirror the restored task's
     * effective +104/+108.  The automatic pairs recompute +108 to the restored
     * finish; the manual pair keeps the effective pair the file stored. */
    std::uint32_t const assn_start =
        state.rows.u32(parent, task_row::kStart2);
    std::uint32_t const assn_finish =
        parent_manual ? state.rows.u32(parent, task_row::kFinish2)
                      : edit.restored_finish;
    if (!is_date(assn_start) || !is_date(assn_finish)) {
        warning = task_label(edit.uid) + ": the restored summary (task uid " +
                  std::to_string(summary.uid) +
                  ") has no valid effective start/finish, so the re-created"
                  " assignment's date words cannot be written";
        return false;
    }
    if (!have_session_word) {
        warning = task_label(edit.uid) + ": 114/Props has no 0x02400007"
                  " current-session word, so the re-created assignment's"
                  " 0x0F40027A block cannot be written";
        return false;
    }

    std::uint32_t const new_level = moved.level - 1;
    state.rows.set_u32(parent, task_row::kDuration, edit.restored_duration);
    state.rows.set_u32(parent, task_row::kStart, edit.restored_start);
    state.rows.set_u32(parent, task_row::kFinish, edit.restored_finish);
    state.rows.set_u16(parent, kHasChildrenByte, 0);
    state.rows.set_u16(parent, task_row::kDurationFormat,
                       edit.restored_duration_format);
    state.rows.set_u32(parent, task_row::kStartAnchor,
                       parent_manual ? parent_start : kInvalidDate);

    if (!parent_manual) {
        /* W29-0.4 automatic-parent pair: the restored summary's +88 and +108
         * roll up, and so do the project row's duration and finish.  The
         * guards above make the project's span the restored duration. */
        state.rows.set_u32(parent, task_row::kRemainingDuration,
                           edit.restored_duration);
        state.rows.set_u32(parent, task_row::kFinish2, edit.restored_finish);
        state.rows.set_u32(0, task_row::kDuration, edit.restored_duration);
        state.rows.set_u32(0, task_row::kRemainingDuration,
                           edit.restored_duration);
        state.rows.set_u32(0, task_row::kFinish, edit.restored_finish);
        state.rows.set_u32(0, task_row::kFinish2, edit.restored_finish);
    }

    state.rows.set_u16(row, kOutlineLevelWord,
                       static_cast<std::uint16_t>(new_level));
    state.rows.set_u16(row, kParentRowByte,
                       static_cast<std::uint16_t>(new_parent_row_id));

    set_row_flags(state, parent, summary.level == 1
                                     ? kFixed2FlagTopLevelLeaf
                                     : kFixed2FlagChildLeaf);
    /* MEASURED: an automatic outdent (both the automatic and the hybrid
     * manual-parent pair) stores the Props project-start record (0x02400002)
     * in the restored leaf's +46, not the parent's +104; the wave-28 manual
     * pair stores +104. */
    set_row_start_copy(state, parent,
                       automatic ? project_start : parent_start);
    set_fixed2_meta_byte(state, parent, kFixed2MetaKind, 0x10);
    set_fixed_meta_blob(state, parent, false, !parent_manual);

    set_row_parent(state, row, row_identity(state, summary.parent_row));
    set_row_flags(state, row, new_level == 1 ? kFixed2FlagTopLevelLeaf
                                             : kFixed2FlagChildLeaf);
    if (automatic) {
        /* MEASURED: a top-level automatic leaf stores NA in +174, kind 0x10
         * in Fixed2Meta, and the project start (Props 0x02400002) as its
         * Fixed2Data +46 start copy. */
        state.rows.set_u32(row, task_row::kStartAnchor, kInvalidDate);
        set_fixed2_meta_byte(state, row, kFixed2MetaKind, 0x10);
        set_row_start_copy(state, row, project_start);
        set_moved_fixed_meta_blob(state, row, false);
        apply_slack(state, rows, edit.slack);
    }

    std::size_t added = 0;
    std::size_t removed = 0;
    if (!hybrid) {
        removed += remove_var_entries(state, summary.uid, kChildListField);
        /* Project appends the project's grown child list first, then the
         * restored leaf's 0x0B4004E5 block (measured: offsets 548 then 568).
         * A block append to an EXISTING entry is not a field-count change;
         * set_var_block only increments `added` for a new entry. */
        std::size_t project_added = 0;
        set_var_block(state, kProjectUid, kChildListField,
                      encode_child_list(project_ids), project_added);
        added += project_added;
    }
    if (find_var_offsets(state.var_meta, kLeafField, summary.uid).empty()) {
        set_var_block(state, summary.uid, kLeafField,
                      std::vector<std::uint8_t>(std::begin(kLeafBlock),
                                                std::end(kLeafBlock)),
                      added);
    }
    std::size_t moved_added = 0;
    if (automatic &&
        find_var_offsets(state.var_meta, kLeafField, moved.uid).empty()) {
        /* MEASURED hybrid pair: the moved automatic row gains its leaf block
         * after the restored parent's (offsets 536 then 548). */
        set_var_block(state, moved.uid, kLeafField,
                      std::vector<std::uint8_t>(std::begin(kLeafBlock),
                                                std::end(kLeafBlock)),
                      moved_added);
    }
    recreate_assignment(state, tombstone_assn_row, parent, work_tick_count,
                        assn_start, assn_finish, parent_manual, session_word);

    adjust_field_count(state, parent,
                       static_cast<std::int64_t>(added) -
                           static_cast<std::int64_t>(removed));
    if (moved_added != 0) {
        adjust_field_count(state, row,
                           static_cast<std::int64_t>(moved_added));
    }
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
    /* W29-1.0b: the opened project row, kept for the
     * DocumentSummaryInformation rewrite at the end (the automatic roll-up
     * changes the project row's duration and finish). */
    TaskTable const opened_rows = state.rows;

    /* 114/Props is read before the edits because an automatic outdent needs the
     * project-start record, an outdent's re-created assignment copies the
     * current-session word, and the project-finish record is rewritten after
     * them.  A missing stream is only fatal once something is written (the
     * existing behaviour); a malformed record is fatal here. */
    std::string const props_path = props_path_for(table);
    bool const has_props = doc.has_stream(props_path);
    std::vector<std::uint8_t> props =
        has_props ? doc.read_stream(props_path) : std::vector<std::uint8_t>{};
    std::size_t const project_start_at =
        has_props ? props_record_payload(props, kPropProjectStart)
                  : static_cast<std::size_t>(-1);
    std::size_t const project_finish_at =
        has_props ? props_record_payload(props, kPropProjectFinish)
                  : static_cast<std::size_t>(-1);
    std::size_t const session_word_at =
        has_props ? props_record_payload(props, kPropSessionWord)
                  : static_cast<std::size_t>(-1);
    bool const have_project_start =
        project_start_at != static_cast<std::size_t>(-1);
    std::uint32_t const project_start =
        have_project_start ? get_u32(props, project_start_at) : kInvalidDate;
    bool const have_session_word =
        session_word_at != static_cast<std::size_t>(-1);
    std::array<std::uint8_t, 4> session_word{};
    if (have_session_word) {
        std::copy(props.begin() + static_cast<std::ptrdiff_t>(session_word_at),
                  props.begin() +
                      static_cast<std::ptrdiff_t>(session_word_at + 4),
                  session_word.begin());
    }

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
        bool const applied =
            edit.indent
                ? apply_indent(state, edit, rows, row, warning)
                : apply_outdent(state, edit, rows, row, have_project_start,
                                project_start, have_session_word, session_word,
                                warning);
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

    /* --- TBkndAssn: the tombstoned or re-created assignment row ---------- */
    AssnState& assn = state.assn;
    std::size_t const assn_entry_count =
        read_var_entries(assn.var_meta).size();
    if (assn.var_meta.size() !=
        kVarMetaHeaderSize + assn_entry_count * kVarMetaEntrySize) {
        throw Error("TBkndAssn/VarMeta lost its 24 + 12*n shape while editing"
                    " the outline");
    }
    put_u32(assn.var_meta, 8, static_cast<std::uint32_t>(assn_entry_count));
    put_u32(assn.var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(assn.var2.size()));
    put_u32(assn.meta, 8, static_cast<std::uint32_t>(assn.row_count));
    put_u32(assn.meta, 12, static_cast<std::uint32_t>(assn.fixed.size()));
    put_u32(assn.fixed2_meta, 8, static_cast<std::uint32_t>(assn.row_count));
    put_u32(assn.fixed2_meta, 12,
            static_cast<std::uint32_t>(assn.fixed2.size()));
    std::vector<std::uint8_t> const final_assn_var_meta =
        merge_logical_var_meta(assn.original_var_meta, assn.var_meta);

    /* --- 114/Props: the six counters an outline save moves, and the
     * project-finish date record every pair keeps equal to the project row's
     * +100 ------------------------------------------------------------- */
    if (!has_props) {
        throw Error(props_path +
                    " is absent, so the outline save counters cannot be kept");
    }
    if (project_finish_at == static_cast<std::size_t>(-1)) {
        throw Error("114/Props has no 4-byte project-finish record "
                    "0x02400003, so the saved project finish cannot be kept"
                    " equal to the project row");
    }
    put_u32(props, project_finish_at,
            state.rows.u32(0, task_row::kFinish));
    std::size_t const task_freed_at = props_value_offset(props, kPropTaskFreed);
    std::size_t const task_size_at =
        props_value_offset(props, kPropTaskVar2Size);
    std::size_t const assn_rows_at = props_value_offset(props, kPropAssnRows);
    std::size_t const assn_freed_at = props_value_offset(props, kPropAssnFreed);
    std::size_t const assn_size_at =
        props_value_offset(props, kPropAssnVar2Size);
    std::size_t const assn_deleted_at =
        props_value_offset(props, kPropAssnDeleted);
    std::uint64_t const task_freed =
        static_cast<std::uint64_t>(get_u32(props, task_freed_at)) +
        state.task_freed_bytes;
    std::uint64_t const assn_freed =
        static_cast<std::uint64_t>(get_u32(props, assn_freed_at)) +
        state.assn_freed_bytes;
    if (task_freed > 0xFFFFFFFFu || assn_freed > 0xFFFFFFFFu) {
        throw Error("the saved freed-byte counters would overflow 32 bits");
    }
    put_u32(props, task_freed_at, static_cast<std::uint32_t>(task_freed));
    put_u32(props, task_size_at, static_cast<std::uint32_t>(state.var2.size()));
    put_u32(props, assn_rows_at, static_cast<std::uint32_t>(assn.row_count));
    put_u32(props, assn_freed_at, static_cast<std::uint32_t>(assn_freed));
    put_u32(props, assn_size_at, static_cast<std::uint32_t>(assn.var2.size()));
    put_u32(props, assn_deleted_at,
            assn_has_unresolved_tombstone(assn) ? 1u : 0u);

    /* W29-1.0b: Project records the edit in Props14 record 0x3540000a (payload
     * 01 00) on every editor save; a missing record in a hand-built fixture is
     * a no-op, a damaged one refuses the save before anything is stored. */
    write_edit_marker(doc);

    state.rows.store_into(doc, table);
    doc.replace_stream(state.meta_path, state.meta);
    doc.replace_stream(state.fixed2_path, state.fixed2);
    doc.replace_stream(state.fixed2_meta_path, state.fixed2_meta);
    doc.replace_stream(state.var2_path, state.var2);
    doc.replace_stream(state.var_meta_path, final_var_meta);
    doc.replace_stream(assn.fixed_path, assn.fixed);
    doc.replace_stream(assn.meta_path, assn.meta);
    doc.replace_stream(assn.fixed2_path, assn.fixed2);
    doc.replace_stream(assn.fixed2_meta_path, assn.fixed2_meta);
    doc.replace_stream(assn.var2_path, assn.var2);
    doc.replace_stream(assn.var_meta_path, final_assn_var_meta);
    doc.replace_stream(props_path, props);

    /* W29-1.0b: an automatic indent/outdent changes the project row's
     * duration and finish, and Project's save changes the project summary's
     * display strings with them; both are edit effects, not per-save noise
     * (the null saves leave DocumentSummaryInformation byte for byte equal to
     * the control). */
    write_project_summary(doc, opened_rows, state.rows);

    std::string const inconsistent = check_table(doc, table);
    if (!inconsistent.empty()) {
        throw Error("the outline writer left the task table inconsistent: " +
                    inconsistent);
    }
    try {
        AssnState const check(doc, assn_table_for(table));
        (void)check;
    } catch (Error const& caught) {
        throw Error(std::string("the outline writer left the assignment table "
                                "inconsistent: ") +
                    caught.what());
    }
    return result;
}

} /* namespace mppwrite */
