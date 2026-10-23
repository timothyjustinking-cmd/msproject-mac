# Wave-22 audit register (closed 2026-10-23, Tim + Fable)

Baseline: docs e252041, code abeafd2 (CI run 39912313201). Checkpoints live here (build/ is git-ignored); finals go to docs/handoff/.

DECISIONS (closed - do not reopen)
1. Fable runs both audits on this box as coordinator: corpus, COM oracle, desktop control of MS Project, git, one CI dispatch.
2. DeepSeek runs the fix session from the work order, as coordinator with full access, on deepseek-flash via the Claude CLI at https://api.deepseek.com/anthropic, plus a Python relay.
3. Tim stores the key himself (DEEPSEEK_API_KEY, user scope).
4. Setup: global npm Claude CLI; per-process launcher tools/deepseek/deepseek-claude.cmd; relay tools/deepseek/relay.py (urllib).
5. The work order IS the wave-23 brief (docs/handoff/wave23-brief.md): audit fixes first, then the wave-22 ranked open items.
6. No ask-first queue: DeepSeek fixes everything and reports after. Backstop: gates are hard stops, golden/fixture changes justified per commit, verifier pass on the fix diff.
7. DeepSeek's existing analysis (docs/analysis, spec, handoffs) is read and verified, not bypassed.
8. Run it all now, checkpointed per lane.
9. Target: 100% parity with installed MS Project 2021 (16.0.20430.20182 ProjectPro2021Retail). Every measurable difference is a finding.

ASSUMPTIONS CARRIED
1. Baseline e252041 (docs) / abeafd2 (code CI validated); drift checked.
2. Severity: Critical = corrupts/loses .mpp data on save or crashes on open; High = wrong schedule numbers or a gate passing without testing its claim; Medium = visible UI/behaviour difference from Project; Low = hygiene.
3. Spec "deliberate limits" stand: our icons/artwork, OS window frame, port identity.
4. macOS launch out of reach here; stays an admitted gap.
