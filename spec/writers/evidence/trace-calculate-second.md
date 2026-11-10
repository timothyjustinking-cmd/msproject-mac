# Trace of the second Calculate Project (target T1b), Project 2021 perpetual: capture done, gate PASSED, analysis not run

_Run Tue 10 Nov 2026 in the isolated trace VM (network disconnected). Written by the TRACE agent; not scanned or committed._

| item | value |
|---|---|
| VM build line | WINPROJ.EXE 16.0.20430.20182 sha256 F1A7E52729C01C115535B1277C5553302D15F996FCE8D433DFCE11E0FD7F5F6B; ProjectPro2021Retail, 5-day pass |
| Input | `wave34-calculate-second-control.mpp` (hash 40D856AD43.. matches the repo file) |
| Edit | calculation manual, user name `trace`, open, `CalculateProject()`, `FileSaveAs` |
| Saved file SHA-256 | 484CF96553776796 380C8F61930A601B 3DC188DC3C82A76D 40D862C546C26394 (spaces added) |
| Saved file size | 236544 bytes, 96 streams |
| Trace | 796 MB, captured with `trace-save2.ps1`: the recording starts after the edit and covers only the save (not the Calculate call) |

## Gate: traced save against `wave34-calculate-second.mpp`

`trace_gate.py calculate-second.mpp wave34-calculate-second.mpp wave34-calculate-second-control.mpp wave34-calculate-second-control-null.mpp`: **96 streams, 93 checked, 13 problem lines**, block SHA-256 a8447171b06b4dca58681a985fd6227da32b2e8a0970860fc77c1d6070008e9c (re-computed on the host from the copied text: equal).

```
96 streams, 93 checked, 13 problems
bytes-differ ???114/Props 26
bytes-differ ???114/TBkndAssn/Fixed2Data 40
bytes-differ ???114/TBkndCal/Fixed2Data 10
bytes-differ ???114/TBkndCons/Fixed2Data 30
bytes-differ ???114/TBkndTask/Fixed2Data 50
bytes-differ ???214/CEdl/FixedData 10
noise-whole ???214/CEdl/Var2Data 618->524
bytes-differ ???214/CFilter/FixedData 20
bytes-differ ???214/CGrouping/FixedData 20
bytes-differ ???214/CTable/FixedData 10
bytes-differ ???214/CV_iew/FixedData 30
bytes-differ ???214/CV_iew/Var2Data 4
bytes-differ ???214/Props 16
size-differs ?SummaryInformation 296->288
noise-whole Props14 584->488
```

Verdict: PASS.  The problem lines are the null-save noise set (stamp words, the user-name-sized `SummaryInformation`).  Task VarMeta, Var2Data and DocumentSummaryInformation equal the pair, i.e. a second Calculate with nothing pending writes nothing back, as `scheduler-save.md` says.

## Questions answered
None yet.  The trace was made with `trace-save2.ps1`, so the Calculate call itself is outside the recording; to confirm "no setter call" a recapture with `trace-save3.ps1` and the setter list of `trace-calculate.md` is needed (a 2-minute job).  Rows: 0.
