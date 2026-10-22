/*
 * roundtrip_cfb.cpp -- stage 1 of the writer: the container round tax.
 *
 * CLAIM UNDER TEST.  For every .mpp in corpus/probe (and the two real projects
 * in corpus/), parsing the CFB container and re-emitting it from the parsed
 * model produces a byte-identical file.  Nothing in the container is carried
 * through as an opaque blob: the FAT, mini FAT, DIFAT, directory sectors and
 * every chain are rebuilt from the model, so equality means the model is
 * complete.
 *
 * The model is deliberately not "the input bytes": the FAT is *derived* from
 * the sector chains (cfb_writer.h), so a wrong chain or a wrong FAT mark shows
 * up as a byte difference.  This test also asserts the supporting facts the
 * derivation rests on:
 *
 *   - no free sectors inside the file (all sectors are owned by something);
 *   - every sector in the file is claimed by exactly one structure;
 *   - every byte of a sector that no structure claims is zero;
 *   - emitted bytes re-parse to the same document (idempotence);
 *   - the anchor facts of corpus/probe/base.mpp that the record layer uses
 *     (Task FixedData = 48 + 4*202, uid at +4, priority at +78, ...).
 *
 * Paths are injected by CMake: MPPWRITE_PROBE_DIR, MPPWRITE_PROJECT_A_PATH,
 * MPPWRITE_PROJECT_B_PATH.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"

/* CMake injects absolute paths.  The relative defaults let the test be run by
 * hand from the repository root as well. */
#ifndef MPPWRITE_PROBE_DIR
#define MPPWRITE_PROBE_DIR "corpus/probe"
#endif
#ifndef MPPWRITE_PROJECT_A_PATH
#define MPPWRITE_PROJECT_A_PATH "corpus/project_a.mpp"
#endif
#ifndef MPPWRITE_PROJECT_B_PATH
#define MPPWRITE_PROJECT_B_PATH "corpus/project_b.mpp"
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
    int report() const {
        std::cout << checks_ << " checks, " << failures_ << " failures\n";
        return failures_ == 0 ? 0 : 1;
    }
    int failures() const { return failures_; }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

/* Not called by every build of this test (the corpus-dependent path uses the
 * container's parser instead), but kept for hand runs against a single file.
 * clang and GCC warn about an unused internal-linkage function; MSVC does not. */
[[maybe_unused]] std::vector<std::uint8_t> read_file(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path);
    }
    input.seekg(0, std::ios::end);
    std::streamoff const length = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

std::vector<std::string> mpp_files_in(std::string const& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".mpp") {
            out.push_back(entry.path().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string base_name(std::string const& path) {
    return std::filesystem::path(path).filename().string();
}

/* corpus/ is git-ignored because it holds a client's real programme, so a
 * build machine may not have it.  A missing corpus is an environment
 * difference, not a test failure: the caller prints SKIP and exits 0. */
bool corpus_available(std::string& missing) {
    std::string const required[] = {MPPWRITE_PROBE_DIR, MPPWRITE_PROJECT_A_PATH,
                                    MPPWRITE_PROJECT_B_PATH};
    for (std::string const& path : required) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            missing = path;
            return false;
        }
    }
    return true;
}

std::string first_difference(std::vector<std::uint8_t> const& a,
                             std::vector<std::uint8_t> const& b) {
    std::size_t const limit = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < limit; ++i) {
        if (a[i] != b[i]) {
            return "sizes " + std::to_string(a.size()) + "/" +
                   std::to_string(b.size()) + ", first difference at byte " +
                   std::to_string(i);
        }
    }
    if (a.size() != b.size()) {
        return "sizes differ: " + std::to_string(a.size()) + " vs " +
               std::to_string(b.size());
    }
    return "identical";
}

/* Everything that must hold for one file.  Returns false when the document
 * could not be parsed at all, so a corrupt file is a failure rather than a
 * crash. */
