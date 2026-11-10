# Trace of Set Baseline 0 (target T2), Project 2021 perpetual: capture done, gate PASSED with a caveat, first analysis pass (setter-call list)

_Run Tue 10 Nov 2026 in the isolated trace VM (network disconnected). Written by the TRACE agent; not scanned or committed._

| item | value |
|---|---|
| VM build line | WINPROJ.EXE 16.0.20430.20182 sha256 F1A7E52729C01C115535B1277C5553302D15F996FCE8D433DFCE11E0FD7F5F6B; ProjectPro2021Retail, 5-day pass |
| Input | `wave30-baseline-control.mpp` (hash F665EA3882FE.. matches the repo file) |
| Edit | calculation AUTOMATIC (the baseline pair was made in the default mode), user name `trace`, open, `BaselineSave(True, 0, 0)`, 25 s settle, `FileSaveAs` |
| Saved file SHA-256 (final capture) | 12B9A74F43276EE1 A0CD1565A0065239 20C73E254BF47CFE DCD0E7B3F82FCAE0 (spaces added) |
| Saved file size | 242176 bytes, 96 streams (the pair's edited file is the same size) |
| Trace | 856 MB, `trace-save3.ps1`: recording covers the edit and the save |

## Gate: traced baseline against `wave30-baseline-set.mpp`

**96 streams, 93 checked, 18 problem lines**, block SHA-256 77c23089063e4180ba1c6ce634b33681dd299238c5c10d0332dff88696e09d98 (host copy equal).

```
96 streams, 93 checked, 18 problems
bytes-differ ???114/Props 41
bytes-differ ???114/TBkndAssn/Fixed2Data 60
bytes-differ ???114/TBkndAssn/Var2Data 24
bytes-differ ???114/TBkndCal/Fixed2Data 44
bytes-differ ???114/TBkndRsc/Fixed2Data 20
bytes-differ ???114/TBkndRsc/FixedMeta 2
bytes-differ ???114/TBkndTask/Fixed2Data 74
bytes-differ ???114/TBkndTask/FixedData 27
bytes-differ ???114/TBkndTask/Var2Data 20
bytes-differ ???214/CEdl/FixedData 10
noise-whole ???214/CEdl/Var2Data 574->486
bytes-differ ???214/CEdl/VarMeta 1
bytes-differ ???214/CFilter/FixedData 20
bytes-differ ???214/CGrouping/FixedData 20
bytes-differ ???214/CTable/FixedData 10
bytes-differ ???214/CV_iew/FixedData 30
bytes-differ ???214/CV_iew/Var2Data 4
bytes-differ ???214/Props 18
size-differs ?SummaryInformation 292->284
noise-whole Props14 562->472
```

Judgement.
- No `only-in` line, no size difference except the user-name `SummaryInformation`: the 94 new var blocks, VarMeta and the Props tail equal the pair (`TBkndTask/VarMeta` and `Var2Data` have no size or byte line beyond 20 bytes of Task Var2Data, below).
- Per-save stamps: `Fixed2Data` 12-byte stamp runs at the same offsets as the null save (Assn 60, Cal 44, Rsc 20, Task 74); Task/Assn Var2Data 20 and 24 bytes are 1 to 4 byte runs repeating every ~298 bytes at offsets +696.. (Task) and +586.. (Assn): the baseline-save timestamp words inside each baseline block.
- Caveat: `TBkndTask/FixedData` 27 bytes and `TBkndRsc/FixedMeta` 2 bytes differ.  Task FixedData differs in 9 runs (stream offsets +346/6, +548/6, +566/1, +570/1, +638/6, +750/6, +768/1, +772/1, +840/6; pair value then traced value).  Three 6-byte runs (+346, +548, +750) are date words: the pair has `781e f13b 201c`, the traced file `c012 f23b d827`, one calendar day later, as after a recalculation; the single bytes at +566, +570, +768, +772 are the low bytes of the same dates (f1 to f3, f4 to f6).  The two other 6-byte runs (+638 and +840) are zero in the pair and `8025 0000 8025` and `4038 0000 4038` in the traced file: not understood (they look like duration words).  The earlier save-only capture (recording after the edit) of the same edit gave 16 problem lines with no Task FixedData line, so the recorder attached before the edit changes when Project reschedules.  Verdict: the baseline blocks (what T2 asks about) are the pair's; the schedule dates are one day off, so do not read the Task FixedData date words from this trace.

## Analysis
See the setter-call list below (added after the capture), if present.

### Setter-call list (first pass)

All field stores go through `FUN_1403dd790` (RVA 0x3dd790; method in `trace-calculate.md`).  459 calls in the final capture's recording, positions 1e9c9:184e to 20154:948.  Per table object, by key prefix:

| table | calls by row index | var keys | fixed keys |
|---|---|---|---|
| Task (0x0b4...) | row 3: 52, row 4: 55, row 5: 55, row 6: 55 (217 calls; positions 1ff44:458 to 1ffee:b56) | 27 | 34 |
| Assignment (0x0f4...) | rows 0 to 2: 26 each (78 calls; positions 20111:2fe to 20154:948) | 14 | 12 |
| unidentified (0x0c4... keys) | rows 3 and 4: 27 and 30 (57 calls; 2005b:21f to 20084:214f) | 11 | 19 |
| unidentified (0x0d4... keys) | rows 5 to 7: 4, 4 and 12 (19 calls; 200b5:2690 to 200be:315) | 6 | 6 |
| four default tables | 14 to 31 calls each at the start of the recording | 2 to 4 | 5 to 13 |

Task var keys written for every task row (these are the new var blocks of the pair; names from `analysis/field_catalog_task.tsv`): 0x0b40009f, 0182, 01df, 00a4 (ActualOvertimeWork), 00a5, 00a8, 00a9, 00aa (the five overtime fields), 00ad, 00ae, **0496 (BaselineDeliverableStart), 0497 (BaselineDeliverableFinish), 0498 (BaselineBudgetWork), 0499 (BaselineBudgetCost)**, 000e (Name), **0001 (BaselineWork), 0006 (BaselineCost), 001b (BaselineDuration), 002b (BaselineStart), 002c (BaselineFinish)**, 00b3, 01c8, 0500 (Manual), 0505 (StartText), 0506 (FinishText), **0513 (BaselineStartText), 0514 (BaselineFinishText)**.  Four task rows x 27 keys is 108 calls; the pair's 94 new blocks are those not already present.  The unnamed keys (009f, 0182, 01df, 00ad, 00ae, 00b3, 01c8) are candidates for the "26 undecoded" blocks of `var-meta.md`; this list does not say which of the 26 each is.

Task fixed keys (34): 0056, 0017 (ID), 0477 (Guid), 0000 to 000a (Work, ActualWork, RemainingWork, Cost, ActualCost, FixedCost, RemainingCost), 0015, 001d, 001f, 0020, 0021, 0023 to 0028 (Start to LateFinish), 0063, 0064, 00c9, 00f9, 0183, 01b6, 01b7, **01e0 (BaselineFixedCost), 0495 (BaselineFixedCostAccrual)**, 04e2, 04e7, 0508, 047f.

Reading by eye; the call list with positions and values is `C:\trace\out\fc-baseline.txt` in the VM (it stays there only until the VM is restored).

## Round 2 addendum (Wed 11 Nov 2026): the caveat reproduces from a recording that starts before the file is opened

`trace-save5.ps1 -Target baseline-open` (the file opened inside the recording, then `BaselineSave`, 25 s settle, save; 1172 MB, saved 242176 bytes) gives the same gate set as round 1's baseline: 96 streams, 93 checked, 18 problems, with `114/Props 41`, `TBkndTask/FixedData 27`, `TBkndTask/Var2Data 28`, `TBkndAssn/Var2Data 30`, `TBkndRsc/FixedMeta 2` and the Fixed2Data stamp lines (block text not transcribed; counts read off the screen, not hash-checked).  So the Task FixedData date words and the 2 Rsc FixedMeta bytes that differ from the pair are not an artefact of when the recorder attached: they are reproducible in this VM and remain unexplained (candidates: the baseline stamp is a time of day; the pair was made on another date and build).  Props framing follow-up: `trace-props-framing.md`.
