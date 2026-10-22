/*
 * roundtrip_authority.cpp -- W26: single-variable fixtures for the "which stored
 * copy does MS Project read?" question (spec/mpp-writer.md §8.1, §10.6).
 *
 * THE QUESTION.  A task row keeps the same logical Start/Finish/Duration in
 * several places:
 *
 *   114/TBkndTask/FixedData   +96/+100   task-frame start/finish
 *                             +104/+108  frame (date-only, UTC) start/finish
 *                             +112/+116  late start / late finish
 *                             +84/+88    duration / remaining duration
 *   114/TBkndTask/Fixed2Data  record row+3: +46/+50 start, +54 finish, +58 duration
 *
 * The stage-4 oracle run wrote every copy and Project did not reproduce the
 * written dates (spec/mpp-writer.md §10.3), so a *consistent* file cannot say
 * which copy is authoritative.  This test emits one file per candidate store,
 * each derived from corpus/probe/base.mpp by changing exactly one copy family,
 * plus one baseline.  The coordinator opens them in Project; the manifest says
 * what each file contains and what a read would look like.
 *
 * WHY RAW BYTE PATCHES AND NOT `write_task_row`.  `write_task_row` deliberately
 * refuses a single-copy change (row_writer.h, spec §10.1): it exists to make
 * inconsistent rows impossible to write through the supported API.  These
 * fixtures exist to *create* inconsistent rows on purpose, one copy at a time,
 * so they are built at the stream level and then proven single-variable by
 * comparing the emitted bytes with the baseline (below).  This is a fixture
 * generator, not a second writer.
 *
 * THE SIX DATE WORDS AND THEIR FIELD NAMES (measured, not inferred).  The
 * coordinator opened corpus/probe/w3-o1.mpp read-only through COM and read all
 * six date fields for every row; they match the six FixedData words exactly,
 * field for field:
 *
 *   +96  Start       +100 Finish
 *   +104 EarlyStart  +108 EarlyFinish
 *   +112 LateStart   +116 LateFinish
 *
 * (uid 3 of w3-o1 is the clincher: stored 2026-10-23 15:00 / 2026-10-26 10:00,
 * 2026-10-23 15:00 / 2026-10-26 10:00, 2026-10-26 13:00 / 2026-10-26 17:00, and
 * Project reported exactly those six values.)  Project's COM date properties
 * are LOCAL WALL CLOCK; pywin32's naive->UTC conversion applies to the input
 * direction only.  So the 13:00/12:00 values in base.mpp are wall-clock times,
 * not a UTC encoding, and there is no timezone translation to apply.
 *
 * BASE.MPP ROW 1 (uid 1), the values this test changes; Project's own save
 * corpus/probe/resaved-base.mpp stores the same copies:
 *
 *   +96  0x3bf212c0  2026-01-05 08:00  Start
 *   +100 0x3bf627d8  2026-01-09 17:00  Finish
 *   +104 0x3bf11e78  2026-01-04 13:00  EarlyStart
 *   +108 0x3bf61c20  2026-01-09 12:00  EarlyFinish
 *   +112 0x3bf11e78  2026-01-04 13:00  LateStart  (uid 1 has zero slack)
 *   +116 0x3bf61c20  2026-01-09 12:00  LateFinish
 *
 * The copies in base.mpp are internally odd -- its EarlyStart (2026-01-04
 * 13:00) is *before* its Start (2026-01-05 08:00), and the project start is
 * 2026-01-04 13:00 -- which is precisely why the authority question cannot be
 * settled by reading base.mpp: it has to be asked of Project.
 *
 * WHAT EACH VARIANT TESTS.  B is the Start/Finish authority test.  C and D
 * change only derived fields (Early*, Late*): if Project recomputes those from
 * Start/Finish on load they will not move, and if they do move that says
 * Project reads them -- a different question from "which store is Start".  E
 * asks whether Fixed2Data is read at all.  F applies base.mpp's own measured
 * deltas (+96 -> +104 is -19 h, +100 -> +108 is -5 h) to the new dates and
 * writes all four derived copies, i.e. the whole Early/Late family shifted
 * consistently.  G isolates the duration store.
 *
 * PRIOR ORACLE RESULT, CORRECTED.  docs/analysis/oracle/oracle-recalc.json
 * reports Project Start 2026-01-04 13:00, Finish 2026-01-27 12:00, and Duration
 * 8160/6240/5760 minutes.  With the measured field mapping that means the file
 * it opened had Start = 2026-01-04 13:00, and the durations are exactly the
 * inclusive working-day counts between Start and Finish (17/13/12 days), so
 * Project recomputed Duration from the dates rather than reading +84.  The
 * fixture on disk today has Start 2026-01-18 13:00 and its mtime is 40 s after
 * the JSON's, so that run used an earlier build of the fixture: it is evidence
 * about Duration/recalculation, not a result for the current file.
 *
 * THE VARIANTS (task row index 1 = uid 1; the intended schedule moves task A
 * forward seven calendar days, Monday 2026-01-12 -> Friday 2026-01-16, so it
 * remains a 5-working-day task and the duration is unchanged):
 *
 *   A  w26-base.mpp    unmodified baseline
 *   B  w26-96.mpp      +96/+100 only            Start/Finish, new wall-clock
 *   C  w26-104.mpp     +104/+108 only           EarlyStart/EarlyFinish, same
 *   D  w26-112.mpp     +112/+116 only           LateStart/LateFinish, same
 *   E  w26-fixed2.mpp  Fixed2 +46/+50/+54 only  same values, verbatim
 *   E1 w26-f2start.mpp Fixed2 +46/+50 only       start words alone
 *   E2 w26-f2finish.mpp Fixed2 +54 only          finish word alone
 *   F  w26-frame.mpp   +104/+108/+112/+116      same values with base.mpp's
 *                                               own -19 h / -5 h deltas
 *   G  w26-dur84.mpp   +84/+88 only             duration 24000 -> 33600 ticks
 *
 * Only B touches Start/Finish.  C, D and F write the same new dates into the
 * derived (Early/Late) stores, so they answer "does Project read or recompute
 * them?", not "is Start authoritative?".  E asks whether Fixed2Data is read at
 * all; E1 and E2 split E's three words so the answer can name the word (the
 * 2026-10-22 oracle run moved Finish in E but left Start at 2026-01-04 13:00
 * even though +46/+50 had changed).  G changes +84/+88 and nothing else, so the
 * dates still describe the old 5-day task and the file's own duration copies
 * disagree with each other.
 *
 * WHAT THE TEST PROVES (no MS Project involved).  For every variant:
 *   - the file is the baseline's size and every stream except the targeted one
 *     is byte-identical (214/ included);
 *   - every byte that differs from the baseline lies inside one of the targeted
 *     4-byte words, and every targeted word actually changed;
 *   - all sixteen row words (+74/+80/+84/+88/+96/+100/+104/+108/+112/+116/
 *     +186/+190 and Fixed2Data +46/+50/+54/+58) read back as expected, and all
 *     rows other than row 1 are unchanged;
 *   - the container self-check and check_table are empty, re-emission is
 *     idempotent, and the saved file reloads to the same bytes/values.
 * The file-level byte diff is the proof of "single variable"; it is computed
 * from the emitted bytes, not asserted from the source of the changes.
 *
 * OUTPUT (build tree, git-ignored): core/mppwrite/build/vs/test-out/w26-*.mpp
 * and w26-manifest.json.  Running the binary again regenerates them.
 */

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <string_view>
#include <utility>
#include <vector>

