# Wave 35b G0 (macOS CI and the Calculate Project Highs): coordinator verification (Mon 9 Nov 2026)

The gated commit is `e1397d1` (`WAVE-35b-G0-COMPLETE`).  It covers items 0.1 to 0.5
(`6472da3`, `efd30c6`, `8695ea8`, `cc9b581`, `15b9efd`).  Gates ran in the verify worktree,
detached at `e1397d1`, with fresh build trees, Release, `CORE_WARNINGS_AS_ERRORS=ON` and the
main corpus junctioned in read-only, from 09:21 to 09:56.  Logs are in `build/verify-w35b/g0/`
(git-ignored).

**Hashes.** The branch was cut from wave 35's `ec326b6` before the operator's history rebuild
of `wave35` (see the end of this file).  The commit ids here are the ones the builder made.
`wave35b` is rebuilt onto the new `wave35` after its CLOSE, and the old-to-new map is committed
then.

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached (`constraint-mso`) |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

**Not compiled or run here:** item 0.1 changes the Qt prompt hook and the Assign Resources
gate for the macOS CI failure (CI-4).  Only CI's `macos-qt` job shows whether it is fixed.
That waits for the `wave35b` push.

**Completion file:** `docs/handoff/wave35b-complete.md` holds the marker line only.  The gate
line, "Guards seen red" and "Deferred" are in the commit messages, not in rows as the standing
rules ask.  This is a process defect, recorded for the next brief.

## Fixtures and names

G0 adds no `.mpp` blob (`ec326b6..e1397d1`: 0 blobs).  `guard_names.py` exits 0 and
`scan_client_names.py` finds 0 hits at `e1397d1`.  All six commits carry
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35b)`.

## Mutations (`build/verify-w35b/mutate-g0.py`, `g0/mutations.log`)

Each guard was switched off with a non-constant false.  The guard's own lane was rebuilt and
its ctest run by exact name.  Then the file was restored with `git checkout`.  The worktree
was clean afterwards, and all three lanes were green again.

| id | guard switched off | result |
|---|---|---|
| P0 | positive control: `calculate_measured_finish` always fails | red in core, app and Qt |
| W1 to W5 | writer: deadline, leaf at project start, shortened project, moved critical path, Finish = Start + Duration | red |
| W6 | writer: the `check_unvaried_parameters` call | red (12 failures) |
| W7 to W10 | writer: resource-table size, Standard calendar, baseline key, exact variable keys | red |
| **W11** | writer: variable records stored for exactly N rows (`by_uid.size() != groups`) | **green: untested** |
| W12, W13 | writer: inactive task, FixedMeta flag byte | red |
| A1 to A12 | app: resources, assignment to a resource, assignment baseline, Standard calendar, inactive task, task baseline, stored task type, deadline, leaf at project start, shortened project, moved critical path, Finish = Start + Duration | red |

- **P0 is red in `qt_calculate_gate`.**  The Qt gate is no longer vacuous for Calculate
  Project (brief item 2.6).
- **W11 is untested.**  The case it alone catches is a row that stores no variable records at
  all.  The exact-keys check passes then, because it only loops over the rows that do store
  records.  It is recorded as **R35bG0-9 (Low)**.

## Independent review (`review-g0.md`)

A read-only agent reviewed `ec326b6..e1397d1`: 0 Critical, **2 High**, 3 Medium, 3 Low.  The
coordinator confirmed both Highs at source.

- **R35bG0-1 (High): the Props project start is never checked.**
  - Both layers compare each leaf's Start with row 0's Start.
  - `save_calculate` stamps every leaf's Fixed2Data +46 with `114/Props` record `0x02400002`
    (`calculate.cpp:552-566`), and neither layer reads that record.
  - So a project start moved under manual calculation passes items 0.3, 0.4 and the
    pending-flag check, and is saved with the old leaf dates.
  - Project's behaviour here is inferred, not measured.
- **R35bG0-2 (High): no other `114/Props` record is pinned.**
  - Only the pending flag `0x02400024` is read.
  - The calculation options (multiple critical paths, the critical-slack limit, minutes per day)
    can differ from the control's and the file is still saved.
- **Mediums:**
  - R35bG0-3: a move made by two Duration edits passes the critical-path guard, so "a Duration
    edit alone" overstates item 0.4.
  - R35bG0-4: the new plan fields are never tested through `reader_adapter.h`.
  - R35bG0-5: the plan layer is narrower than the writer (FixedMeta flags, Baseline 1 to 10,
    notes, tied leaves).  Nothing is mis-written, because the writer check always runs.
- **Lows:**
  - R35bG0-6: the `push()` tests assert yes/no only.
  - R35bG0-7: FixedMeta bytes 4 to 15 and some leaf date copies are not compared.
  - R35bG0-8: the project row's deadline is not checked.
- **Clean:**
  - 0.1's text-based prompt match;
  - 0.2 in both layers;
  - 0.4's date arithmetic;
  - no pin in 0.5 is vacuous;
  - the measured control is still accepted from the real fixture;
  - specific needles;
  - the only writer entry point;
  - hygiene.

R35bG0-1 and -2 are new Highs.  The wave 35b brief cannot change while its driver runs, so
they head the next fix wave's queue.

## Wave 35 history rebuild (operator's decision, Mon 9 Nov 2026)

The operator chose to rebuild `wave35` without the G3 fixtures that carried a name.  The 26
unpushed commits after `daf46f8` were replayed object by object.  The only change: in `9fa599b`,
the three notes-generator fixtures were swapped for their neutral-identity versions from
`9564ddd`.

- 25 of the 26 trees are identical to the originals.
- Every message, author and date is byte-identical.
- The first five commits came out with their original ids.
- 53 blobs scanned, 0 name hits.
- The new tip `2ed490c` was pushed as a fast-forward of `daf46f8`, with no force push.

CI at `2ed490c`:
- `ci`: Windows MSVC, Ubuntu ASan and Ubuntu GCC green;
- `app-macos`: green, the first compile of G4's `main.mm`;
- `qt-shell`: Linux and Windows green; macOS fails the same two tests as before
  (`qt_prompt_hook`, `qt_assign_resources_gate`, CI-4).  G0 item 0.1 addresses them; that is
  proven only when `wave35b`'s CI runs.

## Merge rule

Not met:
- Highs are open: R35bG0-1 and -2, plus the wave 35 Highs that G1 takes;
- CI-4 is unproven;
- the 4(e) Project reopen check has not run.
