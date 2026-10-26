/*
 * actuals.cpp -- the measured actuals writer behind a %-complete edit.
 *
 * The rules are stated with their evidence in actuals.h.  This file keeps the
 * two layers apart on purpose:
 *
 *   - the ROW layer writes the ten words Project's save changes, from the
 *     caller's percent and dates;
 *   - the STORE layer maintains 0x0B40015E / 0x0B4004E5 and the FixedMeta
 *     variable-field count, which is where Project's own save is easiest to get
 *     wrong: entries are physically added and removed, and the count tracks the
 *     net change (11 -> 9 on the manual pair, 9 -> 9 on the automatic one).
 *
 * Dates are the caller's because working time needs the project calendar, which
 * lives in app/ (core_mppwrite is standard-library only by design).  The one
 * exception is ActualDuration, which is pure arithmetic and is exposed so the
 * caller computes Stop from the same value this writer stores.
 */

#include "actuals.h"

#include "add_delete.h"
#include "edit_marker.h"
#include "summary_info.h"
#include "table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

namespace mppwrite {
namespace {

constexpr std::size_t kFixedMetaHeaderSize = 16;
constexpr std::size_t kFixedMetaEntrySize = 47;
constexpr std::size_t kFixedMetaFieldCount = 2;
constexpr std::size_t kFixedMetaRowOffset = 4;
constexpr std::uint8_t kFixedMetaStateDeleted = 0x02;

constexpr std::size_t kFixed2RecordSize = 64;
constexpr std::size_t kFixed2RecordRowOffset = 24;
constexpr std::size_t kFixed2RecordFlags = 42;
constexpr std::uint8_t kFixed2FlagHasChildren = 0x40;

constexpr std::size_t kFixed2MetaHeaderSize = 16;
constexpr std::size_t kFixed2MetaEntrySize = 96;
constexpr std::size_t kFixed2MetaFlags = 8;
constexpr std::uint8_t kFixed2MetaFlagManual = 0x80;

/* The first step of every 0x0B40015E block Project writes from scratch: flag 1,
 * percent 32768.0 (2^15), fresh session token.  MEASURED on the fresh 0 -> 40
 * blocks of both wave-28 zero-percent pairs (auto and manual): three steps --
 * this marker, the new middle, the constant final. */
constexpr double kMarkerPercent = 32768.0;

/* The variable fields Project's percent save touches (measured, W28-1.1).
 * 0x0B40015E is the progress-step block; 0x0B4004E5 is the constant 8-byte
 * block a 0 % row keeps; 0x0B400505/0x0B400506 are the two 2-byte manual
 * fields a 0 % manual row carries and loses when it first gets progress.
 * W29-0.2: 0x0B4004E3 is a second constant 8-byte 0 % block -- the wave-29
 * work pair removes it at first progress alongside 0x0B4004E5 -- and
 * 0x0B4004E4 is uid 0's project-mode block, whose 12-byte payload a milestone
 * 0 -> 100 shrinks in place to 8 bytes ({2,1,7} -> {1,1}, freeing 4). */
constexpr std::uint32_t kProgressField = 0x0B40015Eu;
constexpr std::uint32_t kResetField = 0x0B4004E5u;
constexpr std::uint32_t kResetFieldAlt = 0x0B4004E3u;
constexpr std::uint32_t kProjectModeField = 0x0B4004E4u;
constexpr std::uint32_t kManualFieldA = 0x0B400505u;
constexpr std::uint32_t kManualFieldB = 0x0B400506u;
constexpr std::uint32_t kFinalStepId = 0x340A75F7u;
constexpr std::uint32_t kProgressFlagBits = 0x9000u;

/* W28-1.3: the project status-date record in 114/Props (kind 4, 4-byte packed
 * date; 0xffffffff = NA).  MEASURED on the update pair: the record sits at
 * stream offset 1292 (payload 1304) and its payload goes ffffffff ->
 * 0x3bf40000; the stream size is unchanged. */
constexpr std::uint32_t kStatusDateField = 0x02400045u;
constexpr std::uint32_t kStatusDateKind = 4u;

/* The constant payload of a 0x0B4004E5 block: one u32 pair and a u16 pair. */
constexpr std::uint8_t kResetBlock[8] = {0x01, 0x00, 0x00, 0x00,
                                         0x01, 0x00, 0xFF, 0xFF};

/* W29-0.1: the Task metadata Project rewrites on an actuals save.
 *   FixedMeta +44 is 0x08 once the row has progress, 0x00 at 0 % (measured on
 *   every pair; the control rows carry 0x00 or, at a saved 40 %, 0x08).
 *   FixedMeta +8..23 is the opaque cache blob; three of its bits follow the
 *   edit: entry+9 bit 0x40 (percent < 100 and Actual == Duration*percent/100),
 *   entry+17 bit 0x04 (automatic rows only: 0 < percent < 100; manual rows keep
 *   their byte) and entry+18 bit 0x20 (a percent edit sets it when the old or
 *   the new percent is 100, a date update clears it).
 *   Fixed2Meta entry +88 (clear 0x0C once a row has progress; a 0 % write leaves
 *   it alone) and +95 (0x10 at 0 %, 0x00 above).
 *   Fixed2Data record row+3: +42 0x30 at 0 % / 0x01 above; +46 the project start
 *   at 0 %, NA at 100 %, ActualStart for an automatic row below 100 %, and
 *   unchanged for a manual row below 100 %. */
constexpr std::size_t kFixedMetaFlag44 = 44;
constexpr std::size_t kFixedMetaBlob = 8;
constexpr std::size_t kFixed2MetaLeafTail = 88;
constexpr std::uint8_t kFixed2MetaTailClear = 0x0C;
constexpr std::size_t kFixed2MetaKind = 95;
constexpr std::uint8_t kFixed2MetaKindLeaf = 0x10;
constexpr std::size_t kFixed2RecordStartCopy = 46;
constexpr std::uint8_t kFixed2RecordNoProgress = 0x30;
constexpr std::uint8_t kFixed2RecordHasProgress = 0x01;

/* W29-0.2: the task row's work header doubles, MEASURED on the wave-29
 * work-bearing-assignment pair: +8 planned Work, +16 Actual Work and +24
 * Remaining Work, f64, in thousandths of a minute (960000 = 960 minutes).  A
 * row whose assignment names a real resource stores them from that assignment;
 * an unassigned placeholder row keeps its zeros (the leaf and milestone pairs
 * do not move them). */
constexpr std::size_t kTaskWork = 8;
constexpr std::size_t kTaskActualWork = 16;
constexpr std::size_t kTaskRemainingWork = 24;

/* TBkndAssn: 110-byte rows, 34-byte FixedMeta entries, 53-byte Fixed2Meta
 * entries, and the two variable fields Project's save updates (0F400031 work
 * contour, 0F400032 actual work). */
constexpr std::size_t kAssnStride = 110;
constexpr std::size_t kAssnUid = 0;
constexpr std::size_t kAssnTask = 4;
/* +8 is the resource uid: 0 on a project/summary row, 0xFFFF0001 on Project's
 * UNASSIGNED placeholder assignment, a real resource otherwise (W29-0.2: the
 * work pair's row names the file's one resource; the project row's work
 * doubles and % Work Complete follow the real rows only). */
constexpr std::size_t kAssnResource = 8;
constexpr std::uint32_t kNullResource = 0xFFFF0001u;
constexpr std::size_t kAssnUnits = 12;
constexpr std::size_t kAssnPlanned = 20;
constexpr std::size_t kAssnActual = 28;
constexpr std::size_t kAssnRemaining = 44;
constexpr std::size_t kAssnResume = 60;
constexpr std::size_t kAssnStartCopy = 104;
constexpr std::size_t kAssnMetaHeader = 16;
constexpr std::size_t kAssnMetaEntry = 34;
constexpr std::size_t kAssnMetaFieldCount = 2;
constexpr std::size_t kAssnMetaRowOffset = 4;
constexpr std::size_t kAssnMetaWord10 = 10;
constexpr std::size_t kAssnMetaWord14 = 14;
constexpr std::size_t kAssnFixed2MetaEntry = 53;
constexpr std::size_t kAssnFixed2MetaFlags = 9;
constexpr std::uint32_t kAssnWorkField = 0x0F400031u;
constexpr std::uint32_t kAssnActualWorkField = 0x0F400032u;
constexpr std::size_t kAssnWorkHeader = 44;
constexpr std::size_t kAssnWorkRecord = 28;
constexpr std::size_t kAssnActualHeader = 36;
constexpr std::size_t kAssnActualRecord = 20;
constexpr std::uint32_t kAssnWorkMagic = 0x00200000u;

/* 114/Props scalar entries: [u32 LE 4][tag 4 bytes BIG-endian][u32 LE 4]
 * [value u32 LE].  The four counters an actuals save moves; each occurs exactly
 * once in every measured file (W29-0.1). */
constexpr std::uint32_t kPropTaskFreed = 0x01000002u;
constexpr std::uint32_t kPropTaskVar2Size = 0x01000100u;
constexpr std::uint32_t kPropAssnFreed = 0x04000002u;
constexpr std::uint32_t kPropAssnVar2Size = 0x04000100u;
constexpr std::size_t kPropScalarSize = 16;

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
    double value = 0;
    static_assert(sizeof bits == sizeof value, "IEEE-754 double expected");
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

/* TaskTable exposes u16/u32 setters only; a row-header double is its two
 * little-endian words (W29-0.2). */
void set_row_f64(TaskTable& rows, std::size_t row, std::size_t offset,
                 double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof bits == sizeof value, "IEEE-754 double expected");
    std::memcpy(&bits, &value, sizeof bits);
    rows.set_u32(row, offset, static_cast<std::uint32_t>(bits & 0xFFFFFFFFu));
    rows.set_u32(row, offset + 4, static_cast<std::uint32_t>(bits >> 32));
}

/* One 20-byte step of a 0x0B40015E block.  The trailing id is an opaque token
 * Project allocates per session; it is never interpreted, only carried for the
 * steps already in the file and freshly allocated for new ones. */
struct ProgressStep {
    std::uint32_t flag = 0;
    double percent = 0;
    std::uint32_t id = 0;
};

std::vector<std::uint8_t> encode_steps(std::vector<ProgressStep> const& steps) {
    std::vector<std::uint8_t> out(16 + 20 * steps.size(), 0);
    put_u16(out, 0, static_cast<std::uint16_t>(steps.size()));
    put_u16(out, 2, 4);
    put_u16(out, 4, 16);
    put_u16(out, 6, 0);
    for (std::size_t i = 0; i < steps.size(); ++i) {
        std::size_t const at = 16 + 20 * i;
        put_u32(out, at, steps[i].flag);
        put_u32(out, at + 4, 0);
        put_f64(out, at + 8, steps[i].percent);
        put_u32(out, at + 16, steps[i].id);
    }
    return out;
}

/* Decode an existing block, refusing a shape the writer would not reproduce
 * (the header is constant in every measured file). */
std::vector<ProgressStep> decode_steps(std::vector<std::uint8_t> const& payload) {
    if (payload.size() < 16) {
        throw Error("0x0B40015E block is short of its 16-byte header");
    }
    std::size_t const count = get_u16(payload, 0);
    if (payload.size() != 16 + 20 * count) {
        throw Error("0x0B40015E block is " + std::to_string(payload.size()) +
                    " bytes for a declared " + std::to_string(count) +
                    " steps (expected 16 + 20*n)");
    }
    if (get_u16(payload, 2) != 4 || get_u16(payload, 4) != 16 ||
        get_u16(payload, 6) != 0 || get_u32(payload, 8) != 0 ||
        get_u32(payload, 12) != 0) {
        throw Error("0x0B40015E block has an unexpected header");
    }
    std::vector<ProgressStep> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t const at = 16 + 20 * i;
        if (get_u32(payload, at + 4) != 0) {
            throw Error("0x0B40015E step " + std::to_string(i) +
                        " has a non-zero reserved word");
        }
        ProgressStep step;
        step.flag = get_u32(payload, at);
        step.percent = get_f64(payload, at + 8);
        step.id = get_u32(payload, at + 16);
        out.push_back(step);
    }
    return out;
}

