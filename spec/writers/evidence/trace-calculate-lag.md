# Trace of Calculate Project, wave 35 variant `lag` (NEXT-ROUND C): captures done, gates PASSED, write-back compared

_Wed 11 Nov 2026, isolated trace VM (network disconnected), round 2 of the TRACE agent.  Not scanned or committed.  Text only left the VM.  Build: WINPROJ.EXE 16.0.20430.20182 (the VM's pinned build)._

| item | value |
|---|---|
| Input | `wave35-calculate-lag-control.mpp` (host SHA-256 copy-checked); manual calculation, user `trace`, recorder attached before the edit (`trace-save4.ps1`) |
| Null capture | `w35-lag-null`: no edit, save only; trace 852 MB; saved 236544 bytes |
| Calculate capture | `w35-lag-calc`: `CalculateProject()` then save; trace 872 MB; saved 236544 bytes |
| Traces | stay in the VM (checkpoint `analysis-2026-11-11-1`) |

## Gates (hash-checked on the host: `gate-check.py`, 8 of 8 blocks equal)

**Null capture against the committed `wave35-calculate-lag-control-null.mpp`:**

```
96 streams, 93 checked, 13 problems
bytes-differ ???114/Props 26
bytes-differ ???114/TBkndAssn/Fixed2Data 40
bytes-differ ???114/TBkndCal/Fixed2Data 10
bytes-differ ???114/TBkndCons/Fixed2Data 30
bytes-differ ???114/TBkndTask/Fixed2Data 50
bytes-differ ???214/CEdl/FixedData 10
noise-whole ???214/CEdl/Var2Data 648->516
bytes-differ ???214/CFilter/FixedData 20
bytes-differ ???214/CGrouping/FixedData 20
bytes-differ ???214/CTable/FixedData 10
bytes-differ ???214/CV_iew/FixedData 30
bytes-differ ???214/CV_iew/Var2Data 3
bytes-differ ???214/Props 18
size-differs ?SummaryInformation 296->288
noise-whole Props14 576->480
sha256 6a6522f93e6263481d21fd5da16114b3f5bc0cec78fc2cc440a3df992a02d79c
```

**Calculate capture against the committed `wave35-calculate-lag.mpp`:**

```
96 streams, 93 checked, 13 problems
bytes-differ ???114/Props 26
bytes-differ ???114/TBkndAssn/Fixed2Data 40
bytes-differ ???114/TBkndCal/Fixed2Data 10
bytes-differ ???114/TBkndCons/Fixed2Data 30
bytes-differ ???114/TBkndTask/Fixed2Data 50
bytes-differ ???214/CEdl/FixedData 10
noise-whole ???214/CEdl/Var2Data 612->516
bytes-differ ???214/CFilter/FixedData 20
bytes-differ ???214/CGrouping/FixedData 20
bytes-differ ???214/CTable/FixedData 10
bytes-differ ???214/CV_iew/FixedData 30
bytes-differ ???214/CV_iew/Var2Data 3
bytes-differ ???214/Props 18
size-differs ?SummaryInformation 296->288
noise-whole Props14 578->480
sha256 114e9281e749b8084a710f7428da246e737bf4fd0d0cf8c22117fabaaea834e5
```

Both gates have no `size-differs` other than the user-name-sized `SummaryInformation`, and no `bytes-differ` in any Task, Assn, Cons, Rsc or Cal data stream other than the per-record `Fixed2Data` stamp words, the Props stream and the 214 view state: the null saves' noise set.  **Verdict: PASS.**  **Build-only differences:** none beyond the noise set.  If the committed pair was made on host build 16.0.20527 (not stated in the repo), that build leaves no extra byte in these streams: the 16.0.20430 trace saves equal it outside the per-save stamps.  The `114/Props` line (bytes that differ from the host's file outside the control-vs-null mask) is the per-file/per-save identity records, see `trace-props-framing.md`.

## The engine's write-back: setter calls, compared with the pair

The setter calls were listed as for T1 (`FUN_1403dd790` over the whole trace; `runfc.ps1`), then reduced to the calculate phase (rows 3 to 5 = uid 0 to 2).  `trace-cmp-table.txt` (all four variants, 24 lines, **SHA-256 `A5FAB4D1A3C2916521CB18B0D2BF405B449CC07F4B89A1E8F2594255A5B1D72B`**, transcribed and re-hashed on the host: equal).  Keys shown without `b400` (023 = Start, 024 Finish, 025 EarlyStart, 026 EarlyFinish, 027 LateStart, 028 LateFinish, 01d Duration, 01f RemainingDuration, 015 FreeSlack, 1b6 StartSlack, 1b7 FinishSlack, 183 and 0f9 the unnamed words at row offsets 0xae and 0xac, 4e2 RecalcFlags, 4e7 an unnamed date word, 4e3/4e4/4e5 the var slots).  `nN` is the number of calls on the row; only keys that were called appear.  The `01d` of rows 4 and 5 is the 8-byte field read as 4 bytes and is not a duration.

```
la3 n39 A 024=3c0327d8 025=3bf212c0 026=3c0327d8 027=3bf212c0 028=3c0327d8
la3 B 015=0 01d=10680 01f=10680 0f9=3d270000 183=ffffffff 1b6=0 1b7=0 4e2=40 4e7=ffffffff
la4 n18 A 025=3bf212c0 026=3bfd27d8 027=3bf212c0 028=3bfd27d8
la4 B 015=0 0f9=1 183=ffffffff 1b6=0 1b7=0 4e2=30 4e7=3bf13138
la5 n38 A 023=3c0112c0 024=3c0327d8 025=3c0112c0 026=3c0327d8 027=3c0112c0 028=3c0327d8
la5 B 015=0 01d=c6020e2e 0f9=3c030001 183=3c0027d8 1b6=0 1b7=0 4e2=28 4e7=3c0027d8
```

Cross-reference with the pair's changed words (`rows.py` on the public control and edit files; row offsets as in `trace-calculate.md`):

| row | words the pair changes | the setter call that writes each |
|---|---|---|
| project row (uid 0) | Duration, RemainingDuration (+132..+134, +136..+138: the 3-byte values), Finish, EarlyFinish, LateFinish | la3: 01d, 01f = 10680 (6720 min, 14 days: A 10 days, one day of lag, B 3 days), 024, 026, 028 = 3c0327d8 |
| leaf uid 1 (uid 1) | `0xae` (+174) | la4: 183 = ffffffff |
| leaf uid 2 (uid 2) | six date words and `0xae` | la5: 023, 025, 027 = 3c0112c0 (one working day later than the linked Start 3c0012c0), 024, 026, 028 = 3c0327d8, 183 = 3c0027d8 |

What this shows for the port: the engine sets the same field list on each row in every variant and the stream changes only where the value differs (see `trace-calculate.md` round 2).  The field set per row does not depend on the variant (39 or 38 calls on the project row, 18 on leaf uid 1, 21 or 38 on leaf uid 2: the `38` rows are those where B's Start (023) is also set, i.e. B moves); the values do.

## Open

- Value sources were hopped only for the wave 30 trace (`trace-calculate.md`); the same two store routines (`FUN_140290dec`, `FUN_14028e298`) hold the date arithmetic here; the lag and the SNET constraint are applied upstream of those stores (not decomposed).
- FixedMeta, Fixed2Data and the VarMeta offsets: not decomposed.
