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

#include "cell_format.h"
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

// Wave 29 (W29-1.2): the fill a sheet cell takes from the view's stored
// per-cell formatting.  `set` is false when the row has no entry, or when the
// entry's colour is automatic -- in both cases the cell keeps the row's Text
// Styles background, which is Project's "Automatic" meaning.  A field-specific
// entry wins over the row's 0xffffffff default (core/viewstate/cell_format.h).
struct CellFill {
    bool set = false;
    Rgb colour{};
};

[[nodiscard]] inline CellFill cell_fill_for(
    const msproject::viewstate::CellFormatTable& formats, std::uint32_t row_uid,
    std::uint32_t field_id) noexcept {
    CellFill fill;
    const msproject::viewstate::CellFormat* entry = formats.find(row_uid, field_id);
    if (entry != nullptr && entry->has_background) {
        fill.set = true;
        fill.colour = entry->background;
    }
    return fill;
}

// Wave 29 (W29-1.3): bar labels from the applied view's stored bar styles.
//
// Every `BarStyle` carries five Task field ids (left, right, top, bottom,
// inside) and a Show For / Not Show For pair of 64-bit masks.  The masks index
// Project's PjBarShowFor categories (MPXJ's GanttBarShowTasks; Aspose.Tasks'
// GanttBarShowFor documents the same numbering): 0 Normal, 1 Milestone, 2
// Summary, 3 Critical, 4 Noncritical, 6 Finished, 7 In Progress, 26 Rolled Up,
// 27 Project Summary, 28 Split, 40 Group By Summary, 41 Deliverable, 42
// Dependency, 43 Active, 44 Manually Scheduled, 45 Warning, 49 Placeholder.
//
// A style applies when every *set* Show For category is true and every set Not
// Show For category is false.  A style that tests a category the port cannot
// answer (Marked, the Flag1..20 set, the early/late variants) never matches:
// putting a label on a bar Project leaves bare is worse than showing none.
// The categories the model answers outright are listed on bar_label_texts_for.
struct BarLabelTask {
    bool project_summary = false;
    bool summary = false;   // a summary task other than the project row
    bool milestone = false;
    bool critical = false;
    bool manually_scheduled = false;
    bool finished = false;    // 100 % complete
    bool in_progress = false; // started, not finished
};

// The text each field id can show.  The caller formats them (the label's date
// is the file's date format WITHOUT the weekday: Project prints "14/04/26").
struct BarLabelValues {
    std::string name;
    std::string id;
    std::string start;
    std::string finish;
    std::string duration;
    std::string resource;
    std::string percent;
};

// The five labels one row's stored bar styles call for (empty = none).
struct BarLabelTexts {
    std::string left;
    std::string right;
    std::string top;
    std::string bottom;
    std::string inside;

    bool empty() const noexcept {
        return left.empty() && right.empty() && top.empty() && bottom.empty() &&
               inside.empty();
    }
};

// Matches the stored bar styles against the task and fills each side from the
// FIRST matching style (in file order) that stores a field id for it.  A side
// with no field stores 0xffffffff (the record's sentinel); 0 is accepted as the
// same for a synthetic style.  "First" is what keeps a Summary+Critical style
// that stores no label from hiding the Summary style's left Name on a critical
// summary when the file orders the specific style first.  An id this port has
// no text for claims the side with an empty string (the product draws that
// field; substituting another style's field would invent text).  A style list that
// matches but stores no label yields all-empty texts, which is the product's
// bare bar.
//
// Categories that are true/false by construction of this port's model:
// Normal (!summary && !milestone && !project_summary), Summary (a summary that
// is not the project row), Split false (no split bars are laid out), Rolled Up
// false, Group By Summary false, Deliverable/Dependency false, Placeholder,
// Placeholder Start/Finish/Duration false (the port lays out no placeholder
// rows), Warning false (it models no warning state), Active TRUE (the reader
// decodes no inactive rows, so every row the port lays out is active -- this is
// what makes a stock file's Active-tagged Task/Milestone styles apply).
// Finished/In Progress come from the task's percent complete; Manually
// Scheduled from the reader's measured bit.
[[nodiscard]] BarLabelTexts bar_label_texts_for(
    const msproject::viewstate::GanttStyle& style, const BarLabelTask& task,
    const BarLabelValues& values);

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
//
// `cell_formats`, when non-null, receives the applied view's `0x2240000c`
// per-cell formatting (W29-1.2).  A view without the record leaves it
// undecoded, which is not a failure; a record that does not frame as 44-byte
// entries is refused like any other malformed view record.
[[nodiscard]] bool read_gantt_style(const std::string& path,
                                    msproject::viewstate::GanttStyle& out,
                                    std::string* reason = nullptr,
                                    msproject::viewstate::CellFormatTable*
                                        cell_formats = nullptr);

}  // namespace app
