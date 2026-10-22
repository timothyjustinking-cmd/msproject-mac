# Wave 9/9b complete — the successor's gate

**This file is the wait gate.** Its existence means the wave-9 coordinator
finished: the tree is quiet, every stream is merged, and a successor may work.

* **Final commit:** `2159a6a` (pushed). Wave-9 commits: `6e0dadf` W36,
  `2d5d1cc` W37, `9141946` W39, `3baeae4` W38, `80a78d9` oracle tooling,
  `7739c42` W40, `e2b85d8` resource-type fixture, `08242c9` W42, `b134e47` W41,
  `40f1cca` + `2159a6a` the two CI fixes (comment `/*`, missing initializer).
* **CI:** `ci` and `app-macos` both **green** on `2159a6a` — all three legs
  (`windows-msvc`, `macos-appleclang`, `ubuntu-gcc`) plus the AppKit shell.
* **Full local suite:** `build\vs-w9` (clean MSVC `/W4 /WX`) — **31/31 ctest
  tests passed**, 34 s. The standalone app project: `build\vs-w9-app` **3/3**
  (`app_layout` 253, `app_selection` 90, `app_document` 43).
* **M2:** `build\vs-w9\tools\recalc\Release\recalc.exe corpus\project_a.mpp
  --compare corpus\probe\project_a-recalc.mpp` -> **332/332 fields match**, 268/268,
  64/64, **0 mismatches, attribution "unexplained: none"**. Golden
  `tests/data/project_a_recalc_golden.tsv` unchanged (`DB81218E…46A0A`, 83 uids).
* **schedin (ALAP):** 20/20 dates, 10/10 slack.
* **Writer:** a date edit written by `mppwrite::save_date_edits` reopens in
  Project on the pinned date (SNET + manual-mode bit cleared) —
  `corpus/probe/w37-snet-auto.mpp`, FNV `0xc917b3380e0d660c`.
* **Counters:** 73 agent sessions + 5 coordinator sessions, ~$5.60, ~565M
  tokens, ~$0.077/session.

## What wave 9 did not do (carry into wave 10)

1. Row add/delete in the writer — the hard one; needs the add/delete stream diff
   oracle first (`WAVES.md` "Wave 10 — planned", oracle agenda item 1).
2. Custom fields and formulas; resource and usage views; app v3 (menus,
   preferences, Export PDF, filter dialog).
3. Recurring-exception parameter words (five `w9-r-*` fixtures specified),
   elapsed-DURATION format word (`core/mpp` PjFormatUnit), resource Type byte
   (fixture `corpus/probe/w9-resourcetypes.mpp` is ready and unwasted).
4. Exact-date writes on a **linked** row (inference: `max(pred finish, pin)`);
   fixture `w37-snet-link.mpp` not built.

## Two CI traps this wave paid for

MSVC does not warn about either, so a green local build proves nothing here:

* **`/*` inside a block comment** — appleclang/gcc `-Wcomment -Werror`.
* **Missing member in a braced initializer** — `-Wmissing-field-initializers`
  on appleclang/gcc. Value-initialise (`T x{}; x.field = ...`) or list every
  member.

If a push is red, `gh run view <id> --log-failed` names the file and line in one
shot; fix, push, re-check, and only then update any document that claims green.
