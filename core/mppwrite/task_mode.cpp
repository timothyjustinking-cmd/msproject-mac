/*
 * task_mode.cpp -- the measured automatic -> manual task-mode writer.  The
 * rules and the evidence are in task_mode.h; this file keeps the shape check
 * and the write in one place so both the edit model and the writer validate
 * exactly the same bytes.
 */

#include "task_mode.h"

#include "actuals.h"
#include "edit_marker.h"
#include "table.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace mppwrite {
namespace {

constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

/* TBkndTask FixedMeta: 16-byte header, 47-byte entries, entry +4 names the
 * FixedData row byte offset.  Entry byte 0 is the row state (0 = live,
 * 0x02 = deleted), +26 bit 0x80 the stored summary flag. */
constexpr std::size_t kFixedMetaHeaderSize = 16;
constexpr std::size_t kFixedMetaEntrySize = 47;
constexpr std::size_t kFixedMetaState = 0;
constexpr std::size_t kFixedMetaRowOffset = 4;
constexpr std::size_t kFixedMetaAutomatic17 = 17;
constexpr std::size_t kFixedMetaAutomatic18 = 18;
constexpr std::size_t kFixedMetaSummaryByte = 26;
constexpr std::uint8_t kFixedMetaStateLive = 0x00;
constexpr std::uint8_t kFixedMetaStateDeleted = 0x02;
constexpr std::uint8_t kFixedMetaSummaryFlag = 0x80;
/* MEASURED precondition: an automatic row's entry carries 0x48/0xef; the edit
 * sets bit 0x04 in +17 and clears bit 0x20 in +18. */
constexpr std::uint8_t kAutomatic17 = 0x48;
constexpr std::uint8_t kAutomatic18 = 0xEF;

/* TBkndTask Fixed2Data: 64-byte records, task row r at record r+3, +50/+54/+58
 * the secondary start/finish/duration copies the manual save fills in. */
constexpr std::size_t kFixed2RecordSize = 64;
constexpr std::size_t kFixed2CopyStart = 50;
constexpr std::size_t kFixed2CopyFinish = 54;
constexpr std::size_t kFixed2CopyDuration = 58;

/* TBkndTask Fixed2Meta: 16-byte header, 96-byte entries, paired with FixedMeta
 * by entry index; +4 names the Fixed2Data record offset, +8 bit 0x80 is
 * Project's Manually Scheduled flag. */
constexpr std::size_t kFixed2MetaHeaderSize = 16;
constexpr std::size_t kFixed2MetaEntrySize = 96;
constexpr std::size_t kFixed2MetaRowWord = 4;
constexpr std::size_t kFixed2MetaFlags = 8;
constexpr std::uint8_t kFixed2MetaManualBit = 0x80;
constexpr std::uint8_t kAutomaticFixed2Flags = 0x7F;

/* TBkndTask FixedData words the shape check reads. */
constexpr std::size_t kActualDuration = 80;
constexpr std::size_t kDuration = 84;
constexpr std::size_t kPercentComplete = 92;
constexpr std::size_t kPercentWorkComplete = 94;
constexpr std::size_t kHasChildrenWord = 140;
constexpr std::size_t kParentRowWord = 142;
constexpr std::size_t kOutlineLevelWord = 172;
constexpr std::size_t kStartAnchor = 174;
constexpr std::size_t kCalendarUid = 178;

/* TBkndAssn: 110-byte rows, 34-byte FixedMeta entries, 48-byte Fixed2Data
 * records, 53-byte Fixed2Meta entries; +8 in FixedData is the resource
 * (0xffff0001 = Project's placeholder) and +8 bit 0x80 in Fixed2Meta mirrors
 * the task's scheduling mode.  +20 is a double (planned work, minutes x1000):
 * MEASURED, the placeholder rows store exactly their task's duration there
 * (2400/1440/960 stored minutes for the control's 24000/14400/9600-tick
 * tasks), not zero -- the low u32 word is zero only because the double's low
 * half happens to be, so this reads the whole double. */
constexpr std::size_t kAssnStride = 110;
constexpr std::size_t kAssnUid = 0;
constexpr std::size_t kAssnTask = 4;
constexpr std::size_t kAssnResource = 8;
constexpr std::size_t kAssnPlannedWork = 20;
constexpr std::uint32_t kNullResource = 0xFFFF0001u;
constexpr std::size_t kAssnMetaHeader = 16;
constexpr std::size_t kAssnMetaEntry = 34;
constexpr std::size_t kAssnMetaState = 0;
constexpr std::size_t kAssnMetaRowOffset = 4;
constexpr std::uint8_t kAssnMetaStateLive = 0x00;
constexpr std::size_t kAssnFixed2Record = 48;
constexpr std::size_t kAssnFixed2MetaEntry = 53;
constexpr std::size_t kAssnFixed2MetaRowWord = 4;
constexpr std::size_t kAssnFixed2MetaFlags = 8;
constexpr std::uint8_t kAssnManualBit = 0x80;
constexpr std::uint8_t kAssnAutomaticFlags = 0x4F;
/* TBkndAssn 0x0F400032 is the actual-work block the actuals writer owns; its
 * presence is progress this edit has no pair for. */
constexpr std::uint32_t kAssnActualWorkField = 0x0F400032u;

std::string table_stream(std::string_view table, std::string_view name) {
    return std::string(table) + "/" + std::string(name);
}

/* The sibling assignment table in the same storage ("114/TBkndAssn"). */
std::string assn_table_for(std::string_view table) {
    std::size_t const slash = table.rfind('/');
    if (slash == std::string_view::npos) {
        throw Error("'" + std::string(table) + "' has no storage prefix");
    }
    return std::string(table.substr(0, slash)) + "/TBkndAssn";
}

/* The link table in the same storage ("114/TBkndCons"). */
std::string cons_table_for(std::string_view table) {
    std::size_t const slash = table.rfind('/');
    if (slash == std::string_view::npos) {
        throw Error("'" + std::string(table) + "' has no storage prefix");
    }
    return std::string(table.substr(0, slash)) + "/TBkndCons";
}

/* MEASURED empty link table: FixedData 0 bytes, FixedMeta 16, Fixed2Data 0,
 * Fixed2Meta 16 (the header records only).  A missing or non-empty stream is a
 * linked or hand-built shape this edit has no pair for. */
bool link_table_is_empty(CfbDocument const& doc, std::string_view table) {
    std::string const cons = cons_table_for(table);
    struct StreamSize {
        char const* name;
        std::size_t size;
    };
    static constexpr StreamSize kEmpty[] = {{"FixedData", 0},
                                            {"FixedMeta", 16},
                                            {"Fixed2Data", 0},
                                            {"Fixed2Meta", 16}};
    for (StreamSize const& stream : kEmpty) {
        std::string const path = cons + "/" + stream.name;
        if (!doc.has_stream(path) || doc.read_stream(path).size() != stream.size) {
            return false;
        }
    }
    return true;
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

/* The assignment table's planned work is an 8-byte IEEE double. */
double get_f64(std::vector<std::uint8_t> const& b, std::size_t o) {
    const std::uint64_t bits =
        static_cast<std::uint64_t>(get_u32(b, o)) |
        (static_cast<std::uint64_t>(get_u32(b, o + 4)) << 32);
    double value = 0.0;
    static_assert(sizeof(value) == sizeof(bits),
                  "the assignment work field is an 8-byte double");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void put_u32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

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
    return kNoIndex;
}

std::size_t assn_meta_entry_for_row(std::vector<std::uint8_t> const& meta,
                                    std::size_t row) {
    std::uint32_t const offset = static_cast<std::uint32_t>(row * kAssnStride);
    for (std::size_t i = 0;
         i < (meta.size() - kAssnMetaHeader) / kAssnMetaEntry; ++i) {
        std::size_t const base = kAssnMetaHeader + i * kAssnMetaEntry;
        if (get_u32(meta, base + kAssnMetaRowOffset) == offset) {
            return i;
        }
    }
    return kNoIndex;
}

std::size_t assn_fixed2_meta_entry_for_row(
    std::vector<std::uint8_t> const& meta, std::size_t row) {
    std::uint32_t const offset =
        static_cast<std::uint32_t>(row * kAssnFixed2Record);
    for (std::size_t i = 0;
         i < (meta.size() - kFixed2MetaHeaderSize) / kAssnFixed2MetaEntry; ++i) {
        std::size_t const base =
            kFixed2MetaHeaderSize + i * kAssnFixed2MetaEntry;
        if (get_u32(meta, base + kAssnFixed2MetaRowWord) == offset) {
            return i;
        }
    }
    return kNoIndex;
}

/* The six task streams and the four streams of the EMPTY link table plus the
 * six assignment streams, validated to the geometry every measured file has.
 * A damaged table throws Error; a merely different shape is validate()'s
 * false-with-error. */
struct State {
    State(CfbDocument const& document, std::string_view table);

    CfbDocument const* doc = nullptr;
    std::string table_name;
    TaskTable rows;
    std::string meta_path;
    std::string fixed2_path;
    std::string fixed2_meta_path;
    std::string assn_fixed_path;
    std::string assn_meta_path;
    std::string assn_fixed2_path;
    std::string assn_fixed2_meta_path;
    std::string assn_var_meta_path;
    std::string assn_var2_path;
    std::vector<std::uint8_t> meta;
    std::vector<std::uint8_t> fixed2;
    std::vector<std::uint8_t> fixed2_meta;
    std::vector<std::uint8_t> assn_fixed;
    std::vector<std::uint8_t> assn_meta;
    std::vector<std::uint8_t> assn_fixed2;
    std::vector<std::uint8_t> assn_fixed2_meta;
    std::vector<std::uint8_t> assn_var_meta;
    std::vector<std::uint8_t> assn_var2;
    std::size_t row_count = 0;
    std::size_t assn_row_count = 0;

    bool validate(std::uint32_t uid, TaskModeShape& shape,
                  std::string& error) const;
};

State::State(CfbDocument const& document, std::string_view table)
    : doc(&document),
      table_name(table),
      rows(TaskTable::from_document(document, table)),
      meta_path(table_stream(table, "FixedMeta")),
      fixed2_path(table_stream(table, "Fixed2Data")),
      fixed2_meta_path(table_stream(table, "Fixed2Meta")),
      assn_fixed_path(table_stream(assn_table_for(table), "FixedData")),
      assn_meta_path(table_stream(assn_table_for(table), "FixedMeta")),
      assn_fixed2_path(table_stream(assn_table_for(table), "Fixed2Data")),
      assn_fixed2_meta_path(table_stream(assn_table_for(table), "Fixed2Meta")),
      assn_var_meta_path(table_stream(assn_table_for(table), "VarMeta")),
      assn_var2_path(table_stream(assn_table_for(table), "Var2Data")) {
    row_count = rows.rows();
    if (row_count < 2) {
        throw Error("the task table has no task rows");
    }
    for (std::string const* path :
         {&meta_path, &fixed2_path, &fixed2_meta_path, &assn_fixed_path,
          &assn_meta_path, &assn_fixed2_path, &assn_fixed2_meta_path,
          &assn_var_meta_path, &assn_var2_path}) {
        if (!document.has_stream(*path)) {
            throw Error("the task-mode save needs " + *path +
                        ", which this file does not have");
        }
    }
    meta = document.read_stream(meta_path);
    fixed2 = document.read_stream(fixed2_path);
    fixed2_meta = document.read_stream(fixed2_meta_path);
    assn_fixed = document.read_stream(assn_fixed_path);
    assn_meta = document.read_stream(assn_meta_path);
    assn_fixed2 = document.read_stream(assn_fixed2_path);
    assn_fixed2_meta = document.read_stream(assn_fixed2_meta_path);
    assn_var_meta = document.read_stream(assn_var_meta_path);
    assn_var2 = document.read_stream(assn_var2_path);

    if (meta.size() !=
        kFixedMetaHeaderSize + kFixedMetaEntrySize * (row_count + 3)) {
        throw Error("FixedMeta is " + std::to_string(meta.size()) +
                    " bytes, not 16 + 47*(rows+3)");
    }
    if (fixed2.size() != kFixed2RecordSize * (row_count + 3)) {
        throw Error("Fixed2Data is " + std::to_string(fixed2.size()) +
                    " bytes, not 64*(rows+3)");
    }
    if (fixed2_meta.size() !=
        kFixed2MetaHeaderSize + kFixed2MetaEntrySize * (row_count + 3)) {
        throw Error("Fixed2Meta is " + std::to_string(fixed2_meta.size()) +
                    " bytes, not 16 + 96*(rows+3)");
    }
    /* The task VarMeta/Var2Data are read only for the progress-block rule; a
     * file without them is a shape no Project save has. */
    if (!document.has_stream(table_stream(table, "VarMeta")) ||
        !document.has_stream(table_stream(table, "Var2Data"))) {
        throw Error("the task table has no VarMeta/Var2Data, so the "
                    "progress-block shape cannot be checked");
    }
    (void)read_var_entries(document.read_stream(table_stream(table, "VarMeta")));

    if (assn_fixed.size() % kAssnStride != 0) {
        throw Error("TBkndAssn/FixedData is " + std::to_string(assn_fixed.size()) +
                    " bytes, not 110*n");
    }
    assn_row_count = assn_fixed.size() / kAssnStride;
    if (assn_meta.size() !=
        kAssnMetaHeader + kAssnMetaEntry * assn_row_count) {
        throw Error("TBkndAssn/FixedMeta is " +
                    std::to_string(assn_meta.size()) + " bytes, not 16 + 34*" +
                    std::to_string(assn_row_count));
    }
    if (assn_fixed2.size() != kAssnFixed2Record * assn_row_count) {
        throw Error("TBkndAssn/Fixed2Data is " +
                    std::to_string(assn_fixed2.size()) + " bytes, not 48*" +
                    std::to_string(assn_row_count));
    }
    if (assn_fixed2_meta.size() !=
        kFixed2MetaHeaderSize + kAssnFixed2MetaEntry * assn_row_count) {
        throw Error("TBkndAssn/Fixed2Meta is " +
                    std::to_string(assn_fixed2_meta.size()) +
                    " bytes, not 16 + 53*" + std::to_string(assn_row_count));
    }
    (void)read_var_entries(assn_var_meta);
}

/* One live task row must be the measured top-level, unlinked, unprogressed
 * leaf with the project calendar.  `reason` names the first difference. */
bool check_task_row(State const& state, std::size_t row, std::string& reason) {
    std::uint32_t const uid = state.rows.uid(row);
    std::string const label = "task uid " + std::to_string(uid);
    std::size_t const entry = meta_entry_for_row(state.meta, row);
    if (entry == kNoIndex) {
        reason = "Task FixedMeta has no entry for " + label;
        return false;
    }
    std::size_t const at = kFixedMetaHeaderSize + entry * kFixedMetaEntrySize;
    if (state.meta[at + kFixedMetaState] == kFixedMetaStateDeleted) {
        reason = label + " is a deleted row; the measured task-mode pair has "
                          "only live task rows";
        return false;
    }
    if (state.meta[at + kFixedMetaState] != kFixedMetaStateLive) {
        reason = label + " has FixedMeta state 0x" +
                 std::to_string(state.meta[at + kFixedMetaState]) +
                 ", not a live row";
        return false;
    }
    if ((state.meta[at + kFixedMetaSummaryByte] & kFixedMetaSummaryFlag) != 0) {
        reason = label + " is a summary; the measured task-mode pair's rows "
                          "are all top-level leaves";
        return false;
    }
    if (state.rows.u16(row, kHasChildrenWord) != 0) {
        reason = label + " has children; the measured task-mode pair's rows "
                          "are all top-level leaves";
        return false;
    }
    if (state.rows.u16(row, kOutlineLevelWord) != 1) {
        reason = label + " is at outline level " +
                 std::to_string(state.rows.u16(row, kOutlineLevelWord)) +
                 "; the measured task-mode pair's rows are all top-level";
        return false;
    }
    if (state.rows.u16(row, kParentRowWord) != 0) {
        reason = label + " names a parent row; the measured task-mode pair's "
                          "rows are all top-level";
        return false;
    }
    if (state.rows.u32(row, kDuration) == 0) {
        reason = label + " has no duration (a milestone); the measured "
                          "task-mode pair's rows all have a duration";
        return false;
    }
    if (!is_date(state.rows.u32(row, task_row::kStart)) ||
        !is_date(state.rows.u32(row, task_row::kFinish))) {
        reason = label + " has no valid start or finish";
        return false;
    }
    if (state.rows.u32(row, kActualDuration) != 0 ||
        state.rows.u16(row, kPercentComplete) != 0 ||
        state.rows.u16(row, kPercentWorkComplete) != 0 ||
        is_date(state.rows.u32(row, task_row::kActualStart)) ||
        is_date(state.rows.u32(row, task_row::kActualFinish)) ||
        is_date(state.rows.u32(row, task_row::kResume)) ||
        is_date(state.rows.u32(row, task_row::kStop))) {
        reason = label + " records progress; the measured task-mode pair has "
                          "no progress anywhere in the file";
        return false;
    }
    if (task_has_progress_block(*state.doc, uid, state.table_name)) {
        reason = label + " carries a 0x0B40015E progress step block; the "
                          "measured task-mode pair has no progress anywhere";
        return false;
    }
    if (state.rows.u32(row, kCalendarUid) != 0xFFFFFFFFu) {
        reason = label + " uses a task calendar; the measured task-mode pair "
                          "uses the project calendar";
        return false;
    }
    return true;
}

bool State::validate(std::uint32_t uid, TaskModeShape& shape,
                     std::string& error) const {
    if (uid == 0) {
        error = "uid 0 is the project row, not a task that can change mode";
        return false;
    }
    std::size_t row = kNoIndex;
    for (std::size_t candidate = 1; candidate < row_count; ++candidate) {
        if (rows.uid(candidate) == uid) {
            row = candidate;
            break;
        }
    }
    if (row == kNoIndex) {
        error = "task uid " + std::to_string(uid) +
                " is not in the saved task table";
        return false;
    }

    /* The measured pair's link table is empty.  A linked file needs Project's
     * CPM to decide whether the mode change reschedules anything, and this
     * port has no scheduler (the reverse pair is the evidence). */
    if (!link_table_is_empty(*doc, table_name)) {
        error = "the file has link rows; the measured task-mode pair has an "
                "empty link table";
        return false;
    }

    for (std::size_t candidate = 1; candidate < row_count; ++candidate) {
        std::string reason;
        if (!check_task_row(*this, candidate, reason)) {
            error = std::move(reason);
            return false;
        }
    }

    /* Exactly one live placeholder assignment per task, no tombstoned or extra
     * rows, and no assignment-side actual work.  The same shape every measured
     * control file has. */
    if (assn_row_count != row_count - 1) {
        error = "the assignment table has " + std::to_string(assn_row_count) +
                " rows for " + std::to_string(row_count - 1) +
                " tasks; the measured task-mode pair has exactly one "
                "placeholder assignment per task";
        return false;
    }
    std::vector<bool> task_has_assignment(row_count, false);
    for (std::size_t assn_row = 0; assn_row < assn_row_count; ++assn_row) {
        std::size_t const base = assn_row * kAssnStride;
        std::uint32_t const assn_uid = get_u32(assn_fixed, base + kAssnUid);
        std::uint32_t const task_uid = get_u32(assn_fixed, base + kAssnTask);
        std::string const label =
            "assignment uid " + std::to_string(assn_uid) + " (task uid " +
            std::to_string(task_uid) + ")";
        std::size_t const entry = assn_meta_entry_for_row(assn_meta, assn_row);
        if (entry == kNoIndex) {
            error = "TBkndAssn FixedMeta has no entry for " + label;
            return false;
        }
        std::size_t const meta_at =
            kAssnMetaHeader + entry * kAssnMetaEntry;
        if (assn_meta[meta_at + kAssnMetaState] != kAssnMetaStateLive) {
            error = label + " is deleted; the measured task-mode pair has only "
                            "live placeholder assignments";
            return false;
        }
        if (get_u32(assn_fixed, base + kAssnResource) != kNullResource) {
            error = label + " does not name Project's 0xffff0001 placeholder "
                            "resource";
            return false;
        }
        std::size_t task_row = kNoIndex;
        for (std::size_t candidate = 1; candidate < row_count; ++candidate) {
            if (rows.uid(candidate) == task_uid) {
                task_row = candidate;
                break;
            }
        }
        if (task_row == kNoIndex) {
            error = label + " names no live task row";
            return false;
        }
        if (get_f64(assn_fixed, base + kAssnPlannedWork) !=
            static_cast<double>(rows.u32(task_row, kDuration)) * 100.0) {
            error = label + " stores planned work that is not its task's "
                            "duration; the measured task-mode pair's placeholder "
                            "assignments carry exactly the task duration";
            return false;
        }
        if (!find_var_offsets(assn_var_meta, kAssnActualWorkField, assn_uid)
                 .empty()) {
            error = label + " records actual work; the measured task-mode pair "
                            "has no progress anywhere";
            return false;
        }
        if (task_has_assignment[task_row]) {
            error = "task uid " + std::to_string(task_uid) +
                    " has more than one assignment row; the measured "
                    "task-mode pair has exactly one per task";
            return false;
        }
        task_has_assignment[task_row] = true;
    }
    for (std::size_t candidate = 1; candidate < row_count; ++candidate) {
        if (!task_has_assignment[candidate]) {
            error = "task uid " + std::to_string(rows.uid(candidate)) +
                    " has no assignment row; the measured task-mode pair has "
                    "one placeholder assignment per task";
            return false;
        }
    }

    /* The edited row's measured precondition bytes. */
    std::size_t const entry = meta_entry_for_row(meta, row);
    std::size_t const meta_at = kFixedMetaHeaderSize + entry * kFixedMetaEntrySize;
    /* THE COPY RULE applies to the EDITED row only: the measured fixture's
     * already-manual rows 2 and 3 keep a calculated pair (+96/+100) that
     * differs from their effective pair (+104/+108), so the whole-file check
     * above must not refuse them.  The measured pair's edited automatic row
     * stores the effective pair in every copy, so writing +104/+108/+84 into
     * the +174/Fixed2Data slots reproduces Project's bytes. */
    if (rows.u32(row, task_row::kStart) !=
        rows.u32(row, task_row::kStart2)) {
        error = "task uid " + std::to_string(uid) +
                ": its calculated start " +
                format_date(rows.u32(row, task_row::kStart)) +
                " differs from its effective start " +
                format_date(rows.u32(row, task_row::kStart2)) +
                "; the measured pair stores the effective pair in every copy "
                "of the edited row";
        return false;
    }
    if (rows.u32(row, task_row::kFinish) !=
        rows.u32(row, task_row::kFinish2)) {
        error = "task uid " + std::to_string(uid) +
                ": its calculated finish " +
                format_date(rows.u32(row, task_row::kFinish)) +
                " differs from its effective finish " +
                format_date(rows.u32(row, task_row::kFinish2)) +
                "; the measured pair stores the effective pair in every copy "
                "of the edited row";
        return false;
    }
    if (meta[meta_at + kFixedMetaAutomatic17] != kAutomatic17 ||
        meta[meta_at + kFixedMetaAutomatic18] != kAutomatic18) {
        error = "task uid " + std::to_string(uid) +
                ": its FixedMeta entry stores 0x" +
                std::to_string(meta[meta_at + kFixedMetaAutomatic17]) + "/0x" +
                std::to_string(meta[meta_at + kFixedMetaAutomatic18]) +
                " at +17/+18, not the measured automatic row's 0x48/0xef";
        return false;
    }
    std::size_t const f2m_at = kFixed2MetaHeaderSize + entry * kFixed2MetaEntrySize;
    if (get_u32(fixed2_meta, f2m_at + kFixed2MetaRowWord) !=
        static_cast<std::uint32_t>(kFixed2RecordSize * (row + 3))) {
        error = "task uid " + std::to_string(uid) +
                ": its Fixed2Meta entry does not name Fixed2Data record " +
                std::to_string(row + 3);
        return false;
    }
    if (fixed2_meta[f2m_at + kFixed2MetaFlags] != kAutomaticFixed2Flags) {
        if ((fixed2_meta[f2m_at + kFixed2MetaFlags] & kFixed2MetaManualBit) !=
            0) {
            error = "task uid " + std::to_string(uid) +
                    " is already manually scheduled; only the measured "
                    "automatic -> manual direction has a Project pair";
        } else {
            error = "task uid " + std::to_string(uid) +
                    ": its Fixed2Meta +8 is 0x" +
                    std::to_string(fixed2_meta[f2m_at + kFixed2MetaFlags]) +
                    ", not the measured automatic row's 0x7f";
        }
        return false;
    }
    if (rows.u32(row, kStartAnchor) != kInvalidDate) {
        error = "task uid " + std::to_string(uid) +
                " already stores a start anchor (" +
                format_date(rows.u32(row, kStartAnchor)) +
                "); the measured automatic row stores NA";
        return false;
    }
    std::size_t const f2_at = kFixed2RecordSize * (row + 3);
    if (get_u32(fixed2, f2_at + kFixed2CopyStart) != kInvalidDate ||
        get_u32(fixed2, f2_at + kFixed2CopyFinish) != kInvalidDate ||
        get_u32(fixed2, f2_at + kFixed2CopyDuration) != 0) {
        error = "task uid " + std::to_string(uid) +
                ": its Fixed2Data copies at +50/+54/+58 are not the measured "
                "automatic row's NA/NA/0";
        return false;
    }
    if (rows.u32(row, task_row::kStart3) != rows.u32(row, task_row::kStart) ||
        rows.u32(row, task_row::kFinish3) != rows.u32(row, task_row::kFinish)) {
        error = "task uid " + std::to_string(uid) +
                ": its third date pair (+112/+116) does not repeat the "
                "calculated pair; the measured automatic row repeats it";
        return false;
    }
    std::size_t const assn_row = [&]() {
        for (std::size_t candidate = 0; candidate < assn_row_count; ++candidate) {
            if (get_u32(assn_fixed, candidate * kAssnStride + kAssnTask) ==
                uid) {
                return candidate;
            }
        }
        return kNoIndex;
    }();
    if (assn_row == kNoIndex) {
        error = "task uid " + std::to_string(uid) + " has no assignment row";
        return false;
    }
    std::size_t const assn_f2m = assn_fixed2_meta_entry_for_row(
        assn_fixed2_meta, assn_row);
    if (assn_f2m == kNoIndex) {
        error = "TBkndAssn Fixed2Meta has no entry for task uid " +
                std::to_string(uid);
        return false;
    }
    std::size_t const assn_f2m_at =
        kFixed2MetaHeaderSize + assn_f2m * kAssnFixed2MetaEntry;
    if (assn_fixed2_meta[assn_f2m_at + kAssnFixed2MetaFlags] !=
        kAssnAutomaticFlags) {
        if ((assn_fixed2_meta[assn_f2m_at + kAssnFixed2MetaFlags] &
             kAssnManualBit) != 0) {
            error = "task uid " + std::to_string(uid) +
                    "'s assignment is already manual; only the measured "
                    "automatic -> manual direction has a Project pair";
        } else {
            error = "task uid " + std::to_string(uid) +
                    "'s assignment Fixed2Meta +8 is 0x" +
                    std::to_string(
                        assn_fixed2_meta[assn_f2m_at + kAssnFixed2MetaFlags]) +
                    ", not the measured automatic row's 0x4f";
        }
        return false;
    }

    shape.uid = uid;
    shape.duration_ticks = rows.u32(row, kDuration);
    shape.start = rows.u32(row, task_row::kStart2);
    shape.finish = rows.u32(row, task_row::kFinish2);
    shape.calculated_start = rows.u32(row, task_row::kStart);
    shape.calculated_finish = rows.u32(row, task_row::kFinish);
    return true;
}

} /* namespace */

bool read_task_mode_shape(CfbDocument const& doc, std::uint32_t uid,
                          TaskModeShape& shape, std::string& error,
                          std::string_view table) {
    shape = TaskModeShape{};
    error.clear();
    try {
        State state(doc, table);
        return state.validate(uid, shape, error);
    } catch (Error const& caught) {
        error = caught.what();
        return false;
    }
}

TaskModeWriteResult write_task_mode(CfbDocument& doc,
                                   std::vector<TaskModeEdit> const& edits,
                                   std::string_view table) {
    TaskModeWriteResult result;
    if (edits.empty()) {
        return result;
    }
    if (edits.size() != 1) {
        throw Error("the task-mode writer measured one row per save; " +
                    std::to_string(edits.size()) + " edits were given");
    }
    State state(doc, table);
    TaskModeShape shape;
    std::string error;
    if (!state.validate(edits[0].uid, shape, error)) {
        result.warnings.push_back(error);
        return result;
    }

    std::size_t const row = state.rows.row_for_uid(shape.uid);
    std::size_t const entry = meta_entry_for_row(state.meta, row);
    if (entry == kNoIndex) {
        throw Error("the task-mode writer lost the row's FixedMeta entry");
    }
    std::size_t const meta_at = kFixedMetaHeaderSize + entry * kFixedMetaEntrySize;
    state.meta[meta_at + kFixedMetaAutomatic17] |=
        static_cast<std::uint8_t>(0x04);
    state.meta[meta_at + kFixedMetaAutomatic18] &=
        static_cast<std::uint8_t>(~0x20u);

    std::size_t const f2m_at =
        kFixed2MetaHeaderSize + entry * kFixed2MetaEntrySize;
    state.fixed2_meta[f2m_at + kFixed2MetaFlags] |=
        static_cast<std::uint8_t>(kFixed2MetaManualBit);

    state.rows.set_u32(row, kStartAnchor, shape.start);
    std::size_t const f2_at = kFixed2RecordSize * (row + 3);
    put_u32(state.fixed2, f2_at + kFixed2CopyStart, shape.start);
    put_u32(state.fixed2, f2_at + kFixed2CopyFinish, shape.finish);
    put_u32(state.fixed2, f2_at + kFixed2CopyDuration, shape.duration_ticks);

    std::size_t assn_row = kNoIndex;
    for (std::size_t candidate = 0; candidate < state.assn_row_count;
         ++candidate) {
        if (get_u32(state.assn_fixed,
                    candidate * kAssnStride + kAssnTask) == shape.uid) {
            assn_row = candidate;
            break;
        }
    }
    if (assn_row == kNoIndex) {
        throw Error("the task-mode writer lost the row's assignment");
    }
    std::size_t const assn_f2m =
        assn_fixed2_meta_entry_for_row(state.assn_fixed2_meta, assn_row);
    if (assn_f2m == kNoIndex) {
        throw Error("the task-mode writer lost the assignment's Fixed2Meta "
                    "entry");
    }
    state.assn_fixed2_meta[kFixed2MetaHeaderSize +
                           assn_f2m * kAssnFixed2MetaEntry +
                           kAssnFixed2MetaFlags] |=
        static_cast<std::uint8_t>(kAssnManualBit);

    write_edit_marker(doc);

    state.rows.store_into(doc, table);
    doc.replace_stream(state.meta_path, state.meta);
    doc.replace_stream(state.fixed2_path, state.fixed2);
    doc.replace_stream(state.fixed2_meta_path, state.fixed2_meta);
    doc.replace_stream(state.assn_fixed2_meta_path, state.assn_fixed2_meta);

    std::string const inconsistent = check_table(doc, table);
    if (!inconsistent.empty()) {
        throw Error("the task-mode writer left the task table inconsistent: " +
                    inconsistent);
    }
    ++result.rows_written;
    return result;
}

} /* namespace mppwrite */
