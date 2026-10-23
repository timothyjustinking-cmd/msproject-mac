// qt_task_order -- wave 23 (B-1/A-3), corrected by wave 24 (W24-2): the
// loaded file's rows are Project's rows.
//
// WHAT THIS PROVES
//   * Project displays tasks in ID order, not in the file's FixedData (creation)
//     order, and draws the stored project-summary row as ID 0 ahead of them
//     ONLY when the file stores that setting (214/Props 0x03400009 = 1).
//     corpus/project_a.mpp is the measured case: its first stored task is ID 13
//     and it stores the row ON; every committed synthetic fixture stores it OFF.
//   * The outline levels Project shows (the clean capture
//     project-gantt-loaded-clean.png, rows 0-12) are the reader's stored chain,
//     and the sheet gets them.
//   * The frozen gutter prints the FILE's ID, not a fresh 1..n ordinal.
//
// The committed fixtures prove the rule in CI.  The corpus case pins rows 0-12
// of the clean capture: (ID, indent level, name hash).  Only the HASH of a name
// enters this file -- the names are client content and are never quoted.
//
// Usage: qt_task_order <outline.mpp> <synthetic-base.mpp> <golden.sha256>
//                      [project_a.mpp]

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QStringList>

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "gantt.h"
#include "layout_model.h"
#include "plan_document.h"
#include "reader.h"

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
    // Wave 24 (W24-2): this fixture stores 214/Props 0x03400009 = 0, so Project
    // would NOT draw a project row for it.  The port follows the file.
    check_int(row_id(document, 0), 1, "the outline fixture has no project row; row 0 is ID 1");

    // ID order == row order, and the outline levels are the stored chain.
    const long long expected_ids[] = {1, 2, 3, 4, 5, 6};
    const int expected_levels[] = {0, 1, 2, 2, 1, 0};
    const int expected_summary[] = {1, 1, 0, 0, 0, 0};
    const long long rows = static_cast<long long>(document.sheet()->row_count());
    check_int(rows, 6, "the sheet shows the six tasks (no project row)");
    for (long long row = 0; row < rows && row < 6; ++row) {
        check_int(row_id(document, static_cast<std::size_t>(row)), expected_ids[row],
                  "row order is ID order");
        check_int(document.sheet()->outline_level(static_cast<std::size_t>(row)),
                  expected_levels[row], "the row's outline level");
        check_int(document.sheet()->is_summary(static_cast<std::size_t>(row)) ? 1 : 0,
                  expected_summary[row], "the row's summary flag");
    }
    const char* expected_outline[] = {"1", "1.1", "1.1.1", "1.1.2", "1.2", "2"};
    const core::view::FieldId outline_field = app::task_field_id("OutlineNumber");
    for (std::size_t row = 0; row < 6; ++row) {
        check_int(row_id(document, row), static_cast<long long>(row + 1),
                  "the fixture's IDs are 1..6 in order");
        check(document.sheet()->field_text(row, outline_field) == expected_outline[row],
              "the row's OutlineNumber is the stored sibling path");
    }
}

