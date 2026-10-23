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
// open substitute and is BUNDLED (operator decision 13): app_qt/fonts/ carries
// the four OFL faces, compiled into every executable, so kFontFamilies' first
// entry is always available.  The fallbacks below it remain for safety.  See
// kFontFamilies, configure_application() and ui_font_family().
#pragma once

#include <QColor>
#include <QString>

namespace msqt {

// The window the render tests pin (900x560, the size the skeleton proved).
inline constexpr int kWindowWidth = 900;
inline constexpr int kWindowHeight = 560;

// The reference capture's client size (docs/parity.md section 2).  `--render
// --parity` renders at exactly this size so P1's harness can compare the sheet
// columns: at 1920 px all nine Entry columns fit, at 900 only the first four do
// and the harness reports the widths N/A.
inline constexpr int kParityWidth = 1920;
inline constexpr int kParityHeight = 1032;

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
inline constexpr int kGutterWidth = 60;        // frozen row-number gutter (sheet edge 60)
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

// Project 2021's measured chrome metrics (operator decision 4, wave 20):
//   tab row ~30 px, ribbon content ~95 px, a 32x32 icon over a two-line label,
//   group caption at the bottom, a 24 px quick-access strip before the title.
inline constexpr int kRibbonTabHeight = 30;
inline constexpr int kRibbonGroupHeight = 95;
inline constexpr int kRibbonHeight = kRibbonTabHeight + kRibbonGroupHeight;
inline constexpr int kRibbonButtonHeight = 64;
inline constexpr int kRibbonIconPx = 32;
inline constexpr int kRibbonGroupLabelHeight = 14;
inline constexpr int kQuickAccessHeight = 24;
inline constexpr int kTimelineHeight = 54;
inline constexpr int kStatusHeight = 24;
// The information callout under the ribbon ("New tasks are created in Manually
// Scheduled mode"), drawn only for a new/empty project.
inline constexpr int kInfoCalloutHeight = 24;
// The rotated view caption inside the chart pane, and the vertical TIMELINE
// label at the left of the timeline strip.
inline constexpr int kViewLabelWidth = 22;
inline constexpr int kTimelineLabelWidth = 22;

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
// WAVE 21 (operator decision 8, measured from Project 2021): the selected row and
// cell are GREEN (162,211,160), read from the clean Resource capture the operator
// supplied (corpus/reference/2026-10-23-msproject/).  The previous #dce9f5 was a
// guess; the selection colour is now measured.
inline const QColor kSelection(0xa2, 0xd3, 0xa0);
inline const QColor kRowGutter(0xff, 0xff, 0xff);
inline const QColor kRowGutterText(0x33, 0x33, 0x33);
// Bar colours.  WAVE 25 (W25-2/S2) CORRECTS wave 24: (116,204,212) is
// Project's MANUALLY SCHEDULED task fill, not the ordinary task fill, and
// (138,187,237) is the ordinary AUTO task fill, not a critical fill.  Wave 24
// sampled the first from an all-manual fixture and the second from a blank
// project whose every task was auto and critical, with Critical Tasks
// highlighting OFF -- so it measured two scheduling modes, not task-vs-critical.
//
// MEASURED on Project Professional 2021 (16.0.20430.20182), 2026-10-24, on a
// synthetic plan with an unambiguous row of each type (auto task with slack,
// auto critical task, manual task, auto/manual summary, auto/manual milestone),
// captured locally with Format > Critical Tasks OFF and again ON:
//   auto task fill        (138,187,237)   auto link        (38,124,211)
//   manual task fill      (116,204,212)   manual link      (27,173,186)
//   auto summary line     (84,84,84)      manual summary   (43,163,173)
//   milestone diamond     black, both modes
//   Critical Tasks ON: auto task (247,138,136), auto link (221,37,34);
//   a summary keeps its own colour and a manual task/link does not change.
// Critical highlighting is Project's default OFF and the port has no working
// toggle yet, so ordinary painting uses the normal colours; kBarCritical and
// kLinkCritical are the measured highlight-ON colours, reserved for the toggle.
// The FILE's own bar-style colour records are still not decoded; a file that
// overrides them renders with these measured stock colours (backlog W24-18).
inline const QColor kBarTask(0x8a, 0xbb, 0xed);        // (138,187,237) auto
inline const QColor kBarManual(0x74, 0xcc, 0xd4);      // (116,204,212) manual
inline const QColor kBarCritical(0xf7, 0x8a, 0x88);    // (247,138,136) highlight ON
inline const QColor kBarSummary(0x54, 0x54, 0x54);     // (84,84,84) auto summary
inline const QColor kBarSummaryManual(0x2b, 0xa3, 0xad);  // (43,163,173) manual summary
inline const QColor kBarMilestone(0x00, 0x00, 0x00);
inline const QColor kBarComplete(0x2f, 0x5f, 0x9a);
inline const QColor kBarOutline(0x2b, 0x4a, 0x6f);
inline const QColor kLinkAuto(0x26, 0x7c, 0xd3);       // (38,124,211)
inline const QColor kLinkManual(0x1b, 0xad, 0xba);     // (27,173,186)
inline const QColor kLinkCritical(0xdd, 0x25, 0x22);   // (221,37,34) highlight ON
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
