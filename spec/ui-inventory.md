# UI inventory — what MS Project's own resources specify

_Stream W4. Revised 2026-10-22. Inputs are the git-ignored dumps in
`analysis/res/pjintl/` from PJINTL.DLL (UIFILE 71, RT_DIALOG 11, RT_STRING 2,
RT_RCDATA 1,071, RT_BITMAP 24, RT_ICON, RT_GROUP_ICON, RT_HTML 86,
RT_VERSION, OOBREPORT 26) and `analysis/res/winproj/` (1,829 bitmaps,
221 icons, 112 RCDATA, 20 RAWDATA, 5 WSDL, 5 XSD)._

Everything below is stated with its evidence: a byte offset, a hex dump, an
`index.tsv` row, or a re-runnable command. Where a statement is an inference
it says so. **Nothing here was observed in the running application** — no
oracle was used (see `docs/coordination.md` Revision 3).

Short version: **the resources give an unusually complete vocabulary of the
modern (Office-style) UI and a complete decode of 11 legacy dialogs, but
almost no layout. The core Task Sheet / Gantt / Usage / Calendar geometry is
not in these resources at all.** Details in §8.

## 1. How to reproduce everything here

```powershell
cd "D:\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile"
python scripts/uifile_scan.py       --tsv docs/analysis/ui/uifile_scan.tsv
python scripts/uifile_inventory.py  --tsv docs/analysis/ui/uifile_inventory.tsv
python scripts/uifile_dialog.py     --tsv docs/analysis/ui/dialogs.tsv
python scripts/uifile_rcdata.py
```

Outputs: `docs/analysis/ui/uifile_scan.{txt,tsv}` (header fields, entropy,
names), `uifile_inventory.tsv` (71 rows), `dialogs.tsv` (171 control rows),
`rcdata_scan.tsv` and `rcdata_strings.tsv` (19,792 strings). The scripts are
read-only over the dumps.

## 2. UIFILE container

`index.tsv` rows 28–98 list all 71 UIFILEs, ids 4724–4843, sizes 83 B to
109,527 B (343,064 B total). Every one starts:

```
02 10 4e 55 42 00 0c 00 00 00 01 03 <flag> <u32 size> ...
```

Full dump of the smallest, `UIFILE/4767_1033.bin` (83 bytes; `index.tsv:40`):

```
0000  02 10 4e 55 42 00 0c 00 00 00 01 03 00 53 00 00  ..NUB........S..
0010  00 18 09 00 00 00 00 13 00 1b 16 39 00 00 00 00  ...........9....
0020  13 00 1b 4c 44 58 46 00 0f 46 69 6c 74 65 72 4d  ...LDXF..FilterM
0030  65 6e 75 47 72 6f 75 70 04 00 13 00 1b 00 01 01  enuGroup........
0040  00 05 49 74 65 6d 73 01 04 19 01 00 00 01 00 00  ..Items.........
0050  01 00 00
```

Header layout (all offsets absolute in the blob):

| off | type | value | evidence |
|---|---|---|---|
| 0x00 | u16 | `0x1002` (bytes `02 10`) | all 71 |
| 0x02 | char[4] | `"NUB\0"` | all 71; `NUB` is opaque — no string `"NUB"` occurs in the decompiled C (grep, §10) |
| 0x06 | u32 | always `12` | all 71 |
| 0x0a | u16 | always `0x0301` | all 71 |
| 0x0c | u8 | `0` or `1` | 0: 61 blobs, 1: 10 blobs |
| 0x0d | u32 | **file size in bytes** | verified 71/71; 4767 = `0x53` = 83 |
| 0x11 | — | payload start | variable; see below |

The `u32 size` check is the one hard fact about the container: it is the only
field that is provably the blob length (`scripts/uifile_scan.py` asserts it
for every blob and prints `size field mismatches: 0`). The remaining header
bytes at 0x11–0x1a differ per file and are **not decoded**; the two families
(`... 18 09 00 00 00 00 <id> 00 1b` vs `... 02 xx xx 00 00 01 ...`) are noted
in `docs/analysis/ui/uifile_scan.txt` but no semantic claim is made.

