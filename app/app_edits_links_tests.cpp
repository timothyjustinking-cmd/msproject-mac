// app/app_edits_links_tests.cpp -- editing a task's predecessor links (wave 13).
//
// Two layers:
//   1. the Predecessors dialog MODEL (app/field_link_edits.h): the incoming-link
//      list, the lag syntax ("0", "2d", "-1d", "4h"), the type names, the add
//      validation (self, duplicate, unknown, pending insert/delete) and the
//      display plan/undo-redo of queued link edits;
//   2. the real writer on a COPY of the committed synthetic fixture
//      (tests/data/synthetic-base.mpp; its link table is EMPTY, see
//      tests/roundtrip_links.cpp) under APP_TEST_OUT_DIR: add two links, save,
//      re-read 2 links; delete one, save, re-read 1 link (2 -> 1); delete and
//      re-add the same relation in one Save (deletes run before adds); a
//      pre-existing target is byte-identical when the writer refuses; and the
//      original/source file is never written.
//   3. Unlink (W27-3.3): the both-direction link list on the synthetic
//      wave23-outline fixture, then the real save compared stream-for-stream
//      with Project 2021's own UnlinkTasks save (tests/data/wave27-unlink.mpp);
//      Project removes BOTH links touching the task, TaskDependencies 3 -> 1.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\vs-w55 -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\vs-w55 --config Release
//   ctest --test-dir build\vs-w55 -C Release -R "^app_edits_links$" --output-on-failure

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "add_delete.h"
#include "cfb.h"
#include "date.h"
#include "edit_model.h"
#include "field_link_edits.h"
#include "layout_model.h"
#include "reader_adapter.h"

