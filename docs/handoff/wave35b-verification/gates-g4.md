# Wave 35b G4: the Lows, one record per id

_Records only.  The code, tests and gates are in the commits cited; this file adds no
behaviour.  One finding id per commit, each commit carries its gate line (logs under
`build/w35b/`, git-ignored) and the wave 35b trailers.  Nothing in G4 is deferred._

## Status by id

| Id | Result |
|---|---|
| R35G0-6 | fixed, `51b3ef8` |
| R35G0-7 | fixed, `dacb8a7` |
| R35G0-8 | fixed, `0cc8d85` (documentation: the word at +128 is recorded as plausible, not measured) |
| R35G0-9 | fixed, `678fb45`; the uid class was already added by `7b2ad9a` (RW35-W6) |
| R35G0-10 | fixed, `6d1a801` |
| R35G0-11 | fixed, `dd59f78` |
| R35G0-12 | fixed, `ec07ab7` (see the review-claim note below) |
| R35G0-13 | fixed, `0438005` |
| R35G1-10 | fixed, `bdbcd7e` |
| R35G1-11 | fixed, `c3765b1` |
| R35G1-12 | fixed, `0699eac` |
| R35G1-13 | fixed earlier in the wave by `7b2ad9a` (RW35-W6: the five overtime keys and the project WBS key are refused off uid 0 and take only their measured payload) |
| R35G1-14 | fixed, `3edd463` |
| R35G2-9 | fixed, `ede7553` |
| R35G2-10 | fixed, `231ab61` |
| R35G2-11 | fixed, `db9001c` |
| R35G3-8 | recorded, not fixed (brief: no history rewrite); see below |
| R35G3-9 | fixed, `895b620` |
| R35G3-11 | fixed, `38700bc` |
| R35G3-12 | fixed, `7b0238d` |
| R35G4-3 | fixed, `ee7c63f` |
| R35G4-4 | fixed in W35b-1.7, `48630a4` |
| RW35-W6 | fixed, `7b2ad9a` |
| RW35-W7 | fixed, `6d3a1bd` |
| RW35-W8 | fixed, `a69943c` (W35b-2.1: Calculate Project writes the target once, after every record it touches is checked) |
| RW35-W9 | fixed, `0476516` (the byte-42678 mask is one byte wide in both directions) |
| RW35-W10 | fixed, `a1fa209` |
| RW35-W11 | fixed, `619bcea` |
| RW35-W12 | fixed, `425019a` (the Props14 note) with `e813c11` (RW35-W1) |
| RW35-W13 | left, as the row allows; see below |
| RW35-R6 | confirmed as intended, no change; see below |
| RW35-R7 | fixed, `1d25bf1` |
| RW35-R8 | fixed, `444731f`; the tests that stay on the one-look timer are named in the commit |
| RW35-R9 | acceptable as is; see below |
| RW35-R10 | resolved with RW35-R2, `e4a380d` (W35b-1.5) |

## Ids that needed a decision

**R35G3-8 (six wave 35 commits lack the Generated-By trailer).**  Recorded, not fixed.  The
commits are `9b33d6d`, `2634bb0`, `a46d9f9`, `ee4826f`, `639638c` and `2468736`; the brief
forbids rewriting history.  Every G4 commit (`W35b-4.*`) carries both trailers, checked by
script against `git log main..HEAD`.  A check of the whole of `main..HEAD` also found wave 35b
commits from other groups without the `Generated-By` line (for example `70b8a3b`, `733d6d3`,
`b179918`); those are outside G4 and not touched.

**RW35-W13 (`unbold_cell` is exported but not wired to any shell).**  Left, as the row allows
("leave, or remove until a shell needs it").  Its proofs stand; `wave35-complete.md` already
records it as a G3 writer that needs a mutation pass and a review, and no shell calls it.

