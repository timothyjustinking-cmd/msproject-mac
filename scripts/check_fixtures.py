#!/usr/bin/env python3
"""Verify the committed test fixtures are the ones HEAD says they are.

The Dropbox folder lies about mtimes and has twice resurrected a Project-saved
`synthetic-base.mpp` over the committed one (same path, different bytes, mtime
from hours earlier).  `git status` can miss it when the stat cache matches, so
the only reliable check is `git hash-object` vs `HEAD`, plus the documented
SHA256 for the one fixture whose content is quoted in docs.

    python scripts/check_fixtures.py          # exit 1 on any mismatch
"""

import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# W33-4.4: the marker list lives in scripts/check_markers.py (the CI guard).
# This script's property-set scan uses the same list, so the two halves cannot
# drift; the import works under `python -I` because the directory is added
# explicitly rather than relying on the script-directory path entry.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_markers  # noqa: E402

# Fixtures whose exact bytes are documented (SHA256, from docs/handoff/wave14-complete.md).
DOCUMENTED = {
    # Wave 33 (W33-4.3 / audit B-2): the operator's name was still stored in
    # the SummaryInformation Author and LastAuthor values (VT_LPSTR) and one
    # 214/Var2Data value (VT_LPWSTR, key 0x3540000b).  Each 8-character string
    # was replaced IN PLACE with "W33 Base" (7 differing bytes per copy, 21 in
    # all; size and every offset unchanged), so the file no longer carries the
    # name; the scheduling payload is untouched.  The hash below is the new
    # one; tests/data/README.md's "as copied" record keeps the old hash.
    "tests/data/synthetic-base.mpp":
        "6C120FBCC3D9DD8C5B4064C8555B10BA8E197DDE35CDF7C78F2D099622C3777C",
    # Wave 19: synthetic-base's tasks converted to automatic scheduling plus a
    # second named base calendar ("Shift A", Mon-Fri 09:00-13:00 + 14:00-18:00).
    # Built by scripts/build_calendar_fixture.py; pinned in engine_diff.py too.
    # Wave 28 (W28-0.1): the two UTF-16LE save-path strings (Props14 and the
    # 214/CEdl/Var2Data entry) named the operator and the company Dropbox
    # folder.  Because this file is the engine suite's calendar base and its
    # recorded oracle depends on its bytes, the brief allowed them to be
    # replaced IN PLACE with a neutral same-length string (C:\w19\... padded;
    # scripts/w28_neutralise_path.py).  The operator's name, still stored in
    # the SummaryInformation properties and one Var2Data value of the same
    # blob, was then replaced the same way (scripts/w28_neutralise_operator.py)
    # because the pre-push guard refuses any new blob that carries it.  Every
    # other byte is unchanged; only the path and name text differs, hence the
    # new hash.
    "tests/data/synthetic-calendars.mpp":
        "BAF83A50C19A82F834CD7CCBF833E53C2DB9130C9627F2EAB6EBE44F3C5D68AE",
    "tests/data/project_a_recalc_golden.tsv":
        "36F156F618C94C92D70FBA6D754340D374DDE72E7D2FF136D1E22DBEE5400DB8",
    # Wave 23 (W23-1/W23-7/W23-8): the 3-level outline fixture built through
    # Project 2021 by scripts/oracle_w23.py -- six synthetic tasks, an FS+2 d
    # link, an SS+1 d link, an FF link and "Resource A" assigned to the last
    # task.  No client content.  Rewritten 2026-10-27 (history rewrite before the
    # wave-32 merge): the author name in SummaryInformation and one UTF-16 copy
    # were replaced in place by a same-length neutral string; every offset and
    # length word is unchanged.
    "tests/data/wave23-outline.mpp":
        "36419EF271E2FBDB9DC3BC325FFE44552ABBB0D01A51891FBBFD1F4DA742462D",
    # Wave 23 (A-1d): the MEASUREMENTS of Project 2021's own capture of that
    # fixture.  The screenshot itself stays local (operator ruling 2026-10-23);
    # only the numbers are committed, and this is the CI parity reference.
    "tests/data/reference/wave23-outline-project.measures.json":
        "3CA8261885BD930F234149C8C65894B892F797A89FAE0DFB249FEA85271D60E0",
    # Wave 23 (W23-6): wave23-outline.mpp opened by Project 2021 on a copy,
    # Task.Flag10 set True on ID 3 and saved (the control save first).  The
    # reader's Flag10 decode is checked against this file.
    # Wave 28 (W28-0.1): re-derived by scripts/oracle_w28_privacy.py under
    # C:/w28 because the wave-23 save embedded the operator's local save path.
    # Same Project save of the same base: Task FixedData is byte-identical to
    # wave23-outline.mpp and Task FixedMeta differs only at entry byte +25 bit
    # 0x04 (offset 323, 00 -> 04).
    "tests/data/wave23-flags.mpp":
        "72ACC2086FA9A97781F96EBBD243D4C2CF91E7902BEE9FE19D8139303E3E802A",
    # Wave 24 (W24-2): Project 2021's own save of synthetic-base with Format >
    # Project Summary Task OFF and ON, built by scripts/oracle_w24.py under a
    # neutral path with neutral Author/Manager/Company/UserName.  The reader
    # decodes 214/Props 0x03400009 (0/1) from them; both fixtures store the
    # Timeline shown (0x03400008 = "Time&line").
    "tests/data/wave24-summary-off.mpp":
        "0EF4DE69CC9C2CF6D3C6568C86B9A8B708DE6E8CCC8D404BC4FBE82AE4A6C391",
    "tests/data/wave24-summary-on.mpp":
        "90BF1055E1FF415E453208C04123C2A2F869E38D950237DF116D59D2E53C7C05",
    # Wave 24 (W24-3): the same synthetic project saved with the Timeline strip
    # hidden in Project (214/Props 0x03400008 = empty).  Wave 25 (W25-10/S12)
    # commits its SHOWN partner from the SAME session (the record is
    # "Time&line"), so the pair reproduces from the repo; synthetic-base is a
    # different session and cannot.
    "tests/data/wave24-timeline-hidden.mpp":
        "9BE6D2E9343149F58DE7800F3F840B158AC90C4F0403423BF8167396EC2E61E4",
    "tests/data/wave24-timeline-shown.mpp":
        "829AE5E8DBAF6C33FF4043B5F210CCAB78730E18866BE491186BA2B3EE288EC5",
    # Wave 24 (W24-5): a FileNew project with four tasks, then a task inserted
    # above row 1 and saved.  Project appends the new FixedData row but displays
    # it as ID 2, so the stored order (Alpha, Bravo, Charlie, Delta, Inserted
    # Row) differs from ID order -- deleting the adapter's ID sort now fails CI.
    "tests/data/wave24-order.mpp":
        "515537D2B86415F85A3D15FB320C3F5A0EBB7A0951FF43CBCD00DC6B2BF6DDAF",
    # Wave 24 (W24-15): wave23-outline.mpp with Task.Flag1..Flag20 all True on
    # ID 3, built by the one-flag-at-a-time measurement (measure_flags.py).
    # The reader's generic flag table is checked against this file.
    "tests/data/wave24-flags20.mpp":
        "1C74D403B7EB5280D57F1D6264D6A17B28A2F9578AF15F40282FDDC44CC471FF",
    # Wave 24 (W24-15 review): single-bit controls from the same one-flag-at-a-
    # time measurement -- Flag2 (+24/0x04) and Flag20 (+34/0x04).  They pin the
    # NON-CONTIGUOUS positions object-by-object, not just the all-20 aggregate.
    "tests/data/wave24-flag2.mpp":
        "614097D1031CA82B2F171170A5EB613349DA9BEEF6432CFB61900FBD91294BC6",
    "tests/data/wave24-flag20.mpp":
        "1C1C9F2433962AFE309535074D92CAEAFA8052572B7AA7AFEC22541F7479F64F",
    # Wave 25 (W25-2/S2): wave23-outline.mpp opened as a COPY, EVERY task
    # switched to automatic scheduling (Manual = False) and saved by Project
    # 2021 with neutral metadata.  Same table/geometry as the wave23 reference,
    # so the controlled difference is the bar colour: (138,187,237) auto, where
    # wave23-outline's all-manual capture measured (116,204,212).  The reference
    # that makes wave 24's manual-vs-auto mislabel fail the parity gate.
    "tests/data/wave25-auto.mpp":
        "BD8503EE4BDF7E117491E3C245D1A84E64ECF91F51B1AA0625AE4F064E96ED16",
    "tests/data/reference/wave25-auto.measures.json":
        "054CDBFAA0048F8FAECC20FD73A431E0CB0F57F919B27585351270CA0F8E7637",
    # Wave 25 (W25-9/S11): ONE single-bit control per flag in the +33 group
    # (Flag11..Flag17).  Built by the committed scripts/measure_flags.py from a
    # copy of wave23-outline.mpp: every flag False on ID 3, then only that flag
    # True, saved through Project 2021.  They pin the bit ORDER inside the
    # group, which the all-20 aggregate cannot.
    "tests/data/wave25-flag11.mpp":
        "A420D4C1639DCB8E1C16F6606417E7DBD48A0A95D444C64CF8889C5092E06965",
    "tests/data/wave25-flag12.mpp":
        "AD0C9F7A588F6A89945A0A3EBAD3B53D7DA1B7C522A1157B44A928FBA9151987",
    "tests/data/wave25-flag13.mpp":
        "023D40E4E568C244C3F3319FC64605BBFF52C31F75794FAFD1CDCDA4615DF521",
    "tests/data/wave25-flag14.mpp":
        "736090E534CC69AF08EB467D672B985E901C2B0F96A1C4720555D020862F22D4",
    "tests/data/wave25-flag15.mpp":
        "9873616512655C002AE9D0FAEC172D46E7D85216F4D259323616E6D82DBD36AB",
    "tests/data/wave25-flag16.mpp":
        "0D1F5FF13FB55F98D3AAE8647FB7FAA725C5B85E561B92B99A3D78E5065A92AE",
    "tests/data/wave25-flag17.mpp":
        "B7F9EDF6A35D61E6B17FEBFFC8FB6378325CD29E9F01FAB9D8002A23330AC0EB",
    # Wave 27 (W27-0.1): synthetic-base opened on a copy and Task ID 1 set to
    # 50 % PercentComplete THROUGH PROJECT 2021, saved with neutral metadata.
    # The committed evidence that Project's own % edit writes actual duration
    # (+80), remaining duration (+88), actual start (+120), two dates
    # (+132/+136), a progress flag (+168), the project row's roll-up
    # (+80/+88/+92) and Var2Data field 0x0B40015E -- not the single +92 word the
    # port used to write.  It is also the project-with-actuals fixture the Move
    # Project prompt gate opens now that the % buttons are disabled.
    # Wave 28 (W28-0.1): re-derived by scripts/oracle_w28_privacy.py under
    # C:/w28; the wave-27 save embedded the operator's local save path.
    "tests/data/wave27-percent-actuals.mpp":
        "56198DEA93A518B32E8813858855368C33C472B191D49AAD5E23E27CE8196C2C",
    # Wave 27 (W27-0.3): the SAME Project 2021 session's save of synthetic-base
    # before and after Project Summary Task > Priority was set to 750, with
    # neutral metadata.  The pair's TBkndTask/FixedData differs in exactly the
    # project row's +78 word (500 = 0x01F4 -> 750 = 0x02EE); every other stream
    # difference is per-save bookkeeping (the wave-24 same-session pair shows
    # the same GUID/edit-list churn).  The 750 file is the fixture the port's
    # saved FixedData stream is compared with byte for byte.
    # Wave 28 (W28-0.1): re-derived in one session by
    # scripts/oracle_w28_privacy.py under C:/w28; the wave-27 saves embedded
    # the operator's local save path.  The pair still differs only at +78/+79.
    "tests/data/wave27-priority-control.mpp":
        "5ACA3BBA6E4B49CF6E91F8E582F1E1DCF45E23CFC0C424C3632E10A2F2C72152",
    "tests/data/wave27-priority-750.mpp":
        "C5A6F06E39665323323BE796EA484FDBD1C0ED2CF9261F4F104565DA48FB6252",
    # Wave 28 (W28-1.1): four same-session Project 2021 save pairs for
    # PercentComplete, built by scripts/oracle_w28_actuals.py on copies under
    # C:/w28 with neutral Author/Manager/Company/UserName/Title.  The first
    # three pairs are one continuous session on a synthetic-base copy whose
    # Task ID 1 was switched to Manual=False first: 0 -> 40, 40 -> 100 and
    # 100 -> 0.  The fourth pair is a fresh copy of the fixture's own manual
    # row, 0 -> 40.  The edited files are the field-by-field oracles the
    # port's actuals writer is compared with (W28-1.2).
    "tests/data/wave28-percent-auto0-control.mpp":
        "69BC2E31A7663479109F6137E92EFAA99426C576224EEF78373B18112915E974",
    "tests/data/wave28-percent-auto40.mpp":
        "079ECF4A2B402FBF819A4DE28EFF6F2A334F850369724D2FC778C52C72DF3024",
    "tests/data/wave28-percent-auto40-control.mpp":
        "174CC3E3E56A793215BC9D5FD6928A199FEB2C20523C8C1D828AA4D0DBE09EF4",
    "tests/data/wave28-percent-auto100.mpp":
        "B72297F7DE3E320008C34FC5E3755A23987B734773565CCD08982CD2469E211A",
    "tests/data/wave28-percent-auto100-control.mpp":
        "E37F8ECD42FC56C91FD341DAA7BC20CB54CF71F971EEDC5A12E291B9F828F10F",
    "tests/data/wave28-percent-auto0.mpp":
        "0978235B44C99C82F8943DC74DFF86D39B1C5F9FE7E88427E63B156ED8F9C93E",
    "tests/data/wave28-percent-manual0-control.mpp":
        "17BA9E3FEC598ABB736FB6723E50A331E29FBEA447FB21EDD1F05493743774A9",
    "tests/data/wave28-percent-manual40.mpp":
        "4EF4CB7FD7B115E7FDB2F9F2CC02EC026FBD73E3E28F78725E84B1F0230438C7",
    # Wave 28 (W28-1.3): one same-session Project 2021 save pair per update
    # command, built by scripts/oracle_w28_update.py under C:/w28 with neutral
    # metadata.  The Mark on Track pairs carry status date 2026-01-07 00:00 in
    # the control and change only the selected row (auto row 1 -> 40 %, manual
    # row 2 -> 83 %).  The Update Project pair stores no status date in the
    # control; UpdateProject(All, 2026-01-07, pj0to100Percent) moves all three
    # rows (50/83/100 %) and stores the through date as the status date, so the
    # port's Update Project save owns that 4-byte 114/Props record too.
    "tests/data/wave28-mark-auto-control.mpp":
        "13AD22E4E337FBBBA0C25A5A9A8883C1D6A25A05E6A1103E2B6B962B28684A83",
    "tests/data/wave28-mark-auto.mpp":
        "9ABC9FB58E36D2A9DBE6597496E6AD9E94463E7BDB1601C87B79953D3AB900A1",
    "tests/data/wave28-mark-manual-control.mpp":
        "41D774786BA76FDBDB3DB7D5670BFE892FBDAFA61805B39F6A912BDD4A5DFA1A",
    "tests/data/wave28-mark-manual.mpp":
        "DEF4ABF59135BFD3488B73D02E6955C03A38117778D3E7B80578E49096CADE56",
    "tests/data/wave28-update-control.mpp":
        "66847BFBF5C8BFEAE79A7847AF374DD0BB871C9B2E23D853C67D06DECA6D3164",
    "tests/data/wave28-update.mpp":
        "66E26371D5B5BC0A8A2A24213492CED9B06D1F4743F75BFAA3FE0DC49209EE62",
    # Wave 28 (W28-2.1): three same-session Project 2021 save pairs for
    # OutlineIndent/OutlineOutdent, built by the committed
    # scripts/oracle_w28_outline_edit.py on synthetic-base copies under C:/w28
    # with neutral metadata.  indent: ID 2 under ID 1, so ID 1 becomes a
    # summary; summary: ID 3 under ID 2 first, then ID 2 (and its child) under
    # ID 1, so a whole subtree moves down a level; outdent: the indent shape
    # with ID 2 outdented back to the top, so ID 1 stops being a summary.
    # The edited files are the field-by-field oracles for the port's outline
    # writer (W28-2.1).
    "tests/data/wave28-outline-indent-control.mpp":
        "4A7FDEE530C11EB68AEAF50E24E84EFFE206215267825588746526B7AB717514",
    "tests/data/wave28-outline-indent.mpp":
        "7A8AA14D33D633C39392BAEF5102BB2A36E0049951EA711C115D63D7E280E56F",
    "tests/data/wave28-outline-summary-control.mpp":
        "C2F809C243B0E43B4197EF9E655FAAD2D7A6061A1509B663A9AD76EE43355F09",
    "tests/data/wave28-outline-summary.mpp":
        "0A51E66CA4E550E8BCB50E3129452675C777B2F65AC14A5C230ADB7C20A10A2D",
    "tests/data/wave28-outline-outdent-control.mpp":
        "D2FBF8C358A91905C538DEE4185FFEE2EC6F3BD0B6CA56E984FCD1A48BF2A511",
    "tests/data/wave28-outline-outdent.mpp":
        "A03B8FC55B642FA520083BD2ECE8361DC305A43DDB0DB9AC7E447193F92277AD",
    # Wave 28 (W28-3.1): two same-session Project 2021 save pairs for Options >
    # Schedule > "New tasks created", built by the committed
    # scripts/oracle_w28_new_task_mode.py on synthetic-base copies under C:/w28
    # with neutral metadata.  manual_to_auto: control saved with
    # NewTasksCreatedAsManual=True, edited with False; auto_to_manual: the
    # reverse.  The option is 114/Props record 0x024013c8 (kind 2, 2 bytes);
    # Project normalises the base fixture's 0x0001 to 0x00FF for manual and
    # writes 0x0000 for auto.  The edited files are the field-by-field oracles
    # for the port's new-task-mode writer (W28-3.1).
    "tests/data/wave28-newtask-manual-control.mpp":
        "DFD3812D419E390CB25C2A67436E1C9FC0CD57AC092741514EC4CF13799F0E41",
    "tests/data/wave28-newtask-auto.mpp":
        "269EEB5707B20E257533E05F56C89F338D9B0F567D2D927706F05403CE572A2F",
    "tests/data/wave28-newtask-auto-control.mpp":
        "923FC593ABC10F521BBC6429D94823FC1DAF1ABFF66C762D30D26ED6E53247CB",
    "tests/data/wave28-newtask-manual.mpp":
        "988AE84E0E644F024D515E5E90A97807E8311145CE26C5EBAA4BE9C91B3F39FA",
    # Wave 28 (W28-5.1): one same-session Project 2021 save pair for Split Task
    # on an automatic leaf, built by the committed scripts/oracle_w28_split.py
    # on a synthetic-base copy under C:/w28 with neutral metadata.  Control:
    # ID 1 automatic, 5 days, 2026-01-05 08:00 -> 2026-01-09 17:00, one split
    # part.  Edited: ONE split (part 1 -> Wed 2026-01-07 17:00, part 2 Fri
    # 2026-01-09 08:00 -> Mon 2026-01-12 17:00), duration 2400 ticks kept,
    # finish +3 days.  The split lives on the ASSIGNMENT's 0x0F400031
    # planned-work contour (block 100 -> 128 bytes, first u32 0x00200000 ->
    # 0x00200003 and its 28-byte cumulative-work record list 2 -> 3, VarMeta
    # offset 8 -> 396; TBkndAssn Var2Data 396 -> 528) plus task row 1's
    # +100/+108/+116 dates and +168 bit 0x2000, the project row's
    # Duration/RemainingDuration and every other row's slack/late dates.  No
    # TBkndTask variable structure changes.  The pair is the measurement that
    # DEFERS the split writer (W28-5.1).
    "tests/data/wave28-split-control.mpp":
        "7C4243FE1548B16C4CAF2EBCB91D6753D913D228ECB2257944FF06C557D7B072",
    "tests/data/wave28-split.mpp":
        "F65E36B93117B6962679027282F85EBB20AB60CFB0448CD4755B7B53A6901EEC",
    # Wave 29 (W29-0.2): four Project 2021 save pairs on a shaped copy of the
    # synthetic wave25-auto.mpp schedule (one zero-duration milestone added),
    # built by the committed scripts/oracle_w29_percent.py under C:/w29 with
    # neutral metadata.  Each pair opens a fresh copy of the shaped base,
    # saves the control, applies ONE PercentComplete edit, saves, and then
    # reopens every saved file (facts JSON: build/w29/W29-0.2-project-percent.json).
    #   leaf      ID 3 (outline level 3)          0 -> 40 %
    #   work      ID 6 (work-bearing assignment)  0 -> 40 %
    #   milestone ID 7 (zero duration)            0 -> 100 %
    #   mid       ID 4 (outline level 3)         25 -> 60 %
    # leaf/work/milestone are row-local writes plus Project's summary/project
    # roll-up (percent, actual/remaining duration, ActualStart/Resume/Stop).
    # The mid pair also RESCHEDULES every row -- date caches, summary
    # durations, summary/project Resume/Stop -- which the port's save path
    # cannot reproduce because it links no scheduler; that pair is the named
    # failing check and the 25 -> 60 transition stays refused (W29-0.2).
    "tests/data/wave29-percent-leaf-control.mpp":
        "AB20D37140F9D241770DA3544952DA29E51B438F5003ED1D9BEB05BAB6B38544",
    "tests/data/wave29-percent-leaf40.mpp":
        "F1FC26932EFBF2F96C386B1F9579F7EE10709B92D2D3D8B636ABF906E8A3D2D4",
    "tests/data/wave29-percent-work-control.mpp":
        "0A1D8327EF2ED62E5F4DD2935E5B3585E2F41DBF9B40695B442C552966232BD8",
    "tests/data/wave29-percent-work40.mpp":
        "31A8E5E79FA36C97FAA2085B8ECC74E5E014951DFF25C8E483A82DBDC82DBC4D",
    "tests/data/wave29-percent-milestone-control.mpp":
        "2ED0BB0A27A02B77B4AC0A176827F09DEBEE303515422FCA58FCCD1A23A5BCA5",
    "tests/data/wave29-percent-milestone100.mpp":
        "17E9938FFBA881747E98B10FAA9CCD98A9E7FAAB1BA5455C121579AB7DB5249E",
    "tests/data/wave29-percent-mid25-control.mpp":
        "8975C16CCBB289FC451AF39BBB03E49803764B1354B99DD23B8A48D775C0EFE3",
    "tests/data/wave29-percent-mid60.mpp":
        "260875811C67B126970789CC035DC9595C4644359F7F0144433A7C17D10FBEF7",
    # Wave 29 (W29-0.3): five Project 2021 save pairs on a shaped copy of the
    # synthetic base whose ID 1 already records 60 % (ActualStart on the
    # effective start; its 0x0B40015E block is marker + middle 60 + final 60)
    # and whose IDs 2/3 are untouched leaves.  Mark on Track on ID 1 with
    # the through date after (2026-01-09) and before (2026-01-07) the recorded
    # progress; Update Project with All=True on both dates; and Update
    # Project's "Selected tasks" scope on IDs 1 and 2.  Measured: Project DOES
    # lower recorded progress (60 -> 40) when the date precedes it, appends a
    # middle step when raising and REPLACES the stored middle in place when
    # lowering; a date update's replaced 0F400032 does not move Props
    # 0x04000002; the Selected scope leaves the status-date record at
    # 0xffffffff and the project row's Resume/Stop at the project start.
    "tests/data/wave29-mark-60-after-control.mpp":
        "5CE3E87AB426681416F49DA17AD7F3B33218754DD6191BE17A9691BDFE797196",
    "tests/data/wave29-mark-60-after.mpp":
        "12394B4F6AAF5EA16764CEE529F7D3099A7C98523D0B7E328C38FDF9C6BF0FE1",
    "tests/data/wave29-mark-60-before-control.mpp":
        "C1CB43CAFF52BD11D1DD9F929BF94FBC7B300EA19E5A345C885309413D5D3362",
    "tests/data/wave29-mark-60-before.mpp":
        "154BCB6669C6DE91DBD3DF0A8844693F3ADD38BD75E7624CB8F323482913D71C",
    "tests/data/wave29-update-60-after-control.mpp":
        "EC283054C2B7FF50360D44971CA9C6D96668A18833B60DBA213C20D2543AD56D",
    "tests/data/wave29-update-60-after.mpp":
        "35ABDB9B628AA1765B970FB7058A7AC1BECE95F86007663F7B0FF0359CEECD82",
    "tests/data/wave29-update-60-before-control.mpp":
        "971B55C377425EABD81698DB558125B6FF21FCFE1BCB2AA9F755A771D2E0D0C6",
    "tests/data/wave29-update-60-before.mpp":
        "77D75938112EFDDCC4EE810BF9FFE8AA83A8664028CEFD468AAD81299D078D5A",
    "tests/data/wave29-update-60-selected-control.mpp":
        "F5ABBF88DA9D934FFAD69759397DB794FFA203D7458807CF93D8920EEE24B48A",
    "tests/data/wave29-update-60-selected.mpp":
        "8CBBFE3E2E9268F9C0A25C44044BD3DFBD65965C016F924E8E0431063A9435E6",
    # Wave 29 (W29-0.4): four Project 2021 save pairs on shaped copies of the
    # synthetic base, built by the committed scripts/oracle_w29_outline_auto.py
    # under C:/w29 with neutral metadata.  Each pair opens a fresh copy, sets
    # the scheduling modes through COM, saves the control, applies ONE outline
    # edit, saves, and reopens every saved file (facts JSON, not committed:
    # build/w29/W29-0.4-project-outline-auto.json).
    #   auto-indent   all rows automatic; ID 2 indented under ID 1
    #   auto-summary  all rows automatic; ID 3 indented under ID 2, then ID 2
    #                 (with its child) indented under ID 1
    #   auto-outdent  all rows automatic; ID 2 indented under ID 1 (the
    #                 auto-indent shape), then outdented back
    #   manual-parent ID 1 (the parent) and ID 3 manual, ID 2 automatic; ID 2
    #                 indented under ID 1, then outdented (the hybrid pair)
    # Project recalculates every automatic row on save: the five schedule words
    # (+74/+112/+116/+186/+190) and the duration/finish roll-up (+88, +108 and
    # the project row's +84/+88/+100/+108).  The hybrid pair recalculates its
    # MANUAL restored parent too and leaves the unrelated manual row (ID 3)
    # stale.  These pairs are the W29-0.4 measurement.
    "tests/data/wave29-outline-auto-indent-control.mpp":
        "23CEF994955FBF733719AEAACD5EE273064A84FDE36E4D564E99712525C0C5E2",
    "tests/data/wave29-outline-auto-indent.mpp":
        "566785C4217A118B1D6CC371935324BA44F25644D25351BCA4883DDC57EC7AF2",
    "tests/data/wave29-outline-auto-summary-control.mpp":
        "4BC7A41F03729A69F1D0016D39885E9A267E84D3990943CF47C365D2DFF32FC1",
    "tests/data/wave29-outline-auto-summary.mpp":
        "26FF79D9AF3C368933015E8ED8D141609D4C24080CB54A00033E30DE03AA58A2",
    "tests/data/wave29-outline-auto-outdent-control.mpp":
        "66B22FC1260B99228145FE3D846D4922398EAD6B3466BED370AB85706554C35F",
    "tests/data/wave29-outline-auto-outdent.mpp":
        "7D3009F51D66D67B01086D55055A495C7E0265E4F25F3A40C7E4453C31193AA4",
    "tests/data/wave29-outline-manual-parent-control.mpp":
        "C9BBE9ECA30BF11145C466D2D832EC9E27F25A0EB5AC366945E4F81D3384BC01",
    "tests/data/wave29-outline-manual-parent.mpp":
        "6230E118C3E84EC7C4E284DD4B496D4CB21933B79CFA742B7FC57958B63948BD",
    # Wave 30 (W30-1.0): two Project 2021 save pairs for the Manually Schedule
    # mode write on the synthetic base (all three rows manual), built by the
    # committed scripts/oracle_w30_task_mode.py under C:/w30/fixtures-mode
    # with neutral metadata.  Each pair opens a fresh copy, sets Task ID 1's
    # mode through COM, saves the control, changes the mode again, saves, and
    # reopens every saved file (facts JSON, not committed:
    # build/w30/W30-1.0-project-task-mode.json).
    #   auto_to_manual  ID 1 automatic in the control -> Manual=True; the row's
    #                   bytes move (FixedMeta +17/+18, Fixed2Meta +8, FixedData
    #                   +174, Fixed2Data +50/+54/+58 and the placeholder
    #                   assignment's Fixed2Meta +8) and no date moves.
    #   manual_to_auto  the all-manual base -> ID 1 Manual=False; Project
    #                   RESCHEDULES the row (Start/Finish move), so this
    #                   direction stays disabled in the port.
    # Both controls get a Project no-edit save (the -null fixtures) from
    # scripts/oracle_w30_null_saves.py under C:/w30/null.
    "tests/data/wave30-task-mode-auto-control.mpp":
        "B47984D75E56E378AE1E12670CC4B0E13FD3D9A05D4A280420A48682FAE6CE0F",
    "tests/data/wave30-task-mode-auto-control-null.mpp":
        "45E06E5BE0E4148CD78E5EA2B3ACCF90E3A029780AA2E651680F77F76D52C540",
    "tests/data/wave30-task-mode-manual.mpp":
        "E959D5386431369CFB97EC2DF12C1DCF0D96E1D17F549D9D0F60A9377787BE9D",
    "tests/data/wave30-task-mode-manual-control.mpp":
        "4E3D06F9236D57E7DB8A78C40AB704CB33C3765DC8837B0B6D20B328A2844D7A",
    "tests/data/wave30-task-mode-manual-control-null.mpp":
        "AEE6A9F12112224809543F95D7A017BCF5FB0503D18075FCEFA432A87620A723",
    "tests/data/wave30-task-mode-auto.mpp":
        "9E8ECD8F4E554C72D0FFF79F1C43254CF06141248188849C196F8E73927AE3F1",
    # Wave 30 (W30-2.0): the Resource tab's Add Resources pair, built by
    # scripts/oracle_w30_resource_pairs.py under C:/w30/fixtures-g2 with
    # neutral metadata, plus the control's no-edit save from
    # scripts/oracle_w30_null_saves.py.  Project adds a Work resource row AND
    # a resource-calendar row; the port's add_resource_row writes only the six
    # TBkndRsc streams, so the command stays disabled (tests/data/README.md).
    "tests/data/wave30-resource-add-control.mpp":
        "A5B12A50D56BAFD097680E5F078EA007BAB7CFEB30B69BAFEBEDF3387937EC55",
    "tests/data/wave30-resource-add.mpp":
        "527578E5C62E11FF2B32298D5E28A4A080707E219DD083809D46370B164FCCE2",
    "tests/data/wave30-resource-add-control-null.mpp":
        "A07A5F75F0AE7F7678965A87F3FFBB8DFA7212A9379A3A849CA3FC36CE5BDFD6",
    # Wave 30 (W30-2.1): the resource writes on the resource-add copy --
    # Information's one-field Name change and a first Notes entry -- built by
    # scripts/oracle_w30_resource_pairs.py, with each control's no-edit save.
    # Both are deferred: the port has no resource-field writer and its writer
    # never inserts or updates a VarMeta entry (tests/data/README.md).
    "tests/data/wave30-resource-rename-control.mpp":
        "CFCA5DFCA33F673FE1536E995E835C40CD14CE6D9C8E8EEA748A6BD43629BC3A",
    "tests/data/wave30-resource-rename.mpp":
        "7F4540A5F7258CEE3AFDD41107C021DE6EB8F2B832414990F6C1A7D5A12B674D",
    "tests/data/wave30-resource-rename-control-null.mpp":
        "8A3DC36D0802D8CB3A96DB1F7855BD33F784365D887B45CF695C9EB360BEA998",
    "tests/data/wave30-resource-notes-control.mpp":
        "5335C5781F2B15C28145B7EA981237604EB86588C4426A60AE6F33C7326207C5",
    "tests/data/wave30-resource-notes.mpp":
        "EE755F6FA06FF3BF7E1C56F40D47ACD6EF034C9E97A0C969386A2B8F7F4CCF76",
    "tests/data/wave30-resource-notes-control-null.mpp":
        "A8DB92428104996C0C4B85FE43510FC717DDC7ED689E4415782C8EC3FC69B096",
    # Wave 30 (W30-2.3): Assign Resources -- one work assignment added to
    # task 1, then removed -- built by scripts/oracle_w30_resource_pairs.py,
    # with each control's no-edit save.  Project tombstones/churns the
    # assignment table and updates scheduler pointer words, which the port's
    # append-only add_assignment_row and FixedMeta-only delete_assignment_row
    # cannot write; the command stays disabled (tests/data/README.md).
    "tests/data/wave30-assign-add-control.mpp":
        "A7F5ACD93B1CA7E79FE6D8E764C2EC7AE14E396641C0C61BEFBF290F72AEF489",
    "tests/data/wave30-assign-add.mpp":
        "5F328B959D5E368B266BDB65FE30BFE806FBD36C9828EAF3F931A02D266E1471",
    "tests/data/wave30-assign-add-control-null.mpp":
        "648947C79352BC8D1700D3D5AE894D592356AE307928FC55490A29841D2F9122",
    "tests/data/wave30-assign-delete-control.mpp":
        "F889991BB76075FB51F0A6D0964B53478F0B41ED85CB1EA11ABC769F9D292B3B",
    "tests/data/wave30-assign-delete.mpp":
        "ABFF616786BA1F1F5F0D0D845FC8B9A7C1575A9F20E48E1A899185F4FA812E2D",
    "tests/data/wave30-assign-delete-control-null.mpp":
        "1D3AEA3AE7CF364BC68FEDBF10B5D68BEC00B567180D23E02D9F543B9FBB8490",
    # Wave 30 (W30-2.4): Level All on a fresh automatic, overallocated
    # project -- three 5/3/2-day tasks sharing one 100 % Work resource --
    # built by scripts/oracle_w30_level.py from a reopen-stable seed control,
    # with that control's no-edit save.  Project reschedules the rows and
    # writes leveling delays; the port has no scheduler and no leveling fields,
    # so all six Leveling commands stay disabled (tests/data/README.md).
    "tests/data/wave30-level-control.mpp":
        "CE992A994FEE8CF1B5BD599439F0C4BAAEBA3DD1FE662F74A2A9AFB1605778A6",
    "tests/data/wave30-level-all.mpp":
        "66A2B82F783D3CBF50CFF5FDCE2F23B1CAB1585D20F3ADA966293B0C5047AE44",
    "tests/data/wave30-level-control-null.mpp":
        "F944680A41AF05D5B948B4CB496788289A5CEF9673FAB5E01123CB63D01E2E37",
    # Wave 30 (W30-3.0): Calculate Project -- F9 on a MANUAL-calculation
    # project whose linked automatic successor is still stale -- built by
    # scripts/oracle_w30_project_pairs.py from a reopened seed (a first save
    # after FileNew is not reopen-stable), with the control's no-edit save.
    # Project's F9 writes the recalculated successor dates, the project row's
    # rolled-up Duration/Finish and new Var2Data/VarMeta entries, which the
    # port's writer cannot reproduce; the button stays disabled
    # (tests/data/README.md).
    "tests/data/wave30-calculate-control.mpp":
        "8E4E81D849DF6452EEF24177BF5D90F58A19CA70D170636FA953DFBC6EAB0FD3",
    "tests/data/wave30-calculate.mpp":
        "B50359BFA22C628E6204747B1E2ABD08ED262AA21608819D49245F3636918A50",
    "tests/data/wave30-calculate-control-null.mpp":
        "8FF4083A6F1416D9046A45472DB83132A29003A3E1BA5D75F03D2C9088DD473F",
    # Wave 30 (W30-3.1): Set Baseline -- Application.BaselineSave(All=True,
    # Copy=pjCopyCurrent, Into=Baseline) on the synthetic base -- built by
    # scripts/oracle_w30_project_pairs.py, with the control's no-edit save.
    # Project writes BaselineStart/Finish/Duration for every task and appends
    # baseline blocks to the task, assignment, resource and calendar tables
    # (Task Var2Data 522 -> 1742), which the port's writer cannot reproduce;
    # the menu stays disabled (tests/data/README.md).
    "tests/data/wave30-baseline-control.mpp":
        "F665EA3882FE006AB97E33C105CF1CE17E03A97A41EB12DCDCF744F05A2C7445",
    "tests/data/wave30-baseline-set.mpp":
        "6CF2142905DE7A8AC5CC1B44FF198DB0A23B0F9D45A2976CA4B3603DBBCF718E",
    "tests/data/wave30-baseline-control-null.mpp":
        "003D490B0C350A89825FE7EB83AC71E808A5CD555197102B5A724B9F81B9DC2E",
    # Wave 30 (W30-3.2): Define Code + Renumber -- WBSCodeMaskEdit("W", level
    # 1, ordered, length 2, ".", generate, verify) then WBSCodeRenumber(All)
    # on the synthetic base, built by scripts/oracle_w30_project_pairs.py, with
    # the control's no-edit save.  Task WBS becomes W01/W02/W03; the save
    # inserts 24 bytes into 114/Props (the mask fields) and appends four WBS
    # Var2Data records plus four VarMeta entries, which the port cannot write;
    # the menu stays disabled (tests/data/README.md).
    "tests/data/wave30-wbs-control.mpp":
        "29C60D6DAF12B028FCD0025F1BC51ACE25A5ACB2055E71D3F8F958612028E628",
    "tests/data/wave30-wbs-code.mpp":
        "DE7CBDC3D4C9822551297DC028F1938D75EC5E43D2AB555F2A197BD685B1D985",
    "tests/data/wave30-wbs-control-null.mpp":
        "D627370E48C4595C911A425398727D29D072E89F0FE6B97C4062CA3242A9CE2A",
    # Wave 31 (W31-0.4 / R30-4): Project's Fill Down of Start over two
    # top-level automatic leaf rows, built by scripts/oracle_w31_fill.py, with
    # the control's no-edit save.  The fill's save is a scheduler rewrite of
    # the edited row (constraint AND calculated/effective dates and slack),
    # the project-row roll-up, an appended Task Var2 record and the
    # DocumentSummaryInformation summary strings; the port's start writer
    # writes only the constraint window, so Fill Down stays disabled
    # (tests/data/README.md).
    "tests/data/wave31-fill-control.mpp":
        "594C80CCC9D1C25F70B66BA1A86DC26E3FC21758CCB633342750AACE2F4FBAE7",
    "tests/data/wave31-fill.mpp":
        "9DCFE5D48BCCA159231564CB80F952CF8BDEA35E01BDA452FFE1F3A3E3B7CA9C",
    "tests/data/wave31-fill-control-null.mpp":
        "875C2AD5BBE198766174ADAA5E2FA5549458B81588B56313090D2745D06270A8",
    # Wave 31 (W31-1.3): synthetic-base.mpp saved by Project 2021 with a SECOND
    # stored task table.  `TableEdit(Create=True, NewName="Review")` copied
    # Entry, then a replacement TableEdit put Text1 (0x0b400033, title
    # "Text1") in the copy's second column (the Indicators record).  The file
    # still APPLIES Entry; both task tables are stored, so the Tables command's
    # file-driven list and switching are exercised in CI.  Built by
    # scripts/oracle_w31_tables.py under the neutral root C:/w31/tables with
    # neutral Author/Manager/Company/UserName/Title.
    "tests/data/wave31-tables.mpp":
        "5AEB9C4F6A97893E1C8F3A16BE3FFF4AB100EF3D23D66AB1C595810596C987E8",
    # Wave 31 (W31-3.1): synthetic-base.mpp saved by Project 2021 with the
    # Resource Sheet applied and the resource Entry table applied.  Three Work
    # resources: Crew A is overallocated at 300 %, and the rows carry
    # Group/Code/rates/AccrueAt and Project's computed PeakUnits/Work.  The
    # saved file also keeps a TOMBSTONED resource row (uid 1, FixedMeta entry
    # +0 = 0x02) left by deleting the base resource, which the reader skips
    # from W31-3.2 on.  Built by scripts/oracle_w31_resource_sheet.py under
    # the neutral root C:/w31/resource-sheet with neutral metadata.
    "tests/data/wave31-resource-sheet.mpp":
        "E8166300CA35B6042BEA4967C0E54894AC5ECE99613EBDE72EF907CFC093EF48",
    # Wave 31 (W31-4.1): synthetic-base.mpp saved by Project 2021 with the
    # Task Usage and Resource Usage details set (stored ids 00 02 and 00 09),
    # both timescale style items at weeks/days 1/1, formats 13/20, 100 %.
    # Three 100 %-loaded resources on three automatic tasks, task 1 100 %
    # complete; the COM and TimeScaleData facts and the measured 21/42 px
    # rows, 48 px Details and 64/105 px day columns are in the paired
    # measures file.
    "tests/data/wave31-usage.mpp":
        "AF8DFE1DCC4EE319378FDBD3F454D398F5ACDC924BB66E9F0B4CC8879A1D600A",
    # Wave 33 (W33-1.7 / A-8 / D-7): the FAT-slack fixture.  synthetic-base.mpp
    # (237568 bytes, 463 physical sectors) plus 64 zero sectors = 270336 bytes
    # and 527 physical sectors.  For 512-byte sectors the FAT has num_fat * 128
    # = 512 entries, so the FAT describes 512 sectors and the reader warns that
    # 15 are unreferenced -- the shape Project writes that mppwrite's
    # CfbDocument::to_bytes refuses to re-emit.  Built by appending 32768 zero
    # bytes to a copy; the committed fixture is never opened in place.
    "tests/data/wave33-fat-slack.mpp":
        "E3A7D6CBBB82EA35901D6C2CE8C7E4D40DE032D424785CBA6F6BED41B93A85C8",
    # Wave 34 (W34-0.3): eight measured save pairs, each with its Project
    # no-edit null save, from scripts/oracle_w34_notes.py,
    # oracle_w34_calculate.py, oracle_w34_resource.py and
    # oracle_w34_null_saves.py.  The six note pairs complete the RichEdit
    # escaping/replace/clear rules; calculate-second shows a Calculate with
    # nothing pending writes nothing back; resource-add-third measures a
    # second added resource calendar.  All are synthetic-base descendants with
    # synthetic text and neutral metadata.
    "tests/data/wave34-note-escape-control.mpp":
        "D3716B4A014E7AFBBCEB505BADB6FAF3B866B89400CE9CBFA935BED4219B2BCD",
    "tests/data/wave34-note-escape.mpp":
        "B4C046193F7945B8FFBEFA86EA7F75A8E2E7E7EF820B150FBD44D8DF4017A244",
    "tests/data/wave34-note-escape-control-null.mpp":
        "51BC76503AE665A37F2BB2C7ACF45BA1861F67D4AAA4BA14166099E7EF377282",
    "tests/data/wave34-note-nonascii-control.mpp":
        "83FB3E29EAED4A78F918C8F1504AC29E0C4DA2321511580144CD53934EA7B9CD",
    "tests/data/wave34-note-nonascii.mpp":
        "1DAA9DF7103BF759CC28AE04CE787EC79B006A94717419D17E428AC83AB48912",
    "tests/data/wave34-note-nonascii-control-null.mpp":
        "02B8D386FD58F8681D058A0518E3790D88E7F07D3E89571EB8932B9A57971ECF",
    "tests/data/wave34-note-multiline-control.mpp":
        "54F2F8EFE224E8CBD84DC4C233DDF0E32A7B8247EDB45BE3C123E19B213CAE9C",
    "tests/data/wave34-note-multiline.mpp":
        "C8BDB1CE13D05C79C5E8F0A264183F3359C8EF79F0E212C8D1956E18691E43C0",
    "tests/data/wave34-note-multiline-control-null.mpp":
        "F753D091781589F5E5AFB6596406EDCC69581B19537491D7369D01FC03DBEECF",
    "tests/data/wave34-note-replace-control.mpp":
        "C8138B42647778E397BFD1FD4DC0C66560D4F45028A12282E46171689F859445",
    "tests/data/wave34-note-replace.mpp":
        "46DD8D1025745F8A5C79029E2657BE82AC56CA62671D88F421DF56EB4BFFE8A9",
    "tests/data/wave34-note-replace-control-null.mpp":
        "E75F22B32AD59D95B3B6841C8555AF5498EF2AF2A247007F5A6A01D05BBD9BBE",
    "tests/data/wave34-note-clear-control.mpp":
        "B5413DBD8A1E3BA62E02FBEAA30D29F908F8E21701D57BEFF0B56C3FF66B378D",
    "tests/data/wave34-note-clear.mpp":
        "46FB6F4526557B46FDE28336280FACA916E577DD213AADA38054B8818C5653D0",
    "tests/data/wave34-note-clear-control-null.mpp":
        "8B30DD6D64AC063F9195E429AA161ABAB0513F97720376C9B9FD835C269675E6",
    "tests/data/wave34-note-two-control.mpp":
        "CED6627787ECF8C6200B8ED4C7E7AF772D5A1E32A17297385B913F6EB4518F48",
    "tests/data/wave34-note-two.mpp":
        "FC7D0B3E0F8A656C9334A0C7979AF0C4E2E1D96922A125C73346F9F975484C7A",
    "tests/data/wave34-note-two-control-null.mpp":
        "2ED165C9DFEEDC678A3785805C0B04797A3C9ED94094167945FDA1DBABD30FC0",
    "tests/data/wave34-calculate-second-control.mpp":
        "40D8D856AD43EDFDEE4C54EA11658F3DEFE198B12F55481F019939619E8790BC",
    "tests/data/wave34-calculate-second.mpp":
        "22F71DDF12D5823FDF99F7A47488A21FEA00CF578031EBFB3455F9C138864718",
    "tests/data/wave34-calculate-second-control-null.mpp":
        "9B3708A54DC6B3A31E9E491CEA24AF20B3C05F0D10141BB45613966EC62BE45F",
    # Wave 35 (W35-1.2): the Calculate Project pairs, one factor each.
    "tests/data/wave35-calculate-lag-control-null.mpp":
        "BC150637B9599E551B81698E2E8E976A7F40E1160DB59CD857A7E3A4412942F3",
    "tests/data/wave35-calculate-lag-control.mpp":
        "5FB86983C46E9E65F9A71912C863751D74082694D7F741D9F8814F76700F6927",
    "tests/data/wave35-calculate-lag.mpp":
        "4F7EC8BA54BF7B5DC51560E5DCA20706185C08975EBA47D2B60E0167592536D7",
    "tests/data/wave35-calculate-linked-control-null.mpp":
        "74135BE685CCE8AEEB783B884B3701681433B88B5E0A37D8BEF50B6A83C0E601",
    "tests/data/wave35-calculate-linked-control.mpp":
        "CCD4F0057DA2EE2EE7A06F5E44400EE0D294B883832ADE124ED8DCA5F2F02F25",
    "tests/data/wave35-calculate-linked.mpp":
        "F341C8D8B9DDD077D5EBE128926DC84478670EB82BFC092FF1B970559C1095BA",
    "tests/data/wave35-calculate-snet-control-null.mpp":
        "0702CF99FF66D8D39E5E5C573E5A0F2CF90695E48A79F0BB122EBB488D781058",
    "tests/data/wave35-calculate-snet-control.mpp":
        "E7E1963402B916FD7E3040EB2B16697BC0E9EEAE21941FDF4EA15238A848216B",
    "tests/data/wave35-calculate-snet.mpp":
        "D200621C54DBB560E8850BC1A3266D43A60BBA320702D78549FE1DD0312DD3B5",
    "tests/data/wave35-calculate-unlinked-control-null.mpp":
        "9F9DCECAE587F39280B42AF992E9347646FE54965B2564377E3A09AC9E5E2313",
    "tests/data/wave35-calculate-unlinked-control.mpp":
        "39FCBD668AE0B98D59AD6D15410512980CE98E8C2858C9A07D2296B14B8C83FD",
    "tests/data/wave35-calculate-unlinked.mpp":
        "61B9B02AEA0D8C7A125DB287DFD60EF06F8FBCF7A9F58F276E2DD764F776F7E1",
    # Wave 35 (W35-2.1): the Sort pairs, one factor each.
    "tests/data/wave35-viewstate-sort-duration-desc-control-null.mpp":
        "A517B46CC0A4FD0673BE2CC75BE329498D47ADFC5D6D2D7ABFF57A39A2F49D96",
    "tests/data/wave35-viewstate-sort-duration-desc-control.mpp":
        "09772916229CC93B3C113434C1B40CF19D61A89074D2C47CAF4FF938D43C27AC",
    "tests/data/wave35-viewstate-sort-duration-desc.mpp":
        "725A211D7CD4AD9272F5F2A159CB797AECDEF22731CBE4C10BC537DE26397682",
    "tests/data/wave35-viewstate-sort-flat-name-desc-control-null.mpp":
        "278F89C2B76EBF87AC150C3657B97755EC2795F0AB43E6EEF86E209F0B058017",
    "tests/data/wave35-viewstate-sort-flat-name-desc-control.mpp":
        "896234F2BD8F90F0A97FE68961970F174D72D5DCE32C818CE43A6525A94C33F0",
    "tests/data/wave35-viewstate-sort-flat-name-desc.mpp":
        "68CC7954FB91CB7D6557D78BBF67640DD2B15FB62EB3EEFD291D411089221602",
    "tests/data/wave35-viewstate-sort-name-asc-control-null.mpp":
        "B85A3FA82467D529524B0DF08C89A0AEAB0FB6F5A2271A18E4DC82FC41E71CA6",
    "tests/data/wave35-viewstate-sort-name-asc-control.mpp":
        "3F19CFDE563AE796648DB8FF2E56B9E74672A131249033D5CC0E43CE7F94150F",
    "tests/data/wave35-viewstate-sort-name-asc.mpp":
        "440168CBEEBA1158EC5E508771B4B9C3974DB27E731F9178C138945D9E0A8A8E",
    "tests/data/wave35-viewstate-sort-name-desc-control-null.mpp":
        "0DC1D465DA386C3789C21E2E8C794599C3993D87B866F73234744EB8C4979678",
    "tests/data/wave35-viewstate-sort-name-desc-control.mpp":
        "E901ECADD6B7DF7DDDA30CE888FF44FACF96EA86704D2D82BD1C59FEC19C512F",
    "tests/data/wave35-viewstate-sort-name-desc.mpp":
        "2473BAFB24C4E1500D2C11A73773DFBB64E1E8220FF759CF0B0462D73707E58E",
    "tests/data/wave35-viewstate-sort-two-keys-control-null.mpp":
        "FEE481AF9A2118A2FEA2D63A8C9097CCA70314E58C3F77BFE3EE6A1521C91952",
    "tests/data/wave35-viewstate-sort-two-keys-control.mpp":
        "2AB28E72F9C7DA7A31555275572EF1C798A7F9ED29DCEAB676F876E2C6745A68",
    "tests/data/wave35-viewstate-sort-two-keys.mpp":
        "79DCF0B490EEA25B0E5D13D260D2472D60597758EEDE1BEC9F11BB9A4B21CBE0",
    # W35-2.2 table fixtures (Application.TableApply)
    "tests/data/wave35-viewstate-table-cost-control-null.mpp":
        "1A10CB0EE18430734D497B0F57B23E0E51CAC1283528BD2DC050389C6BEEC579",
    "tests/data/wave35-viewstate-table-cost-control.mpp":
        "23C950CED6E271CE84114378E2D03001960A39F76FB2A4C03F4FDF886497FF25",
    "tests/data/wave35-viewstate-table-cost.mpp":
        "F29B4384EB70C853390B7D01516440E9A20FD00E7E1B379115440B1A4A39EED4",
    "tests/data/wave35-viewstate-table-schedule-control-null.mpp":
        "256A0350430DF9064D72371228262685D97A869BA256729D9A27F0BC0A8A9878",
    "tests/data/wave35-viewstate-table-schedule-control.mpp":
        "1A4FABE7F10956530E5F9F38C28CD4975235D9A673BAB3A6EAB2D26AED4821E5",
    "tests/data/wave35-viewstate-table-schedule.mpp":
        "47DB52EBA9F99A556523EBC0F3B261FD0B87264C8E931B5588B8E2F5F0DE83DF",
    # W35-2.3 outline fixtures (Application.OutlineShowTasks)
    "tests/data/wave35-viewstate-outline-all-control-null.mpp":
        "E0D70A05635D9F1D58754E98B404CA7E4DF28351A2B2540A75FBBF6662E07132",
    "tests/data/wave35-viewstate-outline-all-control.mpp":
        "1EF56EC6EEA740C3DCB896F7CF38D1748CD9B3B88E187E0E1662FD0E0A7B9D20",
    "tests/data/wave35-viewstate-outline-all.mpp":
        "EEB61D10E2BB753B0E3605A980A8DC6593B92FFBCE58C2D246B8DA8D4E296C4E",
    "tests/data/wave35-viewstate-outline-level1-control-null.mpp":
        "A7BB9B6877EB3911D30788DC6DAAD35EFF658C2DA94395A243A44F983D5A5450",
    "tests/data/wave35-viewstate-outline-level1-control.mpp":
        "75B319AC737D9415184CEBA365ADE6949588F226631361DC7FC63AE12A7F972D",
    "tests/data/wave35-viewstate-outline-level1.mpp":
        "B5037B1BC64B58023C0819F60AE300ABA6F684BB598AEF4595F31208772AD671",
    "tests/data/wave35-viewstate-outline-level2-control-null.mpp":
        "029621B820FABC6041E4FC139600EAE49A763D4BD53990954F1FCF40F247066E",
    "tests/data/wave35-viewstate-outline-level2-control.mpp":
        "EDA2BE6F0A1F4283FD29604F3B5CD8C7EB8ED3AF907998A8EA1A5B517CEA399E",
    "tests/data/wave35-viewstate-outline-level2.mpp":
        "689510CD7352828E5A3C1B16C0C15BD47A74667D305275C2D52F0BAD90EFBD83",
    # W35-2.3 outline fixtures, three-level base (Application.OutlineShowTasks)
    "tests/data/wave35-viewstate-outline-deep-level1-control-null.mpp":
        "9326F007BCF82A791260836207C754CDBC7BF3764F0FE057D5114949DDC96826",
    "tests/data/wave35-viewstate-outline-deep-level1-control.mpp":
        "57F34F48CC31EB05B98A21C8016862BF8433457D404AD8EC7C4EC4330EB5BBDD",
    "tests/data/wave35-viewstate-outline-deep-level1.mpp":
        "0033075EBDF7A3CB1E2785F4BE46B011F57D6DBF19797191538B2BB38E3EA7A7",
    "tests/data/wave35-viewstate-outline-deep-level2-control-null.mpp":
        "A02436AEC23F0219BFAA2387FDD4C8B35E6089BC325E21567225BCC9E241A504",
    "tests/data/wave35-viewstate-outline-deep-level2-control.mpp":
        "9C858EBF5BBD4796F34D3B16F165EB1996FF629E37FF0AAA4286D71171C80E57",
    "tests/data/wave35-viewstate-outline-deep-level2.mpp":
        "1353CAA790A5D92492F03E560187835167C204D1306FDA21DC980638DB8DA3B7",
    # W35-2.4 zoom fixtures (Application.ZoomTimescale)
    "tests/data/wave35-viewstate-zoom-custom-control-null.mpp":
        "CFE5C279EBADCDC5E49C32F968A881066A5986EA3A9C3E49CCD6BA2F38CF4C72",
    "tests/data/wave35-viewstate-zoom-custom-control.mpp":
        "AAD57DD553F87856E3F6D10341EAD415ACD8FDD1FA515AA3B39D024CD60AE005",
    "tests/data/wave35-viewstate-zoom-custom.mpp":
        "02437C5F7F2FBADA339FAC92C9CAE1DD7B239669CAD467FB0F914C7699926A5E",
    "tests/data/wave35-viewstate-zoom-custom-10d-control-null.mpp":
        "68593B3F02D25A88B5251A64FEDECCAECA25A6C2DCE306122CD710492CAE5858",
    "tests/data/wave35-viewstate-zoom-custom-10d-control.mpp":
        "398840AD27ADE02FD508C565CFE2E6B9AD93AD7AD79C955CC5A58DB2BE95929B",
    "tests/data/wave35-viewstate-zoom-custom-10d.mpp":
        "8BB9731497EF55DD7F06507A5A55B3BCDE89185D6DD71CB646CB7C11FF07975A",
    "tests/data/wave35-viewstate-zoom-custom-narrow-control-null.mpp":
        "43090F7480F789716A4FDA148842358304D9302A04DE4CC4E33D59F033099A94",
    "tests/data/wave35-viewstate-zoom-custom-narrow-control.mpp":
        "36D57377A444EB96EE0F377C502C43ABAB7292F17EEAF9C72866E612C18E1AC2",
    "tests/data/wave35-viewstate-zoom-custom-narrow.mpp":
        "4B69F745C41EC4F202DDFD7BC2FAD265DAA67D8F0AF79D4E60EE534C2CC0CE42",
    "tests/data/wave35-viewstate-zoom-entire-control-null.mpp":
        "64F59D832F7796A3A492A5612C32135D4235D65204B57C77725F0DE572CC799C",
    "tests/data/wave35-viewstate-zoom-entire-control.mpp":
        "3DD9599B13149038CD9C81341CAC7516C7A695EEA4DF489574B43F80F9D794A2",
    "tests/data/wave35-viewstate-zoom-entire.mpp":
        "98A91E6DE7271B6B06AF7413FEFDDAE4F379A49A4B08ABED72AF8C52FCC4B5A4",
    "tests/data/wave35-viewstate-zoom-reset-control-null.mpp":
        "87DEB891C4FB3B2B564B4E5621F1B678D7B860A385D70915E5F84E5F5470F402",
    "tests/data/wave35-viewstate-zoom-reset-control.mpp":
        "6A536AE817E7E30D7578B8F909387C58A08D4CD32EEE0F2033DFC029D2B9CC3A",
    "tests/data/wave35-viewstate-zoom-reset.mpp":
        "C69D367156042604246A83E3378B611A3F718D54AC7B018C7E6B3E0D67E9BFFF",
    # Wave 35 (W35-3.2): the G3 pairs.
    "tests/data/wave35-g3-baseline-set-nested-control-null.mpp":
        "5C5A18EF317D487BE3823DDF09B02644D90948CA7BCE3C3B0330C59CE740E6B8",
    "tests/data/wave35-g3-baseline-set-nested-control.mpp":
        "C841BADC860A6F4D8038E9A1FB5B9B077F649628D629DED99F57624EC21535B2",
    "tests/data/wave35-g3-baseline-set-nested.mpp":
        "5B4E50A443F61B109041EEEC457E1965FB2011A30946083120EAA0205A44D8CF",
    # Wave 35 (W35-3.1): the G3 pairs.
    "tests/data/wave35-g3-insert-milestone-first-control-null.mpp":
        "0CA96AA029332F36AA79CFFDFE0F6F424DBC16C124674E188A60D68A3FD04DFE",
    "tests/data/wave35-g3-insert-milestone-first-control.mpp":
        "D406A2F80F9E747C2FCA09825C123B46721CEEF266A4CE749C4078E4DB888F2D",
    "tests/data/wave35-g3-insert-milestone-first.mpp":
        "7500C66EC955BE893CE5209F608DD30E0E2803A3ED7F04F2E81BAFD1D079E601",
    "tests/data/wave35-g3-insert-milestone-last-control-null.mpp":
        "0CD83F6E7F618591151AF3CCD134F9F4FFE192297797674C9FCADDE35396B1F3",
    "tests/data/wave35-g3-insert-milestone-last-control.mpp":
        "85255060B45BB97F6760238808E9EB1472C9AA7AD5FFF3CC31FB4C4E4EB1B83A",
    "tests/data/wave35-g3-insert-milestone-last.mpp":
        "0CCADA1CC0886E05412CEAF926564E299AAB42D4FCD1B2EFAC33E5C01CC06CF3",
    "tests/data/wave35-g3-insert-milestone-nested-control-null.mpp":
        "7B447D7F66F1B5B2649305602E573532148ED9CEA03DDB888124541B23C6E339",
    "tests/data/wave35-g3-insert-milestone-nested-control.mpp":
        "DECE1C39399AC2CCD5C084AD77D62461EAD8CCF4A0EDF4493996147B8B9FDF43",
    "tests/data/wave35-g3-insert-milestone-nested.mpp":
        "06E7540B184EA4346FC80FADF39F3B84F40C8C9FCC772911B9146889418E5529",
    "tests/data/wave35-g3-insert-summary-nested-control-null.mpp":
        "F73C2C3252C733F468FC62982F3DE1D8EA407B7062A632B6EA78E7C4ED50212C",
    "tests/data/wave35-g3-insert-summary-nested-control.mpp":
        "5B799DADE3839596460F2352B1E4F47D27154B3325719BEA1EDEFFBB1B5ECB45",
    "tests/data/wave35-g3-insert-summary-nested.mpp":
        "4370DCD988F78AF6ECC195F5962012182627908D1FDC23DCCDB20CCEE6FB88E9",
    "tests/data/wave34-resource-add-third-control.mpp":
        "F9DD625CFD94FD6A29B616F102EB005B637409662F0C2465C0ADEA4FCBC759F4",
    "tests/data/wave34-resource-add-third.mpp":
        "19F03986C0076ED065DCDA5E4C63192D79DB0768797AC0A51148329C887A5D00",
    "tests/data/wave34-resource-add-third-control-null.mpp":
        "3FA4DDC99BBD9B093D7B964AF511098993906175BAFFD4C57141B397D11CF86F",
    # Wave 34 (W34-3.1): the two assignment-count append pairs (count 1 -> 2
    # and 2 -> 3 on task UniqueID 1) and each control's Project no-edit save.
    # Measured by scripts/oracle_w34_assign_counts.py and
    # scripts/oracle_w34_null_saves.py under C:/w34.
    "tests/data/wave34-assign-add-second-control.mpp":
        "1398A7DB6DB2533F8B17C44242D417656EEB43E7B05FC081BFCC1E2426DC54B2",
    "tests/data/wave34-assign-add-second.mpp":
        "82A611093CB9D3021C6F32E26B574D00D414DB6EAD524A9DF9E9DFD31387457E",
    "tests/data/wave34-assign-add-second-control-null.mpp":
        "1259166E4AF3AA385AED37353C29F01D8ABEC82395A2250C1AB815546A1A6582",
    "tests/data/wave34-assign-add-third-control.mpp":
        "3829429E261584B04C3C58F97C078A57C4817A69C57C4479BF405040CFCF84E2",
    "tests/data/wave34-assign-add-third.mpp":
        "D0B1AB50F867EB7A7C48AF47FD5569D1940BD98529F5B4EFE982E1F53CCFDA6A",
    "tests/data/wave34-assign-add-third-control-null.mpp":
        "5B5759A6179D04C7580B783E18DFB34D5B1A1F3082C868BF325E1A9E10E90432",
    # Wave 34 (W34-0.5): the synthetic save pairs for the five paths whose
    # Project evidence was corpus-only -- Insert Task, Delete Task, link ADD,
    # custom-field SET and CLEAR -- with each control's Project no-edit save.
    # Measured by scripts/oracle_w34_task_ops.py and
    # scripts/oracle_w34_null_saves.py under C:/w34.
    "tests/data/wave34-insert-task-control.mpp":
        "C77B3F4ECBA2D33414BB259EFCFC6E75B0227C77EDF2F55AAB0BA25D267FC2F6",
    "tests/data/wave34-insert-task.mpp":
        "8A24408630A0573C3DD98EE8C9B07F8A07A7C5846D7479134E62974F9A7F13BD",
    "tests/data/wave34-insert-task-control-null.mpp":
        "9EE74F5DCA4717D4C60622B62AC04F8D95190559D031BB37957BB55E9A237F78",
    "tests/data/wave34-delete-task-control.mpp":
        "BF65481C2A51591EE55649FE64DC348880FD14E92B53DFF8FED77420DFA8A986",
    "tests/data/wave34-delete-task.mpp":
        "A255CE636B4BC021BE7E8A6FFDF4479E882D99D79F97A5280BE24D57817B9B96",
    "tests/data/wave34-delete-task-control-null.mpp":
        "E1E05382C0679BF80C30F749315CB5829668EE0EC67C5508A2C84BEACAF091C0",
    "tests/data/wave34-link-add-control.mpp":
        "247A57F1391A221D28E12BCC7F64E529859BB6437AD923B78DED2E8D30B1E88C",
    "tests/data/wave34-link-add.mpp":
        "F21BFA3BEDBED80F20E074709F71CEF34801E050E870AD5798BDD7E6BC493703",
    "tests/data/wave34-link-add-control-null.mpp":
        "DF5B85E8B83FB3F86B7D18DA3F9A0383FFC645ED572EED6373A42413244A2E88",
    "tests/data/wave34-field-set-control.mpp":
        "FD52CA1F9225F5107A00CFBABAF1B84BC98B24D10D725EF2BD05FEEDDF144974",
    "tests/data/wave34-field-set.mpp":
        "A48D824D0BCDEA68BE7B4EE6920DA565612C7AED1C1CB0AB39D8131A7B6B550A",
    "tests/data/wave34-field-set-control-null.mpp":
        "E2CBC929DE989F4E8CE1E362CC14C3A54C1F736F41A214F179C928C647C69C5C",
    "tests/data/wave34-field-clear-control.mpp":
        "502468FF805C37EB1959AEC3979CAA4C68728C6A20EBDA1A5E3E87A2962931F5",
    "tests/data/wave34-field-clear.mpp":
        "329C3B4EA8604AA42814C64EE27E5C49EFDC215AFD07AB199D00AF668469F54F",
    "tests/data/wave34-field-clear-control-null.mpp":
        "8A115D64F5E330D416E92BC44BC1BF46E95EEE4EA65BB53B1C9F69D70BA5D978",
    # Wave 31 (W31-4.1): numbers-only Project 2021 measurements of the Task
    # Usage and Resource Usage sheets -- stored details/tiers, the reopened
    # COM facts, daily TimeScaleData over 5-9 January 2026, the logical row
    # order, the stored TableFields widths and the measured pixel geometry.
    # No capture or name.
    "tests/data/reference/wave31-usage.measures.json":
        "A2312B41E0C32D97240A98E225329B4CFB3653FAC144F1B941EFEE10BDA10C7C",
    # Wave 31 (W31-2.2): numbers-only Project 2021 measurements of the View
    # tab's Zoom commands -- Entire Project and Selected Tasks on the two
    # committed fixtures, plus the Zoom dialog's week/month presets.  Measured
    # by scripts/oracle_w31_zoom.py (pure COM) and the local UI Automation
    # driver; the windows, the 85 % fit rule and the origin-flooring rule are
    # recorded, no capture or name.
    "tests/data/reference/w31-zoom.measures.json":
        "C660012035C1FF922D82C35A33DE224F8DA781ED4B426A86E3E38206A8D111B5",
    # Wave 27 (W27-3.3): the SAME Project 2021 session's save of
    # wave23-outline.mpp before and after Task > Unlink Tasks on the one task
    # with both a predecessor and a successor (uid 4: 3 -> 4 FS+2 d and
    # 4 -> 6 FF), with neutral metadata, built by the committed
    # scripts/oracle_w27_unlink.py.  Project removes BOTH links touching the
    # task (TaskDependencies 3 -> 1; 3 -> 5 SS+1 d survives; still 1 after
    # reopen).  The link FixedData stays byte-identical, FixedMeta tombstones
    # entries 0 and 2 and sets header flags 4 -> 6.  The unlinked file is the
    # oracle the port's Unlink save is compared with.
    # Wave 28 (W28-0.1): re-derived in one session by
    # scripts/oracle_w28_privacy.py under C:/w28; the wave-27 saves embedded
    # the operator's local save path.  The control's three TBkndCons streams
    # still equal wave23-outline's, and the unlink save still changes only
    # FixedMeta's tombstones and header flags.
    "tests/data/wave27-unlink-control.mpp":
        "55CF09C7EAE1CD29E8A707394C1F3E49D065E368E6AF4FF113BB071A9D91C171",
    "tests/data/wave27-unlink.mpp":
        "83DFA7677E75CEE5F4810F07E86149E372BA71FEB0D57BE4C84D23B27E6DB178",
    # Wave 26 (W26-3.1): the Gantt view's style record and the 214/Props
    # FONT_BASES payload, extracted read-only from tests/data/synthetic-base.mpp
    # (its first decodable Gantt view, "Gantt &with Timeline"; the W26-3.1
    # fixture commit message said "view 2, &Gantt Chart" -- the errata is in
    # wave26-complete.md).  Project 2021's own MPP14 bytes; the blobs carry no
    # path, host name or task text (checked by the name/path scan).  The
    # decoder's offsets are pinned by scripts/oracle_w26.py's one-change-per-save
    # session (build/w26/g3/), so these are a real file's bytes, not synthetic.
    "tests/data/synthetic-gantt-style.bin":
        "B9FD3240CCB0EBD7B94AF17824C1FE104F44BC860AD4DCE093AF7BFD75B23CF4",
    "tests/data/synthetic-gantt-fonts.bin":
        "BEC0B057A37AE959A123BBC28C052366C6134DCC7DF6C5DE308FA897C568F478",
    # Wave 27 (W27-2.1): the wave-26 one-change-per-save evidence, extracted
    # from Project 2021's own saves of tests/data/synthetic-base.mpp (the
    # applied view's `214/CV_iew` main block, record 0x22400008).  The saves
    # were made by the local, git-ignored scripts under build/w26/g3
    # (oracle_styles.py, oracle_probe.py, oracle_probe3.py); each blob is the
    # record of one save, and control.style.bin is the first session's unedited
    # save.  The two font blobs are the `214/Props` FONT_BASES payload of the
    # font-family and font-size saves; every other save's payload is
    # byte-identical to synthetic-gantt-fonts.bin.  A record carries no path,
    # author, project title or task text (only Project's stock bar-style names
    # and font families), so no metadata needed neutralising.  The source .mpp
    # hashes are in tests/data/README.md; the .mpp files stay local.
    "tests/data/wave26-gantt-saves/control.style.bin":
        "37A84A7963DD671CEADF9CFE2DAB06D1CBD798F00A0614420B9238418E849686",
    "tests/data/wave26-gantt-saves/arial.style.bin":
        "D5E493B8AB72EE8DAFA2FBA0006A8F4F734886167D59D343FAE32253CE26C98C",
    "tests/data/wave26-gantt-saves/arial.fonts.bin":
        "3CBE0D5459C1E30B5F37552D13669064768523C598003CBFFE7FA504C0C29A16",
    "tests/data/wave26-gantt-saves/size8.style.bin":
        "43E14459F7D37FEFC56C36B065D66CA4705C868508AC1DF3C7AB78CADB39E88B",
    "tests/data/wave26-gantt-saves/size8.fonts.bin":
        "DFDD77C6D17133401510414B75AE175244A4C24D5E9111CBCFED074796D8210A",
    "tests/data/wave26-gantt-saves/critical-blue.style.bin":
        "FAF026BB99504B7A8C20A9504FDB4740C5C5FA395A03E0BF84C6402C0FEC3A0E",
    "tests/data/wave26-gantt-saves/milestone-italic.style.bin":
        "B3C4F76B11244AF4DEB7D18DDB6D4C1E58C9D89ED7E5BA6F57A749BB6521620E",
    "tests/data/wave26-gantt-saves/milestone-bold.style.bin":
        "873EDE49F8C21681265B5DCC53B2B221CA13DFF4FED716A76014E4EFDFFED722",
    "tests/data/wave26-gantt-saves/milestone-underline.style.bin":
        "3E250AF7329D9BA3E843BF0A21E1F89BBE237A60ABE2789A3A2D3B1F9D1174B9",
    "tests/data/wave26-gantt-saves/highlight-fill.style.bin":
        "DECD741A59CAE93EFE5027B1B1FA2FCE1D950C517E521C1C2C07136C900A308A",
    "tests/data/wave26-gantt-saves/task-red.style.bin":
        "9CA275EA9945F362E3B369B491F05799E07E215FA70A82430AB0E9D40CAD5610",
    "tests/data/wave26-gantt-saves/summary-green.style.bin":
        "42C316FCFED265E4703846373C75502747E4BB142FE6A49DC426D34E906A3BAB",
    "tests/data/wave26-gantt-saves/milestone-blue.style.bin":
        "C13550BC657B240198B88CDFEFCCF89A940AEAE61E0187C601BA091E78513C39",
    "tests/data/wave26-gantt-saves/ts-yqm.style.bin":
        "24332C5A66BDEA6A101E17FB6EC32471362F98DF08DE3D734696ECA323D61E6B",
    "tests/data/wave26-gantt-saves/ts-ymd.style.bin":
        "E13B487DDAF942C3DB40E7FF98EA61926765C234CFB3DBAEFEA4F2F9768DEC3C",
    "tests/data/wave26-gantt-saves/ts-yhq.style.bin":
        "78C8D721A02628517821FC4A88F4F6F1DF4F2A5250B63B16BF5A4991690E0A43",
}


