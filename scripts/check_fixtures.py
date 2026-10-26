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
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Fixtures whose exact bytes are documented (SHA256, from docs/handoff/wave14-complete.md).
DOCUMENTED = {
    "tests/data/synthetic-base.mpp":
        "55C109F2F62FD6B9F05789AB002C35991A424AAB18C78F92E4200DF7B3FC6069",
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
    # task.  No client content.
    "tests/data/wave23-outline.mpp":
        "055721FF07E93F8C8C06B8428E9EF3495D10A38BE8C432506FCB5FF3A786DA20",
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
def _u16(text):
    return text.encode("utf-16le")


NEUTRAL_ROOT = re.compile(r"^[Cc]:[\\/]w\d+(?:[\\/]|$)")
LOCAL_PATH_UTF16 = re.compile(
    rb"(?<![A-Za-z0-9]\x00)[A-Za-z]\x00:\x00[\\/]\x00")
LOCAL_PATH_BYTES = re.compile(rb"(?<![A-Za-z0-9])[A-Za-z]:[\\/]")
PROFILE_UTF16 = re.compile(b"|".join(_u16(text) for text in (
    "\\Users\\", "/Users/", "/home/", "\\Documents and Settings\\")))
PROFILE_BYTES = re.compile(
    rb"\\Users\\|/Users/|/home/|\\Documents and Settings\\")


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
            failures.append(f"{rel_name}: {kind} at byte {offset}: {value!r}")
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

    if failures:
        print("FIXTURE CHECK FAILED")
        for f in failures:
            print(f"  {f}")
        print("\nRestore with: Remove-Item <file>; git checkout HEAD -- <file>")
        return 1
    print(f"fixtures OK ({len(DOCUMENTED)} documented hashes, {len(tracked)} tracked .mpp)")
    print(f"fixture paths OK ({scanned} tracked test-data files, no local path)")
    print(f"scenario oracle provenance OK ({oracles} oracle(s), recording_commit = "
          f"last measured-body change)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
