// app_qt/ribbon_glyphs.h -- the ribbon's command glyphs, DRAWN BY US (W26-2.1).
//
// docs/ui-qt-port.md section 7: Microsoft's icon bitmaps must not be copied.
// Every ribbon command therefore carries an original monochrome mark painted
// from a 32-unit design grid: a 2 px stroke at the 32 px large size and the
// same drawing at half scale (1 px) for the 16 px one-row size.  One ink colour
// only, so the icon gate can prove "monochrome" from the rendered pixels.
//
// `key` is the pin's icon key (app_qt/ribbon_pin.tsv, the independent readback's
// icon column).  An empty or unknown key draws the neutral placeholder, never a
// blank icon, so a command without artwork fails the gate's placeholder hash
// instead of passing silently.
#pragma once

#include <QIcon>
#include <QString>

namespace msqt {

// One command's glyph at `pixels` (16 or 32).  Any other value falls back to
// 16.  The glyphs are grouped by ribbon tab in ribbon_glyphs.cpp.
QIcon shell_ribbon_icon(const QString& key, int pixels);

// True when `key` names a glyph this file draws.  The gate's real proof is the
// rendered pixel hash; this predicate is what the pin/contact-sheet report uses
// to name a key that has no drawing.
bool shell_ribbon_glyph_known(const QString& key);

}  // namespace msqt
