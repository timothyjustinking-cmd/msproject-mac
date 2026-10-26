# Wave 31 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-31-<group>-COMPLETE` line is appended below (wave-31-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the wave-30 review's High findings

From `docs/handoff/wave30-verification/review.md` (0 Critical, 4 High).

| item | finding | status | evidence |
|---|---|---|---|
| W31-0.1 | R30-1 (High): a link added after a queued automatic outline edit | **DONE** | commit `33fbb3d`; gates `build/w31/W31-0.1-*.log` |
| W31-0.2 | R30-2 (High): the automatic outline save on files unlike the pairs | **DONE** | commit `d9d8d8d`; gates `build/w31/W31-0.2-*.log` |
| W31-0.3 | R30-3 (High): percent/date edits with progress off the edited chain | **DONE** | commit `659fd97`; gates `build/w31/W31-0.3-*.log` |
| W31-0.4 | R30-4 (High): Fill Down is an unmeasured write | **DONE** | commit `9c3213e`; gates `build/w31/W31-0.4-*.log`; measured pair `wave31-fill-control.mpp` / `wave31-fill.mpp` / `wave31-fill-control-null.mpp` |

No G0 item was deferred.  Items 0.1-0.3 narrow an enabled save path to the
shapes its Project pairs measured, or refuse every other shape up front; item
0.4 measured Project's Fill Down pair, and the measurement shows the port's
start writer cannot reproduce the save, so the command stays disabled with
decision 9's exact tooltip (the brief's "until then" rule).  The group found no
new Critical or High finding.

W31-0.1 detail:

- An automatic (slack-carrying) outline edit computes its slack and late dates
  on the unlinked file, and the save writes them at step 5b, after the link
  add at step 2.  Add Link was not checked against a queued outline edit, so
  the shape W30-0.2 refuses in the other order could be saved.
- `validate_link_add` refuses while an applied outline_indent/outdent carries
  a non-empty slack list (the Predecessors dialog path), and `push(add_link)`
  refuses the same queue for a caller that skipped validate;
  `save_structural_edits` refuses a hand-built queue holding both, before the
  first write.
- The reverse order is unchanged and still refused: `make_outline_edit` sees
  the pending link in the display plan and `push` sees the plan's links.
- Tests: `app_edits_links` covers both orders and the save-level refusal on
  `wave29-outline-auto-indent-control.mpp`; the targeted run is 5/5.

W31-0.2 detail:

- `check_automatic_outline_file` reads the saved task table and refuses, file
  wide, a progressed/completed row or stored actuals, a constraint type other
  than ASAP, a deadline (+182), and a stored or structural summary outside the
  moved subtree and anchor.  The moved subtree may hold summaries: the
  automatic-summary pair measured one.
- Both automatic branches (indent and outdent) call it after the linked-file
  refusal, so a refused shape builds no edit.
- Tests: `app_edits_outline` patches one word of one row on copies of the
  committed controls and checks a refusal per shape, plus the outdent path
  (3201 checks).

W31-0.3 detail:

- `check_summary_rollup_scope` takes the scope flag and, unless the edit is
  the measured Entire project date update, refuses any live row outside the
  edited uid set whose stored percent is above zero (the project row is the
  measured roll-up target and is not counted).  The ancestor-chain rules run
  first.
- `make_percent_edit`, `make_update_actuals_edit` (which now computes its
  scope flag before the check) and both `push()` paths pass the flag.
- Test (e) of `check_summary_rollup_scope`, which used to accept a 40 % row
  outside the edited set, now asserts the refusal, the message, the push
  refusal and the Entire project exception; a patched flat control proves the
  date-edit half (`app_edits_actuals` 3782 checks, 50 expected failures).

W31-0.4 detail:

- Measurement: `scripts/oracle_w31_fill.py`.  FileNew synthetic project,
  neutral Author/Manager/Company/UserName/Title, ProjectStart 2026-01-05
  08:00; Task A 2400 min with Start 2026-01-12 08:00 (stored as SNET type 4),
  Task B 1440 min at the project start; automatic calculation.  The seed is
  saved and reopened (this build's first save after FileNew is not
  reopen-stable, W30-2.4), the control is saved from that session, then
  `SelectTaskField(1, "Start", False, 0, 1)` and `FillDown(True)` run and the
  edit is saved.  The null save is a no-edit save from a COPY.
- Pair facts: the control's Task B is 2026-01-05 08:00 -> 2026-01-07 17:00;
  the edit's Task B is 2026-01-12 08:00 -> 2026-01-14 17:00 with SNET type 4
  and total slack 9600 ticks; Task A is untouched; the project row rolls up
  from 4800 to 2400 min at finish 2026-01-16 17:00.  Reopens and the null
  facts match.  Evidence: `build/w31/W31-0.4-oracle.log` (exit 0) and
  `build/w31/W31-0.4-fill-pair.json`.
