/*
 * roundtrip_viewstream.cpp -- W34-2.3: the 214 view-cell writer against
 * Project's own save, plus every refusal the writer offers.
 *
 * WHAT IS PROVEN.  The port opens PROJECT'S CONTROL of wave30-textstyle,
 * appends one Bold cell-format item through core/mppwrite/viewstream_edit.h
 * exactly as the command does (view uid 2, row uid 1, field 0x0b40000e, Bold
 * set, cell selection), and compares EVERY stream of the four documents with
 * Project's edit save through save_pair::compare_all: the four streams the
 * view writer owns are declared (their measured effect sets must be non-empty)
 * and there is NOT ONE KnownGap -- every unmasked byte of every stream must
 * equal Project's save.  The committed null save masks per-save noise.  This is
 * the whole-file form of brief rule 1 on the pair that measured the command.
 *
 * The structural assertions after the pair are independent of the comparison:
 * the target block at 42,576 keeps its offset and grows 42,692 -> 42,748 bytes
 * (outer length word 0x0000a6c4 -> 0x0000a6fc, both inner words 0x0000a6c0 ->
 * 0x0000a6f8, count 10 -> 11), the 56-byte item is the measured
 * (length 44, id 0x2240000c, kind 1, uid 1, field 0x0b40000e, flags
 * 0x01000003, font word 0x90000001), the five later VarMeta entries move +56,
 * the VarMeta +0x14 word and the 214/Props CV_iew scalar read 100,234, and no
 * stream outside {214/CV_iew/VarMeta, 214/CV_iew/Var2Data, 214/Props, Props14}
 * changed.
 *
 * W34-2.3 FOUND AND FIXED.  The first run of this pair test exposed that
 * W34-2.1's writer never rewrote the Var2Data block's OUTER length word (the
 * u32 at the block offset, which the block walk reads), so its own staged
 * geometry check would have failed before any byte was written.  The pair's
 * edit save moves it 0x0000a6c4 -> 0x0000a6fc; the writer now does too, and
 * its proof compares the block's four header words explicitly instead of
 * claiming the whole prefix is unchanged.  The pair test below is what
 * exercises that path.
 *
 * REFUSALS.  Every refusal branch a committed fixture's streams can reach is
 * exercised: zero/two edits; a view uid other than 2; the project summary row
 * (uid 0); field id 0; italic, underline, strikethrough, highlight colour and
 * font colour (each named); Bold not set; whole-row selection; an empty,
 * directory or missing-parent output path; and damaged input -- VarMeta not
 * 24 + 12*n, bad magic, a declared count that is not the entry count, a +0x14
 * word that disagrees with Var2Data, the 0xFFFFFFFF no-block offset, an entry
 * past the end of Var2Data, a block that runs past it, no entry naming a block
 * at offset 0, gapped blocks, trailing bytes, no target entry, two target
 * entries, a target payload shorter than its 16-byte header, a size word that
 * is not payload - 4, a wrong format word, an item id outside the family, an
 * item that runs past the payload, a non-zero pad byte, an item count that is
 * not the walked count, a block that already carries a 0x2240000c item, a
 * 214/Props with no CV_iew size tag, with two of them, and with a size value
 * that disagrees with Var2Data.  Each refusal asserts that the document was
 * not modified and that no output file appeared.
 *
 * NOT CONSTRUCTIBLE HERE: the writer also refuses a document that has no
 * 214/CV_iew/VarMeta, Var2Data or 214/Props stream.  CfbDocument has no
 * remove-stream operation and every committed .mpp carries all three, so that
 * branch is not exercised by this file; it is a plain has_stream() guard.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
#include "viewstream_edit.h"

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

using mppwrite::CfbDocument;
using mppwrite::VarEntry;
using mppwrite::ViewCellAttribute;
using mppwrite::ViewCellSelection;
using mppwrite::ViewCellStyleEdit;
using mppwrite::ViewCellStyleWrite;

/* The measured pair and the streams the edit owns. */
char const* kControl = "wave30-textstyle-control.mpp";
char const* kNull = "wave30-textstyle-control-null.mpp";
char const* kEdit = "wave30-textstyle.mpp";
char const* kViewMeta = "214/CV_iew/VarMeta";
char const* kViewVar2 = "214/CV_iew/Var2Data";
char const* kViewProps = "214/Props";
char const* kEdlVar2 = "214/CEdl/Var2Data";

/* The measured block and item. */
constexpr std::size_t kBlockOffset = 42576;
constexpr std::size_t kViewBlockSessionByte = 42678;  // the writer's masked payload byte
constexpr std::size_t kPayloadBefore = 42692;
constexpr std::size_t kPayloadAfter = 42748;
constexpr std::size_t kInsertAt = kBlockOffset + 4 + kPayloadBefore;
constexpr std::size_t kPropsValueAt = 9962;
constexpr std::uint32_t kPropsValueBefore = 100178;
constexpr std::uint32_t kPropsValueAfter = 100234;
constexpr std::uint32_t kMeasuredRowUid = 1;
constexpr std::uint32_t kMeasuredFieldId = 0x0B40000Eu;

/* The 56 bytes Project appended: u32 length, id, kind, then the 44-byte entry
 * (row uid, field id, flags, three colour words, second colour word, pattern,
 * font word). */
std::array<std::uint32_t, 14> const kMeasuredItem{{
    44, 0x2240000Cu, 1u, kMeasuredRowUid, kMeasuredFieldId, 0x01000003u,
    0xFF000000u, 0, 0, 0xFF000000u, 0, 0, 1, 0x90000001u}};

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

std::string out_path(std::string const& name) {
    return join(MPPWRITE_TEST_OUT, name);
}

std::string missing_path() { return out_path("w34-view-refused.mpp"); }

