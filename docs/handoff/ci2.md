# Handoff — Qt shell CI: runnable macOS and Linux packages (wave 20, stream ci2)

_2026-10-23. Detail and evidence: `memory/ci2.md`. Workflow:
`.github/workflows/qt.yml`; scratch validators: `build/ci2-w20/validate-workflow.py`
and `build/ci2-w20/check_embedded.py`._

## (a) What changed

`qt.yml` already published `msproject-windows-<sha>` (windeployqt + `cmake -E
tar`). Wave 20 adds the equivalent runnable package for the other two jobs, so
every platform has one download a user can run:

| job | runner | package step | artifact | file inside |
|---|---|---|---|---|
| `windows-qt` | `windows-2022` | `Package the Windows build` (unchanged) | `msproject-windows-<sha>` | `msproject-windows.zip` (exe + Qt DLLs) |
| `macos-qt` | `macos-14` | **new** `Package the macOS build as a .app bundle` | **new** `msproject-macos-<sha>` | `msproject-macos.zip` → `MS Project Shell.app` |
| `linux-qt` | `ubuntu-latest` | **new** `Package the Linux build as an AppImage` | **new** `msproject-linux-<sha>` | `dist/msproject-linux-x86_64.AppImage` |
| all | — | render artifact (unchanged) | `qt-window-<runner.os>-<sha>` | four PNGs |

- **Header comment rewritten**: it said only Windows gets a package; it now lists
  the three packages, says the macOS bundle is assembled in the workflow (not by
  CMake), and states the signing/FUSE limits up front.
- **One dependency line changed**: the Linux apt step now also installs
  `libxcb-cursor0`. Qt 6.5's `libqxcb.so` links it and Ubuntu's Qt 6.4-era
  `qt6-base-dev` does not pull it; linuxdeploy copies that plugin into the
  AppImage, so without the library the packaging step fails while resolving the
  plugin's dependencies. It is not a build-time dependency.
- **Windows package is byte-for-byte the same commands**; only its comment
  changed.
- **A new smoke step** (`Run the packaged Linux AppImage (offscreen smoke)`)
  executes the AppImage after the test gate and before the uploads.
- **Ordering**: all three package steps and the smoke step run after the test
  gate (`Build → ctest → parity render → Package Windows/macOS/Linux → smoke →
  uploads`), so a packaging problem cannot skip ctest and hide the concurrent
  app_qt tests. A red test/parity run therefore skips packaging (the package
  uploads warn); the render artifact still uploads.
- **Unchanged on purpose**: the three-row matrix (`ubuntu-latest`,
  `windows-2022`, `macos-14`), Qt `6.5.3` via `jurplel/install-qt-action@v4`
  with the same arches, the same configure flags, the pinned-Qt guard, the
  `ctest --test-dir build-qt --output-on-failure --timeout 60` step, the
  `qt-render-parity.png` gate, the render artifact name/paths/`error`, the
  `workflow_dispatch` + `v*` triggers, `permissions: contents: read`, the
  concurrency block. No caching, no new actions. `app_qt/**` was not touched:
  the bundle is built by the workflow because a CMake `MACOSX_BUNDLE` target
  changes window behaviour and another stream owns that file.
- `ctest` is still count-agnostic — nothing here names a test or a count, so the
  concurrent app_qt work (more tests, dialogs) needs no workflow edit.

Steps went from 10 to 17; the file is 428 lines, LF-only, no tabs.

## (b) The packaging commands, exactly

### Windows (unchanged)

```bash
mkdir -p dist
cp build-qt/msproject_qt.exe dist/
windeployqt --release --compiler-runtime --no-translations dist/msproject_qt.exe
cmake -E tar cf msproject-windows.zip --format=zip dist
```

### macOS — `.app` built in the workflow, then macdeployqt

```bash
app="dist/MS Project Shell.app"
mkdir -p "$app/Contents/MacOS"
cp build-qt/msproject_qt "$app/Contents/MacOS/msproject_qt"
cat > "$app/Contents/Info.plist" <<'PLIST'
... CFBundleName "MS Project Shell", CFBundleIdentifier
    org.personal.msproject-shell, CFBundleExecutable msproject_qt, APPL,
    version 1.0/1, LSMinimumSystemVersion 11.0, NSHighResolutionCapable ...
PLIST
macdeployqt "$app" -always-overwrite -verbose=2
test -x "$app/Contents/MacOS/msproject_qt"
test -d "$app/Contents/Frameworks/QtWidgets.framework"
test -f "$app/Contents/PlugIns/platforms/libqcocoa.dylib"
ditto -c -k --sequesterRsrc --keepParent "$app" msproject-macos.zip
```

