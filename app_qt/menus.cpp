// app_qt/menus.cpp -- see menus.h.

#include "menus.h"

#include <QString>

#include <string_view>

namespace msqt {

QKeySequence menu_shortcut(const app::MenuItemSpec& item) {
    if (item.key == nullptr) {
        return QKeySequence();
    }
    const std::string_view key(item.key);
    if (key.empty()) {
        return QKeySequence();
    }

    // "Ctrl" is Qt's portable name for the command modifier: Qt renders it as
    // Command on macOS (NativeText) and matches it against Command there, and as
    // Control on Windows/Linux.  That is the documented, deliberate divergence.
    QString sequence;
    if (item.command_key) {
        sequence += QStringLiteral("Ctrl+");
    }
    if (item.shift) {
        sequence += QStringLiteral("Shift+");
    }
    if (item.option) {
        sequence += QStringLiteral("Alt+");
    }

    const char code = key.front();
    if (code == '\b') {
        // app/menu_model.cpp writes Backspace as "\b"; QKeySequence's portable
        // name is the only form that parses everywhere.
        sequence += QStringLiteral("Backspace");
    } else if (key == "left" || key == "right") {
        // Wave 28 (W28-2.1): Project's Indent/Outdent keys are Option+Shift+
        // Right/Left.  The menu model stores the key by name (it is not one
        // character); QKeySequence's portable names are Left/Right.
        sequence += key == "left" ? QStringLiteral("Left") : QStringLiteral("Right");
    } else if (key.size() == 1) {
        sequence += QLatin1Char(code);
    } else {
        return QKeySequence();
    }
    return QKeySequence(sequence, QKeySequence::PortableText);
}

}  // namespace msqt