#include "cfb_writer.h"
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

constexpr std::size_t kRow = 1;      /* task A, fixed-row index (row 0 = project) */
constexpr std::uint32_t kUid = 1u;   /* ... and its UniqueID */

/* base.mpp row 1 (uid 1), measured; resaved-base.mpp (Project's own save) stores
 * the same copies.  Field names from the coordinator's COM read of w3-o1.mpp:
 * all six words matched six reported fields exactly. */
constexpr std::uint32_t kBaseStart = 0x3bf212c0u;       /* +96  Start      2026-01-05 08:00 */
constexpr std::uint32_t kBaseFinish = 0x3bf627d8u;      /* +100 Finish     2026-01-09 17:00 */
constexpr std::uint32_t kBaseEarlyStart = 0x3bf11e78u;  /* +104 EarlyStart 2026-01-04 13:00 */
constexpr std::uint32_t kBaseEarlyFinish = 0x3bf61c20u; /* +108 EarlyFinish 2026-01-09 12:00 */
constexpr std::uint32_t kBaseLateStart = 0x3bf11e78u;   /* +112 LateStart  (== +104: zero slack) */
constexpr std::uint32_t kBaseLateFinish = 0x3bf61c20u;  /* +116 LateFinish (== +108) */
constexpr std::uint32_t kBaseDuration = 24000u;         /* +84  5 working days */
constexpr std::uint32_t kBaseRemaining = 24000u;        /* +88 */
constexpr std::uint32_t kBaseFixed2Start = 0x3bf11e78u; /* Fixed2Data +46/+50 */
constexpr std::uint32_t kBaseFixed2Finish = 0x3bf61c20u;/* Fixed2Data +54 */
constexpr std::uint32_t kBaseFixed2Duration = 24000u;   /* Fixed2Data +58 */

/* The one intended new schedule: task A moves +7 calendar days, so it is still
 * Monday 08:00 -> Friday 17:00, a 5-working-day (24,000 tick) task. */
constexpr std::uint32_t kNewStart = 0x3bf912c0u;       /* 2026-01-12 08:00 */
constexpr std::uint32_t kNewFinish = 0x3bfd27d8u;      /* 2026-01-16 17:00 */
/* The same two dates with base.mpp's own measured deltas applied: +96 -> +104
 * is -19 h and +100 -> +108 is -5 h.  These are the EarlyStart/EarlyFinish and
 * LateStart/LateFinish copies of the intended schedule; the delta is a property
 * of this file, not a timezone rule. */
constexpr std::uint32_t kNewShiftedStart = 0x3bf81e78u;  /* 2026-01-11 13:00 */
constexpr std::uint32_t kNewShiftedFinish = 0x3bfd1c20u; /* 2026-01-16 12:00 */
constexpr std::uint32_t kLongDuration = 33600u;          /* 7 working days */

/* Every row word this test watches.  +74/+186/+190 are slack; they are included
 * only so a misplaced write is caught here rather than in Project. */
constexpr std::size_t kFixedWords[] = {74,  80,  84,  88,  96,
                                       100, 104, 108, 112, 116,
                                       186, 190};
constexpr std::size_t kFixed2Words[] = {46, 50, 54, 58};

constexpr std::size_t kWordsPerRecord = 4; /* a change is one 4-byte word */

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

/* Only 214/ is checked here; every other stream is covered by the changed-stream
 * assertion, which is stronger (it names every stream that moved). */
