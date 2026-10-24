// app/app_edits_actuals_tests.cpp -- the actuals writer against Project's own
// save pairs (wave 28, W28-1.2 and W28-1.3; the wave-29 shapes, W29-0.2).
//
// For each committed pair (tests/data/wave28-percent-*.mpp,
// wave28-mark-*.mpp, wave28-update*.mpp and wave29-percent-*.mpp -- neutral
// metadata and the neutral C:/w28 and C:/w29 drive paths, see
// tests/data/README.md) this test:
//
//   1. loads the pair's CONTROL through the app's reader adapter, builds the
//      edit through make_percent_edit() (a % edit) or
//      make_update_actuals_edit() (Mark on Track / Update Project, W28-1.3),
//      queues it with push() and saves it with the real save cascade into
//      APP_TEST_OUT_DIR;
//   2. proves every stream the writer owns matches PROJECT's own save, field by
//      field (W29-0.1, the wave-28 review's R28-1: wave 28 compared these with
//      the CONTROL, so Project's own changes passed silently):
//        Task FixedData, FixedMeta, Fixed2Meta  byte-identical to the pair;
//        Task VarMeta  byte-identical (the LOGICAL entry array: header count,
//                   entries, and the stream's original bytes past that count);
//        Task Var2Data  byte-identical after masking the opaque per-session ids
//                   of the 0x0B40015E steps (actuals.h explains why they cannot
//                   be reproduced byte for byte);
//        Task Fixed2Data  byte-identical after canonicalising the per-save
//                   identity GUIDs (every other byte, including +42 and +46,
//                   must match);
//        TBkndAssn FixedData, FixedMeta, Fixed2Meta, VarMeta and Var2Data
//                   byte-identical (Fixed2Data after identity canonicalisation);
//        114/Props  compared through the committed null save (W29-1.0b): its
//                   two per-save identity GUID records are masked as noise,
//                   every other byte must equal Project's edit save, and the
//                   four counters are ALSO checked by name;
//        each owned row field is ALSO checked by name against Project's file:
//        FixedMeta +2/+44 and the +8..23 blob, Fixed2Meta +88/+95, Fixed2Data
//        +42/+46, the assignment's actual/remaining/resume/stop words and its
//        metadata words, and the four 114/Props counters.
//   3. compares EVERY OTHER STREAM THROUGH THE COMMITTED NULL SAVE (W29-1.0b,
//      the R28-1 residual): Project opened each control and saved it with NO
//      edit; a byte that differs between the control and that null save is
//      per-save noise and is masked by a named rule (16-byte block for
//      fixed-length streams, per-byte after normalising Props14 and
//      214/CEdl/Var2Data; DocumentSummaryInformation has no measured noise and
//      is exact).  Every unmasked byte that differs between the null save and
//      Project's edit save is the edit's EFFECT and must equal the port's save.
//      The test prints each stream's noise and effect counts and the pair
//      totals; a mismatch is a FAILING check, so a shape the port cannot
//      reproduce cannot hide behind an expected failure.
//   4. reopens the port's save and proves the reader agrees with Project's
//      percent and status date.
//
// The null saves are committed as tests/data/<control stem>-null.mpp; a missing
// one FAILS rather than degrading the discipline.
//
// The dates are not hand-fed: the save cascade computes them from the file's
// own calendar (core/cal), which is what makes this a test of the whole path
// rather than of write_percent_actuals() alone.  percent_through() -- the
// date-update arithmetic -- is also checked directly against the measured
// update pair (50/83/100 % from 12000/12000/9600 elapsed ticks).
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\vs-w28 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\vs-w28 --config Release
//   ctest --test-dir build\vs-w28 -C Release -R "^app_edits_actuals$" --output-on-failure

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "actuals.h"
#include "cal.h"
#include "cfb_writer.h"
#include "date.h"
#include "edit_model.h"
#include "layout_model.h"
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
/* 114/Props record 0x02400045, kind 4, one packed date. */
constexpr std::uint32_t kStatusDateRecord = 0x02400045u;
/* The three W28-1.3 pairs update through Wed 2026-01-07 00:00. */
constexpr std::uint32_t kThroughRaw = 0x3BF40000u;

int g_checks = 0;
int g_failures = 0;
int g_expected_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

/* W29-0.1 (the wave-28 review's R28-1): a stream or field Project changes and
 * the port does not reproduce is a NAMED EXPECTED FAILURE, the W25-5 pattern.
 * `project_truth` is what Project's file shows; while the gap is open the call
 * prints EXPECTED FAIL and the test stays green, and the day the port
 * reproduces Project the same call FAILS so the list cannot rot. */
void check_expected_failure(bool project_truth, const char* what) {
    if (project_truth) {
        ++g_failures;
        std::printf("FAIL: expected failure '%s' now PASSES -- remove it from "
                    "the expected-failure list and make it a normal check\n",
                    what);
        return;
    }
    ++g_expected_failures;
    std::printf("EXPECTED FAIL: %s\n", what);
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
        message += " (first of " + std::to_string(verdict.mismatches) +
                   " at byte " + std::to_string(verdict.first_mismatch) +
                   ": port 0x";
        char hex[8];
        std::snprintf(hex, sizeof(hex), "%02x", verdict.output_byte);
        message += hex;
        message += ", Project 0x";
        std::snprintf(hex, sizeof(hex), "%02x", verdict.result_byte);
        message += hex;
        message += ")";
    }
    return message;
}

/* The W29-1.0b report: per stream, how many bytes are per-save noise and how
 * many are the edit's effect. */
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

