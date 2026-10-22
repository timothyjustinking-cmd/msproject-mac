/*
 * roundtrip_save_flow.cpp -- the end-to-end save flow (W37).
 *
 * WHAT THIS PROVES.  `save_date_edits` and `save_plan` (core/mppwrite/save.h)
 * validate, write, and then re-open the file FROM DISK and re-read it.  This
 * test exercises that from the outside, so a save is only "passed" when the
 * bytes on disk carry the change:
 *
 *   (a) no corpus needed: builds from tests/data/synthetic-base.mpp (a
 *       committed, non-client fixture, byte-identical to corpus/probe/base.mpp).
 *       Pins uid 1 through save_date_edits to 2026-01-12 08:00, then writes a
 *       whole shifted plan through save_plan.
 *   (b) with corpus: loads corpus/probe/base.mpp, pins uid 1 through
 *       save_date_edits to 2026-10-21 08:00, and asserts the re-opened file
 *       holds ConstraintType 4 and the exact packed date at FixedData +64/+66,
 *       that every other task row is byte-identical to the source, that only
 *       FixedData changed, and that the CFB self-check and check_table pass.
 *   (c) refusals: unknown uid, uid 0 (project row), empty edit list, a
 *       non-existent output directory, an invalid packed date, a repeated uid.
 *       Each must throw a message naming the problem, must not create the
 *       output file, and must leave the document untouched.
 *
 * The test also emits the coordinator's oracle fixtures when run with a mode
 * switch:
 *   --write-fixture       corpus/probe/w37-snet-save.mpp, the NEGATIVE CONTROL:
 *                         base.mpp + uid 1 SNET 2026-10-21 08:00 with the row's
 *                         manual bit left SET, written by a direct
 *                         `pin_task_start`.  ORACLE RESULT 2026-10-22: Project
 *                         stored the constraint exactly (type 4 + date) and did
 *                         NOT move Start (2026-01-04 13:00).
 *   --write-fixture-auto  corpus/probe/w37-snet-auto.mpp, the save flow's own
 *                         fixture: the same date edit through `save_date_edits`,
 *                         which also clears Fixed2Meta's per-row flags bit 0x80.
 *                         ORACLE RESULT 2026-10-22: Project reported Manual=False,
 *                         Start 2026-10-21 08:00, Finish 2026-10-27 17:00 --
 *                         "pin + convert to auto-scheduled" is the save path.
 * Normal ctest runs never write into corpus/; they write only to the build tree.
 *
 * WHAT IS MEASURED vs CLAIMED.
 *   Measured: a SNET constraint is +64 (u16) / +66 (u32 packed date); SNET on an
 *   auto row is honoured (w29-snet.mpp); SNET alone on a manual row is stored
 *   and ignored (w37-snet-save.mpp); SNET + the manual bit cleared is honoured
 *   and the row becomes auto-scheduled (w37-snet-auto.mpp); the seven-word manual
 *   write set did not move Start (W29 manual/manual2).  Claimed here: only that
 *   the file on disk reads back as written.  UNMEASURED (inference): SNET is
 *   "no earlier than", so a linked successor may be placed at max(pred finish,
 *   pin) rather than at the pin -- no fixture tests that yet.
 *
 * OUTPUT (build tree, git-ignored): core/mppwrite/build/vs-w37/test-out/
 *   w37-synthetic-pinned.mpp, w37-synthetic-plan.mpp, w37-corpus-pinned.mpp
 * and, in the --write-fixture modes only, corpus/probe/w37-snet-save.mpp and
 * corpus/probe/w37-snet-auto.mpp.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "row_writer.h"
#include "save.h"
#include "table.h"

#ifndef MPPWRITE_PROBE_DIR
#define MPPWRITE_PROBE_DIR "corpus/probe"
#endif
#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

char const* const kFixed = "114/TBkndTask/FixedData";
char const* const kFixed2 = "114/TBkndTask/Fixed2Data";
char const* const kFixed2Meta = "114/TBkndTask/Fixed2Meta";

constexpr std::uint32_t kTaskUid = 1u;

/* Fixed2Meta geometry (core/mpp/reader.cpp): 16-byte header, 96-byte entries
 * paired with FixedMeta by entry index, flags at +8; bit 0x80 is the reader's
 * "manually scheduled" bit (base.mpp 0xff on its three manual rows, w3-o1
 * 0x7f on its auto rows). */
constexpr std::size_t kFixed2MetaHeader = 16;
constexpr std::size_t kFixed2MetaEntry = 96;
constexpr std::size_t kFixed2MetaFlags = 8;
constexpr std::uint8_t kManualBit = 0x80;
constexpr std::size_t kFixedMetaHeader = 16;
constexpr std::size_t kFixedMetaEntry = 47;

/* The two dates this test pins.  Both packed values are already measured
 * elsewhere: 0x3bf912c0 is W29's manual fixture start, 0x3d1312c0 is W29's
 * SNET fixture date.  Asserted against pack_date_ymd below so a broken
 * packing routine cannot make the test agree with itself. */
