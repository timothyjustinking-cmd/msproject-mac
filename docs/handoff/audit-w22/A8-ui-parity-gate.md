# A8 -- UI parity gate audit (wave 22, HEAD e252041)

Read-only audit of the claim "UI parity 9/9 against the captured reference on
Windows, macOS and Linux". Sources: scripts/parity_report.py,
tests/parity/run_selftest.py, docs/parity.md, docs/ui-qt-port.md s7,
docs/ui-parity-spec.md, app_qt/tests/parity_test.cpp, app_qt/CMakeLists.txt,
app_qt/main.cpp, app_qt/main_window.cpp, app_qt/shell_style.h,
app_qt/entry_columns.h, .github/workflows/qt.yml, the wave-22 CI artifacts
under build/ci-w22-final/ and the local reference corpus/bench/ (JSON read,
PNG fed to the harness only, never viewed). The harness was re-run locally
against the three downloaded wave-22 renders; nothing was built, nothing was
written except this file.

## Verdict

"9/9" proves that the Qt shell's task sheet and Gantt area, rendered light-themed
and selection-less from a 3-leaf-task fixture, reproduces nine structural
numbers of one MS Project screenshot -- row pitch, header band height, nine grid
column widths, splitter fraction, day-cell pitch, tier count and three flat fill
colours -- to within loose tolerances, and that CMake-level constants equal the
numbers written down from that same screenshot. It proves nothing about bars,
cell text, selection, ribbon, timeline, status bar, dark theme, font or
cross-platform pixel identity; the three "9/9" platform renders are in fact
2.4-3.2% pixel-different from each other and no code anywhere measures that.

## Q1. The nine "comparable" items

The comparison is `compare()` in scripts/parity_report.py:800-872 over the
MEASURES table at :70-91 (10 entries). A row counts as "comparable" only when
both images supply it (:816-831); the task-bar colour is N/A because the
reference has none (:821-824, docs/parity.md:113-121), leaving 9.

Both images are measured at run time by `Analysis` (:183-788) -- the reference
value is the harness's own measurement of corpus/bench/msproject-gantt.png
(sha256 04befde5..., docs/parity.md:49), NOT the numbers in docs/parity.md and
NOT an earlier Qt render. docs/parity.md s2 merely records the same measurement
(build/parity-ref.json is that run). docs/ui-parity-spec.md numbers are not
used by the harness at all.

Measured by the wave-22 run (identical on all three OSs; my re-run):

| # | key | what is measured | how (parity_report.py) | reference | candidate | tol | result |
|---|---|---|---|---|---|---|---|
| 1 | row_height_px | pitch of horizontal rules in the sheet | median-profile rule scan + longest even chain, :447-553 | 22 | 22 | +-1 | PASS |
| 2 | header_height_px | caption band height (body_top - header_top) | :543-546, refined from grid-line tops :625-650 | 42 | 43 | +-2 | PASS |
| 3 | splitter_fraction | x of table/chart divider / width | from first weekend band minus 5 days, snapped to a line, :350-425 | 0.45885 | 0.45924 | +-0.02 | PASS |
| 4 | day_width_px | weekend-band period / 7 | :292-348 | 24 | 24 | +-2 | PASS |
| 5 | timescale_tier_count | count of glyph-row clusters in the chart header band | :654-689 | 2 | 2 | exact | PASS |
| 6 | sheet_column_widths_px | 9 widths between vertical grid lines | :557-611, list compare :833-847 | 39/57/147/81/81/81/105/105/123 | 38/57/147/81/81/81/105/105/124 | +-4 each, same count | PASS (-1,+1) |
| 7 | colour.header | modal RGB inside caption band | :694-705 | (255,255,255) | (255,255,255) | 10/ch | PASS |
| 8 | colour.grid | modal RGB of detected grid-line pixels | :707-723 | (212,212,212) | (212,212,212) | 14/ch | PASS |
| 9 | colour.weekend | modal RGB of shaded-band pixels | :725-731 | (247,247,247) | (247,247,247) | 10/ch | PASS |
| - | colour.task_bar | modal RGB of saturated runs >= 2 days x 3 rows | :737-772 | not found | (74,126,187) | 24/ch | N/A |

Three things the table hides:

