# Handoff — scheduling (W3)

Date: 2026-10-22. Branch: none (single-tree model; coordinator commits).

## Done

- `spec/scheduling.md`: scheduling behaviour spec, evidence-tagged, with
  falsification cases and an oracle request list.
- `scripts/sched_index.py`: query the 2.59 M-line decompile by function,
  string, field id, caller/callee, and vtable slot.
- `scripts/sched_analyze.py`: re-runnable static evidence report backing the
  spec's **[B]** claims.
- `scripts/sched_oracle_recipes.py`: ten precise COM recipes for the
  coordinator, five of them high-value.
- `docs/analysis/sched/README.md`: evidence map and traps.

## Not done

- No oracle results (COM hangs for agents; coordinator-only). Every **[I]** /
  **[O]** tag in the spec is therefore unconfirmed.
- The CPM per-element arithmetic behind `TaskElement::vfunc_65` and
  `SetDependantFields` is not decoded line-by-line; the spec states required
  behaviour, not recovered code.
- No engine code (out of scope for this wave).

## Next three actions

1. Coordinator runs `W3-O1`, `W3-O2`, `W3-O4`, `W3-O3`, `W3-O5`; send JSON back.
2. Update `spec/scheduling.md`: replace **[I]**/**[O]** with measured values and
   attach fixture ids; keep the binary citations.
3. Locate `TTaskTable::SetDependantFields` implementation (use the lambda/RTTI
   xrefs and the 0x1404ddxxx cluster) and write the fixed-point triangle section.

## Traps

- Agents cannot run COM or git; do not retry either.
- Date 0xFFFFFFFF is NA and 0xECD73836 is an overflow sentinel.
- Link enum numbering is 0=FF,1=FS,2=SF,3=SS.
- Work/duration are tenths of a minute, not minutes.
