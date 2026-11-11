# memory: coordinator (11 November 2026, wave 36 G0 done; waves 34 to 35g merged)

**Entry point:** `docs/START-HERE.md`.  Current brief `docs/handoff/wave36-brief.md`, record
`docs/handoff/wave36-complete.md`.  The wave 34 to 35g history, with the old wave states of this
file, is `docs/handoff/history-waves34-35.md`.  Older: `memory/archive/coordinator-through-wave20-full.md`.

## State

* `main` = `b2c0b19` (wave 35g merged; CI green on all seven jobs; 0 Critical, 0 High; 4(e) 7
  checked, 3 not checked).  Waves 23 to 32 were merged on 27 October 2026 as `17f5782` after an
  operator-approved `git filter-branch`: every id in the wave 23 to 32 records is pre-rewrite; map
  it through `docs/handoff/wave32-verification/rewrite-sha-map.txt` (left old, right current).
* `wave36` is cut from `main` at `b2c0b19`, local only.  G0, G1, G2 and CLOSE run; **G3 is not
  started** (no held-out insert pair in `trace-insert-rules-r6.md`; the coordinator re-checks
  before CLOSE).  Wave 37: Notes, Set Baseline, linked, lag and constraint Calculate.
* **G0 (the trace gate and the records), 11 November 2026:** W36-0.1 the noise allow-list is in
  `tools/trace/trace_gate.py` as exact windows and per-stream ceilings, no by-name waiver, exit 0
  is PASS and `runone6.ps1` records it; 0.2 no wrong-length mask, fixed-stride streams masked per
  record; 0.3 an absent null is a FAIL "no null"; 0.4 ctest `trace_gate_guards`
  (`tools/trace/test_trace_gate.py`, flips one byte in every stream of a committed pair); 0.5
  `r5b-skip.ps1` invokes only in a "Sign in" dialog and `kvp/host.ps1` `vmx` throws on a MISMATCH or
  a wrong reply length (ctest `trace_host_scripts`, needs `pwsh`); 0.6 the records.
* **Watch:** the gate's windows are tuned to the null run and the 99 committed control pairs, so
  the coordinator's re-gate of the 109 recordings may FAIL on same-machine noise: that is a
  finding by design, not a gate fault.  CI has no `olefile`, so `trace_gate_guards` SKIPs there.
* **Open, no Critical or High:** Lows R35gG0-1 to -4, R35gG1-1 to -5, R35gW-1 to -5; partly fixed
  or open R35dG2-1, R35cG0-2, R35cG0-4 (wave 36 G2), R35bW-1, R35bW-2, R35bG2-2, R35bG3-1,
  R35bG0-9, R35bG1-10, R35bG2-9.  Only the coordinator's review closes an id.
* **Engine stays 7/8** (`constraint-mso` expected-failing); `mppwrite::save_plan` is test-only.

## What a later session must not re-break

* **Wave 32's refusals stand**: the automatic-outline cross-refusal both orders, Start read-only
  at every shell entry, sorted and filtered range operations refused.
* **Wave 33 G0**: every `.mpp` write goes through `mppwrite::write_file_atomically` (write
  beside, re-read, self-check, rename); a failure leaves the target byte-identical.
* **Wave 33 G1**: Delete Task refuses a summary row; Insert Task refuses a clone source with
  progress, actuals, work, a task calendar, a non-ASAP constraint or a real assignment; formula SET
  accepts only Text1..30 and Number1..20 and CLEAR is disabled; FAT-slack files are read-only at
  open; a second pending formula is refused.
* **Wave 33 G2 and G3**: both container parsers bound DIFAT and Fixed2Meta walks; CFB v4 is
  refused by design; the `app/` suite runs in CI on both OSs; a missing committed fixture fails
  with its path (only `corpus/` may skip); the save-pair mask is exactly the noisy bytes.
* **Wave 33 CLOSE**: `scripts/check_markers.py` has nine markers; the ninth (the short first name)
  is matched on ASCII word boundaries: do not add it as a bare substring.
* **Wave 33b**: a `214/CTable` date-format request for a table index other than 0 is refused at
  `Document::request_view_state_edits` and again in `apply_view_state_edit`; `apply_formula_edit`
  refuses `Operation::clear` before reading `114/Props`; the macOS Task Fields handler commits a
  trial copy only on full acceptance.  CI fixes: no `/*` inside a block comment, no orphaned test
  helpers, no set-but-unused counters; MSVC does not warn, CI is the gate.
* **Waves 34 to 35g**: Assign Resources and its removal refuse every shape but the measured pairs;
  Calculate Project, Sort and Show Level are measured for their shapes only; add every new
  `MenuAction` to `app_qt/command_status.h` (MSVC hides `-Wswitch`).  Details in the history file.

## Privacy and the push

* `python -I build/audit-w22/guard_names.py main..HEAD` exits 0 and
  `python scripts/check_markers.py --range main..HEAD` is 0 hits.  Run BOTH name scans and the
  marker check before every commit.
* Residuals the operator owns: the unsalted name hashes in older public history plus
  `tests/data/project_a_golden.tsv`, `6056f15`'s commit message, the pre-scrub public history and
  the local `wave33-pre-replay` backup branch.
* Push, both CI dispatches (`qt.yml`, `ci.yml`; `app-macos.yml` runs on push), the mutations and the
  independent reviews are the coordinator's, never the builder's.

## Gotchas

* The shell hook blocks backslashes in heredocs and inline `python -c`: Write a script file and
  run it by path.  `python -I` drops `olefile` (user site-packages): run the trace tests without it.
* Never touch the trace VM, `vmconnect` or `build/trace-2021/`; out-of-memory compiler errors
  (C1060, C1076, C3859, heap space) are rerun once and logged.
