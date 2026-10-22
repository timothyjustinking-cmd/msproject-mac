#include "crosscheck.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>  // std::size
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "field_ref.h"
#include "mpp_bridge.h"  // reader -> civil dates; the conversion is header-only
#include "value.h"

namespace mspdi_cmp {
namespace {

using core::view::DateValue;
using core::view::FieldId;
using core::view::Value;
using core::view::ValueKind;

/* The fields compared per task.  Both names and ids come from the descriptor
 * table (resolved by name, never hardcoded), so a regenerated table cannot
 * silently point at the wrong id.  The order is the mismatch-table order.
 * Milestone is in the table (so the comparison count stays 10 per task) but a
 * disagreement is routed to Result::milestone_differences, not to
 * field_mismatches: the reader derives it from duration, so it is a note. */
enum class CompareKind : std::uint8_t { Date, Ticks, Bool, Integer };

struct FieldSpec {
    const char* name;
    CompareKind kind;
};

constexpr FieldSpec kComparedFields[] = {
    {"Start", CompareKind::Date},
    {"Finish", CompareKind::Date},
    {"Duration", CompareKind::Ticks},
    {"Milestone", CompareKind::Bool},
    {"Summary", CompareKind::Bool},
    {"ConstraintType", CompareKind::Integer},
    {"ConstraintDate", CompareKind::Date},
    {"Deadline", CompareKind::Date},
    {"ActualStart", CompareKind::Date},
    {"ActualFinish", CompareKind::Date},
};

constexpr std::size_t kComparedFieldCount = std::size(kComparedFields);

/* MSPDI reserves UID 0: for tasks it is the project summary row, for resources
 * the "no resource" placeholder.  The reader returns neither as a record. */
constexpr std::uint32_t kProjectRowUid = 0;

std::int64_t reader_date32(const mpp::PackedDate& date) {
    if (!date.valid()) {
        return kNoDate;
    }
    return static_cast<std::int64_t>(date.raw);
}

std::string format_reader_date(const mpp::PackedDate& date) {
    const std::string text = mpp::format_date(date);
    return text.empty() ? std::string("(none)") : text;
}

std::string format_xml_date(const Value* value) {
    if (value == nullptr || value->kind != ValueKind::Date) {
        return "(none)";
    }
    const std::string text = core::view::format_date(value->date);
    return text.empty() ? std::string("(none)") : text;
}

std::string format_ticks(std::int64_t ticks) {
    return core::view::format_duration(ticks) + " (" + std::to_string(ticks) + " ticks)";
}

std::string format_bool(bool value) { return value ? "1" : "0"; }

/* Assignment units are compared as a percentage, because that is what the
 * reader stores (units() is 100.0 for a full-time assignment) while Project's
 * export writes the fraction (1.0).  The XML value is multiplied by 100 when it
 * is loaded, so every AssignmentRow::units is a percentage. */
std::string format_percent(double percent) {
    if (!std::isfinite(percent)) {
        return "(none)";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g%%", percent);
    return std::string(buffer);
}

std::uint32_t normalize_resource_uid(std::uint32_t raw) {
    if (raw == 0 || raw == mpp::kNullResourceUid) {
        return 0; /* "no resource" in both models */
    }
    return raw;
}

std::string resource_text(std::uint32_t uid) { return uid == 0 ? "none" : std::to_string(uid); }

std::string type_name(int type) {
    switch (type & 0x3) {
        case 0:
            return "FF";
        case 1:
            return "FS";
        case 2:
            return "SF";
        case 3:
            return "SS";
        default:
            return "?";
    }
}

void append_padded(std::string& out, std::string_view text, std::size_t width) {
    out += text;
    if (text.size() < width) {
        out.append(width - text.size(), ' ');
    }
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
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
                /* Field names, dates and uids are ASCII; a control byte never
                 * occurs, but the output must stay valid JSON if one ever does. */
                out += (static_cast<unsigned char>(ch) < 0x20) ? '?' : ch;
                break;
        }
    }
    return out;
}

std::string json_string(std::string_view text) { return "\"" + json_escape(text) + "\""; }

std::vector<std::uint32_t> live_task_uids(const mpp::Document& reader,
                                          const core::io::MspdiDocument& xml) {
    std::vector<std::uint32_t> uids;
    uids.reserve(reader.tasks.size() + xml.tasks.row_count());
    for (const mpp::Task& task : reader.tasks) {
        if (task.uid != kProjectRowUid) {
            uids.push_back(task.uid);
        }
    }
    for (std::size_t row = 0; row < xml.tasks.row_count(); ++row) {
        const std::uint32_t uid = xml.tasks.uid(row);
        if (uid != kProjectRowUid) {
            uids.push_back(uid);
        }
    }
    std::sort(uids.begin(), uids.end());
    uids.erase(std::unique(uids.begin(), uids.end()), uids.end());
    return uids;
}

/* Multiset difference over two sorted vectors.  Equal keys are consumed in
 * pairs, so a duplicated link on one side stays visible as a difference. */
void diff_links(std::vector<LinkRow> reader, std::vector<LinkRow> xml, Result& result) {
    std::sort(reader.begin(), reader.end());
    std::sort(xml.begin(), xml.end());
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < reader.size() && j < xml.size()) {
        if (reader[i] < xml[j]) {
            result.reader_only_links.push_back(reader[i]);
            ++i;
        } else if (xml[j] < reader[i]) {
            result.xml_only_links.push_back(xml[j]);
            ++j;
        } else {
            ++result.matched_links;
            ++i;
            ++j;
        }
    }
    for (; i < reader.size(); ++i) {
        result.reader_only_links.push_back(reader[i]);
    }
    for (; j < xml.size(); ++j) {
        result.xml_only_links.push_back(xml[j]);
    }
}

