// app_qt/tests/stored_table_test.cpp -- wave 21 (operator decision 7): a loaded
// file's own `214/CTable` column set, order, titles and widths.
//
// WHAT IT PROVES
//   A. The committed fixture's stored table IS the Entry table.  The window must
//      therefore show exactly the nine Entry columns with the stored widths
//      (39/57/147/81/81/81/105/105/123), which is what keeps the 1920x1032
//      parity render where it is.
//   B. A SYNTHETIC non-Entry table.  The fixture is copied under the build's
//      test-out directory and its first CTable block is rewritten in place --
//      field ids, UTF-16 titles and the width byte of each 115-byte column
//      record.  The record LENGTHS are untouched, so the collection framing is
//      valid; the copy is then loaded through MainWindow::open_file(), i.e. the
//      real load path.  The window must display the new order/titles/widths, the
//      sheet model must keep all of the file's columns (including the ID column
//      the frozen gutter draws), editing must still reach the model, and a
//      column the app cannot save must be read-only.
//   C. Optional third argument: a CORPUS file, read-only (git-ignored; the CI
//      runners do not have it, hence the separate ctest entry).  The corpus
//      file's stored Entry table is not the stock one ("Dur", "Pre", "Suc",
//      "SUMMARY", widths 70/9/19/21/...), so this is the real-file proof.
//
// Every write goes under the output directory passed as argv[2]: never
// tests/data/, never corpus/.
#include <QAbstractItemModel>
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTableView>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "entry_columns.h"
#include "main_window.h"
#include "shell_style.h"
#include "sheet.h"
#include "viewstate.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void check_text(const QString& got, const QString& want, const char* what, int index) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s (column %d: got \"%s\", want \"%s\")\n", what, index,
                     got.toUtf8().constData(), want.toUtf8().constData());
        ++g_failures;
    }
}

void check_width(int got, int want, int index) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: stored column %d paints %d px, want %d px\n", index, got,
                     want);
        ++g_failures;
    }
}

// --- the synthetic non-Entry table -------------------------------------------

// One column record of the table written into the fixture copy.  The field ids
// and titles are the fixture's own task fields, re-ordered and re-captioned; the
// record AFTER the ID column uses Text1, which this app cannot write back.
struct SyntheticColumn {
    std::uint32_t field = 0;
    const char* title = "";
    std::uint8_t width = 0;
};

const SyntheticColumn kSyntheticColumns[] = {
    {0x0b400017u, "", 6},              // ID -- drawn by the row-number gutter
    {0x0b40000eu, "Job", 30},          // Task Name, renamed and widened
    {0x0b400505u, "Begin", 18},        // StartText -> Start
    {0x0b400506u, "", 14},             // FinishText, no stored title
    {0x0b400507u, "Elapsed", 10},      // DurationText -> Duration
    {0x0b400033u, "Discipline", 22},   // Text1: no save path, must be read-only
    {0x0b400020u, "% Done", 8},        // % Complete: no save path
    {0x0b40002fu, "Depends On", 11},   // Predecessors
    {0x0b400031u, "Who", 16},          // Resource Names
};
constexpr std::size_t kSyntheticColumnCount =
    sizeof(kSyntheticColumns) / sizeof(kSyntheticColumns[0]);

const char* const kFixedData = "214/CTable/FixedData";
const char* const kFixedMeta = "214/CTable/FixedMeta";
const char* const kVarMeta = "214/CTable/VarMeta";
const char* const kVar2Data = "214/CTable/Var2Data";

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    if (at + 4 > bytes.size()) {
        return;
    }
    bytes[at] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[at + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[at + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

void put_title(std::vector<std::uint8_t>& bytes, std::size_t at, std::string_view title) {
    constexpr std::size_t kCapacity = 51;  // code units, NUL terminated
    if (at + kCapacity * 2 > bytes.size()) {
        return;
    }
    std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(at),
              bytes.begin() + static_cast<std::ptrdiff_t>(at + kCapacity * 2), std::uint8_t{0});
    const std::size_t count = std::min(title.size(), kCapacity - 1);
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t unit = static_cast<std::uint8_t>(title[index]);
        bytes[at + index * 2] = unit;
        bytes[at + index * 2 + 1] = 0;
    }
}

