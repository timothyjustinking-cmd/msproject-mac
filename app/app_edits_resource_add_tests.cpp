// app/app_edits_resource_add_tests.cpp -- Add Resources through the app's edit
// model and save cascade against Project's own save pairs (wave 34, W34-4.2).
//
// The committed pairs (spec/writers/evidence/wave30-resource-add.md and
// wave34-resource-add-third.md):
//
//   wave30-resource-add-control.mpp          -> wave30-resource-add.mpp
//       one resource (n = 2 table rows): Project added the second, "R2"
//   wave34-resource-add-third-control.mpp    -> wave34-resource-add-third.mpp
//       two resources (n = 3 table rows): Project added the third, "R3"
//
// each with a committed null save of its control (the null-save mask source).
//
// This test, for each pair:
//
//   1. loads the control through the app's reader adapter and builds the edit
//      with make_add_resource_edit() using the Created stamp Project recorded;
//   2. queues it and proves the DISPLAY: one more resource row, equal field by
//      field to the row the reader reads from Project's own saved file, every
//      other resource, the tasks and the assignments untouched; and undo/redo,
//      including undo from the DISPLAYED plan, which the shells use;
//   3. saves it with the real cascade and compares every stream of the port's
//      save against Project's under the null-save discipline (save_pair::
//      compare_all, the identity dwords declared through TailNormalisation),
//      with the eleven owned streams plus Props14 DECLARED changed so a mask
//      that swallowed the change fails `effects_ok`; the pair's effect bytes
//      are counted;
//   4. reopens the port's save through the independent core/mpp reader and
//      proves the new resource is the one Project saved.
//
// It also checks every refusal the edit model owes the user: an empty / blank /
// control-character / over-long / non-ASCII name, a zero or all-ones Created
// stamp, a project with no resource beyond the placeholder row or with three
// (the unmeasured counts), a plan that disagrees with the file, a file whose
// resource or calendar table is not the measured shape, the edit's exclusivity
// (in the queue and in the cascade, with another structural edit, with another
// exclusive kind and with a start-date edit) and a save-time refusal on a file
// that changed after the edit was queued.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w34-app -G "Visual Studio 17 2022" -A x64 -DAPP_BUILD_TESTS=ON
//   cmake --build build\w34-app --config Release --target app_edits_resource_add_tests
//   ctest --test-dir build\w34-app -C Release -R "^app_edits_resource_add$" --output-on-failure

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "cfb_writer.h"
#include "edit_model.h"
#include "fixture_guard.h"
#include "layout_model.h"
// core/mppwrite/outline.h pins the core/mppwrite/table.h header (a bare
// "table.h" would find core/view/table.h; the app include path lists
// core/view first), the same rule as app_edits_resource_rename_tests.cpp.
#include "outline.h"
#include "reader.h"
#include "reader_adapter.h"
#include "save_pair_compare.h"
#include "save_pair_noise.h"

#if !defined(APP_TEST_DATA_DIR) || !defined(APP_TEST_OUT_DIR)
int main() {
    std::printf("FAIL: APP_TEST_DATA_DIR/APP_TEST_OUT_DIR are not defined; "
                "configure with APP_BUILD_TESTS=ON (app/CMakeLists.txt)\n");
    return 1;
}
#else

