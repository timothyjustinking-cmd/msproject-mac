# Wave 35c G1 (the Mediums and Lows): coordinator verification (Tue 10 Nov 2026)

The gated commit is `ea54226` (`WAVE-35c-G1-COMPLETE`), the end of items 1.1 to 1.11.  That is
12 builder commits, `c408a07` to `ea54226`, on `wave35c` from `b538646`.  The builder's own
record is `gates-g1.md`.

Gates ran in the verify worktree from 23:48 on 9 November to 00:09 on 10 November 2026.  The
worktree was detached at `ea54226`, with:
- fresh build trees;
- Release, with `CORE_WARNINGS_AS_ERRORS=ON`;
- the corpus junctioned in read-only.

Logs are in `build/verify-w35c/g1/` (git-ignored).  A first launch at 23:47 failed before
building anything: the OUT path in the copied script had lost a backslash.  The script was fixed,
the build trees cleared again, and the gates relaunched.

| gate | result |
|---|---|
| core configure / build | exit 0 / exit 0, 0 warnings |
| core ctest `-E plan_recalc` | **64/65 on the first run**; see below |
| `plan_recalc_tests` no corpus / with corpus | exit 0 / exit 0 |
| engine self-test; suite | exit 0; exit 0 |
| M2 `recalc --compare` | exit 0 |
| app_qt | 52/52, 0 warnings |
| app | 34/34, 0 warnings |
| fixtures; markers | OK; OK |

- **The core miss is a process-start failure, not a test failure.**  `mppwrite_roundtrip_outline`
  reported "Process not started ... [unknown error]" (BAD_COMMAND, 0.00 s) on a freshly linked
  exe.  Run alone at 00:12, the same exe passed: 1757 checks, 0 failures, 13 expected failures
  (`g1/12b-outline-rerun.log`).  This is one run with its own number.  The first-run figure stays
  64/65.
- **Scans:** taken from the builder's gate line.  The coordinator scans run on the whole range at
  CLOSE, before the records are committed.

## Mutations (`build/verify-w35c/mutate-g1.py`, `g1/mutations.log`)

27 mutations, one at a time, all anchors count 1.  **25 of the 27 are red in at least one lane.**
The worktree was clean afterwards, and all 9 lanes were green again.

| id | guard switched off | result |
|---|---|---|
| Q1 | 1.1 `view_change_refusal`, the Show Level branch | outline persist red (24); sort persist green |
| Q2 | 1.1 `view_change_refusal`, the Sort branch | sort persist red (8); outline persist green |
| Q3 | 1.1 the summary-row toggle refusal | red (11) |
| Q4 | 1.2 the queued level re-shown on every rebuild | red (4) |
| Q5 | 1.2 `clear_redo` in `apply_outline_level` | red (7) |
| Q6 | 1.3 the edit gate in `setData` | red in both lanes (5, 5) |
| Q7 | 1.3 the `set_edit_gate` call | red in both lanes (5, 5) |
| Q8 | 1.3 the Task Information refusal | red in both lanes (3, 3) |
| Q9 | 1.3 the Cut refusal | sort persist red (2) |
| **Q10** | **1.3 the Paste refusal** | **green in both lanes** |
| **Q11** | **1.3 the Fill Down refusal** | **green in both lanes** |
| Q12 | 1.4 `clear_redo` in `apply_sort_choice` | red (5) |
| E1 / W1 | 1.5 stored free slack, plan / writer | app red (7) / core red (5); the Qt gate is green, since it runs only the measured file |
| E2 / E3 | 1.7 plan: FixedMeta entry / VarMeta key set | app red (6) / app red (6) |
| W2 / W3 | 1.7 the shared byte compare / the writer's call site | core and app red / core red |
| R1 / R2 | 1.7 reader: entry never copied / keys never filled | app red (42) / app red (90) |
| E4 / E5 | 1.11 plan: tie / ending leaf not the first row | app red (3) / app red (5) |
| E6 / R3 | 1.10 plan: the removal's every-row session byte / its reader flag | assignment red (7) / red (20) |
| W4 | 1.9 writer: only the target row read | core growth red (9), app red (5) |
| E7 / E8 | 1.10 plan: Sort's measured levels / Show Level's view check | sort red (12) / show level red (7) |

- **Q10 confirms R35cG1-3.**  Paste's guard has no test, and it is the only guard on that path.
- **Q11:** Fill Down's guard is untested, but Fill Down fills only Start, which is read-only, so a
  second layer refuses it.  It is recorded as redundant, not as a gap.
- **Q2 is red only through the cell paths:** the Sort branch also serves the edit gate.  Its
  outline refusals under a queued Sort (Expand All, Collapse All, the row toggle, the summary
  toggle) are still untested, as R35cG1-4 says.

## Independent review (`review-g1.md`)

A read-only agent reviewed `b538646..ea54226`: 0 Critical, **2 High**, 2 Medium, 3 Low.  Both
Highs were confirmed by the coordinator reading `ea54226`.  Both are missing behaviour, not a
guard a mutation can switch off.

- **R35cG1-1 (High):** R35bW-2 is still open on the Undo route.
  - The steps: Sort Name descending, Ctrl+Z, then a refused or cleared sort, then Ctrl+Y.
  - Ctrl+Y re-queues the withdrawn sort, and File > Save writes it under a different display.
  - The cause: `clear_redo()` in `apply_sort_choice` sits inside `if (had_queued_sort)`, which is
    false after the Undo.  The rebuild also never re-applies a queued Sort.
- **R35cG1-2 (High):** Cut and Paste never clear the structural redo list.
  - `cut_selection` and `paste_selection` call `after_cell_change()` directly.
    `after_cell_commit` clears the list first.
  - The steps: a queued Sort or Level, Ctrl+Z, a cut or pasted cell, then Ctrl+Y.  This re-queues
    the view edit over unsaved grid edits, bypassing the 1.5 rule.  So 1.3 is only partly closed.
- **R35cG1-3 (Medium):** Paste's guard is untested (Q10 confirms).  The record's reason for no
  test does not hold: the window's own Copy, or the clipboard, can supply the block.
- **R35cG1-4 (Medium):** the "one guard" is not applied consistently.
  - A session Level or Sort chosen while the other kind is queued is accepted.
  - Yet the Sort branch refuses the outline actions, and none of those refusals is tested.
- **Lows:**
  - R35cG1-5: the 1.10 commit subject overstates Show Level's two layers.
  - R35cG1-6: the two removal layers read different resource rows: the plan reads live rows only,
    the writer every physical row.
  - R35cG1-7: a second whole-table VarMeta scan per task in the reader.

Items 1.2 and 1.5 to 1.11 close their findings in the layers each names.  The review found no
dangling reference, cleared-vector read, uninitialised field or off-by-one, and no change that
would make the writer produce a file Project reads differently.

## Merge rule

G1 closes R35bG3-1, R35bG4-1, R35bG0-3/4/5, R35bG1-1/2/3 and R35bG2-1 in source.  It closes
R35bW-1 and R35bW-2 only in part, and adds **R35cG1-1 and R35cG1-2 (High)**.  Both join
R35cG0-1 at the front of the next fix wave, with R35cG1-3 and -4 among its Mediums.  G2 runs on,
uninterrupted.
