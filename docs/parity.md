# UI parity: measured baseline and scoring rules

Stream P1, wave 17.  The Qt shell has to look like MS Project, and "looks
different" is not an actionable bug report.  This file records the measured
geometry of the reference capture, the tolerances the harness applies, and how
to refresh the reference.  The numbers come from `scripts/parity_report.py`;
they were not estimated from a picture.

Everything here is numbers.  The reference PNG itself is a screenshot of a
third-party application: it lives in `corpus/bench/` (git-ignored) and is never
copied into a commit or into this document.  No UI string is quoted either — the
harness never reads text, so no label, column name or dialog text from MS
Project appears in the baseline.

## 1. Files and commands

| path | what it is |
|---|---|
| `scripts/parity_report.py` | the harness: measure one image, or compare two |
| `tests/parity/run_selftest.py` | the harness's CLI contract, for CI |
| `corpus/bench/msproject-gantt.png` | the reference capture (git-ignored, never committed) |
| `corpus/bench/msproject-gantt.json` | the capture's provenance (also git-ignored) |

```sh
# measure the baseline (one image)
python scripts/parity_report.py corpus/bench/msproject-gantt.png

# score a Qt render against it (two images; exit 1 if a measure is out of tolerance)
python scripts/parity_report.py corpus/bench/msproject-gantt.png build/qt-shell/window.png

# machine-readable form of either
python scripts/parity_report.py REF.png [CAND.png] --json build/parity.json

# prove the harness without the reference
python scripts/parity_report.py --selftest
python tests/parity/run_selftest.py
```

Exit codes: `0` all comparable measures pass, `1` a measure failed or nothing was
comparable, `2` usage or I/O error.  A measure that an image cannot supply is
reported as not found and is never guessed.

## 2. The reference capture

| field | value |
|---|---|
| path | `corpus/bench/msproject-gantt.png` (git-ignored) |
| size | 1920 x 1032 px client area |
| sha256 | `04befde5dafb4415bb789b25bdfb453072708b5fc0313f32ff5aecc1b612fcff` |
| background | `(255, 255, 255)` |
| capture | `scripts/msproj_bench.py` under the oracle lock, 100% display scaling |

If the hash differs from the one above, the numbers below no longer apply: see
section 5.

### 2.1 Layout

| measure | pixels | fraction | note |
|---|---|---|---|
| sheet left edge | 60 | — | first detected grid line |
| splitter (table / chart divider) | 881.0 | 0.45885 of width | 5 px divider band at 879..883 |
| sheet right edge | 879 | — | last column's right border |
| chart content starts | 884 | 0.46042 of width | first shaded column |
| header top | 253 | — | first header rule |
| header bottom (= first row top) | 295 | — | |
| header height | 42 | 0.04070 of height | includes the 2-rule top border |
| last row rule | 977 | — | the sheet rows stop here |

### 2.2 Sheet columns

Measured between vertical grid lines, left to right.  The sheet span is
`879 - 60 = 819` px, so the fractions sum to 1.  Column 0 starts at the table's
own left border.

| column | width (px) | width / sheet span |
|---|---|---|
| 0 | 39 | 0.04762 |
| 1 | 57 | 0.06960 |
| 2 | 147 | 0.17949 |
| 3 | 81 | 0.09890 |
| 4 | 81 | 0.09890 |
| 5 | 81 | 0.09890 |
| 6 | 105 | 0.12821 |
| 7 | 105 | 0.12821 |
| 8 | 123 | 0.15018 |
| total | 819 | 1.00000 |

Column boundaries: `60, 99, 156, 303, 384, 465, 546, 651, 756, 879`.

### 2.3 Rows, timescale and colours

| measure | value | fraction |
|---|---|---|
| row height (horizontal rule pitch) | 22 px | 0.02132 of height |
| Gantt day-cell width | 24 px | 0.01250 of width |
| timescale tiers | 2 | — |
| timescale header rules | y = 253, 257, 295 | — |

