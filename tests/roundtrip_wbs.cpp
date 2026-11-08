/*
 * roundtrip_wbs.cpp -- W34-4.6: the WBS code writer against Project's own save,
 * plus every refusal the writer offers.
 *
 * WHAT IS PROVEN.  The port opens PROJECT'S CONTROL of wave30-wbs, defines the
 * measured code (prefix "W", level 1, ordered numbers, length 2, separator ".")
 * through core/mppwrite/wbs_code.h exactly as the command does, and compares
 * EVERY stream of the four documents with Project's edit save through
 * save_pair::compare_all: the five streams the writer owns are declared (their
 * measured effect sets must be non-empty) and there is NOT ONE KnownGap -- every
 * unmasked byte of every stream must equal Project's save.  The committed null
 * save masks per-save noise.  This is the whole-file form of brief rule 1 on the
 * pair that measured the command.
 *
 * The structural assertions after the pair are independent of the comparison:
 * the four 6-byte blocks, their order and VarMeta slots, the Task Var2Data size
 * 512 -> 552, 114/Props 88,724 -> 88,748, the two record payloads, the
 * FixedMeta words and no stream outside the five owned ones changed.
 *
 * REFUSALS.  The Project-saved definition itself (stored WBS text), a file with
 * another task population, an empty output path, and the dry run (document
 * untouched, no file left).  Each refusal asserts that the document was not
 * modified and that no output file appeared.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "fixture_guard.h"
#include "save_pair_compare.h"
#include "table.h"
#include "varmeta_edit.h"
#include "wbs_code.h"

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

using mppwrite::CfbDocument;
using mppwrite::VarEntry;

char const* kControl = "wave30-wbs-control.mpp";
char const* kNull = "wave30-wbs-control-null.mpp";
char const* kEdit = "wave30-wbs-code.mpp";
char const* kTaskMeta = "114/TBkndTask/FixedMeta";
char const* kTaskVarMeta = "114/TBkndTask/VarMeta";
char const* kTaskVar2 = "114/TBkndTask/Var2Data";
char const* kProps = "114/Props";

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
    void expected_failure(bool project_truth, std::string const& what) {
        if (project_truth) {
            ++failures_;
            std::cout << "FAIL: expected failure '" << what
                      << "' now PASSES -- make it a normal check\n";
            return;
        }
        ++expected_failures_;
        std::cout << "EXPECTED FAIL: " << what << "\n";
    }
    int report() const {
        std::cout << checks_ << " checks, " << failures_ << " failures, "
                  << expected_failures_ << " expected failures\n";
        return failures_ == 0 ? 0 : 1;
    }

  private:
    int checks_ = 0;
    int failures_ = 0;
    int expected_failures_ = 0;
};

std::string join(std::string const& a, std::string const& b) { return a + "/" + b; }
std::string out_path(std::string const& name) { return join(MPPWRITE_TEST_OUT, name); }

CfbDocument doc_from_data(std::string const& name) {
    return CfbDocument::from_file(join(MPPWRITE_TEST_DATA, name));
}

bool file_exists(std::string const& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
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

void check_only_owned_streams(TestContext& t, CfbDocument const& before,
                              CfbDocument const& after,
                              std::string const& label,
                              std::vector<std::string> const& owned) {
    for (std::string const& path : before.stream_paths()) {
        if (std::find(owned.begin(), owned.end(), path) != owned.end()) {
            continue;
        }
        t.check(after.has_stream(path) &&
                    after.read_stream(path) == before.read_stream(path),
                label + ": unowned stream changed or vanished: " + path);
    }
}

/* The block of the (uid, key) entry, as {offset, payload bytes}. */
struct Block {
    bool found = false;
    std::size_t index = 0;
    std::size_t offset = 0;
    std::vector<std::uint8_t> payload;
};

