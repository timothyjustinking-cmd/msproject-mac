/*
 * reader_dump -- print the tables of an .mpp file as TSV on stdout.
 *
 * DEFAULT OUTPUT is unchanged from reader v0: one row per live task, laid out
 * exactly like tests/data/project_a_golden.tsv so it can be diffed against MS
 * Project's own export:
 *
 *   uid  id  name  duration_min  start  finish  priority  pct
 *
 * `duration_min` is minutes (MS Project's unit; the file stores tenths of a
 * minute, i.e. 6-second ticks).  Dates are ISO-8601 with a +00:00 suffix and
 * the stored wall-clock time -- no timezone conversion.  A tab or newline in a
 * task name is written as a space so the output stays one record per line.
 *
 * --resources, --links and --assignments add the other three tables; when more
 * than one table is printed, each is preceded by a "# table <name>" line so the
 * records stay distinguishable:
 *
 *   resources:    uid id name initials max_units_pct max_units type
 *   links:        uid pred_uid succ_uid type lag_ticks lag_days lag_units
 *                 pred_known succ_known
 *   assignments:  uid task_uid resource_uid units_pct work_min work_days
 *                 start finish stop
 *
 * --outline appends the outline/status columns to whichever task table is being
 * printed, so the default eight columns (which are diffed against MS Project's
 * own export) stay byte-identical:
 *
 *   level outline summary milestone critical slack_ticks
 *
 * `milestone` is the stored FixedMeta flag when the file carries one and the
 * duration == 0 fallback otherwise (see core/mpp/reader.h); PROJECT_B's 10-day
 * milestone is 1 here.  `slack_ticks` is TotalSlack at +190 (W30: +74 is
 * FreeSlack and +186 StartSlack); --slack prints all three.
 *
 * --slack appends the three stored slack words, in 6-second ticks (4800 = one
 * working day):
 *
 *   free_slack_ticks start_slack_ticks total_slack_ticks
 *
 * --anchor appends the two W31 fixed-row fields, also without touching the
 * default eight:
 *
 *   start_anchor calendar_uid
 *
 * start_anchor is row +174: the bound the Start was aligned from (a manual
 * task's manual Start, an auto task's predecessor-finish bound), ISO-8601 or
 * empty when the file stores none.  It is NOT `start` (+96).  calendar_uid is
 * row +178: -1 means the task uses the project calendar (holiday1.mpp's tasks
 * store 3).
 *
 * --sched appends the CPM input columns (also without touching the default
 * eight, so the golden diff stays valid).  Dates are ISO-8601, empty when the
 * file stores NA; actual_duration_min is minutes like duration_min:
 *
 *   constraint_type constraint_date deadline actual_start actual_finish
 *   actual_duration_min
 *
 * constraint_type is Project's PjConstraint 0=ASAP, 1=ALAP, 2=MSO, 3=MFO,
 * 4=SNET, 5=SNLT, 6=FNET, 7=FNLT.  The deadline is the fixed-row field at
 * +182, not a VarMeta value.  The actual dates are an inference (see
 * core/mpp/reader.h); actual_duration_ticks (+80) is documented.
 *
 * --project prints row 0, the project row, as its own table:
 *
 *   uid id title start finish duration_min pct
 *
 * Links' lag is in 6-second ticks (4800 = one working day) and is signed, so a
 * negative lag is a lead.  When --links is printed it is preceded by a
 * "# links N (live L, dead-endpoint D)" comment line: a row can be live (a
 * FixedMeta entry names it) yet have an endpoint that is a deleted task.
 * PROJECT_B is 72/70/2 -- Project exports and schedules only the 70.  An
 * assignment whose resource uid is the "no resource" sentinel prints 0 in
 * resource_uid; see mpp::Assignment::has_resource().  The assignment table's
 * start/finish are +52/+56 and stop is +60 (a within-span date -- Project's
 * Stop; inference).
 *
 * Exit codes: 0 success, 1 the file could not be read, 2 bad usage.
 */

#include <cstring>
#include <iostream>
#include <string>

