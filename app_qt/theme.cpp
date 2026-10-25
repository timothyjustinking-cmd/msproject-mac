// app_qt/theme.cpp -- see theme.h.

#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QString>
#include <QStyleHints>

#include <string_view>

#include "shell_style.h"

namespace msqt {
namespace {

// WAVE 26 (W26-6.2): two dark palettes, both measured from the local audit
// frames of Project 2021 in the matching Office theme (git-ignored,
// corpus/reference/2026-10-23-audit/):
//   Dark Gray ...-1792744264666-4mp1fn.jpg:  window/tab row (82,87,90),
//                                           group row (189,189,189),
//                                           status bar (84,84,84)
//   Black     ...-1792744264666-ekrn72.jpg:  window/tab row (25,32,40),
//                                           group row (41,41,41),
//                                           status bar (21,21,21)
// The committed numbers are tests/data/reference/w26-theme.measures.json.
// Derived shades (active tab, hover, field borders, the timeline strip, which
// the capture does not show) are marked in the chrome palette below.
const QColor kDarkGrayWindow(82, 87, 90);
const QColor kDarkGrayBase(59, 64, 67);
const QColor kDarkGrayAlternate(69, 74, 77);
const QColor kDarkGrayText(0xf0, 0xf0, 0xf0);
const QColor kDarkGrayButton(90, 95, 98);
const QColor kDarkGrayAccent(0x2f, 0x6b, 0xa8);

const QColor kBlackWindow(25, 32, 40);
const QColor kBlackBase(16, 22, 27);
const QColor kBlackAlternate(30, 38, 44);
const QColor kBlackText(0xf0, 0xf0, 0xf0);
const QColor kBlackButton(32, 40, 46);
const QColor kBlackAccent(0x2f, 0x6b, 0xa8);

// Office light's measured chrome (the parity reference, shell_style.h).
const QColor kLightWindow(0xf0, 0xf0, 0xf0);
const QColor kLightBase(0xff, 0xff, 0xff);
const QColor kLightText(0x10, 0x10, 0x10);
const QColor kLightButton(0xf0, 0xf0, 0xf0);
const QColor kLightAccent(0xdc, 0xe9, 0xf5);

const QColor kLightRibbonHover(0xe8, 0xee, 0xf5);
const QColor kLightContextual(0x2f, 0x6b, 0x3a);
const QColor kDarkGrayRibbonHover(0xd0, 0xd0, 0xd0);
const QColor kBlackRibbonHover(0x34, 0x3b, 0x42);
const QColor kDarkGrayContextual(0x1f, 0x5a, 0x2c);
const QColor kBlackContextual(0x8f, 0xcf, 0x9f);

// The measured drop-down order and texts (see theme.h).  White is laid out and
// disabled: the port has no palette for it yet.
const std::vector<ThemeChoiceSpec> kChoices = {
    {ThemeChoice::colorful, "Colorful", "colorful", true},
    {ThemeChoice::dark_gray, "Dark Gray", "dark_gray", true},
    {ThemeChoice::black, "Black", "black", true},
    {ThemeChoice::white, "White", "white", false},
    {ThemeChoice::system, "Use system setting", "system", true},
};

void set_dark_palette(const QColor& window, const QColor& base, const QColor& alternate,
                      const QColor& text, const QColor& button, const QColor& accent,
                      const QColor& disabled) {
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternate);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::ToolTipBase, alternate);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    QApplication::setPalette(palette);
}

}  // namespace

const std::vector<ThemeChoiceSpec>& office_theme_choices() { return kChoices; }

bool theme_choice_implemented(ThemeChoice choice) noexcept {
    for (const ThemeChoiceSpec& spec : kChoices) {
        if (spec.choice == choice) {
            return spec.implemented;
        }
    }
    return false;
}

const char* theme_choice_stable_name(ThemeChoice choice) noexcept {
    for (const ThemeChoiceSpec& spec : kChoices) {
        if (spec.choice == choice) {
            return spec.stable_name;
        }
    }
    return "colorful";
}

Theme theme_for_choice(ThemeChoice choice) {
    switch (choice) {
        case ThemeChoice::dark_gray:
            return Theme::dark;
        case ThemeChoice::black:
            return Theme::black;
        case ThemeChoice::system: {
            // Qt 6.5's platform colour scheme.  Unknown (offscreen runners,
            // older platforms) keeps the parity reference's light palette.
            if (QGuiApplication::styleHints() != nullptr &&
                QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark) {
                return Theme::dark;
            }
            return Theme::light;
        }
        case ThemeChoice::colorful:
        case ThemeChoice::white:  // not implemented: never applied
            break;
    }
    return Theme::light;
}

