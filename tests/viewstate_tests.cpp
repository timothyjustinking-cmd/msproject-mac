// viewstate_tests -- core/viewstate decoder tests.
//
// Three committed blobs under tests/data are raw `214/CTable/Var2Data` streams
// extracted from the synthetic wave-10 prefs fixtures
// corpus/probe/w10-prefs-{base,col,date}.mpp:
//
//   synthetic-viewstate-ctable-base.bin   1051 B, sha256 80573060...
//   synthetic-viewstate-ctable-col.bin    1051 B, sha256 7b753dfe...
//   synthetic-viewstate-ctable-date.bin   1051 B, sha256 e106e864...
//
// They contain only the stock Entry table's column records (field ids and the
// generic "Task Name" title); no client data.  The other three parts of each
// CTable collection are generated here from the measured framing, so the test
// exercises the real fixture bytes for the table definition and the date-format
// word while keeping the fixture small.
//
// corpus/ is never required.

#include "viewstate.h"
#include "gantt_style.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

template <typename A, typename B>
void check_eq(const A& actual, const B& expected, const std::string& what) {
    ++g_checks;
    if (!(actual == expected)) {
        ++g_failures;
        std::ostringstream out;
        out << what << ": got " << actual << ", expected " << expected;
        std::fprintf(stderr, "FAIL: %s\n", out.str().c_str());
    }
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in) {
        return {};
    }
    return bytes;
}

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

void put_utf16(std::vector<std::uint8_t>& bytes, std::size_t offset, const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[offset + i * 2] = static_cast<std::uint8_t>(text[i]);
        bytes[offset + i * 2 + 1] = 0;
    }
}

struct BlockSpec {
    std::uint32_t uid = 0;
    std::uint32_t key = 0;
    std::vector<std::uint8_t> payload;
};

struct Parts {
    std::vector<std::uint8_t> fixed_data;
    std::vector<std::uint8_t> fixed_meta;
    std::vector<std::uint8_t> var_meta;
    std::vector<std::uint8_t> var2;
};

void append_block(std::vector<std::uint8_t>& var2, const std::vector<std::uint8_t>& payload) {
    const std::size_t offset = var2.size();
    var2.resize(offset + 4 + payload.size(), 0);
    put_u32(var2, offset, static_cast<std::uint32_t>(payload.size()));
    for (std::size_t i = 0; i < payload.size(); ++i) {
        var2[offset + 4 + i] = payload[i];
    }
}

// Builds the four parts around a class-specific FixedData layout.
Parts build_parts(std::size_t row_size,
                  const std::vector<std::pair<std::string, std::string>>& rows,
                  const std::vector<BlockSpec>& blocks,
                  std::uint32_t fixed_meta_entry_size = 10) {
    Parts parts;
    parts.fixed_data.assign(row_size * rows.size(), 0);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t base = i * row_size;
        put_u32(parts.fixed_data, base, static_cast<std::uint32_t>(i + 1));
        put_utf16(parts.fixed_data, base + 4, rows[i].first);
        if (row_size == 230) {
            put_utf16(parts.fixed_data, base + 136, rows[i].second);
        }
        for (std::size_t b = 0; b < 16 && base + row_size - 16 + b < parts.fixed_data.size();
             ++b) {
            parts.fixed_data[base + row_size - 16 + b] = static_cast<std::uint8_t>(0xa0 + i);
        }
    }

    const std::size_t fixed_meta_size = 16 + rows.size() * fixed_meta_entry_size;
    parts.fixed_meta.assign(fixed_meta_size, 0);
    put_u32(parts.fixed_meta, 0, 0xFADFADBAu);
    put_u32(parts.fixed_meta, 4, 4);
    put_u32(parts.fixed_meta, 8, static_cast<std::uint32_t>(rows.size()));
    put_u32(parts.fixed_meta, 12, static_cast<std::uint32_t>(row_size * rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t base = 16 + i * fixed_meta_entry_size;
        std::uint16_t var_count = 0;
        for (const auto& block : blocks) {
            if (block.uid == static_cast<std::uint32_t>(i + 1)) {
                ++var_count;
            }
        }
        put_u16(parts.fixed_meta, base, 0);
        put_u16(parts.fixed_meta, base + 2, var_count);
        put_u32(parts.fixed_meta, base + 4, static_cast<std::uint32_t>(i * row_size));
    }

    std::vector<std::uint32_t> offsets;
    offsets.reserve(blocks.size());
    for (const auto& block : blocks) {
        offsets.push_back(static_cast<std::uint32_t>(parts.var2.size()));
        append_block(parts.var2, block.payload);
    }

    parts.var_meta.assign(24 + blocks.size() * 12, 0);
    put_u32(parts.var_meta, 0, 0xFADFADBAu);
    put_u32(parts.var_meta, 8, static_cast<std::uint32_t>(blocks.size()));
    put_u32(parts.var_meta, 20, static_cast<std::uint32_t>(parts.var2.size()));
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const std::size_t base = 24 + i * 12;
        put_u32(parts.var_meta, base, blocks[i].uid);
        put_u32(parts.var_meta, base + 4, offsets[i]);
        put_u32(parts.var_meta, base + 8, blocks[i].key);
    }
    return parts;
}

std::vector<std::uint8_t> make_table_block(std::uint32_t ordinal,
                                           std::uint8_t date_format,
                                           std::uint32_t flags,
                                           const std::vector<std::pair<std::uint32_t,
                                                                       std::string>>& columns) {
    std::vector<std::uint8_t> payload(12 + columns.size() * 115, 0);
    put_u32(payload, 0, ordinal);
    put_u16(payload, 4, static_cast<std::uint16_t>(columns.size() - 1));
    payload[6] = 1;
    payload[7] = date_format;
    put_u32(payload, 8, flags);
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const std::size_t record = 12 + i * 115;
        put_u32(payload, record, columns[i].first);
        put_utf16(payload, record + 13, columns[i].second);
        for (std::size_t b = 0; b < 9; ++b) {
            payload[record + 4 + b] = static_cast<std::uint8_t>(b + 1);
        }
    }
    return payload;
}

// Attaches a raw `214/CTable/Var2Data` blob to a one-row, one-block parts set.
// build_parts cannot do this because the blob is not built from column specs.
void attach_column_blob(Parts& parts, const std::vector<std::uint8_t>& blob) {
    parts.var2 = blob;
    parts.fixed_meta[18] = 1;
    parts.fixed_meta[20] = 0;
    parts.fixed_meta[21] = 0;
    parts.fixed_meta[22] = 0;
    parts.fixed_meta[23] = 0;
    parts.var_meta.resize(36, 0);
    put_u32(parts.var_meta, 8, 1);
    put_u32(parts.var_meta, 20, static_cast<std::uint32_t>(blob.size()));
    put_u32(parts.var_meta, 24, 1);
    put_u32(parts.var_meta, 28, 0);
    put_u32(parts.var_meta, 32, 0x20400006u);
}

// ---------------------------------------------------------------------------
// CV_iew main-block helpers (wave 15, W62).  The item framing measured on the
// synthetic and corpus views: 16-byte header, then {u32 length, u32 id,
// u32 kind, payload}, padded to an even total.
// ---------------------------------------------------------------------------

struct ViewItemSpec {
    std::uint32_t id = 0;
    std::uint32_t kind = 1;
    std::vector<std::uint8_t> payload;
};

std::vector<std::uint8_t> utf16_item(const std::string& text) {
    std::vector<std::uint8_t> out(text.size() * 2 + 2, 0);
    put_utf16(out, 0, text);
    return out;
}

std::vector<std::uint8_t> make_view_main_block(const std::vector<ViewItemSpec>& items) {
    std::size_t total = 16;
    for (const auto& item : items) {
        std::size_t stride = 12 + item.payload.size();
        if ((stride & 1u) != 0) {
            ++stride;
        }
        total += stride;
    }
    std::vector<std::uint8_t> block(total, 0);
    put_u32(block, 0, static_cast<std::uint32_t>(total - 4));
    put_u32(block, 4, static_cast<std::uint32_t>(total - 4));
    put_u32(block, 8, 0x00010000u);
    put_u32(block, 12, static_cast<std::uint32_t>(items.size()));
    std::size_t at = 16;
    for (const auto& item : items) {
        put_u32(block, at, static_cast<std::uint32_t>(item.payload.size()));
        put_u32(block, at + 4, item.id);
        put_u32(block, at + 8, item.kind);
        std::copy(item.payload.begin(), item.payload.end(),
                  block.begin() + static_cast<std::ptrdiff_t>(at + 12));
        std::size_t stride = 12 + item.payload.size();
        if ((stride & 1u) != 0) {
            ++stride;
        }
        at += stride;
    }
    return block;
}

// The measured 970-byte view-format template: four page margins (1/1000 inch)
// then a fifth word (2000 in every synthetic view); the rest stays zero.
std::vector<std::uint8_t> make_view_options() {
    std::vector<std::uint8_t> options(970, 0);
    for (std::size_t i = 0; i < 4; ++i) {
        put_u32(options, i * 4, 500);
    }
    put_u32(options, 16, 2000);
    return options;
}

// The measured notes record: nine u32 chunk lengths followed by the chunks.
std::vector<std::uint8_t> make_view_notes() {
    const std::string first = "{\\rtf1 top}";
    const std::string last = "{\\rtf1 bottom}";
    std::vector<std::uint8_t> notes(36 + first.size() + last.size(), 0);
    put_u32(notes, 0, static_cast<std::uint32_t>(first.size()));
    put_u32(notes, 32, static_cast<std::uint32_t>(last.size()));
    std::copy(first.begin(), first.end(),
              notes.begin() + static_cast<std::ptrdiff_t>(36));
    std::copy(last.begin(), last.end(),
              notes.begin() + static_cast<std::ptrdiff_t>(36 + first.size()));
    return notes;
}

// ---------------------------------------------------------------------------
// Wave 17 (F1) helpers: the 230-byte row record set, the CFilter/CGrouping
// framing, the CEdl Props document and the CV_iew edit fixtures.
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> field_list_bytes(const std::vector<std::uint32_t>& ids,
                                           bool terminator) {
    std::vector<std::uint8_t> out((ids.size() + (terminator ? 1u : 0u)) * 4, 0);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        put_u32(out, i * 4, ids[i]);
    }
    if (terminator) {
        put_u32(out, ids.size() * 4, 0xffffffffu);
    }
    return out;
}

// Writes the inline record set at row + 0x6e.  `records` are {id, payload} in
// the measured order; the set ends where the last payload ends.
void attach_row_records(Parts& parts,
                        std::size_t row_index,
                        std::size_t row_size,
                        const std::vector<std::pair<std::uint32_t,
                                                    std::vector<std::uint8_t>>>& records) {
    const std::size_t base = row_index * row_size + 0x6e;
    std::size_t size = 8 + records.size() * 8;
    for (const auto& record : records) {
        size += record.second.size();
    }
    check(base + size <= parts.fixed_data.size(), "row record set fits the row");
    put_u32(parts.fixed_data, base, static_cast<std::uint32_t>(size));
    put_u32(parts.fixed_data, base + 4, static_cast<std::uint32_t>(records.size()));
    std::size_t at = 8 + records.size() * 8;
    for (std::size_t i = 0; i < records.size(); ++i) {
        put_u32(parts.fixed_data, base + 8 + i * 8, records[i].first);
        put_u32(parts.fixed_data, base + 12 + i * 8, static_cast<std::uint32_t>(at));
        for (std::size_t b = 0; b < records[i].second.size(); ++b) {
            parts.fixed_data[base + at + b] = records[i].second[b];
        }
        at += records[i].second.size();
    }
}

// The measured CFilter block: ordinal at +0, applies-to offset at +16, and the
// UTF-16 "tasks"/"resources" string at that offset.
std::vector<std::uint8_t> make_filter_block(std::uint32_t ordinal,
                                            const std::string& applies_to) {
    const std::size_t offset = 92;
    std::vector<std::uint8_t> payload(offset, 0);
    put_u32(payload, 0, ordinal);
    put_u32(payload, 4, 0x100u);
    put_u16(payload, 14, 2);
    put_u32(payload, 16, static_cast<std::uint32_t>(offset));
    const std::vector<std::uint8_t> text = utf16_item(applies_to);
    payload.insert(payload.end(), text.begin(), text.end());
    return payload;
}

// The measured CGrouping block: ordinal at +0, the rest opaque (83 bytes in the
// synthetic family, and some blocks are only 12 bytes in the corpus file).
std::vector<std::uint8_t> make_grouping_block(std::uint32_t ordinal) {
    std::vector<std::uint8_t> payload(83, 0);
    put_u32(payload, 0, ordinal);
    put_u32(payload, 8, 0x100u);
    return payload;
}

struct TestEdlRecord {
    std::uint32_t id = 0;
    std::vector<std::uint8_t> payload;
};

// A complete Props document as measured for CEdl: header size words are
// stream_size - 4, then records back to back.
std::vector<std::uint8_t> make_edl_document(const std::vector<TestEdlRecord>& records) {
    std::size_t total = 16;
    for (const auto& record : records) {
        total += 12 + record.payload.size();
    }
    std::vector<std::uint8_t> document(total, 0);
    put_u32(document, 0, static_cast<std::uint32_t>(total - 4));
    put_u32(document, 4, static_cast<std::uint32_t>(total - 4));
    put_u32(document, 8, 0x00010000u);
    put_u32(document, 12, static_cast<std::uint32_t>(records.size()));
    std::size_t at = 16;
    for (const auto& record : records) {
        put_u32(document, at, static_cast<std::uint32_t>(record.payload.size()));
        put_u32(document, at + 4, record.id);
        put_u32(document, at + 8, 1);
        for (std::size_t b = 0; b < record.payload.size(); ++b) {
            document[at + 12 + b] = record.payload[b];
        }
        at += 12 + record.payload.size();
    }
    return document;
}

// The synthetic CEdl collection: one 160-byte row, one Props block, plus an
// opaque record that stands in for the measured host path (no real path here).
Parts make_edl_parts(const std::string& view_name,
                     const std::string& table_name,
                     const std::vector<std::uint32_t>& field_ids,
                     bool terminator) {
    std::vector<TestEdlRecord> records;
    std::vector<std::uint8_t> one(4, 0);
    put_u32(one, 0, 1);
    records.push_back({0x26400022u, one});
    records.push_back({0x26400018u, utf16_item(view_name)});
    records.push_back({0x26400019u, utf16_item(table_name)});
    records.push_back({0x26400017u, field_list_bytes(field_ids, terminator)});
    records.push_back({0x26400031u, utf16_item("opaque-host-record")});
    const std::vector<std::uint8_t> document = make_edl_document(records);
    return build_parts(160, {{"", ""}}, {{1, 0x20400006u, document}}, 11);
}

// Two CV_iew views.  View 0 is a single view whose main block carries a display
// name, the applied table, the filter and the 970-byte options template; view 1
// is a second view (a combination view naming view 0) so a block-offset shift
// is observable.
Parts make_view_parts(const std::string& view0_table = "&Entry",
                      const std::string& view0_caption = "&Gantt Chart") {
    std::vector<ViewItemSpec> items0;
    items0.push_back({0x2240002Au, 1, utf16_item("Gantt Chart")});
    items0.push_back({0x2240000Au, 1, utf16_item(view0_table)});
    items0.push_back({0x2240000Bu, 1, utf16_item("&All Tasks")});
    items0.push_back({0x22400018u, 1, utf16_item("No Group")});
    items0.push_back({0x22400009u, 1, make_view_options()});
    const std::vector<std::uint8_t> main0 = make_view_main_block(items0);

    std::vector<ViewItemSpec> items1;
    items1.push_back({0x2240002Au, 1, utf16_item("Gantt Chart with Timeline")});
    items1.push_back({0x2240000Au, 1, utf16_item("&Gantt Chart")});
    items1.push_back({0x2240000Bu, 1, utf16_item("Time&line")});
    const std::vector<std::uint8_t> main1 = make_view_main_block(items1);

    std::vector<BlockSpec> blocks;
    blocks.push_back({1, 0x20400006u, main0});
    blocks.push_back({2, 0x20400006u, main1});
    return build_parts(138, {{view0_caption, ""}, {"Gantt &with Timeline", ""}}, blocks);
}

// Builds a CTable collection from explicit column payloads (used where the
// committed wave-10 blob is not the right shape).
Parts make_ctable_parts_from_payloads(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& tables) {
    std::vector<BlockSpec> blocks;
    std::vector<std::pair<std::string, std::string>> rows;
    for (std::size_t i = 0; i < tables.size(); ++i) {
        blocks.push_back({static_cast<std::uint32_t>(i + 1), 0x20400006u, tables[i].second});
        const std::string caption = tables[i].first;
        std::string display = caption;
        if (!display.empty() && display.front() == '&') {
            display.erase(display.begin());
        }
        rows.push_back({caption, display});
    }
    return build_parts(230, rows, blocks);
}

msproject::viewstate::ViewState parse_or_fail(const std::string& klass,
                                              const Parts& parts,
                                              bool expect_success = true) {
    msproject::viewstate::PartSet part_set;
    part_set.fixed_data = parts.fixed_data;
    part_set.fixed_meta = parts.fixed_meta;
    part_set.var_meta = parts.var_meta;
    part_set.var2_data = parts.var2;
    msproject::viewstate::ViewState state;
    msproject::viewstate::ParseError error;
    const bool ok = msproject::viewstate::parse_view_state(klass, part_set, state, &error);
    if (expect_success) {
        check(ok, "parse_view_state(" + klass + ")");
    }
    if (!ok) {
        std::fprintf(stderr, "parse error: %s at %zu\n", error.message.c_str(), error.offset);
    }
    return state;
}

