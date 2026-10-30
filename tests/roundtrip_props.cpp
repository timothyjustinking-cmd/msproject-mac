/*
 * roundtrip_props.cpp -- W34-2.3: the Props record/scalar writer against
 * Project's own saves, for every shape it enables, plus every refusal.
 *
 * WHAT IS PROVEN.  For each committed pair the port opens PROJECT'S CONTROL,
 * applies exactly the editing command's inputs through core/mppwrite/props_edit.h
 * and compares the stream the writer owns with Project's EDIT save under the
 * pair's null-save mask.  NOT ONE KnownGap: every unmasked field byte must equal
 * Project's save.
 *
 *   pair                stream      measured edit
 *   wave30-wbs-code     114/Props   record 0x0240138b kind 0 4 -> 24 B,
 *                                   record 0x024013be kind 0 2 -> 6 B,
 *                                   Task size scalar 0x01000100 512 -> 552
 *   wave34-field-set    114/Props   record 0x02401392 kind 2 equal-size 2 -> 2,
 *                                   record 0x024013c5 kind 0 8 -> 16 B,
 *                                   Task size scalar 0x01000100 530 -> 574
 *   wave30-resource-add 114/Props   Resource count 0x02000001 5 -> 6,
 *                                   Resource size 0x02000100 86 -> 140,
 *                                   Calendar count 0x03000001 7 -> 8
 *   wave30-textstyle    214/Props   CV_iew Var2Data size 0x01000100
 *                                   100,178 -> 100,234
 *
 * The record payloads are CALLER'S BYTES: this writer stores a record payload it
 * is given, and producing that payload (the WBS definition's fields) is the
 * command's work, not this writer's.  The test therefore reads each edited
 * record's payload out of Project's edit save and hands it back as the input,
 * exactly as W34-1.2 does for the Var2Data writer's append and re-point; the
 * text-like record's bytes never appear in this file.  The scalar values are
 * plain integers from the pair anatomy.
 *
 * HOW A GROWING STREAM IS COMPARED.  A single whole-stream positional compare
 * is NOT used: when a record grows mid-stream every later byte moves, and the
 * positional mask no longer covers the per-save identity bytes those records
 * carry (measured: one false mismatch per pair, at the shifted-out-of-the-mask
 * seam; spec/writers/props-records.md, "How the tests compare a growing
 * stream").  Instead each FIELD is compared through save_pair::compare with its
 * own control-vs-null mask: the 16-byte header, every record's header+payload
 * (walked separately in each document, so the field is aligned after the
 * shift), and the tail.  The record list is additionally checked structurally:
 * the port must reproduce Project's record count, id, kind and offset for every
 * record, and every record the request did not name must be byte-identical to
 * the control's.
 *
 * REFUSALS.  Every refusal branch the writer offers is exercised: no edit; an
 * unknown stream (including the per-table Props registries); a record edit on
 * 214/Props; a target record absent (the unmeasured insert, with the
 * wave30-baseline evidence in the message), present twice, of another kind,
 * with an equal payload and with a smaller payload; a duplicate record id in
 * the request; a scalar tag outside the stream's whitelist, a duplicate tag in
 * the request, a tag absent from the stream, a tag twice in the stream, a hit
 * inside the record area and a value equal to the current one; a stream shorter
 * than its 16-byte header, wrong size words, a wrong format word and a record
 * that runs past the stream end; and an empty, directory or missing-parent
 * output path.  Each refusal also asserts that the document was not modified
 * and that no output file appeared.
 */

#include <algorithm>
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
#include "props_edit.h"

#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif

namespace {

using mppwrite::CfbDocument;
using mppwrite::PropsEdit;
using mppwrite::PropsEditWrite;
using mppwrite::PropsRecordEdit;
using mppwrite::PropsRecordWrite;
using mppwrite::PropsScalarEdit;
using Bytes = std::vector<std::uint8_t>;

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

std::string join(std::string const& a, std::string const& b) { return a + "/" + b; }

std::string out_path(std::string const& name) {
    return join(MPPWRITE_TEST_OUT, name);
}

std::string missing_path() { return out_path("w34-props-refused.mpp"); }

CfbDocument doc_from_data(std::string const& name) {
    return CfbDocument::from_file(join(MPPWRITE_TEST_DATA, name));
}

bool file_exists(std::string const& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::uint32_t u32_at(Bytes const& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

void put_u32_at(Bytes& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

std::uint32_t big_u32_at(Bytes const& b, std::size_t o) {
    return (static_cast<std::uint32_t>(b[o]) << 24) |
           (static_cast<std::uint32_t>(b[o + 1]) << 16) |
           (static_cast<std::uint32_t>(b[o + 2]) << 8) |
           static_cast<std::uint32_t>(b[o + 3]);
}

void fix_size_words(Bytes& b) {
    put_u32_at(b, 0, static_cast<std::uint32_t>(b.size() - 4));
    put_u32_at(b, 4, static_cast<std::uint32_t>(b.size() - 4));
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
                        Bytes const& bytes) {
    CfbDocument doc = doc_from_data(control_name);
    doc.replace_stream(path, bytes);
    return doc;
}

/* ---- the stream walk (the writer's own record-area rule) ----------------- */

struct RecordSpan {
    std::size_t offset = 0; /* the record header */
    std::size_t length = 0; /* payload bytes */
    std::uint32_t id = 0;
    std::uint32_t kind = 0;
};

struct PropsWalk {
    std::vector<RecordSpan> records;
    std::size_t tail_offset = 0;
};

PropsWalk walk_props(Bytes const& bytes) {
    PropsWalk walk;
    std::size_t pos = 16;
    while (pos + 12 <= bytes.size()) {
        std::uint32_t const id = u32_at(bytes, pos + 4);
        if ((id & 0x00FF0000u) != 0x00400000u) {
            break;
        }
        RecordSpan record;
        record.offset = pos;
        record.length = u32_at(bytes, pos);
        record.id = id;
        record.kind = u32_at(bytes, pos + 8);
        walk.records.push_back(record);
        pos += 12 + record.length;
    }
    walk.tail_offset = pos;
    return walk;
}

Bytes slice(Bytes const& bytes, std::size_t at, std::size_t length) {
    at = std::min(at, bytes.size());
    length = std::min(length, bytes.size() - at);
    return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                 bytes.begin() + static_cast<std::ptrdiff_t>(at + length));
}

std::size_t find_record(PropsWalk const& walk, std::uint32_t id,
                        std::string const& what) {
    std::size_t found = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < walk.records.size(); ++i) {
        if (walk.records[i].id == id) {
            if (found != static_cast<std::size_t>(-1)) {
                throw std::runtime_error(what + ": the id appears twice");
            }
            found = i;
        }
    }
    if (found == static_cast<std::size_t>(-1)) {
        throw std::runtime_error(what + ": the id is absent");
    }
    return found;
}

Bytes record_payload(Bytes const& bytes, PropsWalk const& walk, std::uint32_t id,
                     std::string const& what) {
    RecordSpan const& record = walk.records[find_record(walk, id, what)];
    return slice(bytes, record.offset + 12, record.length);
}

/* The [4][BE tag][4][value] entry start, mirroring the writer's scan. */
std::size_t scalar_entry(std::uint32_t tag, Bytes const& bytes,
                         std::string const& what) {
    std::size_t found = static_cast<std::size_t>(-1);
    for (std::size_t at = 0; at + 16 <= bytes.size(); ++at) {
        if (u32_at(bytes, at) != 4 || u32_at(bytes, at + 8) != 4) {
            continue;
        }
        if (big_u32_at(bytes, at + 4) != tag) {
            continue;
        }
        if (found != static_cast<std::size_t>(-1)) {
            throw std::runtime_error(what + ": the tag appears twice");
        }
        found = at;
    }
    if (found == static_cast<std::size_t>(-1)) {
        throw std::runtime_error(what + ": the tag is absent");
    }
    return found;
}

std::uint32_t scalar_value(Bytes const& bytes, std::uint32_t tag,
                           std::string const& what) {
    return u32_at(bytes, scalar_entry(tag, bytes, what) + 12);
}

/* Remove one 16-byte scalar entry from the tail, keeping the header valid. */
Bytes without_scalar(Bytes bytes, std::uint32_t tag, std::string const& what) {
    std::size_t const at = scalar_entry(tag, bytes, what);
    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                bytes.begin() + static_cast<std::ptrdiff_t>(at + 16));
    fix_size_words(bytes);
    return bytes;
}

/* Append a second copy of one 16-byte scalar entry to the tail. */
Bytes with_duplicate_scalar(Bytes bytes, std::uint32_t tag,
                            std::string const& what) {
    std::size_t const at = scalar_entry(tag, bytes, what);
    bytes.insert(bytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at),
                 bytes.begin() + static_cast<std::ptrdiff_t>(at + 16));
    fix_size_words(bytes);
    return bytes;
}

/* Move one scalar entry into a record payload, so the only hit is outside the
 * tail.  The record walk is unaffected: a payload is opaque to it. */
Bytes with_scalar_inside_records(Bytes bytes, std::uint32_t tag,
                                 std::string const& what) {
    std::size_t const at = scalar_entry(tag, bytes, what);
    Bytes entry(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                bytes.begin() + static_cast<std::ptrdiff_t>(at + 16));
    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                bytes.begin() + static_cast<std::ptrdiff_t>(at + 16));
    PropsWalk const walk = walk_props(bytes);
    if (walk.records.size() < 2 || walk.records[1].length < 16) {
        throw std::runtime_error(what + ": no record payload to plant into");
    }
    std::size_t const target = walk.records[1].offset + 12;
    std::copy(entry.begin(), entry.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(target));
    fix_size_words(bytes);
    return bytes;
}

/* Insert a byte-identical copy of one record after itself. */
Bytes with_duplicate_record(Bytes bytes, std::uint32_t id,
                            std::string const& what) {
    PropsWalk const walk = walk_props(bytes);
    RecordSpan const& record = walk.records[find_record(walk, id, what)];
    std::size_t const end = record.offset + 12 + record.length;
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(end),
                 bytes.begin() + static_cast<std::ptrdiff_t>(record.offset),
                 bytes.begin() + static_cast<std::ptrdiff_t>(end));
    fix_size_words(bytes);
    return bytes;
}

/* ---- the field-wise Project comparison ---------------------------------- */

