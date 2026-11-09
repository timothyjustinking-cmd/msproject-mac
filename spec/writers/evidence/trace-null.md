# Trace of the null save (target N): capture done, gate FAILED, analysis not run

_Run Mon 9 Nov 2026 in the isolated trace VM (network disconnected). Written by the trace
session; scanned and committed by the coordinator on Mon 9 Nov 2026._

| item | value |
|---|---|
| VM build line (`build.txt`, path omitted) | WINPROJ.EXE 16.0.20430.20182 sha256 F1A7E52729C01C115535B1277C5553302D15F996FCE8D433DFCE11E0FD7F5F6B |
| Office product id (VM registry) | ProjectProRetail (subscription client), updates off |
| Input | `wave30-calculate-control.mpp` (copy hash 8E4E81D849DF, matches the repo file) |
| Edit | none (opened, calculation manual, user name `trace`, `FileSaveAs`) |
| Saved file SHA-256 | 5C38BE8C9FCC86D5F669C0DA2E1AEB8D2B4C002A36E21732C5D530FBF8C247A2 |
| Saved file size | 239104 bytes, 104 streams |
| Trace file | one `.run`, 742,391,808 bytes; stays in the VM and was rolled back with it |

## Gate: traced null save against the committed null save

The committed `wave30-calculate-control-null.mpp` (236544 bytes, 96 streams) is the reference.
The noise mask is the bytes where the committed control and its committed null differ; a
stream whose size the null save changes is skipped as whole-stream noise (`214/CEdl/Var2Data`,
`Props14`).  Result: **104 streams, 94 checked, 26 problems**.  The traced file is not the
committed null save, so the null noise measured here is the VM build's, not the pairs'.

| kind | streams | detail |
|---|---|---|
| streams only in the traced save (8) | `114/TBkndBoardColumn/{FixedData,FixedMeta,Var2Data,VarMeta}`, `114/TBkndSprint/{FixedData,FixedMeta,Var2Data,VarMeta}` | sizes 96/56/96/84 and 76/36/46/48; the committed saves have no board or sprint tables |
| size differs (2) | `SummaryInformation` 292 -> 284; `114/Props` 88736 -> 89542 | `Props` grows 806 bytes |
| bytes differ outside the noise mask (16) | `114/TBkndAssn/{Fixed2Data 40, Var2Data 31, VarMeta 3}`, `114/TBkndCal/{Fixed2Data 24, FixedData 4}`, `114/TBkndCons/Fixed2Data 30`, `114/TBkndTask/{Fixed2Data 54, FixedMeta 1}`, `214/CEdl/{FixedData 10, VarMeta 1}`, `214/CFilter/FixedData 20`, `214/CGrouping/FixedData 20`, `214/CTable/FixedData 10`, `214/CV_iew/{FixedData 30, Var2Data 4}`, `214/Props 18` | byte counts; many are the per-save stamp words, which sit at different values and positions in the two files |
| whole-stream noise, sizes differ | `214/CEdl/Var2Data` 602 -> 500; `Props14` 530 -> 456 | not compared |

## Questions answered

None.  The gate failed, so no WinDbg analysis was run on this trace (brief: do not analyse a
save that answers the wrong question).  Rows: 0.

## Likely cause (a hypothesis, not tested)

The pairs were made on the host with Project Professional 2021 (`tests/data/README.md`,
16.0.20430.20182).  The VM runs the same WINPROJ.EXE image (same SHA-256, same section table)
but is licensed as the subscription product `ProjectProRetail`.  The extra board and sprint
tables and the larger `Props` suggest the subscription edition writes agile and other feature
records that the perpetual edition does not.  Falsifying test: install `ProjectPro2021Retail`
in the VM (runbook section 3 names it) and repeat this capture; the gate should then pass.
