/*
 * roundtrip_viewstate_outline.cpp -- W35-2.3: the persisted outline level
 * (View > Outline > Show > Level N) against Project's own saves, plus every
 * refusal the writer offers.
 *
 * WHAT IS PROVEN.  For each of the five pairs Project measured (Level 1 on the
 * five-task base; Level 2 on it, which hides nothing; Level 1 and Level 2 on a
 * six-task three-level outline; Show All on the base) the port opens PROJECT'S
 * CONTROL, applies the level through core/mppwrite/viewstate_outline.h exactly as
 * the command does, and compares EVERY stream of the four documents with
 * Project's edit save through save_pair::compare_all.  The streams the edit owns
 * are declared (their measured effect sets must be non-empty) and there is NOT
 * ONE KnownGap: every unmasked byte of every stream must equal Project's save.
 * The committed null save masks per-save noise.  Level 2 on the base and Show All
 * hide nothing: Project's save differs from its control by the Props14 marker
 * alone, and the port reproduces exactly that (Show All's pair is matched by the
 * same write, since the control and the shape are the same).
 *
 * The structural assertions after each pair are independent of the comparison:
 * the FixedMeta flag of every row, the displayed-row list, the first row, the
 * order copy, the three size words, and the unchanged byte counts of the other
 * streams.
 *
 * REFUSALS.  A view uid other than 2; Show Level 0, 3, 4 and -1; an outline of
 * another row count or another shape; repeated or zero UniqueIDs; a request that
 * is not the file's row order or row count; a file that already hides or
 * collapses a row; a CEdl with a damaged header, a missing, doubled or
 * wrong-length record, a wrong tail, a wrong first row or a stray order-copy
 * dword; a VarMeta or Props size word that does not match the CEdl; and an empty,
 * directory or missing-parent output path.  Each refusal asserts that the
 * document was not modified and that no output file appeared; the dry run writes
 * nothing.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cfb_stream_removal.h"
#include "cfb_writer.h"
#include "edl_damage.h"
#include "fixture_guard.h"
#include "save_pair_compare.h"
#include "table.h"
#include "viewstate_outline.h"
#include "viewstream_edit.h"

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

using mppwrite::CfbDocument;
using mppwrite::OutlineViewRow;
using mppwrite::ViewOutlineEdit;
using mppwrite::ViewOutlineWrite;
using Bytes = std::vector<std::uint8_t>;

char const* kMetaPath = "114/TBkndTask/FixedMeta";
char const* kEdlVar2 = "214/CEdl/Var2Data";
char const* kEdlMeta = "214/CEdl/VarMeta";
char const* kProps = "214/Props";

constexpr std::size_t kEdlSizeAt = 11402; /* the 0x07000100 scalar's value */
constexpr std::size_t kViewBlockAt = 42576; /* the uid-2 block in CV_iew/Var2Data */

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
std::string missing_path() { return out_path("w35-outline-refused.mpp"); }

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
std::vector<std::uint32_t> const kBaseLevels{0, 1, 1, 0, 0};
std::vector<std::uint32_t> const kDeepLevels{0, 1, 2, 2, 1, 0};

struct Pair {
    char const* name;
    int level;
    std::vector<std::uint32_t> const* levels;
    std::vector<std::uint32_t> shown;     /* displayed UniqueIDs, file order */
    std::vector<std::uint32_t> collapsed; /* UniqueIDs whose flag is 0x03 */
    std::size_t flags_changed;
};

std::vector<Pair> pairs() {
    return {
        {"outline-level1", 1, &kBaseLevels, {1, 4, 5}, {1}, 3},
        {"outline-level2", 2, &kBaseLevels, {1, 2, 3, 4, 5}, {}, 0},
        {"outline-deep-level1", 1, &kDeepLevels, {1, 6}, {1}, 5},
        {"outline-deep-level2", 2, &kDeepLevels, {1, 2, 5, 6}, {2}, 3},
    };
}

std::string control_name(std::string const& stem) {
    return "wave35-viewstate-" + stem + "-control.mpp";
}
std::string null_name(std::string const& stem) {
    return "wave35-viewstate-" + stem + "-control-null.mpp";
}
std::string edit_name(std::string const& stem) { return "wave35-viewstate-" + stem + ".mpp"; }

ViewOutlineEdit make_edit(int level, std::vector<std::uint32_t> const& levels) {
    ViewOutlineEdit edit;
    edit.view_uid = mppwrite::kMeasuredViewUid;
    edit.show_level = level;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        edit.rows.push_back(OutlineViewRow{static_cast<std::uint32_t>(i + 1), levels[i]});
    }
    return edit;
}

