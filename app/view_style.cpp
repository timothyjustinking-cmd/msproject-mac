// app/view_style.cpp -- read a file's Gantt style record through the shared
// core decoders (see view_style.h).

#include "view_style.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cell_format.h"
#include "cfb_writer.h"
#include "gantt_style.h"
#include "props.h"
#include "viewstate.h"

namespace app {
namespace {

const char* const kPropsStream = "214/Props";
const char* const kViewParts[4] = {"214/CV_iew/FixedData", "214/CV_iew/FixedMeta",
                                   "214/CV_iew/VarMeta", "214/CV_iew/Var2Data"};
const char* const kEdlParts[4] = {"214/CEdl/FixedData", "214/CEdl/FixedMeta",
                                  "214/CEdl/VarMeta", "214/CEdl/Var2Data"};
constexpr std::uint32_t kFontBasesRecordId = 0x03400000u;

void fail(std::string* reason, std::string text) {
    if (reason != nullptr) {
        *reason = std::move(text);
    }
}

bool read_parts(const mppwrite::CfbDocument& document, const char* const* names,
                std::array<std::vector<std::uint8_t>, 4>& out) {
    for (std::size_t index = 0; index < out.size(); ++index) {
        if (!document.has_stream(names[index])) {
            return false;
        }
        out[index] = document.read_stream(names[index]);
    }
    return true;
}

msproject::viewstate::PartSet part_set(const std::array<std::vector<std::uint8_t>, 4>& raw) {
    msproject::viewstate::PartSet parts;
    parts.fixed_data = raw[0];
    parts.fixed_meta = raw[1];
    parts.var_meta = raw[2];
    parts.var2_data = raw[3];
    return parts;
}

// The applied view's name, as `214/CEdl` records it (0x26400018), or "".
std::string applied_view_name(const mppwrite::CfbDocument& document) {
    std::array<std::vector<std::uint8_t>, 4> raw;
    if (!read_parts(document, kEdlParts, raw)) {
        return {};
    }
    msproject::viewstate::ViewState state;
    if (!msproject::viewstate::parse_view_state("CEdl", part_set(raw), state, nullptr)) {
        return {};
    }
    for (const msproject::viewstate::EmbeddedProps& props : state.embedded_props) {
        if (props.view_name_decoded && !props.view_name.empty()) {
            return props.view_name;
        }
    }
    return {};
}

}  // namespace

BarPalette bar_palette_from(const msproject::viewstate::GanttStyle& style) {
    using msproject::viewstate::BarStyle;
    BarPalette palette;
    if (!style.decoded) {
        return palette;
    }
    palette.decoded = true;

    const auto named = [&style](std::string_view name) -> const BarStyle* {
        const auto found = std::find_if(
            style.bars.begin(), style.bars.end(),
            [name](const BarStyle& bar) { return bar.name == name; });
        return found == style.bars.end() ? nullptr : &*found;
    };
    const auto with_bits = [&style](std::uint64_t bits) -> const BarStyle* {
        const auto found = std::find_if(
            style.bars.begin(), style.bars.end(),
            [bits](const BarStyle& bar) { return (bar.show_for & bits) == bits; });
        return found == style.bars.end() ? nullptr : &*found;
    };
    const auto set = [](Rgb& colour, bool& has, const BarStyle* bar) {
        if (bar != nullptr && bar->has_middle_colour) {
            colour = bar->middle_colour;
            has = true;
        }
    };

    // Normal tasks: a stock project's style is "Task"; an edited file (the
    // corpus) names the pair "Critical Task"/"Non-Critical Task".
    const BarStyle* normal = named("Task");
    if (normal == nullptr) {
        normal = named("Non-Critical Task");
    }
    set(palette.normal, palette.has_normal, normal);
    set(palette.manual_normal, palette.has_manual_normal, named("Manual Task"));

    // Critical normal tasks: the first style that shows for Normal AND
    // Critical.  PROJECT_A's list puts "Critical Task" (pure red) before
    // "Critical" (light red); the capture contains no light-red pixel, so the
    // first is the one Project paints.
    const BarStyle* critical = named("Critical Task");
    if (critical == nullptr) {
        critical = with_bits((std::uint64_t{1} << 0) | (std::uint64_t{1} << 3));
    }
    set(palette.critical, palette.has_critical, critical);

    set(palette.summary, palette.has_summary, named("Summary"));
    set(palette.summary_critical, palette.has_summary_critical,
        with_bits((std::uint64_t{1} << 2) | (std::uint64_t{1} << 3)));
    set(palette.project_summary, palette.has_project_summary,
        named("Project Summary"));

    // A milestone bar is normally drawn with its start/end shape, so its
    // middle colour can be "automatic" while the visible diamond has a colour.
    const BarStyle* milestone = named("Milestone");
    if (milestone != nullptr) {
        if (milestone->middle_shape != 0 && milestone->has_middle_colour) {
            palette.milestone = milestone->middle_colour;
            palette.has_milestone = true;
        } else if (milestone->has_start_colour) {
            palette.milestone = milestone->start_colour;
            palette.has_milestone = true;
        } else if (milestone->has_middle_colour) {
            palette.milestone = milestone->middle_colour;
            palette.has_milestone = true;
        }
    }

    set(palette.progress, palette.has_progress, named("Progress"));
    if (!palette.has_progress) {
        set(palette.progress, palette.has_progress, named("Manual Progress"));
    }
    set(palette.finished, palette.has_finished, named("Finished"));
    return palette;
}

namespace {

// One PjBarShowFor category the port's model can answer (see view_style.h).
struct ShowForFact {
    int category;
    bool value;
};

// True when every category the style tests is answered and agrees with the
// task.  A category outside the fact table is NOT answerable and makes the
// style fail: a style Project applies to a state this port cannot see must not
// put its text on a bar.
bool style_applies_to(const msproject::viewstate::BarStyle& bar,
                      const BarLabelTask& task) {
    const ShowForFact facts[] = {
        {0, !task.summary && !task.milestone && !task.project_summary},  // Normal
        {1, task.milestone},                                             // Milestone
        {2, task.summary && !task.project_summary},                      // Summary
        {3, task.critical},                                              // Critical
        {4, !task.critical},                                             // Noncritical
        {6, task.finished},                                              // Finished
        {7, task.in_progress},                                           // In Progress
        {26, false},  // Rolled Up: the port lays out no rolled-up rows
        {27, task.project_summary},  // Project Summary
        {28, false},                 // Split: no split bars are laid out
        {40, false},                 // Group By Summary: not a view the port draws
        {41, false},                 // Deliverable: not decoded
        {42, false},                 // Dependency: not decoded
        {43, true},                  // Active: the reader decodes no inactive rows
        {44, task.manually_scheduled},  // Manually Scheduled
        {45, false},  // Warning: the port models no warning state
        {46, false},  // Placeholder Start
        {47, false},  // Placeholder Finish
        {48, false},  // Placeholder Duration
        {49, false},  // Placeholder: no placeholder rows
    };
    const auto answered = [&facts](int category, bool& value) {
        for (const ShowForFact& fact : facts) {
            if (fact.category == category) {
                value = fact.value;
                return true;
            }
        }
        return false;
    };
    for (int category = 0; category < 64; ++category) {
        const bool show = ((bar.show_for >> category) & 1u) != 0;
        const bool hide = ((bar.show_for_not >> category) & 1u) != 0;
        if (!show && !hide) {
            continue;
        }
        bool value = false;
        if (!answered(category, value)) {
            return false;
        }
        if (show && !value) {
            return false;
        }
        if (hide && value) {
            return false;
        }
    }
    return true;
}

// The stored Task field id -> the text the caller supplied for it.  The plain
// Start/Finish/Duration ids are the unnamed date32/i32 fields next to
// StartText/FinishText/DurationText in the descriptor table (analysis/
// unnamed_inferred.tsv: 0x0b400504 is "StartText-1", 0x0b400508
// "DurationText+1"); a stock file's Milestone style stores 0x0b400504, which is
// the start date the product prints beside the diamond.
std::string label_text_for(std::uint32_t field_id, const BarLabelValues& values) {
    switch (field_id) {
        case 0x0b40000eu:
            return values.name;
        case 0x0b400017u:
            return values.id;
        case 0x0b400020u:
            return values.percent;
        case 0x0b400031u:
            return values.resource;
        case 0x0b400503u:
            return values.finish;
        case 0x0b400504u:
            return values.start;
        case 0x0b400505u:
            return values.start;
        case 0x0b400506u:
            return values.finish;
        case 0x0b400507u:
            return values.duration;
        case 0x0b400508u:
            return values.duration;
        default:
            return {};
    }
}

}  // namespace

BarLabelTexts bar_label_texts_for(const msproject::viewstate::GanttStyle& style,
                                  const BarLabelTask& task,
                                  const BarLabelValues& values) {
    BarLabelTexts texts;
    if (!style.decoded) {
        return texts;
    }
    bool claimed[5] = {};
    for (const msproject::viewstate::BarStyle& bar : style.bars) {
        if (!style_applies_to(bar, task)) {
            continue;
        }
        const std::uint32_t fields[5] = {bar.left_text, bar.right_text, bar.top_text,
                                         bar.bottom_text, bar.inside_text};
        std::string* out[5] = {&texts.left, &texts.right, &texts.top, &texts.bottom,
                               &texts.inside};
        for (int side = 0; side < 5; ++side) {
            // The record stores 0xffffffff for "no field"; a synthetic style
            // leaves the default 0.
            if (claimed[side] || fields[side] == 0 || fields[side] == 0xffffffffu) {
                continue;
            }
            claimed[side] = true;
            *out[side] = label_text_for(fields[side], values);
        }
    }
    return texts;
}

bool read_gantt_style(const std::string& path, msproject::viewstate::GanttStyle& out,
                      std::string* reason,
                      msproject::viewstate::CellFormatTable* cell_formats) {
    out = msproject::viewstate::GanttStyle{};
    if (cell_formats != nullptr) {
        *cell_formats = msproject::viewstate::CellFormatTable{};
    }
    try {
        const mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(path);

        if (!document.has_stream(kPropsStream)) {
            fail(reason, "the file has no 214/Props stream");
            return false;
        }
        msproject::props::PropsDocument props;
        const std::vector<std::uint8_t> props_bytes = document.read_stream(kPropsStream);
        if (!msproject::props::parse_props(props_bytes, props, nullptr)) {
            fail(reason, "214/Props does not parse");
            return false;
        }
        const msproject::props::PropsRecord* font_record =
            props.find(kFontBasesRecordId);
        if (font_record == nullptr) {
            fail(reason, "214/Props carries no FONT_BASES record (0x03400000)");
            return false;
        }
        std::vector<msproject::viewstate::FontBase> fonts;
        msproject::viewstate::ParseError parse_error;
        if (!msproject::viewstate::parse_font_bases(props.payload(*font_record), fonts,
                                                    &parse_error)) {
            fail(reason, "FONT_BASES: " + parse_error.message);
            return false;
        }

        std::array<std::vector<std::uint8_t>, 4> raw;
        if (!read_parts(document, kViewParts, raw)) {
            fail(reason, "the file has no complete 214/CV_iew stream");
            return false;
        }
        msproject::viewstate::ViewState views;
        if (!msproject::viewstate::parse_view_state("CV_iew", part_set(raw), views,
                                                    &parse_error)) {
            fail(reason, "214/CV_iew: " + parse_error.message);
            return false;
        }
        const std::string applied = applied_view_name(document);
        const msproject::viewstate::ViewDef* view =
            msproject::viewstate::find_applied_view(views, applied);
        if (view == nullptr) {
            fail(reason, "the file stores no decodable Gantt view");
            return false;
        }
        const std::span<const std::uint8_t> style_data =
            msproject::viewstate::style_data_from_view(views, *view);
        if (!msproject::viewstate::parse_gantt_style(style_data, fonts, out,
                                                     &parse_error)) {
            fail(reason, "view '" + view->name + "': " + parse_error.message);
            return false;
        }
        // Wave 29 (W29-1.2): the same view's per-cell formatting.  A view that
        // stores no such record (the committed synthetic fixture does not) is
        // not an error; the table simply stays undecoded.
        const std::span<const std::uint8_t> cell_data =
            msproject::viewstate::cell_format_data_from_view(views, *view);
        if (cell_formats != nullptr && !cell_data.empty() &&
            !msproject::viewstate::parse_cell_formats(cell_data, *cell_formats,
                                                      &parse_error)) {
            fail(reason, "cell formats: " + parse_error.message);
            return false;
        }
        if (reason != nullptr) {
            reason->clear();
        }
        return true;
    } catch (const std::exception& failure) {
        fail(reason, std::string("cannot open the container: ") + failure.what());
        return false;
    }
}

}  // namespace app
