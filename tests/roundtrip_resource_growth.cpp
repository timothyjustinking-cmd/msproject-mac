/*
 * roundtrip_resource_growth.cpp -- W34-3.2: the measured Add Resources writer
 * against Project's own saves, plus every refusal it offers.
 *
 * WHAT IS PROVEN.  For each pair the port opens PROJECT'S CONTROL, adds the
 * resource through core/mppwrite/resource_growth.h exactly as the command does
 * ("R2" with the Created stamp the wave30 pair recorded; "R3" with the
 * wave34 pair's), and compares EVERY stream of the four documents with
 * Project's edit save through save_pair::compare_all: the eleven streams the
 * writer owns are declared (their measured effect sets must be non-empty),
 * there is NOT ONE KnownGap, and the only normalisation is the first dword of
 * each identity in the APPENDED records -- the three identities of the new
 * calendar slot and the two of the new resource Fixed2Data record.  Project
 * renumbers an appended row's identities on every save, so no port can equal
 * them (declared through save_pair::TailNormalisation; every other appended
 * byte is compared).  The committed null save masks per-save noise.  This is
 * the whole-file form of brief rule 1 on the two pairs that measured the
 * command.
 *
 * The structural receipts after each pair are independent of the comparison:
 * every stream size and header word, the appended row, entries, records and
 * five variable blocks in Project's block order, the calendar row, the three
 * 114/Props scalars, and the set of streams that changed.
 *
 * REFUSALS.  Every refusal branch the committed fixtures can reach is
 * exercised: the unmeasured counts, the other pair's shape, the argument
 * checks, the output paths, and damaged input -- every table's size word, entry
 * count and magic, the entry offsets, a tombstoned row, a non-default MaxUnits,
 * the template's 0x55 / 0xe3 blocks, the VarMeta header and order, the
 * calendar row count, the calendar slot's identities and the three Props
 * scalars.  Each refusal asserts the document was not modified and that no
 * output file appeared.
 *
 * NOT CONSTRUCTIBLE HERE: the refusal for a document whose resource or
 * calendar stream is absent (CfbDocument has no remove-stream operation and
 * every committed .mpp carries them) is a plain guard read by inspection.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "fixture_guard.h"
#include "resource_growth.h"
#include "save_pair_compare.h"

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

using mppwrite::CfbDocument;
using Bytes = std::vector<std::uint8_t>;

char const* kRscFixed = "114/TBkndRsc/FixedData";
char const* kRscMeta = "114/TBkndRsc/FixedMeta";
char const* kRscF2 = "114/TBkndRsc/Fixed2Data";
char const* kRscF2Meta = "114/TBkndRsc/Fixed2Meta";
char const* kRscVarMeta = "114/TBkndRsc/VarMeta";
char const* kRscVar2 = "114/TBkndRsc/Var2Data";
char const* kCalFixed = "114/TBkndCal/FixedData";
char const* kCalMeta = "114/TBkndCal/FixedMeta";
char const* kCalF2 = "114/TBkndCal/Fixed2Data";
char const* kCalF2Meta = "114/TBkndCal/Fixed2Meta";
char const* kProps = "114/Props";
char const* kMarker = "Props14";

constexpr std::uint32_t kResourceCountTag = 0x02000001u;
constexpr std::uint32_t kResourceSizeTag = 0x02000100u;
constexpr std::uint32_t kCalendarCountTag = 0x03000001u;

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

std::string refused_path() { return out_path("w34-resource-refused.mpp"); }

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

void put_u32_at(Bytes& b, std::size_t o, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[o + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

void put_f64_at(Bytes& b, std::size_t o, double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof bits);
    for (std::size_t i = 0; i < 8; ++i) {
        b[o + i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu);
    }
}

/* The value word of a 16-byte Props scalar {4, tag (big-endian), 4, value}. */
std::uint32_t scalar_value(Bytes const& props, std::uint32_t tag) {
    for (std::size_t at = 16; at + 16 <= props.size(); ++at) {
        if (u32_at(props, at) == 4 && u32_at(props, at + 8) == 4 &&
            big_u32_at(props, at + 4) == tag) {
            return u32_at(props, at + 12);
        }
    }
    throw std::runtime_error("the Props scalar is missing");
}

void set_scalar(Bytes& props, std::uint32_t tag, std::uint32_t value) {
    for (std::size_t at = 16; at + 16 <= props.size(); ++at) {
        if (u32_at(props, at) == 4 && u32_at(props, at + 8) == 4 &&
            big_u32_at(props, at + 4) == tag) {
            put_u32_at(props, at + 12, value);
            return;
        }
    }
    throw std::runtime_error("the Props scalar is missing");
}

