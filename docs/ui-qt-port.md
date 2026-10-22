# Three-platform UI — Qt shell plan (decided 2026-10-23)

**Decision.** Windows, macOS and Linux ship from **one UI**. The AppKit shell
(`app/main.mm`) is replaced by a Qt 6 Widgets shell that drives the existing
portable `app/` models and `core/`. The requirement is not "it compiles
everywhere" but **it looks and works identically** — same client-area pixels,
same command set, same print/PDF bytes.

The tables in `docs/scope.md` predate the implementation; for what is actually
done see `docs/roadmap.md` and the wave handoffs.

## 1. Why Qt and not three native shells

`core/` is already portable and CI-proven on windows-msvc, macos-appleclang and
ubuntu-gcc. The `app/` layer was kept deliberately split: layout/selection/prefs/
menu/filter/export/views/edit/fields/page-layout/start-warning are plain C++ with
headless tests that already run on Windows; only `main.mm` is AppKit
(~4,240 lines). Three native shells would triple that code and let the three UIs
drift — the opposite of the requirement. One Qt shell is ~3,500–5,000 lines and
one place to maintain.

## 2. The fidelity contract (how "identical" is defined)

1. **Style**: force `QApplication::setStyle("Fusion")`. No platform styles, no
   native widget theming. One stylesheet, one palette built in code.
2. **Fonts and metrics**: bundled fonts (record which, with licence), fixed point
   sizes, `Qt::AA_EnableHighDpiScaling` behaviour pinned and a fixed
   `QT_SCALE_FACTOR` for tests; integer pixel geometry only — no per-platform
   font fallback in the client area.
3. **Layout**: the same `app::` layout/selection models decide every geometry;
   the shell only paints. A model change that alters layout must change all three
   equally, by construction.
4. **Commands**: one action table (`app/menu_model` already is a data model), so
   the menu structure, labels, enablement and order are identical. **One
   deliberate divergence**: the accelerator modifier — `Cmd` on macOS, `Ctrl`
   elsewhere. Recorded here so it is a decision, not an accident.
5. **Dialogs**: Qt's own dialogs (`QFileDialog` with `DontUseNativeDialog`,
   `QMessageBox`, our `QDialog`s) so they look the same everywhere. The macOS
   native panel is the price.
6. **Print and PDF**: page geometry from `app::PageSpec` (already shared with
   Export PDF); PDF **bytes** come from `core/render`, so an export is
   byte-identical on all three. Printing goes through `QPrinter` to the same
   `PageSpec`; the PDF route must equal the `core/render` output.
7. **Platform chrome**: the macOS global menu bar is native (Qt does this); the
   window frame is the OS's. Client-area content is ours and identical.

## 3. How it is proved (not asserted)

- **Golden images on the matrix**: an offscreen UI test (`QT_QPA_PLATFORM=offscreen`)
  renders the main window, the Task Fields dialog, the Predecessors dialog and a
  print page to PNG at a fixed size/scale, then compares **SHA-256 across all
  three OS jobs**. Any pixel divergence fails the build. Store the goldens in
  `tests/golden/ui/` with a documented regeneration command.
- **PDF hash equality**: export a fixture from each OS job and compare hashes.
- **Behavioural parity**: the existing `app/` headless tests + the W63 self-smoke
  run on all three.
- Explicit non-goldens: the window frame, the menu bar placement, and file
  dialogs (documented above).

## 4. Migration shape

1. `app_qt/` shell target links the existing `app/` model libraries unchanged;
   `core/` untouched.
2. The shell is built and run on **Windows first** (the development box), then
   macOS/Linux CI jobs are added to the matrix.
3. `app/main.mm` stays until the Qt shell passes the golden suite on macOS, then
   is deleted in one commit — no dual maintenance window longer than a wave.
4. `app/` model APIs must not grow AppKit or Qt types; the shell is a consumer.
   Any model change needed by both shells lands in `app/` with headless tests.

## 5. Work breakdown (waves 16–17, after wave 15's save-path work)

| # | Stream | Deliverable |
|---|---|---|
| Q1 | shell skeleton + main window | QMainWindow, sheet table view delegating to the sheet model, Gantt canvas via `QPainter`, menus from `app/menu_model`, status bar |
| Q2 | dialogs + commands | Task Fields, Predecessors, prefs, filters, insert/delete, undo stack wiring — behaviour from the existing controllers |
| Q3 | print/PDF | `QPrinter` on `PageSpec`, Save-as-PDF, byte-equality with `core/render` |
| Q4 | golden-image harness | offscreen render + cross-OS hash compare + `tests/golden/ui/` |
| Q5 | packaging | Windows zip/MSI (windeployqt), macOS `.app` + notarization notes, Linux AppImage; one artifact per OS per tag |

Rough cost at the measured ~$0.077/session: **8–12 sessions ≈ $0.6–0.9** for the
shell, **3–4 ≈ $0.3** for packaging/CI. Signing is procurement: Apple Developer
($99/yr) for notarization, an EV certificate (~$200–400/yr) if Windows
SmartScreen warnings matter, nothing for Linux.

## 6. Risks

- **Font/DPI divergence** is the most likely cause of a red golden test; pin the
  font file, size and scale in the harness and treat any change as deliberate.
- **Qt version drift** changes Fusion metrics; pin the Qt version in CI.
- **Native menu bar** cannot be pixel-identical on macOS; documented exception.
- **Obsolete AppKit risk**: the old shell must not be half-migrated. Delete it
  once the Qt shell is green on macOS, not before.


## 7. Benchmark change (2026-10-23): MS Project itself is the target

The requirement is now stricter than "the three builds look the same as each
other": **all three must match MS Project as installed on this machine**. The
reference is captured directly from it:

* `corpus/bench/msproject-gantt.png` — the Gantt view with the Entry table,
  1920x1032 client area (git-ignored: it is a third-party UI screenshot, never
  commit it, and never publish it);
* `scripts/msproj_bench.py` — re-captures it under the oracle lock;
* `scripts/parity_report.py` (P1) — measures both images and scores the Qt render
  against the reference (column widths, row/header heights, splitter fraction,
  day width, tier count, colours) with explicit tolerances;
* `docs/parity.md` — the measured baseline those numbers are held to.

**What "identical" means here, in order of fidelity:**

1. **Geometry and typography** (measurable, the P1 harness scores it): column set
   and widths, row and header heights, splitter position, timescale density and
   tiers, fonts and point sizes, colours, grid and shading.
2. **Structure**: ribbon tabs/groups/buttons in MS Project's order, timeline
   strip, row gutter, Task Mode column, "Add New Column", status bar with view
   buttons and zoom slider.
3. **Behaviour**: editing, selection, scrolling, zoom, splitter drag, dialogs.
4. **Everything else**: the full view set and every dialog.

**Two limits, stated rather than discovered later:**

* **Icons and artwork are ours.** We must not copy Microsoft's icon bitmaps,
  ribbon images or font files. Structure, labels, geometry and behaviour are
  matched; glyphs are original approximations. This is the one place the UI will
  not be pixel-identical.
* **Identity is ours too.** Window title, About box and bundle name say this is a
  personal port, not MS Project.

**Consequence for the plan:** the parity score becomes a per-screen gate (sheet,
Gantt, ribbon, each dialog, each view), and `main.mm` retires only when the Qt
shell passes the scored suite on macOS — not merely when it compiles.