ViewOutlineEdit measured_edit() { return make_edit(1, kBaseLevels); }

std::string const kBaseControl = control_name("outline-level1");

std::size_t flag_at(std::size_t row) { return 16 + 47 * (row + 3) + 15; }

/* The offset of the CEdl item with this id, or 0. */
std::size_t item_of(Bytes const& edl, std::uint32_t id) {
    for (std::size_t i = 20; i + 12 <= edl.size(); i += 2) {
        if (u32_at(edl, i + 4) == id && u32_at(edl, i + 8) == 1u) {
            return i;
        }
    }
    return 0;
}

/*
 * The two CEdl size words (214/CEdl/VarMeta +0x14 and the 214/Props scalar) hold
 * the CEdl Var2Data length, and that length includes the save-path record
 * 0x26400031 -- the full path of the saving file.  Project's null and edit saves
 * are made under other file names than the control, so their size words carry the
 * path-length difference as noise that the byte mask (control against null) only
 * partly covers: a difference that crosses a 256 boundary moves byte +21 / +11403
 * alone, the signature wave 33 recorded as an open gap.  The port keeps the
 * control's path record, so its size words are the stream's own length.  To
 * compare like with like, give Project's saves the size their CEdl would have
 * with the control's path record: the same normalisation, in the same spirit,
 * that save_pair_noise.h applies to the path record inside the stream.  The
 * writer's own size words are asserted separately against the stream length.
 *
 * Evidence that this is a test-side edit of a word that tracks the path, not of
 * one that hides a difference (RW35-W11): Project's null save is the control
 * re-saved under another name with no edit, and its two size words equal its own
 * CEdl length, as the control's do.  with_control_path_size asserts that of every
 * save it rewrites (R35G2-9), so the edit changes a word only by the path-length
 * difference and would throw on any other value.
 */
std::size_t path_record_bytes(Bytes const& edl) {
    std::size_t const at = item_of(edl, 0x26400031u);
    return at == 0 ? 0 : u32_at(edl, at);
}

CfbDocument with_control_path_size(std::string const& name, std::size_t control_path) {
    CfbDocument doc = doc_from_data(name);
    Bytes const edl = doc.read_stream(kEdlVar2);
    std::size_t const size = edl.size() - path_record_bytes(edl) + control_path;
    Bytes meta = doc.read_stream(kEdlMeta);
    Bytes props = doc.read_stream(kProps);
    /* R35G2-9: Project's own words must be its CEdl length before they are replaced, or the
     * normalisation would hide a difference of its own. */
    if (u32_at(meta, 0x14) != edl.size() || u32_at(props, kEdlSizeAt) != edl.size()) {
        throw std::runtime_error(
            "with_control_path_size: " + name + " holds CEdl size words " +
            std::to_string(u32_at(meta, 0x14)) + " (VarMeta +0x14) and " +
            std::to_string(u32_at(props, kEdlSizeAt)) + " (Props), not its CEdl length " +
            std::to_string(edl.size()));
    }
    put_u32_at(meta, 0x14, static_cast<std::uint32_t>(size));
    doc.replace_stream(kEdlMeta, meta);
    put_u32_at(props, kEdlSizeAt, static_cast<std::uint32_t>(size));
    doc.replace_stream(kProps, props);
    return doc;
}

/* ---- the pairs ---------------------------------------------------------- */