Bytes slice_of(Bytes const& bytes, std::size_t offset, std::size_t length) {
    return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
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

struct Pair {
    char const* control;
    char const* null_save;
    char const* edit;
    char const* label;
    char const* name;
    std::uint32_t created;
    std::size_t rows_before;   /* resource table rows, row 0 included */
    std::size_t new_uid;
    std::size_t cal_uid;
    std::size_t var2_before;
    std::size_t fm_flag_13;    /* Rsc FixedMeta +13 of the new entry */
    std::uint8_t f2m_flag_8;   /* Rsc Fixed2Meta +8 of the new entry */
    std::uint16_t row_counter; /* the new row's +154 */
};

Pair const kSecond{"wave30-resource-add-control.mpp",
                   "wave30-resource-add-control-null.mpp",
                   "wave30-resource-add.mpp",
                   "resource-add-second",
                   "R2",
                   0x3d182a12u,
                   2,
                   2,
                   4,
                   86,
                   0x46,
                   0x2B,
                   4};
Pair const kThird{"wave34-resource-add-third-control.mpp",
                  "wave34-resource-add-third-control-null.mpp",
                  "wave34-resource-add-third.mpp",
                  "resource-add-third",
                  "R3",
                  0x3d1c19a0u,
                  3,
                  3,
                  5,
                  140,
                  0x47,
                  0x0B,
                  5};

/* The first dword of every identity in the appended records, which Project
 * renumbers on each save.
 *
 * W34b-0.2 (RC-W4): which of these words are noise and which are references.
 * The control and its null save (wave30-resource-add-control[-null], the third
 * pair likewise) differ, on the EXISTING records, at exactly:
 *   Cal Fixed2Data  +32 of record 5 and +16 / +32 of record 6 (offsets 272, 304,
 *                   320: 89b9c40a -> f958d06f, 89b9c40b -> f958d070), and
 *   Rsc Fixed2Data  +0 and +24 of the resource record (offsets 160, 184:
 *                   89b9c40b -> f958d070), and
 *   Props           the 16-byte identity at 2834 (89b9c40a -> f958d06f),
 * while the calendar records' own +0 (offset 240 / 288: 7e8bcba0, bacb09b6) is
 * identical in both.  So +16 and +32 are not counters: each is a REFERENCE that
 * Project re-issues on every save (+16 = the identity of the resource the
 * calendar belongs to, +32 = the one identity every calendar record shares, the
 * 16 bytes held once in 114/Props), and the appended record's +0 is the fresh
 * identity it is created with, equal to its +16 (it is also the new resource's).
 * The first dwords are masked because their ABSOLUTE value is the save's; the
 * references are compared by what they name in calendar_identity_problem, in
 * Project's save and in the port's output. */
std::vector<save_pair::TailNormalisation> tail_normalisation(
    CfbDocument const& null_save) {
    std::size_t const cal_base = null_save.read_stream(kCalF2).size();
    std::size_t const rsc_base = null_save.read_stream(kRscF2).size();
    save_pair::TailNormalisation cal;
    cal.path = kCalF2;
    cal.ranges = {{cal_base + 0, 4}, {cal_base + 16, 4}, {cal_base + 32, 4}};
    save_pair::TailNormalisation rsc;
    rsc.path = kRscF2;
    rsc.ranges = {{rsc_base + 0, 4}, {rsc_base + 24, 4}};
    return {cal, rsc};
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

/* W34b-0.2 (RC-W4): the appended calendar record [fresh][fresh][shared] names
 * the new resource (+16, and its own identity at +0) and the shared calendar
 * identity (+32, held once in 114/Props and by every other calendar record).
 * Returns the first reference that does not name what it should, or "". */
std::string calendar_identity_problem(CfbDocument const& file,
                                      std::size_t cal_record,
                                      std::size_t rsc_record) {
    Bytes const cal = file.read_stream(kCalF2);
    Bytes const rsc = file.read_stream(kRscF2);
    Bytes const props = file.read_stream(kProps);
    std::size_t const at = cal_record * 48;
    std::size_t const rsc_at = rsc_record * 40;
    if (cal_record == 0 || cal.size() < at + 48 || rsc.size() < rsc_at + 16) {
        return "the calendar or resource record is missing";
    }
    auto same = [](Bytes const& a, std::size_t ai, Bytes const& b, std::size_t bi) {
        return std::equal(a.begin() + static_cast<std::ptrdiff_t>(ai),
                          a.begin() + static_cast<std::ptrdiff_t>(ai + 16),
                          b.begin() + static_cast<std::ptrdiff_t>(bi));
    };
    if (!same(cal, at + 16, rsc, rsc_at)) {
        return "the resource reference (+16) is not the new resource's identity";
    }
    if (!same(cal, at, cal, at + 16)) {
        return "the record's own identity (+0) is not its fresh identity (+16)";
    }
    if (!same(cal, at + 32, cal, at - 48 + 32)) {
        return "the shared identity (+32) is not the other calendars'";
    }
    std::size_t held = 0;
    Bytes const shared = slice_of(cal, at + 32, 16);
    for (auto hit = std::search(props.begin(), props.end(), shared.begin(), shared.end());
         hit != props.end();
         hit = std::search(hit + 1, props.end(), shared.begin(), shared.end())) {
        ++held;
    }
    if (held != 1) {
        return "the shared identity (+32) is not the one held in 114/Props";
    }
    /* W35-0.2 (R34b-4): the resource record holds its identity twice, +0 and +24
     * (16 bytes each).  Project's save has them equal in every record (measured
     * on wave30-resource-add: the re-issued 89b9c420 and the new 89b9c421 at both
     * offsets), and the first dword of each is masked in the pair compare, so
     * the equality is asserted here, on the appended record in both files. */
    if (!same(rsc, rsc_at, rsc, rsc_at + 24)) {
        return "the resource record's second identity (+24) is not its identity (+0)";
    }
    return std::string();
}

void check_calendar_identities(TestContext& t, std::string const& label,
                               CfbDocument const& null_save,
                               CfbDocument const& project,
                               CfbDocument const& port) {
    std::size_t const cal_record = null_save.read_stream(kCalF2).size() / 48;
    std::size_t const rsc_record = null_save.read_stream(kRscF2).size() / 40;
    std::string const project_problem =
        calendar_identity_problem(project, cal_record, rsc_record);
    t.check(project_problem.empty(),
            label + ": Project's appended calendar record names the new resource "
                    "and the shared identity (" + project_problem + ")");
    std::string const port_problem =
        calendar_identity_problem(port, cal_record, rsc_record);
    t.check(port_problem.empty(),
            label + ": the port's appended calendar record names the same "
                    "(" + port_problem + ")");

    /* A wrong resource reference (+16: the previous resource's identity). */
    {
        CfbDocument scratch = port;
        Bytes cal = scratch.read_stream(kCalF2);
        Bytes const rsc = scratch.read_stream(kRscF2);
        std::size_t const other = (rsc_record - 1) * 40;
        std::copy(rsc.begin() + static_cast<std::ptrdiff_t>(other),
                  rsc.begin() + static_cast<std::ptrdiff_t>(other + 16),
                  cal.begin() + static_cast<std::ptrdiff_t>(cal_record * 48 + 16));
        scratch.replace_stream(kCalF2, cal);
        std::string const problem =
            calendar_identity_problem(scratch, cal_record, rsc_record);
        std::cout << "   " << label << " planted wrong resource reference -> "
                  << (problem.empty() ? "NOT DETECTED" : problem) << "\n";
        t.check(problem.find("resource reference (+16)") != std::string::npos,
                label + ": a wrong resource reference (+16) is detected");
    }
    /* A wrong second identity (+24) on the appended resource record: the masked
     * first dword off by one, the only difference the pair compare cannot see. */
    {
        CfbDocument scratch = port;
        Bytes rsc = scratch.read_stream(kRscF2);
        rsc[rsc_record * 40 + 24] = static_cast<std::uint8_t>(rsc[rsc_record * 40 + 24] ^ 1u);
        scratch.replace_stream(kRscF2, rsc);
        std::string const problem =
            calendar_identity_problem(scratch, cal_record, rsc_record);
        std::cout << "   " << label << " planted wrong resource identity copy -> "
                  << (problem.empty() ? "NOT DETECTED" : problem) << "\n";
        t.check(problem.find("second identity (+24)") != std::string::npos,
                label + ": a wrong resource identity copy (+24) is detected");
    }
    /* A shared identity that is not the one in Props (first dword off by one). */
    {
        CfbDocument scratch = port;
        Bytes cal = scratch.read_stream(kCalF2);
        cal[cal_record * 48 + 32] = static_cast<std::uint8_t>(cal[cal_record * 48 + 32] ^ 1u);
        scratch.replace_stream(kCalF2, cal);
        std::string const problem =
            calendar_identity_problem(scratch, cal_record, rsc_record);
        std::cout << "   " << label << " planted wrong shared identity -> "
                  << (problem.empty() ? "NOT DETECTED" : problem) << "\n";
        t.check(problem.find("shared identity (+32)") != std::string::npos,
                label + ": a wrong shared identity (+32) is detected");
    }
}

void test_pair(TestContext& t, Pair const& p) {
    std::string const label = p.label;
    fixture::require_committed(join(MPPWRITE_TEST_DATA, p.control));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, p.null_save));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, p.edit));

    CfbDocument doc = doc_from_data(p.control);
    CfbDocument const before = doc;
    std::size_t const rows = p.rows_before;
    t.equal(doc.read_stream(kRscFixed).size(), 48 + 172 * rows,
            label + " control: Rsc FixedData is 48 + 172 * rows");
    t.equal(doc.read_stream(kRscVar2).size(), p.var2_before,
            label + " control: Rsc Var2Data size");

    std::string const path = out_path("w34-resource-" + label + ".mpp");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    mppwrite::ResourceGrowthSpec spec;
    spec.name = p.name;
    spec.created = p.created;
    mppwrite::ResourceGrowthWrite const written =
        mppwrite::add_work_resource(doc, spec, path);

    t.check(file_exists(path), label + ": the package was written");
    t.equal(written.uid, p.new_uid, label + ": the new resource uid");
    t.equal(written.id, p.new_uid, label + ": the new resource id");
    t.equal(written.calendar_uid, p.cal_uid, label + ": the new calendar uid");
    t.equal(written.rows_before, rows, label + ": rows before");
    t.equal(written.rows_after, rows + 1, label + ": rows after");

    Bytes const rsc_fixed = doc.read_stream(kRscFixed);
    Bytes const rsc_meta = doc.read_stream(kRscMeta);
    Bytes const rsc_f2 = doc.read_stream(kRscF2);
    Bytes const rsc_f2_meta = doc.read_stream(kRscF2Meta);
    Bytes const rsc_var_meta = doc.read_stream(kRscVarMeta);
    Bytes const rsc_var2 = doc.read_stream(kRscVar2);
    Bytes const cal_fixed = doc.read_stream(kCalFixed);
    Bytes const cal_meta = doc.read_stream(kCalMeta);
    Bytes const cal_f2 = doc.read_stream(kCalF2);
    Bytes const cal_f2_meta = doc.read_stream(kCalF2Meta);

    /* Sizes and header words. */
    t.equal(rsc_fixed.size(), 48 + 172 * (rows + 1), label + ": Rsc FixedData");
    t.equal(rsc_meta.size(), 16 + 37 * (rows + 4), label + ": Rsc FixedMeta");
    t.equal(rsc_f2.size(), 40 * (rows + 4), label + ": Rsc Fixed2Data");
    t.equal(rsc_f2_meta.size(), 16 + 51 * (rows + 4), label + ": Rsc Fixed2Meta");
    t.equal(rsc_var_meta.size(), before.read_stream(kRscVarMeta).size() + 60,
            label + ": Rsc VarMeta grows by five entries");
    t.equal(rsc_var2.size(), p.var2_before + 54, label + ": Rsc Var2Data +54");
    t.equal(u32_at(rsc_meta, 8), rows + 4, label + ": Rsc FixedMeta count");
    t.equal(u32_at(rsc_meta, 12), rsc_fixed.size(), label + ": Rsc FixedMeta size");
    t.equal(u32_at(rsc_f2_meta, 8), rows + 4, label + ": Rsc Fixed2Meta count");
    t.equal(u32_at(rsc_f2_meta, 12), rsc_f2.size(), label + ": Rsc Fixed2Meta size");
    t.equal(u32_at(rsc_var_meta, 20), rsc_var2.size(), label + ": VarMeta size");
    t.equal(cal_fixed.size(), 64 + 12 * (rows + 2), label + ": Cal FixedData");
    t.equal(cal_meta.size(), 16 + 10 * (rows + 6), label + ": Cal FixedMeta");
    t.equal(cal_f2.size(), 48 * (rows + 6), label + ": Cal Fixed2Data");
    t.equal(cal_f2_meta.size(), 16 + 10 * (rows + 6), label + ": Cal Fixed2Meta");
    t.equal(u32_at(cal_meta, 8), rows + 6, label + ": Cal FixedMeta count");
    t.equal(u32_at(cal_meta, 12), cal_fixed.size(), label + ": Cal FixedMeta size");
    t.equal(u32_at(cal_f2_meta, 8), rows + 6, label + ": Cal Fixed2Meta count");
    t.equal(u32_at(cal_f2_meta, 12), cal_f2.size(), label + ": Cal Fixed2Meta size");

    /* The appended Rsc row, entry and record. */
    std::size_t const row_at = 48 + 172 * rows;
    t.equal(u32_at(rsc_fixed, row_at + 0), p.new_uid, label + ": row id");
    t.equal(u32_at(rsc_fixed, row_at + 4), p.new_uid, label + ": row uid");
    t.check(f64_at(rsc_fixed, row_at + 8) == 10000.0, label + ": MaxUnits 100 %");
    t.equal(u16_at(rsc_fixed, row_at + 154), p.row_counter,
            label + ": the +154 counter");
    t.check(slice_of(rsc_fixed, 0, 48 + 172 * rows) ==
                before.read_stream(kRscFixed),
            label + ": every old row is kept");
    std::size_t const entry_at = 16 + 37 * (rows + 3);
    t.equal(u32_at(rsc_meta, entry_at + 4), row_at, label + ": entry offset");
    t.equal(rsc_meta[entry_at + 0], 0, label + ": entry is live");
    t.equal(rsc_meta[entry_at + 12], 0x1C, label + ": entry +12");
    t.equal(rsc_meta[entry_at + 13], p.fm_flag_13, label + ": entry +13");
    std::size_t const f2_at = 40 * (rows + 3);
    t.check(std::equal(rsc_f2.begin() + static_cast<std::ptrdiff_t>(f2_at),
                       rsc_f2.begin() + static_cast<std::ptrdiff_t>(f2_at + 16),
                       rsc_f2.begin() + static_cast<std::ptrdiff_t>(f2_at + 24)),
            label + ": the record repeats its identity");
    t.check(f64_at(rsc_f2, f2_at + 16) == static_cast<double>(rows + 1),
            label + ": the record's ordinal is the new row index + 1");
    std::size_t const f2m_at = 16 + 51 * (rows + 3);
    t.equal(u32_at(rsc_f2_meta, f2m_at + 4), f2_at, label + ": Fixed2Meta offset");
    t.equal(rsc_f2_meta[f2m_at + 8], p.f2m_flag_8, label + ": Fixed2Meta +8");

    /* The five variable blocks, in Project's block order. */
    std::size_t const base = p.var2_before;
    t.equal(u32_at(rsc_var2, base), 4, label + ": Created block length");
    t.equal(u32_at(rsc_var2, base + 4), p.created, label + ": Created stamp");
    t.equal(u32_at(rsc_var2, base + 8), 16, label + ": blob length");
    t.equal(u32_at(rsc_var2, base + 28), 6, label + ": Name block length");
    t.equal(rsc_var2[base + 32], 'R', label + ": Name starts with R");
    t.equal(u32_at(rsc_var2, base + 38), 4, label + ": Initials block length");
    t.equal(u32_at(rsc_var2, base + 42), 'R', label + ": Initials");
    t.equal(u32_at(rsc_var2, base + 46), 4, label + ": small block length");
    t.equal(u32_at(rsc_var2, base + 50), 2, label + ": small block value");
    std::size_t const first_new = before.read_stream(kRscVarMeta).size();
    std::array<std::pair<std::uint32_t, std::size_t>, 5> const expected_entries = {{
        {0x0C400001u, base + 28},
        {0x0C400002u, base + 38},
        {0x0C400055u, base + 46},
        {0x0C4002D6u, base + 0},
        {0x0C4002E3u, base + 8},
    }};
    for (std::size_t i = 0; i < expected_entries.size(); ++i) {
        std::size_t const at = first_new + 12 * i;
        t.equal(u32_at(rsc_var_meta, at), p.new_uid,
                label + ": VarMeta entry uid " + std::to_string(i));
        t.equal(u32_at(rsc_var_meta, at + 4), expected_entries[i].second,
                label + ": VarMeta entry offset " + std::to_string(i));
        t.equal(u32_at(rsc_var_meta, at + 8), expected_entries[i].first,
                label + ": VarMeta entry key " + std::to_string(i));
    }

    /* The calendar row, entries and slot. */
    std::size_t const cal_row = 64 + 12 * (rows + 1);
    t.equal(u32_at(cal_fixed, cal_row + 0), 1, label + ": calendar row +0");
    t.equal(u32_at(cal_fixed, cal_row + 4), p.new_uid, label + ": calendar row +4");
    t.equal(u32_at(cal_fixed, cal_row + 8), p.cal_uid, label + ": calendar row +8");
    std::size_t const cal_entry = 16 + 10 * (rows + 5);
    t.equal(u32_at(cal_meta, cal_entry + 4), cal_row, label + ": Cal FixedMeta entry");
    t.equal(u16_at(cal_meta, cal_entry + 8), 0x0E, label + ": Cal FixedMeta trailer");
    t.equal(u32_at(cal_f2_meta, cal_entry + 4), 48 * (rows + 5),
            label + ": Cal Fixed2Meta entry");
    t.equal(u16_at(cal_f2_meta, cal_entry + 8), 0x0E,
            label + ": Cal Fixed2Meta trailer");
    std::size_t const slot = 48 * (rows + 5);
    t.check(slice_of(cal_f2, slot, 16) == slice_of(cal_f2, slot + 16, 16),
            label + ": the slot's first two identities are the same");
    t.check(slice_of(cal_f2, slot + 32, 16) == slice_of(cal_f2, slot - 16, 16),
            label + ": the slot's third identity is the previous slot's third");
    t.check(slice_of(cal_f2, slot, 16) == slice_of(rsc_f2, f2_at, 16),
            label + ": the calendar and resource records share the fresh identity");

    /* The three Props scalars. */
    Bytes const props = doc.read_stream(kProps);
    Bytes const control_props = before.read_stream(kProps);
    t.equal(props.size(), control_props.size(), label + ": 114/Props keeps its size");
    t.equal(scalar_value(props, kResourceCountTag), rows + 4,
            label + ": Resource count");
    t.equal(scalar_value(props, kResourceSizeTag), p.var2_before + 54,
            label + ": Resource size");
    t.equal(scalar_value(props, kCalendarCountTag), rows + 6,
            label + ": Calendar count");
    t.equal(written.resource_count_before, rows + 3, label + ": reported count before");
    t.equal(written.calendar_count_after, rows + 6, label + ": reported cal count");

    check_only_owned_streams(t, before, doc, label,
                             {kRscFixed, kRscMeta, kRscF2, kRscF2Meta,
                              kRscVarMeta, kRscVar2, kCalFixed, kCalMeta, kCalF2,
                              kCalF2Meta, kProps, kMarker});

    static std::array<char const*, 12> const kDeclared = {
        kRscFixed, kRscMeta,  kRscF2,    kRscF2Meta, kRscVarMeta, kRscVar2,
        kCalFixed, kCalMeta,  kCalF2,    kCalF2Meta, kProps,      kMarker};
    CfbDocument const null_save = doc_from_data(p.null_save);
    save_pair::compare_all(t, label, doc_from_data(p.control), null_save,
                           doc_from_data(p.edit), CfbDocument::from_file(path),
                           nullptr, 0, kDeclared.data(), kDeclared.size(),
                           tail_normalisation(null_save));
    check_calendar_identities(t, label, null_save, doc_from_data(p.edit),
                              CfbDocument::from_file(path));
}

