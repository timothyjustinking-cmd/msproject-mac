/*
 * engine_diff_tests.cpp -- E1: the our-side half of the differential engine
 * harness, plus its self-test.
 *
 * WHY THIS FILE EXISTS.  docs/engine-parity.md measures "our engine behaves like
 * MS Project" by running the same scenario through both and comparing a
 * canonical state dump with NO tolerance.  scripts/engine_diff.py owns the JSON
 * scenario grammar, the oracle recording (coordinator-only COM) and the diff;
 * this executable owns the two C++-side primitives it cannot do from Python:
 *
 *   --apply <ops.tsv> <base.mpp> <out.mpp> [<expected-base-sha256>]
 *       Apply an ordered operation list to a copy of a fixture through the
 *       project's own writer (core/mppwrite) and scheduler (core/plan), and
 *       save the result.  The ops file is compiled by scripts/engine_diff.py
 *       from a tests/scenarios/*.json file; see the grammar below.  The base
 *       file is hashed before and after the run: a run that changes its input
 *       is refused, and when the expected hash is supplied (scripts/
 *       engine_diff.py always supplies the pinned one) a changed base is
 *       refused before anything is written.  `out.mpp` must not be the base.
 *
 *   --dump <file.mpp> <out.json>
 *       Decode a saved .mpp with core/mpp (the same reader tools/reader_dump
 *       links) + core/cal (the calendar table) and write the canonical state
 *       dump.  The dump is numbers and ids only: no task, resource, calendar
 *       or exception names ever reach it, so it is safe to commit.
 *
 *   (no arguments)
 *       Self-test: apply a built-in operation list per scenario family to a
 *       COPY of tests/data/synthetic-base.mpp, assert the documented values,
 *       dump the result and prove the dump carries no names.  This proves the
 *       our-side machinery without MS Project; it does NOT claim parity (the
 *       recorded oracle JSON is what claims that).
 *
 * OP GRAMMAR (TSV, one operation per line, '#' comment lines ignored).  Every
 * value is positional so the Python half can compile a scenario without a JSON
 * parser here:
 *
 *   recalculate
 *   set_task_field   <uid> <field-id> <value>
 *   add_link         <pred-uid> <succ-uid> <type 0..3> <lag-ticks>
 *   delete_task      <uid>
 *   add_assignment   <task-uid> <resource-uid> <units-percent>
 *   set_calendar     <task-uid> <calendar-uid>
 *   save
 *
 * `save` must be the last operation; it is the state the harness compares.
 * A field id is a PjField id from analysis/field_id_map.tsv.  The supported
 * subset is exactly the one below; every other id is refused rather than
 * guessed:
 *
 *   188743709  0x0b40001d  Task.Duration        value = file ticks (4800 = 1 day)
 *   188743697  0x0b400011  Task.ConstraintType  value = PjConstraint 0..7
 *   188743698  0x0b400012  Task.ConstraintDate  value = "YYYY-MM-DD HH:MM[:SS]"
 *   188743715  0x0b400023  Task.Start           value = date; written as an SNET
 *                                                pin (mppwrite::pin_task_start)
 *   188743712  0x0b400020  Task.PercentComplete value = 0..100
 *   188743705  0x0b400019  Task.Priority        value = 0..1000
 *   PjTask custom Number/Duration/Date/Cost/Flag1 ids (see core/mpp/
 *   field_value.h): value = number / ticks / date / cost / true|false.
 *   Text fields are refused on purpose: a scenario corpus carries ids and
 *   numbers only, and a text field is where a client name would leak in.
 *
 * WHAT "RECALCULATE" MEANS HERE.  Our engine has no "edit in memory and Project
 * recalculates on open": the harness writes the current document, maps it onto
 * core::plan, runs the CPM + summary rollup, and writes the plan back through
 * mppwrite::save_plan (the row_writer "every copy agrees" path).  A duration
 * edit is a scheduling input to that pass rather than an immediate byte write,
 * which is why every scenario ends with save (and usually recalculate).
 *
 * EXIT CODES: 0 success, 1 the scenario/self-test failed, 2 bad usage.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "add_delete.h"
#include "cal.h"
#include "calendar.h"
#include "cfb_writer.h"
#include "field_value.h"
#include "field_values.h"
#include "plan.h"
#include "reader.h"
#include "row_writer.h"
#include "save.h"
#include "table.h"

#ifndef ENGINE_DIFF_TEST_DATA
#define ENGINE_DIFF_TEST_DATA "tests/data"
#endif
#ifndef ENGINE_DIFF_TEST_OUT
#define ENGINE_DIFF_TEST_OUT "build/e1/engine-diff"
#endif

namespace {

using mppwrite::kInvalidDate;

/* PjField ids used by the operation grammar (analysis/field_id_map.tsv). */
constexpr std::uint32_t kFieldDuration = 188743709u;        /* 0x0b40001d */
constexpr std::uint32_t kFieldConstraintType = 188743697u;  /* 0x0b400011 */
constexpr std::uint32_t kFieldConstraintDate = 188743698u;  /* 0x0b400012 */
constexpr std::uint32_t kFieldStart = 188743715u;           /* 0x0b400023 */
constexpr std::uint32_t kFieldPercentComplete = 188743712u; /* 0x0b400020 */
constexpr std::uint32_t kFieldPriority = 188743705u;        /* 0x0b400019 */

class Error : public std::runtime_error {
  public:
    explicit Error(std::string const& message) : std::runtime_error(message) {}
};

/* ------------------------------------------------------------------------- */
/* SHA-256 (self-contained, no third-party code)                             */
/*                                                                           */
/* The harness must prove that a run did not touch its base fixture: a       */
/* corrupted base silently produces a corrupt oracle, and an oracle is only  */
/* a measurement of the file it was recorded from.  --apply hashes the base  */
/* before and after and compares it with the expected hash the Python half   */
/* passes in (the one pinned in scripts/engine_diff.py).                     */
/* ------------------------------------------------------------------------- */

namespace sha256 {

inline std::uint32_t rotate_right(std::uint32_t value, std::uint32_t count) {
    return (value >> count) | (value << (32u - count));
}

inline std::uint32_t choose(std::uint32_t x, std::uint32_t y,
                            std::uint32_t z) {
    return (x & y) ^ (~x & z);
}
inline std::uint32_t majority(std::uint32_t x, std::uint32_t y,
                              std::uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}
inline std::uint32_t big_sigma0(std::uint32_t x) {
    return rotate_right(x, 2) ^ rotate_right(x, 13) ^ rotate_right(x, 22);
}
inline std::uint32_t big_sigma1(std::uint32_t x) {
    return rotate_right(x, 6) ^ rotate_right(x, 11) ^ rotate_right(x, 25);
}
inline std::uint32_t small_sigma0(std::uint32_t x) {
    return rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3);
}
inline std::uint32_t small_sigma1(std::uint32_t x) {
    return rotate_right(x, 17) ^ rotate_right(x, 19) ^ (x >> 10);
}

inline constexpr std::uint32_t kRound[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

struct Context {
    std::uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                              0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                              0x1f83d9abu, 0x5be0cd19u};
    std::uint64_t bit_length = 0;
    std::uint8_t block[64] = {};
    std::size_t block_length = 0;

    void transform(std::uint8_t const data[64]) {
        std::uint32_t words[64] = {};
        for (std::size_t index = 0; index < 16; ++index) {
            words[index] =
                (static_cast<std::uint32_t>(data[index * 4]) << 24) |
                (static_cast<std::uint32_t>(data[index * 4 + 1]) << 16) |
                (static_cast<std::uint32_t>(data[index * 4 + 2]) << 8) |
                static_cast<std::uint32_t>(data[index * 4 + 3]);
        }
        for (std::size_t index = 16; index < 64; ++index) {
            words[index] = small_sigma1(words[index - 2]) + words[index - 7] +
                           small_sigma0(words[index - 15]) +
                           words[index - 16];
        }
        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];
        for (std::size_t index = 0; index < 64; ++index) {
            const std::uint32_t t1 = h + big_sigma1(e) + choose(e, f, g) +
                                     kRound[index] + words[index];
            const std::uint32_t t2 = big_sigma0(a) + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    void update(std::uint8_t const* data, std::size_t length) {
        for (std::size_t index = 0; index < length; ++index) {
            block[block_length++] = data[index];
            if (block_length == 64) {
                transform(block);
                bit_length += 512;
                block_length = 0;
            }
        }
    }

    void finish(std::uint8_t digest[32]) {
        std::size_t index = block_length;
        block[index++] = 0x80;
        if (index > 56) {
            while (index < 64) {
                block[index++] = 0;
            }
            transform(block);
            index = 0;
        }
        while (index < 56) {
            block[index++] = 0;
        }
        bit_length += static_cast<std::uint64_t>(block_length) * 8u;
        for (std::size_t byte = 0; byte < 8; ++byte) {
            block[63 - byte] =
                static_cast<std::uint8_t>(bit_length >> (byte * 8u));
        }
        transform(block);
        for (std::size_t word = 0; word < 8; ++word) {
            digest[word * 4] =
                static_cast<std::uint8_t>(state[word] >> 24);
            digest[word * 4 + 1] =
                static_cast<std::uint8_t>(state[word] >> 16);
            digest[word * 4 + 2] =
                static_cast<std::uint8_t>(state[word] >> 8);
            digest[word * 4 + 3] = static_cast<std::uint8_t>(state[word]);
        }
    }
};

std::string hex(std::uint8_t const digest[32]) {
    static char const digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(64);
    for (std::size_t index = 0; index < 32; ++index) {
        text.push_back(digits[digest[index] >> 4]);
        text.push_back(digits[digest[index] & 0x0Fu]);
    }
    return text;
}

std::string of_bytes(std::string_view bytes) {
    Context context;
    context.update(reinterpret_cast<std::uint8_t const*>(bytes.data()),
                   bytes.size());
    std::uint8_t digest[32] = {};
    context.finish(digest);
    return hex(digest);
}

std::string of_file(std::string const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw Error("cannot read " + path + " to hash it");
    }
    Context context;
    char buffer[1 << 16];
    while (input) {
        input.read(buffer, sizeof buffer);
        const std::streamsize got = input.gcount();
        if (got > 0) {
            context.update(reinterpret_cast<std::uint8_t const*>(buffer),
                           static_cast<std::size_t>(got));
        }
    }
    if (input.bad()) {
        throw Error("read error while hashing " + path);
    }
    std::uint8_t digest[32] = {};
    context.finish(digest);
    return hex(digest);
}

}  // namespace sha256

