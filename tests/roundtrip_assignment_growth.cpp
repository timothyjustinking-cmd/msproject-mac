/*
 * roundtrip_assignment_growth.cpp -- W34-3.1: the measured assignment ADD and
 * REMOVE writers against Project's own saves, plus every refusal they offer.
 *
 * WHAT IS PROVEN.  For each pair the port opens PROJECT'S CONTROL, performs
 * the edit through core/mppwrite/assignment_growth.h exactly as the command
 * does (assign resource uid 2 to task uid 1 / remove it), and compares EVERY
 * stream of the four documents with Project's edit save through
 * save_pair::compare_all: the streams the writer owns are declared (their
 * measured effect sets must be non-empty), there is NOT ONE KnownGap, and the
 * only normalisation is the three identity counter dwords of the APPENDED
 * assignment Fixed2Data record -- Project renumbers an appended row's
 * identities on every save, so no port can equal them (declared through
 * save_pair::TailNormalisation; every other appended byte is compared).  The
 * committed null save masks per-save noise.  This is the whole-file form of
 * brief rule 1 on the two pairs that measured the commands.
 *
 * The structural receipts after each pair are independent of the comparison:
 * every stream size and header word, the tombstone and the appended entries,
 * the appended row, the four-block group and its contour, the task and
 * resource roll-ups and flags, the task's 0x0B4004E5 leaf, the four 114/Props
 * assignment scalars, the Task table's Var2Data size scalar and the summary's
 * work string.
 *
 * REFUSALS.  Every refusal branch the committed fixtures can reach is
 * exercised: the project summary row, uid 0 and the placeholder resource word,
 * an unknown task or resource, the wrong input shape for each command, the
 * other pair's control (each command demands its own measured state), and
 * damaged input -- every table's size word, entry count and magic; the settled
 * append shape's own guards (its placeholder layout, row kinds, leaf and the
 * Task size scalar); assignment
 * row fields, FixedMeta/Fixed2Meta entries and Fixed2Data identities; the task
 * and resource roll-ups and flag bytes; the VarMeta header, entries, uid
 * groups, key order and offsets; the Var2Data block lengths and the task leaf;
 * the Props scalars, the session record; and the output paths.  Each refusal
 * asserts the document was not modified and that no output file appeared.
 *
 * NOT CONSTRUCTIBLE HERE: the writer also refuses a document whose Task or
 * Assignment table stream is absent (CfbDocument has no remove-stream
 * operation and every committed .mpp carries them), and the removal's
 * "fewer than two assignment rows" branch (no committed control has a
 * one-row assignment table).  Both are plain guards read by inspection.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assignment_growth.h"
#include "cfb_writer.h"
#include "fixture_guard.h"
#include "save_pair_compare.h"
#include "table.h"

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

using mppwrite::CfbDocument;
using mppwrite::VarEntry;
using Bytes = std::vector<std::uint8_t>;

/* The two measured pairs. */
char const* kAddControl = "wave30-assign-add-control.mpp";
char const* kAddNull = "wave30-assign-add-control-null.mpp";
char const* kAddEdit = "wave30-assign-add.mpp";
char const* kDeleteControl = "wave30-assign-delete-control.mpp";
char const* kDeleteNull = "wave30-assign-delete-control-null.mpp";
char const* kDeleteEdit = "wave30-assign-delete.mpp";

/* The streams the writer owns or reads. */
char const* kAssnFixed = "114/TBkndAssn/FixedData";
char const* kAssnMeta = "114/TBkndAssn/FixedMeta";
char const* kAssnF2 = "114/TBkndAssn/Fixed2Data";
char const* kAssnF2Meta = "114/TBkndAssn/Fixed2Meta";
char const* kAssnVarMeta = "114/TBkndAssn/VarMeta";
char const* kAssnVar2 = "114/TBkndAssn/Var2Data";
char const* kTaskFixed = "114/TBkndTask/FixedData";
char const* kTaskMeta = "114/TBkndTask/FixedMeta";
char const* kTaskF2 = "114/TBkndTask/Fixed2Data";
char const* kTaskVarMeta = "114/TBkndTask/VarMeta";
char const* kTaskVar2 = "114/TBkndTask/Var2Data";
char const* kRscFixed = "114/TBkndRsc/FixedData";
char const* kRscMeta = "114/TBkndRsc/FixedMeta";
char const* kRscF2 = "114/TBkndRsc/Fixed2Data";
char const* kCalFixed = "114/TBkndCal/FixedData";
char const* kCalMeta = "114/TBkndCal/FixedMeta";
char const* kCalVarMeta = "114/TBkndCal/VarMeta";
char const* kCalVar2 = "114/TBkndCal/Var2Data";
char const* kProps = "114/Props";
char const* kMarker = "Props14";
/* The stored DocumentSummaryInformation path carries the OLE control prefix;
 * "\005" is an octal escape, so the following "D" is not part of it. */
char const* kSummary = "\005DocumentSummaryInformation";

/* The measured assignment geometry. */
constexpr std::size_t kAssnRowBytes = 110;
constexpr std::size_t kFmEntry = 34;
constexpr std::size_t kF2mEntry = 53;
constexpr std::size_t kF2Record = 48;

/* The four 114/Props assignment scalar tags, read big-endian on disk. */
constexpr std::uint32_t kCountTag = 0x04000001u;
constexpr std::uint32_t kFreedTag = 0x04000002u;
constexpr std::uint32_t kDirtyTag = 0x04008000u;
constexpr std::uint32_t kSizeTag = 0x04000100u;
/* The Task table's Var2Data size scalar, which the appended task leaf array
 * also raises (measured: 650 -> 666 -> 686). */
constexpr std::uint32_t kTaskSizeTag = 0x01000100u;
constexpr std::uint32_t kSessionRecord = 0x02400007u;

