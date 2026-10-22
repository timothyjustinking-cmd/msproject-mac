# Audit handoff — what to attack before a human reviews

This is the brief for an adversarial/debug pass. It is deliberately blunt: the
tree is green and the claims below are measured, but the product has grown fast
and the audit should assume a bug exists in each area until it fails to find one.

## 1. What the product is, and how to build it

A portable C++20 core plus an AppKit shell that reads, recalculates, edits and
writes Microsoft Project `.mpp` files. Windows is the development box (MSVC);
macOS is the target and the only place `app/main.mm` compiles.

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\audit -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\audit --config Release"   # never --parallel
ctest --test-dir build\audit -C Release --output-on-failure

cmd /c "call `"$vc`" >nul && cmake -S app -B build\audit-app -G `"Visual Studio 17 2022`" -A x64 -DAPP_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\audit-app --config Release"
ctest --test-dir build\audit-app -C Release --output-on-failure
```

Expected at the wave-11 gate: **39/39** core ctest and **8/8** app tests
(1,414 app checks). The real corpus (`corpus/*.mpp`) is client data and is
git-ignored; `tests/data/` has committed synthetic fixtures. The two real files
are PROJECT_A (83 tasks) and PROJECT_B (73 tasks, 70 live links / 63 assignments).

## 2. Invariants that must not move

* **M2:** `build\audit\tools\recalc\Release\recalc.exe corpus\project_a.mpp
  --compare corpus\probe\project_a-recalc.mpp` = **332/332** (start/finish/slack/
  critical 83/83), 0 unexplained; schedin 20/20 dates + 10/10 slack.
* **PROJECT_A 83 / PROJECT_B 73 tasks; 88 / 70 live links; 67 / 63 assignments.**
* **CFB round trip byte-identical** for every fixture; `CfbDocument::check()`
  and `check_table()` clean after any write.
* **Only intended bytes move.** A task add may change `TBkndTask` six streams;
  a delete may change `TBkndTask`, and for the cascade `TBkndCons`/`TBkndAssn`
  `FixedMeta` only. `214/` must be byte-identical.
* **No client data** in committed files: no task/calendar/resource name from
  `corpus/` (check any new `docs/analysis/oracle/*` and test output).

## 3. Where to attack (suggested, not exhaustive)

### Reader (`core/mpp`)
- Malformed/truncated CFB: zero-length streams, FixedData not `48+202n`,
  FixedMeta not `16+47n`, VarMeta count vs stream length, VarMeta offsets past
  Var2Data, Var2Data block length overflow, duplicate uids, a row with FixedMeta
  but no VarMeta and vice versa, tombstone `+0` values other than 0/2.
- PROJECT_A's known damage (FixedMeta declares 87 entries but has 88; VarMeta 1,076 vs
  1,080; row 67 no metadata) and PROJECT_B's deleted slots (2 tombstoned links, 10
  tombstoned assignments) must not crash or invent rows.
- Larger inputs: 4,000-task perf test, deep outlines, cycles in Fixed2Data
  parents, link endpoints naming tombstones.
- `Task::custom_fields`/`Document::custom_field`: missing/oversized blocks,
  wrong field kinds, flag bit boundaries, Text with invalid UTF-16.

### Writer (`core/mppwrite`)
- Add: first task, last task, N adds, caller-supplied uid (lower than max),
  duplicate uid, uid 0, empty name; broken table identity (PROJECT_A must refuse, not
  guess); re-read must prove bytes.
- Delete: only task, last task, task with links/assignments (cascade), a task
  whose link endpoint is already a tombstone, deleting twice, deleting uid 0,
  a damaged table; header `+4` bit must be set in every affected table.
- `save_date_edits`: manual row conversion, unlinked row, linked row whose pin
  is before `max(pred finish, pin)` (Project clamps — the app should warn, the
  writer must still store the exact constraint), invalid pack dates, duplicate
  uid, only-intended-bytes proof.
- Custom fields (`field_values`): the three-store contract — VarMeta position
  after Name, both `TBknd*Props` registries, FixedMeta tail bitmap and `+2`
  count. Try writing only one of the three and confirm Project shows 0 (that is
  the known failure mode), then all three and confirm Project shows the value.
  Rewrite, clear text, clear flag, unsupported Cost/Flag2-20 (must refuse).
- CFB growth/shrink boundaries, mini-stream ↔ FAT crossings, file-size changes
  (Project's own save bloats; ours should not corrupt).

### Formula (`core/formula`) and Props (`core/props`)
- Deeply nested `IIf`/parentheses (recursion guard), division by zero, huge
  numbers/durations, negative durations, dates at the epoch edges, unicode in
  strings, `[MSPJ...]` tokens that are not custom fields, unknown functions.
- Props parser: truncated/short stream, record length > stream, non-UTF-16
  formula text, declared counts that disagree with records, the opaque tail.
  The six recovered formulas are the regression set.

### Usage (`core/usage`)
- Zero-duration assignment, assignment with no work, NA start/finish, work that
  cannot be placed on the calendar, cross-year spans, a resource with two
  assignments, empty project, huge spans (bounded?).
- Day/week/month bucket boundaries; Sunday-start weeks are a **decision**, not a
  measured Project rule.

### Calendar (`core/cal`) — wave 12 in flight
- Recurring exceptions: leap day, month-end, a fifth weekday that does not
  exist, yearly/monthly positionals, daily every-N, day-count spans; expansion
  must terminate and not blow memory.
- Holiday/dated exception application on project and per-task calendars.

### App (`app/`)
- Insert/Delete Task + undo/redo + save: unsaved project, Save As, delete a task
  with dependants, save then reload, dirty dot, close prompts.
- Stored custom-field columns and file formula columns are read-only; Save must
  never write them.
- Filter/selection/export interactions across views; PDF determinism.
- `main.mm` is only compiled by `app-macos` CI; the headless tests cover models,
  not AppKit. Treat every new selector/control as suspect until the macOS run.

## 4. Known limitations to state, not hide

1. **Manual scheduling is not preserved on a date edit**: `save_date_edits`
   writes a real SNET pin and clears the manual-mode bit, so the row becomes
   auto-scheduled. Documented, warned per row.
2. **Linked date pins clamp**: Project reopens at `max(pred finish, pin)`; the
   app does not yet warn when an edit cannot take effect.
3. **`214/` is not regenerated**; view state is carried through byte-identically.
4. **Assignment/link creation** is being added in wave 12; a task delete
   cascades over existing rows.
5. **Cost custom fields and Flag2–20** are refused (storage unmeasured);
   field delete is only clear-to-empty.
6. **MPP12 and MPP14 resource Type** are not implemented.
7. **Recurring calendar exceptions** are wave 12; until it lands, only dated
   ranges expand.
8. **App**: save persists start dates, task add/delete and (wave 12) custom-field
   columns are read-only; no multi-window/printing/drag.

## 5. Suggested debug build

An address/UB-sanitized build is cheap on MSVC:

```powershell
cmd /c "call `"$vc`" >nul && cmake -S . -B build\asan -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_BUILD_TYPE=Debug -DCORE_WARNINGS_AS_ERRORS=ON -DCMAKE_CXX_FLAGS=/fsanitize=address"
cmd /c "call `"$vc`" >nul && cmake --build build\asan --config Debug"
ctest --test-dir build\asan -C Debug --output-on-failure
```

Run the reader/writer/formula/props tests under ASan on the synthetic fixtures
and the two real files (read-only). Any finding should come back as: exact
command, fixture, expected vs actual, file/line, and a regression test.

## 6. What “good” looks like

Green gates are necessary, not sufficient. The audit should try to falsify:
**M2**, the **byte-delta claims**, **Project reopening our files**, **client-data
hygiene**, and the **honesty of the docs** (a feature marked verified should have
a command and, for writer behaviour, a Project fixture behind it).