### 2.1 The payload is a name pool followed by code

There are two payload encodings, both starting immediately after the header.

**(a) Flat string pool.** Tokens are `0x01 <u16 LE length> <ASCII>`.
`UIFILE/4774_1033.bin` starts at 0x1a:

```
0000  02 10 4e 55 42 00 0c 00 00 00 01 03 00 1c 06 00
0010  00 02 81 02 00 00 01 2f 01 06 00 38 38 38 38 38
0020  38 01 14 00 58 58 ... 01 06 00 52 65 70 6f 72 74   "888888" "XXXX…" "Report"
0040  01 0b 00 23 44 69 61 6c 6f 67 56 69 65 77           "#DialogView"
0050  01 0a 00 44 69 61 6c 6f 67 56 69 65 77              "DialogView"
```

So: `01 | len | bytes`, repeated. In 4774 the pool runs 0x1a–0x292
(44 names) and at 0x292 the bytes stop looking like text (`02a0  0d 0e 26 27
28 29 05 06 …`). That second region is a binary/opcode stream (repeated
`49` bytes, small-int operands) and is **not decoded**. 4724, 4751, 4752,
4763, 4771, 4773, 4776, 4778, 4780, 4794, 4798, 4802, 4806, 4808, 4811,
4812, 4816, 4826, 4834, 4836, 4842 and the LDXF files all end in this kind
of stream.

**(b) LDXF class record.** Some payloads open with the literal `LDXF\0`
followed by a u8 name length and a class name, then a member list. In 4767
the tag is at 0x23, the class at 0x29 (`FilterMenuGroup`, len `0f` at 0x28),
then member `Items` (len byte 0x05 at 0x41). 4760 (`FilterBooleanChoice`)
shows three members in the same shape:

```
0030  6f 6f 6c 65 61 6e 43 68 6f 69 63 65 04 00 0d 00  ooleanChoice....
0040  1b 00 01 03 00 05 4c 61 62 65 6c 01 04 07 01 00  ......Label.....
0050  00 00 0a 4d 65 6e 75 49 74 65 6d 49 64 01 04 01  ...MenuItemId...
0060  01 00 00 00 12 46 69 6c 74 65 72 42 6f 6f 6c 65  .....FilterBoole
0070  61 6e 56 61 6c 75 65 01 01 03 01 00 00 01 00 00  anValue.........
```

The member descriptors (`01 04 07 01 00 00 00`) are consistent per member
across files but their field meaning is unresolved. What *is* recoverable
reliably is the class name and the member-name list.

**(c) Embedded Lua 5.4.** Five blobs contain a Lua 5.4 chunk header
`1b 4c 75 61 54 00 19 93 0d 0a` — 4838 and 4840 at offset 26, 4826 at 1,276,
4811 at 6,545, 4750 at 10,658. For 4838 the chunk starts at 0x1a, i.e. the
whole resource is essentially a script. `4811` (Board view) contains Lua
source comments such as `Scroll Column Headers` (0x197a) and
`Command)Change percent complete label to text box` (0x33ff), and font names
(`Segoe UI Semibold` at 0x5d9). These are developer text, not UI labels.

### 2.2 What UIFILE does *not* contain

- **No localised strings.** A regex for UTF-16LE printable runs over all 71
  blobs produces only false positives (`-I/I2`, `ABDRTXY1`) plus the Lua
  font/comment text above. The visible text is not here; the identifiers are
  symbolic keys (`PercentCompleteLabel`, `TitleEnterString`, …).
- **No decoded geometry.** Names such as `DefaultWidth`, `DefWidth`,
  `DefaultHeight`, `MinWidth`, `MinHeight`, `Windows`, `Columns` exist (4774,
  4806, 4810) but their values sit in the undecoded value/opcode stream.