/* The measured null-resource identity of an unassigned placeholder row. */
std::array<std::uint8_t, 16> const& null_identity() {
    static const std::array<std::uint8_t, 16> kNull = {
        0x78, 0x8B, 0xCB, 0xA0, 0x8C, 0x2A, 0x6D, 0x43,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF};
    return kNull;
}

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
    void expected_failure(bool project_truth, std::string const& what) {
        if (project_truth) {
            ++failures_;
            std::cout << "FAIL: expected failure '" << what
                      << "' now PASSES -- make it a normal check\n";
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

std::string join(std::string const& a, std::string const& b) {
    return a + "/" + b;
}

std::string out_path(std::string const& name) {
    return join(MPPWRITE_TEST_OUT, name);
}

std::string missing_path() { return out_path("w34-assign-refused.mpp"); }

CfbDocument doc_from_data(std::string const& name) {
    return CfbDocument::from_file(join(MPPWRITE_TEST_DATA, name));
}

bool file_exists(std::string const& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::uint16_t u16_at(Bytes const& b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}

std::uint32_t u32_at(Bytes const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

std::uint32_t big_u32_at(Bytes const& b, std::size_t o) {
    return (static_cast<std::uint32_t>(b[o]) << 24) |
           (static_cast<std::uint32_t>(b[o + 1]) << 16) |
           (static_cast<std::uint32_t>(b[o + 2]) << 8) |
           static_cast<std::uint32_t>(b[o + 3]);
}

double f64_at(Bytes const& b, std::size_t o) {
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        bits |= static_cast<std::uint64_t>(b[o + i]) << (8 * i);
    }
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

void put_u16_at(Bytes& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

void put_u32_at(Bytes& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

void put_f64_at(Bytes& b, std::size_t o, double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof bits);
    for (std::size_t i = 0; i < 8; ++i) {
        b[o + i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu);
    }
}

bool bytes_equal_at(Bytes const& b, std::size_t o,
                    std::array<std::uint8_t, 16> const& other) {
    return std::equal(other.begin(), other.end(),
                      b.begin() + static_cast<std::ptrdiff_t>(o));
}

bool contains_ascii(Bytes const& b, std::string const& text) {
    return std::search(b.begin(), b.end(), text.begin(), text.end()) !=
           b.end();
}

template <typename Fn>
void expect_error(TestContext& t, Fn&& fn, std::string_view needle,
                  std::string const& what) {
    try {
        fn();
        t.check(false, what + ": no exception was thrown");
    } catch (mppwrite::Error const& error) {
        std::string const message = error.what();
        t.check(needle.empty() || message.find(needle) != std::string::npos,
                what + ": error '" + message + "' does not contain '" +
                    std::string(needle) + "'");
    }
}

/* A copy of `control_name` with one stream passed through `mutate`. */
template <typename Mutator>
CfbDocument with_edit(std::string const& control_name, std::string const& path,
                      Mutator&& mutate) {
    CfbDocument doc = doc_from_data(control_name);
    Bytes bytes = doc.read_stream(path);
    mutate(bytes);
    doc.replace_stream(path, bytes);
    return doc;
}

/* Adds one stored block to the calendar table for `uid` (W35-0.1): `length`
 * payload bytes appended to Var2Data, and a VarMeta entry at its (uid, key)
 * position, with the entry count and the Var2Data size updated. */
void add_cal_block(CfbDocument& doc, std::uint32_t uid, std::uint32_t key,
                   std::size_t length) {
    Bytes var2 = doc.read_stream(kCalVar2);
    Bytes meta = doc.read_stream(kCalVarMeta);
    std::uint32_t const offset = static_cast<std::uint32_t>(var2.size());
    Bytes block(4 + length, 0);
    put_u32_at(block, 0, static_cast<std::uint32_t>(length));
    var2.insert(var2.end(), block.begin(), block.end());
    std::size_t const count = (meta.size() - 24) / 12;
    std::size_t at = count;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t const entry_uid = u32_at(meta, 24 + 12 * i);
        std::uint32_t const entry_key = u32_at(meta, 24 + 12 * i + 8);
        if (entry_uid > uid || (entry_uid == uid && entry_key > key)) {
            at = i;
            break;
        }
    }
    Bytes entry(12, 0);
    put_u32_at(entry, 0, uid);
    put_u32_at(entry, 4, offset);
    put_u32_at(entry, 8, key);
    meta.insert(meta.begin() + static_cast<std::ptrdiff_t>(24 + 12 * at),
                entry.begin(), entry.end());
    put_u32_at(meta, 8, static_cast<std::uint32_t>(count + 1));
    put_u32_at(meta, 0x14, static_cast<std::uint32_t>(var2.size()));
    doc.replace_stream(kCalVar2, var2);
    doc.replace_stream(kCalVarMeta, meta);
}

std::vector<VarEntry> entries_of(CfbDocument const& doc,
                                 std::string const& path) {
    return mppwrite::read_var_entries(doc.read_stream(path));
}

/* The task table's 0x0B4004E5 leaf entry for a task uid. */
struct Leaf {
    std::size_t entry_index = static_cast<std::size_t>(-1);
    std::size_t offset = static_cast<std::size_t>(-1);
};

Leaf task_leaf(CfbDocument const& doc, std::uint32_t uid) {
    for (VarEntry const& entry : entries_of(doc, kTaskVarMeta)) {
        if (entry.mid == uid && entry.field_id == 0x0B4004E5u) {
            Leaf leaf;
            leaf.entry_index = entry.index;
            leaf.offset = entry.offset;
            return leaf;
        }
    }
    return {};
}

/* The offset of a 16-byte typed-property scalar `[4][BE tag][4][value]`. */
std::size_t scalar_offset(Bytes const& props, std::uint32_t tag) {
    for (std::size_t at = 16; at + 16 <= props.size(); ++at) {
        if (u32_at(props, at) == 4 && big_u32_at(props, at + 4) == tag &&
            u32_at(props, at + 8) == 4) {
            return at;
        }
    }
    return static_cast<std::size_t>(-1);
}

std::size_t scalar_value(Bytes const& props, std::uint32_t tag) {
    std::size_t const at = scalar_offset(props, tag);
    return at == static_cast<std::size_t>(-1) ? at : u32_at(props, at + 12);
}

/* The offset of one 114/Props directory record's header, walked from +16. */
std::size_t record_offset(Bytes const& props, std::uint32_t id) {
    std::size_t at = 16;
    while (at + 12 <= props.size()) {
        std::uint32_t const record_id = u32_at(props, at + 4);
        if ((record_id & 0x00FF0000u) != 0x00400000u) {
            break;
        }
        if (record_id == id) {
            return at;
        }
        at += 12 + u32_at(props, at);
    }
    return static_cast<std::size_t>(-1);
}

mppwrite::AssignmentKey key(std::uint32_t task_uid,
                            std::uint32_t resource_uid) {
    mppwrite::AssignmentKey out;
    out.task_uid = task_uid;
    out.resource_uid = resource_uid;
    return out;
}

/* The declared-effect stream lists, exactly the measured effect sets. */
std::array<char const*, 14> const kAddDeclared{{
    kAssnFixed, kAssnMeta, kAssnF2, kAssnF2Meta, kAssnVarMeta, kAssnVar2,
    kTaskFixed, kTaskMeta, kTaskVar2, kRscFixed, kRscMeta, kProps, kSummary,
    kMarker}};
std::array<char const*, 13> const kRemoveDeclared{{
    kAssnFixed, kAssnMeta, kAssnF2, kAssnF2Meta, kAssnVarMeta, kAssnVar2,
    kTaskFixed, kTaskMeta, kTaskVar2, kRscFixed, kProps, kSummary, kMarker}};

/* The only normalisation the pairs declare: the appended record's three
 * identity counter dwords, which Project renumbers on every save. */
std::vector<save_pair::TailNormalisation> tail_normalisation(
    CfbDocument const& null_save) {
    std::size_t const base = null_save.read_stream(kAssnF2).size();
    save_pair::TailNormalisation entry;
    entry.path = kAssnF2;
    entry.ranges = {{base + 0, 4}, {base + 16, 4}, {base + 32, 4}};
    return {entry};
}

/* W34b-0.2 (R34G3-2): the appended record's +16 (task reference) and +32
 * (resource reference) are 16-byte copies of the task row's and the resource
 * row's own identity (Task Fixed2Data record uid+3, Resource Fixed2Data record
 * uid+3; the placeholder constant for an unassigned row).  Their first dwords
 * are masked in the pair comparison because Project renumbers every identity on
 * each save: the control and the null save differ at exactly those dwords on
 * every existing record (e.g. the task's own word 89b9c496 -> f958d0d0).  A mask
 * hides a WRONG reference as well, so the reference is compared by what it
 * names: this resolves the appended record in `file` and reports the first
 * reference that is not the identity of the expected row, or "" when both are.
 * It is run on Project's own save (the measured truth) and on the port's output,
 * so the port's reference is tied to Project's, not only to the port's rows. */
std::string identity_reference_problem(CfbDocument const& file,
                                       std::size_t record_index,
                                       std::uint32_t task_uid,
                                       std::uint32_t resource_uid) {
    Bytes const assn_f2 = file.read_stream(kAssnF2);
    Bytes const task_f2 = file.read_stream(kTaskF2);
    Bytes const rsc_f2 = file.read_stream(kRscF2);
    std::size_t const at = record_index * kF2Record;
    std::size_t const task_at = (task_uid + std::size_t{3}) * 64;
    std::size_t const rsc_at = (resource_uid + std::size_t{3}) * 40;
    if (assn_f2.size() < at + kF2Record || task_f2.size() < task_at + 16 ||
        (resource_uid != 0 && rsc_f2.size() < rsc_at + 16)) {
        return "the record or the row it references is missing";
    }
    if (!std::equal(task_f2.begin() + static_cast<std::ptrdiff_t>(task_at),
                    task_f2.begin() + static_cast<std::ptrdiff_t>(task_at + 16),
                    assn_f2.begin() + static_cast<std::ptrdiff_t>(at + 16))) {
        return "the task reference (+16) is not task " + std::to_string(task_uid) +
               "'s identity";
    }
    if (resource_uid == 0) {
        if (!bytes_equal_at(assn_f2, at + 32, null_identity())) {
            return "the resource reference (+32) is not the placeholder identity";
        }
    } else if (!std::equal(rsc_f2.begin() + static_cast<std::ptrdiff_t>(rsc_at),
                           rsc_f2.begin() +
                               static_cast<std::ptrdiff_t>(rsc_at + 16),
                           assn_f2.begin() + static_cast<std::ptrdiff_t>(at + 32))) {
        return "the resource reference (+32) is not resource " +
               std::to_string(resource_uid) + "'s identity";
    }
    return std::string();
}

/* W35b-4.G0-13 (R34b-4's lesser case, R35G0-13): the appended record's own identity (+0, 16
 * bytes: the counter dword is the one the pair masks, the rest is the shared stable suffix) must
 * differ from every other identity in the Assignment, Task and Resource Fixed2Data streams, each
 * walked at its own record stride.  The mask hides a +0 that collides with another row's, so the
 * comparison is the 16 bytes, not the masked counter.  Returns the first collision, or "". */
std::string identity_collision_problem(CfbDocument const& file, std::size_t record_index) {
    Bytes const assn_f2 = file.read_stream(kAssnF2);
    std::size_t const at = record_index * kF2Record;
    if (assn_f2.size() < at + 16) {
        return "the record is missing";
    }
    struct Stream {
        char const* label;
        Bytes bytes;
        std::size_t stride;
    };
    Stream const streams[] = {{"Assignment", assn_f2, kF2Record},
                              {"Task", file.read_stream(kTaskF2), 64},
                              {"Resource", file.read_stream(kRscF2), 40}};
    for (Stream const& stream : streams) {
        for (std::size_t other = 0; other + 16 <= stream.bytes.size(); other += stream.stride) {
            if (&stream == &streams[0] && other == at) {
                continue;
            }
            if (std::equal(assn_f2.begin() + static_cast<std::ptrdiff_t>(at),
                           assn_f2.begin() + static_cast<std::ptrdiff_t>(at + 16),
                           stream.bytes.begin() + static_cast<std::ptrdiff_t>(other))) {
                return std::string("the record's identity (+0) equals the ") + stream.label +
                       " Fixed2Data record at offset " + std::to_string(other);
            }
        }
    }
    return std::string();
}

/* The pair's appended record is the first one past the null save's records.
 * Checks Project's save and the port's output, and shows the check can fail by
 * planting a wrong task and a wrong resource reference in scratch copies of the
 * port's output. */
void check_identity_references(TestContext& t, std::string const& label,
                               CfbDocument const& null_save,
                               CfbDocument const& project,
                               CfbDocument const& port,
                               std::uint32_t task_uid,
                               std::uint32_t resource_uid,
                               std::uint32_t other_task_uid,
                               std::uint32_t other_resource_uid) {
    std::size_t const record = null_save.read_stream(kAssnF2).size() / kF2Record;
    t.equal(project.read_stream(kAssnF2).size(), (record + 1) * kF2Record,
            label + ": Project's save appends exactly one Fixed2Data record");
    t.equal(port.read_stream(kAssnF2).size(), (record + 1) * kF2Record,
            label + ": the port appends exactly one Fixed2Data record");
    std::string const project_problem =
        identity_reference_problem(project, record, task_uid, resource_uid);
    t.check(project_problem.empty(),
            label + ": Project's appended record references the task and resource "
                    "rows' identities (" + project_problem + ")");
    std::string const port_problem =
        identity_reference_problem(port, record, task_uid, resource_uid);
    t.check(port_problem.empty(),
            label + ": the port's appended record references the same rows "
                    "(" + port_problem + ")");

    /* W35b-4.G0-13: the appended record's own identity is unique, in Project's save and in
     * the port's, and the check fails on a planted collision with a task or a resource row. */
    {
        std::string const project_collision = identity_collision_problem(project, record);
        t.check(project_collision.empty(),
                label + ": Project's appended identity is unique (" + project_collision + ")");
        std::string const port_collision = identity_collision_problem(port, record);
        t.check(port_collision.empty(),
                label + ": the port's appended identity is unique (" + port_collision + ")");
        for (bool const from_task : {true, false}) {
            CfbDocument scratch = port;
            Bytes assn_f2 = scratch.read_stream(kAssnF2);
            Bytes const other_f2 = scratch.read_stream(from_task ? kTaskF2 : kRscF2);
            std::size_t const other =
                (from_task ? other_task_uid + std::size_t{3} : other_resource_uid + std::size_t{3}) *
                (from_task ? 64 : 40);
            std::copy(other_f2.begin() + static_cast<std::ptrdiff_t>(other),
                      other_f2.begin() + static_cast<std::ptrdiff_t>(other + 16),
                      assn_f2.begin() + static_cast<std::ptrdiff_t>(record * kF2Record));
            scratch.replace_stream(kAssnF2, assn_f2);
            std::string const problem = identity_collision_problem(scratch, record);
            t.check(problem.find(from_task ? "equals the Task" : "equals the Resource") !=
                        std::string::npos,
                    label + ": an identity planted from a " + (from_task ? "task" : "resource") +
                        " row is detected (" + problem + ")");
        }
    }
    /* A wrong resource reference in a scratch copy of the port's output. */
    {
        CfbDocument scratch = port;
        Bytes assn_f2 = scratch.read_stream(kAssnF2);
        Bytes const rsc_f2 = scratch.read_stream(kRscF2);
        std::size_t const other = (other_resource_uid + std::size_t{3}) * 40;
        std::copy(rsc_f2.begin() + static_cast<std::ptrdiff_t>(other),
                  rsc_f2.begin() + static_cast<std::ptrdiff_t>(other + 16),
                  assn_f2.begin() +
                      static_cast<std::ptrdiff_t>(record * kF2Record + 32));
        scratch.replace_stream(kAssnF2, assn_f2);
        std::string const problem =
            identity_reference_problem(scratch, record, task_uid, resource_uid);
        std::cout << "   " << label << " planted wrong resource reference -> "
                  << (problem.empty() ? "NOT DETECTED" : problem) << "\n";
        t.check(problem.find("resource reference (+32)") != std::string::npos,
                label + ": a wrong resource reference is detected");
    }
    /* A wrong task reference. */
    {
        CfbDocument scratch = port;
        Bytes assn_f2 = scratch.read_stream(kAssnF2);
        Bytes const task_f2 = scratch.read_stream(kTaskF2);
        std::size_t const other = (other_task_uid + std::size_t{3}) * 64;
        std::copy(task_f2.begin() + static_cast<std::ptrdiff_t>(other),
                  task_f2.begin() + static_cast<std::ptrdiff_t>(other + 16),
                  assn_f2.begin() +
                      static_cast<std::ptrdiff_t>(record * kF2Record + 16));
        scratch.replace_stream(kAssnF2, assn_f2);
        std::string const problem =
            identity_reference_problem(scratch, record, task_uid, resource_uid);
        std::cout << "   " << label << " planted wrong task reference -> "
                  << (problem.empty() ? "NOT DETECTED" : problem) << "\n";
        t.check(problem.find("task reference (+16)") != std::string::npos,
                label + ": a wrong task reference is detected");
    }
}

/* Every stream not in `owned` must be byte-identical in `after`. */
void check_only_owned_streams(TestContext& t, CfbDocument const& before,
                              CfbDocument const& after,
                              std::string const& label,
                              std::vector<char const*> const& owned) {
    for (std::string const& path : before.stream_paths()) {
        bool skip = false;
        for (char const* owned_path : owned) {
            if (path == owned_path) {
                skip = true;
                break;
            }
        }
        if (skip) {
            continue;
        }
        t.check(after.has_stream(path) &&
                    after.read_stream(path) == before.read_stream(path),
                label + ": unowned stream changed or vanished: " + path);
    }
}

/* ---- the ADD pair ------------------------------------------------------- */

void test_add_pair(TestContext& t) {
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kAddControl));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kAddNull));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kAddEdit));

    CfbDocument doc = doc_from_data(kAddControl);
    CfbDocument const before = doc;
    t.equal(doc.read_stream(kAssnFixed).size(), 330,
            "add control: Assn FixedData is 330 B (3 rows)");
    t.equal(doc.read_stream(kAssnMeta).size(), 118,
            "add control: Assn FixedMeta is 118 B (3 entries)");
    t.equal(doc.read_stream(kAssnF2Meta).size(), 175,
            "add control: Assn Fixed2Meta is 175 B (3 entries)");
    t.equal(doc.read_stream(kAssnF2).size(), 144,
            "add control: Assn Fixed2Data is 144 B (3 records)");
    t.equal(doc.read_stream(kAssnVarMeta).size(), 132,
            "add control: Assn VarMeta is 132 B (9 entries)");
    t.equal(doc.read_stream(kAssnVar2).size(), 396,
            "add control: Assn Var2Data is 396 B");
    t.equal(doc.read_stream(kTaskFixed).size(), 856,
            "add control: Task FixedData is 856 B (4 rows)");
    t.equal(doc.read_stream(kTaskMeta).size(), 345,
            "add control: Task FixedMeta is 345 B (7 entries)");
    t.equal(doc.read_stream(kRscFixed).size(), 564,
            "add control: Rsc FixedData is 564 B (3 rows)");
    t.equal(doc.read_stream(kRscMeta).size(), 238,
            "add control: Rsc FixedMeta is 238 B (6 entries)");
    std::vector<std::uint8_t> const props_before = doc.read_stream(kProps);
    t.equal(props_before.size(), 88722, "add control: 114/Props is 88,722 B");
    t.equal(scalar_value(props_before, kCountTag), 3,
            "add control: the assignment count scalar is 3");
    t.equal(scalar_value(props_before, kFreedTag), 0,
            "add control: the freed scalar is 0");
    t.equal(scalar_value(props_before, kDirtyTag), 0,
            "add control: the dirty scalar is 0");
    t.equal(scalar_value(props_before, kSizeTag), 396,
            "add control: the assignment size scalar is 396");
    Leaf const leaf = task_leaf(doc, 1);
    t.equal(leaf.offset, 216, "add control: the task 1 leaf is at 216");

    mppwrite::AssignmentGrowthWrite const written = mppwrite::assign_resource(
        doc, key(1, 2), out_path("w34-assign-add.mpp"));
    t.equal(written.task_uid, 1, "add: task uid");
    t.equal(written.resource_uid, 2, "add: resource uid");
    t.equal(written.assignment_uid, 4, "add: the appended row's uid");
    t.equal(written.tombstoned_uid, 1, "add: the tombstoned placeholder uid");
    t.equal(written.rows_before, 3, "add: rows before");
    t.equal(written.rows_after, 4, "add: rows after");
    t.check(written.work == 2400000.0, "add: the assignment work");
    t.equal(written.work_ticks, 24000, "add: the tick count is 24,000");
    t.equal(written.var2_bytes_before, 396, "add: Var2Data before");
    t.equal(written.var2_bytes_after, 520, "add: Var2Data after");
    t.equal(written.var_entries_before, 9, "add: entries before");
    t.equal(written.var_entries_after, 10, "add: entries after");
    t.equal(written.props_count_before, 3, "add: count scalar before");
    t.equal(written.props_count_after, 4, "add: count scalar after");
    t.equal(written.props_freed_before, 0, "add: freed scalar before");
    t.equal(written.props_freed_after, 120, "add: freed scalar after");
    t.equal(written.props_dirty_after, 1, "add: dirty scalar after");
    t.equal(written.props_size_before, 396, "add: size scalar before");
    t.equal(written.props_size_after, 520, "add: size scalar after");

    /* The saved file: sizes and the assignment table. */
    Bytes const assn_fixed = doc.read_stream(kAssnFixed);
    Bytes const assn_meta = doc.read_stream(kAssnMeta);
    Bytes const assn_f2m = doc.read_stream(kAssnF2Meta);
    Bytes const assn_f2 = doc.read_stream(kAssnF2);
    Bytes const assn_vm = doc.read_stream(kAssnVarMeta);
    Bytes const assn_v2 = doc.read_stream(kAssnVar2);
    t.equal(assn_fixed.size(), 440, "add: Assn FixedData is 440 B");
    t.equal(assn_meta.size(), 152, "add: Assn FixedMeta is 152 B");
    t.equal(assn_f2m.size(), 228, "add: Assn Fixed2Meta is 228 B");
    t.equal(assn_f2.size(), 192, "add: Assn Fixed2Data is 192 B");
    t.equal(assn_vm.size(), 144, "add: Assn VarMeta is 144 B");
    t.equal(assn_v2.size(), 520, "add: Assn Var2Data is 520 B");

    std::size_t const row = 3 * kAssnRowBytes;
    t.equal(u32_at(assn_fixed, row + 0), 4, "add: row 3 uid is 4");
    t.equal(u32_at(assn_fixed, row + 4), 1, "add: row 3 task is 1");
    t.equal(u32_at(assn_fixed, row + 8), 2, "add: row 3 resource is 2");
    t.check(f64_at(assn_fixed, row + 12) == 10000.0, "add: row 3 units");
    t.check(f64_at(assn_fixed, row + 20) == 2400000.0, "add: row 3 work");
    std::vector<std::uint8_t> const control_fixed =
        before.read_stream(kAssnFixed);
    t.check(std::equal(control_fixed.begin(), control_fixed.end(),
                       assn_fixed.begin()),
            "add: every old assignment row byte is kept");

    t.equal(u32_at(assn_meta, 4), 6, "add: FixedMeta header +4 is 6");
    t.equal(u32_at(assn_meta, 8), 4, "add: FixedMeta header +8 is 4");
    t.equal(u32_at(assn_meta, 12), 440, "add: FixedMeta header +12 is 440");
    t.equal(u16_at(assn_meta, 16 + 0), 2, "add: entry 0 is tombstoned");
    t.equal(u16_at(assn_meta, 16 + 2), 0, "add: entry 0's count is 0");
    std::size_t const new_fm = 16 + 3 * kFmEntry;
    t.equal(u16_at(assn_meta, new_fm), 0, "add: the new entry's state is 0");
    t.equal(u16_at(assn_meta, new_fm + 2), 4, "add: the new entry's count is 4");
    t.equal(u32_at(assn_meta, new_fm + 4), 330,
            "add: the new entry points at row 3");
    std::array<std::uint8_t, 7> const fm_tail{{0xF1, 0xFF, 0x23, 0xF0, 0x69,
                                               0x00, 0x02}};
    t.check(std::equal(fm_tail.begin(), fm_tail.end(),
                       assn_meta.begin() +
                           static_cast<std::ptrdiff_t>(new_fm + 8)),
            "add: the new entry's tail is the measured seven bytes");

    t.equal(u32_at(assn_f2m, 8), 4, "add: Fixed2Meta header +8 is 4");
    t.equal(u32_at(assn_f2m, 12), 192, "add: Fixed2Meta header +12 is 192");
    std::size_t const new_f2m = 16 + 3 * kF2mEntry;
    t.equal(u32_at(assn_f2m, new_f2m + 4), 144,
            "add: the new Fixed2Meta entry points at record 3");
    t.equal(assn_f2m[new_f2m + 8], 0xCF, "add: Fixed2Meta +8 is 0xcf");
    t.equal(assn_f2m[new_f2m + 9], 0x09, "add: Fixed2Meta +9 is 0x09");
    t.equal(assn_f2m[new_f2m + 35], 0x10, "add: Fixed2Meta +35 is 0x10");
    t.equal(assn_f2m[new_f2m + 52], 0x28, "add: Fixed2Meta +52 is 0x28");

    /* The appended Fixed2Data record: the task and resource identities are the
     * control's, and the fresh identity keeps the greatest suffix. */
    Bytes const task_f2 = doc.read_stream(kTaskF2);
    Bytes const rsc_f2 = doc.read_stream(kRscF2);
    t.check(std::equal(task_f2.begin() + 64 * 4,
                       task_f2.begin() + 64 * 4 + 16,
                       assn_f2.begin() + 3 * kF2Record + 16),
            "add: record 3's task identity is the task row's");
    t.check(std::equal(rsc_f2.begin() + 40 * 5,
                       rsc_f2.begin() + 40 * 5 + 16,
                       assn_f2.begin() + 3 * kF2Record + 32),
            "add: record 3's resource identity is the resource row's");
    std::uint32_t greatest = 0;
    std::size_t greatest_at = 0;
    for (std::size_t at = 0; at < 3 * kF2Record; at += kF2Record) {
        if (u32_at(assn_f2, at) >= greatest) {
            greatest = u32_at(assn_f2, at);
            greatest_at = at;
        }
    }
    std::size_t const fresh = 3 * kF2Record;
    t.check(u32_at(assn_f2, fresh) > greatest,
            "add: the fresh identity is greater than every old one");
    t.check(std::equal(assn_f2.begin() +
                           static_cast<std::ptrdiff_t>(greatest_at + 4),
                       assn_f2.begin() +
                           static_cast<std::ptrdiff_t>(greatest_at + 16),
                       assn_f2.begin() +
                           static_cast<std::ptrdiff_t>(fresh + 4)),
            "add: the fresh identity keeps the greatest stable suffix");
    t.check(!std::equal(assn_f2.begin() + static_cast<std::ptrdiff_t>(fresh),
                        assn_f2.begin() +
                            static_cast<std::ptrdiff_t>(fresh + 16),
                        assn_f2.begin()),
            "add: the fresh identity collides with no old record");

    /* The VarMeta: the new uid's four entries at the end of the array. */
    t.equal(u32_at(assn_vm, 8), 10, "add: VarMeta header +8 is 10");
    t.equal(u32_at(assn_vm, 0x14), 520, "add: VarMeta +0x14 is 520");
    std::vector<std::uint32_t> const new_keys = {0x0F400031u, 0x0F40027Au,
                                                 0x0F400299u, 0x0F40029Bu};
    std::vector<std::uint32_t> const new_offsets = {404, 396, 480, 500};
    bool group_ok = true;
    for (std::size_t i = 0; i < 4; ++i) {
        std::size_t const at = 24 + (6 + i) * 12;
        group_ok = group_ok && u32_at(assn_vm, at) == 4 &&
                   u32_at(assn_vm, at + 4) == new_offsets[i] &&
                   u32_at(assn_vm, at + 8) == new_keys[i];
    }
    t.check(group_ok, "add: the new uid's four entries are the measured ones");
    std::vector<VarEntry> const control_entries = entries_of(before, kAssnVarMeta);
    bool carried_ok = true;
    for (std::size_t i = 0; i < 6; ++i) {
        std::size_t const at = 24 + i * 12;
        carried_ok = carried_ok &&
                     u32_at(assn_vm, at) == control_entries[i + 3].mid &&
                     u32_at(assn_vm, at + 4) == control_entries[i + 3].offset &&
                     u32_at(assn_vm, at + 8) == control_entries[i + 3].field_id;
    }
    t.check(carried_ok, "add: the surviving groups are the control's entries");

    /* The appended Var2Data group and its contour. */
    Bytes const control_v2 = before.read_stream(kAssnVar2);
    t.check(std::equal(control_v2.begin(), control_v2.end(),
                       assn_v2.begin()),
            "add: the control's Var2Data is preserved");
    t.equal(u32_at(assn_v2, 396), 4, "add: the session block is 4 bytes");
    Bytes const control_props = before.read_stream(kProps);
    std::size_t const session = record_offset(control_props, kSessionRecord);
    t.check(session != static_cast<std::size_t>(-1),
            "add: the control has the session record");
    t.check(std::equal(control_props.begin() +
                           static_cast<std::ptrdiff_t>(session + 12),
                       control_props.begin() +
                           static_cast<std::ptrdiff_t>(session + 16),
                       assn_v2.begin() + 400),
            "add: the session block copies the Props record payload");
    t.equal(u32_at(assn_v2, 404), 72, "add: the contour block is 72 bytes");
    t.equal(u32_at(assn_v2, 408), 0x00200000u, "add: contour magic");
    t.equal(u32_at(assn_v2, 412), 44, "add: contour header length");
    t.check(f64_at(assn_v2, 416) == 160000.0, "add: contour units x16");
    t.check(f64_at(assn_v2, 424) == 2400000.0, "add: contour work");
    t.equal(u32_at(assn_v2, 432), 192000, "add: contour ticks x8");
    t.equal(u32_at(assn_v2, 440), 1, "add: the contour holds one record");
    t.check(f64_at(assn_v2, 452) == 2400000.0, "add: record work");
    t.check(f64_at(assn_v2, 460) == 160000.0, "add: record units x16");
    t.equal(u32_at(assn_v2, 476), 192000, "add: record ticks x8");
    /* The two variable blocks at base+84 and base+104: a 16 length word and a
     * 16-byte zero payload each (Project's measured group). */
    bool zeros_ok = u32_at(assn_v2, 480) == 16 && u32_at(assn_v2, 500) == 16;
    for (std::size_t at = 484; at < 520; ++at) {
        if (at >= 500 && at < 504) {
            continue; /* the second block's length word */
        }
        zeros_ok = zeros_ok && assn_v2[at] == 0;
    }
    t.check(zeros_ok, "add: the two 16-byte blocks are length 16 and zero");

    /* The Task and Resource roll-ups. */
    Bytes const task_fixed = doc.read_stream(kTaskFixed);
    Bytes const task_meta = doc.read_stream(kTaskMeta);
    t.check(f64_at(task_fixed, 48 + 8) == 2400000.0, "add: project work +8");
    t.check(f64_at(task_fixed, 48 + 24) == 2400000.0, "add: project work +24");
    t.check(f64_at(task_fixed, 48 + 202 + 8) == 2400000.0,
            "add: task 1 work +8");
    t.check(f64_at(task_fixed, 48 + 202 + 24) == 2400000.0,
            "add: task 1 work +24");
    t.equal(task_meta[16 + 47 * 3 + 16], 0x17,
            "add: the project task flags are 0x17");
    t.equal(task_meta[16 + 47 * 4 + 16], 0x97,
            "add: task 1's flags are 0x97");
    Bytes const task_v2 = doc.read_stream(kTaskVar2);
    t.equal(u32_at(task_v2, leaf.offset + 4), 1, "add: the leaf's first word");
    t.equal(u32_at(task_v2, leaf.offset + 8), 2,
            "add: the leaf now names resource 2");
    Bytes const rsc_fixed = doc.read_stream(kRscFixed);
    t.check(f64_at(rsc_fixed, 48 + 2 * 172 + 48) == 2400000.0,
            "add: resource 2 work +48");
    t.check(f64_at(rsc_fixed, 48 + 2 * 172 + 90) == 2400000.0,
            "add: resource 2 work +90");
    t.check(f64_at(rsc_fixed, 48 + 2 * 172 + 106) == 2400000.0,
            "add: resource 2 work +106");
    t.equal(doc.read_stream(kRscMeta)[16 + 37 * 3 + 13], 0x00,
            "add: the project resource flag is 0x00");

    /* The four scalars and the summary's work string. */
    Bytes const props = doc.read_stream(kProps);
    t.equal(props.size(), 88722, "add: 114/Props keeps its size");
    t.equal(scalar_value(props, kCountTag), 4, "add: count scalar 4");
    t.equal(scalar_value(props, kFreedTag), 120, "add: freed scalar 120");
    t.equal(scalar_value(props, kDirtyTag), 1, "add: dirty scalar 1");
    t.equal(scalar_value(props, kSizeTag), 520, "add: size scalar 520");
    t.check(contains_ascii(doc.read_stream(kSummary), "40 hrs"),
            "add: the summary's work string is '40 hrs'");

    check_only_owned_streams(t, before, doc, "add",
                             {kAssnFixed, kAssnMeta, kAssnF2, kAssnF2Meta,
                              kAssnVarMeta, kAssnVar2, kTaskFixed, kTaskMeta,
                              kTaskVar2, kRscFixed, kRscMeta, kProps, kSummary,
                              kMarker});

    CfbDocument const null_save = doc_from_data(kAddNull);
    save_pair::compare_all(
        t, "assign-add", doc_from_data(kAddControl), null_save,
        doc_from_data(kAddEdit), CfbDocument::from_file(out_path("w34-assign-add.mpp")),
        nullptr, 0, kAddDeclared.data(), kAddDeclared.size(),
        tail_normalisation(null_save));
    check_identity_references(
        t, "assign-add", null_save, doc_from_data(kAddEdit),
        CfbDocument::from_file(out_path("w34-assign-add.mpp")), 1, 2, 2, 1);
}

