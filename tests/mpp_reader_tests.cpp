/*
 * mpp_reader_tests.cpp -- verification for the .mpp Task reader (W6).
 *
 * No third-party framework, same shape as tests/field_table_tests.cpp.
 *
 * The important test compares the reader against MS Project itself: every row
 * of tests/data/project_a_golden.tsv (83 live tasks, exported over COM -- see the
 * notes in tests/data/project_a_golden.md) is matched by UniqueID and every field is
 * checked: id, name hash, duration, start, finish, priority, percent complete.
 * The result is a per-field mismatch table plus the first five disagreements
 * with both values, not a bare PASS.
 *
 * Also checked:
 *   1. packed-date and duration formatting against W1's recorded examples;
 *   2. the committed synthetic fixture (tests/data/synthetic-base.mpp) loads and
 *      decodes to the values MS Project stored in it, including the outline and
 *      status fields (flat outline, no task critical: they are manually
 *      scheduled);
 *   3. PROJECT_A's dead row (uid 10856, row 67): skipped, not returned as a task;
 *   4. PROJECT_A's documented damage is reported in Document::warnings;
 *   5. PROJECT_B (73 live tasks, 12 deleted slots) and every corpus/probe fixture
 *      load when corpus/ is present;
 *   6. a non-CFB file is rejected with mpp::Error;
 *   7. the two W18 fixtures, against the COM values in docs/analysis/oracle/:
 *      corpus/probe/outline1.mpp (OutlineLevel 1/2/2, Summary, OutlineNumber
 *      "1"/"1.1"/"1.2", Critical) and corpus/probe/w3-o1.mpp (Critical flags and
 *      the 0-day milestone).  Both SKIP when corpus/ is absent.
 *   8. W19: the scheduling inputs.  corpus/probe/schedin.mpp has one task per
 *      PjConstraint value (KASAP..KFNLT), a 50 %-complete 2-day task and a
 *      deadline; docs/analysis/oracle/schedin-probe.json is the COM side.
 *      w3-o1's four SNET rows are the independent COM check for +64/+66.
 *   9. W24: PROJECT_A's link liveness.  MS Project's MSPDI export has 88
 *      <PredecessorLink> elements; tests/data/project_a_links_golden.tsv keeps the
 *      88 (succ_uid, pred_uid, type, lag) tuples as uids only.  The reader must
 *      reproduce that multiset exactly, and the 11 uncovered FixedData rows must
 *      not become links.
 *  10. W31: the start anchor (+174), the task-calendar uid (+178), the stored
 *      Milestone flag and the assignment dates (+52/+56/+60).  Evidence:
 *      corpus/probe/project_b-export.xml says Milestone=1 for PROJECT_B uid 10735
 *      (10 d) and 10799 (1 d), so duration == 0 is not the milestone test;
 *      holiday1.mpp's three tasks store task-calendar uid 3;
 *      manual-start-edited.mpp stores COM's new Start for uid 1 at +174 while
 *      +96 is stale; PROJECT_B has 72 links but Project exports 70 because two
 *      have dead endpoints.  All checks use uids/dates/counts only.
 *  11. W33: Task::start/finish are the EFFECTIVE +104/+108 pair.  PROJECT_B uid
 *      10805 is the one row where they differ from the calculated +96/+100;
 *      its effective dates equal Project's export and the calculated pair is
 *      exposed separately.
 *
 * DATA.  tests/data/synthetic-base.mpp is committed, because the reader has to
 * be exercised on a CI runner where corpus/ does not exist; see
 * tests/data/README.md for its provenance.  The corpus itself is read-only and
 * git-ignored, so corpus-dependent checks (PROJECT_A, PROJECT_B, the corpus/probe sweep)
 * print SKIP when it is absent.  Setting MPP_NO_CORPUS=1 forces that absent path
 * on a machine that does have the corpus, which is how the fallback is proved
 * without touching the client files.
 *
 * SAFETY.  This test must never abort into a modal dialog on a build machine:
 * CRT error reporting is redirected to stderr before anything else, output is
 * unbuffered, and main catches every exception and returns 2.
 */

#include <cstdlib>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "reader.h"

/* Optional: the CMake-configured test paths (tests/test_paths.h.in).  Present
 * only when a build puts the generated header on this target's include path; in
 * that case its TEST_DATA_DIR is the authoritative tests/data/ path.  Nothing
 * here requires it -- see test_data_dir() below for the fallback. */
#if __has_include("core_test_paths.h")
#include "core_test_paths.h"
#endif

namespace {

/* ---- SHA-256, for the hashed names in the golden file.  Straight from
 * FIPS 180-4; only the first 16 hex characters are used. */
class Sha256 {
  public:
    Sha256() { reset(); }

