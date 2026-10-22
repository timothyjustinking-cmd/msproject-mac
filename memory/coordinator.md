# memory — coordinator (wave 12 end, 2026-10-22)

Successor: read `docs/handoff/wave12-complete.md` (the gate), then
`docs/handoff/coordinator.md` "Wave 12 final", then this. The build phase before
the human/adversarial audit is complete; `docs/audit-handoff.md` is the audit
brief.

## Where main is

`6498a07` plus the docs commit, pushed. Wave-12 commits: `a5a05f8` W52+W53,
`6498a07` W51. Wave-11 CI debt paid at `1639d80` (two clang/gcc-only fixes).

## The number

* `build\vs-w12`: **40/40 ctest**; app `build\vs-w12-app`: **10/10**, 1,765
  checks.
* **M2 332/332** (83/83 × 4); schedin 20/20 + 10/10; plan_recalc 291; cal 509;
  reader 685; links 773; add_delete 760; field_values 477; cpm 1,321; props 520.

## The six findings that matter

1. **Custom-field display needs three stores** (wave 11): VarMeta entries after
   Name inside the uid group; 88-byte records in both `TBkndTask/Props` and
   `TBkndAssn/Props`; FixedMeta tail bitmap + `+2` count. Project now displays
   all six values and their rewrite.
2. **Delete is a tombstone** with `FixedMeta` header +4 bit 0x02 in every
   affected table; a task delete cascades over links (either endpoint) and
   assignments. No stream shrinks.
3. **Cons add writes four streams**: FixedData 20·n, FixedMeta 16+10·n,
   Fixed2Data 48·n, Fixed2Meta 16+9·n (+8 0x0f/0x07). Link delete tombstones the
   FixedMeta entry only.
4. **Recurring exceptions**: seven u16 words at record +74..+86; expansion
   validated against Project's occurrence counts (18/7/5/4/5/4). Monthly
   positional stores MonthItem 0; weekday derived from the start day.
5. **Linked pin clamp**: SNET is no-earlier-than; a pin before the dependency
   reopens at `max(pred finish, pin)` with the constraint stored exactly.
6. **App**: Insert/Delete Task with two undo stacks, Save = delete → insert →
   dates → reload; stored custom-field and file-formula columns are read-only.

## Open, ranked

1. **Assignment row add/delete** — last structural writer gap; needs the
   add-assignment oracle (resource row + assignment row + VarMeta/Props +
   Fixed2Data).
2. App UI for **writing custom fields** and editing links/assignments.
3. `214/CTable`/`214/CV_iew` view state, MPP12, MPP14 resource Type,
   `114/Props` writes.
4. Manual Mac smoke test; ASan/debug + adversarial audit.
5. `tests/data/synthetic-base.mpp` must stay **237,568 B**; an agent grew it and
   the coordinator restored it. Check `git status tests/data` at handoff.

## Traps (do not relearn)

* **Three custom-field stores** — a VarMeta-only write silently shows 0.
* **Project recomputes task IDs on open**; stored IDs are cosmetic.
* `core/mpp/reader.*` is shared; one writer stream at a time (a half-applied
  reader change segfaulted a concurrent stream's tests).
* Liveness changes move test pins (`io_mspdi_crosscheck` PROJECT_B 72/73 → 70/63).
* **Only `app-macos` compiles `main.mm`.** Keep the ObjC++ delta small; grep
  call sites; budget one CI iteration.
* GCC flags backslash-continued `//` comments (`-Wcomment`) and unused static
  helpers; MSVC does not.
* `FileSaveAs(Name="x.xml", FormatID="MSProject.xml")` can silently write a
  binary `.mpp`; check the first bytes.
* `dir()` on pywin32 COM objects raises "Not implemented"; probe a static list.
* `plan_recalc_tests` rewrites `tests/data/project_a_recalc_golden.tsv` whenever corpus
  is present; only the coordinator re-pins (unchanged so far).
* Dropbox mtime churn: re-read immediately before every edit.
* `git push` works with danger-full-access.

## Counters (wave-12 end)

88 agent sessions (85 + W51 + W52 + W53), 8 coordinator sessions; **~$6.78** at
~$0.077/session; ~680M tokens. Roadmap **~71%**.

## Next three actions

1. Add-assignment oracle, then `add_assignment`/`delete_assignment`.
2. App custom-field write UI + link editing, then the manual-Mac smoke test.
3. ASan/debug + `docs/audit-handoff.md` checklist; fix findings; hand to a human.
