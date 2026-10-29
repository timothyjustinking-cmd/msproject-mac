/*
 * fuzz_xml_main.cpp -- deterministic mutation fuzz for the MSPDI importer and
 * the CSV export surface.
 *
 * WHAT IS FUZZED.  The three committed hand-written MSPDI samples
 * (tests/io_sample_minimal.xml, io_sample_tasks.xml, io_sample_full.xml) are
 * the seeds.  Every case picks one seed, copies the bytes, applies 1..4 seeded
 * mutations, and calls `core::io::import_mspdi`.  Mutation families:
 *
 *   0  a random byte bit flip
 *   1  a zero run at a random offset
 *   2  a 0xFF run at a random offset
 *   3  truncation to a random length
 *   4  a random byte run inside the document
 *   5  one byte replaced by XML punctuation ('<', '>', '&', '"', '\'', '/',
 *      '?', '!') or a saturated value (0x00/0x80/0xFF)
 *
 * THE CONTRACT.  For every case the importer must EITHER throw
 * `core::io::MspdiError` OR return a document whose record counts, warning
 * count and text lengths are bounded by the input size.  Any other exception
 * type, a crash, a timeout or an out-of-bound count is a failure.  ASan turns
 * an out-of-bounds read into a process abort, which the child-process watchdog
 * reports with the seed that was running.
 *
 * CSV: THERE IS NO CSV IMPORTER IN THIS TREE.  core/io/csv.h is export-only
 * (csv_field, csv_cell, export_csv, export_csv_rows, write_csv); the only CSV
 * files are csv.cpp, csv.h and tests/io_csv_tests.cpp, and a grep for
 * read_csv/import_csv/parse_csv/from_csv finds nothing.  The audit's "CSV
 * importer" therefore cannot be fed mutated bytes.  The CSV surface that does
 * exist is the exporter, so every successfully imported document is exported
 * through export_csv below and its output is checked as RFC 4180 with a
 * constant column count -- mutated XML text values reach the quoting code.
 *
 * USAGE
 *   fuzz_xml [--cases N] [--seed N] [--case N] [--fixture PATH]
 *            [--timeout-ms N] [--no-watchdog] [--progress PATH]
 *
 * Exit code 0 = every case satisfied the contract.  A failure prints the exact
 * case seed and mutation list; `--case N` replays that one case.
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "csv.h"
#include "fuzz_common.h"
#include "fuzz_watchdog.h"
#include "mspdi.h"
#include "record_set.h"
#include "table.h"
#include "value.h"

#ifndef FUZZ_IO_SAMPLE_DIR
#define FUZZ_IO_SAMPLE_DIR "tests"
#endif
#ifndef FUZZ_OUT_DIR
#define FUZZ_OUT_DIR "build/fuzz-out"
#endif

namespace {

using fuzz::Bytes;
using Clock = std::chrono::steady_clock;

char const* const kSeedNames[] = {
    "io_sample_minimal.xml",
    "io_sample_tasks.xml",
    "io_sample_full.xml",
};
constexpr unsigned kSeedCount = 3;
constexpr unsigned kMutationKinds = 6;
constexpr double kSlowCaseMs = 2000.0;

/* CSV columns: enough kinds (text, date, duration, number, integer, bool) that
 * a mutated value reaches the exporter's formatting and quoting code. */
constexpr core::view::FieldId kName = 0x0b40000e;
constexpr core::view::FieldId kNotes = 0x0b40000f;
constexpr core::view::FieldId kStart = 0x0b400023;
constexpr core::view::FieldId kDuration = 0x0b40001d;
constexpr core::view::FieldId kCost = 0x0b400005;
constexpr core::view::FieldId kPercentComplete = 0x0b400020;
constexpr core::view::FieldId kSummary = 0x0b40005c;

struct Options {
    unsigned cases = 240;
    std::uint64_t seed = 0x58;
    bool single_case = false;
    unsigned case_index = 0;
    std::string fixture;
    bool watchdog = true;
    unsigned timeout_ms = 30000;
    std::string progress;
    bool worker = false;
};

void print_usage() {
    std::cout
        << "usage: fuzz_xml [--cases N] [--seed N] [--case N] "
           "[--fixture PATH]\n"
           "                [--timeout-ms N] [--no-watchdog] "
           "[--progress PATH]\n"
           "  --cases N       number of mutated cases (default 240)\n"
           "  --seed N        base seed, decimal or 0x-hex (default 0x58)\n"
           "  --case N        run exactly that one case (for a failure repro)\n"
           "  --fixture PATH  one XML seed instead of the three committed ones\n"
           "  --no-watchdog   run in-process (a hang then blocks this process)\n";
}

