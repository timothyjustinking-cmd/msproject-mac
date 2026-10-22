# io stream (MSPDI import/export) — memory, W33 parity breadth

_Updated 2026-10-22 ~14:55 (W33). The import/export design is `spec/mspdi.md`
(mapping, conversion, warnings, writer rules). This file carries what W27 and
W33 added and the cross-check against Project's own exports. Successor: read
`spec/mspdi.md` §4–§8, then this._

## W33 deliverable on disk (coordinator commits; no git from this stream)

| path | what |
|---|---|
| `tools/mspdi_cmp/crosscheck.{h,cpp}` | + **dead-endpoint accounting** for links and assignments, + **resources** (UID set, UID 0 placeholder excluded), + **assignments** (task/resource UID, `Units`, `Work`), + **milestone notes** (reported, not a verdict). `ok()` ignores excluded rows and notes; `strictly_ok()` also fails on notes |
| `tools/mspdi_cmp/main.cpp` | report/JSON for the new categories; `--strict`; exit 0 iff `ok()` (plus no notes under `--strict`) |
| `tools/mspdi_cmp/date_pairs.py` (new) | read-only diagnostic: prints the rows where the stored calculated dates `+96/+100` differ from the effective dates `+104/+108`; uids/dates/counts only. Used to attribute the PROJECT_B Start/Finish difference |
| `core/io/mspdi.h` | `LinkLagSpelling` + `parse_link_lag` / `format_link_lag_decimal` / `format_link_lag_iso`; `MspdiAssignmentRef::resource_uid` documents the -65535 sentinel |
| `core/io/mspdi.cpp` | LinkLag parsing centralised in `parse_link_lag` (**decimal is canonical**, ISO still accepted); `<ResourceUID>` now parses **signed** 32-bit (`-65535` → `0xFFFF0001`, no warning) |
| `core/io/mspdi_writer.cpp` | **`<LinkLag>` is now decimal tenths of a minute** (`4800`), the form Project exports; previously ISO `PT8H0M0S`. Exact string pinned by the writer test |
| `core/io/CMakeLists.txt` | PROJECT_B and assign1 compile definitions for the cross-check test |
| `tests/io_mspdi_crosscheck.cpp` | + dead-endpoint tests (both sides), assignment/resource comparison, assignment Units/Work negative controls, PROJECT_B case, assign1 no-export case |
| `tests/io_mspdi_tests.cpp` | +19: both LinkLag spellings and their exact text, `-65535` ResourceUID sentinel |
| `tests/io_mspdi_writer_tests.cpp` | +2 net: writer output pins `<LinkLag>4800</LinkLag>` and asserts the ISO form is *not* emitted; `format_link_lag_iso` still returns `PT8H0M0S` |

Reader tree this was measured against (W31 changed `core/mpp` twice during the
session, so the hash matters): `core/mpp/reader.h` sha256
`BFB17BC3DF7A05190BBF742C686DB6BED1124D3E740C8C02518592660FDD5F1A` (34,625 B,
14:45:11), `core/mpp/reader.cpp` sha256
`FB2F1BECCB7CEA69C951737FD9C041828745E1D0FAEB4862C57D8A11BC09CF45` (79,628 B,
14:44:50). The first measurements (14:40 reader, milestone stored bit) gave the
same PROJECT_B/PROJECT_A counts. **Re-run `mspdi_cmp` after any `core/mpp` change.**

## mspdi_cmp results (exact, 2026-10-22 ~14:52, MSVC 19.44 `/W4 /permissive- /utf-8 /WX`)

```
build\vs-w33\tools\mspdi_cmp\Release\mspdi_cmp.exe corpus\project_a.mpp corpus\probe\project_a-export.xml
build\vs-w33\tools\mspdi_cmp\Release\mspdi_cmp.exe corpus\project_b.mpp   corpus\probe\project_b-export.xml
build\vs-w33\tools\mspdi_cmp\Release\mspdi_cmp.exe corpus\probe\lagff1.mpp       corpus\probe\lagff1.xml
```

