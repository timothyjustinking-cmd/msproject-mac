/*
 * render_report_tests.cpp -- the report layer.
 *
 *   (b) a synthetic 200-row project: page breaks and page count > 1, every task
 *       rendered exactly once (no row dropped at a page boundary)
 *   (c) an empty project: one page, no crash
 *   (d) long names / parentheses / backslashes / non-ASCII: truncation at a
 *       UTF-8 boundary and PDF escaping
 *       plus the Gantt report on a synthetic linked project
 *   (e) with corpus/ present and the reader built: the real 83-task file.
 *       COUNTS ONLY are printed -- never a task name, per docs/coordination.md.
 *
 * Every produced document is run through the independent parser in
 * render_test.h, so a report that draws beautifully but writes a broken xref
 * still fails.
 */

#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "../core/render/pdf_writer.h"
#include "../core/render/report.h"
#include "render_test.h"

#ifdef RENDER_HAVE_MPP_READER
#include "mpp_bridge.h"
#endif

namespace {

using core::render::GanttReportRequest;
using core::render::PdfPageFormat;
using core::render::PdfWriter;
using core::render::SheetReportRequest;
using core::sched::date32;
using core::sheet::CellKind;
using core::sheet::SheetColumn;
using core::view::DateValue;
using core::view::FieldId;
using core::view::RecordSet;
using core::view::Value;
using render_test::PdfDocument;
using render_test::TestContext;

struct TaskFields {
    FieldId id = 0;
    FieldId name = 0;
    FieldId duration = 0;
    FieldId start = 0;
    FieldId finish = 0;
    FieldId slack = 0;
    FieldId critical = 0;
    FieldId summary = 0;
    FieldId milestone = 0;
    FieldId percent = 0;
};

TaskFields resolve_fields() {
    TaskFields fields;
    auto resolve = [](const char* name) {
        FieldId field = 0;
        core::view::field_id_by_name(core::model::RecordType::Task, name, field);
        return field;
    };
    fields.id = resolve("ID");
    fields.name = resolve("Name");
    fields.duration = resolve("Duration");
    fields.start = resolve("Start");
    fields.finish = resolve("Finish");
    fields.slack = resolve("TotalSlack");
    fields.critical = resolve("Critical");
    fields.summary = resolve("Summary");
    fields.milestone = resolve("Milestone");
    fields.percent = resolve("PercentComplete");
    return fields;
}

void ensure_fields(RecordSet& records, const TaskFields& fields) {
    for (const FieldId field :
         {fields.id, fields.name, fields.duration, fields.start, fields.finish, fields.slack,
          fields.critical, fields.summary, fields.milestone, fields.percent}) {
        if (field != 0) {
            records.add_field(field);
        }
    }
}

DateValue date_from(date32 date) {
    DateValue value;
    if (core::sched::is_na(date)) {
        return value;
    }
    const core::sched::CivilDate civil = core::sched::to_civil(date);
    value.year = civil.year;
    value.month = static_cast<std::uint8_t>(civil.month);
    value.day = static_cast<std::uint8_t>(civil.day);
    const std::int32_t minutes = core::sched::time_of(date) / core::sched::kTenthsPerMinute;
    value.hour = static_cast<std::uint8_t>(minutes / 60);
    value.minute = static_cast<std::uint8_t>(minutes % 60);
    return value;
}

struct SyntheticTask {
    std::uint32_t uid = 1;
    std::uint32_t row_id = 1;
    std::string name;
    std::int64_t duration_ticks = 14400; /* 3 days at 8 h/day */
    date32 start = core::sched::from_ymd(2026, 1, 5, 8, 0);
    date32 finish = core::sched::from_ymd(2026, 1, 7, 17, 0);
    std::int32_t slack_ticks = 0;
    bool critical = false;
    bool summary = false;
    bool milestone = false;
    int percent = 0;
};

void add_task(RecordSet& records, const TaskFields& fields, const SyntheticTask& task) {
    const std::size_t row = records.add_row(task.uid);
    if (fields.id != 0) {
        records.set_field(row, fields.id, Value::of_int(task.row_id));
    }
    if (fields.name != 0) {
        records.set_field(row, fields.name, Value::of_text(task.name));
    }
    if (fields.duration != 0) {
        records.set_field(row, fields.duration, Value::of_duration(task.duration_ticks));
    }
    if (fields.start != 0) {
        records.set_field(row, fields.start, Value::of_date(date_from(task.start)));
    }
    if (fields.finish != 0) {
        records.set_field(row, fields.finish, Value::of_date(date_from(task.finish)));
    }
    if (fields.slack != 0) {
        records.set_field(row, fields.slack, Value::of_duration(task.slack_ticks));
    }
    if (fields.critical != 0) {
        records.set_field(row, fields.critical, Value::of_bool(task.critical));
    }
    if (fields.summary != 0) {
        records.set_field(row, fields.summary, Value::of_bool(task.summary));
    }
    if (fields.milestone != 0) {
        records.set_field(row, fields.milestone, Value::of_bool(task.milestone));
    }
    if (fields.percent != 0) {
        records.set_field(row, fields.percent, Value::of_int(task.percent));
    }
}

/* `count` tasks, three working days each, consecutive, every seventh critical. */
RecordSet make_project(int count, const TaskFields& fields) {
    RecordSet records(core::model::RecordType::Task);
    ensure_fields(records, fields);
    const date32 base = core::sched::from_ymd(2026, 1, 5, 8, 0);
    const date32 finish_base = core::sched::from_ymd(2026, 1, 7, 0, 0);
    for (int i = 0; i < count; ++i) {
        SyntheticTask task;
        task.uid = static_cast<std::uint32_t>(1000 + i);
        task.row_id = static_cast<std::uint32_t>(i + 1);
        task.name = "Task " + std::to_string(i + 1);
        task.start = core::sched::add_days(base, i * 3);
        task.finish = core::sched::at_time(core::sched::add_days(finish_base, i * 3), 10190);
        task.critical = (i % 7) == 0;
        task.slack_ticks = task.critical ? 0 : 4800;
        task.percent = (i % 5) * 20;
        add_task(records, fields, task);
    }
    return records;
}

SheetReportRequest sheet_request(RecordSet& records) {
    SheetReportRequest request;
    request.records = &records;
    request.title = "Task sheet";
    request.subtitle = "Synthetic project";
    request.footer = "core/render report test";
    request.info.title = "Task sheet";
    return request;
}

std::size_t count_literals(const std::vector<std::string>& literals, std::string_view text) {
    std::size_t count = 0;
    for (const std::string& literal : literals) {
        if (literal == text) {
            ++count;
        }
    }
    return count;
}

bool has_literal(const std::vector<std::string>& literals, std::string_view text) {
    return count_literals(literals, text) != 0;
}

/*
 * Writes a SYNTHETIC sample into the build tree so a real PDF reader (or
 * core/render/verify_pdf.py) can check the writer externally.  Never called for
 * the corpus: a client-data document is never written to disk.  Failure to
 * write is a test failure, because the sample is also the bug-report artifact.
 */
void save_sample(PdfWriter& pdf, const char* name, TestContext& ctx) {
    const std::string path = std::string(RENDER_OUTPUT_DIR) + "/" + name;
    std::string error;
    const bool ok = pdf.save(path, error);
    ctx.check(ok, error.empty() ? "save sample PDF" : error.c_str(), __LINE__);
}

void test_default_columns(TestContext& ctx) {
    const std::vector<SheetColumn> columns = core::render::task_report_columns();
    RCHECK(ctx, columns.size() == 7);
    if (columns.size() == 7) {
        RCHECK(ctx, columns[1].title.empty()); /* the descriptor name resolves it */
        RCHECK(ctx, columns[5].title == "Slack");
        RCHECK(ctx, columns[2].kind == CellKind::Duration);
        RCHECK(ctx, columns[3].kind == CellKind::Date);
        RCHECK(ctx, columns[6].kind == CellKind::Bool);
    }
}

void test_sheet_200_rows(TestContext& ctx) {
    const TaskFields fields = resolve_fields();
    RecordSet records = make_project(200, fields);
    SheetReportRequest request = sheet_request(records);

    PdfWriter pdf;
    const std::size_t pages = core::render::render_sheet_report(pdf, request);
    const std::string bytes = pdf.finish();
    save_sample(pdf, "sheet-synthetic-200.pdf", ctx);

    RCHECK(ctx, pages > 1);
    RCHECK(ctx, pages <= 10);
    RCHECK(ctx, bytes.size() > 4000);

    std::string error;
    const bool valid = render_test::validate_pdf(bytes, pages, error);
    ctx.check(valid, error.empty() ? "validate_pdf" : error.c_str(), __LINE__);
    if (!valid) {
        return;
    }
    PdfDocument doc;
    std::string parse_error;
    if (!render_test::parse_pdf(bytes, doc, parse_error)) {
        ctx.check(false, parse_error.c_str(), __LINE__);
        return;
    }

    /* Every synthetic task is printed exactly once: no row lost at a page break. */
    std::size_t found = 0;
    for (int i = 1; i <= 200; ++i) {
        const std::string name = "Task " + std::to_string(i);
        if (count_literals(doc.literals, name) == 1) {
            ++found;
        }
    }
    RCHECK(ctx, found == 200);

    /* The column header repeats on every page, which is what "repeated column
     * headers" means in the brief. */
    RCHECK(ctx, count_literals(doc.literals, "Name") == pages);
    RCHECK(ctx, count_literals(doc.literals, "Duration") == pages);
    RCHECK(ctx, count_literals(doc.literals, "Slack") == pages);

    /* Page numbering is on each page and names the total. */
    for (std::size_t page = 1; page <= pages; ++page) {
        const std::string label =
            "Page " + std::to_string(page) + " of " + std::to_string(pages);
        RCHECK(ctx, has_literal(doc.literals, label));
    }

    /* Byte determinism: rendering the same request twice is identical. */
    PdfWriter again;
    const std::size_t pages_again = core::render::render_sheet_report(again, request);
    const std::string bytes_again = again.finish();
    RCHECK(ctx, pages_again == pages);
    RCHECK(ctx, bytes_again == bytes);
    RCHECK(ctx, render_test::fnv1a_hex(bytes) == render_test::fnv1a_hex(bytes_again));
}

void test_empty_project(TestContext& ctx) {
    const TaskFields fields = resolve_fields();
    RecordSet records(core::model::RecordType::Task);
    ensure_fields(records, fields);

    SheetReportRequest request = sheet_request(records);
    PdfWriter pdf;
    const std::size_t pages = core::render::render_sheet_report(pdf, request);
    const std::string bytes = pdf.finish();
    save_sample(pdf, "sheet-empty.pdf", ctx);
    RCHECK(ctx, pages == 1);
    RCHECK(ctx, bytes.size() > 500);
    std::string error;
    RCHECK(ctx, render_test::validate_pdf(bytes, 1, error));

    PdfDocument doc;
    std::string parse_error;
    if (render_test::parse_pdf(bytes, doc, parse_error)) {
        RCHECK(ctx, has_literal(doc.literals, "(no rows)"));
        RCHECK(ctx, has_literal(doc.literals, "Page 1 of 1"));
    } else {
        ctx.check(false, parse_error.c_str(), __LINE__);
    }

    /* A completely bare RecordSet (no fields either) must also produce one page,
     * not a crash: the columns are the descriptor's, the values are all Null. */
    RecordSet bare(core::model::RecordType::Task);
    SheetReportRequest bare_request = sheet_request(bare);
    PdfWriter bare_pdf;
    const std::size_t bare_pages = core::render::render_sheet_report(bare_pdf, bare_request);
    RCHECK(ctx, bare_pages == 1);
    RCHECK(ctx, render_test::validate_pdf(bare_pdf.finish(), 1, error));

    /* Empty Gantt: one page with a note, no bars. */
    GanttReportRequest gantt_request;
    gantt_request.records = &records;
    gantt_request.title = "Gantt";
    gantt_request.columns = core::render::task_report_columns();
    PdfWriter gantt_pdf;
    const std::size_t gantt_pages = core::render::render_gantt_report(gantt_pdf, gantt_request);
    RCHECK(ctx, gantt_pages == 1);
    RCHECK(ctx, render_test::validate_pdf(gantt_pdf.finish(), 1, error));
}

void test_long_names_and_escaping(TestContext& ctx) {
    const TaskFields fields = resolve_fields();
    RecordSet records(core::model::RecordType::Task);
    ensure_fields(records, fields);

    SyntheticTask long_task;
    long_task.uid = 42;
    long_task.row_id = 1;
    /* Non-ASCII first (so it survives truncation), then 300 characters, then the
     * parenthesis/backslash suffix: the exact combination that breaks naive
     * writers. */
    long_task.name = "Caf\xC3\xA9 " + std::string(300, 'A') + " (with) parens \\";
    add_task(records, fields, long_task);

    SheetReportRequest request = sheet_request(records);
    PdfWriter pdf;
    const std::size_t pages = core::render::render_sheet_report(pdf, request);
    const std::string bytes = pdf.finish();
    save_sample(pdf, "sheet-long-names.pdf", ctx);
    RCHECK(ctx, pages == 1);
    std::string error;
    RCHECK(ctx, render_test::validate_pdf(bytes, pages, error));

    /* The WinAnsi byte is a single byte; the UTF-8 sequence never is. */
    RCHECK(ctx, render_test::contains(bytes, "Caf\xE9"));
    RCHECK(ctx, bytes.find("\xC3\xA9") == std::string::npos);

    PdfDocument doc;
    std::string parse_error;
    if (!render_test::parse_pdf(bytes, doc, parse_error)) {
        ctx.check(false, parse_error.c_str(), __LINE__);
        return;
    }
    std::string longest;
    for (const std::string& literal : doc.literals) {
        if (literal.size() > longest.size()) {
            longest = literal;
        }
    }
    RCHECK(ctx, longest.size() < 300); /* truncated */
    RCHECK(ctx, longest.size() > 3);
    RCHECK(ctx, longest.compare(0, 5, "Caf\xE9 ") == 0);
    RCHECK(ctx, longest.compare(longest.size() - 3, 3, "...") == 0);
    /* The truncation cut at a code point boundary: after the "Café " prefix it
     * is a run of 'A' and the ellipsis, never half a UTF-8 sequence. */
    bool ascii_run = longest.size() > 8;
    for (std::size_t i = 5; i + 3 < longest.size(); ++i) {
        if (longest[i] != 'A') {
            ascii_run = false;
        }
    }
    RCHECK(ctx, ascii_run);

    /* A name that fits but contains parentheses and a backslash round-trips. */
    RecordSet second(core::model::RecordType::Task);
    ensure_fields(second, fields);
    SyntheticTask short_task;
    short_task.uid = 7;
    short_task.row_id = 1;
    short_task.name = "A(B)C\\D";
    add_task(second, fields, short_task);
    SheetReportRequest second_request = sheet_request(second);
    PdfWriter second_pdf;
    core::render::render_sheet_report(second_pdf, second_request);
    const std::string second_bytes = second_pdf.finish();
    RCHECK(ctx, render_test::validate_pdf(second_bytes, 1, error));
    PdfDocument second_doc;
    const bool second_parsed = render_test::parse_pdf(second_bytes, second_doc, parse_error);
    ctx.check(second_parsed, parse_error.empty() ? "parse_pdf second" : parse_error.c_str(),
              __LINE__);
    if (second_parsed) {
        RCHECK(ctx, has_literal(second_doc.literals, "A(B)C\\D"));
    }
}

void test_gantt_report(TestContext& ctx) {
    const TaskFields fields = resolve_fields();
    RecordSet records(core::model::RecordType::Task);
    ensure_fields(records, fields);

    const date32 base = core::sched::from_ymd(2026, 1, 5, 8, 0);
    for (int i = 0; i < 60; ++i) {
        SyntheticTask task;
        task.uid = static_cast<std::uint32_t>(2000 + i);
        task.row_id = static_cast<std::uint32_t>(i + 1);
        task.name = "Task " + std::to_string(i + 1);
        task.start = core::sched::add_days(base, i * 2);
        task.finish = core::sched::at_time(core::sched::add_days(base, i * 2 + 1), 10190);
        task.critical = (i % 9) == 0;
        task.summary = i == 0;          /* one summary bar */
        task.milestone = (i % 10) == 4; /* six milestones */
        task.duration_ticks = task.milestone ? 0 : 9600;
        task.percent = (i % 4) * 25;
        add_task(records, fields, task);
    }

    GanttReportRequest request;
    request.records = &records;
    request.title = "Gantt chart";
    request.subtitle = "Synthetic project";
    request.info.title = "Gantt chart";
    request.columns = core::render::task_report_columns();
    for (int i = 0; i < 59; ++i) {
        core::gantt::LinkInput link;
        link.predecessor_uid = static_cast<std::uint32_t>(2000 + i);
        link.successor_uid = static_cast<std::uint32_t>(2000 + i + 1);
        link.type = (i % 2 == 0) ? core::gantt::LinkType::finish_to_start
                                 : core::gantt::LinkType::start_to_start;
        link.critical = (i % 9) == 0;
        request.links.push_back(link);
    }

    PdfWriter pdf;
    const std::size_t pages = core::render::render_gantt_report(pdf, request);
    const std::string bytes = pdf.finish();
    save_sample(pdf, "gantt-synthetic-60.pdf", ctx);
    RCHECK(ctx, pages > 1);
    RCHECK(ctx, bytes.size() > 8000);

    std::string error;
    RCHECK(ctx, render_test::validate_pdf(bytes, pages, error));

    PdfDocument doc;
    std::string parse_error;
    if (!render_test::parse_pdf(bytes, doc, parse_error)) {
        ctx.check(false, parse_error.c_str(), __LINE__);
        return;
    }
    /* Every task name appears at least once (table and/or bar label). */
    std::size_t distinct = 0;
    for (int i = 1; i <= 60; ++i) {
        if (count_literals(doc.literals, "Task " + std::to_string(i)) >= 1) {
            ++distinct;
        }
    }
    RCHECK(ctx, distinct == 60);
    /* The legend rides on every page, and so does the page counter. */
    RCHECK(ctx, count_literals(doc.literals, "Dependency") == pages);
    RCHECK(ctx, count_literals(doc.literals, "Summary") >= pages);
    RCHECK(ctx, has_literal(doc.literals, "Page " + std::to_string(pages) + " of " +
                                              std::to_string(pages)));
    /* Bars, diamonds and dependency lines are PDF drawing operators. */
    RCHECK(ctx, render_test::contains(bytes, " re\n"));
    RCHECK(ctx, render_test::contains(bytes, "h\nf\n")); /* polygon fill */
    RCHECK(ctx, render_test::contains(bytes, "W n"));    /* clipping */

    PdfWriter again;
    const std::size_t pages_again = core::render::render_gantt_report(again, request);
    RCHECK(ctx, pages_again == pages);
    RCHECK(ctx, again.finish() == bytes);
}

void test_gantt_minimal_and_queries(TestContext& ctx) {
    /* A single task with no links, portrait page: a valid one-page report. */
    const TaskFields fields = resolve_fields();
    RecordSet records(core::model::RecordType::Task);
    ensure_fields(records, fields);
    SyntheticTask task;
    task.uid = 1;
    task.name = "Only task";
    add_task(records, fields, task);

    GanttReportRequest request;
    request.records = &records;
    request.title = "Gantt";
    request.style.page = PdfPageFormat::a4_portrait();
    request.columns.clear();
    PdfWriter pdf;
    const std::size_t pages = core::render::render_gantt_report(pdf, request);
    RCHECK(ctx, pages == 1);
    std::string error;
    RCHECK(ctx, render_test::validate_pdf(pdf.finish(), 1, error));

    /* A null record set is an error page, not a crash. */
    SheetReportRequest null_request;
    null_request.records = nullptr;
    null_request.title = "Nothing";
    PdfWriter null_pdf;
    RCHECK(ctx, core::render::render_sheet_report(null_pdf, null_request) == 1);
    RCHECK(ctx, render_test::validate_pdf(null_pdf.finish(), 1, error));
}

#ifdef RENDER_HAVE_MPP_READER
/* The real 83-task programme.  Only counts are printed. */
void test_corpus_file(TestContext& ctx) {
    const std::string path = RENDER_PROJECT_A_MPP;
    {
        std::ifstream probe(path, std::ios::binary);
        if (!probe) {
            std::printf("[render_report] corpus file absent: SKIP (counts not asserted)\n");
            return;
        }
    }

    mpp::Document document;
    try {
        document = mpp::load_file(path);
    } catch (const std::exception&) {
        std::printf("[render_report] corpus file unreadable: SKIP\n");
        return;
    }
    const std::size_t row_count = document.tasks.size();
    RCHECK(ctx, row_count == 83);

    const TaskFields fields = resolve_fields();
    RecordSet records(core::model::RecordType::Task);
    ensure_fields(records, fields);
    for (const mpp::Task& task : document.tasks) {
        const std::size_t row = records.add_row(task.uid);
        if (fields.id != 0) {
            records.set_field(row, fields.id, Value::of_int(task.row_id));
        }
        if (fields.name != 0) {
            records.set_field(row, fields.name, Value::of_text(task.name));
        }
        if (fields.duration != 0) {
            records.set_field(row, fields.duration, Value::of_duration(task.duration_ticks));
        }
        if (fields.start != 0 && task.start.valid()) {
            records.set_field(row, fields.start,
                              Value::of_date(core::io::bridge_detail::convert_date(task.start)));
        }
        if (fields.finish != 0 && task.finish.valid()) {
            records.set_field(row, fields.finish,
                              Value::of_date(core::io::bridge_detail::convert_date(task.finish)));
        }
        if (fields.slack != 0) {
            records.set_field(row, fields.slack, Value::of_duration(task.total_slack_ticks));
        }
        if (fields.critical != 0) {
            records.set_field(row, fields.critical, Value::of_bool(task.critical));
        }
        if (fields.summary != 0) {
            records.set_field(row, fields.summary, Value::of_bool(task.summary));
        }
        if (fields.milestone != 0) {
            records.set_field(row, fields.milestone, Value::of_bool(task.milestone));
        }
        if (fields.percent != 0) {
            records.set_field(row, fields.percent, Value::of_int(task.percent_complete));
        }
    }
    RCHECK(ctx, records.row_count() == 83);

    SheetReportRequest sheet;
    sheet.records = &records;
    sheet.title = "Task sheet";
    PdfWriter sheet_pdf;
    const std::size_t sheet_pages = core::render::render_sheet_report(sheet_pdf, sheet);
    const std::string sheet_bytes = sheet_pdf.finish();
    std::string error;
    RCHECK(ctx, sheet_pages > 1);
    RCHECK(ctx, sheet_bytes.size() > 4000);
    RCHECK(ctx, render_test::validate_pdf(sheet_bytes, sheet_pages, error));
    std::printf("[render_report] corpus sheet: rows=%zu pages=%zu bytes=%zu\n", row_count,
                sheet_pages, sheet_bytes.size());

    GanttReportRequest gantt;
    gantt.records = &records;
    gantt.title = "Gantt chart";
    for (const mpp::Link& link : document.links) {
        if (!link.endpoints_live()) {
            continue;
        }
        core::gantt::LinkInput input;
        input.predecessor_uid = link.predecessor_uid;
        input.successor_uid = link.successor_uid;
        switch (link.type_raw & 0x3u) {
            case 0:
                input.type = core::gantt::LinkType::finish_to_finish;
                break;
            case 2:
                input.type = core::gantt::LinkType::start_to_finish;
                break;
            case 3:
                input.type = core::gantt::LinkType::start_to_start;
                break;
            case 1:
            default:
                input.type = core::gantt::LinkType::finish_to_start;
                break;
        }
        gantt.links.push_back(input);
    }
    PdfWriter gantt_pdf;
    const std::size_t gantt_pages = core::render::render_gantt_report(gantt_pdf, gantt);
    const std::string gantt_bytes = gantt_pdf.finish();
    RCHECK(ctx, gantt_pages > 1);
    RCHECK(ctx, gantt_bytes.size() > 8000);
    RCHECK(ctx, render_test::validate_pdf(gantt_bytes, gantt_pages, error));
    std::printf("[render_report] corpus gantt: rows=%zu links=%zu pages=%zu bytes=%zu\n",
                row_count, gantt.links.size(), gantt_pages, gantt_bytes.size());
}
#endif

}  // namespace

int run_report_tests(TestContext& ctx) {
    ctx.suite = "render_report";
    test_default_columns(ctx);
    test_sheet_200_rows(ctx);
    test_empty_project(ctx);
    test_long_names_and_escaping(ctx);
    test_gantt_report(ctx);
    test_gantt_minimal_and_queries(ctx);
#ifdef RENDER_HAVE_MPP_READER
    test_corpus_file(ctx);
#else
    std::printf("[render_report] no reader in this build: corpus test SKIP\n");
#endif
    return ctx.failures;
}
