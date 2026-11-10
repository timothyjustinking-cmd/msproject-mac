# Trace of Font Bold (target T4, NEXT-ROUND A): capture done, gate PASSED; the item writer is NOT yet located; the font word's top byte is shown to vary per session

_Run Tue 10 and Wed 11 Nov 2026 in the isolated trace VM (network disconnected), round 2 of the TRACE agent.  Not scanned or committed.  Text only left the VM._

| item | value |
|---|---|
| VM build | WINPROJ.EXE 16.0.20430.20182, sha256 F1A7E527...F5F6B (same as round 1) |
| Input | `wave30-textstyle-control.mpp` (host SHA-256 7C520312...FC3F, copy hash-checked) |
| Edit | calculation automatic, user `trace`, open, `SelectTaskField(1, "Name", False)`, `FontBold(True)`, `FileSaveAs`; recorder attached before the edit (`trace-save4.ps1`, target `bold`) |
| Trace | `C:\trace\runs\bold\WINPROJ01.run`, 852 MB, positions to 130C6:86E; stays in the VM (checkpoint `analysis-2026-11-11-1`) |
| Saved file | `C:\trace\work\bold.mpp`, 239104 bytes, SHA-256 D1D71256 583D3D1D E5E3A832 6197E449 C56BAA5D B37BCA76 DCB8E7EB EDC602FC (spaces added) |

## Gate: traced bold against `wave30-textstyle.mpp`

`trace_gate.py bold.mpp wave30-textstyle.mpp wave30-textstyle-control.mpp wave30-textstyle-control-null.mpp`: **96 streams, 93 checked, 14 problems**, block SHA-256 `6d06e6e9b346ea6c832a96ae1b8ffc5f536e7b159aa2a5d703d481e1feb21dfd`, re-computed on the host from the transcribed text: equal.

```
96 streams, 93 checked, 14 problems
bytes-differ ???114/Props 26
bytes-differ ???114/TBkndAssn/Fixed2Data 60
bytes-differ ???114/TBkndCal/Fixed2Data 30
bytes-differ ???114/TBkndRsc/Fixed2Data 20
bytes-differ ???114/TBkndTask/Fixed2Data 70
bytes-differ ???214/CEdl/FixedData 10
noise-whole ???214/CEdl/Var2Data 582->478
bytes-differ ???214/CEdl/VarMeta 1
bytes-differ ???214/CFilter/FixedData 20
bytes-differ ???214/CGrouping/FixedData 20
bytes-differ ???214/CTable/FixedData 10
bytes-differ ???214/CV_iew/FixedData 30
bytes-differ ???214/CV_iew/Var2Data 5
bytes-differ ???214/Props 19
size-differs ?SummaryInformation 296->288
noise-whole Props14 570->464
```

Same problem set as the null saves (per-save stamps in the Fixed2Data records, the user-name-sized SummaryInformation, CEdl/Props14 noise).  No size differs in `CV_iew/Var2Data` (100234 bytes on both sides) or `CV_iew/VarMeta`, so the traced save contains the same inserted item and the same offset rewrites as the pair.  Verdict: PASS, with the one point below.

## Finding: the font word's top byte differs between sessions (answers RW35-W10 in part)

The gate's 5 `CV_iew/Var2Data` bytes, listed by `bytediff.py` in the VM (pair value against the traced value; the stream's own offsets, not hash-checked, read off the screen):

| stream offset | pair | traced | meaning |
|---|---|---|---|
| 85258 | 0x18 | 0x27 | a stamp byte inside the item before the new one (offset 14 before the new item) |
| **85327** | **0x90** | **0x00** | **the top byte of the new item's font word `0x90000001`** (item at 85272: 12-byte header + 44-byte entry, so 85284 + 43) |
| 87598, 87599, 87600 | c0 4d 6c | 80 99 6f | a 3-byte stamp in a later item |

So the same edit, made by the same API call, stores `0x90` in the pair's session and `0x00` in this one (the other pairs store `0x6b`): the top byte is not derived from the edit, the view or the file.  That is evidence for the "uninitialised byte" reading (RW35-W10) and against any rule that computes it.  The port's `save_view_cell_style` writing `0x90` as a constant is therefore one of several equally valid values; a byte-equal match to Project cannot be promised, and the pair gate must mask that offset.

## What was tried to locate the item writer, and why it failed

- The item is not in the memory-mapped docfile views of this trace: `NtMapViewOfSection` gives 8 views (`6a6c:7f` a 0xdf000 font file, `c2b6:6b` 16 MB, two 512 KB views, a 16 KB, three 4 KB), but at the end of the trace and at four earlier positions (11200, 12000, 12800, 13000) all of them read as zeros, and searches for the item's 8-byte and 24-byte anchors found nothing; a 512 MB heap range search (0x1f2c0000000..) also found nothing at the end.  There are no `NtWriteFile` calls.  `!address` returns nothing under TTD.
- `FUN_140427d70` (the stream `IStream::Write` wrapper from the calculate trace) has 52 calls in this trace, the largest 0x41b bytes: the 100 KB `CV_iew/Var2Data` does not go through it (it is written in another way, or in chunks I did not follow).
- Helpers written for the next attempt (`chain.js`): `findtrace` (search a range and trace a hit's bytes back through copies), `trace4`, `srcfind`, `argcalls`, `wfiles`, `unmaps`.

## Next step for a fresh agent

1. Find where the 100 KB view stream is built in memory: execute-watch `FUN_140377770` (the table stream writer; +0x1e7 is its Var2Data call) and the 214 view code path; or break on the first `memcpy` whose source holds `0c 00 40 22` (the item tag) by scanning positions through `unmaps()`/`views()` timestamps (the mapped views are probably zeroed or remapped between the write and the end: search at the position just after each `NtMapViewOfSection` that follows 11092:60).
2. From a located item, run `trace4` on offset +43 (the top byte) for the last write: either a copy from a stack or heap slot that no instruction in the trace wrote (uninitialised) or a store of a defined value.
3. The VarMeta offsets and the `214/Props` size byte follow from the same write chain: the new size `0x1878A` (`8a 87 01 00`) is the value to search for.