namespace {

using core::gantt::LinkType;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_size(std::size_t got, std::size_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

std::size_t bytes_on_disk(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    return error ? 0 : static_cast<std::size_t>(size);
}

app::TaskRow make_task(std::uint32_t uid, std::uint32_t id, const std::string& name) {
    app::TaskRow row;
    row.uid = uid;
    row.id = id;
    row.name = name;
    row.duration_ticks = 4800;
    row.start = core::sched::from_ymd(2026, 1, 5, 8, 0);
    row.finish = row.start;
    return row;
}

app::PlanInput make_plan() {
    app::PlanInput plan;
    plan.tasks.push_back(make_task(1, 1, "Alpha"));
    plan.tasks.push_back(make_task(2, 2, "Beta"));
    plan.tasks.push_back(make_task(3, 3, "Gamma"));
    app::LinkRow first;
    first.uid = 11;
    first.predecessor_uid = 1;
    first.successor_uid = 2;
    first.type = LinkType::finish_to_start;
    first.lag_ticks = 0;
    app::LinkRow second;
    second.uid = 12;
    second.predecessor_uid = 2;
    second.successor_uid = 3;
    second.type = LinkType::start_to_start;
    second.lag_ticks = 4800;
    plan.links = {first, second};
    return plan;
}

// ---------------------------------------------------------------------------
// The dialog model
// ---------------------------------------------------------------------------

void test_lag_syntax() {
    std::int32_t ticks = 0;
    std::string error;
    struct Case {
        const char* text;
        std::int32_t ticks;
    };
    const Case accepted[] = {{"0", 0},       {"2", 9600},     {"2d", 9600},
                             {"-1d", -4800}, {"4h", 2400},    {"-4h", -2400},
                             {"30m", 300},   {" 3d ", 14400}, {"1.5d", 7200}};
    for (const Case& item : accepted) {
        ++g_checks;
        if (!app::parse_link_lag(item.text, ticks, error) || ticks != item.ticks) {
            ++g_failures;
            std::printf("FAIL: lag \"%s\" -> %d (wanted %d): %s\n", item.text,
                        static_cast<int>(ticks), static_cast<int>(item.ticks),
                        error.c_str());
        }
    }
    const char* refused[] = {"", "abc", "2w", "d", "2dd", "999999999d"};
    for (const char* text : refused) {
        ++g_checks;
        ticks = 123;
        if (app::parse_link_lag(text, ticks, error) || error.empty() || ticks != 0) {
            ++g_failures;
            std::printf("FAIL: lag \"%s\" should be refused\n", text);
        }
    }
    check_text(app::format_link_lag(0), "0", "0 formats as 0");
    check_text(app::format_link_lag(9600), "2d", "two days format as 2d");
    check_text(app::format_link_lag(-4800), "-1d", "a one-day lead formats as -1d");
    check_text(app::format_link_lag(2400), "4h", "four hours format as 4h");
    check_text(app::format_link_lag(300), "30m", "thirty minutes format as 30m");
    check_text(app::format_link_lag(7), "7", "a non-round tick count formats as ticks");

    LinkType type = LinkType::finish_to_start;
    check(app::parse_link_type("FF", type) && type == LinkType::finish_to_finish, "FF parses");
    check(app::parse_link_type("fs", type) && type == LinkType::finish_to_start, "fs parses");
    check(app::parse_link_type("SF", type) && type == LinkType::start_to_finish, "SF parses");
    check(app::parse_link_type("ss", type) && type == LinkType::start_to_start, "ss parses");
    check(!app::parse_link_type("XX", type), "an unknown type is refused");
    check_text(app::link_type_name(LinkType::finish_to_finish), "FF", "FF name");
    check_text(app::link_type_name(LinkType::finish_to_start), "FS", "FS name");
    check_text(app::link_type_name(LinkType::start_to_finish), "SF", "SF name");
    check_text(app::link_type_name(LinkType::start_to_start), "SS", "SS name");
}

void test_predecessor_list() {
    const app::PlanInput plan = make_plan();
    app::StructuralEdits edits;
    const std::vector<app::PredecessorListItem> beta = app::predecessors_of(plan, edits, 2);
    check_size(beta.size(), 1, "task 2 has one incoming link");
    if (!beta.empty()) {
        check(beta[0].link_uid == 11, "the file link uid is carried");
        check(beta[0].predecessor_uid == 1, "the predecessor uid is carried");
        check_text(beta[0].predecessor_id, "1", "the predecessor ID is shown");
        check_text(beta[0].predecessor_name, "Alpha", "the predecessor name is shown");
        check_text(beta[0].type_name, "FS", "the type is shown as FS");
        check_text(beta[0].lag_text, "0", "the lag is shown");
        check(!beta[0].pending, "a file link is not pending");
    }
    const std::vector<app::PredecessorListItem> gamma = app::predecessors_of(plan, edits, 3);
    check_size(gamma.size(), 1, "task 3 has one incoming link");
    if (!gamma.empty()) {
        check_text(gamma[0].type_name, "SS", "task 3's link is SS");
        check_text(gamma[0].lag_text, "1d", "task 3's lag is 1d");
    }
    check_size(app::predecessors_of(plan, edits, 1).size(), 0,
               "task 1 has no incoming link");

    // A pending delete disappears from the list; a pending add appears as pending.
    app::StructuralEdit remove;
    remove.kind = app::StructuralKind::delete_link;
    remove.uid = 2;
    remove.predecessor_uid = 1;
    remove.link_uid = 11;
    check(edits.push(remove), "the link delete is queued");
    check_size(app::predecessors_of(plan, edits, 2).size(), 0,
               "a pending link delete is not listed");
    app::StructuralEdit add;
    std::string pending_error;
    // Successor 3 already has the live link 2 -> 3, which W30-0.7 refuses; the
    // pending row is added into task 1, which has no incoming link.
    check(app::make_link_add(plan, edits, 2, 1, LinkType::finish_to_finish, "2d", add,
                             pending_error),
          "a link add is built");
    check(edits.push(add), "the link add is queued");
    const app::PlanInput shown = edits.apply(plan);
    const std::vector<app::PredecessorListItem> pending =
        app::predecessors_of(shown, edits, 1);
    bool saw_pending = false;
    for (const app::PredecessorListItem& item : pending) {
        if (item.pending && item.predecessor_uid == 2) {
            saw_pending = true;
            check(item.link_uid == 0, "a pending link has no file uid");
            check_text(item.type_name, "FF", "the pending link's type is shown");
            check_text(item.lag_text, "2d", "the pending link's lag is shown");
        }
    }
    check(saw_pending, "the pending add is listed");
}

void test_validation() {
    const app::PlanInput plan = make_plan();
    app::StructuralEdits edits;
    std::string error;
    check(app::validate_link_add(plan, edits, 1, 2, LinkType::finish_to_start) != "",
          "a duplicate relation is refused");
    // W30-0.7 (R29-7): successor 2 already has 1 -> 2 FS.  A second incoming
    // link -- even of another type -- is the shape whose Fixed2Meta byte the
    // old first/later rule fabricated, so it is refused.
    const std::string later = app::validate_link_add(
        plan, edits, 1, 2, LinkType::start_to_start);
    check(later.find("incoming link") != std::string::npos,
          "a second incoming link is refused with the later-link reason (W30-0.7)");
    check(app::validate_link_add(plan, edits, 2, 2, LinkType::finish_to_start) != "",
          "a self-link is refused");
    check(app::validate_link_add(plan, edits, 0, 2, LinkType::finish_to_start) != "",
          "uid 0 is refused");
    check(app::validate_link_add(plan, edits, 99, 2, LinkType::finish_to_start) != "",
          "an unknown predecessor is refused");
    check(app::validate_link_add(plan, edits, 1, 99, LinkType::finish_to_start) != "",
          "an unknown successor is refused");
    check(app::validate_link_add(plan, edits, 1, 3, LinkType::finish_to_start) != "",
          "a successor with a file incoming link is refused (W30-0.7)");

    // The measured shape: first links into successors with no incoming link and
    // no progress (synthetic-base's empty link table, 0 % rows).
    app::PlanInput open = make_plan();
    open.links.clear();
    check(app::validate_link_add(open, edits, 1, 2, LinkType::start_to_start) == "",
          "the same pair with another type is allowed when the successor is free");
    check(app::validate_link_add(open, edits, 1, 3, LinkType::finish_to_start) == "",
          "a new relation into a free successor is allowed");

    // W30-C5 (C3): a link whose endpoint task is not live is dropped from the
    // plan but stays a live row in the file; the file's link row could name this
    // successor, so the free-successor proof above does not hold.
    app::PlanInput skipped = open;
    skipped.skipped_links = 1;
    skipped.unreadable_links = 1;
    const std::string skipped_error =
        app::validate_link_add(skipped, app::StructuralEdits{}, 1, 3,
                               LinkType::finish_to_start);
    check(skipped_error.find("not live") != std::string::npos,
          "an add with a skipped link in the plan is refused (W30-C5)");

    // W30-0.7 (R29-7): the other falsified shape -- a link into a progressed
    // successor.  Wave 29 measured +8 as driving state (0x07 when the successor
    // has progress); no pair adds a link into a progressed task.
    app::PlanInput progressed = open;
    progressed.tasks[2].percent_complete = 25;
    const std::string progress_error =
        app::validate_link_add(progressed, app::StructuralEdits{}, 1, 3,
                               LinkType::finish_to_start);
    check(progress_error.find("25% complete") != std::string::npos,
          "a link into a progressed successor is refused (W30-0.7)");

    app::StructuralEdit add;
    error.clear();
    check(!app::make_link_add(plan, edits, 1, 2, LinkType::finish_to_start, "0", add, error),
          "the builder rejects the duplicate through validate");
    check(!error.empty(), "the duplicate refusal explains itself");
    check(!app::make_link_add(open, edits, 1, 3, LinkType::finish_to_start, "nope", add,
                              error),
          "the builder rejects only the lag");
    check(!error.empty(), "the lag refusal explains itself");
    check(app::make_link_add(open, edits, 1, 3, LinkType::finish_to_start, "2d", add,
                             error),
          "a valid add is built");
    check(add.kind == app::StructuralKind::add_link && add.uid == 3 &&
              add.predecessor_uid == 1 && add.link_type == LinkType::finish_to_start &&
              add.lag_ticks == 9600,
          "the built edit carries the relation");
    check(edits.push(add), "the add is queued");
    check(app::validate_link_add(open, edits, 1, 3, LinkType::finish_to_start) != "",
          "a duplicate pending add is refused");

    // A pending insert has no file row yet.
    app::StructuralEdits inserting;
    app::StructuralEdit insert;
    insert.kind = app::StructuralKind::insert_task;
    insert.name = "New";
    insert.uid = 4;
    check(inserting.push(insert), "the insert is queued");
    check(app::validate_link_add(plan, inserting, 4, 2, LinkType::finish_to_start) != "",
          "a pending inserted task cannot be linked yet");

    // A pending delete cannot take a new link.
    app::StructuralEdits deleting;
    app::StructuralEdit remove;
    remove.kind = app::StructuralKind::delete_task;
    remove.uid = 1;
    check(deleting.push(remove), "the delete is queued");
    check(app::validate_link_add(plan, deleting, 1, 3, LinkType::finish_to_start) != "",
          "a task with a pending delete is refused");

    // Delete builders.
    app::PredecessorListItem pending_item;
    pending_item.pending = true;
    app::StructuralEdit delete_edit;
    check(!app::make_link_delete(pending_item, 2, delete_edit, error),
          "a pending add cannot be deleted from the file");
    const std::vector<app::PredecessorListItem> items =
        app::predecessors_of(plan, app::StructuralEdits{}, 2);
    if (!items.empty()) {
        check(app::make_link_delete(items[0], 2, delete_edit, error),
              "a file link is deletable");
        check(delete_edit.kind == app::StructuralKind::delete_link &&
                  delete_edit.link_uid == 11 && delete_edit.uid == 2 &&
                  delete_edit.predecessor_uid == 1,
              "the delete edit names the link row");
    }
}

void test_push_and_display() {
    const app::PlanInput plan = make_plan();
    app::StructuralEdits edits;
    app::StructuralEdit edit;
    edit.kind = app::StructuralKind::add_link;
    edit.uid = 3;
    edit.predecessor_uid = 3;
    edit.link_type = LinkType::finish_to_start;
    check(!edits.push(edit), "a self-link is refused at push");
    edit.predecessor_uid = 1;
    edit.lag_ticks = mppwrite::kMaxLinkLagTicks + 1;
    check(!edits.push(edit), "an out-of-range lag is refused at push");
    edit.lag_ticks = 9600;
    check(edits.push(edit), "a link add is accepted");
    check(!edits.push(edit), "a duplicate pending add is refused");
    edit.kind = app::StructuralKind::delete_link;
    edit.link_uid = 0;
    check(!edits.push(edit), "a delete without a link uid is refused");
    edit.link_uid = 11;
    check(edits.push(edit), "a link delete is accepted");
    check(!edits.push(edit), "a duplicate link delete is refused");
    check_size(edits.link_edit_count(), 2, "two link edits are pending");

    const app::PlanInput shown = edits.apply(plan);
    check_size(shown.links.size(), 2, "the display plan drops the deleted link and adds one");
    bool saw_add = false;
    for (const app::LinkRow& link : shown.links) {
        if (link.predecessor_uid == 1 && link.successor_uid == 3 &&
            link.type == LinkType::finish_to_start) {
            saw_add = true;
            check(link.uid == 0, "a pending link has no file uid");
            check(link.lag_ticks == 9600, "the pending link carries its lag");
        }
    }
    check(saw_add, "the pending add is in the display plan");
    bool saw_deleted = false;
    for (const app::LinkRow& link : shown.links) {
        if (link.uid == 11) {
            saw_deleted = true;
        }
    }
    check(!saw_deleted, "the deleted link is gone from the display plan");
    check_size(shown.skipped_links, 1, "the dropped link is counted as skipped");

    check(edits.undo(), "undo the delete");
    check_size(edits.apply(plan).links.size(), 3, "the file link is back");
    check(edits.undo(), "undo the add");
    check_size(edits.apply(plan).links.size(), 2, "the add is gone");
    check(edits.redo() && edits.redo(), "redo both");
    check_size(edits.apply(plan).links.size(), 2, "both edits are applied again");
}

void test_delete_prunes_adds() {
    const app::PlanInput plan = make_plan();
    app::StructuralEdits edits;
    app::StructuralEdit add;
    add.kind = app::StructuralKind::add_link;
    add.uid = 3;
    add.predecessor_uid = 2;
    add.link_type = LinkType::finish_to_finish;
    check(edits.push(add), "the link add is queued");
    check_size(edits.link_edit_count(), 1, "one link edit");
    app::StructuralEdit remove;
    remove.kind = app::StructuralKind::delete_task;
    remove.uid = 3;
    check(edits.push(remove), "the delete is queued");
    check_size(edits.link_edit_count(), 0, "the delete dropped the link add naming it");
}

// Project's Unlink Tasks removes BOTH directions (W27-3.3): the helper must
// list a link whether the selected task is its predecessor or its successor.
void test_saved_links_touching() {
    const app::PlanInput plan = make_plan();
    app::StructuralEdits edits;
    check_size(app::saved_links_touching(plan, edits, 1).size(), 1,
               "task 1 has one outgoing link");
    check_size(app::saved_links_touching(plan, edits, 2).size(), 2,
               "task 2 has one link at each end");
    check_size(app::saved_links_touching(plan, edits, 3).size(), 1,
               "task 3 has one incoming link");
    check_size(app::saved_links_touching(plan, edits, 99).size(), 0,
               "an unknown task has no saved link");

    const std::vector<app::TaskLinkItem> both =
        app::saved_links_touching(plan, edits, 2);
    if (both.size() == 2) {
        check(both[0].link_uid == 11 && both[0].predecessor_uid == 1 &&
                  both[0].successor_uid == 2,
              "the incoming link comes first in plan order");
        check(both[1].link_uid == 12 && both[1].predecessor_uid == 2 &&
                  both[1].successor_uid == 3,
              "the outgoing link follows it");
        app::StructuralEdit edit;
        std::string error;
        check(app::make_link_delete_edit(both[1], edit, error),
              "an outgoing link builds a delete edit");
        check(edit.kind == app::StructuralKind::delete_link && edit.uid == 3 &&
                  edit.predecessor_uid == 2 && edit.link_uid == 12,
              "the edit names the link's successor as uid, whichever end was selected");
    }

    app::StructuralEdit remove;
    remove.kind = app::StructuralKind::delete_link;
    remove.uid = 2;
    remove.predecessor_uid = 1;
    remove.link_uid = 11;
    check(edits.push(remove), "the incoming delete is queued");
    const std::vector<app::TaskLinkItem> after =
        app::saved_links_touching(plan, edits, 2);
    check_size(after.size(), 1, "a pending delete drops that link from the list");
    if (!after.empty()) {
        check(after[0].link_uid == 12, "the outgoing link is the one that survives");
    }

    app::StructuralEdits adds;
    app::StructuralEdit add;
    std::string error;
    check(app::make_link_add(plan, adds, 3, 1, LinkType::finish_to_start, "0", add,
                             error),
          "an unrelated link add is built");
    check(adds.push(add), "the link add is queued");
    const app::PlanInput shown = adds.apply(plan);
    check_size(app::saved_links_touching(shown, adds, 1).size(), 1,
               "a pending add is not a SAVED link, so Unlink leaves it to undo");
}

// ---------------------------------------------------------------------------
// The real writer sequence
// ---------------------------------------------------------------------------

#if defined(APP_TEST_DATA_DIR) && defined(APP_TEST_OUT_DIR)

struct Loaded {
    app::PlanInput plan;
    mpp::Document document;
    std::string path;
    bool ok = false;
};

Loaded load(const std::filesystem::path& path) {
    Loaded loaded;
    loaded.path = path.string();
    try {
        loaded.document = mpp::load_file(loaded.path);
        loaded.plan = app::plan_input_from_document(loaded.document);
        loaded.ok = true;
    } catch (const std::exception& error) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: cannot load %s: %s\n", loaded.path.c_str(), error.what());
    }
    return loaded;
}

std::string link_signature(const mpp::Link& link) {
    return std::to_string(link.predecessor_uid) + "->" +
           std::to_string(link.successor_uid) + " " + link.type_name() + " " +
           std::to_string(link.lag_ticks);
}

bool has_signature(const mpp::Document& document, const std::string& signature) {
    for (const mpp::Link& link : document.links) {
        if (link_signature(link) == signature) {
            return true;
        }
    }
    return false;
}

// One stream's bytes from each file, compared.  Throws become a named failure:
// the caller's check reports it without killing the rest of the test.
bool stream_equal(const std::filesystem::path& lhs, const std::filesystem::path& rhs,
                  const char* stream) {
    try {
        const mpp::CompoundFile left = mpp::CompoundFile::from_file(lhs.string());
        const mpp::CompoundFile right = mpp::CompoundFile::from_file(rhs.string());
        return left.read_stream(stream) == right.read_stream(stream);
    } catch (const std::exception& error) {
        std::printf("FAIL: cannot compare %s: %s\n", stream, error.what());
        return false;
    }
}

void test_real_round_trip() {
    const std::filesystem::path fixture =
        std::filesystem::path(APP_TEST_DATA_DIR) / "synthetic-base.mpp";
    if (!std::filesystem::exists(fixture)) {
        std::printf("SKIP: %s is not present; the link round trip needs the fixture\n",
                    fixture.string().c_str());
        return;
    }
    const std::size_t fixture_bytes = bytes_on_disk(fixture);
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);
    const std::filesystem::path copy =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-links-source.mpp";
    std::filesystem::remove(copy, error);
    std::filesystem::copy_file(fixture, copy, std::filesystem::copy_options::overwrite_existing,
                               error);
    check(!error, "the fixture was copied into the build tree");
    const std::size_t copy_bytes = bytes_on_disk(copy);

