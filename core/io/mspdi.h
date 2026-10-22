/*
 * mspdi.h -- Microsoft Project XML (MSPDI) import.
 *
 * MSPDI is the interchange path that works before the .mpp writer exists, and
 * it lands in exactly the same model as the reader and the views:
 * core::view::RecordSet, keyed by PjField id.  The schema is public and stable,
 * and tests use hand-written minimal project files rather than a real export.
 *
 * WHAT IS MAPPED
 *   Project  header fields (name, title, start/finish, calendar defaults,
 *            minutes/day/week, days/month) in MspdiProject.  The descriptor
 *            table's nine Project rows have no recovered property names, so
 *            project-level values cannot be field-mapped yet; they live in the
 *            header struct and spec/mspdi.md records the gap.
 *   Tasks    the curated Task field table at the top of mspdi.cpp.  UID becomes
 *            the RecordSet UniqueID; ID, Name, dates, durations, work, cost,
 *            percent complete, flags, notes, custom Text1/Number1/Cost1/Flag1/
 *            Date1 are mapped to their PjField ids through the descriptor
 *            table.  PredecessorLink entries are preserved structurally.
 *   Resources and Assignments use the same approach.
 *
 * WHAT IS NOT MAPPED (warned once per element name, never silently dropped)
 *   * any element with no named descriptor field (e.g. DurationFormat,
 *     CalendarUID, EmailAddress);
 *   * derived display fields Predecessors and ResourceNames: the links and
 *     assignments are kept structurally, but formatting them into Project's
 *     display text needs the scheduling layer and is not guessed here.
 *
 * TOLERANCE.  A missing optional element is simply absent (Value::Null).  A
 * malformed value becomes a warning and that one value is skipped; a malformed
 * document (bad XML, wrong root) throws MspdiError.  Every warning names the
 * record and element, e.g. "Task[UID=2]/PercentComplete: expected integer,
 * got \"x\"".
 */

#ifndef CORE_IO_MSPDI_H
#define CORE_IO_MSPDI_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "record_set.h"
#include "value.h"

namespace core::io {

class MspdiError : public std::runtime_error {
  public:
    explicit MspdiError(const std::string& message) : std::runtime_error(message) {}
};

struct MspdiProject {
    std::string name;
    std::string title;
    core::view::Value start;   // Date when present
    core::view::Value finish;  // Date when present
    bool schedule_from_start = true;
    bool has_schedule_from_start = false;
    int minutes_per_day = -1;
    int minutes_per_week = -1;
    int days_per_month = -1;
    int calendar_uid = -1;
    std::string default_start_time;   // "HH:MM:SS", as written
    std::string default_finish_time;
};

/* One MSPDI <Assignment> identity triple. */
struct MspdiAssignmentRef {
    std::uint32_t uid = 0;
    std::uint32_t task_uid = 0;
    /* Raw MSPDI <ResourceUID>.  Project's export writes -65535 ("no resource"),
     * which is the two's-complement 0xFFFF0001 -- the same sentinel the .mpp
     * reader calls kNullResourceUid.  The sentinel is preserved, not folded to
     * 0, so callers can tell "the element said no resource" from a missing
     * element.  Real resource UIDs start at 1. */
    std::uint32_t resource_uid = 0;
};

/* One MSPDI <PredecessorLink>, attached to its successor Task. */
struct PredecessorLink {
    std::uint32_t predecessor_uid = 0;
    std::uint32_t successor_uid = 0;
    int type = 1;                 /* raw MSPDI Type value; not interpreted here */
    /* LinkLag in 6-second ticks.  Project's export writes the Project-XML
     * integer form (tenths of a minute), which IS the tick count, so the value
     * is used unchanged; the hand-written sample's ISO "PT8H0M0S" form parses
     * to the same ticks.  Evidence: corpus/probe/project_a-export.xml's 88 links hold
     * 0/96000/139200/240000, matching the .mpp link rows' +14 i32 exactly. */
    std::int64_t lag_ticks = 0;
    int lag_format = 0;           /* raw MSPDI LagFormat value */
};

struct MspdiDocument {
    MspdiProject project;
    core::view::RecordSet tasks{core::model::RecordType::Task};
    core::view::RecordSet resources{core::model::RecordType::Resource};
    core::view::RecordSet assignments{core::model::RecordType::Assignment};
    std::vector<PredecessorLink> links;
    /* Assignment UID -> TaskUID/ResourceUID.  Those two ids have no named
     * Assignment descriptor fields, so they cannot live in the RecordSet; the
     * usage views join on them.  Same order as assignments rows. */
    std::vector<MspdiAssignmentRef> assignment_refs;
    std::vector<std::string> warnings;  // in document order, deduplicated per element
};

struct MspdiLoadOptions {
    bool import_resources = true;
    bool import_assignments = true;
};

/* ---------------------------------------------------------------------------
 * LinkLag spelling (canonical choice)
 *
 * Project's own export writes <LinkLag> as a DECIMAL integer in tenths of a
 * minute.  One tenth of a minute is exactly the reader's 6-second tick, so the
 * integer is used unchanged.  Evidence: corpus/probe/project_a-export.xml's 88 links
 * hold 0 / 96000 / 139200 / 240000 and those are byte-identical to the .mpp
 * link rows' signed i32 lag at +14 (link1.mpp also stores -4800 for a lead).
 *
 * The committed hand-written sample tests/io_sample_tasks.xml instead spells
 * the same quantity as an ISO duration, "PT8H0M0S".  Both spellings are
 * accepted on import; DECIMAL is the canonical wire form and the one the writer
 * emits.  That Project's importer accepts decimal is shown by its own export;
 * that it accepts ISO is unverified (no oracle FileOpen of writer output), so
 * ISO survives only as a legacy import spelling.
 * ------------------------------------------------------------------------- */
enum class LinkLagSpelling : std::uint8_t { none, decimal, iso };

/* Parses either spelling into 6-second ticks.  Decimal is tried first, so a
 * bare number can never be mistaken for a duration.  Returns `none` and leaves
 * `ticks` untouched when the text is neither. */
LinkLagSpelling parse_link_lag(std::string_view text, std::int64_t& ticks);

/* Canonical (Project export) form: tenths of a minute, no unit suffix. */
std::string format_link_lag_decimal(std::int64_t ticks);

/* Legacy sample form: "PT8H0M0S", "-PT1H0M0S", "PT0H0M0S".  Kept so both
 * spellings are testable and an ISO-only consumer still has a helper; the
 * writer does not use it. */
std::string format_link_lag_iso(std::int64_t ticks);

/* Throws MspdiError on malformed XML or a root element that is not Project. */
MspdiDocument import_mspdi(std::string_view xml,
                           const MspdiLoadOptions& options = MspdiLoadOptions{});

/* Reads the file (throws MspdiError with the path on failure) then imports. */
MspdiDocument import_mspdi_file(const std::string& path,
                                const MspdiLoadOptions& options = MspdiLoadOptions{});

}  // namespace core::io

#endif  // CORE_IO_MSPDI_H
