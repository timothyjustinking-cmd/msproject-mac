# memory: app stream (W32 shell v1 + W38 shell v2)

_2026-10-22. Owner: `app/**`, `.github/workflows/app-macos.yml`, `memory/app.md`.
No git commands were run: the files are on disk in the main checkout for the
coordinator to commit._

Read this, then `app/README.md` (the user-facing account: controls, what persists on
save, the date identity, verification), then `app/main.mm` (the only ObjC++),
`app/selection_model.h`, `app/document.h`, and `memory/sheet.md` /
`memory/gantt.md` for the two models the shell drives.

## State at the end of W38

| File | Contents |
|---|---|
| `app/layout_model.{h,cpp}` | v1 mapping + W38 additions: `sync_plan_from_records`, `make_gantt_source_for_rows`, `apply_edit_policy`, and the **date identity fix** |
| `app/selection_model.{h,cpp}` | `RowMap`, `selected_task`, `select_cell`, `select_from_gantt`, `row_after`, `toggle_row`, `scroll_target` |
| `app/document.{h,cpp}` | `collect_start_edits`, `default_save_name`, `save_start_edits`, `accept_start_edits` |
| `app/selection_model_tests.cpp` | 90 checks (synthetic, no corpus) |
| `app/document_tests.cpp` | 45 checks (pure parts + a real save round trip on the committed fixture) |
| `app/layout_model_tests.cpp` | 253 checks with corpus probes (246 in v1; +7 for the date identity) |
| `app/main.mm` | open/save panels, menu, status bar, selection sync, keyboard editing, undo/redo, outline collapse, alerts |
| `app/CMakeLists.txt` | adds `core_mppwrite` to the app build, detects `core/mppwrite/save.h`, three test targets, links UniformTypeIdentifiers |
| `.github/workflows/app-macos.yml` | v2: asserts the three tests are registered, runs them, asserts AppKit + UTI linkage |

## The date bug (W38, fixed) — read this before touching date code

`app::date32_from_packed`/`packed_from_date32` are the **identity**. v1 added a day,
so every date in the shell was one late. Evidence chain in `app/README.md`; the
probes that pin it are in `layout_model_tests.cpp`: two spec-pinned packed values,
`weekday(15633) == 1` (PROJECT_A's Monday), a packed→date32→packed round trip over every
stored date in w3-o1/holiday1/PROJECT_A, and w3-o1's project start exactly (2026-10-19
08:00). One instant is unrepresentable: 1983-12-31 00:00 packs to 0 = "not set".
If another stream reports a date discrepancy, check whether they added/subtracted a
day somewhere; the app side is now identity and the core is identity.

## Verification (last run 2026-10-22)

Full standalone build + `ctest`, MSVC 19.44, Release, `/W4 /WX /permissive- /utf-8`
(`-DCORE_WARNINGS_AS_ERRORS=ON`), no `--parallel`:

```powershell
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S app -B build\vs-w38 -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_BUILD_TYPE=Release -DAPP_BUILD_TESTS=ON -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w38 --config Release"
$ctest='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $ctest --test-dir build\vs-w38 -C Release --output-on-failure
```

Result: build exit 0 with no warnings; `ctest` **3/3** (`app_layout`, `app_selection`,
`app_document`); counts `layout_model_tests` 253, `selection_model_tests` 90,
`document_tests` 43. Configure printed `app: core/mppwrite/save.h found -- saves use
mppwrite::save_date_edits`, and `document_tests` printed `SAVE:
mppwrite::save_date_edits` plus W37's manual-row note and post-save re-read receipt.
Save output goes to `build\vs-w38\test-out`; `tests/data/synthetic-base.mpp` is
byte-identical afterwards (SHA-256 55C109F2…C6069, 237,568 bytes).

Earlier in the session, while `core/mppwrite/save.cpp` was mid-edit (it did not
compile for a while: C2440/C2039 around `ManualState`/`_Vb_reference`), the same three
tests were compiled and linked ad hoc against the core libs with
`/DAPP_MPPWRITE_SAVE_HEADER=0`, i.e. the fallback save branch — 253 / 90 / 45 checks,
`SAVE: mppwrite::pin_task_start + CfbDocument::save`. Both branches have now been run;
2 checks in `app_document` are compiled only on the fallback branch (the SNET
type/date assertion, because the W37 path proves it by re-reading from disk).

The macOS CI job (`app-macos.yml`) is the only proof of `main.mm`.

## Traps hit (W38)

- **A C++ function definition inside `@implementation` is not legal Objective-C++.**
  `sheet_row_at`/`sheet_column_at`/`printable_text` were first written between two
  methods; move them into the file's anonymous namespace.
- **`commit_edit()` clears `edit_target()` on success**, so a status message that
  reads `column_title(edit_target().column)` after the commit names column 0. Capture
  the title first.
- **Do not index the visible-column vector with a model column index** (they differ
  once a column is hidden). Match `ColumnSegment::column` instead.
- **The arc/`@implementation` region is invisible to the Windows compiler.** Every
  edit to `main.mm` is unverified until macOS CI; keep it mechanical.
- **A helper named `file_size`** in a test collides with `std::filesystem::file_size`
  through ADL on a `path` argument (C2668). Rename.
- **`cl` with `/DNAME="a path with spaces"`** does not group the quotes in a response
  file; for ad-hoc compile checks use a path without spaces (CMake passes definitions
  as separate argv items, so the real build is fine).
- **`core/mppwrite/save.h` landed mid-session.** The CMake `if(EXISTS)` flip means a
  stale configure keeps using the fallback; re-run configure after it appears.
- Dropbox rewrites mtimes: `edit` may report "file changed since read"; re-read
  immediately before each edit. Build with `build\vs-w38` only, never `--parallel`.

## Open, ranked

1. **Prove `main.mm` on macOS CI** — the whole AppKit surface is compile-unproven
   here. Watch for: `UTType`/`setAllowedContentTypes:` availability, `@available`
   syntax, ARC on the file-scope globals, and deprecation warnings (they are errors).
   `UniformTypeIdentifiers` is linked explicitly in `app/CMakeLists.txt`.
2. **In-cell editor upgrade** (NSTextField / IME / cursor) — the current editor is
   drawn from `edit_text()` with a `|` caret.
3. **Clipboard** (Cmd-C/Cmd-V over `ClipboardBlock::to_text`/`from_text`) and
   **row insert/delete** (blocked on `RecordSet`).
4. **Real calendar shading** (`core/cal` → `GanttSource::set_calendar`).
5. **Non-date saving**: only start dates persist. Name/Finish/Duration are view-only;
   do not claim otherwise in the README.

## Next three concrete actions

1. Ask the coordinator for the `app-macos` run (this is the gate for `main.mm`); if it
   fails, the likely spots are the `@available`/`UTType` block in `openDocument:`, the
   menu target wiring, and any AppKit deprecation under `-Werror`.
2. If `main.mm` builds, smoke-test by hand on a Mac: Cmd-O, click a row and a bar,
   double-click a Start, type a date, Return, Cmd-Z, Cmd-Shift-Z, click a disclosure
   box, Cmd-S. Everything there is unexercised by CI.
3. If the shell is to become a real editor, land an `NSTextField`-based cell editor and
   a clipboard bridge next; both are shell-only and need no core change.
