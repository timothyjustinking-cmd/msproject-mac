# Wave-14 oracle — the four write layouts (coordinator, 2026-10-22)

Coordinator-only COM run (`scripts/oracle_w14.py`), recovering the layouts the
wave-13 handoff listed as unmeasured: table columns, formula writing, assignment
custom fields, Cost custom fields, manual scheduling. Fixtures are synthetic
(`corpus/probe/w14-*`, git-ignored); committed JSONs carry numbers and the
invented names only.

Method: every case saves a **control** file first and the **edited** file second
in the same Project session, then `scripts/mpp_diff.py control target` isolates
the edit from Project's per-save identity churn. Two traps paid for here:

* `FileSaveAs` reports a padded size (`os.path.getsize` right after the call:
  344,064 / 360,448) but Project **compacts on close** — the file on disk is
  239,104. Only the post-close file is evidence. This explains the wave-13
  `w14-tableadd.json` "bytes 360448" against a 240,128-byte file.
* A crashed oracle run leaves `python` blocked in COM holding `.oracle.lock` and
  **WINPROJ holding `tests/data/synthetic-base.mpp` open** — the fixture then
  reads 238,080 bytes and cannot be deleted or written (Dropbox re-hydration
  blocks on the share violation). Recovery: `taskkill` both pids, remove
  `.oracle.lock`, then delete + `git checkout HEAD --` the fixture and verify
  SHA256 `55C109F2…`.

## 1. `214/CTable` — a column is a 115-byte record, and TableEdit REPLACES

The wave-13 call (`Create=True` + numeric `FieldName`) created a second table
("W14 Entry") with Entry's 9 stock columns and no new column. Why, measured:

| variant | args | result |
|---|---|---|
| D | `FieldName=188743767` (Long) | **error** `The field "188743767" does not exist.` — the argument is coerced to a *string* |
| A | `Create=False, Overwrite=False, FieldName="Number1"` | returns True, **no column**; table flags 6→2 only |
| B | `Create=False, Overwrite=True, FieldName="Number1"`, no ColumnPosition | returns True, **no column** |
| K | `Overwrite=True, FieldName="Text1"`, no ColumnPosition | **no column** |
| C | `Create=True, NewName="W14 Entry C"` | a **copy** of Entry (9 columns) |
| **I** | `Overwrite=True, FieldName="Number1", ColumnPosition=0` | **column written** |
| **J** | `Overwrite=True, FieldName="Text1", ColumnPosition=0` | **column written** |

I/J differ from the control in exactly one 115-byte column record. `Entry`
columns are the same 9 and the Var2Data block stays **1047 bytes**, so
`ColumnPosition=0` **overwrote the column at index 1** rather than appending:

```
col 0  0x0b400017  (ID)          header 061008010801000000
col 1  0x0b4000cd -> 0x0b400057  Number1  header 0c1008000801000000  title "Number1"
col 1  0x0b4000cd -> 0x0b400033  Text1    header 0c1008000801000000  title "Text1"
col 2  0x0b400500  (Duration)    091008000801000000
col 3  0x0b40000e  Task Name     181008000803000000
col 4  0x0b400507  Start         0d1008000801000000
col 5  0x0b400505 / col 6 0x0b400506   0d1008000801000000
col 7  0x0b40002f / col 8 0x0b400031   111008000801000000
```

Measured: the field id (`u32` at +0), the UTF-16 NUL-terminated title at +13
(capacity 51), and the 9 opaque header bytes — identical `0c1008000801000000`
for both a Number and a Text field, so byte +0 is not a value type. **Not**
measured: how to append (grow `last_column_index` at block +4 and the block
length). `ColumnPosition` 8 and 9 are rejected: `This is not a valid number for
the ColumnPosition argument.` — the accepted range is 0..7, and 0 means "replace
index 1". So the only *proved* column write is a replacement inside an existing
table definition; a writer must not claim append support.

Also measured: creating a second table appends one FixedData row (230 B), one
FixedMeta entry (10 B), one VarMeta entry `(uid, offset, key=0x20400006)` and one
Var2Data block — i.e. the generic `214/` collection framing, same as `114/`.

## 2. `114/Props` — formulas live in one record, id `0x2401390`

Control: `114/Props` 88,720 B, record `0x2401390` length **8**
(`08 00 00 00 00 00 00 00` = empty table). After
`CustomFieldSetFormula(0x0b400058, "[Duration]/240")` and
`(0x0b400036, "[Name]")` and a save: 88,864 B, the same record length **168**.

The net **+144 B** is three deltas, not one (W59 corrected this while writing the
encoder): the formula record grows **+160** (8 -> 168), the project-name record
`0x2400008` shrinks **-16** (42 -> 26, the two saves were named
`w14-formula-control` and `w14-formula`), and the rest is per-save identity
churn (`0x2400029`, `0x24013c2`) plus **record `0x02401392` (kind 2, u16)
flipping 0 -> 1**. That last record is a "custom-field data in use" flag, **not**
formula-specific: measured 0 in `synthetic-base`/`w14-formula-control`/probe-v1
and 1 in every fixture that stores custom-field *values* (`w10-fieldvalues`,
`w14-cost`, `w14-assignfield`) as well as in the two formula saves. A formula
writer must set it to 1 on set and 0 only when it actually removed formulas — a
no-op clear must leave it alone, and a project that still stores custom-field
values needs it left at 1 (`core/props` cannot see those streams, so the app
must not blind-clear). Record count stays 161; the tail moves with the stream and
differs in one typed-property byte at `tail+28` (0 -> 16), so it is copied
verbatim at its shifted offset.

