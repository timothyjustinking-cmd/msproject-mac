# Trace of Insert Milestone (target T3b), Project 2021 perpetual: capture done, gate PASSED with stated reasoning, first analysis pass (setter-call list)

_Run Tue 10 Nov 2026 in the isolated trace VM (network disconnected). Written by the TRACE agent; not scanned or committed._

| item | value |
|---|---|
| VM build line | WINPROJ.EXE 16.0.20430.20182 sha256 F1A7E52729C01C115535B1277C5553302D15F996FCE8D433DFCE11E0FD7F5F6B; ProjectPro2021Retail, 5-day pass |
| Input | `wave30-insert-milestone-control.mpp` (hash C4490939C4B1.. matches the repo file) |
| Edit | as insert-summary, with `InsertMilestoneTask()` |
| Saved file SHA-256 (final capture) | BB677AC5CD16F92A C26295C35D71AA11 CC146DB864794FC8 727B9BA4D2ED142B (spaces added) |
| Saved file size | 240128 bytes, 96 streams (equal to the pair's) |
| Trace | 1052 MB, `trace-save3.ps1`: recording covers the edit and the save |

## Gate: traced insert-milestone against `wave30-insert-milestone.mpp`

**96 streams, 93 checked, 16 problem lines**, block SHA-256 62f6e6b13058daaaf89a8a48c5e61735a1690c56447659d9596c75c86b1a54a5 (host copy equal).

```
96 streams, 93 checked, 16 problems
bytes-differ ???114/Props 24
bytes-differ ???114/TBkndAssn/Fixed2Data 88
bytes-differ ???114/TBkndAssn/Var2Data 3
bytes-differ ???114/TBkndCal/Fixed2Data 30
bytes-differ ???114/TBkndRsc/Fixed2Data 20
bytes-differ ???114/TBkndTask/Fixed2Data 98
bytes-differ ???114/TBkndTask/FixedData 3
bytes-differ ???214/CEdl/FixedData 10
noise-whole ???214/CEdl/Var2Data 534->510
bytes-differ ???214/CEdl/VarMeta 1
bytes-differ ???214/CFilter/FixedData 20
bytes-differ ???214/CGrouping/FixedData 20
bytes-differ ???214/CTable/FixedData 10
bytes-differ ???214/CV_iew/FixedData 30
bytes-differ ???214/CV_iew/Var2Data 4
bytes-differ ???214/Props 18
size-differs ?SummaryInformation 292->284
noise-whole Props14 578->488
```

Judgement: PASS, as for insert-summary (same two capture fixes: automatic mode, COM property reads), with two lines beyond it: `TBkndAssn/Fixed2Data` 88 (= the 12-byte stamp runs at +4, +20, +52, +68, +100, +116 and one 32-byte run at +144) and `TBkndAssn/Var2Data` 3 bytes at +400.  Both lie in the assignment table's new row (see below); the 32-byte run is a stamp-sized region I did not decode, so treat it as "probably a stamp, not proven".  Task FixedData 3 bytes at +984 is the same creation stamp as in insert-summary.

## Analysis (first pass: field-setter calls)

405 setter calls in the recording (positions 14eb4f.. to 152b88:e2e).  Per table object (by key prefix):

| table | calls by row index | notes |
|---|---|---|
| Task (0x0b4...) | row 3: 40, row 4: 21, row 5: 21, row 6: 21, **row 7: 121** | row 7 is the inserted milestone: 121 calls, 67 distinct var keys, 55 fixed keys (insert-summary: 69 and 57) |
| Assignment (0x0f4...) | rows 0 to 2: 7 each, **row 3: 58** (positions 152b3c:560 to 152b88:e2e) | **a new assignment row is created by Insert Milestone** (34 var keys, 24 fixed keys); insert-summary gives that table one call on row 3 only |
| unidentified (0x0c4..., 0x0d4... keys) | 7 calls on row 4; 7 calls on rows 5 and 6 | as in insert-summary |
| four default tables | 14 to 31 calls each, first positions of the recording | setup at the start of the recording, not part of the insert |

The two task-row key sets differ by two keys in each list (67 against 69 var keys, 55 against 57 fixed keys); which keys was not compared.

The new assignment row's var keys are the `0x0f40...` set: 0044, 0283, 0284, 02d5, 0084, 008d, 0028, 0029, 002a, 0046, 0048, 0049, 004b, 004c, 004e, 0053, 0054, 009d, 0102, 0109, 010d, 0115, 0118, 011d, 028b, 0298, 02d6, 0302, 0303, 0305, 027a, 0031, 0032, 0299 (34 keys), fixed 0000, 027c, 027d, 0001, 0002, 0007, 0008, 000a, 000b, 000c, 0014, 0015, 0018, 0019, 001a, 001c, 001d, 0037, 0050, 0051, 0091, 0108, 010e, 027e (24).  Reading by eye from the VM screen.

## Not done
Which routine decides that a milestone gets an assignment row, the renumber order, and the value sources.  Positions and values are in `C:\trace\out\fc-insert-milestone.txt` in the VM.