/* A deterministic, non-zero token, distinct from every id already in the block
 * and from Project's constant final id.  The value is opaque to Project; making
 * it a function of (uid, percent, attempt) keeps our saves reproducible. */
std::uint32_t fresh_step_id(std::uint32_t uid, std::uint16_t percent,
                            std::vector<ProgressStep> const& steps) {
    for (std::uint32_t attempt = 0;; ++attempt) {
        std::uint64_t hash = 1469598103934665603ull;
        auto mix = [&hash](std::uint32_t value) {
            for (int i = 0; i < 4; ++i) {
                hash ^= (value >> (8 * i)) & 0xFFu;
                hash *= 1099511628211ull;
            }
        };
        mix(uid);
        mix(percent);
        mix(attempt);
        std::uint32_t id = static_cast<std::uint32_t>(hash ^ (hash >> 32));
        id |= 1u; /* never zero */
        bool clash = id == kFinalStepId;
        for (ProgressStep const& step : steps) {
            clash = clash || step.id == id;
        }
        if (!clash) {
            return id;
        }
    }
}

/* TBkndTask's path with the table name swapped for TBkndAssn.  The writer only
 * ever runs on a task table; any other path is a caller mistake, not a shape to
 * guess about. */
std::string assn_table_for(std::string_view task_table) {
    constexpr std::string_view suffix = "TBkndTask";
    if (task_table.size() < suffix.size() ||
        task_table.substr(task_table.size() - suffix.size()) != suffix) {
        throw Error("the actuals writer needs a TBkndTask path to locate "
                    "TBkndAssn, not '" + std::string(task_table) + "'");
    }
    std::string out(task_table);
    out.replace(out.size() - suffix.size(), suffix.size(), "TBkndAssn");
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
 * big-endian tag.  Requiring exactly one occurrence is the measured shape: the
 * save pair moves one record per counter (W29-0.1). */
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
                    std::to_string(tag) + ", so the save counters cannot be "
                    "kept");
    }
    return found;
}

/* Remove every VarMeta entry for (mid, field); return how many went and add the
 * payload bytes of each removed block to freed_bytes.  `var2` is needed because
 * the entry names the block: Project's freed-byte counters in 114/Props move by
 * exactly the block payload sizes (W29-0.1).  Matching is exact on mid: the
 * writer only ever creates and owns mid == uid entries for these fields, so a
 * 0x80000000|uid family entry is not silently swallowed. */
std::size_t remove_var_entries(std::vector<std::uint8_t>& var_meta,
                               std::vector<std::uint8_t> const& var2,
                               std::uint32_t mid, std::uint32_t field_id,
                               std::size_t& freed_bytes) {
    std::vector<VarEntry> const entries = read_var_entries(var_meta);
    std::size_t removed = 0;
    for (std::size_t i = entries.size(); i-- > 0;) {
        if (entries[i].mid == mid && entries[i].field_id == field_id) {
            if (static_cast<std::size_t>(entries[i].offset) + 4 > var2.size()) {
                throw Error("VarMeta entry for field 0x" +
                            std::to_string(field_id) + " points past Var2Data");
            }
            freed_bytes += get_u32(var2, entries[i].offset);
            std::size_t const at = kVarMetaHeaderSize + i * kVarMetaEntrySize;
            var_meta.erase(var_meta.begin() + static_cast<std::ptrdiff_t>(at),
                           var_meta.begin() +
                               static_cast<std::ptrdiff_t>(at + kVarMetaEntrySize));
            ++removed;
        }
    }
    return removed;
}

std::size_t count_var_entries(std::vector<std::uint8_t> const& var_meta) {
    return read_var_entries(var_meta).size();
}

/* Point the single existing (mid, field) entry at `offset`.  W29-0.2: when a
 * rebuilt block does not fit its old slot the new block is APPENDED and the
 * entry repointed; the old bytes stay in Var2Data and the freed-byte counter
 * does not move (measured on the work pair, whose 0F400031 grows 72 -> 100). */
void repoint_var_entry(std::vector<std::uint8_t>& var_meta, std::uint32_t mid,
                       std::uint32_t field_id, std::uint32_t offset) {
    std::size_t found = 0;
    for (VarEntry const& entry : read_var_entries(var_meta)) {
        if (entry.mid != mid || entry.field_id != field_id) {
            continue;
        }
        std::size_t const at =
            kVarMetaHeaderSize + entry.index * kVarMetaEntrySize + 4;
        put_u32(var_meta, at, offset);
        ++found;
    }
    if (found != 1) {
        throw Error("expected one VarMeta entry for field 0x" +
                    std::to_string(field_id) + " of uid " +
                    std::to_string(mid) + ", found " + std::to_string(found));
    }
}

/* Project's VarMeta has a LOGICAL entry count in header +8 and a possibly longer
 * PHYSICAL array.  MEASURED (W28-1.1): the automatic pairs are 528 bytes with
 * header 40, physical 42 -- slots [40],[41] are stale duplicates no reader is
 * meant to see; the manual pair goes from 528/header 42/physical 42 to
 * 528/header 40/physical 42, and the bytes of its original slots [40],[41] are
 * left exactly as they were.  So the writer edits only the logical array and
 * puts it back into the first C_new slots of the ORIGINAL stream, keeping the
 * stream size and every byte past the new logical count.  Growing past the
 * physical array is unmeasured; that case returns the (longer) logical stream.
 */
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