namespace {

using Bytes = std::vector<std::uint8_t>;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

void check_size(std::size_t got, std::size_t want, const std::string& what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what.c_str(),
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

/* The Context save_pair::compare_all reports through. */
struct Context {
    void check(bool ok, const std::string& what) const { ::check(ok, what); }
    void expected_failure(bool unexpected_pass, const std::string& what) const {
        /* These pairs have no KnownGap entry: any expected-failure report is a
         * failure of the test itself. */
        ::check(false, "unexpected KnownGap report: " + what +
                           (unexpected_pass ? " (gap gone)" : ""));
    }
};

struct Pair {
    const char* control;
    const char* null_save;
    const char* project;
    const char* label;
    const char* name;
    std::uint32_t created;       // the Created stamp Project recorded
    std::size_t resources_before;  // saved resources (table rows without row 0)
    std::uint32_t new_uid;
    std::size_t effects;         // the declared streams' effect bytes
};

// The effect totals are the declared streams' counted bytes: the six resource
// streams, four calendar streams, 114/Props and Props14 (recorded when this test
// was written; spec/writers/evidence/wave30-resource-add.md and
// wave34-resource-add-third.md list the per-stream numbers).
const Pair kSecond{"wave30-resource-add-control.mpp",
                   "wave30-resource-add-control-null.mpp",
                   "wave30-resource-add.mpp",
                   "resource-add-second",
                   "R2",
                   0x3d182a12u,
                   1,
                   2,
                   509};
const Pair kThird{"wave34-resource-add-third-control.mpp",
                  "wave34-resource-add-third-control-null.mpp",
                  "wave34-resource-add-third.mpp",
                  "resource-add-third",
                  "R3",
                  0x3d1c19a0u,
                  2,
                  3,
                  509};

const char* const kRscFixed = "114/TBkndRsc/FixedData";
const char* const kRscMeta = "114/TBkndRsc/FixedMeta";
const char* const kRscF2 = "114/TBkndRsc/Fixed2Data";
const char* const kRscF2Meta = "114/TBkndRsc/Fixed2Meta";
const char* const kRscVarMeta = "114/TBkndRsc/VarMeta";
const char* const kRscVar2 = "114/TBkndRsc/Var2Data";
const char* const kCalFixed = "114/TBkndCal/FixedData";
const char* const kCalMeta = "114/TBkndCal/FixedMeta";
const char* const kCalF2 = "114/TBkndCal/Fixed2Data";
const char* const kCalF2Meta = "114/TBkndCal/Fixed2Meta";
const char* const kProps = "114/Props";
const char* const kMarker = "Props14";

std::filesystem::path data_path(const std::string& name) {
    return std::filesystem::path(APP_TEST_DATA_DIR) / name;
}

std::filesystem::path out_path(const std::string& name) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(APP_TEST_OUT_DIR), error);
    return std::filesystem::path(APP_TEST_OUT_DIR) / name;
}

const app::ResourceRow* find_resource(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::ResourceRow& row : plan.resources) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

bool same_resource(const app::ResourceRow& left, const app::ResourceRow& right) {
    return left.uid == right.uid && left.id == right.id && left.name == right.name &&
           left.initials == right.initials && left.type == right.type &&
           left.max_units_percent == right.max_units_percent && left.group == right.group &&
           left.code == right.code && left.material_label == right.material_label &&
           left.base_calendar == right.base_calendar &&
           left.standard_rate_per_hour == right.standard_rate_per_hour &&
           left.overtime_rate_per_hour == right.overtime_rate_per_hour &&
           left.cost_per_use == right.cost_per_use && left.accrue_at == right.accrue_at;
}

bool same_assignments(const app::PlanInput& left, const app::PlanInput& right) {
    if (left.assignments.size() != right.assignments.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.assignments.size(); ++index) {
        const app::AssignmentRow& a = left.assignments[index];
        const app::AssignmentRow& b = right.assignments[index];
        if (a.uid != b.uid || a.task_uid != b.task_uid || a.resource_uid != b.resource_uid ||
            a.work_ticks != b.work_ticks || a.units_percent != b.units_percent) {
            return false;
        }
    }
    return true;
}

Bytes stream_or_empty(const mppwrite::CfbDocument& document, const std::string& path) {
    return document.has_stream(path) ? document.read_stream(path) : Bytes{};
}

/* The first dword of every identity in the appended records, which Project
 * renumbers on each save (the same declaration as tests/roundtrip_resource_
 * growth.cpp). */
std::vector<save_pair::TailNormalisation> tail_normalisation(
    const mppwrite::CfbDocument& null_save) {
    const std::size_t cal_base = null_save.read_stream(kCalF2).size();
    const std::size_t rsc_base = null_save.read_stream(kRscF2).size();
    save_pair::TailNormalisation cal;
    cal.path = kCalF2;
    cal.ranges = {{cal_base + 0, 4}, {cal_base + 16, 4}, {cal_base + 32, 4}};
    save_pair::TailNormalisation rsc;
    rsc.path = kRscF2;
    rsc.ranges = {{rsc_base + 0, 4}, {rsc_base + 24, 4}};
    return {cal, rsc};
}

/* --- the measured pairs ---------------------------------------------------- */

