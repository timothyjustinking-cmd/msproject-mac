# Wave 19 — Qt shell CI (`.github/workflows/qt.yml`): working memory

_2026-10-23. Files I own this lane: `.github/workflows/qt.yml`,
`docs/handoff/ci-qt.md`, `memory/ci-qt.md`, scratch under `build/ci-w19/**`.
Nothing else was edited. No local Qt: this box has no Qt toolchain (the
coordinator's CI is the only Qt compiler), so every runtime claim below is
untested until the first dispatch._

## What the file is now

One workflow, one job id (`build-qt`), a 3-row `include` matrix, `fail-fast:
false`, `timeout-minutes: 45`, triggers unchanged (`workflow_dispatch` + tags
`v*`), `permissions: contents: read`. Display name changed `qt-linux` →
`qt-shell` because the file now builds all three platforms; the **file path is
unchanged** (`.github/workflows/qt.yml`). Job display names: `linux-qt`,
`windows-qt`, `macos-qt`.

| runner | job | aqt arch | generator | compiler/arch | artifact |
|---|---|---|---|---|---|
| `ubuntu-latest` | `linux-qt` | `gcc_64` | `Unix Makefiles` | GCC x86_64 | `qt-window-Linux-<sha>` |
| `windows-latest` | `windows-qt` | `win64_msvc2019_64` | `Ninja` | MSVC 2022 x64 | `qt-window-Windows-<sha>` |
| `macos-14` | `macos-qt` | `clang_64` | `Unix Makefiles` | AppleClang, **arm64** | `qt-window-macOS-<sha>` |

Qt version: **6.5.3**, defined once as workflow `env.QT_VERSION`, installed by
`jurplel/install-qt-action@v4` (no `actions/cache`; the action's own cache is
left off because this workflow runs a few times a wave).

## Decisions, with the evidence

1. **Qt 6.5.3 on all three, not "whatever the runner has".** `docs/ui-qt-port.md`
   §6: "Qt version drift changes Fusion metrics; pin the Qt version in CI." The
   6.5.3 package exists for all three aqt arches. It is a patch release, so
   `find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)` (app_qt/CMakeLists.txt
   line 23) is satisfied with room to spare.
2. **Linux arch is `gcc_64`, not `linux_gcc_64`.** install-qt-action's
   documented default for a Linux x64 desktop host is `linux_gcc_64` for Qt
   ≥ 6.7 and **`gcc_64` for Qt < 6.7**; aqt's own CLI docs say "gcc_64 or
   linux_gcc_64 for linux desktop". 6.5.3 predates the rename, and aqt builds
   the package name from the arch, so `linux_gcc_64` would request a package
   that does not exist. This is the one place the wave-19 brief's example
   ("e.g. ... linux arch linux_gcc_64") is wrong for 6.5.3 and was deliberately
   not followed.
3. **Windows arch `win64_msvc2019_64`.** aqt's Windows x64 name for
   `5.15 <= Qt < 6.8`; the 2019 and 2022 toolsets are binary compatible, so
   `windows-latest`'s MSVC 2022 links it. `win64_msvc2022_64` only starts at
   Qt 6.8.
4. **macOS arch `clang_64`, and `macos-14` (arm64) is safe.** `clang_64` is
   aqt's only macOS desktop arch. The Qt repository metadata for 6.5.3 says the
   package is **"Qt 6.5.3 Prebuilt Components for macOS (Universal,
   x86_64/arm64)"** (`qt.qt6.653.clang_64`, Qt's own
   `mac_x64/desktop/qt6_653/Updates.xml`), and Qt 6.5's supported-platforms
   table lists `arm64` with Xcode 13+. So the arm64 `macos-14` image needs no
   Rosetta and no `CMAKE_OSX_ARCHITECTURES`.
   Why `macos-14` rather than `macos-latest`: both are arm64 today, but
   `macos-latest` moves (macOS version, Xcode, system fonts). The render tests
   care about the OS's font stack, so the image is pinned. When GitHub retires
   macos-14, move to the then-current arm64 image deliberately and expect a
   font/metrics review.
5. **Linux keeps its apt line.** `qt6-base-dev libgl1-mesa-dev
   libxkbcommon-dev` are the packages the pre-wave-19 single job installed and
   they are still what the offscreen platform plugin/GL needs. The distro Qt
   they pull (6.4.2 on the current ubuntu-24.04 image) is **not** what CMake
   links now: the configure step passes the action's Qt root as
   `CMAKE_PREFIX_PATH`, and the next step fails unless `Qt6_DIR` in
   `CMakeCache.txt` contains `6.5.3`. That guard is the difference between
   "pinned" and "claimed".
