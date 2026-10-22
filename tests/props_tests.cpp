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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef PROPS_HAVE_MPP_CFB
#include "cfb.h"
#endif

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

std::uint32_t get_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

std::uint32_t get_u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u);
}

std::vector<std::uint8_t> formula_payload(
    const msproject::props::PropsDocument& document) {
    const auto* record = document.find(msproject::props::kFormulaRecordId);
    if (record == nullptr) {
        return {};
    }
    const auto bytes = document.payload(*record);
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

// True when `after` differs from `before` only in the formula record payload
// and the 2-byte custom-field flag payload (plus the header size words).
// Everything else, including the opaque tail, must be identical.
bool only_formula_and_flag_changed(const std::vector<std::uint8_t>& before,
                                   const msproject::props::PropsDocument& before_document,
                                   const std::vector<std::uint8_t>& after,
                                   const msproject::props::PropsDocument& after_document,
                                   std::size_t new_payload_length,
                                   std::uint16_t expected_flag) {
    if (before_document.records.size() != after_document.records.size()) {
        return false;
    }
    for (std::size_t i = 0; i < before_document.records.size(); ++i) {
        const auto& old_record = before_document.records[i];
        const auto& new_record = after_document.records[i];
        if (old_record.id != new_record.id || old_record.kind != new_record.kind) {
            return false;
        }
        if (old_record.id == msproject::props::kFormulaRecordId) {
            if (new_record.payload_length != new_payload_length) {
                return false;
            }
            continue;
        }
        const std::size_t old_size =
            12u + static_cast<std::size_t>(old_record.payload_length);
        if (old_record.id == msproject::props::kCustomFieldFlagRecordId) {
            if (old_record.payload_length != 2u || new_record.payload_length != 2u) {
                return false;
            }
            if (!std::equal(
                    before.begin() + static_cast<std::ptrdiff_t>(old_record.offset),
                    before.begin() +
                        static_cast<std::ptrdiff_t>(old_record.offset + 12u),
                    after.begin() + static_cast<std::ptrdiff_t>(new_record.offset))) {
                return false;
            }
            if (get_u16(after, new_record.offset + 12u) != expected_flag) {
                return false;
            }
            continue;
        }
        if (old_record.payload_length != new_record.payload_length ||
            !std::equal(
                before.begin() + static_cast<std::ptrdiff_t>(old_record.offset),
                before.begin() +
                    static_cast<std::ptrdiff_t>(old_record.offset + old_size),
                after.begin() + static_cast<std::ptrdiff_t>(new_record.offset))) {
            return false;
        }
    }
    return std::equal(
        before.begin() + static_cast<std::ptrdiff_t>(before_document.tail_offset),
        before.end(),
        after.begin() + static_cast<std::ptrdiff_t>(after_document.tail_offset));
}

std::uint32_t flag_value(const msproject::props::PropsDocument& document) {
    const auto* record = document.find(msproject::props::kCustomFieldFlagRecordId);
    if (record == nullptr || record->payload_length != 2u) {
        return 0xffffffffu;
    }
    const auto payload = document.payload(*record);
    return static_cast<std::uint32_t>(payload[0]) |
           (static_cast<std::uint32_t>(payload[1]) << 8u);
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

// ---------------------------------------------------------------------------
// W59 writer: the inverse of extract_formulas(), against the committed
// synthetic-props.bin (six real formulas; trailers must be copied, not made).
// ---------------------------------------------------------------------------
void test_writer_committed(const std::vector<std::uint8_t>& fixture) {
    using msproject::props::PropsDocument;
    using msproject::props::ParseError;

    PropsDocument document;
    ParseError parse_error;
    std::string writer_error;
    check(msproject::props::parse_props(fixture, document, &parse_error), "writer: parse fixture");
    const auto before = msproject::props::extract_formulas(document);
    check_eq(before.size(), std::size_t{6}, "writer: six committed formulas");
    const auto* record = document.find(msproject::props::kFormulaRecordId);
    check(record != nullptr, "writer: committed formula record present");
    if (record == nullptr || before.size() != 6) {
        return;
    }
    check_eq(record->payload_length, std::uint32_t{568},
             "writer: committed formula payload length");
    const auto payload = formula_payload(document);
    check_eq(payload.size(), std::size_t{568}, "writer: copied formula payload size");

    // A no-op re-encode must be byte-identical: header, record lengths and tail
    // all stay put.
    std::vector<std::uint8_t> noop;
    check(msproject::props::replace_formula_record(fixture, payload, noop, &writer_error),
          "writer: no-op replace_formula_record succeeds");
    check(noop == fixture, "writer: no-op re-encode is byte-identical");

    // Setting a seventh formula must copy the six existing blocks verbatim,
    // including their measured trailers (0x67, 0x72, 0x30).
    std::vector<std::uint8_t> with_text2;
    check(msproject::props::set_custom_field_formula(fixture, 0x0b400036u, "[Name]",
                                                     with_text2, &writer_error),
          "writer: set Text2 on committed fixture");
    if (with_text2.empty()) {
        return;
    }
    check_eq(with_text2.size(), fixture.size() + 76u,
             "writer: committed set grows by new block plus one table entry");
    PropsDocument changed;
    check(msproject::props::parse_props(with_text2, changed, &parse_error),
          "writer: parse committed fixture after set");
    check_eq(flag_value(document), std::uint32_t{1u},
             "writer: committed fixture custom-field flag is 1");
    check_eq(flag_value(changed), std::uint32_t{1u},
             "writer: set leaves the custom-field flag at 1");
    check(only_formula_and_flag_changed(fixture, document, with_text2, changed, 644u, 1u),
          "writer: committed set changes only the formula and flag records");
    const auto after = msproject::props::extract_formulas(changed);
    check_eq(after.size(), std::size_t{7}, "writer: seven formulas after committed set");
    if (after.size() == 7) {
        const std::uint32_t order[7] = {0x0b400033u, 0x0b400036u, 0x0b400048u,
                                        0x0b400057u, 0x0b400058u, 0x0b400067u,
                                        0x0b400109u};
        bool ordered = true;
        for (std::size_t i = 0; i < 7; ++i) {
            if (after[i].field_id != order[i]) {
                ordered = false;
            }
        }
        check(ordered, "writer: committed table stays in ascending field-id order");
        for (const auto& original : before) {
            const msproject::props::FormulaBlock* found = nullptr;
            for (const auto& entry : after) {
                if (entry.field_id == original.field_id) {
                    found = &entry;
                }
            }
            check(found != nullptr, "writer: original formula still present");
            if (found == nullptr) {
                continue;
            }
            check_eq(found->formula, original.formula,
                     "writer: original formula text copied");
            for (std::size_t word = 0; word < found->format_words.size(); ++word) {
                check_eq(found->format_words[word], original.format_words[word],
                         "writer: original 7 format words copied");
            }
            check_eq(found->has_trailer, original.has_trailer,
                     "writer: original trailer presence copied");
            if (found->has_trailer && original.has_trailer) {
                check_eq(found->trailer, original.trailer,
                         "writer: original trailer value copied");
            }
        }
    }
    // Replacing the same field with the same text is idempotent.
    std::vector<std::uint8_t> again;
    check(msproject::props::set_custom_field_formula(with_text2, 0x0b400036u, "[Name]",
                                                     again, &writer_error),
          "writer: re-set same field succeeds");
    check(again == with_text2, "writer: re-set same field is byte-identical");

    // Clear removes the whole table, shrinking by the 560 payload bytes and
    // shifting the tail back without rewriting it.
    std::vector<std::uint8_t> cleared;
    check(msproject::props::clear_custom_field_formula(fixture, cleared, &writer_error),
          "writer: clear committed fixture");
    check_eq(cleared.size(), fixture.size() - 560u, "writer: clear restores stream size");
    PropsDocument cleared_document;
    check(msproject::props::parse_props(cleared, cleared_document, &parse_error),
          "writer: parse committed fixture after clear");
    check_eq(flag_value(cleared_document), std::uint32_t{0u},
             "writer: clear drops the custom-field flag to 0");
    check(only_formula_and_flag_changed(fixture, document, cleared, cleared_document, 8u,
                                        0u),
          "writer: clear changes only the formula and flag records");
    const auto* cleared_record = cleared_document.find(msproject::props::kFormulaRecordId);
    check(cleared_record != nullptr, "writer: cleared formula record still present");
    if (cleared_record != nullptr) {
        check_eq(cleared_record->payload_length, std::uint32_t{8},
                 "writer: cleared payload is the 8-byte empty table");
    }
    check_eq(msproject::props::extract_formulas(cleared_document).size(), std::size_t{0},
             "writer: cleared fixture has no formulas");
    check_eq(cleared_document.header.size_word,
             static_cast<std::uint32_t>(cleared.size() - 4u),
             "writer: clear fixes the header size word");
    std::vector<std::uint8_t> cleared_again;
    check(msproject::props::clear_custom_field_formula(cleared, cleared_again, &writer_error),
          "writer: clearing an empty table succeeds");
    check(cleared_again == cleared, "writer: clearing an empty table is byte-identical");
}

// ---------------------------------------------------------------------------
// W59 writer refusals: every one must leave `out` untouched and set a message.
// ---------------------------------------------------------------------------
void test_writer_refusals(const std::vector<std::uint8_t>& fixture) {
    using msproject::props::PropsDocument;
    using msproject::props::ParseError;

    PropsDocument document;
    ParseError error;
    check(msproject::props::parse_props(fixture, document, &error),
          "writer refusals: parse fixture");
    const auto* record = document.find(msproject::props::kFormulaRecordId);
    check(record != nullptr, "writer refusals: formula record present");
    if (record == nullptr) {
        return;
    }
    const std::size_t record_offset = record->offset;
    const std::vector<std::uint8_t> sentinel(9, 0xabu);

    auto refused = [&](bool ok, const std::vector<std::uint8_t>& out_value,
                       const std::string& message, const char* what) {
        check(!ok, std::string("writer refusal: ") + what);
        check(out_value == sentinel, std::string("writer refusal leaves output untouched: ") + what);
        check(!message.empty(), std::string("writer refusal has a message: ") + what);
    };

    std::vector<std::uint8_t> out;
    std::string message;

    out = sentinel;
    message.clear();
    refused(msproject::props::set_custom_field_formula(fixture, 0u, "[Name]", out, &message),
            out, message, "zero field id");

    out = sentinel;
    message.clear();
    refused(msproject::props::set_custom_field_formula(fixture, 0x0b40eeeeu, "[Name]", out,
                                                       &message),
            out, message, "unknown field id");

    out = sentinel;
    message.clear();
    refused(msproject::props::set_custom_field_formula(fixture, 0x0b40000eu, "[Name]", out,
                                                       &message),
            out, message, "known but non-custom field id");

    out = sentinel;
    message.clear();
    refused(msproject::props::set_custom_field_formula(
                fixture, 0x0b400036u, "IIf([Duration]>50,1,0", out, &message),
            out, message, "formula the project parser rejects");

    out = sentinel;
    message.clear();
    refused(msproject::props::set_custom_field_formula(fixture, 0x0b400036u,
                                                       "[NoSuchField]+1", out, &message),
            out, message, "formula with an unknown field reference");

    // Valid to the parser, but extract_formulas() gates on the MSPJ token.  A
    // constant formula next to a formula that HAS a field reference is readable
    // (the gate is per record), but a lone constant formula on an otherwise
    // empty table is not: the writer refuses rather than write a formula the
    // decoder cannot see.
    std::vector<std::uint8_t> empty_table;
    check(msproject::props::clear_custom_field_formula(fixture, empty_table, &message),
          "writer refusals: clear fixture for the constant-formula case");
    PropsDocument empty_document;
    ParseError empty_error;
    check(msproject::props::parse_props(empty_table, empty_document, &empty_error),
          "writer refusals: parse the cleared fixture");
    check_eq(msproject::props::extract_formulas(empty_document).size(), std::size_t{0},
             "writer refusals: empty table reads back no formulas");
    out = sentinel;
    message.clear();
    refused(msproject::props::set_custom_field_formula(empty_table, 0x0b400036u, "42", out,
                                                       &message),
            out, message, "lone formula with no field reference");

    std::vector<std::uint8_t> damaged = fixture;
    put_u32(damaged, record_offset + 12u, 7u);  // inner_size != payload size
    out = sentinel;
    message.clear();
    refused(msproject::props::set_custom_field_formula(damaged, 0x0b400036u, "[Name]", out,
                                                       &message),
            out, message, "formula payload with the wrong inner size word");

    damaged = fixture;
    put_u32(damaged, record_offset + 12u + 8u + 4u, 58u);  // first block at 58, not 56
    out = sentinel;
    message.clear();
    refused(msproject::props::set_custom_field_formula(damaged, 0x0b400036u, "[Name]", out,
                                                       &message),
            out, message, "formula table whose first block offset is wrong");

    damaged = fixture;
    put_u32(damaged, record_offset + 12u + 56u, 14u);  // bad block header
    out = sentinel;
    message.clear();
    refused(msproject::props::clear_custom_field_formula(damaged, out, &message), out,
            message, "formula block without the 13 header");

    damaged = fixture;
    put_u32(damaged, record_offset + 8u, 4u);  // formula record kind 4
    out = sentinel;
    message.clear();
    refused(msproject::props::set_custom_field_formula(damaged, 0x0b400036u, "[Name]", out,
                                                       &message),
            out, message, "formula record with an unexpected kind");

    damaged = fixture;
    put_u32(damaged, record_offset + 4u, 0x02401391u);  // rename the formula record
    out = sentinel;
    message.clear();
    refused(msproject::props::clear_custom_field_formula(damaged, out, &message), out,
            message, "stream without a formula record");

    const auto* flag_record = document.find(msproject::props::kCustomFieldFlagRecordId);
    check(flag_record != nullptr, "writer refusals: custom-field flag record present");
    if (flag_record != nullptr) {
        damaged = fixture;
        put_u32(damaged, flag_record->offset + 8u, 4u);  // flag record kind 4
        out = sentinel;
        message.clear();
        refused(msproject::props::set_custom_field_formula(damaged, 0x0b400036u, "[Name]",
                                                           out, &message),
                out, message, "custom-field flag record with an unexpected kind");

        damaged = fixture;
        put_u32(damaged, flag_record->offset + 4u, 0x02401393u);  // rename the flag record
        out = sentinel;
        message.clear();
        refused(msproject::props::set_custom_field_formula(damaged, 0x0b400036u, "[Name]",
                                                           out, &message),
                out, message, "stream without a custom-field flag record");
    }

    std::vector<std::uint8_t> truncated(fixture.begin(), fixture.begin() + 5000);
    out = sentinel;
    message.clear();
    refused(msproject::props::set_custom_field_formula(truncated, 0x0b400036u, "[Name]",
                                                       out, &message),
            out, message, "stream that does not parse");

    const std::vector<std::uint8_t> short_payload = {9u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    out = sentinel;
    message.clear();
    refused(msproject::props::replace_formula_record(fixture, short_payload, out, &message),
            out, message, "replacement payload that is not the 8-byte empty table");

    const std::vector<std::uint8_t> lying_count = {8u, 0u, 0u, 0u, 1u, 0u, 0u, 0u};
    out = sentinel;
    message.clear();
    refused(msproject::props::replace_formula_record(fixture, lying_count, out, &message),
            out, message, "replacement payload whose count does not fit");
}

#ifdef PROPS_HAVE_MPP_CFB
// The committed synthetic base .mpp carries the measured EMPTY 8-byte formula
// table (0 formulas).  Set one, set two, clear, and prove byte identity.
void test_writer_base_mpp() {
    using msproject::props::PropsDocument;
    using msproject::props::ParseError;

    std::vector<std::uint8_t> props;
    try {
        const mpp::CompoundFile file = mpp::CompoundFile::from_file(PROPS_BASE_MPP);
        props = file.read_stream("114/Props");
    } catch (const mpp::Error& read_error) {
        check(false, std::string("writer base: read synthetic-base.mpp 114/Props: ") +
                         read_error.what());
        return;
    }
    check_eq(props.size(), std::size_t{88690}, "writer base: Props stream size");
    PropsDocument document;
    ParseError error;
    std::string writer_error;
    check(msproject::props::parse_props(props, document, &error), "writer base: parse Props");
    check_eq(document.records.size(), std::size_t{161}, "writer base: record count");
    const auto* record = document.find(msproject::props::kFormulaRecordId);
    check(record != nullptr, "writer base: formula record present");
    if (record == nullptr) {
        return;
    }
    check_eq(record->payload_length, std::uint32_t{8},
             "writer base: formula table is the 8-byte empty form");
    check_eq(msproject::props::extract_formulas(document).size(), std::size_t{0},
             "writer base: no formulas to start with");

    // One formula: Text2 = [Name].  Growth is the 68-byte generated block.
    std::vector<std::uint8_t> one;
    check(msproject::props::set_custom_field_formula(props, 0x0b400036u, "[Name]", one,
                                                     &writer_error),
          "writer base: set one formula");
    check_eq(one.size(), props.size() + 76u, "writer base: one formula grows by 76 bytes");
    PropsDocument one_document;
    check(msproject::props::parse_props(one, one_document, &error),
          "writer base: parse after one formula");
    check_eq(flag_value(document), std::uint32_t{0u},
             "writer base: custom-field flag starts at 0");
    check_eq(flag_value(one_document), std::uint32_t{1u},
             "writer base: set raises the custom-field flag to 1");
    check(only_formula_and_flag_changed(props, document, one, one_document, 84u, 1u),
          "writer base: one formula changes only the formula and flag records");
    check_eq(one_document.header.size_word,
             static_cast<std::uint32_t>(one.size() - 4u),
             "writer base: header size word after one formula");
    const auto one_formulas = msproject::props::extract_formulas(one_document);
    check_eq(one_formulas.size(), std::size_t{1}, "writer base: one formula reads back");
    if (one_formulas.size() == 1) {
        check_eq(one_formulas[0].field_id, std::uint32_t{0x0b400036u},
                 "writer base: one formula field id");
        check_eq(one_formulas[0].formula, std::string("[MSPJ188743694]"),
                 "writer base: one formula stored text");
        check_eq(one_formulas[0].format_words[0], std::uint32_t{68u},
                 "writer base: one formula block span word");
        check(!one_formulas[0].has_trailer,
              "writer base: generated block carries no trailer");
    }

    // The rewrite must use the parser's field-reference positions, not a text
    // scan: a bracketed token inside a string literal is formula text.
    std::vector<std::uint8_t> bracketed;
    check(msproject::props::set_custom_field_formula(props, 0x0b400036u,
                                                     "\"a[b]\" & [Name]", bracketed,
                                                     &writer_error),
          "writer base: set formula with a bracket inside a string literal");
    PropsDocument bracketed_document;
    check(msproject::props::parse_props(bracketed, bracketed_document, &error),
          "writer base: parse bracketed-formula stream");
    const auto bracketed_formulas = msproject::props::extract_formulas(
        bracketed_document);
    check_eq(bracketed_formulas.size(), std::size_t{1},
             "writer base: bracketed formula reads back");
    if (bracketed_formulas.size() == 1) {
        check_eq(bracketed_formulas[0].formula,
                 std::string("\"a[b]\" & [MSPJ188743694]"),
                 "writer base: bracket in a string literal is not rewritten");
    }

    // Two formulas: Number2 = [Duration]/240 on top.  The payload must equal
    // Project's measured 168-byte record byte-for-byte (wave-14 fixture pair,
    // 2026-10-22; synthetic project, no client data).
    static const std::uint8_t kMeasuredW14Payload[168] = {
        0xa8, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x36, 0x00, 0x40, 0x0b,
        0x18, 0x00, 0x00, 0x00, 0x58, 0x00, 0x40, 0x0b, 0x5c, 0x00, 0x00, 0x00,
        0x0d, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x44, 0x00, 0x00, 0x00,
        0x44, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00,
        0x44, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00,
        0x5b, 0x00, 0x4d, 0x00, 0x53, 0x00, 0x50, 0x00, 0x4a, 0x00, 0x31, 0x00,
        0x38, 0x00, 0x38, 0x00, 0x37, 0x00, 0x34, 0x00, 0x33, 0x00, 0x36, 0x00,
        0x39, 0x00, 0x34, 0x00, 0x5d, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0x4c, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00,
        0x4c, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00,
        0x4c, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x5b, 0x00, 0x4d, 0x00,
        0x53, 0x00, 0x50, 0x00, 0x4a, 0x00, 0x31, 0x00, 0x38, 0x00, 0x38, 0x00,
        0x37, 0x00, 0x34, 0x00, 0x34, 0x00, 0x39, 0x00, 0x36, 0x00, 0x37, 0x00,
        0x5d, 0x00, 0x2f, 0x00, 0x32, 0x00, 0x34, 0x00, 0x30, 0x00, 0x00, 0x00,
    };
    std::vector<std::uint8_t> two;
    check(msproject::props::set_custom_field_formula(one, 0x0b400058u, "[Duration]/240",
                                                     two, &writer_error),
          "writer base: set second formula");
    check_eq(two.size(), props.size() + 160u, "writer base: two formulas grow by 160 bytes");
    PropsDocument two_document;
    check(msproject::props::parse_props(two, two_document, &error),
          "writer base: parse after two formulas");
    check_eq(flag_value(two_document), std::uint32_t{1u},
             "writer base: two formulas keep the flag at 1");
    check(only_formula_and_flag_changed(props, document, two, two_document, 168u, 1u),
          "writer base: two formulas change only the formula and flag records");
    const auto* two_record = two_document.find(msproject::props::kFormulaRecordId);
    check(two_record != nullptr, "writer base: two-formula record present");
    if (two_record != nullptr) {
        check_eq(two_record->payload_length, std::uint32_t{168},
                 "writer base: two formulas make a 168-byte record");
        const auto two_payload = formula_payload(two_document);
        check_eq(two_payload.size(), std::size_t{168}, "writer base: 168-byte payload");
        if (two_payload.size() == 168) {
            check_eq(get_u32(two_payload, 0), std::uint32_t{168},
                     "writer base: payload inner size word");
            check_eq(get_u32(two_payload, 4), std::uint32_t{2},
                     "writer base: payload formula count");
            check(std::equal(two_payload.begin(), two_payload.end(),
                             std::begin(kMeasuredW14Payload)),
                  "writer base: 168-byte payload equals the measured wave-14 record");
        }
    }
    const auto two_formulas = msproject::props::extract_formulas(two_document);
    check_eq(two_formulas.size(), std::size_t{2}, "writer base: two formulas read back");
    if (two_formulas.size() == 2) {
        check_eq(two_formulas[0].field_id, std::uint32_t{0x0b400036u},
                 "writer base: first entry is Text2 (ascending field id)");
        check_eq(two_formulas[1].field_id, std::uint32_t{0x0b400058u},
                 "writer base: second entry is Number2");
        check_eq(two_formulas[0].formula, std::string("[MSPJ188743694]"),
                 "writer base: Text2 formula text");
        check_eq(two_formulas[1].formula, std::string("[MSPJ188744967]/240"),
                 "writer base: Number2 formula text (Duration stored as DurationText)");
    }

    // Clear returns to the measured 8-byte table AND to the original bytes.
    std::vector<std::uint8_t> cleared;
    check(msproject::props::clear_custom_field_formula(two, cleared, &writer_error),
          "writer base: clear two formulas");
    check_eq(cleared.size(), props.size(), "writer base: clear restores the original length");
    check(cleared == props, "writer base: set-then-clear round-trips byte-identically");
    PropsDocument cleared_document;
    check(msproject::props::parse_props(cleared, cleared_document, &error),
          "writer base: parse after clear");
    const auto* cleared_record =
        cleared_document.find(msproject::props::kFormulaRecordId);
    check(cleared_record != nullptr, "writer base: cleared record present");
    if (cleared_record != nullptr) {
        check_eq(cleared_record->payload_length, std::uint32_t{8},
                 "writer base: cleared payload is 8 bytes");
        check_eq(get_u32(formula_payload(cleared_document), 0), std::uint32_t{8},
                 "writer base: cleared inner size word");
        check_eq(get_u32(formula_payload(cleared_document), 4), std::uint32_t{0},
                 "writer base: cleared count");
    }
    check_eq(msproject::props::extract_formulas(cleared_document).size(), std::size_t{0},
             "writer base: cleared stream has no formulas");
    check_eq(flag_value(cleared_document), std::uint32_t{0u},
             "writer base: clear restores the custom-field flag to 0");

    // No-op re-encode of the unmodified (empty-table) stream is byte-identical.
    std::vector<std::uint8_t> noop;
    const auto empty_payload = formula_payload(document);
    check(msproject::props::replace_formula_record(props, empty_payload, noop, &writer_error),
          "writer base: no-op replace of the empty table");
    check(noop == props, "writer base: no-op re-encode is byte-identical");

    // A file can have an empty formula table and still be "custom fields in
    // use" (measured: w10-fieldvalues has 1 here with no formulas).  Clearing
    // an already-empty table must not guess that flag down to 0.
    std::vector<std::uint8_t> flag_one = props;
    PropsDocument flag_one_document;
    check(msproject::props::parse_props(flag_one, flag_one_document, &error),
          "writer base: parse flag-one copy");
    const auto* flag_record = flag_one_document.find(
        msproject::props::kCustomFieldFlagRecordId);
    check(flag_record != nullptr, "writer base: flag record present");
    if (flag_record != nullptr) {
        flag_one[flag_record->offset + 12u] = 1u;
        flag_one[flag_record->offset + 13u] = 0u;
        PropsDocument flag_one_parsed;
        check(msproject::props::parse_props(flag_one, flag_one_parsed, &error),
              "writer base: parse patched flag-one copy");
        std::vector<std::uint8_t> cleared_noop;
        check(msproject::props::clear_custom_field_formula(flag_one, cleared_noop,
                                                           &writer_error),
              "writer base: clearing an empty table succeeds with flag 1");
        check(cleared_noop == flag_one,
              "writer base: no-op clear leaves the custom-field flag alone");
    }
}
#endif  // PROPS_HAVE_MPP_CFB

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
    test_writer_committed(fixture);
    test_writer_refusals(fixture);
    test_mspj();
    test_tail(fixture);
    test_generic_layout();
    test_malformed(fixture);
#ifdef PROPS_HAVE_MPP_CFB
    test_writer_base_mpp();
#else
    std::printf("SKIP: writer base .mpp case (core/mpp CFB layer not linked)\n");
#endif
    test_optional_corpus_blob();

    if (g_failures != 0) {
        std::fprintf(stderr, "FAIL: %d checks, %d failures\n", g_checks, g_failures);
        return 1;
    }
    std::printf("PASS: %d checks, 0 failures\n", g_checks);
    return 0;
}
