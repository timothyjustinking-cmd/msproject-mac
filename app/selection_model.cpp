// app/selection_model.cpp -- see selection_model.h for the contract.
//
// Everything here is arithmetic and model calls: no AppKit, no reader types, no
// drawing, so app/selection_model_tests.cpp proves it on Windows as well as on
// macOS.  The only state consulted is core::sheet::SheetModel (the selection and
// the outline) plus the layout it is asked about.

#include "selection_model.h"

#include <cstdint>

namespace app {
namespace {

// Clamps to the visible list: a move above the first row lands on the first row
// and a move past the last lands on the last, which is what an arrow key does in
// a grid.  kNoIndex only when there is no row at all.
std::size_t clamp_ordinal(std::int64_t ordinal, std::size_t count) noexcept {
    if (count == 0) {
        return kNoIndex;
    }
    if (ordinal < 0) {
        return 0;
    }
    const std::int64_t last = static_cast<std::int64_t>(count) - 1;
    if (ordinal > last) {
        return static_cast<std::size_t>(last);
    }
    return static_cast<std::size_t>(ordinal);
}

}  // namespace

std::size_t RowMap::row_at(std::size_t ordinal) const noexcept {
    return ordinal < rows.size() ? rows[ordinal] : kNoIndex;
}

std::size_t RowMap::ordinal_of(std::size_t row) const noexcept {
    for (std::size_t ordinal = 0; ordinal < rows.size(); ++ordinal) {
        if (rows[ordinal] == row) {
            return ordinal;
        }
    }
    return kNoIndex;
}

std::size_t RowMap::ordinal_of_uid(std::uint32_t uid) const noexcept {
    for (std::size_t ordinal = 0; ordinal < uids.size(); ++ordinal) {
        if (uids[ordinal] == uid) {
            return ordinal;
        }
    }
    return kNoIndex;
}

RowMap build_row_map(const core::sheet::SheetModel& sheet) {
    RowMap map;
    const std::size_t count = sheet.visible_row_count();
    map.rows.reserve(count);
    map.uids.reserve(count);
    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
        const std::size_t row = sheet.visible_row(ordinal);
        if (row == kNoIndex) {
            break;  // defensive: visible_row_count() and visible_row() disagree
        }
        map.rows.push_back(row);
        map.uids.push_back(sheet.row_uid(row));
    }
    return map;
}

SelectedTask selected_task(const core::sheet::SheetModel& sheet, const RowMap& map) {
    SelectedTask selected;
    if (!sheet.has_selection()) {
        return selected;
    }
    selected.has_selection = true;
    selected.row = sheet.anchor().row;
    selected.ordinal = map.ordinal_of(selected.row);
    if (selected.ordinal != kNoIndex) {
        selected.visible = true;
        selected.uid = map.uids[selected.ordinal];
    } else {
        selected.uid = sheet.row_uid(selected.row);
    }
    return selected;
}

bool select_cell(core::sheet::SheetModel& sheet, std::size_t row, std::size_t column) {
    return sheet.select_cell(row, column);
}

std::size_t select_from_gantt(core::sheet::SheetModel& sheet, const RowMap& map,
                              const core::gantt::GanttLayout& layout, double x, double y,
                              std::size_t column) {
    const core::gantt::HitResult hit = layout.hit_test(x, y);
    if (hit.kind == core::gantt::HitKind::none) {
        return kNoIndex;
    }
    const std::uint32_t uid =
        hit.kind == core::gantt::HitKind::bar ? hit.task_uid : hit.successor_uid;
    if (uid == 0) {
        return kNoIndex;
    }
    const std::size_t ordinal = map.ordinal_of_uid(uid);
    const std::size_t row = map.row_at(ordinal);
    if (row == kNoIndex) {
        return kNoIndex;
    }
    if (!sheet.select_cell(row, column)) {
        return kNoIndex;
    }
    return row;
}

std::size_t row_after(const RowMap& map, std::size_t row, int delta) {
    const std::size_t ordinal = map.ordinal_of(row);
    if (ordinal == kNoIndex) {
        return kNoIndex;
    }
    const std::int64_t target = static_cast<std::int64_t>(ordinal) + static_cast<std::int64_t>(delta);
    const std::size_t clamped = clamp_ordinal(target, map.size());
    return map.row_at(clamped);
}

bool toggle_row(core::sheet::SheetModel& sheet, std::size_t row) {
    if (row >= sheet.row_count() || !sheet.is_summary(row)) {
        return false;
    }
    return sheet.toggle_collapsed(row);
}

ScrollTarget scroll_target(const core::sheet::SheetModel& sheet,
                           const core::sheet::SheetMetrics& metrics, std::size_t row) {
    ScrollTarget target;
    if (row >= sheet.row_count()) {
        return target;
    }
    const std::size_t display = sheet.display_row(row);
    if (display == kNoIndex) {
        return target;  // hidden by a collapsed summary
    }
    const int y = sheet.row_top_points(display, metrics);
    if (y < 0) {
        return target;
    }
    target.valid = true;
    target.y = static_cast<double>(y);
    target.height = static_cast<double>(sheet.row_height_points(row, metrics));
    return target;
}

}  // namespace app
