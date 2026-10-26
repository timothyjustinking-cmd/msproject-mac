// app/field_link_edits.cpp -- see field_link_edits.h for the contract.
//
// No AppKit, no file I/O: the two dialog models are plain functions so the
// headless tests can prove the parse/validation/formatting contract on Windows.

#include "field_link_edits.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "add_delete.h"
#include "columns.h"
#include "field_ref.h"
#include "field_value.h"
#include "value.h"

namespace app {
namespace {

std::string trim_text(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
            text[begin] == '\n')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string lowercase_ascii(std::string text) {
    for (char& character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return text;
}

TaskFieldSpec make_spec(mpp::FieldKind kind, unsigned index,
                        core::sheet::CellKind cell_kind) {
    TaskFieldSpec spec;
    spec.field_id = mpp::custom_field_id(kind, index);
    spec.kind = kind;
    spec.cell_kind = cell_kind;
    if (spec.field_id != 0) {
        spec.title = core::view::field_title(spec.field_id, core::model::RecordType::Task);
    }
    if (spec.title.empty()) {
        spec.title = "0x" + std::to_string(spec.field_id);
    }
    return spec;
}

const TaskRow* find_task(const PlanInput& plan, std::uint32_t uid) {
    for (const TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

bool has_pending_delete(const StructuralEdits& edits, std::uint32_t uid) noexcept {
    for (const StructuralEdit& edit : edits.applied()) {
        if (edit.kind == StructuralKind::delete_task && edit.uid == uid) {
            return true;
        }
    }
    return false;
}

bool has_pending_link_add(const StructuralEdits& edits, std::uint32_t predecessor_uid,
                          std::uint32_t successor_uid,
                          core::gantt::LinkType type) noexcept {
    for (const StructuralEdit& edit : edits.applied()) {
        if (edit.kind == StructuralKind::add_link &&
            edit.predecessor_uid == predecessor_uid && edit.uid == successor_uid &&
            edit.link_type == type) {
            return true;
        }
    }
    return false;
}

// The current stored value of a field, or nullptr.
const mpp::CustomFieldValue* stored_value(const TaskRow& row, std::uint32_t field_id) {
    const mpp::CustomFieldValue* value = row.custom_field(field_id);
    if (value == nullptr || !value->present) {
        return nullptr;
    }
    return value;
}

bool value_matches(const mpp::CustomFieldValue& current, mpp::FieldKind kind,
                   const core::view::Value& value, std::uint32_t packed_date) {
    if (!current.storage_measured) {
        return false;
    }
    switch (kind) {
        case mpp::FieldKind::text:
            return value.kind == core::view::ValueKind::Text && current.text == value.text;
        case mpp::FieldKind::number:
            return current.number == value.number;
        case mpp::FieldKind::date:
            return current.packed_date == packed_date;
        case mpp::FieldKind::duration:
            return current.duration_ticks == static_cast<std::uint32_t>(value.integer);
        case mpp::FieldKind::flag:
            return current.flag == value.boolean;
        case mpp::FieldKind::cost:
        case mpp::FieldKind::unknown:
            return false;
    }
    return false;
}

}  // namespace

const std::vector<TaskFieldSpec>& task_field_specs() {
    static const std::vector<TaskFieldSpec> specs = [] {
        std::vector<TaskFieldSpec> out;
        out.push_back(make_spec(mpp::FieldKind::text, 1, core::sheet::CellKind::Text));
        out.push_back(make_spec(mpp::FieldKind::number, 1, core::sheet::CellKind::Number));
        out.push_back(make_spec(mpp::FieldKind::number, 2, core::sheet::CellKind::Number));
        out.push_back(make_spec(mpp::FieldKind::date, 1, core::sheet::CellKind::Date));
        out.push_back(make_spec(mpp::FieldKind::duration, 1,
                                core::sheet::CellKind::Duration));
        out.push_back(make_spec(mpp::FieldKind::flag, 1, core::sheet::CellKind::Bool));
        return out;
    }();
    return specs;
}

const TaskFieldSpec* task_field_spec(std::uint32_t field_id) noexcept {
    for (const TaskFieldSpec& spec : task_field_specs()) {
        if (spec.field_id == field_id) {
            return &spec;
        }
    }
    return nullptr;
}

std::string field_value_text(const TaskRow& row, std::uint32_t field_id,
                             const core::sheet::SheetFormat& format) {
    const TaskFieldSpec* spec = task_field_spec(field_id);
    if (spec == nullptr) {
        return {};
    }
    const mpp::CustomFieldValue* stored = row.custom_field(field_id);
    if (stored == nullptr || !stored->present || !stored->storage_measured) {
        return {};
    }
    return core::sheet::format_value_text(value_of_custom_field(*stored), spec->cell_kind,
                                          format);
}

bool make_task_field_edits(const TaskRow& row, const std::vector<TaskFieldInput>& inputs,
                           const core::sheet::SheetFormat& format,
                           std::vector<StructuralEdit>& out, std::string& error) {
    out.clear();
    error.clear();
    if (row.uid == 0) {
        error = "the task has no UniqueID";
        return false;
    }
    // Build into a local list and publish it only on success: a form that fails
    // on its last field must queue nothing at all.
    std::vector<StructuralEdit> pending;
    for (const TaskFieldInput& input : inputs) {
        const TaskFieldSpec* spec = task_field_spec(input.field_id);
        if (spec == nullptr) {
            error = "field id " + std::to_string(input.field_id) +
                    " is not one of the six measured custom fields";
            return false;
        }
        std::string text = trim_text(input.text);
        const mpp::CustomFieldValue* current = stored_value(row, input.field_id);
        if (text.empty()) {
            const bool meaningful =
                current != nullptr &&
                (spec->kind != mpp::FieldKind::flag || current->flag);
            if (!meaningful) {
                continue;  // nothing stored: clearing an absent field is a no-op
            }
            StructuralEdit edit;
            edit.kind = StructuralKind::clear_field;
            edit.uid = row.uid;
            edit.field_id = input.field_id;
            edit.field_kind = spec->kind;
            pending.push_back(std::move(edit));
            continue;
        }
        const core::sheet::EditOutcome parsed =
            core::sheet::parse_edit_text(text, spec->cell_kind, format);
        if (!parsed.ok) {
            error = spec->title + ": " + parsed.error;
            return false;
        }
        StructuralEdit edit;
        edit.kind = StructuralKind::set_field;
        edit.uid = row.uid;
        edit.field_id = input.field_id;
        edit.field_kind = spec->kind;
        edit.field_value.field_id = input.field_id;
        edit.field_value.kind = spec->kind;
        std::uint32_t packed_date = 0;
        switch (spec->kind) {
            case mpp::FieldKind::text:
                if (parsed.value.kind != core::view::ValueKind::Text) {
                    error = spec->title + ": expected text";
                    return false;
                }
                edit.field_value.text = parsed.value.text;
                break;
            case mpp::FieldKind::number:
                if (parsed.value.kind == core::view::ValueKind::Integer) {
                    edit.field_value.number =
                        static_cast<double>(parsed.value.integer);
                } else if (parsed.value.kind == core::view::ValueKind::Number) {
                    edit.field_value.number = parsed.value.number;
                } else {
                    error = spec->title + ": expected a number";
                    return false;
                }
                if (!std::isfinite(edit.field_value.number)) {
                    error = spec->title + ": the number is not finite";
                    return false;
                }
                break;
            case mpp::FieldKind::date:
                if (parsed.value.kind != core::view::ValueKind::Date) {
                    error = spec->title + ": expected a date";
                    return false;
                }
                packed_date = packed_from_date32(date32_from_date_value(parsed.value.date));
                edit.field_value.packed_date = packed_date;
                break;
            case mpp::FieldKind::duration:
                if (parsed.value.kind != core::view::ValueKind::Duration) {
                    error = spec->title + ": expected a duration";
                    return false;
                }
                edit.field_value.duration_ticks =
                    static_cast<std::uint32_t>(parsed.value.integer);
                break;
            case mpp::FieldKind::flag:
                if (parsed.value.kind != core::view::ValueKind::Bool) {
                    error = spec->title + ": expected Yes or No";
                    return false;
                }
                edit.field_value.flag = parsed.value.boolean;
                break;
            case mpp::FieldKind::cost:
            case mpp::FieldKind::unknown:
                error = spec->title + " cannot be written";
                return false;
        }
        if (current != nullptr && value_matches(*current, spec->kind, parsed.value,
                                               packed_date)) {
            continue;  // unchanged: no pending edit
        }
        pending.push_back(std::move(edit));
    }
    out = std::move(pending);
    return true;
}

const char* link_type_name(core::gantt::LinkType type) noexcept {
    switch (type) {
        case core::gantt::LinkType::finish_to_finish: return "FF";
        case core::gantt::LinkType::finish_to_start: return "FS";
        case core::gantt::LinkType::start_to_finish: return "SF";
        case core::gantt::LinkType::start_to_start: return "SS";
    }
    return "FS";
}

bool parse_link_type(std::string_view text, core::gantt::LinkType& out) noexcept {
    const std::string lowered = lowercase_ascii(trim_text(text));
    if (lowered == "ff") {
        out = core::gantt::LinkType::finish_to_finish;
    } else if (lowered == "fs") {
        out = core::gantt::LinkType::finish_to_start;
    } else if (lowered == "sf") {
        out = core::gantt::LinkType::start_to_finish;
    } else if (lowered == "ss") {
        out = core::gantt::LinkType::start_to_start;
    } else {
        return false;
    }
    return true;
}

std::vector<PredecessorListItem> predecessors_of(const PlanInput& plan,
                                                 const StructuralEdits& edits,
                                                 std::uint32_t successor_uid) {
    std::vector<PredecessorListItem> items;
    for (const LinkRow& link : plan.links) {
        if (link.successor_uid != successor_uid) {
            continue;
        }
        bool deleted = false;
        if (link.uid != 0) {
            for (const StructuralEdit& edit : edits.applied()) {
                if (edit.kind == StructuralKind::delete_link && edit.link_uid == link.uid) {
                    deleted = true;
                    break;
                }
            }
        }
        if (deleted) {
            continue;
        }
        PredecessorListItem item;
        item.link_uid = link.uid;
        item.predecessor_uid = link.predecessor_uid;
        item.type = link.type;
        item.type_name = link_type_name(link.type);
        item.lag_ticks = link.lag_ticks;
        item.lag_text = format_link_lag(link.lag_ticks);
        item.pending = link.uid == 0;
        if (const TaskRow* row = find_task(plan, link.predecessor_uid)) {
            item.predecessor_id = std::to_string(row->id);
            item.predecessor_name = row->name;
        } else {
            item.predecessor_id = std::to_string(link.predecessor_uid);
        }
        items.push_back(std::move(item));
    }
    return items;
}

bool parse_link_lag(std::string_view text, std::int32_t& ticks, std::string& error) {
    ticks = 0;
    error.clear();
    const std::string trimmed = trim_text(text);
    if (trimmed.empty()) {
        error = "a lag is required (0, 2d, -1d, 4h)";
        return false;
    }
    const char* begin = trimmed.c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
        error = "lag \"" + trimmed +
                "\" is not a number; use 0, 2d, -1d, 4h (d=days, h=hours, m=minutes)";
        return false;
    }
    if (!std::isfinite(value)) {
        error = "lag \"" + trimmed + "\" is not a finite number";
        return false;
    }
    const std::string suffix = lowercase_ascii(trim_text(std::string(end)));
    double ticks_per_unit = 0.0;
    if (suffix.empty() || suffix == "d") {
        ticks_per_unit = static_cast<double>(mppwrite::kTicksPerDay);
    } else if (suffix == "h") {
        ticks_per_unit = static_cast<double>(mppwrite::kTicksPerDay) / 8.0;
    } else if (suffix == "m") {
        ticks_per_unit = static_cast<double>(mppwrite::kTicksPerDay) / 480.0;
    } else {
        error = "lag \"" + trimmed +
                "\" has an unknown unit; use d (days), h (hours) or m (minutes)";
        return false;
    }
    const double raw_ticks = value * ticks_per_unit;
    if (!std::isfinite(raw_ticks) ||
        std::fabs(raw_ticks) > static_cast<double>(mppwrite::kMaxLinkLagTicks)) {
        error = "lag \"" + trimmed + "\" is outside the writer's range";
        return false;
    }
    ticks = static_cast<std::int32_t>(std::llround(raw_ticks));
    return true;
}

std::string format_link_lag(std::int32_t ticks) {
    if (ticks == 0) {
        return "0";
    }
    const long long value = static_cast<long long>(ticks);
    if (value % 4800 == 0) {
        return std::to_string(value / 4800) + "d";
    }
    if (value % 600 == 0) {
        return std::to_string(value / 600) + "h";
    }
    if (value % 10 == 0) {
        return std::to_string(value / 10) + "m";
    }
    return std::to_string(value);
}

std::string validate_link_add(const PlanInput& plan, const StructuralEdits& edits,
                              std::uint32_t predecessor_uid, std::uint32_t successor_uid,
                              core::gantt::LinkType type) {
    if (predecessor_uid == 0 || successor_uid == 0) {
        return "a predecessor needs two task rows";
    }
    if (predecessor_uid == successor_uid) {
        return "a task cannot be its own predecessor";
    }
    if (edits.is_pending_insert(predecessor_uid) || edits.is_pending_insert(successor_uid)) {
        return "a pending inserted task has no file row yet; save it before linking";
    }
    // W31-0.1 (R30-1, High): an automatic outline edit queued before this link
    // carries the slack and late dates Project computed on the UNLINKED file
    // (build_outline_slack), and the save writes them after the link (step 2
    // then step 5b).  No Project save pair measures a link added after the
    // outline recalculated slack, so Add Link is refused until that edit is
    // saved or undone.
    for (const StructuralEdit& pending : edits.applied()) {
        if ((pending.kind == StructuralKind::outline_indent ||
             pending.kind == StructuralKind::outline_outdent) &&
            !pending.slack.empty()) {
            return "an automatic outline edit is pending; its save rewrites "
                   "every live automatic row's slack and late dates on the "
                   "unlinked file, so a link added now would be saved beside "
                   "unlinked slack (no Project pair measures that); save or "
                   "undo the outline edit first";
        }
    }
    if (find_task(plan, predecessor_uid) == nullptr) {
        return "predecessor uid " + std::to_string(predecessor_uid) +
               " is not a task in the plan";
    }
    const TaskRow* successor = find_task(plan, successor_uid);
    if (successor == nullptr) {
        return "successor task is not in the plan";
    }
    if (has_pending_delete(edits, predecessor_uid) ||
        has_pending_delete(edits, successor_uid)) {
        return "a task with a pending delete cannot take a new link";
    }
    for (const LinkRow& link : plan.links) {
        if (link.predecessor_uid == predecessor_uid &&
            link.successor_uid == successor_uid && link.type == type) {
            return std::string("that predecessor already exists as ") +
                   link_type_name(type);
        }
    }
    if (has_pending_link_add(edits, predecessor_uid, successor_uid, type)) {
        return std::string("that predecessor is already pending as ") + link_type_name(type);
    }
    // W30-C5 (C3): a link whose endpoint task is not live is dropped from
    // PlanInput, so the duplicate and incoming-link tests below cannot see one.
    // The file may still hold a live link into the successor (the writer
    // matches live link rows by successor uid, whatever the predecessor), which
    // would make the Fixed2Meta byte shape unmeasured and could duplicate this
    // pair.  Refuse rather than guess.  Links a pending edit dropped are
    // excluded: the same save tombstones them (the measured delete-and-re-add).
    if (plan.unreadable_links != 0) {
        return "the plan has " + std::to_string(plan.unreadable_links) +
               " link(s) whose endpoint task is not live, so an incoming link "
               "into task uid " + std::to_string(successor_uid) +
               " cannot be ruled out; Add Link is refused";
    }
    // W30-0.7 (R29-7): Wave 29 measured the link Fixed2Meta +8 byte as the
    // successor's driving state -- 0x07 once the successor enters progress, not
    // "a later link" (the percent-mid25 control's FIRST of three links into
    // distinct successors is 0x07).  add_task_link used to write 0x07 whenever
    // the successor already had a live link, and 0x0f for a progressed
    // successor's first link; no Project save pair adds either shape.  Refuse
    // both up front (standing rule 1).  `plan` is the display plan, so a pending
    // link delete that frees the successor -- the measured delete-and-re-add
    // save -- has already been applied here.
    for (const LinkRow& link : plan.links) {
        if (link.successor_uid == successor_uid) {
            return "task uid " + std::to_string(successor_uid) +
                   " already has an incoming link; the Fixed2Meta byte for a "
                   "second incoming link is not measured (Wave 29 falsified the "
                   "later-link rule), so Add Link is refused";
        }
    }
    if (successor->percent_complete > 0) {
        return "task uid " + std::to_string(successor_uid) + " is " +
               std::to_string(successor->percent_complete) +
               "% complete; no Project save pair adds a link into a progressed "
               "task, so Add Link is refused";
    }
    return {};
}

bool make_link_add(const PlanInput& plan, const StructuralEdits& edits,
                   std::uint32_t predecessor_uid, std::uint32_t successor_uid,
                   core::gantt::LinkType type, std::string_view lag_text,
                   StructuralEdit& out, std::string& error) {
    error = validate_link_add(plan, edits, predecessor_uid, successor_uid, type);
    if (!error.empty()) {
        return false;
    }
    std::int32_t lag_ticks = 0;
    if (!parse_link_lag(lag_text, lag_ticks, error)) {
        return false;
    }
    out = StructuralEdit{};
    out.kind = StructuralKind::add_link;
    out.uid = successor_uid;
    out.predecessor_uid = predecessor_uid;
    out.link_type = type;
    out.lag_ticks = lag_ticks;
    return true;
}

bool make_link_delete(const PredecessorListItem& item, std::uint32_t successor_uid,
                      StructuralEdit& out, std::string& error) {
    error.clear();
    if (item.pending || item.link_uid == 0) {
        error = "that predecessor is a pending add; undo it instead";
        return false;
    }
    if (successor_uid == 0) {
        error = "the task has no UniqueID";
        return false;
    }
    out = StructuralEdit{};
    out.kind = StructuralKind::delete_link;
    out.uid = successor_uid;
    out.predecessor_uid = item.predecessor_uid;
    out.link_uid = item.link_uid;
    out.link_type = item.type;
    out.lag_ticks = item.lag_ticks;
    return true;
}

std::vector<TaskLinkItem> saved_links_touching(const PlanInput& plan,
                                               const StructuralEdits& edits,
                                               std::uint32_t task_uid) {
    std::vector<TaskLinkItem> items;
    if (task_uid == 0) {
        return items;
    }
    for (const LinkRow& link : plan.links) {
        if (link.uid == 0 ||
            (link.predecessor_uid != task_uid && link.successor_uid != task_uid)) {
            continue;
        }
        bool deleted = false;
        for (const StructuralEdit& edit : edits.applied()) {
            if (edit.kind == StructuralKind::delete_link && edit.link_uid == link.uid) {
                deleted = true;
                break;
            }
        }
        if (deleted) {
            continue;
        }
        TaskLinkItem item;
        item.link_uid = link.uid;
        item.predecessor_uid = link.predecessor_uid;
        item.successor_uid = link.successor_uid;
        item.type = link.type;
        item.lag_ticks = link.lag_ticks;
        items.push_back(item);
    }
    return items;
}

bool make_link_delete_edit(const TaskLinkItem& item, StructuralEdit& out,
                           std::string& error) {
    error.clear();
    if (item.link_uid == 0) {
        error = "that link is a pending add; undo it instead";
        return false;
    }
    if (item.successor_uid == 0 || item.predecessor_uid == 0) {
        error = "the link needs both of its task rows";
        return false;
    }
    out = StructuralEdit{};
    out.kind = StructuralKind::delete_link;
    out.uid = item.successor_uid;
    out.predecessor_uid = item.predecessor_uid;
    out.link_uid = item.link_uid;
    out.link_type = item.type;
    out.lag_ticks = item.lag_ticks;
    return true;
}

}  // namespace app