/* FixedMeta entry that names this FixedData row, or SIZE_MAX. */
std::size_t meta_entry_for_row(std::vector<std::uint8_t> const& meta,
                               std::size_t row) {
    std::uint32_t const offset =
        static_cast<std::uint32_t>(task_row_offset(row));
    for (std::size_t i = 0; i < (meta.size() - kFixedMetaHeaderSize) /
                                    kFixedMetaEntrySize;
         ++i) {
        std::size_t const base =
            kFixedMetaHeaderSize + i * kFixedMetaEntrySize;
        if (get_u32(meta, base + kFixedMetaRowOffset) == offset) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

/* One 0F400031 payload of `records` contour records.  MEASURED on the wave-29
 * leaf pair: header magic 0x00200000, length 44, units*16 at +8, remaining
 * work at +16, remaining ticks*8 at +24 and +32 = 1; every 28-byte record
 * carries (remaining work, units*16, 0, 0, remaining ticks*8).  Project writes
 * one record per started 4800-tick day block, never fewer than one. */
std::vector<std::uint8_t> build_work_block(std::size_t records, double units16,
                                           double remaining_work,
                                           std::uint32_t remaining_ticks8) {
    std::vector<std::uint8_t> work(
        kAssnWorkHeader + records * kAssnWorkRecord, 0);
    put_u32(work, 0, kAssnWorkMagic);
    put_u32(work, 4, static_cast<std::uint32_t>(kAssnWorkHeader));
    put_f64(work, 8, units16);
    put_f64(work, 16, remaining_work);
    put_u32(work, 24, remaining_ticks8);
    put_u32(work, 28, 0);
    put_u32(work, 32, 1);
    put_u32(work, 36, 0);
    put_u32(work, 40, 0);
    for (std::size_t i = 0; i < records; ++i) {
        std::size_t const at = kAssnWorkHeader + i * kAssnWorkRecord;
        put_f64(work, at, remaining_work);
        put_f64(work, at + 8, units16);
        put_u32(work, at + 16, 0);
        put_u32(work, at + 20, 0);
        put_u32(work, at + 24, remaining_ticks8);
    }
    return work;
}

/* W29-0.2: a milestone 0 -> 100 shrinks uid 0's 0x0B4004E4 project-mode block
 * in place.  MEASURED on the milestone pair: the staged base's 12-byte payload
 * {2,1,7} becomes {1,1}, the length word 12 -> 8, the stale third word stays as
 * garbage, and the freed-byte counter gains 4.  An already-shrunk block is left
 * alone so a cascade of milestones counts the four bytes once. */
void shrink_project_mode_block(std::vector<std::uint8_t>& var_meta,
                               std::vector<std::uint8_t>& var2,
                               std::size_t& freed_bytes) {
    std::vector<std::uint32_t> const offsets =
        find_var_offsets(var_meta, kProjectModeField, 0);
    if (offsets.size() != 1) {
        throw Error("the project row has " + std::to_string(offsets.size()) +
                    " 0x0B4004E4 blocks; the measured shape is one");
    }
    std::uint32_t const offset = offsets.front();
    std::vector<std::uint8_t> const old = read_var_block(var2, offset);
    if (old.size() == 8) {
        return; /* already shrunk by an earlier milestone in this save */
    }
    if (old.size() != 12) {
        throw Error("the project row's 0x0B4004E4 block is " +
                    std::to_string(old.size()) +
                    " bytes, not the measured 12-byte shape");
    }
    put_u32(var2, offset, 8);
    put_u32(var2, offset + 4, 1);
    put_u32(var2, offset + 8, 1);
    freed_bytes += 4;
}

/* The task outline: which rows are summaries, and each row's parent.  Summaries
 * follow Fixed2Data's parent links, as core/mpp does (reader.cpp
 * read_outline): a row that some other row names as its parent has children,
 * and its own Duration/Actual Duration are roll-ups, not work.  PROJECT_A's
 * stored has-children flag disagrees with the links on some rows, so the links
 * win.  `parent` is SIZE_MAX for a row whose link is absent or names no row;
 * the wave-29 pairs' links reach the project row (uid 0). */
struct Outline {
    std::vector<bool> summary;
    std::vector<std::size_t> parent;
};

Outline read_outline(CfbDocument const& doc, std::string_view table,
                     std::size_t rows) {
    Outline out;
    out.summary.assign(rows, false);
    out.parent.assign(rows, static_cast<std::size_t>(-1));
    std::string const fixed2_path = table_stream(table, "Fixed2Data");
    if (!doc.has_stream(fixed2_path)) {
        return out;
    }
    std::vector<std::uint8_t> const fixed2 = doc.read_stream(fixed2_path);
    if (fixed2.size() != kFixed2RecordSize * (rows + 3)) {
        return out; /* damaged outline: treat the table as flat */
    }
    using RowId = std::array<std::uint8_t, 16>;
    auto record = [&](std::size_t row) {
        return kFixed2RecordSize * (row + 3);
    };
    auto read_id = [&](std::size_t record_at, std::size_t offset) {
        RowId id{};
        for (std::size_t i = 0; i < id.size(); ++i) {
            id[i] = fixed2[record_at + offset + i];
        }
        return id;
    };
    RowId const no_id{};
    std::map<RowId, std::size_t> row_by_id;
    for (std::size_t row = 0; row < rows; ++row) {
        RowId const id = read_id(record(row), 0);
        if (id != no_id) {
            row_by_id.emplace(id, row);
        }
    }
    for (std::size_t row = 1; row < rows; ++row) {
        if (fixed2[record(row) + kFixed2RecordFlags] & kFixed2FlagHasChildren) {
            out.summary[row] = true;
        }
        RowId const parent = read_id(record(row), kFixed2RecordRowOffset);
        if (parent == no_id) {
            continue;
        }
        auto const found = row_by_id.find(parent);
        if (found != row_by_id.end() && found->second != row) {
            out.summary[found->second] = true;
            out.parent[row] = found->second;
        }
    }
    return out;
}

/* True when `row` lies under `ancestor` in the outline.  A missing link or a
 * cycle ends the walk without an answer. */
bool descends_from(std::vector<std::size_t> const& parent, std::size_t row,
                   std::size_t ancestor) {
    std::size_t at = row;
    for (std::size_t guard = 0; guard <= parent.size(); ++guard) {
        if (at >= parent.size()) {
            return false;
        }
        at = parent[at];
        if (at == ancestor) {
            return true;
        }
        if (at == static_cast<std::size_t>(-1)) {
            return false;
        }
    }
    return false;
}

/* True when the row's own words already record progress.  The summary roll-up
 * uses it to decide which summaries this save rewrites: only a summary with a
 * progressed leaf descendant takes Project's values (W29-0.2). */
bool row_has_progress(TaskTable const& rows, std::size_t row) {
    return rows.u16(row, task_row::kPercentComplete) != 0 ||
           rows.u32(row, task_row::kActualDuration) != 0 ||
           is_date(rows.u32(row, task_row::kActualStart)) ||
           is_date(rows.u32(row, task_row::kActualFinish));
}

std::vector<bool> deleted_rows(std::vector<std::uint8_t> const& meta,
                               std::size_t rows) {
    std::vector<bool> deleted(rows, false);
    for (std::size_t row = 0; row < rows; ++row) {
        std::size_t const index = meta_entry_for_row(meta, row);
        if (index == static_cast<std::size_t>(-1)) {
            continue;
        }
        std::size_t const base =
            kFixedMetaHeaderSize + index * kFixedMetaEntrySize;
        deleted[row] = meta[base] == kFixedMetaStateDeleted;
    }
    return deleted;
}

/* Recompute a summary row from its leaf descendants.  MEASURED on the wave-29
 * leaf pair (uid 3 at 40 % makes uid 1 and uid 2 roll up):
 *   +80  ActualDuration    = the summary's own Duration * sum(leaf actual) /
 *                            sum(leaf Duration), truncated (uid 1: 3840 ticks,
 *                            uid 2: 5760, both over a 28800-tick own duration);
 *   +88  RemainingDuration = own Duration - that;
 *   +92  PercentComplete   = (100 * sum(leaf actual) + sum(leaf Duration)/2) /
 *                            sum(leaf Duration) (uid 1: 13, uid 2: 20);
 *   +120 ActualStart       = the summary's own Start2 (both 3d4612c0);
 *   +132/+136 Resume/Stop  = the EARLIEST valid leaf resume and stop (both
 *                            3d462418, uid 3's working-time values).
 * A summary with no progressed leaf descendant is left byte for byte, and so
 * are every summary's flag/anchor words.  Returns the rows written so the
 * project roll-up can take its Resume/Stop from them.
 *
 * W30-0.5 (R29-5, High): the rules above were measured with ONE progressed
 * leaf, so only the edited rows' ancestor chain (`recompute`) is written.  A
 * summary elsewhere with progressed leaves keeps Project's stored roll-up: the
 * old every-summary sweep overwrote it with the one-leaf "earliest" rule. */
std::vector<bool> write_summary_rollup(TaskTable& table, Outline const& outline,
                                       std::vector<bool> const& deleted,
                                       std::vector<bool> const& recompute) {
    std::size_t const rows = table.rows();
    std::vector<bool> written(rows, false);
    for (std::size_t summary = 1; summary < rows; ++summary) {
        if (summary >= outline.summary.size() || !outline.summary[summary] ||
            (summary < deleted.size() && deleted[summary])) {
            continue;
        }
        if (summary >= recompute.size() || !recompute[summary]) {
            continue;
        }
        std::uint64_t sum_duration = 0;
        std::uint64_t sum_actual = 0;
        bool progressed = false;
        bool have_resume = false;
        bool have_stop = false;
        std::uint32_t resume = kInvalidDate;
        std::uint32_t stop = kInvalidDate;
        for (std::size_t row = 1; row < rows; ++row) {
            if (row >= outline.summary.size() || outline.summary[row] ||
                (row < deleted.size() && deleted[row])) {
                continue;
            }
            if (!descends_from(outline.parent, row, summary)) {
                continue;
            }
            sum_duration += table.u32(row, task_row::kDuration);
            sum_actual += table.u32(row, task_row::kActualDuration);
            if (row_has_progress(table, row)) {
                progressed = true;
            }
            std::uint32_t const row_resume = table.u32(row, task_row::kResume);
            if (is_date(row_resume) && (!have_resume || row_resume < resume)) {
                resume = row_resume;
                have_resume = true;
            }
            std::uint32_t const row_stop = table.u32(row, task_row::kStop);
            if (is_date(row_stop) && (!have_stop || row_stop < stop)) {
                stop = row_stop;
                have_stop = true;
            }
        }
        if (!progressed || sum_duration == 0) {
            continue;
        }
        std::uint64_t const own = table.u32(summary, task_row::kDuration);
        std::uint64_t const actual =
            std::min<std::uint64_t>(own, own * sum_actual / sum_duration);
        std::uint64_t const rounded =
            (sum_actual * 100 + sum_duration / 2) / sum_duration;
        table.set_u32(summary, task_row::kActualDuration,
                      static_cast<std::uint32_t>(actual));
        table.set_u32(summary, task_row::kRemainingDuration,
                      static_cast<std::uint32_t>(own - actual));
        table.set_u16(
            summary, task_row::kPercentComplete,
            static_cast<std::uint16_t>(std::min<std::uint64_t>(100, rounded)));
        table.set_u32(summary, task_row::kActualStart,
                      table.u32(summary, task_row::kStart2));
        if (have_resume) {
            table.set_u32(summary, task_row::kResume, resume);
        }
        if (have_stop) {
            table.set_u32(summary, task_row::kStop, stop);
        }
        written[summary] = true;
    }
    return written;
}

/* Recompute the project row.  Measured on all four W28-1.2 percent pairs, both
 * W28-1.3 mark pairs and the three wave-29 shapes:
 * PercentComplete = 100 * sum(ActualDuration) / sum(Duration) over the
 * non-summary, non-deleted task rows; ActualDuration = project Duration *
 * that percent / 100; Remaining = project Duration - ActualDuration.
 * ActualStart = the project start (+104) when any applied edit has progress --
 * the milestone pair writes it with total actual time still 0 -- or when a task
 * row carries actual time, NA otherwise.  Resume/Stop are the earliest written
 * SUMMARY date (the leaf pair: 3d462418, which is later than the project start
 * and still wins), else, in Update Project's mode (`from_rows`), the earliest
 * valid per-row date, else the project start.  PercentWorkComplete is the
 * real-assignment ratio, MEASURED on the work pair:
 * round(100 * sum(Actual Work) / sum(Work)) over assignments whose resource
 * word is neither 0 nor Project's 0xFFFF0001 placeholder (40 there; 0 on the
 * placeholder-only leaf and milestone pairs).  Those assignments also give the
 * project row's +8/+16/+24 header doubles.  The flag/anchor words are never
 * touched on the project row. */
void write_project_rollup(TaskTable& table, std::vector<bool> const& excluded,
                          bool resume_stop_from_rows, bool any_progress,
                          std::vector<bool> const& summary_written,
                          double real_work, double real_actual_work,
                          std::uint16_t percent_work) {
    std::uint64_t total_duration = 0;
    std::uint64_t total_actual = 0;
    for (std::size_t row = 1; row < table.rows(); ++row) {
        if (row < excluded.size() && excluded[row]) {
            continue;
        }
        total_duration += table.u32(row, task_row::kDuration);
        total_actual += table.u32(row, task_row::kActualDuration);
    }
    std::uint16_t percent = 0;
    if (total_duration > 0) {
        std::uint64_t const rounded =
            (total_actual * 100 + total_duration / 2) / total_duration;
        percent = static_cast<std::uint16_t>(std::min<std::uint64_t>(100, rounded));
    }
    std::uint32_t const project_duration = table.u32(0, task_row::kDuration);
    std::uint32_t project_actual = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(project_duration) * percent / 100);
    if (project_actual > project_duration) {
        project_actual = project_duration;
    }
    table.set_u32(0, task_row::kActualDuration, project_actual);
    table.set_u32(0, task_row::kRemainingDuration,
                  project_duration - project_actual);
    table.set_u16(0, task_row::kPercentComplete, percent);
    table.set_u16(0, task_row::kPercentWorkComplete, percent_work);
    set_row_f64(table, 0, kTaskWork, real_work);
    set_row_f64(table, 0, kTaskActualWork, real_actual_work);
    set_row_f64(table, 0, kTaskRemainingWork, real_work - real_actual_work);
    if (any_progress || total_actual > 0) {
        std::uint32_t const start = table.u32(0, task_row::kStart2);
        table.set_u32(0, task_row::kActualStart, start);
        bool have_resume = false;
        bool have_stop = false;
        std::uint32_t resume = kInvalidDate;
        std::uint32_t stop = kInvalidDate;
        if (summary_written.size() == table.rows()) {
            for (std::size_t row = 1; row < table.rows(); ++row) {
                if (!summary_written[row]) {
                    continue;
                }
                const std::uint32_t row_resume =
                    table.u32(row, task_row::kResume);
                if (is_date(row_resume) &&
                    (!have_resume || row_resume < resume)) {
                    resume = row_resume;
                    have_resume = true;
                }
                const std::uint32_t row_stop = table.u32(row, task_row::kStop);
                if (is_date(row_stop) && (!have_stop || row_stop < stop)) {
                    stop = row_stop;
                    have_stop = true;
                }
            }
        }
        if (!have_resume && !have_stop && resume_stop_from_rows) {
            for (std::size_t row = 1; row < table.rows(); ++row) {
                if (row < excluded.size() && excluded[row]) {
                    continue;
                }
                const std::uint32_t row_resume = table.u32(row, task_row::kResume);
                if (is_date(row_resume) &&
                    (!have_resume || row_resume < resume)) {
                    resume = row_resume;
                    have_resume = true;
                }
                const std::uint32_t row_stop = table.u32(row, task_row::kStop);
                if (is_date(row_stop) && (!have_stop || row_stop < stop)) {
                    stop = row_stop;
                    have_stop = true;
                }
            }
        }
        /* No measurable date (a table whose progress rows carry no
         * resume/stop has no measured pair): keep the project start rather
         * than writing NA under a positive total. */
        if (!have_resume) {
            resume = start;
        }
        if (!have_stop) {
            stop = start;
        }
        table.set_u32(0, task_row::kResume, resume);
        table.set_u32(0, task_row::kStop, stop);
    } else {
        table.set_u32(0, task_row::kActualStart, kInvalidDate);
        table.set_u32(0, task_row::kResume, kInvalidDate);
        table.set_u32(0, task_row::kStop, kInvalidDate);
    }
}

} /* namespace */