ThemeChoice theme_choice_for_theme(Theme theme) noexcept {
    switch (theme) {
        case Theme::dark:
            return ThemeChoice::dark_gray;
        case Theme::black:
            return ThemeChoice::black;
        case Theme::light:
            break;
    }
    return ThemeChoice::colorful;
}

const char* theme_name(Theme theme) noexcept {
    switch (theme) {
        case Theme::light: return "light";
        case Theme::dark: return "dark";
        case Theme::black: return "black";
    }
    return "light";
}

bool theme_from_name(std::string_view name, Theme& out) noexcept {
    if (name == "light") {
        out = Theme::light;
        return true;
    }
    if (name == "dark") {
        out = Theme::dark;
        return true;
    }
    if (name == "black") {
        out = Theme::black;
        return true;
    }
    return false;
}

QColor theme_window_colour(Theme theme) {
    switch (theme) {
        case Theme::light:
            return kLightWindow;
        case Theme::dark:
            return kDarkGrayWindow;
        case Theme::black:
            return kBlackWindow;
    }
    return kLightWindow;
}

void apply_theme(Theme theme) {
    switch (theme) {
        case Theme::dark:
            set_dark_palette(kDarkGrayWindow, kDarkGrayBase, kDarkGrayAlternate,
                             kDarkGrayText, kDarkGrayButton, kDarkGrayAccent,
                             QColor(0x9a, 0x9a, 0x9a));
            return;
        case Theme::black:
            set_dark_palette(kBlackWindow, kBlackBase, kBlackAlternate, kBlackText,
                             kBlackButton, kBlackAccent, QColor(0x7a, 0x7a, 0x7a));
            return;
        case Theme::light:
            break;
    }
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, kLightWindow);
    palette.setColor(QPalette::WindowText, kLightText);
    palette.setColor(QPalette::Base, kLightBase);
    palette.setColor(QPalette::AlternateBase, kLightBase);
    palette.setColor(QPalette::Text, kLightText);
    palette.setColor(QPalette::Button, kLightButton);
    palette.setColor(QPalette::ButtonText, kLightText);
    palette.setColor(QPalette::Highlight, kLightAccent);
    palette.setColor(QPalette::HighlightedText, kLightText);
    palette.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xe1));
    palette.setColor(QPalette::ToolTipText, kLightText);
    palette.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));
    QApplication::setPalette(palette);
}

