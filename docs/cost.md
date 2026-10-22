# Cost calibration

The point of this file is to replace an estimate with a measurement. Every wave
report includes a session count; divide actual provider spend by it and the
projection stops being speculation.

## What a session is

One agent conversation from spawn to handoff. A restart after a crash or a
sandbox fault counts as a separate session, because it burns tokens again. A
continuation message to a sleeping agent (resume, follow-up) also counts.

Coordinator session: the long-running chat — counted separately, because its
context is much longer-lived and its per-turn cost is higher.

## Method

1. I record sessions per wave below (the honest count, including wasted ones).
2. At the end of a wave the coordinator reads spend-to-date from the provider
   dashboard and divides by the cumulative session count.
3. `remaining_cost ≈ (remaining_sessions × measured_$/session) + coordinator overhead`.

## Counter

| Wave | Date | Streams | Agent sessions | Notes |
|---|---|---|---|---|
| 0 | 2026-10-22 | scaffolding | 0 | coordinator only |
| 1 | 2026-10-22 | W1–W4 | **7** | 4 initial + 3 restarts forced by the sandbox failure (W1, W2, W4) |
| 2 | 2026-10-22 | W5–W8 | **4** | reader, scheduler, field keys, CI |
| 3 | 2026-10-22 | W9, W-view, W-io | **25** | writer, view model, MSPDI/CSV, the reader reference pass, field-key follow-ups and two successor handoffs; the split is reconstructed from the handoffs, not logged per spawn, so treat it as the balancing figure |
| 4 | 2026-10-22 | W10–W13 + oracle | **4** | reader breadth, MSPDI export, CPM, CI fixture; the oracle work is the coordinator's and costs no agent session |
| 5–6 | 2026-10-22 | W14–W22 | **11** | views/M2/reader fields/row writer, reconstructed from the 51-session state at the wave-7 start (the split was not logged per spawn) |
| 7 | 2026-10-22 | W23–W28 | **7** | six streams (sched, reader, calendar, writer, io, plan) + one W26 resume for the E1/E2 fixtures |
| — | — | coordinator | 3 (ongoing) | the predecessor's chats and this one |

**To date (end of wave 7): 58 agent sessions + 3 coordinator sessions.**
Measured spend at the wave-7 start was **~$3.90**, **~390M tokens** (51 agent
sessions); wave 7 adds 7 sessions at ~$0.076–0.08 → **~$4.45**, **~445M tokens**,
about **$0.077 per agent session**, still flat.

## Calibration

The measured $/session has been flat at **$0.07–0.08** for four waves, so the
cheap-tier projection is the one to plan on and the mid-tier row is retained only
as the pessimistic case.

| Version of the estimate | Sessions | At $0.076/session | Mid tier (3× headwind) |
|---|---|---|---|
| Full parity, from wave 8 onward | 70–100 | **$5–$8** | $15–$24 |
| Finish M2 (a writer date path Project honours) | 10–15 | **$0.8–$1.2** | $3–$5 |

## Known distortions

- Sessions that die from environment faults cost full price for no output; wave 1
  paid that three times.
- Long coordinator context is the most expensive token in the project. Splitting a
  wave into four short agents is cheaper than one long agent, even though it looks
  like more work.
- Oracle experiments cost no API tokens but occupy the coordinator, so they inflate
  wall-clock rather than spend. Wave 4 spent more coordinator time than agent
  money: six oracle runs, four agent streams, ~$0.30 of agent spend.
- Agents that hit a build error caused by another stream sharing the same
  build directory pay for a retry; wave 4 hit that once. Counted, not hidden.
