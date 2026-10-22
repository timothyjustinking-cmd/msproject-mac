/*
 * mpp_tombstone_tests.cpp -- W45: a tombstoned task row is not a task.
 *
 * THE BUG THIS PINS.  `core/mpp` used to decide Task liveness from VarMeta
 * presence alone.  A delete in Project's own file leaves the row's VarMeta group
 * in place and only sets the FixedMeta entry's +0 byte to 0x02, so
 * corpus/probe/w10-adddel-back.mpp (add a task, delete it) reopened in Project
 * as 5 tasks and the old reader returned 6.  The predicate is now
 * "FixedMeta entry +0 == 0 AND a VarMeta group" (core/mpp/reader.cpp).
 *
 * WHAT THIS TEST DOES.  For every fixture it can find it asserts the live task
 * count, the live UniqueIDs, and that a tombstoned uid is reported as a skipped
 * row rather than a task.  It also asserts the flip side: a file with no
 * tombstones still returns every task, so the fix cannot be "skip more rows".
 *
 * FIXTURES.  tests/data/synthetic-base.mpp is committed and always used.  The
 * others are optional and skipped when absent, because the corpus is
 * git-ignored:
 *   - corpus/probe/w10-adddel-back.mpp  Project's add-then-delete file (5 tasks)
 *   - corpus/probe/w10-adddel-del.mpp   Project's delete-middle file (4 tasks)
 *   - the W45 writer's own fixtures, emitted by mppwrite_roundtrip_add_delete:
 *     w45-add.mpp, w45-add-back.mpp, w45-del-middle.mpp,
 *     w45-append-after-delete.mpp
 *
 * This test never writes a file.  It links only core_mpp.
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "reader.h"

#ifndef MPP_PROBE_DIR
#define MPP_PROBE_DIR "corpus/probe"
#endif
#ifndef MPP_TEST_DATA_DIR
#define MPP_TEST_DATA_DIR "tests/data"
#endif
#ifndef MPP_W45_TEST_OUT_TOP
#define MPP_W45_TEST_OUT_TOP "build/core/mppwrite/test-out"
#endif
#ifndef MPP_W45_TEST_OUT_MIRROR
#define MPP_W45_TEST_OUT_MIRROR "core/mppwrite/build/vs-w45/test-out"
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
    void equal(std::size_t actual, std::size_t expected, std::string const& what) {
        check(actual == expected, what + " (actual " + std::to_string(actual) +
                                      ", expected " + std::to_string(expected) + ")");
    }
    int checks() const { return checks_; }
    int failures() const { return failures_; }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

std::string join(std::string const& a, std::string const& b) {
    return a + "/" + b;
}

bool file_exists(std::string const& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::vector<std::uint32_t> task_uids(mpp::Document const& document) {
    std::vector<std::uint32_t> uids;
    for (mpp::Task const& task : document.tasks) {
        uids.push_back(task.uid);
    }
    return uids;
}

std::vector<std::uint32_t> task_ids(mpp::Document const& document) {
    std::vector<std::uint32_t> ids;
    for (mpp::Task const& task : document.tasks) {
        ids.push_back(task.row_id);
    }
    return ids;
}

bool skipped_as_tombstone(mpp::Document const& document, std::uint32_t uid) {
    for (mpp::SkippedRow const& row : document.skipped_rows) {
        if (row.uid == uid && row.reason.find("tombstone") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool has_task(mpp::Document const& document, std::uint32_t uid) {
    return std::any_of(document.tasks.begin(), document.tasks.end(),
                       [uid](mpp::Task const& task) { return task.uid == uid; });
}

bool has_warning(mpp::Document const& document, std::string_view needle) {
    for (std::string const& warning : document.warnings) {
        if (warning.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/* The W45 writer may have put its fixtures in either build path; use whichever
 * holds w45-add.mpp.  Returns "" when neither does (the writer test has not been
 * run in this build tree). */
std::string writer_fixture_dir() {
    for (std::string const& candidate :
         {std::string(MPP_W45_TEST_OUT_TOP), std::string(MPP_W45_TEST_OUT_MIRROR)}) {
        if (file_exists(join(candidate, "w45-add.mpp"))) {
            return candidate;
        }
    }
    return {};
}

/* A fixture with no tombstones: every task must still be live.  This is the
 * control that proves the new predicate did not simply drop rows. */
