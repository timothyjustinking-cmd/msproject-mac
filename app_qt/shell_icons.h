// app_qt/shell_icons.h -- icons DRAWN BY US.
//
// docs/ui-qt-port.md section 7's second limit: Microsoft's icon bitmaps, ribbon
// images and fonts must not be copied.  The shell therefore has no artwork
// assets; every icon is a small QPainter drawing in this file, generated at run
// time from one ink.  The glyphs are deliberately simple geometric marks (a
// floppy square for Save, two rectangles for Copy, a magnifier for Zoom, ...)
// so the menu bar, quick-access toolbar and status bar read as an application
// chrome without reproducing anyone's artwork.
//
// WAVE 29 (W29-5.2): the drawings follow the wave-26 one-ink style -- a 32 px
// grid scaled to `pixels`, round strokes and a single ink.  The caller passes
// the live chrome ink (ChromePalette::window_ink at the shell's call sites, the
// surface these glyphs are painted on); an invalid colour falls back to the
// light theme's ink (#1F3A5F).
//
// The per-screen gates assert that every visible ribbon button, quick-access
// action and status view button carries a non-null icon of the pinned size;
// the wave-29 gate also proves the chrome artwork is monochrome and follows the
// theme's ink.
#pragma once

#include <QColor>
#include <QIcon>
#include <QSize>

#include "menu_model.h"

namespace msqt {

// The small icon box (px) for the quick-access toolbar and the status bar.
inline constexpr int kShellIconPx = 16;

// One action's glyph at `pixels` (16 for the menu/QAT/status strip, 32 for the
// ribbon), drawn in `ink`.  MenuAction::none (the ribbon buttons the shell
// shows but has not modelled) gets a neutral rounded-square mark, never a
// blank icon.
QIcon shell_action_icon(app::MenuAction action, int pixels = kShellIconPx,
                        const QColor& ink = QColor());

// The five status-bar view glyphs, indexed like kViewButtons (0 Gantt Chart,
// 1 Task Usage, 2 Team Planner, 3 Resource Sheet, 4 Report).
QIcon shell_view_icon(int view_mode, int pixels = kShellIconPx,
                      const QColor& ink = QColor());

// The application/window mark (a small bar chart on a blue tile).
QIcon shell_app_icon();

}  // namespace msqt