| file | tasks (reader/xml) | fields | links (reader live/xml/matched) | assignments | resources | field diffs | exit |
|---|---|---|---|---|---|---|---|
| **PROJECT_A** | 83 / 84 (UID 0 excluded) | 830, **0 mismatches** | 88 / 88 / 88 | 67 / 67 / 67, units 67, work 67, **0 diffs** | 22 / 23 (1 placeholder excluded), 22 matched | 0 | **0 (MATCH)** |
| **PROJECT_B** | 73 / 74 (UID 0 excluded) | 730, **2 mismatches** | 72 (70 live, **2 dead**) / 70 / 70 | 73 (63 live, **10 dead**) / 63 / 63, units 63, work 63, **0 diffs** | 22 / 23 (1 placeholder), 22 matched | 2 (uid 10805 Start+Finish) | **1** |
| **lagff1** | 6 / 7 (UID 0 excluded) | 60, **0 mismatches** | 5 / 5 / 5 | 6 / 6 / 6, units 6, work 6, **0 diffs** | 0 / 1 (placeholder only), 0 compared | 0 | **0 (MATCH)** |
| **assign1** | 2 reader tasks | — | — | **3 reader assignments, 2 resources; no MSPDI export on disk** | — | — | not comparable |

PROJECT_A and lagff1 are clean: every compared task field, every live link, every live
assignment (including Units and Work) and every resource UID matches.
**PROJECT_B's only remaining parity difference is 2 date fields on one task**; all
links, assignments and resources agree once the dead-endpoint rows are accounted
for, and there are 0 milestone notes with the current reader.

### Dead-endpoint accounting (the "2 extras")

Definition: a link or assignment is **dead-endpoint** when one of its endpoint
UIDs is not a live task in EITHER model (the reader's task list or a non-project
XML task). Deleted task slots stay in `TBkndTask`/`TBkndCons`/`TBkndAssn`, and
the reader still returns the rows that reference them; Project neither schedules
nor exports them, so no export can match those rows. They are **counted, listed
and excluded from the verdict**, never silently dropped:

