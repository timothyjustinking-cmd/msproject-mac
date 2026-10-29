# Wave 18 complete — the successor's gate

**This file is the wait gate.** Its existence means the wave-18 coordinator
finished: every wave-18 stream is merged, the tree is green, and a successor may
work.

* **Final commit:** `e1f54d8` on `main` (this file's commit is its child).
* **Repo:** PUBLIC, `https://github.com/<owner>/msproject-mac`.
  CI runs only on `workflow_dispatch` or a `v*` tag — public minutes are free,
  but a run stays a deliberate act.
* **Gates:** `ctest --test-dir build\vs-w18 -C Release -E plan_recalc` = **50/50**
  (run the full suite only serially, never from a stream: `plan_recalc` rewrites
  `tests/data/project_a_recalc_golden.tsv` and concurrent runs truncate it).
  M2 **332/332** (268/268 leaves, 64/64 summaries). Fixtures verified by
  `python scripts/check_fixtures.py` (`55C109F2…6069`).
* **Scores:** UI parity **9/9 measures pass** at 1920x1032; engine parity
  **5/7** scenarios field-exact, 1 recorded-diff, 1 unrecorded.
* **Counters:** ~105 agent sessions, ~$8.1, ~800M tokens.

## What wave 18 delivered

| stream | result |
|---|---|
| Q3 | Qt shell at MS Project's measured 1920x1032: ribbon/bar labels sized by font, frozen gutter, `--parity` render, 9 columns `[38,57,147,81,81,81,105,105,124]`, blank Indicators gate |
| E2 | manual-scheduling semantics (two CPM passes, boundary rule), **free-slack writer defect fixed** (+74 held total slack), applier mapping for the two date pairs, placeholder tombstone |
| F2 | `214/CEdl` corpus "trailer" is a cumulative Props cache (10 docs) — parses and round-trips; CFilter criteria/string table; CGrouping payloads; `viewstate_report --diff` |
| G1 | **MPP12**: `112/`/`212/` tree, 264 B task row (uid/ID reversed, stored parent/level), 188 B resource, 142 B assignment (no Resume), swapped link lag, calendar columns (uid, base, resource); master/subprojects; password flag; 958 checks |

## What wave 18 did not do (next)

1. **`core/cal` still hard-codes `114/` and the MPP14 calendar column order.**
   MPP12 calendar rows are `(uid, base, resource)` at `+0/+4/+8`; MPP14 is
   `(base, resource, uid)`. `core/mpp` now exposes `Document::version`,
   `Document::table_root` and `Document::calendars` — `core/cal` only has to
   consume them. **This is the first job of wave 19.**
2. **Engine parity 5/7**: `constraint-mso` (1 field; the coordinator's probe shows
   Project leaves manual durations unchanged under a repeated MSO, so the fresh
   oracle needs re-examination, not an engine change) and `assignment-50pct`
   (7 fields; the placeholder tombstone matched the earlier oracle).
3. **Qt cannot edit, save, print, export, filter or switch views**, and there are
   no dialogs. No Windows or macOS Qt job exists either.
4. **Calendar family**: needs a fixture with a second **named** base calendar
   (`Project.Calendars`/`BaseCalendars` do not exist in this build; `Task.Calendar`
   is a name), which also unblocks `task-calendar` and every calendar scenario.
5. MPP12 password files (write-reservation and read) to validate the `0x02` path
   and the XOR mask against Project.

## Traps this wave paid for

* **MS Project rewrites a file it opens.** The engine recorder opened the
  committed fixture in place and changed its hash mid-run; recording now copies
  the base into `build/` first. The fixture has been resurrected by Dropbox three
  times — `check_fixtures.py` is the only reliable detector, and
  `git hash-object` vs `HEAD` the only reliable check.
* **Concurrency costs**: several streams running the full `ctest` truncated the
  recalc golden; streams must use `-E plan_recalc` or their own targets, never
  write under `tests/data/`, and keep shared files compiling after every save.
* **`file.*` stream sizes are excluded from engine scoring** — Project's save
  churn is not semantics (it pads and compacts on close).
* A tail doc edit stripped every newline from `docs/engine-parity.md`; it was
  restored from git. Edit docs with a tool that preserves line endings.
