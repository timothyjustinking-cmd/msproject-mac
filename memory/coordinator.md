# memory — coordinator (wave 10 end, 2026-10-22)

Successor: read `docs/handoff/wave10-complete.md` (the gate) first, then
`docs/handoff/coordinator.md` "Wave 10 final", then this. Every claim below has a
re-runnable command in the handoff or in the stream's `docs/handoff/w*.md`.

## Where main is

`0e3fbd7` plus the docs commit after it, pushed. Wave-10 commits: `5fdec56`
oracle, `a043c7d` W43, `3bb2b2b` W44, `27f7e90` W46, `fd19ba6` W45, `54163e3` +
`0e3fbd7` the two `app-macos` fixes. `app-macos` is green on `0e3fbd7`; check
`gh run list` for the three-platform `ci` run on the docs tip before saying green.

## The number

* Clean MSVC `/W4 /WX` `build\vs-w10`: **36/36 ctest** (was 31).
* App `build\vs-w10-app`: **7/7**, 1,043 checks.
* **M2 332/332** (83/83 × 4), schedin 20/20 + 10/10, plan_recalc 231.

## The five findings that matter

1. **Row add/delete works and Project confirms it.** `add_task_row` appends
   +202/+47/+64/+96/+96 B and eight VarMeta entries; `delete_task_row` tombstones
   (entry `+0=2`, `+2=0`) **and sets `FixedMeta` header word +4 bit 0x02 (4→6)**.
   The header bit is the gate: without it Project reopens the dead row as live.
   Coordinator opened all nine fixtures: add 4/5/4, delete 2/2/3/4/4.
   Design + limits: `core/mppwrite/add_delete.h`, spec §11.
2. **Tombstone liveness reader fix.** A Task row is live iff its FixedMeta entry
   `+0 == 0` and it has a VarMeta group. PROJECT_A 83 / PROJECT_B 73 unchanged; the old
   rule read 6 where Project reads 5 on an add-then-delete file.
3. **Custom-field storage is measured; formula *evaluation* is not.** Text
   UTF-16, Number f64, Duration u32 ticks, Date packed u32; **Flag1 = FixedMeta
   entry tail +24 bit 0x02** (mirror Fixed2Meta +31/+61 bit 0x80). Formula text
   lives in `114/Props` as UTF-16 `[MSPJ…]` tokens; Project does not compute
   formulas into `GetField` headlessly (even `1+1` reads 0), so `core/formula`
   semantics are inference.
4. **Usage is computed, not stored.** No timephased stream. Totals in
   `TBkndAssn/FixedData` (+12/+20/+52/+56/+60); contour step list in VarMeta
   `0x0f400031`/`0x0f400032`. W43 spreads Work flat over the calendar.
5. **App v3 is a real shell now**: menus, preferences (NSUserDefaults — Project
   exposes no global date/duration/timescale setter), filter dialog, Export PDF,
   dirty prompts, clipboard, keyboard nav. `main.mm` is 1,257 lines and only
   `app-macos` compiles it.

## Open, ranked

1. Wiring: usage/resource views and computed custom-field columns exist in
   `core` but no UI; custom-field values are not written.
2. Writer tail: linked exact-date writes (`w37-snet-link.mpp` unbuilt),
   add/delete for links/assignments, `214/` deliberately not regenerated, PROJECT_A
   structurally refused (broken `FixedMeta` identity).
3. `114/Props` decoding (formula text, table definitions) before custom fields
   can round-trip.
4. Recurring calendar exceptions, per-task calendars in CPM, MPP12,
   elapsed-DURATION format, resource Type exposure.
5. Manual Mac smoke test of the v3 shell.

## Traps (do not relearn)

* **`FixedMeta` header +4 is a tombstone-state bit.** Project ignores the entry
  `+0=2` alone. Add leaves +4=4, delete makes it 6.
* **Project recomputes IDs on open**; stored `FixedData +0` is cosmetic.
* `Task.Assignments.Add(ResourceID=…)` alone is rejected on this build: use
  `Add(TaskID=…, ResourceID=…)` and re-read `Assignments(1)`; its return is not
  an Assignment. `GetField` reads stored values, not formula results.
* `FileSaveAs(Name="x.xml", FormatID="MSProject.xml")` can silently write a
  binary `.mpp`; check the first bytes.
* `dir()` on a pywin32 COM object raises "Not implemented" on this build; probe a
  static candidate list.
* **Only `app-macos` compiles `main.mm`.** Two clang-only failures this wave:
  an unused static helper (deleted) and a `draw_gantt` call missing its new
  `working_day_shading` argument. MSVC never sees either; grep call sites when a
  signature changes.
* Dropbox mtime churn: re-read immediately before every edit (it hit the
  coordinator repeatedly).
* `plan_recalc_tests` rewrites `tests/data/project_a_recalc_golden.tsv` whenever corpus
  is present; only the coordinator re-pins.
* `git push` under the file sandbox can fail `SEC_E_NO_CREDENTIALS`; retry with
  wider access (the current session has danger-full-access, so it just works).

## Counters (wave-10 end)

78 agent sessions (73 + 4 streams + 1 W45 resume), 6 coordinator sessions;
**~$6.00** at ~$0.077/session; ~605M tokens. Roadmap **~60%** (+7).

## Next three actions

1. Wire `core/usage` + `core/formula` into `core/session`/`core/sheet`/the app,
   keeping `value_of_field` the single read path.
2. Decode `114/Props` formula blocks; then persist custom fields.
3. Build/open `w37-snet-link.mpp`; then add link/assignment row support.