// Copies `source` to `target` and rewrites the first `214/CTable` table so it is
// no longer the Entry table: the same number of column records (the framing
// stays valid) with different field ids, titles and width bytes.  Returns true
// only when the rewritten copy parses back to exactly those columns.
bool write_synthetic_table(const QString& source, const QString& target) {
    try {
        mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(source.toStdString());
        std::array<std::vector<std::uint8_t>, 4> raw;
        const char* const names[4] = {kFixedData, kFixedMeta, kVarMeta, kVar2Data};
        for (std::size_t index = 0; index < raw.size(); ++index) {
            if (!document.has_stream(names[index])) {
                std::fprintf(stderr, "FAIL: the fixture has no %s\n", names[index]);
                return false;
            }
            raw[index] = document.read_stream(names[index]);
        }
        msproject::viewstate::PartSet parts;
        parts.fixed_data = raw[0];
        parts.fixed_meta = raw[1];
        parts.var_meta = raw[2];
        parts.var2_data = raw[3];
        msproject::viewstate::ViewState state;
        msproject::viewstate::ParseError parse_error;
        if (!msproject::viewstate::parse_view_state("CTable", parts, state, &parse_error)) {
            std::fprintf(stderr, "FAIL: the fixture's CTable does not parse: %s at %zu\n",
                         parse_error.message.c_str(), parse_error.offset);
            return false;
        }
        if (state.tables.empty() ||
            state.tables.front().columns.size() < kSyntheticColumnCount) {
            std::fprintf(stderr, "FAIL: the fixture's first table has too few columns\n");
            return false;
        }
        const auto& table = state.tables.front();
        // A Var2Data block is `u32 length` + payload; the table payload starts
        // with a 12-byte header, then one 115-byte record per column.
        const std::size_t payload = static_cast<std::size_t>(table.block_offset) + 4;
        for (std::size_t index = 0; index < kSyntheticColumnCount; ++index) {
            const std::size_t record =
                payload +
                msproject::viewstate::kTableBlockHeaderSize +
                index * msproject::viewstate::kColumnRecordSize;
            if (record + msproject::viewstate::kColumnRecordSize > state.var2_data.size()) {
                std::fprintf(stderr, "FAIL: synthetic column %zu is outside the block\n", index);
                return false;
            }
            put_u32(state.var2_data, record, kSyntheticColumns[index].field);
            state.var2_data[record + 4] = kSyntheticColumns[index].width;
            put_title(state.var2_data, record + msproject::viewstate::kColumnTitleOffset,
                      kSyntheticColumns[index].title);
        }
        msproject::viewstate::EncodedParts encoded;
        msproject::viewstate::WriteError write_error;
        if (!msproject::viewstate::encode_view_state(state, encoded, &write_error)) {
            std::fprintf(stderr, "FAIL: the rewritten CTable does not encode: %s\n",
                         write_error.message.c_str());
            return false;
        }
        document.replace_stream(kFixedData, encoded.fixed_data);
        document.replace_stream(kFixedMeta, encoded.fixed_meta);
        document.replace_stream(kVarMeta, encoded.var_meta);
        document.replace_stream(kVar2Data, encoded.var2_data);
        document.save(target.toStdString());

        // Read the copy back through the same decoder: the test must fail on a
        // fixture that does not carry what it claims to.
        const mppwrite::CfbDocument written =
            mppwrite::CfbDocument::from_file(target.toStdString());
        std::array<std::vector<std::uint8_t>, 4> reread;
        for (std::size_t index = 0; index < reread.size(); ++index) {
            reread[index] = written.read_stream(names[index]);
        }
        msproject::viewstate::PartSet check_parts;
        check_parts.fixed_data = reread[0];
        check_parts.fixed_meta = reread[1];
        check_parts.var_meta = reread[2];
        check_parts.var2_data = reread[3];
        msproject::viewstate::ViewState reparsed;
        if (!msproject::viewstate::parse_view_state("CTable", check_parts, reparsed,
                                                    &parse_error) ||
            reparsed.tables.empty()) {
            std::fprintf(stderr, "FAIL: the written fixture does not parse back\n");
            return false;
        }
        const auto& columns = reparsed.tables.front().columns;
        for (std::size_t index = 0; index < kSyntheticColumnCount; ++index) {
            if (index >= columns.size() || columns[index].field_id !=
                                               kSyntheticColumns[index].field) {
                std::fprintf(stderr, "FAIL: written column %zu did not survive the re-read\n",
                             index);
                return false;
            }
        }
        return true;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "FAIL: cannot build the synthetic table: %s\n", failure.what());
        return false;
    }
}

// --- assertions shared by the cases ------------------------------------------

struct ExpectedColumn {
    const char* title;
    int width;
};