Block block_of(CfbDocument const& doc, std::uint32_t uid, std::uint32_t key) {
    Block block;
    std::vector<std::uint8_t> const var2 = doc.read_stream(kTaskVar2);
    for (VarEntry const& entry : mppwrite::read_var_entries(doc.read_stream(kTaskVarMeta))) {
        if (entry.mid == uid && entry.field_id == key) {
            block.found = true;
            block.index = entry.index;
            block.offset = entry.offset;
            std::uint32_t const length = u32_at(var2, entry.offset);
            block.payload.assign(var2.begin() + static_cast<std::ptrdiff_t>(entry.offset + 4),
                                 var2.begin() + static_cast<std::ptrdiff_t>(entry.offset + 4 + length));
            return block;
        }
    }
    return block;
}

/*
 * COORDINATES OF THE GROWN 114/Props.  save_pair::compare masks by byte
 * position (control vs null), but this edit GROWS two records in the middle of
 * 114/Props (+20 B and +4 B), so every per-save-noise byte after them (the
 * project GUID is stored twice, the second copy at 2835 in the control) lands
 * 24 bytes later in Project's save and the port's.  Only the coordinates
 * differ.  The control and the null save are therefore given the same two
 * zero-filled insertions at the ends of the two records' payloads -- identical
 * in both, so the mask is exactly the control-vs-null difference, now at the
 * right positions -- and the pair is compared in the edit's coordinates.  The
 * inserted bytes and the two length words are real effects against Project's
 * save and are counted as such.  The record ids, the insertion points and the
 * 24/6 byte lengths are asserted against Project's own save, not assumed.
 */
std::vector<std::uint8_t> grown_props(std::vector<std::uint8_t> const& raw) {
    /* Find both insertion points in the ORIGINAL framing, then insert from the
     * later one back (the length words are Project's to change, not ours). */
    std::vector<std::pair<std::size_t, std::size_t>> insertions; /* at, extra */
    std::size_t at = 16;
    while (at + 12 <= raw.size()) {
        std::uint32_t const length = u32_at(raw, at);
        std::uint32_t const id = u32_at(raw, at + 4);
        if (id == 0x0240138Bu) {
            insertions.emplace_back(at + 12 + length, 20);
        } else if (id == 0x024013BEu) {
            insertions.emplace_back(at + 12 + length, 4);
        }
        at += 12 + length;
    }
    std::sort(insertions.begin(), insertions.end());
    std::vector<std::uint8_t> out = raw;
    for (auto it = insertions.rbegin(); it != insertions.rend(); ++it) {
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(it->first), it->second, 0);
    }
    return out;
}

CfbDocument in_edit_coordinates(CfbDocument doc) {
    doc.replace_stream(kProps, grown_props(doc.read_stream(kProps)));
    return doc;
}

