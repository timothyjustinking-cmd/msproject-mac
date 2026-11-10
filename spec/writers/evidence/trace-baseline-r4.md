# Set Baseline (round 4): the "caveat" is automatic recalculation, the Props growth words are table properties

_Wed 11 Nov 2026, round 4 of the TRACE agent.  VM `msp-trace` (Project 2021, build 16.0.20430.20182), traces `baseline` and `baseline-open`, public fixtures only.  Written to `build\trace-2021\` only; the coordinator merges._

**Hash-checked.**  Every VM output came through the KVP channel with `sha OK` (first 12 hex digits): drift runs `582A9F48A332` (auto-null), `DB6EC441E9B3` (man-null and man-base, identical text), gate of the manual-mode baseline `AF5F16FB6A66` (block sha256 `d880e8ab5ea18db8586540ca111ba699f4afe8a892ad1951af20dcc509792e28`), setter list `2400E9703BC5` (`trace-fc4-baseline.txt`; the host copy's own SHA-256 starts `7A43BE23FBC9`), r4b* and r4d* as listed.  Interpretation is mine.

## 1a. The Task FixedData date words and the 2 Rsc FixedMeta bytes: a session effect (calculation mode), not Set Baseline

Experiment (no recorder, `drift.ps1` in the VM, input `wave30-baseline-control.mpp`, user name `trace`):

| run | Task FixedData bytes changed vs the input |
|---|---|
| open, **automatic** mode, save (no edit) | **27**: `+346/+548/+750` `78 1e f1 3b 20 1c` -> `c0 12 f2 3b d8 27` (Start one working day later, Finish word), `+566/+570/+768/+772` low date bytes, `+638` `00..` -> `80 25 00 00 80 25`, `+840` -> `40 38 00 00 40 38` |
| open, **manual** mode, save (no edit) | **0** |
| open, **manual** mode, `BaselineSave(True,0,0)`, save | **0** |

The gate of the manual-mode Set Baseline against the committed pair `wave30-baseline-set.mpp`: **96 streams, 93 checked, 16 problems, with no `TBkndTask/FixedData` line and no `TBkndRsc/FixedMeta` line** (the round-1 and round-2 gates had 18, with those two).  So both caveat items are Project's recalculation of the file when it is opened in automatic mode (the engine moves the stored Start of every task to the next working-day start and fills two duration-like words), which the pair evidently did not undergo: the application-level calculation mode persists between runs and the pair was made in manual mode.  In the `baseline` trace the engine is not in the recording at all (entries of the scheduling stores `FUN_140290dec` and `FUN_14028e298` inside the window: 0); only the save-time setter calls remain, keys 0x0b400023 / 0x0b400024 (Start / Finish) on rows 4 to 6 at positions 1ff73..1ffca with values `3bf212c0` etc. copied from a stack staging buffer in the stream flush (call chain 0x23bb2a <- 0x3a16b2 <- 0x377cbe <- 0x43f8d6 <- 0x47f308).  **For the port: Set Baseline does not touch the schedule words; do not model the 27 bytes.**  (The writer of the recalculated values is the engine at open time, outside any baseline recording; `baseline-open` has the open inside the recording but its setter list shows only the four project-row calls, the task rows being loaded from the stream, so the engine run was not located there.)

## 1c. The words the baseline pair left unexplained in `114/Props` (props-records.md)

Pair `wave30-baseline-control` -> `wave30-baseline-set` (resync diff, `r4_p3.py`), `114/Props` 88734 -> 88844:

| stream offset | change | meaning, from the trace | writer (RVA) |
|---|---|---|---|
| +0, +4 | size words `0x15a9a -> 0x15b08` | blob size after the appended record (+12 + 98) | 0x2541df (blob +0) |
| +12 | `0x106 -> 0x107` | "declared entries": +1 per record added through the container set-by-id | `add word [rdi+8],1` at 0x254122 |
| +838 | `01 -> 00` | pending flag record 0x02400024 | round 3 (0x397cea) |
| +3386 | `0x020a -> 0x06ce` | typed property `0x00010001` = **Task Var2Data size** (522 -> 1742 = the stream's new size) | position 1fff6:6d, 4-byte memcpy of the value computed at **0x397993** (`mov [rbp-18h],eax` with `eax = [table+0x344]`) |
| +40290 | `0x56 -> 0x196` | `0x00010002` = **Rsc Var2Data size** (86 -> 406) | 2008a:2600, same code |
| +62494 | `0x16 -> 0x7a` | `0x00010003` = **Cal Var2Data size** (22 -> 122) | 200c5:1050, same code |
| +63502 | `0x18c -> 0x474` | `0x00010004` = **Assn Var2Data size** (396 -> 1140) | next write after 200c8 (not listed) |
| +62446 | `7 -> 8` | `0x01000003` = **[table+0x250]** of the Cal table (entry count; a new calendar, the built-in baseline calendar, is created: Cal FixedData 100 -> 112, VarMeta +1 entry, Var2Data +100) | 200c3:20f6; value stored at **0x3978d9** (`eax = [rbx+0x250]`, compared with `[rbx+0x318]`) |

All five are produced by the same per-table "publish table properties" function (the caller is the stream flush at 0x37788a <- 0x377ce7 <- 0x43f8d6 <- 0x47f308): for each table `rbx` it sets the property id `[rbx+0x158] | 0x10000` to `[rbx+0x344]` (the var-block running end) and `[rbx+0x158] | 0x1000000` to `[rbx+0x250]`, through the vtable call at 0x1099868 to the Props container's set-by-id (the in-place branch of `FUN_140253fe4`).  The id's low word is the table number (1 Task, 2 Rsc, 3 Cal, 4 Assn).  **Rule for the port: at save, rewrite these typed values from the tables' final Var2Data sizes (and entry counts), after the var blocks are laid out.**  This also explains the note in props-records.md that only `0x00010001` was "tied to a measured quantity": all four are.

The 110-byte appended record and its framing: `trace-props-framing-r4.md`.

## 1c (rest) per-stream-offset rows

`mapdiff.py` (host; public pairs + `trace-fc4-<target>.txt`, the hash-checked setter lists) lays out, per changed Task / Assn FixedData / Fixed2Data run, the stream geometry (Task FixedData `48 + 202*uid + off`; Task Fixed2Data `192 + 64*uid + off`; Assn Fixed2Data `48*row + off`; Assn FixedData `110*row + off`) and the setter call whose uid, row offset and first dword fit: `trace-rowmap-baseline.txt` (no Task/Assn FixedData byte differs in the baseline pair; the Fixed2Data differences are single bytes at `rel 3`, `rel 19` and `rel 27` = the low byte of the 16-byte GUIDs written by `UuidCreateSequential`, see `trace-insert-milestone-r4.md`).