void check_presentation(msqt::MainWindow& window, const std::vector<ExpectedColumn>& expected,
                        const char* what) {
    QTableView* sheet = window.sheet_view();
    if (sheet == nullptr || sheet->model() == nullptr) {
        check(false, "the window has a sheet model");
        return;
    }
    QAbstractItemModel* model = sheet->model();
    if (model->columnCount() != static_cast<int>(expected.size())) {
        std::fprintf(stderr, "FAIL: %s: %d display column(s), want %zu\n", what,
                     model->columnCount(), expected.size());
        ++g_failures;
        return;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        check_text(model->headerData(static_cast<int>(index), Qt::Horizontal, Qt::DisplayRole)
                       .toString(),
                   QString::fromUtf8(expected[index].title), what, static_cast<int>(index));
        check_width(sheet->columnWidth(static_cast<int>(index)), expected[index].width,
                    static_cast<int>(index));
    }
}

// The committed fixture's stored table IS the Entry table: the display must be
// the nine measured columns, so the parity screen cannot move.
void case_fixture_is_entry(const QString& fixture) {
    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    if (!window.open_file(fixture, &error)) {
        std::fprintf(stderr, "FAIL: could not open %s: %s\n", fixture.toUtf8().constData(),
                     error.toUtf8().constData());
        ++g_failures;
        return;
    }
    window.show();
    QApplication::processEvents();

    std::vector<ExpectedColumn> expected;
    for (const msqt::EntryColumn& column : msqt::entry_columns()) {
        expected.push_back(ExpectedColumn{column.title, column.width});
    }
    check_presentation(window, expected, "the fixture's Entry table");
    // The model keeps the file's own set (ID + eight scrolling columns).
    check(window.sheet() != nullptr && window.sheet()->column_count() == 9,
          "the fixture's sheet model keeps all nine stored columns");
}

void case_synthetic_non_entry(const QString& fixture, const QString& out_dir) {
    const QString target = QDir(out_dir).filePath(QStringLiteral("stored-table.mpp"));
    QFile::remove(target);
    if (!QFile::copy(fixture, target)) {
        std::fprintf(stderr, "FAIL: cannot copy the fixture to %s\n", target.toUtf8().constData());
        ++g_failures;
        return;
    }
    if (!write_synthetic_table(fixture, target)) {
        ++g_failures;
        return;
    }

    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    if (!window.open_file(target, &error)) {
        std::fprintf(stderr, "FAIL: could not open the synthetic table: %s\n",
                     error.toUtf8().constData());
        ++g_failures;
        return;
    }
    window.show();
    QApplication::processEvents();

    // Field order, titles and widths as WRITTEN, then the Add New Column stub.
    const std::vector<ExpectedColumn> expected = {
        {"Job", 30 * msqt::kColumnCharPx + msqt::kColumnPaddingPx},
        {"Begin", 18 * msqt::kColumnCharPx + msqt::kColumnPaddingPx},
        {"Finish", 14 * msqt::kColumnCharPx + msqt::kColumnPaddingPx},
        {"Elapsed", 10 * msqt::kColumnCharPx + msqt::kColumnPaddingPx},
        {"Discipline", 22 * msqt::kColumnCharPx + msqt::kColumnPaddingPx},
        {"% Done", 8 * msqt::kColumnCharPx + msqt::kColumnPaddingPx},
        {"Depends On", 11 * msqt::kColumnCharPx + msqt::kColumnPaddingPx},
        {"Who", 16 * msqt::kColumnCharPx + msqt::kColumnPaddingPx},
        {"Add New Column", 20 * msqt::kColumnCharPx + msqt::kColumnPaddingPx},
    };
    check_presentation(window, expected, "the synthetic non-Entry table");

    QTableView* sheet = window.sheet_view();
    QAbstractItemModel* model = sheet != nullptr ? sheet->model() : nullptr;
    check(window.sheet() != nullptr && window.sheet()->column_count() == 9,
          "the sheet model keeps all nine stored columns (ID included)");
    check(window.table_widths_read(), "the stored table was read from the file");
    check(window.table_column_chars(0x0b40000eu) == 30,
          "Task Name stores the synthetic 30 characters");
    if (model == nullptr || sheet == nullptr || window.sheet() == nullptr) {
        return;
    }

    // The ID column is not a display column: the frozen gutter draws it.
    const core::view::FieldId id_field = app::task_field_id("ID");
    check(window.sheet_table_model()->display_column_for_field(id_field) == -1,
          "the stored ID column is not a scrolling display column");

    // Editing still works on a file-driven column: the Name column is display 0
    // and writes the model field the Gantt/undo/save path reads.
    const QModelIndex job = model->index(0, 0);
    check(model->flags(job).testFlag(Qt::ItemIsEditable), "the renamed Task Name column is editable");
    check(model->setData(job, QStringLiteral("Renamed by the gate"), Qt::EditRole),
          "a cell edit on a file-driven column commits");
    const core::view::FieldId name_field = app::task_field_id("Name");
    const std::size_t name_model_column =
        window.sheet_table_model()->visible_column_for_field(name_field);
    check(name_model_column != core::sheet::kNoIndex, "the sheet model has the Name field");
    check(window.sheet()->cell_text(0, name_model_column) == "Renamed by the gate",
          "the edit reached the core sheet model");
    check(window.sheet()->undo(), "the edit is one undo step");
    check(window.sheet()->cell_text(0, name_model_column) != "Renamed by the gate",
          "undo restored the loaded name");

    // A Start edit is what the save diff collects: it must mark the document dirty.
    check(!window.document_dirty(), "a freshly loaded file has nothing pending");
    const QModelIndex begin = model->index(0, 1);
    check(model->flags(begin).testFlag(Qt::ItemIsEditable), "the Begin (Start) column is editable");
    check(model->setData(begin, QStringLiteral("2026-11-02 08:00"), Qt::EditRole),
          "a date edit on the file's Begin column commits");
    check(window.pending_start_edits() == 1,
          "the Start edit is the pending change the save would write");

    // A column with no save path is read-only: no fake edit.
    const QModelIndex discipline = model->index(0, 4);
    check(!model->flags(discipline).testFlag(Qt::ItemIsEditable),
          "the Discipline (Text1) column is read-only");
    check(!model->setData(discipline, QStringLiteral("nope"), Qt::EditRole),
          "the read-only column refuses an edit");

    // The task/Gantt row map is untouched by the presentation change.
    check(window.sheet_row_count() == 3, "the synthetic table keeps the three task rows");
    check(window.gantt_bar_count() >= 1, "the Gantt still draws the plan's bars");
}