std::uint32_t actual_duration_ticks(std::uint32_t duration_ticks,
                                    std::uint16_t percent) noexcept {
    std::uint64_t const scaled =
        static_cast<std::uint64_t>(duration_ticks) * percent / 100;
    return scaled > duration_ticks ? duration_ticks
                                   : static_cast<std::uint32_t>(scaled);
}

bool task_is_manually_scheduled(CfbDocument const& doc, std::size_t row,
                                std::string_view table) {
    std::string const meta_path = table_stream(table, "FixedMeta");
    std::string const fixed2_meta_path = table_stream(table, "Fixed2Meta");
    if (!doc.has_stream(meta_path) || !doc.has_stream(fixed2_meta_path)) {
        return false;
    }
    std::vector<std::uint8_t> const meta = doc.read_stream(meta_path);
    std::vector<std::uint8_t> const fixed2_meta = doc.read_stream(fixed2_meta_path);
    if (meta.size() < kFixedMetaHeaderSize ||
        (meta.size() - kFixedMetaHeaderSize) % kFixedMetaEntrySize != 0 ||
        fixed2_meta.size() < kFixed2MetaHeaderSize ||
        (fixed2_meta.size() - kFixed2MetaHeaderSize) % kFixed2MetaEntrySize != 0) {
        return false; /* no usable metadata: the reader's default is automatic */
    }
    std::size_t const index = meta_entry_for_row(meta, row);
    if (index == static_cast<std::size_t>(-1)) {
        return false;
    }
    std::size_t const flags_at =
        kFixed2MetaHeaderSize + index * kFixed2MetaEntrySize + kFixed2MetaFlags;
    if (flags_at >= fixed2_meta.size()) {
        return false;
    }
    return (fixed2_meta[flags_at] & kFixed2MetaFlagManual) != 0;
}

bool task_has_recorded_progress(CfbDocument const& doc, std::uint32_t uid,
                                std::string_view table) {
    TaskTable const rows = TaskTable::from_document(doc, table);
    std::size_t row = rows.rows();
    for (std::size_t candidate = 1; candidate < rows.rows(); ++candidate) {
        if (rows.uid(candidate) == uid) {
            row = candidate;
            break;
        }
    }
    if (row == rows.rows()) {
        return false; /* the caller reports a uid with no saved row itself */
    }
    if (rows.u16(row, task_row::kPercentComplete) != 0) {
        return true;
    }
    if (is_date(rows.u32(row, task_row::kActualStart)) ||
        is_date(rows.u32(row, task_row::kActualFinish))) {
        return true;
    }
    return task_has_progress_block(doc, uid, table);
}

bool task_has_progress_block(CfbDocument const& doc, std::uint32_t uid,
                             std::string_view table) {
    std::string const var_meta_path = table_stream(table, "VarMeta");
    if (!doc.has_stream(var_meta_path)) {
        return false;
    }
    std::vector<std::uint8_t> const var_meta = doc.read_stream(var_meta_path);
    return !find_var_offsets(var_meta, kProgressField, uid).empty();
}

