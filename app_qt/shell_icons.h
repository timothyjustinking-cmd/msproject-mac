// app_qt/shell_icons.h -- icons DRAWN BY US.
//
// docs/ui-qt-port.md section 7's second limit: Microsoft's icon bitmaps, ribbon
// images and fonts must not be copied.  The shell therefore has no artwork
// assets; every icon is a small QPainter drawing in this file, generated at run
// time from a fixed palette.  The glyphs are deliberately simple geometric
// marks (a floppy square for Save, two rectangles for Copy, a magnifier for
// Zoom, ...) so the ribbon and quick-access toolbar read as an application
// toolbar without reproducing anyone's artwork.
//
// The per-screen gates assert that every visible ribbon button and every
// quick-access action carries a non-null icon of the pinned size; they never
// compare the pixels to a reference.
#pragma once

#include <QIcon>
#include <QSize>

#include "menu_model.h"

namespace msqt {

// The small icon box (px) for the quick-access toolbar and the status bar.
inline constexpr int kShellIconPx = 16;

// One action's glyph at `pixels` (16 for the status/QAT strip, 32 for the
// ribbon).  MenuAction::none (the ribbon buttons the shell shows but has not
// modelled) gets a neutral rounded-square mark, never a blank icon.
QIcon shell_action_icon(app::MenuAction action, int pixels = kShellIconPx);

// The four status-bar view glyphs, indexed like app::ViewMode (0 task sheet,
// 1 resource sheet, 2 task usage, 3 resource usage).
QIcon shell_view_icon(int view_mode);

// The application/window mark (a small bar chart on a blue tile).
QIcon shell_app_icon();

}  // namespace msqt
