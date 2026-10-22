#include "mspdi.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "field_ref.h"
#include "value.h"
#include "xml.h"

namespace core::io {
namespace {

using core::view::DateValue;
using core::view::FieldId;
using core::view::RecordSet;
using core::view::Value;
using core::view::ValueKind;

enum class Kind : std::uint8_t { Text, Integer, Number, Bool, Date, Duration };

struct FieldSpec {
    const char* element;
    const char* field;
    Kind kind;
};

/* --------------------------------------------------------------------------
 * The curated MSPDI -> PjField map.  Every name here resolves in
 * analysis/field_metadata_all.tsv through core/model/field_table.cpp; a name
 * that stops resolving becomes a warning and the element is skipped rather
 * than written to a guessed id.
 *
 * Elements deliberately absent because the analysis has no named field for
 * them (they are reported as warnings, once per name):
 *   Task: DurationFormat, CalendarUID, ResumeValid, Recurring, OverAllocated,
 *         IsSubproject*, ExternalTask*, ACWP, CV, LevelAssignments,
 *         DurationFormat-related variance fields, Indicators
 *   Resource: EmailAddress, Active
 *   Assignment: ID (only TaskID/ResourceID have names)
 * -------------------------------------------------------------------------- */
constexpr FieldSpec kTaskFields[] = {
    {"ID", "ID", Kind::Integer},
    {"Name", "Name", Kind::Text},
    {"Type", "Type", Kind::Integer},
    {"WBS", "WBS", Kind::Text},
    {"OutlineNumber", "OutlineNumber", Kind::Text},
    {"OutlineLevel", "OutlineLevel", Kind::Integer},
    {"Priority", "Priority", Kind::Integer},
    {"Start", "Start", Kind::Date},
    {"Finish", "Finish", Kind::Date},
    {"ActualStart", "ActualStart", Kind::Date},
    {"ActualFinish", "ActualFinish", Kind::Date},
    {"ConstraintType", "ConstraintType", Kind::Integer},
    {"ConstraintDate", "ConstraintDate", Kind::Date},
    {"Deadline", "Deadline", Kind::Date},
    {"EarlyStart", "EarlyStart", Kind::Date},
    {"EarlyFinish", "EarlyFinish", Kind::Date},
    {"LateStart", "LateStart", Kind::Date},
    {"LateFinish", "LateFinish", Kind::Date},
    {"BaselineStart", "BaselineStart", Kind::Date},
    {"BaselineFinish", "BaselineFinish", Kind::Date},
    {"Resume", "Resume", Kind::Date},
    {"Date1", "Date1", Kind::Date},
    {"Duration", "Duration", Kind::Duration},
    {"RemainingDuration", "RemainingDuration", Kind::Duration},
    {"ActualDuration", "ActualDuration", Kind::Duration},
    {"BaselineDuration", "BaselineDuration", Kind::Duration},
    {"LevelingDelay", "LevelingDelay", Kind::Duration},
    {"TotalSlack", "TotalSlack", Kind::Duration},
    {"FreeSlack", "FreeSlack", Kind::Duration},
    {"Work", "Work", Kind::Duration},
    {"RemainingWork", "RemainingWork", Kind::Duration},
    {"ActualWork", "ActualWork", Kind::Duration},
    {"BaselineWork", "BaselineWork", Kind::Duration},
    {"OvertimeWork", "OvertimeWork", Kind::Duration},
    {"ActualOvertimeWork", "ActualOvertimeWork", Kind::Duration},
    {"RemainingOvertimeWork", "RemainingOvertimeWork", Kind::Duration},
    {"RegularWork", "RegularWork", Kind::Duration},
    {"PercentComplete", "PercentComplete", Kind::Integer},
    {"PercentWorkComplete", "PercentWorkComplete", Kind::Integer},
    {"FixedCostAccrual", "FixedCostAccrual", Kind::Integer},
    {"Cost", "Cost", Kind::Number},
    {"FixedCost", "FixedCost", Kind::Number},
    {"ActualCost", "ActualCost", Kind::Number},
    {"RemainingCost", "RemainingCost", Kind::Number},
    {"BaselineCost", "BaselineCost", Kind::Number},
    {"OvertimeCost", "OvertimeCost", Kind::Number},
    {"ActualOvertimeCost", "ActualOvertimeCost", Kind::Number},
    {"RemainingOvertimeCost", "RemainingOvertimeCost", Kind::Number},
    {"Cost1", "Cost1", Kind::Number},
    {"Number1", "Number1", Kind::Number},
    {"Milestone", "Milestone", Kind::Bool},
    {"Summary", "Summary", Kind::Bool},
    {"Critical", "Critical", Kind::Bool},
    {"Estimated", "Estimated", Kind::Bool},
    {"EffortDriven", "EffortDriven", Kind::Bool},
    {"IgnoreResourceCalendar", "IgnoreResourceCalendar", Kind::Bool},
    {"LevelingCanSplit", "LevelingCanSplit", Kind::Bool},
    {"Active", "Active", Kind::Bool},
    {"LinkedFields", "LinkedFields", Kind::Bool},
    {"Flag1", "Flag1", Kind::Bool},
    {"Notes", "Notes", Kind::Text},
    {"Hyperlink", "Hyperlink", Kind::Text},
    {"HyperlinkAddress", "HyperlinkAddress", Kind::Text},
    {"Text1", "Text1", Kind::Text},
};

constexpr FieldSpec kResourceFields[] = {
    {"ID", "ID", Kind::Integer},
    {"Name", "Name", Kind::Text},
    {"Type", "Type", Kind::Integer},
    {"Initials", "Initials", Kind::Text},
    {"Group", "Group", Kind::Text},
    {"MaxUnits", "MaxUnits", Kind::Number},
    {"StandardRate", "StandardRate", Kind::Number},
    {"OvertimeRate", "OvertimeRate", Kind::Number},
    {"CostPerUse", "CostPerUse", Kind::Number},
    {"Notes", "Notes", Kind::Text},
    {"MaterialLabel", "MaterialLabel", Kind::Text},
    {"AccrueAt", "AccrueAt", Kind::Integer},
    {"Cost1", "Cost1", Kind::Number},
    {"Number1", "Number1", Kind::Number},
    {"Text1", "Text1", Kind::Text},
    {"Flag1", "Flag1", Kind::Bool},
    {"Date1", "Date1", Kind::Date},
};

constexpr FieldSpec kAssignmentFields[] = {
    {"TaskID", "TaskID", Kind::Integer},
    {"ResourceID", "ResourceID", Kind::Integer},
    {"TaskName", "TaskName", Kind::Text},
    {"ResourceName", "ResourceName", Kind::Text},
    {"Units", "Units", Kind::Number},
    {"Work", "Work", Kind::Duration},
    {"ActualWork", "ActualWork", Kind::Duration},
    {"RemainingWork", "RemainingWork", Kind::Duration},
    {"Cost", "Cost", Kind::Number},
    {"ActualCost", "ActualCost", Kind::Number},
    {"RemainingCost", "RemainingCost", Kind::Number},
    {"Cost1", "Cost1", Kind::Number},
    {"Number1", "Number1", Kind::Number},
    {"Start", "Start", Kind::Date},
    {"Finish", "Finish", Kind::Date},
    {"ActualStart", "ActualStart", Kind::Date},
    {"ActualFinish", "ActualFinish", Kind::Date},
    {"PercentWorkComplete", "PercentWorkComplete", Kind::Integer},
    {"Notes", "Notes", Kind::Text},
    {"Text1", "Text1", Kind::Text},
};

/* Standard MSPDI elements that carry no view-visible value for us yet and are
 * pure boilerplate or scheduling output: importing them would invent derived
 * state that the scheduling layer owns.  Every OTHER unmapped element produces
 * one warning, so "warned" means "not imported and possibly relevant". */
constexpr const char* kIgnoredTaskElements[] = {
    "IsNull",       "CreateDate",       "Manual",       "ManualStart",
    "ManualFinish", "IsSubproject",     "IsSubprojectReadOnly",
    "ExternalTask", "ExternalTaskProject", "SubprojectName", "SubprojectReadOnly",
    "Subproject",   "StartVariance",    "FinishVariance", "WorkVariance",
    "CostVariance", "StartSlack",       "FinishSlack",
};

constexpr const char* kIgnoredProjectElements[] = {
    "SaveVersion", "BuildNumber", "NewProject", "GUID", "LastSaved",
    "CreationDate", "FYStartDate", "CriticalSlackLimit", "CurrencyDigits",
    "CurrencySymbol", "CurrencyCode", "CurrencySymbolPosition",
    "DefaultTaskType", "DefaultFixedCostAccrual", "DefaultStandardRate",
    "DefaultOvertimeRate", "DefaultTaskEVMethod", "DefaultCostAccrual",
    "WorkFormat", "DurationFormat", "WeekStartDay", "ShowProjectSummaryTask",
    "ExtendedAttributes", "Calendars", "WBSMasks", "TaskLinks",
};

class Warnings {
  public:
    explicit Warnings(std::vector<std::string>& out) : out_(out) {}