bool roundtrip_file(TestContext& t, std::string const& path) {
    std::string const name = base_name(path);
    std::optional<mppwrite::CfbDocument> parsed;
    try {
        parsed = mppwrite::CfbDocument::from_file(path);
    } catch (std::exception const& e) {
        t.check(false, name + ": parse threw: " + e.what());
        return false;
    }
    mppwrite::CfbDocument const& doc = *parsed;

    std::string const check = doc.check();
    t.check(check.empty(), name + ": check(): " + check);

    /* 1. byte-identical re-emission */
    std::vector<std::uint8_t> const emitted = doc.to_bytes();
    bool const identical = emitted == doc.source_bytes();
    t.check(identical, name + ": re-emitted bytes differ (" +
                           first_difference(emitted, doc.source_bytes()) + ")");
    if (!identical) {
        return false; /* later claims would be noise */
    }

    /* 2. idempotence: emit, re-parse, emit again */
    mppwrite::CfbDocument again = mppwrite::CfbDocument::from_bytes(emitted);
    t.check(again.check().empty(), name + ": re-parsed check(): " + again.check());
    t.check(again.to_bytes() == emitted, name + ": second emission differs");

    /* 3. the stream set survives the trip, payloads included */
    std::vector<std::string> const paths = doc.stream_paths();
    t.check(again.stream_count() == doc.stream_count(),
            name + ": stream count changes across a round trip");
    bool payloads_equal = true;
    for (std::string const& p : paths) {
        if (!again.has_stream(p) || again.read_stream(p) != doc.read_stream(p)) {
            payloads_equal = false;
            break;
        }
    }
    t.check(payloads_equal, name + ": a stream payload changed across a trip");

    /* 4. the facts the FAT derivation rests on */
    std::size_t const sectors = doc.sector_count();
    std::vector<std::uint32_t> const& fat = doc.fat();
    t.check(!fat.empty(), name + ": empty allocation table");
    std::size_t free_inside = 0;
    for (std::size_t sid = 0; sid < sectors && sid < fat.size(); ++sid) {
        if (fat[sid] == mppwrite::kFreeSector) {
            ++free_inside;
        }
    }
    t.check(free_inside == 0,
            name + ": " + std::to_string(free_inside) +
                " free sectors inside the file (round trip assumed none)");

    std::vector<std::uint32_t> const used = doc.sectors_used();
    t.check(used.size() == sectors,
            name + ": " + std::to_string(used.size()) + " owned sectors for " +
                std::to_string(sectors) + " in the file");

    /* 5. bytes in sectors nobody owns are zero */
    std::size_t non_zero_free_bytes = 0;
    {
        std::vector<bool> owned(sectors, false);
        for (std::uint32_t const sid : used) {
            if (sid < sectors) {
                owned[sid] = true;
            }
        }
        std::vector<std::uint8_t> const& raw = doc.source_bytes();
        for (std::size_t sid = 0; sid < sectors; ++sid) {
            if (owned[sid]) {
                continue;
            }
            std::size_t const base = 512 + sid * doc.sector_size();
            for (std::size_t i = 0; i < doc.sector_size(); ++i) {
                if (raw[base + i] != 0) {
                    ++non_zero_free_bytes;
                }
            }
        }
    }
    t.check(non_zero_free_bytes == 0,
            name + ": " + std::to_string(non_zero_free_bytes) +
                " non-zero bytes in unowned sectors");

    /* 6. the container this project targets */
    t.check(doc.major_version() == 3, name + ": major version is not 3");
    t.check(doc.sector_size() == 512, name + ": sector size is not 512");
    t.check(doc.mini_sector_size() == 64, name + ": mini sector size is not 64");
    t.check(doc.mini_cutoff() == 4096, name + ": mini cutoff is not 4096");
    return true;
}

/* corpus/probe/base.mpp is the reference fixture.  These are the numbers the
 * record layer (spec/mpp-records.md) depends on, re-checked from the writer's
 * own parser so a container change cannot silently break the row offsets. */
