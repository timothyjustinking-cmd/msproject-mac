# memory — coordinator (2026-10-24, wave 24 closed on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave24-complete.md`.  History:
`memory/archive/coordinator-through-wave20-full.md`.

## Where the branches are

`main` = **488481f** (wave-22 docs close).  **`wave23`** and **`wave24`** are
NOT merged and NOT pushed (remote `wave23` = `63cd087`; no `origin/wave24`).
The push is held by the client-name policy decision — see wave24-complete §5
and brief section 0.  The local `wave23-with-screenshot` / `wave23-pre-push`
branches are unpushed backups; **never push them**.  Tim decides whether the
public repo becomes private or its history is rewritten.

## Numbers (wave 24 on `wave24`, tip `cc4ce8a`)

* core ctest **51/51**; M2 **332/332**; plan_recalc **266** no-corpus / **512**
  with corpus (golden read-only, exact `1` opt-in); app **15/15**; app_qt
  **18/18**; reader **1054** with corpus / **377** without; fixtures OK
  (11 documented hashes, 10 tracked `.mpp`).
* Engine **7/8** (`constraint-mso` deliberately left failing; the lane pins the
  failing SET by name; `save_plan` is test-only).
* parity vs the recaptured Project reference: **10/10**, task_bar found and
  equal (116,204,212); colour not-found is now a gate failure.
* client-name scan at the tip: **0 hits**; 0 commit messages quote names.
* CI: **not run — publication hold**.  No push.

## Wave 24 result (what a later session must not re-break)

* **Row vs uid (Critical)**: every shell call site resolves the selection to a
  UID (`PlanDocument::task_by_uid`); never index `plan().tasks` with a RecordSet
  row.  `qt_uid_routing` pins Task Information, Custom Fields save/re-read,
  Delete naming, the row-menu label, print row alignment and Insert Task's
  pending row.
* **File view settings**: `214/Props` `0x03400009` = project-summary row,
  `0x03400008` = Timeline strip ("Time&line"/empty).  Both are FILE state;
  the wave-23 "application state" conclusion was wrong.  Fixtures:
  `wave24-summary-off/on`, `wave24-timeline-hidden`.
* **Flags**: Flag1..20 measured bits (+24/+25/+33/+34) on `Task::flags`;
  Flag10 = SUMMARY; the name `summary_flag` is gone.  `wave24-flags20.mpp`.
* **ID sort**: `wave24-order.mpp` stores a different order from its IDs;
  deleting the adapter sort fails CI.
* **CI/harness**: corpus-free plan_recalc step; `--expect-fail
  constraint-mso`; workflow_run checks head_sha, success only, no tag duplicate;
  per-row Pre/Suc golden; colour-not-found is a failure.
* **Stock style fallback**: a blank Project capture says milestone rows are
  upright, critical rows normal text, task bar teal, critical light blue,
  summary dark teal.  The FILE's own styles are still undecoded (largest gap).

## Next

wave24-complete §3: decode the FILE's text/bar styles (W24-13/18), Custom
Fields definition dialog (W24-14), timescale tiers (W24-17), then the carried
W24-19..25 backlog.  Keep the §2 gates green before each commit; no push until
Tim rules.