Payload head (168 B): `a8 00 00 00` (168) `90 13 40 02` (id `0x02401390`)
`00 00 00 00` `a8 00 00 00` (168) `02 00 00 00` (count 2), then per entry the
field id (`0x0b400036`, `0x0b400058`) and a nested expression blob whose field
references are stored as `[MSPJ<u32 field id>]` UTF-16 tokens.

The reader already inverts this: `props_report --formulas` on the target prints

```
field=0x0b400036 ... formula=[0x0b40000e]        (Text2   = [Name])
field=0x0b400058 ... formula=[0x0b400507]/240    (Number2 = [Duration]/240)
```

and on the control prints `formulas: 0`. So W59 writes this record by inverting
`props::extract_formulas`; the empty form is the 8-byte record above.

## 3. Assignment custom fields — the VarMeta key is NOT the field id

Control vs edited (`w14-assignfield-control.mpp` → `w14-assignfield.mpp`), one
assignment, `a.Number1 = 7.5`, `a.Text1 = "assign-text"`:

| store | measured change |
|---|---|
| `114/TBkndAssn/VarMeta` | 9 → 11 entries: `(2, 176, 0x0f40403c)`, `(2, 188, 0x0f404064)` |
| `114/TBkndAssn/Var2Data` | +40 B: `08 00 00 00` + f64 **7.5** at 176; `18 00 00 00` + UTF-16 `"assign-text"` + NUL at 188 |
| `114/TBkndAssn/FixedMeta` | entry 1 `+2` var_count 9 → 11 (**no** tail presence bit changed) |
| `114/TBkndAssn/Props` | 88 → 264 B: two 88-byte registry entries |

**The key is not `0x0f40006c`/`0x0f400058`.** `Assignment.Number1` stores under
VarMeta key **`0x0f40403c`** and `Assignment.Text1` under **`0x0f404064`** — the
`0x0f4040xx` family that also appears as `0x0f40401e` inside a Props registry
entry. Writing the field id as the key would be silently ignored by Project.

`Assignment.SetField` does not exist in the typelib (only `Task`, `Resource`,
`Engagement`, `Issue`, `Risk` have it); the values were written through the
property and read back as 7.5 / `"assign-text"`.

## 4. Task `Cost1` — key is the field id, value is f64 ×100, plus a presence bit

`t.SetField(0x0b40006a, 123.45)` on task uid 1; `GetField` reads back
`"$123.45"`. Control vs edited:

| store | measured change |
|---|---|
| `114/TBkndTask/VarMeta` | 42 → 43 entries: `(1, 502, 0x0b40006a)` (key = field id) |
| `114/TBkndTask/Var2Data` | block at 502: `08 00 00 00` + f64 **12345.0** (the COM value × 100) |
| `114/TBkndTask/FixedMeta` | the uid-1 entry: `+2` var_count 11 → 12 and **`+26`: 0x00 → 0x08** (presence bitmap; Flag1 uses `+24` bit 0x02) |
| `114/TBkndTask/Props` | 86 → 174 B: one 88-byte registry entry |

The 88-byte registry entry is the same shape as the assignment one: after a
36-byte `{8,8,0}`-triple header and `u32 count`, `u32 entry_size=88`, the entry
carries the field id little-endian at +0, `5`, `0x0f40401e`, `14775`,
`0x4ceb8bbe`, `0x8cfac482`, the field id again **big-endian** at +24, then zeros.
W60 should copy Project's exact bytes rather than synthesise them.

## 5. Manual scheduling — a moved Start removes the pinned Start/Finish blocks

Task uid 1 was **already manual** in the base (`Manual=True` before the edit), so
this measures a *start move on a manual task*, not the manual bit turning on
(that bit was measured in wave 9). `t.Start = 2026-01-07 09:00`, control vs
edited:

* `114/TBkndTask/FixedData` uid 1 `+104..+119`: the eight date words move
  (start/finish/early/late); the project row (uid 0) moves with them.
* `114/TBkndTask/VarMeta`: **two entries removed** — `(1, 186, 0x0b400505)` and
  `(1, 192, 0x0b400506)`, i.e. the explicit Start and Finish VarMeta blocks are
  gone after the move; the dates are carried by FixedData alone.
* `114/TBkndTask/FixedMeta` uid-1 entry: `+2` var_count 0x0b → 0x09 and `+9`
  `0xbf` → `0xff`.
* `114/TBkndTask/Fixed2Meta` entry 4: `+88` `0x0c` → `0x00`.

So a manual pin is *not* a separate stored field: it is the absence of the
Start/Finish constraint blocks plus the FixedData date words. Consequence for the
app: there is no "manual pin" to write, only dates — and Project still clamps a
pinned date behind `max(pred finish, pin)` when the task is linked (audit
`docs/audit-handoff.md` §4.2). The app must therefore warn on the *edit* (it
cannot take effect), not try to store a pin.

## Files

* `scripts/oracle_w14.py` — the runnable oracle (cases above + the wave-13
  `tablecol`/`tablecol2`/`colinsert` probes).
* `scripts/extract_viewstate.py` — extract `114/`/`214/` streams for the CLIs.
* `docs/analysis/oracle/w14-{tableadd,tablecol,tablecol2,formula,assignfield,cost,manual,tableappend}.json`.
* `corpus/probe/w14-*-control.mpp` / `w14-*.mpp` (git-ignored): the fixture pairs
  every layout above is diffed from.

Re-run: `$env:W14_BASE = ...; python scripts/oracle_w14.py formula` (one Project
launch per case; never pass `/automation`; `FileClose(0)`).
