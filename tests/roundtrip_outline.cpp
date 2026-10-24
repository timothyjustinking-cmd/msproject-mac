/*
 * roundtrip_outline.cpp -- W28-2.1: the measured Indent / Outdent save.
 *
 * CLAIM UNDER TEST.  tests/data/wave28-outline-{indent,summary,outdent}.mpp are
 * Project 2021's own saves of one outline edit each, made from the matching
 * -control.mpp (docs/handoff/wave28-brief.md, the measurement recipe).  This test
 * makes the same edit with mppwrite::write_outline_edits and compares the port's
 * save with Project's, field by field:
 *
 *   FixedData    byte-identical (levels, parent row id, summary flag and roll-up,
 *                restored dates);
 *   FixedMeta    byte-identical (all three entries, including +2 counts);
 *   Fixed2Meta   byte-identical (+88/+95);
 *   Fixed2Data   identical after canonicalising the 16-byte identity GUIDs:
 *                Project allocates fresh GUIDs on every save (the first u32 is a
 *                session counter), so each row's identity is replaced by its row
 *                index and each parent link by the parent's index.  Every other
 *                byte (flags +42, start copy +46..49, +40/+41) must match;
 *   VarMeta      byte-identical (logical entries and offsets: in-place shrink
 *                versus appended block);
 *   Var2Data     byte-identical (block order and the stale bytes Project leaves).
 *
 * W29-0.1 (the wave-28 review's R28-1) widened the ownership to Project's
 * measured outline side effects: the six TBkndAssn streams and the six
 * 114/Props counters.  They are compared with PROJECT's file, never with the
 * control: the tombstoned assignment (indent / summary) and the re-created
 * assignment row (outdent) must be byte-identical, and the two per-save opaque
 * Fixed2Data identities and the two per-save Props id words are named EXPECTED
 * FAILURES rather than silent masks.  Every stream the writer does not own is
 * either equal to Project's file or a named expected failure; "port == control"
 * is no longer asserted anywhere.
 *
 * W29-0.4 (R28-4) added the four AUTOMATIC-scheduling pairs:
 * wave29-outline-auto-{indent,summary,outdent} and
 * wave29-outline-manual-parent (an automatic row leaving a MANUAL parent).
 * Their saves also rewrite the five schedule words on every recalculated row
 * (+74 FreeSlack, +112 LateStart, +116 LateFinish, +186 StartSlack, +190
 * TotalSlack) and the automatic rows' duration/finish roll-up (+88, +108, and
 * the project row's +84/+88/+100/+108).  The edit carries those measured words
 * (OutlineEdit::slack, hard-coded from Project's files here; the app computes
 * them with the project calendar); the test compares them byte for byte with
 * Project's file, including the hybrid pair's manual parent (recalculated back
 * into the set) and the manual row it leaves stale.
 *
 * The fourth manual case applies indent then outdent in ONE call from the indent
 * control and compares with Project's outdent save: Project produced that file
 * by saving the indent result and then outdenting it, so the whole two-save
 * chain (including the Var2Data append offsets 520/536/548/568) must match.
 *
 * Reader checks use the independent core/mpp reader when the top-level build
 * provides it: the port's file must decode to the same outline Project's file
 * does (levels, outline numbers, summary flags, child counts, durations and
 * dates for uids 1..3).
 *
 * Refusals (documented in outline.h) must leave the document untouched:
 * indenting the first top-level row, indenting into an existing summary,
 * outdenting a summary, outdenting a top-level leaf, and an unknown uid.
 *
 * NO CORPUS NEEDED: all inputs are committed fixtures under tests/data.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "outline.h"
#include "table.h"

#ifdef MPPWRITE_HAVE_READER
#include "reader.h"
#endif

using namespace mppwrite;

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif
#ifndef MPPWRITE_FIXTURE_MIRROR
#define MPPWRITE_FIXTURE_MIRROR "core/mppwrite/build/vs-w28/test-out"
#endif

namespace {

class TestContext {
  public:
    void check(bool condition, std::string const& what) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::cout << "FAIL: " << what << "\n";
        }
    }
    void equal(std::size_t actual, std::size_t expected,
               std::string const& what) {
        check(actual == expected,
              what + " (actual " + std::to_string(actual) + ", expected " +
                  std::to_string(expected) + ")");
    }
    /* W29-0.1 / the W25-5 convention: a gap Project's file shows and the port
     * does not reproduce is a NAMED expected failure.  While the gap is open it
     * prints EXPECTED FAIL and the test stays green; the day the port reproduces
     * Project, the same call is a hard failure so the list cannot rot. */
    void expected_failure(bool project_truth, std::string const& what) {
        if (project_truth) {
            ++failures_;
            std::cout << "FAIL: expected failure '" << what
                      << "' now PASSES -- remove it from the expected-failure "
                         "list and make it a normal check\n";
            return;
        }
        ++expected_failures_;
        std::cout << "EXPECTED FAIL: " << what << "\n";
    }
    int report() const {
        std::cout << checks_ << " checks, " << failures_ << " failures, "
                  << expected_failures_ << " expected failures\n";
        return failures_ == 0 ? 0 : 1;
    }

  private:
    int checks_ = 0;
    int failures_ = 0;
    int expected_failures_ = 0;
};

std::string fixture(std::string const& name) {
    return std::string(MPPWRITE_TEST_DATA) + "/" + name;
}

std::string out_path(std::string const& name) {
    return std::string(MPPWRITE_TEST_OUT) + "/" + name;
}

std::vector<std::uint8_t> read_file(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path);
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

void write_file(std::string const& path, std::vector<std::uint8_t> const& bytes) {
    std::filesystem::path const parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write " + path);
    }
    output.write(reinterpret_cast<char const*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("short write to " + path);
    }
}

std::string task_stream(std::string const& name) {
    return "114/TBkndTask/" + name;
}

