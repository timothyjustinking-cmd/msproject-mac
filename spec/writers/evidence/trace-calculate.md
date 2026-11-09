# Trace of Calculate Project (target T1): capture done, gate FAILED, analysis not run

_Run Mon 9 Nov 2026 in the isolated trace VM (network disconnected). Written by the trace
session; scanned and committed by the coordinator on Mon 9 Nov 2026.  Read with
[`trace-null.md`](trace-null.md), whose build, edition and cause notes apply here too._

| item | value |
|---|---|
| VM build line (`build.txt`, path omitted) | WINPROJ.EXE 16.0.20430.20182 sha256 F1A7E52729C01C115535B1277C5553302D15F996FCE8D433DFCE11E0FD7F5F6B |
| Office product id (VM registry) | ProjectProRetail (subscription client), updates off |
| Input | `wave30-calculate-control.mpp` (copy hash 8E4E81D849DF, matches the repo file) |
| Edit | `CalculateProject()` with calculation manual, as `tools/trace/trace-save.ps1` repeats it |
| Saved file SHA-256 | 8858009BCB0A9D50BDB97043AEB3411F3A44F531556D5005C06BA60593980664 |
| Saved file size | 239616 bytes, 104 streams |
| Trace file | one `.run`, 444,596,224 bytes; stays in the VM and was rolled back with it |

## Gate: traced calculate save against `wave30-calculate.mpp` (237056 bytes, 96 streams)

The noise mask is the bytes where the committed control and its committed null differ.
Result: **104 streams, 94 checked, 27 problems**.

| kind | streams | detail |
|---|---|---|
| streams only in the traced save (8) | `114/TBkndBoardColumn/*` (4), `114/TBkndSprint/*` (4) | as in the null trace |
| size differs (2) | `SummaryInformation` 292 -> 284; `114/Props` 88736 -> 89542 | as in the null trace |
| bytes differ outside the noise mask (17) | `114/TBkndAssn/{Fixed2Data 40, Var2Data 31, VarMeta 3}`, `114/TBkndCal/{Fixed2Data 24, FixedData 4}`, `114/TBkndCons/Fixed2Data 30`, `114/TBkndTask/{Fixed2Data 50, Var2Data 157, VarMeta 40}`, `214/CEdl/{FixedData 10, VarMeta 1}`, `214/CFilter/FixedData 20`, `214/CGrouping/FixedData 20`, `214/CTable/FixedData 10`, `214/CV_iew/{FixedData 30, Var2Data 4}`, `214/Props 18` | byte counts |
| whole-stream noise, sizes differ | `214/CEdl/Var2Data` 590 -> 510; `Props14` 556 -> 466 | not compared |

Not differing outside the mask: `114/TBkndTask/FixedData` and `114/TBkndAssn/FixedData`, the
streams that hold the slack, late-date, project-finish and assignment-date words.  This is a
byte comparison, not an explanation of the words.

Beside the null gate, the Calculate-only differences are `114/TBkndTask/Var2Data` (157 bytes)
and `114/TBkndTask/VarMeta` (40 bytes): the null gate shows no difference in either stream.

## Supplementary: does the edit's delta agree?

For each stream, the bytes that change from the null save to the calculate save were compared
between the committed pair (`wave30-calculate-control-null.mpp` to `wave30-calculate.mpp`) and
the traced saves.  **78 streams have an identical delta; 26 do not** (the 8 new streams, 4
streams whose size differs, and 14 same-size streams, the main ones below).  This is a diagnostic only.

| stream | committed changed bytes | traced changed bytes | note |
|---|---|---|---|
| `114/TBkndTask/Var2Data` | 0 in the shared 264 bytes (the 108 appended bytes are not compared) | 127 | the traced calculate save rewrites bytes the traced null save leaves alone |
| `114/TBkndTask/VarMeta` | 68 | 62 | 8 committed-only and 2 traced-only offsets; 17 shared offsets differ in value |
| `114/TBkndTask/Fixed2Data` | 33 | 34 | 4 and 5 offsets on one side only |
| `114/TBkndTask/FixedMeta` | 8 | 9 | 1 traced-only offset |
| `114/TBkndAssn/Fixed2Data`, `TBkndCal/Fixed2Data`, `TBkndCons/Fixed2Data` | 16, 4, 12 | 20, 10, 15 | stamp words |
| `214/*` view tables, `214/Props` | 4 to 12 | 5 to 15 | stamp words |

## Questions answered

None.  The gate failed, so no WinDbg analysis was run on this trace.  Rows: 0 explained, and
the changed words listed in [`calculate.md`](../calculate.md) sections 3 and 4 remain open.
