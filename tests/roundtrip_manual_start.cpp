/*
 * roundtrip_manual_start.cpp -- W29: write a manual task the way Project does,
 * and pin an auto task with a real SNET constraint.
 *
 * THE QUESTION.  W26/the coordinator measured that writing the six FixedData
 * date copies of a Manual=True task is internally consistent but ineffective:
 * Project recomputes the row on open.  Project's own save after setting the
 * task's Start (corpus/probe/manual-start-edited.mpp) writes a *different* set,
 * and reopening it reports the new dates:
 *
 *   FixedData  +104/+108  effective Start/Finish   (NOT +96/+100)
 *              +112/+116  LateStart/LateFinish     (== Start/Finish here: 0 slack)
 *              +174       start anchor             (the manual Start)
 *   Fixed2Data +50/+54    the start/finish copies  (NOT +46)
 *
 * leaving +96/+100 (calculated schedule), +84/+88 (duration family),
 * Fixed2Data +46 and +58 untouched.  This test builds exactly that write
 * through `mppwrite::write_manual_task_row` from base.mpp row 1 (uid 1) with the
 * intended schedule 2026-01-12 08:00 -> 2026-01-16 17:00 (still 24,000 ticks),
 * emits test-out/w29-manual.mpp, and proves from the emitted bytes that exactly
 * those seven words changed -- nothing else in the row, nothing else in the two
 * streams, no other stream (214/ included) -- and that +96/+100/+84/+88 and
 * Fixed2Data +46/+58 did not move.
 *
 * The second fixture tests the working save strategy, oracle-verified on
 * 2026-10-22: `w29-snet.mpp` is corpus/probe/w3-o1.mpp (auto task uid 1, 1 day,
 * Mon 2026-10-19 08:00 -> 17:00) with a real SNET constraint written through
 * `mppwrite::pin_task_start`: ConstraintType = 4 at row +64 (u16) and
 * ConstraintDate = 2026-10-21 08:00 at row +66 (u32), every date word left as
 * stored.  Project reopened it with uid 1 at 2026-10-21 08:00 -> 17:00, Early
 * the same, Late 10-26, slack 1440, and recalculated the project start to
 * 2026-10-20 (the earliest remaining task).  w3-o1 already stores SNET rows
 * (uid 2/3/4 hold type 4 + a date at +64/+66), so the encoding is copied from
 * Project's own file, and the byte manifest requires exactly the two constraint
 * words to change.
 *
 * ORACLE RESULT for the manual fixture (2026-10-22): **it failed to move
 * Start**.  Project reported uid 1 Start 2026-01-04 13:00 (unchanged), Finish
 * 2026-01-16 17:00, Duration 5040 (10.5 d), project finish 01-16.  So the seven
 * words Project itself wrote are not sufficient either: +174 is not the manual
 * Start authority by itself.  The third fixture, `w29-manual2.mpp`, is the last
 * cheap candidate: w29-manual plus the two other semantic differences Project's
 * own save made -- Fixed2Meta entry row+3 +88 set to 0 and uid 1's VarMeta
 * StartText/FinishText entries removed (declared count 42 -> 40) -- with the
 * same date words and Var2Data untouched.  If that also fails, manual scheduling
 * keeps its open status and SNET is the documented save path.
 *
 * WHAT THE TEST PROVES WITHOUT MS PROJECT (the re-runnable part).  For each
 * fixture:
 *   - the container check and check_table are empty, the stream set and the
 *     stream file ranges are unchanged, re-emission is idempotent and the saved
 *     file reloads to the same values;
 *   - the whole-file byte diff against the source contains exactly the expected
 *     number of bytes and every one of them lies inside a targeted word;
 *   - every targeted word changed and holds the expected new value, and every
 *     non-targeted row word (including the ones Project itself left stale)
 *     holds its old value;
 *   - the row reads back through `read_task_values`, no other row changed, and
 *     only the two expected streams differ (all 214/ streams byte-identical).
 *
 * WHAT STILL NEEDS THE ORACLE.  Whether Project's load accepts the file and
 * reports the intended dates.  The coordinator opens the two fixtures and
 * reports every field; a `PASS` here is necessary, not sufficient.
 *
 * OUTPUT (build tree, git-ignored): core/mppwrite/build/vs/test-out/
 * w29-manual.mpp, w29-snet.mpp and w29-manifest.json.  Running the binary again
 * regenerates them.  corpus/ is only read, never written.
 */

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "fixture_guard.h"
#include "row_writer.h"
#include "table.h"

#ifndef MPPWRITE_PROBE_DIR
#define MPPWRITE_PROBE_DIR "corpus/probe"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

char const* const kFixed = "114/TBkndTask/FixedData";
char const* const kFixed2 = "114/TBkndTask/Fixed2Data";

constexpr std::size_t kRow = 1;    /* task row index (row 0 = project row) */
constexpr std::uint32_t kUid = 1u; /* ... and its UniqueID in both fixtures */

/* base.mpp row 1 (uid 1) is the manual task; values measured by W26 and
 * re-derived from the bytes here. */
constexpr std::uint32_t kBaseStart = 0x3bf212c0u;           /* +96  2026-01-05 08:00 */
constexpr std::uint32_t kBaseFinish = 0x3bf627d8u;          /* +100 2026-01-09 17:00 */
constexpr std::uint32_t kBaseEffectiveStart = 0x3bf11e78u;  /* +104/+112/+174 2026-01-04 13:00 */
constexpr std::uint32_t kBaseEffectiveFinish = 0x3bf61c20u; /* +108/+116 2026-01-09 12:00 */
constexpr std::uint32_t kBaseDuration = 24000u;             /* +84/+88 */

/* The intended manual schedule: Monday 2026-01-12 08:00 -> Friday 2026-01-16
 * 17:00, still five working days (24,000 ticks). */
constexpr std::uint32_t kNewStart = 0x3bf912c0u;  /* 2026-01-12 08:00 */
constexpr std::uint32_t kNewFinish = 0x3bfd27d8u; /* 2026-01-16 17:00 */

/* base.mpp duration and slack, which the manual write must not disturb. */
constexpr std::int32_t kBaseSlack = 0;

/* w3-o1.mpp row 1 (uid 1): the auto-scheduled 1-day task. */
constexpr std::uint32_t kAutoStart = 0x3d1112c0u;      /* +96/+104 2026-10-19 08:00 */
constexpr std::uint32_t kAutoFinish = 0x3d1127d8u;     /* +100/+108 2026-10-19 17:00 */
constexpr std::uint32_t kAutoLateStart = 0x3d1812c0u;  /* +112 2026-10-26 08:00 */
constexpr std::uint32_t kAutoLateFinish = 0x3d1827d8u; /* +116 2026-10-26 17:00 */
constexpr std::uint32_t kAutoDuration = 4800u;         /* 1 day */
constexpr std::uint32_t kSnetDate = 0x3d1312c0u;       /* 2026-10-21 08:00 */
constexpr std::uint16_t kSnetType = 4u;                /* PjConstraint SNET */

/* The word set Project wrote on its own manual save (W29 coordinator diff),
 * and the word set this test therefore requires to be the *only* changes. */
constexpr std::size_t kManualFixedWords[] = {104, 108, 112, 116, 174};
constexpr std::size_t kManualFixed2Words[] = {50, 54};

/* Row words the manual write must leave alone; +96/+100 and Fixed2Data +46 are
 * the words Project itself left stale, so they are the important ones.  +172 is
 * a u16 flag and is checked separately (a u32 read at +172 overlaps +174). */
constexpr std::size_t kManualUntouchedFixed[] = {64,  66,  74,  80,  84,  88,
                                                 92,  96,  100, 186, 190};