CfbDocument doc_from_data(std::string const& name) {
    return CfbDocument::from_file(join(MPPWRITE_TEST_DATA, name));
}

bool file_exists(std::string const& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::uint16_t u16_at(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}

std::uint32_t u32_at(std::vector<std::uint8_t> const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

void put_u32_at(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

void put_u16_at(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
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

CfbDocument with_stream(std::string const& control_name, std::string const& path,
                        std::vector<std::uint8_t> const& bytes) {
    CfbDocument doc = doc_from_data(control_name);
    doc.replace_stream(path, bytes);
    return doc;
}

std::vector<VarEntry> entries_of(CfbDocument const& doc,
                                 std::string const& path) {
    return mppwrite::read_var_entries(doc.read_stream(path));
}

/* The target (uid 2, key 0x20400006) entry and its block in a control. */
struct Target {
    std::size_t entry_index = 0;
    std::size_t block = 0;
    std::size_t payload = 0;
};

Target target_of(std::vector<VarEntry> const& entries,
                 std::vector<std::uint8_t> const& var2) {
    for (VarEntry const& entry : entries) {
        if (entry.mid == mppwrite::kMeasuredViewUid &&
            entry.field_id == mppwrite::kViewStateKey) {
            Target target;
            target.entry_index = entry.index;
            target.block = entry.offset;
            target.payload = u32_at(var2, entry.offset);
            return target;
        }
    }
    return {};
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

ViewCellStyleEdit measured_edit() {
    ViewCellStyleEdit edit;
    edit.view_uid = mppwrite::kMeasuredViewUid;
    edit.uid = kMeasuredRowUid;
    edit.field_id = kMeasuredFieldId;
    edit.attribute = ViewCellAttribute::bold;
    edit.selection = ViewCellSelection::cell;
    edit.enabled = true;
    return edit;
}

/* ---- the pair ----------------------------------------------------------- */

void test_textstyle_pair(TestContext& t) {
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kControl));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kNull));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kEdit));

    CfbDocument doc = doc_from_data(kControl);
    CfbDocument const before = doc;
    std::vector<std::uint8_t> const control_var2 = doc.read_stream(kViewVar2);
    std::vector<std::uint8_t> const control_meta = doc.read_stream(kViewMeta);
    std::vector<std::uint8_t> const control_props = doc.read_stream(kViewProps);
    t.equal(control_var2.size(), 100178, "pair: the control's Var2Data is 100,178 B");
    t.equal(control_meta.size(), 108, "pair: the control's VarMeta is 108 B");
    std::vector<VarEntry> const before_entries = entries_of(doc, kViewMeta);
    t.equal(before_entries.size(), 7, "pair: the control's VarMeta holds 7 entries");
    Target const target = target_of(before_entries, control_var2);
    t.equal(target.block, kBlockOffset, "pair: the target block starts at 42,576");
    t.equal(target.payload, kPayloadBefore, "pair: the target payload is 42,692 B");

    std::vector<ViewCellStyleEdit> const edits{measured_edit()};
    ViewCellStyleWrite const written = mppwrite::save_view_cell_style(
        doc, edits, out_path("w34-view-bold.mpp"));
    t.equal(written.view_uid, 2, "pair: view uid");
    t.equal(written.uid, kMeasuredRowUid, "pair: row uid");
    t.equal(written.field_id, kMeasuredFieldId, "pair: field id");
    t.equal(written.block_offset, kBlockOffset, "pair: block offset");
    t.equal(written.block_payload_before, kPayloadBefore,
            "pair: payload before");
    t.equal(written.block_payload_after, kPayloadAfter, "pair: payload after");
    t.equal(written.item_count_before, 10, "pair: item count before");
    t.equal(written.item_count_after, 11, "pair: item count after");
    t.equal(written.appended_bytes, 56, "pair: the item is 56 B");
    t.equal(written.var2_bytes_before, 100178, "pair: Var2Data before");
    t.equal(written.var2_bytes_after, 100234, "pair: Var2Data after");
    t.equal(written.repointed_entries, 5, "pair: five later entries move +56");
    t.equal(written.props_value_before, kPropsValueBefore,
            "pair: 214/Props size value before");
    t.equal(written.props_value_after, kPropsValueAfter,
            "pair: 214/Props size value after");

    /* The target block: outer length, inner size words, format, count. */
    std::vector<std::uint8_t> const var2 = doc.read_stream(kViewVar2);
    std::vector<std::uint8_t> const meta = doc.read_stream(kViewMeta);
    std::vector<std::uint8_t> const props = doc.read_stream(kViewProps);
    t.equal(var2.size(), 100234, "pair: the written Var2Data is 100,234 B");
    t.equal(u32_at(var2, kBlockOffset), kPayloadAfter,
            "pair: the block's outer length word grows with the payload");
    t.equal(u32_at(var2, kBlockOffset + 4), kPayloadAfter - 4,
            "pair: inner size word 1 is payload - 4");
    t.equal(u32_at(var2, kBlockOffset + 8), kPayloadAfter - 4,
            "pair: inner size word 2 is payload - 4");
    t.equal(u32_at(var2, kBlockOffset + 12), 0x00010000u,
            "pair: the format word is preserved");
    t.equal(u16_at(var2, kBlockOffset + 16), 11, "pair: the item count is 11");
    t.equal(u16_at(var2, kBlockOffset + 18),
            u16_at(control_var2, kBlockOffset + 18),
            "pair: the word beside the count is preserved");

    /* The appended item and the shifted tail. */
    bool item_ok = true;
    for (std::size_t i = 0; i < kMeasuredItem.size(); ++i) {
        item_ok = item_ok && u32_at(var2, kInsertAt + 4 * i) == kMeasuredItem[i];
    }
    t.check(item_ok, "pair: the appended item is the measured 56 bytes");
    t.check(std::equal(control_var2.begin() +
                           static_cast<std::ptrdiff_t>(kInsertAt),
                       control_var2.end(),
                       var2.begin() + static_cast<std::ptrdiff_t>(kInsertAt + 56)),
            "pair: the Var2Data tail is the control's bytes shifted by 56");
    t.check(std::equal(control_var2.begin(),
                       control_var2.begin() +
                           static_cast<std::ptrdiff_t>(kBlockOffset),
                       var2.begin()),
            "pair: the Var2Data before the block is the control's");

    /* VarMeta: same entries, five offsets +56, the new size word. */
    t.equal(meta.size(), 108, "pair: VarMeta keeps its size");
    t.equal(u32_at(meta, 8), 7, "pair: the declared entry count is kept");
    t.equal(u32_at(meta, 0x14), 100234, "pair: VarMeta +0x14 is the new size");
    std::vector<VarEntry> const after_entries = entries_of(doc, kViewMeta);
    t.equal(after_entries.size(), before_entries.size(),
            "pair: the entry count is kept");
    bool entries_ok = true;
    for (std::size_t i = 0; i < before_entries.size(); ++i) {
        std::size_t const expected =
            before_entries[i].offset +
            (before_entries[i].offset > kBlockOffset ? 56 : 0);
        entries_ok = entries_ok && after_entries[i].mid == before_entries[i].mid &&
                     after_entries[i].field_id == before_entries[i].field_id &&
                     after_entries[i].offset == expected;
    }
    t.check(entries_ok, "pair: every later entry is the control's +56");

    /* 214/Props: the CV_iew size value and nothing else. */
    t.equal(props.size(), control_props.size(), "pair: 214/Props keeps its size");
    t.equal(u32_at(props, kPropsValueAt), kPropsValueAfter,
            "pair: the CV_iew size value is 100,234");
    bool props_ok = true;
    for (std::size_t i = 0; i < props.size(); ++i) {
        bool const owned = i >= kPropsValueAt && i < kPropsValueAt + 4;
        props_ok = props_ok && (owned || props[i] == control_props[i]);
    }
    t.check(props_ok, "pair: 214/Props changed only in the size value word");

    check_only_owned_streams(t, before, doc, "pair",
                             {kViewMeta, kViewVar2, kViewProps, "Props14"});

    std::array<char const*, 4> const declared{{
        kViewMeta, kViewVar2, kViewProps, "Props14"}};
    save_pair::compare_all(
        t, "view-bold", doc_from_data(kControl), doc_from_data(kNull),
        doc_from_data(kEdit),
        CfbDocument::from_file(out_path("w34-view-bold.mpp")), nullptr, 0,
        declared.data(), declared.size());
}