6. **`CMAKE_PREFIX_PATH` comes from `steps.install-qt.outputs.qt-root-dir`,
   not `Qt6_DIR`.** install-qt-action v4's documented outputs are
   `qt-root-dir`/`QT_ROOT_DIR` (plus `qt-plugin-path`, `version`, `arch`, ...).
   The README lists a deprecated `Qt5_DIR` only; there is **no `Qt6_DIR`
   environment variable**. Using `${{ env.Qt6_DIR }}` (as older examples did)
   would expand to an empty string; on Linux that silently configures against
   the distro Qt. Hence the explicit output and the guard.
7. **Windows MSVC before configure**, `ilammy/msvc-dev-cmd@v1` with
   `arch: x64`, then `-G Ninja`. The `"Visual Studio 17 2022"` generator is not
   discoverable on `windows-latest` (ci.yml header, first run 2026-10-22).
   This is the same mechanism ci.yml uses.
8. **Same configure flags everywhere**, differing only in `-G`:
   `cmake -S app_qt -B build-qt -G "<generator>" -DCMAKE_BUILD_TYPE=Release
   -DCMAKE_PREFIX_PATH="<qt-root>" -DCORE_WARNINGS_AS_ERRORS=ON`.
   `CORE_WARNINGS_AS_ERRORS=ON` means `/W4 /permissive- /utf-8 /WX` on MSVC and
   `-Wall -Wextra -Wpedantic -Werror` on GCC/AppleClang (app_qt/CMakeLists.txt).
9. **Tests are not given the platform env.** `app_qt/CMakeLists.txt` wraps every
   test in `${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen
   QT_SCALE_FACTOR=1`; the workflow deliberately sets neither, so a test that
   forgets them fails instead of inheriting a pass.
10. **One upload step per OS.** Action inputs are per step, not per path, so
    `if-no-files-found: error` covers the whole set; the 1920x1032 parity file
    gets its own `test -s` gate before the upload so a missing parity render is
    named even when the 900x560 renders exist. `if: always()` is kept so a red
    run still yields the images.

## Artifacts: exactly what lands

Artifact name `qt-window-${runner.os}-${github.sha}` (runner.os =
`Linux`/`Windows`/`macOS`; full 40-char SHA). Paths:

- `build-qt/qt-render-smoke.png` — 900x560 (`qt_render_smoke`)
- `build-qt/test-out/qt-render-document.png` — 900x560 (`qt_render_document`)
- `build-qt/test-out/qt-shell-window.png` — 900x560 (`qt_shell_offscreen`)
- `build-qt/test-out/qt-render-parity.png` — 1920x1032 (`qt_render_parity`)

`qt_parity_geometry` writes no PNG by design. The parity PNG is the one
`scripts/parity_report.py` scores against `corpus/bench/msproject-gantt.png`
(git-ignored; never uploaded/committed). The old artifact name
`qt-window-<sha>` is gone — nothing in the repo referenced it
(`grep -r qt-window` over `*.md` and `scripts/` was empty), but a bookmark or a
human habit may.

## Verification actually run here

- `python build/ci-w19/validate-workflow.py` (PyYAML 6.0.3, Python 3.14.4),
  normalising the YAML 1.1 `on:` → `True` trap that memory/ci.md records.
  Output:

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

- Structural assertions in that script beyond parsing: triggers are exactly
  `workflow_dispatch` + `push.tags ["v*"]` (no PR, no schedule), permissions
  `contents: read`, one job, three matrix rows with the expected
  os/arch/generator, `fail-fast: false`, the Qt action is `@v4` with version and
  arch from the pinned env/matrix, MSVC step is Windows-only, configure has all
  five required flags and the documented output (and not `Qt6_DIR`), ctest does
  not set `QT_QPA_PLATFORM`, the parity gate names the right file, and the
  upload step has `always()`, `error`, the two real paths and the intended name.
- Ended `OK` (exit 0); LF-only, no tabs, 183 lines / 8,182 bytes.
- Not run: anything GitHub-side. No `git`, no dispatch (hard rule), no local
  configure/build/test (no Qt on this box, and installing it is out of scope).

## Traps a successor will hit

- **A wrong aqt arch fails at download, not configure.** The error names the
  missing `qt.qt6.653.<arch>` package; it does not say "arch syntax". For 6.5.3
  the three names above are the only correct ones.
- **`CMAKE_PREFIX_PATH` quoting on Windows.** The action hands over a native
  `D:\...` path and the configure step runs under Git Bash. Instead of relying on
  bash double-quote/backslash and CMake `-D` escaping, the step converts it with
  `tr '\\' '/'` before configure; CMake and Qt accept `D:/...` on MSVC. If it
  ever still fails, echo `$qt_prefix` before the cmake call.