- **No control ids.** The word "id" appears in `HelpId`, `GroupId`,
  `TaskID`, but there is no numeric id table recoverable at this layer.

## 3. UIFILE inventory

Full machine-readable list: `docs/analysis/ui/uifile_inventory.tsv`
(rid, bytes, kind, class, name count, has DialogView, Lua offset, surface,
confidence, all recovered names). Table below condenses it. **"Surface" is an
inference from the class name and the identifier vocabulary, not a resource
field**; confidence is high when the LDXF class names the surface, medium
when the identifier vocabulary does, low when it is guesswork.

| rid | bytes | class / kind | inferred surface | conf |
|---|---|---|---|---|
| 4724 | 109527 | pool | feature/audience config (`Audience::Production`, `Microsoft.Project.Project`) | low |
| 4750 | 33450 | pool + Lua | Backstage / File view (`OfficeCenterView`) | medium |
| 4751 | 2168 | LDXF `OptionsDoc` | Options / calculation settings | high |
| 4752 | 3779 | LDXF `OptionsGlobal` | Options / global settings | high |
| 4753 | 374 | LDXF `OptionsGenericList` | Options list control | high |
| 4760–4770 | 131–190 | LDXF `Filter*` (10 classes) | filter menu / filter dialog parts | high |
| 4763 | 5012 | pool | filter UI | high |
| 4771 | 3018 | pool | filter UI | high |
| 4772 | 332 | LDXF `TrustCenter` | Trust Center | high |
| 4773 | 5996 | pool | unidentified (options-like names) | low |
| 4774 | 1564 | pool, DialogView | Backstage gallery / dialog | medium |
| 4775 | 176 | LDXF `ReportsDialog` | Reports dialog | high |
| 4776 | 5714 | pool | filter/report selector | medium |
| 4777 | 1184 | LDXF `ReportPane` | Reports pane (visual report designer) | high |
| 4778 | 838 | pool, DialogView | generic single-select dialog | low |
| 4779 | 160 | LDXF `ReportFieldSettingsDialog` | report field settings | high |
| 4780 | 92324 | pool | Backstage (`BackstageSaveAsButton`, `HomePageNewBlankDocButton`) | high |
| 4781 | 1358 | LDXF `OutDocInfo` | Save As / document properties | medium |
| 4785 | 125 | LDXF `OutSaveAs` | Save As | medium |
| 4786 | 891 | LDXF `OutSpaceWSSSaveSpace` | Save-to-site / SharePoint space | low |
| 4788 | 141 | LDXF `OutDocLocation` | document location | medium |
| 4791 | 894 | LDXF `ProjectPrintSettings` | Page Setup / print settings | high |
| 4793 | 804 | LDXF `ReportFieldItem` | report field item | high |
| 4794 | 1199 | pool | validator / field input | low |
| 4795 | 238 | LDXF `TimePhasedFormat` | time-phased format | low |
| 4798 | 4938 | pool, DialogView | task comments | medium |
| 4799–4805 | 721 each | LDXF `ResourceItemDS` (×5) + 4803 `Inspector…` | resource picker; assignment inspector | medium |
| 4806 | 785 | pool, DialogView | generic dialog shell (width/height/HasOK) | low |
| 4807 | 591 | LDXF `BarLabelDialog` | Gantt bar label dialog | high |
| 4808 | 1618 | pool | generic dialog content | low |
| 4809 | 261 | LDXF `ExternalTask` | external task link UI | low |
| 4810 | 2184 | LDXF `ResourceItem` | resource picker | medium |
| 4811 | 17227 | pool + Lua | Board view card layout (`CardsList` etc.) | high |
| 4812 | 506 | pool | dialog shell | low |
| 4813 | 97 | LDXF `SelectAgileDialog` | board setup dialog | medium |
| 4815–4822 | 893 each | LDXF `GroupDS` (×7) | Group By dialog data source | medium |
| 4816 | 5577 | pool | deliverables/board content | low |
| 4826 | 2367 | pool + Lua, DialogView | Task Board customize / dialog | high |
| 4827–4828 | 315 each | LDXF `TaskBoardCustomizeCards` | Task Board card customization | high |
| 4834 | 1361 | pool, DialogView | generic dialog shell | low |
| 4835 | 214 | LDXF `AlertDialog` | alert dialog template | medium |
| 4836 | 1283 | pool | Compare Projects legend | high |
| 4837 | 133 | LDXF `CompareProjectsLegendPane` | Compare Projects legend pane | high |
| 4838 | 2455 | pool + Lua | Import Resources wizard | high |
| 4839 | 452 | LDXF `ImportResourcesWizardPane` | Import Resources wizard | high |
| 4840 | 2976 | pool + Lua | Import Project wizard | high |
| 4841 | 696 | LDXF `ImportProjectWizardPane` | Import Project wizard | high |
| 4842 | 4975 | pool | Deliverables pane | medium |
| 4843 | 2195 | LDXF `DeliverableItem` | Deliverables pane | medium |

