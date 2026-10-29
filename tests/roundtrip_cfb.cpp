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
#include "fixture_guard.h"

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

/* ---- synthetic FAT slack (no corpus needed) ------------------------------
 * A file may carry trailing sectors that no chain references: MS Project writes
 * them (corpus/imported/sf248.mpp has a 768-entry FAT for 959 sectors, so 191
 * sectors are slack).  The rule under test mirrors core/mpp/cfb.cpp: a FAT
 * shorter than the file is a WARNING and the read continues; a chain that
 * points past the FAT is a positioned Error.  The base below is a minimal valid
 * CFB built from bytes here -- 3 sectors (directory, FAT, one 512-byte stream)
 * -- so the test also runs on a machine with no corpus. */
void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes[offset + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

/* UTF-16LE name plus the u16 byte count at +64 (terminating NUL included). */
void put_name(std::vector<std::uint8_t>& bytes, std::size_t entry,
              std::string_view name) {
    for (std::size_t i = 0; i < name.size(); ++i) {
        bytes[entry + 2 * i] = static_cast<std::uint8_t>(name[i]);
        bytes[entry + 2 * i + 1] = 0;
    }
    put_u16(bytes, entry + 64,
            static_cast<std::uint16_t>(2 * (name.size() + 1)));
}

std::vector<std::uint8_t> minimal_cfb() {
    constexpr std::size_t kSector = 512;
    std::vector<std::uint8_t> bytes(512 + 3 * kSector, 0);

    /* header */
    std::uint8_t const signature[8] = {0xD0, 0xCF, 0x11, 0xE0,
                                       0xA1, 0xB1, 0x1A, 0xE1};
    std::copy(std::begin(signature), std::end(signature), bytes.begin());
    put_u16(bytes, 0x18, 0x003E);       /* minor version */
    put_u16(bytes, 0x1A, 3);            /* major version */
    put_u16(bytes, 0x1C, 0xFFFE);       /* little-endian */
    put_u16(bytes, 0x1E, 9);            /* 512-byte sectors */
    put_u16(bytes, 0x20, 6);            /* 64-byte mini sectors */
    put_u32(bytes, 0x2C, 1);            /* one FAT sector */
    put_u32(bytes, 0x30, 0);            /* directory starts at sector 0 */
    put_u32(bytes, 0x38, 4096);         /* mini cutoff */
    put_u32(bytes, 0x3C, 0xFFFFFFFEu);  /* no mini FAT */
    put_u32(bytes, 0x44, 0xFFFFFFFEu);  /* no DIFAT continuation */
    for (std::size_t i = 0; i < 109; ++i) {
        put_u32(bytes, 0x4C + 4 * i, 0xFFFFFFFFu);
    }
    put_u32(bytes, 0x4C, 1); /* the FAT lives in sector 1 */

    /* directory: sector 0.  Root entry -> one stream entry -> two free slots. */
    std::size_t const dir = kSector;
    put_name(bytes, dir, "Root Entry");
    bytes[dir + 66] = 5; /* root storage */
    bytes[dir + 67] = 1; /* black */
    put_u32(bytes, dir + 68, 0xFFFFFFFFu);
    put_u32(bytes, dir + 72, 0xFFFFFFFFu);
    put_u32(bytes, dir + 76, 1);             /* child: entry 1 */
    put_u32(bytes, dir + 116, 0xFFFFFFFEu);  /* no regular chain */
    std::size_t const stream = dir + 128;
    put_name(bytes, stream, "Stream");
    bytes[stream + 66] = 2; /* stream */
    bytes[stream + 67] = 1;
    put_u32(bytes, stream + 68, 0xFFFFFFFFu);
    put_u32(bytes, stream + 72, 0xFFFFFFFFu);
    put_u32(bytes, stream + 76, 0xFFFFFFFFu);
    put_u32(bytes, stream + 116, 2); /* data sector */
    put_u32(bytes, stream + 120, kSector);

    /* FAT: sector 1. */
    std::size_t const fat = 512 + kSector;
    for (std::size_t i = 0; i < 128; ++i) {
        put_u32(bytes, fat + 4 * i, 0xFFFFFFFFu);
    }
    put_u32(bytes, fat + 0, 0xFFFFFFFEu); /* directory chain ends */
    put_u32(bytes, fat + 4, 0xFFFFFFFDu); /* FAT sector mark */
    put_u32(bytes, fat + 8, 0xFFFFFFFEu); /* stream chain ends */

    /* payload: sector 2. */
    std::size_t const data = 512 + 2 * kSector;
    std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(data),
              bytes.begin() + static_cast<std::ptrdiff_t>(data + kSector),
              std::uint8_t{0xAB});
    return bytes;
}

