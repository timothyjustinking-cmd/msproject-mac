/*
 * roundtrip_varmeta.cpp -- W34-1.2: the single-record Var2Data writer against
 * Project's own saves, for every shape it enables, plus every refusal.
 *
 * WHAT IS PROVEN.  For each committed save pair the port opens PROJECT'S
 * CONTROL, applies exactly one edit through core/mppwrite/varmeta_edit.h and
 * compares every stream with Project's EDIT save through
 * save_pair::compare_all, with the pair's null save masking per-save noise and
 * NOT ONE KnownGap: every unmasked byte of every stream must equal Project's.
 *
 *   shape    pair                    table     row/key                 what moves
 *   append   wave30-notes            Task      uid 2 / 0x0b40000f       VarMeta 42 -> 43,
 *                                                                       block at 516 (198 B),
 *                                                                       Var2Data 516 -> 718,
 *                                                                       FixedMeta +2 +1,
 *                                                                       +14/+44 |= 0x10
 *   re-point wave34-note-replace     Task      uid 2 / 0x0b40000f       entry 524 -> 725,
 *                                                                       197 -> 209 B,
 *                                                                       Var2Data 725 -> 938
 *   re-point wave30-resource-rename  Resource  uid 2 / 0x0c400001       114 -> 140, 6 -> 8 B,
 *                                                                       Var2Data 140 -> 152
 *   remove   wave34-field-clear      Task      uid 2 / 0x0b400033       entry 21 erased,
 *                                                                       count 43 -> 42, stream
 *                                                                       stays 540 B, freed 0 -> 40
 *   remove   wave34-note-clear       Task      uid 2 / 0x0b40000f       entry 21 erased,
 *                                                                       count 43 -> 42, stream
 *                                                                       stays 540 B, freed 0 -> 188
 *
 * The append and re-point payloads are the CALLER'S string: this writer stores
 * a payload it is given, and the note encoder is not part of it, so the test
 * reads the block Project stored from the pair's edit save and hands it back as
 * the command's input.  The resource NAME is decoded from Project's block to
 * UTF-8 and passed to rename_resource, which re-encodes it; no task or resource
 * text is written into this file.
 *
 * The structural assertions after each pair are independent of the comparison:
 * the entry position and offset, the FixedMeta +2 word, the presence bits, the
 * Var2Data length, the 114/Props size and freed words, and -- for a removal --
 * that the stream length is kept and the freed last slot is a byte-identical
 * duplicate of the new last logical entry.
 *
 * W34-1.3, THE FIRST CONSUMER.  The resource-rename pair above goes through
 * `rename_resource` (the command), and test_rename_resource_contract() adds the
 * consumer's own contract: an independent read-back of the name from the file
 * the command wrote (entry count, offset, and the exact stored bytes), and its
 * refusals -- an empty name, an embedded NUL, a name past 65,535 bytes, an
 * unknown uid, a shorter name and the control's own equal-size name (the
 * shrink and equal-size re-points are unmeasured and refused).
 *
 * REFUSALS.  Every refusal the writer offers is exercised here: a count other
 * than one, an unknown table, the project row, key 0, an unknown uid, a
 * duplicated uid, a dead FixedMeta state, damaged table geometry, a VarMeta
 * that is not 24 + 12*n, a stale tail on the way in, a +0x14 word that
 * disagrees with Var2Data, an append onto a key that exists (plain and family),
 * an append or remove on Resource, an empty append payload, a re-point with no
 * / two entries, a family entry, a 0xFFFFFFFF offset, a zero-length old block,
 * an equal or smaller re-point payload, a remove with a payload, a remove with
 * no entry, presence bytes outside the row entry, a multi-bit mask, a duplicate
 * presence operation, and an empty or unusable output path.  Each refusal also
 * checks that the document was not modified and that no output file appeared.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "fixture_guard.h"
#include "save_pair_compare.h"
#include "table.h"
#include "varmeta_edit.h"

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

using mppwrite::CfbDocument;
using mppwrite::VarPresenceBit;
using mppwrite::VarRecordEdit;
using mppwrite::VarRecordShape;
using mppwrite::VarRecordWrite;

char const* kTaskVarMeta = "114/TBkndTask/VarMeta";
char const* kTaskVar2 = "114/TBkndTask/Var2Data";
char const* kTaskFixedMeta = "114/TBkndTask/FixedMeta";
char const* kTaskFixed = "114/TBkndTask/FixedData";
char const* kTaskFixed2 = "114/TBkndTask/Fixed2Data";
char const* kTaskFixed2Meta = "114/TBkndTask/Fixed2Meta";
char const* kRscVarMeta = "114/TBkndRsc/VarMeta";
char const* kRscVar2 = "114/TBkndRsc/Var2Data";
char const* kProps = "114/Props";

/* Task/Notes and Task/Text1: the measured keys the pairs move. */
constexpr std::uint32_t kNotesKey = 0x0B40000Fu;
constexpr std::uint32_t kText1Key = 0x0B400033u;

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
    /* compare_all reports a matching KnownGap through this; with no gaps in
     * this file it can only be reached if one is added wrongly. */
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

/* Every entry of a table, for the structural assertions. */
std::vector<mppwrite::VarEntry> entries_of(CfbDocument const& doc,
                                           std::string const& var_meta_path) {
    return mppwrite::read_var_entries(doc.read_stream(var_meta_path));
}

/* The one stored block for (uid, key) in a COMMITTED PROJECT FILE.  Used as
 * the command's payload input (see the file comment). */
