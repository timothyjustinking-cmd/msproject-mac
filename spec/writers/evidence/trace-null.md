# Trace of the null save (target N), Project 2021 perpetual: capture done, gate PASSED, analysis not needed

_Run Tue 10 Nov 2026 in the isolated trace VM (network disconnected). Written by the TRACE agent; not scanned or committed._

| item | value |
|---|---|
| VM build line (`build.txt`, path omitted) | WINPROJ.EXE 16.0.20430.20182 sha256 F1A7E52729C01C115535B1277C5553302D15F996FCE8D433DFCE11E0FD7F5F6B (matches the pinned hash) |
| Office product id (VM registry, ClickToRun ProductReleaseIds) | ProjectPro2021Retail, updates off, on the 5-day pass ("Activation Required", full features until Sun 15 Nov 2026) |
| Input | `wave30-calculate-control.mpp` (copy hash 8E4E81D849DF, matches the repo file) |
| Edit | none (opened, calculation manual, user name `trace`, `FileSaveAs`) |
| Saved file SHA-256 | B539328F8D1F61EF88358815BAFA14C442D643E4FAC560F146BC831D808EBEFF |
| Saved file size | 236544 bytes, 96 streams (the committed null save is also 236544 bytes, 96 streams) |
| Trace file | one `.run`, 83,886,080 bytes; stays in the VM |

## Gate: traced null save against the committed null save

Command: `trace_gate.py null.mpp wave30-calculate-control-null.mpp wave30-calculate-control.mpp wave30-calculate-control-null.mpp`.
Result: **96 streams, 93 checked, 14 problem lines** (printed block SHA-256 fd18b60c99799dd58e1bea52fc4479c57f0e8ca5d490b995f31e7d3848fbf603, re-computed on the host from the copied text: equal).

```
96 streams, 93 checked, 14 problems
bytes-differ ???114/Props 24
bytes-differ ???114/TBkndAssn/Fixed2Data 40
bytes-differ ???114/TBkndCal/Fixed2Data 10
bytes-differ ???114/TBkndCons/Fixed2Data 30
bytes-differ ???114/TBkndTask/Fixed2Data 50
bytes-differ ???214/CEdl/FixedData 10
noise-whole ???214/CEdl/Var2Data 602->500
bytes-differ ???214/CEdl/VarMeta 1
bytes-differ ???214/CFilter/FixedData 20
bytes-differ ???214/CGrouping/FixedData 20
bytes-differ ???214/CTable/FixedData 10
bytes-differ ???214/CV_iew/FixedData 30
bytes-differ ???214/CV_iew/Var2Data 4
bytes-differ ???214/Props 18
size-differs ?SummaryInformation 292->284
noise-whole Props14 530->464
```

(The `???` is the stream-name prefix characters the script masks.)

## Judgement

- No `only-in-traced` or `only-in-reference` line: the 8 `114/TBkndBoardColumn` and `114/TBkndSprint` streams of the failed subscription run are gone, and the stream count (96) and file size (236544) equal the committed null save. The edition is now the perpetual one.
- `114/Props` has no size-differs line (it was 88736 -> 89542 in the failed run).
- One `size-differs`: `SummaryInformation` 292 -> 284 (8 bytes). It is the OLE property set holding the author and last-author strings; the VM's neutral user name `trace` differs in length from the host's. Per-machine, not per-save.
- Remaining `bytes-differ` lines are in Fixed2Data and view-state streams and are counted outside the mask built from the committed control against its null save; a Fixed2Data stamp word per record and the view stream save stamps change on every save, so the committed null itself differs from the control at positions that the traced save does not repeat. `noise-whole` streams (`CEdl/Var2Data`, `Props14`) are skipped by the script because the control and its null save differ in size there (control 606 -> null 602 and 572 -> 530). The traced sizes are 500 and 464, against the committed null's 602 and 530 (the failed run gave 500 and 456). Hypothesis, not examined: these streams carry file-path and user-name strings, which differ in length on the VM (`C:\trace\work\...`, user `trace`).
- Verdict: PASS as a noise baseline (not an exact reproduction of the committed null, because of the user-name-sized SummaryInformation and per-save stamp words). The null run's own diff against the committed null is the noise to subtract from every other target.

## Questions answered

None required (the null target's purpose is the noise baseline). Rows: 0.
