# Trace of Calculate Project, wave 35 variant `linked` (NEXT-ROUND C): captures done, gates PASSED, write-back compared

_Wed 11 Nov 2026, isolated trace VM (network disconnected), round 2 of the TRACE agent.  Not scanned or committed.  Text only left the VM.  Build: WINPROJ.EXE 16.0.20430.20182 (the VM's pinned build)._

| item | value |
|---|---|
| Input | `wave35-calculate-linked-control.mpp` (host SHA-256 copy-checked); manual calculation, user `trace`, recorder attached before the edit (`trace-save4.ps1`) |
| Null capture | `w35-linked-null`: no edit, save only; trace 796 MB; saved 236544 bytes |
| Calculate capture | `w35-linked-calc`: `CalculateProject()` then save; trace 800 MB; saved 236544 bytes |
| Traces | stay in the VM (checkpoint `analysis-2026-11-11-1`; the linked calculate trace was recaptured after that checkpoint) |

**Capture note.**  The first linked calculate capture (856 MB) gave only 88 setter calls over the whole trace and none on the Task table, although its gate passed; the cause is not established (the recording may have missed the engine phase).  The capture was repeated (800 MB; gate again 96 streams, 93 checked, 13 problems, first problem line `114/Props 30`; the rest of that second block was not transcribed) and the second trace shows the normal phase (215 calls, rows 3 to 5).  The tables below are from the second capture.  The first run is gone (the folder was overwritten); the gate block shown below is the first capture's.

## Gates (hash-checked on the host: `gate-check.py`, 8 of 8 blocks equal)

**Null capture against the committed `wave35-calculate-linked-control-null.mpp`:**

```
96 streams, 93 checked, 13 problems
bytes-differ ???114/Props 30
bytes-differ ???114/TBkndAssn/Fixed2Data 48
bytes-differ ???114/TBkndCal/Fixed2Data 12
bytes-differ ???114/TBkndCons/Fixed2Data 36
bytes-differ ???114/TBkndTask/Fixed2Data 60
bytes-differ ???214/CEdl/FixedData 12
noise-whole ???214/CEdl/Var2Data 654->522
bytes-differ ???214/CFilter/FixedData 24
bytes-differ ???214/CGrouping/FixedData 24
bytes-differ ???214/CTable/FixedData 12
bytes-differ ???214/CV_iew/FixedData 36
bytes-differ ???214/CV_iew/Var2Data 3
bytes-differ ???214/Props 21
size-differs ?SummaryInformation 300->292
noise-whole Props14 582->486
sha256 17b2e4f6a1580d1989edfa94722409cb072477efb576695cd8eceb1e92e69a85
```

**Calculate capture against the committed `wave35-calculate-linked.mpp`:**

```
96 streams, 93 checked, 13 problems
bytes-differ ???114/Props 30
bytes-differ ???114/TBkndAssn/Fixed2Data 48
bytes-differ ???114/TBkndCal/Fixed2Data 12
bytes-differ ???114/TBkndCons/Fixed2Data 36
bytes-differ ???114/TBkndTask/Fixed2Data 60
bytes-differ ???214/CEdl/FixedData 12
noise-whole ???214/CEdl/Var2Data 618->522
bytes-differ ???214/CFilter/FixedData 24
bytes-differ ???214/CGrouping/FixedData 24
bytes-differ ???214/CTable/FixedData 12
bytes-differ ???214/CV_iew/FixedData 36
bytes-differ ???214/CV_iew/Var2Data 3
bytes-differ ???214/Props 21
size-differs ?SummaryInformation 300->292
noise-whole Props14 584->486
sha256 529697aa49502cab404649c0780fcd2f21f2fe1e7b1d89d1ebe4b5962925b2ba
```

Both gates have no `size-differs` other than the user-name-sized `SummaryInformation`, and no `bytes-differ` in any Task, Assn, Cons, Rsc or Cal data stream other than the per-record `Fixed2Data` stamp words, the Props stream and the 214 view state: the null saves' noise set.  **Verdict: PASS.**  **Build-only differences:** none beyond the noise set.  If the committed pair was made on host build 16.0.20527 (not stated in the repo), that build leaves no extra byte in these streams: the 16.0.20430 trace saves equal it outside the per-save stamps.  The `114/Props` line (bytes that differ from the host's file outside the control-vs-null mask) is the per-file/per-save identity records, see `trace-props-framing.md`.