Counts: 48 of 71 carry an `LDXF` class name; 5 embed Lua; 5 contain a
`DialogView` element (4774, 4778, 4798, 4826, 4834); 33 rows are high
confidence, 23 medium, 15 low.

**Labels recovered:** none that a user would read. The useful output is the
symbolic vocabulary — 1,973 distinct identifiers across the 71 blobs, e.g.
`PercentCompleteLabel`, `PercentCompleteEnterString`,
`PercentCompleteAccessibleName`, `TitleEnterString` (4811), `Label`,
`MenuItemId`, `ImageSource` (4762), `DefaultWidth`, `DefWidth`, `Title`,
`HelpId` (4774). These are exactly the keys a localisation layer would map to
RCDATA/RT_STRING entries, but **the key→string binding is not proven** (§9).

## 4. RT_DIALOG — fully decoded (the reliable half)

All 11 dialogs parse as **DLGTEMPLATEEX** and consume their resource exactly
(0 bytes of unexplained padding); `scripts/uifile_dialog.py` reports
`11/11 dialogs parsed with exact consumption`.

The `index.tsv` heads had already given this away:
`01 00 ff ff 00000000 00000000 c400c880 …` = dlgVer 1, signature 0xFFFF,
helpID 0, exStyle 0, style 0x80C800C4 (WS_POPUP|WS_CAPTION|WS_SYSMENU|
DS_MODALFRAME|DS_SETFONT), then cDlgItems/x/y/cx/cy.

| resource | title | items | notes |
|---|---|---|---|
| `TEXT` | Text Styles | 29 | the only Project-specific one: item-to-change combo (id 103), font/style/size, Apply (1026), Help (1038) |
| `FONTWITHBG` | Font | 26 | font + background colour/pattern |
| `32248` | Insert Object | 19 | MFC OLE UI |
| `32249` | Change Icon | 17 | MFC OLE UI |
| `32250` | Convert | 18 | MFC OLE UI |
| `32251` | Paste Special | 18 | MFC OLE UI |
| `32252` | Links | 17 | MFC OLE UI |
| `32254` | Server Busy | 6 | MFC OLE UI |
| `32257` | Change Source | 6 | MFC OLE UI |
| `PROTECTED` | Password | 5 | file-open password |
| `RESERVED` | Password | 9 | write-reserved password |

All use 8 pt Tahoma (DS_SETFONT). Control lists with ids, classes, captions
and geometry are in `docs/analysis/ui/dialogs.tsv`; `uifile_dialog.py --dump`
prints them. Example, `32248` (Insert Object):

```
id=2100 Button  (   6,  18, 67,10) 'Create &New:'
id=2101 Button  (   6,  36, 67,10) 'Create from &File:'
id=2111 Static  (  84,   4,110, 8) 'Object &Type:'
id=2103 ListBox (  82,  16,132,73) ''
id=2106 Button  (  82,  50, 48,14) '&Browse...'
id=2102 Button  ( 140,  52, 40,10) '&Link'
id=   1 Button  ( 222,   6, 66,14) 'OK'
id=   2 Button  ( 222,  24, 66,14) 'Cancel'
```

