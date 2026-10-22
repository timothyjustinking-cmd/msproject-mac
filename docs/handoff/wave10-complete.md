# Wave 10 complete — the successor's gate

**This file is the wait gate.** Its existence means the wave-10 coordinator
finished: the tree is quiet, every stream is merged, and a successor may work.

* **Final code commit:** `0e3fbd7` (pushed). The docs commit that adds this file
  is its child. Wave-10 commits: `5fdec56` oracle tooling + evidence, `a043c7d`
  W43, `3bb2b2b` W44, `27f7e90` W46, `fd19ba6` W45, `54163e3` + `0e3fbd7` the
  two `app-macos` fixes.
* **CI:** `ci` run `39739104639` **green on all three legs**
  (`windows-msvc`, `macos-appleclang`, `ubuntu-gcc`) at `0e3fbd7`; `app-macos`
  run `39739104632` **green** at `0e3fbd7` (the ObjC++ shell compiles and the
  seven headless app tests run on macOS).
* **Full local suite:** `build\vs-w10` (clean MSVC `/W4 /WX`) — **36/36 ctest
  tests passed**, ~38 s. New tests this wave: `resource_view`, `usage`,
  `formula`, `mpp_tombstone`, `mppwrite_roundtrip_add_delete`.
* **App:** `build\vs-w10-app` — **7/7 tests**, 1,043 checks (`app_layout` 253,
  `app_selection` 90, `app_document` 43, `app_prefs` 82, `app_menu` 457,
  `app_filter` 60, `app_export` 58).
* **M2:** **332/332** — start 83/83, finish 83/83, slack 83/83, critical 83/83,
  0 unexplained; golden `tests/data/project_a_recalc_golden.tsv` unchanged.
  schedin 20/20 dates, 10/10 slack; `plan_recalc` 231 checks.
* **Project gate for W45:** the coordinator opened every emitted add/delete
  fixture read-only. Adds reopen as **4 / 5 / 4** tasks (new task present,
  priority preserved); deletes reopen as **2 / 2 / 3 / 4 / 4** (tombstoned row
  gone, surviving uids/IDs as expected).
* **Counters:** 78 agent sessions + 6 coordinator sessions, ~$6.00, ~605M
  tokens, ~$0.077/session. Roadmap **~60%** (wave 9 end: 53%).

## What wave 10 delivered

| # | Stream | Deliverable |
|---|---|---|
| W43 | resource + usage views | `core/usage` day/week/month buckets, task/resource/assignment projections, calendar-aware flat spread; `core/view/resource_tables` (nine RT_RCDATA 311 tables, 86 columns) |
| W44 | custom fields + formulas | `core/formula` tokenizer/parser/evaluator, `IIf`/comparisons/date+duration arithmetic/`&`, 100-field registry, measured storage encodings, computed columns through one `value_of_field` |
| W45 | writer add/delete | `add_task_row` / `delete_task_row`, all-or-nothing and re-read from disk; Task tombstone liveness fix in `core/mpp/reader.cpp` |
| W46 | app shell v3 | menus, preferences, filter dialog, Export PDF via `core/render`, dirty-state save prompts, clipboard, keyboard navigation; `main.mm` v3 |

Oracle evidence: `docs/analysis/oracle/w10-oracle.md` + `w10-{adddelete,
formulas, usage, prefs, fieldvalues, flagdiff}.json`; tools
`scripts/oracle_w10.py`, `scripts/mpp_diff.py`.

## The one measurement to carry forward

**`FixedMeta` header word +4 bit 0x02 is the tombstone gate.** Entry `+0=0x02`
/ `+2=0` alone is ignored by Project; Project's own saves also change +4 from 4
to 6. W45's first delete fixtures omitted it and Project reopened the deleted
task; after the fix all five delete fixtures are honoured. PROJECT_B's real file
carries `+4 = 6`, so this is not fixture-only behaviour.

## What wave 10 did not do (carry into wave 11)

1. **No wiring.** `core/usage` and `core/formula` are models: the app shows no
   resource/usage view and no computed custom-field column, and the writer does
   not persist custom-field values.
2. **Writer tail:** exact-date writes on a linked row (`w37-snet-link.mpp` is
   still unbuilt), add/delete for links and assignments, `214/` still not
   regenerated (deliberate, spec §10.4), PROJECT_A structurally refused because its
   `FixedMeta` size identity is broken.
3. **`114/Props` decoding** — formula text and table definitions live there; it
   is the gate for custom fields round-tripping and view settings persisting.
4. Recurring calendar exception parameters, per-task calendars in CPM, MPP12,
   elapsed-DURATION format, resource Type exposure (the W40 fixture exists).
5. **Manual Mac smoke test** of the v3 shell (menus, filter, clipboard, zoom,
   shading, preferences, export, dirty prompts); `main.mm` is proven by
   compilation + headless models, not by a human.

## Two CI traps this wave paid for

Both are invisible to MSVC and only `app-macos` (clang) catches them:

* **Unused static helper in an app test** — `-Wunused-function -Werror`.
* **Call/declaration arity in `main.mm`** — `draw_gantt` gained a parameter and
  one call site was missed; clang says "no matching function". When a `main.mm`
  signature changes, grep every call site before pushing.

If a push is red, `gh run view <id> --log-failed` names the file and line in one
shot; fix, push, re-check, and only then update any document that claims green.
