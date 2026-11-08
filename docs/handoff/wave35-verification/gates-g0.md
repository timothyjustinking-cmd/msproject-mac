# Wave 35 G0: coordinator verification (Mon 9 Nov 2026)

The gated commit is `b8238bb` (`WAVE-35-G0-COMPLETE`).  Gates ran in the verify worktree,
detached at `b8238bb`, with fresh build trees, Release, `CORE_WARNINGS_AS_ERRORS=ON` and the
main corpus junctioned in read-only, from 01:48 to 02:17.  Logs are in `build/verify-w35/g0/`
(git-ignored).

| gate | result |
|---|---|
| core configure / build / ctest `-E plan_recalc` | exit 0 / exit 0, 0 warnings / 62/62 |
| `plan_recalc_tests` no corpus / with corpus | 266 / 512 checks |
| engine self-test; suite | 265 checks, 0 failures; expected failing set reached (`constraint-mso`) |
| M2 `recalc --compare` | exit 0, no unexplained rows |
| app_qt / app | 49/49 / 31/31, 0 warnings |
| fixtures; markers | OK; OK |

## CI on `b8238bb`

| workflow (run) | result |
|---|---|
| `ci` 42491589430 | ubuntu-gcc, ubuntu-asan, windows-msvc green |
| `qt-shell` 42491592188 | linux-qt green; windows-qt green; **macos-qt red** |

macos-qt fails in two tests: `qt_assign_resources_gate` ("an assigned task is offered the
removal") and `qt_prompt_hook` ("the late question's title is recorded", twice).  The earlier
run 42489754746 on `42d51f7` failed the same way.  W35-0.3's change (`b0bbd5f`, poll for the
dialog) did not fix CI-4.

**CI-4 cause (R35G0-1, confirmed by the coordinator at source).**  The prompt is seen and
answered on macOS.  The failing checks compare the message box's window title
(`app_qt/tests/assign_resources_gate_test.cpp:326`, `app_qt/tests/prompt_hook_test.cpp:51`),
and a macOS `QMessageBox` carries no title.  The repo already records this at
`app_qt/tests/backstage_gate_test.cpp:449-453`, which identifies its box by text for that
reason.  In the CI log only the title checks fail; "a late QMessageBox is seen as a question"
passes.  The fix is test-only: identify and assert the box by its text, keep the title check
for the input dialog, and print the diagnostic on any failure.

## Mutations (coordinator, `build/verify-w35/mutate-g0.py`)

Each mutation was applied in the verify worktree.  The test target was rebuilt and its ctest
run by exact name, then the file was restored and the target re-run green.

| mutation | result |
|---|---|
| M1: writer call site of `check_resource_calendar` removed | red, 58 FAIL lines (add and removal) |
| M2: `check_resource_calendar` returns at entry | red, 58 FAIL lines |
| M3: edit-model base-calendar refusal off | red, 14 FAIL lines (builder and `push()`, assign and removal) |
| M4: edit-model stored-block refusal off | red, 14 FAIL lines |
| M5: reader's Standard extra-block count forced to 0 | **green in core, app and Qt** (R35G0-3) |

M5 confirms R35G0-3: no test drives the reader field `calendar_base_extra_blocks`.  The save
is still refused, because the writer checks the Standard calendar's blocks itself (M1 and M2
show that the writer gate is tested).  It stays a Medium.

## Independent review (`review-g0.md`)

A read-only agent reviewed `1b432b6..b8238bb`: 0 Critical, 0 High, 5 Medium, 8 Low.

- **R35G0-1:** CI-4's cause is the title check (above).
- **R35G0-2:** the project default calendar and week are checked only in the writer; the
  edit model has no project-calendar input.
- **R35G0-3:** the reader's Standard extra-block count is untested (M5).
- **R35G0-4:** the settled add copies the first existing assignment's row without checking
  that resource's calendar or the row's dates.  The gap is writer-only, because the edit model
  refuses an assign on a task that already has one.
- **R35G0-5:** the effort-driven deferral (R34b-2) rests on a corpus search.  No pair was
  measured, although measuring one is allowed.
- **Lows R35G0-6 to -13:** listed in the review.

The review verified W35-0.1 as complete in the writer: base calendar, stored blocks, flags,
the Standard row, and the project calendar and week, for add, settled add and removal.  It
also verified R34b-3 to R34b-6, RW34b-2/3, RC-W1, RC-W2 and RC-W8 as complete, and RC-W6 and
RC-W7 as complete to the brief.  W35-0.3 is incomplete.

## Merge rule

G0 meets the review half of the rule: no Critical and no High.  It does not meet the CI half
while macos-qt is red.  Waves 34, 34b and 35 G0 stay unmerged until R35G0-1 is fixed and
macos-qt is green.  Brief section 4(e), the Project reopen check of the enabled saves, is still
the coordinator's and runs before any merge.  R35G0-1 to -5 are queued as the first items of
the next fix group, since G1 is running in the main tree.