void test_fat_slack(TestContext& t) {
    std::vector<std::uint8_t> const valid = minimal_cfb();
    std::vector<std::uint8_t> const payload(512, std::uint8_t{0xAB});

    mppwrite::CfbDocument const compact =
        mppwrite::CfbDocument::from_bytes(valid);
    t.check(compact.warnings().empty(),
            "slack: a compact CFB reports no warning (no false positive)");
    t.check(compact.read_stream("Stream") == payload,
            "slack: the synthetic stream reads back");
    t.check(compact.to_bytes() == valid,
            "slack: a compact CFB re-emits byte-identically (the writer's byte "
            "output is unchanged)");

    /* Append 140 unreferenced sectors: 3 + 140 = 143 sectors against a
     * 128-entry FAT, so 15 are slack. */
    std::vector<std::uint8_t> slack = valid;
    slack.resize(valid.size() + 140 * 512, 0);
    mppwrite::CfbDocument const doc = mppwrite::CfbDocument::from_bytes(slack);
    t.check(doc.sector_count() == 143, "slack: the file has 143 sectors");
    t.check(doc.warnings().size() == 1,
            "slack: a short FAT reports exactly one warning");
    if (!doc.warnings().empty()) {
        std::string const& warning = doc.warnings().front();
        t.check(warning.find("15 unreferenced sectors") != std::string::npos &&
                    warning.find("FAT describes 128") != std::string::npos &&
                    warning.find("file's 143") != std::string::npos,
                "slack: the warning counts the 15 slack sectors: " + warning);
    }
    t.check(doc.read_stream("Stream") == payload,
            "slack: the stream still reads when the FAT is short");

    /* A slack container is readable, not round-trippable: the writer derives
     * the FAT from the chains it knows, so it must refuse rather than emit a
     * different file (the header's FAT SLACK contract). */
    t.check(!doc.check().empty() &&
                doc.check().find("not round-trippable") != std::string::npos,
            "slack: check() reports the file as not round-trippable: " +
                doc.check());
    bool reemit_refused = false;
    try {
        std::vector<std::uint8_t> const emitted = doc.to_bytes();
        (void)emitted;
    } catch (mppwrite::Error const& error) {
        reemit_refused =
            std::string(error.what()).find("unreferenced sectors") !=
            std::string::npos;
    } catch (std::exception const&) {
        reemit_refused = false;
    }
    t.check(reemit_refused,
            "slack: to_bytes() refuses rather than emit a different file");

    /* The fatal half: point the stream's start at sector 130, which exists
     * physically (the file has 143 sectors) but has no FAT entry. */
    std::vector<std::uint8_t> broken = slack;
    put_u32(broken, 512 + 128 + 116, 130);
    bool threw = false;
    std::string message;
    try {
        mppwrite::CfbDocument const bad =
            mppwrite::CfbDocument::from_bytes(broken);
        (void)bad;
    } catch (mppwrite::Error const& error) {
        threw = true;
        message = error.what();
    } catch (std::exception const& error) {
        threw = true;
        message = std::string("non-mppwrite exception: ") + error.what();
    }
    t.check(threw, "slack: a chain that walks past the FAT is still fatal");
    t.check(message.find("130") != std::string::npos &&
                message.find("allocation table") != std::string::npos,
            "slack: the error names sector 130 and the allocation table: " +
                message);
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
        /* Corpus-free: the FAT-slack rule and the fatal chain are synthetic. */
        test_fat_slack(t);

        std::string missing;
        if (!corpus_available(missing)) {
            fixture::corpus_absent(missing);
            return t.report();
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
