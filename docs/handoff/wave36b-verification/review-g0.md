# Independent review of wave 36b, group G0 (706078b, diff e1b2ee5..706078b)

Reviewer: read-only, 22 Nov 2026. The verify worktree was not touched; scripts and copies were in a temp folder.
Network addresses are redacted to the first 6 hex digits throughout.

## Verdict

No Critical or High finding. The six fixes do what they claim on the points I could check. One Medium (the host's
address is still recoverable from 19 notes, which the record does not say), the rest Low.

## Findings

### R36bG0-1 (Medium) the host's node is still printed in 19 notes under spec/ and docs/, in spaced-hex form; the record and the test message understate it
Evidence: scan of every tracked text file at 706078b for the SHA-256 digests stored at tools/trace/test_trace_gate.py
(after separators `space : , . - _` and `0x` are removed between hex digits, forward and byte-reversed;
scripts scan2.py, scan3.py). Digest A is the host's node (prefix 1cc10c; it matched the old literal), digest B the
VM's (prefix 00155d; it matches the two VM addresses the e1b2ee5 note printed, vmdigest.py).
- Digest B (the VM): no hit anywhere in the tip, in any form. W36b-0.3 is closed for the VM.
- Digest A (the host): hits in 19 notes, all GUID byte dumps (the node is the last 6 bytes of a version-1 GUID):
  docs/analysis/oracle/w12-oracle.md:45; spec/mpp-records.md:324; spec/writers/calendar-tbkdcal.md:38,45,46,98,99;
  spec/writers/evidence/{wave30-assign-add:81, wave30-assign-delete:81, wave30-baseline:67,85,
  wave30-insert-milestone:82,220, wave30-insert-summary:167, wave30-wbs:103,104, wave34-assign-add-second:79,
  wave34-assign-add-third:80, wave34-field-set:61,100,101, wave34-insert-task:82,216,
  wave35-g3-assign-remove-port-saved:81, wave35-g3-baseline-set-nested:65,80,
  wave35-g3-insert-milestone-first:85,215, -last:82,212, -nested:82,212, wave35-g3-insert-summary-nested:160}.md.
  Also contiguous or quoted in analysis/{com_validation,unnamed_inferred,unnamed_probe}.tsv, core/mppwrite/assignment_growth.cpp:694-695
  (the builder lists these four) and tests/data/README.md (spaced).
- The W36-C.4 check's comment calls a spaced hex dump "data and out of scope", so it passes. Its check message ("no note
  under spec/ or docs/ prints the host's or the VM's network address") and the record's "Found, not fixed" (which names
  only the .tsv and .cpp files) are therefore stronger than the tip. The brief's Done-when says "no network address in the
  tip"; R36W-3 itself called recorded bytes fine, so this is a scoping gap, not a regression.
Failure scenario: anyone with the public tip recovers the host MAC from any of those notes. History already holds it.
Fix: either say plainly in the record that spaced GUID dumps keep the host's node (evidence bytes), or redact the last
6 bytes of the node in the dumps; and add the spaced/byte-reversed form to address_hits if the dumps are redacted.

### R36bG0-2 (Low) the view date mask still blinds the gate in every pair whose control and null differ in a date word (about 11% of pairs)
tools/trace/trace_gate.py:752-760 (`_pair_date_differs`), :808-812. By the brief's design the word is masked on both
sides when control and null hold two different in-span dates there; the target's value is then not checked beyond "in
span". Measured with the gate's own functions (resid.py): 21 of 196 control/null pairs have a masked word
(wave27-unlink, wave28-* x13, wave29-* x5, wave30-task-mode-auto, wave35-g3-assign-remove-port-saved). A zoom writer
save recorded in such a session, with a wrong date at that same word, passes. None of the five zoom references is among
them (they agree), and the review's case FAILs. A wrong date in a different word of the same record still FAILs
(tested, "the pair differing at +N only"). Residual, not a defect of the fix; worth one line in the note
(trace-gate-fields-g0b.md already says "only in a pair whose control and null hold two different in-span dates").
Possible tightening if a zoom pair is ever recorded in such a session: require the target's word to equal one of the
pair's two values or to be in the span of the pair's own dates.