std::vector<std::uint8_t> stored_block(std::string const& file,
                                       std::string const& var_meta_path,
                                       std::string const& var2_path,
                                       std::uint32_t uid,
                                       std::uint32_t field_id) {
    CfbDocument const doc = doc_from_data(file);
    std::vector<std::uint8_t> const var2 = doc.read_stream(var2_path);
    for (mppwrite::VarEntry const& entry : entries_of(doc, var_meta_path)) {
        if (entry.mid == uid && entry.field_id == field_id) {
            return mppwrite::read_var_block(var2, entry.offset);
        }
    }
    return {};
}

/* The FixedMeta entry for a Task/Resource row: entry index = row + 3. */
std::size_t meta_base_for_row(std::size_t entry_size, std::size_t row) {
    return 16 + (row + 3) * entry_size;
}

/* The offset of the 16-byte 114/Props scalar group carrying `tag_be`, found
 * the way the writer finds it (a byte-wise scan for [u32 4][tag][u32 4][u32]),
 * or npos. */
std::size_t props_group_for(std::vector<std::uint8_t> const& props,
                            std::uint32_t tag_be) {
    for (std::size_t at = 0; at + 16 <= props.size(); ++at) {
        if (u32_at(props, at) != 4) {
            continue;
        }
        std::uint32_t const tag = (static_cast<std::uint32_t>(props[at + 4]) << 24) |
                                  (static_cast<std::uint32_t>(props[at + 5]) << 16) |
                                  (static_cast<std::uint32_t>(props[at + 6]) << 8) |
                                  static_cast<std::uint32_t>(props[at + 7]);
        if (tag == tag_be) {
            return at;
        }
    }
    return static_cast<std::size_t>(-1);
}

void check_only_owned_streams(TestContext& t, CfbDocument const& before,
                              CfbDocument const& after, std::string const& label,
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

/* ---- the five committed pairs ------------------------------------------ */

/*
 * APPEND.  Project's wave30-notes save adds Task/Notes to uid 2: the entry is
 * inserted at index 21 (key order, inside uid 2's group), the 198-byte block is
 * appended at the control's Var2Data length 516, the declared count 42 -> 43,
 * the row's FixedMeta +2 11 -> 12 and bits +14 0x10 / +44 0x10 are set, and the
 * 114/Props Task size word 516 -> 718.
 */
void test_append_pair(TestContext& t) {
    fixture::require_committed(join(MPPWRITE_TEST_DATA, "wave30-notes-control.mpp"));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, "wave30-notes.mpp"));
    std::vector<std::uint8_t> const payload =
        stored_block("wave30-notes.mpp", kTaskVarMeta, kTaskVar2, 2, kNotesKey);
    t.equal(payload.size(), 198, "append: Project's stored Notes payload is 198 B");

    CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
    CfbDocument const before = doc;
    std::size_t const var2_before = doc.read_stream(kTaskVar2).size();
    t.equal(var2_before, 516, "append: the control's Var2Data is 516 B");

    VarRecordEdit edit;
    edit.uid = 2;
    edit.field_id = kNotesKey;
    edit.shape = VarRecordShape::append;
    edit.payload = payload;
    edit.presence = {{14, 0x10, true}, {44, 0x10, true}};
    std::vector<VarRecordEdit> const edits{edit};
    VarRecordWrite const written =
        mppwrite::save_var_record(doc, mppwrite::kVarTaskTable, edits,
                                  out_path("w34-var-append.mpp"));
    t.equal(written.entries_before, 42, "append: entries before");
    t.equal(written.entries_after, 43, "append: entries after");
    t.equal(written.row_entries_before, 11, "append: row entries before");
    t.equal(written.row_entries_after, 12, "append: row entries after");
    t.equal(written.freed_bytes_before, 0, "append: freed bytes before");
    t.equal(written.freed_bytes_after, 0, "append: an append frees nothing");
    t.equal(written.new_offset, var2_before,
            "append: the block lands at the old Var2Data end");
    t.equal(written.new_payload_bytes, 198, "append: payload bytes");
    t.equal(written.var2_bytes_after, 718, "append: Var2Data 516 -> 718");

    /* Structure, independent of Project's file. */
    t.equal(entries_of(doc, kTaskVarMeta).size(), 43,
            "append: the written VarMeta holds 43 entries");
    std::vector<std::uint8_t> const meta = doc.read_stream(kTaskFixedMeta);
    std::size_t const base = meta_base_for_row(47, 2);
    t.equal(u16_at(meta, base + 2), 12, "append: FixedMeta +2 is 12");
    t.equal(meta[base + 14], 0x77, "append: FixedMeta +14 has 0x10");
    t.equal(meta[base + 44], 0x10, "append: FixedMeta +44 has 0x10");
    check_only_owned_streams(t, before, doc, "append",
                             {"114/TBkndTask/VarMeta", "114/TBkndTask/Var2Data",
                              "114/TBkndTask/FixedMeta", "114/Props",
                              "Props14"});

    std::array<char const*, 5> const declared{{
        "114/Props", "114/TBkndTask/FixedMeta", "114/TBkndTask/Var2Data",
        "114/TBkndTask/VarMeta", "Props14"}};
    save_pair::compare_all(t, "var-append", doc_from_data("wave30-notes-control.mpp"),
                           doc_from_data("wave30-notes-control-null.mpp"),
                           doc_from_data("wave30-notes.mpp"),
                           CfbDocument::from_file(out_path("w34-var-append.mpp")),
                           nullptr, 0, declared.data(), declared.size());
}

/*
 * RE-POINT (Task).  Project's wave34-note-replace save moves uid 2's Notes
 * entry from 524 to 725 (the control's Var2Data end), stores 209 B instead of
 * 197, updates the header size word 725 -> 938 and the 114/Props Task size
 * word; the count, FixedMeta and freed word do not move.
 */
