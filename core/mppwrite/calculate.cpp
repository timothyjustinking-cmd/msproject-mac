/*
 * calculate.cpp -- the measured Calculate Project save for the unlinked
 * two-leaf shape.  The rules and the evidence are in calculate.h.
 */

#include "calculate.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <map>
#include <system_error>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "actuals.h"
#include "edit_marker.h"
#include "props_edit.h"
#include "row_writer.h"
#include "summary_info.h"
#include "table.h"
#include "task_mode.h"
#include "varmeta_edit.h"

namespace mppwrite {

/* W35d-1.3 (R35cG2-1): the test-only fault seam of save_calculate (calculate.h).  0 is off. */
static std::size_t g_fault_after_appends = 0;

void set_calculate_fault_after_appends(std::size_t appends) {
    g_fault_after_appends = appends;
}

/* W35d-2.7 (R35cG2-6): the test-only fault seam of the scratch-path probe (calculate.h). */
static std::filesystem::file_type g_scratch_probe_type = std::filesystem::file_type::none;
static std::error_code g_scratch_probe_fault;

void set_calculate_scratch_probe_fault(std::filesystem::file_type type, std::error_code fault) {
    g_scratch_probe_type = type;
    g_scratch_probe_fault = fault;
}

bool calculate_measured_finish(std::uint32_t start, std::uint32_t duration,
                               std::uint32_t& finish) {
    constexpr int kDayStartSeconds = 8 * 3600;
    constexpr int kDayFinishSeconds = 17 * 3600;
    constexpr std::uint32_t kDayTicks = 4800; /* 8 hours */
    constexpr int kSaturday = 0;              /* day 0 (1983-12-31) was a Saturday */
    constexpr int kSunday = 1;
    if (!is_date(start) || duration == 0 || duration % kDayTicks != 0) {
        return false;
    }
    PackedDate const first = unpack_date(start);
    int const weekday = first.day % 7;
    if (first.seconds_of_day != kDayStartSeconds || weekday == kSaturday ||
        weekday == kSunday) {
        return false;
    }
    std::uint32_t remaining = duration / kDayTicks - 1;
    if (remaining > 0xFFFFu) {
        return false;
    }
    PackedDate last = first;
    while (remaining > 0) {
        ++last.day;
        int const next = last.day % 7;
        if (next != kSaturday && next != kSunday) {
            --remaining;
        }
    }
    if (last.day > 0xFFFF) {
        return false;
    }
    last.seconds_of_day = kDayFinishSeconds;
    finish = pack_date(last);
    return true;
}

bool calculate_measured_late(std::uint32_t finish, std::uint32_t duration,
                             std::uint32_t project_finish, std::uint32_t& slack,
                             std::uint32_t& late_start) {
    constexpr int kDayStartSeconds = 8 * 3600;
    constexpr int kDayFinishSeconds = 17 * 3600;
    constexpr std::uint32_t kDayTicks = 4800; /* 8 hours */
    constexpr int kSaturday = 0;
    constexpr int kSunday = 1;
    if (!is_date(finish) || !is_date(project_finish) || duration == 0 ||
        duration % kDayTicks != 0) {
        return false;
    }
    PackedDate const first = unpack_date(finish);
    PackedDate last = unpack_date(project_finish);
    if (first.seconds_of_day != kDayFinishSeconds ||
        last.seconds_of_day != kDayFinishSeconds || last.day < first.day ||
        first.day % 7 == kSaturday || first.day % 7 == kSunday ||
        last.day % 7 == kSaturday || last.day % 7 == kSunday) {
        return false;
    }
    /* the working days after the leaf's finish up to the project finish */
    std::uint32_t days = 0;
    for (int day = first.day + 1; day <= last.day; ++day) {
        if (day % 7 != kSaturday && day % 7 != kSunday) {
            ++days;
        }
    }
    /* the leaf's late start: its duration's working days back from the project finish */
    std::uint32_t remaining = duration / kDayTicks - 1;
    if (remaining > 0xFFFFu) {
        return false;
    }
    while (remaining > 0) {
        --last.day;
        if (last.day < 0) {
            return false;
        }
        if (last.day % 7 != kSaturday && last.day % 7 != kSunday) {
            --remaining;
        }
    }
    last.seconds_of_day = kDayStartSeconds;
    slack = days * kDayTicks;
    late_start = pack_date(last);
    return true;
}

bool calculate_measured_first_working_start(std::uint32_t props_start,
                                            std::uint32_t& first) {
    constexpr int kDayStartSeconds = 8 * 3600;
    constexpr int kDayFinishSeconds = 17 * 3600;
    constexpr int kSaturday = 0; /* day 0 (1983-12-31) was a Saturday */
    constexpr int kSunday = 1;
    if (!is_date(props_start)) {
        return false;
    }
    PackedDate at = unpack_date(props_start);
    /* unpack_date multiplies any 16-bit tick count by six, so a time word of 24:00 or later
     * would count as "at or after 17:00" and map to a real working day (R35cG0-5). */
    if (at.seconds_of_day >= 86400) {
        return false;
    }
    bool const working_day = at.day % 7 != kSaturday && at.day % 7 != kSunday;
    if (working_day && at.seconds_of_day >= kDayStartSeconds &&
        at.seconds_of_day < kDayFinishSeconds) {
        first = props_start;
        return true;
    }
    /* before 08:00 on a working day, the same day; otherwise the next working day */
    if (!(working_day && at.seconds_of_day < kDayStartSeconds)) {
        do {
            ++at.day;
        } while (at.day % 7 == kSaturday || at.day % 7 == kSunday);
    }
    if (at.day > 0xFFFF) {
        return false;
    }
    at.seconds_of_day = kDayStartSeconds;
    first = pack_date(at);
    return true;
}

namespace {

constexpr char const* kTable = "114/TBkndTask";
constexpr std::size_t kRows = 3; /* the project row and two leaves */
constexpr std::uint32_t kNoDate = 0xFFFFFFFFu;

/* FixedData words this save moves (row_writer.h names the rest). */
constexpr std::size_t kFreeSlack = 74;
constexpr std::size_t kStartSlack = 186;
constexpr std::size_t kTotalSlack = 190;

/* 114/Props records (ids stored little-endian, 0x40 in the third byte). */
constexpr std::uint32_t kPropProjectStart = 0x02400002u;
constexpr std::uint32_t kPropProjectFinish = 0x02400003u;
constexpr std::uint32_t kPropCalculationPending = 0x02400024u;
constexpr std::uint32_t kPropsRecordKindDate = 4;
constexpr std::uint32_t kPropsRecordKindFlag = 2;

/* Fixed2Data stamps (record = row + 3). */
constexpr std::size_t kFixed2Stamp = 42;
constexpr std::size_t kFixed2ProjectDuration = 58;

/* FixedMeta: a task row's entry is the one whose +4 word is the row's FixedData
 * offset (task_row_offset), as task_mode.cpp resolves it; the entry's position is
 * not assumed (R35G1-12). */
constexpr std::size_t kFixedMetaHeader = 16;
constexpr std::size_t kFixedMetaEntry = 47;
constexpr std::size_t kFixedMetaRowOffset = 4;
constexpr std::size_t kMetaPresenceLow = 18;
constexpr std::size_t kMetaPresenceHigh = 19;
constexpr std::size_t kNoMetaEntry = static_cast<std::size_t>(-1);

std::uint32_t get_u32(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

/* The byte position of the FixedMeta entry for a task row, or kNoMetaEntry. */
std::size_t fixed_meta_entry_at(std::vector<std::uint8_t> const& meta,
                                std::size_t row) {
    if (meta.size() < kFixedMetaHeader) {
        return kNoMetaEntry;
    }
    std::uint32_t const offset =
        static_cast<std::uint32_t>(task_row_offset(row));
    for (std::size_t at = kFixedMetaHeader;
         at + kFixedMetaEntry <= meta.size(); at += kFixedMetaEntry) {
        if (get_u32(meta, at + kFixedMetaRowOffset) == offset) {
            return at;
        }
    }
    return kNoMetaEntry;
}

void put_u32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[o + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

std::string hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08X", value);
    return text;
}

/* Offset of the PAYLOAD of the one 114/Props record with this id, or SIZE_MAX
 * when absent.  The chain is [u32 length][u32 id][u32 kind][payload] from
 * offset 16; a duplicate is a shape no file has. */
std::size_t props_payload(std::vector<std::uint8_t> const& props,
                          std::uint32_t id, std::uint32_t& length_out) {
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
            if (found != static_cast<std::size_t>(-1)) {
                throw Error("114/Props holds more than one record for " +
                            hex(id));
            }
            found = at + 12;
            length_out = length;
        }
        at += 12u + length;
    }
    return found;
}

bool fail(std::string& error, std::string const& message) {
    error = message;
    return false;
}

/* W35c-0.3 (R35bG0-2): the project's calculation options (multiple critical paths, the
 * critical-slack limit, minutes per day and per week, schedule from) live in 114/Props, and
 * no pair varies them.  The stream is pinned to the measured control's: the same records
 * in the same order, each with the control's id, kind, length and payload, and the same
 * opaque tail.  scripts/gen_calculate_props_pin.py writes the table from the control. */
struct PropsPinRecord {
    std::uint32_t id;
    std::uint32_t kind;
    std::uint32_t length;
    std::uint64_t payload_hash; /* FNV-1a 64 */
};

#include "calculate_props_pin.inc"

std::uint64_t fnv1a64(std::uint8_t const* data, std::size_t size) {
    std::uint64_t value = 0xCBF29CE484222325ULL;
    for (std::size_t i = 0; i < size; ++i) {
        value ^= data[i];
        value *= 0x100000001B3ULL;
    }
    return value;
}

/* Records whose payload the save or a measured relation moves, so the payload is not pinned
 * (id, kind and length still are): the project finish and the pending flag, which the save
 * writes; the project start, which 0.2 relates to the leaf Start; and the two GUIDs the
 * control's null save changes (per-save noise). */
bool props_payload_is_free(std::uint32_t id) {
    return id == kPropProjectFinish || id == kPropCalculationPending ||
           id == kPropProjectStart || id == 0x02400029u || id == 0x024013C2u;
}

void check_props_pin(std::vector<std::uint8_t> const& props) {
    constexpr std::size_t kCount = sizeof kPropsPin / sizeof kPropsPin[0];
    std::string const differs = " differs from the measured control";
    if (props.size() < 16 || get_u32(props, 8) != kPropsPinHeaderWord8 ||
        get_u32(props, 12) != kPropsPinHeaderWord12) {
        throw Error("114/Props header" + differs);
    }
    std::size_t at = 16;
    std::size_t index = 0;
    while (at + 12 <= props.size()) {
        std::uint32_t const length = get_u32(props, at);
        std::uint32_t const id = get_u32(props, at + 4);
        if ((id & 0x00FF0000u) != 0x00400000u) {
            break;
        }
        if (static_cast<std::uint64_t>(at) + 12u + length > props.size()) {
            throw Error("a 114/Props record runs past the stream");
        }
        if (index >= kCount) {
            throw Error("114/Props record " + hex(id) + " (the " +
                        std::to_string(index + 1) + "th) is past the " +
                        std::to_string(kCount) + " records of the measured control: "
                        "the stream" + differs);
        }
        PropsPinRecord const& want = kPropsPin[index];
        if (id != want.id || get_u32(props, at + 8) != want.kind || length != want.length) {
            throw Error("114/Props record " + std::to_string(index) + " is " + hex(id) +
                        " (kind " + std::to_string(get_u32(props, at + 8)) + ", " +
                        std::to_string(length) + " bytes), not the control's " + hex(want.id) +
                        " (kind " + std::to_string(want.kind) + ", " +
                        std::to_string(want.length) + " bytes): the stream" + differs);
        }
        if (!props_payload_is_free(id) && fnv1a64(props.data() + at + 12, length) != want.payload_hash) {
            /* W35d-1.4 (R35cG0-3): the pin also holds per-file identity records (the project
             * title at 0x02400008, a creation stamp at 0x02401394), so the text must not say
             * the differing record is an option. */
            throw Error("114/Props record " + hex(id) + differs +
                        " (an option, a default or a per-file value that no measured Calculate "
                        "Project pair varies; Calculate Project is enabled for the measured "
                        "control's values only)");
        }
        at += 12u + length;
        ++index;
    }
    if (index != kCount) {
        throw Error("114/Props holds " + std::to_string(index) + " records, the measured "
                    "control holds " + std::to_string(kCount) + ": the stream" + differs);
    }
    /* The opaque tail: the control's, except the one scalar the save itself rewrites (tag
     * 0x01000100, the Task Var2Data size, which follows the stored names and values). */
    std::vector<std::uint8_t> tail(props.begin() + static_cast<std::ptrdiff_t>(at), props.end());
    if (tail.size() != kPropsPinTailBytes) {
        throw Error("114/Props tail is " + std::to_string(tail.size()) + " bytes, the control's "
                    "is " + std::to_string(kPropsPinTailBytes) + ": the tail" + differs);
    }
    std::size_t key_at = 0;
    std::size_t keys = 0;
    for (std::size_t i = 0; i + 16 <= tail.size(); i += 16) {
        if (get_u32(tail, i) == 4 && get_u32(tail, i + 8) == 4 &&
            ((static_cast<std::uint32_t>(tail[i + 4]) << 24) |
             (static_cast<std::uint32_t>(tail[i + 5]) << 16) |
             (static_cast<std::uint32_t>(tail[i + 6]) << 8) | tail[i + 7]) == 0x01000100u) {
            key_at = i + 12;
            ++keys;
        }
    }
    if (keys != 1) {
        throw Error("114/Props tail holds " + std::to_string(keys) +
                    " Task size scalars, not one: the tail" + differs);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        tail[key_at + i] = 0;
    }
    if (fnv1a64(tail.data(), tail.size()) != kPropsPinTailHash) {
        throw Error("114/Props tail" + differs + " (a scalar other than the Task size the "
                    "save writes has another value)");
    }
}

std::string props_path() {
    return kPropsBackEnd;
}

/* W35b-0.5 (R35G1-3): the parameters the pair does not vary.  Every table the
 * save leaves untouched is pinned to the measured control's shape, so a file
 * that differs in a way no pair measured is refused rather than saved with
 * values the port cannot derive. */
constexpr std::size_t kRscHeader = 48;
constexpr std::size_t kRscRow = 172;
constexpr std::size_t kCalHeader = 64;
constexpr std::size_t kCalRow = 12;
constexpr std::size_t kF2MetaHeader = 16;
constexpr std::size_t kF2MetaEntry = 96;
constexpr std::size_t kF2MetaRowWord = 4;
constexpr std::size_t kF2MetaFlags = 8;
constexpr std::size_t kF2Record = 64;
constexpr std::uint8_t kF2MetaActive = 0x40;
constexpr std::uint32_t kCalNameKey = 0x0D400001u;

bool is_baseline_key(std::uint32_t key) {
    switch (key) {
        /* Task work, cost, duration, start, finish. */
        case 0x0B400001u: case 0x0B400006u: case 0x0B40001Bu:
        case 0x0B40002Bu: case 0x0B40002Cu:
        /* Resource work, cost. */
        case 0x0C40000Fu: case 0x0C400011u:
        /* Assignment work, cost, start, finish. */
        case 0x0F400010u: case 0x0F400020u: case 0x0F400092u: case 0x0F400093u:
            return true;
        default:
            return false;
    }
}

/* A table's VarMeta: no baseline block, and per uid exactly the measured keys
 * (a note, calendar, deadline, task-type or resource block changes the shape).
 * `zero` is the key set of uid 0 when it has its own, `row` that of every other
 * uid; `groups` is how many uids store anything. */
void check_var_keys(CfbDocument const& doc, std::string const& table,
                    std::string const& label,
                    std::vector<std::uint32_t> const* zero,
                    std::vector<std::uint32_t> const& row, std::size_t groups) {
    std::vector<VarEntry> const entries =
        read_var_entries(doc.read_stream("114/" + table + "/VarMeta"));
    std::map<std::uint32_t, std::vector<std::uint32_t>> by_uid;
    for (VarEntry const& entry : entries) {
        if (is_baseline_key(entry.field_id)) {
            throw Error(label + " uid " + std::to_string(entry.mid) +
                        " carries a baseline (" + hex(entry.field_id) +
                        "); the measured Calculate Project save has none");
        }
        by_uid[entry.mid].push_back(entry.field_id);
    }
    for (auto& [uid, keys] : by_uid) {
        std::sort(keys.begin(), keys.end());
        std::vector<std::uint32_t> const& want = (uid == 0 && zero != nullptr) ? *zero : row;
        if (keys != want) {
            throw Error(label + " uid " + std::to_string(uid) +
                        " stores variable records other than the measured "
                        "ones; the measured Calculate Project save stores "
                        "only the keys of its control");
        }
    }
    if (by_uid.size() != groups) {
        throw Error(label + " table stores variable records for " +
                    std::to_string(by_uid.size()) + " rows; the measured "
                    "Calculate Project save stores them for " +
                    std::to_string(groups));
    }
}

std::vector<std::uint32_t> const kProjectTaskKeys = {
    0x0B40000Eu, 0x0B4004E4u, 0x0B400563u, 0x0B400564u};
std::vector<std::uint32_t> const kLeafTaskKeys = {
    0x0B40000Eu, 0x0B4000A4u, 0x0B4000A5u, 0x0B4000A8u, 0x0B4000A9u,
    0x0B4000AAu, 0x0B4004E5u, 0x0B400563u, 0x0B400564u};
std::vector<std::uint32_t> const kPlaceholderAssignmentKeys = {
    0x0F400031u, 0x0F40027Au, 0x0F400299u};
std::vector<std::uint32_t> const kNullResourceKeys = {0x0C4002D6u, 0x0C4002E3u};

void check_unvaried_parameters(CfbDocument const& doc, TaskTable const& rows) {
    /* The Resource Sheet holds only the null row; the assignments name
     * Project's placeholder. */
    std::size_t const rsc_size = doc.read_stream("114/TBkndRsc/FixedData").size();
    if (rsc_size != kRscHeader + kRscRow) {
        throw Error("the resource table has " +
                    std::to_string(rsc_size >= kRscHeader
                                       ? (rsc_size - kRscHeader) / kRscRow
                                       : 0) +
                    " rows (" + std::to_string(rsc_size) +
                    " bytes); the measured Calculate Project save has only "
                    "Project's null resource row, no resources on the "
                    "Resource Sheet");
    }
    /* The Standard calendar with no working-time or exception block of its own. */
    std::size_t const cal_size = doc.read_stream("114/TBkndCal/FixedData").size();
    std::vector<VarEntry> const cal_entries =
        read_var_entries(doc.read_stream("114/TBkndCal/VarMeta"));
    bool standard = cal_size == kCalHeader + 2 * kCalRow &&
                    cal_entries.size() == 1 && cal_entries[0].mid == 1 &&
                    cal_entries[0].field_id == kCalNameKey;
    if (standard) {
        std::vector<std::uint8_t> const name = read_var_block(
            doc.read_stream("114/TBkndCal/Var2Data"), cal_entries[0].offset);
        static std::vector<std::uint8_t> const want = {
            'S', 0, 't', 0, 'a', 0, 'n', 0, 'd', 0, 'a', 0, 'r', 0, 'd', 0, 0, 0};
        standard = name == want;
    }
    if (!standard) {
        throw Error("the calendar table is not the measured Standard calendar "
                    "alone (named Standard, with no working-time or exception "
                    "block of its own); the measured Calculate Project save "
                    "has no other calendar");
    }
    check_var_keys(doc, "TBkndTask", "task", &kProjectTaskKeys, kLeafTaskKeys, kRows);
    check_var_keys(doc, "TBkndAssn", "assignment", nullptr,
                   kPlaceholderAssignmentKeys, kRows - 1);
    check_var_keys(doc, "TBkndRsc", "resource", &kNullResourceKeys, kNullResourceKeys, 1);
    /* Active tasks with the measured FixedMeta flags (summary, effort-driven,
     * task-type and presence bits), which the pair never varies. */
    std::vector<std::uint8_t> const f2_meta =
        doc.read_stream(std::string(kTable) + "/Fixed2Meta");
    std::vector<std::uint8_t> const meta =
        doc.read_stream(std::string(kTable) + "/FixedMeta");
    for (std::size_t row = 1; row < kRows; ++row) {
        std::size_t const at = fixed_meta_entry_at(meta, row);
        if (at == kNoMetaEntry) {
            throw Error("Task FixedMeta has no entry for task uid " +
                        std::to_string(rows.uid(row)));
        }
        /* W35c-2.9 (R35bG4-3): the Fixed2Meta entry is the one at the FixedMeta entry's
         * index, as task_mode.cpp pairs them, not the one at the row's position. */
        std::size_t const f2_at =
            kF2MetaHeader + ((at - kFixedMetaHeader) / kFixedMetaEntry) * kF2MetaEntry;
        /* W35d-2.6 (R35cG2-3): as task_mode.cpp does, the entry must also name this row's
         * Fixed2Data record (row + 3), or the uid blamed below rests on an unchecked pairing. */
        if (f2_meta.size() >= f2_at + kF2MetaEntry &&
            get_u32(f2_meta, f2_at + kF2MetaRowWord) !=
                static_cast<std::uint32_t>(kF2Record * (row + 3))) {
            throw Error("task uid " + std::to_string(rows.uid(row)) +
                        ": its Fixed2Meta entry does not name Fixed2Data record " +
                        std::to_string(row + 3));
        }
        if (f2_meta.size() < f2_at + kF2MetaEntry ||
            (f2_meta[f2_at + kF2MetaFlags] & kF2MetaActive) == 0) {
            throw Error("task uid " + std::to_string(rows.uid(row)) +
                        " is inactive; the measured Calculate Project save "
                        "has active tasks only");
        }
        int const differs = calculate_leaf_meta_mismatch(meta.data() + at, kFixedMetaEntry);
        if (differs >= 0) {
            throw Error("task uid " + std::to_string(rows.uid(row)) +
                        "'s FixedMeta metadata flags differ from the "
                        "measured entry at byte " + std::to_string(differs) +
                        " (summary, effort-driven, task-type and presence "
                        "bits)");
        }
    }
}

} /* namespace */