std::array<std::string, 6> const kTaskStreams = {
    task_stream("FixedData"), task_stream("FixedMeta"),
    task_stream("Fixed2Data"), task_stream("Fixed2Meta"),
    task_stream("VarMeta"), task_stream("Var2Data")};

bool is_task_stream(std::string const& path) {
    return std::find(kTaskStreams.begin(), kTaskStreams.end(), path) !=
           kTaskStreams.end();
}

std::vector<std::uint8_t> stream_of(CfbDocument const& doc,
                                    std::string const& path) {
    return doc.read_stream(path);
}

std::vector<std::uint8_t> stream_or_empty(CfbDocument const& doc,
                                          std::string const& path) {
    return doc.has_stream(path) ? doc.read_stream(path)
                                : std::vector<std::uint8_t>{};
}

std::string assn_stream(std::string const& name) {
    return "114/TBkndAssn/" + name;
}

std::array<std::string, 6> const kAssnStreams = {
    assn_stream("FixedData"), assn_stream("FixedMeta"),
    assn_stream("Fixed2Data"), assn_stream("Fixed2Meta"),
    assn_stream("VarMeta"), assn_stream("Var2Data")};

/* The streams this writer owns: the six Task streams, the six Assn streams and
 * 114/Props (the save counters). */
bool owned_stream(std::string const& path) {
    return is_task_stream(path) || "114/Props" == path ||
           std::find(kAssnStreams.begin(), kAssnStreams.end(), path) !=
               kAssnStreams.end();
}

std::uint32_t get_u32(std::vector<std::uint8_t> const& bytes,
                      std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint16_t get_u16(std::vector<std::uint8_t> const& bytes,
                      std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

/* One 114/Props record: u32 length / u32 id / u32 kind / payload. */
struct PropsRecord {
    std::uint32_t id = 0;
    std::uint32_t length = 0;
    std::size_t payload = 0;
};

std::vector<PropsRecord> parse_props(std::vector<std::uint8_t> const& props) {
    std::vector<PropsRecord> records;
    std::size_t offset = 16;
    while (offset + 12 <= props.size()) {
        PropsRecord record;
        record.length = get_u32(props, offset);
        record.id = get_u32(props, offset + 4);
        if ((record.id & 0x00FF0000u) != 0x00400000u) {
            break;
        }
        if (static_cast<std::uint64_t>(offset) + 12u + record.length >
            static_cast<std::uint64_t>(props.size())) {
            break;
        }
        record.payload = offset + 12;
        records.push_back(record);
        offset += 12u + record.length;
    }
    return records;
}

PropsRecord const* find_props_record(std::vector<PropsRecord> const& records,
                                     std::uint32_t id) {
    for (PropsRecord const& record : records) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

/* The 16-byte scalar Props record framing outline.cpp writes: u32 4 / big-endian
 * tag / u32 4 / u32 value.  SIZE_MAX unless the tag occurs exactly once. */
std::size_t props_scalar_value(std::vector<std::uint8_t> const& props,
                               std::uint32_t tag) {
    std::size_t found = static_cast<std::size_t>(-1);
    for (std::size_t at = 0; at + 16 <= props.size(); ++at) {
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
            return static_cast<std::size_t>(-1);
        }
        found = at + 12;
    }
    return found;
}

void zero_span(std::vector<std::uint8_t>& bytes, std::size_t at,
               std::size_t n) {
    if (at + n > bytes.size()) {
        return;
    }
    std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(at),
              bytes.begin() + static_cast<std::ptrdiff_t>(at + n),
              std::uint8_t{0});
}

std::string hex_tag(std::uint32_t tag) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%08x", tag);
    return buffer;
}

std::string describe(std::string const& label, std::string const& path,
                     std::size_t actual, std::size_t expected) {
    return label + ": " + path + " is " + std::to_string(actual) +
           " bytes, expected " + std::to_string(expected);
}

void require_same_streams(TestContext& ctx, std::string const& label,
                          CfbDocument const& expected,
                          CfbDocument const& actual) {
    std::vector<std::string> a = expected.stream_paths();
    std::vector<std::string> b = actual.stream_paths();
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    std::vector<std::string> missing;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                        std::back_inserter(missing));
    std::vector<std::string> extra;
    std::set_difference(b.begin(), b.end(), a.begin(), a.end(),
                        std::back_inserter(extra));
    ctx.check(missing.empty() && extra.empty(),
              label + ": stream sets agree");
}

/*
 * Canonicalise the session identity GUIDs of Fixed2Data: each record's +0..15
 * becomes a canonical id (row index in the low u32) and each parent link
 * (+24..39) becomes the parent's canonical id.  A link to an unknown or empty
 * id is kept verbatim (both measured controls use all-zero for no parent).
 */
std::vector<std::uint8_t> canonical_fixed2(
    std::vector<std::uint8_t> const& fixed2, std::size_t rows) {
    constexpr std::size_t kRecord = 64;
    constexpr std::size_t kRowOffset = 24;
    if (fixed2.size() != kRecord * (rows + 3)) {
        throw std::runtime_error("Fixed2Data is not 64*(rows+3) bytes");
    }
    using Id = std::array<std::uint8_t, 16>;
    std::map<Id, Id> canonical;
    for (std::size_t row = 0; row < rows; ++row) {
        Id id{};
        std::size_t const base = kRecord * (row + 3);
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base),
                  fixed2.begin() + static_cast<std::ptrdiff_t>(base + 16),
                  id.begin());
        bool const empty =
            std::all_of(id.begin(), id.end(),
                        [](std::uint8_t b) { return b == 0; });
        if (!empty) {
            Id replacement{};
            replacement[0] = static_cast<std::uint8_t>(row & 0xFFu);
            replacement[1] = static_cast<std::uint8_t>((row >> 8) & 0xFFu);
            replacement[2] = static_cast<std::uint8_t>((row >> 16) & 0xFFu);
            replacement[3] = static_cast<std::uint8_t>((row >> 24) & 0xFFu);
            canonical.emplace(id, replacement);
        }
    }
    std::vector<std::uint8_t> out = fixed2;
    auto replace_id = [&out](std::size_t at, Id const& id) {
        std::copy(id.begin(), id.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(at));
    };
    for (std::size_t row = 0; row < rows; ++row) {
        std::size_t const base = kRecord * (row + 3);
        Id id{};
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base),
                  fixed2.begin() + static_cast<std::ptrdiff_t>(base + 16),
                  id.begin());
        auto const self = canonical.find(id);
        if (self != canonical.end()) {
            replace_id(base, self->second);
        }
        Id parent{};
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base + kRowOffset),
                  fixed2.begin() +
                      static_cast<std::ptrdiff_t>(base + kRowOffset + 16),
                  parent.begin());
        auto const found = canonical.find(parent);
        if (found != canonical.end()) {
            replace_id(base + kRowOffset, found->second);
        }
    }
    return out;
}