void test_repoint_pair(TestContext& t) {
    fixture::require_committed(
        join(MPPWRITE_TEST_DATA, "wave34-note-replace-control.mpp"));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, "wave34-note-replace.mpp"));
    std::vector<std::uint8_t> const payload = stored_block(
        "wave34-note-replace.mpp", kTaskVarMeta, kTaskVar2, 2, kNotesKey);
    t.equal(payload.size(), 209, "re-point: Project's stored Notes payload is 209 B");

    CfbDocument doc = doc_from_data("wave34-note-replace-control.mpp");
    CfbDocument const before = doc;
    std::size_t const var2_before = doc.read_stream(kTaskVar2).size();
    t.equal(var2_before, 725, "re-point: the control's Var2Data is 725 B");

    VarRecordEdit edit;
    edit.uid = 2;
    edit.field_id = kNotesKey;
    edit.shape = VarRecordShape::repoint;
    edit.payload = payload;
    std::vector<VarRecordEdit> const edits{edit};
    VarRecordWrite const written =
        mppwrite::save_var_record(doc, mppwrite::kVarTaskTable, edits,
                                  out_path("w34-var-repoint.mpp"));
    t.equal(written.old_offset, 524, "re-point: the old block is at 524");
    t.equal(written.old_payload_bytes, 197, "re-point: the old block is 197 B");
    t.equal(written.new_offset, 725, "re-point: the new block is at 725");
    t.equal(written.entries_before, 43, "re-point: entries before");
    t.equal(written.entries_after, 43, "re-point: entries after");
    t.equal(written.row_entries_after, 12, "re-point: row entries unchanged");
    t.equal(written.freed_bytes_after, 0,
            "re-point: a re-point does not move the freed word");
    t.equal(written.var2_bytes_after, 938, "re-point: Var2Data 725 -> 938");
    t.check(doc.read_stream(kTaskVarMeta).size() == 540,
            "re-point: VarMeta keeps its 540 bytes");
    check_only_owned_streams(t, before, doc, "re-point",
                             {"114/TBkndTask/VarMeta", "114/TBkndTask/Var2Data",
                              "114/Props", "Props14"});

    std::array<char const*, 4> const declared{{
        "114/Props", "114/TBkndTask/Var2Data", "114/TBkndTask/VarMeta",
        "Props14"}};
    save_pair::compare_all(t, "var-repoint",
                           doc_from_data("wave34-note-replace-control.mpp"),
                           doc_from_data("wave34-note-replace-control-null.mpp"),
                           doc_from_data("wave34-note-replace.mpp"),
                           CfbDocument::from_file(out_path("w34-var-repoint.mpp")),
                           nullptr, 0, declared.data(), declared.size());
}

/*
 * RE-POINT (Resource) through the first consumer, rename_resource: the name is
 * decoded from Project's block to UTF-8 and re-encoded by the command.  The
 * Resource table's uid 2 Name moves 114 -> 140 (6 -> 8 B), Var2Data 140 -> 152,
 * the 114/Props Resource size word 140 -> 152; FixedMeta is untouched.
 */
void test_resource_rename_pair(TestContext& t) {
    fixture::require_committed(
        join(MPPWRITE_TEST_DATA, "wave30-resource-rename-control.mpp"));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, "wave30-resource-rename.mpp"));
    std::vector<std::uint8_t> const stored = stored_block(
        "wave30-resource-rename.mpp", kRscVarMeta, kRscVar2, 2,
        mppwrite::kResourceNameFieldId);
    t.equal(stored.size(), 8, "rename: Project's stored name block is 8 B");
    std::string const name = mppwrite::decode_utf16z(stored);
    t.equal(name.size(), 3, "rename: the stored name decodes to three bytes");
    t.check(mppwrite::encode_utf16z(name) == stored,
            "rename: the measured encoding round-trips the stored block");

    CfbDocument doc = doc_from_data("wave30-resource-rename-control.mpp");
    CfbDocument const before = doc;
    t.equal(doc.read_stream(kRscVar2).size(), 140,
            "rename: the control's Resource Var2Data is 140 B");
    VarRecordWrite const written = mppwrite::rename_resource(
        doc, 2, name, out_path("w34-var-resource-rename.mpp"));
    t.equal(written.old_offset, 114, "rename: the old name block is at 114");
    t.equal(written.old_payload_bytes, 6, "rename: the old name block is 6 B");
    t.equal(written.new_offset, 140, "rename: the new name block is at 140");
    t.equal(written.new_payload_bytes, 8, "rename: the new name block is 8 B");
    t.equal(written.entries_after, 12, "rename: the entry count is unchanged");
    t.equal(written.freed_bytes_after, 0,
            "rename: the Resource freed word does not move");
    t.equal(written.var2_bytes_after, 152, "rename: Var2Data 140 -> 152");
    t.check(doc.read_stream(kTaskFixedMeta) == before.read_stream(kTaskFixedMeta),
            "rename: the TASK table was not touched");
    check_only_owned_streams(t, before, doc, "rename",
                             {"114/TBkndRsc/VarMeta", "114/TBkndRsc/Var2Data",
                              "114/Props", "Props14"});

    std::array<char const*, 4> const declared{{
        "114/Props", "114/TBkndRsc/Var2Data", "114/TBkndRsc/VarMeta",
        "Props14"}};
    save_pair::compare_all(t, "resource-rename",
                           doc_from_data("wave30-resource-rename-control.mpp"),
                           doc_from_data("wave30-resource-rename-control-null.mpp"),
                           doc_from_data("wave30-resource-rename.mpp"),
                           CfbDocument::from_file(
                               out_path("w34-var-resource-rename.mpp")),
                           nullptr, 0, declared.data(), declared.size());

    /* Independent read-back from the file the COMMAND wrote: one entry for the
     * Resource name key, at the measured offset, holding exactly the measured
     * encoding of the requested name.  This does not go through the command's
     * own read-back check. */
    CfbDocument const written_file =
        CfbDocument::from_file(out_path("w34-var-resource-rename.mpp"));
    std::vector<std::uint32_t> const offsets = mppwrite::find_var_offsets(
        written_file.read_stream(kRscVarMeta), mppwrite::kResourceNameFieldId, 2);
    t.equal(offsets.size(), 1, "rename: one entry for the resource name key");
    if (offsets.size() == 1) {
        std::vector<std::uint8_t> const block = mppwrite::read_var_block(
            written_file.read_stream(kRscVar2), offsets.front());
        t.equal(offsets.front(), 140, "rename: the stored offset on disk");
        t.check(block == mppwrite::encode_utf16z(name),
                "rename: the block on disk is the measured encoding");
        t.check(mppwrite::decode_utf16z(block) == name,
                "rename: the value on disk decodes to the requested name");
    }
}