/* ---- Bold off (W35-3.4) -------------------------------------------------- */

/* The pair: wave30-textstyle's Bold save, resaved by Project, then Bold turned
 * off on the same cell.  Project rewrites the item in place. */
char const* kOffControl = "wave35-g3-font-bold-off-control.mpp";
char const* kOffNull = "wave35-g3-font-bold-off-control-null.mpp";
char const* kOffEdit = "wave35-g3-font-bold-off.mpp";

ViewCellStyleEdit bold_off_edit() {
    ViewCellStyleEdit edit = measured_edit();
    edit.enabled = false;
    return edit;
}

void test_bold_off_pair(TestContext& t) {
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kOffControl));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kOffNull));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, kOffEdit));

    CfbDocument doc = doc_from_data(kOffControl);
    CfbDocument const before = doc;
    std::vector<std::uint8_t> const control_var2 = doc.read_stream(kViewVar2);
    ViewCellStyleWrite const written = mppwrite::save_view_cell_style(
        doc, {bold_off_edit()}, out_path("w35-view-bold-off.mpp"));
    t.equal(written.appended_bytes, 0, "bold off: no bytes are appended");
    t.equal(written.repointed_entries, 0, "bold off: no entry moves");
    t.equal(written.var2_bytes_after, written.var2_bytes_before,
            "bold off: Var2Data keeps its size");
    t.equal(written.item_count_after, written.item_count_before,
            "bold off: the item count is kept");

    std::vector<std::uint8_t> const var2 = doc.read_stream(kViewVar2);
    t.equal(var2.size(), control_var2.size(), "bold off: Var2Data is 100,234 B");
    std::size_t differing = 0;
    for (std::size_t i = 0; i < var2.size(); ++i) {
        if (var2[i] != control_var2[i]) {
            ++differing;
        }
    }
    t.equal(differing, 2, "bold off: exactly two bytes of Var2Data change");
    t.equal(u32_at(var2, kInsertAt + 12 + 8), 0x00000003u,
            "bold off: the flags word is 0x00000003");
    t.equal(u32_at(var2, kInsertAt + 12 + 40), 0x90000000u,
            "bold off: the font word is 0x90000000");
    check_only_owned_streams(t, before, doc, "bold off",
                             {kViewVar2, "Props14"});

    std::array<char const*, 2> const declared{{kViewVar2, "Props14"}};
    save_pair::compare_all(
        t, "view-bold-off", doc_from_data(kOffControl), doc_from_data(kOffNull),
        doc_from_data(kOffEdit),
        CfbDocument::from_file(out_path("w35-view-bold-off.mpp")), nullptr, 0,
        declared.data(), declared.size());

    /* A dry run builds and checks the same bytes and writes nothing. */
    CfbDocument dry = doc_from_data(kOffControl);
    std::vector<std::uint8_t> const dry_before = dry.to_bytes();
    ViewCellStyleWrite const planned =
        mppwrite::save_view_cell_style(dry, {bold_off_edit()}, std::string(), true);
    t.equal(planned.appended_bytes, 0, "bold off dry run: nothing appended");
    t.check(dry.to_bytes() == dry_before, "bold off dry run: the document changed");

    /* The shapes the pair does not measure. */
    std::string const missing = missing_path();
    auto refuses = [&](CfbDocument const& source, std::string_view needle,
                       std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument copy = source;
        std::vector<std::uint8_t> const copy_before = copy.to_bytes();
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_view_cell_style(
                             copy, {bold_off_edit()}, missing);
                     },
                     needle, what);
        t.check(copy.to_bytes() == copy_before, what + ": the document changed");
        t.check(!file_exists(missing), what + ": an output file was written");
    };
    refuses(doc_from_data(kControl), "exactly one cell format item",
            "Bold off on a view that carries no Bold item refused");
    {
        /* The item formats another attribute: flags 0x02000003 is Italic. */
        std::vector<std::uint8_t> var2_italic = control_var2;
        put_u32_at(var2_italic, kInsertAt + 12 + 8, 0x02000003u);
        refuses(with_stream(kOffControl, kViewVar2, var2_italic),
                "is not the measured Bold item",
                "Bold off on an Italic item refused");
    }
    {
        /* The item is on another row. */
        std::vector<std::uint8_t> var2_row = control_var2;
        put_u32_at(var2_row, kInsertAt + 12, 2);
        refuses(with_stream(kOffControl, kViewVar2, var2_row),
                "is not the measured Bold item",
                "Bold off on another row's item refused");
    }
    {
        /* The block is not the measured control's outside the item. */
        std::vector<std::uint8_t> var2_style = control_var2;
        var2_style[kBlockOffset + 4 + 20000] ^= 0x01;
        refuses(with_stream(kOffControl, kViewVar2, var2_style),
                "differs from the measured control's",
                "Bold off on a changed view record refused");
    }
    /* W35b-3.6 (R35G3-4): the four unbold_cell guards the cases above did not reach, each
     * with the needle of its own message. */
    {
        /* The Bold item is not the last item of the block: it swaps places with the item
         * before it (same bytes, same total, no pad: both items are an even length). */
        constexpr std::size_t kItemBeforeBytes = 32;  // 12 header + 20 payload (id 0x22400015)
        constexpr std::size_t kCellItemBytes = 56;    // 12 header + 44 payload (id 0x2240000c)
        t.equal(u32_at(control_var2, kInsertAt - kItemBeforeBytes + 4), 0x22400015u,
                "bold off, not last: the item before the Bold item is the measured one");
        t.equal(u32_at(control_var2, kInsertAt + 4), 0x2240000cu,
                "bold off, not last: the last item is the Bold item");
        std::vector<std::uint8_t> var2_swapped = control_var2;
        std::copy(control_var2.begin() + static_cast<std::ptrdiff_t>(kInsertAt),
                  control_var2.begin() + static_cast<std::ptrdiff_t>(kInsertAt + kCellItemBytes),
                  var2_swapped.begin() +
                      static_cast<std::ptrdiff_t>(kInsertAt - kItemBeforeBytes));
        std::copy(control_var2.begin() + static_cast<std::ptrdiff_t>(kInsertAt - kItemBeforeBytes),
                  control_var2.begin() + static_cast<std::ptrdiff_t>(kInsertAt),
                  var2_swapped.begin() +
                      static_cast<std::ptrdiff_t>(kInsertAt - kItemBeforeBytes + kCellItemBytes));
        refuses(with_stream(kOffControl, kViewVar2, var2_swapped),
                "is not the last item of the view block",
                "Bold off on an item that is not last refused");
    }
    {
        /* Two cell format items: the item before the Bold item carries the cell id as well
         * (the lengths are unchanged, so the block still walks). */
        std::vector<std::uint8_t> var2_two = control_var2;
        put_u32_at(var2_two, kInsertAt - 32 + 4, 0x2240000cu);
        refuses(with_stream(kOffControl, kViewVar2, var2_two), "this block carries 2",
                "Bold off on a block with two cell format items refused");
    }
    {
        /* The font word's top byte is not 0x90 (only 0x90 is accepted). */
        std::vector<std::uint8_t> var2_font = control_var2;
        t.equal(var2_font[kInsertAt + 12 + 40 + 3], 0x90, "bold off, font word: the top byte is 0x90");
        var2_font[kInsertAt + 12 + 40 + 3] = 0x80;
        refuses(with_stream(kOffControl, kViewVar2, var2_font),
                "is not the measured Bold item for row uid 1",
                "Bold off on a font word with another top byte refused");
    }
    {
        /* The item is already off (Project's own Bold-off save: flags 0x00000003, font word
         * 0x90000000), so there is no Bold item to turn off. */
        fixture::require_committed(join(MPPWRITE_TEST_DATA, kOffEdit));
        refuses(doc_from_data(kOffEdit), "is not the measured Bold item for row uid 1",
                "Bold off on an already unbolded item refused");
    }
    /* W35b-3.5 (R35G3-3): the digest masks payload byte 42,678 and only that byte.  The
     * neighbours either side are refused, so a mask widened to a window passes no test. */
    for (std::size_t const payload_at : {kViewBlockSessionByte - 1, kViewBlockSessionByte + 1}) {
        std::vector<std::uint8_t> var2_neighbour = control_var2;
        var2_neighbour[kBlockOffset + 4 + payload_at] ^= 0x01;
        refuses(with_stream(kOffControl, kViewVar2, var2_neighbour),
                "differs from the measured control's",
                "Bold off with payload byte " + std::to_string(payload_at) + " changed refused");
    }
    {
        /* The masked byte itself is another session's value and is accepted. */
        std::vector<std::uint8_t> var2_session = control_var2;
        var2_session[kBlockOffset + 4 + kViewBlockSessionByte] ^= 0x0F;
        CfbDocument other_session = with_stream(kOffControl, kViewVar2, var2_session);
        bool accepted = true;
        try {
            (void)mppwrite::save_view_cell_style(other_session, {bold_off_edit()},
                                                 out_path("w35b-view-bold-off-session.mpp"));
        } catch (mppwrite::Error const&) {
            accepted = false;
        }
        t.check(accepted, "Bold off accepts another session's value at payload byte 42678");
    }
    {
        ViewCellStyleEdit edit = bold_off_edit();
        edit.selection = ViewCellSelection::whole_row;
        CfbDocument copy = doc_from_data(kOffControl);
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_view_cell_style(copy, {edit},
                                                              std::string(), true);
                     },
                     "whole-row selection", "Bold off on a whole row refused");
    }
}

