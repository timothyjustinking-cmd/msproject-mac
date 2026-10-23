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
// the sheet and chart keep the file's colours.  The shell's custom-painted
// surfaces (the ribbon, the quick-access strip, the timeline strip and the
// status bar) therefore take their colours from here rather than from static
// shell_style constants, and this struct is what set_theme() repaints them with.
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
};

const ChromePalette& chrome_palette(Theme theme);

}  // namespace msqt
