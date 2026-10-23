# Wave 26 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-26-<group>-COMPLETE` line is appended below (wave-26-brief section 1 rule
2).  Deferrals need the attempt evidence wave-26-brief section 1 rule 4 names.

## G0 - wave-25 verifier findings (Critical/High only)

The independent review of `wave24..wave25` found 0 Critical and 1 High; its
Mediums and Lows are queued for after this wave and are not G0's work.

| item | finding | status | evidence |
|---|---|---|---|
| W26-0.1 | High: macOS Print / Print-to-PDF drops the last task when the project row is shown (`app/main.mm` built the print row list from `plan.view.tasks.size()`, but those are RecordSet rows) | **DONE** | commit `28a9f9b`; gates `build/w26/gates-a5dff41-W26-0.1.log` |

W26-0.1 detail:

- The one row-list decision now lives in the shared layer:
  `app::print_row_list()` (`app/page_layout.h/cpp`) returns the filter's rows
  when a filter is active (an empty zero-match list prints nothing) and every
  RecordSet row otherwise.  `make_print_request()` in `app/main.mm` uses it for
  both the Gantt and sheet print requests, and `app_qt::print_rows()` delegates
  to it, so the Qt and macOS paths cannot drift.  The task count is still the
  subtitle's "N of N" numerator.
- Falsifying test in the headless `app_layout` ctest: on
  `tests/data/wave24-order.mpp` (project row shown, five tasks) the print row
  list is six rows, ends at the last RecordSet row and carries every task uid.
  Under the old `0 .. tasks.size()-1` arithmetic it is five rows and the
  highest-ID task is missing; the assertions fail.
- The review's Medium 3 (the smoke half of W25-1's acceptance) is closed by
  the same commit: `app::smoke_row_shown()` runs the row-addressed sequences on
  `wave24-order.mpp` (load identity, the row's sheet cell, the uid-0 edit
  guard, the Gantt source by uid, the print row list and a printed page,
  matching and zero-match filters, a core/render export).  `app_smoke` runs it
  on this machine; `app/main.mm`'s AppKit half now also loads and prints the
  row-shown copy through the real paths and no longer asserts `bars == tasks`.
- macOS `main.mm` is not compiled on this machine (no toolchain).  The shared
  function and the portable sequence are the tested half; the AppKit additions
  are written for CI, not claimed as run here.

Gates on the commit's tree (all numbers from
`build/w26/gates-a5dff41-W26-0.1.log`, parent `a5dff41`): core ctest
`-E plan_recalc` 51/51; `plan_recalc` 512 checks with corpus / 266 with
`MPP_NO_CORPUS=1`; M2 `recalc --compare` 332/332 (268 leaves, 64 summaries);
reader 1100 with corpus / 420 without; app ctest 15/15 (`app_layout` 279
checks, `app_smoke` 121); app_qt ctest 18/18; `check_fixtures.py` OK (23
documented hashes, 21 tracked `.mpp`) and oracle provenance OK (8); parity
wave23-outline 10 comparable / 0 failing and wave25-auto `colour.task_bar`
1 / 0; client-name scan 0 hits on the parent, on the working-tree content and
on `28a9f9b` including its commit message.

## Found, not fixed

Nothing new in G0.  The wave-25 review's Medium/Low list stays queued for after
this wave, as the brief says.

WAVE-26-G0-COMPLETE