    void add(const std::string& message) { out_.push_back(message); }

    /* One message per stable key, even if the element occurs on every task. */
    void once(const std::string& key, const std::string& message) {
        if (seen_.insert(key).second) {
            out_.push_back(message);
        }
    }

  private:
    std::vector<std::string>& out_;
    std::set<std::string> seen_;
};

bool is_ignored(const char* name, const char* const* list, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (std::string_view(name) == list[i]) {
            return true;
        }
    }
    return false;
}

template <std::size_t N>
bool is_ignored(const char* name, const char* const (&list)[N]) {
    return is_ignored(name, list, N);
}

bool parse_uint32(std::string_view text, std::uint32_t& out) noexcept {
    unsigned long long value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value > 0xFFFFFFFFull) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

/* Project's export writes <ResourceUID>-65535</ResourceUID> for "no resource".
 * That is a signed 32-bit value whose two's-complement bits are the reader's
 * kNullResourceUid (0xFFFF0001); accepting both signed and unsigned spellings
 * and keeping the raw bits is what lets the other code path compare like with
 * like.  A real resource UID is positive and unaffected. */
bool parse_uid_bits(std::string_view text, std::uint32_t& out) noexcept {
    std::int64_t value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end ||
        value < -2147483648LL || value > 4294967295LL) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool parse_int(std::string_view text, int& out) noexcept {
    int value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    out = value;
    return true;
}

bool parse_int64(std::string_view text, std::int64_t& out) noexcept {
    std::int64_t value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    out = value;
    return true;
}

bool parse_double(std::string_view text, double& out) noexcept {
    double value = 0.0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    out = value;
    return true;
}

/* Fills `value` from the element's trimmed text.  Empty text is Null. */
bool parse_value(const XmlElement& element, Kind kind, Value& value, std::string& error) {
    const std::string text = text_of(element);
    if (text.empty()) {
        value = Value{};
        return true;
    }
    switch (kind) {
        case Kind::Text:
            value = Value::of_text(text);
            return true;
        case Kind::Integer: {
            std::int64_t parsed = 0;
            const char* begin = text.data();
            const char* end = begin + text.size();
            const auto result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end) {
                error = "expected an integer, got \"" + text + "\"";
                return false;
            }
            value = Value::of_int(parsed);
            return true;
        }
        case Kind::Number: {
            double parsed = 0.0;
            if (!parse_double(text, parsed)) {
                error = "expected a number, got \"" + text + "\"";
                return false;
            }
            value = Value::of_number(parsed);
            return true;
        }
        case Kind::Bool: {
            bool parsed = false;
            if (!parse_bool_text(text, parsed)) {
                error = "expected 0/1, got \"" + text + "\"";
                return false;
            }
            value = Value::of_bool(parsed);
            return true;
        }
        case Kind::Date: {
            DateValue parsed;
            if (!core::view::parse_iso_datetime(text, parsed)) {
                error = "expected an ISO date/time, got \"" + text + "\"";
                return false;
            }
            value = Value::of_date(parsed);
            return true;
        }
        case Kind::Duration: {
            std::int64_t ticks = 0;
            if (!core::view::parse_iso_duration(text, ticks)) {
                error = "expected an ISO duration, got \"" + text + "\"";
                return false;
            }
            value = Value::of_duration(ticks);
            return true;
        }
    }
    error = "internal: unknown value kind";
    return false;
}