/* ---- the REMOVE pair ---------------------------------------------------- */

void test_remove_pair(TestContext& t) {
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kDeleteControl));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kDeleteNull));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kDeleteEdit));

    CfbDocument doc = doc_from_data(kDeleteControl);
    CfbDocument const before = doc;
    t.equal(doc.read_stream(kAssnFixed).size(), 440,
            "remove control: Assn FixedData is 440 B (4 rows)");
    t.equal(doc.read_stream(kAssnMeta).size(), 152,
            "remove control: Assn FixedMeta is 152 B (4 entries)");
    t.equal(doc.read_stream(kAssnF2Meta).size(), 228,
            "remove control: Assn Fixed2Meta is 228 B (4 entries)");
    t.equal(doc.read_stream(kAssnF2).size(), 192,
            "remove control: Assn Fixed2Data is 192 B (4 records)");
    t.equal(doc.read_stream(kAssnVarMeta).size(), 144,
            "remove control: Assn VarMeta is 144 B (10 entries)");
    t.equal(doc.read_stream(kAssnVar2).size(), 520,
            "remove control: Assn Var2Data is 520 B");
    std::vector<std::uint8_t> const props_before = doc.read_stream(kProps);
    t.equal(scalar_value(props_before, kCountTag), 3,
            "remove control: the assignment count scalar is 3");
    t.equal(scalar_value(props_before, kFreedTag), 120,
            "remove control: the freed scalar is 120");
    t.equal(scalar_value(props_before, kDirtyTag), 0,
            "remove control: the dirty scalar is 0");
    t.equal(scalar_value(props_before, kSizeTag), 520,
            "remove control: the assignment size scalar is 520");
    Leaf const leaf = task_leaf(doc, 1);
    t.equal(leaf.offset, 216, "remove control: the task 1 leaf is at 216");

    mppwrite::AssignmentGrowthWrite const written = mppwrite::unassign_resource(
        doc, key(1, 2), out_path("w34-assign-remove.mpp"));
    t.equal(written.assignment_uid, 5, "remove: the appended row's uid");
    t.equal(written.tombstoned_uid, 4, "remove: the tombstoned assignment uid");
    t.equal(written.rows_before, 4, "remove: rows before");
    t.equal(written.rows_after, 5, "remove: rows after");
    t.check(written.work == 2400000.0, "remove: the assignment work");
    t.equal(written.work_ticks, 24000, "remove: the tick count is 24,000");
    t.equal(written.var2_bytes_before, 520, "remove: Var2Data before");
    t.equal(written.var2_bytes_after, 644, "remove: Var2Data after");
    t.equal(written.var_entries_before, 10, "remove: entries before");
    t.equal(written.var_entries_after, 10, "remove: entries after");
    t.equal(written.props_freed_before, 120, "remove: freed scalar before");
    t.equal(written.props_freed_after, 228, "remove: freed scalar after");
    t.equal(written.props_size_after, 644, "remove: size scalar after");

    Bytes const assn_fixed = doc.read_stream(kAssnFixed);
    Bytes const assn_meta = doc.read_stream(kAssnMeta);
    Bytes const assn_f2m = doc.read_stream(kAssnF2Meta);
    Bytes const assn_f2 = doc.read_stream(kAssnF2);
    Bytes const assn_vm = doc.read_stream(kAssnVarMeta);
    Bytes const assn_v2 = doc.read_stream(kAssnVar2);
    t.equal(assn_fixed.size(), 550, "remove: Assn FixedData is 550 B");
    t.equal(assn_meta.size(), 152, "remove: Assn FixedMeta keeps 152 B");
    t.equal(assn_f2m.size(), 228, "remove: Assn Fixed2Meta keeps 228 B");
    t.equal(assn_f2.size(), 240, "remove: Assn Fixed2Data is 240 B");
    t.equal(assn_vm.size(), 144, "remove: Assn VarMeta keeps 144 B");
    t.equal(assn_v2.size(), 644, "remove: Assn Var2Data is 644 B");

    std::size_t const row = 4 * kAssnRowBytes;
    t.equal(u32_at(assn_fixed, row + 0), 5, "remove: row 4 uid is 5");
    t.equal(u32_at(assn_fixed, row + 4), 1, "remove: row 4 task is 1");
    t.equal(u32_at(assn_fixed, row + 8), 0xFFFF0001u,
            "remove: row 4 is an unassigned placeholder");
    t.check(f64_at(assn_fixed, row + 20) == 2400000.0,
            "remove: row 4 keeps the work");
    Bytes const control_fixed = before.read_stream(kAssnFixed);
    t.check(std::equal(control_fixed.begin(), control_fixed.end(),
                       assn_fixed.begin()),
            "remove: every old assignment row byte is kept");

    t.equal(u32_at(assn_meta, 4), 6, "remove: FixedMeta header +4 is 6");
    t.equal(u32_at(assn_meta, 8), 4, "remove: FixedMeta header +8 is 4");
    t.equal(u32_at(assn_meta, 12), 550, "remove: FixedMeta header +12 is 550");
    t.equal(u16_at(assn_meta, 16 + 2 * kFmEntry), 2,
            "remove: the real row's entry is tombstoned");
    t.equal(u16_at(assn_meta, 16 + 2 * kFmEntry + 2), 0,
            "remove: the tombstone's count is 0");
    std::size_t const spare = 16 + 3 * kFmEntry;
    t.equal(u32_at(assn_meta, spare + 4), 440,
            "remove: the spare entry points at row 4");
    t.equal(assn_meta[spare + 13], 0x01, "remove: the spare entry +13 is 0x01");
    t.equal(assn_meta[spare + 12], 0x69, "remove: the spare entry +12 is kept");

    t.equal(u32_at(assn_f2m, 8), 4, "remove: Fixed2Meta header +8 is 4");
    t.equal(u32_at(assn_f2m, 12), 240, "remove: Fixed2Meta header +12 is 240");
    std::size_t const spare_f2 = 16 + 3 * kF2mEntry;
    t.equal(u32_at(assn_f2m, spare_f2 + 4), 192,
            "remove: the spare Fixed2Meta entry points at record 4");
    t.equal(assn_f2m[spare_f2 + 9], 0x01, "remove: the spare +9 is 0x01");
    t.equal(assn_f2m[spare_f2 + 52], 0x28, "remove: the spare +52 is kept");

    Bytes const task_f2 = doc.read_stream(kTaskF2);
    t.check(std::equal(task_f2.begin() + 64 * 4,
                       task_f2.begin() + 64 * 4 + 16,
                       assn_f2.begin() + 4 * kF2Record + 16),
            "remove: record 4's task identity is the task row's");
    t.check(bytes_equal_at(assn_f2, 4 * kF2Record + 32, null_identity()),
            "remove: record 4 names the placeholder resource");

    t.equal(u32_at(assn_vm, 8), 10, "remove: VarMeta header +8 keeps 10");
    t.equal(u32_at(assn_vm, 0x14), 644, "remove: VarMeta +0x14 is 644");
    std::vector<std::uint32_t> const new_keys = {0x0F400031u, 0x0F40027Au,
                                                 0x0F400299u, 0x0F40029Bu};
    std::vector<std::uint32_t> const new_offsets = {528, 520, 604, 624};
    bool group_ok = true;
    for (std::size_t i = 0; i < 4; ++i) {
        std::size_t const at = 24 + (6 + i) * 12;
        group_ok = group_ok && u32_at(assn_vm, at) == 5 &&
                   u32_at(assn_vm, at + 4) == new_offsets[i] &&
                   u32_at(assn_vm, at + 8) == new_keys[i];
    }
    t.check(group_ok, "remove: the re-keyed group is the measured one");

    Bytes const control_v2 = before.read_stream(kAssnVar2);
    t.check(std::equal(control_v2.begin(),
                       control_v2.begin() + static_cast<std::ptrdiff_t>(396),
                       assn_v2.begin()),
            "remove: the control's Var2Data before the group is kept");
    {
        /* The appended group is the real group's copy with its 28-byte contour
         * record (group +56) zeroed. */
        Bytes expected_copy(control_v2.begin() + 396, control_v2.end());
        t.equal(expected_copy.size(), 124, "remove: the copied group is 124 B");
        std::fill(expected_copy.begin() + 56, expected_copy.begin() + 84,
                  std::uint8_t{0});
        t.check(std::equal(expected_copy.begin(), expected_copy.end(),
                           assn_v2.begin() + 520),
                "remove: the appended group is the measured zeroed-record copy");
        /* The record's first dword is zero in the control too; its later
         * dwords (the diffs at group +60..+63 and +69..+71) are not. */
        t.check(std::any_of(control_v2.begin() + (396 + 56),
                            control_v2.begin() + (396 + 84),
                            [](std::uint8_t byte) { return byte != 0; }),
                "remove: the control's contour record is not already zero");
    }

    Bytes const task_fixed = doc.read_stream(kTaskFixed);
    Bytes const task_meta = doc.read_stream(kTaskMeta);
    t.check(f64_at(task_fixed, 48 + 8) == 0.0, "remove: project work +8 is 0");
    t.check(f64_at(task_fixed, 48 + 24) == 0.0, "remove: project work +24 is 0");
    t.check(f64_at(task_fixed, 48 + 202 + 8) == 0.0, "remove: task 1 work +8");
    t.check(f64_at(task_fixed, 48 + 202 + 24) == 0.0, "remove: task 1 work +24");
    t.equal(task_meta[16 + 47 * 3 + 16], 0x17,
            "remove: the project task flags stay 0x17");
    t.equal(task_meta[16 + 47 * 4 + 16], 0x87,
            "remove: task 1's flags are 0x87");
    Bytes const task_v2 = doc.read_stream(kTaskVar2);
    t.equal(u32_at(task_v2, leaf.offset + 8), 0xFFFF0001u,
            "remove: the leaf names the placeholder again");
    Bytes const rsc_fixed = doc.read_stream(kRscFixed);
    t.check(f64_at(rsc_fixed, 48 + 2 * 172 + 48) == 0.0,
            "remove: resource 2 work +48 is 0");
    t.check(f64_at(rsc_fixed, 48 + 2 * 172 + 90) == 0.0,
            "remove: resource 2 work +90 is 0");
    t.check(f64_at(rsc_fixed, 48 + 2 * 172 + 106) == 0.0,
            "remove: resource 2 work +106 is 0");
    t.equal(doc.read_stream(kRscMeta)[16 + 37 * 3 + 13], 0x00,
            "remove: the project resource flag stays 0x00");

    Bytes const props = doc.read_stream(kProps);
    t.equal(scalar_value(props, kCountTag), 4, "remove: count scalar 4");
    t.equal(scalar_value(props, kFreedTag), 228, "remove: freed scalar 228");
    t.equal(scalar_value(props, kDirtyTag), 1, "remove: dirty scalar 1");
    t.equal(scalar_value(props, kSizeTag), 644, "remove: size scalar 644");
    t.check(contains_ascii(doc.read_stream(kSummary), "0 hrs"),
            "remove: the summary's work string is '0 hrs'");

    check_only_owned_streams(t, before, doc, "remove",
                             {kAssnFixed, kAssnMeta, kAssnF2, kAssnF2Meta,
                              kAssnVarMeta, kAssnVar2, kTaskFixed, kTaskMeta,
                              kTaskVar2, kRscFixed, kProps, kSummary, kMarker});

    CfbDocument const null_save = doc_from_data(kDeleteNull);
    save_pair::compare_all(
        t, "assign-remove", doc_from_data(kDeleteControl), null_save,
        doc_from_data(kDeleteEdit),
        CfbDocument::from_file(out_path("w34-assign-remove.mpp")), nullptr, 0,
        kRemoveDeclared.data(), kRemoveDeclared.size(),
        tail_normalisation(null_save));
    check_identity_references(
        t, "assign-remove", null_save, doc_from_data(kDeleteEdit),
        CfbDocument::from_file(out_path("w34-assign-remove.mpp")), 1, 0, 2, 1);
}