void test_pair(TestContext& t, Pair const& p) {
    std::string const label = p.name;
    std::string const stem = p.name;
    fixture::require_committed(join(MPPWRITE_TEST_DATA, control_name(stem)));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, null_name(stem)));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, edit_name(stem)));

    CfbDocument doc = doc_from_data(control_name(stem));
    CfbDocument const before = doc;
    std::size_t const n = p.levels->size();
    Bytes const control_edl = doc.read_stream(kEdlVar2);
    t.equal(u32_at(doc.read_stream(kEdlMeta), 0x14), control_edl.size(),
            label + ": the control's VarMeta size word is the CEdl size");
    t.equal(u32_at(doc.read_stream(kProps), kEdlSizeAt), control_edl.size(),
            label + ": the control's Props size word is the CEdl size");

    ViewOutlineEdit const edit = make_edit(p.level, *p.levels);
    std::string const output = out_path(std::string("w35-") + p.name + ".mpp");
    ViewOutlineWrite const written = mppwrite::save_view_outline(doc, edit, output);
    t.equal(static_cast<std::size_t>(written.show_level), static_cast<std::size_t>(p.level),
            label + ": level");
    t.equal(written.rows, n, label + ": rows");
    t.equal(written.hidden_rows, n - p.shown.size(), label + ": hidden rows");
    t.equal(written.collapsed_rows, p.collapsed.size(), label + ": collapsed rows");
    t.equal(written.flags_written, p.flags_changed, label + ": flag bytes that changed");
    t.equal(written.edl_bytes_before, control_edl.size(), label + ": CEdl bytes before");
    bool const has_layout = item_of(control_edl, 0x2640003Du) != 0;
    t.equal(written.edl_bytes_after,
            control_edl.size() - (has_layout ? 8 : 4) * (n - p.shown.size()),
            label + ": CEdl bytes after (4 per hidden row in the order record, and 4 in "
                    "the row-layout record where there is one)");

    /* FixedMeta flags. */
    Bytes const meta = doc.read_stream(kMetaPath);
    Bytes const control_meta = before.read_stream(kMetaPath);
    t.equal(meta.size(), control_meta.size(), label + ": FixedMeta keeps its size");
    for (std::size_t row = 1; row <= n; ++row) {
        std::uint32_t const uid = static_cast<std::uint32_t>(row);
        bool const is_shown = std::find(p.shown.begin(), p.shown.end(), uid) != p.shown.end();
        bool const is_collapsed =
            std::find(p.collapsed.begin(), p.collapsed.end(), uid) != p.collapsed.end();
        std::uint8_t const expected = !is_shown ? 0x00 : is_collapsed ? 0x03 : 0x01;
        t.equal(meta[flag_at(row)], expected,
                label + ": FixedMeta flag of row " + std::to_string(row));
    }

    /* The CEdl: displayed rows, first row, order copy, size words. */
    Bytes const edl = doc.read_stream(kEdlVar2);
    std::size_t const order = item_of(edl, 0x26400035u);
    std::size_t const first = item_of(edl, 0x26400036u);
    std::size_t const copy = item_of(edl, 0x26400024u);
    t.check(order != 0 && first != 0 && copy != 0, label + ": the three records exist");
    t.equal(u32_at(edl, order), 4 * (p.shown.size() + 3), label + ": order record length");
    for (std::size_t i = 0; order != 0 && i < p.shown.size(); ++i) {
        t.equal(u32_at(edl, order + 12 + 4 * i), p.shown[i],
                label + ": displayed row " + std::to_string(i));
    }
    t.equal(u32_at(edl, order + 12 + 4 * p.shown.size()), 0x000FFDC0u - (n - p.shown.size()),
            label + ": order tail");
    t.equal(u32_at(edl, first + 12), p.shown.front(), label + ": first displayed row");
    for (std::size_t i = 0; i < 16; ++i) {
        t.equal(u32_at(edl, copy + 12 + 4 * i), i < p.shown.size() ? p.shown[i] : 0xFFFFFFFFu,
                label + ": order copy dword " + std::to_string(i));
    }
    /* The row-layout record, where the control has one, follows the row count. */
    std::size_t const layout = item_of(edl, 0x2640003Du);
    t.check((layout != 0) == (item_of(control_edl, 0x2640003Du) != 0),
            label + ": the row-layout record is neither added nor dropped");
    if (layout != 0) {
        t.equal(u32_at(edl, layout), 14 + 4 * p.shown.size(), label + ": row-layout length");
        t.equal(u32_at(edl, layout + 12), p.shown.size(), label + ": row-layout row count");
        t.equal(u32_at(edl, layout + 12 + 4 * p.shown.size() + 4), 22 * p.shown.size(),
                label + ": row-layout end offset");
    }
    t.equal(u32_at(edl, 0), edl.size() - 4, label + ": CEdl header word 0");
    t.equal(u32_at(edl, 4), edl.size() - 8, label + ": CEdl header word 1");
    t.equal(u32_at(edl, 8), edl.size() - 8, label + ": CEdl header word 2");
    t.equal(u32_at(doc.read_stream(kEdlMeta), 0x14), edl.size(), label + ": VarMeta size word");
    t.equal(u32_at(doc.read_stream(kProps), kEdlSizeAt), edl.size(), label + ": Props size word");

    /* Nothing outside the five owned streams moved. */
    for (std::string const& path : before.stream_paths()) {
        if (path == kMetaPath || path == kEdlVar2 || path == kEdlMeta || path == kProps ||
            path == "Props14") {
            continue;
        }
        t.check(doc.has_stream(path) && doc.read_stream(path) == before.read_stream(path),
                label + ": unowned stream changed: " + path);
    }

    /* A level that hides nothing is the marker and nothing else. */
    if (p.flags_changed == 0) {
        t.check(edl == control_edl, label + ": a no-op level leaves the CEdl as it was");
        t.check(meta == control_meta, label + ": a no-op level leaves FixedMeta as it was");
    }

    std::size_t const control_path = path_record_bytes(control_edl);
    std::array<char const*, 5> const declared{
        {"Props14", kMetaPath, kEdlVar2, kEdlMeta, kProps}};
    save_pair::compare_all(t, label, doc_from_data(control_name(stem)),
                           with_control_path_size(null_name(stem), control_path),
                           with_control_path_size(edit_name(stem), control_path),
                           CfbDocument::from_file(output), nullptr, 0, declared.data(),
                           p.flags_changed == 0 ? 1 : 5);
}

