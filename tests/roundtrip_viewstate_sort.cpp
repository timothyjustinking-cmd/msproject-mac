/*
 * roundtrip_viewstate_sort.cpp -- W35-2.1: the persisted sort against Project's
 * own saves, plus every refusal the writer offers.
 *
 * WHAT IS PROVEN.  For each of the three single-key pairs Project measured
 * (Name descending, Name ascending, Duration descending; the Outline option
 * on, Renumber off) the port opens PROJECT'S CONTROL, applies the sort through
 * core/mppwrite/viewstate_sort.h exactly as the command does, and compares
 * EVERY stream of the four documents with Project's edit save through
 * save_pair::compare_all.  The three streams the sort owns are declared (their
 * measured effect sets must be non-empty) and there is NOT ONE KnownGap: every
 * unmasked byte of every stream must equal Project's save.  The committed null
 * save masks per-save noise.  Name ascending is a pair whose order does not
 * change (the rows are already ascending) and whose spec does, so its effects
 * are the spec and the marker only.
 *
 * The Name-descending control carries the 0x26400024 order copy and the other
 * two carry 0x2640003d; both layouts are exercised.  The structural assertions
 * after each pair are independent of the comparison: the two spec slots, the
 * order dwords, the first-row word, and the unchanged byte counts.
 *
 * REFUSALS.  Zero and two keys; ID, Start, Finish and any other key; a view
 * uid other than 2; Outline = False; Renumber = True; a row count other than
 * 5; a sorted order that is not a permutation, has the wrong length, repeats a
 * UniqueID or names UniqueID 0; a base order that is not the control's; a file
 * that already carries a sort (in either spec copy); a uid-2 block that is not
 * the measured one (digest); a CEdl with a damaged header, a missing, doubled
 * or wrong-length order record, a wrong tail or first-row word, a wrong
 * order-copy payload; and an empty, directory or missing-parent output path.
 * Each refusal asserts that the document was not modified and that no output
 * file appeared; the dry run writes nothing.
 */

#include <array>
#include <exception>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "fixture_guard.h"
#include "save_pair_compare.h"
#include "table.h"
#include "viewstate_sort.h"
#include "viewstream_edit.h"

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

using mppwrite::CfbDocument;
using mppwrite::SortField;
using mppwrite::SortKey;
using mppwrite::ViewSortEdit;
using mppwrite::ViewSortWrite;
using Bytes = std::vector<std::uint8_t>;

char const* kViewVar2 = "214/CV_iew/Var2Data";
char const* kViewMeta = "214/CV_iew/VarMeta";
char const* kEdlVar2 = "214/CEdl/Var2Data";

constexpr std::size_t kBlockOffset = 42576;

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
        check(actual == expected,
              what + " (actual " + std::to_string(actual) + ", expected " +
                  std::to_string(expected) + ")");
    }
    /* A KnownGap that is gone is a failure; this file records no gap at all. */
    void expected_failure(bool project_truth, std::string const& what) {
        ++failures_;
        std::cout << "FAIL: " << (project_truth ? "a recorded gap now passes: "
                                                : "a recorded gap exists: ")
                  << what << "\n";
    }
    int report() const {
        std::cout << checks_ << " checks, " << failures_ << " failures\n";
        return failures_ == 0 ? 0 : 1;
    }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

std::string join(std::string const& a, std::string const& b) { return a + "/" + b; }
std::string out_path(std::string const& name) { return join(MPPWRITE_TEST_OUT, name); }
std::string missing_path() { return out_path("w35-sort-refused.mpp"); }

CfbDocument doc_from_data(std::string const& name) {
    return CfbDocument::from_file(join(MPPWRITE_TEST_DATA, name));
}