* PROJECT_B links: reader 72, live 70, excluded 2 — `10722→10723` and
  `10723→10724`, both FS lag 0. Neither 10722, 10723 nor 10724 is a live task
  (they are not in the export's 73 task UIDs either). 70 live links match 70/70.
* PROJECT_B assignments: reader 73, live 63, excluded 10 — UIDs 7475, 9689,
  9692–9697, 9702, 9703. Every one references a task UID that is **not live**
  (8142, 10637, 10722–10727, 8141, 10732). The 63 live assignments match 63/63
  on task UID, resource UID, Units and Work.
* PROJECT_A and lagff1 have 0 dead rows, so the accounting does not weaken their gates.

A row whose endpoint is live in one model only is **not** dead: it stays a
difference (the task UID difference is reported separately), so a genuine reader
bug cannot hide behind this rule.

### Resources and assignments (new comparison)

* **Resources**: compared as a UID set. Project's export always carries a UID 0
  placeholder resource (`IsNull`, the "no resource" row) that the reader does not
  return; it is excluded and printed (`xml 23 (1 placeholder resource uid 0
  excluded)`). PROJECT_A/PROJECT_B: 22 reader = 22 live XML. lagff1: 0/0.
* **Assignments**: matched by assignment UID; task UID and resource UID compared
  always, `Units` and `Work` only when the export carries them (counts printed).
  * `Units` is a **fraction in MSPDI** (`1` = 100 %) but the reader's
    `units()` is already a **percentage** (`100.0` = 100 %), so the comparison
    normalises XML × 100 and reports percent. PROJECT_A/PROJECT_B/lagff1 all export
    `Units=1` and the reader stores 100 % on all 136 live assignments.
  * `Work`: the export's ISO duration (`PT80H0M0S`) is compared in 6-second
    ticks with `reader.work_ticks()`. It matches on all 136.
  * `ResourceUID=-65535` (Project's "no resource", bits `0xFFFF0001` = the
    reader's `kNullResourceUid`) is normalised to "none" on both sides. **All 136
    live assignments in the three exports carry -65535**: these programmes have
    no real resource assignment, so Units/Work is the substantive comparison.
    `corpus/probe/assign1.mpp` is the only file with real resource assignments
    (3 assignments, 2 resources) and the coordinator has not exported it, so real
    resource-UID parity is still unmeasured against an export.

### Milestone (reader now stored-bit; W31 owns core/mpp)

The reader used to derive `Milestone` as `duration == 0`. **W31 landed the
stored FixedMeta milestone bit during this session** (`reader.h`: bit 0x02 of
byte +10 of the task FixedMeta entry, duration fallback only when the row has no
entry). Consequence for the PROJECT_B count:

| reader | PROJECT_B milestone notes | which |
|---|---|---|
| duration-only (before ~14:36) | **2** | uid 10735 duration 48,000 ticks = **10 d**, uid 10799 = 1 d; both `Milestone=1` in the export |
| stored-bit (current) | **0** | both read from the stored bit |

`mspdi_cmp` still reports a Milestone disagreement as a **note**, not a parity
failure: the reader can fall back to duration when the row has no FixedMeta
entry, so the note text says which path produced the value (`(stored FixedMeta
bit)` or `(duration fallback)`). `--strict` promotes notes to failures.
(The coordinator brief said 10735 is 5 d; the measured duration is 48,000 ticks
= 10 × 4,800 and the export says `PT80H0M0S` = 80 h = 10 d.)

## PROJECT_B parity conclusion: real bug vs stored-vs-recalc

**The 2 field differences are a stored-vs-effective date-pair difference, not an
MSPDI import bug and not a recalc-on-export.**

Evidence (re-runnable, uids/dates only, no names):

```
python tools\mspdi_cmp\date_pairs.py corpus\project_b.mpp
#   row  uid    dur_ticks  +96 start           +100 finish          +104 eff start       +108 eff finish     same
#    85  10805          0  2026-10-15 17:00    2026-10-15 17:00     2026-03-23 08:00     2026-03-23 08:00    NO
#   summary: 85 FixedMeta-covered task rows, 1 with +96/+100 != +104/+108
```

* Exactly **one** row in PROJECT_B has `+96/+100 != +104/+108`; PROJECT_A and lagff1 have
  **zero** such rows (why their date comparison is exact).
* `mspdi_cmp` prints for uid 10805: reader `2026-10-15 17:00`, XML
  `2026-03-23 08:00`. The reader's Start/Finish are `+96/+100` (reader.h,
  `Task.start/finish`); the XML values equal **`+104/+108` exactly** on that
  same row. So the export writes the effective pair, the reader returns the
  calculated pair, and the file has them out of sync (PROJECT_B was edited and not
  fully recalculated).
* This is a **core/mpp model decision** (should `Task.start/finish` expose
  `+96/+100`, `+104/+108`, or both?), not a bug in `core/io`. If W31 switches
  Start/Finish to the effective pair, PROJECT_B becomes a clean MATCH (0 field
  differences); the crosscheck test deliberately does not pin the residual pair.

**What is a real bug vs not, for PROJECT_B:**

| finding | verdict |
|---|---|
| 2 extra reader links `10722→10723`, `10723→10724` | **not a parity failure** — endpoints are deleted task slots; named exclusion |
| 10 extra reader assignments (uids above) | **not a parity failure** — same deleted slots; named exclusion |
| Start/Finish on uid 10805 | **not an io bug** — `+96/+100` vs `+104/+108` (verified above); reader-model question for W31 |
| 2 Milestone flags (10735, 10799) | **reader bug, now fixed by W31** (stored bit); cross-check note count is 0 |
| links/assignments/resources/tasks otherwise | **0 differences** |

## LinkLag canonical choice (deliverable 3)

* **Decimal tenths of a minute is canonical**: it is what Project exports, one
  tenth == one 6-second tick == the reader's link lag, and it is what the writer
  now emits. `parse_link_lag` returns `LinkLagSpelling::decimal` / `::iso` /
  `::none` and tries decimal first.
* **ISO stays an import spelling** for the committed hand-written sample
  (`tests/io_sample_tasks.xml`, `PT8H0M0S`). `format_link_lag_iso` still produces
  the exact legacy text (pinned).
* The writer change is pinned by exact string, as required:
  `io_mspdi_writer_tests` asserts the output contains `<LinkLag>4800</LinkLag>`,
  does **not** contain `<LinkLag>PT8H0M0S</LinkLag>`, and
  `format_link_lag_iso(4800) == "PT8H0M0S"`.
* The round-trip sample (ISO in) still works because both spellings parse to the
  same ticks; the writer's output re-imports with 0 warnings (writer test).

## Verification (2026-10-22 ~14:52, MSVC 19.44, `/W4 /permissive- /utf-8 /WX`)

```
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w33 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w33 --config Release --target mspdi_cmp io_mspdi_tests io_mspdi_writer_tests io_mspdi_crosscheck io_csv_tests"
cmd /c "call `"$vc`" >nul && ctest --test-dir build\vs-w33 -C Release -R io_ --output-on-failure"
```

* `io_mspdi_tests`: **144 checks passed** (was 125; +19).
* `io_mspdi_writer_tests`: **862 checks passed** (was 860; +2 net).
* `io_mspdi_crosscheck`: **109 checks passed** (was 23; new dead-endpoint,
  assignment/resource, PROJECT_B and assign1 cases; corpus-gated, prints SKIP for a
  missing file).
* `ctest -R io_`: **4/4** (`io_mspdi`, `io_csv`, `io_mspdi_writer`,
  `io_mspdi_crosscheck`).
* Clean build under `/WX`; no warnings.

Negative controls run by hand (not committed):

* `lagff1.xml` copy with one `Duration` changed `PT8H0M0S`→`PT9H0M0S` →
  `1 field mismatches`, exit 1.
* missing `.mpp` / missing `.xml` → exit 1 naming the path; no arguments → exit 2.
* `mspdi_cmp ... --json <path>` → valid JSON (checked with
  `ConvertFrom-Json`); `ok`/`strictly_ok`/`counts.total` are present.
* `--strict` on PROJECT_A → exit 0 (0 notes).

The tool never reads a task name; the table and JSON carry uids, field names,
dates, numbers and counts only.

## Verified vs inferred

* **Verified**: PROJECT_A XML (84 Task incl UID 0, 88 links, 23 Resource incl UID 0,
  67 Assignment) and full agreement; PROJECT_B XML (74 Task incl UID 0, 70 links,
  23 Resource incl UID 0, 63 Assignment) and 70/70 links, 63/63 assignments,
  22/22 resources agree; 2 dead links + 10 dead assignments reference non-live
  task UIDs; PROJECT_B uid 10805 is the only row whose `+96/+100` differs from
  `+104/+108`, and its export dates equal `+104/+108`; all 136 live assignments
  export `Units=1` and `ResourceUID=-65535`; Work matches on all 136; the writer
  emits `<LinkLag>4800</LinkLag>`; both LinkLag spellings parse to the same ticks;
  `-65535` imports without a warning as `0xFFFF0001`.
* **Inferred**: that Project's *export* uses `+104/+108` as its date source is
  inferred from the one differing row where the values are byte-equal (all other
  rows have the pairs equal, so they cannot distinguish the two); that the
  PROJECT_B file was "edited without a full recalc" is the explanation consistent
  with that evidence, not separately measured; that Project's *importer* accepts
  the writer's decimal `<LinkLag>` is inferred from it being Project's own export
  spelling (no oracle FileOpen of writer output).
* **Not checked**: real resource-assignment parity (assign1 has no export); MPP
  date-only/UTC behaviour; whether a writer-produced file opens in Project.

## Open items for the next io session

1. **PROJECT_B `+96/+100` vs `+104/+108`** is now a `core/mpp` question (W31): if
   `Task.start/finish` moves to the effective pair (or exposes both),
   `mspdi_cmp` PROJECT_B drops from 2 field differences to 0. Do not paper over it
   in `core/io`.
2. **Assignment Units convention in the model**: `import_mspdi` stores the XML
   value raw, so a real export's `Units=1` lands as 1.0 while the hand-written
   sample's `100` lands as 100.0 in the same PjField. The cross-check normalises
   for comparison only; a view that formats Units would show 1 % vs 100 %. Needs
   a fixture with a variable assignment unit before changing the importer.
3. **Real resource assignments**: ask the coordinator for an `assign1.xml`
   export (`FileSaveAs FormatID="MSProject.xml"` of `corpus/probe/assign1.mpp`);
   the cross-check will then exercise real resource UIDs, and 2 resources.
4. **Writer acceptance**: one coordinator-only `FileOpen` of a writer-produced
   MSPDI file would settle element order *and* the decimal `<LinkLag>`; still
   unverified.
5. `tests/io_mspdi_crosscheck.cpp` still reports the PROJECT_A reader differences
   instead of pinning agreement; it asserts `ok()` when it holds and can be
   tightened once the reader is frozen (unchanged from W27).
6. `spec/mspdi.md` is behind the code — see "Spec notes" below (coordinator
   edits `spec/`; this stream must not).

## Spec notes for `spec/mspdi.md` (for the coordinator)

* §4 Task: no change.
* §4 Resource: add that Project's export writes a **UID 0 placeholder resource**
  (`IsNull`) and that the cross-check compares the UID set excluding it.
* §4 Assignment: `Units` in Project's export is a **fraction** (`1` = 100 %),
  while the reader stores a percentage; `ResourceUID=-65535` is the
  **"no resource" sentinel** (`0xFFFF0001`); `TaskUID`/`ResourceUID` come from
  `assignment_refs`; `Work` is an ISO duration in ticks. The importer stores
  `Units` raw (known inconsistency, open item 2).
* §4/§5 `PredecessorLink`: **`LinkLag` is canonical as decimal tenths of a
  minute**; ISO is accepted only as the legacy hand-written-sample spelling;
  `parse_link_lag`/`format_link_lag_decimal`/`format_link_lag_iso` are the API.
* §6: add the `-65535` `ResourceUID` acceptance (no warning).
* §8.3: the value-rules table's `Duration` row no longer describes `LinkLag`:
  a link's `<LinkLag>` is decimal tenths (`4800`), pinned by the writer test;
  ISO is no longer emitted.
* §8.4/§8.5: writer output changed for `LinkLag` only; the three sample
  round-trips still pass because both spellings parse to the same ticks.
* §9: counts are now `io_mspdi_tests 144`, `io_mspdi_writer_tests 862`,
  `io_mspdi_crosscheck 109`, `ctest -R io_ 4/4`, PROJECT_A/lagff1 `mspdi_cmp` exit 0.
* §10: add the `mspdi_cmp` exclusions (dead endpoints, milestone notes), the
  PROJECT_B conclusion (2 stored-vs-effective date fields; 0 after a reader
  date-source fix), and replace "PROJECT_B has no MSPDI export yet" with the
  results above. Assignment/resource comparison is no longer a gap; real
  resource assignments are.

## Traps (do not relearn)

* **`LinkLag` in a real export is decimal tenths of a minute.** The writer now
  emits that form; do not "restore" ISO without a test that pins the exact
  string and evidence that Project's importer needs it.
* **`ResourceUID` can be negative.** `-65535` is "no resource"; parse it as
  signed bits, not with an unsigned-only parser (it used to warn once per
  assignment and fold to 0).
* **MSPDI `Units` is a fraction; the reader's `units()` is a percentage.**
  Multiplying one side by 100 is the comparison rule, not a data fix.
* **A real export excludes rows whose task slot was deleted**, and the reader
  does not. That is what dead-endpoint accounting is for; it is an exclusion with
  a printed count, not a tolerance.
* `core/mpp` changed under this session twice (W31 milestone stored bit,
  ~14:36–14:40). Rebuild and re-run `mspdi_cmp` after any reader change; the
  PROJECT_B milestone-note count and the date pair above depend on it.
* `ctest` is not on PATH without `vcvars64.bat`; `ctest -R io_` also needs
  `io_csv_tests` built or it reports "Not Run".
* The file-observation policy makes each `write`/`edit` require a fresh `read`
  of the file; expect to re-read between edits.
* The XML's UID 0 task is the project summary row; the resource UID 0 is the
  placeholder resource. Exclude both or every run reports a phantom record.
