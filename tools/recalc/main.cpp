// tools/recalc -- recalculate an .mpp programme with core::plan and compare the
// result with the dates and slack stored in a file.
//
//   recalc <file.mpp> [--compare <other.mpp>] [--tsv out.tsv] [--fields]
//
// No options: one TSV row per task -- uid, start, finish, slack (minutes),
// critical -- from core::plan's recalculation of <file.mpp>.  A summary row
// carries the dates core::sched::rollup_tasks rolled up from its children.
//
// --compare <other.mpp>: the recalculation of <file.mpp> is compared with the
// values STORED in <other.mpp>.  That is the M2 question: open file A, ignore
// the dates Project saved, rebuild them from the graph, and diff against the
// dates Project saved in B.  One line per differing field is printed, then a
// per-field match/mismatch table, split into leaves and summaries.  The stored
// side comes from mpp::Task: start, finish, total_slack_ticks (row +74), critical
// (derived) and summary.
//
// --tsv <out.tsv>: write the five recalculated columns to a file.
//
// --fields: dump the reader fields core::plan::from_mpp consumed for <file.mpp>,
// one row per task: uid, row, outline number, summary, child count, outline
// level, duration, percent complete, start, finish, stored slack, stored
// critical, constraint type/date, deadline, actual start/finish and actual
// duration.  This is how a reader gap is shown rather than guessed: while W19's
// decoder is still default the constraint/actual columns are 0/NA on every row.
// No task names are printed (corpus files are client data).
//
// --links: dump the link rows the bridge mapped (predecessor, successor, type,
// lag in working days) and the ones it skipped.
//
// --attrib-json <out.json>: with --compare, write the attribution counts and one
// object per mismatching (uid, field) to <out.json>: family, ours, stored.  The
// classifier and its rules are core/plan/attribution.h.  The same family x field
// matrix is always printed after the --compare tables (uids and values only, no
// names).  --attrib-json without --compare is a usage error.
//
// CALENDAR, CONSTRAINTS, ACTUALS, SUMMARIES.  The project calendar is decoded
// from the file's own table root -- `<table_root>/TBkndCal` plus the Standard
// week in `<table_root>/Props`, where table_root is the root the READER detected
// ("114" for MPP14, "112" for MPP12; core/cal::decode_file applies the same
// rule).  Every task that stores a +178 calendar uid gets its own
// core::sched::Calendar through core::plan::attach_task_calendars.  A file that
// cannot be decoded falls back to the stock Mon-Fri 08:00-12:00/13:00-17:00
// week with a visible note.
// Constraints and deadlines are taken from mpp::Task; actual dates pin a started
// or completed task
// (spec/scheduling.md §2.4) and summaries are rolled up from their children after
// the CPM passes (spec §3.7).  The notes from the mapping are printed on stderr,
// so a divergence caused by an assumption is visible rather than mistaken for an
// engine bug.  See core/plan/plan.h.
//
// Exit codes: 0 success (including "the schedules differ": the counts are the
// result), 1 the input could not be read or written, 2 bad usage.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "attribution.h"
#include "cal.h"
#include "plan.h"
#include "reader.h"