/*
 * W34-1.3: the consumer's own contract.  Every name refusal the header lists,
 * and -- the two unmeasured re-points the command must not silently perform --
 * a shorter name and the control's own equal-size name.  Each refusal leaves
 * the document byte-identical and writes no file.
 */
void test_rename_resource_contract(TestContext& t) {
    std::string const control = "wave30-resource-rename-control.mpp";
    std::string const missing = out_path("w34-ren-refused.mpp");
    fixture::require_committed(join(MPPWRITE_TEST_DATA, control));

    auto refuses = [&](CfbDocument& doc, std::string_view name,
                       std::string_view needle, std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        std::vector<std::uint8_t> const before = doc.to_bytes();
        expect_error(t,
                     [&] {
                         (void)mppwrite::rename_resource(doc, 2, name, missing);
                     },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
        t.check(!file_exists(missing), what + ": an output file was written");
    };

    {
        CfbDocument doc = doc_from_data(control);
        refuses(doc, "", "the name is empty", "an empty name refused");
    }
    {
        CfbDocument doc = doc_from_data(control);
        refuses(doc, std::string_view("a\0b", 3), "embedded NUL",
                "an embedded NUL refused");
    }
    {
        CfbDocument doc = doc_from_data(control);
        std::string const huge(65536, 'a');
        refuses(doc, huge, "65,535-byte cap",
                "a name past 65,535 bytes refused");
    }
    {
        CfbDocument doc = doc_from_data(control);
        refuses(doc, "a", "not larger than the stored",
                "a shorter name refused (the shrink re-point is unmeasured)");
    }
    {
        /* The control's own name is the equal-size case. */
        CfbDocument doc = doc_from_data(control);
        std::string const same = mppwrite::decode_utf16z(
            stored_block(control, kRscVarMeta, kRscVar2, 2,
                         mppwrite::kResourceNameFieldId));
        refuses(doc, same, "not larger than the stored",
                "the same name (equal size) refused");
    }
    {
        /* A valid name for a uid the table does not hold reaches the row
         * lookup and is refused there. */
        CfbDocument doc = doc_from_data(control);
        std::string const name = mppwrite::decode_utf16z(
            stored_block(control, kRscVarMeta, kRscVar2, 2,
                         mppwrite::kResourceNameFieldId));
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        std::vector<std::uint8_t> const before = doc.to_bytes();
        expect_error(t,
                     [&] {
                         (void)mppwrite::rename_resource(doc, 99, name, missing);
                     },
                     "no row of", "an unknown uid refused");
        t.check(doc.to_bytes() == before,
                "an unknown uid: the document changed");
        t.check(!file_exists(missing),
                "an unknown uid: an output file was written");
    }
}

/*
 * REMOVE.  Two pairs: wave34-field-clear (Text1, 40 B unlinked) and
 * wave34-note-clear (Notes, 188 B).  In both Project erases one 12-byte entry,
 * decrements the declared count, keeps the 540-byte stream (the freed last slot
 * stays a byte-identical copy of the new last logical entry), clears the
 * measured presence bits, increments the freed word and leaves Var2Data and the
 * FixedMeta entry count alone except for +2.
 */
void test_remove_pair(TestContext& t, std::string const& label,
                      std::string const& control_name,
                      std::string const& edit_name, std::uint32_t field_id,
                      std::size_t var2_bytes, std::uint32_t freed_expected,
                      std::vector<VarPresenceBit> const& presence,
                      std::string const& out_name) {
    fixture::require_committed(join(MPPWRITE_TEST_DATA, control_name));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, edit_name));

    CfbDocument doc = doc_from_data(control_name);
    CfbDocument const before = doc;
    std::vector<std::uint8_t> const var2_before = doc.read_stream(kTaskVar2);
    std::vector<std::uint8_t> const var_meta_before = doc.read_stream(kTaskVarMeta);
    std::vector<mppwrite::VarEntry> const entries_before = entries_of(doc, kTaskVarMeta);
    t.equal(u32_at(var_meta_before, 8), 43, label + ": the control declares 43 entries");
    t.equal(var_meta_before.size(), 24 + 43 * 12,
            label + ": the control has no stale tail before the edit");
    t.equal(var2_before.size(), var2_bytes,
            label + ": the control's Var2Data is " +
                std::to_string(var2_bytes) + " B");

    VarRecordEdit edit;
    edit.uid = 2;
    edit.field_id = field_id;
    edit.shape = VarRecordShape::remove;
    edit.presence = presence;
    std::vector<VarRecordEdit> const edits{edit};
    VarRecordWrite const written = mppwrite::save_var_record(
        doc, mppwrite::kVarTaskTable, edits, out_path(out_name));
    t.equal(written.freed_bytes_before, 0, label + ": freed before");
    t.equal(written.freed_bytes_after, freed_expected, label + ": freed after");
    t.equal(written.entries_after, 42, label + ": entries after");
    t.equal(written.row_entries_after, 11, label + ": row entries after");

    /* The stream keeps its length; the freed slot duplicates the last entry. */
    std::vector<std::uint8_t> const var_meta_after = doc.read_stream(kTaskVarMeta);
    t.equal(var_meta_after.size(), 540, label + ": VarMeta stays 540 B");
    t.equal(u32_at(var_meta_after, 8), 42, label + ": the header declares 42");
    std::vector<mppwrite::VarEntry> const physical = entries_of(doc, kTaskVarMeta);
    t.equal(physical.size(), 43, label + ": 43 physical entry slots remain");
    if (physical.size() == 43) {
        t.check(physical[42].mid == physical[41].mid &&
                    physical[42].field_id == physical[41].field_id &&
                    physical[42].offset == physical[41].offset,
                label + ": the stale slot duplicates the new last entry");
    }
    t.check(doc.read_stream(kTaskVar2) == var2_before,
            label + ": Var2Data is byte-identical (the block stays orphaned)");
    std::vector<std::uint8_t> const meta = doc.read_stream(kTaskFixedMeta);
    std::size_t const base = meta_base_for_row(47, 2);
    t.equal(u16_at(meta, base + 2), 11, label + ": FixedMeta +2 is 11");
    for (VarPresenceBit const& op : presence) {
        t.equal(meta[base + op.byte],
                static_cast<std::uint8_t>(before.read_stream(kTaskFixedMeta)
                                              [base + op.byte] &
                                          static_cast<std::uint8_t>(~op.bit)),
                label + ": presence byte +" + std::to_string(op.byte) + " cleared");
    }
    /* The removed entry is gone from the logical array, the rest keep order. */
    bool logical_ok = true;
    std::vector<mppwrite::VarEntry> const expected = [&] {
        std::vector<mppwrite::VarEntry> out = entries_before;
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (out[i].mid == 2 && out[i].field_id == field_id) {
                out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        return out;
    }();
    for (std::size_t i = 0; i < expected.size() && i < 42; ++i) {
        logical_ok = logical_ok && physical[i].mid == expected[i].mid &&
                     physical[i].field_id == expected[i].field_id &&
                     physical[i].offset == expected[i].offset;
    }
    t.check(logical_ok, label + ": the logical array is the control minus the entry");
    check_only_owned_streams(t, before, doc, label,
                             {"114/TBkndTask/VarMeta", "114/Props",
                              "114/TBkndTask/FixedMeta", "Props14"});

    std::array<char const*, 4> const declared{{
        "114/Props", "114/TBkndTask/FixedMeta", "114/TBkndTask/VarMeta",
        "Props14"}};
    save_pair::compare_all(t, label, doc_from_data(control_name),
                           doc_from_data(save_pair::null_fixture_name(control_name)),
                           doc_from_data(edit_name),
                           CfbDocument::from_file(out_path(out_name)), nullptr, 0,
                           declared.data(), declared.size());
}

