# A7 -- fixtures, goldens and tests/data write discipline (wave-22 audit)

Read-only audit of HEAD e252041, 2026-10-23. Commands run: `python -I
scripts/check_fixtures.py`, `git ls-files`, `git log`, `git rev-parse`,
`sha256sum`, greps and reads. No build, no git write, nothing written outside
this file.

## Verdict

**PASS with four documentation/process drifts and one latent rewrite path.**
No test or tool writes under `tests/data` or `corpus/` when run the documented
way; every committed fixture matches HEAD and the pinned hashes; the three
goldens carry no client strings; W57 F3 and F1 are both resolved in the tree.
The gate's "3 documented hashes, 3 tracked .mpp" line is literally true but
counts things the reader would not expect, and the one golden writer
(`plan_recalc`) is guarded only by an env var and by `corpus/` being absent,
not by CI configuration.

Top findings, in severity order:

1. **A7-1 (Medium, process):** `plan_recalc` rewrites
   `tests/data/project_a_recalc_golden.tsv` unconditionally whenever
   `corpus/project_a.mpp` and `corpus/probe/project_a-recalc.mpp` exist and
   `MPP_NO_CORPUS` is unset (`tests/plan_recalc_tests.cpp:1276`). There is no
   `--update` flag and no compare-before-write. CI (`.github/workflows/ci.yml:98`)
   runs `ctest` with no `-E plan_recalc`; it is safe only because GitHub runners
   have no `corpus/`. On a self-hosted runner or any developer box with the
   corpus, the documented `ctest` line rewrites a committed file.
2. **A7-2 (Medium, house-rule breach in scripts):** four coordinator COM scripts
   open the committed fixture `tests/data/synthetic-base.mpp` in MS Project
   **in place with `ReadOnly=False`** (`scripts/oracle_w11.py:147,176,207`,
   `scripts/oracle_w12.py:139,166`, `scripts/oracle_w14.py:109-389` (8 sites),
   `scripts/msproj_bench.py:67`). None copies to `build/` first. This is the
   direct mechanism for the "Project-saved synthetic-base.mpp resurrected over
   the committed one" event that `scripts/check_fixtures.py:4-8` describes and
   that W57 F3 measured. START-HERE's rule ("never open a committed fixture in
   place") is not followed by its own tooling.
3. **A7-3 (Low, documentation):** the fixture count claim and
   `tests/data/README.md` are stale. "3 documented hashes" = 2 `.mpp` + the
   recalc golden TSV; "3 tracked .mpp" includes `analysis/test_project.mpp`
   (238,080 B, COM probe, no documented hash, outside `tests/data`). README:8
   still says `synthetic-base.mpp` is "The only `.mpp` that may be committed"
   and lists only 3 of the 19 tracked files; it omits `synthetic-calendars.mpp`
   and its hash, the four `.bin` fixtures, the links and recalc goldens and the
   eight oracle JSONs. `check_fixtures.py` is not run by any workflow.
4. **A7-4 (Low, stale text):** F1 is fixed (`core/formula/expr.h:98`
   `kMaxFormulaDepth = 64`, pinned by `tests/formula_tests.cpp:562`) but
   `tests/fuzz/CMakeLists.txt:176-185`, `tests/fuzz/README.md:108-115` and
   `docs/audit-findings.md:18` still describe 160 and a "residual open" ASan
   failure.
5. **A7-5 (Info):** `MPP_NO_CORPUS` is honoured by five test binaries only
   (`mpp_reader`, `plan_recalc`, `props`, `session`, `usage`); the eleven
   `mppwrite_*` round-trip tests, `inplace_patch`, `engine_diff` and the fuzzers
   gate on file existence alone. They never write the corpus, so this is a
   coverage note for the "prove the no-corpus path" claim, not a data risk.

## Q1 -- check_fixtures.py output, what it hashes, the third .mpp

Output (exit 0):

```
fixtures OK (3 documented hashes, 3 tracked .mpp)
```

What it hashes (`scripts/check_fixtures.py:20-30`, `DOCUMENTED`):

| path | pinned SHA256 |
|---|---|
| `tests/data/synthetic-base.mpp` | `55C109F2...6069` |
| `tests/data/synthetic-calendars.mpp` | `F486F0C3...17CA` |
| `tests/data/project_a_recalc_golden.tsv` | `36F156F6...0DB8` |

