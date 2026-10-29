/*
 * cfb_hardening_tests.cpp -- wave 33 G2: bounded CFB parsing.
 *
 * The read-only reader (core/mpp/cfb.cpp) and the writer-side container parser
 * (core/mppwrite/cfb_writer.cpp) consume the same untrusted bytes.  The reader
 * has had file-size bounds and visited sets since wave 20; the writer's parser
 * did not.  Each case here patches the committed synthetic fixture IN MEMORY
 * (nothing under tests/data is written) and requires BOTH parsers to refuse the
 * same bytes promptly:
 *
 *   W33-2.1 (D-2)  a DIFAT continuation sector whose last word points at
 *                  itself.  The header's num_difat is a u32 the file controls,
 *                  so the walk must be bounded by the file's sector count and
 *                  must visit each sector at most once.  Before the fix the
 *                  writer-side walk appended 127 ids per step until bad_alloc.
 *   W33-2.2 (D-5)  a Resource Fixed2Meta stream that is header-only with a
 *                  declared entry count of 0xFFFFFFFF.  The count must be
 *                  bounded by the stream's bytes.
 *   W33-2.3 (D-6)  a header claiming CFB v4 (4096-byte sectors).  No v4 file
 *                  exists to measure, so the parsers refuse it by design with a
 *                  message that names v4 rather than a misleading sector-size
 *                  error.
 *
 * A missing committed fixture is a FAILURE with its path, never a skip (the
 * wave-33 G3 rule).  Run with `--case difat|fixed2meta|v4` to run one case.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "reader.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "tests/data"
#endif

namespace {

using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;

/* Wall-clock ceiling for a refusal.  A parser that refuses damaged bytes does
 * no work proportional to the claimed count, so anything slower than this is
 * the unbounded loop under test, not a slow machine. */
constexpr double kRefusalCeilingMs = 100.0;

class TestContext {
  public:
    void check(bool condition, std::string const& what) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::cout << "FAIL: " << what << "\n";
        }
    }
    int report() const {
        if (failures_ == 0) {
            std::cout << "PASS: " << checks_ << " checks" << std::endl;
            return 0;
        }
        std::cout << "FAIL: " << failures_ << " failed of " << checks_
                  << " checks" << std::endl;
        return 1;
    }
    int failures() const { return failures_; }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

std::filesystem::path fixture_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "synthetic-base.mpp";
}

bool read_fixture(Bytes& bytes, TestContext& t) {
    const std::filesystem::path path = fixture_path();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        t.check(false, "the committed fixture is missing: " + path.string());
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0) {
        t.check(false, "cannot size the committed fixture: " + path.string());
        return false;
    }
    input.seekg(0, std::ios::beg);
    bytes.assign(static_cast<std::size_t>(length), 0);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            t.check(false, "short read on the committed fixture: " + path.string());
            return false;
        }
    }
    return true;
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

/* "123.4 ms" for the receipts. */
std::string ms_text(double millis) {
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.1f ms", millis);
    return text;
}

struct Refusal {
    bool clean = false;    /* threw the parser's own error type */
    bool wrong_type = false;
    std::string message;
    double millis = 0.0;
};

Refusal reader_refusal(Bytes bytes) {
    Refusal refusal;
    const Clock::time_point start = Clock::now();
    try {
        const mpp::Document document = mpp::load_bytes(std::move(bytes));
        (void)document;
    } catch (const mpp::Error& error) {
        refusal.clean = true;
        refusal.message = error.what();
    } catch (const std::exception& error) {
        refusal.wrong_type = true;
        refusal.message = std::string("wrong exception type: ") + error.what();
    } catch (...) {
        refusal.wrong_type = true;
        refusal.message = "wrong exception type (not std)";
    }
    refusal.millis = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return refusal;
}