/*
 * Canonicalise the TBkndAssn Fixed2Data identities.  A record's own +0..15 is a
 * per-save identity; its parent +16..31 names a TASK Fixed2Data identity (a
 * different id space, the rule actuals.h measured).  Each file resolves the
 * parent through its OWN task identities first.  MEASURED (W29-0.1): Project
 * does NOT rewrite the parent links of assignment rows it does not touch when
 * it reallocates the Task identities on save, so those links are STALE and are
 * resolved through the CONTROL's task identities (the common ancestor) instead.
 * Links that name no identity in either map are kept verbatim: both files carry
 * the same stale bytes there, or the comparison reports a real difference.
 */
using Identity = std::array<std::uint8_t, 16>;

std::map<Identity, Identity> identity_map(
    std::vector<std::uint8_t> const& fixed2, std::size_t records,
    std::size_t stride, std::size_t first_record, std::uint8_t tag) {
    std::map<Identity, Identity> map;
    for (std::size_t record = 0; record < records; ++record) {
        std::size_t const base = stride * (first_record + record);
        Identity id{};
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base),
                  fixed2.begin() + static_cast<std::ptrdiff_t>(base + 16),
                  id.begin());
        bool const empty =
            std::all_of(id.begin(), id.end(),
                        [](std::uint8_t b) { return b == 0; });
        if (!empty) {
            Identity replacement{};
            replacement[0] = tag;
            for (std::size_t i = 0; i < 4; ++i) {
                replacement[1 + i] =
                    static_cast<std::uint8_t>((record >> (8 * i)) & 0xFFu);
            }
            map.emplace(id, replacement);
        }
    }
    return map;
}

std::vector<std::uint8_t> canonical_assn_fixed2(
    std::vector<std::uint8_t> const& assn_fixed2, std::size_t assn_rows,
    std::vector<std::uint8_t> const& task_fixed2, std::size_t task_rows,
    std::vector<std::uint8_t> const& control_task_fixed2,
    std::size_t control_task_rows) {
    constexpr std::size_t kRecord = 48;
    constexpr std::size_t kParent = 16;
    if (assn_fixed2.size() != kRecord * assn_rows) {
        throw std::runtime_error("TBkndAssn/Fixed2Data is not 48*rows bytes");
    }
    std::map<Identity, Identity> const own =
        identity_map(assn_fixed2, assn_rows, kRecord, 0, 0x41);
    std::map<Identity, Identity> const parents =
        identity_map(task_fixed2, task_rows, 64, 3, 0x42);
    std::map<Identity, Identity> const stale =
        identity_map(control_task_fixed2, control_task_rows, 64, 3, 0x42);
    std::vector<std::uint8_t> out = assn_fixed2;
    auto replace_id = [&out](std::size_t at, Identity const& id) {
        std::copy(id.begin(), id.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(at));
    };
    for (std::size_t row = 0; row < assn_rows; ++row) {
        std::size_t const base = kRecord * row;
        Identity id{};
        std::copy(assn_fixed2.begin() + static_cast<std::ptrdiff_t>(base),
                  assn_fixed2.begin() +
                      static_cast<std::ptrdiff_t>(base + 16),
                  id.begin());
        auto const self = own.find(id);
        if (self != own.end()) {
            replace_id(base, self->second);
        }
        Identity parent{};
        std::copy(assn_fixed2.begin() +
                      static_cast<std::ptrdiff_t>(base + kParent),
                  assn_fixed2.begin() +
                      static_cast<std::ptrdiff_t>(base + kParent + 16),
                  parent.begin());
        auto const found = parents.find(parent);
        auto const fallback = stale.find(parent);
        if (found != parents.end()) {
            replace_id(base + kParent, found->second);
        } else if (fallback != stale.end()) {
            replace_id(base + kParent, fallback->second);
        }
    }
    return out;
}

struct PairCase {
    char const* label;
    char const* control;
    char const* project;
    OutlineEdit edit;
};

/*
 * W29-0.1: FixedMeta is now written in full.  The edited row's +2 field count,
 * +26 summary bit and the 16-byte +8..23 cache blob all follow measured rules
 * (set_fixed_meta_blob in outline.cpp), so the whole stream must equal
 * PROJECT's file byte for byte -- the wave-28 test masked the blob and compared
 * it with the control, which R28-1 rejected.
 */
void check_fixed_meta(TestContext& ctx, std::string const& label,
                      CfbDocument const& project, CfbDocument const& port) {
    std::vector<std::uint8_t> const theirs =
        stream_of(project, task_stream("FixedMeta"));
    std::vector<std::uint8_t> const ours =
        stream_of(port, task_stream("FixedMeta"));
    ctx.equal(ours.size(), theirs.size(), label + ": FixedMeta size");
    if (ours.size() == theirs.size()) {
        ctx.check(ours == theirs,
                  label + ": FixedMeta equals Project's byte for byte (+2, "
                          "+8..23 and +26 included)");
    }
}

