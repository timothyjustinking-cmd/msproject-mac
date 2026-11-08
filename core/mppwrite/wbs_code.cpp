/*
 * wbs_code.cpp -- see wbs_code.h.  W34-4.6.
 */

#include "wbs_code.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "props_edit.h"
#include "table.h"
#include "varmeta_edit.h"

namespace mppwrite {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr char const* kFunction = "define WBS code";
constexpr char const* kTaskFixed = "114/TBkndTask/FixedData";
constexpr char const* kTaskMeta = "114/TBkndTask/FixedMeta";
constexpr char const* kTaskVarMeta = "114/TBkndTask/VarMeta";
constexpr char const* kTaskVar2 = "114/TBkndTask/Var2Data";
constexpr char const* kDirectoryProps = "114/Props";
constexpr std::size_t kFixedHeader = 48;
constexpr std::size_t kFixedStride = 202;
/* Task FixedData row words (core/mpp/reader.cpp kRowId/kRowUid; outline.h +140
 * has-children, +172 OutlineLevel). */
constexpr std::size_t kRowId = 0;
constexpr std::size_t kRowUid = 4;
constexpr std::size_t kRowHasChildren = 140;
constexpr std::size_t kRowOutlineLevel = 172;
/* Task FixedMeta: 16-byte header, 47-byte entries, the entry for FixedData row
 * r at index r + 3, whose +4 word is the row's FixedData offset; +0 is the state
 * (0x02 = deleted) and +26 bit 0x80 the stored summary flag. */
constexpr std::size_t kMetaHeader = 16;
constexpr std::size_t kMetaEntry = 47;
constexpr std::size_t kMetaFirstRow = 3;
constexpr std::size_t kMetaState = 0;
constexpr std::size_t kMetaRowWord = 4;
constexpr std::size_t kMetaSummaryByte = 26;
constexpr std::uint8_t kMetaSummaryBit = 0x80;

/* The measured Props records. */
constexpr std::uint32_t kMaskRecordId = 0x0240138Bu;
constexpr std::uint32_t kPrefixRecordId = 0x024013BEu;
constexpr std::size_t kMaskBytesBefore = 4;
constexpr std::size_t kPrefixBytesBefore = 2;

/* wave30-wbs-code: `01000000 00000200 2e000000 00000000 07000000 57000000`
 * ('.' separator, 'W' prefix) and "W" + NUL + two zero bytes. */
Bytes mask_payload() {
    return Bytes{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
                 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x07, 0x00, 0x00, 0x00, 0x57, 0x00, 0x00, 0x00};
}

Bytes prefix_payload() { return Bytes{0x57, 0x00, 0x00, 0x00, 0x00, 0x00}; }

/* The stored number text: two ASCII digits as UTF-16LE plus the NUL. */
Bytes number_payload(unsigned number) {
    char const tens = static_cast<char>('0' + (number / 10) % 10);
    char const ones = static_cast<char>('0' + number % 10);
    return Bytes{static_cast<std::uint8_t>(tens), 0x00,
                 static_cast<std::uint8_t>(ones), 0x00, 0x00, 0x00};
}

std::uint32_t get_u32(Bytes const& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

std::uint16_t get_u16(Bytes const& bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

std::string hex_byte(std::uint8_t value) {
    constexpr char digits[] = "0123456789abcdef";
    return std::string{digits[value >> 4], digits[value & 0x0F]};
}

/* The Task table's physical rows must be exactly the project row and the three
 * measured tasks, in uid order, with no tombstone. */
void check_population(CfbDocument const& doc) {
    for (char const* stream :
         {kTaskFixed, kTaskMeta, kTaskVarMeta, kTaskVar2, kDirectoryProps}) {
        if (!doc.has_stream(stream)) {
            throw Error(std::string(kFunction) + ": the file has no " + stream +
                        " stream; this edit needs the whole measured table");
        }
    }
    Bytes const fixed = doc.read_stream(kTaskFixed);
    if (fixed.size() < kFixedHeader ||
        (fixed.size() - kFixedHeader) % kFixedStride != 0) {
        throw Error(std::string(kFunction) + ": the Task table geometry is not "
                    "the measured one");
    }
    std::size_t const rows = (fixed.size() - kFixedHeader) / kFixedStride;
    if (rows != kWbsMeasuredTaskCount + 1) {
        throw Error(std::string(kFunction) + ": the file has " +
                    std::to_string(rows) + " task rows; the measured WBS pair "
                    "has the project row and exactly " +
                    std::to_string(kWbsMeasuredTaskCount) + " tasks, and another "
                    "count is not implemented in this port");
    }
    Bytes const meta = doc.read_stream(kTaskMeta);
    for (std::size_t row = 0; row < rows; ++row) {
        std::size_t const base = kFixedHeader + kFixedStride * row;
        std::uint32_t const uid = get_u32(fixed, base + kRowUid);
        if (uid != row) {
            throw Error(std::string(kFunction) + ": task row " +
                        std::to_string(row) + " has UniqueID " +
                        std::to_string(uid) + "; the measured pair has the "
                        "project row and tasks 1 to 3 in order");
        }
        /* R34G4-5: the writer numbers by UniqueID, so the task's ID must be
         * its UniqueID (Project numbers an ordered code by position). */
        std::uint32_t const id = get_u32(fixed, base + kRowId);
        if (id != uid) {
            throw Error(std::string(kFunction) + ": task row " +
                        std::to_string(row) + " has ID " + std::to_string(id) +
                        " but UniqueID " + std::to_string(uid) + "; the "
                        "measured pair numbers the tasks 1, 2 and 3 where ID "
                        "equals UniqueID, and another order is not measured");
        }
        if (row == 0) {
            continue;
        }
        /* The measured tasks are flat, unsummarised and live. */
        std::size_t const entry = kMetaHeader + kMetaEntry * (row + kMetaFirstRow);
        if (meta.size() < entry + kMetaEntry ||
            get_u32(meta, entry + kMetaRowWord) != base) {
            throw Error(std::string(kFunction) + ": task row " +
                        std::to_string(row) + " has no FixedMeta entry at the "
                        "measured slot; the FixedMeta state is not the "
                        "measured one");
        }
        if (meta[entry + kMetaState] != 0) {
            throw Error(std::string(kFunction) + ": task row " +
                        std::to_string(row) + " has FixedMeta state 0x" +
                        hex_byte(meta[entry + kMetaState]) + "; the measured "
                        "pair has live rows (state 0)");
        }
        unsigned const level = fixed[base + kRowOutlineLevel];
        if (level != 1 || fixed[base + kRowOutlineLevel + 1] != 0) {
            throw Error(std::string(kFunction) + ": task row " +
                        std::to_string(row) + " has outline level " +
                        std::to_string(get_u16(fixed, base + kRowOutlineLevel)) +
                        "; the measured pair has three top-level tasks, and a "
                        "nested outline's generated numbers are not measured");
        }
        if ((meta[entry + kMetaSummaryByte] & kMetaSummaryBit) != 0 ||
            get_u16(fixed, base + kRowHasChildren) != 0) {
            throw Error(std::string(kFunction) + ": task row " +
                        std::to_string(row) + " is a summary; the measured "
                        "pair has no summary task, and a nested outline's "
                        "generated numbers are not measured");
        }
    }
    for (VarEntry const& entry : read_var_entries(doc.read_stream(kTaskVarMeta))) {
        if (entry.field_id == kTaskWbsFieldId ||
            entry.field_id == kTaskProjectWbsFieldId) {
            throw Error(std::string(kFunction) + ": the file already stores WBS "
                        "text (uid " + std::to_string(entry.mid) + "); redefining "
                        "a code is not implemented in this port");
        }
    }
}

void check_directory_records(CfbDocument const& doc) {
    Bytes const props = doc.read_stream(kDirectoryProps);
    std::size_t at = 16;
    std::size_t mask = 0;
    std::size_t prefix = 0;
    bool found_mask = false;
    bool found_prefix = false;
    while (at + 12 <= props.size()) {
        std::uint32_t const length = get_u32(props, at);
        std::uint32_t const id = get_u32(props, at + 4);
        if (((id >> 16) & 0xFFu) != 0x40u ||
            length > props.size() - at - 12) {
            break;
        }
        if (id == kMaskRecordId) {
            found_mask = true;
            mask = length;
            for (std::size_t i = 0; i < length; ++i) {
                if (props[at + 12 + i] != 0) {
                    throw Error(std::string(kFunction) + ": a code definition "
                                "already exists; redefining it is not "
                                "implemented in this port");
                }
            }
        } else if (id == kPrefixRecordId) {
            found_prefix = true;
            prefix = length;
            for (std::size_t i = 0; i < length; ++i) {
                if (props[at + 12 + i] != 0) {
                    throw Error(std::string(kFunction) + ": a code prefix "
                                "already exists; redefining it is not "
                                "implemented in this port");
                }
            }
        }
        at += 12 + length;
    }
    if (!found_mask || !found_prefix || mask != kMaskBytesBefore ||
        prefix != kPrefixBytesBefore) {
        throw Error(std::string(kFunction) + ": 114/Props does not carry the "
                    "measured empty code records (0x0240138b of 4 bytes and "
                    "0x024013be of 2 bytes)");
    }
}

std::string scratch_path() {
    static std::atomic<unsigned> counter{0};
    std::error_code ignore;
    std::filesystem::path directory = std::filesystem::temp_directory_path(ignore);
    if (ignore) {
        directory = ".";
    }
    return (directory / ("w34-wbs-dry-" + std::to_string(++counter) + ".mpp"))
        .string();
}

void remove_quietly(std::string const& path) {
    std::error_code ignore;
    std::filesystem::remove(path, ignore);
}

WbsCodeWrite run_chain(CfbDocument& work, std::string const& path) {
    WbsCodeWrite written;
    written.var2_bytes_before = work.read_stream(kTaskVar2).size();
    written.props_bytes_before = work.read_stream(kDirectoryProps).size();

    /* The project row first: the measured Var2Data order is uid 0 at 512, then
     * tasks 1-3 at +10 each. */
    {
        VarRecordEdit edit;
        edit.uid = 0;
        edit.field_id = kTaskProjectWbsFieldId;
        edit.shape = VarRecordShape::append;
        edit.payload = number_payload(kWbsMeasuredTaskCount);
        edit.presence = {VarPresenceBit{40, 0x04, true}};
        (void)save_var_record(work, kVarTaskTable, {edit}, path);
        ++written.blocks_written;
    }
    for (std::uint32_t uid = 1; uid <= kWbsMeasuredTaskCount; ++uid) {
        VarRecordEdit edit;
        edit.uid = uid;
        edit.field_id = kTaskWbsFieldId;
        edit.shape = VarRecordShape::append;
        edit.payload = number_payload(uid);
        edit.presence = {VarPresenceBit{20, 0x80, true}};
        (void)save_var_record(work, kVarTaskTable, {edit}, path);
        ++written.blocks_written;
    }

    PropsEdit props;
    props.stream = kPropsBackEnd;
    props.records.push_back(PropsRecordEdit{kMaskRecordId, 0, mask_payload()});
    props.records.push_back(PropsRecordEdit{kPrefixRecordId, 0, prefix_payload()});
    (void)save_props_edit(work, props, path);

    written.var2_bytes_after = work.read_stream(kTaskVar2).size();
    written.props_bytes_after = work.read_stream(kDirectoryProps).size();
    return written;
}

/* Nothing outside the five owned streams may differ from the input. */
void prove_owned_streams(CfbDocument const& before, CfbDocument const& after) {
    constexpr std::array<std::string_view, 5> owned{
        {kTaskMeta, kTaskVarMeta, kTaskVar2, kDirectoryProps, "Props14"}};
    for (std::string const& stream : before.stream_paths()) {
        bool const is_owned =
            std::find(owned.begin(), owned.end(), std::string_view(stream)) !=
            owned.end();
        if (is_owned) {
            continue;
        }
        if (!after.has_stream(stream) ||
            after.read_stream(stream) != before.read_stream(stream)) {
            throw Error(std::string(kFunction) + ": the stream " + stream +
                        " changed; only the Task VarMeta, Var2Data and "
                        "FixedMeta, 114/Props and Props14 may move");
        }
    }
    for (std::string_view stream : owned) {
        if (!after.has_stream(stream)) {
            throw Error(std::string(kFunction) + ": the written file lost " +
                        std::string(stream));
        }
    }
}

} /* namespace */

WbsCodeWrite save_wbs_code(CfbDocument& doc, std::string const& path,
                           bool dry_run) {
    check_population(doc);
    check_directory_records(doc);

    if (!dry_run && path.empty()) {
        throw Error(std::string(kFunction) + ": the output path is empty");
    }

    /* The five measured writers each write and prove a file, so the chain runs
     * against a scratch file; the caller's path is written ONCE, after the whole
     * chain and the owned-streams proof have succeeded (a caller that protects
     * its target by letting only its last writer touch it stays protected). */
    CfbDocument work = doc;
    std::string const scratch = scratch_path();
    WbsCodeWrite written;
    try {
        written = run_chain(work, scratch);
        prove_owned_streams(doc, work);
    } catch (...) {
        remove_quietly(scratch);
        throw;
    }
    remove_quietly(scratch);
    if (dry_run) {
        return written;
    }

    std::error_code exists_error;
    bool const existed = std::filesystem::exists(path, exists_error);
    try {
        work.save(path);
        CfbDocument const reread = CfbDocument::from_file(path);
        if (reread.to_bytes() != work.to_bytes()) {
            throw Error(std::string(kFunction) + ": the written file does not "
                        "re-emit the document built");
        }
    } catch (...) {
        if (!existed) {
            remove_quietly(path);
        }
        throw;
    }
    doc = std::move(work);
    return written;
}

} /* namespace mppwrite */
