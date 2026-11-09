# Dynamic trace of Project's save: runbook (wave 34 decision 3, approved Sun 8 Nov 2026)

_Written by the Opus coordinator, Sun 8 Nov 2026.  The operator approved decision 3 of
`docs/handoff/wave34-brief.md` ("a dynamic trace of `WINPROJ.EXE`") on 8 Nov 2026.  The
brief's conditions stand:

- it runs on an isolated VM;
- it is the coordinator's work, not the builder's;
- it uses free Microsoft tooling;
- it never uses exoproc and never runs on the work machine.

This file is the plan and the method.  Nothing in it has been run yet._

## 1. What the trace is for

Each decode note in `spec/writers/` lists its open rules and says whether a save-time trace
could settle them (the W34-0.4 record).  The trace answers, for one stream offset, "which
routine wrote this byte, and where did its value come from".  The pairs cannot answer that,
and the static model is too large to read for it.  The targets, in the order wave 35 needs
them:

| # | target | open question | note | trace input (a committed pair's control) |
|---|---|---|---|---|
| T1 | Calculate Project (wave 35 G1) | which task, assignment and project words the engine's write-back rewrites after F9, and the nine new VarMeta entries; the static model does not expose the write-back at all | `scheduler-save.md` | `wave30-calculate-control.mpp`; second run `wave34-calculate-second-control.mpp` |
| T2 | Set Baseline 0 (wave 35 G3.2) | the 94 new var blocks (26 undecoded), the Props tail growth, the record-framing routine | `props-records.md`, `var-meta.md` | `wave30-baseline-control.mpp` |
| T3 | Insert Summary / Milestone (wave 35 G3.1) | renumber and reparent order, the 2.5 ordinal, the placeholder rows, CEdl | `table-growth.md` | `wave30-insert-summary-control.mpp`, `wave30-insert-milestone-control.mpp` |
| T4 | view-stream item replace (wave 35 G3.4, fonts) | the item replace rule and the font word `0x90000001` | `view-stream-214.md` | the W34-4.5 Bold pair's control (added to the script when T4 is reached) |
| N | null save | the per-save noise words; every target's diff is read against it | all | `wave30-calculate-control.mpp`, no edit |

The trace does not replace the pairs.  It turns "this byte changed" into "this routine
wrote it from that field".  The writer is still enabled only when the port's save matches
Project's pair field by field (rule 1).

## 2. Why not on the work machine

- **Memory capture.** A TTD recording holds every byte the process touched: open files,
  the recent-files list, the Office account token and anything on the clipboard.
- **Injection.** Attaching the recorder injects into the process.
- **Isolation.** The VM holds only public synthetic fixtures, a neutral Windows user and a
  neutral Office user name.

## 3. The build problem (checked Sun 8 Nov 2026)

The Ghidra model is of an older build than the installed one.  Neither the model nor the
repo records the decompiled binary's hash.  The audits from wave 22 on name
16.0.20430.20182 as the target; this machine runs 16.0.20527.20112:

| | `.text` end | `.rdata` end | `.data` end |
|---|---|---|---|
| decompiled (`analysis/meta/memory.tsv`) | `140f497ff` | `1412a47ff` | `1415e6a57` |
| installed WINPROJ.EXE 16.0.20527.20112 | `140f57943` | `1412b69aa` | `1415f8a87` |

The installed build's code section is 0xE144 bytes (about 57 KB) larger, so a trace address
from today's build does not map onto a `FUN_1400xxxxx` name.  There are two ways to fix it,
in order:

1. **Pin the VM to the decompiled build.**  Install 16.0.20430.20182 in the VM with the
   Office Deployment Tool (`<Add ... Version="16.0.20430.20182">`, updates disabled).
   - Hash its WINPROJ.EXE and check its section table against `memory.tsv` (script:
     section starts and ends must be equal).
   - Then a trace RVA maps straight onto the model (`FUN_` address = 0x140000000 + RVA).
   - **Done, Mon 9 Nov 2026.**  The Current Channel network still serves the build.  The VM
     `msp-trace` runs ProjectPro2021Retail 16.0.20430.20182 with updates off, installed by the
     ODT with `Version="16.0.20430.20182"`.
     - Every section start matches `memory.tsv`, and every end matches once the virtual-size
       end is rounded up to the 0x200 file alignment Ghidra uses.  For example, `.text` ends
       at `140f49653` and rounds to `140f497ff`, and `.data` ends at `1415e6a57` exactly.
     - WINPROJ.EXE SHA-256:
       `F1A7E52729C01C115535B1277C5553302D15F996FCE8D433DFCE11E0FD7F5F6B`.
     - So option 2 is not needed.
2. **Otherwise re-decompile the VM's build.**
   - Run Ghidra headless on the VM's pinned WINPROJ.EXE (`ghidra-project/` is regenerable;
     the auto-analysis of a 32 MB image takes hours).
   - Match functions to the old model by byte signature, to carry the field-id work across.
   - Record the new binary's hash in `analysis/meta/BUILD.txt`, so this cannot recur.

Either way the VM's Project build is pinned and never updates during the trace work.  A
build change restarts this section.

## 4. The VM (operator steps; the coordinator cannot do these)

Each step changes system settings, downloads software or enters a licence, so the operator
does them.