std::uint16_t get_u16(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t get_u32(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::string task_stream(const char* tail) {
    return std::string("114/TBkndTask/") + tail;
}

std::size_t uid_row(const Bytes& fixed_data, std::uint32_t uid) {
    if (fixed_data.size() < 48) {
        return kNoIndex;
    }
    const std::size_t rows = (fixed_data.size() - 48) / 202;
    for (std::size_t row = 0; row < rows; ++row) {
        if (get_u32(fixed_data, 48 + 202 * row + 4) == uid) {
            return row;
        }
    }
    return kNoIndex;
}

/* Zero the u32 opaque token at the end of every 0x0B40015E step in a copy of
 * Var2Data.  The writer's save pairs then compare exactly: flags, percents and
 * everything else are still compared byte for byte. */
void mask_progress_ids(Bytes& var2, const Bytes& var_meta) {
    std::vector<mppwrite::VarEntry> entries;
    try {
        entries = mppwrite::read_var_entries(var_meta);
    } catch (const std::exception&) {
        return; /* the caller's equality check will report the damage */
    }
    for (const mppwrite::VarEntry& entry : entries) {
        if (entry.field_id != 0x0B40015Eu) {
            continue;
        }
        const std::size_t payload = static_cast<std::size_t>(entry.offset) + 4;
        if (payload + 16 > var2.size()) {
            continue;
        }
        const std::size_t count = get_u16(var2, payload);
        if (payload + 16 + 20 * count > var2.size()) {
            continue;
        }
        for (std::size_t step = 0; step < count; ++step) {
            const std::size_t id = payload + 16 + 20 * step + 16;
            std::fill(var2.begin() + static_cast<std::ptrdiff_t>(id),
                      var2.begin() + static_cast<std::ptrdiff_t>(id + 4),
                      std::uint8_t{0});
        }
    }
}

/* Index of the FixedMeta 47-byte entry that names this FixedData row. */
std::size_t meta_entry_for_row(const Bytes& meta, std::size_t row) {
    if (meta.size() < 16) {
        return kNoIndex;
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(48 + 202 * row);
    for (std::size_t i = 0; i < (meta.size() - 16) / 47; ++i) {
        if (get_u32(meta, 16 + i * 47 + 4) == offset) {
            return i;
        }
    }
    return kNoIndex;
}

/* Payload offset of the project status-date record in a 114/Props stream, or
 * kNoIndex when the stream or the record is absent or malformed. */
std::size_t status_date_payload(const Bytes& props) {
    if (props.size() < 16) {
        return kNoIndex;
    }
    std::size_t offset = 16;
    while (offset + 12 <= props.size()) {
        const std::uint32_t length = get_u32(props, offset);
        const std::uint32_t id = get_u32(props, offset + 4);
        if ((id & 0x00FF0000u) != 0x00400000u) {
            break;
        }
        if (static_cast<std::uint64_t>(offset) + 12u + length >
            static_cast<std::uint64_t>(props.size())) {
            return kNoIndex;
        }
        if (id == kStatusDateRecord && length >= 4u) {
            return offset + 12;
        }
        offset += 12u + length;
    }
    return kNoIndex;
}

/* The 16-byte scalar record framing actuals.cpp writes: u32 4 / big-endian tag
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

std::string hex_tag(std::uint32_t tag) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%08x", tag);
    return buffer;
}

bool same_span(const Bytes& a, const Bytes& b, std::size_t at, std::size_t n) {
    if (at + n > a.size() || at + n > b.size()) {
        return false;
    }
    return std::equal(a.begin() + static_cast<std::ptrdiff_t>(at),
                      a.begin() + static_cast<std::ptrdiff_t>(at + n),
                      b.begin() + static_cast<std::ptrdiff_t>(at));
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

Identity row_identity(std::size_t row) {
    Identity replacement{};
    replacement[0] = static_cast<std::uint8_t>(row & 0xFFu);
    replacement[1] = static_cast<std::uint8_t>((row >> 8) & 0xFFu);
    replacement[2] = static_cast<std::uint8_t>((row >> 16) & 0xFFu);
    replacement[3] = static_cast<std::uint8_t>((row >> 24) & 0xFFu);
    return replacement;
}

/* Canonicalise the per-save identity GUIDs of the Task Fixed2Data: each data
 * record's +0..15 becomes a canonical id (the row index in its low u32) and
 * each parent link (+24..39) becomes the parent's canonical id.  Project
 * allocates fresh identities on every save; every other byte must match. */
Bytes canonical_task_fixed2(const Bytes& fixed2, std::size_t rows) {
    constexpr std::size_t kRecord = 64;
    constexpr std::size_t kParent = 24;
    if (fixed2.size() != kRecord * (rows + 3)) {
        return {};  /* the caller's size check reports the damage */
    }
    std::map<Identity, Identity> canonical;
    for (std::size_t row = 0; row < rows; ++row) {
        const Identity id = read_identity(fixed2, kRecord * (row + 3));
        if (!identity_empty(id)) {
            canonical.emplace(id, row_identity(row));
        }
    }
    Bytes out = fixed2;
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t base = kRecord * (row + 3);
        const Identity id = read_identity(fixed2, base);
        const auto self = canonical.find(id);
        if (self != canonical.end()) {
            std::copy(self->second.begin(), self->second.end(),
                      out.begin() + static_cast<std::ptrdiff_t>(base));
        }
        const Identity parent = read_identity(fixed2, base + kParent);
        const auto found = canonical.find(parent);
        if (found != canonical.end()) {
            std::copy(found->second.begin(), found->second.end(),
                      out.begin() + static_cast<std::ptrdiff_t>(base + kParent));
        }
    }
    return out;
}

/* The Task identities, for the assignment table's parent links. */
std::map<Identity, Identity> task_identity_map(const Bytes& fixed2,
                                               std::size_t rows) {
    std::map<Identity, Identity> canonical;
    constexpr std::size_t kRecord = 64;
    if (fixed2.size() != kRecord * (rows + 3)) {
        return canonical;
    }
    for (std::size_t row = 0; row < rows; ++row) {
        const Identity id = read_identity(fixed2, kRecord * (row + 3));
        if (!identity_empty(id)) {
            canonical.emplace(id, row_identity(row));
        }
    }
    return canonical;
}

/* The RESOURCE identities (114/TBkndRsc/Fixed2Data: 40-byte records, three
 * leading records, row r's identity at +0..15).  A real-resource assignment's
 * +32..47 word carries the resource's per-save identity; Project reallocates it
 * on every save (the wave-29 work pair's control/result do), while Project's
 * unassigned placeholders carry a constant there.  Canonicalising the link
 * through the resource map lets every other assignment-Fixed2Data byte be
 * compared exactly. */
std::map<Identity, Identity> resource_identity_map(const Bytes& fixed2) {
    std::map<Identity, Identity> canonical;
    constexpr std::size_t kRecord = 40;
    constexpr std::size_t kFirst = 3;
    if (fixed2.size() < kRecord * kFirst || fixed2.size() % kRecord != 0) {
        return canonical;
    }
    const std::size_t rows = fixed2.size() / kRecord - kFirst;
    for (std::size_t row = 0; row < rows; ++row) {
        const Identity id = read_identity(fixed2, kRecord * (row + kFirst));
        if (!identity_empty(id)) {
            canonical.emplace(id, row_identity(row));
        }
    }
    return canonical;
}

/* The same canonicalisation for TBkndAssn Fixed2Data: 48-byte records, the
 * record's own id at +0..15, its parent link at +16..31 and, on a real-resource
 * assignment, the resource's per-save identity at +32..47.  The parent is the
 * TASK's Fixed2Data identity (different id space from the assignment ids), so
 * the links are canonicalised through the Task table's map; the resource word
 * is left alone unless it names a resource (Project's placeholder constant is
 * compared exactly). */
Bytes canonical_assn_fixed2(const Bytes& fixed2,
                            const std::map<Identity, Identity>& task_ids,
                            const std::map<Identity, Identity>& resource_ids) {
    constexpr std::size_t kRecord = 48;
    constexpr std::size_t kParent = 16;
    constexpr std::size_t kResource = 32;
    if (fixed2.size() % kRecord != 0) {
        return {};
    }
    const std::size_t rows = fixed2.size() / kRecord;
    Bytes out = fixed2;
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t base = kRecord * row;
        const Identity id = read_identity(fixed2, base);
        if (!identity_empty(id)) {
            const Identity replacement = row_identity(row);
            std::copy(replacement.begin(), replacement.end(),
                      out.begin() + static_cast<std::ptrdiff_t>(base));
        }
        const Identity parent = read_identity(fixed2, base + kParent);
        const auto found = task_ids.find(parent);
        if (found != task_ids.end()) {
            std::copy(found->second.begin(), found->second.end(),
                      out.begin() + static_cast<std::ptrdiff_t>(base + kParent));
        }
        const Identity resource = read_identity(fixed2, base + kResource);
        const auto resource_found = resource_ids.find(resource);
        if (resource_found != resource_ids.end()) {
            std::copy(resource_found->second.begin(), resource_found->second.end(),
                      out.begin() +
                          static_cast<std::ptrdiff_t>(base + kResource));
        }
    }
    return out;
}

/* The TBkndAssn FixedData row whose task word (+4) names this task uid. */
std::size_t assn_row_for_uid(const Bytes& fixed, std::uint32_t uid) {
    if (fixed.size() < 110) {
        return kNoIndex;
    }
    for (std::size_t row = 0; row < fixed.size() / 110; ++row) {
        if (get_u32(fixed, 110 * row + 4) == uid) {
            return row;
        }
    }
    return kNoIndex;
}

/* Index of the TBkndAssn FixedMeta 34-byte entry that names this row. */
std::size_t assn_meta_entry_for_row(const Bytes& meta, std::size_t row) {
    if (meta.size() < 16) {
        return kNoIndex;
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(110 * row);
    for (std::size_t i = 0; i < (meta.size() - 16) / 34; ++i) {
        if (get_u32(meta, 16 + i * 34 + 4) == offset) {
            return i;
        }
    }
    return kNoIndex;
}

bool owned_stream(const std::string& path) {
    static const char* const kOwned[] = {
        "114/TBkndTask/FixedData",  "114/TBkndTask/FixedMeta",
        "114/TBkndTask/Fixed2Data", "114/TBkndTask/Fixed2Meta",
        "114/TBkndTask/VarMeta",    "114/TBkndTask/Var2Data",
        "114/TBkndAssn/FixedData",  "114/TBkndAssn/FixedMeta",
        "114/TBkndAssn/Fixed2Data", "114/TBkndAssn/Fixed2Meta",
        "114/TBkndAssn/VarMeta",    "114/TBkndAssn/Var2Data",
        "114/Props",
    };
    for (const char* owned : kOwned) {
        if (path == owned) {
            return true;
        }
    }
    return false;
}

Bytes stream_or_empty(mppwrite::CfbDocument& document, const std::string& path) {
    return document.has_stream(path) ? document.read_stream(path) : Bytes{};
}

struct Pair {
    const char* slug;
    const char* control;
    const char* result;
    const std::uint32_t* uids;
    const std::uint16_t* percents;
    /* Expected elapsed ticks per uid; nullptr for a % edit (the writer derives
     * them from Duration * percent / 100, W28-1.2). */
    const std::uint32_t* ticks;
    std::size_t count;
    bool via_percent;        /* make_percent_edit vs make_update_actuals_edit */
    bool moves_status_date;  /* Update Project stores its date */
    std::uint32_t status_date_raw; /* control's stored date; 0 = unchecked */
    std::uint32_t through_raw;     /* the packed update date (W29-0.3 pairs) */
    /* W29-0.3: true only for Update Project's entire-project scope, whose
     * project row takes the earliest per-row Resume/Stop. */
    bool entire_project;
};

const std::uint32_t kUid1[] = {1};
const std::uint32_t kUid2[] = {2};
const std::uint32_t kUid12[] = {1, 2};
const std::uint32_t kUid123[] = {1, 2, 3};
/* W29-0.2: the wave-29 pairs' edited rows -- a leaf under two summaries, the
 * one task with a work-bearing assignment, and the top-level milestone. */
const std::uint32_t kUid3[] = {3};
const std::uint32_t kUid6[] = {6};
const std::uint32_t kUid7[] = {7};

const std::uint16_t kPct0[] = {0};
const std::uint16_t kPct40[] = {40};
const std::uint16_t kPct80[] = {80};
const std::uint16_t kPct100[] = {100};
const std::uint16_t kMarkManualPct[] = {83};
const std::uint16_t kUpdatePct[] = {50, 83, 100};
/* W29-0.3: Mark on Track 60 -> 80 / 60 -> 40; Update Project all rows and the
 * two-of-three selection. */
const std::uint16_t kPct80_100[] = {80, 100};
const std::uint16_t kPct80_100_100[] = {80, 100, 100};
const std::uint16_t kPct40_83_100[] = {40, 83, 100};

const std::uint32_t kTicks9600[] = {9600};
const std::uint32_t kTicks12000[] = {12000};
const std::uint32_t kTicks19200[] = {19200};
const std::uint32_t kUpdateTicks[] = {12000, 12000, 9600};
const std::uint32_t kTicks19200_14400[] = {19200, 14400};
const std::uint32_t kTicks9600_12000_9600[] = {9600, 12000, 9600};
const std::uint32_t kTicks19200_14400_9600[] = {19200, 14400, 9600};

const Pair kPairs[] = {
    /* W28-1.1/W28-1.2: PerentComplete save pairs. */
    {"auto0-40", "wave28-percent-auto0-control.mpp",
     "wave28-percent-auto40.mpp", kUid1, kPct40, nullptr, 1, true, false,
     0, kThroughRaw, false},
    {"auto40-100", "wave28-percent-auto40-control.mpp",
     "wave28-percent-auto100.mpp", kUid1, kPct100, nullptr, 1, true, false,
     0, kThroughRaw, false},
    {"auto100-0", "wave28-percent-auto100-control.mpp",
     "wave28-percent-auto0.mpp", kUid1, kPct0, nullptr, 1, true, false,
     0, kThroughRaw, false},
    {"manual0-40", "wave28-percent-manual0-control.mpp",
     "wave28-percent-manual40.mpp", kUid1, kPct40, nullptr, 1, true, false,
     0, kThroughRaw, false},
    /* W28-1.3: Mark on Track (one row, status date left alone) and Update
     * Project (three rows, status date moved to the through date). */
    {"mark-auto", "wave28-mark-auto-control.mpp", "wave28-mark-auto.mpp",
     kUid1, kPct40, kTicks9600, 1, false, false, kThroughRaw, kThroughRaw,
     false},
    {"mark-manual", "wave28-mark-manual-control.mpp", "wave28-mark-manual.mpp",
     kUid2, kMarkManualPct, kTicks12000, 1, false, false, kThroughRaw,
     kThroughRaw, false},
    {"update", "wave28-update-control.mpp", "wave28-update.mpp",
     kUid123, kUpdatePct, kUpdateTicks, 3, false, true, 0xFFFFFFFFu,
     kThroughRaw, true},
    /* W29-0.2: the three measured wave-29 shapes, each one 0 -> measured edit
     * on an automatic row -- a leaf under a summary (the summary and project
     * roll-up), a work-bearing assignment (% Work Complete, task and
     * assignment actual work), and a zero-duration milestone 0 -> 100. */
    {"wave29-leaf40", "wave29-percent-leaf-control.mpp",
     "wave29-percent-leaf40.mpp", kUid3, kPct40, nullptr, 1, true, false, 0,
     kThroughRaw, false},
    {"wave29-work40", "wave29-percent-work-control.mpp",
     "wave29-percent-work40.mpp", kUid6, kPct40, nullptr, 1, true, false, 0,
     kThroughRaw, false},
    {"wave29-milestone100", "wave29-percent-milestone-control.mpp",
     "wave29-percent-milestone100.mpp", kUid7, kPct100, nullptr, 1, true,
     false, 0, kThroughRaw, false},
    /* W29-0.3: a row already at 60 % with its ActualStart on the effective
     * start.  Mark on Track on that one row, with the status date after
     * (2026-01-09) and before (2026-01-07) the recorded progress; Update
     * Project with All=True on the same two dates; and its Selected-tasks
     * scope on a two-of-three selection (uids 1 and 2), which leaves the
     * status-date record at 0xffffffff (measured). */
    {"wave29-mark-60-after", "wave29-mark-60-after-control.mpp",
     "wave29-mark-60-after.mpp", kUid1, kPct80, kTicks19200, 1, false, false,
     0x3BF60000u, 0x3BF60000u, false},
    {"wave29-mark-60-before", "wave29-mark-60-before-control.mpp",
     "wave29-mark-60-before.mpp", kUid1, kPct40, kTicks9600, 1, false, false,
     0x3BF40000u, 0x3BF40000u, false},
    {"wave29-update-60-after", "wave29-update-60-after-control.mpp",
     "wave29-update-60-after.mpp", kUid123, kPct80_100_100,
     kTicks19200_14400_9600, 3, false, true, 0xFFFFFFFFu, 0x3BF60000u, true},
    {"wave29-update-60-before", "wave29-update-60-before-control.mpp",
     "wave29-update-60-before.mpp", kUid123, kPct40_83_100,
     kTicks9600_12000_9600, 3, false, true, 0xFFFFFFFFu, 0x3BF40000u, true},
    {"wave29-update-60-selected", "wave29-update-60-selected-control.mpp",
     "wave29-update-60-selected.mpp", kUid12, kPct80_100,
     kTicks19200_14400, 2, false, false, 0xFFFFFFFFu, 0x3BF60000u, false},
};

void check_pair(const Pair& pair) {
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
    /* W29-1.0b: the mask source is committed with the pair.  A missing null
     * save would silently degrade the discipline, so it is a failure. */
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
    if (pair.status_date_raw != 0) {
        check(plan.status_date_stored,
              "the control stores a project status date");
        const app::date32 expected =
            pair.status_date_raw == 0xFFFFFFFFu
                ? app::kDateNa
                : app::date32_from_packed(pair.status_date_raw);
        check(plan.status_date == expected,
              "the reader decodes the control's status date");
    }

    const app::date32 through_date = app::date32_from_packed(pair.through_raw);
    app::StructuralEdit edit;
    std::string edit_error;
    if (pair.via_percent) {
        check(app::make_percent_edit(plan, pair.uids[0], pair.percents[0], edit,
                                     edit_error),
              "make_percent_edit accepts the saved leaf");
    } else {
        const std::vector<std::uint32_t> uids(pair.uids, pair.uids + pair.count);
        check(app::make_update_actuals_edit(plan, control_path.string(), uids,
                                            through_date, pair.moves_status_date,
                                            edit, edit_error),
              "make_update_actuals_edit accepts the saved leaves");
        check(edit.kind == app::StructuralKind::update_actuals,
              "the built edit is a date update");
        check(edit.through_date == through_date,
              "the edit carries the through date");
        check(edit.updates_status_date == pair.moves_status_date,
              "the edit carries the status-date decision");
        check(edit.update_entire_project == pair.entire_project,
              "the edit carries the update scope (entire project vs selection)");
        check_size(edit.actuals.size(), pair.count,
                   "the update covers every target");
        if (edit.actuals.size() == pair.count) {
            for (std::size_t i = 0; i < pair.count; ++i) {
                check_size(edit.actuals[i].uid, pair.uids[i],
                           "the target uid is the measured one");
                check_size(edit.actuals[i].percent, pair.percents[i],
                           "the target percent is Project's");
                if (pair.ticks != nullptr) {
                    check_size(edit.actuals[i].actual_ticks, pair.ticks[i],
                               "the target elapsed ticks are Project's");
                }
            }
        }
    }
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), "the actuals edit is queued");

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) /
        (std::string("app-actuals-") + pair.slug + ".mpp");
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        edits, {}, control_path.string(), out_path.string(), false);
    check(saved.ok, "the actuals save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.percent_rows_written, pair.count,
               "every edited row's actuals were written");
    check(!std::filesystem::exists(out_path.string() + ".w51-structural-tmp"),
          "the save leaves no intermediate file");

    mppwrite::CfbDocument control;
    mppwrite::CfbDocument null_save;
    mppwrite::CfbDocument result;
    mppwrite::CfbDocument output;
    try {
        control = mppwrite::CfbDocument::from_file(control_path.string());
        null_save = mppwrite::CfbDocument::from_file(null_path.string());
        result = mppwrite::CfbDocument::from_file(result_path.string());
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
     * them passed silently.  Every comparison below is Project's. */
    const Bytes control_fixed = control.read_stream(task_stream("FixedData"));
    const Bytes result_fixed = result.read_stream(task_stream("FixedData"));
    const Bytes output_fixed = output.read_stream(task_stream("FixedData"));
    check(output_fixed == result_fixed,
          "Task FixedData equals Project's save byte for byte");

    const Bytes control_meta = control.read_stream(task_stream("FixedMeta"));
    const Bytes result_meta = result.read_stream(task_stream("FixedMeta"));
    const Bytes output_meta = output.read_stream(task_stream("FixedMeta"));
    check(output_meta == result_meta,
          "Task FixedMeta equals Project's save byte for byte (+44 and the "
          "+8..23 blob included)");

    const Bytes control_f2m = control.read_stream(task_stream("Fixed2Meta"));
    const Bytes result_f2m = result.read_stream(task_stream("Fixed2Meta"));
    const Bytes output_f2m = output.read_stream(task_stream("Fixed2Meta"));
    check(output_f2m == result_f2m,
          "Task Fixed2Meta equals Project's save byte for byte (+88/+95 "
          "included)");

    const Bytes control_f2 = control.read_stream(task_stream("Fixed2Data"));
    const Bytes result_f2 = result.read_stream(task_stream("Fixed2Data"));
    const Bytes output_f2 = output.read_stream(task_stream("Fixed2Data"));
    const std::size_t task_rows =
        control_fixed.size() < 48 ? 0 : (control_fixed.size() - 48) / 202;
    check_size(output_f2.size(), result_f2.size(),
               "Task Fixed2Data size equals Project's");
    if (output_f2.size() == result_f2.size()) {
        check(canonical_task_fixed2(output_f2, task_rows) ==
                  canonical_task_fixed2(result_f2, task_rows),
              "Task Fixed2Data equals Project's after identity canonicalisation "
              "(+42 and +46 included)");
    }

    const Bytes control_var_meta = control.read_stream(task_stream("VarMeta"));
    const Bytes result_var_meta = result.read_stream(task_stream("VarMeta"));
    const Bytes output_var_meta = output.read_stream(task_stream("VarMeta"));
    check(output_var_meta == result_var_meta,
          "Task VarMeta equals Project's save byte for byte (entries and counts)");

    Bytes control_var2 = control.read_stream(task_stream("Var2Data"));
    Bytes result_var2 = result.read_stream(task_stream("Var2Data"));
    Bytes output_var2 = output.read_stream(task_stream("Var2Data"));
    mask_progress_ids(result_var2, result_var_meta);
    mask_progress_ids(output_var2, output_var_meta);
    check(output_var2 == result_var2,
          "Task Var2Data equals Project's save after masking the opaque step ids");

    /* --- the edited rows' Task fields, each by name ---------------------- */
    bool meta_rows_ok = true;
    for (std::size_t i = 0; i < pair.count; ++i) {
        const std::string uid = std::to_string(pair.uids[i]);
        const std::size_t row = uid_row(control_fixed, pair.uids[i]);
        check(row != kNoIndex,
              (uid + ": the edited uid is a saved row").c_str());
        if (row == kNoIndex) {
            meta_rows_ok = false;
            continue;
        }
        const std::size_t entry = meta_entry_for_row(control_meta, row);
        check(entry != kNoIndex,
              (uid + ": the edited row has a FixedMeta entry").c_str());
        if (entry == kNoIndex) {
            meta_rows_ok = false;
            continue;
        }
        const std::size_t at = 16 + entry * 47;
        check_size(get_u16(output_meta, at + 2),
                   get_u16(result_meta, at + 2),
                   (uid + " FixedMeta +2 field count equals Project's").c_str());
        check(output_meta[at + 44] == result_meta[at + 44],
              (uid + " FixedMeta +44 progress flag equals Project's").c_str());
        check(same_span(output_meta, result_meta, at + 8, 16),
              (uid + " FixedMeta +8..23 blob equals Project's").c_str());
        const std::size_t f2m_at = 16 + entry * 96;
        if (f2m_at + 96 <= output_f2m.size() &&
            f2m_at + 96 <= result_f2m.size()) {
            check(output_f2m[f2m_at + 88] == result_f2m[f2m_at + 88],
                  (uid + " Fixed2Meta +88 progress tail equals Project's").c_str());
            check(output_f2m[f2m_at + 95] == result_f2m[f2m_at + 95],
                  (uid + " Fixed2Meta +95 kind byte equals Project's").c_str());
        } else {
            check(false, (uid + ": Fixed2Meta has the row's entry").c_str());
        }
        const std::size_t f2_at = 64 * (row + 3);
        if (f2_at + 64 <= output_f2.size() && f2_at + 64 <= result_f2.size()) {
            check(output_f2[f2_at + 42] == result_f2[f2_at + 42],
                  (uid + " Fixed2Data +42 progress/schedule byte equals "
                   "Project's").c_str());
            check(get_u32(output_f2, f2_at + 46) ==
                      get_u32(result_f2, f2_at + 46),
                  (uid + " Fixed2Data +46 start copy equals Project's").c_str());
        } else {
            check(false, (uid + ": Fixed2Data has the row's record").c_str());
        }
    }
    check(meta_rows_ok, "every edited row has a control metadata entry");

    /* --- the TBkndAssn streams the writer owns, against PROJECT's file --- */
    const Bytes control_assn_fixed =
        control.read_stream("114/TBkndAssn/FixedData");
    const Bytes result_assn_fixed =
        result.read_stream("114/TBkndAssn/FixedData");
    const Bytes output_assn_fixed =
        output.read_stream("114/TBkndAssn/FixedData");
    check(output_assn_fixed == result_assn_fixed,
          "TBkndAssn FixedData equals Project's save byte for byte");

    const Bytes control_assn_meta =
        control.read_stream("114/TBkndAssn/FixedMeta");
    const Bytes result_assn_meta =
        result.read_stream("114/TBkndAssn/FixedMeta");
    const Bytes output_assn_meta =
        output.read_stream("114/TBkndAssn/FixedMeta");
    check(output_assn_meta == result_assn_meta,
          "TBkndAssn FixedMeta equals Project's save byte for byte");

    const Bytes control_assn_f2m =
        control.read_stream("114/TBkndAssn/Fixed2Meta");
    const Bytes result_assn_f2m =
        result.read_stream("114/TBkndAssn/Fixed2Meta");
    const Bytes output_assn_f2m =
        output.read_stream("114/TBkndAssn/Fixed2Meta");
    check(output_assn_f2m == result_assn_f2m,
          "TBkndAssn Fixed2Meta equals Project's save byte for byte (+9 "
          "included)");

    const Bytes control_assn_f2 =
        control.read_stream("114/TBkndAssn/Fixed2Data");
    const Bytes result_assn_f2 =
        result.read_stream("114/TBkndAssn/Fixed2Data");
    const Bytes output_assn_f2 =
        output.read_stream("114/TBkndAssn/Fixed2Data");
    check_size(output_assn_f2.size(), result_assn_f2.size(),
               "TBkndAssn Fixed2Data size equals Project's");
    if (output_assn_f2.size() == result_assn_f2.size()) {
        /* Each file's assignment parents name ITS OWN Task identities, and a
         * real-resource assignment's +32 word names its OWN resource identity
         * (Project allocates a fresh set per save; the port keeps the
         * control's), so both maps are built from the same file's streams. */
        check(canonical_assn_fixed2(
                  output_assn_f2, task_identity_map(output_f2, task_rows),
                  resource_identity_map(
                      stream_or_empty(output, "114/TBkndRsc/Fixed2Data"))) ==
                  canonical_assn_fixed2(
                      result_assn_f2, task_identity_map(result_f2, task_rows),
                      resource_identity_map(
                          stream_or_empty(result, "114/TBkndRsc/Fixed2Data"))),
              "TBkndAssn Fixed2Data equals Project's after identity "
              "canonicalisation");
    }

    const Bytes control_assn_var_meta =
        control.read_stream("114/TBkndAssn/VarMeta");
    const Bytes result_assn_var_meta =
        result.read_stream("114/TBkndAssn/VarMeta");
    const Bytes output_assn_var_meta =
        output.read_stream("114/TBkndAssn/VarMeta");
    check(output_assn_var_meta == result_assn_var_meta,
          "TBkndAssn VarMeta equals Project's save byte for byte");

    const Bytes control_assn_var2 =
        control.read_stream("114/TBkndAssn/Var2Data");
    const Bytes result_assn_var2 =
        result.read_stream("114/TBkndAssn/Var2Data");
    const Bytes output_assn_var2 =
        output.read_stream("114/TBkndAssn/Var2Data");
    check(output_assn_var2 == result_assn_var2,
          "TBkndAssn Var2Data equals Project's save byte for byte "
          "(0F400031/0F400032 included)");

    /* --- the edited rows' assignment fields, each by name ---------------- */
    for (std::size_t i = 0; i < pair.count; ++i) {
        const std::string uid = std::to_string(pair.uids[i]);
        const std::size_t arow = assn_row_for_uid(control_assn_fixed, pair.uids[i]);
        check(arow != kNoIndex,
              (uid + ": the edited task has an assignment row").c_str());
        if (arow == kNoIndex) {
            continue;
        }
        const std::size_t abase = 110 * arow;
        check(same_span(output_assn_fixed, result_assn_fixed, abase + 28, 8),
              (uid + " assignment +28 actual work equals Project's").c_str());
        check(same_span(output_assn_fixed, result_assn_fixed, abase + 44, 8),
              (uid + " assignment +44 remaining work equals Project's").c_str());
        check(get_u32(output_assn_fixed, abase + 60) ==
                  get_u32(result_assn_fixed, abase + 60),
              (uid + " assignment +60 resume equals Project's").c_str());
        check(get_u32(output_assn_fixed, abase + 104) ==
                  get_u32(result_assn_fixed, abase + 104),
              (uid + " assignment +104 stop copy equals Project's").c_str());
        const std::size_t aentry =
            assn_meta_entry_for_row(control_assn_meta, arow);
        check(aentry != kNoIndex,
              (uid + ": the assignment row has a FixedMeta entry").c_str());
        if (aentry != kNoIndex) {
            const std::size_t aat = 16 + aentry * 34;
            check_size(get_u16(output_assn_meta, aat + 2),
                       get_u16(result_assn_meta, aat + 2),
                       (uid + " assignment FixedMeta +2 field count equals "
                        "Project's").c_str());
            check(output_assn_meta[aat + 10] == result_assn_meta[aat + 10],
                  (uid + " assignment FixedMeta +10 progress word equals "
                   "Project's").c_str());
            check(output_assn_meta[aat + 14] == result_assn_meta[aat + 14],
                  (uid + " assignment FixedMeta +14 progress word equals "
                   "Project's").c_str());
        }
        /* Fixed2Meta is indexed by the FixedMeta ENTRY, not by the data row:
         * the assignment table's entry order can differ from its FixedData
         * order once a row is tombstoned (the wave-29 milestone pair). */
        if (aentry != kNoIndex) {
            const std::size_t af2m = 16 + aentry * 53;
            if (af2m + 53 <= output_assn_f2m.size() &&
                af2m + 53 <= result_assn_f2m.size()) {
                check(output_assn_f2m[af2m + 9] == result_assn_f2m[af2m + 9],
                      (uid + " assignment Fixed2Meta +9 live bit equals "
                       "Project's").c_str());
            } else {
                check(false,
                      (uid + ": Fixed2Meta has the assignment entry").c_str());
            }
        }
    }

    /* --- 114/Props against PROJECT's file -------------------------------- */
    const Bytes control_props = control.read_stream("114/Props");
    const Bytes result_props = result.read_stream("114/Props");
    const Bytes output_props = output.read_stream("114/Props");
    check_size(output_props.size(), result_props.size(),
               "Props keeps Project's length");
    const std::uint32_t kCounterTags[] = {0x01000002u, 0x01000100u, 0x04000002u,
                                          0x04000100u};
    for (const std::uint32_t tag : kCounterTags) {
        const std::size_t ours = props_scalar_value(output_props, tag);
        const std::size_t theirs = props_scalar_value(result_props, tag);
        check(ours != kNoIndex && theirs != kNoIndex,
              ("Props carries the " + hex_tag(tag) + " record").c_str());
        if (ours != kNoIndex && theirs != kNoIndex) {
            check(get_u32(output_props, ours) == get_u32(result_props, theirs),
                  ("Props " + hex_tag(tag) + " counter equals Project's").c_str());
        }
    }
    if (pair.moves_status_date || pair.status_date_raw != 0) {
        const std::size_t control_at = status_date_payload(control_props);
        const std::size_t result_at = status_date_payload(result_props);
        const std::size_t output_at = status_date_payload(output_props);
        check(control_at != kNoIndex && result_at != kNoIndex &&
                  output_at != kNoIndex,
              "each props stream carries the status-date record");
        if (control_at != kNoIndex && result_at != kNoIndex &&
            output_at != kNoIndex) {
            check(get_u32(output_props, output_at) ==
                      get_u32(result_props, result_at),
                  "the port's status date equals Project's");
            if (pair.moves_status_date) {
                check(get_u32(result_props, result_at) == pair.through_raw,
                      "Project's update pair stores the through date");
                check(get_u32(control_props, control_at) == 0xFFFFFFFFu,
                      "the update control stores no status date");
            } else {
                check(get_u32(control_props, control_at) == pair.status_date_raw,
                      "the control's stored status date is the measured one");
                check(get_u32(result_props, result_at) == pair.status_date_raw,
                      "Project's save leaves the status date alone");
            }
        }
    }
    /* W29-1.0b: Props's per-save id GUIDs are NOISE, not an expected failure:
     * they differ between the control and Project's no-edit save, so the
     * 16-byte block mask hides exactly those records.  Every other byte -- the
     * counters above and the status date -- is an edit effect or untouched and
     * must equal Project's edit save. */
    const Bytes null_props = null_save.read_stream("114/Props");
    const save_pair::Verdict props_verdict = save_pair::compare(
        save_pair::Kind::fixed_block, control_props, null_props, result_props,
        output_props);
    check(props_verdict.lengths_ok,
          "Props has equal control and null-save lengths");
    check(props_verdict.mismatches == 0,
          mismatch_message("114/Props", props_verdict).c_str());
    report_stream("114/Props", save_pair::Kind::fixed_block, props_verdict);
    std::size_t total_noise = props_verdict.noise;
    std::size_t total_effects = props_verdict.effects;

    /* --- every other stream, through the null save ------------------------ */
    /* W29-1.0b (the R28-1 residual): a whole-stream expected failure cannot
     * tell a per-save byte from a change the edit caused.  The committed null
     * save masks the former; every unmasked byte Project's edit save changes
     * is the edit's effect and must equal the port's save. */
    const std::vector<std::string> control_path_list = control.stream_paths();
    const std::vector<std::string> output_path_list = output.stream_paths();
    std::set<std::string> const control_paths(control_path_list.begin(),
                                              control_path_list.end());
    std::set<std::string> const output_paths(output_path_list.begin(),
                                             output_path_list.end());
    check(control_paths == output_paths,
          "the save adds and removes no stream");
    std::set<std::string> all_paths = control_paths;
    const std::vector<std::string> result_path_list = result.stream_paths();
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
            stream_or_empty(null_save, path), stream_or_empty(result, path),
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

    /* --- the reader agrees with Project's percent and status date -------- */
    try {
        const mpp::Document reread = mpp::load_file(out_path.string());
        for (std::size_t i = 0; i < pair.count; ++i) {
            bool found = false;
            for (const mpp::Task& task : reread.tasks) {
                if (task.uid != pair.uids[i]) {
                    continue;
                }
                found = true;
                check_size(task.percent_complete, pair.percents[i],
                           "the saved file reads back Project's percent");
            }
            check(found, "the edited uid is a live task in the saved file");
        }
        if (pair.moves_status_date || pair.status_date_raw != 0) {
            const std::uint32_t expected = pair.moves_status_date
                                               ? pair.through_raw
                                               : pair.status_date_raw;
            check(reread.project.status_date_stored,
                  "the save keeps the status-date record");
            check(reread.project.status_date.raw == expected,
                  "the saved file reads back the status date");
        }
    } catch (const std::exception& read_error) {
        check(false, "the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* W29-0.2: the mid-to-mid pair (25 -> 60) stays refused.  Project's own save of
 * that edit does not merely write the row's actuals: it RESCHEDULES the chain
 * (the summary durations shrink 26400 -> 23040 ticks, the summary/project row
 * takes a new start and roll-up, the other leaves' date copies move, and the
 * edited leaf's Resume/Stop follow the new schedule).  This save path has no
 * scheduler, so the transition stays refused and every field Project moves is a
 * NAMED EXPECTED FAILURE, not an assertion that the control's bytes are right
 * (rule 1: a field Project changes in the pair that the port does not produce is
 * a failing check).  Because the edit is refused the port writes nothing, so
 * the port's "save" keeps the control's value; each check below compares that
 * value with Project's file.  The refusal check is the anti-rot guard: the day
 * the transition is enabled this block must be rewritten against a port save. */
void check_mid_pair_refused() {
    std::printf("-- pair wave29-mid25-60 (refused shape):\n");
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) /
        "wave29-percent-mid25-control.mpp";
    const std::filesystem::path result_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / "wave29-percent-mid60.mpp";
    if (!std::filesystem::exists(control_path) ||
        !std::filesystem::exists(result_path)) {
        std::printf("SKIP: the mid pair is not present\n");
        return;
    }

    app::PlanInput plan;
    try {
        plan =
            app::plan_input_from_document(mpp::load_file(control_path.string()));
    } catch (const std::exception& load_error) {
        check(false, "the mid pair control loads through the reader");
        std::printf("LOAD ERROR: %s\n", load_error.what());
        return;
    }
    app::StructuralEdit edit;
    std::string edit_error;
    check(!app::make_percent_edit(plan, 4, 60, edit, edit_error),
          "make_percent_edit refuses the unmeasured 25 -> 60 mid edit");
    check(edit_error.find("no Project save pair measures") != std::string::npos,
          "the mid refusal names the missing transition measurement");

    mppwrite::CfbDocument control;
    mppwrite::CfbDocument result;
    try {
        control = mppwrite::CfbDocument::from_file(control_path.string());
        result = mppwrite::CfbDocument::from_file(result_path.string());
    } catch (const std::exception& open_error) {
        check(false, "the mid pair files open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }
    const Bytes control_fixed = control.read_stream(task_stream("FixedData"));
    const Bytes result_fixed = result.read_stream(task_stream("FixedData"));

    struct RescheduledField {
        std::uint32_t uid;
        std::size_t offset;
        const char* name;
    };
    /* Every word the pair measurement showed Project's save moving. */
    const RescheduledField kRescheduled[] = {
        {0, 80, "ActualDuration"},  {0, 84, "Duration"},
        {0, 88, "RemainingDuration"}, {0, 92, "% complete"},
        {0, 96, "Start"},           {0, 104, "Start2"},
        {0, 112, "Start3"},         {0, 120, "ActualStart"},
        {0, 132, "Resume"},         {0, 136, "Stop"},
        {1, 80, "ActualDuration"},  {1, 84, "Duration"},
        {1, 88, "RemainingDuration"}, {1, 92, "% complete"},
        {1, 96, "Start"},           {1, 104, "Start2"},
        {1, 112, "Start3"},         {1, 120, "ActualStart"},
        {1, 132, "Resume"},         {1, 136, "Stop"},
        {2, 80, "ActualDuration"},  {2, 84, "Duration"},
        {2, 88, "RemainingDuration"}, {2, 92, "% complete"},
        {2, 96, "Start"},           {2, 104, "Start2"},
        {2, 112, "Start3"},         {2, 120, "ActualStart"},
        {2, 132, "Resume"},         {2, 136, "Stop"},
        {3, 96, "Start"},           {3, 100, "Finish"},
        {3, 104, "Start2"},         {3, 108, "Finish2"},
        {3, 112, "Start3"},         {3, 116, "Finish3"},
        {3, 174, "start anchor"},   {4, 80, "ActualDuration"},
        {4, 88, "RemainingDuration"}, {4, 92, "% complete"},
        {4, 132, "Resume"},         {4, 136, "Stop"},
        {5, 104, "Start2"},         {5, 108, "Finish2"},
        {5, 186, "StartSlack"},     {5, 190, "TotalSlack"},
        {7, 104, "Start2"},         {7, 108, "Finish2"},
        {7, 186, "StartSlack"},     {7, 190, "TotalSlack"},
    };
    for (const RescheduledField& field : kRescheduled) {
        const std::string uid = std::to_string(field.uid);
        const std::size_t row = uid_row(control_fixed, field.uid);
        check(row != kNoIndex,
              (uid + ": the rescheduled uid is a saved row").c_str());
        if (row == kNoIndex) {
            continue;
        }
        const std::size_t at = 48 + 202 * row + field.offset;
        check_expected_failure(
            get_u32(control_fixed, at) == get_u32(result_fixed, at),
            (uid + " " + field.name +
             " equals Project's (Project reschedules the chain on this edit; "
             "the port has no scheduler on this save path)").c_str());
    }
}

const app::TaskRow* find_task(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

int shown_percent(const app::PlanInput& plan, std::uint32_t uid) {
    const app::TaskRow* row = find_task(plan, uid);
    return row == nullptr ? -1 : static_cast<int>(row->percent_complete);
}

/* percent_through(): the measured date-update arithmetic.  On the update
 * control the three rows are 0 % and every row's effective start is Sunday
 * 13:00; through Wed 2026-01-07 Project's save stores 50 %, 83 % (unrounded
 * 83.33333333333333 in the step block) and 100 % from 12000, 12000 and 9600
 * elapsed ticks. */
void check_percent_through() {
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / "wave28-update-control.mpp";
    if (!std::filesystem::exists(control_path)) {
        std::printf("SKIP: wave28-update-control.mpp is not present\n");
        return;
    }
    app::PlanInput plan;
    core::sched::Calendar calendar;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
        calendar = core::cal::load_project_calendar(control_path.string());
    } catch (const std::exception& error) {
        check(false, "the update control loads for the percent-through checks");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    const app::date32 through_date = core::sched::from_ymd(2026, 1, 7);
    const std::uint32_t through = app::packed_from_date32(through_date);
    struct Expected {
        std::uint32_t uid;
        std::uint16_t percent;
        std::uint32_t ticks;
        double step;
    };
    const Expected expected[] = {
        {1, 50, 12000, 50.0},
        {2, 83, 12000, 12000.0 * 100.0 / 14400.0},
        {3, 100, 9600, 100.0},
    };
    for (const Expected& want : expected) {
        const app::TaskRow* row = find_task(plan, want.uid);
        check(row != nullptr, "the update control has the measured task");
        if (row == nullptr) {
            continue;
        }
        const std::uint32_t start = app::packed_from_date32(row->start);
        const std::uint32_t duration = static_cast<std::uint32_t>(row->duration_ticks);
        app::PercentThrough progress;
        check(app::percent_through(calendar, start, duration,
                                   row->manually_scheduled, through, progress),
              "percent_through measures the update date");
        check_size(progress.percent, want.percent,
                   "percent_through matches Project's percent");
        check_size(progress.actual_ticks, want.ticks,
                   "percent_through matches Project's elapsed ticks");
        check(std::fabs(progress.step_percent - want.step) < 1e-9,
              "percent_through carries the unrounded step percent");

        /* Through the row's own start is the 0 % shape: no actual time. */
        app::PercentThrough zero;
        check(app::percent_through(calendar, start, duration,
                                   row->manually_scheduled, start, zero),
              "percent_through accepts the row's start");
        check_size(zero.percent, 0, "a date at the start is 0 %");
        check_size(zero.actual_ticks, 0, "0 % carries no actual time");
    }
}

/* The edit model around a date update: one edit, many rows; last write wins
 * across kinds; a pending delete displaces the whole update and undo restores
 * it. */
void check_group_edit_model() {
    const std::filesystem::path control_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / "wave28-update-control.mpp";
    if (!std::filesystem::exists(control_path)) {
        std::printf("SKIP: wave28-update-control.mpp is not present\n");
        return;
    }
    app::PlanInput plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
    } catch (const std::exception& error) {
        check(false, "the update control loads for the edit-model checks");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    const app::date32 through_date = core::sched::from_ymd(2026, 1, 7);
    const std::vector<std::uint32_t> uids = {1, 2, 3};
    app::StructuralEdit edit;
    std::string edit_error;
    check(app::make_update_actuals_edit(plan, control_path.string(), uids,
                                        through_date, true, edit, edit_error),
          "the three-row date update builds");
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), "the date update is queued");
    check_size(edits.size(), 1, "one edit represents the whole update");

    const app::PlanInput shown = edits.apply(plan);
    check(shown_percent(shown, 1) == 50 && shown_percent(shown, 2) == 83 &&
              shown_percent(shown, 3) == 100,
          "apply() shows Project's measured percents");
    check(edits.undo(), "the update undoes as one edit");
    const app::PlanInput restored = edits.apply(plan);
    check(shown_percent(restored, 1) == 0 && shown_percent(restored, 2) == 0 &&
              shown_percent(restored, 3) == 0,
          "undo restores the control's percents");
    check(edits.redo(), "the update redoes");
    const app::PlanInput again = edits.apply(plan);
    check(shown_percent(again, 1) == 50, "redo shows the measured percent again");

    /* A hand-built target the writer could not store is refused by push. */
    app::StructuralEdit broken = edit;
    broken.actuals[0].actual_ticks = 0;
    app::StructuralEdits broken_edits;
    check(!broken_edits.push(broken, &plan),
          "a date update whose ticks contradict its percent is refused");

    /* A pending delete of one target refuses a later update. */
    app::StructuralEdits delete_then_update;
    app::StructuralEdit remove;
    remove.kind = app::StructuralKind::delete_task;
    remove.uid = 2;
    check(delete_then_update.push(remove, &plan), "the delete is queued");
    check(!delete_then_update.push(edit, &plan),
          "an update naming a pending-deleted task is refused");

    /* A delete over an existing update displaces the whole update; undo puts it
     * back. */
    app::StructuralEdits update_then_delete;
    check(update_then_delete.push(edit, &plan), "the update is queued first");
    check(update_then_delete.push(remove, &plan), "the delete is queued over it");
    check_size(update_then_delete.size(), 1,
               "the delete displaced the whole update");
    if (update_then_delete.size() == 1) {
        check_size(update_then_delete.applied().front().displaced.size(), 1,
                   "the delete keeps the displaced update");
    }
    check(update_then_delete.undo(), "the delete undoes");
    check_size(update_then_delete.size(), 1, "undo restores the displaced update");
    if (update_then_delete.size() == 1) {
        check(update_then_delete.applied().front().kind ==
                  app::StructuralKind::update_actuals,
              "the restored edit is the date update");
    }

    /* Last write wins across kinds, in the display AND in the file: a % edit
     * queued after the update owns uid 1, while uid 2/3 (and the status date)
     * still come from the update. */
    app::StructuralEdits mixed;
    check(mixed.push(edit, &plan), "the update is queued for the mixed save");
    app::StructuralEdit percent;
    check(app::make_percent_edit(plan, 1, 40, percent, edit_error),
          "the later percent edit builds");
    check(mixed.push(percent, &plan), "the percent edit is queued after it");
    const app::PlanInput mixed_shown = mixed.apply(plan);
    check(shown_percent(mixed_shown, 1) == 40 &&
              shown_percent(mixed_shown, 2) == 83 &&
              shown_percent(mixed_shown, 3) == 100,
          "apply() shows the last write per row across both kinds");

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        error);
    const std::filesystem::path out_path =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-actuals-mixed.mpp";
    std::filesystem::remove(out_path, error);
    const app::StructuralSaveResult saved = app::save_structural_edits(
        mixed, {}, control_path.string(), out_path.string(), false);
    check(saved.ok, "the mixed update + percent save succeeds");
    if (saved.ok) {
        check_size(saved.percent_rows_written, 3,
                   "the save writes one winning value per row");
        try {
            const mpp::Document reread = mpp::load_file(out_path.string());
            int percent1 = -1;
            int percent2 = -1;
            int percent3 = -1;
            for (const mpp::Task& task : reread.tasks) {
                if (task.uid == 1) {
                    percent1 = task.percent_complete;
                } else if (task.uid == 2) {
                    percent2 = task.percent_complete;
                } else if (task.uid == 3) {
                    percent3 = task.percent_complete;
                }
            }
            check(percent1 == 40 && percent2 == 83 && percent3 == 100,
                  "the saved file stores the last write per row");
            check(reread.project.status_date_stored &&
                      reread.project.status_date.raw == kThroughRaw,
                  "the mixed save still moves the project status date");
        } catch (const std::exception& read_error) {
            check(false, "the mixed save reopens through the reader");
            std::printf("READ ERROR: %s\n", read_error.what());
        }
    }
}

/* W29-0.0 (the wave-28 review's R28-3, High): a date update is refused on a
 * row that already records progress -- stored % > 0, an ActualStart, or a
 * 0x0B40015E progress block.  One check per shape; the last two patch a copy of
 * the committed control so the refusal is proved on the file-side check alone
 * (the plan's percent stays 0). */
void check_update_refusals() {
    const std::filesystem::path eight_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / "wave28-percent-auto40-control.mpp";
    const std::filesystem::path zero_path =
        std::filesystem::path(APP_TEST_DATA_DIR) / "wave28-update-control.mpp";
    if (!std::filesystem::exists(eight_path) || !std::filesystem::exists(zero_path)) {
        std::printf("SKIP: the W29-0.0 refusal controls are not present\n");
        return;
    }
    const app::date32 through_date = core::sched::from_ymd(2026, 1, 7);
    app::StructuralEdit edit;
    std::string error;

    /* (a) the stored percent: uid 1 is at 40 % in the committed control. */
    app::PlanInput stored;
    try {
        stored = app::plan_input_from_document(mpp::load_file(eight_path.string()));
    } catch (const std::exception& load_error) {
        check(false, "the 40 % control loads");
        std::printf("LOAD ERROR: %s\n", load_error.what());
        return;
    }
    check(!app::make_update_actuals_edit(stored, eight_path.string(), {1u},
                                         through_date, false, edit, error),
          "a date update on a row stored at 40 % is refused");
    check(error.find("already") != std::string::npos && error.find("%") != std::string::npos,
          "the stored-percent refusal names the progress");

    std::error_code fs_error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        fs_error);
    auto patched_copy = [](const std::filesystem::path& source,
                           const std::filesystem::path& target,
                           const std::function<void(mppwrite::TaskTable&, std::size_t)>& patch,
                           std::string& failure) -> bool {
        try {
            mppwrite::CfbDocument document =
                mppwrite::CfbDocument::from_file(source.string());
            mppwrite::TaskTable table = mppwrite::TaskTable::from_document(document);
            const std::size_t row = table.row_for_uid(1);
            patch(table, row);
            table.store_into(document);
            std::error_code remove_error;
            std::filesystem::remove(target, remove_error);
            document.save(target.string());
            return true;
        } catch (const std::exception& patch_error) {
            failure = patch_error.what();
            return false;
        }
    };

    /* (b) an ActualStart on an otherwise 0 % row. */
    const std::filesystem::path actual_start_path =
        std::filesystem::path(APP_TEST_OUT_DIR) / "w29-actual-start.mpp";
    std::string patch_error;
    const bool wrote_actual_start = patched_copy(
        zero_path, actual_start_path,
        [](mppwrite::TaskTable& table, std::size_t row) {
            table.set_u32(row, mppwrite::task_row::kActualStart, 0x3BF212C0u);
        },
        patch_error);
    check(wrote_actual_start, "the ActualStart copy is written");
    if (wrote_actual_start) {
        app::PlanInput patched;
        try {
            patched = app::plan_input_from_document(
                mpp::load_file(actual_start_path.string()));
        } catch (const std::exception& load_error) {
            check(false, "the ActualStart copy loads");
            std::printf("LOAD ERROR: %s\n", load_error.what());
            patched = {};
        }
        check(patched.tasks.size() > 1 && patched.tasks[0].percent_complete == 0,
              "the ActualStart copy still reads as 0 %");
        check(!app::make_update_actuals_edit(patched, actual_start_path.string(), {1u},
                                             through_date, false, edit, error),
              "a date update on a 0 % row with an ActualStart is refused");
        check(error.find("actual date") != std::string::npos,
              "the ActualStart refusal names the recorded actual date");
    }

    /* (c) a progress block on a 0 % row with no actual dates. */
    const std::filesystem::path progress_block_path =
        std::filesystem::path(APP_TEST_OUT_DIR) / "w29-progress-block.mpp";
    const bool wrote_progress_block = patched_copy(
        eight_path, progress_block_path,
        [](mppwrite::TaskTable& table, std::size_t row) {
            table.set_u16(row, mppwrite::task_row::kPercentComplete, 0);
            table.set_u32(row, mppwrite::task_row::kActualStart, 0xFFFFFFFFu);
            table.set_u32(row, mppwrite::task_row::kActualFinish, 0xFFFFFFFFu);
        },
        patch_error);
    check(wrote_progress_block, "the progress-block copy is written");
    if (wrote_progress_block) {
        app::PlanInput patched;
        try {
            patched = app::plan_input_from_document(
                mpp::load_file(progress_block_path.string()));
        } catch (const std::exception& load_error) {
            check(false, "the progress-block copy loads");
            std::printf("LOAD ERROR: %s\n", load_error.what());
            patched = {};
        }
        check(patched.tasks.size() > 1 && patched.tasks[0].percent_complete == 0,
              "the progress-block copy reads as 0 %");
        check(!app::make_update_actuals_edit(patched, progress_block_path.string(),
                                             {1u}, through_date, false, edit, error),
              "a date update on a 0 % row with a progress block is refused");
        check(error.find("progress") != std::string::npos,
              "the progress-block refusal names the block");
    }
}

} /* namespace */

int main() {
    for (const Pair& pair : kPairs) {
        check_pair(pair);
    }
    check_mid_pair_refused();
    check_percent_through();
    check_group_edit_model();
    check_update_refusals();
    std::printf("app_edits_actuals_tests: %d checks passed, %d expected failure(s)\n",
                g_checks - g_failures, g_expected_failures);
    if (g_failures != 0) {
        std::printf("app_edits_actuals_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
