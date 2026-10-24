/*
 * roundtrip_outline.cpp -- W28-2.1: the measured Indent / Outdent save.
 *
 * CLAIM UNDER TEST.  tests/data/wave28-outline-{indent,summary,outdent}.mpp are
 * Project 2021's own saves of one outline edit each, made from the matching
 * -control.mpp (docs/handoff/wave28-brief.md, the measurement recipe).  This test
 * makes the same edit with mppwrite::write_outline_edits and compares the port's
 * save with Project's, field by field:
 *
 *   FixedData    byte-identical (levels, parent row id, summary flag and roll-up,
 *                restored dates);
 *   FixedMeta    byte-identical (all three entries, including +2 counts);
 *   Fixed2Meta   byte-identical (+88/+95);
 *   Fixed2Data   identical after canonicalising the 16-byte identity GUIDs:
 *                Project allocates fresh GUIDs on every save (the first u32 is a
 *                session counter), so each row's identity is replaced by its row
 *                index and each parent link by the parent's index.  Every other
 *                byte (flags +42, start copy +46..49, +40/+41) must match;
 *   VarMeta      byte-identical (logical entries and offsets: in-place shrink
 *                versus appended block);
 *   Var2Data     byte-identical (block order and the stale bytes Project leaves).
 *
 * The other streams are the control's: this writer owns the six TBkndTask
 * streams only.  Project's own save also rewrites 214/CEdl, Props, Props14,
 * SummaryInformation and the other tables' Fixed2Data (session ids and saved
 * timestamps), and TBkndAssn (the assignment side effect documented in
 * outline.h: a task that becomes a summary loses its assignment row; an outdent
 * re-creates a placeholder).  The test asserts the port leaves all of those at
 * the control's bytes and REPORTS that Project's differ -- Project's reopen of
 * the port's save is the acceptance for TBkndAssn (W28-0.4 precedent).
 *
 * The fourth case applies indent then outdent in ONE call from the indent
 * control and compares with Project's outdent save: Project produced that file
 * by saving the indent result and then outdenting it, so the whole two-save
 * chain (including the Var2Data append offsets 520/536/548/568) must match.
 *
 * Reader checks use the independent core/mpp reader when the top-level build
 * provides it: the port's file must decode to the same outline Project's file
 * does (levels, outline numbers, summary flags, child counts, durations and
 * dates for uids 1..3).
 *
 * Refusals (documented in outline.h) must leave the document untouched:
 * indenting the first top-level row, indenting into an existing summary,
 * outdenting a summary, outdenting a top-level leaf, and an unknown uid.
 *
 * NO CORPUS NEEDED: all inputs are committed fixtures under tests/data.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "outline.h"
#include "table.h"

#ifdef MPPWRITE_HAVE_READER
#include "reader.h"
#endif

using namespace mppwrite;

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif
#ifndef MPPWRITE_FIXTURE_MIRROR
#define MPPWRITE_FIXTURE_MIRROR "core/mppwrite/build/vs-w28/test-out"
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

std::string fixture(std::string const& name) {
    return std::string(MPPWRITE_TEST_DATA) + "/" + name;
}

std::string out_path(std::string const& name) {
    return std::string(MPPWRITE_TEST_OUT) + "/" + name;
}

std::vector<std::uint8_t> read_file(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path);
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

void write_file(std::string const& path, std::vector<std::uint8_t> const& bytes) {
    std::filesystem::path const parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write " + path);
    }
    output.write(reinterpret_cast<char const*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("short write to " + path);
    }
}

std::string task_stream(std::string const& name) {
    return "114/TBkndTask/" + name;
}

std::array<std::string, 6> const kTaskStreams = {
    task_stream("FixedData"), task_stream("FixedMeta"),
    task_stream("Fixed2Data"), task_stream("Fixed2Meta"),
    task_stream("VarMeta"), task_stream("Var2Data")};

bool is_task_stream(std::string const& path) {
    return std::find(kTaskStreams.begin(), kTaskStreams.end(), path) !=
           kTaskStreams.end();
}

std::vector<std::uint8_t> stream_of(CfbDocument const& doc,
                                    std::string const& path) {
    return doc.read_stream(path);
}

std::string describe(std::string const& label, std::string const& path,
                     std::size_t actual, std::size_t expected) {
    return label + ": " + path + " is " + std::to_string(actual) +
           " bytes, expected " + std::to_string(expected);
}

void require_same_streams(TestContext& ctx, std::string const& label,
                          CfbDocument const& expected,
                          CfbDocument const& actual) {
    std::vector<std::string> a = expected.stream_paths();
    std::vector<std::string> b = actual.stream_paths();
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    std::vector<std::string> missing;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                        std::back_inserter(missing));
    std::vector<std::string> extra;
    std::set_difference(b.begin(), b.end(), a.begin(), a.end(),
                        std::back_inserter(extra));
    ctx.check(missing.empty() && extra.empty(),
              label + ": stream sets agree");
}

/*
 * Canonicalise the session identity GUIDs of Fixed2Data: each record's +0..15
 * becomes a canonical id (row index in the low u32) and each parent link
 * (+24..39) becomes the parent's canonical id.  A link to an unknown or empty
 * id is kept verbatim (both measured controls use all-zero for no parent).
 */