#include "reader.h"

namespace {

void print_usage(std::ostream& out) {
    out << "usage: reader_dump [options] <file.mpp>\n"
        << "\n"
        << "Default output is one TSV row per live task:\n"
        << "  uid, id, name, duration_min, start, finish, priority, pct\n"
        << "\n"
        << "table options (several may be combined):\n"
        << "  --tasks         the default table, explicitly\n"
        << "  --project       uid, id, title, start, finish, duration_min, pct\n"
        << "                  (row 0, the project summary row)\n"
        << "  --resources     uid, id, name, initials, max_units_pct, max_units, type\n"
        << "  --links         uid, pred_uid, succ_uid, type, lag_ticks, lag_days,\n"
        << "                  lag_units, pred_known, succ_known\n"
        << "  --assignments   uid, task_uid, resource_uid, units_pct, work_min,\n"
        << "                  work_days, start, finish, stop\n"
        << "  --all           all four data tables (not the project row)\n"
        << "\n"
        << "other options:\n"
        << "  --outline       append level, outline, summary, milestone, critical,\n"
        << "                  slack_ticks to the task table\n"
        << "  --anchor        append start_anchor (+174, the pre-alignment bound)\n"
        << "                  and calendar_uid (+178, -1 = project calendar)\n"
        << "  --slack         append free_slack_ticks (+74), start_slack_ticks\n"
        << "                  (+186) and total_slack_ticks (+190)\n"
        << "  --sched         append constraint_type, constraint_date, deadline,\n"
        << "                  actual_start, actual_finish, actual_duration_min to\n"
        << "                  the task table\n"
        << "  --no-header     omit the column header line\n"
        << "  --skipped       write skipped (dead) rows to stderr\n"
        << "  --warnings      write reader warnings to stderr\n"
        << "  -h, --help      show this text\n";
}

std::string sanitise(std::string text) {
    for (char& ch : text) {
        if (ch == '\t' || ch == '\n' || ch == '\r') {
            ch = ' ';
        }
    }
    return text;
}

const char* resource_type_name(mpp::ResourceType type) {
    switch (type) {
        case mpp::ResourceType::work:
            return "Work";
        case mpp::ResourceType::material:
            return "Material";
        case mpp::ResourceType::cost:
            return "Cost";
        default:
            return "?";
    }
}

void print_table_separator(const char* name) {
    std::cout << "# table " << name << '\n';
}

void print_tasks(const mpp::Document& document, bool header, bool with_outline,
                 bool with_anchor, bool want_slack, bool with_sched) {
    if (header) {
        std::cout << "uid\tid\tname\tduration_min\tstart\tfinish\tpriority\tpct";
        if (with_outline) {
            std::cout << "\tlevel\toutline\tsummary\tmilestone\tcritical\tslack_ticks";
        }
        if (with_anchor) {
            std::cout << "\tstart_anchor\tcalendar_uid";
        }
        if (want_slack) {
            std::cout << "\tfree_slack_ticks\tstart_slack_ticks\ttotal_slack_ticks";
        }
        if (with_sched) {
            std::cout << "\tconstraint_type\tconstraint_date\tdeadline\tactual_start"
                         "\tactual_finish\tactual_duration_min";
        }
        std::cout << '\n';
    }
    for (const mpp::Task& task : document.tasks) {
        std::cout << task.uid << '\t' << task.row_id << '\t' << sanitise(task.name)
                  << '\t' << task.duration_minutes() << '\t'
                  << mpp::format_date_iso(task.start) << '\t'
                  << mpp::format_date_iso(task.finish) << '\t'
                  << static_cast<unsigned>(task.priority) << '\t'
                  << static_cast<unsigned>(task.percent_complete);
        if (with_outline) {
            std::cout << '\t' << task.outline_level << '\t' << task.outline_number
                      << '\t' << (task.summary ? 1 : 0) << '\t'
                      << (task.is_milestone() ? 1 : 0) << '\t'
                      << (task.critical ? 1 : 0) << '\t' << task.total_slack_ticks;
        }
        if (with_anchor) {
            std::cout << '\t' << mpp::format_date_iso(task.start_anchor) << '\t'
                      << task.calendar_uid;
        }
        if (want_slack) {
            std::cout << '\t' << task.free_slack_ticks << '\t' << task.start_slack_ticks
                      << '\t' << task.total_slack_ticks;
        }
        if (with_sched) {
            std::cout << '\t' << static_cast<unsigned>(task.constraint_type) << '\t'
                      << mpp::format_date_iso(task.constraint_date) << '\t'
                      << mpp::format_date_iso(task.deadline) << '\t'
                      << mpp::format_date_iso(task.actual_start) << '\t'
                      << mpp::format_date_iso(task.actual_finish) << '\t'
                      << mpp::duration_minutes(task.actual_duration_ticks);
        }
        std::cout << '\n';
    }
}

void print_project(const mpp::Document& document, bool header) {
    if (header) {
        std::cout << "uid\tid\ttitle\tstart\tfinish\tduration_min\tpct\n";
    }
    const mpp::ProjectRow& project = document.project;
    std::cout << project.uid << '\t' << project.row_id << '\t' << sanitise(project.title)
              << '\t' << mpp::format_date_iso(project.start) << '\t'
              << mpp::format_date_iso(project.finish) << '\t'
              << mpp::duration_minutes(project.duration_ticks) << '\t'
              << static_cast<unsigned>(project.percent_complete) << '\n';
}

void print_resources(const mpp::Document& document, bool header) {
    if (header) {
        std::cout << "uid\tid\tname\tinitials\tmax_units_pct\tmax_units\ttype\n";
    }
    for (const mpp::Resource& resource : document.resources) {
        std::cout << resource.uid << '\t' << resource.row_id << '\t'
                  << sanitise(resource.name) << '\t' << sanitise(resource.initials)
                  << '\t' << resource.max_units_percent() << '\t'
                  << resource.max_units() << '\t'
                  << resource_type_name(resource.type) << '\n';
    }
}

void print_links(const mpp::Document& document, bool header) {
    if (header) {
        std::cout << "# links " << document.links.size() << " (live "
                  << document.link_count_with_live_endpoints() << ", dead-endpoint "
                  << document.link_count_with_dead_endpoints() << ")\n";
        std::cout << "uid\tpred_uid\tsucc_uid\ttype\tlag_ticks\tlag_days\tlag_units"
                     "\tpred_known\tsucc_known\n";
    }
    for (const mpp::Link& link : document.links) {
        std::cout << link.uid << '\t' << link.predecessor_uid << '\t'
                  << link.successor_uid << '\t' << link.type_name() << '\t'
                  << link.lag_ticks << '\t' << link.lag_days() << '\t'
                  << static_cast<unsigned>(link.lag_units) << '\t'
                  << (link.predecessor_known ? 1 : 0) << '\t'
                  << (link.successor_known ? 1 : 0) << '\n';
    }
}

void print_assignments(const mpp::Document& document, bool header) {
    if (header) {
        std::cout << "uid\ttask_uid\tresource_uid\tunits_pct\twork_min\twork_days"
                     "\tstart\tfinish\tresume\n";
    }
    for (const mpp::Assignment& assignment : document.assignments) {
        std::cout << assignment.uid << '\t' << assignment.task_uid << '\t'
                  << assignment.resource_uid() << '\t' << assignment.units_percent()
                  << '\t' << assignment.work_minutes() << '\t'
                  << assignment.work_days() << '\t'
                  << mpp::format_date_iso(assignment.start) << '\t'
                  << mpp::format_date_iso(assignment.finish) << '\t'
                  << mpp::format_date_iso(assignment.resume) << '\n';
    }
}

void print_skipped(const mpp::Document& document) {
    const auto print = [](const char* table, const std::vector<mpp::SkippedRow>& rows) {
        for (const mpp::SkippedRow& skipped : rows) {
            std::cerr << "skipped " << table << " row " << skipped.row << " (uid "
                      << skipped.uid << "): " << skipped.reason;
            if (!skipped.orphan_name.empty()) {
                std::cerr << "; orphaned name block: '" << skipped.orphan_name << "'";
            }
            std::cerr << '\n';
        }
    };
    print("task", document.skipped_rows);
    print("resource", document.skipped_resources);
    print("link", document.skipped_links);
    print("assignment", document.skipped_assignments);
}

}  // namespace