The day-cell width is derived from the weekend shading: full shaded bands are
48 px (2 days) and their ends are 168 px apart (7 days), so `168 / 7 = 24`.
Shaded bands begin at x = 884, 1027, 1195, 1363, 1531, 1699, 1867 (the first is
clipped by the chart's left edge).  The second tier is measured from the text
rows in the chart's header band, not from a rule — that header has only the top
border pair and the bottom border, so counting rules alone would undercount.

Dominant colours:

| feature | RGB | how it is sampled |
|---|---|---|
| header fill | `(255, 255, 255)` | modal colour inside the caption band |
| grid lines | `(212, 212, 212)` | modal colour of the detected grid-line pixels |
| weekend shading | `(247, 247, 247)` | modal colour of the shaded pixels only |
| task bar | **not found** | see below |

**The task bar colour is not measurable from this reference.**  The captured
chart area contains no bars at all, so there is no bar colour to sample.  The
harness reports `colour.task_bar: not found`, and the comparison marks that
measure `N/A` because the reference has nothing to match.  It will start working
as soon as a reference capture with a visible bar exists; the bar detector
requires a filled saturated run at least two day-cells wide and three rows tall,
so an isolated status line or text antialiasing does not qualify.

## 3. Comparison rules and tolerances

The reference is 1920x1032 and the Qt render is 900x560.  The two are **not**
the same picture at different scales: the shell is a smaller viewport at the
same pixel density.  Measures are therefore compared in the units that mean
the same thing in both:

* pixel metrics — row height, header height, column widths, day width — are
  compared **in absolute pixels**, because MS Project's row pitch and column
  widths are pixel quantities at 100% DPI, not percentages of the window;
* layout proportions — the splitter position — are compared as a **fraction of
  the image width**;
* colours are compared as the **largest per-channel difference**.

A measure is only scored when both images supply it.  Column widths are only
comparable when both images have the same number of columns; a different column
count is `N/A` with the two counts stated, not a failure.  (The shell is
expected to change its column set during wave 17, so this will switch from
`N/A` to scored the moment the sets match.)

| measure | unit | tolerance | why this number |
|---|---|---|---|
| row height | px | ±1 | it is an integer pitch; 1 px is rounding, 2 px is a different design |
| header height | px | ±2 | two-line caption band, font metrics round by up to 2 px |
| splitter | fraction of width | ±0.02 | 38 px at 1920, 18 px at 900; covers the 5 px divider band and grip width without excusing a different table/chart split |
| day-cell width | px | ±2 | the timescale must be at the same zoom; date arithmetic can round by a px |
| timescale tiers | count | exact | an integer; a missing tier is structural, not a rounding |
| column widths | px | ±4, same count required | integer column widths; 4 px covers rendering rounding but not a different column |
| header colour | RGB | max channel ±10 | flat fill, exact palette values are achievable |
| grid colour | RGB | max channel ±14 | 1 px lines are antialiased, so a few levels drift |
| weekend colour | RGB | max channel ±10 | flat fill |
| task bar colour | RGB | max channel ±24 | bars are gradients/antialiased, so a wider band is honest |

Verdicts are `PASS`, `FAIL`, `N/A` (nothing comparable) and a failure with
"candidate does not show this feature" when the reference has a measure the
candidate lacks.  Overall verdict is `PASS` only when at least one measure was
comparable and none failed; nothing comparable is a failure, because the
harness cannot certify anything then.

## 4. Current Qt gap (informational, not a baseline)

The only Qt render on disk at the time of writing is
`build/qt-shell/window.png` (900x560, git-ignored).  It is an early wave-17
artifact, not the current shell (stream Q2 is still changing the layout), but it
is a useful example of the harness finding a real gap.  It scores:

| measure | reference | this render | verdict |
|---|---|---|---|
| row height | 22 px | 20 px | FAIL |
| header height | 42 px | 32 px | FAIL |
| splitter | 0.45885 | 0.72444 | FAIL |
| day-cell width | 24 px | 20 px | FAIL |
| timescale tiers | 2 | 2 | PASS |
| column widths | 9 columns | 6 columns | N/A (different sets) |
| header colour | (255,255,255) | (248,248,248) | PASS |
| grid colour | (212,212,212) | (200,200,200) | PASS |
| weekend colour | (247,247,247) | (239,239,239) | PASS |
| task bar colour | not found | (74,125,189) | N/A |

Four fails and no column comparison: the gap is real, not cosmetic.  Re-run the
compare command after the next Qt build; do not treat these numbers as a target
in themselves — the target is section 2.

## 5. How to update the reference

Only the coordinator can drive MS Project (`docs/coordination.md`, Revision 3:
COM is coordinator-only and the oracle lock is serialised).  The capture script
is `scripts/msproj_bench.py`; run it with no arguments to the application, let
it close the project and release the lock, then re-measure:

```sh
python scripts/msproj_bench.py
python scripts/parity_report.py corpus/bench/msproject-gantt.png --json build/parity-ref.json
```

Rules for a usable capture:

1. The same client size (1920 x 1032) and 100% display scaling.  A different DPI
   changes every pixel metric here.
2. The same view, table and timescale zoom as the current reference; a capture
   at another zoom invalidates the day-cell width and the column widths.
3. The window maximised and fully painted before the grab (the script sleeps
   before capturing).
4. Replace the numbers in section 2 and the sha256 in section 1 with the
   measured ones.  If a number changes, say why in the commit message.

`corpus/` is git-ignored and the PNG must stay there.  If the reference's hash
changes for any reason other than a deliberate re-capture (a different machine,
a Project update, a font change), stop and treat the baseline as suspect rather
than editing the tolerances to make it pass.

## 6. What the harness cannot measure

* **Font family and size**: only their consequences (header height, row pitch)
  are visible as numbers.  Two different fonts can produce the same band
  height.  Pinning the font file and point size remains a separate task
  (`docs/ui-qt-port.md` section 2.2).
* **Antialiasing and subpixel rendering**: both captures use subpixel
  antialiasing, so text carries colour fringes that differ between the two
  renderers.  That noise is why the measures are structural and why colours are
  sampled from flat regions only.  Stroke weight, hinting and glyph spacing are
  not measured.
* **Icon artwork**: the small glyphs in cells and the header sort arrows are
  never compared; only the grid lines around them are.
* **Text content**: nothing is OCR'd, and no UI string is stored.  Row content
  (names, dates, durations) is out of scope — it is covered by the behavioural
  test suite, not by pixels.
* **Chrome outside the client area**: window frame, title bar, menus, ribbon,
  dialogs, scrollbars and the status bar are excluded or ignored.  Only the
  sheet + chart view is measured.
* **Exact gradients, hover/selection tints and focus rectangles**: the palette
  is sampled from flat regions only, so a gradient or a selected row's tint is
  invisible to the comparison.
* **DPI scaling and Qt style drift**: the harness reads pixels; it cannot tell
  whether a difference came from `QT_SCALE_FACTOR`, the Fusion version or a
  stylesheet.  Pin those separately.
* **Behaviour**: clicks, edits, undo, dialogs.  This is a look-alike harness,
  not a functional one.

## 7. Verification

```sh
python scripts/parity_report.py --selftest      # synthetic images, +-1 px asserts
python tests/parity/run_selftest.py             # the same plus the CLI contract
python scripts/parity_report.py corpus/bench/msproject-gantt.png
```

The self-check draws two fake sheets with known geometry (640x400 with a 20 px
pitch and a 16 px day; 900x560 with a 24 px pitch and a 20 px day), measures
them and asserts the column widths, row height, header height, splitter
fraction, day width and tier count to ±1 px (fractions to 0.002) and the four
colours exactly (largest channel difference ≤ 1).  It also proves the blank and
single-colour cases report "not found" instead of failing, and that compare mode
returns 0 for identical images and 1 for a real difference.

## 8. Wave 19: the same score on all three platforms

The Qt shell now builds and renders on Linux, Windows and macOS (`.github/workflows/qt.yml`,
pinned Qt 6.5.3).  Each job uploads its 1920x1032 `--parity` render; each was scored
against the same git-ignored reference with the command from section 1:

| platform | run | comparable | failing | row | header | splitter | day | tiers | columns |
|---|---|---|---|---|---|---|---|---|---|
| Linux (ubuntu-latest) | 39888287026 | 9 | 0 | 22 | 43 | 0.45924 | 24 | 2 | 9, deltas -1/+1 |
| Windows (windows-2022) | 39888287026 | 9 | 0 | 22 | 43 | 0.45925 | 24 | 2 | 9, deltas -1/+1 |
| macOS (macos-14, arm64) | 39888287026 | 9 | 0 | 22 | 43 | 0.45924 | 24 | 2 | 9, deltas -1/+1 |

All three report the reference-measured geometry: header `(255,255,255)`, grid
`(212,212,212)`, weekend `(247,247,247)` exact; the per-column deltas are the
documented gutter-border (-1) and splitter-line (+1) conventions, not a different
column set.  The platform font stacks differ (Calibri/Arial on Windows/macOS,
Liberation/DejaVu on Linux) and the measured geometry is still identical, which is
the point of pinning the point sizes and integer row/column metrics.

`task bar colour` stays `N/A`: the reference capture has no bars (section 2.3).
The three jobs' artifacts are `qt-window-<OS>-<sha>` containing
`qt-render-smoke.png` and `qt-render-document.png` (900x560),
`qt-shell-window.png` (900x560) and `test-out/qt-render-parity.png` (1920x1032).

Behaviour is now covered by six offscreen tests on every platform
(`qt_render_smoke`, `qt_render_document`, `qt_render_parity`, `qt_shell_offscreen`,
`qt_parity_geometry`, `qt_save_reopen`): 6/6 passed on all three in that run.
`qt_save_reopen` edits Duration through the shell's own model and undoes/redoes
it through the menu actions; a Start edit is refused (W32-0.2: the field is
read-only because a Start save is not measured against Project); it saves a
queued Text1 custom-field edit through `app::Document` to a build-local file,
reopens it and asserts the edit persisted.