/* ---- the settled append pairs (count 1 -> 2 and 2 -> 3) ----------------- */

/* True when two equal-length streams agree outside the listed [from, to)
 * ranges. */
Bytes slice_of(Bytes const& bytes, std::size_t offset, std::size_t length) {
    return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
}

bool equal_except(Bytes const& a, Bytes const& b,
                  std::vector<std::pair<std::size_t, std::size_t>> const& skip) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t at = 0; at < a.size(); ++at) {
        bool skipped = false;
        for (auto const& range : skip) {
            if (at >= range.first && at < range.second) {
                skipped = true;
                break;
            }
        }
        if (!skipped && a[at] != b[at]) {
            return false;
        }
    }
    return true;
}

/* The streams the append edit changes, exactly the measured effect set: the
 * Task and Resource FixedMeta streams stay byte-identical. */
std::array<char const*, 13> const kAppendDeclared{{
    kAssnFixed, kAssnMeta, kAssnF2, kAssnF2Meta, kAssnVarMeta, kAssnVar2,
    kTaskFixed, kTaskVarMeta, kTaskVar2, kRscFixed, kProps, kSummary, kMarker}};
/* A control taken from an earlier edit save already holds the marker's
 * post-save value; Project's null save clears it, so the control/null mask
 * hides the edit's 0 -> 1: the marker has no visible effect and must not be
 * declared (it is still compared byte for byte outside the mask). */
std::array<char const*, 12> const kAppendDeclaredNoMarker{{
    kAssnFixed, kAssnMeta, kAssnF2, kAssnF2Meta, kAssnVarMeta, kAssnVar2,
    kTaskFixed, kTaskVarMeta, kTaskVar2, kRscFixed, kProps, kSummary}};

struct AppendPair {
    char const* control;
    char const* null_save;
    char const* edit;
    char const* label;
    std::uint32_t resource_uid;
    std::size_t rows_before;
    std::uint32_t new_uid;
    std::size_t var2_before;
    std::size_t var2_after;
    std::size_t entries_before;
    std::size_t task_var2_before;
    std::size_t leaf_offset;
    std::size_t leaf_count;
    double old_total;
    double new_total;
    std::size_t rsc_row;
    std::uint32_t dirty_before;
    bool marker_moves;
    char const* summary_old;
    char const* summary_new;
};

void test_append_pair(TestContext& t, AppendPair const& p) {
    std::string const label = p.label;
    fixture::require_committed(join(MPPWRITE_TEST_DATA, p.control));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, p.null_save));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, p.edit));

    CfbDocument doc = doc_from_data(p.control);
    CfbDocument const before = doc;
    Bytes const control_fixed = doc.read_stream(kAssnFixed);
    Bytes const control_meta = doc.read_stream(kAssnMeta);
    Bytes const control_f2m = doc.read_stream(kAssnF2Meta);
    Bytes const control_f2 = doc.read_stream(kAssnF2);
    Bytes const control_vm = doc.read_stream(kAssnVarMeta);
    Bytes const control_v2 = doc.read_stream(kAssnVar2);
    Bytes const control_task_fixed = doc.read_stream(kTaskFixed);
    Bytes const control_task_meta = doc.read_stream(kTaskMeta);
    Bytes const control_task_vm = doc.read_stream(kTaskVarMeta);
    Bytes const control_task_v2 = doc.read_stream(kTaskVar2);
    Bytes const control_rsc_fixed = doc.read_stream(kRscFixed);
    Bytes const control_rsc_meta = doc.read_stream(kRscMeta);
    Bytes const control_props = doc.read_stream(kProps);

    std::size_t const rows_after = p.rows_before + 1;
    t.equal(control_fixed.size(), p.rows_before * kAssnRowBytes,
            label + " control: the assignment table's row count");
    t.equal(control_v2.size(), p.var2_before,
            label + " control: Var2Data size");
    t.equal(control_vm.size(), 24 + p.entries_before * 12,
            label + " control: VarMeta entry count");
    t.equal(scalar_value(control_props, kCountTag), p.rows_before,
            label + " control: count scalar");
    t.equal(scalar_value(control_props, kFreedTag), 120,
            label + " control: freed scalar");
    t.equal(scalar_value(control_props, kDirtyTag), p.dirty_before,
            label + " control: dirty scalar");
    t.equal(scalar_value(control_props, kSizeTag), p.var2_before,
            label + " control: size scalar");
    t.equal(scalar_value(control_props, kTaskSizeTag), p.task_var2_before,
            label + " control: Task size scalar");
    Leaf const leaf = task_leaf(doc, 1);
    t.equal(leaf.offset, p.leaf_offset, label + " control: the task 1 leaf");
    t.equal(u32_at(control_task_v2, leaf.offset + 4), p.leaf_count,
            label + " control: the leaf's resource count");
    t.check(f64_at(control_task_fixed, 48 + 8) == p.old_total,
            label + " control: the project work is the old total");
    t.check(f64_at(control_task_fixed, 48 + 202 + 8) == p.old_total,
            label + " control: the task work is the old total");

    mppwrite::AssignmentGrowthWrite const written = mppwrite::assign_resource(
        doc, key(1, p.resource_uid), out_path("w34-append-" + label + ".mpp"));
    t.equal(written.assignment_uid, p.new_uid,
            label + ": the appended row's uid");
    t.equal(written.tombstoned_uid, 0,
            label + ": nothing is tombstoned in this shape");
    t.equal(written.rows_before, p.rows_before, label + ": rows before");
    t.equal(written.rows_after, rows_after, label + ": rows after");
    t.check(written.work == 2400000.0, label + ": the copied row's work");
    t.equal(written.work_ticks, 24000, label + ": the tick count");
    t.equal(written.var2_bytes_before, p.var2_before, label + ": Var2 before");
    t.equal(written.var2_bytes_after, p.var2_after, label + ": Var2 after");
    t.equal(written.var_entries_before, p.entries_before,
            label + ": entries before");
    t.equal(written.var_entries_after, p.entries_before + 4,
            label + ": entries after");
    t.equal(written.props_count_before, p.rows_before, label + ": count before");
    t.equal(written.props_count_after, rows_after, label + ": count after");
    t.equal(written.props_freed_before, 120, label + ": freed before");
    t.equal(written.props_freed_after, 120,
            label + ": the freed scalar is unchanged");
    t.equal(written.props_dirty_before, p.dirty_before,
            label + ": dirty before");
    t.equal(written.props_dirty_after, 0, label + ": dirty after is 0");
    t.equal(written.props_size_before, p.var2_before, label + ": size before");
    t.equal(written.props_size_after, p.var2_after, label + ": size after");

    /* The saved file's sizes. */
    Bytes const assn_fixed = doc.read_stream(kAssnFixed);
    Bytes const assn_meta = doc.read_stream(kAssnMeta);
    Bytes const assn_f2m = doc.read_stream(kAssnF2Meta);
    Bytes const assn_f2 = doc.read_stream(kAssnF2);
    Bytes const assn_vm = doc.read_stream(kAssnVarMeta);
    Bytes const assn_v2 = doc.read_stream(kAssnVar2);
    Bytes const task_fixed = doc.read_stream(kTaskFixed);
    Bytes const task_meta = doc.read_stream(kTaskMeta);
    Bytes const task_vm = doc.read_stream(kTaskVarMeta);
    Bytes const task_v2 = doc.read_stream(kTaskVar2);
    Bytes const rsc_fixed = doc.read_stream(kRscFixed);
    t.equal(assn_fixed.size(), rows_after * kAssnRowBytes,
            label + ": Assn FixedData size");
    t.equal(assn_meta.size(), 16 + rows_after * kFmEntry,
            label + ": Assn FixedMeta size");
    t.equal(assn_f2m.size(), 16 + rows_after * kF2mEntry,
            label + ": Assn Fixed2Meta size");
    t.equal(assn_f2.size(), rows_after * kF2Record,
            label + ": Assn Fixed2Data size");
    t.equal(assn_vm.size(), 24 + (p.entries_before + 4) * 12,
            label + ": Assn VarMeta size");
    t.equal(assn_v2.size(), p.var2_after, label + ": Assn Var2Data size");
    t.equal(task_fixed.size(), control_task_fixed.size(),
            label + ": Task FixedData size is unchanged");
    t.equal(task_meta.size(), control_task_meta.size(),
            label + ": Task FixedMeta size is unchanged");
    t.equal(task_vm.size(), control_task_vm.size(),
            label + ": Task VarMeta size is unchanged");
    t.equal(task_v2.size(), p.task_var2_before + 8 + 4 * (p.leaf_count + 1),
            label + ": Task Var2Data holds the appended leaf array");
    t.equal(rsc_fixed.size(), control_rsc_fixed.size(),
            label + ": Rsc FixedData size is unchanged");
    t.check(doc.read_stream(kRscMeta) == control_rsc_meta,
            label + ": Rsc FixedMeta is byte-identical");

    /* The appended row is a copy of the first real row with the new uid and
     * resource; every old row is kept. */
    t.check(std::equal(control_fixed.begin(), control_fixed.end(),
                       assn_fixed.begin()),
            label + ": every old assignment row byte is kept");
    std::size_t const model_row = p.rows_before - p.leaf_count;
    Bytes const model = slice_of(control_fixed, model_row * kAssnRowBytes,
                                 kAssnRowBytes);
    Bytes const appended_row =
        slice_of(assn_fixed, p.rows_before * kAssnRowBytes, kAssnRowBytes);
    t.equal(u32_at(appended_row, 0), p.new_uid, label + ": row uid");
    t.equal(u32_at(appended_row, 4), 1, label + ": row task uid");
    t.equal(u32_at(appended_row, 8), p.resource_uid, label + ": row resource");
    t.check(f64_at(appended_row, 12) == 10000.0, label + ": row units");
    t.check(f64_at(appended_row, 20) == 2400000.0, label + ": row work");
    t.check(equal_except(model, appended_row, {{0, 4}, {8, 12}}),
            label + ": the appended row copies the real row's every other byte");

    /* The FixedMeta and Fixed2Meta: one appended entry, no tombstone. */
    t.check(equal_except(slice_of(assn_meta, 0, control_meta.size()),
                         control_meta, {{8, 16}}),
            label + ": every old FixedMeta byte including +4 is kept");
    t.equal(u32_at(assn_meta, 8), rows_after, label + ": FixedMeta count");
    t.equal(u32_at(assn_meta, 12), rows_after * kAssnRowBytes,
            label + ": FixedMeta size word");
    std::size_t const new_fm = 16 + p.rows_before * kFmEntry;
    t.equal(u16_at(assn_meta, new_fm), 0, label + ": the new entry's state");
    t.equal(u16_at(assn_meta, new_fm + 2), 4, label + ": the new entry's count");
    t.equal(u32_at(assn_meta, new_fm + 4), p.rows_before * kAssnRowBytes,
            label + ": the new entry's row offset");
    std::array<std::uint8_t, 7> const fm_tail{{0xF1, 0xFF, 0x23, 0xF0, 0x69,
                                               0x00, 0x02}};
    t.check(std::equal(fm_tail.begin(), fm_tail.end(),
                       assn_meta.begin() +
                           static_cast<std::ptrdiff_t>(new_fm + 8)),
            label + ": the new FixedMeta entry's tail");
    t.check(equal_except(slice_of(assn_f2m, 0, control_f2m.size()),
                         control_f2m, {{8, 16}}),
            label + ": every old Fixed2Meta byte is kept");
    t.equal(u32_at(assn_f2m, 8), rows_after, label + ": Fixed2Meta count");
    t.equal(u32_at(assn_f2m, 12), rows_after * kF2Record,
            label + ": Fixed2Meta size word");
    std::size_t const new_f2m = 16 + p.rows_before * kF2mEntry;
    t.equal(u32_at(assn_f2m, new_f2m + 4), p.rows_before * kF2Record,
            label + ": the new Fixed2Meta entry's record offset");
    t.equal(assn_f2m[new_f2m + 8], 0xCF, label + ": Fixed2Meta +8");
    t.equal(assn_f2m[new_f2m + 9], 0x09, label + ": Fixed2Meta +9");
    t.equal(assn_f2m[new_f2m + 35], 0x10, label + ": Fixed2Meta +35");
    t.equal(assn_f2m[new_f2m + 52], 0x28, label + ": Fixed2Meta +52");

    /* The appended Fixed2Data record references the control's task and
     * resource identities; the fresh counter is greater than every old one
     * and keeps the greatest suffix. */
    t.check(std::equal(control_f2.begin(), control_f2.end(), assn_f2.begin()),
            label + ": every old Fixed2Data record is kept");
    std::size_t const fresh = p.rows_before * kF2Record;
    std::uint32_t greatest = 0;
    std::size_t greatest_at = 0;
    for (std::size_t at = 0; at < fresh; at += kF2Record) {
        if (u32_at(assn_f2, at) >= greatest) {
            greatest = u32_at(assn_f2, at);
            greatest_at = at;
        }
    }
    t.check(u32_at(assn_f2, fresh) > greatest,
            label + ": the fresh identity is greater than every old one");
    t.check(std::equal(assn_f2.begin() +
                           static_cast<std::ptrdiff_t>(greatest_at + 4),
                       assn_f2.begin() +
                           static_cast<std::ptrdiff_t>(greatest_at + 16),
                       assn_f2.begin() +
                           static_cast<std::ptrdiff_t>(fresh + 4)),
            label + ": the fresh identity keeps the stable suffix");
    Bytes const task_f2 = doc.read_stream(kTaskF2);
    t.check(std::equal(task_f2.begin() + static_cast<std::ptrdiff_t>(64 * 4),
                       task_f2.begin() +
                           static_cast<std::ptrdiff_t>(64 * 4 + 16),
                       assn_f2.begin() +
                           static_cast<std::ptrdiff_t>(fresh + 16)),
            label + ": the record's task identity is the task row's");
    Bytes const rsc_f2 = doc.read_stream(kRscF2);
    t.check(std::equal(rsc_f2.begin() +
                           static_cast<std::ptrdiff_t>(40 * (p.rsc_row + 3)),
                       rsc_f2.begin() +
                           static_cast<std::ptrdiff_t>(40 * (p.rsc_row + 3) + 16),
                       assn_f2.begin() +
                           static_cast<std::ptrdiff_t>(fresh + 32)),
            label + ": the record's resource identity is the resource row's");

    /* The VarMeta: every old entry is kept and the new uid's four entries are
     * appended in ascending key order; its blocks tile the new Var2Data. */
    t.check(equal_except(slice_of(assn_vm, 0, control_vm.size()), control_vm,
                         {{8, 12}, {0x14, 0x18}}),
            label + ": every old VarMeta entry is kept");
    t.equal(u32_at(assn_vm, 8), p.entries_before + 4, label + ": entry count");
    t.equal(u32_at(assn_vm, 0x14), p.var2_after, label + ": Var2 size word");
    std::vector<std::uint32_t> const new_keys = {0x0F400031u, 0x0F40027Au,
                                                 0x0F400299u, 0x0F40029Bu};
    std::vector<std::size_t> const new_offsets = {
        p.var2_before + 8, p.var2_before, p.var2_before + 84,
        p.var2_before + 104};
    bool entries_ok = true;
    for (std::size_t i = 0; i < 4; ++i) {
        std::size_t const at = 24 + (p.entries_before + i) * 12;
        entries_ok = entries_ok && u32_at(assn_vm, at) == p.new_uid &&
                     u32_at(assn_vm, at + 4) == new_offsets[i] &&
                     u32_at(assn_vm, at + 8) == new_keys[i];
    }
    t.check(entries_ok, label + ": the new uid's four entries are measured");
    t.check(std::equal(control_v2.begin(), control_v2.end(), assn_v2.begin()),
            label + ": every old Var2Data byte is kept");

    /* The appended group: the session word, the flat contour and the two
     * 16-byte blocks, at the measured offsets from the old end. */
    std::size_t const base = p.var2_before;
    t.equal(u32_at(assn_v2, base), 4, label + ": the session block is 4 B");
    std::size_t const session = record_offset(control_props, kSessionRecord);
    t.check(session != static_cast<std::size_t>(-1),
            label + ": the control has the session record");
    t.check(std::equal(control_props.begin() +
                           static_cast<std::ptrdiff_t>(session + 12),
                       control_props.begin() +
                           static_cast<std::ptrdiff_t>(session + 16),
                       assn_v2.begin() + static_cast<std::ptrdiff_t>(base + 4)),
            label + ": the session block copies the Props payload");
    t.equal(u32_at(assn_v2, base + 8), 72, label + ": the contour is 72 B");
    t.equal(u32_at(assn_v2, base + 12), 0x00200000u,
            label + ": contour magic");
    t.equal(u32_at(assn_v2, base + 16), 44, label + ": contour header length");
    t.check(f64_at(assn_v2, base + 20) == 160000.0, label + ": contour units");
    t.check(f64_at(assn_v2, base + 28) == 2400000.0, label + ": contour work");
    t.equal(u32_at(assn_v2, base + 36), 192000, label + ": contour ticks");
    t.equal(u32_at(assn_v2, base + 44), 1, label + ": one contour record");
    t.check(f64_at(assn_v2, base + 56) == 2400000.0, label + ": record work");
    t.check(f64_at(assn_v2, base + 64) == 160000.0, label + ": record units");
    t.equal(u32_at(assn_v2, base + 80), 192000, label + ": record ticks");
    bool zeros_ok = u32_at(assn_v2, base + 84) == 16 &&
                    u32_at(assn_v2, base + 104) == 16;
    for (std::size_t at = base + 88; at < base + 124; ++at) {
        if (at >= base + 104 && at < base + 108) {
            continue; /* the second block's length word */
        }
        zeros_ok = zeros_ok && assn_v2[at] == 0;
    }
    t.check(zeros_ok, label + ": the two 16-byte blocks are length 16 and zero");

    /* The task roll-ups and the leaf array. */
    t.check(equal_except(control_task_fixed, task_fixed,
                         {{56, 64}, {72, 80}, {258, 266}, {274, 282}}),
            label + ": only the four task work words move");
    t.check(f64_at(task_fixed, 56) == p.new_total, label + ": project work +8");
    t.check(f64_at(task_fixed, 72) == p.new_total, label + ": project work +24");
    t.check(f64_at(task_fixed, 258) == p.new_total, label + ": task work +8");
    t.check(f64_at(task_fixed, 274) == p.new_total, label + ": task work +24");
    t.check(task_meta == control_task_meta,
            label + ": the Task FixedMeta is byte-identical");
    t.check(equal_except(control_task_vm, task_vm,
                         {{24 + leaf.entry_index * 12 + 4,
                           24 + leaf.entry_index * 12 + 8},
                          {0x14, 0x18}}),
            label + ": only the leaf entry's offset and the size word move");
    t.equal(u32_at(task_vm, 24 + leaf.entry_index * 12 + 4),
            p.task_var2_before, label + ": the leaf entry points at the old end");
    t.equal(u32_at(task_vm, 0x14),
            p.task_var2_before + 8 + 4 * (p.leaf_count + 1),
            label + ": the Task VarMeta size word");
    t.check(std::equal(control_task_v2.begin(), control_task_v2.end(),
                       task_v2.begin()),
            label + ": the stale leaf record is kept");
    std::vector<std::uint32_t> leaf_uids;
    for (std::size_t i = 0; i < p.leaf_count; ++i) {
        leaf_uids.push_back(u32_at(control_task_v2, p.leaf_offset + 8 + 4 * i));
    }
    leaf_uids.push_back(p.resource_uid);
    std::sort(leaf_uids.begin(), leaf_uids.end());
    bool leaf_ok = u32_at(task_v2, p.task_var2_before) ==
                       4 + 4 * (p.leaf_count + 1) &&
                   u32_at(task_v2, p.task_var2_before + 4) == p.leaf_count + 1;
    for (std::size_t i = 0; i < leaf_uids.size(); ++i) {
        leaf_ok = leaf_ok &&
                  u32_at(task_v2, p.task_var2_before + 8 + 4 * i) ==
                      leaf_uids[i];
    }
    t.check(leaf_ok, label + ": the appended leaf array is the sorted uids");

    /* The resource roll-up and the four scalars. */
    t.check(equal_except(control_rsc_fixed, rsc_fixed,
                         {{48 + p.rsc_row * 172 + 48,
                           48 + p.rsc_row * 172 + 56},
                          {48 + p.rsc_row * 172 + 90,
                           48 + p.rsc_row * 172 + 98},
                          {48 + p.rsc_row * 172 + 106,
                           48 + p.rsc_row * 172 + 114}}),
            label + ": only the target resource's three work words move");
    for (std::size_t offset : {std::size_t{48}, std::size_t{90},
                               std::size_t{106}}) {
        t.check(f64_at(rsc_fixed, 48 + p.rsc_row * 172 + offset) == 2400000.0,
                label + ": the target resource's work word at +" +
                    std::to_string(offset));
    }
    Bytes const props = doc.read_stream(kProps);
    t.equal(props.size(), control_props.size(),
            label + ": 114/Props keeps its size");
    t.equal(scalar_value(props, kCountTag), rows_after, label + ": count");
    t.equal(scalar_value(props, kFreedTag), 120,
            label + ": the freed scalar is unchanged");
    t.equal(scalar_value(props, kDirtyTag), 0, label + ": dirty scalar 0");
    t.equal(scalar_value(props, kSizeTag), p.var2_after, label + ": size");
    t.equal(scalar_value(props, kTaskSizeTag),
            p.task_var2_before + 8 + 4 * (p.leaf_count + 1),
            label + ": Task size");
    t.check(contains_ascii(doc.read_stream(kSummary), p.summary_new),
            label + ": the summary work string is '" +
                std::string(p.summary_new) + "'");
    t.check(!contains_ascii(doc.read_stream(kSummary), p.summary_old),
            label + ": the old summary work string is gone");

    check_only_owned_streams(t, before, doc, label,
                             {kAssnFixed, kAssnMeta, kAssnF2, kAssnF2Meta,
                              kAssnVarMeta, kAssnVar2, kTaskFixed, kTaskMeta,
                              kTaskVarMeta, kTaskVar2, kRscFixed, kRscMeta,
                              kProps, kSummary, kMarker});

    CfbDocument const null_save = doc_from_data(p.null_save);
    save_pair::compare_all(
        t, label, doc_from_data(p.control), null_save,
        doc_from_data(p.edit),
        CfbDocument::from_file(out_path("w34-append-" + label + ".mpp")),
        nullptr, 0,
        p.marker_moves ? kAppendDeclared.data() : kAppendDeclaredNoMarker.data(),
        p.marker_moves ? kAppendDeclared.size() : kAppendDeclaredNoMarker.size(),
        tail_normalisation(null_save));
    check_identity_references(
        t, label, null_save, doc_from_data(p.edit),
        CfbDocument::from_file(out_path("w34-append-" + label + ".mpp")), 1,
        p.resource_uid, 2, p.resource_uid == 1 ? 2 : 1);
}