std::vector<std::uint8_t> canonical_fixed2(
    std::vector<std::uint8_t> const& fixed2, std::size_t rows) {
    constexpr std::size_t kRecord = 64;
    constexpr std::size_t kRowOffset = 24;
    if (fixed2.size() != kRecord * (rows + 3)) {
        throw std::runtime_error("Fixed2Data is not 64*(rows+3) bytes");
    }
    using Id = std::array<std::uint8_t, 16>;
    std::map<Id, Id> canonical;
    for (std::size_t row = 0; row < rows; ++row) {
        Id id{};
        std::size_t const base = kRecord * (row + 3);
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base),
                  fixed2.begin() + static_cast<std::ptrdiff_t>(base + 16),
                  id.begin());
        bool const empty =
            std::all_of(id.begin(), id.end(),
                        [](std::uint8_t b) { return b == 0; });
        if (!empty) {
            Id replacement{};
            replacement[0] = static_cast<std::uint8_t>(row & 0xFFu);
            replacement[1] = static_cast<std::uint8_t>((row >> 8) & 0xFFu);
            replacement[2] = static_cast<std::uint8_t>((row >> 16) & 0xFFu);
            replacement[3] = static_cast<std::uint8_t>((row >> 24) & 0xFFu);
            canonical.emplace(id, replacement);
        }
    }
    std::vector<std::uint8_t> out = fixed2;
    auto replace_id = [&out](std::size_t at, Id const& id) {
        std::copy(id.begin(), id.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(at));
    };
    for (std::size_t row = 0; row < rows; ++row) {
        std::size_t const base = kRecord * (row + 3);
        Id id{};
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base),
                  fixed2.begin() + static_cast<std::ptrdiff_t>(base + 16),
                  id.begin());
        auto const self = canonical.find(id);
        if (self != canonical.end()) {
            replace_id(base, self->second);
        }
        Id parent{};
        std::copy(fixed2.begin() + static_cast<std::ptrdiff_t>(base + kRowOffset),
                  fixed2.begin() +
                      static_cast<std::ptrdiff_t>(base + kRowOffset + 16),
                  parent.begin());
        auto const found = canonical.find(parent);
        if (found != canonical.end()) {
            replace_id(base + kRowOffset, found->second);
        }
    }
    return out;
}

struct PairCase {
    char const* label;
    char const* control;
    char const* project;
    OutlineEdit edit;
};

