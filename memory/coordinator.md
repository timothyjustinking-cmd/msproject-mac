# memory — coordinator (wave 11 end / wave 12 in flight, 2026-10-22)

Successor: read `docs/handoff/wave11-complete.md` (the gate) first, then
`docs/handoff/coordinator.md` "Wave 11 final", then this. Wave 12 is in flight
(W51 app editing, W52 writer links/clear, W53 calendar exceptions); check
`WAVES.md` and the stream handoffs before touching their areas.

## Where main is

`f7c1279` plus the docs commit, pushed. Wave-11 commits: `8a34c14` W47, `c2acbe4`
W50, `b233f78` oracle, `3f11019` W48+W49, `c4e885e` custom-field oracle,
`f7c1279` crosscheck fix + `w12-exceptions` fixture. CI runs `39748803937`
(ci) / `39748803465` (app-macos) were queued at handoff; check them.

## The number

* Clean `build\vs-w11`: **39/39 ctest**; app `build\vs-w11-app`: **8/8**, 1,414
  checks.
* **M2 332/332** (83/83 × 4); schedin 20/20 + 10/10; plan_recalc 277; reader
  685; add_delete 760; field_values 317; cpm 1,321; props 520.
* Project gates: cascade reopens 2 tasks; **all six custom-field values and the
  rewrite display correctly**.

## The five findings that matter

1. **Custom-field display needs THREE stores.** (a) VarMeta entries inside the
   uid group after Name, field-id order (+ `0x0b4000b7` = `07 00` for Duration);
   (b) 88-byte records in both `TBkndTask/Props` and `TBkndAssn/Props`; (c) the
   FixedMeta entry tail bitmap and `+2` = the uid's VarMeta count (Text1
   +21|0x20, Number1 +25|0x08, Number2 +25|0x10, Duration1 +26|0x01/+27|0x20,
   Date1 +29|0x40, Flag1 +24|0x02). Skipping any one leaves Project showing 0.
2. **Linked pin clamp confirmed:** SNET is no-earlier-than; an earlier pin
   reopens at `max(pred finish, pin)` with the constraint stored exactly.
3. **Task-delete cascade:** tombstone the task, its `TBkndCons` rows (either
   endpoint) and its `TBkndAssn` rows, each table's FixedMeta header +4 |= 2.
   No stream shrinks. Project's own post file is reproduced.
4. **Tombstone liveness** now applies to links/assignments too: PROJECT_B reads 70
   links / 63 assignments (0 dead endpoints), matching Project's export; PROJECT_A
   unchanged 88/67.
5. **Reader/engine gains:** resource Type/MaxUnits from metadata bits, per-task
   calendars in CPM (`holiday1` proven), elapsed lag/duration (T8 exact,
   DurationFormat u16 at +164), all M2-neutral.

## Open, ranked

1. App Insert/Delete Task, stored custom-field columns, file formulas (W51, in
   flight).
2. Writer link add/delete + custom-field clear (W52, in flight); assignment add
   needs an oracle fixture.
3. Recurring calendar exception parameters (W53, in flight; fixture
   `corpus/probe/w12-exceptions.mpp` + `docs/analysis/oracle/w12-exceptions.json`).
4. `214/CTable`/`214/CV_iew` view state, MPP12, elapsed-task mixes, MPP14
   resource Type, custom-field delete/Cost storage.
5. Manual Mac smoke test; adversarial audit.

## Traps (do not relearn)

* **Three custom-field stores** above — a VarMeta-only write silently shows 0.
* **Project recomputes task IDs on open**; stored IDs are cosmetic.
* `core/mpp/reader.*` is shared; only one stream may edit it at a time (a
  half-applied reader change segfaulted W49's tests). Coordinate via the
  coordinator.
* A liveness change moves test pins: `io_mspdi_crosscheck` moved from PROJECT_B
  72/73 to 70/63.
* **Only `app-macos` compiles `main.mm`.** Two clang-only failures in wave 10
  (unused static, call arity). Keep the ObjC++ delta small; grep call sites.
* `FileSaveAs(Name="x.xml", FormatID="MSProject.xml")` can silently write a
  binary `.mpp`; check first bytes.
* `dir()` on pywin32 COM objects raises "Not implemented" on this build; probe a
  static list.
* `plan_recalc_tests` rewrites `tests/data/project_a_recalc_golden.tsv` whenever corpus
  is present; only the coordinator re-pins (it has been unchanged).
* Dropbox mtime churn: re-read immediately before every edit.
* `git push` works with danger-full-access (the sandbox credential failure only
  appears under the restricted mode).

## Counters (wave-11 end)

85 agent sessions (78 + W47 + W48×4 + W49 + W50), 7 coordinator sessions;
**~$6.55** at ~$0.077/session; ~655M tokens. Roadmap **~67%**.

## Next three actions

1. Finish wave 12, run its Project gates (link add fixture; exception fixture)
   and the full clean suite.
2. Wire/polish the app editing + stored fields, then a manual-Mac smoke test.
3. Build the audit package: ASan/debug pass, `docs/audit-handoff.md`, clean tree.