/*
 * W29-0.1: the outline save's measured side effects.  TBkndAssn (the tombstoned
 * assignment of a new summary and the row an outdent re-creates) and the six
 * 114/Props counters are OWNED and compared with PROJECT's file; every stream
 * the writer does not own is either equal to Project's file or a named expected
 * failure.  Nothing here compares the port with the control.
 */
void check_owned_side_effects(TestContext& ctx, std::string const& label,
                              CfbDocument const& control,
                              CfbDocument const& project,
                              CfbDocument const& port,
                              std::size_t task_rows) {
    for (std::string const& stream : kAssnStreams) {
        if (stream == assn_stream("Fixed2Data")) {
            continue;
        }
        ctx.check(stream_of(port, stream) == stream_of(project, stream),
                  label + ": " + stream + " equals Project's");
    }
    {
        std::vector<std::uint8_t> const port_assn =
            stream_of(port, assn_stream("Fixed2Data"));
        std::vector<std::uint8_t> const project_assn =
            stream_of(project, assn_stream("Fixed2Data"));
        std::vector<std::uint8_t> const project_task_fixed2 =
            stream_of(project, task_stream("Fixed2Data"));
        std::size_t const project_task_rows =
            (stream_of(project, task_stream("FixedData")).size() -
             task_row::kHeaderSize) /
            task_row::kStride;
        std::vector<std::uint8_t> const control_task_fixed2 =
            stream_of(control, task_stream("Fixed2Data"));
        std::size_t const control_task_rows =
            (stream_of(control, task_stream("FixedData")).size() -
             task_row::kHeaderSize) /
            task_row::kStride;
        ctx.equal(port_assn.size(), project_assn.size(),
                  label + ": TBkndAssn/Fixed2Data size");
        if (port_assn.size() == project_assn.size()) {
            ctx.check(canonical_assn_fixed2(
                          port_assn, port_assn.size() / 48,
                          stream_of(port, task_stream("Fixed2Data")), task_rows,
                          control_task_fixed2, control_task_rows) ==
                          canonical_assn_fixed2(
                              project_assn, project_assn.size() / 48,
                              project_task_fixed2, project_task_rows,
                              control_task_fixed2, control_task_rows),
                      label + ": TBkndAssn/Fixed2Data equals Project's after "
                              "identity canonicalisation");
        }
    }

    std::vector<std::uint8_t> const port_props = stream_of(port, "114/Props");
    std::vector<std::uint8_t> const project_props =
        stream_of(project, "114/Props");
    ctx.equal(port_props.size(), project_props.size(),
              label + ": Props keeps Project's length");
    if (port_props.size() == project_props.size()) {
        std::uint32_t const kCounters[] = {0x01000002u, 0x01000100u,
                                           0x04000001u, 0x04000002u,
                                           0x04000100u, 0x04008000u};
        for (std::uint32_t const tag : kCounters) {
            std::size_t const ours = props_scalar_value(port_props, tag);
            std::size_t const theirs = props_scalar_value(project_props, tag);
            ctx.check(ours != static_cast<std::size_t>(-1) &&
                          theirs != static_cast<std::size_t>(-1),
                      label + ": Props carries the " + hex_tag(tag) +
                          " record");
            if (ours != static_cast<std::size_t>(-1) &&
                theirs != static_cast<std::size_t>(-1)) {
                ctx.equal(get_u32(port_props, ours),
                          get_u32(project_props, theirs),
                          label + ": Props " + hex_tag(tag) +
                              " counter equals Project's");
            }
        }
        /* Project reallocates these two per-session id words on every save; the
         * port leaves the control's bytes.  They are the only Props bytes not
         * compared byte for byte with Project's save, and they are a NAMED
         * expected failure, not a silent mask. */
        std::uint32_t const kOpaque[] = {0x02400029u, 0x024013c2u};
        std::vector<PropsRecord> const port_records = parse_props(port_props);
        std::vector<PropsRecord> const project_records =
            parse_props(project_props);
        std::vector<std::uint8_t> port_masked = port_props;
        std::vector<std::uint8_t> project_masked = project_props;
        for (std::uint32_t const id : kOpaque) {
            PropsRecord const* ours = find_props_record(port_records, id);
            PropsRecord const* theirs = find_props_record(project_records, id);
            bool const present = ours != nullptr && theirs != nullptr &&
                                 ours->length >= 4 && theirs->length >= 4;
            ctx.check(present, label + ": Props carries record " + hex_tag(id));
            if (!present) {
                continue;
            }
            ctx.expected_failure(
                get_u16(port_props, ours->payload + 2) ==
                    get_u16(project_props, theirs->payload + 2),
                label + ": Props record " + hex_tag(id) +
                    " per-save id word equals Project's (Project reallocates it"
                    " on every save; the port does not own it)");
            zero_span(port_masked, ours->payload + 2, 2);
            zero_span(project_masked, theirs->payload + 2, 2);
        }
        ctx.check(port_masked == project_masked,
                  label + ": Props equals Project's save except the two per-save"
                          " id words");
    }

    /* R28-1: "port == control" is not the measurement.  A stream Project's save
     * leaves alone must equal Project's file; one it rewrites is a named
     * expected failure, never an assertion that the control's bytes are right. */
    std::vector<std::string> a_paths = control.stream_paths();
    std::vector<std::string> b_paths = port.stream_paths();
    std::sort(a_paths.begin(), a_paths.end());
    std::sort(b_paths.begin(), b_paths.end());
    ctx.check(a_paths == b_paths,
              label + ": the save adds and removes no stream");
    std::vector<std::string> all_paths = a_paths;
    for (std::string const& path : project.stream_paths()) {
        all_paths.push_back(path);
    }
    std::sort(all_paths.begin(), all_paths.end());
    all_paths.erase(std::unique(all_paths.begin(), all_paths.end()),
                    all_paths.end());
    for (std::string const& path : all_paths) {
        if (owned_stream(path)) {
            continue;
        }
        bool const project_changed = stream_or_empty(project, path) !=
                                     stream_or_empty(control, path);
        bool const ours_matches = stream_or_empty(port, path) ==
                                  stream_or_empty(project, path);
        if (project_changed) {
            ctx.expected_failure(
                ours_matches, label + ": " + path +
                                  ": Project's save rewrites it and the port "
                                  "does not reproduce it");
        } else {
            ctx.check(ours_matches,
                      label + ": " + path + " equals Project's save");
        }
    }
}

