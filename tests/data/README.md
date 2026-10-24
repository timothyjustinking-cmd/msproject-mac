# tests/data — committed test data

Everything in this directory is committed and must stay reproducible from a clean
checkout. Nothing here is client data.

| file | what it is |
|---|---|
| `synthetic-base.mpp` | **synthetic** MPP14 fixture, 237,568 bytes, 3 tasks + project row. Only synthetic `.mpp` files may be committed. |
| `project_a_golden.tsv` | MS Project's own COM export of the PROJECT_A corpus file (83 live tasks), used as the oracle. See `project_a_golden.md`. |
| `project_a_golden.md` | how the golden TSV was produced and what its columns mean |
| `wave23-outline.mpp` | **synthetic** MPP14 fixture built by Project 2021 for wave 23: 3-level outline, FS+2 d / SS+1 d / FF links, one resource. See its provenance below. |
| `wave23-flags.mpp` | the same fixture after Project set Task.Flag10 (0x0b400051) True on ID 3; the measured byte for the SUMMARY column (W23-6). SHA256 `6B74A9DA…F1B80958`. |
| `wave24-summary-off.mpp` | synthetic-base saved by Project 2021 with the project-summary row OFF (214/Props 0x03400009 = 0); wave 24 W24-2. |
| `wave24-summary-on.mpp` | the same file with the row ON (0x03400009 = 1); the only difference from the off save is that record and per-save handles. |
| `wave24-timeline-hidden.mpp` | synthetic-base saved with the Timeline strip hidden (214/Props 0x03400008 empty). |
| `wave24-timeline-shown.mpp` | the SAME session's shown save of the same project (0x03400008 = "Time&line"); the pair is what reproduces the "only the Timeline setting differs semantically" claim (W25-10).  `synthetic-base.mpp` is a different session and cannot be the partner. |
| `wave24-order.mpp` | FileNew project Alpha..Delta, then a task inserted above row 1 and saved; stored order differs from ID order (W24-5). |
| `wave24-flags20.mpp` | wave23-outline.mpp with Task.Flag1..Flag20 all True on ID 3 (wave 24 W24-15); the generic flag decode's fixture. |
| `wave24-flag2.mpp` | the same measurement's single-bit control: only Flag2 True on ID 3 (byte +24 bit 0x04). |
| `wave24-flag20.mpp` | only Flag20 True on ID 3 (byte +34 bit 0x04), pinning the non-contiguous id/bit position. |
| `wave25-auto.mpp` | **synthetic** `wave23-outline.mpp` with every task switched to automatic scheduling (`Manual = False`) and saved by Project 2021 with neutral metadata; the parity reference that distinguishes an auto task bar from a manual one (W25-2). |
| `wave25-flag11.mpp` … `wave25-flag17.mpp` | seven single-bit controls, one per flag in the +33 group: wave23-outline.mpp with every flag False on ID 3, then ONLY that flag True, saved through Project 2021 by the committed `scripts/measure_flags.py` (W25-9).  They pin the bit order the all-20 fixture cannot. |
| `wave27-percent-actuals.mpp` | **synthetic** synthetic-base with Task ID 1 set to 50 % PercentComplete by Project 2021, saved with neutral metadata by `scripts/oracle_w27.py`.  Project's own % edit writes the actuals, dates, progress flag, project-row roll-up and Var2Data field 0x0B40015E as well as FixedData +92 (W27-0.1 evidence); also the project-with-actuals fixture the Move Project gate opens. |
| `synthetic-calendars.mpp` | synthetic two-calendar fixture (wave 19, `scripts/build_calendar_fixture.py`) |

## `wave23-outline.mpp` — provenance (wave 23)

- **Built by the coordinator through COM automation of MS Project Professional
  2021 (16.0.20430.20182)** on **2026-10-23**, under `scripts/oracle_lock.py`,
  by `scripts/oracle_w23.py`. The file was written to `build/w23/` and copied
  here only after it loaded and the reader showed the intended shape.