/* ---- refusals ----------------------------------------------------------- */

void test_edit_refusals(TestContext& t) {
    std::string const missing = missing_path();
    auto refuses = [&](CfbDocument const& source,
                       std::vector<ViewCellStyleEdit> const& edits,
                       std::string_view needle, std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        std::vector<std::uint8_t> const before = doc.to_bytes();
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_view_cell_style(doc, edits,
                                                              missing);
                     },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
        t.check(!file_exists(missing), what + ": an output file was written");
    };

    CfbDocument const control = doc_from_data(kControl);

    /* The edit model: exactly one item, the measured view, row, field,
     * attribute, direction and selection. */
    {
        refuses(control, {}, "exactly one cell-format item",
                "zero edits refused");
        std::vector<ViewCellStyleEdit> two{measured_edit(), measured_edit()};
        refuses(control, two, "exactly one cell-format item",
                "two edits refused");
    }
    {
        ViewCellStyleEdit edit = measured_edit();
        edit.view_uid = 3;
        refuses(control, {edit}, "is not the measured applied view",
                "view uid 3 refused");
    }
    {
        ViewCellStyleEdit edit = measured_edit();
        edit.uid = 0;
        refuses(control, {edit}, "uid 0 is the project summary row",
                "the project summary row refused");
    }
    {
        ViewCellStyleEdit edit = measured_edit();
        edit.field_id = 0;
        refuses(control, {edit}, "field id 0 is not a stored field",
                "field id 0 refused");
    }
    /* W35-0.4 RC-W8: the pair formats one cell, the Task Name cell of row uid
     * 1; the writer stored any other row uid or field id verbatim. */
    for (std::uint32_t const uid : {2u, 3u, 0xFFFFFFFFu}) {
        ViewCellStyleEdit edit = measured_edit();
        edit.uid = uid;
        refuses(control, {edit}, "is not the measured row uid",
                "row uid " + std::to_string(uid) + " refused");
    }
    for (std::uint32_t const field_id : {0x0B40000Fu, 0x0B400001u, 0x0B40000Du,
                                         0x0C40000Eu, 1u}) {
        ViewCellStyleEdit edit = measured_edit();
        edit.field_id = field_id;
        /* W35b-4.G0-11 (R35G0-11): the refusal prints both field ids in hex, the form the
         * ids are written in everywhere else (never 188743694 for 0x0B40000E). */
        char given[16];
        std::snprintf(given, sizeof given, "0x%08x", field_id);
        refuses(control, {edit},
                std::string("field id ") + given +
                    " is not the measured field id (0x0b40000e, the Task Name cell)",
                "field id " + std::to_string(field_id) + " refused");
    }
    std::array<std::pair<ViewCellAttribute, char const*>, 5> const siblings{{
        {ViewCellAttribute::italic, "italic"},
        {ViewCellAttribute::underline, "underline"},
        {ViewCellAttribute::strikethrough, "strikethrough"},
        {ViewCellAttribute::highlight_colour, "highlight colour"},
        {ViewCellAttribute::font_colour, "font colour"}}};
    for (auto const& sibling : siblings) {
        ViewCellStyleEdit edit = measured_edit();
        edit.attribute = sibling.first;
        refuses(control, {edit}, "only Bold is measured",
                std::string(sibling.second) + " refused");
    }
    {
        ViewCellStyleEdit edit = measured_edit();
        edit.enabled = false;
        refuses(control, {edit}, "turning Bold off",
                "Bold off on a block with no Bold item refused");
    }
    {
        ViewCellStyleEdit edit = measured_edit();
        edit.selection = ViewCellSelection::whole_row;
        refuses(control, {edit}, "whole-row selection",
                "whole-row selection refused");
    }

    /* R34G4-4: the view the writer edits must be the file's applied view, and
     * its record must be the measured control's.  The two committed files that
     * apply another view (the Resource Sheet, uid 4, and Resource Usage, uid 5)
     * each hold a view with uid 2 and the measured shape of everything else. */
    refuses(doc_from_data("wave31-resource-sheet.mpp"), {measured_edit()},
            "(CV_iew uid 4), not the measured view uid 2",
            "a file whose applied view is the Resource Sheet refused");
    refuses(doc_from_data("wave31-usage.mpp"), {measured_edit()},
            "(CV_iew uid 5), not the measured view uid 2",
            "a file whose applied view is Resource Usage refused");
    {
        /* Same file, CEdl re-pointed at a caption no view has. */
        std::vector<std::uint8_t> edl = control.read_stream(kEdlVar2);
        std::string const name = "&Gantt Chart";
        std::vector<std::uint8_t> needle;
        for (char const ch : name) {
            needle.push_back(static_cast<std::uint8_t>(ch));
            needle.push_back(0);
        }
        auto const at = std::search(edl.begin(), edl.end(), needle.begin(), needle.end());
        t.check(at != edl.end(), "the control's CEdl holds the applied view name");
        if (at != edl.end()) {
            *(at + 2 * 11) = static_cast<std::uint8_t>('2');
            refuses(with_stream(kControl, kEdlVar2, edl), {measured_edit()},
                    "matches 0 CV_iew rows", "an applied view no CV_iew row names refused");
        }
    }
    {
        /* One byte of the uid-2 record's text style differs from the measured
         * control: the block still walks, so only the byte comparison can see it. */
        std::vector<std::uint8_t> var2 = control.read_stream(kViewVar2);
        var2[kBlockOffset + 4 + 20000] ^= 0x01;
        refuses(with_stream(kControl, kViewVar2, var2), {measured_edit()},
                "differs from the measured control's",
                "a view record that is not the measured control's refused");
    }
    /* W35b-3.5 (R35G3-3): Bold on is the enabled direction, and its digest masks payload
     * byte 42,678 (a session-date float32, spec/writers/view-stream-214.md) and only that
     * byte; the neighbours either side are refused.  The masked byte itself is accepted: a
     * control another Project session saved differs there and nowhere else. */
    for (std::size_t const payload_at : {kViewBlockSessionByte - 1, kViewBlockSessionByte + 1}) {
        std::vector<std::uint8_t> var2 = control.read_stream(kViewVar2);
        var2[kBlockOffset + 4 + payload_at] ^= 0x01;
        refuses(with_stream(kControl, kViewVar2, var2), {measured_edit()},
                "differs from the measured control's",
                "Bold on with payload byte " + std::to_string(payload_at) + " changed refused");
    }
    {
        std::vector<std::uint8_t> var2 = control.read_stream(kViewVar2);
        var2[kBlockOffset + 4 + kViewBlockSessionByte] ^= 0x0F;
        CfbDocument other_session = with_stream(kControl, kViewVar2, var2);
        bool accepted = true;
        try {
            (void)mppwrite::save_view_cell_style(other_session, {measured_edit()},
                                                 out_path("w35b-view-bold-on-session.mpp"));
        } catch (mppwrite::Error const&) {
            accepted = false;
        }
        t.check(accepted, "Bold on accepts another session's value at payload byte 42678");
    }
}