bool parse_u64(std::string const& text, std::uint64_t& out) {
    if (text.empty()) {
        return false;
    }
    unsigned base = 10;
    std::size_t index = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        index = 2;
    }
    std::uint64_t value = 0;
    for (; index < text.size(); ++index) {
        char const ch = text[index];
        unsigned digit = 0;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<unsigned>(ch - '0');
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = static_cast<unsigned>(ch - 'a') + 10u;
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = static_cast<unsigned>(ch - 'A') + 10u;
        } else {
            return false;
        }
        value = value * base + digit;
    }
    out = value;
    return true;
}

bool take_value(int argc, char** argv, int& index, std::string& out) {
    if (index + 1 >= argc) {
        return false;
    }
    out = argv[++index];
    return true;
}

bool parse_options(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        std::string const argument = argv[i];
        std::string value;
        if (argument == "--worker") {
            options.worker = true;
        } else if (argument == "--help" || argument == "-h") {
            print_usage();
            std::exit(0);
        } else if (argument == "--no-watchdog") {
            options.watchdog = false;
        } else if (argument == "--cases") {
            std::uint64_t parsed = 0;
            if (!take_value(argc, argv, i, value) || !parse_u64(value, parsed) ||
                parsed == 0 || parsed > 1000000) {
                error = "bad --cases value";
                return false;
            }
            options.cases = static_cast<unsigned>(parsed);
        } else if (argument == "--case") {
            std::uint64_t parsed = 0;
            if (!take_value(argc, argv, i, value) || !parse_u64(value, parsed) ||
                parsed > 1000000) {
                error = "bad --case value";
                return false;
            }
            options.single_case = true;
            options.case_index = static_cast<unsigned>(parsed);
        } else if (argument == "--seed") {
            if (!take_value(argc, argv, i, value) || !parse_u64(value, options.seed)) {
                error = "bad --seed value";
                return false;
            }
        } else if (argument == "--timeout-ms") {
            std::uint64_t parsed = 0;
            if (!take_value(argc, argv, i, value) || !parse_u64(value, parsed) ||
                parsed == 0 || parsed > 3600000) {
                error = "bad --timeout-ms value";
                return false;
            }
            options.timeout_ms = static_cast<unsigned>(parsed);
        } else if (argument == "--fixture") {
            if (!take_value(argc, argv, i, options.fixture)) {
                error = "missing value for --fixture";
                return false;
            }
        } else if (argument == "--progress") {
            if (!take_value(argc, argv, i, options.progress)) {
                error = "missing value for --progress";
                return false;
            }
        } else {
            error = "unknown argument: " + argument;
            return false;
        }
    }
    return true;
}

std::string join_log(std::vector<std::string> const& log) {
    std::string out;
    for (std::size_t index = 0; index < log.size(); ++index) {
        if (index != 0) {
            out += ", ";
        }
        out += log[index];
    }
    return out;
}

/* Applies one mutation; returns false when the case has no bytes left. */
bool apply_mutation(Bytes& bytes, fuzz::Rng& rng, unsigned kind, std::string& log) {
    if (bytes.empty()) {
        return false;
    }
    switch (kind) {
        case 0: {
            std::size_t const offset =
                rng.below(static_cast<std::uint32_t>(bytes.size()));
            bytes[offset] ^= static_cast<std::uint8_t>(1u << rng.below(8));
            log = "flip@" + std::to_string(offset);
            return true;
        }
        case 1:
        case 2: {
            std::size_t const offset =
                rng.below(static_cast<std::uint32_t>(bytes.size()));
            std::size_t const room = bytes.size() - offset;
            std::size_t const length =
                1 + rng.below(static_cast<std::uint32_t>(std::min<std::size_t>(64, room)));
            std::uint8_t const value = kind == 1 ? 0x00 : 0xFF;
            for (std::size_t i = 0; i < length; ++i) {
                bytes[offset + i] = value;
            }
            log = std::string(kind == 1 ? "zero" : "ff") + "@" +
                  std::to_string(offset) + "+" + std::to_string(length);
            return true;
        }
        case 3: {
            std::size_t const length =
                rng.below(static_cast<std::uint32_t>(bytes.size()));
            bytes.resize(length);
            log = "truncate->" + std::to_string(length);
            return true;
        }
        case 4: {
            std::size_t const offset =
                rng.below(static_cast<std::uint32_t>(bytes.size()));
            std::size_t const room = bytes.size() - offset;
            std::size_t const length =
                1 + rng.below(static_cast<std::uint32_t>(std::min<std::size_t>(32, room)));
            for (std::size_t i = 0; i < length; ++i) {
                bytes[offset + i] = rng.byte();
            }
            log = "garbage@" + std::to_string(offset) + "+" + std::to_string(length);
            return true;
        }
        default: {
            std::size_t const offset =
                rng.below(static_cast<std::uint32_t>(bytes.size()));
            std::uint8_t const menu[] = {'<', '>', '&', '"', '\'', '/', '?', '!',
                                         0x00, 0x80, 0xFF};
            bytes[offset] = menu[rng.below(11)];
            log = "xmlpunct@" + std::to_string(offset) + "=" +
                  fuzz::hex32(bytes[offset]);
            return true;
        }
    }
}