    Loaded base = load(copy);
    if (!base.ok || base.plan.tasks.size() != 3) {
        return;
    }
    check_size(base.plan.links.size(), 0, "the fixture has no link rows");
    const std::uint32_t first_uid = base.plan.tasks[0].uid;
    const std::uint32_t second_uid = base.plan.tasks[1].uid;
    const std::uint32_t third_uid = base.plan.tasks[2].uid;

    // ---- add two links ----------------------------------------------------
    app::StructuralEdits adds;
    std::string error_text;
    app::StructuralEdit first_add;
    check(app::make_link_add(base.plan, adds, first_uid, second_uid,
                             LinkType::finish_to_start, "0", first_add, error_text),
          "the first link add is built");
    check(adds.push(first_add), "the first link add is queued");
    app::StructuralEdit second_add;
    check(app::make_link_add(base.plan, adds, first_uid, third_uid,
                             LinkType::start_to_start, "1d", second_add, error_text),
          "the second link add is built");
    check(adds.push(second_add), "the second link add is queued");
    check_size(adds.link_edit_count(), 2, "two link adds are pending");
    check_size(adds.apply(base.plan).links.size(), 2, "the display plan shows both links");

    const std::filesystem::path two =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-links-two.mpp";
    std::filesystem::remove(two, error);
    const app::StructuralSaveResult add_outcome =
        app::save_structural_edits(adds, {}, copy.string(), two.string(), false);
    check(add_outcome.ok, "the two links save");
    if (!add_outcome.ok) {
        std::printf("SAVE ERROR: %s\n", add_outcome.error.c_str());
        return;
    }
    check_size(add_outcome.links_added, 2, "two links were added");
    check_size(add_outcome.links_deleted, 0, "nothing was deleted");
    check_size(add_outcome.added_link_uids.size(), 2, "the writer reported both link uids");
    check(add_outcome.api.find("add_task_link") != std::string::npos,
          "the outcome names add_task_link");
    check_size(bytes_on_disk(copy), copy_bytes, "the source copy is untouched");
    check(std::filesystem::exists(two), "the written file exists");
    check(!std::filesystem::exists(two.string() + ".w51-structural-tmp"),
          "no intermediate file is left behind");

