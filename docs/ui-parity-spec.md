# UI parity spec — agreed decisions (wave 20)

Target: the Qt shell must match **Microsoft Project Professional 2021** (Version
2609, Current Channel) as installed on this machine.  "Match" means the criteria
in `docs/ui-qt-port.md` §7: geometry and typography, structure, behaviour; icons
and identity are ours by policy.

This file is the record of a structured interview with the operator on
2026-10-23.  Where a later decision contradicts an earlier plan, this file wins.

## Decisions

| # | Surface | Decision |
|---|---|---|
| 1 | Ribbon | **All seven tabs** populated in the reference order: Task, Resource, Report, Project, View, Help, and the contextual Gantt Chart Format. Same groups, labels and order. |
| 2 | File | **Full backstage shell**: left nav Home, New, Open, Info, Save, Save As, Print, Share, Export, Close, with Options pinned at the bottom. Working pages are real; Info/Account/Share carry honest local content — no Microsoft identity, no OneDrive, no fake cloud UI. |
| 3 | Icons | **Original monochrome icon set** for every ribbon command and status-bar button, Office-like weight, our artwork. No Microsoft bitmaps. Reuse an icon where Project reuses one. |
| 4 | Metrics | Copy Project 2021's measured chrome metrics (tab row ≈30 px, ribbon content ≈95 px, 32×32 icon over a two-line label, group label at the bottom, fixed group separators, QAT 24 px, Tell-me box at the tab row's right, contextual tab only in the Gantt view). Measure from a 1920×1032 capture; do not guess. |
| 5 | Timeline | **Full strip**: vertical TIMELINE label, project Start/Finish (today for a new project), centred prompt when empty, and a canvas that draws tasks added to the timeline. Dragging bars comes later. |
| 6 | Table | **Real Entry table**: all eight columns incl. Add New Column, header menu arrows (insert/hide/sort act; the rest are honest stubs), row gutter with IDs, first empty row with its Task Mode cell and (i). |
| 7 | Loaded files | **Render what the file says**: columns, order, titles and widths from the stored table (`214/CTable`), Entry defaults only for a new/empty project; plus summary-row shading, critical red, milestone diamonds with date labels, bar labels, % complete fill, highlight row. |
| 8 | Status bar | "Ready" left; pin + "New Tasks : <mode>" centre; four icon view buttons that switch views; zoom −/+ slider far right. |
| 9 | Unimplemented commands | **Disabled** (greyed) with tooltip "not implemented in this port", and listed in a **generated** backlog `docs/ui-command-backlog.md` (tab, group, label, why missing). |
| 10 | Themes | Match **both** Office themes, light and dark: ribbon/tab/window greys, Project accent, selection, grid, weekend shading, summary/critical/milestone/bar palette, highlight row. Light is the parity reference; a preference (Options ▸ General) chooses the theme. |
| 11 | New project | Today's date; **Manually Scheduled** default; the callout follows the document's mode; Entry headers present; timeline Start/Finish = today. |
| 12 | Measurement | **Structural gates** per screen at 1920×1032 and 900×560, light and dark: element presence, position, size, colour, count. A pixel-difference score is reported but does not gate (original icons, metric font). Human visual review is added once we are close. |
| 13 | Font | **Carlito bundled on all three platforms** (metrically identical to Calibri, OFL); the shell reports the real family name. |
| 14 | Dialogs | **Every dialog on every tab exists**, including Report/Visual Reports/Compare Projects/WBS/Custom Fields, in addition to the Gantt-view set (Task Information all six tabs, Project Information, Change Working Time + Details, Options all 13 pages, Timescale, Text Styles, Gridlines, Bar Styles, Layout, Insert/Column Settings, Sort, Filter, Group By, Assign Resources, Leveling Options, Page Setup). |
| 15 | Dialog semantics | Where the engine exists, the dialog is **fully functional**. Where it does not (e.g. Compare Projects, Visual Reports), the dialog is **laid out with its primary action disabled** and an inline note, Cancel works, and the gap is in the backlog. No fake success. |
| 16 | Split view | View ▸ **Timeline** toggles the strip; View ▸ **Details** docks a task form at the bottom, synchronised with the selection, resizable splitter. |

**Audit note (wave 23, decision 12).**  The pixel-difference score wave 20
claimed did not exist (A-1).  It is implemented in `scripts/parity_report.py
--score`: per-region changed-pixel percentages (title, ribbon, timeline, sheet,
chart, status) plus a total, reported against a reference and never gating.
Decision 12's text above is the operator's and is unchanged.

## Build order

1. Empty state + chrome structure: ribbon (geometry and icons), timeline, table
   headers, status bar, QAT/title/Tell-me, light and dark.
2. Backstage.
3. Loaded-file table and formatting.
4. Dialogs: functional set first, then laid-out-only, then the backlog.
5. Split-view Details pane; final backlog pass.

## Gates that would have failed the 1984 screenshot

- Timescale origin is today/project start, never the epoch.
- Table header row exists with the expected titles and x positions.
- Timeline Start/Finish block exists at its measured y/height.
- Four status view buttons and a zoom slider exist.
- Ribbon group rectangles within ±2 px; no clipped label.
- Backstage left rail with all eleven entries.
- Both themes render (palette swap asserted, not eyeballed).

Existing tests stay green: `qt_render_smoke`, `qt_render_document`,
`qt_render_parity`, `qt_shell_offscreen`, `qt_parity_geometry`, `qt_save_reopen`.

## Deliberate limits, stated rather than discovered

- Icons and artwork are ours; no Microsoft bitmaps or fonts.
- The window frame, native menu bar placement and file dialogs are the OS's.
- Identity (title, About, bundle name) says this is a personal port, not
  Microsoft Project.
- The reference screenshots and any client project shown in them are local-only
  and are never committed or quoted.

## Still unspecified — to settle before the dialog work

1. Dark theme palette: copy Office's dark values exactly, or derive ours?
2. QAT: static Save/Undo/Redo, or the Customize QAT dialog (decision 14 implies
   the dialog exists; what does it change?).
3. Print: Page Setup dialog and Print preview in the backstage — in scope now?
4. Editing behaviour: edit triggers (typing/F2), Enter/Tab navigation, undo
   granularity, drag-fill.
5. Zoom slider mapping and steps; the percentage readout, if any.
6. Keyboard accelerators (Ctrl+… on Windows/Linux, Cmd on macOS, per
   `docs/ui-qt-port.md` §2.4).
7. Delivery: which milestone gets the next runnable Windows zip — proposed after
   build-order step 2 (chrome and backstage structurally right).