std::string lowercase(std::string text) {
    for (char& character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return text;
}

/* The canonical form of a path, lowercased on Windows so two spellings of the
 * same file compare equal.  Used only to refuse "write the output over the
 * input"; the real guard is the before/after hash. */
std::string canonical_path(std::string const& path) {
    std::error_code error;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
    if (error) {
        resolved = std::filesystem::absolute(path, error);
        if (error) {
            resolved = std::filesystem::path(path);
        }
    }
    std::string text = resolved.string();
#ifdef _WIN32
    text = lowercase(text);
#endif
    return text;
}

/* ------------------------------------------------------------------------- */
/* small parsing helpers                                                     */
/* ------------------------------------------------------------------------- */

std::string trim(std::string text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::uint32_t parse_u32(std::string const& text, char const* what) {
    std::size_t used = 0;
    long long value = 0;
    try {
        value = std::stoll(trim(text), &used, 10);
    } catch (std::exception const&) {
        throw Error(std::string("expected a decimal integer for ") + what +
                    ", got '" + text + "'");
    }
    if (used != trim(text).size() || value < 0 || value > 0xFFFFFFFFll) {
        throw Error(std::string("expected a decimal integer for ") + what +
                    ", got '" + text + "'");
    }
    return static_cast<std::uint32_t>(value);
}

std::int32_t parse_i32(std::string const& text, char const* what) {
    std::size_t used = 0;
    long long value = 0;
    try {
        value = std::stoll(trim(text), &used, 10);
    } catch (std::exception const&) {
        throw Error(std::string("expected a signed integer for ") + what +
                    ", got '" + text + "'");
    }
    if (used != trim(text).size() || value < -2147483648ll ||
        value > 2147483647ll) {
        throw Error(std::string("expected a signed integer for ") + what +
                    ", got '" + text + "'");
    }
    return static_cast<std::int32_t>(value);
}

double parse_double(std::string const& text, char const* what) {
    std::size_t used = 0;
    double value = 0.0;
    try {
        value = std::stod(trim(text), &used);
    } catch (std::exception const&) {
        throw Error(std::string("expected a number for ") + what + ", got '" +
                    text + "'");
    }
    if (used != trim(text).size() || !std::isfinite(value)) {
        throw Error(std::string("expected a finite number for ") + what +
                    ", got '" + text + "'");
    }
    return value;
}

bool parse_bool(std::string const& text, char const* what) {
    std::string const value = trim(text);
    if (value == "true" || value == "True" || value == "1") {
        return true;
    }
    if (value == "false" || value == "False" || value == "0") {
        return false;
    }
    throw Error(std::string("expected true or false for ") + what + ", got '" +
                text + "'");
}

/* "YYYY-MM-DD HH:MM[:SS]", with 'T' as the separator, an optional '+00:00' or
 * 'Z' suffix, or a date only (midnight).  Returns the packed date word. */
std::uint32_t parse_date(std::string text, char const* what) {
    text = trim(text);
    if (text.size() >= 6 &&
        text.compare(text.size() - 6, 6, "+00:00") == 0) {
        text.resize(text.size() - 6);
    } else if (!text.empty() && (text.back() == 'Z' || text.back() == 'z')) {
        text.pop_back();
    }
    for (char& character : text) {
        if (character == 'T') {
            character = ' ';
        }
    }
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    char dash1 = 0;
    char dash2 = 0;
    char colon1 = 0;
    char colon2 = 0;
    std::istringstream in(text);
    in >> year >> dash1 >> month >> dash2 >> day;
    if (in.fail() || dash1 != '-' || dash2 != '-') {
        throw Error(std::string("expected a date for ") + what + ", got '" +
                    text + "'");
    }
    if (in >> hour) {
        if (!(in >> colon1 >> minute) || colon1 != ':') {
            throw Error(std::string("expected HH:MM after the date for ") + what +
                        ", got '" + text + "'");
        }
        if (in >> colon2) {
            if (colon2 != ':' || !(in >> second)) {
                throw Error(std::string("expected :SS after HH:MM for ") + what +
                            ", got '" + text + "'");
            }
        }
    }
    if (!in.eof() && in.fail()) {
        throw Error(std::string("trailing characters in date for ") + what +
                    ", got '" + text + "'");
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        throw Error(std::string("date out of range for ") + what + ": '" + text +
                    "'");
    }
    return mppwrite::pack_date_ymd(year, month, day,
                                   hour * 3600 + minute * 60 + second);
}

std::string date_text(std::uint32_t raw) {
    return mpp::format_date_iso(mpp::PackedDate{raw});
}

std::string field_id_text(std::uint32_t field_id) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << std::setw(8)
        << std::setfill('0') << field_id;
    return out.str();
}

/* ------------------------------------------------------------------------- */
/* canonical state dump                                                      */
/* ------------------------------------------------------------------------- */

class JsonWriter {
  public:
    explicit JsonWriter(std::ostream& out) : out_(out) { first_.push_back(true); }

    void begin_object() {
        prefix();
        out_ << '{';
        first_.push_back(true);
        ++depth_;
    }
    void end_object() {
        out_ << '\n';
        --depth_;
        indent();
        out_ << '}';
        first_.pop_back();
    }
    void begin_array() {
        prefix();
        out_ << '[';
        first_.push_back(true);
        ++depth_;
    }
    void end_array() {
        out_ << '\n';
        --depth_;
        indent();
        out_ << ']';
        first_.pop_back();
    }
    void key(std::string_view text) {
        prefix();
        out_ << quote(text) << ": ";
        after_key_ = true;
    }
    void value(std::string_view text) {
        prefix();
        out_ << quote(text);
    }
    void value(char const* text) { value(std::string_view(text)); }
    void value(bool flag) {
        prefix();
        out_ << (flag ? "true" : "false");
    }
    template <typename T,
              std::enable_if_t<std::is_integral_v<T> &&
                                   !std::is_same_v<std::remove_cv_t<T>, bool>,
                               int> = 0>
    void value(T number) {
        prefix();
        out_ << static_cast<long long>(number);
    }
    void value(double number) {
        prefix();
        std::ostringstream text;
        text << std::setprecision(17) << number;
        out_ << text.str();
    }

  private:
    void prefix() {
        if (after_key_) {
            after_key_ = false;
            return;
        }
        if (!first_.back()) {
            out_ << ',';
        }
        first_.back() = false;
        if (depth_ > 0) {
            out_ << '\n';
            indent();
        }
    }
    void indent() { out_ << std::string(static_cast<std::size_t>(depth_) * 2, ' '); }
    static std::string quote(std::string_view text) {
        std::string out;
        out.reserve(text.size() + 2);
        out.push_back('"');
        for (char const character : text) {
            switch (character) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(character) < 0x20) {
                        out += '?';
                    } else {
                        out.push_back(character);
                    }
                    break;
            }
        }
        out.push_back('"');
        return out;
    }

    std::ostream& out_;
    std::vector<bool> first_;
    int depth_ = 0;
    bool after_key_ = false;
};

void write_week(JsonWriter& json, core::cal::WeekSpec const& week) {
    json.begin_array();
    for (core::cal::DaySpec const& day : week) {
        json.begin_object();
        json.key("use_base");
        json.value(day.use_base);
        json.key("intervals");
        json.begin_array();
        for (core::cal::WorkInterval const& interval : day.intervals) {
            json.begin_array();
            json.value(interval.start_ticks);
            json.value(interval.length_ticks);
            json.end_array();
        }
        json.end_array();
        json.end_object();
    }
    json.end_array();
}

const char* custom_kind_name(mpp::FieldKind kind) {
    switch (kind) {
        case mpp::FieldKind::text:
            return "text";
        case mpp::FieldKind::number:
            return "number";
        case mpp::FieldKind::duration:
            return "duration";
        case mpp::FieldKind::date:
            return "date";
        case mpp::FieldKind::flag:
            return "flag";
        case mpp::FieldKind::cost:
            return "cost";
        case mpp::FieldKind::unknown:
        default:
            return "unknown";
    }
}

void write_custom_fields(JsonWriter& json, mpp::Task const& task) {
    json.key("custom");
    json.begin_object();
    std::vector<mpp::CustomFieldValue const*> fields;
    for (mpp::CustomFieldValue const& field : task.custom_fields) {
        /* Text is deliberately absent: a text custom field is where a name
         * would leak into a committed oracle state. */
        if (field.kind != mpp::FieldKind::text) {
            fields.push_back(&field);
        }
    }
    std::sort(fields.begin(), fields.end(),
              [](mpp::CustomFieldValue const* left,
                 mpp::CustomFieldValue const* right) {
                  return left->field_id < right->field_id;
              });
    for (mpp::CustomFieldValue const* field : fields) {
        json.key(field_id_text(field->field_id));
        json.begin_object();
        json.key("kind");
        json.value(custom_kind_name(field->kind));
        json.key("present");
        json.value(field->present);
        json.key("measured");
        json.value(field->storage_measured);
        if (field->present && field->storage_measured) {
            switch (field->kind) {
                case mpp::FieldKind::number:
                    json.key("value");
                    json.value(field->number);
                    break;
                case mpp::FieldKind::cost:
                    json.key("value");
                    json.value(field->cost);
                    break;
                case mpp::FieldKind::duration:
                    json.key("ticks");
                    json.value(field->duration_ticks);
                    break;
                case mpp::FieldKind::date:
                    json.key("value");
                    json.value(date_text(field->packed_date));
                    break;
                case mpp::FieldKind::flag:
                    json.key("value");
                    json.value(field->flag);
                    break;
                default:
                    break;
            }
        }
        json.end_object();
    }
    json.end_object();
}

