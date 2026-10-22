// app_qt/shell_style.h -- the fidelity contract's fixed numbers, in one place.
//
// docs/ui-qt-port.md section 2.2: fixed point sizes, integer pixel geometry, no
// per-platform font fallback in the client area.  Everything the shell draws
// takes its metrics from here (or from the app/ models), never from the
// platform's default UI metrics.  The golden images depend on these values.
//
// WAVE 17 Q2: the numbers below are P1's MEASURED reference values
// (scripts/parity_report.py over corpus/bench/msproject-gantt.png; the baseline
// is docs/parity.md section 2), not the earlier estimates:
//
//   row pitch 22 px, sheet header 42 px, row-number gutter 60 px,
//   9 Entry columns 39/57/147/81/81/81/105/105/123 (sheet span 819 px),
//   sheet/Gantt splitter at 881/1920 = 0.45885 of the client width,
//   24 px per day (week 168 px), 2 timescale tiers,
//   header fill white (255,255,255), grid (212,212,212), weekend (247,247,247).
//
// The reference capture has no visible task bars (the fixture's tasks are
// manually scheduled), so the bar colours below are the shell's own palette and
// are NOT measured; they stay for the next capture (docs/parity.md section 2.3).
//
// TYPOGRAPHY.  The reference is Calibri 11 pt.  Carlito is the metric-compatible
// open substitute and is preferred when installed; it is NOT installed on this
// machine and downloading is forbidden, so the shell falls back to a fixed
// family list (Calibri first: it IS installed on Windows) and reports which
// family it got.  See kFontFamilies and ui_font_family().
#pragma once

#include <QColor>
#include <QString>

namespace msqt {

// The window the render tests pin (900x560, the size the skeleton proved).
inline constexpr int kWindowWidth = 900;
inline constexpr int kWindowHeight = 560;

// ---------------------------------------------------------------------------
// Typography
// ---------------------------------------------------------------------------

inline constexpr int kBodyPointSize = 11;      // Calibri 11 pt: sheet cells
inline constexpr int kHeaderPointSize = 11;    // sheet header captions
inline constexpr int kTimescalePointSize = 9;  // Gantt two-tier timescale
inline constexpr int kStatusPointSize = 9;
inline constexpr int kRibbonPointSize = 8;
inline constexpr int kGroupLabelPointSize = 8;

// The fixed family list, in order.  Carlito first so an installation of the
// metric-compatible open font wins over the proprietary original; Calibri next
// because that is the reference's own font and is present on Windows; the rest
// keep the Linux CI deterministic.
inline constexpr const char* kFontFamilies[] = {"Carlito", "Calibri", "Liberation Sans",
                                                "DejaVu Sans", "Noto Sans", "Arial"};
inline constexpr int kFontFamilyCount = 6;

// ---------------------------------------------------------------------------
// Sheet geometry (docs/parity.md section 2)
// ---------------------------------------------------------------------------

inline constexpr int kRowHeight = 22;
inline constexpr int kSheetHeaderHeight = 42;  // caption band, top rule included
inline constexpr int kGutterWidth = 60;        // row-number gutter (sheet edge 60)
inline constexpr int kSplitHandleWidth = 6;    // visible drag thumb
inline constexpr double kSplitFraction = 0.459;  // 881 / 1920, docs/parity.md
inline constexpr int kSheetSpan = 819;         // 9 columns, 60 .. 879

// Gantt timescale: weekly tier label above single-letter day cells.
inline constexpr int kGanttHeaderHeight = 42;  // == kSheetHeaderHeight
inline constexpr int kGanttTierHeight = 21;    // two 21 px tiers
inline constexpr double kPixelsPerDay = 24.0;  // measured day cell
inline constexpr double kPixelsPerWeek = kPixelsPerDay * 7.0;  // 168

// ---------------------------------------------------------------------------
// Chrome heights
// ---------------------------------------------------------------------------

inline constexpr int kRibbonTabHeight = 26;
inline constexpr int kRibbonGroupHeight = 78;
inline constexpr int kRibbonHeight = kRibbonTabHeight + kRibbonGroupHeight;
inline constexpr int kTimelineHeight = 44;
inline constexpr int kStatusHeight = 24;

// ---------------------------------------------------------------------------
// Colours (the measured reference values, docs/parity.md section 2.3)
// ---------------------------------------------------------------------------

inline const QColor kHeaderBackground(0xff, 0xff, 0xff);  // measured white
inline const QColor kHeaderText(0x1f, 0x1f, 0x1f);
inline const QColor kHeaderTextMuted(0x8a, 0x8a, 0x8a);   // Add New Column stub
inline const QColor kHeaderSeparator(0xd4, 0xd4, 0xd4);   // measured (212,212,212)
inline const QColor kGridLine(0xd4, 0xd4, 0xd4);          // measured (212,212,212)
inline const QColor kGridLineStrong(0xb0, 0xb0, 0xb0);    // month/week boundary
inline const QColor kWeekend(0xf7, 0xf7, 0xf7);           // measured (247,247,247)
inline const QColor kSelection(0xdc, 0xe9, 0xf5);
inline const QColor kRowGutter(0xff, 0xff, 0xff);
inline const QColor kRowGutterText(0x33, 0x33, 0x33);
// Not measurable from the current reference (no bars visible); kept from the
// shell palette until the coordinator re-captures an auto-scheduled fixture.
inline const QColor kBarTask(0x4a, 0x7e, 0xbb);
inline const QColor kBarSummary(0x1f, 0x38, 0x64);
inline const QColor kBarMilestone(0x1f, 0x38, 0x64);
inline const QColor kBarCritical(0xc0, 0x39, 0x2b);
inline const QColor kBarComplete(0x2f, 0x5f, 0x9a);
inline const QColor kBarOutline(0x2b, 0x4a, 0x6f);
inline const QColor kLinkLine(0x6a, 0x6a, 0x6a);
inline const QColor kBodyText(0x10, 0x10, 0x10);
inline const QColor kMutedText(0x5a, 0x5a, 0x5a);
inline const QColor kSheetBackground(0xff, 0xff, 0xff);

// Shell chrome.  The timeline strip is WHITE on purpose: the parity harness
// treats columns whose pixels are light grey (225..250) for >= 35 % of the image
// height as Gantt weekend bands, and a grey strip plus the ribbon and status bar
// would push the sheet's own columns close to that threshold.
inline const QColor kRibbonBackground(0xf7, 0xf7, 0xf7);
inline const QColor kRibbonTabActive(0xff, 0xff, 0xff);
inline const QColor kRibbonBorder(0xd0, 0xd0, 0xd0);
inline const QColor kTimelineBackground(0xff, 0xff, 0xff);
inline const QColor kStatusBackground(0xf0, 0xf0, 0xf0);

// The process's resolved UI family (filled by configure_application()).
QString& ui_font_family();
bool ui_font_is_carlito();

}  // namespace msqt