void compare_field(TestContext& t, std::string const& label,
                   Bytes const& control, Bytes const& null_save,
                   Bytes const& project, Bytes const& port, bool declared) {
    save_pair::Verdict const verdict = save_pair::compare(
        save_pair::Kind::fixed, control, null_save, project, port, declared);
    t.check(verdict.lengths_ok, label + ": control and null save lengths agree");
    std::string message = label + ": equals Project's edit save outside the mask";
    if (verdict.mismatches != 0) {
        message += " (first of " + std::to_string(verdict.mismatches) +
                   " at byte " + std::to_string(verdict.first_mismatch) +
                   ": port " + std::to_string(verdict.output_byte) +
                   ", Project " + std::to_string(verdict.result_byte) + ")";
    }
    t.check(verdict.mismatches == 0, message);
    if (declared) {
        t.check(verdict.effects_ok,
                label + ": the declared effect set is non-empty");
    }
}

/*
 * Compare one whole Props stream field by field: the 16-byte header, every
 * record's header+payload (extracted by walking each document, so the field is
 * aligned after the edit's shift) and the tail.  `edited_records` declares the
 * records the request changes; a scalar edit declares the tail.
 */
void compare_stream_fields(TestContext& t, std::string const& label,
                           CfbDocument const& control,
                           CfbDocument const& null_save,
                           CfbDocument const& project,
                           CfbDocument const& port, std::string const& stream,
                           std::vector<std::uint32_t> const& edited_records,
                           bool scalar_declared) {
    Bytes const control_bytes = control.read_stream(stream);
    Bytes const null_bytes = null_save.read_stream(stream);
    Bytes const project_bytes = project.read_stream(stream);
    Bytes const port_bytes = port.read_stream(stream);
    PropsWalk const control_walk = walk_props(control_bytes);
    PropsWalk const null_walk = walk_props(null_bytes);
    PropsWalk const project_walk = walk_props(project_bytes);
    PropsWalk const port_walk = walk_props(port_bytes);

    t.equal(port_walk.records.size(), project_walk.records.size(),
            label + ": the record count equals Project's");
    bool layout_ok = port_walk.records.size() == project_walk.records.size();
    for (std::size_t i = 0;
         i < port_walk.records.size() && i < project_walk.records.size(); ++i) {
        layout_ok = layout_ok &&
                    port_walk.records[i].id == project_walk.records[i].id &&
                    port_walk.records[i].kind == project_walk.records[i].kind &&
                    port_walk.records[i].offset == project_walk.records[i].offset;
    }
    t.check(layout_ok,
            label + ": every record's id, kind and offset equals Project's");

    t.equal(port_bytes.size(), project_bytes.size(),
            label + ": the stream length equals Project's");
    compare_field(t, label + " header", slice(control_bytes, 0, 16),
                  slice(null_bytes, 0, 16), slice(project_bytes, 0, 16),
                  slice(port_bytes, 0, 16), control_bytes.size() != project_bytes.size());

    for (std::size_t i = 0;
         i < control_walk.records.size() && i < null_walk.records.size() &&
         i < project_walk.records.size() && i < port_walk.records.size();
         ++i) {
        RecordSpan const& record = control_walk.records[i];
        bool const edited = std::find(edited_records.begin(),
                                      edited_records.end(),
                                      record.id) != edited_records.end();
        compare_field(
            t, label + " record " + std::to_string(i),
            slice(control_bytes, record.offset, 12 + record.length),
            slice(null_bytes, null_walk.records[i].offset,
                  12 + null_walk.records[i].length),
            slice(project_bytes, project_walk.records[i].offset,
                  12 + project_walk.records[i].length),
            slice(port_bytes, port_walk.records[i].offset,
                  12 + port_walk.records[i].length),
            edited);
    }

    compare_field(t, label + " tail",
                  slice(control_bytes, control_walk.tail_offset,
                        control_bytes.size() - control_walk.tail_offset),
                  slice(null_bytes, null_walk.tail_offset,
                        null_bytes.size() - null_walk.tail_offset),
                  slice(project_bytes, project_walk.tail_offset,
                        project_bytes.size() - project_walk.tail_offset),
                  slice(port_bytes, port_walk.tail_offset,
                        port_bytes.size() - port_walk.tail_offset),
                  scalar_declared);

    t.check(port_walk.tail_offset == project_walk.tail_offset,
            label + ": the tail starts where Project's does");
    t.equal(port_bytes.size() - port_walk.tail_offset,
            project_bytes.size() - project_walk.tail_offset,
            label + ": the tail length equals Project's");
}