constexpr std::size_t kManualUntouchedFixed2[] = {46, 58};

/* w3-o1's row words the SNET write must leave alone: every date, duration,
 * slack, anchor and constraint neighbour.  +172 is checked as a u16 for the
 * same overlap reason as above. */
constexpr std::size_t kSnetUntouchedFixed[] = {74,  80,  84,  88,  92,  96,
                                               100, 104, 108, 112, 116, 174,
                                               178, 186, 190};
constexpr std::size_t kSnetUntouchedFixed2[] = {46, 50, 54, 58};

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

void put_u32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    if (o + 4 > b.size()) {
        throw std::runtime_error("u32 write past the end of a stream");
    }
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
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

std::string json_escape(std::string_view text) {
    std::string out;
    for (char const c : text) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::uint64_t fnv1a64(std::vector<std::uint8_t> const& bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (std::uint8_t const byte : bytes) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool starts_with(std::string const& text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
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
    return out;
}

void check_214_untouched(TestContext& t, mppwrite::CfbDocument const& before,
                         mppwrite::CfbDocument const& after,
                         std::string const& label) {
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

std::uint64_t file_offset_for_stream_byte(mppwrite::CfbDocument const& doc,
                                          std::string const& stream,
                                          std::size_t stream_offset) {
    std::size_t remaining = stream_offset;
    for (std::pair<std::uint64_t, std::size_t> const& range :
         doc.stream_file_ranges(stream)) {
        if (remaining < range.second) {
            return range.first + static_cast<std::uint64_t>(remaining);
        }
        remaining -= range.second;
    }
    throw std::runtime_error("stream offset " + std::to_string(stream_offset) +
                             " is past the end of " + stream);
}

/* One 4-byte (or 2-byte) word the fixture is allowed to change. */
struct TargetWord {
    std::string stream;     /* FixedData or Fixed2Data */
    std::size_t offset = 0; /* stream-relative */
    std::size_t row_offset = 0; /* relative to the row / Fixed2Data record */
    std::size_t length = 4; /* 4 for a packed date/u32, 2 for a u16 */
    std::uint32_t old_value = 0;
    std::uint32_t new_value = 0;
    std::string field;
    bool is_date = true;
};

struct WordReport {
    TargetWord word;
    std::uint64_t file_offset = 0;
    std::vector<std::uint64_t> changed_file_bytes;
};

/* Every byte of a stream that differs, as (offset, old, new). */
struct ByteDiff {
    std::size_t offset = 0;
    std::uint8_t old_byte = 0;
    std::uint8_t new_byte = 0;
};

std::vector<ByteDiff> stream_diff(std::vector<std::uint8_t> const& before,
                                  std::vector<std::uint8_t> const& after) {
    std::vector<ByteDiff> out;
    std::size_t const n = std::min(before.size(), after.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (before[i] != after[i]) {
            ByteDiff d;
            d.offset = i;
            d.old_byte = before[i];
            d.new_byte = after[i];
            out.push_back(d);
        }
    }
    return out;
}

/* The core single-variable proof.  Checks, from the emitted bytes:
 *   - the file has the same size as the source;
 *   - the whole-file diff has exactly `expected_file_bytes` bytes;
 *   - every targeted word holds `old_value` before and `new_value` after, and
 *     at least one byte of it changed;
 *   - every differing file byte lies inside a targeted word;
 *   - in each of the two task streams, every differing byte lies inside a
 *     targeted word (and nothing else in either stream moved).
 */
std::vector<WordReport> check_word_changes(
    TestContext& t, mppwrite::CfbDocument const& before,
    mppwrite::CfbDocument const& after, std::string const& label,
    std::vector<TargetWord> const& words, std::size_t expected_file_bytes) {
    t.check(before.to_bytes().size() == after.to_bytes().size(),
            label + ": the file size changed");
    std::vector<std::uint8_t> const a = before.to_bytes();
    std::vector<std::uint8_t> const b = after.to_bytes();
    std::size_t const differing = [&a, &b]() {
        if (a.size() != b.size()) {
            return std::size_t{0};
        }
        std::size_t n = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                ++n;
            }
        }
        return n;
    }();
    t.check(differing == expected_file_bytes,
            label + ": " + std::to_string(differing) +
                " file bytes differ, expected " +
                std::to_string(expected_file_bytes));

    std::vector<std::pair<std::uint64_t, std::uint64_t>> allowed; /* [first,last) */
    std::vector<WordReport> reports;
    for (TargetWord const& word : words) {
        WordReport report;
        report.word = word;
        std::vector<std::uint8_t> const before_stream =
            before.read_stream(word.stream);
        std::vector<std::uint8_t> const after_stream =
            after.read_stream(word.stream);
        std::string const where =
            label + ": " + word.stream + " +" + std::to_string(word.offset) +
            " (" + word.field + ")";
        t.check(word.offset + word.length <= before_stream.size(),
                where + " is past the end of the stream");
        t.check(word.offset + word.length <= after_stream.size(),
                where + " is past the end of the written stream");
        if (word.length == 4) {
            t.check(u32_at(before_stream, word.offset) == word.old_value,
                    where + " holds " + hex32(u32_at(before_stream, word.offset)) +
                        " before the write, expected " + hex32(word.old_value));
            t.check(u32_at(after_stream, word.offset) == word.new_value,
                    where + " holds " + hex32(u32_at(after_stream, word.offset)) +
                        " after the write, expected " + hex32(word.new_value));
        } else {
            t.check(u16_at(before_stream, word.offset) ==
                        static_cast<std::uint16_t>(word.old_value),
                    where + " holds " +
                        std::to_string(u16_at(before_stream, word.offset)) +
                        " before the write, expected " +
                        std::to_string(word.old_value));
            t.check(u16_at(after_stream, word.offset) ==
                        static_cast<std::uint16_t>(word.new_value),
                    where + " holds " +
                        std::to_string(u16_at(after_stream, word.offset)) +
                        " after the write, expected " +
                        std::to_string(word.new_value));
        }
        report.file_offset =
            file_offset_for_stream_byte(before, word.stream, word.offset);
        for (std::size_t i = 0; i < word.length; ++i) {
            if (i < before_stream.size() - word.offset &&
                i < after_stream.size() - word.offset &&
                before_stream[word.offset + i] != after_stream[word.offset + i]) {
                report.changed_file_bytes.push_back(
                    file_offset_for_stream_byte(before, word.stream,
                                                word.offset + i));
            }
        }
        t.check(!report.changed_file_bytes.empty(),
                where + " did not change");
        allowed.emplace_back(report.file_offset,
                             report.file_offset +
                                 static_cast<std::uint64_t>(word.length));
        reports.push_back(std::move(report));
    }

    /* Every differing file byte must sit inside an allowed word. */
    std::size_t stray_file_bytes = 0;
    if (a.size() == b.size()) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] == b[i]) {
                continue;
            }
            bool ok = false;
            for (std::pair<std::uint64_t, std::uint64_t> const& range : allowed) {
                if (static_cast<std::uint64_t>(i) >= range.first &&
                    static_cast<std::uint64_t>(i) < range.second) {
                    ok = true;
                    break;
                }
            }
            if (!ok) {
                ++stray_file_bytes;
            }
        }
    }
    t.check(stray_file_bytes == 0,
            label + ": " + std::to_string(stray_file_bytes) +
                " differing file bytes fall outside the targeted words");

    /* And the same statement per stream, independent of the file mapping. */
    for (std::string const& stream : {std::string(kFixed), std::string(kFixed2)}) {
        std::size_t stray = 0;
        for (ByteDiff const& d :
             stream_diff(before.read_stream(stream), after.read_stream(stream))) {
            bool ok = false;
            for (TargetWord const& word : words) {
                if (word.stream == stream && d.offset >= word.offset &&
                    d.offset < word.offset + word.length) {
                    ok = true;
                    break;
                }
            }
            if (!ok) {
                ++stray;
            }
        }
        t.check(stray == 0, label + ": " + std::to_string(stray) +
                                " non-targeted bytes changed in " + stream);
    }
    /* Same size means the sectors are reused and the mapping cannot move. */
    for (std::string const& stream : {std::string(kFixed), std::string(kFixed2)}) {
        t.check(after.stream_file_ranges(stream) ==
                    before.stream_file_ranges(stream),
                label + ": " + stream + " moved inside the file");
    }
    return reports;
}