    Loaded after_add = load(two);
    if (!after_add.ok) {
        return;
    }
    check_size(after_add.document.links.size(), 2, "the reader shows two links");
    check(has_signature(after_add.document, "1->2 FS 0"), "1->2 FS lag 0 reads back");
    check(has_signature(after_add.document, "1->3 SS 4800"), "1->3 SS +1d reads back");
    check_size(after_add.plan.links.size(), 2, "the app plan has both links");
    for (const app::LinkRow& link : after_add.plan.links) {
        check(link.uid != 0, "the app plan carries the file link uid");
        if (link.successor_uid == third_uid) {
            check(link.lag_ticks == 4800, "the app plan carries the link lag");
            check(link.type == LinkType::start_to_start, "the app plan carries the link type");
        }
    }

    // ---- delete one: 2 -> 1 links ----------------------------------------
    const std::vector<app::PredecessorListItem> incoming =
        app::predecessors_of(after_add.plan, app::StructuralEdits{}, second_uid);
    check_size(incoming.size(), 1, "task 2 has one incoming link after the save");
    app::StructuralEdits deletes;
    app::StructuralEdit delete_edit;
    error_text.clear();
    if (!incoming.empty()) {
        check(app::make_link_delete(incoming[0], second_uid, delete_edit, error_text),
              "the link delete is built");
        check(deletes.push(delete_edit), "the link delete is queued");
    }
    const std::filesystem::path one =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-links-one.mpp";
    std::filesystem::remove(one, error);
    const app::StructuralSaveResult delete_outcome =
        app::save_structural_edits(deletes, {}, two.string(), one.string(), false);
    check(delete_outcome.ok, "the link delete saves");
    if (delete_outcome.ok) {
        check_size(delete_outcome.links_deleted, 1, "one link was tombstoned");
        check(delete_outcome.api.find("delete_task_link") != std::string::npos,
              "the outcome names delete_task_link");
        Loaded after_delete = load(one);
        if (after_delete.ok) {
            check_size(after_delete.document.links.size(), 1, "the reader shows one live link");
            check(has_signature(after_delete.document, "1->3 SS 4800"),
                  "the surviving link is the other one");
            check_size(after_delete.plan.links.size(), 1, "the app plan agrees");
        }
    }