Every dialog also carries a zero-size hidden static id **12000** with text
`DAL=On` — a marker, not UI.

Blunt point: **these are not Project's dialogs.** They are MFC's
OLE/insert-object templates plus the shared Font and Password dialogs. The
Project dialogs the port needs (Task Information, Resource Information, Bar
Styles, Calendar Working Time, Filter, Group By, Page Setup, Options, …) are
*not* RT_DIALOG resources. Five of them appear to be defined in UIFILE
instead (`DialogView`), but their content/geometry is in the undecoded layer.

## 5. RT_RCDATA — 1,071 blobs, and what they hold

Two populations:

**(a) 442 localised UTF-16 string tables (19,792 strings).** Layout, verified
on 638 and 431:

```
u16 count
count × { u32 id, u32 textOffset, u16 byteLength }   (10 bytes/record)
UTF-16LE text area, absolute file offsets, lengths in bytes
```

`RT_RCDATA/638_1033.bin` (32,820 B) starts:

```
0000  be 02 00 00 00 00 6e 1b 00 00 00 00 01 00 00 00
0010  72 1b 00 00 00 00 02 00 00 00 76 1b 00 00 10 00
0020  03 00 ...
```

count `0x02be` = 702; record 0 at 2: id 0, offset `0x1b6e` = 7022, len 0;
record 2 at 22: id 2, offset `0x1b76` = 7030, len 16 → `&Message`. Index ends
at 2+10·702 = 7022, where the text area begins. `431` (129,324 B) is the
same shape: count 434, first id 0, offset 4342, len 434 → the field-help
sentence beginning "The total amount of time scheduled on a task…".

Notable tables (id, size, first string):

| RCDATA | bytes | strings | first string | inference |
|---|---|---|---|---|
| 1078 | 297,104 | 1,577 | "You have unsaved changes to some engagements." | message/help corpus |
| 14 | 58,372 | 1,153 | "Can't Undo" | menubar/command labels |
| 638 | 32,820 | 702 | "&Message" | menu/command labels |
| 13 | 30,160 | 609 | "Project" | dialog/UI strings |
| 1181 | 109,392 | 511 | "This operation can be performed only on selections…" | error/help text |
| 431 | 129,324 | 434 | "The total amount of time scheduled…" | field help |
| 437/422 | 95,812/80,984 | 310/264 | field descriptions | field help |
| 174/346/173/345 | 6,996/6,284/3,604/3,456 | 344/304/165/157 | "&Entry" | table/menu captions |
| 18 | 5,692 | 193 | "Date" | field/column captions |
| 207 | 4,720 | 177 | "Notes" | field captions |
| 1155 | 4,936 | 168 | "% Complete" | table column captions |
| 700 | 4,644 | 149 | "OK" | common button strings |
| 586 | 19,516 | 152 | "{This is not a valid file name.}…" | validators/messages |
| 177 | 4,936 | 143 | "Default task information" | dialog help |

The `{…}` prefix in 586 (and `%Y0%` in 548) shows these tables carry format
placeholders, i.e. they are the runtime message catalogue, not just labels.
**This is the single richest label source in the product's resources** — but
the id→control binding is unproven (§9).

**(b) 629 opaque blobs.** Grouped by first 8 bytes, 132 distinct signatures.
The big groups: `00000000…` ×202, `01000100` ×39, `03000000` ×29,
`cc000000` ×17 (seventeen blobs of exactly 5,516 B, identical head — almost
certainly one table replicated per view/mode), `07000100` ×15, `02000000`
×15, `00001800` ×14, `02000100` ×13, `04000000` ×12, `03000100` ×12.

