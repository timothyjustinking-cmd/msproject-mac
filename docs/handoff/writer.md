# Handoff — W9 (`.mpp` writer)

2026-10-22. Branch/commits: none from me — the coordinator commits (Rev 2 of
`docs/coordination.md`). Everything is on disk in the main checkout.

## (a) What is done

Three working stages, 784 checks, all passing under MSVC 19.44
(`/W4 /permissive- /utf-8`):

1. **Container round trip** (`core/mppwrite/cfb_writer.*`,
   `tests/roundtrip_cfb.cpp`, 310 checks). Parse the CFB and re-emit it
   **byte-identically** for all 19 `corpus/probe/*.mpp`, `project_a.mpp`
   and `project_b.mpp`. The FAT, mini FAT, DIFAT, directory and header are
   derived from stored chains, not copied, so this is evidence the container is
   understood rather than carried opaquely. `CfbDocument::check()` fails if the
   derived FAT disagrees with the stored one.

2. **In-place patching** (`core/mppwrite/inplace.*`, `tests/inplace_patch.cpp`,
   35 checks). Change Priority (+78), PercentComplete (+92), Duration (+84),
   Start (+96) and an arbitrary u32 in a FAT-chained stream by rewriting only
   the bytes that carry them: file length unchanged, no sector moved, container
   still round-trips, exactly the patched streams differ. Both allocation paths
   are exercised (FixedData is in the mini stream; `114/Props` is in FAT
   sectors).

3. **Stream replacement** (`CfbDocument::replace_stream`,
   `core/mppwrite/table.*`, `tests/roundtrip_stream_replace.cpp`, 439 checks).
   Replace `114/TBkndTask/FixedData` (same size == the in-place result, byte for
   byte), grow a task name (append-only Var2Data + VarMeta repoint), cross the
   4,096-byte mini cutoff, grow PROJECT_A's FAT (24 → 26 FAT sectors, file 1,567,744 →
   1,671,168), and shrink a stream back into the mini stream (176 FAT sectors
   released inside the file). `214/` is asserted byte-identical in every case.

Written files are validated by two parsers that are not mine: `olefile`
(`docs/analysis/writer/validate_written.py`, 6/6 clean) and the project's own
reader (`core/mpp/reader.cpp`, 0 warnings, written values read back).

Spec: `spec/mpp-writer.md`. State: `memory/writer.md`.

## (b) What is not done

- **No MS Project oracle run yet.** The writer's files have not been opened by
  Project. This is the one thing that separates "three parsers agree" from
  "the file is valid". Exact fixtures and questions: `spec/mpp-writer.md` §8.
- `core/mppwrite` is **not** in the top-level build. The top-level
  `CMakeLists.txt` (W8's) adds only `mpp` and `sched`; it was not mine to edit.
  Asked the coordinator for a one-line change: add `mppwrite` to
  `foreach(stream_subdir mpp sched)`.
- No directory-entry creation (no new streams), no row add/delete, no new
  VarMeta entries, no damage repair, no derived-field recomputation, no
  free-sector reuse. Reasons and the consistency list: spec §4.2 and §5.
- Nothing writes to `corpus/`; fixtures go to
  `core/mppwrite/build/vs/test-out/`.

## (c) Next three concrete actions

1. **Run the oracle** (coordinator): copy
   `core/mppwrite/build/vs/test-out/{inplace-patched,replace-fixeddata,replace-name}.mpp`
   into `corpus/probe/` as `oracle-patch.mpp`, `oracle-rebuild.mpp`,
   `oracle-name.mpp`, open each, report per-task UniqueID / Priority /
   PercentComplete / Duration / Start / Name, note any repair dialog and whether
   Finish moved, then Save As each and return the files. Also Save As
   `base.mpp` untouched as `resaved-base.mpp` to settle whether Project rewrites
   `214/` on save.
2. **Wire the build**: add `mppwrite` to the stream subdirectory list in the
   top-level `CMakeLists.txt`, then run the root `ctest` and confirm the three
   `mppwrite_*` tests appear and pass from the root working directory.
3. **Add directory-entry creation** (`CfbDocument::add_stream`): new 128-byte
   entry, red-black tree insert (colour + left/right/child), a new directory
   sector when the last one is full, and FAT/mini-FAT allocation for it. That is
   the prerequisite for any real save path and for regenerating `214/`.

## (d) Traps that cost time

1. **Mini sectors are not allocated in directory order** (`base.mpp`: `Props14`
   at 0, `\x01CompObj` at 29). Store the chain, never recompute.
2. **Padding is not always zero**: PROJECT_A `Fixed2Data` has 198 non-zero pad bytes.
   Zero-filling breaks byte equality.
3. **The mini stream is the root entry's payload.** Growing it without updating
   the root entry's `size` truncates it on re-read. The test caught this; it is
   the single most likely bug in any future allocator change.
4. **`FixedMeta` +0x0c and `VarMeta` +0x14 are size words** for FixedData and
   Var2Data. Stale values parse but confuse readers; `check_table()` catches
   them.
5. **PROJECT_A is damaged** (FixedMeta 87 vs 88 entries, VarMeta 1,076 vs 1,080, row 67
   has no metadata). `check_table()` is strict and reports it; the writer carries
   the damage through rather than repairing it. Do not "fix" it without an
   oracle run.
6. **Do not use `cmake --build --parallel`** on this toolchain: it returns 1
   with 0 errors.
7. `Corpus/probe` is read-only for streams; the four written fixtures are copied
   there only by the coordinator.