void test_append_pairs(TestContext& t) {
    AppendPair const second{
        "wave34-assign-add-second-control.mpp",
        "wave34-assign-add-second-control-null.mpp",
        "wave34-assign-add-second.mpp",
        "assign-add-second",
        1, /* resource uid */
        4, /* rows before */
        5, /* new uid */
        520, /* Var2 before */
        644, /* Var2 after */
        10, /* assignment VarMeta entries */
        650, /* Task Var2 before */
        216, /* leaf offset */
        1, /* leaf count */
        2400000.0, /* old total work */
        4800000.0, /* new total work */
        1, /* resource row */
        1, /* dirty before */
        true, /* the control's marker is 0, so the edit moves it */
        "40 hrs",
        "80 hrs"};
    AppendPair const third{
        "wave34-assign-add-third-control.mpp",
        "wave34-assign-add-third-control-null.mpp",
        "wave34-assign-add-third.mpp",
        "assign-add-third",
        3, /* resource uid */
        5, /* rows before */
        6, /* new uid */
        644, /* Var2 before */
        768, /* Var2 after */
        14, /* assignment VarMeta entries */
        666, /* Task Var2 before */
        650, /* leaf offset */
        2, /* leaf count */
        4800000.0, /* old total work */
        7200000.0, /* new total work */
        3, /* resource row */
        0, /* dirty before */
        false, /* the control already holds the marker's post-save value */
        "80 hrs",
        "120 hrs"};
    test_append_pair(t, second);
    test_append_pair(t, third);
}

/* ---- refusals ----------------------------------------------------------- */

void check_unchanged(TestContext& t, CfbDocument const& doc, Bytes const& before,
                     std::string const& what) {
    t.check(doc.to_bytes() == before, what + ": the document changed");
    t.check(!file_exists(missing_path()), what + ": an output file was written");
}

void test_edit_refusals(TestContext& t) {
    std::string const missing = missing_path();
    auto refuses_add = [&](CfbDocument const& source,
                           mppwrite::AssignmentKey const& which,
                           std::string_view needle, std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        expect_error(t,
                     [&] { (void)mppwrite::assign_resource(doc, which, missing); },
                     needle, what);
        check_unchanged(t, doc, before, what);
    };
    auto refuses_remove = [&](CfbDocument const& source,
                              mppwrite::AssignmentKey const& which,
                              std::string_view needle,
                              std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        expect_error(
            t, [&] { (void)mppwrite::unassign_resource(doc, which, missing); },
            needle, what);
        check_unchanged(t, doc, before, what);
    };

    CfbDocument const add = doc_from_data(kAddControl);
    CfbDocument const remove = doc_from_data(kDeleteControl);

    /* The key itself. */
    refuses_add(add, key(0, 2), "project summary row", "add: task uid 0");
    refuses_add(add, key(1, 0), "not a resource this command can assign",
                "add: resource uid 0");
    refuses_add(add, key(1, mppwrite::kPlaceholderResourceUid),
                "not a resource this command can assign",
                "add: the placeholder resource word");
    refuses_add(add, key(42, 2), "no task row with UniqueID 42",
                "add: an unknown task");
    refuses_add(add, key(1, 42), "the resource table has no UniqueID 42",
                "add: an unknown resource");
    refuses_remove(remove, key(0, 2), "project summary row", "remove: task uid 0");
    refuses_remove(remove, key(1, 0), "not a resource this command can remove",
                   "remove: resource uid 0");
    refuses_remove(remove, key(1, mppwrite::kPlaceholderResourceUid),
                   "not a resource this command can remove",
                   "remove: the placeholder resource word");
    refuses_remove(remove, key(42, 2), "no task row with UniqueID 42",
                   "remove: an unknown task");
    refuses_remove(remove, key(1, 42),
                   "0x0B4004E5 leaf does not name the resource",
                   "remove: a resource the task's leaf does not name");
    {
        /* With the leaf naming 42, the unknown-resource check is reached. */
        CfbDocument const control = doc_from_data(kDeleteControl);
        Leaf const leaf = task_leaf(control, 1);
        refuses_remove(with_edit(kDeleteControl, kTaskVar2,
                                 [&](Bytes& b) {
                                     put_u32_at(b, leaf.offset + 8, 42);
                                 }),
                       key(1, 42), "the resource table has no UniqueID 42",
                       "remove: an unknown resource");
    }

    /* Each command demands its own measured input state. */
    refuses_add(remove, key(1, 2),
                "the assignment FixedMeta is not the measured rows / rows shape",
                "add: the settled removal shape");
    refuses_remove(add, key(1, 2),
                   "0x0B4004E5 leaf does not name the resource",
                   "remove: the three-placeholder add shape");
    refuses_remove(remove, key(2, 2),
                   "0x0B4004E5 leaf does not name the resource",
                   "remove: a task whose leaf names no resource");
    refuses_remove(remove, key(1, 1),
                   "0x0B4004E5 leaf does not name the resource",
                   "remove: a resource the leaf does not name");

    /* A second add on the file the first add produced: the task now holds a
     * real assignment for that resource, so the add is refused. */
    {
        CfbDocument second = doc_from_data(kAddEdit);
        refuses_add(second, key(1, 2), "is already assigned to the task",
                    "add: a task already assigned that resource");
    }
}

