# Review of the post-CLOSE fixes (bde04b3..b914006, branch wave33b)

Reviewer: independent, read-only.  Scope: CI-4 90d7eab, CI-5 52e15c8, CI-6 b96ebd5, R33b-1 b914006.

## Verdict

**PASS.** I found no Critical, High or Medium findings, and 4 Low.

Each fix is correct and complete for the failure it targets.  There is no behaviour change beyond what each commit claims, no gate is weakened, and no unmeasured write is enabled.  The ubuntu-asan job passed on b914006: ci run 40969809660 (headSha b914006b9a4d...) shows ubuntu-asan completed/success and windows-msvc completed/success.  ubuntu-gcc was still in progress when this review was written.

## Findings

| id | severity | file:line | claim | evidence | fix |
|---|---|---|---|---|---|
| RF-1 | Low | app/document_tests.cpp:773-774, 792, 800-801 | The new test's two hook "no write" checks (`!hook.view_state_written`, 214/CTable/Var2Data byte-identical) pass with or without the fix.  They do not discriminate. | The fixture's table 1 (Review) already has column 1 = Text1 0x0b400033 (fixture description, app_qt/tests/table_date_format_test.cpp:27-29).  The test replaces column 1 with field 0x0b400033 and title "Text1", so the unguarded set_table_column re-encodes the same bytes.  The RED log (build/w33b-coord/R33b-1-red.log:38-41, 83) shows exactly 4 failures: the refusal, the reason, queues nothing, and the hook warning.  Both no-write checks passed on the unguarded code.  The hook guard is still RED-covered by the warning check at :799, so the guard is tested, but the test's "byte-identical" claim proves nothing.  The test also compares only Var2Data, not all four 214/CTable parts (the earlier table-0-only test at :728 does the same). | Use a replacement that differs from the fixture's table-1 column, for example field 0x0b400034 with title "Text2", or column 0.  Confirm on the unguarded code that the no-write and byte-identical checks then go RED.  Optionally compare all four kCTableParts. |
| RF-2 | Low | app/document_tests.cpp:1777-1797 | The `#else` stub list (APP_TEST_DATA_DIR/APP_TEST_OUT_DIR undefined) has no `test_view_state_column_table0_only() {}`, but main() calls it unconditionally (:1809).  That configuration would fail to compile. | The new test is defined inside `#if defined(APP_TEST_DATA_DIR) && defined(APP_TEST_OUT_DIR)` (:446-1777).  The sibling `test_view_state_date_format_table0_only` does have a stub.  This is latent: app/CMakeLists.txt:262-265 always defines both macros for document_tests. | Add the empty stub next to `test_view_state_date_format_table0_only() {}`. |
| RF-3 | Low | core/mpp/reader.h:706, 787, 790 | This is the same unchecked float-to-int class as CI-6.  `static_cast<std::uint32_t>(x / 100.0 + 0.5)` runs on f64 fields read straight from file bytes, with no finite or range check.  A NaN, negative or out-of-range value is UB. | The sources are core/mpp/reader.cpp:2209, 2451-2452, 3525, 3688-3689 (f64_at of the file).  Callers in core: core/usage/usage_mpp.h:58 (work_minutes).  Callers outside core: app/reader_adapter.h:146, tools/mspdi_cmp/crosscheck.cpp:622 (work_ticks), tools/reader_dump/main.cpp:219, 250-251.  The fuzz targets do not call these accessors today (no match under tests/fuzz), so the asan job cannot hit them.  The core suite feeds only sane fixtures.  Out of scope for these commits and latent; reported because the brief asked for concrete instances. | Guard as eval.cpp's round_to_ticks does (isfinite, then a range check against the target type), or clamp before the cast. |
| RF-4 | Low | core/view/group.cpp:155 | Same class: `static_cast<int>(std::floor(number / 10.0))` runs before the 0..10 clamp at :156-161.  A NaN or \|number\| > ~2.1e10 is UB. | numeric_value (group.cpp) passes ValueKind::Number through unchecked.  An MSPDI Number goes through parse_double (mspdi.cpp:269-279), which rejects non-finite values but accepts 1e300.  No fuzz target reaches grouping, so this is latent. | Clamp `number` to [0, 100] (and reject NaN) before the cast. |

## Per commit

### CI-4 90d7eab: verified

- ci.yml:302, 313 and 322: configure (`-B`), build (`--build`) and test (`--test-dir`) all point at `build/asan`.  `git grep build-asan` at b914006 finds only the explanatory comment at ci.yml:297.  Nothing in docs/ or scripts/ references either name.
- Root cause confirmed.  CMakeLists.txt:213-223 passes `--workdir ${CMAKE_CURRENT_BINARY_DIR}/engine-diff/...`.  scripts/engine_diff.py:249-265 requires the work directory to resolve under `ROOT/build` (:166).  Under `build/asan` that holds.  The rule itself is untouched.
- No ctest walks the repository tree (no marker or EOL ctest), so moving the tree under build/ cannot pull build products into a source scan.  check_markers runs as a step before configure.
- ubuntu-asan passed on b914006, including engine_diff_harness and engine_diff_suite (they ran, because the job is green with no failures).