int calculate_leaf_meta_mismatch(std::uint8_t const* entry, std::size_t size) {
    /* Bytes 4-7 are the row's offset.  Bytes 8-11 are e3 ?? fd fe in the control, its null save
     * and the Project-calculated file, with byte 9 ff on the lengthened leaf and bf on the
     * other (the save does not change either), so byte 9 takes either value.  Bytes 12-15 are
     * a per-file stamp (the project row's differs from the leaves'), the only bytes the
     * measured entries do not constrain. */
    static std::uint8_t const kLeafMeta[kFixedMetaEntry] = {
        0x00, 0x00, 0x09, 0x00, 0, 0, 0, 0, 0xE3, 0x00, 0xFD, 0xFE, 0, 0, 0, 0,
        0x07, 0x48, 0xEF, 0x1F, 0x10};
    if (entry == nullptr || size != kFixedMetaEntry) {
        return 0;
    }
    for (std::size_t i = 0; i < kFixedMetaEntry; ++i) {
        if ((i >= 4 && i < 8) || (i >= 12 && i < 16)) {
            continue; /* the row's offset; the per-file stamp */
        }
        if (i == 9) {
            if (entry[i] != 0xBF && entry[i] != 0xFF) {
                return static_cast<int>(i);
            }
            continue;
        }
        if (entry[i] != kLeafMeta[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<std::uint32_t> const& calculate_leaf_var_keys() {
    return kLeafTaskKeys;
}

bool read_calculate_shape(CfbDocument const& doc, CalculateShape& shape,
                          std::string& error) {
    shape = CalculateShape{};
    error.clear();
    try {
        TaskTable const rows = TaskTable::from_document(doc, kTable);
        if (rows.rows() != kRows) {
            return fail(error, "the file has " + std::to_string(rows.rows()) +
                                   " task rows; the measured Calculate Project "
                                   "save has the project row and exactly two "
                                   "leaf tasks");
        }
        if (rows.uid(0) != 0) {
            return fail(error, "row 0 is not the project row");
        }
        /* Whole-file shape: unlinked (empty link table), automatic, top-level
         * leaves with no progress, no task calendar, one placeholder
         * assignment per task, no manual rows.  A linked, lagged or
         * constrained file is refused here (the link table is non-empty) or
         * by the constraint check below. */
        std::string reason;
        if (!read_task_file_shape(doc, reason, kTable, "Calculate Project pair")) {
            return fail(error, reason);
        }
        /* W35c-2.3 (R35bG0-8): the loops below start at the first leaf.  A Deadline (or a
         * constraint) can be set on the project summary task, and whether Project carries it
         * into the leaves' late dates is not measured, so row 0 is checked too. */
        if (is_date(rows.u32(0, task_row::kDeadline))) {
            return fail(error, "the project summary task carries a deadline; the measured "
                               "Calculate Project save has none");
        }
        /* W35d-2.5 (R35cG2-2): 33 of the 332 committed fixtures store 1 at row 0 +64 and are no
         * constraint pairs, so the word is not shown to be a constraint the user set.  The text
         * states what is read, not what it means. */
        if (rows.u16(0, task_row::kConstraintType) != 0) {
            return fail(error, "the project row's ConstraintType word (+64) is not 0, as it is "
                               "in the measured Calculate Project save; the word is not shown "
                               "to be a constraint the user set");
        }
        std::vector<std::uint8_t> const meta =
            doc.read_stream(std::string(kTable) + "/FixedMeta");
        for (std::size_t row = 1; row < kRows; ++row) {
            std::size_t const entry_at = fixed_meta_entry_at(meta, row);
            if (entry_at == kNoMetaEntry) {
                return fail(error, "Task FixedMeta has no entry for task uid " +
                                       std::to_string(rows.uid(row)));
            }
            if (task_is_manually_scheduled(doc, row, kTable)) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " is manually scheduled; the measured "
                                       "Calculate Project save has automatic "
                                       "rows only");
            }
            if (meta[entry_at + 17] != 0x48 || meta[entry_at + 18] != 0xEF) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " does not store the automatic row's "
                                       "0x48/0xef at FixedMeta +17/+18");
            }
            if (rows.u16(row, task_row::kConstraintType) != 0) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " carries a constraint; the measured "
                                       "Calculate Project save has none");
            }
            /* W35b-0.2 (R35G1-1): a Deadline limits the backward pass; the pair
             * has none and the port's slack and late dates ignore it. */
            if (is_date(rows.u32(row, task_row::kDeadline))) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " carries a deadline; the measured "
                                       "Calculate Project save has none");
            }
            /* W35b-0.3 (R35G1-2): the writer stamps Fixed2Data +46 with the Props
             * project start, which is right only for a leaf that starts at the
             * project start; the linked and lag pairs store the predecessor-finish
             * bound there. */
            if (rows.u32(row, task_row::kStart) != rows.u32(0, task_row::kStart)) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " starts at a date other than the project "
                                       "start; the measured Calculate Project "
                                       "save has every leaf starting at the "
                                       "project start");
            }
            shape.leaf_uids.push_back(rows.uid(row));
        }
        /* W35b-2.7 (RW35-W3): the leaf stamp is the measured 0x30 only for the measured
         * arrangement, one leaf ending the project and it the first leaf row.  Equal leaf
         * finishes are a tie no pair measures (the ordering below would hand it to the later
         * row), so they are refused before the critical leaf is picked. */
        if (rows.u32(1, task_row::kFinish) == rows.u32(2, task_row::kFinish)) {
            return fail(error, "task uid " + std::to_string(rows.uid(1)) + " and task uid " +
                                   std::to_string(rows.uid(2)) +
                                   " finish together; the measured Calculate Project save "
                                   "has one leaf ending the project");
        }
        std::uint32_t latest = 0;
        for (std::size_t row = 1; row < kRows; ++row) {
            std::uint32_t const finish = rows.u32(row, task_row::kFinish);
            if (finish >= latest) {
                latest = finish;
                shape.critical_uid = rows.uid(row);
                shape.critical_duration = rows.u32(row, task_row::kDuration);
            }
        }
        shape.project_start = rows.u32(0, task_row::kStart);
        shape.project_finish = latest;
        std::vector<std::uint8_t> const props = doc.read_stream(props_path());
        std::uint32_t length = 0;
        std::size_t const flag =
            props_payload(props, kPropCalculationPending, length);
        if (flag == static_cast<std::size_t>(-1) || length != 2) {
            return fail(error, "114/Props has no 0x02400024 calculation-"
                               "pending record, so the file cannot be a "
                               "pending recalculation");
        }
        if (props[flag] != 1 || props[flag + 1] != 0) {
            return fail(error, "the calculation is not pending (114/Props "
                               "0x02400024 is not 01 00); there is nothing to "
                               "calculate");
        }
        /* W35b-2.3 (R35G1-6): the measured save is the FIRST calculation of an
         * automatically built file.  A file that already holds the five overtime
         * records on its project row was calculated before (by Project or by this
         * port) and edited again; Project's repeat save is unmeasured, and this
         * writer's append would collide with them, so it is refused up front.  (A
         * calculated file that is not pending is refused above, as not pending.) */
        for (VarEntry const& entry :
             read_var_entries(doc.read_stream(std::string(kTable) + "/VarMeta"))) {
            if (entry.mid == 0 && (entry.field_id == kTaskActualOvertimeWorkFieldId ||
                                   entry.field_id == kTaskRemainingOvertimeWorkFieldId ||
                                   entry.field_id == kTaskOvertimeCostFieldId ||
                                   entry.field_id == kTaskActualOvertimeCostFieldId ||
                                   entry.field_id == kTaskRemainingOvertimeCostFieldId)) {
                return fail(error, "the project row already holds the overtime record " +
                                       hex(entry.field_id) +
                                       ", so the file has been calculated before; the "
                                       "measured Calculate Project save is the first "
                                       "calculation of an automatically built file");
            }
        }
        /* W35b-2.1 (R35G1-4): the two other Props records the save writes are
         * checked here, before any write, so no append can land in a target that
         * a later Props edit would then refuse. */
        std::uint32_t finish_length = 0;
        std::size_t const finish_at =
            props_payload(props, kPropProjectFinish, finish_length);
        if (finish_at == static_cast<std::size_t>(-1) || finish_length != 4 ||
            get_u32(props, finish_at - 4) != kPropsRecordKindDate) {
            return fail(error, "114/Props has no 4-byte date record for the "
                               "project finish (0x02400003), so the save cannot "
                               "move it");
        }
        std::uint32_t start_length = 0;
        std::size_t const start_at =
            props_payload(props, kPropProjectStart, start_length);
        if (start_at == static_cast<std::size_t>(-1) || start_length != 4 ||
            get_u32(props, start_at - 4) != kPropsRecordKindDate) {
            return fail(error, "114/Props has no 4-byte date record for the "
                               "project start (0x02400002), so the leaf Fixed2Data "
                               "+46 cannot be set");
        }
        shape.props_project_start = get_u32(props, start_at);
        /* W35b-0.3: the pair lengthens the project; no pair shortens it. */
        if (shape.project_finish < rows.u32(0, task_row::kFinish)) {
            return fail(error, "the recalculation would shorten the project (the "
                               "latest task finish is before the stored project "
                               "finish); the measured Calculate Project save "
                               "lengthens it");
        }
        if (rows.u32(0, task_row::kFinish) == shape.project_finish) {
            return fail(error, "the pending change does not move the project "
                               "finish; that recalculation has no measured "
                               "pair");
        }
        /* W35b-0.3: the stored project finish is the one before the change, so a
         * leaf that finishes before it had slack and one that finishes at it ended
         * the project.  The measured pair has one leaf past that finish and the
         * other before it, so Project's critical flags do not change.  A second
         * leaf at or past it (the lengthened leaf overtaken, or the old critical
         * leaf left behind) moves the critical path, and the writer never writes a
         * critical flag.  (The reader's flag is stored total slack <= 0, which is
         * stale here, so it cannot be compared.) */
        for (std::size_t row = 1; row < kRows; ++row) {
            if (rows.uid(row) != shape.critical_uid &&
                rows.u32(row, task_row::kFinish) >= rows.u32(0, task_row::kFinish)) {
                return fail(error, "the recalculation moves the critical path (task "
                                   "uid " + std::to_string(rows.uid(row)) +
                                       " does not finish before the stored project "
                                       "finish, as the other leaf must); the "
                                       "measured Calculate Project save leaves the "
                                       "critical flags as they were");
            }
        }
        /* W35b-2.7: the leaf that ends the project is the first leaf row in the measured
         * pair; the stamp it and the other leaf carry is not measured the other way round. */
        if (shape.critical_uid != rows.uid(1)) {
            return fail(error, "the leaf that ends the project (task uid " +
                                   std::to_string(shape.critical_uid) +
                                   ") is not the first leaf row; the measured Calculate "
                                   "Project save has the first leaf ending the project");
        }
        /* W35b-0.4 (RW35-W2): the pair changes one Duration, so every leaf's
         * early dates are already final: Finish is Start plus Duration on the
         * measured calendar.  A file left pending by a Start, Finish, progress or
         * calendar edit fails this, and the writer would save the old dates. */
        for (std::size_t row = 1; row < kRows; ++row) {
            std::uint32_t expected = 0;
            if (!calculate_measured_finish(rows.u32(row, task_row::kStart),
                                           rows.u32(row, task_row::kDuration), expected) ||
                expected != rows.u32(row, task_row::kFinish)) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       "'s finish is not its start plus its duration "
                                       "on the measured calendar (Monday to Friday, "
                                       "08:00 to 17:00, whole 8-hour days), so the "
                                       "pending recalculation was not caused by a "
                                       "Duration edit alone");
            }
        }
        /* W35c-2.2 (R35bG0-7): the leaf's second date copy (+104/+108) and its Remaining
         * Duration (+88) equal its Start, Finish and Duration in the control, so a pending
         * cause that updated one copy and not the other is refused. */
        for (std::size_t row = 1; row < kRows; ++row) {
            if (rows.u32(row, task_row::kStart2) != rows.u32(row, task_row::kStart) ||
                rows.u32(row, task_row::kFinish2) != rows.u32(row, task_row::kFinish)) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       "'s second date copy (+104/+108) differs from its "
                                       "Start and Finish, so the pending recalculation was "
                                       "not caused by a Duration edit alone; the measured "
                                       "Calculate Project save has the copies equal");
            }
            if (rows.u32(row, task_row::kRemainingDuration) !=
                rows.u32(row, task_row::kDuration)) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       "'s remaining duration (+88) differs from its "
                                       "duration, so the pending recalculation was not "
                                       "caused by a Duration edit alone; the measured "
                                       "Calculate Project save has them equal");
            }
        }
        /* W35c-0.2 (R35bG0-1): the leaf Start above is compared with row 0's Start, the
         * roll-up manual calculation leaves stale; the writer stamps each leaf's
         * Fixed2Data +46 with the Props project start instead.  The measured relation is
         * that the leaf Start is the first working instant at or after the Props start
         * (Sunday 21:00 maps to Monday 08:00).  A project start moved under manual
         * calculation breaks it, and the port cannot say where Project would put the
         * leaves.  After the 0.4 date check so each guard keeps its own test. */
        for (std::size_t row = 1; row < kRows; ++row) {
            std::uint32_t first = 0;
            if (!calculate_measured_first_working_start(shape.props_project_start, first) ||
                first != rows.u32(row, task_row::kStart)) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " does not start at the first working instant at "
                                       "or after the project start in 114/Props "
                                       "(0x02400002), so the project start was moved "
                                       "after the leaves were scheduled; the measured "
                                       "Calculate Project save has the leaves starting "
                                       "there (Sunday 21:00 to Monday 08:00)");
            }
        }
        /* W35c-1.5 (R35bG0-3): the finish-position guard above cannot tell one Duration edit
         * from two (A shortened below the stored finish and B lengthened past it has the
         * measured pair's signature with the critical path moved).  The stored Free Slack
         * (+74) is the before-state: manual calculation leaves it as the last calculation
         * wrote it (the control's B holds 9600, two days, and F9 rewrites it to seven).  The
         * leaf that ends the project now must have been critical (free slack 0), and every
         * other leaf must still have the slack its unchanged finish leaves before the stored
         * project finish; a leaf that fails either was edited too. */
        for (std::size_t row = 1; row < kRows; ++row) {
            std::uint32_t expected = 0;
            if (rows.uid(row) != shape.critical_uid) {
                std::uint32_t late_start = 0;
                if (!calculate_measured_late(rows.u32(row, task_row::kFinish),
                                             rows.u32(row, task_row::kDuration),
                                             rows.u32(0, task_row::kFinish), expected,
                                             late_start)) {
                    return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                           "'s slack before the stored project finish cannot "
                                           "be worked out on the measured calendar");
                }
            }
            if (rows.u32(row, task_row::kFreeSlack) != expected) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       "'s stored free slack (+74) is " +
                                       std::to_string(rows.u32(row, task_row::kFreeSlack)) +
                                       " ticks, not the " + std::to_string(expected) +
                                       " the measured Calculate Project save has before the "
                                       "change; more than one leaf was edited, or not the "
                                       "leaf that ended the project, so the critical path "
                                       "may have moved");
            }
        }
        /* W35b-0.5 (R35G1-3): the parameters no pair varies. */
        check_unvaried_parameters(doc, rows);
        for (std::size_t row = 1; row < kRows; ++row) {
            if (rows.u32(row, task_row::kStartAnchor) == kNoDate) {
                return fail(error, "task uid " + std::to_string(rows.uid(row)) +
                                       " has no start anchor; the measured "
                                       "Calculate Project save starts from "
                                       "anchored leaves");
            }
        }
        /* W35c-0.3 (R35bG0-2): the project's calculation options, which no pair varies.
         * W35d-2.8 (R35cW-5): last, not before the parameter checks.  The pin also holds
         * per-file identity records (R35cG0-3), so every Project-saved file but the control
         * fails it; run first, it hid the resource-table, calendar, VarMeta, Active, FixedMeta
         * and start-anchor reasons behind a text about options.  Each of those checks and each
         * pin test is unchanged: the pin tests alter only a Props record, so they reach it. */
        check_props_pin(props);
        return true;
    } catch (Error const& caught) {
        error = caught.what();
        return false;
    }
}