void check_214_untouched(TestContext& t, mppwrite::CfbDocument const& before,
                         mppwrite::CfbDocument const& after, std::string const& label) {
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

/* --- the variants -------------------------------------------------------- */

enum class WhichStream { Fixed, Fixed2 };

struct WordChange {
    WhichStream stream = WhichStream::Fixed;
    std::size_t row_offset = 0;   /* inside the 202-byte row / 64-byte record */
    std::uint32_t old_value = 0;
    std::uint32_t new_value = 0;
    bool is_date = true;
    std::string candidate_field; /* hypothesis, printed in the manifest */
};

WordChange change(WhichStream stream, std::size_t row_offset,
                  std::uint32_t old_value, std::uint32_t new_value, bool is_date,
                  std::string candidate_field) {
    WordChange out;
    out.stream = stream;
    out.row_offset = row_offset;
    out.old_value = old_value;
    out.new_value = new_value;
    out.is_date = is_date;
    out.candidate_field = std::move(candidate_field);
    return out;
}

struct Variant {
    std::string file;
    std::string role;
    std::string note;
    std::vector<WordChange> changes;
};

std::vector<Variant> make_variants() {
    std::vector<Variant> out;

    Variant a;
    a.file = "w26-base.mpp";
    a.role = "A: baseline (unmodified base.mpp)";
    a.note = "Reference file: no byte differs from corpus/probe/base.mpp.";
    out.push_back(std::move(a));

    Variant b;
    b.file = "w26-96.mpp";
    b.role = "B: FixedData +96/+100 only (Start/Finish) -- the authority test";
    b.note =
        "The intended new Start/Finish written verbatim, wall clock, and "
        "nothing else. If Project takes the task's Start/Finish from the file, "
        "it should report Start 2026-01-12 08:00 and Finish 2026-01-16 17:00; "
        "if it reschedules the task from the project start instead, the stored "
        "values are irrelevant and it will report the old schedule. The "
        "Early/Late copies still say 2026-01-04 13:00 / 2026-01-09 12:00.";
    b.changes = {
        change(WhichStream::Fixed, 96, kBaseStart, kNewStart, true, "Start"),
        change(WhichStream::Fixed, 100, kBaseFinish, kNewFinish, true, "Finish"),
    };
    out.push_back(std::move(b));

    Variant c;
    c.file = "w26-104.mpp";
    c.role = "C: FixedData +104/+108 only (EarlyStart/EarlyFinish)";
    c.note =
        "The intended new dates written verbatim into the Early pair, with "
        "Start/Finish (+96/+100) left at baseline. EarlyStart/EarlyFinish are "
        "derived fields, so Project may recompute them from the schedule on "
        "load; if they move, Project read this store. No Start/Finish change "
        "should be inferred from this file.";
    c.changes = {
        change(WhichStream::Fixed, 104, kBaseEarlyStart, kNewStart, true,
               "EarlyStart"),
        change(WhichStream::Fixed, 108, kBaseEarlyFinish, kNewFinish, true,
               "EarlyFinish"),
    };
    out.push_back(std::move(c));

    Variant d;
    d.file = "w26-112.mpp";
    d.role = "D: FixedData +112/+116 only (LateStart/LateFinish)";
    d.note =
        "The intended new dates written verbatim into the Late pair, with "
        "Start/Finish and Early* left at baseline. Like C this tests whether "
        "Project reads or recomputes a derived field; it does not test Start "
        "authority.";
    d.changes = {
        change(WhichStream::Fixed, 112, kBaseLateStart, kNewStart, true,
               "LateStart"),
        change(WhichStream::Fixed, 116, kBaseLateFinish, kNewFinish, true,
               "LateFinish"),
    };
    out.push_back(std::move(d));

    Variant e;
    e.file = "w26-fixed2.mpp";
    e.role = "E: Fixed2Data +46/+50/+54 only (start/finish copies)";
    e.note =
        "The intended new dates written verbatim into Fixed2Data record 4 "
        "(row 1). +58 (duration) is unchanged at 24000 because the intended "
        "schedule is still 5 working days. Fixed2Data holds no field Project "
        "reports directly, so this file answers 'is Fixed2Data read at all, "
        "and as what?'";
    e.changes = {
        change(WhichStream::Fixed2, 46, kBaseFixed2Start, kNewStart, true,
               "Fixed2Data start copy"),
        change(WhichStream::Fixed2, 50, kBaseFixed2Start, kNewStart, true,
               "Fixed2Data start copy"),
        change(WhichStream::Fixed2, 54, kBaseFixed2Finish, kNewFinish, true,
               "Fixed2Data finish copy"),
    };
    out.push_back(std::move(e));

    /* E1/E2 split E's three-word change: they separate "Fixed2Data is read" from
     * "which of its words is read".  The 2026-10-22 oracle run moved Finish in E
     * but left Start at 2026-01-04 13:00 even though +46/+50 had changed. */
    Variant e1;
    e1.file = "w26-f2start.mpp";
    e1.role = "E1: Fixed2Data +46/+50 only (start copies; +54 untouched)";
    e1.note =
        "Fixed2Data start copies only, set to 2026-01-12 08:00; +54 stays at "
        "2026-01-09 12:00. If Project reads +46/+50, uid 1 Start should move "
        "while Finish stays; if Start stays at 2026-01-04 13:00, the start "
        "words are not the Start store (E already changed them together with "
        "+54 and Start did not move).";
    e1.changes = {
        change(WhichStream::Fixed2, 46, kBaseFixed2Start, kNewStart, true,
               "Fixed2Data start copy"),
        change(WhichStream::Fixed2, 50, kBaseFixed2Start, kNewStart, true,
               "Fixed2Data start copy"),
    };
    out.push_back(std::move(e1));

    Variant e2;
    e2.file = "w26-f2finish.mpp";
    e2.role = "E2: Fixed2Data +54 only (finish copy; +46/+50 untouched)";
    e2.note =
        "Fixed2Data finish copy only, set to 2026-01-16 17:00; +46/+50 stay at "
        "2026-01-04 13:00. This isolates the word that moved Finish in E: if "
        "Finish moves to 2026-01-16 17:00 again (Duration and project finish "
        "with it), +54 alone is sufficient and +46/+50 are not needed for the "
        "finish to take effect.";
    e2.changes = {
        change(WhichStream::Fixed2, 54, kBaseFixed2Finish, kNewFinish, true,
               "Fixed2Data finish copy"),
    };
    out.push_back(std::move(e2));

    Variant f;
    f.file = "w26-frame.mpp";
    f.role = "F: FixedData +104..+116, all Early/Late copies shifted by "
             "base.mpp's own deltas";
    f.note =
        "The brief's 'UTC-translated' variant, renamed w26-frame.mpp: the "
        "coordinator's COM read of w3-o1 shows there is no UTC frame, only "
        "wall-clock Start/Finish plus derived Early/Late copies. So this file "
        "applies base.mpp's own measured deltas to the new dates: +96 -> +104 "
        "is -19 h (2026-01-11 13:00) and +100 -> +108 is -5 h (2026-01-16 "
        "12:00), for both the Early pair (+104/+108) and the Late pair "
        "(+112/+116). Start/Finish stay at baseline. If Project reads any "
        "derived copy, this file carries the whole derived family consistently "
        "for the +7-day schedule.";
    f.changes = {
        change(WhichStream::Fixed, 104, kBaseEarlyStart, kNewShiftedStart, true,
               "EarlyStart"),
        change(WhichStream::Fixed, 108, kBaseEarlyFinish, kNewShiftedFinish,
               true, "EarlyFinish"),
        change(WhichStream::Fixed, 112, kBaseLateStart, kNewShiftedStart, true,
               "LateStart"),
        change(WhichStream::Fixed, 116, kBaseLateFinish, kNewShiftedFinish, true,
               "LateFinish"),
    };
    out.push_back(std::move(f));

    Variant g;
    g.file = "w26-dur84.mpp";
    g.role = "G: FixedData +84/+88 only (duration / remaining duration)";
    g.note =
        "Duration 24,000 -> 33,600 ticks (5 -> 7 working days) and remaining "
        "duration with it; every date copy, Fixed2Data +58 and slack are left "
        "at baseline, so the row now says 7 days but still spans 2026-01-05 to "
        "2026-01-09. If Project reports 7 days, the fixed-row duration is "
        "authoritative; if it reports 5 days (or recomputes from the dates), "
        "it is not.";
    g.changes = {
        change(WhichStream::Fixed, 84, kBaseDuration, kLongDuration, false,
               "Duration"),
        change(WhichStream::Fixed, 88, kBaseRemaining, kLongDuration, false,
               "RemainingDuration"),
    };
    out.push_back(std::move(g));

    return out;
}

/* --- building and checking one variant ----------------------------------- */

struct ChangeReport {
    WordChange change;
    std::size_t stream_offset = 0;
    std::uint64_t file_offset = 0;
    std::vector<std::uint64_t> changed_file_bytes;
};

struct VariantResult {
    std::vector<std::uint8_t> bytes;
    std::vector<ChangeReport> reports;
};

std::size_t record_offset(WhichStream stream) {
    return stream == WhichStream::Fixed ? mppwrite::task_row_offset(kRow)
                                        : mppwrite::fixed2_record_offset(kRow);
}

std::string stream_name(WhichStream stream) {
    return stream == WhichStream::Fixed ? kFixed : kFixed2;
}

mppwrite::CfbDocument build_variant(mppwrite::CfbDocument const& base,
                                    Variant const& variant) {
    mppwrite::CfbDocument doc =
        mppwrite::CfbDocument::from_bytes(base.source_bytes());
    std::vector<std::uint8_t> fixed = doc.read_stream(kFixed);
    std::vector<std::uint8_t> fixed2 = doc.read_stream(kFixed2);
    bool touch_fixed = false;
    bool touch_fixed2 = false;
    for (WordChange const& c : variant.changes) {
        std::vector<std::uint8_t>& target =
            c.stream == WhichStream::Fixed ? fixed : fixed2;
        std::size_t const p = record_offset(c.stream) + c.row_offset;
        std::uint32_t const current = u32_at(target, p);
        if (current != c.old_value) {
            throw mppwrite::Error(variant.file + ": " + stream_name(c.stream) +
                                  " row offset " + std::to_string(c.row_offset) +
                                  " holds " + hex32(current) + ", expected " +
                                  hex32(c.old_value));
        }
        put_u32(target, p, c.new_value);
        if (c.stream == WhichStream::Fixed) {
            touch_fixed = true;
        } else {
            touch_fixed2 = true;
        }
    }
    if (touch_fixed) {
        doc.replace_stream(kFixed, fixed);
    }
    if (touch_fixed2) {
        doc.replace_stream(kFixed2, fixed2);
    }
    return doc;
}

/* Every 4-byte word the variant is allowed to change. */
std::vector<std::pair<std::string, std::size_t>> allowed_words(
    Variant const& variant) {
    std::vector<std::pair<std::string, std::size_t>> out;
    for (WordChange const& c : variant.changes) {
        out.emplace_back(stream_name(c.stream),
                         record_offset(c.stream) + c.row_offset);
    }
    return out;
}

bool is_allowed_byte(std::vector<std::pair<std::string, std::size_t>> const& words,
                     std::string const& stream, std::size_t stream_offset) {
    for (std::pair<std::string, std::size_t> const& word : words) {
        if (word.first == stream && stream_offset >= word.second &&
            stream_offset < word.second + kWordsPerRecord) {
            return true;
        }
    }
    return false;
}

/* Every differing byte of one stream must sit inside an allowed word.  Returns
 * the number of bytes that differ outside the allowed words (0 = clean). */
std::size_t stray_stream_bytes(
    std::vector<std::uint8_t> const& before, std::vector<std::uint8_t> const& after,
    std::vector<std::pair<std::string, std::size_t>> const& words,
    std::string const& stream) {
    if (before.size() != after.size()) {
        return std::max(before.size(), after.size());
    }
    std::size_t stray = 0;
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i] != after[i] && !is_allowed_byte(words, stream, i)) {
            ++stray;
        }
    }
    return stray;
}

