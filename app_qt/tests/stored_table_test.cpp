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
// WAVE 22 adds the COLUMN VALUES:
//   D. case_computed_text_rules(): the format rules of the three columns Project
//      computes (Predecessors, Successors, Resource Names) on a CONSTRUCTED plan
//      -- the bare id for a lag-free FS link, "FS+50 d" for a lagged one (the
//         file's own unit label, wave 23 W23-7),
//      "33SS" for a type that is not FS, ", " inside Resource Names, and dead
//      endpoints / unassigned rows / unknown uids skipped.
//   E. The same text reaches the sheet: on the synthetic table's "Depends On"
//      (0x0b40002f) and "Who" (0x0b400031) columns -- that part runs in CI -- and
//      on every row of the corpus file's Pre (0x0b40002f), Suc (0x0b400030) and
//      % Complete (0x0b400020) columns, with exact strings pinned for the rows
//      the clean MS Project capture shows.
//   F. SUMMARY (Task.Flag10, 0x0b400051) is DECODED as of wave 23 (W23-6) from
//      the Task FixedMeta bit, so the corpus's SUMMARY column is asserted to
//      hold Project's Yes/No against the plan's summary flags.  Resource Names
//      on these files stays blank: their assignment rows store the null
//      resource id, asserted blank WITH that byte reason.
//
// Every write goes under the output directory passed as argv[2]: never
// tests/data/, never corpus/.
#include <QAbstractItemModel>
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QTableView>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "cfb_writer.h"
#include "entry_columns.h"
#include "gantt_canvas.h"
#include "main_window.h"
#include "reader_adapter.h"
#include "ribbon_bar.h"
#include "shell_style.h"
#include "sheet.h"
#include "viewstate.h"

namespace {

int g_failures = 0;
int g_expected_failures = 0;

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

void check_offset(int got, int want, int index) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: display column %d starts at %d px, want %d px\n", index, got,
                     want);
        ++g_failures;
    }
}

// W29-1.1: the measured character unit and the width arithmetic.  This is a
// pure rule (no window), so it runs on every platform: the table entries are
// the faces the two measured files use, which a CI runner without Segoe UI or
// Arial must still convert to the same px.
void case_column_units() {
    check(msqt::column_char_px("Arial", 8) == 5,
          "Arial 8 -> 5 px: PROJECT_A's stored column unit");
    check(msqt::column_char_px("Segoe UI", 10) == 6,
          "Segoe UI 10 -> 6 px: the committed fixture's stored column unit");
    check(msqt::column_char_px("Calibri", 11) == 7, "Calibri 11 -> 7 px (measured)");
    check(msqt::column_char_px("Carlito", 11) == 7,
          "Carlito 11 -> 7 px (the metric-compatible open face)");
    check(msqt::column_char_px("", 0) == msqt::kColumnCharPx,
          "an absent font keeps the stock 6 px unit");
    check(msqt::column_width_px(70, 5) == 353, "70 characters at 5 px + 3 = 353");
    check(msqt::column_width_px(7, 5) == 38, "7 characters at 5 px + 3 = 38");
    check(msqt::column_width_px(0, 5) == 0, "a zero stored width paints nothing");
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

// Wave 22: the computed-column checks for the synthetic table.  Declared here and
// defined after the wave-22 helpers (load_plan/shown_text), which it needs.
void check_computed_columns_synthetic(msqt::MainWindow& window, const QString& path);

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
    // and writes the model field the Gantt/undo/save path reads.  Wave 24
    // (W24-2): this fixture stores the project-summary row OFF, so row 0 is the
    // first file task.
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

    // W32-0.2 (R31-2, High): the Begin (Start) column is read-only by policy --
    // its save is not measured against Project -- so the table model refuses
    // the edit and nothing becomes a pending save change.
    check(!window.document_dirty(), "a freshly loaded file has nothing pending");
    const QModelIndex begin = model->index(0, 1);
    check(!model->flags(begin).testFlag(Qt::ItemIsEditable),
          "the Begin (Start) column is read-only");
    check(!model->setData(begin, QStringLiteral("2026-11-02 08:00"), Qt::EditRole),
          "a date edit on the file's Begin column is refused");
    check(window.pending_start_edits() == 0,
          "the refused Start edit is not a pending save change");
    check(!window.document_dirty(), "the refused Start edit leaves the document clean");

    // A column with no save path is read-only: no fake edit.
    const QModelIndex discipline = model->index(0, 4);
    check(!model->flags(discipline).testFlag(Qt::ItemIsEditable),
          "the Discipline (Text1) column is read-only");
    check(!model->setData(discipline, QStringLiteral("nope"), Qt::EditRole),
          "the read-only column refuses an edit");

    // The task/Gantt row map is untouched by the presentation change.
    check(window.sheet_row_count() == 3,
          "the synthetic table keeps its three task rows (the file hides the project row)");
    check(window.gantt_bar_count() >= 1, "the Gantt still draws the plan's bars");

    // Wave 22: this table's "Depends On" (0x0b40002f) and "Who" (0x0b400031)
    // columns are the two computed fields, on a COMMITTED fixture -- so the value
    // rendering is gated in CI (13/13) as well as on the corpus.  The checker is
    // defined after the wave-22 helpers (it needs them).
    check_computed_columns_synthetic(window, target);
}

// --- wave 22: the columns Project fills with values --------------------------

void check_std_text(const std::string& got, const std::string& want, const char* what) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s (got \"%s\", want \"%s\")\n", what, got.c_str(),
                     want.c_str());
        ++g_failures;
    }
}

// The file's plan read straight from its bytes by the SAME reader the window
// used.  This is the gate's oracle: the assertion is "the sheet shows this file's
// links and assignments", not "the sheet agrees with itself".  The two raw-word
// counters are byte-level evidence for Resource Names: an assignment row that
// stores 0 or mpxj's ASSIGNMENT_NULL_RESOURCE_ID at +8 names no resource, so the
// column has nothing to show.
struct PlanOracle {
    bool ok = false;
    app::PlanInput plan;
    std::size_t null_resource_word_rows = 0;  // raw == mpp::kNullResourceUid
    std::size_t zero_resource_word_rows = 0;  // raw == 0
    std::string error;
};

PlanOracle load_plan(const QString& path) {
    PlanOracle oracle;
    try {
        const mpp::Document document = mpp::load_file(path.toStdString());
        for (const mpp::Assignment& assignment : document.assignments) {
            if (assignment.resource_uid_raw == mpp::kNullResourceUid) {
                ++oracle.null_resource_word_rows;
            } else if (assignment.resource_uid_raw == 0) {
                ++oracle.zero_resource_word_rows;
            }
        }
        oracle.plan = app::plan_input_from_document(document);
        oracle.ok = true;
    } catch (const std::exception& failure) {
        oracle.error = failure.what();
    }
    return oracle;
}