void test_wbs_pair(TestContext& t) {
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kControl));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kNull));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kEdit));

    CfbDocument doc = doc_from_data(kControl);
    CfbDocument const before = doc;
    t.equal(doc.read_stream(kTaskVar2).size(), 512, "pair: the control's Var2Data is 512 B");
    t.equal(doc.read_stream(kProps).size(), 88724, "pair: the control's 114/Props is 88,724 B");

    mppwrite::WbsCodeWrite const written =
        mppwrite::save_wbs_code(doc, out_path("w34-wbs-code.mpp"));
    t.equal(written.blocks_written, 4, "pair: four var blocks");
    t.equal(written.var2_bytes_before, 512, "pair: Var2Data before");
    t.equal(written.var2_bytes_after, 552, "pair: Var2Data after");
    t.equal(written.props_bytes_before, 88724, "pair: 114/Props before");
    t.equal(written.props_bytes_after, 88748, "pair: 114/Props after");

    /* The four blocks, their offsets and payloads (UTF-16LE + NUL). */
    struct Expect {
        std::uint32_t uid;
        std::uint32_t key;
        std::size_t offset;
        std::uint8_t tens;
        std::uint8_t ones;
    };
    std::array<Expect, 4> const expect{{
        {0, mppwrite::kTaskProjectWbsFieldId, 512, '0', '3'},
        {1, mppwrite::kTaskWbsFieldId, 522, '0', '1'},
        {2, mppwrite::kTaskWbsFieldId, 532, '0', '2'},
        {3, mppwrite::kTaskWbsFieldId, 542, '0', '3'}}};
    for (Expect const& e : expect) {
        Block const block = block_of(doc, e.uid, e.key);
        std::string const label = "pair: uid " + std::to_string(e.uid);
        t.check(block.found, label + " has its WBS entry");
        t.equal(block.offset, e.offset, label + " block offset");
        t.check(block.payload ==
                    std::vector<std::uint8_t>{e.tens, 0, e.ones, 0, 0, 0},
                label + " block holds the two-digit number");
    }
    t.equal(block_of(doc, 0, mppwrite::kTaskProjectWbsFieldId).index, 6,
            "pair: the project row's entry sits at slot 6");
    t.equal(block_of(doc, 1, mppwrite::kTaskWbsFieldId).index, 16,
            "pair: task 1's entry sits at slot 16");
    t.equal(block_of(doc, 2, mppwrite::kTaskWbsFieldId).index, 28,
            "pair: task 2's entry sits at slot 28");
    t.equal(block_of(doc, 3, mppwrite::kTaskWbsFieldId).index, 40,
            "pair: task 3's entry sits at slot 40");

    std::vector<std::uint8_t> const meta = doc.read_stream(kTaskVarMeta);
    t.equal(u32_at(meta, 8), 46, "pair: the declared VarMeta count is 46");
    t.equal(u32_at(meta, 20), 552, "pair: VarMeta +0x14 is the Var2Data size");

    /* FixedMeta: +2 field count +1 on the four rows, the two measured bits. */
    std::vector<std::uint8_t> const meta_before = before.read_stream(kTaskMeta);
    std::vector<std::uint8_t> const meta_after = doc.read_stream(kTaskMeta);
    t.equal(meta_after.size(), meta_before.size(), "pair: FixedMeta keeps its length");
    for (std::size_t row = 0; row < 4; ++row) {
        std::size_t const base = 16 + 47 * (3 + row);
        t.equal(u16_at(meta_after, base + 2), u16_at(meta_before, base + 2) + 1u,
                "pair: row " + std::to_string(row) + " field count +1");
        if (row == 0) {
            t.equal(meta_after[base + 40], meta_before[base + 40] | 0x04u,
                    "pair: the project row's +40 gains bit 0x04");
        } else {
            t.equal(meta_after[base + 20], meta_before[base + 20] | 0x80u,
                    "pair: task " + std::to_string(row) + " +20 gains bit 0x80");
        }
    }

    check_only_owned_streams(t, before, doc, "pair",
                             {kTaskMeta, kTaskVarMeta, kTaskVar2, kProps, "Props14"});

    /* The insertion points the coordinate shift assumes, against Project. */
    {
        std::vector<std::uint8_t> const project_props =
            doc_from_data(kEdit).read_stream(kProps);
        std::vector<std::uint8_t> const control_props = before.read_stream(kProps);
        auto record_length = [](std::vector<std::uint8_t> const& props,
                                std::uint32_t id) -> long {
            std::size_t at = 16;
            while (at + 12 <= props.size()) {
                std::uint32_t const length = u32_at(props, at);
                if (u32_at(props, at + 4) == id) {
                    return static_cast<long>(length);
                }
                at += 12 + length;
            }
            return -1;
        };
        t.equal(static_cast<std::size_t>(record_length(control_props, 0x0240138Bu)), 4,
                "pair: the control's 0x0240138b record is 4 B");
        t.equal(static_cast<std::size_t>(record_length(project_props, 0x0240138Bu)), 24,
                "pair: Project's 0x0240138b record is 24 B");
        t.equal(static_cast<std::size_t>(record_length(control_props, 0x024013BEu)), 2,
                "pair: the control's 0x024013be record is 2 B");
        t.equal(static_cast<std::size_t>(record_length(project_props, 0x024013BEu)), 6,
                "pair: Project's 0x024013be record is 6 B");
        t.equal(grown_props(control_props).size(), project_props.size(),
                "pair: the grown control has Project's 114/Props length");
    }

    std::array<char const*, 5> const declared{{
        kTaskMeta, kTaskVarMeta, kTaskVar2, kProps, "Props14"}};
    save_pair::compare_all(
        t, "wbs-code", in_edit_coordinates(doc_from_data(kControl)),
        in_edit_coordinates(doc_from_data(kNull)), doc_from_data(kEdit),
        CfbDocument::from_file(out_path("w34-wbs-code.mpp")), nullptr, 0,
        declared.data(), declared.size());
}