# W28-0.1: a committed fixture must not carry a local absolute path.  Project
# records the save path in every save (`Props14` and the `214/CEdl/Var2Data`
# entry, UTF-16LE), and a save made in the working tree therefore named the
# operator and the company Dropbox folder.  The six wave-23/27 fixtures were
# re-derived under C:/w28 and the wave-19 calendar fixture's two records were
# edited in place, so every Project record now names only a neutral oracle
# root.  Those roots are the ONE allowed form: `C:\wNN\...` (any NN, either
# separator, with or without a trailing separator).  Any other drive path --
# and any user-profile path, whichever drive -- fails.
#
# Both encodings are searched: UTF-16LE as a byte sequence (this finds a
# string wherever it starts in the file, i.e. at either byte alignment) and
# raw ASCII.  The stock `gbui://mainpage.htm` record contains "i:/" but is not
# a drive path, hence the look-behind for a preceding alphanumeric; in UTF-16LE
# that look-behind is the two-byte pair `[A-Za-z]\0`.
# The profile-directory fragments are assembled from escaped bytes so that
# this file does not carry the literals it searches for: the W33-4.4 marker
# guard (scripts/check_markers.py) scans every tracked file, this one too.
# The UTF-16 alternatives are regex-escaped; R28-5/R29-11 recorded that the
# old raw join let the first backslashes be read as regex escapes, so those
# branches could never match.
_PROFILE_TEXT = (
    b"\x5cUsers\x5c",                      # Windows profile directory
    b"\x2fUsers\x2f",                      # POSIX profile directory
    b"\x2fhome\x2f",                       # POSIX home
    b"\x5cDocuments\x20and\x20Settings\x5c",  # pre-Vista profile directory
)
PROFILE_BYTES = re.compile(b"|".join(re.escape(item) for item in _PROFILE_TEXT))
PROFILE_UTF16 = re.compile(b"|".join(
    re.escape(item.decode("latin-1").encode("utf-16le"))
    for item in _PROFILE_TEXT))