void check_emitted(TestContext& t, mppwrite::CfbDocument const& doc,
                   std::string const& label) {
    t.check(doc.check().empty(), label + ": container check: " + doc.check());
    std::vector<std::uint8_t> const bytes = doc.to_bytes();
    mppwrite::CfbDocument const again = mppwrite::CfbDocument::from_bytes(bytes);
    t.check(again.to_bytes() == bytes, label + ": re-emission is not idempotent");
    t.check(mppwrite::check_table(doc).empty(),
            label + ": table check: " + mppwrite::check_table(doc));
}

void check_word_table(TestContext& t, mppwrite::CfbDocument const& base,
                      mppwrite::CfbDocument const& doc, Variant const& variant,
                      std::string const& label) {
    std::vector<std::uint8_t> const base_fixed = base.read_stream(kFixed);
    std::vector<std::uint8_t> const base_fixed2 = base.read_stream(kFixed2);
    std::vector<std::uint8_t> const fixed = doc.read_stream(kFixed);
    std::vector<std::uint8_t> const fixed2 = doc.read_stream(kFixed2);
    std::size_t const fp = mppwrite::task_row_offset(kRow);
    std::size_t const f2p = mppwrite::fixed2_record_offset(kRow);

    auto expected = [&variant](WhichStream stream, std::size_t row_offset,
                               std::uint32_t base_value) {
        for (WordChange const& c : variant.changes) {
            if (c.stream == stream && c.row_offset == row_offset) {
                return c.new_value;
            }
        }
        return base_value;
    };

    for (std::size_t const offset : kFixedWords) {
        std::uint32_t const base_value = u32_at(base_fixed, fp + offset);
        std::uint32_t const want = expected(WhichStream::Fixed, offset, base_value);
        t.check(u32_at(fixed, fp + offset) == want,
                label + ": FixedData +" + std::to_string(offset) + " is " +
                    hex32(u32_at(fixed, fp + offset)) + ", expected " +
                    hex32(want));
    }
    for (std::size_t const offset : kFixed2Words) {
        std::uint32_t const base_value = u32_at(base_fixed2, f2p + offset);
        std::uint32_t const want = expected(WhichStream::Fixed2, offset, base_value);
        t.check(u32_at(fixed2, f2p + offset) == want,
                label + ": Fixed2Data +" + std::to_string(offset) + " is " +
                    hex32(u32_at(fixed2, f2p + offset)) + ", expected " +
                    hex32(want));
    }
}