So "3 documented hashes" is two `.mpp` plus one TSV. The second loop
(`check_fixtures.py:59-69`) runs `git ls-files '*.mpp'` over the **whole repo**
and compares `git hash-object` with `HEAD:` for each; it never looks at `.bin`
files or the other goldens.

`git ls-files '*.mpp'` returns three files:

```
analysis/test_project.mpp          (238,080 B, blob 0704716d, SHA256 7127500f...a1c2)
tests/data/synthetic-base.mpp      (237,568 B, blob e2085e14)
tests/data/synthetic-calendars.mpp (blob 6b4983ea)
```

The third `.mpp` is `analysis/test_project.mpp`, committed in d563c79 and
described at `docs/status.md:42` as "generated via COM as a probe" (its creator
is `scripts/phase2d_create_project.py:12`, which hard-codes an absolute path).
It has no pinned SHA256 anywhere; only the HEAD-equality check covers it. It is
not under `tests/data` and no test reads it.

Does the count claim match reality? Numerically yes (3 and 3). Semantically it
over-states: a reader of the gate line and of `tests/data/README.md:8` ("The
only `.mpp` that may be committed") would infer one committed fixture with one
hash, or three `.mpp` fixtures under `tests/data` with three hashes. Neither is
the case. `git ls-files tests/data` shows 19 files: README, 3 TSV goldens, 1
golden .md, 8 oracle JSONs, 2 `.mpp`, 4 `.bin`.

`check_fixtures.py` is referenced in 20 handoff docs but in **no workflow**
(`.github/workflows/{ci,qt,app-macos}.yml` contain no call). It is a
pre-commit habit, not a gate.

## Q2 -- SHA256 of every tracked .mpp/.bin vs README; W57 F3

Working tree today (`sha256sum`), `git status tests/data analysis` clean:

| file | SHA256 (lower) | documented where |
|---|---|---|
| `tests/data/synthetic-base.mpp` | `55c109f2f62fd6b9f05789ab002c35991a424aab18c78f92e4200df7b3fc6069` | README:24, check_fixtures.py:22, engine_diff.py:116, build_calendar_fixture.py:59 -- all equal |
| `tests/data/synthetic-calendars.mpp` | `f486f0c3235bd8a592542e942141fe873c4262e6afa19e029a9c4c79386017ca` | check_fixtures.py:27, engine_diff.py:120 -- **not in README** |
| `tests/data/synthetic-props.bin` | `8739cdb83d96210ed3874ad14055f02c88b97f9c5ff74c343ad04d548b8e0334` | nowhere |
| `tests/data/synthetic-viewstate-ctable-base.bin` | `80573060bbacc24b656f9a6ff70bf907a421bee0677c2f99a8f7d3ab5993f4b9` | nowhere |
| `tests/data/synthetic-viewstate-ctable-col.bin` | `7b753dfe1c930cf95c6c2e3d75a821505a2b6f43c35fb42c30979d824e5ff52e` | nowhere |
| `tests/data/synthetic-viewstate-ctable-date.bin` | `e106e86452c37f4ea73263f043ae5ef1280beef46999c62271933960705eb3cb` | nowhere |
| `analysis/test_project.mpp` | `7127500fc21951377097c18fd7a4434243985fa7126970f9263d432cb81fa1c2` | nowhere |
| `tests/data/project_a_golden.tsv` | `a1b57d7e...69b6` | nowhere |
| `tests/data/project_a_links_golden.tsv` | `8c38c4f0...fad6` | nowhere |
| `tests/data/project_a_recalc_golden.tsv` | `36f156f618c94c92d70fba6d754340d374dde72e7d2ff136d1e22dbee5400db8` | check_fixtures.py:29 |

**W57 F3 is resolved.** The committed blob of `synthetic-base.mpp` has only
ever had one version (`git log -- tests/data/synthetic-base.mpp` = d563c79,
blob e2085e14), and that blob hashes to `55C109...6069`, which is exactly what
README:24 declares. W57 measured `092C51BD...` on 2026-10-22
(`docs/audit-findings.md:174-176`) because the **working copy** had been
replaced (the Dropbox resurrection that `check_fixtures.py:4-8` documents and
that A7-2 explains); the committed fixture was never wrong. Today working tree
== HEAD == README. README's own timeline ("generated 2026-10-22") versus the
git dates (every commit is 2026-10-23 07:21-12:44 after the "history scrub"
named in fbd4fe1) is consistent with a rewritten history, not a drift.

Residual (A7-3): README documents one hash of ten tracked binary/golden files;
`synthetic-calendars.mpp` and the recalc golden are pinned only in
`check_fixtures.py` / `engine_diff.py`. README:8 "the only .mpp that may be
committed" is false by two files.

## Q3 -- every writer under tests/data, its trigger, ctest targets, CI

Grep basis: `ofstream|fopen|open(...,'w')|shutil|copy_file|rename|golden|
--update|--record` over tests/, app/, app_qt/, core/, tools/, scripts/,
CMakeLists.txt (build dirs excluded). Writers whose destination can be
`tests/data`:

| # | writer | destination | trigger / guard |
|---|---|---|---|
| W1 | `tests/plan_recalc_tests.cpp:1276` `std::ofstream golden(PLAN_GOLDEN_PATH)` | `tests/data/project_a_recalc_golden.tsv` (`core/plan/CMakeLists.txt:99`) | inside `test_project_a`; runs whenever `PLAN_PROJECT_A_PATH` (`corpus/project_a.mpp`) and `corpus/probe/project_a-recalc.mpp` exist and `MPP_NO_CORPUS` is empty (`:1061-1067`). **Unconditional**: no flag, no diff-before-write, and the write happens after `context.equal` pinned-count checks, which do not abort, so a drifted engine still rewrites the file (with only matching uids). Text-mode ofstream => CRLF on Windows; the committed file is CRLF (confirmed with `cat -A`), no `.gitattributes`. |
| W2 | `scripts/engine_diff.py:944-948` | `tests/data/scenarios/<name>.oracle.json` | `--record` only (coordinator, needs pywin32 + `oracle_lock.py`). `--check/--suite/--selftest` read only (`:941-943`). Base hash verified before and after (`:188-214`). |
| W3 | `scripts/build_calendar_fixture.py:60,131` `app.FileSaveAs(str(OUT))` | `tests/data/synthetic-calendars.mpp` | manual coordinator run; refuses if OUT exists unless `--force` (`:77-79`); verifies base SHA (`:82-85`); copies base to `build/oracle-w19` before opening (`:89-90`). |
| W4 | `scripts/oracle_w11.py:147,176,207`, `scripts/oracle_w12.py:139,166`, `scripts/oracle_w14.py:109,156,186,210,249,302,367,389`, `scripts/msproj_bench.py:67` | `tests/data/synthetic-base.mpp` **opened in MS Project with `ReadOnly=False`** | manual coordinator COM runs. No copy-first (`grep shutil|copy` over the three oracle scripts: none). Saves go to `corpus/probe/` via `FileSaveAs`, but Project holds and can rewrite the opened file; `oracle_w14.py:29-30` even documents working around the fixture being "held by a sync client". This is A7-2. |

Explicit refusals found: `scripts/import_corpus.py:313-321` and
`scripts/import_robustness.py:68-75` refuse `--out` under `tests/data`;
`tests/engine_diff_tests.cpp:1233-1248` refuses output over input and refuses a
base hash mismatch. `tools/recalc/main.cpp:644-656` writes `--tsv`/`--attrib-json`
to any operator path with no guard, but no ctest or script invokes it with a
`tests/data` path.

Every other writer in tests resolves to a build-tree directory:
`APP_TEST_OUT_DIR` (`app/CMakeLists.txt:213`), `QT_TEST_OUT_DIR`
(`app_qt/CMakeLists.txt:89`), `ENGINE_DIFF_TEST_OUT` (`CMakeLists.txt:201`),
`FUZZ_OUT_DIR` (`tests/fuzz/CMakeLists.txt:137`), `MPPWRITE_TEST_OUT`
(`core/mppwrite/CMakeLists.txt:86`), `MPPWRITE_FIXTURE_MIRROR` =
`core/mppwrite/build/vs-w54/test-out` (`core/mppwrite/CMakeLists.txt:93-94`,
in-tree but matched by `.gitignore:36` `build/`; 0 tracked files under it),
and `mpp_reader_tests.cpp:3134` under `<repo>/build/g1/test-out`.

**ctest targets that can rewrite a golden:** exactly one, `plan_recalc`
(`core/plan/CMakeLists.txt:118`, WORKING_DIRECTORY repo root). `engine_diff`
(self-test) and all `mppwrite_*`, `app_*`, `qt_*`, `fuzz_*` targets write only
under their build dirs.

**Is plan_recalc excluded in CI?** No.
- `ci.yml:98`: `ctest --test-dir build -C Release --output-on-failure --timeout 60`
  -- no `-E`, comment at `:94-95` says "No exclusions". Safe on GitHub only
  because `corpus/` is absent, so `test_project_a` prints SKIP (`:1065-1067`).
  The `MPP_NO_CORPUS=1` step at `ci.yml:110-111` is `-R '^mpp_reader$'` and
  does not cover `plan_recalc`.
- `qt.yml:162,204`: configures `-S app_qt` and tests `build-qt`; `app_qt`
  adds only `app/` (`app_qt/CMakeLists.txt:35`), so `plan_recalc` is not in
  that tree.
- `app-macos.yml:127`: full ctest of `build-app`; same corpus-absent argument.
- `docs/START-HERE.md:70-72` states the rule ("never run casually ... use
  `MPP_NO_CORPUS=1`"); nothing enforces it.

**What `MPP_NO_CORPUS=1` changes:** any non-empty value makes `corpus_hidden()`
true in `tests/mpp_reader_tests.cpp:333`, `tests/plan_recalc_tests.cpp:108`,
`tests/session_tests.cpp:76`, `tests/usage_tests.cpp:94`, and
`tests/props_tests.cpp:1035`. Every corpus-gated case in those five binaries
prints SKIP instead of loading `corpus/` (reader: PROJECT_A/B, probe sweep,
new-task-mode, outline1, w3-o1, W31, W49, schedin; plan_recalc: PROJECT_A
(**which is the golden writer**), schedin, elapsed-lag, task-calendar, w12).
It has no effect on `mppwrite_*`, `inplace_patch`, `engine_diff`, `fuzz_*`
or the app/Qt tests (A7-5).

## Q4 -- read-write or in-place opens of corpus/*.mpp or tests/data/*.mpp

None in C++. Grep for `ios::in|ios::out`, `"r+b"`, `"rb+"`, `O_RDWR`,
`GENERIC_WRITE`, `QIODevice::ReadWrite`, `patch_in_place`, `inplace` across
tests/, app/, app_qt/, core/, tools/ finds only the in-memory patcher:

- `tests/inplace_patch.cpp:149` reads `corpus/probe/base.mpp`
  (`MPPWRITE_PROBE_DIR`, `core/mppwrite/CMakeLists.txt:77`) through
  `InPlacePatcher::from_file` (`core/mppwrite/inplace.h:53`), which loads the
  bytes; `patcher.save(out)` (`inplace.h:88`) writes
  `MPPWRITE_TEST_OUT/inplace-patched.mpp` (`inplace_patch.cpp:313-316`).
  Header comment `:22-23` matches the code. SKIPs when corpus absent (`:153-158`).
- `app/app_edit_tests.cpp:520-529`: the only "overwrite the opened file" case
  (`allow_source_overwrite = true`) acts on `APP_TEST_OUT_DIR/app-edit-own.mpp`,
  a copy; the same-path refusal is asserted at `:508-513`.
- Copies to build dirs: `app/app_edit_tests.cpp:309,520`,
  `app/app_edits_fields_tests.cpp:390,612`, `app/app_edits_links_tests.cpp:414,596`,
  `app/document_tests.cpp:1465`, `app_qt/tests/save_reopen_test.cpp:110`
  (`out_dir = argv[2]` = `QT_TEST_OUT_DIR`), `app_qt/tests/stored_table_test.cpp:311`,
  `tests/engine_diff_tests.cpp:2301` (`out_dir`), fuzz writer
  (`tests/fuzz/fuzz_writer_main.cpp:30` comment, `FUZZ_OUT_DIR`).

The in-place opens that do exist are the COM scripts in A7-2 (Q3 W4), which
hand the committed fixture to MS Project writable.

## Q5 -- writes into corpus/ other than corpus/probe/

None found. Code: no writer targets `corpus/`; the "w54-corpus-*.mpp" and
"w37-corpus-pinned.mpp" names (`tests/roundtrip_assignments.cpp:1047,1060`,
`tests/roundtrip_save_flow.cpp:603`) go through `out_path()` into
`MPPWRITE_TEST_OUT`. Scripts: every `FileSaveAs` destination is `PROBE =
corpus/probe` (`oracle_probe.py:39`, `oracle_w3.py:35`, `oracle_w9.py:34`,
`oracle_w10.py:49`, `oracle_w11.py:32`, `oracle_w12.py:29`, `oracle_w14.py:46`,
`oracle_writer.py:37`), `docs/analysis/oracle` (`oracle_w3.py:34`,
`oracle_writer.py:36`, `oracle_patch_variants.py:34`), `build/e1/engine-diff`
(`engine_diff.py:102`), or `tests/data` (W3). `import_corpus.py` /
`import_robustness.py` default `--out` to `corpus/imported/*.tsv` (their
docstrings `:14`, `:14`) -- operator-chosen inventory output, not under
`corpus/probe`, but also not a client `.mpp`. `oracle_lock.py` writes only
`<repo>/.oracle.lock` (`:39,67-84`).

## Q6 -- goldens derived from the client corpus; client strings

Yes, all three are derived from `corpus/project_a.mpp` (README:9;
`project_a_golden.md:3`; recalc header `plan_recalc_tests.cpp:1166-1168`
"Source: corpus/probe/project_a-recalc.mpp"). Scan (counts only, nothing quoted):

| file | rows | columns | text cells | hashed names | capitalised multi-word |
|---|---|---|---|---|---|
| `project_a_golden.tsv` | 83 | uid, id, name, duration_min, start, finish, priority, pct | 83 (name) | **83/83** match `sha256:[0-9a-f]{16}` | 0 in rows |
| `project_a_links_golden.tsv` | 88 | succ_uid, pred_uid, type, lag_ticks | 0 | n/a | 0 |
| `project_a_recalc_golden.tsv` | 83 + 6 comment lines | uid, start, finish, slack_min, critical | 0 | n/a (no name column) | 0 in rows; 1 in comments = the generated phrase "Save As" (`plan_recalc_tests.cpp:1168`) |

No non-name cell in any golden contains four or more consecutive letters.
`project_a_golden.md:8` documents the hashing (`sha256:<16 hex>`) and `:3`
the read-only COM export. No client strings leak. The 8 oracle JSONs are
scenario measurements on the synthetic bases (`engine_diff.py:106-128`), not
client-derived.

## Q7 -- W57 F1 residual (formula depth guard vs ASan)

**Resolved in the tree; documentation stale.**

- `core/formula/expr.h:98`: `inline constexpr int kMaxFormulaDepth = 64;`
  enforced at `expr.cpp:489,501,685` and `eval.cpp:542,618`.
- `tests/formula_tests.cpp:562-563` pins the constant to 64 with the ASan
  measurement in the message ("72 parsed, 80 overflowed"); `:555-558` records
  the rationale.
- `git log -S"kMaxFormulaDepth = 64" -- core/formula/expr.h` = 5fb93c0 ("Wave
  13 ... fuzz/ASan", dated 2026-10-23 07:21 because the history was rewritten;
  `git log --since=2026-10-22 -- core/formula` therefore returns 37629e1 and
  5fb93c0 and is not a useful discriminator).
- Stale: `tests/fuzz/CMakeLists.txt:176-185` ("kMaxFormulaDepth = 160 ...
  RESIDUAL ... fail in build\vs-w57-asan until lowered to 64"),
  `tests/fuzz/README.md:108-115` (same), `docs/audit-findings.md:18,29-35`
  ("residual open"). The guard tests at 256/1024 remain enabled
  (`tests/fuzz/CMakeLists.txt:187-194`).
- Not re-verified under ASan here (no build permitted); the claim rests on the
  constant and the unit test, both of which now say 64.

## Suggested actions (for the coordinator, not performed)

1. Guard W1: make the golden write opt-in (`PLAN_WRITE_GOLDEN=1` or an
   `--update-golden` argv) and otherwise compare against the committed file;
   add `-E '^plan_recalc$'` or `MPP_NO_CORPUS=1` to every documented `ctest`
   line that may run where `corpus/` exists.
2. Fix A7-2: have `oracle_w11/w12/w14.py` and `msproj_bench.py` copy
   `tests/data/synthetic-base.mpp` under `build/` before `FileOpen`, as
   `build_calendar_fixture.py:89-90` and `engine_diff.py:916` already do.
3. Rewrite `tests/data/README.md` to list all 19 tracked files with the two
   `.mpp` hashes, the recalc-golden hash and the `.bin` provenance; add
   `analysis/test_project.mpp` to `DOCUMENTED` or drop it from the "tracked
   .mpp" count; run `check_fixtures.py` in `ci.yml`.
4. Update the three stale F1 texts to 64 / closed.