/* MSPDI writes a resource's MaxUnits and an assignment's Units as a FRACTION
 * (1.0 == 100 %), while the shared model uses percent (the .mpp reader's
 * units()/max_units() are 100.0 at 100 %).  The two fields are the only such
 * pair; everything else is already in model units.  MEASURED: Project's own
 * exports of PROJECT_A/PROJECT_B/assign1 write `<Units>1</Units>` and
 * `<MaxUnits>1.00</MaxUnits>` for assignments/resources the reader reports at
 * 100 % (assign1 is the real-resource fixture).
 *
 * Import therefore scales the fraction to percent, and the writer scales back;
 * the RecordSet a caller sees always uses percent. */
bool is_percent_fraction_field(core::model::RecordType record_type,
                               std::string_view element) {
    return (record_type == core::model::RecordType::Assignment && element == "Units") ||
           (record_type == core::model::RecordType::Resource && element == "MaxUnits");
}

struct ResolvedSpec {
    std::string element;
    FieldId field = 0;
    Kind kind = Kind::Text;
};

std::vector<ResolvedSpec> resolve(const FieldSpec* specs, std::size_t count,
                                  core::model::RecordType record_type,
                                  Warnings& warnings) {
    std::vector<ResolvedSpec> resolved;
    resolved.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        FieldId field = 0;
        if (!core::view::field_id_by_name(record_type, specs[i].field, field)) {
            warnings.once(std::string("missing:") + specs[i].field,
                          std::string("descriptor table has no named ") +
                              core::model::record_type_name(record_type) + " field \"" +
                              specs[i].field + "\"; element <" + specs[i].element +
                              "> is not imported");
            continue;
        }
        resolved.push_back({specs[i].element, field, specs[i].kind});
    }
    return resolved;
}

