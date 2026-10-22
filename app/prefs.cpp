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

// Wave 12: a field id in the stored column list.  Hex with an optional 0x
// prefix; decimal is deliberately not accepted so the two cannot be confused.
bool parse_field_key(std::string_view text, std::uint32_t& out) noexcept {
    if (text.size() > 2 && (text[0] == '0') && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
    }
    if (text.empty() || text.size() > 8) {
        return false;
    }
    std::uint32_t value = 0;
    for (const char character : text) {
        std::uint32_t digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint32_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<std::uint32_t>(character - 'a') + 10u;
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint32_t>(character - 'A') + 10u;
        } else {
            return false;
        }
        value = (value << 4) | digit;
    }
    out = value;
    return value != 0;
}

// "0x0b400033" -- the stored form of a field id.
std::string field_key(std::uint32_t field) {
    static const char digits[] = "0123456789abcdef";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text.push_back(digits[(field >> static_cast<unsigned>(shift)) & 0x0fu]);
    }
    return text;
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

bool column_visible(const Preferences& preferences, core::view::FieldId field,
                    bool default_visible) noexcept {
    if (field == 0) {
        return default_visible;
    }
    for (const ColumnVisibility& column : preferences.column_visibility) {
        if (column.field == field) {
            return column.visible;
        }
    }
    return default_visible;
}

