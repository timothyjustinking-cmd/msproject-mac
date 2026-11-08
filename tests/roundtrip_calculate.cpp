/*
 * roundtrip_calculate.cpp -- W35-1.2: the measured Calculate Project save.
 *
 * The pair is tests/data/wave35-calculate-unlinked: the control (a pending
 * Duration change on an unlinked, automatically scheduled leaf), Project's F9
 * then save, and the committed null save of the control.  mppwrite::
 * save_calculate is applied to the control with the request read from Project's
 * result, and EVERY stream of the port's file is compared with Project's under
 * the null-save discipline of tests/save_pair_noise.h: the control-vs-null
 * positions are noise and masked, every null-vs-edit position is an effect the
 * port must reproduce, and the streams the edit changes are declared so a
 * stream that passes with no effect fails.
 *
 * The other three committed pairs (linked, lag, snet) are NOT writable and
 * each is refused with its first difference named; the unmodelled-shape and
 * request refusals each have a test with a specific needle.
 */

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "calculate.h"
#include "cfb_writer.h"
#include "fixture_guard.h"
#include "row_writer.h"
#include "save_pair_noise.h"
#include "table.h"

namespace {

using mppwrite::CalculateRequest;
using mppwrite::CfbDocument;
using mppwrite::TaskTable;
using Bytes = std::vector<std::uint8_t>;

int g_failures = 0;
int g_checks = 0;

void check(bool ok, std::string const& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

std::string fixture_path(std::string const& name) {
    return std::string(MPPWRITE_TEST_DATA) + "/" + name;
}

CfbDocument open(std::string const& name) {
    fixture::require_committed(fixture_path(name));
    return CfbDocument::from_file(fixture_path(name));
}

Bytes stream_or_empty(CfbDocument const& doc, std::string const& path) {
    return doc.has_stream(path) ? doc.read_stream(path) : Bytes{};
}

std::string out_path(std::string const& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

save_pair::Kind kind_for_path(std::string const& path) {
    std::string name = path;
    if (!name.empty() && name.front() == '\x05') {
        name.erase(name.begin());
    }
    if (name == "DocumentSummaryInformation") {
        return save_pair::Kind::exact;
    }
    if (name == "Props14") {
        return save_pair::Kind::props14;
    }
    if (name == "214/CEdl/Var2Data") {
        return save_pair::Kind::cedl;
    }
    return save_pair::Kind::fixed;
}

bool ends_with(std::string const& text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/* The streams Project's F9 save changes in the unlinked pair (the evidence's
 * stream summary).  Each must show a non-empty effect set. */
bool declared_stream(std::string const& path) {
    for (std::string_view suffix :
         {"114/Props", "TBkndTask/FixedData", "TBkndTask/FixedMeta",
          "TBkndTask/Fixed2Data", "TBkndTask/Var2Data", "TBkndTask/VarMeta",
          "DocumentSummaryInformation", "Props14"}) {
        if (ends_with(path, suffix)) {
            return true;
        }
    }
    return false;
}

/* Project's result as a request: the words the engine will own in W35-1.3. */
CalculateRequest request_from(CfbDocument const& result) {
    TaskTable const rows = TaskTable::from_document(result);
    CalculateRequest request;
    request.project_duration = rows.u32(0, mppwrite::task_row::kDuration);
    request.project_finish = rows.u32(0, mppwrite::task_row::kFinish);
    for (std::size_t row = 1; row < rows.rows(); ++row) {
        mppwrite::CalculateLeaf leaf;
        leaf.uid = rows.uid(row);
        leaf.late_start = rows.u32(row, mppwrite::task_row::kStart3);
        leaf.late_finish = rows.u32(row, mppwrite::task_row::kFinish3);
        leaf.free_slack =
            static_cast<std::int32_t>(rows.u32(row, 74));
        leaf.start_slack =
            static_cast<std::int32_t>(rows.u32(row, mppwrite::task_row::kStartSlack));
        leaf.total_slack = static_cast<std::int32_t>(rows.u32(row, 190));
        request.leaves.push_back(leaf);
    }
    return request;
}

template <typename Fn>
void refuses(Fn&& fn, std::string_view needle, std::string const& what) {
    try {
        fn();
        check(false, what + ": expected a refusal, none thrown");
    } catch (std::exception const& error) {
        std::string const text = error.what();
        check(text.find(needle) != std::string::npos,
              what + ": refusal names '" + std::string(needle) + "' (got: " +
                  text + ")");
    }
}

void unlinked_pair() {
    CfbDocument const control = open("wave35-calculate-unlinked-control.mpp");
    CfbDocument const null_save =
        open("wave35-calculate-unlinked-control-null.mpp");
    CfbDocument const project = open("wave35-calculate-unlinked.mpp");
    CfbDocument doc = control;
    std::string const path = out_path("w35-calculate-unlinked-port.mpp");
    mppwrite::CalculateWrite const written =
        mppwrite::save_calculate(doc, request_from(project), path);
    check(written.leaves_written == 2, "unlinked: two leaves written");
    check(written.var_records_appended == 5, "unlinked: five overtime appends");
    check(doc.check().empty(), "unlinked: container check");
    CfbDocument const port = CfbDocument::from_file(path);

    std::vector<std::string> paths = control.stream_paths();
    for (std::string const& p : project.stream_paths()) {
        paths.push_back(p);
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    std::size_t effects = 0;
    for (std::string const& stream : paths) {
        save_pair::Kind const kind = kind_for_path(stream);
        bool const declared = declared_stream(stream);
        save_pair::Verdict const verdict = save_pair::compare(
            kind, stream_or_empty(control, stream),
            stream_or_empty(null_save, stream),
            stream_or_empty(project, stream), stream_or_empty(port, stream),
            declared);
        effects += verdict.effects;
        save_pair::GapState const gap = save_pair::open_gap_state(stream, verdict);
        if (gap != save_pair::GapState::none) {
            std::cout << "   open gap (named, not this writer's): " << stream
                      << "\n";
            continue;
        }
        check(verdict.mismatches == 0,
              "unlinked: " + stream + " equals Project's outside the mask (" +
                  std::to_string(verdict.mismatches) + " mismatches, first " +
                  std::to_string(verdict.first_mismatch) + ": port " +
                  std::to_string(verdict.output_byte) + " project " +
                  std::to_string(verdict.result_byte) + ")");
        if (declared) {
            check(verdict.effects_ok,
                  "unlinked: " + stream + " effect set is non-empty");
        }
    }
    check(effects > 0, "unlinked: the pair has measured effects");
    std::filesystem::remove(path);
}

void refused_shapes() {
    for (auto const& [pair, needle] :
         std::vector<std::pair<std::string, std::string>>{
             {"linked", "link rows"},
             {"lag", "link rows"},
             {"snet", "link rows"}}) {
        CfbDocument doc = open("wave35-calculate-" + pair + "-control.mpp");
        CfbDocument const project = open("wave35-calculate-" + pair + ".mpp");
        std::string const path = out_path("w35-calculate-" + pair + "-refused.mpp");
        refuses([&] { mppwrite::save_calculate(doc, request_from(project), path); },
                needle, pair);
        check(!std::filesystem::exists(path), pair + ": no file left behind");
    }
}

void refused_requests() {
    CfbDocument const control = open("wave35-calculate-unlinked-control.mpp");
    CfbDocument const project = open("wave35-calculate-unlinked.mpp");
    std::string const path = out_path("w35-calculate-request-refused.mpp");
    CalculateRequest const good = request_from(project);

    {
        /* Project's own result is not pending any more. */
        CfbDocument done = project;
        refuses([&] { mppwrite::save_calculate(done, good, path); },
                "not pending", "already calculated");
    }
    {
        CfbDocument doc = control;
        CalculateRequest bad = good;
        bad.project_finish += 1;
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "is not the latest task finish", "wrong project finish");
    }
    {
        CfbDocument doc = control;
        CalculateRequest bad = good;
        bad.project_duration += 1;
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "is not the critical task's duration", "wrong duration");
    }
    {
        CfbDocument doc = control;
        CalculateRequest bad = good;
        bad.leaves.pop_back();
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "leaf results were given", "missing leaf");
    }
    {
        CfbDocument doc = control;
        CalculateRequest bad = good;
        bad.leaves[0].uid = 99;
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "is for uid 99", "wrong leaf uid");
    }
    {
        CfbDocument doc = control;
        CalculateRequest bad = good;
        bad.leaves[1].total_slack = -4800;
        bad.leaves[1].free_slack = -4800;
        bad.leaves[1].start_slack = -4800;
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "slack the unlinked shape cannot have", "negative slack");
    }
    {
        CfbDocument doc = control;
        CalculateRequest bad = good;
        bad.leaves[1].free_slack = bad.leaves[1].total_slack + 4800;
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "slack the unlinked shape cannot have", "free slack differs");
    }
    {
        CfbDocument doc = control;
        refuses([&] { mppwrite::save_calculate(doc, good, ""); },
                "output path is empty", "empty path");
    }
    {
        /* A constraint on a leaf (type 4, SNET): no measured unlinked pair. */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u16(1, mppwrite::task_row::kConstraintType, 4);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "carries a constraint", "constraint");
    }
    {
        /* A manually scheduled leaf (Fixed2Meta +8 bit 0x80 of entry 4). */
        CfbDocument doc = control;
        std::string const meta_path = "114/TBkndTask/Fixed2Meta";
        Bytes meta = doc.read_stream(meta_path);
        meta[16 + 96 * 4 + 8] |= 0x80;
        doc.replace_stream(meta_path, meta);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "is manually scheduled", "manual leaf");
    }
    {
        /* A progressed leaf: the task-mode whole-file check names it. */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u16(2, mppwrite::task_row::kPercentComplete, 50);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "records progress", "progress");
    }
    {
        /* A leaf with its own calendar. */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(1, 178, 3);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "uses a task calendar", "task calendar");
    }
    {
        CfbDocument doc = control;
        std::string reason;
        mppwrite::CalculateShape shape;
        check(mppwrite::read_calculate_shape(doc, shape, reason),
              "the control reads as the measured shape: " + reason);
        check(shape.leaf_uids.size() == 2, "the shape has two leaves");
    }
    check(!std::filesystem::exists(path), "request refusals leave no file");
}

}  // namespace

int main() {
    try {
        unlinked_pair();
        refused_shapes();
        refused_requests();
    } catch (std::exception const& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << std::endl;
        return 1;
    }
    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
