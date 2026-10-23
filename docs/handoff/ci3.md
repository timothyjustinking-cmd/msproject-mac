# Handoff — parallel Linux build (wave 21, stream C6)

_2026-10-23. Workflow: `.github/workflows/qt.yml`. Scratch (git-ignored, do not
commit): `build/ci3-w21/validate-workflow.py`, `validate.log`,
`qt-before.yml` (pre-edit snapshot), `patch.py`, `build-step.sh`.
This box has no Linux, no runner and a sandbox that blocks Git Bash startup, so
**every runtime claim below is marked unverified**;
no `git`, no dispatch, no installs._

## (a) What changed

Exactly one step — `Build` — and the comment block above it. The whole-file diff
is a **single hunk, +14 / −1**; every other step's `name`, `if`, `uses`, `with`
and `run` is unchanged, and the YAML's semantic difference is exactly one leaf:
`jobs.build-qt.steps[6].run`.

```diff
--- .github/workflows/qt.yml (before)
+++ .github/workflows/qt.yml (after)
@@ -179,9 +179,22 @@
             exit 1
           fi
 
+      # Linux was the slow job: Unix Makefiles with no -j, about 18 min
+      # serial (wave-20 open item 11).  --parallel 2 is the honest budget
+      # for the job's 2-core sizing -- it is compile/link bound, and an
+      # unbounded -j only oversubscribes and adds contention.  Windows
+      # (Ninja) keeps the exact command it had: Ninja already starts as
+      # many compilers as the machine allows.  macOS is also Make and
+      # could take the same flag later, but this change is scoped to the
+      # one job it was asked for.
       - name: Build
         shell: bash
-        run: cmake --build build-qt --config Release
+        run: |
+          if [ "${{ runner.os }}" = "Linux" ]; then
+            cmake --build build-qt --config Release --parallel 2
+          else
+            cmake --build build-qt --config Release
+          fi
 
       # The tests set QT_QPA_PLATFORM=offscreen and QT_SCALE_FACTOR=1 themselves
       # (${CMAKE_COMMAND} -E env in app_qt/CMakeLists.txt).  Deliberately not
```

Why a condition inside the one step, and not an `if:` on a step: an `if:` would
need a second Build step, which either changes the 17-step list the wave-20
handoff and the validator pin, or leaves a skipped no-op step. The matrix was
off the table by the brief (do not change it). `${{ runner.os }}` is substituted
by Actions before the shell runs, so on `windows-2022` and `macos-14` the
`else` branch executes **the previous line character for character**; the
validator asserts that verbatim equality.

Why `2`: the brief's budget (the job is compile/link bound, an unbounded `-j`
only oversubscribes). `cmake --build … --parallel N` passes `-j N` to the
Makefiles generator since CMake 3.12; the runner's CMake is 3.31-era and
`app_qt/CMakeLists.txt` already requires 3.21, so the flag cannot be too new.
No new action, no matrix key, no cache, no install.

Deliberately **not** done: macOS stays serial (same generator, same benefit, but
this lane was scoped to Linux); Windows stays as-is (Ninja already parallelises).

## (b) Parsed validation (run here, PyYAML 6.0.3 / Python 3.14.4)

`python build/ci3-w21/validate-workflow.py` parses the before snapshot and the
edited file (normalising the YAML 1.1 `on:` → `True` trap), asserts every
wave-20 invariant, and proves the change is the only semantic difference. Exit
**0**. The snapshot is that pre-edit copy; if the scratch tree is wiped, it can
be regenerated from the committed parent, before this change is committed, as
`git show HEAD:.github/workflows/qt.yml > build/ci3-w21/qt-before.yml`.
Output:

