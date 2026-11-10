# Trace of Insert Summary (target T3a), Project 2021 perpetual: capture done, gate PASSED with stated reasoning, first analysis pass (setter-call list)

_Run Tue 10 Nov 2026 in the isolated trace VM (network disconnected). Written by the TRACE agent; not scanned or committed._

| item | value |
|---|---|
| VM build line | WINPROJ.EXE 16.0.20430.20182 sha256 F1A7E52729C01C115535B1277C5553302D15F996FCE8D433DFCE11E0FD7F5F6B; ProjectPro2021Retail, 5-day pass |
| Input | `wave30-insert-summary-control.mpp` (hash 500C813F831F.. matches the repo file) |
| Edit | calculation AUTOMATIC, user name `trace`; COM property reads of every task and assignment before and after the edit (the pair's `all_facts`); `SelectTaskField(2, "Name", False)`, `InsertSummaryTask()`, `FileSaveAs` |
| Saved file SHA-256 (final capture) | E2C15B6BFF79E907 FA94B9C467359A9B B2FF6A2CF5A955ED AA9DD5A5FBCD92AB (spaces added) |
| Saved file size | 239616 bytes, 96 streams (equal to the pair's) |
| Trace | 964 MB, `trace-save3.ps1`: recording covers the edit and the save |

## Gate: traced insert-summary against `wave30-insert-summary.mpp`

**96 streams, 93 checked, 15 problem lines**, block SHA-256 be56f4ec94a2e667df5c4a49695f827f681a90137ed94fa15e4d476fa4f7659b (host copy equal).

```
96 streams, 93 checked, 15 problems
bytes-differ ???114/Props 24
bytes-differ ???114/TBkndAssn/Fixed2Data 60
bytes-differ ???114/TBkndCal/Fixed2Data 30
bytes-differ ???114/TBkndRsc/Fixed2Data 20
bytes-differ ???114/TBkndTask/Fixed2Data 98
bytes-differ ???114/TBkndTask/FixedData 3
bytes-differ ???214/CEdl/FixedData 10
noise-whole ???214/CEdl/Var2Data 530->506
bytes-differ ???214/CEdl/VarMeta 1
bytes-differ ???214/CFilter/FixedData 20
bytes-differ ???214/CGrouping/FixedData 20
bytes-differ ???214/CTable/FixedData 10
bytes-differ ???214/CV_iew/FixedData 30
bytes-differ ???214/CV_iew/Var2Data 4
bytes-differ ???214/Props 19
size-differs ?SummaryInformation 292->284
noise-whole Props14 574->484
```

Judgement: PASS.  Two earlier attempts failed for reasons now fixed in the capture script and worth recording (the first two gate blocks, "19 problems", had `TBkndTask/Var2Data` 664 -> 652 and `VarMeta` 648 -> 636, i.e. the new row's `0x0b4004e4` var entry missing):
1. the calculation mode must be automatic (the app-level mode sticks from the earlier manual-mode captures);
2. the pair read every task's properties through COM before saving, which allocates var blocks.
The remaining differences are stamps: `Fixed2Data` 12-byte per-record stamps at the null-save offsets (Assn 60, Cal 30, Rsc 20, Task 98: rows added by the insert carry their own stamp) and 3 bytes at `TBkndTask/FixedData` +984 (`922218` -> `d23227`), a creation-time stamp of the new row.

## Analysis (first pass: field-setter calls)

All field stores go through the setter `FUN_1403dd790` (RVA 0x3dd790), watched by execute-breakpoint on its entry (method in `trace-calculate.md`); 349 calls in the recording, in positions 8c276:1199 to 94f53:436.  Per table object (identified by field-key prefix):

| table (key prefix) | calls by row index | notes |
|---|---|---|
| Task (0x0b4...) | row 3: 40, row 4: 21, row 5: 22, row 6: 21, **row 7: 121** (positions 93c93:ba3 to 94394:c0e) | rows 3 to 6 are the four existing rows (uid 0 to 3 in order); **row 7 is the inserted summary task**: 121 calls, 69 distinct var keys and 57 distinct fixed keys |
| Assignment (0x0f4...) | rows 0 to 2: 7 each, row 3: 1 | rows 0 to 2 are the three existing assignment rows; the single call on row 3 is unexplained (in the milestone trace row 3 gets 58 calls) |
| unidentified table (0x0c4... keys) | row 4: 7 | keys 0x0c400051, 0x0c400019, 0x0c4002e0 |
| unidentified table (0x0d4... keys) | rows 5 and 6: 3 and 4 | key 0x0d40000b |
| four default tables | 14 to 31 calls each at positions 8c276 to 8c7d7 | setup at the start of the recording, same four tables and keys as in the baseline trace: not part of the insert |

The inserted row's fields (read off the descriptor `d3` dword; names from `analysis/field_catalog_task.tsv`, "-" = no name there):
- fixed keys (57): 0x0b400056, 0017 (ID), 0477 (Guid), 0000 (Work), 0002, 0004, 0005, 0007, 0008, 000a, 0015 (FreeSlack), 001d (Duration), 001f (RemainingDuration), 0020 (PercentComplete), 0021, 0024 (Finish), 0025 (EarlyStart), 0026, 0027, 0028, 0063 (Resume), 0064 (Stop), 00c9, 00f9, 0183, 01b6 (StartSlack), 01b7 (FinishSlack), 04e2 (RecalcFlags), 04e7, 0508, 009f, 0182, 047f, 00a0, 0479, 0011, 0012, 0014, 0019, 001c, 0023 (Start), 0029, 002a, 005d, 0080, 00ab, 00ac, 00b2, 00b5, 00c8, 0191, 01b5, 01e0, 0495, 0503, 0504, 0509;
- var keys (69, the blocks the pair's `Task/VarMeta` grows by): 009f, 0182, 01df, 00a4 to 00aa (the five overtime fields), 000e (Name), **04e4**, 0025, 0500 (Manual), 0505 and 0506 (StartText, FinishText), 0062 (LinkedFields), 00f7, 0013 (Critical), 0018 (Milestone), 0022 (FixedDuration), 0047 (Marked), 0052 (Rollup), 005c (Summary), 006d (HideBar), 0081 (Recurring), 0083, 0084 (EffortDriven), 0085, 009e, 00c2, 00c7, 00cc, 00d2 to 00d4, 00de, 00df, 00e7, 00e8 (ExternalTask), 00f6, 00fc, 00fd, 0178 to 017e, 0184, 018f (IgnoreResourceCalendar), 01b4, 01c5, 01c7, 01c8, 04ff (Active), 050d, 0534 (IgnoreWarnings), 0580 (Agile), 058b, 0590 to 0592, 0597, 0598, 059c, 05a3, 0563, 0564, 007c.

Reading by eye from the VM screen; single digits may be wrong.  The setter list is the row-level record of what "Insert Summary" does to the table (`table-growth.md`: renumber and reparent order, placeholder rows): rows 3 to 6 get 40, 21, 22 and 21 calls (the renumber/reparent of the existing rows) and the new row gets 121.

## Not done
The order of the renumber and reparent calls (positions are in `C:\trace\out\fc-insert-summary.txt` in the VM), the 2.5 ordinal, CEdl, and the value sources.