void add_schema(RecordSet& records, const std::vector<ResolvedSpec>& specs) {
    for (const auto& spec : specs) {
        records.add_field(spec.field);
    }
}

const ResolvedSpec* find_spec(const std::vector<ResolvedSpec>& specs,
                              std::string_view element) {
    for (const auto& spec : specs) {
        if (spec.element == element) {
            return &spec;
        }
    }
    return nullptr;
}

std::uint32_t read_uid(const XmlElement& record, Warnings& warnings,
                       const std::string& label, std::uint32_t fallback) {
    const XmlElement* uid = child(record, "UID");
    if (uid == nullptr) {
        warnings.add(label + ": no <UID>; using " + std::to_string(fallback));
        return fallback;
    }
    std::uint32_t parsed = 0;
    const std::string text = text_of(*uid);
    if (!parse_uint32(text, parsed)) {
        warnings.add(label + ": <UID> is not a 32-bit integer (\"" + text + "\"); using " +
                     std::to_string(fallback));
        return fallback;
    }
    return parsed;
}

void import_task_fields(const XmlElement& element, RecordSet& records, std::size_t row,
                        const std::vector<ResolvedSpec>& specs, const std::string& label,
                        Warnings& warnings) {
    for (const auto& attribute : element.children) {
        if (attribute.name == "UID" || attribute.name == "PredecessorLink") {
            continue;
        }
        const ResolvedSpec* spec = find_spec(specs, attribute.name);
        if (spec == nullptr) {
            if (is_ignored(attribute.name.c_str(), kIgnoredTaskElements)) {
                continue;
            }
            warnings.once("unsupported:Task:" + attribute.name,
                          "unsupported Task element <" + attribute.name +
                              "> (no named Task field; ignored)");
            continue;
        }
        Value value;
        std::string error;
        if (!parse_value(attribute, spec->kind, value, error)) {
            warnings.add(label + "/" + attribute.name + ": " + error);
            continue;
        }
        if (!value.is_null()) {
            records.set_field(row, spec->field, std::move(value));
        }
    }
}

void import_resource_fields(const XmlElement& element, RecordSet& records, std::size_t row,
                            const std::vector<ResolvedSpec>& specs, const std::string& label,
                            Warnings& warnings) {
    for (const auto& attribute : element.children) {
        if (attribute.name == "UID") {
            continue;
        }
        const ResolvedSpec* spec = find_spec(specs, attribute.name);
        if (spec == nullptr) {
            warnings.once("unsupported:Resource:" + attribute.name,
                          "unsupported Resource element <" + attribute.name +
                              "> (no named Resource field; ignored)");
            continue;
        }
        Value value;
        std::string error;
        if (!parse_value(attribute, spec->kind, value, error)) {
            warnings.add(label + "/" + attribute.name + ": " + error);
            continue;
        }
        if (value.kind == ValueKind::Number &&
            is_percent_fraction_field(records.record_type(), attribute.name)) {
            value.number *= 100.0; /* MSPDI fraction -> model percent */
        }
        if (!value.is_null()) {
            records.set_field(row, spec->field, std::move(value));
        }
    }
}

