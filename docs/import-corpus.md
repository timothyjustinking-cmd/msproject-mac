# Testing against real `.mpp` files (imported corpus)

_Written 2026-10-23 by stream M5 (wave 20). Companion to
`docs/coordination.md` §"Client data in committed files" and
`scripts/check_fixtures.py`._

The port is only as good as the files it has been tried against.
`tests/data/synthetic-*.mpp` are the committed fixtures and they are synthetic;
`corpus/project_a.mpp` / `corpus/project_b.mpp` are one client's programmes in
one Project version (16.0, build 19725). Neither tells us whether the reader
survives a file written by Project 98, a file with 26 recurring calendar
exceptions, or a file whose compound-file allocation is slightly malformed.

This document records where real `.mpp` files may be obtained, which ones are
now on disk, what their terms are, and how to re-run the inventory and
robustness sweeps. It is deliberately explicit about what could **not** be
obtained here, because the answer was "almost everything except GitHub".

---

## 1. The rules

1. **Never commit an imported file, or anything derived from one.** `corpus/` is
   git-ignored (`.gitignore`: `# Client project files and any other real .mpp
   corpus — never commit` → `corpus/`). `corpus/imported/` therefore needs no new
   ignore rule, and no file in it may be copied to `tests/data/` or quoted in
   `docs/`. Task and resource names inside a real file are personal data: an
   inventory records counts, hashes and stream names, never names. That is why
   `scripts/import_corpus.py` prints no row content at all, and why
   `scripts/import_robustness.py` throws the reader dump away instead of keeping
   it.
2. **Treat every imported file as hostile input.** A `.mpp` is an OLE2 compound
   file: it can carry a VBA project (`212/CVba/...` appears in
   `corpus/probe/rt2007.mpp`), embedded OLE objects, and any stream length it
   likes. Parse it with *our* reader first, in this workspace. Do not open it with
   an application that will run macros or embedded objects.
3. **If a file must be opened in MS Project, open a copy.** The coordinator is the
   only agent allowed to drive Project (`docs/coordination.md` §Revision 3), and
   only for a source whose terms clearly allow it. Take a copy, give it a new
   name, and never let the downloaded original be the file Project touched: an
   application that "repairs" a file writes to it.
4. **Nothing here is a licence to redistribute.** No imported file, and no
   derived artefact (golden TSV, dump, report with names), leaves this machine.
5. **Do not install anything, do not run git, do not write outside the
   workspace.** The two scripts honour this: they refuse to write under
   `tests/data/`, they read the corpus read-only, and their scratch directory is
   inside the corpus tree and removed at the end.

---

## 2. Sources

Legend for **verdict**: **use** = terms clearly permit local test use, none of
the file needs to be committed; **operator** = you must make the call, because
the terms are personal/official-download style rather than a licence;
**do not use** = no terms found, so it is not worth the risk.

### 2.1 Verified usable

| Source | What it is | Licence / terms as stated | `.mpp`? | Project versions | URL |
|---|---|---|---|---|---|
| **MPXJ test resources (`joniles/mpxj`, `junit/data/`)** | 618 `.mpp` files: generated fixtures, per-format conversion fixtures (`mpp12*-from14`), and years of user-submitted bug reproductions (`legacy/bug1..4`, `sf248.mpp`, `headertest8/9`, `*password9`) | Repository `LICENSE` is **LGPL-2.1**; `legal/licence.txt` is the same text. There is no per-file statement, and several files are user bug reports whose provenance is undocumented — so: local test use only, never redistribute, never quote their contents | yes (also `.mpd`, `.mspdi.xml`, `.mpx` beside them — ignore those unless testing import) | filenames state Project 98 (`mpp8`), 2000/2002/2003 (`mpp9`), 2007 (`mpp12`), 2010/2013/2016/2019 (`mpp12` + `mpp14`) | <https://github.com/joniles/mpxj/tree/master/junit/data> |
| **MPXJ VBA generator (`junit/GenerateTestData.bas`)** | not a file source, but the provenance record: a macro that creates each custom field and saves the project once per installed Project version | same repository | n/a | explains the `generated/*` naming | <https://github.com/joniles/mpxj/blob/master/junit/GenerateTestData.bas> |
| **MPXJ Ruby specs (`src.ruby/mpxj/spec/`)** | 9 tiny hand-built samples (`task.mpp`, `resource.mpp`, `relation.mpp`, …) | same repository (LGPL-2.1) | yes | `-9` (MPP9) era | <https://github.com/joniles/mpxj/tree/master/src.ruby/mpxj/spec> |