`macdeployqt` copies the Qt frameworks (Widgets, Gui, Core, PrintSupport, …)
into `Contents/Frameworks` and the plugins into `Contents/PlugIns`, rewriting
references to `@executable_path`. It **ad-hoc signs by default**
(`-codesign=-`, Qt docs) and exits non-zero if signing fails; that is what lets
an arm64 bundle launch after `install_name_tool` invalidated the linker's
signature. The three `test`s are the structural proof that the deployment
landed (the macOS equivalent of checking that windeployqt copied DLLs — there is
no window server on the runner, so the bundle is not launched).

### Linux — AppImage with linuxdeploy + linuxdeploy-plugin-qt

```bash
# continuous tools, next to each other so linuxdeploy's plugin discovery finds
# linuxdeploy-plugin-qt; APPIMAGE_EXTRACT_AND_RUN=1 runs both without FUSE 2
curl -fsSL --retry 3 -o $tools/linuxdeploy-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
curl -fsSL --retry 3 -o $tools/linuxdeploy-plugin-qt-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x "$tools/"*.AppImage

# 256x256 icon drawn with the Python stdlib (the repo has no icon file; the
# shell paints its icons in code) + a minimal desktop entry
python3 - dist/linux/msproject-shell.png <<'PY' ... PY
cat > dist/linux/msproject-shell.desktop <<'DESKTOP' ... DESKTOP

export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$tools:$PATH"
export QMAKE="<install-qt qt-root-dir>/bin/qmake"
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"
export LDAI_OUTPUT="msproject-linux-x86_64.AppImage"   # current plugin
export OUTPUT="msproject-linux-x86_64.AppImage"        # older bundled plugin

"$tools/linuxdeploy-x86_64.AppImage" \
  --appdir "$appdir" \
  --executable "$PWD/build-qt/msproject_qt" \
  --desktop-file "$PWD/dist/linux/msproject-shell.desktop" \
  --icon-file "$PWD/dist/linux/msproject-shell.png" \
  --plugin qt \
  --output appimage
mv "msproject-linux-x86_64.AppImage" dist/
test -s dist/msproject-linux-x86_64.AppImage
```

`QMAKE` is what makes the Qt plugin use the pinned 6.5.3 rather than the
distro Qt. `EXTRA_PLATFORM_PLUGINS` adds the offscreen plugin next to the xcb
plugin linuxdeploy deploys by default; it is only there so the smoke run needs no
window server.

Smoke step (after the test gate and the three package steps):

```bash
APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen \
  ./dist/msproject-linux-x86_64.AppImage \
  --render build-qt/appimage/appimage-smoke.png | tee build-qt/appimage/appimage-smoke.log
grep -q "rendered 900x560" build-qt/appimage/appimage-smoke.log
```

**Chosen: AppImage, not the tar.gz fallback.** It did not "prove fragile" here
(nothing runtime was run — see (d)); the known runner hazard (FUSE 2) is
designed out with `APPIMAGE_EXTRACT_AND_RUN=1` rather than papered over with a
fallback. If the first run shows linuxdeploy/appimagetool cannot produce a
working file, the documented fallback is a tar.gz of `msproject_qt` plus the
`ldd`-collected Qt libraries (and plugins) with a launcher that sets
`LD_LIBRARY_PATH`; that would be a deliberate change, not a silent branch.

## (c) Artifacts, exactly

- `msproject-windows-<sha>` ← `msproject-windows.zip`, `if-no-files-found: warn`,
  `if: always() && runner.os == 'Windows'` (unchanged).
- `msproject-macos-<sha>` ← `msproject-macos.zip`, warn,
  `if: always() && runner.os == 'macOS'` (new).
- `msproject-linux-<sha>` ← `dist/msproject-linux-x86_64.AppImage`, warn,
  `if: always() && runner.os == 'Linux'` (new).
- `qt-window-<runner.os>-<sha>` ← `build-qt/*.png`, `build-qt/test-out/*.png`,
  `if-no-files-found: error`, `if: always()` (unchanged).

The `warn` on the package uploads matches the existing Windows step: on a
configure failure the package genuinely does not exist, and a red upload on top
of a red build adds nothing. The render upload keeps `error`.

The AppImage scratch tree is `build-qt/appimage/` (tools, AppDir, smoke PNG), so
it cannot leak into the render artifact's `build-qt/*.png` glob. The macOS step
writes no PNGs.

## (d) Local validation (run here, PyYAML 6.0.3 / Python 3.14.4)

`python build/ci2-w20/validate-workflow.py` parses the file (normalising the
YAML 1.1 `on:` → `True` trap) and asserts triggers, permissions, concurrency,
matrix, Qt pin, cache-off, configure/guard/ctest/parity, the exact step order
(`Build → ctest → parity → packages → smoke → uploads`), the three package steps
and the four uploads. Output (exit 0):