bool has_warning(const msproject::viewstate::ViewState& state, const std::string& needle) {
    for (const auto& warning : state.warnings) {
        if (warning.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const std::vector<std::uint32_t> kEntryFieldIds = {
    0x0b400017u, 0x0b4000cdu, 0x0b400500u, 0x0b40000eu, 0x0b400507u,
    0x0b400505u, 0x0b400506u, 0x0b40002fu, 0x0b400031u};

std::size_t count_pattern(const std::vector<std::uint8_t>& bytes,
                          const std::array<std::uint8_t, 4>& pattern) {
    std::size_t count = 0;
    if (bytes.size() < pattern.size()) {
        return 0;
    }
    for (std::size_t i = 0; i + pattern.size() <= bytes.size(); ++i) {
        bool match = true;
        for (std::size_t b = 0; b < pattern.size(); ++b) {
            if (bytes[i + b] != pattern[b]) {
                match = false;
                break;
            }
        }
        if (match) {
            ++count;
        }
    }
    return count;
}

void test_real_ctable_blobs() {
    const std::vector<std::uint8_t> base =
        read_file(VIEWSTATE_TEST_DATA "/synthetic-viewstate-ctable-base.bin");
    const std::vector<std::uint8_t> col =
        read_file(VIEWSTATE_TEST_DATA "/synthetic-viewstate-ctable-col.bin");
    const std::vector<std::uint8_t> date =
        read_file(VIEWSTATE_TEST_DATA "/synthetic-viewstate-ctable-date.bin");
    check(!base.empty(), "read base CTable Var2Data blob");
    check(!col.empty(), "read col CTable Var2Data blob");
    check(!date.empty(), "read date CTable Var2Data blob");
    if (base.empty() || col.empty() || date.empty()) {
        return;
    }
    check_eq(base.size(), std::size_t{1051}, "base blob size");
    check_eq(col.size(), std::size_t{1051}, "col blob size");
    check_eq(date.size(), std::size_t{1051}, "date blob size");

    // The fixture pair is single-variable by byte: exactly one byte changed.
    std::vector<std::size_t> base_col_diffs;
    for (std::size_t i = 0; i < base.size(); ++i) {
        if (base[i] != col[i]) {
            base_col_diffs.push_back(i);
        }
    }
    check_eq(base_col_diffs.size(), std::size_t{1}, "base -> col differs in one byte");
    if (base_col_diffs.size() == 1) {
        check_eq(base_col_diffs[0], std::size_t{12}, "base -> col changed table flags byte");
        check_eq(static_cast<int>(base[12]), 6, "base flags byte");
        check_eq(static_cast<int>(col[12]), 2, "col flags byte");
    }
    std::vector<std::size_t> col_date_diffs;
    for (std::size_t i = 0; i < col.size(); ++i) {
        if (col[i] != date[i]) {
            col_date_diffs.push_back(i);
        }
    }
    check_eq(col_date_diffs.size(), std::size_t{1}, "col -> date differs in one byte");
    if (col_date_diffs.size() == 1) {
        check_eq(col_date_diffs[0], std::size_t{11}, "col -> date changed date-format byte");
        check_eq(static_cast<int>(col[11]), 0xff, "col date format byte");
        check_eq(static_cast<int>(date[11]), 0x05, "date date format byte");
    }

    // The oracle "add a Number1 column" call persisted no Number1 field id.
    const std::array<std::uint8_t, 4> number1 = {0x57, 0x00, 0x40, 0x0b};
    check_eq(count_pattern(base, number1), std::size_t{0}, "base has no Number1 field id");
    check_eq(count_pattern(col, number1), std::size_t{0}, "col has no Number1 field id");
    check_eq(count_pattern(date, number1), std::size_t{0}, "date has no Number1 field id");

    const Parts base_parts = build_parts(230, {{"&Entry", "Entry"}}, {}, 10);
    Parts with_base = base_parts;
    attach_column_blob(with_base, base);
    const auto base_state = parse_or_fail("CTable", with_base);
    check_eq(base_state.rows.size(), std::size_t{1}, "base rows");
    check_eq(base_state.tables.size(), std::size_t{1}, "base tables");
    if (base_state.tables.size() == 1) {
        const auto& table = base_state.tables[0];
        check(table.decoded, "base table decoded");
        check_eq(table.caption, std::string("&Entry"), "base table caption");
        check_eq(table.display_caption, std::string("Entry"), "base display caption");
        check_eq(table.column_count(), 9u, "base column count");
        check_eq(table.date_format, static_cast<std::uint8_t>(0xff), "base date format");
        check_eq(table.flags, 6u, "base flags");
        check_eq(table.last_column_index, static_cast<std::uint16_t>(8), "base last index");
        check_eq(table.format_word, static_cast<std::uint8_t>(1), "base format word");
        for (std::size_t i = 0; i < table.columns.size() && i < kEntryFieldIds.size(); ++i) {
            check_eq(table.columns[i].field_id, kEntryFieldIds[i],
                     "base column " + std::to_string(i) + " field id");
        }
        if (table.columns.size() > 3) {
            check_eq(table.columns[3].title, std::string("Task Name"), "Name column title");
        }
        check_eq(table.columns[0].opaque_header[0], static_cast<std::uint8_t>(0x06),
                 "ID column first opaque byte");
    }

    Parts with_col = base_parts;
    attach_column_blob(with_col, col);
    const auto col_state = parse_or_fail("CTable", with_col);
    check_eq(col_state.tables.size(), std::size_t{1}, "col tables");
    if (col_state.tables.size() == 1) {
        check_eq(col_state.tables[0].date_format, static_cast<std::uint8_t>(0xff),
                 "col date format");
        check_eq(col_state.tables[0].flags, 2u, "col flags");
        check_eq(col_state.tables[0].column_count(), 9u, "col column count");
    }

    Parts with_date = base_parts;
    attach_column_blob(with_date, date);
    const auto date_state = parse_or_fail("CTable", with_date);
    check_eq(date_state.tables.size(), std::size_t{1}, "date tables");
    if (date_state.tables.size() == 1) {
        check_eq(date_state.tables[0].date_format, static_cast<std::uint8_t>(0x05),
                 "date date format");
        check_eq(date_state.tables[0].flags, 2u, "date flags");
        check_eq(date_state.tables[0].column_count(), 9u, "date column count");
    }
    const std::string summary = msproject::viewstate::describe_view_state(date_state);
    check(summary.find("date_format=0x00000005") != std::string::npos,
          "describe shows date format 5");
    check(summary.find("columns=9") != std::string::npos, "describe shows 9 columns");
}

void test_generated_ctable() {
    const Parts parts = build_parts(
        230, {{"&Test Table", "Test Table"}}, {});
    Parts with_block = parts;
    const auto payload = make_table_block(
        1, 0x04, 0x0a,
        {{0x0b400017u, ""}, {0x0b40000eu, "Name Col"}, {0x0b400031u, ""}});
    append_block(with_block.var2, payload);
    with_block.fixed_meta[18] = 1;
    with_block.fixed_meta[20] = 0;
    with_block.fixed_meta[21] = 0;
    with_block.fixed_meta[22] = 0;
    with_block.fixed_meta[23] = 0;
    with_block.var_meta.resize(36, 0);
    put_u32(with_block.var_meta, 8, 1);
    put_u32(with_block.var_meta, 20, static_cast<std::uint32_t>(with_block.var2.size()));
    put_u32(with_block.var_meta, 24, 1);
    put_u32(with_block.var_meta, 28, 0);
    put_u32(with_block.var_meta, 32, 0x20400006u);

    const auto state = parse_or_fail("CTable", with_block);
    check_eq(state.tables.size(), std::size_t{1}, "generated tables");
    if (state.tables.size() == 1) {
        const auto& table = state.tables[0];
        check(table.decoded, "generated table decoded");
        check_eq(table.caption, std::string("&Test Table"), "generated caption");
        check_eq(table.display_caption, std::string("Test Table"), "generated display");
        check_eq(table.column_count(), 3u, "generated column count");
        check_eq(table.date_format, static_cast<std::uint8_t>(0x04), "generated date format");
        check_eq(table.flags, 0x0au, "generated flags");
        if (table.columns.size() == 3) {
            check_eq(table.columns[1].title, std::string("Name Col"), "generated title");
            check_eq(table.columns[1].opaque_header[0], static_cast<std::uint8_t>(1),
                     "generated opaque header preserved");
        }
    }
}

void test_generated_views() {
    std::vector<std::uint8_t> main_block(64, 0);
    put_u32(main_block, 0, 28);
    put_u32(main_block, 4, 28);
    put_u32(main_block, 8, 0x00010000u);
    put_u32(main_block, 12, 2);
    put_utf16(main_block, 16, "opaque view state");

    std::vector<std::uint8_t> notes(16, 0);
    const std::string rtf = "{\\rtf1 synthetic notes}";
    notes.resize(16 + rtf.size(), 0);
    for (std::size_t i = 0; i < rtf.size(); ++i) {
        notes[16 + i] = static_cast<std::uint8_t>(rtf[i]);
    }

    const std::string timeline = "<TLViewData dfltTLView=\"x\"/>";
    std::vector<std::uint8_t> timeline_block(timeline.size() * 2, 0);
    put_utf16(timeline_block, 0, timeline);

    std::vector<BlockSpec> blocks;
    blocks.push_back({1, 0x20400006u, main_block});
    blocks.push_back({1, 0x22400014u, notes});
    blocks.push_back({2, 0x20400006u, main_block});
    blocks.push_back({2, 0x2240002fu, timeline_block});

    const Parts parts = build_parts(138, {{"&Gantt Chart", ""}, {"Time&line", ""}}, blocks);
    const auto state = parse_or_fail("CV_iew", parts);
    check_eq(state.rows.size(), std::size_t{2}, "views rows");
    check_eq(state.views.size(), std::size_t{2}, "views count");
    check_eq(state.blocks.size(), std::size_t{4}, "views block count");
    if (state.views.size() == 2) {
        check_eq(state.views[0].name, std::string("&Gantt Chart"), "view 1 name");
        check_eq(state.views[1].name, std::string("Time&line"), "view 2 name");
        check_eq(state.views[0].blocks.size(), std::size_t{2}, "view 1 block count");
        if (state.views[0].blocks.size() == 2) {
            check_eq(state.views[0].blocks[0].kind, std::string("view-state"), "view-state kind");
            check_eq(state.views[0].blocks[1].kind, std::string("view-notes"), "notes kind");
            check(state.views[0].blocks[1].preview.find("rtf") != std::string::npos,
                  "notes preview finds rtf");
        }
        if (state.views[1].blocks.size() == 2) {
            check_eq(state.views[1].blocks[1].kind, std::string("timeline-view"),
                     "timeline kind");
            check(state.views[1].blocks[1].preview.find("<TLViewData") != std::string::npos,
                  "timeline preview finds the XML element");
        }
    }
    const std::string summary = msproject::viewstate::describe_view_state(state);
    check(summary.find("view 1") != std::string::npos, "describe shows views");
}

void test_view_main_decode() {
    // View 1 is a single view: table/filter/group names, a display name, the
    // measured 970-byte options template and the notes grid.  One item is the
    // measured 37-byte odd size, which is what forces the two-byte padding.
    std::vector<std::uint8_t> odd(37, 0);
    put_u32(odd, 0, 0x00000100u);
    put_u32(odd, 8, 0x00000017u);
    std::vector<ViewItemSpec> view1_items;
    view1_items.push_back({0x22400007u, 23, odd});
    view1_items.push_back({0x2240000Au, 1, utf16_item("&Entry")});
    view1_items.push_back({0x2240000Bu, 1, utf16_item("&All Tasks")});
    view1_items.push_back({0x22400018u, 1, utf16_item("No Group")});
    view1_items.push_back({0x2240002Au, 1, utf16_item("Gantt Chart")});
    view1_items.push_back({0x22400009u, 1, make_view_options()});
    view1_items.push_back({0x22400014u, 1, make_view_notes()});
    const std::vector<std::uint8_t> main1 = make_view_main_block(view1_items);

    // View 2 is the measured combination shape: 0x2240000a/0x2240000b hold
    // view names (one of them matches view 1's caption), not a table/filter.
    std::vector<ViewItemSpec> view2_items;
    view2_items.push_back({0x2240000Au, 1, utf16_item("&Gantt Chart")});
    view2_items.push_back({0x2240000Bu, 1, utf16_item("Time&line")});
    view2_items.push_back({0x2240002Au, 1, utf16_item("Gantt Chart with Timeline")});
    const std::vector<std::uint8_t> main2 = make_view_main_block(view2_items);

    std::vector<BlockSpec> blocks;
    blocks.push_back({1, 0x20400006u, main1});
    blocks.push_back({1, 0x22400014u, make_view_notes()});
    blocks.push_back({2, 0x20400006u, main2});
    const Parts parts = build_parts(138, {{"&Gantt Chart", ""}, {"Gantt &with Timeline", ""}},
                                    blocks);
    const auto state = parse_or_fail("CV_iew", parts);
    check_eq(state.views.size(), std::size_t{2}, "main decode views");
    if (state.views.size() != 2) {
        return;
    }
    const auto& view1 = state.views[0];
    check(view1.main_decoded, "view 1 main block decoded");
    check_eq(view1.records.size(), std::size_t{7}, "view 1 item count");
    check_eq(view1.main_payload_length, static_cast<std::uint32_t>(main1.size()),
             "view 1 main payload length");
    check_eq(view1.table_or_top, std::string("&Entry"), "view 1 table name");
    check_eq(view1.filter_or_bottom, std::string("&All Tasks"), "view 1 filter name");
    check_eq(view1.group_name, std::string("No Group"), "view 1 group name");
    check_eq(view1.display_name, std::string("Gantt Chart"), "view 1 display name");
    check(!view1.looks_like_combination, "view 1 is not a combination view");
    check(view1.options_decoded, "view 1 options decoded");
    check_eq(view1.page_margins_thousandths[0], 500u, "view 1 left page margin");
    check_eq(view1.page_margins_thousandths[3], 500u, "view 1 bottom page margin");
    check_eq(view1.page_margin_extra, 2000u, "view 1 options fifth word");
    check(view1.notes_decoded, "view 1 notes decoded");
    check_eq(view1.notes_chunk_lengths[0], 11u, "view 1 notes first chunk length");
    check_eq(view1.notes_chunk_lengths[8], 14u, "view 1 notes last chunk length");
    check(view1.notes_previews[0].find("rtf") != std::string::npos,
          "view 1 notes preview finds rtf");

    const auto& view2 = state.views[1];
    check(view2.main_decoded, "view 2 main block decoded");
    check_eq(view2.records.size(), std::size_t{3}, "view 2 item count");
    check_eq(view2.table_or_top, std::string("&Gantt Chart"), "view 2 top view name");
    check_eq(view2.filter_or_bottom, std::string("Time&line"), "view 2 bottom view name");
    check(view2.looks_like_combination, "view 2 is a combination view (inference)");
    const std::string summary = msproject::viewstate::describe_view_state(state);
    check(summary.find("table-or-top=\"&Entry\"") != std::string::npos,
          "describe shows the decoded table name");

    // Ordinals are not row indices: the corpus fixtures skip ordinals (13 and 22
    // in `project_a`).  A view row whose ordinal is 3 (not 2) must
    // still find its uid-3 block.
    Parts gap = parts;
    put_u32(gap.fixed_data, 138, 3);        // second row's ordinal 2 -> 3
    put_u32(gap.var_meta, 24 + 2 * 12, 3);  // third VarMeta entry uid 2 -> 3
    const auto gap_state = parse_or_fail("CV_iew", gap);
    check_eq(gap_state.views.size(), std::size_t{2}, "gapped-ordinal views");
    if (gap_state.views.size() == 2) {
        check_eq(gap_state.views[1].ordinal, 3u, "gapped-ordinal second view ordinal");
        check(gap_state.views[1].main_decoded, "gapped-ordinal view finds its main block");
        check_eq(gap_state.views[1].table_or_top, std::string("&Gantt Chart"),
                 "gapped-ordinal view decodes its table/top name");
    }

    // Every item of the walk must land exactly on the block end; a wrong size
    // word or id is refused rather than guessed.
    std::vector<msproject::viewstate::ViewRecord> records;
    msproject::viewstate::ParseError error;
    check(msproject::viewstate::parse_view_records(main1, records, &error),
          "parse_view_records accepts the measured framing");
    check_eq(records.size(), std::size_t{7}, "parse_view_records item count");
    std::vector<std::uint8_t> bad_size = main1;
    put_u32(bad_size, 0, 1);
    check(!msproject::viewstate::parse_view_records(bad_size, records, &error),
          "parse_view_records refuses a bad size word");
    std::vector<std::uint8_t> bad_id = main1;
    put_u32(bad_id, 20, 0x12345678u);
    check(!msproject::viewstate::parse_view_records(bad_id, records, &error),
          "parse_view_records refuses a non-view id");

    // Decoding must not perturb the bytes: encode is still a no-op.
    msproject::viewstate::EncodedParts encoded;
    check(msproject::viewstate::encode_view_state(state, encoded, nullptr),
          "encode a decoded CV_iew");
    check(encoded.fixed_data == parts.fixed_data, "CV_iew main decode: FixedData unchanged");
    check(encoded.fixed_meta == parts.fixed_meta, "CV_iew main decode: FixedMeta unchanged");
    check(encoded.var_meta == parts.var_meta, "CV_iew main decode: VarMeta unchanged");
    check(encoded.var2_data == parts.var2, "CV_iew main decode: Var2Data unchanged");
}

void test_ceDL_props_document() {
    std::vector<std::uint8_t> props_stream(16 + 12 + 8, 0);
    const std::uint32_t size_word = static_cast<std::uint32_t>(props_stream.size() - 4);
    put_u32(props_stream, 0, size_word);
    put_u32(props_stream, 4, size_word);
    put_u32(props_stream, 8, 0x00010000u);
    put_u32(props_stream, 12, 1);
    put_u32(props_stream, 16, 8);
    put_u32(props_stream, 20, 0x26400022u);
    put_u32(props_stream, 24, 1);
    const std::string payload = "abcdefgh";
    for (std::size_t i = 0; i < payload.size(); ++i) {
        props_stream[28 + i] = static_cast<std::uint8_t>(payload[i]);
    }

    std::vector<BlockSpec> blocks;
    blocks.push_back({1, 0x20400006u, props_stream});
    const Parts parts = build_parts(160, {{"", ""}}, blocks, 11);
    const auto state = parse_or_fail("CEdl", parts);
    check_eq(state.embedded_props.size(), std::size_t{1}, "CEdl embedded Props count");
    if (state.embedded_props.size() == 1) {
        check_eq(state.embedded_props[0].document.records.size(), std::size_t{1},
                 "CEdl Props record count");
        check_eq(state.embedded_props[0].document.records[0].id, 0x26400022u,
                 "CEdl Props record id");
        check_eq(state.embedded_props[0].document.tail_size(), 0u, "CEdl Props tail");
    }
    check_eq(state.block_refs.size(), std::size_t{1}, "CEdl block refs");
    if (state.block_refs.size() == 1) {
        check_eq(state.block_refs[0].kind, std::string("props-document"), "CEdl block kind");
    }
}

void test_empty_class() {
    Parts parts;
    parts.fixed_meta.assign(16, 0);
    put_u32(parts.fixed_meta, 0, 0xFADFADBAu);
    put_u32(parts.fixed_meta, 4, 4);
    parts.var_meta.assign(24, 0);
    put_u32(parts.var_meta, 0, 0xFADFADBAu);
    const auto state = parse_or_fail("CMap", parts);
    check_eq(state.rows.size(), std::size_t{0}, "empty class rows");
    check_eq(state.blocks.size(), std::size_t{0}, "empty class blocks");
    check_eq(state.row_size, 0u, "empty class row size");
    check(state.warnings.empty(), "empty class has no warnings");
}

void test_errors_and_warnings() {
    {
        std::vector<std::uint8_t> bad(16, 0);
        put_u32(bad, 0, 0x12345678u);
        msproject::viewstate::FixedMetaStream meta;
        msproject::viewstate::ParseError error;
        check(!msproject::viewstate::parse_fixed_meta(bad, meta, &error),
              "bad FixedMeta magic rejected");
        check(error.message.find("magic") != std::string::npos, "bad magic message");
    }
    {
        std::vector<std::uint8_t> too_small(17, 0);
        put_u32(too_small, 0, 0xFADFADBAu);
        put_u32(too_small, 8, 1);
        msproject::viewstate::FixedMetaStream meta;
        check(!msproject::viewstate::parse_fixed_meta(too_small, meta, nullptr),
              "FixedMeta entry size 1 rejected");
    }
    {
        std::vector<std::uint8_t> truncated(24 + 11, 0);
        put_u32(truncated, 0, 0xFADFADBAu);
        put_u32(truncated, 8, 1);
        msproject::viewstate::VarMetaStream meta;
        check(!msproject::viewstate::parse_var_meta(truncated, meta, nullptr),
              "truncated VarMeta rejected");
    }
    {
        std::vector<std::uint8_t> overrun = {0xff, 0x00, 0x00, 0x00, 0x01, 0x02};
        msproject::viewstate::Var2Stream var2;
        msproject::viewstate::ParseError error;
        check(!msproject::viewstate::parse_var2(overrun, var2, &error),
              "Var2Data block overrun rejected");
        check(error.message.find("declares") != std::string::npos, "overrun message");
    }
    {
        const std::vector<std::uint8_t> base =
            read_file(VIEWSTATE_TEST_DATA "/synthetic-viewstate-ctable-base.bin");
        Parts parts = build_parts(230, {{"&Entry", "Entry"}}, {}, 10);
        attach_column_blob(parts, base);
        put_u16(parts.var2, 8, 9);
        const auto state = parse_or_fail("CTable", parts);
        check_eq(state.tables.size(), std::size_t{1}, "mismatch table listed");
        if (state.tables.size() == 1) {
            check(!state.tables[0].decoded, "mismatched column count stays opaque");
        }
        check(has_warning(state, "header implies"), "mismatch warning text");
    }
    {
        Parts parts = build_parts(230, {{"&Entry", "Entry"}},
                                  {{1, 0x20400006u, make_table_block(
                                        1, 0xff, 6, {{0x0b400017u, ""}})}});
        put_u32(parts.var_meta, 28, 2);
        const auto state = parse_or_fail("CTable", parts);
        check(has_warning(state, "not a block start"), "misaligned VarMeta offset warning");
        if (state.tables.size() == 1) {
            check(!state.tables[0].decoded, "misaligned column block stays opaque");
        }
    }
    {
        const auto payload = make_table_block(1, 0xff, 6, {{0x0b400017u, ""}});
        Parts parts = build_parts(230, {{"&Entry", "Entry"}},
                                  {{1, 0x20400006u, payload}});
        append_block(parts.var2, {0x00, 0x01});
        put_u32(parts.var_meta, 20, static_cast<std::uint32_t>(parts.var2.size()));
        const auto state = parse_or_fail("CTable", parts);
        check(has_warning(state, "not named by any VarMeta entry"),
              "unreferenced block warning");
        bool unreferenced = false;
        for (const auto& ref : state.block_refs) {
            if (ref.kind == "unreferenced") {
                unreferenced = true;
            }
        }
        check(unreferenced, "unreferenced block exposed");
    }
    {
        const Parts parts = build_parts(230, {{"&Entry", "Entry"}},
                                        {{1, 0x20400006u,
                                          make_table_block(1, 0xff, 6,
                                                           {{0x0b400017u, ""}})}});
        const auto state = parse_or_fail("CUnknown", parts);
        check_eq(state.tables.size(), std::size_t{0}, "unknown class has no tables");
        check_eq(state.block_refs.size(), std::size_t{1}, "unknown class block listed");
        if (state.block_refs.size() == 1) {
            check_eq(state.block_refs[0].kind, std::string("opaque"), "unknown class opaque");
        }
        check(!msproject::viewstate::is_known_class("CUnknown"), "unknown class name");
        check(msproject::viewstate::is_known_class("CV_iew"), "known class name");
    }
    {
        Parts parts = build_parts(230, {{"&Entry", "Entry"}}, {});
        put_u32(parts.fixed_meta, 12, 999);
        const auto state = parse_or_fail("CTable", parts);
        check(has_warning(state, "fixed_data_size"), "fixed_data_size warning");
    }
}

void test_text_helpers() {
    std::vector<std::uint8_t> bytes = {0x26, 0x00, 0x45, 0x00, 0x6e, 0x00, 0x00, 0x00};
    check_eq(msproject::viewstate::utf16_ascii(bytes, 0, 8), std::string("&En"),
             "utf16_ascii stops at NUL");
    const std::vector<std::uint8_t> rtf = {0x00, 0x00, 0x7b, 0x5c, 0x72, 0x74, 0x66};
    check_eq(msproject::viewstate::preview_bytes(rtf), std::string("{\\rtf"),
             "preview finds an ASCII run");
    const std::vector<std::uint8_t> binary = {0x01, 0x02, 0x03, 0x04};
    check_eq(msproject::viewstate::preview_bytes(binary), std::string("01020304"),
             "preview falls back to hex");
}

// ---------------------------------------------------------------------------
// Wave 26 (W26-3.1): the Gantt view's style record
//
// The two committed blobs are the `0x22400008` record and the `214/Props`
// FONT_BASES payload of tests/data/synthetic-base.mpp's first decodable Gantt
// view ("Gantt &with Timeline"; the applied-view selection itself is exercised
// by app_view_style on the same file).  They are Project 2021's own MPP14
// bytes; no client content (synthetic-base is neutral and the blobs carry no
// path or name).
//
// The offsets are MPXJ's GanttChartView14.processViewProperties layout, then
// verified by scripts/oracle_w26.py's one-change-per-save session: the font
// family save moves every record's font index, the size save changes the
// FONT_BASES entry the index names, the milestone italic save sets bit 0x02,
// the critical colour save writes RGB in the first three colour bytes, and the
// highlight fill save writes the background colour at +16.
// ---------------------------------------------------------------------------

void test_gantt_style() {
    using msproject::viewstate::FontBase;
    using msproject::viewstate::GanttStyle;
    using msproject::viewstate::ParseError;
    using msproject::viewstate::Rgb;
    using msproject::viewstate::TextStyle;
    using msproject::viewstate::TextStyleItem;

    const std::string data_dir = VIEWSTATE_TEST_DATA;
    const std::vector<std::uint8_t> style_bytes =
        read_file(data_dir + "/synthetic-gantt-style.bin");
    const std::vector<std::uint8_t> font_bytes =
        read_file(data_dir + "/synthetic-gantt-fonts.bin");
    check(!style_bytes.empty(), "gantt style: the committed style blob reads");
    check(!font_bytes.empty(), "gantt style: the committed FONT_BASES blob reads");
    if (style_bytes.empty() || font_bytes.empty()) {
        return;
    }

    std::vector<FontBase> fonts;
    ParseError error;
    check(msproject::viewstate::parse_font_bases(font_bytes, fonts, &error),
          "gantt style: FONT_BASES parses");
    check_eq(fonts.size(), std::size_t{64}, "gantt style: 64 FONT_BASES slots");
    check(fonts[3].valid, "gantt style: font base 3 is defined");
    check_eq(fonts[3].name, std::string("Calibri"), "gantt style: font base 3 family");
    check_eq(fonts[3].point_size, 11, "gantt style: font base 3 size");
    check(!fonts[2].valid, "gantt style: an empty family is an invalid slot");

    GanttStyle style;
    check(msproject::viewstate::parse_gantt_style(style_bytes, fonts, style, &error),
          "gantt style: the record parses");
    check(style.decoded, "gantt style: decoded flag");

    const TextStyle& highlighted =
        style.text[static_cast<std::size_t>(TextStyleItem::highlighted)];
    check(highlighted.valid, "gantt style: the highlighted style resolves its font");
    check_eq(highlighted.font_name, std::string("Calibri"),
             "gantt style: highlighted family");
    check_eq(highlighted.point_size, 11, "gantt style: highlighted size");
    check(!highlighted.bold && !highlighted.italic, "gantt style: highlighted is upright");
    check(highlighted.has_colour, "gantt style: highlighted foreground is an explicit black");
    check(highlighted.colour == (Rgb{0, 0, 0}),
          "gantt style: highlighted foreground is black");
    check(highlighted.has_background, "gantt style: highlighted background is set");
    check(highlighted.background == (Rgb{255, 255, 102}),
          "gantt style: highlighted background is the stored pale yellow");
    check_eq(highlighted.background_pattern, std::uint16_t{1},
             "gantt style: highlighted background is solid");

    const TextStyle& summary =
        style.text[static_cast<std::size_t>(TextStyleItem::summary)];
    check(summary.bold, "gantt style: the summary style is bold");
    check(!summary.has_colour, "gantt style: the stock summary colour is automatic");

    const TextStyle& milestone =
        style.text[static_cast<std::size_t>(TextStyleItem::milestone)];
    check(!milestone.italic, "gantt style: the stock milestone style is upright");
    check(!milestone.has_colour, "gantt style: the stock milestone colour is automatic");

    // Wave 26 (W26-3.2): the active view's bar styles.  The stock synthetic
    // view has all 41 Project bar styles; the values below are Project 2021's
    // own (and the summary/milestone middle/start/end colours are the fields
    // the controlled GanttBarStyleEdit saves changed).
    check_eq(style.bars.size(), std::size_t{41}, "gantt style: 41 bar styles");
    if (!style.bars.empty()) {
        const msproject::viewstate::BarStyle& task = style.bars[0];
        check_eq(task.name, std::string("Task"), "gantt style: bar 0 is Task");
        check_eq(task.middle_shape, std::uint8_t{1}, "gantt style: Task middle shape");
        check_eq(task.middle_pattern, std::uint8_t{1}, "gantt style: Task pattern solid");
        check(task.has_middle_colour && task.middle_colour == (Rgb{138, 187, 237}),
              "gantt style: Task auto middle colour is (138,187,237)");
        check((task.show_for & 0x1u) != 0, "gantt style: Task shows for Normal tasks");
    }
    for (const msproject::viewstate::BarStyle& bar : style.bars) {
        if (bar.name == "Summary") {
            check(bar.has_middle_colour && bar.middle_colour == (Rgb{84, 84, 84}),
                  "gantt style: the Summary bar middle colour is (84,84,84)");
            check_eq(bar.middle_shape, std::uint8_t{5},
                     "gantt style: the Summary bar is a top line");
            check(bar.has_start_colour && bar.start_colour == (Rgb{84, 84, 84}),
                  "gantt style: the Summary bar start colour");
        }
        if (bar.name == "Milestone") {
            check_eq(bar.start_shape, std::uint8_t{3},
                     "gantt style: the Milestone start shape is a diamond");
            check(bar.has_start_colour && bar.start_colour == (Rgb{99, 99, 99}),
                  "gantt style: the stock Milestone start colour is grey");
        }
        if (bar.name == "Progress") {
            check(bar.has_middle_colour && bar.middle_colour == (Rgb{59, 135, 212}),
                  "gantt style: the stock Progress colour");
        }
    }

    // Wave 26 (W26-3.3): the timescale tiers.  The committed record is the
    // synthetic fixture's first decodable Gantt view ("Gantt &with Timeline"),
    // which stores Project's stock two-tier week/day scale with no top tier.
    using msproject::viewstate::TimescaleUnit;
    check(style.top.unit == TimescaleUnit::none,
          "gantt style: the stock top tier is absent (stored 255)");
    check(style.middle.unit == TimescaleUnit::weeks,
          "gantt style: the stock middle tier is weeks");
    check_eq(style.middle.count, 1, "gantt style: the stock middle tier count");
    check_eq(style.middle.format, 13, "gantt style: the stock week format");
    check_eq(style.middle.align, 0, "gantt style: the stock week labels are left aligned");
    check(style.bottom.unit == TimescaleUnit::days,
          "gantt style: the stock bottom tier is days");
    check_eq(style.bottom.count, 1, "gantt style: the stock bottom tier count");
    check_eq(style.bottom.format, 20,
             "gantt style: the stock day format is single letters");
    check_eq(style.bottom.align, 1, "gantt style: the stock day labels are centred");
    check_eq(style.view_flags, 0x1f, "gantt style: the stock view flags");
    check(style.middle.tick_lines && style.bottom.tick_lines,
          "gantt style: both stock tiers draw tick lines");
    check_eq(style.timescale_size, 100, "gantt style: the stock timescale size");

    // A record shorter than the measured minimum is refused, not read past.
    const std::vector<std::uint8_t> short_record(style_bytes.begin(),
                                                 style_bytes.begin() + 41361);
    GanttStyle refused;
    check(!msproject::viewstate::parse_gantt_style(short_record, fonts, refused, &error),
          "gantt style: a 41361-byte record is refused");
    check(!error.message.empty(), "gantt style: the refusal names the size");

    // A FONT_BASES count that cannot fit the payload is refused.
    std::vector<std::uint8_t> bad_fonts = font_bytes;
    put_u16(bad_fonts, 0, 0xffff);
    std::vector<FontBase> bad_out;
    check(!msproject::viewstate::parse_font_bases(bad_fonts, bad_out, &error),
          "gantt style: an oversized FONT_BASES count is refused");

    // A font index past the table keeps the record decodable but marks that
    // style invalid and warns -- presentation damage must not lose the view.
    std::vector<std::uint8_t> bad_index = style_bytes;
    bad_index[26] = 200;
    GanttStyle partial;
    check(msproject::viewstate::parse_gantt_style(bad_index, fonts, partial, &error),
          "gantt style: a bad font index still parses the record");
    check(!partial.text[static_cast<std::size_t>(TextStyleItem::highlighted)].valid,
          "gantt style: a bad font index marks the style invalid");
    check(!partial.warnings.empty(), "gantt style: a bad font index warns");
}

// ---------------------------------------------------------------------------
// Writer tests (wave 14, W58)
//
// The base/col/date blobs are the wave-10 prefs fixtures Project itself wrote.
// The strongest proofs here are:
//   * parse -> encode is byte-identical for a collection that was not mutated;
//   * setting the date format on the *col* fixture reproduces the *date*
//     fixture blob byte-for-byte (the oracle's single-variable save);
//   * a column edit changes only the targeted 115-byte record.
// ---------------------------------------------------------------------------

using msproject::viewstate::EncodedParts;
using msproject::viewstate::ViewState;
using msproject::viewstate::WriteError;

std::vector<std::size_t> diff_positions(const std::vector<std::uint8_t>& before,
                                        const std::vector<std::uint8_t>& after) {
    std::vector<std::size_t> out;
    const std::size_t common = before.size() < after.size() ? before.size() : after.size();
    for (std::size_t i = 0; i < common; ++i) {
        if (before[i] != after[i]) {
            out.push_back(i);
        }
    }
    if (before.size() != after.size()) {
        out.push_back(common);  // the sizes differ; this is not a byte offset
    }
    return out;
}

void check_same_parts(const EncodedParts& actual,
                      const Parts& expected,
                      const std::string& what) {
    check(actual.fixed_data == expected.fixed_data, what + ": FixedData byte-identical");
    check(actual.fixed_meta == expected.fixed_meta, what + ": FixedMeta byte-identical");
    check(actual.var_meta == expected.var_meta, what + ": VarMeta byte-identical");
    check(actual.var2_data == expected.var2, what + ": Var2Data byte-identical");
}

// Builds the wave-10 CTable collection around one raw Var2Data blob.
bool make_ctable_parts(const std::string& blob_name, Parts& parts) {
    const std::vector<std::uint8_t> blob = read_file(VIEWSTATE_TEST_DATA "/" + blob_name);
    check(!blob.empty(), "read " + blob_name);
    if (blob.empty()) {
        return false;
    }
    parts = build_parts(230, {{"&Entry", "Entry"}}, {}, 10);
    attach_column_blob(parts, blob);
    return true;
}

void test_writer_roundtrip() {
    Parts parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-base.bin", parts)) {
        return;
    }
    const auto state = parse_or_fail("CTable", parts);
    check(state.parsed, "parsed flag set by parse_view_state");

    EncodedParts encoded;
    WriteError error;
    check(encode_view_state(state, encoded, &error), "encode_view_state on an unmutated state");
    if (!error.message.empty()) {
        std::fprintf(stderr, "encode error: %s\n", error.message.c_str());
    }
    check_same_parts(encoded, parts, "no-op round-trip");

    // The encoded parts must parse back to the same table definition.
    const msproject::viewstate::PartSet encoded_parts = encoded.parts();
    ViewState again;
    msproject::viewstate::ParseError parse_error;
    check(msproject::viewstate::parse_view_state("CTable", encoded_parts, again, &parse_error),
          "re-parse encoded CTable");
    check(again.parsed, "re-parsed state has the parsed flag");
    check_eq(again.tables.size(), std::size_t{1}, "re-parsed tables");
    if (again.tables.size() == 1) {
        check_eq(again.tables[0].column_count(), 9u, "re-parsed column count");
        check_eq(again.tables[0].date_format, static_cast<std::uint8_t>(0xff),
                 "re-parsed date format");
        check_eq(again.tables[0].flags, 6u, "re-parsed flags");
        for (std::size_t i = 0; i < again.tables[0].columns.size() && i < kEntryFieldIds.size();
             ++i) {
            check_eq(again.tables[0].columns[i].field_id, kEntryFieldIds[i],
                     "re-parsed column " + std::to_string(i) + " field id");
        }
    }

    // An empty collection exercises the 0-entry FixedMeta / 0-block VarMeta
    // path (CDrawing/CMap/CReport are exactly this in the fixtures).
    Parts empty;
    empty.fixed_meta.assign(16, 0);
    put_u32(empty.fixed_meta, 0, 0xFADFADBAu);
    put_u32(empty.fixed_meta, 4, 4);
    empty.var_meta.assign(24, 0);
    put_u32(empty.var_meta, 0, 0xFADFADBAu);
    const auto empty_state = parse_or_fail("CMap", empty);
    EncodedParts empty_encoded;
    check(encode_view_state(empty_state, empty_encoded, nullptr), "encode empty CMap");
    check_same_parts(empty_encoded, empty, "empty collection round-trip");

    // A state that did not come from parse_view_state is refused.
    ViewState unparsed;
    EncodedParts refused;
    WriteError refuse_error;
    check(!encode_view_state(unparsed, refused, &refuse_error), "encode refuses an unparsed state");
    check(!refuse_error.message.empty(), "unparsed encode has a message");
    check(refused.fixed_data.empty() && refused.var2_data.empty(),
          "refused encode leaves the output empty");
}

void test_writer_date_format() {
    // base (flags 6) -> set 0x05: exactly one byte changes, at payload +7.
    Parts base_parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-base.bin", base_parts)) {
        return;
    }
    auto base_state = parse_or_fail("CTable", base_parts);
    EncodedParts before;
    check(encode_view_state(base_state, before, nullptr), "encode base before date mutation");
    WriteError error;
    check(msproject::viewstate::set_table_date_format(base_state, 0, 0x05, &error),
          "set_table_date_format(0, 0x05)");
    if (!error.message.empty()) {
        std::fprintf(stderr, "date-format error: %s\n", error.message.c_str());
    }
    EncodedParts after;
    check(encode_view_state(base_state, after, nullptr), "encode base after date mutation");

    check(after.fixed_data == before.fixed_data, "date format: FixedData unchanged");
    check(after.fixed_meta == before.fixed_meta, "date format: FixedMeta unchanged");
    check(after.var_meta == before.var_meta, "date format: VarMeta unchanged");
    const auto diffs = diff_positions(before.var2_data, after.var2_data);
    check_eq(diffs.size(), std::size_t{1}, "date format changes exactly one Var2Data byte");
    if (diffs.size() == 1) {
        check_eq(diffs[0], std::size_t{11}, "date format byte is block payload +7 (stream 11)");
        check_eq(static_cast<int>(after.var2_data[11]), 0x05, "written date format is 0x05");
    }
    const auto reparsed = parse_or_fail("CTable", [&after] {
        Parts parts;
        parts.fixed_data = after.fixed_data;
        parts.fixed_meta = after.fixed_meta;
        parts.var_meta = after.var_meta;
        parts.var2 = after.var2_data;
        return parts;
    }());
    check_eq(reparsed.tables.size(), std::size_t{1}, "date mutation re-parsed tables");
    if (reparsed.tables.size() == 1) {
        check_eq(reparsed.tables[0].date_format, static_cast<std::uint8_t>(0x05),
                 "re-parsed date format is 0x05");
        check_eq(reparsed.tables[0].column_count(), 9u, "date mutation keeps 9 columns");
    }

    // The oracle proof: col (0xff) -> date (0x05) was Project's only semantic
    // change, so setting 0x05 on the committed col blob must equal the
    // committed date blob byte-for-byte.
    Parts col_parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-col.bin", col_parts)) {
        return;
    }
    auto col_state = parse_or_fail("CTable", col_parts);
    check(msproject::viewstate::set_table_date_format(col_state, 0, 0x05, nullptr),
          "set date format on col fixture");
    EncodedParts col_edited;
    check(encode_view_state(col_state, col_edited, nullptr), "encode col fixture after edit");
    const std::vector<std::uint8_t> date_blob =
        read_file(VIEWSTATE_TEST_DATA "/synthetic-viewstate-ctable-date.bin");
    check(!date_blob.empty(), "read date blob");
    check(col_edited.var2_data == date_blob,
          "col + set_table_date_format(0x05) reproduces the date fixture blob");
}