412 of the 629 still contain ≥3 UTF-16 runs, so many are *text lists with a
different (non-indexed) header*, e.g. 136 (108 B): `00000000 64000000 100d
1904 0f0f` then UTF-16 `Name / Appearance / Show For … Tasks / Row / From /
To` — a field-picker list. The rest look like command tables: 1–5 begin
`00000000 d6000000 0180ffff 680008ff …`; 73–106 are uniform 4-byte arrays
(`00000001 00000003 00000007 00000008`); 219–235 are the 5,516 B replicas.
**I did not decode a menu, toolbar or accelerator template in this group.**
They are plausibly command-bar/lookup data, but that is an inference and the
one that matters most for "can we specify menus from resources".

## 6. The other resource types (low value, confirmed)

- **RT_STRING** (2 resources). 7 = locale `"1033"`; 36 = `"Microsoft Project
  Standard"`, `"Microsoft Project Professional"`. Nothing else.
- **RT_HTML** (86). Named assets: `ACCEPT.GIF`, `BLACKBAR.GIF`,
  `BLACKTHEME.CSS`, `BLANK.HTM`, `BULLET.GIF`, … These are the HTML help
  images/pages, not UI definitions. Titles may name help topics; not mined.
- **RT_BITMAP** (24) / **RT_ICON** / **RT_GROUP_ICON**. Standard
  BITMAPINFOHEADER (`28 00 00 00`), 4/8/24-bpp, up to 123,240 B. Usable as
  artwork only; the ribbon/toolbar bitmap strip is not here (winproj has
  1,829 bitmaps).
- **OOBREPORT** (26). ZIP (`50 4b 03 04`), report templates — already a
  deliberate non-goal (`docs/scope.md` §6).

## 7. Cross-check: what exists in `winproj` resources

`analysis/res/winproj/index.tsv` has 1,829 bitmaps, 221 icons, 112 RCDATA,
20 RAWDATA, 5 WSDL, 5 XSD. I did not decode these in depth. They are the
right place to look for the *Gantt bar/toolbar artwork* and possibly command
tables; `docs/analysis/ui/` currently covers PJINTL only.

## 8. Honest assessment

### 8.1 What the resources can specify

1. **All 11 legacy/OLE dialogs, exactly** — controls, ids, captions,
   geometry, styles. This is genuinely a spec you could implement from.
2. **The localisation corpus** — 19,792 keyed strings covering menus,
   commands, buttons, column captions, field help and validation messages.
   Even without bindings, this is what the app says; it removes textual
   guesswork.
3. **The modern UI vocabulary** — names such as `FilterMenuGroup`,
   `TaskBoardCustomizeCards`, `ImportProjectWizardPane`,
   `CompareProjectsLegendPane`, `ReportPane`, `GroupDS`, `BarLabelDialog`,
   plus 1,973 element/property identifiers (labels, data sources, commands,
   visibility flags, widths/heights).
4. **Which surfaces exist and roughly what they contain.** The inventory in
   §3 is a defensible map of the Office-style layer: filters (14 resources),
   groups/sorts (7), reports (4), board (4), Backstage (3), Save As (3),
   options (3), page setup (1), compare projects (2), import wizards (2),
   deliverables/comments/trust center.

### 8.2 What they cannot specify

1. **Layout geometry of anything modern.** Every `DefaultWidth`/
   `MinHeight`/`Title` name is present without a readable value. There is no
   DLGTEMPLATE for these surfaces. If you want the Reports pane to look
   right, you must observe it.
2. **The core views.** Across all 1,973 identifiers there is exactly one
   Gantt hit (`IsGanttChartView`, a print flag), zero `Timeline`, zero
   `Usage`, and no sheet/grid definition. The Task Sheet, Gantt chart,
   Usage views, Calendar drawing, Network diagram and Timeline are **native
   code, not resource-defined**. (This is consistent with §4: Project's
   dialogs are built in code, not from RT_DIALOG.)
3. **Interaction semantics** — what a click does, selection model, inline
   editing, drag/drop, keyboard map. Two UIFILEs embed short Lua handlers,
   but the behaviour of the main UI is in the 2.59 M lines of C++, not in
   resources.
