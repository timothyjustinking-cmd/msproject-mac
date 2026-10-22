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

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
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
    run("edl", test_ceDL_props_document);
    run("empty", test_empty_class);
    run("errors", test_errors_and_warnings);
    run("text", test_text_helpers);

    if (g_failures == 0) {
        std::printf("PASS: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    std::fprintf(stderr, "FAIL: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
}
