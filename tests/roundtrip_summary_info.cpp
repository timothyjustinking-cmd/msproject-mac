/*
 * roundtrip_summary_info.cpp -- W34-3.3: the DocumentSummaryInformation writer
 * against Project's own saves of Calculate Project and Move, plus its refusals.
 *
 * WHAT IS PROVEN.  For each pair the port opens PROJECT'S CONTROL, takes the
 * task table of PROJECT'S EDIT SAVE as the "edited" table (so the only thing
 * under test is the derivation, not a row writer), runs
 * `write_project_summary` and requires the stream it leaves to equal the
 * stream in Project's edit save BYTE FOR BYTE.  The stream has no per-save
 * noise (control vs the committed null save is byte-identical in every pair),
 * so there is no mask and no normalisation: any difference is the writer's.
 * Every other stream must be untouched.
 *
 * The pairs: Calculate Project (wave30-calculate, which changes 175 bytes and
 * grows the stream by 4), Move back (7 bytes, the length is kept), Move
 * forward and the second Calculate (Project leaves the stream byte-identical:
 * the writer must derive the same bytes, not merely leave them alone), and
 * the Inactivate, Fill and manual-mode saves that also rewrite it.  The
 * "changed" flag in each row below is asserted against control vs edit, so a
 * pair that stopped exercising the writer would fail rather than pass
 * vacuously.
 *
 * The port's output is also saved under MPPWRITE_TEST_OUT so
 * scripts/dsi_reader.py -- a separate Python reader of the property set,
 * sharing no code with the writer -- can read the port's file and Project's
 * file and compare them property by property (scripts/check_dsi.py).
 *
 * REFUSALS.  A damaged stream (short, byte order, section count, format
 * identifiers, an appended byte, a flipped pad byte) and each project-row
 * value the writer will not guess (a DurationFormat it has not measured, a
 * negative, non-finite, fractional or one-hour total work, an invalid start)
 * are refused with the document unchanged.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "fixture_guard.h"
#include "summary_info.h"
#include "table.h"

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

using mppwrite::CfbDocument;
using mppwrite::TaskTable;
using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kRowWork = 8; /* the project row's total work, f64 */

class TestContext {
  public:
    void check(bool condition, std::string const& what) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::cout << "FAIL: " << what << "\n";
        }
    }
    void equal(std::size_t actual, std::size_t expected,
               std::string const& what) {
        check(actual == expected,
              what + " (actual " + std::to_string(actual) + ", expected " +
                  std::to_string(expected) + ")");
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

std::string out_path(std::string const& name) {
    return join(MPPWRITE_TEST_OUT, name);
}

CfbDocument doc_from_data(std::string const& name) {
    std::string const path = join(MPPWRITE_TEST_DATA, name);
    fixture::require_committed(path);
    return CfbDocument::from_file(path);
}

std::string summary_stream(CfbDocument const& doc) {
    for (std::string const& path : doc.stream_paths()) {
        std::string const suffix = "DocumentSummaryInformation";
        if (path.size() >= suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) ==
                0) {
            return path;
        }
    }
    throw std::runtime_error("the document has no DocumentSummaryInformation");
}

template <typename Fn>
void expect_error(TestContext& t, Fn&& fn, std::string_view needle,
                  std::string const& what) {
    try {
        fn();
        t.check(false, what + ": no exception was thrown");
    } catch (mppwrite::Error const& error) {
        std::string const message = error.what();
        t.check(needle.empty() || message.find(needle) != std::string::npos,
                what + ": error '" + message + "' does not contain '" +
                    std::string(needle) + "'");
    }
}

struct Pair {
    char const* control;
    char const* edit;
    char const* label;
    bool changed; /* Project's save changes the stream */
};

Pair const kPairs[] = {
    {"wave30-calculate-control.mpp", "wave30-calculate.mpp", "calculate", true},
    {"wave34-calculate-second-control.mpp", "wave34-calculate-second.mpp",
     "calculate-second", false},
    {"wave30-move-back-control.mpp", "wave30-move-back.mpp", "move-back", true},
    {"wave30-move-forward-control.mpp", "wave30-move-forward.mpp",
     "move-forward", false},
    {"wave30-inactivate-second-control.mpp", "wave30-inactivate-second.mpp",
     "inactivate-second", true},
    {"wave31-fill-control.mpp", "wave31-fill.mpp", "fill", true},
    {"wave30-task-mode-manual-control.mpp", "wave30-task-mode-manual.mpp",
     "task-mode-manual", true},
};

