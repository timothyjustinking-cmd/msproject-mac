# W36-C.1 to C.3: the G0f review's wrong files against the fixed gate

Coordinator, Sun 22 Nov 2026.  Script `c123-check.ps1`, log `c123-check.log`.  Files: the G0f review's
`D:\dev\msproject-mac-wt-revg0f\build\rev\files`.  Before: the gate at `c7ea56f` (the G0f marker).  After: the gate at
`79109bb` (C.3), in the worktree `D:\dev\msproject-mac-wt-close`.

| item | wrong file | before | after |
|---|---|---|---|
| C.1 | zoom-custom, unit word changed | exit 0 | exit 1, 1 problem |
| C.1 | zoom-entire, unit word changed | exit 0 | exit 1, 1 problem |
| C.1 | zoom-reset, unit word changed | exit 0 | exit 1, 1 problem |
| C.1 | zoom-custom, window dates +1176/+1182 moved 30 days | exit 0 | **exit 0** |
| C.2 | wave30-calculate, existing row's stamp changed | exit 0 | exit 1, 1 problem |
| C.3 | wave28-mark-auto, CEdl rows kind 1 to 0x99 | exit 0 | exit 1, 1 problem |

The unmodified references of the three stems still pass against themselves at `79109bb` (exit 0, 0 problems).

**Residual, for the whole-wave review (R36W-C1, Medium):** the zoom window's date words stay masked as any in-span
date under their W36-0f.4.2 evidence (they move on null saves: 20, 5 and 15 of 191 pairs at +1176, +1182, +1186).
`de97271` says so and the brief's C.1 allows it.  So a save with the right zoom unit and a wrong window passes.
The fix is to compare the window when the unit is data, by the evidence of the zoom pairs.
