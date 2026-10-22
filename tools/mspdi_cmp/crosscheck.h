/*
 * crosscheck.h -- compare the .mpp reader's model with an MSPDI export.
 *
 * WHY THIS EXISTS.  Two independent code paths read the same project:
 * `core/mpp` parses the binary .mpp, `core/io` parses Project's own MSPDI
 * export (`FileSaveAs FormatID="MSProject.xml"`).  When a reader claim is
 * checked only against a second reader run, a parser bug can hide behind the
 * same bug twice.  This module compares the two paths so the coordinator can
 * verify a reader change against Project's export instead of against the
 * reader's own output.
 *
 * WHAT IS COMPARED
 *
 *   Tasks, per UID present in both models:
 *     start, finish          packed-date 32 bits vs the XML Date
 *     duration               6-second ticks
 *     milestone              bool -- see the NOTE below
 *     summary                bool (the reader derives it)
 *     constraint_type        int (PjConstraint 0..7)
 *     constraint_date        date
 *     deadline               date
 *     actual_start, actual_finish  date
 *
 *   Links, as a MULTISET of (predecessor uid, successor uid, type, lag_ticks):
 *     the .mpp link table can repeat a pair and the XML cannot, so a set would
 *     hide a duplicate.  Only LIVE links are compared; see "DEAD ENDPOINTS".
 *
 *   Resources, by UID: Project writes a placeholder resource with UID 0 (the
 *     "no resource" row), which the reader does not return, so it is excluded
 *     and reported explicitly.
 *
 *   Assignments, by UID: task UID and resource UID always; `Units` and `Work`
 *     when the export carries them.  Project writes `Units` as a FRACTION
 *     (1.0 == 100 %) while the reader's units() is already a percentage
 *     (100.0 == 100 %), so the comparison multiplies the XML value by 100 and
 *     reports percent.  The exporter's `Work` is the reader's work_ticks().
 *     The resource UID sentinel -65535 is normalised to "no resource" on both
 *     sides.  Only live assignments are compared; see "DEAD ENDPOINTS".
 *
 * DEAD ENDPOINTS (named exclusion, not a parity failure)
 *   A real .mpp can still hold link and assignment rows whose task slot was
 *   deleted (PROJECT_B: 2 links and 10 assignments).  The reader returns those
 *   rows; Project does not schedule or export them.  A link/assignment is
 *   therefore DEAD-ENDPOINT when an endpoint UID is not a live task in EITHER
 *   model (reader task UID or a non-project XML task UID).  Dead ones are
 *   counted and listed but excluded from the parity verdict, because no export
 *   can ever match them.  A link or assignment whose endpoint exists in one
 *   model only is NOT dead: the task UID difference is reported by the task
 *   comparison, and the row is compared normally.
 *
 * NOTE ON MILESTONE
 *   The reader reads the stored FixedMeta milestone bit when the row has one and
 *   falls back to duration == 0 otherwise (core/mpp/reader.h, "Milestone"), while
 *   a Project export carries the effective flag.  When they disagree the note
 *   says which path the reader took.  A Milestone difference is therefore
 *   reported as a NOTE, not a mismatch: it says the two models read the flag
 *   differently, which is a core/mpp concern (W31), not an MSPDI parity failure.
 *   The PROJECT_B count is 0 with the stored-bit reader (it was 2 while the reader
 *   was duration-only).  `ok()` ignores notes; `strictly_ok()` does not.
 *
 * DATES are compared as the reader's own encoding, `(day << 16) | secs/6` with
 * day 0 = 1983-12-31, so a match is a date32 match, not a formatted-string
 * match.  "Not set" (0xffffffff in the row, an absent XML element) is a value
 * of its own: it matches only another "not set".
 *
 * TASK NAMES ARE NEVER READ.  The model exposes names, this module does not
 * touch them, and the report/JSON carry uids, field names, dates and numbers
 * only.  That keeps its output safe to paste anywhere.
 */

#ifndef MSPDI_CMP_CROSSCHECK_H
#define MSPDI_CMP_CROSSCHECK_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mspdi.h"
#include "reader.h"

namespace mspdi_cmp {

/* The date32 the reader stores for a civil date/time, or kNoDate when the value
 * is absent. */
inline constexpr std::int64_t kNoDate = -1;

/* One link identity, the way both sides can express it.  type is Project's
 * PjRelationType: 0 = FF, 1 = FS, 2 = SF, 3 = SS. */
struct LinkRow {
    std::uint32_t predecessor_uid = 0;
    std::uint32_t successor_uid = 0;
    int type = 0;
    std::int64_t lag_ticks = 0;
};

bool operator==(const LinkRow& a, const LinkRow& b) noexcept;
bool operator<(const LinkRow& a, const LinkRow& b) noexcept;

/* One assignment identity, plus the values the comparison actually used.
 * resource_uid is normalised: 0 means "no resource" (the -65535 sentinel and
 * the reader's kNullResourceUid both fold to it). */
struct AssignmentRow {
    std::uint32_t uid = 0;
    std::uint32_t task_uid = 0;
    std::uint32_t resource_uid = 0;
    bool units_compared = false;
    double units = 0.0; /* percentage, 100.0 == 100 % (both sides normalised) */
    bool work_compared = false;
    std::int64_t work_ticks = 0;
    /* Assignment dates: the reader's +52/+56/+60 against the export's
     * <Start>/<Finish>/<Resume>.  kNoDate means "not set"; Start/Finish are
     * compared always (absent == kNoDate on both sides), Resume only when the
     * export carries the element. */
    std::int64_t start = kNoDate;
    std::int64_t finish = kNoDate;
    std::int64_t resume = kNoDate;
    bool resume_compared = false;
};

/* One resource's compared values: identity plus the two fields the MSPDI
 * export carries and the reader exposes.  max_units is a percentage on both
 * sides after normalisation (the export writes a fraction). */
struct ResourceRow {
    std::uint32_t uid = 0;
    int type = 0;          /* reader ResourceType as int */
    double max_units = 0;  /* percentage, 100.0 == 100 % */
};

/* One field whose two readings disagree.  The values are already formatted for
 * display: "(none)" for "not set". */
struct FieldMismatch {
    std::uint32_t uid = 0;
    std::string field;
    std::string reader_value;
    std::string xml_value;
};

struct Result {
    /* MSPDI's project summary task is UID 0.  The reader returns it as
     * Document::project, not as a task, so it is excluded from the task
     * comparison and reported here instead. */
    bool xml_has_project_row = false;
    std::uint32_t project_uid = 0;
    /* Project also writes a UID 0 placeholder resource (the "no resource" row);
     * the reader does not return it, so it is excluded and reported. */
    bool xml_has_placeholder_resource = false;