    void update(const void* data, std::size_t length) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < length; ++i) {
            buffer_[buffer_length_++] = bytes[i];
            if (buffer_length_ == 64) {
                compress();
                buffer_length_ = 0;
                bit_length_ += 512;
            }
        }
    }

    std::string hex_digest() {
        const std::uint64_t bits = bit_length_ + buffer_length_ * 8;
        const unsigned char pad = 0x80;
        update(&pad, 1);
        const unsigned char zero = 0;
        while (buffer_length_ != 56) {
            update(&zero, 1);
        }
        unsigned char length_bytes[8];
        for (int i = 0; i < 8; ++i) {
            length_bytes[7 - i] = static_cast<unsigned char>((bits >> (8 * i)) & 0xFFu);
        }
        update(length_bytes, 8);
        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (const std::uint32_t word : state_) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                out.push_back(digits[(word >> shift) & 0xFu]);
            }
        }
        return out;
    }

  private:
    static std::uint32_t rotr(std::uint32_t value, unsigned bits) {
        return (value >> bits) | (value << (32 - bits));
    }

    void reset() {
        state_[0] = 0x6a09e667u;
        state_[1] = 0xbb67ae85u;
        state_[2] = 0x3c6ef372u;
        state_[3] = 0xa54ff53au;
        state_[4] = 0x510e527fu;
        state_[5] = 0x9b05688cu;
        state_[6] = 0x1f83d9abu;
        state_[7] = 0x5be0cd19u;
        buffer_length_ = 0;
        bit_length_ = 0;
    }

    void compress() {
        static const std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
            0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
            0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
            0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
            0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
            0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
            0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
            0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
            0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(buffer_[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(buffer_[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(buffer_[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(buffer_[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 =
                rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 =
                rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + s1 + choose + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::uint32_t state_[8];
    unsigned char buffer_[64];
    std::size_t buffer_length_ = 0;
    std::uint64_t bit_length_ = 0;
};

std::string name_hash(const std::string& name) {
    Sha256 sha;
    sha.update(name.data(), name.size());
    return "sha256:" + sha.hex_digest().substr(0, 16);
}

class TestContext {
  public:
    void check(bool condition, const std::string& what) {
        if (condition) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << std::endl;
    }

    template <typename A, typename B>
    void equal(const A& actual, const B& expected, const std::string& what) {
        if (actual == expected) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (actual=" << actual
                  << ", expected=" << expected << ")" << std::endl;
    }

    int failures() const { return failures_; }
    int checks() const { return checks_; }

  private:
    int failures_ = 0;
    int checks_ = 0;
};

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (true) {
        const std::size_t tab = line.find('\t', begin);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(begin));
            return fields;
        }
        fields.push_back(line.substr(begin, tab - begin));
        begin = tab + 1;
    }
}

std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

bool file_exists(const std::string& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

/* ---- Where the committed test data lives. */
/* tests/data/ holds the one .mpp that may be committed (synthetic-base.mpp) and
 * the committed golden TSV.  MPP_GOLDEN_PATH is configured into this test by
 * core/mpp/CMakeLists.txt, so its parent directory is the reliable handle on
 * tests/data/ in every build tree -- no dependence on the working directory,
 * which is what makes the same binary work under ctest, NMake, Xcode and a
 * direct invocation.  A build that also wires tests/test_paths.h.in's
 * TEST_DATA_DIR into this target wins; see that file. */
std::filesystem::path test_data_dir() {
#ifdef TEST_DATA_DIR
    return TEST_DATA_DIR;
#else
    return std::filesystem::path(MPP_GOLDEN_PATH).parent_path();
#endif
}

/* Any non-empty value makes the corpus-dependent checks behave exactly as they
 * do when corpus/ is absent, so the committed-fixture path can be exercised on
 * a machine that has the corpus.  See tests/data/README.md for the command. */
std::string environment_value(const char* name) {
    /* std::getenv is ISO C++, but MSVC's CRT deprecates it (C4996, fatal under
     * /WX).  _dupenv_s is the sanctioned replacement and owns a malloc buffer;
     * guarded so GCC and AppleClang keep the portable call. */
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return std::string();
    }
    const std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

bool corpus_hidden() { return !environment_value("MPP_NO_CORPUS").empty(); }

const mpp::Task* find_task(const mpp::Document& document, std::uint32_t uid) {
    const auto found =
        std::find_if(document.tasks.begin(), document.tasks.end(),
                     [uid](const mpp::Task& task) { return task.uid == uid; });
    return found == document.tasks.end() ? nullptr : &*found;
}

bool has_warning(const mpp::Document& document, std::string_view needle) {
    return std::any_of(document.warnings.begin(), document.warnings.end(),
                       [needle](const std::string& warning) {
                           return warning.find(needle) != std::string::npos;
                       });
}

std::string describe(const mpp::Document& document) {
    std::string out = std::to_string(document.tasks.size()) + " tasks";
    if (!document.skipped_rows.empty()) {
        out += ", " + std::to_string(document.skipped_rows.size()) + " skipped rows";
    }
    if (!document.warnings.empty()) {
        out += ", " + std::to_string(document.warnings.size()) + " warnings";
    }
    return out;
}

/* ---- 1. Formatting, against spec/mpp-records.md §3's recorded examples. */
void test_formatting(TestContext& context) {
    const mpp::PackedDate packed_0800{0x3BF212C0u}; /* day 15346, 4800 ticks */
    const mpp::PackedDate packed_1700{0x3BF327D8u}; /* day 15347, 10200 ticks */
    const mpp::PackedDate packed_save{0x3D141482u}; /* day 15636, 5250 ticks */

    context.equal(mpp::format_date(packed_0800), std::string("2026-01-05 08:00"),
                  "0x3bf212c0 decodes to 2026-01-05 08:00");
    context.equal(mpp::format_date(packed_1700), std::string("2026-01-06 17:00"),
                  "0x3bf327d8 decodes to 2026-01-06 17:00");
    context.equal(mpp::format_date(packed_save), std::string("2026-10-22 08:45"),
                  "0x3d141482 decodes to 2026-10-22 08:45");
    context.equal(mpp::format_date_iso(packed_0800),
                  std::string("2026-01-05 08:00:00+00:00"),
                  "the ISO form matches MS Project's export style");
    context.check(!mpp::PackedDate{0}.valid(), "packed date 0 is not a date");
    context.check(!mpp::PackedDate{0xFFFFFFFFu}.valid(),
                  "packed date 0xffffffff is not a date");
    context.equal(mpp::format_date(mpp::PackedDate{0}), std::string(),
                  "an invalid date formats as an empty string");
    context.equal(packed_0800.day(), 15346, "day index of 0x3bf212c0");
    context.equal(packed_0800.seconds_of_day(), 4800 * 6,
                  "seconds of day of 0x3bf212c0");

    context.equal(mpp::format_duration_days(0), std::string("0"), "0 ticks is 0 days");
    context.equal(mpp::format_duration_days(4800), std::string("1"), "4800 ticks is 1 day");
    context.equal(mpp::format_duration_days(24000), std::string("5"),
                  "24000 ticks is 5 days");
    context.equal(mpp::format_duration_days(7200), std::string("1.5"),
                  "7200 ticks is 1.5 days");
    context.equal(mpp::duration_minutes(571200), std::uint32_t{57120},
                  "571200 ticks is 57,120 minutes, the unit COM reports");
    context.equal(mpp::duration_minutes(4800), std::uint32_t{480},
                  "4800 ticks is 480 minutes");
}

/* ---- The golden comparison: MS Project's own export vs the reader. */
struct GoldenRow {
    std::uint32_t uid = 0;
    std::uint32_t id = 0;
    std::string name_hash;
    std::uint32_t duration_min = 0;
    std::string start;
    std::string finish;
    std::uint32_t priority = 0;
    std::uint32_t percent = 0;
};

std::vector<GoldenRow> read_golden(const std::string& path, TestContext& context) {
    std::vector<GoldenRow> rows;
    const std::vector<std::string> lines = read_lines(path);
    if (lines.empty()) {
        context.check(false, std::string("golden file is unreadable: ") + path);
        return rows;
    }
    for (std::size_t index = 1; index < lines.size(); ++index) {
        const std::vector<std::string> fields = split_tabs(lines[index]);
        if (fields.size() < 8) {
            context.check(false, "golden line " + std::to_string(index + 1) + " has " +
                                     std::to_string(fields.size()) +
                                     " fields, expected 8");
            continue;
        }
        GoldenRow row;
        row.uid = static_cast<std::uint32_t>(std::stoul(fields[0]));
        row.id = static_cast<std::uint32_t>(std::stoul(fields[1]));
        row.name_hash = fields[2];
        row.duration_min = static_cast<std::uint32_t>(std::stoul(fields[3]));
        row.start = fields[4];
        row.finish = fields[5];
        row.priority = static_cast<std::uint32_t>(std::stoul(fields[6]));
        row.percent = static_cast<std::uint32_t>(std::stoul(fields[7]));
        rows.push_back(std::move(row));
    }
    return rows;
}

void compare_against_golden(TestContext& context, const mpp::Document& document) {
    const std::vector<GoldenRow> golden = read_golden(MPP_GOLDEN_PATH, context);
    context.check(!golden.empty(), "golden file holds rows");
    context.equal(document.tasks.size(), golden.size(),
                  "live task count matches MS Project's export");

    const std::vector<const char*> fields = {"missing", "id",      "name",
                                             "duration", "start", "finish",
                                             "priority", "pct"};
    std::map<std::string, std::size_t> mismatches;
    for (const char* field : fields) {
        mismatches[field] = 0;
    }
    std::vector<std::string> details;
    std::set<std::uint32_t> golden_uids;

    for (const GoldenRow& row : golden) {
        golden_uids.insert(row.uid);
        const mpp::Task* task = find_task(document, row.uid);
        if (task == nullptr) {
            ++mismatches["missing"];
            if (details.size() < 5) {
                details.push_back("uid " + std::to_string(row.uid) +
                                  ": missing from the reader's task list");
            }
            continue;
        }
        const auto report = [&](const char* field, const std::string& actual,
                                const std::string& expected) {
            ++mismatches[field];
            if (details.size() < 5) {
                details.push_back(std::string(field) + " uid " + std::to_string(row.uid) +
                                  ": actual=" + actual + " expected=" + expected);
            }
        };
        if (task->row_id != row.id) {
            report("id", std::to_string(task->row_id), std::to_string(row.id));
        }
        const std::string hash = name_hash(task->name);
        if (hash != row.name_hash) {
            report("name", hash + " ('" + task->name + "')", row.name_hash);
        }
        if (task->duration_minutes() != row.duration_min) {
            report("duration", std::to_string(task->duration_minutes()),
                   std::to_string(row.duration_min));
        }
        const std::string start = mpp::format_date_iso(task->start);
        if (start != row.start) {
            report("start", start, row.start);
        }
        const std::string finish = mpp::format_date_iso(task->finish);
        if (finish != row.finish) {
            report("finish", finish, row.finish);
        }
        if (task->priority != row.priority) {
            report("priority", std::to_string(task->priority),
                   std::to_string(row.priority));
        }
        if (task->percent_complete != row.percent) {
            report("pct", std::to_string(task->percent_complete),
                   std::to_string(row.percent));
        }
    }

    std::size_t extra = 0;
    for (const mpp::Task& task : document.tasks) {
        if (golden_uids.count(task.uid) == 0) {
            ++extra;
            if (details.size() < 5) {
                details.push_back("uid " + std::to_string(task.uid) +
                                  ": reported by the reader but not by MS Project");
            }
        }
    }

    std::size_t total = extra;
    std::cout << "golden comparison: " << golden.size() << " oracle rows vs "
              << document.tasks.size() << " reader rows\n";
    std::cout << "  field         mismatches\n";
    for (const char* field : fields) {
        const std::string name(field);
        std::cout << "  " << name << std::string(14 - name.size(), ' ')
                  << mismatches[field] << '\n';
        total += mismatches[field];
    }
    std::cout << "  extra         " << extra << "\n";
    if (details.empty()) {
        std::cout << "  no mismatches" << std::endl;
    } else {
        for (const std::string& detail : details) {
            std::cout << "  MISMATCH " << detail << '\n';
        }
        std::cout << std::flush;
    }
    context.equal(total, std::size_t{0},
                  "every golden field matches MS Project (see the table above)");
}

/* ---- 2. The committed synthetic fixture: the reader with no corpus/ at all. */
/* corpus/probe/base.mpp was generated by the coordinator driving MS Project over
 * COM (3 tasks, project row), and the identical bytes are committed as
 * tests/data/synthetic-base.mpp.  This is the only check that can run on a clean
 * CI checkout, so it asserts concrete decoded values, not just "it loaded". */
void test_synthetic_fixture(TestContext& context) {
    const std::filesystem::path directory = test_data_dir();
    const std::string path = (directory / "synthetic-base.mpp").string();
    if (!file_exists(path)) {
        /* The golden TSV is committed in the same directory.  Both present is a
         * normal checkout; neither present is a tree without tests/data/, where
         * skipping is right.  One without the other is a broken checkout -- most
         * likely the fixture was never `git add`ed -- and must not hide behind a
         * SKIP, because CI would then go green without exercising the reader. */
        const std::string golden = (directory / "project_a_golden.tsv").string();
        if (file_exists(golden)) {
            context.check(false, "committed fixture " + path +
                                     " is missing although " + golden + " is present");
            return;
        }
        std::cout << "SKIP: " << path << " is not present (this tree has no tests/data/)\n";
        return;
    }
    const mpp::Document document = mpp::load_file(path);
    std::cout << "SYNTHETIC: " << describe(document) << '\n' << std::flush;

    context.equal(document.tasks.size(), std::size_t{3},
                  "synthetic-base.mpp has 3 live tasks");
    context.check(document.skipped_rows.empty(),
                  "synthetic-base.mpp has no deleted rows");
    context.check(document.warnings.empty(),
                  "synthetic-base.mpp parses without warnings");
    context.equal(document.project.uid, std::uint32_t{0},
                  "synthetic-base.mpp project row uid");
    context.equal(document.project.title, std::string("base"),
                  "synthetic-base.mpp project title");
    context.equal(document.project.duration_ticks, std::uint32_t{21600},
                  "synthetic-base.mpp project duration ticks");

    struct ExpectedTask {
        std::uint32_t uid;
        const char* name;
        const char* days;
        std::uint32_t duration_min;
        const char* start;
        const char* finish;
    };
    /* The EFFECTIVE pair (+104/+108), which is what COM reported for
     * corpus/probe/base.mpp: its three manual tasks start at the date-only
     * project start (2026-01-04 13:00 = 2026-01-05 00:00 Sydney) and finish at
     * the same wall-clock instants.  The calculated pair (+96/+100) is
     * asserted separately below -- it still stores 08:00/17:00 scheduled times. */
    const ExpectedTask expected[] = {
        {1, "Task A", "5", 2400, "2026-01-04 13:00", "2026-01-09 12:00"},
        {2, "Task B", "3", 1440, "2026-01-04 13:00", "2026-01-07 12:00"},
        {3, "Task C", "2", 960, "2026-01-04 13:00", "2026-01-06 12:00"},
    };
    for (const ExpectedTask& row : expected) {
        const std::string uid = std::to_string(row.uid);
        const mpp::Task* task = find_task(document, row.uid);
        context.check(task != nullptr,
                      "synthetic-base.mpp has uid " + uid);
        if (task == nullptr) {
            continue;
        }
        context.equal(task->row_id, row.uid, "synthetic base uid " + uid + " ID column");
        context.equal(task->name, std::string(row.name),
                      "synthetic base uid " + uid + " name");
        context.equal(mpp::format_duration_days(task->duration_ticks),
                      std::string(row.days),
                      "synthetic base uid " + uid + " duration in working days");
        context.equal(task->duration_minutes(), row.duration_min,
                      "synthetic base uid " + uid + " duration in minutes");
        context.equal(mpp::format_date(task->start), std::string(row.start),
                      "synthetic base uid " + uid + " start");
        context.equal(mpp::format_date(task->finish), std::string(row.finish),
                      "synthetic base uid " + uid + " finish");
        /* W33: the calculated +96/+100 copy stays separate.  For these manual
         * tasks it still holds the 08:00/17:00 scheduled times while the
         * effective start is the date-only 13:00 UTC value. */
        context.equal(mpp::format_date(task->calculated_start),
                      std::string("2026-01-05 08:00"),
                      "synthetic base uid " + uid +
                          " calculated Start stays at +96 (08:00)");
        context.equal(static_cast<unsigned>(task->priority), 500u,
                      "synthetic base uid " + uid + " priority");
        context.equal(static_cast<unsigned>(task->percent_complete), 0u,
                      "synthetic base uid " + uid + " percent complete");
        /* ---- W19: base.mpp is all-ASAP, has no deadline and no progress.
         * The committed fixture is the only one CI always has, so it is also
         * the check that the new fields default to NA rather than to garbage. */
        context.equal(static_cast<unsigned>(task->constraint_type), 0u,
                      "synthetic base uid " + uid + " ConstraintType is ASAP (+64)");
        context.check(!task->constraint_date.valid(),
                      "synthetic base uid " + uid + " has no ConstraintDate (+66)");
        context.check(!task->deadline.valid(),
                      "synthetic base uid " + uid + " has no Deadline (+182)");
        context.check(!task->actual_start.valid(),
                      "synthetic base uid " + uid + " has no ActualStart (+120)");
        context.check(!task->actual_finish.valid(),
                      "synthetic base uid " + uid + " has no ActualFinish (+124)");
        context.equal(task->actual_duration_ticks, std::uint32_t{0},
                      "synthetic base uid " + uid + " ActualDuration is 0 (+80)");
    }

    std::set<std::uint32_t> uids;
    for (const mpp::Task& task : document.tasks) {
        uids.insert(task.uid);
    }
    context.equal(uids.size(), document.tasks.size(),
                  "synthetic-base.mpp uids are unique");

    /* ---- W18: outline and status, on the one fixture CI always has.
     * base.mpp is a flat project of three *manually scheduled* tasks (COM:
     * Manual=true, Critical=false for all three, docs/analysis/oracle/
     * writer-base.json), all at OutlineLevel 1, none a summary or a milestone.
     * Task A has zero slack but is not critical -- the manual-task exception. */
    context.equal(mpp::format_date(document.project.start),
                  std::string("2026-01-04 13:00"),
                  "synthetic base project start is the stored UTC instant of the "
                  "2026-01-05 date-only project start");
    context.equal(mpp::format_date(document.project.finish),
                  std::string("2026-01-09 12:00"),
                  "synthetic base project finish is the stored value");
    context.equal(document.project.start.raw, std::uint32_t{0x3BF11E78u},
                  "synthetic base project start bytes (+96 of row 0)");
    context.equal(document.project.finish.raw, std::uint32_t{0x3BF61C20u},
                  "synthetic base project finish bytes (+100 of row 0)");
    for (const mpp::Task& task : document.tasks) {
        const std::string uid = std::to_string(task.uid);
        context.equal(task.outline_level, std::uint32_t{1},
                      "base uid " + uid + " has OutlineLevel 1");
        context.check(!task.summary, "base uid " + uid + " is not a summary");
        context.equal(task.child_count, std::uint32_t{0},
                      "base uid " + uid + " has no children");
        context.check(!task.is_milestone(), "base uid " + uid + " is not a milestone");
        context.equal(task.milestone_stored, true,
                      "base uid " + uid +
                          " reads Milestone from FixedMeta (stored, not duration)");
        context.check(!task.critical,
                      "base uid " + uid + " is not critical (manually scheduled)");
        context.check(task.manually_scheduled,
                      "base uid " + uid + " is manually scheduled (COM Manual=true)");
    }
    const mpp::Task* task_a = find_task(document, 1);
    context.check(task_a != nullptr, "base has uid 1");
    if (task_a != nullptr) {
        context.equal(task_a->outline_number, std::string("1"),
                      "base uid 1 OutlineNumber");
        context.equal(task_a->total_slack_ticks, std::int32_t{0},
                      "base uid 1 stored TotalSlack is 0 (+190)");
        /* W31: a manually scheduled task's +174 is its manual Start.  base's
         * project start is a date-only property, so the stored value is the UTC
         * encoding 2026-01-04 13:00 (= 2026-01-05 00:00 Sydney). */
        context.equal(mpp::format_date_iso(task_a->start_anchor),
                      std::string("2026-01-04 13:00:00+00:00"),
                      "base uid 1's +174 is its manual Start, not its 08:00 Start");
        context.equal(task_a->calendar_uid, std::int32_t{-1},
                      "base uid 1 names no task calendar (+178 is -1)");
    }
    const mpp::Task* task_b = find_task(document, 2);
    if (task_b != nullptr) {
        context.equal(task_b->outline_number, std::string("2"),
                      "base uid 2 OutlineNumber");
        context.equal(task_b->total_slack_ticks, std::int32_t{9600},
                      "base uid 2 stored TotalSlack is 9600 ticks (2 days)");
    }
    const mpp::Task* task_c = find_task(document, 3);
    if (task_c != nullptr) {
        context.equal(task_c->outline_number, std::string("3"),
                      "base uid 3 OutlineNumber");
        context.equal(task_c->total_slack_ticks, std::int32_t{14400},
                      "base uid 3 stored TotalSlack is 14400 ticks (3 days)");
    }
}

/* ---- 3 and 4. PROJECT_A: golden comparison, dead row, documented damage. */
void test_project_a(TestContext& context) {
    if (corpus_hidden()) {
        std::cout << "SKIP: PROJECT_A golden checks (MPP_NO_CORPUS is set: corpus forced absent)\n";
        return;
    }
    if (!file_exists(MPP_PROJECT_A_PATH)) {
        std::cout << "SKIP: " << MPP_PROJECT_A_PATH << " is not present (corpus is git-ignored)\n";
        return;
    }
    const mpp::Document document = mpp::load_file(MPP_PROJECT_A_PATH);
    std::cout << "PROJECT_A: " << describe(document) << '\n' << std::flush;

    context.equal(document.tasks.size(), std::size_t{83},
                  "PROJECT_A has 83 live tasks (85 FixedData rows - project row - dead row)");
    context.equal(document.skipped_rows.size(), std::size_t{1},
                  "PROJECT_A has exactly one row that is not a live task");
    if (!document.skipped_rows.empty()) {
        const mpp::SkippedRow& skipped = document.skipped_rows.front();
        context.equal(skipped.row, std::uint32_t{67}, "the dead row is row 67");
        context.equal(skipped.uid, std::uint32_t{10856}, "the dead row's uid is 10856");
        context.equal(skipped.orphan_name,
                      std::string("client-text-014)"),
                      "the dead slot's orphaned name block is reported as evidence");
    }

    context.equal(document.project.uid, std::uint32_t{0}, "PROJECT_A project row uid");
    // No client project title is committed; the decode is covered by the
    // synthetic fixtures above.
    context.equal(document.project.duration_ticks, std::uint32_t{1041600},
                  "PROJECT_A project duration ticks");
    context.equal(mpp::format_date(document.project.start),
                  std::string("2026-01-01 08:00"), "PROJECT_A project start");
    context.equal(mpp::format_date(document.project.finish),
                  std::string("2026-10-30 17:00"), "PROJECT_A project finish");

    compare_against_golden(context, document);

    /* Unique ids, sane percentages, valid dates, present names. */
    std::set<std::uint32_t> uids;
    bool all_nonzero = true;
    bool dates_valid = true;
    bool percent_sane = true;
    bool names_present = true;
    for (const mpp::Task& task : document.tasks) {
        uids.insert(task.uid);
        all_nonzero = all_nonzero && task.uid != 0;
        dates_valid = dates_valid && task.start.valid() && task.finish.valid();
        percent_sane = percent_sane && task.percent_complete <= 100;
        names_present = names_present && !task.name.empty();
    }
    context.equal(uids.size(), document.tasks.size(), "PROJECT_A uids are unique");
    context.check(all_nonzero, "no live PROJECT_A task has UniqueID 0");
    context.check(dates_valid, "every live PROJECT_A task has a valid packed start and finish");
    context.check(percent_sane,
                  "PercentComplete is a percentage (u16 at +92), not the u32 at +92");
    context.check(names_present, "every live PROJECT_A task has a name");

    /* ---- W19: the scheduling inputs on the real programme.  No oracle export
     * carried PROJECT_A's ConstraintType/Deadline/Actual* columns, so this is a
     * structural check against the bytes: 13 tasks hold a non-ASAP constraint
     * (12 SNET + 1 FNET), and the actual-date block follows percent complete --
     * 19 tasks are at 100 % and all 19 store an ActualFinish, while every task
     * at 99 % or less stores NA.  (The brief said "19 tasks held by
     * constraints"; the stored type says 13, which is the measured number.) */
    std::size_t constrained = 0;
    std::size_t with_constraint_date = 0;
    std::size_t with_actual_start = 0;
    std::size_t with_actual_finish = 0;
    std::size_t complete = 0;
    std::size_t actual_finish_mismatch = 0;
    for (const mpp::Task& task : document.tasks) {
        if (task.constraint_type != 0) {
            ++constrained;
            with_constraint_date += task.constraint_date.valid() ? 1u : 0u;
        }
        with_actual_start += task.actual_start.valid() ? 1u : 0u;
        with_actual_finish += task.actual_finish.valid() ? 1u : 0u;
        if (task.percent_complete == 100) {
            ++complete;
            if (!task.actual_finish.valid()) {
                ++actual_finish_mismatch;
            }
        } else if (task.actual_finish.valid()) {
            ++actual_finish_mismatch;
        }
    }
    context.equal(constrained, std::size_t{13},
                  "PROJECT_A has 13 tasks with a non-ASAP ConstraintType (+64)");
    context.equal(with_constraint_date, constrained,
                  "every constrained PROJECT_A task stores a ConstraintDate (+66)");
    context.equal(with_actual_start, std::size_t{33},
                  "33 PROJECT_A tasks store an ActualStart (+120)");
    context.equal(complete, std::size_t{19}, "19 PROJECT_A tasks are 100 % complete");
    context.equal(with_actual_finish, complete,
                  "ActualFinish (+124) is set on exactly the completed tasks");
    context.equal(actual_finish_mismatch, std::size_t{0},
                  "no PROJECT_A task below 100 % stores an ActualFinish");

    /* PROJECT_A row 1 is stored as "Site establishment " (trailing space); COM trims it.
     * The display name must match the oracle, the raw block must stay verbatim
     * for a writer's round trip. */
    const mpp::Task* spaced = find_task(document, 8141);
    context.check(spaced != nullptr, "uid 8141 is present");
    if (spaced != nullptr) {
        context.equal(spaced->name_raw, std::string("Site establishment "),
                      "uid 8141 keeps the stored trailing space in name_raw");
        context.equal(spaced->name, std::string("Site establishment"),
                      "uid 8141's display name is trimmed like MS Project's");
    }

    const mpp::Task* named = find_task(document, 10733);
    context.check(named != nullptr, "uid 10733 is present");
    if (named != nullptr) {
        context.equal(named->name,
                      std::string("client-text-003"),
                      "uid 10733 name via key 0x0b40000e");
        context.equal(named->duration_minutes(), std::uint32_t{16800},
                      "uid 10733 duration in minutes");
    }

    /* PROJECT_A's documented damage must be reported, not crash or be ignored. */
    context.check(has_warning(document, "declares 1076 entries"),
                  "warns that VarMeta declares 1,076 entries but holds 1,081");
    context.check(has_warning(document, "declares 87 entries"),
                  "warns that FixedMeta declares 87 entries but holds 88");
    context.check(has_warning(document, "treated as a deleted row"),
                  "warns that row 67 is a deleted row slot, not a task");
    context.check(has_warning(document, "101 entries whose Var2Data offset"),
                  "warns about the 101 VarMeta entries with a 0xffffffff offset");
    context.check(has_warning(document, "repeats"),
                  "warns about the repeated (uid, key) pairs");

    /* ---- W31: the two extra fixed-row fields (+174 start anchor, +178 task
     * calendar) and the stored milestone flag.  PROJECT_A has 10 zero-day tasks and
     * the FixedMeta flag agrees with every one of them (0 non-zero-duration
     * milestones here, unlike PROJECT_B); every row is covered by FixedMeta, so
     * all 83 milestones are stored rather than derived, and every task names
     * no calendar (-1), which is why PROJECT_A correctly uses the project calendar. */
    std::size_t project_a_milestones = 0;
    std::size_t project_a_milestone_stored = 0;
    std::size_t project_a_named_calendar = 0;
    for (const mpp::Task& task : document.tasks) {
        project_a_milestones += task.milestone ? 1u : 0u;
        project_a_milestone_stored += task.milestone_stored ? 1u : 0u;
        project_a_named_calendar += task.calendar_uid != -1 ? 1u : 0u;
    }
    context.equal(project_a_milestones, std::size_t{10},
                  "PROJECT_A has 10 milestones (stored flag; all are 0-day here)");
    context.equal(project_a_milestone_stored, document.tasks.size(),
                  "every PROJECT_A task reads its milestone from FixedMeta, not duration");
    context.equal(project_a_named_calendar, std::size_t{0},
                  "no PROJECT_A task names a task calendar (+178 is -1)");
    const mpp::Task* anchored = find_task(document, 10759);
    context.check(anchored != nullptr, "PROJECT_A uid 10759 is present");
    if (anchored != nullptr) {
        context.equal(mpp::format_date_iso(anchored->start_anchor),
                      std::string("2026-08-10 17:00:00+00:00"),
                      "uid 10759's +174 is its predecessor's 17:00 finish");
        context.equal(mpp::format_date_iso(anchored->start),
                      std::string("2026-08-11 08:00:00+00:00"),
                      "uid 10759's Start (+96) is the next-morning alignment, "
                      "not the +174 anchor");
    }
    const mpp::Task* same_day = find_task(document, 10742);
    context.check(same_day != nullptr, "PROJECT_A uid 10742 is present");
    if (same_day != nullptr) {
        context.equal(mpp::format_date_iso(same_day->start_anchor),
                      std::string("2026-07-07 17:00:00+00:00"),
                      "uid 10742's +174 is its own 17:00");
        context.check(same_day->start_anchor.raw == same_day->start.raw &&
                          same_day->start.raw == same_day->finish.raw,
                      "uid 10742 is a 17:00 milestone: anchor == Start == Finish");
    }

    /* ---- W18: the outline, derived from Fixed2Data's parent links.  COM's
     * OutlineLevel was not exported for PROJECT_A, so this is a structural check plus
     * the counts the file itself pins down: 2 top-level tasks, 16 summaries
     * (Fixed2Data's has-children bit and the parent links agree on all 85 rows),
     * levels 1..4 only, and an outline number whose depth always matches its
     * level.  It is also the check that a damaged table cannot quietly turn into
     * a flat outline: a flat fallback would give 83 level-1 tasks. */
    std::map<std::uint32_t, std::size_t> by_level;
    std::set<std::string> outline_numbers;
    std::size_t summaries = 0;
    bool depth_matches = true;
    bool numbers_present = true;
    for (const mpp::Task& task : document.tasks) {
        by_level[task.outline_level] += 1;
        if (task.summary) {
            ++summaries;
        }
        const auto dots = static_cast<std::size_t>(std::count(
            task.outline_number.begin(), task.outline_number.end(), '.'));
        depth_matches = depth_matches && dots + 1 == task.outline_level;
        numbers_present = numbers_present && !task.outline_number.empty();
        outline_numbers.insert(task.outline_number);
    }
    context.equal(by_level[1], std::size_t{2}, "PROJECT_A has 2 top-level tasks");
    context.equal(by_level[2], std::size_t{7}, "PROJECT_A has 7 level-2 tasks");
    context.equal(by_level[3], std::size_t{23}, "PROJECT_A has 23 level-3 tasks");
    context.equal(by_level[4], std::size_t{51}, "PROJECT_A has 51 level-4 tasks");
    context.equal(by_level.size(), std::size_t{4},
                  "no PROJECT_A task is outside outline levels 1..4");
    context.equal(summaries, std::size_t{16},
                  "PROJECT_A's outline has 16 summaries (has-children rows minus the "
                  "project row)");
    context.equal(outline_numbers.size(), document.tasks.size(),
                  "PROJECT_A outline numbers are unique");
    context.check(depth_matches,
                  "every PROJECT_A outline number has as many parts as its level");
    context.check(numbers_present, "every PROJECT_A task has an outline number");
    context.check(!has_warning(document, "not reachable from the project row"),
                  "no live PROJECT_A row is orphaned from the outline tree");
}

/* ---- 5. PROJECT_B: 86 rows, 73 live tasks, 12 deleted slots. */
void test_project_b(TestContext& context) {
    if (corpus_hidden()) {
        std::cout << "SKIP: PROJECT_B checks (MPP_NO_CORPUS is set: corpus forced absent)\n";
        return;
    }
    if (!file_exists(MPP_PROJECT_B_PATH)) {
        std::cout << "SKIP: " << MPP_PROJECT_B_PATH << " is not present\n";
        return;
    }
    const mpp::Document document = mpp::load_file(MPP_PROJECT_B_PATH);
    std::cout << "PROJECT_B: " << describe(document) << '\n' << std::flush;
    context.equal(document.tasks.size(), std::size_t{73},
                  "PROJECT_B has 73 live tasks (86 FixedData rows - project - 12 deleted)");
    context.equal(document.skipped_rows.size(), std::size_t{12},
                  "PROJECT_B's 12 rows with no VarMeta group are deleted slots");
    context.equal(document.project.uid, std::uint32_t{0}, "PROJECT_B project row uid");
    // No client project title is committed; the decode is covered by the
    // synthetic fixtures above.
    const std::ptrdiff_t empty_names =
        std::count_if(document.tasks.begin(), document.tasks.end(),
                      [](const mpp::Task& task) { return task.name.empty(); });
    context.equal(empty_names, std::ptrdiff_t{0},
                  "no live PROJECT_B task has an empty name");
    context.check(document.skipped_rows.front().row == 1 &&
                      document.skipped_rows.front().uid == 8141,
                  "PROJECT_B's first deleted slot is row 1 (uid 8141)");
    context.check(!has_warning(document, "declares 1436 entries"),
                  "PROJECT_B's VarMeta header count matches the stream");

    /* ---- W18: PROJECT_B's 12 deleted rows sit at rows 1..12 under live rows 3 and
     * 5, and they keep Fixed2Data records with parent links.  If they were
     * numbered, the first live task would start at "6"; numbering live rows only
     * is what makes the live outline start at "1" (both counts are measured: 10
     * top-level tasks, 63 at level 2). */
    std::size_t top_level = 0;
    std::size_t level_two = 0;
    for (const mpp::Task& task : document.tasks) {
        if (task.outline_level == 1) {
            ++top_level;
        } else if (task.outline_level == 2) {
            ++level_two;
        }
    }
    context.equal(top_level, std::size_t{10}, "PROJECT_B has 10 top-level tasks");
    context.equal(level_two, std::size_t{63}, "PROJECT_B has 63 level-2 tasks");
    context.equal(top_level + level_two, document.tasks.size(),
                  "every PROJECT_B task is at outline level 1 or 2");
    const mpp::Task* project_b_first = find_task(document, 10733);
    context.check(project_b_first != nullptr, "PROJECT_B has uid 10733");
    if (project_b_first != nullptr) {
        context.equal(project_b_first->outline_number, std::string("1"),
                      "PROJECT_B's first live task is numbered 1, not 6: deleted "
                      "slots do not consume outline numbers");
    }

    /* W19: PROJECT_B has six rows holding an SNET constraint (type 4), but five
     * are the deleted slots at rows 2, 6..9 -- only row 14 (uid 10734) is a
     * live task.  The +64 field is per row, including dead rows. */
    std::size_t constrained = 0;
    for (const mpp::Task& task : document.tasks) {
        if (task.constraint_type != 0) {
            ++constrained;
        }
    }
    context.equal(constrained, std::size_t{1},
                  "PROJECT_B has 1 live task with a non-ASAP ConstraintType (+64); "
                  "five more sit on deleted row slots and are not returned");
    const mpp::Task* constrained_task = find_task(document, 10734);
    context.check(constrained_task != nullptr && constrained_task->constraint_type == 4 &&
                      constrained_task->constraint_date.valid(),
                  "PROJECT_B uid 10734 is the live SNET task with a stored date");

    /* ---- W31: Milestone is a stored flag, not "duration == 0".  Project's
     * own MSPDI export (corpus/probe/project_b-export.xml) says Milestone=1 on 10
     * PROJECT_B tasks: 8 are zero-day and two are not -- uid 10735 is 10 days and
     * uid 10799 is 1 day.  The duration test alone would miss exactly those
     * two, so the file's FixedMeta flag is the source of truth (all 73 live rows
     * are covered by FixedMeta, so none uses the fallback). */
    std::size_t project_b_milestones = 0;
    std::size_t project_b_stored = 0;
    std::size_t project_b_long_milestones = 0;
    std::size_t project_b_named_calendar = 0;
    for (const mpp::Task& task : document.tasks) {
        project_b_milestones += task.milestone ? 1u : 0u;
        project_b_stored += task.milestone_stored ? 1u : 0u;
        project_b_long_milestones += (task.milestone && task.duration_ticks != 0) ? 1u : 0u;
        project_b_named_calendar += task.calendar_uid != -1 ? 1u : 0u;
    }
    context.equal(project_b_milestones, std::size_t{10},
                  "PROJECT_B has 10 stored milestones (export says Milestone=1 on 10)");
    context.equal(project_b_stored, document.tasks.size(),
                  "every PROJECT_B milestone flag comes from FixedMeta, not duration");
    context.equal(project_b_long_milestones, std::size_t{2},
                  "2 PROJECT_B milestones have a non-zero duration, so the old "
                  "duration == 0 rule was wrong");
    context.equal(project_b_named_calendar, std::size_t{0},
                  "no PROJECT_B task names a task calendar (+178 is -1)");
    const mpp::Task* long_milestone = find_task(document, 10735);
    context.check(long_milestone != nullptr, "PROJECT_B uid 10735 is present");
    if (long_milestone != nullptr) {
        context.check(long_milestone->is_milestone(),
                      "PROJECT_B uid 10735 is a milestone although it is 10 days long");
        context.equal(long_milestone->duration_minutes(), std::uint32_t{4800},
                      "PROJECT_B uid 10735's stored duration is still 4,800 minutes");
    }
    const mpp::Task* day_milestone = find_task(document, 10799);
    context.check(day_milestone != nullptr && day_milestone->is_milestone() &&
                      day_milestone->duration_minutes() == 480,
                  "PROJECT_B uid 10799 is a 1-day milestone (MSPDI Milestone=1)");

    /* ---- W31/W33: PROJECT_B is NOT fully calculated.  Its row for uid 10805 is
     * the one corpus row where the calculated and effective date pairs differ:
     * +96/+100 still hold 2026-10-15 17:00 while +104/+108 hold 2026-03-23
     * 08:00, which is exactly what Project's MSPDI export reports.  The reader
     * exposes start/finish from +104/+108 (and the stale calculated pair
     * separately), and the +174 anchor already holds the same 03-23 08:00. */
    const mpp::Task* effective = find_task(document, 10805);
    context.check(effective != nullptr, "PROJECT_B uid 10805 is present");
    if (effective != nullptr) {
        context.equal(mpp::format_date_iso(effective->start),
                      std::string("2026-03-23 08:00:00+00:00"),
                      "PROJECT_B uid 10805's Start is the effective +104 value that "
                      "Project's export reports");
        context.equal(mpp::format_date_iso(effective->finish),
                      std::string("2026-03-23 08:00:00+00:00"),
                      "PROJECT_B uid 10805's Finish is the effective +108 value");
        context.equal(mpp::format_date_iso(effective->calculated_start),
                      std::string("2026-10-15 17:00:00+00:00"),
                      "PROJECT_B uid 10805 keeps the stale calculated +96 Start separately");
        context.equal(mpp::format_date_iso(effective->start_anchor),
                      std::string("2026-03-23 08:00:00+00:00"),
                      "PROJECT_B uid 10805's +174 anchor is the recalculated 03-23 08:00");
    }
}

/* ---- 7. W18: the outline fixture, against docs/analysis/oracle/outline-probe.json.
 *
 * corpus/probe/outline1.mpp: S1 is a summary at OutlineLevel 1 with children C1
 * (2 d) and C2 (1 d, a week later) at OutlineLevel 2.  COM reported
 * OutlineLevel 1/2/2, Summary true only for S1, Critical true for S1 and C2
 * (TotalSlack 0) and false for C1 (1920 min), OutlineNumber 1/1.1/1.2 and no
 * milestone.  The reader derives the level from Fixed2Data's stored parent
 * links (see reader.cpp), so this test is the one that proves the derivation
 * against Project's own numbers. */
void test_outline_fixture(TestContext& context) {
    const std::string directory = MPP_PROBE_DIR;
    if (corpus_hidden()) {
        std::cout << "SKIP: outline1.mpp checks (MPP_NO_CORPUS is set)\n";
        return;
    }
    const std::string path = directory + "/outline1.mpp";
    if (!file_exists(path)) {
        std::cout << "SKIP: " << path << " is not present\n";
        return;
    }
    const mpp::Document document = mpp::load_file(path);
    std::cout << "OUTLINE1: " << describe(document) << '\n' << std::flush;

    context.equal(document.tasks.size(), std::size_t{3}, "outline1 has 3 tasks");
    context.check(document.skipped_rows.empty(), "outline1 has no deleted rows");
    context.check(document.warnings.empty(), "outline1 parses without warnings");
    context.equal(document.project.title, std::string("outline1"),
                  "outline1 project title");
    /* Row 0 carries the summary row's dates: the earliest task start and the
     * latest task finish, exactly what COM reports for S1 (outline-probe.json). */
    context.equal(mpp::format_date_iso(document.project.start),
                  std::string("2026-10-19 08:00:00+00:00"),
                  "outline1 project start is the summary row's +96");
    context.equal(mpp::format_date_iso(document.project.finish),
                  std::string("2026-10-26 17:00:00+00:00"),
                  "outline1 project finish is the summary row's +100");
    context.equal(mpp::duration_minutes(document.project.duration_ticks),
                  std::uint32_t{2880}, "outline1 project duration in minutes");

    struct Expected {
        std::uint32_t uid;
        const char* name;
        std::uint32_t level;
        const char* number;
        bool summary;
        std::uint32_t children;
        bool critical;
        std::uint32_t slack_min;
        const char* start;
        const char* finish;
    };
    /* Values from docs/analysis/oracle/outline-probe.json (COM). */
    const Expected expected[] = {
        {1, "S1", 1, "1", true, 2, true, 0, "2026-10-19 08:00:00+00:00",
         "2026-10-26 17:00:00+00:00"},
        {2, "C1", 2, "1.1", false, 0, false, 1920, "2026-10-19 08:00:00+00:00",
         "2026-10-20 17:00:00+00:00"},
        {3, "C2", 2, "1.2", false, 0, true, 0, "2026-10-26 08:00:00+00:00",
         "2026-10-26 17:00:00+00:00"},
    };
    for (const Expected& row : expected) {
        const std::string uid = std::to_string(row.uid);
        const mpp::Task* task = find_task(document, row.uid);
        context.check(task != nullptr, "outline1 has uid " + uid);
        if (task == nullptr) {
            continue;
        }
        context.equal(task->name, std::string(row.name), "outline1 uid " + uid + " name");
        context.equal(task->outline_level, row.level,
                      "outline1 uid " + uid + " OutlineLevel (COM: " +
                          std::to_string(row.level) + ")");
        context.equal(task->outline_number, std::string(row.number),
                      "outline1 uid " + uid + " OutlineNumber");
        context.equal(task->summary, row.summary,
                      "outline1 uid " + uid + " Summary (COM: " +
                          (row.summary ? "true" : "false") + ")");
        context.equal(task->child_count, row.children,
                      "outline1 uid " + uid + " child count");
        context.equal(task->critical, row.critical,
                      "outline1 uid " + uid + " Critical (COM: " +
                          (row.critical ? "true" : "false") + ")");
        context.equal(task->total_slack_minutes(),
                      static_cast<std::int32_t>(row.slack_min),
                      "outline1 uid " + uid + " TotalSlack in minutes (+190)");
        if (row.uid == 1) {
            /* W30: the summary is where +74 and +186 disagree.  COM reported
             * TotalSlack 0, FreeSlack 0 and StartSlack 1920; the file stores
             * +190 = 0, +74 = 0 and +186 = 19200 ticks.  Reading +74 as
             * TotalSlack was the W18 error. */
            context.equal(task->start_slack_minutes(), std::int32_t{1920},
                          "outline1's summary StartSlack is 1920 min (+186)");
            context.equal(task->free_slack_minutes(), std::int32_t{0},
                          "outline1's summary FreeSlack is 0 (+74)");
        }
        context.check(!task->is_milestone(),
                      "outline1 uid " + uid + " is not a milestone");
        context.equal(mpp::format_date_iso(task->start), std::string(row.start),
                      "outline1 uid " + uid + " start");
        context.equal(mpp::format_date_iso(task->finish), std::string(row.finish),
                      "outline1 uid " + uid + " finish");
    }
}

/* ---- 8. W18: the Critical/milestone fixture, against
 * docs/analysis/oracle/w3-o1.json.
 *
 * corpus/probe/w3-o1.mpp: T1 1 d with 2400 min of slack (not critical), T2 5 d
 * (critical, slack 0), T3 4 h (not critical, 360 min), T4 a 0-day milestone
 * (not critical, 1920 min).  All four are OutlineLevel 1 and none is a summary.
 * The critical flag is derived (not stored): slack <= 0 and not manually
 * scheduled. */
void test_status_fixture(TestContext& context) {
    const std::string directory = MPP_PROBE_DIR;
    if (corpus_hidden()) {
        std::cout << "SKIP: w3-o1.mpp checks (MPP_NO_CORPUS is set)\n";
        return;
    }
    const std::string path = directory + "/w3-o1.mpp";
    if (!file_exists(path)) {
        std::cout << "SKIP: " << path << " is not present\n";
        return;
    }
    const mpp::Document document = mpp::load_file(path);
    std::cout << "W3-O1: " << describe(document) << '\n' << std::flush;

    context.equal(document.tasks.size(), std::size_t{4}, "w3-o1 has 4 tasks");
    context.check(document.warnings.empty(), "w3-o1 parses without warnings");
    context.equal(document.project.title, std::string("w3-o1"), "w3-o1 project title");
    context.equal(mpp::format_date_iso(document.project.start),
                  std::string("2026-10-19 08:00:00+00:00"), "w3-o1 project start");
    context.equal(mpp::format_date_iso(document.project.finish),
                  std::string("2026-10-26 17:00:00+00:00"), "w3-o1 project finish");

    struct Expected {
        std::uint32_t uid;
        const char* name;
        std::uint32_t level;
        const char* number;
        bool critical;
        bool milestone;
        std::uint32_t slack_ticks;
        std::uint32_t duration_min;
        std::uint32_t constraint_type;
        const char* constraint_date;
    };
    /* Values from docs/analysis/oracle/w3-o1.json (COM).  W19: every task was
     * pinned by a Set Start edit, which Project stores as SNET (type 4) with
     * that instant as the constraint date; T1's remains ASAP with no date. */
    const Expected expected[] = {
        {1, "T1", 1, "1", false, false, 24000, 480, 0, ""},
        {2, "T2", 1, "2", true, false, 0, 2400, 4, "2026-10-20 08:00:00+00:00"},
        {3, "T3", 1, "3", false, false, 3600, 240, 4, "2026-10-23 15:00:00+00:00"},
        {4, "T4", 1, "4", false, true, 19200, 0, 4, "2026-10-21 08:00:00+00:00"},
    };
    for (const Expected& row : expected) {
        const std::string uid = std::to_string(row.uid);
        const mpp::Task* task = find_task(document, row.uid);
        context.check(task != nullptr, "w3-o1 has uid " + uid);
        if (task == nullptr) {
            continue;
        }
        context.equal(task->name, std::string(row.name), "w3-o1 uid " + uid + " name");
        context.equal(task->outline_level, row.level,
                      "w3-o1 uid " + uid + " OutlineLevel");
        context.equal(task->outline_number, std::string(row.number),
                      "w3-o1 uid " + uid + " OutlineNumber");
        context.equal(task->critical, row.critical,
                      "w3-o1 uid " + uid + " Critical (COM: " +
                          (row.critical ? "true" : "false") + ")");
        context.equal(task->is_milestone(), row.milestone,
                      "w3-o1 uid " + uid + " Milestone (COM: " +
                          (row.milestone ? "true" : "false") + ")");
        context.equal(task->milestone_stored, true,
                      "w3-o1 uid " + uid +
                          " reads Milestone from the stored FixedMeta flag");
        context.equal(task->total_slack_ticks,
                      static_cast<std::int32_t>(row.slack_ticks),
                      "w3-o1 uid " + uid + " TotalSlack ticks (+190)");
        context.equal(task->total_slack_minutes(),
                      static_cast<std::int32_t>(row.slack_ticks / 10),
                      "w3-o1 uid " + uid + " TotalSlack minutes");
        /* W30: w3-o1's four rows have FreeSlack == StartSlack == TotalSlack, so
         * this fixture pins the values but cannot separate the three offsets;
         * milestone1.mpp ID6 and outline1's summary do that. */
        context.check(task->free_slack_ticks ==
                              static_cast<std::int32_t>(row.slack_ticks) &&
                          task->start_slack_ticks ==
                              static_cast<std::int32_t>(row.slack_ticks),
                      "w3-o1 uid " + uid +
                          " has FreeSlack (+74) == StartSlack (+186) == TotalSlack (+190)");
        context.equal(task->duration_minutes(), row.duration_min,
                      "w3-o1 uid " + uid + " duration in minutes");
        /* W19: +64/+66 against the COM ConstraintType/ConstraintDate. */
        context.equal(static_cast<unsigned>(task->constraint_type), row.constraint_type,
                      "w3-o1 uid " + uid + " ConstraintType (+64, COM: " +
                          std::to_string(row.constraint_type) + ")");
        context.equal(mpp::format_date_iso(task->constraint_date),
                      std::string(row.constraint_date),
                      "w3-o1 uid " + uid + " ConstraintDate (+66, COM)");
        context.check(!task->deadline.valid(), "w3-o1 uid " + uid + " has no Deadline");
        context.check(!task->summary, "w3-o1 uid " + uid + " is not a summary");
        context.check(!task->manually_scheduled,
                      "w3-o1 uid " + uid + " is auto-scheduled (COM Manual=false)");
    }

    /* W31: none of w3-o1's tasks has a stored start anchor (its dates come from
     * SNET constraints, not a driving predecessor) and none names a task
     * calendar. */
    const std::ptrdiff_t anchors = std::count_if(
        document.tasks.begin(), document.tasks.end(),
        [](const mpp::Task& task) { return task.start_anchor.valid(); });
    context.equal(anchors, std::ptrdiff_t{0},
                  "no w3-o1 task stores a +174 start anchor");
    const std::ptrdiff_t calendars = std::count_if(
        document.tasks.begin(), document.tasks.end(),
        [](const mpp::Task& task) { return task.calendar_uid != -1; });
    context.equal(calendars, std::ptrdiff_t{0},
                  "no w3-o1 task names a task calendar (+178 is -1)");

    /* ---- W40: the manual-mode bit is measured, not inferred.  The coordinator
     * built corpus/probe/w37-snet-auto.mpp from base.mpp by clearing bit 0x80
     * of Fixed2Meta entry 4 (row 1) and setting SNET on +64/+66; Project opens
     * it with uid 1 Manual=False while the otherwise identical w37-snet-save.mpp
     * opens Manual=True.  The fixture is git-ignored, so guard it. */
    const std::string base_path = directory + "/base.mpp";
    const std::string auto_path = directory + "/w37-snet-auto.mpp";
    if (file_exists(base_path) && file_exists(auto_path)) {
        const mpp::Document base = mpp::load_file(base_path);
        for (const std::uint32_t uid : {1u, 2u, 3u}) {
            const mpp::Task* task = find_task(base, uid);
            context.check(task != nullptr && task->manually_scheduled,
                          "base uid " + std::to_string(uid) +
                              " reads the set manual bit (COM Manual=true)");
        }
        const mpp::Document auto_doc = mpp::load_file(auto_path);
        const mpp::Task* cleared = find_task(auto_doc, 1);
        context.check(cleared != nullptr && !cleared->manually_scheduled,
                      "w37-snet-auto uid 1 reads the cleared manual bit "
                      "(COM Manual=false)");
        const mpp::Task* kept = find_task(auto_doc, 2);
        context.check(kept != nullptr && kept->manually_scheduled,
                      "w37-snet-auto uid 2 keeps its manual bit (COM Manual=true)");
    } else {
        std::cout << "SKIP: " << auto_path
                  << " is not present (manual-bit oracle fixture absent)\n";
    }
}

/* ---- 8b. W31: the start anchor (+174) and task-calendar uid (+178) on the
 * fixtures that prove what they mean.
 *
 *   holiday1.mpp: COM reported SiteCal as the task calendar for all three
 *     tasks, and every task row stores 3 at +178.  T2's +174 is T1's 17:00
 *     finish (2026-10-20 17:00) while T2 starts 2026-10-22 08:00 -- the 21st is
 *     the calendar's holiday, which is exactly why +174 and Start differ there.
 *   manual-start-edited.mpp: COM set uid 1's Start to 2026-01-12 08:00; the
 *     effective +104 and +174 hold it while the calculated +96 is still the
 *     stale 2026-01-05 08:00.  uids 2/3 still store their date-only manual
 *     start (2026-01-04 13:00 = 2026-01-05 00:00 Sydney, the UTC encoding of a
 *     date-only property).
 *   milestone1.mpp: the stored milestone flag matches COM row for row
 *     (docs/analysis/oracle/milestone-probe.json: uids 2,3,4,5,7).
 *   schedin.mpp: only P50 stores an anchor, and it is P50's own start.
 *
 * All values are uids/dates/counts -- no task names. */
void test_w31_fixed_fields(TestContext& context) {
    const std::string directory = MPP_PROBE_DIR;
    if (corpus_hidden()) {
        std::cout << "SKIP: W31 +174/+178 fixture checks (MPP_NO_CORPUS is set)\n";
        return;
    }

    const std::string holiday = directory + "/holiday1.mpp";
    if (!file_exists(holiday)) {
        std::cout << "SKIP: " << holiday << " is not present\n";
    } else {
        const mpp::Document document = mpp::load_file(holiday);
        std::cout << "HOLIDAY1: " << describe(document) << '\n' << std::flush;
        context.equal(document.tasks.size(), std::size_t{3}, "holiday1 has 3 tasks");
        const std::ptrdiff_t calendar_3 = std::count_if(
            document.tasks.begin(), document.tasks.end(),
            [](const mpp::Task& task) { return task.calendar_uid == 3; });
        context.equal(calendar_3, std::ptrdiff_t{3},
                      "all three holiday1 tasks name task calendar uid 3 (SiteCal)");
        const mpp::Task* t2 = find_task(document, 2);
        context.check(t2 != nullptr, "holiday1 has uid 2");
        if (t2 != nullptr) {
            context.equal(mpp::format_date_iso(t2->start_anchor),
                          std::string("2026-10-20 17:00:00+00:00"),
                          "holiday1 uid 2's +174 is its predecessor's finish");
            context.equal(mpp::format_date_iso(t2->start),
                          std::string("2026-10-22 08:00:00+00:00"),
                          "holiday1 uid 2 starts after the calendar holiday, "
                          "so +174 and Start differ");
        }
        const mpp::Task* t1 = find_task(document, 1);
        const mpp::Task* t3 = find_task(document, 3);
        context.check(t1 != nullptr && !t1->start_anchor.valid(),
                      "holiday1 uid 1 stores no start anchor");
        context.check(t3 != nullptr && !t3->start_anchor.valid(),
                      "holiday1 uid 3 stores no start anchor");
    }

    const std::string manual = directory + "/manual-start-edited.mpp";
    if (!file_exists(manual)) {
        std::cout << "SKIP: " << manual << " is not present\n";
    } else {
        const mpp::Document document = mpp::load_file(manual);
        const mpp::Task* edited = find_task(document, 1);
        context.check(edited != nullptr, "manual-start-edited has uid 1");
        if (edited != nullptr) {
            context.check(edited->manually_scheduled,
                          "manual-start-edited uid 1 is manually scheduled");
            context.equal(mpp::format_date_iso(edited->start_anchor),
                          std::string("2026-01-12 08:00:00+00:00"),
                          "a manual task's +174 is COM's edited manual Start");
            context.equal(mpp::format_date_iso(edited->start),
                          std::string("2026-01-12 08:00:00+00:00"),
                          "the effective +104 Start is COM's post-edit Start");
            context.equal(mpp::format_date_iso(edited->calculated_start),
                          std::string("2026-01-05 08:00:00+00:00"),
                          "the calculated +96 Start is still the stale pre-edit value");
        }
        const mpp::Task* unchanged = find_task(document, 2);
        context.check(unchanged != nullptr, "manual-start-edited has uid 2");
        if (unchanged != nullptr) {
            context.equal(mpp::format_date_iso(unchanged->start_anchor),
                          std::string("2026-01-04 13:00:00+00:00"),
                          "an untouched manual task stores its date-only manual "
                          "start at +174 (UTC encoding)");
        }
    }

    const std::string milestones = directory + "/milestone1.mpp";
    if (!file_exists(milestones)) {
        std::cout << "SKIP: " << milestones << " is not present\n";
    } else {
        const mpp::Document document = mpp::load_file(milestones);
        std::cout << "MILESTONE1: " << describe(document) << '\n' << std::flush;
        context.equal(document.tasks.size(), std::size_t{7},
                      "milestone1 has 7 tasks");
        std::size_t flagged = 0;
        std::size_t stored = 0;
        for (const mpp::Task& task : document.tasks) {
            flagged += task.milestone ? 1u : 0u;
            stored += task.milestone_stored ? 1u : 0u;
        }
        context.equal(flagged, std::size_t{5},
                      "milestone1 has COM's 5 milestones (uids 2,3,4,5,7)");
        context.equal(stored, document.tasks.size(),
                      "every milestone1 row carries the stored flag");
        const std::uint32_t milestone_uids[] = {2, 3, 4, 5, 7};
        bool all_flagged = true;
        for (const std::uint32_t uid : milestone_uids) {
            const mpp::Task* task = find_task(document, uid);
            all_flagged = all_flagged && task != nullptr && task->is_milestone();
        }
        context.check(all_flagged,
                      "milestone1 uids 2,3,4,5,7 are exactly the milestones");
        const mpp::Task* not_milestone = find_task(document, 1);
        context.check(not_milestone != nullptr && !not_milestone->is_milestone(),
                      "milestone1 uid 1 is not a milestone");

        /* W30: the discriminator for the three slack offsets.  COM reports ID6
         * (uid 6) as FreeSlack 0, StartSlack 22080 and TotalSlack 22080 minutes;
         * the file stores +74 = 0, +186 = +190 = 220800 ticks.  Reading +74 as
         * TotalSlack therefore produced 0 instead of 22080. */
        const mpp::Task* id6 = find_task(document, 6);
        context.check(id6 != nullptr, "milestone1 has uid 6");
        if (id6 != nullptr) {
            context.equal(id6->free_slack_ticks, std::int32_t{0},
                          "milestone1 uid 6 FreeSlack is 0 (+74)");
            context.equal(id6->start_slack_ticks, std::int32_t{220800},
                          "milestone1 uid 6 StartSlack is 220800 ticks (+186)");
            context.equal(id6->total_slack_ticks, std::int32_t{220800},
                          "milestone1 uid 6 TotalSlack is 220800 ticks (+190)");
            context.equal(id6->total_slack_minutes(), std::int32_t{22080},
                          "milestone1 uid 6 TotalSlack is 22080 minutes (COM)");
        }
    }

    const std::string schedin = directory + "/schedin.mpp";
    if (file_exists(schedin)) {
        const mpp::Document document = mpp::load_file(schedin);
        const std::ptrdiff_t anchors = std::count_if(
            document.tasks.begin(), document.tasks.end(),
            [](const mpp::Task& task) { return task.start_anchor.valid(); });
        context.equal(anchors, std::ptrdiff_t{1},
                      "only schedin's P50 stores a +174 anchor");
        const mpp::Task* p50 = find_task(document, 9);
        context.check(p50 != nullptr && p50->start_anchor.raw == p50->start.raw,
                      "schedin P50's +174 equals its own Start");
    }
}

/* ---- 9. W19: the scheduler-input fixture, against
 * docs/analysis/oracle/schedin-probe.json.
 *
 * corpus/probe/schedin.mpp: ten 1-day tasks, one per PjConstraint value in ID
 * order KASAP (0) .. KFNLT (7); KMSO..KFNLT carry ConstraintDate
 * 2026-10-26 08:00 and KALAP is type 1 with no date.  P50 is a 2-day task at
 * 50 % (ActualDuration 4800 ticks) and DL has Deadline 2026-10-21 17:00.
 * The constraint type/date live at +64 (u16) and +66 (u32) of the fixed row;
 * the deadline at +182, not in VarMeta. */
void test_schedin_fixture(TestContext& context) {
    const std::string directory = MPP_PROBE_DIR;
    if (corpus_hidden()) {
        std::cout << "SKIP: schedin.mpp checks (MPP_NO_CORPUS is set)\n";
        return;
    }
    const std::string path = directory + "/schedin.mpp";
    if (!file_exists(path)) {
        std::cout << "SKIP: " << path << " is not present\n";
        return;
    }
    const mpp::Document document = mpp::load_file(path);
    std::cout << "SCHEDIN: " << describe(document) << '\n' << std::flush;

    context.equal(document.tasks.size(), std::size_t{10}, "schedin has 10 tasks");
    context.check(document.skipped_rows.empty(), "schedin has no deleted rows");
    context.check(document.warnings.empty(), "schedin parses without warnings");

    struct Expected {
        std::uint32_t uid;
        const char* name;
        std::uint32_t constraint_type;
        const char* constraint_date; /* "" = NA */
        const char* deadline;        /* "" = NA */
        std::uint32_t actual_duration_min;
        const char* actual_start;    /* "" = NA, inference */
        const char* actual_finish;   /* "" = NA, inference */
    };
    /* COM values from docs/analysis/oracle/schedin-probe.json; the dates are
     * the fixed-row words at +66/+182/+120/+124. */
    const Expected expected[] = {
        {1, "KASAP", 0, "", "", 0, "", ""},
        {2, "KALAP", 1, "", "", 0, "", ""},
        {3, "KMSO", 2, "2026-10-26 08:00:00+00:00", "", 0, "", ""},
        {4, "KMFO", 3, "2026-10-26 08:00:00+00:00", "", 0, "", ""},
        {5, "KSNET", 4, "2026-10-26 08:00:00+00:00", "", 0, "", ""},
        {6, "KSNLT", 5, "2026-10-26 08:00:00+00:00", "", 0, "", ""},
        {7, "KFNET", 6, "2026-10-26 08:00:00+00:00", "", 0, "", ""},
        {8, "KFNLT", 7, "2026-10-26 08:00:00+00:00", "", 0, "", ""},
        {9, "P50", 4, "2026-11-02 08:00:00+00:00", "", 480,
         "2026-11-02 08:00:00+00:00", ""},
        {10, "DL", 0, "", "2026-10-21 17:00:00+00:00", 0, "", ""},
    };
    for (const Expected& row : expected) {
        const std::string uid = std::to_string(row.uid);
        const mpp::Task* task = find_task(document, row.uid);
        context.check(task != nullptr, "schedin has uid " + uid);
        if (task == nullptr) {
            continue;
        }
        context.equal(task->name, std::string(row.name), "schedin uid " + uid + " name");
        context.equal(static_cast<unsigned>(task->constraint_type),
                      row.constraint_type,
                      "schedin uid " + uid + " ConstraintType (+64, COM: " +
                          std::to_string(row.constraint_type) + ")");
        context.equal(mpp::format_date_iso(task->constraint_date),
                      std::string(row.constraint_date),
                      "schedin uid " + uid + " ConstraintDate (+66, COM)");
        context.equal(mpp::format_date_iso(task->deadline), std::string(row.deadline),
                      "schedin uid " + uid + " Deadline (+182, COM)");
        context.equal(mpp::duration_minutes(task->actual_duration_ticks),
                      row.actual_duration_min,
                      "schedin uid " + uid + " ActualDuration (+80)");
        context.equal(mpp::format_date_iso(task->actual_start),
                      std::string(row.actual_start),
                      "schedin uid " + uid + " ActualStart (+120, INFERENCE)");
        context.equal(mpp::format_date_iso(task->actual_finish),
                      std::string(row.actual_finish),
                      "schedin uid " + uid + " ActualFinish (+124, INFERENCE)");
        context.check(!task->summary, "schedin uid " + uid + " is not a summary");
    }

    /* Raw words, so the test fails on an offset shift even if the decoded
     * strings happen to agree. */
    const mpp::Task* kmso = find_task(document, 3);
    context.check(kmso != nullptr, "schedin has KMSO");
    if (kmso != nullptr) {
        context.equal(kmso->constraint_date.raw, std::uint32_t{0x3D1812C0u},
                      "schedin KMSO ConstraintDate bytes (+66) are 2026-10-26 08:00");
    }
    const mpp::Task* dl = find_task(document, 10);
    context.check(dl != nullptr, "schedin has DL");
    if (dl != nullptr) {
        context.equal(dl->deadline.raw, std::uint32_t{0x3D1327D8u},
                      "schedin DL Deadline bytes (+182) are 2026-10-21 17:00");
        context.check(!dl->constraint_date.valid(),
                      "schedin DL is ASAP and stores no constraint date");
    }
    const mpp::Task* p50 = find_task(document, 9);
    context.check(p50 != nullptr, "schedin has P50");
    if (p50 != nullptr) {
        context.equal(p50->actual_duration_ticks, std::uint32_t{4800},
                      "schedin P50 ActualDuration is 4800 ticks (1 of its 2 days)");
        context.equal(static_cast<unsigned>(p50->percent_complete), 50u,
                      "schedin P50 PercentComplete is 50");
        context.check(p50->actual_start.valid() && !p50->actual_finish.valid(),
                      "schedin P50 has an actual start but no actual finish (50 %)");
    }

    /* The file's structure: eight tasks carry a non-ASAP type and seven carry a
     * constraint date (KALAP is type 1 with no date). */
    std::size_t typed = 0;
    std::size_t dated = 0;
    for (const mpp::Task& task : document.tasks) {
        typed += task.constraint_type != 0 ? 1u : 0u;
        dated += task.constraint_date.valid() ? 1u : 0u;
    }
    context.equal(typed, std::size_t{8}, "schedin has 8 constrained tasks");
    context.equal(dated, std::size_t{7}, "schedin has 7 stored constraint dates");
}

/* ---- 6. Every corpus/probe fixture loads; counts and a few values match. */
void test_probe_fixtures(TestContext& context) {
    const std::string directory = MPP_PROBE_DIR;
    if (corpus_hidden()) {
        std::cout << "SKIP: " << directory
                  << " sweep (MPP_NO_CORPUS is set: corpus forced absent)\n";
        return;
    }
    if (!file_exists(directory)) {
        std::cout << "SKIP: " << directory << " is not present\n";
        return;
    }
    const std::vector<std::pair<std::string, std::size_t>> expected = {
        {"base.mpp", 3},       {"confined-test.mpp", 1}, {"n1.mpp", 1},
        {"n2.mpp", 2},         {"n3.mpp", 3},            {"n4.mpp", 4},
        {"n5.mpp", 5},         {"n6.mpp", 6},            {"probe-v1.mpp", 3},
        {"probe-v2.mpp", 3},
    };
    std::error_code error;
    std::size_t loaded = 0;
    for (const auto& item : std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        if (!item.is_regular_file() || item.path().extension() != ".mpp") {
            continue;
        }
        const std::string name = item.path().filename().string();
        try {
            const mpp::Document document = mpp::load_file(item.path().string());
            ++loaded;
            const auto it = std::find_if(
                expected.begin(), expected.end(),
                [&name](const std::pair<std::string, std::size_t>& candidate) {
                    return candidate.first == name;
                });
            if (it != expected.end()) {
                context.equal(document.tasks.size(), it->second, name + " task count");
            }
            if (name == "base.mpp") {
                context.equal(document.project.title, std::string("base"),
                              "base.mpp project title");
                if (document.tasks.size() == 3) {
                    context.equal(document.tasks[0].name, std::string("Task A"),
                                  "base.mpp task 1 name");
                    context.equal(
                        mpp::format_duration_days(document.tasks[0].duration_ticks),
                        std::string("5"), "base.mpp task A is 5 days");
                    context.equal(mpp::format_date(document.tasks[0].start),
                                  std::string("2026-01-04 13:00"),
                                  "base.mpp task A effective start (+104) is the "
                                  "date-only manual start, exactly what COM reports");
                }
            }
            if (name == "var-title.mpp") {
                context.equal(document.project.title, std::string("Probe Title"),
                              "var-title.mpp project title follows the edit");
            }
            if (name == "var-pct.mpp" && document.tasks.size() == 3) {
                context.equal(
                    static_cast<unsigned>(document.tasks[0].percent_complete), 50u,
                    "var-pct.mpp task 1 is 50 % complete");
            }
            if (name == "var-priority.mpp" && document.tasks.size() == 3) {
                context.equal(static_cast<unsigned>(document.tasks[2].priority), 900u,
                              "var-priority.mpp task 3 priority is 900");
            }
        } catch (const std::exception& exception) {
            context.check(false, name + " failed to load: " + exception.what());
        }
    }
    context.check(loaded >= 19, "every corpus/probe fixture loaded (" +
                                    std::to_string(loaded) + " files)");
}

/* ---- 7. A file that is not a compound file is rejected, not guessed at. */
void test_bad_input(TestContext& context) {
    if (!file_exists(MPP_GOLDEN_PATH)) {
        return;
    }
    bool threw = false;
    std::string message;
    try {
        (void)mpp::load_file(MPP_GOLDEN_PATH);
    } catch (const mpp::Error& error) {
        threw = true;
        message = error.what();
    }
    context.check(threw, "a non-CFB file raises mpp::Error");
    context.check(message.find("compound file") != std::string::npos,
                  "the error names the compound-file problem: " + message);

    bool threw_empty = false;
    try {
        (void)mpp::load_bytes(std::vector<std::uint8_t>{});
    } catch (const mpp::Error&) {
        threw_empty = true;
    }
    context.check(threw_empty, "an empty buffer raises mpp::Error");
}


const mpp::Resource* find_resource(const mpp::Document& document, std::uint32_t uid) {
    const auto found = std::find_if(
        document.resources.begin(), document.resources.end(),
        [uid](const mpp::Resource& resource) { return resource.uid == uid; });
    return found == document.resources.end() ? nullptr : &*found;
}

const mpp::Link* find_link(const mpp::Document& document, std::uint32_t uid) {
    const auto found = std::find_if(
        document.links.begin(), document.links.end(),
        [uid](const mpp::Link& link) { return link.uid == uid; });
    return found == document.links.end() ? nullptr : &*found;
}

const mpp::Assignment* find_assignment(const mpp::Document& document, std::uint32_t uid) {
    const auto found = std::find_if(
        document.assignments.begin(), document.assignments.end(),
        [uid](const mpp::Assignment& assignment) { return assignment.uid == uid; });
    return found == document.assignments.end() ? nullptr : &*found;
}

/* ---- The committed PROJECT_A link oracle: MS Project's 88 <PredecessorLink>
 * elements from corpus/probe/project_a-export.xml, as uids only (no names, no dates)
 * in tests/data/project_a_links_golden.tsv.  MSPDI's LinkLag is tenths of a minute,
 * the same scale as the link row's +14 i32, so the values compare directly.
 * Columns: succ_uid, pred_uid, type (0 FF / 1 FS / 2 SF / 3 SS), lag_ticks. */
struct GoldenLink {
    std::uint32_t successor_uid = 0;
    std::uint32_t predecessor_uid = 0;
    unsigned type = 0;
    std::int32_t lag_ticks = 0;
};

std::vector<GoldenLink> read_link_golden(const std::string& path, TestContext& context) {
    std::vector<GoldenLink> rows;
    const std::vector<std::string> lines = read_lines(path);
    if (lines.empty()) {
        context.check(false, std::string("link golden file is unreadable: ") + path);
        return rows;
    }
    for (std::size_t index = 1; index < lines.size(); ++index) {
        const std::vector<std::string> fields = split_tabs(lines[index]);
        if (fields.size() < 4) {
            context.check(false, "link golden line " + std::to_string(index + 1) +
                                     " has " + std::to_string(fields.size()) +
                                     " fields, expected 4");
            continue;
        }
        GoldenLink row;
        row.successor_uid = static_cast<std::uint32_t>(std::stoul(fields[0]));
        row.predecessor_uid = static_cast<std::uint32_t>(std::stoul(fields[1]));
        row.type = static_cast<unsigned>(std::stoul(fields[2]));
        row.lag_ticks = static_cast<std::int32_t>(std::stol(fields[3]));
        rows.push_back(row);
    }
    return rows;
}

/* ---- 6. Resources: FixedData = 48-byte header + 172 bytes per resource row;
 * row 0 is the uid-0 placeholder, rows 1.. are the resources.  Name is VarMeta
 * 0x0c400001; the only work value in the row is the double at +8 (MaxUnits
 * x100: 10000.0 = 100 %). */
void test_resources(TestContext& context) {
    const std::string probe = MPP_PROBE_DIR;
    if (!file_exists(MPP_PROJECT_A_PATH) || !file_exists(MPP_PROJECT_B_PATH) ||
        !file_exists(probe + "/base.mpp")) {
        std::cout << "SKIP: corpus is not present\n";
        return;
    }
    const mpp::Document base = mpp::load_file(probe + "/base.mpp");
    context.equal(base.resources.size(), std::size_t{1},
                  "base.mpp has one resource (48 + 172*2 bytes of FixedData)");
    const mpp::Resource* crew = find_resource(base, 1);
    context.check(crew != nullptr, "base.mpp resource uid 1 is present");
    if (crew != nullptr) {
        context.equal(crew->row, std::uint32_t{1},
                      "the first resource is FixedData row 1 (row 0 is the placeholder)");
        context.equal(crew->row_id, std::uint32_t{1}, "base.mpp resource row id");
        context.equal(crew->name, std::string("Crew"),
                      "base.mpp resource name via VarMeta 0x0c400001");
        context.equal(crew->initials, std::string("C"),
                      "base.mpp resource initials via VarMeta 0x0c400002");
        context.equal(crew->max_units_x100, 10000.0,
                      "MaxUnits is the double at +8 (x100: 10000.0 = 100 %)");
        context.equal(crew->max_units_percent(), std::uint32_t{100},
                      "MaxUnits reports as 100 %");
        context.check(crew->is_work(), "resource type defaults to Work (not stored in the row)");
    }

    const mpp::Document n1 = mpp::load_file(probe + "/n1.mpp");
    context.equal(n1.resources.size(), std::size_t{0},
                  "n1.mpp has no resources (only the 48-byte header + placeholder)");

    const mpp::Document two = mpp::load_file(probe + "/var-rsc2.mpp");
    context.equal(two.resources.size(), std::size_t{2}, "var-rsc2.mpp has two resources");
    const mpp::Resource* plant = find_resource(two, 2);
    context.check(plant != nullptr, "var-rsc2.mpp resource uid 2 is present");
    if (plant != nullptr) {
        context.equal(plant->name, std::string("Plant"), "var-rsc2.mpp second resource is Plant");
    }

    const std::string assign1 = probe + "/assign1.mpp";
    if (file_exists(assign1)) {
        const mpp::Document assignments = mpp::load_file(assign1);
        context.equal(assignments.resources.size(), std::size_t{2},
                      "assign1.mpp has two resources (Crew, Crane)");
        const mpp::Resource* crane = find_resource(assignments, 2);
        context.check(crane != nullptr, "assign1.mpp resource uid 2 is present");
        if (crane != nullptr) {
            context.equal(crane->name, std::string("Crane"), "assign1.mpp resource 2 is Crane");
        }
    }

    const mpp::Document project_a = mpp::load_file(MPP_PROJECT_A_PATH);
    context.equal(project_a.resources.size(), std::size_t{22}, "PROJECT_A has 22 resources");
    const mpp::Resource* first = find_resource(project_a, 1);
    context.check(first != nullptr, "PROJECT_A resource uid 1 is present");
    if (first != nullptr) {
        context.equal(first->name, std::string("client-text-102"), "PROJECT_A resource 1 name");
    }
    const mpp::Resource* spaced = find_resource(project_a, 3);
    context.check(spaced != nullptr, "PROJECT_A resource uid 3 is present");
    if (spaced != nullptr) {
        context.equal(spaced->name_raw, std::string("client-text-097 "),
                      "resource name_raw keeps the stored trailing space");
        context.equal(spaced->name, std::string("client-text-097"),
                      "resource name is trimmed for display like COM");
    }
    context.equal(mpp::load_file(MPP_PROJECT_B_PATH).resources.size(), std::size_t{22},
                  "PROJECT_B has 22 resources");
}

/* ---- 7. Links: FixedData = 20*links, no header.  +12 type (0 FF / 1 FS /
 * 2 SF / 3 SS), +14 signed lag in 6-second ticks (4800 = one working day). */
void test_links(TestContext& context) {
    const std::string probe = MPP_PROBE_DIR;
    if (!file_exists(MPP_PROJECT_A_PATH) || !file_exists(MPP_PROJECT_B_PATH) ||
        !file_exists(probe + "/base.mpp") || !file_exists(probe + "/link1.mpp")) {
        std::cout << "SKIP: corpus is not present\n";
        return;
    }
    const mpp::Document link1 = mpp::load_file(probe + "/link1.mpp");
    context.equal(link1.links.size(), std::size_t{5},
                  "link1.mpp has 5 links (100 bytes = 5 x 20, no header)");
    context.equal(link1.link_count_with_live_endpoints(), std::size_t{5},
                  "all 5 link1.mpp links have live endpoints");
    context.equal(link1.link_count_with_dead_endpoints(), std::size_t{0},
                  "link1.mpp has no dead-endpoint links");
    const mpp::Link* first = find_link(link1, 1);
    context.check(first != nullptr, "link1.mpp link uid 1 is present");
    if (first != nullptr) {
        context.equal(first->predecessor_uid, std::uint32_t{1}, "link 1 predecessor");
        context.equal(first->successor_uid, std::uint32_t{2}, "link 1 successor");
        context.equal(first->type_name(), std::string("FS"), "link 1 is Finish-to-Start");
        context.equal(first->lag_ticks, std::int32_t{0}, "link 1 lag is 0");
        context.equal(static_cast<unsigned>(first->lag_units), 7u,
                      "link lag format word is 7");
        context.check(first->predecessor_known && first->successor_known,
                      "link 1 endpoints resolve to live tasks");
    }
    const auto mpx_link_case = [&](std::uint32_t uid, const char* type, std::int32_t lag) {
        const mpp::Link* link = find_link(link1, uid);
        context.check(link != nullptr, std::string("link1.mpp link uid ") +
                                           std::to_string(uid) + " is present");
        if (link != nullptr) {
            context.equal(link->type_name(), std::string(type),
                          "link " + std::to_string(uid) + " type");
            context.equal(link->lag_ticks, lag,
                          "link " + std::to_string(uid) + " lag ticks");
        }
    };
    mpx_link_case(2, "FF", -4800); /* lead = negative */
    mpx_link_case(3, "SF", 1200);  /* 2 hours */
    mpx_link_case(4, "SS", 4800);  /* 1 working day */

    const mpp::Document project_b = mpp::load_file(MPP_PROJECT_B_PATH);
    context.equal(project_b.links.size(), std::size_t{72},
                  "PROJECT_B has 72 links (1440 = 20 x 72)");
    if (!project_b.links.empty()) {
        context.equal(project_b.links.front().uid, std::uint32_t{7371}, "PROJECT_B first link uid");
        context.equal(project_b.links.front().predecessor_uid, std::uint32_t{10722},
                      "PROJECT_B first link predecessor");
        context.equal(project_b.links.front().successor_uid, std::uint32_t{10723},
                      "PROJECT_B first link successor");
        context.equal(project_b.links.front().type_name(), std::string("FS"),
                      "PROJECT_B links are Finish-to-Start");
    }
    const std::ptrdiff_t non_fs = std::count_if(
        project_b.links.begin(), project_b.links.end(),
        [](const mpp::Link& link) { return link.type_raw != 1; });
    context.equal(non_fs, std::ptrdiff_t{0}, "all 72 PROJECT_B links have type 1 (FS)");

    /* ---- W31: PROJECT_B is the case where row liveness and endpoint liveness
     * differ.  All 72 rows are named by FixedMeta, but the first two links
     * (10722 -> 10723 and 10723 -> 10724) name uids that exist only in deleted
     * task slots, and Project's MSPDI export has only 70 <PredecessorLink>
     * elements.  The reader keeps the 72 but reports the split. */
    context.equal(project_b.link_count_with_live_endpoints(), std::size_t{70},
                  "PROJECT_B has 70 links with both endpoints live "
                  "(Project exports 70)");
    context.equal(project_b.link_count_with_dead_endpoints(), std::size_t{2},
                  "PROJECT_B's other 2 links have dead endpoints");
    const auto dead_link = [&project_b](std::uint32_t pred, std::uint32_t succ) {
        return std::any_of(
            project_b.links.begin(), project_b.links.end(),
            [pred, succ](const mpp::Link& link) {
                return link.predecessor_uid == pred && link.successor_uid == succ &&
                       !link.endpoints_live();
            });
    };
    context.check(dead_link(10722, 10723) && dead_link(10723, 10724),
                  "PROJECT_B's two dead-endpoint links are 10722 -> 10723 and "
                  "10723 -> 10724");
    context.check(!project_b.links.empty() && !project_b.links.front().endpoints_live(),
                  "the first PROJECT_B link has no live endpoint");

    const mpp::Document project_a = mpp::load_file(MPP_PROJECT_A_PATH);
    context.equal(project_a.links.size(), std::size_t{88},
                  "PROJECT_A has 88 live links: one per FixedMeta entry naming a row");
    /* W31: every PROJECT_A link's endpoints are live tasks (the links to the deleted
     * task 10856 are all uncovered rows and were skipped above). */
    context.equal(project_a.link_count_with_live_endpoints(), std::size_t{88},
                  "all 88 PROJECT_A links have live endpoints");
    context.equal(project_a.link_count_with_dead_endpoints(), std::size_t{0},
                  "PROJECT_A has no dead-endpoint links");
    context.equal(project_a.skipped_links.size(), std::size_t{11},
                  "PROJECT_A's 11 uncovered link rows are skipped (2 all-zero + 9 "
                  "deleted non-zero rows)");
    const std::ptrdiff_t fs = std::count_if(
        project_a.links.begin(), project_a.links.end(),
        [](const mpp::Link& link) { return link.type_raw == 1; });
    context.equal(fs, std::ptrdiff_t{87},
                  "87 of PROJECT_A's 88 links are FS (the 88th is SS)");
    const std::ptrdiff_t ss = std::count_if(
        project_a.links.begin(), project_a.links.end(),
        [](const mpp::Link& link) { return link.type_raw == 3; });
    context.equal(ss, std::ptrdiff_t{1}, "PROJECT_A has one SS link");

    /* ---- The MSPDI oracle: corpus/probe/project_a-export.xml has exactly 88
     * <PredecessorLink> elements, and Project schedules one link per FixedMeta
     * entry.  The committed golden keeps only uids/type/lag (no client names).
     * Lag is LinkLag in tenths of a minute, the same scale as the reader's
     * +14 i32, so the values compare directly. */
    const std::string golden_path =
        (test_data_dir() / "project_a_links_golden.tsv").string();
    if (!file_exists(golden_path)) {
        context.check(false, "committed link golden " + golden_path + " is missing");
    } else {
        const std::vector<GoldenLink> golden = read_link_golden(golden_path, context);
        context.equal(golden.size(), std::size_t{88},
                      "the committed PROJECT_A link golden holds the 88 MSPDI links");
        using LinkKey = std::tuple<std::uint32_t, std::uint32_t, unsigned, std::int32_t>;
        std::vector<LinkKey> expected;
        expected.reserve(golden.size());
        for (const GoldenLink& row : golden) {
            expected.emplace_back(row.successor_uid, row.predecessor_uid, row.type,
                                  row.lag_ticks);
        }
        std::vector<LinkKey> actual;
        actual.reserve(project_a.links.size());
        for (const mpp::Link& link : project_a.links) {
            actual.emplace_back(link.successor_uid, link.predecessor_uid,
                                static_cast<unsigned>(link.type_raw), link.lag_ticks);
        }
        std::sort(expected.begin(), expected.end());
        std::sort(actual.begin(), actual.end());
        context.equal(actual.size(), expected.size(),
                      "PROJECT_A link count equals MS Project's exported link count");
        context.check(actual == expected,
                      "PROJECT_A's (successor, predecessor, type, lag) multiset equals "
                      "MS Project's 88 exported PredecessorLink elements");
    }

    /* ---- The two links the old reading got wrong.  Project schedules 10846
     * from 10849, 10877 and 10759 (not 10760), and 10859 from 10858 with lag 0
     * (not 10758 with a 6-day lag). */
    const auto predecessors_of = [&project_a](std::uint32_t successor) {
        std::vector<std::uint32_t> predecessors;
        for (const mpp::Link& link : project_a.links) {
            if (link.successor_uid == successor) {
                predecessors.push_back(link.predecessor_uid);
            }
        }
        std::sort(predecessors.begin(), predecessors.end());
        return predecessors;
    };
    context.check(predecessors_of(10846) ==
                      std::vector<std::uint32_t>({10759, 10849, 10877}),
                  "10846's predecessors are 10759, 10849 and 10877 (not 10760)");
    const std::vector<std::uint32_t> pred_10859 = predecessors_of(10859);
    context.equal(pred_10859.size(), std::size_t{1},
                  "10859 has exactly one predecessor");
    if (pred_10859.size() == 1) {
        context.equal(pred_10859.front(), std::uint32_t{10858},
                      "10859's predecessor is 10858");
    }
    const mpp::Link* link_10859 = nullptr;
    for (const mpp::Link& link : project_a.links) {
        if (link.successor_uid == 10859) {
            link_10859 = &link;
        }
    }
    context.check(link_10859 != nullptr && link_10859->lag_ticks == 0,
                  "10859 <- 10858 carries lag 0, not the dead row's 28800 ticks");
    const auto has_link = [&project_a](std::uint32_t successor, std::uint32_t predecessor) {
        return std::any_of(project_a.links.begin(), project_a.links.end(),
                           [successor, predecessor](const mpp::Link& link) {
                               return link.successor_uid == successor &&
                                      link.predecessor_uid == predecessor;
                           });
    };
    context.check(has_link(10847, 10846) && has_link(10848, 10847) &&
                      has_link(10854, 10848) && has_link(10855, 10854) &&
                      has_link(10878, 10855),
                  "the 10846 -> 10847 -> 10848 -> 10854 -> 10855 -> 10878 "
                  "cascade is present");
    context.check(!has_link(10846, 10760),
                  "no 10846 <- 10760 link is invented from an uncovered row");

    const mpp::Link* long_lag = find_link(project_a, 7538);
    context.check(long_lag != nullptr, "PROJECT_A link uid 7538 is present");
    if (long_lag != nullptr) {
        context.equal(long_lag->lag_ticks, std::int32_t{240000},
                      "PROJECT_A link 7538 lag is 240000 ticks");
        context.equal(long_lag->lag_days(), 50.0, "240000 ticks is 50 working days");
    }
    const mpp::Link* lead = find_link(project_a, 7436);
    context.check(lead != nullptr, "PROJECT_A link uid 7436 is present");
    if (lead != nullptr) {
        context.equal(lead->lag_ticks, std::int32_t{96000}, "PROJECT_A link 7436 lag is 96000 ticks");
        context.equal(lead->lag_days(), 20.0, "96000 ticks is 20 working days");
    }
    const mpp::Link* deleted = find_link(project_a, 7505);
    context.check(deleted == nullptr,
                  "PROJECT_A link 7505 (a row pointing at deleted task 10856) is not live");
    bool skipped_deleted = false;
    for (const mpp::SkippedRow& skipped : project_a.skipped_links) {
        if (skipped.row == 59 && skipped.uid == 7505) {
            skipped_deleted = true;
        }
    }
    context.check(skipped_deleted,
                  "PROJECT_A link row 59 (uid 7505) is reported as an uncovered slot");
    context.check(find_link(project_a, 7482) == nullptr,
                  "the 10859 <- 10758 lag-28800 row (uid 7482) is not a live link");

    const mpp::Document base = mpp::load_file(probe + "/base.mpp");
    context.equal(base.links.size(), std::size_t{0},
                  "base.mpp has no links (probe-v1's brief was wrong about this)");
    const mpp::Document probe_v1 = mpp::load_file(probe + "/probe-v1.mpp");
    context.equal(probe_v1.links.size(), std::size_t{0},
                  "probe-v1.mpp's TBkndCons/FixedData is empty (0 links)");
}

/* ---- 8. Assignments: FixedData = 110*assignments, no header.  +0 uid,
 * +4 task uid, +8 resource uid, +12 Units (x100), +20 Work (minutes x1000). */
void test_assignments(TestContext& context) {
    const std::string probe = MPP_PROBE_DIR;
    if (!file_exists(MPP_PROJECT_A_PATH) || !file_exists(MPP_PROJECT_B_PATH) ||
        !file_exists(probe + "/base.mpp")) {
        std::cout << "SKIP: corpus is not present\n";
        return;
    }
    const mpp::Document base = mpp::load_file(probe + "/base.mpp");
    context.equal(base.assignments.size(), std::size_t{3},
                  "base.mpp has 3 assignment rows (330 = 110 x 3, no header)");
    const mpp::Assignment* first = find_assignment(base, 1);
    context.check(first != nullptr, "base.mpp assignment uid 1 is present");
    if (first != nullptr) {
        context.equal(first->task_uid, std::uint32_t{1}, "assignment 1 task uid");
        context.equal(first->units_x100, 10000.0, "Units is the double at +12 (x100)");
        context.equal(first->units_percent(), std::uint32_t{100}, "assignment 1 is at 100 %");
        context.equal(first->work_x1000, 2400000.0,
                      "assignment 1 Work is 2,400,000 (minutes x1000)");
        context.equal(first->work_minutes(), std::uint32_t{2400},
                      "assignment 1 work is 2,400 minutes");
        context.equal(first->work_days(), 5.0, "assignment 1 work is 5 working days");
        context.equal(first->resource_uid_raw, mpp::kNullResourceUid,
                      "base.mpp assignment rows carry the null-resource sentinel");
        context.check(!first->has_resource(), "so assignment 1 has no resource");
        context.equal(first->resource_uid(), std::uint32_t{0}, "no resource uid is reported");
    }
    const mpp::Assignment* second = find_assignment(base, 2);
    context.check(second != nullptr && second->work_minutes() == 1440,
                  "assignment 2 work is 1,440 minutes (Task B, 3 days)");
    const mpp::Assignment* third = find_assignment(base, 3);
    context.check(third != nullptr && third->work_minutes() == 960,
                  "assignment 3 work is 960 minutes (Task C, 2 days)");

    const mpp::Document probe_v2 = mpp::load_file(probe + "/probe-v2.mpp");
    const mpp::Assignment* longer = find_assignment(probe_v2, 1);
    context.check(longer != nullptr, "probe-v2 assignment 1 is present");
    if (longer != nullptr) {
        context.equal(longer->work_x1000, 3360000.0,
                      "probe-v2 assignment 1 work follows 5 d -> 7 d (x1.4)");
        context.equal(longer->work_days(), 7.0, "probe-v2 assignment 1 is 7 days");
    }

    const std::string assign1 = probe + "/assign1.mpp";
    if (file_exists(assign1)) {
        const mpp::Document assignments = mpp::load_file(assign1);
        context.equal(assignments.assignments.size(), std::size_t{3},
                      "assign1.mpp has 3 real assignments (5 rows - 2 placeholders)");
        context.equal(assignments.skipped_assignments.size(), std::size_t{2},
                      "assign1.mpp's two placeholder rows (task uid 0) are skipped");
        const mpp::Assignment* a1 = find_assignment(assignments, 3);
        context.check(a1 != nullptr, "assign1.mpp assignment uid 3 is present");
        if (a1 != nullptr) {
            context.equal(a1->task_uid, std::uint32_t{1}, "assign1 T1/R1 task uid");
            context.equal(a1->resource_uid(), std::uint32_t{1},
                          "assign1 T1/R1 resource uid from +8");
            context.check(a1->has_resource(), "assign1 T1/R1 has a resource");
            context.equal(a1->work_minutes(), std::uint32_t{1440},
                          "assign1 T1/R1 work is 1,440 minutes");
            context.equal(a1->units_percent(), std::uint32_t{100}, "assign1 T1/R1 units");
        }
        const mpp::Assignment* a2 = find_assignment(assignments, 4);
        context.check(a2 != nullptr && a2->task_uid == 1 && a2->resource_uid() == 2,
                      "assign1 T1/R2 is task 1 / resource 2");
        const mpp::Assignment* a3 = find_assignment(assignments, 5);
        context.check(a3 != nullptr && a3->task_uid == 2 && a3->resource_uid() == 1 &&
                          a3->work_minutes() == 960,
                      "assign1 T2/R1 is task 2 / resource 1 with 960 minutes");
    }

    const std::string link1 = probe + "/link1.mpp";
    if (file_exists(link1)) {
        const mpp::Document assignments = mpp::load_file(link1);
        context.equal(assignments.assignments.size(), std::size_t{4},
                      "link1.mpp has one unassigned assignment row per task");
        const std::ptrdiff_t assigned = std::count_if(
            assignments.assignments.begin(), assignments.assignments.end(),
            [](const mpp::Assignment& assignment) { return assignment.has_resource(); });
        context.equal(assigned, std::ptrdiff_t{0},
                      "link1.mpp's assignments have +8 = 0, also 'no resource'");
    }

    const mpp::Document project_b = mpp::load_file(MPP_PROJECT_B_PATH);
    context.equal(project_b.assignments.size(), std::size_t{73},
                  "PROJECT_B has 73 assignments (8030 = 110 x 73)");
    context.equal(project_b.skipped_assignments.size(), std::size_t{0},
                  "PROJECT_B has no skipped assignment rows");
    const std::ptrdiff_t project_b_null = std::count_if(
        project_b.assignments.begin(), project_b.assignments.end(),
        [](const mpp::Assignment& assignment) {
            return assignment.resource_uid_raw == mpp::kNullResourceUid;
        });
    context.equal(project_b_null, std::ptrdiff_t{73},
                  "every PROJECT_B assignment carries the null-resource sentinel");

    const mpp::Document project_a = mpp::load_file(MPP_PROJECT_A_PATH);
    context.equal(project_a.assignments.size(), std::size_t{67},
                  "PROJECT_A has 67 live assignments (69 rows - 2 uncovered)");
    context.equal(project_a.skipped_assignments.size(), std::size_t{2},
                  "PROJECT_A's two uncovered assignment rows are skipped");
    const mpp::Assignment* project_a_first = find_assignment(project_a, 7475);
    context.check(project_a_first != nullptr, "PROJECT_A assignment uid 7475 is present");
    if (project_a_first != nullptr) {
        context.equal(project_a_first->task_uid, std::uint32_t{8142}, "PROJECT_A assignment 7475 task uid");
        context.equal(project_a_first->resource_uid_raw, mpp::kNullResourceUid,
                      "PROJECT_A assignment 7475 has the null-resource sentinel");
        context.equal(project_a_first->units_percent(), std::uint32_t{100},
                      "PROJECT_A assignment 7475 units");
    }
    const std::ptrdiff_t project_a_null = std::count_if(
        project_a.assignments.begin(), project_a.assignments.end(),
        [](const mpp::Assignment& assignment) {
            return assignment.resource_uid_raw == mpp::kNullResourceUid;
        });
    context.equal(project_a_null, std::ptrdiff_t{67},
                  "all 67 live PROJECT_A assignments carry the null-resource sentinel");

    /* ---- W31 dates, W40 identity.  +52 is the assignment Start and +56 its
     * Finish: both equal the task's own effective pair on every live assignment
     * of these files.  +60 is the assignment's **Resume** date, measured against
     * Project's own export (21 non-empty <Resume> elements, 21/21 equal; the
     * golden TSV below).  It is not Stop: the two split rows have Stop one
     * working interval earlier.  Counts only, no names. */
    struct DateCounts {
        std::size_t assignments = 0;
        std::size_t start_matches = 0;
        std::size_t finish_matches = 0;
        std::size_t resume_is_start = 0;
        std::size_t resume_is_finish = 0;
        std::size_t resume_between = 0;
    };
    const auto count_dates = [](const mpp::Document& document) {
        DateCounts counts;
        for (const mpp::Assignment& assignment : document.assignments) {
            const auto found =
                std::find_if(document.tasks.begin(), document.tasks.end(),
                             [&assignment](const mpp::Task& task) {
                                 return task.uid == assignment.task_uid;
                             });
            if (found == document.tasks.end()) {
                continue;
            }
            ++counts.assignments;
            counts.start_matches += assignment.start.raw == found->start.raw ? 1u : 0u;
            counts.finish_matches += assignment.finish.raw == found->finish.raw ? 1u : 0u;
            if (assignment.resume.raw == assignment.start.raw) {
                ++counts.resume_is_start;
            } else if (assignment.resume.raw == assignment.finish.raw) {
                ++counts.resume_is_finish;
            } else if (assignment.resume.raw > assignment.start.raw &&
                       assignment.resume.raw < assignment.finish.raw) {
                ++counts.resume_between;
            }
        }
        return counts;
    };
    const DateCounts project_a_dates = count_dates(project_a);
    context.equal(project_a_dates.start_matches, project_a_dates.assignments,
                  "every live PROJECT_A assignment's +52 equals its task's Start");
    context.equal(project_a_dates.finish_matches, project_a_dates.assignments,
                  "every live PROJECT_A assignment's +56 equals its task's Finish");
    context.equal(project_a_dates.resume_is_start, std::size_t{51},
                  "51 zero-percent PROJECT_A assignments store Start at +60 (Resume)");
    context.equal(project_a_dates.resume_is_finish, std::size_t{13},
                  "13 completed PROJECT_A assignments store Finish at +60 (Resume)");
    context.equal(project_a_dates.resume_between, std::size_t{3},
                  "3 partial PROJECT_A assignments (10 %, 40 %, 40 %) store a date "
                  "strictly between +52 and +56 at +60 (Resume)");
    context.equal(project_a_dates.assignments, std::size_t{67},
                  "all 67 live PROJECT_A assignments resolve to a live task");

    const DateCounts project_b_dates = count_dates(project_b);
    /* PROJECT_B's assignment table is stale in the same way as its links: 10 of
     * its 73 rows name task uids that only exist in deleted task slots (8141,
     * 8142, 10637, 10722..10727, 10732), so only 63 resolve to a live task. */
    context.equal(project_b_dates.assignments, std::size_t{63},
                  "63 of PROJECT_B's 73 live assignment rows resolve to a live task");
    context.equal(project_b_dates.start_matches, std::size_t{63},
                  "all 63 resolved PROJECT_B +52 dates equal their task's effective Start");
    context.equal(project_b_dates.finish_matches, std::size_t{63},
                  "all 63 resolved PROJECT_B +56 dates equal their task's effective Finish");
    context.equal(project_b_dates.resume_is_start, std::size_t{63},
                  "every resolved PROJECT_B assignment is at 0 %, so +60 equals Start");

    const DateCounts link1_dates = count_dates(mpp::load_file(probe + "/link1.mpp"));
    context.equal(link1_dates.start_matches, std::size_t{4},
                  "all 4 link1.mpp assignment starts match their task Start");
    context.equal(link1_dates.finish_matches, std::size_t{4},
                  "all 4 link1.mpp assignment finishes match their task Finish");
    context.equal(link1_dates.resume_is_start, std::size_t{4},
                  "all 4 link1.mpp assignments store Start at +60");

    if (file_exists(assign1)) {
        const DateCounts assign1_dates = count_dates(mpp::load_file(assign1));
        context.equal(assign1_dates.assignments, std::size_t{3},
                      "assign1.mpp's 3 real assignments resolve to a task");
        context.equal(assign1_dates.start_matches, std::size_t{3},
                      "assign1.mpp +52 is the task Start on all 3 assignments");
        context.equal(assign1_dates.finish_matches, std::size_t{3},
                      "assign1.mpp +56 is the task Finish on all 3 assignments");
    }

    /* ---- W40: the oracle's own assignment dates, committed as
     * tests/mpp_assignment_dates_golden.tsv from Project's MSPDI export of PROJECT_A:
     * 67 uids, dates only (no names).  This is the independent proof that +60
     * is Resume, not Stop. */
#ifndef MPP_ASSIGNMENT_GOLDEN_PATH
#define MPP_ASSIGNMENT_GOLDEN_PATH ""
#endif
    const std::string assignment_golden = MPP_ASSIGNMENT_GOLDEN_PATH;
    if (assignment_golden.empty() || !file_exists(assignment_golden)) {
        std::cout << "SKIP: " << assignment_golden
                  << " is not present (no assignment date oracle)\n";
        return;
    }
    struct GoldenAssignmentDate {
        std::uint32_t uid = 0;
        std::uint32_t task_uid = 0;
        std::string start;
        std::string finish;
        std::string stop;
        std::string resume;
    };
    std::vector<GoldenAssignmentDate> golden_rows;
    for (const std::string& line : read_lines(assignment_golden)) {
        if (line.empty() || line[0] == 'u') {
            continue; /* header */
        }
        const std::vector<std::string> fields = split_tabs(line);
        if (fields.size() < 6) {
            context.check(false, "assignment golden line has fewer than 6 fields");
            continue;
        }
        GoldenAssignmentDate row;
        row.uid = static_cast<std::uint32_t>(std::stoul(fields[0]));
        row.task_uid = static_cast<std::uint32_t>(std::stoul(fields[1]));
        row.start = fields[2];
        row.finish = fields[3];
        row.stop = fields[4];
        row.resume = fields[5];
        golden_rows.push_back(row);
    }
    context.equal(golden_rows.size(), std::size_t{67},
                  "assignment date golden holds all 67 PROJECT_A assignment uids");
    std::size_t golden_start = 0;
    std::size_t golden_finish = 0;
    std::size_t golden_resume_present = 0;
    std::size_t golden_resume_match = 0;
    std::size_t golden_stop_equal_resume = 0;
    std::size_t golden_stop_differs = 0;
    std::size_t golden_resume_absent = 0;
    for (const GoldenAssignmentDate& row : golden_rows) {
        const mpp::Assignment* assignment = find_assignment(project_a, row.uid);
        context.check(assignment != nullptr,
                      "PROJECT_A golden assignment uid " + std::to_string(row.uid) +
                          " is present in the reader");
        if (assignment == nullptr) {
            continue;
        }
        const auto iso = [](const mpp::PackedDate& date) {
            std::string text = mpp::format_date_iso(date);
            if (!text.empty()) {
                text[10] = 'T';
                if (text.size() > 6) {
                    text.resize(text.size() - 6); /* drop "+00:00" */
                }
            }
            return text;
        };
        if (iso(assignment->start) == row.start) {
            ++golden_start;
        }
        if (iso(assignment->finish) == row.finish) {
            ++golden_finish;
        }
        if (!row.resume.empty()) {
            ++golden_resume_present;
            if (iso(assignment->resume) == row.resume) {
                ++golden_resume_match;
            }
            if (row.stop == row.resume) {
                ++golden_stop_equal_resume;
            } else {
                ++golden_stop_differs;
                context.check(iso(assignment->resume) != row.stop,
                              "uid " + std::to_string(row.uid) +
                                  ": +60 is Resume, not the earlier Stop");
            }
        } else {
            ++golden_resume_absent;
        }
    }
    context.equal(golden_start, std::size_t{67},
                  "all 67 assignment Starts equal Project's export");
    context.equal(golden_finish, std::size_t{67},
                  "all 67 assignment Finishes equal Project's export");
    context.equal(golden_resume_present, std::size_t{21},
                  "Project's export carries Resume on 21 assignments");
    context.equal(golden_resume_match, std::size_t{21},
                  "all 21 exported Resume dates equal the reader's +60");
    context.equal(golden_stop_equal_resume, std::size_t{19},
                  "on 19 rows Stop and Resume coincide");
    context.equal(golden_stop_differs, std::size_t{2},
                  "on 2 split rows Stop and Resume differ, and +60 is Resume");
    context.equal(golden_resume_absent, std::size_t{46},
                  "the other 46 assignments export no Resume (stored value is "
                  "Start or Finish)");
}

int run_tests() {
    TestContext context;
    test_formatting(context);
    test_synthetic_fixture(context);
    test_project_a(context);
    test_project_b(context);
    test_outline_fixture(context);
    test_status_fixture(context);
    test_w31_fixed_fields(context);
    test_schedin_fixture(context);
    test_probe_fixtures(context);
    test_bad_input(context);
    test_resources(context);
    test_links(context);
    test_assignments(context);

    if (context.failures() != 0) {
        std::cerr << "FAIL: " << context.failures() << " failed of " << context.checks()
                  << " checks" << std::endl;
        return 1;
    }
    std::cout << "PASS: " << context.checks() << " checks" << std::endl;
    return 0;
}

}  // namespace

int main() {
    /* Before anything else: never raise a modal CRT dialog on a build machine.
       MSVC-only: clang and gcc have no CRT error-mode dialog to suppress, and
       calling it unguarded breaks both (CI, 2026-10-22). */
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
#endif
    std::cout << std::unitbuf;
    try {
        return run_tests();
    } catch (const std::exception& error) {
        std::cerr << "FATAL: unhandled exception: " << error.what() << std::endl;
        return 2;
    } catch (...) {
        std::cerr << "FATAL: unhandled non-standard exception" << std::endl;
        return 2;
    }
}
