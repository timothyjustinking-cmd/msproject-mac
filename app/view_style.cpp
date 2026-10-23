// app/view_style.cpp -- read a file's Gantt style record through the shared
// core decoders (see view_style.h).

#include "view_style.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <utility>
#include <vector>

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

bool read_gantt_style(const std::string& path, msproject::viewstate::GanttStyle& out,
                      std::string* reason) {
    out = msproject::viewstate::GanttStyle{};
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