void test_path_refusals(TestContext& t) {
    CfbDocument const add = doc_from_data(kAddControl);
    CfbDocument const remove = doc_from_data(kDeleteControl);
    auto refuses_path = [&](CfbDocument const& source,
                            mppwrite::AssignmentKey const& which,
                            bool is_add, std::string const& path,
                            std::string_view needle, std::string const& what) {
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        expect_error(t,
                     [&] {
                         if (is_add) {
                             (void)mppwrite::assign_resource(doc, which, path);
                         } else {
                             (void)mppwrite::unassign_resource(doc, which, path);
                         }
                     },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
    };
    for (bool const is_add : {true, false}) {
        CfbDocument const& source = is_add ? add : remove;
        std::string const label = is_add ? "add" : "remove";
        refuses_path(source, key(1, 2), is_add, "", "the output path is empty",
                     label + ": an empty path");
        refuses_path(source, key(1, 2), is_add, MPPWRITE_TEST_OUT,
                     "is a directory", label + ": a directory path");
        refuses_path(source, key(1, 2), is_add,
                     out_path("no-such-dir/w34-assign.mpp"), "does not exist",
                     label + ": a missing parent directory");
        t.check(!file_exists(out_path("no-such-dir/w34-assign.mpp")),
                label + ": a missing parent directory: an output file");
    }
}

void test_add_damaged(TestContext& t) {
    std::string const missing = missing_path();
    auto refuses = [&](CfbDocument const& source,
                       mppwrite::AssignmentKey const& which,
                       std::string_view needle, std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        expect_error(t,
                     [&] { (void)mppwrite::assign_resource(doc, which, missing); },
                     needle, what);
        check_unchanged(t, doc, before, what);
    };
    mppwrite::AssignmentKey const target = key(1, 2);

    /* The assignment table's shape. */
    refuses(with_edit(kAddControl, kAssnFixed,
                      [](Bytes& b) { b.push_back(0); }),
            target, "not 0 + 110 * rows", "add: a ragged FixedData");
    refuses(with_edit(kAddControl, kAssnMeta,
                      [](Bytes& b) { put_u32_at(b, 4, 0); }),
            target, "rows / rows / flag 4", "add: a FixedMeta flag other than 4");
    refuses(with_edit(kAddControl, kAssnMeta,
                      [](Bytes& b) { put_u32_at(b, 8, 2); }),
            target, "rows / rows / flag 4", "add: a stale FixedMeta count");
    refuses(with_edit(kAddControl, kAssnMeta,
                      [](Bytes& b) { b.push_back(0); }),
            target, "16-byte header plus 34-byte entries",
            "add: a FixedMeta whose size is not a whole number of entries");
    refuses(with_edit(kAddControl, kAssnF2Meta,
                      [](Bytes& b) { put_u32_at(b, 8, 2); }),
            target, "measured rows / rows shape", "add: a stale Fixed2Meta count");
    refuses(with_edit(kAddControl, kAssnF2Meta,
                      [](Bytes& b) { b.push_back(0); }),
            target, "16-byte header plus 53-byte entries",
            "add: a Fixed2Meta with a ragged size");
    refuses(with_edit(kAddControl, kAssnF2,
                      [](Bytes& b) { b.push_back(0); }),
            target, "but its data stream is 145", "add: a ragged Fixed2Data");
    refuses(with_edit(kAddControl, kAssnF2Meta,
                      [](Bytes& b) { put_u32_at(b, 12, 145); }),
            target, "Fixed2Meta: header +12", "add: a Fixed2Meta size word");
    refuses(with_edit(kAddControl, kAssnMeta,
                      [](Bytes& b) { put_u32_at(b, 12, 331); }),
            target, "FixedMeta: header +12", "add: a FixedMeta size word");

    /* The rows. */
    refuses(with_edit(kAddControl, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 110 + 8, 2); }),
            target, "row 1 is not an unassigned placeholder",
            "add: an assigned row");
    refuses(with_edit(kAddControl, kAssnFixed,
                      [](Bytes& b) { put_f64_at(b, 110 + 12, 0.0); }),
            target, "measured 100% units", "add: a row at another units value");
    refuses(with_edit(kAddControl, kAssnFixed,
                      [](Bytes& b) { put_f64_at(b, 110 + 20, 0.0); }),
            target, "no positive finite work", "add: a row with no work");
    refuses(with_edit(kAddControl, kAssnFixed,
                      [](Bytes& b) { put_f64_at(b, 110 + 36, 0.0); }),
            target, "work copies disagree", "add: mismatched work copies");
    refuses(with_edit(kAddControl, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 2 * 110 + 4, 1); }),
            target, "more than one assignment row names",
            "add: two rows naming the task");
    refuses(with_edit(kAddControl, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 4, 9); }),
            target, "no assignment row names UniqueID 1",
            "add: no row naming the task");
    /* W35-0.4 (RC-W1): the first-assignment add writes constants measured at
     * three placeholder rows going to four, so another row count is refused.
     * The FixedData, Fixed2Data and both size words are kept consistent, so
     * only the row-count gate can give this wording. */
    auto with_row_count = [&](bool grow) {
        CfbDocument doc = doc_from_data(kAddControl);
        Bytes fixed = doc.read_stream(kAssnFixed);
        Bytes f2 = doc.read_stream(kAssnF2);
        Bytes fixed_meta = doc.read_stream(kAssnMeta);
        Bytes f2_meta = doc.read_stream(kAssnF2Meta);
        if (grow) {
            Bytes const row(fixed.begin(), fixed.begin() + 110);
            fixed.insert(fixed.end(), row.begin(), row.end());
            Bytes const record(f2.begin(), f2.begin() + 48);
            f2.insert(f2.end(), record.begin(), record.end());
        } else {
            fixed.resize(fixed.size() - 110);
            f2.resize(f2.size() - 48);
        }
        put_u32_at(fixed_meta, 12, static_cast<std::uint32_t>(fixed.size()));
        put_u32_at(f2_meta, 12, static_cast<std::uint32_t>(f2.size()));
        doc.replace_stream(kAssnFixed, fixed);
        doc.replace_stream(kAssnF2, f2);
        doc.replace_stream(kAssnMeta, fixed_meta);
        doc.replace_stream(kAssnF2Meta, f2_meta);
        return doc;
    };
    refuses(with_row_count(true), target,
            "first-assignment add is measured only on a table of 3",
            "add: a fourth placeholder row");
    refuses(with_row_count(false), target,
            "first-assignment add is measured only on a table of 3",
            "add: only two placeholder rows");
    refuses(with_edit(kAddControl, kAssnMeta,
                      [](Bytes& b) { b[16 + 2] = 0x02; }),
            target, "not the measured placeholder entry",
            "add: a placeholder entry with the wrong count");
    refuses(with_edit(kAddControl, kAssnMeta,
                      [](Bytes& b) { b[16 + 13] = 0x00; }),
            target, "not the measured placeholder entry",
            "add: a placeholder entry with the wrong tail");
    refuses(with_edit(kAddControl, kAssnF2Meta,
                      [](Bytes& b) { b[16 + 9] = 0x09; }),
            target, "not the measured placeholder entry",
            "add: a Fixed2Meta entry with the real byte");
    refuses(with_edit(kAddControl, kAssnF2,
                      [](Bytes& b) { b[32] = 0x79; }),
            target, "does not name the placeholder resource",
            "add: a row with another resource identity");

    /* The Task table. */
    refuses(with_edit(kAddControl, kTaskFixed,
                      [](Bytes& b) { put_f64_at(b, 48 + 8, 1.0); }),
            target, "task row 0's work words", "add: project work not at zero");
    refuses(with_edit(kAddControl, kTaskFixed,
                      [](Bytes& b) { put_f64_at(b, 48 + 202 + 8, 1.0); }),
            target, "task row 1's work words", "add: task work not at zero");
    refuses(with_edit(kAddControl, kTaskMeta,
                      [](Bytes& b) { b[16 + 47 * 3 + 16] = 0x06; }),
            target, "measured 0x07", "add: the project task flags");
    refuses(with_edit(kAddControl, kTaskMeta,
                      [](Bytes& b) { b[16 + 47 * 4 + 16] = 0x06; }),
            target, "measured 0x07", "add: the task flags");
    refuses(with_edit(kAddControl, kTaskMeta,
                      [](Bytes& b) { put_u32_at(b, 12, 857); }),
            target, "task FixedMeta: header +12", "add: a task FixedMeta size");
    refuses(with_edit(kAddControl, kTaskMeta,
                      [](Bytes& b) { b.insert(b.end(), 47, 0); }),
            target, "not rows + 3", "add: a task FixedMeta spare entry");
    refuses(with_edit(kAddControl, kTaskF2,
                      [](Bytes& b) { b.push_back(0); }),
            target, "64 * (rows + 3)", "add: a ragged task Fixed2Data");
    {
        CfbDocument const control = doc_from_data(kAddControl);
        Leaf const leaf = task_leaf(control, 1);
        refuses(with_edit(kAddControl, kTaskVar2,
                          [&](Bytes& b) {
                              put_u32_at(b, leaf.offset + 8, 3);
                          }),
                target, "measured [1][placeholder]",
                "add: a leaf that names another resource");
        refuses(with_edit(kAddControl, kTaskVar2,
                          [&](Bytes& b) { put_u32_at(b, leaf.offset, 7); }),
                target, "not the measured 8", "add: a short leaf block");
        refuses(with_edit(kAddControl, kTaskVarMeta,
                          [&](Bytes& b) {
                              put_u32_at(b, 24 + leaf.entry_index * 12 + 8,
                                         0x0B4004E4u);
                          }),
                target, "0x0B4004E5 blocks for UniqueID 1",
                "add: a missing leaf");
        refuses(with_edit(kAddControl, kTaskVarMeta,
                          [](Bytes& b) { put_u32_at(b, 0x14, 400); }),
                target, "the task VarMeta: header +0x14",
                "add: a task VarMeta size word");
    }

    /* The Resource table. */
    refuses(with_edit(kAddControl, kRscFixed,
                      [](Bytes& b) { b.push_back(0); }),
            target, "not 48 + 172 * rows", "add: a ragged resource FixedData");
    refuses(with_edit(kAddControl, kRscFixed,
                      [](Bytes& b) { put_f64_at(b, 48 + 2 * 172 + 48, 1.0); }),
            target, "resource row 2's work words",
            "add: resource work not at zero");
    refuses(with_edit(kAddControl, kRscMeta,
                      [](Bytes& b) { b[16 + 37 * 3 + 13] = 0x00; }),
            target, "measured 0x01", "add: the project resource flag");
    refuses(with_edit(kAddControl, kRscMeta,
                      [](Bytes& b) { put_u32_at(b, 12, 565); }),
            target, "resource FixedMeta: header +12",
            "add: a resource FixedMeta size");
    refuses(with_edit(kAddControl, kRscMeta,
                      [](Bytes& b) { b.insert(b.end(), 37, 0); }),
            target, "not rows + 3", "add: a resource FixedMeta spare entry");
    refuses(with_edit(kAddControl, kRscF2,
                      [](Bytes& b) { b.push_back(0); }),
            target, "40 * (rows + 3)", "add: a ragged resource Fixed2Data");

    /* The assignment VarMeta and Var2Data. */
    refuses(with_edit(kAddControl, kAssnVarMeta,
                      [](Bytes& b) { put_u32_at(b, 0x14, 400); }),
            target, "assignment VarMeta: header +0x14",
            "add: a VarMeta size word");
    refuses(with_edit(kAddControl, kAssnVarMeta,
                      [](Bytes& b) { put_u32_at(b, 24 + 4, 400); }),
            target, "points past Var2Data", "add: an entry past Var2Data");
    /* W34b-1.1 (R34G3-3): an offset within four of 2^32 made offset + 4 wrap to
     * a small number in 32-bit arithmetic, so the old check let get_u32 read
     * far outside Var2Data.  It must be refused as past the end. */
    refuses(with_edit(kAddControl, kAssnVarMeta,
                      [](Bytes& b) { put_u32_at(b, 24 + 4, 0xFFFFFFFDu); }),
            target, "points past Var2Data",
            "add: an entry at offset 0xFFFFFFFD (32-bit wrap)");
    refuses(with_edit(kAddControl, kAssnVarMeta,
                      [](Bytes& b) { put_u32_at(b, 24 + 4, 0xFFFFFFFCu); }),
            target, "points past Var2Data",
            "add: an entry at offset 0xFFFFFFFC (32-bit wrap)");
    {
        CfbDocument const control = doc_from_data(kAddControl);
        std::size_t const first =
            u32_at(control.read_stream(kAssnVarMeta), 24 + 4);
        refuses(with_edit(kAddControl, kAssnVar2,
                          [&](Bytes& b) {
                              put_u32_at(b, first, 0x7FFFFFFFu);
                          }),
                target, "block runs past Var2Data",
                "add: a block longer than the stream");
    }
    refuses(with_edit(kAddControl, kAssnVarMeta,
                      [](Bytes& b) { put_u32_at(b, 24, 7); }),
            target, "follows a greater uid", "add: a foreign uid group");
    refuses(with_edit(kAddControl, kAssnVarMeta,
                      [](Bytes& b) { put_u32_at(b, 24 + 8, 0x0F400032u); }),
            target, "measured key order", "add: a wrong key");
    refuses(with_edit(kAddControl, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 110, 9); }),
            target, "row order", "add: a row whose uid has no group");

    /* 114/Props. */
    refuses(with_edit(kAddControl, kProps,
                      [](Bytes& b) {
                          std::size_t const at = scalar_offset(b, kCountTag);
                          put_u32_at(b, at + 12, 7);
                      }),
            target, "assignment scalars do not agree",
            "add: a count scalar that disagrees");
    refuses(with_edit(kAddControl, kProps,
                      [](Bytes& b) {
                          std::size_t const at = scalar_offset(b, kSizeTag);
                          put_u32_at(b, at + 12, 400);
                      }),
            target, "assignment scalars do not agree",
            "add: a size scalar that disagrees");
    refuses(with_edit(kAddControl, kProps,
                      [](Bytes& b) {
                          std::size_t const at = scalar_offset(b, kDirtyTag);
                          put_u32_at(b, at + 12, 1);
                      }),
            target, "assignment scalars do not agree",
            "add: a document already dirty");
    refuses(with_edit(kAddControl, kProps,
                      [](Bytes& b) {
                          std::size_t const at = record_offset(b, kSessionRecord);
                          put_u32_at(b, at + 4, 0x02400008u);
                      }),
            target, "no 0x02400007 record", "add: a missing session record");
    refuses(with_edit(kAddControl, kProps,
                      [](Bytes& b) {
                          std::size_t const at = record_offset(b, kSessionRecord);
                          put_u32_at(b, at, 3);
                      }),
            target, "four-byte session word", "add: a short session record");
}

/* The settled append shape's own guards, reached through the second control:
 * its task already holds one real assignment (row 3) and one free placeholder
 * row (row 0), so every branch unique to the count 1 -> 2 path runs. */
void test_settled_damaged(TestContext& t) {
    std::string const missing = missing_path();
    auto refuses = [&](CfbDocument const& source,
                       mppwrite::AssignmentKey const& which,
                       std::string_view needle, std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        expect_error(t,
                     [&] { (void)mppwrite::assign_resource(doc, which, missing); },
                     needle, what);
        check_unchanged(t, doc, before, what);
    };
    char const* control = "wave34-assign-add-second-control.mpp";
    /* The control already holds resource 2's assignment; resource 1 is free. */
    mppwrite::AssignmentKey const target = key(1, 1);

    refuses(with_edit(control, kAssnFixed, [](Bytes& b) { b.push_back(0); }),
            target, "not 0 + 110 * rows", "settled add: a ragged FixedData");
    refuses(with_edit(control, kAssnMeta,
                      [](Bytes& b) { put_u32_at(b, 8, 2); }),
            target, "measured rows / rows shape",
            "settled add: a stale FixedMeta count");
    refuses(with_edit(control, kAssnMeta, [](Bytes& b) { b[16] = 0x00; }),
            target, "measured entry for its row kind",
            "settled add: a target placeholder entry at another state");
    refuses(with_edit(control, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 110 + 8, 5); }),
            target, "real rows only for the target task",
            "settled add: a foreign task's real row");
    refuses(with_edit(control, kAssnFixed,
                      [](Bytes& b) { put_f64_at(b, 3 * 110 + 20, 0.0); }),
            target, "no positive finite work",
            "settled add: a real row with no work");
    /* R34G3-4: the placeholder-count guards.  Row 1 is another task's free
     * placeholder; re-homing it onto task 1 gives the task two, and re-homing
     * the task's own placeholder (row 0) onto task 2 leaves it none. */
    refuses(with_edit(control, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 110 + 4, 1); }),
            target, "the task has more than one free placeholder row",
            "settled add: two free placeholder rows");
    refuses(with_edit(control, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 0 * 110 + 4, 2); }),
            target, "the task has no free placeholder row",
            "settled add: no free placeholder row");
    /* The count 3 -> 4 step: the third pair's output already holds three real
     * assignments on task 1, and its resource 3 is one of them. */
    {
        char const* third = "wave34-assign-add-third.mpp";
        refuses(doc_from_data(third), key(1, 3),
                "the task already has 3 real assignments; the measured counts "
                "are 1 and 2",
                "settled add: a fourth assignment on a task with three");
        refuses(doc_from_data(third), key(1, 1),
                "the task already has 3 real assignments; the measured counts "
                "are 1 and 2",
                "settled add: a fourth assignment, another resource");
    }
    /* W35-0.4 (RC-W2): only a lower uid on one assigned resource
     * (wave34-assign-add-second: 1 into [2]) and a higher uid on two
     * (wave34-assign-add-third: 3 into [1, 2]) were saved by Project; the
     * other orders are refused before the leaf array is read.  The control's
     * task holds resource 2, so resource 3 would be a higher uid on one. */
    refuses(doc_from_data(control), key(1, 3),
            "in an order no pair measures",
            "settled add: a higher uid on one assigned resource");
    {
        /* The third control's task holds resources 1 and 2.  Re-pointing the
         * resource-2 row at resource 3 leaves [1, 3], so adding 2 would go in
         * between. */
        char const* third_control = "wave34-assign-add-third-control.mpp";
        refuses(with_edit(third_control, kAssnFixed,
                          [](Bytes& b) { put_u32_at(b, 3 * 110 + 8, 3); }),
                key(1, 2), "in an order no pair measures",
                "settled add: a uid between two assigned resources");
        /* Re-pointing the resource-1 row at resource 3 leaves [2, 3], so
         * adding 1 would go below both. */
        refuses(with_edit(third_control, kAssnFixed,
                          [](Bytes& b) { put_u32_at(b, 4 * 110 + 8, 3); }),
                key(1, 1), "in an order no pair measures",
                "settled add: a uid below two assigned resources");
    }
    /* W35b-2.11 (R35G0-4): the new row copies an existing real row, so the
     * resources already assigned are checked like the new one (resource 2, row 2
     * of the resource tables, calendar uid 4), and the copied row's date words
     * must be the task's own.  Each case damages the ALREADY ASSIGNED resource or
     * row only; resource 1, the one being added, stays the measured shape. */
    {
        std::size_t const rsc2 = 48 + 172 * 2;
        refuses(with_edit(control, kRscFixed,
                          [&](Bytes& b) { put_f64_at(b, rsc2 + 16, 25.0); }),
                target, "the resource's standard rate is not the measured one",
                "settled add: an assigned resource with a standard rate");
        refuses(with_edit(control, kRscMeta,
                          [](Bytes& b) { b[16 + 37 * (2 + 3) + 13] = 0x00; }),
                target, "the resource's FixedMeta entry differs from the measured entry",
                "settled add: an assigned resource with another FixedMeta flag");
        refuses(with_edit(control, kCalFixed,
                          [](Bytes& b) {
                              for (std::size_t row = 0; (64 + 12 * (row + 1)) <= b.size();
                                   ++row) {
                                  if (u32_at(b, 64 + 12 * row + 4) == 2) {
                                      put_u32_at(b, 64 + 12 * row, 3);
                                  }
                              }
                          }),
                target, "is based on calendar 3",
                "settled add: an assigned resource on another base calendar");
        {
            CfbDocument exception = doc_from_data(control);
            add_cal_block(exception, 4, 0x0D400008u, 540);
            refuses(exception, target, "the resource's calendar stores a block",
                    "settled add: an assigned resource whose calendar has an exception");
        }
        /* Row 3 is the real row.  Its +56 is the finish word and +52 a start word. */
        refuses(with_edit(control, kAssnFixed,
                          [](Bytes& b) { put_u32_at(b, 3 * 110 + 56, 0x3bf51c20u); }),
                target, "is not the task's own Start/Finish",
                "settled add: a real row whose finish is not the task's");
        refuses(with_edit(control, kAssnFixed,
                          [](Bytes& b) { put_u32_at(b, 3 * 110 + 104, 0x3bf21e78u); }),
                target, "is not the task's own Start/Finish",
                "settled add: a real row whose start is not the task's");
    }
    refuses(with_edit(control, kTaskFixed,
                      [](Bytes& b) { put_f64_at(b, 48 + 202 + 8, 1.0); }),
            target, "task row 1's work words", "settled add: the task work");
    refuses(with_edit(control, kTaskMeta,
                      [](Bytes& b) { b[16 + 47 * 4 + 16] = 0x07; }),
            target, "0x17 / 0x97", "settled add: the task flags");
    refuses(with_edit(control, kRscFixed,
                      [](Bytes& b) { put_f64_at(b, 48 + 172 + 48, 1.0); }),
            target, "resource row 1's work words",
            "settled add: the resource work");
    {
        CfbDocument const source = doc_from_data(control);
        Leaf const leaf = task_leaf(source, 1);
        refuses(with_edit(control, kTaskVar2,
                          [&](Bytes& b) { put_u32_at(b, leaf.offset + 4, 2); }),
                target, "1-resource array",
                "settled add: the task leaf's resource count");
    }
    refuses(with_edit(control, kTaskVarMeta,
                      [](Bytes& b) { put_u32_at(b, 0x14, 400); }),
            target, "header +0x14 is 400",
            "settled add: a Task VarMeta size word");
    refuses(with_edit(control, kProps,
                      [](Bytes& b) {
                          std::size_t const at = scalar_offset(b, kCountTag);
                          put_u32_at(b, at + 12, 7);
                      }),
            target, "assignment scalars do not agree",
            "settled add: a count scalar that disagrees");
    refuses(with_edit(control, kProps,
                      [](Bytes& b) {
                          std::size_t const at = scalar_offset(b, kTaskSizeTag);
                          put_u32_at(b, at + 12, 400);
                      }),
            target, "assignment scalars do not agree",
            "settled add: a Task size scalar that disagrees");
}

