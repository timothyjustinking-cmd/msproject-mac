/*
 * fuzz_writer_main.cpp -- deterministic edit-sequence fuzz for the .mpp writer.
 *
 * WHAT IS FUZZED.  `tests/data/synthetic-base.mpp` is copied into the fuzz build
 * directory and then a seeded sequence of the public edit APIs is applied to it:
 *
 *   add_task_row, delete_task_row, add_task_link, delete_task_link,
 *   add_resource_row, add_assignment_row, delete_assignment_row,
 *   save_field_values, clear_field_value
 *
 * (The W54 assignment APIs are present in core/mppwrite/add_delete.h, so they
 * are in the sequence.)  Roughly one operation in five is deliberately invalid
 * (duplicate uid, uid 0, an unknown uid, an empty name, a Cost custom field) so
 * the refusal paths are exercised, not only the happy path.
 *
 * WHAT EVERY OPERATION MUST PROVE
 *   on success:
 *     - `CfbDocument::check()` and `check_table()` are clean;
 *     - the file on disk equals `doc.to_bytes()` (byte-identical re-emit);
 *     - the independent reader (`mpp::load_file`) reports exactly the counts the
 *       operation log predicts (task/link/assignment/resource deltas; a task
 *       delete's cascade deltas are computed from the previous reader view);
 *     - a delete never changes the file size (a tombstone shrinks nothing);
 *       every other operation never shrinks the file;
 *   on refusal:
 *     - the file did not appear, or its bytes are unchanged;
 *     - `doc.to_bytes()` is unchanged, so the in-memory document was not
 *       partially edited.
 *
 * All output files go under the fuzz build directory (never tests/data, never
 * corpus).  The seed and operation index are printed, so any failure is
 * reproducible with `--seeds`/`--ops`/`--seed`.
 *
 * USAGE
 *   fuzz_writer [--seeds N] [--ops N] [--seed N] [--fixture PATH] [--out DIR]
 *               [--timeout-ms N] [--no-watchdog] [--progress PATH]
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "add_delete.h"
#include "cfb_writer.h"
#include "field_values.h"
#include "fuzz_common.h"
#include "fuzz_watchdog.h"
#include "table.h"

#include "reader.h"

#ifndef FUZZ_TEST_DATA_DIR
#define FUZZ_TEST_DATA_DIR "tests/data"
#endif
#ifndef FUZZ_OUT_DIR
#define FUZZ_OUT_DIR "build/fuzz-out"
#endif

namespace {

using fuzz::Bytes;

constexpr char const* kFixtureName = "synthetic-base.mpp";

/* The reader's and the writer's field tables are separate copies of the measured
 * ids; the fuzz cross-check relies on them agreeing for Text1. */
static_assert(mpp::custom_field_id(mpp::FieldKind::text, 1) ==
                  mppwrite::kText1FieldId,
              "reader and writer Text1 ids must agree");

struct Options {
    unsigned seeds = 6;
    unsigned ops = 40;
    std::uint64_t seed = 0x57;
    std::string fixture;
    std::string out_dir;
    std::string corpus_dir;
    bool use_corpus = true;
    bool watchdog = true;
    unsigned timeout_ms = 60000;
    std::string progress;
    bool worker = false;
};

