# Wave 20 — Qt shell packaging (`.github/workflows/qt.yml`): working memory

_2026-10-23. Lane: stream ci2. Files I own: `.github/workflows/qt.yml`,
`docs/handoff/ci2.md`, `memory/ci2.md`, scratch under `build/ci2-w20/**`.
Nothing else was edited. This box has no Qt, no runners and a sandbox that
blocks Git Bash startup, so **every runtime claim below is unverified until the
first dispatch**; no `git`, no dispatch, no installs._

## What the workflow is now

Same single job (`build-qt`) and three-row matrix as wave 19; the only change is
that two more platforms get a runnable package and Linux gets a post-test smoke.

| runner | job | package step | artifact | format |
|---|---|---|---|---|
| `windows-2022` | `windows-qt` | unchanged | `msproject-windows-<sha>` | zip (windeployqt + `cmake -E tar`) |
| `macos-14` | `macos-qt` | new | `msproject-macos-<sha>` | zip of `MS Project Shell.app` (ditto) |
| `ubuntu-latest` | `linux-qt` | new | `msproject-linux-<sha>` | AppImage (linuxdeploy + plugin-qt) |
| all | — | unchanged | `qt-window-<os>-<sha>` | the four render PNGs |

Steps 10 → 17. File 428 lines, 20,920 bytes, LF-only, no tabs. Triggers
(`workflow_dispatch` + `v*` tags), `permissions: contents: read`, concurrency,
matrix, Qt 6.5.3, configure flags, the pin guard, ctest and the render artifact
are unchanged. No caching anywhere (`cache: 'false'` on install-qt still, no
`actions/cache`).

## Decisions, with the evidence

1. **macOS bundle built in the workflow, not by CMake.** The brief forbids
   touching `app_qt/CMakeLists.txt` (another stream owns it, and `MACOSX_BUNDLE`
   changes window behaviour). So the step creates `dist/MS Project Shell.app`
   by hand: `Contents/MacOS/msproject_qt` + a heredoc `Contents/Info.plist` with
   `CFBundleName` "MS Project Shell", `CFBundleIdentifier`
   `org.personal.msproject-shell`, `CFBundleExecutable` `msproject_qt`, plus
   `APPL`, versions, `LSMinimumSystemVersion 11.0`, `NSHighResolutionCapable`.
2. **`macdeployqt` with its default signing.** Qt's macOS deployment docs
   (`https://doc.qt.io/qt-6/macos-deployment.html`, fetched 2026-10-23) say the
   tool deploys the platform plugin always, image formats/icon engines/print
   support/accessibility/style plugins, and that `-codesign=<ident>` runs
   codesign on all executables with **ad-hoc signing (`-codesign=-`) as the
   default**; `-no-codesign` disables it. That default is what makes the arm64
   bundle launch after `install_name_tool` rewrites invalidate the linker's
   signature, so the step does not add a separate `codesign` call. `-verbose=2`
   is for triage, `-always-overwrite` for reruns.
3. **Structural checks instead of launching.** There is no window server on the
   runner, so the step proves the deployment landed where dyld/QPA look:
   `Contents/MacOS/msproject_qt` executable,
   `Contents/Frameworks/QtWidgets.framework` dir, and
   `Contents/PlugIns/platforms/libqcocoa.dylib` file. `Contents/PlugIns` is the
   standard bundle plugin directory and the one macdeployqt writes.
4. **Zip with ditto, `--keepParent`.** `ditto -c -k --sequesterRsrc
   --keepParent "$app" msproject-macos.zip` keeps `MS Project Shell.app` as the
   zip's top-level entry (not `dist/`), and preserves permissions/signature and
   resource forks. This is the same idiom `app-macos.yml` already uses for its
   borrow-a-Mac zip, so the behaviour is precedented in this repo.