    // ---- delete and re-add the same relation in ONE save ------------------
    // The writer's delete runs before the add, so a re-add that validate_link_add
    // would refuse against the unchanged file is legal here.
    Loaded for_readd = load(two);
    if (for_readd.ok) {
        const std::vector<app::PredecessorListItem> readd_incoming =
            app::predecessors_of(for_readd.plan, app::StructuralEdits{}, second_uid);
        app::StructuralEdits readd;
        app::StructuralEdit remove;
        error_text.clear();
        if (!readd_incoming.empty() &&
            app::make_link_delete(readd_incoming[0], second_uid, remove, error_text)) {
            check(readd.push(remove), "the delete half is queued");
        }
        app::StructuralEdit add;
        check(app::make_link_add(readd.apply(for_readd.plan), readd, first_uid, second_uid,
                                 LinkType::finish_to_start, "0", add, error_text),
              "the re-add is validated against the display plan (the delete is applied)");
        check(readd.push(add), "the add half is queued");
        const std::filesystem::path readded =
            std::filesystem::path(APP_TEST_OUT_DIR) / "app-links-readded.mpp";
        std::filesystem::remove(readded, error);
        const app::StructuralSaveResult readd_outcome =
            app::save_structural_edits(readd, {}, two.string(), readded.string(), false);
        check(readd_outcome.ok, "the delete + re-add save succeeds");
        if (readd_outcome.ok) {
            check_size(readd_outcome.links_deleted, 1, "the old link was deleted");
            check_size(readd_outcome.links_added, 1, "the relation was re-added");
            Loaded after_readd = load(readded);
            if (after_readd.ok) {
                check_size(after_readd.document.links.size(), 2,
                           "the re-added file has two live links");
                check(has_signature(after_readd.document, "1->2 FS 0"),
                      "the re-added relation reads back");
            }
        }
    }