/* The TaskValues a correct build must read back. */
mppwrite::TaskValues expected_values(mppwrite::CfbDocument const& base,
                                     Variant const& variant) {
    mppwrite::TaskValues want = mppwrite::read_task_values(base, kRow);
    for (WordChange const& c : variant.changes) {
        if (c.stream == WhichStream::Fixed) {
            switch (c.row_offset) {
                case 84: want.duration = c.new_value; break;
                case 88: want.remaining_duration = c.new_value; break;
                case 96: want.start = c.new_value; break;
                case 100: want.finish = c.new_value; break;
                case 104: want.project_start = c.new_value; break;
                case 108: want.project_finish = c.new_value; break;
                case 112: want.late_start = c.new_value; break;
                case 116: want.late_finish = c.new_value; break;
                default: break;
            }
        } else {
            switch (c.row_offset) {
                case 46:
                case 50: want.fixed2_start = c.new_value; break;
                case 54: want.fixed2_finish = c.new_value; break;
                case 58: want.fixed2_duration = c.new_value; break;
                default: break;
            }
        }
    }
    return want;
}

VariantResult check_variant(TestContext& t, mppwrite::CfbDocument const& base,
                            std::vector<std::uint8_t> const& base_bytes,
                            Variant const& variant) {
    std::string const label = variant.file;
    mppwrite::CfbDocument const doc = build_variant(base, variant);
    /* Determinism: two independent builds must be identical. */
    mppwrite::CfbDocument const again = build_variant(base, variant);
    t.check(doc.to_bytes() == again.to_bytes(),
            label + ": two builds of the same variant differ");

    VariantResult result;
    result.bytes = doc.to_bytes();

    /* --- the file-level single-variable proof --------------------------- */
    t.check(result.bytes.size() == base_bytes.size(),
            label + ": file size " + std::to_string(result.bytes.size()) +
                " != baseline " + std::to_string(base_bytes.size()));
    std::vector<std::uint64_t> const differing =
        [&result, &base_bytes]() {
            std::vector<std::uint64_t> offsets;
            if (result.bytes.size() != base_bytes.size()) {
                return offsets;
            }
            for (std::size_t i = 0; i < base_bytes.size(); ++i) {
                if (base_bytes[i] != result.bytes[i]) {
                    offsets.push_back(static_cast<std::uint64_t>(i));
                }
            }
            return offsets;
        }();
    std::vector<std::pair<std::string, std::size_t>> const words =
        allowed_words(variant);
    std::size_t const allowed_file_bytes = words.size() * kWordsPerRecord;
    t.check(differing.size() <= allowed_file_bytes,
            label + ": " + std::to_string(differing.size()) +
                " file bytes differ, more than the " +
                std::to_string(allowed_file_bytes) + " targeted bytes allow");
    std::size_t stray = 0;
    for (std::uint64_t const offset : differing) {
        bool allowed = false;
        for (std::pair<std::string, std::size_t> const& word : words) {
            std::uint64_t const first =
                file_offset_for_stream_byte(base, word.first, word.second);
            if (offset >= first && offset < first + kWordsPerRecord) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            ++stray;
        }
    }
    t.check(stray == 0, label + ": " + std::to_string(stray) +
                            " differing bytes fall outside the targeted words");
    t.check(stray_stream_bytes(base.read_stream(kFixed), doc.read_stream(kFixed),
                               words, kFixed) == 0,
            label + ": a non-targeted byte changed in FixedData");
    t.check(stray_stream_bytes(base.read_stream(kFixed2), doc.read_stream(kFixed2),
                               words, kFixed2) == 0,
            label + ": a non-targeted byte changed in Fixed2Data");

    /* Every targeted word must actually have changed. */
    for (WordChange const& c : variant.changes) {
        t.check(c.old_value != c.new_value,
                label + ": variant change has old == new at +" +
                    std::to_string(c.row_offset));
        std::size_t const p = record_offset(c.stream) + c.row_offset;
        std::vector<std::uint8_t> const before =
            c.stream == WhichStream::Fixed ? base.read_stream(kFixed)
                                           : base.read_stream(kFixed2);
        std::vector<std::uint8_t> const after =
            c.stream == WhichStream::Fixed ? doc.read_stream(kFixed)
                                           : doc.read_stream(kFixed2);
        std::size_t changed = 0;
        for (std::size_t i = 0; i < kWordsPerRecord; ++i) {
            if (before[p + i] != after[p + i]) {
                ++changed;
            }
        }
        t.check(changed > 0, label + ": " + stream_name(c.stream) + " +" +
                                 std::to_string(c.row_offset) + " did not change");
        ChangeReport report;
        report.change = c;
        report.stream_offset = p;
        report.file_offset = file_offset_for_stream_byte(base, stream_name(c.stream), p);
        for (std::size_t i = 0; i < kWordsPerRecord; ++i) {
            if (before[p + i] != after[p + i]) {
                report.changed_file_bytes.push_back(
                    file_offset_for_stream_byte(base, stream_name(c.stream), p + i));
            }
        }
        result.reports.push_back(std::move(report));
    }

    /* Same size means replace_stream reuses the sectors: the mapping from a
     * stream byte to a file byte must not have moved. */
    t.check(doc.stream_file_ranges(kFixed) == base.stream_file_ranges(kFixed),
            label + ": FixedData moved in the file");
    t.check(doc.stream_file_ranges(kFixed2) == base.stream_file_ranges(kFixed2),
            label + ": Fixed2Data moved in the file");

    /* --- nothing else in the container changed -------------------------- */
    std::vector<std::string> const changed = changed_streams(base, doc);
    std::vector<std::string> expected_streams;
    for (WordChange const& c : variant.changes) {
        std::string const name = stream_name(c.stream);
        if (std::find(expected_streams.begin(), expected_streams.end(), name) ==
            expected_streams.end()) {
            expected_streams.push_back(name);
        }
    }
    t.check(changed == expected_streams,
            label + ": changed streams are [" + join_list(changed) +
                "], expected [" + join_list(expected_streams) + "]");
    t.check(doc.stream_paths() == base.stream_paths(),
            label + ": the stream set changed");
    check_214_untouched(t, base, doc, label);
    check_emitted(t, doc, label);

    /* --- the row reads back as intended -------------------------------- */
    mppwrite::TaskValues const want = expected_values(base, variant);
    mppwrite::TaskValues const got = mppwrite::read_task_values(doc, kRow);
    t.check(got == want, label + ": row " + std::to_string(kRow) +
                             " does not read back as intended");
    t.check(mppwrite::TaskTable::from_document(doc).uid(kRow) == kUid,
            label + ": row " + std::to_string(kRow) + " is not uid " +
                std::to_string(kUid));
    for (std::size_t row = 0; row < mppwrite::TaskTable::from_document(base).rows();
         ++row) {
        if (row == kRow) {
            continue;
        }
        t.check(mppwrite::read_task_values(doc, row) ==
                    mppwrite::read_task_values(base, row),
                label + ": row " + std::to_string(row) +
                    " changed when row " + std::to_string(kRow) + " was written");
    }
    check_word_table(t, base, doc, variant, label);

    /* --- save, reload, compare ----------------------------------------- */
    std::string const out = out_path(variant.file);
    doc.save(out);
    std::vector<std::uint8_t> const saved = read_file(out);
    t.check(saved == result.bytes, label + ": saved file differs from to_bytes()");
    mppwrite::CfbDocument const reloaded = mppwrite::CfbDocument::from_file(out);
    t.check(reloaded.check().empty(),
            label + ": reloaded container check: " + reloaded.check());
    t.check(mppwrite::check_table(reloaded).empty(),
            label + ": reloaded table check: " + mppwrite::check_table(reloaded));
    t.check(mppwrite::read_task_values(reloaded, kRow) == want,
            label + ": reloaded row does not match");
    return result;
}

