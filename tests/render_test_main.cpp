/*
 * render_test_main.cpp -- one binary, two suites.
 *
 *   render_tests: N checks passed
 *
 * Prints nothing about the corpus contents: only page counts, row counts and
 * byte sizes (see the corpus-gated test in render_report_tests.cpp).
 */

#include <cstdio>

#include "render_test.h"

int main() {
    render_test::TestContext pdf;
    pdf.suite = "render_pdf";
    const int pdf_failures = run_pdf_tests(pdf);

    render_test::TestContext report;
    report.suite = "render_report";
    const int report_failures = run_report_tests(report);

    const int checks = pdf.checks + report.checks;
    const int failures = pdf_failures + report_failures;

    std::printf("render_pdf: %d checks, %d failed\n", pdf.checks, pdf.failures);
    std::printf("render_report: %d checks, %d failed\n", report.checks, report.failures);
    if (failures != 0) {
        std::printf("render_tests: %d checks, %d FAILED\n", checks, failures);
        return 1;
    }
    std::printf("render_tests: %d checks passed\n", checks);
    return 0;
}