void dump_file(std::string const& mpp_path, std::string const& json_path) {
    mpp::Document document;
    mppwrite::CfbDocument container;
    try {
        document = mpp::load_file(mpp_path);
        container = mppwrite::CfbDocument::from_file(mpp_path);
    } catch (std::exception const& error) {
        throw Error("cannot read " + mpp_path + " for the state dump: " +
                    error.what());
    }

    /* Recoverable container facts, not failures.  core/mpp loads a file whose
     * FAT is shorter than its sector count with a warning (sf248: 191
     * unreferenced of 959 sectors); mppwrite::CfbDocument now mirrors that rule,
     * so the dump must say what it tolerated.  stderr only: the JSON is
     * compared field by field and gains no new leaf. */
    for (std::string const& warning : container.warnings()) {
        std::cerr << "engine_diff: container warning: " << warning << "\n";
    }

    bool calendars_decoded = true;
    core::cal::CalendarTable calendar_table;
    try {
        calendar_table = core::cal::decode_file(mpp_path);
    } catch (std::exception const& error) {
        calendars_decoded = false;
        std::cerr << "engine_diff: calendar table not decoded (" << error.what()
                  << "); the dump records decoded=false\n";
    }

    std::ofstream out(json_path);
    if (!out) {
        throw Error("cannot write the state dump to " + json_path);
    }
    JsonWriter json(out);
    json.begin_object();

    json.key("schema");
    json.value("engine-diff/2");

    /* ---- project row ---------------------------------------------------- */
    json.key("project");
    json.begin_object();
    json.key("uid");
    json.value(document.project.uid);
    json.key("id");
    json.value(document.project.row_id);
    json.key("start");
    json.value(date_text(document.project.start.raw));
    json.key("finish");
    json.value(date_text(document.project.finish.raw));
    json.key("duration_ticks");
    json.value(document.project.duration_ticks);
    json.key("priority");
    json.value(document.project.priority);
    json.key("percent_complete");
    json.value(document.project.percent_complete);
    json.end_object();

    /* ---- tasks ---------------------------------------------------------- */
    std::vector<mpp::Task const*> tasks;
    tasks.reserve(document.tasks.size());
    for (mpp::Task const& task : document.tasks) {
        tasks.push_back(&task);
    }
    std::sort(tasks.begin(), tasks.end(),
              [](mpp::Task const* left, mpp::Task const* right) {
                  return left->uid < right->uid;
              });
    json.key("tasks");
    json.begin_array();
    for (mpp::Task const* task : tasks) {
        json.begin_object();
        json.key("uid");
        json.value(task->uid);
        json.key("id");
        json.value(task->row_id);
        json.key("outline_number");
        json.value(task->outline_number);
        json.key("outline_level");
        json.value(task->outline_level);
        json.key("summary");
        json.value(task->summary);
        json.key("milestone");
        json.value(task->milestone);
        json.key("milestone_stored");
        json.value(task->milestone_stored);
        json.key("manually_scheduled");
        json.value(task->manually_scheduled);
        json.key("critical");
        json.value(task->critical);
        json.key("duration_ticks");
        json.value(task->duration_ticks);
        json.key("duration_format");
        json.value(task->duration_format);
        json.key("start");
        json.value(date_text(task->start.raw));
        json.key("finish");
        json.value(date_text(task->finish.raw));
        json.key("calculated_start");
        json.value(date_text(task->calculated_start.raw));
        json.key("calculated_finish");
        json.value(date_text(task->calculated_finish.raw));
        /* The stored late pair (+112/+116).  NOT a copy of start/finish on every
         * row: synthetic-base's uid 2 stores 2026-01-06 13:00 here while its
         * start is 2026-01-04 13:00, and COM's LateStart/LateFinish equal these
         * words on corpus/probe/w3-o1.mpp.  The COM EarlyStart/EarlyFinish
         * properties are the +104/+108 words already dumped as start/finish
         * (they are equal on every COM read-back on disk), so there is no
         * duplicate early_* key; see core/mpp/reader.h. */
        json.key("late_start");
        json.value(date_text(task->late_start.raw));
        json.key("late_finish");
        json.value(date_text(task->late_finish.raw));
        json.key("priority");
        json.value(task->priority);
        json.key("percent_complete");
        json.value(task->percent_complete);
        json.key("free_slack_ticks");
        json.value(task->free_slack_ticks);
        json.key("start_slack_ticks");
        json.value(task->start_slack_ticks);
        json.key("total_slack_ticks");
        json.value(task->total_slack_ticks);
        json.key("start_anchor");
        json.value(date_text(task->start_anchor.raw));
        json.key("calendar_uid");
        json.value(task->calendar_uid);
        json.key("constraint_type");
        json.value(task->constraint_type);
        json.key("constraint_date");
        json.value(date_text(task->constraint_date.raw));
        json.key("deadline");
        json.value(date_text(task->deadline.raw));
        json.key("actual_start");
        json.value(date_text(task->actual_start.raw));
        json.key("actual_finish");
        json.value(date_text(task->actual_finish.raw));
        json.key("actual_duration_ticks");
        json.value(task->actual_duration_ticks);
        json.key("child_count");
        json.value(task->child_count);
        write_custom_fields(json, *task);
        json.end_object();
    }
    json.end_array();

    /* ---- links ---------------------------------------------------------- */
    std::vector<mpp::Link const*> links;
    links.reserve(document.links.size());
    for (mpp::Link const& link : document.links) {
        links.push_back(&link);
    }
    std::sort(links.begin(), links.end(),
              [](mpp::Link const* left, mpp::Link const* right) {
                  return left->uid < right->uid;
              });
    json.key("links");
    json.begin_array();
    for (mpp::Link const* link : links) {
        json.begin_object();
        json.key("uid");
        json.value(link->uid);
        json.key("predecessor_uid");
        json.value(link->predecessor_uid);
        json.key("successor_uid");
        json.value(link->successor_uid);
        json.key("type");
        json.value(static_cast<unsigned>(link->type()));
        json.key("lag_ticks");
        json.value(link->lag_ticks);
        json.key("lag_units");
        json.value(link->lag_units);
        json.key("predecessor_known");
        json.value(link->predecessor_known);
        json.key("successor_known");
        json.value(link->successor_known);
        json.end_object();
    }
    json.end_array();

    /* ---- assignments ---------------------------------------------------- */
    std::vector<mpp::Assignment const*> assignments;
    assignments.reserve(document.assignments.size());
    for (mpp::Assignment const& assignment : document.assignments) {
        assignments.push_back(&assignment);
    }
    std::sort(assignments.begin(), assignments.end(),
              [](mpp::Assignment const* left, mpp::Assignment const* right) {
                  return left->uid < right->uid;
              });
    json.key("assignments");
    json.begin_array();
    for (mpp::Assignment const* assignment : assignments) {
        json.begin_object();
        json.key("uid");
        json.value(assignment->uid);
        json.key("task_uid");
        json.value(assignment->task_uid);
        json.key("resource_uid");
        json.value(assignment->resource_uid());
        json.key("units_x100");
        json.value(static_cast<long long>(std::llround(assignment->units_x100)));
        json.key("work_ticks");
        json.value(assignment->work_ticks());
        json.key("start");
        json.value(date_text(assignment->start.raw));
        json.key("finish");
        json.value(date_text(assignment->finish.raw));
        json.key("resume");
        json.value(date_text(assignment->resume.raw));
        json.end_object();
    }
    json.end_array();

    /* ---- resources ------------------------------------------------------ */
    std::vector<mpp::Resource const*> resources;
    resources.reserve(document.resources.size());
    for (mpp::Resource const& resource : document.resources) {
        resources.push_back(&resource);
    }
    std::sort(resources.begin(), resources.end(),
              [](mpp::Resource const* left, mpp::Resource const* right) {
                  return left->uid < right->uid;
              });
    json.key("resources");
    json.begin_array();
    for (mpp::Resource const* resource : resources) {
        json.begin_object();
        json.key("uid");
        json.value(resource->uid);
        json.key("id");
        json.value(resource->row_id);
        json.key("type");
        json.value(static_cast<unsigned>(resource->type));
        json.key("type_stored");
        json.value(resource->type_stored);
        json.key("max_units_x100");
        json.value(static_cast<long long>(std::llround(resource->max_units_x100)));
        /* Derived by the reader from the calendar table's resource column (not a
         * resource-row word); -1 when the file has no resource-calendar row.
         * Measured against Project's MSPDI export of w9-resourcetypes.mpp
         * (CalendarUID 3/4/5); see core/mpp/reader.h. */
        json.key("calendar_uid");
        json.value(resource->calendar_uid);
        json.end_object();
    }
    json.end_array();

    /* ---- calendars ------------------------------------------------------ */
    json.key("calendar_table");
    json.begin_object();
    json.key("decoded");
    json.value(calendars_decoded);
    if (calendars_decoded) {
        json.key("default_week");
        write_week(json, calendar_table.default_week);
        json.key("metadata_anomalies");
        json.value(calendar_table.metadata_anomalies);
        json.key("recurring_exceptions");
        json.value(calendar_table.recurring_exceptions);
        json.key("sub_minute_intervals");
        json.value(calendar_table.sub_minute_intervals);
        std::vector<core::cal::CalendarDefinition const*> calendars;
        calendars.reserve(calendar_table.calendars.size());
        for (core::cal::CalendarDefinition const& calendar :
             calendar_table.calendars) {
            calendars.push_back(&calendar);
        }
        std::sort(calendars.begin(), calendars.end(),
                  [](core::cal::CalendarDefinition const* left,
                     core::cal::CalendarDefinition const* right) {
                      return left->uid < right->uid;
                  });
        json.key("calendars");
        json.begin_array();
        for (core::cal::CalendarDefinition const* calendar : calendars) {
            json.begin_object();
            json.key("uid");
            json.value(calendar->uid);
            json.key("base_uid");
            json.value(calendar->base_uid);
            json.key("resource_uid");
            json.value(calendar->resource_uid);
            json.key("has_week_data");
            json.value(calendar->has_week_data);
            json.key("week");
            write_week(json, calendar->week);
            json.key("exceptions");
            json.begin_array();
            for (core::cal::DatedException const& exception :
                 calendar->exceptions) {
                json.begin_object();
                json.key("start_day");
                json.value(exception.start_day);
                json.key("end_day");
                json.value(exception.end_day);
                json.key("working");
                json.value(exception.working);
                json.key("intervals");
                json.begin_array();
                for (core::cal::WorkInterval const& interval :
                     exception.intervals) {
                    json.begin_array();
                    json.value(interval.start_ticks);
                    json.value(interval.length_ticks);
                    json.end_array();
                }
                json.end_array();
                json.key("recurrence_type");
                json.value(exception.recurrence_type);
                json.key("occurrences");
                json.value(exception.occurrences);
                json.key("period");
                json.value(exception.period);
                json.key("days_of_week");
                json.value(exception.days_of_week);
                json.key("month");
                json.value(exception.month);
                json.key("month_day");
                json.value(exception.month_day);
                json.key("month_item");
                json.value(exception.month_item);
                json.key("month_position");
                json.value(exception.month_position);
                json.key("raw_recurrence_words");
                json.begin_array();
                for (std::uint16_t const word : exception.raw_recurrence_words) {
                    json.value(word);
                }
                json.end_array();
                json.end_object();
            }
            json.end_array();
            json.key("work_weeks");
            json.begin_array();
            for (core::cal::DatedWorkWeek const& week : calendar->work_weeks) {
                json.begin_object();
                json.key("start_day");
                json.value(week.start_day);
                json.key("end_day");
                json.value(week.end_day);
                json.key("days");
                write_week(json, week.days);
                json.end_object();
            }
            json.end_array();
            json.end_object();
        }
        json.end_array();
    }
    json.end_object();

    /* ---- file container -------------------------------------------------- */
    json.key("file");
    json.begin_object();
    json.key("stream_count");
    json.value(container.stream_count());
    std::vector<std::string> paths = container.stream_paths();
    std::sort(paths.begin(), paths.end());
    json.key("streams");
    json.begin_array();
    for (std::string const& path : paths) {
        json.begin_object();
        json.key("path");
        json.value(path);
        json.key("size");
        json.value(container.stream_size(path));
        json.end_object();
    }
    json.end_array();
    json.end_object();

    json.end_object();
    out << '\n';
    out.close();
    if (!out) {
        throw Error("write error on the state dump " + json_path);
    }
}

/* ------------------------------------------------------------------------- */
/* operation application                                                     */
/* ------------------------------------------------------------------------- */

struct Operation {
    std::string name;
    std::vector<std::string> args;
};

std::vector<Operation> read_operations(std::string const& path) {
    std::ifstream in(path);
    if (!in) {
        throw Error("cannot open the operations file " + path);
    }
    std::vector<Operation> operations;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (true) {
            const std::size_t tab = line.find('\t', start);
            if (tab == std::string::npos) {
                parts.push_back(line.substr(start));
                break;
            }
            parts.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        Operation operation;
        operation.name = trim(parts[0]);
        for (std::size_t i = 1; i < parts.size(); ++i) {
            operation.args.push_back(parts[i]);
        }
        if (operation.name.empty()) {
            throw Error("operations file " + path + " line " +
                        std::to_string(line_number) + ": empty operation name");
        }
        operations.push_back(std::move(operation));
    }
    return operations;
}

void require_arity(Operation const& operation, std::size_t expected) {
    if (operation.args.size() != expected) {
        throw Error("operation '" + operation.name + "': expected " +
                    std::to_string(expected) + " argument(s), got " +
                    std::to_string(operation.args.size()));
    }
}

/* ------------------------------------------------------------------------- */
/* Project's save-time duration word for an off-calendar start               */
/*                                                                           */
/* MEASURED (docs/engine-parity.md "Wave 20", nine rows): when a MANUAL row  */
/* that carries a Must Start On constraint is saved, Project's +84 duration  */
/* word gains the remaining work windows of the START DAY when that day is   */
/* not a working day for the row's calendar; a working-day start gains 0.    */
/* Project's COM Task.Duration stays 960 min (9600 ticks) throughout, so only */
/* the stored +84 word moves: the kept dates, the calculated pair, the late  */
/* pair, the slack and the assignment work are the plan's.                   */
/*                                                                           */
/* Scope measured: MSO only.  constraint-snet applies an SNET constraint to  */
/* another manual row whose start is the same off-calendar Sunday and its    */
/* stored duration stays 14400 (no bump), so SNET is a negative control, not */
/* a second trigger.  The four starts a fixture row cannot be moved to (the  */
/* manual start write path is unsupported, row_writer.h) are exercised       */
/* through off_calendar_start_bump() directly; the five the applier reaches   */
/* are end-to-end in run_self_test().                                        */
/* ------------------------------------------------------------------------- */

/* PjConstraint 2: Must Start On (row_writer.h documents SNET = 4). */
constexpr std::uint16_t kConstraintMso = 2u;

