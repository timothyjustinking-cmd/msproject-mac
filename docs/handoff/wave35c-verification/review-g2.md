0C/0H/1M/7L

# Wave 35c G2 review (ea54226..319d187)

Independent, read-only review of wave 35c group G2 (items 2.1 to 2.28). Code was read at
319d187 in a detached worktree. Nothing was built or run in the repo. Fixture bytes were read
with a read-only OLE scan of `tests/data`.

Line numbers are at 319d187.

## Findings

### R35cG2-1 (Medium): since 2.7, no test reaches the save_calculate scratch chain; R35bG2-2 is open again, and the test comment and the records say it is closed

- Where:
  - `core/mppwrite/calculate.cpp:1011-1015`: the 2.7 existence check, which runs before the chain.
  - `:1016-1052`: the scratch chain.
  - `tests/roundtrip_calculate.cpp:1162-1185`: the squat test.
  - `:1163-1166`: the comment "with the indirection reverted the save would succeed".
  - `docs/handoff/wave35c-verification/gates-g2.md:20` and
    `docs/handoff/wave35c-complete.md:117, 149`: 2.4 "fixed", with "two mutations" seen red.
- Failure scenario:
  - At 21b61d6 (2.4), the squat directory made the chain's first write fail, so the test went
    red when the appends wrote to `path`.
  - At d8cfb6c (2.7), the squat test was changed to expect the new up-front refusal ("already
    exists; it is not removed"). That refusal fires before any append.
  - Mutation: make the five `save_var_record` calls and `save_props_edit` write to `path`, and
    keep the 2.7 check. Then:
    - the squat test still refuses at :1011, so it stays green;
    - the pending-kind test (:1144-1160) is refused by the 0.3 Props pin, so it stays green;
    - the existing-target success test (:1209) writes the target directly and leaves no
      scratch file, so it stays green.
  - So the only guard against a failure mid-chain leaving a partly calculated target (one
    append written, no project finish) has no test that turns red, which is the defect
    R35bG2-2 named. The records and the test comment state the opposite.
  - Not High: no known input reaches a refusal after the first append today. The guard
    covers I/O failures and later regressions.
- Fix: add a test-only fault seam (for example a counter that throws after the first append),
  assert the existing target is byte-identical and no scratch file is left, and correct the
  comment and both record rows.

### R35cG2-2 (Low): the 2.3 row-0 constraint pin fires on files whose project row stores 1 at +64, and names a constraint the user did not set

- Where: `core/mppwrite/calculate.cpp:569-572`. It runs before the Props pin at `:824`.
- Failure scenario:
  - 33 of 332 committed fixtures store 1 at row 0 +64. They are wave 23 to 30 pairs, for
    example `wave29-percent-leaf-control.mpp` and
    `wave30-respect-links-auto-control.mpp`, which are not constraint pairs.
  - So +64 on the project row is not shown to be a user-set constraint.
  - Such a file is refused with "the project summary task carries a constraint", not with the
    Props-pin or shape reason. The refusal is right; the stated reason may be false.
  - No row-0 +182 was a date in any fixture, which is consistent with the deadline half.
- Fix: word the refusal as "the project row's ConstraintType word (+64) is not 0, as in the
  measured save", or move it after the Props pin.

### R35cG2-3 (Low): 2.9 pairs Fixed2Meta with the FixedMeta entry's index but does not check that the entry names the row

- Where: `core/mppwrite/calculate.cpp:477-491`. Compare `task_mode.cpp:577-583`, which checks
  the Fixed2Meta row word against record row+3.
- Failure scenario:
  - The 2.9 test (`tests/roundtrip_calculate.cpp:988-1019`) swaps the FixedMeta entries but not
    the Fixed2Meta entries. Calculate accepts that inconsistent pairing and reports "is
    inactive".
  - `read_task_file_shape` does not verify the pairing per row on this path.
  - Accept and refuse are unaffected, because every leaf must be active, but the blamed uid
    rests on an unchecked assumption.
- Fix: require `get_u32(f2_meta, f2_at + 4) == 64 * (row + 3)` before reading the flag. Name
  the mismatch the way `task_mode.cpp:580-582` does.

### R35cG2-4 (Low): 2.12 accepts two unmeasured style and level combinations on the six-task outline, and nothing above the core proves the per-shape refusal

- Where:
  - `core/mppwrite/viewstate_outline.cpp:470-475`: `alt_digest_measured_for` tests the shape
    only.
  - `spec/writers/view-state.md:238`.
  - `app/edit_model.cpp:3464`.
- Failure scenario:
  - The six-task outline was measured at Level 1 on the first style and at Level 2 on the
    second.
  - The code accepts Level 1 on the second style and Level 2 on the first, which are
    unmeasured. The spec row says "both are accepted there" without saying this rests on
    R35bG1-4's inference that the style bytes do not affect what Show Level writes.
  - R35bG1-4's fix asked for "a test that runs Show Level on the sort-flat-name-desc control".
    Only the core test does. No app test drives `make_show_level_edit` on that file, so the
    user-facing prefix ("the view Show Level would persist on is not the measured one") is
    unpinned for this case.
- Fix:
  - State the inference in the spec row (or pin the level per style as well).
  - Add an `app_edits_show_level` case on `wave35-viewstate-sort-flat-name-desc-control.mpp`
    with the builder prefix and "first text style only".

### R35cG2-5 (Low): `require_measured_view_block`'s new parameter defaults to fail-open, and no caller uses the default

- Where: `core/mppwrite/viewstate_sort.h:175-176` (`bool alt_digest_measured = true`). Both
  callers (`viewstate_outline.cpp:482, 581`) pass it explicitly.
- Failure scenario: a future view-state writer that calls this helper silently accepts the
  second digest for any shape, which is the R35bG1-4 defect again.
- Fix: drop the default so every caller must state what is measured.

### R35cG2-6 (Low): the 2.7 refusal says "already exists" when `symlink_status` fails for another reason

- Where: `core/mppwrite/calculate.cpp:1011-1015`.
- Failure scenario:
  - Suppose the parent directory cannot be stat'ed (access denied, or a path error).
  - `symlink_status(scratch, ec)` returns `file_type::none` with `ec` set.
  - The save is refused with "the scratch path ... already exists; ... move it aside".
  - The advice is wrong, and the error code is discarded. The branch is untested.
- Fix: check `ignored` first, and refuse with the OS message when it is set and is not
  "no such file".

### R35cG2-7 (Low): the "found, not fixed" note about the g3 fixtures misstates the fixtures

- Where: `docs/handoff/wave35c-verification/gates-g2.md:143` and
  `docs/handoff/wave35c-complete.md:157`. Both say "The 24 `wave35-g3-*` fixtures store
  project start 2026-01-04 13:00 (Props `781ef13b`)".
- What the fixtures show:
  - A raw byte scan of the 51 `wave35-g3-*` files finds `781ef13b` in 36 and `3831f13b`
    (2026-01-04 21:00) in the other 15. The 15 are the `baseline-set-nested` and
    `insert-milestone-*` triples, and no file has both values.
  - The 51 `wave35-viewstate-*` files all carry `3831f13b`, which agrees with the 2.22 README
    rows.
  - This is a substring scan, not a Props decode.
- Failure scenario: a later record fix that follows the note writes 13:00 into five pairs'
  rows that store 21:00.
- Fix: list the 12 pairs that store `781ef13b` and the 5 that store `3831f13b` by name.

### R35cG2-8 (Low): the R35bW-4 queue additions list as open ids that this branch has since closed, and the records disagree on 2.6

- Where:
  - `docs/START-HERE.md:26`, `memory/coordinator.md:17` and
    `docs/handoff/plan-waves27-30.md:403` list R35bW-CI1, R35bG4-2 to -6 and R35bW-4 to -6 as
    open. START-HERE has no wave 35c line.
  - `gates-g2.md:22` says 2.6 "first suggestion fixed", where `wave35c-complete.md:119` says
    "partly fixed".
- Failure scenario: a new session reads START-HERE first and re-queues ids that W35c-0.1 and G2
  have fixed at 319d187.
- Fix: at CLOSE, add a wave 35c line to START-HERE naming the ids 35c closed, and use one status
  for 2.6 in both records.

## Per-item verdicts (by reading)

Each item was checked for four things: whether it closes the finding in the layers named,
whether each new guard is red-tested, whether it refuses anything wrongly, and whether it adds
a new defect.

- 2.1: closed. `expect_push_refused` asserts the shared plan refusal's needle and push()'s bool.
  The push() call site is still proved only by the bool; this is the finding's own suggested
  fix.
- 2.2: closed. The Start2, Finish2 and RemainingDuration pins and the FixedMeta byte 8-11 pins
  are each tested in isolation, and byte 9 is tested at both values.
  - The fixture scan supports the pin: leaf entries carry `e3 bf/ff fd fe` in every wave 29 to
    35 file. The exception is `e3 bf ff fe` (byte 10 `ff`) on percent-complete files, which
    are refused elsewhere.
  - So the pin should not refuse a file from another Project session.
- 2.3: closed in the writer, and in the builder through `read_calculate_shape`
  (`edit_model.cpp:3985`). A hand-built edit has no digest. See R35cG2-2 for the wording.
- 2.4: was closed at 21b61d6 and reopened by 2.7 (R35cG2-1).
- 2.5: the statements match the code: `calculate_measured_late` at `calculate.cpp:907-916`.
- 2.6: closed. The digest needle is asserted. The dry run is unreachable through push() as
  recorded: a hand edit has `source_digest == 0`.
- 2.7: closed, with no deletion. See R35cG2-1 and R35cG2-6.
- 2.8: closed. `name_the_pair` now runs on caught text. The constructor's stream text is
  retargeted and tested with the full sentence. The loop is finite.
- 2.9: closed, and both sites are tested. See R35cG2-3.
- 2.10 and 2.11: closed.
  - `type_block_known` is set only at `reader_adapter.h:196`.
  - Pending rows (`file_row == 0`) are refused first.
  - The default-built row test needs the specific needle, so G7c turns it red.
  - There is no wrong refusal on reader-built rows.
- 2.12: closed as a pin. See R35cG2-4 and R35cG2-5.
- 2.13 to 2.16: closed.
  - The needles are per fixture and per handler.
  - The 2.16 Level 2 on {0,1,2,3,0} case is in range and hides rows, so only the shape guard
    refuses it.
- 2.17: recorded as a measuring-wave item.
- 2.18: all three looks now use `gate::PromptAnswerer`. About asserts `seen && question` and the
  text; Options asserts the title.
- 2.19 to 2.21: wording only. They match the code. The trim difference in 2.21 is correctly
  stated as the writer being stricter (`assignment_growth.cpp:798, 920`).
- 2.22: correct. All 51 viewstate files carry `3831f13b`.
- 2.23 to 2.27: the claims were checked against git:
  - the eleven named 35b G2 commits carry no `Generated-By` trailer;
  - `d60b3a6` adds `coord-gates-g3.md` (118 lines) and `review-g3.md` (140 lines) beside a
    14-line test change.
  - The queue text is incomplete in currency only (R35cG2-8).
- New defects:
  - none of the dangling-reference kind;
  - no read of a cleared vector;
  - no new uninitialised state (`alt_digest_measured_for` and `name_the_pair` are pure).
- Hygiene:
  - All 28 commits carry the wave 35c trailer.
  - The added lines name no client or person and add no personal path.

## Guards with no red-going test at 319d187

1. The scratch-chain indirection, `calculate.cpp:1016-1052` (R35cG2-1).
2. The `symlink_status` error branch, `calculate.cpp:1011` with `ec` set (R35cG2-6).
3. The builder-layer per-shape refusal for a five-task file on the second style,
   `edit_model.cpp:3464`. It has no app test; the core test only.
4. The Fixed2Meta row-word pairing in `check_unvaried_parameters` (`calculate.cpp:484`). It is
   not checked at all (R35cG2-3).
5. Not a guard but a record: no check pins a menu row's gap text (2.19), as the builder records.