```
parsed: dict top-level keys: ['concurrency', 'env', 'jobs', 'name', 'on', 'permissions']
  [ok  ] name is qt-shell
  [ok  ] triggers are workflow_dispatch + push tags v*
  [ok  ] permissions: contents: read
  [ok  ] concurrency group/cancel
  [ok  ] exactly one job
  [ok  ] job name is the matrix name
  [ok  ] runs-on is the matrix os
  [ok  ] timeout-minutes 45
  [ok  ] fail-fast false
  [ok  ] three-row matrix unchanged
  [ok  ] matrix has exactly three rows
  [ok  ] no extra matrix keys
  [ok  ] QT_VERSION is 6.5.3
  [ok  ] env has only QT_VERSION
  [ok  ] one install-qt step
  [ok  ] install-qt-action@v4 with pinned version/arch, cache off
  [ok  ] no other action pins a Qt version

steps: 17
   0. Check out
   1. Install Linux Qt dependencies [if: runner.os == 'Linux']
   2. Install Qt ${{ env.QT_VERSION }} (${{ matrix.arch }})
   3. Set up the MSVC environment [if: runner.os == 'Windows']
   4. Configure
   5. Prove configure used the pinned Qt
   6. Build
   7. Test (offscreen renders + parity geometry)
   8. The 1920x1032 parity render was written [if: always()]
   9. Package the Windows build [if: runner.os == 'Windows']
  10. Package the macOS build as a .app bundle [if: runner.os == 'macOS']
  11. Package the Linux build as an AppImage [if: runner.os == 'Linux']
  12. Run the packaged Linux AppImage (offscreen smoke) [if: runner.os == 'Linux']
  13. Upload the Windows build [if: always() && runner.os == 'Windows']
  14. Upload the macOS bundle [if: always() && runner.os == 'macOS']
  15. Upload the Linux AppImage [if: always() && runner.os == 'Linux']
  16. Upload the 900x560 and 1920x1032 renders [if: always()]
  [ok  ] step count 17 and exact order
  [ok  ] Linux apt line still installs libxcb-cursor0
  [ok  ] Linux apt step is the only runner.os == 'Linux' apt step
  [ok  ] configure flags unchanged (Release, CMAKE_PREFIX_PATH, warnings-as-errors)
  [ok  ] configure picks up the matrix generator
  [ok  ] pinned-Qt guard intact (Qt6_DIR greps QT_VERSION, exits 1)
  [ok  ] ctest step unchanged
  [ok  ] parity render gate intact and always()
  [ok  ] Windows package unchanged (windeployqt + cmake -E tar)
  [ok  ] macOS package unchanged (plist + macdeployqt + ditto)
  [ok  ] Linux package keeps QMAKE resolved AND verified
  [ok  ] Linux package keeps APPIMAGE_EXTRACT_AND_RUN=1
  [ok  ] Linux package keeps linuxdeploy --plugin qt --output appimage
  [ok  ] AppImage smoke unchanged (executes the AppImage, greps rendered 900x560)
  [ok  ] four artifact uploads, names/paths/modes/ifs unchanged

artifacts:
  msproject-windows-${{ github.sha }}           path=msproject-windows.zip if-no-files-found=warn
  msproject-macos-${{ github.sha }}             path=msproject-macos.zip if-no-files-found=warn
  msproject-linux-${{ github.sha }}             path=dist/msproject-linux-x86_64.AppImage if-no-files-found=warn
  qt-window-${{ runner.os }}-${{ github.sha }}  path=build-qt/*.png + build-qt/test-out/*.png if-no-files-found=error

semantic differences between before and after: 1
  jobs.build-qt.steps[6].run
  [ok  ] exactly one semantic difference and it is the Build step's run
  [ok  ] step 6 is named Build
  [ok  ] every step name/if/uses/with is byte-identical to before
  [ok  ] before: no --parallel anywhere
  [ok  ] after: exactly one step run mentions --parallel
  [ok  ] the single --parallel 2 command is inside the Build step
  [ok  ] raw mentions are the command + its comment (2), not two commands
  [ok  ] Linux branch is the --parallel 2 line
  [ok  ] non-Linux branch is the original command, verbatim
  [ok  ] no other step's run gained a flag or a shell condition

before: 22125 bytes, sha256 67eabe909f271c61…, CR=442 LF=442 TAB=0
after: 22835 bytes, sha256 8375b6a7cfa0eb1d…, CR=455 LF=455 TAB=0
  [ok  ] the whole-file diff is a single hunk

--- whole-file unified diff (1 hunk expected): +14 -1 ---
(same hunk as (a))

OK: qt.yml matches wave-20 shape, and the only change is Build's Linux --parallel 2
```

The matrix and Qt pin the validator checks (unchanged from wave 20):

| row | os | aqt arch | generator | Build command on that row |
|---|---|---|---|---|
| `linux-qt` | `ubuntu-latest` | `gcc_64` | `Unix Makefiles` | `cmake --build build-qt --config Release --parallel 2` |
| `windows-qt` | `windows-2022` | `win64_msvc2019_64` | `Ninja` | `cmake --build build-qt --config Release` |
| `macos-qt` | `macos-14` | `clang_64` | `Unix Makefiles` | `cmake --build build-qt --config Release` |

The ordering the wave-20 CI2 work established is asserted step-by-step and did
not move: `Build → Test (ctest) → 1920×1032 parity render → Package Windows →
Package macOS → Package Linux → AppImage smoke → 4 uploads`. So a red ctest or
parity run still skips packaging (a package of a red run is never published) and
the render artifact still uploads on `always()`.

## (c) Verified here vs. unverified (and why)

**Verified**

- The YAML parses; one job, three-row matrix, Qt `6.5.3`, 17 steps in the same
  order, the same `if:` on every conditional step, the same four artifact
  names/paths/`if-no-files-found`, the same configure flags, pinned-Qt guard,
  ctest command, parity gate, three package steps and AppImage smoke.
- The parsed semantic difference between the pre-edit file and the edited file
  is exactly one leaf (`jobs.build-qt.steps[6].run`), and the whole-file text
  diff is one hunk of +14 / −1.
- The non-Linux branch contains the pre-edit command verbatim
  (`cmake --build build-qt --config Release`), so Windows and macOS behaviour is
  unchanged by construction, not by inspection.
- Byte hygiene: 455 lines, no tabs, 455 CR / 455 LF (CRLF) — the same convention
  the file already had before the edit (442/442 in the snapshot), so the edit
  introduced no line-ending churn.

