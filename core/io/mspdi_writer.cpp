#include "mspdi_writer.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "field_ref.h"
#include "record_set.h"
#include "value.h"
#include "xml.h"

namespace core::io::mspdi {
namespace {

using core::model::RecordType;
using core::view::DateValue;
using core::view::FieldId;
using core::view::RecordSet;
using core::view::Value;
using core::view::ValueKind;

/* Two spaces per level, the shape the committed samples use. */
constexpr int kContainerDepth = 1;
constexpr int kRecordDepth = 2;
constexpr int kFieldDepth = 3;
constexpr int kLinkFieldDepth = kFieldDepth + 1;

constexpr const char* kXmlDeclaration =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
constexpr const char* kProjectOpen =
    "<Project xmlns=\"http://schemas.microsoft.com/project\">\n";

/* --------------------------------------------------------------------------
 * Element order.
 *
 * INFERENCE, not a validated schema sequence.  The only MSPDI in this
 * repository is tests/io_sample_minimal.xml, tests/io_sample_tasks.xml and
 * tests/io_sample_full.xml, all hand-written by the import stream.  The order
 * below keeps the relative order the samples agree on (UID, ID, Name,
 * OutlineNumber/Level, Duration before Start/Finish, the flags, <PredecessorLink>
 * last on the successor) and fills the rest from the importer's curated table.
 * Where the samples contradict each other (minimal puts PercentComplete before
 * Milestone/Summary/Critical, tasks puts Summary before Duration) the richer
 * "tasks" file wins.  spec/mspdi.md section 8 records this as inferred; no MS
 * Project importer round-trip was run (the oracle is coordinator-only).
 * -------------------------------------------------------------------------- */
constexpr const char* kTaskOrder[] = {
    "ID", "Name", "Type", "WBS", "OutlineNumber", "OutlineLevel", "Priority",
    "Summary", "Milestone", "Duration", "RemainingDuration", "ActualDuration",
    "BaselineDuration", "Start", "Finish", "ActualStart", "ActualFinish",
    "Resume", "ConstraintType", "ConstraintDate", "Deadline", "EarlyStart",
    "EarlyFinish", "LateStart", "LateFinish", "BaselineStart", "BaselineFinish",
    "Date1",
    "PercentComplete", "PercentWorkComplete", "Critical", "Estimated",
    "EffortDriven", "IgnoreResourceCalendar", "LevelingCanSplit", "Active",
    "LinkedFields", "Flag1", "Work", "RemainingWork", "ActualWork",
    "BaselineWork", "OvertimeWork", "ActualOvertimeWork",
    "RemainingOvertimeWork", "RegularWork", "Cost", "FixedCost", "ActualCost",
    "RemainingCost", "BaselineCost", "OvertimeCost", "ActualOvertimeCost",
    "RemainingOvertimeCost", "Cost1", "Number1", "FixedCostAccrual",
    "LevelingDelay", "TotalSlack", "FreeSlack", "Notes", "Hyperlink",
    "HyperlinkAddress", "Text1",
};

constexpr const char* kResourceOrder[] = {
    "ID", "Name", "Type", "Initials", "Group", "MaxUnits", "StandardRate",
    "OvertimeRate", "CostPerUse", "MaterialLabel", "AccrueAt", "Flag1",
    "Number1", "Cost1", "Date1", "Text1", "Notes",
};

constexpr const char* kAssignmentOrder[] = {
    "TaskID", "ResourceID", "TaskName", "ResourceName", "Units", "Work",
    "ActualWork", "RemainingWork", "Cost", "ActualCost", "RemainingCost",
    "Start", "Finish", "ActualStart", "ActualFinish", "PercentWorkComplete",
    "Cost1", "Number1", "Text1", "Notes",
};

struct ElementRef {
    const char* name = "";
    FieldId field = 0;
};

/* Resolves the names through core/model/field_table.cpp, the same table the
 * importer resolves, so a regenerated descriptor table cannot mis-point an
 * element.  A name that stops resolving is skipped rather than guessed: the
 * model could not hold a value for it anyway. */
std::vector<ElementRef> resolve_order(const char* const* names, std::size_t count,
                                      RecordType record_type) {
    std::vector<ElementRef> elements;
    elements.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        FieldId field = 0;
        if (core::view::field_id_by_name(record_type, names[i], field)) {
            elements.push_back({names[i], field});
        }
    }
    return elements;
}

void append_indent(std::string& out, int depth) {
    out.append(static_cast<std::size_t>(depth * 2), ' ');
}

void write_text_element(std::string& out, int depth, std::string_view name,
                        std::string_view text) {
    append_indent(out, depth);
    out += '<';
    out.append(name);
    out += '>';
    out += escape_xml(text);
    out += "</";
    out.append(name);
    out += ">\n";
}

std::string date_time_text(const DateValue& date) {
    /* The importer's ISO form is YYYY-MM-DDTHH:MM[:SS]; write the seconds
     * always so the text is stable and the civil time is unambiguous. */
    if (!date.valid() || date.year < 1 || date.year > 9999) {
        return {};
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d",
                  static_cast<int>(date.year), static_cast<int>(date.month),
                  static_cast<int>(date.day), static_cast<int>(date.hour),
                  static_cast<int>(date.minute), static_cast<int>(date.second));
    return std::string(buffer);
}

/* False when MSPDI has no representation for the value and the element must be
 * omitted.  Empty Text counts as "no value": the importer reads <x/> and
 * <x></x> both as Null, so writing an empty element would lose the distinction
 * anyway and break the "empty means no element" rule. */
bool value_to_text(const Value& value, std::string& text) {
    switch (value.kind) {
        case ValueKind::Null:
            return false;
        case ValueKind::Bool:
            text = value.boolean ? "1" : "0";
            return true;
        case ValueKind::Integer:
            text = std::to_string(value.integer);
            return true;
        case ValueKind::Number:
            if (!std::isfinite(value.number)) {
                return false;
            }
            text = core::view::format_value(value);  // shortest round-trip
            return !text.empty();
        case ValueKind::Text:
            text = value.text;
            return !text.empty();
        case ValueKind::Duration:
            text = core::view::format_value(value);  // PT#H#M#S
            return true;
        case ValueKind::Date:
            text = date_time_text(value.date);
            return !text.empty();
    }
    return false;
}

void write_record_fields(std::string& out, const RecordSet& records, std::size_t row,
                         const std::vector<ElementRef>& elements) {
    for (const ElementRef& element : elements) {
        const Value* value = records.get(row, element.field);
        std::string text;
        if (value != nullptr && value_to_text(*value, text)) {
            write_text_element(out, kFieldDepth, element.name, text);
            continue;
        }
        /* <ID> is a display sequence number, not project data: Project's
         * exports always carry it, so fall back to the row's 1-based position
         * when the model has no value.  (INFERENCE -- see the header.) */
        if (std::string_view(element.name) == "ID") {
            write_text_element(out, kFieldDepth, element.name, std::to_string(row + 1));
        }
    }
}

void write_predecessor_link(std::string& out, const PredecessorLink& link) {
    append_indent(out, kFieldDepth);
    out += "<PredecessorLink>\n";
    write_text_element(out, kLinkFieldDepth, "PredecessorUID",
                       std::to_string(link.predecessor_uid));
    write_text_element(out, kLinkFieldDepth, "Type", std::to_string(link.type));
    /* In the committed sample and in Project's own exports; the importer
     * ignores it.  A link in this model is by definition in this project. */
    write_text_element(out, kLinkFieldDepth, "CrossProject", "0");
    /* Canonical Project-export spelling: decimal tenths of a minute, which IS
     * the 6-second tick.  Project's own export writes "0"/"240000", not an ISO
     * duration; writing decimal therefore matches the one producer whose format
     * is evidenced (mspdi.h, "LinkLag spelling").  The tests pin this exact
     * string. */
    write_text_element(out, kLinkFieldDepth, "LinkLag",
                       format_link_lag_decimal(link.lag_ticks));
    write_text_element(out, kLinkFieldDepth, "LagFormat", std::to_string(link.lag_format));
    append_indent(out, kFieldDepth);
    out += "</PredecessorLink>\n";
}

const std::vector<const PredecessorLink*> kNoLinks;

void write_task(std::string& out, const MspdiDocument& document, std::size_t row,
                const std::vector<ElementRef>& elements,
                const std::vector<const PredecessorLink*>& links) {
    append_indent(out, kRecordDepth);
    out += "<Task>\n";
    write_text_element(out, kFieldDepth, "UID", std::to_string(document.tasks.uid(row)));
    write_record_fields(out, document.tasks, row, elements);
    for (const PredecessorLink* link : links) {
        write_predecessor_link(out, *link);
    }
    append_indent(out, kRecordDepth);
    out += "</Task>\n";
}

void write_tasks(std::string& out, const MspdiDocument& document,
                 const std::vector<ElementRef>& elements) {
    if (document.tasks.empty()) {
        append_indent(out, kContainerDepth);
        out += "<Tasks/>\n";
        return;
    }
    /* PredecessorLink lives under its successor, so group links by successor
     * UID first.  A link whose successor is not a task row is skipped below. */
    std::map<std::uint32_t, std::vector<const PredecessorLink*>> by_successor;
    for (const PredecessorLink& link : document.links) {
        by_successor[link.successor_uid].push_back(&link);
    }
    append_indent(out, kContainerDepth);
    out += "<Tasks>\n";
    for (std::size_t row = 0; row < document.tasks.row_count(); ++row) {
        const auto found = by_successor.find(document.tasks.uid(row));
        write_task(out, document, row, elements,
                   found == by_successor.end() ? kNoLinks : found->second);
    }
    append_indent(out, kContainerDepth);
    out += "</Tasks>\n";
}

void write_resources(std::string& out, const MspdiDocument& document,
                     const std::vector<ElementRef>& elements) {
    if (document.resources.empty()) {
        append_indent(out, kContainerDepth);
        out += "<Resources/>\n";
        return;
    }
    append_indent(out, kContainerDepth);
    out += "<Resources>\n";
    for (std::size_t row = 0; row < document.resources.row_count(); ++row) {
        append_indent(out, kRecordDepth);
        out += "<Resource>\n";
        write_text_element(out, kFieldDepth, "UID",
                           std::to_string(document.resources.uid(row)));
        write_record_fields(out, document.resources, row, elements);
        append_indent(out, kRecordDepth);
        out += "</Resource>\n";
    }
    append_indent(out, kContainerDepth);
    out += "</Resources>\n";
}

void write_assignments(std::string& out, const MspdiDocument& document,
                       const std::vector<ElementRef>& elements) {
    if (document.assignments.empty() || document.assignment_refs.empty()) {
        append_indent(out, kContainerDepth);
        out += "<Assignments/>\n";
        return;
    }
    append_indent(out, kContainerDepth);
    out += "<Assignments>\n";
    const std::size_t rows = document.assignments.row_count();
    for (std::size_t row = 0; row < rows; ++row) {
        /* The importer keeps one ref per assignment row, in row order.  A row
         * without a ref cannot name a task and a resource, so it is not
         * written; there is no valid MSPDI form for it. */
        if (row >= document.assignment_refs.size()) {
            break;
        }
        const MspdiAssignmentRef& ref = document.assignment_refs[row];
        append_indent(out, kRecordDepth);
        out += "<Assignment>\n";
        write_text_element(out, kFieldDepth, "UID",
                           std::to_string(document.assignments.uid(row)));
        write_text_element(out, kFieldDepth, "TaskUID", std::to_string(ref.task_uid));
        write_text_element(out, kFieldDepth, "ResourceUID",
                           std::to_string(ref.resource_uid));
        write_record_fields(out, document.assignments, row, elements);
        append_indent(out, kRecordDepth);
        out += "</Assignment>\n";
    }
    append_indent(out, kContainerDepth);
    out += "</Assignments>\n";
}

void write_date_element(std::string& out, const char* name, const Value& value) {
    if (value.kind != ValueKind::Date) {
        return;
    }
    const std::string text = date_time_text(value.date);
    if (!text.empty()) {
        write_text_element(out, kContainerDepth, name, text);
    }
}

/* -1 is the "not present" sentinel in MspdiProject; a value that was never set
 * stays absent so the re-import matches. */
void write_int_element(std::string& out, const char* name, int value) {
    if (value >= 0) {
        write_text_element(out, kContainerDepth, name, std::to_string(value));
    }
}

void write_project_header(std::string& out, const MspdiProject& project) {
    if (!project.name.empty()) {
        write_text_element(out, kContainerDepth, "Name", project.name);
    }
    if (!project.title.empty()) {
        write_text_element(out, kContainerDepth, "Title", project.title);
    }
    write_date_element(out, "StartDate", project.start);
    write_date_element(out, "FinishDate", project.finish);
    /* Only when import recorded it: <ScheduleFromStart> is not implied, and
     * writing it always would change has_schedule_from_start on re-import. */
    if (project.has_schedule_from_start) {
        write_text_element(out, kContainerDepth, "ScheduleFromStart",
                           project.schedule_from_start ? "1" : "0");
    }
    write_int_element(out, "MinutesPerDay", project.minutes_per_day);
    write_int_element(out, "MinutesPerWeek", project.minutes_per_week);
    write_int_element(out, "DaysPerMonth", project.days_per_month);
    write_int_element(out, "CalendarUID", project.calendar_uid);
    if (!project.default_start_time.empty()) {
        write_text_element(out, kContainerDepth, "DefaultStartTime",
                           project.default_start_time);
    }
    if (!project.default_finish_time.empty()) {
        write_text_element(out, kContainerDepth, "DefaultFinishTime",
                           project.default_finish_time);
    }
}

}  // namespace

std::string write(const MspdiDocument& document) {
    const std::vector<ElementRef> task_elements =
        resolve_order(kTaskOrder, std::size(kTaskOrder), RecordType::Task);
    const std::vector<ElementRef> resource_elements =
        resolve_order(kResourceOrder, std::size(kResourceOrder), RecordType::Resource);
    const std::vector<ElementRef> assignment_elements =
        resolve_order(kAssignmentOrder, std::size(kAssignmentOrder),
                      RecordType::Assignment);

    std::string out;
    out.reserve(256 + document.tasks.row_count() * 320 +
                document.resources.row_count() * 160 +
                document.assignments.row_count() * 240);
    out += kXmlDeclaration;
    out += kProjectOpen;
    write_project_header(out, document.project);
    write_tasks(out, document, task_elements);
    write_resources(out, document, resource_elements);
    write_assignments(out, document, assignment_elements);
    out += "</Project>\n";
    return out;
}

void write_file(const MspdiDocument& document, const std::string& path) {
    const std::string text = write(document);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw MspdiError("cannot open \"" + path + "\" for writing");
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    if (!stream) {
        throw MspdiError("failed to write \"" + path + "\"");
    }
}

}  // namespace core::io::mspdi