bool file_exists(std::string const& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::uint32_t u32_at(Bytes const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

void put_u32_at(Bytes& b, std::size_t o, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[o + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

template <typename Fn>
void expect_error(TestContext& t, Fn&& fn, std::string_view needle,
                  std::string const& what) {
    try {
        fn();
        t.check(false, what + ": no exception was thrown");
    } catch (mppwrite::Error const& error) {
        std::string const message = error.what();
        t.check(!needle.empty() && message.find(needle) != std::string::npos,
                what + ": error '" + message + "' does not contain '" +
                    std::string(needle) + "'");
    }
}

/* The pairs. */
struct Pair {
    char const* name;
    SortField field;
    bool ascending;
    std::uint32_t field_id;
    std::vector<std::uint32_t> sorted;
    bool order_changes;
    std::size_t spec_bytes; /* bytes that differ from the default, both copies */
};

std::vector<std::uint32_t> const kBase{1, 2, 3, 4, 5};

std::vector<Pair> pairs() {
    return {
        {"sort-name-desc", SortField::name, false, 0x0B40000Eu, {5, 4, 1, 3, 2}, true, 4},
        {"sort-name-asc", SortField::name, true, 0x0B40000Eu, {1, 2, 3, 4, 5}, false, 2},
        {"sort-duration-desc", SortField::duration, false, 0x0B400507u,
         {4, 1, 2, 3, 5}, true, 6},
    };
}

std::string control_of(Pair const& p) {
    return std::string("wave35-viewstate-") + p.name + "-control.mpp";
}
std::string null_of(Pair const& p) {
    return std::string("wave35-viewstate-") + p.name + "-control-null.mpp";
}
std::string edit_of(Pair const& p) {
    return std::string("wave35-viewstate-") + p.name + ".mpp";
}

ViewSortEdit make_edit(SortField field, bool ascending,
                       std::vector<std::uint32_t> const& sorted) {
    ViewSortEdit edit;
    edit.view_uid = mppwrite::kMeasuredViewUid;
    edit.keys = {SortKey{field, ascending}};
    edit.outline = true;
    edit.renumber = false;
    edit.base_order = kBase;
    edit.sorted_order = sorted;
    return edit;
}

ViewSortEdit measured_edit() {
    Pair const p = pairs().front();
    return make_edit(p.field, p.ascending, p.sorted);
}

/* ---- the pairs ---------------------------------------------------------- */

void test_pair(TestContext& t, Pair const& p) {
    std::string const label = p.name;
    fixture::require_committed(join(MPPWRITE_TEST_DATA, control_of(p)));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, null_of(p)));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, edit_of(p)));

    CfbDocument doc = doc_from_data(control_of(p));
    CfbDocument const before = doc;
    Bytes const control_var2 = doc.read_stream(kViewVar2);
    Bytes const control_edl = doc.read_stream(kEdlVar2);
    t.equal(control_var2.size(), 100178, label + ": the control's Var2Data is 100,178 B");
    t.equal(u32_at(control_var2, kBlockOffset), 42692,
            label + ": the uid-2 block is 42,692 B at 42,576");

    ViewSortEdit const edit = make_edit(p.field, p.ascending, p.sorted);
    std::string const output = out_path(std::string("w35-") + p.name + ".mpp");
    ViewSortWrite const written = mppwrite::save_view_sort(doc, edit, output);
    t.equal(written.view_uid, 2, label + ": view uid");
    t.equal(written.field_id, p.field_id, label + ": field id");
    t.check(written.ascending == p.ascending, label + ": direction");
    t.equal(written.block_offset, kBlockOffset, label + ": block offset");
    t.equal(written.order_rows, 5, label + ": five rows");
    t.equal(written.order_records_written, 3, label + ": order records written");
    t.equal(written.first_row_before, 1, label + ": first row before");
    t.equal(written.first_row_after, p.sorted.front(), label + ": first row after");
    t.equal(written.spec_bytes_changed, p.spec_bytes,
            label + ": spec bytes that differ from the default");

    /* The written spec, both copies. */
    Bytes const var2 = doc.read_stream(kViewVar2);
    Bytes const edl = doc.read_stream(kEdlVar2);
    t.equal(var2.size(), control_var2.size(), label + ": Var2Data keeps its size");
    t.equal(edl.size(), control_edl.size(), label + ": CEdl Var2Data keeps its size");
    for (std::size_t const at : {std::size_t{40}, std::size_t{1192}}) {
        t.equal(u32_at(var2, kBlockOffset + at), p.field_id,
                label + ": slot 0 field id at block + " + std::to_string(at));
        t.equal(var2[kBlockOffset + at + 4], p.ascending ? 1 : 0,
                label + ": slot 0 direction at block + " + std::to_string(at));
        t.equal(var2[kBlockOffset + at + 5], 0,
                label + ": slot 0 direction high byte at block + " + std::to_string(at));
        t.equal(u32_at(var2, kBlockOffset + at + 6), 0xFFFFFFFFu,
                label + ": slot 1 is unused at block + " + std::to_string(at));
        t.equal(u32_at(var2, kBlockOffset + at + 12), 0xFFFFFFFFu,
                label + ": slot 2 is unused at block + " + std::to_string(at));
    }

    /* The order record is the first run of five dwords after id 0x26400035. */
    std::size_t order_at = 0;
    for (std::size_t i = 20; i + 12 < edl.size(); i += 2) {
        if (u32_at(edl, i + 4) == 0x26400035u && u32_at(edl, i + 8) == 1u) {
            order_at = i + 12;
            break;
        }
    }
    t.check(order_at != 0, label + ": the order record is present");
    for (std::size_t i = 0; order_at != 0 && i < 5; ++i) {
        t.equal(u32_at(edl, order_at + 4 * i), p.sorted[i],
                label + ": order dword " + std::to_string(i));
    }

    /* Nothing outside the three owned streams moved. */
    for (std::string const& path : before.stream_paths()) {
        if (path == kViewVar2 || path == kEdlVar2 || path == "Props14") {
            continue;
        }
        t.check(doc.has_stream(path) && doc.read_stream(path) == before.read_stream(path),
                label + ": unowned stream changed: " + path);
    }

    /* Name ascending leaves the rows where they are: the edit list has no
     * effect to declare there (the spec and the marker are the whole edit). */
    std::array<char const*, 3> const declared{{kViewVar2, "Props14", kEdlVar2}};
    save_pair::compare_all(t, label, doc_from_data(control_of(p)),
                           doc_from_data(null_of(p)), doc_from_data(edit_of(p)),
                           CfbDocument::from_file(output), nullptr, 0,
                           declared.data(), p.order_changes ? 3 : 2);
}