void test_control(TestContext& t) {
    std::string const path = join(MPP_TEST_DATA_DIR, "synthetic-base.mpp");
    if (!file_exists(path)) {
        std::cout << "SKIP: control: " << path << " is missing\n";
        return;
    }
    mpp::Document const document = mpp::load_file(path);
    t.equal(document.tasks.size(), std::size_t{3},
            "synthetic-base: three live tasks (no tombstones)");
    t.check(task_uids(document) == std::vector<std::uint32_t>({1, 2, 3}),
            "synthetic-base: uids are 1,2,3");
    t.check(task_ids(document) == std::vector<std::uint32_t>({1, 2, 3}),
            "synthetic-base: IDs are 1,2,3");
    t.check(document.skipped_rows.empty(),
            "synthetic-base: nothing is skipped when there are no tombstones");
}

/* One fixture with a known tombstoned uid and a known live set. */
void check_fixture(TestContext& t, std::string const& path,
                   std::string const& label,
                   std::vector<std::uint32_t> const& live_uids,
                   std::vector<std::uint32_t> const& tombstoned) {
    if (!file_exists(path)) {
        std::cout << "SKIP: " << label << " (" << path << " is missing)\n";
        return;
    }
    mpp::Document const document = mpp::load_file(path);
    t.equal(document.tasks.size(), live_uids.size(),
            label + ": live task count matches Project");
    t.check(task_uids(document) == live_uids, label + ": live uids");
    std::vector<std::uint32_t> expected_ids;
    for (std::size_t i = 0; i < live_uids.size(); ++i) {
        expected_ids.push_back(static_cast<std::uint32_t>(i + 1));
    }
    t.check(task_ids(document) == expected_ids,
            label + ": live IDs are 1..N in row order");
    for (std::uint32_t const uid : tombstoned) {
        t.check(skipped_as_tombstone(document, uid),
                label + ": uid " + std::to_string(uid) +
                    " is not reported as a tombstoned skipped row");
        t.check(!has_task(document, uid),
                label + ": tombstoned uid " + std::to_string(uid) +
                    " was returned as a live task");
    }
    t.equal(document.skipped_rows.size(), tombstoned.size(),
            label + ": only the tombstoned rows are skipped");
}

void test_project_fixtures(TestContext& t) {
    std::string const probe = MPP_PROBE_DIR;
    check_fixture(t, join(probe, "w10-adddel-back.mpp"),
                  "w10-adddel-back (add then delete)", {1, 2, 3, 4, 5}, {6});
    check_fixture(t, join(probe, "w10-adddel-del.mpp"),
                  "w10-adddel-del (delete middle)", {1, 2, 3, 5}, {4, 6});

    /* The regression's own shape: uid 6 keeps its VarMeta group, so the old
     * VarMeta-only predicate returned it as a task.  The warning must say so. */
    std::string const back = join(probe, "w10-adddel-back.mpp");
    if (file_exists(back)) {
        mpp::Document const document = mpp::load_file(back);
        t.check(has_warning(document, "VarMeta group is still present"),
                "w10-adddel-back: warns that the tombstone still has a VarMeta group");
    }
}

void test_writer_fixtures(TestContext& t) {
    std::string const dir = writer_fixture_dir();
    if (dir.empty()) {
        std::cout << "SKIP: W45 writer fixtures absent (run "
                     "mppwrite_roundtrip_add_delete first); looked in "
                  << MPP_W45_TEST_OUT_TOP << " and " << MPP_W45_TEST_OUT_MIRROR
                  << "\n";
        return;
    }
    check_fixture(t, join(dir, "w45-add.mpp"), "w45-add (one appended task)",
                  {1, 2, 3, 4}, {});
    check_fixture(t, join(dir, "w45-add-back.mpp"),
                  "w45-add-back (append then delete the appended row)", {1, 2, 3}, {4});
    check_fixture(t, join(dir, "w45-del-middle.mpp"), "w45-del-middle (delete uid 2)",
                  {1, 3}, {2});
    check_fixture(t, join(dir, "w45-del-middle-b.mpp"),
                  "w45-del-middle-b (Project's VarMeta shape: entries kept)", {1, 3},
                  {2});
    check_fixture(t, join(dir, "w45-append-after-delete.mpp"),
                  "w45-append-after-delete", {1, 2, 3, 5}, {4});
}

} /* namespace */

int main() {
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
#endif
    try {
        TestContext context;
        test_control(context);
        test_project_fixtures(context);
        test_writer_fixtures(context);
        if (context.failures() != 0) {
            std::cerr << "FAIL: " << context.failures() << " failed of "
                      << context.checks() << " checks" << std::endl;
            return 1;
        }
        std::cout << "PASS: " << context.checks() << " checks" << std::endl;
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << std::endl;
        return 1;
    }
}
