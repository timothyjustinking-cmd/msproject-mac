/*
 * roundtrip_field_values.cpp -- W48: write Task custom-field values and prove
 * them from the bytes on disk.  W52 adds the CLEAR (empty-value) cases.
 *
 * CLAIM UNDER TEST (measured encodings: docs/analysis/oracle/w10-oracle.md §2):
 *
 *   Text1     0x0b400033  VarMeta block, UTF-16LE + NUL
 *   Number1   0x0b400057  VarMeta block, 8-byte little-endian f64
 *   Number2   0x0b400058  the same family
 *   Duration1 0x0b400067  VarMeta block, u32 file ticks (4800 = 1 day; COM min x 10)
 *   Date1     0x0b400109  VarMeta block, packed u32
 *   Flag1     0x0b400048  Task FixedMeta entry +24 bit 0x02 (NOT VarMeta), with
 *                         Project's own Fixed2Meta mirror (+31/+61 bit 0x80)
 *
 * W52 CLEAR (core/mppwrite/field_values.h: clear_field_value): store the
 * measured EMPTY value (Text = 2-byte UTF-16 NUL, Number = f64 0.0, Duration =
 * u32 0, Date = 0xffffffff, Flag1 = bit clear) and clear the FixedMeta tail
 * presence bits + the Flag1 mirror.  The VarMeta entry is KEPT and the Props
 * registries are byte-identical: there is no measured field-delete shape.
 *
 * For every write/clear the test asserts, from the written file:
 *   1. the value reads back through the writer's own re-read AND (when the
 *      top-level build provides core/mpp) through the independent reader;
 *   2. the exact payload bytes and VarMeta entry offsets;
 *   3. the FixedMeta entry's +2 field count and +24 Flag1 bit, and the
 *      Fixed2Meta +31/+61 mirror;
 *   4. only FixedMeta / Fixed2Meta / VarMeta / Var2Data changed -- FixedData and
 *      Fixed2Data are byte-identical, every other stream (214/ included) too;
 *   5. the file re-emits byte-identically, `check()` and `check_table()` pass;
 *   6. the refusals fire (unknown id, Cost (unmeasured), Flag2 (bit unmeasured),
 *      a field without a measured tail bit, wrong kind, uid 0, unknown uid,
 *      duplicate edit, invalid date, NaN number, embedded NUL, missing output
 *      directory).
 *
 * NO CORPUS NEEDED for the write cases: tests/data/synthetic-base.mpp is the
 * committed synthetic source.  Project's measured w10-fieldvalues.mpp is read
 * when present (corpus/ is git-ignored) to show the reader agrees with the
 * fixture that defined the encodings.
 *
 * OUTPUT (build tree, git-ignored): w48-fields.mpp, w48-fields-rewrite.mpp,
 * w48-flag-on.mpp, w48-flag-off.mpp, w52-fields-cleared.mpp (W52: the six-field
 * file with every measured value cleared) and w48-manifest.json (size + FNV-1a
 * 64), written to ${MPPWRITE_TEST_OUT} and mirrored to
 * ${MPPWRITE_FIXTURE_MIRROR}.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "field_values.h"
#include "save.h"
#include "table.h"

#ifdef MPPWRITE_HAVE_READER
#include "field_value.h"
#include "reader.h"
#endif

#ifndef MPPWRITE_PROBE_DIR
#define MPPWRITE_PROBE_DIR "corpus/probe"
#endif
#ifndef MPPWRITE_TEST_DATA
#define MPPWRITE_TEST_DATA "tests/data"
#endif
#ifndef MPPWRITE_TEST_OUT
#define MPPWRITE_TEST_OUT "core/mppwrite/build/test-out"
#endif
#ifndef MPPWRITE_FIXTURE_MIRROR
#define MPPWRITE_FIXTURE_MIRROR "core/mppwrite/build/vs-w52/test-out"
#endif

namespace {

using mppwrite::FieldEdit;
using mppwrite::FieldKind;
using mppwrite::FieldValue;

char const* kFixed = "114/TBkndTask/FixedData";
char const* kMeta = "114/TBkndTask/FixedMeta";
char const* kFixed2 = "114/TBkndTask/Fixed2Data";
char const* kFixed2Meta = "114/TBkndTask/Fixed2Meta";
char const* kVarMeta = "114/TBkndTask/VarMeta";
char const* kVar2 = "114/TBkndTask/Var2Data";
char const* kProps = "114/TBkndTask/Props";
char const* kAssnProps = "114/TBkndAssn/Props";

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

std::vector<std::uint8_t> read_file(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path);
    }
    input.seekg(0, std::ios::end);
    std::streamoff const length = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

void write_file(std::string const& path, std::vector<std::uint8_t> const& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create " + path);
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<char const*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        throw std::runtime_error("cannot write " + path);
    }
}

bool file_exists(std::string const& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::uint64_t fnv1a64(std::vector<std::uint8_t> const& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::uint8_t const byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hex64(std::uint64_t value) {
    static char const* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (std::size_t i = 0; i < 16; ++i) {
        out[15 - i] = digits[(value >> (4 * i)) & 0xFu];
    }
    return "0x" + out;
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

std::vector<std::string> changed_streams(mppwrite::CfbDocument const& a,
                                         mppwrite::CfbDocument const& b) {
    std::vector<std::string> out;
    for (std::string const& path : a.stream_paths()) {
        if (!b.has_stream(path)) {
            out.push_back(path + " (missing)");
        } else if (a.read_stream(path) != b.read_stream(path)) {
            out.push_back(path);
        }
    }
    return out;
}

std::string join_list(std::vector<std::string> const& items) {
    std::string out;
    for (std::string const& item : items) {
        if (!out.empty()) {
            out += ", ";
        }
        out += item;
    }
    return out;
}

bool contains(std::vector<std::string> const& items, std::string_view wanted) {
    return std::find(items.begin(), items.end(), wanted) != items.end();
}

void check_214_untouched(TestContext& t, mppwrite::CfbDocument const& before,
                         mppwrite::CfbDocument const& after, std::string const& label) {
    std::size_t compared = 0;
    for (std::string const& path : before.stream_paths()) {
        if (path.rfind("214/", 0) != 0) {
            continue;
        }
        ++compared;
        t.check(after.has_stream(path), label + ": 214/ stream vanished: " + path);
        if (after.has_stream(path)) {
            t.check(after.read_stream(path) == before.read_stream(path),
                    label + ": 214/ stream changed: " + path);
        }
    }
    t.check(compared > 0, label + ": no 214/ streams found");
}

/* FixedMeta entry index for a FixedData row (the entry's +4 word is the row
 * offset, so a row lookup goes through the offset). */