PercentActualsResult write_percent_actuals(
    CfbDocument& doc, std::vector<PercentActualEdit> const& edits,
    std::string_view table, PercentActualsOptions const& options) {
    PercentActualsResult result;
    TaskTable rows = TaskTable::from_document(doc, table);
    /* W29-1.0b: the opened project row, kept for the DocumentSummaryInformation
     * rewrite at the end.  The summary's start/finish FILETIMEs are derived
     * from the DAY DELTA of this row, so the opened bytes (not a timezone
     * guess) are the reference. */
    TaskTable const opened_rows = rows;
    std::size_t const row_count = rows.rows();

    std::string const meta_path = table_stream(table, "FixedMeta");
    std::string const var_meta_path = table_stream(table, "VarMeta");
    std::string const var2_path = table_stream(table, "Var2Data");
    std::string const fixed2_path = table_stream(table, "Fixed2Data");
    std::string const fixed2_meta_path = table_stream(table, "Fixed2Meta");
    std::vector<std::uint8_t> meta = doc.read_stream(meta_path);
    std::vector<std::uint8_t> var_meta = doc.read_stream(var_meta_path);
    std::vector<std::uint8_t> var2 = doc.read_stream(var2_path);
    std::vector<std::uint8_t> fixed2 = doc.read_stream(fixed2_path);
    std::vector<std::uint8_t> fixed2_meta = doc.read_stream(fixed2_meta_path);
    if (meta.size() < kFixedMetaHeaderSize ||
        (meta.size() - kFixedMetaHeaderSize) % kFixedMetaEntrySize != 0) {
        throw Error("FixedMeta is " + std::to_string(meta.size()) +
                    " bytes, not 16 + 47*n");
    }
    (void)read_var_entries(var_meta); /* validates the 24 + 12*n shape */
    if (fixed2.size() != kFixed2RecordSize * (row_count + 3)) {
        throw Error("Fixed2Data is " + std::to_string(fixed2.size()) +
                    " bytes for " + std::to_string(row_count) + " rows (expected " +
                    std::to_string(kFixed2RecordSize * (row_count + 3)) + ")");
    }
    if (fixed2_meta.size() < kFixed2MetaHeaderSize ||
        (fixed2_meta.size() - kFixed2MetaHeaderSize) % kFixed2MetaEntrySize != 0) {
        throw Error("Fixed2Meta is " + std::to_string(fixed2_meta.size()) +
                    " bytes, not 16 + 96*n");
    }

    /* W29-0.1: the applied edits also drive the TBkndAssn streams and the
     * 114/Props counters, which are read once after the task loop. */
    struct AppliedEdit {
        PercentActualEdit edit;
        std::size_t row = 0;
        bool manual = false;
        bool has_progress = false;
        bool real_assignment = false;
        std::uint32_t duration = 0;
        std::uint32_t actual = 0;
        /* W29-0.3: the row's stored percent BEFORE this edit; the assignment
         * writer needs the direction (raise vs lower) to reproduce Project's
         * 0F400032 replacement record. */
        std::uint16_t old_percent = 0;
    };
    std::vector<AppliedEdit> applied;
    std::size_t freed_task_bytes = 0;

    /* Edit only the logical entries; the physical tail beyond header +8 belongs
     * to no reader and is preserved verbatim (see merge_logical_var_meta). */
    std::vector<std::uint8_t> const original_var_meta = var_meta;
    std::size_t const physical_entries =
        (var_meta.size() - kVarMetaHeaderSize) / kVarMetaEntrySize;
    std::size_t const declared_entries = get_u32(var_meta, 8);
    if (declared_entries > physical_entries) {
        throw Error("VarMeta declares " + std::to_string(declared_entries) +
                    " entries but holds only " + std::to_string(physical_entries));
    }
    var_meta.resize(kVarMetaHeaderSize + declared_entries * kVarMetaEntrySize);

    std::vector<bool> const tombstones = deleted_rows(meta, row_count);
    Outline const outline = read_outline(doc, table, row_count);

    /* W29-0.2: the task row's +8/+16/+24 doubles and +94 follow the assignment
     * that carries them, so TBkndAssn/FixedData is read BEFORE the task loop.
     * A REAL assignment names a resource that is neither 0 (a project/summary
     * row) nor 0xFFFF0001 (Project's unassigned placeholder); the wave-29 work
     * pair's one real assignment is the only measured case. */
    std::string const assn_table = assn_table_for(table);
    std::string const assn_fixed_path = table_stream(assn_table, "FixedData");
    std::vector<std::uint8_t> assn_fixed = doc.read_stream(assn_fixed_path);
    if (assn_fixed.size() % kAssnStride != 0) {
        throw Error("TBkndAssn/FixedData is " +
                    std::to_string(assn_fixed.size()) + " bytes, not a multiple of " +
                    std::to_string(kAssnStride));
    }
    struct AssignmentWork {
        bool real = false;
        double planned = 0;
    };
    std::vector<AssignmentWork> assignment_work(row_count);
    for (std::size_t at = 0; at < assn_fixed.size() / kAssnStride; ++at) {
        std::size_t const base = at * kAssnStride;
        std::uint32_t const resource = get_u32(assn_fixed, base + kAssnResource);
        if (resource == 0 || resource == kNullResource) {
            continue;
        }
        std::uint32_t const task = get_u32(assn_fixed, base + kAssnTask);
        for (std::size_t row = 1; row < row_count; ++row) {
            if (rows.uid(row) != task) {
                continue;
            }
            assignment_work[row].real = true;
            assignment_work[row].planned +=
                get_f64(assn_fixed, base + kAssnPlanned);
            break;
        }
    }

    /* Streams are built on the copies above and only handed to the document
     * after every edit has been accepted, so a throw leaves `doc` untouched. */
    for (PercentActualEdit const& edit : edits) {
        if (edit.uid == 0) {
            result.warnings.push_back("uid 0 is the project row, not a task");
            continue;
        }
        if (edit.percent > 100) {
            result.warnings.push_back(
                "task uid " + std::to_string(edit.uid) + ": percent " +
                std::to_string(edit.percent) + " is above 100");
            continue;
        }
        std::size_t row = row_count;
        for (std::size_t candidate = 1; candidate < row_count; ++candidate) {
            if (rows.uid(candidate) == edit.uid) {
                row = candidate;
                break;
            }
        }
        if (row == row_count) {
            result.warnings.push_back(
                "task uid " + std::to_string(edit.uid) +
                " is not in the saved task table, so its percent cannot be "
                "written");
            continue;
        }
        if (outline.summary[row]) {
            result.warnings.push_back(
                "task uid " + std::to_string(edit.uid) +
                " is a summary row; its percent is a roll-up");
            continue;
        }
        if (tombstones[row]) {
            result.warnings.push_back(
                "task uid " + std::to_string(edit.uid) +
                " is a deleted row; its percent cannot be written");
            continue;
        }
        if (edit.percent > 0 && !is_date(edit.dates.actual_start)) {
            result.warnings.push_back(
                "task uid " + std::to_string(edit.uid) +
                ": percent " + std::to_string(edit.percent) +
                " needs an actual start date");
            continue;
        }
        if (edit.percent == 100 && !is_date(edit.dates.actual_finish)) {
            result.warnings.push_back(
                "task uid " + std::to_string(edit.uid) +
                ": 100 % needs an actual finish date");
            continue;
        }
        if (edit.percent > 0 && edit.percent < 100 &&
            (!is_date(edit.dates.resume) || !is_date(edit.dates.stop))) {
            result.warnings.push_back(
                "task uid " + std::to_string(edit.uid) + ": percent " +
                std::to_string(edit.percent) +
                " needs working-time resume and stop dates");
            continue;
        }

        std::uint16_t const old_percent =
            rows.u16(row, task_row::kPercentComplete);
        std::uint32_t const duration = rows.u32(row, task_row::kDuration);
        /* W28-1.3: an explicit elapsed value wins over the duration*percent
         * derivation (Mark on Track / Update Project store the working time
         * between the actual start and the date, not a percentage of the
         * duration).  Zero keeps the W28-1.2 shape. */
        std::uint32_t const actual =
            edit.actual_ticks != 0
                ? edit.actual_ticks
                : actual_duration_ticks(duration, edit.percent);
        if (actual > duration) {
            result.warnings.push_back(
                "task uid " + std::to_string(edit.uid) + ": actual duration " +
                std::to_string(actual) + " ticks exceeds the row's duration " +
                std::to_string(duration));
            continue;
        }
        bool const has_progress = edit.percent > 0;
        std::size_t const meta_index = meta_entry_for_row(meta, row);
        if (meta_index == static_cast<std::size_t>(-1)) {
            throw Error("FixedMeta has no entry for FixedData row " +
                        std::to_string(row) + " (uid " +
                        std::to_string(edit.uid) +
                        "), so its metadata cannot be kept");
        }
        std::size_t const meta_base =
            kFixedMetaHeaderSize + meta_index * kFixedMetaEntrySize;
        std::size_t const fixed2_meta_at =
            kFixed2MetaHeaderSize + meta_index * kFixed2MetaEntrySize;
        if (fixed2_meta_at + kFixed2MetaEntrySize > fixed2_meta.size()) {
            throw Error("Fixed2Meta has no entry " + std::to_string(meta_index) +
                        " for task uid " + std::to_string(edit.uid));
        }
        bool const manual =
            (fixed2_meta[fixed2_meta_at + kFixed2MetaFlags] &
             kFixed2MetaFlagManual) != 0;
        /* W29-0.2: the anchor rule below needs the row's saved kind, and the
         * write at the end of this block overwrites it. */
        std::uint8_t const old_kind =
            fixed2_meta[fixed2_meta_at + kFixed2MetaKind];
        bool const derived_actual =
            actual == actual_duration_ticks(duration, edit.percent);
        AssignmentWork const& assignment = assignment_work[row];
        double const task_actual_work =
            assignment.real
                ? (duration == 0
                       ? 0.0
                       : assignment.planned * static_cast<double>(actual) /
                             static_cast<double>(duration))
                : 0.0;
        rows.set_u32(row, task_row::kActualDuration, actual);
        rows.set_u32(row, task_row::kRemainingDuration, duration - actual);
        rows.set_u16(row, task_row::kPercentComplete, edit.percent);
        /* W29-0.2: a row whose assignment names a real resource stores the
         * edit's percent as % Work Complete (the work pair: 0 -> 40); the
         * placeholder assignment keeps the wave-28 rule (100 at 100 %, else 0;
         * the leaf and milestone pairs). */
        rows.set_u16(row, task_row::kPercentWorkComplete,
                     assignment.real ? edit.percent
                                     : (edit.percent == 100 ? 100 : 0));
        if (assignment.real) {
            set_row_f64(rows, row, kTaskWork, assignment.planned);
            set_row_f64(rows, row, kTaskActualWork, task_actual_work);
            set_row_f64(rows, row, kTaskRemainingWork,
                        assignment.planned - task_actual_work);
        }
        rows.set_u32(row, task_row::kActualStart,
                     has_progress ? edit.dates.actual_start : kInvalidDate);
        rows.set_u32(row, task_row::kActualFinish,
                     edit.percent == 100 ? edit.dates.actual_finish
                                         : kInvalidDate);
        rows.set_u32(row, task_row::kResume,
                     has_progress ? edit.dates.resume : kInvalidDate);
        rows.set_u32(row, task_row::kStop,
                     has_progress ? edit.dates.stop : kInvalidDate);
        if (has_progress) {
            if (edit.progress_flags) {
                rows.set_u32(row, task_row::kProgressFlags,
                             rows.u32(row, task_row::kProgressFlags) |
                                 kProgressFlagBits);
            }
            /* W29-0.2: the +174 start anchor is re-pointed only on the shapes
             * that measured it: a manual row (the wave-28 manual pair) or a row
             * whose saved Fixed2Meta kind is the wave-28 FLAT leaf 0x10 (the
             * mark/update controls).  Every wave-29 shape has a different saved
             * kind (0x00/0x14/0x04) and keeps the anchor Project left -- the
             * leaf's 3d4a12c0 and the work row's NA stay byte for byte, so
             * writing ActualStart there would fail the pair. */
            if (manual || old_kind == kFixed2MetaKindLeaf) {
                rows.set_u32(row, task_row::kStartAnchor,
                             edit.dates.actual_start);
            }
        } else {
            rows.set_u32(row, task_row::kStartAnchor, kInvalidDate);
        }
        /* W28-1.3 slack and late-date rules, measured on the mark/update pairs:
         * a row that has started loses its start slack (+186 := 0), and a row
         * at 100 % loses every slack word and stores its actual dates as the
         * late pair (+112/+116 := ActualStart/ActualFinish).  The 83 % manual
         * mark row keeps Free/TotalSlack (9600 each) while StartSlack goes to
         * 0; the 100 % update row goes 14400 -> 0 on all three and its late
         * pair moves from Wed/Fri to the actual Sun/Tue.  A row written at 0 %
         * keeps all of them (auto 100->0 leaves the late pair alone).  These
         * words were already at the measured values on every W28-1.2 percent
         * pair, so setting them there is byte-identical. */
        if (has_progress) {
            rows.set_u32(row, task_row::kStartSlack, 0);
        }
        if (edit.percent == 100) {
            rows.set_u32(row, task_row::kFreeSlack, 0);
            rows.set_u32(row, task_row::kTotalSlack, 0);
            rows.set_u32(row, task_row::kStart3, edit.dates.actual_start);
            rows.set_u32(row, task_row::kFinish3, edit.dates.actual_finish);
            /* W29-0.2: the milestone pair moves Start2/Finish2 (the project
             * start, 3d4612c0) onto the actual dates (3d4d27d8).  The wave-28
             * 100 % rows already store those dates there, so the write is
             * byte-identical for them. */
            rows.set_u32(row, task_row::kStart2, edit.dates.actual_start);
            rows.set_u32(row, task_row::kFinish2, edit.dates.actual_finish);
        }

        /* --- the row's variable fields ---------------------------------- */
        std::size_t added = 0;
        std::size_t removed = 0;
        /* Bytes the props counter 0x01000002 gains: the payloads of the blocks
         * this edit stops referencing.  MEASURED (W29-0.1): a replaced progress
         * block is NOT counted, so its bytes go to a discarded sink. */
        std::size_t discarded = 0;
        std::vector<ProgressStep> steps;
        bool had_progress_block = false;
        std::size_t old_progress_size = 0;
        std::vector<std::uint32_t> const old_offsets =
            find_var_offsets(var_meta, kProgressField, edit.uid);
        if (old_offsets.size() > 1) {
            throw Error("task uid " + std::to_string(edit.uid) + " has " +
                        std::to_string(old_offsets.size()) +
                        " 0x0B40015E blocks; the measured shape is one");
        }
        if (has_progress && !old_offsets.empty()) {
            had_progress_block = true;
            std::vector<std::uint8_t> const old_block =
                read_var_block(var2, old_offsets.front());
            old_progress_size = old_block.size();
            steps = decode_steps(old_block);
        }
        const double step_percent =
            edit.step_percent < 0.0 ? static_cast<double>(edit.percent)
                                    : edit.step_percent;
        /* W29-0.3: Project LOWERS a recorded percent by replacing the stored
         * middle step and rewriting the block where it stands.  MEASURED on
         * the two "before" pairs: [marker, middle 60, final 60] at offset 536
         * becomes [marker, middle 40 (fresh id), final 40] at the SAME offset;
         * VarMeta and the Var2Data size do not move.  A block of any other
         * shape is not a measured lowering and refuses. */
        bool progress_in_place = false;
        std::vector<std::uint8_t> progress_payload;
        if (has_progress) {
            if (had_progress_block && steps.size() > 1 && steps.back().flag == 1) {
                steps.pop_back(); /* the previous "current" step */
            }
            if (steps.empty()) {
                /* A block Project builds from scratch starts with the measured
                 * marker step; 40 -> 100 keeps the marker it already has. */
                ProgressStep marker;
                marker.flag = 1;
                marker.percent = kMarkerPercent;
                marker.id = fresh_step_id(edit.uid, edit.percent, steps);
                steps.push_back(marker);
            }
            bool const lowering = !edit.progress_flags &&
                                  edit.percent < old_percent;
            if (lowering &&
                !(had_progress_block && steps.size() == 2 &&
                  steps.front().flag == 1 && steps.back().flag == 0 &&
                  step_percent < steps.back().percent)) {
                throw Error("task uid " + std::to_string(edit.uid) +
                            ": lowering a recorded percent is measured only "
                            "from a [marker, middle, current] block, not this "
                            "one");
            }
            /* W29-0.3: Project RAISES a recorded percent by appending a new
             * middle step (60 -> 80 is [marker, middle 60, middle 80, final
             * 80]); it LOWERS it by replacing the stored middle (60 -> 40 is
             * [marker, middle 40, final 40]).  Both are Project's own saves
             * of the same 60 % row. */
            if (lowering) {
                steps.pop_back(); /* the stored middle, replaced below */
                ProgressStep middle;
                middle.flag = 0;
                middle.percent = step_percent;
                middle.id = fresh_step_id(edit.uid, edit.percent, steps);
                steps.push_back(middle);
                ProgressStep final_step;
                final_step.flag = 1;
                final_step.percent = step_percent;
                final_step.id = kFinalStepId;
                steps.push_back(final_step);
                progress_payload = encode_steps(steps);
                progress_in_place =
                    progress_payload.size() == old_progress_size;
                if (!progress_in_place) {
                    throw Error("task uid " + std::to_string(edit.uid) +
                                ": lowering the recorded percent changes the "
                                "0x0B40015E block size, which is not a "
                                "measured shape");
                }
            } else {
                ProgressStep middle;
                middle.flag = 0;
                middle.percent = step_percent;
                middle.id = fresh_step_id(edit.uid, edit.percent, steps);
                steps.push_back(middle);
                ProgressStep final_step;
                final_step.flag = 1;
                final_step.percent = step_percent;
                final_step.id = kFinalStepId;
                steps.push_back(final_step);
                progress_payload = encode_steps(steps);
            }
        }
        removed += remove_var_entries(var_meta, var2, edit.uid, kResetField,
                                      freed_task_bytes);
        /* W29-0.2: a 0 % row may also carry the second constant 8-byte block
         * 0x0B4004E3 (the work pair does); it goes at first progress and its
         * payload counts toward the freed-byte word like 4E5's. */
        removed += remove_var_entries(var_meta, var2, edit.uid, kResetFieldAlt,
                                      freed_task_bytes);
        if (has_progress && old_percent == 0) {
            removed += remove_var_entries(var_meta, var2, edit.uid,
                                          kManualFieldA, freed_task_bytes);
            removed += remove_var_entries(var_meta, var2, edit.uid,
                                          kManualFieldB, freed_task_bytes);
        }
        if (!progress_in_place) {
            removed += remove_var_entries(var_meta, var2, edit.uid,
                                          kProgressField,
                                          has_progress ? discarded
                                                       : freed_task_bytes);
        }
        if (has_progress) {
            if (progress_in_place) {
                /* W29-0.3 measured lowering: same payload size, same offset;
                 * the VarMeta entry is not touched at all. */
                put_u32(var2, old_offsets.front(),
                        static_cast<std::uint32_t>(progress_payload.size()));
                std::copy(progress_payload.begin(), progress_payload.end(),
                          var2.begin() +
                              static_cast<std::ptrdiff_t>(old_offsets.front() +
                                                          4));
            } else {
                std::uint32_t const offset =
                    static_cast<std::uint32_t>(var2.size());
                std::vector<std::uint8_t> const block =
                    encode_block(progress_payload);
                var2.insert(var2.end(), block.begin(), block.end());
                insert_var_entry(var_meta, edit.uid, kProgressField, offset);
                ++added;
            }
        } else if (find_var_offsets(var_meta, kResetField, edit.uid).empty()) {
            std::uint32_t const offset = static_cast<std::uint32_t>(var2.size());
            std::vector<std::uint8_t> const payload(std::begin(kResetBlock),
                                                    std::end(kResetBlock));
            std::vector<std::uint8_t> const block = encode_block(payload);
            var2.insert(var2.end(), block.begin(), block.end());
            insert_var_entry(var_meta, edit.uid, kResetField, offset);
            ++added;
        }
        /* W29-0.2: a milestone at 100 % shrinks the project row's 0x0B4004E4
         * project-mode block in place (measured: payload 12 -> 8, freed += 4;
         * the second milestone in a cascade sees 8 bytes and adds nothing). */
        if (duration == 0 && edit.percent == 100) {
            shrink_project_mode_block(var_meta, var2, freed_task_bytes);
        }

        /* FixedMeta +2 is the row's variable-field count.  The measured pairs
         * move it by the NET entry change (manual 0->40: 11 -> 9). */
        std::int64_t const count =
            static_cast<std::int64_t>(get_u16(meta, meta_base + kFixedMetaFieldCount)) +
            static_cast<std::int64_t>(added) - static_cast<std::int64_t>(removed);
        if (count < 0 || count > 0xFFFF) {
            throw Error("FixedMeta field count for uid " +
                        std::to_string(edit.uid) + " would become " +
                        std::to_string(count));
        }
        put_u16(meta, meta_base + kFixedMetaFieldCount,
                static_cast<std::uint16_t>(count));

        /* --- the Task metadata Project rewrites (W29-0.1) --------------- */
        meta[meta_base + kFixedMetaFlag44] = has_progress ? 0x08 : 0x00;
        std::size_t const blob = meta_base + kFixedMetaBlob;
        auto set_blob_bit = [&meta, blob](std::size_t index, std::uint8_t bit,
                                          bool value) {
            if (value) {
                meta[blob + index] |= bit;
            } else {
                meta[blob + index] =
                    static_cast<std::uint8_t>(meta[blob + index] & ~bit);
            }
        };
        /* entry+9 bit 0x40: percent < 100 and the elapsed work is exactly the
         * rounded Duration*percent/100.  Mark on Track's 83 % manual row stores
         * 1200 of 19920 ticks and clears it; its 50 % row (1200 of 1200) sets
         * it. */
        set_blob_bit(1, 0x40, edit.percent < 100 && derived_actual);
        /* entry+17 bit 0x04: automatic rows only, below 100 % with progress.
         * Manual rows keep the byte they had. */
        if (!manual) {
            set_blob_bit(9, 0x04, has_progress && edit.percent < 100);
        }
        /* entry+18 bit 0x20: a percent edit sets it when either side is 100 %;
         * Mark on Track / Update Project clear it. */
        set_blob_bit(10, 0x20,
                     edit.progress_flags &&
                         (old_percent == 100 || edit.percent == 100));

        if (has_progress) {
            fixed2_meta[fixed2_meta_at + kFixed2MetaLeafTail] = static_cast<std::uint8_t>(
                fixed2_meta[fixed2_meta_at + kFixed2MetaLeafTail] &
                ~kFixed2MetaTailClear);
        }
        fixed2_meta[fixed2_meta_at + kFixed2MetaKind] =
            has_progress ? 0x00 : kFixed2MetaKindLeaf;

        std::size_t const fixed2_at = kFixed2RecordSize * (row + 3);
        /* W29-0.2: the wave-29 rows carry +42 values 0x84 / 0x2c / 0x04, so the
         * write is unconditional (the wave-28 0x30/0x01 guard was a false
         * limit, not a measured assertion of the control). */
        fixed2[fixed2_at + kFixed2RecordFlags] =
            has_progress ? kFixed2RecordHasProgress : kFixed2RecordNoProgress;
        std::size_t const start_copy = fixed2_at + kFixed2RecordStartCopy;
        if (edit.percent == 100) {
            put_u32(fixed2, start_copy, kInvalidDate);
        } else if (edit.percent == 0) {
            put_u32(fixed2, start_copy, rows.u32(0, task_row::kStart));
        } else if (!manual) {
            put_u32(fixed2, start_copy, edit.dates.actual_start);
        }

        AppliedEdit done;
        done.edit = edit;
        done.row = row;
        done.manual = manual;
        done.has_progress = has_progress;
        done.real_assignment = assignment.real;
        done.duration = duration;
        done.actual = actual;
        done.old_percent = old_percent;
        applied.push_back(done);
        ++result.rows_written;
    }

    if (result.rows_written == 0) {
        return result;
    }

    std::vector<bool> excluded(row_count, false);
    for (std::size_t row = 1; row < row_count; ++row) {
        excluded[row] = outline.summary[row] || tombstones[row];
    }
    /* W29-0.2: the summary rows above a progressed leaf roll up first; the
     * project row (which takes its Resume/Stop from them) is written after the
     * assignment loop, once the real-assignment work sums are final.
     * write_summary_rollup skips leaf rows that are summaries itself, so it
     * needs only the tombstoned rows -- `excluded` above also marks every
     * summary (the project sum must not charge a summary's roll-up twice). */
    /* W30-0.5 (R29-5, High): the recompute set is the edited rows' ancestor
     * chain only.  The measured roll-up rules have one progressed leaf, so a
     * summary elsewhere in the file must keep Project's stored words rather
     * than take the one-leaf "earliest Resume/Stop" rule. */
    std::vector<bool> rollup_chain(row_count, false);
    for (AppliedEdit const& item : applied) {
        std::size_t row = item.row;
        for (std::size_t guard = 0; guard <= row_count; ++guard) {
            if (row >= outline.parent.size()) {
                break;
            }
            const std::size_t parent = outline.parent[row];
            if (parent == static_cast<std::size_t>(-1) || parent >= row_count ||
                parent == row) {
                break;
            }
            if (outline.summary[parent]) {
                rollup_chain[parent] = true;
            }
            row = parent;
        }
    }
    std::vector<bool> const summary_written =
        write_summary_rollup(rows, outline, tombstones, rollup_chain);

    /* Header words: the entry count and the Var2Data size. */
    std::size_t const entry_count = count_var_entries(var_meta);
    if (var_meta.size() != kVarMetaHeaderSize + entry_count * kVarMetaEntrySize) {
        throw Error("VarMeta lost its 24 + 12*n shape while editing");
    }
    put_u32(var_meta, 8, static_cast<std::uint32_t>(entry_count));
    put_u32(var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(var2.size()));
    std::vector<std::uint8_t> const final_var_meta =
        merge_logical_var_meta(original_var_meta, var_meta);

    /* --- TBkndAssn: the assignment's actual work (W29-0.1) --------------
     * Every measured task row has exactly one assignment (the unassigned
     * placeholder, resource uid via the 0xFFFF sentinel).  The pair moves the
     * assignment's Actual Work (+28), Remaining Work (+44), Resume (+60) and
     * Stop copy (+104); the FixedMeta entry's field count and two flag words;
     * the Fixed2Meta `has actual work` bit; and the two variable blocks
     * 0F400031 (rewritten in place) and 0F400032 (append, replace or remove). */
    std::string const assn_meta_path = table_stream(assn_table, "FixedMeta");
    std::string const assn_fixed2_meta_path =
        table_stream(assn_table, "Fixed2Meta");
    std::string const assn_var_meta_path = table_stream(assn_table, "VarMeta");
    std::string const assn_var2_path = table_stream(assn_table, "Var2Data");
    std::vector<std::uint8_t> assn_meta = doc.read_stream(assn_meta_path);
    std::vector<std::uint8_t> assn_fixed2_meta =
        doc.read_stream(assn_fixed2_meta_path);
    std::vector<std::uint8_t> assn_var_meta = doc.read_stream(assn_var_meta_path);
    std::vector<std::uint8_t> assn_var2 = doc.read_stream(assn_var2_path);
    if (assn_meta.size() < kAssnMetaHeader ||
        (assn_meta.size() - kAssnMetaHeader) % kAssnMetaEntry != 0) {
        throw Error("TBkndAssn/FixedMeta is " + std::to_string(assn_meta.size()) +
                    " bytes, not 16 + 34*n");
    }
    if (assn_fixed2_meta.size() < kFixed2MetaHeaderSize ||
        (assn_fixed2_meta.size() - kFixed2MetaHeaderSize) %
                kAssnFixed2MetaEntry !=
            0) {
        throw Error("TBkndAssn/Fixed2Meta is " +
                    std::to_string(assn_fixed2_meta.size()) +
                    " bytes, not 16 + 53*n");
    }
    (void)read_var_entries(assn_var_meta);
    std::vector<std::uint8_t> const original_assn_var_meta = assn_var_meta;
    std::size_t const assn_physical_entries =
        (assn_var_meta.size() - kVarMetaHeaderSize) / kVarMetaEntrySize;
    std::size_t const assn_declared_entries = get_u32(assn_var_meta, 8);
    if (assn_declared_entries > assn_physical_entries) {
        throw Error("TBkndAssn/VarMeta declares " +
                    std::to_string(assn_declared_entries) + " entries but holds " +
                    std::to_string(assn_physical_entries));
    }
    assn_var_meta.resize(kVarMetaHeaderSize +
                         assn_declared_entries * kVarMetaEntrySize);

    std::size_t freed_assn_bytes = 0;
    /* W29-0.3: a date update that REPLACES 0F400032 does not move Props
     * 0x04000002 (all five new pairs keep the word at 0), while a percent
     * edit's replacement does (wave-28 40 -> 100 counts the 56-byte block,
     * 100 -> 0 counts 76).  So a date update's old payload goes to a
     * discarded sink. */
    std::size_t discarded_assn_bytes = 0;
    for (AppliedEdit const& item : applied) {
        PercentActualEdit const& edit = item.edit;
        std::size_t assn_row = static_cast<std::size_t>(-1);
        for (std::size_t candidate = 0;
             candidate < assn_fixed.size() / kAssnStride; ++candidate) {
            if (get_u32(assn_fixed, candidate * kAssnStride + kAssnTask) ==
                edit.uid) {
                if (assn_row != static_cast<std::size_t>(-1)) {
                    throw Error("task uid " + std::to_string(edit.uid) +
                                " has more than one TBkndAssn row; the measured "
                                "shape is one assignment");
                }
                assn_row = candidate;
            }
        }
        if (assn_row == static_cast<std::size_t>(-1)) {
            throw Error("task uid " + std::to_string(edit.uid) +
                        " has no TBkndAssn row, so its assignment actuals "
                        "cannot be written");
        }
        std::size_t const base = assn_row * kAssnStride;
        std::uint32_t const assn_uid = get_u32(assn_fixed, base + kAssnUid);
        double const planned = get_f64(assn_fixed, base + kAssnPlanned);
        double const units = get_f64(assn_fixed, base + kAssnUnits);
        double const old_actual_work = get_f64(assn_fixed, base + kAssnActual);
        /* W29-0.2: a milestone (Duration 0) has no work to pro-rate -- the
         * milestone pair stores 0 actual and the planned work (0) as remaining
         * -- so the ratio is only computed for a durated row. */
        double const actual_work =
            item.duration == 0
                ? 0.0
                : planned * static_cast<double>(item.actual) /
                      static_cast<double>(item.duration);
        double const remaining_work = planned - actual_work;
        std::uint32_t const remaining_ticks = item.duration - item.actual;
        if (item.actual > 0xFFFFFFFFu / 8u) {
            throw Error("task uid " + std::to_string(edit.uid) +
                        ": actual ticks overflow the 32-bit work word");
        }
        std::uint32_t const ticks8 = item.actual * 8u;

        /* 0F400031: 100 % with a percent edit stores a 44-byte zero header and
         * drops the records (their bytes stay as garbage); 100 % from a date
         * update keeps the records byte for byte and clears the word at +32;
         * 0 % keeps the old record count and rewrites the remaining work into
         * each one.  0 < percent < 100 (W29-0.2) rebuilds one record per
         * started 4800-tick day, and when the rebuilt payload does not fit the
         * old slot it is APPENDED and the entry repointed (the work pair grows
         * 72 -> 100 bytes; the leaf pair's 100-byte block is rewritten in
         * place).  W29-0.3: a date update that LOWERS a recorded percent keeps
         * the old first record as the second entry with the new tick word.
         * A milestone's block is left byte for byte (milestone pair).
         */
        if (item.duration != 0) {
            std::vector<std::uint32_t> const work_offsets =
                find_var_offsets(assn_var_meta, kAssnWorkField, assn_uid);
            if (work_offsets.size() != 1) {
                throw Error("assignment uid " + std::to_string(assn_uid) +
                            " has " + std::to_string(work_offsets.size()) +
                            " 0F400031 blocks; the measured shape is one");
            }
            std::uint32_t const work_offset = work_offsets.front();
            std::vector<std::uint8_t> const old_work =
                read_var_block(assn_var2, work_offset);
            if (old_work.size() < kAssnWorkHeader) {
                throw Error("assignment uid " + std::to_string(assn_uid) +
                            ": 0F400031 is " + std::to_string(old_work.size()) +
                            " bytes, shorter than its 44-byte header");
            }
            if ((old_work.size() - kAssnWorkHeader) % kAssnWorkRecord != 0) {
                throw Error("assignment uid " + std::to_string(assn_uid) +
                            ": 0F400031 is " + std::to_string(old_work.size()) +
                            " bytes, not 44 + 28*n");
            }
            std::vector<std::uint8_t> work;
            if (edit.percent == 100) {
                work.assign(kAssnWorkHeader, 0);
                put_u32(work, 0, kAssnWorkMagic);
                put_u32(work, 4, static_cast<std::uint32_t>(kAssnWorkHeader));
                put_u32(work, 32, edit.progress_flags ? 1u : 0u);
                if (!edit.progress_flags) {
                    work = old_work;
                    put_u32(work, 0, kAssnWorkMagic);
                    put_u32(work, 4, static_cast<std::uint32_t>(kAssnWorkHeader));
                    put_f64(work, 8, 0.0);
                    put_f64(work, 16, 0.0);
                    put_u32(work, 24, 0);
                    put_u32(work, 28, 0);
                    put_u32(work, 32, 0);
                    put_u32(work, 36, 0);
                    put_u32(work, 40, 0);
                }
            } else if (edit.percent == 0) {
                work = old_work;
                put_u32(work, 0, kAssnWorkMagic);
                put_u32(work, 4, static_cast<std::uint32_t>(kAssnWorkHeader));
                put_f64(work, 8, units * 16.0);
                put_f64(work, 16, remaining_work);
                put_u32(work, 24, remaining_ticks * 8u);
                put_u32(work, 28, 0);
                put_u32(work, 32, 1);
                put_u32(work, 36, 0);
                put_u32(work, 40, 0);
                std::size_t const records =
                    (old_work.size() - kAssnWorkHeader) / kAssnWorkRecord;
                for (std::size_t i = 0; i < records; ++i) {
                    std::size_t const at =
                        kAssnWorkHeader + i * kAssnWorkRecord;
                    put_f64(work, at, remaining_work);
                    put_f64(work, at + 8, units * 16.0);
                    put_u32(work, at + 16, 0);
                    put_u32(work, at + 20, 0);
                    put_u32(work, at + 24, remaining_ticks * 8u);
                }
            } else {
                /* W29-0.2: a REAL-resource assignment's contour carries one
                 * record per started 4800-tick day (measured: 9600 ticks ->
                 * 2 records, from a 1-record control); a placeholder keeps
                 * the record count it had (wave-28: 2 at 40 %; the leaf pair:
                 * 2).  The placeholder count is not recomputed, because
                 * Project did not recompute it on any measured pair.
                 * W30-0.6 (R29-6, High): that rule has ONE data point, and only
                 * at the measured point do "one record per 8-hour day" and "an
                 * actual segment plus a remaining segment" agree.  The edit
                 * model refuses every other duration, units and calendar; this
                 * guard is the last line of defence for a hand-built queue.  The
                 * assignment's +12 word is Units x100 (10000.0 = 100 %). */
                std::size_t records =
                    (old_work.size() - kAssnWorkHeader) / kAssnWorkRecord;
                if (item.real_assignment) {
                    constexpr std::uint32_t kMeasuredWorkTicks = 2u * kTicksPerDay;
                    constexpr double kMeasuredUnitsX100 = 10000.0;
                    if (item.duration != kMeasuredWorkTicks ||
                        units != kMeasuredUnitsX100) {
                        throw Error(
                            "assignment uid " + std::to_string(assn_uid) +
                            ": the resource-loaded contour is measured only on "
                            "a 9600-tick task with a 100 % assignment");
                    }
                    records = std::max<std::size_t>(
                        1, static_cast<std::size_t>(
                               (static_cast<std::uint64_t>(item.duration) + 4799u) /
                               4800u));
                }
                work = build_work_block(records, units * 16.0, remaining_work,
                                        remaining_ticks * 8u);
                bool const lowering =
                    !edit.progress_flags && edit.percent < item.old_percent;
                if (lowering) {
                    /* W29-0.3 measured LOWERING (60 -> 40 on the placeholder
                     * assignment): magic gains its low bit, +32 clears, the
                     * header and record 0 carry the new remaining work and
                     * ticks, and record 1 keeps the old remaining work and
                     * units with the NEW tick word.  A real assignment or a
                     * block that is not two records is not measured. */
                    if (item.real_assignment ||
                        old_work.size() !=
                            kAssnWorkHeader + 2 * kAssnWorkRecord) {
                        throw Error(
                            "assignment uid " + std::to_string(assn_uid) +
                            ": lowering a recorded percent is measured only on "
                            "a placeholder 0F400031 of two records");
                    }
                    std::size_t const second =
                        kAssnWorkHeader + kAssnWorkRecord;
                    put_u32(work, 0, kAssnWorkMagic | 1u);
                    put_u32(work, 32, 0);
                    put_f64(work, second, get_f64(old_work, kAssnWorkHeader));
                    put_f64(work, second + 8,
                            get_f64(old_work, kAssnWorkHeader + 8));
                    put_u32(work, second + 24, remaining_ticks * 8u);
                }
            }
            if (edit.percent == 100 || work.size() == old_work.size()) {
                /* In place.  A 100 % percent edit SHORTENS the payload to its
                 * 44-byte header at the same offset; the record bytes past it
                 * stay as Project leaves them (garbage).  The rebuilt 0..99 %
                 * payloads are written in place only when the size is
                 * unchanged. */
                put_u32(assn_var2, work_offset,
                        static_cast<std::uint32_t>(work.size()));
                std::copy(work.begin(), work.end(),
                          assn_var2.begin() +
                              static_cast<std::ptrdiff_t>(work_offset + 4));
            } else {
                std::uint32_t const offset =
                    static_cast<std::uint32_t>(assn_var2.size());
                std::vector<std::uint8_t> const block = encode_block(work);
                assn_var2.insert(assn_var2.end(), block.begin(), block.end());
                repoint_var_entry(assn_var_meta, assn_uid, kAssnWorkField,
                                  offset);
            }
        }

        /* 0F400032: remove at 0 %, append 56 bytes when none existed, replace
         * (append + remove) otherwise.  A 100 % percent edit's replacement
         * carries the old actual work as a second record.  W29-0.3: so does a
         * date update that recomputes a recorded percent, and the second
         * record's shape depends on the direction (raise vs lower). */
        std::vector<std::uint32_t> const actual_offsets =
            find_var_offsets(assn_var_meta, kAssnActualWorkField, assn_uid);
        if (actual_offsets.size() > 1) {
            throw Error("assignment uid " + std::to_string(assn_uid) + " has " +
                        std::to_string(actual_offsets.size()) +
                        " 0F400032 blocks; the measured shape is one");
        }
        bool const had_actual_work = !actual_offsets.empty();
        std::vector<std::uint8_t> old_actual_payload;
        if (had_actual_work) {
            old_actual_payload = read_var_block(assn_var2, actual_offsets.front());
            std::size_t const removed32 = remove_var_entries(
                assn_var_meta, assn_var2, assn_uid, kAssnActualWorkField,
                edit.progress_flags ? freed_assn_bytes
                                    : discarded_assn_bytes);
            if (removed32 != 1) {
                throw Error("assignment uid " + std::to_string(assn_uid) +
                            ": 0F400032 was not removed exactly once");
            }
        }
        if (edit.percent > 0) {
            bool const date_update_replace =
                !edit.progress_flags && edit.percent < 100 &&
                edit.percent != item.old_percent;
            if (had_actual_work &&
                !(edit.percent == 100 && edit.progress_flags) &&
                !date_update_replace) {
                throw Error("assignment uid " + std::to_string(assn_uid) +
                            ": replacing 0F400032 below 100 % (or at 100 % "
                            "from a date update) is not a measured shape");
            }
            std::vector<std::uint8_t> payload(
                kAssnActualHeader + kAssnActualRecord, 0);
            /* W29-0.2: the milestone pair's appended block has u16 +0 = 0 and
             * a zero units word, because the row has no actual work; a row
             * with actual work stores 1 and the assignment's units (the leaf
             * and work pairs). */
            bool const has_actual_work = actual_work > 0.0;
            put_u16(payload, 0, has_actual_work ? 1 : 0);
            put_u16(payload, 2, 24);
            put_u32(payload, 4, static_cast<std::uint32_t>(kAssnActualHeader));
            put_f64(payload, 8, has_actual_work ? units : 0.0);
            put_f64(payload, 16, actual_work);
            put_u32(payload, 24, ticks8);
            put_f64(payload, 36, actual_work);
            put_f64(payload, 44, has_actual_work ? units : 0.0);
            put_u32(payload, 52, ticks8);
            if (had_actual_work) {
                payload.resize(kAssnActualHeader + 2 * kAssnActualRecord);
                /* W29-0.3: RAISING a recorded percent (60 -> 80) stores the
                 * old actual work with units 0 and the NEW tick word;
                 * LOWERING it (60 -> 40) keeps the old record byte for byte as
                 * the second entry.  Both are Project's own saves. */
                bool const lowering =
                    !edit.progress_flags && edit.percent < item.old_percent;
                if (lowering) {
                    if (old_actual_payload.size() <
                        kAssnActualHeader + kAssnActualRecord) {
                        throw Error(
                            "assignment uid " + std::to_string(assn_uid) +
                            ": the 0F400032 being lowered carries no record to "
                            "keep, which is not a measured shape");
                    }
                    std::copy(old_actual_payload.begin() + kAssnActualHeader,
                              old_actual_payload.begin() + kAssnActualHeader +
                                  kAssnActualRecord,
                              payload.begin() + kAssnActualHeader +
                                  kAssnActualRecord);
                } else {
                    put_f64(payload, 56, old_actual_work);
                    put_f64(payload, 64, 0.0);
                    put_u32(payload, 72, ticks8);
                }
            }
            std::uint32_t const offset =
                static_cast<std::uint32_t>(assn_var2.size());
            std::vector<std::uint8_t> const block = encode_block(payload);
            assn_var2.insert(assn_var2.end(), block.begin(), block.end());
            insert_var_entry(assn_var_meta, assn_uid, kAssnActualWorkField,
                             offset);
        }

        put_f64(assn_fixed, base + kAssnActual, actual_work);
        put_f64(assn_fixed, base + kAssnRemaining, remaining_work);
        std::uint32_t const no_progress_date =
            rows.u32(item.row, task_row::kStart);
        put_u32(assn_fixed, base + kAssnResume,
                item.has_progress ? edit.dates.resume : no_progress_date);
        put_u32(assn_fixed, base + kAssnStartCopy,
                item.has_progress ? edit.dates.stop : no_progress_date);

        std::size_t assn_meta_index = static_cast<std::size_t>(-1);
        for (std::size_t i = 0;
             i < (assn_meta.size() - kAssnMetaHeader) / kAssnMetaEntry; ++i) {
            if (get_u32(assn_meta, kAssnMetaHeader + i * kAssnMetaEntry +
                                      kAssnMetaRowOffset) == base) {
                assn_meta_index = i;
                break;
            }
        }
        if (assn_meta_index == static_cast<std::size_t>(-1)) {
            throw Error("TBkndAssn/FixedMeta has no entry for row " +
                        std::to_string(assn_row));
        }
        std::size_t const assn_meta_at =
            kAssnMetaHeader + assn_meta_index * kAssnMetaEntry;
        std::int64_t const assn_count =
            static_cast<std::int64_t>(
                get_u16(assn_meta, assn_meta_at + kAssnMetaFieldCount)) +
            (edit.percent > 0 ? 1 : 0) - (had_actual_work ? 1 : 0);
        if (assn_count < 0 || assn_count > 0xFFFF) {
            throw Error("TBkndAssn field count for uid " +
                        std::to_string(edit.uid) + " would become " +
                        std::to_string(assn_count));
        }
        put_u16(assn_meta, assn_meta_at + kAssnMetaFieldCount,
                static_cast<std::uint16_t>(assn_count));
        assn_meta[assn_meta_at + kAssnMetaWord10] = static_cast<std::uint8_t>(
            (assn_meta[assn_meta_at + kAssnMetaWord10] & ~0x18u) |
            (item.has_progress ? 0x08u : 0u) |
            (edit.percent == 100 ? 0x10u : 0u));
        assn_meta[assn_meta_at + kAssnMetaWord14] = static_cast<std::uint8_t>(
            (assn_meta[assn_meta_at + kAssnMetaWord14] & ~0x04u) |
            (item.has_progress ? 0x04u : 0u));
        std::size_t const assn_f2_meta_at =
            kFixed2MetaHeaderSize + assn_meta_index * kAssnFixed2MetaEntry;
        if (assn_f2_meta_at + kAssnFixed2MetaEntry >
            assn_fixed2_meta.size()) {
            throw Error("TBkndAssn/Fixed2Meta has no entry " +
                        std::to_string(assn_meta_index));
        }
        if (item.has_progress) {
            assn_fixed2_meta[assn_f2_meta_at + kAssnFixed2MetaFlags] =
                static_cast<std::uint8_t>(
                    assn_fixed2_meta[assn_f2_meta_at +
                                     kAssnFixed2MetaFlags] &
                    ~0x01u);
        }
    }

    /* W29-0.2: the project row's +8/+16/+24 doubles and its +94 percent are the
     * REAL assignments' work sums (measured on the work pair:
     * 960000/384000/576000 and 40 %); the project row itself is written now,
     * after every assignment row has its final values, so its Resume/Stop can
     * also take the summary dates write_summary_rollup set above. */
    double real_work = 0.0;
    double real_actual_work = 0.0;
    for (std::size_t at = 0; at < assn_fixed.size() / kAssnStride; ++at) {
        std::size_t const base = at * kAssnStride;
        std::uint32_t const resource = get_u32(assn_fixed, base + kAssnResource);
        if (resource == 0 || resource == kNullResource) {
            continue;
        }
        real_work += get_f64(assn_fixed, base + kAssnPlanned);
        real_actual_work += get_f64(assn_fixed, base + kAssnActual);
    }
    std::uint16_t percent_work = 0;
    if (real_work > 0.0) {
        double const ratio = 100.0 * real_actual_work / real_work;
        percent_work = static_cast<std::uint16_t>(
            std::min(100.0, std::max(0.0, std::floor(ratio + 0.5))));
    }
    bool any_progress = false;
    for (AppliedEdit const& item : applied) {
        any_progress = any_progress || item.has_progress;
    }
    write_project_rollup(rows, excluded, options.project_resume_stop_from_rows,
                         any_progress, summary_written, real_work,
                         real_actual_work, percent_work);

    std::size_t const assn_entry_count = count_var_entries(assn_var_meta);
    if (assn_var_meta.size() !=
        kVarMetaHeaderSize + assn_entry_count * kVarMetaEntrySize) {
        throw Error("TBkndAssn/VarMeta lost its 24 + 12*n shape while editing");
    }
    put_u32(assn_var_meta, 8,
            static_cast<std::uint32_t>(assn_entry_count));
    if (assn_var2.size() > 0xFFFFFFFFu) {
        throw Error("TBkndAssn/Var2Data grew past the 32-bit size word");
    }
    put_u32(assn_var_meta, kVarMetaVar2SizeOffset,
            static_cast<std::uint32_t>(assn_var2.size()));
    std::vector<std::uint8_t> const final_assn_var_meta =
        merge_logical_var_meta(original_assn_var_meta, assn_var_meta);

    /* --- 114/Props counters -------------------------------------------- */
    std::string const props_path = props_path_for(table);
    if (!doc.has_stream(props_path)) {
        throw Error(props_path + " is absent, so the save counters cannot be "
                    "kept");
    }
    std::vector<std::uint8_t> props = doc.read_stream(props_path);
    std::size_t const task_freed_at = props_value_offset(props, kPropTaskFreed);
    std::size_t const task_var2_at =
        props_value_offset(props, kPropTaskVar2Size);
    std::size_t const assn_freed_at = props_value_offset(props, kPropAssnFreed);
    std::size_t const assn_var2_at =
        props_value_offset(props, kPropAssnVar2Size);
    auto accumulated = [](std::uint32_t old, std::size_t added,
                          const char* what) {
        if (added > 0xFFFFFFFFu - old) {
            throw Error(std::string(what) + " counter overflows its 32-bit word");
        }
        return static_cast<std::uint32_t>(old + added);
    };
    if (var2.size() > 0xFFFFFFFFu) {
        throw Error("TBkndTask/Var2Data grew past the 32-bit size word");
    }
    put_u32(props, task_freed_at,
            accumulated(get_u32(props, task_freed_at), freed_task_bytes,
                        "Task freed-byte"));
    put_u32(props, task_var2_at, static_cast<std::uint32_t>(var2.size()));
    put_u32(props, assn_freed_at,
            accumulated(get_u32(props, assn_freed_at), freed_assn_bytes,
                        "Assn freed-byte"));
    put_u32(props, assn_var2_at,
            static_cast<std::uint32_t>(assn_var2.size()));

    /* W29-1.0b: the link table's driving-state byte.  Project's work pair takes
     * the ONE live incoming link to the edited row from 0x0f to 0x07 when that
     * row enters progress; every other measured pair has no live incoming link
     * and stages nothing.  A shape this rule does not cover is refused here,
     * before any stream is stored. */
    std::vector<ProgressLinkEdit> progress_edits;
    progress_edits.reserve(applied.size());
    for (AppliedEdit const& item : applied) {
        ProgressLinkEdit progress;
        progress.uid = item.edit.uid;
        progress.entered_progress =
            item.old_percent == 0 && item.edit.percent != 0;
        progress_edits.push_back(progress);
    }
    ProgressLinks const link_flags =
        stage_progress_link_flags(doc, progress_edits);

    /* W29-1.0b: Project records on every editor save that the file was edited
     * (Props14 record 0x3540000a -> 01 00).  The whole-stream expected failure
     * this replaces could not tell that effect from the save's own noise. */
    write_edit_marker(doc);

    rows.store_into(doc, table);
    doc.replace_stream(meta_path, meta);
    doc.replace_stream(fixed2_path, fixed2);
    doc.replace_stream(fixed2_meta_path, fixed2_meta);
    doc.replace_stream(var2_path, var2);
    doc.replace_stream(var_meta_path, final_var_meta);
    doc.replace_stream(assn_fixed_path, assn_fixed);
    doc.replace_stream(assn_meta_path, assn_meta);
    doc.replace_stream(assn_fixed2_meta_path, assn_fixed2_meta);
    doc.replace_stream(assn_var_meta_path, final_assn_var_meta);
    doc.replace_stream(assn_var2_path, assn_var2);
    doc.replace_stream(props_path, props);
    if (!link_flags.fixed2_meta.empty()) {
        doc.replace_stream("114/TBkndCons/Fixed2Meta", link_flags.fixed2_meta);
    }

    /* W29-1.0b: the project summary's display strings are an edit effect, not
     * per-save noise (the null saves leave DocumentSummaryInformation byte for
     * byte equal to the control).  Rewrite it from the project row the writer
     * just stored; the writer refuses a shape it has not measured. */
    write_project_summary(doc, opened_rows, rows);

    std::string const inconsistent = check_table(doc, table);
    if (!inconsistent.empty()) {
        throw Error("the actuals writer left the task table inconsistent: " +
                    inconsistent);
    }
    return result;
}