* All nine are geometry-of-lines or flat-fill colours. No text, no glyph, no
  icon, no bar, no selection, no chrome (parity_report.py:17-18; docs/parity.md
  s6 :214-240 lists the exclusions in the authors' own words).
* The candidate DOES draw bars (row 10: (74,126,187) found) while the reference
  shows none; the two chart areas have different content and the harness scores
  only their shading pitch and shade colour.
* The vertical position of the sheet is not compared: header_top is 253 in the
  reference and 203 in the Qt render (50 px of chrome difference), body_bottom
  977 vs 311 (Project draws empty-row rules to the window bottom; the shell
  draws 3 rows). Neither is a MEASURES entry.

## Q2. The parity fixture

tests/data/synthetic-base.mpp: a `FileNew()` project with project row "base" and
three tasks, Task A 5d, Task B 3d, Task C 2d, all starting 2026-01-05 08:00, FS
chain, no resources, no notes (tests/data/README.md:14-19). Built by the
coordinator via COM on 2026-10-22, copied from corpus/probe/base.mpp, sha256
pinned (README.md:21-24; scripts/check_fixtures.py:21-22). shell_style.h:18-19
states its tasks are manually scheduled. The same file is both the capture
fixture (scripts/msproj_bench.py:29, corpus/bench/msproject-gantt.json
"fixture"/"tasks": 3) and the render input (app_qt/CMakeLists.txt:122).

Exercised in the parity render (CMakeLists.txt:119-126, main.cpp:171-195):

* the stored table: `214/CTable` widths are read and drive the column widths
  (parity_test.cpp:175-178; main_window.cpp:1295) -- width only, not values;
* the timeline strip: present, 54 px, painted white (shell_style.h:95,148) --
  present in the pixels, measured by nothing;
* the status bar: present with "Ready" (main_window.cpp:1025) -- in the pixels,
  measured by nothing;
* bars: three leaf bars in kBarTask blue (harness finds (74,126,187)) -- N/A.

NOT exercised by the fixture: summary rows (no summary task), milestones (no
zero-duration task), critical colouring (bars render kBarTask blue, not
kBarCritical red, so no critical path is shown), % complete fill, bar labels,
links in red, resources / Resource Names cell text, notes, more than three rows,
a wide stored table, any non-Entry table, any view other than Gantt, dark
theme, a selection, any dialog, scrolling.

## Q3. Which regressions pass 9/9 unchanged

All eight pass, from the code:

(a) Bar colours wrong -- PASS. Reference has no bar, so colour.task_bar is
    N/A whatever the candidate draws (parity_report.py:821-824). Summary /
    milestone / critical colours are never sampled. qt_parity_geometry checks
    only the constant `kBarTask == #4a7ebb` (parity_test.cpp:224, labelled
    "unmeasured"), not what gantt_canvas.cpp:62-67 paints.
(b) Stored-table values wrong -- PASS. No text is read (parity_report.py:17;
    docs/parity.md:227-229). Column boundaries come from grid lines only.
    parity_test.cpp:176-178 checks three width bytes, not cell values.
(c) Selection #DCE9F5 instead of #A2D3A0 -- PASS. The parity render is taken
    with the selection cleared (main.cpp:171-175, main_window.cpp:1061-1072),
    so no selection pixel exists in the image; docs/parity.md:233-235 says
    selection tints are invisible to the comparison. parity_test.cpp:223
    compares the constant kSelection to a literal, which catches a changed
    constant but not a wrong paint path.
(d) Ribbon groups reordered -- PASS. Chrome is excluded (docs/parity.md
    :230-232); the ribbon lies above header_top, and header_top is not a
    MEASURES entry. (qt_ribbon_inventory, a separate gate, pins the order:
    ribbon_inventory_test.cpp:289-294.)
(e) Status bar text wrong -- PASS. No OCR; the status band is a flat
    (240,240,240) strip below body_bottom.
(f) Timeline strip missing -- PASS. The strip is white (shell_style.h:141-148,
    chosen precisely so the harness cannot see it); removing it shifts
    header_top from 203 to ~149, which is not compared, and leaves all nine
    numbers unchanged. parity_test.cpp:243-246 checks `timeline() != nullptr`
    and `height() == kTimelineHeight` (its comment says 44 px; the constant is
    54, shell_style.h:95), so that gate would catch a deleted widget but not a
    strip that is present and empty.
(g) Dark theme broken -- PASS. The render path never loads a theme
    (main.cpp:146-158: PreferencesStore only when render_path is empty;
    theme_ defaults to light, main_window.h:325; configure_application applies
    Theme::light, main_window.cpp:164). The parity image is always light.
(h) Font not Carlito -- PASS. The harness cannot see fonts (docs/parity.md
    :215-219). Row height and header height are fixed constants
    (sheet_views.cpp:146,350; main_window.cpp:970), so a metric change would not
    move them. Wave 19 demonstrated this: different platform font stacks
    (Calibri/Arial vs Liberation/DejaVu) scored an identical 9/9
    (docs/parity.md:273-275). parity_test.cpp:136-138 accepts
    `family_is_pinned(...) || ui_font_family() == QApplication::font().family()`;
    configure_application sets the application font's family to exactly
    ui_font_family() (main_window.cpp:200-205), so the second clause is true by
    construction and the check cannot fail.

## Q4. The "pixel-difference score" (decision 12)

Not computed anywhere. scripts/parity_report.py contains no image-vs-image
pixel comparison (its only `diff` is numpy run-length detection, :99); its
outputs are the structural MEASURES above. No other script, test or workflow
computes one (grep over scripts/, tests/, app_qt/, .github/). The ui-qt-port.md
s3 plan (:49-53) of SHA-256 cross-OS golden equality in tests/golden/ui/ is not
implemented either: no such directory, no hash compare.

docs/handoff/wave20-complete.md:89 says "The pixel-difference score exists
(parity_report.py) and is reported by the wave" -- that is incorrect;
wave21-complete.md:135 and wave22-complete.md:132 walk it back to "no
per-screen pixel diff". ui-parity-spec.md:26 still lists it as decided.

For scale: the three wave-22 CI renders differ from each other by 47,932
(Linux/Windows), 63,760 (Linux/macOS) and 64,012 (Windows/macOS) pixels of
1,981,440 (2.4-3.2%), max channel delta 239, spread over chrome, header, body
and chart. Those differences are invisible to 9/9.

## Q5. What CI compares against

Nothing. .github/workflows/qt.yml never invokes parity_report.py or
tests/parity/run_selftest.py (grep of .github/ finds neither), and corpus/ is
git-ignored (.gitignore "corpus/"), so no reference exists on a runner. CI's
parity-related gates are:

* ctest qt_render_parity: `--open synthetic-base.mpp --render ... --parity`
  passes if stdout matches "rendered 1920x1032" (CMakeLists.txt:119-126);
* ctest qt_parity_geometry: the in-process constant/widget checks of
  parity_test.cpp (Q6);
* "The 1920x1032 parity render was written": `test -s` on the PNG
  (qt.yml:209-214);
* upload of the PNGs as an artifact (qt.yml:447-455).

The "9/9 on all three" in docs/handoff/wave22-complete.md:111 was produced
off-CI: the operator downloaded the three artifacts to build/ci-w22-final/ and
ran the harness on the dev box against the local corpus/bench reference. My
re-run reproduces exactly that (9 comparable, 0 failing, per-column deltas
-1/+1, on each OS). So CI's 9/9 and local 9/9 are the same nine numbers, but
only the local run computes them; CI itself asserts only that a 1920x1032 PNG
exists. docs/parity.md:20 calling run_selftest.py "the harness's CLI contract,
for CI" describes an intent; no workflow runs it.

## Q6. qt_parity_geometry vs its own constants

Largely self-referential. The expected values live in app_qt/shell_style.h
(kRowHeight 22 :68, kSheetHeaderHeight 42 :69, kGutterWidth 60 :70,
kSplitFraction 0.459 :72, kGanttHeaderHeight 42 :76, kGanttTierHeight 21 :77,
kPixelsPerDay 24 :78, kTimelineHeight 54 :95, the colours :109-133) and the
production code reads those same symbols (sheet_views.cpp:146,350-351,423;
main_window.cpp:345,970,988). parity_test.cpp then asserts the widget equals the
symbol: :150 defaultSectionSize == kRowHeight, :163 header height ==
kSheetHeaderHeight, :158 gutter == kGutterWidth, :194 split_fraction near
kSplitFraction, :205-208 header/tier/day == kGanttHeaderHeight /
kGanttTierHeight / kPixelsPerDay, :245 timeline == kTimelineHeight. Change the
constant and the shell and the test move together; the test cannot detect a
drift from the reference, only a widget that ignores its constant.

The column widths are a partial exception: the test compares
`sheet->columnWidth(i)` to `entry_columns()[i].width` (:181-189), which is
`entry_width_px(chars)` = chars*6+3 from entry_columns.h:115-117 over the
character counts in entry_columns.cpp:22-36 -- again the implementation's own
table. Only a few literals are independent of the implementation: 9 columns
(:169-171), 147 px for 24 chars (:179), 819 content width (:190), 20 px/day at
day zoom (:262), and the colour literals (:218-224). Those literals were typed
in from docs/parity.md s2, which was measured from the reference, so they are
reference-derived; but a colour literal checks a QColor constant, not a painted
pixel.

Net: qt_parity_geometry gates that constants were not edited and that widgets
honour them. The reference-vs-render comparison exists only in the local
harness run.

## Q7. "9/9 on all three renders" and fonts

All three renders are offscreen (QT_QPA_PLATFORM=offscreen, QT_SCALE_FACTOR=1,
CMakeLists.txt:120) with Carlito compiled in as a Qt resource and registered by
QFontDatabase::addApplicationFont (main_window.cpp:166-173; the macOS build log
shows rcc for carlito_* at build/ci-w22-final/mac.log:518-565). So, when the
resource loads, all three use the same bundled font and differ only in Qt's
per-platform rasterisation -- which is what the 2.4-3.2% pixel difference above
is.

Would a platform font substitution be detected? No:

* the harness measures no glyph property (docs/parity.md:215-219), and the
  sheet/Gantt heights are fixed constants, so metrics cannot leak into the nine
  numbers; wave 19 scored 9/9 with three different font stacks
  (docs/parity.md:273-275);
* parity_test.cpp:136-138 is a tautology (Q3h), so a missing resource that fell
  through to Arial would pass qt_parity_geometry;
* no cross-OS hash or pixel comparison exists (Q4), so the renders can diverge
  without any gate noticing -- and they do.

The honest cross-platform claim is: the same constants produce the same
structural numbers on three OSs; the pixels are not identical and nothing checks
how different they are.