void test_writer_column() {
    Parts parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-base.bin", parts)) {
        return;
    }
    auto state = parse_or_fail("CTable", parts);
    check_eq(state.tables.size(), std::size_t{1}, "column edit tables");
    if (state.tables.size() != 1 || state.tables[0].columns.size() < 2) {
        return;
    }
    const std::array<std::uint8_t, 9> original_header = state.tables[0].columns[1].opaque_header;
    check_eq(state.tables[0].columns[1].field_id, 0x0b4000cdu,
             "record 1 starts with the stock Entry field id");
    check_eq(state.tables[0].columns[1].title, std::string(""), "record 1 starts untitled");

    EncodedParts before;
    check(encode_view_state(state, before, nullptr), "encode before column edit");

    // The oracle replaced index 1 with Number1 (field 0x0b400057, title
    // "Number1"); Text1 is 0x0b400033.
    WriteError error;
    check(msproject::viewstate::set_table_column(state, 0, 1, 0x0b400057u, "Number1", &error),
          "set_table_column(0, 1, Number1)");
    if (!error.message.empty()) {
        std::fprintf(stderr, "column error: %s\n", error.message.c_str());
    }
    EncodedParts after;
    check(encode_view_state(state, after, nullptr), "encode after column edit");

    check(after.fixed_data == before.fixed_data, "column edit: FixedData unchanged");
    check(after.fixed_meta == before.fixed_meta, "column edit: FixedMeta unchanged");
    check(after.var_meta == before.var_meta, "column edit: VarMeta unchanged");
    check_eq(after.var2_data.size(), before.var2_data.size(), "column edit keeps block size");
    // Absolute position in Var2Data: 4-byte block length + 12-byte payload
    // header + one 115-byte record.
    const std::size_t record = 4 + 12 + 1 * 115;
    const auto diffs = diff_positions(before.var2_data, after.var2_data);
    check(!diffs.empty(), "column edit changes bytes");
    bool confined = true;
    for (const std::size_t at : diffs) {
        if (at < record || at >= record + 115) {
            confined = false;
        }
    }
    check(confined, "column edit changes only the 115-byte record at index 1");

    // Field id at +0, 9 opaque bytes preserved, zero-filled UTF-16 title.
    if (after.var2_data.size() > record + 115) {
        std::uint32_t field = 0;
        for (std::size_t b = 0; b < 4; ++b) {
            field |= static_cast<std::uint32_t>(after.var2_data[record + b]) << (8u * b);
        }
        check_eq(field, 0x0b400057u, "column record field id written");
        bool header_kept = true;
        for (std::size_t b = 0; b < 9; ++b) {
            if (after.var2_data[record + 4 + b] != original_header[b]) {
                header_kept = false;
            }
        }
        check(header_kept, "9 opaque column bytes preserved");
        check_eq(msproject::viewstate::utf16_ascii(after.var2_data, record + 13, 51),
                 std::string("Number1"), "column record title written");
        bool tail_zero = true;
        for (std::size_t at = record + 13 + 7 * 2; at < record + 115; ++at) {
            if (after.var2_data[at] != 0) {
                tail_zero = false;
            }
        }
        check(tail_zero, "title tail is zero-filled after the NUL");
    }

    Parts edited_parts;
    edited_parts.fixed_data = after.fixed_data;
    edited_parts.fixed_meta = after.fixed_meta;
    edited_parts.var_meta = after.var_meta;
    edited_parts.var2 = after.var2_data;
    const auto reparsed = parse_or_fail("CTable", edited_parts);
    check_eq(reparsed.tables.size(), std::size_t{1}, "column edit re-parsed tables");
    if (reparsed.tables.size() == 1 && reparsed.tables[0].columns.size() == 9) {
        check_eq(reparsed.tables[0].columns[1].field_id, 0x0b400057u,
                 "re-parsed column field id");
        check_eq(reparsed.tables[0].columns[1].title, std::string("Number1"),
                 "re-parsed column title");
        check(reparsed.tables[0].columns[1].opaque_header == original_header,
              "re-parsed opaque header preserved");
    }

    // The full 51-code-unit capacity is accepted (no NUL fits; the decoder
    // reads 51 units), and 52 is refused.
    const std::string full_title(51, 'A');
    check(msproject::viewstate::set_table_column(state, 0, 1, 0x0b400033u, full_title, nullptr),
          "51-code-unit title accepted");
    EncodedParts full;
    check(encode_view_state(state, full, nullptr), "encode 51-unit title");
    check_eq(msproject::viewstate::utf16_ascii(full.var2_data, record + 13, 51), full_title,
             "51-code-unit title reads back");
}

