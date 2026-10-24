# Wave 27 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-27-<group>-COMPLETE` line is appended below (wave-27-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the wave-26 Highs (save safety)

All three items are DONE; nothing in G0 is deferred.  Each item's fixture
commit precedes its code commit under the same finding id.

| item | finding | status | evidence |
|---|---|---|---|
| W27-0.1 | High: % Complete saves one word of the eight-plus copies Project writes (`corpus/probe/var-pct.mpp`) | **DONE** | fixtures `d30c2e8`, code `0961f19`; gates `build/w27/gates-d30c2e8-W27-0.1.log`, fixtures `build/w27/gates-fixtures-W27-0.1.log` |
| W27-0.2 | High: % Complete accepted on summary tasks; the refusal lived only in the shell | **DONE** | commit `29f6827`; gates `build/w27/gates-0961f19-W27-0.2.log` |
| W27-0.3 | Medium: Project Information Priority save unmeasured | **DONE (KEPT)** | fixtures `a40ef6d`, code `5a991e9`; gates `build/w27/gates-a40ef6d-W27-0.3.log`, fixtures `build/w27/gates-fixtures-W27-0.3.log` |

W27-0.1 detail:

- `scripts/oracle_w27.py` (committed) opened `tests/data/synthetic-base.mpp` on
  a copy under `C:/w27`, set Task ID 1 `PercentComplete = 50` through Project
  2021 COM, neutralised UserName/Author/Manager/Company/Title and saved
  `tests/data/wave27-percent-actuals.mpp` (239,616 bytes, SHA-256
  `4C9AECBF...CC7AA2`).  A byte-by-byte Task FixedData diff against
  synthetic-base shows the task row changing +80/+81 ActualDuration,
  +88/+89 RemainingDuration, +92 PercentComplete, +120..123 ActualStart,
  +132..139 the two date words, +169 (the +168 flag word) and Var2Data key
  0x0B40015E, and the project row changing +80/+81, +88/+89, +92, +120..123 and
  +132..139.  Project writes eight-plus copies; the port wrote +92 only.
- Because the port has no measured actuals writer, the five Task > Schedule %
  buttons are DISABLED with the exact decision-9 tooltip ("not implemented in
  this port"): the pin (`app_qt/ribbon_pin.tsv`), the live ribbon spec, the
  enabled-wiring list and the live gate all agree.  `MainWindow::
  set_percent_complete` is removed and the five ribbon cases collapse to an
  unreachable return, so no UI route can queue `StructuralKind::set_percent`.
- The edit kind, its save path and its tests are kept and marked NOT ENABLED,
  ready for the wave-28 actuals writer that re-enables the buttons.
- The Move Project gate can no longer create actuals by clicking 50 %; it opens
  the Project-saved actuals fixture, checks the prompt text/buttons and asserts
  answering Yes queues no edit (app_qt 23/23).

W27-0.2 detail:

- The leaf/summary rule is now in the edit model.  `app::make_percent_edit(plan,
  uid, percent, out, error)` refuses uid 0 (project summary), an unknown uid (a
  pending insert has no saved row), a value above 100 and any `PlanInput`
  TaskRow whose `summary` is set, naming the reason in `error`.
- `StructuralEdits::push(edit, const PlanInput* = nullptr)` REQUIRES the plan
  for a `set_percent` edit and refuses a plan-less one rather than trusting it;
  the validated edit replaces the caller's.  The other kinds ignore the new
  argument.
- The new app test covers every refusal, a hand-built summary edit pushed with
  the plan, a plan-less push, leaf acceptance and the 50 % display value; the
  two real-save pushes pass the loaded plan.

W27-0.3 detail:

- The parent pair `tests/data/wave27-priority-control.mpp` /
  `wave27-priority-750.mpp` is ONE Project 2021 session's saves of synthetic-base
  at Priority 500 and after `ProjectSummaryTask.Priority = 750`, with neutral
  metadata.  The pair's Task FixedData differs in exactly row 0's +78/+79 word
  (f4 01 -> ee 02, 500 -> 750); every other stream difference is the per-save
  bookkeeping the wave-24 same-session pair also shows (114/Props GUID tail,
  the Fixed2Data GUIDs, the edit list, cached view state).  Project's stored
  edit is therefore the one +78 word, which is what `set_priority` writes, so
  the save is KEPT, not disabled.
- `app/project_dialogs_tests.cpp` now saves Priority 750 FROM the control
  fixture through the shared structural cascade and asserts, through
  `mpp::CompoundFile` rather than the port reader, that the saved
  `114/TBkndTask/FixedData` stream is byte-identical to Project's 750 fixture.
  It also pins the pair itself (only +78/+79, 0x01F4 -> 0x02EE) and keeps the
  round-trip checks.  This is the field-by-field fixture comparison standing
  rule 1 requires.

Gates on the group's final commit `5a991e9`, fresh trees configured from scratch
and built serially under `build/w27-fresh{,-app,-qt}` (log
`build/w27/g0-fresh-gates.log`):

- core configure/build exit 0 with `CORE_WARNINGS_AS_ERRORS=ON`; core ctest
  `-E plan_recalc` 51/51
- `plan_recalc` 512 checks with corpus / 266 with `MPP_NO_CORPUS=1`
- `engine_diff.py --selftest` 53 checks, 0 failures; `--suite
  --expect-fail constraint-mso` 7 pass + the one expected failure, 0 unrecorded
- reader 1100 checks with corpus / 420 without; M2 `recalc --compare` 332/332
  (268 leaf + 64 summary fields)
- app ctest 17/17; app_qt ctest 23/23
- `check_fixtures.py` OK (28 documented hashes, 24 tracked `.mpp`), oracle
  provenance OK (8)
- client-name scan 0 hits on HEAD; whitespace-normalised `guard_names.py` 0 on
  `c632c8f..HEAD`; client-string hex grep no matches

## Found, not fixed

- `app/edit_model.cpp` is wholly CRLF in the index (1,093 lines) while
  `app/edit_model.h` and the surrounding app sources are LF; the wave-26 review's
  W27-3.6 names two other wholly-CRLF files and does not name this one.
  Converting it under W27-0.2 would have put a whole-file EOL diff under a
  two-function finding, so it is left for the G3 EOL item.  Everything G0 added
  or created elsewhere is LF.
- Project changes per-save GUIDs and edit-list/view-state bytes on EVERY save
  (the wave-24 same-session pair proves it), so no future save test can compare
  two whole files byte-for-byte even when both were produced by Project.  The
  comparison has to be the semantic stream that carries the edited field, as
  W27-0.3 does for Task FixedData.

WAVE-27-G0-COMPLETE