void import_assignment_fields(const XmlElement& element, RecordSet& records, std::size_t row,
                              const std::vector<ResolvedSpec>& specs,
                              const std::string& label, Warnings& warnings) {
    for (const auto& attribute : element.children) {
        if (attribute.name == "UID" || attribute.name == "TaskUID" ||
            attribute.name == "ResourceUID" || attribute.name == "Stop" ||
            attribute.name == "Resume") {
            /* Stop/Resume have no Assignment descriptor field; they are carried
             * in MspdiAssignmentRef for the cross-check instead of warned about
             * as unsupported. */
            continue;
        }
        const ResolvedSpec* spec = find_spec(specs, attribute.name);
        if (spec == nullptr) {
            warnings.once("unsupported:Assignment:" + attribute.name,
                          "unsupported Assignment element <" + attribute.name +
                              "> (no named Assignment field; ignored)");
            continue;
        }
        Value value;
        std::string error;
        if (!parse_value(attribute, spec->kind, value, error)) {
            warnings.add(label + "/" + attribute.name + ": " + error);
            continue;
        }
        if (value.kind == ValueKind::Number &&
            is_percent_fraction_field(records.record_type(), attribute.name)) {
            value.number *= 100.0; /* MSPDI fraction -> model percent */
        }
        if (!value.is_null()) {
            records.set_field(row, spec->field, std::move(value));
        }
    }
}

void parse_predecessor_link(const XmlElement& element, std::uint32_t successor_uid,
                            std::vector<PredecessorLink>& links, const std::string& label,
                            Warnings& warnings) {
    PredecessorLink link;
    link.successor_uid = successor_uid;
    const XmlElement* predecessor = child(element, "PredecessorUID");
    if (predecessor == nullptr) {
        warnings.add(label + "/PredecessorLink: no <PredecessorUID>; link ignored");
        return;
    }
    if (!parse_uint32(text_of(*predecessor), link.predecessor_uid)) {
        warnings.add(label + "/PredecessorLink: invalid <PredecessorUID> \"" +
                     text_of(*predecessor) + "\"; link ignored");
        return;
    }
    if (const XmlElement* type = child(element, "Type")) {
        if (!parse_int(text_of(*type), link.type)) {
            warnings.add(label + "/PredecessorLink/Type: expected an integer");
        }
    }
    if (const XmlElement* lag = child(element, "LinkLag")) {
        const std::string text = text_of(*lag);
        std::int64_t ticks = 0;
        const LinkLagSpelling spelling = parse_link_lag(text, ticks);
        if (spelling != LinkLagSpelling::none) {
            link.lag_ticks = ticks;
        } else {
            warnings.add(label +
                         "/PredecessorLink/LinkLag: expected tenths of a minute or an ISO "
                         "duration, got \"" +
                         text + "\"");
        }
    }
    if (const XmlElement* format = child(element, "LagFormat")) {
        if (!parse_int(text_of(*format), link.lag_format)) {
            warnings.add(label + "/PredecessorLink/LagFormat: expected an integer");
        }
    }
    links.push_back(std::move(link));
}

void import_tasks(const XmlElement& container, MspdiDocument& document, Warnings& warnings,
                  const std::vector<ResolvedSpec>& specs) {
    std::uint32_t fallback = 1;
    for (const XmlElement* task : children(container, "Task")) {
        const std::uint32_t uid = read_uid(*task, warnings, "Task", fallback);
        const std::string label = "Task[UID=" + std::to_string(uid) + "]";
        if (document.tasks.row_of_uid(uid) >= 0) {
            warnings.add(label + ": duplicate UID; row kept");
        }
        const std::size_t row = document.tasks.add_row(uid);
        import_task_fields(*task, document.tasks, row, specs, label, warnings);
        for (const auto& element : task->children) {
            if (element.name == "PredecessorLink") {
                parse_predecessor_link(element, uid, document.links, label, warnings);
            }
        }
        ++fallback;
    }
}