void check_pair(const Pair& pair) {
    const std::string label = pair.label;
    const std::filesystem::path control_path = data_path(pair.control);
    const std::filesystem::path result_path = data_path(pair.project);
    const std::filesystem::path null_path = data_path(pair.null_save);
    fixture::require_committed(control_path);
    fixture::require_committed(result_path);
    fixture::require_committed(null_path);
    check(null_path.filename().string() == save_pair::null_fixture_name(pair.control),
          label + ": the null save is the control's committed null fixture");

    app::PlanInput plan;
    app::PlanInput project_plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
        project_plan = app::plan_input_from_document(mpp::load_file(result_path.string()));
    } catch (const std::exception& error) {
        check(false, label + ": the control and Project's save load through the reader");
        std::printf("LOAD ERROR: %s\n", error.what());
        return;
    }
    check_size(plan.resources.size(), pair.resources_before, label + ": the control's resources");
    check_size(project_plan.resources.size(), pair.resources_before + 1,
               label + ": Project's save holds one more resource");
    const app::ResourceRow* project_row = find_resource(project_plan, pair.new_uid);
    check(project_row != nullptr, label + ": Project's save has the new resource uid");
    check(find_resource(plan, pair.new_uid) == nullptr,
          label + ": the control does not have the new uid");
    if (project_row == nullptr) {
        return;
    }
    check(project_row->name == pair.name, label + ": Project named the resource " + pair.name);

    /* --- make_add_resource_edit ------------------------------------------- */
    app::StructuralEdit edit;
    std::string edit_error;
    const bool built = app::make_add_resource_edit(plan, control_path.string(), pair.name,
                                                   pair.created, edit, edit_error);
    check(built, label + ": make_add_resource_edit accepts the measured shape");
    if (!built) {
        std::printf("EDIT ERROR: %s\n", edit_error.c_str());
        return;
    }
    check(edit.kind == app::StructuralKind::add_resource,
          label + ": the built edit has the add_resource kind");
    check_size(edit.uid, pair.new_uid, label + ": the edit allocates the uid Project allocated");
    check_size(edit.resource_id, project_row->id,
               label + ": the edit allocates the ID Project allocated");
    check(edit.name == pair.name, label + ": the edit carries the name");
    check_size(edit.created, pair.created, label + ": the edit carries the Created stamp");
    {
        app::StructuralEdit padded;
        std::string padded_error;
        check(app::make_add_resource_edit(plan, control_path.string(),
                                          std::string("  ") + pair.name + " \t", pair.created,
                                          padded, padded_error) &&
                  padded.name == pair.name,
              label + ": a name is trimmed before it is queued");
    }

    /* --- push + display + undo/redo --------------------------------------- */
    app::StructuralEdits edits;
    check(edits.push(edit, &plan), label + ": the add edit is queued");
    const app::PlanInput shown = edits.apply(plan);
    check_size(shown.resources.size(), plan.resources.size() + 1,
               label + ": the display shows one more resource");
    const app::ResourceRow* shown_row = find_resource(shown, pair.new_uid);
    check(shown_row != nullptr, label + ": the display shows the new resource");
    if (shown_row != nullptr) {
        /* Field by field against the row the reader reads from Project's own
         * saved file. */
        check_size(shown_row->id, project_row->id, label + ": display ID equals Project's");
        check(shown_row->name == project_row->name, label + ": display name equals Project's");
        check(shown_row->initials == project_row->initials,
              label + ": display initials equal Project's (\"" + shown_row->initials +
                  "\" vs \"" + project_row->initials + "\")");
        check(shown_row->type == project_row->type, label + ": display type equals Project's");
        check(shown_row->max_units_percent == project_row->max_units_percent,
              label + ": display Max. Units equal Project's");
        check(shown_row->standard_rate_per_hour == project_row->standard_rate_per_hour,
              label + ": display Standard rate equals Project's");
        check(shown_row->overtime_rate_per_hour == project_row->overtime_rate_per_hour,
              label + ": display Overtime rate equals Project's");
        check(shown_row->cost_per_use == project_row->cost_per_use,
              label + ": display Cost/Use equals Project's");
        check(shown_row->accrue_at == project_row->accrue_at,
              label + ": display Accrue At equals Project's (" +
                  std::to_string(shown_row->accrue_at) + " vs " +
                  std::to_string(project_row->accrue_at) + ")");
        check(shown_row->group == project_row->group && shown_row->code == project_row->code &&
                  shown_row->material_label == project_row->material_label,
              label + ": display group, code and material label equal Project's");
        check(shown_row->base_calendar == project_row->base_calendar,
              label + ": display base calendar equals Project's (\"" +
                  shown_row->base_calendar + "\" vs \"" + project_row->base_calendar + "\")");
    }
    for (const app::ResourceRow& row : plan.resources) {
        const app::ResourceRow* other = find_resource(shown, row.uid);
        check(other != nullptr && same_resource(*other, row),
              label + ": the display leaves every existing resource alone");
    }
    check(same_assignments(shown, plan), label + ": the display leaves the assignments alone");
    check_size(shown.tasks.size(), plan.tasks.size(), label + ": the display keeps the tasks");

    check(edits.undo(), label + ": the add undoes");
    check_size(edits.apply(plan).resources.size(), plan.resources.size(),
               label + ": undo removes the new resource");
    check_size(edits.apply(edits.without_structural_edits(shown, plan)).resources.size(),
               plan.resources.size(), label + ": undo from the displayed plan removes it");
    check(edits.redo(), label + ": the add redoes");
    check(find_resource(edits.apply(plan), pair.new_uid) != nullptr,
          label + ": redo shows the new resource again");
    {
        const app::PlanInput redone_from_display =
            edits.apply(edits.without_structural_edits(shown, plan));
        check_size(redone_from_display.resources.size(), plan.resources.size() + 1,
                   label + ": redo from the displayed plan keeps exactly one new resource");
    }

    /* --- save through the real cascade ------------------------------------ */
    const std::filesystem::path out = out_path(std::string("app-") + pair.label + ".mpp");
    std::error_code error;
    std::filesystem::remove(out, error);
    const app::StructuralSaveResult saved =
        app::save_structural_edits(edits, {}, control_path.string(), out.string(), false);
    check(saved.ok, label + ": the add save succeeds");
    if (!saved.ok) {
        std::printf("SAVE ERROR: %s\n", saved.error.c_str());
        return;
    }
    check_size(saved.resources_added, 1, label + ": the cascade wrote the resource");
    check(saved.api.find("add_work_resource") != std::string::npos,
          label + ": the save's api names add_work_resource");
    check(saved.warnings.empty(), label + ": the add save has no warnings");
    check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
          label + ": the save leaves no intermediate file");

    mppwrite::CfbDocument control;
    mppwrite::CfbDocument null_save;
    mppwrite::CfbDocument project;
    mppwrite::CfbDocument output;
    try {
        control = mppwrite::CfbDocument::from_file(control_path.string());
        null_save = mppwrite::CfbDocument::from_file(null_path.string());
        project = mppwrite::CfbDocument::from_file(result_path.string());
        output = mppwrite::CfbDocument::from_file(out.string());
    } catch (const std::exception& open_error) {
        check(false, label + ": the control, null save, Project pair and port save open");
        std::printf("OPEN ERROR: %s\n", open_error.what());
        return;
    }

    /* Every stream, under the null-save discipline.  The twelve owned streams
     * are declared changed by these pairs, so `effects_ok` must hold for each. */
    const std::array<const char*, 12> declared{{kRscFixed, kRscMeta, kRscF2, kRscF2Meta,
                                                kRscVarMeta, kRscVar2, kCalFixed, kCalMeta,
                                                kCalF2, kCalF2Meta, kProps, kMarker}};
    const Context context;
    const std::vector<save_pair::TailNormalisation> normalisation = tail_normalisation(null_save);
    save_pair::compare_all(context, label, control, null_save, project, output, nullptr, 0,
                           declared.data(), declared.size(), normalisation);

    std::size_t effects = 0;
    for (const char* path : declared) {
        const save_pair::Kind kind = save_pair::kind_for_path(path);
        const save_pair::Verdict verdict = save_pair::compare(
            kind, stream_or_empty(control, path), stream_or_empty(null_save, path),
            stream_or_empty(project, path), stream_or_empty(output, path), true,
            save_pair::normalisation_for(normalisation, path));
        effects += verdict.effects + verdict.tail_effects;
        check(verdict.effects_ok, label + ": " + path + ": the declared effect set is non-empty");
        check_size(verdict.mismatches, 0, label + ": " + path + ": equals Project's save");
    }
    std::printf("   %s: declared effect bytes = %llu\n", pair.label,
                static_cast<unsigned long long>(effects));
    check_size(effects, pair.effects, label + ": the pair's declared effect bytes");

    /* Every stream OUTSIDE the declared twelve is byte-identical to the control
     * (the null-save noise masked is compare_all's business; here the port's
     * save is compared to the control directly, which holds because the port
     * adds no per-save noise). */
    for (const std::string& path : control.stream_paths()) {
        if (std::find_if(declared.begin(), declared.end(), [&path](const char* owned) {
                return path == owned;
            }) != declared.end()) {
            continue;
        }
        check(stream_or_empty(output, path) == stream_or_empty(control, path),
              label + ": " + path + ": untouched by the add");
    }
    check(mppwrite::check_table(output).empty(), label + ": the saved task table passes check_table");

    /* The independent reader reads back the resource Project saved. */
    try {
        const app::PlanInput reread =
            app::plan_input_from_document(mpp::load_file(out.string()));
        check_size(reread.resources.size(), plan.resources.size() + 1,
                   label + ": the save holds one more resource");
        const app::ResourceRow* reread_row = find_resource(reread, pair.new_uid);
        check(reread_row != nullptr && same_resource(*reread_row, *project_row),
              label + ": the reader reads back the row Project saved, field by field");
        for (const app::ResourceRow& row : plan.resources) {
            const app::ResourceRow* other = find_resource(reread, row.uid);
            check(other != nullptr && same_resource(*other, row),
                  label + ": the save keeps every existing resource");
        }
        check(same_assignments(reread, plan), label + ": the save keeps every assignment");
        check_size(reread.tasks.size(), plan.tasks.size(), label + ": the save keeps the tasks");
        /* The display, the save and Project agree. */
        check(shown_row != nullptr && reread_row != nullptr && same_resource(*shown_row, *reread_row),
              label + ": the display row equals the row read back from the save");
    } catch (const std::exception& read_error) {
        check(false, label + ": the reader reopens the port's save");
        std::printf("READ ERROR: %s\n", read_error.what());
    }
}