void test_writer_refusals() {
    Parts parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-base.bin", parts)) {
        return;
    }
    auto state = parse_or_fail("CTable", parts);
    EncodedParts before;
    check(encode_view_state(state, before, nullptr), "encode before refused edits");

    WriteError error;
    check(!msproject::viewstate::set_table_date_format(state, 1, 0x05, &error),
          "date format: table index 1 refused");
    check(error.message.find("out of range") != std::string::npos,
          "date format refusal message names the range");
    error = WriteError{};
    check(!msproject::viewstate::set_table_column(state, 0, 9, 0x0b400057u, "x", &error),
          "column: index 9 refused");
    check(error.message.find("out of range") != std::string::npos,
          "column index refusal message names the range");
    error = WriteError{};
    check(!msproject::viewstate::set_table_column(state, 0, 1, 0u, "x", &error),
          "column: field id 0 refused");
    check(error.message.find("field id 0") != std::string::npos, "field id refusal message");
    error = WriteError{};
    check(!msproject::viewstate::set_table_column(state, 0, 1, 0x0b400057u,
                                                  std::string(52, 'A'), &error),
          "column: 52-code-unit title refused");
    check(error.message.find("capacity") != std::string::npos, "title length refusal message");
    error = WriteError{};
    check(!msproject::viewstate::set_table_column(state, 0, 1, 0x0b400057u, "caf\xe9", &error),
          "column: non-ASCII title refused");
    error = WriteError{};

    // No refused call may have changed anything.
    EncodedParts after;
    check(encode_view_state(state, after, nullptr), "encode after refused edits");
    check_same_parts(after, parts, "refused edits leave every part unchanged");

    // A collection whose table block did not decode is refused too.
    Parts undecoded = parts;
    put_u16(undecoded.var2, 8, 9);
    auto opaque_state = parse_or_fail("CTable", undecoded);
    error = WriteError{};
    check(!msproject::viewstate::set_table_date_format(opaque_state, 0, 0x05, &error),
          "date format: undecoded table refused");    check(error.message.find("decoded") != std::string::npos, "undecoded refusal message");
}

void test_writer_append() {
    Parts parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-base.bin", parts)) {
        return;
    }
    auto state = parse_or_fail("CTable", parts);
    EncodedParts before;
    check(encode_view_state(state, before, nullptr), "encode base before append");
    check_eq(state.tables.size(), std::size_t{1}, "append tables");
    if (state.tables.size() != 1) {
        return;
    }

    // Append one column: 10 columns, last index 9, block +115, and every byte of
    // the old payload still identical (the record is inserted after the last
    // column, which is the end of the only block).
    WriteError error;
    check(msproject::viewstate::append_table_column(state, 0, 0x0b400057u, "Number1", &error),
          "append_table_column(0, Number1)");
    if (!error.message.empty()) {
        std::fprintf(stderr, "append error: %s\n", error.message.c_str());
    }
    EncodedParts after;
    check(encode_view_state(state, after, nullptr), "encode after append");
    check(after.fixed_data == before.fixed_data, "append: FixedData unchanged");
    check(after.fixed_meta == before.fixed_meta, "append: FixedMeta unchanged");
    check_eq(after.var2_data.size(), before.var2_data.size() + 115, "append: block +115 bytes");
    check_eq(after.var_meta.size(), before.var_meta.size(), "append: VarMeta size unchanged");
    if (after.var2_data.size() == before.var2_data.size() + 115) {
        // The new record is appended after the last column, so in the old bytes
        // only the u32 frame length word (0..3) and the u16 last-column index
        // (payload +4 = stream 8..9) may differ.
        check(std::equal(before.var2_data.begin() + 4, before.var2_data.begin() + 8,
                         after.var2_data.begin() + 4),
              "append: payload ordinal unchanged");
        check(std::equal(before.var2_data.begin() + 10, before.var2_data.end(),
                         after.var2_data.begin() + 10),
              "append: old payload bytes after the index are unchanged");
        bool only_frame = true;
        for (std::size_t i = 0; i < before.var2_data.size(); ++i) {
            if (before.var2_data[i] != after.var2_data[i] && i >= 4 && i != 8 && i != 9) {
                only_frame = false;
            }
        }
        check(only_frame, "append: only the length word and last-column index differ");
        check_eq(static_cast<int>(after.var2_data[8]), 9, "append: written last-column index low");
        check_eq(static_cast<int>(after.var2_data[9]), 0, "append: written last-column index high");
        check_eq(after.var2_data.size() - 4,
                 static_cast<std::size_t>(1047 + 115), "append: new block length");
        check_eq(read_u32(after.var2_data, 0), 1047u + 115u,
                 "append: written block length word");
        const std::size_t record = after.var2_data.size() - 115;
        std::uint32_t field = 0;
        for (std::size_t b = 0; b < 4; ++b) {
            field |= static_cast<std::uint32_t>(after.var2_data[record + b]) << (8u * b);
        }
        check_eq(field, 0x0b400057u, "append: field id at record +0");
        bool header_ok = true;
        for (std::size_t b = 0; b < 9; ++b) {
            if (after.var2_data[record + 4 + b] != msproject::viewstate::kFreshColumnHeader[b]) {
                header_ok = false;
            }
        }
        check(header_ok, "append: measured fresh-column nine bytes at +4");
        check_eq(msproject::viewstate::utf16_ascii(after.var2_data, record + 13, 51),
                 std::string("Number1"), "append: title at +13");
        bool tail_zero = true;
        for (std::size_t at = record + 13 + 7 * 2; at < record + 115; ++at) {
            if (after.var2_data[at] != 0) {
                tail_zero = false;
            }
        }
        check(tail_zero, "append: title tail zero-filled");
    }
    // VarMeta changed only in its var2_size word (+20).
    check_eq(read_u32(after.var_meta, 20),
             static_cast<std::uint32_t>(after.var2_data.size()),
             "append: VarMeta var2_size recomputed");
    bool only_var2_size = true;
    for (std::size_t i = 0; i < before.var_meta.size(); ++i) {
        if (i >= 20 && i < 24) {
            continue;
        }
        if (before.var_meta[i] != after.var_meta[i]) {
            only_var2_size = false;
        }
    }
    check(only_var2_size, "append: VarMeta differs only at var2_size");

    // Decode back: 10 columns and last_column_index 9.
    Parts appended;
    appended.fixed_data = after.fixed_data;
    appended.fixed_meta = after.fixed_meta;
    appended.var_meta = after.var_meta;
    appended.var2 = after.var2_data;
    const auto reparsed = parse_or_fail("CTable", appended);
    check_eq(reparsed.tables.size(), std::size_t{1}, "append re-parsed tables");
    if (reparsed.tables.size() == 1) {
        check_eq(reparsed.tables[0].columns.size(), std::size_t{10},
                 "append re-parsed column count");
        check_eq(reparsed.tables[0].last_column_index, static_cast<std::uint16_t>(9),
                 "append re-parsed last column index");
        if (reparsed.tables[0].columns.size() == 10) {
            check_eq(reparsed.tables[0].columns[9].field_id, 0x0b400057u,
                     "append re-parsed field id");
            check_eq(reparsed.tables[0].columns[9].title, std::string("Number1"),
                     "append re-parsed title");
            check(reparsed.tables[0].columns[9].opaque_header ==
                      msproject::viewstate::kFreshColumnHeader,
                  "append re-parsed nine bytes");
        }
    }

    // Twice: 11 columns, last index 10, block +230.
    check(msproject::viewstate::append_table_column(state, 0, 0x0b400033u, "Text1", nullptr),
          "append the second column");
    EncodedParts twice;
    check(encode_view_state(state, twice, nullptr), "encode after two appends");
    check_eq(twice.var2_data.size(), before.var2_data.size() + 230,
             "two appends: block +230 bytes");
    Parts appended2;
    appended2.fixed_data = twice.fixed_data;
    appended2.fixed_meta = twice.fixed_meta;
    appended2.var_meta = twice.var_meta;
    appended2.var2 = twice.var2_data;
    const auto reparsed2 = parse_or_fail("CTable", appended2);
    if (reparsed2.tables.size() == 1) {
        check_eq(reparsed2.tables[0].columns.size(), std::size_t{11},
                 "two appends re-parsed column count");
        check_eq(reparsed2.tables[0].last_column_index, static_cast<std::uint16_t>(10),
                 "two appends re-parsed last index");
        if (reparsed2.tables[0].columns.size() == 11) {
            check_eq(reparsed2.tables[0].columns[10].title, std::string("Text1"),
                     "two appends re-parsed second title");
        }
    }

    // Two tables: appending to the first must shift the second block's stream
    // offset and its VarMeta entry, and a second append to the (now shifted)
    // second table must still find it.
    const auto payload_one =
        make_table_block(1, 0xff, 6, {{0x0b400017u, ""}, {0x0b400500u, ""}});
    const auto payload_two = make_table_block(
        2, 0xff, 6, {{0x0b400017u, ""}, {0x0b4000cdu, ""}, {0x0b40000eu, "Task Name"}});
    std::vector<BlockSpec> two_blocks;
    two_blocks.push_back({1, 0x20400006u, payload_one});
    two_blocks.push_back({2, 0x20400006u, payload_two});
    Parts two = build_parts(230, {{"&One", "One"}, {"&Two", "Two"}}, two_blocks);
    auto two_state = parse_or_fail("CTable", two);
    EncodedParts two_before;
    check(encode_view_state(two_state, two_before, nullptr), "encode two-table CTable");
    check_eq(two_state.tables.size(), std::size_t{2}, "two-table tables");
    if (two_state.tables.size() == 2) {
        check_eq(two_state.tables[1].block_offset, 246u, "two-table second block offset");
        check(msproject::viewstate::append_table_column(two_state, 0, 0x0b400057u, "Number1",
                                                        nullptr),
              "append to the first of two tables");
        check_eq(two_state.tables[1].block_offset, 246u + 115u,
                 "append shifts the second table's decoded block offset");
        EncodedParts two_after;
        check(encode_view_state(two_state, two_after, nullptr), "encode two-table after append");
        check_eq(two_after.var2_data.size(), two_before.var2_data.size() + 115,
                 "two-table append: Var2Data +115");
        check_eq(read_u32(two_after.var2_data, 361), 357u,
                 "two-table append: second block length word still 357");
        // Appending to the shifted second table exercises the repaired offset.
        check(msproject::viewstate::append_table_column(two_state, 1, 0x0b400033u, "Text1",
                                                        nullptr),
              "append to the shifted second table");
        EncodedParts both;
        check(encode_view_state(two_state, both, nullptr), "encode after both appends");
        Parts both_parts;
        both_parts.fixed_data = both.fixed_data;
        both_parts.fixed_meta = both.fixed_meta;
        both_parts.var_meta = both.var_meta;
        both_parts.var2 = both.var2_data;
        const auto both_state = parse_or_fail("CTable", both_parts);
        check_eq(both_state.tables.size(), std::size_t{2}, "two-table re-parsed tables");
        if (both_state.tables.size() == 2) {
            check_eq(both_state.tables[0].columns.size(), std::size_t{3},
                     "two-table first table 3 columns");
            check_eq(both_state.tables[1].columns.size(), std::size_t{4},
                     "two-table second table 4 columns");
            check_eq(both_state.tables[0].columns[2].title, std::string("Number1"),
                     "two-table first table appended title");
            check_eq(both_state.tables[1].columns[3].title, std::string("Text1"),
                     "two-table second table appended title");
        }
    }
}