/* ---- the importer contract --------------------------------------------- */

struct Outcome {
    bool ok = false;
    bool wrong_exception = false;
    std::size_t tasks = 0;
    std::size_t resources = 0;
    std::size_t assignments = 0;
    std::size_t links = 0;
    std::size_t warnings = 0;
    double millis = 0.0;
    std::string error;
};

/* Bounds every count the importer derives from the document by the bytes it was
 * given: a record, link or warning needs at least one byte of markup/text. */
std::string check_document(core::io::MspdiDocument const& doc,
                           std::size_t file_size) {
    const std::size_t cap = file_size + 1;
    if (doc.tasks.row_count() > cap) {
        return "task count " + std::to_string(doc.tasks.row_count()) +
               " exceeds the input size";
    }
    if (doc.resources.row_count() > cap) {
        return "resource count exceeds the input size";
    }
    if (doc.assignments.row_count() > cap) {
        return "assignment count exceeds the input size";
    }
    if (doc.links.size() > cap) {
        return "link count exceeds the input size";
    }
    if (doc.assignment_refs.size() > cap) {
        return "assignment reference count exceeds the input size";
    }
    if (doc.warnings.size() > cap) {
        return "warning vector is larger than the input can justify";
    }
    for (std::string const& warning : doc.warnings) {
        if (warning.size() > file_size) {
            return "a warning is larger than the input";
        }
    }
    const core::view::RecordSet* const sets[] = {&doc.tasks, &doc.resources,
                                                 &doc.assignments};
    for (core::view::RecordSet const* records : sets) {
        for (std::size_t row = 0; row < records->row_count(); ++row) {
            for (std::size_t column = 0; column < records->field_count(); ++column) {
                core::view::Value const* value = records->at(row, column);
                if (value != nullptr &&
                    value->kind == core::view::ValueKind::Text &&
                    value->text.size() > file_size) {
                    return "a text value is larger than the input";
                }
            }
        }
    }
    return {};
}

Outcome run_case(Bytes const& bytes) {
    Outcome outcome;
    std::string_view const xml =
        bytes.empty() ? std::string_view{}
                      : std::string_view(reinterpret_cast<char const*>(bytes.data()),
                                         bytes.size());
    Clock::time_point const start = Clock::now();
    try {
        core::io::MspdiDocument const doc = core::io::import_mspdi(xml);
        outcome.ok = true;
        outcome.tasks = doc.tasks.row_count();
        outcome.resources = doc.resources.row_count();
        outcome.assignments = doc.assignments.row_count();
        outcome.links = doc.links.size();
        outcome.warnings = doc.warnings.size();
        outcome.error = check_document(doc, bytes.size());
    } catch (core::io::MspdiError const& error) {
        outcome.ok = false;
        outcome.error = error.what();
    } catch (std::exception const& error) {
        outcome.ok = false;
        outcome.wrong_exception = true;
        outcome.error = std::string("non-MspdiError exception: ") + error.what();
    } catch (...) {
        outcome.ok = false;
        outcome.wrong_exception = true;
        outcome.error = "non-std exception";
    }
    outcome.millis = fuzz::elapsed_ms(start);
    return outcome;
}

/* ---- the CSV export surface --------------------------------------------- */