void diff_uids(std::vector<std::uint32_t> reader, std::vector<std::uint32_t> xml,
               std::size_t& matched, std::vector<std::uint32_t>& reader_only,
               std::vector<std::uint32_t>& xml_only) {
    std::sort(reader.begin(), reader.end());
    std::sort(xml.begin(), xml.end());
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < reader.size() && j < xml.size()) {
        if (reader[i] < xml[j]) {
            reader_only.push_back(reader[i]);
            ++i;
        } else if (xml[j] < reader[i]) {
            xml_only.push_back(xml[j]);
            ++j;
        } else {
            ++matched;
            ++i;
            ++j;
        }
    }
    for (; i < reader.size(); ++i) {
        reader_only.push_back(reader[i]);
    }
    for (; j < xml.size(); ++j) {
        xml_only.push_back(xml[j]);
    }
}

void append_link_line(std::string& out, const LinkRow& link) {
    out += "    pred ";
    append_padded(out, std::to_string(link.predecessor_uid), 8);
    out += "succ ";
    append_padded(out, std::to_string(link.successor_uid), 8);
    out += type_name(link.type);
    out += "  lag ";
    out += std::to_string(link.lag_ticks);
    out += " ticks\n";
}

void append_assignment_line(std::string& out, const AssignmentRow& row) {
    out += "    assignment ";
    append_padded(out, std::to_string(row.uid), 8);
    out += "task ";
    append_padded(out, std::to_string(row.task_uid), 8);
    out += "resource ";
    out += resource_text(row.resource_uid);
    out += '\n';
}

void append_mismatch_rows(std::string& out, const std::vector<FieldMismatch>& rows) {
    if (rows.empty()) {
        return;
    }
    out += "  uid      field              reader                     xml\n";
    for (const FieldMismatch& mismatch : rows) {
        out += "  ";
        append_padded(out, std::to_string(mismatch.uid), 9);
        append_padded(out, mismatch.field, 19);
        append_padded(out, mismatch.reader_value, 27);
        out += mismatch.xml_value;
        out += '\n';
    }
}

void append_mismatch_section(std::string& out, const char* heading,
                             const std::vector<FieldMismatch>& rows) {
    out += std::string(heading) + ": " + std::to_string(rows.size()) + "\n";
    append_mismatch_rows(out, rows);
}

}  // namespace

bool operator==(const LinkRow& a, const LinkRow& b) noexcept {
    return a.predecessor_uid == b.predecessor_uid && a.successor_uid == b.successor_uid &&
           a.type == b.type && a.lag_ticks == b.lag_ticks;
}

bool operator<(const LinkRow& a, const LinkRow& b) noexcept {
    if (a.predecessor_uid != b.predecessor_uid) {
        return a.predecessor_uid < b.predecessor_uid;
    }
    if (a.successor_uid != b.successor_uid) {
        return a.successor_uid < b.successor_uid;
    }
    if (a.type != b.type) {
        return a.type < b.type;
    }
    return a.lag_ticks < b.lag_ticks;
}

std::int64_t date32_of(const DateValue& date) {
    if (!date.valid()) {
        return kNoDate;
    }
    const std::int64_t serial =
        core::io::bridge_detail::days_from_civil(date.year, date.month, date.day) -
        core::io::bridge_detail::reader_serial_base();
    const std::int64_t seconds =
        static_cast<std::int64_t>(date.hour) * 3600 + date.minute * 60 + date.second;
    /* Same layout as the reader's packed date: (day << 16) | secs/6, day 0 =
     * 1983-12-31.  A day that does not fit the reader's 16-bit day field cannot
     * be compared against a reader value, so it reads as absent. */
    if (serial < 0 || serial > 0xFFFF) {
        return kNoDate;
    }
    return (serial << 16) | (seconds / 6);
}