**RW35-R6 (Project > Schedule > Move Project is a live prompt, pin 1).**  Confirmed as intended.
The review read the brief's "Respect Links, Move, Auto Schedule and Level All stay disabled" as
covering this button.  Those are four other controls (Task > Schedule > Respect Links, Task >
Tasks > Move, Task > Tasks > Auto Schedule and Resource > Level > Level All), all pin 0 in
`app_qt/ribbon_pin.tsv` (lines 60, 71, 69, 102).  Move Project (line 133) is a different button:
Project shows an actual-values question when the project has actuals, the measures file carries
its text, `show_move_project` (`app_qt/main_window.cpp`) shows that text verbatim and nothing
else, Yes answers "moving a project is not implemented in this port; the file was not changed",
and `project_dialog_gate_test.cpp` (the Move Project section) gates the prompt, its text and
its absence for a project with no actuals.  It writes nothing, so there is no save to refuse.

**RW35-R9 (the task-mode plus table-format or formula combination is refused at save, not at
queue time).**  Acceptable as the row says: the refusal has a specific needle in the app tests
and the Qt gate, the queue is kept and no file is written.  The wording of rule 4 ("refused up
front") is the only difference.

## Notes the coordinator needs

- **Review claim that did not hold (R35G0-12).**  The review said the older removal checks
  stay green when `push()` refuses every removal.  On this tree, with `push()`'s assignment case
  made to refuse every `unassign_resource` edit, 13 checks fail: the two new positive controls,
  six assign-remove checks and the "(the plan shows the assignment)" acceptance.  The commit
  adds the positive controls the row asked for and quotes the mutation.
- **Files other sessions had left in the tree.**  `ec07ab7` (R35G0-12) also swept in
  `docs/handoff/wave35b-verification/coord-gates-g3.md` and `review-g3.md`, which belong to the
  G3 session; they are records only and are not removed, because no history is rewritten.  The
  app gate run for `678fb45` (R35G0-9) started after the R35G0-10 `props_edit` edit was in the
  working tree, and the app gate run for `dd59f78` (R35G0-11) after the R35G0-12 test edit;
  each commit's message says so, and each of those ids was gated on its own commit.
- **No red run, with the reason.**  A comment, a README row or a COM script cannot fail a test
  here: `231ab61` (R35G2-10, header comments), `895b620` (R35G3-9, README hashes; the
  generating script asserted each hash equals `check_fixtures.py`'s, and `check_fixtures` is
  rerun in the gates), `a1fa209` (RW35-W10), `619bcea` (RW35-W11) and the RW35-W12 commit are
  comment or documentation only.  `3edd463` (R35G1-14) and `38700bc` (R35G3-11) change the
  Python COM oracles, which cannot run without Project; the check is that the script compiles
  and that the paths and the restore are as read.  `0cc8d85` (R35G0-8) is documentation.
  `db9001c` (R35G2-11) has a red run for its one behavioural text.  `444731f` (RW35-R8) is a
  test-only migration of a timing flake, which cannot be reproduced red; its commit shows by
  mutation that a migrated check still fails when the shell's behaviour changes.
- **RW35-R7 in detail.**  Collapse All and Expand All were not refused with a Show Level
  queued (a single row's toggle already was); they now are.  Undo of a queued Show Level needed
  no change, because the rebuild after the undo shows every row again, so the display follows
  the file.  Undo of a queued Sort leaves the session sort applied (the rebuild keeps it) while
  the file keeps its order, so the status line now says so.  A first version of the test reused
  the toggle's status text and could not fail for Collapse All; it uses text only the new
  refusals carry, and Collapse All's case is on the deep outline (it is enabled only while
  something is expanded).
- **RW35-R8 in detail.**  The five tests that answer a plain QMessageBox or QInputDialog moved
  to the scoped `gate::PromptAnswerer`.  The remaining one-look timers drive a bespoke dialog
  class and read or fill its own controls, which the answerer does not do.
  `wbs_code_gate_test.cpp` and `cell_bold_gate_test.cpp` open no prompt.
- **app/main.mm** cannot be compiled here; no G4 change touches it.

The gate status for every commit above is in `build/w35b/*-gates.status` (guard_names=1 is the
two pre-existing `wave35-g3-notes-generator` blobs in `main..HEAD`).
