// qt_task_order -- wave 23 (B-1/A-3): the loaded file's rows are Project's rows.
//
// WHAT THIS PROVES
//   * Project displays tasks in ID order, not in the file's FixedData (creation)
//     order, and draws the stored project-summary row as ID 0 ahead of them.
//     corpus/project_a.mpp is the measured case: its first stored task is ID 13.
//   * The outline levels Project shows (the clean capture
//     project-gantt-loaded-clean.png, rows 0-12) are the reader's stored chain,
//     and the sheet gets them.
//   * The frozen gutter prints the FILE's ID, not a fresh 1..n ordinal.
//
// The committed fixtures prove the rule in CI.  The corpus case pins rows 0-12
// of the clean capture: (ID, indent level, name hash).  Only the HASH of a name
// enters this file -- the names are client content and are never quoted.
//
// Usage: qt_task_order <outline.mpp> <synthetic-base.mpp> [corpus/project_a.mpp]

#include <QCoreApplication>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "layout_model.h"
#include "plan_document.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

void check_int(long long got, long long want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s (got %lld, want %lld)\n", what, got, want);
    }
}

std::uint64_t fnv1a64(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

// The row's ID cell as an integer (the gutter's text), or -1 when absent.
long long row_id(const msqt::PlanDocument& document, std::size_t row) {
    const core::view::FieldId id_field = app::task_field_id("ID");
    const std::string text = document.sheet()->field_text(row, id_field);
    if (text.empty()) {
        return -1;
    }
    try {
        return std::stoll(text);
    } catch (...) {
        return -1;
    }
}

void check_project_row(const msqt::PlanDocument& document, const char* what) {
    check(document.sheet() != nullptr, what);
    if (document.sheet() == nullptr) {
        return;
    }
    check(document.sheet()->row_count() > 0, "the sheet has rows");
    check_int(row_id(document, 0), 0, "row 0 carries the file's ID 0");
    check_int(document.sheet()->row_uid(0), 0, "row 0 is UniqueID 0");
    check(document.sheet()->is_summary(0), "row 0 is a summary row");
    check(document.sheet()->has_children(0), "row 0 has children");
    check_int(document.sheet()->outline_level(0), -1,
              "row 0 is the outline parent of the top-level tasks");
}

void case_outline(const QString& path) {
    msqt::PlanDocument document;
    std::string error;
    check(document.load(path.toStdString(), app::ZoomLevel::week, error),
          "the 3-level outline fixture loads");
    if (!error.empty()) {
        std::fprintf(stderr, "note: load error: %s\n", error.c_str());
        return;
    }
    check_int(static_cast<long long>(document.loaded().tasks.size()), 6,
              "the outline fixture has six tasks");
    check_project_row(document, "the outline fixture shows the project row");

    // ID order == row order, and the outline levels are the stored chain.
    const long long expected_ids[] = {0, 1, 2, 3, 4, 5, 6};
    const int expected_levels[] = {-1, 0, 1, 2, 2, 1, 0};
    const int expected_summary[] = {1, 1, 1, 0, 0, 0, 0};
    const long long rows = static_cast<long long>(document.sheet()->row_count());
    check_int(rows, 7, "the sheet shows the project row + six tasks");
    for (long long row = 0; row < rows && row < 7; ++row) {
        check_int(row_id(document, static_cast<std::size_t>(row)), expected_ids[row],
                  "row order is ID order");
        check_int(document.sheet()->outline_level(static_cast<std::size_t>(row)),
                  expected_levels[row], "the row's outline level");
        check_int(document.sheet()->is_summary(static_cast<std::size_t>(row)) ? 1 : 0,
                  expected_summary[row], "the row's summary flag");
    }
    const char* expected_outline[] = {"", "1", "1.1", "1.1.1", "1.1.2", "1.2", "2"};
    const core::view::FieldId outline_field = app::task_field_id("OutlineNumber");
    for (std::size_t row = 1; row < 7; ++row) {
        check_int(row_id(document, row), static_cast<long long>(row),
                  "the fixture's IDs are 1..6 in order");
        check(document.sheet()->field_text(row, outline_field) == expected_outline[row],
              "the row's OutlineNumber is the stored sibling path");
    }
}

void case_synthetic(const QString& path) {
    msqt::PlanDocument document;
    std::string error;
    check(document.load(path.toStdString(), app::ZoomLevel::week, error),
          "the committed synthetic fixture loads");
    if (!error.empty()) {
        std::fprintf(stderr, "note: load error: %s\n", error.c_str());
        return;
    }
    check_project_row(document, "the synthetic fixture shows the project row");
    check_int(static_cast<long long>(document.sheet()->row_count()), 4,
              "the synthetic sheet is the project row + three tasks");
    for (std::size_t row = 1; row < 4; ++row) {
        check_int(row_id(document, row), static_cast<long long>(row),
                  "synthetic rows are ID order");
        check_int(document.sheet()->outline_level(row), 0,
                  "the synthetic fixture is flat (level 0 under the project row)");
    }
}

// The clean capture's first thirteen rows (project row + IDs 1..12), measured
// 2026-10-23.  Only name hashes are recorded: the strings are client content.
void case_corpus(const QString& path) {
    msqt::PlanDocument document;
    std::string error;
    check(document.load(path.toStdString(), app::ZoomLevel::week, error),
          "project_a.mpp loads");
    if (!error.empty()) {
        std::fprintf(stderr, "note: load error: %s\n", error.c_str());
        return;
    }
    // ID 5 is a milestone CHILD of ID 2 ("Stage 1 ..."), not of ID 3/4: the
    // reader's stored chain (level 2 -> 0-based 1) and the capture's indent
    // agree.  The earlier 2 here was a transcription slip the gate caught.
    const int expected_levels[] = {-1, 0, 1, 2, 2, 1, 1, 2, 2, 2, 2, 2, 0};
    const std::uint64_t expected_hashes[] = {
        0x366272367ae937deull, 0x9ed96c036bddd5d0ull, 0xafd15e31e5d6fd52ull,
        0xbed77fed78047781ull, 0xb9086d11d495b957ull, 0x86cd7ec4d42dccdaull,
        0xad5a1517f0c725aaull, 0x43fd5367e9606d70ull, 0x6b9d2887528a1c2cull,
        0x6c0a03fedfa3d97dull, 0x375cdd816a5f39c2ull, 0x873b6ad6e022f6b7ull,
        0x47bae979e7c3b1ecull};
    const core::view::FieldId name_field = app::task_field_id("Name");
    check(document.sheet() != nullptr && document.sheet()->row_count() >= 13,
          "project_a.mpp has at least the capture's 13 visible rows");
    if (document.sheet() == nullptr || document.sheet()->row_count() < 13) {
        return;
    }
    for (std::size_t row = 0; row < 13; ++row) {
        check_int(row_id(document, row), static_cast<long long>(row),
                  "project_a row order is ID order");
        check_int(document.sheet()->outline_level(row), expected_levels[row],
                  "project_a outline level matches the clean capture");
        const std::string name = document.sheet()->field_text(row, name_field);
        check(fnv1a64(name) == expected_hashes[row],
              "project_a name hash matches the clean capture");
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <outline.mpp> <synthetic-base.mpp> [project_a.mpp]\n",
                     argv[0]);
        return 2;
    }
    case_outline(QString::fromLocal8Bit(argv[1]));
    case_synthetic(QString::fromLocal8Bit(argv[2]));
    if (argc >= 4) {
        case_corpus(QString::fromLocal8Bit(argv[3]));
    }
    std::printf("qt_task_order: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
