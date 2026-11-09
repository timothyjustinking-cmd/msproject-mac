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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

/* A leaf whose Duration changed also changes its placeholder assignment's planned work (an
 * IEEE double at TBkndAssn +20 of 110-byte rows, ticks x 100), as Project saves it. */
void set_planned_work(CfbDocument& doc, std::uint32_t task_uid, std::uint32_t ticks) {
    std::string const stream = "114/TBkndAssn/FixedData";
    Bytes assn = doc.read_stream(stream);
    for (std::size_t base = 0; base + 110 <= assn.size(); base += 110) {
        std::uint32_t const uid = static_cast<std::uint32_t>(assn[base + 4]) |
                                  (static_cast<std::uint32_t>(assn[base + 5]) << 8) |
                                  (static_cast<std::uint32_t>(assn[base + 6]) << 16) |
                                  (static_cast<std::uint32_t>(assn[base + 7]) << 24);
        if (uid == task_uid) {
            double const work = static_cast<double>(ticks) * 100.0;
            std::memcpy(&assn[base + 20], &work, sizeof work);
        }
    }
    doc.replace_stream(stream, assn);
}

/* A VarMeta entry (uid, Var2Data offset 0, key) added to a table, as a stored variable
 * record would be; the checks under test read the keys, not the blocks. */
void add_var_key(CfbDocument& doc, std::string const& table, std::uint32_t uid,
                 std::uint32_t key) {
    std::string const stream = "114/" + table + "/VarMeta";
    Bytes meta = doc.read_stream(stream);
    auto const put = [&meta](std::size_t at, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            meta[at + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
        }
    };
    std::uint32_t const count = static_cast<std::uint32_t>(meta[8]) |
                                (static_cast<std::uint32_t>(meta[9]) << 8) |
                                (static_cast<std::uint32_t>(meta[10]) << 16) |
                                (static_cast<std::uint32_t>(meta[11]) << 24);
    put(8, count + 1);
    std::size_t const at = meta.size();
    meta.resize(at + 12, 0);
    put(at, uid);
    put(at + 4, 0);
    put(at + 8, key);
    doc.replace_stream(stream, meta);
}

/* Rename a 114/Props record's id (the third byte stays 0x40), which removes the record for
 * every reader that looks it up by id.  Chain: [u32 length][u32 id][u32 kind][payload]. */
void rename_props_record(CfbDocument& doc, std::uint32_t id, std::uint32_t renamed) {
    Bytes props = doc.read_stream("114/Props");
    auto const get = [&props](std::size_t at) {
        return static_cast<std::uint32_t>(props[at]) |
               (static_cast<std::uint32_t>(props[at + 1]) << 8) |
               (static_cast<std::uint32_t>(props[at + 2]) << 16) |
               (static_cast<std::uint32_t>(props[at + 3]) << 24);
    };
    bool found = false;
    for (std::size_t at = 16; at + 12 <= props.size() && (get(at + 4) & 0x00FF0000u) == 0x00400000u;
         at += 12 + get(at)) {
        if (get(at + 4) == id) {
            for (std::size_t i = 0; i < 4; ++i) {
                props[at + 4 + i] = static_cast<std::uint8_t>((renamed >> (8 * i)) & 0xFFu);
            }
            found = true;
        }
    }
    check(found, "the Props record to rename exists");
    doc.replace_stream("114/Props", props);
}

/* Set the kind word of the 114/Props record `id`. */
void set_props_kind(CfbDocument& doc, std::uint32_t id, std::uint32_t kind) {
    Bytes props = doc.read_stream("114/Props");
    auto const get = [&props](std::size_t at) {
        return static_cast<std::uint32_t>(props[at]) |
               (static_cast<std::uint32_t>(props[at + 1]) << 8) |
               (static_cast<std::uint32_t>(props[at + 2]) << 16) |
               (static_cast<std::uint32_t>(props[at + 3]) << 24);
    };
    bool found = false;
    for (std::size_t at = 16; at + 12 <= props.size() && (get(at + 4) & 0x00FF0000u) == 0x00400000u;
         at += 12 + get(at)) {
        if (get(at + 4) == id) {
            for (std::size_t i = 0; i < 4; ++i) {
                props[at + 8 + i] = static_cast<std::uint8_t>((kind >> (8 * i)) & 0xFFu);
            }
            found = true;
        }
    }
    check(found, "the Props record whose kind is set exists");
    doc.replace_stream("114/Props", props);
}

/* Overwrite the payload of the 114/Props record `id` (same length). */
void set_props_payload(CfbDocument& doc, std::uint32_t id, Bytes const& payload) {
    Bytes props = doc.read_stream("114/Props");
    auto const get = [&props](std::size_t at) {
        return static_cast<std::uint32_t>(props[at]) |
               (static_cast<std::uint32_t>(props[at + 1]) << 8) |
               (static_cast<std::uint32_t>(props[at + 2]) << 16) |
               (static_cast<std::uint32_t>(props[at + 3]) << 24);
    };
    bool found = false;
    for (std::size_t at = 16; at + 12 <= props.size() && (get(at + 4) & 0x00FF0000u) == 0x00400000u;
         at += 12 + get(at)) {
        if (get(at + 4) == id && get(at) == payload.size()) {
            std::copy(payload.begin(), payload.end(), props.begin() + at + 12);
            found = true;
        }
    }
    check(found, "the Props record to overwrite exists");
    doc.replace_stream("114/Props", props);
}

/* The 114/Props framed records as (offset of the length word, id, length), and where the
 * opaque tail starts. */
struct PropsFrame {
    std::size_t at;
    std::uint32_t id;
    std::uint32_t length;
};

std::vector<PropsFrame> props_frames(Bytes const& props, std::size_t& tail_start) {
    auto const get = [&props](std::size_t at) {
        return static_cast<std::uint32_t>(props[at]) |
               (static_cast<std::uint32_t>(props[at + 1]) << 8) |
               (static_cast<std::uint32_t>(props[at + 2]) << 16) |
               (static_cast<std::uint32_t>(props[at + 3]) << 24);
    };
    std::vector<PropsFrame> frames;
    std::size_t at = 16;
    while (at + 12 <= props.size() && (get(at + 4) & 0x00FF0000u) == 0x00400000u) {
        frames.push_back({at, get(at + 4), get(at)});
        at += 12 + get(at);
    }
    tail_start = at;
    return frames;
}