/*
 * Working ticks in the row's start day at/after the start clock, using the
 * calendar's STANDARD DAY WINDOWS.  Zero on a working day (measured: Fri 13:00
 * gains nothing).  The window template is the first non-empty day pattern in
 * the calendar's default week: every measured row is on synthetic-base, whose
 * five working weekdays share 08:00-12:00 + 13:00-17:00, so the template for a
 * non-working day is INFERRED (next working weekday's shape), not measured
 * across calendars.
 */
core::sched::ticks off_calendar_start_bump(core::sched::Calendar const& calendar,
                                           core::sched::date32 start) {
    if (core::sched::is_na(start) || core::sched::is_overflow(start)) {
        return 0;
    }
    std::int32_t const day = core::sched::day_of(start);
    if (!calendar.pattern_for_day(day).empty()) {
        return 0; /* a working day: measured increment 0 */
    }
    core::sched::DayPattern const* standard = nullptr;
    for (core::sched::DayPattern const& pattern : calendar.default_week()) {
        if (!pattern.empty()) {
            standard = &pattern;
            break;
        }
    }
    if (standard == nullptr) {
        return 0;
    }
    std::int32_t const clock = core::sched::time_of(start);
    core::sched::ticks total = 0;
    for (std::size_t i = 0; i < standard->count; ++i) {
        std::int32_t const window_start = standard->start_ticks(i);
        std::int32_t const window_end = standard->end_ticks(i);
        if (clock < window_start) {
            total += window_end - window_start;
        } else if (clock < window_end) {
            total += window_end - clock;
        }
    }
    return total;
}

core::sched::Calendar const& calendar_for(core::plan::PlanInput const& input,
                                          std::int32_t uid) {
    if (uid > 0) {
        for (core::plan::TaskCalendar const& task_calendar :
             input.task_calendars) {
            if (task_calendar.uid == uid) {
                return task_calendar.calendar;
            }
        }
    }
    return input.calendar;
}

class Applier {
  public:
    Applier(std::string const& base_path, std::string const& out_path,
            std::string expected_base_sha256 = {})
        : base_path_(base_path),
          out_path_(out_path),
          work_path_(out_path + ".work.mpp") {
        if (canonical_path(base_path_) == canonical_path(out_path_)) {
            throw Error("--apply refuses to write the output over its input "
                        "(" + base_path_ +
                        "); the base fixture must never be modified");
        }
        base_sha256_before_ = sha256::of_file(base_path_);
        if (!expected_base_sha256.empty() &&
            lowercase(expected_base_sha256) != base_sha256_before_) {
            throw Error(
                "base fixture SHA-256 mismatch for " + base_path_ + ": the "
                "file hashes to " + base_sha256_before_ + ", expected " +
                lowercase(expected_base_sha256) +
                ".  A scenario measurement is only valid against the base it "
                "was recorded from; restore the fixture (see "
                "tests/data/README.md) and re-run.  Refusing to write " +
                out_path_);
        }
        doc_ = mppwrite::CfbDocument::from_file(base_path_);
    }

    /* Opt-in: model Project's save-time duration word (the measured MSO rule)
     * in the file this applier writes.  OFF for every scenario the differential
     * suite scores -- that file must be what the production save path writes.
     * The self-test sets it to exercise the rule end to end. */
    void model_project_save(bool enabled) { model_project_save_ = enabled; }

    void run(std::vector<Operation> const& operations) {
        if (operations.empty()) {
            throw Error("the scenario has no operations");
        }
        for (Operation const& operation : operations) {
            apply(operation);
        }
        if (!saved_) {
            throw Error(
                "the operations do not end with 'save'; the harness compares "
                "the state the save operation writes");
        }
        verify_base_unchanged();
    }

  private:
    void verify_base_unchanged() const {
        const std::string after = sha256::of_file(base_path_);
        if (after != base_sha256_before_) {
            throw Error(
                "the base fixture changed during this run: " + base_path_ +
                " was " + base_sha256_before_ + " and is now " + after +
                ".  Every harness write must go to its output path under "
                "build/; refusing the measurement");
        }
    }
    struct PendingConstraint {
        bool have_type = false;
        std::uint16_t type = 0;
        bool have_date = false;
        std::uint32_t date = kInvalidDate;
    };

    void apply(Operation const& operation) {
        if (saved_) {
            throw Error("operation '" + operation.name +
                        "' follows 'save'; save must be the last operation");
        }
        if (operation.name == "recalculate") {
            require_arity(operation, 0);
            recalculate(work_path_);
            return;
        }
        if (operation.name == "save") {
            require_arity(operation, 0);
            /* The compared state is the save.  Project's save-time duration
             * word (off_calendar_start_bump) is NOT applied unless the caller
             * opted in: a scored scenario's file must be exactly what the
             * production save path writes.  The self-test opts in to exercise
             * the measured rule end to end (never on an intermediate pass, so
             * the bumped word cannot feed back into the plan). */
            recalculate(out_path_, model_project_save_);
            saved_ = true;
            return;
        }
        if (operation.name == "set_task_field") {
            require_arity(operation, 3);
            set_task_field(parse_u32(operation.args[0], "uid"),
                           parse_u32(operation.args[1], "field-id"),
                           operation.args[2]);
            return;
        }
        if (operation.name == "add_link") {
            require_arity(operation, 4);
            const std::uint32_t predecessor =
                parse_u32(operation.args[0], "predecessor uid");
            const std::uint32_t successor =
                parse_u32(operation.args[1], "successor uid");
            const std::uint32_t type = parse_u32(operation.args[2], "link type");
            const std::int32_t lag = parse_i32(operation.args[3], "lag ticks");
            if (type > 3) {
                throw Error("add_link: link type " + std::to_string(type) +
                            " is outside 0..3 (0 FF, 1 FS, 2 SF, 3 SS)");
            }
            /* The lag reaches COM as "<minutes>m" (scripts/engine_diff.py
             * `_lag_text`), so Project stores LagFormat 3 (minutes); the
             * link-fs-lag oracle stores 3 where the writer used to store 7. */
            const mppwrite::AddedLink added = mppwrite::add_task_link(
                doc_, predecessor, successor, static_cast<std::uint16_t>(type),
                lag, work_path_, mppwrite::kLinkLagFormatMinutes);
            report("add_link", added.warnings);
            /* Project recalculates after a structural edit: the COM half calls
             * calc(app) after add_link/delete_task/add_assignment/set_calendar
             * (`_apply_com_operation`).  The recalculated dates are part of the
             * measured state -- a MANUAL successor's kept start is moved by the
             * new link and then SAVED (link-fs-lag uid 2 = 01-12 13:00,
             * delete-task-with-successor uid 3 = 01-14 13:00), so without this
             * pass the final recalculation would start from the old stored dates
             * and could not reproduce the oracle.  A link ADD is also the one
             * edit that moves a manual successor (plan.h
             * PlanInput::manual_rows_follow_links); a recalculation does not. */
            manual_follow_links_ = true;
            recalculate(work_path_);
            manual_follow_links_ = false;
            return;
        }
        if (operation.name == "delete_task") {
            require_arity(operation, 1);
            const mppwrite::DeletedRow deleted = mppwrite::delete_task_row(
                doc_, parse_u32(operation.args[0], "uid"), work_path_);
            report("delete_task", deleted.warnings);
            recalculate(work_path_); /* calc(app) on Project's side */
            return;
        }
        if (operation.name == "add_assignment") {
            require_arity(operation, 3);
            mppwrite::AssignmentSpec spec;
            spec.task_uid = parse_u32(operation.args[0], "task uid");
            spec.resource_uid = parse_u32(operation.args[1], "resource uid");
            spec.units_percent =
                parse_double(operation.args[2], "units percent");
            const mppwrite::AddedAssignment added =
                mppwrite::add_assignment_row(doc_, spec, work_path_);
            report("add_assignment", added.warnings);
            tombstone_null_resource_placeholder(spec.task_uid);
            recalculate(work_path_); /* calc(app) on Project's side */
            return;
        }
        if (operation.name == "set_calendar") {
            require_arity(operation, 2);
            set_calendar(parse_u32(operation.args[0], "task uid"),
                         parse_u32(operation.args[1], "calendar uid"));
            recalculate(work_path_); /* calc(app) on Project's side */
            return;
        }
        throw Error("unknown operation '" + operation.name + "'");
    }

    void report(std::string const& operation,
                std::vector<std::string> const& warnings) const {
        for (std::string const& warning : warnings) {
            std::cerr << "engine_diff: " << operation << ": " << warning << "\n";
        }
    }

    std::size_t row_for_uid(std::uint32_t uid) {
        const mppwrite::TaskTable table =
            mppwrite::TaskTable::from_document(doc_);
        return table.row_for_uid(uid);
    }

    void set_calendar(std::uint32_t uid, std::uint32_t calendar_uid) {
        if (calendar_uid == 0 || calendar_uid > 0x7FFFFFFFu) {
            throw Error("set_calendar: calendar uid " +
                        std::to_string(calendar_uid) +
                        " is not a positive calendar UniqueID");
        }
        const std::size_t row = row_for_uid(uid);
        mppwrite::TaskTable table = mppwrite::TaskTable::from_document(doc_);
        /* +178 is an i32 with exactly one stored copy (reader.h), so the raw
         * row setter is the complete write: no other stream carries it. */
        table.set_u32(row, 178, calendar_uid);
        table.store_into(doc_);
        std::cerr << "engine_diff: set_calendar: uid " << uid << " -> calendar "
                  << calendar_uid << "\n";
    }

    void set_task_field(std::uint32_t uid, std::uint32_t field_id,
                        std::string const& text) {
        const std::size_t row = row_for_uid(uid);
        if (field_id == kFieldDuration) {
            const std::uint32_t ticks = parse_u32(text, "duration ticks");
            if (ticks == 0 || ticks == kInvalidDate) {
                throw Error("set_task_field Duration: " + std::to_string(ticks) +
                            " is not a valid duration");
            }
            duration_override_[uid] = ticks;
            /* COM is given a duration in MINUTES (`task.Duration = "<n>m"`), and
             * Project stores the unit of the edit: duration-change.oracle.json
             * records task 1 DurationFormat 3 (minutes) while the fixture stores
             * 7 (days).  Writing only the ticks left that word at 7. */
            {
                mppwrite::TaskTable table =
                    mppwrite::TaskTable::from_document(doc_);
                table.set_u16(row, mppwrite::task_row::kDurationFormat,
                              mppwrite::kFormatUnitMinutes);
                table.store_into(doc_);
            }
            std::cerr << "engine_diff: set_task_field: uid " << uid
                      << " Duration -> " << ticks << " ticks (and DurationFormat "
                      << mppwrite::kFormatUnitMinutes << ")\n";
            return;
        }
        if (field_id == kFieldConstraintType) {
            const std::uint32_t type = parse_u32(text, "constraint type");
            if (type > 7) {
                throw Error("set_task_field ConstraintType: " +
                            std::to_string(type) + " is outside PjConstraint 0..7");
            }
            pending_[uid].type = static_cast<std::uint16_t>(type);
            pending_[uid].have_type = true;
            flush_constraint(uid);
            return;
        }
        if (field_id == kFieldConstraintDate) {
            PendingConstraint& pending = pending_[uid];
            pending.date = parse_date(text, "constraint date");
            pending.have_date = true;
            flush_constraint(uid);
            return;
        }
        if (field_id == kFieldStart) {
            const std::uint32_t date = parse_date(text, "start");
            mppwrite::pin_task_start(doc_, row, date);
            std::cerr << "engine_diff: set_task_field: uid " << uid
                      << " Start -> SNET " << date_text(date) << "\n";
            return;
        }
        if (field_id == kFieldPercentComplete) {
            const std::uint32_t percent = parse_u32(text, "percent complete");
            if (percent > 100) {
                throw Error("set_task_field PercentComplete: " +
                            std::to_string(percent) + " > 100");
            }
            mppwrite::TaskTable table =
                mppwrite::TaskTable::from_document(doc_);
            table.set_percent_complete(row,
                                       static_cast<std::uint16_t>(percent));
            table.store_into(doc_);
            return;
        }
        if (field_id == kFieldPriority) {
            const std::uint32_t priority = parse_u32(text, "priority");
            if (priority > 1000) {
                throw Error("set_task_field Priority: " +
                            std::to_string(priority) + " > 1000");
            }
            mppwrite::TaskTable table =
                mppwrite::TaskTable::from_document(doc_);
            table.set_priority(row, static_cast<std::uint16_t>(priority));
            table.store_into(doc_);
            return;
        }
        set_custom_field(uid, field_id, text);
    }