void test_pair(TestContext& t, Pair const& p) {
    std::string const label = p.label;
    CfbDocument const control = doc_from_data(p.control);
    CfbDocument const edit = doc_from_data(p.edit);
    std::string const stream = summary_stream(control);
    Bytes const control_dsi = control.read_stream(stream);
    Bytes const edit_dsi = edit.read_stream(summary_stream(edit));
    t.check((control_dsi != edit_dsi) == p.changed,
            label + ": Project's save " +
                (p.changed ? "changes" : "leaves") + " the stream");

    TaskTable const before = TaskTable::from_document(control);
    TaskTable const after = TaskTable::from_document(edit);
    CfbDocument work = control;
    mppwrite::write_project_summary(work, before, after);

    Bytes const built = work.read_stream(stream);
    t.equal(built.size(), edit_dsi.size(), label + ": stream length");
    t.check(built == edit_dsi,
            label + ": the stream equals Project's edit save byte for byte");
    for (std::string const& path : control.stream_paths()) {
        if (path == stream) {
            continue;
        }
        t.check(work.has_stream(path) &&
                    work.read_stream(path) == control.read_stream(path),
                label + ": unowned stream changed: " + path);
    }
    work.save(out_path("w34-summary-" + label + ".mpp"));
}

/* ---- refusals ----------------------------------------------------------- */

void refuses_stream(TestContext& t, std::string const& what,
                    std::string_view needle,
                    void (*mutate)(Bytes&)) {
    CfbDocument const edit = doc_from_data("wave30-calculate.mpp");
    CfbDocument doc = doc_from_data("wave30-calculate-control.mpp");
    std::string const stream = summary_stream(doc);
    Bytes bytes = doc.read_stream(stream);
    mutate(bytes);
    doc.replace_stream(stream, bytes);
    Bytes const before = doc.to_bytes();
    TaskTable const table = TaskTable::from_document(doc);
    TaskTable const after = TaskTable::from_document(edit);
    expect_error(
        t, [&] { mppwrite::write_project_summary(doc, table, after); }, needle,
        what);
    t.check(doc.to_bytes() == before, what + ": the document changed");
}

void put_f64(TaskTable& table, std::size_t row, std::size_t offset,
             double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    table.set_u32(row, offset, static_cast<std::uint32_t>(bits & 0xFFFFFFFFu));
    table.set_u32(row, offset + 4, static_cast<std::uint32_t>(bits >> 32));
}

template <typename Mutator>
void refuses_row(TestContext& t, std::string const& what,
                 std::string_view needle, Mutator&& mutate) {
    CfbDocument doc = doc_from_data("wave30-calculate-control.mpp");
    TaskTable const before = TaskTable::from_document(doc);
    TaskTable after = TaskTable::from_document(doc_from_data("wave30-calculate.mpp"));
    mutate(after);
    Bytes const unchanged = doc.to_bytes();
    expect_error(
        t, [&] { mppwrite::write_project_summary(doc, before, after); }, needle,
        what);
    t.check(doc.to_bytes() == unchanged, what + ": the document changed");
}

void test_refusals(TestContext& t) {
    refuses_stream(t, "a stream shorter than its header",
                   "shorter than its 28-byte header",
                   [](Bytes& b) { b.resize(10); });
    refuses_stream(t, "a wrong byte-order word", "byte-order word",
                   [](Bytes& b) { b[0] ^= 0xFF; });
    refuses_stream(t, "three sections", "sections", [](Bytes& b) { b[24] = 3; });
    refuses_stream(t, "another format identifier", "format identifiers",
                   [](Bytes& b) { b[28] ^= 0xFF; });
    refuses_stream(t, "a trailing byte", "DocumentSummaryInformation:",
                   [](Bytes& b) { b.push_back(0); });
    refuses_stream(t, "a truncated stream", "DocumentSummaryInformation:",
                   [](Bytes& b) { b.resize(b.size() - 40); });

    refuses_row(t, "an unmeasured DurationFormat", "DurationFormat 99",
                [](TaskTable& table) {
                    table.set_u16(0, mppwrite::task_row::kDurationFormat, 99);
                });
    refuses_row(t, "an invalid start", "valid packed date",
                [](TaskTable& table) {
                    table.set_u32(0, mppwrite::task_row::kStart, 0xFFFFFFFFu);
                });
    refuses_row(t, "a negative total work", "negative or not finite",
                [](TaskTable& table) {
                    put_f64(table, 0, kRowWork, -60000.0);
                });
    refuses_row(t, "a non-finite total work", "negative or not finite",
                [](TaskTable& table) {
                    put_f64(table, 0, kRowWork,
                            std::numeric_limits<double>::infinity());
                });
    refuses_row(t, "a fractional-hour total work", "whole number of hours",
                [](TaskTable& table) {
                    put_f64(table, 0, kRowWork, 90000.0);
                });
    refuses_row(t, "a one-hour total work", "one hour", [](TaskTable& table) {
        put_f64(table, 0, kRowWork, 60000.0);
    });
}

} /* namespace */

int main() {
    try {
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);
        TestContext t;
        for (Pair const& pair : kPairs) {
            test_pair(t, pair);
        }
        test_refusals(t);
        return t.report();
    } catch (std::exception const& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << std::endl;
        return 1;
    }
}