const app::TaskRow* task_by_id(const app::PlanInput& plan, std::uint32_t id) {
    for (const app::TaskRow& row : plan.tasks) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

const app::TaskRow* task_by_uid(const app::PlanInput& plan, std::uint32_t uid) {
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

// The first `214/CTable` table of a file (read-only), for the field-id evidence
// lines and the "which column is which field id" assertions.
bool read_first_table(const QString& path, msproject::viewstate::TableDef& out) {
    try {
        const mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(path.toStdString());
        const char* const names[4] = {kFixedData, kFixedMeta, kVarMeta, kVar2Data};
        std::array<std::vector<std::uint8_t>, 4> raw;
        for (std::size_t index = 0; index < raw.size(); ++index) {
            if (!document.has_stream(names[index])) {
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
        if (!msproject::viewstate::parse_view_state("CTable", parts, state, nullptr) ||
            state.tables.empty()) {
            return false;
        }
        out = state.tables.front();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Wave 22: the FORMAT of the three computed columns, on a constructed plan.  This
// is where the syntax is pinned; the corpus case then proves the same text
// reaches the sheet from the file's own links.
//
// The shape is measured from the clean MS Project capture
// (corpus/reference/2026-10-23-msproject/project-gantt-loaded-clean.png): the
// bare id for a finish-to-start link with NO lag ("9"), "FS" plus a signed lag
// when an FS link carries one ("9FS+50 d"), a type that is not FS always printed
// ("33SS"), and a comma with NO space between entries ("18,33FS+20").
void case_computed_text_rules() {
    app::PlanInput plan;
    for (std::uint32_t index = 1; index <= 5; ++index) {
        app::TaskRow row;
        row.uid = 100 + index;  // uids do not have to equal IDs
        row.id = index;
        row.name = "Task " + std::to_string(index);
        plan.tasks.push_back(row);
    }
    auto add_link = [&plan](std::uint32_t predecessor, std::uint32_t successor,
                            core::gantt::LinkType type, std::int32_t lag_ticks) {
        app::LinkRow link;
        link.predecessor_uid = predecessor;
        link.successor_uid = successor;
        link.type = type;
        link.lag_ticks = lag_ticks;
        plan.links.push_back(link);
    };
    // In plan (file) order, which is the order Project lists them in.
    add_link(101, 103, core::gantt::LinkType::finish_to_start, 0);      // task 3 <- 1
    add_link(102, 103, core::gantt::LinkType::start_to_start, 9600);    // 2SS+2 days
    add_link(103, 104, core::gantt::LinkType::finish_to_finish, -9600);  // 4FF-2 days
    add_link(104, 103, core::gantt::LinkType::start_to_finish, 0);      // 4SF
    add_link(105, 103, core::gantt::LinkType::finish_to_start, 24000);  // 5FS+5 days
    add_link(999, 103, core::gantt::LinkType::finish_to_start, 0);      // dead: no such task
    app::ResourceRow alice;
    alice.uid = 5;
    alice.name = "Alice";
    app::ResourceRow bob;
    bob.uid = 6;
    bob.name = "Bob";
    app::ResourceRow nameless;
    nameless.uid = 7;
    plan.resources = {alice, bob, nameless};
    auto add_assignment = [&plan](std::uint32_t task_uid, std::uint32_t resource_uid) {
        app::AssignmentRow assignment;
        assignment.task_uid = task_uid;
        assignment.resource_uid = resource_uid;
        plan.assignments.push_back(assignment);
    };
    add_assignment(103, 5);   // Alice
    add_assignment(103, 6);   // Bob
    add_assignment(103, 0);   // unassigned row: names nothing
    add_assignment(103, 77);  // a resource the plan does not know: no invented name
    add_assignment(103, 7);   // a resource with no name: skipped
    add_assignment(102, 5);   // a different task's assignment

    const core::sheet::SheetFormat format;  // the shell's default duration labels
    // Two days of lag is 2 * 4800 ticks, rendered by the sheet's own formatter
    // (so the unit text follows the shell's labels, "days" by default).
    check_std_text(msqt::predecessors_text(plan, 103, format), "1,2SS+2 days,4SF,5FS+5 days",
                   "predecessors: file order, bare FS, SS+lag, SF, lagged FS, dead endpoint dropped");
    check_std_text(msqt::successors_text(plan, 103, format), "4FF-2 days",
                   "successors: FF with a negative lag");
    check_std_text(msqt::predecessors_text(plan, 104, format), "3FF-2 days",
                   "a successor's row lists the same link from the other end");
    check_std_text(msqt::successors_text(plan, 101, format), "3",
                   "a plain FS successor is the bare id");
    check_std_text(msqt::predecessors_text(plan, 101, format), "",
                   "a task with no incoming link is empty (never a placeholder)");
    check_std_text(msqt::successors_text(plan, 104, format), "3SF",
                   "a task that is a predecessor lists its successor");
    check_std_text(msqt::predecessors_text(plan, 12345, format), "",
                   "an unknown uid is empty, not a guess");
    check_std_text(msqt::resource_names_text(plan, 103), "Alice, Bob",
                   "resource names: assignment order, ', ' separated, unknown/empty/unassigned skipped");
    check_std_text(msqt::resource_names_text(plan, 102), "Alice",
                   "each row names only its own assignments");
    check_std_text(msqt::resource_names_text(plan, 101), "",
                   "a task with no assignment is empty");
    check_std_text(msqt::resource_names_text(plan, 12345), "", "an unknown uid is empty");
}

// The per-row text a display row shows for `column`.
QString shown_text(QAbstractItemModel* model, int display_row, int column) {
    if (model == nullptr || column < 0) {
        return {};
    }
    return model->index(display_row, column).data(Qt::DisplayRole).toString();
}

// The display ordinal of the row whose task uid is `uid`, or -1.
int display_row_of_uid(msqt::MainWindow& window, std::uint32_t uid) {
    msqt::SheetTableModel* model = window.sheet_table_model();
    core::sheet::SheetModel* sheet = window.sheet();
    if (model == nullptr || sheet == nullptr || window.sheet_view() == nullptr) {
        return -1;
    }
    QAbstractItemModel* table = window.sheet_view()->model();
    if (table == nullptr) {
        return -1;
    }
    for (int display = 0; display < table->rowCount(); ++display) {
        const std::size_t record = model->record_row(display);
        if (record != core::sheet::kNoIndex && sheet->row_uid(record) == uid) {
            return display;
        }
    }
    return -1;
}

// Wave 22: the synthetic table's "Depends On" (0x0b40002f) and "Who" (0x0b400031)
// columns on the COMMITTED fixture, so the derived-value rendering is gated in CI
// (13/13) and not only on this box's corpus.  The fixture's own plan is the oracle.
void check_computed_columns_synthetic(msqt::MainWindow& window, const QString& path) {
    if (window.sheet() == nullptr || window.sheet_view() == nullptr ||
        window.sheet_view()->model() == nullptr) {
        check(false, "the synthetic window has a sheet model");
        return;
    }
    QAbstractItemModel* model = window.sheet_view()->model();
    const int depends_column =
        window.sheet_table_model()->display_column_for_field(msqt::kPredecessorsField);
    const int who_column =
        window.sheet_table_model()->display_column_for_field(msqt::kResourceNamesField);
    check(depends_column == 6, "Depends On is display column 6 (0x0b40002f)");
    check(who_column == 7, "Who is display column 7 (0x0b400031)");
    if (depends_column < 0 || who_column < 0) {
        return;
    }
    const PlanOracle fixture_plan = load_plan(path);
    check(fixture_plan.ok, "the synthetic copy's plan reads back for the derived columns");
    if (!fixture_plan.ok) {
        return;
    }
    const core::sheet::SheetFormat format;
    const app::PlanInput& plan = fixture_plan.plan;
    std::printf("note: synthetic fixture plan tasks=%zu links=%zu resources=%zu assignments=%zu\n",
                plan.tasks.size(), plan.links.size(), plan.resources.size(),
                plan.assignments.size());
    int rows_with_links = 0;
    int rows_with_resources = 0;
    for (int display = 0; display < model->rowCount(); ++display) {
        const std::size_t record = window.sheet_table_model()->record_row(display);
        if (record == core::sheet::kNoIndex) {
            continue;
        }
        const std::uint32_t uid = window.sheet()->row_uid(record);
        const QString shown = shown_text(model, display, depends_column);
        check_text(shown, QString::fromStdString(msqt::predecessors_text(plan, uid, format)),
                   "Depends On shows the fixture plan's predecessors", display);
        if (!shown.isEmpty()) {
            ++rows_with_links;
        }
        // Who shows whatever the fixture's own assignment rows name, and nothing
        // when they name nothing (the derivation is the answer, not a blank cell).
        const QString shown_who = shown_text(model, display, who_column);
        check_text(shown_who,
                   QString::fromStdString(msqt::resource_names_text(plan, uid)),
                   "Who shows the fixture plan's resource names", display);
        if (!shown_who.isEmpty()) {
            ++rows_with_resources;
        }
    }
    std::printf("note: fixture columns: %d row(s) with a predecessor, %d with a resource name\n",
                rows_with_links, rows_with_resources);
    if (!plan.links.empty()) {
        check(rows_with_links > 0, "a fixture with links shows at least one predecessor value");
    }
    if (!plan.assignments.empty()) {
        // Not every assignment names a resource (a probe row may store the null id),
        // so this is a report, not a requirement; the corpus case gates the reason
        // for a blank Resource Names column.
        std::printf("note: fixture assignments=%zu (%zu null-word, %zu zero-word), rows showing "
                    "a resource name=%d\n",
                    plan.assignments.size(), fixture_plan.null_resource_word_rows,
                    fixture_plan.zero_resource_word_rows, rows_with_resources);
    }
}

// Optional: a real corpus file (read-only).  Its stored table is not Entry.
// W26-REV-4: a live Font-group combo by its accessible name (the Task tab).
QComboBox* ribbon_font_combo(msqt::RibbonBar* ribbon, const QString& name) {
    if (ribbon == nullptr) {
        return nullptr;
    }
    const int task = ribbon->tab_titles().indexOf(QStringLiteral("Task"));
    if (task < 0) {
        return nullptr;
    }
    // Do not rebuild an already-active page: a rebuild deletes the widgets, so
    // a pointer returned by an earlier call would dangle.
    if (ribbon->active_tab_index() != task) {
        ribbon->set_active_tab(task);
        QApplication::processEvents();
    }
    const QStringList groups = ribbon->group_titles();
    for (int group = 0; group < groups.size(); ++group) {
        for (int control = 0; control < ribbon->group_control_count(group); ++control) {
            auto* combo = qobject_cast<QComboBox*>(ribbon->group_control(group, control));
            if (combo != nullptr && combo->accessibleName() == name) {
                return combo;
            }
        }
    }
    return nullptr;
}

// W26-REV-5 (the independent review's gate hole): count the pixels of a
// rendered widget that are within `tolerance` of a decoded Project colour.  The
// G3 checks below use this so a paint-layer regression cannot pass on the model
// accessor alone.
int pixels_near(const QImage& image, QRgb want, int tolerance) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb rgb = image.pixel(x, y);
            if (qAbs(qRed(rgb) - qRed(want)) <= tolerance &&
                qAbs(qGreen(rgb) - qGreen(want)) <= tolerance &&
                qAbs(qBlue(rgb) - qBlue(want)) <= tolerance) {
                ++count;
            }
        }
    }
    return count;
}

void case_corpus(const QString& corpus) {
    if (!QFileInfo::exists(corpus)) {
        std::fprintf(stderr, "note: no corpus file at %s; skipped\n", corpus.toUtf8().constData());
        return;
    }
    // --- wave 22: the field-id map of the file's own table -------------------
    // Printed (not only asserted) because the ids ARE the evidence for which
    // column carries which value: the descriptor table names none of the three
    // computed fields, so the ids -- not names -- are the contract.
    msproject::viewstate::TableDef table;
    if (read_first_table(corpus, table)) {
        std::printf("note: %s first 214/CTable table \"%s\" (%zu columns):\n",
                    QFileInfo(corpus).fileName().toUtf8().constData(), table.caption.c_str(),
                    table.columns.size());
        for (const msproject::viewstate::ColumnDef& column : table.columns) {
            std::printf("note:   0x%08x  \"%s\"  %u chars\n", column.field_id,
                        column.title.c_str(), column.opaque_header[0]);
        }
    } else {
        check(false, "the corpus file's first 214/CTable table reads back");
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
    // W26-REV-4: the Font group's value displays are the FILE's default text
    // style (PROJECT_A's first FONT_BASES entry, Arial 8), not the hard-coded
    // new-project fallback.  Project's own Font box shows the same pair.
    QComboBox* font_name = ribbon_font_combo(window.ribbon(), QStringLiteral("font name"));
    QComboBox* font_size = ribbon_font_combo(window.ribbon(), QStringLiteral("font size"));
    std::printf("note: Font combo live state '%s' / '%s'\n",
                font_name != nullptr ? font_name->currentText().toUtf8().constData() : "(none)",
                font_size != nullptr ? font_size->currentText().toUtf8().constData() : "(none)");
    check(font_name != nullptr && font_name->currentText() == QStringLiteral("Arial"),
          "the Font name combo shows the file's decoded default");
    check(font_size != nullptr && font_size->currentText() == QStringLiteral("8"),
          "the Font size combo shows the file's decoded default");
    // Measured from corpus/project_a.mpp's first CTable row: Indicators width 0,
    // Task Name 70, Dur 9, Start 19, Finish 21, % Comp 7, Pre 13, Suc 13,
    // SUMMARY 17 characters.  W29-1.1: the file's default font is Arial 8 (the
    // Font box above pins it), whose measured unit is 5 px, so the eight data
    // columns paint 353/48/98/108/38/68/68/88 px -- the widths in Project's own
    // clean 1920 capture (boundaries 54/407/455/553/661/699/767/835/883 with
    // Task Name at the sheet edge).  The Indicators record stores width 0 and
    // Project draws no column for it.
    const std::vector<ExpectedColumn> expected = {
        {"Task Name", 353},  // 70 characters at 5 px + 3
        {"Dur", 48},         // 9
        {"Start", 98},       // 19
        {"Finish", 108},     // 21
        {"% Complete", 38},  // 7
        {"Pre", 68},         // 13
        {"Suc", 68},         // 13
        {"SUMMARY", 88},     // 17
        {"Add New Column", 103},  // 20 characters at the file's unit
    };
    check_presentation(window, expected, "the corpus file's stored table");
    check(window.sheet() != nullptr && window.sheet()->column_count() == 10,
          "the corpus sheet model keeps all ten stored columns");
    check(window.sheet() != nullptr && !window.sheet()->column_visible(1),
          "the stored zero-width Indicators column is hidden, not drawn as 39 px");

    // W29-1.1: the live 1920 layout, not only the model's width numbers.  A
    // fresh window at the reference client size loads with the same split
    // fraction the parity render uses; the eight data columns must start at
    // Project's measured boundaries (54/407/455/553/661/699/767/835) minus its
    // 54-px row-number gutter.  The last one starts inside the sheet pane (the
    // capture clips SUMMARY at the pane edge too, at 48 of its 88 px).
    msqt::MainWindow parity;
    parity.resize(msqt::kParityWidth, msqt::kParityHeight);
    if (!parity.open_file(corpus, &error)) {
        std::fprintf(stderr, "FAIL: could not open the corpus file at 1920: %s\n",
                     error.toUtf8().constData());
        ++g_failures;
    } else {
        parity.show();
        QApplication::processEvents();
        QTableView* sheet = parity.sheet_view();
        if (sheet == nullptr || sheet->viewport() == nullptr) {
            check(false, "the 1920 corpus window has a sheet viewport");
        } else {
            const int reference_starts[] = {0, 353, 401, 499, 607, 645, 713, 781};
            for (int index = 0; index < 8; ++index) {
                check_offset(sheet->columnViewportPosition(index), reference_starts[index],
                             index);
            }
            check(sheet->columnViewportPosition(7) < sheet->viewport()->width(),
                  "the eighth stored column starts inside the 1920 sheet pane");
            check(parity.sheet_table_model() != nullptr &&
                      parity.sheet_table_model()->columnCount() == 9,
                  "the 1920 sheet shows the eight data columns and the stub");
        }
    }
    // A mapped column must show the file's value: the corpus stores Duration as
    // DurationText, which has no RecordSet column of its own.
    if (window.sheet() != nullptr) {
        const std::size_t duration = window.sheet_table_model()->visible_column_for_field(
            app::task_field_id("Duration"));
        check(duration != core::sheet::kNoIndex, "the corpus sheet has a Duration column");
        check(!window.sheet()->cell_text(0, duration).empty(),
              "the corpus Duration column shows the task's duration");
    }

    // --- wave 22: the columns Project fills with values ----------------------
    if (window.sheet() == nullptr || window.sheet_view() == nullptr ||
        window.sheet_view()->model() == nullptr) {
        check(false, "the corpus window has a sheet model");
        return;
    }
    QAbstractItemModel* model = window.sheet_view()->model();
    msqt::SheetTableModel* table_model = window.sheet_table_model();
    const core::sheet::SheetModel& sheet = *window.sheet();
    const PlanOracle oracle = load_plan(corpus);
    if (!oracle.ok) {
        std::fprintf(stderr, "FAIL: the corpus plan does not read back: %s\n",
                     oracle.error.c_str());
        ++g_failures;
        return;
    }
    const app::PlanInput& plan = oracle.plan;
    std::printf("note: corpus plan tasks=%zu links=%zu resources=%zu assignments=%zu\n",
                plan.tasks.size(), plan.links.size(), plan.resources.size(),
                plan.assignments.size());

    const int pred_column = table_model->display_column_for_field(msqt::kPredecessorsField);
    const int succ_column = table_model->display_column_for_field(msqt::kSuccessorsField);
    const int pct_column = table_model->display_column_for_field(msqt::kPercentCompleteField);
    const int summary_column = table_model->display_column_for_field(msqt::kSummaryFlagField);
    const int resources_column = table_model->display_column_for_field(msqt::kResourceNamesField);
    check(pred_column >= 0, "the corpus table's Pre column is a display column (0x0b40002f)");
    check(succ_column >= 0, "the corpus table's Suc column is a display column (0x0b400030)");
    check(pct_column >= 0, "% Complete is a display column (0x0b400020)");
    check(summary_column >= 0, "the SUMMARY column is a display column (0x0b400051 = Flag10)");
    check(resources_column == -1,
          "the corpus table stores no Resource Names column (0x0b400031)");
    check(table_model->display_kind(pred_column) == msqt::EntryCellKind::predecessors,
          "the stored Pre column maps to the predecessors kind");
    check(table_model->display_kind(succ_column) == msqt::EntryCellKind::successors,
          "the stored Suc column maps to the successors kind");

    // Wave 26 (W26-3.1): the file's own text styles are decoded now, so these
    // are ordinary checks.  Wave 25 (W25-5/S6) had kept them as named expected
    // failures until the decode landed.  The red is MEASURED, not guessed: the
    // style record stores RGB (255,0,0) (build/w26/g3/decode_style.py on
    // corpus/project_a.mpp), and the clean capture contains 10,858 pure-red
    // pixels with zero 0xB01C1C pixels -- the old 0xB01C1C expectation was a
    // wave-23 sampling error, not Project's colour.
    const int name_display = table_model->display_column_for_field(app::name_field_id());
    check(name_display >= 0, "the corpus sheet shows a Name display column");
    if (name_display >= 0 && model->rowCount() > 3) {
        const QModelIndex milestone_cell = model->index(3, name_display);
        const QFont font = model->data(milestone_cell, Qt::FontRole).value<QFont>();
        const QColor colour =
            model->data(milestone_cell, Qt::ForegroundRole).value<QColor>();
        check(font.italic(), "project_a row 3 (a milestone) is italic as Project shows");
        check(colour == QColor(0xff, 0x00, 0x00),
              "project_a row 3 (a critical milestone) is Project's stored red 0xFF0000");
    }
    // W26-REV-5: the decoded text colours must reach the PAINTED sheet, not only
    // the model roles.  A paint-layer regression (a different brush) fails here.
    if (window.sheet_view() != nullptr && window.sheet_view()->viewport() != nullptr) {
        const QImage painted = window.sheet_view()->viewport()->grab().toImage();
        check(pixels_near(painted, qRgb(0xff, 0x00, 0x00), 60) > 20,
              "the painted sheet carries the decoded milestone red text");
        check(pixels_near(painted, qRgb(0x33, 0x66, 0xff), 60) > 20,
              "the painted sheet carries the decoded summary blue text");
    }

    // Wave 29 (W29-1.2): the view's stored per-cell formatting reaches the
    // sheet.  The capture's yellow row is ID 10 (uid 10742): every scrolling
    // cell stores (255,242,0).  The project row and ID 1 store the grey
    // (82,87,91) that the capture shows, and an automatic row (ID 2, uid
    // 10733) stores no fill at all.  These colours come from the file's
    // `0x2240000c` record, not from a constant in the shell.
    {
        const app::TaskRow* highlighted_task = task_by_id(plan, 10);
        const int highlighted_row =
            highlighted_task != nullptr ? display_row_of_uid(window, highlighted_task->uid)
                                        : -1;
        check(highlighted_row >= 0, "the Highlighted row (ID 10) is displayed");
        if (highlighted_row >= 0) {
            const std::vector<msqt::DisplayColumn>& columns = table_model->display_columns();
            int yellow_columns = 0;
            for (int column = 0; column < model->columnCount(); ++column) {
                if (static_cast<std::size_t>(column) >= columns.size() ||
                    columns[static_cast<std::size_t>(column)].kind ==
                        msqt::EntryCellKind::stub ||
                    columns[static_cast<std::size_t>(column)].field == 0) {
                    continue;
                }
                const QColor background =
                    model->data(model->index(highlighted_row, column), Qt::BackgroundRole)
                        .value<QColor>();
                if (background == QColor(255, 242, 0)) {
                    ++yellow_columns;
                } else {
                    std::fprintf(stderr,
                                 "FAIL: W29-1.2 display column %d is not yellow (got %s)\n",
                                 column, qPrintable(background.name()));
                    ++g_failures;
                }
            }
            check(yellow_columns == 8,
                  "all eight scrolling cells of the Highlighted row are yellow");

            // The painted Name cell takes the model's brush, not the delegate's
            // white sheet fill (the hierarchy paint path bypasses Qt's own
            // background drawing).
            if (window.sheet_view() != nullptr &&
                window.sheet_view()->viewport() != nullptr) {
                const QRect rect =
                    window.sheet_view()->visualRect(model->index(highlighted_row, 0));
                if (!rect.isEmpty()) {
                    const QImage image =
                        window.sheet_view()->viewport()->grab().toImage();
                    // The stored Name column is 353 px (70 characters at Arial
                    // 8's 5 px unit) and the offscreen viewport is 336 px, so
                    // the sample is clamped into the painted area; y = top + 2
                    // sits above the text, where the cell's fill is unobstructed.
                    const int sample_x = std::min(rect.right() - 5, image.width() - 3);
                    const int sample_y = rect.top() + 2;
                    const QColor sample = image.pixelColor(sample_x, sample_y);
                    std::printf("note: W29-1.2 Highlighted Name rect %d,%d %dx%d; "
                                "sample (%d,%d) = %s; viewport %dx%d\n",
                                rect.x(), rect.y(), rect.width(), rect.height(),
                                sample_x, sample_y, qPrintable(sample.name()),
                                image.width(), image.height());
                    check(sample == QColor(255, 242, 0),
                          "the painted Task Name cell of the Highlighted row is yellow");
                } else {
                    std::printf("note: W29-1.2 the Highlighted row is scrolled out; "
                                "painted Name-cell check skipped\n");
                }
            }
        }
        if (name_display >= 0) {
            const int project_row = display_row_of_uid(window, 0);
            check(project_row >= 0, "the stored project row is displayed");
            if (project_row >= 0) {
                check(model->data(model->index(project_row, name_display),
                                  Qt::BackgroundRole)
                          .value<QColor>() == QColor(82, 87, 91),
                      "the project row's Task Name keeps the stored grey");
            }
            const app::TaskRow* plain = task_by_id(plan, 2);
            const int plain_row =
                plain != nullptr ? display_row_of_uid(window, plain->uid) : -1;
            if (plain_row >= 0) {
                check(!model->data(model->index(plain_row, name_display), Qt::BackgroundRole)
                           .value<QColor>()
                           .isValid(),
                      "an automatic row stores no fill (the Text Styles rule applies)");
            }
        }
    }
    // W29-1.2: the fills must be in the PAINTED pixels as well as the model.
    if (window.sheet_view() != nullptr && window.sheet_view()->viewport() != nullptr) {
        const QImage painted = window.sheet_view()->viewport()->grab().toImage();
        const int yellow_pixels = pixels_near(painted, qRgb(255, 242, 0), 0);
        const int grey_pixels = pixels_near(painted, qRgb(82, 87, 91), 2);
        std::printf("note: W29-1.2 painted sheet: %d yellow, %d grey pixels\n",
                    yellow_pixels, grey_pixels);
        check(yellow_pixels > 200, "the painted sheet carries the stored yellow row");
        check(grey_pixels > 200, "the painted sheet carries the stored grey row fills");
    }

    // Wave 26 (W26-3.2): the Gantt bars use PROJECT_A's own 10-style palette.
    // The colours asserted are the decoded ones the clean capture shows:
    // noncritical (0,32,96), critical (255,0,0), summary (0,0,128); the
    // capture contains 0 pixels of the file's unused light-red "Critical"
    // style, so no bar may take it.
    if (window.gantt_view() != nullptr) {
        msqt::GanttCanvas* gantt = window.gantt_view();
        check(gantt->uses_file_style(), "the corpus Gantt uses the file's bar palette");
        std::set<QRgb> bar_colours;
        bool project_row_red = false;
        for (std::size_t index = 0; index < gantt->layout().bars.size(); ++index) {
            const QRgb rgb = gantt->bar_colour(index).rgb();
            bar_colours.insert(rgb);
            if (gantt->layout().bars[index].task_uid == 0) {
                project_row_red = rgb == qRgb(0xff, 0x00, 0x00);
            }
        }
        check(bar_colours.count(qRgb(0x00, 0x20, 0x60)) != 0,
              "project_a's noncritical bars are (0,32,96)");
        check(bar_colours.count(qRgb(0xff, 0x00, 0x00)) != 0,
              "project_a's critical bars are (255,0,0)");
        check(bar_colours.count(qRgb(0x00, 0x00, 0x80)) != 0,
              "project_a's summary bars are (0,0,128)");
        check(bar_colours.count(qRgb(0xf7, 0x8a, 0x88)) == 0,
              "project_a never paints the unused light-red 'Critical' bar style");
        check(project_row_red, "project_a's project-summary bar is its red summary style");

        // W26-REV-5: the palette must be in the PAINTED pixels, not only the
        // model accessor.  Render the live canvas; the visible rows carry the
        // critical, summary and progress roles at the test window size.
        const QImage chart = gantt->grab().toImage();
        check(pixels_near(chart, qRgb(0xff, 0x00, 0x00), 2) > 100,
              "the painted chart contains the critical (255,0,0) bars");
        check(pixels_near(chart, qRgb(0x00, 0x00, 0x80), 2) > 100,
              "the painted chart contains the summary (0,0,128) bars");
        check(pixels_near(chart, qRgb(0x00, 0xb0, 0x50), 2) > 100,
              "the painted chart contains the file's green progress/finished bars");
        check(pixels_near(chart, qRgb(0xf7, 0x8a, 0x88), 0) == 0,
              "the painted chart contains none of the unused light-red style");

        // Wave 26 (W26-3.3): the file stores three timescale tiers -- year
        // ("2026"), quarter ("Qtr 2, 2026") and month ("Apr") -- so the 42-px
        // header is split three ways, not the stock 2x21.
        const msqt::GanttHeaderInfo info = gantt->header_info(gantt->width());
        check(info.header_height == 42, "the Gantt header band is still 42 px");
        check(info.tier_height == 14, "project_a's three tiers share the 42 px header");
        check(info.first_major_label.find("Qtr ") != std::string::npos &&
                  info.first_major_label.find(',') != std::string::npos,
              "project_a's middle tier prints the stored quarter format");
        check(info.first_minor_label.size() == 3,
              "project_a's bottom tier prints the stored short month name");
    }

    // --- Wave 29 (W29-1.3): the stored bar labels ----------------------------
    // The applied view's ten bar styles call for four label sets: Non-Critical
    // and Critical put Name on the right, Milestone puts StartText on the left
    // and Name on the right, Summary puts Name on the left, and the
    // project-summary style puts nothing.  Style order decides a row that is
    // both summary and milestone (Milestone comes first), so a row is
    // classified by milestone, then summary, then normal.  Names are compared
    // with the row's own name and never printed; the date text is pinned by
    // SHAPE only (dd/MM/yy, no weekday) because its value is client data.
    {
        const core::gantt::GanttLayout& layout = window.gantt();
        const app::TaskRow project_row = app::project_summary_row(plan);
        int project_bars = 0;
        int summary_bars = 0;
        int milestone_bars = 0;
        int normal_bars = 0;
        int label_errors = 0;
        double sample_left_gap = 0.0;
        double sample_right_gap = 0.0;
        const auto date_shaped = [](const std::string& text) {
            if (text.size() != 8 || text[2] != '/' || text[5] != '/') {
                return false;
            }
            for (const std::size_t index : {0, 1, 3, 4, 6, 7}) {
                if (text[index] < '0' || text[index] > '9') {
                    return false;
                }
            }
            return true;
        };
        // The side's text must exist with `want`, and the rect must sit one
        // label gap (4 px) off the bar edge.
        const auto expect_side = [&](const core::gantt::Bar& bar,
                                     core::gantt::BarLabelSide side,
                                     const std::string& want) {
            const core::gantt::BarText* found = nullptr;
            for (const core::gantt::BarText& text : bar.texts) {
                if (text.side == side) {
                    found = &text;
                }
            }
            const char* side_name = side == core::gantt::BarLabelSide::left ? "left" : "right";
            if (found == nullptr || found->text != want) {
                ++label_errors;
                std::fprintf(stderr, "  label row uid %u: %s text wrong or missing\n",
                             bar.task_uid, side_name);
                return;
            }
            const double gap = side == core::gantt::BarLabelSide::left
                                   ? bar.rect.x - found->rect.right()
                                   : found->rect.x - bar.rect.right();
            if (std::fabs(gap - 4.0) > 0.01) {
                ++label_errors;
                std::fprintf(stderr, "  label row uid %u: %s gap %.2f, want 4\n",
                             bar.task_uid, side_name, gap);
                return;
            }
            if (side == core::gantt::BarLabelSide::left) {
                sample_left_gap = gap;
            } else {
                sample_right_gap = gap;
            }
        };
        const auto expect_absent = [&](const core::gantt::Bar& bar,
                                       core::gantt::BarLabelSide side) {
            for (const core::gantt::BarText& text : bar.texts) {
                if (text.side == side) {
                    ++label_errors;
                    std::fprintf(stderr, "  label row uid %u: unexpected side text\n",
                                 bar.task_uid);
                }
            }
        };
        for (const core::gantt::Bar& bar : layout.bars) {
            const app::TaskRow* row = bar.task_uid == 0 ? &project_row
                                                        : task_by_uid(plan, bar.task_uid);
            if (row == nullptr) {
                ++label_errors;
                std::fprintf(stderr, "  label bar uid %u is not a live row\n", bar.task_uid);
                continue;
            }
            // The stored styles replace the old single fallback label even when
            // all five sides are empty (the project row's bare bar).
            if (bar.label.visible) {
                ++label_errors;
                std::fprintf(stderr, "  label row uid %u: fallback label still visible\n",
                             bar.task_uid);
            }
            if (bar.task_uid == 0) {
                ++project_bars;
                if (!bar.texts.empty()) {
                    ++label_errors;
                    std::fprintf(stderr, "  project row carries %zu stored texts\n",
                                 bar.texts.size());
                }
            } else if (row->milestone) {
                ++milestone_bars;
                if (bar.texts.size() != 2) {
                    ++label_errors;
                    std::fprintf(stderr, "  milestone row uid %u has %zu texts, want 2\n",
                                 bar.task_uid, bar.texts.size());
                    continue;
                }
                const core::gantt::BarText* left = nullptr;
                for (const core::gantt::BarText& text : bar.texts) {
                    if (text.side == core::gantt::BarLabelSide::left) {
                        left = &text;
                    }
                }
                if (left == nullptr || !date_shaped(left->text)) {
                    ++label_errors;
                    std::fprintf(stderr,
                                 "  milestone row uid %u: left text is not dd/MM/yy\n",
                                 bar.task_uid);
                }
                expect_side(bar, core::gantt::BarLabelSide::right, row->name);
                expect_absent(bar, core::gantt::BarLabelSide::top);
                expect_absent(bar, core::gantt::BarLabelSide::bottom);
                expect_absent(bar, core::gantt::BarLabelSide::inside);
            } else if (row->summary) {
                ++summary_bars;
                if (bar.texts.size() != 1) {
                    ++label_errors;
                    std::fprintf(stderr, "  summary row uid %u has %zu texts, want 1\n",
                                 bar.task_uid, bar.texts.size());
                    continue;
                }
                expect_side(bar, core::gantt::BarLabelSide::left, row->name);
                expect_absent(bar, core::gantt::BarLabelSide::right);
            } else {
                ++normal_bars;
                if (bar.texts.size() != 1) {
                    ++label_errors;
                    std::fprintf(stderr, "  normal row uid %u has %zu texts, want 1\n",
                                 bar.task_uid, bar.texts.size());
                    continue;
                }
                expect_side(bar, core::gantt::BarLabelSide::right, row->name);
                expect_absent(bar, core::gantt::BarLabelSide::left);
            }
        }
        check(label_errors == 0,
              "every stored bar label is on its stored side with its stored field");
        check(project_bars == 1, "the project-summary bar carries no label");
        check(milestone_bars > 0 && summary_bars > 0 && normal_bars > 0,
              "the file's milestones, summaries and normal task bars are all checked");
        check(project_bars + milestone_bars + summary_bars + normal_bars ==
                  static_cast<int>(layout.bars.size()),
              "every drawn corpus bar is covered by the label classification");
        std::printf("note: W29-1.3 stored bar labels: %d normal (right), %d summary "
                    "(left), %d milestone (left date + right), %d project (none); "
                    "gaps left %.0f right %.0f\n",
                    normal_bars, summary_bars, milestone_bars, project_bars,
                    sample_left_gap, sample_right_gap);
    }

    // EVERY displayed row: % Complete is the task's stored percent, and Pre/Suc
    // are the plan's link text (the format itself is pinned by
    // case_computed_text_rules; this proves the file's links reach the sheet).
    // Wave 23 (W23-7): the expectation uses the DOCUMENT's format, because this
    // file stores the short day label ("d") and the port must print it.
    const core::sheet::SheetFormat& format = sheet.format();
    int rows_checked = 0;
    int rows_with_links = 0;
    int blank_predecessors = 0;
    for (int display = 0; display < model->rowCount(); ++display) {
        const std::size_t record = table_model->record_row(display);
        if (record == core::sheet::kNoIndex) {
            continue;
        }
        const std::uint32_t uid = sheet.row_uid(record);
        if (uid == 0) {
            // Wave 23 (B-1): the synthetic project-summary row, ID 0, always
            // first and never a live task of the file.
            check(display == 0, "the project summary row is the first displayed row");
            continue;
        }
        const app::TaskRow* task = task_by_uid(plan, uid);
        check(task != nullptr, "every displayed task row is a live task of the file");
        if (task == nullptr) {
            continue;
        }
        ++rows_checked;
        // Project prints the percent sign (the capture reads "57%", "100%", "0%");
        // PlanDocument::stored_columns() declares the column CellKind::Percent.
        check_text(shown_text(model, display, pct_column),
                   QString::number(static_cast<unsigned>(task->percent_complete)) +
                       QStringLiteral("%"),
                   "% Complete shows the task's stored percent", display);
        const QString shown_predecessors = shown_text(model, display, pred_column);
        const QString shown_successors = shown_text(model, display, succ_column);
        check_text(shown_predecessors,
                   QString::fromStdString(msqt::predecessors_text(plan, uid, format)),
                   "Pre shows the plan's predecessor list", display);
        check_text(shown_successors,
                   QString::fromStdString(msqt::successors_text(plan, uid, format)),
                   "Suc shows the plan's successor list", display);
        if (shown_predecessors.isEmpty()) {
            ++blank_predecessors;
        }
        if (!shown_predecessors.isEmpty() || !shown_successors.isEmpty()) {
            ++rows_with_links;
        }
    }
    check(rows_checked == static_cast<int>(plan.tasks.size()),
          "the gate checks every task of the file (83 rows)");
    check(rows_with_links >= 60, "most tasks show a predecessor or a successor");
    check(blank_predecessors > 0, "a task with no incoming link keeps an empty Pre cell");
    std::printf("note: %d rows checked, %d with a predecessor/successor, %d blank Pre\n",
                rows_checked, rows_with_links, blank_predecessors);

    // Exact strings, cross-checked against the clean MS Project capture
    // (corpus/reference/2026-10-23-msproject/project-gantt-loaded-clean.png).  The
    // number is the file's own task ID (the column Project prints):
    //   3 -> 4, 4 <- 3 -> 5, 5 <- 4;
    //   9 <- 8 -> 10FS+50 d, 10 <- 9FS+50 d (240000 ticks = 50 days) -> 11;
    //   17 -> 18,33FS+20 d,56,57,25 (the capture's 13-char column shows only
    //     "18,33FS+20 d": Project CLIPS there, so the frame is the pin, not the PNG);
    //   33 <- 17FS+20 days -> 42SS,34; 34 <- 33 -> 39;
    //   35 <- 46 -> 40,36 and 36 <- 52,35 -> 39,37 and 37 <- 36 -> 38: the capture
    //     shows those two multi-entry lists in exactly this order, which is the
    //     FILE's link order and NOT ascending ID -- the strongest evidence that the
    //     list order is the stored one;
    //   42 <- 33SS, 57.
    struct PinnedText {
        std::uint32_t id;
        const char* predecessors;
        const char* successors;
    };
    const PinnedText kPinned[] = {
        {3, "", "4"},
        {4, "3", "5"},
        {5, "4", ""},
        // Wave 23 (W23-7a): these are Project's own COM strings for this file.
        // The four lagged rows print "d" (the file's day label is 0), not
        // "days"; the wave-22 pins had encoded the port's own label rule.
        {9, "8", "10FS+50 d"},
        {10, "9FS+50 d", "11"},
        {17, "15", "18,33FS+20 d,56,57,25"},
        {33, "17FS+20 d", "42SS,34"},
        {34, "33", "39"},
        {35, "46", "40,36"},
        {36, "52,35", "39,37"},
        {37, "36", "38"},
        {42, "33SS,57", nullptr},
    };
    for (const PinnedText& pinned : kPinned) {
        const app::TaskRow* task = task_by_id(plan, pinned.id);
        check(task != nullptr, "the pinned task id is in the file");
        if (task == nullptr) {
            continue;
        }
        const int display = display_row_of_uid(window, task->uid);
        check(display >= 0, "the pinned task has a displayed row");
        if (display < 0) {
            continue;
        }
        check_text(shown_text(model, display, pred_column),
                   QString::fromUtf8(pinned.predecessors), "the pinned Pre text",
                   static_cast<int>(pinned.id));
        if (pinned.successors != nullptr) {
            check_text(shown_text(model, display, succ_column),
                       QString::fromUtf8(pinned.successors), "the pinned Suc text",
                       static_cast<int>(pinned.id));
        }
    }

    // % Complete: the exact strings the coordinator's render of this file reported
    // (the file's own stored percents, printed with Project's sign).
    struct PinnedPercent {
        std::uint32_t id;
        const char* text;
    };
    const PinnedPercent kPinnedPercent[] = {
        // Identified by the file's ID column only: task names are client data
        // and are not quoted in the repository.
        {13, "57%"},  // ID 13
        {1, "99%"},   // ID 1
        {12, "29%"},  // ID 12
        {5, "100%"},  // ID 5
        {10, "0%"},   // ID 10
    };
    for (const PinnedPercent& pinned : kPinnedPercent) {
        const app::TaskRow* task = task_by_id(plan, pinned.id);
        check(task != nullptr, "the pinned percent's task id is in the file");
        if (task == nullptr) {
            continue;
        }
        const int display = display_row_of_uid(window, task->uid);
        check(display >= 0, "the pinned percent's task has a displayed row");
        if (display < 0) {
            continue;
        }
        check_text(shown_text(model, display, pct_column), QString::fromUtf8(pinned.text),
                   "the pinned % Complete text", static_cast<int>(pinned.id));
    }

    // SUMMARY is Task.Flag10 (0x0b400051).  Wave 23 (W23-6) decoded its Task
    // FixedMeta bit (byte +25 bit 0x04, measured through COM).  Wave 24
    // (W24-9/R11) also decodes the PROJECT row's own bit, so row 0 here is the
    // file's stored flag, not a constant; the capture's values are the
    // independent second witness.
    check(sheet.records().has_field(msqt::kSummaryFlagField),
          "the RecordSet carries the decoded Flag10 column");
    // The clean capture's first thirteen visible rows, typed from the pixels:
    // only the project summary row (0) and ID 12 read Yes.  Flag10
    // is the FILE's own field, NOT Task.Summary -- the capture's summary rows
    // 1/2/6 read No, which is exactly why the column is a stored flag.
    const char* captured_summary[13] = {"Yes", "No", "No", "No", "No", "No", "No",
                                        "No",  "No", "No", "No", "No", "Yes"};
    int summary_yes = 0;
    int summary_no = 0;
    int summary_wrong = 0;
    for (int display = 0; display < model->rowCount(); ++display) {
        const std::size_t record = table_model->record_row(display);
        if (record == core::sheet::kNoIndex) {
            continue;
        }
        const std::uint32_t uid = sheet.row_uid(record);
        const QString text = shown_text(model, display, summary_column);
        bool stored = false;
        if (uid == 0) {
            // Wave 24 (W24-9/R11): the project row's own stored FixedMeta
            // Flag10 bit, not a constant copied from the capture.  The capture
            // comparison below is then an independent second check.
            stored = plan.project_flag10_stored && plan.project_flag10;
        } else if (const app::TaskRow* task = task_by_uid(plan, uid)) {
            stored = task->flag10_stored && task->flag10;
        }
        if ((text == QStringLiteral("Yes")) != stored) {
            ++summary_wrong;
            std::fprintf(stderr, "  SUMMARY row uid %u: got %s want %s\n", uid,
                         text.toUtf8().constData(), stored ? "Yes" : "No");
        } else if (stored) {
            ++summary_yes;
        } else {
            ++summary_no;
        }
        if (display < 13 && text != QString::fromLatin1(captured_summary[display])) {
            ++summary_wrong;
            std::fprintf(stderr, "  SUMMARY capture row %d: got %s want %s\n", display,
                         text.toUtf8().constData(), captured_summary[display]);
        }
    }
    check(summary_wrong == 0,
          "SUMMARY equals the stored Flag10 and the capture's first thirteen rows");
    check(summary_yes > 0 && summary_no > 0, "SUMMARY has both values in the file");

    // Resource Names: the file's table stores no such column, and none of its 67
    // live assignment rows names a resource -- every row's raw +8 word is the null
    // sentinel 0xFFFF0001 (mpxj's ASSIGNMENT_NULL_RESOURCE_ID).  Both facts are read
    // from the bytes, so "blank" here is correct rather than a gap.
    std::size_t assignments_with_resource = 0;
    for (const app::AssignmentRow& assignment : plan.assignments) {
        if (assignment.resource_uid != 0) {
            ++assignments_with_resource;
        }
    }
    check(plan.assignments.size() == 67, "the corpus has 67 live assignment rows");
    check(assignments_with_resource == 0,
          "no corpus assignment row names a resource (mapped resource_uid == 0)");
    check(oracle.null_resource_word_rows + oracle.zero_resource_word_rows ==
              plan.assignments.size(),
          "every corpus assignment row stores 0xFFFF0001 or 0 in its resource word");
    bool any_resource_name = false;
    for (const app::TaskRow& task : plan.tasks) {
        if (!msqt::resource_names_text(plan, task.uid).empty()) {
            any_resource_name = true;
            break;
        }
    }
    check(!any_resource_name, "Resource Names has no source in this file");
    std::printf("note: Resource Names: %zu resource rows, %zu assignments (%zu with the "
                "0xFFFF0001 null word, %zu with 0), %zu naming a resource -> no text\n",
                plan.resources.size(), plan.assignments.size(),
                oracle.null_resource_word_rows, oracle.zero_resource_word_rows,
                assignments_with_resource);
}

}  // namespace

int main(int argc, char** argv) {
    // Unbuffered stdout: the gate's `note:` evidence lines must survive to the log
    // even if a check aborts.
    setvbuf(stdout, nullptr, _IONBF, 0);
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

    case_column_units();
    case_computed_text_rules();
    case_fixture_is_entry(fixture);
    case_synthetic_non_entry(fixture, out_dir);
    if (!corpus.isEmpty()) {
        case_corpus(corpus);
    }

    std::printf("qt_stored_table: %d failure(s), %d expected failure(s)\n", g_failures,
                g_expected_failures);
    return g_failures == 0 ? 0 : 1;
}