core::view::TableDefinition task_table() {
    core::view::TableDefinition table;
    table.id = "fuzz";
    table.name = "Fuzz";
    table.record_type = core::model::RecordType::Task;
    table.columns.push_back({kStart, {}, 0, core::view::ColumnAlign::Auto});
    table.columns.push_back({kName, {}, 0, core::view::ColumnAlign::Auto});
    table.columns.push_back({kDuration, {}, 0, core::view::ColumnAlign::Auto});
    table.columns.push_back({kNotes, {}, 0, core::view::ColumnAlign::Auto});
    table.columns.push_back({kCost, {}, 0, core::view::ColumnAlign::Auto});
    table.columns.push_back(
        {kPercentComplete, {}, 0, core::view::ColumnAlign::Auto});
    table.columns.push_back({kSummary, {}, 0, core::view::ColumnAlign::Auto});
    return table;
}

/*
 * Parses the exporter's output as RFC 4180 (CRLF, doubled quotes) and returns
 * an empty string when it is well formed and every record has `columns`
 * fields.  This is deliberately a second, independent implementation: it must
 * not share the exporter's quoting logic.
 */
std::string check_csv_shape(std::string const& text, std::size_t columns,
                            std::size_t& records) {
    records = 0;
    std::size_t pos = 0;
    if (text.empty()) {
        return "the CSV output is empty";
    }
    while (pos < text.size()) {
        ++records;
        std::size_t fields = 0;
        for (;;) {
            ++fields;
            if (pos < text.size() && text[pos] == '"') {
                ++pos;
                bool closed = false;
                while (pos < text.size()) {
                    if (text[pos] == '"') {
                        if (pos + 1 < text.size() && text[pos + 1] == '"') {
                            pos += 2; /* escaped quote */
                        } else {
                            ++pos;
                            closed = true;
                            break;
                        }
                    } else {
                        ++pos;
                    }
                }
                if (!closed) {
                    return "an unterminated quoted field";
                }
            } else {
                while (pos < text.size() && text[pos] != ',' &&
                       text[pos] != '\r' && text[pos] != '\n') {
                    if (text[pos] == '"') {
                        return "a bare quote inside an unquoted field";
                    }
                    ++pos;
                }
            }
            if (pos < text.size() && text[pos] == ',') {
                ++pos;
                continue;
            }
            break;
        }
        if (fields != columns) {
            return "record " + std::to_string(records) + " has " +
                   std::to_string(fields) + " fields, expected " +
                   std::to_string(columns);
        }
        if (pos + 1 >= text.size() || text[pos] != '\r' || text[pos + 1] != '\n') {
            return "record " + std::to_string(records) + " does not end in CRLF";
        }
        pos += 2;
    }
    return {};
}

/* Exports the imported tasks; returns an error string on any failure. */
std::string check_csv_export(core::io::MspdiDocument const& doc) {
    try {
        core::view::TableDefinition const table = task_table();
        std::string const csv = core::io::export_csv(doc.tasks, table);
        std::size_t records = 0;
        std::string const shape =
            check_csv_shape(csv, table.columns.size(), records);
        if (!shape.empty()) {
            return shape;
        }
        if (records != doc.tasks.row_count() + 1) {
            return "CSV has " + std::to_string(records) + " records for " +
                   std::to_string(doc.tasks.row_count()) + " rows plus header";
        }
    } catch (std::exception const& error) {
        return std::string("CSV export threw: ") + error.what();
    } catch (...) {
        return "CSV export threw a non-std exception";
    }
    return {};
}

/* ---- seed resolution ---------------------------------------------------- */

std::vector<std::string> resolve_seeds(std::string const& requested) {
    std::vector<std::string> seeds;
    if (!requested.empty()) {
        seeds.push_back(fuzz::find_first_existing({requested}));
        return seeds;
    }
    for (unsigned index = 0; index < kSeedCount; ++index) {
        char const* const name = kSeedNames[index];
        std::vector<std::string> const candidates = {
            fuzz::join_path(FUZZ_IO_SAMPLE_DIR, name),
            fuzz::join_path("tests", name),
            fuzz::join_path("../tests", name),
        };
        seeds.push_back(fuzz::find_first_existing(candidates));
    }
    return seeds;
}

struct Case {
    Bytes bytes;
    std::vector<std::string> log;
    std::string seed_name;
    unsigned last_kind = 0;
};

