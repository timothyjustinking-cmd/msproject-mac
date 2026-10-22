/*
 * mspdi_cmp -- compare an .mpp file with an MSPDI export of the same project.
 *
 *   mspdi_cmp <file.mpp> <file.xml> [--json <out.json>] [--strict]
 *
 * Loads the .mpp through core_mpp (the binary reader) and the .xml through
 * core_io (the MSPDI importer), then compares, per task UID: start, finish,
 * duration, milestone, summary, constraint type/date, deadline, actual
 * start/finish; per link: the (predecessor uid, successor uid, type, lag ticks)
 * multiset; per resource: the UID set; and per assignment: task/resource UIDs
 * plus Units and Work where the export carries them.
 *
 * Two classes of difference are reported but do not fail the run, each with a
 * named reason and a separate count/category:
 *   - DEAD-ENDPOINT links and assignments: an endpoint UID is not a live task
 *     in either model (a deleted task slot).  Project does not schedule or
 *     export such rows, so no export can ever match them.
 *   - MILESTONE notes: the reader derives Milestone from duration == 0, while
 *     Project also marks a non-zero-duration task.  That is a core/mpp
 *     decision, not an MSPDI parity failure.
 * `--strict` promotes the milestone notes to failures.
 *
 * Exit code 0 means every compared field, link, resource and assignment
 * matches; 1 means a mismatch or an unreadable file; 2 means bad usage.
 *
 * It never prints a task name (client data rule, docs/coordination.md): the
 * model's names are not read, and the table and JSON carry uids, field names,
 * dates and numbers only.
 *
 * WHY A SEPARATE TOOL: the coordinator's oracle export is the ground truth for
 * links/constraints/actuals, and a reader claim verified through the reader
 * again would not be independent.  This is the other code path.
 */

#include <exception>
#include <fstream>
#include <iostream>
#include <string>

#include "crosscheck.h"
#include "mspdi.h"
#include "reader.h"

namespace {

void print_usage(std::ostream& out) {
    out << "usage: mspdi_cmp <file.mpp> <file.xml> [--json <out.json>] [--strict]\n"
        << "\n"
        << "Compares the .mpp reader's task/link/resource/assignment model with an\n"
        << "MSPDI export.\n"
        << "  fields: start, finish, duration, milestone, summary, constraint_type,\n"
        << "          constraint_date, deadline, actual_start, actual_finish\n"
        << "  links:  (predecessor uid, successor uid, type, lag_ticks) multiset,\n"
        << "          live endpoints only\n"
        << "  resources: uid set (Project's UID 0 placeholder excluded)\n"
        << "  assignments: task/resource uids, Units and Work where exported,\n"
        << "               live task endpoints only\n"
        << "\n"
        << "options:\n"
        << "  --json <path>   also write the result as JSON (uids and counts only)\n"
        << "  --strict        treat milestone notes as failures\n"
        << "  -h, --help      show this text\n"
        << "\n"
        << "exit: 0 every compared field and every live uid/link set matches,\n"
        << "      1 mismatch or unreadable input, 2 bad usage\n";
}

bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    return static_cast<bool>(stream);
}

}  // namespace

int main(int argc, char** argv) {
    std::string mpp_path;
    std::string xml_path;
    std::string json_path;
    bool want_help = false;
    bool strict = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            want_help = true;
        } else if (argument == "--strict") {
            strict = true;
        } else if (argument == "--json") {
            if (i + 1 >= argc) {
                std::cerr << "mspdi_cmp: --json needs a path\n";
                return 2;
            }
            json_path = argv[++i];
        } else if (!argument.empty() && argument[0] == '-') {
            std::cerr << "mspdi_cmp: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return 2;
        } else if (mpp_path.empty()) {
            mpp_path = argument;
        } else if (xml_path.empty()) {
            xml_path = argument;
        } else {
            std::cerr << "mspdi_cmp: more than two files given\n";
            print_usage(std::cerr);
            return 2;
        }
    }
    if (want_help) {
        print_usage(std::cout);
        return 0;
    }
    if (mpp_path.empty() || xml_path.empty()) {
        print_usage(std::cerr);
        return 2;
    }

    mpp::Document reader;
    try {
        reader = mpp::load_file(mpp_path);
    } catch (const std::exception& error) {
        std::cerr << "mspdi_cmp: cannot read \"" << mpp_path << "\": " << error.what() << '\n';
        return 1;
    }

    core::io::MspdiDocument xml;
    try {
        xml = core::io::import_mspdi_file(xml_path);
    } catch (const std::exception& error) {
        std::cerr << "mspdi_cmp: cannot read \"" << xml_path << "\": " << error.what() << '\n';
        return 1;
    }

    const mspdi_cmp::Result result = mspdi_cmp::compare(reader, xml);

    std::cout << "mspdi_cmp\n  mpp: " << mpp_path << "\n  xml: " << xml_path << '\n';
    std::cout << mspdi_cmp::format_report(result);

    if (!json_path.empty()) {
        if (!write_text_file(json_path, mspdi_cmp::format_json(result, mpp_path, xml_path))) {
            std::cerr << "mspdi_cmp: cannot write \"" << json_path << "\"\n";
            return 1;
        }
    }
    if (strict && !result.strictly_ok()) {
        return 1;
    }
    return result.ok() ? 0 : 1;
}
