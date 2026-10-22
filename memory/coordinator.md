# memory — coordinator (wave 13 end, 2026-10-22)

Successor: read `docs/handoff/wave13-complete.md` (the gate), then
`docs/handoff/coordinator.md` "Wave 13 final", then this. The build phase is
complete; `docs/audit-handoff.md` + `docs/audit-findings.md` are the audit brief.

## Where main is

`d60cd77` plus the docs commit, pushed. Wave-13 commits: `15f8ec3` W53b,
`87f7659` W54, `1516b5b` W55, `dfb5fb0` W56, `41a85e4` app/fuzz wiring,
`b35cd64`+`5743a79` W44, `ded8503` W57, `d60cd77` fuzz fix. Check CI on the docs
tip.

## The number

* `build\vs-w13`: **47/47 ctest**; app `build\vs-w13-app`: **12/12**, 2,132
  checks.
* **M2 332/332**; schedin 20/20 + 10/10; plan 291; cal 553; reader 685;
  assignments 679; links 773; add_delete 760; field_values 477; formula 1,249;
  viewstate 113; fuzz 5/5.

## The findings that matter

1. **Assignment add works and Project confirms it**: resource row 172 B / 37 /
   40 / 51 / 5 VarMeta; assignment row 110 B / 34 / 48 / 53 / 4. New rows are
   byte-identical to Project's own `w12-assignadd.mpp`; Project reopens 2
   resources + 1 assignment, and 0 after delete.
2. **App edits everything the writer writes**: Insert/Delete Task, Task Fields
   (six measured custom fields, clear supported), Predecessors (add/delete,
   FF/FS/SF/SS, lags). One pending-edit stack and one save order: link deletes →
   link adds → task deletes → task inserts → field values → start dates; only
   the last writer call writes the target, then reload/adopt.
3. **214 view state**: DateFormat=5 is `214/CTable` block payload **+7**.
   The wave-10 "added Number1 column" oracle call did **not** add a column
   (`Create=False`), so CTable still has 9 columns; the true added-column layout
   is unknown. `114/Props` does not hold table state.
4. **Formula stack overflow found and fixed**: 256-deep input crashed; under ASan
   the *destructor* of a 4,096-term flat `And` AST crashed too. Guard is
   `kMaxFormulaDepth = 64` with a parser AST-height check; positioned error.
   Formula 1,249 Release + ASan.
5. **No reader/writer/Props bug** in 240 reader mutations, 6×40 writer ops, 600
   parser cases, longer ASan campaigns; every refusal leaves the file
   byte-identical. The only finding was the formula recursion.

## Open, ranked

1. **`214/` writing**: true added-column layout (needs a corrected
   `TableEdit(Create=True, OverwriteExisting=True)` fixture), `CV_iew` item
   encoding, `CEdl` semantics.
2. `114/Props` formula writing; assignment custom fields; Cost/Flag2–20.
3. MPP12, MPP14 resource Type, summary-calendar rollup, elapsed mixes.
4. App multi-window/printing, **manual Mac smoke**, LeakSanitizer/UBSan (MSVC
   has neither).

## Traps (do not relearn)

* **Dropbox mtime hides fixture edits from `git status`.** `synthetic-base.mpp`
  was changed with the same size and a preserved mtime; Git's stat cache said
  clean. Force-restore by deleting the file, `git checkout HEAD --`, and check
  its SHA256 `55C109F2…`. Compare `git hash-object` with `HEAD`.
* ASan exes must run under `vcvars64.bat` (ASan runtime DLL), and
  `-DCMAKE_CXX_FLAGS=/fsanitize=address` drops `/EHsc`; use
  `/DWIN32 /D_WINDOWS /W3 /GR /EHsc /fsanitize=address`.
* Fuzz/test code compiles under clang/gcc `-Werror` (unused, sign-compare);
  MSVC misses both.
* Formula text is attacker-controlled from `114/Props`: depth-bound every
  recursion, including AST destruction.
* `core/mpp/reader.*` is a single-writer lane; `core/formula` is shared by the
  app read-only.
* Project recomputes IDs on open; stored IDs are cosmetic.
* Only `app-macos` compiles `main.mm`; keep the ObjC++ delta small and grep call
  sites (two arity/enum errors this wave).
* `plan_recalc_tests` rewrites the golden TSV when corpus is present; only the
  coordinator re-pins (unchanged).

## Counters (wave-13 end)

95 agent sessions (88 + W53b + W54 + W55 + W56 + W57 + two W44 resumes), 9
coordinator sessions; **~$7.32** at ~$0.077/session; ~730M tokens. Roadmap
**~73%**.

## Next three actions

1. Manual Mac smoke test of the v3 app (insert/delete, fields, predecessors,
   undo, save/reopen, filters, export).
2. Corrected table-add fixture → `214/CTable` column write → view persistence.
3. Hand the audit docs to the external adversarial audit; fix findings; human
   review.