NEUTRAL_ROOT = re.compile(r"^[Cc]:[\\/]w\d+(?:[\\/]|$)")
LOCAL_PATH_UTF16 = re.compile(
    rb"(?<![A-Za-z0-9]\x00)[A-Za-z]\x00:\x00[\\/]\x00")
LOCAL_PATH_BYTES = re.compile(rb"(?<![A-Za-z0-9])[A-Za-z]:[\\/]")


def _path_token(data, at, wide):
    """The path-like token containing byte offset `at` (quotes/NUL delimit)."""
    path_chars = ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                  "0123456789:\\/._-~$ ")

    def path_char(value):
        return 0x20 <= value < 0x7F and chr(value) in path_chars

    if wide:
        start = at
        while start >= 2 and data[start - 1] == 0 and path_char(data[start - 2]):
            start -= 2
        end = at
        while end + 1 < len(data) and data[end + 1] == 0 and path_char(data[end]):
            end += 2
        return data[start:end].decode("utf-16le", "replace")
    start = at
    while start >= 1 and path_char(data[start - 1]):
        start -= 1
    end = at
    while end < len(data) and path_char(data[end]):
        end += 1
    return data[start:end].decode("latin-1")


def _path_hits(data, rel):
    hits = []
    for regex, kind, wide in ((LOCAL_PATH_UTF16, "drive path", True),
                              (PROFILE_UTF16, "user-profile path", True),
                              (LOCAL_PATH_BYTES, "drive path", False),
                              (PROFILE_BYTES, "user-profile path", False)):
        for match in regex.finditer(data):
            at = match.start()
            value = _path_token(data, at, wide)
            if kind == "user-profile path" or not NEUTRAL_ROOT.match(value):
                hits.append((rel, kind, at, value))
    return hits