const ChromePalette& chrome_palette(Theme theme) {
    // Light: the parity measurements (shell_style.h).
    static const ChromePalette light{
        /*ribbon_background=*/kRibbonBackground,
        /*ribbon_tab_background=*/kRibbonBackground,
        /*ribbon_tab_active=*/kRibbonTabActive,
        /*ribbon_tab_text=*/QColor(0x23, 0x23, 0x23),
        /*ribbon_contextual_text=*/kLightContextual,
        /*ribbon_border=*/kRibbonBorder,
        /*ribbon_hover=*/kLightRibbonHover,
        /*ribbon_text=*/QColor(0x23, 0x23, 0x23),
        /*ribbon_muted=*/QColor(0x6a, 0x6a, 0x6a),
        /*ribbon_field_background=*/QColor(0xff, 0xff, 0xff),
        /*ribbon_field_border=*/QColor(0xb8, 0xb8, 0xb8),
        /*timeline_background=*/kTimelineBackground,
        /*timeline_label=*/QColor(0xf2, 0xf2, 0xf2),
        /*timeline_block=*/QColor(0xfa, 0xfa, 0xfa),
        /*timeline_block_border=*/QColor(0xd0, 0xd0, 0xd0),
        /*timeline_text=*/QColor(0x3a, 0x3a, 0x3a),
        /*timeline_muted=*/QColor(0x6e, 0x6e, 0x6e),
        /*status_background=*/kStatusBackground,
        /*status_text=*/QColor(0x20, 0x20, 0x20),
        /*quick_access_background=*/kRibbonBackground,
        // W29-5.1: the sheet/chart header bands and the one-ink glyph colour,
        // from the wave-21 parity constants (shell_style.h) -- the same values
        // the sheet_views.cpp/gantt_canvas.cpp painters used before this wave.
        /*sheet_header_background=*/kHeaderBackground,
        /*sheet_header_text=*/kHeaderText,
        /*sheet_header_text_muted=*/kHeaderTextMuted,
        /*sheet_header_separator=*/kHeaderSeparator,
        /*sheet_header_arrow=*/QColor(0x6a, 0x6a, 0x6a),
        /*sheet_header_grip=*/QColor(0xb0, 0xb0, 0xb0),
        /*row_header_background=*/kRowGutter,
        /*row_header_text=*/kRowGutterText,
        /*chart_header_background=*/QColor(0xfb, 0xfb, 0xfb),
        /*chart_header_background_alt=*/QColor(0xff, 0xff, 0xff),
        /*chart_header_weekend=*/kWeekend,
        /*chart_header_text=*/QColor(0x33, 0x33, 0x33),
        /*chart_header_rule=*/kGridLine,
        /*chart_header_rule_strong=*/kGridLineStrong,
        /*ribbon_ink=*/QColor(0x1f, 0x3a, 0x5f),
        // W29-5.2: the light QAT (#F7F7F7) and status (#F0F0F0) take the same ink.
        /*window_ink=*/QColor(0x1f, 0x3a, 0x5f),
    };
    // Dark Gray: the captured window/tab row (82,87,90) and group row
    // (189,189,189) put dark text on the light group row and light text on the
    // tab row, exactly as the capture shows.  The active tab, hover, field
    // borders and the timeline strip are derived shades (the capture predates
    // the port's timeline in that theme).
    static const ChromePalette dark{
        /*ribbon_background=*/QColor(189, 189, 189),
        /*ribbon_tab_background=*/kDarkGrayWindow,
        /*ribbon_tab_active=*/QColor(100, 105, 108),
        /*ribbon_tab_text=*/QColor(0xf2, 0xf2, 0xf2),
        /*ribbon_contextual_text=*/kDarkGrayContextual,
        /*ribbon_border=*/QColor(138, 143, 146),
        /*ribbon_hover=*/kDarkGrayRibbonHover,
        /*ribbon_text=*/QColor(0x23, 0x23, 0x23),
        /*ribbon_muted=*/QColor(0x4a, 0x4a, 0x4a),
        /*ribbon_field_background=*/QColor(0xff, 0xff, 0xff),
        /*ribbon_field_border=*/QColor(0x8e, 0x92, 0x94),
        /*timeline_background=*/kDarkGrayWindow,
        /*timeline_label=*/QColor(70, 75, 78),
        /*timeline_block=*/QColor(92, 97, 100),
        /*timeline_block_border=*/QColor(58, 63, 66),
        /*timeline_text=*/QColor(0xf2, 0xf2, 0xf2),
        /*timeline_muted=*/QColor(0xc8, 0xc8, 0xc8),
        /*status_background=*/QColor(84, 84, 84),
        /*status_text=*/QColor(0xf2, 0xf2, 0xf2),
        /*quick_access_background=*/kDarkGrayWindow,
        // W29-5.1: Dark Gray's measured header bands.  The column caption band
        // is the dark of the pair (38,38,38) with light captions; the frozen
        // row gutter and the corner are the mid shade (102,102,102), and the
        // chart timescale band is the same mid shade.  The chart's label ink
        // stays the file's #333333 (the capture's label cores sample at ~#38);
        // its day/tier rule is #5F5F5F and its week rule #545454 (see
        // tests/data/reference/w29-dark-header.measures.json).
        /*sheet_header_background=*/QColor(0x26, 0x26, 0x26),
        /*sheet_header_text=*/QColor(0xf2, 0xf2, 0xf2),
        /*sheet_header_text_muted=*/QColor(0xb0, 0xb0, 0xb0),
        /*sheet_header_separator=*/QColor(0x22, 0x22, 0x22),
        /*sheet_header_arrow=*/QColor(0xb0, 0xb0, 0xb0),
        /*sheet_header_grip=*/QColor(0xb0, 0xb0, 0xb0),
        /*row_header_background=*/QColor(0x66, 0x66, 0x66),
        /*row_header_text=*/QColor(0xf2, 0xf2, 0xf2),
        /*chart_header_background=*/QColor(0x66, 0x66, 0x66),
        /*chart_header_background_alt=*/QColor(0x66, 0x66, 0x66),
        /*chart_header_weekend=*/QColor(0x66, 0x66, 0x66),
        /*chart_header_text=*/QColor(0x33, 0x33, 0x33),
        /*chart_header_rule=*/QColor(0x5f, 0x5f, 0x5f),
        /*chart_header_rule_strong=*/QColor(0x54, 0x54, 0x54),
        // The group row is the light (189,189,189), so the wave-26 ink stays.
        /*ribbon_ink=*/QColor(0x1f, 0x3a, 0x5f),
        // W29-5.2: the QAT (82,87,90) and status (84,84,84) surfaces are dark
        // here, so their artwork takes the light ink (5.9:1 / 6.1:1) rather
        // than the ribbon ink, which would be only 1.5:1 on them.
        /*window_ink=*/QColor(0xe6, 0xe6, 0xe6),
    };
    // Black: the captured window/tab row (25,32,40), group row (41,41,41) and
    // status bar (21,21,21).
    static const ChromePalette black{
        /*ribbon_background=*/QColor(41, 41, 41),
        /*ribbon_tab_background=*/kBlackWindow,
        /*ribbon_tab_active=*/QColor(38, 46, 54),
        /*ribbon_tab_text=*/QColor(0xf2, 0xf2, 0xf2),
        /*ribbon_contextual_text=*/kBlackContextual,
        /*ribbon_border=*/QColor(12, 16, 20),
        /*ribbon_hover=*/kBlackRibbonHover,
        /*ribbon_text=*/QColor(0xe6, 0xe6, 0xe6),
        /*ribbon_muted=*/QColor(0xb0, 0xb0, 0xb0),
        /*ribbon_field_background=*/QColor(27, 34, 40),
        /*ribbon_field_border=*/QColor(74, 82, 88),
        /*timeline_background=*/kBlackWindow,
        /*timeline_label=*/QColor(16, 22, 27),
        /*timeline_block=*/QColor(32, 40, 46),
        /*timeline_block_border=*/QColor(12, 16, 20),
        /*timeline_text=*/QColor(0xf0, 0xf0, 0xf0),
        /*timeline_muted=*/QColor(0xb0, 0xb0, 0xb0),
        /*status_background=*/QColor(21, 21, 21),
        /*status_text=*/QColor(0xf0, 0xf0, 0xf0),
        /*quick_access_background=*/kBlackWindow,
        // W29-5.1: Black swaps the pair -- the column caption band is the mid
        // shade (100,100,100) with light captions, and the row gutter, corner
        // and chart timescale band are the dark one (38,38,38).  The chart's
        // #333333 label ink is near the band (as in the capture); the day/tier
        // rule is #2D2D2D and the week rule #323232.
        /*sheet_header_background=*/QColor(0x64, 0x64, 0x64),
        /*sheet_header_text=*/QColor(0xf2, 0xf2, 0xf2),
        /*sheet_header_text_muted=*/QColor(0xb0, 0xb0, 0xb0),
        /*sheet_header_separator=*/QColor(0x58, 0x58, 0x58),
        /*sheet_header_arrow=*/QColor(0xb0, 0xb0, 0xb0),
        /*sheet_header_grip=*/QColor(0xb0, 0xb0, 0xb0),
        /*row_header_background=*/QColor(0x26, 0x26, 0x26),
        /*row_header_text=*/QColor(0xf2, 0xf2, 0xf2),
        /*chart_header_background=*/QColor(0x26, 0x26, 0x26),
        /*chart_header_background_alt=*/QColor(0x26, 0x26, 0x26),
        /*chart_header_weekend=*/QColor(0x26, 0x26, 0x26),
        /*chart_header_text=*/QColor(0x33, 0x33, 0x33),
        /*chart_header_rule=*/QColor(0x2d, 0x2d, 0x2d),
        /*chart_header_rule_strong=*/QColor(0x32, 0x32, 0x32),
        // The group row is (41,41,41): the dark wave-26 ink would vanish, so
        // Black's one ink is the light text colour (contrast 11.6:1).
        /*ribbon_ink=*/QColor(0xe6, 0xe6, 0xe6),
        // W29-5.2: Black's window/status surfaces are near-black, so the same
        // light ink serves the QAT, menu pop-ups and status bar.
        /*window_ink=*/QColor(0xe6, 0xe6, 0xe6),
    };
    switch (theme) {
        case Theme::dark:
            return dark;
        case Theme::black:
            return black;
        case Theme::light:
            break;
    }
    return light;
}

}  // namespace msqt
