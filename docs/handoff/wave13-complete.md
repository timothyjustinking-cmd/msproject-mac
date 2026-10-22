# Wave 13 complete — the successor's gate

**This file is the wait gate.** Its existence means the wave-13 coordinator
finished: the tree is quiet, every wave-13 stream is merged, and a successor or
auditor may work.

* **Final code commit:** `d60cd77` (pushed). The docs commit that adds this file
  is its child. Wave-13 commits: `15f8ec3` W53b, `87f7659` W54, `1516b5b` W55,
  `dfb5fb0` W56, `41a85e4` app/fuzz wiring, `b35cd64` + `5743a79` W44 guard,
  `ded8503` W57, `d60cd77` fuzz clang fix, plus the docs commits.
* **CI:** check `gh run list` on the docs tip before quoting run ids;
  `app-macos` was green on `41a85e4`/`ded8503`. The two clang/gcc-only fuzz
  warnings were fixed in `d60cd77`.
* **Full local suite:** `build\vs-w13` (clean MSVC `/W4 /WX`) — **47/47 ctest**
  (wave 12: 40). App `build\vs-w13-app` — **12/12 tests, 2,132 checks**
  (`app_layout` 253, `app_selection` 90, `app_document` 43, `app_prefs` 82,
  `app_menu` 744, `app_filter` 60, `app_export` 58, `app_views` 164,
  `app_edit` 120, `app_fields` 199, `app_edits_fields` 160,
  `app_edits_links` 159).
* **M2:** **332/332** (83/83 × 4, 0 unexplained); schedin 20/20 + 10/10;
  `plan_recalc` 291; `cal` 553; reader 685; assignments 679; links 773;
  add_delete 760; field_values 477; formula 1,249; viewstate 113; fuzz 5/5.
* **Project gates:** `w54-resource.mpp` reopens with 2 resources (uid 2 Work
  100 %); `w54-assignment.mpp` with the T1/R54 assignment (100 %, 2,400 min);
  `w54-assignment-deleted.mpp` with none. The app's Task Fields/Predecessors
  build and all 12 tests run under `app-macos` CI.
* **Counters:** 95 agent sessions + 9 coordinator sessions, ~$7.32, ~730M
  tokens, ~$0.077/session. Roadmap **~73%**.

## What wave 13 delivered

| # | Stream | Deliverable |
|---|---|---|
| W53b | exceptions tail | type-5 monthly positional with a non-matching start day: `+76 = position | (item << 8)`, `+78 = period`; both shapes measured |
| W54 | writer resources/assignments | resource row add, assignment add/delete; new rows byte-identical to Project's own; Project gate passed |
| W55 | app field/link editing | Task Fields and Predecessors dialogs, one pending-edit stack, fixed save order, app-macos green |
| W56 | `214/` view state | table/view decoder; **DateFormat=5 at CTable payload +7**; corrected the "added column" premise |
| W57 | adversarial fuzz/ASan | deterministic reader/writer/parser fuzzers, ASan pass, findings report |
| W44b | formula guard | `kMaxFormulaDepth = 64` + AST-height/destructor guard; Release and ASan green |

## The audit finding this wave paid for

**A formula stack overflow (F1).** A 256-deep `IIf`/parenthesis formula crashed
the process; a 4,096-term flat `And` chain overflowed in the **destructor** of
its left-nested AST under ASan. The guard is now 64 with a parser height check,
so deeply nested/very long formulas return a positioned error instead of
crashing. Formula text comes from `114/Props`, so this was reachable from a
corrupt file. No reader, writer or Props-parser bug was found by the fuzzers;
every writer refusal left the file byte-identical.

## What wave 13 did not do (next)

1. **`214/` writing**: the date-format word is known; the true added-column
   layout, `CV_iew` item encoding and `CEdl`/Props semantics are not.
2. `114/Props` formula writing; assignment custom fields; Cost custom fields;
   Flag2–20 bits.
3. MPP12, MPP14 resource Type, elapsed-task mixed cases, summary-calendar
   rollup.
4. App multi-window/printing, manual Mac smoke; LeakSanitizer/UBSan (MSVC ASan
   has neither).

## Traps this wave paid for

* **Dropbox mtime lies.** A test/write changed `tests/data/synthetic-base.mpp`
  (same size, different SHA256) while `git status` still said clean, because
  Git's stat cache matched the preserved mtime/size. Force-restore by deleting
  the file then `git checkout HEAD -- ...`; verify its SHA256
  (`55C109F2…`) at every handoff. `git ls-files -v` and `git diff` do not catch
  this; `git hash-object` vs `HEAD` does.
* ASan binaries need `vcvars64.bat` on PATH for
  `clang_rt.asan_dynamic-x86_64.dll`; run them through the vcvars shell.
* Fuzz/test code is still compiled with `-Werror` on clang/gcc: unused
  `total_ms`, sign-compare. MSVC does not warn.
* `core/mpp/reader.*` stays a single-writer lane; `core/formula` is shared by
  the app (read-only).