void test_field_clear_pair(TestContext& t) {
    test_remove_pair(t, "var-remove-text1", "wave34-field-clear-control.mpp",
                     "wave34-field-clear.mpp", kText1Key, 574, 40,
                     {{21, 0x20, false}}, "w34-var-remove-text1.mpp");
}

void test_note_clear_pair(TestContext& t) {
    test_remove_pair(t, "var-remove-notes", "wave34-note-clear-control.mpp",
                     "wave34-note-clear.mpp", kNotesKey, 716, 188,
                     {{14, 0x10, false}, {44, 0x10, false}},
                     "w34-var-remove-notes.mpp");
}

/* ---- refusals ----------------------------------------------------------- */

/* A document copy with one stream replaced, for the damaged-input refusals. */
CfbDocument with_stream(std::string const& control_name, std::string const& path,
                        std::vector<std::uint8_t> const& bytes) {
    CfbDocument doc = doc_from_data(control_name);
    doc.replace_stream(path, bytes);
    return doc;
}

void test_refusals(TestContext& t) {
    std::string const missing = out_path("w34-var-refused.mpp");
    std::error_code ignored;
    std::filesystem::remove(missing, ignored);

    /* Every refusal runs against a valid control; the lambda's document is a
     * fresh copy so the "unchanged" assertion is meaningful. */
    auto refuses = [&](std::string const& control_name,
                       std::vector<VarRecordEdit> const& edits,
                       std::string_view needle, std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = doc_from_data(control_name);
        std::vector<std::uint8_t> const before = doc.to_bytes();
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             doc, mppwrite::kVarTaskTable, edits, missing);
                     },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
        t.check(!file_exists(missing), what + ": an output file was written");
    };

    auto append_edit = [](std::uint32_t uid, std::uint32_t key,
                          std::vector<std::uint8_t> payload) {
        VarRecordEdit edit;
        edit.uid = uid;
        edit.field_id = key;
        edit.shape = VarRecordShape::append;
        edit.payload = std::move(payload);
        return std::vector<VarRecordEdit>{edit};
    };
    auto repoint_edit = [](std::uint32_t uid, std::uint32_t key,
                           std::vector<std::uint8_t> payload) {
        VarRecordEdit edit;
        edit.uid = uid;
        edit.field_id = key;
        edit.shape = VarRecordShape::repoint;
        edit.payload = std::move(payload);
        return std::vector<VarRecordEdit>{edit};
    };
    auto remove_edit = [](std::uint32_t uid, std::uint32_t key) {
        VarRecordEdit edit;
        edit.uid = uid;
        edit.field_id = key;
        edit.shape = VarRecordShape::remove;
        return std::vector<VarRecordEdit>{edit};
    };

    /* The edit-count and table refusals. */
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> const before = doc.to_bytes();
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             doc, mppwrite::kVarTaskTable, {}, missing);
                     },
                     "exactly one record", "zero edits refused");
        t.check(doc.to_bytes() == before, "zero edits: the document changed");
        t.check(!file_exists(missing), "zero edits: an output file was written");
    }
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<VarRecordEdit> two = append_edit(2, kNotesKey, {1, 2});
        two.push_back(two.front());
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             doc, mppwrite::kVarTaskTable, two, missing);
                     },
                     "exactly one record", "two edits refused");
    }
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             doc, "114/TBkndAssn", edits, missing);
                     },
                     "is not a table this writer", "the Assignment table refused");
    }
    refuses("wave30-notes-control.mpp", append_edit(0, kNotesKey, {1, 2}),
            "uid 0 is the project row", "the project row refused");
    refuses("wave30-notes-control.mpp", append_edit(2, 0, {1, 2}),
            "field key 0", "key 0 refused");
    refuses("wave30-notes-control.mpp", append_edit(99, kNotesKey, {1, 2}),
            "no row of", "an unknown uid refused");

    /* Shape availability. */
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             doc, mppwrite::kVarResourceTable, edits, missing);
                     },
                     "appending one record to the 114/TBkndRsc table is not "
                     "measured",
                     "an append on Resource refused");
    }
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<VarRecordEdit> const edits = remove_edit(2, kNotesKey);
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             doc, mppwrite::kVarResourceTable, edits, missing);
                     },
                     "removing one record from the 114/TBkndRsc table is not "
                     "measured",
                     "a remove on Resource refused");
    }

    /* Append collision: wave30-notes.mpp already stores the Notes entry. */
    refuses("wave30-notes.mpp", append_edit(2, kNotesKey, {1, 2}),
            "already has a record", "an append onto an existing key refused");
    refuses("wave30-notes-control.mpp", append_edit(2, kNotesKey, {}),
            "an append needs a non-empty payload", "an empty append refused");

    /* Re-point and remove entry shapes. */
    refuses("wave30-notes-control.mpp", repoint_edit(2, kNotesKey, {1, 2, 3}),
            "a re-point needs exactly one existing record",
            "a re-point with no entry refused");
    refuses("wave30-notes-control.mpp", remove_edit(2, kNotesKey),
            "a remove needs exactly one existing record",
            "a remove with no entry refused");
    refuses("wave30-notes.mpp", repoint_edit(2, kNotesKey, {1, 2, 3}),
            "not larger than the stored",
            "a smaller re-point payload refused");
    {
        std::vector<std::uint8_t> const stored = stored_block(
            "wave30-notes.mpp", kTaskVarMeta, kTaskVar2, 2, kNotesKey);
        refuses("wave30-notes.mpp", repoint_edit(2, kNotesKey, stored),
                "not larger than the stored", "an equal-size re-point refused");
    }
    {
        VarRecordEdit edit;
        edit.uid = 2;
        edit.field_id = kNotesKey;
        edit.shape = VarRecordShape::remove;
        edit.payload = {1, 2, 3};
        refuses("wave30-notes.mpp", {edit}, "a remove takes no payload",
                "a remove with a payload refused");
    }
    /* Presence operations. */
    {
        VarRecordEdit edit;
        edit.uid = 2;
        edit.field_id = kNotesKey;
        edit.shape = VarRecordShape::append;
        edit.payload = {1, 2};
        edit.presence = {{2, 0x10, true}};
        std::vector<VarRecordEdit> const edits{edit};
        refuses("wave30-notes-control.mpp", edits,
                "outside the measured row-entry tail", "presence byte 2 refused");
        VarRecordEdit past = edit;
        past.presence = {{47, 0x10, true}};
        refuses("wave30-notes-control.mpp", {past},
                "outside the measured row-entry tail",
                "presence byte 47 (past the 47-byte entry) refused");
        VarRecordEdit multi = edit;
        multi.presence = {{14, 0x03, true}};
        refuses("wave30-notes-control.mpp", {multi}, "not a single bit",
                "a multi-bit presence mask refused");
        VarRecordEdit twice = edit;
        twice.presence = {{14, 0x10, true}, {14, 0x10, false}};
        refuses("wave30-notes-control.mpp", {twice}, "is requested twice",
                "a duplicated presence operation refused");
    }

    /* Output paths. */
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             doc, mppwrite::kVarTaskTable, edits, "");
                     },
                     "the output path is empty", "an empty output path refused");
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             doc, mppwrite::kVarTaskTable, edits,
                             "core/mppwrite/no-such-dir/w34.mpp");
                     },
                     "does not exist", "a missing output directory refused");
    }

    /* Damaged table streams: each refusal fires before anything is written. */
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> fixed = doc.read_stream(kTaskFixed);
        fixed.pop_back();
        CfbDocument damaged = with_stream("wave30-notes-control.mpp", kTaskFixed, fixed);
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "refusing to edit a table whose geometry is broken",
                     "damaged FixedData refused");
    }
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> var_meta = doc.read_stream(kTaskVarMeta);
        var_meta.push_back(0);
        CfbDocument damaged = with_stream("wave30-notes-control.mpp", kTaskVarMeta, var_meta);
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "not 24 + 12*n", "a VarMeta that is not 24 + 12*n refused");
    }
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> var_meta = doc.read_stream(kTaskVarMeta);
        put_u32_at(var_meta, 0x14, u32_at(var_meta, 0x14) + 4);
        CfbDocument damaged = with_stream("wave30-notes-control.mpp", kTaskVarMeta, var_meta);
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "size word says", "a +0x14 size word mismatch refused");
    }
    {
        /* A file this writer has already shrunk once: wave34-field-clear.mpp
         * carries the one-slot stale tail a removal leaves, and it is refused
         * as a SOURCE (no committed pair edits from that state). */
        CfbDocument doc = doc_from_data("wave34-field-clear.mpp");
        std::vector<mppwrite::VarEntry> const entries = entries_of(doc, kTaskVarMeta);
        std::uint32_t key = 0;
        std::size_t old = 0;
        for (mppwrite::VarEntry const& entry : entries) {
            if (entry.mid == 2) {
                key = entry.field_id;
                old = entry.offset;
                break;
            }
        }
        t.check(key != 0, "stale tail: the edit file has a uid 2 entry");
        std::vector<std::uint8_t> payload =
            mppwrite::read_var_block(doc.read_stream(kTaskVar2),
                                     static_cast<std::uint32_t>(old));
        payload.insert(payload.end(), 8, 0);
        std::vector<VarRecordEdit> const edits = repoint_edit(2, key, payload);
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             doc, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "a stale tail is not a shape this writer edits",
                     "a stale tail on the way in refused");
    }
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> var_meta = doc.read_stream(kTaskVarMeta);
        std::vector<mppwrite::VarEntry> const entries = entries_of(doc, kTaskVarMeta);
        std::size_t at = 0;
        for (mppwrite::VarEntry const& entry : entries) {
            if (entry.mid == 2) {
                at = 24 + entry.index * 12;
                break;
            }
        }
        put_u32_at(var_meta, at + 4, 0xFFFFFFFFu);
        CfbDocument damaged = with_stream("wave30-notes-control.mpp", kTaskVarMeta, var_meta);
        std::vector<VarRecordEdit> const edits = repoint_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "0xFFFFFFFF no-block offset",
                     "a 0xFFFFFFFF entry offset refused");
        std::vector<VarRecordEdit> const removals = remove_edit(2, kNotesKey);
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, removals, missing);
                     },
                     "0xFFFFFFFF no-block offset",
                     "a 0xFFFFFFFF entry offset refused for a remove too");
        /* W34b-1.1 (R34G3-3): offsets within four of 2^32 must be refused as
         * past the end of Var2Data, not wrapped by the +4 of the bounds check. */
        for (std::uint32_t const wild : {0xFFFFFFFCu, 0xFFFFFFFDu, 0xFFFFFFFEu}) {
            std::vector<std::uint8_t> wrapped = var_meta;
            put_u32_at(wrapped, at + 4, wild);
            CfbDocument wild_doc =
                with_stream("wave30-notes-control.mpp", kTaskVarMeta, wrapped);
            expect_error(t,
                         [&] {
                             (void)mppwrite::save_var_record(
                                 wild_doc, mppwrite::kVarTaskTable, edits, missing);
                         },
                         "is past the end of Var2Data",
                         "an entry offset 0x" + std::to_string(wild) +
                             " (32-bit wrap) refused");
        }
    }
    {
        /* A duplicated (uid, key) pair: overwrite a second uid 2 entry with the
         * Notes key, keeping its offset. */
        CfbDocument doc = doc_from_data("wave30-notes.mpp");
        std::vector<std::uint8_t> var_meta = doc.read_stream(kTaskVarMeta);
        std::vector<mppwrite::VarEntry> const entries = entries_of(doc, kTaskVarMeta);
        std::size_t target = 0;
        std::size_t victim = 0;
        bool have_victim = false;
        for (mppwrite::VarEntry const& entry : entries) {
            if (entry.mid == 2 && entry.field_id == kNotesKey) {
                target = 24 + entry.index * 12;
            } else if (entry.mid == 2 && !have_victim) {
                victim = 24 + entry.index * 12;
                have_victim = true;
            }
        }
        t.check(target != 0 && have_victim, "duplicate: two uid 2 entries exist");
        put_u32_at(var_meta, victim + 8, kNotesKey);
        CfbDocument damaged = with_stream("wave30-notes.mpp", kTaskVarMeta, var_meta);
        std::vector<VarRecordEdit> const edits =
            repoint_edit(2, kNotesKey, std::vector<std::uint8_t>(300, 0x42));
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "a re-point needs exactly one existing record",
                     "two entries for one key refused");
        std::vector<VarRecordEdit> const removals = remove_edit(2, kNotesKey);
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, removals, missing);
                     },
                     "a remove needs exactly one existing record",
                     "two entries for one key refused for a remove too");
    }
    {
        /* A 0x80000000-family entry for the key: the plain uid must not adopt
         * it.  Overwrite the Notes entry's uid with 0x80000002. */
        CfbDocument doc = doc_from_data("wave30-notes.mpp");
        std::vector<std::uint8_t> var_meta = doc.read_stream(kTaskVarMeta);
        for (mppwrite::VarEntry const& entry : entries_of(doc, kTaskVarMeta)) {
            if (entry.mid == 2 && entry.field_id == kNotesKey) {
                put_u32_at(var_meta, 24 + entry.index * 12, 0x80000002u);
            }
        }
        CfbDocument damaged = with_stream("wave30-notes.mpp", kTaskVarMeta, var_meta);
        std::vector<VarRecordEdit> const edits =
            repoint_edit(2, kNotesKey, std::vector<std::uint8_t>(300, 0x42));
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "0x80000000", "a 0x80000000-family entry refused");
        std::vector<VarRecordEdit> const removals = remove_edit(2, kNotesKey);
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, removals, missing);
                     },
                     "0x80000000",
                     "a 0x80000000-family entry refused for a remove too");
        std::vector<VarRecordEdit> const appends = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, appends, missing);
                     },
                     "already has a record",
                     "an append onto a family entry refused");
    }
    {
        /* A zero-length old block: append four zero bytes to Var2Data and point
         * the Notes entry at them. */
        CfbDocument doc = doc_from_data("wave30-notes.mpp");
        std::vector<std::uint8_t> var2 = doc.read_stream(kTaskVar2);
        std::size_t const zero_at = var2.size();
        var2.insert(var2.end(), 4, 0);
        std::vector<std::uint8_t> var_meta = doc.read_stream(kTaskVarMeta);
        for (mppwrite::VarEntry const& entry : entries_of(doc, kTaskVarMeta)) {
            if (entry.mid == 2 && entry.field_id == kNotesKey) {
                put_u32_at(var_meta, 24 + entry.index * 12 + 4,
                           static_cast<std::uint32_t>(zero_at));
            }
        }
        put_u32_at(var_meta, 0x14, static_cast<std::uint32_t>(var2.size()));
        CfbDocument damaged = doc;
        damaged.replace_stream(kTaskVarMeta, var_meta);
        damaged.replace_stream(kTaskVar2, var2);
        std::vector<VarRecordEdit> const edits =
            repoint_edit(2, kNotesKey, std::vector<std::uint8_t>(300, 0x42));
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "has zero payload", "a zero-length old block refused");
        std::vector<VarRecordEdit> const removals = remove_edit(2, kNotesKey);
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, removals, missing);
                     },
                     "has zero payload",
                     "a zero-length old block refused for a remove too");
    }

    {
        /* A FixedMeta entry that names another row's FixedData offset. */
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> meta = doc.read_stream(kTaskFixedMeta);
        put_u32_at(meta, meta_base_for_row(47, 2) + 4,
                   static_cast<std::uint32_t>(48 + 202 * 3));
        CfbDocument damaged =
            with_stream("wave30-notes-control.mpp", kTaskFixedMeta, meta);
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "does not name its FixedData row",
                     "a FixedMeta entry that does not name its row refused");
    }
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> fixed2 = doc.read_stream(kTaskFixed2);
        fixed2.pop_back();
        CfbDocument damaged =
            with_stream("wave30-notes-control.mpp", kTaskFixed2, fixed2);
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "Fixed2Data is", "a damaged Fixed2Data refused");
    }
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> fixed2_meta = doc.read_stream(kTaskFixed2Meta);
        fixed2_meta.pop_back();
        CfbDocument damaged =
            with_stream("wave30-notes-control.mpp", kTaskFixed2Meta, fixed2_meta);
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "Fixed2Meta is", "a damaged Fixed2Meta refused");
    }
    {
        /* The same 114/Props scalar group twice: the writer must not choose
         * which word is the counter. */
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> const props = doc.read_stream(kProps);
        std::size_t const group = props_group_for(props, 0x01000100u);
        t.check(group != static_cast<std::size_t>(-1),
                "duplicate Props: the Task size tag exists");
        if (group != static_cast<std::size_t>(-1)) {
            std::vector<std::uint8_t> twice = props;
            twice.insert(twice.begin() + static_cast<std::ptrdiff_t>(group + 16),
                         props.begin() + static_cast<std::ptrdiff_t>(group),
                         props.begin() + static_cast<std::ptrdiff_t>(group + 16));
            CfbDocument damaged =
                with_stream("wave30-notes-control.mpp", kProps, twice);
            std::vector<VarRecordEdit> const edits =
                append_edit(2, kNotesKey, {1, 2});
            expect_error(t,
                         [&] {
                             (void)mppwrite::save_var_record(
                                 damaged, mppwrite::kVarTaskTable, edits, missing);
                         },
                         "more than one scalar record",
                         "a duplicated 114/Props tag refused");
        }
    }

    /* A duplicated uid in FixedData, and a dead FixedMeta state byte. */
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> fixed = doc.read_stream(kTaskFixed);
        std::size_t const row = 2;
        if (fixed.size() >= 48 + 202 * 4) {
            put_u32_at(fixed, 48 + 202 * (row + 1) + 4, 2);
        }
        CfbDocument damaged = with_stream("wave30-notes-control.mpp", kTaskFixed, fixed);
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "names more than one row", "a duplicated uid refused");
    }
    {
        CfbDocument doc = doc_from_data("wave30-notes-control.mpp");
        std::vector<std::uint8_t> meta = doc.read_stream(kTaskFixedMeta);
        meta[meta_base_for_row(47, 2)] = 0x01;
        CfbDocument damaged = with_stream("wave30-notes-control.mpp", kTaskFixedMeta, meta);
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "is not live", "a tombstoned row refused");
    }

    /* A missing 114/Props scalar tag. */
    {
        std::vector<std::uint8_t> props = doc_from_data("wave30-notes-control.mpp")
                                              .read_stream(kProps);
        std::vector<std::uint8_t> blank(props.size(), 0);
        CfbDocument damaged = with_stream("wave30-notes-control.mpp", kProps, blank);
        std::vector<VarRecordEdit> const edits = append_edit(2, kNotesKey, {1, 2});
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_var_record(
                             damaged, mppwrite::kVarTaskTable, edits, missing);
                     },
                     "has no scalar record", "a missing Props counter tag refused");
    }

    t.check(!file_exists(missing),
            "refusals: an output file was written by one of the refused calls");
}

} /* namespace */

int main() {
    try {
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);
        TestContext t;
        test_append_pair(t);
        test_repoint_pair(t);
        test_resource_rename_pair(t);
        test_rename_resource_contract(t);
        test_field_clear_pair(t);
        test_note_clear_pair(t);
        test_refusals(t);
        return t.report();
    } catch (std::exception const& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << std::endl;
        return 1;
    }
}
