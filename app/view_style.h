// app/view_style.h -- what a loaded file's Gantt style record means for a row.
//
// The decoder lives in core/viewstate (gantt_style.h); this seam answers the
// presentation question the two shells share: which Text Styles item applies to
// a task row, and which bar style's colours to paint.  Keeping it here means the
// headless app ctest exercises the rule and the Qt/macOS painters only draw.
//
// The row facts are the ones core::sheet::SheetModel already exposes
// (is_summary/is_milestone/is_critical) plus uid 0 for the synthetic project
// row, so the mapping has no dependency on the Qt shell.

#pragma once

#include <cstdint>
#include <string>

#include "gantt_style.h"

namespace app {

// The decoder's RGB triple, named once here so the palette and the tests read
// the same type.
using Rgb = msproject::viewstate::Rgb;

// The Text Styles item Project applies to a row.  The precedence is measured
// from PROJECT_A's capture: its row 3 is both critical and a milestone and
// renders with the MILESTONE style (italic red), not the critical style (which
// the same file stores as upright black).  Likewise summary rows take the
// summary style even when critical (the summary's bold blue is what the capture
// shows on row 2).  So the most specific row kind wins, in this order:
// project summary, summary, milestone, then critical/noncritical.
[[nodiscard]] inline msproject::viewstate::TextStyleItem text_style_item_for_row(
    bool project_summary, bool summary, bool milestone, bool critical) noexcept {
    using msproject::viewstate::TextStyleItem;
    if (project_summary) {
        return TextStyleItem::project_summary;
    }
    if (summary) {
        return TextStyleItem::summary;
    }
    if (milestone) {
        return TextStyleItem::milestone;
    }
    return critical ? TextStyleItem::critical : TextStyleItem::non_critical;
}

// The decoded style for such a row, or nullptr when the file carried no style
// record or the record's font index did not resolve.
[[nodiscard]] inline const msproject::viewstate::TextStyle* text_style_for_row(
    const msproject::viewstate::GanttStyle& style, bool project_summary,
    bool summary, bool milestone, bool critical) noexcept {
    if (!style.decoded) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(
        text_style_item_for_row(project_summary, summary, milestone, critical));
    if (index >= style.text.size()) {
        return nullptr;
    }
    const msproject::viewstate::TextStyle& item = style.text[index];
    return item.valid ? &item : nullptr;
}

// Wave 26 (W26-3.2): the colours the active view's bar styles give each role.
// A role is absent (`has_* == false`) when the file defines no such style; the
// shell then keeps the wave-25 measured stock colour for it.
//
// Resolution is by the decoded style's own Name and ShowFor bits, not by slot
// order: PROJECT_A's active list has 10 styles (Critical Task, Non-Critical
// Task, Summary, Project Summary, ...) while a stock project has 41.  The
// ShowFor bits are GanttBarShowForTasks: Normal 0, Milestone 1, Summary 2,
// Critical 3, Noncritical 4, In Progress 7, Finished 6, Project Summary 27.
struct BarPalette {
    bool decoded = false;
    Rgb normal{};
    bool has_normal = false;
    Rgb manual_normal{};
    bool has_manual_normal = false;
    Rgb critical{};
    bool has_critical = false;
    Rgb summary{};
    bool has_summary = false;
    // The style that shows for Summary AND Critical (PROJECT_A's unnamed
    // 2+3 record, the red the capture shows on its critical summary bars).
    Rgb summary_critical{};
    bool has_summary_critical = false;
    Rgb milestone{};
    bool has_milestone = false;
    Rgb project_summary{};
    bool has_project_summary = false;
    Rgb progress{};
    bool has_progress = false;
    Rgb finished{};
    bool has_finished = false;
};

// Reads the palette from a decoded Gantt style.  `decoded` is false (and every
// has_* false) when the record did not decode.
[[nodiscard]] BarPalette bar_palette_from(const msproject::viewstate::GanttStyle& style);

// Reads a whole .mpp and decodes the applied view's Gantt style record:
//
//   * `214/Props` record 0x03400000        -> the FONT_BASES table
//   * `214/CV_iew`                          -> the view definitions
//   * `214/CEdl` record 0x26400018          -> the applied view name
//
// The first two streams must exist and parse; a file without a Gantt view
// (a new/empty project, or a view family this reader does not decode) returns
// false with a reason, and the caller keeps its stock presentation.  This is
// the shared seam: the Qt shell and app/main.mm both paint Gantt bars, and the
// headless app test drives this function on the committed synthetic fixture.
[[nodiscard]] bool read_gantt_style(const std::string& path,
                                    msproject::viewstate::GanttStyle& out,
                                    std::string* reason = nullptr);

}  // namespace app