void test_writer_append_refusals() {
    Parts parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-base.bin", parts)) {
        return;
    }
    auto state = parse_or_fail("CTable", parts);
    EncodedParts before;
    check(encode_view_state(state, before, nullptr), "encode before refused appends");

    WriteError error;
    check(!msproject::viewstate::append_table_column(state, 0, 0x0b400057u,
                                                     std::string(52, 'A'), &error),
          "append: 52-code-unit title refused");
    check(error.message.find("capacity") != std::string::npos, "append title refusal message");
    error = WriteError{};
    check(!msproject::viewstate::append_table_column(state, 0, 0u, "x", &error),
          "append: field id 0 refused");
    check(error.message.find("field id 0") != std::string::npos, "append field id message");
    error = WriteError{};
    check(!msproject::viewstate::append_table_column(state, 3, 0x0b400057u, "x", &error),
          "append: bad table index refused");
    check(error.message.find("out of range") != std::string::npos,
          "append bad table refusal message");
    error = WriteError{};
    check(!msproject::viewstate::append_table_column(state, 0, 0x0b400057u, "caf\xe9", &error),
          "append: non-ASCII title refused");

    // Unparsed state and undecoded table are refused too.
    msproject::viewstate::ViewState unparsed;
    error = WriteError{};
    check(!msproject::viewstate::append_table_column(unparsed, 0, 0x0b400057u, "x", &error),
          "append: unparsed state refused");
    check(error.message.find("did not parse") != std::string::npos,
          "append unparsed refusal message");
    Parts undecoded = parts;
    put_u16(undecoded.var2, 8, 9);
    auto opaque_state = parse_or_fail("CTable", undecoded);
    error = WriteError{};
    check(!msproject::viewstate::append_table_column(opaque_state, 0, 0x0b400057u, "x", &error),
          "append: undecoded table refused");
    check(error.message.find("decoded") != std::string::npos,
          "append undecoded refusal message");

    EncodedParts after;
    check(encode_view_state(state, after, nullptr), "encode after refused appends");
    check_same_parts(after, parts, "refused appends leave every part unchanged");

    // A full table (0x10000 columns, last_column_index 0xffff) cannot grow: the
    // u16 index has no next value.
    std::vector<std::uint8_t> payload(12 + 65536 * 115, 0);
    put_u32(payload, 0, 1);
    put_u16(payload, 4, 0xffff);
    payload[6] = 1;
    payload[7] = 0xff;
    put_u32(payload, 8, 6);
    std::vector<std::uint8_t> blob;
    append_block(blob, payload);
    Parts big = build_parts(230, {{"&Big", "Big"}}, {}, 10);
    attach_column_blob(big, blob);
    auto big_state = parse_or_fail("CTable", big);
    check_eq(big_state.tables.size(), std::size_t{1}, "over-capacity table parsed");
    if (big_state.tables.size() == 1) {
        check_eq(big_state.tables[0].columns.size(), std::size_t{65536},
                 "over-capacity table column count");
        check(big_state.tables[0].decoded, "over-capacity table decoded");
    }
    error = WriteError{};
    EncodedParts big_before;
    check(encode_view_state(big_state, big_before, nullptr), "encode before over-capacity append");
    check(!msproject::viewstate::append_table_column(big_state, 0, 0x0b400057u, "x", &error),
          "append: over-capacity table refused");
    check(error.message.find("cannot grow") != std::string::npos,
          "over-capacity refusal message");
    EncodedParts big_after;
    check(encode_view_state(big_state, big_after, nullptr), "encode after over-capacity append");
    check(big_after.fixed_data == big_before.fixed_data &&
              big_after.fixed_meta == big_before.fixed_meta &&
              big_after.var_meta == big_before.var_meta &&
              big_after.var2_data == big_before.var2_data,
          "over-capacity refusal leaves every part unchanged");
}

void test_writer_apply_edits() {
    Parts parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-base.bin", parts)) {
        return;
    }
    const auto state = parse_or_fail("CTable", parts);
    EncodedParts noop;
    check(encode_view_state(state, noop, nullptr), "encode before apply_table_edits");

    // An empty edit list is the no-op encode.
    EncodedParts empty_out;
    WriteError error;
    check(msproject::viewstate::apply_table_edits(
              state, std::span<const msproject::viewstate::TableEdit>(), empty_out, &error),
          "apply_table_edits with no edits");
    check_same_parts(empty_out, parts, "no edits is a no-op encode");

    // One edit agrees with calling the mutator directly.
    const std::vector<msproject::viewstate::TableEdit> date_edits = {
        msproject::viewstate::table_date_format_edit(0, 0x05)};
    EncodedParts date_out;
    check(msproject::viewstate::apply_table_edits(state, date_edits, date_out, &error),
          "apply_table_edits date format");
    auto direct = state;
    check(msproject::viewstate::set_table_date_format(direct, 0, 0x05, nullptr),
          "direct date format");
    EncodedParts direct_out;
    check(encode_view_state(direct, direct_out, nullptr), "encode direct date format");
    check(date_out.var2_data == direct_out.var2_data, "apply_table_edits matches the mutator");

    // A mixed list: append, replace and date format, in order.
    const std::vector<msproject::viewstate::TableEdit> edits = {
        msproject::viewstate::table_column_append_edit(0, 0x0b400057u, "Number1"),
        msproject::viewstate::table_column_replace_edit(0, 1, 0x0b400033u, "Text1"),
        msproject::viewstate::table_date_format_edit(0, 0x05)};
    EncodedParts mixed;
    check(msproject::viewstate::apply_table_edits(state, edits, mixed, &error),
          "apply_table_edits mixed list");
    Parts mixed_parts;
    mixed_parts.fixed_data = mixed.fixed_data;
    mixed_parts.fixed_meta = mixed.fixed_meta;
    mixed_parts.var_meta = mixed.var_meta;
    mixed_parts.var2 = mixed.var2_data;
    const auto mixed_state = parse_or_fail("CTable", mixed_parts);
    check_eq(mixed_state.tables.size(), std::size_t{1}, "mixed edits re-parsed tables");
    if (mixed_state.tables.size() == 1) {
        check_eq(mixed_state.tables[0].columns.size(), std::size_t{10},
                 "mixed edits column count");
        check_eq(mixed_state.tables[0].last_column_index, static_cast<std::uint16_t>(9),
                 "mixed edits last index");
        if (mixed_state.tables[0].columns.size() == 10) {
            check_eq(mixed_state.tables[0].columns[1].field_id, 0x0b400033u,
                     "mixed edits replaced field");
            check_eq(mixed_state.tables[0].columns[1].title, std::string("Text1"),
                     "mixed edits replaced title");
        }
        check_eq(mixed_state.tables[0].date_format, static_cast<std::uint8_t>(0x05),
                 "mixed edits date format");
    }

    // A failing edit after a valid one: the output is empty and the input state
    // is untouched.
    const std::vector<msproject::viewstate::TableEdit> failing = {
        msproject::viewstate::table_column_append_edit(0, 0x0b400057u, "Number1"),
        msproject::viewstate::table_date_format_edit(7, 0x05)};
    EncodedParts refused;
    error = WriteError{};
    check(!msproject::viewstate::apply_table_edits(state, failing, refused, &error),
          "apply_table_edits refuses a bad table index");
    check(refused.fixed_data.empty() && refused.var2_data.empty(),
          "failed apply_table_edits leaves the output empty");
    check(error.message.find("table edit 1") != std::string::npos,
          "failed apply_table_edits names the edit index");
    EncodedParts still_noop;
    check(encode_view_state(state, still_noop, nullptr), "encode after failed apply_table_edits");
    check_same_parts(still_noop, parts, "input state is untouched by apply_table_edits");
}

// ---------------------------------------------------------------------------
// Wave 17 (F1) tests: row record sets, CFilter/CGrouping framing, CEdl typed
// records + regeneration, CV_iew editing and the regenerate_view_state seam.
// ---------------------------------------------------------------------------

void test_row_record_sets() {
    // Task-shaped set (criteria + name): 8 + 2*8 entries then a 2-byte criteria
    // payload and the name at relative offset 26 = row + 136.
    Parts entry = build_parts(230, {{"&Entry", "Entry"}}, {}, 10);
    attach_row_records(entry, 0, 230,
                       {{0x20400007u, {0x00, 0x00}},
                        {0x20400009u, utf16_item("Entry")}});
    const auto entry_state = parse_or_fail("CTable", entry);
    check_eq(entry_state.rows.size(), std::size_t{1}, "row set: one CTable row");
    if (entry_state.rows.size() == 1) {
        const auto& row = entry_state.rows[0];
        check(row.definition.decoded, "row set: CTable row set decoded");
        check_eq(row.definition.size, 38u, "row set: task-shaped size");
        check_eq(row.definition.records.size(), std::size_t{2}, "row set: task-shaped count");
        check_eq(row.definition.name, std::string("Entry"), "row set: decoded name");
        check_eq(row.display_caption, std::string("Entry"), "row set: display caption from name");
        const auto* criteria = row.definition.find(0x20400007u);
        check(criteria != nullptr, "row set: criteria record exposed");
        if (criteria != nullptr) {
            check_eq(criteria->payload_offset, 24u, "row set: criteria offset");
            check_eq(criteria->payload_length, 2u, "row set: criteria length");
        }
        const auto* name = row.definition.find(0x20400009u);
        check(name != nullptr, "row set: name record exposed");
        if (name != nullptr) {
            check_eq(name->payload_offset, 26u, "row set: task-shaped name offset");
        }
    }

    // Resource-shaped set (name only): the name starts at relative offset 16 =
    // row + 126, so the legacy +136 read would produce "esources".  The record
    // set is authoritative.
    Parts resource = build_parts(230, {{"&All Resources", "esources"}}, {}, 10);
    attach_row_records(resource, 0, 230,
                       {{0x20400009u, utf16_item("All Resources")}});
    const auto resource_state = parse_or_fail("CFilter", resource);
    check_eq(resource_state.rows.size(), std::size_t{1}, "row set: one CFilter row");
    if (resource_state.rows.size() == 1) {
        check_eq(resource_state.rows[0].definition.name, std::string("All Resources"),
                 "row set: resource-shaped name");
        check_eq(resource_state.rows[0].display_caption, std::string("All Resources"),
                 "row set: resource-shaped display caption overrides +136");
        if (!resource_state.rows[0].definition.records.empty()) {
            check_eq(resource_state.rows[0].definition.records[0].payload_offset, 16u,
                     "row set: resource-shaped name offset");
        }
    }

    msproject::viewstate::RowRecordSet set;
    msproject::viewstate::ParseError error;
    const std::vector<std::uint8_t> zero(230, 0);
    check(msproject::viewstate::parse_row_record_set(zero, set, &error),
          "row set: zero size/count is the measured no-set shape");
    check(!set.decoded && set.records.empty(), "row set: no-set shape is not decoded");

    std::vector<std::uint8_t> bad = zero;
    put_u32(bad, 0x6e, 32);
    put_u32(bad, 0x72, 1);
    put_u32(bad, 0x76, 0x12345678u);
    put_u32(bad, 0x7a, 16);
    check(!msproject::viewstate::parse_row_record_set(bad, set, &error),
          "row set: non-property id refused");
    check(error.message.find("property id") != std::string::npos,
          "row set: non-property id message");

    bad = zero;
    put_u32(bad, 0x6e, 32);
    put_u32(bad, 0x72, 1);
    put_u32(bad, 0x76, 0x20400009u);
    put_u32(bad, 0x7a, 40);
    check(!msproject::viewstate::parse_row_record_set(bad, set, &error),
          "row set: payload offset outside the set refused");

    bad = zero;
    put_u32(bad, 0x6e, 40);
    put_u32(bad, 0x72, 0);
    check(!msproject::viewstate::parse_row_record_set(bad, set, &error),
          "row set: size without records refused");
}

void test_filter_definitions() {
    Parts parts = build_parts(230, {{"&All Tasks", "All Tasks"}},
                              {{1, 0x20400006u, make_filter_block(1, "tasks")}});
    attach_row_records(parts, 0, 230,
                       {{0x20400007u, {0x00, 0x00}},
                        {0x20400009u, utf16_item("All Tasks")}});
    const auto state = parse_or_fail("CFilter", parts);
    check_eq(state.filters.size(), std::size_t{1}, "filter defs: count");
    if (state.filters.size() == 1) {
        const auto& filter = state.filters[0];
        check_eq(filter.caption, std::string("&All Tasks"), "filter defs: caption");
        check_eq(filter.display_name, std::string("All Tasks"), "filter defs: display name");
        check_eq(filter.definition.name, std::string("All Tasks"),
                 "filter defs: record-set name");
        check(filter.block_decoded, "filter defs: block decoded");
        check_eq(filter.block_offset, 0u, "filter defs: block offset");
        check_eq(filter.block_ordinal, 1u, "filter defs: block ordinal");
        check(filter.applies_to_decoded, "filter defs: applies-to decoded");
        check_eq(filter.applies_to, std::string("tasks"), "filter defs: applies-to text");
        check_eq(filter.applies_to_offset, 92u, "filter defs: applies-to offset");
    }
    check_eq(state.block_refs.size(), std::size_t{1}, "filter defs: block refs");
    if (!state.block_refs.empty()) {
        check_eq(state.block_refs[0].kind, std::string("filter-definition"),
                 "filter defs: block kind");
    }
    EncodedParts encoded;
    check(encode_view_state(state, encoded, nullptr), "filter defs: encode");
    check_same_parts(encoded, parts, "filter defs: no-op round-trip");

    // The measured resources variant: same framing, a different applies-to.
    Parts resources = build_parts(230, {{"&All Resources", "All Resources"}},
                                  {{1, 0x20400006u, make_filter_block(1, "resources")}});
    attach_row_records(resources, 0, 230,
                       {{0x20400009u, utf16_item("All Resources")}});
    const auto resource_state = parse_or_fail("CFilter", resources);
    check_eq(resource_state.filters.size(), std::size_t{1}, "filter defs: resources count");
    if (resource_state.filters.size() == 1) {
        check_eq(resource_state.filters[0].applies_to, std::string("resources"),
                 "filter defs: resources applies-to");
        check_eq(resource_state.filters[0].block_length, 112u,
                 "filter defs: resources block length");
    }
}