int meta_entry_for_row(std::vector<std::uint8_t> const& meta, std::size_t row) {
    std::uint32_t const wanted = static_cast<std::uint32_t>(mppwrite::task_row_offset(row));
    std::size_t const entries = (meta.size() - 16) / 47;
    for (std::size_t i = 0; i < entries; ++i) {
        if (u32_at(meta, 16 + i * 47 + 4) == wanted) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

struct Block {
    std::uint32_t offset = 0;
    std::vector<std::uint8_t> payload;
};

/* The first VarMeta block for (uid, field id), or an empty payload. */
bool block_for(mppwrite::CfbDocument const& doc, std::uint32_t uid,
               std::uint32_t field_id, Block& out) {
    std::vector<mppwrite::VarEntry> const entries =
        mppwrite::read_var_entries(doc.read_stream(kVarMeta));
    std::vector<std::uint8_t> const var2 = doc.read_stream(kVar2);
    for (mppwrite::VarEntry const& entry : entries) {
        if (entry.field_id == field_id && entry.mid == uid) {
            out.offset = entry.offset;
            out.payload = mppwrite::read_var_block(var2, entry.offset);
            return true;
        }
    }
    return false;
}

std::size_t var_entry_count(mppwrite::CfbDocument const& doc) {
    return mppwrite::read_var_entries(doc.read_stream(kVarMeta)).size();
}

struct Fixture {
    std::string file;
    std::size_t bytes = 0;
    std::uint64_t fnv = 0;
    std::string note;
};

std::vector<Fixture>& fixtures() {
    static std::vector<Fixture> list;
    return list;
}

void emit(TestContext& t, mppwrite::CfbDocument const& doc, std::string const& name,
          std::string const& note) {
    std::string const path = out_path(name);
    doc.save(path);
    std::vector<std::uint8_t> const bytes = read_file(path);
    t.check(bytes == doc.to_bytes(), "emit " + name + ": saved bytes differ");
    Fixture fixture;
    fixture.file = name;
    fixture.bytes = bytes.size();
    fixture.fnv = fnv1a64(bytes);
    fixture.note = note;
    fixtures().push_back(fixture);

    std::error_code error;
    std::filesystem::create_directories(MPPWRITE_FIXTURE_MIRROR, error);
    write_file(join(MPPWRITE_FIXTURE_MIRROR, name), bytes);
}

void write_manifest(TestContext& t) {
    std::string json = "{\n  \"wave\": \"w48\",\n  \"generated_by\": "
                       "\"mppwrite_roundtrip_field_values\",\n  \"fixtures\": [\n";
    for (std::size_t i = 0; i < fixtures().size(); ++i) {
        Fixture const& fixture = fixtures()[i];
        json += "    {\"file\": \"" + fixture.file + "\", \"bytes\": " +
                std::to_string(fixture.bytes) + ", \"fnv1a64\": \"" +
                hex64(fixture.fnv) + "\", \"note\": \"" + fixture.note + "\"}";
        json += (i + 1 == fixtures().size()) ? "\n" : ",\n";
    }
    json += "  ]\n}\n";
    std::vector<std::uint8_t> const bytes(json.begin(), json.end());
    write_file(out_path("w48-manifest.json"), bytes);
    std::error_code error;
    std::filesystem::create_directories(MPPWRITE_FIXTURE_MIRROR, error);
    write_file(join(MPPWRITE_FIXTURE_MIRROR, "w48-manifest.json"), bytes);
    t.check(file_exists(out_path("w48-manifest.json")), "manifest was not written");
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

FieldValue value_for(FieldKind kind, std::uint32_t field_id) {
    FieldValue value;
    value.kind = kind;
    value.field_id = field_id;
    return value;
}

/* ---- (a) all measured kinds in one write -------------------------------- */

void test_write_all(TestContext& t) {
    std::string const source = join(MPPWRITE_TEST_DATA, "synthetic-base.mpp");
    mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
    mppwrite::CfbDocument const before = doc;
    std::size_t const entries_before = var_entry_count(doc);
    std::vector<std::uint8_t> const meta_before = doc.read_stream(kMeta);
    int const entry = meta_entry_for_row(meta_before, 1);
    t.check(entry >= 0, "synthetic: row 1 has a FixedMeta entry");
    t.equal(u16_at(meta_before, 16 + static_cast<std::size_t>(entry) * 47 + 2), 11,
            "synthetic: uid 1 starts with 11 variable fields");

    std::vector<FieldEdit> edits;
    FieldEdit text;
    text.uid = 1;
    text.value = value_for(FieldKind::text, mppwrite::kText1FieldId);
    text.value.text = "W48 alpha";
    edits.push_back(text);
    FieldEdit number;
    number.uid = 1;
    number.value = value_for(FieldKind::number, mppwrite::kNumber1FieldId);
    number.value.number = 42.5;
    edits.push_back(number);
    FieldEdit number2;
    number2.uid = 1;
    number2.value = value_for(FieldKind::number, 0x0B400058u);
    number2.value.number = 3.25;
    edits.push_back(number2);
    FieldEdit duration;
    duration.uid = 1;
    duration.value = value_for(FieldKind::duration, mppwrite::kDuration1FieldId);
    duration.value.duration_ticks = 4800;
    edits.push_back(duration);
    FieldEdit date;
    date.uid = 1;
    date.value = value_for(FieldKind::date, mppwrite::kDate1FieldId);
    date.value.packed_date = mppwrite::pack_date_ymd(2026, 10, 26, 8 * 3600);
    edits.push_back(date);
    FieldEdit flag;
    flag.uid = 1;
    flag.value = value_for(FieldKind::flag, mppwrite::kFlag1FieldId);
    flag.value.flag = true;
    edits.push_back(flag);

    mppwrite::FieldWriteResult const result =
        mppwrite::save_field_values(doc, edits, out_path("w48-fields.mpp"));
    t.equal(result.fields_written, std::size_t{6}, "write: six fields written");
    t.check(doc.to_bytes() == read_file(out_path("w48-fields.mpp")),
            "write: document equals the file on disk");

    /* Result values, in edit order. */
    t.check(result.fields.size() == 6, "write: six receipts");
    if (result.fields.size() == 6) {
        t.check(result.fields[0].value.text == "W48 alpha", "receipt: Text1");
        t.check(result.fields[1].value.number == 42.5, "receipt: Number1");
        t.check(result.fields[2].value.number == 3.25, "receipt: Number2");
        t.check(result.fields[3].value.duration_ticks == 4800, "receipt: Duration1");
        t.check(result.fields[4].value.packed_date ==
                    mppwrite::pack_date_ymd(2026, 10, 26, 8 * 3600),
                "receipt: Date1");
        t.check(result.fields[5].value.flag, "receipt: Flag1 true");
    }

    /* VarMeta grew by 6 entries: five value fields + Project's internal
     * 0x0b4000b7 duration marker.  Every entry points at a measured block. */
    std::size_t const entries_after = var_entry_count(doc);
    t.equal(entries_after, entries_before + 6, "write: VarMeta grew by six entries");
    Block block;
    t.check(block_for(doc, 1, mppwrite::kText1FieldId, block),
            "write: Text1 has a VarMeta entry");
    t.check(block.payload == mppwrite::encode_utf16z("W48 alpha"),
            "write: Text1 payload is UTF-16LE + NUL");
    t.check(block_for(doc, 1, mppwrite::kNumber1FieldId, block) &&
                block.payload.size() == 8 && block.payload[0] == 0x00 &&
                block.payload[6] == 0x45 && block.payload[7] == 0x40,
            "write: Number1 payload is the 8-byte f64 42.5");
    t.check(block_for(doc, 1, mppwrite::kDuration1FieldId, block) &&
                block.payload == std::vector<std::uint8_t>({0xC0, 0x12, 0x00, 0x00}),
            "write: Duration1 payload is u32 4800 in file ticks");
    t.check(block_for(doc, 1, mppwrite::kDate1FieldId, block) &&
                block.payload.size() == 4 &&
                u32_at(block.payload, 0) ==
                    mppwrite::pack_date_ymd(2026, 10, 26, 8 * 3600),
            "write: Date1 payload is the packed date word");
    /* Project's internal Duration marker: key 0x0b4000b7, payload 07 00. */
    t.check(block_for(doc, 1, 0x0B4000B7u, block) &&
                block.payload == std::vector<std::uint8_t>({0x07, 0x00}),
            "write: the duration marker 0x0b4000b7 carries 07 00");

    /* FixedMeta: +2 is the row's exact VarMeta entry count (11 + 5 + marker =
     * 17), and the tail presence bits are Project's measured set. */
    std::vector<std::uint8_t> const meta = doc.read_stream(kMeta);
    int const entry_after = meta_entry_for_row(meta, 1);
    t.check(entry_after >= 0, "write: row 1 keeps its FixedMeta entry");
    if (entry_after >= 0) {
        std::size_t const base = 16 + static_cast<std::size_t>(entry_after) * 47;
        t.equal(u16_at(meta, base + 2), 17, "write: FixedMeta +2 is 11 + 5 + marker");
        t.equal(meta[base + 21], 0x20, "write: tail +21 has Text1's bit");
        t.equal(meta[base + 24], 0x02, "write: tail +24 has Flag1's bit");
        t.equal(meta[base + 25], 0x18, "write: tail +25 has Number1/Number2's bits");
        t.equal(meta[base + 26], 0x01, "write: tail +26 has Duration1's first bit");
        t.equal(meta[base + 27], 0x20, "write: tail +27 has Duration1's second bit");
        t.equal(meta[base + 29], 0x40, "write: tail +29 has Date1's bit");
    }
    /* Fixed2Meta mirror (Project's own Flag1 write shape). */
    std::vector<std::uint8_t> const fixed2_meta = doc.read_stream(kFixed2Meta);
    if (entry_after >= 0) {
        std::size_t const base = 16 + static_cast<std::size_t>(entry_after) * 96;
        t.equal(fixed2_meta[base + 31], 0x80, "write: Fixed2Meta +31 mirror bit");
        t.equal(fixed2_meta[base + 61], 0x80, "write: Fixed2Meta +61 mirror bit");
    }

    /* Streams: FixedData/Fixed2Data byte-identical; five streams change (the
     * sixth is TBkndTask/Props, the field registry). */
    t.check(doc.read_stream(kFixed) == before.read_stream(kFixed),
            "write: FixedData changed");
    t.check(doc.read_stream(kFixed2) == before.read_stream(kFixed2),
            "write: Fixed2Data changed");
    std::vector<std::string> const changed = changed_streams(before, doc);
    t.check(changed.size() == 6 && contains(changed, kMeta) &&
                contains(changed, kFixed2Meta) && contains(changed, kVarMeta) &&
                contains(changed, kVar2) && contains(changed, kProps) &&
                contains(changed, kAssnProps),
            "write: changed streams are not exactly the five value streams: " +
                join_list(changed));
    /* TBkndTask/Props: 86-byte preamble + one 88-byte record per written field,
     * sorted by field id, with the three measured size words (W48 gate). */
    std::vector<std::uint8_t> const props = doc.read_stream(kProps);
    t.equal(props.size(), std::size_t{86 + 6 * 88},
            "write: Props is 86 + 6*88 bytes");
    t.equal(u32_at(props, 0), static_cast<std::uint32_t>(props.size() - 4),
            "write: Props +0 is size-4");
    t.equal(u32_at(props, 4), static_cast<std::uint32_t>(props.size() - 4),
            "write: Props +4 is size-4");
    t.equal(u16_at(props, 30), static_cast<std::uint16_t>(props.size() - 42),
            "write: Props +30 is size-42");
    t.equal(u16_at(props, 78), 6, "write: Props +78 is the record count");
    t.equal(u16_at(props, 82), 6 * 88, "write: Props +82 is count*88");
    /* The Assignment-side mirror (same six fields, sorted by assignment
     * property id 0x09,0x0a,0x28,0x3c,0x3d,0x64, total u32 at +32). */
    std::vector<std::uint8_t> const assn_props = doc.read_stream(kAssnProps);
    t.equal(assn_props.size(), std::size_t{88 + 6 * 88},
            "write: Assn Props is 88 + 6*88 bytes");
    if (assn_props.size() == 88 + 6 * 88) {
        t.equal(u32_at(assn_props, 0),
                static_cast<std::uint32_t>(assn_props.size() - 4),
                "write: Assn Props +0 is size-4");
        t.equal(u32_at(assn_props, 32), 44u + 6u * 88u,
                "write: Assn Props +32 is 44+88*n");
        t.equal(u32_at(assn_props, 80), 6u, "write: Assn Props +80 is the count");
        t.equal(u32_at(assn_props, 84), 6u * 88u, "write: Assn Props +84 is count*88");
        std::array<std::uint32_t, 6> const property_ids = {0x0F404000u, 0x0F40400Au,
                                                          0x0F404028u, 0x0F40403Cu,
                                                          0x0F40403Du, 0x0F404064u};
        for (std::size_t i = 0; i < property_ids.size(); ++i) {
            t.equal(u32_at(assn_props, 88 + i * 88), property_ids[i],
                    "write: Assn Props record " + std::to_string(i) +
                        " is the right property id");
        }
    }
    std::array<std::uint32_t, 6> const registered = {
        mppwrite::kText1FieldId,  mppwrite::kFlag1FieldId,
        mppwrite::kNumber1FieldId, 0x0B400058u,
        mppwrite::kDuration1FieldId, mppwrite::kDate1FieldId};
    for (std::size_t i = 0; i < registered.size(); ++i) {
        t.equal(u32_at(props, 86 + i * 88), registered[i],
                "write: Props record " + std::to_string(i) + " is the right field");
    }
    /* When Project's own value file is present, our registry must be byte-
     * identical to it: same six fields, same order, same size words. */
    {
        std::string const measured = join(MPPWRITE_PROBE_DIR, "w10-fieldvalues.mpp");
        if (file_exists(measured)) {
            mppwrite::CfbDocument const project =
                mppwrite::CfbDocument::from_file(measured);
            t.check(project.read_stream(kProps) == props,
                    "write: Props is byte-identical to Project's w10-fieldvalues.mpp");
        } else {
            std::cout << "SKIP: " << measured
                      << " is missing (corpus/ is git-ignored); the Props "
                         "byte-for-byte check is skipped\n";
        }
    }
    check_214_untouched(t, before, doc, "w48-fields.mpp");
    t.check(mppwrite::check_table(doc).empty(),
            "write: check_table: " + mppwrite::check_table(doc));
    t.check(doc.check().empty(), "write: container check: " + doc.check());
    t.equal(u32_at(doc.read_stream(kVarMeta), 8), entries_after,
            "write: VarMeta +8 declares the new entry count");
    t.equal(u32_at(doc.read_stream(kVarMeta), 0x14),
            doc.read_stream(kVar2).size(),
            "write: VarMeta +0x14 is the new Var2Data size");

    /* THE PROJECT-GATE PROPERTY (measured on Project's own w10-fieldvalues.mpp:
     * [0]*9, [1]*14, [2]*8): each uid's entries are CONTIGUOUS, uids ascend, and
     * the keys ascend inside a uid.  Our first attempt appended the five new
     * entries after uid 3 and Project reopened the file with every value empty.
     * Text1 must land at index 10, immediately after Name, exactly as in
     * Project's file. */
    std::vector<mppwrite::VarEntry> const entries =
        mppwrite::read_var_entries(doc.read_stream(kVarMeta));
    std::vector<std::uint32_t> sequence;
    for (mppwrite::VarEntry const& var_entry : entries) {
        sequence.push_back(var_entry.mid);
    }
    std::vector<std::uint32_t> expected_sequence;
    expected_sequence.insert(expected_sequence.end(), 9, 0u);  /* project row */
    expected_sequence.insert(expected_sequence.end(), 17, 1u); /* 11 + 5 fields + marker */
    expected_sequence.insert(expected_sequence.end(), 11, 2u);
    expected_sequence.insert(expected_sequence.end(), 11, 3u);
    t.check(sequence == expected_sequence,
            "write: VarMeta is not grouped [0]*9 [1]*17 [2]*11 [3]*11");
    t.equal(entries[10].mid, 1u, "write: uid 1's group starts at index 9");
    t.equal(entries[10].field_id, mppwrite::kText1FieldId,
            "write: Text1 sits immediately after Name, as in Project's file");
    bool grouped_and_sorted = true;
    for (std::size_t i = 1; i < entries.size(); ++i) {
        std::uint32_t const previous = entries[i - 1].mid & 0x7FFFFFFFu;
        std::uint32_t const current = entries[i].mid & 0x7FFFFFFFu;
        if (current < previous) {
            grouped_and_sorted = false;
        }
        if (current == previous && entries[i - 1].field_id > entries[i].field_id) {
            grouped_and_sorted = false;
        }
    }
    t.check(grouped_and_sorted,
            "write: VarMeta uids do not ascend or keys do not ascend inside a uid");

    emit(t, doc, "w48-fields.mpp",
         "synthetic-base uid 1: Text1/Number1/Number2/Duration1/Date1/Flag1 true");
}

/* ---- (b) overwrite the same fields: entries repointed, counts stable ----- */

void test_rewrite(TestContext& t) {
    mppwrite::CfbDocument doc =
        mppwrite::CfbDocument::from_file(out_path("w48-fields.mpp"));
    std::size_t const entries_before = var_entry_count(doc);
    std::size_t const var2_before = doc.read_stream(kVar2).size();
    std::vector<std::uint8_t> const meta_before = doc.read_stream(kMeta);
    int const entry = meta_entry_for_row(meta_before, 1);
    std::uint16_t const count_before =
        entry >= 0 ? u16_at(meta_before, 16 + static_cast<std::size_t>(entry) * 47 + 2)
                   : 0;

    std::vector<FieldEdit> edits;
    FieldEdit text;
    text.uid = 1;
    text.value = value_for(FieldKind::text, mppwrite::kText1FieldId);
    text.value.text = "beta";
    edits.push_back(text);
    FieldEdit number;
    number.uid = 1;
    number.value = value_for(FieldKind::number, mppwrite::kNumber1FieldId);
    number.value.number = -7.25;
    edits.push_back(number);
    FieldEdit duration;
    duration.uid = 1;
    duration.value = value_for(FieldKind::duration, mppwrite::kDuration1FieldId);
    duration.value.duration_ticks = 9600;
    edits.push_back(duration);
    FieldEdit date;
    date.uid = 1;
    date.value = value_for(FieldKind::date, mppwrite::kDate1FieldId);
    date.value.packed_date = mppwrite::pack_date_ymd(2026, 11, 2, 8 * 3600);
    edits.push_back(date);
    FieldEdit flag;
    flag.uid = 1;
    flag.value = value_for(FieldKind::flag, mppwrite::kFlag1FieldId);
    flag.value.flag = false;
    edits.push_back(flag);

    mppwrite::FieldWriteResult const result =
        mppwrite::save_field_values(doc, edits, out_path("w48-fields-rewrite.mpp"));
    t.equal(result.fields_written, std::size_t{5}, "rewrite: five fields written");
    t.equal(var_entry_count(doc), entries_before,
            "rewrite: the VarMeta entry count is unchanged");
    std::vector<std::uint8_t> const meta = doc.read_stream(kMeta);
    int const entry_after = meta_entry_for_row(meta, 1);
    t.check(entry_after >= 0 && u16_at(meta, 16 +
                                               static_cast<std::size_t>(entry_after) * 47 +
                                               2) == count_before,
            "rewrite: the FixedMeta field count is unchanged");
    if (entry_after >= 0) {
        std::size_t const base = 16 + static_cast<std::size_t>(entry_after) * 47;
        t.equal(meta[base + 24], 0x00, "rewrite: Flag1's bit is cleared");
    }
    std::vector<std::uint8_t> const fixed2_meta = doc.read_stream(kFixed2Meta);
    if (entry_after >= 0) {
        std::size_t const base = 16 + static_cast<std::size_t>(entry_after) * 96;
        t.equal(fixed2_meta[base + 31], 0x00, "rewrite: Fixed2Meta +31 cleared");
        t.equal(fixed2_meta[base + 61], 0x00, "rewrite: Fixed2Meta +61 cleared");
    }
    t.check(doc.read_stream(kVar2).size() > var2_before,
            "rewrite: the new blocks were appended to Var2Data");
    Block block;
    t.check(block_for(doc, 1, mppwrite::kText1FieldId, block) &&
                block.payload == mppwrite::encode_utf16z("beta"),
            "rewrite: Text1 reads 'beta'");
    t.check(block_for(doc, 1, mppwrite::kNumber1FieldId, block) &&
                block.payload.size() == 8,
            "rewrite: Number1 has an 8-byte block");
    t.check(block_for(doc, 1, mppwrite::kDuration1FieldId, block) &&
                block.payload == std::vector<std::uint8_t>({0x80, 0x25, 0x00, 0x00}),
            "rewrite: Duration1 is 9600 ticks");
    std::vector<std::string> const changed =
        changed_streams(mppwrite::CfbDocument::from_file(out_path("w48-fields.mpp")),
                        doc);
    t.check(changed.size() == 4 && contains(changed, kMeta) &&
                contains(changed, kFixed2Meta) && contains(changed, kVarMeta) &&
                contains(changed, kVar2),
            "rewrite: changed streams: " + join_list(changed));
    /* A rewrite of already-registered fields must not touch the registry. */
    t.check(doc.read_stream(kProps) ==
                mppwrite::CfbDocument::from_file(out_path("w48-fields.mpp"))
                    .read_stream(kProps),
            "rewrite: TBkndTask/Props changed; registration is once per field");
    t.check(mppwrite::check_table(doc).empty(),
            "rewrite: check_table: " + mppwrite::check_table(doc));
    emit(t, doc, "w48-fields-rewrite.mpp",
         "w48-fields + Text1 'beta', Number1 -7.25, Duration1 9600, Date1 11-02, "
         "Flag1 false (entries repointed, counts unchanged)");
}

/* ---- (c) a flag-only write touches exactly FixedMeta and Fixed2Meta ------ */

void test_flag_only(TestContext& t) {
    std::string const source = join(MPPWRITE_TEST_DATA, "synthetic-base.mpp");
    mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
    mppwrite::CfbDocument const before = doc;
    std::vector<std::uint8_t> const var_meta_before = doc.read_stream(kVarMeta);
    std::vector<std::uint8_t> const var2_before = doc.read_stream(kVar2);

    std::vector<FieldEdit> on;
    FieldEdit set;
    set.uid = 2;
    set.value = value_for(FieldKind::flag, mppwrite::kFlag1FieldId);
    set.value.flag = true;
    on.push_back(set);
    (void)mppwrite::save_field_values(doc, on, out_path("w48-flag-on.mpp"));
    std::vector<std::string> const changed_on = changed_streams(before, doc);
    t.check(changed_on.size() == 4 && contains(changed_on, kMeta) &&
                contains(changed_on, kFixed2Meta) && contains(changed_on, kProps) &&
                contains(changed_on, kAssnProps),
            "flag on: changed streams: " + join_list(changed_on));
    std::vector<std::uint8_t> const props_on = doc.read_stream(kProps);
    t.equal(props_on.size(), std::size_t{86 + 88},
            "flag on: Props gained the Flag1 registration record");
    t.equal(u32_at(props_on, 86), mppwrite::kFlag1FieldId,
            "flag on: the Props record is Flag1's");
    t.check(doc.read_stream(kVarMeta) == var_meta_before &&
                doc.read_stream(kVar2) == var2_before,
            "flag on: VarMeta/Var2Data changed");
    std::vector<std::uint8_t> const meta = doc.read_stream(kMeta);
    int const entry = meta_entry_for_row(meta, 2);
    if (entry >= 0) {
        std::size_t const base = 16 + static_cast<std::size_t>(entry) * 47;
        t.equal(meta[base + 24], 0x02, "flag on: uid 2 FixedMeta +24 bit 0x02");
    }
    t.check(doc.read_stream(kFixed) == before.read_stream(kFixed),
            "flag on: FixedData changed");
    t.check(doc.read_stream(kFixed2) == before.read_stream(kFixed2),
            "flag on: Fixed2Data changed");
    emit(t, doc, "w48-flag-on.mpp", "synthetic-base uid 2: Flag1 true");

    std::vector<FieldEdit> off;
    FieldEdit clear;
    clear.uid = 2;
    clear.value = value_for(FieldKind::flag, mppwrite::kFlag1FieldId);
    clear.value.flag = false;
    off.push_back(clear);
    mppwrite::CfbDocument from_on =
        mppwrite::CfbDocument::from_file(out_path("w48-flag-on.mpp"));
    std::vector<std::uint8_t> const props_before_off = from_on.read_stream(kProps);
    (void)mppwrite::save_field_values(from_on, off, out_path("w48-flag-off.mpp"));
    t.check(from_on.read_stream(kMeta) == before.read_stream(kMeta),
            "flag off: FixedMeta is byte-identical to the source (bit cleared)");
    t.check(from_on.read_stream(kFixed2Meta) == before.read_stream(kFixed2Meta),
            "flag off: Fixed2Meta is byte-identical to the source");
    /* Registration is permanent: clearing a value does not unregister the field,
     * so the Props stream keeps Flag1's record (it differs from the source now). */
    t.check(from_on.read_stream(kProps) == props_before_off,
            "flag off: Props changed; registration is not undone by clearing");
    emit(t, from_on, "w48-flag-off.mpp",
         "w48-flag-on minus Flag1; the Flag1 registration record stays in Props");
}

/* ---- (d) refusals ------------------------------------------------------- */

void test_refusals(TestContext& t) {
    std::string const source = join(MPPWRITE_TEST_DATA, "synthetic-base.mpp");
    mppwrite::CfbDocument const pristine = mppwrite::CfbDocument::from_file(source);

    auto refuses = [&](std::string_view needle, std::string const& what,
                       auto&& build) {
        mppwrite::CfbDocument doc = pristine;
        std::vector<std::uint8_t> const before = doc.to_bytes();
        std::vector<FieldEdit> const edits = build();
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_field_values(
                             doc, edits, out_path("w48-refused.mpp"));
                     },
                     needle, what);
        t.check(doc.to_bytes() == before, what + ": the document changed");
        t.check(!file_exists(out_path("w48-refused.mpp")),
                what + ": an output file was written");
    };

    refuses("empty", "empty edit list", [] { return std::vector<FieldEdit>{}; });
    refuses("not a task custom field", "unknown field id", [] {
        std::vector<FieldEdit> edits;
        FieldEdit edit;
        edit.uid = 1;
        edit.value = value_for(FieldKind::unknown, 0x0B40000Eu); /* Name */
        edits.push_back(edit);
        return edits;
    });
    refuses("storage is unmeasured", "Cost1 is refused (unmeasured)", [] {
        std::vector<FieldEdit> edits;
        FieldEdit edit;
        edit.uid = 1;
        edit.value = value_for(FieldKind::cost, mppwrite::kCost1FieldId);
        edit.value.number = 12.5;
        edits.push_back(edit);
        return edits;
    });
    refuses("Flag2", "Flag2 is refused (bit position unmeasured)", [] {
        std::vector<FieldEdit> edits;
        FieldEdit edit;
        edit.uid = 1;
        edit.value = value_for(FieldKind::flag, 0x0B400049u);
        edit.value.flag = true;
        edits.push_back(edit);
        return edits;
    });
    refuses("no measured registration record", "Number3 is refused (no Props template)",
            [] {
                std::vector<FieldEdit> edits;
                FieldEdit edit;
                edit.uid = 1;
                edit.value = value_for(FieldKind::number, 0x0B400059u);
                edit.value.number = 1.0;
                edits.push_back(edit);
                return edits;
            });
    refuses("kind must match", "a mismatched kind is refused", [] {
        std::vector<FieldEdit> edits;
        FieldEdit edit;
        edit.uid = 1;
        edit.value.field_id = mppwrite::kText1FieldId;
        edit.value.kind = FieldKind::number;
        edits.push_back(edit);
        return edits;
    });
    refuses("uid 0", "uid 0 (project row) is refused", [] {
        std::vector<FieldEdit> edits;
        FieldEdit edit;
        edit.uid = 0;
        edit.value = value_for(FieldKind::text, mppwrite::kText1FieldId);
        edit.value.text = "x";
        edits.push_back(edit);
        return edits;
    });
    refuses("no task row", "an unknown uid is refused", [] {
        std::vector<FieldEdit> edits;
        FieldEdit edit;
        edit.uid = 99;
        edit.value = value_for(FieldKind::number, mppwrite::kNumber1FieldId);
        edits.push_back(edit);
        return edits;
    });
    refuses("appears twice", "a duplicate (uid, field) edit is refused", [] {
        std::vector<FieldEdit> edits;
        FieldEdit first;
        first.uid = 1;
        first.value = value_for(FieldKind::number, mppwrite::kNumber1FieldId);
        first.value.number = 1.0;
        edits.push_back(first);
        FieldEdit second = first;
        second.value.number = 2.0;
        edits.push_back(second);
        return edits;
    });
    refuses("is not a packed date", "an invalid date is refused", [] {
        std::vector<FieldEdit> edits;
        FieldEdit edit;
        edit.uid = 1;
        edit.value = value_for(FieldKind::date, mppwrite::kDate1FieldId);
        edit.value.packed_date = mppwrite::kInvalidDate;
        edits.push_back(edit);
        return edits;
    });
    refuses("not finite", "a NaN number is refused", [] {
        std::vector<FieldEdit> edits;
        FieldEdit edit;
        edit.uid = 1;
        edit.value = value_for(FieldKind::number, mppwrite::kNumber1FieldId);
        edit.value.number = std::numeric_limits<double>::quiet_NaN();
        edits.push_back(edit);
        return edits;
    });
    refuses("embedded NUL", "text with an embedded NUL is refused", [] {
        std::vector<FieldEdit> edits;
        FieldEdit edit;
        edit.uid = 1;
        edit.value = value_for(FieldKind::text, mppwrite::kText1FieldId);
        edit.value.text = std::string("a\0b", 3);
        edits.push_back(edit);
        return edits;
    });
    {
        mppwrite::CfbDocument doc = pristine;
        std::vector<FieldEdit> edits;
        FieldEdit edit;
        edit.uid = 1;
        edit.value = value_for(FieldKind::number, mppwrite::kNumber1FieldId);
        edits.push_back(edit);
        expect_error(t,
                     [&] {
                         (void)mppwrite::save_field_values(
                             doc, edits, "core/mppwrite/no-such-dir/w48.mpp");
                     },
                     "does not exist", "a missing output directory is refused");
    }
}

/* ---- (e) the id table agrees with core/mpp's ---------------------------- */

#ifdef MPPWRITE_HAVE_READER
void test_reader_cross_check(TestContext& t) {
    FieldKind const kinds[] = {FieldKind::text,   FieldKind::flag,
                               FieldKind::number, FieldKind::duration,
                               FieldKind::cost,   FieldKind::date};
    std::size_t compared = 0;
    for (FieldKind const kind : kinds) {
        for (unsigned index = 1; index <= 30; ++index) {
            std::uint32_t const id = mppwrite::field_id_for(kind, index);
            if (id == 0) {
                continue;
            }
            ++compared;
            t.check(id == mpp::custom_field_id(static_cast<mpp::FieldKind>(kind), index),
                    "id table mismatch at kind " +
                        std::to_string(static_cast<int>(kind)) + " index " +
                        std::to_string(index));
        }
    }
    t.equal(compared, std::size_t{100},
            "cross-check: the writer and reader id tables agree on 100 ids");

    /* Read the written file through core/mpp: the independent reader must see
     * every value the writer proved. */
    mpp::Document const written = mpp::load_file(out_path("w48-fields.mpp"));
    t.equal(written.tasks.size(), std::size_t{3}, "w48-fields: three tasks");
    mpp::CustomFieldValue const text = written.custom_field(1, 0x0B400033u);
    t.check(text.present && text.text == "W48 alpha", "reader: Text1 is 'W48 alpha'");
    mpp::CustomFieldValue const number = written.custom_field(1, 0x0B400057u);
    t.check(number.present && number.number == 42.5, "reader: Number1 is 42.5");
    mpp::CustomFieldValue const number2 = written.custom_field(1, 0x0B400058u);
    t.check(number2.present && number2.number == 3.25, "reader: Number2 is 3.25");
    mpp::CustomFieldValue const duration = written.custom_field(1, 0x0B400067u);
    t.check(duration.present && duration.duration_ticks == 4800,
            "reader: Duration1 is 4800 ticks");
    mpp::CustomFieldValue const date = written.custom_field(1, 0x0B400109u);
    t.check(date.present && date.packed_date ==
                              mppwrite::pack_date_ymd(2026, 10, 26, 8 * 3600),
            "reader: Date1 is 2026-10-26 08:00");
    mpp::CustomFieldValue const flag = written.custom_field(1, 0x0B400048u);
    t.check(flag.present && flag.flag, "reader: Flag1 is true");
    t.check(!written.custom_field(2, 0x0B400033u).present,
            "reader: uid 2 has no Text1");

    /* The rewrite file: cleared flag and the new values. */
    mpp::Document const rewritten = mpp::load_file(out_path("w48-fields-rewrite.mpp"));
    t.check(rewritten.custom_field(1, 0x0B400033u).text == "beta",
            "reader: rewrite Text1 is 'beta'");
    t.check(rewritten.custom_field(1, 0x0B400057u).number == -7.25,
            "reader: rewrite Number1 is -7.25");
    t.check(!rewritten.custom_field(1, 0x0B400048u).flag,
            "reader: rewrite Flag1 is false");
}

/* Project's measured field-values file, when the corpus is present: the read
 * API must reproduce exactly what defined the encodings. */
void test_measured_fixture(TestContext& t) {
    std::string const path = join(MPPWRITE_PROBE_DIR, "w10-fieldvalues.mpp");
    if (!file_exists(path)) {
        std::cout << "SKIP: " << path << " is missing (corpus/ is git-ignored)\n";
        return;
    }
    mpp::Document const document = mpp::load_file(path);
    mpp::CustomFieldValue const text = document.custom_field(1, 0x0B400033u);
    t.check(text.present && text.text == "alpha",
            "measured: w10-fieldvalues uid 1 Text1 is 'alpha'");
    mpp::CustomFieldValue const number = document.custom_field(1, 0x0B400057u);
    t.check(number.present && number.number == 42.5,
            "measured: w10-fieldvalues uid 1 Number1 is 42.5");
    mpp::CustomFieldValue const duration = document.custom_field(1, 0x0B400067u);
    t.check(duration.present && duration.duration_ticks == 4800,
            "measured: w10-fieldvalues uid 1 Duration1 is 4800 ticks");
    mpp::CustomFieldValue const date = document.custom_field(1, 0x0B400109u);
    t.check(date.present && date.packed_date == 0x3D1812C0u,
            "measured: w10-fieldvalues uid 1 Date1 is 0x3d1812c0");
    mpp::CustomFieldValue const flag = document.custom_field(1, 0x0B400048u);
    t.check(flag.present && flag.flag,
            "measured: w10-fieldvalues uid 1 Flag1 is true");
}
#else
void test_reader_cross_check(TestContext&) {}
void test_measured_fixture(TestContext&) {}
#endif

/* ---- (e2) W52: clear every measured field ------------------------------- */

bool warns(std::vector<std::string> const& warnings, std::string_view needle) {
    for (std::string const& warning : warnings) {
        if (warning.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void test_clear(TestContext& t) {
    std::string const source = out_path("w48-fields.mpp");
    mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_file(source);
    mppwrite::CfbDocument const before = doc;
    std::vector<std::uint8_t> const props_before = doc.read_stream(kProps);
    std::vector<std::uint8_t> const assn_props_before = doc.read_stream(kAssnProps);
    std::vector<std::uint8_t> const fixed_before = doc.read_stream(kFixed);
    std::vector<std::uint8_t> const fixed2_before = doc.read_stream(kFixed2);
    std::vector<std::uint8_t> const meta_before = doc.read_stream(kMeta);
    std::size_t const entries_before = var_entry_count(doc);
    std::size_t const var2_before = doc.read_stream(kVar2).size();
    int const entry = meta_entry_for_row(meta_before, 1);
    t.check(entry >= 0, "clear: row 1 has a FixedMeta entry");
    if (entry < 0) {
        return;
    }
    std::size_t const meta_base = 16 + static_cast<std::size_t>(entry) * 47;
    std::size_t const fixed2_base = 16 + static_cast<std::size_t>(entry) * 96;
    t.equal(u16_at(meta_before, meta_base + 2), 17,
            "clear: the source row has 17 variable fields");

    struct Step {
        std::uint32_t field_id;
        char const* name;
        char const* file;
    };
    std::array<Step, 6> const steps = {{
        {mppwrite::kText1FieldId, "Text1", "w52-clear-text.mpp"},
        {mppwrite::kNumber1FieldId, "Number1", "w52-clear-number1.mpp"},
        {0x0B400058u, "Number2", "w52-clear-number2.mpp"},
        {mppwrite::kDuration1FieldId, "Duration1", "w52-clear-duration1.mpp"},
        {mppwrite::kDate1FieldId, "Date1", "w52-clear-date1.mpp"},
        {mppwrite::kFlag1FieldId, "Flag1", "w52-fields-cleared.mpp"},
    }};
    for (Step const& step : steps) {
        std::string const label = std::string("clear ") + step.name;
        mppwrite::ClearedField const cleared = mppwrite::clear_field_value(
            doc, 1, step.field_id, out_path(step.file));
        t.equal(cleared.uid, 1, label + ": uid");
        t.equal(cleared.field_id, step.field_id, label + ": field id");
        t.check(cleared.was_present, label + ": the value was present");
        if (step.field_id == mppwrite::kFlag1FieldId) {
            t.check(!cleared.flag, label + ": the flag reads back false");
            t.check(!cleared.entry_kept, label + ": a flag has no VarMeta entry");
        } else {
            t.check(cleared.entry_kept, label + ": the VarMeta entry is kept");
            t.check(cleared.value.present && cleared.value.storage_measured,
                    label + ": the empty value is stored and decodable");
            switch (cleared.kind) {
                case mppwrite::FieldKind::text:
                    t.check(cleared.value.text.empty(), label + ": text is empty");
                    break;
                case mppwrite::FieldKind::number:
                    t.check(cleared.value.number == 0.0, label + ": number is 0.0");
                    break;
                case mppwrite::FieldKind::duration:
                    t.check(cleared.value.duration_ticks == 0,
                            label + ": duration is 0 ticks");
                    break;
                case mppwrite::FieldKind::date:
                    t.check(cleared.value.packed_date == mppwrite::kInvalidDate,
                            label + ": date is 0xffffffff (NA)");
                    break;
                case mppwrite::FieldKind::flag:
                case mppwrite::FieldKind::cost:
                case mppwrite::FieldKind::unknown:
                    break;
            }
        }
    }

    /* Exact empty payloads, read straight from Var2Data. */
    Block block;
    t.check(block_for(doc, 1, mppwrite::kText1FieldId, block) &&
                block.payload == std::vector<std::uint8_t>({0x00, 0x00}),
            "clear: Text1 payload is the 2-byte empty UTF-16 string");
    t.check(block_for(doc, 1, mppwrite::kNumber1FieldId, block) &&
                block.payload == std::vector<std::uint8_t>(8, 0x00),
            "clear: Number1 payload is f64 0.0");
    t.check(block_for(doc, 1, 0x0B400058u, block) &&
                block.payload == std::vector<std::uint8_t>(8, 0x00),
            "clear: Number2 payload is f64 0.0");
    t.check(block_for(doc, 1, mppwrite::kDuration1FieldId, block) &&
                block.payload == std::vector<std::uint8_t>(4, 0x00),
            "clear: Duration1 payload is u32 0");
    t.check(block_for(doc, 1, mppwrite::kDate1FieldId, block) &&
                block.payload == std::vector<std::uint8_t>({0xFF, 0xFF, 0xFF, 0xFF}),
            "clear: Date1 payload is 0xffffffff");

    /* The presence bits and the flag mirror are clear, the entry count is not. */
    std::vector<std::uint8_t> const meta = doc.read_stream(kMeta);
    t.equal(u16_at(meta, meta_base + 2), 17,
            "clear: FixedMeta +2 is unchanged (entries kept)");
    t.equal(meta[meta_base + 21], 0x00, "clear: Text1 tail bit cleared");
    t.equal(meta[meta_base + 24], 0x00, "clear: Flag1 tail bit cleared");
    t.equal(meta[meta_base + 25], 0x00,
            "clear: Number1/Number2 tail bits cleared");
    t.equal(meta[meta_base + 26], 0x00, "clear: Duration1 first tail bit cleared");
    t.equal(meta[meta_base + 27], 0x00,
            "clear: Duration1 second tail bit cleared");
    t.equal(meta[meta_base + 29], 0x00, "clear: Date1 tail bit cleared");
    std::vector<std::uint8_t> const fixed2_meta = doc.read_stream(kFixed2Meta);
    t.equal(fixed2_meta[fixed2_base + 31], 0x00,
            "clear: Flag1 Fixed2Meta +31 mirror cleared");
    t.equal(fixed2_meta[fixed2_base + 61], 0x00,
            "clear: Flag1 Fixed2Meta +61 mirror cleared");

    /* Streams: FixedData/Fixed2Data/FixedMeta sizes and the registries are
     * untouched; VarMeta keeps its entry count; Var2Data only grew. */
    t.check(doc.read_stream(kFixed) == fixed_before,
            "clear: FixedData is byte-identical");
    t.check(doc.read_stream(kFixed2) == fixed2_before,
            "clear: Fixed2Data is byte-identical");
    t.check(doc.read_stream(kProps) == props_before,
            "clear: Task/Props is byte-identical (field stays registered)");
    t.check(doc.read_stream(kAssnProps) == assn_props_before,
            "clear: Assn/Props is byte-identical");
    t.equal(var_entry_count(doc), entries_before,
            "clear: the VarMeta entry count is unchanged");
    t.check(doc.read_stream(kVar2).size() == var2_before + 46,
            "clear: Var2Data grew by the five empty blocks (46 bytes)");
    t.check(doc.read_stream(kMeta).size() == meta_before.size(),
            "clear: FixedMeta size is unchanged");
    std::vector<std::string> const changed = changed_streams(before, doc);
    t.check(changed.size() == 4 && contains(changed, kMeta) &&
                contains(changed, kFixed2Meta) && contains(changed, kVarMeta) &&
                contains(changed, kVar2),
            "clear: only FixedMeta/Fixed2Meta/VarMeta/Var2Data changed");
    check_214_untouched(t, before, doc, "clear");
    t.check(mppwrite::check_table(doc).empty(),
            "clear: check_table: " + mppwrite::check_table(doc));
    t.check(doc.check().empty(), "clear: container check: " + doc.check());
    t.check(doc.to_bytes() == read_file(out_path("w52-fields-cleared.mpp")),
            "clear: the document equals the file on disk");
    emit(t, doc, "w52-fields-cleared.mpp",
         "w48-fields.mpp with Text1/Number1/Number2/Duration1/Date1/Flag1 "
         "cleared to the measured empty encodings; Props untouched");

    /* Idempotent: clearing the same field again writes the same bytes. */
    mppwrite::CfbDocument const cleared = doc;
    mppwrite::ClearedField const again = mppwrite::clear_field_value(
        doc, 1, mppwrite::kText1FieldId, out_path("w52-clear-again.mpp"));
    t.check(warns(again.warnings, "already empty"),
            "clear again: the call reports the no-op");
    t.check(doc.to_bytes() == cleared.to_bytes(),
            "clear again: the file is byte-identical");

    /* Clearing a field the row never stored adds no entry and changes nothing. */
    mppwrite::CfbDocument absent =
        mppwrite::CfbDocument::from_file(join(MPPWRITE_TEST_DATA,
                                              "synthetic-base.mpp"));
    mppwrite::CfbDocument const absent_before = absent;
    mppwrite::ClearedField const absent_clear = mppwrite::clear_field_value(
        absent, 2, mppwrite::kText1FieldId, out_path("w52-clear-absent.mpp"));
    t.check(!absent_clear.was_present, "clear absent: was_present is false");
    t.check(!absent_clear.entry_kept, "clear absent: no entry was inserted");
    t.check(warns(absent_clear.warnings, "no VarMeta entry"),
            "clear absent: the warning says the entry stays absent");
    t.check(absent.to_bytes() == absent_before.to_bytes(),
            "clear absent: the document is unchanged");

    /* Refusals: unknown id, Cost, Flag2, Text2 (no measured tail bit), uid 0,
     * unknown uid and a bad output path. */
    mppwrite::CfbDocument refusals = mppwrite::CfbDocument::from_file(source);
    std::string const missing = out_path("w52-clear-should-not-exist.mpp");
    std::error_code ignored;
    std::filesystem::remove(missing, ignored);
    auto expect_clear_refusal = [&](std::uint32_t uid, std::uint32_t field_id,
                                    std::string_view needle,
                                    std::string const& what) {
        mppwrite::CfbDocument const unchanged = refusals;
        expect_error(
            t,
            [&] {
                (void)mppwrite::clear_field_value(refusals, uid, field_id,
                                                  missing);
            },
            needle, what);
        t.check(refusals.to_bytes() == unchanged.to_bytes(),
                what + ": the document changed on refusal");
        t.check(!file_exists(missing), what + ": the output file was created");
    };
    expect_clear_refusal(1, 0x0B4000FFu, "not a task custom field",
                         "unknown field id refused");
    expect_clear_refusal(1, mppwrite::kCost1FieldId, "Cost", "Cost refused");
    expect_clear_refusal(1, 0x0B400049u, "not stored where it is measured",
                         "Flag2 refused");
    expect_clear_refusal(1, 0x0B400036u, "no measured FixedMeta tail-presence bit",
                         "Text2 refused (no measured tail bit)");
    expect_clear_refusal(0, mppwrite::kText1FieldId, "project row",
                         "uid 0 refused");
    expect_clear_refusal(99, mppwrite::kText1FieldId, "no task row",
                         "unknown uid refused");
    expect_error(
        t,
        [&] {
            (void)mppwrite::clear_field_value(refusals, 1,
                                              mppwrite::kText1FieldId, "");
        },
        "output path", "an empty output path is refused");

#ifdef MPPWRITE_HAVE_READER
    mpp::Document const cleared_file =
        mpp::load_file(out_path("w52-fields-cleared.mpp"));
    mpp::CustomFieldValue const text =
        cleared_file.custom_field(1, mppwrite::kText1FieldId);
    t.check(text.present && text.text.empty(),
            "reader: cleared Text1 is present-and-empty");
    mpp::CustomFieldValue const number =
        cleared_file.custom_field(1, mppwrite::kNumber1FieldId);
    t.check(number.present && number.number == 0.0,
            "reader: cleared Number1 is 0.0");
    mpp::CustomFieldValue const duration =
        cleared_file.custom_field(1, mppwrite::kDuration1FieldId);
    t.check(duration.present && duration.duration_ticks == 0,
            "reader: cleared Duration1 is 0 ticks");
    mpp::CustomFieldValue const date =
        cleared_file.custom_field(1, mppwrite::kDate1FieldId);
    t.check(date.present && date.packed_date == 0xFFFFFFFFu,
            "reader: cleared Date1 is 0xffffffff");
    mpp::CustomFieldValue const flag =
        cleared_file.custom_field(1, mppwrite::kFlag1FieldId);
    t.check(flag.present && !flag.flag, "reader: cleared Flag1 is false");
#endif
}

/* ---- (f) the linked-pin predicate (w11-oracle §1) ----------------------- */

void test_pin_predicate(TestContext& t) {
    std::uint32_t const pin = mppwrite::pack_date_ymd(2026, 10, 21, 8 * 3600);
    std::uint32_t const allowed = mppwrite::pack_date_ymd(2026, 10, 26, 8 * 3600);
    t.check(mppwrite::pin_would_be_clamped(pin, allowed),
            "pin before the dependency-allowed start is reported as clamped");
    t.check(!mppwrite::pin_would_be_clamped(allowed, allowed),
            "pin equal to the allowed start is not clamped");
    t.check(!mppwrite::pin_would_be_clamped(
                mppwrite::pack_date_ymd(2026, 10, 27, 8 * 3600), allowed),
            "a later pin is not clamped");
    t.check(!mppwrite::pin_would_be_clamped(pin, mppwrite::kInvalidDate),
            "an invalid allowed start gives no warning");
    t.check(!mppwrite::pin_would_be_clamped(mppwrite::kInvalidDate, allowed),
            "an invalid pin gives no warning");
}

} /* namespace */

int main() {
    try {
        std::filesystem::create_directories(MPPWRITE_TEST_OUT);
        TestContext t;
        test_write_all(t);
        test_rewrite(t);
        test_flag_only(t);
        test_refusals(t);
        test_reader_cross_check(t);
        test_measured_fixture(t);
        test_clear(t);
        test_pin_predicate(t);
        write_manifest(t);
        return t.report();
    } catch (std::exception const& error) {
        std::cerr << "FAIL: unhandled exception: " << error.what() << std::endl;
        return 1;
    }
}
