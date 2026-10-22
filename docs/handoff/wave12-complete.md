# Wave 12 complete — the successor's gate

**This file is the wait gate.** Its existence means the wave-12 coordinator
finished: the tree is quiet, every wave-12 stream is merged, and a successor or
auditor may work.

* **Final code commit:** `6aa2594` (pushed). The docs commit that adds this file
  is its child. Wave-12 commits: `a5a05f8` W52+W53, `6498a07` W51, `6aa2594` the
  `main.mm` signature fix (clang-only; MSVC never parses that file).
* **CI:** `app-macos` run `39753281031` is **green** on `6aa2594`; the
  three-platform `ci` run `39753281715` is the check on the docs tip — confirm
  with `gh run list` before quoting it. The wave-11 CI debt was paid at
  `1639d80` (green all legs; two clang/gcc-only fixes).
* **Full local suite:** `build\vs-w12` (clean MSVC `/W4 /WX`) — **40/40 ctest
  tests passed**. App `build\vs-w12-app` — **10/10 tests, 1,765 checks**
  (`app_layout` 253, `app_selection` 90, `app_document` 43, `app_prefs` 82,
  `app_menu` 696, `app_filter` 60, `app_export` 58, `app_views` 164,
  `app_edit` 120, `app_fields` 199).
* **M2:** **332/332** (83/83 × 4, 0 unexplained); schedin 20/20 + 10/10;
  `plan_recalc` 291; `cal` 509; reader 685; links 773; add_delete 760;
  field_values 477; cpm 1,321; props 520.
* **Project gates:** `w52-links.mpp` reopens with two links (1→2 FS+0,
  1→3 SS+1d); `w52-links-deleted.mpp` with one; `w52-fields-cleared.mpp` with
  all six fields blank/0/NA/No and still registered.
* **Counters:** 88 agent sessions + 8 coordinator sessions, ~$6.78, ~680M
  tokens, ~$0.077/session. Roadmap **~70%** (wave 11: 67%).

## What wave 12 delivered

| # | Stream | Deliverable |
|---|---|---|
| W51 | app editing | Insert/Delete Task with pending-edit undo/redo and one Save (writer then reload), stored custom-field columns, formula columns from the file's `114/Props` |
| W52 | writer links + clear | `add_task_link`/`delete_task_link` over all four Cons streams; `clear_field_value` |
| W53 | recurring exceptions | parameter words at record +74..+86 decoded for all types and expanded; per-task calendar proof |

## What wave 12 did not do (next)

1. **Assignment row add/delete** — the last structural writer gap; task-delete
   cascade already tombstones assignment rows. Needs the Project add-assignment
   oracle (resource row + assignment row + VarMeta/Props).
2. App UI for **writing custom fields** and editing links/assignments (the APIs
   exist; the columns are read-only).
3. `214/CTable`/`214/CV_iew` view state, MPP12, MPP14 resource Type, writing
   `114/Props` formulas.
4. **Manual Mac smoke test** and the adversarial/ASan audit
   (`docs/audit-handoff.md` is the brief).

## Traps this wave paid for

* An agent wrote to the committed `tests/data/synthetic-base.mpp` (size changed
  237,568 → 238,080). The coordinator restored it before committing; tests must
  copy the fixture, never modify it. Check `git status tests/data` at handoff.
* The first top-level configure failed because one stream's CMakeLists named a
  test file another stream had not written yet; a retry after it landed is the
  fix, never editing the other lane.
* `core/mpp/reader.*` is shared between streams; a half-applied change produced
  segfaults in a concurrent stream's build until it settled.
* CI remains the only compiler that catches clang/gcc-only mistakes: this wave's
  wave-11 follow-ups were an unused test helper and a backslash-continued `//`
  comment. Run both a GCC-like warning review and `app-macos` before claiming
  green.