void test_remove_damaged(TestContext& t) {
    std::string const missing = missing_path();
    auto refuses = [&](CfbDocument const& source,
                       mppwrite::AssignmentKey const& which,
                       std::string_view needle, std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        expect_error(
            t, [&] { (void)mppwrite::unassign_resource(doc, which, missing); },
            needle, what);
        check_unchanged(t, doc, before, what);
    };
    mppwrite::AssignmentKey const target = key(1, 2);

    refuses(with_edit(kDeleteControl, kAssnFixed,
                      [](Bytes& b) { b.push_back(0); }),
            target, "not 0 + 110 * rows", "remove: a ragged FixedData");
    refuses(with_edit(kDeleteControl, kAssnMeta,
                      [](Bytes& b) { put_u32_at(b, 8, 2); }),
            target, "settled shape", "remove: a stale FixedMeta count");
    refuses(with_edit(kDeleteControl, kAssnMeta,
                      [](Bytes& b) { put_u32_at(b, 4, 0); }),
            target, "settled shape", "remove: a FixedMeta flag other than 4");
    refuses(with_edit(kDeleteControl, kAssnMeta,
                      [](Bytes& b) { b.push_back(0); }),
            target, "16-byte header plus 34-byte entries",
            "remove: a FixedMeta whose size is not a whole number of entries");
    refuses(with_edit(kDeleteControl, kAssnF2Meta,
                      [](Bytes& b) { put_u32_at(b, 8, 2); }),
            target, "(rows - 1) / rows settled shape",
            "remove: a stale Fixed2Meta count");
    refuses(with_edit(kDeleteControl, kAssnMeta,
                      [](Bytes& b) { put_u32_at(b, 12, 551); }),
            target, "FixedMeta: header +12", "remove: a FixedMeta size word");
    refuses(with_edit(kDeleteControl, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 8, 2); }),
            target, "row 0 is not an unassigned placeholder",
            "remove: an assigned placeholder row");
    refuses(with_edit(kDeleteControl, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 3 * 110, 1); }),
            target, "the real assignment is not the greatest uid",
            "remove: a real row that is not the greatest uid");
    refuses(with_edit(kDeleteControl, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 3 * 110 + 4, 2); }),
            target, "is not the (task, resource) the caller named",
            "remove: a real row with another task");
    refuses(with_edit(kDeleteControl, kAssnFixed,
                      [](Bytes& b) { put_u32_at(b, 3 * 110 + 8, 3); }),
            target, "is not the (task, resource) the caller named",
            "remove: a real row with another resource");
    refuses(with_edit(kDeleteControl, kAssnFixed,
                      [](Bytes& b) { put_f64_at(b, 3 * 110 + 20, 0.0); }),
            target, "no positive finite work", "remove: a real row with no work");
    refuses(with_edit(kDeleteControl, kAssnMeta,
                      [](Bytes& b) { b[16 + 2] = 0x02; }),
            target, "not the measured settled entry",
            "remove: a placeholder entry with the wrong count");
    refuses(with_edit(kDeleteControl, kAssnMeta,
                      [](Bytes& b) { b[16 + 3 * kFmEntry + 20] = 0x01; }),
            target, "spare entry is not a byte duplicate",
            "remove: a FixedMeta spare that is not a copy");
    refuses(with_edit(kDeleteControl, kAssnF2Meta,
                      [](Bytes& b) { b[16 + 9] = 0x09; }),
            target, "not the measured settled entry",
            "remove: a Fixed2Meta entry with the real byte");
    refuses(with_edit(kDeleteControl, kAssnF2Meta,
                      [](Bytes& b) { b[16 + 3 * kF2mEntry + 20] = 0x01; }),
            target, "Fixed2Meta spare entry is not a byte duplicate",
            "remove: a Fixed2Meta spare that is not a copy");
    refuses(with_edit(kDeleteControl, kAssnF2,
                      [](Bytes& b) { b[32] = 0x79; }),
            target, "does not name the placeholder resource",
            "remove: a placeholder with another resource identity");
    refuses(with_edit(kDeleteControl, kAssnF2,
                      [](Bytes& b) {
                          std::copy(null_identity().begin(),
                                    null_identity().end(),
                                    b.begin() + 3 * kF2Record + 32);
                      }),
            target, "names the placeholder resource",
            "remove: a real row that names the placeholder");

    refuses(with_edit(kDeleteControl, kTaskFixed,
                      [](Bytes& b) { put_f64_at(b, 48 + 8, 1.0); }),
            target, "task row 0's work words", "remove: project work");
    refuses(with_edit(kDeleteControl, kTaskFixed,
                      [](Bytes& b) { put_f64_at(b, 48 + 202 + 8, 1.0); }),
            target, "task row 1's work words", "remove: task work");
    refuses(with_edit(kDeleteControl, kTaskMeta,
                      [](Bytes& b) { b[16 + 47 * 3 + 16] = 0x07; }),
            target, "0x17 / 0x97", "remove: the project task flags");
    refuses(with_edit(kDeleteControl, kTaskMeta,
                      [](Bytes& b) { b[16 + 47 * 4 + 16] = 0x07; }),
            target, "0x17 / 0x97", "remove: the task flags");
    refuses(with_edit(kDeleteControl, kRscFixed,
                      [](Bytes& b) { put_f64_at(b, 48 + 2 * 172 + 48, 0.0); }),
            target, "resource row 2's work words", "remove: resource work");
    refuses(with_edit(kDeleteControl, kRscMeta,
                      [](Bytes& b) { b[16 + 37 * 3 + 13] = 0x01; }),
            target, "measured 0x00", "remove: the project resource flag");
    {
        CfbDocument const control = doc_from_data(kDeleteControl);
        Leaf const leaf = task_leaf(control, 1);
        refuses(with_edit(kDeleteControl, kTaskVar2,
                          [&](Bytes& b) {
                              put_u32_at(b, leaf.offset + 8, 3);
                          }),
                target, "does not name the resource",
                "remove: a leaf that names another resource");
    }

    refuses(with_edit(kDeleteControl, kAssnVarMeta,
                      [](Bytes& b) { put_u32_at(b, 0x14, 648); }),
            target, "assignment VarMeta: header +0x14",
            "remove: a VarMeta size word");
    refuses(with_edit(kDeleteControl, kAssnVarMeta,
                      [](Bytes& b) {
                          for (std::size_t i = 6; i < 10; ++i) {
                              put_u32_at(b, 24 + i * 12, 6);
                          }
                      }),
            target, "last VarMeta group is not the real assignment's",
            "remove: a re-keyed group with another uid");
    refuses(with_edit(kDeleteControl, kAssnVarMeta,
                      [](Bytes& b) { put_u32_at(b, 24 + 6 * 12 + 4, 396); }),
            target, "block is not at the measured offset",
            "remove: a group block at another offset");
    refuses(with_edit(kDeleteControl, kAssnVar2,
                      [](Bytes& b) { put_u32_at(b, 396, 5); }),
            target, "block is not the measured length",
            "remove: a group block of another length");
    refuses(with_edit(kDeleteControl, kAssnVarMeta,
                      [](Bytes& b) { put_u32_at(b, 24 + 4, 640); }),
            target, "points past Var2Data", "remove: an entry past Var2Data");
    refuses(with_edit(kDeleteControl, kAssnVarMeta,
                      [](Bytes& b) { put_u32_at(b, 24 + 4, 0xFFFFFFFDu); }),
            target, "points past Var2Data",
            "remove: an entry at offset 0xFFFFFFFD (32-bit wrap)");

    refuses(with_edit(kDeleteControl, kProps,
                      [](Bytes& b) {
                          std::size_t const at = scalar_offset(b, kCountTag);
                          put_u32_at(b, at + 12, 7);
                      }),
            target, "assignment scalars do not agree",
            "remove: a count scalar that disagrees");
    refuses(with_edit(kDeleteControl, kProps,
                      [](Bytes& b) {
                          std::size_t const at = scalar_offset(b, kSizeTag);
                          put_u32_at(b, at + 12, 524);
                      }),
            target, "assignment scalars do not agree",
            "remove: a size scalar that disagrees");
    refuses(with_edit(kDeleteControl, kProps,
                      [](Bytes& b) {
                          std::size_t const at = scalar_offset(b, kDirtyTag);
                          put_u32_at(b, at + 12, 1);
                      }),
            target, "assignment scalars do not agree",
            "remove: a document already dirty");
}

/* W34-4.3: dry_run validates and reports without writing.  The report equals
 * the real write's, the document is untouched, the (empty) path is never
 * examined, and every refusal the real call raises is raised here too. */
void test_dry_run(TestContext& t) {
    struct Case {
        char const* control;
        char const* label;
        bool add;
    };
    for (Case const& c : {Case{kAddControl, "add", true},
                          Case{kDeleteControl, "remove", false}}) {
        std::string const what = std::string("dry run ") + c.label;
        CfbDocument dry = doc_from_data(c.control);
        Bytes const before = dry.to_bytes();
        mppwrite::AssignmentGrowthWrite const planned =
            c.add ? mppwrite::assign_resource(dry, key(1, 2), "", true)
                  : mppwrite::unassign_resource(dry, key(1, 2), "", true);
        t.check(dry.to_bytes() == before, what + ": the document changed");

        CfbDocument real = doc_from_data(c.control);
        std::string const path =
            out_path(std::string("w34-dry-") + c.label + ".mpp");
        mppwrite::AssignmentGrowthWrite const written =
            c.add ? mppwrite::assign_resource(real, key(1, 2), path)
                  : mppwrite::unassign_resource(real, key(1, 2), path);
        t.equal(planned.assignment_uid, written.assignment_uid,
                what + ": the report's assignment uid");
        t.equal(planned.tombstoned_uid, written.tombstoned_uid,
                what + ": the report's tombstoned uid");
        t.equal(planned.rows_before, written.rows_before, what + ": rows before");
        t.equal(planned.rows_after, written.rows_after, what + ": rows after");
        t.check(planned.work == written.work, what + ": the report's work");
        t.equal(planned.props_size_after, written.props_size_after,
                what + ": the report's Props size");
    }

    /* A refusal is raised by the dry run too, and nothing is touched. */
    {
        CfbDocument doc = doc_from_data(kDeleteControl);
        Bytes const before = doc.to_bytes();
        expect_error(
            t, [&] { (void)mppwrite::assign_resource(doc, key(1, 2), "", true); },
            "the assignment FixedMeta is not the measured rows / rows shape",
            "dry run add: the settled removal shape is refused");
        t.check(doc.to_bytes() == before,
                "dry run add refusal: the document changed");
    }
    {
        CfbDocument doc = doc_from_data(kAddControl);
        Bytes const before = doc.to_bytes();
        expect_error(
            t,
            [&] { (void)mppwrite::unassign_resource(doc, key(1, 2), "", true); },
            "0x0B4004E5 leaf does not name the resource",
            "dry run remove: the unassigned shape is refused");
        t.check(doc.to_bytes() == before,
                "dry run remove refusal: the document changed");
    }
}

/* ---- W34b-0.1: the unmeasured target shapes -------------------------------
 * Every pair assigns a Work resource to a MANUAL, top-level, 0 % leaf of 24000
 * ticks on the project calendar, 40 hours per assignment.  Each case below
 * changes one fact of the task or the resource (or the work amount) in the add
 * control, the removal control and, for the append shape, the second control,
 * and the writer must refuse with a needle that names that gate. */