// Wave 23 (W23-7): the Depends On text of the oracle-built fixture, measured
// through COM (Task.Predecessors/Successors) when the fixture was built, plus
// one constructed case per stored PjFormatUnit.  The labels come from the
// file's Options > Edit records (day = full words in this fixture).
void case_link_text(const QString& path) {
    msqt::PlanDocument document;
    std::string error;
    check(document.load(path.toStdString(), app::ZoomLevel::week, error),
          "the outline fixture loads for the link-text case");
    if (!error.empty()) {
        return;
    }
    const auto text_of = [&document](std::uint32_t uid, bool incoming) {
        return incoming ? msqt::predecessors_text(document.plan(), uid, document.format())
                        : msqt::successors_text(document.plan(), uid, document.format());
    };
    const std::string t3 = text_of(3, false);
    check(t3 == "4FS+2 days,5SS+1 day", "COM successor text of task 3");
    check(text_of(4, true) == "3FS+2 days", "COM predecessor text of task 4");
    check(text_of(4, false) == "6FF", "COM successor text of task 4");
    check(text_of(5, true) == "3SS+1 day", "COM predecessor text of task 5");
    check(text_of(6, true) == "4FF", "COM predecessor text of task 6");
    // W23-7(d): the fixture carries a NAMED assignment, so the Who column is
    // not blank-vs-blank in CI.  Project's Resource Names string for task 6 is
    // the resource's name (read through COM when the fixture was built).
    check(msqt::resource_names_text(document.plan(), 6) == "Resource A",
          "the fixture's Who text is its assigned resource");
    check(msqt::resource_names_text(document.plan(), 3).empty(),
          "an unassigned task has an empty Who text");

    // Every stored lag unit Project can write, in the fixture's full-word
    // labels (2 days, 3 hrs, 1 wk, 1 mon, 50%).  Elapsed units carry Project's
    // "e" prefix; a negative lag carries "-".
    app::PlanInput plan;
    plan.title = "units";
    app::TaskRow first;
    first.uid = 1;
    first.id = 1;
    first.name = "A";
    app::TaskRow second;
    second.uid = 2;
    second.id = 2;
    second.name = "B";
    plan.tasks = {first, second};
    core::sheet::SheetFormat format;  // full words, matching the fixture
    struct UnitCase {
        std::int32_t ticks;
        std::uint16_t units;
        const char* expected;
        const char* what;
    };
    const UnitCase cases[] = {
        {1800, 5, "1FS+3 hrs", "working hours"},
        {1800, 6, "1FS+3 ehrs", "elapsed hours"},
        {9600, 7, "1FS+2 days", "working days"},
        {28800, 8, "1FS+2 edays", "elapsed days"},
        {24000, 9, "1FS+1 wk", "working weeks"},
        // COM prints "1 ewk" (singular), not "ewks", for one elapsed week.
        {100800, 10, "1FS+1 ewk", "elapsed weeks"},
        {96000, 11, "1FS+1 mon", "working months"},
        {50, 19, "1FS+50%", "percent lag"},
        {-9600, 7, "1FS-2 days", "negative lag"},
        // A finish-to-start link with no lag is the BARE id (the capture's
        // "9", "33"); the letters are dropped only for that case.
        {0, 7, "1", "a bare finish-to-start link"},
    };
    for (const UnitCase& item : cases) {
        plan.links.clear();
        app::LinkRow link;
        link.uid = 1;
        link.predecessor_uid = 1;
        link.successor_uid = 2;
        link.type = core::gantt::LinkType::finish_to_start;
        link.lag_ticks = item.ticks;
        link.lag_units = item.units;
        plan.links.push_back(link);
        const std::string got = msqt::predecessors_text(plan, 2, format);
        if (got != item.expected) {
            std::fprintf(stderr, "FAIL: %s (got %s, want %s)\n", item.what, got.c_str(),
                         item.expected);
            ++g_failures;
        }
        ++g_checks;
    }
}

