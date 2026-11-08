# spec/ - what the port knows about the format and the product

Specifications are written before the code that implements them.  This file
indexes them; start with the one that matches the area you are touching.

| file | covers |
|---|---|
| `mpp-container.md` | the CFB container, streams and sectors |
| `mpp-records.md` | table, row and Var2Data record layout |
| `mpp-field-keys.md` | field ids and VarMeta keys |
| `mpp-writer.md` | the save pipeline and the standing shape rules |
| `mspdi.md` | the MSPDI (XML) interchange format |
| `scheduling.md` | the scheduling engine's rules |
| `view-model.md`, `ui-inventory.md` | the view model and the UI inventory |
| `fixtures.md` | how fixtures are built, hashed and used |

## Wave 34 writer decode notes (`writers/`)

Each note states a rule, its evidence, its limits and the pairs still needed
(wave-34 brief, rule 3).  Static evidence is from the git-ignored Ghidra model;
no dynamic trace was taken (W34-0.4).  The per-pair offset reports are in
`writers/evidence/` (38 files: the 23 wave-30/31 pairs plus the wave-34
measurements, with their null saves).

| note | writer | state at the close of wave 34 |
|---|---|---|
| `writers/var-meta.md` | `core/mppwrite/varmeta_edit` - one Var2Data record per row: append, re-point, remove | built (W34-1.1); consumer `rename_resource` enabled in both shells (W34-4.1) |
| `writers/props-records.md` | `core/mppwrite/props_edit` - `114/Props` and `214/Props` record update and typed scalars | update only; insert refused (W34-2.2); WBS prefix definition enabled (W34-4.6) |
| `writers/view-stream-214.md` | `core/mppwrite/viewstream_edit` - one cell-format item | Bold only (W34-2.1); enabled for one cell (W34-4.5) |
| `writers/table-growth.md` | `core/mppwrite/assignment_growth` - assignment append and remove with re-keying | built (W34-3.1); Assign Resources enabled for the 40-hour first assignment (W34-4.3); see the wave-34 verification for the open work-amount finding |
| `writers/calendar-tbkdcal.md` | `core/mppwrite/resource_growth` - resource row, `TBkndCal` row, three Props counts | built (W34-3.2); Add Resources enabled for one work resource with default rates (W34-4.2) |
| `writers/document-summary-information.md` | `core/mppwrite/summary_info` - the summary strings and dates | built and read back by an independent reader (W34-3.3) |
| `writers/note-richtext.md` | Project's RichEdit note serialisation | rule decoded for plain ASCII; the writer is NOT built - the payload embeds the saving machine's generator string (W34-4.1); Notes stay disabled |
| `writers/scheduler-save.md` | the scheduler SAVE path (recalculate, then write back) | decode only; G5 was not started in wave 34 and moves to wave 35 |

Open rules are listed at the foot of each note under "pairs still needed".