    // ---- delete and add a different link in one save ----------------------
    Loaded mixed_base = load(two);
    if (mixed_base.ok) {
        app::StructuralEdits mixed;
        app::StructuralEdit remove;
        error_text.clear();
        const std::vector<app::PredecessorListItem> mixed_incoming =
            app::predecessors_of(mixed_base.plan, app::StructuralEdits{}, second_uid);
        if (!mixed_incoming.empty() &&
            app::make_link_delete(mixed_incoming[0], second_uid, remove, error_text)) {
            check(mixed.push(remove), "the mixed delete is queued");
        }
        app::StructuralEdit add;
        // Into task 1, whose only links are outgoing: task 3 already has the
        // untouched incoming link, and W30-0.7 refuses a second one.
        check(app::make_link_add(mixed.apply(mixed_base.plan), mixed, second_uid, first_uid,
                                 LinkType::finish_to_finish, "-1d", add, error_text),
              "the mixed add is validated against the display plan");
        check(mixed.push(add), "the mixed add is queued");
        const std::filesystem::path mixed_path =
            std::filesystem::path(APP_TEST_OUT_DIR) / "app-links-mixed.mpp";
        std::filesystem::remove(mixed_path, error);
        const app::StructuralSaveResult mixed_outcome =
            app::save_structural_edits(mixed, {}, two.string(), mixed_path.string(), false);
        check(mixed_outcome.ok, "the mixed save succeeds");
        if (mixed_outcome.ok) {
            Loaded after_mixed = load(mixed_path);
            if (after_mixed.ok) {
                check_size(after_mixed.document.links.size(), 2,
                           "the mixed file has two live links");
                check(has_signature(after_mixed.document, "1->3 SS 4800"),
                      "the untouched link survives");
                check(has_signature(after_mixed.document, "2->1 FF -4800"),
                      "the added link reads back with its negative lag");
            }
        }
    }