/* Everything else that must be true of any well-formed written fixture. */
void check_emitted(TestContext& t, mppwrite::CfbDocument const& before,
                   mppwrite::CfbDocument const& after,
                   std::string const& label,
                   std::vector<std::string> const& expected_streams) {
    t.check(after.check().empty(), label + ": container check: " + after.check());
    t.check(mppwrite::check_table(after).empty(),
            label + ": table check: " + mppwrite::check_table(after));
    t.check(after.stream_paths() == before.stream_paths(),
            label + ": the stream set changed");
    std::vector<std::string> const changed = changed_streams(before, after);
    t.check(changed == expected_streams,
            label + ": changed streams are [" + join_list(changed) +
                "], expected [" + join_list(expected_streams) + "]");
    check_214_untouched(t, before, after, label);
    std::vector<std::uint8_t> const bytes = after.to_bytes();
    mppwrite::CfbDocument const again = mppwrite::CfbDocument::from_bytes(bytes);
    t.check(again.to_bytes() == bytes, label + ": re-emission is not idempotent");
}

/* Save, reload, and prove the reloaded file is the same file and the same row. */
void check_saved(TestContext& t, mppwrite::CfbDocument const& doc,
                 std::string const& file, std::string const& label,
                 mppwrite::TaskValues const& want) {
    std::string const path = out_path(file);
    doc.save(path);
    std::vector<std::uint8_t> const saved = read_file(path);
    t.check(saved == doc.to_bytes(), label + ": saved file differs from to_bytes()");
    mppwrite::CfbDocument const reloaded = mppwrite::CfbDocument::from_file(path);
    t.check(reloaded.check().empty(),
            label + ": reloaded container check: " + reloaded.check());
    t.check(mppwrite::check_table(reloaded).empty(),
            label + ": reloaded table check: " + mppwrite::check_table(reloaded));
    t.check(mppwrite::read_task_values(reloaded, kRow) == want,
            label + ": reloaded row " + std::to_string(kRow) +
                " does not match");
}

/* Other rows must be untouched (their TaskValues are the whole row). */
void check_other_rows(TestContext& t, mppwrite::CfbDocument const& before,
                      mppwrite::CfbDocument const& after,
                      std::string const& label) {
    std::size_t const rows = mppwrite::TaskTable::from_document(before).rows();
    for (std::size_t row = 0; row < rows; ++row) {
        if (row == kRow) {
            continue;
        }
        t.check(mppwrite::read_task_values(after, row) ==
                    mppwrite::read_task_values(before, row),
                label + ": row " + std::to_string(row) +
                    " changed when row " + std::to_string(kRow) + " was written");
    }
}

/* Untouched-word check for the two fixture-specific lists. */
void check_untouched_words(TestContext& t, mppwrite::CfbDocument const& before,
                           mppwrite::CfbDocument const& after,
                           std::string const& label,
                           std::vector<std::size_t> const& fixed_words,
                           std::vector<std::size_t> const& fixed2_words) {
    std::vector<std::uint8_t> const before_fixed = before.read_stream(kFixed);
    std::vector<std::uint8_t> const after_fixed = after.read_stream(kFixed);
    std::size_t const fp = mppwrite::task_row_offset(kRow);
    for (std::size_t const offset : fixed_words) {
        t.check(u32_at(after_fixed, fp + offset) ==
                    u32_at(before_fixed, fp + offset),
                label + ": FixedData +" + std::to_string(offset) +
                    " changed from " + hex32(u32_at(before_fixed, fp + offset)) +
                    " to " + hex32(u32_at(after_fixed, fp + offset)));
    }
    std::vector<std::uint8_t> const before_fixed2 = before.read_stream(kFixed2);
    std::vector<std::uint8_t> const after_fixed2 = after.read_stream(kFixed2);
    std::size_t const f2p = mppwrite::fixed2_record_offset(kRow);
    for (std::size_t const offset : fixed2_words) {
        t.check(u32_at(after_fixed2, f2p + offset) ==
                    u32_at(before_fixed2, f2p + offset),
                label + ": Fixed2Data +" + std::to_string(offset) +
                    " changed from " + hex32(u32_at(before_fixed2, f2p + offset)) +
                    " to " + hex32(u32_at(after_fixed2, f2p + offset)));
    }
}

/* --- the two fixtures ---------------------------------------------------- */

struct FixtureReport {
    std::string file;
    std::string source;
    std::string role;
    std::string note;
    std::string oracle_expectation;
    std::vector<std::string> extra_edits; /* fixture-specific, manifest only */
    std::vector<std::uint8_t> bytes;
    std::vector<WordReport> words;
    std::vector<TargetWord> untouched; /* for the manifest only */
};

std::string probe_label(std::string const& path) {
    return (std::filesystem::path("corpus/probe") /
            std::filesystem::path(path).filename())
        .generic_string();
}

