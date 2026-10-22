/*
 * roundtrip_row_write.cpp -- stage 4: write a task row so every copy agrees.
 *
 * CLAIM UNDER TEST.  A date or a duration has several stored copies, and a row
 * is only writable if they all move together:
 *
 *   114/TBkndTask/FixedData   +74 (TotalSlack), +80/+84/+88 (actual/duration/
 *                             remaining), +96/+100, +104/+108, +112/+116,
 *                             +186/+190 (slack copies)
 *   114/TBkndTask/Fixed2Data  record row+3: +46/+50 (project-frame start),
 *                             +54 (project-frame finish), +58 (duration)
 *
 * The test does not trust the mapping.  For every write it
 *   1. asserts the values it read out of base.mpp first (anchors),
 *   2. writes through `write_task_row`, which re-reads the row and refuses if
 *      any value did not take,
 *   3. asserts every copy with `read_task_values` *and* with raw byte offsets,
 *   4. asserts the row that was not written is byte-identical,
 *   5. asserts exactly two streams changed, 214/ untouched, the container still
 *      round-trips byte-identically, the file size did not change, and
 *      `check_table` is empty,
 *   6. asserts the refusals fire: a partial TaskValues, and a one-copy change
 *      dressed up as a full one.
 *
 * The last block builds `oracle-recalc.mpp` for an MS Project check: the whole
 * project shifted forward 14 days plus task A lengthened from 5 to 7 working
 * days, written through every copy.  See spec/mpp-writer.md §10.
 */

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
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

char const* kFixed = "114/TBkndTask/FixedData";
char const* kFixed2 = "114/TBkndTask/Fixed2Data";

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

/* Only 214/ is checked here; the other tests cover it stream by stream. */
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

/* A packed date's day field is the high half, so a whole-day move is an add. */
std::uint32_t shift_days(std::uint32_t packed, int days) {
    std::uint32_t const day = packed >> 16;
    std::uint32_t const ticks = packed & 0xFFFFu;
    return ((day + static_cast<std::uint32_t>(days)) << 16) | ticks;
}