void import_resources(const XmlElement& container, MspdiDocument& document, Warnings& warnings,
                      const std::vector<ResolvedSpec>& specs) {
    std::uint32_t fallback = 1;
    for (const XmlElement* resource : children(container, "Resource")) {
        const std::uint32_t uid = read_uid(*resource, warnings, "Resource", fallback);
        const std::string label = "Resource[UID=" + std::to_string(uid) + "]";
        if (document.resources.row_of_uid(uid) >= 0) {
            warnings.add(label + ": duplicate UID; row kept");
        }
        const std::size_t row = document.resources.add_row(uid);
        import_resource_fields(*resource, document.resources, row, specs, label, warnings);
        ++fallback;
    }
}

void read_assignment_date(const XmlElement& assignment, const char* name,
                          core::view::Value& out, const std::string& label,
                          Warnings& warnings) {
    const XmlElement* element = child(assignment, name);
    if (element == nullptr) {
        return; /* absent is not an error */
    }
    const std::string text = text_of(*element);
    if (text.empty()) {
        return;
    }
    core::view::DateValue date;
    if (!core::view::parse_iso_datetime(text, date)) {
        warnings.add(label + "/" + name + ": expected an ISO date/time, got \"" + text +
                     "\"");
        return;
    }
    out = Value::of_date(date);
}

void import_assignments(const XmlElement& container, MspdiDocument& document,
                        Warnings& warnings, const std::vector<ResolvedSpec>& specs) {
    std::uint32_t fallback = 1;
    for (const XmlElement* assignment : children(container, "Assignment")) {
        const std::uint32_t uid = read_uid(*assignment, warnings, "Assignment", fallback);
        const std::string label = "Assignment[UID=" + std::to_string(uid) + "]";
        MspdiAssignmentRef ref;
        ref.uid = uid;
        read_assignment_date(*assignment, "Start", ref.start, label, warnings);
        read_assignment_date(*assignment, "Finish", ref.finish, label, warnings);
        read_assignment_date(*assignment, "Stop", ref.stop, label, warnings);
        read_assignment_date(*assignment, "Resume", ref.resume, label, warnings);
        if (const XmlElement* task_uid = child(*assignment, "TaskUID")) {
            if (!parse_uint32(text_of(*task_uid), ref.task_uid)) {
                warnings.add(label + "/TaskUID: expected a 32-bit integer");
            }
        } else {
            warnings.add(label + ": no <TaskUID>");
        }
        if (const XmlElement* resource_uid = child(*assignment, "ResourceUID")) {
            if (!parse_uid_bits(text_of(*resource_uid), ref.resource_uid)) {
                warnings.add(label + "/ResourceUID: expected a 32-bit integer");
            }
        } else {
            warnings.add(label + ": no <ResourceUID>");
        }
        if (document.assignments.row_of_uid(uid) >= 0) {
            warnings.add(label + ": duplicate UID; row kept");
        }
        if (ref.task_uid != 0 && document.tasks.row_of_uid(ref.task_uid) < 0) {
            warnings.add(label + ": TaskUID " + std::to_string(ref.task_uid) +
                         " names no imported task");
        }
        if (ref.resource_uid != 0 && ref.resource_uid != 0xFFFF0001u &&
            document.resources.row_of_uid(ref.resource_uid) < 0) {
            warnings.add(label + ": ResourceUID " + std::to_string(ref.resource_uid) +
                         " names no imported resource");
        }
        const std::size_t row = document.assignments.add_row(uid);
        import_assignment_fields(*assignment, document.assignments, row, specs, label,
                                 warnings);
        document.assignment_refs.push_back(ref);
        ++fallback;
    }
}