### R36bG0-3 (Low) the address check has holes besides R36bG0-1
test_trace_gate.py (address_hits): walks only .md and .txt (not the 60 .json, .tsv, .csv, .py under spec/ docs/); strips
only `:` and `-` (a dotted Cisco form `xxxx.xxxx.xxxx`, a space-separated form and a byte-reversed form are not found);
a 12-hex window of a 48-bit value hashes to a guard, not a secret (the builder says so). I scanned all tracked text
files for those forms: no VM hit, no further host hit beyond R36bG0-1. The "planted address" cases use a synthetic
address by its own digest, so the two real digests are only shown by the red run (the old note) and by my scan
(A and B each match the real node). Confirmed, not a defect.

### R36bG0-4 (Low) nothing pins the CI wiring that W36b-0.5 and 0.6 depend on
- `env: MPP_REQUIRE_OLEFILE: "1"` is at workflow level in ci.yml (parsed: `env` key present, all four jobs inherit it).
  No test reads ci.yml, so deleting that block silently restores the skip (test_require_olefile.py tests the scripts, not
  the YAML). app-macos.yml and qt.yml do not set it; they configure app/ and app_qt/, which register no trace tests, so
  that is correct today.
- Shard completeness: `--only named` runs all GUARDS and no candidates, `--only derived` the reverse, so the union is the
  full set (test_gate_guards_meta.py:~760-770). An unknown `--only` value runs everything twice, never less. The
  REDUNDANT end-of-run check is split correctly (an entry judged where it ran, a cover judged where it ran; the named
  shard judges named covers, the derived shard the derived entries). No test asserts the union; it holds by construction.
- Job names changed from `trace-gate-meta` to `trace-gate-meta-named` and `-derived`; a required-status-check setting
  naming the old job (not visible from the repo) would wait forever.

### R36bG0-5 (Low) CI limits are plausible, with thin margin on a 2-vCPU runner
Named 24.2 min and derived 41.0 min locally at 6 workers on 11 cores. ubuntu-latest has 4 vCPU on a public repo, so the
script runs 4 workers: about 36 and 62 min against ctest TIMEOUT 75 and 125 min and job limits 90 and 150. On a 2-vCPU
runner the same scaling gives about 73 and 123 min: at the ctest limit. No hosted-runner timing exists (the record says
so). ctest TIMEOUT 4500 and 7500 are set per test in CMakeLists.txt:~236; `--timeout 60` on the other jobs does not
override a test's own property.

### R36bG0-6 (Low) stale heading above the correction in spec/writers/props-records.md
Line 297 still reads "Supersedes the insert refusal above (W34-2.2) for two ids." The section's closing paragraph
(line 321) corrects it, but a reader of the heading line gets the old claim. Also wave36-complete.md keeps its old
rows by design; the correction's last bullet ("the record's other W36-2.2 claims ... hold") is asserted, not checked.

### R36bG0-7 (Info) 0.4's refusal now lives only in a function no save calls
R36W-4/R36G2-3 asked for a test of the already-present refusal in save_props_edit. That branch was deleted with the insert
path (every absent id is refused first), so the test covers props_insert_framing (note and baseline twice, another payload of
the same id, the second id beside the first). Adequate for the claim "framing refuses a record already in the stream"; it
does not test an id already in the typed-property section of a real stream (it uses a stream the framing itself extended).

## Item by item