void compare_marker(TestContext& t, std::string const& label,
                    CfbDocument const& control, CfbDocument const& null_save,
                    CfbDocument const& project, CfbDocument const& port) {
    /* Props14 is the marker every measured editor save carries; the pair
     * comparison normalises the fixture-builder region the same way every
     * other wave's Props14 check does. */
    save_pair::Verdict const verdict = save_pair::compare(
        save_pair::Kind::props14, control.read_stream("Props14"),
        null_save.read_stream("Props14"), project.read_stream("Props14"),
        port.read_stream("Props14"), true);
    t.check(verdict.lengths_ok,
            label + " Props14: the normalised lengths agree");
    std::string message =
        label + " Props14: equals Project's edit save outside the mask";
    if (verdict.mismatches != 0) {
        message += " (first of " + std::to_string(verdict.mismatches) +
                   " at byte " + std::to_string(verdict.first_mismatch) +
                   ": port " + std::to_string(verdict.output_byte) +
                   ", Project " + std::to_string(verdict.result_byte) + ")";
    }
    t.check(verdict.mismatches == 0, message);
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

/* The writer's marker is the same one every measured editor save carries. */
PropsRecordWrite const& write_for(PropsEditWrite const& written,
                                  std::uint32_t id) {
    for (PropsRecordWrite const& write : written.record_writes) {
        if (write.id == id) {
            return write;
        }
    }
    throw std::runtime_error("no recorded write for the id");
}

/* ---- the four committed pairs ------------------------------------------ */

void test_wbs_pair(TestContext& t) {
    fixture::require_committed(join(MPPWRITE_TEST_DATA, "wave30-wbs-control.mpp"));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, "wave30-wbs-control-null.mpp"));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, "wave30-wbs-code.mpp"));
    CfbDocument const control = doc_from_data("wave30-wbs-control.mpp");
    CfbDocument const null_save = doc_from_data("wave30-wbs-control-null.mpp");
    CfbDocument const project = doc_from_data("wave30-wbs-code.mpp");

    Bytes const control_bytes = control.read_stream(mppwrite::kPropsBackEnd);
    Bytes const project_bytes = project.read_stream(mppwrite::kPropsBackEnd);
    PropsWalk const control_walk = walk_props(control_bytes);
    PropsWalk const project_walk = walk_props(project_bytes);
    t.equal(control_bytes.size(), 88724, "wbs: the control's 114/Props is 88,724 B");
    t.equal(project_bytes.size(), 88748, "wbs: Project's edit save is 88,748 B");
    t.equal(control_walk.records.size(), 161, "wbs: the control holds 161 records");
    t.equal(control_walk.tail_offset, 3316, "wbs: the control's tail starts at 3,316");

    Bytes const payload_8b = record_payload(
        project_bytes, project_walk, 0x0240138Bu, "wbs: 0x0240138b");
    Bytes const payload_be = record_payload(
        project_bytes, project_walk, 0x024013BEu, "wbs: 0x024013be");
    t.equal(record_payload(control_bytes, control_walk, 0x0240138Bu, "wbs")
                .size(),
            4, "wbs: 0x0240138b is 4 B in the control");
    t.equal(record_payload(control_bytes, control_walk, 0x024013BEu, "wbs")
                .size(),
            2, "wbs: 0x024013be is 2 B in the control");
    t.equal(payload_8b.size(), 24, "wbs: Project stores a 24 B payload");
    t.equal(payload_be.size(), 6, "wbs: Project stores a 6 B payload");
    t.equal(scalar_value(control_bytes, mppwrite::kPropsTaskSizeTag, "wbs"),
            512, "wbs: the control's Task size is 512");
    t.equal(scalar_value(project_bytes, mppwrite::kPropsTaskSizeTag, "wbs"),
            552, "wbs: Project's Task size is 552");

    PropsEdit edit;
    edit.records.push_back({0x0240138Bu, 0, payload_8b});
    edit.records.push_back({0x024013BEu, 0, payload_be});
    edit.scalars.push_back({mppwrite::kPropsTaskSizeTag, 552});
    CfbDocument doc = control;
    CfbDocument const before = doc;
    PropsEditWrite const written =
        mppwrite::save_props_edit(doc, edit, out_path("w34-props-wbs.mpp"));
    t.equal(written.records_before, 161, "wbs: records before");
    t.equal(written.records_after, 161, "wbs: records after");
    t.equal(written.stream_bytes_before, 88724, "wbs: stream before");
    t.equal(written.stream_bytes_after, 88748, "wbs: stream after");
    t.equal(written.tail_offset_before, 3316, "wbs: tail before");
    t.equal(written.tail_offset_after, 3340, "wbs: tail after");
    t.equal(written.scalars_updated, 1, "wbs: one scalar updated");
    t.equal(written.record_writes.size(), 2, "wbs: two records updated");
    PropsRecordWrite const& w8b = write_for(written, 0x0240138Bu);
    t.equal(w8b.old_payload_bytes, 4, "wbs: 0x0240138b old payload");
    t.equal(w8b.new_payload_bytes, 24, "wbs: 0x0240138b new payload");
    t.equal(w8b.old_offset, w8b.new_offset,
            "wbs: 0x0240138b keeps its position");
    PropsRecordWrite const& wbe = write_for(written, 0x024013BEu);
    t.equal(wbe.old_payload_bytes, 2, "wbs: 0x024013be old payload");
    t.equal(wbe.new_payload_bytes, 6, "wbs: 0x024013be new payload");

    /* The port's stream structure, independent of the comparison. */
    Bytes const port_bytes = doc.read_stream(mppwrite::kPropsBackEnd);
    PropsWalk const port_walk = walk_props(port_bytes);
    t.equal(u32_at(port_bytes, 0), 88744, "wbs: size word 1 is 88,744");
    t.equal(u32_at(port_bytes, 4), 88744, "wbs: size word 2 is 88,744");
    t.equal(u32_at(port_bytes, 8), 0x00010000u, "wbs: the format word is kept");
    t.equal(u32_at(port_bytes, 12), 262, "wbs: the +12 word is preserved");
    t.equal(port_walk.tail_offset, 3340, "wbs: the tail moved to 3,340");
    t.equal(scalar_value(port_bytes, mppwrite::kPropsTaskSizeTag, "wbs"), 552,
            "wbs: the written Task size is 552");
    t.equal(wbe.new_offset, project_walk.records[find_record(
                                project_walk, 0x024013BEu, "wbs")]
                                .offset,
            "wbs: 0x024013be sits where Project's does");

    check_only_owned_streams(t, before, doc, "wbs",
                             {mppwrite::kPropsBackEnd, "Props14"});
    compare_stream_fields(t, "wbs", control, null_save, project, doc,
                          mppwrite::kPropsBackEnd, {0x0240138Bu, 0x024013BEu},
                          true);
    compare_marker(t, "wbs", control, null_save, project, doc);
}

