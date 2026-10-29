/*
 * inplace_patch.cpp -- stage 2: change one known field in place.
 *
 * CLAIM UNDER TEST.  A single field of a task row can be changed by rewriting
 * only the bytes that carry it.  Nothing else moves: the file length is
 * unchanged, every other stream's payload is unchanged, and the container still
 * round-trips byte for byte.
 *
 * The test does not take the mapping on trust.  For each patch it
 *   1. computes the file offset the container says holds the field,
 *   2. asserts the changed byte ranges are exactly those bytes,
 *   3. re-parses the patched file and reads the value back through the record
 *      layer (TaskTable), and
 *   4. re-emits the patched file and asserts byte identity, so the patch did not
 *      leave the container inconsistent.
 *
 * It exercises both allocation paths on purpose: TBkndTask/FixedData is 856
 * bytes and therefore lives in the MINI stream (sectors of 64 bytes inside the
 * root entry's chain), while 214/CV_iew/Var2Data is ~100 KB and lives in
 * regular FAT sectors.  A mapping bug in either path shows up here.
 *
 * Fixtures are read from corpus/probe (read-only); nothing is written outside
 * the build tree.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "fixture_guard.h"
#include "inplace.h"
#include "row_writer.h"
#include "table.h"

#ifndef MPPWRITE_PROBE_DIR
#define MPPWRITE_PROBE_DIR "corpus/probe"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

char const* kFixed = "114/TBkndTask/FixedData";
/* The table's storage prefix, kept for tests that address streams relative to
 * it; this test only uses the absolute path, so clang and GCC flag it unused. */
[[maybe_unused]] char const* kTask = "114/TBkndTask";

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

  private:
    int checks_ = 0;
    int failures_ = 0;
};

std::string join(std::string const& a, std::string const& b) {
    return a + "/" + b;
}

/* File offset of byte `offset` inside `stream`, according to the container. */
std::uint64_t file_offset_of(mppwrite::CfbDocument const& doc,
                             std::string_view stream, std::uint64_t offset) {
    std::uint64_t position = 0;
    for (auto const& range : doc.stream_file_ranges(stream)) {
        if (offset < position + range.second) {
            return range.first + (offset - position);
        }
        position += range.second;
    }
    throw std::runtime_error("offset " + std::to_string(offset) +
                             " is outside stream " + std::string(stream));
}

std::string describe(std::vector<mppwrite::ByteRange> const& ranges) {
    std::string out;
    for (auto const& r : ranges) {
        if (!out.empty()) {
            out += ", ";
        }
        out += "[" + std::to_string(r.offset) + "+" + std::to_string(r.length) +
               "]";
    }
    return out;
}

std::vector<std::uint8_t> read_file(std::string const& path) {
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

/* The first stream at or above the mini cutoff, so the patch goes through the
 * FAT chain instead of the mini chain.  Prefers 114/, so that no UI stream is
 * touched even in a test: base.mpp's only large 114/ stream is 114/Props
 * (88,690 bytes). */
std::string pick_regular_stream(mppwrite::CfbDocument const& doc) {
    std::string fallback;
    for (std::string const& path : doc.stream_paths()) {
        if (doc.stream_size(path) < doc.mini_cutoff()) {
            continue;
        }
        if (path.rfind("114/", 0) == 0) {
            return path;
        }
        if (fallback.empty()) {
            fallback = path;
        }
    }
    if (fallback.empty()) {
        throw std::runtime_error("no regular stream in base.mpp");
    }
    return fallback;
}

} /* namespace */