- Six synthetic tasks: `Summary A` (level 1), `Sub A1` (2), `Leaf A1a` and
  `Leaf A1b` (3), `Sub A2` (2), `Final Task` (1); links `Leaf A1a -> Leaf A1b`
  FS+2 days, `Leaf A1a -> Sub A2` SS+1 day, `Leaf A1b -> Final Task` FF; resource
  `Resource A` assigned to `Final Task`. No client content.
- **SHA256 055721FF07E93F8C8C06B8428E9EF3495D10A38BE8C432506FCB5FF3A786DA20**,
  pinned in `scripts/check_fixtures.py`. It is the CI fixture for wave 23's
  ID-order/outline gate (`qt_task_order`), the Depends On/Who gate
  (`qt_stored_table`) and the parity reference work.
- Regenerate with `python scripts/oracle_w23.py` (Project must be closed); the
  script writes to `build/w23/`, so copy the file here and update the hash only
  after a deliberate review of the reader dump.

## Every tracked file and its SHA-256 (wave 23, A-8/A-7d)

`scripts/check_fixtures.py` pins the six files whose content is quoted in
docs or used as an oracle reference; this table is the complete inventory so a
silent substitution is visible even for the small binaries.  These are the
hashes updated at the wave-25 close (branch `wave25`; the wave-23 originals for
the files wave 25 did not touch); regenerate the table in the
same commit as any deliberate change -- never edit a fixture to make a test
pass.  `README.md` cannot carry its own hash (it changes when this table does).