void test_field_set_pair(TestContext& t) {
    fixture::require_committed(
        join(MPPWRITE_TEST_DATA, "wave34-field-set-control.mpp"));
    fixture::require_committed(
        join(MPPWRITE_TEST_DATA, "wave34-field-set-control-null.mpp"));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, "wave34-field-set.mpp"));
    CfbDocument const control = doc_from_data("wave34-field-set-control.mpp");
    CfbDocument const null_save =
        doc_from_data("wave34-field-set-control-null.mpp");
    CfbDocument const project = doc_from_data("wave34-field-set.mpp");

    Bytes const control_bytes = control.read_stream(mppwrite::kPropsBackEnd);
    Bytes const project_bytes = project.read_stream(mppwrite::kPropsBackEnd);
    PropsWalk const control_walk = walk_props(control_bytes);
    PropsWalk const project_walk = walk_props(project_bytes);
    t.equal(control_bytes.size(), 88742, "set: the control's 114/Props is 88,742 B");
    t.equal(project_bytes.size(), 88750, "set: Project's edit save is 88,750 B");
    t.equal(control_walk.records.size(), 161, "set: the control holds 161 records");
    t.equal(control_walk.tail_offset, 3334, "set: the control's tail starts at 3,334");

    Bytes const payload_92 = record_payload(
        project_bytes, project_walk, 0x02401392u, "set: 0x02401392");
    Bytes const payload_c5 = record_payload(
        project_bytes, project_walk, 0x024013C5u, "set: 0x024013c5");
    t.equal(record_payload(project_bytes, project_walk, 0x02401392u, "set")
                .size(),
            2, "set: 0x02401392 keeps 2 B");
    t.check(record_payload(control_bytes, control_walk, 0x02401392u, "set") ==
                Bytes({0, 0}),
            "set: 0x02401392 is 00 00 in the control");
    t.check(payload_92 == Bytes({1, 0}),
            "set: 0x02401392 is 01 00 in Project's save");
    t.equal(payload_c5.size(), 16, "set: 0x024013c5 grows to 16 B");
    t.equal(record_payload(control_bytes, control_walk, 0x024013C5u, "set")
                .size(),
            8, "set: 0x024013c5 is 8 B in the control");
    t.equal(scalar_value(control_bytes, mppwrite::kPropsTaskSizeTag, "set"),
            530, "set: the control's Task size is 530");
    t.equal(scalar_value(project_bytes, mppwrite::kPropsTaskSizeTag, "set"),
            574, "set: Project's Task size is 574");

    PropsEdit edit;
    edit.records.push_back({0x02401392u, 2, payload_92});
    edit.records.push_back({0x024013C5u, 0, payload_c5});
    edit.scalars.push_back({mppwrite::kPropsTaskSizeTag, 574});
    CfbDocument doc = control;
    CfbDocument const before = doc;
    PropsEditWrite const written =
        mppwrite::save_props_edit(doc, edit, out_path("w34-props-field-set.mpp"));
    t.equal(written.stream_bytes_before, 88742, "set: stream before");
    t.equal(written.stream_bytes_after, 88750, "set: stream after");
    t.equal(written.tail_offset_before, 3334, "set: tail before");
    t.equal(written.tail_offset_after, 3342, "set: tail after");
    t.equal(written.scalars_updated, 1, "set: one scalar updated");
    t.equal(written.record_writes.size(), 2, "set: two records updated");
    PropsRecordWrite const& w92 = write_for(written, 0x02401392u);
    t.equal(w92.old_payload_bytes, 2, "set: 0x02401392 old payload");
    t.equal(w92.new_payload_bytes, 2, "set: 0x02401392 new payload");
    /* 0x024013c5 sits before 0x02401392 in the stream, so its 8-byte growth
     * moves the later record by 8 and no padding absorbs it. */
    t.equal(w92.new_offset, w92.old_offset + 8,
            "set: 0x02401392 moves by the earlier record's growth");
    PropsRecordWrite const& wc5 = write_for(written, 0x024013C5u);
    t.equal(wc5.old_payload_bytes, 8, "set: 0x024013c5 old payload");
    t.equal(wc5.new_payload_bytes, 16, "set: 0x024013c5 new payload");

    Bytes const port_bytes = doc.read_stream(mppwrite::kPropsBackEnd);
    PropsWalk const port_walk = walk_props(port_bytes);
    t.equal(u32_at(port_bytes, 0), 88746, "set: size word 1 is 88,746");
    t.equal(u32_at(port_bytes, 4), 88746, "set: size word 2 is 88,746");
    t.equal(u32_at(port_bytes, 12), 262, "set: the +12 word is preserved");
    t.equal(port_walk.tail_offset, 3342, "set: the tail moved to 3,342");
    t.equal(scalar_value(port_bytes, mppwrite::kPropsTaskSizeTag, "set"), 574,
            "set: the written Task size is 574");
    t.equal(wc5.new_offset, project_walk.records[find_record(
                                project_walk, 0x024013C5u, "set")]
                                .offset,
            "set: 0x024013c5 sits where Project's does");

    check_only_owned_streams(t, before, doc, "set",
                             {mppwrite::kPropsBackEnd, "Props14"});
    compare_stream_fields(t, "set", control, null_save, project, doc,
                          mppwrite::kPropsBackEnd, {0x02401392u, 0x024013C5u},
                          true);
    compare_marker(t, "set", control, null_save, project, doc);
}

