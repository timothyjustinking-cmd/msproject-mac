// app_qt/usage_sheet.cpp -- see usage_sheet.h.

#include "usage_sheet.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "date.h"
#include "layout_model.h"  // app::date_value_from_date32
#include "value.h"
#include "view_model.h"    // app::kUsageLabelField / kUsageTotalField / ...

namespace msqt {
namespace {

using core::view::FieldId;
using core::view::Value;
using msproject::viewstate::UsageDetailKind;

const char* kShortMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Project's Details-cell caption for each stored detail, measured on the
// wave-31 captures: Task Usage shows "Work" and "Act. W"; Resource Usage shows
// "Work" and "Cost".
const char* details_caption(UsageDetailKind kind) noexcept {
    switch (kind) {
        case UsageDetailKind::work:
            return "Work";
        case UsageDetailKind::actual_work:
            return "Act. W";
        case UsageDetailKind::cost:
            return "Cost";
        case UsageDetailKind::other:
            break;
    }
    return "";
}

bool detail_is_cost(UsageDetailKind kind) noexcept {
    return kind == UsageDetailKind::cost;
}

std::string short_month(int month) {
    if (month < 1 || month > 12) {
        return {};
    }
    return kShortMonths[month - 1];
}

core::sched::CivilDate civil_of(int day) noexcept {
    return core::sched::to_civil(core::sched::pack(day, 0));
}

// The single-letter weekday Project draws in the measured day tier
// (M T W T F S S; core::sched counts 0 = Sunday).
const char* day_letter(int day) noexcept {
    switch (core::sched::weekday_of_day(day)) {
        case 1: return "M";
        case 2: return "T";
        case 3: return "W";
        case 4: return "T";
        case 5: return "F";
        case 6: return "S";
        default: return "S";
    }
}

// The measured top-tier week label: day without a leading zero, three-letter
// English month, apostrophe and two-digit year ("4 May '26").
std::string week_major_label(int day) {
    const core::sched::CivilDate civil = civil_of(day);
    const std::string month = short_month(civil.month);
    if (month.empty()) {
        return {};
    }
    return std::to_string(civil.day) + " " + month + " '" +
           (civil.year % 100 < 10 ? "0" : "") + std::to_string(civil.year % 100);
}

// The port's measured month shape ("May 2026"), the same one the wave-20 usage
// header used (app/view_model.cpp period_label).
std::string month_major_label(int day) {
    const core::sched::CivilDate civil = civil_of(day);
    const std::string month = short_month(civil.month);
    if (month.empty()) {
        return {};
    }
    return month + " " + std::to_string(civil.year);
}

// The bottom-tier label for the grid's stored bottom unit.
std::string minor_label(const app::UsageGrid& grid, int first_day) {
    using msproject::viewstate::TimescaleUnit;
    switch (grid.minor_tier.unit) {
        case TimescaleUnit::days:
            return day_letter(first_day);
        case TimescaleUnit::weeks: {
            // The measured shape: "Wk Mon 19/10/26" -- the week's Monday date.
            core::sheet::SheetFormat format;
            return "Wk " + core::sheet::format_date_text(
                               app::date_value_from_date32(
                                   core::sched::pack(first_day, 0)),
                               format);
        }
        case TimescaleUnit::months:
            return month_major_label(first_day);
        default:
            break;
    }
    return {};
}

// Monday of the week containing `day` (Project's week boundary).
int monday_of(int day) noexcept {
    return day - ((core::sched::weekday_of_day(day) + 6) % 7);
}

int clipped_align(int align) noexcept { return std::clamp(align, 0, 2); }

// A blank cell, like Project's measured empty buckets, is simply not stored.
void set_amount(core::view::RecordSet& records, std::size_t row, FieldId field,
                std::int64_t amount, bool cost) {
    if (amount == 0) {
        return;
    }
    if (cost) {
        // Cost is kept in integer cents by the app grid; the sheet stores a
        // Number of currency units so CellKind::Cost renders "$1,200.00".
        records.set_field(row, field, Value::of_number(static_cast<double>(amount) / 100.0));
        return;
    }
    records.set_field(row, field,
                      Value::of_duration(amount * core::view::kTicksPerMinute));
}

}  // namespace

void UsageSheet::build(const app::UsageGrid& grid) {
    resource_ = grid.resource;
    logical_rows_ = grid.rows.size();
    detail_count_ = std::max<std::size_t>(1, grid.details.size());
    // The period columns reuse app/view_model.h's synthetic field-id block,
    // whose cap keeps every id clear of real fields (kUsageMaxPeriodColumns).
    period_count_ = std::min(grid.periods.size(), app::kUsageMaxPeriodColumns);

    detail_kinds_.clear();
    detail_kinds_.reserve(detail_count_);
    for (std::size_t detail = 0; detail < detail_count_; ++detail) {
        detail_kinds_.push_back(detail < grid.details.size()
                                    ? grid.details[detail].kind
                                    : UsageDetailKind::work);
    }

    records_ = core::view::RecordSet(core::model::RecordType::Assignment);
    records_.add_field(app::kUsageLabelField);
    records_.add_field(kUsageDetailsField);
    records_.add_field(app::kUsageTotalField);
    for (std::size_t period = 0; period < period_count_; ++period) {
        records_.add_field(
            static_cast<FieldId>(app::kUsagePeriodFieldBase + period));
    }

    rows_.clear();
    spans_.clear();
    row_numbers_.clear();
    const std::size_t physical_rows = logical_rows_ * detail_count_;
    rows_.reserve(physical_rows);
    spans_.reserve(logical_rows_);
    row_numbers_.reserve(physical_rows);

    for (std::size_t logical = 0; logical < grid.rows.size(); ++logical) {
        const app::UsageGridRow& source = grid.rows[logical];
        const std::size_t first_row = records_.row_count();
        spans_.push_back(UsageRowSpan{static_cast<int>(first_row),
                                      static_cast<int>(detail_count_)});
        for (std::size_t detail = 0; detail < detail_count_; ++detail) {
            const std::size_t row = records_.add_row(source.uid);
            rows_.push_back(UsageSheetRow{logical, source.uid, source.kind, detail});
            if (detail == 0) {
                records_.set_field(row, app::kUsageLabelField,
                                   Value::of_text(source.label));
            }
            if (detail < grid.details.size()) {
                const std::string caption =
                    details_caption(grid.details[detail].kind);
                if (!caption.empty()) {
                    records_.set_field(row, kUsageDetailsField,
                                       Value::of_text(caption));
                }
            }
            const bool cost = detail_is_cost(detail_kinds_[detail]);
            if (detail < source.values.size()) {
                const std::vector<std::int64_t>& values = source.values[detail];
                const std::size_t count = std::min(values.size(), period_count_);
                for (std::size_t period = 0; period < count; ++period) {
                    set_amount(records_, row,
                               static_cast<FieldId>(app::kUsagePeriodFieldBase +
                                                    period),
                               values[period], cost);
                }
            }
            if (detail < source.totals.size()) {
                set_amount(records_, row, app::kUsageTotalField,
                           source.totals[detail], cost);
            }
            // Project numbers the parent rows with their stored ID and leaves
            // every assignment sub-row blank; the port's Total row is blank too
            // (measured on the wave-31 captures).
            row_numbers_.push_back(detail == 0 && source.numbered
                                       ? std::to_string(source.display_id)
                                       : std::string());
        }
    }

    // The columns, in measured order and pixel widths (usage_sheet.h).
    columns_.clear();
    widths_.clear();
    auto add_column = [this](FieldId field, std::string title,
                             core::view::ColumnAlign align,
                             core::sheet::CellKind kind) {
        core::sheet::SheetColumn column;
        column.field = field;
        column.title = std::move(title);
        column.align = align;
        column.kind = kind;
        column.visible = true;
        column.read_only = true;  // no measured usage-view save path
        columns_.push_back(std::move(column));
    };
    add_column(app::kUsageLabelField, resource_ ? "Resource Name" : "Task Name",
               core::view::ColumnAlign::Left, core::sheet::CellKind::Text);
    widths_.push_back(resource_ ? 123 : 147);
    add_column(kUsageDetailsField, "Details", core::view::ColumnAlign::Left,
               core::sheet::CellKind::Text);
    widths_.push_back(48);
    bool has_cost = false;
    for (const UsageDetailKind kind : detail_kinds_) {
        has_cost = has_cost || detail_is_cost(kind);
    }
    const int period_width = has_cost ? (resource_ ? 105 : 84) : 64;
    for (std::size_t period = 0; period < period_count_; ++period) {
        add_column(static_cast<FieldId>(app::kUsagePeriodFieldBase + period),
                   minor_label(grid, grid.periods[period].first_day),
                   core::view::ColumnAlign::Right, core::sheet::CellKind::Auto);
        widths_.push_back(period_width);
    }
    add_column(app::kUsageTotalField, "Total", core::view::ColumnAlign::Right,
               core::sheet::CellKind::Auto);
    widths_.push_back(84);  // the port's Total affordance keeps wave-20's width

    build_header(grid);
}

void UsageSheet::build_header(const app::UsageGrid& grid) {
    using msproject::viewstate::TimescaleUnit;
    header_.assign(columns_.size(), UsageHeaderCell{});
    const bool major_weeks = grid.major_tier.unit == TimescaleUnit::weeks;
    const bool major_months = grid.major_tier.unit == TimescaleUnit::months;
    const int major_align = clipped_align(grid.major_tier.align);
    const int minor_align = clipped_align(grid.minor_tier.align);
    for (std::size_t period = 0; period < period_count_; ++period) {
        UsageHeaderCell& cell = header_[static_cast<std::size_t>(kFirstPeriodColumn) + period];
        cell.tiered = true;
        cell.minor_align = minor_align;
        cell.major_align = major_align;
        const int first_day = grid.periods[period].first_day;
        cell.minor = minor_label(grid, first_day);
        // The top tier labels the first column of each stored week/month group
        // and draws the stronger boundary rule there (measured: no vertical
        // rules in the top tier except the week boundary).
        bool boundary = false;
        if (major_weeks) {
            const int monday = monday_of(first_day);
            const bool first = period == 0;
            const int previous =
                first ? monday
                      : monday_of(grid.periods[period - 1].first_day);
            boundary = first || monday != previous;
            if (boundary) {
                cell.major = week_major_label(monday);
            }
        } else if (major_months) {
            const core::sched::CivilDate civil = civil_of(first_day);
            const bool first = period == 0;
            const core::sched::CivilDate previous =
                first ? civil : civil_of(grid.periods[period - 1].first_day);
            boundary = first || civil.month != previous.month ||
                       civil.year != previous.year;
            if (boundary) {
                cell.major = month_major_label(first_day);
            }
        }
        cell.group_start = boundary && period > 0;
    }
}

core::sheet::CellKind UsageSheet::value_kind(int display_row, int display_column) const {
    if (display_row < 0 || display_column < 0 ||
        static_cast<std::size_t>(display_row) >= rows_.size()) {
        return core::sheet::CellKind::Auto;
    }
    if (display_column == kLabelColumn || display_column == kDetailsColumn) {
        return core::sheet::CellKind::Text;
    }
    const int period_end = kFirstPeriodColumn + static_cast<int>(period_count_);
    if (display_column >= kFirstPeriodColumn && display_column <= period_end) {
        const std::size_t detail = rows_[static_cast<std::size_t>(display_row)].detail;
        if (detail < detail_kinds_.size() && detail_is_cost(detail_kinds_[detail])) {
            return core::sheet::CellKind::Cost;
        }
        return core::sheet::CellKind::Duration;
    }
    return core::sheet::CellKind::Auto;
}

}  // namespace msqt
