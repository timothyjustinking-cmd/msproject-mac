// app_qt/menus.h -- one translation from app::menu_model to Qt.
//
// The menu bar itself is data (app/menu_model.h); this is the only place that
// knows how a MenuItemSpec's key equivalent becomes a QKeySequence.  The
// modifier rule is the fidelity contract's one deliberate divergence
// (docs/ui-qt-port.md section 2.4): Qt::CTRL is Command on macOS and Control on
// Windows/Linux, so one sequence gives Cmd-S on macOS and Ctrl-S elsewhere.
#pragma once

#include <QKeySequence>

#include "menu_model.h"

namespace msqt {

// The QKeySequence for one item, or an empty sequence when the item has no key
// equivalent (or one this shell cannot map -- it returns empty rather than a
// guess, so the menu never advertises a shortcut it does not have).
QKeySequence menu_shortcut(const app::MenuItemSpec& item);

}  // namespace msqt