void check_base_fixture(TestContext& t, std::string const& path) {
    std::optional<mppwrite::CfbDocument> parsed;
    try {
        parsed = mppwrite::CfbDocument::from_file(path);
    } catch (std::exception const& e) {
        t.check(false, std::string("base.mpp: parse threw: ") + e.what());
        return;
    }
    mppwrite::CfbDocument const& doc = *parsed;

    t.check(doc.has_stream("114/TBkndTask/FixedData"),
            "base.mpp: no 114/TBkndTask/FixedData");
    t.check(doc.has_stream("114/TBkndTask/VarMeta"),
            "base.mpp: no 114/TBkndTask/VarMeta");
    t.check(doc.has_stream("114/TBkndTask/Var2Data"),
            "base.mpp: no 114/TBkndTask/Var2Data");
    t.check(doc.has_stream("114/TBkndTask/FixedMeta"),
            "base.mpp: no 114/TBkndTask/FixedMeta");
    t.check(doc.has_stream("214/CV_iew/Var2Data"),
            "base.mpp: no 214/CV_iew/Var2Data");

    /* storage names keep their padding, so the stored path is the only place
     * the leading spaces are visible */
    bool stored_padding = false;
    for (std::string const& p : doc.stored_stream_paths()) {
        if (p == "   114/TBkndTask/FixedData") {
            stored_padding = true;
            break;
        }
    }
    t.check(stored_padding,
            "base.mpp: stored path is not '   114/TBkndTask/FixedData'");

    std::vector<std::uint8_t> const fixed =
        doc.read_stream("114/TBkndTask/FixedData");
    t.check(fixed.size() == 856, "base.mpp: FixedData is not 856 bytes");
    t.check((fixed.size() - 48) % 202 == 0,
            "base.mpp: FixedData is not 48 + 202*n");
    std::size_t const rows = (fixed.size() - 48) / 202;
    t.check(rows == 4, "base.mpp: rows is not 4 (project + 3 tasks)");

    auto u16 = [&](std::size_t row, std::size_t off) {
        std::size_t const p = 48 + row * 202 + off;
        return static_cast<std::uint16_t>(fixed[p] | (fixed[p + 1] << 8));
    };
    auto u32 = [&](std::size_t row, std::size_t off) {
        std::size_t const p = 48 + row * 202 + off;
        return static_cast<std::uint32_t>(fixed[p]) |
               (static_cast<std::uint32_t>(fixed[p + 1]) << 8) |
               (static_cast<std::uint32_t>(fixed[p + 2]) << 16) |
               (static_cast<std::uint32_t>(fixed[p + 3]) << 24);
    };
    t.check(u32(0, 0) == 0 && u32(0, 4) == 0,
            "base.mpp: row 0 is not the project row (id 0, uid 0)");
    t.check(u32(1, 4) == 1 && u32(2, 4) == 2 && u32(3, 4) == 3,
            "base.mpp: task uids are not 1,2,3");
    for (std::size_t row = 0; row < rows; ++row) {
        if (u16(row, 78) != 500) {
            t.check(false, "base.mpp: priority at +78 is not 500 in row " +
                               std::to_string(row));
        }
    }
    t.check(u32(1, 84) == 24000, "base.mpp: task A duration is not 24000 ticks");
    t.check(u32(2, 84) == 14400, "base.mpp: task B duration is not 14400 ticks");
    t.check(u32(3, 84) == 9600, "base.mpp: task C duration is not 9600 ticks");
    t.check(u32(1, 96) == 0x3bf212c0u,
            "base.mpp: task A start is not 2026-01-05 08:00");
    t.check(u16(1, 92) == 0, "base.mpp: task A percent complete is not 0");
}

} /* namespace */

int main(int argc, char** argv) {
    TestContext t;
    try {
        std::string missing;
        if (!corpus_available(missing)) {
            std::cout << "SKIP: corpus not present (" << missing
                      << " is missing; corpus/ is git-ignored)\n";
            return 0;
        }
        std::vector<std::string> files = mpp_files_in(MPPWRITE_PROBE_DIR);
        if (files.empty()) {
            std::cout << "no probe files: check MPPWRITE_PROBE_DIR\n";
            return 1;
        }
        for (std::string const& path : files) {
            if (!roundtrip_file(t, path)) {
                std::cout << "stopping after a failed round trip on "
                          << base_name(path) << "\n";
                break;
            }
        }
        for (std::string const& path : files) {
            if (base_name(path) == "base.mpp") {
                check_base_fixture(t, path);
                break;
            }
        }
        roundtrip_file(t, MPPWRITE_PROJECT_A_PATH);
        roundtrip_file(t, MPPWRITE_PROJECT_B_PATH);
        if (argc > 1) {
            for (int i = 1; i < argc; ++i) {
                roundtrip_file(t, argv[i]);
            }
        }
    } catch (std::exception const& e) {
        std::cout << "fatal: " << e.what() << "\n";
        return 1;
    }
    return t.report();
}