## The engine's write-back: setter calls, compared with the pair

The setter calls were listed as for T1 (`FUN_1403dd790` over the whole trace; `runfc.ps1`), then reduced to the calculate phase (rows 3 to 5 = uid 0 to 2).  `trace-cmp-table.txt` (all four variants, 24 lines, **SHA-256 `A5FAB4D1A3C2916521CB18B0D2BF405B449CC07F4B89A1E8F2594255A5B1D72B`**, transcribed and re-hashed on the host: equal).  Keys shown without `b400` (023 = Start, 024 Finish, 025 EarlyStart, 026 EarlyFinish, 027 LateStart, 028 LateFinish, 01d Duration, 01f RemainingDuration, 015 FreeSlack, 1b6 StartSlack, 1b7 FinishSlack, 183 and 0f9 the unnamed words at row offsets 0xae and 0xac, 4e2 RecalcFlags, 4e7 an unnamed date word, 4e3/4e4/4e5 the var slots).  `nN` is the number of calls on the row; only keys that were called appear.  The `01d` of rows 4 and 5 is the 8-byte field read as 4 bytes and is not a duration.

```
li3 n39 A 024=3c0227d8 025=3bf212c0 026=3c0227d8 027=3bf212c0 028=3c0227d8
li3 B 015=0 01d=f3c0 01f=f3c0 0f9=0 183=ffffffff 1b6=0 1b7=0 4e2=40 4e7=ffffffff
li4 n18 A 025=3bf212c0 026=3bfd27d8 027=3bf212c0 028=3bfd27d8
li4 B 015=0 0f9=1 183=ffffffff 1b6=0 1b7=0 4e2=30 4e7=3bf13138
li5 n38 A 023=3c0012c0 024=3c0227d8 025=3c0012c0 026=3c0227d8 027=3c0012c0 028=3c0227d8
li5 B 015=0 01d=ea8c6b0e 0f9=3d260001 183=3bfd27d8 1b6=0 1b7=0 4e2=28 4e7=3bfd27d8
```

Cross-reference with the pair's changed words (`rows.py` on the public control and edit files; row offsets as in `trace-calculate.md`):

| row | words the pair changes | the setter call that writes each |
|---|---|---|
| project row (uid 0) | Duration, RemainingDuration (+132, +136), Finish, EarlyFinish, LateFinish | li3: 01d, 01f = f3c0 (6240 min, 13 days, the same as `wave30-calculate`), 024, 026, 028 = 3c0227d8 |
| leaf uid 1 (uid 1) | `0xae` (+174) | li4: 183 = ffffffff |
| leaf uid 2 (uid 2) | all six date words Start to LateFinish (98..119) and `0xae` (174..176) | li5: 023..028 = 3c0012c0 / 3c0227d8 (B moves behind A: FS+0), 183 = 3bfd27d8 |

What this shows for the port: the engine sets the same field list on each row in every variant and the stream changes only where the value differs (see `trace-calculate.md` round 2).  The field set per row does not depend on the variant (39 or 38 calls on the project row, 18 on leaf uid 1, 21 or 38 on leaf uid 2: the `38` rows are those where B's Start (023) is also set, i.e. B moves); the values do.

## Open

- Value sources were hopped only for the wave 30 trace (`trace-calculate.md`); the same two store routines (`FUN_140290dec`, `FUN_14028e298`) hold the date arithmetic here; the lag and the SNET constraint are applied upstream of those stores (not decomposed).
- FixedMeta, Fixed2Data and the VarMeta offsets: not decomposed.