    void set_custom_field(std::uint32_t uid, std::uint32_t field_id,
                          std::string const& text) {
        const mppwrite::FieldKind kind = mppwrite::field_kind_for_id(field_id);
        if (kind == mppwrite::FieldKind::unknown) {
            throw Error("set_task_field: field id " + field_id_text(field_id) +
                        " (" + std::to_string(field_id) +
                        ") is not in the documented operation subset; see "
                        "tests/engine_diff_tests.cpp");
        }
        if (kind == mppwrite::FieldKind::text) {
            throw Error("set_task_field: field id " + field_id_text(field_id) +
                        " is a Text field; scenarios carry ids and numbers only");
        }
        const unsigned index = mppwrite::field_index_for_id(field_id);
        if (!mppwrite::field_storage_measured(kind, index)) {
            throw Error("set_task_field: field id " + field_id_text(field_id) +
                        " has no measured file storage");
        }
        mppwrite::FieldValue value;
        value.field_id = field_id;
        value.kind = kind;
        switch (kind) {
            case mppwrite::FieldKind::number:
                value.number = parse_double(text, "custom number");
                break;
            case mppwrite::FieldKind::cost:
                value.cost = parse_double(text, "custom cost");
                break;
            case mppwrite::FieldKind::duration:
                value.duration_ticks = parse_u32(text, "custom duration ticks");
                break;
            case mppwrite::FieldKind::date:
                value.packed_date = parse_date(text, "custom date");
                break;
            case mppwrite::FieldKind::flag:
                value.flag = parse_bool(text, "custom flag");
                break;
            default:
                throw Error("set_task_field: unsupported custom field kind");
        }
        std::vector<mppwrite::FieldEdit> edits;
        edits.push_back(mppwrite::FieldEdit{uid, value});
        const mppwrite::FieldWriteResult result =
            mppwrite::save_field_values(doc_, edits, work_path_);
        report("set_task_field", result.warnings);
    }

    /* A ConstraintType and its ConstraintDate are two single-copy words but one
     * instruction; Project lets a caller set them in either order.  The pair is
     * held until it is complete, then written once.  A date-bearing type with no
     * date is refused by flush_all_constraints at the next recalculate. */
    void flush_constraint(std::uint32_t uid) {
        const auto found = pending_.find(uid);
        if (found == pending_.end()) {
            return;
        }
        PendingConstraint& pending = found->second;
        const std::size_t row = row_for_uid(uid);
        const mppwrite::TaskConstraint current =
            mppwrite::read_task_constraint(doc_, row);
        const std::uint16_t type =
            pending.have_type ? pending.type : current.type;
        std::uint32_t date = pending.have_date ? pending.date : current.date;
        bool ready = false;
        if (type == 0 || type == 1) {
            /* ASAP / ALAP carry no date.  write_task_constraint refuses a
             * non-zero type with an invalid date, and ALAP is exactly that
             * shape in a real file (reader.h: ALAP stores 0xffffffff), so the
             * two no-date types are written as their two single-copy words. */
            mppwrite::TaskTable table =
                mppwrite::TaskTable::from_document(doc_);
            table.set_u16(row, mppwrite::task_row::kConstraintType, type);
            table.set_u32(row, mppwrite::task_row::kConstraintDate,
                          kInvalidDate);
            table.store_into(doc_);
            ready = true;
        } else if (mppwrite::is_date(date)) {
            mppwrite::write_task_constraint(doc_, row,
                                            mppwrite::TaskConstraint{type, date});
            ready = true;
        }
        if (ready) {
            std::cerr << "engine_diff: set_task_field: uid " << uid
                      << " Constraint -> type " << type << " date "
                      << date_text(date == kInvalidDate ? kInvalidDate : date)
                      << "\n";
            pending_.erase(found);
        }
    }

    void flush_all_constraints() {
        std::vector<std::uint32_t> uids;
        for (auto const& entry : pending_) {
            uids.push_back(entry.first);
        }
        for (std::uint32_t const uid : uids) {
            flush_constraint(uid);
        }
        if (!pending_.empty()) {
            const std::uint32_t uid = pending_.begin()->first;
            throw Error("uid " + std::to_string(uid) +
                        ": a date-bearing ConstraintType needs a "
                        "ConstraintDate before the next recalculate/save");
        }
    }

    static std::uint32_t packed(core::sched::date32 date) {
        return core::sched::is_na(date) ? kInvalidDate : date;
    }

    /* Project's own Task.Assignments.Add TOMBSTONES the live null-resource
     * placeholder row of the SAME task (measured: core/mppwrite/add_delete.h
     * "assignment rows", w12-assignadd.mpp).  Our add_assignment_row appends the
     * new row and leaves every existing row byte-identical, so the placeholder
     * survived and the assignment-50pct oracle -- which has no placeholder for
     * task 1 -- differed on that row alone.  This is Project's measured edit
     * behaviour, not a guess: the tombstone is exactly what its own add wrote. */
    void tombstone_null_resource_placeholder(std::uint32_t task_uid) {
        std::string const stream = "114/TBkndAssn/FixedData";
        std::string const meta_stream = "114/TBkndAssn/FixedMeta";
        if (!doc_.has_stream(stream) || !doc_.has_stream(meta_stream)) {
            return;
        }
        std::vector<std::uint8_t> rows;
        std::vector<std::uint8_t> meta;
        try {
            rows = doc_.read_stream(stream);
            meta = doc_.read_stream(meta_stream);
        } catch (std::exception const&) {
            return;
        }
        constexpr std::size_t kRowBytes = 110;      /* add_delete.h: no header */
        constexpr std::size_t kMetaHeader = 16;
        constexpr std::size_t kMetaEntry = 34;
        constexpr std::uint32_t kNullResource = 0xFFFF0001u;
        const auto u32_at = [](std::vector<std::uint8_t> const& b, std::size_t o) {
            return static_cast<std::uint32_t>(b[o]) |
                   (static_cast<std::uint32_t>(b[o + 1]) << 8) |
                   (static_cast<std::uint32_t>(b[o + 2]) << 16) |
                   (static_cast<std::uint32_t>(b[o + 3]) << 24);
        };
        for (std::size_t row = 0; (row + 1) * kRowBytes <= rows.size(); ++row) {
            const std::size_t base = row * kRowBytes;
            if (u32_at(rows, base + 4) != task_uid ||
                u32_at(rows, base + 8) != kNullResource) {
                continue;
            }
            const std::size_t entry = kMetaHeader + row * kMetaEntry;
            if (entry >= meta.size() || meta[entry] == 0x02u) {
                continue; /* no live placeholder for this task */
            }
            const mppwrite::DeletedAssignment deleted = mppwrite::delete_assignment_row(
                doc_, u32_at(rows, base + 0), work_path_);
            report("add_assignment", deleted.warnings);
            std::cerr << "engine_diff: add_assignment: tombstoned the null-resource "
                         "placeholder of task "
                      << task_uid << " (uid " << deleted.uid
                      << "), as Project's Assignments.Add does\n";
            return;
        }
    }