FixtureReport build_manual_fixture(TestContext& t, std::string const& source) {
    FixtureReport fixture;
    fixture.file = "w29-manual.mpp";
    fixture.source = probe_label(source);
    fixture.role =
        "W29-A: manual task uid 1 moved by write_manual_task_row to "
        "2026-01-12 08:00 -> 2026-01-16 17:00";
    fixture.note =
        "Exactly the word set Project wrote on its own save of this manual task "
        "(corpus/probe/manual-start-edited.mpp): FixedData +104/+108, "
        "+112/+116, +174 and Fixed2Data +50/+54. +96/+100 (calculated "
        "Start/Finish), +84/+88 (duration), Fixed2Data +46/+58 and the "
        "constraint were left as stored.";
    fixture.oracle_expectation =
        "uid 1 Start 2026-01-12 08:00, Finish 2026-01-16 17:00, Duration 2400 "
        "minutes (5 d); Project Finish 2026-01-16 17:00; TotalSlack 0. Compare "
        "with base.mpp's own open (Start 2026-01-04 13:00, Finish 2026-01-09 "
        "12:00): the Start must be the NEW date for this fixture to pass.";

    mppwrite::CfbDocument const before =
        mppwrite::CfbDocument::from_file(source);
    mppwrite::CfbDocument doc =
        mppwrite::CfbDocument::from_bytes(before.source_bytes());

    /* Anchors: fail loudly if the source is not the file this test was built on. */
    mppwrite::TaskValues const anchor = mppwrite::read_task_values(before, kRow);
    t.check(mppwrite::TaskTable::from_document(before).uid(kRow) == kUid,
            "base.mpp row 1 is not uid 1");
    t.check(anchor.start == kBaseStart && anchor.finish == kBaseFinish,
            "base.mpp row 1 Start/Finish are not 2026-01-05/2026-01-09");
    t.check(anchor.project_start == kBaseEffectiveStart &&
                anchor.project_finish == kBaseEffectiveFinish,
            "base.mpp row 1 effective Start/Finish are not 2026-01-04 13:00 / "
            "2026-01-09 12:00");
    t.check(anchor.late_start == kBaseEffectiveStart &&
                anchor.late_finish == kBaseEffectiveFinish,
            "base.mpp row 1 late dates are not the effective dates (slack 0)");
    t.check(anchor.start_anchor == kBaseEffectiveStart,
            "base.mpp row 1 start anchor (+174) is not 2026-01-04 13:00");
    t.check(anchor.duration == kBaseDuration &&
                anchor.remaining_duration == kBaseDuration &&
                anchor.actual_duration == 0,
            "base.mpp row 1 duration is not 24000/24000/0");
    t.check(anchor.total_slack == kBaseSlack && anchor.start_slack == kBaseSlack,
            "base.mpp row 1 slack is not 0/0");
    t.check(anchor.constraint_type == 0 &&
                anchor.constraint_date == mppwrite::kInvalidDate,
            "base.mpp row 1 constraint is not ASAP with no date");
    t.check(mppwrite::format_date(kNewStart) == "2026-01-12 08:00" &&
                mppwrite::format_date(kNewFinish) == "2026-01-16 17:00",
            "the intended manual dates do not decode as expected");
    t.check(kNewStart - kBaseStart == (7u << 16) &&
                kNewFinish - kBaseFinish == (7u << 16),
            "the intended manual dates are not exactly +7 days");

    mppwrite::ManualTaskWrite write;
    write.start = kNewStart;
    write.finish = kNewFinish;
    write.late_start = mppwrite::kInherit;  /* copy start: zero slack */
    write.late_finish = mppwrite::kInherit; /* copy finish */
    mppwrite::ManualWriteReport const report =
        mppwrite::write_manual_task_row(doc, kRow, write);
    t.check(report.warnings.empty(),
            "the manual write produced warnings: " +
                join_list(report.warnings));
    t.check(report.before == anchor, "the manual write changed the row's before-read");
    t.check(report.after.project_start == kNewStart &&
                report.after.project_finish == kNewFinish &&
                report.after.late_start == kNewStart &&
                report.after.late_finish == kNewFinish &&
                report.after.start_anchor == kNewStart,
            "the manual write's report does not carry the new effective dates");
    t.check(report.after.start == kBaseStart &&
                report.after.finish == kBaseFinish,
            "the manual write moved the calculated Start/Finish (+96/+100)");

    mppwrite::TaskValues want = anchor;
    want.project_start = kNewStart;
    want.project_finish = kNewFinish;
    want.late_start = kNewStart;
    want.late_finish = kNewFinish;
    want.start_anchor = kNewStart;
    want.fixed2_finish = kNewFinish;
    t.check(mppwrite::read_task_values(doc, kRow) == want,
            "the written manual row does not read back as intended");
    t.check(report.after == want, "the manual write report is not the read-back row");

    std::vector<TargetWord> words;
    auto add_word = [&words](std::string stream, std::size_t base_offset,
                             std::size_t row_offset, std::uint32_t old_value,
                             std::uint32_t new_value, std::string field,
                             std::size_t length) {
        TargetWord word;
        word.stream = std::move(stream);
        word.offset = base_offset + row_offset;
        word.row_offset = row_offset;
        word.length = length;
        word.old_value = old_value;
        word.new_value = new_value;
        word.field = std::move(field);
        word.is_date = length == 4;
        words.push_back(std::move(word));
    };
    std::size_t const fp = mppwrite::task_row_offset(kRow);
    std::size_t const f2p = mppwrite::fixed2_record_offset(kRow);
    add_word(kFixed, fp, 104, kBaseEffectiveStart, kNewStart,
             "effective Start (+104)", 4);
    add_word(kFixed, fp, 108, kBaseEffectiveFinish, kNewFinish,
             "effective Finish (+108)", 4);
    add_word(kFixed, fp, 112, kBaseEffectiveStart, kNewStart, "LateStart (+112)", 4);
    add_word(kFixed, fp, 116, kBaseEffectiveFinish, kNewFinish,
             "LateFinish (+116)", 4);
    add_word(kFixed, fp, 174, kBaseEffectiveStart, kNewStart,
             "start anchor (+174)", 4);
    add_word(kFixed2, f2p, 50, kBaseEffectiveStart, kNewStart,
             "Fixed2Data start copy (+50)", 4);
    add_word(kFixed2, f2p, 54, kBaseEffectiveFinish, kNewFinish,
             "Fixed2Data finish copy (+54)", 4);

    for (std::size_t const offset : kManualFixedWords) {
        t.check(std::any_of(words.begin(), words.end(),
                            [offset, fp](TargetWord const& w) {
                                return w.stream == kFixed &&
                                       w.offset == fp + offset;
                            }),
                "the manual target list is missing FixedData +" +
                    std::to_string(offset));
    }
    for (std::size_t const offset : kManualFixed2Words) {
        t.check(std::any_of(words.begin(), words.end(),
                            [offset, f2p](TargetWord const& w) {
                                return w.stream == kFixed2 &&
                                       w.offset == f2p + offset;
                            }),
                "the manual target list is missing Fixed2Data +" +
                    std::to_string(offset));
    }

    /* Seven 4-byte words.  Only three bytes of each date word differ in this
     * fixture: the packed date's top byte (the day's high byte) is the same
     * before and after the +7-day move. */
    fixture.words =
        check_word_changes(t, before, doc, fixture.file, words, 21);
    t.check(fixture.words.size() == 7, "the manual fixture does not name 7 words");
    std::size_t changed_bytes = 0;
    for (WordReport const& word : fixture.words) {
        changed_bytes += word.changed_file_bytes.size();
    }
    t.check(changed_bytes == 21,
            "the manual fixture changed " + std::to_string(changed_bytes) +
                " bytes across its words, expected 21");

    check_untouched_words(t, before, doc, fixture.file,
                          std::vector<std::size_t>(
                              std::begin(kManualUntouchedFixed),
                              std::end(kManualUntouchedFixed)),
                          std::vector<std::size_t>(
                              std::begin(kManualUntouchedFixed2),
                              std::end(kManualUntouchedFixed2)));
    /* The words Project itself left stale, called out separately because they
     * are the reason this path exists. */
    std::vector<std::uint8_t> const fixed_bytes = doc.read_stream(kFixed);
    t.check(u32_at(fixed_bytes, fp + 96) == kBaseStart,
            "the manual write moved the calculated Start (+96)");
    t.check(u32_at(fixed_bytes, fp + 100) == kBaseFinish,
            "the manual write moved the calculated Finish (+100)");
    t.check(u32_at(fixed_bytes, fp + 84) == kBaseDuration &&
                u32_at(fixed_bytes, fp + 88) == kBaseDuration,
            "the manual write moved the duration family (+84/+88)");
    t.check(u16_at(fixed_bytes, fp + 172) == static_cast<std::uint16_t>(1),
            "the manual write moved the +172 u16 flag");
    std::vector<std::uint8_t> const fixed2_bytes = doc.read_stream(kFixed2);
    t.check(u32_at(fixed2_bytes, f2p + 46) == kBaseEffectiveStart,
            "the manual write moved Fixed2Data +46 (Project left it stale)");
    t.check(u32_at(fixed2_bytes, f2p + 58) == kBaseDuration,
            "the manual write moved Fixed2Data +58");
    t.check(u32_at(fixed2_bytes, f2p + 50) == kNewStart &&
                u32_at(fixed2_bytes, f2p + 54) == kNewFinish,
            "the manual write did not put the new dates in Fixed2Data +50/+54");

    check_emitted(t, before, doc, fixture.file,
                  std::vector<std::string>{kFixed, kFixed2});
    check_other_rows(t, before, doc, fixture.file);
    t.check(mppwrite::read_task_values(doc, kRow) == want,
            "the manual row does not read back after the stream checks");
    check_saved(t, doc, fixture.file, fixture.file, want);

    fixture.bytes = doc.to_bytes();
    for (std::size_t const offset : kManualUntouchedFixed) {
        TargetWord word;
        word.stream = kFixed;
        word.offset = fp + offset;
        word.row_offset = offset;
        word.length = 4;
        word.old_value = u32_at(before.read_stream(kFixed), fp + offset);
        word.new_value = word.old_value;
        word.field = "untouched: FixedData +" + std::to_string(offset);
        word.is_date = true;
        fixture.untouched.push_back(std::move(word));
    }
    for (std::size_t const offset : kManualUntouchedFixed2) {
        TargetWord word;
        word.stream = kFixed2;
        word.offset = f2p + offset;
        word.row_offset = offset;
        word.length = 4;
        word.old_value = u32_at(before.read_stream(kFixed2), f2p + offset);
        word.new_value = word.old_value;
        word.field = "untouched: Fixed2Data +" + std::to_string(offset);
        word.is_date = true;
        fixture.untouched.push_back(std::move(word));
    }
    return fixture;
}