/* ---- refusals ----------------------------------------------------------- */

mppwrite::ResourceGrowthSpec good_spec() {
    mppwrite::ResourceGrowthSpec spec;
    spec.name = "R3";
    spec.created = 0x3d1c19a0u;
    return spec;
}

void refuses(TestContext& t, CfbDocument const& source,
             mppwrite::ResourceGrowthSpec const& spec, std::string_view needle,
             std::string const& what, std::string const& path = refused_path()) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    CfbDocument doc = source;
    Bytes const before = doc.to_bytes();
    expect_error(t, [&] { (void)mppwrite::add_work_resource(doc, spec, path); },
                 needle, what);
    t.check(doc.to_bytes() == before, what + ": the document changed");
    t.check(!file_exists(path), what + ": an output file was written");
}

void test_argument_refusals(TestContext& t) {
    CfbDocument const control = doc_from_data(kThird.control);
    mppwrite::ResourceGrowthSpec spec = good_spec();

    spec = good_spec();
    spec.name.clear();
    refuses(t, control, spec, "name must be", "an empty name");
    spec.name = std::string(65, 'A');
    refuses(t, control, spec, "name must be", "a 65-character name");
    /* W34b-1.4 (R34G4-6): the longest measured name is two characters. */
    spec.name = "R33";
    refuses(t, control, spec, "name must be 1..2", "a three-character name");
    for (char const* delimited : {"A,", "[A", "A]"}) {
        spec.name = delimited;
        refuses(t, control, spec, "none of , [ ]",
                std::string("a name holding a delimiter: ") + delimited);
    }
    spec.name = "caf\xC3\xA9";
    refuses(t, control, spec, "name must be", "a non-ASCII name");
    spec.name = "A\tB";
    refuses(t, control, spec, "name must be", "a control character in the name");
    spec = good_spec();
    spec.initials = "ABCDEFGHI";
    refuses(t, control, spec, "initials must be", "nine initials");
    spec.initials = "\xC3\xA9";
    refuses(t, control, spec, "initials must be", "non-ASCII initials");
    spec = good_spec();
    spec.created = 0;
    refuses(t, control, spec, "Created stamp", "a zero Created stamp");
    spec.created = 0xFFFFFFFFu;
    refuses(t, control, spec, "Created stamp", "an all-ones Created stamp");

    spec = good_spec();
    refuses(t, control, spec, "output path is empty", "an empty output path", "");
    refuses(t, control, spec, "is a directory", "a directory as the path",
            MPPWRITE_TEST_OUT);
    refuses(t, control, spec, "directory", "a missing parent directory",
            join(out_path("w34-no-such-dir"), "x.mpp"));
}