/* ---- the dry run (W34-4.5) ----------------------------------------------- */

void test_dry_run(TestContext& t) {
    CfbDocument const control = doc_from_data(kControl);
    std::vector<ViewCellStyleEdit> const edits{measured_edit()};
    std::string const target = out_path("w34-view-dry-run.mpp");
    std::error_code ignored;
    std::filesystem::remove(target, ignored);

    /* The measured shape: the plan equals the real write's counts, nothing is
     * written and the document is untouched; an empty path is allowed. */
    CfbDocument doc = control;
    std::vector<std::uint8_t> const before = doc.to_bytes();
    ViewCellStyleWrite const planned =
        mppwrite::save_view_cell_style(doc, edits, target, true);
    t.check(doc.to_bytes() == before, "dry run: the document changed");
    t.check(!file_exists(target), "dry run: an output file was written");
    t.equal(planned.block_offset, kBlockOffset, "dry run: block offset");
    t.equal(planned.block_payload_before, kPayloadBefore, "dry run: payload before");
    t.equal(planned.block_payload_after, kPayloadAfter, "dry run: payload after");
    t.equal(planned.item_count_before, 10, "dry run: item count before");
    t.equal(planned.item_count_after, 11, "dry run: item count after");
    t.equal(planned.appended_bytes, 56, "dry run: the item is 56 B");
    t.equal(planned.var2_bytes_before, 100178, "dry run: Var2Data before");
    t.equal(planned.var2_bytes_after, 100234, "dry run: Var2Data after");
    t.equal(planned.repointed_entries, 5, "dry run: five later entries move");
    t.equal(planned.props_value_before, kPropsValueBefore, "dry run: props before");
    t.equal(planned.props_value_after, kPropsValueAfter, "dry run: props after");
    ViewCellStyleWrite const empty_path =
        mppwrite::save_view_cell_style(doc, edits, std::string(), true);
    t.equal(empty_path.var2_bytes_after, 100234, "dry run: an empty path plans too");
    t.check(doc.to_bytes() == before, "dry run (empty path): the document changed");

    /* The real write agrees with the plan. */
    ViewCellStyleWrite const written =
        mppwrite::save_view_cell_style(doc, edits, out_path("w34-view-dry-real.mpp"));
    t.equal(written.var2_bytes_after, planned.var2_bytes_after, "dry run: write agrees (Var2Data)");
    t.equal(written.block_payload_after, planned.block_payload_after,
            "dry run: write agrees (payload)");
    t.equal(written.repointed_entries, planned.repointed_entries,
            "dry run: write agrees (entries)");

    /* A refusal is raised by the dry run too, and still leaves the document. */
    auto refuses = [&](CfbDocument const& source,
                       std::vector<ViewCellStyleEdit> const& bad,
                       std::string_view needle, std::string const& what) {
        CfbDocument copy = source;
        std::vector<std::uint8_t> const copy_before = copy.to_bytes();
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_view_cell_style(copy, bad, std::string(), true);
                     },
                     needle, what);
        t.check(copy.to_bytes() == copy_before, what + ": the document changed");
    };
    {
        ViewCellStyleEdit edit = measured_edit();
        edit.view_uid = 3;
        refuses(control, {edit}, "is not the measured applied view", "dry run: view uid 3 refused");
    }
    {
        ViewCellStyleEdit edit = measured_edit();
        edit.attribute = ViewCellAttribute::italic;
        refuses(control, {edit}, "only Bold is measured", "dry run: italic refused");
    }
    refuses(control, {}, "exactly one cell-format item", "dry run: zero edits refused");
    refuses(doc_from_data("wave31-resource-sheet.mpp"), edits,
            "(CV_iew uid 4), not the measured view uid 2",
            "dry run: a file whose applied view is not uid 2 refused");
    /* A view that already carries a cell format item (Project's own save). */
    refuses(doc_from_data(kEdit), edits, "already carries a cell", "dry run: a formatted view refused");
}