namespace {

// One mismatching (uid, field) with its attribution.  Values are formatted for
// display/JSON; no task name ever reaches this struct.
struct Mismatch {
    std::int32_t uid = 0;
    core::plan::AttribField field = core::plan::AttribField::start;
    core::plan::DivergenceFamily family = core::plan::DivergenceFamily::unexplained;
    std::string ours;
    std::string stored;
};

// The attribution half of a --compare run: per-field match/mismatch counts (the
// same numbers the existing tables print), the family x field matrix and the
// mismatch rows.
struct Attribution {
    std::size_t matches[core::plan::kAttribFieldCount] = {};
    std::size_t mismatches[core::plan::kAttribFieldCount] = {};
    std::size_t family_field[core::plan::kDivergenceFamilyCount]
                            [core::plan::kAttribFieldCount] = {};
    std::vector<Mismatch> rows;
};

void print_usage(std::ostream& out) {
    out << "usage: recalc <file.mpp> [--compare <other.mpp>] [--tsv <out.tsv>] [--fields]\n"
        << "\n"
        << "  <file.mpp>            recalculate this programme\n"
        << "  --compare <other.mpp> compare the recalculation with the values\n"
        << "                        stored in <other.mpp>\n"
        << "  --tsv <out.tsv>       write uid/start/finish/slack/critical to a file\n"
        << "  --attrib-json <path>  with --compare, write attribution counts and rows\n"
        << "  --fields              dump the reader fields the bridge consumed\n"
        << "  --links               dump the links the bridge mapped and skipped\n"
        << "  -h, --help            show this text\n";
}

const char* yes_no(bool value) { return value ? "yes" : "no"; }

std::string describe_result(const core::plan::PlanResult& result) {
    return "error=" + std::string(core::sched::to_string(result.error)) + " " +
           result.message;
}

// Packed dates: "NA" for the reader's two "not set" values, the date otherwise.
std::string date_text(std::uint32_t raw) {
    if (raw == 0 || raw == 0xFFFFFFFFu) {
        return "NA";
    }
    return core::plan::format_date(raw);
}

void print_fields(const mpp::Document& document) {
    std::cout << "fields: " << document.tasks.size()
              << " live tasks (reader values; no names)\n";
    std::cout << "uid\trow\toutline\tsummary\tchildren\tlevel\tmanual\tdur_ticks\t"
                 "dur_fmt\tcalendar_uid\tpct\tstart\t"
                 "finish\tslack_ticks\tcritical\tconstraint_type\tconstraint_date\t"
                 "deadline\tactual_start\tactual_finish\tactual_dur_ticks\n";
    for (const mpp::Task& task : document.tasks) {
        std::cout << task.uid << '\t' << task.row << '\t'
                  << (task.outline_number.empty() ? "-" : task.outline_number) << '\t'
                  << (task.summary ? 1 : 0) << '\t' << task.child_count << '\t'
                  << task.outline_level << '\t' << (task.manually_scheduled ? 1 : 0) << '\t'
                  << task.duration_ticks << '\t' << task.duration_format << '\t'
                  << task.calendar_uid << '\t'
                  << task.percent_complete << '\t' << date_text(task.start.raw) << '\t'
                  << date_text(task.finish.raw) << '\t' << task.total_slack_ticks << '\t'
                  << (task.critical ? 1 : 0) << '\t' << task.constraint_type << '\t'
                  << date_text(task.constraint_date.raw) << '\t'
                  << date_text(task.deadline.raw) << '\t'
                  << date_text(task.actual_start.raw) << '\t'
                  << date_text(task.actual_finish.raw) << '\t'
                  << task.actual_duration_ticks << '\n';
    }
    std::cout << std::flush;
}

void print_links(const core::plan::PlanInput& input) {
    std::cout << "links: " << input.links.size() << " mapped, " << input.skipped_links
              << " skipped (deleted/unknown/self), " << input.summary_links
              << " skipped (summary endpoint)\n";
    std::cout << "pred\tsucc\ttype\tlag_days\tlag_ticks\telapsed\n";
    for (const core::plan::PlanLink& link : input.links) {
        std::cout << link.predecessor << '\t' << link.successor << '\t'
                  << static_cast<int>(link.type) << '\t'
                  << (static_cast<double>(link.lag) / 4800.0) << '\t' << link.lag << '\t'
                  << (link.elapsed_lag ? 1 : 0) << '\n';
    }
    std::cout << std::flush;
}

// The family x field matrix, the match/mismatch totals and the first mismatch
// uids per family.  Everything printed here is a uid, a date or a count: the
// corpus task names are never read by this tool.
void print_attribution(const Attribution& attribution) {
    std::cout << "\nattribution: every mismatching uid/field in exactly one family\n"
              << "  (rules: core/plan/attribution.h; precedence is the row order)\n";
    std::cout << "  family                start  finish   slack  critical   total\n";
    for (std::size_t family = 0; family < core::plan::kDivergenceFamilyCount; ++family) {
        const std::string name = core::plan::family_name(
            static_cast<core::plan::DivergenceFamily>(family));
        std::size_t total = 0;
        std::cout << "  " << std::left << std::setw(20) << name << std::right;
        for (std::size_t field = 0; field < core::plan::kAttribFieldCount; ++field) {
            std::cout << std::setw(8) << attribution.family_field[family][field];
            total += attribution.family_field[family][field];
        }
        std::cout << std::setw(8) << total << '\n';
    }
    std::cout << "  " << std::left << std::setw(20) << "mismatch" << std::right;
    std::size_t mismatch_total = 0;
    for (std::size_t field = 0; field < core::plan::kAttribFieldCount; ++field) {
        std::cout << std::setw(8) << attribution.mismatches[field];
        mismatch_total += attribution.mismatches[field];
    }
    std::cout << std::setw(8) << mismatch_total << '\n';
    std::cout << "  " << std::left << std::setw(20) << "match" << std::right;
    for (std::size_t field = 0; field < core::plan::kAttribFieldCount; ++field) {
        std::cout << std::setw(8) << (std::to_string(attribution.matches[field]) + "/" +
                                       std::to_string(attribution.matches[field] +
                                                      attribution.mismatches[field]));
    }
    std::cout << '\n';

    // First few mismatches per family, uids/dates/values only.  Kept short so the
    // table remains readable; the complete list is in --attrib-json.
    constexpr std::size_t kExamplesPerFamily = 4;
    std::cout << "  first mismatches per family (uid field ours stored; max "
              << kExamplesPerFamily << " each):\n";
    for (std::size_t family = 0; family < core::plan::kDivergenceFamilyCount; ++family) {
        const auto family_id = static_cast<core::plan::DivergenceFamily>(family);
        std::size_t shown = 0;
        for (const Mismatch& row : attribution.rows) {
            if (row.family != family_id || shown >= kExamplesPerFamily) {
                continue;
            }
            std::cout << "    " << core::plan::family_name(family_id) << ": uid=" << row.uid
                      << " field=" << core::plan::field_name(row.field)
                      << " ours=" << row.ours << " stored=" << row.stored << '\n';
            ++shown;
        }
        if (shown == 0) {
            std::cout << "    " << core::plan::family_name(family_id) << ": (none)\n";
        }
    }
    std::cout << "  note: 'actual' is a date-field family; rows that keep an actual\n"
              << "        start/finish are counted in the compare header above.\n";
    std::cout << std::flush;
}

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char character : text) {
        switch (character) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                // Paths/date strings are printable; replace any other control
                // character rather than emit invalid JSON.
                out += static_cast<unsigned char>(character) < 0x20 ? '?' : character;
                break;
        }
    }
    return out;
}

