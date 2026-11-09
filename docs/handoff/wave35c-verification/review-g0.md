# Wave 35c G0 review: the Linux build break and the two Calculate Project Highs

Range: `2ceb810..b538646` (4 commits: `9a2c079` 0.1, `7d923dc` 0.2, `743ad8b` 0.3, `b538646`
records).  Brief: `docs/handoff/wave35c-brief.md`, section 1 and "### G0".  Findings fixed:
R35bW-CI1 (`docs/handoff/wave35b-verification/coord-gates-close.md`), R35bG0-1 and R35bG0-2
(`docs/handoff/wave35b-verification/review-g0.md`).  Builder's records:
`docs/handoff/wave35c-complete.md` (G0 row) and `docs/handoff/wave35c-verification/gates-g0.md`.
Date: Mon 9 Nov 2026.

Reviewer: independent, adversarial, read-only.  Nothing in the repository was built, run or
edited.  Every statement below comes from reading the source, tests and records at `b538646`
and the commit messages.  Two checks ran outside the build: the `114/Props`, `214/Props` and
`Props14` streams of the committed calculate fixtures were compared with a scratch script
(olefile, read only), and the builder's git-ignored logs under `build/w35c/` were read.
Line numbers are at `b538646`.

Verdict: 0 Critical, 1 High, 2 Medium, 3 Low.

## Per item

