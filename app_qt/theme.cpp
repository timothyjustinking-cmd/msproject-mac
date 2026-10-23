// app_qt/theme.cpp -- see theme.h.

#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QString>
#include <QStyleHints>

#include <string_view>

namespace msqt {
namespace {

// Office 2021's own dark values, as agreed with the operator: a near-black
// window, a slightly lighter base, a muted button face and the Project accent
// for selection.  (These are our recorded values, not sampled artwork.)
const QColor kDarkWindow(0x1f, 0x1f, 0x1f);
const QColor kDarkBase(0x26, 0x26, 0x26);
const QColor kDarkAlternate(0x2b, 0x2b, 0x2b);
const QColor kDarkText(0xe6, 0xe6, 0xe6);
const QColor kDarkButton(0x33, 0x33, 0x33);
const QColor kDarkAccent(0x2f, 0x6b, 0xa8);

// The measured drop-down order and texts (see theme.h).  Black and White are
// laid out and disabled: the port has no palettes for them yet.
const std::vector<ThemeChoiceSpec> kChoices = {
    {ThemeChoice::colorful, "Colorful", "colorful", true},
    {ThemeChoice::dark_gray, "Dark Gray", "dark_gray", true},
    {ThemeChoice::black, "Black", "black", false},
    {ThemeChoice::white, "White", "white", false},
    {ThemeChoice::system, "Use system setting", "system", true},
};

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
        case ThemeChoice::black:  // not implemented: never applied
        case ThemeChoice::white:  // not implemented: never applied
            break;
    }
    return Theme::light;
}

ThemeChoice theme_choice_for_theme(Theme theme) noexcept {
    return theme == Theme::dark ? ThemeChoice::dark_gray : ThemeChoice::colorful;
}

const char* theme_name(Theme theme) noexcept {
    switch (theme) {
        case Theme::light: return "light";
        case Theme::dark: return "dark";
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
    return false;
}

QColor theme_window_colour(Theme theme) {
    return theme == Theme::dark ? kDarkWindow : QColor(0xf0, 0xf0, 0xf0);
}

void apply_theme(Theme theme) {
    QPalette palette = QApplication::palette();
    if (theme == Theme::dark) {
        palette.setColor(QPalette::Window, kDarkWindow);
        palette.setColor(QPalette::WindowText, kDarkText);
        palette.setColor(QPalette::Base, kDarkBase);
        palette.setColor(QPalette::AlternateBase, kDarkAlternate);
        palette.setColor(QPalette::Text, kDarkText);
        palette.setColor(QPalette::Button, kDarkButton);
        palette.setColor(QPalette::ButtonText, kDarkText);
        palette.setColor(QPalette::Highlight, kDarkAccent);
        palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        palette.setColor(QPalette::ToolTipBase, kDarkAlternate);
        palette.setColor(QPalette::ToolTipText, kDarkText);
        palette.setColor(QPalette::PlaceholderText, QColor(0x9a, 0x9a, 0x9a));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x88, 0x88, 0x88));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x88, 0x88, 0x88));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x88, 0x88, 0x88));
    } else {
        palette.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
        palette.setColor(QPalette::WindowText, QColor(0x10, 0x10, 0x10));
        palette.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
        palette.setColor(QPalette::AlternateBase, QColor(0xff, 0xff, 0xff));
        palette.setColor(QPalette::Text, QColor(0x10, 0x10, 0x10));
        palette.setColor(QPalette::Button, QColor(0xf0, 0xf0, 0xf0));
        palette.setColor(QPalette::ButtonText, QColor(0x10, 0x10, 0x10));
        palette.setColor(QPalette::Highlight, QColor(0xdc, 0xe9, 0xf5));
        palette.setColor(QPalette::HighlightedText, QColor(0x10, 0x10, 0x10));
        palette.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xe1));
        palette.setColor(QPalette::ToolTipText, QColor(0x10, 0x10, 0x10));
        palette.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));
    }
    QApplication::setPalette(palette);
}

}  // namespace msqt