// The JSON is the machine-readable copy of the printed attribution: the same
// per-field counts, the same family x field matrix and one object per
// mismatching (uid, field).  Only uids, formatted dates/values and family/field
// names are written -- never a task name.
bool write_attribution_json(const std::string& path, const std::string& source,
                            const std::string& compared, const Attribution& attribution) {
    std::ofstream out(path);
    if (!out) {
        std::cerr << "recalc: cannot write " << path << '\n';
        return false;
    }
    out << "{\n";
    out << "  \"schema\": \"core.plan.attribution/1\",\n";
    out << "  \"recalculated\": \"" << json_escape(source) << "\",\n";
    out << "  \"stored\": \"" << json_escape(compared) << "\",\n";
    out << "  \"fields\": {\n";
    for (std::size_t field = 0; field < core::plan::kAttribFieldCount; ++field) {
        const auto field_id = static_cast<core::plan::AttribField>(field);
        out << "    \"" << core::plan::field_name(field_id) << "\": {\"match\": "
            << attribution.matches[field] << ", \"mismatch\": "
            << attribution.mismatches[field] << ", \"total\": "
            << attribution.matches[field] + attribution.mismatches[field] << '}'
            << (field + 1 < core::plan::kAttribFieldCount ? "," : "") << '\n';
    }
    out << "  },\n";
    out << "  \"families\": {\n";
    for (std::size_t family = 0; family < core::plan::kDivergenceFamilyCount; ++family) {
        const auto family_id = static_cast<core::plan::DivergenceFamily>(family);
        out << "    \"" << core::plan::family_name(family_id) << "\": {";
        for (std::size_t field = 0; field < core::plan::kAttribFieldCount; ++field) {
            out << "\"" << core::plan::field_name(static_cast<core::plan::AttribField>(field))
                << "\": " << attribution.family_field[family][field]
                << (field + 1 < core::plan::kAttribFieldCount ? ", " : "");
        }
        out << '}' << (family + 1 < core::plan::kDivergenceFamilyCount ? "," : "") << '\n';
    }
    out << "  },\n";
    out << "  \"rows\": [\n";
    for (std::size_t index = 0; index < attribution.rows.size(); ++index) {
        const Mismatch& row = attribution.rows[index];
        out << "    {\"uid\": " << row.uid << ", \"field\": \""
            << core::plan::field_name(row.field) << "\", \"family\": \""
            << core::plan::family_name(row.family) << "\", \"ours\": \""
            << json_escape(row.ours) << "\", \"stored\": \"" << json_escape(row.stored)
            << "\"}" << (index + 1 < attribution.rows.size() ? "," : "") << '\n';
    }
    out << "  ]\n";
    out << "}\n";
    out.close();
    if (!out) {
        std::cerr << "recalc: write error on " << path << '\n';
        return false;
    }
    std::cerr << "recalc: wrote attribution (" << attribution.rows.size()
              << " mismatching uid/field rows) to " << path << '\n';
    return true;
}