std::uint32_t write_project_status_date(CfbDocument& doc,
                                        std::uint32_t packed_date,
                                        std::string_view root) {
    if (!is_date(packed_date)) {
        throw Error("the project status date " + format_date(packed_date) +
                    " is not a writable packed date");
    }
    std::string const path = std::string(root) + "/Props";
    if (!doc.has_stream(path)) {
        throw Error(path + " is absent, so the project status date cannot be "
                    "written");
    }
    std::vector<std::uint8_t> props = doc.read_stream(path);
    if (props.size() < 16) {
        throw Error(path + " is " + std::to_string(props.size()) +
                    " bytes, shorter than the 16-byte props header");
    }
    std::size_t offset = 16;
    while (offset + 12 <= props.size()) {
        const std::uint32_t length = get_u32(props, offset);
        const std::uint32_t id = get_u32(props, offset + 4);
        const std::uint32_t kind = get_u32(props, offset + 8);
        if ((id & 0x00FF0000u) != 0x00400000u) {
            break; /* the record framing has ended */
        }
        if (static_cast<std::uint64_t>(offset) + 12u + length > props.size()) {
            throw Error(path + " has a truncated record 0x" +
                        std::to_string(id) + " (declared " +
                        std::to_string(length) + " bytes)");
        }
        if (id == kStatusDateField) {
            if (kind != kStatusDateKind || length < 4u) {
                throw Error(path + " stores the status date as kind " +
                            std::to_string(kind) + " / " +
                            std::to_string(length) +
                            " bytes, not the measured kind 4 / 4 bytes");
            }
            const std::uint32_t previous = get_u32(props, offset + 12);
            put_u32(props, offset + 12, packed_date);
            doc.replace_stream(path, props);
            return previous;
        }
        offset += 12u + length;
    }
    throw Error(path + " has no status-date record (0x02400045), so the "
                "project status date cannot be written");
}

} /* namespace mppwrite */