void apply_and_compare(TestContext& ctx, PairCase const& test) {
    std::string const label = test.label;
    CfbDocument doc = CfbDocument::from_file(fixture(test.control));
    OutlineWriteResult const result =
        write_outline_edits(doc, {test.edit});
    ctx.equal(result.rows_written, 1, label + ": one row written");
    ctx.check(result.warnings.empty(), label + ": no warnings");
    ctx.check(doc.check().empty(), label + ": container check");
    ctx.check(check_table(doc).empty(), label + ": task table check");

    std::string const name = std::string("w28-outline-") + test.label + ".mpp";
    doc.save(out_path(name));
    write_file(std::string(MPPWRITE_FIXTURE_MIRROR) + "/" + name,
               read_file(out_path(name)));

    CfbDocument const control = CfbDocument::from_file(fixture(test.control));
    CfbDocument const project = CfbDocument::from_file(fixture(test.project));
    CfbDocument const port = CfbDocument::from_file(out_path(name));
    require_same_streams(ctx, label, control, port);
    require_same_streams(ctx, label, control, project);

    /* --- the six owned streams ------------------------------------------- */
    std::vector<std::uint8_t> const control_fixed =
        stream_of(control, task_stream("FixedData"));
    std::size_t const rows =
        (control_fixed.size() - task_row::kHeaderSize) / task_row::kStride;

    ctx.check(stream_of(port, task_stream("FixedData")) ==
                  stream_of(project, task_stream("FixedData")),
              label + ": FixedData equals Project's");
    ctx.check(stream_of(port, task_stream("FixedData")) != control_fixed,
              label + ": FixedData actually changed");

    check_fixed_meta(ctx, label, project, port);
    ctx.check(stream_of(port, task_stream("Fixed2Meta")) ==
                  stream_of(project, task_stream("Fixed2Meta")),
              label + ": Fixed2Meta equals Project's");
    ctx.check(stream_of(port, task_stream("VarMeta")) ==
                  stream_of(project, task_stream("VarMeta")),
              label + ": VarMeta equals Project's");
    ctx.check(stream_of(port, task_stream("Var2Data")) ==
                  stream_of(project, task_stream("Var2Data")),
              label + ": Var2Data equals Project's");

    std::vector<std::uint8_t> const port_fixed2 =
        stream_of(port, task_stream("Fixed2Data"));
    std::vector<std::uint8_t> const project_fixed2 =
        stream_of(project, task_stream("Fixed2Data"));
    ctx.equal(port_fixed2.size(), project_fixed2.size(),
              label + ": Fixed2Data size");
    if (port_fixed2.size() == project_fixed2.size()) {
        ctx.check(canonical_fixed2(port_fixed2, rows) ==
                      canonical_fixed2(project_fixed2, rows),
                  label + ": Fixed2Data equals Project's after identity "
                          "canonicalisation");
        /* Project really does allocate fresh identities: prove the
         * canonicalisation is not hiding an identical control. */
        ctx.check(canonical_fixed2(port_fixed2, rows) !=
                      canonical_fixed2(stream_of(control, task_stream(
                                                     "Fixed2Data")),
                                       rows),
                  label + ": Fixed2Data outline bytes changed");
    }

    check_owned_side_effects(ctx, label, control, project, port, rows);

#ifdef MPPWRITE_HAVE_READER
    mpp::Document const project_reader =
        mpp::load_file(fixture(test.project));
    mpp::Document const port_reader =
        mpp::load_file(out_path(name));
    ctx.equal(port_reader.tasks.size(), project_reader.tasks.size(),
              label + ": reader task count");
    auto find_task = [](mpp::Document const& document,
                        std::uint32_t uid) -> mpp::Task const* {
        for (mpp::Task const& task : document.tasks) {
            if (task.uid == uid) {
                return &task;
            }
        }
        return nullptr;
    };
    for (std::uint32_t uid = 1; uid <= 3; ++uid) {
        mpp::Task const* ours = find_task(port_reader, uid);
        mpp::Task const* theirs = find_task(project_reader, uid);
        ctx.check(ours != nullptr && theirs != nullptr,
                  label + ": reader has uid " + std::to_string(uid));
        if (ours == nullptr || theirs == nullptr) {
            continue;
        }
        ctx.equal(ours->outline_level, theirs->outline_level,
                  label + ": uid " + std::to_string(uid) + " outline level");
        ctx.check(ours->outline_number == theirs->outline_number,
                  label + ": uid " + std::to_string(uid) + " outline number");
        ctx.check(ours->summary == theirs->summary,
                  label + ": uid " + std::to_string(uid) + " summary flag");
        ctx.equal(ours->child_count, theirs->child_count,
                  label + ": uid " + std::to_string(uid) + " child count");
        ctx.equal(ours->duration_ticks, theirs->duration_ticks,
                  label + ": uid " + std::to_string(uid) + " duration");
        ctx.equal(ours->duration_format, theirs->duration_format,
                  label + ": uid " + std::to_string(uid) + " duration format");
        ctx.check(ours->start.raw == theirs->start.raw,
                  label + ": uid " + std::to_string(uid) + " start");
        ctx.check(ours->finish.raw == theirs->finish.raw,
                  label + ": uid " + std::to_string(uid) + " finish");
        ctx.check(ours->calculated_start.raw == theirs->calculated_start.raw,
                  label + ": uid " + std::to_string(uid) +
                      " calculated start");
        ctx.check(ours->calculated_finish.raw == theirs->calculated_finish.raw,
                  label + ": uid " + std::to_string(uid) +
                      " calculated finish");
    }
#endif
}

