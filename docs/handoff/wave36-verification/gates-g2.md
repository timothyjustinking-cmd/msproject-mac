# Wave 36 G2: the builder's gates

Builder Claude Sonnet 5.5, branch `wave36`.  G2 is the Props group: the trace gate's spans (W36-2.0), the derived Props size and count words (W36-2.1), the new-record framing of the Props
writer (W36-2.2), the plan's Props digest (W36-2.3, closes R35cG0-4) and the split of the guard meta-test into its own ctest (W36-2.4).  Logs are under `build/w36/` (git-ignored), each named for the
parent commit of the item it gates.  The builder used no COM, did not touch the VM, `build/trace-2021/` (read only, by `tools/trace/span_probe.py`, in W36-2.0) or `build/r7/`, and pushed nothing.
W36-2.0 to 2.3 were built in an earlier session; W36-2.4 was checkpointed at `ba49c23` when the operator paused the wave, and was finished in this one.

## Per item

Doc gates for each commit, in order: `check_fixtures`, `check_markers`, `guard_names`, `scan_client_names`, the `client-string-[0-9a-f]{8}` grep (none), `git diff --check`: all clean at W36-2.1, 2.2, 2.3
(`8d0d032-W36-2.1-gates.status`, `7125071-W36-2.2-gates.status`, `7961a6e-W36-2.3-gates.status`) and at W36-2.4 (`86044a0-W36-2.4-scans2.log`).  W36-2.0 has no scan log of its own; the range
`74a94cf..HEAD` was scanned again at the tip (`bfd4c33-W36-G2-range.log`: `guard_names` 0, `check_markers` 0, `git diff --check` clean) and `scan_client_names` ran on `HEAD` (0).

| item | commit | red | green |
|---|---|---|---|
| W36-2.0 | `8d0d032` | the test's new span ends against the old gate: 8 FAIL, 1731 PASS (`74a94cf-W36-2.0-red.log`) | `trace_gate_guards` 1739 PASS, 0 FAIL (`74a94cf-W36-2.0-green2.log`); named guard `stamp-span-vm` red when its top is removed (`74a94cf-W36-2.0-meta-pick.log`) |
| W36-2.1 | `7125071` | the size word off by one: `mppwrite_roundtrip_props`, `_assignment_growth`, `_resource_growth` FAIL, 3 of 8 words (`8d0d032-W36-2.1-red.log`) | core `ctest -E plan_recalc` 67/67; `plan_recalc` with `MPP_NO_CORPUS=1` 1/1 (`8d0d032-W36-2.1-green-core.log`, `-green-plan.log`) |
| W36-2.2 | `7961a6e` | the +12 count word left alone: `mppwrite_roundtrip_props` FAILs, "the record count word at header +12 is 262" (`7125071-W36-2.2-red.log`) | `roundtrip_props` 2717 checks, 0 FAIL (`7125071-W36-2.2-green-props.log`; the commit message of 2.2 says 2763, corrected in 2.3's); core 67/67; `plan_recalc` 1/1 |
| W36-2.3 | `aa025f1` | the digest refusal switched off: `app_edits_calculate` FAILs 7 checks (`7961a6e-W36-2.3-red-ctest.log`) | `app_edits_calculate` passes; core 67/67; `plan_recalc` 1/1; app 34/34 (`7961a6e-W36-2.3-green-*.log`) |
| W36-2.4 | `bfd4c33` (split in `ba49c23`) | before: the meta-test ran at the end of `test_trace_gate.py` (lines 3926 and 3928) and `ctest -N -L trace_gate_meta` listed 0 tests (`aa025f1-W36-2.4-red.log`) | `-L trace_gate_meta` lists `trace_gate_meta` alone, `-R trace_gate_guards` lists `trace_gate_guards` alone, `-LE trace_gate_meta -R trace_gate` lists `trace_gate_guards` alone (`86044a0-W36-2.4-registration-L.log`); `trace_gate_guards` passes (125 s on the checkpoint, 54 s in the group run); one full `trace_gate_meta` run passes in **75 minutes** (4528 s) |

## The meta-test, timed (W36-2.4)

One full `ctest -C Release -L trace_gate_meta` on a fresh configure of `86044a0` (`build/w36/86044a0-W36-2.4-meta-full.log`, six gate copies at a time; the pre-commit scans ran in its first minutes and nothing else after them):
**exit 0 in 4528 s, 75 minutes.**

* `named guards listed 184, red 184, not red 0`
* `candidates derived 299 (run 299): removed red 283 (85 of them by a case that asserts the gate does not raise), listed redundant 16, green 0, crashed 0`
* `the unmodified gate: passes`

It is one process, so the closing loop over `REDUNDANT` (which raised a `KeyError` in the partial runs of G0e and G0f) ran to the end without error.  An earlier attempt at the checkpoint (`aa025f1-W36-2.4-meta-timeout3600.log`)
hit the old ctest `TIMEOUT` of 3600 s at 60 minutes; a second (`aa025f1-W36-2.4-green-meta.log`) was stopped by the coordinator at 70 minutes.  Neither is evidence.  The ctest `TIMEOUT` is 10800 s and the
`trace-gate-meta` CI job's limit is 200 minutes.

## Per group, at `86044a0` (plus the CMake comment and CI job limit of `bfd4c33`, which change no build output)

Logs `build/w36/86044a0-W36-G2-group-*` (`groupgates.sh`, the guard meta-test excluded with `-LE trace_gate_meta` because it ran on its own above).  The trees are the wave 35g CLOSE trees,
rebuilt in place; the meta run used a fresh configure, `build/w36-meta-tree`.

| gate | result |
|---|---|
| core build (`build/w35g-close-fresh`, Release) | exit 0 |
| core `ctest -E plan_recalc -LE trace_gate_meta -j4` | 67/67 (`trace_gate_guards` 54 s, `trace_host_scripts` 26 s) |
| `MPP_NO_CORPUS=1 plan_recalc` | 1/1 |
| M2 recalc `--compare` | exit 0; 332/332, 268/268, 64/64 fields |
| `check_fixtures` | exit 0 |
| app tree ctest (`build/w35g-close-fresh-app`) | 34/34 |
| Qt tree ctest (`build/w35g-close-fresh-qt`) | 52/52 |
| `trace_gate_meta` (above) | passes, 75 minutes |

G2 changes C++ source in `core/mppwrite/` (`props_edit`, `assignment_growth`, `resource_growth`, `calculate`), `core/mpp/reader` (the Props digest) and `app/` (`edit_model`, `layout_model.h`, `reader_adapter.h`, `app_edits_calculate_tests`), none in `app/main.mm`, so the macOS-only CI jobs have nothing new to gate.
The engine stays 7/8 (`constraint-mso` expected-failing).