void print_usage() {
    std::cout << "usage: fuzz_writer [--seeds N] [--ops N] [--seed N] "
                 "[--fixture PATH] [--out DIR]\n"
                 "                   [--corpus DIR] [--no-corpus] "
                 "[--timeout-ms N] [--no-watchdog] [--progress PATH]\n"
                 "  --seeds N       number of independent edit sequences (default 6)\n"
                 "  --ops N         operations per sequence (default 40)\n"
                 "  --seed N        base seed, decimal or 0x-hex (default 0x57)\n"
                 "  --out DIR       output directory (default " FUZZ_OUT_DIR ")\n"
                 "  --corpus DIR    directory holding the real files, for the\n"
                 "                  damaged-table refusal control (read only)\n"
                 "  --no-corpus     skip that control\n";
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
        } else if (argument == "--no-corpus") {
            options.use_corpus = false;
        } else if (argument == "--corpus") {
            if (!take_value(argc, argv, i, options.corpus_dir)) {
                error = "missing value for --corpus";
                return false;
            }
        } else if (argument == "--seeds") {
            std::uint64_t parsed = 0;
            if (!take_value(argc, argv, i, value) || !parse_u64(value, parsed) ||
                parsed == 0 || parsed > 1000) {
                error = "bad --seeds value";
                return false;
            }
            options.seeds = static_cast<unsigned>(parsed);
        } else if (argument == "--ops") {
            std::uint64_t parsed = 0;
            if (!take_value(argc, argv, i, value) || !parse_u64(value, parsed) ||
                parsed == 0 || parsed > 10000) {
                error = "bad --ops value";
                return false;
            }
            options.ops = static_cast<unsigned>(parsed);
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
        } else if (argument == "--out") {
            if (!take_value(argc, argv, i, options.out_dir)) {
                error = "missing value for --out";
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

/* ---- operation planning ------------------------------------------------ */

enum class Op : unsigned {
    add_task = 0,
    delete_task,
    add_link,
    delete_link,
    add_resource,
    add_assignment,
    delete_assignment,
    save_field,
    clear_field,
    count,
};

char const* op_name(Op op) {
    switch (op) {
        case Op::add_task:
            return "add_task";
        case Op::delete_task:
            return "delete_task";
        case Op::add_link:
            return "add_link";
        case Op::delete_link:
            return "delete_link";
        case Op::add_resource:
            return "add_resource";
        case Op::add_assignment:
            return "add_assignment";
        case Op::delete_assignment:
            return "delete_assignment";
        case Op::save_field:
            return "save_field";
        case Op::clear_field:
            return "clear_field";
        default:
            return "?";
    }
}

struct Counts {
    std::size_t tasks = 0;
    std::size_t resources = 0;
    std::size_t links = 0;
    std::size_t assignments = 0;
};

Counts counts_of(mpp::Document const& document) {
    Counts counts;
    counts.tasks = document.tasks.size();
    counts.resources = document.resources.size();
    counts.links = document.links.size();
    counts.assignments = document.assignments.size();
    return counts;
}

struct PlannedOp {
    Op op = Op::add_task;
    bool invalid = false;
    std::uint32_t uid = 0;
    std::uint32_t other_uid = 0;
    std::uint32_t link_uid = 0;
    std::uint16_t link_type = 1;
    std::int32_t lag = 0;
    std::uint32_t field_id = 0;
    std::string text;
    long delta_tasks = 0;
    long delta_links = 0;
    long delta_assignments = 0;
    long delta_resources = 0;
    bool delete_like = false;
};

std::uint32_t pick_task_uid(mpp::Document const& view, fuzz::Rng& rng) {
    if (view.tasks.empty()) {
        return 0;
    }
    return view.tasks[rng.below(static_cast<std::uint32_t>(view.tasks.size()))].uid;
}

PlannedOp plan_op(mpp::Document const& view, fuzz::Rng& rng, unsigned sequence_index) {
    PlannedOp plan;
    plan.op = static_cast<Op>(rng.below(static_cast<std::uint32_t>(Op::count)));
    plan.invalid = rng.below(5) == 0;

    switch (plan.op) {
        case Op::add_task: {
            plan.delta_tasks = 1;
            plan.text = "fuzz-task-" + std::to_string(sequence_index);
            if (plan.invalid) {
                if (rng.below(2) == 0 && !view.tasks.empty()) {
                    plan.uid = view.tasks.front().uid; /* duplicate uid */
                } else {
                    plan.text.clear(); /* empty name */
                }
            }
            break;
        }
        case Op::delete_task: {
            plan.delete_like = true;
            plan.delta_tasks = -1;
            plan.uid = pick_task_uid(view, rng);
            for (mpp::Link const& link : view.links) {
                if (link.predecessor_uid == plan.uid || link.successor_uid == plan.uid) {
                    --plan.delta_links;
                }
            }
            for (mpp::Assignment const& assignment : view.assignments) {
                if (assignment.task_uid == plan.uid) {
                    --plan.delta_assignments;
                }
            }
            if (plan.invalid) {
                plan.uid = rng.below(2) == 0 ? 0u : 0xFFFFFFFDu;
            }
            break;
        }
        case Op::add_link: {
            plan.delta_links = 1;
            plan.uid = pick_task_uid(view, rng);
            plan.other_uid = pick_task_uid(view, rng);
            plan.link_type = static_cast<std::uint16_t>(rng.below(4));
            plan.lag = static_cast<std::int32_t>(rng.below(9602)) - 4801;
            bool duplicate = false;
            for (mpp::Link const& link : view.links) {
                if (link.predecessor_uid == plan.uid &&
                    link.successor_uid == plan.other_uid &&
                    static_cast<std::uint16_t>(link.type_raw & 0x3u) == plan.link_type) {
                    duplicate = true;
                }
                if (link.predecessor_uid == plan.other_uid &&
                    link.successor_uid == plan.uid &&
                    static_cast<std::uint16_t>(link.type_raw & 0x3u) == plan.link_type) {
                    duplicate = true;
                }
            }
            if (plan.invalid) {
                if (rng.below(2) == 0) {
                    plan.other_uid = plan.uid; /* self link */
                } else {
                    plan.other_uid = 0; /* no such task */
                }
            } else if (duplicate || plan.uid == plan.other_uid ||
                       plan.uid == 0 || plan.other_uid == 0) {
                plan.invalid = true;
                plan.other_uid = rng.below(2) == 0 ? 0u : plan.uid;
            }
            break;
        }
        case Op::delete_link: {
            plan.delete_like = true;
            plan.delta_links = -1;
            if (!view.links.empty()) {
                plan.link_uid =
                    view.links[rng.below(static_cast<std::uint32_t>(view.links.size()))]
                        .uid;
            }
            if (plan.invalid) {
                plan.link_uid = rng.below(2) == 0 ? 0u : 0xFFFFFFFCu;
            }
            break;
        }
        case Op::add_resource: {
            plan.delta_resources = 1;
            plan.text = "fuzz-resource-" + std::to_string(sequence_index);
            if (plan.invalid) {
                plan.text.clear(); /* empty name refuses */
            }
            break;
        }
        case Op::add_assignment: {
            plan.delta_assignments = 1;
            plan.uid = pick_task_uid(view, rng);
            if (!view.resources.empty()) {
                plan.other_uid =
                    view.resources[rng.below(static_cast<std::uint32_t>(
                                              view.resources.size()))]
                        .uid;
            }
            bool duplicate = false;
            for (mpp::Assignment const& assignment : view.assignments) {
                if (assignment.task_uid == plan.uid &&
                    assignment.has_resource() &&
                    assignment.resource_uid() == plan.other_uid) {
                    duplicate = true;
                }
            }
            if (plan.invalid || duplicate || plan.uid == 0) {
                plan.invalid = true;
                plan.other_uid = rng.below(2) == 0 ? 0u : 0xFFFFFFFDu;
            }
            break;
        }
        case Op::delete_assignment: {
            plan.delete_like = true;
            plan.delta_assignments = -1;
            if (!view.assignments.empty()) {
                mpp::Assignment const& assignment =
                    view.assignments[rng.below(
                        static_cast<std::uint32_t>(view.assignments.size()))];
                plan.uid = assignment.uid;
            }
            if (plan.invalid) {
                plan.uid = rng.below(2) == 0 ? 0u : 0xFFFFFFFDu;
            }
            break;
        }
        case Op::save_field:
        case Op::clear_field: {
            plan.uid = pick_task_uid(view, rng);
            switch (rng.below(3)) {
                case 0:
                    plan.field_id = mppwrite::kText1FieldId;
                    plan.text = "fuzz-" + std::to_string(sequence_index);
                    break;
                case 1:
                    plan.field_id = mppwrite::kNumber1FieldId;
                    plan.text = "42.5";
                    break;
                default:
                    plan.field_id = mppwrite::kFlag1FieldId;
                    plan.text = "true";
                    break;
            }
            if (plan.invalid) {
                plan.field_id = mppwrite::kCost1FieldId; /* unmeasured storage */
                plan.uid = rng.below(2) == 0 ? 0u : plan.uid;
            }
            break;
        }
        default:
            break;
    }
    return plan;
}

/* ---- operation execution ----------------------------------------------- */

struct RunState {
    mppwrite::CfbDocument doc;
    mpp::Document view;
    std::string path;
    std::size_t file_size = 0;
    bool file_present = false;
};

bool execute(RunState& state, PlannedOp const& plan, std::string& detail,
             std::string& refusal) {
    try {
        switch (plan.op) {
            case Op::add_task: {
                mppwrite::NewTaskRow row;
                row.name = plan.text;
                row.uid = plan.uid;
                mppwrite::AddedRow const added =
                    mppwrite::add_task_row(state.doc, row, state.path);
                detail = "uid " + std::to_string(added.uid);
                break;
            }
            case Op::delete_task: {
                mppwrite::DeletedRow const deleted =
                    mppwrite::delete_task_row(state.doc, plan.uid, state.path);
                detail = "uid " + std::to_string(deleted.uid) + ", links " +
                         std::to_string(deleted.link_rows_tombstoned) +
                         ", assignments " +
                         std::to_string(deleted.assignment_rows_tombstoned);
                break;
            }
            case Op::add_link: {
                mppwrite::AddedLink const added = mppwrite::add_task_link(
                    state.doc, plan.uid, plan.other_uid, plan.link_type, plan.lag,
                    state.path);
                detail = std::to_string(added.uid) + ": " +
                         std::to_string(added.predecessor_uid) + "->" +
                         std::to_string(added.successor_uid);
                break;
            }
            case Op::delete_link: {
                mppwrite::DeletedLink const deleted =
                    mppwrite::delete_task_link(state.doc, plan.link_uid, state.path);
                detail = "uid " + std::to_string(deleted.uid);
                break;
            }
            case Op::add_resource: {
                mppwrite::ResourceSpec spec;
                spec.name = plan.text;
                spec.max_units_percent = 100.0;
                spec.kind = mppwrite::ResourceKind::work;
                mppwrite::AddedResource const added =
                    mppwrite::add_resource_row(state.doc, spec, state.path);
                detail = "uid " + std::to_string(added.uid);
                break;
            }
            case Op::add_assignment: {
                mppwrite::AssignmentSpec spec;
                spec.task_uid = plan.uid;
                spec.resource_uid = plan.other_uid;
                mppwrite::AddedAssignment const added =
                    mppwrite::add_assignment_row(state.doc, spec, state.path);
                detail = "uid " + std::to_string(added.uid) + " task " +
                         std::to_string(added.task_uid) + " resource " +
                         std::to_string(added.resource_uid);
                break;
            }
            case Op::delete_assignment: {
                mppwrite::DeletedAssignment const deleted =
                    mppwrite::delete_assignment_row(state.doc, plan.uid, state.path);
                detail = "uid " + std::to_string(deleted.uid);
                break;
            }
            case Op::save_field: {
                mppwrite::FieldValue value;
                value.field_id = plan.field_id;
                value.kind = mppwrite::field_kind_for_id(plan.field_id);
                if (value.kind == mppwrite::FieldKind::text) {
                    value.text = plan.text;
                } else if (value.kind == mppwrite::FieldKind::number) {
                    value.number = 42.5;
                } else if (value.kind == mppwrite::FieldKind::flag) {
                    value.flag = true;
                } else if (value.kind == mppwrite::FieldKind::duration) {
                    value.duration_ticks = 4800;
                } else if (value.kind == mppwrite::FieldKind::date) {
                    value.packed_date = 0x3D1812C0u;
                }
                mppwrite::FieldEdit edit;
                edit.uid = plan.uid;
                edit.value = value;
                mppwrite::FieldWriteResult const result =
                    mppwrite::save_field_values(state.doc, {edit}, state.path);
                detail = "field " + std::to_string(plan.field_id) + " uid " +
                         std::to_string(plan.uid) + ", written " +
                         std::to_string(result.fields_written);
                break;
            }
            case Op::clear_field: {
                mppwrite::ClearedField const cleared = mppwrite::clear_field_value(
                    state.doc, plan.uid, plan.field_id, state.path);
                detail = "field " + std::to_string(cleared.field_id) + " uid " +
                         std::to_string(cleared.uid);
                break;
            }
            default:
                refusal = "unplanned operation";
                return false;
        }
    } catch (mppwrite::Error const& error) {
        refusal = error.what();
        return false;
    } catch (std::exception const& error) {
        refusal = std::string("non-mppwrite exception: ") + error.what();
        return false;
    }
    return true;
}

/* The cross-check the brief asks for: the reader's counts against the log. */
void verify_success(fuzz::Checker& checker, RunState& state, PlannedOp const& plan,
                    Counts const& before, std::size_t before_size, bool before_present,
                    std::string const& receipt) {
    std::string const container = state.doc.check();
    checker.check(container.empty(), receipt + ": CfbDocument::check: " + container);
    std::string const table = mppwrite::check_table(state.doc, "114/TBkndTask");
    checker.check(table.empty(), receipt + ": check_table: " + table);

    Bytes const disk = fuzz::read_file(state.path);
    checker.check(disk == state.doc.to_bytes(),
                  receipt + ": the file on disk differs from doc.to_bytes()");

    mpp::Document const reread = mpp::load_file(state.path);
    Counts const after = counts_of(reread);
    checker.equal(after.tasks, static_cast<std::size_t>(
                                   static_cast<long>(before.tasks) + plan.delta_tasks),
                  receipt + ": task count");
    checker.equal(after.links, static_cast<std::size_t>(
                                   static_cast<long>(before.links) + plan.delta_links),
                  receipt + ": link count");
    checker.equal(after.assignments,
                  static_cast<std::size_t>(static_cast<long>(before.assignments) +
                                           plan.delta_assignments),
                  receipt + ": assignment count");
    checker.equal(after.resources,
                  static_cast<std::size_t>(static_cast<long>(before.resources) +
                                           plan.delta_resources),
                  receipt + ": resource count");

    if (plan.delete_like) {
        checker.equal(disk.size(), before_size,
                      receipt + ": a delete must not change the file size");
    } else {
        checker.check(disk.size() >= before_size,
                      receipt + ": a write shrank the file (" +
                          std::to_string(before_size) + " -> " +
                          std::to_string(disk.size()) + ")");
    }
    if (!before_present) {
        checker.check(fuzz::file_exists(state.path),
                      receipt + ": the operation reported success but wrote no file");
    }

    /* A text custom field written by the writer must be visible to the reader. */
    if (plan.op == Op::save_field && !plan.invalid &&
        plan.field_id == mppwrite::kText1FieldId) {
        mpp::CustomFieldValue const value =
            reread.custom_field(plan.uid, mpp::custom_field_id(mpp::FieldKind::text, 1));
        checker.check(value.present, receipt + ": the reader does not see Text1");
    }

    state.view = reread;
    state.file_size = disk.size();
    state.file_present = true;
}

void verify_refusal(fuzz::Checker& checker, RunState const& state,
                    std::uint64_t doc_hash, std::size_t before_size,
                    bool before_present, std::string const& receipt) {
    checker.check(fuzz::fnv1a64(state.doc.to_bytes()) == doc_hash,
                  receipt + ": a refusal modified the in-memory document");
    bool const present_now = fuzz::file_exists(state.path);
    checker.check(present_now == before_present,
                  receipt + ": a refusal created or removed the output file");
    if (present_now) {
        checker.equal(fuzz::read_file(state.path).size(), before_size,
                      receipt + ": a refusal changed the file size");
    }
}

/*
 * The damaged-table control.  The real PROJECT_A file's Task FixedMeta header declares
 * 87 entries and holds 88, so the writer must REFUSE a structural edit rather
 * than guess a repair (memory/writer.md, spec/mpp-writer.md §5.3).  This is the
 * one place the fuzz lane touches a real file; it is copied into the build tree
 * and never written back.
 */
void check_damaged_table_refusal(fuzz::Checker& checker, std::string const& out_dir,
                                 std::string const& corpus_dir) {
    std::string const source = fuzz::join_path(corpus_dir, "project_a.mpp");
    if (!fuzz::file_exists(source)) {
        std::cout << "SKIP: damaged-table refusal control (no PROJECT_A corpus file)\n";
        return;
    }
    Bytes const corpus = fuzz::read_file(source);
    std::string const path = fuzz::join_path(out_dir, "damaged-table-refusal.mpp");
    fuzz::write_file(path, corpus);
    Bytes const before = fuzz::read_file(path);

    bool add_refused = false;
    try {
        mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_bytes(corpus);
        mppwrite::NewTaskRow row;
        row.name = "w57-damaged-table";
        (void)mppwrite::add_task_row(doc, row, path);
    } catch (mppwrite::Error const&) {
        add_refused = true;
    }
    checker.check(add_refused,
                  "damaged Task table: add_task_row must refuse the PROJECT_A table, "
                  "not guess a repair");
    checker.check(fuzz::read_file(path) == before,
                  "damaged Task table: the add refusal modified the file");

    mpp::Document const view = mpp::load_file(source);
    if (!view.tasks.empty()) {
        bool delete_refused = false;
        try {
            mppwrite::CfbDocument doc = mppwrite::CfbDocument::from_bytes(corpus);
            (void)mppwrite::delete_task_row(doc, view.tasks.front().uid, path);
        } catch (mppwrite::Error const&) {
            delete_refused = true;
        }
        checker.check(delete_refused,
                      "damaged Task table: delete_task_row must refuse the PROJECT_A "
                      "table, not guess a repair");
        checker.check(fuzz::read_file(path) == before,
                      "damaged Task table: the delete refusal modified the file");
    }
    std::cout << "damaged-table control: add refused = " << (add_refused ? "yes" : "no")
              << " (file left byte-identical)\n";
}

std::string seed_path(std::string const& out_dir, unsigned seed) {
    return fuzz::join_path(out_dir, "writer-seed-" + std::to_string(seed) + ".mpp");
}

int run_worker(Options const& options) {
    fuzz::Checker checker;

    std::string const out_dir =
        options.out_dir.empty() ? FUZZ_OUT_DIR : options.out_dir;
    std::error_code directory_error;
    std::filesystem::create_directories(out_dir, directory_error);
    if (directory_error) {
        std::cout << "FAIL: cannot create the output directory " << out_dir << ": "
                  << directory_error.message() << "\n";
        return 1;
    }
    std::string progress_path = options.progress;
    if (progress_path.empty()) {
        progress_path = fuzz::join_path(out_dir, "fuzz_writer.progress");
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
    std::cout << "output: " << out_dir << "\n";

    unsigned op_ok[static_cast<unsigned>(Op::count)] = {};
    unsigned op_refused[static_cast<unsigned>(Op::count)] = {};
    unsigned total_ok = 0;
    unsigned total_refused = 0;

    for (unsigned seed_index = 0; seed_index < options.seeds; ++seed_index) {
        unsigned const sequence = static_cast<unsigned>(options.seed & 0xFFFFu) +
                                  seed_index * 131u;
        RunState state;
        state.path = seed_path(out_dir, seed_index);
        state.doc = mppwrite::CfbDocument::from_bytes(pristine);
        state.view = mpp::load_bytes(pristine);
        state.file_size = pristine.size(); /* the logical source before the first write */
        state.file_present = false;
        std::error_code remove_error;
        std::filesystem::remove(state.path, remove_error);

        fuzz::Rng rng(options.seed + static_cast<std::uint64_t>(seed_index) *
                                       0xD1B54A32D192ED03ULL);
        std::cout << "sequence " << seed_index << " (" << state.path << "): "
                  << options.ops << " operations\n";

        for (unsigned op_index = 0; op_index < options.ops; ++op_index) {
            progress.note("sequence " + std::to_string(seed_index) + " operation " +
                          std::to_string(op_index));
            PlannedOp const plan = plan_op(state.view, rng, sequence + op_index);
            Counts const before = counts_of(state.view);
            std::size_t const before_size = state.file_size;
            bool const before_present = state.file_present;
            std::uint64_t const doc_hash = fuzz::fnv1a64(state.doc.to_bytes());
            std::string const receipt =
                "seed " + std::to_string(seed_index) + " op " +
                std::to_string(op_index) + " " + op_name(plan.op) +
                (plan.invalid ? " (invalid)" : "");

            std::string detail;
            std::string refusal;
            bool const ok = execute(state, plan, detail, refusal);
            if (ok) {
                ++op_ok[static_cast<unsigned>(plan.op)];
                ++total_ok;
                verify_success(checker, state, plan, before, before_size,
                               before_present, receipt);
                std::cout << "  " << receipt << " -> " << detail << ", tasks "
                          << state.view.tasks.size() << ", links "
                          << state.view.links.size() << ", assignments "
                          << state.view.assignments.size() << "\n";
            } else {
                ++op_refused[static_cast<unsigned>(plan.op)];
                ++total_refused;
                std::cout << "  " << receipt << " -> refused: " << refusal << "\n";
                verify_refusal(checker, state, doc_hash, before_size, before_present,
                               receipt);
                if (plan.invalid) {
                    checker.check(!refusal.empty(),
                                  receipt + ": an invalid request was not refused");
                }
            }
            if (checker.failures() > 0) {
                std::cout << "FAIL: stopping this sequence after the first failure\n";
                break;
            }
        }
        if (checker.failures() > 0) {
            break;
        }
    }

    if (options.use_corpus) {
        std::vector<std::string> corpus_candidates;
        if (!options.corpus_dir.empty()) {
            corpus_candidates.push_back(options.corpus_dir);
        }
        corpus_candidates.push_back(FUZZ_CORPUS_DIR);
        corpus_candidates.push_back("corpus");
        std::string corpus_dir;
        for (std::string const& candidate : corpus_candidates) {
            std::error_code is_dir_error;
            if (std::filesystem::is_directory(candidate, is_dir_error)) {
                corpus_dir = candidate;
                break;
            }
        }
        if (corpus_dir.empty()) {
            std::cout << "SKIP: no corpus directory (damaged-table control not run)\n";
        } else {
            check_damaged_table_refusal(checker, out_dir, corpus_dir);
        }
    }

    for (unsigned op = 0; op < static_cast<unsigned>(Op::count); ++op) {
        std::cout << "  " << op_name(static_cast<Op>(op)) << ": ok " << op_ok[op]
                  << ", refused " << op_refused[op] << "\n";
        checker.check(op_ok[op] > 0,
                      std::string("no successful ") + op_name(static_cast<Op>(op)) +
                          " operation in the whole run (coverage gap)");
    }
    checker.check(total_refused > 0, "no operation was ever refused");
    std::cout << "operations: " << total_ok << " succeeded, " << total_refused
              << " refused, " << checker.failures() << " failures\n";
    return checker.report("fuzz_writer");
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
        std::string const out_dir =
            options.out_dir.empty() ? FUZZ_OUT_DIR : options.out_dir;
        std::error_code mkdir_error;
        std::filesystem::create_directories(out_dir, mkdir_error);
        progress = fuzz::join_path(out_dir, "fuzz_writer.progress");
    }
    std::cout << "fuzz_writer: supervising worker, timeout " << options.timeout_ms
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
            std::cout << "FAIL: last started operation: " << line << "\n";
        } else {
            std::cout << "FAIL: no progress was recorded\n";
        }
    }
    return result;
}
