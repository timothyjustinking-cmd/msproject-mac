# Wave 25 brief - verify-and-fix, then parity (for DeepSeek, coordinator role)

_Written 2026-10-24 (early morning) by the Opus verifier session after wave 24
closed.  Baseline: branch `wave25`, cut from local `wave24` = `2561a51`.  Neither
`wave23` nor `wave24` is merged to `main`; nothing has been pushed since the hold.
Sources: the verifier's fresh-tree gates and independent review of
`wave23..wave24` (`docs/handoff/wave24-verification/gates.md`, `review.md`, ids
S1..S14), and the items `wave24-complete.md` deferred.  This file is the one
document the fix session reads; where it conflicts with an older handoff, this file
wins._

## 0. Publication hold - unchanged from wave 24 section 0

The operator has not yet ruled on the client data already on the public repo.  Every rule in
`wave24-brief.md` section 0 still applies: **no push, no history rewrite, no
force-push, no deletion of the backup branches, CI only if the operator lifts the hold**, and
`build/audit-w22/scan_client_names.py HEAD --messages HEAD~1..HEAD` must report 0
hits for every commit.  When the hold is the only thing left, end with `BLOCKED:
publication hold` as wave 24 did.

## 1. Standing rules (tightened after the wave-24 review)

Everything in `wave24-brief.md` section 1, plus:

- **One finding id per commit, no exceptions.**  Wave 24 bundled up to nine ids in
  one commit (S8), so no single fix can be reverted.  A fixture commit carries
  fixtures only; code and its tests go in the next commit.
- **Gate numbers must belong to the commit they are written in** (S5: ca9e7d7
  claimed app_qt 18/18 on a tree with 17 tests; two commits a minute apart each
  claimed a full run).  Before each commit, run the gates on that exact tree and
  write the log to `build/w25/gates-<short sha of the parent>-<item>.log`; quote the
  numbers from that log.  The close record names the log for each number.