- **The pin guard is load-bearing on Linux.** Remove it and a changed action
  output/blank expression silently builds against Ubuntu's Qt while every test
  still passes.
- **`if-no-files-found: error` + `if: always()`.** A configure failure means no
  PNGs, so the upload step is red too. That is by design: the job is red and the
  artifact genuinely does not exist. Do not "fix" it by going back to `warn`
  without also keeping the parity gate.
- **Fonts differ even when Qt does not.** The shell resolves Carlito → Calibri →
  Liberation Sans → DejaVu Sans → Noto Sans → Arial, then the platform family
  (`app_qt/shell_style.h`, `main_window.cpp::configure_application`). Windows
  gets Calibri, Linux Liberation/DejaVu, macOS Arial. The numeric parity test
  allows the documented fallback, but cross-OS **pixel hashes** (Q4's plan) will
  not match until the font is pinned/bundled on every runner. Pinning Qt is
  necessary, not sufficient, for the golden harness.
- **PrintSupport is not a workflow change.** app_qt links `Qt6::Widgets` only
  today. On Windows/macOS, PrintSupport is part of aqt's `qtbase`; on Ubuntu,
  `qt6-base-dev` ships its CMake config/headers. If Q3 adds
  `find_package(Qt6 ... COMPONENTS PrintSupport)` + `Qt6::PrintSupport` link,
  the workflow still works. A **Qt6::Svg** dependency would not: that is the
  `qtsvg` module (install-qt-action `modules: qtsvg`; Ubuntu
  `libqt6svg6-dev`) and would need a deliberate line.
- **Qt 6.5 LTS support ended 2026-04-03** (Qt supported-platforms table). That
  does not affect CI, but a security-driven bump means re-baselining Fusion
  metrics/goldens: treat it as a UI change, not a chore.
- **`macos-latest` will move.** The file uses `macos-14`; when that image is
  retired the replacement is arm64 and Qt 6.5.3 clang_64 still works, but
  re-check fonts/renders, not just "does it compile".
- **`timeout-minutes: 45`** is a job-level safety net (Qt download on three OSs
  can be slow); the per-test net stays `--timeout 60`.

## Open questions / what only a run can answer

- Does `install-qt-action@v4` resolve Qt 6.5.3 for all three arches today
  (mirror availability)? Local network fetch of the Linux/Windows
  `Updates.xml` failed with an SSL error in the confined shell; the macOS
  metadata was fetched successfully through the read-only web tool. The arch
  names themselves are documented and were cross-checked.
- Windows: does MSVC 2022 at `/W4 /WX` compile against Qt 6.5.3 headers cleanly
  (imported targets are SYSTEM, so it should), and does `qoffscreen.dll` load
  from the aqt install when `msproject_qt.exe` runs from `build-qt`?
- macOS: do the universal Qt dylibs link/run on arm64 with AppleClang from the
  macos-14 image, and does the offscreen plugin initialise with no window
  server?
- Do the parity numbers (row/header heights, 24 px/day, 0.459 split, nine
  columns at 1920) hold with Windows' Calibri and macOS' Arial? The test was
  written on Windows and has not run on Linux CI in this exact configuration
  either.
- Does the configure guard print `Qt6_DIR:PATH=...` on MSVC's CMake, and does
  `[[ ... ]]` behave in the image's Git Bash? Both are standard, neither is
  exercised here.

## Next three actions

1. **Coordinator: commit + dispatch.** Commit `.github/workflows/qt.yml`,
   `docs/handoff/ci-qt.md`, `memory/ci-qt.md` (and this lane's `build/ci-w19/**`
   is scratch, do not commit). Dispatch `qt-shell` once (`workflow_dispatch`) or
   tag. Send me the run id/log if any job is red.
2. **Triage by platform, not by symptom.** Linux red at "Prove configure used
   the pinned Qt" = Qt install/prefix problem, not code. Windows red at
   configure = MSVC/Ninja or prefix quoting; red at compile with `C2220` =
   a real `/W4` warning (report the exact warning, do not touch `/WX`). macOS
   red at link = arch (should not happen: universal package); red at
   `qt_parity_geometry` = font metrics, an app_qt/coordinator decision.
3. **After the first green run**: update the docs that still say Windows/macOS
   "join later" (`docs/ui-qt-port.md` §4, `docs/handoff/next-coordinator-prompt.md`
   line ~87 still says `qt-linux`), and only then decide Q4's cross-OS golden
   hashes — which require a pinned/bundled font first.