// The same-file oracle for the 83-row Predecessors/Successors sweep: Project's
// own COM strings, hashed.  The file carries only hashes, so no client task
// name enters the repository.  Wave 24 (W24-8/V): ONE HASH PER ROW (ID + Pre
// hash + Suc hash), so a failure names the row instead of only the whole file.
// Provenance: tests/data/project_a_preds_golden.sha256, regenerated through COM
// by scripts/oracle_preds_golden.py.
void case_preds_golden(const QString& path, const QString& golden_path) {
    msqt::PlanDocument document;
    std::string error;
    check(document.load(path.toStdString(), app::ZoomLevel::week, error),
          "project_a.mpp loads for the predecessors golden");
    if (!error.empty()) {
        return;
    }
    const auto digest = [](const std::string& text) {
        return QString::fromLatin1(
            QCryptographicHash::hash(QByteArray::fromStdString(text),
                                     QCryptographicHash::Sha256)
                .toHex()
                .toUpper());
    };
    QFile golden(golden_path);
    if (!golden.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "FAIL: cannot open %s\n", qPrintable(golden_path));
        ++g_failures;
        ++g_checks;
        return;
    }
    struct RowHashes {
        QString predecessors;
        QString successors;
        bool seen = false;
    };
    std::map<long long, RowHashes> expected;
    for (const QByteArray& raw : golden.readAll().split('\n')) {
        const QString line = QString::fromLatin1(raw).trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const QStringList cells = line.split('\t');
        if (cells.size() != 3) {
            std::fprintf(stderr, "FAIL: golden line is not ID\\tpre\\tsuc: %s\n",
                         qPrintable(line));
            ++g_failures;
            ++g_checks;
            continue;
        }
        bool ok = false;
        const long long id = cells[0].toLongLong(&ok);
        if (!ok) {
            ++g_failures;
            ++g_checks;
            continue;
        }
        expected[id] = RowHashes{cells[1], cells[2], false};
    }
    check(expected.size() == 83, "the golden carries one line per live task (83)");
    long long matched = 0;
    for (const app::TaskRow& task : document.plan().tasks) {
        const auto found = expected.find(static_cast<long long>(task.id));
        if (found == expected.end()) {
            std::fprintf(stderr, "FAIL: golden has no row for ID %u\n", task.id);
            ++g_failures;
            ++g_checks;
            continue;
        }
        found->second.seen = true;
        const QString predecessors =
            QString::fromStdString(msqt::predecessors_text(document.plan(), task.uid,
                                                           document.format()));
        const QString successors =
            QString::fromStdString(msqt::successors_text(document.plan(), task.uid,
                                                         document.format()));
        if (digest(predecessors.toStdString()) != found->second.predecessors) {
            std::fprintf(stderr,
                         "FAIL: ID %u Predecessors differ from Project's COM text\n",
                         task.id);
            ++g_failures;
        } else if (digest(successors.toStdString()) != found->second.successors) {
            std::fprintf(stderr,
                         "FAIL: ID %u Successors differ from Project's COM text\n",
                         task.id);
            ++g_failures;
        } else {
            ++matched;
        }
        ++g_checks;
    }
    check(matched == 83, "all 83 rows' Pre/Suc text match Project's COM strings");
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
    // Wave 24 (W24-2): synthetic-base stores 0x03400009 = 0 -- Project shows
    // three task rows, no project row.  The port follows the file.
    check_int(row_id(document, 0), 1, "the synthetic fixture has no project row; row 0 is ID 1");
    check_int(static_cast<long long>(document.sheet()->row_count()), 3,
              "the synthetic sheet is three tasks (no project row)");
    for (std::size_t row = 0; row < 3; ++row) {
        check_int(row_id(document, row), static_cast<long long>(row + 1),
                  "synthetic rows are ID order");
        check_int(document.sheet()->outline_level(row), 0,
                  "the synthetic fixture is flat (level 0)");
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
    // ID 5 is a milestone CHILD of ID 2 (a summary), not of ID 3/4: the
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

// Wave 24 (W24-5/R8): the ID sort is actually EXERCISED in CI.  Both older
// committed fixtures are already stored in ID order, so deleting the adapter's
// sort left qt_task_order green.  wave24-order.mpp is Project's own save after
// a task was inserted above row 1: FixedData order is Alpha, Bravo, Charlie,
// Delta, Inserted Row, while the displayed IDs are 1..5 with Inserted Row as
// ID 2.  The first assertion reads the STORED order from the reader directly
// (independent of the adapter) and requires it to differ; the second requires
// the sheet to be in ID order.
void case_order(const QString& path) {
    const mpp::Document stored = mpp::load_file(path.toStdString());
    std::vector<long long> stored_ids;
    for (const mpp::Task& task : stored.tasks) {
        stored_ids.push_back(static_cast<long long>(task.row_id));
    }
    std::vector<long long> sorted_ids = stored_ids;
    std::sort(sorted_ids.begin(), sorted_ids.end());
    check_int(static_cast<long long>(stored_ids.size()), 5,
              "the order fixture has five tasks");
    ++g_checks;
    if (stored_ids == sorted_ids) {
        ++g_failures;
        std::fprintf(stderr,
                     "FAIL: the order fixture's STORED order is already ID order; "
                     "deleting the sort would not be caught\n");
    }

    msqt::PlanDocument document;
    std::string error;
    check(document.load(path.toStdString(), app::ZoomLevel::week, error),
          "the order fixture loads");
    if (!error.empty()) {
        return;
    }
    // The file stores the project row shown, so its sheet starts with ID 0.
    check_int(row_id(document, 0), 0, "the order fixture shows the project row");
    check_int(static_cast<long long>(document.sheet()->row_count()), 6,
              "the order fixture shows the project row + five tasks");
    const long long expected_ids[] = {0, 1, 2, 3, 4, 5};
    for (std::size_t row = 0; row < 6; ++row) {
        check_int(row_id(document, row), expected_ids[row],
                  "the sheet is in ID order although the file stores it out of order");
    }
    const core::view::FieldId name_field = app::task_field_id("Name");
    const char* expected_names[] = {"", "Alpha", "Inserted Row", "Bravo", "Charlie",
                                    "Delta"};
    for (std::size_t row = 1; row < 6; ++row) {
        check(document.sheet()->field_text(row, name_field) == expected_names[row],
              "the ID-order task is on its displayed row");
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: %s <outline.mpp> <synthetic-base.mpp> <golden.sha256> "
                     "<wave24-order.mpp> [project_a.mpp]\n",
                     argv[0]);
        return 2;
    }
    case_outline(QString::fromLocal8Bit(argv[1]));
    case_synthetic(QString::fromLocal8Bit(argv[2]));
    case_order(QString::fromLocal8Bit(argv[4]));
    case_link_text(QString::fromLocal8Bit(argv[1]));
    if (argc >= 6) {
        case_preds_golden(QString::fromLocal8Bit(argv[5]),
                          QString::fromLocal8Bit(argv[3]));
        case_corpus(QString::fromLocal8Bit(argv[5]));
    }
    std::printf("qt_task_order: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