void check_refusal(TestContext& ctx, std::string const& label,
                   std::string const& control, OutlineEdit const& edit,
                   std::string const& why) {
    CfbDocument doc = CfbDocument::from_file(fixture(control));
    OutlineWriteResult const result = write_outline_edits(doc, {edit});
    ctx.equal(result.rows_written, 0, label + ": refused");
    ctx.equal(result.warnings.size(), 1, label + ": one warning (" + why + ")");
    ctx.check(doc.unmodified(), label + ": document untouched");
}

void refusal_cases(TestContext& ctx) {
    OutlineEdit edit;
    edit.uid = 99;
    edit.indent = true;
    edit.summary_duration = 0x2ee0;
    check_refusal(ctx, "unknown uid", "wave28-outline-indent-control.mpp", edit,
                  "uid 99");

    edit.uid = 1;
    check_refusal(ctx, "indent first top-level row",
                  "wave28-outline-indent-control.mpp", edit,
                  "no preceding sibling");

    edit.uid = 1;
    edit.indent = false;
    check_refusal(ctx, "outdent top-level leaf",
                  "wave28-outline-indent-control.mpp", edit,
                  "no parent summary");

    edit.uid = 2;
    check_refusal(ctx, "outdent top-level leaf (second)",
                  "wave28-outline-indent-control.mpp", edit,
                  "no parent summary");

    edit.uid = 1;
    edit.indent = false;
    edit.restored_duration = 0x5dc0;
    edit.restored_duration_format = 0x0007;
    edit.restored_start = 0x3bf212c0;
    edit.restored_finish = 0x3bf627d8;
    check_refusal(ctx, "outdent a summary",
                  "wave28-outline-outdent-control.mpp", edit,
                  "summary move unmeasured");

    /* W29-0.4: the automatic rules run only with the slack vector of exactly
     * the rows Project recalculated.  These refusals prove the set is checked
     * (and the document left untouched) before the first mutation. */
    OutlineEdit auto_indent;
    auto_indent.uid = 2;
    auto_indent.indent = true;
    auto_indent.summary_duration = 0x3840;
    auto_indent.slack = {{0, 0, 0, 0, 0x3bf212c0, 0x3bf427d8}};
    check_refusal(ctx, "automatic indent with a partial slack set",
                  "wave29-outline-auto-indent-control.mpp", auto_indent,
                  "queued slack list does not match");

    auto_indent.slack = {{0, 0, 0, 0, 0, 0x3bf427d8}};
    check_refusal(ctx, "automatic indent with a non-date LateStart",
                  "wave29-outline-auto-indent-control.mpp", auto_indent,
                  "valid LateStart/LateFinish");

    OutlineEdit auto_outdent;
    auto_outdent.uid = 2;
    auto_outdent.indent = false;
    auto_outdent.restored_duration = 0x5dc0;
    auto_outdent.restored_duration_format = 0x0007;
    auto_outdent.restored_start = 0x3bf212c0;
    auto_outdent.restored_finish = 0x3bf627d8;
    check_refusal(ctx, "automatic outdent with no slack",
                  "wave29-outline-auto-outdent-control.mpp", auto_outdent,
                  "queued slack list does not match");

    OutlineEdit manual_indent;
    manual_indent.uid = 2;
    manual_indent.indent = true;
    manual_indent.summary_duration = 0x2ee0;
    manual_indent.slack = {{0, 0, 0, 0, 0x3bf212c0, 0x3bf427d8}};
    check_refusal(ctx, "manual indent carrying automatic slack",
                  "wave28-outline-indent-control.mpp", manual_indent,
                  "takes no slack");
}

/*
 * Indent then outdent in one call, from the indent control.  Project's outdent
 * result was produced from the SUMMARY save, not from this indent control, so
 * the Task streams (which the pair proves) must equal it while the assignment
 * side cannot: the two inputs' TBkndAssn/Var2Data differ by 20 bytes of blocks
 * an earlier session had already dropped.  The chain's assignment comparison is
 * therefore a NAMED expected failure (see the block below), not a claim.
 */
