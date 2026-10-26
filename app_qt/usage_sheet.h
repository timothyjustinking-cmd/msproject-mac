// app_qt/usage_sheet.h -- Task Usage / Resource Usage as Project's time-phased
// sheet draws them (wave 31, G4, W31-4.4).
//
// WHAT THIS IS
// ------------
// The Qt presentation of app::UsageGrid (app/usage_grid.h).  Project's usage
// sheets give every logical row one 21 px sub-row per drawn DETAIL, stacked
// two to a 42 px logical row (tests/data/reference/wave31-usage.measures.json,
// measured_pixels.logical_row_pitch_px): the parent row's label spans its
// sub-rows, the Details column names each one ("Work", "Act. W", "Cost"), and
// each sub-row carries its own numbers in the period columns.  This class
// turns a UsageGrid into that shape:
//
//   physical row = (logical row, detail), in grid order, details in stored
//                  order, then the port's Total row at the end;
//   columns      = label, Details, one column per period, Total.
//
// MEASURED PRESENTATION (wave-31 captures)
// ----------------------------------------
//   * label column  147 px Task Usage (Task Name, 24 chars x 6 + 3) and 123 px
//                   Resource Usage (Resource Name, 20 chars x 6 + 3);
//   * Details        48 px, captioned "Details"; the detail cells read exactly
//                   as Project abbreviates them: "Work", "Act. W", "Cost";
//   * period columns 64 px for Work + Actual Work, 84 px Task Usage with Cost,
//                   105 px Resource Usage with Cost (the width follows the
//                   widest rendered cost text; the committed pairs isolate it);
//   * Total          84 px -- a PORT AFFORDANCE: the measures record that
//                   Project's usage sheets have no total row or column;
//   * row pitch      21 px, gutter numbers on the parent rows only (their
//                   stored Task.ID / Resource.ID), blank on assignments and on
//                   the port's Total row;
//   * header         two tiers: the top tier carries the week/month group label
//                   on the group's first column ("4 May '26" measured), the
//                   bottom tier the centred day letter; light rules in the
//                   bottom tier only, a stronger rule at each group boundary.
//
// VALUES
// ------
// Work/Actual Work cells are Duration values (minutes x kTicksPerMinute), Cost
// cells are Number dollars (integer cents / 100), so the RecordSet keeps the
// same numbers COM reported (minutes and cents).  A zero period cell is Null,
// i.e. drawn blank, like Project's measured blank buckets.  The shell keeps the
// model's Cost cells typed through SheetTableModel's per-cell value-kind lookup
// (a period column mixes Duration and Cost rows) and renders Cost with the
// measured "$" symbol in the sheet format.
//
// Nothing here writes a file: the RecordSet is presentation data built from an
// in-memory plan, and row labels are the plan's own strings.

#ifndef MSQT_USAGE_SHEET_H
#define MSQT_USAGE_SHEET_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "record_set.h"
#include "sheet.h"
#include "usage_grid.h"

namespace msqt {

// The Details column's field id, in the same app-private block as
// app::kUsageLabelField / kUsageTotalField / kUsagePeriodFieldBase.
inline constexpr core::view::FieldId kUsageDetailsField = 0x0b4f0003u;

// One column's two-tier caption.  Detail/label/Total columns are not tiered
// (`tiered == false`) and keep the ordinary single-band caption.
struct UsageHeaderCell {
    std::string major;         // top-tier label; empty except a group's first column
    std::string minor;         // bottom-tier label
    bool tiered = false;       // a time-phased (period) column
    bool group_start = false;  // draw the stronger group-boundary rule at its left edge
    int major_align = 0;       // stored TimescaleTier::align: 0 left, 1 centre, 2 right
    int minor_align = 1;
};

// A vertical merge on the label column: `rows` physical sub-rows starting at
// `row`.
struct UsageRowSpan {
    int row = 0;
    int rows = 0;
};

// One physical row's provenance: which logical grid row and detail it draws.
struct UsageSheetRow {
    std::size_t logical = 0;  // index into the grid's rows
    std::uint32_t uid = 0;    // the logical row's uid
    app::UsageGridRowKind kind = app::UsageGridRowKind::task;
    std::size_t detail = 0;   // index into the grid's details
};

class UsageSheet final {
  public:
    UsageSheet() = default;
    explicit UsageSheet(const app::UsageGrid& grid) { build(grid); }

    // Builds the RecordSet, columns, widths and header from `grid`.  A grid
    // with no valued detail keeps one blank sub-row per logical row so the
    // label column still has a cell to draw.
    void build(const app::UsageGrid& grid);

    const core::view::RecordSet& records() const noexcept { return records_; }
    const std::vector<core::sheet::SheetColumn>& columns() const noexcept { return columns_; }
    // Measured pixel widths, parallel to columns() (NOT the SheetColumn
    // character-width convention: these are pixels).
    const std::vector<int>& column_widths() const noexcept { return widths_; }
    const std::vector<UsageRowSpan>& label_spans() const noexcept { return spans_; }
    // The frozen gutter's text per physical row; "" is a blank gutter cell.
    const std::vector<std::string>& row_numbers() const noexcept { return row_numbers_; }
    const std::vector<UsageHeaderCell>& header_cells() const noexcept { return header_; }
    const std::vector<UsageSheetRow>& rows() const noexcept { return rows_; }

    // The kind a period/Total cell renders as: Duration on a Work/Actual Work
    // sub-row, Cost on a Cost sub-row, Text on the non-tiered columns, Auto
    // outside the sheet.  Display ordinals equal model columns (every column is
    // visible in the generic presentation).
    core::sheet::CellKind value_kind(int display_row, int display_column) const;

    bool resource() const noexcept { return resource_; }
    std::size_t detail_count() const noexcept { return detail_count_; }
    std::size_t period_count() const noexcept { return period_count_; }
    std::size_t logical_row_count() const noexcept { return logical_rows_; }

    // The display column showing period `index`, and the Total column; both
    // kNoIndex out of range.
    static constexpr int kLabelColumn = 0;
    static constexpr int kDetailsColumn = 1;
    static constexpr int kFirstPeriodColumn = 2;
    int period_column(std::size_t index) const noexcept {
        return index < period_count_ ? kFirstPeriodColumn + static_cast<int>(index) : -1;
    }
    int total_column() const noexcept { return kFirstPeriodColumn + static_cast<int>(period_count_); }

  private:
    void build_header(const app::UsageGrid& grid);

    core::view::RecordSet records_{core::model::RecordType::Assignment};
    std::vector<core::sheet::SheetColumn> columns_;
    std::vector<int> widths_;
    std::vector<UsageRowSpan> spans_;
    std::vector<std::string> row_numbers_;
    std::vector<UsageHeaderCell> header_;
    std::vector<UsageSheetRow> rows_;
    // The value rule of each detail in stored order (work when the grid stores
    // no valued detail at all -- the blank sub-row guard).
    std::vector<msproject::viewstate::UsageDetailKind> detail_kinds_;
    bool resource_ = false;
    std::size_t detail_count_ = 1;
    std::size_t period_count_ = 0;
    std::size_t logical_rows_ = 0;
};

}  // namespace msqt

#endif  // MSQT_USAGE_SHEET_H