namespace {

/* The request must be consistent with the stored leaves it is applied to. */
void check_request(CalculateShape const& shape, TaskTable const& rows,
                   CalculateRequest const& request) {
    if (request.project_finish != shape.project_finish) {
        throw Error("calculate: the project finish " +
                    hex(request.project_finish) +
                    " is not the latest task finish " +
                    hex(shape.project_finish));
    }
    if (request.project_duration != shape.critical_duration) {
        throw Error("calculate: the project duration " +
                    std::to_string(request.project_duration) +
                    " is not the critical task's duration " +
                    std::to_string(shape.critical_duration));
    }
    if (request.leaves.size() != shape.leaf_uids.size()) {
        throw Error("calculate: " + std::to_string(request.leaves.size()) +
                    " leaf results were given for " +
                    std::to_string(shape.leaf_uids.size()) + " leaves");
    }
    for (std::size_t i = 0; i < request.leaves.size(); ++i) {
        CalculateLeaf const& leaf = request.leaves[i];
        if (leaf.uid != shape.leaf_uids[i]) {
            throw Error("calculate: leaf result " + std::to_string(i) +
                        " is for uid " + std::to_string(leaf.uid) +
                        ", the file's leaf is uid " +
                        std::to_string(shape.leaf_uids[i]));
        }
        std::size_t const row = rows.row_for_uid(leaf.uid);
        std::uint32_t const start = rows.u32(row, task_row::kStart);
        std::uint32_t const finish = rows.u32(row, task_row::kFinish);
        if (leaf.late_start < start || leaf.late_finish < finish) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " has a late date before its early date");
        }
        if (leaf.late_finish > request.project_finish) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " has a late finish after the project finish");
        }
        /* W35b-2.2 (R35G1-5): the unlinked backward pass gives every leaf the project
         * finish as its late finish (spec/writers/calculate.md section 8), and the leaf
         * that ends the project has no slack and starts late where it starts early. */
        if (leaf.late_finish != request.project_finish) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " has a late finish before the project finish; the unlinked "
                        "backward pass gives every leaf the project finish");
        }
        if (leaf.uid == shape.critical_uid &&
            (leaf.total_slack != 0 || leaf.late_start != start)) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " ends the project, so its slack is 0 and its late start is its "
                        "early start");
        }
        if (leaf.free_slack < 0 || leaf.total_slack < 0 ||
            leaf.start_slack != leaf.total_slack ||
            leaf.free_slack != leaf.total_slack) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " has slack the unlinked shape cannot have (free, "
                        "start and total slack are equal and not negative)");
        }
        /* W35b-3.7 (RW35-R4): the slack and the late start are the measured calendar's
         * working time between the leaf's finish and the project finish, not any
         * non-negative number. */
        std::uint32_t slack = 0;
        std::uint32_t late_start = 0;
        if (!calculate_measured_late(finish, rows.u32(row, task_row::kDuration),
                                     request.project_finish, slack, late_start) ||
            static_cast<std::uint32_t>(leaf.free_slack) != slack ||
            leaf.late_start != late_start) {
            throw Error("calculate: uid " + std::to_string(leaf.uid) +
                        " has slack or a late start that is not the working time between "
                        "its finish and the project finish on the measured calendar "
                        "(Monday to Friday, 08:00 to 17:00)");
        }
    }
}

} /* namespace */