Note on MPXJ: LGPL-2.1 restricts *distribution* of derived works, not use. Nothing
in this project is distributed, and nothing is committed, so local parsing,
dumping and diffing are outside what the licence regulates. The reason to keep
the files out of commits is the project's own rule plus the fact that some of them
contain a real user's project names.

### 2.2 Reachable but blocked in this environment

Binary HTTPS download does not work from a confined agent shell here. Every
route was tried:

| Route | Result |
|---|---|
| `curl.exe` → `https://…` | `curl: (35) schannel: AcquireCredentialsHandle failed: SEC_E_NO_CREDENTIALS` |
| `curl.exe` → `http://…` | the host 301s to https, so the same failure follows the redirect |
| `Invoke-WebRequest` → `https://…` | `The SSL connection could not be established` |
| `gh api` → `raw.githubusercontent.com` content | **works** (Go's own TLS stack, separate credential store) |

So the only machine-readable source here is GitHub, through `gh api`. The
following are worth having and must be fetched by the operator (or by a session
where TLS works):

| Source | What it is | Terms | `.mpp`? | Versions | Status here |
|---|---|---|---|---|---|
| **Microsoft Press / O'Reilly companion content: _Microsoft Project 2013 Step by Step_** (ISBN 9780735669116) | the book's practice files, published for free download by Microsoft Press via O'Reilly | "Companion content is hosted for Microsoft Press by O'Reilly Media" — O'Reilly book-companion terms, personal use with the book, no redistribution. **Operator decision** | yes (inside a zip) | Project 2013 (MPP14) | page fetched; the zip (`http://examples.oreilly.com/9780735669116-files/9780735669116_files.zip`) 301s to https and the download failed (TLS, above) |
| **Microsoft template gallery** (`templates.office.com`) | Project templates | Microsoft Services Agreement; no redistribution | **no** — templates are `.mpt`, not `.mpp` | various | not verified (site needs JavaScript, no direct file URL found) |
| **Archive.org** | searched with `advancedsearch.php?q=extension:mpp` | item-dependent | — | — | **0 hits**; no collection of MS Project files found |
| **ProjectLibre** (GPLv3 project manager) | its docs describe *creating* an example project | GPLv3 applies to the program | no hosted `.mpp` corpus found | — | nothing to import |
| **GitHub code search** `extension:mpp` | 3,020 hits, essentially all unrelated: OCaml templates, C++20 modules, OpenM++ models, `coin-or/SYMPHONY` (mathematical programming) | repo-dependent | **not MS Project files** | — | nothing to import; MPXJ is the only public MS Project corpus on GitHub that this search exposes |

### 2.3 Do not use

| Source | Why not |
|---|---|
| **Le Moyne College course page — "Microsoft Project exercises"** (<https://webserver.lemoyne.edu/courseinformation/Grabowski/MIS%20460%20CSC%20460%20BUS%20460%20Project%20Management/Microsoft%20Project%20exercises/>) | Five real `.mpp` files (181–191 KB, Project 2010 era: "School of Business Costs, Levels, Reallocations", "Hong Kong Airport, Costs, Levels"). The page states **no terms at all**, the files are course solutions (i.e. someone's assessed work), and the site is a departmental web server with no licence. Useful shape (costs, levelling, baselines) but **do not use** unless the department grants permission in writing. |
| **any client/probe file already in `corpus/`** | Not a source, and never a candidate for import or commit. |

---

## 3. What is now in `corpus/imported/`

Twelve files, 2,465,280 bytes (2.35 MiB), fetched 2026-10-23 through the GitHub
contents API (`gh api repos/joniles/mpxj/contents/<path>`); the same bytes are
browsable at `https://github.com/joniles/mpxj/blob/master/<path>`. All are
git-ignored by the `corpus/` rule.

| file | source path in `joniles/mpxj` | bytes | sha256 (first 16) | MPP | accepted by reader |
|---|---|---|---|---|---|
| `sample98.mpp` | `junit/data/legacy/sample98.mpp` | 87,552 | `A87D2C82B5FFCD24` | MPP8 | no (no Props12/14) |
| `caltest.mpp` | `junit/data/legacy/caltest.mpp` | 81,408 | `79553219073B2ECB` | MPP9 | no |
| `bug1.mpp` | `junit/data/legacy/bug1.mpp` | 127,488 | `98F54372FEDEF6F8` | MPP9 | no |
| `empty.mpp` | `junit/data/legacy/empty.mpp` | 73,728 | `F7903E28396864DD` | MPP9 | no |
| `mpp9nulltasks.mpp` | `junit/data/mpp9nulltasks.mpp` | 120,320 | `542AD0E08B20D25E` | MPP9 | no |
| `task-baselines-project2003-mpp9.mpp` | `junit/data/generated/task-baselines/task-baselines-project2003-mpp9.mpp` | 164,864 | `2668E2F4CDE639C3` | MPP9 | no |
| `mpp12nulltasks-from14.mpp` | `junit/data/mpp12nulltasks-from14.mpp` | 186,368 | `F207FA519B5BED0B` | MPP12 | yes (0 tasks) |
| `task-costs-project2013-mpp12.mpp` | `junit/data/generated/task-costs/task-costs-project2013-mpp12.mpp` | 274,944 | `DE75A900FE56B302` | MPP12 | yes (10 tasks) |
| `calendar-recurring-exceptions-project2013-mpp14.mpp` | `junit/data/generated/calendar-recurring-exceptions/calendar-recurring-exceptions-project2013-mpp14.mpp` | 242,176 | `E5C182434F4CF8EB` | MPP14 | yes (0 tasks, 26 recurring exceptions) |
| `resource-type-project2016-mpp14.mpp` | `junit/data/generated/resource-type/resource-type-project2016-mpp14.mpp` | 245,760 | `E76D2F3070038F98` | MPP14 | yes (15 resources) |
| `task-baselines-project2019-mpp14.mpp` | `junit/data/generated/task-baselines/task-baselines-project2019-mpp14.mpp` | 369,152 | `19A4A7A6D3FE510E` | MPP14 | yes (110 tasks) |
| `sf248.mpp` | `junit/data/assignment/assignment-deletion/sf248.mpp` | 491,520 | `6AF9D473D7F9A840` | MPP14 | no (FAT extent) |

`sha256` in full is in `corpus/imported/inventory.tsv` (regenerated by the
command in §5.2); the 16-hex prefix is enough to recognise a file.

Reports written by the sweeps, also git-ignored:

| file | contents |
|---|---|
| `corpus/imported/inventory.tsv` | per-file version/hash/streams/reader verdict for the 12 imports |
| `corpus/imported/robustness.tsv` | reader + recalc outcome per imported file |
| `corpus/imported/robustness-mutate.tsv` | the same, plus four damaged copies of each file |
| `corpus/imported/probe-inventory.tsv` | the same inventory over the 117 files in `corpus/probe/` |
| `corpus/imported/probe-robustness.tsv` | reader + recalc over `corpus/probe/` |

---

## 4. The tools

Both are Python 3, standard library plus `olefile` (already a dependency of
`scripts/mpp_rows.py`). Neither modifies a `.mpp` file.

### 4.1 `scripts/import_corpus.py`

```text
python scripts/import_corpus.py [DIR ...] [--reader-bin BIN] [--out TSV]
                               [--timeout SECONDS] [--scratch DIR]
                               [--keep-scratch] [--list-only]
```

Defaults to `corpus/imported`. Columns: `file, bytes, sha256, version, evidence,
props, storages, streams, tbknd_task, reader, reader_ms, reader_rows,
reader_note`.

Version detection is signature-based, from the container itself:

| marker | family |
|---|---|
| root stream `Props` (unversioned) + storages `1`,`2` | MPP8 (Project 98) |
| `Props9` + `19`,`29` | MPP9 (Project 2000–2003) |
| `Props12` + `112`,`212` | MPP12 (Project 2007) |
| `Props14` + `114`,`214` | MPP14 (Project 2010+) |

A file can carry two sets — Project 2007 leaves the Project 2003 storages in
place when it re-saves a converted file — so the version is the *newest* marker
and `evidence` says `mixed(MPP9+MPP12)`. `corpus/probe/mpp12viewstate.mpp` is
exactly that case (`Props9` **and** `Props12`; storages `19,29,112,212`).

Reader probing is optional and auto-detects the interface: a binary whose name
contains `reader_dump` is called as `reader_dump <file>` (row count from stdout),
anything else as `engine_diff_tests --dump <file> <scratch.json>` (task count from
the dump, which is then deleted with the scratch tree).

Guarantees: every file's `(size, mtime_ns)` is recorded before it is touched and
re-checked at the end; if one changed, the run prints which and exits 1 rather
than reporting a lie. `--out` refuses any path under `tests/data/`. The scratch
directory is removed unless `--keep-scratch`, and `--scratch` is refused if it
contains a scanned directory (otherwise the cleanup would delete the corpus).

### 4.2 `scripts/import_robustness.py`

```text
python scripts/import_robustness.py [DIR ...] [--reader-bin BIN] [--recalc-bin BIN]
                                    [--timeout SECONDS] [--out TSV] [--mutate]
                                    [--strict] [--keep-scratch] [--scratch DIR]
```

Runs each binary over each file, with a timeout, and records
`file, mutation, bytes, version, stage, result, exit, ms, output_bytes, note`.
`result` is one of `ok`, `rejected` (exit 1), `exit-2` (the reader rejected its
own arguments — a harness bug, not a file finding), `crash` (an NTSTATUS value
such as `0xC0000005` access violation, `0xC00000FD` stack overflow,
`0xC0000374` heap corruption), `timeout`, or `exit-N`.

**A crash is the finding, not a failure of the script**, so the exit status is 0
even when a file crashes; `--strict` turns a crash or a timeout into exit 1 when
you want a gate.

`--mutate` writes four damaged **copies** per file into the scratch directory and
runs the reader over those too — truncate to 75%, truncate to 25%, zero the
512-byte CFB header, flip one byte at the midpoint. The originals are never
opened for writing, and `mutate()` refuses if source and destination are the same
path.

### 4.3 What neither script does

- No extraction of names, notes, text or custom-field values into any output.
- No network access. Fetching is a separate, manual step (§5.1).
- No MS Project, no COM, no `oracle_lock`.

---

## 5. Reproducing a run

### 5.1 Fetching (manual, one file at a time)

`gh api` returns the blob as base64 for files under 1 MB, which is every file
imported here (the largest is 492 KB). Guard: this writes a file, so run it from
the workspace root, and never write into `tests/data/`.

```powershell
$path = "junit/data/generated/task-costs/task-costs-project2013-mpp12.mpp"
$b64  = gh api "repos/joniles/mpxj/contents/$path" --jq '.content'
$name = ($path -split '/')[-1]
[IO.File]::WriteAllBytes("corpus\imported\$name",
    [Convert]::FromBase64String((($b64 -join '') -replace '\s','')))
```

For a file over 1 MB, use the blob API instead
(`gh api repos/joniles/mpxj/git/blobs/<sha>` → `.content`); that path was not
exercised here because no imported file needed it.

### 5.2 Inventory

```powershell
python scripts\import_corpus.py corpus\imported `
    --reader-bin build\vs-w19\Release\engine_diff_tests.exe `
    --out corpus\imported\inventory.tsv
```

Output tail (2026-10-23):

```text
files 12  bytes 2465280 (2.35 MiB)
version  MPP12=2  MPP14=4  MPP8=1  MPP9=5
reader   ok=5  rejected=7
duplicate content: none
wrote corpus\imported\inventory.tsv
```

`corpus/probe` (117 files, the existing synthetic fixtures):

```text
files 117  bytes 28097536 (26.80 MiB)
version  MPP12=15  MPP14=102
reader   ok=116  rejected=1
duplicate content: none
mixed version markers (newest wins):
  mpp12viewstate.mpp: Props12+Props9;112,19,212,29;mixed(MPP9+MPP12)
wrote corpus\imported\probe-inventory.tsv
```

### 5.3 Robustness

```powershell
python scripts\import_robustness.py corpus\imported `
    --reader-bin build\vs-w19\Release\engine_diff_tests.exe `
    --recalc-bin build\w62\tools\recalc\Release\recalc.exe `
    --out corpus\imported\robustness.tsv
```

```text
runs 24 over 12 file(s)
  reader ok         5
  reader rejected   7
  recalc ok         1
  recalc rejected  11
findings: none (every run was ok or a clean rejection)
wrote corpus\imported\robustness.tsv
```

Damaged copies (no crash, no silent acceptance of a short file):

```powershell
python scripts\import_robustness.py corpus\imported `
    --reader-bin build\vs-w19\Release\engine_diff_tests.exe `
    --mutate --out corpus\imported\robustness-mutate.tsv
```

```text
runs 60 over 12 file(s)
  reader ok         10
  reader rejected   50
findings: none (every run was ok or a clean rejection)
```

`corpus/probe` with both stages (234 runs, 3.5 s):

```text
runs 234 over 117 file(s)
  reader ok         116
  reader rejected   1
  recalc ok         98
  recalc rejected   19
```

### 5.4 Which reader binary

`--dump` exists only in `engine_diff_tests`, and the newest build of it in the
tree is `build\vs-w19\Release\engine_diff_tests.exe` (w19). Other builds matter
because they disagree, which is itself information:

| binary | MPP12 | `sf248.mpp` |
|---|---|---|
| `build\vs-w19\Release\engine_diff_tests.exe` | ok (10 tasks) | rejected: `FAT covers 768 sectors but the file has 959` |
| `build\vs-w40-reader-dump\Release\reader_dump.exe` | rejected | **accepted, 22 nonsense rows** |
| `build\w62\tools\recalc\Release\recalc.exe` | rejected (`no Task table`) | rejected (`task list is empty`) |

Always record which binary produced a report.

---

## 6. Results: the imported files against the current reader

Version, size, verdict and the feature each file exists to test. "Reader" is
`build\vs-w19\Release\engine_diff_tests.exe --dump`.

| file | MPP | bytes | reader | notable feature |
|---|---|---|---|---|
| `sample98.mpp` | MPP8 | 87,552 | **rejected** — `no Props12/Props14 stream` | oldest container: unversioned `Props` stream, storages `1`/`2`; 31 streams only |
| `caltest.mpp` | MPP9 | 81,408 | rejected (same) | calendar fixture in the Project 2000/2003 format |
| `bug1.mpp` | MPP9 | 127,488 | rejected | user-submitted bug file, MPP9 |
| `empty.mpp` | MPP9 | 73,728 | rejected | a project with nothing in it (67 streams) |
| `mpp9nulltasks.mpp` | MPP9 | 120,320 | rejected | null/tombstone task rows — the case `.mpp` readers usually crash on |
| `task-baselines-project2003-mpp9.mpp` | MPP9 | 164,864 | rejected | baseline fields in the MPP9 layout |
| `mpp12nulltasks-from14.mpp` | MPP12 | 186,368 | **ok**, 0 tasks | an MPP14 project saved down to MPP12 by Project (MPXJ's `-from14` naming); exercises `112/`+`212/` |
| `task-costs-project2013-mpp12.mpp` | MPP12 | 274,944 | **ok**, 10 tasks, 10 assignments | cost fields (`Cost`, `FixedCost`, per-task cost tables) in MPP12 |
| `calendar-recurring-exceptions-project2013-mpp14.mpp` | MPP14 | 242,176 | **ok**, 0 tasks, 2 calendars, **26 recurring exceptions** | daily/weekly/monthly/yearly recurrences, `month_item`/`month_position`, one *working* exception with two intervals |
| `resource-type-project2016-mpp14.mpp` | MPP14 | 245,760 | **ok**, 15 resources, 0 tasks | work/material/cost resource types, resource calendars |
| `task-baselines-project2019-mpp14.mpp` | MPP14 | 369,152 | **ok**, 110 tasks, 110 placeholder assignments, 0 resources | baselines on a modern file; placeholder assignment rows with the "no resource" sentinel |
| `sf248.mpp` | MPP14 | 491,520 | **rejected** — `FAT covers 768 sectors but the file has 959` | a real user project that MPXJ keeps as a regression fixture; compound-file slack |

### The `corpus/probe` sweep (117 files)

`reader ok=116, rejected=1`. The single rejection is
`mpp12subproject-from14.mpp`:

```text
MPP12 Task table at 112/TBkndTask/FixedData holds 3 data rows of 206 bytes each
(row 0 is the project row); the Project-2007 layout this reader implements is 264
bytes.  The file may be an MPXJ-written MPP12 or a different variant; its record
size is not guessed at.
```

That message is a model of how to fail: it names the stream, the observed stride,
the implemented stride, and refuses to guess. The import set adds nothing that
changes it — `mpp12nulltasks-from14.mpp` and `mpp12subproject-from14.mpp` are both
MPXJ-written MPP12 and both carry a 206-byte task row.

---

## 7. Edge cases these files expose

Each item is evidence, with the command that shows it.

1. **MPP8 and MPP9 are rejected outright.** 6 of the 12 imports are Project 98 /
   2000 / 2003 files and the reader answers `no Props12/Props14 stream and no
   112/TBkndTask or 114/TBkndTask table`. That is a *decision* in the reader, not
   a crash, and it is honest — but it means no claim about Project 98–2003
   compatibility can be tested yet. Real content now exists for that work:
   `sample98.mpp` (MPP8), `caltest.mpp`, `mpp9nulltasks.mpp`,
   `task-baselines-project2003-mpp9.mpp`.

2. **`tools/recalc` cannot read MPP12 at all, although the reader can.** All 15
   MPP12 files in `corpus/probe` fail in recalc with

   ```text
   recalc: <file>: no Task table: no stream '114/TBkndTask/FixedData' in compound
   file (an MPP14 file stores it at 114/TBkndTask/FixedData)
   ```

   while `engine_diff_tests --dump` reads the same files. The scheduling path and
   the reader therefore disagree about what is readable; anything built on
   `core::plan` (parity, app) inherits the MPP12 hole.

3. **A real MPP14 file is rejected for compound-file slack.** `sf248.mpp` has 959
   sectors but a FAT that describes 768 (`nfat=6`, `shift=9`). `olefile` reads all
   81 streams to their declared sizes, so the extra 191 sectors are unreferenced
   slack, not truncation. The reader treats a short FAT as fatal. A tolerant
   parser should only fail when a *chain walks past* the FAT, and warn otherwise.
   Before the guard existed, older reader builds returned 22 rows of nonsense
   (`uid 0`, `duration_min 429496729`) for this file — silently wrong, which is
   worse than rejecting it.

4. **A single flipped byte is accepted silently.** In `--mutate`, `flip-mid` is
   accepted on 5 of 12 files, and the dump changes size (`task-costs` 53,937 →
   24,547 bytes of JSON) without a non-zero exit. CFB has no checksums, so this is
   expected of any reader; record it so that "decodes" is never mistaken for
   "decodes the same". Truncation to 75%/25% and a zeroed header are rejected
   every time, and nothing crashed — the parser's bounds checks hold.

5. **Recurring calendar exceptions**: `.mpp` stores `recurrence_type`,
   `occurrences`, `period`, `days_of_week`, `month_item`, `month_position` and two
   raw recurrence words. The imported file has 26 of them across all types,
   including one *working* exception with two intervals
   (`[[5400,2400],[8400,1800]]` in 6-second ticks). This is the densest calendar
   test material in the tree.

6. **Version markers are not exclusive.** `corpus/probe/mpp12viewstate.mpp`
   carries `Props9` *and* `Props12`, and storages `19,29,112,212`. Any code that
   picks a layout by "is Props14 present?" or by hard-coded `114/` paths (see
   item 2) will be wrong on converted files.

7. **Subproject fixtures decode as garbage.** `corpus/probe/SubprojectA-9.mpp`
   and `SubprojectB-9.mpp` are MPP14 containers, but the reader warns
   `TBkndTask/FixedData is 872 bytes = 48 + 202*n + 16; the trailing bytes are
   ignored`, emits rows with `UniqueID 0` repeated and nonsense dates, and recalc
   then reports "the task list is empty". Either the files are unusual or the
   FixedData stride assumption does not hold for them; both are worth the reader
   owner's attention. (`python scripts\mpp_rows.py corpus\probe\SubprojectA-9.mpp`
   shows the same 48 + 4×202 split.)

8. **Empty projects are a legitimate non-zero exit.** Four MPP14 probe files with
   0 tasks (`SubprojectA-9`, `SubprojectB-9`, `w12-exceptions*`) make recalc exit
   1 with `the task list is empty`. A harness that treats exit 1 as "corrupt
   file" will mis-report these; keep the stderr text in the report, as
   `import_robustness.py` does.

### Gaps the imported files expose only as *material*, not yet as results

- **MPP12 written by MPXJ versus by Project**: the 206-byte row. Both imported
  MPP12 files that decode (`mpp12nulltasks-from14`, `task-costs-…-mpp12`) do so,
  so the 264-byte layout is right for Project 2007 output and the 206-byte
  variant is an MPXJ artefact — worth confirming with a file saved by Project
  2007 itself.
- **Baselines**: `task-baselines-…-mpp9` (unreadable) and `…-2019-mpp14` (110
  tasks, `Fixed2Data` outline warnings) — no test asserts baseline fields yet.
- **Cost resources / material resources**: `resource-type-…-mpp14` decodes 15
  resources; nothing compares their type or rate.
- **Master project / inserted subprojects**: not covered by anything imported;
  `mpp12subproject*.mpp` and `mpp14subproject.mpp` in MPXJ are the candidates.
- **Password-protected files** (`legacy/*password9.mpp`): a reader must fail
  cleanly, not hang. Not imported.
- **Corrupt files**: the only corruption tested is synthetic (`--mutate`). If a
  genuinely damaged real file is ever obtained, it belongs here with a note on
  what is expected to happen.

---

## 8. Next imports (ranked, all from MPXJ, all under 500 KB)

| why | path | bytes |
|---|---|---|
| subproject/master, MPP14 | `junit/data/mpp14subproject.mpp` | 177,664 |
| split tasks, MPP14 | `junit/data/mpp14splittask.mpp` | 176,640 |
| cost rate tables, MPP14 | `junit/data/mpp14costratetable.mpp` | 182,272 |
| recurring tasks, MPP14 | `junit/data/mpp14recurring.mpp` | 202,240 |
| enterprise/global, MPP12 | `junit/data/mpp12enterprise.mpp` | 430,080 |
| password-protected, MPP9 | `junit/data/password-protected-mpp9.mpp` | 74,240 |
| MPP9 split tasks | `junit/data/mpp9splittask.mpp` | 77,312 |
| 9 Ruby-spec samples (1.68 MB total) | `src.ruby/mpxj/spec/*.mpp` | 180,736–200,704 each |

Avoid the 11.8 MB `junit/data/calendar-exception-precedence.mpp` unless the
20 MB budget is raised; the `-{daily,weekly,monthly,yearly}` variants carry the
same feature at ~240 KB.

## 9. Cleanup

`corpus/imported/` may be emptied at any time; nothing else depends on it. The
reports in it are regenerated by §5. The `.import-scratch` /
`.robustness-scratch` directories are removed by the scripts; if one is left
behind by an interrupted run, delete it — it contains only dumps and damaged
copies, never a modified original.