void test_grouping_definitions() {
    std::vector<BlockSpec> blocks;
    blocks.push_back({1, 0x20400006u, make_grouping_block(1)});
    blocks.push_back({2, 0x20400006u, make_grouping_block(2)});
    Parts parts = build_parts(230, {{"&No Group", "No Task Group"}, {"&No Group", "No Resource Group"}},
                              blocks);
    attach_row_records(parts, 0, 230,
                       {{0x2D400007u, {0x00, 0x00}},
                        {0x2D400009u, utf16_item("No Task Group")}});
    attach_row_records(parts, 1, 230,
                       {{0x2D400009u, utf16_item("No Resource Group")}});
    const auto state = parse_or_fail("CGrouping", parts);
    check_eq(state.groupings.size(), std::size_t{2}, "grouping defs: count");
    if (state.groupings.size() == 2) {
        check_eq(state.groupings[0].caption, std::string("&No Group"),
                 "grouping defs: first caption");
        check_eq(state.groupings[0].display_name, std::string("No Task Group"),
                 "grouping defs: first display name");
        check_eq(state.groupings[0].definition.name, std::string("No Task Group"),
                 "grouping defs: first record-set name");
        check(state.groupings[0].block_decoded, "grouping defs: first block decoded");
        check_eq(state.groupings[0].block_offset, 0u, "grouping defs: first block offset");
        check_eq(state.groupings[1].display_name, std::string("No Resource Group"),
                 "grouping defs: second display name (name-only set)");
        check_eq(state.groupings[1].block_offset, 87u, "grouping defs: second block offset");
        check_eq(state.groupings[1].block_length, 83u, "grouping defs: second block length");
    }
    EncodedParts encoded;
    check(encode_view_state(state, encoded, nullptr), "grouping defs: encode");
    check_same_parts(encoded, parts, "grouping defs: no-op round-trip");
}

void test_edl_typed() {
    std::vector<std::uint32_t> ids(kEntryFieldIds.begin() + 1, kEntryFieldIds.end());
    const Parts parts = make_edl_parts("&Gantt Chart", "Entry", ids, true);
    const auto state = parse_or_fail("CEdl", parts);
    check_eq(state.embedded_props.size(), std::size_t{1}, "CEdl typed: one Props block");
    if (state.embedded_props.size() == 1) {
        const auto& edl = state.embedded_props[0];
        check(edl.view_name_decoded, "CEdl typed: view name decoded");
        check_eq(edl.view_name, std::string("&Gantt Chart"), "CEdl typed: view name");
        check(edl.table_name_decoded, "CEdl typed: table name decoded");
        check_eq(edl.table_name, std::string("Entry"), "CEdl typed: table name");
        check(edl.field_list_decoded, "CEdl typed: field list decoded");
        check(edl.field_list_terminated, "CEdl typed: terminator seen");
        check_eq(edl.field_ids.size(), ids.size(), "CEdl typed: field id count");
        if (edl.field_ids.size() == ids.size()) {
            for (std::size_t i = 0; i < ids.size(); ++i) {
                check_eq(edl.field_ids[i], ids[i],
                         "CEdl typed: field id " + std::to_string(i));
            }
        }
        check_eq(edl.records.size(), std::size_t{5}, "CEdl typed: record set size");
        const auto* host = edl.find(0x26400031u);
        check(host != nullptr, "CEdl typed: opaque host-record exposed");
        if (host != nullptr) {
            check_eq(host->payload_length, 38u, "CEdl typed: opaque record length");
        }
    }
    EncodedParts encoded;
    check(encode_view_state(state, encoded, nullptr), "CEdl typed: encode");
    check_same_parts(encoded, parts, "CEdl typed: no-op round-trip");

    // A field list with no terminator (the measured flags-2 shape).
    Parts plain = make_edl_parts("&Gantt Chart", "Entry", ids, false);
    const auto plain_state = parse_or_fail("CEdl", plain);
    check_eq(plain_state.embedded_props.size(), std::size_t{1}, "CEdl typed: plain block");
    if (plain_state.embedded_props.size() == 1) {
        check(plain_state.embedded_props[0].field_list_decoded,
              "CEdl typed: plain field list decoded");
        check(!plain_state.embedded_props[0].field_list_terminated,
              "CEdl typed: plain field list has no terminator");
        check_eq(plain_state.embedded_props[0].field_ids.size(), ids.size(),
                 "CEdl typed: plain field id count");
    }
}

void test_noop_five_classes() {
    std::vector<std::uint32_t> ids(kEntryFieldIds.begin() + 1, kEntryFieldIds.end());

    Parts ctable;
    check(make_ctable_parts("synthetic-viewstate-ctable-base.bin", ctable),
          "five-class no-op: CTable fixture");
    const Parts views = make_view_parts();
    const Parts edl = make_edl_parts("&Gantt Chart", "Entry", ids, true);
    Parts filter = build_parts(230, {{"&All Tasks", "All Tasks"}},
                               {{1, 0x20400006u, make_filter_block(1, "tasks")}});
    attach_row_records(filter, 0, 230,
                       {{0x20400007u, {0x00, 0x00}},
                        {0x20400009u, utf16_item("All Tasks")}});
    std::vector<BlockSpec> grouping_blocks;
    grouping_blocks.push_back({1, 0x20400006u, make_grouping_block(1)});
    Parts grouping = build_parts(230, {{"&No Group", "No Task Group"}}, grouping_blocks);
    attach_row_records(grouping, 0, 230,
                       {{0x2D400009u, utf16_item("No Task Group")}});

    const std::pair<const char*, const Parts*> cases[] = {
        {"CTable", &ctable},     {"CV_iew", &views},       {"CEdl", &edl},
        {"CFilter", &filter},    {"CGrouping", &grouping},
    };
    for (const auto& [klass, parts] : cases) {
        const auto state = parse_or_fail(klass, *parts);
        EncodedParts encoded;
        check(encode_view_state(state, encoded, nullptr),
              std::string("five-class no-op: encode ") + klass);
        check_same_parts(encoded, *parts, std::string("five-class no-op: ") + klass);
    }
}

void test_view_name_edit() {
    const Parts parts = make_view_parts();
    auto state = parse_or_fail("CV_iew", parts);
    EncodedParts before;
    check(encode_view_state(state, before, nullptr), "view name: encode before");

    WriteError error;
    check(msproject::viewstate::set_view_name(state, 0, "Renamed &View", &error),
          "view name: rename view 0");
    if (!error.message.empty()) {
        std::fprintf(stderr, "view-name error: %s\n", error.message.c_str());
    }
    check_eq(state.views[0].name, std::string("Renamed &View"), "view name: decoded name");
    check_eq(state.rows[0].caption, std::string("Renamed &View"), "view name: row caption");
    EncodedParts after;
    check(encode_view_state(state, after, nullptr), "view name: encode after");

    check(after.fixed_meta == before.fixed_meta, "view name: FixedMeta unchanged");
    check(after.var_meta == before.var_meta, "view name: VarMeta unchanged");
    check(after.var2_data == before.var2_data, "view name: Var2Data unchanged");
    const std::size_t caption_begin = 4;
    const std::size_t caption_end = 4 + 118;
    const auto diffs = diff_positions(before.fixed_data, after.fixed_data);
    check(!diffs.empty(), "view name: rename changes bytes");
    bool confined = true;
    for (const std::size_t at : diffs) {
        if (at < caption_begin || at >= caption_end) {
            confined = false;
        }
    }
    check(confined, "view name: changes only the 118-byte caption area of row 0");
    check_eq(msproject::viewstate::utf16_ascii(after.fixed_data, 4, 59),
             std::string("Renamed &View"), "view name: caption reads back");

    Parts edited;
    edited.fixed_data = after.fixed_data;
    edited.fixed_meta = after.fixed_meta;
    edited.var_meta = after.var_meta;
    edited.var2 = after.var2_data;
    const auto reparsed = parse_or_fail("CV_iew", edited);
    check_eq(reparsed.views.size(), std::size_t{2}, "view name: re-parsed views");
    if (reparsed.views.size() == 2) {
        check_eq(reparsed.views[0].name, std::string("Renamed &View"),
                 "view name: re-parsed name");
        check_eq(reparsed.views[0].display_name, std::string("Gantt Chart"),
                 "view name: display name left byte-identical");
        check(reparsed.views[1].main_decoded, "view name: other view still decodes");
    }

    // Refusals leave every part unchanged.
    EncodedParts guard;
    check(encode_view_state(state, guard, nullptr), "view name: encode before refusals");
    error = WriteError{};
    check(!msproject::viewstate::set_view_name(state, 2, "x", &error),
          "view name: bad index refused");
    check(error.message.find("out of range") != std::string::npos,
          "view name: bad index message");
    error = WriteError{};
    check(!msproject::viewstate::set_view_name(state, 0, "", &error),
          "view name: empty name refused");
    error = WriteError{};
    check(!msproject::viewstate::set_view_name(state, 0, "caf\xe9", &error),
          "view name: non-ASCII name refused");
    error = WriteError{};
    check(!msproject::viewstate::set_view_name(state, 0, std::string(60, 'A'), &error),
          "view name: 60-unit name refused");
    check(error.message.find("capacity") != std::string::npos,
          "view name: capacity message");
    error = WriteError{};
    check(msproject::viewstate::set_view_name(state, 0, std::string(59, 'A'), nullptr),
          "view name: 59-unit name accepted");
    EncodedParts full;
    check(encode_view_state(state, full, nullptr), "view name: encode 59-unit name");
    check_eq(msproject::viewstate::utf16_ascii(full.fixed_data, 4, 59),
             std::string(59, 'A'), "view name: 59-unit name reads back");

    auto ctable = parse_or_fail("CTable", [&] {
        Parts parts;
        make_ctable_parts("synthetic-viewstate-ctable-base.bin", parts);
        return parts;
    }());
    error = WriteError{};
    check(!msproject::viewstate::set_view_name(ctable, 0, "x", &error),
          "view name: CTable state refused");
}

void test_view_table_edit() {
    const Parts parts = make_view_parts();
    auto state = parse_or_fail("CV_iew", parts);
    check_eq(state.views.size(), std::size_t{2}, "view table: views");
    if (state.views.size() != 2) {
        return;
    }
    const std::uint32_t old_block_offset = state.views[1].main_block_offset;
    EncodedParts before;
    check(encode_view_state(state, before, nullptr), "view table: encode before");

    WriteError error;
    check(msproject::viewstate::set_view_table(state, 0, "W14 Entry C", &error),
          "view table: set table on view 0");
    if (!error.message.empty()) {
        std::fprintf(stderr, "view-table error: %s\n", error.message.c_str());
    }
    check_eq(state.views[0].table_or_top, std::string("W14 Entry C"),
             "view table: decoded table name");
    check_eq(state.views[1].main_block_offset, old_block_offset + 10u,
             "view table: later view block offset shifted by +10");
    check(state.views[1].main_decoded, "view table: later view still decodes");
    check_eq(state.views[1].table_or_top, std::string("&Gantt Chart"),
             "view table: later view item unchanged");

    EncodedParts after;
    check(encode_view_state(state, after, nullptr), "view table: encode after");
    check(after.fixed_data == before.fixed_data, "view table: FixedData unchanged");
    check(after.fixed_meta == before.fixed_meta, "view table: FixedMeta unchanged");
    check(after.var_meta.size() == before.var_meta.size(), "view table: VarMeta size unchanged");
    check_eq(after.var2_data.size(), before.var2_data.size() + 10,
             "view table: Var2Data grows by the item delta");
    check_eq(read_u32(after.var2_data, 0), read_u32(before.var2_data, 0) + 10u,
             "view table: block length word updated");
    // The target item payload starts after the 16-byte block header and the
    // first item (12 + len); find it from the decoded view.
    std::size_t item_record_start = 0;
    std::size_t item_start = 0;
    for (const auto& record : state.views[0].records) {
        if (record.id == 0x2240000Au) {
            item_record_start = 4 + record.record_offset;
            item_start = item_record_start + 12;
        }
    }
    check(item_start != 0, "view table: target item located");
    if (item_start != 0) {
        check_eq(msproject::viewstate::utf16_ascii(after.var2_data, item_start, 24),
                 std::string("W14 Entry C"), "view table: new item payload written");
        // Bytes after the block length word and the payload's two size words
        // (payload +0/+4) are identical up to the target item's own header.
        bool prefix_same = true;
        for (std::size_t i = 4 + 16; i < item_record_start; ++i) {
            if (before.var2_data[i] != after.var2_data[i]) {
                prefix_same = false;
            }
        }
        check(prefix_same, "view table: every item before the target is unchanged");
    }

    Parts edited;
    edited.fixed_data = after.fixed_data;
    edited.fixed_meta = after.fixed_meta;
    edited.var_meta = after.var_meta;
    edited.var2 = after.var2_data;
    const auto reparsed = parse_or_fail("CV_iew", edited);
    check_eq(reparsed.views.size(), std::size_t{2}, "view table: re-parsed views");
    if (reparsed.views.size() == 2) {
        check(reparsed.views[0].main_decoded, "view table: re-parsed main decoded");
        check_eq(reparsed.views[0].table_or_top, std::string("W14 Entry C"),
                 "view table: re-parsed table name");
        check_eq(reparsed.views[0].display_name, std::string("Gantt Chart"),
                 "view table: re-parsed display name");
        check_eq(reparsed.views[1].table_or_top, std::string("&Gantt Chart"),
                 "view table: re-parsed later view table");
        check_eq(reparsed.views[0].options_decoded, true, "view table: options still decode");
    }

    // Writing the same name is a byte-identical no-op.
    EncodedParts guard;
    check(encode_view_state(state, guard, nullptr), "view table: encode before no-op");
    check(msproject::viewstate::set_view_table(state, 0, "W14 Entry C", nullptr),
          "view table: same name accepted");
    EncodedParts same;
    check(encode_view_state(state, same, nullptr), "view table: encode after same name");
    check(same.var2_data == guard.var2_data, "view table: same name is a no-op");

    // Refusals.
    error = WriteError{};
    check(!msproject::viewstate::set_view_table(state, 7, "X", &error),
          "view table: bad index refused");
    error = WriteError{};
    check(!msproject::viewstate::set_view_table(state, 0, "", &error),
          "view table: empty name refused");
    error = WriteError{};
    check(!msproject::viewstate::set_view_table(state, 0, "caf\xe9", &error),
          "view table: non-ASCII name refused");
    error = WriteError{};
    check(!msproject::viewstate::set_view_table(state, 0, std::string(128, 'A'), &error),
          "view table: 128-unit name refused");
    // A view whose main block has no 0x2240000a item is refused: build one.
    std::vector<ViewItemSpec> no_table;
    no_table.push_back({0x2240002Au, 1, utf16_item("Only A Name")});
    Parts no_table_parts = build_parts(138, {{"&No Table", ""}},
                                       {{1, 0x20400006u, make_view_main_block(no_table)}});
    auto no_table_state = parse_or_fail("CV_iew", no_table_parts);
    error = WriteError{};
    check(!msproject::viewstate::set_view_table(no_table_state, 0, "Entry", &error),
          "view table: view without 0x2240000a refused");
    check(error.message.find("0x2240000a") != std::string::npos,
          "view table: missing item message");
    EncodedParts no_table_after;
    check(encode_view_state(no_table_state, no_table_after, nullptr),
          "view table: encode refused state");
    check_same_parts(no_table_after, no_table_parts, "view table: refusal leaves parts unchanged");
}

void test_view_margin_edit() {
    const Parts parts = make_view_parts();
    auto state = parse_or_fail("CV_iew", parts);
    EncodedParts before;
    check(encode_view_state(state, before, nullptr), "view margins: encode before");

    const std::array<std::uint32_t, 4> margins = {720, 720, 720, 720};
    WriteError error;
    check(msproject::viewstate::set_view_page_margins(state, 0, margins, &error),
          "view margins: set margins");
    check(state.views[0].page_margins_thousandths == margins, "view margins: decoded state");
    EncodedParts after;
    check(encode_view_state(state, after, nullptr), "view margins: encode after");

    const auto diffs = diff_positions(before.var2_data, after.var2_data);
    // 500 -> 720 changes two bytes per u32 (0x01f4 -> 0x02d0): eight bytes total.
    check_eq(diffs.size(), std::size_t{8}, "view margins: eight bytes change");
    check_eq(after.var2_data.size(), before.var2_data.size(), "view margins: size unchanged");
    std::size_t margin_base = 0;
    for (const auto& record : state.views[0].records) {
        if (record.id == 0x22400009u) {
            margin_base = static_cast<std::size_t>(state.views[0].main_block_offset) + 4 +
                          record.record_offset + 12;
        }
    }
    bool in_margin_words = !diffs.empty() && margin_base != 0;
    for (const std::size_t at : diffs) {
        if (at < margin_base || at >= margin_base + 16) {
            in_margin_words = false;
        }
    }
    check(in_margin_words, "view margins: every change is inside the four margin u32");

    Parts edited;
    edited.fixed_data = after.fixed_data;
    edited.fixed_meta = after.fixed_meta;
    edited.var_meta = after.var_meta;
    edited.var2 = after.var2_data;
    const auto reparsed = parse_or_fail("CV_iew", edited);
    if (reparsed.views.size() == 2) {
        check(reparsed.views[0].page_margins_thousandths == margins,
              "view margins: re-parsed margins");
        check_eq(reparsed.views[0].page_margin_extra, 2000u,
                 "view margins: fifth word preserved");
        check_eq(reparsed.views[0].table_or_top, std::string("&Entry"),
                 "view margins: table item preserved");
    }

    error = WriteError{};
    check(!msproject::viewstate::set_view_page_margins(state, 1, margins, &error),
          "view margins: view without options refused");
    check(error.message.find("0x22400009") != std::string::npos,
          "view margins: missing options message");
    error = WriteError{};
    const std::array<std::uint32_t, 4> huge = {1, 2, 3, 100001};
    check(!msproject::viewstate::set_view_page_margins(state, 0, huge, &error),
          "view margins: over-bound value refused");
    EncodedParts guard;
    check(encode_view_state(state, guard, nullptr), "view margins: encode after refusals");
    check(guard.var2_data == after.var2_data, "view margins: refusals leave parts unchanged");
}