| Item | Id | Commit | Verdict | Evidence |
|---|---|---|---|---|
| 0.1 | R35bW-CI1 | `9a2c079` | Closed in source; CI is its gate | `unique_edl_item` takes `char const* what` (`core/mppwrite/viewstate_outline.cpp:415-434`); the three callers pass literals (`:438-453`); `function` stays `std::string const&` but every caller passes the lvalue parameter.  Own sweep of `core/`, `app/` and `app_qt/` for reference-returning functions (split-line and trailing-return forms included): the rest are accessors with integer or no parameters, `require_prop` (label `char const*`, section an lvalue element, `summary_info.cpp:494, 601-609`), `shape_for` (lvalue strings at `varmeta_edit.cpp:382, 1049`) and the test helper `menu_named` (`app/menu_tests.cpp:110`, wave 10, built green on Linux since).  No other temporary bound to a reference parameter of a reference-returning function.  The messages are byte-identical. |
| 0.2 | R35bG0-1 | `7d923dc` | Closed for the reported case; two gaps (R35cG0-2, R35cG0-5) | Helper `calculate.cpp:114-142`; writer check `:717-729`, after 0.4 and before the pin; Props start kind-4 check `:646-652`.  Plan check `app/edit_model.cpp:3751-3768`; reader `core/mpp/reader.cpp` (kind 4 / 4 bytes, else not stored); `reader_adapter.h:232-233`.  Day 0 = Saturday 31 Dec 1983 is right (1 Jan 1984 was a Sunday; the control's Props start, day 15345, is Sunday 4 Jan 2026 21:00).  Boundaries: 17:00 exactly and Friday after 17:00 go to the next working day 08:00, 08:00 exactly is itself, before 08:00 is the same day 08:00; holidays cannot occur (calendar pinned with no exception block, both layers).  An unread or invalid Props start is refused in both layers.  Red logs show both layers' tests failing with the guards off (`build/w35c/0.2-red-app-ctest.log`: 4 FAILs). |
| 0.3 | R35bG0-2 | `743ad8b` | Closed in the writer; one untested sole guard (R35cG0-1), scope wrongly recorded (R35cG0-3), plan layer writer-only (R35cG0-4), test hygiene (R35cG0-6) | `check_props_pin` `calculate.cpp:278-351`, called at `:731`; table `calculate_props_pin.inc` (161 records, tail 85,408 B); generator `scripts/gen_calculate_props_pin.py`.  The red log has 158 "expected a refusal" lines (156 payloads, one kind, one tail byte) plus the no-file check, as claimed (`build/w35c/0.3-red-core-ctest.log`). |
| G0 records | - | `b538646` | Partly accurate (R35cG0-3, R35cG0-4) | Trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35c)` on all four commits (counted).  LF on every touched file.  No client or people's names, no personal paths. |

### 0.3: what the pin covers

Checked against the fixtures:
- **Exempt payloads.** Control vs Project's F9 save (`wave35-calculate-unlinked.mpp`) differs
  in exactly `0x02400003`, `0x02400024`, `0x02400029`, `0x024013c2` and one tail scalar (tag
  `0x01000100`, `0x142` to `0x17e`).  Control vs null save differs in the two GUIDs only, tail
  identical.  So the exempt list is right: the two the save writes, the 0.2 start, the two
  GUIDs.  None of the five can hold an option: two dates, the pending flag (checked to be
  `01 00` at `:599-610`) and two 16-byte GUIDs.
- **Bytes outside the hash.** Header words +0 and +4 are not pinned.  Both hold the stream
  length less 4 (`0x15aae` for an 88,754-byte stream), which the pinned record lengths and tail
  length already fix.  In the tail, only the one 4-byte value is excluded.
- **FNV-1a 64.** It is taken over every non-exempt payload and over the whole tail.  Combined
  with id, kind and length per record, nothing in `114/Props` falls outside the hash.

## Findings

### R35cG0-1 (High): three branches of `check_props_pin` are sole guards that no test can turn red

- Where: `core/mppwrite/calculate.cpp:281-284` (header words +8 and +12), `:296-301` (a record
  past the control's 161) and `:318-321` (fewer records than the control).  Tests:
  `tests/roundtrip_calculate.cpp:655-736` flip payloads, one kind (`frames[10]`) and one tail
  byte only.  `git grep` finds none of the texts "114/Props header", "is past the", "the
  measured control holds" in `tests/` or `app/`.
- What is wrong: each branch is the only check for its shape.
  - Header: no other check reads words +8 and +12.
  - Fewer records: drop the last framed record (`0x024013fb`, 14 bytes).  The loop breaks at
    the tail's first entry (id `0x01000001`).  The tail keeps its 85,408 bytes and its hash,
    so only `:318` refuses.
  - More records: append a record after the 161st.  Only `:296` refuses.  Without it,
    `kPropsPin[161]` is read past the end of the table (undefined behaviour).
  These are the shapes the pin exists for: an option stored only when it is not the default
  shows up as a record added or dropped, not a payload change.
- Failure: delete any of the three `if` blocks.  `mppwrite_roundtrip_calculate` stays green.
  The next file with an extra or missing trailing option record is saved with slack measured
  under the control's options.  This is High on the brief's scale (a guard no test can turn
  red).  No Project file is known to vary these words today, and the fix is tests only.
- Fix: add three writer tests with their own needles:
  - header word +12 changed: "114/Props header differs from the measured control";
  - last record removed: "114/Props holds 160 records, the measured control holds 161";
  - one 2-byte record appended after the last: "is past the 161 records of the measured
    control".
  Optionally also cover the tail length (`:325`) and the key count (`:340`).  The tail hash
  backs those two, so they are Low.

### R35cG0-2 (Medium): the two layers' 0.2 checks compare different Start words, so each word is guarded at one layer only

- Where:
  - Writer: `core/mppwrite/calculate.cpp:717-729` compares `rows.u32(row, task_row::kStart)`,
    FixedData +96 (`core/mppwrite/table.h:119`).
  - Plan: `app/edit_model.cpp:3756-3767` compares `task.start`.  That is
    `date32_from_packed(task.start.raw)` (`app/reader_adapter.h:171`), and the reader fills it
    from FixedData +104 (`core/mpp/reader.cpp:585, 1984`; the comment at `:575-582` records
    files where +96/+100 stay stale).
- What is wrong: the brief asks for one relation, refused in both layers.  The plan proves it
  for +104 and the writer for +96.  Neither layer checks the other word, and the writer does
  not relate +104 to +96 (R35bG0-7, still open for G2).  So the "both layers" claim in the
  0.2 commit, `gates-g0.md` and `calculate.md` section 8 holds for neither word on its own.
- Failure: take a pending file where a leaf's +96 is Monday 08:00 (the first working instant
  after the Props start) and its +104 is stale.
  - `mppwrite::save_calculate`, called on its own (the core API and the core tests' path),
    accepts it.
  - A plan read from such a file is refused, but only at the plan layer.
  - The reverse case is caught only by the writer.
  Through the app, `make_calculate_edit` runs both layers and the save's `source_digest` ties
  the edit to that file, so no wrong file is written from the shell today.  Medium: a refusal
  at one layer per word, where the rule needs both.
- Fix: in `read_calculate_shape`, refuse a leaf whose +104 differs from +96.  This is the Start
  half of R35bG0-7, and it makes the writer's check cover both words.  In the plan, either expose
  `calculated_start` (+96) on `TaskRow` and check it too, or record in section 8 that the plan
  layer checks +104 only and why.

### R35cG0-3 (Medium): the Props pin also pins per-file identity, so Calculate Project is now refused for every project except the control and its copies; the records misstate this

- Where: `core/mppwrite/calculate_props_pin.inc`; `calculate.cpp:310-314` (the refusal text);
  `docs/handoff/wave35c-complete.md:31-34`; `gates-g0.md` "Found, not fixed";
  `spec/writers/calculate.md:258-274`.
- Evidence from the committed fixtures (`114/Props`, read only):
  - `0x02400008` is a UTF-16 project title string: "Synthetic Calculate unlinked Fixture" in
    the control, "...linked..." and "...snet..." in the sibling controls.
  - `0x02401394` is a 16-byte SYSTEMTIME, 2026-11-09 02:01:42 in the control and :51 in the
    linked control.  The control's null save does NOT change it, so it is a per-file value
    (creation-like), not per-save noise.
  - Against `wave30-calculate-control.mpp`, `0x02400006`, `0x02400007` and `0x024013b1`
    (dates) also differ, as does one tail entry at tail offset 81,824.  That tail entry also
    differs between the unlinked control and its linked and snet siblings.
- What is wrong: every other project Project creates has its own title and timestamp.  Every
  such project is now refused: in practice the enabled save applies to the measured control
  file alone.  That is allowed under the rule (a refusal, nothing written), but the records do
  not say so:
  - The records describe the residual as "a file saved elsewhere ... until a second null-save
    pair shows them".  A null save cannot show these records as noise, because they do not
    change on a resave.
  - The user-visible text blames "the project's calculation options and defaults" for a title
    or a creation date.
  - A pending file the port itself saved after an enabled edit that writes `114/Props` is now
    also refused at Calculate Project.  Those edits are the WBS code (`0x0240138b`,
    `0x024013be`), field set (`0x02401392`, `0x024013c5`) and assignment or resource growth.
    The records do not mention this.
  - The 4(e) merge check passes only because it uses the control itself.
- Failure: a user opens their own pending two-task project (Project-built, manual calculation,
  one Duration edited).  Calculate Project is refused with "114/Props record 0x02400006 differs
  from the measured control; the project's calculation options ... are stored in these
  records".  The record says that is a narrow, temporary limit that a second null save would
  lift.
- Fix:
  - This wave (no refusal lifted):
    - state in `calculate.md` section 8, `wave35c-complete.md` and the coordinator's merge
      note that Calculate Project is enabled for the measured control file only;
    - name the per-file identity records seen across the four controls;
    - correct the "second null-save pair" remedy;
    - word the refusal so it does not assert that the differing record is an option (for
      example, "...differs from the measured control (an option, default or per-file value no
      measured pair varies)").
  - A measuring wave: a second control with every option at its default, created at another
    time under another title.  That separates identity records from options, so they can be
    exempted on evidence.

### R35cG0-4 (Low): the Props pin is writer-only and `push()` accepts a calculate edit the writer refuses; the stated reason is partly inaccurate

- Where: `app/edit_model.cpp:4369-4384` (`push()` runs `calculate_plan_refusal` and
  `calculate_slack_refusal` only); `spec/writers/calculate.md:276-283`; `gates-g0.md` 0.3 "Plan
  layer".
- What is wrong:
  - The brief allows this pin to be writer-only with a stated reason, so this is Low, not the
    Medium a one-layer refusal would otherwise be.  `make_calculate_edit` reaches the pin
    through `read_calculate_shape` (`:3838`), so the builder refuses.  Only `push()` of a
    hand-built edit does not, and the save then refuses it on `source_digest` (`:6504-6510`).
  - The stated reason has two slips:
    - "`save_calculate` compares the request's `source_digest`": `mppwrite::save_calculate`
      has no digest.  The comparison is in the edit model's save (`edit_model.cpp:6504`).
    - "a Props digest ... the writer would then have to trust over the file": a plan-side
      digest would be an extra refusal at the queue, not a replacement for the writer's pin.
      The reader already decodes `114/Props` (`reader.cpp:4057`), so the field is cheap.
- Fix: correct the two sentences in section 8.  Either carry a reader-computed `114/Props`
  digest on `PlanInput` (same exemptions) with a plan-needle test, or leave it to W35c-1.7
  (R35bG0-5) and say so there.

### R35cG0-5 (Low): `calculate_measured_first_working_start` accepts an out-of-range time word

- Where: `core/mppwrite/calculate.cpp:114-142`; header `calculate.h:88-97` ("Returns false for
  an invalid date").
- What is wrong: `unpack_date` gives `seconds_of_day = ticks * 6` for any 16-bit tick value, so
  a Props start with ticks of 14,400 or more (24:00 or later) counts as "at or after 17:00".
  It maps to the next working day 08:00 and returns true.  `0x02400002` is exempt from the pin,
  so a Sunday-dated start word with ticks `0xffff` passes 0.2 in the writer.  `save_calculate`
  then stamps that invalid word into every leaf's Fixed2Data +46.  Project does not write such
  a word, and the writer accepted any Props start before this wave, so this is not a
  regression.  It is the new helper's place to refuse it.
- Fix: return false when `at.seconds_of_day >= 86400`, and add the case to the relation table
  (`tests/roundtrip_calculate.cpp:738-772`).

### R35cG0-6 (Low): test hygiene in the 0.2 and 0.3 tests

- Where: `tests/roundtrip_calculate.cpp:698-707`; `calculate.cpp:646-652`; `:273-276`.
- What is wrong:
  - The record-kind test's needle "differs from the measured control" is shared by the header,
    count, payload and tail texts.  It reaches the right branch (only the kind changed, so the
    payload hash still matches), but it is not specific, as section 1 requires.
  - The new kind-4 check on `0x02400002` (`:646-652`) has no test.  It is redundant: the pin's
    kind check would refuse the same file later, with another text.
  - No test accepts a different Props start that still maps to the leaves' start (for example
    Saturday 00:00).  So removing `kPropProjectStart` from `props_payload_is_free` (`:273-276`)
    stays green.  That direction only over-refuses.
- Fix:
  - Use the record text as the needle: "is 0x02400010 (kind 1, 6 bytes), not the control's
    0x02400010 (kind 0".
  - Add a kind-change test for `0x02400002` with "date record for the project start".
  - Add an acceptance case: Props start Saturday 00:00, leaves unchanged.

## Clean

- **0.1.** The fix is the brief's, the messages are unchanged, and the sweep holds (see the
  table).  `function` still binds as a reference, but only to lvalues.
- **0.2 helper.**
  - Weekday arithmetic is right.
  - The `int` day cannot wrap before the `> 0xFFFF` check.
  - `is_date` refuses 0 and `0xffffffff`.
  - The lunch hour is not modelled.  The header says so, and it cannot matter, because a
    start inside the working day never equals an 08:00 leaf.
- **0.2 plan field.**
  - It is reader-filled and pinned through the reader.  The test loads the moved file with
    `load_plan` and asserts the value (`app/app_edits_calculate_tests.cpp:739-745`), so
    removing either `reader_adapter.h` line turns `app_edits_calculate` red.
  - A file without the record, or with another kind or length, reports not-stored, and the
    plan refuses it (`:752-757`).
  - The reader's decode checks kind 4 and length 4, and `for_each_props_record` bounds every
    payload.
- **0.2 needles.**
  - Plan: "for a leaf that does not start at the first working instant".
  - Writer: "uid 1 does not start at the first working" (clean plan, moved file).
  - Writer core test: "project start in 114/Props", which no earlier guard carries.
  - Each test reaches its own guard: only `0x02400002` moves, so the 0.3 and 0.4 checks pass.
    The 0.2 check runs before the pin, and `0x02400002` is payload-free.
- **0.3 pin.**
  - The generator reads only the committed control and its null save, and fails if the noisy
    set is not the two GUIDs.  `--check` was run.
  - The 156 payload flips each assert their own record's hex id.  `hex()` prints `0x%08X`,
    the same as the test's `snprintf`.
  - The GUID and Task-size acceptance cases prove the exemptions.
  - The measured unlinked pair still writes and matches (`build/w35c/7d923dc-W35c-0.3-*`:
    core 65/65, app 34/34, app_qt 52/52).
- **Calculation options in a payload-free record or outside the hash.** None can be there (see
  "0.3: what the pin covers").  Not checked, and not checkable without COM: whether any option
  lives outside `114/Props`.  `214/Props` (13,102 B) and the root `Props14` (604 B) are not
  pinned.  The finding's own premise, and the MPP decodes the port follows, put the schedule
  options in `114/Props`.
- **Records.**
  - One finding id per commit, and nothing deferred.
  - The 0.1 status "CI is its gate" matches the brief.
  - The gate lines match the logs and `7d923dc-W35c-0.3-gates.status`.