/* --- refusals -------------------------------------------------------------- */

void expect_refused(const app::PlanInput& plan, const std::string& source,
                    const std::string& name, std::uint32_t created, const char* needle,
                    const std::string& what) {
    app::StructuralEdit edit;
    std::string error;
    const bool built = app::make_add_resource_edit(plan, source, name, created, edit, error);
    check(!built, what);
    if (built) {
        std::printf("  UNEXPECTED ACCEPT: %s\n", what.c_str());
        return;
    }
    check(error.find(needle) != std::string::npos,
          what + ": the reason names \"" + needle + "\" (got \"" + error + "\")");
    check(edit.kind == app::StructuralKind::insert_task && edit.uid == 0 && edit.name.empty() &&
              edit.resource_id == 0 && edit.created == 0,
          what + ": the refused edit is left default-constructed");
}

app::StructuralEdit hand_edit(std::uint32_t uid, std::uint32_t id, const std::string& name,
                              std::uint32_t created) {
    app::StructuralEdit hand;
    hand.kind = app::StructuralKind::add_resource;
    hand.uid = uid;
    hand.resource_id = id;
    hand.name = name;
    hand.created = created;
    return hand;
}

void check_refusals() {
    const std::filesystem::path control_path = data_path(kSecond.control);
    const std::filesystem::path third_path = data_path(kThird.control);
    const std::filesystem::path result_path = data_path(kThird.project);
    fixture::require_committed(control_path);
    fixture::require_committed(third_path);
    fixture::require_committed(result_path);
    app::PlanInput plan;
    app::PlanInput third_plan;
    app::PlanInput full_plan;
    try {
        plan = app::plan_input_from_document(mpp::load_file(control_path.string()));
        third_plan = app::plan_input_from_document(mpp::load_file(third_path.string()));
        full_plan = app::plan_input_from_document(mpp::load_file(result_path.string()));
    } catch (const std::exception&) {
        check(false, "the refusal fixtures load");
        return;
    }
    const std::string source = control_path.string();
    const std::uint32_t stamp = kSecond.created;

    /* The name rule. */
    expect_refused(plan, source, "", stamp, "empty", "an empty name");
    expect_refused(plan, source, " \t ", stamp, "empty", "a blank name");
    expect_refused(plan, source, std::string("ab\0c", 4), stamp, "control character",
                   "an embedded NUL");
    expect_refused(plan, source, "line\none", stamp, "control character",
                   "a newline in the name");
    expect_refused(plan, source, "Caf\xC3\xA9 Crew", stamp, "a name that has non-ASCII characters",
                   "a non-ASCII name");
    expect_refused(plan, source, std::string(65, 'x'), stamp, "a name longer than 64 characters",
                   "a 65-character name");
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_add_resource_edit(plan, source, std::string(64, 'x'), stamp, edit,
                                          error),
              "a 64-character name is accepted");
    }

    /* The Created stamp. */
    expect_refused(plan, source, "R2", 0, "Created", "a zero Created stamp");
    expect_refused(plan, source, "R2", 0xFFFFFFFFu, "Created", "an all-ones Created stamp");

    /* The unmeasured resource counts: the file decides (the writer's dry run),
     * so a plan that agrees with the file is still refused for the count. */
    expect_refused(full_plan, result_path.string(), "R4", stamp,
                   "resource add: the resource table has 4 rows",
                   "a project that already holds three resources");
    expect_refused(plan, result_path.string(), "R4", stamp,
                   "resource add: the resource table has 4 rows",
                   "a plan for fewer resources than the file");

    /* A plan that disagrees with the file (stale): the control holds one
     * resource, the third control two. */
    expect_refused(third_plan, source, "R2", stamp, "no longer matches",
                   "a plan that shows more resources than the file");
    expect_refused(plan, third_path.string(), "R3", stamp, "no longer matches",
                   "a plan that shows fewer resources than the file");

    /* A file that cannot be read. */
    expect_refused(plan, (std::filesystem::path(APP_TEST_OUT_DIR) / "no-such-file.mpp").string(),
                   "R2", stamp, "could not be read", "a missing project file");

    /* A file that is not the measured shape: the control with one calendar
     * FixedData stream truncated is refused by the writer's own validation, and
     * the refusal is the decision-9 text. */
    {
        mppwrite::CfbDocument broken = mppwrite::CfbDocument::from_file(source);
        Bytes props = broken.read_stream(kProps);
        broken.replace_stream(kProps, Bytes(props.begin(), props.begin() + 40));
        const std::filesystem::path broken_path = out_path("app-resource-add-broken-props.mpp");
        broken.save(broken_path.string());
        expect_refused(plan, broken_path.string(), "R2", stamp,
                       "the Props stream has no 0x02000001 scalar",
                       "a file whose 114/Props is not the measured shape");
    }

    /* The writer copies the last resource's row, and Project's Add Resources
     * gives default rates: a template that is not a default-rate work resource
     * is a shape neither pair measured. */
    {
        app::PlanInput rated = plan;
        rated.resources.back().standard_rate_per_hour = 25.0;
        expect_refused(rated, source, "R2", stamp, "default rates", "a template with a Standard rate");
        rated = plan;
        rated.resources.back().overtime_rate_per_hour = 37.5;
        expect_refused(rated, source, "R2", stamp, "default rates", "a template with an Overtime rate");
        rated = plan;
        rated.resources.back().cost_per_use = 100.0;
        expect_refused(rated, source, "R2", stamp, "default rates", "a template with a Cost/Use");
        rated = plan;
        rated.resources.back().type = core::view::ResourceType::material;
        expect_refused(rated, source, "R2", stamp, "default rates", "a material template");
        rated = plan;
        rated.resources.back().standard_rate_per_hour = 25.0;
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(kSecond.new_uid, 2, "R2", stamp), &rated),
              "push() refuses an add after a template with a rate");
    }

    /* R34G4-3: the writer clones the last resource's whole row, so the plan must
     * show a template with the measured Max. Units, accrual and no work. */
    {
        app::PlanInput odd = plan;
        odd.resources.back().max_units_percent = 50.0;
        expect_refused(odd, source, "R2", stamp, "Max. Units is not 100%",
                       "a template at 50% Max. Units");
        odd = plan;
        odd.resources.back().cost_fields_stored = true;
        odd.resources.back().accrue_at = 1;
        expect_refused(odd, source, "R2", stamp, "cost accrual is not the default",
                       "a template with start accrual");
        odd = plan;
        app::AssignmentRow held;
        held.uid = 1;
        held.task_uid = 1;
        held.resource_uid = odd.resources.back().uid;
        held.work_ticks = 24000;
        odd.assignments.push_back(held);
        expect_refused(odd, source, "R2", stamp, "holds an assignment",
                       "a template that holds an assignment");
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(kSecond.new_uid, 2, "R2", stamp), &odd),
              "push() refuses an add after a template that holds an assignment");
        odd = plan;
        odd.resources.back().max_units_percent = 50.0;
        app::StructuralEdits more;
        check(!more.push(hand_edit(kSecond.new_uid, 2, "R2", stamp), &odd),
              "push() refuses an add after a template at 50% Max. Units");
    }

    /* The reproduction itself: the last resource of wave30-assign-add.mpp holds
     * 40 h of work.  The plan read from the file shows its assignment (plan-side
     * refusal); a plan that hides it still meets the writer's dry run, which
     * compares the row with the measured template. */
    {
        const std::filesystem::path assign_path = data_path("wave30-assign-add.mpp");
        fixture::require_committed(assign_path);
        app::PlanInput assign_plan;
        try {
            assign_plan = app::plan_input_from_document(mpp::load_file(assign_path.string()));
        } catch (const std::exception&) {
            check(false, "wave30-assign-add.mpp loads");
            return;
        }
        expect_refused(assign_plan, assign_path.string(), "R9", stamp, "holds an assignment",
                       "a project whose last resource carries 40 h of work");
        app::PlanInput hidden = assign_plan;
        hidden.assignments.clear();
        expect_refused(hidden, assign_path.string(), "R9", stamp, "row offset 52",
                       "the same project with the assignment hidden from the plan");
    }

    /* push(): the plan-side backstop for a hand-built queue. */
    const std::uint32_t new_uid = kSecond.new_uid;
    const std::uint32_t new_id = 2;
    {
        app::StructuralEdits edits;
        check(!edits.push(hand_edit(new_uid, new_id, "R2", stamp), nullptr),
              "push() refuses an add with no plan");
        check(!edits.push(hand_edit(0, new_id, "R2", stamp), &plan), "push() refuses uid 0");
        check(!edits.push(hand_edit(new_uid, 0, "R2", stamp), &plan), "push() refuses ID 0");
        check(!edits.push(hand_edit(1, new_id, "R2", stamp), &plan),
              "push() refuses a uid the plan already uses");
        check(!edits.push(hand_edit(new_uid, 1, "R2", stamp), &plan),
              "push() refuses an ID the plan already uses");
        check(!edits.push(hand_edit(new_uid, new_id, "  ", stamp), &plan),
              "push() refuses a blank name");
        check(!edits.push(hand_edit(new_uid, new_id, std::string("a\0b", 3), stamp), &plan),
              "push() refuses an embedded NUL");
        check(!edits.push(hand_edit(new_uid, new_id, "Caf\xC3\xA9", stamp), &plan),
              "push() refuses a non-ASCII name");
        check(!edits.push(hand_edit(new_uid, new_id, std::string(65, 'x'), stamp), &plan),
              "push() refuses a 65-character name");
        check(!edits.push(hand_edit(new_uid, new_id, "R2", 0), &plan),
              "push() refuses a zero Created stamp");
        check(!edits.push(hand_edit(new_uid, new_id, "R2", 0xFFFFFFFFu), &plan),
              "push() refuses an all-ones Created stamp");
        check(!edits.push(hand_edit(3, 3, "R3", stamp), &full_plan),
              "push() refuses an add over three resources");
        check(edits.empty(), "no refused add left anything in the queue");
    }

    /* Exclusivity, in both directions and with the other exclusive kinds. */
    {
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        app::StructuralEdits edits;
        check(edits.push(std::move(priority), &plan), "a priority edit queues on its own");
        check(!edits.push(hand_edit(new_uid, new_id, "R2", stamp), &plan),
              "push() refuses an add over another edit");
    }
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(new_uid, new_id, "R2", stamp), &plan),
              "an add queues on an empty list");
        app::StructuralEdit priority;
        priority.kind = app::StructuralKind::set_project_priority;
        priority.priority = 500;
        check(!edits.push(std::move(priority), &plan), "push() refuses another edit over an add");
        check(!edits.push(hand_edit(new_uid, new_id, "R2 again", stamp), &plan),
              "push() refuses a second add");
        app::StructuralEdit rename;
        rename.kind = app::StructuralKind::rename_resource;
        rename.uid = 1;
        rename.name = "A longer resource name";
        check(!edits.push(std::move(rename), &plan), "push() refuses a rename over an add");
        app::StructuralEdit task_mode;
        task_mode.kind = app::StructuralKind::set_task_mode;
        task_mode.uid = 1;
        task_mode.task_manual = true;
        check(!edits.push(std::move(task_mode), &plan), "push() refuses a task-mode edit over an add");
        check_size(edits.size(), 1, "only the add stays queued");
    }

    /* The cascade refuses a start-date edit beside an add before it writes. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_add_resource_edit(plan, source, "R2", stamp, edit, error),
              "the exclusivity fixture builds its add");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the exclusivity fixture queues the add");
        const std::filesystem::path out = out_path("app-resource-add-start-edit-refused.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        app::StartEdit start;
        start.uid = 1;
        start.start = app::date32_from_packed(0x3BF212C0u);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {start}, source, out.string(), false);
        check(!saved.ok, "the save refuses an add with a start-date edit");
        check(saved.error.find("start-date") != std::string::npos,
              "the save's refusal names the start-date edit");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }

    /* W34b-1.3 (RC-R3, R34G4-8): a pending table-format or custom-field formula
     * edit reaches the same save through the document-edit hook, so the cascade
     * refuses the combination before the hook or any write; the queue is kept. */
    {
        app::StructuralEdit edit;
        std::string error;
        check(app::make_add_resource_edit(plan, source, "R2", stamp, edit, error),
              "the document-edit fixture builds its add");
        app::StructuralEdits edits;
        check(edits.push(std::move(edit), &plan), "the document-edit fixture queues its add");
        const std::filesystem::path out = out_path("app-resource-add-document-edit-refused.mpp");
        const std::size_t queued = edits.applied().size();
        int hook_calls = 0;
        for (const bool as_formula : {false, true}) {
            std::error_code fs_error;
            std::filesystem::remove(out, fs_error);
            app::StructuralDocumentEdits request;
            if (as_formula) {
                request.formula.operation = app::CustomFormulaEdit::Operation::set;
                request.formula.field_id = 0x0b400033u;
                request.formula.formula = "[Duration]/240";
            } else {
                request.view.set_date_format = true;
                request.view.date_format = 0x05;
            }
            request.hook = [&hook_calls](mppwrite::CfbDocument&, const app::ViewStateEdits&,
                                         const app::CustomFormulaEdit&) {
                ++hook_calls;
                return app::DocumentEditOutcome{};
            };
            const std::string what = std::string("its add with a pending ") +
                                     (as_formula ? "formula" : "table format") + " edit";
            const app::StructuralSaveResult saved = app::save_structural_edits(
                edits, {}, source, out.string(), false, request);
            check(!saved.ok, "the save refuses " + what);
            check(saved.error.find("an Add Resources edit cannot be combined with a table format") != std::string::npos,
                  "the save's refusal names the combination for " + what + " (got \"" +
                      saved.error + "\")");
            check(hook_calls == 0, "the hook never ran for " + what);
            check(!std::filesystem::exists(out), "the refused save writes no target for " + what);
            check(edits.applied().size() == queued, "the queue is unchanged after " + what);
        }
    }

    /* A file that changed after the edit was queued: the queued uid and ID came
     * from the control (one resource); the same edit saved against the third
     * control (two resources, so the writer would allocate uid 3) is refused
     * before anything is written. */
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(new_uid, new_id, "R2", stamp), &plan),
              "the stale-file fixture queues the add");
        const std::filesystem::path out = out_path("app-resource-add-stale.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {}, third_path.string(), out.string(), false);
        check(!saved.ok, "the save fails on a file that no longer matches the queued uid");
        check(saved.error.find("not the one the edit queued") != std::string::npos,
              "the failed save names the uid mismatch (got \"" + saved.error + "\")");
        check(!std::filesystem::exists(out), "the failed save writes no target file");
        check(!std::filesystem::exists(out.string() + ".w51-structural-tmp"),
              "the failed save leaves no intermediate file");
    }
    /* ... and a file with three resources, which no pair measures. */
    {
        app::StructuralEdits edits;
        check(edits.push(hand_edit(new_uid, new_id, "R2", stamp), &plan),
              "the three-resource fixture queues the add");
        const std::filesystem::path out = out_path("app-resource-add-three.mpp");
        std::error_code fs_error;
        std::filesystem::remove(out, fs_error);
        const app::StructuralSaveResult saved =
            app::save_structural_edits(edits, {}, result_path.string(), out.string(), false);
        check(!saved.ok, "the save fails on a file that already holds three resources");
        check(!std::filesystem::exists(out), "the refused save writes no target file");
    }
}

/* The Created stamp helper: a packed date near now. */
void check_created_stamp() {
    const std::uint32_t stamp = app::current_created_stamp();
    check(stamp != 0 && stamp != 0xFFFFFFFFu, "the Created stamp helper returns a packed date");
    check((stamp >> 16) > 15000, "the Created stamp's day is after 2024");
    check((stamp & 0xFFFFu) < 14400, "the Created stamp's ticks are within one day");
}

}  // namespace

int main() {
    check_pair(kSecond);
    check_pair(kThird);
    check_refusals();
    check_created_stamp();
    std::printf("app_edits_resource_add_tests: %d checks passed\n", g_checks - g_failures);
    if (g_failures != 0) {
        std::printf("app_edits_resource_add_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}

#endif