void test_edl_regenerate() {
    Parts ctable_parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-base.bin", ctable_parts)) {
        return;
    }
    const Parts view_parts = make_view_parts();
    std::vector<std::uint32_t> ids(kEntryFieldIds.begin() + 1, kEntryFieldIds.end());
    const Parts edl_parts = make_edl_parts("&Gantt Chart", "Entry", ids, true);

    auto ctable = parse_or_fail("CTable", ctable_parts);
    auto views = parse_or_fail("CV_iew", view_parts);
    auto edl = parse_or_fail("CEdl", edl_parts);
    ctable.related = {&views, &edl};
    std::vector<std::uint8_t> host_before;
    if (!edl.embedded_props.empty()) {
        const auto* host = edl.embedded_props[0].document.find(0x26400031u);
        if (host != nullptr) {
            const auto payload = edl.embedded_props[0].document.payload(*host);
            host_before.assign(payload.begin(), payload.end());
        }
    }

    // A consistent CEdl regenerates to the same bytes.
    EncodedParts edl_before;
    check(encode_view_state(edl, edl_before, nullptr), "CEdl regen: encode before");
    WriteError error;
    check(msproject::viewstate::regenerate_edl(edl, ctable, &error),
          "CEdl regen: consistent state");
    if (!error.message.empty()) {
        std::fprintf(stderr, "regenerate_edl error: %s\n", error.message.c_str());
    }
    EncodedParts edl_after_noop;
    check(encode_view_state(edl, edl_after_noop, nullptr), "CEdl regen: encode no-op");
    check_same_parts(edl_after_noop, edl_parts, "CEdl regen: consistent state is a no-op");

    // Append a column: the field list grows by one id (and the terminator stays).
    check(append_table_column(ctable, 0, 0x0b400057u, "Number1", nullptr),
          "CEdl regen: append column");
    error = WriteError{};
    check(msproject::viewstate::regenerate_edl(edl, ctable, &error),
          "CEdl regen: regenerate after append");
    check_eq(edl.embedded_props.size(), std::size_t{1}, "CEdl regen: still one Props block");
    if (edl.embedded_props.size() == 1) {
        const auto& refreshed = edl.embedded_props[0];
        check_eq(refreshed.field_ids.size(), std::size_t{9}, "CEdl regen: nine field ids");
        check(refreshed.field_list_terminated, "CEdl regen: terminator preserved");
        if (refreshed.field_ids.size() == 9) {
            check_eq(refreshed.field_ids.back(), 0x0b400057u,
                     "CEdl regen: appended column is last");
        }
        check_eq(refreshed.view_name, std::string("&Gantt Chart"),
                 "CEdl regen: view name refreshed");
        check_eq(refreshed.table_name, std::string("Entry"), "CEdl regen: table name");
        check_eq(refreshed.records.size(), std::size_t{5}, "CEdl regen: record count preserved");
        const auto* host = refreshed.find(0x26400031u);
        check(host != nullptr, "CEdl regen: opaque host record preserved");
        check(refreshed.document.find(0x26400031u) != nullptr,
              "CEdl regen: typed accessor finds the host record");
        if (host != nullptr) {
            check_eq(host->payload_length, 38u, "CEdl regen: host record length preserved");
            const auto payload = refreshed.document.payload(*refreshed.document.find(0x26400031u));
            check_eq(payload.size(), host_before.size(),
                     "CEdl regen: host record size preserved");
            check(std::equal(host_before.begin(), host_before.end(), payload.begin(),
                             payload.end()),
                  "CEdl regen: host record bytes preserved");
        }
        // The 0x26400017 record payload after appending Number1 must be the
        // exact 40-byte record Project wrote on its own re-save of the gate file
        // (`dump-w15proj`): the base Entry columns 1..8, the appended Number1
        // 0x0b400057, then the 0xffffffff terminator.
        const auto* field_list = refreshed.document.find(0x26400017u);
        check(field_list != nullptr, "CEdl regen: field-list record present");
        const auto* typed_field_list = refreshed.find(0x26400017u);
        check(typed_field_list != nullptr, "CEdl regen: typed field-list record present");
        if (typed_field_list != nullptr) {
            check_eq(typed_field_list->payload_length, 40u,
                     "CEdl regen: typed field-list payload length");
        }
        if (field_list != nullptr) {
            const std::array<std::uint8_t, 40> expected = {
                0xcd, 0x00, 0x40, 0x0b, 0x00, 0x05, 0x40, 0x0b, 0x0e, 0x00,
                0x40, 0x0b, 0x07, 0x05, 0x40, 0x0b, 0x05, 0x05, 0x40, 0x0b,
                0x06, 0x05, 0x40, 0x0b, 0x2f, 0x00, 0x40, 0x0b, 0x31, 0x00,
                0x40, 0x0b, 0x57, 0x00, 0x40, 0x0b, 0xff, 0xff, 0xff, 0xff};
            const auto payload = refreshed.document.payload(*field_list);
            check_eq(payload.size(), expected.size(),
                     "CEdl regen: field-list payload length matches the Project re-save");
            bool same = payload.size() == expected.size();
            for (std::size_t i = 0; i < payload.size() && i < expected.size(); ++i) {
                if (payload[i] != expected[i]) {
                    same = false;
                }
            }
            check(same, "CEdl regen: field-list bytes match Project's re-save exactly");
        }
    }
    EncodedParts edl_after;
    check(encode_view_state(edl, edl_after, nullptr), "CEdl regen: encode after");
    check_eq(edl_after.var2_data.size(), edl_before.var2_data.size() + 4,
             "CEdl regen: field list grew by one u32");
    check_eq(read_u32(edl_after.var2_data, 0), read_u32(edl_before.var2_data, 0) + 4u,
             "CEdl regen: block length word grew by 4");
    Parts regenerated;
    regenerated.fixed_data = edl_after.fixed_data;
    regenerated.fixed_meta = edl_after.fixed_meta;
    regenerated.var_meta = edl_after.var_meta;
    regenerated.var2 = edl_after.var2_data;
    const auto reparsed = parse_or_fail("CEdl", regenerated);
    check_eq(reparsed.embedded_props.size(), std::size_t{1}, "CEdl regen: re-parsed CEdl");
    if (reparsed.embedded_props.size() == 1) {
        check_eq(reparsed.embedded_props[0].field_ids.size(), std::size_t{9},
                 "CEdl regen: re-parsed field id count");
        check_eq(reparsed.embedded_props[0].table_name, std::string("Entry"),
                 "CEdl regen: re-parsed table name");
    }

    // Stale names are refreshed from the linked CV_iew.
    auto stale = parse_or_fail("CEdl", make_edl_parts("Stale View", "Stale Table", ids, false));
    stale.related = {&views};
    error = WriteError{};
    check(msproject::viewstate::regenerate_edl(stale, ctable, &error),
          "CEdl regen: stale names resolved through the applied table");
    if (stale.embedded_props.size() == 1) {
        check_eq(stale.embedded_props[0].view_name, std::string("&Gantt Chart"),
                 "CEdl regen: stale view name refreshed");
        check_eq(stale.embedded_props[0].table_name, std::string("Entry"),
                 "CEdl regen: stale table name refreshed");
    }

    // Table change via CV_iew: set_view_table("W14 Entry C") must move both the
    // table reference and the field list to that table.
    Parts two_table_parts = make_ctable_parts_from_payloads(
        {{"&Entry", make_table_block(1, 0xff, 6, {{0x0b400017u, ""}, {0x0b40000eu, "Task Name"}})},
         {"&W14 Entry C",
          make_table_block(2, 0xff, 6,
                           {{0x0b400017u, ""}, {0x0b400057u, "Number1"}, {0x0b400031u, "Text1"}})}});
    auto two_tables = parse_or_fail("CTable", two_table_parts);
    auto two_views = parse_or_fail("CV_iew", make_view_parts());
    auto two_edl = parse_or_fail("CEdl", make_edl_parts("&Gantt Chart", "Entry",
                                                        {0x0b40000eu, 0x0b400017u}, false));
    two_tables.related = {&two_views, &two_edl};
    check(msproject::viewstate::set_view_table(two_views, 0, "W14 Entry C", nullptr),
          "CEdl regen: view switched to the second table");
    error = WriteError{};
    check(msproject::viewstate::regenerate_edl(two_edl, two_tables, &error),
          "CEdl regen: regenerate against the second table");
    if (two_edl.embedded_props.size() == 1) {
        check_eq(two_edl.embedded_props[0].table_name, std::string("W14 Entry C"),
                 "CEdl regen: switched table name");
        check_eq(two_edl.embedded_props[0].field_ids.size(), std::size_t{2},
                 "CEdl regen: switched table field ids");
        if (two_edl.embedded_props[0].field_ids.size() == 2) {
            check_eq(two_edl.embedded_props[0].field_ids[0], 0x0b400057u,
                     "CEdl regen: switched table first field");
            check_eq(two_edl.embedded_props[0].field_ids[1], 0x0b400031u,
                     "CEdl regen: switched table second field");
        }
    }

    // Refusals leave the state untouched.
    EncodedParts guard;
    check(encode_view_state(edl, guard, nullptr), "CEdl regen: encode before refusals");
    error = WriteError{};
    check(!msproject::viewstate::regenerate_edl(views, ctable, &error),
          "CEdl regen: non-CEdl state refused");
    check(error.message.find("CEdl") != std::string::npos, "CEdl regen: class message");
    error = WriteError{};
    check(!msproject::viewstate::regenerate_edl(edl, views, &error),
          "CEdl regen: non-CTable source refused");
    // A CEdl with no field-list record cannot be regenerated.
    std::vector<TestEdlRecord> bare;
    bare.push_back({0x26400018u, utf16_item("&Gantt Chart")});
    bare.push_back({0x26400019u, utf16_item("Entry")});
    Parts bare_parts = build_parts(160, {{"", ""}},
                                   {{1, 0x20400006u, make_edl_document(bare)}}, 11);
    auto bare_state = parse_or_fail("CEdl", bare_parts);
    error = WriteError{};
    check(!msproject::viewstate::regenerate_edl(bare_state, ctable, &error),
          "CEdl regen: missing field-list record refused");
    check(error.message.find("0x26400017") != std::string::npos,
          "CEdl regen: missing record message names the id");
    // Ambiguous table selector (two tables with the same name) refused.
    Parts ambiguous = make_ctable_parts_from_payloads(
        {{"&Entry", make_table_block(1, 0xff, 6, {{0x0b400017u, ""}, {0x0b40000eu, "Task Name"}})},
         {"&Entry", make_table_block(2, 0xff, 6, {{0x0b400017u, ""}, {0x0b400031u, "Text1"}})}});
    auto ambiguous_state = parse_or_fail("CTable", ambiguous);
    auto ambiguous_edl = parse_or_fail("CEdl", make_edl_parts("&Gantt Chart", "Entry", ids, true));
    error = WriteError{};
    check(!msproject::viewstate::regenerate_edl(ambiguous_edl, ambiguous_state, &error),
          "CEdl regen: ambiguous table refused");
    check(error.message.find("exactly one") != std::string::npos,
          "CEdl regen: ambiguity message");
    EncodedParts guard_after;
    check(encode_view_state(edl, guard_after, nullptr), "CEdl regen: encode after refusals");
    check(guard_after.var2_data == guard.var2_data && guard_after.fixed_data == guard.fixed_data,
          "CEdl regen: refusals leave the state untouched");
}

void test_regenerate_view_state() {
    Parts ctable_parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-base.bin", ctable_parts)) {
        return;
    }
    const Parts view_parts = make_view_parts();
    std::vector<std::uint32_t> ids(kEntryFieldIds.begin() + 1, kEntryFieldIds.end());
    const Parts edl_parts = make_edl_parts("&Gantt Chart", "Entry", ids, true);

    auto original_ctable = parse_or_fail("CTable", ctable_parts);
    auto ctable = original_ctable;
    auto views = parse_or_fail("CV_iew", view_parts);
    auto edl = parse_or_fail("CEdl", edl_parts);
    ctable.related = {&views, &edl};

    EncodedParts views_before;
    check(encode_view_state(views, views_before, nullptr), "seam: encode CV_iew before");
    const std::vector<msproject::viewstate::TableEdit> edits = {
        msproject::viewstate::table_column_append_edit(0, 0x0b400057u, "Number1")};
    WriteError error;
    check(msproject::viewstate::regenerate_view_state(ctable, edits, &error),
          "seam: apply append through regenerate_view_state");
    if (!error.message.empty()) {
        std::fprintf(stderr, "seam error: %s\n", error.message.c_str());
    }
    check_eq(ctable.tables.size(), std::size_t{1}, "seam: CTable tables");
    if (ctable.tables.size() == 1) {
        check_eq(ctable.tables[0].columns.size(), std::size_t{10}, "seam: appended column");
    }
    check_eq(edl.embedded_props.size(), std::size_t{1}, "seam: CEdl block");
    if (edl.embedded_props.size() == 1) {
        check_eq(edl.embedded_props[0].field_ids.size(), std::size_t{9},
                 "seam: CEdl field list refreshed");
        if (edl.embedded_props[0].field_ids.size() == 9) {
            check_eq(edl.embedded_props[0].field_ids.back(), 0x0b400057u,
                     "seam: CEdl appended field id");
        }
    }
    EncodedParts views_after;
    check(encode_view_state(views, views_after, nullptr), "seam: encode CV_iew after");
    check_same_parts(views_after, view_parts, "seam: CV_iew left byte-identical");

    // The seam's table bytes equal apply_table_edits() on the same edits.
    EncodedParts seam_table;
    check(encode_view_state(ctable, seam_table, nullptr), "seam: encode CTable");
    EncodedParts direct;
    check(msproject::viewstate::apply_table_edits(original_ctable, edits, direct, nullptr),
          "seam: apply_table_edits reference");
    check(seam_table.var2_data == direct.var2_data,
          "seam: table bytes match apply_table_edits");
    check(seam_table.fixed_data == direct.fixed_data &&
              seam_table.fixed_meta == direct.fixed_meta &&
              seam_table.var_meta == direct.var_meta,
          "seam: table framing matches apply_table_edits");

    // Re-parse the regenerated CTable.
    Parts seam_parts;
    seam_parts.fixed_data = seam_table.fixed_data;
    seam_parts.fixed_meta = seam_table.fixed_meta;
    seam_parts.var_meta = seam_table.var_meta;
    seam_parts.var2 = seam_table.var2_data;
    const auto reparsed = parse_or_fail("CTable", seam_parts);
    if (reparsed.tables.size() == 1) {
        check_eq(reparsed.tables[0].columns.size(), std::size_t{10},
                 "seam: re-parsed column count");
    }

    // A later failing edit leaves both states untouched.
    auto guard_ctable = original_ctable;
    auto guard_views = parse_or_fail("CV_iew", view_parts);
    auto guard_edl = parse_or_fail("CEdl", edl_parts);
    guard_ctable.related = {&guard_views, &guard_edl};
    const std::vector<msproject::viewstate::TableEdit> failing = {
        msproject::viewstate::table_column_append_edit(0, 0x0b400057u, "Number1"),
        msproject::viewstate::table_date_format_edit(7, 0x05)};
    error = WriteError{};
    check(!msproject::viewstate::regenerate_view_state(guard_ctable, failing, &error),
          "seam: failing edit refuses");
    check(error.message.find("table edit 1") != std::string::npos,
          "seam: failure names the edit");
    EncodedParts guard_after;
    check(encode_view_state(guard_ctable, guard_after, nullptr), "seam: encode guard CTable");
    check(guard_after.var2_data == ctable_parts.var2, "seam: CTable untouched on failure");
    EncodedParts guard_edl_out;
    check(encode_view_state(guard_edl, guard_edl_out, nullptr), "seam: encode guard CEdl");
    check_same_parts(guard_edl_out, edl_parts, "seam: CEdl untouched on failure");

    // Without links the seam is an in-place table edit.
    auto alone = parse_or_fail("CTable", ctable_parts);
    check(msproject::viewstate::regenerate_view_state(alone, edits, nullptr),
          "seam: works without links");
    check_eq(alone.tables.size(), std::size_t{1}, "seam: unlinked table");
    if (alone.tables.size() == 1) {
        check_eq(alone.tables[0].columns.size(), std::size_t{10},
                 "seam: unlinked table edited");
    }
    error = WriteError{};
    auto not_ctable = parse_or_fail("CV_iew", view_parts);
    check(!msproject::viewstate::regenerate_view_state(not_ctable, edits, &error),
          "seam: non-CTable state refused");
    check(error.message.find("CTable") != std::string::npos, "seam: CTable message");
}


// ---------------------------------------------------------------------------
// Wave 18 (F2): the CEdl cumulative-document cache, the CFilter criterion
// framing / ...0007 record, the CGrouping body framing and the
// CDrawing/CMap/CReport inventory.
// ---------------------------------------------------------------------------

struct FilterCriterionSpec {
    std::uint32_t tag = 0;
    std::uint32_t op = 0;
    std::uint32_t field = 0;
};

std::vector<std::uint8_t> make_filter_block_full(
    std::uint32_t ordinal,
    std::uint32_t flags,
    const std::vector<FilterCriterionSpec>& criteria,
    const std::vector<std::string>& strings) {
    const std::size_t table = msproject::viewstate::kFilterBlockHeaderSize +
                              criteria.size() * msproject::viewstate::kFilterCriterionSize;
    std::size_t total = table;
    for (const auto& text : strings) {
        total += text.size() * 2 + 2;
    }
    std::vector<std::uint8_t> payload(total, 0);
    put_u32(payload, 0, ordinal);
    put_u32(payload, 4, flags);
    put_u16(payload, 14, static_cast<std::uint16_t>(criteria.size()));
    put_u32(payload, 16, static_cast<std::uint32_t>(table));
    for (std::size_t i = 0; i < criteria.size(); ++i) {
        const std::size_t base = msproject::viewstate::kFilterBlockHeaderSize +
                                 i * msproject::viewstate::kFilterCriterionSize;
        put_u32(payload, base, criteria[i].tag);
        put_u32(payload, base + 4, criteria[i].op);
        put_u32(payload, base + 8, criteria[i].field);
    }
    std::size_t at = table;
    for (const auto& text : strings) {
        put_utf16(payload, at, text);
        at += text.size() * 2 + 2;
    }
    return payload;
}

