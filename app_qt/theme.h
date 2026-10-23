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

namespace msqt {

enum class Theme : unsigned char { light = 0, dark = 1 };

// Stable identifiers for the preference store ("light"/"dark").
const char* theme_name(Theme theme) noexcept;
bool theme_from_name(std::string_view name, Theme& out) noexcept;

// Applies the theme's QPalette to QApplication.  Idempotent.
void apply_theme(Theme theme);

// The theme's window colour, for the structural gate (palette swap asserted,
// not eyeballed).
QColor theme_window_colour(Theme theme);

}  // namespace msqt