def check_fixture_paths():
    """W28-0.1: no committed fixture may embed a local absolute path."""
    failures = []
    tracked = git("ls-files", "tests/data").stdout.split()
    for rel in tracked:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        with open(path, "rb") as handle:
            data = handle.read()
        for rel_name, kind, offset, value in _path_hits(data, rel):
            # The value is redacted: this message can land in a public CI log.
            failures.append(f"{rel_name}: {kind} at byte {offset}: "
                            f"{check_markers.redact(value)!r}")
    return failures, len(tracked)


# ---------------------------------------------------------------------------
# W33-4.4 (audit B-2/B-4): the property sets of EVERY tracked .mpp.
#
# The path scan above looks at tests/data only, and `analysis/test_project.mpp`
# is tracked outside it.  The B-2 audit opened the SummaryInformation and
# DocumentSummaryInformation streams by hand; this makes the same read a gate.
# A CFB v3 reader is enough: W33-2.3 refuses v4 at open, and every tracked
# fixture is v3 (the writer's own check).  Values are tested against the same
# marker list as scripts/check_markers.py; a hit prints the property name and
# never the value.
# ---------------------------------------------------------------------------

_CFB_MAGIC = b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1"
_ENDOFCHAIN = 0xFFFFFFFE
_FREESECT = 0xFFFFFFFF
_MAXREGSECT = 0xFFFFFFFA