std::uint32_t u32_at(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

std::uint16_t u16_at(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}

std::string date_label(std::uint32_t packed) {
    return mppwrite::format_date(packed);
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

        mppwrite::CfbDocument const original =
            mppwrite::CfbDocument::from_file(source);
        std::vector<std::uint8_t> const original_bytes = read_file(source);
        t.check(original.check().empty(),
                "base.mpp: container check: " + original.check());

        /* ---- 0. anchors: what read_task_values sees in base.mpp --------- */
        {
            mppwrite::TaskValues const a =
                mppwrite::read_task_values(original, 1);
            t.check(a.start == 0x3bf212c0u,
                    "row 1 start is " + std::to_string(a.start) + ", expected "
                    "0x3bf212c0 (2026-01-05 08:00), got " +
                        date_label(a.start));
            t.check(date_label(a.start) == "2026-01-05 08:00",
                    "row 1 start reads '" + date_label(a.start) + "'");
            t.check(a.finish == 0x3bf627d8u, "row 1 finish is not 0x3bf627d8");
            t.check(date_label(a.finish) == "2026-01-09 17:00",
                    "row 1 finish reads '" + date_label(a.finish) + "'");
            t.check(a.project_start == 0x3bf11e78u,
                    "row 1 project_start is not 0x3bf11e78");
            t.check(date_label(a.project_start) == "2026-01-04 13:00",
                    "row 1 project_start reads '" +
                        date_label(a.project_start) + "'");
            t.check(a.project_finish == 0x3bf61c20u,
                    "row 1 project_finish is not 0x3bf61c20");
            t.check(date_label(a.project_finish) == "2026-01-09 12:00",
                    "row 1 project_finish reads '" +
                        date_label(a.project_finish) + "'");
            t.check(a.late_start == a.project_start &&
                        a.late_finish == a.project_finish,
                    "row 1 has slack 0, so late start/finish should equal the "
                    "project frame");
            t.check(a.duration == 24000 && a.remaining_duration == 24000 &&
                        a.actual_duration == 0,
                    "row 1 duration family is not 24000/24000/0");
            t.check(a.total_slack == 0 && a.start_slack == 0,
                    "row 1 slack is not 0/0");
            t.check(a.fixed2_start == a.project_start &&
                        a.fixed2_finish == a.project_finish &&
                        a.fixed2_duration == a.duration,
                    "row 1 Fixed2Data copies do not equal +104/+108/+84");
            t.check(a.priority == 500 && a.percent_complete == 0,
                    "row 1 priority/percent is not 500/0");

            mppwrite::TaskValues const b =
                mppwrite::read_task_values(original, 2);
            t.check(b.total_slack == 9600 && b.start_slack == 9600,
                    "row 2 slack is not 9600/9600");
        }

        /* ================= 1. every copy of a changed row ================= */
        mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
        std::uint32_t const new_start =
            mppwrite::pack_date_ymd(2026, 3, 2, 8 * 3600);
        std::uint32_t const new_finish =
            mppwrite::pack_date_ymd(2026, 3, 10, 17 * 3600);
        std::uint32_t const new_project_start =
            mppwrite::pack_date_ymd(2026, 3, 1, 13 * 3600);
        std::uint32_t const new_project_finish =
            mppwrite::pack_date_ymd(2026, 3, 10, 12 * 3600);
        t.check(new_start == 0x3c2a12c0u,
                "pack_date_ymd(2026,3,2,08:00) is not 0x3c2a12c0");

        mppwrite::TaskValues values =
            mppwrite::read_task_values(doc, 1); /* keep the row's frame */
        values.start = new_start;
        values.finish = new_finish;
        values.project_start = new_project_start;
        values.project_finish = new_project_finish;
        values.late_start = new_project_start;
        values.late_finish = new_project_finish;
        values.duration = 33600; /* 7 working days */
        /* Say "recompute the copies of duration" rather than leaving the old
         * 24000s in place: the writer refuses a duration family that does not
         * add up and a single-copy duration change. */
        values.remaining_duration = mppwrite::kInherit;
        values.fixed2_duration = mppwrite::kInherit;
        values.total_slack = 0;

        mppwrite::TaskValues const resolved =
            mppwrite::resolve_task_values(doc, 1, values);
        t.check(resolved.remaining_duration == 33600,
                "resolve: remaining duration is not 33600");
        t.check(resolved.fixed2_start == new_project_start &&
                    resolved.fixed2_finish == new_project_finish &&
                    resolved.fixed2_duration == 33600,
                "resolve: Fixed2Data copies were not derived");

        mppwrite::RowWriteReport const report =
            mppwrite::write_task_row(doc, 1, values);
        t.check(report.warnings.empty(),
                "row 1 write produced warnings: " +
                    join_list(report.warnings));
        t.check(report.written == resolved,
                "row 1 report.written differs from resolve_task_values");

        /* ---- 1a. the writer's reader sees every copy -------------------- */
        mppwrite::TaskValues const read_back =
            mppwrite::read_task_values(doc, 1);
        t.check(read_back == resolved,
                "row 1 does not read back as written");
        t.check(read_back.start == new_start && read_back.finish == new_finish,
                "task-frame dates do not read back");
        t.check(read_back.project_start == new_project_start &&
                    read_back.project_finish == new_project_finish,
                "project-frame dates do not read back");
        t.check(read_back.late_start == new_project_start &&
                    read_back.late_finish == new_project_finish,
                "late dates do not read back");
        t.check(read_back.duration == 33600 &&
                    read_back.remaining_duration == 33600,
                "duration copies do not read back");
        t.check(read_back.total_slack == 0 && read_back.start_slack == 0,
                "slack copies do not read back");
        t.check(read_back.fixed2_start == new_project_start &&
                    read_back.fixed2_finish == new_project_finish &&
                    read_back.fixed2_duration == 33600,
                "Fixed2Data copies do not read back");

        /* ---- 1b. and so do the raw bytes ------------------------------- */
        {
            mppwrite::TaskTable const table =
                mppwrite::TaskTable::from_document(doc);
            std::size_t const row = table.row_for_uid(1);
            std::size_t const p = mppwrite::task_row_offset(row);
            std::vector<std::uint8_t> const fixed = table.fixed_data();
            t.check(u32_at(fixed, p + mppwrite::task_row::kStart) == new_start,
                    "byte +96 is not the new start");
            t.check(u32_at(fixed, p + mppwrite::task_row::kFinish) == new_finish,
                    "byte +100 is not the new finish");
            t.check(u32_at(fixed, p + mppwrite::task_row::kStart2) ==
                        new_project_start,
                    "byte +104 is not the new project-frame start");
            t.check(u32_at(fixed, p + mppwrite::task_row::kFinish2) ==
                        new_project_finish,
                    "byte +108 is not the new project-frame finish");
            t.check(u32_at(fixed, p + mppwrite::task_row::kStart3) ==
                        new_project_start,
                    "byte +112 is not the new late start");
            t.check(u32_at(fixed, p + mppwrite::task_row::kFinish3) ==
                        new_project_finish,
                    "byte +116 is not the new late finish");
            t.check(u32_at(fixed, p + mppwrite::task_row::kDuration) == 33600,
                    "byte +84 is not 33600");
            t.check(u32_at(fixed, p + mppwrite::task_row::kRemainingDuration) ==
                        33600,
                    "byte +88 is not 33600");
            t.check(u32_at(fixed, p + mppwrite::task_row::kTotalSlack) == 0,
                    "byte +74 is not 0");
            t.check(u32_at(fixed, p + mppwrite::task_row::kTotalSlackCopy) == 0,
                    "byte +190 is not 0");
            t.check(u16_at(fixed, p + mppwrite::task_row::kPriority) == 500,
                    "priority changed when it was not supplied");

            std::vector<std::uint8_t> const fixed2 =
                mppwrite::read_fixed2_data(doc);
            std::size_t const r = mppwrite::fixed2_record_offset(row);
            t.check(u32_at(fixed2, r + mppwrite::fixed2_row::kStart) ==
                        new_project_start,
                    "Fixed2Data +46 is not the new project-frame start");
            t.check(u32_at(fixed2, r + mppwrite::fixed2_row::kStartCopy) ==
                        new_project_start,
                    "Fixed2Data +50 is not the new project-frame start");
            t.check(u32_at(fixed2, r + mppwrite::fixed2_row::kFinish) ==
                        new_project_finish,
                    "Fixed2Data +54 is not the new project-frame finish");
            t.check(u32_at(fixed2, r + mppwrite::fixed2_row::kDuration) == 33600,
                    "Fixed2Data +58 is not 33600");
        }

        /* ---- 1c. nothing else moved ------------------------------------ */
        {
            std::vector<std::string> const changed =
                changed_streams(original, doc);
            t.check(changed.size() == 2 &&
                        changed[0] == kFixed && changed[1] == kFixed2,
                    "changed streams: " + join_list(changed));
            check_214_untouched(t, original, doc, "row-write");
            t.check(doc.to_bytes().size() == original_bytes.size(),
                    "the file changed size (" +
                        std::to_string(doc.to_bytes().size()) + " vs " +
                        std::to_string(original_bytes.size()) + ")");
            check_emitted(t, doc, "row-write");
            mppwrite::TaskValues const other =
                mppwrite::read_task_values(doc, 2);
            mppwrite::TaskValues const before =
                mppwrite::read_task_values(original, 2);
            t.check(other == before, "row 2 changed when row 1 was written");
            /* exactly two streams means the in-place patcher's byte range is
             * untouched elsewhere; re-check the count deliberately. */
            std::size_t streams_changed = 0;
            std::vector<std::string> const paths = original.stream_paths();
            for (std::string const& path : paths) {
                if (original.read_stream(path) != doc.read_stream(path)) {
                    ++streams_changed;
                }
            }
            t.check(streams_changed == 2,
                    std::to_string(streams_changed) + " streams changed");
        }

        /* ---- 1d. save + reload through the container ------------------- */
        {
            std::string const out = join(MPPWRITE_TEST_OUT,
                                         "row-write-all-copies.mpp");
            doc.save(out);
            t.check(read_file(out) == doc.to_bytes(), "saved file differs");
            mppwrite::CfbDocument const reloaded =
                mppwrite::CfbDocument::from_file(out);
            t.check(mppwrite::read_task_values(reloaded, 1) == resolved,
                    "reloaded: row 1 does not match");
            t.check(mppwrite::check_table(reloaded).empty(),
                    "reloaded: table check: " +
                        mppwrite::check_table(reloaded));
        }

        /* ================= 2. the refusals ============================== */
        {
            mppwrite::CfbDocument fresh =
                mppwrite::CfbDocument::from_file(source);
            std::vector<std::uint8_t> const before = fresh.to_bytes();

            /* (i) only the task start supplied: every other copy missing. */
            mppwrite::TaskValues partial;
            partial.start = new_start;
            bool threw = false;
            std::string message;
            try {
                mppwrite::write_task_row(fresh, 1, partial);
            } catch (mppwrite::Error const& e) {
                threw = true;
                message = e.what();
            }
            t.check(threw, "a partial TaskValues was accepted");
            t.check(message.find("incomplete date copies") != std::string::npos,
                    "partial write message was: " + message);
            t.check(message.find("+104") != std::string::npos,
                    "partial write message does not name +104: " + message);

            /* (ii) every copy present, but only +96 differs from the row. */
            mppwrite::TaskValues single = mppwrite::read_task_values(fresh, 1);
            single.start = new_start;
            threw = false;
            message.clear();
            try {
                mppwrite::write_task_row(fresh, 1, single);
            } catch (mppwrite::Error const& e) {
                threw = true;
                message = e.what();
            }
            t.check(threw, "a one-copy start change was accepted");
            t.check(message.find("single-copy") != std::string::npos,
                    "single-copy message was: " + message);
            t.check(fresh.to_bytes() == before,
                    "a refused write still changed the file");

            /* (iii) a duration change with a contradictory remaining value. */
            mppwrite::TaskValues contradiction =
                mppwrite::read_task_values(fresh, 1);
            contradiction.duration = 33600;
            contradiction.remaining_duration = 24000;
            threw = false;
            message.clear();
            try {
                mppwrite::write_task_row(fresh, 1, contradiction);
            } catch (mppwrite::Error const& e) {
                threw = true;
                message = e.what();
            }
            t.check(threw, "a duration family that does not add up was accepted");
            t.check(message.find("duration family disagrees") != std::string::npos,
                    "duration message was: " + message);
            t.check(fresh.to_bytes() == before,
                    "a refused duration write still changed the file");
        }

        /* ============ 3. recalculate_write_path: shift a whole plan ====== */
        {
            mppwrite::CfbDocument plan_doc =
                mppwrite::CfbDocument::from_file(source);
            std::vector<mppwrite::PlannedRow> rows;
            for (std::uint32_t uid = 0; uid <= 3; ++uid) {
                mppwrite::PlannedRow planned;
                planned.uid = uid;
                planned.values =
                    mppwrite::read_task_values(plan_doc, uid);
                planned.values.start =
                    shift_days(planned.values.start, 14);
                planned.values.finish =
                    shift_days(planned.values.finish, 14);
                planned.values.project_start =
                    shift_days(planned.values.project_start, 14);
                planned.values.project_finish =
                    shift_days(planned.values.project_finish, 14);
                planned.values.late_start =
                    shift_days(planned.values.late_start, 14);
                planned.values.late_finish =
                    shift_days(planned.values.late_finish, 14);
                rows.push_back(planned);
            }
            std::vector<std::string> warnings;
            std::size_t const written =
                mppwrite::recalculate_write_path(plan_doc, rows,
                                                 "114/TBkndTask", &warnings);
            t.check(written == 4, "recalculate_write_path wrote " +
                                      std::to_string(written) + " rows");
            t.check(warnings.empty(), "recalculate_write_path warnings: " +
                                          join_list(warnings));

            for (std::uint32_t uid = 0; uid <= 3; ++uid) {
                mppwrite::TaskValues const now =
                    mppwrite::read_task_values(plan_doc, uid);
                mppwrite::TaskValues const was =
                    mppwrite::read_task_values(original, uid);
                t.check(now.start == shift_days(was.start, 14) &&
                            now.finish == shift_days(was.finish, 14) &&
                            now.project_start ==
                                shift_days(was.project_start, 14) &&
                            now.project_finish ==
                                shift_days(was.project_finish, 14) &&
                            now.late_start == shift_days(was.late_start, 14) &&
                            now.late_finish == shift_days(was.late_finish, 14),
                        "uid " + std::to_string(uid) +
                            " did not shift every date copy by 14 days");
                t.check(now.duration == was.duration &&
                            now.remaining_duration == was.remaining_duration,
                        "uid " + std::to_string(uid) + " duration changed");
                t.check(now.total_slack == was.total_slack &&
                            now.start_slack == was.start_slack,
                        "uid " + std::to_string(uid) + " slack changed");
            }
            /* +74 is set, not merely copied: check it through the raw row. */
            mppwrite::TaskTable const table =
                mppwrite::TaskTable::from_document(plan_doc);
            t.check(table.u32(2, mppwrite::task_row::kTotalSlack) == 9600,
                    "row 2 TotalSlack at +74 is " +
                        std::to_string(
                            table.u32(2, mppwrite::task_row::kTotalSlack)) +
                        ", expected 9600");
            t.check(table.u32(2, mppwrite::task_row::kTotalSlackCopy) == 9600,
                    "row 2 TotalSlack at +190 is not 9600");
            t.check(table.u32(2, mppwrite::task_row::kStartSlack) == 9600,
                    "row 2 StartSlack at +186 is not 9600");
            check_emitted(t, plan_doc, "recalculate");
            std::vector<std::string> const changed =
                changed_streams(original, plan_doc);
            t.check(changed.size() == 2,
                    "recalculate changed streams: " + join_list(changed));
            check_214_untouched(t, original, plan_doc, "recalculate");
            std::string const out =
                join(MPPWRITE_TEST_OUT, "recalculate-shift14.mpp");
            plan_doc.save(out);
            t.check(read_file(out) == plan_doc.to_bytes(),
                    "recalculate: saved file differs");
        }

        /* ======== 4. oracle-recalc.mpp for the MS Project run ============ */
        {
            mppwrite::CfbDocument oracle =
                mppwrite::CfbDocument::from_file(source);
            std::uint32_t const project_start =
                mppwrite::pack_date_ymd(2026, 1, 18, 13 * 3600); /* Mon 19 00:00
                                                                    local */
            std::uint32_t const project_finish =
                mppwrite::pack_date_ymd(2026, 1, 27, 12 * 3600);
            std::uint32_t const finish_a =
                mppwrite::pack_date_ymd(2026, 1, 27, 17 * 3600);
            std::uint32_t const finish_b =
                mppwrite::pack_date_ymd(2026, 1, 21, 17 * 3600);
            std::uint32_t const finish_c =
                mppwrite::pack_date_ymd(2026, 1, 20, 17 * 3600);
            std::uint32_t const frame_a =
                mppwrite::pack_date_ymd(2026, 1, 27, 12 * 3600);
            std::uint32_t const frame_b =
                mppwrite::pack_date_ymd(2026, 1, 21, 12 * 3600);
            std::uint32_t const frame_c =
                mppwrite::pack_date_ymd(2026, 1, 20, 12 * 3600);
            std::uint32_t const start_tasks =
                mppwrite::pack_date_ymd(2026, 1, 19, 8 * 3600);

            std::vector<mppwrite::PlannedRow> rows;
            /* project row: start/finish in all three pairs, slack 0 */
            mppwrite::PlannedRow project;
            project.uid = 0;
            project.values = mppwrite::read_task_values(oracle, 0);
            project.values.start = project_start;
            project.values.project_start = project_start;
            project.values.late_start = project_start;
            project.values.finish = project_finish;
            project.values.project_finish = project_finish;
            project.values.late_finish = project_finish;
            project.values.total_slack = 0;
            project.values.start_slack = 0;
            rows.push_back(project);

            /* task A: 5d -> 7d, ending Tue 2026-01-27 17:00, slack 0 */
            mppwrite::PlannedRow a;
            a.uid = 1;
            a.values = mppwrite::read_task_values(oracle, 1);
            a.values.start = start_tasks;
            a.values.finish = finish_a;
            a.values.project_start = project_start;
            a.values.project_finish = frame_a;
            a.values.late_start = project_start;
            a.values.late_finish = project_finish;
            a.values.duration = 33600;
            a.values.remaining_duration = mppwrite::kInherit;
            a.values.fixed2_duration = mppwrite::kInherit;
            a.values.total_slack = 0;
            rows.push_back(a);

            /* task B: 3d, ends Wed 2026-01-21; 4 working days of slack */
            mppwrite::PlannedRow b;
            b.uid = 2;
            b.values = mppwrite::read_task_values(oracle, 2);
            b.values.start = start_tasks;
            b.values.finish = finish_b;
            b.values.project_start = project_start;
            b.values.project_finish = frame_b;
            b.values.late_start =
                mppwrite::pack_date_ymd(2026, 1, 22, 13 * 3600);
            b.values.late_finish = project_finish;
            b.values.total_slack = 19200;
            rows.push_back(b);

            /* task C: 2d, ends Tue 2026-01-20; 5 working days of slack */
            mppwrite::PlannedRow c;
            c.uid = 3;
            c.values = mppwrite::read_task_values(oracle, 3);
            c.values.start = start_tasks;
            c.values.finish = finish_c;
            c.values.project_start = project_start;
            c.values.project_finish = frame_c;
            c.values.late_start =
                mppwrite::pack_date_ymd(2026, 1, 23, 13 * 3600);
            c.values.late_finish = project_finish;
            c.values.total_slack = 24000;
            rows.push_back(c);

            std::vector<std::string> warnings;
            mppwrite::recalculate_write_path(oracle, rows, "114/TBkndTask",
                                             &warnings);
            t.check(warnings.empty(),
                    "oracle fixture warnings: " + join_list(warnings));
            check_emitted(t, oracle, "oracle-recalc");
            std::vector<std::string> const changed =
                changed_streams(original, oracle);
            t.check(changed.size() == 2,
                    "oracle fixture changed streams: " + join_list(changed));
            check_214_untouched(t, original, oracle, "oracle-recalc");

            /* what MS Project should report after FileOpen + CalculateProject */
            mppwrite::TaskValues const oa =
                mppwrite::read_task_values(oracle, 1);
            mppwrite::TaskValues const ob =
                mppwrite::read_task_values(oracle, 2);
            mppwrite::TaskValues const oc =
                mppwrite::read_task_values(oracle, 3);
            mppwrite::TaskValues const o0 =
                mppwrite::read_task_values(oracle, 0);
            t.check(date_label(o0.start) == "2026-01-18 13:00" &&
                        date_label(o0.finish) == "2026-01-27 12:00",
                    "oracle project row is " + date_label(o0.start) + " / " +
                        date_label(o0.finish));
            t.check(oa.total_slack == 0 && oa.start_slack == 0 &&
                        ob.total_slack == 19200 && ob.start_slack == 19200 &&
                        oc.total_slack == 24000 && oc.start_slack == 24000,
                    "oracle slack is A " + std::to_string(oa.total_slack) + "/" +
                        std::to_string(oa.start_slack) + ", B " +
                        std::to_string(ob.total_slack) + "/" +
                        std::to_string(ob.start_slack) + ", C " +
                        std::to_string(oc.total_slack) + "/" +
                        std::to_string(oc.start_slack));
            t.check(date_label(oa.start) == "2026-01-19 08:00" &&
                        date_label(oa.finish) == "2026-01-27 17:00" &&
                        oa.duration == 33600,
                    "oracle task A is " + date_label(oa.start) + " / " +
                        date_label(oa.finish) + " / " +
                        std::to_string(oa.duration));
            t.check(date_label(ob.start) == "2026-01-19 08:00" &&
                        date_label(ob.finish) == "2026-01-21 17:00" &&
                        ob.duration == 14400,
                    "oracle task B is " + date_label(ob.start) + " / " +
                        date_label(ob.finish) + " / " +
                        std::to_string(ob.duration));
            t.check(date_label(oc.start) == "2026-01-19 08:00" &&
                        date_label(oc.finish) == "2026-01-20 17:00" &&
                        oc.duration == 9600,
                    "oracle task C is " + date_label(oc.start) + " / " +
                        date_label(oc.finish) + " / " +
                        std::to_string(oc.duration));

            std::string const out =
                join(MPPWRITE_TEST_OUT, "oracle-recalc.mpp");
            oracle.save(out);
            t.check(read_file(out) == oracle.to_bytes(),
                    "oracle fixture: saved file differs");
            mppwrite::CfbDocument const reloaded =
                mppwrite::CfbDocument::from_file(out);
            t.check(reloaded.check().empty(),
                    "oracle fixture: container check: " + reloaded.check());
            t.check(mppwrite::check_table(reloaded).empty(),
                    "oracle fixture: table check: " +
                        mppwrite::check_table(reloaded));
            t.check(differing_bytes(read_file(out), original_bytes) > 0,
                    "oracle fixture is identical to base.mpp");

            std::cout << "oracle fixture: " << out << "\n"
                      << "  project start 2026-01-19 08:00 (row 0: +96/+104/"
                         "+112 = 2026-01-18 13:00)\n"
                      << "  project finish 2026-01-27 17:00 (row 0: +100/+108/"
                         "+116 = 2026-01-27 12:00)\n"
                      << "  A 7d 2026-01-19 08:00 -> 2026-01-27 17:00\n"
                      << "  B 3d 2026-01-19 08:00 -> 2026-01-21 17:00\n"
                      << "  C 2d 2026-01-19 08:00 -> 2026-01-20 17:00\n";
        }

        /* the source fixture on disk is never touched */
        t.check(read_file(source) == original_bytes,
                "the source fixture was modified");
    } catch (std::exception const& e) {
        std::cout << "fatal: " << e.what() << "\n";
        return 1;
    }
    return t.report();
}