int run(const std::string& path, const std::string& compare_path, const std::string& tsv_path,
        const std::string& attrib_json_path, bool dump_fields, bool dump_links) {
    mpp::Document document;
    try {
        document = mpp::load_file(path);
    } catch (const std::exception& error) {
        std::cerr << "recalc: " << path << ": " << error.what() << '\n';
        return 1;
    }

    // Reader/container anomalies (FAT slack: "N unreferenced sectors", recoverable
    // table damage from Document::warnings).  Printed on stderr, so the TSV and the
    // --compare tables on stdout are unchanged.  A damaged real file can carry
    // hundreds of warnings (PROJECT_A 30, sf248 280+), so show a bounded list and
    // the total count; reader_dump --warnings prints all of them.
    constexpr std::size_t kMaxReaderWarnings = 20;
    for (std::size_t i = 0; i < document.warnings.size() && i < kMaxReaderWarnings; ++i) {
        std::cerr << "  reader warning: " << document.warnings[i] << '\n';
    }
    if (document.warnings.size() > kMaxReaderWarnings) {
        std::cerr << "  reader warning: ... and "
                  << (document.warnings.size() - kMaxReaderWarnings)
                  << " more (reader_dump --warnings prints all)\n";
    }

    if (dump_fields) {
        print_fields(document);
    }

    core::plan::PlanInput input = core::plan::from_mpp(document);
    // The file's own calendar: core::cal detects the table root the same way the
    // reader does (114/ for MPP14, 112/ for MPP12) and reads TBkndCal + Props
    // there.  from_mpp still returns the stock standard calendar; replace it when
    // the table decodes, and say so on stderr.  A damaged calendar must not stop
    // the recalculation: fall back to the standard week with a visible note.
    try {
        const core::cal::CalendarTable table = core::cal::decode_file(path);
        input.calendar = core::cal::build_project_calendar(table);
        core::plan::attach_task_calendars(input, table);
        std::cerr << "  calendar: decoded from the file ("
                  << input.calendar.work_week_count() << " project week overrides, "
                  << input.task_calendars.size() << " task calendars)\n";
    } catch (const std::exception& error) {
        std::cerr << "  note: calendar decode failed, using the standard week: "
                  << error.what() << '\n';
    }
    const core::plan::PlanResult result = core::plan::recalculate(input);

    std::cerr << "recalc: " << path << '\n';
    std::cerr << "  tasks " << input.tasks.size() << ", summaries " << input.summaries
              << ", links " << input.links.size() << ", skipped links "
              << input.skipped_links << ", summary links " << input.summary_links << '\n';
    for (const std::string& note : input.notes) {
        std::cerr << "  note: " << note << '\n';
    }
    if (!result.ok()) {
        std::cerr << "recalc: recalculation failed: " << describe_result(result) << '\n';
        return 1;
    }
    std::cerr << "  project start " << core::plan::format_date(result.project_start)
              << ", finish " << core::plan::format_date(result.project_finish) << '\n';
    if (dump_links) {
        print_links(input);
    }

    if (!tsv_path.empty()) {
        std::ofstream out(tsv_path);
        if (!out) {
            std::cerr << "recalc: cannot write " << tsv_path << '\n';
            return 1;
        }
        out << "uid\tstart\tfinish\tslack_min\tcritical\n";
        for (const core::plan::PlanTaskResult& task : result.tasks) {
            out << task.uid << '\t' << core::plan::format_date(task.start) << '\t'
                << core::plan::format_date(task.finish) << '\t'
                << core::plan::format_slack(task.total_slack) << '\t'
                << yes_no(task.critical) << '\n';
        }
        if (!out) {
            std::cerr << "recalc: write error on " << tsv_path << '\n';
            return 1;
        }
        std::cerr << "recalc: wrote " << result.tasks.size() << " rows to " << tsv_path
                  << '\n';
    }

    // The table itself.  uid, start, finish, slack, critical (the brief's
    // five columns); slack is TotalSlack in minutes because that is MS
    // Project's COM unit (spec §6).
    std::cout << "uid\tsummary\tstart\tfinish\tslack_min\tcritical\n";
    for (const core::plan::PlanTaskResult& task : result.tasks) {
        std::cout << task.uid << '\t' << (task.summary ? 1 : 0) << '\t'
                  << core::plan::format_date(task.start) << '\t'
                  << core::plan::format_date(task.finish) << '\t'
                  << core::plan::format_slack(task.total_slack) << '\t'
                  << yes_no(task.critical) << '\n';
    }
    if (!std::cout) {
        std::cerr << "recalc: write error on stdout\n";
        return 1;
    }

    if (compare_path.empty()) {
        return 0;
    }

    mpp::Document stored;
    try {
        stored = mpp::load_file(compare_path);
    } catch (const std::exception& error) {
        std::cerr << "recalc: " << compare_path << ": " << error.what() << '\n';
        return 1;
    }

    std::map<std::int32_t, core::plan::StoredRow> by_uid;
    for (const mpp::Task& task : stored.tasks) {
        core::plan::StoredRow row;
        row.start = task.start.raw;
        row.finish = task.finish.raw;
        row.total_slack = static_cast<core::sched::ticks>(task.total_slack_ticks);
        row.critical = task.critical;
        row.summary = task.summary;
        by_uid[static_cast<std::int32_t>(task.uid)] = row;
    }

    struct FieldCount {
        const char* name = "";
        std::size_t match = 0;
        std::size_t mismatch = 0;
        std::size_t leaf_match = 0;
        std::size_t leaf_mismatch = 0;
        std::size_t summary_match = 0;
        std::size_t summary_mismatch = 0;
    };
    std::vector<FieldCount> fields{{"start"}, {"finish"}, {"slack"}, {"critical"}};
    const auto field_index = [&fields](const char* name) -> FieldCount& {
        for (FieldCount& field : fields) {
            if (std::string(field.name) == name) {
                return field;
            }
        }
        return fields.front();  // unreachable: every name above is present
    };
    const auto record = [&field_index](const char* name, bool is_summary, bool matched) {
        FieldCount& field = field_index(name);
        if (matched) {
            ++field.match;
            ++(is_summary ? field.summary_match : field.leaf_match);
        } else {
            ++field.mismatch;
            ++(is_summary ? field.summary_mismatch : field.leaf_mismatch);
        }
    };
    const auto report = [](const char* name, std::int32_t uid, const std::string& actual,
                           const std::string& expected) {
        std::cout << "DIFF uid=" << uid << " field=" << name << " recalculated=" << actual
                  << " stored=" << expected << '\n';
    };

    // One attribution row per mismatching field, in the same task/field order as
    // the DIFF lines above.  classify() is a pure function; the stored row is
    // used only for its Summary fallback and to format the stored value.
    Attribution attribution;
    const auto attribute = [&attribution](const core::plan::PlanTaskResult& ours,
                                          const core::plan::StoredRow& stored_row,
                                          core::plan::AttribField field,
                                          const std::string& ours_text,
                                          const std::string& stored_text) {
        attribution.rows.push_back(Mismatch{ours.uid, field,
                                            core::plan::classify(ours, stored_row, field),
                                            ours_text, stored_text});
    };

    std::size_t missing = 0;
    std::size_t summary_rows = 0;
    std::size_t actual_starts = 0;
    std::size_t actual_finishes = 0;
    // Wave 25 (W25-11/S13): the scheduled Summary flag vs the stored one.  This
    // was mis-renamed flag10_disagreements by wave 24's blanket rename; it has
    // nothing to do with Task.Flag10.
    std::size_t summary_disagreements = 0;
    for (const core::plan::PlanTaskResult& task : result.tasks) {
        const auto found = by_uid.find(task.uid);
        if (found == by_uid.end()) {
            ++missing;
            if (missing <= 5) {
                std::cout << "DIFF uid=" << task.uid << " field=missing"
                          << " recalculated=present stored=absent\n";
            }
            continue;
        }
        const core::plan::StoredRow& expected = found->second;
        const bool is_summary = expected.summary;
        if (is_summary) {
            ++summary_rows;
        }
        if (is_summary != task.summary) {
            ++summary_disagreements;
        }
        if (task.start_is_actual) {
            ++actual_starts;
        }
        if (task.finish_is_actual) {
            ++actual_finishes;
        }

        // The four comparisons are core::plan::field_matches (the same exact
        // equality the table has always used); a mismatch is additionally
        // classified by core::plan::classify.
        const bool start_ok = core::plan::field_matches(task, expected,
                                                        core::plan::AttribField::start);
        record("start", is_summary, start_ok);
        if (!start_ok) {
            const std::string ours = core::plan::format_date(task.start);
            const std::string stored_text = core::plan::format_date(expected.start);
            report("start", task.uid, ours, stored_text);
            attribute(task, expected, core::plan::AttribField::start, ours, stored_text);
        }
        const bool finish_ok = core::plan::field_matches(task, expected,
                                                         core::plan::AttribField::finish);
        record("finish", is_summary, finish_ok);
        if (!finish_ok) {
            const std::string ours = core::plan::format_date(task.finish);
            const std::string stored_text = core::plan::format_date(expected.finish);
            report("finish", task.uid, ours, stored_text);
            attribute(task, expected, core::plan::AttribField::finish, ours, stored_text);
        }

        // mpp::Task exposes Project's stored TotalSlack (row +74) and the
        // derived Critical flag, so no raw-row decode is needed here.
        const bool slack_ok = core::plan::field_matches(task, expected,
                                                        core::plan::AttribField::slack);
        record("slack", is_summary, slack_ok);
        if (!slack_ok) {
            const std::string ours = core::plan::format_slack(task.total_slack);
            const std::string stored_text = core::plan::format_slack(expected.total_slack);
            report("slack", task.uid, ours, stored_text);
            attribute(task, expected, core::plan::AttribField::slack, ours, stored_text);
        }
        const bool critical_ok = core::plan::field_matches(task, expected,
                                                           core::plan::AttribField::critical);
        record("critical", is_summary, critical_ok);
        if (!critical_ok) {
            const std::string ours = yes_no(task.critical);
            const std::string stored_text = yes_no(expected.critical);
            report("critical", task.uid, ours, stored_text);
            attribute(task, expected, core::plan::AttribField::critical, ours, stored_text);
        }
    }

    // The attribution counts come from the same rows/fields the tables above
    // counted, so the two views cannot drift: `fields` is in AttribField order.
    static_assert(core::plan::kAttribFieldCount == 4,
                  "the --compare field table and AttribField must stay in step");
    for (std::size_t field = 0; field < core::plan::kAttribFieldCount; ++field) {
        attribution.matches[field] = fields[field].match;
        attribution.mismatches[field] = fields[field].mismatch;
    }
    for (const Mismatch& row : attribution.rows) {
        ++attribution.family_field[static_cast<std::size_t>(row.family)]
                                  [static_cast<std::size_t>(row.field)];
    }

    const auto print_table = [](const char* title, const std::vector<FieldCount>& fields,
                                bool leaves_only) {
        std::cout << title << '\n';
        std::cout << "  field      match  mismatch  total\n";
        std::size_t total_match = 0;
        std::size_t total_mismatch = 0;
        for (const FieldCount& field : fields) {
            const std::string name(field.name);
            const std::size_t match = leaves_only ? field.leaf_match : field.match;
            const std::size_t mismatch = leaves_only ? field.leaf_mismatch : field.mismatch;
            std::cout << "  " << name
                      << std::string(10 - std::min<std::size_t>(name.size(), 10), ' ')
                      << match << std::string(7, ' ') << mismatch << std::string(10, ' ')
                      << (match + mismatch) << '\n';
            total_match += match;
            total_mismatch += mismatch;
        }
        std::cout << "  overall    " << total_match << " / " << (total_match + total_mismatch)
                  << " fields match\n";
    };

    std::cout << "\ncompare: recalculated " << path << " vs stored " << compare_path << '\n';
    std::cout << "  tasks " << result.tasks.size() << ", stored rows " << stored.tasks.size()
              << ", missing " << missing << ", summaries " << summary_rows
              << ", rows with an actual start " << actual_starts << ", with an actual finish "
              << actual_finishes << '\n';
    if (summary_disagreements != 0) {
        std::cout << "  note: " << summary_disagreements
                  << " rows disagree with the stored Summary flag\n";
    }
    print_table("  all rows:", fields, false);
    std::cout << '\n';
    print_table("  leaves only:", fields, true);
    std::cout << '\n';
    {
        std::vector<FieldCount> summaries = fields;
        for (FieldCount& field : summaries) {
            field.match = field.summary_match;
            field.mismatch = field.summary_mismatch;
        }
        print_table("  summaries only:", summaries, false);
    }

    // W28: the same mismatches, attributed.  Always printed after the
    // unchanged tables so an old run and a new run diff line for line.
    print_attribution(attribution);
    if (!attrib_json_path.empty() &&
        !write_attribution_json(attrib_json_path, path, compare_path, attribution)) {
        return 1;
    }
    std::cout << std::flush;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string path;
    std::string compare_path;
    std::string tsv_path;
    std::string attrib_json_path;
    bool dump_fields = false;
    bool dump_links = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            print_usage(std::cout);
            return 0;
        }
        if (argument == "--compare") {
            if (i + 1 >= argc) {
                std::cerr << "recalc: --compare needs a file\n";
                return 2;
            }
            compare_path = argv[++i];
        } else if (argument == "--tsv") {
            if (i + 1 >= argc) {
                std::cerr << "recalc: --tsv needs a file\n";
                return 2;
            }
            tsv_path = argv[++i];
        } else if (argument == "--attrib-json") {
            if (i + 1 >= argc) {
                std::cerr << "recalc: --attrib-json needs a file\n";
                return 2;
            }
            attrib_json_path = argv[++i];
        } else if (argument == "--fields") {
            dump_fields = true;
        } else if (argument == "--links") {
            dump_links = true;
        } else if (!argument.empty() && argument[0] == '-') {
            std::cerr << "recalc: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return 2;
        } else if (path.empty()) {
            path = argument;
        } else {
            std::cerr << "recalc: more than one file given\n";
            print_usage(std::cerr);
            return 2;
        }
    }
    if (path.empty()) {
        print_usage(std::cerr);
        return 2;
    }
    if (!attrib_json_path.empty() && compare_path.empty()) {
        std::cerr << "recalc: --attrib-json needs --compare\n";
        return 2;
    }
    try {
        return run(path, compare_path, tsv_path, attrib_json_path, dump_fields, dump_links);
    } catch (const std::exception& error) {
        std::cerr << "recalc: " << error.what() << '\n';
        return 1;
    }
}