1. **Hyper-V.**  Enable Hyper-V on Windows 11 Pro.  This is a Windows feature, needs
   elevation, and reboots.
   - Create one Generation 2 VM: 4 vCPU, 8 GB RAM and a 128 GB dynamic disk on `D:` or
     `E:`, never under Dropbox.
   - Create it with a Windows 11 image and a local, neutral user (for example `trace`).
2. **Office.**  Install Project Professional 2021 in the VM with the Office Deployment
   Tool: the pinned version (section 3), updates off, and Office UserName and Initials
   neutral.
   - Activate it with the operator's licence.
   - Then disconnect the VM's network: no switch, or an internal switch only.
3. **Debugger.**  Install WinDbg from Microsoft in the VM, for the analysis.  The recorder,
   `C:\Windows\System32\tttracer.exe`, is built into Windows (1.11.580 on this host).
4. **The trace folders.**  Create `C:\trace\TRACE-VM.marker` (an empty file; the capture
   script refuses to run without it), plus `C:\trace\in`, `C:\trace\work`, `C:\trace\runs`
   and `C:\trace\out`.
5. **Fixtures in, text out.**
   - Copy the inputs from `tests/data` into `C:\trace\in`.  They are public synthetic
     fixtures; never anything from `corpus/`.
   - Share only `C:\trace\out` with the host, and only text crosses it.
   - Take a VM checkpoint named `clean` before the first trace.

## 5. Capture (in the VM)

```
pwsh -NoProfile -File C:\trace\tools\trace-save.ps1 -Target null
pwsh -NoProfile -File C:\trace\tools\trace-save.ps1 -Target calculate
```

`tools/trace/trace-save.ps1`, run once per target:

1. Copies the pair's control from `C:\trace\in`.
2. Opens it through COM, with calculation manual and a neutral user name.
3. Repeats the pair's exact edit (the `scripts/oracle_w30_*` calls).
4. Attaches `tttracer -attach <pid>` only around `FileSaveAs`, then `tttracer -stop`.
5. Records the WINPROJ build and hash, and the saved file's SHA-256.

**First check (gate).**  The saved `C:\trace\work\<target>.mpp` must equal the committed
pair's edited file on every stream, outside the null-save noise.  Use the repo's existing
comparison (`tests/save_pair_noise.h` through the `mpp_dump`/pair tools, run on the host
against a copy).  A trace of a save that differs from the pair answers the wrong question,
so re-measure before analysing.  Run `tttracer -?` once first to confirm the switches on
the VM's build.

## 6. Analysis (WinDbg in the VM; one question per stream offset)

The `.mpp` is a compound file, so Project writes each stream through structured storage
(`IStream::Write`, in `coml2.dll` on current Windows).  The method below uses only
documented TTD data-model queries.

1. **Open** `C:\trace\runs\<target>\WINPROJ*.run` in WinDbg.  Run `!tt 0` and `lm`, and
   note the WINPROJ base, so that RVA = address - base.
2. **Map streams.**  `dx -g @$cursession.TTD.Calls("coml2!*CreateStream*")` (and
   `*OpenStream*`) gives each stream name and the object it returned.
3. **List writes.**  `dx -g @$cursession.TTD.Calls("coml2!*Stream*Write*")` lists each
   write with `this`, the buffer, the length and its time position.  Track the stream
   position through `*Seek*` calls.
4. **Find the call.**  For the target stream offset, take the write call whose range
   covers it, then the buffer address of that byte.
5. **Find the writer.**  `dx @$cursession.TTD.Memory(<buf+k>, <buf+k+4>, "w")` lists the
   instructions that last wrote those bytes before the call.  Travel to the last one
   (`!tt <position>`), then run `k` for the call stack and `u .` for the instruction.
   Convert each WINPROJ frame to an RVA and look it up in the model
   (`analysis/meta/functions.tsv`, `decompiled/`).
6. **Find the value's source.**  From the writing instruction's source operand, repeat
   step 5 with `"r"` and then `"w"` on the source address.  Stop at a field the reader
   already decodes (a task row field, a calendar value) or at a constant.
7. **Record** one row per question in `C:\trace\out\<target>.md`:
   - the stream and offset;
   - the old and new word;
   - the writing RVA and its `FUN_` name;
   - the call-stack RVAs;
   - the value's source (a decoded field id, a computed value with its routine, or a
     constant);
   - the TTD position.

   Never record task, resource or project names, or any raw memory dump; the fixtures are
   synthetic, but the rule is the same as for the corpus.

## 7. What leaves the VM, and what gets committed

- **Out:** only `C:\trace\out\*.md`, the `build.txt` and `saved.sha256` lines, and the
  section-table check.
- **Kept in:** the `.run` files and the saved `.mpp` copies stay in the VM.  Delete them
  when the target's note is committed; roll back to the `clean` checkpoint between
  targets.
- **Committed:** each target's findings, under `spec/writers/evidence/trace-<target>.md`,
  with the VM build hash.  The decode note's "rule" section cites them.
- **Scans:** `guard_names.py`, `scan_client_names.py` and `check_markers.py` run before the
  commit, as for any fixture.
- **Who acts on it:** a builder wave uses those notes like any other evidence.  The builder
  never runs the trace.

## 8. Order of work

1. The operator does section 4, steps 1 to 5.
2. The coordinator checks the VM build against section 3.  It pins the build if possible,
   otherwise schedules the re-decompile.
3. N, then T1, before wave 35 G1 needs it.  If T1 is not ready when G1 starts, G1 runs on
   the pairs as briefed, and the trace notes feed its retry.
4. T2 and T3 before wave 35 G3, then T4.