5. **Linux: AppImage, as `docs/ui-qt-port.md` Q5 names for Linux.** Downloaded
   both `linuxdeploy-x86_64.AppImage` and
   `linuxdeploy-plugin-qt-x86_64.AppImage` from the `continuous` release tags
   into `build-qt/appimage/tools/`, `chmod +x`, and ran them from there. Per
   linuxdeploy's plugin-spec wiki, plugin discovery is: next to the linuxdeploy
   AppImage, next to the binary, `$PATH`, then cwd, and filenames are matched by
   `^linuxdeploy-plugin-([^\s\.-]+)(?:-[^\.]+)?(?:\..+)?$` — so
   `linuxdeploy-plugin-qt-x86_64.AppImage` next to the tool **and** in `$PATH`
   is found as plugin `qt`. The plugin README's recommended setup is exactly
   "put it next to linuxdeploy's AppImage".
6. **`APPIMAGE_EXTRACT_AND_RUN=1` instead of FUSE.** AppImage's own docs
   (`https://docs.appimage.org/user-guide/troubleshooting/fuse.html`) document
   the env var as the extract-and-run fallback when FUSE is unavailable, and
   note it is usually forwarded to AppImage child processes — which is what lets
   linuxdeploy start the plugin AppImage the same way. ubuntu-latest does not
   guarantee `libfuse2` (Ubuntu 24.04 renamed it `libfuse2t64`), so this removes
   a package dependency from the runner rather than adding one.
7. **`QMAKE` is pinned explicitly.** linuxdeploy-plugin-qt uses `qmake`/`QMAKE`
   to find `QT_INSTALL_PLUGINS` and the Qt libs. Qt's `bin` is on `PATH` from
   install-qt-action, but an image change must not silently swap to the distro
   qmake, so the step exports
   `QMAKE="${{ steps.install-qt.outputs.qt-root-dir }}/bin/qmake"` — the same
   documented output the Configure step and the pin guard use.
