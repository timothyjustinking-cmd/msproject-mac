/*
 * fuzz_reader_main.cpp -- deterministic mutation fuzz for the .mpp reader.
 *
 * WHAT IS FUZZED.  `tests/data/synthetic-base.mpp` (committed, synthetic) is the
 * seed.  Every case copies the bytes, applies 1..5 seeded mutations, and calls
 * `mpp::load_bytes`.  Since wave 33 (W33-2.4) every case is then also handed
 * to the writer's container parser, `mppwrite::CfbDocument::from_bytes`: the
 * same untrusted bytes reach two independent parsers, and either may only
 * throw its own error type (`mpp::Error` / `mppwrite::Error`) or return a
 * self-consistent object.  `--no-writer-parse` turns the second parse off for
 * bisecting a failure.  Mutation families:
 *
 *   0  a random byte bit flip
 *   1  a zero run at a random offset
 *   2  a 0xFF run at a random offset
 *   3  truncation to a random length (the container is cut anywhere)
 *   4  a structured length/count corruption: CFB header words, directory-entry
 *      size/start/name-length/type words, FixedMeta/Fixed2Meta/VarMeta header
 *      counts and size words, the first Var2Data block length, Props header
 *      words, task FixedData uid/ID/constraint/date words, FixedMeta entry state
 *      bytes, VarMeta entry uid/offset words
 *   5  a random byte run inside a stream payload
 *   6  a one-byte overwrite with a saturated value (0x00/0x7F/0x80/0xFF)
 *
 * THE CONTRACT.  For every case the reader must EITHER throw `mpp::Error` OR
 * return a self-consistent `mpp::Document`.  The writer-side parser must EITHER
 * throw `mppwrite::Error` OR return a CfbDocument whose entry/stream counts
 * agree.  Any other exception type, a crash, a timeout, or a document that
 * breaks one of the invariants below is a failure.
 * ASan turns an out-of-bounds read into a process abort, which the child-process
 * watchdog reports with the seed that was running.
 *
 * SELF-CONSISTENCY INVARIANTS (chosen so a damaged real file can still satisfy
 * them; tests/fuzz/README.md says why each is here):
 *   - live task rows strictly increase and are bounded by the file size;
 *   - link/assignment/resource counts are bounded by the file size;
 *   - every live task has a non-empty outline number (flat fallback included);
 *   - `Link::predecessor_known` / `successor_known` agree with the live task uid
 *     set, and `endpoints_live()` is exactly their conjunction;
 *   - the two link counts add up to `links.size()`;
 *   - duplicate task uids are reported in `warnings` (the reader's documented
 *     "records keyed by uid are ambiguous" contract);
 *   - no single name/string is larger than the file, and the warnings vector
 *     cannot explode without bound.
 *
 * USAGE
 *   fuzz_reader [--cases N] [--seed N] [--case N] [--fixture PATH]
 *               [--corpus DIR] [--no-corpus] [--timeout-ms N] [--no-watchdog]
 *               [--writer-parse] [--no-writer-parse] [--progress PATH]
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
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "fuzz_common.h"
#include "fuzz_watchdog.h"
#include "reader.h"

#ifndef FUZZ_TEST_DATA_DIR
#define FUZZ_TEST_DATA_DIR "tests/data"
#endif
#ifndef FUZZ_CORPUS_DIR
#define FUZZ_CORPUS_DIR "corpus"
#endif
#ifndef FUZZ_OUT_DIR
#define FUZZ_OUT_DIR "build/fuzz-out"
#endif

namespace {

using fuzz::Bytes;
using Clock = std::chrono::steady_clock;

constexpr char const* kFixtureName = "synthetic-base.mpp";
constexpr unsigned kMutationKinds = 7;
constexpr double kSlowCaseMs = 2000.0;

struct Options {
    unsigned cases = 240;
    std::uint64_t seed = 0x57;
    bool single_case = false;
    unsigned case_index = 0;
    std::string fixture;
    std::string corpus_dir;
    bool use_corpus = true;
    bool watchdog = true;
    unsigned timeout_ms = 30000;
    std::string progress;
    bool worker = false;
    /* W33-2.4: feed every mutated case to the writer's container parser too.
     * On by default, because a parser only reached by an opt-in flag is not
     * coverage; --no-writer-parse exists to bisect a failure. */
    bool writer_parse = true;
};