4. **Binding of labels to controls.** The symbolic keys in UIFILE and the
   strings in RCDATA are not linked by anything decoded here. A control's
   visible caption therefore cannot yet be produced from resources alone.
5. **Menus/toolbars.** Despite 629 opaque RCDATA blobs and the plausible
   `00000000 …` command-table family, no menu or accelerator structure was
   proven. Treat "menus are in RCDATA" as unproven.

### 8.3 The number

If "the UI spec" means *every screen's layout, text and behaviour*, resources
give you **the text (roughly) and the inventory (partially), and almost none
of the layout or behaviour**. A defensible estimate:

- **Legacy dialogs: ~100% specifiable** (11 dialogs, but they are the least
  important ones).
- **Localised strings: ~90% of the corpus recoverable**, bindings missing.
- **Modern surfaces: ~30–40% specifiable** — enough to know they exist, what
  they are called, and many element names; not enough to build them.
- **Core sheet/Gantt/usage/calendar/network/timeline: <10%** — essentially
  nothing beyond field/table vocabularies.

So the honest answer to "how much of the UI can be specified from resources
rather than by staring at the app": **the words, yes; the pictures, mostly
no.** The resources are a strong *checklist and label source* and a poor
*layout source*. The plan must pair them with (a) decompiled UI code for
structure/behaviour and (b) screen capture of the running app for geometry,
spacing, colours and states.

## 9. Ruled out / open

Ruled out with evidence:

- Classic `DLGTEMPLATE` for the 11 dialogs — contradicted by dlgVer/signature
  and a zero item count under that reading (§4).
- Any literal `"NUB"`/`"LDXF"`/`"UIFILE"` string in `decompiled/*.c` —
  grep returns nothing, so the loader cannot be found by string reference;
  do not expect a readable parser there.
- UTF-16 user text inside UIFILE — checked all 71 (§2.2).
- RT_STRING as a label source — two resources only (§6).
- A simple `count + fixed records` model for every RCDATA — only 442/1071
  fit it; the rest need per-family work (§5b).

Open, in priority order:

1. **Decode the UIFILE value/opcode stream** (the region after the name
   pool, e.g. 4774 from 0x292). If it is a stack VM, one interpreter gets
   geometry for every DialogView/gallery. This is the highest-value unknown.
2. **Bind UIFILE symbolic keys to RCDATA/RT_STRING ids.** Look for a
   name→id map, or for the code that calls `LoadString` with these keys.
   Without it, captions are not specifiable.
3. **Classify the 629 opaque RCDATA blobs**, starting with the 5,516 B ×17
   set and the `00000000` family; decide menus/toolbars vs lookup tables.
4. Decode `winproj` RCDATA (112) — likely the command/toolbar layer.
5. Mine RT_HTML titles for a topic→dialog map.

## 10. Evidence index

- `analysis/res/pjintl/index.tsv:28–98` — UIFILE rows, sizes, first 24 bytes.
- `UIFILE/4767_1033.bin` — full 83-byte dump quoted in §2.
- `UIFILE/4774_1033.bin` — name pool 0x1a–0x292; binary stream after.
- `UIFILE/4838_1033.bin` — Lua 5.4 header at 0x1a.
- `RT_RCDATA/638_1033.bin` — `02be` count, record layout, `&Message` at 7030.
- `RT_RCDATA/431_1033.bin` — count 434, field help at 4342.
- `RT_RCDATA/136_1033.bin` — unindexed UTF-16 field list.
- `RT_DIALOG/32248_1033.bin` — DLGTEMPLATEEX with 19 items, "Insert Object".
- Generated: `docs/analysis/ui/{uifile_scan.txt,uifile_scan.tsv,
  uifile_inventory.tsv,dialogs.txt,dialogs.tsv,rcdata_scan.tsv,
  rcdata_strings.tsv}`.
- Grep claim (§9): `Select-String -Path decompiled\*.c -Pattern
  'LDXF|LuaT|OfficeCenterView'` → no matches.