/* --- W29 candidate C: the Project save's remaining differences ----------- */

/*
 * w29-manual2.mpp = w29-manual.mpp plus the two semantic differences Project's
 * own save made that the seven date words do not cover:
 *
 *   - Fixed2Meta entry row+3 byte +88: u32 0x0000000c -> 0x00000000 (the only
 *     Fixed2Meta byte Project changed);
 *   - the uid-1 VarMeta entries for 0x0b400505 and 0x0b400506
 *     (StartText/FinishText) removed, header entry count 42 -> 40.
 *
 * Project did it append-only: the stream keeps its 528 bytes, the 40 remaining
 * entries are packed from offset 24, and the last 24 bytes are the stale tail of
 * the old entry array -- which is exactly the shape manual-start-edited.mpp
 * contains.  Var2Data is NOT touched: the old blocks stay in place and become
 * unreferenced, as they do in Project's save (its 508-byte Var2Data differs only
 * by an appended project-title block, which is not a date store).
 *
 * This is a fixture generator, not a second writer: the extra edits are raw
 * bytes so the oracle can answer "is THIS the shape Project trusts?" without
 * committing an API for an unverified layout.
 */

struct VarEntryLike {
    std::uint32_t mid = 0;
    std::uint32_t offset = 0;
    std::uint32_t key = 0;
};

std::uint32_t var_declared_count(std::vector<std::uint8_t> const& var_meta) {
    return u32_at(var_meta, 8);
}

std::vector<VarEntryLike> declared_var_entries(
    std::vector<std::uint8_t> const& var_meta) {
    std::vector<VarEntryLike> out;
    std::uint32_t const count = var_declared_count(var_meta);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::size_t const p = 24 + static_cast<std::size_t>(i) * 12;
        if (p + 12 > var_meta.size()) {
            throw std::runtime_error("VarMeta declares more entries than fit");
        }
        VarEntryLike e;
        e.mid = u32_at(var_meta, p);
        e.offset = u32_at(var_meta, p + 4);
        e.key = u32_at(var_meta, p + 8);
        out.push_back(e);
    }
    return out;
}

bool same_var_entry(VarEntryLike const& a, VarEntryLike const& b) {
    return a.mid == b.mid && a.offset == b.offset && a.key == b.key;
}

FixtureReport build_manual2_fixture(TestContext& t,
                                    std::string const& source,
                                    FixtureReport const& manual) {
    FixtureReport fixture;
    fixture.file = "w29-manual2.mpp";
    fixture.source = probe_label(source);
    fixture.role =
        "W29-C: w29-manual.mpp plus Project's two remaining save differences "
        "(Fixed2Meta row+3 +88 = 0; uid 1 VarMeta StartText/FinishText entries "
        "removed, count 42 -> 40)";
    fixture.note =
        "Same seven date words as w29-manual.mpp (FixedData +104/+108, "
        "+112/+116, +174; Fixed2Data +50/+54). The extra edits are the only "
        "semantic differences left against Project's own save of the same "
        "manual Start edit. Var2Data is untouched; the 0x0b400564 bytes already "
        "match Project's save.";
    fixture.oracle_expectation =
        "uid 1 Start 2026-01-12 08:00, Finish 2026-01-16 17:00, Duration 2400 "
        "minutes; project finish 2026-01-16 17:00. If Start is still "
        "2026-01-04 13:00, the manual Start authority is elsewhere and manual "
        "scheduling keeps its open status.";
    fixture.extra_edits = {
        "Fixed2Meta entry row+3 +88: 0x0000000c -> 0x00000000 (the only "
        "Fixed2Meta byte Project changed on its own manual save)",
        "VarMeta: removed uid 1 entries 0x0b400505 (StartText) and 0x0b400506 "
        "(FinishText); declared entry count 42 -> 40; stream size unchanged at "
        "528 B and the last 24 B are the stale tail of the old entry array; "
        "Var2Data untouched"};

    mppwrite::CfbDocument const before =
        mppwrite::CfbDocument::from_file(source);
    mppwrite::CfbDocument doc =
        mppwrite::CfbDocument::from_bytes(before.source_bytes());
    mppwrite::ManualTaskWrite write;
    write.start = kNewStart;
    write.finish = kNewFinish;
    mppwrite::write_manual_task_row(doc, kRow, write);

    /* --- extra edit 1: Fixed2Meta entry row+3 byte +88 ------------------- */
    std::string const fixed2_meta = "114/TBkndTask/Fixed2Meta";
    std::string const var_meta = "114/TBkndTask/VarMeta";
    std::string const var2_data = "114/TBkndTask/Var2Data";
    std::vector<std::uint8_t> f2m = doc.read_stream(fixed2_meta);
    std::size_t const entry =
        16 + 96 * (kRow + 3); /* 96-byte entry per row, 16-byte header */
    t.check(entry + 96 <= f2m.size(), "Fixed2Meta is too short for row 1");
    t.check(u32_at(f2m, entry + 88) == 0x0000000cu,
            "w29-manual Fixed2Meta row+3 +88 is not 0x0c");
    put_u32(f2m, entry + 88, 0u);
    doc.replace_stream(fixed2_meta, f2m);

    /* --- extra edit 2: drop uid 1's StartText/FinishText VarMeta entries - */
    std::vector<std::uint8_t> vm = doc.read_stream(var_meta);
    std::vector<VarEntryLike> const old_entries = declared_var_entries(vm);
    t.check(old_entries.size() == 42,
            "w29-manual VarMeta does not declare 42 entries");
    std::vector<VarEntryLike> kept;
    std::size_t dropped = 0;
    for (VarEntryLike const& e : old_entries) {
        bool const is_text =
            e.mid == kUid && (e.key == 0x0b400505u || e.key == 0x0b400506u);
        if (is_text) {
            ++dropped;
        } else {
            kept.push_back(e);
        }
    }
    t.check(dropped == 2,
            "w29-manual does not hold the two uid 1 StartText/FinishText "
            "entries");
    t.check(kept.size() == 40, "the VarMeta drop did not leave 40 entries");
    std::size_t const used = 24 + kept.size() * 12;
    std::vector<std::uint8_t> new_vm = vm; /* header + stale tail kept */
    for (std::size_t i = 0; i < kept.size(); ++i) {
        std::size_t const p = 24 + i * 12;
        put_u32(new_vm, p, kept[i].mid);
        put_u32(new_vm, p + 4, kept[i].offset);
        put_u32(new_vm, p + 8, kept[i].key);
    }
    put_u32(new_vm, 8, static_cast<std::uint32_t>(kept.size()));
    doc.replace_stream(var_meta, new_vm);

    /* --- the extra edits are exactly what they claim --------------------- */
    mppwrite::CfbDocument const manual_doc =
        mppwrite::CfbDocument::from_bytes(manual.bytes);
    t.check(doc.read_stream("114/TBkndTask/FixedData") ==
                manual_doc.read_stream("114/TBkndTask/FixedData"),
            "w29-manual2 moved FixedData relative to w29-manual");
    t.check(doc.read_stream("114/TBkndTask/Fixed2Data") ==
                manual_doc.read_stream("114/TBkndTask/Fixed2Data"),
            "w29-manual2 moved Fixed2Data relative to w29-manual");
    t.check(doc.read_stream(var2_data) == manual_doc.read_stream(var2_data),
            "w29-manual2 moved Var2Data relative to w29-manual");
    std::vector<ByteDiff> const f2m_diff =
        stream_diff(manual_doc.read_stream(fixed2_meta),
                    doc.read_stream(fixed2_meta));
    t.check(f2m_diff.size() == 1,
            "w29-manual2 changed " + std::to_string(f2m_diff.size()) +
                " Fixed2Meta bytes, expected 1");
    if (!f2m_diff.empty()) {
        t.check(f2m_diff.front().offset == entry + 88,
                "w29-manual2 changed the wrong Fixed2Meta byte");
    }
    t.check(u32_at(doc.read_stream(fixed2_meta), entry + 88) == 0u,
            "w29-manual2 did not clear Fixed2Meta row+3 +88");

    std::vector<std::uint8_t> const vm_new = doc.read_stream(var_meta);
    t.check(vm_new.size() == vm.size(),
            "w29-manual2 changed the VarMeta stream size");
    t.check(var_declared_count(vm_new) == 40,
            "w29-manual2 VarMeta does not declare 40 entries");
    t.check(u32_at(vm_new, 0x14) ==
                static_cast<std::uint32_t>(doc.read_stream(var2_data).size()),
            "w29-manual2 VarMeta's Var2Data size is wrong");
    std::vector<VarEntryLike> const new_entries = declared_var_entries(vm_new);
    t.check(new_entries.size() == kept.size(),
            "w29-manual2's declared entry count does not match the rewrite");
    bool entries_match = new_entries.size() == kept.size();
    for (std::size_t i = 0; entries_match && i < kept.size(); ++i) {
        entries_match = same_var_entry(new_entries[i], kept[i]);
    }
    t.check(entries_match,
            "w29-manual2's VarMeta entries are not base minus the two uid 1 "
            "StartText/FinishText entries");
    bool tail_kept = true;
    for (std::size_t i = used; i < vm.size(); ++i) {
        if (vm_new[i] != vm[i]) {
            tail_kept = false;
            break;
        }
    }
    t.check(tail_kept, "w29-manual2 rewrote the stale VarMeta tail");

    /* --- the row itself is unchanged from w29-manual --------------------- */
    mppwrite::TaskValues want = mppwrite::read_task_values(manual_doc, kRow);
    t.check(mppwrite::read_task_values(doc, kRow) == want,
            "w29-manual2's row does not read back as w29-manual's");
    t.check(want.project_start == kNewStart && want.project_finish == kNewFinish,
            "w29-manual2's row does not hold the intended dates");

    check_emitted(t, manual_doc, doc, fixture.file,
                  std::vector<std::string>{var_meta, fixed2_meta});
    check_other_rows(t, manual_doc, doc, fixture.file);
    check_saved(t, doc, fixture.file, fixture.file, want);

    fixture.bytes = doc.to_bytes();
    /* The date words are the same seven as w29-manual's (same file layout, so
     * the WordReports' file offsets are still valid). */
    fixture.words = manual.words;
    return fixture;
}