8. **`EXTRA_PLATFORM_PLUGINS=libqoffscreen.so`.** linuxdeploy ships `libqxcb.so`
   by default (what a user's desktop needs) and the README says
   `EXTRA_PLATFORM_PLUGINS` adds "platforms in addition to libqxcb.so". The
   offscreen plugin is added purely so the final smoke run needs no window
   server. No `EXTRA_QT_MODULES`/`QML_SOURCES_PATHS`: the shell is Widgets-only,
   no QML.
9. **Icon generated, not committed.** A desktop entry requires an icon, and the
   repository has none (the shell paints its icons in `app_qt/shell_icons.cpp`).
   Rather than depend on ImageMagick on the runner, the step writes a 256x256
   PNG (three Gantt bars on a dark field) with the Python standard library
   (`struct`/`zlib`). Verified locally: parses, 256x256, 771 bytes.
10. **Linux desktop file**: `Type=Application`, `Name=MS Project Shell`,
    `Exec=msproject_qt` (the name linuxdeploy installs into `usr/bin`),
    `Icon=msproject-shell` (basename of the icon file), `Terminal=false`,
    `Categories=Office;ProjectManagement;`.
11. **Output name set explicitly twice.** The current
    `linuxdeploy-plugin-appimage` README documents `LDAI_OUTPUT`; the older
    linuxdeploy plugin-spec wiki documents `OUTPUT=...`. The plugin's own
    `src/main.cpp` (fetched 2026-10-23) reads both, in that order, and passes
    the value to appimagetool as its output argument, so both are exported with
    the same value: `mv msproject-linux-x86_64.AppImage dist/` is deterministic
    and does not guess appimagetool's default name.
12. **Smoke run is a separate step after ctest.** It executes the AppImage
    itself (`APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen
    ./dist/... --render ... | tee`) and greps `rendered 900x560`, i.e. the same
    contract the CMake test uses. It was originally inside the packaging step;
    splitting it means a speculative runtime check cannot suppress ctest and the
    render upload. The packaging steps themselves stay before ctest, where the
    Windows step already was (wave 19), to keep the diff minimal; the trade-off
    is recorded in the handoff.
13. **`libxcb-cursor0` added to the Linux apt line.** Qt 6.5's `libqxcb.so` links
    `libxcb-cursor.so.0`; Ubuntu's `qt6-base-dev` (distro Qt 6.4) does not pull
    it. linuxdeploy copies the plugin and resolves its dependencies, so without
    the library the package step fails while bundling the xcb plugin. It is not
    needed to build or to run the offscreen tests.
14. **No new actions, no caching.** Everything new is shell (`curl`, `chmod`,
    `python3`, `macdeployqt`, `ditto`, linuxdeploy). The only actions are the
    three wave-19 ones plus `upload-artifact@v4` for the two new packages.

## Artifacts: exactly what lands

- `msproject-windows-<sha>` ← `msproject-windows.zip` (unchanged)
- `msproject-macos-<sha>` ← `msproject-macos.zip` → `MS Project Shell.app`
- `msproject-linux-<sha>` ← `dist/msproject-linux-x86_64.AppImage`
- `qt-window-<runner.os>-<sha>` ← `build-qt/*.png`, `build-qt/test-out/*.png`

The three package uploads are `warn` on no files (matching the existing Windows
step); the render upload stays `error`. The AppImage scratch tree lives under
`build-qt/appimage/`, so its PNG/log cannot be picked up by the render globs —
asserted in the validator, not just intended.

## Verification actually run here

- `python build/ci2-w20/validate-workflow.py` — PyYAML 6.0.3 / Python 3.14.4.
  Parses (normalising `on:` → `True`), prints the 17 steps and four artifacts,
  asserts the unchanged invariants and the new steps/artifacts. Exit 0. Output
  is pasted in `docs/handoff/ci2.md` (d).
- `python build/ci2-w20/check_embedded.py` — extracts the heredocs from the
  parsed YAML and checks what PyYAML cannot: the Info.plist parses with
  `plistlib` (10 keys; the three required values), the desktop entry parses,
  and the icon generator is executed (256x256 PNG, 771 bytes). Exit 0.
- **Not run**: `bash -n`. Git Bash exists at
  `C:\Program Files\Git\bin\bash.exe`, but the DSH sandbox kills it at startup
  with `couldn't create signal pipe, Win32 error 5`. The checker reports this
  as "shell syntax NOT checked" instead of passing silently. Read the scripts by
  hand instead (see the printed run blocks in the handoff).
- **Not run**: any Qt configure/build, `macdeployqt`, `linuxdeploy`,
  `appimagetool`, the AppImage, GitHub Actions. No git, no dispatch, no
  installs.

## Evidence consulted (external)

- Qt "Qt for macOS — Deployment" (macdeployqt options, default ad-hoc signing,
  which plugins are deployed):
  `https://doc.qt.io/qt-6/macos-deployment.html`
- linuxdeploy README (plugin discovery, `--plugin`):
  `https://github.com/linuxdeploy/linuxdeploy`
- linuxdeploy plugin specification wiki (naming regex, discovery order):
  `https://github.com/linuxdeploy/linuxdeploy/wiki/Plugin-system`
- linuxdeploy user guide (`--executable`/`--desktop-file`/`--icon-file`
  semantics, "an icon is always required"):
  `https://docs.appimage.org/packaging-guide/from-source/linuxdeploy-user-guide.html`
- linuxdeploy-plugin-qt README (`QMAKE`, `EXTRA_PLATFORM_PLUGINS`,
  `EXTRA_QT_MODULES`, `QML_SOURCES_PATHS`):
  `https://github.com/linuxdeploy/linuxdeploy-plugin-qt`
- linuxdeploy-plugin-appimage README (`LDAI_OUTPUT`):
  `https://github.com/linuxdeploy/linuxdeploy-plugin-appimage`
- AppImage FUSE troubleshooting (`APPIMAGE_EXTRACT_AND_RUN=1`, child-process
  forwarding): `https://docs.appimage.org/user-guide/troubleshooting/fuse.html`

## Open questions only a run can answer

1. Is `macdeployqt` on `PATH` on `macos-14` (install-qt-action prepends Qt's
   `bin`; `windeployqt` already relies on this on Windows)?
2. Does the macdeployqt output really land at
   `Contents/PlugIns/platforms/libqcocoa.dylib` for Qt 6.5.3 (i.e. does the
   structural third `test` pass)? If not, the bundle is probably still fine and
   the check needs relaxing.
3. Does ad-hoc signing on the runner succeed without a keychain? Qt docs say it
   is the default; macdeployqt exits non-zero if `codesign` fails.
4. Do the linuxdeploy/plugin continuous AppImages download from GitHub in the
   workflow's network context and start under `APPIMAGE_EXTRACT_AND_RUN=1`?
5. Does the produced AppImage start offscreen and print `rendered 900x560`?
   This is the only execution of a delivered file in the workflow.
6. Is `libxcb-cursor0` still the only extra xcb library Qt 6.5.3's plugin needs
   on the current image? A future image/Qt bump may need another apt line.
7. Does `LDAI_OUTPUT` (or the older `OUTPUT`) actually name the output file, so
   the `mv` finds it? If not, `mv ./*.AppImage dist/...` is the one-line fix.
8. Does the macOS zip preserve the bundle's signature/permissions through
   `ditto` and upload/download? It should; only a real Mac can confirm the app
   launches.

## Traps a successor will hit

- **The macOS bundle must not become a CMake target.** `app_qt/CMakeLists.txt`
  is owned elsewhere and `MACOSX_BUNDLE` changes window behaviour; the workflow
  wrapper is deliberate. Do not "simplify" it into the CMake file.
- **`Contents/PlugIns`, not `Contents/Plugins`.** The standard (and macdeployqt)
  directory is `PlugIns`. A case error in the structural check looks like a
  packaging failure but is not.
- **Ad-hoc is not notarized.** Gatekeeper still blocks a browser-downloaded
  bundle: right-click Open or `xattr -dr com.apple.quarantine`. Do not read the
  green job as "double-click works everywhere".
- **AppImage and FUSE.** The workflow designs FUSE out for itself; users still
  need FUSE 2 (Ubuntu 24.04: `libfuse2t64`) or
  `--appimage-extract-and-run`. Put that in the release text.
- **`continuous` floats.** Unlike the Qt pin, linuxdeploy/plugin-qt/appimagetool
  move under you. If the packaging step starts failing for no code reason, pin
  the two URLs to a dated release tag; do not switch to distro `linuxdeployqt`.
- **The packaging steps run before ctest.** A red packaging step skips the tests
  and makes the render upload red (missing files). If that becomes annoying, move
  the three `Package ...` steps below the parity gate; the smoke step is already
  after ctest for this reason.
- **Render artifact globs.** Anything named `build-qt/*.png` or
  `build-qt/test-out/*.png` joins the render artifact. The AppImage work writes
  under `build-qt/appimage/` deliberately; keep new packaging output out of
  those two globs.
- **No fixed test count.** app_qt is gaining tests in parallel; the workflow
  names no test and no count. Do not add a `ctest -R`/`-N | grep` gate here.
- **`windows-2022` is pinned** for the Qt 6.5.3/MSVC compatibility reason in the
  header; do not "modernise" it to `windows-latest`.

## Next three actions

1. Coordinator: commit `qt.yml`, `docs/handoff/ci2.md`, `memory/ci2.md` (not
   `build/ci2-w20/**`), dispatch once, send the run log for any red job.
2. Triage by step: Linux package = tools/deps; Linux smoke = bundle contents;
   macOS package = macdeployqt/layout/signing. Never by platform superstition,
   and never by touching app_qt from this lane.
3. After green: open the macOS zip on a real Mac and the AppImage on a Qt-less
   desktop, then update `docs/ui-qt-port.md` Q5 (packages done, notarization
   outstanding) and the release notes with the Gatekeeper/FUSE notes.