Result compare(const mpp::Document& reader, const core::io::MspdiDocument& xml) {
    Result result;

    /* ---- Resolve the compared fields by name, once. */
    struct ResolvedField {
        FieldId field = 0;
        const FieldSpec* spec = nullptr;
    };
    std::vector<ResolvedField> fields;
    fields.reserve(kComparedFieldCount);
    for (const FieldSpec& spec : kComparedFields) {
        FieldId field = 0;
        if (!core::view::field_id_by_name(core::model::RecordType::Task, spec.name, field)) {
            result.problems.push_back(std::string("the descriptor table has no Task field \"") +
                                      spec.name + "\"; it cannot be compared");
            continue;
        }
        fields.push_back({field, &spec});
    }

    /* Assignment values are resolved the same way.  Identity (TaskUID /
     * ResourceUID) comes from MspdiDocument::assignment_refs, not the table. */
    FieldId assignment_units_field = 0;
    if (!core::view::field_id_by_name(core::model::RecordType::Assignment, "Units",
                                      assignment_units_field)) {
        result.problems.push_back(
            "the descriptor table has no Assignment field \"Units\"; units cannot be compared");
    }
    FieldId assignment_work_field = 0;
    if (!core::view::field_id_by_name(core::model::RecordType::Assignment, "Work",
                                      assignment_work_field)) {
        result.problems.push_back(
            "the descriptor table has no Assignment field \"Work\"; work cannot be compared");
    }
    FieldId resource_type_field = 0;
    FieldId resource_max_units_field = 0;
    if (!core::view::field_id_by_name(core::model::RecordType::Resource, "Type",
                                      resource_type_field)) {
        result.problems.push_back(
            "the descriptor table has no Resource field \"Type\"; it cannot be compared");
    }
    if (!core::view::field_id_by_name(core::model::RecordType::Resource, "MaxUnits",
                                      resource_max_units_field)) {
        result.problems.push_back(
            "the descriptor table has no Resource field \"MaxUnits\"; it cannot be compared");
    }

    /* ---- Task UID sets. */
    result.reader_tasks = reader.tasks.size();
    result.reader_links = reader.links.size();
    result.xml_tasks = xml.tasks.row_count();
    result.xml_links = xml.links.size();

    /* MSPDI's project summary task is UID 0 and the reader returns row 0 as
     * Document::project, not as a task.  Excluding it is not a concession: it is
     * the one structural difference between the two models. */
    const int project_row = xml.tasks.row_of_uid(kProjectRowUid);
    if (project_row >= 0) {
        result.xml_has_project_row = true;
        result.project_uid = kProjectRowUid;
    }

    for (const mpp::Task& task : reader.tasks) {
        if (xml.tasks.row_of_uid(task.uid) < 0) {
            result.reader_only_uids.push_back(task.uid);
        }
    }
    for (std::size_t row = 0; row < xml.tasks.row_count(); ++row) {
        const std::uint32_t uid = xml.tasks.uid(row);
        if (result.xml_has_project_row && uid == result.project_uid) {
            continue;
        }
        const bool in_reader =
            std::any_of(reader.tasks.begin(), reader.tasks.end(),
                        [uid](const mpp::Task& task) { return task.uid == uid; });
        if (!in_reader) {
            result.xml_only_uids.push_back(uid);
        }
    }
    std::sort(result.reader_only_uids.begin(), result.reader_only_uids.end());
    std::sort(result.xml_only_uids.begin(), result.xml_only_uids.end());

    /* ---- Field comparison on the UIDs present in both models. */
    for (const mpp::Task& task : reader.tasks) {
        const int xml_row = xml.tasks.row_of_uid(task.uid);
        if (xml_row < 0) {
            continue;
        }
        ++result.compared_tasks;
        const std::size_t row = static_cast<std::size_t>(xml_row);
        for (const ResolvedField& resolved : fields) {
            const FieldSpec& spec = *resolved.spec;
            const Value* xml_value = xml.tasks.get(row, resolved.field);
            ++result.compared_fields;
            bool match = true;
            std::string reader_text;
            std::string xml_text;
            switch (spec.kind) {
                case CompareKind::Date: {
                    const mpp::PackedDate* packed = nullptr;
                    if (std::string_view(spec.name) == "Start") {
                        packed = &task.start;
                    } else if (std::string_view(spec.name) == "Finish") {
                        packed = &task.finish;
                    } else if (std::string_view(spec.name) == "ConstraintDate") {
                        packed = &task.constraint_date;
                    } else if (std::string_view(spec.name) == "Deadline") {
                        packed = &task.deadline;
                    } else if (std::string_view(spec.name) == "ActualStart") {
                        packed = &task.actual_start;
                    } else if (std::string_view(spec.name) == "ActualFinish") {
                        packed = &task.actual_finish;
                    }
                    if (packed == nullptr) {
                        result.problems.push_back(
                            std::string("no reader member for <") + spec.name + ">");
                        match = false;
                        break;
                    }
                    const bool xml_present = xml_value != nullptr &&
                                             xml_value->kind == ValueKind::Date;
                    const std::int64_t xml_date =
                        xml_present ? date32_of(xml_value->date) : kNoDate;
                    match = reader_date32(*packed) == xml_date;
                    reader_text = format_reader_date(*packed);
                    xml_text = format_xml_date(xml_value);
                    break;
                }
                case CompareKind::Ticks: {
                    const bool xml_present = xml_value != nullptr &&
                                             xml_value->kind == ValueKind::Duration;
                    const std::int64_t reader_ticks =
                        static_cast<std::int64_t>(task.duration_ticks);
                    match = xml_present && xml_value->integer == reader_ticks;
                    reader_text = format_ticks(reader_ticks);
                    xml_text = xml_present ? format_ticks(xml_value->integer)
                                           : std::string("(none)");
                    break;
                }
                case CompareKind::Bool: {
                    const bool is_milestone_field =
                        std::string_view(spec.name) == "Milestone";
                    const bool reader_bool =
                        is_milestone_field ? task.is_milestone() : task.summary;
                    const bool xml_present = xml_value != nullptr &&
                                             xml_value->kind == ValueKind::Bool;
                    const bool xml_bool = xml_present && xml_value->boolean;
                    match = xml_present && xml_bool == reader_bool;
                    reader_text = format_bool(reader_bool);
                    if (is_milestone_field) {
                        /* Say which reader path produced the value: the stored
                         * FixedMeta bit, or the duration == 0 fallback. */
                        reader_text += task.milestone_stored
                                           ? " (stored FixedMeta bit)"
                                           : " (duration fallback)";
                    }
                    xml_text = xml_present ? format_bool(xml_bool) : std::string("(none)");
                    break;
                }
                case CompareKind::Integer: {
                    /* ConstraintType is the only Integer field in the table. */
                    const std::int64_t reader_int =
                        static_cast<std::int64_t>(task.constraint_type);
                    const bool xml_present = xml_value != nullptr &&
                                             xml_value->kind == ValueKind::Integer;
                    match = xml_present && xml_value->integer == reader_int;
                    reader_text = std::to_string(reader_int);
                    xml_text = xml_present ? std::to_string(xml_value->integer)
                                           : std::string("(none)");
                    break;
                }
            }
            if (!match) {
                /* Milestone: the reader takes the stored FixedMeta bit when the
                 * row has one and falls back to duration == 0, while a real
                 * export carries the effective flag.  A disagreement is a note
                 * for core/mpp, not MSPDI parity. */
                if (std::string_view(spec.name) == "Milestone") {
                    result.milestone_differences.push_back(
                        {task.uid, spec.name, std::move(reader_text), std::move(xml_text)});
                } else {
                    result.field_mismatches.push_back(
                        {task.uid, spec.name, std::move(reader_text), std::move(xml_text)});
                }
            }
        }
    }

    /* ---- Links.  A link is live only when BOTH endpoints are a live task in
     * one of the models; a row whose endpoint is not a task anymore (deleted
     * slot) cannot be scheduled or exported, so it is a named exclusion. */
    const std::vector<std::uint32_t> live_uids = live_task_uids(reader, xml);
    const auto is_live = [&live_uids](std::uint32_t uid) {
        return std::binary_search(live_uids.begin(), live_uids.end(), uid);
    };

    std::vector<LinkRow> reader_links;
    reader_links.reserve(reader.links.size());
    for (const mpp::Link& link : reader.links) {
        const LinkRow row{link.predecessor_uid, link.successor_uid,
                          static_cast<int>(link.type_raw & 0x3u), link.lag_ticks};
        if (!is_live(row.predecessor_uid) || !is_live(row.successor_uid)) {
            result.dead_endpoint_links.push_back(row);
        } else {
            reader_links.push_back(row);
        }
    }
    std::vector<LinkRow> xml_links;
    xml_links.reserve(xml.links.size());
    for (const core::io::PredecessorLink& link : xml.links) {
        const LinkRow row{link.predecessor_uid, link.successor_uid, link.type & 0x3,
                          link.lag_ticks};
        if (!is_live(row.predecessor_uid) || !is_live(row.successor_uid)) {
            result.xml_dead_endpoint_links.push_back(row);
        } else {
            xml_links.push_back(row);
        }
    }
    result.live_reader_links = reader_links.size();
    result.live_xml_links = xml_links.size();
    diff_links(std::move(reader_links), std::move(xml_links), result);

    /* ---- Resources.  Project writes a UID 0 placeholder ("no resource") that
     * the reader does not return; excluding it is the resource counterpart of
     * the task project row. */
    result.reader_resources = reader.resources.size();
    result.xml_resources = xml.resources.row_count();
    if (xml.resources.row_of_uid(kProjectRowUid) >= 0) {
        result.xml_has_placeholder_resource = true;
    }
    std::vector<std::uint32_t> reader_resource_uids;
    reader_resource_uids.reserve(reader.resources.size());
    for (const mpp::Resource& resource : reader.resources) {
        reader_resource_uids.push_back(resource.uid);
    }
    std::vector<std::uint32_t> xml_resource_uids;
    xml_resource_uids.reserve(xml.resources.row_count());
    for (std::size_t row = 0; row < xml.resources.row_count(); ++row) {
        const std::uint32_t uid = xml.resources.uid(row);
        if (result.xml_has_placeholder_resource && uid == kProjectRowUid) {
            continue;
        }
        xml_resource_uids.push_back(uid);
    }
    diff_uids(std::move(reader_resource_uids), std::move(xml_resource_uids),
              result.matched_resources, result.reader_only_resource_uids,
              result.xml_only_resource_uids);
    result.compared_resources = result.matched_resources;

    /* Resource Type/MaxUnits.  All 46 resources in the three real exports are
     * Type=1 (Work) with MaxUnits=1.00, and the reader's +8 double is 10000.0
     * (100 %).  The comparison normalises the XML fraction to percent; the
     * non-Work enum values are not mapped because no fixture contains one. */
    for (const mpp::Resource& resource : reader.resources) {
        const int xml_row = xml.resources.row_of_uid(resource.uid);
        if (xml_row < 0) {
            continue;
        }
        const std::size_t row = static_cast<std::size_t>(xml_row);
        if (resource_type_field != 0) {
            const Value* type = xml.resources.get(row, resource_type_field);
            if (type != nullptr && type->kind == ValueKind::Integer) {
                ++result.compared_resource_types;
                const bool reader_work = resource.type == mpp::ResourceType::work;
                /* MSPDI Type 1 is Work (measured on all 46 resources; 0/2/3 have
                 * no fixture, so they are reported as a mismatch rather than
                 * guessed). */
                if (!(reader_work && type->integer == 1)) {
                    result.resource_mismatches.push_back(
                        {resource.uid, "Type",
                         reader_work ? std::string("work (0)")
                                     : std::string("non-work"),
                         std::to_string(type->integer)});
                }
            }
        }
        if (resource_max_units_field != 0) {
            const Value* max_units = xml.resources.get(row, resource_max_units_field);
            if (max_units != nullptr && max_units->kind == ValueKind::Number) {
                ++result.compared_resource_max_units;
                /* core::io already scaled the export fraction to percent. */
                const double xml_percent = max_units->number;
                if (!(std::fabs(resource.max_units() - xml_percent) <= 1e-6)) {
                    result.resource_mismatches.push_back(
                        {resource.uid, "MaxUnits", format_percent(resource.max_units()),
                         format_percent(xml_percent)});
                }
            }
        }
    }

    /* ---- Assignments.  Identity is the assignment UID; the task UID decides
     * liveness (a deleted task slot takes its assignments with it). */
    result.reader_assignments = reader.assignments.size();
    result.xml_assignments = xml.assignments.row_count();

    std::vector<AssignmentRow> reader_assignments;
    reader_assignments.reserve(reader.assignments.size());
    for (const mpp::Assignment& assignment : reader.assignments) {
        AssignmentRow row;
        row.uid = assignment.uid;
        row.task_uid = assignment.task_uid;
        row.resource_uid = normalize_resource_uid(assignment.resource_uid_raw);
        row.units_compared = true;
        row.units = assignment.units();
        row.work_compared = true;
        row.work_ticks = static_cast<std::int64_t>(assignment.work_ticks());
        row.start = reader_date32(assignment.start);
        row.finish = reader_date32(assignment.finish);
        row.resume = reader_date32(assignment.resume);
        if (!is_live(row.task_uid)) {
            result.dead_endpoint_assignments.push_back(row);
        } else {
            reader_assignments.push_back(row);
        }
    }
    result.live_reader_assignments = reader_assignments.size();

    std::vector<AssignmentRow> xml_assignments;
    xml_assignments.reserve(xml.assignments.row_count());
    for (std::size_t row = 0; row < xml.assignments.row_count(); ++row) {
        const std::uint32_t uid = xml.assignments.uid(row);
        AssignmentRow out;
        out.uid = uid;
        if (row < xml.assignment_refs.size() && xml.assignment_refs[row].uid == uid) {
            out.task_uid = xml.assignment_refs[row].task_uid;
            out.resource_uid =
                normalize_resource_uid(xml.assignment_refs[row].resource_uid);
            const auto ref_date = [](const Value& value) {
                return value.kind == ValueKind::Date ? date32_of(value.date) : kNoDate;
            };
            out.start = ref_date(xml.assignment_refs[row].start);
            out.finish = ref_date(xml.assignment_refs[row].finish);
            if (xml.assignment_refs[row].resume.kind == ValueKind::Date) {
                out.resume = date32_of(xml.assignment_refs[row].resume.date);
                out.resume_compared = true;
            }
        } else {
            result.problems.push_back("Assignment[UID=" + std::to_string(uid) +
                                      "] has no matching assignment_ref; its task and "
                                      "resource cannot be compared");
        }
        if (assignment_units_field != 0) {
            const Value* units = xml.assignments.get(row, assignment_units_field);
            if (units != nullptr) {
                if (units->kind == ValueKind::Number) {
                    out.units_compared = true;
                    /* core::io normalises Project's fraction to the model's
                     * percent, so both sides are already percent. */
                    out.units = units->number;
                } else {
                    result.problems.push_back("Assignment[UID=" + std::to_string(uid) +
                                              "]/Units is not a number");
                }
            }
        }
        if (assignment_work_field != 0) {
            const Value* work = xml.assignments.get(row, assignment_work_field);
            if (work != nullptr) {
                if (work->kind == ValueKind::Duration) {
                    out.work_compared = true;
                    out.work_ticks = work->integer;
                } else {
                    result.problems.push_back("Assignment[UID=" + std::to_string(uid) +
                                              "]/Work is not a duration");
                }
            }
        }
        if (!is_live(out.task_uid)) {
            result.xml_dead_endpoint_assignments.push_back(out);
        } else {
            xml_assignments.push_back(out);
        }
    }
    result.live_xml_assignments = xml_assignments.size();

    const auto by_uid = [](const AssignmentRow& a, const AssignmentRow& b) {
        return a.uid < b.uid;
    };
    std::sort(reader_assignments.begin(), reader_assignments.end(), by_uid);
    std::sort(xml_assignments.begin(), xml_assignments.end(), by_uid);
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < reader_assignments.size() && j < xml_assignments.size()) {
        const AssignmentRow& reader_row = reader_assignments[i];
        const AssignmentRow& xml_row = xml_assignments[j];
        if (reader_row.uid < xml_row.uid) {
            result.reader_only_assignments.push_back(reader_row);
            ++i;
        } else if (xml_row.uid < reader_row.uid) {
            result.xml_only_assignments.push_back(xml_row);
            ++j;
        } else {
            ++result.compared_assignments;
            ++result.matched_assignments;
            if (reader_row.task_uid != xml_row.task_uid) {
                result.assignment_mismatches.push_back(
                    {reader_row.uid, "TaskUID", std::to_string(reader_row.task_uid),
                     std::to_string(xml_row.task_uid)});
            }
            if (reader_row.resource_uid != xml_row.resource_uid) {
                result.assignment_mismatches.push_back(
                    {reader_row.uid, "ResourceUID", resource_text(reader_row.resource_uid),
                     resource_text(xml_row.resource_uid)});
            }
            if (xml_row.units_compared) {
                ++result.compared_assignment_units;
                if (!(std::fabs(reader_row.units - xml_row.units) <= 1e-6)) {
                    result.assignment_mismatches.push_back(
                        {reader_row.uid, "Units", format_percent(reader_row.units),
                         format_percent(xml_row.units)});
                }
            }
            if (xml_row.work_compared) {
                ++result.compared_assignment_work;
                if (reader_row.work_ticks != xml_row.work_ticks) {
                    result.assignment_mismatches.push_back(
                        {reader_row.uid, "Work", format_ticks(reader_row.work_ticks),
                         format_ticks(xml_row.work_ticks)});
                }
            }
            /* Start/Finish: both sides use kNoDate for "not set", so an absent
             * XML element and an NA reader field compare equal. */
            const auto format_date32 = [](std::int64_t value) {
                return value == kNoDate
                           ? std::string("(none)")
                           : format_reader_date(
                                 mpp::PackedDate{static_cast<std::uint32_t>(value)});
            };
            ++result.compared_assignment_dates;
            if (reader_row.start != xml_row.start) {
                result.assignment_mismatches.push_back(
                    {reader_row.uid, "Start", format_date32(reader_row.start),
                     format_date32(xml_row.start)});
            }
            if (reader_row.finish != xml_row.finish) {
                result.assignment_mismatches.push_back(
                    {reader_row.uid, "Finish", format_date32(reader_row.finish),
                     format_date32(xml_row.finish)});
            }
            if (xml_row.resume_compared) {
                ++result.compared_assignment_resume;
                if (reader_row.resume != xml_row.resume) {
                    result.assignment_mismatches.push_back(
                        {reader_row.uid, "Resume", format_date32(reader_row.resume),
                         format_date32(xml_row.resume)});
                }
            }
            ++i;
            ++j;
        }
    }
    for (; i < reader_assignments.size(); ++i) {
        result.reader_only_assignments.push_back(reader_assignments[i]);
    }
    for (; j < xml_assignments.size(); ++j) {
        result.xml_only_assignments.push_back(xml_assignments[j]);
    }

    return result;
}

