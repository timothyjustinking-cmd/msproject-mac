# Committed, client-free parity references

A reference here is derived from **MS Project itself** running a committed
synthetic fixture — never from a render of this port (the audit's A-1 verifier:
our own earlier render is not a reference).

**Operator ruling (Tim, 2026-10-23): Project screenshots stay local and never
reach the repository.**  Only the NUMBERS measured from them are committed.
The coordinator's local pre-push guard (not tracked in the repo) enforces this,
and the ruling stands for any clone regardless of the hook.

| file | what |
|---|---|
| `wave23-outline-project.measures.json` | `scripts/parity_report.py`'s measurements of a 1920x1040 Project Professional 2021 (16.0.20430.20182) capture of `tests/data/wave23-outline.mpp`, taken 2026-10-23 through `scripts/oracle_lock.py` from a COPY of the fixture, window maximised.  The PNG lives under `build/w23/` (git-ignored); this JSON is the structural reference the Windows CI gate compares the 1920x1032 render against.  SHA256 of this file: `3CA8261885BD930F234149C8C65894B892F797A89FAE0DFB249FEA85271D60E0`. |
| `wave25-auto.measures.json` | the same measurements of a 1920x1032 capture of `tests/data/wave25-auto.mpp` (`wave23-outline.mpp` with every task AUTO-scheduled, Critical Tasks OFF), 2026-10-24.  Every bar is an auto task bar: `colour.task_bar` is `(138,187,237)`, so a port that calls the manual fill "task" fails this gate.  The gate compares **`--only colour.task_bar`**: this reference's claim is the colour, and its saved view geometry is not part of it (the full ten-measure structural gate stays the wave23-outline reference above).  SHA256 of this file: `054CDBFAA0048F8FAECC20FD73A431E0CB0F57F919B27585351270CA0F8E7637`. |
| `w29-projecta-timescale.measures.json` | a numbers-only record of PROJECT_A's stored timescale (`0x22400008` +1176/+1180), the month-column geometry of a local Project 2021 capture of it, and three COM controls that patch only the stored size on a neutral synthetic copy.  PROJECT_A and the capture stay git-ignored and no image is committed (rule 4).  It is the W29-1.4 reference for the port's stored scroll origin and zoom; unlike the two rows above it is not a CI parity image reference. |
| `w29-format-dialogs.measures.json` | the W29-2.1 numbers-only record of the Text Styles (408x352), Gridlines (472x188) and Bar Styles (747x536) dialogs from the local Project 2021 captures: each control's client-relative rectangle and the captured list counts.  The captures stay git-ignored (rule 4); `qt_format_dialog_gate` measures the live widget tree against this file. |
| `w29-page-setup.measures.json` | the W29-3.1 numbers-only record of the Page Setup - Gantt Chart dialog (476x362) from the local Project 2021 capture: the 22 Page-tab control rectangles, the five action buttons, the title and the tab count.  The captured tab control's six tabs are owner-drawn and read empty, so only the count is recorded, not their text or band.  The capture stays git-ignored (rule 4); `qt_page_setup_gate` measures the live widget tree against this file. |
| `w29-report-dialogs.measures.json` | the W29-4.1 numbers-only record of the Visual Reports - Create Report (625x541) and Compare Project Versions (469x251) dialogs from the local Project 2021 captures: each control's client-relative rectangle and captured caption, the two-row tab layout and item order, the ten template names and the three check-box states.  The three combos and the tab control are owner-drawn, so their unseen item strings are not recorded; the Compare Projects caption is stored as the pattern with `%1` for the opened copy's file name.  The captures stay git-ignored (rule 4); `qt_report_dialog_gate` measures the live widget tree against this file. |
| `w31-zoom.measures.json` | the W31-2.2 numbers-only record of Project 2021's View > Zoom results: Entire Project and Selected Tasks (task ID 3 selected) on `synthetic-base.mpp` and `wave23-outline.mpp`, measured by `scripts/oracle_w31_zoom.py` through `scripts/oracle_lock.py`, plus the Zoom dialog's 1-week/2-week/1-month/3-month windows from the local UI Automation run.  The four command windows, the 85 % fit rule and the bottom-tier origin floor are recorded; no capture or name is committed.  SHA256 of this file: `C660012035C1FF922D82C35A33DE224F8DA781ED4B426A86E3E38206A8D111B5`.  `qt_zoom_commands` compares the port's fit against it. |

The fixture is synthetic (`Summary A`, `Sub A1`, `Leaf A1a` ... `Resource A`);
no client data appears.  The PNG's own SHA-256 is recorded inside the JSON
(`image.sha256`) so a local capture can be identified without committing it.

Regenerate: capture the same fixture locally, run
`python scripts/parity_report.py --json <this file> <capture.png>`, sanitize the
`image.path` field, and update the hash in `scripts/check_fixtures.py` and here
in the same commit.