```
parsed: dict top-level keys: ['concurrency', 'env', 'jobs', 'name', 'on', 'permissions']
job build-qt: ${{ matrix.name }} | runs-on: ${{ matrix.os }} | timeout: 45
  linux-qt    ubuntu-latest   arch=gcc_64               generator=Unix Makefiles
  windows-qt  windows-2022    arch=win64_msvc2019_64    generator=Ninja
  macos-qt    macos-14        arch=clang_64             generator=Unix Makefiles
steps: 17
  - Check out
  - Install Linux Qt dependencies [if: runner.os == 'Linux']
  - Install Qt ${{ env.QT_VERSION }} (${{ matrix.arch }})
  - Set up the MSVC environment [if: runner.os == 'Windows']
  - Configure
  - Prove configure used the pinned Qt
  - Build
  - Test (offscreen renders + parity geometry)
  - The 1920x1032 parity render was written [if: always()]
  - Package the Windows build [if: runner.os == 'Windows']
  - Package the macOS build as a .app bundle [if: runner.os == 'macOS']
  - Package the Linux build as an AppImage [if: runner.os == 'Linux']
  - Run the packaged Linux AppImage (offscreen smoke) [if: runner.os == 'Linux']
  - Upload the Windows build [if: always() && runner.os == 'Windows']
  - Upload the macOS bundle [if: always() && runner.os == 'macOS']
  - Upload the Linux AppImage [if: always() && runner.os == 'Linux']
  - Upload the 900x560 and 1920x1032 renders [if: always()]
artifacts:
  msproject-windows-${{ github.sha }}        path=msproject-windows.zip if-no-files-found=warn
  msproject-macos-${{ github.sha }}          path=msproject-macos.zip if-no-files-found=warn
  msproject-linux-${{ github.sha }}          path=dist/msproject-linux-x86_64.AppImage if-no-files-found=warn
  qt-window-${{ runner.os }}-${{ github.sha }} paths=['build-qt/*.png', 'build-qt/test-out/*.png'] if-no-files-found=error
OK: qt.yml shape matches the wave-20 ci2 brief
```

`python build/ci2-w20/check_embedded.py` then exercises the embedded text that
PyYAML alone cannot: the Info.plist heredoc is parsed with `plistlib` (10 keys,
the three required values), the desktop heredoc is parsed key=value, and the
embedded Python icon generator is extracted, executed and its PNG header checked
(256x256). Output (exit 0):

```
Info.plist: parses, 10 keys, CFBundleName='MS Project Shell', CFBundleIdentifier='org.personal.msproject-shell', CFBundleExecutable='msproject_qt'
desktop file: parses, Exec=msproject_qt, Icon=msproject-shell
icon: generator ran, build\ci2-w20\msproject-shell.png is 256x256, 771 bytes
bash: C:\Program Files\Git\bin\bash.exe
bash: the sandbox blocked Git Bash startup; shell syntax NOT checked here
OK: plist, desktop entry and icon generator are valid (shell syntax unverified in this sandbox)
```

The shell scripts themselves could **not** be syntax-checked here: the DSH
sandbox blocks Git Bash from creating its signal pipe (`couldn't create signal
pipe, Win32 error 5`), and there is no other bash. That is a sandbox limit, not
a workflow failure; the scripts were reviewed by hand. Also **not run**: any Qt
build, `macdeployqt`, `linuxdeploy`, `appimagetool`, the AppImage, or GitHub
Actions itself. **Everything runtime is unverified until the first dispatch** —
no `git`, no dispatch, no installs.

## (e) What must be true for the first run to succeed

1. **Qt 6.5.3 still downloads** for `gcc_64`, `win64_msvc2019_64` and `clang_64`
   (unchanged from wave 19; the guard proves the prefix).
2. **macOS**: `macdeployqt` is on `PATH` (install-qt-action puts Qt's `bin`
   there; `windeployqt` already relies on the same mechanism), the runner's
   toolchain accepts ad-hoc signing, the deployment lands at
   `Contents/Frameworks/QtWidgets.framework` and
   `Contents/PlugIns/platforms/libqcocoa.dylib`, and `ditto` keeps
   `MS Project Shell.app` at the zip root. The bundle is not launched, so
   "runs" is a structural claim on macOS until someone opens it.
3. **Linux**: the runner has `python3`, `curl`, and now `libxcb-cursor0`; both
   continuous AppImages download and start under
   `APPIMAGE_EXTRACT_AND_RUN=1`; `QMAKE` resolves inside the pinned 6.5.3;
   appimagetool honours `LDAI_OUTPUT`/`OUTPUT`; the AppImage starts offscreen
   with its own bundled Qt and the offscreen plugin. That last one is the only
   place the workflow executes the delivered file, and it is the check most
   likely to need a fix on the first run.
