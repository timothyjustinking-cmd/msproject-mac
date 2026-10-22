// app/smoke_tests.cpp -- `app_smoke`: the portable half of the shell's
// self-smoke (`msproject_app --smoke`), proved on Windows.
//
// WHAT THIS TEST IS FOR
//   docs/handoff/w63.md explains the problem: the macOS build is the only proof
//   the shell exists, nobody on the project has a Mac, and the old "manual smoke
//   test" was never run.  app/smoke.cpp is the portable half of the replacement,
//   and this test is its assertion: on this box, with no AppKit, the smoke's
//   sequence must actually run and every check must pass.  If it does, the only
//   unproven part of a CI `--smoke` run is the AppKit half (app/main.mm), and that
//   is one file instead of a whole smoke test.
//
// WHAT IT COVERS
//   1. the report bookkeeping (steps, passed/failed, artifact dedupe, ok());
//   2. the JSON encoding, including escaping;
//   3. the failure path: a missing fixture must produce a non-zero-worthy report
//      that NAMES THE STEP, not an exception or an empty pass;
//   4. the real sequence on the committed synthetic fixture, checking the report
//      is complete (named steps, no failures), that the specific decisions ran,
//      the artifacts exist, and that tests/data/synthetic-base.mpp is untouched.
//
// WHERE IT WRITES
//   Only under the build tree: APP_SMOKE_OUT_DIR (a `test-out/smoke` directory).
//   The committed fixture is read and copied, never written.
//
// RE-RUN
//   cmake --build build\w63-app --config Release
//   ctest --test-dir build\w63-app -C Release -R "^app_smoke$" --output-on-failure

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "smoke.h"

namespace {

namespace fs = std::filesystem;

int g_checks = 0;
int g_failures = 0;

void check(bool passed, const std::string& what) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

void check_size(std::size_t got, std::size_t want, const std::string& what) {
    const bool same = got == want;
    ++g_checks;
    if (!same) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what.c_str(),
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

void check_text(const std::string& got, const std::string& want, const std::string& what) {
    const bool same = got == want;
    ++g_checks;
    if (!same) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what.c_str(), got.c_str(),
                    want.c_str());
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::size_t bytes_on_disk(const fs::path& path) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    return error ? 0 : static_cast<std::size_t>(size);
}

bool read_all(const fs::path& path, std::string& bytes) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    bytes = buffer.str();
    return true;
}

