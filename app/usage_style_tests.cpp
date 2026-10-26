// app/usage_style_tests.cpp -- the usage views' stored presentation, proved
// headlessly against the committed wave-31 fixture.
//
// `app::read_usage_view_style` opens a whole .mpp, decodes `214/CV_iew` and
// reads the named usage view's two items:
//
//   0x22400008  the timescale style record (weeks over days on the fixture)
//   0x2240003c  the Details list (raw u8 selectors; Work/Actual Work/Cost are
//               valued, anything else is kept as `other`)
//
// The expected numbers are the committed oracle facts in
// tests/data/reference/wave31-usage.measures.json (stored_views): Task Usage
// stores details `00 02` whose Details column reads Work/Actual Work, Resource
// Usage stores `00 09` reading Work/Cost; both store weeks (format 13) over
// days (format 20), counts 1/1, bottom alignment 1 (centre), middle alignment
// 0 (left), start 14400 and size 100.  The named-table references are the
// file's own spellings, "Usa&ge" and "&Usage", accelerator placement included.
//
// The fixture is read-only here: nothing under tests/data is ever written.
//
// Build/run (Windows, MSVC; see app/README.md):
//   cmake -S app -B build\w31-fresh-app -G "Visual Studio 17 2022" -A x64
//   cmake --build build\w31-fresh-app --config Release
//   ctest --test-dir build\w31-fresh-app -C Release -R "^app_usage_style$"

#include <cstddef>
#include <cstdio>
#include <string>

#include "usage_style.h"
#include "view_style.h"

#ifndef APP_TEST_DATA_DIR
#error "APP_TEST_DATA_DIR must name the repository's tests/data directory"
#endif

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(),
                    want.c_str());
    }
}

using msproject::viewstate::TimescaleUnit;
using msproject::viewstate::UsageDetailKind;

void test_task_usage(const std::string& path) {
    app::UsageViewStyle style;
    std::string reason;
    check(app::read_usage_view_style(path, "Task Usage", style, &reason),
          "task usage: the fixture's Task Usage view decodes");
    check(style.decoded, "task usage: decoded flag");
    check_text(style.view_name, "Tas&k Usage", "task usage: stored view name");
    check_text(style.display_name, "Task Usage", "task usage: display name");
    // The fixture stores the Task Usage table reference as "Usa&ge" (the
    // accelerator sits before the g) and Resource Usage's as "&Usage": the
    // placement is whatever Project wrote, so the test pins the bytes.
    check_text(style.table_name, "Usa&ge",
               "task usage: the view names the stored Usage table reference");
    check_text(style.filter_name, "&All Tasks", "task usage: the view names All Tasks");

    const auto& stored = style.stored;
    check(stored.details.size() == 2,
          "task usage: the fixture stores two details");
    check(stored.details[0].stored_id == 0x00 &&
              stored.details[0].kind == UsageDetailKind::work,
          "task usage: detail 0 is Work");
    check(stored.details[1].stored_id == 0x02 &&
              stored.details[1].kind == UsageDetailKind::actual_work,
          "task usage: detail 1 is Actual Work");
    check(stored.warnings.empty(),
          "task usage: both stored details have a value rule");

    check(stored.major.unit == TimescaleUnit::weeks && stored.major.count == 1 &&
              stored.major.format == 13 && stored.major.align == 0,
          "task usage: the rendered top tier is weeks, format 13, left");
    check(stored.minor.unit == TimescaleUnit::days && stored.minor.count == 1 &&
              stored.minor.format == 20 && stored.minor.align == 1,
          "task usage: the rendered bottom tier is days, format 20, centred");
    check(stored.timescale_start == 14400,
          "task usage: the stored scroll origin is 14400");
    check(stored.timescale_size == 100, "task usage: the stored size is 100 %");
}

void test_resource_usage(const std::string& path) {
    app::UsageViewStyle style;
    std::string reason;
    check(app::read_usage_view_style(path, "Resource Usage", style, &reason),
          "resource usage: the fixture's Resource Usage view decodes");
    check(style.decoded, "resource usage: decoded flag");
    check_text(style.view_name, "Resource &Usage", "resource usage: stored view name");
    check_text(style.display_name, "Resource Usage", "resource usage: display name");
    check_text(style.table_name, "&Usage",
               "resource usage: the view names the Usage table");
    check_text(style.filter_name, "&All Resources",
               "resource usage: the view names All Resources");

    const auto& stored = style.stored;
    check(stored.details.size() == 2,
          "resource usage: the fixture stores two details");
    check(stored.details[0].kind == UsageDetailKind::work,
          "resource usage: detail 0 is Work");
    check(stored.details[1].stored_id == 0x09 &&
              stored.details[1].kind == UsageDetailKind::cost,
          "resource usage: detail 1 is Cost");
    check(stored.warnings.empty(),
          "resource usage: both stored details have a value rule");

    check(stored.major.unit == TimescaleUnit::weeks && stored.major.format == 13,
          "resource usage: the rendered top tier is weeks, format 13");
    check(stored.minor.unit == TimescaleUnit::days && stored.minor.format == 20,
          "resource usage: the rendered bottom tier is days, format 20");
}

void test_absent(const std::string& path) {
    // synthetic-base.mpp stores three views (two Gantt, one Timeline) and no
    // usage view, so both lookups fail with a reason and leave the result
    // undecoded.  A file without the views is presentation metadata only.
    app::UsageViewStyle style;
    std::string reason;
    check(!app::read_usage_view_style(path, "Task Usage", style, &reason),
          "absent: a file without Task Usage reports failure");
    check(!style.decoded && style.view_name.empty(),
          "absent: a failed read leaves the style undecoded");
    check(!reason.empty(), "absent: the failure names a reason");

    app::UsageViewStyle other;
    check(!app::read_usage_view_style(path, "Resource Usage", other, &reason),
          "absent: a file without Resource Usage reports failure");

    app::UsageViewStyle unknown;
    check(!app::read_usage_view_style(
              std::string(APP_TEST_DATA_DIR) + "/wave31-usage.mpp",
              "Task Form", unknown, &reason),
          "absent: an unknown view name reports failure");
}

}  // namespace

int main() {
    const std::string data = APP_TEST_DATA_DIR;
    const std::string fixture = data + "/wave31-usage.mpp";
    test_task_usage(fixture);
    test_resource_usage(fixture);
    test_absent(data + "/synthetic-base.mpp");

    std::printf("usage style tests: %d checks, %d failure(s)\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