| file | SHA-256 |
|---|---|
| `project_a_golden.md` | `03BA7AB54910BB8A766E69326A0C61ED9F14C18DDD5295C1EF1326B570A01DEC` |
| `project_a_golden.tsv` | `A1B57D7E48E86B3A0536562521ED9962F34D3BDD2DE768FA86BEBD86830769B6` |
| `project_a_links_golden.tsv` | `8C38C4F09CEC09028DA61BBBFA24EA1FD47D29452CE77D3E5AD2E9828B60FAD6` |
| `project_a_preds_golden.sha256` | `BF9A9E184830AD66E2C510F4EF91DC5CBA213EAD1024A2C326CA1F8827F7D820` |
| `project_a_recalc_golden.tsv` | `36F156F618C94C92D70FBA6D754340D374DDE72E7D2FF136D1E22DBEE5400DB8` |
| `reference/README.md` | `65364F2B553D0D80CAC7459409FA88E04E01B21CF1CF3552C69C350ECFC51ED4` |
| `reference/wave23-outline-project.measures.json` | `3CA8261885BD930F234149C8C65894B892F797A89FAE0DFB249FEA85271D60E0` |
| `scenarios/assignment-50pct.oracle.json` | `84AD1FD9897AC4C0E750B4C47A95E7654980D7BE97340DA15C038FE9DDFF6607` |
| `scenarios/constraint-mso.oracle.json` | `1EE253E931F90A8C27694A46D24C518E090397BFD1FAAAB7A2DB13F8B39DBBE0` |
| `scenarios/constraint-snet.oracle.json` | `0DD549FB8A60D0A873B022BEF46C15A46F5156C1DE4F87AF7B2D61295486A85D` |
| `scenarios/delete-task-with-successor.oracle.json` | `55E910F499DF66B9E729CA17F1318DB1D117D97E87B8FF6AA4E47547B16119C6` |
| `scenarios/duration-change.oracle.json` | `93588E74E2C970227CCE7D25AD2F4FC7D3C42408791EBBD43786B5DCA0C9C5EA` |
| `scenarios/link-fs-lag.oracle.json` | `95F837781F314AC657F2045D810BFFA1BFED18DF54BC0B5FCB2F205FED76C9DC` |
| `scenarios/recalculate.oracle.json` | `457CE73B3895BA1A1FDF9A2F4F163EE9D6C504BAD6485D6E158DE69FA2384F9B` |
| `scenarios/task-calendar.oracle.json` | `855A7EED5E0A2F5C1DDFA9B8195216462B75017F8EDB45CB3987F901A90D58BE` |
| `synthetic-base.mpp` | `55C109F2F62FD6B9F05789AB002C35991A424AAB18C78F92E4200DF7B3FC6069` |
| `synthetic-calendars.mpp` | `F486F0C3235BD8A592542E942141FE873C4262E6AFA19E029A9C4C79386017CA` |
| `synthetic-props.bin` | `8739CDB83D96210ED3874AD14055F02C88B97F9C5FF74C343AD04D548B8E0334` |
| `synthetic-viewstate-ctable-base.bin` | `80573060BBACC24B656F9A6FF70BF907A421BEE0677C2F99A8F7D3AB5993F4B9` |
| `synthetic-viewstate-ctable-col.bin` | `7B753DFE1C930CF95C6C2E3D75A821505A2B6F43C35FB42C30979D824E5FF52E` |
| `synthetic-viewstate-ctable-date.bin` | `E106E86452C37F4EA73263F043AE5EF1280BEEF46999C62271933960705EB3CB` |
| `wave23-outline.mpp` | `055721FF07E93F8C8C06B8428E9EF3495D10A38BE8C432506FCB5FF3A786DA20` |
| `wave23-flags.mpp` | `6B74A9DAEA67F0759A6A465BAD63D20556C8A06EFFFEDCE3EBF71CF4F1B80958` |
| `wave24-order.mpp` | `515537D2B86415F85A3D15FB320C3F5A0EBB7A0951FF43CBCD00DC6B2BF6DDAF` |
| `wave24-summary-off.mpp` | `0EF4DE69CC9C2CF6D3C6568C86B9A8B708DE6E8CCC8D404BC4FBE82AE4A6C391` |
| `wave24-summary-on.mpp` | `90BF1055E1FF415E453208C04123C2A2F869E38D950237DF116D59D2E53C7C05` |
| `wave24-timeline-hidden.mpp` | `9BE6D2E9343149F58DE7800F3F840B158AC90C4F0403423BF8167396EC2E61E4` |
| `wave24-timeline-shown.mpp` | `829AE5E8DBAF6C33FF4043B5F210CCAB78730E18866BE491186BA2B3EE288EC5` |
| `wave24-flags20.mpp` | `1C74D403B7EB5280D57F1D6264D6A17B28A2F9578AF15F40282FDDC44CC471FF` |
| `wave24-flag2.mpp` | `614097D1031CA82B2F171170A5EB613349DA9BEEF6432CFB61900FBD91294BC6` |
| `wave24-flag20.mpp` | `1C1C9F2433962AFE309535074D92CAEAFA8052572B7AA7AFEC22541F7479F64F` |
| `wave25-auto.mpp` | `BD8503EE4BDF7E117491E3C245D1A84E64ECF91F51B1AA0625AE4F064E96ED16` |
| `reference/wave25-auto.measures.json` | `054CDBFAA0048F8FAECC20FD73A431E0CB0F57F919B27585351270CA0F8E7637` |
| `wave25-flag11.mpp` | `A420D4C1639DCB8E1C16F6606417E7DBD48A0A95D444C64CF8889C5092E06965` |
| `wave25-flag12.mpp` | `AD0C9F7A588F6A89945A0A3EBAD3B53D7DA1B7C522A1157B44A928FBA9151987` |
| `wave25-flag13.mpp` | `023D40E4E568C244C3F3319FC64605BBFF52C31F75794FAFD1CDCDA4615DF521` |
| `wave25-flag14.mpp` | `736090E534CC69AF08EB467D672B985E901C2B0F96A1C4720555D020862F22D4` |
| `wave25-flag15.mpp` | `9873616512655C002AE9D0FAEC172D46E7D85216F4D259323616E6D82DBD36AB` |
| `wave25-flag16.mpp` | `0D1F5FF13FB55F98D3AAE8647FB7FAA725C5B85E561B92B99A3D78E5065A92AE` |
| `wave25-flag17.mpp` | `B7F9EDF6A35D61E6B17FEBFFC8FB6378325CD29E9F01FAB9D8002A23330AC0EB` |
| `wave27-percent-actuals.mpp` | `4C9AECBF544377878E058CC0103BEF059B65C48F0C21B079E3AF4EE215CC7AA2` |