void test_resource_add_pair(TestContext& t) {
    fixture::require_committed(
        join(MPPWRITE_TEST_DATA, "wave30-resource-add-control.mpp"));
    fixture::require_committed(
        join(MPPWRITE_TEST_DATA, "wave30-resource-add-control-null.mpp"));
    fixture::require_committed(
        join(MPPWRITE_TEST_DATA, "wave30-resource-add.mpp"));
    CfbDocument const control = doc_from_data("wave30-resource-add-control.mpp");
    CfbDocument const null_save =
        doc_from_data("wave30-resource-add-control-null.mpp");
    CfbDocument const project = doc_from_data("wave30-resource-add.mpp");

    Bytes const control_bytes = control.read_stream(mppwrite::kPropsBackEnd);
    Bytes const project_bytes = project.read_stream(mppwrite::kPropsBackEnd);
    t.equal(control_bytes.size(), 88722,
            "resource-add: the control's 114/Props is 88,722 B");
    t.equal(project_bytes.size(), 88722,
            "resource-add: the edit save keeps the size");
    t.equal(scalar_value(control_bytes, mppwrite::kPropsResourceCountTag,
                         "resource-add"),
            5, "resource-add: the control's Resource count is 5");
    t.equal(scalar_value(project_bytes, mppwrite::kPropsResourceCountTag,
                         "resource-add"),
            6, "resource-add: Project's Resource count is 6");
    t.equal(scalar_value(control_bytes, mppwrite::kPropsResourceSizeTag,
                         "resource-add"),
            86, "resource-add: the control's Resource size is 86");
    t.equal(scalar_value(project_bytes, mppwrite::kPropsResourceSizeTag,
                         "resource-add"),
            140, "resource-add: Project's Resource size is 140");
    t.equal(scalar_value(control_bytes, mppwrite::kPropsCalendarCountTag,
                         "resource-add"),
            7, "resource-add: the control's Calendar count is 7");
    t.equal(scalar_value(project_bytes, mppwrite::kPropsCalendarCountTag,
                         "resource-add"),
            8, "resource-add: Project's Calendar count is 8");

    PropsEdit edit;
    edit.scalars.push_back({mppwrite::kPropsResourceCountTag, 6});
    edit.scalars.push_back({mppwrite::kPropsResourceSizeTag, 140});
    edit.scalars.push_back({mppwrite::kPropsCalendarCountTag, 8});
    CfbDocument doc = control;
    CfbDocument const before = doc;
    PropsEditWrite const written = mppwrite::save_props_edit(
        doc, edit, out_path("w34-props-resource-add.mpp"));
    t.equal(written.records_before, 161, "resource-add: records before");
    t.equal(written.records_after, 161, "resource-add: records after");
    t.equal(written.stream_bytes_before, 88722, "resource-add: stream before");
    t.equal(written.stream_bytes_after, 88722, "resource-add: stream after");
    t.equal(written.tail_offset_before, 3314, "resource-add: tail before");
    t.equal(written.tail_offset_after, 3314, "resource-add: tail after");
    t.equal(written.scalars_updated, 3, "resource-add: three scalars updated");
    t.equal(written.record_writes.size(), 0,
            "resource-add: no record is touched");

    Bytes const port_bytes = doc.read_stream(mppwrite::kPropsBackEnd);
    t.equal(scalar_value(port_bytes, mppwrite::kPropsResourceCountTag,
                         "resource-add"),
            6, "resource-add: the written Resource count is 6");
    t.equal(scalar_value(port_bytes, mppwrite::kPropsResourceSizeTag,
                         "resource-add"),
            140, "resource-add: the written Resource size is 140");
    t.equal(scalar_value(port_bytes, mppwrite::kPropsCalendarCountTag,
                         "resource-add"),
            8, "resource-add: the written Calendar count is 8");

    check_only_owned_streams(t, before, doc, "resource-add",
                             {mppwrite::kPropsBackEnd, "Props14"});
    compare_stream_fields(t, "resource-add", control, null_save, project, doc,
                          mppwrite::kPropsBackEnd, {}, true);
    compare_marker(t, "resource-add", control, null_save, project, doc);
}