### CI-5 52e15c8: verified

- tests/gantt_tests.cpp:52-60: `equal` is a template that compares with `==`.  The old call was `const char*` against `char[6]`, which decays to an address compare.  It now compares `std::string` with `std::string`.  This is a test-only change.
- Sweep: I ran a regex scan of every `equal`/`check_eq`/`EXPECT_EQ` call with a string-literal expected value in tests/, app/, app_qt/ and tools/, multi-line calls included.  The remaining hits are `gantt::bar_label_text`, which returns `std::string` (gantt.h:485).  Every other `const char*`-returning core name function used in tests goes through `equal_string(const std::string&, ...)`, or through `check_eq(..., std::string(...))` (props_tests.cpp:248-252, 1013).  The `.name == "..."` hits in tests are all `std::string` members (formula fields.h:104, engine_diff_tests.cpp:1095, and others).  I found no other pointer compare.
- It would fail without the fix: it did, on clang/ASan (run 40965688565).  It passes on run 40969809660.

### CI-6 b96ebd5: verified

- core/formula/expr.cpp:190-199: the guard (`!isfinite || <= -9.2e18 || >= 9.2e18` returns an error at `token.position`) comes before the cast.  The rounding expression `static_cast<int64_t>(ticks < 0 ? ticks - 0.5 : ticks + 0.5)` is byte-for-byte unchanged, so every literal below the bound tokenizes exactly as before.
- With \|ticks\| < 9.2e18, \|ticks\| + 0.5 rounds to at most 9.2e18 < 2^63, so the cast is defined.
- The bound matches eval.cpp:66-76 and :105-115, which are the only other double-to-int64 casts in core/formula, and both are guarded.
- One narrow behaviour change: a literal with ticks in [9.2e18, 9.223e18) was previously converted (defined behaviour) and is now refused.  This is about 1.9e15 days, it is consistent with the evaluator's own bound, and it is not a finding.
- The test (tests/formula_tests.cpp:344-350) is meaningful.  1e16 * 4800 = 4.8e19 is past the bound.  The RED log shows the 3 new checks fail on the unfixed lexer (`formula_tests: 3 of 1252 checks FAILED`).  GREEN covers formula, fuzz_parsers and fuzz_deep_nesting_guard_256 and _1024 (4/4), and the CI asan run is green.

### R33b-1 b914006: verified (test partly meaningful, RF-1)

- Request, app/document.cpp:590: `edits.any() && table_index != 0` refuses with the date-format or column reason.
- Hook, app/document.cpp:352: `table_index != 0` refuses, after the `!edits.any()` early return at :343.
- Table 0 is unchanged, because both guards are keyed only on `table_index != 0`.  test_save_view_state_column (app/document_tests.cpp:808-850, table_index defaults to 0) still writes; app is 25/25 GREEN.
- Both save paths go through `apply_document_edits`: Document::save at :688 and the structural request hook at :762-768.  There is no third writer of 214/CTable.
- An empty request with a non-zero table index is still accepted at the request.  That is unchanged from before, and the hook no-ops on it.
- Callers grepped (app/, app_qt/, tools/, tests/):
  - app/main.mm:3738-3749 always sets `table_index = 0`.
  - app_qt/main_window.cpp:4404 refuses `selected_table() != 0` before the dialog opens, and the dialog (app_qt/dialogs.cpp:311-345) always sets `set_date_format = true`, so the Qt path was already refused for table != 0 before this commit.
  - app_qt/tests/dialog_gate_test.cpp:278-309 checks only that the dialog carries `table_index == 1`.  It never calls Document, so it is unaffected (app_qt 41/41).
  - The save_structural_edits callers in app/app_edit*_tests.cpp pass no view-state request (`{}`).
  - tools/ has no caller.
  - No legitimate table_index != 0 user breaks.
- No gate is weakened, and the commit only narrows what can be written (standing rule 1 upheld).
- Test: the request checks and the hook-warning check go RED without the fix.  The two hook no-write checks would pass, and did pass, without it (RF-1), so the "byte-identical" proof is vacuous for this input.

## Sweep for the same classes (asan scope: core suite plus fuzz targets)

- Pointer compare: none found beyond the fixed CI-5 site (method under CI-5).
- Unchecked float-to-int: RF-3 and RF-4 are the only concrete instances found.  Neither is reached by the fuzz targets.
- Checked and clean:
  - core/view/value.cpp:263-268 parse_iso_duration has a range check, and only digits and '.' are accepted, so the input is never NaN.  llround overflow is a domain error, not UB.
  - core/gantt/gantt.cpp:458-465 clamps NaN, below 0 and above 1 to [0, 1] before the cast.
  - core/mppwrite/actuals.cpp:1882 uses min/max, and NaN falls to 0.
  - core/mppwrite/field_values.cpp:905-909 has an isfinite and magnitude guard.
  - core/mppwrite/summary_info.cpp:376 takes a uint32 source, so it is bounded.