- **A gate is never flipped to assert the port's current output.**  When the port
  knowingly differs from Project, the test keeps Project's truth and is listed as
  an expected failure by name (the engine lane's `--expect-fail` pattern), so it
  goes green the day the gap closes (S6).
- Shared `app/` behaviour is tested in the headless `app/` ctest as well as the Qt
  tests, because macOS is not built or launched on this machine (S1).

## 2. Authorised now - in this order

### 2.1 Regressions and mislabelled measurements

**W25-1  S1 (High)  Row 0 on the shared and macOS paths.**  W24-2 moved the
"show the project row" decision into `app/reader_adapter.h:144`, so app/ and macOS
now see a row 0 too, but `app::make_gantt_source_for_rows`
(`app/layout_model.cpp:~523`, `input.tasks[row]`), the macOS screen Gantt
(`app/main.mm:654`) and `app/smoke.cpp:380/396` still index tasks by sheet row:
bars shift one row and the last bar is dropped on any file with the row shown;
macOS also lacks the uid-0 edit guard (R5).  Fix in the shared layer: resolve every
row through its uid, guard uid 0 for edits, and add an `app/` headless test on
`tests/data/wave24-order.mpp` (row shown, stored order differs from ID order) that
fails under row arithmetic for the Gantt source and the smoke paths.

**W25-2  S2 (High)  The "stock" bar colours are probably manual-vs-auto, not
task-vs-critical.**  (116,204,212) was sampled from an all-manual fixture;
(138,187,237) from a blank project where every task was auto and critical, while
Project's default Gantt does not highlight critical tasks.  Measure properly in
Project on a neutral copy: one plan with an auto task, a manual task, a summary, a
milestone and a critical auto task, captured with Format > Critical Tasks OFF and
again ON (captures local).  Derive each colour from the row whose type is unambiguous,
re-pin the parity/ux/shell gates from that measurement, and add an
auto-scheduled reference to the parity JSON so this class of error fails the gate.

### 2.2 The privacy scrub, done properly (tree only; history is the operator's)

**W25-3  S3 / S4 (Medium)  Replace `client-string-<sha8>` and the person-name
hash pins.**  Unsalted 8-hex prefixes of short names are invertible from a guess;
the reviewer inverted three.  Use opaque labels with no derivation from the text
(e.g. `client-name-07`), keep any label-to-name mapping only under the git-ignored
`corpus/` or `build/`, and compare in tests against the reader's value via a
local-only map or a salted keyed hash whose key is not committed.  Remove the UTF-16
hex of name prefixes beside the hashes in `spec/mpp-records.md:372-383`.  Undo the
find-and-replace damage to non-client content: `docs/analysis/oracle/writer-name.json`
(regenerate it from a synthetic fixture rather than holding a value Project never
returned), the spec byte counts that no longer match their strings, the decompiler
script banner word, and the earlier review's falsifying command.  Acceptance: the
name scan reports 0 hits on the tip, AND a grep for any 8-hex `client-string-`
token returns nothing.

### 2.3 Honesty of the record

**W25-4  S5 (Medium)** Reconcile every gate number in `wave24-complete.md` and the
wave-24 commit messages against logs on the exact commits; correct START-HERE
("11 hashes, 10 .mpp", "clean as of ff22c7b").  Errata go in `wave25-complete.md`;
do not rewrite wave-24 commits.

**W25-5  S6 (Medium)** Restore `app_qt/tests/stored_table_test.cpp:787-803` to assert
what Project shows for PROJECT_A's text styles, and list it as an expected failure by
name until W25-12 decodes the file's text styles.

**W25-6  S7 (Medium)** The eight scenario oracles' provenance names 4299ff8/d6724da;
all eight were last re-recorded by e91f19b.  Correct the recording commit from
`git log --follow` and add a check that the provenance commit is the last commit
that changed each oracle's measured body.

### 2.4 Gate holes (Low, but each makes a gate pass without testing)

**W25-7  S9** The ribbon `kind` check must read the LIVE widgets (combo, checkbox,
gallery, button), not only the pin file.
**W25-8  S10** The corpus record-count check derives its expectation from the value
under test; pin it from an independent source.  The Flag1..Flag20 block must FAIL,
not skip, when `wave24-flags20.mpp` is missing.
**W25-9  S11** Add single-bit controls for Flag11..Flag17, and commit the generator
(`measure_flags.py`) under `scripts/`.
**W25-10  S12** Commit BOTH halves of the Timeline shown/hidden save pair so the
"only that record differs" claim reproduces from the repo.
**W25-11  S13 / S14 / review R16-R17** The `flag10_disagreements` mis-rename, stale
comments, the PDF export subtitle counting the project row, and a written disposition
for the previous review's R16 and R17.

### 2.5 Parity work carried from waves 23-24 (after 2.1-2.4 are committed)

**W25-12** File text styles (font family/size, milestone/critical/summary styles,
highlighted rows) from the view records (W24-13/W24-19).
**W25-13** File bar styles from the `0x22400008` view blob (W24-18), falling back to
the W25-2 measured stock palette.
**W25-14** Timescale tiers and units from the same blob (W24-17).
**W25-15** View > Task Views / Resource Views drop-downs with Task Sheet, Resource
Sheet, Resource Usage where Project has them (W24-12).
**W25-16** Custom Fields as the field-definition dialog; per-task values on Task
Information's Custom Fields tab (W24-14).
**W25-17** Distinct monochrome icons with a uniqueness gate (W24-20).
**W25-18** Ribbon control kinds from `app_qt/ribbon_pin_unmodelled.tsv` (W24-21).
**W25-19** Backstage pages, Account/About, the 11-page Options dialog, the B-14
dialogs (W24-22).
**W25-20** Dark theme: chrome only, sheet and chart unchanged, live repaint (W24-23).
**W25-21** Remaining dialogs and the split view (W24-24); W24-25 items as capacity
allows.

## 3. Fixes where the obvious change is wrong

| finding | tempting fix | why wrong | correct fix |
|---|---|---|---|
| W25-1 | hide row 0 on macOS | the file says to show it | route by uid in the shared layer |
| W25-2 | swap the two colours | still unmeasured | measure each colour from an unambiguous row, critical highlighting off and on |
| W25-3 | longer hash prefix | still a dictionary attack on short names | opaque labels, mapping kept out of git |
| W25-5 | keep the flipped assertion | hides the gap forever | Project's truth + expected-failure by name |
| W25-4 | amend the wave-24 commits | history is frozen under the hold | errata in wave25-complete.md |

## 4. Verification gate (before `wave25-complete.md` says done)

1. Every W25 item fixed with commit id, or deferred with a reason.
2. Fresh-tree gates on the final commit, numbers with their log paths; the name scan
   and the 8-hex token grep both clean on the tip and on every wave-25 commit.
3. A separate review pass over `git diff wave24..wave25`; findings fixed or listed.
4. CI per section 0.
5. Live same-file check against Project on a copy of `corpus/project_a.mpp`
   (captures local), a table of what still differs.
6. START-HERE, INDEX, `memory/coordinator.md` updated; the 16 decisions re-scored.