void print_usage() {
    std::cout
        << "usage: fuzz_reader [--cases N] [--seed N] [--case N] "
           "[--fixture PATH]\n"
           "                   [--corpus DIR] [--no-corpus] [--timeout-ms N]\n"
           "                   [--no-watchdog] [--no-writer-parse] "
           "[--progress PATH]\n"
           "  --cases N       number of mutated cases (default 240)\n"
           "  --seed N        base seed, decimal or 0x-hex (default 0x57)\n"
           "  --case N        run exactly that one case (for a failure repro)\n"
           "  --fixture PATH  .mpp seed (default tests/data/synthetic-base.mpp)\n"
           "  --corpus DIR    directory holding the real files (read only)\n"
           "  --no-corpus     skip the real-file baseline loads\n"
           "  --no-watchdog   run in-process (a hang then blocks this process)\n"
           "  --no-writer-parse  skip mppwrite::CfbDocument::from_bytes\n"
           "                     (default: every case is fed to both parsers)\n";
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
        } else if (argument == "--no-corpus") {
            options.use_corpus = false;
        } else if (argument == "--writer-parse") {
            options.writer_parse = true;
        } else if (argument == "--no-writer-parse") {
            options.writer_parse = false;
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
        } else if (argument == "--corpus") {
            if (!take_value(argc, argv, i, options.corpus_dir)) {
                error = "missing value for --corpus";
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

/* ---- mutation sites ---------------------------------------------------- */

struct WordSite {
    std::size_t offset = 0;
    std::uint8_t width = 4;
    std::string label;
};

struct StreamWord {
    char const* path = nullptr;
    std::size_t stream_offset = 0;
    std::uint8_t width = 4;
    char const* label = nullptr;
};

StreamWord const kStreamWords[] = {
    {"114/TBkndTask/FixedMeta", 0, 4, "task.fixedmeta.flags"},
    {"114/TBkndTask/FixedMeta", 4, 4, "task.fixedmeta.deleted_bit"},
    {"114/TBkndTask/FixedMeta", 8, 4, "task.fixedmeta.entry_count"},
    {"114/TBkndTask/FixedMeta", 12, 4, "task.fixedmeta.fixed_size"},
    {"114/TBkndTask/Fixed2Meta", 8, 4, "task.fixed2meta.entry_count"},
    {"114/TBkndTask/Fixed2Meta", 12, 4, "task.fixed2meta.fixed2_size"},
    {"114/TBkndTask/VarMeta", 8, 4, "task.varmeta.declared_entries"},
    {"114/TBkndTask/VarMeta", 0x14, 4, "task.varmeta.var2_size"},
    {"114/TBkndTask/Var2Data", 0, 4, "task.var2.first_block_length"},
    {"114/TBkndCons/FixedMeta", 4, 4, "cons.fixedmeta.deleted_bit"},
    {"114/TBkndCons/FixedMeta", 8, 4, "cons.fixedmeta.entry_count"},
    {"114/TBkndCons/FixedMeta", 12, 4, "cons.fixedmeta.fixed_size"},
    {"114/TBkndCons/Fixed2Meta", 8, 4, "cons.fixed2meta.entry_count"},
    {"114/TBkndCons/Fixed2Meta", 12, 4, "cons.fixed2meta.fixed2_size"},
    {"114/TBkndAssn/FixedMeta", 4, 4, "assn.fixedmeta.deleted_bit"},
    {"114/TBkndAssn/FixedMeta", 8, 4, "assn.fixedmeta.entry_count"},
    {"114/TBkndAssn/FixedMeta", 12, 4, "assn.fixedmeta.fixed_size"},
    {"114/TBkndAssn/Fixed2Meta", 8, 4, "assn.fixed2meta.entry_count"},
    {"114/TBkndAssn/Fixed2Meta", 12, 4, "assn.fixed2meta.fixed2_size"},
    {"114/TBkndRsc/FixedMeta", 8, 4, "rsc.fixedmeta.entry_count"},
    {"114/TBkndRsc/FixedMeta", 12, 4, "rsc.fixedmeta.fixed_size"},
    {"114/TBkndRsc/Fixed2Meta", 8, 4, "rsc.fixed2meta.entry_count"},
    {"114/TBkndRsc/Fixed2Meta", 12, 4, "rsc.fixed2meta.fixed2_size"},
    {"114/Props", 0, 4, "props.size_word"},
    {"114/Props", 4, 4, "props.size_repeat"},
    {"114/Props", 8, 4, "props.format_version"},
    {"114/Props", 12, 4, "props.declared_entries"},
};

struct Model {
    std::vector<WordSite> words;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
};

bool file_offset_for(mppwrite::CfbDocument const& doc, std::string const& path,
                     std::size_t stream_offset, std::size_t& out) {
    std::size_t consumed = 0;
    for (std::pair<std::uint64_t, std::size_t> const& range :
         doc.stream_file_ranges(path)) {
        std::size_t const length = range.second;
        if (stream_offset < consumed + length) {
            out = static_cast<std::size_t>(range.first) + (stream_offset - consumed);
            return true;
        }
        consumed += length;
    }
    return false;
}

void scan_directory_entries(Bytes const& bytes, Model& model) {
    for (std::size_t offset = 0; offset + 128 <= bytes.size(); offset += 128) {
        std::uint16_t const name_length = fuzz::u16_at(bytes, offset + 64);
        std::uint8_t const type = bytes[offset + 66];
        if (name_length < 2 || name_length > 64 || (name_length % 2) != 0) {
            continue;
        }
        if (type != 1 && type != 2 && type != 5) {
            continue;
        }
        std::string name;
        bool printable = true;
        for (std::size_t index = 0;
             index + 1 < static_cast<std::size_t>(name_length) - 2; index += 2) {
            std::uint16_t const code = fuzz::u16_at(bytes, offset + index);
            if (code < 0x20 || code > 0x7E) {
                printable = false;
                break;
            }
            name.push_back(static_cast<char>(code));
        }
        if (!printable || name.empty()) {
            continue;
        }
        model.words.push_back({offset + 116, 4, "dir.start " + name});
        model.words.push_back({offset + 120, 4, "dir.size " + name});
        model.words.push_back({offset + 64, 2, "dir.name_length " + name});
        model.words.push_back({offset + 66, 1, "dir.type " + name});
    }
}

Model build_model(Bytes const& pristine) {
    Model model;

    struct HeaderWord {
        std::size_t offset;
        std::uint8_t width;
        char const* label;
    };
    HeaderWord const header_words[] = {
        {0x18, 2, "cfb.minor_version"},   {0x1A, 2, "cfb.major_version"},
        {0x1C, 2, "cfb.byte_order"},      {0x1E, 2, "cfb.sector_shift"},
        {0x20, 2, "cfb.mini_shift"},      {0x28, 4, "cfb.dir_sector_count"},
        {0x2C, 4, "cfb.fat_sector_count"}, {0x30, 4, "cfb.dir_start"},
        {0x34, 4, "cfb.transaction"},     {0x38, 4, "cfb.mini_cutoff"},
        {0x3C, 4, "cfb.mini_fat_start"},  {0x40, 4, "cfb.mini_fat_count"},
        {0x44, 4, "cfb.difat_start"},     {0x48, 4, "cfb.difat_count"},
    };
    for (HeaderWord const& word : header_words) {
        if (fuzz::has_range(pristine, word.offset, word.width)) {
            model.words.push_back({word.offset, word.width, word.label});
        }
    }
    scan_directory_entries(pristine, model);

    try {
        mppwrite::CfbDocument const doc = mppwrite::CfbDocument::from_bytes(pristine);
        for (std::string const& path : doc.stream_paths()) {
            for (std::pair<std::uint64_t, std::size_t> const& range :
                 doc.stream_file_ranges(path)) {
                model.ranges.emplace_back(static_cast<std::size_t>(range.first),
                                          range.second);
            }
        }
        for (StreamWord const& word : kStreamWords) {
            if (!doc.has_stream(word.path)) {
                continue;
            }
            std::size_t offset = 0;
            if (!file_offset_for(doc, word.path, word.stream_offset, offset)) {
                continue;
            }
            if (!fuzz::has_range(pristine, offset, word.width)) {
                continue;
            }
            model.words.push_back({offset, word.width, word.label});
        }

        std::uint64_t const task_fixed = doc.stream_size("114/TBkndTask/FixedData");
        if (task_fixed >= 48) {
            std::size_t const rows = static_cast<std::size_t>((task_fixed - 48) / 202);
            for (std::size_t row = 0; row < rows && row < 8; ++row) {
                std::size_t const base = 48 + 202 * row;
                struct RowWord {
                    std::size_t offset;
                    std::uint8_t width;
                    char const* label;
                };
                RowWord const row_words[] = {
                    {0, 4, "task.row.id"},          {4, 4, "task.row.uid"},
                    {64, 2, "task.row.constraint"}, {104, 4, "task.row.start"},
                    {182, 4, "task.row.deadline"},
                };
                for (RowWord const& word : row_words) {
                    std::size_t offset = 0;
                    if (!file_offset_for(doc, "114/TBkndTask/FixedData",
                                         base + word.offset, offset)) {
                        continue;
                    }
                    if (fuzz::has_range(pristine, offset, word.width)) {
                        model.words.push_back(
                            {offset, word.width,
                             std::string(word.label) + "[" + std::to_string(row) + "]"});
                    }
                }
            }
        }
        std::uint64_t const task_meta = doc.stream_size("114/TBkndTask/FixedMeta");
        if (task_meta >= 16) {
            std::size_t const entries = static_cast<std::size_t>((task_meta - 16) / 47);
            for (std::size_t entry = 0; entry < entries && entry < 8; ++entry) {
                std::size_t offset = 0;
                if (!file_offset_for(doc, "114/TBkndTask/FixedMeta",
                                     16 + 47 * entry, offset)) {
                    continue;
                }
                if (fuzz::has_range(pristine, offset, 1)) {
                    model.words.push_back(
                        {offset, 1,
                         "task.fixedmeta.entry_state[" + std::to_string(entry) + "]"});
                }
            }
        }
        std::uint64_t const var_meta = doc.stream_size("114/TBkndTask/VarMeta");
        if (var_meta >= 24) {
            std::size_t const entries = static_cast<std::size_t>((var_meta - 24) / 12);
            for (std::size_t entry = 0; entry < entries && entry < 16; ++entry) {
                for (std::size_t field = 0; field < 2; ++field) {
                    std::size_t offset = 0;
                    if (!file_offset_for(doc, "114/TBkndTask/VarMeta",
                                         24 + 12 * entry + 4 * field, offset)) {
                        continue;
                    }
                    if (fuzz::has_range(pristine, offset, 4)) {
                        char const* const label =
                            field == 0 ? "task.varmeta.entry_uid["
                                       : "task.varmeta.entry_offset[";
                        model.words.push_back(
                            {offset, 4, label + std::to_string(entry) + "]"});
                    }
                }
            }
        }
    } catch (mppwrite::Error const& error) {
        std::cout << "WARN: mutation-site model unavailable (" << error.what()
                  << "); random offsets only\n";
    }
    return model;
}

std::uint32_t extreme_value(fuzz::Rng& rng) {
    switch (rng.below(8)) {
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 0xFFFFFFFFu;
        case 3:
            return 0x7FFFFFFFu;
        case 4:
            return 0xFFFFu;
        case 5:
            return 0x80000000u;
        case 6:
            return 0xFFFFFFFEu;
        default:
            return static_cast<std::uint32_t>(rng.next() & 0xFFFFFFFFu);
    }
}

/* Applies one mutation; returns false when the case has no bytes left. */
bool apply_mutation(Bytes& bytes, Model const& model, fuzz::Rng& rng, unsigned kind,
                    std::string& log) {
    if (bytes.empty()) {
        return false;
    }
    switch (kind) {
        case 0: {
            std::size_t const offset = rng.below(static_cast<std::uint32_t>(bytes.size()));
            bytes[offset] ^= static_cast<std::uint8_t>(1u << rng.below(8));
            log = "flip@" + std::to_string(offset);
            return true;
        }
        case 1:
        case 2: {
            std::size_t const offset = rng.below(static_cast<std::uint32_t>(bytes.size()));
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
            std::size_t const length = rng.below(static_cast<std::uint32_t>(bytes.size()));
            bytes.resize(length);
            log = "truncate->" + std::to_string(length);
            return !bytes.empty();
        }
        case 4: {
            if (model.words.empty()) {
                return false;
            }
            WordSite const& site =
                model.words[rng.below(static_cast<std::uint32_t>(model.words.size()))];
            if (!fuzz::has_range(bytes, site.offset, site.width)) {
                return false;
            }
            std::uint32_t const value = extreme_value(rng);
            if (site.width == 1) {
                bytes[site.offset] = static_cast<std::uint8_t>(value & 0xFFu);
            } else if (site.width == 2) {
                fuzz::put_u16_at(bytes, site.offset,
                                 static_cast<std::uint16_t>(value & 0xFFFFu));
            } else {
                fuzz::put_u32_at(bytes, site.offset, value);
            }
            log = site.label + "@" + std::to_string(site.offset) + "=" +
                  fuzz::hex32(value);
            return true;
        }
        case 5: {
            if (model.ranges.empty()) {
                return false;
            }
            std::pair<std::size_t, std::size_t> const& range =
                model.ranges[rng.below(static_cast<std::uint32_t>(model.ranges.size()))];
            if (range.second == 0) {
                return false;
            }
            std::size_t const offset =
                range.first + rng.below(static_cast<std::uint32_t>(range.second));
            if (!fuzz::has_range(bytes, offset, 1)) {
                return false;
            }
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
            std::size_t const offset = rng.below(static_cast<std::uint32_t>(bytes.size()));
            std::uint8_t const menu[] = {0x00, 0x7F, 0x80, 0xFF};
            bytes[offset] = menu[rng.below(4)];
            log = "set@" + std::to_string(offset) + "=" + fuzz::hex32(bytes[offset]);
            return true;
        }
    }
}

/* ---- the reader contract ----------------------------------------------- */

std::string check_document(mpp::Document const& doc, std::size_t file_size) {
    std::size_t const row_cap = file_size / 202 + 8;
    if (doc.tasks.size() > row_cap) {
        return "task count " + std::to_string(doc.tasks.size()) +
               " exceeds the file's row capacity";
    }
    if (doc.links.size() > file_size / 20 + 8) {
        return "link count exceeds the file's row capacity";
    }
    if (doc.assignments.size() > file_size / 110 + 8) {
        return "assignment count exceeds the file's row capacity";
    }
    if (doc.resources.size() > file_size / 172 + 8) {
        return "resource count exceeds the file's row capacity";
    }
    if (doc.warnings.size() > file_size / 20 + 64) {
        return "warning vector is larger than the file can justify";
    }

    std::set<std::uint32_t> uids;
    for (std::size_t index = 0; index < doc.tasks.size(); ++index) {
        mpp::Task const& task = doc.tasks[index];
        if (index > 0 && task.row <= doc.tasks[index - 1].row) {
            return "task rows are not strictly increasing at index " +
                   std::to_string(index);
        }
        if (task.name.size() > file_size) {
            return "a task name is larger than the file";
        }
        if (task.outline_number.empty()) {
            return "live task row " + std::to_string(task.row) +
                   " has no outline number";
        }
        uids.insert(task.uid);
    }
    for (mpp::Link const& link : doc.links) {
        bool const predecessor_live = uids.count(link.predecessor_uid) != 0;
        bool const successor_live = uids.count(link.successor_uid) != 0;
        if (link.predecessor_known != predecessor_live) {
            return "link " + std::to_string(link.uid) +
                   " predecessor_known disagrees with the live task set";
        }
        if (link.successor_known != successor_live) {
            return "link " + std::to_string(link.uid) +
                   " successor_known disagrees with the live task set";
        }
        if (link.endpoints_live() != (predecessor_live && successor_live)) {
            return "link " + std::to_string(link.uid) +
                   " endpoints_live disagrees with its endpoint flags";
        }
    }
    if (doc.link_count_with_live_endpoints() + doc.link_count_with_dead_endpoints() !=
        doc.links.size()) {
        return "the two link counts do not add up to links.size()";
    }
    if (uids.size() != doc.tasks.size()) {
        bool warned = false;
        for (std::string const& warning : doc.warnings) {
            if (warning.find("appears on more than one row") != std::string::npos) {
                warned = true;
                break;
            }
        }
        if (!warned) {
            return "duplicate task UniqueIDs were not reported in warnings";
        }
    }
    for (mpp::Task const& task : doc.tasks) {
        for (mpp::CustomFieldValue const& field : task.custom_fields) {
            if (field.field_id == 0) {
                return "a custom field has field_id 0";
            }
            if (field.text.size() > file_size) {
                return "a custom-field text value is larger than the file";
            }
        }
    }
    return {};
}

struct Outcome {
    bool ok = false;
    bool wrong_exception = false;
    std::size_t tasks = 0;
    std::size_t links = 0;
    std::size_t assignments = 0;
    std::size_t resources = 0;
    std::size_t warnings = 0;
    double millis = 0.0;
    std::string error;
};

Outcome run_case(Bytes bytes, std::size_t file_size) {
    Outcome outcome;
    Clock::time_point const start = Clock::now();
    try {
        mpp::Document const doc = mpp::load_bytes(std::move(bytes));
        outcome.ok = true;
        outcome.tasks = doc.tasks.size();
        outcome.links = doc.links.size();
        outcome.assignments = doc.assignments.size();
        outcome.resources = doc.resources.size();
        outcome.warnings = doc.warnings.size();
        outcome.error = check_document(doc, file_size);
    } catch (mpp::Error const& error) {
        outcome.ok = false;
        outcome.error = error.what();
    } catch (std::exception const& error) {
        outcome.ok = false;
        outcome.wrong_exception = true;
        outcome.error = std::string("non-mpp exception: ") + error.what();
    } catch (...) {
        outcome.ok = false;
        outcome.wrong_exception = true;
        outcome.error = "non-std exception";
    }
    outcome.millis = fuzz::elapsed_ms(start);
    return outcome;
}

/* W33-2.4: the same mutated bytes through the writer-side container parser.
 * mppwrite::Error is the only acceptable refusal; any other exception, a crash
 * or a hang is a failure.  A parsed document must at least agree with itself:
 * one root entry and no more streams than directory entries. */
struct WriterOutcome {
    bool clean = false;          /* parsed, or refused with mppwrite::Error */
    bool parsed = false;
    bool wrong_exception = false;
    std::size_t entries = 0;
    std::size_t streams = 0;
    double millis = 0.0;
    std::string error;
};

WriterOutcome run_writer_case(Bytes const& bytes) {
    WriterOutcome outcome;
    Clock::time_point const start = Clock::now();
    try {
        mppwrite::CfbDocument const doc = mppwrite::CfbDocument::from_bytes(bytes);
        outcome.clean = true;
        outcome.parsed = true;
        outcome.entries = doc.entry_count();
        outcome.streams = doc.stream_count();
        if (outcome.entries == 0) {
            outcome.error = "a parsed document has no directory entries";
        } else if (outcome.streams > outcome.entries) {
            outcome.error = "stream count " + std::to_string(outcome.streams) +
                            " exceeds the " + std::to_string(outcome.entries) +
                            " directory entries";
        }
    } catch (mppwrite::Error const& error) {
        outcome.clean = true;
        outcome.error = error.what();
    } catch (std::exception const& error) {
        outcome.wrong_exception = true;
        outcome.error = std::string("non-mppwrite exception: ") + error.what();
    } catch (...) {
        outcome.wrong_exception = true;
        outcome.error = "non-std exception";
    }
    outcome.millis = fuzz::elapsed_ms(start);
    return outcome;
}

std::string resolve_fixture(std::string const& requested) {
    std::vector<std::string> candidates;
    if (!requested.empty()) {
        candidates.push_back(requested);
    } else {
        candidates.push_back(fuzz::join_path(FUZZ_TEST_DATA_DIR, kFixtureName));
        candidates.push_back(fuzz::join_path("tests/data", kFixtureName));
        candidates.push_back(fuzz::join_path("../data", kFixtureName));
    }
    return fuzz::find_first_existing(candidates);
}

/* Runs one case (case_index) and returns its outcome; used by both the loop and
 * the single-case replay mode.  `log` receives the applied mutations.  When
 * `writer_parse` is set, the same mutated bytes are also fed to
 * CfbDocument::from_bytes and `writer` receives that parser's outcome. */
Outcome run_indexed_case(Bytes const& pristine, Model const& model,
                         std::uint64_t base_seed, unsigned index,
                         std::vector<std::string>& log, unsigned& last_kind,
                         bool writer_parse, WriterOutcome& writer) {
    std::uint64_t const case_seed =
        base_seed + static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL;
    Bytes bytes = pristine;
    fuzz::Rng rng(case_seed);
    unsigned const mutations = 1 + rng.below(5);
    last_kind = 0;
    for (unsigned mutation = 0; mutation < mutations; ++mutation) {
        unsigned const kind = rng.below(kMutationKinds);
        std::string entry;
        if (!apply_mutation(bytes, model, rng, kind, entry)) {
            break;
        }
        log.push_back(entry);
        last_kind = kind;
    }
    Outcome const outcome = run_case(bytes, pristine.size());
    if (writer_parse) {
        writer = run_writer_case(bytes);
    }
    return outcome;
}

std::string case_receipt(unsigned index, std::uint64_t base_seed,
                         std::vector<std::string> const& log) {
    std::uint64_t const case_seed =
        base_seed + static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL;
    return "case " + std::to_string(index) + " seed " + fuzz::hex64(case_seed) +
           " mutations [" + join_log(log) + "]";
}

int run_worker(Options const& options) {
    fuzz::Checker checker;
    /* The supervisor and the worker must agree on the progress file even though
     * only the supervisor computes a default path, so the worker derives the
     * same default here when it was started without --progress. */
    std::string progress_path = options.progress;
    if (progress_path.empty()) {
        std::error_code mkdir_error;
        std::filesystem::create_directories(FUZZ_OUT_DIR, mkdir_error);
        progress_path = fuzz::join_path(FUZZ_OUT_DIR, "fuzz_reader.progress");
    }
    fuzz::Progress progress(progress_path);

    std::string const fixture = resolve_fixture(options.fixture);
    if (fixture.empty()) {
        std::cout << "FAIL: no fixture found (looked for " << kFixtureName
                  << " under tests/data)\n";
        return 1;
    }
    Bytes const pristine = fuzz::read_file(fixture);
    std::cout << "fixture: " << fixture << " (" << pristine.size() << " bytes)\n";

    Outcome const baseline = run_case(pristine, pristine.size());
    if (!baseline.ok) {
        std::cout << "FAIL: the unmutated fixture did not load: " << baseline.error
                  << "\n";
        return 1;
    }
    std::cout << "baseline: " << baseline.tasks << " tasks, " << baseline.resources
              << " resources, " << baseline.links << " links, "
              << baseline.assignments << " assignments, " << baseline.warnings
              << " warnings\n";
    if (options.writer_parse) {
        WriterOutcome const writer_baseline = run_writer_case(pristine);
        if (!writer_baseline.parsed || !writer_baseline.error.empty()) {
            std::cout << "FAIL: the unmutated fixture did not parse on the writer "
                         "side: "
                      << writer_baseline.error << "\n";
            return 1;
        }
        std::cout << "writer baseline: " << writer_baseline.entries
                  << " directory entries, " << writer_baseline.streams
                  << " streams\n";
    }

    Model const model = build_model(pristine);
    std::cout << "mutation sites: " << model.words.size() << " structured words, "
              << model.ranges.size() << " stream ranges\n";

    /* ---- single-case replay (failure repro) ---- */
    if (options.single_case) {
        std::vector<std::string> log;
        unsigned last_kind = 0;
        WriterOutcome writer;
        fuzz::Progress const single(progress_path);
        single.note("case " + std::to_string(options.case_index) + " seed " +
                    fuzz::hex64(options.seed + static_cast<std::uint64_t>(
                                                   options.case_index) *
                                                   0x9E3779B97F4A7C15ULL));
        Outcome const outcome =
            run_indexed_case(pristine, model, options.seed, options.case_index, log,
                             last_kind, options.writer_parse, writer);
        std::string const receipt = case_receipt(options.case_index, options.seed, log);
        if (outcome.wrong_exception) {
            checker.fail(receipt + " threw " + outcome.error);
        } else if (outcome.ok && !outcome.error.empty()) {
            checker.fail(receipt + " returned an inconsistent document: " +
                         outcome.error);
        } else if (outcome.ok) {
            std::cout << receipt << ": ok, " << outcome.tasks << " tasks, "
                      << outcome.links << " links, " << outcome.assignments
                      << " assignments\n";
        } else {
            std::cout << receipt << ": refused with mpp::Error: " << outcome.error
                      << "\n";
        }
        if (options.writer_parse) {
            if (writer.wrong_exception || !writer.clean) {
                checker.fail(receipt + " threw on the writer side: " + writer.error);
            } else if (writer.parsed && !writer.error.empty()) {
                checker.fail(receipt +
                             " returned an inconsistent CfbDocument: " +
                             writer.error);
            } else {
                std::cout << receipt << ": writer side "
                          << (writer.parsed ? "parsed " +
                                                  std::to_string(writer.entries) +
                                                  " entries / " +
                                                  std::to_string(writer.streams) +
                                                  " streams"
                                            : "refused with mppwrite::Error: " +
                                                  writer.error)
                          << "\n";
            }
        }
        return checker.report("fuzz_reader (single case)");
    }

    unsigned outcome_ok[kMutationKinds] = {};
    unsigned outcome_error[kMutationKinds] = {};
    unsigned writer_ok = 0;
    unsigned writer_refused = 0;
    double max_writer_ms = 0.0;
    unsigned total_ok = 0;
    unsigned total_refused = 0;
    double max_case_ms = baseline.millis;
    std::uint64_t slow_case_seed = 0;
    unsigned slow_case_index = 0;
    Clock::time_point const run_start = Clock::now();

    for (unsigned index = 0; index < options.cases; ++index) {
        progress.note("case " + std::to_string(index) + " seed " +
                      fuzz::hex64(options.seed + static_cast<std::uint64_t>(index) *
                                                     0x9E3779B97F4A7C15ULL));

        std::vector<std::string> log;
        unsigned last_kind = 0;
        WriterOutcome writer;
        Outcome const outcome =
            run_indexed_case(pristine, model, options.seed, index, log, last_kind,
                             options.writer_parse, writer);
        max_case_ms = std::max(max_case_ms, outcome.millis);
        max_writer_ms = std::max(max_writer_ms, writer.millis);

        std::string const receipt = case_receipt(index, options.seed, log);
        if (outcome.wrong_exception) {
            checker.fail(receipt + " threw " + outcome.error);
            break;
        }
        if (outcome.ok && !outcome.error.empty()) {
            checker.fail(receipt + " returned an inconsistent document: " +
                         outcome.error);
            break;
        }
        if (options.writer_parse) {
            if (writer.wrong_exception || !writer.clean) {
                checker.fail(receipt + " threw on the writer side: " + writer.error);
                break;
            }
            if (writer.parsed && !writer.error.empty()) {
                checker.fail(receipt + " returned an inconsistent CfbDocument: " +
                             writer.error);
                break;
            }
            if (writer.parsed) {
                ++writer_ok;
            } else {
                ++writer_refused;
            }
        }
        if (outcome.ok) {
            ++outcome_ok[last_kind % kMutationKinds];
            ++total_ok;
        } else {
            ++outcome_error[last_kind % kMutationKinds];
            ++total_refused;
        }
        if (outcome.millis > kSlowCaseMs) {
            slow_case_seed = options.seed +
                             static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL;
            slow_case_index = index;
        }

        /* Determinism: the first few cases must reproduce their outcome. */
        if (index < 8) {
            std::vector<std::string> replay_log;
            unsigned replay_kind = 0;
            WriterOutcome replay_writer;
            Outcome const replay = run_indexed_case(
                pristine, model, options.seed, index, replay_log, replay_kind,
                options.writer_parse, replay_writer);
            checker.check(replay.ok == outcome.ok &&
                              replay.tasks == outcome.tasks &&
                              replay.links == outcome.links &&
                              replay_log == log,
                          receipt + " is not reproducible on a second run");
            if (options.writer_parse) {
                checker.check(replay_writer.parsed == writer.parsed &&
                                  replay_writer.entries == writer.entries &&
                                  replay_writer.streams == writer.streams,
                              receipt + " writer side is not reproducible on a "
                                        "second run");
            }
        }
    }

    double const wall_ms = fuzz::elapsed_ms(run_start);
    std::cout << "cases: " << options.cases << ", ok " << total_ok << ", mpp::Error "
              << total_refused << ", max case " << fuzz::ms_text(max_case_ms)
              << ", total " << fuzz::ms_text(wall_ms) << "\n";
    if (options.writer_parse) {
        std::cout << "writer side: parsed " << writer_ok << ", mppwrite::Error "
                  << writer_refused << ", max case " << fuzz::ms_text(max_writer_ms)
                  << "\n";
    }
    for (unsigned kind = 0; kind < kMutationKinds; ++kind) {
        std::cout << "  kind " << kind << ": ok " << outcome_ok[kind] << ", error "
                  << outcome_error[kind] << "\n";
    }
    if (slow_case_seed != 0) {
        checker.fail("case " + std::to_string(slow_case_index) + " seed " +
                     fuzz::hex64(slow_case_seed) + " took longer than " +
                     fuzz::ms_text(kSlowCaseMs));
    }
    checker.check(total_ok > 0, "no mutated case ever produced a Document");
    checker.check(total_refused > 0, "no mutated case was ever refused");

    /* ---- the two real files, read-only, unmutated + a few mutations ---- */
    if (options.use_corpus) {
        std::vector<std::string> candidates;
        if (!options.corpus_dir.empty()) {
            candidates.push_back(options.corpus_dir);
        }
        candidates.push_back(FUZZ_CORPUS_DIR);
        candidates.push_back("corpus");
        std::string corpus_dir;
        for (std::string const& candidate : candidates) {
            std::error_code error;
            if (std::filesystem::is_directory(candidate, error)) {
                corpus_dir = candidate;
                break;
            }
        }
        if (corpus_dir.empty()) {
            std::cout << "SKIP: corpus absent: no corpus directory; "
                         "real-file baseline not run\n";
        } else {
            char const* const names[] = {"project_a.mpp",
                                         "project_b.mpp"};
            for (char const* name : names) {
                std::string const path = fuzz::join_path(corpus_dir, name);
                if (!fuzz::file_exists(path)) {
                    std::cout << "SKIP: corpus absent (" << path << ")\n";
                    continue;
                }
                Bytes const corpus = fuzz::read_file(path);
                Outcome const loaded = run_case(corpus, corpus.size());
                checker.check(loaded.ok, std::string("real file ") + name +
                                             " did not load: " + loaded.error);
                if (!loaded.ok) {
                    continue;
                }
                std::cout << "real file " << name << ": " << loaded.tasks
                          << " tasks, " << loaded.links << " links, "
                          << loaded.assignments << " assignments (read only)\n";
                Model const corpus_model = build_model(corpus);
                unsigned const corpus_cases = 16;
                unsigned corpus_ok = 0;
                unsigned corpus_refused = 0;
                for (unsigned index = 0; index < corpus_cases; ++index) {
                    std::uint64_t const case_seed =
                        options.seed + 0x5EED0000ULL +
                        static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL;
                    progress.note(std::string("real file ") + name + " case " +
                                  std::to_string(index) + " seed " +
                                  fuzz::hex64(case_seed));
                    Bytes bytes = corpus;
                    fuzz::Rng rng(case_seed);
                    std::vector<std::string> log;
                    unsigned const mutations = 1 + rng.below(4);
                    for (unsigned mutation = 0; mutation < mutations; ++mutation) {
                        unsigned const kind = rng.below(kMutationKinds);
                        std::string entry;
                        if (!apply_mutation(bytes, corpus_model, rng, kind, entry)) {
                            break;
                        }
                        log.push_back(entry);
                    }
                    Outcome const outcome = run_case(bytes, corpus.size());
                    WriterOutcome const writer =
                        options.writer_parse ? run_writer_case(bytes)
                                             : WriterOutcome{};
                    if (outcome.wrong_exception ||
                        (outcome.ok && !outcome.error.empty())) {
                        checker.fail(std::string(name) + " case " +
                                     std::to_string(index) + " seed " +
                                     fuzz::hex64(case_seed) + " mutations [" +
                                     join_log(log) + "]: " +
                                     (outcome.wrong_exception ? "threw " : "inconsistent: ") +
                                     outcome.error);
                        break;
                    }
                    if (options.writer_parse &&
                        (writer.wrong_exception || !writer.clean ||
                         (writer.parsed && !writer.error.empty()))) {
                        checker.fail(std::string(name) + " case " +
                                     std::to_string(index) + " seed " +
                                     fuzz::hex64(case_seed) + " mutations [" +
                                     join_log(log) + "]: writer side " +
                                     (writer.wrong_exception ? "threw " : writer.error));
                        break;
                    }
                    if (outcome.ok) {
                        ++corpus_ok;
                    } else {
                        ++corpus_refused;
                    }
                }
                std::cout << "real file " << name << ": " << corpus_cases
                          << " mutations, ok " << corpus_ok << ", error "
                          << corpus_refused << "\n";
            }
        }
    }

    return checker.report("fuzz_reader");
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
        progress = fuzz::join_path(FUZZ_OUT_DIR, "fuzz_reader.progress");
    }
    std::cout << "fuzz_reader: supervising worker, timeout " << options.timeout_ms
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