void test_measured_shape_refusals(TestContext& t) {
    std::string const missing = missing_path();
    char const* const kTaskF2Meta = "114/TBkndTask/Fixed2Meta";
    char const* const kRscVarMeta = "114/TBkndRsc/VarMeta";
    mppwrite::AssignmentKey const target = key(1, 2);
    constexpr std::size_t kTask1Row = 48 + 202;   /* task FixedData row 1 */
    constexpr std::size_t kRsc2Row = 48 + 2 * 172; /* resource FixedData row 2 */

    struct Case {
        std::string label;
        std::string stream;
        std::function<void(Bytes&)> mutate;
        std::string needle;
        /* When set, edits the whole document instead of one stream. */
        std::function<void(CfbDocument&)> edit_doc = nullptr;
    };
    std::vector<Case> cases;
    cases.push_back({"an automatically scheduled task", kTaskF2Meta,
                     [](Bytes& b) { b[16 + 96 * 4 + 8] = 0x7F; },
                     "is not manually scheduled"});
    cases.push_back({"a task of another duration", kTaskFixed,
                     [&](Bytes& b) { put_u32_at(b, kTask1Row + 84, 14400); },
                     "duration is not the measured 24000 ticks"});
    cases.push_back({"a task with its own calendar", kTaskFixed,
                     [&](Bytes& b) { put_u32_at(b, kTask1Row + 178, 5); },
                     "differs from the measured task row at byte 178"});
    cases.push_back({"a constrained task", kTaskFixed,
                     [&](Bytes& b) { put_u16_at(b, kTask1Row + 64, 2); },
                     "differs from the measured task row at byte 64"});
    cases.push_back({"a task with another stamp word at +128", kTaskFixed,
                     [&](Bytes& b) { put_u32_at(b, kTask1Row + 128, 0x3d182d28u); },
                     "the task's stamp word at +128 differs from the measured 0x3d141482"});
    cases.push_back({"a task of another type or effort setting", kTaskMeta,
                     [](Bytes& b) { b[16 + 47 * 4 + 17] = 0x44; },
                     "FixedMeta entry differs from the measured entry at byte 17"});
    cases.push_back({"a task whose Fixed2Data differs", kTaskF2,
                     [](Bytes& b) { b[64 * 4 + 40] = 0x02; },
                     "Fixed2Data record differs from the measured record at byte 40"});
    cases.push_back({"a task with an extra stored block", kTaskVarMeta,
                     [](Bytes& b) {
                         std::size_t const entries = (b.size() - 24) / 12;
                         for (std::size_t i = 0; i < entries; ++i) {
                             if (u32_at(b, 24 + 12 * i) == 1 &&
                                 u32_at(b, 24 + 12 * i + 8) == 0x0B4000A4u) {
                                 put_u32_at(b, 24 + 12 * i + 8, 0x0B400080u);
                             }
                         }
                     },
                     "VarMeta blocks are not the measured eleven keys"});
    cases.push_back({"a resource with a standard rate", kRscFixed,
                     [&](Bytes& b) { put_f64_at(b, kRsc2Row + 16, 5000.0); },
                     "standard rate is not the measured one"});
    cases.push_back({"a resource with an overtime rate", kRscFixed,
                     [&](Bytes& b) { put_f64_at(b, kRsc2Row + 24, 7500.0); },
                     "overtime rate is not the measured one"});
    cases.push_back({"a resource with a cost per use", kRscFixed,
                     [&](Bytes& b) { put_f64_at(b, kRsc2Row + 72, 2500.0); },
                     "cost per use is not the measured one"});
    cases.push_back({"a resource with another accrual", kRscFixed,
                     [&](Bytes& b) { put_u32_at(b, kRsc2Row + 80, 1); },
                     "accrual is not the measured one"});
    cases.push_back({"a resource on the standard base calendar", kRscFixed,
                     [&](Bytes& b) { put_u32_at(b, kRsc2Row + 154, 1); },
                     "not on its own base calendar"});
    cases.push_back({"a resource with a stored group", kRscVarMeta,
                     [](Bytes& b) {
                         std::size_t const entries = (b.size() - 24) / 12;
                         for (std::size_t i = 0; i < entries; ++i) {
                             if (u32_at(b, 24 + 12 * i) == 2 &&
                                 u32_at(b, 24 + 12 * i + 8) == 0x0C4002E3u) {
                                 put_u32_at(b, 24 + 12 * i + 8, 0x0C400003u);
                             }
                         }
                     },
                     "resource's VarMeta blocks are not the measured five keys"});

    /* W35-0.1 (R34b-1): the resource's calendar is read, not just named.  Every
     * case keeps word 154 = uid + 2, so the identity gate passes and only the
     * calendar's contents differ.  Resource 2's calendar is uid 4 (row 3). */
    auto const cal_edit = [](std::uint32_t resource,
                             std::function<void(Bytes&, std::size_t)> mutate) {
        return [resource, mutate](Bytes& b) {
            for (std::size_t row = 0; (64 + 12 * (row + 1)) <= b.size(); ++row) {
                if (u32_at(b, 64 + 12 * row + 4) == resource) {
                    mutate(b, 64 + 12 * row);
                }
            }
        };
    };
    cases.push_back({"a resource calendar based on another calendar", kCalFixed,
                     cal_edit(2, [](Bytes& b, std::size_t at) { put_u32_at(b, at, 3); }),
                     "is based on calendar 3"});
    cases.push_back({"a resource with no calendar row of its own", kCalFixed,
                     cal_edit(2, [](Bytes& b, std::size_t at) { put_u32_at(b, at + 4, 9); }),
                     "has 0 rows for the resource's calendar"});
    cases.push_back({"a resource calendar row that names another calendar uid", kCalFixed,
                     cal_edit(2, [](Bytes& b, std::size_t at) { put_u32_at(b, at + 8, 7); }),
                     "not the calendar word 154 names"});
    cases.push_back({"a Standard calendar row that is not a base calendar", kCalFixed,
                     [](Bytes& b) { put_u32_at(b, 64, 5); },
                     "calendar 1 is not the measured Standard base calendar"});
    cases.push_back({"a resource calendar with other FixedMeta flags", kCalMeta,
                     [](Bytes& b) { b[16 + 10 * (3 + 4) + 8] = 0x8F; },
                     "resource calendar's FixedMeta entry is not the measured"});
    cases.push_back({"a Standard calendar with other FixedMeta flags", kCalMeta,
                     [](Bytes& b) { b[16 + 10 * 4 + 8] = 0x0E; },
                     "Standard calendar's FixedMeta entry is not the measured"});
    cases.push_back({"a resource calendar with a stored name", kCalVarMeta,
                     [](Bytes&) {}, "the resource's calendar stores a block",
                     [](CfbDocument& d) { add_cal_block(d, 4, 0x0D400001u, 20); }});
    cases.push_back({"a resource calendar with an exception", kCalVarMeta,
                     [](Bytes&) {}, "the resource's calendar stores a block",
                     [](CfbDocument& d) { add_cal_block(d, 4, 0x0D400008u, 540); }});
    cases.push_back({"a Standard calendar with an exception", kCalVarMeta,
                     [](Bytes&) {},
                     "Standard base calendar stores a block beyond its name",
                     [](CfbDocument& d) { add_cal_block(d, 1, 0x0D400008u, 540); }});
    cases.push_back({"another default project calendar", kProps,
                     [](Bytes& b) {
                         std::size_t const at = record_offset(b, 0x0240000Eu);
                         b[at + 12] = 'N';
                     },
                     "default calendar is not the Standard base calendar"});
    cases.push_back({"another default working week", kProps,
                     [](Bytes& b) {
                         std::size_t const at = record_offset(b, 0x02401388u);
                         b[at + 12 + 60 + 4] ^= 0x01;
                     },
                     "default working week differs"});

    auto run = [&](char const* control, bool add, Case const& c) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument source = c.edit_doc ? doc_from_data(control)
                                        : with_edit(control, c.stream, c.mutate);
        if (c.edit_doc) {
            c.edit_doc(source);
        }
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        std::string const what =
            std::string(add ? "add: " : "remove: ") + c.label + " (" + control + ")";
        if (add) {
            expect_error(t,
                         [&] { (void)mppwrite::assign_resource(doc, target, missing); },
                         c.needle, what);
        } else {
            expect_error(
                t, [&] { (void)mppwrite::unassign_resource(doc, target, missing); },
                c.needle, what);
        }
        check_unchanged(t, doc, before, what);
    };
    for (Case const& c : cases) {
        run(kAddControl, true, c);
        run(kDeleteControl, false, c);
    }

    /* The work amount: every pair adds 40 hours (2400000.0 per assignment). */
    {
        auto set_row_work = [](Bytes& b, std::size_t row) {
            put_f64_at(b, row * 110 + 20, 1800000.0);
            put_f64_at(b, row * 110 + 36, 1800000.0);
            put_f64_at(b, row * 110 + 44, 1800000.0);
        };
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument add_doc =
            with_edit(kAddControl, kAssnFixed, [&](Bytes& b) { set_row_work(b, 0); });
        Bytes const add_before = add_doc.to_bytes();
        expect_error(t,
                     [&] { (void)mppwrite::assign_resource(add_doc, target, missing); },
                     "work is not the measured 40 hours",
                     "add: a 30-hour assignment on the placeholder row");
        check_unchanged(t, add_doc, add_before, "add: a 30-hour assignment");

        CfbDocument remove_doc = with_edit(
            kDeleteControl, kAssnFixed, [&](Bytes& b) { set_row_work(b, (b.size() / 110) - 1); });
        Bytes const remove_before = remove_doc.to_bytes();
        expect_error(
            t, [&] { (void)mppwrite::unassign_resource(remove_doc, target, missing); },
            "work is not the measured 40 hours",
            "remove: a 30-hour real assignment");
        check_unchanged(t, remove_doc, remove_before, "remove: a 30-hour assignment");

        CfbDocument second_doc = with_edit(
            "wave34-assign-add-second-control.mpp", kAssnFixed, [&](Bytes& b) {
                for (std::size_t r = 0; r < b.size() / 110; ++r) {
                    if (u32_at(b, r * 110 + 4) == 1 && u32_at(b, r * 110 + 8) != 0xFFFF0001u) {
                        set_row_work(b, r);
                    }
                }
            });
        Bytes const second_before = second_doc.to_bytes();
        expect_error(
            t,
            [&] { (void)mppwrite::assign_resource(second_doc, key(1, 1), missing); },
            "work is not the measured 40 hours",
            "add (append shape): a 30-hour real assignment");
        check_unchanged(t, second_doc, second_before, "add (append shape): 30 hours");
    }

    /* The append shape (a task that already holds an assignment) reaches the
     * same calendar gates through its own call sites (W35-0.1). */
    {
        char const* const second = "wave34-assign-add-second-control.mpp";
        struct AppendCase {
            char const* label;
            char const* stream;
            std::function<void(Bytes&)> mutate;
            char const* needle;
        };
        std::vector<AppendCase> const append_cases = {
            {"a resource calendar based on another calendar", kCalFixed,
             [](Bytes& b) {
                 for (std::size_t row = 0; 64 + 12 * (row + 1) <= b.size(); ++row) {
                     if (u32_at(b, 64 + 12 * row + 4) == 1) {
                         put_u32_at(b, 64 + 12 * row, 4);
                     }
                 }
             },
             "is based on calendar 4"},
            {"another default project calendar", kProps,
             [](Bytes& b) { b[record_offset(b, 0x0240000Eu) + 12] = 'N'; },
             "default calendar is not the Standard base calendar"},
        };
        for (AppendCase const& c : append_cases) {
            std::error_code ignored;
            std::filesystem::remove(missing, ignored);
            CfbDocument doc = with_edit(second, c.stream, c.mutate);
            Bytes const before = doc.to_bytes();
            std::string const what = std::string("add (append shape): ") + c.label;
            expect_error(
                t, [&] { (void)mppwrite::assign_resource(doc, key(1, 1), missing); },
                c.needle, what);
            check_unchanged(t, doc, before, what);
        }
    }
}

/* ---- W35-3.5: removing an assignment from a file the port itself saved --- */

char const* kPortControl = "wave35-g3-assign-remove-port-saved-control.mpp";
char const* kPortNull = "wave35-g3-assign-remove-port-saved-control-null.mpp";
char const* kPortEdit = "wave35-g3-assign-remove-port-saved.mpp";

/* The pair: the port's own Assign Resources output on the wave-30 add control
 * (control), Project reopening and resaving it (null save), and Project
 * deleting task 1's assignment to resource 2 from it (edit).  Measured in
 * W35-3.5; the removal stays REFUSED (spec/writers/table-growth.md, "Removing an
 * assignment from a port-saved file"). */
void test_remove_port_saved(TestContext& t) {
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kPortControl));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kPortNull));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kPortEdit));

    /* 1. The port's own file is refused, unchanged: Assn FixedMeta is the
     * append shape (count == rows, a leading tombstoned entry), not the
     * settled (rows - 1) / rows / flag 4 shape that Project's resave writes. */
    CfbDocument const control = doc_from_data(kPortControl);
    {
        CfbDocument doc = control;
        Bytes const before = doc.to_bytes();
        std::error_code ignored;
        std::filesystem::remove(missing_path(), ignored);
        expect_error(
            t, [&] { (void)mppwrite::unassign_resource(doc, key(1, 2), missing_path()); },
            "FixedMeta is not the measured (rows - 1) / rows / flag 4 settled shape",
            "remove on the port-saved file");
        check_unchanged(t, doc, before, "remove on the port-saved file");
    }

    /* 2. W35b-1.6 (R35G3-1, RW35-W1): the file Project resaved is refused too.
     * Project's null save of the port's file settles Assn FixedMeta and
     * Fixed2Meta, so the settled-shape gate passes; what it also sets is bit 0x40
     * on the target resource's FixedMeta entry (byte 12, 0x1C to 0x5C), and
     * Project's removal from that file clears Rsc Fixed2Meta byte 279 (0x2B to
     * 0x0B), which the port never writes.  The writer refuses the file on the
     * byte, so it does not write what Project would not.
     *
     * RW35-W12: Project's save of the port's file also carries a Props14 edit
     * marker, and this test does not vouch for it.  Nothing here needs to: no
     * removal is written from that file (every case below ends in the refusal,
     * with the document unchanged and no output file), so there is no port
     * output whose Props14 could be compared.  A later change that makes the
     * removal succeed on a resaved file must add the Props14 comparison to the
     * pair that proves it. */
    CfbDocument const null_save = doc_from_data(kPortNull);
    char const* const resaved_needle =
        "the target resource's FixedMeta byte 12 is 0x5C, not the measured 0x1C";
    {
        CfbDocument doc = null_save;
        Bytes const before = doc.to_bytes();
        std::error_code ignored;
        std::filesystem::remove(missing_path(), ignored);
        expect_error(
            t, [&] { (void)mppwrite::unassign_resource(doc, key(1, 2), missing_path()); },
            resaved_needle, "remove on the file Project resaved (the null save)");
        check_unchanged(t, doc, before, "remove on the file Project resaved");
        t.check(!file_exists(missing_path()), "the refused removal writes no file");
    }
    {
        CfbDocument doc = control;
        for (char const* path : {kAssnMeta, kAssnF2Meta, kRscMeta, kProps}) {
            doc.replace_stream(path, null_save.read_stream(path));
        }
        Bytes const before = doc.to_bytes();
        expect_error(
            t, [&] { (void)mppwrite::unassign_resource(doc, key(1, 2), missing_path()); },
            resaved_needle, "remove on the port's file with Project's resaved Rsc FixedMeta");
        check_unchanged(t, doc, before, "remove with Project's resaved Rsc FixedMeta");
    }
    {
        /* W35c-1.9 (R35bG1-2): the guard read only the target row.  Project's resave sets
         * 0x40 on every resource row, and the port's own resource add stamps 0x1C even into a
         * file Project has saved, so a file whose TARGET row (resource 2, stream byte 213) is
         * 0x1C but another row is 0x5C is still a file a session has saved.  Only the other
         * row (resource 1, byte 176) is changed here. */
        CfbDocument doc = control;
        for (char const* path : {kAssnMeta, kAssnF2Meta, kProps}) {
            doc.replace_stream(path, null_save.read_stream(path)); /* the settled shape */
        }
        Bytes meta = doc.read_stream(kRscMeta);
        t.equal(meta[176], 0x1C, "the control's resource 1 session byte is 0x1C");
        t.equal(meta[213], 0x1C, "the control's resource 2 (the target) session byte is 0x1C");
        meta[176] = 0x5C;
        doc.replace_stream(kRscMeta, meta);
        Bytes const before = doc.to_bytes();
        std::error_code ignored;
        std::filesystem::remove(missing_path(), ignored);
        expect_error(
            t, [&] { (void)mppwrite::unassign_resource(doc, key(1, 2), missing_path()); },
            "the resource table's row 1 FixedMeta byte 12 is 0x5C, not the measured 0x1C",
            "remove with only a non-target resource row marked as Project-saved");
        check_unchanged(t, doc, before, "remove with only a non-target row marked");
        t.check(!file_exists(missing_path()), "the refused removal writes no file");
    }

    /* 3. The settle step is not the only gap.  Give the control the streams
     * Project's null save rewrites except the resource flag byte (Assn FixedMeta
     * and Fixed2Meta, Props): the writer then accepts the file, and its output
     * still differs from Project's edit save in four streams, each a named gap.
     * The four are a LOWER BOUND: the mask compare_all builds is the control
     * against the null save, and the null save rewrote 19 streams (49 bytes of
     * Assn Fixed2Data, 22 of 114/Props, 49 of Task Fixed2Data, 190 of CEdl, 247
     * of Props14), so every byte the settle changed is masked and a port
     * output wrong at one of them still passes.  Only the Rsc flag (refused
     * by 2. above) and the CEdl rebuild are open items; the identity bytes are
     * Project's per-save renumbering.  (The resource flag byte itself is the null save's session bit, masked by
     * the harness, and is what 2. refuses).  Project's save also carries a
     * Props14 edit marker the harness cannot see as an effect for this pair
     * (declared-effect check fails with effects = 0), so Props14 is not in the
     * declared list below and this test does not vouch for it. */
    CfbDocument settled = control;
    for (char const* path : {kAssnMeta, kAssnF2Meta, kProps}) {
        settled.replace_stream(path, null_save.read_stream(path));
    }
    std::string const out = out_path("w35-assign-remove-port-saved.mpp");
    (void)mppwrite::unassign_resource(settled, key(1, 2), out);

    std::array<char const*, 12> const declared{{
        kAssnFixed, kAssnMeta, kAssnF2, kAssnF2Meta, kAssnVarMeta, kAssnVar2,
        kTaskFixed, kTaskMeta, kTaskVar2, kRscFixed, kProps, kSummary}};
    std::array<save_pair::KnownGap, 4> const gaps{{
        /* The appended record's session identity suffix, which Project
         * renumbers on every save (see the identity notes above): the
         * transplant left the control's wave-30-session Assn Fixed2Data in
         * place, whereas the null's records already carry Project's suffix.
         * It is not a rule to derive.  The Var2Data gap at 524 is very likely
         * the same thing (not separated by a measurement). */
        {kAssnF2, 6, 196, 0xd1, 0xdb},
        {kAssnVar2, 3, 524, 0x12, 0xba},
        /* Project clears a resource flag the port-saved file still carries. */
        {"114/TBkndRsc/Fixed2Meta", 1, 279, 0x2b, 0x0b},
        /* Project's CEdl is 64 bytes shorter (normalised 430 against the port's 494; raw 540
         * against 586): the displayed-row list it rebuilds.  A length gap, so the signature's
         * 0x00 / 0x00 names no differing value (see the pins below). */
        {"214/CEdl/Var2Data", 64, 430, 0x00, 0x00}}};
    {
        /* R35G3-12: the CEdl gap above is a LENGTH, not a changed byte.  Compared after
         * normalise_cedl (the save-path record already swapped for a fixed sentinel, so path
         * length is not in it), Project's CEdl is 430 bytes (the null save's, and its edit
         * save's) and the port's is the control's 494: Project rebuilds the displayed-row list
         * 64 bytes shorter.  Positions 430 to 493 are therefore stored only by the port, which
         * is why the gap's signature reads port 0x00 / Project 0x00: 0x00 is the control's byte
         * at 430 and Project's side is past its end, which the harness reports as 0.  The
         * checks below pin what the signature cannot: the lengths, and that the port leaves
         * the control's CEdl as it was. */
        Bytes const port_cedl = save_pair::normalise_cedl(
            CfbDocument::from_file(out).read_stream("214/CEdl/Var2Data"));
        Bytes const project_cedl = save_pair::normalise_cedl(
            doc_from_data(kPortEdit).read_stream("214/CEdl/Var2Data"));
        Bytes const control_cedl =
            save_pair::normalise_cedl(control.read_stream("214/CEdl/Var2Data"));
        t.equal(project_cedl.size(), std::size_t{430},
                "the CEdl gap: Project's normalised CEdl is 430 bytes");
        t.equal(port_cedl.size(), std::size_t{494},
                "the CEdl gap: the port's normalised CEdl is the control's 494 bytes");
        t.check(port_cedl == control_cedl,
                "the CEdl gap: the port leaves the control's CEdl as it was");
        t.check(port_cedl.size() > 430 && port_cedl[430] == 0x00,
                "the CEdl gap: the port's byte at 430 is the 0x00 the signature names");
    }
    save_pair::compare_all(t, "assign-remove-port-saved", control, null_save,
                           doc_from_data(kPortEdit), CfbDocument::from_file(out),
                           gaps.data(), gaps.size(), declared.data(),
                           declared.size(), tail_normalisation(null_save));
}

} /* namespace */

int main() {
    try {
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);
        TestContext t;
        test_add_pair(t);
        test_remove_pair(t);
        test_remove_port_saved(t);
        test_append_pairs(t);
        test_edit_refusals(t);
        test_path_refusals(t);
        test_add_damaged(t);
        test_measured_shape_refusals(t);
        test_settled_damaged(t);
        test_remove_damaged(t);
        test_dry_run(t);
        return t.report();
    } catch (std::exception const& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << std::endl;
        return 1;
    }
}