void test_textstyle_pair(TestContext& t) {
    fixture::require_committed(
        join(MPPWRITE_TEST_DATA, "wave30-textstyle-control.mpp"));
    fixture::require_committed(
        join(MPPWRITE_TEST_DATA, "wave30-textstyle-control-null.mpp"));
    fixture::require_committed(join(MPPWRITE_TEST_DATA, "wave30-textstyle.mpp"));
    CfbDocument const control = doc_from_data("wave30-textstyle-control.mpp");
    CfbDocument const null_save =
        doc_from_data("wave30-textstyle-control-null.mpp");
    CfbDocument const project = doc_from_data("wave30-textstyle.mpp");

    Bytes const control_bytes = control.read_stream(mppwrite::kPropsFrontEnd);
    Bytes const project_bytes = project.read_stream(mppwrite::kPropsFrontEnd);
    t.equal(control_bytes.size(), 13102,
            "textstyle: the control's 214/Props is 13,102 B");
    t.equal(project_bytes.size(), 13102,
            "textstyle: the edit save keeps the size");
    t.equal(walk_props(control_bytes).records.size(), 47,
            "textstyle: the control holds 47 records");
    t.equal(scalar_value(control_bytes, mppwrite::kPropsTaskSizeTag,
                         "textstyle"),
            100178, "textstyle: the control's CV_iew size is 100,178");
    t.equal(scalar_value(project_bytes, mppwrite::kPropsTaskSizeTag,
                         "textstyle"),
            100234, "textstyle: Project's CV_iew size is 100,234");

    PropsEdit edit;
    edit.stream = mppwrite::kPropsFrontEnd;
    edit.scalars.push_back({mppwrite::kPropsTaskSizeTag, 100234});
    CfbDocument doc = control;
    CfbDocument const before = doc;
    PropsEditWrite const written = mppwrite::save_props_edit(
        doc, edit, out_path("w34-props-textstyle.mpp"));
    t.check(written.stream == "214/Props", "textstyle: the edited stream");
    t.equal(written.records_before, 47, "textstyle: records before");
    t.equal(written.records_after, 47, "textstyle: records after");
    t.equal(written.stream_bytes_before, 13102, "textstyle: stream before");
    t.equal(written.stream_bytes_after, 13102, "textstyle: stream after");
    t.equal(written.tail_offset_before, 9902, "textstyle: tail before");
    t.equal(written.tail_offset_after, 9902, "textstyle: tail after");
    t.equal(written.scalars_updated, 1, "textstyle: one scalar updated");
    t.equal(scalar_value(doc.read_stream(mppwrite::kPropsFrontEnd),
                         mppwrite::kPropsTaskSizeTag, "textstyle"),
            100234, "textstyle: the written CV_iew size is 100,234");

    check_only_owned_streams(t, before, doc, "textstyle",
                             {mppwrite::kPropsFrontEnd, "Props14"});
    compare_stream_fields(t, "textstyle", control, null_save, project, doc,
                          mppwrite::kPropsFrontEnd, {}, true);
    compare_marker(t, "textstyle", control, null_save, project, doc);
}

/* ---- refusals ----------------------------------------------------------- */