void test_edl_cache_residue() {
    using namespace msproject::viewstate;
    const std::vector<std::uint8_t> doc_a = make_edl_document(
        {{0x26400018u, utf16_item("&Gantt Chart")},
         {0x26400019u, utf16_item("Entry")},
         {0x26400017u, field_list_bytes({0x0b40000eu, 0x0b400017u}, true)}});
    const std::vector<std::uint8_t> doc_b = make_edl_document(
        {{0x26400018u, utf16_item("&Tracking Gantt")},
         {0x26400019u, utf16_item("Entry")},
         {0x26400017u, field_list_bytes({0x0b40000eu}, false)}});

    Parts parts = build_parts(160, {{"", ""}}, {});
    parts.var2.clear();
    append_block(parts.var2, doc_a);
    const std::size_t block_b_offset = parts.var2.size() + 4;
    parts.var2.insert(parts.var2.end(), {0x5e, 0x2a, 0x00, 0x00});
    append_block(parts.var2, doc_b);
    parts.var2.insert(parts.var2.end(), {0xff, 0xff, 0xff, 0xff});
    parts.var_meta.assign(36, 0);
    put_u32(parts.var_meta, 0, 0xFADFADBAu);
    put_u32(parts.var_meta, 8, 1);
    put_u32(parts.var_meta, 20, static_cast<std::uint32_t>(parts.var2.size()));
    put_u32(parts.var_meta, 24, 1);
    put_u32(parts.var_meta, 28, static_cast<std::uint32_t>(block_b_offset));
    put_u32(parts.var_meta, 32, 0x20400006u);

    Var2Stream strict;
    ParseError strict_error;
    check(!parse_var2(parts.var2, strict, &strict_error),
          "CEdl cache: strict walk refuses the residue");
    check(strict_error.message.find("remain") != std::string::npos,
          "CEdl cache: strict message names the remainder");

    const auto state = parse_or_fail("CEdl", parts);
    check_eq(state.blocks.size(), std::size_t{2}, "CEdl cache: two document blocks");
    check_eq(state.var2.gaps.size(), std::size_t{2}, "CEdl cache: two residue runs");
    check(state.var2.cache_tolerant, "CEdl cache: marked cache-tolerant");
    std::size_t residue = 0;
    for (const auto& gap : state.var2.gaps) {
        residue += gap.bytes.size();
    }
    check_eq(residue, std::size_t{8}, "CEdl cache: eight residue bytes");
    if (state.var2.gaps.size() == 2) {
        check_eq(state.var2.gaps[0].offset,
                 static_cast<std::uint32_t>(block_b_offset - 4),
                 "CEdl cache: first residue offset");
        check_eq(state.var2.gaps[1].bytes.size(), std::size_t{4},
                 "CEdl cache: trailing residue size");
    }
    check_eq(state.embedded_props.size(), std::size_t{2},
             "CEdl cache: two documents decoded");
    if (state.embedded_props.size() == 2) {
        check_eq(state.embedded_props[1].view_name, std::string("&Tracking Gantt"),
                 "CEdl cache: live document is the second");
    }
    check_eq(state.block_refs.size(), std::size_t{2}, "CEdl cache: block refs");
    if (state.block_refs.size() == 2) {
        check_eq(state.block_refs[1].uid, 1u, "CEdl cache: VarMeta names the second block");
    }
    check(has_warning(state, "cumulative document cache"), "CEdl cache: warning posted");
    EncodedParts encoded;
    check(encode_view_state(state, encoded, nullptr), "CEdl cache: encode");
    check_same_parts(encoded, parts, "CEdl cache: no-op round-trip keeps the residue");

    // A VarMeta offset that is not a block start is refused by the documented rule.
    Parts bad_meta = parts;
    put_u32(bad_meta.var_meta, 28, static_cast<std::uint32_t>(block_b_offset + 2));
    ParseError error;
    ViewState refused;
    const PartSet bad_set{bad_meta.fixed_data, bad_meta.fixed_meta, bad_meta.var_meta,
                          bad_meta.var2};
    check(!parse_view_state("CEdl", bad_set, refused, &error),
          "CEdl cache: VarMeta offset that is not a block start refused");
    check(error.message.find("not a 214/CEdl document block start") != std::string::npos,
          "CEdl cache: bad offset message");

    // More than the bounded residue is refused too.
    Parts too_far = build_parts(160, {{"", ""}}, {});
    too_far.var2.clear();
    append_block(too_far.var2, doc_a);
    too_far.var2.insert(too_far.var2.end(), 12, std::uint8_t{0xAA});
    append_block(too_far.var2, doc_b);
    too_far.var_meta.assign(36, 0);
    put_u32(too_far.var_meta, 0, 0xFADFADBAu);
    put_u32(too_far.var_meta, 8, 1);
    put_u32(too_far.var_meta, 20, static_cast<std::uint32_t>(too_far.var2.size()));
    put_u32(too_far.var_meta, 24, 1);
    put_u32(too_far.var_meta, 28,
            static_cast<std::uint32_t>(doc_a.size() + 4 + 12));
    put_u32(too_far.var_meta, 32, 0x20400006u);
    error = ParseError{};
    ViewState refused2;
    const PartSet far_set{too_far.fixed_data, too_far.fixed_meta, too_far.var_meta,
                          too_far.var2};
    check(!parse_view_state("CEdl", far_set, refused2, &error),
          "CEdl cache: residue over the bound refused");
    check(error.message.find("not a Props-document cache walk") != std::string::npos,
          "CEdl cache: over-bound message");

    // The tolerance is CEdl-only: the same stream still fails as another class.
    error = ParseError{};
    ViewState not_edl;
    const PartSet other_set{parts.fixed_data, parts.fixed_meta, parts.var_meta, parts.var2};
    check(!parse_view_state("CGrouping", other_set, not_edl, &error),
          "CEdl cache: tolerance is CEdl-only");
    check(error.message.find("declares") != std::string::npos,
          "CEdl cache: strict error kept for another class");

    // Regenerating the live document inside a cache stream shifts the trailing
    // residue and keeps it verbatim (the block before the gap grows).
    Parts ctable_parts;
    if (!make_ctable_parts("synthetic-viewstate-ctable-base.bin", ctable_parts)) {
        return;
    }
    const Parts view_parts = make_view_parts();
    auto ctable = parse_or_fail("CTable", ctable_parts);
    auto views = parse_or_fail("CV_iew", view_parts);
    auto cache_edl = parse_or_fail("CEdl", parts);
    check(append_table_column(ctable, 0, 0x0b400057u, "Number1", nullptr),
          "CEdl cache: append column");
    WriteError regen_error;
    check(regenerate_edl(cache_edl, ctable, &regen_error),
          "CEdl cache: regenerate the live document");
    EncodedParts regen_out;
    check(encode_view_state(cache_edl, regen_out, nullptr),
          "CEdl cache: encode after regeneration");
    check(regen_out.var2_data.size() > parts.var2.size(),
          "CEdl cache: regenerated stream grows");
    if (regen_out.var2_data.size() > parts.var2.size()) {
        const std::vector<std::uint8_t> tail_residue = {0xff, 0xff, 0xff, 0xff};
        const bool kept =
            std::search(regen_out.var2_data.begin(), regen_out.var2_data.end(),
                        tail_residue.begin(), tail_residue.end()) != regen_out.var2_data.end();
        check(kept, "CEdl cache: trailing residue preserved verbatim");
    }
    Parts regenerated_parts;
    regenerated_parts.fixed_data = regen_out.fixed_data;
    regenerated_parts.fixed_meta = regen_out.fixed_meta;
    regenerated_parts.var_meta = regen_out.var_meta;
    regenerated_parts.var2 = regen_out.var2_data;
    const auto reparsed = parse_or_fail("CEdl", regenerated_parts);
    check_eq(reparsed.blocks.size(), std::size_t{2}, "CEdl cache: re-parse keeps both blocks");
    check_eq(reparsed.var2.gaps.size(), std::size_t{2},
             "CEdl cache: re-parse keeps both residue runs");
    if (reparsed.embedded_props.size() == 2 && !ctable.tables.empty()) {
        const std::size_t expected_ids = ctable.tables[0].columns.size() - 1;
        check_eq(reparsed.embedded_props[1].field_ids.size(), expected_ids,
                 "CEdl cache: live field list regenerated from the table");
        check_eq(reparsed.embedded_props[1].field_ids.back(), 0x0b400057u,
                 "CEdl cache: appended field id is in the live field list");
    }
    if (reparsed.embedded_props.size() == 2 && state.embedded_props.size() == 2) {
        check(reparsed.embedded_props[0].document.bytes ==
                  state.embedded_props[0].document.bytes,
              "CEdl cache: the stale document is left byte-identical");
    }
}

void test_filter_criteria_payload() {
    using namespace msproject::viewstate;
    const std::vector<FilterCriterionSpec> criteria = {{11, 0, 0}, {7, 3, 0x0b40000eu}};
    Parts parts = build_parts(
        230, {{"&All Tasks", "All Tasks"}},
        {{1, 0x20400006u,
          make_filter_block_full(1, 0x100u, criteria, {"tasks", "Enter Task Name Here"})}});
    std::vector<std::uint8_t> criteria_record = {0x00, 0x00};
    const std::vector<std::uint8_t> label = utf16_item("All Tasks");
    criteria_record.insert(criteria_record.end(), label.begin(), label.end());
    attach_row_records(parts, 0, 230,
                       {{0x20400007u, criteria_record},
                        {0x20400009u, utf16_item("All Tasks")}});
    const auto state = parse_or_fail("CFilter", parts);
    check_eq(state.filters.size(), std::size_t{1}, "filter payload: row count");
    if (state.filters.size() == 1) {
        const auto& filter = state.filters[0];
        check(filter.criteria_decoded, "filter payload: criterion framing decoded");
        check_eq(filter.criterion_count, std::uint16_t{2}, "filter payload: criterion count");
        check_eq(filter.block_flags, 0x100u, "filter payload: flags word");
        check_eq(filter.string_table_offset, 92u, "filter payload: string table offset");
        check_eq(filter.string_table.size(), std::size_t{2}, "filter payload: string count");
        if (filter.string_table.size() == 2) {
            check_eq(filter.string_table[0], std::string("tasks"),
                     "filter payload: first string");
            check_eq(filter.string_table[1], std::string("Enter Task Name Here"),
                     "filter payload: prompt string");
        }
        check_eq(filter.applies_to, std::string("tasks"), "filter payload: applies-to");
        check(filter.criteria_record_decoded, "filter payload: ...0007 decoded");
        check_eq(filter.criteria_record_word, std::uint16_t{0},
                 "filter payload: ...0007 word");
        check_eq(filter.criteria_record_label, std::string("All Tasks"),
                 "filter payload: ...0007 label");
    }
    EncodedParts encoded;
    check(encode_view_state(state, encoded, nullptr), "filter payload: encode");
    check_same_parts(encoded, parts, "filter payload: no-op round-trip");

    // A count/offset disagreement keeps the framing opaque but still parses.
    Parts bad = build_parts(
        230, {{"&All Tasks", "All Tasks"}},
        {{1, 0x20400006u, make_filter_block_full(1, 0x100u, criteria, {"tasks"})}});
    put_u16(bad.var2, 4 + 14, 3);
    const auto bad_state = parse_or_fail("CFilter", bad);
    check_eq(bad_state.filters.size(), std::size_t{1}, "filter payload: bad count row");
    if (bad_state.filters.size() == 1) {
        check(!bad_state.filters[0].criteria_decoded,
              "filter payload: mismatched count leaves the framing opaque");
        check_eq(bad_state.filters[0].applies_to, std::string("tasks"),
                 "filter payload: string table still decoded");
    }
    check(has_warning(bad_state, "string-table offset"), "filter payload: mismatch warning");
}

void test_grouping_body_framing() {
    using namespace msproject::viewstate;
    std::vector<std::uint8_t> stored(83, 0);
    put_u32(stored, 0, 11);
    put_u32(stored, 4, 0x100u);
    put_u16(stored, 10, 1);
    put_u32(stored, 12, 0x0b40021fu);
    std::vector<std::uint8_t> empty(12, 0);
    put_u32(empty, 0, 9);
    put_u32(empty, 8, 0x100u);
    Parts parts = build_parts(230,
                              {{"&No Group", "No Task Group"}, {"Status", "Status"}},
                              {{1, 0x20400006u, empty}, {2, 0x20400006u, stored}});
    attach_row_records(parts, 0, 230, {{0x2D400009u, utf16_item("No Task Group")}});
    attach_row_records(parts, 1, 230, {{0x2D400009u, utf16_item("Status")}});
    const auto state = parse_or_fail("CGrouping", parts);
    check_eq(state.groupings.size(), std::size_t{2}, "grouping body: row count");
    if (state.groupings.size() == 2) {
        check(!state.groupings[0].has_body, "grouping body: 12-byte block has no body");
        check_eq(state.groupings[0].block_length, 12u, "grouping body: empty block size");
        check(state.groupings[1].has_body, "grouping body: stored block has a body");
        check_eq(state.groupings[1].block_length, 83u, "grouping body: stored block size");
        check_eq(state.groupings[1].block_flags, 0x100u, "grouping body: flags word");
        check_eq(state.groupings[1].header_word_8, 0u, "grouping body: word at +8");
        check_eq(state.groupings[1].header_word_10, 1u, "grouping body: word at +10");
        check(state.groupings[1].body_field_decoded, "grouping body: field id decoded");
        check_eq(state.groupings[1].body_field_id, 0x0b40021fu,
                 "grouping body: field id value");
    }
    EncodedParts encoded;
    check(encode_view_state(state, encoded, nullptr), "grouping body: encode");
    check_same_parts(encoded, parts, "grouping body: no-op round-trip");
}

void test_drawing_inventory() {
    using namespace msproject::viewstate;
    std::vector<std::uint8_t> body(64, 0x5a);
    Parts drawing = build_parts(230,
                                {{"Drawing 1", ""}, {"Drawing 2", ""}, {"Drawing 3", ""}},
                                {{1, 0x2040000bu, body}});
    const auto state = parse_or_fail("CDrawing", drawing);
    check_eq(state.row_size, 230u, "drawing: row size");
    check_eq(state.rows.size(), std::size_t{3}, "drawing: three rows");
    check_eq(state.blocks.size(), std::size_t{1}, "drawing: one block");
    check_eq(state.block_refs.size(), std::size_t{1}, "drawing: one block ref");
    if (!state.rows.empty()) {
        check(!state.rows[0].definition.decoded,
              "drawing: row is not an inline record set");
    }
    if (!state.block_refs.empty()) {
        check_eq(state.block_refs[0].kind, std::string("opaque"), "drawing: block stays opaque");
    }
    EncodedParts encoded;
    check(encode_view_state(state, encoded, nullptr), "drawing: encode");
    check_same_parts(encoded, drawing, "drawing: no-op round-trip");

    // CMap and CReport are header-only in every measured tree.
    for (const char* klass : {"CMap", "CReport"}) {
        Parts empty;
        empty.fixed_meta.assign(16, 0);
        put_u32(empty.fixed_meta, 0, 0xFADFADBAu);
        put_u32(empty.fixed_meta, 4, 4);
        empty.var_meta.assign(24, 0);
        put_u32(empty.var_meta, 0, 0xFADFADBAu);
        const auto empty_state = parse_or_fail(klass, empty);
        check_eq(empty_state.rows.size(), std::size_t{0}, "empty inventory: no rows");
        check_eq(empty_state.blocks.size(), std::size_t{0}, "empty inventory: no blocks");
        EncodedParts empty_encoded;
        check(encode_view_state(empty_state, empty_encoded, nullptr),
              "empty inventory: encode");
        check_same_parts(empty_encoded, empty, "empty inventory: no-op round-trip");
    }
}

void test_writer() {
    test_writer_roundtrip();
    test_writer_date_format();
    test_writer_column();
    test_writer_refusals();
    test_writer_append();
    test_writer_append_refusals();
    test_writer_apply_edits();
    // Wave 17 (F1)
    test_row_record_sets();
    test_filter_definitions();
    test_grouping_definitions();
    test_edl_typed();
    test_noop_five_classes();
    test_view_name_edit();
    test_view_table_edit();
    test_view_margin_edit();
    test_edl_regenerate();
    test_regenerate_view_state();
    // Wave 18 (F2)
    test_edl_cache_residue();
    test_filter_criteria_payload();
    test_grouping_body_framing();
    test_drawing_inventory();
}

}  // namespace

int main(int argc, char** argv) {
    // An optional argument selects one case, which makes a crash easy to bisect.
    const std::string only = argc > 1 ? argv[1] : std::string();
    const auto run = [&only](const char* name, void (*function)()) {
        if (only.empty() || only == name) {
            function();
        }
    };
    run("real", test_real_ctable_blobs);
    run("ctable", test_generated_ctable);
    run("views", test_generated_views);
    run("views-main", test_view_main_decode);
    run("edl", test_ceDL_props_document);
    run("empty", test_empty_class);
    run("errors", test_errors_and_warnings);
    run("text", test_text_helpers);
    run("gantt-style", test_gantt_style);
    run("writer-roundtrip", test_writer_roundtrip);
    run("writer-date-format", test_writer_date_format);
    run("writer-column", test_writer_column);
    run("writer-refusals", test_writer_refusals);
    run("writer-append", test_writer_append);
    run("writer-append-refusals", test_writer_append_refusals);
    run("writer-apply-edits", test_writer_apply_edits);
    // Wave 17 (F1)
    run("row-records", test_row_record_sets);
    run("filters", test_filter_definitions);
    run("groupings", test_grouping_definitions);
    run("edl-typed", test_edl_typed);
    run("noop-five", test_noop_five_classes);
    run("view-name", test_view_name_edit);
    run("view-table", test_view_table_edit);
    run("view-margins", test_view_margin_edit);
    run("edl-regenerate", test_edl_regenerate);
    run("regenerate", test_regenerate_view_state);
    // Wave 18 (F2)
    run("edl-cache", test_edl_cache_residue);
    run("filter-payload", test_filter_criteria_payload);
    run("grouping-payload", test_grouping_body_framing);
    run("drawing-inventory", test_drawing_inventory);
    // The group name is for the separate ctest entries; the default run above
    // already covers every case individually.
    if (only == "writer") {
        test_writer();
    }

    if (g_failures == 0) {
        std::printf("PASS: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    std::fprintf(stderr, "FAIL: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
}