void chain_case(TestContext& ctx) {
    OutlineEdit indent;
    indent.uid = 2;
    indent.indent = true;
    indent.summary_duration = 0x2ee0;
    OutlineEdit outdent;
    outdent.uid = 2;
    outdent.indent = false;
    outdent.restored_duration = 0x5dc0;
    outdent.restored_duration_format = 0x0007;
    outdent.restored_start = 0x3bf212c0;
    outdent.restored_finish = 0x3bf627d8;

    CfbDocument doc =
        CfbDocument::from_file(fixture("wave28-outline-indent-control.mpp"));
    OutlineWriteResult const result =
        write_outline_edits(doc, {indent, outdent});
    ctx.equal(result.rows_written, 2, "chain: two rows written");
    ctx.check(result.warnings.empty(), "chain: no warnings");
    ctx.check(check_table(doc).empty(), "chain: task table check");
    std::string const name = "w28-outline-chain.mpp";
    doc.save(out_path(name));
    write_file(std::string(MPPWRITE_FIXTURE_MIRROR) + "/" + name,
               read_file(out_path(name)));

    CfbDocument const control =
        CfbDocument::from_file(fixture("wave28-outline-indent-control.mpp"));
    CfbDocument const project =
        CfbDocument::from_file(fixture("wave28-outline-outdent.mpp"));
    CfbDocument const port = CfbDocument::from_file(out_path(name));

    std::vector<std::uint8_t> const control_fixed =
        stream_of(control, task_stream("FixedData"));
    std::size_t const rows =
        (control_fixed.size() - task_row::kHeaderSize) / task_row::kStride;
    for (std::string const& stream : kTaskStreams) {
        if (stream == task_stream("FixedMeta")) {
            continue;
        }
        if (stream == task_stream("Fixed2Data")) {
            ctx.check(canonical_fixed2(stream_of(port, stream), rows) ==
                          canonical_fixed2(stream_of(project, stream), rows),
                      "chain: Fixed2Data equals the outdent result after "
                      "canonicalisation");
        } else {
            ctx.check(stream_of(port, stream) == stream_of(project, stream),
                      "chain: " + stream + " equals the outdent result");
        }
    }
    check_fixed_meta(ctx, "chain", project, port);
    ctx.check(stream_of(port, task_stream("FixedData")) ==
                  stream_of(control, task_stream("FixedData")),
              "chain: FixedData returns to the control after outdent");
    /* --- the chain's assignment side --------------------------------------
     * Project never applied two outline edits in ONE save, so this chain is not
     * a Project pair for TBkndAssn.  Its input is the indent control
     * (assignment Var2Data 396 bytes); the outdent pair starts from the SUMMARY
     * save's result (376 bytes: an earlier session had already dropped task 2's
     * tombstoned blocks), so the re-created row lands 20 bytes later, the freed
     * counter keeps the indent step's 120 (Project's file 104), and the
     * tombstoned row's Fixed2Meta carries the indent pair's +52 = 0x08 rather
     * than the summary save's 0x00.  Those are NAMED expected failures; the
     * three measured pairs above are the strict comparison. */
    for (std::string const& stream : kAssnStreams) {
        if (stream == assn_stream("Fixed2Data")) {
            continue;
        }
        bool const equal =
            stream_of(port, stream) == stream_of(project, stream);
        if (stream == assn_stream("Fixed2Meta") ||
            stream == assn_stream("VarMeta") ||
            stream == assn_stream("Var2Data")) {
            ctx.expected_failure(
                equal, "chain: " + stream + " equals the outdent result (the "
                       "chain's input is the indent control, not the summary "
                       "save the outdent pair started from)");
        } else {
            ctx.check(equal, "chain: " + stream + " equals the outdent result");
        }
    }
    {
        std::vector<std::uint8_t> const port_assn =
            stream_of(port, assn_stream("Fixed2Data"));
        std::vector<std::uint8_t> const project_assn =
            stream_of(project, assn_stream("Fixed2Data"));
        std::vector<std::uint8_t> const control_task_fixed2 =
            stream_of(control, task_stream("Fixed2Data"));
        std::size_t const project_task_rows =
            (stream_of(project, task_stream("FixedData")).size() -
             task_row::kHeaderSize) /
            task_row::kStride;
        ctx.expected_failure(
            port_assn.size() == project_assn.size() &&
                canonical_assn_fixed2(
                    port_assn, port_assn.size() / 48,
                    stream_of(port, task_stream("Fixed2Data")), rows,
                    control_task_fixed2, rows) ==
                    canonical_assn_fixed2(
                        project_assn, project_assn.size() / 48,
                        stream_of(project, task_stream("Fixed2Data")),
                        project_task_rows, control_task_fixed2, rows),
            "chain: TBkndAssn/Fixed2Data equals the outdent result after "
            "identity canonicalisation (the outdent pair's assignment rows "
            "carry an older session's parent links)");
    }
    {
        std::vector<std::uint8_t> const port_props =
            stream_of(port, "114/Props");
        std::vector<std::uint8_t> const project_props =
            stream_of(project, "114/Props");
        ctx.equal(port_props.size(), project_props.size(),
                  "chain: Props keeps the outdent result's length");
        std::uint32_t const kCounters[] = {0x01000002u, 0x01000100u,
                                           0x04000001u, 0x04000002u,
                                           0x04000100u, 0x04008000u};
        for (std::uint32_t const tag : kCounters) {
            std::size_t const ours = props_scalar_value(port_props, tag);
            std::size_t const theirs = props_scalar_value(project_props, tag);
            bool const present = ours != static_cast<std::size_t>(-1) &&
                                 theirs != static_cast<std::size_t>(-1);
            ctx.check(present, "chain: Props carries the " + hex_tag(tag) +
                                   " record");
            if (!present) {
                continue;
            }
            bool const equal = get_u32(port_props, ours) ==
                               get_u32(project_props, theirs);
            if (tag == 0x04000002u || tag == 0x04000100u) {
                ctx.expected_failure(
                    equal, "chain: Props " + hex_tag(tag) +
                               " counter equals the outdent result (the "
                               "chain's input carries blocks Project's summary "
                               "save had already dropped)");
            } else {
                ctx.check(equal, "chain: Props " + hex_tag(tag) +
                                     " counter equals the outdent result");
            }
        }
        std::uint32_t const kOpaque[] = {0x02400029u, 0x024013c2u};
        std::vector<PropsRecord> const port_records = parse_props(port_props);
        std::vector<PropsRecord> const project_records =
            parse_props(project_props);
        std::vector<std::uint8_t> port_masked = port_props;
        std::vector<std::uint8_t> project_masked = project_props;
        for (std::uint32_t const id : kOpaque) {
            PropsRecord const* ours = find_props_record(port_records, id);
            PropsRecord const* theirs = find_props_record(project_records, id);
            bool const present = ours != nullptr && theirs != nullptr &&
                                 ours->length >= 4 && theirs->length >= 4;
            ctx.check(present, "chain: Props carries record " + hex_tag(id));
            if (!present) {
                continue;
            }
            zero_span(port_masked, ours->payload + 2, 2);
            zero_span(project_masked, theirs->payload + 2, 2);
        }
        ctx.expected_failure(
            port_masked == project_masked,
            "chain: Props equals the outdent result except the per-save id "
            "words (the two assignment counters differ; see above)");
    }

    OutlineMarker const marker = read_outline_marker(
        CfbDocument::from_file(out_path(name)), 1);
    ctx.check(marker.present, "chain: the restored leaf keeps its marker");
    ctx.equal(marker.duration_ticks, 0x5dc0, "chain: marker duration");
    ctx.equal(marker.duration_format, 0x0007, "chain: marker format");
}