const app::SmokeCheck* find_check(const app::SmokeReport& report, const std::string& step,
                                 const std::string& name) {
    for (const app::SmokeCheck& entry : report.checks) {
        if (entry.step == step && entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

// Asserts one named check exists and passed.  Each call is one assertion in this
// test, so a renamed smoke check fails here instead of silently disappearing.
void check_named(const app::SmokeReport& report, const std::string& step,
                 const std::string& name) {
    const app::SmokeCheck* found = find_check(report, step, name);
    std::string what = "the smoke ran and passed [" + step + "] " + name;
    if (found == nullptr) {
        what += " -- the check is missing";
    } else if (!found->ok) {
        what += " -- failed: " + found->detail;
    }
    check(found != nullptr && found->ok, what);
}

// ---------------------------------------------------------------------------
// 1. Report bookkeeping
// ---------------------------------------------------------------------------

void test_report_recording() {
    app::SmokeReport empty;
    check(!empty.ok(), "a report with no checks is not a pass");
    check_size(empty.steps_run(), 0, "a fresh report has no steps");

    app::SmokeReport report;
    app::smoke_begin_step(report, "alpha");
    app::smoke_begin_step(report, "alpha");
    check_size(report.steps_run(), 1, "a step begun twice is one step");

    app::smoke_add_check(report, "alpha", "one", true);
    check_size(report.checks_passed, 1, "a passing check counts as passed");
    check_size(report.checks_failed, 0, "a passing check does not count as failed");
    check(report.ok(), "a report of passing checks is a pass");
    check_size(report.checks.size(), 1, "the check is kept");

    app::smoke_add_check(report, "beta", "two", false, "because");
    check_size(report.steps_run(), 2, "a check for a new step records the step");
    check_size(report.checks_failed, 1, "a failing check counts as failed");
    check(!report.ok(), "one failed check fails the report");
    check_text(report.checks.back().step, "beta", "the failing check names its step");
    check_text(report.checks.back().detail, "because", "the failing check keeps its detail");

    app::SmokeReport broken;
    app::smoke_add_check(broken, "alpha", "one", true);
    broken.error = "the sequence stopped";
    check(!broken.ok(), "a report with an error is not a pass");

    app::SmokeReport artifacts;
    app::smoke_add_artifact(artifacts, "input", "/a");
    app::smoke_add_artifact(artifacts, "input", "/b");
    check_size(artifacts.artifacts.size(), 1, "an artifact name is recorded once");
    check_text(artifacts.artifacts.front().path, "/a", "the first artifact path wins");

    const std::string summary = app::smoke_summary(report);
    check(contains(summary, "2 check(s)"), "the summary counts the checks");
    check(contains(summary, "beta"), "the summary names the first failing step");
}

// ---------------------------------------------------------------------------
// 2. JSON
// ---------------------------------------------------------------------------

void test_json() {
    app::SmokeReport report;
    report.fixture_path = "C:\\fixture\\base.mpp";
    report.out_dir = "/tmp/out";
    app::smoke_add_check(report, "load", "quote \" backslash \\ newline \n tab \t", true);
    app::smoke_add_check(report, "save", "a failing check", false, "detail");

    const std::string text = app::smoke_json(report);
    check(!text.empty() && text.back() == '\n', "the JSON ends with a newline");
    check(contains(text, "\"tool\": \"msproject_app --smoke\""), "the JSON names the tool");
    check(contains(text, "\"ok\": false"), "a failed report serialises ok=false");
    check(contains(text, "\"steps_run\": 2"), "the JSON carries the step count");
    check(contains(text, "\"checks_passed\": 1"), "the JSON carries the passed count");
    check(contains(text, "\"checks_failed\": 1"), "the JSON carries the failed count");
    check(contains(text, "\"step\": \"load\""), "each check names its step");
    check(contains(text, "quote \\\" backslash \\\\ newline \\n tab \\t"),
          "quotes, backslashes and control characters are escaped");
    check(contains(text, "C:\\\\fixture\\\\base.mpp"),
          "a Windows path's backslashes are escaped");
    check(!contains(text, "newline \n tab"), "no raw newline leaks into a JSON string");
    check(contains(text, "\"detail\": \"detail\""), "a failure keeps its detail");
    check(contains(text, "\"artifacts\": ["), "the JSON has an artifacts array");
}

// ---------------------------------------------------------------------------
// 3. The failure path names the step
// ---------------------------------------------------------------------------

#if defined(APP_SMOKE_OUT_DIR)

void test_missing_fixture() {
    const fs::path out_dir = fs::path(APP_SMOKE_OUT_DIR) / "missing";
    std::error_code error;
    fs::remove_all(out_dir, error);

    app::SmokeOptions options;
    options.fixture_path = (fs::path(APP_SMOKE_OUT_DIR) / "no-such-file.mpp").string();
    options.out_dir = out_dir.string();
    app::SmokeReport report = app::smoke_plan(options);

    check(!report.ok(), "a missing fixture is not a pass");
    check(!report.error.empty(), "a missing fixture sets the report error");
    check(contains(report.error, "fixture"), "the error names the fixture step");
    check(report.checks_failed >= 1, "the failure is recorded as a check");
    if (!report.checks.empty()) {
        check_text(report.checks.front().step, "fixture",
                   "the failing check names the fixture step");
        check(!report.checks.front().ok, "the fixture check failed");
    }

    std::string write_error;
    check(app::smoke_write_json(report, out_dir.string(), write_error),
          "the JSON is still written after a failed sequence: " + write_error);
    std::string text;
    check(read_all(out_dir / app::kSmokeJsonFile, text), "the failed run's JSON exists");
    check(contains(text, "\"ok\": false"), "the failed run's JSON says ok=false");
    check(contains(text, "\"checks_failed\": 1"), "the failed run's JSON counts the failure");
    check(contains(text, "\"error\": \"fixture:"), "the failed run's JSON carries the error");
}

#else

void test_missing_fixture() {
    std::printf("SKIP: APP_SMOKE_OUT_DIR is not defined\n");
}

#endif

// ---------------------------------------------------------------------------
// 4. The real sequence on the committed synthetic fixture
// ---------------------------------------------------------------------------

#if defined(APP_TEST_DATA_DIR) && defined(APP_SMOKE_OUT_DIR)

void test_real_plan() {
    const fs::path fixture = fs::path(APP_TEST_DATA_DIR) / "synthetic-base.mpp";
    if (!fs::exists(fixture)) {
        std::printf("SKIP: %s is not present; the smoke needs the committed fixture\n",
                    fixture.string().c_str());
        return;
    }
    std::string fixture_before;
    check(read_all(fixture, fixture_before), "the committed fixture can be read");
    const std::size_t fixture_size = bytes_on_disk(fixture);

    const fs::path out_dir = fs::path(APP_SMOKE_OUT_DIR) / "run";
    std::error_code error;
    fs::remove_all(out_dir, error);

    app::SmokeOptions options;
    options.fixture_path = fixture.string();
    options.out_dir = out_dir.string();
    app::SmokeReport report = app::smoke_plan(options);

    if (!report.error.empty()) {
        std::printf("SMOKE ERROR: %s\n", report.error.c_str());
    }
    for (const app::SmokeCheck& entry : report.checks) {
        if (!entry.ok) {
            std::printf("SMOKE FAIL [%s] %s: %s\n", entry.step.c_str(), entry.name.c_str(),
                        entry.detail.c_str());
        }
    }

    check(report.error.empty(), "the smoke sequence runs to the end: " + report.error);
    check_size(report.checks_failed, 0, "no smoke check failed");
    check(report.checks_passed >= 50, "the smoke ran a meaningful number of checks");
    check_size(report.checks_passed, report.checks.size(),
               "every recorded check passed (passed + failed == checks)");
    check(report.steps_run() >= 9, "the smoke ran every step");
    check(report.ok(), "the smoke report is a pass");

    // Every check names a step that the report registered, and every step ran.
    bool named = true;
    for (const app::SmokeCheck& entry : report.checks) {
        if (entry.step.empty() || entry.name.empty()) {
            named = false;
        }
        bool registered = false;
        for (const std::string& step : report.steps) {
            registered = registered || step == entry.step;
        }
        if (!registered) {
            named = false;
            std::printf("SMOKE: check [%s] %s names an unregistered step\n", entry.step.c_str(),
                        entry.name.c_str());
        }
    }
    check(named, "every check names a registered step");

    const char* const expected_steps[] = {"fixture", "load",        "filter",
                                          "task_fields", "predecessors", "start_edit",
                                          "warning", "save",        "reopen",
                                          "print"};
    for (const char* const step : expected_steps) {
        check(std::find(report.steps.begin(), report.steps.end(), std::string(step)) !=
                  report.steps.end(),
              std::string("the smoke ran the ") + step + " step");
    }

    // The decisions that matter, by name: a renamed or dropped check fails here.
    check_named(report, "load", "the fixture has three tasks");
    check_named(report, "load", "the fixture's first task is stored as manually scheduled");
    check_named(report, "filter", "the filter matches two of the three tasks");
    check_named(report, "filter", "the displayed row list is restricted to the filter");
    check_named(report, "task_fields", "the Task Fields form accepts a text value");
    check_named(report, "task_fields", "the display plan shows the queued value");
    check_named(report, "predecessors", "the dialog accepts the first -> second FS add");
    check_named(report, "predecessors", "the display plan shows the linked row");
    check_named(report, "start_edit", "the typed date is accepted");
    check_named(report, "start_edit", "exactly one start edit is pending");
    check_named(report, "warning", "an FS predecessor clamps an earlier start");
    check_named(report, "warning", "the save about to run warns once for the edited row");
    check_named(report, "save", "saving over the opened file is refused");
    check_named(report, "save", "the save wrote one row");
    check_named(report, "save", "the committed fixture is untouched by the save");
    check_named(report, "reopen", "the edited start is stored as a Start No Earlier Than "
                                  "constraint");
    check_named(report, "reopen", "the constraint date is the typed date");
    check_named(report, "print", "the printer and PDF routes produce identical pages");
    check_named(report, "print", "the page footer is the date and the page number");

    // The artifacts the CI upload relies on.
    const app::SmokeArtifact* input = nullptr;
    const app::SmokeArtifact* edited = nullptr;
    for (const app::SmokeArtifact& artifact : report.artifacts) {
        if (artifact.name == "input") {
            input = &artifact;
        }
        if (artifact.name == "edited") {
            edited = &artifact;
        }
    }
    check(input != nullptr, "the input copy is listed as an artifact");
    check(edited != nullptr, "the edited file is listed as an artifact");
    if (input != nullptr) {
        check(fs::is_regular_file(input->path), "the input copy exists: " + input->path);
        check(bytes_on_disk(input->path) == fixture_size,
              "the input copy has the fixture's size");
        std::string copy_bytes;
        check(read_all(input->path, copy_bytes) && copy_bytes == fixture_before,
              "the input copy is byte-identical to the fixture");
    }
    if (edited != nullptr) {
        check(fs::is_regular_file(edited->path), "the edited file exists: " + edited->path);
        check(bytes_on_disk(edited->path) > 0, "the edited file is not empty");
        std::string edited_bytes;
        check(read_all(edited->path, edited_bytes) && edited_bytes != fixture_before,
              "the edited file differs from the fixture");
    }

    // The committed fixture is never written, by the smoke or by this test.
    std::string fixture_after;
    check(read_all(fixture, fixture_after) && fixture_after == fixture_before,
          "tests/data/synthetic-base.mpp is byte-identical after the smoke");
    check_size(bytes_on_disk(fixture), fixture_size, "the fixture's size is unchanged");

    // The JSON the CI uploads.
    std::string write_error;
    check(app::smoke_write_json(report, out_dir.string(), write_error),
          "smoke.json is written: " + write_error);
    const fs::path json_path = out_dir / app::kSmokeJsonFile;
    check(fs::is_regular_file(json_path), "smoke.json exists");
    std::string text;
    check(read_all(json_path, text), "smoke.json can be read back");
    check(contains(text, "\"ok\": true"), "smoke.json says ok=true");
    check(contains(text, "\"checks_failed\": 0"), "smoke.json records no failures");
    check(contains(text, "\"checks_passed\": " + std::to_string(report.checks_passed)),
          "smoke.json records the passed count");
    check(contains(text, "\"steps_run\": " + std::to_string(report.steps_run())),
          "smoke.json records the step count");
    check(contains(text, "the printer and PDF routes produce identical pages"),
          "smoke.json lists the checks by name");
    check(contains(text, app::kSmokeInputFile), "smoke.json lists the input artifact");
    check(contains(text, app::kSmokeEditedFile), "smoke.json lists the edited artifact");
    bool json_listed = false;
    for (const app::SmokeArtifact& artifact : report.artifacts) {
        json_listed = json_listed || artifact.name == "json";
    }
    check(json_listed, "the JSON lists itself among the artifacts");
}

#else

void test_real_plan() {
    std::printf("SKIP: APP_TEST_DATA_DIR/APP_SMOKE_OUT_DIR are not defined\n");
}

#endif

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    test_report_recording();
    test_json();
    test_missing_fixture();
    test_real_plan();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("smoke_tests: %d checks passed\n", g_checks);
    return 0;
}