    // ---- refusals --------------------------------------------------------
    app::StructuralEdits bad;
    app::StructuralEdit unknown;
    unknown.kind = app::StructuralKind::add_link;
    unknown.uid = second_uid;
    unknown.predecessor_uid = 999999u;
    unknown.link_type = LinkType::finish_to_start;
    check(bad.push(unknown), "the unknown-predecessor add is queued");
    const std::filesystem::path guarded =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-links-guarded.mpp";
    std::filesystem::remove(guarded, error);
    std::filesystem::copy_file(fixture, guarded, std::filesystem::copy_options::overwrite_existing,
                               error);
    const std::size_t guarded_bytes = bytes_on_disk(guarded);
    const app::StructuralSaveResult guarded_outcome =
        app::save_structural_edits(bad, {}, copy.string(), guarded.string(), false);
    check(!guarded_outcome.ok, "a link to an unknown task is refused");
    check(!guarded_outcome.error.empty(), "the refusal explains itself");
    check_size(bytes_on_disk(guarded), guarded_bytes,
               "the pre-existing target is byte-identical after the refusal");
    check(!std::filesystem::exists(guarded.string() + ".w51-structural-tmp"),
          "the refusal removed the intermediate file");

    // Two link adds where the SECOND is invalid: the first one writes only the
    // intermediate file, so the target is never created and the intermediate is
    // removed.
    app::StructuralEdits cascade;
    app::StructuralEdit good_add;
    error_text.clear();
    check(app::make_link_add(base.plan, cascade, first_uid, third_uid,
                             LinkType::finish_to_finish, "0", good_add, error_text),
          "the good first add is built");
    check(cascade.push(good_add), "the good first add is queued");
    app::StructuralEdit bad_add = good_add;
    bad_add.predecessor_uid = 999999u;
    check(cascade.push(bad_add), "the invalid second add is queued");
    const std::filesystem::path never =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-links-never.mpp";
    std::filesystem::remove(never, error);
    const app::StructuralSaveResult cascade_outcome =
        app::save_structural_edits(cascade, {}, copy.string(), never.string(), false);
    check(!cascade_outcome.ok, "the second link refusal fails the whole save");
    check(!std::filesystem::exists(never),
          "an earlier refusal leaves no target file behind");
    check(!std::filesystem::exists(never.string() + ".w51-structural-tmp"),
          "the earlier refusal removed the intermediate file");

    const app::StructuralSaveResult same =
        app::save_structural_edits(adds, {}, copy.string(), copy.string(), false);
    check(!same.ok, "writing links over the opened file is refused");
    check_size(bytes_on_disk(copy), copy_bytes, "the refused save left the source untouched");

    check_size(bytes_on_disk(fixture), fixture_bytes,
               "the committed fixture is never written");
    check_size(bytes_on_disk(copy), copy_bytes, "the source copy is untouched by every save");
}

// ---------------------------------------------------------------------------
// Unlink: the port's save against Project's own UnlinkTasks save (W27-3.3)
// ---------------------------------------------------------------------------

