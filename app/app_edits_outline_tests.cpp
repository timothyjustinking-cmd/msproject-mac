// app/app_edits_outline_tests.cpp -- Indent / Outdent through the app's edit
// model and save cascade against Project's own save pairs (wave 28 measured the
// manual pairs; wave 29, W29-0.1, gave the writer the assignment table and
// 114/Props; W29-0.4 measured the automatic pairs).
//
// For each committed pair (tests/data/wave28-outline-{indent,summary,outdent}
// and tests/data/wave29-outline-auto-{indent,summary,outdent} plus
// wave29-outline-manual-parent, and the matching -control.mpp; neutral metadata
// and the neutral C:/w28 drive path, see tests/data/README.md and
// core/mppwrite/outline.h) this test:
//
//   1. loads the pair's CONTROL through the app's reader adapter and builds the
//      edit with make_outline_edit() -- the app's own file-side validation and
//      calendar arithmetic -- then proves the measured words it computed:
//        indent : the new summary's +84 is 0x2ee0 for both measured manual
//                 indents and 0x3840 / 0x2580 for the automatic ones (the
//                 calendar's working time across the moved subtree);
//        outdent: the restored marker words are 0x5dc0 / format 7 and the
//                 restored schedule is next_start(parent +104) = 0x3bf212c0,
//                 add_work(that, 0x5dc0) = 0x3bf627d8 (Project's words);
//        automatic shapes also carry the slack set Project's save writes on
//                 every recalculated row (+74/+112/+116/+186/+190), measured
//                 from the result file (W29-0.4); the manual pairs carry none;
//   2. queues it with push(), checks the DISPLAY the edit model would show
//      (levels, OutlineNumber paths, summary flags, +84 durations; the manual
//      pairs leave the +104/+108 dates the sheet and Gantt draw alone, while an
//      automatic save recomputes the summary's +108 and the project row's
//      duration/finish, which the display follows -- W29-0.4), and checks undo
//      restores the control and redo the move;
//   3. saves it with the real cascade into APP_TEST_OUT_DIR and proves every
//      stream the writer owns matches PROJECT's own save, field by field
//      (W29-0.1, the wave-28 review's R28-1: wave 28 compared these with the
//      CONTROL, so Project's own changes passed silently):
//        Task FixedData, FixedMeta, Fixed2Meta  byte-identical to the pair
//                   (FixedMeta including the summary row's +2, +8..23 blob and
//                   +26, which the writer now reproduces);
//        Task VarMeta, Var2Data  byte-identical (including Project's stale
//                   bytes past the logical entry count);
//        Task Fixed2Data  byte-identical after canonicalising the per-save
//                   identity GUIDs (flags +42, start copy +46 and the parent
//                   links included);
//        TBkndAssn FixedData, FixedMeta, Fixed2Meta, VarMeta, Var2Data
//                   byte-identical: the indent's assignment tombstone (state
//                   0x02, field count 0, header +4 -> 6, VarMeta entries
//                   removed) and the outdent's re-created row (new uid, planned
//                   work copied, Fixed2Meta +9/+52, four Var2 blocks);
//        TBkndAssn Fixed2Data  byte-identical after canonicalising the record
//                   identities AND resolving the parent links, which Project
//                   leaves stale on rows it does not touch (outline.h);
//        114/Props  compared through the committed null save (W29-1.0b): its
//                   two per-save identity GUID records are masked as noise,
//                   every other byte must equal Project's edit save, and the
//                   six counters 0x01000002, 0x01000100, 0x04000001, 0x04000002,
//                   0x04000100 and 0x04008000 are ALSO checked by name;
//        each owned row field is ALSO checked by name against Project's file:
//        the summary row's FixedMeta +2/+8..23/+26, Fixed2Meta +88/+95,
//        Fixed2Data +42/+46, and the assignment row's state/count/header word,
//        uid and planned work.
//   4. compares EVERY OTHER STREAM THROUGH THE COMMITTED NULL SAVE (W29-1.0b,
//      the R28-1 residual): a byte that differs between the control and
//      Project's no-edit save is per-save noise and is masked by a named rule;
//      every unmasked byte that differs between the null save and Project's
//      edit save is the edit's EFFECT and must equal the port's save.  Each
//      stream's noise and effect counts are printed, and a mismatch is a
//      FAILING check -- no whole-stream expected failure remains;
//   5. reopens the port's save through the independent core/mpp reader and
//      proves the DISPLAY edit_model shows is the file Project's reader shows:
//      outline level, OutlineNumber, summary flag, duration and the +104/+108
//      pair all equal Project's saved file, field by field.
//
// It also checks the refusals make_outline_edit()/push() owe the user: the
// first top-level row has no preceding sibling, a summary cannot be outdented,
// indenting under an existing summary is unmeasured, an unknown uid and a
// pending-deleted row are refused, and deleting either row of a queued outline
// edit displaces it (undo restores it).
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w29-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w29-app --config Release --target app_edits_outline_tests
//   ctest --test-dir build\w29-app -C Release -R "^app_edits_outline$" --output-on-failure

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "cfb_writer.h"
#include "edit_model.h"
#include "layout_model.h"
// core/mppwrite/outline.h includes core/mppwrite/table.h from its own
// directory.  The app include path lists core/view first, so a bare
// "table.h" here would find core/view/table.h instead; outline.h pins the
// mppwrite header that task_row/check_table live in.
#include "outline.h"
#include "reader.h"
#include "reader_adapter.h"
#include "save_pair_noise.h"
#include "table.h"

#if !defined(APP_TEST_DATA_DIR) || !defined(APP_TEST_OUT_DIR)
int main() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
    return 0;
}
#else

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

/* TBkndAssn stream geometry (outline.cpp): 110-byte FixedData rows, 34-byte
 * FixedMeta entries, 48-byte Fixed2Data records, 53-byte Fixed2Meta entries. */
constexpr std::size_t kAssnStride = 110;
constexpr std::size_t kAssnMetaHeader = 16;
constexpr std::size_t kAssnMetaEntry = 34;
constexpr std::size_t kAssnFixed2MetaEntry = 53;
constexpr std::size_t kAssnFixed2Record = 48;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

const char* kind_name(save_pair::Kind kind) {
    switch (kind) {
        case save_pair::Kind::fixed_block:
            return "block";
        case save_pair::Kind::exact:
            return "exact";
        case save_pair::Kind::props14:
            return "props14";
        case save_pair::Kind::cedl:
            return "cedl";
    }
    return "?";
}

save_pair::Kind kind_for_path(const std::string& path) {
    /* OLE control streams carry a leading 0x05 in their path; the measured
     * zero-noise stream is DocumentSummaryInformation below it. */
    std::string name = path;
    if (!name.empty() && name.front() == '\x05') {
        name.erase(name.begin());
    }
    if (name == "DocumentSummaryInformation") {
        return save_pair::Kind::exact;
    }
    if (name == "Props14") {
        return save_pair::Kind::props14;
    }
    if (name == "214/CEdl/Var2Data") {
        return save_pair::Kind::cedl;
    }
    return save_pair::Kind::fixed_block;
}

std::string mismatch_message(const std::string& path,
                             const save_pair::Verdict& verdict) {
    std::string message =
        path + " equals Project's edit save outside the null-save mask";
    if (verdict.mismatches != 0) {
        char ours[8];
        char theirs[8];
        std::snprintf(ours, sizeof(ours), "%02x", verdict.output_byte);
        std::snprintf(theirs, sizeof(theirs), "%02x", verdict.result_byte);
        message += " (first of " + std::to_string(verdict.mismatches) +
                   " at byte " + std::to_string(verdict.first_mismatch) +
                   ": port 0x" + ours + ", Project 0x" + theirs + ")";
    }
    return message;
}

/* The W29-1.0b report: per stream, the per-save noise and the edit's effect. */
void report_stream(const std::string& path, save_pair::Kind kind,
                   const save_pair::Verdict& verdict) {
    std::printf("   %-34s %-7s noise=%-4llu effects=%-4llu",
                path.c_str(), kind_name(kind),
                static_cast<unsigned long long>(verdict.noise),
                static_cast<unsigned long long>(verdict.effects));
    if (verdict.masked_tail != 0) {
        std::printf(" masked-tail=%llu",
                    static_cast<unsigned long long>(verdict.masked_tail));
    }
    std::printf("\n");
}