**Not verified — cannot be, in this sandbox**

- **Shell syntax.** Git Bash cannot start here: `bash -n build/ci3-w21/build-step.sh`
  dies with `fatal error - couldn't create signal pipe, Win32 error 5` before
  reading the file, and there is no other `sh`. The `if … then … else … fi` was
  reviewed by hand and is POSIX sh; PyYAML confirms the block scalar's content is
  exactly the six lines shown (the validator compares it as a string). **No
  shell-level check was performed.**
- **No dispatch** (the coordinator dispatches at the wave close), so: no run, no
  green Linux job, no timing, and no proof that the runner's `make` accepts the
  `-j 2` CMake passes. All of that is the next run's evidence.

## (d) Expected wall-clock effect, and its uncertainty

Wave-20 measured the Linux job at ~18 min serial Make (open item 11) and the
Build step is the dominant term (configure ≈ 1 min, Qt install ≈ 2–3 min, ctest
and the AppImage packaging are minutes more; none of those change).

- **Best case, 2 workers, compile-bound:** a near-2× Build step (Amdahl-limited
  by the serial parts: the single executable's link, `moc`/`uic` fan-in, the
  four-font Qt resource compile per target, and the make jobserver ramp-up).
  Realistic range: **Build 1.3–1.8× faster; Linux job ~18 min → ~12–15 min**.
- **Uncertainty 1 — the runner's core count.** GitHub's standard public-repo
  runner has been 4 vCPU / 16 GB since 2024 (private repos stay 2-core), and
  this repo is public per the workflow header. If the runner really is 4-core,
  `--parallel 2` is conservative and the gain lands at the low end; `--parallel 4`
  would be the follow-up, but that is a deliberate separate change, not this one.
  The brief's budget is 2, and 2 is what was written.
- **Uncertainty 2 — what the long pole is.** If the Build step turns out not to
  dominate the Linux job on the runner (Qt download/install and the AppImage
  packaging are candidates), the job-level gain is smaller than the Build-level
  gain. Only the step timings from the next run answer this.
- **Uncertainty 3 — no direct log evidence of `-j 2`.** CMake does not echo the
  flags it hands to `make`, so the proof is the step duration versus run
  `39898703443`. A `warning: jobserver unavailable: using -j1` in the log would
  mean `-j 2` was dropped (it should not appear: CMake invokes one top-level
  make).

## (e) What the coordinator must watch on the next run

1. **Linux `Build` stays green.** The only new failure mode is a build-graph
   ordering bug that serial make hid: symptoms are `No rule to make target …`,
   a missing `moc_*.cpp`/`ui_*.h`, or a header consumed before it is generated.
   `app_qt/CMakeLists.txt` has no `add_dependencies` and no `add_custom_command`
   (only `qt_add_executable`/`qt_add_resources`, which emit real Makefile
   dependencies), and the Windows job already builds in parallel — Ninja starts
   one job per core by default — and has been green through wave 20, so a race
   is unlikely but is the first thing to check if it goes red. The fix would be
   a dependency edge in `app_qt/CMakeLists.txt` (Q6 owns that file), not a revert.
2. **Timing.** Compare the Linux `Build` step and the total job duration with
   `39898703443`. Unchanged Build time = the flag did not take effect or the
   build is not compile-bound; report which.
3. **Do not read a red ctest/parity as a build-flag regression.** Those steps are
   untouched and run after Build; the numbers to expect are the wave-20 ones
   (ctest green, parity 9 comparable / 0 failing). A flaky test would be the
   signal, and a render diff would be a real bug — neither is expected from `-j 2`.
4. **Line endings before committing.** `qt.yml` is **CRLF on disk** (455/455,
   no tabs) while the repo's other workflows are LF (`ci.yml` 0 CR,
   `app-macos.yml` 1 CR) and there is no `.gitattributes`. This predates the
   edit (the snapshot taken before any change was already 442/442 CRLF); it
   contradicts CI2's "LF-only" note from wave 20, so the on-disk file was
   rewritten by something after that. Check
   `git diff --numstat -- .github/workflows/qt.yml`: **expect `14 1`**. If it
   reports ~`455 442`, the blob is LF and the worktree CRLF — normalise the file
   to LF (the byte replace below), re-run the validator (it should still pass,
   +14/−1), then commit. Do not commit a whole-file line-ending diff. The
   normalisation is content-neutral:

   ```bash
   python -c "from pathlib import Path; p = Path('.github/workflows/qt.yml'); t = p.read_bytes(); p.write_bytes(t.replace(bytes([13, 10]), bytes([10])))"
   ```
5. **Nothing else moved.** No matrix row, no Qt pin, no cache, no step reorder,
   no package/upload change — the validator asserts all of it. Do not commit
   `build/ci3-w21/**` (`build/` is git-ignored).

## (f) Files

- `.github/workflows/qt.yml` — the `Build` step and its comment only.
- `docs/handoff/ci3.md` (this file), `memory/ci3.md`.
- Untracked scratch, not a deliverable: `build/ci3-w21/`.
