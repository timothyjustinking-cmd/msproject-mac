// app_qt/theme.h -- the two Office themes the shell must render.
//
// Operator decision 10 (docs/ui-parity-spec.md): light AND dark, switchable from
// Options > General (and the Preferences dialog).  Light is the parity
// reference.  The palette here is applied through QApplication, so standard
// widgets (dialogs, menus, scrollbars) follow it; the shell's custom-painted
// surfaces take their colours from shell_style.h for light and will grow dark
// variants as the theme work lands.
//
// No Q_OBJECT: a plain enum and functions.
#pragma once

#include <QColor>
#include <string_view>
#include <vector>

namespace msqt {

// The resolved palettes the shell can paint.  WAVE 26 (W26-6.2) adds Black as
// its own palette: Dark Gray and Black are two measured Project themes, not two
// names for one dark mode.  `dark` is Project's Dark Gray.
enum class Theme : unsigned char { light = 0, dark = 1, black = 2 };

// WAVE 26 (W26-4.2): Project 2021's five Office Theme names, read from the
// clean Account capture's open drop-down
// (corpus/reference/2026-10-23-audit/mcp-computer-use-blob-1792744218543-obuhaj.jpg):
//   Colorful, Dark Gray, Black, White, Use system setting.
// The port implements the light (Colorful), Dark Gray and (from W26-6.2) Black
// palettes and resolves "Use system setting" through the platform colour
// scheme; White is laid out and disabled with the exact decision-9 tooltip
// until a later wave paints it.  The enum is the chooser's stable identity.
enum class ThemeChoice : unsigned char {
    colorful = 0,
    dark_gray,
    black,
    white,
    system,
};

struct ThemeChoiceSpec {
    ThemeChoice choice;
    const char* name;         // the capture's display text
    const char* stable_name;  // persisted identity ("colorful", ...)
    bool implemented;
};

// The five choices in the capture's drop-down order.
const std::vector<ThemeChoiceSpec>& office_theme_choices();

bool theme_choice_implemented(ThemeChoice choice) noexcept;
const char* theme_choice_stable_name(ThemeChoice choice) noexcept;
// The palette a choice resolves to.  White is not implemented; it returns the
// light palette so a disabled item can never be applied by accident.
Theme theme_for_choice(ThemeChoice choice);
// The implemented choice that shows a palette (Colorful for light, Dark Gray
// for dark, Black for black), used when the theme is set programmatically.
ThemeChoice theme_choice_for_theme(Theme theme) noexcept;

// Stable identifiers for the preference store ("light"/"dark"/"black").
const char* theme_name(Theme theme) noexcept;
bool theme_from_name(std::string_view name, Theme& out) noexcept;

// Applies the theme's QPalette to QApplication.  Idempotent.
void apply_theme(Theme theme);

// The theme's window colour, for the structural gate (palette swap asserted,
// not eyeballed).
QColor theme_window_colour(Theme theme);

// ---------------------------------------------------------------------------
// WAVE 26 (W26-6.2): the CHROME palette
// ---------------------------------------------------------------------------
//
// Decision 10 and operator Q14: in Dark Gray and Black only the chrome darkens;
// the sheet and chart keep the file's colours.  WAVE 29 (W29-5.1) closes the two
// gaps wave 26 recorded: the sheet/chart HEADER BANDS are chrome and darken with
// the theme (their measured fills are below; the sheet's cells and the chart's
// body still keep the file's colours), and the ribbon's one-ink glyphs take a
// per-theme ink so they stay legible on each measured ribbon surface.  The
// shell's custom-painted surfaces (the ribbon, the quick-access strip, the
// timeline strip, the status bar, the sheet captions and the chart header)
// therefore take their colours from here rather than from static shell_style
// constants, and this struct is what set_theme() repaints them with.
//
// The light values ARE the parity measurements (shell_style.h: ribbon #F7F7F7,
// status #F0F0F0).  The two dark palettes were sampled from the local audit
// frames of Project 2021 in each theme (git-ignored:
// corpus/reference/2026-10-23-audit/mcp-computer-use-blob-1792744264666-4mp1fn.jpg
// = Dark Gray, ...-ekrn72.jpg = Black); the committed numbers are
// tests/data/reference/w26-theme.measures.json.  White stays unimplemented.
struct ChromePalette {
    QColor ribbon_background;      // the group-row surface
    QColor ribbon_tab_background;  // the tab row above it
    QColor ribbon_tab_active;      // the selected tab's fill
    QColor ribbon_tab_text;        // tab labels (the tab row can be darker)
    QColor ribbon_contextual_text; // the contextual Gantt Chart Format tab
    QColor ribbon_border;          // the bottom rule and group separators
    QColor ribbon_hover;
    QColor ribbon_text;            // labels on the group-row surface
    QColor ribbon_muted;           // group captions
    QColor ribbon_field_background;  // combos and the date field
    QColor ribbon_field_border;
    QColor timeline_background;
    QColor timeline_label;         // the vertical TIMELINE block
    QColor timeline_block;         // Start/Finish blocks
    QColor timeline_block_border;
    QColor timeline_text;
    QColor timeline_muted;
    QColor status_background;
    QColor status_text;
    QColor quick_access_background;
    // W29-5.1: the sheet's caption band and frozen row-number gutter.  Light
    // values are the wave-21 parity measurements (shell_style.h); the two dark
    // palettes were sampled from the same local audit frames as the chrome
    // above (tests/data/reference/w29-dark-header.measures.json).
    QColor sheet_header_background;   // the column caption band's fill
    QColor sheet_header_text;         // caption ink
    QColor sheet_header_text_muted;   // the Add New Column stub's ink
    QColor sheet_header_separator;    // the caption band's column rules
    QColor sheet_header_arrow;        // the sort chevron
    QColor sheet_header_grip;         // the column-resize ticks
    QColor row_header_background;     // the frozen gutter and the corner widget
    QColor row_header_text;           // the row numbers
    // W29-5.1: the chart timescale band.  The band is chrome and follows the
    // theme; the chart BODY below it keeps the file's colours, and the label ink
    // stays the file's #333333 in all three themes (measured).
    QColor chart_header_background;      // the top tier's fill
    QColor chart_header_background_alt;  // the lower tiers' fill (light differs)
    QColor chart_header_weekend;         // weekend day cells in the day tier
    QColor chart_header_text;            // the label ink when the file has none
    QColor chart_header_rule;            // the day tier's ordinary cell rules
    QColor chart_header_rule_strong;     // Monday/week boundaries and tier rules
    // W29-5.1: the ribbon's one-ink glyph colour (decision 3).  Dark Gray's
    // measured group row is light, so the wave-26 ink stays; Black's measured
    // group row is near-black, so its glyphs switch to the light ink.
    QColor ribbon_ink;
    // W29-5.2: the ink for artwork on the WINDOW-coloured chrome -- the
    // quick-access strip, menu pop-ups and the status bar.  In Dark Gray the
    // ribbon's group row is light but these surfaces are the dark window
    // (82,87,90) and status (84,84,84) shades, where the dark ribbon ink is
    // only 1.5:1; they take the light ink instead (>= 5.8:1).  Light keeps
    // #1F3A5F and Black already uses the light ink.
    QColor window_ink;
};

const ChromePalette& chrome_palette(Theme theme);

}  // namespace msqt