4. **Windows**: unchanged, so the wave-19 behaviour carries over.
5. **ctest still passes** with whatever tests app_qt has by then; the workflow
   names none.

## (f) Top three failure modes and their fixes

1. **Linux `Package the Linux build as an AppImage` fails in linuxdeploy.**
   Most likely: a `libqxcb.so` dependency is missing on the runner
   (`Could not find dependency ...`, e.g. another xcb library on a future image)
   → add the distro package to the `Install Linux Qt dependencies` apt line, as
   `libxcb-cursor0` already was. Next: `QMAKE` is wrong/empty
   (`Could not find Qt installation`) → echo
   `"${{ steps.install-qt.outputs.qt-root-dir }}/bin/qmake"` and use
   `$QT_ROOT_DIR/bin/qmake`; the plugin bundled inside linuxdeploy still works
   as a fallback. Or the output name is not the one `mv` expects
   (`mv: cannot stat 'msproject-linux-x86_64.AppImage'`) → the tools are in a
   subdirectory, so `mv ./*.AppImage dist/msproject-linux-x86_64.AppImage` is
   safe (exactly one candidate in the repo root).
2. **Linux smoke fails**: `could not find or load the Qt platform plugin
   "offscreen"` → the plugin was not deployed; check `--list-plugins`, and widen
   `EXTRA_PLATFORM_PLUGINS` (`libqoffscreen.so;libqminimal.so`), or point
   `QMAKE` at the right Qt so the plugin reads the same prefix as the libs.
   If instead it says `error loading libfuse.so.2`, the AppImage ran without
   `APPIMAGE_EXTRACT_AND_RUN=1` (it is set inline in the step; keep it there).
   Either way the AppImage artifact still uploads, so the package is not lost.
3. **macOS packaging fails**: `macdeployqt: command not found` → call
   `"${{ steps.install-qt.outputs.qt-root-dir }}/bin/macdeployqt"` explicitly
   (same class of assumption as `windeployqt` on Windows). If the structural
   `test` for `Contents/PlugIns/platforms/libqcocoa.dylib` fails, inspect
   `find "$app" -name 'libqcocoa*'`: the deployment may use a different layout
   on a future Qt, and the fix is to relax that check, not to stop deploying. If
   signing is the failure (non-zero from macdeployqt), pass `-codesign=-`
   explicitly; if that still fails, `-no-codesign` plus
   `codesign --force --deep --sign - "$app"`, and note that an unsigned arm64
   bundle will not launch.

Ordering (wave-20 coordinator decision): the package steps sit **after**
`The 1920x1032 parity render was written`, i.e. `Build → ctest → parity render →
Package Windows → Package macOS → Package Linux → AppImage smoke → uploads`.
ctest always runs first, so a speculative macdeployqt/linuxdeploy failure cannot
skip it and hide the concurrent app_qt test results. The package steps keep the
default success condition, so a red ctest or parity gate skips packaging and the
package uploads warn (no artifact) — that is the intended "do not publish a
package of a red run"; the render artifact still uploads, because its upload is
`always()`.

## (g) User-facing notes (put these in the release text if there is one)

- **macOS**: unzip → `MS Project Shell.app`; it is ad-hoc signed, not notarized,
  so Gatekeeper wants right-click Open on first launch (or
  `xattr -dr com.apple.quarantine "MS Project Shell.app"`). No Developer ID
  exists yet (Q5's notarization step is procurement).
- **Linux**: `chmod +x msproject-linux-x86_64.AppImage` after download. It needs
  FUSE 2 on the user's machine (Ubuntu 24.04: `libfuse2t64`), or run it with
  `--appimage-extract-and-run`.
- **Windows**: as before — unzip and run `dist/msproject_qt.exe`; the Qt DLLs
  and MSVC runtime are beside it.

## (h) Next three actions

1. **Coordinator**: commit `.github/workflows/qt.yml` + this handoff +
   `memory/ci2.md`; do **not** commit `build/ci2-w20/**` (scratch). Dispatch
   `qt-shell` once (or tag) and send the run log if any job is red.
2. **Triage by step**: Linux red at `Package the Linux build as an AppImage` is
   the toolchain/dependency, not the shell; red at the smoke step is the bundle
   contents; macOS red is macdeployqt/layout/signing, not app_qt. Do not "fix" a
   red ctest step by touching packaging.
3. **After green**: smoke-run the macOS zip on a real Mac (the one claim CI
   cannot make) and the AppImage on a desktop with no Qt, then update
   `docs/ui-qt-port.md` Q5 (packaging done; notarization outstanding) and the
   release notes with (g).