FixtureReport build_snet_fixture(TestContext& t, std::string const& source) {
    FixtureReport fixture;
    fixture.file = "w29-snet.mpp";
    fixture.source = probe_label(source);
    fixture.role =
        "W29-B: auto task uid 1 pinned with ConstraintType=4 (SNET) and "
        "ConstraintDate=2026-10-21 08:00";
    fixture.note =
        "w3-o1.mpp row 1 with only +64 (u16 ConstraintType) and +66 (u32 "
        "ConstraintDate) written. Every stored date is left as it was, so "
        "Project must move the task to the constraint date by recalculation. "
        "w3-o1's uid 2/3/4 already store type 4 + a date at the same offsets, "
        "so the encoding is copied from Project's own file, not invented.";
    fixture.oracle_expectation =
        "uid 1 Start 2026-10-21 08:00, Finish 2026-10-21 17:00, Duration 480 "
        "minutes (1 d), ConstraintType SNET, ConstraintDate 2026-10-21 08:00. "
        "Compare with w3-o1's own open (Start 2026-10-19 08:00, Finish 17:00): "
        "the Start must be the NEW date for this fixture to pass.";

    mppwrite::CfbDocument const before =
        mppwrite::CfbDocument::from_file(source);
    mppwrite::CfbDocument doc =
        mppwrite::CfbDocument::from_bytes(before.source_bytes());

    mppwrite::TaskValues const anchor = mppwrite::read_task_values(before, kRow);
    mppwrite::TaskConstraint const constraint =
        mppwrite::read_task_constraint(before, kRow);
    t.check(mppwrite::TaskTable::from_document(before).uid(kRow) == kUid,
            "w3-o1.mpp row 1 is not uid 1");
    t.check(anchor.start == kAutoStart && anchor.finish == kAutoFinish,
            "w3-o1.mpp row 1 Start/Finish are not 2026-10-19 08:00/17:00");
    t.check(anchor.project_start == kAutoStart &&
                anchor.project_finish == kAutoFinish,
            "w3-o1.mpp row 1 effective dates are not the Start/Finish");
    t.check(anchor.late_start == kAutoLateStart &&
                anchor.late_finish == kAutoLateFinish,
            "w3-o1.mpp row 1 late dates are not 2026-10-26 08:00/17:00");
    t.check(anchor.duration == kAutoDuration &&
                anchor.remaining_duration == kAutoDuration,
            "w3-o1.mpp row 1 duration is not 4800 ticks");
    t.check(anchor.start_anchor == mppwrite::kInvalidDate,
            "w3-o1.mpp row 1 start anchor (+174) is not 0xFFFFFFFF");
    t.check(constraint.type == 0 &&
                constraint.date == mppwrite::kInvalidDate,
            "w3-o1.mpp row 1 constraint is not ASAP with no date");
    t.check(mppwrite::format_date(kSnetDate) == "2026-10-21 08:00",
            "the SNET date does not decode as expected");

    /* `pin_task_start` is the supported date write; it must be exactly
     * `write_task_constraint` with SNET + the date. */
    mppwrite::TaskConstraint const written =
        mppwrite::pin_task_start(doc, kRow, kSnetDate);
    {
        mppwrite::CfbDocument raw =
            mppwrite::CfbDocument::from_bytes(before.source_bytes());
        mppwrite::TaskConstraint raw_constraint;
        raw_constraint.type = kSnetType;
        raw_constraint.date = kSnetDate;
        mppwrite::write_task_constraint(raw, kRow, raw_constraint);
        t.check(raw.to_bytes() == doc.to_bytes(),
                "pin_task_start and write_task_constraint(SNET, date) disagree");
    }
    t.check(written.type == kSnetType && written.date == kSnetDate,
            "the SNET write did not read back as written");
    t.check(mppwrite::read_task_constraint(doc, kRow).type == kSnetType &&
                mppwrite::read_task_constraint(doc, kRow).date == kSnetDate,
            "the written SNET constraint is not in the file");

    mppwrite::TaskValues want = anchor;
    want.constraint_type = kSnetType;
    want.constraint_date = kSnetDate;
    t.check(mppwrite::read_task_values(doc, kRow) == want,
            "the written SNET row does not read back as intended");

    std::size_t const fp = mppwrite::task_row_offset(kRow);
    std::vector<TargetWord> words;
    TargetWord type_word;
    type_word.stream = kFixed;
    type_word.offset = fp + 64;
    type_word.row_offset = 64;
    type_word.length = 2;
    type_word.old_value = 0;
    type_word.new_value = kSnetType;
    type_word.field = "ConstraintType (+64)";
    type_word.is_date = false;
    words.push_back(std::move(type_word));
    TargetWord date_word;
    date_word.stream = kFixed;
    date_word.offset = fp + 66;
    date_word.row_offset = 66;
    date_word.length = 4;
    date_word.old_value = mppwrite::kInvalidDate;
    date_word.new_value = kSnetDate;
    date_word.field = "ConstraintDate (+66)";
    date_word.is_date = true;
    words.push_back(std::move(date_word));

    /* 0x0000 -> 0x0004 is one byte; 0xFFFFFFFF -> 0x3d1312c0 is four. */
    fixture.words = check_word_changes(t, before, doc, fixture.file, words, 5);
    t.check(fixture.words.size() == 2, "the SNET fixture does not name 2 words");

    check_untouched_words(t, before, doc, fixture.file,
                          std::vector<std::size_t>(
                              std::begin(kSnetUntouchedFixed),
                              std::end(kSnetUntouchedFixed)),
                          std::vector<std::size_t>(
                              std::begin(kSnetUntouchedFixed2),
                              std::end(kSnetUntouchedFixed2)));
    std::vector<std::uint8_t> const fixed_bytes = doc.read_stream(kFixed);
    t.check(u32_at(fixed_bytes, fp + 96) == kAutoStart &&
                u32_at(fixed_bytes, fp + 100) == kAutoFinish &&
                u32_at(fixed_bytes, fp + 104) == kAutoStart &&
                u32_at(fixed_bytes, fp + 108) == kAutoFinish &&
                u32_at(fixed_bytes, fp + 112) == kAutoLateStart &&
                u32_at(fixed_bytes, fp + 116) == kAutoLateFinish,
            "the SNET write moved a date word");
    t.check(u32_at(fixed_bytes, fp + 84) == kAutoDuration &&
                u32_at(fixed_bytes, fp + 88) == kAutoDuration,
            "the SNET write moved the duration family");
    t.check(u16_at(fixed_bytes, fp + 172) == static_cast<std::uint16_t>(1),
            "the SNET write moved the +172 u16 flag");

    check_emitted(t, before, doc, fixture.file,
                  std::vector<std::string>{kFixed});
    check_other_rows(t, before, doc, fixture.file);
    check_saved(t, doc, fixture.file, fixture.file, want);

    fixture.bytes = doc.to_bytes();
    for (std::size_t const offset : kSnetUntouchedFixed) {
        TargetWord word;
        word.stream = kFixed;
        word.offset = fp + offset;
        word.row_offset = offset;
        word.length = 4;
        word.old_value = u32_at(before.read_stream(kFixed), fp + offset);
        word.new_value = word.old_value;
        word.field = "untouched: FixedData +" + std::to_string(offset);
        word.is_date = true;
        fixture.untouched.push_back(std::move(word));
    }
    for (std::size_t const offset : kSnetUntouchedFixed2) {
        TargetWord word;
        word.stream = kFixed2;
        word.offset = mppwrite::fixed2_record_offset(kRow) + offset;
        word.row_offset = offset;
        word.length = 4;
        word.old_value = u32_at(before.read_stream(kFixed2),
                                mppwrite::fixed2_record_offset(kRow) + offset);
        word.new_value = word.old_value;
        word.field = "untouched: Fixed2Data +" + std::to_string(offset);
        word.is_date = true;
        fixture.untouched.push_back(std::move(word));
    }
    return fixture;
}