bool set_column_visible(Preferences& preferences, core::view::FieldId field, bool visible,
                        bool default_visible) noexcept {
    if (field == 0) {
        return false;
    }
    for (ColumnVisibility& column : preferences.column_visibility) {
        if (column.field != field) {
            continue;
        }
        if (visible == default_visible) {
            // A choice equal to the default is not a preference: dropping it
            // keeps the stored line (and the parser) honest and short.
            for (std::size_t index = 0; index < preferences.column_visibility.size();
                 ++index) {
                if (preferences.column_visibility[index].field == field) {
                    preferences.column_visibility.erase(
                        preferences.column_visibility.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    break;
                }
            }
        } else {
            column.visible = visible;
        }
        return true;
    }
    if (visible == default_visible) {
        return true;  // nothing recorded, and nothing needs to be
    }
    if (preferences.column_visibility.size() >= kMaxColumnVisibility) {
        return false;
    }
    ColumnVisibility entry;
    entry.field = field;
    entry.visible = visible;
    preferences.column_visibility.push_back(entry);
    return true;
}

bool formula_column_visible(const Preferences& preferences, std::size_t index) noexcept {
    bool fallback = true;
    if (index == 0) {
        fallback = preferences.formula_duration_column;
    } else if (index == 1) {
        fallback = preferences.formula_date_column;
    } else {
        return true;
    }
    const std::vector<FormulaColumnSpec>& specs = formula_column_specs();
    if (index >= specs.size() || specs[index].field == 0) {
        return fallback;
    }
    return column_visible(preferences, specs[index].field, fallback);
}

bool set_formula_column_visible(Preferences& preferences, std::size_t index,
                                bool visible) noexcept {
    bool fallback = true;
    if (index == 0) {
        preferences.formula_duration_column = visible;
    } else if (index == 1) {
        preferences.formula_date_column = visible;
    } else {
        return false;
    }
    // The demo column's field id may be unresolved in a table-less build; the
    // flag above still carries the choice, so this is only the per-column
    // override on top of it.
    const std::vector<FormulaColumnSpec>& specs = formula_column_specs();
    if (index < specs.size() && specs[index].field != 0) {
        (void)set_column_visible(preferences, specs[index].field, visible, fallback);
    }
    return true;
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
    // Wave-11 keys: additive, so a string written before this wave still parses.
    text += ";view=";
    text += view_mode_name(preferences.view);
    text += ";usage=";
    text += core::usage::period_name(preferences.usage_period);
    text += ";formula1=";
    text += preferences.formula_duration_column ? "1" : "0";
    text += ";formula2=";
    text += preferences.formula_date_column ? "1" : "0";
    text += ";custom=";
    text += preferences.stored_custom_fields ? "1" : "0";
    // Wave 12: the per-column overrides (stored custom fields and formula
    // columns), as field id:visibility pairs.  Only deviations are listed.
    text += ";columns=";
    bool first_column = true;
    for (const ColumnVisibility& column : preferences.column_visibility) {
        if (column.field == 0) {
            continue;
        }
        if (!first_column) {
            text.push_back(',');
        }
        first_column = false;
        text += field_key(column.field);
        text += column.visible ? ":1" : ":0";
    }
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
            } else if (key == "view") {
                ViewMode mode = parsed.view;
                if (view_mode_from_name(value, mode)) {
                    parsed.view = mode;
                    read_any = true;
                }
            } else if (key == "usage") {
                core::usage::Period period = parsed.usage_period;
                if (core::usage::period_from_name(value, period)) {
                    parsed.usage_period = period;
                    read_any = true;
                }
            } else if (key == "formula1") {
                bool shown = parsed.formula_duration_column;
                if (parse_bool(value, shown)) {
                    parsed.formula_duration_column = shown;
                    read_any = true;
                }
            } else if (key == "formula2") {
                bool shown = parsed.formula_date_column;
                if (parse_bool(value, shown)) {
                    parsed.formula_date_column = shown;
                    read_any = true;
                }
            } else if (key == "custom") {
                bool shown = parsed.stored_custom_fields;
                if (parse_bool(value, shown)) {
                    parsed.stored_custom_fields = shown;
                    read_any = true;
                }
            } else if (key == "columns") {
                // "0x0b400033:1,0x0b400057:0".  A malformed entry is skipped,
                // never fatal; an empty value clears the list.
                parsed.column_visibility.clear();
                std::size_t entry_position = 0;
                while (entry_position <= value.size()) {
                    const std::size_t comma = value.find(',', entry_position);
                    const std::string_view entry = trim(value.substr(
                        entry_position, comma == std::string_view::npos
                                            ? value.size() - entry_position
                                            : comma - entry_position));
                    if (!entry.empty()) {
                        const std::size_t colon = entry.find(':');
                        std::uint32_t field = 0;
                        bool shown = true;
                        if (colon != std::string_view::npos &&
                            parse_field_key(trim(entry.substr(0, colon)), field) &&
                            parse_bool(trim(entry.substr(colon + 1)), shown) &&
                            parsed.column_visibility.size() < kMaxColumnVisibility) {
                            ColumnVisibility column;
                            column.field = field;
                            column.visible = shown;
                            parsed.column_visibility.push_back(column);
                            read_any = true;
                        }
                    }
                    if (comma == std::string_view::npos) {
                        break;
                    }
                    entry_position = comma + 1;
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
    text += ", view ";
    text += view_mode_name(preferences.view);
    text += ", usage ";
    text += core::usage::period_name(preferences.usage_period);
    text += ", formula columns ";
    text += std::to_string((preferences.formula_duration_column ? 1 : 0) +
                           (preferences.formula_date_column ? 1 : 0));
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
    // Any Duration cell goes through the app-side rendering when the preference
    // asks for hours or days.  In the task sheet that is the Duration column; in
    // the usage grids it is every period and Total cell, which is exactly where
    // "8 hrs" rather than "1 day" reads best.  A non-Duration value (text, date,
    // number) is never touched -- the sheet's own text is returned.
    if (preferences.duration_style != DurationStyle::auto_units &&
        column < sheet.column_count()) {
        const Value* value = sheet.cell_value(row, column);
        if (value != nullptr && value->kind == ValueKind::Duration) {
            return format_duration(value->integer, sheet.format(), preferences.duration_style);
        }
    }
    return sheet.cell_text(row, column);
}

}  // namespace app
