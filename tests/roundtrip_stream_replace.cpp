/*
 * roundtrip_stream_replace.cpp -- stage 3: replace a whole stream.
 *
 * THREE CASES, in increasing difficulty.
 *
 *   A. SAME SIZE.  Replace 114/TBkndTask/FixedData with a copy in which one
 *      task's priority changed.  The allocation does not have to move, so this
 *      path must produce byte-for-byte the same file as the in-place patcher
 *      (inplace.h).  Two independent implementations agreeing is the point: one
 *      maps a byte into a sector, the other rebuilds the whole container.
 *
 *   B. GROWTH INSIDE THE MINI STREAM.  Replace a task name.  Var2Data grows, so
 *      the block is appended, the VarMeta entry is repointed and the Var2Data
 *      size in the VarMeta header is updated.  Nothing may move: the old block
 *      stays as garbage, every other block keeps its offset, FixedData is
 *      untouched and 214/ is untouched.
 *
 *   C. GROWTH ACROSS THE MINI CUTOFF.  A long name pushes Var2Data past 4,096
 *      bytes, so the stream has to leave the mini stream and live in FAT
 *      sectors.  This exercises the allocator, the directory entry update and
 *      the mini FAT release.  (A 4,200-character task name is not something MS
 *      Project would write -- Project's limit is 255 -- but the container-level
 *      claim is what is under test, and it is the only way to reach the code
 *      path without a multi-megabyte fixture.)
 *
 * EVERY CASE ends by re-parsing the emitted file and checking:
 *   - the container self-check passes and re-emission is idempotent;
 *   - check_table() is clean (FixedMeta size word, VarMeta Var2Data size word,
 *     every VarMeta block inside Var2Data);
 *   - every 214/ stream is byte-identical to the source;
 *   - exactly the expected streams changed.
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
#include "table.h"

#ifndef MPPWRITE_PROBE_DIR
#define MPPWRITE_PROBE_DIR "corpus/probe"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

char const* kFixed = "114/TBkndTask/FixedData";
char const* kVar2 = "114/TBkndTask/Var2Data";
char const* kVarMeta = "114/TBkndTask/VarMeta";

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

std::size_t differing_bytes(std::vector<std::uint8_t> const& a,
                            std::vector<std::uint8_t> const& b) {
    if (a.size() != b.size()) {
        return std::max(a.size(), b.size());
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            ++n;
        }
    }
    return n;
}

bool starts_with(std::string const& text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
}

/* Which streams differ between two documents, by normalised path. */
std::vector<std::string> changed_streams(mppwrite::CfbDocument const& a,
                                         mppwrite::CfbDocument const& b) {
    std::vector<std::string> out;
    for (std::string const& path : a.stream_paths()) {
        if (!b.has_stream(path)) {
            out.push_back(path + " (missing)");
        } else if (a.read_stream(path) != b.read_stream(path)) {
            out.push_back(path);
        }
    }
    return out;
}

std::string join_list(std::vector<std::string> const& items) {
    std::string out;
    for (std::string const& item : items) {
        if (!out.empty()) {
            out += ", ";
        }
        out += item;
    }
    return out;
}

/* 214/ is the UI document storage.  Nothing the writer does to 114/ may touch
 * it: MS Project rewrites view state on its own schedule, and carrying it
 * through opaquely is the whole point of writing at the container level. */
void check_214_untouched(TestContext& t, mppwrite::CfbDocument const& before,
                         mppwrite::CfbDocument const& after, std::string label) {
    std::size_t compared = 0;
    for (std::string const& path : before.stream_paths()) {
        if (!starts_with(path, "214/")) {
            continue;
        }
        ++compared;
        t.check(after.has_stream(path), label + ": 214/ stream vanished: " + path);
        if (after.has_stream(path)) {
            t.check(after.read_stream(path) == before.read_stream(path),
                    label + ": 214/ stream changed: " + path);
        }
    }
    t.check(compared > 0, label + ": no 214/ streams found");
}