void validate_calculate(CfbDocument const& doc, CalculateRequest const& request) {
    CalculateShape shape;
    std::string reason;
    if (!read_calculate_shape(doc, shape, reason)) {
        throw Error("calculate: " + reason);
    }
    TaskTable const before = TaskTable::from_document(doc, kTable);
    check_request(shape, before, request);
}

CalculateWrite save_calculate(CfbDocument& doc, CalculateRequest const& request,
                              std::string const& path) {
    if (path.empty() || std::filesystem::path(path).filename().empty()) {
        throw Error("calculate: the output path is empty or names a directory");
    }
    CalculateShape shape;
    std::string reason;
    if (!read_calculate_shape(doc, shape, reason)) {
        throw Error("calculate: " + reason);
    }
    TaskTable const before = TaskTable::from_document(doc, kTable);
    check_request(shape, before, request);

    CfbDocument work = doc;
    TaskTable after = before;

    /* The project row. */
    after.set_u32(0, task_row::kDuration, request.project_duration);
    after.set_u32(0, task_row::kRemainingDuration, request.project_duration);
    after.set_u32(0, task_row::kFinish, request.project_finish);
    after.set_u32(0, task_row::kFinish2, request.project_finish);
    after.set_u32(0, task_row::kFinish3, request.project_finish);
    /* The leaves. */
    for (CalculateLeaf const& leaf : request.leaves) {
        std::size_t const row = before.row_for_uid(leaf.uid);
        after.set_u32(row, kFreeSlack,
                      static_cast<std::uint32_t>(leaf.free_slack));
        after.set_u32(row, task_row::kStart3, leaf.late_start);
        after.set_u32(row, task_row::kFinish3, leaf.late_finish);
        after.set_u32(row, task_row::kStartAnchor, kNoDate);
        after.set_u32(row, kStartSlack,
                      static_cast<std::uint32_t>(leaf.start_slack));
        after.set_u32(row, kTotalSlack,
                      static_cast<std::uint32_t>(leaf.total_slack));
    }

    /* Fixed2Data: the project duration word clears, each leaf gets the
     * measured stamp and the Props project start. */
    std::uint32_t const project_start = shape.props_project_start;
    std::vector<std::uint8_t> fixed2 = read_fixed2_data(work, kTable);
    put_u32(fixed2, fixed2_record_offset(0) + kFixed2ProjectDuration, 0);
    for (CalculateLeaf const& leaf : request.leaves) {
        std::size_t const at = fixed2_record_offset(before.row_for_uid(leaf.uid));
        fixed2[at + kFixed2Stamp] = kCalculateLeafStamp;
        put_u32(fixed2, at + fixed2_row::kStart, project_start);
    }
    work.replace_stream(std::string(kTable) + "/Fixed2Data", fixed2);

    write_project_summary(work, before, after);
    after.store_into(work, kTable);
    write_edit_marker(work);

    /* The five overtime records on the project row, one measured append each;
     * the FixedMeta presence bits Project sets are 0x40 at +18 and 0x1f at +19
     * (the pair measures the five together, so the split below is a
     * bookkeeping choice that leaves the final state exact). */
    struct Append {
        std::uint32_t field;
        std::size_t byte;
        std::uint8_t bit;
    };
    static constexpr Append kAppends[] = {
        {kTaskActualOvertimeWorkFieldId, kMetaPresenceHigh, 0x01},
        {kTaskRemainingOvertimeWorkFieldId, kMetaPresenceHigh, 0x02},
        {kTaskOvertimeCostFieldId, kMetaPresenceHigh, 0x04},
        {kTaskActualOvertimeCostFieldId, kMetaPresenceHigh, 0x08},
        {kTaskRemainingOvertimeCostFieldId, kMetaPresenceHigh, 0x10},
    };
    CalculateWrite result;
    /* W35b-2.1 (R35G1-4): save_var_record and save_props_edit each write the path they are
     * given, so the chain runs against a private temporary file beside the target and the
     * target is written once, atomically, after the whole result is proved.  A refusal
     * anywhere leaves the target exactly as it was. */
    std::string const scratch = path + ".calculate.tmp";
    std::error_code ignored;
    /* W35c-2.7 (R35bG2-7): the scratch name is not ours to clear.  A file or directory
     * already there (a user's own, or the leftover of a crashed save) is refused, not
     * deleted; this runs before the try block so the cleanup below never touches it. */
    /* W35d-2.7 (R35cG2-6): symlink_status reports file_type::none with the error code set when
     * it cannot look (an unreadable parent, a path error), and that is not "already exists", so
     * the OS message is given and the advice to move a file aside is not. */
    std::filesystem::file_status const scratch_status =
        g_scratch_probe_fault ? std::filesystem::file_status(g_scratch_probe_type)
                              : std::filesystem::symlink_status(scratch, ignored);
    if (g_scratch_probe_fault) {
        ignored = g_scratch_probe_fault;
    }
    if (ignored && scratch_status.type() != std::filesystem::file_type::not_found) {
        throw Error("calculate: cannot check whether the scratch path " + scratch +
                    " is free: " + ignored.message());
    }
    if (scratch_status.type() != std::filesystem::file_type::not_found) {
        throw Error("calculate: the scratch path " + scratch +
                    " already exists; it is not removed, so move it aside and save again");
    }
    try {
        for (Append const& append : kAppends) {
            VarRecordEdit edit;
            edit.uid = 0;
            edit.field_id = append.field;
            edit.shape = VarRecordShape::append;
            edit.payload.assign(8, 0);
            edit.presence.push_back({append.byte, append.bit, true});
            if (result.var_records_appended == 0) {
                edit.presence.push_back({kMetaPresenceLow, 0x40, true});
            }
            save_var_record(work, kVarTaskTable, {edit}, scratch);
            ++result.var_records_appended;
            if (g_fault_after_appends != 0 && result.var_records_appended == g_fault_after_appends) {
                throw Error("calculate: injected fault after append " +
                            std::to_string(result.var_records_appended) + " (test seam)");
            }
        }

        /* 114/Props: the project finish and the pending flag, in place. */
        PropsEdit props_edit;
        PropsRecordEdit finish;
        finish.id = kPropProjectFinish;
        finish.kind = kPropsRecordKindDate;
        finish.payload.resize(4);
        put_u32(finish.payload, 0, request.project_finish);
        PropsRecordEdit pending;
        pending.id = kPropCalculationPending;
        pending.kind = kPropsRecordKindFlag;
        pending.payload = {0, 0};
        props_edit.records = {finish, pending};
        save_props_edit(work, props_edit, scratch);
        /* W35e-2.9 (R35dG1-4): one past the last append is the Props edit.  Three steps remain
         * after it: the scratch save, the re-read compare and the target write (W35f-1.11,
         * R35eG2-7, corrected the wording; the next seam point is after the first two). */
        if (g_fault_after_appends == std::size(kAppends) + 1) {
            throw Error("calculate: injected fault after the Props edit (test seam)");
        }

        work.save(scratch);
        CfbDocument const reread = CfbDocument::from_file(scratch);
        if (reread.to_bytes() != work.to_bytes()) {
            throw Error("calculate: the written file does not re-read to the "
                        "bytes built");
        }
        /* W35f-1.11 (R35eG2-7): two past the last append is the step after the scratch save and
         * the re-read compare, the last step before the target is written. */
        if (g_fault_after_appends == std::size(kAppends) + 2) {
            throw Error("calculate: injected fault after the re-read (test seam)");
        }
        reread.save(path);
        doc = reread;
    } catch (...) {
        std::filesystem::remove(scratch, ignored);
        throw;
    }
    std::filesystem::remove(scratch, ignored);
    result.leaves_written = request.leaves.size();
    return result;
}

} /* namespace mppwrite */