/* Show All on the base hides nothing: Project's save is the control plus the
 * marker, and so is the port's Level 2 write on the same control. */
void test_show_all(TestContext& t) {
    std::string const stem = "outline-all";
    fixture::require_committed(join(MPPWRITE_TEST_DATA, control_name(stem)));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, null_name(stem)));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, edit_name(stem)));
    CfbDocument doc = doc_from_data(control_name(stem));
    CfbDocument const before = doc;
    std::string const output = out_path("w35-outline-all.mpp");
    ViewOutlineWrite const written =
        mppwrite::save_view_outline(doc, make_edit(2, kBaseLevels), output);
    t.equal(written.hidden_rows, 0, "outline-all: nothing hidden");
    t.equal(written.flags_written, 0, "outline-all: no flag changed");
    for (std::string const& path : before.stream_paths()) {
        if (path != "Props14") {
            t.check(doc.read_stream(path) == before.read_stream(path),
                    "outline-all: stream changed: " + path);
        }
    }
    std::size_t const control_path = path_record_bytes(before.read_stream(kEdlVar2));
    std::array<char const*, 1> const declared{{"Props14"}};
    save_pair::compare_all(t, "outline-all", doc_from_data(control_name(stem)),
                           with_control_path_size(null_name(stem), control_path),
                           with_control_path_size(edit_name(stem), control_path),
                           CfbDocument::from_file(output), nullptr, 0, declared.data(), 1);
}

/* ---- the dry run -------------------------------------------------------- */

void test_dry_run(TestContext& t) {
    CfbDocument doc = doc_from_data(kBaseControl);
    Bytes const before = doc.to_bytes();
    std::error_code ignored;
    std::filesystem::remove(missing_path(), ignored);
    ViewOutlineWrite const planned = mppwrite::save_view_outline(doc, measured_edit(), "", true);
    t.check(doc.to_bytes() == before, "dry run: the document changed");
    t.equal(planned.hidden_rows, 2, "dry run: two hidden rows");
    t.equal(planned.collapsed_rows, 1, "dry run: one collapsed row");
    t.check(!file_exists(missing_path()), "dry run: an output file appeared");
    ViewOutlineEdit bad = measured_edit();
    bad.show_level = 3;
    expect_error(t, [&] { (void)mppwrite::save_view_outline(doc, bad, "", true); },
                 "Show Level 3 is unmeasured", "dry run: level 3 refused");
}

/* ---- refusals ----------------------------------------------------------- */

using edl_damage::duplicate_item;
using edl_damage::resize_item;