/* --- manifest ------------------------------------------------------------ */

std::string make_manifest(std::vector<Variant> const& variants,
                          std::vector<VariantResult> const& results,
                          std::size_t source_bytes) {
    std::ostringstream out;
    out << "{\n"
        << "  \"stream\": \"W26\",\n"
        << "  \"generated_by\": \"mppwrite_roundtrip_authority "
           "(tests/roundtrip_authority.cpp)\",\n"
        << "  \"source\": \"corpus/probe/base.mpp\",\n"
        << "  \"source_bytes\": " << source_bytes << ",\n"
        << "  \"task_row_index\": " << kRow << ",\n"
        << "  \"task_unique_id\": " << kUid << ",\n"
        << "  \"oracle_question\": \"Which stored copy of a task row's "
           "Start/Finish does MS Project read on load, and does it read or "
           "recompute the Early/Late/Duration copies?\",\n"
        << "  \"oracle_should_report\": [\"Project.ProjectStart\", "
           "\"Project.ProjectFinish\", \"Task.Start\", \"Task.Finish\", "
           "\"Task.EarlyStart\", \"Task.EarlyFinish\", \"Task.LateStart\", "
           "\"Task.LateFinish\", \"Task.Duration\", \"Task.DurationText\", "
           "\"Task.StartSlack\", \"Task.TotalSlack\", \"Task.Critical\"],\n"
        << "  \"oracle_note_time\": \"Project's COM date properties are local "
           "wall clock (measured on w3-o1.mpp); pywin32's naive->UTC conversion "
           "applies to the input direction only. Compare reported strings "
           "directly with the values below, no timezone conversion.\",\n"
        << "  \"field_names\": {\n"
        << "    \"measured_on\": \"corpus/probe/w3-o1.mpp via COM, read-only; "
           "all six words matched six reported fields exactly\",\n"
        << "    \"+96\": \"Start\", \"+100\": \"Finish\", "
           "\"+104\": \"EarlyStart\", \"+108\": \"EarlyFinish\", "
           "\"+112\": \"LateStart\", \"+116\": \"LateFinish\",\n"
        << "    \"note\": \"base.mpp's own copies are internally odd: its "
           "EarlyStart 2026-01-04 13:00 precedes its Start 2026-01-05 08:00. "
           "That is why the authority question needs Project.\"\n"
        << "  },\n"
        << "  \"baseline_row\": {\"Start\": \"2026-01-05 08:00\", "
           "\"Finish\": \"2026-01-09 17:00\", \"EarlyStart\": "
           "\"2026-01-04 13:00\", \"EarlyFinish\": \"2026-01-09 12:00\", "
           "\"LateStart\": \"2026-01-04 13:00\", \"LateFinish\": "
           "\"2026-01-09 12:00\", \"Duration_minutes\": 2400},\n"
        << "  \"intended_schedule\": {\"Start\": \"2026-01-12 08:00\", "
           "\"Finish\": \"2026-01-16 17:00\", \"start_day\": "
           "\"2026-01-12 (Monday)\", \"finish_day\": \"2026-01-16 "
           "(Friday)\", \"duration_ticks\": 24000, \"duration_minutes\": "
           "2400, \"duration_days\": 5},\n"
        << "  \"derived_deltas\": {\"start_to_early\": \"-19 h (2026-01-12 "
           "08:00 -> 2026-01-11 13:00)\", \"finish_to_early\": \"-5 h "
           "(2026-01-16 17:00 -> 2026-01-16 12:00)\", \"note\": \"measured "
           "on base.mpp row 1; a property of this file, not a timezone rule\"},\n"
        << "  \"what_each_variant_tests\": {\n"
        << "    \"B\": \"whether Project takes Start/Finish from +96/+100 or "
           "reschedules the task\",\n"
        << "    \"C\": \"whether Project reads or recomputes "
           "EarlyStart/EarlyFinish\",\n"
        << "    \"D\": \"whether Project reads or recomputes "
           "LateStart/LateFinish\",\n"
        << "    \"E\": \"whether Fixed2Data is read at all (all three "
           "words)\",\n"
        << "    \"E1\": \"whether the Fixed2Data start words (+46/+50) are "
           "read; +54 untouched\",\n"
        << "    \"E2\": \"whether the Fixed2Data finish word (+54) is read; "
           "+46/+50 untouched\",\n"
        << "    \"F\": \"the whole Early/Late family shifted by base.mpp's "
           "own deltas (the brief's 'UTC-translated' variant, renamed)\",\n"
        << "    \"G\": \"whether Duration comes from +84/+88 or is recomputed "
           "from the dates\"\n"
        << "  },\n"
        << "  \"prior_observation\": \"docs/analysis/oracle/oracle-recalc.json "
           "(mtime 40 s before the fixture it names) reports Start "
           "2026-01-04 13:00, Finish 2026-01-27 12:00 and Duration "
           "8160/6240/5760 minutes. That is consistent with Start/Finish "
           "being read from +96/+100 and Duration being recomputed from them "
           "as an inclusive working-day count (17/13/12 days); it is not the "
           "stored +84. The run used an earlier build of oracle-recalc.mpp, so "
           "it is evidence about the recalc, not a result for the current "
           "fixture.\",\n"
        << "  \"oracle_result_2026_10_22\": {\n"
        << "    \"run\": \"docs/analysis/oracle/w26-open.json (coordinator, "
           "read-only open; Project's own load-time recalculation)\",\n"
        << "    \"uid1\": \"Manual=True, ConstraintType=0 (ASAP)\",\n"
        << "    \"identical_to_base\": [\"w26-96.mpp\", \"w26-104.mpp\", "
           "\"w26-112.mpp\", \"w26-frame.mpp\", \"w26-dur84.mpp\"],\n"
        << "    \"w26-fixed2\": \"Start 2026-01-04 13:00 unchanged, Finish "
           "2026-01-09 12:00 -> 2026-01-16 17:00, Duration 2400 -> 5040 min, "
           "project finish -> 2026-01-16 17:00, B/C TotalSlack 960 -> 3600 and "
           "1440 -> 4080\",\n"
        << "    \"open_anomaly\": \"+46/+50 changed to 2026-01-12 08:00 in "
           "w26-fixed2 but Start stayed 2026-01-04 13:00; w26-f2start and "
           "w26-f2finish separate read from ignored.\",\n"
        << "    \"raw_field_note\": \"GetField(Duration) exposes the stored "
           "+84 word while the model recomputes: w26-dur84 raw '7 days' vs "
           "model 5 days; w26-fixed2 raw '5 days' vs model 10.5 days.\"\n"
        << "  },\n"
        << "  \"variants\": [\n";
    for (std::size_t v = 0; v < variants.size(); ++v) {
        Variant const& variant = variants[v];
        VariantResult const& result = results[v];
        out << "    {\n"
            << "      \"file\": \"" << json_escape(variant.file) << "\",\n"
            << "      \"role\": \"" << json_escape(variant.role) << "\",\n"
            << "      \"bytes\": " << result.bytes.size() << ",\n"
            << "      \"fnv1a64\": \"" << hex64(fnv1a64(result.bytes)) << "\",\n"
            << "      \"note\": \"" << json_escape(variant.note) << "\",\n"
            << "      \"changes\": [";
        if (result.reports.empty()) {
            out << "]\n";
        } else {
            out << "\n";
            for (std::size_t c = 0; c < result.reports.size(); ++c) {
                ChangeReport const& report = result.reports[c];
                WordChange const& word = report.change;
                out << "        {\"stream\": \""
                    << json_escape(stream_name(word.stream))
                    << "\", \"row_offset\": " << word.row_offset
                    << ", \"stream_offset\": " << report.stream_offset
                    << ", \"file_offset\": " << report.file_offset
                    << ", \"candidate_field\": \""
                    << json_escape(word.candidate_field) << "\", \"old\": \""
                    << hex32(word.old_value) << "\", \"new\": \""
                    << hex32(word.new_value) << "\"";
                if (word.is_date) {
                    out << ", \"old_decodes\": \""
                        << mppwrite::format_date(word.old_value)
                        << "\", \"new_decodes\": \""
                        << mppwrite::format_date(word.new_value) << "\"";
                } else {
                    out << ", \"old_ticks\": " << word.old_value
                        << ", \"new_ticks\": " << word.new_value;
                }
                out << ", \"changed_file_bytes\": [";
                for (std::size_t i = 0; i < report.changed_file_bytes.size(); ++i) {
                    if (i != 0) {
                        out << ", ";
                    }
                    out << report.changed_file_bytes[i];
                }
                out << "]}";
                if (c + 1 != result.reports.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "      ]\n";
        }
        out << "    }";
        if (v + 1 != variants.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

std::size_t count_occurrences(std::string const& text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

} /* namespace */

int main() {
    TestContext t;
    try {
        std::string const source = join(MPPWRITE_PROBE_DIR, "base.mpp");
        std::error_code error;
        if (!std::filesystem::exists(source, error)) {
            std::cout << "SKIP: corpus not present (" << source
                      << " is missing; corpus/ is git-ignored)\n";
            return 0;
        }
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);

        mppwrite::CfbDocument const base = mppwrite::CfbDocument::from_file(source);
        std::vector<std::uint8_t> const base_bytes = read_file(source);
        t.check(base.check().empty(),
                "base.mpp: container check: " + base.check());
        t.check(base.to_bytes() == base_bytes,
                "base.mpp: the parsed document does not re-emit byte-identically");

        /* ---- anchors: the measured copies this test is built on ---------- */
        {
            mppwrite::TaskValues const a = mppwrite::read_task_values(base, kRow);
            t.check(mppwrite::TaskTable::from_document(base).uid(kRow) == kUid,
                    "row 1 is not uid 1");
            t.check(a.start == kBaseStart && a.finish == kBaseFinish,
                    "row 1 Start/Finish are not " + hex32(kBaseStart) + "/" +
                        hex32(kBaseFinish));
            t.check(a.project_start == kBaseEarlyStart &&
                        a.project_finish == kBaseEarlyFinish,
                    "row 1 EarlyStart/EarlyFinish are not " +
                        hex32(kBaseEarlyStart) + "/" + hex32(kBaseEarlyFinish));
            t.check(a.late_start == kBaseLateStart &&
                        a.late_finish == kBaseLateFinish,
                    "row 1 late dates are not " + hex32(kBaseLateStart) + "/" +
                        hex32(kBaseLateFinish));
            t.check(a.duration == kBaseDuration &&
                        a.remaining_duration == kBaseRemaining,
                    "row 1 duration is not 24000/24000");
            t.check(a.fixed2_start == kBaseFixed2Start &&
                        a.fixed2_finish == kBaseFixed2Finish &&
                        a.fixed2_duration == kBaseFixed2Duration,
                    "row 1 Fixed2Data copies are not the baseline values");
            t.check(mppwrite::format_date(kBaseStart) == "2026-01-05 08:00" &&
                        mppwrite::format_date(kBaseFinish) == "2026-01-09 17:00",
                    "baseline task-frame dates do not decode as expected");
            t.check(mppwrite::format_date(kNewStart) == "2026-01-12 08:00" &&
                        mppwrite::format_date(kNewFinish) == "2026-01-16 17:00",
                    "new task-frame dates do not decode as expected");
            t.check(mppwrite::format_date(kNewShiftedStart) ==
                            "2026-01-11 13:00" &&
                        mppwrite::format_date(kNewShiftedFinish) ==
                            "2026-01-16 12:00",
                    "new derived-copy dates do not decode as expected");
            t.check(kNewStart - kBaseStart == (7u << 16),
                    "the new start is not exactly the baseline start + 7 days");
            t.check(kNewFinish - kBaseFinish == (7u << 16),
                    "the new finish is not exactly the baseline finish + 7 days");
            t.check(kNewShiftedStart - kBaseEarlyStart == (7u << 16) &&
                        kNewShiftedFinish - kBaseEarlyFinish == (7u << 16),
                    "the new derived copies are not exactly +7 days");
        }

        /* Project's own save of base.mpp keeps the same copies, so the values
         * above survive a Project round trip.  Skipped when absent. */
        {
            std::string const resaved = join(MPPWRITE_PROBE_DIR, "resaved-base.mpp");
            if (std::filesystem::exists(resaved, error)) {
                mppwrite::CfbDocument const doc =
                    mppwrite::CfbDocument::from_file(resaved);
                t.check(mppwrite::read_task_values(doc, kRow) ==
                            mppwrite::read_task_values(base, kRow),
                        "resaved-base.mpp row 1 differs from base.mpp row 1");
            }
        }

        std::vector<Variant> const variants = make_variants();
        std::vector<VariantResult> results;
        results.reserve(variants.size());
        for (Variant const& variant : variants) {
            results.push_back(check_variant(t, base, base_bytes, variant));
        }

        /* The baseline variant must be byte-identical to the source. */
        t.check(results.front().bytes == base_bytes,
                "w26-base.mpp differs from corpus/probe/base.mpp");

        /* ---- the manifest is oracle input; make sure it says what it must - */
        std::string const manifest =
            make_manifest(variants, results, base_bytes.size());
        std::string const manifest_path = out_path("w26-manifest.json");
        {
            std::ofstream output(manifest_path, std::ios::binary | std::ios::trunc);
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
                "w26-manifest.json did not save as written");
        t.check(count_occurrences(manifest, "\"file\": \"") ==
                    static_cast<std::size_t>(variants.size()),
                "the manifest does not list every variant");
        for (Variant const& variant : variants) {
            t.check(manifest.find("\"file\": \"" + variant.file + "\"") !=
                        std::string::npos,
                    "the manifest does not name " + variant.file);
        }
        for (VariantResult const& result : results) {
            for (ChangeReport const& report : result.reports) {
                t.check(manifest.find("\"old\": \"" +
                                      hex32(report.change.old_value) + "\"") !=
                            std::string::npos,
                        "the manifest is missing an old value " +
                            hex32(report.change.old_value));
                t.check(manifest.find("\"new\": \"" +
                                      hex32(report.change.new_value) + "\"") !=
                            std::string::npos,
                        "the manifest is missing a new value " +
                            hex32(report.change.new_value));
            }
        }

        /* Keep the source fixture untouched: this test never writes corpus/. */
        t.check(read_file(source) == base_bytes,
                "the source fixture was modified");

        std::cout << "\nW26 fixtures in " << MPPWRITE_TEST_OUT << ":\n";
        for (VariantResult const& result : results) {
            std::cout << "  " << result.bytes.size() << " B  "
                      << hex64(fnv1a64(result.bytes)) << "  "
                      << result.reports.size() << " changed words\n";
        }
        for (Variant const& variant : variants) {
            std::cout << "  " << out_path(variant.file) << "  -- "
                      << variant.role << "\n";
        }
        std::cout << "  " << manifest_path << "\n";
    } catch (std::exception const& e) {
        std::cout << "fatal: " << e.what() << "\n";
        return 1;
    }
    return t.report();
}