    std::size_t reader_tasks = 0;
    std::size_t xml_tasks = 0;      /* including the project row */
    std::size_t compared_tasks = 0; /* uids present in both, project row excluded */
    std::size_t compared_fields = 0;

    /* Live links: both endpoints are tasks the reader returns (or XML tasks).
     * Dead-endpoint rows are held separately and do not affect the verdict. */
    std::size_t reader_links = 0;
    std::size_t live_reader_links = 0;
    std::size_t xml_links = 0;
    std::size_t live_xml_links = 0;
    std::size_t matched_links = 0;

    std::size_t reader_resources = 0;
    std::size_t xml_resources = 0; /* including the UID 0 placeholder */
    std::size_t compared_resources = 0;
    std::size_t matched_resources = 0;
    std::size_t compared_resource_types = 0;
    std::size_t compared_resource_max_units = 0;

    std::size_t reader_assignments = 0;
    std::size_t live_reader_assignments = 0;
    std::size_t xml_assignments = 0;
    std::size_t live_xml_assignments = 0;
    std::size_t compared_assignments = 0;
    std::size_t matched_assignments = 0;
    std::size_t compared_assignment_units = 0;
    std::size_t compared_assignment_work = 0;
    std::size_t compared_assignment_dates = 0;   /* Start+Finish pairs */
    std::size_t compared_assignment_resume = 0;  /* exports carrying <Resume> */

    std::vector<std::uint32_t> reader_only_uids;  /* live in the .mpp, absent from XML */
    std::vector<std::uint32_t> xml_only_uids;     /* in XML, absent from the .mpp */
    std::vector<LinkRow> reader_only_links;
    std::vector<LinkRow> xml_only_links;
    std::vector<FieldMismatch> field_mismatches;

    /* Excluded, named: see "DEAD ENDPOINTS" above. */
    std::vector<LinkRow> dead_endpoint_links;
    std::vector<LinkRow> xml_dead_endpoint_links;
    std::vector<AssignmentRow> dead_endpoint_assignments;
    std::vector<AssignmentRow> xml_dead_endpoint_assignments;

    /* Reported, not a verdict: the reader derives Milestone from duration. */
    std::vector<FieldMismatch> milestone_differences;

    std::vector<std::uint32_t> reader_only_resource_uids;
    std::vector<std::uint32_t> xml_only_resource_uids;
    std::vector<FieldMismatch> resource_mismatches;
    std::vector<AssignmentRow> reader_only_assignments;
    std::vector<AssignmentRow> xml_only_assignments;
    std::vector<FieldMismatch> assignment_mismatches;

    /* Setup problems (a descriptor field name that no longer resolves, a field
     * whose XML value has the wrong kind).  Any problem means the comparison is
     * not trustworthy, so ok() is false. */
    std::vector<std::string> problems;

    std::size_t link_differences() const {
        return reader_only_links.size() + xml_only_links.size();
    }
    std::size_t uid_differences() const {
        return reader_only_uids.size() + xml_only_uids.size();
    }
    std::size_t resource_differences() const {
        return reader_only_resource_uids.size() + xml_only_resource_uids.size() +
               resource_mismatches.size();
    }
    std::size_t assignment_differences() const {
        return reader_only_assignments.size() + xml_only_assignments.size() +
               assignment_mismatches.size();
    }
    std::size_t note_count() const { return milestone_differences.size(); }
    std::size_t mismatch_count() const {
        return uid_differences() + link_differences() + field_mismatches.size() +
               resource_differences() + assignment_differences() + problems.size();
    }
    bool ok() const { return mismatch_count() == 0; }
    /* `ok()` plus no milestone notes.  mspdi_cmp --strict uses this. */
    bool strictly_ok() const { return ok() && note_count() == 0; }
};

/* Compares the two loaded models.  Pure: it reads them and returns data. */
Result compare(const mpp::Document& reader, const core::io::MspdiDocument& xml);

/* The compact human-readable form: differences, the named exclusions and notes,
 * then counts. */
std::string format_report(const Result& result);

/* The same data as JSON (uids, field names, dates and numbers only). */
std::string format_json(const Result& result, const std::string& mpp_path,
                        const std::string& xml_path);

/* The date32 the reader stores for a civil date/time, or kNoDate when the value
 * is absent.  Exposed for the test that builds a synthetic pair. */
std::int64_t date32_of(const core::view::DateValue& date);

}  // namespace mspdi_cmp

#endif  // MSPDI_CMP_CROSSCHECK_H