1. W36b-0.1 (confirmed by reading): save_props_edit throws "inserting a record is not a measured save" for any id with
   count == 0 in state.records, before any change and before the output path is touched
   (props_edit.cpp:~564-585, inside the planning loop). 214/Props record edits are refused earlier. `git grep` finds no
   caller of props_insert_framing outside tests/roundtrip_props.cpp; no caller of the removed `records_inserted`,
   `appended_bytes` (PropsEditWrite) or `inserted`. The other save_props_edit callers (calculate.cpp, wbs_code.cpp,
   assignment_growth.cpp, resource_growth.cpp) edit measured ids only. The function is a public, non-static exported
   symbol (props_edit.h:248): not a save, but a future caller could replace_stream with it; the header says so.
   Records: wave36-complete.md gets an appended section only (diff is +14 lines at the end, nothing above edited); its
   "not lifted" statement matches coord-4e-g2.md (0 of 9, no baseline, no note, rule (c) fails). props_edit.h and
   START-HERE (7789 bytes, under 8 KB) say "framing decoded, not lifted" and do not claim "lifted". Not checked: the red
   run (no build allowed): the claimed 24 failures = 18 + 6 is arithmetic from the test code (9 pairs x 2 + 6 in the
   refusal list) and consistent with the record's own correction of the commit message.
2. W36b-0.2: `_pair_date_differs` is per record (uid and index), per word, same length on both sides, both values in the
   span, and different; `compare` passes (ctl, nul) raw; a missing stream, VarMeta or block, a non-chaining block or a
   shorter record list in the pair returns False (nothing masked, no raise). The review's case (+1176 and +1182 moved
   30 days in the zoom-custom save) FAILs in both directions (test cases added at test_trace_gate.py:~3877). I ran the full
   test_trace_gate.py from a copy at 706078b: 2009 PASS, 0 FAIL; and at e1b2ee5: all passed; both print
   "202 control-nulls, 180 PASS, 22 OPEN; negative: 191 edits, 181 FAIL, 10 PASS" (counts equal before and after; the
   22-pair OPEN list is pinned by the test; I did not diff the 10 PASS edit names). Under-masking: no genuine-noise FAIL in
   either sweep. Residual: R36bG0-2.
3. W36b-0.3: see R36bG0-1 and -3. The two stored digests are real (A = host node, B = VM node, both matched). The planted
   cases (alone, in a sentence, capitals, inside a longer run, colons, hyphens, table cell) are found; near misses are not.
   "00155d" appears once in the tip, as a bare OUI in a pattern in review-wave.md:35, not an address.
4. W36b-0.4/0.5/0.6: 0.5 by reading: both scripts exit 1 with a FAIL line when MPP_REQUIRE_OLEFILE is not "" or "0"; the
   test hides olefile with a PYTHONPATH shim (first on sys.path, so it wins over site-packages) and checks 1 / 77 in four
   settings, so removing the switch in either script fails it. The two SKIP_RETURN_CODE tests are the only ones in the tree
   (`git grep`). 0.6: CMake registers trace_gate_meta_named and _derived with labels (trace_gate_meta;trace_gate_meta_X),
   so `-LE trace_gate_meta` excludes both and `-L trace_gate_meta_X` selects one. Not run: the 0.4 and 0.6 red runs, the
   65-minute shard runs, any CI. ci.yml parses as YAML; job names and timeout-minutes are 90 and 150.
5. Personal data: the diff adds no user-profile path, no user name, no client name, no e-mail. Author metadata is the
   existing no-reply address. The only "Users" hit is a generic word in the R36G0f-8 text.

## Builder claims
Confirmed: item list and ids; insert refusal on the save path and framing unreachable from saves; correction text and
START-HERE wording; the CV_iew gate change, the review case and the sweep counts (before and after, by running both);
the VM's address is gone from the tip; both digests are real; MPP_REQUIRE_OLEFILE reaches all four ci.yml jobs; shard
union by construction; limits are about 3x local time.
Could not check: every "seen red" run (no build or mutant runs), the full meta shards, the host-digest "shown once"
log, any hosted-runner timing, the 0.1 commit-message count beyond arithmetic, and the claim that no pair moved in the
sweeps beyond equal counts and a pinned OPEN list.
