# Wave 35f: completion record

## G0: the Mediums

Builder record: `wave35f-verification/gates-g0.md`. Commits: `fd457b4` (0.1), `08b83f9` (0.2),
`1a2436e` (0.3), `fdff5d8` (0.4). The trailer is on each, checked by script.

| Item | Finding | Status |
|---|---|---|
| 0.1 | R35eG1-1 | fixed `fd457b4`; only the QUEUE of a Level under a shown session Sort is refused, a Level that could never queue is shown for the session as in wave 31; tested in `qt_outline_persist` |
| 0.2 | R35eG2-1 | fixed `08b83f9`; Project > Custom Fields... refuses a formula beside a queued Calculate before its dialog; a `custom_fields` case in `qt_calculate_gate` |
| 0.3 | R35eG1-3 | fixed `1a2436e`; the five-task Undo-Sort-then-Level test in `qt_sort_persist`; the 1.1 records corrected; the wrong "Found, not fixed" item withdrawn |
| 0.4 | R35eG1-2, with R35eG2-9 and R35eG1-5 | fixed `fdff5d8` (records): the open queue, the four partly fixed 35b ids with their remainders, R35cW-3's one status, R35cW-4's remainder naming R35dW-5 |

**Gate line** (Visual Studio 17 2022, Release, `CORE_WARNINGS_AS_ERRORS=ON`; 0.1
`d884b57-W35f-0.1-*`, the group's final tree `fdff5d8-W35f-G0-record-*`): core build 0 with 0
warnings; `ctest -E plan_recalc` 65/65; app 34/34; app_qt 52/52; `check_fixtures` 0;
`check_markers --range main..HEAD` 0; `guard_names` 0; `scan_client_names` 0; client-string grep
none. 0.4 is records only (scans, fixtures and `git diff --check`, 0). The measured pairs still
write and match with effects on.

**Guards seen red:**
- 0.1: the queue-branch `sort_active()` check, 3 FAILs in `qt_outline_persist`
  (`build/w35f/0.1-red-run.log`).
- 0.2: the `show_custom_fields` guard, 2 FAILs in `qt_calculate_gate` (`0.2-red-run.log`).
- 0.3: the queue-branch check mutated so it cannot fire, 4 FAILs in the new `qt_sort_persist`
  block and 3 knock-on FAILs (`0.3-red-run.log`).

**Deferred:** nothing.

**Found, not fixed:**
- Under a shown Sort a Level the builder would accept now changes the display for the session
  while File > Save keeps the file's outline, as every session-only Level does; the note says so
  (`gates-g0.md`).

No writer added, no refusal of a persisted shape lifted, no pair measured, no COM, no VM or trace
touched, nothing pushed, no workflow dispatched.

WAVE-35f-G0-COMPLETE