void check_size(std::size_t got, std::size_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

std::uint16_t get_u16(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t get_u32(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

std::string task_stream(const char* tail) {
    return std::string("114/TBkndTask/") + tail;
}

std::string assn_stream(const char* tail) {
    return std::string("114/TBkndAssn/") + tail;
}

const std::array<std::string, 6> kTaskStreams = {
    task_stream("FixedData"), task_stream("FixedMeta"),
    task_stream("Fixed2Data"), task_stream("Fixed2Meta"),
    task_stream("VarMeta"), task_stream("Var2Data")};

const std::array<std::string, 6> kAssnStreams = {
    assn_stream("FixedData"), assn_stream("FixedMeta"),
    assn_stream("Fixed2Data"), assn_stream("Fixed2Meta"),
    assn_stream("VarMeta"), assn_stream("Var2Data")};

/* The streams this writer owns: the six Task streams, the six Assn streams and
 * 114/Props (the save counters). */
bool owned_stream(const std::string& path) {
    return "114/Props" == path ||
           std::find(kTaskStreams.begin(), kTaskStreams.end(), path) !=
               kTaskStreams.end() ||
           std::find(kAssnStreams.begin(), kAssnStreams.end(), path) !=
               kAssnStreams.end();
}

Bytes stream_or_empty(mppwrite::CfbDocument& document, const std::string& path) {
    return document.has_stream(path) ? document.read_stream(path) : Bytes{};
}

bool same_span(const Bytes& a, const Bytes& b, std::size_t at, std::size_t n) {
    if (at + n > a.size() || at + n > b.size()) {
        return false;
    }
    return std::equal(a.begin() + static_cast<std::ptrdiff_t>(at),
                      a.begin() + static_cast<std::ptrdiff_t>(at + n),
                      b.begin() + static_cast<std::ptrdiff_t>(at));
}

std::string hex_tag(std::uint32_t tag) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%08x", tag);
    return buffer;
}

/* The 16-byte scalar record framing outline.cpp writes: u32 4 / big-endian tag
 * / u32 4 / u32 value.  Returns the value's offset, or kNoIndex unless the tag
 * occurs exactly once. */
std::size_t props_scalar_value(const Bytes& props, std::uint32_t tag) {
    std::size_t found = kNoIndex;
    for (std::size_t at = 0; at + 16 <= props.size(); ++at) {
        if (get_u32(props, at) != 4 || get_u32(props, at + 8) != 4) {
            continue;
        }
        const std::uint32_t stored =
            (static_cast<std::uint32_t>(props[at + 4]) << 24) |
            (static_cast<std::uint32_t>(props[at + 5]) << 16) |
            (static_cast<std::uint32_t>(props[at + 6]) << 8) |
            static_cast<std::uint32_t>(props[at + 7]);
        if (stored != tag) {
            continue;
        }
        if (found != kNoIndex) {
            return kNoIndex;  /* the caller's check reports the duplicate */
        }
        found = at + 12;
    }
    return found;
}

/* The field keys of every physical VarMeta entry whose uid is `uid`, in file
 * order.  The TABLES' entries are (uid, Var2Data offset, key) triples. */
std::vector<std::uint32_t> var_keys_for_uid(const Bytes& var_meta,
                                            std::uint32_t uid) {
    std::vector<std::uint32_t> keys;
    try {
        for (const mppwrite::VarEntry& entry :
             mppwrite::read_var_entries(var_meta)) {
            if (entry.mid == uid) {
                keys.push_back(entry.field_id);
            }
        }
    } catch (const std::exception&) {
        /* the caller's byte-for-byte check reports the damage */
    }
    return keys;
}

std::size_t uid_row(const Bytes& fixed_data, std::uint32_t uid) {
    if (fixed_data.size() < mppwrite::task_row::kHeaderSize) {
        return kNoIndex;
    }
    const std::size_t rows = (fixed_data.size() - mppwrite::task_row::kHeaderSize) /
                             mppwrite::task_row::kStride;
    for (std::size_t row = 0; row < rows; ++row) {
        if (get_u32(fixed_data, mppwrite::task_row::kHeaderSize +
                                   mppwrite::task_row::kStride * row + 4) == uid) {
            return row;
        }
    }
    return kNoIndex;
}

/* Index of the FixedMeta 47-byte entry that names this FixedData row. */
std::size_t meta_entry_for_row(const Bytes& meta, std::size_t row) {
    if (meta.size() < 16) {
        return kNoIndex;
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(
        mppwrite::task_row::kHeaderSize + mppwrite::task_row::kStride * row);
    for (std::size_t i = 0; i < (meta.size() - 16) / 47; ++i) {
        if (get_u32(meta, 16 + i * 47 + 4) == offset) {
            return i;
        }
    }
    return kNoIndex;
}

/* The first TBkndAssn FixedData row whose task word (+4) names this task uid. */
std::size_t assn_uid_row(const Bytes& fixed, std::uint32_t uid) {
    if (fixed.size() < kAssnStride) {
        return kNoIndex;
    }
    for (std::size_t row = 0; row < fixed.size() / kAssnStride; ++row) {
        if (get_u32(fixed, kAssnStride * row + 4) == uid) {
            return row;
        }
    }
    return kNoIndex;
}

/* Index of the TBkndAssn FixedMeta 34-byte entry that names this row. */
std::size_t assn_meta_entry_for_row(const Bytes& meta, std::size_t row) {
    if (meta.size() < kAssnMetaHeader) {
        return kNoIndex;
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(kAssnStride * row);
    for (std::size_t i = 0;
         i < (meta.size() - kAssnMetaHeader) / kAssnMetaEntry; ++i) {
        if (get_u32(meta, kAssnMetaHeader + i * kAssnMetaEntry + 4) == offset) {
            return i;
        }
    }
    return kNoIndex;
}

/* Index of the TBkndAssn Fixed2Meta 53-byte entry that names this row. */
std::size_t assn_fixed2_meta_entry_for_row(const Bytes& meta, std::size_t row) {
    if (meta.size() < 16) {
        return kNoIndex;
    }
    const std::uint32_t offset =
        static_cast<std::uint32_t>(kAssnFixed2Record * row);
    for (std::size_t i = 0;
         i < (meta.size() - 16) / kAssnFixed2MetaEntry; ++i) {
        if (get_u32(meta, 16 + i * kAssnFixed2MetaEntry + 4) == offset) {
            return i;
        }
    }
    return kNoIndex;
}

/* The 16-byte identity GUID Project allocates in Fixed2Data. */
using Identity = std::array<std::uint8_t, 16>;

Identity read_identity(const Bytes& fixed2, std::size_t at) {
    Identity id{};
    if (at + 16 <= fixed2.size()) {
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(at),
                  fixed2.begin() + static_cast<std::ptrdiff_t>(at + 16),
                  id.begin());
    }
    return id;
}

bool identity_empty(const Identity& id) {
    return std::all_of(id.begin(), id.end(),
                       [](std::uint8_t b) { return b == 0; });
}

/* The identity map of one Fixed2Data stream: every record's own +0..15 becomes
 * a canonical id carrying `tag` and the record number, so two files that
 * allocate different GUIDs still compare equal.  `records` counts the entries
 * (not the stream's header records), `first_record` their stream position. */
std::map<Identity, Identity> identity_map(const Bytes& fixed2,
                                          std::size_t records,
                                          std::size_t stride,
                                          std::size_t first_record,
                                          std::uint8_t tag) {
    std::map<Identity, Identity> map;
    for (std::size_t record = 0; record < records; ++record) {
        const std::size_t base = stride * (first_record + record);
        const Identity id = read_identity(fixed2, base);
        if (identity_empty(id)) {
            continue;
        }
        Identity replacement{};
        replacement[0] = tag;
        for (std::size_t i = 0; i < 4; ++i) {
            replacement[1 + i] =
                static_cast<std::uint8_t>((record >> (8 * i)) & 0xFFu);
        }
        map.emplace(id, replacement);
    }
    return map;
}

/* Canonicalise the per-save identity GUIDs of the Task Fixed2Data: each data
 * record's +0..15 becomes a canonical id and each parent link (+24..39)
 * becomes the parent's canonical id.  Project allocates fresh identities on
 * every save; every other byte must match. */
Bytes canonical_task_fixed2(const Bytes& fixed2, std::size_t rows) {
    constexpr std::size_t kRecord = 64;
    constexpr std::size_t kParent = 24;
    if (fixed2.size() != kRecord * (rows + 3)) {
        return {};  /* the caller's size check reports the damage */
    }
    const std::map<Identity, Identity> canonical =
        identity_map(fixed2, rows, kRecord, 3, 0x42);
    Bytes out = fixed2;
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t base = kRecord * (row + 3);
        const auto self = canonical.find(read_identity(fixed2, base));
        if (self != canonical.end()) {
            std::copy(self->second.begin(), self->second.end(),
                      out.begin() + static_cast<std::ptrdiff_t>(base));
        }
        const auto found = canonical.find(read_identity(fixed2, base + kParent));
        if (found != canonical.end()) {
            std::copy(found->second.begin(), found->second.end(),
                      out.begin() + static_cast<std::ptrdiff_t>(base + kParent));
        }
    }
    return out;
}

/* Canonicalise the TBkndAssn Fixed2Data identities.  A record's own +0..15 is a
 * per-save identity; its parent +16..31 names a TASK Fixed2Data identity (a
 * different id space).  Each file resolves the parent through its OWN task
 * identities first.  MEASURED (W29-0.1): Project does NOT rewrite the parent
 * links of assignment rows it does not touch when it reallocates the Task
 * identities on save, so those links are STALE and resolve through the
 * CONTROL's task identities instead.  Links that name no identity in either map
 * are kept verbatim: both files carry the same stale bytes, or the comparison
 * reports a real difference. */
Bytes canonical_assn_fixed2(const Bytes& assn_fixed2, std::size_t assn_rows,
                            const Bytes& task_fixed2, std::size_t task_rows,
                            const Bytes& control_task_fixed2,
                            std::size_t control_task_rows) {
    constexpr std::size_t kParent = 16;
    if (assn_fixed2.size() != kAssnFixed2Record * assn_rows) {
        return {};  /* the caller's size check reports the damage */
    }
    const std::map<Identity, Identity> own =
        identity_map(assn_fixed2, assn_rows, kAssnFixed2Record, 0, 0x41);
    const std::map<Identity, Identity> parents =
        identity_map(task_fixed2, task_rows, 64, 3, 0x42);
    const std::map<Identity, Identity> stale =
        identity_map(control_task_fixed2, control_task_rows, 64, 3, 0x42);
    Bytes out = assn_fixed2;
    for (std::size_t row = 0; row < assn_rows; ++row) {
        const std::size_t base = kAssnFixed2Record * row;
        const auto self = own.find(read_identity(assn_fixed2, base));
        if (self != own.end()) {
            std::copy(self->second.begin(), self->second.end(),
                      out.begin() + static_cast<std::ptrdiff_t>(base));
        }
        const Identity parent = read_identity(assn_fixed2, base + kParent);
        const auto found = parents.find(parent);
        const auto fallback = stale.find(parent);
        if (found != parents.end()) {
            std::copy(found->second.begin(), found->second.end(),
                      out.begin() +
                          static_cast<std::ptrdiff_t>(base + kParent));
        } else if (fallback != stale.end()) {
            std::copy(fallback->second.begin(), fallback->second.end(),
                      out.begin() +
                          static_cast<std::ptrdiff_t>(base + kParent));
        }
    }
    return out;
}

/* W29-0.4: one row of the slack words an AUTOMATIC edit makes the save write.
 * The measured rule (build_outline_slack): free = start = total =
 * work_time_between(row finish, resulting project finish), late start =
 * add_work(row start, start_slack), late finish = add_work(row finish,
 * total_slack) = the resulting project finish. */
struct SlackExpect {
    std::uint32_t uid;
    std::uint32_t free_slack;
    std::uint32_t start_slack;
    std::uint32_t total_slack;
    std::uint32_t late_start_raw;
    std::uint32_t late_finish_raw;
};

const SlackExpect kAutoIndentSlack[] = {
    {0, 0, 0, 0, 0x3BF212C0u, 0x3BF427D8u},
    {1, 0, 0, 0, 0x3BF212C0u, 0x3BF427D8u},
    {2, 0, 0, 0, 0x3BF212C0u, 0x3BF427D8u},
    {3, 4800, 4800, 4800, 0x3BF312C0u, 0x3BF427D8u},
};
const SlackExpect kAutoSummarySlack[] = {
    {0, 0, 0, 0, 0x3BF212C0u, 0x3BF327D8u},
    {1, 0, 0, 0, 0x3BF212C0u, 0x3BF327D8u},
    {2, 0, 0, 0, 0x3BF212C0u, 0x3BF327D8u},
    {3, 0, 0, 0, 0x3BF212C0u, 0x3BF327D8u},
};
const SlackExpect kAutoOutdentSlack[] = {
    {0, 0, 0, 0, 0x3BF212C0u, 0x3BF627D8u},
    {1, 0, 0, 0, 0x3BF212C0u, 0x3BF627D8u},
    {2, 9600, 9600, 9600, 0x3BF412C0u, 0x3BF627D8u},
    {3, 14400, 14400, 14400, 0x3BF512C0u, 0x3BF627D8u},
};
/* The hybrid pair: the parent uid 1 is MANUAL, so the save recalculates it as
 * the restored leaf plus the automatic project row and moved row; the other
 * manual row (uid 3) keeps its stale late dates and is absent. */
const SlackExpect kManualParentSlack[] = {
    {0, 0, 0, 0, 0x3BF11E78u, 0x3BF627D8u},
    {1, 0, 0, 0, 0x3BF212C0u, 0x3BF627D8u},
    {2, 9600, 9600, 9600, 0x3BF412C0u, 0x3BF627D8u},
};

struct PairCase {
    const char* slug;
    const char* control;
    const char* result;
    std::uint32_t uid;        /* the moved task */
    bool indent;
    std::uint32_t anchor_uid; /* the summary row the edit touches */
    /* The measured words make_outline_edit must compute.  Indent uses
     * summary_duration; outdent uses the four restored_* values. */
    std::uint32_t summary_duration;
    std::uint32_t restored_duration;
    std::uint16_t restored_duration_format;
    std::uint32_t restored_start_raw;
    std::uint32_t restored_finish_raw;
    /* W29-0.4: the slack set an automatic edit carries (empty for the manual
     * wave-28 pairs). */
    const SlackExpect* slack = nullptr;
    std::size_t slack_count = 0;
};

const PairCase kPairs[] = {
    /* Project indented Task 2 (a leaf) under Task 1: Task 1 becomes a summary. */
    {"indent", "wave28-outline-indent-control.mpp", "wave28-outline-indent.mpp",
     2, true, 1, 0x2EE0u, 0, 0, 0, 0},
    /* Project indented Task 2 (itself a summary of Task 3) under Task 1: Task 1
     * becomes a summary of a summary. */
    {"summary", "wave28-outline-summary-control.mpp", "wave28-outline-summary.mpp",
     2, true, 1, 0x2EE0u, 0, 0, 0, 0},
    /* Project outdented Task 2 from its single-child parent Task 1: Task 1 is a
     * leaf again with the marker's stored duration and a restored schedule. */
    {"outdent", "wave28-outline-outdent-control.mpp", "wave28-outline-outdent.mpp",
     2, false, 1, 0, 0x5DC0u, 0x0007u, 0x3BF212C0u, 0x3BF627D8u},
    /* W29-0.4: the four automatic-scheduling pairs.  Project recalculates every
     * automatic row, so the save ALSO writes the summary's +108 and the project
     * row's duration/finish, and the edit carries the slack words measured from
     * Project's result file (core/mppwrite/outline.h). */
    {"auto-indent", "wave29-outline-auto-indent-control.mpp",
     "wave29-outline-auto-indent.mpp", 2, true, 1, 0x3840u, 0, 0, 0, 0,
     kAutoIndentSlack, sizeof(kAutoIndentSlack) / sizeof(kAutoIndentSlack[0])},
    {"auto-summary", "wave29-outline-auto-summary-control.mpp",
     "wave29-outline-auto-summary.mpp", 2, true, 1, 0x2580u, 0, 0, 0, 0,
     kAutoSummarySlack, sizeof(kAutoSummarySlack) / sizeof(kAutoSummarySlack[0])},
    {"auto-outdent", "wave29-outline-auto-outdent-control.mpp",
     "wave29-outline-auto-outdent.mpp", 2, false, 1, 0, 0x5DC0u, 0x0007u,
     0x3BF212C0u, 0x3BF627D8u, kAutoOutdentSlack,
     sizeof(kAutoOutdentSlack) / sizeof(kAutoOutdentSlack[0])},
    /* The hybrid pair: an automatic row leaving a MANUAL parent.  The restored
     * parent keeps its effective +104/+108; its duration and the slack words
     * are the automatic rule's. */
    {"manual-parent", "wave29-outline-manual-parent-control.mpp",
     "wave29-outline-manual-parent.mpp", 2, false, 1, 0, 0x5DC0u, 0x0007u,
     0x3BF212C0u, 0x3BF627D8u, kManualParentSlack,
     sizeof(kManualParentSlack) / sizeof(kManualParentSlack[0])},
};

const app::TaskRow* find_row(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

const mpp::Task* find_task(const mpp::Document& document, std::uint32_t uid) {
    for (const mpp::Task& task : document.tasks) {
        if (task.uid == uid) {
            return &task;
        }
    }
    return nullptr;
}

/* The display fields an outline edit owns: level, OutlineNumber, summary flag,
 * duration (+84).  Start/finish (+104/+108) are included because Project's save
 * leaves them where the control had them and the sheet still draws them. */
void check_row_row(const app::TaskRow& shown, const app::TaskRow& expected,
                   const char* slug, std::uint32_t uid, const char* side) {
    const std::string prefix = std::string(slug) + " " + side + " uid " +
                               std::to_string(uid);
    check(shown.outline_level == expected.outline_level,
          (prefix + " outline level").c_str());
    check_size(shown.duration_ticks, expected.duration_ticks,
               (prefix + " duration").c_str());
    check(shown.summary == expected.summary,
          (prefix + " summary flag").c_str());
    check(shown.outline_number == expected.outline_number,
          (prefix + " outline number").c_str());
    check(shown.start == expected.start, (prefix + " start").c_str());
    check(shown.finish == expected.finish, (prefix + " finish").c_str());
}

void check_display_matches(const app::PlanInput& shown,
                           const app::PlanInput& expected, const char* slug,
                           const char* side) {
    // W29-0.4: an automatic save recalculates the project row's duration and
    // finish, and the display plan follows; the manual shapes leave both.
    check_size(shown.project_duration_ticks, expected.project_duration_ticks,
               (std::string(slug) + " " + side + " project duration").c_str());
    check(shown.project_finish == expected.project_finish,
          (std::string(slug) + " " + side + " project finish").c_str());
    for (std::uint32_t uid = 1; uid <= 3; ++uid) {
        const app::TaskRow* a = find_row(shown, uid);
        const app::TaskRow* b = find_row(expected, uid);
        check(a != nullptr && b != nullptr,
              (std::string(slug) + " " + side + ": uid " + std::to_string(uid) +
               " is a row").c_str());
        if (a != nullptr && b != nullptr) {
            check_row_row(*a, *b, slug, uid, side);
        }
    }
}

/* W29-0.1: FixedMeta is now written in full.  The summary row's +2 field count,
 * +26 summary bit and the 16-byte +8..23 cache blob all follow measured rules
 * (set_fixed_meta_blob in outline.cpp), so the whole stream must equal
 * PROJECT's file byte for byte -- wave 28 masked the blob and compared it with
 * the control, which R28-1 rejected. */
void check_fixed_meta(const Bytes& project, const Bytes& ours, const char* slug) {
    const std::string prefix = std::string(slug);
    check_size(ours.size(), project.size(),
               (prefix + " FixedMeta size equals Project's").c_str());
    if (ours.size() == project.size()) {
        check(ours == project,
              (prefix + " FixedMeta equals Project's byte for byte").c_str());
    }
}

void check_pair(const PairCase& pair) {
    std::printf("-- pair %s:\n", pair.slug);
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / pair.control;
    const std::filesystem::path result_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / pair.result;
    const std::filesystem::path null_path =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        save_pair::null_fixture_name(pair.control);
    if (!std::filesystem::exists(control_path) ||
        !std::filesystem::exists(result_path)) {
        std::printf("SKIP: %s / %s are not present\n", pair.control, pair.result);
        return;
    }
    /* W29-1.0b: the mask source is committed with the pair. */
    check(std::filesystem::exists(null_path),
          (std::string("the committed null save for ") + pair.control +
           " is present").c_str());

    app::PlanInput plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
    } catch (const std::exception& error) {
        check(false, "the pair control loads through the reader");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    const app::TaskRow* moved = find_row(plan, pair.uid);
    const app::TaskRow* anchor = find_row(plan, pair.anchor_uid);
    check(moved != nullptr && anchor != nullptr,
          "the control has the moved row and its summary row");

    /* --- make_outline_edit: the measured words --------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_outline_edit(plan, control_path.string(),
                                              pair.uid, pair.indent, edit,
                                              edit_error);
    check(built, "make_outline_edit accepts the measured shape");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == (pair.indent ? app::StructuralKind::outline_indent
                                    : app::StructuralKind::outline_outdent),
          "the built edit has the outline kind");
    check_size(edit.uid, pair.uid, "the edit names the moved task");
    check_size(edit.anchor_uid, pair.anchor_uid,
               "the edit names the summary row the display touches");
    if (pair.indent) {
        check_size(edit.summary_duration, pair.summary_duration,
                   "the summary duration is Project's +84 (the calendar span)");
    } else {
        check_size(edit.restored_duration, pair.restored_duration,
                   "the restored duration is the marker's word");
        check_size(edit.restored_duration_format, pair.restored_duration_format,
                   "the restored duration format is the marker's word");
        check_size(app::packed_from_date32(edit.restored_start),
                   pair.restored_start_raw,
                   "the restored start is next_start(parent +104)");
        check_size(app::packed_from_date32(edit.restored_finish),
                   pair.restored_finish_raw,
                   "the restored finish is add_work(start, marker duration)");
    }
    // W29-0.4: the automatic shapes carry the recalculated rows' slack words,
    // the manual ones carry none.
    check_size(edit.slack.size(), pair.slack_count,
               "the edit carries Project's slack rows");
    if (edit.slack.size() == pair.slack_count) {
        for (std::size_t index = 0; index < pair.slack_count; ++index) {
            const SlackExpect& want = pair.slack[index];
            const app::OutlineSlackEntry& got = edit.slack[index];
            const std::string at =
                "slack row " + std::to_string(index) + " (uid " +
                std::to_string(want.uid) + ")";
            check_size(got.uid, want.uid, (at + " uid").c_str());
            check_size(got.free_slack, want.free_slack,
                       (at + " free slack").c_str());
            check_size(got.start_slack, want.start_slack,
                       (at + " start slack").c_str());
            check_size(got.total_slack, want.total_slack,
                       (at + " total slack").c_str());
            check_size(app::packed_from_date32(got.late_start),
                       want.late_start_raw,
                       (at + " late start").c_str());
            check_size(app::packed_from_date32(got.late_finish),
                       want.late_finish_raw,
                       (at + " late finish").c_str());
        }
    }

    /* --- push + display + undo/redo -------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), "the outline edit is queued");
    app::PlanInput shown = edits.apply(plan);
    const app::PlanInput expected =
        app::plan_input_from_document(mpp::load_file(result_path.string()));
    check_display_matches(shown, expected, pair.slug, "apply");

    check(edits.undo(), "the outline edit undoes");
    check_display_matches(edits.apply(plan), plan, pair.slug, "undo");
    // The shell undoes by rebuilding from the DISPLAYED plan (shown), not from
    // the file plan: without_structural_edits() must reset the rows the undone
    // edit owned, or the summary/level/duration would stay in the sheet.
    check_display_matches(edits.apply(edits.without_structural_edits(shown, plan)), plan,
                          pair.slug, "undo from the displayed plan");
    check(edits.redo(), "the outline edit redoes");
    check_display_matches(edits.apply(plan), expected, pair.slug, "redo");
    check_display_matches(edits.apply(edits.without_structural_edits(shown, plan)),
                          expected, pair.slug, "redo from the displayed plan");

    /* --- save through the real cascade ----------------------------------- */
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) /
        (std::string("app-outline-") + pair.slug + ".mpp");
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        edits, {}, control_path.string(), out_path.string(), false);
    check(saved.ok, "the outline save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.outline_rows_written, 1,
               "the cascade wrote the outline edit");
    check(saved.warnings.empty(), "the outline save has no warnings");
    check(!std::filesystem::exists(out_path.string() + ".w51-structural-tmp"),
          "the save leaves no intermediate file");

    mppwrite::CfbDocument control;
    mppwrite::CfbDocument null_save;
    mppwrite::CfbDocument project;
    mppwrite::CfbDocument output;
    try {
        control = mppwrite::CfbDocument::from_file(control_path.string());
        null_save = mppwrite::CfbDocument::from_file(null_path.string());
        project = mppwrite::CfbDocument::from_file(result_path.string());
        output = mppwrite::CfbDocument::from_file(out_path.string());
    } catch (const std::exception& open_error) {
        check(false, "the control, the null save, the Project pair and the "
                     "port's save open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }

    /* --- the Task streams the writer owns, against PROJECT's file -------- */
    /* W29-0.1 (the wave-28 review's R28-1): wave 28 compared FixedMeta,
     * Fixed2Data and Fixed2Meta with the CONTROL, so Project's own changes to
     * them passed silently.  Every comparison below is Project's file. */
    const Bytes control_fixed = control.read_stream(task_stream("FixedData"));
    const Bytes project_fixed = project.read_stream(task_stream("FixedData"));
    const Bytes output_fixed = output.read_stream(task_stream("FixedData"));
    check(output_fixed == project_fixed,
          "Task FixedData equals Project's save byte for byte");

    const Bytes project_meta = project.read_stream(task_stream("FixedMeta"));
    const Bytes output_meta = output.read_stream(task_stream("FixedMeta"));
    check_fixed_meta(project_meta, output_meta, pair.slug);

    const Bytes project_f2m = project.read_stream(task_stream("Fixed2Meta"));
    const Bytes output_f2m = output.read_stream(task_stream("Fixed2Meta"));
    check(output_f2m == project_f2m,
          "Task Fixed2Meta equals Project's save byte for byte");

    const Bytes project_var_meta = project.read_stream(task_stream("VarMeta"));
    const Bytes output_var_meta = output.read_stream(task_stream("VarMeta"));
    check(output_var_meta == project_var_meta,
          "Task VarMeta equals Project's save byte for byte");

    check(output.read_stream(task_stream("Var2Data")) ==
              project.read_stream(task_stream("Var2Data")),
          "Task Var2Data equals Project's save byte for byte");

    const Bytes project_fixed2 = project.read_stream(task_stream("Fixed2Data"));
    const Bytes output_fixed2 = output.read_stream(task_stream("Fixed2Data"));
    const std::size_t rows =
        control_fixed.size() < mppwrite::task_row::kHeaderSize
            ? 0
            : (control_fixed.size() - mppwrite::task_row::kHeaderSize) /
                  mppwrite::task_row::kStride;
    check_size(output_fixed2.size(), project_fixed2.size(),
               "Task Fixed2Data size equals Project's");
    if (output_fixed2.size() == project_fixed2.size()) {
        check(canonical_task_fixed2(output_fixed2, rows) ==
                  canonical_task_fixed2(project_fixed2, rows),
              "Task Fixed2Data equals Project's after identity canonicalisation "
              "(+42 and +46 included)");
        check(canonical_task_fixed2(output_fixed2, rows) !=
                  canonical_task_fixed2(control.read_stream(task_stream("Fixed2Data")),
                                        rows),
              "Fixed2Data outline bytes actually changed");
    }

    /* --- the summary row's Task fields, each by name --------------------- */
    const std::size_t edited_row = uid_row(control_fixed, pair.anchor_uid);
    check(edited_row != kNoIndex, "the summary row is a saved row");
    if (edited_row != kNoIndex) {
        const std::size_t entry = meta_entry_for_row(project_meta, edited_row);
        check(entry != kNoIndex,
              "Project's save keeps the summary row's FixedMeta entry");
        if (entry != kNoIndex) {
            const std::size_t at = 16 + entry * 47;
            check_size(get_u16(output_meta, at + 2),
                       get_u16(project_meta, at + 2),
                       "summary FixedMeta +2 field count equals Project's");
            check(output_meta[at + 26] == project_meta[at + 26],
                  "summary FixedMeta +26 flag byte equals Project's");
            check(same_span(output_meta, project_meta, at + 8, 16),
                  "summary FixedMeta +8..23 blob equals Project's");
            if (pair.indent) {
                check((project_meta[at + 26] & 0x80u) != 0,
                      "Project's indent sets the summary bit");
            }
            const std::size_t f2m_at = 16 + entry * 96;
            if (f2m_at + 96 <= output_f2m.size() &&
                f2m_at + 96 <= project_f2m.size()) {
                check(output_f2m[f2m_at + 88] == project_f2m[f2m_at + 88],
                      "summary Fixed2Meta +88 leaf tail equals Project's");
                check(output_f2m[f2m_at + 95] == project_f2m[f2m_at + 95],
                      "summary Fixed2Meta +95 kind byte equals Project's");
            } else {
                check(false, "Fixed2Meta has the summary row's entry");
            }
        }
        const std::size_t f2_at = 64 * (edited_row + 3);
        if (f2_at + 64 <= output_fixed2.size() &&
            f2_at + 64 <= project_fixed2.size()) {
            check(output_fixed2[f2_at + 42] == project_fixed2[f2_at + 42],
                  "summary Fixed2Data +42 flags byte equals Project's");
            check(get_u32(output_fixed2, f2_at + 46) ==
                      get_u32(project_fixed2, f2_at + 46),
                  "summary Fixed2Data +46 start copy equals Project's");
        } else {
            check(false, "Fixed2Data has the summary row's record");
        }
    }

    /* --- the TBkndAssn streams the writer owns, against PROJECT's file --- */
    const Bytes control_assn_fixed = control.read_stream(assn_stream("FixedData"));
    const Bytes project_assn_fixed = project.read_stream(assn_stream("FixedData"));
    const Bytes output_assn_fixed = output.read_stream(assn_stream("FixedData"));
    check(output_assn_fixed == project_assn_fixed,
          "TBkndAssn FixedData equals Project's save byte for byte (the "
          "tombstone and the re-created row included)");

    const Bytes control_assn_meta = control.read_stream(assn_stream("FixedMeta"));
    const Bytes project_assn_meta = project.read_stream(assn_stream("FixedMeta"));
    const Bytes output_assn_meta = output.read_stream(assn_stream("FixedMeta"));
    check(output_assn_meta == project_assn_meta,
          "TBkndAssn FixedMeta equals Project's save byte for byte");

    const Bytes project_assn_f2m = project.read_stream(assn_stream("Fixed2Meta"));
    const Bytes output_assn_f2m = output.read_stream(assn_stream("Fixed2Meta"));
    check(output_assn_f2m == project_assn_f2m,
          "TBkndAssn Fixed2Meta equals Project's save byte for byte (+9 and "
          "+52 included)");

    const Bytes project_assn_var_meta =
        project.read_stream(assn_stream("VarMeta"));
    const Bytes output_assn_var_meta =
        output.read_stream(assn_stream("VarMeta"));
    check(output_assn_var_meta == project_assn_var_meta,
          "TBkndAssn VarMeta equals Project's save byte for byte");

    check(output.read_stream(assn_stream("Var2Data")) ==
              project.read_stream(assn_stream("Var2Data")),
          "TBkndAssn Var2Data equals Project's save byte for byte");

    const Bytes project_assn_fixed2 =
        project.read_stream(assn_stream("Fixed2Data"));
    const Bytes output_assn_fixed2 =
        output.read_stream(assn_stream("Fixed2Data"));
    const Bytes control_task_fixed2 =
        control.read_stream(task_stream("Fixed2Data"));
    check_size(output_assn_fixed2.size(), project_assn_fixed2.size(),
               "TBkndAssn Fixed2Data size equals Project's");
    if (output_assn_fixed2.size() == project_assn_fixed2.size()) {
        check(canonical_assn_fixed2(
                  output_assn_fixed2, output_assn_fixed2.size() / kAssnFixed2Record,
                  output_fixed2, rows, control_task_fixed2, rows) ==
                  canonical_assn_fixed2(
                      project_assn_fixed2,
                      project_assn_fixed2.size() / kAssnFixed2Record,
                      project_fixed2, rows, control_task_fixed2, rows),
              "TBkndAssn Fixed2Data equals Project's after identity "
              "canonicalisation (stale parent links resolved through the "
              "control's task identities)");
    }

    /* --- the assignment row the edit touched, each field by name --------- */
    const std::size_t anchor_assn =
        assn_uid_row(control_assn_fixed, pair.anchor_uid);
    check(anchor_assn != kNoIndex,
          "the summary row's assignment is a saved row");
    check_size(get_u32(output_assn_meta, 4), get_u32(project_assn_meta, 4),
               "TBkndAssn FixedMeta +4 header word equals Project's");
    check_size(get_u32(project_assn_meta, 4), 6,
               "Project's save leaves the table's tombstone word at 6");
    if (anchor_assn != kNoIndex) {
        /* Both indent pairs tombstone this row; the outdent control already
         * carries it tombstoned, so the state checks hold for every pair. */
        const std::size_t aentry =
            assn_meta_entry_for_row(project_assn_meta, anchor_assn);
        check(aentry != kNoIndex,
              "Project's save keeps the assignment's FixedMeta entry");
        if (aentry != kNoIndex) {
            const std::size_t aat = kAssnMetaHeader + aentry * kAssnMetaEntry;
            check(output_assn_meta[aat] == project_assn_meta[aat],
                  "the tombstoned assignment's FixedMeta state equals "
                  "Project's");
            check_size(project_assn_meta[aat], 0x02,
                       "Project's save leaves the assignment tombstoned");
            check_size(get_u16(output_assn_meta, aat + 2),
                       get_u16(project_assn_meta, aat + 2),
                       "assignment FixedMeta +2 field count equals Project's");
            check_size(get_u16(project_assn_meta, aat + 2), 0,
                       "the tombstoned assignment's field count is 0");
            check_size(get_u32(project_assn_meta, aat + 4),
                       static_cast<std::uint32_t>(kAssnStride * anchor_assn),
                       "the tombstoned entry still names its FixedData row");
        }
        const std::size_t af2e =
            assn_fixed2_meta_entry_for_row(project_assn_f2m, anchor_assn);
        check(af2e != kNoIndex,
              "Project's save keeps the assignment's Fixed2Meta entry");
        if (af2e != kNoIndex) {
            const std::size_t at = 16 + af2e * kAssnFixed2MetaEntry;
            check(output_assn_f2m[at + 9] == project_assn_f2m[at + 9],
                  "tombstoned assignment Fixed2Meta +9 equals Project's");
            check(output_assn_f2m[at + 52] == project_assn_f2m[at + 52],
                  "tombstoned assignment Fixed2Meta +52 equals Project's");
        }
        check(var_keys_for_uid(output_assn_var_meta, pair.anchor_uid).empty(),
              "the tombstoned assignment's VarMeta entries are gone");
        check(var_keys_for_uid(project_assn_var_meta, pair.anchor_uid).empty(),
              "Project's save removes the tombstoned assignment's VarMeta "
              "entries");
    }

    /* The outdent re-creates the former summary's assignment: a new row at the
     * end of FixedData with a fresh uid, the tombstoned row's words, and four
     * Var2 blocks (0F400031, 0F40027A, 0F400299, 0F40029B). */
    if (!pair.indent && anchor_assn != kNoIndex) {
        const std::size_t project_rows = project_assn_fixed.size() / kAssnStride;
        check_size(project_rows, control_assn_fixed.size() / kAssnStride + 1,
                   "Project's outdent appends one assignment row");
        if (project_rows > 0) {
            const std::size_t new_row = project_rows - 1;
            const std::size_t base = kAssnStride * new_row;
            std::uint32_t max_uid = 0;
            for (std::size_t row = 0;
                 row < control_assn_fixed.size() / kAssnStride; ++row) {
                max_uid = std::max(max_uid,
                                   get_u32(control_assn_fixed, kAssnStride * row));
            }
            check_size(get_u32(project_assn_fixed, base + 4), pair.anchor_uid,
                       "Project's outdent re-creates the former summary's "
                       "assignment");
            check_size(get_u32(output_assn_fixed, base), get_u32(project_assn_fixed, base),
                       "the re-created assignment's uid equals Project's");
            check_size(get_u32(project_assn_fixed, base), max_uid + 1u,
                       "Project allocates max uid + 1");
            check(get_u32(output_assn_fixed, base + 20) ==
                      get_u32(project_assn_fixed, base + 20),
                  "the re-created assignment's planned work equals Project's");
            check(get_u32(project_assn_fixed, base + 20) ==
                      get_u32(control_assn_fixed, kAssnStride * anchor_assn + 20),
                  "Project copies the tombstoned row's planned work");
            const std::size_t new_entry =
                assn_meta_entry_for_row(project_assn_meta, new_row);
            check(new_entry != kNoIndex,
                  "Project's save gives the re-created row a FixedMeta entry");
            if (new_entry != kNoIndex) {
                const std::size_t at =
                    kAssnMetaHeader + new_entry * kAssnMetaEntry;
                check_size(output_assn_meta[at], 0x00,
                           "the re-created assignment is live");
                check_size(get_u16(output_assn_meta, at + 2), 4,
                           "the re-created entry's field count is 4");
                check(output_assn_meta[at] == project_assn_meta[at] &&
                          get_u16(output_assn_meta, at + 2) ==
                              get_u16(project_assn_meta, at + 2),
                      "the re-created entry's state and count equal Project's");
            }
            const std::size_t new_f2e =
                assn_fixed2_meta_entry_for_row(project_assn_f2m, new_row);
            check(new_f2e != kNoIndex,
                  "Project's save gives the re-created row a Fixed2Meta entry");
            if (new_f2e != kNoIndex) {
                const std::size_t at = 16 + new_f2e * kAssnFixed2MetaEntry;
                check_size(output_assn_f2m[at + 9], 0x00,
                           "the re-created Fixed2Meta +9 is the new-row bit");
                check_size(output_assn_f2m[at + 52], 0x28,
                           "the re-created Fixed2Meta +52 is 0x28");
                check(output_assn_f2m[at + 9] == project_assn_f2m[at + 9] &&
                          output_assn_f2m[at + 52] == project_assn_f2m[at + 52],
                      "the re-created Fixed2Meta bytes equal Project's");
            }
            const std::uint32_t new_uid = get_u32(output_assn_fixed, base);
            const std::vector<std::uint32_t> keys =
                var_keys_for_uid(output_assn_var_meta, new_uid);
            check_size(keys.size(), 4,
                       "the re-created assignment carries four Var2 blocks");
            check(keys == var_keys_for_uid(project_assn_var_meta, new_uid),
                  "the re-created assignment's field keys equal Project's");
            check(std::is_sorted(keys.begin(), keys.end()),
                  "the re-created assignment's field keys ascend");
        }
    }

    /* --- 114/Props through the committed null save ----------------------- */
    const Bytes control_props = control.read_stream("114/Props");
    const Bytes project_props = project.read_stream("114/Props");
    const Bytes output_props = output.read_stream("114/Props");
    check_size(output_props.size(), project_props.size(),
               "Props keeps Project's length");
    if (output_props.size() == project_props.size()) {
        const std::uint32_t kCounters[] = {0x01000002u, 0x01000100u,
                                           0x04000001u, 0x04000002u,
                                           0x04000100u, 0x04008000u};
        for (const std::uint32_t tag : kCounters) {
            const std::size_t ours = props_scalar_value(output_props, tag);
            const std::size_t theirs = props_scalar_value(project_props, tag);
            check(ours != kNoIndex && theirs != kNoIndex,
                  ("Props carries the " + hex_tag(tag) + " record").c_str());
            if (ours != kNoIndex && theirs != kNoIndex) {
                check(get_u32(output_props, ours) == get_u32(project_props, theirs),
                      ("Props " + hex_tag(tag) +
                       " counter equals Project's").c_str());
            }
        }
    }
    /* W29-1.0b: the two per-save identity GUID records differ between the
     * control and Project's no-edit save; the 16-byte block mask hides exactly
     * those.  Every other Props byte must equal Project's edit save. */
    const Bytes null_props = null_save.read_stream("114/Props");
    const save_pair::Verdict props_verdict = save_pair::compare(
        save_pair::Kind::fixed_block, control_props, null_props, project_props,
        output_props);
    check(props_verdict.lengths_ok,
          "Props has equal control and null-save lengths");
    check(props_verdict.mismatches == 0,
          mismatch_message("114/Props", props_verdict).c_str());
    report_stream("114/Props", save_pair::Kind::fixed_block, props_verdict);
    std::size_t total_noise = props_verdict.noise;
    std::size_t total_effects = props_verdict.effects;

    /* --- every other stream, through the committed null save ------------- */
    const std::vector<std::string> control_path_list = control.stream_paths();
    const std::vector<std::string> output_path_list = output.stream_paths();
    std::set<std::string> const control_paths(control_path_list.begin(),
                                              control_path_list.end());
    std::set<std::string> const output_paths(output_path_list.begin(),
                                             output_path_list.end());
    check(control_paths == output_paths,
          "the save adds and removes no stream");
    std::set<std::string> all_paths = control_paths;
    const std::vector<std::string> result_path_list = project.stream_paths();
    all_paths.insert(result_path_list.begin(), result_path_list.end());
    const std::vector<std::string> null_path_list = null_save.stream_paths();
    all_paths.insert(null_path_list.begin(), null_path_list.end());
    for (const std::string& path : all_paths) {
        if (owned_stream(path)) {
            continue;
        }
        const save_pair::Kind kind = kind_for_path(path);
        const save_pair::Verdict verdict = save_pair::compare(
            kind, stream_or_empty(control, path),
            stream_or_empty(null_save, path), stream_or_empty(project, path),
            stream_or_empty(output, path));
        report_stream(path, kind, verdict);
        total_noise += verdict.noise;
        total_effects += verdict.effects;
        if (kind == save_pair::Kind::exact) {
            check(verdict.noise_free,
                  (path + ": the null save leaves it byte for byte equal to "
                          "the control").c_str());
        } else {
            check(verdict.lengths_ok,
                  (path + ": control and null save have the measured lengths")
                      .c_str());
        }
        check(verdict.mismatches == 0,
              mismatch_message(path, verdict).c_str());
    }
    std::printf("   pair totals: noise=%llu effects=%llu\n",
                static_cast<unsigned long long>(total_noise),
                static_cast<unsigned long long>(total_effects));

    check(mppwrite::check_table(output).empty(),
          "the saved task table passes check_table");

    /* --- the reader and the display agree with Project's save ------------ */
    try {
        const mpp::Document reread_port = mpp::load_file(out_path.string());
        const mpp::Document reread_project = mpp::load_file(result_path.string());
        for (std::uint32_t uid = 1; uid <= 3; ++uid) {
            const mpp::Task* ours = find_task(reread_port, uid);
            const mpp::Task* theirs = find_task(reread_project, uid);
            check(ours != nullptr && theirs != nullptr,
                  "both saved files have the measured task");
            if (ours == nullptr || theirs == nullptr) {
                continue;
            }
            check_size(ours->outline_level, theirs->outline_level,
                       "the saved file reads back Project's outline level");
            check(ours->outline_number == theirs->outline_number,
                  "the saved file reads back Project's OutlineNumber");
            check(ours->summary == theirs->summary,
                  "the saved file reads back Project's summary flag");
            check_size(ours->child_count, theirs->child_count,
                       "the saved file reads back Project's child count");
            check_size(ours->duration_ticks, theirs->duration_ticks,
                       "the saved file reads back Project's duration");
            check(ours->start.raw == theirs->start.raw,
                  "the saved file reads back Project's +104 start");
            check(ours->finish.raw == theirs->finish.raw,
                  "the saved file reads back Project's +108 finish");
        }
        /* The display edit_model showed is the file the reader shows. */
        const app::PlanInput shown_again = edits.apply(plan);
        const app::PlanInput reread_plan =
            app::plan_input_from_document(reread_port);
        check_display_matches(shown_again, reread_plan, pair.slug, "reread");
        check_display_matches(reread_plan,
                              app::plan_input_from_document(reread_project),
                              pair.slug, "project");
    } catch (const std::exception& read_error) {
        check(false, "the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* W29-0.0: write a copy of `source` with the Fixed2Meta manual bit (entry +8,
 * 0x80) of `uid` cleared, so make_outline_edit's file-side scheduling refusal
 * can be tested without waiting for a Project auto-scheduled pair.  The
 * FixedMeta/Fixed2Meta pairing is the one mppwrite uses (47-byte entry +4
 * names the FixedData row offset; the same index in the 96-byte Fixed2Meta). */
/* W30-0.1: write a copy of `source` whose TBkndAssn row for `uid` names a REAL
 * resource (FixedData +8 = 7) instead of Project's 0xFFFF0001 placeholder.  The
 * plan loaded from the untouched control keeps saying "placeholder", so the
 * edit model accepts the indent and only the writer's defence-in-depth guard
 * can refuse it at save time. */
bool write_real_assignment_copy(const std::filesystem::path& source,
                                const std::filesystem::path& target,
                                std::uint32_t uid, std::string& failure) {
    constexpr std::size_t kAssnResource = 8;
    constexpr std::uint32_t kRealResource = 7;
    try {
        mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(source.string());
        Bytes fixed = document.read_stream(assn_stream("FixedData"));
        const std::size_t row = assn_uid_row(fixed, uid);
        if (row == kNoIndex) {
            failure = "the control has no assignment row for uid " +
                      std::to_string(uid);
            return false;
        }
        put_u32(fixed, kAssnStride * row + kAssnResource, kRealResource);
        document.replace_stream(assn_stream("FixedData"), fixed);
        std::error_code remove_error;
        std::filesystem::remove(target, remove_error);
        document.save(target.string());
        return true;
    } catch (const std::exception& error) {
        failure = error.what();
        return false;
    }
}

bool write_auto_row_copy(const std::filesystem::path& source,
                         const std::filesystem::path& target, std::uint32_t uid,
                         std::string& failure) {
    constexpr std::size_t kFixed2MetaEntry = 96;
    constexpr std::size_t kFixed2MetaFlags = 8;
    try {
        mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(source.string());
        const Bytes fixed_data = document.read_stream(task_stream("FixedData"));
        const Bytes meta = document.read_stream(task_stream("FixedMeta"));
        Bytes fixed2_meta = document.read_stream(task_stream("Fixed2Meta"));
        const std::size_t row = uid_row(fixed_data, uid);
        const std::size_t entry =
            row == kNoIndex ? kNoIndex : meta_entry_for_row(meta, row);
        if (row == kNoIndex || entry == kNoIndex ||
            fixed2_meta.size() < 16 + entry * kFixed2MetaEntry + kFixed2MetaFlags + 1) {
            failure = "the control has no paired metadata entry for uid " +
                      std::to_string(uid);
            return false;
        }
        fixed2_meta[16 + entry * kFixed2MetaEntry + kFixed2MetaFlags] &=
            static_cast<std::uint8_t>(~0x80u);
        document.replace_stream(task_stream("Fixed2Meta"), fixed2_meta);
        std::error_code remove_error;
        std::filesystem::remove(target, remove_error);
        document.save(target.string());
        return true;
    } catch (const std::exception& error) {
        failure = error.what();
        return false;
    }
}

void check_refusals() {
    const std::filesystem::path indent_control =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        "wave28-outline-indent-control.mpp";
    const std::filesystem::path outdent_control =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        "wave28-outline-outdent-control.mpp";
    if (!std::filesystem::exists(indent_control) ||
        !std::filesystem::exists(outdent_control)) {
        std::printf("SKIP: the refusal controls are not present\n");
        return;
    }
    app::PlanInput indent_plan;
    app::PlanInput outdent_plan;
    try {
        indent_plan =
            app::plan_input_from_document(mpp::load_file(indent_control.string()));
        outdent_plan = app::plan_input_from_document(
            mpp::load_file(outdent_control.string()));
    } catch (const std::exception& error) {
        check(false, "the refusal controls load");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    app::StructuralEdit edit;
    std::string error;

    check(!app::make_outline_edit(indent_plan, indent_control.string(), 1, true,
                                 edit, error),
          "the first top-level row has no preceding sibling to indent under");
    check(!error.empty(), "the refusal names its reason");

    check(!app::make_outline_edit(indent_plan, indent_control.string(), 1, false,
                                  edit, error),
          "a top-level leaf cannot be outdented");
    check(!app::make_outline_edit(outdent_plan, outdent_control.string(), 1,
                                  false, edit, error),
          "a summary cannot be outdented");
    check(!app::make_outline_edit(outdent_plan, outdent_control.string(), 2,
                                  true, edit, error),
          "indenting under an existing summary is not measured");
    check(!app::make_outline_edit(indent_plan, indent_control.string(), 99, true,
                                  edit, error),
          "an unknown uid is refused");

    // W29-0.4 (R28-4, High): the measured automatic shapes are the four wave-29
    // pairs above.  A MIXED indent -- the moved row and the row that would
    // become its summary scheduled differently -- is still refused: no Project
    // save pair measures it.  The file's Fixed2Meta bit is the authority; these
    // copies clear it without touching the committed pairs.
    std::error_code out_error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        out_error);
    std::string patch_failure;
    const std::filesystem::path auto_moved_path =
        std::filesystem::path(APP_TEST_OUT_DIR) / "w29-outline-auto-moved.mpp";
    check(write_auto_row_copy(indent_control, auto_moved_path, 2, patch_failure),
          "the automatic moved-row copy is written");
    if (std::filesystem::exists(auto_moved_path)) {
        app::PlanInput auto_plan;
        try {
            auto_plan =
                app::plan_input_from_document(mpp::load_file(auto_moved_path.string()));
        } catch (const std::exception& load_error) {
            check(false, "the automatic moved-row copy loads");
            std::printf("LOAD ERROR: %s\n", load_error.what());
        }
        check(!app::make_outline_edit(auto_plan, auto_moved_path.string(), 2, true,
                                      edit, error),
              "a mixed-scheduling indent is refused");
        check(error.find("scheduled differently") != std::string::npos,
              "the mixed-indent refusal names the scheduling difference");
    }

    const std::filesystem::path auto_parent_path =
        std::filesystem::path(APP_TEST_OUT_DIR) / "w29-outline-auto-parent.mpp";
    check(write_auto_row_copy(outdent_control, auto_parent_path, 1, patch_failure),
          "the automatic outdent-parent copy is written");
    if (std::filesystem::exists(auto_parent_path)) {
        app::PlanInput auto_plan;
        try {
            auto_plan =
                app::plan_input_from_document(mpp::load_file(auto_parent_path.string()));
        } catch (const std::exception& load_error) {
            check(false, "the automatic outdent-parent copy loads");
            std::printf("LOAD ERROR: %s\n", load_error.what());
        }
        check(!app::make_outline_edit(auto_plan, auto_parent_path.string(), 2, false,
                                      edit, error),
              "outdent from an automatically scheduled parent is refused");
        check(error.find("automatically scheduled") != std::string::npos,
              "the automatic-parent refusal names the scheduling mode");
    }

    // W29-0.0: the new summary (the indent's preceding sibling) must not carry
    // progress, a resource-loaded work assignment or a queued actuals edit.
    app::PlanInput progressed = indent_plan;
    for (app::TaskRow& task : progressed.tasks) {
        if (task.uid == 1) {
            task.percent_complete = 40;
        }
    }
    check(!app::make_outline_edit(progressed, indent_control.string(), 2, true, edit,
                                  error),
          "indent under a row with progress is refused");
    check(error.find("progress") != std::string::npos,
          "the progress refusal names the progress");

    app::PlanInput working = indent_plan;
    app::AssignmentRow work;
    work.uid = 500;
    work.task_uid = 1;
    work.resource_uid = 100;
    work.work_ticks = 4800;
    working.assignments.push_back(work);
    check(!app::make_outline_edit(working, indent_control.string(), 2, true, edit,
                                  error),
          "indent under a work-bearing row is refused");
    check(error.find("carries work") != std::string::npos,
          "the work refusal names the assignment");

    // W30-0.1 (R29-1, Critical): the indent tombstones the new summary's one
    // assignment, so a REAL assignment -- including a work resource at zero
    // work, which the "carries work" rule above does not see -- is refused
    // before the save can delete it.
    app::PlanInput real_zero = indent_plan;
    for (app::AssignmentRow& assignment : real_zero.assignments) {
        if (assignment.task_uid == 1) {
            assignment.resource_uid = 7;
            assignment.work_ticks = 0;
        }
    }
    check(!app::make_outline_edit(real_zero, indent_control.string(), 2, true,
                                  edit, error),
          "indent under a real zero-work assignment is refused");
    check(error.find("real resource assignment") != std::string::npos,
          "the real-assignment refusal names the assignment kind");

    app::PlanInput no_assignment = indent_plan;
    no_assignment.assignments.erase(
        std::remove_if(no_assignment.assignments.begin(),
                       no_assignment.assignments.end(),
                       [](const app::AssignmentRow& assignment) {
                           return assignment.task_uid == 1;
                       }),
        no_assignment.assignments.end());
    check(!app::make_outline_edit(no_assignment, indent_control.string(), 2, true,
                                  edit, error),
          "indent under a row with no assignment row is refused");
    check(error.find("assignment rows") != std::string::npos,
          "the no-assignment refusal names the assignment table");

    check(!app::make_outline_edit(indent_plan, indent_control.string(), 2, true, edit,
                                  error, {1u}),
          "indent under a row with a pending actuals edit is refused");
    check(error.find("pending actuals") != std::string::npos,
          "the pending-actuals refusal names the queued edit");

    /* push() re-checks the plan it is given: no plan, a wrong anchor, and a
     * pending delete of either row. */
    app::StructuralEdit good;
    check(app::make_outline_edit(indent_plan, indent_control.string(), 2, true,
                                 good, error),
          "the indent builds for the push checks");
    app::StructuralEdits no_plan;
    check(!no_plan.push(good, nullptr),
          "push refuses an outline edit without the plan");
    app::StructuralEdit wrong_anchor = good;
    wrong_anchor.anchor_uid = 3;
    app::StructuralEdits wrong;
    check(!wrong.push(wrong_anchor, &indent_plan),
          "push refuses an anchor that is not the row above");
    app::StructuralEdit remove_anchor;
    remove_anchor.kind = app::StructuralKind::delete_task;
    remove_anchor.uid = 1;
    app::StructuralEdits deleted_anchor;
    check(deleted_anchor.push(remove_anchor, &indent_plan),
          "the summary row delete is queued");
    check(!deleted_anchor.push(good, &indent_plan),
          "an outline edit naming a pending-deleted row is refused");

    /* Deleting either row of a queued outline edit displaces it; undo puts it
     * back. */
    app::StructuralEdits displaced;
    check(displaced.push(good, &indent_plan), "the outline edit is queued first");
    check(displaced.push(remove_anchor, &indent_plan),
          "the summary delete is queued over it");
    check_size(displaced.size(), 1, "the delete displaced the outline edit");
    if (displaced.size() == 1) {
        check_size(displaced.applied().front().displaced.size(), 1,
                   "the delete keeps the displaced outline edit");
    }
    check(displaced.undo(), "the delete undoes");
    check_size(displaced.size(), 1, "undo restores the displaced outline edit");
    if (displaced.size() == 1) {
        check(displaced.applied().front().kind ==
                  app::StructuralKind::outline_indent,
              "the restored edit is the outline indent");
    }

    /* W30-0.1 (R29-1, Critical): the writer's own guard.  The plan loaded from
     * the untouched control says "placeholder", so the edit model and push()
     * accept the indent; the SOURCE file the save reads has an assignment row
     * that names a real resource.  The writer must refuse the tombstone and
     * leave the assignment in the output. */
    const std::filesystem::path real_assignment_path =
        std::filesystem::path(APP_TEST_OUT_DIR) / "w30-outline-real-assignment.mpp";
    std::string copy_failure;
    check(write_real_assignment_copy(indent_control, real_assignment_path, 1,
                                     copy_failure),
          "the real-assignment source copy is written");
    if (std::filesystem::exists(real_assignment_path)) {
        app::StructuralEdit real_edit;
        check(app::make_outline_edit(indent_plan, real_assignment_path.string(), 2,
                                     true, real_edit, error),
              "the indent builds against the unpatched plan");
        app::StructuralEdits real_edits;
        check(real_edits.push(real_edit, &indent_plan),
              "the indent is queued against the unpatched plan");
        const std::filesystem::path real_out =
            std::filesystem::path(APP_TEST_OUT_DIR) /
            "w30-outline-real-assignment-out.mpp";
        std::error_code remove_error;
        std::filesystem::remove(real_out, remove_error);
        const app::StructuralSaveResult real_saved = app::save_structural_edits(
            real_edits, {}, real_assignment_path.string(), real_out.string(),
            false);
        check(real_saved.ok, "the save completes with the indent refused");
        check_size(real_saved.outline_rows_written, 0,
                   "the writer refused to tombstone the real assignment");
        bool named_placeholder = false;
        for (const std::string& warning : real_saved.warnings) {
            if (warning.find("0xffff0001") != std::string::npos) {
                named_placeholder = true;
            }
        }
        check(named_placeholder,
              "the writer's refusal names Project's placeholder resource");
        if (std::filesystem::exists(real_out)) {
            try {
                mppwrite::CfbDocument out_document =
                    mppwrite::CfbDocument::from_file(real_out.string());
                const Bytes out_fixed =
                    out_document.read_stream(assn_stream("FixedData"));
                const std::size_t row = assn_uid_row(out_fixed, 1);
                check(row != kNoIndex &&
                          get_u32(out_fixed, kAssnStride * row + 8) == 7,
                      "the real assignment is still in the saved file");
            } catch (const std::exception& open_error) {
                check(false, "the refused save reopens");
                std::printf("OPEN ERROR: %s\n", open_error.what());
            }
        }
    }
}

} /* namespace */

int main() {
    for (const PairCase& pair : kPairs) {
        check_pair(pair);
    }
    check_refusals();
    std::printf("app_edits_outline_tests: %d checks passed\n",
                g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_outline_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