void marker_case(TestContext& ctx) {
    CfbDocument const control =
        CfbDocument::from_file(fixture("wave28-outline-outdent-control.mpp"));
    OutlineMarker const marker = read_outline_marker(control, 1);
    ctx.check(marker.present, "marker: uid 1 has one");
    ctx.equal(marker.duration_ticks, 0x5dc0, "marker: duration 0x5dc0");
    ctx.equal(marker.duration_format, 0x0007, "marker: format 7");
    OutlineMarker const leaf = read_outline_marker(control, 2);
    ctx.check(!leaf.present, "marker: uid 2 (leaf) has none");

    std::vector<OutlineRow> rows;
    std::string error;
    ctx.check(read_outline_rows(control, "114/TBkndTask", rows, error),
              "rows: decode succeeds");
    ctx.equal(rows.size(), 4, "rows: project plus three tasks");
    if (rows.size() == 4) {
        ctx.equal(rows[0].uid, 0, "rows: project uid 0");
        ctx.check(rows[1].parent_row == 0, "rows: uid 1 is top-level");
        ctx.check(rows[2].parent_row == 1, "rows: uid 2's parent is uid 1");
        ctx.check(rows[3].parent_row == 0, "rows: uid 3 is top-level");
        ctx.equal(rows[1].child_rows.size(), 1, "rows: uid 1 has one child");
        ctx.equal(rows[1].stored_child_ids.size(), 1,
                  "rows: uid 1's stored child list");
        if (rows[1].stored_child_ids.size() == 1) {
            ctx.equal(rows[1].stored_child_ids.front(), rows[2].row_id,
                      "rows: stored child is uid 2's row id");
        }
    }
}

} /* namespace */

int main() {
    TestContext ctx;
    try {
        PairCase const cases[] = {
            {"indent", "wave28-outline-indent-control.mpp",
             "wave28-outline-indent.mpp",
             OutlineEdit{2, true, 0x2ee0, 0, 0, kInvalidDate, kInvalidDate}},
            {"summary", "wave28-outline-summary-control.mpp",
             "wave28-outline-summary.mpp",
             OutlineEdit{2, true, 0x2ee0, 0, 0, kInvalidDate, kInvalidDate}},
            {"outdent", "wave28-outline-outdent-control.mpp",
             "wave28-outline-outdent.mpp",
             OutlineEdit{2, false, 0, 0x5dc0, 0x0007, 0x3bf212c0, 0x3bf627d8}},
            /* W29-0.4: the four automatic-scheduling pairs.  The edit carries
             * every recalculated row's +74/+112/+116/+186/+190 words (project
             * row included), plus the restored manual parent in the hybrid
             * pair.  Values measured from Project's result file. */
            {"auto-indent", "wave29-outline-auto-indent-control.mpp",
             "wave29-outline-auto-indent.mpp",
             OutlineEdit{2, true, 0x3840, 0, 0, kInvalidDate, kInvalidDate,
                         {{0, 0, 0, 0, 0x3bf212c0, 0x3bf427d8},
                          {1, 0, 0, 0, 0x3bf212c0, 0x3bf427d8},
                          {2, 0, 0, 0, 0x3bf212c0, 0x3bf427d8},
                          {3, 4800, 4800, 4800, 0x3bf312c0, 0x3bf427d8}}}},
            {"auto-summary", "wave29-outline-auto-summary-control.mpp",
             "wave29-outline-auto-summary.mpp",
             OutlineEdit{2, true, 0x2580, 0, 0, kInvalidDate, kInvalidDate,
                         {{0, 0, 0, 0, 0x3bf212c0, 0x3bf327d8},
                          {1, 0, 0, 0, 0x3bf212c0, 0x3bf327d8},
                          {2, 0, 0, 0, 0x3bf212c0, 0x3bf327d8},
                          {3, 0, 0, 0, 0x3bf212c0, 0x3bf327d8}}}},
            {"auto-outdent", "wave29-outline-auto-outdent-control.mpp",
             "wave29-outline-auto-outdent.mpp",
             OutlineEdit{2, false, 0, 0x5dc0, 0x0007, 0x3bf212c0, 0x3bf627d8,
                         {{0, 0, 0, 0, 0x3bf212c0, 0x3bf627d8},
                          {1, 0, 0, 0, 0x3bf212c0, 0x3bf627d8},
                          {2, 9600, 9600, 9600, 0x3bf412c0, 0x3bf627d8},
                          {3, 14400, 14400, 14400, 0x3bf512c0,
                           0x3bf627d8}}}},
            {"manual-parent", "wave29-outline-manual-parent-control.mpp",
             "wave29-outline-manual-parent.mpp",
             OutlineEdit{2, false, 0, 0x5dc0, 0x0007, 0x3bf212c0, 0x3bf627d8,
                         {{0, 0, 0, 0, 0x3bf11e78, 0x3bf627d8},
                          {1, 0, 0, 0, 0x3bf212c0, 0x3bf627d8},
                          {2, 9600, 9600, 9600, 0x3bf412c0, 0x3bf627d8}}}},
        };
        for (PairCase const& test : cases) {
            apply_and_compare(ctx, test);
        }
        chain_case(ctx);
        marker_case(ctx);
        refusal_cases(ctx);
    } catch (std::exception const& error) {
        std::cout << "FAIL: exception: " << error.what() << "\n";
        return 1;
    }
    return ctx.report();
}