- Noise-mask comparison: the control-vs-null difference is empty (Task
  FixedData 0 bytes, `DocumentSummaryInformation` byte-identical), so the
  measured effect is Project's edit alone: Task FixedData 25 bytes (the
  edited row's constraint window +64/+66 AND calculated/effective dates
  +98/+102/+106/+110, slack +74/+186/+190, +169/+170; the project row's
  duration/remaining +84/+88, start +96/+104, total slack +186), FixedMeta
  byte 253, Fixed2Data 24 bytes, Var2Data 350 -> 366 (one 16-byte record
  APPENDED), VarMeta 9 bytes, TBkndAssn FixedData 4 bytes, Props 10 bytes,
  and `DocumentSummaryInformation` 1104 -> 1100 with 181 effect bytes.
  `SummaryInformation` is byte-identical.
- Conclusion: `save_date_edits` may change only the 6-byte constraint window
  of an edited row (plus the manual bit), so this pair cannot pass
  `tests/save_pair_noise.h` and the port has no writer for the row rewrite,
  the Var2 append or the DSI summary strings.  Standing rule 1 keeps Fill
  Down disabled.
- UI: the ribbon Fill control is disabled (`app_qt/ribbon_bar.cpp`), the pin's
  enabled column is 0 (`app_qt/ribbon_pin.tsv`), and the row-menu Fill Down is
  disabled with decision 9's exact tooltip `not implemented in this port`.
  The `RibbonCommand::fill_down` handler stays behind the disabled control;
  `fill_down()` refuses a summary row before staging anything (R30-4's U1).
- Tests: `qt_fill_gate` asserts the control exists, stays wired to
  `RibbonCommand::fill_down`, is disabled with the exact tooltip, and stages
  nothing (clean document, no undo step); the row-menu action is disabled with
  the same tooltip; and the committed pair opens read-only to pin the facts a
  future full start writer must reproduce (control and edit Task B starts and
  finishes; automatic 0 % leaves on the project calendar).
  `qt_ribbon_inventory` drops the stale enabled-wiring entry and its pin check
  asserts the disabled state and tooltip.
- Fixtures: documented in `tests/data/README.md` (provenance, COM facts, every
  measured effect byte, all three SHA-256) and pinned in
  `scripts/check_fixtures.py`.

Per-commit gates (logs under `build/w31/`, named `W31-0.x-*.log`; all four
items' logs report scan HEAD 0 files / 0 hits, guard `wave30..HEAD` exit 0,
hex grep no match and fixtures OK):

| commit | builds | item tests | app ctest |
|---|---|---|---|
| W31-0.1 `33fbb3d` | app build exit 0 | targeted 5/5: `app_edit`, `app_edits_links`, `app_edits_actuals`, `app_edits_outline`, `app_smoke` | not run (targeted) |
| W31-0.2 `d9d8d8d` | app build exit 0 | `app_edits_outline` 3201 checks | 22/22 |
| W31-0.3 `659fd97` | app build exit 0; app_qt build exit 0 | `app_edits_actuals` 3782 checks, 50 expected failures; app_qt 32/32 | 22/22 |
| W31-0.4 `9c3213e` | app build exit 0; app_qt build exit 0 | `qt_fill_gate`, `qt_ribbon_inventory`; app_qt 32/32 | 22/22 |

G0 group gates (fresh trees configured from scratch on `9c3213e` under
`build/w31-fresh{,-app,-qt}`, log `build/w31/W31-G0-fresh-gates.log`; the run
started 22:48 and finished 22:58 with no lock retries):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 52/52 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failures / 8 recorded (7 pass, 1 expected fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1100 checks / 420 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app ctest / app_qt ctest | 22/22 / 32/32 passed |
| `check_fixtures.py` / oracle provenance | fixtures OK (134 documented hashes, 170 tracked `.mpp`); paths OK (217 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave30..HEAD` / `guard_names wave30..HEAD` / hex grep | 0 files, 0 hits / 0 commits / exit 0 / no matches (exit 1, expected) |

The numbers belong to `9c3213e`; the marker commit that follows is docs-only.

## Found, not fixed

- Nothing new.  G0 raised no Critical, High, Medium or Low finding beyond the
  four items above.
- Residual note (not a finding): Fill Down's summary-row refusal is
  defence-in-depth behind the disabled control; the UI cannot reach it, so the
  gates assert the disabled state, tooltip and inert click rather than the
  refusal itself.

WAVE-31-G0-COMPLETE