void test_shape_refusals(TestContext& t) {
    mppwrite::ResourceGrowthSpec const spec = good_spec();
    refuses(t, doc_from_data(kThird.edit), spec, "4 rows",
            "the third pair's edit (4 rows) is not a measured count");
    /* The wave30 control has two rows; its pair measures "R2" with n = 2, so the
     * writer must accept it -- proven by the pair test.  A table grown by a row
     * without its meta entry is damage. */
    CfbDocument grown = doc_from_data(kThird.control);
    {
        Bytes fixed = grown.read_stream(kRscFixed);
        fixed.insert(fixed.end(), 172, 0);
        grown.replace_stream(kRscFixed, fixed);
    }
    refuses(t, grown, spec, "", "a resource row without its meta entry");

    /* R34G4-3: the last resource of wave30-assign-add.mpp carries 40 h of work.
     * Cloning its row used to yield a new resource with 40 h of work and no
     * assignment; the writer now refuses any template that is not the measured
     * one, and names the offset (52 is the first work word). */
    refuses(t, doc_from_data("wave30-assign-add.mpp"), spec, "row offset 52",
            "a last resource that carries 40 h of work");
}

/* R34G4-3: every template-row field outside the ones the writer rewrites must
 * equal the measured template.  Each case plants one byte on the last row (or its
 * meta entries) of the third pair's control. */