std::string format_report(const Result& result) {
    std::string out;
    out += "tasks: reader " + std::to_string(result.reader_tasks) + ", xml " +
           std::to_string(result.xml_tasks);
    if (result.xml_has_project_row) {
        out += " (1 project summary row uid " + std::to_string(result.project_uid) +
               " excluded)";
    }
    out += ", compared " + std::to_string(result.compared_tasks) + "\n";

    out += "resources: reader " + std::to_string(result.reader_resources) + ", xml " +
           std::to_string(result.xml_resources);
    if (result.xml_has_placeholder_resource) {
        out += " (1 placeholder resource uid 0 excluded)";
    }
    out += ", compared " + std::to_string(result.compared_resources) + " (type on " +
           std::to_string(result.compared_resource_types) + ", max units on " +
           std::to_string(result.compared_resource_max_units) + ")\n";

    out += "assignments: reader " + std::to_string(result.reader_assignments) + " (" +
           std::to_string(result.live_reader_assignments) + " live, " +
           std::to_string(result.dead_endpoint_assignments.size()) +
           " dead-endpoint excluded), xml " + std::to_string(result.xml_assignments) +
           ", compared " + std::to_string(result.compared_assignments) + " (units on " +
           std::to_string(result.compared_assignment_units) + ", work on " +
           std::to_string(result.compared_assignment_work) + ", start+finish on " +
           std::to_string(result.compared_assignment_dates) + ", resume on " +
           std::to_string(result.compared_assignment_resume) + ")\n";

    out += "links: reader " + std::to_string(result.reader_links) + " (" +
           std::to_string(result.live_reader_links) + " live, " +
           std::to_string(result.dead_endpoint_links.size()) +
           " dead-endpoint excluded), xml " + std::to_string(result.xml_links) +
           ", matched " + std::to_string(result.matched_links) + "\n";

    out += "fields: " + std::to_string(result.compared_fields) + " comparisons (" +
           std::to_string(kComparedFieldCount) +
           " per task: Start, Finish, Duration, Milestone, Summary, ConstraintType, "
           "ConstraintDate, Deadline, ActualStart, ActualFinish; Milestone is a note)\n";

    out += "uid differences: " + std::to_string(result.uid_differences()) + "\n";
    for (const std::uint32_t uid : result.reader_only_uids) {
        out += "  uid " + std::to_string(uid) + "  reader only\n";
    }
    for (const std::uint32_t uid : result.xml_only_uids) {
        out += "  uid " + std::to_string(uid) + "  xml only\n";
    }

    append_mismatch_section(out, "field mismatches", result.field_mismatches);

    out += "milestone notes: " + std::to_string(result.milestone_differences.size()) +
           " (the reader takes the stored FixedMeta bit when the row has one and falls "
           "back to duration == 0; Project's export carries the effective flag, so a "
           "disagreement is reported here and does not affect the verdict)\n";
    append_mismatch_rows(out, result.milestone_differences);

    out += "link differences: " + std::to_string(result.link_differences()) +
           " (reader only " + std::to_string(result.reader_only_links.size()) +
           ", xml only " + std::to_string(result.xml_only_links.size()) + ")\n";
    if (!result.reader_only_links.empty()) {
        out += "  reader-only links (predecessor, successor, type, lag_ticks):\n";
        for (const LinkRow& link : result.reader_only_links) {
            append_link_line(out, link);
        }
    }
    if (!result.xml_only_links.empty()) {
        out += "  xml-only links (predecessor, successor, type, lag_ticks):\n";
        for (const LinkRow& link : result.xml_only_links) {
            append_link_line(out, link);
        }
    }

    out += "dead-endpoint links excluded: " +
           std::to_string(result.dead_endpoint_links.size() +
                          result.xml_dead_endpoint_links.size()) +
           " (an endpoint uid is not a live task on either side; Project does not "
           "schedule or export such a link)\n";
    for (const LinkRow& link : result.dead_endpoint_links) {
        append_link_line(out, link);
    }
    for (const LinkRow& link : result.xml_dead_endpoint_links) {
        out += "    xml ";
        append_link_line(out, link);
    }

    out += "dead-endpoint assignments excluded: " +
           std::to_string(result.dead_endpoint_assignments.size() +
                          result.xml_dead_endpoint_assignments.size()) +
           " (the task uid is not a live task on either side; Project does not export "
           "such an assignment)\n";
    for (const AssignmentRow& row : result.dead_endpoint_assignments) {
        append_assignment_line(out, row);
    }
    for (const AssignmentRow& row : result.xml_dead_endpoint_assignments) {
        out += "    xml ";
        append_assignment_line(out, row);
    }

    out += "resource differences: " + std::to_string(result.resource_differences()) +
           " (reader only " + std::to_string(result.reader_only_resource_uids.size()) +
           ", xml only " + std::to_string(result.xml_only_resource_uids.size()) + ")\n";
    for (const std::uint32_t uid : result.reader_only_resource_uids) {
        out += "  resource " + std::to_string(uid) + "  reader only\n";
    }
    for (const std::uint32_t uid : result.xml_only_resource_uids) {
        out += "  resource " + std::to_string(uid) + "  xml only\n";
    }
    append_mismatch_section(out, "resource field mismatches",
                            result.resource_mismatches);

    out += "assignment differences: " +
           std::to_string(result.assignment_differences()) + " (reader only " +
           std::to_string(result.reader_only_assignments.size()) + ", xml only " +
           std::to_string(result.xml_only_assignments.size()) + ", field " +
           std::to_string(result.assignment_mismatches.size()) + ")\n";
    for (const AssignmentRow& row : result.reader_only_assignments) {
        out += "  reader only ";
        append_assignment_line(out, row);
    }
    for (const AssignmentRow& row : result.xml_only_assignments) {
        out += "  xml only ";
        append_assignment_line(out, row);
    }
    append_mismatch_rows(out, result.assignment_mismatches);

    if (!result.problems.empty()) {
        out += "problems:\n";
        for (const std::string& problem : result.problems) {
            out += "  " + problem + "\n";
        }
    }

    out += "counts: tasks " + std::to_string(result.compared_tasks) +
           " compared, live links " + std::to_string(result.matched_links) + "/" +
           std::to_string(result.live_xml_links) + " matched, assignments " +
           std::to_string(result.matched_assignments) + "/" +
           std::to_string(result.live_xml_assignments) + " matched, resources " +
           std::to_string(result.matched_resources) + "/" +
           std::to_string(result.xml_resources -
                          (result.xml_has_placeholder_resource ? std::size_t{1}
                                                              : std::size_t{0})) +
           " matched, " + std::to_string(result.field_mismatches.size()) +
           " field mismatches, " + std::to_string(result.uid_differences()) +
           " uid differences, " + std::to_string(result.link_differences()) +
           " link differences, " + std::to_string(result.assignment_differences()) +
           " assignment differences, " + std::to_string(result.resource_differences()) +
           " resource differences, " + std::to_string(result.note_count()) +
           " milestone notes -> " + (result.ok() ? "MATCH" : "MISMATCH") + "\n";
    return out;
}