Refusal writer_refusal(Bytes bytes) {
    Refusal refusal;
    const Clock::time_point start = Clock::now();
    try {
        const mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_bytes(std::move(bytes));
        (void)document;
    } catch (const mppwrite::Error& error) {
        refusal.clean = true;
        refusal.message = error.what();
    } catch (const std::exception& error) {
        refusal.wrong_type = true;
        refusal.message = std::string("wrong exception type: ") + error.what();
    } catch (...) {
        refusal.wrong_type = true;
        refusal.message = "wrong exception type (not std)";
    }
    refusal.millis = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return refusal;
}

bool contains(std::string const& text, std::string const& needle) {
    return text.find(needle) != std::string::npos;
}

void check_refusal(TestContext& t, Refusal const& refusal, std::string const& parser,
                   std::string const& expected) {
    t.check(refusal.clean,
            parser + " refuses the damaged container; got: " +
                (refusal.message.empty() ? "no error" : refusal.message));
    if (refusal.clean) {
        t.check(contains(refusal.message, expected),
                parser + " names the damage (\"" + expected + "\"): " +
                    refusal.message);
    }
    t.check(refusal.millis < kRefusalCeilingMs,
            parser + " refuses it in under 0.1 s (" + ms_text(refusal.millis) + ")");
}

/* ---- W33-2.1: the writer-side DIFAT walk ------------------------------- */

/*
 * Patches the container so the first DIFAT continuation sector is sector 0 and
 * its last word points back at sector 0.  The sector's 127 payload words are
 * marked free (0xFFFFFFFF), so a parser without a visited set appends only the
 * one continuation id per step while it spins -- the loop is still unbounded,
 * but a pre-fix run does not consume a gigabyte per second while proving it.
 */
Bytes self_pointing_difat(Bytes const& pristine, std::uint32_t declared) {
    Bytes bytes = pristine;
    const std::size_t sector_size = 512;
    const std::size_t sector0 = sector_size;  /* after the 512-byte header */
    const std::size_t per_sector = sector_size / 4;
    if (bytes.size() < sector0 + sector_size) {
        return {};
    }
    for (std::size_t i = 0; i + 1 < per_sector; ++i) {
        put_u32(bytes, sector0 + 4 * i, 0xFFFFFFFFu);
    }
    put_u32(bytes, sector0 + (per_sector - 1) * 4, 0u); /* self-point */
    put_u32(bytes, 0x44, 0u);                            /* first DIFAT = 0 */
    put_u32(bytes, 0x48, declared);                      /* num_difat */
    return bytes;
}

void test_self_pointing_difat(TestContext& t) {
    Bytes pristine;
    if (!read_fixture(pristine, t)) {
        return;
    }
    /* Warm the file/caches and prove the fixture is readable by both parsers
     * before the damaged variants below. */
    const Refusal baseline_reader = reader_refusal(pristine);
    t.check(!baseline_reader.wrong_type && baseline_reader.message.empty(),
            "the pristine fixture loads through mpp::load_bytes");
    {
        const mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_bytes(pristine);
        t.check(document.stream_count() > 0,
                "the pristine fixture loads through CfbDocument::from_bytes");
    }

    const std::uint32_t sector_total = static_cast<std::uint32_t>(
        (pristine.size() - 512) / 512);

    /* The brief's fixture: num_difat = 0xFFFFFFFF.  Both parsers bound the
     * count by the file's sectors, so they refuse before walking. */
    const Bytes huge = self_pointing_difat(pristine, 0xFFFFFFFFu);
    t.check(!huge.empty(), "the 0xFFFFFFFF DIFAT fixture was built");
    if (!huge.empty()) {
        check_refusal(t, reader_refusal(huge), "mpp::load_bytes", "DIFAT");
        check_refusal(t, writer_refusal(huge), "CfbDocument::from_bytes", "DIFAT");
    }

    /* A count the file could plausibly contain: the count bound cannot catch
     * this one, so only the visited set ends the self-pointing chain. */
    const Bytes loop = self_pointing_difat(pristine, sector_total);
    t.check(!loop.empty(), "the self-pointing DIFAT fixture was built");
    if (!loop.empty()) {
        check_refusal(t, reader_refusal(loop), "mpp::load_bytes", "loops");
        check_refusal(t, writer_refusal(loop), "CfbDocument::from_bytes", "loops");
    }
}

