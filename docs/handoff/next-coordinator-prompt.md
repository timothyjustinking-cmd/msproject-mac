# Paste this into a fresh coordinator session (wave 10)

You are the coordinator for porting Microsoft Project to a native macOS app
(personal use). Work in: `D:\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile`
(Windows, MSVC 19.44; the Dropbox folder lies about mtimes — re-read before edits).

## STEP 0 — WAIT GATE. Do this before reading, building or writing anything.

Run exactly this and look at the answer:

```
Test-Path docs\handoff\wave9-complete.md
```

* **`False` -> STOP.** Reply with one line only: "standing by: the wave-9
  coordinator has not finished (no docs/handoff/wave9-complete.md)". Do not read
  further, do not build, do not edit, do not run git. The previous coordinator is
  still running in the same working tree and two sessions writing one checkout
  corrupts it.
* **`True` -> read that file first** (it names the final commit, the CI run ids,
  the verified numbers and the counters), then continue with READ below.
* **Human override:** if the operator says the previous coordinator is gone, skip
  the gate, read `docs/handoff/coordinator.md` §"Recovery" and follow it before
  doing anything else. A dead coordinator means uncommitted stream files may be
  on disk; they are recoverable, the tree state is not.

## READ, in this order

1. `docs/handoff/wave9-complete.md` — the marker: final commit, CI, numbers.
2. `docs/handoff/coordinator.md` — the last section is the wave 9/9b handoff. It
   supersedes every earlier section and any number below.
3. `WAVES.md` — wave 8 results, wave 9/9b, and **"Wave 10 — planned"** at the end.
   That section is your job order, with enter conditions and reserved build dirs.
4. `docs/roadmap.md`, `docs/cost.md`, `docs/coordination.md` (the rules agents
   are held to), then `memory/coordinator.md` (traps and next actions).

## STATE AT THE TIME THIS PROMPT WAS WRITTEN (2026-10-22 ~16:55) — snapshot only

* GitHub `main` = `f35a5ab`, CI green on `windows-msvc`, `macos-appleclang`,
  `ubuntu-gcc`.
* Local, verified and committed but **not yet pushed** when this was written:
  `6e0dadf` W36 `core/session` (821 checks with corpus, 121 without),
  `2d5d1cc` W37 save flow (`save_date_edits` + `save_plan`, 7/7 writer tests,
  101 checks; `spec/mpp-writer.md` §10.8), `9141946` W39 `core/render` (169
  checks, pypdf-validated).
* Still running at that moment: **W38** app shell v2, **W40** reader/io tail,
  **W41** engine tail (ALAP), **W42** views breadth. The marker says which of
  them landed and what each verified.
* **M2 = 332/332** (start, finish, slack, critical 83/83 each), 0 unexplained;
  `plan_recalc` 185 checks; golden 83 uids. Treat any movement as a stop.
* **The writer gap is closed, measured 2026-10-22**: a date edit is written as a
  real SNET constraint at FixedData `+64/+66` **and** clears Fixed2Meta entry
  byte `+8` bit `0x80`; Project reopens the row at the pinned date with
  `Manual=False`. SNET alone on a manual row is stored-but-ignored (type 4 and
  the date read back, Start unchanged). So `save_date_edits` warns per converted
  row: the row becomes auto-scheduled. Fixtures `corpus/probe/w37-snet-auto.mpp`
  (honoured) and `w37-snet-save.mpp` (negative control).
* Open: ALAP display dates (W41), exact-date writes for a **linked** row
  (inference: Project will use `max(pred finish, pin)`), assignment Stop
  evidence, resource Type/MaxUnits (W40), no visual oracle for PDF layout
  (decisions live in `ReportStyle`), per-task calendars not scheduled, PROJECT_B's
  two dead-endpoint links are named exclusions.

## YOUR JOB (wave 10)

Launch the four streams in `WAVES.md` "Wave 10 — planned" — **W43** resource and
usage views, **W44** custom fields and formulas, **W45** writer row add/delete,
**W46** app shell v3 — five streams are not needed and cost more in build retries
than they buy. One message, one stream per subagent, one build dir each
(`build\vs-w43` .. `build\vs-w46`), enter conditions honoured: a stream whose
premise did not land gets re-scoped, not forced.

**Before launching, do the oracle agenda** in that section (coordinator-only; no
agent can drive COM):

1. **Add/delete stream diff** — Project's own file after one `Tasks.Add` and one
   delete, diffed stream by stream against the pre-save copy, including
   FixedMeta size words, Fixed2Meta entries, VarMeta and Var2Data. This *is*
   W45's design; W45 does not start until it exists.
2. **Formula ground truth** — 5-6 custom fields (`[Duration]*2`, `[Start]+7`,
   `IIf([% Complete]>50;1;0)`, a text concat, a duration division) read back via
   `GetField`, so W44 has expected values and storage keys.
3. **Time-phased usage** — one assignment spread over three days; save, then
   compare the stored streams with `TimeScaleData`, so W43 knows whether usage is
   stored or computed.
4. **Preference storage** — where date format, duration format and timescale live
   (`114/Props` candidates), so W46's preferences persist.

## RULES THAT ARE EASY TO GET WRONG

* **Agents cannot reach MS Project.** The oracle is yours alone, through
  `scripts/oracle_lock.py`. Never pass `/automation` to WINPROJ.EXE.
  `Application.Calculate` does not exist — `CalculateProject`. `FileClose()`
  defaults to `Save=2` and blocks — always `FileClose(0)`. pywin32 converts a
  naive datetime to UTC: pass aware datetimes. `GetField` returns formatted text.
  `FileSaveAs` needs an absolute path; MSPDI uses `FormatID="MSProject.xml"`.
  COM date getters return **local wall clock**, not UTC.
  `scripts/oracle_task_fields.py` is the fast way to open a fixture and read
  Manual/ConstraintType/ConstraintDate/Start/Finish/slack back.
* **`corpus/` is git-ignored client data.** Never commit a `.mpp` or any
  task/calendar/exception name; derived files keep uids/dates/counts only. Check
  `git show --stat` before every push.
* **MSVC is not on PATH**: `vcvars64.bat`, the "Visual Studio 17 2022"
  generator, and **never** `cmake --build --parallel`. A transient
  MSB6003/C1083 on this tree is a retry. clang/GCC in CI reject unused
  file-scope constants/functions and missing initializers: mark doc-only
  constants `[[maybe_unused]]`, value-initialise struct strings.
* **Verify every agent claim by re-running its command before committing.**
  Nothing counts until it is on `main` and CI is green on all three platforms.
* `plan_recalc_tests` rewrites `tests/data/project_a_recalc_golden.tsv` on every run
  with corpus present. No stream may re-pin it; you re-pin deliberately.
* The app (`app/`, ObjC++/AppKit) **cannot be compiled here**. macOS CI is the
  only build proof: push, then `gh run list` / `gh run view`.
* `git push` can fail with `SEC_E_NO_CREDENTIALS` under the file sandbox; retry
  that exact push with wider access rather than inventing another route.

## REPORTING AND HANDOFF

After the wave: roadmap bars from `docs/roadmap.md`, one line per agent, the new
M2/verification numbers, and tokens/cost at ~$0.08/session (wave-9 end counters
are in the marker; add to them). At ~70% of your usable context, write
`memory/coordinator.md` and a new final section in `docs/handoff/coordinator.md`
for your successor, create the next marker (`docs/handoff/wave10-complete.md`),
and say plainly that you have done so.
