# Wave 11 complete — the successor's gate

**This file is the wait gate.** Its existence means the wave-11 coordinator
finished: the tree is quiet (wave 12 had not started when this was written), every
wave-11 stream is merged, and a successor may work.

* **Final code commit:** `f7c1279` (pushed). The docs commit that adds this file
  is its child. Wave-11 commits: `8a34c14` W47, `c2acbe4` W50, `b233f78`
  wave-11 oracle, `3f11019` W48+W49 core, `c4e885e` custom-field oracle,
  `f7c1279` crosscheck fix + wave-12 exception fixture.
* **CI:** the first three-platform runs on `f7c1279` were red on clang/gcc-only
  checks (an unused test helper, then a backslash-continued `//` comment). Both
  were fixed in `b71e2a3` / `1639d80`, which are **green on all three legs**;
  `app-macos` was green throughout. Check `gh run list` before quoting any id.
* **Full local suite:** `build\vs-w11` (clean MSVC `/W4 /WX`) — **39/39 ctest
  tests passed** (wave 10: 36). New: `props`, `mpp_field_value`,
  `mppwrite_roundtrip_field_values`.
* **App:** `build\vs-w11-app` — **8/8 tests**, 1,414 checks (`app_layout` 253,
  `app_selection` 90, `app_document` 43, `app_prefs` 82, `app_menu` 664,
  `app_filter` 60, `app_export` 58, `app_views` 164).
* **M2:** **332/332** (83/83 × 4, 0 unexplained); golden unchanged; schedin
  20/20 dates, 10/10 slack; `plan_recalc` 277 checks.
* **Project gates (coordinator, read-only opens):**
  * `w48-del-cascade.mpp` reopens as 2 tasks (uids 1, 3), no link/assignment to
    the deleted uid;
  * `w48-fields.mpp` reopens with uid 1 **Text1 "W48 alpha", Number1 42.5,
    Number2 3.25, Duration1 "1 day", Date1 "Mon 26/10/26", Flag1 Yes**;
  * `w48-fields-rewrite.mpp` reopens with **"beta", -7.25, 3.25, "2 days",
    "Mon 2/11/26", No**.
* **Counters:** 85 agent sessions + 7 coordinator sessions, ~$6.55, ~655M
  tokens, ~$0.077/session. Roadmap **~67%** (wave 10: 60%).

## What wave 11 delivered

| # | Stream | Deliverable |
|---|---|---|
| W47 | app views | Resource Sheet, Task Usage, Resource Usage (day/week/month), computed formula columns, view/period/column preferences; app 8/8 |
| W48 | writer cascade + fields | task-delete cascade tombstones dependent links/assignments; custom-field read+write (Text/Number/Date/Duration/Flag1) **Project-verified**; linked-pin clamp contract |
| W49 | reader/engine parity | resource Type/MaxUnits (metadata bits), per-task calendars in CPM, elapsed lag/duration, link/assignment tombstone liveness |
| W50 | `114/Props` | record/tail framing, all six measured formulas recovered, `114/Props` table-state correction |

## The three-gate custom-field contract (carry forward)

Writing a VarMeta value is not enough for Project to show it. All three are
required, and all are measured (`docs/analysis/oracle/w11-oracle.md` §4):
1. VarMeta entries inside the uid's contiguous group, **after Name**, sorted by
   field id; Duration also adds `0x0b4000b7` = `07 00` after the overtime fields.
2. 88-byte records in **both** `114/TBkndTask/Props` and `114/TBkndAssn/Props`
   (preamble count/byte words included).
3. FixedMeta entry tail bitmap + `+2` = the uid's VarMeta count
   (Text1 +21|0x20, Number1 +25|0x08, Number2 +25|0x10, Duration1
   +26|0x01/+27|0x20, Date1 +29|0x40, Flag1 +24|0x02).

## What wave 11 did not do (wave 12 and later)

1. **App editing**: Insert/Delete Task, stored custom-field columns in the app,
   formula columns from the file (wave 12, W51).
2. **Writer**: link/assignment row create/delete, custom-field clear/delete
   (wave 12, W52); assignment add still needs an oracle fixture.
3. **Recurring calendar exception parameters** (fixture `w12-exceptions.mpp`
   built; wave 12, W53).
4. `214/CTable`/`214/CV_iew` view-state decoding, MPP12, elapsed-task mixed with
   link/constraint, resource Type on MPP14.

## Traps this wave paid for

* **Custom fields need three stores** (VarMeta position + both Props registries
  + FixedMeta tail bits). Each was invisible in isolation; the third gate was
  found only by having Project write the same values on the same base fixture.
* **Project recomputes IDs on open**; a stored ID renumber is cosmetic.
* **Only `app-macos` compiles `main.mm`**; keep the ObjC++ delta small and grep
  call sites when a signature changes.
* **A reader-liveness change moves test pins**: the `io_mspdi_crosscheck`
  expectations for PROJECT_B's deleted-slot links/assignments had to follow the
  tombstone fix (70 links / 63 assignments, zero dead endpoints).