void test_edit_refusals(TestContext& t) {
    std::string const missing = missing_path();
    CfbDocument const control = doc_from_data("wave30-wbs-control.mpp");
    auto refuses = [&](CfbDocument const& source, PropsEdit const& edit,
                       std::string_view needle, std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        expect_error(t,
                     [&] { (void)mppwrite::save_props_edit(doc, edit, missing); },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
        t.check(!file_exists(missing), what + ": an output file was written");
    };

    Bytes const bytes = control.read_stream(mppwrite::kPropsBackEnd);
    PropsWalk const walk = walk_props(bytes);
    Bytes const control_8b =
        record_payload(bytes, walk, 0x0240138Bu, "refusals");
    Bytes const bigger_8b(24, 0);
    Bytes const smaller_8b(2, 0);

    {
        PropsEdit edit;
        refuses(control, edit, "no record and no scalar edit",
                "no edit refused");
    }
    {
        PropsEdit edit;
        edit.stream = "114/TBkndTask/Props";
        edit.scalars.push_back({mppwrite::kPropsTaskSizeTag, 1});
        refuses(control, edit, "is not a stream this writer has measured",
                "a per-table Props stream refused");
    }
    {
        PropsEdit edit;
        edit.stream = mppwrite::kPropsFrontEnd;
        edit.records.push_back({0x0240138Bu, 0, bigger_8b});
        refuses(control, edit, "no measured pair changes a 214/Props record",
                "a 214/Props record edit refused");
    }
    {
        PropsEdit edit;
        edit.records.push_back({0x024013FFu, 0, Bytes(4, 0)});
        refuses(control, edit, "inserting a record is not a measured shape",
                "an absent record (the insert) refused");
    }
    {
        CfbDocument duplicated = with_stream(
            "wave30-wbs-control.mpp", mppwrite::kPropsBackEnd,
            with_duplicate_record(bytes, 0x02400002u, "refusals"));
        PropsEdit edit;
        edit.records.push_back({0x02400002u, 4, Bytes(20, 0)});
        refuses(duplicated, edit, "refusing to choose",
                "a record present twice refused");
    }
    {
        PropsEdit edit;
        edit.records.push_back({0x0240138Bu, 1, bigger_8b});
        refuses(control, edit, "every measured update keeps the kind",
                "a kind change refused");
    }
    {
        PropsEdit edit;
        edit.records.push_back({0x0240138Bu, 0, control_8b});
        refuses(control, edit, "would keep its payload",
                "an equal payload refused");
    }
    {
        PropsEdit edit;
        edit.records.push_back({0x0240138Bu, 0, smaller_8b});
        refuses(control, edit, "would shrink", "a smaller payload refused");
    }
    {
        PropsEdit edit;
        edit.records.push_back({0x0240138Bu, 0, bigger_8b});
        edit.records.push_back({0x0240138Bu, 0, bigger_8b});
        refuses(control, edit, "twice; one update per record per save",
                "a duplicate record request refused");
    }
    {
        PropsEdit edit;
        edit.scalars.push_back({0x02000002u, 1});
        refuses(control, edit, "is not measured on 114/Props",
                "an unlisted scalar tag refused");
    }
    {
        PropsEdit edit;
        edit.scalars.push_back({mppwrite::kPropsTaskSizeTag, 552});
        edit.scalars.push_back({mppwrite::kPropsTaskSizeTag, 553});
        refuses(control, edit, "twice; one update per tag per save",
                "a duplicate scalar request refused");
    }
    {
        CfbDocument damaged = with_stream(
            "wave30-wbs-control.mpp", mppwrite::kPropsBackEnd,
            without_scalar(bytes, mppwrite::kPropsTaskFreedTag, "refusals"));
        PropsEdit edit;
        edit.scalars.push_back({mppwrite::kPropsTaskFreedTag, 40});
        refuses(damaged, edit, "has no scalar record for tag",
                "an absent scalar tag refused");
    }
    {
        CfbDocument duplicated = with_stream(
            "wave30-wbs-control.mpp", mppwrite::kPropsBackEnd,
            with_duplicate_scalar(bytes, mppwrite::kPropsTaskSizeTag,
                                  "refusals"));
        PropsEdit edit;
        edit.scalars.push_back({mppwrite::kPropsTaskSizeTag, 552});
        refuses(duplicated, edit, "more than one scalar record for tag",
                "a tag twice in the stream refused");
    }
    {
        CfbDocument inside = with_stream(
            "wave30-wbs-control.mpp", mppwrite::kPropsBackEnd,
            with_scalar_inside_records(bytes, mppwrite::kPropsTaskFreedTag,
                                       "refusals"));
        PropsEdit edit;
        edit.scalars.push_back({mppwrite::kPropsTaskFreedTag, 40});
        refuses(inside, edit, "inside the record area",
                "a scalar hit outside the tail refused");
    }
    {
        PropsEdit edit;
        edit.scalars.push_back({mppwrite::kPropsTaskSizeTag, 512});
        refuses(control, edit, "would keep the value",
                "an equal scalar value refused");
    }
}

void test_damaged_streams(TestContext& t) {
    std::string const missing = missing_path();
    auto refuses = [&](CfbDocument const& source, std::string_view needle,
                       std::string const& what) {
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        CfbDocument doc = source;
        Bytes const before = doc.to_bytes();
        PropsEdit edit;
        edit.scalars.push_back({mppwrite::kPropsTaskSizeTag, 552});
        expect_error(t,
                     [&] { (void)mppwrite::save_props_edit(doc, edit, missing); },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
        t.check(!file_exists(missing), what + ": an output file was written");
    };

    CfbDocument const control = doc_from_data("wave30-wbs-control.mpp");
    Bytes const bytes = control.read_stream(mppwrite::kPropsBackEnd);
    PropsWalk const walk = walk_props(bytes);

    {
        refuses(with_stream("wave30-wbs-control.mpp", mppwrite::kPropsBackEnd,
                            Bytes(8, 0)),
                "shorter than its 16-byte header",
                "a stream shorter than its header refused");
    }
    {
        Bytes damaged = bytes;
        put_u32_at(damaged, 0, static_cast<std::uint32_t>(bytes.size()));
        refuses(with_stream("wave30-wbs-control.mpp", mppwrite::kPropsBackEnd,
                            damaged),
                "not stream size - 4", "a wrong size word refused");
    }
    {
        Bytes damaged = bytes;
        put_u32_at(damaged, 8, 0);
        refuses(with_stream("wave30-wbs-control.mpp", mppwrite::kPropsBackEnd,
                            damaged),
                "format word is not", "a wrong format word refused");
    }
    {
        Bytes damaged = bytes;
        put_u32_at(damaged, walk.records[1].offset,
                   static_cast<std::uint32_t>(bytes.size()));
        refuses(with_stream("wave30-wbs-control.mpp", mppwrite::kPropsBackEnd,
                            damaged),
                "runs past the stream end",
                "a record that runs past the stream refused");
    }
}

void test_path_refusals(TestContext& t) {
    CfbDocument const control = doc_from_data("wave30-wbs-control.mpp");
    PropsEdit edit;
    edit.scalars.push_back({mppwrite::kPropsTaskSizeTag, 552});
    auto refuses = [&](std::string const& path, std::string_view needle,
                       std::string const& what) {
        CfbDocument doc = control;
        Bytes const before = doc.to_bytes();
        expect_error(t,
                     [&] { (void)mppwrite::save_props_edit(doc, edit, path); },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
    };
    refuses("", "the output path is empty", "an empty path refused");
    refuses(MPPWRITE_TEST_OUT, "is a directory", "a directory path refused");
    refuses(out_path("no-such-dir/w34-props.mpp"), "does not exist",
            "a missing parent directory refused");
    t.check(!file_exists(out_path("no-such-dir/w34-props.mpp")),
            "a missing parent directory: an output file was written");
}

} /* namespace */

int main() {
    try {
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);
        TestContext t;
        test_wbs_pair(t);
        test_field_set_pair(t);
        test_resource_add_pair(t);
        test_textstyle_pair(t);
        test_edit_refusals(t);
        test_damaged_streams(t);
        test_path_refusals(t);
        return t.report();
    } catch (std::exception const& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << std::endl;
        return 1;
    }
}
