// app/prefs.cpp -- see prefs.h for the contract and the measured reason the
// preferences are app-side rather than written into a .mpp.
//
// No AppKit here: the macOS shell reads/writes NSUserDefaults and hands the
// string to parse_preferences()/serialize_preferences().  That keeps the whole
// model -- including the tolerant parser and the app-side duration rendering --
// provable on Windows (app/prefs_tests.cpp).

#include "prefs.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "layout_model.h"
#include "value.h"

namespace app {
namespace {

using core::view::Value;
using core::view::ValueKind;

std::string_view trim(std::string_view text) {
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
    return text.substr(begin, end - begin);
}

// The working-day length a "days" rendering divides by.  A nonsensical stored
// value falls back to the core's own 8-hour default rather than dividing by 0.
std::int64_t working_day_ticks(const core::sheet::SheetFormat& format) noexcept {
    int hours = format.hours_per_day;
    if (hours <= 0 || hours > 24) {
        hours = 8;
    }
    return static_cast<std::int64_t>(hours) * core::view::kTicksPerHour;
}

// A tick count as a decimal number of `unit_ticks`: at most two decimals,
// trailing zeros dropped, `decimal_separator` between the parts.  Integer
// arithmetic only, so it cannot pick up a locale's decimal point.
std::string render_quantity(std::int64_t ticks, std::int64_t unit_ticks,
                            char decimal_separator) {
    const std::uint64_t unit =
        unit_ticks > 0 ? static_cast<std::uint64_t>(unit_ticks) : 1u;
    const bool negative = ticks < 0;
    const std::uint64_t magnitude =
        negative ? static_cast<std::uint64_t>(-(ticks + 1)) + 1u
                 : static_cast<std::uint64_t>(ticks);
    const std::uint64_t hundredths = (magnitude * 100u + unit / 2u) / unit;

    std::string text;
    if (negative && hundredths != 0) {
        text.push_back('-');
    }
    text += std::to_string(hundredths / 100u);
    std::uint64_t fraction = hundredths % 100u;
    if (fraction != 0) {
        std::string digits;
        digits.push_back(static_cast<char>('0' + fraction / 10u));
        digits.push_back(static_cast<char>('0' + fraction % 10u));
        while (!digits.empty() && digits.back() == '0') {
            digits.pop_back();
        }
        text.push_back(decimal_separator);
        text += digits;
    }
    return text;
}

const char* label_for(const std::string& number, const core::sheet::UnitLabels& units,
                      bool days) {
    const bool singular = number == "1";
    if (days) {
        return singular ? units.day_singular.c_str() : units.day_plural.c_str();
    }
    return singular ? units.hour_singular.c_str() : units.hour_plural.c_str();
}

bool parse_bool(std::string_view text, bool& out) noexcept {
    if (text == "1" || text == "true" || text == "yes" || text == "on") {
        out = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "no" || text == "off") {
        out = false;
        return true;
    }
    return false;
}

bool parse_int(std::string_view text, int& out) noexcept {
    if (text.empty()) {
        return false;
    }
    int value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10 + (character - '0');
        if (value > 1000000) {
            return false;
        }
    }
    out = value;
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------

const char* date_style_name(DateStyle style) noexcept {
    switch (style) {
        case DateStyle::day_month_year: return "day_month_year";
        case DateStyle::month_day_year: return "month_day_year";
        case DateStyle::iso: return "iso";
    }
    return "day_month_year";
}

const char* duration_style_name(DurationStyle style) noexcept {
    switch (style) {
        case DurationStyle::auto_units: return "auto_units";
        case DurationStyle::hours: return "hours";
        case DurationStyle::days: return "days";
    }
    return "auto_units";
}

const char* zoom_level_name(ZoomLevel level) noexcept {
    switch (level) {
        case ZoomLevel::day: return "day";
        case ZoomLevel::week: return "week";
        case ZoomLevel::month: return "month";
    }
    return "day";
}

bool date_style_from_name(std::string_view name, DateStyle& out) noexcept {
    if (name == "day_month_year") {
        out = DateStyle::day_month_year;
        return true;
    }
    if (name == "month_day_year") {
        out = DateStyle::month_day_year;
        return true;
    }
    if (name == "iso") {
        out = DateStyle::iso;
        return true;
    }
    return false;
}

bool duration_style_from_name(std::string_view name, DurationStyle& out) noexcept {
    if (name == "auto_units") {
        out = DurationStyle::auto_units;
        return true;
    }
    if (name == "hours") {
        out = DurationStyle::hours;
        return true;
    }
    if (name == "days") {
        out = DurationStyle::days;
        return true;
    }
    return false;
}

bool zoom_level_from_name(std::string_view name, ZoomLevel& out) noexcept {
    if (name == "day") {
        out = ZoomLevel::day;
        return true;
    }
    if (name == "week") {
        out = ZoomLevel::week;
        return true;
    }
    if (name == "month") {
        out = ZoomLevel::month;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Applying a preference
// ---------------------------------------------------------------------------

core::sheet::DateFormat date_format_for(const Preferences& preferences) noexcept {
    switch (preferences.date_style) {
        case DateStyle::day_month_year:
            return core::sheet::DateFormat::short_day_month_year();
        case DateStyle::month_day_year:
            return core::sheet::DateFormat::short_month_day_year();
        case DateStyle::iso:
            return core::sheet::DateFormat::iso();
    }
    return core::sheet::DateFormat::short_day_month_year();
}

void apply_preferences(const Preferences& preferences,
                       core::sheet::SheetFormat& format) noexcept {
    // The date format is the one presentation choice core::sheet owns outright
    // (every date cell, the status line and the PDF report read SheetFormat).
    format.date = date_format_for(preferences);
}

core::gantt::GanttZoom gantt_zoom(ZoomLevel level) noexcept {
    switch (level) {
        case ZoomLevel::day: return core::gantt::GanttZoom::day;
        case ZoomLevel::week: return core::gantt::GanttZoom::week;
        case ZoomLevel::month: return core::gantt::GanttZoom::month;
    }
    return core::gantt::GanttZoom::day;
}

ZoomLevel zoom_level_of(core::gantt::GanttZoom zoom) noexcept {
    switch (zoom) {
        case core::gantt::GanttZoom::day: return ZoomLevel::day;
        case core::gantt::GanttZoom::week: return ZoomLevel::week;
        case core::gantt::GanttZoom::month: return ZoomLevel::month;
    }
    return ZoomLevel::day;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

std::string serialize_preferences(const Preferences& preferences) {
    std::string text = "version=" + std::to_string(preferences.version);
    text += ";date=";
    text += date_style_name(preferences.date_style);
    text += ";duration=";
    text += duration_style_name(preferences.duration_style);
    text += ";zoom=";
    text += zoom_level_name(preferences.zoom);
    text += ";shading=";
    text += preferences.working_day_shading ? "1" : "0";
    return text;
}

bool parse_preferences(std::string_view text, Preferences& out) {
    Preferences parsed;  // defaults: an unreadable string never breaks launch
    bool read_any = false;
    std::size_t position = 0;
    while (position <= text.size()) {
        const std::size_t end = text.find(';', position);
        const std::string_view token =
            trim(text.substr(position, end == std::string_view::npos ? text.size() - position
                                                                    : end - position));
        const std::size_t equals = token.find('=');
        if (equals != std::string_view::npos) {
            const std::string_view key = trim(token.substr(0, equals));
            const std::string_view value = trim(token.substr(equals + 1));
            if (key == "version") {
                int version = 0;
                if (parse_int(value, version) && version > 0) {
                    parsed.version = version;
                    read_any = true;
                }
            } else if (key == "date") {
                DateStyle style = parsed.date_style;
                if (date_style_from_name(value, style)) {
                    parsed.date_style = style;
                    read_any = true;
                }
            } else if (key == "duration") {
                DurationStyle style = parsed.duration_style;
                if (duration_style_from_name(value, style)) {
                    parsed.duration_style = style;
                    read_any = true;
                }
            } else if (key == "zoom") {
                ZoomLevel level = parsed.zoom;
                if (zoom_level_from_name(value, level)) {
                    parsed.zoom = level;
                    read_any = true;
                }
            } else if (key == "shading") {
                bool shading = parsed.working_day_shading;
                if (parse_bool(value, shading)) {
                    parsed.working_day_shading = shading;
                    read_any = true;
                }
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        position = end + 1;
    }
    out = parsed;
    return read_any;
}

std::string preferences_summary(const Preferences& preferences) {
    std::string text = "date ";
    switch (preferences.date_style) {
        case DateStyle::day_month_year: text += "day/month/year"; break;
        case DateStyle::month_day_year: text += "month/day/year"; break;
        case DateStyle::iso: text += "ISO"; break;
    }
    text += ", duration ";
    switch (preferences.duration_style) {
        case DurationStyle::auto_units: text += "days + hours"; break;
        case DurationStyle::hours: text += "hours"; break;
        case DurationStyle::days: text += "days"; break;
    }
    text += ", timescale ";
    text += zoom_level_name(preferences.zoom);
    text += ", working-day shading ";
    text += preferences.working_day_shading ? "on" : "off";
    return text;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

std::string format_duration(std::int64_t ticks, const core::sheet::SheetFormat& format,
                            DurationStyle style) {
    if (style == DurationStyle::auto_units) {
        return core::sheet::format_duration_text(ticks, format);
    }
    const bool as_days = style == DurationStyle::days;
    const std::int64_t unit_ticks =
        as_days ? working_day_ticks(format) : core::view::kTicksPerHour;
    const std::string number = render_quantity(ticks, unit_ticks, format.decimal_separator);
    std::string text = number;
    const char* label = label_for(number, format.units, as_days);
    if (label != nullptr && label[0] != '\0') {
        text.push_back(' ');
        text += label;
    }
    return text;
}

std::string cell_text(const core::sheet::SheetModel& sheet, std::size_t row,
                      std::size_t column, const Preferences& preferences) {
    if (preferences.duration_style != DurationStyle::auto_units &&
        column < sheet.column_count()) {
        const core::view::FieldId duration_field = task_field_id("Duration");
        if (duration_field != 0 && sheet.column(column).field == duration_field) {
            const Value* value = sheet.cell_value(row, column);
            if (value != nullptr && (value->kind == ValueKind::Duration ||
                                     value->kind == ValueKind::Integer ||
                                     value->kind == ValueKind::Number)) {
                const std::int64_t ticks =
                    value->kind == ValueKind::Number
                        ? static_cast<std::int64_t>(value->number)
                        : value->integer;
                return format_duration(ticks, sheet.format(), preferences.duration_style);
            }
        }
    }
    return sheet.cell_text(row, column);
}

}  // namespace app