std::size_t fixed_meta_entry_for_row(std::vector<std::uint8_t> const& meta,
                                     std::size_t row) {
    std::uint32_t const offset =
        static_cast<std::uint32_t>(task_row::kHeaderSize +
                                   task_row::kStride * row);
    for (std::size_t i = 0; i < (meta.size() - 16) / 47; ++i) {
        std::size_t const base = 16 + i * 47;
        std::uint32_t const stored = static_cast<std::uint32_t>(meta[base + 4]) |
                                     (static_cast<std::uint32_t>(meta[base + 5])
                                      << 8) |
                                     (static_cast<std::uint32_t>(meta[base + 6])
                                      << 16) |
                                     (static_cast<std::uint32_t>(meta[base + 7])
                                      << 24);
        if (stored == offset) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

/*
 * Project rewrites the 16-byte opaque blob at FixedMeta entry +8 on the row it
 * edits (a fresh session token, like the percent-step ids of actuals.h); the
 * port cannot reproduce it and leaves the row's blob at the control's bytes.
 * Every other byte of the edited entry -- in particular the +2 field count and
 * the +26 summary bit -- must equal Project's, and every untouched entry must
 * be the control's.
 */
void check_fixed_meta(TestContext& ctx, std::string const& label,
                      CfbDocument const& control, CfbDocument const& project,
                      CfbDocument const& port, std::size_t edited_row) {
    std::vector<std::uint8_t> const base =
        stream_of(control, task_stream("FixedMeta"));
    std::vector<std::uint8_t> const theirs =
        stream_of(project, task_stream("FixedMeta"));
    std::vector<std::uint8_t> const ours =
        stream_of(port, task_stream("FixedMeta"));
    ctx.equal(ours.size(), theirs.size(), label + ": FixedMeta size");
    ctx.equal(ours.size(), base.size(), label + ": FixedMeta control size");
    if (ours.size() != theirs.size() || ours.size() != base.size() ||
        ours.size() < 16) {
        return;
    }
    std::size_t const edited = fixed_meta_entry_for_row(base, edited_row);
    ctx.check(edited != static_cast<std::size_t>(-1),
              label + ": edited row has a FixedMeta entry");
    if (edited == static_cast<std::size_t>(-1)) {
        return;
    }
    bool header = true;
    for (std::size_t i = 0; i < 16; ++i) {
        header = header && ours[i] == base[i] && ours[i] == theirs[i];
    }
    ctx.check(header, label + ": FixedMeta header equals the pair's");
    std::size_t const entries = (ours.size() - 16) / 47;
    std::size_t edited_differences = 0;
    for (std::size_t entry = 0; entry < entries; ++entry) {
        std::size_t const at = 16 + entry * 47;
        if (entry == edited) {
            for (std::size_t b = 0; b < 47; ++b) {
                if (b >= 8 && b < 24) {
                    continue; /* opaque per-save blob */
                }
                if (ours[at + b] != theirs[at + b]) {
                    ++edited_differences;
                }
            }
            bool blob_kept = true;
            for (std::size_t b = 8; b < 24; ++b) {
                blob_kept = blob_kept && ours[at + b] == base[at + b];
            }
            ctx.check(blob_kept,
                      label + ": edited row's opaque blob is the control's");
        } else {
            for (std::size_t b = 0; b < 47; ++b) {
                if (ours[at + b] != base[at + b]) {
                    ++edited_differences;
                }
            }
        }
    }
    ctx.equal(edited_differences, 0,
              label + ": FixedMeta equals the pair's outside the edited "
                      "row's opaque blob");
}

void apply_and_compare(TestContext& ctx, PairCase const& test) {
    std::string const label = test.label;
    CfbDocument doc = CfbDocument::from_file(fixture(test.control));
    OutlineWriteResult const result =
        write_outline_edits(doc, {test.edit});
    ctx.equal(result.rows_written, 1, label + ": one row written");
    ctx.check(result.warnings.empty(), label + ": no warnings");
    ctx.check(doc.check().empty(), label + ": container check");
    ctx.check(check_table(doc).empty(), label + ": task table check");

    std::string const name = std::string("w28-outline-") + test.label + ".mpp";
    doc.save(out_path(name));
    write_file(std::string(MPPWRITE_FIXTURE_MIRROR) + "/" + name,
               read_file(out_path(name)));

    CfbDocument const control = CfbDocument::from_file(fixture(test.control));
    CfbDocument const project = CfbDocument::from_file(fixture(test.project));
    CfbDocument const port = CfbDocument::from_file(out_path(name));
    require_same_streams(ctx, label, control, port);
    require_same_streams(ctx, label, control, project);

    /* --- the six owned streams ------------------------------------------- */
    std::vector<std::uint8_t> const control_fixed =
        stream_of(control, task_stream("FixedData"));
    std::size_t const rows =
        (control_fixed.size() - task_row::kHeaderSize) / task_row::kStride;

    ctx.check(stream_of(port, task_stream("FixedData")) ==
                  stream_of(project, task_stream("FixedData")),
              label + ": FixedData equals Project's");
    ctx.check(stream_of(port, task_stream("FixedData")) != control_fixed,
              label + ": FixedData actually changed");

    check_fixed_meta(ctx, label, control, project, port, 1);
    ctx.check(stream_of(port, task_stream("Fixed2Meta")) ==
                  stream_of(project, task_stream("Fixed2Meta")),
              label + ": Fixed2Meta equals Project's");
    ctx.check(stream_of(port, task_stream("VarMeta")) ==
                  stream_of(project, task_stream("VarMeta")),
              label + ": VarMeta equals Project's");
    ctx.check(stream_of(port, task_stream("Var2Data")) ==
                  stream_of(project, task_stream("Var2Data")),
              label + ": Var2Data equals Project's");

    std::vector<std::uint8_t> const port_fixed2 =
        stream_of(port, task_stream("Fixed2Data"));
    std::vector<std::uint8_t> const project_fixed2 =
        stream_of(project, task_stream("Fixed2Data"));
    ctx.equal(port_fixed2.size(), project_fixed2.size(),
              label + ": Fixed2Data size");
    if (port_fixed2.size() == project_fixed2.size()) {
        ctx.check(canonical_fixed2(port_fixed2, rows) ==
                      canonical_fixed2(project_fixed2, rows),
                  label + ": Fixed2Data equals Project's after identity "
                          "canonicalisation");
        /* Project really does allocate fresh identities: prove the
         * canonicalisation is not hiding an identical control. */
        ctx.check(canonical_fixed2(port_fixed2, rows) !=
                      canonical_fixed2(stream_of(control, task_stream(
                                                     "Fixed2Data")),
                                       rows),
                  label + ": Fixed2Data outline bytes changed");
    }

    /* --- everything else stays the control's ----------------------------- */
    for (std::string const& path : control.stream_paths()) {
        if (is_task_stream(path)) {
            continue;
        }
        std::vector<std::uint8_t> const ours = stream_of(port, path);
        std::vector<std::uint8_t> const base = stream_of(control, path);
        ctx.check(ours == base, label + ": " + path + " is the control's");
    }
    /* TBkndAssn is Project's own assignment side effect (outline.h).  Report it
     * rather than asserting equality with Project.  Assignment rows are 110
     * bytes with no fixed header (add_delete.h), so the count is size / 110. */
    std::size_t project_assn_rows = 0;
    std::size_t control_assn_rows = 0;
    if (project.has_stream("114/TBkndAssn/FixedData")) {
        project_assn_rows =
            stream_of(project, "114/TBkndAssn/FixedData").size() / 110;
    }
    if (control.has_stream("114/TBkndAssn/FixedData")) {
        control_assn_rows =
            stream_of(control, "114/TBkndAssn/FixedData").size() / 110;
    }
    std::cout << "  " << label << ": TBkndAssn rows control " << control_assn_rows
              << ", Project " << project_assn_rows
              << " (not written; documented in outline.h)\n";

#ifdef MPPWRITE_HAVE_READER
    mpp::Document const project_reader =
        mpp::load_file(fixture(test.project));
    mpp::Document const port_reader =
        mpp::load_file(out_path(name));
    ctx.equal(port_reader.tasks.size(), project_reader.tasks.size(),
              label + ": reader task count");
    auto find_task = [](mpp::Document const& document,
                        std::uint32_t uid) -> mpp::Task const* {
        for (mpp::Task const& task : document.tasks) {
            if (task.uid == uid) {
                return &task;
            }
        }
        return nullptr;
    };
    for (std::uint32_t uid = 1; uid <= 3; ++uid) {
        mpp::Task const* ours = find_task(port_reader, uid);
        mpp::Task const* theirs = find_task(project_reader, uid);
        ctx.check(ours != nullptr && theirs != nullptr,
                  label + ": reader has uid " + std::to_string(uid));
        if (ours == nullptr || theirs == nullptr) {
            continue;
        }
        ctx.equal(ours->outline_level, theirs->outline_level,
                  label + ": uid " + std::to_string(uid) + " outline level");
        ctx.check(ours->outline_number == theirs->outline_number,
                  label + ": uid " + std::to_string(uid) + " outline number");
        ctx.check(ours->summary == theirs->summary,
                  label + ": uid " + std::to_string(uid) + " summary flag");
        ctx.equal(ours->child_count, theirs->child_count,
                  label + ": uid " + std::to_string(uid) + " child count");
        ctx.equal(ours->duration_ticks, theirs->duration_ticks,
                  label + ": uid " + std::to_string(uid) + " duration");
        ctx.equal(ours->duration_format, theirs->duration_format,
                  label + ": uid " + std::to_string(uid) + " duration format");
        ctx.check(ours->start.raw == theirs->start.raw,
                  label + ": uid " + std::to_string(uid) + " start");
        ctx.check(ours->finish.raw == theirs->finish.raw,
                  label + ": uid " + std::to_string(uid) + " finish");
        ctx.check(ours->calculated_start.raw == theirs->calculated_start.raw,
                  label + ": uid " + std::to_string(uid) +
                      " calculated start");
        ctx.check(ours->calculated_finish.raw == theirs->calculated_finish.raw,
                  label + ": uid " + std::to_string(uid) +
                      " calculated finish");
    }
#endif
}

void check_refusal(TestContext& ctx, std::string const& label,
                   std::string const& control, OutlineEdit const& edit,
                   std::string const& why) {
    CfbDocument doc = CfbDocument::from_file(fixture(control));
    OutlineWriteResult const result = write_outline_edits(doc, {edit});
    ctx.equal(result.rows_written, 0, label + ": refused");
    ctx.equal(result.warnings.size(), 1, label + ": one warning (" + why + ")");
    ctx.check(doc.unmodified(), label + ": document untouched");
}

void refusal_cases(TestContext& ctx) {
    OutlineEdit edit;
    edit.uid = 99;
    edit.indent = true;
    edit.summary_duration = 0x2ee0;
    check_refusal(ctx, "unknown uid", "wave28-outline-indent-control.mpp", edit,
                  "uid 99");

    edit.uid = 1;
    check_refusal(ctx, "indent first top-level row",
                  "wave28-outline-indent-control.mpp", edit,
                  "no preceding sibling");

    edit.uid = 1;
    edit.indent = false;
    check_refusal(ctx, "outdent top-level leaf",
                  "wave28-outline-indent-control.mpp", edit,
                  "no parent summary");

    edit.uid = 2;
    check_refusal(ctx, "outdent top-level leaf (second)",
                  "wave28-outline-indent-control.mpp", edit,
                  "no parent summary");

    edit.uid = 1;
    edit.indent = false;
    edit.restored_duration = 0x5dc0;
    edit.restored_duration_format = 0x0007;
    edit.restored_start = 0x3bf212c0;
    edit.restored_finish = 0x3bf627d8;
    check_refusal(ctx, "outdent a summary",
                  "wave28-outline-outdent-control.mpp", edit,
                  "summary move unmeasured");
}

/*
 * Indent then outdent in one call, from the indent control.  Project's outdent
 * result was produced by saving the indent result and outdenting it, so the
 * whole chain -- including where each Var2Data block lands -- must equal it.
 */
void chain_case(TestContext& ctx) {
    OutlineEdit indent;
    indent.uid = 2;
    indent.indent = true;
    indent.summary_duration = 0x2ee0;
    OutlineEdit outdent;
    outdent.uid = 2;
    outdent.indent = false;
    outdent.restored_duration = 0x5dc0;
    outdent.restored_duration_format = 0x0007;
    outdent.restored_start = 0x3bf212c0;
    outdent.restored_finish = 0x3bf627d8;

    CfbDocument doc =
        CfbDocument::from_file(fixture("wave28-outline-indent-control.mpp"));
    OutlineWriteResult const result =
        write_outline_edits(doc, {indent, outdent});
    ctx.equal(result.rows_written, 2, "chain: two rows written");
    ctx.check(result.warnings.empty(), "chain: no warnings");
    ctx.check(check_table(doc).empty(), "chain: task table check");
    std::string const name = "w28-outline-chain.mpp";
    doc.save(out_path(name));
    write_file(std::string(MPPWRITE_FIXTURE_MIRROR) + "/" + name,
               read_file(out_path(name)));

    CfbDocument const control =
        CfbDocument::from_file(fixture("wave28-outline-indent-control.mpp"));
    CfbDocument const project =
        CfbDocument::from_file(fixture("wave28-outline-outdent.mpp"));
    CfbDocument const port = CfbDocument::from_file(out_path(name));

    std::vector<std::uint8_t> const control_fixed =
        stream_of(control, task_stream("FixedData"));
    std::size_t const rows =
        (control_fixed.size() - task_row::kHeaderSize) / task_row::kStride;
    for (std::string const& stream : kTaskStreams) {
        if (stream == task_stream("FixedMeta")) {
            continue;
        }
        if (stream == task_stream("Fixed2Data")) {
            ctx.check(canonical_fixed2(stream_of(port, stream), rows) ==
                          canonical_fixed2(stream_of(project, stream), rows),
                      "chain: Fixed2Data equals the outdent result after "
                      "canonicalisation");
        } else {
            ctx.check(stream_of(port, stream) == stream_of(project, stream),
                      "chain: " + stream + " equals the outdent result");
        }
    }
    check_fixed_meta(ctx, "chain", control, project, port, 1);
    ctx.check(stream_of(port, task_stream("FixedData")) ==
                  stream_of(control, task_stream("FixedData")),
              "chain: FixedData returns to the control after outdent");
    for (std::string const& path : control.stream_paths()) {
        if (path.rfind("114/TBkndTask/", 0) == 0 ||
            path.rfind("114/TBkndAssn/", 0) == 0) {
            continue;
        }
        ctx.check(stream_of(port, path) == stream_of(control, path),
                  "chain: " + path + " is the control's");
    }

    OutlineMarker const marker = read_outline_marker(
        CfbDocument::from_file(out_path(name)), 1);
    ctx.check(marker.present, "chain: the restored leaf keeps its marker");
    ctx.equal(marker.duration_ticks, 0x5dc0, "chain: marker duration");
    ctx.equal(marker.duration_format, 0x0007, "chain: marker format");
}

void marker_case(TestContext& ctx) {
    CfbDocument const control =
        CfbDocument::from_file(fixture("wave28-outline-outdent-control.mpp"));
    OutlineMarker const marker = read_outline_marker(control, 1);
    ctx.check(marker.present, "marker: uid 1 has one");
    ctx.equal(marker.duration_ticks, 0x5dc0, "marker: duration 0x5dc0");
    ctx.equal(marker.duration_format, 0x0007, "marker: format 7");
    OutlineMarker const leaf = read_outline_marker(control, 2);
    ctx.check(!leaf.present, "marker: uid 2 (leaf) has none");

    std::vector<OutlineRow> rows;
    std::string error;
    ctx.check(read_outline_rows(control, "114/TBkndTask", rows, error),
              "rows: decode succeeds");
    ctx.equal(rows.size(), 4, "rows: project plus three tasks");
    if (rows.size() == 4) {
        ctx.equal(rows[0].uid, 0, "rows: project uid 0");
        ctx.check(rows[1].parent_row == 0, "rows: uid 1 is top-level");
        ctx.check(rows[2].parent_row == 1, "rows: uid 2's parent is uid 1");
        ctx.check(rows[3].parent_row == 0, "rows: uid 3 is top-level");
        ctx.equal(rows[1].child_rows.size(), 1, "rows: uid 1 has one child");
        ctx.equal(rows[1].stored_child_ids.size(), 1,
                  "rows: uid 1's stored child list");
        if (rows[1].stored_child_ids.size() == 1) {
            ctx.equal(rows[1].stored_child_ids.front(), rows[2].row_id,
                      "rows: stored child is uid 2's row id");
        }
    }
}

} /* namespace */

int main() {
    TestContext ctx;
    try {
        PairCase const cases[] = {
            {"indent", "wave28-outline-indent-control.mpp",
             "wave28-outline-indent.mpp",
             OutlineEdit{2, true, 0x2ee0, 0, 0, kInvalidDate, kInvalidDate}},
            {"summary", "wave28-outline-summary-control.mpp",
             "wave28-outline-summary.mpp",
             OutlineEdit{2, true, 0x2ee0, 0, 0, kInvalidDate, kInvalidDate}},
            {"outdent", "wave28-outline-outdent-control.mpp",
             "wave28-outline-outdent.mpp",
             OutlineEdit{2, false, 0, 0x5dc0, 0x0007, 0x3bf212c0, 0x3bf627d8}},
        };
        for (PairCase const& test : cases) {
            apply_and_compare(ctx, test);
        }
        chain_case(ctx);
        marker_case(ctx);
        refusal_cases(ctx);
    } catch (std::exception const& error) {
        std::cout << "FAIL: exception: " << error.what() << "\n";
        return 1;
    }
    return ctx.report();
}
