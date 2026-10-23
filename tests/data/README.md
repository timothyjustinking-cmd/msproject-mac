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
| `wave24-timeline-hidden.mpp` | synthetic-base saved with the Timeline strip hidden (214/Props 0x03400008 empty); `synthetic-base.mpp` is its shown partner ("Time&line"). |
| `wave24-order.mpp` | FileNew project Alpha..Delta, then a task inserted above row 1 and saved; stored order differs from ID order (W24-5). |
| `wave24-flags20.mpp` | wave23-outline.mpp with Task.Flag1..Flag20 all True on ID 3 (wave 24 W24-15); the generic flag decode's fixture. |
| `wave24-flag2.mpp` | the same measurement's single-bit control: only Flag2 True on ID 3 (byte +24 bit 0x04). |
| `wave24-flag20.mpp` | only Flag20 True on ID 3 (byte +34 bit 0x04), pinning the non-contiguous id/bit position. |
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
hashes at the wave-23 close (branch `wave23`); regenerate the table in the
same commit as any deliberate change -- never edit a fixture to make a test
pass.  `README.md` cannot carry its own hash (it changes when this table does).

| file | SHA-256 |
|---|---|
| `project_a_golden.md` | `03BA7AB54910BB8A766E69326A0C61ED9F14C18DDD5295C1EF1326B570A01DEC` |
| `project_a_golden.tsv` | `A1B57D7E48E86B3A0536562521ED9962F34D3BDD2DE768FA86BEBD86830769B6` |
| `project_a_links_golden.tsv` | `8C38C4F09CEC09028DA61BBBFA24EA1FD47D29452CE77D3E5AD2E9828B60FAD6` |
| `project_a_preds_golden.sha256` | `E6F607073BAF551F2932330707D32A294085FFE2A4008139225597427C4D3CBB` |
| `project_a_recalc_golden.tsv` | `36F156F618C94C92D70FBA6D754340D374DDE72E7D2FF136D1E22DBEE5400DB8` |
| `reference/README.md` | `0357BFF58DDA4FF2FF59AB2CC788FF656588D28B5DD560B6547EB12AC150E1C0` |
| `reference/wave23-outline-project.measures.json` | `3CA8261885BD930F234149C8C65894B892F797A89FAE0DFB249FEA85271D60E0` |
| `scenarios/assignment-50pct.oracle.json` | `F569449EE16C92FB964DBE65760DC4623928D74F8E3601269292AB6B0A9D309A` |
| `scenarios/constraint-mso.oracle.json` | `075FE8D6FA1624453B3D70C61224E5147DC7D65C2ABDB4A3BEF36995DEF291B9` |
| `scenarios/constraint-snet.oracle.json` | `37F8963B5AE84CB426035FD893A13743224DC7B98F211906FDA3E62585651769` |
| `scenarios/delete-task-with-successor.oracle.json` | `DDD9B9BF829AA0A02E4D3A28EBE90A9751370EF12471D37425D7EF932359FA96` |
| `scenarios/duration-change.oracle.json` | `74644C7F4F7F838B5EDC0ACB1A7CB0A91F4F5EA72CBDF3A5ACE689AF468224BC` |
| `scenarios/link-fs-lag.oracle.json` | `54F7B89CD8C88238DE82F70B2D264F03D13934526D82E79B0E1972E79691E76D` |
| `scenarios/recalculate.oracle.json` | `43A9CE25C560530F4790206ECDDB1A1EB82702FD106CA7EEA2C8D09050025F2D` |
| `scenarios/task-calendar.oracle.json` | `0F53B1F1565E1A5D29B3C1875EB120423C526951A29849E8BC439D0127E043F9` |
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
| `wave24-flags20.mpp` | `1C74D403B7EB5280D57F1D6264D6A17B28A2F9578AF15F40282FDDC44CC471FF` |
| `wave24-flag2.mpp` | `614097D1031CA82B2F171170A5EB613349DA9BEEF6432CFB61900FBD91294BC6` |
| `wave24-flag20.mpp` | `1C1C9F2433962AFE309535074D92CAEAFA8052572B7AA7AFEC22541F7479F64F` |

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
