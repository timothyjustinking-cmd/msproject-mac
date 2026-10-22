# Oracle results — 2026-10-22

Raw output of experiments run against **MS Project 16.0** (WINPROJ.EXE) by the
coordinator. Agents cannot reach the oracle (COM hangs in a confined shell), so
these files are the evidence the specs cite.

Every file is JSON produced by one of:

| script | what it does |
|---|---|
| `scripts/oracle_w3.py o1\|o3\|o4\|o4b` | builds schedules over COM and reads them back |
| `scripts/oracle_writer.py patch\|rebuild\|name\|base` | opens the writer's fixtures, reads, Save As |
| `scripts/oracle_writer.py diff` | byte-diffs a resaved file against its source |
| `scripts/oracle_patch_variants.py make\|open` | single-field bisect of the refused fixture |
| `scripts/oracle_wave7.py export` | opens PROJECT_A, CalculateProject, saves MSPDI (`FormatID="MSProject.xml"`); writes `project_a_export_links.json` |
| `scripts/oracle_wave7.py holiday\|milestone\|lagff` | builds the calendar, 0-duration and FF-lag fixtures over COM |

| file | content |
|---|---|
| `w3-o1.json` | four tasks on a blank project: finish instants, weekend skip, mid-day start; Project's own save is `corpus/probe/w3-o1.mpp` |
| `w3-o3.json` | FS/SS/FF/SF with lag 0, +1d, -1d, +2h |
| `w3-o4.json` | the eight constraint types, feasible and infeasible predecessor |
| `w3-o4b.json` | deadlines with negative slack, ASAP+ConstraintDate side effect, CriticalSlackLimit |
| `links.json` | which COM call can create a dependency (`Task.LinkPredecessors` / `TaskDependencies.Add`), and that `Task.GetField` returns **formatted text**, not a raw value |
| `writer-{patch,rebuild,name,base}.json` | values Project showed for each written fixture |
| `writer-diff.json` | every stream of the four resaves vs its source |
| `patch-variants.json` | one-field edits of `base.mpp`, each opened in Project |
| `project_a_export_links.json` | the authoritative MSPDI link set of the real programme: 88 links, succ/pred uids, type, lag; constraints/deadlines/actual-row counts. Uids and dates only. The XML itself is `corpus/probe/project_a-export.xml` (git-ignored) |
| `project_b_export_links.json` | the same for PROJECT_B: 74 Task elements (incl. UID 0), **70 links**, 1 constraint, 0 actuals. Uids only; the XML is `corpus/probe/project_b-export.xml`. The reader returns 72 links: the 2 extras point at deleted task slots (Project does not schedule them) |
| `holiday-probe.json` | `SiteCal` on the **task** calendar (row +178 = 3): Friday 08:00-12:00 + a non-working Wed 2026-10-21; the project default stayed Standard. Project's three task dates are the proof for `core/cal` |
| `milestone-probe.json` | 0-duration tasks: FS+0 / FS+50d at 17:00, SS+0 at 08:00, an assigned 17:00 start normalised to SNET next-morning |
| `lagff-probe.json` | FF-1d/-3d/-6d/+0/-1ed on a 5-day predecessor: negative lag applied, floored at the project start |
| `w3-o1-open.json` | a Project-saved file opened read-only and read over COM: the six FixedData date words each equal one of the six reported date fields, and the values are local wall clock (not UTC). Start and EarlyStart are equal in this file, so it cannot separate +96 from +104 — the manual-edit probe below does |
| `w26-open.json` | the seven W26 writer variants opened in Project: a manual task ignores +96/+100, +104/+108, +112/+116 and +84/+88, while a Fixed2Data change moves its Finish, Duration and the project finish |
| `w26-open-e1e2.json` | the E1/E2 split: Fixed2Data +46/+50 alone and +54 alone both move the Finish (and nothing moves Start) |
| `manual-start-edit.json` | Project's own save after `Task.Start` is set on a manual task: it writes `+104/+108`, `+112/+116` and Fixed2Data `+50/+54`, leaving `+96/+100` and Fixed2Data `+46` at their old values (`corpus/probe/manual-start-edited.mpp`). The effective Start/Finish are `+104/+108` |
| `w29-open.json` | the writer save-path test: **SNET works** — `w29-snet.mpp` changes only `+64/+66` and Project puts uid 1 on 2026-10-21 08:00-17:00. The seven-word manual write (`+104/+108`, `+112/+116`, `+174`, Fixed2 `+50/+54`) moves Finish but **not** Start |

Provenance rules:

* `corpus/` (including `corpus/probe/`) is git-ignored and holds synthetic probes
  plus a client programme. **No client data is in these JSONs.**
* `corpus/probe/base.mpp` is synthetic (built by `scripts/oracle_probe.py` via
  COM) and is the only `.mpp` that may be committed, as a test fixture under
  `tests/data/`.
* Host-specific paths are relative to the repository root.

Reproduce (any of them starts and closes its own Project instance):

```
python scripts/oracle_w3.py o1
python scripts/oracle_writer.py base
python scripts/oracle_writer.py diff
```

Known trap, learned the hard way: pass dates to Project as **UTC-aware**
`datetime`s. pywin32 converts a naive datetime to UTC before the call, which
shifts wall-clock times by the local offset and silently changes what the
experiment measures.
