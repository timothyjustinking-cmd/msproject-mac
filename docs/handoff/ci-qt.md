# Handoff — Qt shell CI: Windows + macOS join the matrix (wave 19)

_2026-10-23. Detail and evidence: `memory/ci-qt.md`. Workflow:
`.github/workflows/qt.yml`; validator (scratch): `build/ci-w19/validate-workflow.py`._

## (a) What changed

`qt.yml` was a single `build-qt` job on `ubuntu-latest`. It is now **one job id
with a three-row matrix** (`fail-fast: false`, `timeout-minutes: 45`) so the Qt
shell is compiled and rendered on all three desktop platforms:

| job | runner | Qt arch | generator | artifact |
|---|---|---|---|---|
| `linux-qt` | `ubuntu-latest` | `gcc_64` | `Unix Makefiles` | `qt-window-Linux-<sha>` |
| `windows-qt` | `windows-latest` | `win64_msvc2019_64` | `Ninja` | `qt-window-Windows-<sha>` |
| `macos-qt` | `macos-14` (arm64) | `clang_64` | `Unix Makefiles` | `qt-window-macOS-<sha>` |

- **Qt is pinned to 6.5.3 on all three**, installed by
  `jurplel/install-qt-action@v4` from the workflow-level `env.QT_VERSION`; no
  `actions/cache` (the action's own cache is off: this runs a few times a wave).
  Same configure flags everywhere — only `-G` differs:
  `cmake -S app_qt -B build-qt -G "<gen>" -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_PREFIX_PATH="<install-qt qt-root-dir>" -DCORE_WARNINGS_AS_ERRORS=ON`.
- **Linux keeps its old apt line** (`qt6-base-dev libgl1-mesa-dev
  libxkbcommon-dev`). The distro Qt it pulls is no longer linked; the
  `CMAKE_PREFIX_PATH` output points at 6.5.3, and a **guard step fails the job
  unless `Qt6_DIR` in `CMakeCache.txt` contains `6.5.3`**. That guard is why
  "pinned" is a fact and not a claim.
- **Windows** uses `ilammy/msvc-dev-cmd@v1` (`arch: x64`) then Ninja, the same
  pattern as `ci.yml`: the VS 2022 generator is not discoverable on
  `windows-latest` (first CI run 2026-10-22).
- **macOS** is `macos-14`, not `macos-latest`: it is arm64 today and Qt 6.5.3's
  `clang_64` package is universal (x86_64 + arm64, per Qt's package metadata),
  so it runs natively. Pinning the image keeps the OS/font stack from moving
  under the render tests; `macos-latest` would be the same architecture today
  but a different OS tomorrow.
- Tests are unchanged: `ctest --test-dir build-qt --output-on-failure
  --timeout 60`. The tests set `QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1`
  themselves; the workflow deliberately does **not** set them.
- Triggers and permissions are untouched: `workflow_dispatch` +
  `push: tags: ["v*"]`, `permissions: contents: read`. No cron.
- Workflow display name changed `qt-linux` → `qt-shell` (the file path did not
  change; job ids are `build-qt` with display names `linux-qt`/`windows-qt`/
  `macos-qt`). Revert the `name:` line if the old label is wanted.

## (b) Artifacts, exactly

One artifact per OS, `qt-window-${runner.os}-${github.sha}`, uploaded with
`if: always()` and `if-no-files-found: error`:

- `build-qt/qt-render-smoke.png` — 900x560;
- `build-qt/test-out/qt-render-document.png` — 900x560;
- `build-qt/test-out/qt-shell-window.png` — 900x560 (`qt_shell_offscreen`);
- `build-qt/test-out/qt-render-parity.png` — **1920x1032**, the file
  `scripts/parity_report.py` scores against `corpus/bench/msproject-gantt.png`.

`qt_parity_geometry` writes no PNG. Because `if-no-files-found` is per step (not
per path), a dedicated `test -s build-qt/test-out/qt-render-parity.png` step runs
first, so a lost parity render is named even when the 900px images exist. On a
configure failure there are no PNGs and both that step and the upload are red —
expected: the job is already red and the artifact genuinely does not exist. The
old artifact name `qt-window-<sha>` is gone; nothing in the repo used it.

## (c) YAML validation (run locally, PyYAML 6.0.3 / Python 3.14.4)

`python build/ci-w19/validate-workflow.py` normalises the YAML 1.1
`on:` → `True` trap, parses, then asserts triggers, permissions, matrix, steps,
paths and artifact settings. Output:

```
parsed: dict top-level keys: ['env', 'jobs', 'name', 'on', 'permissions']
job build-qt: ${{ matrix.name }} | runs-on: ${{ matrix.os }} | timeout: 45
  linux-qt    ubuntu-latest   arch=gcc_64               generator=Unix Makefiles
  windows-qt  windows-latest  arch=win64_msvc2019_64    generator=Ninja
  macos-qt    macos-14        arch=clang_64             generator=Unix Makefiles
steps: 10
  - Check out
  - Install Linux Qt dependencies [if: runner.os == 'Linux']
  - Install Qt ${{ env.QT_VERSION }} (${{ matrix.arch }})
  - Set up the MSVC environment [if: runner.os == 'Windows']
  - Configure
  - Prove configure used the pinned Qt
  - Build
  - Test (offscreen renders + parity geometry)
  - The 1920x1032 parity render was written [if: always()]
  - Upload the 900x560 and 1920x1032 renders [if: always()]
artifact: qt-window-${{ runner.os }}-${{ github.sha }} | paths: ['build-qt/*.png', 'build-qt/test-out/*.png'] | if-no-files-found: error
OK: qt.yml shape matches the wave-19 brief
```

Exit 0. File is LF-only, no tabs, 183 lines. No `git`, no dispatch, no local
Qt install (this box has no Qt; installing is out of scope), so **nothing
runtime has been exercised** — see (d).

## (d) Verified only by the first dispatch (everything runtime)

1. `install-qt-action@v4` can download Qt **6.5.3** for `gcc_64`,
   `win64_msvc2019_64` and `clang_64` (mirror/arch availability).
2. The action's `qt-root-dir` output expands correctly on all three runners
   (the README documents the output and the `QT_ROOT_DIR` env alias; v4 does
   **not** export `Qt6_DIR`). The guard should print a `Qt6_DIR` under a path
   containing `6.5.3`.
3. Windows: `msvc-dev-cmd` x64 + Ninja configures and builds, Qt 6.5.3's
   msvc2019_64 libraries link with VS 2022 (binary compatible by design), and
   `qoffscreen.dll` loads for ctest.
4. macOS arm64: the universal Qt dylibs link/run natively, and the offscreen
   plugin works with no window server.
5. Warnings-as-errors hold against Qt headers on all three
   (`/W4 /WX`, `-Wall -Wextra -Wpedantic -Werror`); Qt's imported targets are
   SYSTEM includes, so this should hold, but it is a prediction.
6. The numeric parity gate with Windows' Calibri / macOS' Arial / Linux's
   Liberation or DejaVu, and the 1920x1032 nine-column fit on each.
7. `if-no-files-found: error` combined with `if: always()` on a failed run.

## (e) Traps the coordinator should know

- **Do not use `Qt6_DIR`.** install-qt-action v4's documented interface is the
  `qt-root-dir` output / `QT_ROOT_DIR` env. An empty prefix on Linux silently
  falls back to the image's Qt (6.4.2 on ubuntu-24.04) and passes; the guard step
  exists for that reason. Keep it.
- **Windows prefix path is normalised, deliberately.** The action hands over a
  native `D:\...` path and the step runs under Git Bash, so the workflow
  converts it with `tr '\\' '/'` before passing `-DCMAKE_PREFIX_PATH`; CMake and
  Qt accept `D:/...` on MSVC. Don't "simplify" that back to the raw output unless
  you have watched it work on Windows.
- **aqt arch names:** for 6.5.3 the Linux arch is `gcc_64`; `linux_gcc_64` is
  the Qt ≥ 6.7 name (the wave-19 brief's example would request a package that
  does not exist). Windows is `win64_msvc2019_64` (< 6.8); macOS is `clang_64`
  (the only macOS desktop arch, universal).
- **PrintSupport:** no install change is needed. app_qt links `Qt6::Widgets`
  only today; PrintSupport is part of qtbase on all three (aqt) and
  `qt6-base-dev` ships its CMake config on Ubuntu. If the app gains a
  **Qt6::Svg** dependency, that is a separate module (`modules: qtsvg` for the
  action, `libqt6svg6-dev` on Linux) and a real workflow edit.
- **Fonts are not pinned by this workflow.** Windows resolves Calibri, macOS
  Arial, Linux Liberation/DejaVu, and the parity test tolerates that documented
  fallback. Pinning Qt is necessary but not sufficient for Q4's cross-OS pixel
  hashes; a bundled/installed font is the remaining piece.
- **`if-no-files-found: error` + `if: always()`** makes the upload step red on a
  configure failure too. That is intended; do not soften it to `warn` without
  keeping the parity `test -s` gate.
- **Bumping Qt is a UI change.** Qt 6.5 LTS support ended 2026-04-03; a bump
  changes Fusion metrics and needs the parity/golden numbers re-baselined, so
  edit `env.QT_VERSION` deliberately, not as maintenance.
- **`macos-14` will be retired eventually.** Move to the then-current arm64
  image and re-check fonts/renders, not just compilation.

## (f) Next three actions

1. **Coordinator (pre-dispatch):** confirm app_qt/CMakeLists.txt still links
   only `Qt6::Widgets` (or add PrintSupport, which needs no workflow change),
   then commit `qt.yml` + these docs and dispatch **once**. Do not commit
   `build/ci-w19/**` (scratch). Send the run log if any job is red.
2. **Triage by failing step, not by platform superstition.** Linux red at the
   pin guard = Qt install/prefix, not code. Windows red at configure =
   MSVC/Ninja/prefix quoting; red at compile with `C2220` = a genuine `/W4`
   warning (report it, never disable `/WX`). macOS red at link = architecture
   (should be impossible with the universal package); red in
   `qt_parity_geometry` = font metrics, an app_qt/coordinator decision.
3. **After green:** update `docs/ui-qt-port.md` §3/§4 and
   `docs/handoff/next-coordinator-prompt.md` (it still calls the job
   `qt-linux`), then decide Q4's cross-OS golden hashes — which need a pinned
   font on all three runners first.