    /* An assignment row has no schedule of its own: Project recomputes its work
     * and dates from its task on every save.  MEASURED: in duration-change the
     * placeholder assignment of task 1 stores work 14400 and finish 01-07 12:00
     * (the edited task's dates), and in link-fs-lag/delete the placeholder of the
     * moved task stores its new Start/Resume/Finish.  The applier wrote the task
     * rows only, so ours kept the fixture's values.  Layout: 110-byte rows, no
     * header (core/mppwrite/add_delete.h "assignment rows"); +12 units x100,
     * +20/+36/+44 work as minutes x1000, +52/+56/+60 Start/Finish/Resume,
     * +104 a second Start copy. */
    void update_assignment_copies(
        std::map<std::int32_t, core::plan::PlanTaskResult const*> const& by_uid,
        std::string const& path) {
        std::string const stream = "114/TBkndAssn/FixedData";
        if (!doc_.has_stream(stream)) {
            return;
        }
        std::vector<std::uint8_t> before;
        try {
            before = doc_.read_stream(stream);
        } catch (std::exception const&) {
            return;
        }
        std::vector<std::uint8_t> rows = before;
        const auto u32_at = [](std::vector<std::uint8_t> const& b, std::size_t o) {
            return static_cast<std::uint32_t>(b[o]) |
                   (static_cast<std::uint32_t>(b[o + 1]) << 8) |
                   (static_cast<std::uint32_t>(b[o + 2]) << 16) |
                   (static_cast<std::uint32_t>(b[o + 3]) << 24);
        };
        const auto f64_at = [](std::vector<std::uint8_t> const& b, std::size_t o) {
            std::uint64_t bits = 0;
            for (int i = 7; i >= 0; --i) {
                bits = (bits << 8) | b[o + static_cast<std::size_t>(i)];
            }
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        };
        const auto put_u32 = [](std::vector<std::uint8_t>& b, std::size_t o,
                                std::uint32_t v) {
            for (int i = 0; i < 4; ++i) {
                b[o + static_cast<std::size_t>(i)] =
                    static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
            }
        };
        const auto put_f64 = [](std::vector<std::uint8_t>& b, std::size_t o, double v) {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &v, sizeof(bits));
            for (int i = 0; i < 8; ++i) {
                b[o + static_cast<std::size_t>(i)] =
                    static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu);
            }
        };
        std::size_t updated = 0;
        for (std::size_t base = 0; base + 110 <= rows.size(); base += 110) {
            const std::uint32_t task_uid = u32_at(rows, base + 4);
            const auto found = by_uid.find(static_cast<std::int32_t>(task_uid));
            if (found == by_uid.end()) {
                continue;
            }
            const core::plan::PlanTaskResult& planned = *found->second;
            const std::uint32_t start = packed(planned.display_start);
            const std::uint32_t finish = packed(planned.display_finish);
            if (start == kInvalidDate || finish == kInvalidDate) {
                continue;
            }
            const double units_x100 = f64_at(rows, base + 12);
            const double fraction = units_x100 > 0.0 ? units_x100 / 10000.0 : 1.0;
            /* FixedData +20 is minutes x1000; the engine's duration is 6-second
             * ticks (600 per minute), so ticks * 100 is minutes x1000. */
            const double work =
                static_cast<double>(planned.duration) * 100.0 * fraction;
            put_f64(rows, base + 20, work);
            put_f64(rows, base + 36, work);
            put_f64(rows, base + 44, work);
            put_u32(rows, base + 52, start);
            put_u32(rows, base + 56, finish);
            put_u32(rows, base + 60, start);
            put_u32(rows, base + 104, start);
            ++updated;
        }
        if (rows == before) {
            return;
        }
        doc_.replace_stream(stream, std::move(rows));
        doc_.save(path);
        std::cerr << "engine_diff: save: maintained the work/dates of " << updated
                  << " assignment row(s) from their tasks\n";
    }

    /* `model_project_save` is set only by the self-test: it is the only pass
     * that applies Project's save-time duration word for a manual MSO row
     * (off_calendar_start_bump).  The intermediate recalculation must keep the
     * plan duration, because its output is re-read by the next pass and a
     * bumped word there would be planned as if it were the duration. */
    void recalculate(std::string const& out_path, bool project_save = false) {
        flush_all_constraints();
        try {
            doc_.save(work_path_);
        } catch (std::exception const& error) {
            throw Error(std::string("cannot write the working file: ") +
                        error.what());
        }
        mpp::Document model;
        try {
            model = mpp::load_file(work_path_);
        } catch (std::exception const& error) {
            throw Error(std::string("our writer produced a file core/mpp "
                                    "cannot read: ") +
                        error.what());
        }
        core::plan::PlanInput input = core::plan::from_mpp(model);
        input.manual_rows_follow_links = manual_follow_links_;
        try {
            const core::cal::CalendarTable table =
                core::cal::decode_file(work_path_);
            input.calendar = core::cal::build_project_calendar(table);
            core::plan::attach_task_calendars(input, table);
        } catch (std::exception const& error) {
            std::cerr << "engine_diff: calendar decode failed; using the stock "
                         "standard week: "
                      << error.what() << "\n";
        }
        for (core::plan::PlanTask& task : input.tasks) {
            const auto found = duration_override_.find(
                static_cast<std::uint32_t>(task.uid));
            if (found != duration_override_.end()) {
                task.duration = static_cast<core::sched::ticks>(found->second);
            }
        }
        const core::plan::PlanResult result = core::plan::recalculate(input);
        if (!result.ok()) {
            throw Error("our engine's recalculation failed: " +
                        std::string(core::sched::to_string(result.error)) + " " +
                        result.message);
        }
        std::map<std::int32_t, core::plan::PlanTaskResult const*> by_uid;
        for (core::plan::PlanTaskResult const& task : result.tasks) {
            by_uid[task.uid] = &task;
        }

        std::vector<mppwrite::PlannedRow> rows;
        const std::uint32_t project_start = packed(result.project_start);
        const std::uint32_t project_finish = packed(result.project_finish);
        if (project_start == kInvalidDate || project_finish == kInvalidDate) {
            throw Error("our engine produced no project start/finish");
        }
        mppwrite::PlannedRow project;
        project.uid = 0;
        project.values = mppwrite::read_task_values(doc_, 0);
        project.values.start = project_start;
        project.values.finish = project_finish;
        project.values.project_start = project_start;
        project.values.project_finish = project_finish;
        project.values.late_start = project_start;
        project.values.late_finish = project_finish;
        /* The project row's Duration (+84).  Project recomputes it as the
         * working time between the project start and finish (MEASURED on the
         * seven oracles: 21600 / 12000 / 40800 / 21600 / 21600 / 21600 / 45600,
         * plan.h PlanResult::project_duration).  Leaving the stored word alone
         * was why a Duration edit moved the oracle's project duration and not
         * ours. */
        project.values.duration =
            static_cast<std::uint32_t>(result.project_duration);
        project.values.remaining_duration = mppwrite::kInherit;
        project.values.actual_duration = mppwrite::kInherit;
        project.values.fixed2_duration = mppwrite::kInherit;
        project.values.total_slack = 0;
        project.values.start_slack = 0;
        rows.push_back(project);

        for (mpp::Task const& task : model.tasks) {
            const auto found = by_uid.find(static_cast<std::int32_t>(task.uid));
            if (found == by_uid.end()) {
                throw Error("our engine returned no schedule for uid " +
                            std::to_string(task.uid));
            }
            const core::plan::PlanTaskResult& planned = *found->second;
            const std::uint32_t start = packed(planned.start);
            const std::uint32_t finish = packed(planned.finish);
            /* The KEPT/effective pair (+104/+108).  For an auto-scheduled row it
             * equals start/finish; for a MANUAL row it does not: Project stores
             * the calculated pair at +96/+100 and keeps the row's own dates at
             * +104/+108 (docs/engine-parity.md "Wave 18", plan.h "MANUAL
             * SCHEDULING").  Writing one pair into both was the applier bug that
             * made a manual scenario unsaveable: the writer refuses a
             * single-copy start write, and overwriting the kept dates on the
             * first save of the two the scenario performs changed what the second
             * recalculation saw. */
            const std::uint32_t kept_start = packed(planned.display_start);
            const std::uint32_t kept_finish = packed(planned.display_finish);
            std::uint32_t late_start = packed(planned.late_start);
            std::uint32_t late_finish = packed(planned.late_finish);
            if (start == kInvalidDate || finish == kInvalidDate ||
                kept_start == kInvalidDate || kept_finish == kInvalidDate) {
                throw Error("our engine produced no start/finish for uid " +
                            std::to_string(task.uid));
            }
            if (late_start == kInvalidDate || late_finish == kInvalidDate) {
                throw Error("our engine produced no late start/finish for uid " +
                            std::to_string(task.uid));
            }
            mppwrite::PlannedRow row;
            row.uid = task.uid;
            row.values = mppwrite::read_task_values(doc_, task.row);
            /* The stored values, before this row is overwritten. */
            mppwrite::TaskValues const stored = row.values;
            bool const dates_move =
                start != stored.start || finish != stored.finish ||
                kept_start != stored.project_start ||
                kept_finish != stored.project_finish;
            row.values.manual_dates = planned.manually_scheduled;
            row.values.start = start;              /* +96  calculated */
            row.values.finish = finish;            /* +100 calculated */
            row.values.project_start = kept_start;   /* +104 effective */
            row.values.project_finish = kept_finish; /* +108 effective */
            row.values.start_anchor =
                planned.start_anchor == core::sched::kDateNa
                    ? mppwrite::kInherit                /* an auto row: keep stored */
                    : packed(planned.start_anchor);     /* +174 a manual row's start */
            /* The stored late pair (+112/+116).
             * A MANUAL row: Project stores the row PLACED AT ITS LATEST, not the
             * CPM late pair and not a copy of the kept dates (recalculate uid 2
             * keeps a 01-04 13:00 start and stores 01-06 13:00 here).  The
             * planner computes that pair by the measured rule (plan.h "MANUAL
             * LATE PAIR"), so it is written like any other planned field; the
             * row's manual_dates flag lets the writer hold the split.  Wave 20
             * fixed the four scenarios that compared this field: the applier used
             * to keep the stored pair on every manual row and wrote the CPM pair
             * whenever a row's dates moved, which was right in neither frame.
             * An unpinned manual row (no stored start, elapsed duration, actuals)
             * has no measured rule: keep the stored pair, as before.
             * An AUTO row: write the CPM pair only when its dates move with it.
             * Project recomputes +112/+116 on open, and a late date that moves
             * alone is the single-copy split the writer refuses (measured wave 19
             * on a Project-written auto row whose stored late pair predates the
             * fixture's conversion). */
            if (planned.manually_pinned ||
                (!planned.manually_scheduled && dates_move)) {
                row.values.late_start = late_start;
                row.values.late_finish = late_finish;
            }
            /* Project's save-time duration WORD for a manual row carrying MSO
             * and starting on a non-working day (off_calendar_start_bump).  It
             * moves +84 only; the dates, late pair and slack above are the
             * plan's, which is the incoherent state Project itself saves. */
            std::uint32_t duration_word =
                static_cast<std::uint32_t>(planned.duration);
            if (project_save && !planned.summary && planned.manually_pinned &&
                stored.constraint_type == kConstraintMso &&
                mppwrite::is_date(stored.constraint_date)) {
                core::sched::ticks const bump = off_calendar_start_bump(
                    calendar_for(input, planned.calendar_uid), kept_start);
                if (bump > 0) {
                    duration_word += static_cast<std::uint32_t>(bump);
                    std::cerr << "engine_diff: save: uid " << task.uid
                              << " duration word gains " << bump
                              << " ticks: the remaining work windows of the "
                                 "off-calendar start "
                              << date_text(kept_start)
                              << " (Project's save-time rule)\n";
                }
            }
            row.values.duration = duration_word;
            row.values.remaining_duration = mppwrite::kInherit;
            row.values.actual_duration = mppwrite::kInherit;
            /* The writer stores slack as i32 ticks; the engine's tick type is
             * int64.  A slack beyond i32 is not a value the file can hold.
             * The THREE words are distinct (W30): +74 FreeSlack, +186 StartSlack,
             * +190 TotalSlack.  The writer used to put TotalSlack at +74, which
             * the reader reports as FreeSlack, so free could never differ from
             * total; each word is now passed separately. */
            row.values.free_slack =
                static_cast<std::int32_t>(planned.free_slack);
            row.values.total_slack =
                static_cast<std::int32_t>(planned.total_slack);
            row.values.start_slack =
                static_cast<std::int32_t>(planned.start_slack);
            row.values.fixed2_start = mppwrite::kInherit;
            row.values.fixed2_finish = mppwrite::kInherit;
            row.values.fixed2_duration = mppwrite::kInherit;
            rows.push_back(row);
        }

        const mppwrite::SaveResult saved =
            mppwrite::save_plan(doc_, rows, out_path);
        for (std::string const& warning : saved.warnings) {
            std::cerr << "engine_diff: save: " << warning << "\n";
        }
        update_assignment_copies(by_uid, out_path);
        std::cerr << "engine_diff: recalculate: wrote " << saved.rows_written
                  << " row(s) to " << out_path << "\n";
    }

    mppwrite::CfbDocument doc_;
    std::string base_path_;
    std::string out_path_;
    std::string work_path_;
    std::string base_sha256_before_;
    bool saved_ = false;
    std::map<std::uint32_t, std::uint32_t> duration_override_;
    std::map<std::uint32_t, PendingConstraint> pending_;
    /* Set for the one recalculation that follows an add_link: Project moves a
     * manually scheduled successor when the link is ADDED (measured; see
     * core/plan/plan.h PlanInput::manual_rows_follow_links). */
    bool manual_follow_links_ = false;
    /* Self-test only: model Project's save-time duration word.  Never set by
     * the scenario runner (see recalculate). */
    bool model_project_save_ = false;
};

/* ------------------------------------------------------------------------- */
/* self-test                                                                 */
/* ------------------------------------------------------------------------- */

class TestContext {
  public:
    void check(bool condition, std::string const& what) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::cout << "FAIL: " << what << "\n";
        }
    }
    template <typename T, typename U>
    void equal(T const& actual, U const& expected, std::string const& what) {
        std::ostringstream message;
        message << what << " (actual " << actual << ", expected " << expected
                << ")";
        check(actual == expected, message.str());
    }
    int report() const {
        std::cout << checks_ << " checks, " << failures_ << " failures\n";
        return failures_ == 0 ? 0 : 1;
    }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

std::string join_path(std::string const& directory, std::string const& name) {
    return (std::filesystem::path(directory) / name).string();
}

void write_text(std::string const& path, std::string const& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw Error("cannot write " + path);
    }
    out << text;
    if (!out) {
        throw Error("write error on " + path);
    }
}