// Optional: a real corpus file (read-only).  Its stored table is not Entry.
void case_corpus(const QString& corpus) {
    if (!QFileInfo::exists(corpus)) {
        std::fprintf(stderr, "note: no corpus file at %s; skipped\n", corpus.toUtf8().constData());
        return;
    }
    msqt::MainWindow window;
    window.resize(msqt::kWindowWidth, msqt::kWindowHeight);
    QString error;
    if (!window.open_file(corpus, &error)) {
        std::fprintf(stderr, "FAIL: could not open the corpus file: %s\n",
                     error.toUtf8().constData());
        ++g_failures;
        return;
    }
    window.show();
    QApplication::processEvents();
    if (window.sheet_row_count() == 0) {
        std::fprintf(stderr, "note: the corpus file loads no tasks; skipped\n");
        return;
    }
    // Measured from corpus/project_a.mpp's first CTable row: Indicators width 0,
    // Task Name 70, Dur 9, Start 19, Finish 21, % Comp 7, Pre 13, Suc 13,
    // SUMMARY 17 characters (docs/handoff/q6.md).
    const std::vector<ExpectedColumn> expected = {
        {"", 39},            // Indicators: no stored width, the stock 39 px
        {"Task Name", 423},  // 70 characters
        {"Dur", 57},         // 9
        {"Start", 117},      // 19
        {"Finish", 129},     // 21
        {"% Complete", 45},  // 7
        {"Pre", 81},         // 13
        {"Suc", 81},         // 13
        {"SUMMARY", 105},    // 17
        {"Add New Column", 123},
    };
    check_presentation(window, expected, "the corpus file's stored table");
    check(window.sheet() != nullptr && window.sheet()->column_count() == 10,
          "the corpus sheet model keeps all ten stored columns");
    // A mapped column must show the file's value: the corpus stores Duration as
    // DurationText, which has no RecordSet column of its own.
    if (window.sheet() != nullptr) {
        const std::size_t duration = window.sheet_table_model()->visible_column_for_field(
            app::task_field_id("Duration"));
        check(duration != core::sheet::kNoIndex, "the corpus sheet has a Duration column");
        check(!window.sheet()->cell_text(0, duration).empty(),
              "the corpus Duration column shows the task's duration");
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: qt_stored_table_tests <fixture.mpp> <out-dir> [corpus.mpp]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString out_dir = QString::fromLocal8Bit(argv[2]);
    const QString corpus = argc >= 4 ? QString::fromLocal8Bit(argv[3]) : QString();

    case_fixture_is_entry(fixture);
    case_synthetic_non_entry(fixture, out_dir);
    if (!corpus.isEmpty()) {
        case_corpus(corpus);
    }

    std::printf("qt_stored_table: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