void test_template_refusals(TestContext& t) {
    mppwrite::ResourceGrowthSpec const spec = good_spec();
    std::string const control = kThird.control;
    constexpr std::size_t last_row = 48 + 172 * 2;
    auto row_byte = [&](std::size_t offset, std::uint8_t value,
                        std::string const& what) {
        refuses(t,
                with_edit(control, kRscFixed,
                          [&](Bytes& b) { b[last_row + offset] = value; }),
                spec, "row offset " + std::to_string(offset), what);
    };
    row_byte(52, 0x80, "work on the last row");
    row_byte(80, 0x07, "a changed flag word on the last row");
    row_byte(94, 0x80, "actual work on the last row");
    row_byte(110, 0x80, "remaining work on the last row");
    row_byte(130, 0x01, "a cost figure on the last row");
    row_byte(158, 0x00, "a changed calendar-reference word on the last row");
    row_byte(166, 0x05, "a changed type word on the last row");
    row_byte(168, 0x05, "a changed accrual word on the last row");

    /* W35-0.2 (R34b-5): word 154 is masked in the template compare, so it is tied to
     * the calendar table separately -- it must be the highest calendar uid, on a row
     * that belongs to the last resource. */
    refuses(t,
            with_edit(control, kRscFixed,
                      [&](Bytes& b) { b[last_row + 154] = static_cast<std::uint8_t>(b[last_row + 154] - 1); }),
            spec, "not the highest calendar uid", "a calendar word that is not the highest uid");
    refuses(t,
            with_edit(control, kRscFixed,
                      [&](Bytes& b) { b[last_row + 154] = static_cast<std::uint8_t>(b[last_row + 154] + 1); }),
            spec, "not the highest calendar uid", "a calendar word beyond the highest uid");
    refuses(t,
            with_edit(control, kCalFixed,
                      [](Bytes& b) {
                          std::size_t const rows = (b.size() - 64) / 12;
                          std::uint32_t top = 0;
                          for (std::size_t r = 0; r < rows; ++r) {
                              top = std::max(top, u32_at(b, 64 + 12 * r + 8));
                          }
                          for (std::size_t r = 0; r < rows; ++r) {
                              if (u32_at(b, 64 + 12 * r + 8) == top) {
                                  put_u32_at(b, 64 + 12 * r + 4, 99);
                              }
                          }
                      }),
            spec, "does not belong to that resource",
            "the highest calendar row belonging to another resource");

    refuses(t,
            with_edit(control, kRscMeta,
                      [](Bytes& b) { b[16 + 37 * 5 + 2] ^= 0x01; }),
            spec, "FixedMeta entry differs", "a changed last-resource FixedMeta entry");
    refuses(t,
            with_edit(control, kRscF2Meta,
                      [](Bytes& b) { b[16 + 51 * 5 + 38] ^= 0x01; }),
            spec, "Fixed2Meta entry differs",
            "a changed last-resource Fixed2Meta entry");
}