void test_path_refusals(TestContext& t) {
    std::vector<ViewCellStyleEdit> const edits{measured_edit()};
    CfbDocument const control = doc_from_data(kControl);
    auto refuses = [&](std::string const& path, std::string_view needle,
                       std::string const& what) {
        CfbDocument doc = control;
        std::vector<std::uint8_t> const before = doc.to_bytes();
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_view_cell_style(doc, edits, path);
                     },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
    };
    refuses("", "the output path is empty", "an empty path refused");
    refuses(MPPWRITE_TEST_OUT, "is a directory", "a directory path refused");
    refuses(out_path("no-such-dir/w34-view-bold.mpp"), "does not exist",
            "a missing parent directory refused");
    t.check(!file_exists(out_path("no-such-dir/w34-view-bold.mpp")),
            "a missing parent directory: an output file was written");
}

void test_damaged_streams(TestContext& t) {
    std::string const missing = missing_path();
    auto refuses = [&](CfbDocument const& source, std::string_view needle,
                       std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        std::vector<std::uint8_t> const before = doc.to_bytes();
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_view_cell_style(
                             doc, {measured_edit()}, missing);
                     },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
        t.check(!file_exists(missing), what + ": an output file was written");
    };

    CfbDocument const control = doc_from_data(kControl);
    std::vector<std::uint8_t> const var2 = control.read_stream(kViewVar2);
    std::vector<std::uint8_t> const props = control.read_stream(kViewProps);
    std::vector<std::uint8_t> const meta = control.read_stream(kViewMeta);
    std::vector<VarEntry> const entries = entries_of(control, kViewMeta);
    Target const target = target_of(entries, var2);

    {
        std::vector<std::uint8_t> damaged = meta;
        damaged.push_back(0);
        refuses(with_stream(kControl, kViewMeta, damaged), "not 24 + 12*n",
                "a VarMeta that is not 24 + 12*n refused");
    }
    {
        std::vector<std::uint8_t> damaged = meta;
        put_u32_at(damaged, 0, 0);
        refuses(with_stream(kControl, kViewMeta, damaged),
                "does not start with the measured magic",
                "a bad VarMeta magic refused");
    }
    {
        std::vector<std::uint8_t> damaged = meta;
        put_u32_at(damaged, 8, 999);
        refuses(with_stream(kControl, kViewMeta, damaged),
                "declares 999 entries", "a stale VarMeta count refused");
    }
    {
        std::vector<std::uint8_t> damaged = meta;
        put_u32_at(damaged, mppwrite::kVarMetaVar2SizeOffset,
                   static_cast<std::uint32_t>(var2.size() + 4));
        refuses(with_stream(kControl, kViewMeta, damaged),
                "size word says", "a VarMeta size word that disagrees refused");
    }
    {
        std::vector<std::uint8_t> damaged = meta;
        put_u32_at(damaged, mppwrite::kVarMetaHeaderSize + 4, 0xFFFFFFFFu);
        refuses(with_stream(kControl, kViewMeta, damaged), "no-block offset",
                "the 0xFFFFFFFF offset refused");
    }
    {
        std::vector<std::uint8_t> damaged = meta;
        put_u32_at(damaged, mppwrite::kVarMetaHeaderSize + 4,
                   static_cast<std::uint32_t>(var2.size()));
        refuses(with_stream(kControl, kViewMeta, damaged),
                "is past the end of Var2Data",
                "an entry offset past Var2Data refused");
    }
    for (std::uint32_t const wild : {0xFFFFFFFCu, 0xFFFFFFFDu, 0xFFFFFFFEu}) {
        /* W34b-1.1 (R34G3-3): an offset within four of 2^32 is past the end, not
         * a small number after a wrapped +4. */
        std::vector<std::uint8_t> damaged = meta;
        put_u32_at(damaged, mppwrite::kVarMetaHeaderSize + 4, wild);
        refuses(with_stream(kControl, kViewMeta, damaged),
                "is past the end of Var2Data",
                "an entry offset " + std::to_string(wild) +
                    " (32-bit wrap) refused");
    }
    {
        std::vector<std::uint8_t> damaged = var2;
        put_u32_at(damaged, 0, 0x7FFFFFFFu);
        refuses(with_stream(kControl, kViewVar2, damaged),
                "runs past the end of Var2Data",
                "a block that runs past Var2Data refused");
    }
    {
        /* Point the first entry at block 1, so no entry names offset 0; every
         * entry still addresses a valid block. */
        std::vector<std::uint8_t> damaged = meta;
        put_u32_at(damaged, mppwrite::kVarMetaHeaderSize + 4, entries[1].offset);
        refuses(with_stream(kControl, kViewMeta, damaged),
                "does not begin with a block start",
                "a Var2Data that does not begin at a block refused");
    }
    {
        std::vector<std::uint8_t> damaged = var2;
        put_u32_at(damaged, 0, u32_at(var2, 0) + 2);
        refuses(with_stream(kControl, kViewVar2, damaged),
                "are not back to back", "gapped blocks refused");
    }
    {
        std::vector<std::uint8_t> damaged = var2;
        damaged.push_back(0);
        damaged.push_back(0);
        std::vector<std::uint8_t> damaged_meta = meta;
        put_u32_at(damaged_meta, mppwrite::kVarMetaVar2SizeOffset,
                   static_cast<std::uint32_t>(damaged.size()));
        CfbDocument doc = with_stream(kControl, kViewVar2, damaged);
        doc.replace_stream(kViewMeta, damaged_meta);
        refuses(doc, "has trailing bytes after its last block",
                "trailing Var2Data bytes refused");
    }
    {
        std::vector<std::uint8_t> damaged = meta;
        put_u32_at(damaged,
                   mppwrite::kVarMetaHeaderSize + target.entry_index * 12 + 8,
                   0x20400007u);
        refuses(with_stream(kControl, kViewMeta, damaged),
                "has no main block entry",
                "a missing target entry refused");
    }
    {
        /* Make entry 0 (uid 1, key 0x20400006) a second uid-2 main entry. */
        std::vector<std::uint8_t> damaged = meta;
        put_u32_at(damaged, mppwrite::kVarMetaHeaderSize, 2);
        refuses(with_stream(kControl, kViewMeta, damaged),
                "holds two entries for the view main block",
                "two target entries refused");
    }
    {
        /* Cut the target payload to 8 bytes and shift every later block back,
         * so the block walk still closes and only the payload check can fail. */
        std::size_t const delta = target.payload - 8;
        std::vector<std::uint8_t> damaged(
            var2.begin(),
            var2.begin() + static_cast<std::ptrdiff_t>(target.block + 12));
        damaged.insert(damaged.end(),
                       var2.begin() +
                           static_cast<std::ptrdiff_t>(target.block + 4 +
                                                       target.payload),
                       var2.end());
        put_u32_at(damaged, target.block, 8);
        std::vector<std::uint8_t> damaged_meta = meta;
        for (VarEntry const& entry : entries) {
            if (entry.offset > target.block) {
                put_u32_at(damaged_meta,
                           mppwrite::kVarMetaHeaderSize + entry.index * 12 + 4,
                           static_cast<std::uint32_t>(entry.offset - delta));
            }
        }
        put_u32_at(damaged_meta, mppwrite::kVarMetaVar2SizeOffset,
                   static_cast<std::uint32_t>(damaged.size()));
        CfbDocument doc = with_stream(kControl, kViewVar2, damaged);
        doc.replace_stream(kViewMeta, damaged_meta);
        refuses(doc, "shorter than its 16-byte header",
                "a target payload shorter than its header refused");
    }
    {
        std::vector<std::uint8_t> damaged = var2;
        put_u32_at(damaged, target.block + 4, u32_at(var2, target.block + 4) - 4);
        refuses(with_stream(kControl, kViewVar2, damaged),
                "size words are not payload - 4",
                "a wrong block size word refused");
    }
    {
        std::vector<std::uint8_t> damaged = var2;
        put_u32_at(damaged, target.block + 12, 0);
        refuses(with_stream(kControl, kViewVar2, damaged),
                "format word is not", "a wrong block format word refused");
    }
    {
        std::vector<std::uint8_t> damaged = var2;
        put_u32_at(damaged, target.block + 4 + 16 + 4, 0x12345678u);
        refuses(with_stream(kControl, kViewVar2, damaged),
                "which is not a view property id",
                "an item id outside the family refused");
    }
    {
        std::vector<std::uint8_t> damaged = var2;
        put_u32_at(damaged, target.block + 4 + 16, 0x7FFFFFu);
        refuses(with_stream(kControl, kViewVar2, damaged),
                "runs past the view block", "an over-long item refused");
    }
    {
        std::vector<std::uint8_t> damaged = var2;
        damaged[target.block + 4 + 65] = 1;
        refuses(with_stream(kControl, kViewVar2, damaged),
                "non-zero pad byte", "a non-zero item pad refused");
    }
    {
        std::vector<std::uint8_t> damaged = var2;
        put_u16_at(damaged, target.block + 16, 9);
        refuses(with_stream(kControl, kViewVar2, damaged),
                "trailing byte(s) after 9 item(s)",
                "an item count that is not the walked count refused");
    }
    {
        fixture::require_committed(join(MPPWRITE_TEST_DATA, kEdit));
        refuses(doc_from_data(kEdit), "already carries a cell format item",
                "a block that already carries the item refused");
    }
    {
        /* Remove the 214/Props tag's bytes (the entry stays, unfindable). */
        std::vector<std::uint8_t> damaged = props;
        put_u32_at(damaged, kPropsValueAt - 8, 0);
        refuses(with_stream(kControl, kViewProps, damaged),
                "has no scalar record for tag", "a missing CV_iew tag refused");
    }
    {
        std::vector<std::uint8_t> damaged = props;
        damaged.insert(damaged.end(), props.begin() +
                                          static_cast<std::ptrdiff_t>(
                                              kPropsValueAt - 12),
                       props.begin() +
                           static_cast<std::ptrdiff_t>(kPropsValueAt + 4));
        put_u32_at(damaged, 0, static_cast<std::uint32_t>(damaged.size() - 4));
        put_u32_at(damaged, 4, static_cast<std::uint32_t>(damaged.size() - 4));
        refuses(with_stream(kControl, kViewProps, damaged),
                "more than one scalar record", "a duplicated CV_iew tag refused");
    }
    {
        std::vector<std::uint8_t> damaged = props;
        put_u32_at(damaged, kPropsValueAt, kPropsValueBefore + 1);
        refuses(with_stream(kControl, kViewProps, damaged),
                "size words disagree", "a CV_iew size value that disagrees refused");
    }
}

} /* namespace */

int main() {
    try {
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);
        TestContext t;
        test_textstyle_pair(t);
        test_bold_off_pair(t);
        test_edit_refusals(t);
        test_path_refusals(t);
        test_damaged_streams(t);
        test_dry_run(t);
        return t.report();
    } catch (std::exception const& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << std::endl;
        return 1;
    }
}