Bytes file_bytes(std::string const& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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
             /* W35b-4.G1-10 (R35G1-10): the refusal names the Calculate Project pair, not the
              * task-mode pair the shared check was written for. */
             {"linked", "link rows; the measured Calculate Project pair has an empty link table"},
             {"lag", "link rows; the measured Calculate Project pair has an empty link table"},
             {"snet", "link rows; the measured Calculate Project pair has an empty link table"}}) {
        CfbDocument doc = open("wave35-calculate-" + pair + "-control.mpp");
        CfbDocument const project = open("wave35-calculate-" + pair + ".mpp");
        std::string const path = out_path("w35-calculate-" + pair + "-refused.mpp");
        refuses([&] { mppwrite::save_calculate(doc, request_from(project), path); },
                needle, pair);
        check(!std::filesystem::exists(path), pair + ": no file left behind");
    }
    {
        /* W35c-2.8 (R35bG4-2): a refusal the shape check raises as an exception, not returns,
         * names the Calculate Project pair too.  The assignment storage's name is changed in
         * the directory (same length, so the container stays well formed), which leaves the
         * file without 114/TBkndAssn/FixedData. */
        CfbDocument const control = open("wave35-calculate-unlinked-control.mpp");
        CfbDocument const project = open("wave35-calculate-unlinked.mpp");
        Bytes raw = control.to_bytes();
        Bytes const name = {'T', 0, 'B', 0, 'k', 0, 'n', 0, 'd', 0, 'A', 0, 's', 0, 's', 0, 'n', 0};
        std::size_t renamed = 0;
        for (auto at = std::search(raw.begin(), raw.end(), name.begin(), name.end());
             at != raw.end();
             at = std::search(at + 1, raw.end(), name.begin(), name.end())) {
            *(at + static_cast<std::ptrdiff_t>(name.size()) - 2) = 'x';
            ++renamed;
        }
        check(renamed > 0, "the assignment storage's name is found in the directory");
        CfbDocument doc = CfbDocument::from_bytes(raw);
        check(!doc.has_stream("114/TBkndAssn/FixedData"),
              "the renamed file has no assignment FixedData");
        std::string const path = out_path("w35-calculate-no-assn-refused.mpp");
        refuses([&] { mppwrite::save_calculate(doc, request_from(project), path); },
                "the measured Calculate Project pair needs 114/TBkndAssn/FixedData, which this "
                "file does not have",
                "a missing stream names the Calculate Project pair");
        check(!std::filesystem::exists(path), "the missing-stream refusal leaves no file behind");
    }
}

