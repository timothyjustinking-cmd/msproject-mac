// props_tests -- core/props decoder tests.
//
// The always-on fixture is tests/data/synthetic-props.bin, a raw 114/Props
// stream extracted from the synthetic oracle project w10-formulas.mpp.  It has
// no client task/calendar names; its only text is the synthetic project name,
// the generic TBknd... table-name list and the six oracle formulas.
//
// corpus/ is never required.  If PROPS_CORPUS_BLOB names an extracted Props
// stream and MPP_NO_CORPUS is not set, that blob is parsed as an extra smoke
// case; otherwise the case is skipped.

#include "props.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
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

std::vector<std::uint8_t> read_file(const char* path) {
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

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void test_fixture(const std::vector<std::uint8_t>& fixture) {
    check_eq(fixture.size(), std::size_t{89266}, "fixture size");

    msproject::props::PropsDocument document;
    msproject::props::ParseError error;
    check(msproject::props::parse_props(fixture, document, &error), "parse synthetic Props");
    if (g_failures != 0) {
        std::fprintf(stderr, "parse error: %s at %zu\n", error.message.c_str(), error.offset);
        return;
    }

    check_eq(document.size(), std::uint32_t{89266}, "document size");
    check_eq(document.header.size_word, std::uint32_t{89262}, "header size word");
    check_eq(document.header.size_repeat, std::uint32_t{89262}, "header size repeat");
    check_eq(document.header.format_version, std::uint32_t{0x00010000u}, "format version");
    check_eq(document.header.declared_entries, std::uint32_t{262}, "declared entries");
    check_eq(document.records.size(), std::size_t{161}, "top-level record count");
    check_eq(document.tail_offset, std::uint32_t{3858}, "tail offset");
    check_eq(document.tail_size(), std::uint32_t{85408}, "tail size");

    check_eq(document.records.front().offset, std::uint32_t{16}, "first record offset");
    check_eq(document.records.front().payload_length, std::uint32_t{4}, "first record length");
    check_eq(document.records.front().id, std::uint32_t{0x35400016u}, "first record id");
    check_eq(document.records.front().kind, std::uint32_t{4}, "first record kind");
    check_eq(document.records[1].offset, std::uint32_t{32}, "second record offset");
    check_eq(document.records[1].id, std::uint32_t{0x024003e8u}, "second record id");
    check_eq(document.records[1].payload_length, std::uint32_t{316}, "second record length");
    const std::string table_preview =
        msproject::props::utf16_preview(document.payload(document.records[1]));
    check(table_preview.rfind("TBkndTask,TBkndRsc", 0) == 0,
          "table-name list preview");

    const auto* name_record = document.find(0x02400008u);
    check(name_record != nullptr, "project-name record present");
    if (name_record != nullptr) {
        check_eq(msproject::props::utf16_preview(document.payload(*name_record)),
                 std::string("w10-formulas"), "project-name preview");
        check_eq(name_record->payload_length, std::uint32_t{28}, "project-name payload length");
    }

    const auto* formula_record = document.find(0x02401390u);
    check(formula_record != nullptr, "formula-table record present");
    if (formula_record != nullptr) {
        check_eq(formula_record->kind, std::uint32_t{0}, "formula record kind");
        check_eq(formula_record->payload_length, std::uint32_t{568},
                 "formula record payload length");
        check(msproject::props::utf16_preview(document.payload(*formula_record))
                  .rfind("hex:", 0) == 0,
              "formula record preview is binary");
    }

    check_eq(document.records.back().offset, std::uint32_t{3844}, "last record offset");
    check_eq(document.records.back().id, std::uint32_t{0x024013fbu}, "last record id");
    check_eq(document.records.back().kind, std::uint32_t{2}, "last record kind");

    int kind_blob = 0;
    int kind_int16 = 0;
    int kind_int32 = 0;
    int kind_int64 = 0;
    for (const auto& record : document.records) {
        if (record.kind == 0) {
            ++kind_blob;
        } else if (record.kind == 2) {
            ++kind_int16;
        } else if (record.kind == 4) {
            ++kind_int32;
        } else if (record.kind == 9) {
            ++kind_int64;
        }
    }
    check_eq(kind_blob, 20, "blob record count");
    check_eq(kind_int16, 90, "int16 record count");
    check_eq(kind_int32, 49, "int32 record count");
    check_eq(kind_int64, 2, "int64 record count");

    for (const auto& record : document.records) {
        check(record.offset + 12u + record.payload_length <= document.bytes.size(),
              "record stays inside the stream");
        check(msproject::props::is_props_id(record.id), "record id carries the 0x40 byte");
    }
    check_eq(msproject::props::kind_name(0), std::string("blob"), "kind name 0");
    check_eq(msproject::props::kind_name(2), std::string("int16"), "kind name 2");
    check_eq(msproject::props::kind_name(4), std::string("int32"), "kind name 4");
    check_eq(msproject::props::kind_name(9), std::string("int64"), "kind name 9");
    check_eq(msproject::props::kind_name(77), std::string("unknown"), "kind name unknown");

    check(msproject::props::is_props_id(0x02400008u), "is_props_id 0x0240");
    check(msproject::props::is_props_id(0x0b40000eu), "is_props_id 0x0b40");
    check(!msproject::props::is_props_id(0x00000008u), "is_props_id rejects 0");

    const std::string dump = msproject::props::dump_document(document, 3);
    check(dump.find("Props stream: 89266 bytes") != std::string::npos, "dump header");
    check(dump.find("tail: offset=3858 length=85408") != std::string::npos, "dump tail");
    check(dump.find("id=0x35400016") != std::string::npos, "dump record id");
}

void test_formulas(const std::vector<std::uint8_t>& fixture) {
    msproject::props::PropsDocument document;
    msproject::props::ParseError error;
    check(msproject::props::parse_props(fixture, document, &error), "parse for formulas");
    const auto formulas = msproject::props::extract_formulas(document);
    check_eq(formulas.size(), std::size_t{6}, "six formulas");

    if (formulas.size() != 6) {
        return;
    }

    struct Expected {
        std::uint32_t field;
        std::uint32_t block;
        std::uint32_t text;
        std::uint32_t format;
        bool trailer;
        std::uint32_t trailer_value;
        const char* formula;
    };
    const Expected expected[] = {
        {0x0b400033u, 2786u, 2822u, 0x74u, false, 0u,
         "[MSPJ188743694] & \"-\" & [MSPJ188744967]"},
        {0x0b400048u, 2902u, 2938u, 0x5cu, false, 0u,
         "IIf([MSPJ188743712]>50,1,0)"},
        {0x0b400057u, 2994u, 3030u, 0x4cu, true, 0x67u, "[MSPJ188744967]*2"},
        {0x0b400058u, 3070u, 3106u, 0x4cu, false, 0u, "[MSPJ188744967]/480"},
        {0x0b400067u, 3146u, 3182u, 0x4cu, true, 0x72u, "[MSPJ188744967]/2"},
        {0x0b400109u, 3222u, 3258u, 0x4cu, true, 0x30u, "[MSPJ188744965]+7"},
    };

    for (std::size_t i = 0; i < formulas.size(); ++i) {
        const auto& formula = formulas[i];
        const auto& want = expected[i];
        std::ostringstream label;
        label << "formula " << i;
        check_eq(formula.field_id, want.field, label.str() + " field id");
        check_eq(formula.record_id, std::uint32_t{0x02401390u}, label.str() + " record id");
        check_eq(formula.block_offset, want.block, label.str() + " block offset");
        check_eq(formula.text_offset, want.text, label.str() + " text offset");
        check_eq(formula.formula, std::string(want.formula), label.str() + " text");
        for (std::size_t word = 0; word < formula.format_words.size(); ++word) {
            check_eq(formula.format_words[word], want.format,
                     label.str() + " format word " + std::to_string(word));
        }
        check_eq(formula.has_trailer, want.trailer, label.str() + " trailer flag");
        if (formula.has_trailer && want.trailer) {
            check_eq(formula.trailer, want.trailer_value, label.str() + " trailer value");
        }
    }

    // The oracle source formula `[Name] & "-" & [Duration]` is stored rewritten.
    check_eq(formulas[0].formula, std::string("[MSPJ188743694] & \"-\" & [MSPJ188744967]"),
             "oracle Name-Duration formula");
    check_eq(formulas[1].formula, std::string("IIf([MSPJ188743712]>50,1,0)"),
             "oracle percent-complete IIf formula");

    // A corrupted first block must be skipped, not guessed.
    std::vector<std::uint8_t> damaged = fixture;
    put_u32(damaged, 2786, 14u);
    msproject::props::PropsDocument damaged_document;
    check(msproject::props::parse_props(damaged, damaged_document, &error),
          "parse damaged formula fixture");
    const auto damaged_formulas = msproject::props::extract_formulas(damaged_document);
    check_eq(damaged_formulas.size(), std::size_t{5}, "damaged block skipped");
    if (!damaged_formulas.empty()) {
        check_eq(damaged_formulas.front().field_id, std::uint32_t{0x0b400048u},
                 "damaged block leaves next formula first");
    }
}

void test_mspj() {
    std::uint32_t field = 0;
    check(msproject::props::parse_mspj_field("[MSPJ188744967]", field), "parse MSPJ Duration");
    check_eq(field, std::uint32_t{188744967u}, "MSPJ Duration value");
    check_eq(field, std::uint32_t{0x0b400507u}, "MSPJ Duration is a 0x0b40 id");
    check(msproject::props::parse_mspj_field("MSPJ188743694", field), "parse bare MSPJ Name");
    check_eq(field, std::uint32_t{0x0b40000eu}, "MSPJ Name value");
    check(msproject::props::parse_mspj_field("[MSPJ188743712]", field), "parse percent complete");
    check_eq(field, std::uint32_t{0x0b400020u}, "MSPJ percent complete value");
    check(msproject::props::parse_mspj_field("[MSPJ188744965]", field), "parse start");
    check_eq(field, std::uint32_t{0x0b400505u}, "MSPJ start value");

    check(!msproject::props::parse_mspj_field("[MSPJ]", field), "reject empty MSPJ");
    check(!msproject::props::parse_mspj_field("[MSPK188744967]", field), "reject wrong token");
    check(!msproject::props::parse_mspj_field("[MSPJ12x]", field), "reject non-digit");
    check(!msproject::props::parse_mspj_field("", field), "reject empty token");

    check_eq(msproject::props::formula_with_field_ids("[MSPJ188744967]*2"),
             std::string("[0x0b400507]*2"), "rewrite MSPJ to field id");
    check_eq(msproject::props::formula_with_field_ids("IIf([MSPJ188743712]>50,1,0)"),
             std::string("IIf([0x0b400020]>50,1,0)"), "rewrite inside IIf");
    check_eq(msproject::props::formula_with_field_ids("plain text"), std::string("plain text"),
             "rewrite leaves plain text");
}

void test_tail(const std::vector<std::uint8_t>& fixture) {
    msproject::props::PropsDocument document;
    msproject::props::ParseError error;
    check(msproject::props::parse_props(fixture, document, &error), "parse for tail");

    const auto properties = msproject::props::tail_properties(document);
    check_eq(properties.size(), std::size_t{6}, "tail typed property count");
    if (properties.size() == 6) {
        check_eq(properties[0].key, std::uint32_t{0x01000001u}, "tail key 0");
        check_eq(properties[0].value, std::uint32_t{7}, "tail value 0");
        check_eq(properties[1].key, std::uint32_t{0x02000001u}, "tail key 1");
        check_eq(properties[1].value, std::uint32_t{0}, "tail value 1");
        check_eq(properties[2].key, std::uint32_t{0x00800001u}, "tail key 2");
        check_eq(properties[3].key, std::uint32_t{0x00010001u}, "tail key 3");
        check_eq(properties[3].value, std::uint32_t{0x00000220u}, "tail value 3");
        check_eq(properties[4].key, std::uint32_t{0x04000001u}, "tail key 4");
        check_eq(properties[4].value, std::uint32_t{0x00000131u}, "tail value 4");
        check_eq(properties[5].key, std::uint32_t{0x00400001u}, "tail key 5");
        check_eq(properties[5].value, std::uint32_t{0x000000cau}, "tail value 5");
        check_eq(properties[0].offset - document.tail_offset, std::uint32_t{0},
                 "tail property 0 offset");
        check_eq(properties[5].offset - document.tail_offset, std::uint32_t{80},
                 "tail property 5 offset");
    }

    const auto references = msproject::props::field_references(document);
    check_eq(references.size(), std::size_t{1312}, "field-reference count");
    if (!references.empty()) {
        check_eq(references.front().entry_offset - document.tail_offset,
                 std::uint32_t{112}, "first field entry relative offset");
        check_eq(references.front().field_id, std::uint32_t{0x0b400017u},
                 "first field-reference id");
        check_eq(references.front().index_word, std::uint32_t{0}, "first field index word");
        check_eq(references.front().length_word, std::uint32_t{10}, "first field length word");
        check_eq(references.front().words[0], std::uint32_t{0x20u},
                 "first field word 3");
        check_eq(references.front().words[1], std::uint32_t{0x00040003u},
                 "first field word 4");
        check_eq(references.back().field_id, std::uint32_t{0x0b40059du},
                 "last field-reference id");
    }

    int stride_28 = 0;
    int other_stride = 0;
    for (std::size_t i = 1; i < references.size(); ++i) {
        const std::uint32_t delta =
            references[i].entry_offset - references[i - 1].entry_offset;
        if (delta == 28u) {
            ++stride_28;
        } else {
            ++other_stride;
        }
    }
    check_eq(stride_28, 1310, "28-byte field-entry strides");
    check_eq(other_stride, 1, "one variable field-entry stride");
    if (references.size() > 1) {
        bool found_40 = false;
        for (std::size_t i = 1; i < references.size(); ++i) {
            if (references[i].entry_offset - references[i - 1].entry_offset == 40u) {
                found_40 = true;
            }
        }
        check(found_40, "measured 40-byte gap present");
    }

    const std::string summary = msproject::props::describe_tail(document);
    check(summary.find("tail offset=3858 length=85408") != std::string::npos,
          "tail description header");
    check(summary.find("typed properties: 6") != std::string::npos,
          "tail description property count");
    check(summary.find("field-reference entries: 1312") != std::string::npos,
          "tail description reference count");
}

void test_generic_layout() {
    // A minimal stream with the same header + record layout, exercising the
    // 214/Props and Props14 route without any extra fixture.
    std::vector<std::uint8_t> bytes(50, 0);
    put_u32(bytes, 0, 46u);
    put_u32(bytes, 4, 46u);
    put_u32(bytes, 8, 0x00010000u);
    put_u32(bytes, 12, 2u);
    put_u32(bytes, 16, 4u);
    put_u32(bytes, 20, 0x35400010u);
    put_u32(bytes, 24, 4u);
    put_u32(bytes, 28, 0x11223344u);
    put_u32(bytes, 32, 6u);
    put_u32(bytes, 36, 0x02400008u);
    put_u32(bytes, 40, 0u);
    bytes[44] = 'H';
    bytes[45] = 0;
    bytes[46] = 'i';
    bytes[47] = 0;
    bytes[48] = 0;
    bytes[49] = 0;

    msproject::props::PropsDocument document;
    msproject::props::ParseError error;
    check(msproject::props::parse_props(bytes, document, &error), "parse generic Props");
    check_eq(document.records.size(), std::size_t{2}, "generic record count");
    check_eq(document.tail_offset, std::uint32_t{50}, "generic tail offset");
    check_eq(document.tail_size(), std::uint32_t{0}, "generic tail size");
    if (document.records.size() == 2) {
        check_eq(msproject::props::utf16_preview(document.payload(document.records[1])),
                 std::string("Hi"), "generic UTF-16 preview");
    }
    check_eq(msproject::props::tail_properties(document).size(), std::size_t{0},
             "generic tail has no typed properties");
    check_eq(msproject::props::field_references(document).size(), std::size_t{0},
             "generic tail has no field references");

    // Props14 stores a size word that is NOT stream_size - 4: measured deltas
    // were 32, 56 and 80 bytes on the probe and corpus streams.  The record
    // layout is still the same, so a matching-but-different size word parses.
    std::vector<std::uint8_t> props14_style = bytes;
    put_u32(props14_style, 0, 18u);
    put_u32(props14_style, 4, 18u);
    check(msproject::props::parse_props(props14_style, document, &error),
          "parse Props14-style size word");
    check_eq(document.header.size_word, std::uint32_t{18}, "Props14-style size word");
    check_eq(document.records.size(), std::size_t{2}, "Props14-style record count");
}

void test_malformed(const std::vector<std::uint8_t>& fixture) {
    msproject::props::PropsDocument document;
    msproject::props::ParseError error;

    check(!msproject::props::parse_props(std::vector<std::uint8_t>{}, document, &error),
          "empty stream refused");
    check(!error.message.empty(), "empty stream has a message");

    std::vector<std::uint8_t> short_header(16, 0);
    put_u32(short_header, 0, 12u);
    put_u32(short_header, 4, 12u);
    put_u32(short_header, 8, 0x00010000u);
    put_u32(short_header, 12, 0u);
    check(!msproject::props::parse_props(short_header, document, &error),
          "header-only stream refused");

    std::vector<std::uint8_t> bad_size = fixture;
    put_u32(bad_size, 0, 0u);
    check(!msproject::props::parse_props(bad_size, document, &error),
          "bad size word refused");

    std::vector<std::uint8_t> truncated(fixture.begin(), fixture.begin() + 5000);
    check(!msproject::props::parse_props(truncated, document, &error),
          "truncated stream refused");

    std::vector<std::uint8_t> bad_first_id = fixture;
    put_u32(bad_first_id, 20, 0u);
    check(!msproject::props::parse_props(bad_first_id, document, &error),
          "invalid first id refused");

    std::vector<std::uint8_t> bad_middle_id = fixture;
    put_u32(bad_middle_id, 360 + 4, 0u);
    check(msproject::props::parse_props(bad_middle_id, document, &error),
          "invalid middle id ends the record list");
    check_eq(document.records.size(), std::size_t{2},
             "records before the invalid id survive");
    check_eq(document.tail_offset, std::uint32_t{360},
             "invalid id becomes the opaque tail");

    std::vector<std::uint8_t> unknown_kind = fixture;
    put_u32(unknown_kind, 16 + 8, 7u);
    check(msproject::props::parse_props(unknown_kind, document, &error),
          "unknown kind is preserved");
    if (!document.records.empty()) {
        check_eq(document.records.front().kind, std::uint32_t{7}, "unknown kind value");
        check_eq(msproject::props::kind_name(7), std::string("unknown"),
                 "unknown kind name");
    }
}

std::string env_value(const char* name) {
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

void test_optional_corpus_blob() {
    const std::string path = env_value("PROPS_CORPUS_BLOB");
    const std::string no_corpus = env_value("MPP_NO_CORPUS");
    if (path.empty() || !no_corpus.empty()) {
        std::printf("SKIP: corpus Props blob (set PROPS_CORPUS_BLOB to enable)\n");
        return;
    }
    const auto bytes = read_file(path.c_str());
    if (bytes.empty()) {
        std::printf("SKIP: corpus Props blob could not be read\n");
        return;
    }
    msproject::props::PropsDocument document;
    msproject::props::ParseError error;
    check(msproject::props::parse_props(bytes, document, &error),
          "parse optional corpus Props blob");
    check(!document.records.empty(), "optional corpus Props blob has records");
    std::printf("corpus Props blob: %zu bytes, %zu records, tail %u\n", bytes.size(),
                document.records.size(), document.tail_size());
}

}  // namespace

int main() {
    const auto fixture = read_file(PROPS_TEST_DATA);
    if (fixture.empty()) {
        std::fprintf(stderr, "FAIL: cannot read %s\n", PROPS_TEST_DATA);
        return 1;
    }

    test_fixture(fixture);
    test_formulas(fixture);
    test_mspj();
    test_tail(fixture);
    test_generic_layout();
    test_malformed(fixture);
    test_optional_corpus_blob();

    if (g_failures != 0) {
        std::fprintf(stderr, "FAIL: %d checks, %d failures\n", g_checks, g_failures);
        return 1;
    }
    std::printf("PASS: %d checks, 0 failures\n", g_checks);
    return 0;
}