Case build_case(std::vector<Bytes> const& primed,
                std::vector<std::string> const& names, std::uint64_t base_seed,
                unsigned index) {
    std::uint64_t const case_seed =
        base_seed + static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL;
    fuzz::Rng rng(case_seed);
    unsigned const seed_index =
        rng.below(static_cast<std::uint32_t>(primed.size()));
    Case result;
    result.seed_name = names[seed_index];
    result.bytes = primed[seed_index];
    unsigned const mutations = 1 + rng.below(4);
    for (unsigned mutation = 0; mutation < mutations; ++mutation) {
        unsigned const kind = rng.below(kMutationKinds);
        std::string entry;
        if (!apply_mutation(result.bytes, rng, kind, entry)) {
            break;
        }
        result.log.push_back(entry);
        result.last_kind = kind;
    }
    return result;
}

std::string case_receipt(unsigned index, std::uint64_t base_seed,
                         Case const& running) {
    std::uint64_t const case_seed =
        base_seed + static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL;
    return "case " + std::to_string(index) + " seed " + fuzz::hex64(case_seed) +
           " file " + running.seed_name + " mutations [" + join_log(running.log) +
           "]";
}

int run_worker(Options const& options) {
    fuzz::Checker checker;
    std::string progress_path = options.progress;
    if (progress_path.empty()) {
        std::error_code mkdir_error;
        std::filesystem::create_directories(FUZZ_OUT_DIR, mkdir_error);
        progress_path = fuzz::join_path(FUZZ_OUT_DIR, "fuzz_xml.progress");
    }
    fuzz::Progress progress(progress_path);

    std::vector<std::string> const seeds = resolve_seeds(options.fixture);
    if (seeds.empty() || seeds.front().empty()) {
        std::cout << "FAIL: no XML seed found (looked for the committed samples "
                     "under tests/)\n";
        return 1;
    }
    std::vector<Bytes> primed;
    std::vector<std::string> names;
    for (std::size_t index = 0; index < seeds.size(); ++index) {
        if (seeds[index].empty()) {
            std::cout << "FAIL: the committed XML seed is missing: "
                      << kSeedNames[index] << " under tests/\n";
            return 1;
        }
        primed.push_back(fuzz::read_file(seeds[index]));
        names.push_back(std::filesystem::path(seeds[index]).filename().string());
        std::cout << "seed: " << seeds[index] << " (" << primed.back().size()
                  << " bytes)\n";
    }

    /* The unmutated seeds are the baseline: each must import and export. */
    for (std::size_t index = 0; index < primed.size(); ++index) {
        Outcome const baseline = run_case(primed[index]);
        if (!baseline.ok || !baseline.error.empty()) {
            std::cout << "FAIL: the unmutated seed " << names[index]
                      << " did not import cleanly: " << baseline.error << "\n";
            return 1;
        }
        std::string const csv_error = check_csv_export(
            core::io::import_mspdi(std::string_view(
                reinterpret_cast<char const*>(primed[index].data()),
                primed[index].size())));
        if (!csv_error.empty()) {
            std::cout << "FAIL: the unmutated seed " << names[index]
                      << " did not export cleanly: " << csv_error << "\n";
            return 1;
        }
        std::cout << "baseline " << names[index] << ": " << baseline.tasks
                  << " tasks, " << baseline.resources << " resources, "
                  << baseline.assignments << " assignments, " << baseline.links
                  << " links, " << baseline.warnings << " warnings\n";
    }

    if (options.single_case) {
        Case const running =
            build_case(primed, names, options.seed, options.case_index);
        progress.note("case " + std::to_string(options.case_index) + " seed " +
                      fuzz::hex64(options.seed + static_cast<std::uint64_t>(
                                                    options.case_index) *
                                                    0x9E3779B97F4A7C15ULL));
        Outcome const outcome = run_case(running.bytes);
        std::string const receipt =
            case_receipt(options.case_index, options.seed, running);
        if (outcome.wrong_exception) {
            checker.fail(receipt + " threw " + outcome.error);
        } else if (outcome.ok && !outcome.error.empty()) {
            checker.fail(receipt + " returned an inconsistent document: " +
                         outcome.error);
        } else if (outcome.ok) {
            core::io::MspdiDocument const doc = core::io::import_mspdi(
                std::string_view(reinterpret_cast<char const*>(running.bytes.data()),
                                 running.bytes.size()));
            std::string const csv_error = check_csv_export(doc);
            if (!csv_error.empty()) {
                checker.fail(receipt + " CSV: " + csv_error);
            } else {
                std::cout << receipt << ": ok, " << outcome.tasks << " tasks, "
                          << outcome.links << " links, CSV well formed\n";
            }
        } else {
            std::cout << receipt << ": refused with MspdiError: " << outcome.error
                      << "\n";
        }
        return checker.report("fuzz_xml (single case)");
    }

    unsigned outcome_ok[kMutationKinds] = {};
    unsigned outcome_error[kMutationKinds] = {};
    unsigned total_ok = 0;
    unsigned total_refused = 0;
    unsigned csv_checked = 0;
    double max_case_ms = 0.0;
    std::uint64_t slow_case_seed = 0;
    unsigned slow_case_index = 0;
    Clock::time_point const run_start = Clock::now();

    for (unsigned index = 0; index < options.cases; ++index) {
        progress.note("case " + std::to_string(index) + " seed " +
                      fuzz::hex64(options.seed + static_cast<std::uint64_t>(index) *
                                                     0x9E3779B97F4A7C15ULL));
        Case const running = build_case(primed, names, options.seed, index);
        Outcome const outcome = run_case(running.bytes);
        max_case_ms = std::max(max_case_ms, outcome.millis);

        std::string const receipt = case_receipt(index, options.seed, running);
        if (outcome.wrong_exception) {
            checker.fail(receipt + " threw " + outcome.error);
            break;
        }
        if (outcome.ok && !outcome.error.empty()) {
            checker.fail(receipt + " returned an inconsistent document: " +
                         outcome.error);
            break;
        }
        if (outcome.ok) {
            ++outcome_ok[running.last_kind % kMutationKinds];
            ++total_ok;
            core::io::MspdiDocument const doc = core::io::import_mspdi(
                std::string_view(reinterpret_cast<char const*>(running.bytes.data()),
                                 running.bytes.size()));
            std::string const csv_error = check_csv_export(doc);
            if (!csv_error.empty()) {
                checker.fail(receipt + " CSV: " + csv_error);
                break;
            }
            ++csv_checked;
        } else {
            ++outcome_error[running.last_kind % kMutationKinds];
            ++total_refused;
        }
        if (outcome.millis > kSlowCaseMs) {
            slow_case_seed = options.seed +
                             static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL;
            slow_case_index = index;
        }

        /* Determinism: the first few cases must reproduce their bytes. */
        if (index < 8) {
            Case const replay = build_case(primed, names, options.seed, index);
            checker.check(replay.bytes == running.bytes && replay.log == running.log,
                          receipt + " is not reproducible on a second run");
        }
    }

    double const wall_ms = fuzz::elapsed_ms(run_start);
    std::cout << "cases: " << options.cases << ", ok " << total_ok
              << ", MspdiError " << total_refused << ", max case "
              << fuzz::ms_text(max_case_ms) << ", total " << fuzz::ms_text(wall_ms)
              << "\n";
    std::cout << "CSV exports checked: " << csv_checked << "\n";
    for (unsigned kind = 0; kind < kMutationKinds; ++kind) {
        std::cout << "  kind " << kind << ": ok " << outcome_ok[kind] << ", error "
                  << outcome_error[kind] << "\n";
    }
    if (slow_case_seed != 0) {
        checker.fail("case " + std::to_string(slow_case_index) + " seed " +
                     fuzz::hex64(slow_case_seed) + " took longer than " +
                     fuzz::ms_text(kSlowCaseMs));
    }
    checker.check(total_ok > 0, "no mutated case ever imported");
    checker.check(total_refused > 0, "no mutated case was ever refused");

    return checker.report("fuzz_xml");
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cout << "FAIL: " << error << "\n";
        print_usage();
        return 1;
    }
    if (options.worker || !options.watchdog) {
        return run_worker(options);
    }

    std::string progress = options.progress;
    if (progress.empty()) {
        std::error_code mkdir_error;
        std::filesystem::create_directories(FUZZ_OUT_DIR, mkdir_error);
        progress = fuzz::join_path(FUZZ_OUT_DIR, "fuzz_xml.progress");
    }
    std::cout << "fuzz_xml: supervising worker, timeout " << options.timeout_ms
              << " ms\n";
    std::string message;
    int const result =
        fuzz::run_watched(fuzz::worker_command(argc, argv), options.timeout_ms,
                          message);
    if (result != 0) {
        std::cout << "FAIL: " << message << "\n";
        fuzz::Progress last(progress);
        std::string line;
        if (last.last(line)) {
            std::cout << "FAIL: last started case: " << line << "\n";
            std::cout << "FAIL: replay it with --worker --case N (N from the line "
                         "above) -- the seed follows from case N and --seed\n";
        } else {
            std::cout << "FAIL: no progress was recorded\n";
        }
    }
    return result;
}