void test_damaged(TestContext& t) {
    mppwrite::ResourceGrowthSpec const spec = good_spec();
    std::string const control = kThird.control;
    auto damaged = [&](char const* stream, std::string_view needle,
                       std::string const& what, auto&& mutate) {
        refuses(t, with_edit(control, stream, mutate), spec, needle, what);
    };

    /* Resource table. */
    damaged(kRscFixed, "48 + 172", "Rsc FixedData off the row grid",
            [](Bytes& b) { b.push_back(0); });
    damaged(kRscMeta, "table magic", "Rsc FixedMeta magic",
            [](Bytes& b) { b[0] ^= 0xFF; });
    damaged(kRscMeta, "disagree", "Rsc FixedMeta count",
            [](Bytes& b) { b[8] += 1; });
    damaged(kRscMeta, "disagree", "Rsc FixedMeta size", [](Bytes& b) { b[12] += 1; });
    damaged(kRscMeta, "does not name its row", "Rsc FixedMeta entry offset",
            [](Bytes& b) { b[16 + 37 * 4 + 4] += 1; });
    damaged(kRscMeta, "not live", "a tombstoned resource row",
            [](Bytes& b) { b[16 + 37 * 5] = 2; });
    damaged(kRscF2, "records", "Rsc Fixed2Data length",
            [](Bytes& b) { b.resize(b.size() - 40); });
    damaged(kRscF2Meta, "table magic", "Rsc Fixed2Meta magic",
            [](Bytes& b) { b[1] ^= 0xFF; });
    damaged(kRscF2Meta, "does not name its record", "Rsc Fixed2Meta entry offset",
            [](Bytes& b) { b[16 + 51 * 2 + 4] += 8; });
    damaged(kRscFixed, "MaxUnits", "a non-default MaxUnits on the last row",
            [](Bytes& b) { put_f64_at(b, 48 + 172 * 2 + 8, 5000.0); });
    damaged(kRscVarMeta, "table magic", "Rsc VarMeta magic",
            [](Bytes& b) { b[2] ^= 0xFF; });
    damaged(kRscVarMeta, "disagree", "Rsc VarMeta count", [](Bytes& b) { b[8] += 1; });
    damaged(kRscVarMeta, "disagree", "Rsc VarMeta size", [](Bytes& b) { b[20] += 1; });
    damaged(kRscVarMeta, "outside Var2Data", "a VarMeta entry past Var2Data",
            [](Bytes& b) { put_u32_at(b, 24 + 12 * 3 + 4, 0x00FFFFFFu); });
    damaged(kRscVarMeta, "beyond the largest", "a VarMeta uid past the last row",
            [](Bytes& b) { put_u32_at(b, 24 + 12 * 11, 9); });
    damaged(kRscVarMeta, "ascending uid", "an unsorted VarMeta",
            [](Bytes& b) {
                put_u32_at(b, 24, 2);
                put_u32_at(b, 24 + 12, 1);
            });
    damaged(kRscVar2, "0x0c400055", "the last row's 0x55 block changed",
            [&](Bytes& b) {
                CfbDocument doc = doc_from_data(control);
                Bytes const meta = doc.read_stream(kRscVarMeta);
                for (std::size_t i = 0; i < (meta.size() - 24) / 12; ++i) {
                    if (u32_at(meta, 24 + 12 * i) == 2 &&
                        u32_at(meta, 24 + 12 * i + 8) == 0x0C400055u) {
                        b[u32_at(meta, 24 + 12 * i + 4) + 4] = 3;
                    }
                }
            });
    damaged(kRscVar2, "0x0c400055", "the last row's 0xe3 block not zero",
            [&](Bytes& b) {
                CfbDocument doc = doc_from_data(control);
                Bytes const meta = doc.read_stream(kRscVarMeta);
                for (std::size_t i = 0; i < (meta.size() - 24) / 12; ++i) {
                    if (u32_at(meta, 24 + 12 * i) == 2 &&
                        u32_at(meta, 24 + 12 * i + 8) == 0x0C4002E3u) {
                        b[u32_at(meta, 24 + 12 * i + 4) + 4] = 1;
                    }
                }
            });

    /* Calendar table. */
    damaged(kCalFixed, "64 + 12", "Cal FixedData off the row grid",
            [](Bytes& b) { b.push_back(0); });
    damaged(kCalFixed, "calendars", "a calendar row count off the pair",
            [](Bytes& b) { b.insert(b.end(), 12, 0); });
    damaged(kCalMeta, "table magic", "Cal FixedMeta magic",
            [](Bytes& b) { b[0] ^= 0xFF; });
    damaged(kCalMeta, "disagree", "Cal FixedMeta count", [](Bytes& b) { b[8] += 1; });
    damaged(kCalMeta, "disagree", "Cal FixedMeta size", [](Bytes& b) { b[12] += 1; });
    damaged(kCalF2, "rows + 4", "Cal Fixed2Data length",
            [](Bytes& b) { b.resize(b.size() - 48); });
    damaged(kCalF2Meta, "table magic", "Cal Fixed2Meta magic",
            [](Bytes& b) { b[3] ^= 0xFF; });
    damaged(kCalF2Meta, "disagree", "Cal Fixed2Meta size",
            [](Bytes& b) { b[12] += 1; });
    damaged(kCalF2, "third place", "an empty third identity in the last slot",
            [](Bytes& b) { std::fill(b.end() - 16, b.end(), std::uint8_t{0}); });
    damaged(kCalF2, "third place", "a third identity of another family",
            [](Bytes& b) { b[b.size() - 1] ^= 0xFF; });
    damaged(kCalF2, "overflow", "an identity counter at 0xffff", [](Bytes& b) {
        b[b.size() - 14] = 0xFF;
        b[b.size() - 13] = 0xFF;
    });

    /* Props. */
    damaged(kProps, "do not agree", "the Resource count scalar", [](Bytes& b) {
        set_scalar(b, kResourceCountTag, scalar_value(b, kResourceCountTag) + 1);
    });
    damaged(kProps, "do not agree", "the Resource size scalar", [](Bytes& b) {
        set_scalar(b, kResourceSizeTag, scalar_value(b, kResourceSizeTag) + 1);
    });
    damaged(kProps, "do not agree", "the Calendar count scalar", [](Bytes& b) {
        set_scalar(b, kCalendarCountTag, scalar_value(b, kCalendarCountTag) + 1);
    });
}