std::string format_json(const Result& result, const std::string& mpp_path,
                        const std::string& xml_path) {
    std::string out = "{\n";
    out += "  \"mpp_path\": " + json_string(mpp_path) + ",\n";
    out += "  \"xml_path\": " + json_string(xml_path) + ",\n";
    out += "  \"project_row_uid\": ";
    out += result.xml_has_project_row ? std::to_string(result.project_uid) : "null";
    out += ",\n";
    out += "  \"reader_counts\": {\"tasks\": " + std::to_string(result.reader_tasks) +
           ", \"links\": " + std::to_string(result.reader_links) +
           ", \"resources\": " + std::to_string(result.reader_resources) +
           ", \"assignments\": " + std::to_string(result.reader_assignments) + "},\n";
    out += "  \"xml_counts\": {\"tasks\": " + std::to_string(result.xml_tasks) +
           ", \"links\": " + std::to_string(result.xml_links) +
           ", \"resources\": " + std::to_string(result.xml_resources) +
           ", \"assignments\": " + std::to_string(result.xml_assignments) + "},\n";
    out += "  \"live_counts\": {\"links\": " + std::to_string(result.live_reader_links) +
           ", \"assignments\": " + std::to_string(result.live_reader_assignments) +
           ", \"dead_endpoint_links\": " +
           std::to_string(result.dead_endpoint_links.size()) +
           ", \"dead_endpoint_assignments\": " +
           std::to_string(result.dead_endpoint_assignments.size()) + "},\n";
    out += "  \"compared\": {\"tasks\": " + std::to_string(result.compared_tasks) +
           ", \"fields\": " + std::to_string(result.compared_fields) +
           ", \"links\": " + std::to_string(result.matched_links) +
           ", \"resources\": " + std::to_string(result.compared_resources) +
           ", \"assignments\": " + std::to_string(result.compared_assignments) +
           ", \"assignment_units\": " + std::to_string(result.compared_assignment_units) +
           ", \"assignment_work\": " + std::to_string(result.compared_assignment_work) +
           ", \"assignment_dates\": " + std::to_string(result.compared_assignment_dates) +
           ", \"assignment_resume\": " + std::to_string(result.compared_assignment_resume) +
           ", \"resource_types\": " + std::to_string(result.compared_resource_types) +
           ", \"resource_max_units\": " +
           std::to_string(result.compared_resource_max_units) + "},\n";

    const auto write_uids = [&out](const char* name, const std::vector<std::uint32_t>& uids,
                                   bool last) {
        out += "  \"";
        out += name;
        out += "\": [";
        for (std::size_t i = 0; i < uids.size(); ++i) {
            out += (i == 0 ? "" : ", ") + std::to_string(uids[i]);
        }
        out += "]";
        out += last ? "\n" : ",\n";
    };
    write_uids("reader_only_uids", result.reader_only_uids, false);
    write_uids("xml_only_uids", result.xml_only_uids, false);
    write_uids("reader_only_resource_uids", result.reader_only_resource_uids, false);
    write_uids("xml_only_resource_uids", result.xml_only_resource_uids, false);

    const auto write_links = [&out](const char* name, const std::vector<LinkRow>& links,
                                    bool last) {
        out += "  \"";
        out += name;
        out += "\": [";
        for (std::size_t i = 0; i < links.size(); ++i) {
            out += i == 0 ? "\n    " : ",\n    ";
            out += "{\"pred_uid\": " + std::to_string(links[i].predecessor_uid);
            out += ", \"succ_uid\": " + std::to_string(links[i].successor_uid);
            out += ", \"type\": " + std::to_string(links[i].type);
            out += ", \"lag_ticks\": " + std::to_string(links[i].lag_ticks) + "}";
        }
        if (!links.empty()) {
            out += "\n  ";
        }
        out += "]";
        out += last ? "\n" : ",\n";
    };
    write_links("reader_only_links", result.reader_only_links, false);
    write_links("xml_only_links", result.xml_only_links, false);
    write_links("dead_endpoint_links", result.dead_endpoint_links, false);
    write_links("xml_dead_endpoint_links", result.xml_dead_endpoint_links, false);

    const auto write_assignments = [&out](const char* name,
                                          const std::vector<AssignmentRow>& rows,
                                          bool last) {
        out += "  \"";
        out += name;
        out += "\": [";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            out += i == 0 ? "\n    " : ",\n    ";
            out += "{\"uid\": " + std::to_string(rows[i].uid);
            out += ", \"task_uid\": " + std::to_string(rows[i].task_uid);
            out += ", \"resource_uid\": " + std::to_string(rows[i].resource_uid);
            if (rows[i].units_compared) {
                out += ", \"units\": " + json_string(format_percent(rows[i].units));
            }
            if (rows[i].work_compared) {
                out += ", \"work_ticks\": " + std::to_string(rows[i].work_ticks);
            }
            out += "}";
        }
        if (!rows.empty()) {
            out += "\n  ";
        }
        out += "]";
        out += last ? "\n" : ",\n";
    };
    write_assignments("reader_only_assignments", result.reader_only_assignments, false);
    write_assignments("xml_only_assignments", result.xml_only_assignments, false);
    write_assignments("dead_endpoint_assignments", result.dead_endpoint_assignments, false);
    write_assignments("xml_dead_endpoint_assignments", result.xml_dead_endpoint_assignments,
                      false);

    const auto write_mismatches = [&out](const char* name,
                                         const std::vector<FieldMismatch>& rows,
                                         bool last) {
        out += "  \"";
        out += name;
        out += "\": [";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            out += i == 0 ? "\n    " : ",\n    ";
            out += "{\"uid\": " + std::to_string(rows[i].uid);
            out += ", \"field\": " + json_string(rows[i].field);
            out += ", \"reader\": " + json_string(rows[i].reader_value);
            out += ", \"xml\": " + json_string(rows[i].xml_value) + "}";
        }
        if (!rows.empty()) {
            out += "\n  ";
        }
        out += "]";
        out += last ? "\n" : ",\n";
    };
    write_mismatches("field_mismatches", result.field_mismatches, false);
    write_mismatches("milestone_differences", result.milestone_differences, false);
    write_mismatches("assignment_mismatches", result.assignment_mismatches, false);

    out += "  \"problems\": [";
    for (std::size_t i = 0; i < result.problems.size(); ++i) {
        out += (i == 0 ? "" : ", ") + json_string(result.problems[i]);
    }
    out += "],\n";
    out += "  \"counts\": {\"uid_differences\": " +
           std::to_string(result.uid_differences()) +
           ", \"field_mismatches\": " + std::to_string(result.field_mismatches.size()) +
           ", \"link_differences\": " + std::to_string(result.link_differences()) +
           ", \"resource_differences\": " + std::to_string(result.resource_differences()) +
           ", \"assignment_differences\": " +
           std::to_string(result.assignment_differences()) +
           ", \"milestone_notes\": " + std::to_string(result.note_count()) +
           ", \"problems\": " + std::to_string(result.problems.size()) +
           ", \"total\": " + std::to_string(result.mismatch_count()) + "},\n";
    out += "  \"ok\": ";
    out += result.ok() ? "true" : "false";
    out += ",\n  \"strictly_ok\": ";
    out += result.strictly_ok() ? "true" : "false";
    out += "\n}\n";
    return out;
}

}  // namespace mspdi_cmp