/* Refusals: the write paths must not produce a partial file. */
void check_refusals(TestContext& t, std::string const& manual_source,
                    std::string const& snet_source) {
    {
        mppwrite::CfbDocument doc =
            mppwrite::CfbDocument::from_file(manual_source);
        std::vector<std::uint8_t> const before = doc.to_bytes();
        mppwrite::ManualTaskWrite write;
        write.start = kNewStart;
        write.finish = kNewFinish;
        bool threw = false;
        try {
            mppwrite::write_manual_task_row(doc, 0, write);
        } catch (mppwrite::Error const&) {
            threw = true;
        }
        t.check(threw, "write_manual_task_row did not refuse the project row");
        t.check(doc.to_bytes() == before,
                "write_manual_task_row changed bytes when it refused row 0");

        mppwrite::ManualTaskWrite bad = write;
        bad.start = mppwrite::kInvalidDate;
        threw = false;
        try {
            mppwrite::write_manual_task_row(doc, kRow, bad);
        } catch (mppwrite::Error const&) {
            threw = true;
        }
        t.check(threw, "write_manual_task_row did not refuse an invalid start");
        t.check(doc.to_bytes() == before,
                "write_manual_task_row changed bytes when it refused a bad start");
    }
    {
        mppwrite::CfbDocument doc =
            mppwrite::CfbDocument::from_file(snet_source);
        std::vector<std::uint8_t> const before = doc.to_bytes();
        mppwrite::TaskConstraint bad;
        bad.type = kSnetType;
        bad.date = mppwrite::kInvalidDate;
        bool threw = false;
        try {
            mppwrite::write_task_constraint(doc, kRow, bad);
        } catch (mppwrite::Error const&) {
            threw = true;
        }
        t.check(threw,
                "write_task_constraint did not refuse an SNET without a date");
        t.check(doc.to_bytes() == before,
                "write_task_constraint changed bytes when it refused a bad date");

        /* A date without a type cannot be honoured: ASAP stores no date. */
        bad.type = 0;
        bad.date = kSnetDate;
        threw = false;
        try {
            mppwrite::write_task_constraint(doc, kRow, bad);
        } catch (mppwrite::Error const&) {
            threw = true;
        }
        t.check(threw,
                "write_task_constraint did not refuse a date without a type");
        t.check(doc.to_bytes() == before,
                "write_task_constraint changed bytes when it refused a "
                "date-only write");

        /* pin_task_start is SNET + a date, so the date must be valid. */
        threw = false;
        try {
            mppwrite::pin_task_start(doc, kRow, mppwrite::kInvalidDate);
        } catch (mppwrite::Error const&) {
            threw = true;
        }
        t.check(threw, "pin_task_start did not refuse an invalid date");
        t.check(doc.to_bytes() == before,
                "pin_task_start changed bytes when it refused an invalid date");
    }
}

/* --- manifest ------------------------------------------------------------ */

