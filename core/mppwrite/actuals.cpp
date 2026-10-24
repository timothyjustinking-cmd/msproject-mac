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

/* The four variable fields Project's percent save touches (measured, W28-1.1).
 * 0x0B40015E is the progress-step block; 0x0B4004E5 is the constant 8-byte
 * block a 0 % row keeps; 0x0B400505/0x0B400506 are the two 2-byte manual
 * fields a 0 % manual row carries and loses when it first gets progress. */
constexpr std::uint32_t kProgressField = 0x0B40015Eu;
constexpr std::uint32_t kResetField = 0x0B4004E5u;
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

/* Remove every VarMeta entry for (mid, field); return how many went.  Matching
 * is exact on mid: the writer only ever creates and owns mid == uid entries for
 * these fields, so a 0x80000000|uid family entry is not silently swallowed. */
std::size_t remove_var_entries(std::vector<std::uint8_t>& var_meta,
                               std::uint32_t mid, std::uint32_t field_id) {
    std::vector<VarEntry> const entries = read_var_entries(var_meta);
    std::size_t removed = 0;
    for (std::size_t i = entries.size(); i-- > 0;) {
        if (entries[i].mid == mid && entries[i].field_id == field_id) {
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

/* Which task rows are summaries or tombstones.  Summaries follow Fixed2Data's
 * parent links, as core/mpp does (reader.cpp read_outline): a row that some
 * other row names as its parent has children, and its own Duration/Actual
 * Duration are roll-ups, not work.  PROJECT_A's stored has-children flag
 * disagrees with the links on some rows, so the links win. */
std::vector<bool> summary_rows(CfbDocument const& doc, std::string_view table,
                               std::size_t rows) {
    std::vector<bool> summary(rows, false);
    std::string const fixed2_path = table_stream(table, "Fixed2Data");
    if (!doc.has_stream(fixed2_path)) {
        return summary;
    }
    std::vector<std::uint8_t> const fixed2 = doc.read_stream(fixed2_path);
    if (fixed2.size() != kFixed2RecordSize * (rows + 3)) {
        return summary; /* damaged outline: treat the table as flat */
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
            summary[row] = true;
        }
        RowId const parent = read_id(record(row), kFixed2RecordRowOffset);
        if (parent == no_id) {
            continue;
        }
        auto const found = row_by_id.find(parent);
        if (found != row_by_id.end() && found->second != row) {
            summary[found->second] = true;
        }
    }
    return summary;
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

/* Recompute the project row from the leaf rows.  Measured on all four W28-1.2
 * pairs and both W28-1.3 mark pairs:
 * PercentComplete = 100 * sum(ActualDuration) / sum(Duration) over the
 * non-summary, non-deleted task rows; ActualDuration = project Duration *
 * that percent / 100; Remaining = project Duration - ActualDuration; and
 * ActualStart = the project start (+104) while any task has progress, NA
 * otherwise.  Resume/Stop are the project start too, except in Update
 * Project's mode (`from_rows`), where they are the earliest valid per-row
 * resume and stop -- MEASURED on wave28-update.mpp: project Resume = Stop =
 * Tue 12:00, the row-3 finish, while rows 1 and 2 stop Tue 17:00 and resume
 * Wed 08:00.  PercentWorkComplete and the flag/anchor words are never touched
 * on the project row. */
void write_project_rollup(TaskTable& table, std::vector<bool> const& excluded,
                          bool resume_stop_from_rows) {
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
    if (total_actual > 0) {
        std::uint32_t const start = table.u32(0, task_row::kStart2);
        table.set_u32(0, task_row::kActualStart, start);
        std::uint32_t resume = start;
        std::uint32_t stop = start;
        if (resume_stop_from_rows) {
            bool have_resume = false;
            bool have_stop = false;
            for (std::size_t row = 1; row < table.rows(); ++row) {
                if (row < excluded.size() && excluded[row]) {
                    continue;
                }
                const std::uint32_t row_resume = table.u32(row, task_row::kResume);
                if (is_date(row_resume) && (!have_resume || row_resume < resume)) {
                    resume = row_resume;
                    have_resume = true;
                }
                const std::uint32_t row_stop = table.u32(row, task_row::kStop);
                if (is_date(row_stop) && (!have_stop || row_stop < stop)) {
                    stop = row_stop;
                    have_stop = true;
                }
            }
            /* No measurable row date (a table whose progress rows carry no
             * resume/stop has no measured pair): keep the project start rather
             * than writing NA under a positive total. */
            if (!have_resume) {
                resume = start;
            }
            if (!have_stop) {
                stop = start;
            }
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
    std::size_t const row_count = rows.rows();

    std::string const meta_path = table_stream(table, "FixedMeta");
    std::string const var_meta_path = table_stream(table, "VarMeta");
    std::string const var2_path = table_stream(table, "Var2Data");
    std::vector<std::uint8_t> meta = doc.read_stream(meta_path);
    std::vector<std::uint8_t> var_meta = doc.read_stream(var_meta_path);
    std::vector<std::uint8_t> var2 = doc.read_stream(var2_path);
    if (meta.size() < kFixedMetaHeaderSize ||
        (meta.size() - kFixedMetaHeaderSize) % kFixedMetaEntrySize != 0) {
        throw Error("FixedMeta is " + std::to_string(meta.size()) +
                    " bytes, not 16 + 47*n");
    }
    (void)read_var_entries(var_meta); /* validates the 24 + 12*n shape */

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

    std::vector<bool> const summaries = summary_rows(doc, table, row_count);
    std::vector<bool> const tombstones = deleted_rows(meta, row_count);

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
        if (summaries[row]) {
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
        rows.set_u32(row, task_row::kActualDuration, actual);
        rows.set_u32(row, task_row::kRemainingDuration, duration - actual);
        rows.set_u16(row, task_row::kPercentComplete, edit.percent);
        rows.set_u16(row, task_row::kPercentWorkComplete,
                     edit.percent == 100 ? 100 : 0);
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
            rows.set_u32(row, task_row::kStartAnchor,
                         edit.dates.actual_start);
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
        }

        /* --- the row's variable fields ---------------------------------- */
        std::size_t added = 0;
        std::size_t removed = 0;
        std::vector<ProgressStep> steps;
        bool had_progress_block = false;
        if (has_progress) {
            std::vector<std::uint32_t> const old_offsets =
                find_var_offsets(var_meta, kProgressField, edit.uid);
            if (!old_offsets.empty()) {
                had_progress_block = true;
                steps = decode_steps(read_var_block(var2, old_offsets.front()));
            }
        }
        removed += remove_var_entries(var_meta, edit.uid, kResetField);
        if (has_progress && old_percent == 0) {
            removed += remove_var_entries(var_meta, edit.uid, kManualFieldA);
            removed += remove_var_entries(var_meta, edit.uid, kManualFieldB);
        }
        removed += remove_var_entries(var_meta, edit.uid, kProgressField);
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
            const double step_percent =
                edit.step_percent < 0.0 ? static_cast<double>(edit.percent)
                                        : edit.step_percent;
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
            std::uint32_t const offset = static_cast<std::uint32_t>(var2.size());
            std::vector<std::uint8_t> const block = encode_block(encode_steps(steps));
            var2.insert(var2.end(), block.begin(), block.end());
            insert_var_entry(var_meta, edit.uid, kProgressField, offset);
            ++added;
        } else if (find_var_offsets(var_meta, kResetField, edit.uid).empty()) {
            std::uint32_t const offset = static_cast<std::uint32_t>(var2.size());
            std::vector<std::uint8_t> const payload(std::begin(kResetBlock),
                                                    std::end(kResetBlock));
            std::vector<std::uint8_t> const block = encode_block(payload);
            var2.insert(var2.end(), block.begin(), block.end());
            insert_var_entry(var_meta, edit.uid, kResetField, offset);
            ++added;
        }

        /* FixedMeta +2 is the row's variable-field count.  The measured pairs
         * move it by the NET entry change (manual 0->40: 11 -> 9). */
        std::size_t const meta_index = meta_entry_for_row(meta, row);
        if (meta_index == static_cast<std::size_t>(-1)) {
            throw Error("FixedMeta has no entry for FixedData row " +
                        std::to_string(row) + " (uid " +
                        std::to_string(edit.uid) +
                        "), so its variable-field count cannot be kept");
        }
        std::size_t const meta_base =
            kFixedMetaHeaderSize + meta_index * kFixedMetaEntrySize;
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
        ++result.rows_written;
    }

    if (result.rows_written == 0) {
        return result;
    }

    std::vector<bool> excluded(row_count, false);
    for (std::size_t row = 1; row < row_count; ++row) {
        excluded[row] = summaries[row] || tombstones[row];
    }
    write_project_rollup(rows, excluded,
                         options.project_resume_stop_from_rows);

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

    rows.store_into(doc, table);
    doc.replace_stream(meta_path, meta);
    doc.replace_stream(var2_path, var2);
    doc.replace_stream(var_meta_path, final_var_meta);

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