/* ---- the dry run -------------------------------------------------------- */

void test_dry_run(TestContext& t) {
    CfbDocument doc = doc_from_data(control_of(pairs().front()));
    Bytes const before = doc.to_bytes();
    std::error_code ignored;
    std::filesystem::remove(missing_path(), ignored);
    ViewSortWrite const planned =
        mppwrite::save_view_sort(doc, measured_edit(), "", true);
    t.check(doc.to_bytes() == before, "dry run: the document changed");
    t.equal(planned.order_rows, 5, "dry run: five rows");
    t.equal(planned.order_records_written, 3, "dry run: three order records");
    t.check(!file_exists(missing_path()), "dry run: an output file appeared");
    /* The dry run refuses what the real write refuses. */
    ViewSortEdit bad = measured_edit();
    bad.keys.clear();
    expect_error(t, [&] { (void)mppwrite::save_view_sort(doc, bad, "", true); },
                 "exactly one key", "dry run: zero keys refused");
}

/* ---- refusals ----------------------------------------------------------- */

void test_refusals(TestContext& t) {
    std::string const missing = missing_path();
    auto refuses = [&](CfbDocument const& source, ViewSortEdit const& edit,
                       std::string_view needle, std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        expect_error(t, [&] { (void)mppwrite::save_view_sort(doc, edit, missing); },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
        t.check(!file_exists(missing), what + ": an output file was written");
    };

    Pair const named = pairs().front();
    CfbDocument const control = doc_from_data(control_of(named));

    /* The edit model. */
    {
        ViewSortEdit edit = measured_edit();
        edit.keys.clear();
        refuses(control, edit, "exactly one key", "zero keys refused");
        edit = measured_edit();
        edit.keys.push_back(SortKey{SortField::duration, true});
        refuses(control, edit, "exactly one key", "two keys refused (the two-key pair "
                "is evidence only)");
    }
    for (auto const& [field, needle] :
         std::vector<std::pair<SortField, std::string_view>>{
             {SortField::id, "sorting by ID"},
             {SortField::start, "Start sort key"},
             {SortField::finish, "Finish sort key"},
             {SortField::other, "this sort key is unmeasured"}}) {
        ViewSortEdit edit = measured_edit();
        edit.keys = {SortKey{field, true}};
        refuses(control, edit, needle,
                "sort field " + std::to_string(static_cast<int>(field)) + " refused");
    }
    {
        ViewSortEdit edit = measured_edit();
        edit.view_uid = 1;
        refuses(control, edit, "view uid 1 is unmeasured", "view uid 1 refused");
        edit.view_uid = 0;
        refuses(control, edit, "view uid 0 is unmeasured", "view uid 0 refused");
        edit = measured_edit();
        edit.outline = false;
        refuses(control, edit, "Outline = False", "Outline = False refused");
        edit = measured_edit();
        edit.renumber = true;
        refuses(control, edit, "Renumber = True", "Renumber = True refused");
        edit = measured_edit();
        edit.base_order = {1, 2, 3, 4};
        edit.sorted_order = {4, 3, 2, 1};
        refuses(control, edit, "4 rows were requested", "four rows refused");
        edit.base_order = {1, 2, 3, 4, 5, 6};
        edit.sorted_order = {6, 5, 4, 3, 2, 1};
        refuses(control, edit, "6 rows were requested", "six rows refused");
        edit = measured_edit();
        edit.sorted_order = {5, 4, 1, 3};
        refuses(control, edit, "the sorted order holds 4 rows", "short sorted order refused");
        edit = measured_edit();
        edit.sorted_order = {5, 4, 1, 3, 9};
        refuses(control, edit, "not a permutation", "foreign UniqueID refused");
        edit = measured_edit();
        edit.sorted_order = {5, 4, 4, 3, 2};
        refuses(control, edit, "not a permutation", "repeated UniqueID refused");
        edit = measured_edit();
        edit.base_order = {1, 2, 3, 4, 4};
        refuses(control, edit, "not a list of distinct", "duplicate base refused");
        edit = measured_edit();
        edit.base_order = {0, 2, 3, 4, 5};
        edit.sorted_order = {5, 4, 0, 3, 2};
        refuses(control, edit, "not a list of distinct", "UniqueID 0 refused");
        edit = measured_edit();
        edit.base_order = {1, 2, 3, 5, 4};
        edit.sorted_order = {5, 4, 3, 2, 1};
        refuses(control, edit, "does not hold the expected UniqueID",
                "a base order that is not the control's refused");
    }

    /* W35b-1.2 (R35G2-2): the request is the file's own.  Each request below is a
     * well-formed one (a permutation of the file's UniqueIDs in the control's
     * order); the file's outline, names or durations disagree with it, or the
     * sorted order is not the one the pinned rules give. */
    {
        auto edited = [&](CfbDocument const& source, auto&& mutate) {
            CfbDocument doc = source;
            mppwrite::TaskTable table = mppwrite::TaskTable::from_document(doc);
            mutate(doc, table);
            table.store_into(doc);
            return doc;
        };
        auto by_duration = [&](std::vector<std::uint32_t> const& sorted) {
            return make_edit(SortField::duration, false, sorted);
        };
        Pair const duration = pairs().back();
        CfbDocument const duration_control = doc_from_data(control_of(duration));
        t.check(mppwrite::read_task_name(control, 5) == "Task E" &&
                    mppwrite::read_task_name(control, 1) == "Task A",
                "R35G2-2: the control's names are Task A to Task E");

        /* The outline. */
        refuses(edited(control,
                       [](CfbDocument&, mppwrite::TaskTable& table) {
                           table.set_u16(3, 172, 1);
                       }),
                measured_edit(),
                "the sorted order does not match the order the file's own outline",
                "R35G2-2: a file whose outline is not the request's order refused");
        refuses(edited(control,
                       [](CfbDocument&, mppwrite::TaskTable& table) {
                           table.set_u16(1, 172, 0);
                       }),
                measured_edit(), "levels are 1-based", "R35G2-2: a level word of 0 refused");
        refuses(edited(control,
                       [](CfbDocument&, mppwrite::TaskTable& table) {
                           table.set_u16(2, 172, 3);
                           table.set_u16(3, 172, 3);
                       }),
                measured_edit(), "deeper than its parent allows",
                "R35G2-2: a row deeper than its parent refused");

        /* The names. */
        {
            CfbDocument renamed = control;
            mppwrite::replace_task_name(renamed, 5, "Task C");
            refuses(renamed, measured_edit(),
                    "the sorted order does not match the order the file's own outline",
                    "R35G2-2: a file whose names give another order refused");
            CfbDocument digit = control;
            mppwrite::replace_task_name(digit, 5, "Task 5");
            refuses(digit, measured_edit(), "differ at a letter of the same case",
                    "R35G2-2: a file with an unmeasured name collation refused");
        }

        /* The sorted order itself. */
        refuses(control, make_edit(SortField::name, false, {2, 1, 3, 4, 5}),
                "the sorted order does not match the order the file's own outline",
                "R35G2-2: a permutation that is not the sort refused");
        refuses(control, make_edit(SortField::name, false, {1, 2, 3, 4, 5}),
                "the sorted order does not match the order the file's own outline",
                "R35G2-2: an unsorted order for Name descending refused");
        refuses(control, make_edit(SortField::name, true, {5, 4, 1, 3, 2}),
                "the sorted order does not match the order the file's own outline",
                "R35G2-2: the descending order for Name ascending refused");

        /* The durations. */
        refuses(edited(duration_control,
                       [](CfbDocument&, mppwrite::TaskTable& table) {
                           table.set_u32(3, mppwrite::task_row::kDuration, 30000);
                       }),
                by_duration(duration.sorted),
                "the sorted order does not match the order the file's own outline",
                "R35G2-2: a file whose durations give another order refused");
        refuses(edited(duration_control,
                       [](CfbDocument&, mppwrite::TaskTable& table) {
                           table.set_u32(5, mppwrite::task_row::kDuration, 24000);
                       }),
                by_duration(duration.sorted), "equal durations among siblings",
                "R35G2-2: equal sibling durations in the file refused");

        /* The rows. */
        refuses(edited(control,
                       [](CfbDocument&, mppwrite::TaskTable& table) {
                           table.set_u32(2, mppwrite::task_row::kUniqueId, 9);
                       }),
                measured_edit(), "the request is not the file's row order",
                "R35G2-2: file rows that are not the request's refused");

        /* The measured shapes still write (the pair tests prove them against
         * Project's saves; this is the same recompute on the three controls). */
        for (Pair const& p : pairs()) {
            CfbDocument doc = doc_from_data(control_of(p));
            (void)mppwrite::save_view_sort(doc, make_edit(p.field, p.ascending, p.sorted), "",
                                           true);
        }
    }

    /* The output path. */
    {
        CfbDocument doc = control;
        Bytes const before = doc.to_bytes();
        expect_error(t, [&] { (void)mppwrite::save_view_sort(doc, measured_edit(), ""); },
                     "output path is empty", "empty path refused");
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_view_sort(doc, measured_edit(),
                                                        MPPWRITE_TEST_OUT);
                     },
                     "is a directory", "directory path refused");
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_view_sort(
                             doc, measured_edit(),
                             out_path("no-such-directory-w35/out.mpp"));
                     },
                     "does not exist", "missing parent refused");
        t.check(doc.to_bytes() == before, "path refusals: the document changed");
    }

    /* The file. */
    {
        CfbDocument const sorted = doc_from_data(edit_of(named));
        refuses(sorted, measured_edit(), "already carries a sort",
                "a file that is already sorted refused");

        CfbDocument second = control;
        Bytes var2 = second.read_stream(kViewVar2);
        put_u32_at(var2, kBlockOffset + 1192, 0x0B40000Eu);
        second.replace_stream(kViewVar2, var2);
        refuses(second, measured_edit(), "block + 1192",
                "a sort in the second spec copy only refused");

        CfbDocument styled = control;
        var2 = styled.read_stream(kViewVar2);
        var2[kBlockOffset + 500] ^= 1;
        styled.replace_stream(kViewVar2, var2);
        refuses(styled, measured_edit(), "digest mismatch",
                "a changed byte outside the spec refused");
        refuses(doc_from_data("wave30-textstyle-control.mpp"), measured_edit(),
                "digest mismatch", "another file's view block refused");

        CfbDocument longer = control;
        var2 = longer.read_stream(kViewVar2);
        put_u32_at(var2, kBlockOffset, 42693);
        longer.replace_stream(kViewVar2, var2);
        refuses(longer, measured_edit(), "not the measured 42692-byte block",
                "a view block of another length refused");

        CfbDocument unkeyed = control;
        Bytes meta = unkeyed.read_stream(kViewMeta);
        for (mppwrite::VarEntry const& entry : mppwrite::read_var_entries(meta)) {
            if (entry.mid == 2 && entry.field_id == 0x20400006u) {
                put_u32_at(meta, 24 + 12 * entry.index + 8, 0x20400007u);
            }
        }
        unkeyed.replace_stream(kViewMeta, meta);
        refuses(unkeyed, measured_edit(), "0 view-state blocks",
                "no view-state block refused");
    }

    /* The edit list: every damaged shape of the order records. */
    {
        auto item_of = [](Bytes const& edl, std::uint32_t id) {
            for (std::size_t i = 20; i + 12 <= edl.size(); i += 2) {
                if (u32_at(edl, i + 4) == id && u32_at(edl, i + 8) == 1u) {
                    return i;
                }
            }
            return std::size_t{0};
        };
        auto damaged = [&](CfbDocument const& source, auto&& mutate) {
            CfbDocument doc = source;
            Bytes edl = doc.read_stream(kEdlVar2);
            mutate(edl);
            doc.replace_stream(kEdlVar2, edl);
            return doc;
        };
        Bytes const named_edl = control.read_stream(kEdlVar2);
        std::size_t const order = item_of(named_edl, 0x26400035u);
        std::size_t const first = item_of(named_edl, 0x26400036u);
        std::size_t const copy = item_of(named_edl, 0x26400024u);
        t.check(order != 0 && first != 0 && copy != 0,
                "edit list: the named control holds all three order records");

        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, 12, 0); }),
                measured_edit(), "measured header", "a damaged CEdl header refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, copy + 4, 0x26400099u); }),
                measured_edit(), "0 order copies", "no order copy refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, order + 4, 0x26400099u); }),
                measured_edit(), "0 order records", "no order record refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, first + 4, 0x26400099u); }),
                measured_edit(), "0 first-row records", "no first-row record refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, order, 28); }),
                measured_edit(), "order record holds 28 bytes",
                "an order record of another length refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, first, 8); }),
                measured_edit(), "first-row record holds 8 bytes",
                "a first-row record of another length refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, copy, 60); }),
                measured_edit(), "order-copy record holds 60 bytes",
                "an order copy of another length refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, order + 12 + 20, 0x1234); }),
                measured_edit(), "tail dwords", "a different order tail refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, first + 12, 3); }),
                measured_edit(), "first-row record is not the first row",
                "a first-row word that is not the first row refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, copy + 12 + 20, 7); }),
                measured_edit(), "order copy does not hold the expected dword 5",
                "an order copy with a stray dword refused");
    }
}

}  // namespace

int main() {
    TestContext t;
    try {
        for (Pair const& p : pairs()) {
            test_pair(t, p);
        }
        test_dry_run(t);
        test_refusals(t);
    } catch (std::exception const& error) {
        t.check(false, std::string("an exception escaped a test: ") + error.what());
    }
    return t.report();
}