int main() {
    TestContext t;
    try {
        std::string const source = join(MPPWRITE_PROBE_DIR, "base.mpp");
        if (!fixture::corpus_present(source)) {
            return 0;
        }
        mppwrite::InPlacePatcher patcher = mppwrite::InPlacePatcher::from_file(source);
        mppwrite::CfbDocument const& doc = patcher.container();

        t.check(doc.check().empty(), "base.mpp: container self-check: " + doc.check());
        std::uint64_t const original_size = patcher.file_size();
        std::vector<std::uint8_t> const original_bytes = patcher.bytes();
        std::vector<std::vector<std::uint8_t>> const original_streams = [&] {
            std::vector<std::vector<std::uint8_t>> out;
            for (std::string const& p : doc.stream_paths()) {
                out.push_back(doc.read_stream(p));
            }
            return out;
        }();

        /* --- anchor the row layout through the patcher itself ------------- */
        std::vector<std::uint8_t> const fixed =
            patcher.read(kFixed, 0, patcher.stream_size(kFixed));
        t.check(fixed.size() == 856, "base.mpp: FixedData is not 856 bytes");
        t.check(mppwrite::task_row_count(fixed.size()) == 4,
                "base.mpp: FixedData does not hold 4 rows");
        mppwrite::TaskTable table = mppwrite::TaskTable::from_document(doc);
        t.check(table.uid(1) == 1 && table.uid(2) == 2 && table.uid(3) == 3,
                "base.mpp: task uids are not 1,2,3");
        std::size_t const row = table.row_for_uid(1);
        t.check(row == 1, "base.mpp: uid 1 is not at row 1");

        /* --- 1. Priority (u16 at +78) ------------------------------------ */
        std::size_t const priority_offset =
            mppwrite::task_row_offset(row) + mppwrite::task_row::kPriority;
        t.check(patcher.read_u16(kFixed, priority_offset) == 500,
                "base.mpp: row 1 priority is not 500");
        std::uint64_t const priority_file_offset =
            file_offset_of(doc, kFixed, priority_offset);
        patcher.write_u16(kFixed, priority_offset, 900);
        std::vector<mppwrite::ByteRange> const priority_changes =
            patcher.changed_ranges();
        t.check(priority_changes.size() == 1 &&
                    priority_changes[0].offset == priority_file_offset &&
                    priority_changes[0].length == 2,
                "priority patch changed " + describe(priority_changes) +
                    ", expected 2 bytes at " +
                    std::to_string(priority_file_offset));
        t.check(patcher.changed_byte_count() == 2,
                "priority patch changed " +
                    std::to_string(patcher.changed_byte_count()) + " bytes");
        t.check(patcher.file_size() == original_size,
                "priority patch changed the file size");
        t.check(patcher.read_u16(kFixed, priority_offset) == 900,
                "priority does not read back as 900");

        std::size_t const after_priority = patcher.changed_byte_count();

        /* --- 2. PercentComplete (u16 at +92) ----------------------------- */
        std::size_t const percent_offset =
            mppwrite::task_row_offset(row) + mppwrite::task_row::kPercentComplete;
        patcher.write_u16(kFixed, percent_offset, 50);
        t.check(patcher.read_u16(kFixed, percent_offset) == 50,
                "percent complete does not read back as 50");
        t.check(patcher.read_u32(kFixed,
                                 mppwrite::task_row_offset(row) +
                                     mppwrite::task_row::kActualDuration) == 0,
                "percent complete patch touched ActualDuration");
        t.check(patcher.changed_byte_count() - after_priority == 1,
                "percent complete 0 -> 50 should change exactly one byte, not " +
                    std::to_string(patcher.changed_byte_count() - after_priority));

        /* --- 3. Duration (u32 at +84) ------------------------------------ */
        std::size_t const duration_offset =
            mppwrite::task_row_offset(row) + mppwrite::task_row::kDuration;
        patcher.write_u32(kFixed, duration_offset, 33600); /* 7 working days */
        t.check(patcher.read_u32(kFixed, duration_offset) == 33600,
                "duration does not read back as 33600");
        t.check(patcher.read_u32(kFixed,
                                 mppwrite::task_row_offset(row) +
                                     mppwrite::task_row::kRemainingDuration) ==
                    24000,
                "duration patch touched RemainingDuration");

        /* --- 4. Packed date (u32 at +96) --------------------------------- */
        std::size_t const start_offset =
            mppwrite::task_row_offset(row) + mppwrite::task_row::kStart;
        std::uint32_t const new_start =
            mppwrite::pack_date_ymd(2026, 3, 2, 8 * 3600);
        patcher.write_u32(kFixed, start_offset, new_start);
        std::uint32_t const read_back = patcher.read_u32(kFixed, start_offset);
        t.check(read_back == new_start, "start date does not read back");
        t.check(mppwrite::format_date(read_back) == "2026-03-02 08:00",
                "start date formats as " + mppwrite::format_date(read_back));

        /* --- 5. a regular (FAT-chained) stream, across a sector boundary - */
        std::string const regular = pick_regular_stream(doc);
        std::size_t const regular_size = patcher.stream_size(regular);
        t.check(regular_size > 512,
                "regular stream '" + regular + "' is too small");
        std::uint8_t const before = patcher.read(regular, 0, 1)[0];
        std::size_t const before_regular = patcher.changed_byte_count();
        patcher.write_u32(regular, 510, 0xA1B2C3D4u);
        t.check(patcher.read_u32(regular, 510) == 0xA1B2C3D4u,
                "regular stream u32 does not read back");
        std::size_t const regular_delta =
            patcher.changed_byte_count() - before_regular;
        t.check(regular_delta >= 1 && regular_delta <= 4,
                "regular stream patch changed " +
                    std::to_string(regular_delta) + " bytes, expected 1..4");
        t.check(patcher.read(regular, 0, 1)[0] == before,
                "regular stream byte 0 changed unexpectedly");

        /* --- 6. the patched file is still a valid, byte-identical container */
        mppwrite::CfbDocument const patched =
            mppwrite::CfbDocument::from_bytes(patcher.bytes());
        t.check(patched.check().empty(),
                "patched file fails the container self-check: " + patched.check());
        t.check(patched.to_bytes() == patcher.bytes(),
                "patched file does not re-emit byte-identically");
        t.check(patched.sector_count() == doc.sector_count(),
                "patched file changed sector count");
        t.check(patched.entry_count() == doc.entry_count(),
                "patched file changed directory entry count");

        /* --- 7. every other stream is untouched -------------------------- */
        std::size_t changed_streams = 0;
        {
            std::vector<std::string> const paths = doc.stream_paths();
            for (std::size_t i = 0; i < paths.size(); ++i) {
                std::vector<std::uint8_t> const now =
                    patched.read_stream(paths[i]);
                if (now != original_streams[i]) {
                    ++changed_streams;
                }
            }
        }
        t.check(changed_streams == 2,
                std::to_string(changed_streams) +
                    " streams changed, expected 2 (FixedData and " + regular +
                    ")");

        /* --- 8. the record layer agrees --------------------------------- */
        mppwrite::TaskTable const patched_table =
            mppwrite::TaskTable::from_document(patched);
        t.check(patched_table.uid(1) == 1, "patched: uid 1 no longer at row 1");
        t.check(patched_table.u16(1, mppwrite::task_row::kPriority) == 900,
                "patched: priority is not 900");
        t.check(patched_table.u16(1, mppwrite::task_row::kPercentComplete) == 50,
                "patched: percent complete is not 50");
        t.check(patched_table.u32(1, mppwrite::task_row::kDuration) == 33600,
                "patched: duration is not 33600");
        t.check(patched_table.u32(2, mppwrite::task_row::kPriority) == 500 &&
                    patched_table.u32(2, mppwrite::task_row::kDuration) == 14400,
                "patched: row 2 changed as well");
        t.check(mppwrite::check_table(patched).empty(),
                "patched: table consistency: " + mppwrite::check_table(patched));

        /* --- 9. save and reload from disk -------------------------------- */
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);
        std::string const out =
            join(MPPWRITE_TEST_OUT, "inplace-patched.mpp");
        patcher.save(out);
        mppwrite::InPlacePatcher const reloaded =
            mppwrite::InPlacePatcher::from_file(out);
        t.check(read_file(out) == patcher.bytes(),
                "saved file differs from the patched bytes");
        t.check(reloaded.read_u16(kFixed, priority_offset) == 900,
                "reloaded: priority is not 900");
        t.check(read_file(out).size() == original_bytes.size(),
                "saved file size changed");

        /* --- 10. the same single-copy edit through the consistent writer --
         * Stage 2's primitive is a byte writer and deliberately knows nothing
         * about the copies a date has; stage 4 (row_writer.h) is the layer that
         * knows, and it must refuse the +96-only edit performed above.  This is
         * the boundary between the two, asserted rather than described. */
        {
            mppwrite::CfbDocument fresh =
                mppwrite::CfbDocument::from_file(source);
            std::vector<std::uint8_t> const untouched = fresh.to_bytes();
            mppwrite::TaskValues values =
                mppwrite::read_task_values(fresh, 1);
            values.start = new_start; /* +96 only: +104/+112 stay behind */
            bool refused = false;
            std::string why;
            try {
                mppwrite::write_task_row(fresh, 1, values);
            } catch (mppwrite::Error const& e) {
                refused = true;
                why = e.what();
            }
            t.check(refused,
                    "write_task_row accepted the +96-only start patch that the "
                    "in-place patcher just made");
            t.check(why.find("single-copy") != std::string::npos,
                    "refusal message was: " + why);
            t.check(fresh.to_bytes() == untouched,
                    "the refused write changed the file anyway");
        }
    } catch (std::exception const& e) {
        std::cout << "fatal: " << e.what() << "\n";
        return 1;
    }
    return t.report();
}