// tests/data/wave23-outline.mpp has three links: 3 -> 4 FS+2 d, 3 -> 5 SS+1 d
// and 4 -> 6 FF.  Project 2021's Unlink Tasks on uid 4 -- the one task with a
// predecessor AND a successor -- removed BOTH links touching it and left the
// unrelated 3 -> 5 (measured: TaskDependencies 3 -> 1, still 1 after reopen;
// build/w27/unlink-facts.json).  The fixture pair is the same session's save
// before and after, so the link-table streams are the field-by-field oracle.
void test_real_unlink_matches_project() {
    const std::filesystem::path data(APP_TEST_DATA_DIR);
    const std::filesystem::path fixture = data / "wave23-outline.mpp";
    const std::filesystem::path project_control = data / "wave27-unlink-control.mpp";
    const std::filesystem::path project_unlinked = data / "wave27-unlink.mpp";
    if (!std::filesystem::exists(fixture) ||
        !std::filesystem::exists(project_control) ||
        !std::filesystem::exists(project_unlinked)) {
        std::printf("SKIP: the outline/unlink fixtures are not present\n");
        return;
    }
    std::error_code copy_error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR),
                                        copy_error);
    const std::filesystem::path copy =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-unlink-source.mpp";
    std::filesystem::remove(copy, copy_error);
    std::filesystem::copy_file(fixture, copy,
                               std::filesystem::copy_options::overwrite_existing,
                               copy_error);
    check(!copy_error, "the outline fixture was copied into the build tree");

    Loaded base = load(copy);
    if (!base.ok) {
        return;
    }
    check_size(base.plan.tasks.size(), 6, "the outline fixture has six tasks");
    check_size(base.plan.links.size(), 3, "the outline fixture has three links");
    check(has_signature(base.document, "3->4 FS 9600"),
          "3 -> 4 is FS with a two-day lag");
    check(has_signature(base.document, "3->5 SS 4800"),
          "3 -> 5 is SS with a one-day lag");
    check(has_signature(base.document, "4->6 FF 0"), "4 -> 6 is FF with no lag");

    // The Project control save carries the same link table as the committed
    // base, so comparing the port's save with the UNLINKED save is comparing
    // Unlink's bytes only.  Fixed2Data is deliberately excluded: Project
    // rewrites its three link GUIDs on every save (the control already differs
    // from the base), which is per-save bookkeeping, not Unlink.
    //
    // W28-0.4: Project's Unlink save also reschedules rows 3 and 4 (their
    // stored start/finish, anchor and slack move) and the port's no-CPM save
    // leaves those bytes as the base has them.  That difference is invisible
    // to Project: reopening the PORT's save with automatic calculation shows
    // the same dates, links and outline numbers as reopening Project's own
    // save -- 0 differing fields over the project row and IDs 1..6, before and
    // after an explicit CalculateAll (build/w28/W28-0.4-project-reopen.json,
    // scripts/oracle_w28_unlink.py).  Unlink therefore stays enabled with the
    // no-reschedule save, and these assertions stay scoped to the link table:
    // it is the only table Unlink is responsible for.
    const char* const link_streams[] = {"114/TBkndCons/FixedData",
                                        "114/TBkndCons/FixedMeta",
                                        "114/TBkndCons/Fixed2Meta"};
    for (const char* stream : link_streams) {
        check(stream_equal(fixture, project_control, stream),
              "the Project control's link table matches the base fixture");
    }

    const std::uint32_t selected_uid = 4;
    const std::vector<app::TaskLinkItem> touching =
        app::saved_links_touching(base.plan, app::StructuralEdits{}, selected_uid);
    check_size(touching.size(), 2,
               "uid 4 has one incoming and one outgoing saved link");
    std::size_t incoming = 0;
    std::size_t outgoing = 0;
    for (const app::TaskLinkItem& item : touching) {
        if (item.successor_uid == selected_uid) {
            ++incoming;
        }
        if (item.predecessor_uid == selected_uid) {
            ++outgoing;
        }
    }
    check_size(incoming, 1, "one saved link ends at uid 4");
    check_size(outgoing, 1, "one saved link starts at uid 4");

    app::StructuralEdits unlink;
    std::string error_text;
    for (const app::TaskLinkItem& item : touching) {
        app::StructuralEdit edit;
        check(app::make_link_delete_edit(item, edit, error_text),
              "each link touching uid 4 builds a delete edit");
        check(unlink.push(edit), "each delete edit is queued");
    }
    const std::filesystem::path port_path =
        std::filesystem::path(APP_TEST_OUT_DIR) / "app-unlink-port.mpp";
    std::filesystem::remove(port_path, copy_error);
    const app::StructuralSaveResult outcome =
        app::save_structural_edits(unlink, {}, copy.string(), port_path.string(),
                                   false);
    check(outcome.ok, "the Unlink save succeeds");
    if (!outcome.ok) {
        std::printf("SAVE ERROR: %s\n", outcome.error.c_str());
        return;
    }
    check_size(outcome.links_deleted, 2,
               "Project removes both links touching the task: two tombstones");
    check(outcome.api.find("delete_task_link") != std::string::npos,
          "the outcome names delete_task_link");

    Loaded after = load(port_path);
    if (after.ok) {
        check_size(after.document.links.size(), 1, "one live link remains");
        check(has_signature(after.document, "3->5 SS 4800"),
              "the unrelated link survives");
        check(!has_signature(after.document, "3->4 FS 9600"),
              "the incoming link is gone");
        check(!has_signature(after.document, "4->6 FF 0"),
              "the outgoing link is gone");
        check_size(after.plan.links.size(), 1, "the app plan agrees");
    }

    for (const char* stream : link_streams) {
        check(stream_equal(port_path, project_unlinked, stream),
              "the port's saved link table is byte-identical to Project's Unlink save");
    }
}

#else
void test_real_round_trip() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
}

void test_real_unlink_matches_project() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined\n");
}
#endif

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_lag_syntax();
    test_predecessor_list();
    test_validation();
    test_push_and_display();
    test_delete_prunes_adds();
    test_saved_links_touching();
    test_real_round_trip();
    test_real_unlink_matches_project();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("app_edits_links_tests: %d checks passed\n", g_checks);
    return 0;
}