void refused_requests() {
    CfbDocument const control = open("wave35-calculate-unlinked-control.mpp");
    CfbDocument const project = open("wave35-calculate-unlinked.mpp");
    std::string const path = out_path("w35-calculate-request-refused.mpp");
    std::filesystem::remove(path); /* a stale file from an earlier run is not this run's */
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
        /* W35b-3.7 (RW35-R4): slack that is internally consistent (free, start and total
         * equal, not negative) but is not the working time to the project finish. */
        CfbDocument doc = control;
        CalculateRequest bad = good;
        bad.leaves[1].free_slack += 4800;
        bad.leaves[1].start_slack += 4800;
        bad.leaves[1].total_slack += 4800;
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "is not the working time between its finish and the project finish",
                "slack one day too long");
    }
    {
        CfbDocument doc = control;
        CalculateRequest bad = good;
        bad.leaves[1].late_start += 4800;
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "is not the working time between its finish and the project finish",
                "late start that is not the project finish less the duration");
    }
    {
        /* W35b-2.2 (R35G1-5): the unlinked backward pass gives every leaf the project finish as
         * its late finish; a request whose late finish is earlier is the caller's engine
         * disagreeing with the rule, and the writer does not trust it. */
        CfbDocument doc = control;
        mppwrite::CalculateShape shape;
        std::string reason;
        check(mppwrite::read_calculate_shape(doc, shape, reason), "2.2: the control reads");
        std::size_t noncritical = 0;
        std::size_t critical = 0;
        for (std::size_t i = 0; i < good.leaves.size(); ++i) {
            (good.leaves[i].uid == shape.critical_uid ? critical : noncritical) = i;
        }
        CalculateRequest bad = good;
        bad.leaves[noncritical].late_finish -= 0x10000; /* a day earlier, still >= its finish */
        bad.leaves[noncritical].late_start -= 0x10000;
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "late finish before the project finish", "late finish short of the project finish");
        CalculateRequest slack = good;
        slack.leaves[critical].total_slack = slack.leaves[critical].free_slack =
            slack.leaves[critical].start_slack = 4800;
        refuses([&] { mppwrite::save_calculate(doc, slack, path); }, "ends the project, so its slack is 0",
                "critical leaf with slack");
        CalculateRequest late = good;
        late.leaves[critical].late_start += 0x10000;
        refuses([&] { mppwrite::save_calculate(doc, late, path); }, "ends the project, so its slack is 0",
                "critical leaf with a late start after its start");
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
        /* W35b-0.2 (R35G1-1): a Deadline changes the backward pass Project writes. */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(2, mppwrite::task_row::kDeadline, 0x3BF212C0u);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "carries a deadline", "deadline");
    }
    {
        /* W35c-2.3 (R35bG0-8): the project summary task's Deadline and constraint, which the
         * leaf loops never read. */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(0, mppwrite::task_row::kDeadline, 0x3BFD27D8u);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "the project summary task carries a deadline", "project row deadline");
        CfbDocument constrained = control;
        TaskTable crows = TaskTable::from_document(constrained);
        crows.set_u16(0, mppwrite::task_row::kConstraintType, 4);
        crows.store_into(constrained);
        refuses([&] { mppwrite::save_calculate(constrained, good, path); },
                "the project summary task carries a constraint", "project row constraint");
    }
    {
        /* W35b-0.3 (R35G1-2) case 2: the recalculation would shorten the project. */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(0, mppwrite::task_row::kFinish, 0x3BFE27D8u);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "would shorten the project", "shortened project");
    }
    {
        /* W35b-0.3 case 3: a leaf that does not start at the project start (the
         * writer would stamp Fixed2Data +46 with the project start). */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(2, mppwrite::task_row::kStart, 0x3BF312C0u);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "starts at a date other than the project start", "leaf start");
    }
    {
        /* W35b-0.3 case 1: leaf 2 is lengthened past leaf 1, so the critical path
         * moves from leaf 1 to leaf 2.  The request is consistent with the file (the
         * latest finish, the critical duration, slack from the finishes), so only the
         * critical-flag check can refuse it. */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(2, mppwrite::task_row::kDuration, 72000);
        rows.set_u32(2, mppwrite::task_row::kFinish, 0x3C0427D8u);
        rows.store_into(doc);
        set_planned_work(doc, 2, 72000);
        CalculateRequest moved = good;
        moved.project_finish = 0x3C0427D8u;
        moved.project_duration = 72000;
        moved.leaves[0].late_start = rows.u32(1, mppwrite::task_row::kStart);
        moved.leaves[0].late_finish = 0x3C0427D8u;
        moved.leaves[0].free_slack = moved.leaves[0].start_slack = moved.leaves[0].total_slack =
            24000;
        moved.leaves[1].late_start = rows.u32(2, mppwrite::task_row::kStart);
        moved.leaves[1].late_finish = 0x3C0427D8u;
        moved.leaves[1].free_slack = moved.leaves[1].start_slack = moved.leaves[1].total_slack = 0;
        refuses([&] { mppwrite::save_calculate(doc, moved, path); },
                "moves the critical path", "critical path moves");
    }
    /* W35b-0.5 (R35G1-3): the parameters a user can vary that no pair measured.  The pair has
     * no resource row beyond Project's placeholder, the Standard calendar with no block of its
     * own, active tasks with the measured metadata flags, and no baseline or other stored
     * variable record. */
    {
        CfbDocument doc = control;
        Bytes rsc = doc.read_stream("114/TBkndRsc/FixedData");
        rsc.resize(rsc.size() + 172, 0);
        doc.replace_stream("114/TBkndRsc/FixedData", rsc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); }, "resource table has",
                "resource row beyond the placeholder");
    }
    {
        CfbDocument doc = control;
        Bytes cal = doc.read_stream("114/TBkndCal/FixedData");
        cal.resize(cal.size() + 12, 0);
        doc.replace_stream("114/TBkndCal/FixedData", cal);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "is not the measured Standard calendar", "extra calendar row");
    }
    {
        CfbDocument doc = control;
        Bytes name = doc.read_stream("114/TBkndCal/Var2Data");
        name[4] = 'X';
        doc.replace_stream("114/TBkndCal/Var2Data", name);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "is not the measured Standard calendar", "calendar name");
    }
    {
        CfbDocument doc = control;
        add_var_key(doc, "TBkndCal", 1, 0x0d400008u);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "is not the measured Standard calendar", "calendar working-times block");
    }
    {
        CfbDocument doc = control;
        add_var_key(doc, "TBkndTask", 1, 0x0b40001bu);
        refuses([&] { mppwrite::save_calculate(doc, good, path); }, "carries a baseline",
                "task baseline");
    }
    {
        CfbDocument doc = control;
        add_var_key(doc, "TBkndAssn", 1, 0x0f400010u);
        refuses([&] { mppwrite::save_calculate(doc, good, path); }, "carries a baseline",
                "assignment baseline");
    }
    {
        CfbDocument doc = control;
        add_var_key(doc, "TBkndRsc", 0, 0x0c40000fu);
        refuses([&] { mppwrite::save_calculate(doc, good, path); }, "carries a baseline",
                "resource baseline");
    }
    {
        CfbDocument doc = control;
        add_var_key(doc, "TBkndTask", 2, 0x0b400080u);
        refuses([&] { mppwrite::save_calculate(doc, good, path); }, "stores variable record",
                "task type block");
    }
    {
        CfbDocument doc = control;
        Bytes meta = doc.read_stream("114/TBkndTask/Fixed2Meta");
        meta[16 + 5 * 96 + 8] = static_cast<std::uint8_t>(meta[16 + 5 * 96 + 8] & ~0x40u);
        doc.replace_stream("114/TBkndTask/Fixed2Meta", meta);
        refuses([&] { mppwrite::save_calculate(doc, good, path); }, "is inactive",
                "inactive task");
    }
    {
        /* A flag byte of the task's FixedMeta entry: the summary, effort-driven and task-type
         * bits live there (assignment_growth.cpp compares the entry for them). */
        CfbDocument doc = control;
        Bytes meta = doc.read_stream("114/TBkndTask/FixedMeta");
        meta[16 + 5 * 47 + 21] = 0x01;
        doc.replace_stream("114/TBkndTask/FixedMeta", meta);
        refuses([&] { mppwrite::save_calculate(doc, good, path); }, "metadata flags differ",
                "task metadata flag byte");
    }
    {
        /* W35b-0.4 (RW35-W2): the pending recalculation must have been caused by a
         * Duration change alone.  Leaf 2's Duration is raised and its Finish is left
         * where it was (a Duration edit Project has not yet moved the early dates for). */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(2, mppwrite::task_row::kDuration, 19200);
        rows.store_into(doc);
        set_planned_work(doc, 2, 19200);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "is not its start plus its duration", "leaf duration without its finish");
    }
    {
        /* A leaf Finish edit: the Finish moves a day, the Duration does not. */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(2, mppwrite::task_row::kFinish, 0x3BF527D8u);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "is not its start plus its duration", "leaf finish edit");
    }
    {
        /* A Start edit that moved the project start and both leaf starts to the next day
         * (so the 0.3 start check passes) and left the Finish dates where they were. */
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        for (std::size_t row = 0; row < 3; ++row) {
            rows.set_u32(row, mppwrite::task_row::kStart, 0x3BF312C0u);
        }
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "is not its start plus its duration", "leaf start edit");
    }
    {
        /* W35c-2.2 (R35bG0-7): the leaf's second date copy and Remaining Duration equal its
         * Start, Finish and Duration in the control.  Each is moved alone (the first-copy
         * relations stay as they were), so only the new guard can refuse. */
        CfbDocument start2 = control;
        TaskTable rows = TaskTable::from_document(start2);
        rows.set_u32(2, mppwrite::task_row::kStart2, 0x3BF312C0u);
        rows.store_into(start2);
        refuses([&] { mppwrite::save_calculate(start2, good, path); },
                "second date copy (+104/+108) differs", "2.2: leaf Start2 moved");
        CfbDocument finish2 = control;
        TaskTable rows_f = TaskTable::from_document(finish2);
        rows_f.set_u32(1, mppwrite::task_row::kFinish2, 0x3BF527D8u);
        rows_f.store_into(finish2);
        refuses([&] { mppwrite::save_calculate(finish2, good, path); },
                "second date copy (+104/+108) differs", "2.2: leaf Finish2 moved");
        CfbDocument remaining = control;
        TaskTable rows_r = TaskTable::from_document(remaining);
        rows_r.set_u32(2, mppwrite::task_row::kRemainingDuration, 4800);
        rows_r.store_into(remaining);
        refuses([&] { mppwrite::save_calculate(remaining, good, path); },
                "remaining duration (+88) differs from its duration", "2.2: leaf remaining duration");
    }
    {
        /* W35c-2.2 (R35bG0-7): FixedMeta bytes 8-11 of a leaf entry (e3 ?? fd fe) are pinned,
         * byte 9 takes the control's two values (ff on the lengthened leaf, bf on the other),
         * and bytes 12-15 are the per-file stamp the pin leaves free. */
        auto meta_case = [&](std::size_t entry, std::size_t byte, std::uint8_t value) {
            CfbDocument doc = control;
            Bytes meta = doc.read_stream("114/TBkndTask/FixedMeta");
            meta[16 + entry * 47 + byte] = value;
            doc.replace_stream("114/TBkndTask/FixedMeta", meta);
            return doc;
        };
        mppwrite::CalculateShape shape;
        std::string reason;
        for (std::size_t entry : {4u, 5u}) {
            for (std::size_t byte : {8u, 10u, 11u}) {
                CfbDocument broken = meta_case(entry, byte, 0x00);
                refuses([&] { mppwrite::save_calculate(broken, good, path); },
                        "FixedMeta metadata flags differ from the measured entry at byte " +
                            std::to_string(byte),
                        "2.2: leaf FixedMeta byte " + std::to_string(byte));
            }
            CfbDocument nine = meta_case(entry, 9, 0x00);
            refuses([&] { mppwrite::save_calculate(nine, good, path); },
                    "FixedMeta metadata flags differ from the measured entry at byte 9",
                    "2.2: leaf FixedMeta byte 9");
            check(mppwrite::read_calculate_shape(meta_case(entry, 9, 0xBF), shape, reason) &&
                      mppwrite::read_calculate_shape(meta_case(entry, 9, 0xFF), shape, reason),
                  "2.2: byte 9 takes 0xbf or 0xff: " + reason);
            check(mppwrite::read_calculate_shape(meta_case(entry, 12, 0x55), shape, reason),
                  "2.2: the per-file stamp at byte 12 is free: " + reason);
        }
    }
    {
        /* W35c-0.2 (R35bG0-1): ONLY the Props project start (0x02400002) moves, by a week
         * (Sunday 21:00 to the next Sunday 21:00), under manual calculation.  The leaves,
         * row 0 and every date relation are untouched, so the 0.3 and 0.4 checks pass and
         * only the Props relation can refuse.  The writer would have stamped Fixed2Data +46
         * with the moved start and cleared the pending flag. */
        CfbDocument doc = control;
        mppwrite::CalculateShape shape;
        std::string reason;
        check(mppwrite::read_calculate_shape(doc, shape, reason), "0.2: control reads: " + reason);
        std::uint32_t const moved = shape.props_project_start + (7u << 16);
        set_props_payload(doc, 0x02400002u,
                          Bytes{static_cast<std::uint8_t>(moved & 0xFFu),
                                static_cast<std::uint8_t>((moved >> 8) & 0xFFu),
                                static_cast<std::uint8_t>((moved >> 16) & 0xFFu),
                                static_cast<std::uint8_t>((moved >> 24) & 0xFFu)});
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "project start in 114/Props", "0.2: only the Props project start moved a week");
        /* One day later (Monday 21:00 maps to Tuesday 08:00), still not the leaves' start. */
        std::uint32_t const next_day = shape.props_project_start + (1u << 16);
        doc = control;
        set_props_payload(doc, 0x02400002u,
                          Bytes{static_cast<std::uint8_t>(next_day & 0xFFu),
                                static_cast<std::uint8_t>((next_day >> 8) & 0xFFu),
                                static_cast<std::uint8_t>((next_day >> 16) & 0xFFu),
                                static_cast<std::uint8_t>((next_day >> 24) & 0xFFu)});
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "project start in 114/Props", "0.2: only the Props project start moved a day");
    }
    {
        /* W35c-1.5 (R35bG0-3): the finish-position guard cannot tell one Duration edit from two
         * (the old critical leaf shortened below the stored finish, the other lengthened past
         * it).  Every date relation is the control's; only the stored Free Slack (+74), the
         * before-state manual calculation leaves behind, differs. */
        struct Case {
            std::size_t row;
            std::uint32_t free_slack;
            char const* what;
        };
        Case const cases[] = {
            {2, 0, "1.5: the leaf now before the finish was critical (two Duration edits)"},
            {1, 9600, "1.5: the leaf now ending the project was not critical"},
            {2, 4800, "1.5: the other leaf's finish moved as well"},
        };
        for (Case const& one : cases) {
            CfbDocument doc = control;
            TaskTable rows = TaskTable::from_document(doc);
            rows.set_u32(one.row, mppwrite::task_row::kFreeSlack, one.free_slack);
            rows.store_into(doc);
            refuses([&] { mppwrite::save_calculate(doc, good, path); }, "stored free slack (+74)",
                    one.what);
        }
    }
    {
        /* W35c-0.3 (R35bG0-2): 114/Props is pinned to the measured control's.  Every record
         * the save does not write (all but the finish, the pending flag, the start the 0.2
         * relation covers, and the two per-save GUIDs) is refused when one payload byte
         * changes: the calculation options (multiple critical paths, slack limit, minutes per
         * day and week, schedule from) are among them, and no pair varies them. */
        std::size_t tail_start = 0;
        Bytes const props = control.read_stream("114/Props");
        std::vector<PropsFrame> const frames = props_frames(props, tail_start);
        check(frames.size() == 161, "0.3: the control's Props holds 161 framed records");
        std::size_t flipped = 0;
        for (PropsFrame const& frame : frames) {
            bool const written = frame.id == 0x02400003u || frame.id == 0x02400024u ||
                                 frame.id == 0x02400002u || frame.id == 0x02400029u ||
                                 frame.id == 0x024013C2u;
            if (written || frame.length == 0) {
                continue;
            }
            Bytes changed = props;
            changed[frame.at + 12] ^= 0x01;
            CfbDocument doc = control;
            doc.replace_stream("114/Props", changed);
            char needle[96];
            std::snprintf(needle, sizeof needle, "114/Props record 0x%08X differs from the measured control",
                          frame.id);
            refuses([&] { mppwrite::save_calculate(doc, good, path); }, needle,
                    std::string("0.3: one byte of a non-written Props record flipped, ") + needle);
            ++flipped;
        }
        check(flipped > 100, "0.3: the flip covered the pinned records");
        /* The records the save writes, or the 0.2 relation covers, are not payload-pinned:
         * a changed GUID is per-save noise and is accepted. */
        for (PropsFrame const& frame : frames) {
            if (frame.id == 0x02400029u || frame.id == 0x024013C2u) {
                Bytes changed = props;
                changed[frame.at + 12] ^= 0x01;
                CfbDocument doc = control;
                doc.replace_stream("114/Props", changed);
                mppwrite::CalculateShape shape;
                std::string reason;
                check(mppwrite::read_calculate_shape(doc, shape, reason),
                      "0.3: a per-save GUID record changed is accepted: " + reason);
            }
        }
        /* The record's kind is pinned as well as its payload. */
        {
            Bytes changed = props;
            changed[frames[10].at + 8] ^= 0x01;
            CfbDocument doc = control;
            doc.replace_stream("114/Props", changed);
            refuses([&] { mppwrite::save_calculate(doc, good, path); },
                    "differs from the measured control", "0.3: a Props record kind changed");
        }
        /* W35d-1.4 (R35cG0-3): the pin also holds per-file identity records, the project title
         * (0x02400008) among them, so a project saved under another title is refused here.
         * The text says so: it names "a per-file value" and that Calculate Project is enabled
         * for the measured control's values only, and it does not call the record an option. */
        std::size_t titles = 0;
        for (PropsFrame const& frame : frames) {
            if (frame.id != 0x02400008u) {
                continue;
            }
            Bytes changed = props;
            changed[frame.at + 12] ^= 0x01;
            CfbDocument doc = control;
            doc.replace_stream("114/Props", changed);
            std::string message;
            try {
                mppwrite::save_calculate(doc, good, path);
            } catch (std::exception const& failure) {
                message = failure.what();
            }
            check(message.find("114/Props record 0x02400008 differs from the measured control") !=
                      std::string::npos,
                  "1.4: a changed project title is refused by the Props pin");
            check(message.find("a per-file value") != std::string::npos &&
                      message.find("enabled for the measured control's values only") !=
                          std::string::npos,
                  "1.4: the refusal names a per-file value and the control-only scope");
            check(message.find("calculation options") == std::string::npos,
                  "1.4: the refusal does not call the record a calculation option");
            ++titles;
        }
        check(titles == 1, "1.4: the control's Props holds one project title record");
        /* W35d-0.3 (R35cG0-1): the three branches of check_props_pin that no payload flip can
         * reach, each with a needle only its own branch produces (the generic "differs from
         * the measured control" text is shared with the record checks, so it is not used).
         * An option Project stores only when it is not the default shows up as a record added
         * or dropped, or a changed header word, not as a payload change. */
        for (std::size_t word : {8u, 12u}) {
            Bytes changed = props;
            changed[word] ^= 0x01;
            CfbDocument doc = control;
            doc.replace_stream("114/Props", changed);
            refuses([&] { mppwrite::save_calculate(doc, good, path); },
                    "114/Props header differs from the measured control",
                    "0.3: Props header word +" + std::to_string(word) + " changed");
        }
        {
            /* Fewer records: the last framed record is cut out, so the tail follows the 160th
             * and only the record-count check can refuse (the tail keeps its length and hash). */
            PropsFrame const& last = frames.back();
            Bytes changed(props.begin(), props.begin() + static_cast<std::ptrdiff_t>(last.at));
            changed.insert(changed.end(), props.begin() + static_cast<std::ptrdiff_t>(tail_start),
                           props.end());
            CfbDocument doc = control;
            doc.replace_stream("114/Props", changed);
            refuses([&] { mppwrite::save_calculate(doc, good, path); },
                    "114/Props holds 160 records, the measured control holds 161",
                    "0.3: the last Props record removed");
        }
        {
            /* More records: one 2-byte record appended after the 161st (length, id, kind, two
             * payload bytes) and before the tail.  Only the past-the-table check refuses; the
             * table is indexed by it, so without the check the read runs off its end. */
            Bytes extra;
            auto const put = [&extra](std::uint32_t value) {
                for (int i = 0; i < 4; ++i) {
                    extra.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
                }
            };
            put(2);
            put(0x024013FCu);
            put(1);
            extra.push_back(0x00);
            extra.push_back(0x00);
            Bytes changed(props.begin(), props.begin() + static_cast<std::ptrdiff_t>(tail_start));
            changed.insert(changed.end(), extra.begin(), extra.end());
            changed.insert(changed.end(), props.begin() + static_cast<std::ptrdiff_t>(tail_start),
                           props.end());
            CfbDocument doc = control;
            doc.replace_stream("114/Props", changed);
            refuses([&] { mppwrite::save_calculate(doc, good, path); },
                    "is past the 161 records of the measured control",
                    "0.3: one Props record appended after the 161st");
        }
        /* The opaque tail: one byte flipped near its end is refused; the one Task Var2Data
         * size scalar the save rewrites (tag 0x01000100) is not part of the pin. */
        {
            Bytes changed = props;
            changed[changed.size() - 1] ^= 0x01;
            CfbDocument doc = control;
            doc.replace_stream("114/Props", changed);
            refuses([&] { mppwrite::save_calculate(doc, good, path); },
                    "114/Props tail differs from the measured control", "0.3: a Props tail byte flipped");
        }
        {
            Bytes changed = props;
            std::size_t keys = 0;
            for (std::size_t at = tail_start; at + 16 <= changed.size(); at += 16) {
                if (changed[at + 4] == 0x01 && changed[at + 5] == 0x00 && changed[at + 6] == 0x01 &&
                    changed[at + 7] == 0x00) {
                    changed[at + 12] ^= 0x55;
                    ++keys;
                }
            }
            check(keys == 1, "0.3: the tail holds one Task size scalar");
            CfbDocument doc = control;
            doc.replace_stream("114/Props", changed);
            mppwrite::CalculateShape shape;
            std::string reason;
            check(mppwrite::read_calculate_shape(doc, shape, reason),
                  "0.3: the Task size scalar the save rewrites is accepted: " + reason);
        }
    }
    {
        /* The relation itself, on the measured calendar. */
        auto const maps = [](std::uint32_t from, std::uint32_t want, std::string const& what) {
            std::uint32_t got = 0;
            check(mppwrite::calculate_measured_first_working_start(from, got) && got == want,
                  "0.2 first working start: " + what);
        };
        constexpr int h8 = 8 * 3600;
        constexpr int h21 = 21 * 3600;
        maps(mppwrite::pack_date_ymd(2026, 1, 4, h21), mppwrite::pack_date_ymd(2026, 1, 5, h8),
             "Sunday 21:00 is Monday 08:00 (the measured pair)");
        maps(mppwrite::pack_date_ymd(2026, 1, 10, 0), mppwrite::pack_date_ymd(2026, 1, 12, h8),
             "Saturday is Monday 08:00");
        maps(mppwrite::pack_date_ymd(2026, 1, 9, 17 * 3600), mppwrite::pack_date_ymd(2026, 1, 12, h8),
             "Friday 17:00 is Monday 08:00");
        maps(mppwrite::pack_date_ymd(2026, 1, 5, 7 * 3600), mppwrite::pack_date_ymd(2026, 1, 5, h8),
             "Monday 07:00 is Monday 08:00");
        maps(mppwrite::pack_date_ymd(2026, 1, 5, h8), mppwrite::pack_date_ymd(2026, 1, 5, h8),
             "Monday 08:00 is itself");
        maps(mppwrite::pack_date_ymd(2026, 1, 5, 10 * 3600 + 1800),
             mppwrite::pack_date_ymd(2026, 1, 5, 10 * 3600 + 1800),
             "Monday 10:30 is itself, which no leaf start equals");
        std::uint32_t none = 0;
        check(!mppwrite::calculate_measured_first_working_start(0xFFFFFFFFu, none),
              "0.2 first working start: no date is refused");
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
        /* W35b-2.3 (R35G1-6): a file Project (or this port) already calculated, edited again
         * under manual calculation so the flag is pending once more.  Its project row already
         * holds the five overtime records, so the save would collide on the first append; the
         * command refuses it up front with its own message.  The pending flag is set back and
         * nothing else is changed, so every other guard passes and only this one can fire. */
        CfbDocument calculated = control;
        std::string const first = out_path("w35-calculate-twice-first.mpp");
        mppwrite::save_calculate(calculated, good, first);
        std::filesystem::remove(first);
        set_props_payload(calculated, 0x02400024u, Bytes{1, 0});
        std::string const again = out_path("w35-calculate-twice-second.mpp");
        std::string reason;
        mppwrite::CalculateShape shape;
        check(!mppwrite::read_calculate_shape(calculated, shape, reason), "2.3: refused");
        check(reason.find("has been calculated before") != std::string::npos,
              "2.3: the shape check names the earlier calculation (got: " + reason + ")");
        refuses([&] { mppwrite::save_calculate(calculated, good, again); },
                "has been calculated before", "a file calculated once before");
        check(!std::filesystem::exists(again), "2.3: no file left behind");
    }
    /* W35b-2.4 (R35G1-7): one synthetic mutation per writer guard, each with the guard's own
     * words as the needle, so removing any one guard turns this test red.  Every mutation
     * changes only what its guard reads. */
    {
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(0, mppwrite::task_row::kUniqueId, 7);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); }, "row 0 is not the project row",
                "2.4: row 0 uid");
    }
    {
        /* A leaf's FixedMeta +17/+18 (the automatic row's 0x48/0xef) differs. */
        CfbDocument doc = control;
        Bytes meta = doc.read_stream("114/TBkndTask/FixedMeta");
        meta[16 + 4 * 47 + 17] = 0x00;
        doc.replace_stream("114/TBkndTask/FixedMeta", meta);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "0x48/0xef at FixedMeta +17/+18", "2.4: FixedMeta +17");
        CfbDocument other = control;
        Bytes meta2 = other.read_stream("114/TBkndTask/FixedMeta");
        meta2[16 + 5 * 47 + 18] = 0x00;
        other.replace_stream("114/TBkndTask/FixedMeta", meta2);
        refuses([&] { mppwrite::save_calculate(other, good, path); },
                "0x48/0xef at FixedMeta +17/+18", "2.4: FixedMeta +18");
    }
    {
        /* W35b-4.G1-12 (R35G1-12): the entry is found by its row offset, not by position.  The
         * entries of rows 1 and 2 swap places and the one that is row 2's (now first) loses its
         * 0x48.  A positional read would blame row 1 (the entry now at row 1's position); the
         * lookup names row 2, and the file still reads when the swap alone is made. */
        CfbDocument swapped = control;
        Bytes meta = swapped.read_stream("114/TBkndTask/FixedMeta");
        for (std::size_t i = 0; i < 47; ++i) {
            std::swap(meta[16 + 4 * 47 + i], meta[16 + 5 * 47 + i]);
        }
        swapped.replace_stream("114/TBkndTask/FixedMeta", meta);
        mppwrite::CalculateShape shape;
        std::string reason;
        check(mppwrite::read_calculate_shape(swapped, shape, reason),
              "G1-12: the control with its leaf entries swapped still reads (got: " + reason + ")");
        CfbDocument doc = swapped;
        meta[16 + 4 * 47 + 17] = 0x00; /* row 2's entry, now at position 4 */
        doc.replace_stream("114/TBkndTask/FixedMeta", meta);
        TaskTable const rows = TaskTable::from_document(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "task uid " + std::to_string(rows.uid(2)) +
                    " does not store the automatic row's 0x48/0xef",
                "G1-12: entries are found by row offset");
    }
    {
        /* W35c-2.9 (R35bG4-3): the two sites of the order-varied lookup.  With the leaf entries
         * swapped, row 2's FixedMeta entry is at position 4.  (a) Its Fixed2Meta entry is the
         * one at the same position, so the active bit cleared there is row 2's: a read at the
         * row's own position would blame row 1.  (b) Byte 20 of that FixedMeta entry, which
         * the +17/+18 check does not read, is named for row 2 and not for row 1. */
        CfbDocument swapped = control;
        Bytes meta = swapped.read_stream("114/TBkndTask/FixedMeta");
        for (std::size_t i = 0; i < 47; ++i) {
            std::swap(meta[16 + 4 * 47 + i], meta[16 + 5 * 47 + i]);
        }
        swapped.replace_stream("114/TBkndTask/FixedMeta", meta);
        TaskTable const rows = TaskTable::from_document(swapped);
        {
            CfbDocument doc = swapped;
            Bytes f2meta = doc.read_stream("114/TBkndTask/Fixed2Meta");
            f2meta[16 + 4 * 96 + 8] &= static_cast<std::uint8_t>(~0x40u);
            doc.replace_stream("114/TBkndTask/Fixed2Meta", f2meta);
            refuses([&] { mppwrite::save_calculate(doc, good, path); },
                    "task uid " + std::to_string(rows.uid(2)) + " is inactive",
                    "2.9: Fixed2Meta is read at the FixedMeta entry's index");
        }
        {
            CfbDocument doc = swapped;
            meta[16 + 4 * 47 + 20] = 0x00; /* row 2's entry, now at position 4 */
            doc.replace_stream("114/TBkndTask/FixedMeta", meta);
            refuses([&] { mppwrite::save_calculate(doc, good, path); },
                    "task uid " + std::to_string(rows.uid(2)) +
                        "'s FixedMeta metadata flags differ",
                    "2.9: a FixedMeta byte the +17/+18 check does not read");
        }
    }
    {
        CfbDocument doc = control;
        rename_props_record(doc, 0x02400024u, 0x02400FF4u);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "no 0x02400024 calculation-pending record", "2.4: pending record missing");
    }
    {
        /* The flag stays 01 00 (pending) and the project row's finish already equals the latest
         * leaf finish.  Project would still rewrite a non-critical leaf's slack and late dates
         * (R35G1-11), which no pair measures, so the refusal says that rather than "nothing to
         * calculate".  Project's own result stops at "not pending" and never reaches this
         * guard. */
        CfbDocument doc = control;
        mppwrite::CalculateShape shape;
        std::string reason;
        check(mppwrite::read_calculate_shape(doc, shape, reason), "2.4: the control reads");
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(0, mppwrite::task_row::kFinish, shape.project_finish);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "the pending change does not move the project finish; that recalculation has no "
                "measured pair",
                "2.4: project finish already equals");
    }
    {
        CfbDocument doc = control;
        TaskTable rows = TaskTable::from_document(doc);
        rows.set_u32(1, mppwrite::task_row::kStartAnchor, 0xFFFFFFFFu);
        rows.store_into(doc);
        refuses([&] { mppwrite::save_calculate(doc, good, path); }, "has no start anchor",
                "2.4: start anchor");
    }
    {
        CfbDocument doc = control;
        CalculateRequest bad = good;
        bad.leaves[0].late_start = 0;
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "has a late date before its early date", "2.4: late start before early start");
        CalculateRequest bad_finish = good;
        bad_finish.leaves[1].late_finish = 0;
        refuses([&] { mppwrite::save_calculate(doc, bad_finish, path); },
                "has a late date before its early date", "2.4: late finish before early finish");
    }
    {
        CfbDocument doc = control;
        CalculateRequest bad = good;
        bad.leaves[0].late_finish += 0x10000;
        refuses([&] { mppwrite::save_calculate(doc, bad, path); },
                "has a late finish after the project finish",
                "2.4: late finish after the project finish");
    }
    {
        /* The row count: a task table with a row fewer is refused by the writer's own row-count
         * guard (the file also fails the assignment count, which is why the needle is the
         * guard's). */
        CfbDocument doc = control;
        Bytes fixed = doc.read_stream("114/TBkndTask/FixedData");
        fixed.resize(fixed.size() - 202);
        doc.replace_stream("114/TBkndTask/FixedData", fixed);
        refuses([&] { mppwrite::save_calculate(doc, good, path); },
                "task rows; the measured Calculate Project save has the project row and exactly "
                "two leaf tasks",
                "2.4: row count");
    }
    {
        /* W35b-2.7 (RW35-W3): the leaf stamp is measured for one arrangement only.  A tie (both
         * leaves finish together) and the swapped order (the second leaf ends the project, the
         * first finishes before the stored project finish) are no pair's; the request is the
         * good one, so only the shape guards can refuse. */
        CfbDocument tie = control;
        TaskTable rows = TaskTable::from_document(tie);
        std::uint32_t const d1 = rows.u32(1, mppwrite::task_row::kDuration);
        std::uint32_t const f1 = rows.u32(1, mppwrite::task_row::kFinish);
        std::uint32_t const d2 = rows.u32(2, mppwrite::task_row::kDuration);
        std::uint32_t const f2 = rows.u32(2, mppwrite::task_row::kFinish);
        check(f1 != f2, "2.7: the control's leaves finish apart");
        rows.set_u32(2, mppwrite::task_row::kDuration, d1);
        rows.set_u32(2, mppwrite::task_row::kFinish, f1);
        rows.store_into(tie);
        set_planned_work(tie, 2, d1);
        refuses([&] { mppwrite::save_calculate(tie, good, path); }, "finish together",
                "2.7: equal leaf finishes");

        CfbDocument swapped = control;
        TaskTable swap_rows = TaskTable::from_document(swapped);
        swap_rows.set_u32(1, mppwrite::task_row::kDuration, d2);
        swap_rows.set_u32(1, mppwrite::task_row::kFinish, f2);
        swap_rows.set_u32(2, mppwrite::task_row::kDuration, d1);
        swap_rows.set_u32(2, mppwrite::task_row::kFinish, f1);
        swap_rows.store_into(swapped);
        set_planned_work(swapped, 1, d2);
        set_planned_work(swapped, 2, d1);
        refuses([&] { mppwrite::save_calculate(swapped, good, path); },
                "is not the first leaf row", "2.7: the critical leaf in the second row");
    }
    {
        /* W35b-2.1 (R35G1-4): a Props record the save needs after its appends is missing.  The
         * target already exists (Save over the open file); it must be left byte-identical. */
        CfbDocument doc = control;
        rename_props_record(doc, 0x02400003u, 0x02400FF3u);
        std::string const target = out_path("w35-calculate-atomic-target.mpp");
        std::filesystem::copy_file(fixture_path("wave35-calculate-unlinked-control.mpp"), target,
                                   std::filesystem::copy_options::overwrite_existing);
        Bytes const before = file_bytes(target);
        refuses([&] { mppwrite::save_calculate(doc, good, target); }, "(0x02400003)",
                "project finish record missing");
        check(file_bytes(target) == before, "project finish record missing: target unchanged");
        check(!std::filesystem::exists(target + ".calculate.tmp"),
              "project finish record missing: no scratch file left");
        std::filesystem::remove(target);
    }
    {
        /* The same for a missing project start, which the write needs for Fixed2Data +46. */
        CfbDocument doc = control;
        rename_props_record(doc, 0x02400002u, 0x02400FF2u);
        std::string const target = out_path("w35-calculate-atomic-target2.mpp");
        std::filesystem::copy_file(fixture_path("wave35-calculate-unlinked-control.mpp"), target,
                                   std::filesystem::copy_options::overwrite_existing);
        Bytes const before = file_bytes(target);
        refuses([&] { mppwrite::save_calculate(doc, good, target); }, "(0x02400002)",
                "project start record missing");
        check(file_bytes(target) == before, "project start record missing: target unchanged");
        std::filesystem::remove(target);
    }
    {
        /* W35c-2.4 (R35bG2-2): the pending flag (0x02400024) of another kind.  Before 0.3's
         * Props pin this reached save_props_edit after the five appends, and only the scratch
         * file protected the target; the pin now refuses it up front.  The target already
         * exists and must be byte-identical, with no scratch file left. */
        CfbDocument doc = control;
        set_props_kind(doc, 0x02400024u, 0x0Fu);
        std::string const target = out_path("w35-calculate-atomic-target4.mpp");
        std::filesystem::copy_file(fixture_path("wave35-calculate-unlinked-control.mpp"), target,
                                   std::filesystem::copy_options::overwrite_existing);
        Bytes const before = file_bytes(target);
        refuses([&] { mppwrite::save_calculate(doc, good, target); },
                "differs from the measured control", "pending record of another kind");
        check(file_bytes(target) == before, "pending record of another kind: target unchanged");
        check(!std::filesystem::exists(target + ".calculate.tmp"),
              "pending record of another kind: no scratch file left");
        std::filesystem::remove(target);
    }
    {
        /* W35d-1.3 (R35cG2-1): the scratch chain itself.  Since 2.7 the squat test below stops at
         * the up-front existence check, and no other test reaches a refusal after the first
         * append (the Props pin refuses first), so this uses the test-only fault seam: the save
         * throws after the first append, and after the last, once each has been written to the
         * scratch file.  With the five appends and the Props edit writing `path` instead, the
         * target would hold a partly calculated file; it must be byte-identical, with no scratch
         * file left. */
        for (std::size_t const appends : {std::size_t{1}, std::size_t{3}, std::size_t{5}}) {
            std::string const label = "fault after append " + std::to_string(appends);
            CfbDocument doc = control;
            std::string const target = out_path("w35-calculate-fault-target.mpp");
            std::filesystem::copy_file(fixture_path("wave35-calculate-unlinked-control.mpp"),
                                       target, std::filesystem::copy_options::overwrite_existing);
            Bytes const before = file_bytes(target);
            mppwrite::set_calculate_fault_after_appends(appends);
            bool refused_by_seam = false;
            try {
                mppwrite::save_calculate(doc, good, target);
            } catch (std::exception const& failure) {
                refused_by_seam =
                    std::string(failure.what()).find("injected fault after append " +
                                                     std::to_string(appends)) != std::string::npos;
            }
            mppwrite::set_calculate_fault_after_appends(0);
            check(refused_by_seam, label + ": the seam fires after that append");
            check(file_bytes(target) == before, label + ": target unchanged");
            check(!std::filesystem::exists(target + ".calculate.tmp"),
                  label + ": no scratch file left");
            std::filesystem::remove(target);
        }
        /* The seam is off again: the same save succeeds. */
        CfbDocument doc = control;
        std::string const target = out_path("w35-calculate-fault-target.mpp");
        std::filesystem::copy_file(fixture_path("wave35-calculate-unlinked-control.mpp"), target,
                                   std::filesystem::copy_options::overwrite_existing);
        mppwrite::save_calculate(doc, good, target);
        check(file_bytes(target) != file_bytes(fixture_path("wave35-calculate-unlinked-control.mpp")),
              "the seam off: the save writes the target");
        std::filesystem::remove(target);
    }
    {
        /* W35c-2.4 (R35bG2-2), 2.7 (R35bG2-7): the scratch name is not ours to clear.  A
         * non-empty directory squats on it.  The save is refused up front and neither the
         * existing target nor the squatter is touched; before 2.7 the squatter was deleted
         * first.  This stops at the existence check, so it does NOT reach the chain: that is
         * the fault-seam test above (W35d-1.3, R35cG2-1). */
        CfbDocument doc = control;
        std::string const target = out_path("w35-calculate-atomic-target5.mpp");
        std::filesystem::copy_file(fixture_path("wave35-calculate-unlinked-control.mpp"), target,
                                   std::filesystem::copy_options::overwrite_existing);
        Bytes const before = file_bytes(target);
        std::string const squat = target + ".calculate.tmp";
        std::filesystem::create_directories(squat);
        {
            std::ofstream keep(squat + "/keep.txt");
            keep << "squatter";
        }
        refuses([&] { mppwrite::save_calculate(doc, good, target); },
                "already exists; it is not removed", "the scratch path is taken");
        check(file_bytes(target) == before, "the scratch path is taken: target unchanged");
        check(std::filesystem::exists(squat + "/keep.txt"),
              "the scratch path is taken: the squatter is left alone");
        std::error_code ignored;
        std::filesystem::remove_all(squat, ignored);
        std::filesystem::remove(target);
    }
    {
        /* W35c-2.7 (R35bG2-7): a user's own plain file on the scratch name is refused and kept,
         * not deleted.  The scratch name is then free again and the same save succeeds. */
        CfbDocument doc = control;
        std::string const target = out_path("w35-calculate-atomic-target6.mpp");
        std::string const squat = target + ".calculate.tmp";
        std::filesystem::remove(target);
        std::filesystem::remove(squat);
        {
            std::ofstream keep(squat);
            keep << "a user's own file";
        }
        refuses([&] { mppwrite::save_calculate(doc, good, target); },
                "already exists; it is not removed", "a plain file on the scratch name");
        check(!std::filesystem::exists(target), "a plain file on the scratch name: no target written");
        check(std::filesystem::exists(squat), "a plain file on the scratch name: kept");
        std::filesystem::remove(squat);
        mppwrite::save_calculate(doc, good, target);
        check(std::filesystem::exists(target), "the save succeeds once the scratch name is free");
        check(!std::filesystem::exists(squat), "success leaves no scratch (after the squat)");
        std::filesystem::remove(target);
    }
    {
        /* A success over an existing target replaces it and leaves no scratch file. */
        CfbDocument doc = control;
        std::string const target = out_path("w35-calculate-atomic-target3.mpp");
        std::filesystem::copy_file(fixture_path("wave35-calculate-unlinked-control.mpp"), target,
                                   std::filesystem::copy_options::overwrite_existing);
        mppwrite::save_calculate(doc, good, target);
        check(file_bytes(target) == doc.to_bytes(), "success over an existing target writes it");
        check(!std::filesystem::exists(target + ".calculate.tmp"), "success leaves no scratch");
        std::filesystem::remove(target);
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