std::string manifest_word(WordReport const& report) {
    std::ostringstream out;
    TargetWord const& word = report.word;
    out << "{\"stream\": \"" << json_escape(word.stream) << "\", "
        << "\"stream_offset\": " << word.offset << ", "
        << "\"row_offset\": " << word.row_offset << ", "
        << "\"file_offset\": " << report.file_offset << ", "
        << "\"length\": " << word.length << ", "
        << "\"field\": \"" << json_escape(word.field) << "\", "
        << "\"old\": \"" << hex32(word.old_value) << "\", "
        << "\"new\": \"" << hex32(word.new_value) << "\"";
    if (word.is_date) {
        out << ", \"old_decodes\": \""
            << mppwrite::format_date(word.old_value)
            << "\", \"new_decodes\": \""
            << mppwrite::format_date(word.new_value) << "\"";
    } else {
        out << ", \"old_u16\": " << word.old_value
            << ", \"new_u16\": " << word.new_value;
    }
    out << ", \"changed_file_bytes\": [";
    for (std::size_t i = 0; i < report.changed_file_bytes.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << report.changed_file_bytes[i];
    }
    out << "]}";
    return out.str();
}

std::string manifest_untouched(TargetWord const& word) {
    std::ostringstream out;
    out << "{\"stream\": \"" << json_escape(word.stream) << "\", "
        << "\"stream_offset\": " << word.offset << ", "
        << "\"row_offset\": " << word.row_offset << ", "
        << "\"old\": \"" << hex32(word.old_value) << "\", "
        << "\"new\": \"" << hex32(word.new_value) << "\"";
    if (word.is_date) {
        out << ", \"decodes\": \"" << mppwrite::format_date(word.old_value)
            << "\"";
    }
    out << "}";
    return out.str();
}

std::string make_manifest(std::vector<FixtureReport> const& fixtures) {
    std::ostringstream out;
    out << "{\n"
        << "  \"stream\": \"W29\",\n"
        << "  \"generated_by\": \"mppwrite_roundtrip_manual "
           "(tests/roundtrip_manual_start.cpp)\",\n"
        << "  \"oracle_question\": \"Does Project accept the manual-task word "
           "set it wrote itself (+104/+108, +112/+116, +174, Fixed2Data "
           "+50/+54), and does a real SNET constraint pin an auto task?\",\n"
        << "  \"oracle_note_time\": \"Project's COM date properties are local "
           "wall clock; compare the reported strings with the values below, no "
           "timezone conversion.\",\n"
        << "  \"prior_oracle_result_2026_10_22\": {\"w29-snet.mpp\": \"SUCCESS "
           "-- uid 1 Start 2026-10-21 08:00, Finish 17:00, Early 10-21, Late "
           "10-26, slack 1440; project start recalculated to 2026-10-20 "
           "08:00\", \"w29-manual.mpp\": \"FAILED to move Start -- reported "
           "Start 2026-01-04 13:00 unchanged, Finish 2026-01-16 17:00, "
           "Duration 5040 (10.5 d), project finish 01-16\", \"w29-manual2.mpp\": "
           "\"pending\"},\n"
        << "  \"fixtures\": [\n";
    for (std::size_t f = 0; f < fixtures.size(); ++f) {
        FixtureReport const& fixture = fixtures[f];
        out << "    {\n"
            << "      \"file\": \"" << json_escape(fixture.file) << "\",\n"
            << "      \"source\": \"" << json_escape(fixture.source) << "\",\n"
            << "      \"role\": \"" << json_escape(fixture.role) << "\",\n"
            << "      \"bytes\": " << fixture.bytes.size() << ",\n"
            << "      \"fnv1a64\": \"" << hex64(fnv1a64(fixture.bytes))
            << "\",\n"
            << "      \"note\": \"" << json_escape(fixture.note) << "\",\n"
            << "      \"extra_edits\": [";
        for (std::size_t e = 0; e < fixture.extra_edits.size(); ++e) {
            if (e != 0) {
                out << ", ";
            }
            out << "\"" << json_escape(fixture.extra_edits[e]) << "\"";
        }
        out << "],\n"
            << "      \"oracle_should_report\": \""
            << json_escape(fixture.oracle_expectation) << "\",\n"
            << "      \"task_row_index\": " << kRow << ",\n"
            << "      \"task_unique_id\": " << kUid << ",\n"
            << "      \"changed_words\": [\n";
        for (std::size_t w = 0; w < fixture.words.size(); ++w) {
            out << "        " << manifest_word(fixture.words[w]);
            if (w + 1 != fixture.words.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ],\n"
            << "      \"untouched_words\": [\n";
        for (std::size_t w = 0; w < fixture.untouched.size(); ++w) {
            out << "        " << manifest_untouched(fixture.untouched[w]);
            if (w + 1 != fixture.untouched.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ]\n"
            << "    }";
        if (f + 1 != fixtures.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

} /* namespace */

int main() {
    TestContext t;
    try {
        std::string const manual_source = join(MPPWRITE_PROBE_DIR, "base.mpp");
        std::string const snet_source = join(MPPWRITE_PROBE_DIR, "w3-o1.mpp");
        if (!fixture::corpus_present(manual_source) ||
            !fixture::corpus_present(snet_source)) {
            return 0;
        }
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);

        std::vector<std::uint8_t> const manual_before = read_file(manual_source);
        std::vector<std::uint8_t> const snet_before = read_file(snet_source);
        t.check(!manual_before.empty() && !snet_before.empty(),
                "a source fixture is empty");

        FixtureReport manual = build_manual_fixture(t, manual_source);
        FixtureReport snet = build_snet_fixture(t, snet_source);
        FixtureReport manual2 = build_manual2_fixture(t, manual_source, manual);
        check_refusals(t, manual_source, snet_source);

        std::string const manifest = make_manifest({manual, snet, manual2});
        std::string const manifest_path = out_path("w29-manifest.json");
        {
            std::ofstream output(manifest_path,
                                 std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot write " + manifest_path);
            }
            output.write(manifest.data(),
                         static_cast<std::streamsize>(manifest.size()));
            if (!output) {
                throw std::runtime_error("failed writing " + manifest_path);
            }
        }
        t.check(read_file(manifest_path) ==
                    std::vector<std::uint8_t>(manifest.begin(), manifest.end()),
                "w29-manifest.json did not save as written");
        t.check(manifest.find("\"file\": \"" + manual.file + "\"") !=
                    std::string::npos,
                "the manifest does not name " + manual.file);
        t.check(manifest.find("\"file\": \"" + snet.file + "\"") !=
                    std::string::npos,
                "the manifest does not name " + snet.file);
        t.check(manifest.find("\"file\": \"" + manual2.file + "\"") !=
                    std::string::npos,
                "the manifest does not name " + manual2.file);
        for (FixtureReport const* fixture : {&manual, &snet, &manual2}) {
            for (WordReport const& word : fixture->words) {
                t.check(manifest.find("\"old\": \"" + hex32(word.word.old_value) +
                                      "\"") != std::string::npos,
                        "the manifest is missing an old value " +
                            hex32(word.word.old_value));
                t.check(manifest.find("\"new\": \"" + hex32(word.word.new_value) +
                                      "\"") != std::string::npos,
                        "the manifest is missing a new value " +
                            hex32(word.word.new_value));
            }
        }

        /* The sources are read-only: nothing in this test writes corpus/. */
        t.check(read_file(manual_source) == manual_before,
                "the manual source fixture was modified");
        t.check(read_file(snet_source) == snet_before,
                "the SNET source fixture was modified");

        std::cout << "\nW29 fixtures in " << MPPWRITE_TEST_OUT << ":\n";
        for (FixtureReport const* fixture : {&manual, &snet, &manual2}) {
            std::cout << "  " << fixture->bytes.size() << " B  "
                      << hex64(fnv1a64(fixture->bytes)) << "  "
                      << fixture->words.size() << " changed words  "
                      << fixture->file << "  -- " << fixture->role << "\n";
        }
        std::cout << "  " << manifest_path << "\n";
    } catch (std::exception const& e) {
        std::cout << "fatal: " << e.what() << "\n";
        return 1;
    }
    return t.report();
}