std::string read_text(std::string const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw Error("cannot read " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

mpp::Task const* find_task(mpp::Document const& document, std::uint32_t uid) {
    for (mpp::Task const& task : document.tasks) {
        if (task.uid == uid) {
            return &task;
        }
    }
    return nullptr;
}

void check_late_pair(TestContext& context, mpp::Document const& document,
                     std::uint32_t uid, char const* late_start,
                     char const* late_finish, std::string const& label) {
    mpp::Task const* task = find_task(document, uid);
    const std::string which = label + " uid " + std::to_string(uid);
    context.check(task != nullptr, which + " exists");
    if (task == nullptr) {
        return;
    }
    context.equal(mpp::format_date(task->late_start), std::string(late_start),
                  which + " stored late start (+112)");
    context.equal(mpp::format_date(task->late_finish), std::string(late_finish),
                  which + " stored late finish (+116)");
}

void dump_check(TestContext& context, std::string const& mpp_path,
                std::string const& label) {
    const std::string json_path = mpp_path + ".json";
    dump_file(mpp_path, json_path);
    const std::string text = read_text(json_path);
    context.check(text.find("\"schema\": \"engine-diff/2\"") != std::string::npos,
                  label + ": the dump carries the schema line");
    for (char const* family :
         {"project", "tasks", "links", "assignments", "resources",
          "calendar_table", "file"}) {
        context.check(text.find(std::string("\"") + family + "\"") !=
                          std::string::npos,
                      label + ": the dump carries the " + family + " family");
    }
    /* Schema 2 adds the stored late pair on every task and the resource calendar
     * uid.  The pair is a stored field, not a copy of start/finish: on
     * synthetic-base, uid 2 stores 2026-01-06 13:00 at +112 while its start is
     * 2026-01-04 13:00 (the base block below pins the measured values; each
     * built-in case pins the pair it must store). */
    for (char const* key : {"late_start", "late_finish"}) {
        context.check(
            text.find(std::string("\"") + key + "\": \"20") != std::string::npos,
            label + ": the dump carries a non-empty " + key +
                " (the stored +112/+116 late pair)");
    }
    context.check(text.find("\"calendar_uid\": 3") != std::string::npos,
                  label + ": the resource calendar uid is derived from the "
                          "calendar table (resource 1 -> calendar 3)");
    context.check(text.find("\"calendar_uid\": -1") != std::string::npos,
                  label + ": a task with no task calendar still reports -1 (+178)");
    for (char const* name : {"\"Task A\"", "\"Task B\"", "\"Task C\"",
                             "\"Crew\"", "Task A", "Task B", "Task C", "Crew"}) {
        context.check(text.find(name) == std::string::npos,
                      label + ": the dump carries no task/resource name (" +
                          std::string(name) + ")");
    }
    context.check(text.find("\"streams\"") != std::string::npos,
                  label + ": the dump carries the file stream sizes");
}

struct BuiltInCase {
    char const* name;
    char const* operations;
};

/* ------------------------------------------------------------------------- */
/* constraint-mso: the nine-row regression                                   */
/*                                                                           */
/* docs/engine-parity.md "Wave 20" records nine measurements of Project's     */
/* stored +84 word on synthetic-base task 3 (2 d) unless noted.  The five rows */
/* the applier can reach end-to-end (the fixture already starts task 3 on the */
/* off-calendar Sunday 13:00, and a Duration edit reaches the other planned   */
/* durations) are asserted from the saved file; the four starts a manual row  */
/* cannot be moved to (row_writer.h: the manual Start write path is not       */
/* supported) are asserted against the rule itself, together with the table's */
/* stored total.  The no-constraint control is end-to-end.                   */
/* ------------------------------------------------------------------------- */
void check_mso_duration_table(TestContext& context, std::string const& fixture,
                              std::string const& out_dir) {
    core::sched::Calendar calendar;
    try {
        core::cal::CalendarTable const table = core::cal::decode_file(fixture);
        calendar = core::cal::build_project_calendar(table);
    } catch (std::exception const& error) {
        context.check(false,
                      std::string("mso table: cannot build the fixture's "
                                  "calendar: ") +
                          error.what());
        return;
    }

    /* 2026-01-04 is a Sunday, 01-03 a Saturday, 01-02 a Friday. */
    struct IncrementRow {
        std::int32_t year;
        std::int32_t month;
        std::int32_t day;
        std::int32_t hour;
        std::int32_t minute;
        core::sched::ticks increment;     /* table "increment" */
        core::sched::ticks plan_duration; /* the row's Duration */
        std::uint32_t stored_total;       /* table "stored duration" */
        char const* label;
    };
    IncrementRow const rows[] = {
        {2026, 1, 4, 13, 0, 2400, 9600, 12000, "Sun 13:00, 2 d"},
        {2026, 1, 4, 13, 0, 2400, 4800, 7200, "Sun 13:00, 1 d"},
        {2026, 1, 4, 8, 0, 4800, 4800, 9600, "Sun 08:00, 1 d"},
        {2026, 1, 4, 10, 0, 3600, 4800, 8400, "Sun 10:00, 1 d"},
        {2026, 1, 4, 15, 0, 1200, 4800, 6000, "Sun 15:00, 1 d"},
        {2026, 1, 3, 9, 0, 4200, 4800, 9000, "Sat 09:00, 1 d"},
        {2026, 1, 2, 13, 0, 0, 4800, 4800, "Fri 13:00, 1 d (working day)"},
        {2026, 1, 4, 13, 0, 2400, 14400, 16800, "task 2, 3 d, Sun 13:00"},
        {2026, 1, 4, 13, 0, 2400, 24000, 26400, "task 1, 5 d, Sun 13:00"},
    };
    for (IncrementRow const& row : rows) {
        core::sched::date32 const start = core::sched::from_ymd(
            row.year, row.month, row.day, row.hour, row.minute);
        core::sched::ticks const bump = off_calendar_start_bump(calendar, start);
        std::string const label = std::string("mso table: ") + row.label;
        context.equal(bump, row.increment, label + " increment");
        context.equal(static_cast<std::uint32_t>(row.plan_duration + bump),
                      row.stored_total, label + " stored duration");
    }

    struct ApplyRow {
        char const* name;
        std::uint32_t uid;
        char const* duration_ticks; /* "" keeps the fixture row's duration */
        bool mso;
        std::uint32_t expected; /* the stored +84 word */
    };
    ApplyRow const cases[] = {
        {"mso-sun13-2d", 3, "", true, 12000},
        {"mso-sun13-1d", 3, "4800", true, 7200},
        {"mso-task2-3d", 2, "14400", true, 16800},
        {"mso-task1-5d", 1, "24000", true, 26400},
        {"mso-control", 3, "", false, 9600},
    };
    for (ApplyRow const& test_case : cases) {
        std::string const stem =
            join_path(out_dir, std::string("selftest-") + test_case.name);
        std::string const base = stem + "-base.mpp";
        std::string const ops = stem + ".ops";
        std::string const out = stem + ".mpp";
        try {
            std::filesystem::copy_file(
                fixture, base,
                std::filesystem::copy_options::overwrite_existing);
            std::string text;
            std::string const uid = std::to_string(test_case.uid);
            if (test_case.duration_ticks[0] != '\0') {
                text += "set_task_field\t" + uid + "\t188743709\t" +
                        test_case.duration_ticks + "\n";
            }
            if (test_case.mso) {
                text += "set_task_field\t" + uid + "\t188743697\t2\n";
                text += "set_task_field\t" + uid +
                        "\t188743698\t2026-01-20 08:00\n";
            }
            text += "recalculate\nsave\n";
            write_text(ops, text);
            Applier applier(base, out);
            /* This table is the measured rule's end-to-end regression: opt in
             * to modelling Project's save-time word.  The scored scenarios do
             * not (they compare the production save path). */
            applier.model_project_save(true);
            applier.run(read_operations(ops));
            mpp::Document const document = mpp::load_file(out);
            mpp::Task const* task = find_task(document, test_case.uid);
            std::string const label =
                std::string("mso table: ") + test_case.name;
            context.check(task != nullptr, label + ": the row exists");
            if (task != nullptr) {
                context.equal(task->duration_ticks, test_case.expected,
                              label + " stored duration (+84)");
            }
        } catch (std::exception const& error) {
            context.check(false, std::string("mso table: ") +
                                     test_case.name + " threw " +
                                     error.what());
        }
    }
}

int run_self_test() {
    TestContext context;
    const std::string fixture =
        join_path(ENGINE_DIFF_TEST_DATA, "synthetic-base.mpp");
    const std::string out_dir = ENGINE_DIFF_TEST_OUT;
    std::error_code error;
    std::filesystem::create_directories(out_dir, error);
    if (error) {
        std::cerr << "engine_diff: cannot create " << out_dir << ": "
                  << error.message() << "\n";
        return 1;
    }
    context.check(std::filesystem::is_regular_file(fixture),
                  "the committed fixture exists: " + fixture);

    /* ---- the SHA-256 implementation the base guard rides on --------------- */
    context.check(sha256::of_bytes("") ==
                      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca"
                      "495991b7852b855",
                  "SHA-256 of the empty string");
    context.check(sha256::of_bytes("abc") ==
                      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb"
                      "410ff61f20015ad",
                  "SHA-256 of 'abc'");
    context.check(sha256::of_bytes(
                      "The quick brown fox jumps over the lazy dog") ==
                      "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d"
                      "02d0bf37c9e592",
                  "SHA-256 of the quick-brown-fox sentence");
    const std::string fixture_sha256_before = sha256::of_file(fixture);
    context.check(fixture_sha256_before.size() == 64,
                  "the base fixture hashes to 64 hex digits");

    /* A wrong expected hash is refused before anything is written, and a run
     * never changes its base.  Both are the guards that keep a corrupted base
     * from silently producing an oracle. */
    {
        const std::string guard_base =
            join_path(out_dir, "selftest-guard-base.mpp");
        const std::string guard_out =
            join_path(out_dir, "selftest-guard-out.mpp");
        const std::string guard_ops =
            join_path(out_dir, "selftest-guard.ops");
        std::filesystem::copy_file(
            fixture, guard_base,
            std::filesystem::copy_options::overwrite_existing);
        write_text(guard_ops, "recalculate\nsave\n");
        bool refused = false;
        try {
            Applier applier(guard_base, guard_out,
                            "000000000000000000000000000000000000000000000000"
                            "0000000000000000");
            applier.run(read_operations(guard_ops));
        } catch (Error const& error) {
            refused = std::string(error.what()).find("SHA-256 mismatch") !=
                      std::string::npos;
        }
        context.check(refused,
                      "a wrong expected base hash is refused before writing");
        context.check(!std::filesystem::exists(guard_out),
                      "a refused base-hash run leaves no output file");
        context.check(sha256::of_file(guard_base) == fixture_sha256_before,
                      "the guarded base copy is unchanged");
    }

    const std::vector<BuiltInCase> cases = {
        {"recalculate", "recalculate\nsave\n"},
        {"duration", "set_task_field\t1\t188743709\t14400\nrecalculate\nsave\n"},
        {"link", "add_link\t1\t2\t1\t4800\nrecalculate\nsave\n"},
        {"constraint-snet",
         "set_task_field\t2\t188743697\t4\n"
         "set_task_field\t2\t188743698\t2026-02-02 08:00\n"
         "recalculate\nsave\n"},
        {"assignment",
         "add_assignment\t1\t1\t50\nrecalculate\nsave\n"},
        {"calendar", "set_calendar\t1\t2\nrecalculate\nsave\n"},
        {"delete",
         "add_link\t1\t2\t1\t0\nadd_link\t2\t3\t1\t0\n"
         "delete_task\t2\nrecalculate\nsave\n"},
    };

    for (BuiltInCase const& test_case : cases) {
        const std::string base_path =
            join_path(out_dir, std::string("selftest-") + test_case.name +
                                    "-base.mpp");
        const std::string ops_path =
            join_path(out_dir, std::string("selftest-") + test_case.name + ".ops");
        const std::string out_path =
            join_path(out_dir, std::string("selftest-") + test_case.name + ".mpp");
        try {
            std::filesystem::copy_file(fixture, base_path,
                                       std::filesystem::copy_options::overwrite_existing);
            write_text(ops_path, test_case.operations);
            Applier applier(base_path, out_path, fixture_sha256_before);
            applier.run(read_operations(ops_path));
            mpp::Document const document = mpp::load_file(out_path);
            const std::string name = test_case.name;
            if (name == "recalculate") {
                context.equal(document.tasks.size(), std::size_t{3},
                              "recalculate: three live tasks");
                mpp::Task const* task = find_task(document, 1);
                context.check(task != nullptr, "recalculate: uid 1 exists");
                if (task != nullptr) {
                    context.equal(mpp::format_date(task->start),
                                  std::string("2026-01-04 13:00"),
                                  "recalculate: uid 1 start is the kept manual date "
                                  "(the fixture rows are manually scheduled)");
                    context.equal(mpp::format_date(task->finish),
                                  std::string("2026-01-09 12:00"),
                                  "recalculate: uid 1 finish is 5 days later, charged "
                                  "from the start's clock to the end of the working day");
                    context.equal(task->duration_ticks, std::uint32_t{24000},
                                  "recalculate: uid 1 duration is 5 days");
                }
                /* THE MEASURED MANUAL LATE PAIR (plan.h "MANUAL LATE PAIR"):
                 * the row placed at its latest.  uid 2's +112 is 01-06 13:00, not
                 * its kept 01-04 13:00 and not a CPM date. */
                check_late_pair(context, document, 1, "2026-01-04 13:00",
                                "2026-01-09 12:00", name);
                check_late_pair(context, document, 2, "2026-01-06 13:00",
                                "2026-01-09 12:00", name);
                check_late_pair(context, document, 3, "2026-01-07 13:00",
                                "2026-01-09 12:00", name);
            } else if (name == "duration") {
                mpp::Task const* task = find_task(document, 1);
                context.check(task != nullptr, "duration: uid 1 exists");
                if (task != nullptr) {
                    context.equal(task->duration_ticks, std::uint32_t{14400},
                                  "duration: uid 1 Duration is 14400 ticks");
                    context.equal(mpp::format_date(task->finish),
                                  std::string("2026-01-07 12:00"),
                                  "duration: uid 1 finish follows the new "
                                  "duration");
                }
                /* The duration-change oracle's stored late pair: the project
                 * finish moved to 01-07 12:00 and uid 3's latest start is
                 * 01-05 13:00 (the row placed at that finish).  The old applier
                 * kept the fixture's 01-09 12:00 here, which is the wave-20
                 * duration-change failure in one direction. */
                check_late_pair(context, document, 1, "2026-01-04 13:00",
                                "2026-01-07 12:00", name);
                check_late_pair(context, document, 2, "2026-01-04 13:00",
                                "2026-01-07 12:00", name);
                check_late_pair(context, document, 3, "2026-01-05 13:00",
                                "2026-01-07 12:00", name);
            } else if (name == "link") {
                context.equal(document.links.size(), std::size_t{1},
                              "link: one live link");
                if (document.links.size() == 1) {
                    mpp::Link const& link = document.links.front();
                    context.equal(link.predecessor_uid, std::uint32_t{1},
                                  "link: predecessor uid");
                    context.equal(link.successor_uid, std::uint32_t{2},
                                  "link: successor uid");
                    context.equal(static_cast<int>(link.type()), 1,
                                  "link: type is FS");
                    context.equal(link.lag_ticks, 4800, "link: lag is 1 day");
                }
                mpp::Task const* first = find_task(document, 1);
                mpp::Task const* second = find_task(document, 2);
                context.check(first != nullptr && second != nullptr,
                              "link: both endpoint tasks exist");
                if (first != nullptr && second != nullptr) {
                    context.check(second->start.raw > first->finish.raw,
                                  "link: the successor starts after the "
                                  "predecessor finishes");
                }
                /* FS+1d: uid 1's stored late finish is 01-09 13:00 -- uid 2's
                 * KEPT start 01-12 13:00 minus the lag under the calendar walk.
                 * Aligning that bound back to a finish instant would give 12:00,
                 * which is the wave-20 link-fs-lag failure in the other
                 * direction.  uid 2's own latest pair is 01-12 13:00 ->
                 * 01-15 12:00. */
                check_late_pair(context, document, 1, "2026-01-04 13:00",
                                "2026-01-09 13:00", name);
                check_late_pair(context, document, 2, "2026-01-12 13:00",
                                "2026-01-15 12:00", name);
            } else if (name == "constraint-snet") {
                mpp::Task const* task = find_task(document, 2);
                context.check(task != nullptr, "constraint: uid 2 exists");
                if (task != nullptr) {
                    context.equal(task->constraint_type, std::uint16_t{4},
                                  "constraint: uid 2 ConstraintType is SNET");
                    context.equal(mpp::format_date(task->constraint_date),
                                  std::string("2026-02-02 08:00"),
                                  "constraint: the SNET date is stored");
                    context.equal(mpp::format_date(task->start),
                                  std::string("2026-01-04 13:00"),
                                  "constraint: a manual row stores the SNET date but "
                                  "keeps its own dates (Project does not reschedule it)");
                    /* NEGATIVE CONTROL for Project's save-time duration word:
                     * the same off-calendar Sunday start with an SNET
                     * constraint does NOT gain the remaining day windows
                     * (constraint-snet.oracle.json stores 14400, not 16800). */
                    context.equal(task->duration_ticks, std::uint32_t{14400},
                                  "constraint: an SNET row's stored duration is "
                                  "unchanged (the save-time bump is MSO-only)");
                }
                /* A manual row's own constraint does not move its stored late pair
                 * either: uid 2's +112/+116 are the same as in recalculate. */
                check_late_pair(context, document, 2, "2026-01-06 13:00",
                                "2026-01-09 12:00", name);
            } else if (name == "assignment") {
                // The applier now tombstones the live null-resource placeholder
                // exactly as Project's Assignments.Add does (E2, wave 18), so the
                // fixture's three placeholder rows become two live placeholders
                // plus the added assignment.
                context.equal(document.assignments.size(), std::size_t{3},
                              "assignment: two live placeholders plus the new row");
                mpp::Assignment const* added = nullptr;
                for (mpp::Assignment const& assignment : document.assignments) {
                    if (assignment.task_uid == 1 &&
                        assignment.resource_uid() == 1) {
                        added = &assignment;
                    }
                }
                context.check(added != nullptr,
                              "assignment: task 1 / resource 1 exists");
                if (added != nullptr) {
                    context.equal(added->units_percent(), std::uint32_t{50},
                                  "assignment: units are 50 percent");
                    context.equal(added->work_ticks(), std::uint32_t{12000},
                                  "assignment: work is half the task duration");
                }
                check_late_pair(context, document, 1, "2026-01-04 13:00",
                                "2026-01-09 12:00", name);
            } else if (name == "calendar") {
                mpp::Task const* task = find_task(document, 1);
                context.check(task != nullptr, "calendar: uid 1 exists");
                if (task != nullptr) {
                    context.equal(task->calendar_uid, std::int32_t{2},
                                  "calendar: uid 1 names calendar 2");
                }
                check_late_pair(context, document, 1, "2026-01-04 13:00",
                                "2026-01-09 12:00", name);
            } else if (name == "delete") {
                context.equal(document.tasks.size(), std::size_t{2},
                              "delete: two live tasks remain");
                context.check(find_task(document, 2) == nullptr,
                              "delete: uid 2 is gone");
                context.check(find_task(document, 1) != nullptr &&
                                  find_task(document, 3) != nullptr,
                              "delete: uid 1 and uid 3 remain");
                context.check(document.links.empty(),
                              "delete: the links naming uid 2 are gone");
                for (mpp::Assignment const& assignment :
                     document.assignments) {
                    context.check(assignment.task_uid != 2,
                                  "delete: no live assignment names uid 2");
                }
                /* After the delete the project finish is uid 3's 01-16 12:00, so
                 * BOTH remaining manual rows are placed at that finish: uid 1
                 * (5 d) stores 01-09 13:00 / 01-16 12:00 and uid 3 (2 d)
                 * 01-14 13:00 / 01-16 12:00.  The old applier kept the fixture's
                 * 01-09 12:00 pair, which is the wave-20 delete failure (ours
                 * EARLIER by a week). */
                check_late_pair(context, document, 1, "2026-01-09 13:00",
                                "2026-01-16 12:00", name);
                check_late_pair(context, document, 3, "2026-01-14 13:00",
                                "2026-01-16 12:00", name);
            }
            dump_check(context, out_path, name);
        } catch (std::exception const& failure) {
            context.check(false, std::string(test_case.name) +
                                     ": threw " + failure.what());
        }
    }
    /* constraint-mso's measured save-time duration word, with the nine-row
     * table as the regression (docs/engine-parity.md "Wave 20"). */
    check_mso_duration_table(context, fixture, out_dir);
    /* ---- the late pair is a SEPARATE stored field, not a date copy --------
     * Dumping the fixture itself (to build/, never over it) proves the new
     * leaves carry values that are not derivable from start/finish: uid 2's
     * +112 is 2026-01-06 13:00 while its start is 2026-01-04 13:00, and uid 3's
     * is 2026-01-07 13:00.  The resource calendar uid (3) is the TBkndCal row
     * whose resource column is 1; Project's MSPDI export of the same fixture
     * family writes that as Resource.CalendarUID (w9-resourcetypes: 3/4/5). */
    {
        const std::string base_json = join_path(out_dir, "selftest-base.json");
        dump_file(fixture, base_json);
        const std::string text = read_text(base_json);
        context.check(text.find("\"schema\": \"engine-diff/2\"") !=
                          std::string::npos,
                      "base: the dump carries the schema line");
        context.check(text.find("\"late_start\": \"2026-01-04 13:00:00+00:00\"") !=
                          std::string::npos,
                      "base: uid 1 late start is its stored +112 word");
        context.check(text.find("\"late_start\": \"2026-01-06 13:00:00+00:00\"") !=
                          std::string::npos,
                      "base: uid 2 late start (+112) is not a copy of its start");
        context.check(text.find("\"late_start\": \"2026-01-07 13:00:00+00:00\"") !=
                          std::string::npos,
                      "base: uid 3 late start (+112) is not a copy of its start");
        context.check(text.find("\"late_finish\": \"2026-01-09 12:00:00+00:00\"") !=
                          std::string::npos,
                      "base: the late finish (+116) is the project finish");
        context.check(text.find("\"calendar_uid\": 3") != std::string::npos,
                      "base: the resource's calendar uid is 3");
        for (char const* name : {"Task A", "Task B", "Task C", "Crew"}) {
            context.check(text.find(name) == std::string::npos,
                          "base: the dump carries no name (" + std::string(name) +
                              ")");
        }
    }
    context.check(sha256::of_file(fixture) == fixture_sha256_before,
                  "the committed base fixture is byte-identical after every "
                  "self-test run (all writes went to build/)");
    return context.report();
}

int usage() {
    std::cerr
        << "usage: engine_diff_tests\n"
        << "       engine_diff_tests --apply <ops.tsv> <base.mpp> <out.mpp>\n"
        << "                          [<expected-base-sha256>]\n"
        << "       engine_diff_tests --dump <file.mpp> <out.json>\n"
        << "\n"
        << "The self-test (no arguments) needs the committed fixture at\n"
        << ENGINE_DIFF_TEST_DATA "/synthetic-base.mpp and writes under\n"
        << ENGINE_DIFF_TEST_OUT ".\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            return run_self_test();
        }
        const std::string mode = argv[1];
        if (mode == "-h" || mode == "--help") {
            usage();
            return 0;
        }
        if (mode == "--apply") {
            if (argc != 5 && argc != 6) {
                return usage();
            }
            const std::string expected =
                argc == 6 ? std::string(argv[5]) : std::string();
            Applier applier(argv[3], argv[4], expected);
            applier.run(read_operations(argv[2]));
            return 0;
        }
        if (mode == "--dump") {
            if (argc != 4) {
                return usage();
            }
            dump_file(argv[2], argv[3]);
            return 0;
        }
        std::cerr << "engine_diff_tests: unknown mode '" << mode << "'\n";
        return usage();
    } catch (std::exception const& failure) {
        std::cerr << "engine_diff_tests: " << failure.what() << "\n";
        return 1;
    }
}