/* ---- W33-2.2: the Resource Fixed2Meta declared count -------------------- */

/*
 * Rebuilds the committed fixture with the Resource Fixed2Meta stream replaced
 * by a 16-byte header-only stream that declares 0xFFFFFFFF entries.  The
 * reader's count bound must come from the stream's bytes, not from that word;
 * before the fix the row loop ran 4.3 billion cheap iterations (~2.2 s on the
 * audit's reader_dump build) and still returned the same rows.
 */
bool resource_fixed2_hang_fixture(Bytes const& pristine, Bytes& patched,
                                  TestContext& t) {
    mppwrite::CfbDocument document =
        mppwrite::CfbDocument::from_bytes(pristine);
    std::string path;
    for (std::string const& candidate : document.stream_paths()) {
        if (contains(candidate, "Rsc") && contains(candidate, "Fixed2Meta")) {
            path = candidate;
            break;
        }
    }
    t.check(!path.empty(), "the fixture has a Resource Fixed2Meta stream");
    if (path.empty()) {
        return false;
    }
    Bytes payload(16, 0);
    put_u32(payload, 0, 0xFADFADBAu);   /* the FixedMeta magic */
    put_u32(payload, 8, 0xFFFFFFFFu);   /* the declared entry count */
    document.replace_stream(path, payload);
    patched = document.to_bytes();
    t.check(patched.size() >= 1024, "the patched container was emitted");
    return patched.size() >= 1024;
}

struct LoadResult {
    bool ok = false;
    std::string error;
    std::vector<std::uint32_t> task_uids;
    std::vector<std::uint32_t> resource_uids;
    double millis = 0.0;
};

LoadResult timed_load(Bytes bytes) {
    LoadResult result;
    const Clock::time_point start = Clock::now();
    try {
        const mpp::Document document = mpp::load_bytes(std::move(bytes));
        result.ok = true;
        for (const mpp::Task& task : document.tasks) {
            result.task_uids.push_back(task.uid);
        }
        for (const mpp::Resource& resource : document.resources) {
            result.resource_uids.push_back(resource.uid);
        }
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    result.millis = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

void test_resource_fixed2_count(TestContext& t) {
    Bytes pristine;
    if (!read_fixture(pristine, t)) {
        return;
    }
    const LoadResult baseline = timed_load(pristine);
    t.check(baseline.ok, "the pristine fixture loads: " + baseline.error);

    Bytes patched;
    if (!resource_fixed2_hang_fixture(pristine, patched, t)) {
        return;
    }
    const LoadResult result = timed_load(patched);
    t.check(result.ok, "the Fixed2Meta fixture still loads: " + result.error);
    if (!result.ok) {
        return;
    }
    t.check(result.millis < kRefusalCeilingMs,
            "the row loop is bounded by the stream size; loaded in " +
                ms_text(result.millis));
    t.check(result.task_uids == baseline.task_uids,
            "the damaged metadata does not change the task rows");
    t.check(result.resource_uids == baseline.resource_uids,
            "the damaged metadata does not change the resource rows");
}

}  // namespace

int main(int argc, char** argv) {
    std::string only;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--case" && i + 1 < argc) {
            only = argv[++i];
        } else {
            std::cout << "usage: mpp_cfb_hardening [--case difat|fixed2meta|v4]\n";
            return 2;
        }
    }
    std::cout << std::unitbuf;
    TestContext context;
    if (only.empty() || only == "difat") {
        test_self_pointing_difat(context);
    }
    if (only.empty() || only == "fixed2meta") {
        test_resource_fixed2_count(context);
    }
    if (!only.empty() && only != "difat" && only != "fixed2meta") {
        std::cout << "unknown case: " << only << "\n";
        return 2;
    }
    return context.report();
}
