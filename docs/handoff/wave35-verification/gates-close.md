# Wave 35 G4 (the macOS shell) and CLOSE: coordinator verification (Mon 9 Nov 2026)

The gated commit is `18465b9` (`WAVE-35-COMPLETE`).  It covers G4 (`1cd37a9`, record
`4e3ba80`) and CLOSE (`513de29`, `25185a3`, `d4f3038`, `18465b9`).  Gates ran in the verify
worktree, detached at `18465b9`, with fresh build trees, Release, `CORE_WARNINGS_AS_ERRORS=ON`
and the main corpus junctioned in read-only, from 07:41 to 08:11.  Logs are in
`build/verify-w35/close/` (git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 65/65 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached (`constraint-mso`) |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 0 warnings; 32/34 in the gate run, see below; 34/34 on the rerun |
| fixtures; markers | OK; OK |

**App lane:** in the gate run, ctest could not start two executables, `app_fields_tests.exe`
and `app_edits_task_mode_tests.exe` ("Process not started ... [unknown error]", reported as
BAD_COMMAND).  Both had been built and were on disk.  Run again by exact name a few minutes
later, both passed, and a full app ctest then passed 34/34 (`32b-app-rerun.log`,
`32c-app-ctest-full.log`).  This was a launch failure on this machine, not a test failure.  It
is recorded rather than called green: CI is the independent check.

**Not compiled here:** `app/main.mm` (G4) is Objective-C++.  CI's macOS jobs are its only
compile gate.

## Fixtures

G4 and CLOSE add no `.mpp` blob (`2468736..18465b9`: 0 blobs).

## CI

Not run.  The branch cannot be pushed until the operator settles commit `9fa599b`, whose two
fixture blobs carry the operator's name (`gates-g3.md`).

## Independent review of G4 (`review-g4.md`)

A read-only agent reviewed `1cd37a9`: 0 Critical, **1 High**, 1 Medium, 2 Low.  The
coordinator confirmed both at source.

- **R35G4-1 (High, macOS only):** Resource Information and Assign Resources fill an
  `NSPopUpButton` with `addItemWithTitle:`, which removes an existing item of the same title.
  They then map the chosen index through a separate list of uids.  In a plan with duplicate or
  blank resource names, a later choice maps to the wrong resource, and Save renames or assigns
  it.
- **R35G4-2 (Medium, certain CI break):** `app_qt/command_status.h`'s `shell_implements`
  switch has no `default:` and missed the five new MenuActions.  `-Wswitch` under `-Werror`
  fails it on GCC and Clang; MSVC does not warn.  CLOSE found the same thing (RW35-R1) and fixed
  the compile in `513de29`, but by listing the five as unimplemented Qt menu items.
- **Lows R35G4-3, -4:** three permissive menu cases are untested; the macOS rename dialog does
  not prefill the name.
- **Clean:**
  - all five handlers go through the shared builder and `push()`, and show its refusal text;
  - the moved helpers leave the Qt shell byte-identical;
  - the five edit kinds are exclusive in `push()`;
  - the macOS dispatch and the exhaustive switches in `menu_model.cpp` are complete;
  - hygiene.

## CLOSE's own reviews

CLOSE committed `close-review-writers.md` (0 Critical, 2 High, 5 Medium, 6 Low) and
`close-review-shells.md` (0 Critical, 2 High, one closed by `513de29`, 3 Medium, 5 Low).  Its
new Highs:
- **RW35-W2:** Calculate never checks the leaves' Start and Finish.
- **RW35-R2:** Sort and Show Level are built from the session plan, which carries unsaved grid
  renames.

RW35-W1 is R35G3-1 found again.  The coordinator read both new Highs against the source rows
they cite.

## Merge rule

Wave 35 does not meet it:
- Highs are open: R35G1-1 to -3, R35G2-1 to -4, R35G3-1, R35G4-1, RW35-W2 and RW35-R2;
- no CI run covers G3, G4 or CLOSE;
- `9fa599b` is unresolved;
- the 4(e) Project reopen check has not run.

The fix-only wave 35b (`docs/handoff/wave35b-brief.md`) takes every High in its G0 and G1,
and the Mediums and Lows after them.