/* R34G4-5 (RC-W5).  The measured numbers "01" "02" "03" are written from the
 * row's UniqueID, but Project numbers an ordered code by the task's position:
 * the pair is only proven where ID == UniqueID == row, every task is top-level
 * (OutlineLevel 1) and none is a summary or a deleted slot.  Each mutation
 * below starts from Project's own control, so everything else is the measured
 * shape and only the named word differs. */
constexpr std::size_t kTaskFixedHeader = 48;
constexpr std::size_t kTaskFixedStride = 202;
constexpr std::size_t kTaskMetaHeader = 16;
constexpr std::size_t kTaskMetaEntry = 47;
constexpr std::size_t kTaskMetaFirstRowEntry = 3; /* entry index = row + 3 */

void put_u32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

void test_numbering_refusals(TestContext& t, std::string const& refused) {
    char const* const fixed = "114/TBkndTask/FixedData";
    auto run = [&](CfbDocument doc, std::string_view needle,
                   std::string const& what) {
        CfbDocument const before = doc;
        std::filesystem::remove(refused);
        expect_error(t, [&] { (void)mppwrite::save_wbs_code(doc, refused); },
                     needle, what);
        t.check(doc.to_bytes() == before.to_bytes(), what + ": doc untouched");
        t.check(!file_exists(refused), what + ": no output file");
        CfbDocument dry = before;
        expect_error(t, [&] { (void)mppwrite::save_wbs_code(dry, refused, true); },
                     needle, what + " (dry run)");
    };
    auto row_at = [](std::size_t row) {
        return kTaskFixedHeader + kTaskFixedStride * row;
    };

    /* IDs 2, 1, 3 against UniqueIDs 1, 2, 3: the row order is not the ID order. */
    {
        CfbDocument doc = doc_from_data(kControl);
        std::vector<std::uint8_t> bytes = doc.read_stream(fixed);
        put_u32(bytes, row_at(1), 2);
        put_u32(bytes, row_at(2), 1);
        doc.replace_stream(fixed, bytes);
        run(std::move(doc), "has ID 2 but UniqueID 1", "rows reordered (ID != UniqueID)");
    }
    /* One task whose ID is not its UniqueID. */
    {
        CfbDocument doc = doc_from_data(kControl);
        std::vector<std::uint8_t> bytes = doc.read_stream(fixed);
        put_u32(bytes, row_at(3), 9);
        doc.replace_stream(fixed, bytes);
        run(std::move(doc), "has ID 9 but UniqueID 3", "one ID that is not its UniqueID");
    }
    /* A nested file: Project's own outline pair (task 1 summarises task 2). */
    run(doc_from_data("wave28-outline-indent.mpp"), "is a summary",
        "a nested three-task file");
    /* The level word alone (the summary byte stays clear). */
    {
        CfbDocument doc = doc_from_data(kControl);
        std::vector<std::uint8_t> bytes = doc.read_stream(fixed);
        bytes[row_at(2) + 172] = 2;
        doc.replace_stream(fixed, bytes);
        run(std::move(doc), "has outline level 2", "a task below the top level");
    }
    /* The stored summary bit alone. */
    {
        CfbDocument doc = doc_from_data(kControl);
        std::vector<std::uint8_t> meta = doc.read_stream(kTaskMeta);
        meta[kTaskMetaHeader + kTaskMetaEntry * (kTaskMetaFirstRowEntry + 2) + 26] |=
            0x80;
        doc.replace_stream(kTaskMeta, meta);
        run(std::move(doc), "is a summary", "the stored summary bit alone");
    }
    /* The has-children word alone. */
    {
        CfbDocument doc = doc_from_data(kControl);
        std::vector<std::uint8_t> bytes = doc.read_stream(fixed);
        bytes[row_at(1) + 140] = 1;
        doc.replace_stream(fixed, bytes);
        run(std::move(doc), "is a summary", "the has-children word alone");
    }
    /* A FixedMeta state other than the pair's (the deleted-row flag). */
    {
        CfbDocument doc = doc_from_data(kControl);
        std::vector<std::uint8_t> meta = doc.read_stream(kTaskMeta);
        meta[kTaskMetaHeader + kTaskMetaEntry * (kTaskMetaFirstRowEntry + 3)] = 0x02;
        doc.replace_stream(kTaskMeta, meta);
        run(std::move(doc), "FixedMeta state", "a task with the deleted-row state");
    }
}