constexpr std::uint32_t kSyntheticPin = 0x3bf912c0u; /* 2026-01-12 08:00 */
constexpr std::uint32_t kCorpusPin = 0x3d1312c0u;    /* 2026-10-21 08:00 */

constexpr std::uint16_t kSnet = 4u; /* PjConstraint: Start No Earlier Than */

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

std::string out_path(std::string const& name) {
    return (std::filesystem::path(MPPWRITE_TEST_OUT) / name).string();
}

std::string join(std::string const& a, std::string const& b) {
    return (std::filesystem::path(a) / b).string();
}

bool file_exists(std::string const& path) {
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
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

std::uint16_t u16_at(std::vector<std::uint8_t> const& b, std::size_t o) {
    if (o + 2 > b.size()) {
        throw std::runtime_error("u16 read past the end of a stream");
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(b[o]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(b[o + 1]) << 8));
}

std::uint32_t u32_at(std::vector<std::uint8_t> const& b, std::size_t o) {
    if (o + 4 > b.size()) {
        throw std::runtime_error("u32 read past the end of a stream");
    }
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

std::uint64_t fnv1a64(std::vector<std::uint8_t> const& bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (std::uint8_t const byte : bytes) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool contains_text(std::vector<std::string> const& lines, std::string_view needle) {
    return std::any_of(lines.begin(), lines.end(),
                       [needle](std::string const& line) {
                           return line.find(needle) != std::string::npos;
                       });
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
    if (a.stream_paths().size() != b.stream_paths().size()) {
        out.push_back("<stream count " + std::to_string(a.stream_paths().size()) +
                      " -> " + std::to_string(b.stream_paths().size()) + ">");
    }
    return out;
}

/* Byte offsets of a stream that differ between two documents. */
std::vector<std::size_t> stream_diff_offsets(std::vector<std::uint8_t> const& before,
                                             std::vector<std::uint8_t> const& after) {
    std::vector<std::size_t> out;
    std::size_t const n = std::min(before.size(), after.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (before[i] != after[i]) {
            out.push_back(i);
        }
    }
    return out;
}

/* One FixedData row's bytes, so "every other row is identical" is a byte
 * assertion, not a field-by-field one. */
bool same_row_bytes(std::vector<std::uint8_t> const& before,
                    std::vector<std::uint8_t> const& after, std::size_t row) {
    std::size_t const begin = mppwrite::task_row_offset(row);
    std::size_t const end = begin + mppwrite::task_row::kStride;
    if (end > before.size() || end > after.size()) {
        return false;
    }
    return std::equal(before.begin() + static_cast<std::ptrdiff_t>(begin),
                      before.begin() + static_cast<std::ptrdiff_t>(end),
                      after.begin() + static_cast<std::ptrdiff_t>(begin));
}

/* The 6-byte constraint window of `row` in FixedData (u16 at +64, u32 at +66;
 * the 2 bytes between them are covered too, so the window is contiguous). */
bool in_constraint_window(std::size_t row, std::size_t stream_offset) {
    std::size_t const begin =
        mppwrite::task_row_offset(row) + mppwrite::task_row::kConstraintType;
    std::size_t const end = begin + 2u + 4u;
    return stream_offset >= begin && stream_offset < end;
}

/*
 * Stream offset of `row`'s Fixed2Meta flags byte, located through the FixedMeta
 * entry that names the row's FixedData offset (the two metadata streams are
 * paired by entry index, not by array position).
 */
std::size_t fixed2_meta_flags_offset(mppwrite::CfbDocument const& doc,
                                     std::size_t row) {
    std::vector<std::uint8_t> const meta = doc.read_stream(
        "114/TBkndTask/FixedMeta");
    std::uint32_t const wanted =
        static_cast<std::uint32_t>(mppwrite::task_row_offset(row));
    for (std::size_t i = 0;
         kFixedMetaHeader + (i + 1) * kFixedMetaEntry <= meta.size(); ++i) {
        std::size_t const base = kFixedMetaHeader + i * kFixedMetaEntry;
        if (u32_at(meta, base + 4) == wanted) {
            return kFixed2MetaHeader + i * kFixed2MetaEntry + kFixed2MetaFlags;
        }
    }
    throw std::runtime_error("no FixedMeta entry covers row " +
                             std::to_string(row));
}

/*
 * The shared body of (a) and (b): pin one uid through save_date_edits, then
 * prove the result from disk.  `source` is any fixture with >= 2 rows and uid 1
 * at row 1; `pin` is the packed date.  Returns nothing; checks accumulate.
 */
void check_pin_and_save(TestContext& t, std::string const& source,
                        std::string const& out, std::uint32_t pin,
                        std::string const& label) {
    if (!file_exists(source)) {
        std::cout << "SKIP: " << label << ": " << source << " is missing\n";
        return;
    }
    mppwrite::CfbDocument const original = mppwrite::CfbDocument::from_file(source);
    t.check(original.check().empty(),
            label + ": source CFB self-check: " + original.check());
    t.check(mppwrite::check_table(original).empty(),
            label + ": source table: " + mppwrite::check_table(original));

    mppwrite::TaskTable const source_table = mppwrite::TaskTable::from_document(original);
    t.check(source_table.rows() >= 2,
            label + ": source has " + std::to_string(source_table.rows()) + " rows");
    t.check(source_table.uid(1) == kTaskUid,
            label + ": row 1 is uid " + std::to_string(source_table.uid(1)) +
                ", expected " + std::to_string(kTaskUid));
    t.check(mppwrite::read_task_constraint(original, 1).type == 0,
            label + ": the source row already has a constraint");

    std::size_t const source_flags = fixed2_meta_flags_offset(original, 1);
    bool const source_manual =
        (original.read_stream(kFixed2Meta)[source_flags] & kManualBit) != 0;

    mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
    std::vector<mppwrite::DateEdit> edits;
    mppwrite::DateEdit edit;
    edit.uid = kTaskUid;
    edit.start = pin;
    edits.push_back(edit);
    mppwrite::SaveResult const result =
        mppwrite::save_date_edits(doc, edits, out);

    t.check(result.rows_written == 1,
            label + ": rows_written " + std::to_string(result.rows_written));
    t.check(contains_text(result.warnings, "re-read from disk"),
            label + ": no re-read receipt; warnings: " + join_list(result.warnings));
    /* For a manual row the edit is "pin + clear the mode bit" (measured
     * 2026-10-22 on w37-snet-auto.mpp), and the warning must say the row was
     * converted.  A row that was already auto must not get that warning. */
    t.check(contains_text(result.warnings, "converted to auto-scheduled") ==
                source_manual,
            label + ": conversion warning presence does not match the stored "
                    "manual bit (" +
                std::to_string(source_manual) + ")");
    t.check(doc.to_bytes() == read_file(out),
            label + ": the in-memory document differs from the file on disk");

    /* The pin is in the caller's document too. */
    mppwrite::TaskConstraint const in_memory =
        mppwrite::read_task_constraint(doc, 1);
    t.check(in_memory.type == kSnet && in_memory.date == pin,
            label + ": in-memory constraint is type " +
                std::to_string(in_memory.type) + " date " +
                mppwrite::format_date(in_memory.date) + ", expected SNET " +
                mppwrite::format_date(pin));

    /* Re-open the file from disk, independently of the save flow. */
    mppwrite::CfbDocument const reread = mppwrite::CfbDocument::from_file(out);
    t.check(reread.check().empty(),
            label + ": CFB self-check of the written file: " + reread.check());
    t.check(mppwrite::check_table(reread).empty(),
            label + ": check_table of the written file: " +
                mppwrite::check_table(reread));
    t.check(reread.to_bytes() == read_file(out),
            label + ": the written file does not re-emit byte-identically");

    mppwrite::TaskTable const written = mppwrite::TaskTable::from_document(reread);
    t.check(written.rows() == source_table.rows(),
            label + ": row count changed " +
                std::to_string(source_table.rows()) + " -> " +
                std::to_string(written.rows()));
    std::vector<std::uint8_t> const fixed_before = original.read_stream(kFixed);
    std::vector<std::uint8_t> const fixed_after = reread.read_stream(kFixed);
    std::vector<std::uint8_t> const fixed2_before = original.read_stream(kFixed2);
    std::vector<std::uint8_t> const fixed2_after = reread.read_stream(kFixed2);

    /* Exact packed date at the exact offsets, from the raw stream. */
    std::size_t const row_begin = mppwrite::task_row_offset(1);
    t.check(u16_at(fixed_after, row_begin + mppwrite::task_row::kConstraintType) ==
                kSnet,
            label + ": raw +64 ConstraintType is " +
                std::to_string(u16_at(fixed_after,
                                      row_begin +
                                          mppwrite::task_row::kConstraintType)) +
                ", expected 4");
    t.check(u32_at(fixed_after, row_begin + mppwrite::task_row::kConstraintDate) ==
                pin,
            label + ": raw +66 ConstraintDate is " +
                hex32(u32_at(fixed_after,
                             row_begin + mppwrite::task_row::kConstraintDate)) +
                ", expected " + hex32(pin));

    /* Every other task row is byte-identical; the pinned row differs only in
     * the constraint window; Fixed2Data is untouched. */
    bool other_rows_identical = true;
    for (std::size_t row = 0; row < source_table.rows(); ++row) {
        if (row == 1) {
            continue;
        }
        if (!same_row_bytes(fixed_before, fixed_after, row)) {
            other_rows_identical = false;
            t.check(false, label + ": row " + std::to_string(row) +
                               " changed although only uid 1 was pinned");
        }
    }
    t.check(other_rows_identical, label + ": a row other than uid 1 changed");

    std::vector<std::size_t> const fixed_diff =
        stream_diff_offsets(fixed_before, fixed_after);
    t.check(!fixed_diff.empty(), label + ": FixedData did not change at all");
    bool only_constraint_bytes = true;
    for (std::size_t const offset : fixed_diff) {
        if (!in_constraint_window(1, offset)) {
            only_constraint_bytes = false;
            t.check(false, label + ": FixedData byte " + std::to_string(offset) +
                               " changed outside row 1's constraint window");
        }
    }
    t.check(only_constraint_bytes,
            label + ": FixedData changed outside the constraint window");
    t.check(fixed2_before == fixed2_after,
            label + ": Fixed2Data changed although only a constraint was written");

    /* The manual-mode bit: cleared for the edited row when it was set, and
     * not touched anywhere else (a single byte changes). */
    std::vector<std::uint8_t> const f2_before = original.read_stream(kFixed2Meta);
    std::vector<std::uint8_t> const f2_after = reread.read_stream(kFixed2Meta);
    std::vector<std::size_t> const f2_diff =
        stream_diff_offsets(f2_before, f2_after);
    if (source_manual) {
        t.check(f2_diff.size() == 1 && f2_diff[0] == source_flags,
                label + ": Fixed2Meta changed " +
                    std::to_string(f2_diff.size()) +
                    " bytes, expected only the flags byte at " +
                    std::to_string(source_flags));
        t.check(f2_after[source_flags] ==
                    static_cast<std::uint8_t>(f2_before[source_flags] &
                                              ~kManualBit),
                label + ": the manual-mode bit did not clear (flags " +
                    hex32(f2_before[source_flags]) + " -> " +
                    hex32(f2_after[source_flags]) + ")");
    } else {
        t.check(f2_diff.empty(), label + ": Fixed2Meta changed on an auto row");
    }

    std::vector<std::string> const changed = changed_streams(original, reread);
    bool const fixed_changed =
        std::find(changed.begin(), changed.end(), kFixed) != changed.end();
    bool const fixed2meta_changed =
        std::find(changed.begin(), changed.end(), kFixed2Meta) != changed.end();
    t.check(changed.size() == (source_manual ? 2u : 1u) && fixed_changed &&
                fixed2meta_changed == source_manual,
            label + ": changed streams: " + join_list(changed));

    /* The public reader agrees with the raw bytes. */
    mppwrite::TaskConstraint const on_disk = mppwrite::read_task_constraint(reread, 1);
    t.check(on_disk.type == kSnet && on_disk.date == pin,
            label + ": read_task_constraint is type " + std::to_string(on_disk.type) +
                " date " + mppwrite::format_date(on_disk.date));
    mppwrite::TaskValues const on_disk_values = mppwrite::read_task_values(reread, 1);
    mppwrite::TaskValues expected_values = mppwrite::read_task_values(original, 1);
    expected_values.constraint_type = kSnet;
    expected_values.constraint_date = pin;
    t.check(on_disk_values == expected_values,
            label + ": the pin changed a task value other than the constraint");

    std::cout << label << ": " << out << " (" << read_file(out).size()
              << " bytes, FNV-1a " << hex64(fnv1a64(read_file(out))) << ", uid "
              << kTaskUid << " SNET " << mppwrite::format_date(pin) << ")\n";
}

/* (a) no corpus: tests/data/synthetic-base.mpp, then save_plan over it. */
void test_synthetic(TestContext& t) {
    std::string const source = join(MPPWRITE_TEST_DATA, "synthetic-base.mpp");
    check_pin_and_save(t, source, out_path("w37-synthetic-pinned.mpp"),
                       kSyntheticPin, "synthetic");

    if (!file_exists(source)) {
        return;
    }

    /* ---- save_plan: shift every date copy by 7 days and save ---- */
    std::string const pinned = out_path("w37-synthetic-pinned.mpp");
    if (!file_exists(pinned)) {
        t.check(false, "synthetic: the pinned file was not written");
        return;
    }
    mppwrite::CfbDocument plan_doc = mppwrite::CfbDocument::from_file(pinned);
    mppwrite::TaskTable const plan_table = mppwrite::TaskTable::from_document(plan_doc);
    std::vector<mppwrite::PlannedRow> rows;
    for (std::size_t row = 0; row < plan_table.rows(); ++row) {
        mppwrite::PlannedRow planned;
        planned.uid = plan_table.uid(row);
        planned.values = mppwrite::read_task_values(plan_doc, row);
        /* +7 days is +7 << 16 on a packed date; every copy shifts together, so
         * the duration/duration-family and slack values are unchanged. */
        planned.values.start += 7u << 16;
        planned.values.finish += 7u << 16;
        planned.values.project_start += 7u << 16;
        planned.values.project_finish += 7u << 16;
        planned.values.late_start += 7u << 16;
        planned.values.late_finish += 7u << 16;
        rows.push_back(planned);
    }
    std::string const out = out_path("w37-synthetic-plan.mpp");
    mppwrite::SaveResult const result = mppwrite::save_plan(plan_doc, rows, out);
    t.check(result.rows_written == rows.size(),
            "synthetic plan: rows_written " +
                std::to_string(result.rows_written) + ", expected " +
                std::to_string(rows.size()));
    t.check(contains_text(result.warnings, "re-read from disk"),
            "synthetic plan: no re-read receipt; warnings: " +
                join_list(result.warnings));
    t.check(plan_doc.to_bytes() == read_file(out),
            "synthetic plan: the in-memory document differs from the file");

    mppwrite::CfbDocument const reread = mppwrite::CfbDocument::from_file(out);
    t.check(reread.check().empty(),
            "synthetic plan: CFB self-check: " + reread.check());
    t.check(mppwrite::check_table(reread).empty(),
            "synthetic plan: check_table: " + mppwrite::check_table(reread));
    t.check(reread.to_bytes() == read_file(out),
            "synthetic plan: the written file does not re-emit identically");

    mppwrite::TaskTable const rt = mppwrite::TaskTable::from_document(reread);
    t.check(rt.rows() == plan_table.rows(), "synthetic plan: row count changed");
    bool every_row_shifted = true;
    for (std::size_t i = 0; i < rows.size() && i < rt.rows(); ++i) {
        mppwrite::TaskValues const now = mppwrite::read_task_values(reread, i);
        bool const shifted =
            now.start == rows[i].values.start &&
            now.finish == rows[i].values.finish &&
            now.project_start == rows[i].values.project_start &&
            now.project_finish == rows[i].values.project_finish &&
            now.late_start == rows[i].values.late_start &&
            now.late_finish == rows[i].values.late_finish;
        if (!shifted) {
            every_row_shifted = false;
            t.check(false, "synthetic plan: row " + std::to_string(i) +
                               " does not read back with every date shifted");
        }
        bool const preserved =
            now.duration == rows[i].values.duration &&
            now.remaining_duration == rows[i].values.remaining_duration &&
            now.total_slack == rows[i].values.total_slack &&
            now.start_slack == rows[i].values.start_slack;
        t.check(preserved, "synthetic plan: row " + std::to_string(i) +
                               " changed duration or slack");
    }
    t.check(every_row_shifted, "synthetic plan: a row did not shift by 7 days");
    /* The SNET written by save_date_edits survives the plan write. */
    mppwrite::TaskConstraint const constraint =
        mppwrite::read_task_constraint(reread, 1);
    t.check(constraint.type == kSnet && constraint.date == kSyntheticPin,
            "synthetic plan: the SNET pin did not survive; type " +
                std::to_string(constraint.type) + " date " +
                mppwrite::format_date(constraint.date));

    mppwrite::CfbDocument const input_doc =
        mppwrite::CfbDocument::from_file(pinned);
    std::vector<std::string> const changed = changed_streams(input_doc, reread);
    t.check(changed.size() == 2,
            "synthetic plan: changed streams: " + join_list(changed));
    t.check(std::find(changed.begin(), changed.end(), kFixed) != changed.end() &&
                std::find(changed.begin(), changed.end(), kFixed2) != changed.end(),
            "synthetic plan: FixedData/Fixed2Data are not both in: " +
                join_list(changed));

    /* ---- re-pin an ALREADY auto row: no conversion, no Fixed2Meta change ---- */
    {
        mppwrite::CfbDocument repin = mppwrite::CfbDocument::from_file(pinned);
        std::uint32_t const second_date = kSyntheticPin + (7u << 16);
        std::vector<mppwrite::DateEdit> second_edits;
        mppwrite::DateEdit second_edit;
        second_edit.uid = kTaskUid;
        second_edit.start = second_date;
        second_edits.push_back(second_edit);
        std::string const repin_out = out_path("w37-synthetic-repin.mpp");
        mppwrite::SaveResult const repin_result =
            mppwrite::save_date_edits(repin, second_edits, repin_out);
        t.check(repin_result.rows_written == 1, "re-pin: rows_written");
        t.check(!contains_text(repin_result.warnings,
                               "converted to auto-scheduled"),
                "re-pin: an already-auto row was reported as converted");
        mppwrite::CfbDocument const again =
            mppwrite::CfbDocument::from_file(repin_out);
        t.check(mppwrite::read_task_constraint(again, 1).date == second_date,
                "re-pin: the new date did not read back");
        t.check(again.read_stream(kFixed2Meta) ==
                    mppwrite::CfbDocument::from_file(pinned).read_stream(kFixed2Meta),
                "re-pin: Fixed2Meta changed on an already-auto row");
        std::vector<std::string> const repin_changed =
            changed_streams(mppwrite::CfbDocument::from_file(pinned), again);
        t.check(repin_changed.size() == 1 && repin_changed[0] == kFixed,
                "re-pin: changed streams: " + join_list(repin_changed));
    }
}

/* (b) with corpus: corpus/probe/base.mpp, uid 1 -> 2026-10-21 08:00. */
void test_corpus(TestContext& t) {
    std::string const source = join(MPPWRITE_PROBE_DIR, "base.mpp");
    check_pin_and_save(t, source, out_path("w37-corpus-pinned.mpp"), kCorpusPin,
                       "corpus");
}

/* (c) refusals: no message, no file, no document change. */
void expect_save_error(TestContext& t, std::string const& label,
                       std::string const& source,
                       std::vector<mppwrite::DateEdit> const& edits,
                       std::string const& path, std::string const& needle) {
    mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
    std::vector<std::uint8_t> const before = doc.to_bytes();
    bool threw = false;
    std::string message;
    try {
        (void)mppwrite::save_date_edits(doc, edits, path);
    } catch (mppwrite::Error const& e) {
        threw = true;
        message = e.what();
    } catch (std::exception const& e) {
        message = std::string("non-mppwrite exception: ") + e.what();
    }
    t.check(threw, label + ": the call did not throw");
    t.check(message.find(needle) != std::string::npos,
            label + ": message was: " + message);
    t.check(!file_exists(path), label + ": created " + path);
    t.check(doc.to_bytes() == before, label + ": the document changed");
}

void test_errors(TestContext& t) {
    std::string const source = join(MPPWRITE_TEST_DATA, "synthetic-base.mpp");
    if (!file_exists(source)) {
        std::cout << "SKIP: errors: " << source << " is missing\n";
        return;
    }
    mppwrite::DateEdit valid;
    valid.uid = kTaskUid;
    valid.start = kSyntheticPin;

    /* unknown uid */
    {
        mppwrite::DateEdit edit = valid;
        edit.uid = 424242u;
        expect_save_error(t, "unknown uid", source, {edit},
                          out_path("w37-err-unknown-uid.mpp"), "424242");
    }
    /* uid 0 = the project row */
    {
        mppwrite::DateEdit edit = valid;
        edit.uid = 0;
        expect_save_error(t, "uid 0", source, {edit},
                          out_path("w37-err-uid0.mpp"), "project row");
    }
    /* empty edit list */
    expect_save_error(t, "empty edits", source, {},
                      out_path("w37-err-empty.mpp"), "empty");
    /* non-existent output directory */
    {
        std::string const missing_dir =
            (std::filesystem::path(MPPWRITE_TEST_OUT) / "w37-no-such-directory")
                .string();
        t.check(!file_exists(missing_dir),
                "non-existent directory: " + missing_dir +
                    " already exists; the case cannot be exercised");
        expect_save_error(t, "missing output directory", source, {valid},
                          join(missing_dir, "out.mpp"), "does not exist");
    }
    /* invalid packed date */
    {
        mppwrite::DateEdit edit = valid;
        edit.start = 0xFFFFFFFFu;
        expect_save_error(t, "invalid date", source, {edit},
                          out_path("w37-err-date.mpp"), "not a valid packed date");
    }
    /* the same uid twice */
    {
        mppwrite::DateEdit second = valid;
        second.start += 7u << 16;
        expect_save_error(t, "duplicate uid", source, {valid, second},
                          out_path("w37-err-duplicate.mpp"), "twice");
    }
    /* an empty output path */
    expect_save_error(t, "empty output path", source, {valid}, "", "output path");
}

/*
 * --write-fixture: the NEGATIVE-CONTROL fixture, w37-snet-save.mpp.
 *
 * This is the experiment that showed SNET alone is not enough on a Manual=True
 * row: base.mpp + uid 1's SNET 2026-10-21 08:00, with the manual bit left SET.
 * Project reopened it with the constraint stored exactly and Start unchanged
 * (2026-01-04 13:00).  It must therefore be written by a DIRECT
 * `pin_task_start`, not by `save_date_edits` (which now also clears the manual
 * bit); keeping it regenerable keeps the evidence for the save.h verdict.
 */
int write_fixture() {
    std::string const source = join(MPPWRITE_PROBE_DIR, "base.mpp");
    if (!file_exists(source)) {
        std::cerr << "cannot write the fixture: " << source << " is missing\n";
        return 1;
    }
    mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
    mppwrite::TaskTable const table = mppwrite::TaskTable::from_document(doc);
    std::size_t const row = table.row_for_uid(kTaskUid);
    std::size_t const flags = fixed2_meta_flags_offset(doc, row);
    std::uint8_t const source_flags = doc.read_stream(kFixed2Meta)[flags];
    mppwrite::pin_task_start(doc, row, kCorpusPin);
    std::string const out = join(MPPWRITE_PROBE_DIR, "w37-snet-save.mpp");
    doc.save(out);

    /* Independent proof from disk, so the printed summary is not self-reported
     * by the code under test. */
    mppwrite::CfbDocument const reread = mppwrite::CfbDocument::from_file(out);
    mppwrite::TaskTable const reread_table = mppwrite::TaskTable::from_document(reread);
    std::size_t const reread_row = reread_table.row_for_uid(kTaskUid);
    mppwrite::TaskConstraint const constraint =
        mppwrite::read_task_constraint(reread, reread_row);
    std::vector<std::uint8_t> const fixed = reread.read_stream(kFixed);
    std::size_t const begin = mppwrite::task_row_offset(reread_row);
    std::uint8_t const reread_flags = reread.read_stream(kFixed2Meta)[flags];
    std::vector<std::uint8_t> const bytes = read_file(out);
    std::cout << "fixture (negative control: SNET only, manual bit kept): "
              << out << "\n"
              << "  source: " << source << "\n"
              << "  size: " << bytes.size() << " bytes\n"
              << "  FNV-1a 64: " << hex64(fnv1a64(bytes)) << "\n"
              << "  uid " << kTaskUid << " row " << reread_row
              << ": ConstraintType "
              << u16_at(fixed, begin + mppwrite::task_row::kConstraintType)
              << ", ConstraintDate " << hex32(u32_at(
                     fixed, begin + mppwrite::task_row::kConstraintDate))
              << " (" << mppwrite::format_date(constraint.date) << ")\n"
              << "  Fixed2Meta flags byte at " << flags << ": "
              << hex32(source_flags) << " -> " << hex32(reread_flags)
              << " (manual bit 0x80 left set)\n"
              << "  CFB self-check: "
              << (reread.check().empty() ? "clean" : reread.check()) << "\n"
              << "  check_table: "
              << (mppwrite::check_table(reread).empty()
                      ? "clean"
                      : mppwrite::check_table(reread))
              << "\n";
    if (!reread.check().empty() || !mppwrite::check_table(reread).empty() ||
        constraint.type != kSnet || constraint.date != kCorpusPin ||
        (reread_flags & kManualBit) == 0 || reread_flags != source_flags) {
        std::cerr << "fixture proof failed\n";
        return 1;
    }
    return 0;
}

/*
 * --write-fixture-auto: w37-snet-auto.mpp = the save flow's own oracle fixture.
 *
 * `save_date_edits` produces it directly: uid 1's SNET 2026-10-21 08:00 plus its
 * Fixed2Meta per-row flags byte with bit 0x80 cleared, nothing else.  MEASURED
 * 2026-10-22 (coordinator's read-only open): Project reported uid 1 Manual=False,
 * Start 2026-10-21 08:00, Finish 2026-10-27 17:00 -- the date is honoured.  So
 * the flow's default date edit really is "pin + convert to auto-scheduled", and
 * this fixture is the proof, not a candidate.
 */
int write_auto_fixture() {
    std::string const source = join(MPPWRITE_PROBE_DIR, "base.mpp");
    if (!file_exists(source)) {
        std::cerr << "cannot write the fixture: " << source << " is missing\n";
        return 1;
    }
    mppwrite::CfbDocument const source_doc =
        mppwrite::CfbDocument::from_file(source);
    mppwrite::TaskTable const source_table =
        mppwrite::TaskTable::from_document(source_doc);
    std::size_t const row = source_table.row_for_uid(kTaskUid);
    std::size_t const flags = fixed2_meta_flags_offset(source_doc, row);
    std::vector<std::uint8_t> const source_f2_meta =
        source_doc.read_stream(kFixed2Meta);
    std::uint8_t const old_flags = source_f2_meta[flags];
    std::uint8_t const expected_flags = static_cast<std::uint8_t>(
        old_flags & ~static_cast<std::uint8_t>(kManualBit));
    if ((old_flags & kManualBit) == 0) {
        std::cerr << "uid " << kTaskUid << " is already stored as auto "
                  << "(Fixed2Meta flags " << hex32(old_flags)
                  << "); the fixture cannot show a conversion\n";
        return 1;
    }

    mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
    std::vector<mppwrite::DateEdit> edits;
    mppwrite::DateEdit edit;
    edit.uid = kTaskUid;
    edit.start = kCorpusPin;
    edits.push_back(edit);
    std::string const out = join(MPPWRITE_PROBE_DIR, "w37-snet-auto.mpp");
    mppwrite::SaveResult const result = mppwrite::save_date_edits(doc, edits, out);

    /* ---- independent proof from disk ------------------------------------- */
    mppwrite::CfbDocument const written = mppwrite::CfbDocument::from_file(out);
    int checks = 0;
    int failures = 0;
    auto check = [&checks, &failures](bool condition, std::string const& what) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << what << "\n";
        }
    };
    check(written.check().empty(),
          "CFB self-check: " + written.check());
    check(mppwrite::check_table(written).empty(),
          "check_table: " + mppwrite::check_table(written));
    mppwrite::TaskTable const written_table =
        mppwrite::TaskTable::from_document(written);
    std::size_t const written_row = written_table.row_for_uid(kTaskUid);
    check(written_row == row, "uid row changed");
    check(result.rows_written == 1, "rows_written " +
                                        std::to_string(result.rows_written));
    check(contains_text(result.warnings, "converted to auto-scheduled"),
          "the conversion warning is missing from the save result");
    mppwrite::TaskConstraint const constraint =
        mppwrite::read_task_constraint(written, written_row);
    check(constraint.type == kSnet && constraint.date == kCorpusPin,
          "constraint did not read back as SNET " +
              mppwrite::format_date(kCorpusPin));
    std::vector<std::uint8_t> const fixed_before = source_doc.read_stream(kFixed);
    std::vector<std::uint8_t> const fixed_after = written.read_stream(kFixed);
    std::vector<std::size_t> const fixed_diff =
        stream_diff_offsets(fixed_before, fixed_after);
    check(fixed_diff.size() == 5, "FixedData changed " +
                                      std::to_string(fixed_diff.size()) +
                                      " bytes, expected 5");
    check(std::all_of(fixed_diff.begin(), fixed_diff.end(),
                      [written_row](std::size_t offset) {
                          return in_constraint_window(written_row, offset);
                      }),
          "FixedData changed outside the constraint window");
    std::vector<std::uint8_t> const f2_before = source_doc.read_stream(kFixed2Meta);
    std::vector<std::uint8_t> const f2_after = written.read_stream(kFixed2Meta);
    std::vector<std::size_t> const f2_diff = stream_diff_offsets(f2_before, f2_after);
    check(f2_diff.size() == 1, "Fixed2Meta changed " +
                                   std::to_string(f2_diff.size()) +
                                   " bytes, expected 1");
    if (f2_diff.size() == 1) {
        check(f2_diff[0] == flags, "Fixed2Meta changed the wrong byte");
        check(f2_before[flags] == old_flags && f2_after[flags] == expected_flags,
              "Fixed2Meta flags byte is " + hex32(f2_after[flags]) +
                  ", expected " + hex32(expected_flags));
    }
    std::vector<std::string> const changed = changed_streams(source_doc, written);
    check(changed.size() == 2 &&
              std::find(changed.begin(), changed.end(), kFixed) != changed.end() &&
              std::find(changed.begin(), changed.end(), kFixed2Meta) !=
                  changed.end(),
          "changed streams: " + join_list(changed));
    check(source_doc.read_stream(kFixed2) == written.read_stream(kFixed2),
          "Fixed2Data changed");

    std::vector<std::uint8_t> const bytes = read_file(out);
    std::cout << "fixture: " << out << "\n"
              << "  source: " << source << "\n"
              << "  size: " << bytes.size() << " bytes\n"
              << "  FNV-1a 64: " << hex64(fnv1a64(bytes)) << "\n"
              << "  uid " << kTaskUid << " row " << row << ": ConstraintType "
              << u16_at(fixed_after, mppwrite::task_row_offset(row) +
                                         mppwrite::task_row::kConstraintType)
              << ", ConstraintDate "
              << hex32(u32_at(fixed_after, mppwrite::task_row_offset(row) +
                                              mppwrite::task_row::kConstraintDate))
              << " (" << mppwrite::format_date(constraint.date) << ")\n"
              << "  changed: FixedData row " << row << " +64/+66 (5 bytes), "
              << "Fixed2Meta flags byte at stream offset " << flags << " "
              << hex32(old_flags) << " -> " << hex32(expected_flags)
              << " (bit 0x80 cleared)\n"
              << "  CFB self-check: "
              << (written.check().empty() ? "clean" : written.check()) << "\n"
              << "  check_table: "
              << (mppwrite::check_table(written).empty()
                      ? "clean"
                      : mppwrite::check_table(written))
              << "\n"
              << "  rows_written: " << result.rows_written << "\n";
    for (std::string const& warning : result.warnings) {
        std::cout << "  warning: " << warning << "\n";
    }
    std::cout << "  " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

} /* namespace */

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--write-fixture") {
            return write_fixture();
        }
        if (argc > 1 && std::string_view(argv[1]) == "--write-fixture-auto") {
            return write_auto_fixture();
        }
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);
        /* The packed-date constants must be what their comments say. */
        TestContext t;
        t.check(mppwrite::pack_date_ymd(2026, 1, 12, 8 * 3600) ==
                    kSyntheticPin,
                "pack_date_ymd(2026-01-12 08:00) is " +
                    hex32(mppwrite::pack_date_ymd(2026, 1, 12, 8 * 3600)) +
                    ", expected " + hex32(kSyntheticPin));
        t.check(mppwrite::pack_date_ymd(2026, 10, 21, 8 * 3600) ==
                    kCorpusPin,
                "pack_date_ymd(2026-10-21 08:00) is " +
                    hex32(mppwrite::pack_date_ymd(2026, 10, 21, 8 * 3600)) +
                    ", expected " + hex32(kCorpusPin));

        test_synthetic(t);
        test_corpus(t);
        test_errors(t);
        return t.report();
    } catch (std::exception const& e) {
        std::cerr << "FATAL: " << e.what() << "\n";
        return 1;
    }
}
