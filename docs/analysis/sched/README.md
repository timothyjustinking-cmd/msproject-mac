# Scheduling analysis (W3)

Deliverable: [`spec/scheduling.md`](../../../spec/scheduling.md).
Tooling: `scripts/sched_index.py` (decompile index/query),
`scripts/sched_analyze.py` (static evidence report),
`scripts/sched_oracle_recipes.py` (recipes for the coordinator-run oracle).

## What was established statically

| fact | evidence |
|---|---|
| Date = `date32`: day<<16 \| tenths-of-minute; day 1 = 1984-01-01; 0=Sun | `FUN_140200b10`, `FUN_14038bdb0`; `analysis/flat_buffer_field_map.tsv:21` |
| Durations/work are integer tenths of a minute | `FUN_140200b10`; `scripts/phase2d_task_record.py:134` |
| Calendar day = count + up to 8 (start,len) intervals in a 60-byte record | `FUN_140200cac`, `FUN_14085ae80` |
| Effective week cache: start/finish/minutes at cal+0x1c0/+0x1c2/+0x1c4 | `FUN_140203ccc` |
| AddWork = whole weeks → whole days → interval walk; finish = start+duration | `FUN_14024e7c0`, `FUN_140200b10` |
| SubWork is the symmetric backward walk | `FUN_14038bdb0`, `FUN_14038c410`, `FUN_14038c530`, `FUN_1403da710` |
| Recalc order: scheduling-direction pass, opposite pass, optional repeat, outline rollup | `FUN_1402b5360` |
| Pass repeats over elements until no element reports a change | `FUN_1402b570c` (`+0x170`, `+0x208`, `+0x128`) |
| Task type at node+0xf2; effort-driven = bit 11 of node+0x1c; mode map 0..4 | `FUN_1404e9400` |
| Constraint type at node+0x84, date at +0x78; changing type migrates the date | `FUN_1401f2570` case `0xb400011` |
| Enums: PjConstraint 0..7, PjTaskFixedType 0..2, PjTaskLinkType 0=FF,1=FS,2=SF,3=SS | `analysis/typelib.tsv` |
| TotalSlack is computed; StartSlack/FinishSlack/FreeSlack are stored | `scripts/phase2d_slack_verify.py:110-114` |
| Graph validation rejects inconsistent/cyclic pred/succ lists | `FUN_1404ac7b0` |

## What is still open (needs the oracle)

1. Link lag anchor (next working instant vs predecessor finish) — `W3-O3`.
2. Duration/work/units triangle and internal units scale — `W3-O2`.
3. Upper-bound constraints (SNLT/FNLT), deadlines, negative slack — `W3-O4`.
4. Elapsed month length and hours-per-day conversion interaction — `W3-O5`.
5. Summary criticality/slack and free-slack clamp — `W3-O6`, `W3-O7`.

## Traps

- The decompile is stripped: nearly all engine functions are `FUN_*`; the CPM
  arithmetic is behind virtual calls (`TaskElement` 364-slot vtable), so
  call-graph walking stops at the interface. The spec states the required
  semantics and the entry points, not a line-by-line formula from the binary.
- `0xFFFFFFFF` is NA in date fields; `0xECD73836` is the walker overflow
  sentinel — neither is a usable date.
- Link types are **not** in FS/SS/FF/SF order: 0=FF, 1=FS, 2=SF, 3=SS.
- Slack display rounds; keep tenths internally and round at the COM boundary.
