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

enum class Theme : unsigned char { light = 0, dark = 1 };

// WAVE 26 (W26-4.2): Project 2021's five Office Theme names, read from the
// clean Account capture's open drop-down
// (corpus/reference/2026-10-23-audit/mcp-computer-use-blob-1792744218543-obuhaj.jpg):
//   Colorful, Dark Gray, Black, White, Use system setting.
// The port implements the light (Colorful) and dark (Dark Gray) palettes and
// resolves "Use system setting" through the platform colour scheme; Black and
// White are laid out and disabled with the exact decision-9 tooltip until a
// later wave paints them.  The enum is the chooser's stable identity.
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
// The palette a choice resolves to.  Black/White are not implemented; they
// return the light palette so a disabled item can never be applied by accident.
Theme theme_for_choice(ThemeChoice choice);
// The implemented choice that shows a palette (Colorful for light, Dark Gray
// for dark), used when the theme is set programmatically.
ThemeChoice theme_choice_for_theme(Theme theme) noexcept;

// Stable identifiers for the preference store ("light"/"dark").
const char* theme_name(Theme theme) noexcept;
bool theme_from_name(std::string_view name, Theme& out) noexcept;

// Applies the theme's QPalette to QApplication.  Idempotent.
void apply_theme(Theme theme);

// The theme's window colour, for the structural gate (palette swap asserted,
// not eyeballed).
QColor theme_window_colour(Theme theme);

}  // namespace msqt