void test_refusals(TestContext& t) {
    std::string const missing = missing_path();
    auto refuses = [&](CfbDocument const& source, ViewOutlineEdit const& edit,
                       std::string_view needle, std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        expect_error(t, [&] { (void)mppwrite::save_view_outline(doc, edit, missing); }, needle,
                     what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
        t.check(!file_exists(missing), what + ": an output file was written");
    };

    CfbDocument const control = doc_from_data(kBaseControl);

    /* The edit model. */
    {
        ViewOutlineEdit edit = measured_edit();
        edit.view_uid = 1;
        refuses(control, edit, "view uid 1 is unmeasured", "view uid 1 refused");
        edit.view_uid = 0;
        refuses(control, edit, "view uid 0 is unmeasured", "view uid 0 refused");
        for (int level : {0, 3, 4, -1}) {
            edit = measured_edit();
            edit.show_level = level;
            refuses(control, edit, "Show Level " + std::to_string(level) + " is unmeasured",
                    "Show Level " + std::to_string(level) + " refused");
        }
        edit = measured_edit();
        edit.rows.clear();
        refuses(control, edit, "is not one the pairs measure", "no rows refused");
        edit = measured_edit();
        edit.rows.pop_back();
        refuses(control, edit, "outline of 4 tasks is not one", "four rows refused");
        edit = measured_edit();
        edit.rows.push_back(OutlineViewRow{6, 0});
        refuses(control, edit, "outline of 6 tasks is not one", "six rows of the wrong shape");
        edit = measured_edit();
        edit.rows[2].level = 2;
        refuses(control, edit, "is not one the pairs measure", "another outline shape refused");
        /* R35G2-11: the refusal names the measured outlines as the 0-based levels the code and
         * the constants use, not 1-based ones. */
        refuses(control, edit,
                "(0-based levels 0,1,1,0,0 over five tasks or 0,1,2,2,1,0 over six)",
                "the refusal names the 0-based measured outlines");
        edit = measured_edit();
        edit.rows[4].level = 1;
        refuses(control, edit, "is not one the pairs measure",
                "a last row indented under the summary refused");
        edit = measured_edit();
        edit.rows[1].uid = 1;
        refuses(control, edit, "not distinct task UniqueIDs", "a repeated UniqueID refused");
        edit = measured_edit();
        edit.rows[0].uid = 0;
        refuses(control, edit, "not distinct task UniqueIDs", "UniqueID 0 refused");
        edit = measured_edit();
        std::swap(edit.rows[3].uid, edit.rows[4].uid);
        refuses(control, edit, "the request is not the file's row order",
                "rows that are not the file's order refused");
        refuses(doc_from_data(control_name("outline-deep-level1")), measured_edit(),
                "task rows (with the project row)", "a six-task file with a five-task request");
    }

    /* W35b-1.1 (R35G2-1): the view state the level sits on.  Each Project save
     * below passes every other check (five tasks UID 1..5, every row shown, the
     * CEdl order 1..5) and differs from the measured control only in its view
     * state, so only the new pins refuse it. */
    {
        for (char const* stem : {"table-cost", "table-schedule", "zoom-entire",
                                 "zoom-custom", "zoom-reset", "sort-name-asc"}) {
            fixture::require_committed(join(MPPWRITE_TEST_DATA, edit_name(stem)));
        }
        refuses(doc_from_data(edit_name("table-cost")), measured_edit(),
                "the applied table is \"Cost\", not the measured Entry table",
                "R35G2-1: the Cost table applied refused");
        refuses(doc_from_data(edit_name("table-schedule")), measured_edit(),
                "the applied table is \"Schedule\", not the measured Entry table",
                "R35G2-1: the Schedule table applied refused");
        for (char const* stem : {"zoom-entire", "zoom-custom", "zoom-reset"}) {
            refuses(doc_from_data(edit_name(stem)), measured_edit(),
                    "view-state block is not one of the two measured controls' "
                    "outside its sort spec (digest mismatch",
                    std::string("R35G2-1: a zoomed timescale refused: ") + stem);
        }
        refuses(doc_from_data(edit_name("sort-name-asc")), measured_edit(),
                "the file already carries a sort (the spec at block + 40",
                "R35G2-1: a persisted Name-ascending sort refused");

        /* Synthetic damage to the two records, so each pin is reached alone. */
        auto with_edl = [&](auto&& mutate) {
            CfbDocument doc = control;
            Bytes edl = doc.read_stream(kEdlVar2);
            mutate(edl);
            doc.replace_stream(kEdlVar2, edl);
            return doc;
        };
        Bytes const control_edl = control.read_stream(kEdlVar2);
        std::size_t const view_name = item_of(control_edl, 0x26400018u);
        std::size_t const table_name = item_of(control_edl, 0x26400019u);
        std::size_t const table_fields = item_of(control_edl, 0x26400017u);
        t.check(view_name != 0 && table_name != 0 && table_fields != 0,
                "R35G2-1: the control holds the applied view, table and column records");
        /* '&Gantt Chart' -> '&Gantt Charm' (the 11th UTF-16 unit). */
        refuses(with_edl([&](Bytes& e) { e[view_name + 12 + 2 * 11] = 'm'; }),
                measured_edit(), "the applied view is \"&Gantt Charm\", not the measured",
                "R35G2-1: another applied view refused");
        refuses(with_edl([&](Bytes& e) { e[table_name + 12] = 'X'; }), measured_edit(),
                "the applied table is \"Xntry\"", "R35G2-1: another table name refused");
        refuses(with_edl([&](Bytes& e) { put_u32_at(e, table_fields + 12 + 8, 0x0b400009u); }),
                measured_edit(), "applied table's columns are not the measured Entry table's",
                "R35G2-1: another column list refused");
        refuses(with_edl([&](Bytes& e) { put_u32_at(e, view_name + 4, 0x26400099u); }),
                measured_edit(), "holds no applied-view name record",
                "R35G2-1: a CEdl without the applied view refused");

        /* One sort-spec copy changed in an otherwise measured control. */
        CfbDocument spec_one = control;
        Bytes var2 = spec_one.read_stream("214/CV_iew/Var2Data");
        std::size_t const block = kViewBlockAt;
        t.equal(u32_at(var2, block), 42692, "R35G2-1: the uid-2 block is at its measured offset");
        var2[block + 1192] = 0x0e;
        spec_one.replace_stream("214/CV_iew/Var2Data", var2);
        refuses(spec_one, measured_edit(),
                "the file already carries a sort (the spec at block + 1192",
                "R35G2-1: a sort in the second spec copy only refused");
        /* A byte outside the spec changed: the digest no longer matches. */
        CfbDocument style_changed = control;
        var2 = style_changed.read_stream("214/CV_iew/Var2Data");
        var2[block + 4 + 5000] ^= 0x01;
        style_changed.replace_stream("214/CV_iew/Var2Data", var2);
        refuses(style_changed, measured_edit(), "(digest mismatch",
                "R35G2-1: a changed byte in the view block refused");
        /* The six-task control's own digest still passes (the pair test writes it). */
        {
            CfbDocument deep = doc_from_data(control_name("outline-deep-level2"));
            t.check(!deep.read_stream("214/CV_iew/Var2Data").empty(),
                    "R35G2-1: the alternate-digest control opens");
            ViewOutlineEdit const deep_edit = make_edit(2, kDeepLevels);
            (void)mppwrite::save_view_outline(deep, deep_edit, "", true);
        }
        /* W35c-2.12 (R35bG1-4): the second text style is measured for the six-task outline
         * only.  The flat sort control is a five-task file carrying it; Show Level on it was
         * accepted although no pair measured that combination, and the writer and the
         * builder's own view check (require_measured_outline_view) now both refuse it. */
        {
            fixture::require_committed(
                join(MPPWRITE_TEST_DATA, control_name("sort-flat-name-desc")));
            CfbDocument flat = doc_from_data(control_name("sort-flat-name-desc"));
            refuses(flat, measured_edit(),
                    "no pair measured for this shape (digest mismatch for the first text style only)",
                    "R35bG1-4: the five-task outline on the second text style refused");
            expect_error(
                t, [&] { mppwrite::require_measured_outline_view(flat, measured_edit().rows); },
                "no pair measured for this shape (digest mismatch for the first text style only)",
                "R35bG1-4: the builder's view check refuses the five-task outline too");
            bool six_task_passes = true;
            try {
                mppwrite::require_measured_outline_view(flat, make_edit(2, kDeepLevels).rows);
            } catch (std::exception const&) {
                six_task_passes = false;
            }
            t.check(six_task_passes,
                    "R35bG1-4: the second text style is still accepted for the six-task outline");
        }
    }

    /* W35b-1.2 (R35G2-2): the request's outline is the file's own.  The request
     * below is the measured shape (so every request-side check passes); the file's
     * OutlineLevel word (FixedData +172) disagrees with it. */
    {
        auto with_level = [&](std::size_t row, std::uint16_t stored) {
            CfbDocument doc = control;
            mppwrite::TaskTable table = mppwrite::TaskTable::from_document(doc);
            table.set_u16(row, 172, stored);
            table.store_into(doc);
            return doc;
        };
        t.check(mppwrite::TaskTable::from_document(control).u16(3, 172) == 2,
                "R35G2-2: the control stores row 3 at outline level 2");
        refuses(with_level(3, 1), measured_edit(),
                "task row 3 stores outline level 1 in the file, but the request describes level 2",
                "R35G2-2: a file that stores a row at another level refused");
        refuses(with_level(4, 2), measured_edit(),
                "task row 4 stores outline level 2 in the file, but the request describes level 1",
                "R35G2-2: a file that nests a top-level row refused");
        refuses(with_level(1, 0), measured_edit(), "stores outline level 0 in the file",
                "R35G2-2: a level word of 0 refused");
        /* The request is the file's: the same edit still writes the control. */
        CfbDocument whole = control;
        (void)mppwrite::save_view_outline(whole, measured_edit(), "", true);
    }

    /* The output path. */
    {
        CfbDocument doc = control;
        Bytes const before = doc.to_bytes();
        expect_error(t, [&] { (void)mppwrite::save_view_outline(doc, measured_edit(), ""); },
                     "output path is empty", "empty path refused");
        expect_error(
            t,
            [&] { (void)mppwrite::save_view_outline(doc, measured_edit(), MPPWRITE_TEST_OUT); },
            "is a directory", "directory path refused");
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_view_outline(
                             doc, measured_edit(), out_path("no-such-directory-w35/out.mpp"));
                     },
                     "does not exist", "missing parent refused");
        t.check(doc.to_bytes() == before, "path refusals: the document changed");
    }

    /* The file's current outline state. */
    {
        CfbDocument const collapsed = doc_from_data(edit_name("outline-level1"));
        refuses(collapsed, measured_edit(), "already collapsed or hidden",
                "a file that already hides rows refused");

        CfbDocument flagged = control;
        Bytes meta = flagged.read_stream(kMetaPath);
        meta[flag_at(3)] = 0x03;
        flagged.replace_stream(kMetaPath, meta);
        refuses(flagged, measured_edit(), "task row 3 is already collapsed or hidden (flag 3)",
                "a single collapsed row refused");

        CfbDocument short_meta = control;
        meta = short_meta.read_stream(kMetaPath);
        meta.resize(meta.size() - 47);
        short_meta.replace_stream(kMetaPath, meta);
        refuses(short_meta, measured_edit(), "114/TBkndTask/FixedMeta holds",
                "a FixedMeta of another size refused");

        /* The collapsed file's CEdl against the control's FixedMeta: the order
         * record is shorter than a fully expanded outline needs. */
        CfbDocument mixed = collapsed;
        meta = mixed.read_stream(kMetaPath);
        for (std::size_t row = 1; row <= 5; ++row) {
            meta[flag_at(row)] = 0x01;
        }
        mixed.replace_stream(kMetaPath, meta);
        refuses(mixed, measured_edit(), "control's CEdl order record holds 24 bytes, not 32",
                "a CEdl that already lists fewer rows refused");
    }

    /* W35b-3.3 (R35G2-7): a document that lacks one of the streams the write needs. */
    for (char const* stream : {"114/TBkndTask/FixedData", kMetaPath, kEdlVar2, kEdlMeta, kProps}) {
        refuses(cfb_test::without_stream(control, stream), measured_edit(),
                std::string("the document has no ") + stream,
                std::string("a document without ") + stream + " refused");
    }

    /* The edit list: every damaged shape. */
    {
        auto damaged = [&](CfbDocument const& source, auto&& mutate) {
            CfbDocument doc = source;
            Bytes edl = doc.read_stream(kEdlVar2);
            mutate(edl);
            doc.replace_stream(kEdlVar2, edl);
            return doc;
        };
        Bytes const control_edl = control.read_stream(kEdlVar2);
        std::size_t const order = item_of(control_edl, 0x26400035u);
        std::size_t const first = item_of(control_edl, 0x26400036u);
        std::size_t const copy = item_of(control_edl, 0x26400024u);
        t.check(order != 0 && first != 0 && copy != 0,
                "edit list: the control holds all three order records");

        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, 12, 0); }), measured_edit(),
                "measured header", "a damaged CEdl header refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, copy + 4, 0x26400099u); }),
                measured_edit(), "0 order copies", "no order copy refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, order + 4, 0x26400099u); }),
                measured_edit(), "0 order records", "no order record refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, first + 4, 0x26400099u); }),
                measured_edit(), "0 first-row records", "no first-row record refused");
        /* A length word that is only changed misparses the list; to reach the
         * writer's own length check the item is resized with the stream. */
        refuses(damaged(control, [&](Bytes& e) { resize_item(e, first, 8); }), measured_edit(),
                "first-row record holds 8 bytes", "a first-row record of another length refused");
        refuses(damaged(control, [&](Bytes& e) { resize_item(e, copy, 60); }), measured_edit(),
                "order-copy record holds 60 bytes", "an order copy of another length refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, first, 8); }), measured_edit(),
                "runs past the stream", "a length word that misparses the list refused");
        refuses(damaged(control, [&](Bytes& e) { e.push_back(0); put_u32_at(e, 0, 8); }),
                measured_edit(), "measured header", "a header word that disagrees refused");
        refuses(damaged(control,
                        [&](Bytes& e) {
                            e.insert(e.end(), 2, 0);
                            put_u32_at(e, 0, static_cast<std::uint32_t>(e.size() - 4));
                            put_u32_at(e, 4, static_cast<std::uint32_t>(e.size() - 8));
                            put_u32_at(e, 8, static_cast<std::uint32_t>(e.size() - 8));
                        }),
                measured_edit(), "does not end at its last item",
                "bytes after the last item refused");
        /* W35b-3.3 (R35G2-7): a doubled record, each with its own count. */
        refuses(damaged(control, [&](Bytes& e) { duplicate_item(e, order); }), measured_edit(),
                "holds 2 order records, 1 first-row records, 1 order copies and 0 row-layout",
                "a doubled order record refused");
        refuses(damaged(control, [&](Bytes& e) { duplicate_item(e, first); }), measured_edit(),
                "holds 1 order records, 2 first-row records, 1 order copies and 0 row-layout",
                "a doubled first-row record refused");
        refuses(damaged(control, [&](Bytes& e) { duplicate_item(e, copy); }), measured_edit(),
                "holds 1 order records, 1 first-row records, 2 order copies and 0 row-layout",
                "a doubled order copy refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, order + 12 + 8, 9); }),
                measured_edit(), "does not hold the expected UniqueID at row 3",
                "a displayed row that is not the file's refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, order + 12 + 20, 0x1234); }),
                measured_edit(), "tail dwords", "a different order tail refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, first + 12, 3); }),
                measured_edit(), "first-row record is not the first displayed row",
                "a first-row word that is not the first row refused");
        refuses(damaged(control, [&](Bytes& e) { put_u32_at(e, copy + 12 + 20, 7); }),
                measured_edit(), "order copy does not hold the expected dword 5",
                "an order copy with a stray dword refused");
    }

    /* The optional row-layout record (0x2640003d) of the six-task controls. */
    {
        CfbDocument const deep = doc_from_data(control_name("outline-deep-level1"));
        ViewOutlineEdit const deep_edit = make_edit(1, kDeepLevels);
        Bytes const deep_edl = deep.read_stream(kEdlVar2);
        std::size_t const layout = item_of(deep_edl, 0x2640003Du);
        t.check(layout != 0, "row layout: the six-task control holds the record");
        CfbDocument bad = deep;
        Bytes edl = deep_edl;
        put_u32_at(edl, layout + 12 + 8, 0x20);
        bad.replace_stream(kEdlVar2, edl);
        refuses(bad, deep_edit, "row-layout record is not the measured layout at byte 8",
                "a row layout with another row height refused");
        /* W35b-3.3 (R35G2-7): a layout record of another length, and a doubled one. */
        std::size_t const layout_length = u32_at(deep_edl, layout);
        bad = deep;
        edl = deep_edl;
        resize_item(edl, layout, layout_length + 4);
        bad.replace_stream(kEdlVar2, edl);
        refuses(bad, deep_edit,
                "row-layout record holds " + std::to_string(layout_length + 4) +
                    " bytes, not " + std::to_string(layout_length) + " for 6 displayed rows",
                "a row layout of another length refused");
        bad = deep;
        edl = deep_edl;
        duplicate_item(edl, layout);
        bad.replace_stream(kEdlVar2, edl);
        refuses(bad, deep_edit,
                "holds 1 order records, 1 first-row records, 1 order copies and 2 row-layout",
                "a doubled row layout refused");
        bad = deep;
        edl = deep_edl;
        put_u32_at(edl, layout + 12, 5);
        bad.replace_stream(kEdlVar2, edl);
        refuses(bad, deep_edit, "row-layout record is not the measured layout at byte 0",
                "a row layout that disagrees on the row count refused");
    }

    /* The size words that mirror the CEdl. */
    {
        CfbDocument bad_meta = control;
        Bytes meta = bad_meta.read_stream(kEdlMeta);
        put_u32_at(meta, 0x14, u32_at(meta, 0x14) + 4);
        bad_meta.replace_stream(kEdlMeta, meta);
        refuses(bad_meta, measured_edit(), "does not carry the CEdl Var2Data size",
                "a VarMeta size word that is not the CEdl size refused");

        CfbDocument bad_props = control;
        Bytes props = bad_props.read_stream(kProps);
        put_u32_at(props, kEdlSizeAt, u32_at(props, kEdlSizeAt) + 4);
        bad_props.replace_stream(kProps, props);
        refuses(bad_props, measured_edit(), "214/Props says the CEdl Var2Data is",
                "a Props size word that is not the CEdl size refused");

        CfbDocument no_tag = control;
        props = no_tag.read_stream(kProps);
        props[kEdlSizeAt - 8 + 3] = 0x08; /* the tag's low byte: no longer 0x07000100 */
        no_tag.replace_stream(kProps, props);
        refuses(no_tag, measured_edit(), "scalar records for the CEdl size tag",
                "a Props without the size tag refused");
    }
}

}  // namespace

/* A writer that throws on a measured pair is a failure of that pair, reported by
 * name, not a crash of the whole test. */
template <typename Fn>
void guarded(TestContext& t, std::string const& what, Fn&& fn) {
    try {
        fn();
    } catch (std::exception const& error) {
        t.check(false, what + ": raised " + error.what());
    }
}

int main() {
    TestContext t;
    for (Pair const& p : pairs()) {
        guarded(t, p.name, [&] { test_pair(t, p); });
    }
    guarded(t, "Show All", [&] { test_show_all(t); });
    guarded(t, "dry run", [&] { test_dry_run(t); });
    guarded(t, "refusals", [&] { test_refusals(t); });
    return t.report();
}