void check_emitted(TestContext& t, mppwrite::CfbDocument const& doc,
                   std::string label) {
    t.check(doc.check().empty(), label + ": container check: " + doc.check());
    std::vector<std::uint8_t> const bytes = doc.to_bytes();
    mppwrite::CfbDocument const again = mppwrite::CfbDocument::from_bytes(bytes);
    t.check(again.to_bytes() == bytes, label + ": re-emission is not idempotent");
    t.check(mppwrite::check_table(doc).empty(),
            label + ": table check: " + mppwrite::check_table(doc));
}

} /* namespace */

int main() {
    TestContext t;
    try {
        std::string const source = join(MPPWRITE_PROBE_DIR, "base.mpp");
        std::string const project_a_path = MPPWRITE_PROJECT_A_PATH;
        /* corpus/ is git-ignored (it holds a client's real programme), so a
         * build machine may not have the fixtures.  That is an environment
         * difference, not a failure. */
        if (!fixture::corpus_present(source) ||
            !fixture::corpus_present(project_a_path)) {
            return 0;
        }
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);

        mppwrite::CfbDocument const original =
            mppwrite::CfbDocument::from_file(source);
        t.check(original.check().empty(),
                "base.mpp: container check: " + original.check());
        std::vector<std::uint8_t> const original_bytes = read_file(source);

        /* ================= A. same-size FixedData replacement ============= */
        {
            mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
            mppwrite::TaskTable table = mppwrite::TaskTable::from_document(doc);
            std::size_t const row = table.row_for_uid(3);
            t.check(table.u16(row, mppwrite::task_row::kPriority) == 500,
                    "A: uid 3 priority is not 500");
            table.set_priority(row, 900);
            table.store_into(doc);

            std::vector<std::uint8_t> const bytes = doc.to_bytes();
            t.check(bytes.size() == original_bytes.size(),
                    "A: file size changed");
            std::size_t const diff = differing_bytes(bytes, original_bytes);
            t.check(diff == 2,
                    "A: " + std::to_string(diff) +
                        " bytes differ, expected 2 (500 -> 900 changes both "
                        "bytes of the u16)");

            mppwrite::CfbDocument const reloaded =
                mppwrite::CfbDocument::from_bytes(bytes);
            check_emitted(t, reloaded, "A");
            mppwrite::TaskTable const after =
                mppwrite::TaskTable::from_document(reloaded);
            t.check(after.u16(row, mppwrite::task_row::kPriority) == 900,
                    "A: priority does not read back as 900");
            t.check(after.u32(1, mppwrite::task_row::kDuration) == 24000 &&
                        after.u32(2, mppwrite::task_row::kDuration) == 14400,
                    "A: other task rows changed");
            t.check(reloaded.read_stream(kFixed) != original.read_stream(kFixed),
                    "A: FixedData did not change");
            t.check(original.read_stream(kVar2) == reloaded.read_stream(kVar2) &&
                        original.read_stream(kVarMeta) ==
                            reloaded.read_stream(kVarMeta),
                    "A: a variable-data stream changed");
            check_214_untouched(t, original, reloaded, "A");

            /* the independent in-place path must agree byte for byte */
            mppwrite::InPlacePatcher patcher =
                mppwrite::InPlacePatcher::from_file(source);
            patcher.write_u16(kFixed,
                              mppwrite::task_row_offset(row) +
                                  mppwrite::task_row::kPriority,
                              900);
            t.check(patcher.bytes() == bytes,
                    "A: the container rebuild and the in-place patch disagree");

            std::string const out = join(MPPWRITE_TEST_OUT, "replace-fixeddata.mpp");
            doc.save(out);
            t.check(read_file(out) == bytes, "A: saved file differs");
        }

        /* ================= B. grow inside the mini stream ================= */
        {
            mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
            std::string const old_name = mppwrite::read_task_name(doc, 2);
            t.check(old_name == "Task B",
                    "B: uid 2 name is '" + old_name + "', expected 'Task B'");
            // Wave 25 (W25-3): a SYNTHETIC 30-character name, the same length
            // as the scrub label this test used to carry.  The length keeps
            // the Var2Data growth and the container's +512-byte sector in the
            // measured case B; the name is not client text.
            std::string const new_name = "Task B renamed (synthetic rev)";
            std::uint64_t const var2_before = doc.stream_size(kVar2);
            std::uint64_t const var_meta_before = doc.stream_size(kVarMeta);

            mppwrite::replace_task_name(doc, 2, new_name);

            std::vector<std::uint8_t> const bytes = doc.to_bytes();
            t.check(bytes.size() >= original_bytes.size(),
                    "B: file shrank");
            mppwrite::CfbDocument const reloaded =
                mppwrite::CfbDocument::from_bytes(bytes);
            check_emitted(t, reloaded, "B");
            t.check(mppwrite::read_task_name(reloaded, 2) == new_name,
                    "B: the new name does not read back");
            t.check(mppwrite::read_task_name(reloaded, 1) == "Task A" &&
                        mppwrite::read_task_name(reloaded, 3) == "Task C",
                    "B: another task's name changed");
            t.check(reloaded.read_stream(kFixed) == original.read_stream(kFixed),
                    "B: FixedData changed (a name lives in Var2Data)");
            t.check(reloaded.stream_size(kVarMeta) == var_meta_before,
                    "B: VarMeta changed size; no entry was added");

            /* append-only: the old bytes are still there, in order */
            std::vector<std::uint8_t> const var2 = reloaded.read_stream(kVar2);
            std::vector<std::uint8_t> const var2_old = original.read_stream(kVar2);
            t.check(var2.size() > var2_before, "B: Var2Data did not grow");
            t.check(std::equal(var2_old.begin(), var2_old.end(), var2.begin()),
                    "B: Var2Data is not append-only");
            std::size_t const payload = 2 * new_name.size() + 2; /* UTF-16 + NUL */
            t.check(var2.size() == var2_before + 4 + payload,
                    "B: Var2Data grew by " +
                        std::to_string(var2.size() - var2_before) +
                        " bytes, expected " + std::to_string(4 + payload));

            std::vector<std::string> const changed =
                changed_streams(original, reloaded);
            t.check(changed.size() == 2,
                    "B: changed streams: " + join_list(changed));
            check_214_untouched(t, original, reloaded, "B");

            std::string const out = join(MPPWRITE_TEST_OUT, "replace-name.mpp");
            doc.save(out);
            t.check(read_file(out) == bytes, "B: saved file differs");
        }

        /* ====== B2. the LAST row, and the project row (VarMeta layout) =====
         * The writer used to parse VarMeta as a 32-byte header of
         * (key, uid, offset) entries with a trailer.  That shift happened to
         * address the right block for uid 1 and uid 2, whose name entries are
         * preceded by a row's last key, but there is no such predecessor for the
         * last row, so uid 3 could not be renamed at all.  With the measured
         * layout -- 24-byte header, (uid, offset, key) entries -- every row is
         * addressable.  This block renames the last row and the project row and
         * checks the VarMeta entry that carries it, parsed here independently of
         * table.cpp. */
        {
            mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
            t.check(mppwrite::read_task_name(doc, 3) == "Task C",
                    "B2: uid 3 name is '" + mppwrite::read_task_name(doc, 3) +
                        "', expected 'Task C'");
            t.check(mppwrite::read_task_name(doc, 0) == "base",
                    "B2: the project row's name block is '" +
                        mppwrite::read_task_name(doc, 0) +
                        "', expected the title 'base' (key 0x0b40000e)");

            std::uint64_t const var2_before = doc.stream_size(kVar2);
            std::string const new_name = "Task C renamed at the last row";
            mppwrite::replace_task_name(doc, 3, new_name);

            /* Independent VarMeta parse: 24-byte header, then 12-byte entries
             * (uid, Var2Data offset, key). */
            std::vector<std::uint8_t> const var_meta = doc.read_stream(kVarMeta);
            t.check((var_meta.size() - 24) % 12 == 0,
                    "B2: VarMeta is " + std::to_string(var_meta.size()) +
                        " bytes, not 24 + 12*n");
            bool found = false;
            std::size_t entries = (var_meta.size() - 24) / 12;
            for (std::size_t i = 0; i < entries; ++i) {
                std::size_t const p = 24 + 12 * i;
                auto const u32 = [&var_meta](std::size_t o) {
                    return static_cast<std::uint32_t>(var_meta[o]) |
                           (static_cast<std::uint32_t>(var_meta[o + 1]) << 8) |
                           (static_cast<std::uint32_t>(var_meta[o + 2]) << 16) |
                           (static_cast<std::uint32_t>(var_meta[o + 3]) << 24);
                };
                if (u32(p) == 3 && u32(p + 8) == 0x0b40000eu) {
                    found = true;
                    t.check(u32(p + 4) == var2_before,
                            "B2: the uid 3 name entry points at " +
                                std::to_string(u32(p + 4)) +
                                ", expected the appended block at " +
                                std::to_string(var2_before));
                }
            }
            t.check(found, "B2: no VarMeta entry for (uid 3, key 0x0b40000e)");
            t.check(doc.stream_size(kVarMeta) == 528,
                    "B2: VarMeta changed size; no entry was added");

            std::vector<std::uint8_t> const bytes = doc.to_bytes();
            mppwrite::CfbDocument const reloaded =
                mppwrite::CfbDocument::from_bytes(bytes);
            check_emitted(t, reloaded, "B2");
            t.check(mppwrite::read_task_name(reloaded, 3) == new_name,
                    "B2: the last row's new name does not read back");
            t.check(mppwrite::read_task_name(reloaded, 1) == "Task A" &&
                        mppwrite::read_task_name(reloaded, 2) == "Task B",
                    "B2: another task's name changed");
            t.check(reloaded.read_stream(kFixed) == original.read_stream(kFixed),
                    "B2: FixedData changed (a name lives in Var2Data)");
            std::vector<std::string> const changed =
                changed_streams(original, reloaded);
            t.check(changed.size() == 2,
                    "B2: changed streams: " + join_list(changed));
            check_214_untouched(t, original, reloaded, "B2");
            std::string const out =
                join(MPPWRITE_TEST_OUT, "replace-name-last-row.mpp");
            doc.save(out);
            t.check(read_file(out) == bytes, "B2: saved file differs");
        }

        /* ============ C. growth across the mini cutoff (4,096 B) ========== */
        {
            mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
            std::string const long_name(4200, 'x');
            mppwrite::replace_task_name(doc, 1, long_name);

            std::vector<std::uint8_t> const bytes = doc.to_bytes();
            mppwrite::CfbDocument const reloaded =
                mppwrite::CfbDocument::from_bytes(bytes);
            check_emitted(t, reloaded, "C");
            t.check(reloaded.stream_size(kVar2) > reloaded.mini_cutoff(),
                    "C: Var2Data is still below the mini cutoff");
            t.check(mppwrite::read_task_name(reloaded, 1) == long_name,
                    "C: the long name does not read back");
            t.check(mppwrite::read_task_name(reloaded, 2) ==
                        mppwrite::read_task_name(original, 2),
                    "C: another task's name changed");
            t.check(reloaded.read_stream(kFixed) == original.read_stream(kFixed),
                    "C: FixedData changed");

            /* it must now be addressed through FAT sectors, not mini sectors */
            std::uint64_t const var2_offset = 0;
            auto const ranges = reloaded.stream_file_ranges(kVar2);
            t.check(!ranges.empty() && ranges.front().second == 512,
                    "C: Var2Data is not addressed in whole 512-byte sectors");
            (void)var2_offset;

            std::vector<std::string> const changed =
                changed_streams(original, reloaded);
            t.check(changed.size() == 2,
                    "C: changed streams: " + join_list(changed));
            check_214_untouched(t, original, reloaded, "C");
            t.check(reloaded.sector_count() > original.sector_count(),
                    "C: the file did not grow (the old stream should be "
                    "released and the new one appended)");

            std::string const out = join(MPPWRITE_TEST_OUT, "replace-grow.mpp");
            doc.save(out);
            t.check(read_file(out) == bytes, "C: saved file differs");
        }

        /* ====== D. growth that needs a new FAT sector (PROJECT_A, 3,061 sectors) ==
         * 24 FAT sectors cover 24 * 128 = 3,072 sectors, so a stream that grows
         * by more than 11 sectors forces the writer to append a 25th FAT sector
         * and update the header.  That path is otherwise untested. */
        {
            mppwrite::CfbDocument const project_a =
                mppwrite::CfbDocument::from_file(MPPWRITE_PROJECT_A_PATH);
            std::string regular;
            for (std::string const& path : project_a.stream_paths()) {
                if (path.rfind("114/", 0) == 0 &&
                    project_a.stream_size(path) >= project_a.mini_cutoff()) {
                    regular = path;
                    break;
                }
            }
            t.check(!regular.empty(), "D: PROJECT_A has no large 114/ stream");
            if (!regular.empty()) {
                mppwrite::CfbDocument doc = project_a;
                std::vector<std::uint8_t> grown = doc.read_stream(regular);
                std::size_t const old_size = grown.size();
                grown.resize(old_size + 12000, 0);
                std::size_t const fat_before = doc.fat().size();
                doc.replace_stream(regular, grown);

                std::vector<std::uint8_t> const bytes = doc.to_bytes();
                mppwrite::CfbDocument const reloaded =
                    mppwrite::CfbDocument::from_bytes(bytes);
                t.check(reloaded.check().empty(),
                        "D: container check: " + reloaded.check());
                t.check(reloaded.read_stream(regular).size() == old_size + 12000,
                        "D: grown stream does not read back at its new size");
                t.check(reloaded.fat().size() > fat_before,
                        "D: the FAT did not grow (" +
                            std::to_string(fat_before) + " -> " +
                            std::to_string(reloaded.fat().size()) + " entries)");
                t.check(reloaded.sector_count() > project_a.sector_count() + 11,
                        "D: the file did not grow by enough sectors");
                std::vector<std::string> const changed =
                    changed_streams(project_a, reloaded);
                t.check(changed.size() == 1 &&
                            changed[0] == regular,
                        "D: changed streams: " + join_list(changed));
                check_214_untouched(t, project_a, reloaded, "D");
                doc.save(join(MPPWRITE_TEST_OUT, "replace-fatgrow.mpp"));
            }
        }

        /* ====== E. shrink a regular stream below the cutoff ==============
         * 174 FAT sectors are released, so the emitted file contains free
         * sectors inside it (legal, and the re-read has to tolerate them) and
         * the stream moves into the mini stream. */
        {
            mppwrite::CfbDocument const project_a =
                mppwrite::CfbDocument::from_file(MPPWRITE_PROJECT_A_PATH);
            std::string regular;
            for (std::string const& path : project_a.stream_paths()) {
                if (path.rfind("114/", 0) == 0 &&
                    project_a.stream_size(path) >= project_a.mini_cutoff()) {
                    regular = path;
                    break;
                }
            }
            t.check(!regular.empty(), "E: PROJECT_A has no large 114/ stream");
            if (!regular.empty()) {
                mppwrite::CfbDocument doc = project_a;
                std::vector<std::uint8_t> small = doc.read_stream(regular);
                small.resize(100);
                doc.replace_stream(regular, small);

                std::vector<std::uint8_t> const bytes = doc.to_bytes();
                mppwrite::CfbDocument const reloaded =
                    mppwrite::CfbDocument::from_bytes(bytes);
                t.check(reloaded.check().empty(),
                        "E: container check: " + reloaded.check());
                t.check(reloaded.read_stream(regular) == small,
                        "E: shrunk stream does not read back");
                t.check(reloaded.stream_size(regular) < reloaded.mini_cutoff(),
                        "E: the stream did not move below the cutoff");
                auto const ranges = reloaded.stream_file_ranges(regular);
                t.check(!ranges.empty() && ranges.front().second <= 64,
                        "E: the shrunk stream is not addressed in mini sectors");
                std::size_t free_inside = 0;
                std::vector<std::uint32_t> const& fat = reloaded.fat();
                for (std::size_t sid = 0;
                     sid < reloaded.sector_count() && sid < fat.size(); ++sid) {
                    if (fat[sid] == mppwrite::kFreeSector) {
                        ++free_inside;
                    }
                }
                t.check(free_inside >= 100,
                        "E: only " + std::to_string(free_inside) +
                            " free sectors inside the file after releasing a "
                            "large stream");
                t.check(reloaded.to_bytes() == bytes,
                        "E: a file with free sectors is not idempotent");
                check_214_untouched(t, project_a, reloaded, "E");
                doc.save(join(MPPWRITE_TEST_OUT, "replace-shrink.mpp"));
            }
        }

        /* the original file on disk is never touched */
        t.check(read_file(source) == original_bytes,
                "the source fixture was modified");
    } catch (std::exception const& e) {
        std::cout << "fatal: " << e.what() << "\n";
        return 1;
    }
    return t.report();
}