int main(int argc, char** argv) {
    bool print_header = true;
    bool print_warnings = false;
    bool print_skipped_rows = false;
    bool want_outline = false;
    bool want_anchor = false;
    bool want_slack = false;
    bool want_sched = false;
    bool want_tasks = false;
    bool want_project = false;
    bool want_resources = false;
    bool want_links = false;
    bool want_assignments = false;
    std::string path;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            print_usage(std::cout);
            return 0;
        }
        if (argument == "--no-header") {
            print_header = false;
        } else if (argument == "--outline") {
            want_outline = true;
        } else if (argument == "--anchor") {
            want_anchor = true;
        } else if (argument == "--slack") {
            want_slack = true;
        } else if (argument == "--sched") {
            want_sched = true;
        } else if (argument == "--warnings") {
            print_warnings = true;
        } else if (argument == "--skipped") {
            print_skipped_rows = true;
        } else if (argument == "--tasks") {
            want_tasks = true;
        } else if (argument == "--project") {
            want_project = true;
        } else if (argument == "--resources") {
            want_resources = true;
        } else if (argument == "--links") {
            want_links = true;
        } else if (argument == "--assignments") {
            want_assignments = true;
        } else if (argument == "--all") {
            want_tasks = true;
            want_resources = true;
            want_links = true;
            want_assignments = true;
        } else if (!argument.empty() && argument[0] == '-') {
            std::cerr << "reader_dump: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return 2;
        } else if (path.empty()) {
            path = argument;
        } else {
            std::cerr << "reader_dump: more than one file given\n";
            print_usage(std::cerr);
            return 2;
        }
    }
    if (path.empty()) {
        print_usage(std::cerr);
        return 2;
    }
    if (!want_tasks && !want_resources && !want_links && !want_assignments &&
        !want_project) {
        want_tasks = true;
    }

    mpp::Document document;
    try {
        document = mpp::load_file(path);
    } catch (const std::exception& error) {
        std::cerr << "reader_dump: " << error.what() << '\n';
        return 1;
    }

    const int table_count = static_cast<int>(want_tasks) +
                            static_cast<int>(want_project) +
                            static_cast<int>(want_resources) +
                            static_cast<int>(want_links) +
                            static_cast<int>(want_assignments);
    const bool separators = table_count > 1;
    if (separators) {
        bool first = true;
        const auto begin = [&first](const char* name) {
            if (!first) {
                std::cout << '\n';
            }
            first = false;
            print_table_separator(name);
        };
        if (want_tasks) {
            begin("tasks");
            print_tasks(document, print_header, want_outline, want_anchor, want_slack,
                        want_sched);
        }
        if (want_project) {
            begin("project");
            print_project(document, print_header);
        }
        if (want_resources) {
            begin("resources");
            print_resources(document, print_header);
        }
        if (want_links) {
            begin("links");
            print_links(document, print_header);
        }
        if (want_assignments) {
            begin("assignments");
            print_assignments(document, print_header);
        }
    } else {
        if (want_tasks) {
            print_tasks(document, print_header, want_outline, want_anchor, want_slack,
                        want_sched);
        } else if (want_project) {
            print_project(document, print_header);
        } else if (want_resources) {
            print_resources(document, print_header);
        } else if (want_links) {
            print_links(document, print_header);
        } else {
            print_assignments(document, print_header);
        }
    }

    if (!std::cout) {
        std::cerr << "reader_dump: write error on stdout\n";
        return 1;
    }
    if (print_skipped_rows) {
        print_skipped(document);
    }
    if (print_warnings) {
        for (const std::string& warning : document.warnings) {
            std::cerr << "warning: " << warning << '\n';
        }
    }
    return 0;
}
