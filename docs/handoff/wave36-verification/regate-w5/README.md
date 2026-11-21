# W-5: the committed re-gate of the trace recordings

Coordinator, Sun 22 Nov 2026.  Gate `tools/trace/trace_gate.py` at `efe9061` (sha256 `4a3543e3930ec52a9137afbc22283052752e6059409e3a6a509bd6daccf13c48`), run on the host
from the 563 saved copies of V-1 (every copy sha256-equal to the guest list), against the committed reference,
control and control-null of each target.  OPEN means every problem line also appears when the control-null is
gated against its own control.

**250 targets: 198 PASS, 46 FAIL, 0 OPEN, 0 no-null, 6 missing copy.**

Against the V-2 triage with spans widened (215 PASS, 29 FAIL): the same 29 FAIL; 17 that passed there FAIL here,
all R36W5-1, because W36-C.2 compares the stamp of a row the control holds.  Every gate text is `gate-<target>.txt`;
`regate-w5.csv` gives each target its verdict and, for a FAIL, its finding.

| id | FAILs | finding |
|---|---|---|
| R36W5-1 | 17 | VM against host: the VM save rewrote the 3-byte stamp of a row the control already holds; the host reference kept the control value.  These passed before W36-C.2 only because every row stamp was masked (R36G0f-2).  Cause undecided (noise of an edited row, or a Project build difference) |
| R36W5-2 | 6 | undecided: one Rsc Fixed2Meta byte; the round 15 re-recordings keep it, so not the session chain |
| R36W5-3 | 6 | host: the committed reference does not hold the edit (host TableEdit and DateFormat take no effect; Outline=False had no effect; one bold row only).  The VM recordings are the only Project evidence |
| R36W5-4 | 2 | undecided: Task FixedMeta and Assn Fixed2Meta bytes, kept by the round 15 re-recording |
| R36W5-5 | 3 | noise shown in the trace (pane height or width, stale heap bytes) but no mask in the gate |
| R36W5-6 | 2 | undecided: flag 0xc0 on a different Assn Props slot; origin outside WINPROJ (round 14) |
| R36W5-7 | 1 | VM: the spec ran automatic and skipped the oracle chain; baseline-r15, with the chain, PASSes |
| R36W5-8 | 6 | undecided: Task, Assn or Rsc Var2Data bytes of a baseline save differ between VM and host |
| R36W5-9 | 1 | undecided: 114/Props bytes of a baseline save differ between VM and host (a G2 held-out pair) |
| R36W5-10 | 1 | undecided: Task FixedMeta bytes of a level save |
| R36W5-11 | 1 | undecided: 214/CEdl/Var2Data is 64 bytes longer in the VM save |

Missing copy (no saved copy in the guest, V-1): wave36-r7-calendar-add-even, wave36-r7-calendar-add-odd, wave36-r7-calendar-delete, wave36-r7-w37-b-rebaseline-into1, wave36-r8-b-mode-automatic-built, wave36-r8-c-note-summary-255.