void test_refusals(TestContext& t) {
    std::string const refused = out_path("w34-wbs-refused.mpp");
    std::filesystem::remove(refused);

    /* Project's own definition already stores WBS text. */
    {
        CfbDocument doc = doc_from_data(kEdit);
        CfbDocument const before = doc;
        expect_error(t, [&] { (void)mppwrite::save_wbs_code(doc, refused); },
                     "already stores WBS text", "a file with a stored code");
        t.check(doc.to_bytes() == before.to_bytes(), "a stored code: doc untouched");
        t.check(!file_exists(refused), "a stored code: no output file");
    }
    /* Another task population. */
    {
        CfbDocument doc = doc_from_data("wave30-insert-milestone.mpp");
        CfbDocument const before = doc;
        expect_error(t, [&] { (void)mppwrite::save_wbs_code(doc, refused); },
                     "is not implemented in this port", "another task population");
        t.check(doc.to_bytes() == before.to_bytes(), "population: doc untouched");
        t.check(!file_exists(refused), "population: no output file");
    }
    /* An empty output path. */
    {
        CfbDocument doc = doc_from_data(kControl);
        CfbDocument const before = doc;
        expect_error(t, [&] { (void)mppwrite::save_wbs_code(doc, std::string()); },
                     "output path is empty", "an empty output path");
        t.check(doc.to_bytes() == before.to_bytes(), "empty path: doc untouched");
    }
    test_numbering_refusals(t, refused);
}

void test_dry_run(TestContext& t) {
    std::string const path = out_path("w34-wbs-dry.mpp");
    std::filesystem::remove(path);
    CfbDocument doc = doc_from_data(kControl);
    CfbDocument const before = doc;
    mppwrite::WbsCodeWrite const planned =
        mppwrite::save_wbs_code(doc, path, true);
    t.equal(planned.blocks_written, 4, "dry run: reports four blocks");
    t.equal(planned.var2_bytes_after, 552, "dry run: reports Var2Data 552");
    t.equal(planned.props_bytes_after, 88748, "dry run: reports 114/Props 88,748");
    t.check(doc.to_bytes() == before.to_bytes(), "dry run: the document is untouched");
    t.check(!file_exists(path), "dry run: no file at the requested path");

    /* A dry run on a refused file throws the same refusal. */
    CfbDocument stored = doc_from_data(kEdit);
    expect_error(t, [&] { (void)mppwrite::save_wbs_code(stored, path, true); },
                 "already stores WBS text", "dry run on a stored code");
}

} /* namespace */

int main() {
    try {
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);
        TestContext t;
        test_wbs_pair(t);
        test_refusals(t);
        test_dry_run(t);
        return t.report();
    } catch (std::exception const& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << std::endl;
        return 1;
    }
}