## `synthetic-base.mpp` — provenance

- **Generated by the coordinator via COM automation of MS Project** on
  **2026-10-22**, under `scripts/oracle_lock.py`, writing only to
  `corpus/probe/`. It is a fresh `FileNew()` project: project row titled `base`
  plus three tasks (`Task A` 5d, `Task B` 3d, `Task C` 2d, all starting
  2026-01-05 08:00, FS chain). No resources, no notes, no client content.
- **Not client data.** `corpus/` is git-ignored because it holds a client's real
  construction programme; `corpus/probe/` is not — those files were produced by
  driving MS Project, and this one is a synthetic minimum.
- **Copied byte-for-byte** from `corpus/probe/base.mpp`, which is the smallest
  probe fixture with more than one task. Verified at copy time:
  `SHA256 55C109F2F62FD6B9F05789AB002C35991A424AAB18C78F92E4200DF7B3FC6069`,
  237,568 bytes, source and destination hashes equal.
- **Do not replace this file with a client file**, and do not "refresh" it from
  `corpus/project_a.mpp` or `corpus/project_b.mpp`. The client
  corpus is read-only and must never enter a commit.
- If the fixture genuinely has to change (a new synthetic probe, a new field),
  regenerate it with MS Project through `oracle_lock.py`, write it to
  `corpus/probe/`, copy it here, update the hash above and the expected values in
  `tests/mpp_reader_tests.cpp` (`test_synthetic_fixture`).

## Why it is committed

`mpp_reader` used to be excluded from CI because on a clean checkout `corpus/`
does not exist, so every `.mpp` check printed `SKIP:`. With this fixture the
reader test builds and runs on all three runners; the PROJECT_A/PROJECT_B/corpus-probe
checks still skip when `corpus/` is absent, as they must.

The fixture is not allowed to go missing quietly: if `project_a_golden.tsv` is present
in this directory but `synthetic-base.mpp` is not, the test **fails** (it does not
skip), so a forgotten `git add` or a too-broad ignore rule cannot turn CI green
with the reader unexercised.

## Proving the fallback on a machine that has `corpus/`

The reader test never needs the working directory: it looks for the fixture
beside the committed golden TSV, whose absolute path `core/mpp/CMakeLists.txt`
already configures into the test (`MPP_GOLDEN_PATH`). `tests/test_paths.h.in`
also carries `TEST_DATA_DIR` for any target that wires the configured header in;
when it is defined, the test uses it instead. To exercise the no-corpus path
without touching the client files, set `MPP_NO_CORPUS` to any non-empty value:

```bat
REM Windows, from the repo root, after configuring build\vs
set MPP_NO_CORPUS=1
ctest --test-dir build\vs -C Release -R "^mpp_reader$" --output-on-failure
set MPP_NO_CORPUS=
```

```sh
# macOS / Linux
MPP_NO_CORPUS=1 ctest --test-dir build -R '^mpp_reader$' --output-on-failure
```

The run must pass, print `SYNTHETIC: 3 tasks`, and print `SKIP:` lines for PROJECT_A,
PROJECT_B and the `corpus/probe` sweep. CI runs that exact invocation as a second,
focused step, so the fallback cannot rot even if a runner ever gains a `corpus/`.
