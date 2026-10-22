// app_qt/shell_style.h -- the fidelity contract's fixed numbers, in one place.
//
// docs/ui-qt-port.md section 2.2: fixed point sizes, integer pixel geometry, no
// per-platform font fallback in the client area.  Everything the shell draws
// takes its metrics from here (or from the app/ models), never from the
// platform's default UI metrics.  The golden images depend on these values.
#pragma once

namespace msqt {

// The window the render tests pin (900x560, the size the skeleton proved).
inline constexpr int kWindowWidth = 900;
inline constexpr int kWindowHeight = 560;

// One fixed size per surface, whatever the platform's default UI font is.  The
// font FAMILY is still the platform's; pinning a bundled font file is the
// golden-image stream's job (Q4), which is the one part of section 2.2 not done.
inline constexpr int kBodyPointSize = 9;    // sheet cells, Gantt bars, status line
inline constexpr int kHeaderPointSize = 8;  // Gantt timescale tiers
inline constexpr int kStatusPointSize = 9;  // the status line

}  // namespace msqt