_SI_PROPERTY_NAMES = {2: "Title", 3: "Subject", 4: "Author", 5: "Keywords",
                      6: "Comments", 7: "Template", 8: "LastAuthor",
                      9: "RevNumber", 18: "AppName"}
_DSI_PROPERTY_NAMES = {2: "Category", 14: "Manager", 15: "Company"}


def _cfb_sector(data, size, sid):
    offset = (sid + 1) * size
    return data[offset:offset + size]


def _cfb_chain(data, size, table, start):
    parts = []
    seen = set()
    sid = start
    while (sid not in (_ENDOFCHAIN, _FREESECT) and sid < len(table)
           and sid not in seen):
        seen.add(sid)
        parts.append(_cfb_sector(data, size, sid))
        sid = table[sid]
    return b"".join(parts)


def _cfb_streams(data):
    """{name: bytes} for the two property-set streams of a v3 CFB file."""
    if len(data) < 0x4C + 436 or data[:8] != _CFB_MAGIC:
        return {}
    shift = struct.unpack_from("<H", data, 0x1E)[0]
    if shift != 9:
        return {}
    size = 1 << shift
    (_n_fat, dir_start, _tx, mini_cutoff, minifat_start, _n_minifat,
     difat_start, n_difat) = struct.unpack_from("<8I", data, 0x2C)
    difat = list(struct.unpack_from("<109I", data, 0x4C))
    sid = difat_start
    seen = set()
    # The same bounds the writer gained in W33-2.1: the declared count may not
    # exceed the file's sectors and a continuation sector is visited once.
    for _ in range(min(n_difat, len(data) // size + 1)):
        if sid in (_ENDOFCHAIN, _FREESECT) or sid >= _MAXREGSECT or sid in seen:
            break
        seen.add(sid)
        sector = _cfb_sector(data, size, sid)
        if len(sector) < size:
            break
        values = struct.unpack_from("<%dI" % (size // 4), sector)
        difat.extend(values[:-1])
        sid = values[-1]
    fat = []
    for sid in difat:
        if sid >= _MAXREGSECT:
            continue
        sector = _cfb_sector(data, size, sid)
        if len(sector) < size:
            continue
        fat.extend(struct.unpack_from("<%dI" % (size // 4), sector))
    directory = _cfb_chain(data, size, fat, dir_start)
    entries = []
    for offset in range(0, len(directory) - 127, 128):
        entry = directory[offset:offset + 128]
        name_len = struct.unpack_from("<H", entry, 0x40)[0]
        name = (entry[:max(0, name_len - 2)].decode("utf-16-le", "replace")
                if name_len >= 2 else "")
        etype = entry[0x42]
        start, length = struct.unpack_from("<IQ", entry, 0x74)
        entries.append((name, etype, start, length & 0xFFFFFFFF))
    if not entries:
        return {}
    root = entries[0]
    mini = (_cfb_chain(data, size, fat, root[2])[:root[3]]
            if root[2] not in (_ENDOFCHAIN, _FREESECT) else b"")
    minifat = []
    if minifat_start not in (_ENDOFCHAIN, _FREESECT):
        raw = _cfb_chain(data, size, fat, minifat_start)
        minifat = list(struct.unpack_from("<%dI" % (len(raw) // 4), raw))
    mini_size = 1 << struct.unpack_from("<H", data, 0x20)[0]
    streams = {}
    for name, etype, start, length in entries:
        # A property-set stream is conventionally named with a leading 0x05
        # byte; some writers omit it, so accept both spellings.
        plain = name[1:] if name.startswith("\x05") else name
        if etype != 2 or plain not in ("SummaryInformation",
                                       "DocumentSummaryInformation"):
            continue
        if length < mini_cutoff:
            parts = []
            seen = set()
            sid = start
            while (sid not in (_ENDOFCHAIN, _FREESECT)
                   and sid < len(minifat) and sid not in seen):
                seen.add(sid)
                offset = sid * mini_size
                parts.append(mini[offset:offset + mini_size])
                sid = minifat[sid]
            streams[plain] = b"".join(parts)[:length]
        else:
            streams[plain] = _cfb_chain(data, size, fat, start)[:length]
    return streams


def _property_values(blob):
    """[(section, property id, decoded text)] for every string property."""
    values = []
    if not blob or len(blob) < 48:
        return values
    try:
        count = struct.unpack_from("<I", blob, 24)[0]
        for section_index in range(min(count, 4)):
            base = 28 + 20 * section_index
            if base + 16 > len(blob):
                break
            section = struct.unpack_from("<I", blob, base + 16)[0]
            if section + 8 > len(blob):
                continue
            _section_size, props = struct.unpack_from("<II", blob, section)
            entries = []
            for index in range(props):
                entry = section + 8 + 8 * index
                if entry + 8 > len(blob):
                    break
                pid, offset = struct.unpack_from("<II", blob, entry)
                entries.append((pid, section + offset))
            codepage = 1252
            for pid, at in entries:
                if (pid == 1 and at + 6 <= len(blob)
                        and struct.unpack_from("<I", blob, at)[0] & 0xFFFF == 2):
                    codepage = struct.unpack_from("<H", blob, at + 4)[0] or 1252
            for pid, at in entries:
                if at + 8 > len(blob):
                    continue
                vtype = struct.unpack_from("<I", blob, at)[0] & 0xFFFF
                length = struct.unpack_from("<I", blob, at + 4)[0]
                if vtype == 0x1E:      # VT_LPSTR, in the section's codepage
                    raw = blob[at + 8:at + 8 + length]
                    values.append((section_index, pid, raw.split(
                        b"\x00", 1)[0].decode("cp%d" % codepage, "replace")))
                elif vtype == 0x1F:    # VT_LPWSTR
                    raw = blob[at + 8:at + 8 + 2 * length]
                    values.append((section_index, pid, raw.decode(
                        "utf-16-le", "replace").split("\x00", 1)[0]))
    except struct.error:
        pass
    return values


def check_tracked_mpp_properties():
    """Every tracked .mpp/.mpt: no marker in a property set, no local path."""
    failures = []
    tracked = git("ls-files", "*.mpp", "*.mpt").stdout.split()
    for rel in tracked:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            failures.append(f"MISSING {rel}")
            continue
        with open(path, "rb") as handle:
            data = handle.read()
        streams = _cfb_streams(data)
        if not streams:
            failures.append(f"{rel}: no readable CFB v3 property sets")
        for stream, names in (("SummaryInformation", _SI_PROPERTY_NAMES),
                              ("DocumentSummaryInformation",
                               _DSI_PROPERTY_NAMES)):
            for section, pid, value in _property_values(streams.get(stream)):
                hit = check_markers.find_markers_in_text(value)
                if not hit:
                    continue
                prop = (names.get(pid, f"property {pid}") if section == 0
                        else f"custom property {pid}")
                failures.append(f"{rel}: {stream} {prop} carries "
                                f"{' '.join(hit)}")
        for rel_name, kind, offset, _value in _path_hits(data, rel):
            failures.append(f"{rel_name}: {kind} at byte {offset}")
    return failures, len(tracked)


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                          text=True, encoding="utf-8", errors="replace")


def sha256(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


SCENARIO_DIR = os.path.join(ROOT, "tests", "data", "scenarios")


def _oracle_body(text):
    """The oracle's measured body, canonicalised: everything but provenance."""
    import hashlib
    import json
    data = json.loads(text)
    data.pop("provenance", None)
    canonical = json.dumps(data, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _last_body_commit(rel):
    """The newest commit whose measured body differs from its predecessor's."""
    # --follow, per the brief: a renamed oracle must keep the commit that last
    # changed its body, not restart at the rename.
    commits = git("log", "--follow", "--format=%h", "--", rel).stdout.split()
    previous = None
    previous_sha = None
    for sha in commits:
        text = git("show", f"{sha}:{rel}").stdout
        if not text:
            continue
        body = _oracle_body(text)
        if previous is not None and body != previous:
            # `previous_sha` is the NEWER commit of the pair, so it is the one
            # whose body differs from its parent's.
            return previous_sha
        previous = body
        previous_sha = sha
    return commits[-1] if commits else None


def check_oracle_provenance():
    """W25-6/S7: provenance.recording_commit must be the last BODY change.

    The wave-24 provenance named the commit that introduced six oracles and one
    more for task-calendar, but every oracle's measured body was re-recorded by
    e91f19b.  This reconstructs the answer from git (a provenance-only edit does
    not count as a body change) and requires the JSON to hold it.
    """
    import glob
    import json
    import os as _os
    failures = []
    checked = 0
    for path in sorted(glob.glob(_os.path.join(SCENARIO_DIR, "*.oracle.json"))):
        rel = _os.path.relpath(path, ROOT).replace("\\", "/")
        try:
            with open(path, encoding="utf-8") as handle:
                data = json.load(handle)
        except (OSError, ValueError) as exc:               # noqa: BLE001
            failures.append(f"{rel}: unreadable oracle ({exc})")
            continue
        provenance = data.get("provenance") or {}
        claimed = str(provenance.get("recording_commit") or "")
        actual = _last_body_commit(rel) or ""
        if not claimed:
            failures.append(f"{rel}: provenance has no recording_commit")
            continue
        if not actual.startswith(claimed) and not claimed.startswith(actual):
            failures.append(
                f"{rel}: recording_commit {claimed} is not the last body change "
                f"({actual})")
            continue
        if not actual:
            # A staged-but-never-committed oracle has no history to verify; a
            # claimed commit would otherwise pass the empty-prefix test.
            failures.append(
                f"{rel}: not committed; its recording_commit cannot be verified")
            continue
        recorded = git("show", f"{actual}:{rel}").stdout
        if not recorded:
            failures.append(f"{rel}: {actual} does not contain the oracle")
            continue
        if _oracle_body(recorded) != _oracle_body(open(path, encoding="utf-8").read()):
            failures.append(
                f"{rel}: working body differs from the body recorded at {actual} "
                f"(re-record or update provenance)")
            continue
        checked += 1
    return failures, checked


def main():
    failures = []

    for rel, want in DOCUMENTED.items():
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            failures.append(f"MISSING {rel}")
            continue
        got = sha256(path)
        if got != want:
            failures.append(f"SHA256 {rel}\n  want {want}\n  got  {got}")

    # Every tracked .mpp must match HEAD byte-for-byte.
    tracked = git("ls-files", "*.mpp").stdout.split()
    for rel in tracked:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            failures.append(f"MISSING {rel}")
            continue
        # A fixture staged for its FIRST commit is in the index but not in
        # HEAD; its documented SHA256 above is the check, and the HEAD
        # comparison starts applying on the next run.
        resolved = git("rev-parse", "--verify", "-q", f"HEAD:{rel}")
        head = resolved.stdout.strip()
        if resolved.returncode != 0 or not head:
            continue
        work = git("hash-object", "--", rel).stdout.strip()
        if work != head:
            failures.append(f"differs from HEAD: {rel} (work {work[:12]} head {head[:12]})")

    provenance_failures, oracles = check_oracle_provenance()
    failures.extend(provenance_failures)

    path_failures, scanned = check_fixture_paths()
    failures.extend(path_failures)

    property_failures, mpp_scanned = check_tracked_mpp_properties()
    failures.extend(property_failures)

    if failures:
        print("FIXTURE CHECK FAILED")
        for f in failures:
            print(f"  {f}")
        print("\nRestore with: Remove-Item <file>; git checkout HEAD -- <file>")
        return 1
    print(f"fixtures OK ({len(DOCUMENTED)} documented hashes, {len(tracked)} tracked .mpp)")
    print(f"fixture paths OK ({scanned} tracked test-data files, no local path)")
    print(f"tracked .mpp property sets OK ({mpp_scanned} tracked .mpp/.mpt, "
          f"no marker, no local path)")
    print(f"scenario oracle provenance OK ({oracles} oracle(s), recording_commit = "
          f"last measured-body change)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