/* W34-4.2: a dry run builds and validates exactly what a write does, reports
 * the same identities and counts, and touches neither the document nor any
 * file -- the path is not even examined.  The app's edit builder uses it to
 * refuse an unmeasured shape before anything is queued. */
void test_dry_run(TestContext& t, Pair const& p) {
    std::string const label = std::string("dry run ") + p.label;
    CfbDocument const control = doc_from_data(p.control);
    mppwrite::ResourceGrowthSpec spec;
    spec.name = p.name;
    spec.created = p.created;

    std::string const real_path = out_path("w34-resource-dry-real-" + label + ".mpp");
    std::error_code ignored;
    std::filesystem::remove(real_path, ignored);
    CfbDocument real = control;
    mppwrite::ResourceGrowthWrite const expected =
        mppwrite::add_work_resource(real, spec, real_path);

    std::string const never = join(out_path("w34-no-such-dir"), "dry.mpp");
    spec.dry_run = true;
    CfbDocument dry = control;
    Bytes const before = dry.to_bytes();
    mppwrite::ResourceGrowthWrite const reported =
        mppwrite::add_work_resource(dry, spec, never);
    t.check(dry.to_bytes() == before, label + ": the document is untouched");
    t.check(!file_exists(never), label + ": no file is written");
    t.equal(reported.uid, expected.uid, label + ": the uid a write would use");
    t.equal(reported.id, expected.id, label + ": the id a write would use");
    t.equal(reported.calendar_uid, expected.calendar_uid,
            label + ": the calendar uid a write would use");
    t.equal(reported.rows_after, expected.rows_after, label + ": rows after");
    t.equal(reported.resource_size_after, expected.resource_size_after,
            label + ": Resource size after");
    t.equal(reported.calendar_count_after, expected.calendar_count_after,
            label + ": Calendar count after");

    /* The same shape refusals apply to a dry run. */
    CfbDocument fourth = doc_from_data(kThird.edit);
    Bytes const fourth_before = fourth.to_bytes();
    expect_error(t, [&] { (void)mppwrite::add_work_resource(fourth, spec, never); },
                 "4 rows", label + ": an unmeasured count is refused");
    t.check(fourth.to_bytes() == fourth_before,
            label + ": the refused document is untouched");
    mppwrite::ResourceGrowthSpec bad = spec;
    bad.name = "caf\xC3\xA9";
    CfbDocument again = control;
    expect_error(t, [&] { (void)mppwrite::add_work_resource(again, bad, never); },
                 "name must be", label + ": a non-ASCII name is refused");
}

} /* namespace */

int main() {
    try {
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);
        TestContext t;
        test_pair(t, kSecond);
        test_pair(t, kThird);
        test_dry_run(t, kSecond);
        test_dry_run(t, kThird);
        test_argument_refusals(t);
        test_shape_refusals(t);
        test_template_refusals(t);
        test_damaged(t);
        return t.report();
    } catch (std::exception const& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << std::endl;
        return 1;
    }
}