void import_project_header(const XmlElement& project, MspdiProject& header, Warnings& warnings) {
    for (const auto& element : project.children) {
        if (element.name == "Name") {
            header.name = text_of(element);
        } else if (element.name == "Title") {
            header.title = text_of(element);
        } else if (element.name == "StartDate" || element.name == "FinishDate") {
            DateValue date;
            if (core::view::parse_iso_datetime(text_of(element), date)) {
                if (element.name == "StartDate") {
                    header.start = Value::of_date(date);
                } else {
                    header.finish = Value::of_date(date);
                }
            } else if (!text_of(element).empty()) {
                warnings.add("Project/" + element.name + ": expected an ISO date/time, got \"" +
                             text_of(element) + "\"");
            }
        } else if (element.name == "ScheduleFromStart") {
            bool parsed = false;
            if (parse_bool_text(text_of(element), parsed)) {
                header.schedule_from_start = parsed;
                header.has_schedule_from_start = true;
            } else {
                warnings.add("Project/ScheduleFromStart: expected 0/1, got \"" +
                             text_of(element) + "\"");
            }
        } else if (element.name == "MinutesPerDay" || element.name == "MinutesPerWeek" ||
                   element.name == "DaysPerMonth" || element.name == "CalendarUID") {
            int parsed = 0;
            if (parse_int(text_of(element), parsed)) {
                if (element.name == "MinutesPerDay") {
                    header.minutes_per_day = parsed;
                } else if (element.name == "MinutesPerWeek") {
                    header.minutes_per_week = parsed;
                } else if (element.name == "DaysPerMonth") {
                    header.days_per_month = parsed;
                } else {
                    header.calendar_uid = parsed;
                }
            } else if (!text_of(element).empty()) {
                warnings.add("Project/" + element.name + ": expected an integer, got \"" +
                             text_of(element) + "\"");
            }
        } else if (element.name == "DefaultStartTime") {
            header.default_start_time = text_of(element);
        } else if (element.name == "DefaultFinishTime") {
            header.default_finish_time = text_of(element);
        } else if (element.name == "Tasks" || element.name == "Resources" ||
                   element.name == "Assignments") {
            continue;
        } else if (!is_ignored(element.name.c_str(), kIgnoredProjectElements)) {
            warnings.once("unsupported:Project:" + element.name,
                          "unsupported Project element <" + element.name + "> (ignored)");
        }
    }
}

std::string read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw MspdiError("cannot open \"" + path + "\"");
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

}  // namespace

MspdiDocument import_mspdi(std::string_view xml, const MspdiLoadOptions& options) {
    const XmlParseResult parsed = parse_xml(xml);
    if (!parsed.ok) {
        throw MspdiError("MSPDI XML line " + std::to_string(parsed.line) + ": " +
                         parsed.error);
    }
    if (parsed.root.name != "Project") {
        throw MspdiError("MSPDI root element is <" + parsed.root.name +
                         ">, expected <Project>");
    }

    MspdiDocument document;
    Warnings warnings(document.warnings);
    import_project_header(parsed.root, document.project, warnings);

    const auto task_specs =
        resolve(kTaskFields, std::size(kTaskFields), core::model::RecordType::Task, warnings);
    const auto resource_specs = resolve(kResourceFields, std::size(kResourceFields),
                                        core::model::RecordType::Resource, warnings);
    const auto assignment_specs = resolve(kAssignmentFields, std::size(kAssignmentFields),
                                          core::model::RecordType::Assignment, warnings);
    add_schema(document.tasks, task_specs);
    add_schema(document.resources, resource_specs);
    add_schema(document.assignments, assignment_specs);

    if (const XmlElement* tasks = child(parsed.root, "Tasks")) {
        import_tasks(*tasks, document, warnings, task_specs);
    }
    if (options.import_resources) {
        if (const XmlElement* resources = child(parsed.root, "Resources")) {
            import_resources(*resources, document, warnings, resource_specs);
        }
    }
    if (options.import_assignments) {
        if (const XmlElement* assignments = child(parsed.root, "Assignments")) {
            import_assignments(*assignments, document, warnings, assignment_specs);
        }
    }
    return document;
}

MspdiDocument import_mspdi_file(const std::string& path, const MspdiLoadOptions& options) {
    return import_mspdi(read_file(path), options);
}

/* ---- LinkLag spelling.  The canonical rule and its evidence live in mspdi.h;
 * the importer above and the writer both go through these functions, so the two
 * spellings cannot drift apart. */
LinkLagSpelling parse_link_lag(std::string_view text, std::int64_t& ticks) {
    std::int64_t parsed = 0;
    if (parse_int64(text, parsed)) {
        ticks = parsed; /* tenths of a minute == 6-second ticks */
        return LinkLagSpelling::decimal;
    }
    if (core::view::parse_iso_duration(text, parsed)) {
        ticks = parsed;
        return LinkLagSpelling::iso;
    }
    return LinkLagSpelling::none;
}

std::string format_link_lag_decimal(std::int64_t ticks) { return std::to_string(ticks); }

std::string format_link_lag_iso(std::int64_t ticks) {
    return core::view::format_duration(ticks);
}

}  // namespace core::io
